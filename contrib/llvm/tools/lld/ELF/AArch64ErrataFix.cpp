//===- AArch64ErrataFix.cpp -----------------------------------------------===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// This file implements Section Patching for the purpose of working around
// errata in CPUs. The general principle is that an erratum sequence of one or
// more instructions is detected in the instruction stream, one of the
// instructions in the sequence is replaced with a branch to a patch sequence
// of replacement instructions. At the end of the replacement sequence the
// patch branches back to the instruction stream.

// This technique is only suitable for fixing an erratum when:
// - There is a set of necessary conditions required to trigger the erratum that
// can be detected at static link time.
// - There is a set of replacement instructions that can be used to remove at
// least one of the necessary conditions that trigger the erratum.
// - We can overwrite an instruction in the erratum sequence with a branch to
// the replacement sequence.
// - We can place the replacement sequence within range of the branch.

// FIXME:
// - The implementation here only supports one patch, the AArch64 Cortex-53
// errata 843419 that affects r0p0, r0p1, r0p2 and r0p4 versions of the core.
// To keep the initial version simple there is no support for multiple
// architectures or selection of different patches.
//===----------------------------------------------------------------------===//

#include "AArch64ErrataFix.h"
#include "Config.h"
#include "LinkerScript.h"
#include "OutputSections.h"
#include "Relocations.h"
#include "Symbols.h"
#include "SyntheticSections.h"
#include "Target.h"
#include "lld/Common/Memory.h"
#include "lld/Common/Strings.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>

using namespace llvm;
using namespace llvm::ELF;
using namespace llvm::object;
using namespace llvm::support;
using namespace llvm::support::endian;

using namespace lld;
using namespace lld::elf;

// Helper functions to identify instructions and conditions needed to trigger
// the Cortex-A53-843419 erratum.

// ADRP
// | 1 | immlo (2) | 1 | 0 0 0 0 | immhi (19) | Rd (5) |
static bool isADRP(uint32_t Instr) {
  return (Instr & 0x9f000000) == 0x90000000;
}

// Load and store bit patterns from ARMv8-A ARM ARM.
// Instructions appear in order of appearance starting from table in
// C4.1.3 Loads and Stores.

// All loads and stores have 1 (at bit postion 27), (0 at bit position 25).
// | op0 x op1 (2) | 1 op2 0 op3 (2) | x | op4 (5) | xxxx | op5 (2) | x (10) |
static bool isLoadStoreClass(uint32_t Instr) {
  return (Instr & 0x0a000000) == 0x08000000;
}

// LDN/STN multiple no offset
// | 0 Q 00 | 1100 | 0 L 00 | 0000 | opcode (4) | size (2) | Rn (5) | Rt (5) |
// LDN/STN multiple post-indexed
// | 0 Q 00 | 1100 | 1 L 0 | Rm (5)| opcode (4) | size (2) | Rn (5) | Rt (5) |
// L == 0 for stores.

// Utility routine to decode opcode field of LDN/STN multiple structure
// instructions to find the ST1 instructions.
// opcode == 0010 ST1 4 registers.
// opcode == 0110 ST1 3 registers.
// opcode == 0111 ST1 1 register.
// opcode == 1010 ST1 2 registers.
static bool isST1MultipleOpcode(uint32_t Instr) {
  return (Instr & 0x0000f000) == 0x00002000 ||
         (Instr & 0x0000f000) == 0x00006000 ||
         (Instr & 0x0000f000) == 0x00007000 ||
         (Instr & 0x0000f000) == 0x0000a000;
}

static bool isST1Multiple(uint32_t Instr) {
  return (Instr & 0xbfff0000) == 0x0c000000 && isST1MultipleOpcode(Instr);
}

// Writes to Rn (writeback).
static bool isST1MultiplePost(uint32_t Instr) {
  return (Instr & 0xbfe00000) == 0x0c800000 && isST1MultipleOpcode(Instr);
}

// LDN/STN single no offset
// | 0 Q 00 | 1101 | 0 L R 0 | 0000 | opc (3) S | size (2) | Rn (5) | Rt (5)|
// LDN/STN single post-indexed
// | 0 Q 00 | 1101 | 1 L R | Rm (5) | opc (3) S | size (2) | Rn (5) | Rt (5)|
// L == 0 for stores

