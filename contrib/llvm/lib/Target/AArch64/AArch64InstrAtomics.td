//=- AArch64InstrAtomics.td - AArch64 Atomic codegen support -*- tablegen -*-=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// AArch64 Atomic operand code-gen constructs.
//
//===----------------------------------------------------------------------===//

//===----------------------------------
// Atomic fences
//===----------------------------------
let AddedComplexity = 15, Size = 0 in
def CompilerBarrier : Pseudo<(outs), (ins i32imm:$ordering),
                             [(atomic_fence imm:$ordering, 0)]>, Sched<[]>;
def : Pat<(atomic_fence (i64 4), (imm)), (DMB (i32 0x9))>;
def : Pat<(atomic_fence (imm), (imm)), (DMB (i32 0xb))>;

//===----------------------------------
// Atomic loads
//===----------------------------------

// When they're actually atomic, only one addressing mode (GPR64sp) is
// supported, but when they're relaxed and anything can be used, all the
// standard modes would be valid and may give efficiency gains.

// A atomic load operation that actually needs acquire semantics.
class acquiring_load<PatFrag base>
  : PatFrag<(ops node:$ptr), (base node:$ptr)> {
  let IsAtomic = 1;
  let IsAtomicOrderingAcquireOrStronger = 1;
}

// An atomic load operation that does not need either acquire or release
// semantics.
class relaxed_load<PatFrag base>
  : PatFrag<(ops node:$ptr), (base node:$ptr)> {
  let IsAtomic = 1;
  let IsAtomicOrderingAcquireOrStronger = 0;
}

// 8-bit loads
def : Pat<(acquiring_load<atomic_load_8>  GPR64sp:$ptr), (LDARB GPR64sp:$ptr)>;
def : Pat<(relaxed_load<atomic_load_8> (ro_Windexed8 GPR64sp:$Rn, GPR32:$Rm,
                                                     ro_Wextend8:$offset)),
          (LDRBBroW GPR64sp:$Rn, GPR32:$Rm, ro_Wextend8:$offset)>;
def : Pat<(relaxed_load<atomic_load_8> (ro_Xindexed8 GPR64sp:$Rn, GPR64:$Rm,
                                                     ro_Xextend8:$offset)),
          (LDRBBroX GPR64sp:$Rn, GPR64:$Rm, ro_Xextend8:$offset)>;
def : Pat<(relaxed_load<atomic_load_8> (am_indexed8 GPR64sp:$Rn,
                                                    uimm12s1:$offset)),
          (LDRBBui GPR64sp:$Rn, uimm12s1:$offset)>;
def : Pat<(relaxed_load<atomic_load_8>
               (am_unscaled8 GPR64sp:$Rn, simm9:$offset)),
          (LDURBBi GPR64sp:$Rn, simm9:$offset)>;

// 16-bit loads
def : Pat<(acquiring_load<atomic_load_16> GPR64sp:$ptr), (LDARH GPR64sp:$ptr)>;
def : Pat<(relaxed_load<atomic_load_16> (ro_Windexed16 GPR64sp:$Rn, GPR32:$Rm,
                                                       ro_Wextend16:$extend)),
          (LDRHHroW GPR64sp:$Rn, GPR32:$Rm, ro_Wextend16:$extend)>;
def : Pat<(relaxed_load<atomic_load_16> (ro_Xindexed16 GPR64sp:$Rn, GPR64:$Rm,
                                                       ro_Xextend16:$extend)),
          (LDRHHroX GPR64sp:$Rn, GPR64:$Rm, ro_Xextend16:$extend)>;
def : Pat<(relaxed_load<atomic_load_16> (am_indexed16 GPR64sp:$Rn,
                                                      uimm12s2:$offset)),
          (LDRHHui GPR64sp:$Rn, uimm12s2:$offset)>;
def : Pat<(relaxed_load<atomic_load_16>
               (am_unscaled16 GPR64sp:$Rn, simm9:$offset)),
          (LDURHHi GPR64sp:$Rn, simm9:$offset)>;

// 32-bit loads
def : Pat<(acquiring_load<atomic_load_32> GPR64sp:$ptr), (LDARW GPR64sp:$ptr)>;
def : Pat<(relaxed_load<atomic_load_32> (ro_Windexed32 GPR64sp:$Rn, GPR32:$Rm,
                                                       ro_Wextend32:$extend)),
          (LDRWroW GPR64sp:$Rn, GPR32:$Rm, ro_Wextend32:$extend)>;
