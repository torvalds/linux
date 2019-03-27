// Copyright 2008, Google Inc.
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
// Author: preston.a.jackson@gmail.com (Preston Jackson)
//
// Google Test - FrameworkSample
// widget_test.cc
//

// This is a simple test file for the Widget class in the Widget.framework

#include <string>
#include "gtest/gtest.h"

#include <Widget/widget.h>

// This test verifies that the constructor sets the internal state of the
// Widget class correctly.
TEST(WidgetInitializerTest, TestConstructor) {
  Widget widget(1.0f, "name");
  EXPECT_FLOAT_EQ(1.0f, widget.GetFloatValue());
  EXPECT_EQ(std::string("name"), widget.GetStringValue());
}

// This test verifies the conversion of the float and string values to int and
// char*, respectively.
TEST(WidgetInitializerTest, TestConversion) {
  Widget widget(1.0f, "name");
  EXPECT_EQ(1, widget.GetIntValue());

  size_t max_size = 128;
  char buffer[max_size];
  widget.GetCharPtrValue(buffer, max_size);
  EXPECT_STREQ("name", buffer);
}

// Use the Google Test main that is linked into the framework. It does something
// like this:
// int main(int argc, char** argv) {
//   testing::InitGoogleTest(&argc, argv);
//   return RUN_ALL_TESTS();
// }
