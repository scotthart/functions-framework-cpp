// Copyright 2020 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "google/cloud/functions/internal/parse_cloud_event_http.h"
#include <gmock/gmock.h>
#include <algorithm>
#include <iterator>

namespace google::cloud::functions_internal {
inline namespace FUNCTIONS_FRAMEWORK_CPP_NS {
namespace {

using ::testing::ElementsAre;

auto TestBeastRequest() {
  BeastRequest request;
  request.insert("ce-type", "com.example.someevent");
  request.insert("ce-source", "/mycontext");
  request.insert("ce-id", "A234-1234-1234");
  return request;
}

TEST(ParseCloudEventHttp, Basic) {
  auto request = TestBeastRequest();
  auto ce = ParseCloudEventHttpBinary(request);
  EXPECT_EQ(ce.id(), "A234-1234-1234");
  EXPECT_EQ(ce.source(), "/mycontext");
  EXPECT_EQ(ce.type(), "com.example.someevent");
  EXPECT_EQ(ce.spec_version(), functions::CloudEvent::kDefaultSpecVersion);
}

TEST(ParseCloudEventHttp, WithSpecVersion) {
  auto request = TestBeastRequest();
  request.insert("ce-specversion", "1.1");
  auto ce = ParseCloudEventHttpBinary(request);
  EXPECT_EQ(ce.id(), "A234-1234-1234");
  EXPECT_EQ(ce.source(), "/mycontext");
  EXPECT_EQ(ce.type(), "com.example.someevent");
  EXPECT_EQ(ce.spec_version(), "1.1");
}

TEST(ParseCloudEventHttp, MissingRequiredFields) {
  for (auto const* field : {"ce-type", "ce-source", "ce-id"}) {
    auto request = TestBeastRequest();
    request.erase(field);
    EXPECT_THROW(ParseCloudEventHttpBinary(request), std::exception)
        << " field=" << field;
  }
}

TEST(ParseCloudEventHttp, WithCloudEventDataContentType) {
  auto request = TestBeastRequest();
  request.insert("ce-datacontenttype", "text/plain");
  auto ce = ParseCloudEventHttpBinary(request);
  EXPECT_EQ(ce.data_content_type().value_or(""), "text/plain");
}

TEST(ParseCloudEventHttp, WithContentType) {
  auto request = TestBeastRequest();
  request.insert("content-type", "text/plain");
  auto ce = ParseCloudEventHttpBinary(request);
  EXPECT_EQ(ce.data_content_type().value_or(""), "text/plain");
}

TEST(ParseCloudEventHttp, MismatchedContentTypes) {
  auto request = TestBeastRequest();
  request.insert("ce-datacontenttype", "text/plain");
  request.insert("content-type", "application/json");
  EXPECT_THROW(ParseCloudEventHttpBinary(request), std::invalid_argument);
}

TEST(ParseCloudEventHttp, WithDataSchema) {
  auto request = TestBeastRequest();
  request.insert("ce-dataschema", "test-dataschema");
  auto ce = ParseCloudEventHttpBinary(request);
  EXPECT_EQ(ce.data_schema().value_or(""), "test-dataschema");
}

TEST(ParseCloudEventHttp, WithSubject) {
  auto request = TestBeastRequest();
  request.insert("ce-subject", "test-subject");
  auto ce = ParseCloudEventHttpBinary(request);
  EXPECT_EQ(ce.subject().value_or(""), "test-subject");
}

TEST(ParseCloudEventHttp, WithTime) {
  // obtained using: date -u --date='2018-04-05T17:31:05Z' +%s
  auto constexpr kExpectedTime = std::chrono::seconds(1522949465L);

  auto request = TestBeastRequest();
  request.insert("ce-time", "2018-04-05T17:31:05Z");
  auto ce = ParseCloudEventHttpBinary(request);
  auto tp = ce.time().value_or(std::chrono::system_clock::from_time_t(0));
  // This assumes the platform uses a unix epoch, reasonably safe assumption for
  // our needs.
  EXPECT_EQ(tp, std::chrono::system_clock::from_time_t(kExpectedTime.count()));
}

TEST(ParseCloudEventHttp, WithData) {
  auto request = TestBeastRequest();
  request.body() = "Hello World\n";
  request.prepare_payload();
  auto ce = ParseCloudEventHttpBinary(request);
  EXPECT_EQ(ce.data().value_or(""), "Hello World\n");
}

TEST(ParseCloudEventHttp, WithoutData) {
  auto request = TestBeastRequest();
  request.prepare_payload();
  auto ce = ParseCloudEventHttpBinary(request);
  EXPECT_FALSE(ce.data().has_value());
}

TEST(ParseCloudEventHttp, Json) {
  auto constexpr kText = R"js({
    "type" : "com.example.someevent",
    "source" : "/mycontext",
    "id" : "A234-1234-1234"})js";
  BeastRequest request;
  request.insert("content-type", "application/cloudevents+json; charset=utf-8");
  request.body() = kText;
  request.prepare_payload();
  auto events = ParseCloudEventHttp(request);
  ASSERT_THAT(events.size(), 1);
  auto const& ce = events[0];
  EXPECT_EQ(ce.id(), "A234-1234-1234");
  EXPECT_EQ(ce.source(), "/mycontext");
  EXPECT_EQ(ce.type(), "com.example.someevent");
  EXPECT_EQ(ce.spec_version(), functions::CloudEvent::kDefaultSpecVersion);
}

TEST(ParseCloudEventHttp, JsonBatch) {
  auto constexpr kText = R"js([
  {
    "type" : "com.example.someevent",
    "source" : "/mycontext",
    "id" : "A234-1234-1234-0"
  },
  {
    "type" : "com.example.someevent",
    "source" : "/mycontext",
    "id" : "A234-1234-1234-1"
  },
  {
    "type" : "com.example.someevent",
    "source" : "/mycontext",
    "id" : "A234-1234-1234-2"
  }
  ])js";
  BeastRequest request;
  request.insert("content-type",
                 "application/cloudevents-batch+json; charset=utf-8");
  request.body() = kText;
  auto const events = ParseCloudEventHttp(request);
  std::vector<std::string> ids;
  std::transform(events.begin(), events.end(), std::back_inserter(ids),
                 [](auto ce) { return ce.id(); });
  EXPECT_THAT(ids, ElementsAre("A234-1234-1234-0", "A234-1234-1234-1",
                               "A234-1234-1234-2"));
}

