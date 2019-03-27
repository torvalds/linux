//===-- xray_trampoline_mips64.s --------------------------------*- ASM -*-===//
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
// This implements the MIPS64-specific assembler for the trampolines.
//
//===----------------------------------------------------------------------===//

  .text
  .file "xray_trampoline_mips64.S"
  .globl __xray_FunctionEntry
  .p2align 2
  .type __xray_FunctionEntry,@function
__xray_FunctionEntry:
  .cfi_startproc
  // Save argument registers before doing any actual work.
  .cfi_def_cfa_offset 144
  daddiu  $sp, $sp, -144
  sd      $ra, 136($sp)
  .cfi_offset 31, -8
  sd      $gp, 128($sp)
  sd      $a7, 120($sp)
  sd      $a6, 112($sp)
  sd      $a5, 104($sp)
  sd      $a4, 96($sp)
  sd      $a3, 88($sp)
  sd      $a2, 80($sp)
  sd      $a1, 72($sp)
  sd      $a0, 64($sp)
  sdc1    $f19, 56($sp)
  sdc1    $f18, 48($sp)
  sdc1    $f17, 40($sp)
  sdc1    $f16, 32($sp)
  sdc1    $f15, 24($sp)
  sdc1    $f14, 16($sp)
  sdc1    $f13, 8($sp)
  sdc1    $f12, 0($sp)

  lui     $gp, %hi(%neg(%gp_rel(__xray_FunctionEntry)))
  daddu   $gp, $gp, $t9
  daddiu  $gp ,$gp, %lo(%neg(%gp_rel(__xray_FunctionEntry)))

  dla     $t9, _ZN6__xray19XRayPatchedFunctionE
  ld      $t9, 0($t9)

  beqz    $t9, FunctionEntry_restore

  // a1=0 means that we are tracing an entry event
  move    $a1, $zero
  // Function ID is in t0 (the first parameter).
  move    $a0, $t0
  jalr    $t9

FunctionEntry_restore:
  // Restore argument registers
  ldc1    $f12, 0($sp)
  ldc1    $f13, 8($sp)
  ldc1    $f14, 16($sp)
  ldc1    $f15, 24($sp)
  ldc1    $f16, 32($sp)
  ldc1    $f17, 40($sp)
  ldc1    $f18, 48($sp)
  ldc1    $f19, 56($sp)
  ld      $a0, 64($sp)
  ld      $a1, 72($sp)
  ld      $a2, 80($sp)
  ld      $a3, 88($sp)
  ld      $a4, 96($sp)
  ld      $a5, 104($sp)
  ld      $a6, 112($sp)
  ld      $a7, 120($sp)
  ld      $gp, 128($sp)
  ld      $ra, 136($sp)
  daddiu  $sp, $sp, 144
  jr      $ra
FunctionEntry_end:
  .size __xray_FunctionEntry, FunctionEntry_end-__xray_FunctionEntry
  .cfi_endproc

  .text
  .globl __xray_FunctionExit
  .p2align 2
  .type __xray_FunctionExit,@function
__xray_FunctionExit:
  .cfi_startproc
  // Save return registers before doing any actual work.
  .cfi_def_cfa_offset 64
  daddiu  $sp, $sp, -64
  sd      $ra, 56($sp)
  .cfi_offset 31, -8
  sd      $gp, 48($sp)
  sd      $a0, 40($sp)
  sd      $v1, 32($sp)
  sd      $v0, 24($sp)
  sdc1    $f2, 16($sp)
  sdc1    $f1, 8($sp)
  sdc1    $f0, 0($sp)

  lui     $gp, %hi(%neg(%gp_rel(__xray_FunctionExit)))
  daddu   $gp, $gp, $t9
  daddiu  $gp ,$gp, %lo(%neg(%gp_rel(__xray_FunctionExit)))

  dla     $t9, _ZN6__xray19XRayPatchedFunctionE
  ld      $t9, 0($t9)

  beqz    $t9, FunctionExit_restore

  // a1=1 means that we are tracing an exit event
  li      $a1, 1
  // Function ID is in t0 (the first parameter).
  move    $a0, $t0
  jalr    $t9

FunctionExit_restore:
  // Restore return registers
  ldc1    $f0, 0($sp)
  ldc1    $f1, 8($sp)
  ldc1    $f2, 16($sp)
  ld      $v0, 24($sp)
  ld      $v1, 32($sp)
  ld      $a0, 40($sp)
  ld      $gp, 48($sp)
  ld      $ra, 56($sp)
  daddiu  $sp, $sp, 64
  jr      $ra

FunctionExit_end:
  .size __xray_FunctionExit, FunctionExit_end-__xray_FunctionExit
  .cfi_endproc
