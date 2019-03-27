//===------------------------ UnwindRegistersSave.S -----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "assembly.h"

    .text

#if !defined(__USING_SJLJ_EXCEPTIONS__)

#if defined(__i386__)

#
# extern int unw_getcontext(unw_context_t* thread_state)
#
# On entry:
#   +                       +
#   +-----------------------+
#   + thread_state pointer  +
#   +-----------------------+
#   + return address        +
#   +-----------------------+   <-- SP
#   +                       +
#
DEFINE_LIBUNWIND_FUNCTION(unw_getcontext)
  push  %eax
  movl  8(%esp), %eax
  movl  %ebx,  4(%eax)
  movl  %ecx,  8(%eax)
  movl  %edx, 12(%eax)
  movl  %edi, 16(%eax)
  movl  %esi, 20(%eax)
  movl  %ebp, 24(%eax)
  movl  %esp, %edx
  addl  $8, %edx
  movl  %edx, 28(%eax)  # store what sp was at call site as esp
  # skip ss
  # skip eflags
  movl  4(%esp), %edx
  movl  %edx, 40(%eax)  # store return address as eip
  # skip cs
  # skip ds
  # skip es
  # skip fs
  # skip gs
  movl  (%esp), %edx
  movl  %edx, (%eax)  # store original eax
  popl  %eax
  xorl  %eax, %eax    # return UNW_ESUCCESS
  ret

#elif defined(__x86_64__)

#
# extern int unw_getcontext(unw_context_t* thread_state)
#
# On entry:
#  thread_state pointer is in rdi
#
DEFINE_LIBUNWIND_FUNCTION(unw_getcontext)
#if defined(_WIN64)
#define PTR %rcx
#define TMP %rdx
#else
#define PTR %rdi
#define TMP %rsi
#endif

  movq  %rax,   (PTR)
  movq  %rbx,  8(PTR)
  movq  %rcx, 16(PTR)
  movq  %rdx, 24(PTR)
  movq  %rdi, 32(PTR)
  movq  %rsi, 40(PTR)
  movq  %rbp, 48(PTR)
  movq  %rsp, 56(PTR)
  addq  $8,   56(PTR)
  movq  %r8,  64(PTR)
  movq  %r9,  72(PTR)
  movq  %r10, 80(PTR)
  movq  %r11, 88(PTR)
  movq  %r12, 96(PTR)
  movq  %r13,104(PTR)
  movq  %r14,112(PTR)
  movq  %r15,120(PTR)
  movq  (%rsp),TMP
  movq  TMP,128(PTR) # store return address as rip
  # skip rflags
  # skip cs
  # skip fs
  # skip gs

#if defined(_WIN64)
  movdqu %xmm0,176(PTR)
  movdqu %xmm1,192(PTR)
  movdqu %xmm2,208(PTR)
  movdqu %xmm3,224(PTR)
  movdqu %xmm4,240(PTR)
  movdqu %xmm5,256(PTR)
  movdqu %xmm6,272(PTR)
  movdqu %xmm7,288(PTR)
  movdqu %xmm8,304(PTR)
  movdqu %xmm9,320(PTR)
  movdqu %xmm10,336(PTR)
  movdqu %xmm11,352(PTR)
  movdqu %xmm12,368(PTR)
  movdqu %xmm13,384(PTR)
  movdqu %xmm14,400(PTR)
  movdqu %xmm15,416(PTR)
#endif
  xorl  %eax, %eax    # return UNW_ESUCCESS
  ret

#elif defined(__mips__) && defined(_ABIO32) && _MIPS_SIM == _ABIO32

#
# extern int unw_getcontext(unw_context_t* thread_state)
#
# On entry:
#  thread_state pointer is in a0 ($4)
#
DEFINE_LIBUNWIND_FUNCTION(unw_getcontext)
  .set push
  .set noat
  .set noreorder
  .set nomacro
  sw    $1, (4 * 1)($4)
  sw    $2, (4 * 2)($4)
  sw    $3, (4 * 3)($4)
  sw    $4, (4 * 4)($4)
  sw    $5, (4 * 5)($4)
  sw    $6, (4 * 6)($4)
  sw    $7, (4 * 7)($4)
  sw    $8, (4 * 8)($4)
  sw    $9, (4 * 9)($4)
  sw    $10, (4 * 10)($4)
  sw    $11, (4 * 11)($4)
  sw    $12, (4 * 12)($4)
  sw    $13, (4 * 13)($4)
  sw    $14, (4 * 14)($4)
  sw    $15, (4 * 15)($4)
  sw    $16, (4 * 16)($4)
  sw    $17, (4 * 17)($4)
  sw    $18, (4 * 18)($4)
  sw    $19, (4 * 19)($4)
  sw    $20, (4 * 20)($4)
  sw    $21, (4 * 21)($4)
  sw    $22, (4 * 22)($4)
  sw    $23, (4 * 23)($4)
  sw    $24, (4 * 24)($4)
  sw    $25, (4 * 25)($4)
  sw    $26, (4 * 26)($4)
  sw    $27, (4 * 27)($4)
  sw    $28, (4 * 28)($4)
  sw    $29, (4 * 29)($4)
  sw    $30, (4 * 30)($4)
  sw    $31, (4 * 31)($4)
  # Store return address to pc
  sw    $31, (4 * 32)($4)
  # hi and lo
  mfhi  $8
  sw    $8,  (4 * 33)($4)
  mflo  $8
  sw    $8,  (4 * 34)($4)
