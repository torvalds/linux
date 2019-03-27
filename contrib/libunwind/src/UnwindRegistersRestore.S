//===-------------------- UnwindRegistersRestore.S ------------------------===//
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
DEFINE_LIBUNWIND_PRIVATE_FUNCTION(_ZN9libunwind13Registers_x866jumptoEv)
#
# void libunwind::Registers_x86::jumpto()
#
#if defined(_WIN32)
# On windows, the 'this' pointer is passed in ecx instead of on the stack
  movl   %ecx, %eax
#else
# On entry:
#  +                       +
#  +-----------------------+
#  + thread_state pointer  +
#  +-----------------------+
#  + return address        +
#  +-----------------------+   <-- SP
#  +                       +
  movl   4(%esp), %eax
#endif
  # set up eax and ret on new stack location
  movl  28(%eax), %edx # edx holds new stack pointer
  subl  $8,%edx
  movl  %edx, 28(%eax)
  movl  0(%eax), %ebx
  movl  %ebx, 0(%edx)
  movl  40(%eax), %ebx
  movl  %ebx, 4(%edx)
  # we now have ret and eax pushed onto where new stack will be
  # restore all registers
  movl   4(%eax), %ebx
  movl   8(%eax), %ecx
  movl  12(%eax), %edx
  movl  16(%eax), %edi
  movl  20(%eax), %esi
  movl  24(%eax), %ebp
  movl  28(%eax), %esp
  # skip ss
  # skip eflags
  pop    %eax  # eax was already pushed on new stack
  ret        # eip was already pushed on new stack
  # skip cs
  # skip ds
  # skip es
  # skip fs
  # skip gs

#elif defined(__x86_64__)

DEFINE_LIBUNWIND_PRIVATE_FUNCTION(_ZN9libunwind16Registers_x86_646jumptoEv)
#
# void libunwind::Registers_x86_64::jumpto()
#
#if defined(_WIN64)
# On entry, thread_state pointer is in rcx; move it into rdi
# to share restore code below. Since this routine restores and
# overwrites all registers, we can use the same registers for
# pointers and temporaries as on unix even though win64 normally
# mustn't clobber some of them.
  movq  %rcx, %rdi
#else
# On entry, thread_state pointer is in rdi
#endif

  movq  56(%rdi), %rax # rax holds new stack pointer
  subq  $16, %rax
  movq  %rax, 56(%rdi)
  movq  32(%rdi), %rbx  # store new rdi on new stack
  movq  %rbx, 0(%rax)
  movq  128(%rdi), %rbx # store new rip on new stack
  movq  %rbx, 8(%rax)
  # restore all registers
  movq    0(%rdi), %rax
  movq    8(%rdi), %rbx
  movq   16(%rdi), %rcx
  movq   24(%rdi), %rdx
  # restore rdi later
  movq   40(%rdi), %rsi
  movq   48(%rdi), %rbp
  # restore rsp later
  movq   64(%rdi), %r8
  movq   72(%rdi), %r9
  movq   80(%rdi), %r10
  movq   88(%rdi), %r11
  movq   96(%rdi), %r12
  movq  104(%rdi), %r13
  movq  112(%rdi), %r14
  movq  120(%rdi), %r15
  # skip rflags
  # skip cs
  # skip fs
  # skip gs

#if defined(_WIN64)
  movdqu 176(%rdi),%xmm0
  movdqu 192(%rdi),%xmm1
  movdqu 208(%rdi),%xmm2
  movdqu 224(%rdi),%xmm3
  movdqu 240(%rdi),%xmm4
  movdqu 256(%rdi),%xmm5
  movdqu 272(%rdi),%xmm6
  movdqu 288(%rdi),%xmm7
  movdqu 304(%rdi),%xmm8
  movdqu 320(%rdi),%xmm9
  movdqu 336(%rdi),%xmm10
  movdqu 352(%rdi),%xmm11
  movdqu 368(%rdi),%xmm12
  movdqu 384(%rdi),%xmm13
  movdqu 400(%rdi),%xmm14
  movdqu 416(%rdi),%xmm15
#endif
  movq  56(%rdi), %rsp  # cut back rsp to new location
  pop    %rdi      # rdi was saved here earlier
  ret            # rip was saved here


#elif defined(__powerpc64__)

DEFINE_LIBUNWIND_PRIVATE_FUNCTION(_ZN9libunwind15Registers_ppc646jumptoEv)
//
// void libunwind::Registers_ppc64::jumpto()
//
// On entry:
//  thread_state pointer is in r3
//

// load register (GPR)
#define PPC64_LR(n) \
  ld    %r##n, (8 * (n + 2))(%r3)

  // restore integral registers
  // skip r0 for now
  // skip r1 for now
  PPC64_LR(2)
  // skip r3 for now
  // skip r4 for now
  // skip r5 for now
  PPC64_LR(6)
  PPC64_LR(7)
  PPC64_LR(8)
  PPC64_LR(9)
  PPC64_LR(10)
  PPC64_LR(11)
  PPC64_LR(12)
  PPC64_LR(13)
  PPC64_LR(14)
  PPC64_LR(15)
  PPC64_LR(16)
  PPC64_LR(17)
  PPC64_LR(18)
  PPC64_LR(19)
  PPC64_LR(20)
  PPC64_LR(21)
  PPC64_LR(22)
  PPC64_LR(23)
  PPC64_LR(24)
  PPC64_LR(25)
  PPC64_LR(26)
  PPC64_LR(27)
  PPC64_LR(28)
  PPC64_LR(29)
  PPC64_LR(30)
  PPC64_LR(31)