// Utility routine to decode opcode field of LDN/STN single structure
// instructions to find the ST1 instructions.
// R == 0 for ST1 and ST3, R == 1 for ST2 and ST4.
// opcode == 000 ST1 8-bit.
// opcode == 010 ST1 16-bit.
// opcode == 100 ST1 32 or 64-bit (Size determines which).
static bool isST1SingleOpcode(uint32_t Instr) {
  return (Instr & 0x0040e000) == 0x00000000 ||
         (Instr & 0x0040e000) == 0x00004000 ||
         (Instr & 0x0040e000) == 0x00008000;
}

static bool isST1Single(uint32_t Instr) {
  return (Instr & 0xbfff0000) == 0x0d000000 && isST1SingleOpcode(Instr);
}

// Writes to Rn (writeback).
static bool isST1SinglePost(uint32_t Instr) {
  return (Instr & 0xbfe00000) == 0x0d800000 && isST1SingleOpcode(Instr);
}

static bool isST1(uint32_t Instr) {
  return isST1Multiple(Instr) || isST1MultiplePost(Instr) ||
         isST1Single(Instr) || isST1SinglePost(Instr);
}

// Load/store exclusive
// | size (2) 00 | 1000 | o2 L o1 | Rs (5) | o0 | Rt2 (5) | Rn (5) | Rt (5) |
// L == 0 for Stores.
static bool isLoadStoreExclusive(uint32_t Instr) {
  return (Instr & 0x3f000000) == 0x08000000;
}

static bool isLoadExclusive(uint32_t Instr) {
  return (Instr & 0x3f400000) == 0x08400000;
}

// Load register literal
// | opc (2) 01 | 1 V 00 | imm19 | Rt (5) |
static bool isLoadLiteral(uint32_t Instr) {
  return (Instr & 0x3b000000) == 0x18000000;
}

// Load/store no-allocate pair
// (offset)
// | opc (2) 10 | 1 V 00 | 0 L | imm7 | Rt2 (5) | Rn (5) | Rt (5) |
// L == 0 for stores.
// Never writes to register
static bool isSTNP(uint32_t Instr) {
  return (Instr & 0x3bc00000) == 0x28000000;
}

// Load/store register pair
// (post-indexed)
// | opc (2) 10 | 1 V 00 | 1 L | imm7 | Rt2 (5) | Rn (5) | Rt (5) |
// L == 0 for stores, V == 0 for Scalar, V == 1 for Simd/FP
// Writes to Rn.
static bool isSTPPost(uint32_t Instr) {
  return (Instr & 0x3bc00000) == 0x28800000;
}

// (offset)
// | opc (2) 10 | 1 V 01 | 0 L | imm7 | Rt2 (5) | Rn (5) | Rt (5) |
static bool isSTPOffset(uint32_t Instr) {
  return (Instr & 0x3bc00000) == 0x29000000;
}

// (pre-index)
// | opc (2) 10 | 1 V 01 | 1 L | imm7 | Rt2 (5) | Rn (5) | Rt (5) |
// Writes to Rn.
static bool isSTPPre(uint32_t Instr) {
  return (Instr & 0x3bc00000) == 0x29800000;
}

static bool isSTP(uint32_t Instr) {
  return isSTPPost(Instr) || isSTPOffset(Instr) || isSTPPre(Instr);
}

// Load/store register (unscaled immediate)
// | size (2) 11 | 1 V 00 | opc (2) 0 | imm9 | 00 | Rn (5) | Rt (5) |
// V == 0 for Scalar, V == 1 for Simd/FP.
static bool isLoadStoreUnscaled(uint32_t Instr) {
  return (Instr & 0x3b000c00) == 0x38000000;
}

// Load/store register (immediate post-indexed)
// | size (2) 11 | 1 V 00 | opc (2) 0 | imm9 | 01 | Rn (5) | Rt (5) |
static bool isLoadStoreImmediatePost(uint32_t Instr) {
  return (Instr & 0x3b200c00) == 0x38000400;
}

// Load/store register (unprivileged)
// | size (2) 11 | 1 V 00 | opc (2) 0 | imm9 | 10 | Rn (5) | Rt (5) |
static bool isLoadStoreUnpriv(uint32_t Instr) {
  return (Instr & 0x3b200c00) == 0x38000800;
}

