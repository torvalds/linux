//== SummaryManager.h - Generic handling of function summaries --*- C++ -*--==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines SummaryManager and related classes, which provides
//  a generic mechanism for managing function summaries.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_GR_SUMMARY
#define LLVM_CLANG_GR_SUMMARY

namespace clang {

namespace ento {

namespace summMgr {


/* Key kinds:

 - C functions
 - C++ functions (name + parameter types)
 - ObjC methods:
   - Class, selector (class method)
   - Class, selector (instance method)
   - Category, selector (instance method)
   - Protocol, selector (instance method)
 - C++ methods
  - Class, function name + parameter types + const
 */

class SummaryKey {

};

} // end namespace clang::summMgr

class SummaryManagerImpl {

};


template <typename T>
class SummaryManager : SummaryManagerImpl {

};

} // end GR namespace

} // end clang namespace

#endif