#ifdef __mips_hard_float
#if __mips_fpr != 64
  sdc1  $f0, (4 * 36 + 8 * 0)($4)
  sdc1  $f2, (4 * 36 + 8 * 2)($4)
  sdc1  $f4, (4 * 36 + 8 * 4)($4)
  sdc1  $f6, (4 * 36 + 8 * 6)($4)
  sdc1  $f8, (4 * 36 + 8 * 8)($4)
  sdc1  $f10, (4 * 36 + 8 * 10)($4)
  sdc1  $f12, (4 * 36 + 8 * 12)($4)
  sdc1  $f14, (4 * 36 + 8 * 14)($4)
  sdc1  $f16, (4 * 36 + 8 * 16)($4)
  sdc1  $f18, (4 * 36 + 8 * 18)($4)
  sdc1  $f20, (4 * 36 + 8 * 20)($4)
  sdc1  $f22, (4 * 36 + 8 * 22)($4)
  sdc1  $f24, (4 * 36 + 8 * 24)($4)
  sdc1  $f26, (4 * 36 + 8 * 26)($4)
  sdc1  $f28, (4 * 36 + 8 * 28)($4)
  sdc1  $f30, (4 * 36 + 8 * 30)($4)
#else
  sdc1  $f0, (4 * 36 + 8 * 0)($4)
  sdc1  $f1, (4 * 36 + 8 * 1)($4)
  sdc1  $f2, (4 * 36 + 8 * 2)($4)
  sdc1  $f3, (4 * 36 + 8 * 3)($4)
  sdc1  $f4, (4 * 36 + 8 * 4)($4)
  sdc1  $f5, (4 * 36 + 8 * 5)($4)
  sdc1  $f6, (4 * 36 + 8 * 6)($4)
  sdc1  $f7, (4 * 36 + 8 * 7)($4)
  sdc1  $f8, (4 * 36 + 8 * 8)($4)
  sdc1  $f9, (4 * 36 + 8 * 9)($4)
  sdc1  $f10, (4 * 36 + 8 * 10)($4)
  sdc1  $f11, (4 * 36 + 8 * 11)($4)
  sdc1  $f12, (4 * 36 + 8 * 12)($4)
  sdc1  $f13, (4 * 36 + 8 * 13)($4)
  sdc1  $f14, (4 * 36 + 8 * 14)($4)
  sdc1  $f15, (4 * 36 + 8 * 15)($4)
  sdc1  $f16, (4 * 36 + 8 * 16)($4)
  sdc1  $f17, (4 * 36 + 8 * 17)($4)
  sdc1  $f18, (4 * 36 + 8 * 18)($4)
  sdc1  $f19, (4 * 36 + 8 * 19)($4)
  sdc1  $f20, (4 * 36 + 8 * 20)($4)
  sdc1  $f21, (4 * 36 + 8 * 21)($4)
  sdc1  $f22, (4 * 36 + 8 * 22)($4)
  sdc1  $f23, (4 * 36 + 8 * 23)($4)
  sdc1  $f24, (4 * 36 + 8 * 24)($4)
  sdc1  $f25, (4 * 36 + 8 * 25)($4)
  sdc1  $f26, (4 * 36 + 8 * 26)($4)
  sdc1  $f27, (4 * 36 + 8 * 27)($4)
  sdc1  $f28, (4 * 36 + 8 * 28)($4)
  sdc1  $f29, (4 * 36 + 8 * 29)($4)
  sdc1  $f30, (4 * 36 + 8 * 30)($4)
  sdc1  $f31, (4 * 36 + 8 * 31)($4)
#endif
#endif
  jr	$31
  # return UNW_ESUCCESS
  or    $2, $0, $0
  .set pop

#elif defined(__mips64)

#
# extern int unw_getcontext(unw_context_t* thread_state)
#
# On entry:
#  thread_state pointer is in a0 ($4)
#
DEFINE_LIBUNWIND_FUNCTION(unw_getcontext)
  .set push
  .set noat
  .set noreorder
  .set nomacro
  sd    $1, (8 * 1)($4)
  sd    $2, (8 * 2)($4)
  sd    $3, (8 * 3)($4)
  sd    $4, (8 * 4)($4)
  sd    $5, (8 * 5)($4)
  sd    $6, (8 * 6)($4)
  sd    $7, (8 * 7)($4)
  sd    $8, (8 * 8)($4)
  sd    $9, (8 * 9)($4)
  sd    $10, (8 * 10)($4)
  sd    $11, (8 * 11)($4)
  sd    $12, (8 * 12)($4)
  sd    $13, (8 * 13)($4)
  sd    $14, (8 * 14)($4)
  sd    $15, (8 * 15)($4)
  sd    $16, (8 * 16)($4)
  sd    $17, (8 * 17)($4)
  sd    $18, (8 * 18)($4)
  sd    $19, (8 * 19)($4)
  sd    $20, (8 * 20)($4)
  sd    $21, (8 * 21)($4)
  sd    $22, (8 * 22)($4)
  sd    $23, (8 * 23)($4)
  sd    $24, (8 * 24)($4)
  sd    $25, (8 * 25)($4)
  sd    $26, (8 * 26)($4)
  sd    $27, (8 * 27)($4)
  sd    $28, (8 * 28)($4)
  sd    $29, (8 * 29)($4)
  sd    $30, (8 * 30)($4)
  sd    $31, (8 * 31)($4)
  # Store return address to pc
  sd    $31, (8 * 32)($4)
  # hi and lo
  mfhi  $8
  sd    $8,  (8 * 33)($4)
  mflo  $8
  sd    $8,  (8 * 34)($4)
