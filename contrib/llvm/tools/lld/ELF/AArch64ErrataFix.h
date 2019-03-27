//===- AArch64ErrataFix.h ---------------------------------------*- C++ -*-===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLD_ELF_AARCH64ERRATAFIX_H
#define LLD_ELF_AARCH64ERRATAFIX_H

#include "lld/Common/LLVM.h"
#include <map>
#include <vector>

namespace lld {
namespace elf {

class Defined;
class InputSection;
struct InputSectionDescription;
class OutputSection;
class Patch843419Section;

class AArch64Err843419Patcher {
public:
  // return true if Patches have been added to the OutputSections.
  bool createFixes();

private:
  std::vector<Patch843419Section *>
  patchInputSectionDescription(InputSectionDescription &ISD);

  void insertPatches(InputSectionDescription &ISD,
                     std::vector<Patch843419Section *> &Patches);

  void init();

  // A cache of the mapping symbols defined by the InputSecion sorted in order
  // of ascending value with redundant symbols removed. These describe
  // the ranges of code and data in an executable InputSection.
  std::map<InputSection *, std::vector<const Defined *>> SectionMap;

  bool Initialized = false;
};

} // namespace elf
} // namespace lld

#endif
