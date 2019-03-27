//===-------------------------- CodeRegion.h -------------------*- C++ -* -===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file implements class CodeRegion and CodeRegions.
///
/// A CodeRegion describes a region of assembly code guarded by special LLVM-MCA
/// comment directives.
///
///   # LLVM-MCA-BEGIN foo
///     ...  ## asm
///   # LLVM-MCA-END
///
/// A comment starting with substring LLVM-MCA-BEGIN marks the beginning of a
/// new region of code.
/// A comment starting with substring LLVM-MCA-END marks the end of the
/// last-seen region of code.
///
/// Code regions are not allowed to overlap. Each region can have a optional
/// description; internally, regions are described by a range of source
/// locations (SMLoc objects).
///
/// An instruction (a MCInst) is added to a region R only if its location is in
/// range [R.RangeStart, R.RangeEnd].
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_MCA_CODEREGION_H
#define LLVM_TOOLS_LLVM_MCA_CODEREGION_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/MC/MCInst.h"
#include "llvm/Support/SMLoc.h"
#include "llvm/Support/SourceMgr.h"
#include <vector>

namespace llvm {
namespace mca {

/// A region of assembly code.
///
/// It identifies a sequence of machine instructions.
class CodeRegion {
  // An optional descriptor for this region.
  llvm::StringRef Description;
  // Instructions that form this region.
  std::vector<llvm::MCInst> Instructions;
  // Source location range.
  llvm::SMLoc RangeStart;
  llvm::SMLoc RangeEnd;

  CodeRegion(const CodeRegion &) = delete;
  CodeRegion &operator=(const CodeRegion &) = delete;

public:
  CodeRegion(llvm::StringRef Desc, llvm::SMLoc Start)
      : Description(Desc), RangeStart(Start), RangeEnd() {}

  void addInstruction(const llvm::MCInst &Instruction) {
    Instructions.emplace_back(Instruction);
  }

  llvm::SMLoc startLoc() const { return RangeStart; }
  llvm::SMLoc endLoc() const { return RangeEnd; }

  void setEndLocation(llvm::SMLoc End) { RangeEnd = End; }
  bool empty() const { return Instructions.empty(); }
  bool isLocInRange(llvm::SMLoc Loc) const;

  llvm::ArrayRef<llvm::MCInst> getInstructions() const { return Instructions; }

  llvm::StringRef getDescription() const { return Description; }
};

class CodeRegions {
  // A source manager. Used by the tool to generate meaningful warnings.
  llvm::SourceMgr &SM;

  std::vector<std::unique_ptr<CodeRegion>> Regions;

  // Construct a new region of code guarded by LLVM-MCA comments.
  void addRegion(llvm::StringRef Description, llvm::SMLoc Loc) {
    Regions.emplace_back(llvm::make_unique<CodeRegion>(Description, Loc));
  }

  CodeRegions(const CodeRegions &) = delete;
  CodeRegions &operator=(const CodeRegions &) = delete;

public:
  typedef std::vector<std::unique_ptr<CodeRegion>>::iterator iterator;
  typedef std::vector<std::unique_ptr<CodeRegion>>::const_iterator
      const_iterator;

  iterator begin() { return Regions.begin(); }
  iterator end() { return Regions.end(); }
  const_iterator begin() const { return Regions.cbegin(); }
  const_iterator end() const { return Regions.cend(); }

  void beginRegion(llvm::StringRef Description, llvm::SMLoc Loc);
  void endRegion(llvm::SMLoc Loc);
  void addInstruction(const llvm::MCInst &Instruction);
  llvm::SourceMgr &getSourceMgr() const { return SM; }

  CodeRegions(llvm::SourceMgr &S) : SM(S) {
    // Create a default region for the input code sequence.
    addRegion("Default", llvm::SMLoc());
  }

  llvm::ArrayRef<llvm::MCInst> getInstructionSequence(unsigned Idx) const {
    return Regions[Idx]->getInstructions();
  }

  bool empty() const {
    return llvm::all_of(Regions, [](const std::unique_ptr<CodeRegion> &Region) {
      return Region->empty();
    });
  }
};

} // namespace mca
} // namespace llvm

#endif
