//===- YAMLOutputStyle.h -------------------------------------- *- C++ --*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVMPDBDUMP_YAMLOUTPUTSTYLE_H
#define LLVM_TOOLS_LLVMPDBDUMP_YAMLOUTPUTSTYLE_H

#include "OutputStyle.h"
#include "PdbYaml.h"

#include "llvm/Support/ScopedPrinter.h"
#include "llvm/Support/YAMLTraits.h"

namespace llvm {
namespace pdb {
class ModuleDebugStreamRef;

class YAMLOutputStyle : public OutputStyle {
public:
  YAMLOutputStyle(PDBFile &File);

  Error dump() override;

private:
  Error dumpStringTable();
  Error dumpFileHeaders();
  Error dumpStreamMetadata();
  Error dumpStreamDirectory();
  Error dumpPDBStream();
  Error dumpDbiStream();
  Error dumpTpiStream();
  Error dumpIpiStream();
  Error dumpPublics();

  void flush();

  PDBFile &File;
  llvm::yaml::Output Out;

  yaml::PdbObject Obj;
};
} // namespace pdb
} // namespace llvm

#endif // LLVM_TOOLS_LLVMPDBDUMP_YAMLOUTPUTSTYLE_H
