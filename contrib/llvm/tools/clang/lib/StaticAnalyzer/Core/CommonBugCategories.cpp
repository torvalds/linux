//=--- CommonBugCategories.cpp - Provides common issue categories -*- C++ -*-=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Core/BugReporter/CommonBugCategories.h"

// Common strings used for the "category" of many static analyzer issues.
namespace clang { namespace ento { namespace categories {

const char * const CoreFoundationObjectiveC = "Core Foundation/Objective-C";
const char * const LogicError = "Logic error";
const char * const MemoryRefCount =
  "Memory (Core Foundation/Objective-C/OSObject)";
const char * const MemoryError = "Memory error";
const char * const UnixAPI = "Unix API";
}}}
