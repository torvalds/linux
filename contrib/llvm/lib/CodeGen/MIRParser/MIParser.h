//===- MIParser.h - Machine Instructions Parser -----------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares the function that parses the machine instructions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_CODEGEN_MIRPARSER_MIPARSER_H
#define LLVM_LIB_CODEGEN_MIRPARSER_MIPARSER_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/Allocator.h"

namespace llvm {

class MachineBasicBlock;
class MachineFunction;
class MDNode;
class RegisterBank;
struct SlotMapping;
class SMDiagnostic;
class SourceMgr;
class StringRef;
class TargetRegisterClass;

struct VRegInfo {
  enum uint8_t {
    UNKNOWN, NORMAL, GENERIC, REGBANK
  } Kind = UNKNOWN;
  bool Explicit = false; ///< VReg was explicitly specified in the .mir file.
  union {
    const TargetRegisterClass *RC;
    const RegisterBank *RegBank;
  } D;
  unsigned VReg;
  unsigned PreferredReg = 0;
};

using Name2RegClassMap = StringMap<const TargetRegisterClass *>;
using Name2RegBankMap = StringMap<const RegisterBank *>;

struct PerFunctionMIParsingState {
  BumpPtrAllocator Allocator;
  MachineFunction &MF;
  SourceMgr *SM;
  const SlotMapping &IRSlots;
  const Name2RegClassMap &Names2RegClasses;
  const Name2RegBankMap &Names2RegBanks;

  DenseMap<unsigned, MachineBasicBlock *> MBBSlots;
  DenseMap<unsigned, VRegInfo*> VRegInfos;
  StringMap<VRegInfo*> VRegInfosNamed;
  DenseMap<unsigned, int> FixedStackObjectSlots;
  DenseMap<unsigned, int> StackObjectSlots;
  DenseMap<unsigned, unsigned> ConstantPoolSlots;
  DenseMap<unsigned, unsigned> JumpTableSlots;

  PerFunctionMIParsingState(MachineFunction &MF, SourceMgr &SM,
                            const SlotMapping &IRSlots,
                            const Name2RegClassMap &Names2RegClasses,
                            const Name2RegBankMap &Names2RegBanks);

  VRegInfo &getVRegInfo(unsigned Num);
  VRegInfo &getVRegInfoNamed(StringRef RegName);
};

/// Parse the machine basic block definitions, and skip the machine
/// instructions.
///
/// This function runs the first parsing pass on the machine function's body.
/// It parses only the machine basic block definitions and creates the machine
/// basic blocks in the given machine function.
///
/// The machine instructions aren't parsed during the first pass because all
/// the machine basic blocks aren't defined yet - this makes it impossible to
/// resolve the machine basic block references.
///
/// Return true if an error occurred.
bool parseMachineBasicBlockDefinitions(PerFunctionMIParsingState &PFS,
                                       StringRef Src, SMDiagnostic &Error);

/// Parse the machine instructions.
///
/// This function runs the second parsing pass on the machine function's body.
/// It skips the machine basic block definitions and parses only the machine
/// instructions and basic block attributes like liveins and successors.
///
/// The second parsing pass assumes that the first parsing pass already ran
/// on the given source string.
///
/// Return true if an error occurred.
bool parseMachineInstructions(PerFunctionMIParsingState &PFS, StringRef Src,
                              SMDiagnostic &Error);

bool parseMBBReference(PerFunctionMIParsingState &PFS,
                       MachineBasicBlock *&MBB, StringRef Src,
                       SMDiagnostic &Error);

bool parseRegisterReference(PerFunctionMIParsingState &PFS,
                            unsigned &Reg, StringRef Src,
                            SMDiagnostic &Error);

bool parseNamedRegisterReference(PerFunctionMIParsingState &PFS, unsigned &Reg,
                                 StringRef Src, SMDiagnostic &Error);

bool parseVirtualRegisterReference(PerFunctionMIParsingState &PFS,
                                   VRegInfo *&Info, StringRef Src,
                                   SMDiagnostic &Error);

bool parseStackObjectReference(PerFunctionMIParsingState &PFS, int &FI,
                               StringRef Src, SMDiagnostic &Error);

bool parseMDNode(PerFunctionMIParsingState &PFS, MDNode *&Node, StringRef Src,
                 SMDiagnostic &Error);

} // end namespace llvm

#endif // LLVM_LIB_CODEGEN_MIRPARSER_MIPARSER_H