// Load/store register (immediate pre-indexed)
// | size (2) 11 | 1 V 00 | opc (2) 0 | imm9 | 11 | Rn (5) | Rt (5) |
static bool isLoadStoreImmediatePre(uint32_t Instr) {
  return (Instr & 0x3b200c00) == 0x38000c00;
}

// Load/store register (register offset)
// | size (2) 11 | 1 V 00 | opc (2) 1 | Rm (5) | option (3) S | 10 | Rn | Rt |
static bool isLoadStoreRegisterOff(uint32_t Instr) {
  return (Instr & 0x3b200c00) == 0x38200800;
}

// Load/store register (unsigned immediate)
// | size (2) 11 | 1 V 01 | opc (2) | imm12 | Rn (5) | Rt (5) |
static bool isLoadStoreRegisterUnsigned(uint32_t Instr) {
  return (Instr & 0x3b000000) == 0x39000000;
}

// Rt is always in bit position 0 - 4.
static uint32_t getRt(uint32_t Instr) { return (Instr & 0x1f); }

// Rn is always in bit position 5 - 9.
static uint32_t getRn(uint32_t Instr) { return (Instr >> 5) & 0x1f; }

// C4.1.2 Branches, Exception Generating and System instructions
// | op0 (3) 1 | 01 op1 (4) | x (22) |
// op0 == 010 101 op1 == 0xxx Conditional Branch.
// op0 == 110 101 op1 == 1xxx Unconditional Branch Register.
// op0 == x00 101 op1 == xxxx Unconditional Branch immediate.
// op0 == x01 101 op1 == 0xxx Compare and branch immediate.
// op0 == x01 101 op1 == 1xxx Test and branch immediate.
static bool isBranch(uint32_t Instr) {
  return ((Instr & 0xfe000000) == 0xd6000000) || // Cond branch.
         ((Instr & 0xfe000000) == 0x54000000) || // Uncond branch reg.
         ((Instr & 0x7c000000) == 0x14000000) || // Uncond branch imm.
         ((Instr & 0x7c000000) == 0x34000000);   // Compare and test branch.
}

static bool isV8SingleRegisterNonStructureLoadStore(uint32_t Instr) {
  return isLoadStoreUnscaled(Instr) || isLoadStoreImmediatePost(Instr) ||
         isLoadStoreUnpriv(Instr) || isLoadStoreImmediatePre(Instr) ||
         isLoadStoreRegisterOff(Instr) || isLoadStoreRegisterUnsigned(Instr);
}

// Note that this function refers to v8.0 only and does not include the
// additional load and store instructions added for in later revisions of
// the architecture such as the Atomic memory operations introduced
// in v8.1.
static bool isV8NonStructureLoad(uint32_t Instr) {
  if (isLoadExclusive(Instr))
    return true;
  if (isLoadLiteral(Instr))
    return true;
  else if (isV8SingleRegisterNonStructureLoadStore(Instr)) {
    // For Load and Store single register, Loads are derived from a
    // combination of the Size, V and Opc fields.
    uint32_t Size = (Instr >> 30) & 0xff;
    uint32_t V = (Instr >> 26) & 0x1;
    uint32_t Opc = (Instr >> 22) & 0x3;
    // For the load and store instructions that we are decoding.
    // Opc == 0 are all stores.
    // Opc == 1 with a couple of exceptions are loads. The exceptions are:
    // Size == 00 (0), V == 1, Opc == 10 (2) which is a store and
    // Size == 11 (3), V == 0, Opc == 10 (2) which is a prefetch.
    return Opc != 0 && !(Size == 0 && V == 1 && Opc == 2) &&
           !(Size == 3 && V == 0 && Opc == 2);
  }
  return false;
}

// The following decode instructions are only complete up to the instructions
// needed for errata 843419.

// Instruction with writeback updates the index register after the load/store.
static bool hasWriteback(uint32_t Instr) {
  return isLoadStoreImmediatePre(Instr) || isLoadStoreImmediatePost(Instr) ||
         isSTPPre(Instr) || isSTPPost(Instr) || isST1SinglePost(Instr) ||
         isST1MultiplePost(Instr);
}