#ifdef __mips_hard_float
  sdc1  $f0, (8 * 35)($4)
  sdc1  $f1, (8 * 36)($4)
  sdc1  $f2, (8 * 37)($4)
  sdc1  $f3, (8 * 38)($4)
  sdc1  $f4, (8 * 39)($4)
  sdc1  $f5, (8 * 40)($4)
  sdc1  $f6, (8 * 41)($4)
  sdc1  $f7, (8 * 42)($4)
  sdc1  $f8, (8 * 43)($4)
  sdc1  $f9, (8 * 44)($4)
  sdc1  $f10, (8 * 45)($4)
  sdc1  $f11, (8 * 46)($4)
  sdc1  $f12, (8 * 47)($4)
  sdc1  $f13, (8 * 48)($4)
  sdc1  $f14, (8 * 49)($4)
  sdc1  $f15, (8 * 50)($4)
  sdc1  $f16, (8 * 51)($4)
  sdc1  $f17, (8 * 52)($4)
  sdc1  $f18, (8 * 53)($4)
  sdc1  $f19, (8 * 54)($4)
  sdc1  $f20, (8 * 55)($4)
  sdc1  $f21, (8 * 56)($4)
  sdc1  $f22, (8 * 57)($4)
  sdc1  $f23, (8 * 58)($4)
  sdc1  $f24, (8 * 59)($4)
  sdc1  $f25, (8 * 60)($4)
  sdc1  $f26, (8 * 61)($4)
  sdc1  $f27, (8 * 62)($4)
  sdc1  $f28, (8 * 63)($4)
  sdc1  $f29, (8 * 64)($4)
  sdc1  $f30, (8 * 65)($4)
  sdc1  $f31, (8 * 66)($4)
#endif
  jr	$31
  # return UNW_ESUCCESS
  or    $2, $0, $0
  .set pop

# elif defined(__mips__)

#
# extern int unw_getcontext(unw_context_t* thread_state)
#
# Just trap for the time being.
DEFINE_LIBUNWIND_FUNCTION(unw_getcontext)
  teq $0, $0

#elif defined(__powerpc64__)

//
// extern int unw_getcontext(unw_context_t* thread_state)
//
// On entry:
//  thread_state pointer is in r3
//
DEFINE_LIBUNWIND_FUNCTION(unw_getcontext)

// store register (GPR)
#define PPC64_STR(n) \
  std   %r##n, (8 * (n + 2))(%r3)

  // save GPRs
  PPC64_STR(0)
  mflr  %r0
  std   %r0, PPC64_OFFS_SRR0(%r3) // store lr as ssr0
  PPC64_STR(1)
  PPC64_STR(2)
  PPC64_STR(3)
  PPC64_STR(4)
  PPC64_STR(5)
  PPC64_STR(6)
  PPC64_STR(7)
  PPC64_STR(8)
  PPC64_STR(9)
  PPC64_STR(10)
  PPC64_STR(11)
  PPC64_STR(12)
  PPC64_STR(13)
  PPC64_STR(14)
  PPC64_STR(15)
  PPC64_STR(16)
  PPC64_STR(17)
  PPC64_STR(18)
  PPC64_STR(19)
  PPC64_STR(20)
  PPC64_STR(21)
  PPC64_STR(22)
  PPC64_STR(23)
  PPC64_STR(24)
  PPC64_STR(25)
  PPC64_STR(26)
  PPC64_STR(27)
  PPC64_STR(28)
  PPC64_STR(29)
  PPC64_STR(30)
  PPC64_STR(31)

  mfcr  %r0
  std   %r0,  PPC64_OFFS_CR(%r3)
  mfxer %r0
  std   %r0,  PPC64_OFFS_XER(%r3)
  mflr  %r0
  std   %r0,  PPC64_OFFS_LR(%r3)
  mfctr %r0
  std   %r0,  PPC64_OFFS_CTR(%r3)
  mfvrsave    %r0
  std   %r0,  PPC64_OFFS_VRSAVE(%r3)

#ifdef PPC64_HAS_VMX
  // save VS registers
  // (note that this also saves floating point registers and V registers,
  // because part of VS is mapped to these registers)

  addi  %r4, %r3, PPC64_OFFS_FP

