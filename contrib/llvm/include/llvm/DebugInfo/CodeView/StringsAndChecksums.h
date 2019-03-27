//===- StringsAndChecksums.h ------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_STRINGSANDCHECKSUMS_H
#define LLVM_DEBUGINFO_CODEVIEW_STRINGSANDCHECKSUMS_H

#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/CodeView/DebugChecksumsSubsection.h"
#include "llvm/DebugInfo/CodeView/DebugStringTableSubsection.h"
#include "llvm/DebugInfo/CodeView/DebugSubsectionRecord.h"
#include <memory>

namespace llvm {
namespace codeview {

class StringsAndChecksumsRef {
public:
  // If no subsections are known about initially, we find as much as we can.
  StringsAndChecksumsRef();

  // If only a string table subsection is given, we find a checksums subsection.
  explicit StringsAndChecksumsRef(const DebugStringTableSubsectionRef &Strings);

  // If both subsections are given, we don't need to find anything.
  StringsAndChecksumsRef(const DebugStringTableSubsectionRef &Strings,
                         const DebugChecksumsSubsectionRef &Checksums);

  void setStrings(const DebugStringTableSubsectionRef &Strings);
  void setChecksums(const DebugChecksumsSubsectionRef &CS);

  void reset();
  void resetStrings();
  void resetChecksums();

  template <typename T> void initialize(T &&FragmentRange) {
    for (const DebugSubsectionRecord &R : FragmentRange) {
      if (Strings && Checksums)
        return;
      if (R.kind() == DebugSubsectionKind::FileChecksums) {
        initializeChecksums(R);
        continue;
      }
      if (R.kind() == DebugSubsectionKind::StringTable && !Strings) {
        // While in practice we should never encounter a string table even
        // though the string table is already initialized, in theory it's
        // possible.  PDBs are supposed to have one global string table and
        // then this subsection should not appear.  Whereas object files are
        // supposed to have this subsection appear exactly once.  However,
        // for testing purposes it's nice to be able to test this subsection
        // independently of one format or the other, so for some tests we
        // manually construct a PDB that contains this subsection in addition
        // to a global string table.
        initializeStrings(R);
        continue;
      }
    }
  }

  const DebugStringTableSubsectionRef &strings() const { return *Strings; }
  const DebugChecksumsSubsectionRef &checksums() const { return *Checksums; }

  bool hasStrings() const { return Strings != nullptr; }
  bool hasChecksums() const { return Checksums != nullptr; }

private:
  void initializeStrings(const DebugSubsectionRecord &SR);
  void initializeChecksums(const DebugSubsectionRecord &FCR);

  std::shared_ptr<DebugStringTableSubsectionRef> OwnedStrings;
  std::shared_ptr<DebugChecksumsSubsectionRef> OwnedChecksums;

  const DebugStringTableSubsectionRef *Strings = nullptr;
  const DebugChecksumsSubsectionRef *Checksums = nullptr;
};

class StringsAndChecksums {
public:
  using StringsPtr = std::shared_ptr<DebugStringTableSubsection>;
  using ChecksumsPtr = std::shared_ptr<DebugChecksumsSubsection>;

  // If no subsections are known about initially, we find as much as we can.
  StringsAndChecksums() = default;

  void setStrings(const StringsPtr &SP) { Strings = SP; }
  void setChecksums(const ChecksumsPtr &CP) { Checksums = CP; }

  const StringsPtr &strings() const { return Strings; }
  const ChecksumsPtr &checksums() const { return Checksums; }

  bool hasStrings() const { return Strings != nullptr; }
  bool hasChecksums() const { return Checksums != nullptr; }

private:
  StringsPtr Strings;
  ChecksumsPtr Checksums;
};

} // end namespace codeview
} // end namespace llvm

#endif // LLVM_DEBUGINFO_CODEVIEW_STRINGSANDCHECKSUMS_H