#ifdef PPC64_HAS_VMX

  // restore VS registers
  // (note that this also restores floating point registers and V registers,
  // because part of VS is mapped to these registers)

  addi  %r4, %r3, PPC64_OFFS_FP

// load VS register
#define PPC64_LVS(n)         \
  lxvd2x  %vs##n, 0, %r4    ;\
  addi    %r4, %r4, 16

  // restore the first 32 VS regs (and also all floating point regs)
  PPC64_LVS(0)
  PPC64_LVS(1)
  PPC64_LVS(2)
  PPC64_LVS(3)
  PPC64_LVS(4)
  PPC64_LVS(5)
  PPC64_LVS(6)
  PPC64_LVS(7)
  PPC64_LVS(8)
  PPC64_LVS(9)
  PPC64_LVS(10)
  PPC64_LVS(11)
  PPC64_LVS(12)
  PPC64_LVS(13)
  PPC64_LVS(14)
  PPC64_LVS(15)
  PPC64_LVS(16)
  PPC64_LVS(17)
  PPC64_LVS(18)
  PPC64_LVS(19)
  PPC64_LVS(20)
  PPC64_LVS(21)
  PPC64_LVS(22)
  PPC64_LVS(23)
  PPC64_LVS(24)
  PPC64_LVS(25)
  PPC64_LVS(26)
  PPC64_LVS(27)
  PPC64_LVS(28)
  PPC64_LVS(29)
  PPC64_LVS(30)
  PPC64_LVS(31)

  // use VRSAVE to conditionally restore the remaining VS regs,
  // that are where the V regs are mapped

  ld    %r5, PPC64_OFFS_VRSAVE(%r3)   // test VRsave
  cmpwi %r5, 0
  beq   Lnovec

// conditionally load VS
#define PPC64_CLVS_BOTTOM(n)               \
  beq    Ldone##n                         ;\
  addi   %r4, %r3, PPC64_OFFS_FP + n * 16 ;\
  lxvd2x %vs##n, 0, %r4                   ;\
Ldone##n:

#define PPC64_CLVSl(n)           \
  andis. %r0, %r5, (1<<(47-n))  ;\
PPC64_CLVS_BOTTOM(n)

#define PPC64_CLVSh(n)           \
  andi.  %r0, %r5, (1<<(63-n))  ;\
PPC64_CLVS_BOTTOM(n)

  PPC64_CLVSl(32)
  PPC64_CLVSl(33)
  PPC64_CLVSl(34)
  PPC64_CLVSl(35)
  PPC64_CLVSl(36)
  PPC64_CLVSl(37)
  PPC64_CLVSl(38)
  PPC64_CLVSl(39)
  PPC64_CLVSl(40)
  PPC64_CLVSl(41)
  PPC64_CLVSl(42)
  PPC64_CLVSl(43)
  PPC64_CLVSl(44)
  PPC64_CLVSl(45)
  PPC64_CLVSl(46)
  PPC64_CLVSl(47)
  PPC64_CLVSh(48)
  PPC64_CLVSh(49)
  PPC64_CLVSh(50)
  PPC64_CLVSh(51)
  PPC64_CLVSh(52)
  PPC64_CLVSh(53)
  PPC64_CLVSh(54)
  PPC64_CLVSh(55)
  PPC64_CLVSh(56)
  PPC64_CLVSh(57)
  PPC64_CLVSh(58)
  PPC64_CLVSh(59)
  PPC64_CLVSh(60)
  PPC64_CLVSh(61)
  PPC64_CLVSh(62)
  PPC64_CLVSh(63)

#else

// load FP register
#define PPC64_LF(n) \
  lfd   %f##n, (PPC64_OFFS_FP + n * 16)(%r3)

  // restore float registers
  PPC64_LF(0)
  PPC64_LF(1)
  PPC64_LF(2)
  PPC64_LF(3)
  PPC64_LF(4)
  PPC64_LF(5)
  PPC64_LF(6)
  PPC64_LF(7)
  PPC64_LF(8)
  PPC64_LF(9)
  PPC64_LF(10)
  PPC64_LF(11)
  PPC64_LF(12)
  PPC64_LF(13)
  PPC64_LF(14)
  PPC64_LF(15)
  PPC64_LF(16)
  PPC64_LF(17)
  PPC64_LF(18)
  PPC64_LF(19)
  PPC64_LF(20)
  PPC64_LF(21)
  PPC64_LF(22)
  PPC64_LF(23)
  PPC64_LF(24)
  PPC64_LF(25)
  PPC64_LF(26)
  PPC64_LF(27)
  PPC64_LF(28)
  PPC64_LF(29)
  PPC64_LF(30)
  PPC64_LF(31)

  // restore vector registers if any are in use
  ld    %r5, PPC64_OFFS_VRSAVE(%r3)   // test VRsave
  cmpwi %r5, 0
  beq   Lnovec

  subi  %r4, %r1, 16
  // r4 is now a 16-byte aligned pointer into the red zone
  // the _vectorScalarRegisters may not be 16-byte aligned
  // so copy via red zone temp buffer

#define PPC64_CLV_UNALIGNED_BOTTOM(n)            \
  beq    Ldone##n                               ;\
  ld     %r0, (PPC64_OFFS_V + n * 16)(%r3)      ;\
  std    %r0, 0(%r4)                            ;\
  ld     %r0, (PPC64_OFFS_V + n * 16 + 8)(%r3)  ;\
  std    %r0, 8(%r4)                            ;\
  lvx    %v##n, 0, %r4                          ;\
Ldone  ## n:

#define PPC64_CLV_UNALIGNEDl(n)  \
  andis. %r0, %r5, (1<<(15-n))  ;\
PPC64_CLV_UNALIGNED_BOTTOM(n)

