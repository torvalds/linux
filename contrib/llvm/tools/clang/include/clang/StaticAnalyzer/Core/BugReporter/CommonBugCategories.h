//=--- CommonBugCategories.h - Provides common issue categories -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_STATICANALYZER_CORE_BUGREPORTER_COMMONBUGCATEGORIES_H
#define LLVM_CLANG_STATICANALYZER_CORE_BUGREPORTER_COMMONBUGCATEGORIES_H

// Common strings used for the "category" of many static analyzer issues.
namespace clang {
  namespace ento {
    namespace categories {
      extern const char * const CoreFoundationObjectiveC;
      extern const char * const LogicError;
      extern const char * const MemoryRefCount;
      extern const char * const MemoryError;
      extern const char * const UnixAPI;
    }
  }
}
#endif

