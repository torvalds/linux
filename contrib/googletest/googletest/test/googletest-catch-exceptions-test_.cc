// Copyright 2010, Google Inc.
// All rights reserved.
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
// Tests for Google Test itself. Tests in this file throw C++ or SEH
// exceptions, and the output is verified by
// googletest-catch-exceptions-test.py.

#include <stdio.h>  // NOLINT
#include <stdlib.h>  // For exit().

#include "gtest/gtest.h"

#if GTEST_HAS_SEH
# include <windows.h>
#endif

#if GTEST_HAS_EXCEPTIONS
# include <exception>  // For set_terminate().
# include <stdexcept>
#endif

using testing::Test;

#if GTEST_HAS_SEH

class SehExceptionInConstructorTest : public Test {
 public:
  SehExceptionInConstructorTest() { RaiseException(42, 0, 0, NULL); }
};

TEST_F(SehExceptionInConstructorTest, ThrowsExceptionInConstructor) {}

class SehExceptionInDestructorTest : public Test {
 public:
  ~SehExceptionInDestructorTest() { RaiseException(42, 0, 0, NULL); }
};

TEST_F(SehExceptionInDestructorTest, ThrowsExceptionInDestructor) {}

class SehExceptionInSetUpTestCaseTest : public Test {
 public:
  static void SetUpTestCase() { RaiseException(42, 0, 0, NULL); }
};

TEST_F(SehExceptionInSetUpTestCaseTest, ThrowsExceptionInSetUpTestCase) {}

class SehExceptionInTearDownTestCaseTest : public Test {
 public:
  static void TearDownTestCase() { RaiseException(42, 0, 0, NULL); }
};

TEST_F(SehExceptionInTearDownTestCaseTest, ThrowsExceptionInTearDownTestCase) {}

class SehExceptionInSetUpTest : public Test {
 protected:
  virtual void SetUp() { RaiseException(42, 0, 0, NULL); }
};

TEST_F(SehExceptionInSetUpTest, ThrowsExceptionInSetUp) {}

class SehExceptionInTearDownTest : public Test {
 protected:
  virtual void TearDown() { RaiseException(42, 0, 0, NULL); }
};

TEST_F(SehExceptionInTearDownTest, ThrowsExceptionInTearDown) {}

TEST(SehExceptionTest, ThrowsSehException) {
  RaiseException(42, 0, 0, NULL);
}

#endif  // GTEST_HAS_SEH

#if GTEST_HAS_EXCEPTIONS

class CxxExceptionInConstructorTest : public Test {
 public:
  CxxExceptionInConstructorTest() {
    // Without this macro VC++ complains about unreachable code at the end of
    // the constructor.
    GTEST_SUPPRESS_UNREACHABLE_CODE_WARNING_BELOW_(
        throw std::runtime_error("Standard C++ exception"));
  }

  static void TearDownTestCase() {
    printf("%s",
           "CxxExceptionInConstructorTest::TearDownTestCase() "
           "called as expected.\n");
  }

 protected:
  ~CxxExceptionInConstructorTest() {
    ADD_FAILURE() << "CxxExceptionInConstructorTest destructor "
                  << "called unexpectedly.";
  }

  virtual void SetUp() {
    ADD_FAILURE() << "CxxExceptionInConstructorTest::SetUp() "
                  << "called unexpectedly.";
  }

  virtual void TearDown() {
    ADD_FAILURE() << "CxxExceptionInConstructorTest::TearDown() "
                  << "called unexpectedly.";
  }
};

TEST_F(CxxExceptionInConstructorTest, ThrowsExceptionInConstructor) {
  ADD_FAILURE() << "CxxExceptionInConstructorTest test body "
                << "called unexpectedly.";
}

// Exceptions in destructors are not supported in C++11.
#if !GTEST_LANG_CXX11
class CxxExceptionInDestructorTest : public Test {
 public:
  static void TearDownTestCase() {
    printf("%s",
           "CxxExceptionInDestructorTest::TearDownTestCase() "
           "called as expected.\n");
  }

 protected:
  ~CxxExceptionInDestructorTest() {
    GTEST_SUPPRESS_UNREACHABLE_CODE_WARNING_BELOW_(
        throw std::runtime_error("Standard C++ exception"));
  }
};

TEST_F(CxxExceptionInDestructorTest, ThrowsExceptionInDestructor) {}
#endif  // C++11 mode