def : Pat<(relaxed_load<atomic_load_32> (ro_Xindexed32 GPR64sp:$Rn, GPR64:$Rm,
                                                       ro_Xextend32:$extend)),
          (LDRWroX GPR64sp:$Rn, GPR64:$Rm, ro_Xextend32:$extend)>;
def : Pat<(relaxed_load<atomic_load_32> (am_indexed32 GPR64sp:$Rn,
                                                      uimm12s4:$offset)),
          (LDRWui GPR64sp:$Rn, uimm12s4:$offset)>;
def : Pat<(relaxed_load<atomic_load_32>
               (am_unscaled32 GPR64sp:$Rn, simm9:$offset)),
          (LDURWi GPR64sp:$Rn, simm9:$offset)>;

// 64-bit loads
def : Pat<(acquiring_load<atomic_load_64> GPR64sp:$ptr), (LDARX GPR64sp:$ptr)>;
def : Pat<(relaxed_load<atomic_load_64> (ro_Windexed64 GPR64sp:$Rn, GPR32:$Rm,
                                                       ro_Wextend64:$extend)),
          (LDRXroW GPR64sp:$Rn, GPR32:$Rm, ro_Wextend64:$extend)>;
def : Pat<(relaxed_load<atomic_load_64> (ro_Xindexed64 GPR64sp:$Rn, GPR64:$Rm,
                                                       ro_Xextend64:$extend)),
          (LDRXroX GPR64sp:$Rn, GPR64:$Rm, ro_Xextend64:$extend)>;
def : Pat<(relaxed_load<atomic_load_64> (am_indexed64 GPR64sp:$Rn,
                                                      uimm12s8:$offset)),
          (LDRXui GPR64sp:$Rn, uimm12s8:$offset)>;
def : Pat<(relaxed_load<atomic_load_64>
               (am_unscaled64 GPR64sp:$Rn, simm9:$offset)),
          (LDURXi GPR64sp:$Rn, simm9:$offset)>;

//===----------------------------------
// Atomic stores
//===----------------------------------

// When they're actually atomic, only one addressing mode (GPR64sp) is
// supported, but when they're relaxed and anything can be used, all the
// standard modes would be valid and may give efficiency gains.

// A store operation that actually needs release semantics.
class releasing_store<PatFrag base>
  : PatFrag<(ops node:$ptr, node:$val), (base node:$ptr, node:$val)> {
  let IsAtomic = 1;
  let IsAtomicOrderingReleaseOrStronger = 1;
}

// An atomic store operation that doesn't actually need to be atomic on AArch64.
class relaxed_store<PatFrag base>
  : PatFrag<(ops node:$ptr, node:$val), (base node:$ptr, node:$val)> {
  let IsAtomic = 1;
  let IsAtomicOrderingReleaseOrStronger = 0;
}

// 8-bit stores
def : Pat<(releasing_store<atomic_store_8> GPR64sp:$ptr, GPR32:$val),
          (STLRB GPR32:$val, GPR64sp:$ptr)>;
def : Pat<(relaxed_store<atomic_store_8>
               (ro_Windexed8 GPR64sp:$Rn, GPR32:$Rm, ro_Wextend8:$extend),
               GPR32:$val),
          (STRBBroW GPR32:$val, GPR64sp:$Rn, GPR32:$Rm, ro_Wextend8:$extend)>;
def : Pat<(relaxed_store<atomic_store_8>
               (ro_Xindexed8 GPR64sp:$Rn, GPR64:$Rm, ro_Xextend8:$extend),
               GPR32:$val),
          (STRBBroX GPR32:$val, GPR64sp:$Rn, GPR64:$Rm, ro_Xextend8:$extend)>;
def : Pat<(relaxed_store<atomic_store_8>
               (am_indexed8 GPR64sp:$Rn, uimm12s1:$offset), GPR32:$val),
          (STRBBui GPR32:$val, GPR64sp:$Rn, uimm12s1:$offset)>;
def : Pat<(relaxed_store<atomic_store_8>
               (am_unscaled8 GPR64sp:$Rn, simm9:$offset), GPR32:$val),
          (STURBBi GPR32:$val, GPR64sp:$Rn, simm9:$offset)>;

// 16-bit stores
def : Pat<(releasing_store<atomic_store_16> GPR64sp:$ptr, GPR32:$val),
          (STLRH GPR32:$val, GPR64sp:$ptr)>;