// For the load and store class of instructions, a load can write to the
// destination register, a load and a store can write to the base register when
// the instruction has writeback.
static bool doesLoadStoreWriteToReg(uint32_t Instr, uint32_t Reg) {
  return (isV8NonStructureLoad(Instr) && getRt(Instr) == Reg) ||
         (hasWriteback(Instr) && getRn(Instr) == Reg);
}

// Scanner for Cortex-A53 errata 843419
// Full details are available in the Cortex A53 MPCore revision 0 Software
// Developers Errata Notice (ARM-EPM-048406).
//
// The instruction sequence that triggers the erratum is common in compiled
// AArch64 code, however it is sensitive to the offset of the sequence within
// a 4k page. This means that by scanning and fixing the patch after we have
// assigned addresses we only need to disassemble and fix instances of the
// sequence in the range of affected offsets.
//
// In summary the erratum conditions are a series of 4 instructions:
// 1.) An ADRP instruction that writes to register Rn with low 12 bits of
//     address of instruction either 0xff8 or 0xffc.
// 2.) A load or store instruction that can be:
// - A single register load or store, of either integer or vector registers.
// - An STP or STNP, of either integer or vector registers.
// - An Advanced SIMD ST1 store instruction.
// - Must not write to Rn, but may optionally read from it.
// 3.) An optional instruction that is not a branch and does not write to Rn.
// 4.) A load or store from the  Load/store register (unsigned immediate) class
//     that uses Rn as the base address register.
//
// Note that we do not attempt to scan for Sequence 2 as described in the
// Software Developers Errata Notice as this has been assessed to be extremely
// unlikely to occur in compiled code. This matches gold and ld.bfd behavior.

// Return true if the Instruction sequence Adrp, Instr2, and Instr4 match
// the erratum sequence. The Adrp, Instr2 and Instr4 correspond to 1.), 2.),
// and 4.) in the Scanner for Cortex-A53 errata comment above.
static bool is843419ErratumSequence(uint32_t Instr1, uint32_t Instr2,
                                    uint32_t Instr4) {
  if (!isADRP(Instr1))
    return false;

  uint32_t Rn = getRt(Instr1);
  return isLoadStoreClass(Instr2) &&
         (isLoadStoreExclusive(Instr2) || isLoadLiteral(Instr2) ||
          isV8SingleRegisterNonStructureLoadStore(Instr2) || isSTP(Instr2) ||
          isSTNP(Instr2) || isST1(Instr2)) &&
         !doesLoadStoreWriteToReg(Instr2, Rn) &&
         isLoadStoreRegisterUnsigned(Instr4) && getRn(Instr4) == Rn;
}

// Scan the instruction sequence starting at Offset Off from the base of
// InputSection IS. We update Off in this function rather than in the caller as
// we can skip ahead much further into the section when we know how many
// instructions we've scanned.
// Return the offset of the load or store instruction in IS that we want to
// patch or 0 if no patch required.
static uint64_t scanCortexA53Errata843419(InputSection *IS, uint64_t &Off,
                                          uint64_t Limit) {
  uint64_t ISAddr = IS->getVA(0);

  // Advance Off so that (ISAddr + Off) modulo 0x1000 is at least 0xff8.
  uint64_t InitialPageOff = (ISAddr + Off) & 0xfff;
  if (InitialPageOff < 0xff8)
    Off += 0xff8 - InitialPageOff;

  bool OptionalAllowed = Limit - Off > 12;
  if (Off >= Limit || Limit - Off < 12) {
    // Need at least 3 4-byte sized instructions to trigger erratum.
    Off = Limit;
    return 0;
  }

  uint64_t PatchOff = 0;
  const uint8_t *Buf = IS->data().begin();
  const ulittle32_t *InstBuf = reinterpret_cast<const ulittle32_t *>(Buf + Off);
  uint32_t Instr1 = *InstBuf++;
  uint32_t Instr2 = *InstBuf++;
  uint32_t Instr3 = *InstBuf++;
  if (is843419ErratumSequence(Instr1, Instr2, Instr3)) {
    PatchOff = Off + 8;
  } else if (OptionalAllowed && !isBranch(Instr3)) {
    uint32_t Instr4 = *InstBuf++;
    if (is843419ErratumSequence(Instr1, Instr2, Instr4))
      PatchOff = Off + 12;
  }
  if (((ISAddr + Off) & 0xfff) == 0xff8)
    Off += 4;
  else
    Off += 0xffc;
  return PatchOff;
}