#define PPC64_CLV_UNALIGNEDh(n)  \
  andi.  %r0, %r5, (1<<(31-n))  ;\
PPC64_CLV_UNALIGNED_BOTTOM(n)

  PPC64_CLV_UNALIGNEDl(0)
  PPC64_CLV_UNALIGNEDl(1)
  PPC64_CLV_UNALIGNEDl(2)
  PPC64_CLV_UNALIGNEDl(3)
  PPC64_CLV_UNALIGNEDl(4)
  PPC64_CLV_UNALIGNEDl(5)
  PPC64_CLV_UNALIGNEDl(6)
  PPC64_CLV_UNALIGNEDl(7)
  PPC64_CLV_UNALIGNEDl(8)
  PPC64_CLV_UNALIGNEDl(9)
  PPC64_CLV_UNALIGNEDl(10)
  PPC64_CLV_UNALIGNEDl(11)
  PPC64_CLV_UNALIGNEDl(12)
  PPC64_CLV_UNALIGNEDl(13)
  PPC64_CLV_UNALIGNEDl(14)
  PPC64_CLV_UNALIGNEDl(15)
  PPC64_CLV_UNALIGNEDh(16)
  PPC64_CLV_UNALIGNEDh(17)
  PPC64_CLV_UNALIGNEDh(18)
  PPC64_CLV_UNALIGNEDh(19)
  PPC64_CLV_UNALIGNEDh(20)
  PPC64_CLV_UNALIGNEDh(21)
  PPC64_CLV_UNALIGNEDh(22)
  PPC64_CLV_UNALIGNEDh(23)
  PPC64_CLV_UNALIGNEDh(24)
  PPC64_CLV_UNALIGNEDh(25)
  PPC64_CLV_UNALIGNEDh(26)
  PPC64_CLV_UNALIGNEDh(27)
  PPC64_CLV_UNALIGNEDh(28)
  PPC64_CLV_UNALIGNEDh(29)
  PPC64_CLV_UNALIGNEDh(30)
  PPC64_CLV_UNALIGNEDh(31)

#endif

Lnovec:
  ld    %r0, PPC64_OFFS_CR(%r3)
  mtcr  %r0
  ld    %r0, PPC64_OFFS_SRR0(%r3)
  mtctr %r0

  PPC64_LR(0)
  PPC64_LR(5)
  PPC64_LR(4)
  PPC64_LR(1)
  PPC64_LR(3)
  bctr

#elif defined(__ppc__)

DEFINE_LIBUNWIND_PRIVATE_FUNCTION(_ZN9libunwind13Registers_ppc6jumptoEv)
;
; void libunwind::Registers_ppc::jumpto()
;
; On entry:
;  thread_state pointer is in r3
;

  ; restore integral registerrs
  ; skip r0 for now
  ; skip r1 for now
  lwz     r2, 16(r3)
  ; skip r3 for now
  ; skip r4 for now
  ; skip r5 for now
  lwz     r6, 32(r3)
  lwz     r7, 36(r3)
  lwz     r8, 40(r3)
  lwz     r9, 44(r3)
  lwz    r10, 48(r3)
  lwz    r11, 52(r3)
  lwz    r12, 56(r3)
  lwz    r13, 60(r3)
  lwz    r14, 64(r3)
  lwz    r15, 68(r3)
  lwz    r16, 72(r3)
  lwz    r17, 76(r3)
  lwz    r18, 80(r3)
  lwz    r19, 84(r3)
  lwz    r20, 88(r3)
  lwz    r21, 92(r3)
  lwz    r22, 96(r3)
  lwz    r23,100(r3)
  lwz    r24,104(r3)
  lwz    r25,108(r3)
  lwz    r26,112(r3)
  lwz    r27,116(r3)
  lwz    r28,120(r3)
  lwz    r29,124(r3)
  lwz    r30,128(r3)
  lwz    r31,132(r3)

  ; restore float registers
  lfd    f0, 160(r3)
  lfd    f1, 168(r3)
  lfd    f2, 176(r3)
  lfd    f3, 184(r3)
  lfd    f4, 192(r3)
  lfd    f5, 200(r3)
  lfd    f6, 208(r3)
  lfd    f7, 216(r3)
  lfd    f8, 224(r3)
  lfd    f9, 232(r3)
  lfd    f10,240(r3)
  lfd    f11,248(r3)
  lfd    f12,256(r3)
  lfd    f13,264(r3)
  lfd    f14,272(r3)
  lfd    f15,280(r3)
  lfd    f16,288(r3)
  lfd    f17,296(r3)
  lfd    f18,304(r3)
  lfd    f19,312(r3)
  lfd    f20,320(r3)
  lfd    f21,328(r3)
  lfd    f22,336(r3)
  lfd    f23,344(r3)
  lfd    f24,352(r3)
  lfd    f25,360(r3)
  lfd    f26,368(r3)
  lfd    f27,376(r3)
  lfd    f28,384(r3)
  lfd    f29,392(r3)
  lfd    f30,400(r3)
  lfd    f31,408(r3)

  ; restore vector registers if any are in use
  lwz    r5,156(r3)  ; test VRsave
  cmpwi  r5,0
  beq    Lnovec

  subi  r4,r1,16
  rlwinm  r4,r4,0,0,27  ; mask low 4-bits
  ; r4 is now a 16-byte aligned pointer into the red zone
  ; the _vectorRegisters may not be 16-byte aligned so copy via red zone temp buffer