def : Pat<(relaxed_store<atomic_store_16> (ro_Windexed16 GPR64sp:$Rn, GPR32:$Rm,
                                                         ro_Wextend16:$extend),
                                          GPR32:$val),
          (STRHHroW GPR32:$val, GPR64sp:$Rn, GPR32:$Rm, ro_Wextend16:$extend)>;
def : Pat<(relaxed_store<atomic_store_16> (ro_Xindexed16 GPR64sp:$Rn, GPR64:$Rm,
                                                         ro_Xextend16:$extend),
                                          GPR32:$val),
          (STRHHroX GPR32:$val, GPR64sp:$Rn, GPR64:$Rm, ro_Xextend16:$extend)>;
def : Pat<(relaxed_store<atomic_store_16>
              (am_indexed16 GPR64sp:$Rn, uimm12s2:$offset), GPR32:$val),
          (STRHHui GPR32:$val, GPR64sp:$Rn, uimm12s2:$offset)>;
def : Pat<(relaxed_store<atomic_store_16>
               (am_unscaled16 GPR64sp:$Rn, simm9:$offset), GPR32:$val),
          (STURHHi GPR32:$val, GPR64sp:$Rn, simm9:$offset)>;

// 32-bit stores
def : Pat<(releasing_store<atomic_store_32> GPR64sp:$ptr, GPR32:$val),
          (STLRW GPR32:$val, GPR64sp:$ptr)>;
def : Pat<(relaxed_store<atomic_store_32> (ro_Windexed32 GPR64sp:$Rn, GPR32:$Rm,
                                                         ro_Wextend32:$extend),
                                          GPR32:$val),
          (STRWroW GPR32:$val, GPR64sp:$Rn, GPR32:$Rm, ro_Wextend32:$extend)>;
def : Pat<(relaxed_store<atomic_store_32> (ro_Xindexed32 GPR64sp:$Rn, GPR64:$Rm,
                                                         ro_Xextend32:$extend),
                                          GPR32:$val),
          (STRWroX GPR32:$val, GPR64sp:$Rn, GPR64:$Rm, ro_Xextend32:$extend)>;
def : Pat<(relaxed_store<atomic_store_32>
              (am_indexed32 GPR64sp:$Rn, uimm12s4:$offset), GPR32:$val),
          (STRWui GPR32:$val, GPR64sp:$Rn, uimm12s4:$offset)>;
def : Pat<(relaxed_store<atomic_store_32>
               (am_unscaled32 GPR64sp:$Rn, simm9:$offset), GPR32:$val),
          (STURWi GPR32:$val, GPR64sp:$Rn, simm9:$offset)>;

// 64-bit stores
def : Pat<(releasing_store<atomic_store_64> GPR64sp:$ptr, GPR64:$val),
          (STLRX GPR64:$val, GPR64sp:$ptr)>;
def : Pat<(relaxed_store<atomic_store_64> (ro_Windexed64 GPR64sp:$Rn, GPR32:$Rm,
                                                         ro_Wextend16:$extend),
                                          GPR64:$val),
          (STRXroW GPR64:$val, GPR64sp:$Rn, GPR32:$Rm, ro_Wextend64:$extend)>;
def : Pat<(relaxed_store<atomic_store_64> (ro_Xindexed64 GPR64sp:$Rn, GPR64:$Rm,
                                                         ro_Xextend16:$extend),
                                          GPR64:$val),
          (STRXroX GPR64:$val, GPR64sp:$Rn, GPR64:$Rm, ro_Xextend64:$extend)>;
def : Pat<(relaxed_store<atomic_store_64>
              (am_indexed64 GPR64sp:$Rn, uimm12s8:$offset), GPR64:$val),
          (STRXui GPR64:$val, GPR64sp:$Rn, uimm12s8:$offset)>;
def : Pat<(relaxed_store<atomic_store_64>
               (am_unscaled64 GPR64sp:$Rn, simm9:$offset), GPR64:$val),
          (STURXi GPR64:$val, GPR64sp:$Rn, simm9:$offset)>;

//===----------------------------------
// Low-level exclusive operations
//===----------------------------------

// Load-exclusives.

def ldxr_1 : PatFrag<(ops node:$ptr), (int_aarch64_ldxr node:$ptr), [{
  return cast<MemIntrinsicSDNode>(N)->getMemoryVT() == MVT::i8;
}]>;

def ldxr_2 : PatFrag<(ops node:$ptr), (int_aarch64_ldxr node:$ptr), [{
  return cast<MemIntrinsicSDNode>(N)->getMemoryVT() == MVT::i16;
}]>;