class lld::elf::Patch843419Section : public SyntheticSection {
public:
  Patch843419Section(InputSection *P, uint64_t Off);

  void writeTo(uint8_t *Buf) override;

  size_t getSize() const override { return 8; }

  uint64_t getLDSTAddr() const;

  // The Section we are patching.
  const InputSection *Patchee;
  // The offset of the instruction in the Patchee section we are patching.
  uint64_t PatcheeOffset;
  // A label for the start of the Patch that we can use as a relocation target.
  Symbol *PatchSym;
};

lld::elf::Patch843419Section::Patch843419Section(InputSection *P, uint64_t Off)
    : SyntheticSection(SHF_ALLOC | SHF_EXECINSTR, SHT_PROGBITS, 4,
                       ".text.patch"),
      Patchee(P), PatcheeOffset(Off) {
  this->Parent = P->getParent();
  PatchSym = addSyntheticLocal(
      Saver.save("__CortexA53843419_" + utohexstr(getLDSTAddr())), STT_FUNC, 0,
      getSize(), *this);
  addSyntheticLocal(Saver.save("$x"), STT_NOTYPE, 0, 0, *this);
}

uint64_t lld::elf::Patch843419Section::getLDSTAddr() const {
  return Patchee->getVA(PatcheeOffset);
}

void lld::elf::Patch843419Section::writeTo(uint8_t *Buf) {
  // Copy the instruction that we will be replacing with a branch in the
  // Patchee Section.
  write32le(Buf, read32le(Patchee->data().begin() + PatcheeOffset));

  // Apply any relocation transferred from the original PatcheeSection.
  // For a SyntheticSection Buf already has OutSecOff added, but relocateAlloc
  // also adds OutSecOff so we need to subtract to avoid double counting.
  this->relocateAlloc(Buf - OutSecOff, Buf - OutSecOff + getSize());

  // Return address is the next instruction after the one we have just copied.
  uint64_t S = getLDSTAddr() + 4;
  uint64_t P = PatchSym->getVA() + 4;
  Target->relocateOne(Buf + 4, R_AARCH64_JUMP26, S - P);
}

void AArch64Err843419Patcher::init() {
  // The AArch64 ABI permits data in executable sections. We must avoid scanning
  // this data as if it were instructions to avoid false matches. We use the
  // mapping symbols in the InputObjects to identify this data, caching the
  // results in SectionMap so we don't have to recalculate it each pass.

  // The ABI Section 4.5.4 Mapping symbols; defines local symbols that describe
  // half open intervals [Symbol Value, Next Symbol Value) of code and data
  // within sections. If there is no next symbol then the half open interval is
  // [Symbol Value, End of section). The type, code or data, is determined by
  // the mapping symbol name, $x for code, $d for data.
  auto IsCodeMapSymbol = [](const Symbol *B) {
    return B->getName() == "$x" || B->getName().startswith("$x.");
  };
  auto IsDataMapSymbol = [](const Symbol *B) {
    return B->getName() == "$d" || B->getName().startswith("$d.");
  };

  // Collect mapping symbols for every executable InputSection.
  for (InputFile *File : ObjectFiles) {
    auto *F = cast<ObjFile<ELF64LE>>(File);
    for (Symbol *B : F->getLocalSymbols()) {
      auto *Def = dyn_cast<Defined>(B);
      if (!Def)
        continue;
      if (!IsCodeMapSymbol(Def) && !IsDataMapSymbol(Def))
        continue;
      if (auto *Sec = dyn_cast_or_null<InputSection>(Def->Section))
        if (Sec->Flags & SHF_EXECINSTR)
          SectionMap[Sec].push_back(Def);
    }
  }
  // For each InputSection make sure the mapping symbols are in sorted in
  // ascending order and free from consecutive runs of mapping symbols with
  // the same type. For example we must remove the redundant $d.1 from $x.0
  // $d.0 $d.1 $x.1.
  for (auto &KV : SectionMap) {
    std::vector<const Defined *> &MapSyms = KV.second;
    if (MapSyms.size() <= 1)
      continue;
    std::stable_sort(
        MapSyms.begin(), MapSyms.end(),
        [](const Defined *A, const Defined *B) { return A->Value < B->Value; });
    MapSyms.erase(
        std::unique(MapSyms.begin(), MapSyms.end(),
                    [=](const Defined *A, const Defined *B) {
                      return (IsCodeMapSymbol(A) && IsCodeMapSymbol(B)) ||
                             (IsDataMapSymbol(A) && IsDataMapSymbol(B));
                    }),
        MapSyms.end());
  }
  Initialized = true;
}