TEST(ParseCloudEventHttp, Binary) {
  auto request = TestBeastRequest();
  request.prepare_payload();
  auto events = ParseCloudEventHttp(request);
  ASSERT_THAT(events.size(), 1);
  auto const& ce = events[0];
  EXPECT_EQ(ce.id(), "A234-1234-1234");
  EXPECT_EQ(ce.source(), "/mycontext");
  EXPECT_EQ(ce.type(), "com.example.someevent");
  EXPECT_EQ(ce.spec_version(), functions::CloudEvent::kDefaultSpecVersion);
}

TEST(ParseCloudEventHttp, BinaryWithUnknownContentType) {
  auto request = TestBeastRequest();
  request.insert("content-type", "application/cloudevents+avro");
  request.prepare_payload();
  auto events = ParseCloudEventHttp(request);
  ASSERT_THAT(events.size(), 1);
  auto const& ce = events[0];
  EXPECT_EQ(ce.id(), "A234-1234-1234");
  EXPECT_EQ(ce.source(), "/mycontext");
  EXPECT_EQ(ce.type(), "com.example.someevent");
  EXPECT_EQ(ce.spec_version(), functions::CloudEvent::kDefaultSpecVersion);
}

}  // namespace
}  // namespace FUNCTIONS_FRAMEWORK_CPP_NS
}  // namespace google::cloud::functions_internal