// store VS register
#define PPC64_STVS(n)      \
  stxvd2x %vs##n, 0, %r4  ;\
  addi    %r4, %r4, 16

  PPC64_STVS(0)
  PPC64_STVS(1)
  PPC64_STVS(2)
  PPC64_STVS(3)
  PPC64_STVS(4)
  PPC64_STVS(5)
  PPC64_STVS(6)
  PPC64_STVS(7)
  PPC64_STVS(8)
  PPC64_STVS(9)
  PPC64_STVS(10)
  PPC64_STVS(11)
  PPC64_STVS(12)
  PPC64_STVS(13)
  PPC64_STVS(14)
  PPC64_STVS(15)
  PPC64_STVS(16)
  PPC64_STVS(17)
  PPC64_STVS(18)
  PPC64_STVS(19)
  PPC64_STVS(20)
  PPC64_STVS(21)
  PPC64_STVS(22)
  PPC64_STVS(23)
  PPC64_STVS(24)
  PPC64_STVS(25)
  PPC64_STVS(26)
  PPC64_STVS(27)
  PPC64_STVS(28)
  PPC64_STVS(29)
  PPC64_STVS(30)
  PPC64_STVS(31)
  PPC64_STVS(32)
  PPC64_STVS(33)
  PPC64_STVS(34)
  PPC64_STVS(35)
  PPC64_STVS(36)
  PPC64_STVS(37)
  PPC64_STVS(38)
  PPC64_STVS(39)
  PPC64_STVS(40)
  PPC64_STVS(41)
  PPC64_STVS(42)
  PPC64_STVS(43)
  PPC64_STVS(44)
  PPC64_STVS(45)
  PPC64_STVS(46)
  PPC64_STVS(47)
  PPC64_STVS(48)
  PPC64_STVS(49)
  PPC64_STVS(50)
  PPC64_STVS(51)
  PPC64_STVS(52)
  PPC64_STVS(53)
  PPC64_STVS(54)
  PPC64_STVS(55)
  PPC64_STVS(56)
  PPC64_STVS(57)
  PPC64_STVS(58)
  PPC64_STVS(59)
  PPC64_STVS(60)
  PPC64_STVS(61)
  PPC64_STVS(62)
  PPC64_STVS(63)

#else

// store FP register
#define PPC64_STF(n) \
  stfd  %f##n, (PPC64_OFFS_FP + n * 16)(%r3)

  // save float registers
  PPC64_STF(0)
  PPC64_STF(1)
  PPC64_STF(2)
  PPC64_STF(3)
  PPC64_STF(4)
  PPC64_STF(5)
  PPC64_STF(6)
  PPC64_STF(7)
  PPC64_STF(8)
  PPC64_STF(9)
  PPC64_STF(10)
  PPC64_STF(11)
  PPC64_STF(12)
  PPC64_STF(13)
  PPC64_STF(14)
  PPC64_STF(15)
  PPC64_STF(16)
  PPC64_STF(17)
  PPC64_STF(18)
  PPC64_STF(19)
  PPC64_STF(20)
  PPC64_STF(21)
  PPC64_STF(22)
  PPC64_STF(23)
  PPC64_STF(24)
  PPC64_STF(25)
  PPC64_STF(26)
  PPC64_STF(27)
  PPC64_STF(28)
  PPC64_STF(29)
  PPC64_STF(30)
  PPC64_STF(31)

  // save vector registers

  // Use 16-bytes below the stack pointer as an
  // aligned buffer to save each vector register.
  // Note that the stack pointer is always 16-byte aligned.
  subi  %r4, %r1, 16

#define PPC64_STV_UNALIGNED(n)                 \
  stvx  %v##n, 0, %r4                         ;\
  ld    %r5, 0(%r4)                           ;\
  std   %r5, (PPC64_OFFS_V + n * 16)(%r3)     ;\
  ld    %r5, 8(%r4)                           ;\
  std   %r5, (PPC64_OFFS_V + n * 16 + 8)(%r3)

  PPC64_STV_UNALIGNED(0)
  PPC64_STV_UNALIGNED(1)
  PPC64_STV_UNALIGNED(2)
  PPC64_STV_UNALIGNED(3)
  PPC64_STV_UNALIGNED(4)
  PPC64_STV_UNALIGNED(5)
  PPC64_STV_UNALIGNED(6)
  PPC64_STV_UNALIGNED(7)
  PPC64_STV_UNALIGNED(8)
  PPC64_STV_UNALIGNED(9)
  PPC64_STV_UNALIGNED(10)
  PPC64_STV_UNALIGNED(11)
  PPC64_STV_UNALIGNED(12)
  PPC64_STV_UNALIGNED(13)
  PPC64_STV_UNALIGNED(14)
  PPC64_STV_UNALIGNED(15)
  PPC64_STV_UNALIGNED(16)
  PPC64_STV_UNALIGNED(17)
  PPC64_STV_UNALIGNED(18)
  PPC64_STV_UNALIGNED(19)
  PPC64_STV_UNALIGNED(20)
  PPC64_STV_UNALIGNED(21)
  PPC64_STV_UNALIGNED(22)
  PPC64_STV_UNALIGNED(23)
  PPC64_STV_UNALIGNED(24)
  PPC64_STV_UNALIGNED(25)
  PPC64_STV_UNALIGNED(26)
  PPC64_STV_UNALIGNED(27)
  PPC64_STV_UNALIGNED(28)
  PPC64_STV_UNALIGNED(29)
  PPC64_STV_UNALIGNED(30)
  PPC64_STV_UNALIGNED(31)

#endif

  li    %r3,  0   // return UNW_ESUCCESS
  blr


#elif defined(__ppc__)