// Insert the PatchSections we have created back into the
// InputSectionDescription. As inserting patches alters the addresses of
// InputSections that follow them, we try and place the patches after all the
// executable sections, although we may need to insert them earlier if the
// InputSectionDescription is larger than the maximum branch range.
void AArch64Err843419Patcher::insertPatches(
    InputSectionDescription &ISD, std::vector<Patch843419Section *> &Patches) {
  uint64_t ISLimit;
  uint64_t PrevISLimit = ISD.Sections.front()->OutSecOff;
  uint64_t PatchUpperBound = PrevISLimit + Target->getThunkSectionSpacing();
  uint64_t OutSecAddr = ISD.Sections.front()->getParent()->Addr;

  // Set the OutSecOff of patches to the place where we want to insert them.
  // We use a similar strategy to Thunk placement. Place patches roughly
  // every multiple of maximum branch range.
  auto PatchIt = Patches.begin();
  auto PatchEnd = Patches.end();
  for (const InputSection *IS : ISD.Sections) {
    ISLimit = IS->OutSecOff + IS->getSize();
    if (ISLimit > PatchUpperBound) {
      while (PatchIt != PatchEnd) {
        if ((*PatchIt)->getLDSTAddr() - OutSecAddr >= PrevISLimit)
          break;
        (*PatchIt)->OutSecOff = PrevISLimit;
        ++PatchIt;
      }
      PatchUpperBound = PrevISLimit + Target->getThunkSectionSpacing();
    }
    PrevISLimit = ISLimit;
  }
  for (; PatchIt != PatchEnd; ++PatchIt) {
    (*PatchIt)->OutSecOff = ISLimit;
  }

  // merge all patch sections. We use the OutSecOff assigned above to
  // determine the insertion point. This is ok as we only merge into an
  // InputSectionDescription once per pass, and at the end of the pass
  // assignAddresses() will recalculate all the OutSecOff values.
  std::vector<InputSection *> Tmp;
  Tmp.reserve(ISD.Sections.size() + Patches.size());
  auto MergeCmp = [](const InputSection *A, const InputSection *B) {
    if (A->OutSecOff < B->OutSecOff)
      return true;
    if (A->OutSecOff == B->OutSecOff && isa<Patch843419Section>(A) &&
        !isa<Patch843419Section>(B))
      return true;
    return false;
  };
  std::merge(ISD.Sections.begin(), ISD.Sections.end(), Patches.begin(),
             Patches.end(), std::back_inserter(Tmp), MergeCmp);
  ISD.Sections = std::move(Tmp);
}