def ldxr_4 : PatFrag<(ops node:$ptr), (int_aarch64_ldxr node:$ptr), [{
  return cast<MemIntrinsicSDNode>(N)->getMemoryVT() == MVT::i32;
}]>;

def ldxr_8 : PatFrag<(ops node:$ptr), (int_aarch64_ldxr node:$ptr), [{
  return cast<MemIntrinsicSDNode>(N)->getMemoryVT() == MVT::i64;
}]>;

def : Pat<(ldxr_1 GPR64sp:$addr),
          (SUBREG_TO_REG (i64 0), (LDXRB GPR64sp:$addr), sub_32)>;
def : Pat<(ldxr_2 GPR64sp:$addr),
          (SUBREG_TO_REG (i64 0), (LDXRH GPR64sp:$addr), sub_32)>;
def : Pat<(ldxr_4 GPR64sp:$addr),
          (SUBREG_TO_REG (i64 0), (LDXRW GPR64sp:$addr), sub_32)>;
def : Pat<(ldxr_8 GPR64sp:$addr), (LDXRX GPR64sp:$addr)>;

def : Pat<(and (ldxr_1 GPR64sp:$addr), 0xff),
          (SUBREG_TO_REG (i64 0), (LDXRB GPR64sp:$addr), sub_32)>;
def : Pat<(and (ldxr_2 GPR64sp:$addr), 0xffff),
          (SUBREG_TO_REG (i64 0), (LDXRH GPR64sp:$addr), sub_32)>;
def : Pat<(and (ldxr_4 GPR64sp:$addr), 0xffffffff),
          (SUBREG_TO_REG (i64 0), (LDXRW GPR64sp:$addr), sub_32)>;

// Load-exclusives.

def ldaxr_1 : PatFrag<(ops node:$ptr), (int_aarch64_ldaxr node:$ptr), [{
  return cast<MemIntrinsicSDNode>(N)->getMemoryVT() == MVT::i8;
}]>;

def ldaxr_2 : PatFrag<(ops node:$ptr), (int_aarch64_ldaxr node:$ptr), [{
  return cast<MemIntrinsicSDNode>(N)->getMemoryVT() == MVT::i16;
}]>;

def ldaxr_4 : PatFrag<(ops node:$ptr), (int_aarch64_ldaxr node:$ptr), [{
  return cast<MemIntrinsicSDNode>(N)->getMemoryVT() == MVT::i32;
}]>;

def ldaxr_8 : PatFrag<(ops node:$ptr), (int_aarch64_ldaxr node:$ptr), [{
  return cast<MemIntrinsicSDNode>(N)->getMemoryVT() == MVT::i64;
}]>;

def : Pat<(ldaxr_1 GPR64sp:$addr),
          (SUBREG_TO_REG (i64 0), (LDAXRB GPR64sp:$addr), sub_32)>;
def : Pat<(ldaxr_2 GPR64sp:$addr),
          (SUBREG_TO_REG (i64 0), (LDAXRH GPR64sp:$addr), sub_32)>;
def : Pat<(ldaxr_4 GPR64sp:$addr),
          (SUBREG_TO_REG (i64 0), (LDAXRW GPR64sp:$addr), sub_32)>;
def : Pat<(ldaxr_8 GPR64sp:$addr), (LDAXRX GPR64sp:$addr)>;

def : Pat<(and (ldaxr_1 GPR64sp:$addr), 0xff),
          (SUBREG_TO_REG (i64 0), (LDAXRB GPR64sp:$addr), sub_32)>;
def : Pat<(and (ldaxr_2 GPR64sp:$addr), 0xffff),
          (SUBREG_TO_REG (i64 0), (LDAXRH GPR64sp:$addr), sub_32)>;
def : Pat<(and (ldaxr_4 GPR64sp:$addr), 0xffffffff),
          (SUBREG_TO_REG (i64 0), (LDAXRW GPR64sp:$addr), sub_32)>;

// Store-exclusives.

def stxr_1 : PatFrag<(ops node:$val, node:$ptr),
                     (int_aarch64_stxr node:$val, node:$ptr), [{
  return cast<MemIntrinsicSDNode>(N)->getMemoryVT() == MVT::i8;
}]>;

def stxr_2 : PatFrag<(ops node:$val, node:$ptr),
                     (int_aarch64_stxr node:$val, node:$ptr), [{
  return cast<MemIntrinsicSDNode>(N)->getMemoryVT() == MVT::i16;
}]>;