;
; extern int unw_getcontext(unw_context_t* thread_state)
;
; On entry:
;  thread_state pointer is in r3
;
DEFINE_LIBUNWIND_FUNCTION(unw_getcontext)
  stw    r0,  8(r3)
  mflr  r0
  stw    r0,  0(r3)  ; store lr as ssr0
  stw    r1, 12(r3)
  stw    r2, 16(r3)
  stw    r3, 20(r3)
  stw    r4, 24(r3)
  stw    r5, 28(r3)
  stw    r6, 32(r3)
  stw    r7, 36(r3)
  stw    r8, 40(r3)
  stw    r9, 44(r3)
  stw     r10, 48(r3)
  stw     r11, 52(r3)
  stw     r12, 56(r3)
  stw     r13, 60(r3)
  stw     r14, 64(r3)
  stw     r15, 68(r3)
  stw     r16, 72(r3)
  stw     r17, 76(r3)
  stw     r18, 80(r3)
  stw     r19, 84(r3)
  stw     r20, 88(r3)
  stw     r21, 92(r3)
  stw     r22, 96(r3)
  stw     r23,100(r3)
  stw     r24,104(r3)
  stw     r25,108(r3)
  stw     r26,112(r3)
  stw     r27,116(r3)
  stw     r28,120(r3)
  stw     r29,124(r3)
  stw     r30,128(r3)
  stw     r31,132(r3)

  ; save VRSave register
  mfspr  r0,256
  stw    r0,156(r3)
  ; save CR registers
  mfcr  r0
  stw    r0,136(r3)
  ; save CTR register
  mfctr  r0
  stw    r0,148(r3)

  ; save float registers
  stfd    f0, 160(r3)
  stfd    f1, 168(r3)
  stfd    f2, 176(r3)
  stfd    f3, 184(r3)
  stfd    f4, 192(r3)
  stfd    f5, 200(r3)
  stfd    f6, 208(r3)
  stfd    f7, 216(r3)
  stfd    f8, 224(r3)
  stfd    f9, 232(r3)
  stfd    f10,240(r3)
  stfd    f11,248(r3)
  stfd    f12,256(r3)
  stfd    f13,264(r3)
  stfd    f14,272(r3)
  stfd    f15,280(r3)
  stfd    f16,288(r3)
  stfd    f17,296(r3)
  stfd    f18,304(r3)
  stfd    f19,312(r3)
  stfd    f20,320(r3)
  stfd    f21,328(r3)
  stfd    f22,336(r3)
  stfd    f23,344(r3)
  stfd    f24,352(r3)
  stfd    f25,360(r3)
  stfd    f26,368(r3)
  stfd    f27,376(r3)
  stfd    f28,384(r3)
  stfd    f29,392(r3)
  stfd    f30,400(r3)
  stfd    f31,408(r3)


  ; save vector registers

  subi  r4,r1,16
  rlwinm  r4,r4,0,0,27  ; mask low 4-bits
  ; r4 is now a 16-byte aligned pointer into the red zone

#define SAVE_VECTOR_UNALIGNED(_vec, _offset) \
  stvx  _vec,0,r4           @\
  lwz    r5, 0(r4)          @\
  stw    r5, _offset(r3)    @\
  lwz    r5, 4(r4)          @\
  stw    r5, _offset+4(r3)  @\
  lwz    r5, 8(r4)          @\
  stw    r5, _offset+8(r3)  @\
  lwz    r5, 12(r4)         @\
  stw    r5, _offset+12(r3)

  SAVE_VECTOR_UNALIGNED( v0, 424+0x000)
  SAVE_VECTOR_UNALIGNED( v1, 424+0x010)
  SAVE_VECTOR_UNALIGNED( v2, 424+0x020)
  SAVE_VECTOR_UNALIGNED( v3, 424+0x030)
  SAVE_VECTOR_UNALIGNED( v4, 424+0x040)
  SAVE_VECTOR_UNALIGNED( v5, 424+0x050)
  SAVE_VECTOR_UNALIGNED( v6, 424+0x060)
  SAVE_VECTOR_UNALIGNED( v7, 424+0x070)
  SAVE_VECTOR_UNALIGNED( v8, 424+0x080)
  SAVE_VECTOR_UNALIGNED( v9, 424+0x090)
  SAVE_VECTOR_UNALIGNED(v10, 424+0x0A0)
  SAVE_VECTOR_UNALIGNED(v11, 424+0x0B0)
  SAVE_VECTOR_UNALIGNED(v12, 424+0x0C0)
  SAVE_VECTOR_UNALIGNED(v13, 424+0x0D0)
  SAVE_VECTOR_UNALIGNED(v14, 424+0x0E0)
  SAVE_VECTOR_UNALIGNED(v15, 424+0x0F0)
  SAVE_VECTOR_UNALIGNED(v16, 424+0x100)
  SAVE_VECTOR_UNALIGNED(v17, 424+0x110)
  SAVE_VECTOR_UNALIGNED(v18, 424+0x120)
  SAVE_VECTOR_UNALIGNED(v19, 424+0x130)
  SAVE_VECTOR_UNALIGNED(v20, 424+0x140)
  SAVE_VECTOR_UNALIGNED(v21, 424+0x150)
  SAVE_VECTOR_UNALIGNED(v22, 424+0x160)
  SAVE_VECTOR_UNALIGNED(v23, 424+0x170)
  SAVE_VECTOR_UNALIGNED(v24, 424+0x180)
  SAVE_VECTOR_UNALIGNED(v25, 424+0x190)
  SAVE_VECTOR_UNALIGNED(v26, 424+0x1A0)
  SAVE_VECTOR_UNALIGNED(v27, 424+0x1B0)
  SAVE_VECTOR_UNALIGNED(v28, 424+0x1C0)
  SAVE_VECTOR_UNALIGNED(v29, 424+0x1D0)
  SAVE_VECTOR_UNALIGNED(v30, 424+0x1E0)
  SAVE_VECTOR_UNALIGNED(v31, 424+0x1F0)

  li  r3, 0    ; return UNW_ESUCCESS
  blr