// Given an erratum sequence that starts at address AdrpAddr, with an
// instruction that we need to patch at PatcheeOffset from the start of
// InputSection IS, create a Patch843419 Section and add it to the
// Patches that we need to insert.
static void implementPatch(uint64_t AdrpAddr, uint64_t PatcheeOffset,
                           InputSection *IS,
                           std::vector<Patch843419Section *> &Patches) {
  // There may be a relocation at the same offset that we are patching. There
  // are four cases that we need to consider.
  // Case 1: R_AARCH64_JUMP26 branch relocation. We have already patched this
  // instance of the erratum on a previous patch and altered the relocation. We
  // have nothing more to do.
  // Case 2: A TLS Relaxation R_RELAX_TLS_IE_TO_LE. In this case the ADRP that
  // we read will be transformed into a MOVZ later so we actually don't match
  // the sequence and have nothing more to do.
  // Case 3: A load/store register (unsigned immediate) class relocation. There
  // are two of these R_AARCH_LD64_ABS_LO12_NC and R_AARCH_LD64_GOT_LO12_NC and
  // they are both absolute. We need to add the same relocation to the patch,
  // and replace the relocation with a R_AARCH_JUMP26 branch relocation.
  // Case 4: No relocation. We must create a new R_AARCH64_JUMP26 branch
  // relocation at the offset.
  auto RelIt = std::find_if(
      IS->Relocations.begin(), IS->Relocations.end(),
      [=](const Relocation &R) { return R.Offset == PatcheeOffset; });
  if (RelIt != IS->Relocations.end() &&
      (RelIt->Type == R_AARCH64_JUMP26 || RelIt->Expr == R_RELAX_TLS_IE_TO_LE))
    return;

  log("detected cortex-a53-843419 erratum sequence starting at " +
      utohexstr(AdrpAddr) + " in unpatched output.");

  auto *PS = make<Patch843419Section>(IS, PatcheeOffset);
  Patches.push_back(PS);

  auto MakeRelToPatch = [](uint64_t Offset, Symbol *PatchSym) {
    return Relocation{R_PC, R_AARCH64_JUMP26, Offset, 0, PatchSym};
  };

  if (RelIt != IS->Relocations.end()) {
    PS->Relocations.push_back(
        {RelIt->Expr, RelIt->Type, 0, RelIt->Addend, RelIt->Sym});
    *RelIt = MakeRelToPatch(PatcheeOffset, PS->PatchSym);
  } else
    IS->Relocations.push_back(MakeRelToPatch(PatcheeOffset, PS->PatchSym));
}

// Scan all the instructions in InputSectionDescription, for each instance of
// the erratum sequence create a Patch843419Section. We return the list of
// Patch843419Sections that need to be applied to ISD.
std::vector<Patch843419Section *>
AArch64Err843419Patcher::patchInputSectionDescription(
    InputSectionDescription &ISD) {
  std::vector<Patch843419Section *> Patches;
  for (InputSection *IS : ISD.Sections) {
    //  LLD doesn't use the erratum sequence in SyntheticSections.
    if (isa<SyntheticSection>(IS))
      continue;
    // Use SectionMap to make sure we only scan code and not inline data.
    // We have already sorted MapSyms in ascending order and removed consecutive
    // mapping symbols of the same type. Our range of executable instructions to
    // scan is therefore [CodeSym->Value, DataSym->Value) or [CodeSym->Value,
    // section size).
    std::vector<const Defined *> &MapSyms = SectionMap[IS];

    auto CodeSym = llvm::find_if(MapSyms, [&](const Defined *MS) {
      return MS->getName().startswith("$x");
    });

    while (CodeSym != MapSyms.end()) {
      auto DataSym = std::next(CodeSym);
      uint64_t Off = (*CodeSym)->Value;
      uint64_t Limit =
          (DataSym == MapSyms.end()) ? IS->data().size() : (*DataSym)->Value;

      while (Off < Limit) {
        uint64_t StartAddr = IS->getVA(Off);
        if (uint64_t PatcheeOffset = scanCortexA53Errata843419(IS, Off, Limit))
          implementPatch(StartAddr, PatcheeOffset, IS, Patches);
      }
      if (DataSym == MapSyms.end())
        break;
      CodeSym = std::next(DataSym);
    }
  }
  return Patches;
}

// For each InputSectionDescription make one pass over the executable sections
// looking for the erratum sequence; creating a synthetic Patch843419Section
// for each instance found. We insert these synthetic patch sections after the
// executable code in each InputSectionDescription.
//
// PreConditions:
// The Output and Input Sections have had their final addresses assigned.
//
// PostConditions:
// Returns true if at least one patch was added. The addresses of the
// Ouptut and Input Sections may have been changed.
// Returns false if no patches were required and no changes were made.
bool AArch64Err843419Patcher::createFixes() {
  if (Initialized == false)
    init();

  bool AddressesChanged = false;
  for (OutputSection *OS : OutputSections) {
    if (!(OS->Flags & SHF_ALLOC) || !(OS->Flags & SHF_EXECINSTR))
      continue;
    for (BaseCommand *BC : OS->SectionCommands)
      if (auto *ISD = dyn_cast<InputSectionDescription>(BC)) {
        std::vector<Patch843419Section *> Patches =
            patchInputSectionDescription(*ISD);
        if (!Patches.empty()) {
          insertPatches(*ISD, Patches);
          AddressesChanged = true;
        }
      }
  }
  return AddressesChanged;
}
