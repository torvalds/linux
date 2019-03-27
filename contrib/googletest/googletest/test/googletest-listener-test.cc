// Copyright 2009 Google Inc. All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

//
// The Google C++ Testing and Mocking Framework (Google Test)
//
// This file verifies Google Test event listeners receive events at the
// right times.

#include <vector>

#include "gtest/gtest.h"

using ::testing::AddGlobalTestEnvironment;
using ::testing::Environment;
using ::testing::InitGoogleTest;
using ::testing::Test;
using ::testing::TestCase;
using ::testing::TestEventListener;
using ::testing::TestInfo;
using ::testing::TestPartResult;
using ::testing::UnitTest;

// Used by tests to register their events.
std::vector<std::string>* g_events = NULL;

namespace testing {
namespace internal {

class EventRecordingListener : public TestEventListener {
 public:
  explicit EventRecordingListener(const char* name) : name_(name) {}

 protected:
  virtual void OnTestProgramStart(const UnitTest& /*unit_test*/) {
    g_events->push_back(GetFullMethodName("OnTestProgramStart"));
  }

  virtual void OnTestIterationStart(const UnitTest& /*unit_test*/,
                                    int iteration) {
    Message message;
    message << GetFullMethodName("OnTestIterationStart")
            << "(" << iteration << ")";
    g_events->push_back(message.GetString());
  }

  virtual void OnEnvironmentsSetUpStart(const UnitTest& /*unit_test*/) {
    g_events->push_back(GetFullMethodName("OnEnvironmentsSetUpStart"));
  }

  virtual void OnEnvironmentsSetUpEnd(const UnitTest& /*unit_test*/) {
    g_events->push_back(GetFullMethodName("OnEnvironmentsSetUpEnd"));
  }

  virtual void OnTestCaseStart(const TestCase& /*test_case*/) {
    g_events->push_back(GetFullMethodName("OnTestCaseStart"));
  }

  virtual void OnTestStart(const TestInfo& /*test_info*/) {
    g_events->push_back(GetFullMethodName("OnTestStart"));
  }

  virtual void OnTestPartResult(const TestPartResult& /*test_part_result*/) {
    g_events->push_back(GetFullMethodName("OnTestPartResult"));
  }

  virtual void OnTestEnd(const TestInfo& /*test_info*/) {
    g_events->push_back(GetFullMethodName("OnTestEnd"));
  }

  virtual void OnTestCaseEnd(const TestCase& /*test_case*/) {
    g_events->push_back(GetFullMethodName("OnTestCaseEnd"));
  }

  virtual void OnEnvironmentsTearDownStart(const UnitTest& /*unit_test*/) {
    g_events->push_back(GetFullMethodName("OnEnvironmentsTearDownStart"));
  }

  virtual void OnEnvironmentsTearDownEnd(const UnitTest& /*unit_test*/) {
    g_events->push_back(GetFullMethodName("OnEnvironmentsTearDownEnd"));
  }

  virtual void OnTestIterationEnd(const UnitTest& /*unit_test*/,
                                  int iteration) {
    Message message;
    message << GetFullMethodName("OnTestIterationEnd")
            << "("  << iteration << ")";
    g_events->push_back(message.GetString());
  }

  virtual void OnTestProgramEnd(const UnitTest& /*unit_test*/) {
    g_events->push_back(GetFullMethodName("OnTestProgramEnd"));
  }

 private:
  std::string GetFullMethodName(const char* name) {
    return name_ + "." + name;
  }

  std::string name_;
};

class EnvironmentInvocationCatcher : public Environment {
 protected:
  virtual void SetUp() {
    g_events->push_back("Environment::SetUp");
  }

  virtual void TearDown() {
    g_events->push_back("Environment::TearDown");
  }
};

class ListenerTest : public Test {
 protected:
  static void SetUpTestCase() {
    g_events->push_back("ListenerTest::SetUpTestCase");
  }

  static void TearDownTestCase() {
    g_events->push_back("ListenerTest::TearDownTestCase");
  }

  virtual void SetUp() {
    g_events->push_back("ListenerTest::SetUp");
  }

  virtual void TearDown() {
    g_events->push_back("ListenerTest::TearDown");
  }
};

TEST_F(ListenerTest, DoesFoo) {
  // Test execution order within a test case is not guaranteed so we are not
  // recording the test name.
  g_events->push_back("ListenerTest::* Test Body");
  SUCCEED();  // Triggers OnTestPartResult.
}

TEST_F(ListenerTest, DoesBar) {
  g_events->push_back("ListenerTest::* Test Body");
  SUCCEED();  // Triggers OnTestPartResult.
}

}  // namespace internal

}  // namespace testing

using ::testing::internal::EnvironmentInvocationCatcher;
using ::testing::internal::EventRecordingListener;