def stxr_4 : PatFrag<(ops node:$val, node:$ptr),
                     (int_aarch64_stxr node:$val, node:$ptr), [{
  return cast<MemIntrinsicSDNode>(N)->getMemoryVT() == MVT::i32;
}]>;

def stxr_8 : PatFrag<(ops node:$val, node:$ptr),
                     (int_aarch64_stxr node:$val, node:$ptr), [{
  return cast<MemIntrinsicSDNode>(N)->getMemoryVT() == MVT::i64;
}]>;


def : Pat<(stxr_1 GPR64:$val, GPR64sp:$addr),
          (STXRB (EXTRACT_SUBREG GPR64:$val, sub_32), GPR64sp:$addr)>;
def : Pat<(stxr_2 GPR64:$val, GPR64sp:$addr),
          (STXRH (EXTRACT_SUBREG GPR64:$val, sub_32), GPR64sp:$addr)>;
def : Pat<(stxr_4 GPR64:$val, GPR64sp:$addr),
          (STXRW (EXTRACT_SUBREG GPR64:$val, sub_32), GPR64sp:$addr)>;
def : Pat<(stxr_8 GPR64:$val, GPR64sp:$addr),
          (STXRX GPR64:$val, GPR64sp:$addr)>;

def : Pat<(stxr_1 (zext (and GPR32:$val, 0xff)), GPR64sp:$addr),
          (STXRB GPR32:$val, GPR64sp:$addr)>;
def : Pat<(stxr_2 (zext (and GPR32:$val, 0xffff)), GPR64sp:$addr),
          (STXRH GPR32:$val, GPR64sp:$addr)>;
def : Pat<(stxr_4 (zext GPR32:$val), GPR64sp:$addr),
          (STXRW GPR32:$val, GPR64sp:$addr)>;

def : Pat<(stxr_1 (and GPR64:$val, 0xff), GPR64sp:$addr),
          (STXRB (EXTRACT_SUBREG GPR64:$val, sub_32), GPR64sp:$addr)>;
def : Pat<(stxr_2 (and GPR64:$val, 0xffff), GPR64sp:$addr),
          (STXRH (EXTRACT_SUBREG GPR64:$val, sub_32), GPR64sp:$addr)>;
def : Pat<(stxr_4 (and GPR64:$val, 0xffffffff), GPR64sp:$addr),
          (STXRW (EXTRACT_SUBREG GPR64:$val, sub_32), GPR64sp:$addr)>;

// Store-release-exclusives.

def stlxr_1 : PatFrag<(ops node:$val, node:$ptr),
                     (int_aarch64_stlxr node:$val, node:$ptr), [{
  return cast<MemIntrinsicSDNode>(N)->getMemoryVT() == MVT::i8;
}]>;

def stlxr_2 : PatFrag<(ops node:$val, node:$ptr),
                     (int_aarch64_stlxr node:$val, node:$ptr), [{
  return cast<MemIntrinsicSDNode>(N)->getMemoryVT() == MVT::i16;
}]>;

def stlxr_4 : PatFrag<(ops node:$val, node:$ptr),
                     (int_aarch64_stlxr node:$val, node:$ptr), [{
  return cast<MemIntrinsicSDNode>(N)->getMemoryVT() == MVT::i32;
}]>;

def stlxr_8 : PatFrag<(ops node:$val, node:$ptr),
                     (int_aarch64_stlxr node:$val, node:$ptr), [{
  return cast<MemIntrinsicSDNode>(N)->getMemoryVT() == MVT::i64;
}]>;


def : Pat<(stlxr_1 GPR64:$val, GPR64sp:$addr),
          (STLXRB (EXTRACT_SUBREG GPR64:$val, sub_32), GPR64sp:$addr)>;
def : Pat<(stlxr_2 GPR64:$val, GPR64sp:$addr),
          (STLXRH (EXTRACT_SUBREG GPR64:$val, sub_32), GPR64sp:$addr)>;
def : Pat<(stlxr_4 GPR64:$val, GPR64sp:$addr),
          (STLXRW (EXTRACT_SUBREG GPR64:$val, sub_32), GPR64sp:$addr)>;
def : Pat<(stlxr_8 GPR64:$val, GPR64sp:$addr),
          (STLXRX GPR64:$val, GPR64sp:$addr)>;

def : Pat<(stlxr_1 (zext (and GPR32:$val, 0xff)), GPR64sp:$addr),
          (STLXRB GPR32:$val, GPR64sp:$addr)>;