#elif defined(__arm64__) || defined(__aarch64__)

//
// extern int unw_getcontext(unw_context_t* thread_state)
//
// On entry:
//  thread_state pointer is in x0
//
  .p2align 2
DEFINE_LIBUNWIND_FUNCTION(unw_getcontext)
  stp    x0, x1,  [x0, #0x000]
  stp    x2, x3,  [x0, #0x010]
  stp    x4, x5,  [x0, #0x020]
  stp    x6, x7,  [x0, #0x030]
  stp    x8, x9,  [x0, #0x040]
  stp    x10,x11, [x0, #0x050]
  stp    x12,x13, [x0, #0x060]
  stp    x14,x15, [x0, #0x070]
  stp    x16,x17, [x0, #0x080]
  stp    x18,x19, [x0, #0x090]
  stp    x20,x21, [x0, #0x0A0]
  stp    x22,x23, [x0, #0x0B0]
  stp    x24,x25, [x0, #0x0C0]
  stp    x26,x27, [x0, #0x0D0]
  stp    x28,x29, [x0, #0x0E0]
  str    x30,     [x0, #0x0F0]
  mov    x1,sp
  str    x1,      [x0, #0x0F8]
  str    x30,     [x0, #0x100]    // store return address as pc
  // skip cpsr
  stp    d0, d1,  [x0, #0x110]
  stp    d2, d3,  [x0, #0x120]
  stp    d4, d5,  [x0, #0x130]
  stp    d6, d7,  [x0, #0x140]
  stp    d8, d9,  [x0, #0x150]
  stp    d10,d11, [x0, #0x160]
  stp    d12,d13, [x0, #0x170]
  stp    d14,d15, [x0, #0x180]
  stp    d16,d17, [x0, #0x190]
  stp    d18,d19, [x0, #0x1A0]
  stp    d20,d21, [x0, #0x1B0]
  stp    d22,d23, [x0, #0x1C0]
  stp    d24,d25, [x0, #0x1D0]
  stp    d26,d27, [x0, #0x1E0]
  stp    d28,d29, [x0, #0x1F0]
  str    d30,     [x0, #0x200]
  str    d31,     [x0, #0x208]
  mov    x0, #0                   // return UNW_ESUCCESS
  ret

#elif defined(__arm__) && !defined(__APPLE__)

#if !defined(__ARM_ARCH_ISA_ARM)
  .thumb
#endif

@
@ extern int unw_getcontext(unw_context_t* thread_state)
@
@ On entry:
@  thread_state pointer is in r0
@ 
@ Per EHABI #4.7 this only saves the core integer registers.
@ EHABI #7.4.5 notes that in general all VRS registers should be restored
@ however this is very hard to do for VFP registers because it is unknown
@ to the library how many registers are implemented by the architecture.
@ Instead, VFP registers are demand saved by logic external to unw_getcontext.
@
  .p2align 2
DEFINE_LIBUNWIND_FUNCTION(unw_getcontext)
#if !defined(__ARM_ARCH_ISA_ARM) && __ARM_ARCH_ISA_THUMB == 1
  stm r0!, {r0-r7}
  mov r1, r8
  mov r2, r9
  mov r3, r10
  stm r0!, {r1-r3}
  mov r1, r11
  mov r2, sp
  mov r3, lr
  str r1, [r0, #0]   @ r11
  @ r12 does not need storing, it it the intra-procedure-call scratch register
  str r2, [r0, #8]   @ sp
  str r3, [r0, #12]  @ lr
  str r3, [r0, #16]  @ store return address as pc
  @ T1 does not have a non-cpsr-clobbering register-zeroing instruction.
  @ It is safe to use here though because we are about to return, and cpsr is
  @ not expected to be preserved.
  movs r0, #0        @ return UNW_ESUCCESS
#else
  @ 32bit thumb-2 restrictions for stm:
  @ . the sp (r13) cannot be in the list
  @ . the pc (r15) cannot be in the list in an STM instruction
  stm r0, {r0-r12}
  str sp, [r0, #52]
  str lr, [r0, #56]
  str lr, [r0, #60]  @ store return address as pc
  mov r0, #0         @ return UNW_ESUCCESS
#endif
  JMP(lr)

@
@ static void libunwind::Registers_arm::saveVFPWithFSTMD(unw_fpreg_t* values)
@
@ On entry:
@  values pointer is in r0
@
  .p2align 2
#if defined(__ELF__)
  .fpu vfpv3-d16
#endif
DEFINE_LIBUNWIND_PRIVATE_FUNCTION(_ZN9libunwind13Registers_arm16saveVFPWithFSTMDEPy)
  vstmia r0, {d0-d15}
  JMP(lr)

@
@ static void libunwind::Registers_arm::saveVFPWithFSTMX(unw_fpreg_t* values)
@
@ On entry:
@  values pointer is in r0
@
  .p2align 2
#if defined(__ELF__)
  .fpu vfpv3-d16
#endif
DEFINE_LIBUNWIND_PRIVATE_FUNCTION(_ZN9libunwind13Registers_arm16saveVFPWithFSTMXEPy)
  vstmia r0, {d0-d15} @ fstmiax is deprecated in ARMv7+ and now behaves like vstmia
  JMP(lr)

@
@ static void libunwind::Registers_arm::saveVFPv3(unw_fpreg_t* values)
@
@ On entry:
@  values pointer is in r0
@
  .p2align 2
#if defined(__ELF__)
  .fpu vfpv3
#endif
DEFINE_LIBUNWIND_PRIVATE_FUNCTION(_ZN9libunwind13Registers_arm9saveVFPv3EPy)
  @ VFP and iwMMX instructions are only available when compiling with the flags
  @ that enable them. We do not want to do that in the library (because we do not
  @ want the compiler to generate instructions that access those) but this is
  @ only accessed if the personality routine needs these registers. Use of
  @ these registers implies they are, actually, available on the target, so
  @ it's ok to execute.
  @ So, generate the instructions using the corresponding coprocessor mnemonic.
  vstmia r0, {d16-d31}
  JMP(lr)

#if defined(_LIBUNWIND_ARM_WMMX)

@
@ static void libunwind::Registers_arm::saveiWMMX(unw_fpreg_t* values)
@
@ On entry:
@  values pointer is in r0
@
  .p2align 2
#if defined(__ELF__)
  .arch armv5te
#endif
DEFINE_LIBUNWIND_PRIVATE_FUNCTION(_ZN9libunwind13Registers_arm9saveiWMMXEPy)
  stcl p1, cr0, [r0], #8  @ wstrd wR0, [r0], #8
  stcl p1, cr1, [r0], #8  @ wstrd wR1, [r0], #8
  stcl p1, cr2, [r0], #8  @ wstrd wR2, [r0], #8
  stcl p1, cr3, [r0], #8  @ wstrd wR3, [r0], #8
  stcl p1, cr4, [r0], #8  @ wstrd wR4, [r0], #8
  stcl p1, cr5, [r0], #8  @ wstrd wR5, [r0], #8
  stcl p1, cr6, [r0], #8  @ wstrd wR6, [r0], #8
  stcl p1, cr7, [r0], #8  @ wstrd wR7, [r0], #8
  stcl p1, cr8, [r0], #8  @ wstrd wR8, [r0], #8
  stcl p1, cr9, [r0], #8  @ wstrd wR9, [r0], #8
  stcl p1, cr10, [r0], #8  @ wstrd wR10, [r0], #8
  stcl p1, cr11, [r0], #8  @ wstrd wR11, [r0], #8
  stcl p1, cr12, [r0], #8  @ wstrd wR12, [r0], #8
  stcl p1, cr13, [r0], #8  @ wstrd wR13, [r0], #8
  stcl p1, cr14, [r0], #8  @ wstrd wR14, [r0], #8
  stcl p1, cr15, [r0], #8  @ wstrd wR15, [r0], #8
  JMP(lr)

@
@ static void libunwind::Registers_arm::saveiWMMXControl(unw_uint32_t* values)
@
@ On entry:
@  values pointer is in r0
@
  .p2align 2
#if defined(__ELF__)
  .arch armv5te
#endif
DEFINE_LIBUNWIND_PRIVATE_FUNCTION(_ZN9libunwind13Registers_arm16saveiWMMXControlEPj)
  stc2 p1, cr8, [r0], #4  @ wstrw wCGR0, [r0], #4
  stc2 p1, cr9, [r0], #4  @ wstrw wCGR1, [r0], #4
  stc2 p1, cr10, [r0], #4  @ wstrw wCGR2, [r0], #4
  stc2 p1, cr11, [r0], #4  @ wstrw wCGR3, [r0], #4
  JMP(lr)

#endif

#elif defined(__or1k__)

#
# extern int unw_getcontext(unw_context_t* thread_state)
#
# On entry:
#  thread_state pointer is in r3
#
DEFINE_LIBUNWIND_FUNCTION(unw_getcontext)
  l.sw       0(r3), r0
  l.sw       4(r3), r1
  l.sw       8(r3), r2
  l.sw      12(r3), r3
  l.sw      16(r3), r4
  l.sw      20(r3), r5
  l.sw      24(r3), r6
  l.sw      28(r3), r7
  l.sw      32(r3), r8
  l.sw      36(r3), r9
  l.sw      40(r3), r10
  l.sw      44(r3), r11
  l.sw      48(r3), r12
  l.sw      52(r3), r13
  l.sw      56(r3), r14
  l.sw      60(r3), r15
  l.sw      64(r3), r16
  l.sw      68(r3), r17
  l.sw      72(r3), r18
  l.sw      76(r3), r19
  l.sw      80(r3), r20
  l.sw      84(r3), r21
  l.sw      88(r3), r22
  l.sw      92(r3), r23
  l.sw      96(r3), r24
  l.sw     100(r3), r25
  l.sw     104(r3), r26
  l.sw     108(r3), r27
  l.sw     112(r3), r28
  l.sw     116(r3), r29
  l.sw     120(r3), r30
  l.sw     124(r3), r31
  # store ra to pc
  l.sw     128(r3), r9
  # zero epcr
  l.sw     132(r3), r0

#elif defined(__riscv)

#
# extern int unw_getcontext(unw_context_t* thread_state)
#
# On entry:
#  thread_state pointer is in a0
#
DEFINE_LIBUNWIND_FUNCTION(unw_getcontext)
  // x0 is zero
  sd    x1, (8 * 1)(a0)
  sd    x2, (8 * 2)(a0)
  sd    x3, (8 * 3)(a0)
  sd    x4, (8 * 4)(a0)
  sd    x5, (8 * 5)(a0)
  sd    x6, (8 * 6)(a0)
  sd    x7, (8 * 7)(a0)
  sd    x8, (8 * 8)(a0)
  sd    x9, (8 * 9)(a0)
  sd    x10, (8 * 10)(a0)
  sd    x11, (8 * 11)(a0)
  sd    x12, (8 * 12)(a0)
  sd    x13, (8 * 13)(a0)
  sd    x14, (8 * 14)(a0)
  sd    x15, (8 * 15)(a0)
  sd    x16, (8 * 16)(a0)
  sd    x17, (8 * 17)(a0)
  sd    x18, (8 * 18)(a0)
  sd    x19, (8 * 19)(a0)
  sd    x20, (8 * 20)(a0)
  sd    x21, (8 * 21)(a0)
  sd    x22, (8 * 22)(a0)
  sd    x23, (8 * 23)(a0)
  sd    x24, (8 * 24)(a0)
  sd    x25, (8 * 25)(a0)
  sd    x26, (8 * 26)(a0)
  sd    x27, (8 * 27)(a0)
  sd    x28, (8 * 28)(a0)
  sd    x29, (8 * 29)(a0)
  sd    x30, (8 * 30)(a0)
  sd    x31, (8 * 31)(a0)

#ifdef __riscv_float_abi_double
  fsd    f0, (8 * 32 + 8 * 0)(a0)
  fsd    f1, (8 * 32 + 8 * 1)(a0)
  fsd    f2, (8 * 32 + 8 * 2)(a0)
  fsd    f3, (8 * 32 + 8 * 3)(a0)
  fsd    f4, (8 * 32 + 8 * 4)(a0)
  fsd    f5, (8 * 32 + 8 * 5)(a0)
  fsd    f6, (8 * 32 + 8 * 6)(a0)
  fsd    f7, (8 * 32 + 8 * 7)(a0)
  fsd    f8, (8 * 32 + 8 * 8)(a0)
  fsd    f9, (8 * 32 + 8 * 9)(a0)
  fsd    f10, (8 * 32 + 8 * 10)(a0)
  fsd    f11, (8 * 32 + 8 * 11)(a0)
  fsd    f12, (8 * 32 + 8 * 12)(a0)
  fsd    f13, (8 * 32 + 8 * 13)(a0)
  fsd    f14, (8 * 32 + 8 * 14)(a0)
  fsd    f15, (8 * 32 + 8 * 15)(a0)
  fsd    f16, (8 * 32 + 8 * 16)(a0)
  fsd    f17, (8 * 32 + 8 * 17)(a0)
  fsd    f18, (8 * 32 + 8 * 18)(a0)
  fsd    f19, (8 * 32 + 8 * 19)(a0)
  fsd    f20, (8 * 32 + 8 * 20)(a0)
  fsd    f21, (8 * 32 + 8 * 21)(a0)
  fsd    f22, (8 * 32 + 8 * 22)(a0)
  fsd    f23, (8 * 32 + 8 * 23)(a0)
  fsd    f24, (8 * 32 + 8 * 24)(a0)
  fsd    f25, (8 * 32 + 8 * 25)(a0)
  fsd    f26, (8 * 32 + 8 * 26)(a0)
  fsd    f27, (8 * 32 + 8 * 27)(a0)
  fsd    f28, (8 * 32 + 8 * 28)(a0)
  fsd    f29, (8 * 32 + 8 * 29)(a0)
  fsd    f30, (8 * 32 + 8 * 30)(a0)
  fsd    f31, (8 * 32 + 8 * 31)(a0)
#endif

  li     a0, 0  // return UNW_ESUCCESS
  ret           // jump to ra

#elif defined(__sparc__)

#
# extern int unw_getcontext(unw_context_t* thread_state)
#
# On entry:
#  thread_state pointer is in o0
#
DEFINE_LIBUNWIND_FUNCTION(unw_getcontext)
  ta 3
  add %o7, 8, %o7
  std %g0, [%o0 +   0]
  std %g2, [%o0 +   8]
  std %g4, [%o0 +  16]
  std %g6, [%o0 +  24]
  std %o0, [%o0 +  32]
  std %o2, [%o0 +  40]
  std %o4, [%o0 +  48]
  std %o6, [%o0 +  56]
  std %l0, [%o0 +  64]
  std %l2, [%o0 +  72]
  std %l4, [%o0 +  80]
  std %l6, [%o0 +  88]
  std %i0, [%o0 +  96]
  std %i2, [%o0 + 104]
  std %i4, [%o0 + 112]
  std %i6, [%o0 + 120]
  jmp %o7
   clr %o0                   // return UNW_ESUCCESS
#endif
#endif /* !defined(__USING_SJLJ_EXCEPTIONS__) */

NO_EXEC_STACK_DIRECTIVE
