//===- ISectionContribVisitor.h ---------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_RAW_ISECTIONCONTRIBVISITOR_H
#define LLVM_DEBUGINFO_PDB_RAW_ISECTIONCONTRIBVISITOR_H

namespace llvm {
namespace pdb {

struct SectionContrib;
struct SectionContrib2;

class ISectionContribVisitor {
public:
  virtual ~ISectionContribVisitor() = default;

  virtual void visit(const SectionContrib &C) = 0;
  virtual void visit(const SectionContrib2 &C) = 0;
};

} // end namespace pdb
} // end namespace llvm

#endif // LLVM_DEBUGINFO_PDB_RAW_ISECTIONCONTRIBVISITOR_H
