//===-- TaggedASTType.h -----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_TaggedASTType_h_
#define liblldb_TaggedASTType_h_

#include "lldb/Symbol/CompilerType.h"

namespace lldb_private {

// For cases in which there are multiple classes of types that are not
// interchangeable, to allow static type checking.
template <unsigned int C> class TaggedASTType : public CompilerType {
public:
  TaggedASTType(const CompilerType &compiler_type)
      : CompilerType(compiler_type) {}

  TaggedASTType(lldb::opaque_compiler_type_t type, TypeSystem *type_system)
      : CompilerType(type_system, type) {}

  TaggedASTType(const TaggedASTType<C> &tw) : CompilerType(tw) {}

  TaggedASTType() : CompilerType() {}

  virtual ~TaggedASTType() {}

  TaggedASTType<C> &operator=(const TaggedASTType<C> &tw) {
    CompilerType::operator=(tw);
    return *this;
  }
};

// Commonly-used tagged types, so code using them is interoperable
typedef TaggedASTType<0> TypeFromParser;
typedef TaggedASTType<1> TypeFromUser;
}

#endif