#define LOAD_VECTOR_UNALIGNEDl(_index) \
  andis.  r0,r5,(1<<(15-_index))  @\
  beq    Ldone  ## _index     @\
  lwz    r0, 424+_index*16(r3)  @\
  stw    r0, 0(r4)        @\
  lwz    r0, 424+_index*16+4(r3)  @\
  stw    r0, 4(r4)        @\
  lwz    r0, 424+_index*16+8(r3)  @\
  stw    r0, 8(r4)        @\
  lwz    r0, 424+_index*16+12(r3)@\
  stw    r0, 12(r4)        @\
  lvx    v ## _index,0,r4    @\
Ldone  ## _index:

#define LOAD_VECTOR_UNALIGNEDh(_index) \
  andi.  r0,r5,(1<<(31-_index))  @\
  beq    Ldone  ## _index    @\
  lwz    r0, 424+_index*16(r3)  @\
  stw    r0, 0(r4)        @\
  lwz    r0, 424+_index*16+4(r3)  @\
  stw    r0, 4(r4)        @\
  lwz    r0, 424+_index*16+8(r3)  @\
  stw    r0, 8(r4)        @\
  lwz    r0, 424+_index*16+12(r3)@\
  stw    r0, 12(r4)        @\
  lvx    v ## _index,0,r4    @\
  Ldone  ## _index:


  LOAD_VECTOR_UNALIGNEDl(0)
  LOAD_VECTOR_UNALIGNEDl(1)
  LOAD_VECTOR_UNALIGNEDl(2)
  LOAD_VECTOR_UNALIGNEDl(3)
  LOAD_VECTOR_UNALIGNEDl(4)
  LOAD_VECTOR_UNALIGNEDl(5)
  LOAD_VECTOR_UNALIGNEDl(6)
  LOAD_VECTOR_UNALIGNEDl(7)
  LOAD_VECTOR_UNALIGNEDl(8)
  LOAD_VECTOR_UNALIGNEDl(9)
  LOAD_VECTOR_UNALIGNEDl(10)
  LOAD_VECTOR_UNALIGNEDl(11)
  LOAD_VECTOR_UNALIGNEDl(12)
  LOAD_VECTOR_UNALIGNEDl(13)
  LOAD_VECTOR_UNALIGNEDl(14)
  LOAD_VECTOR_UNALIGNEDl(15)
  LOAD_VECTOR_UNALIGNEDh(16)
  LOAD_VECTOR_UNALIGNEDh(17)
  LOAD_VECTOR_UNALIGNEDh(18)
  LOAD_VECTOR_UNALIGNEDh(19)
  LOAD_VECTOR_UNALIGNEDh(20)
  LOAD_VECTOR_UNALIGNEDh(21)
  LOAD_VECTOR_UNALIGNEDh(22)
  LOAD_VECTOR_UNALIGNEDh(23)
  LOAD_VECTOR_UNALIGNEDh(24)
  LOAD_VECTOR_UNALIGNEDh(25)
  LOAD_VECTOR_UNALIGNEDh(26)
  LOAD_VECTOR_UNALIGNEDh(27)
  LOAD_VECTOR_UNALIGNEDh(28)
  LOAD_VECTOR_UNALIGNEDh(29)
  LOAD_VECTOR_UNALIGNEDh(30)
  LOAD_VECTOR_UNALIGNEDh(31)

Lnovec:
  lwz    r0, 136(r3) ; __cr
  mtocrf  255,r0
  lwz    r0, 148(r3) ; __ctr
  mtctr  r0
  lwz    r0, 0(r3)  ; __ssr0
  mtctr  r0
  lwz    r0, 8(r3)  ; do r0 now
  lwz    r5,28(r3)  ; do r5 now
  lwz    r4,24(r3)  ; do r4 now
  lwz    r1,12(r3)  ; do sp now
  lwz    r3,20(r3)  ; do r3 last
  bctr

#elif defined(__arm64__) || defined(__aarch64__)

//
// void libunwind::Registers_arm64::jumpto()
//
// On entry:
//  thread_state pointer is in x0
//
  .p2align 2
