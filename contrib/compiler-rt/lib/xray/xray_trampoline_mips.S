//===-- xray_trampoline_mips.s ----------------------------------*- ASM -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of XRay, a dynamic runtime instrumentation system.
//
// This implements the MIPS-specific assembler for the trampolines.
//
//===----------------------------------------------------------------------===//

  .text
  .file "xray_trampoline_mips.S"
  .globl __xray_FunctionEntry
  .p2align 2
  .type __xray_FunctionEntry,@function
__xray_FunctionEntry:
  .cfi_startproc
  .set noreorder
  .cpload $t9
  .set reorder
  // Save argument registers before doing any actual work
  .cfi_def_cfa_offset 36
  addiu  $sp, $sp, -36
  sw     $ra, 32($sp)
  .cfi_offset 31, -4
  sw     $a3, 28($sp)
  sw     $a2, 24($sp)
  sw     $a1, 20($sp)
  sw     $a0, 16($sp)
  sdc1	 $f14, 8($sp)
  sdc1	 $f12, 0($sp)

  la     $t9, _ZN6__xray19XRayPatchedFunctionE
  lw     $t9, 0($t9)

  beqz   $t9, FunctionEntry_restore

  // a1=0 means that we are tracing an entry event
  move   $a1, $zero
  // Function ID is in t0 (the first parameter).
  move   $a0, $t0
  jalr   $t9

FunctionEntry_restore:
  // Restore argument registers
  ldc1   $f12, 0($sp)
  ldc1   $f14, 8($sp)
  lw     $a0, 16($sp)
  lw     $a1, 20($sp)
  lw     $a2, 24($sp)
  lw     $a3, 28($sp)
  lw     $ra, 32($sp)
  addiu	 $sp, $sp, 36
  jr     $ra
FunctionEntry_end:
  .size __xray_FunctionEntry, FunctionEntry_end-__xray_FunctionEntry
  .cfi_endproc

  .text
  .globl __xray_FunctionExit
  .p2align 2
  .type __xray_FunctionExit,@function
__xray_FunctionExit:
  .cfi_startproc
  .set noreorder
  .cpload $t9
  .set reorder
  // Save return registers before doing any actual work.
  .cfi_def_cfa_offset 36
  addiu  $sp, $sp, -36
  sw     $ra, 32($sp)
  .cfi_offset 31, -4
  sw     $a1, 28($sp)
  sw     $a0, 24($sp)
  sw     $v1, 20($sp)
  sw     $v0, 16($sp)
  sdc1   $f2, 8($sp)
  sdc1   $f0, 0($sp)

  la     $t9, _ZN6__xray19XRayPatchedFunctionE
  lw     $t9, 0($t9)

  beqz	 $t9, FunctionExit_restore

  // a1=1 means that we are tracing an exit event
  li     $a1, 1
  // Function ID is in t0 (the first parameter).
  move   $a0, $t0
  jalr   $t9

FunctionExit_restore:
  // Restore return registers
  ldc1   $f0, 0($sp)
  ldc1   $f2, 8($sp)
  lw     $v0, 16($sp)
  lw     $v1, 20($sp)
  lw     $a0, 24($sp)
  lw     $a1, 28($sp)
  lw     $ra, 32($sp)
  addiu  $sp, $sp, 36
  jr     $ra

FunctionExit_end:
  .size __xray_FunctionExit, FunctionExit_end-__xray_FunctionExit
  .cfi_endproc