def : Pat<(stlxr_2 (zext (and GPR32:$val, 0xffff)), GPR64sp:$addr),
          (STLXRH GPR32:$val, GPR64sp:$addr)>;
def : Pat<(stlxr_4 (zext GPR32:$val), GPR64sp:$addr),
          (STLXRW GPR32:$val, GPR64sp:$addr)>;

def : Pat<(stlxr_1 (and GPR64:$val, 0xff), GPR64sp:$addr),
          (STLXRB (EXTRACT_SUBREG GPR64:$val, sub_32), GPR64sp:$addr)>;
def : Pat<(stlxr_2 (and GPR64:$val, 0xffff), GPR64sp:$addr),
          (STLXRH (EXTRACT_SUBREG GPR64:$val, sub_32), GPR64sp:$addr)>;
def : Pat<(stlxr_4 (and GPR64:$val, 0xffffffff), GPR64sp:$addr),
          (STLXRW (EXTRACT_SUBREG GPR64:$val, sub_32), GPR64sp:$addr)>;


// And clear exclusive.

def : Pat<(int_aarch64_clrex), (CLREX 0xf)>;

//===----------------------------------
// Atomic cmpxchg for -O0
//===----------------------------------

// The fast register allocator used during -O0 inserts spills to cover any VRegs
// live across basic block boundaries. When this happens between an LDXR and an
// STXR it can clear the exclusive monitor, causing all cmpxchg attempts to
// fail.

// Unfortunately, this means we have to have an alternative (expanded
// post-regalloc) path for -O0 compilations. Fortunately this path can be
// significantly more naive than the standard expansion: we conservatively
// assume seq_cst, strong cmpxchg and omit clrex on failure.

let Constraints = "@earlyclobber $Rd,@earlyclobber $scratch",
    mayLoad = 1, mayStore = 1 in {
def CMP_SWAP_8 : Pseudo<(outs GPR32:$Rd, GPR32:$scratch),
                        (ins GPR64:$addr, GPR32:$desired, GPR32:$new), []>,
                 Sched<[WriteAtomic]>;

def CMP_SWAP_16 : Pseudo<(outs GPR32:$Rd, GPR32:$scratch),
                         (ins GPR64:$addr, GPR32:$desired, GPR32:$new), []>,
                  Sched<[WriteAtomic]>;

def CMP_SWAP_32 : Pseudo<(outs GPR32:$Rd, GPR32:$scratch),
                         (ins GPR64:$addr, GPR32:$desired, GPR32:$new), []>,
                  Sched<[WriteAtomic]>;

def CMP_SWAP_64 : Pseudo<(outs GPR64:$Rd, GPR32:$scratch),
                         (ins GPR64:$addr, GPR64:$desired, GPR64:$new), []>,
                  Sched<[WriteAtomic]>;
}

let Constraints = "@earlyclobber $RdLo,@earlyclobber $RdHi,@earlyclobber $scratch",
    mayLoad = 1, mayStore = 1 in
def CMP_SWAP_128 : Pseudo<(outs GPR64:$RdLo, GPR64:$RdHi, GPR32:$scratch),
                          (ins GPR64:$addr, GPR64:$desiredLo, GPR64:$desiredHi,
                               GPR64:$newLo, GPR64:$newHi), []>,
                   Sched<[WriteAtomic]>;

// v8.1 Atomic instructions:
let Predicates = [HasLSE] in {
  defm : LDOPregister_patterns<"LDADD", "atomic_load_add">;
  defm : LDOPregister_patterns<"LDSET", "atomic_load_or">;
  defm : LDOPregister_patterns<"LDEOR", "atomic_load_xor">;
  defm : LDOPregister_patterns<"LDCLR", "atomic_load_clr">;
  defm : LDOPregister_patterns<"LDSMAX", "atomic_load_max">;
  defm : LDOPregister_patterns<"LDSMIN", "atomic_load_min">;
  defm : LDOPregister_patterns<"LDUMAX", "atomic_load_umax">;
  defm : LDOPregister_patterns<"LDUMIN", "atomic_load_umin">;
  defm : LDOPregister_patterns<"SWP", "atomic_swap">;
  defm : CASregister_patterns<"CAS", "atomic_cmp_swap">;

  // These two patterns are only needed for global isel, selection dag isel
  // converts atomic load-sub into a sub and atomic load-add, and likewise for
  // and -> clr.
  defm : LDOPregister_patterns_mod<"LDADD", "atomic_load_sub", "SUB">;
  defm : LDOPregister_patterns_mod<"LDCLR", "atomic_load_and", "ORN">;
}