DEFINE_LIBUNWIND_PRIVATE_FUNCTION(_ZN9libunwind15Registers_arm646jumptoEv)
  // skip restore of x0,x1 for now
  ldp    x2, x3,  [x0, #0x010]
  ldp    x4, x5,  [x0, #0x020]
  ldp    x6, x7,  [x0, #0x030]
  ldp    x8, x9,  [x0, #0x040]
  ldp    x10,x11, [x0, #0x050]
  ldp    x12,x13, [x0, #0x060]
  ldp    x14,x15, [x0, #0x070]
  ldp    x16,x17, [x0, #0x080]
  ldp    x18,x19, [x0, #0x090]
  ldp    x20,x21, [x0, #0x0A0]
  ldp    x22,x23, [x0, #0x0B0]
  ldp    x24,x25, [x0, #0x0C0]
  ldp    x26,x27, [x0, #0x0D0]
  ldp    x28,x29, [x0, #0x0E0]
  ldr    x30,     [x0, #0x100]  // restore pc into lr
  ldr    x1,      [x0, #0x0F8]
  mov    sp,x1                  // restore sp

  ldp    d0, d1,  [x0, #0x110]
  ldp    d2, d3,  [x0, #0x120]
  ldp    d4, d5,  [x0, #0x130]
  ldp    d6, d7,  [x0, #0x140]
  ldp    d8, d9,  [x0, #0x150]
  ldp    d10,d11, [x0, #0x160]
  ldp    d12,d13, [x0, #0x170]
  ldp    d14,d15, [x0, #0x180]
  ldp    d16,d17, [x0, #0x190]
  ldp    d18,d19, [x0, #0x1A0]
  ldp    d20,d21, [x0, #0x1B0]
  ldp    d22,d23, [x0, #0x1C0]
  ldp    d24,d25, [x0, #0x1D0]
  ldp    d26,d27, [x0, #0x1E0]
  ldp    d28,d29, [x0, #0x1F0]
  ldr    d30,     [x0, #0x200]
  ldr    d31,     [x0, #0x208]

  ldp    x0, x1,  [x0, #0x000]  // restore x0,x1
  ret    x30                    // jump to pc

#elif defined(__arm__) && !defined(__APPLE__)

#if !defined(__ARM_ARCH_ISA_ARM)
  .thumb
#endif

@
@ void libunwind::Registers_arm::restoreCoreAndJumpTo()
@
@ On entry:
@  thread_state pointer is in r0
@
  .p2align 2
DEFINE_LIBUNWIND_PRIVATE_FUNCTION(_ZN9libunwind13Registers_arm20restoreCoreAndJumpToEv)
#if !defined(__ARM_ARCH_ISA_ARM) && __ARM_ARCH_ISA_THUMB == 1
  @ r8-r11: ldm into r1-r4, then mov to r8-r11
  adds r0, #0x20
  ldm r0!, {r1-r4}
  subs r0, #0x30
  mov r8, r1
  mov r9, r2
  mov r10, r3
  mov r11, r4
  @ r12 does not need loading, it it the intra-procedure-call scratch register
  ldr r2, [r0, #0x34]
  ldr r3, [r0, #0x3c]
  mov sp, r2
  mov lr, r3         @ restore pc into lr
  ldm r0, {r0-r7}
#else
  @ Use lr as base so that r0 can be restored.
  mov lr, r0
  @ 32bit thumb-2 restrictions for ldm:
  @ . the sp (r13) cannot be in the list
  @ . the pc (r15) and lr (r14) cannot both be in the list in an LDM instruction
  ldm lr, {r0-r12}
  ldr sp, [lr, #52]
  ldr lr, [lr, #60]  @ restore pc into lr
#endif
  JMP(lr)

@
@ static void libunwind::Registers_arm::restoreVFPWithFLDMD(unw_fpreg_t* values)
@
@ On entry:
@  values pointer is in r0
@
  .p2align 2
#if defined(__ELF__)
  .fpu vfpv3-d16
#endif
DEFINE_LIBUNWIND_PRIVATE_FUNCTION(_ZN9libunwind13Registers_arm19restoreVFPWithFLDMDEPy)
  @ VFP and iwMMX instructions are only available when compiling with the flags
  @ that enable them. We do not want to do that in the library (because we do not
  @ want the compiler to generate instructions that access those) but this is
  @ only accessed if the personality routine needs these registers. Use of
  @ these registers implies they are, actually, available on the target, so
  @ it's ok to execute.
  @ So, generate the instruction using the corresponding coprocessor mnemonic.
  vldmia r0, {d0-d15}
  JMP(lr)

@
@ static void libunwind::Registers_arm::restoreVFPWithFLDMX(unw_fpreg_t* values)
@
@ On entry:
@  values pointer is in r0
@
  .p2align 2
#if defined(__ELF__)
  .fpu vfpv3-d16
#endif
DEFINE_LIBUNWIND_PRIVATE_FUNCTION(_ZN9libunwind13Registers_arm19restoreVFPWithFLDMXEPy)
  vldmia r0, {d0-d15} @ fldmiax is deprecated in ARMv7+ and now behaves like vldmia
  JMP(lr)

@
@ static void libunwind::Registers_arm::restoreVFPv3(unw_fpreg_t* values)
@
@ On entry:
@  values pointer is in r0
@
  .p2align 2
#if defined(__ELF__)
  .fpu vfpv3
#endif
DEFINE_LIBUNWIND_PRIVATE_FUNCTION(_ZN9libunwind13Registers_arm12restoreVFPv3EPy)
  vldmia r0, {d16-d31}
  JMP(lr)

#if defined(__ARM_WMMX)

@
@ static void libunwind::Registers_arm::restoreiWMMX(unw_fpreg_t* values)
@
@ On entry:
@  values pointer is in r0
@
  .p2align 2
#if defined(__ELF__)
  .arch armv5te
#endif
DEFINE_LIBUNWIND_PRIVATE_FUNCTION(_ZN9libunwind13Registers_arm12restoreiWMMXEPy)
  ldcl p1, cr0, [r0], #8  @ wldrd wR0, [r0], #8
  ldcl p1, cr1, [r0], #8  @ wldrd wR1, [r0], #8
  ldcl p1, cr2, [r0], #8  @ wldrd wR2, [r0], #8
  ldcl p1, cr3, [r0], #8  @ wldrd wR3, [r0], #8
  ldcl p1, cr4, [r0], #8  @ wldrd wR4, [r0], #8
  ldcl p1, cr5, [r0], #8  @ wldrd wR5, [r0], #8
  ldcl p1, cr6, [r0], #8  @ wldrd wR6, [r0], #8
  ldcl p1, cr7, [r0], #8  @ wldrd wR7, [r0], #8
  ldcl p1, cr8, [r0], #8  @ wldrd wR8, [r0], #8
  ldcl p1, cr9, [r0], #8  @ wldrd wR9, [r0], #8
  ldcl p1, cr10, [r0], #8  @ wldrd wR10, [r0], #8
  ldcl p1, cr11, [r0], #8  @ wldrd wR11, [r0], #8
  ldcl p1, cr12, [r0], #8  @ wldrd wR12, [r0], #8
  ldcl p1, cr13, [r0], #8  @ wldrd wR13, [r0], #8
  ldcl p1, cr14, [r0], #8  @ wldrd wR14, [r0], #8
  ldcl p1, cr15, [r0], #8  @ wldrd wR15, [r0], #8
  JMP(lr)

@
@ static void libunwind::Registers_arm::restoreiWMMXControl(unw_uint32_t* values)
@
@ On entry:
@  values pointer is in r0
@
  .p2align 2
#if defined(__ELF__)
  .arch armv5te
#endif
DEFINE_LIBUNWIND_PRIVATE_FUNCTION(_ZN9libunwind13Registers_arm19restoreiWMMXControlEPj)
  ldc2 p1, cr8, [r0], #4  @ wldrw wCGR0, [r0], #4
  ldc2 p1, cr9, [r0], #4  @ wldrw wCGR1, [r0], #4
  ldc2 p1, cr10, [r0], #4  @ wldrw wCGR2, [r0], #4
  ldc2 p1, cr11, [r0], #4  @ wldrw wCGR3, [r0], #4
  JMP(lr)

#endif

#elif defined(__or1k__)

DEFINE_LIBUNWIND_PRIVATE_FUNCTION(_ZN9libunwind14Registers_or1k6jumptoEv)
#
# void libunwind::Registers_or1k::jumpto()
#
# On entry:
#  thread_state pointer is in r3
#

  # restore integral registers
  l.lwz     r0,  0(r3)
  l.lwz     r1,  4(r3)
  l.lwz     r2,  8(r3)
  # skip r3 for now
  l.lwz     r4, 16(r3)
  l.lwz     r5, 20(r3)
  l.lwz     r6, 24(r3)
  l.lwz     r7, 28(r3)
  l.lwz     r8, 32(r3)
  # skip r9
  l.lwz    r10, 40(r3)
  l.lwz    r11, 44(r3)
  l.lwz    r12, 48(r3)
  l.lwz    r13, 52(r3)
  l.lwz    r14, 56(r3)
  l.lwz    r15, 60(r3)
  l.lwz    r16, 64(r3)
  l.lwz    r17, 68(r3)
  l.lwz    r18, 72(r3)
  l.lwz    r19, 76(r3)
  l.lwz    r20, 80(r3)
  l.lwz    r21, 84(r3)
  l.lwz    r22, 88(r3)
  l.lwz    r23, 92(r3)
  l.lwz    r24, 96(r3)
  l.lwz    r25,100(r3)
  l.lwz    r26,104(r3)
  l.lwz    r27,108(r3)
  l.lwz    r28,112(r3)
  l.lwz    r29,116(r3)
  l.lwz    r30,120(r3)
  l.lwz    r31,124(r3)

  # at last, restore r3
  l.lwz    r3,  12(r3)

  # load new pc into ra
  l.lwz    r9, 128(r3)
  # jump to pc
  l.jr     r9
   l.nop

#elif defined(__riscv)

//
// void libunwind::Registers_riscv::jumpto()
//
// On entry:
//  thread_state pointer is in a0
//
  .p2align 2
DEFINE_LIBUNWIND_PRIVATE_FUNCTION(_ZN9libunwind15Registers_riscv6jumptoEv)
#ifdef __riscv_float_abi_double
  fld    f0, (8 * 32 + 8 * 0)(a0)
  fld    f1, (8 * 32 + 8 * 1)(a0)
  fld    f2, (8 * 32 + 8 * 2)(a0)
  fld    f3, (8 * 32 + 8 * 3)(a0)
  fld    f4, (8 * 32 + 8 * 4)(a0)
  fld    f5, (8 * 32 + 8 * 5)(a0)
  fld    f6, (8 * 32 + 8 * 6)(a0)
  fld    f7, (8 * 32 + 8 * 7)(a0)
  fld    f8, (8 * 32 + 8 * 8)(a0)
  fld    f9, (8 * 32 + 8 * 9)(a0)
  fld    f10, (8 * 32 + 8 * 10)(a0)
  fld    f11, (8 * 32 + 8 * 11)(a0)
  fld    f12, (8 * 32 + 8 * 12)(a0)
  fld    f13, (8 * 32 + 8 * 13)(a0)
  fld    f14, (8 * 32 + 8 * 14)(a0)
  fld    f15, (8 * 32 + 8 * 15)(a0)
  fld    f16, (8 * 32 + 8 * 16)(a0)
  fld    f17, (8 * 32 + 8 * 17)(a0)
  fld    f18, (8 * 32 + 8 * 18)(a0)
  fld    f19, (8 * 32 + 8 * 19)(a0)
  fld    f20, (8 * 32 + 8 * 20)(a0)
  fld    f21, (8 * 32 + 8 * 21)(a0)
  fld    f22, (8 * 32 + 8 * 22)(a0)
  fld    f23, (8 * 32 + 8 * 23)(a0)
  fld    f24, (8 * 32 + 8 * 24)(a0)
  fld    f25, (8 * 32 + 8 * 25)(a0)
  fld    f26, (8 * 32 + 8 * 26)(a0)
  fld    f27, (8 * 32 + 8 * 27)(a0)
  fld    f28, (8 * 32 + 8 * 28)(a0)
  fld    f29, (8 * 32 + 8 * 29)(a0)
  fld    f30, (8 * 32 + 8 * 30)(a0)
  fld    f31, (8 * 32 + 8 * 31)(a0)
#endif

  // x0 is zero
  ld    x1, (8 * 1)(a0)
  ld    x2, (8 * 2)(a0)
  ld    x3, (8 * 3)(a0)
  ld    x4, (8 * 4)(a0)
  ld    x5, (8 * 5)(a0)
  ld    x6, (8 * 6)(a0)
  ld    x7, (8 * 7)(a0)
  ld    x8, (8 * 8)(a0)
  ld    x9, (8 * 9)(a0)
  // skip a0 for now
  ld    x11, (8 * 11)(a0)
  ld    x12, (8 * 12)(a0)
  ld    x13, (8 * 13)(a0)
  ld    x14, (8 * 14)(a0)
  ld    x15, (8 * 15)(a0)
  ld    x16, (8 * 16)(a0)
  ld    x17, (8 * 17)(a0)
  ld    x18, (8 * 18)(a0)
  ld    x19, (8 * 19)(a0)
  ld    x20, (8 * 20)(a0)
  ld    x21, (8 * 21)(a0)
  ld    x22, (8 * 22)(a0)
  ld    x23, (8 * 23)(a0)
  ld    x24, (8 * 24)(a0)
  ld    x25, (8 * 25)(a0)
  ld    x26, (8 * 26)(a0)
  ld    x27, (8 * 27)(a0)
  ld    x28, (8 * 28)(a0)
  ld    x29, (8 * 29)(a0)
  ld    x30, (8 * 30)(a0)
  ld    x31, (8 * 31)(a0)
  ld    x10, (8 * 10)(a0)   // restore a0

  ret                       // jump to ra

#elif defined(__mips__) && defined(_ABIO32) && _MIPS_SIM == _ABIO32

//
// void libunwind::Registers_mips_o32::jumpto()
//
// On entry:
//  thread state pointer is in a0 ($4)
//
DEFINE_LIBUNWIND_PRIVATE_FUNCTION(_ZN9libunwind18Registers_mips_o326jumptoEv)
  .set push
  .set noat
  .set noreorder
  .set nomacro
#ifdef __mips_hard_float
#if __mips_fpr != 64
  ldc1  $f0, (4 * 36 + 8 * 0)($4)
  ldc1  $f2, (4 * 36 + 8 * 2)($4)
  ldc1  $f4, (4 * 36 + 8 * 4)($4)
  ldc1  $f6, (4 * 36 + 8 * 6)($4)
  ldc1  $f8, (4 * 36 + 8 * 8)($4)
  ldc1  $f10, (4 * 36 + 8 * 10)($4)
  ldc1  $f12, (4 * 36 + 8 * 12)($4)
  ldc1  $f14, (4 * 36 + 8 * 14)($4)
  ldc1  $f16, (4 * 36 + 8 * 16)($4)
  ldc1  $f18, (4 * 36 + 8 * 18)($4)
  ldc1  $f20, (4 * 36 + 8 * 20)($4)
  ldc1  $f22, (4 * 36 + 8 * 22)($4)
  ldc1  $f24, (4 * 36 + 8 * 24)($4)
  ldc1  $f26, (4 * 36 + 8 * 26)($4)
  ldc1  $f28, (4 * 36 + 8 * 28)($4)
  ldc1  $f30, (4 * 36 + 8 * 30)($4)
#else
  ldc1  $f0, (4 * 36 + 8 * 0)($4)
  ldc1  $f1, (4 * 36 + 8 * 1)($4)
  ldc1  $f2, (4 * 36 + 8 * 2)($4)
  ldc1  $f3, (4 * 36 + 8 * 3)($4)
  ldc1  $f4, (4 * 36 + 8 * 4)($4)
  ldc1  $f5, (4 * 36 + 8 * 5)($4)
  ldc1  $f6, (4 * 36 + 8 * 6)($4)
  ldc1  $f7, (4 * 36 + 8 * 7)($4)
  ldc1  $f8, (4 * 36 + 8 * 8)($4)
  ldc1  $f9, (4 * 36 + 8 * 9)($4)
  ldc1  $f10, (4 * 36 + 8 * 10)($4)
  ldc1  $f11, (4 * 36 + 8 * 11)($4)
  ldc1  $f12, (4 * 36 + 8 * 12)($4)
  ldc1  $f13, (4 * 36 + 8 * 13)($4)
  ldc1  $f14, (4 * 36 + 8 * 14)($4)
  ldc1  $f15, (4 * 36 + 8 * 15)($4)
  ldc1  $f16, (4 * 36 + 8 * 16)($4)
  ldc1  $f17, (4 * 36 + 8 * 17)($4)
  ldc1  $f18, (4 * 36 + 8 * 18)($4)
  ldc1  $f19, (4 * 36 + 8 * 19)($4)
  ldc1  $f20, (4 * 36 + 8 * 20)($4)
  ldc1  $f21, (4 * 36 + 8 * 21)($4)
  ldc1  $f22, (4 * 36 + 8 * 22)($4)
  ldc1  $f23, (4 * 36 + 8 * 23)($4)
  ldc1  $f24, (4 * 36 + 8 * 24)($4)
  ldc1  $f25, (4 * 36 + 8 * 25)($4)
  ldc1  $f26, (4 * 36 + 8 * 26)($4)
  ldc1  $f27, (4 * 36 + 8 * 27)($4)
  ldc1  $f28, (4 * 36 + 8 * 28)($4)
  ldc1  $f29, (4 * 36 + 8 * 29)($4)
  ldc1  $f30, (4 * 36 + 8 * 30)($4)
  ldc1  $f31, (4 * 36 + 8 * 31)($4)
#endif
#endif
  // restore hi and lo
  lw    $8, (4 * 33)($4)
  mthi  $8
  lw    $8, (4 * 34)($4)
  mtlo  $8
  // r0 is zero
  lw    $1, (4 * 1)($4)
  lw    $2, (4 * 2)($4)
  lw    $3, (4 * 3)($4)
  // skip a0 for now
  lw    $5, (4 * 5)($4)
  lw    $6, (4 * 6)($4)
  lw    $7, (4 * 7)($4)
  lw    $8, (4 * 8)($4)
  lw    $9, (4 * 9)($4)
  lw    $10, (4 * 10)($4)
  lw    $11, (4 * 11)($4)
  lw    $12, (4 * 12)($4)
  lw    $13, (4 * 13)($4)
  lw    $14, (4 * 14)($4)
  lw    $15, (4 * 15)($4)
  lw    $16, (4 * 16)($4)
  lw    $17, (4 * 17)($4)
  lw    $18, (4 * 18)($4)
  lw    $19, (4 * 19)($4)
  lw    $20, (4 * 20)($4)
  lw    $21, (4 * 21)($4)
  lw    $22, (4 * 22)($4)
  lw    $23, (4 * 23)($4)
  lw    $24, (4 * 24)($4)
  lw    $25, (4 * 25)($4)
  lw    $26, (4 * 26)($4)
  lw    $27, (4 * 27)($4)
  lw    $28, (4 * 28)($4)
  lw    $29, (4 * 29)($4)
  lw    $30, (4 * 30)($4)
  // load new pc into ra
  lw    $31, (4 * 32)($4)
  // jump to ra, load a0 in the delay slot
  jr    $31
  lw    $4, (4 * 4)($4)
  .set pop

#elif defined(__mips64)

//
// void libunwind::Registers_mips_newabi::jumpto()
//
// On entry:
//  thread state pointer is in a0 ($4)
//
DEFINE_LIBUNWIND_PRIVATE_FUNCTION(_ZN9libunwind21Registers_mips_newabi6jumptoEv)
  .set push
  .set noat
  .set noreorder
  .set nomacro
#ifdef __mips_hard_float
  ldc1  $f0, (8 * 35)($4)
  ldc1  $f1, (8 * 36)($4)
  ldc1  $f2, (8 * 37)($4)
  ldc1  $f3, (8 * 38)($4)
  ldc1  $f4, (8 * 39)($4)
  ldc1  $f5, (8 * 40)($4)
  ldc1  $f6, (8 * 41)($4)
  ldc1  $f7, (8 * 42)($4)
  ldc1  $f8, (8 * 43)($4)
  ldc1  $f9, (8 * 44)($4)
  ldc1  $f10, (8 * 45)($4)
  ldc1  $f11, (8 * 46)($4)
  ldc1  $f12, (8 * 47)($4)
  ldc1  $f13, (8 * 48)($4)
  ldc1  $f14, (8 * 49)($4)
  ldc1  $f15, (8 * 50)($4)
  ldc1  $f16, (8 * 51)($4)
  ldc1  $f17, (8 * 52)($4)
  ldc1  $f18, (8 * 53)($4)
  ldc1  $f19, (8 * 54)($4)
  ldc1  $f20, (8 * 55)($4)
  ldc1  $f21, (8 * 56)($4)
  ldc1  $f22, (8 * 57)($4)
  ldc1  $f23, (8 * 58)($4)
  ldc1  $f24, (8 * 59)($4)
  ldc1  $f25, (8 * 60)($4)
  ldc1  $f26, (8 * 61)($4)
  ldc1  $f27, (8 * 62)($4)
  ldc1  $f28, (8 * 63)($4)
  ldc1  $f29, (8 * 64)($4)
  ldc1  $f30, (8 * 65)($4)
  ldc1  $f31, (8 * 66)($4)
#endif
  // restore hi and lo
  ld    $8, (8 * 33)($4)
  mthi  $8
  ld    $8, (8 * 34)($4)
  mtlo  $8
  // r0 is zero
  ld    $1, (8 * 1)($4)
  ld    $2, (8 * 2)($4)
  ld    $3, (8 * 3)($4)
  // skip a0 for now
  ld    $5, (8 * 5)($4)
  ld    $6, (8 * 6)($4)
  ld    $7, (8 * 7)($4)
  ld    $8, (8 * 8)($4)
  ld    $9, (8 * 9)($4)
  ld    $10, (8 * 10)($4)
  ld    $11, (8 * 11)($4)
  ld    $12, (8 * 12)($4)
  ld    $13, (8 * 13)($4)
  ld    $14, (8 * 14)($4)
  ld    $15, (8 * 15)($4)
  ld    $16, (8 * 16)($4)
  ld    $17, (8 * 17)($4)
  ld    $18, (8 * 18)($4)
  ld    $19, (8 * 19)($4)
  ld    $20, (8 * 20)($4)
  ld    $21, (8 * 21)($4)
  ld    $22, (8 * 22)($4)
  ld    $23, (8 * 23)($4)
  ld    $24, (8 * 24)($4)
  ld    $25, (8 * 25)($4)
  ld    $26, (8 * 26)($4)
  ld    $27, (8 * 27)($4)
  ld    $28, (8 * 28)($4)
  ld    $29, (8 * 29)($4)
  ld    $30, (8 * 30)($4)
  // load new pc into ra
  ld    $31, (8 * 32)($4)
  // jump to ra, load a0 in the delay slot
  jr    $31
  ld    $4, (8 * 4)($4)
  .set pop

#elif defined(__sparc__)

//
// void libunwind::Registers_sparc_o32::jumpto()
//
// On entry:
//  thread_state pointer is in o0
//
DEFINE_LIBUNWIND_PRIVATE_FUNCTION(_ZN9libunwind15Registers_sparc6jumptoEv)
  ta 3
  ldd [%o0 + 64],  %l0
  ldd [%o0 + 72],  %l2
  ldd [%o0 + 80],  %l4
  ldd [%o0 + 88],  %l6
  ldd [%o0 + 96],  %i0
  ldd [%o0 + 104], %i2
  ldd [%o0 + 112], %i4
  ldd [%o0 + 120], %i6
  ld  [%o0 + 60],  %o7
  jmp %o7
   nop

#endif

#endif /* !defined(__USING_SJLJ_EXCEPTIONS__) */

NO_EXEC_STACK_DIRECTIVE