void VerifyResults(const std::vector<std::string>& data,
                   const char* const* expected_data,
                   size_t expected_data_size) {
  const size_t actual_size = data.size();
  // If the following assertion fails, a new entry will be appended to
  // data.  Hence we save data.size() first.
  EXPECT_EQ(expected_data_size, actual_size);

  // Compares the common prefix.
  const size_t shorter_size = expected_data_size <= actual_size ?
      expected_data_size : actual_size;
  size_t i = 0;
  for (; i < shorter_size; ++i) {
    ASSERT_STREQ(expected_data[i], data[i].c_str())
        << "at position " << i;
  }

  // Prints extra elements in the actual data.
  for (; i < actual_size; ++i) {
    printf("  Actual event #%lu: %s\n",
        static_cast<unsigned long>(i), data[i].c_str());
  }
}

int main(int argc, char **argv) {
  std::vector<std::string> events;
  g_events = &events;
  InitGoogleTest(&argc, argv);

  UnitTest::GetInstance()->listeners().Append(
      new EventRecordingListener("1st"));
  UnitTest::GetInstance()->listeners().Append(
      new EventRecordingListener("2nd"));

  AddGlobalTestEnvironment(new EnvironmentInvocationCatcher);

  GTEST_CHECK_(events.size() == 0)
      << "AddGlobalTestEnvironment should not generate any events itself.";

  ::testing::GTEST_FLAG(repeat) = 2;
  int ret_val = RUN_ALL_TESTS();

  const char* const expected_events[] = {
    "1st.OnTestProgramStart",
    "2nd.OnTestProgramStart",
    "1st.OnTestIterationStart(0)",
    "2nd.OnTestIterationStart(0)",
    "1st.OnEnvironmentsSetUpStart",
    "2nd.OnEnvironmentsSetUpStart",
    "Environment::SetUp",
    "2nd.OnEnvironmentsSetUpEnd",
    "1st.OnEnvironmentsSetUpEnd",
    "1st.OnTestCaseStart",
    "2nd.OnTestCaseStart",
    "ListenerTest::SetUpTestCase",
    "1st.OnTestStart",
    "2nd.OnTestStart",
    "ListenerTest::SetUp",
    "ListenerTest::* Test Body",
    "1st.OnTestPartResult",
    "2nd.OnTestPartResult",
    "ListenerTest::TearDown",
    "2nd.OnTestEnd",
    "1st.OnTestEnd",
    "1st.OnTestStart",
    "2nd.OnTestStart",
    "ListenerTest::SetUp",
    "ListenerTest::* Test Body",
    "1st.OnTestPartResult",
    "2nd.OnTestPartResult",
    "ListenerTest::TearDown",
    "2nd.OnTestEnd",
    "1st.OnTestEnd",
    "ListenerTest::TearDownTestCase",
    "2nd.OnTestCaseEnd",
    "1st.OnTestCaseEnd",
    "1st.OnEnvironmentsTearDownStart",
    "2nd.OnEnvironmentsTearDownStart",
    "Environment::TearDown",
    "2nd.OnEnvironmentsTearDownEnd",
    "1st.OnEnvironmentsTearDownEnd",
    "2nd.OnTestIterationEnd(0)",
    "1st.OnTestIterationEnd(0)",
    "1st.OnTestIterationStart(1)",
    "2nd.OnTestIterationStart(1)",
    "1st.OnEnvironmentsSetUpStart",
    "2nd.OnEnvironmentsSetUpStart",
    "Environment::SetUp",
    "2nd.OnEnvironmentsSetUpEnd",
    "1st.OnEnvironmentsSetUpEnd",
    "1st.OnTestCaseStart",
    "2nd.OnTestCaseStart",
    "ListenerTest::SetUpTestCase",
    "1st.OnTestStart",
    "2nd.OnTestStart",
    "ListenerTest::SetUp",
    "ListenerTest::* Test Body",
    "1st.OnTestPartResult",
    "2nd.OnTestPartResult",
    "ListenerTest::TearDown",
    "2nd.OnTestEnd",
    "1st.OnTestEnd",
    "1st.OnTestStart",
    "2nd.OnTestStart",
    "ListenerTest::SetUp",
    "ListenerTest::* Test Body",
    "1st.OnTestPartResult",
    "2nd.OnTestPartResult",
    "ListenerTest::TearDown",
    "2nd.OnTestEnd",
    "1st.OnTestEnd",
    "ListenerTest::TearDownTestCase",
    "2nd.OnTestCaseEnd",
    "1st.OnTestCaseEnd",
    "1st.OnEnvironmentsTearDownStart",
    "2nd.OnEnvironmentsTearDownStart",
    "Environment::TearDown",
    "2nd.OnEnvironmentsTearDownEnd",
    "1st.OnEnvironmentsTearDownEnd",
    "2nd.OnTestIterationEnd(1)",
    "1st.OnTestIterationEnd(1)",
    "2nd.OnTestProgramEnd",
    "1st.OnTestProgramEnd"
  };
  VerifyResults(events,
                expected_events,
                sizeof(expected_events)/sizeof(expected_events[0]));

  // We need to check manually for ad hoc test failures that happen after
  // RUN_ALL_TESTS finishes.
  if (UnitTest::GetInstance()->Failed())
    ret_val = 1;

  return ret_val;
}