class CxxExceptionInSetUpTestCaseTest : public Test {
 public:
  CxxExceptionInSetUpTestCaseTest() {
    printf("%s",
           "CxxExceptionInSetUpTestCaseTest constructor "
           "called as expected.\n");
  }

  static void SetUpTestCase() {
    throw std::runtime_error("Standard C++ exception");
  }

  static void TearDownTestCase() {
    printf("%s",
           "CxxExceptionInSetUpTestCaseTest::TearDownTestCase() "
           "called as expected.\n");
  }

 protected:
  ~CxxExceptionInSetUpTestCaseTest() {
    printf("%s",
           "CxxExceptionInSetUpTestCaseTest destructor "
           "called as expected.\n");
  }

  virtual void SetUp() {
    printf("%s",
           "CxxExceptionInSetUpTestCaseTest::SetUp() "
           "called as expected.\n");
  }

  virtual void TearDown() {
    printf("%s",
           "CxxExceptionInSetUpTestCaseTest::TearDown() "
           "called as expected.\n");
  }
};

TEST_F(CxxExceptionInSetUpTestCaseTest, ThrowsExceptionInSetUpTestCase) {
  printf("%s",
         "CxxExceptionInSetUpTestCaseTest test body "
         "called as expected.\n");
}

class CxxExceptionInTearDownTestCaseTest : public Test {
 public:
  static void TearDownTestCase() {
    throw std::runtime_error("Standard C++ exception");
  }
};

TEST_F(CxxExceptionInTearDownTestCaseTest, ThrowsExceptionInTearDownTestCase) {}

class CxxExceptionInSetUpTest : public Test {
 public:
  static void TearDownTestCase() {
    printf("%s",
           "CxxExceptionInSetUpTest::TearDownTestCase() "
           "called as expected.\n");
  }

 protected:
  ~CxxExceptionInSetUpTest() {
    printf("%s",
           "CxxExceptionInSetUpTest destructor "
           "called as expected.\n");
  }

  virtual void SetUp() { throw std::runtime_error("Standard C++ exception"); }

  virtual void TearDown() {
    printf("%s",
           "CxxExceptionInSetUpTest::TearDown() "
           "called as expected.\n");
  }
};

TEST_F(CxxExceptionInSetUpTest, ThrowsExceptionInSetUp) {
  ADD_FAILURE() << "CxxExceptionInSetUpTest test body "
                << "called unexpectedly.";
}

class CxxExceptionInTearDownTest : public Test {
 public:
  static void TearDownTestCase() {
    printf("%s",
           "CxxExceptionInTearDownTest::TearDownTestCase() "
           "called as expected.\n");
  }

 protected:
  ~CxxExceptionInTearDownTest() {
    printf("%s",
           "CxxExceptionInTearDownTest destructor "
           "called as expected.\n");
  }

  virtual void TearDown() {
    throw std::runtime_error("Standard C++ exception");
  }
};

TEST_F(CxxExceptionInTearDownTest, ThrowsExceptionInTearDown) {}

class CxxExceptionInTestBodyTest : public Test {
 public:
  static void TearDownTestCase() {
    printf("%s",
           "CxxExceptionInTestBodyTest::TearDownTestCase() "
           "called as expected.\n");
  }

 protected:
  ~CxxExceptionInTestBodyTest() {
    printf("%s",
           "CxxExceptionInTestBodyTest destructor "
           "called as expected.\n");
  }

  virtual void TearDown() {
    printf("%s",
           "CxxExceptionInTestBodyTest::TearDown() "
           "called as expected.\n");
  }
};

TEST_F(CxxExceptionInTestBodyTest, ThrowsStdCxxException) {
  throw std::runtime_error("Standard C++ exception");
}

TEST(CxxExceptionTest, ThrowsNonStdCxxException) {
  throw "C-string";
}

// This terminate handler aborts the program using exit() rather than abort().
// This avoids showing pop-ups on Windows systems and core dumps on Unix-like
// ones.
void TerminateHandler() {
  fprintf(stderr, "%s\n", "Unhandled C++ exception terminating the program.");
  fflush(NULL);
  exit(3);
}

#endif  // GTEST_HAS_EXCEPTIONS

int main(int argc, char** argv) {
#if GTEST_HAS_EXCEPTIONS
  std::set_terminate(&TerminateHandler);
#endif
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
