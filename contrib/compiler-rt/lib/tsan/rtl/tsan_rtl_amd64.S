// The content of this file is x86_64-only:
#if defined(__x86_64__)

#include "sanitizer_common/sanitizer_asm.h"

#if !defined(__APPLE__)
.section .text
#else
.section __TEXT,__text
#endif

ASM_HIDDEN(__tsan_trace_switch)
.globl ASM_SYMBOL(__tsan_trace_switch_thunk)
ASM_SYMBOL(__tsan_trace_switch_thunk):
  CFI_STARTPROC
  # Save scratch registers.
  push %rax
  CFI_ADJUST_CFA_OFFSET(8)
  CFI_REL_OFFSET(%rax, 0)
  push %rcx
  CFI_ADJUST_CFA_OFFSET(8)
  CFI_REL_OFFSET(%rcx, 0)
  push %rdx
  CFI_ADJUST_CFA_OFFSET(8)
  CFI_REL_OFFSET(%rdx, 0)
  push %rsi
  CFI_ADJUST_CFA_OFFSET(8)
  CFI_REL_OFFSET(%rsi, 0)
  push %rdi
  CFI_ADJUST_CFA_OFFSET(8)
  CFI_REL_OFFSET(%rdi, 0)
  push %r8
  CFI_ADJUST_CFA_OFFSET(8)
  CFI_REL_OFFSET(%r8, 0)
  push %r9
  CFI_ADJUST_CFA_OFFSET(8)
  CFI_REL_OFFSET(%r9, 0)
  push %r10
  CFI_ADJUST_CFA_OFFSET(8)
  CFI_REL_OFFSET(%r10, 0)
  push %r11
  CFI_ADJUST_CFA_OFFSET(8)
  CFI_REL_OFFSET(%r11, 0)
  # Align stack frame.
  push %rbx  # non-scratch
  CFI_ADJUST_CFA_OFFSET(8)
  CFI_REL_OFFSET(%rbx, 0)
  mov %rsp, %rbx  # save current rsp
  CFI_DEF_CFA_REGISTER(%rbx)
  shr $4, %rsp  # clear 4 lsb, align to 16
  shl $4, %rsp

  call ASM_SYMBOL(__tsan_trace_switch)

  # Unalign stack frame back.
  mov %rbx, %rsp  # restore the original rsp
  CFI_DEF_CFA_REGISTER(%rsp)
  pop %rbx
  CFI_ADJUST_CFA_OFFSET(-8)
  # Restore scratch registers.
  pop %r11
  CFI_ADJUST_CFA_OFFSET(-8)
  pop %r10
  CFI_ADJUST_CFA_OFFSET(-8)
  pop %r9
  CFI_ADJUST_CFA_OFFSET(-8)
  pop %r8
  CFI_ADJUST_CFA_OFFSET(-8)
  pop %rdi
  CFI_ADJUST_CFA_OFFSET(-8)
  pop %rsi
  CFI_ADJUST_CFA_OFFSET(-8)
  pop %rdx
  CFI_ADJUST_CFA_OFFSET(-8)
  pop %rcx
  CFI_ADJUST_CFA_OFFSET(-8)
  pop %rax
  CFI_ADJUST_CFA_OFFSET(-8)
  CFI_RESTORE(%rax)
  CFI_RESTORE(%rbx)
  CFI_RESTORE(%rcx)
  CFI_RESTORE(%rdx)
  CFI_RESTORE(%rsi)
  CFI_RESTORE(%rdi)
  CFI_RESTORE(%r8)
  CFI_RESTORE(%r9)
  CFI_RESTORE(%r10)
  CFI_RESTORE(%r11)
  ret
  CFI_ENDPROC

ASM_HIDDEN(__tsan_report_race)
.globl ASM_SYMBOL(__tsan_report_race_thunk)
ASM_SYMBOL(__tsan_report_race_thunk):
  CFI_STARTPROC
  # Save scratch registers.
  push %rax
  CFI_ADJUST_CFA_OFFSET(8)
  CFI_REL_OFFSET(%rax, 0)
  push %rcx
  CFI_ADJUST_CFA_OFFSET(8)
  CFI_REL_OFFSET(%rcx, 0)
  push %rdx
  CFI_ADJUST_CFA_OFFSET(8)
  CFI_REL_OFFSET(%rdx, 0)
  push %rsi
  CFI_ADJUST_CFA_OFFSET(8)
  CFI_REL_OFFSET(%rsi, 0)
  push %rdi
  CFI_ADJUST_CFA_OFFSET(8)
  CFI_REL_OFFSET(%rdi, 0)
  push %r8
  CFI_ADJUST_CFA_OFFSET(8)
  CFI_REL_OFFSET(%r8, 0)
  push %r9
  CFI_ADJUST_CFA_OFFSET(8)
  CFI_REL_OFFSET(%r9, 0)
  push %r10
  CFI_ADJUST_CFA_OFFSET(8)
  CFI_REL_OFFSET(%r10, 0)
  push %r11
  CFI_ADJUST_CFA_OFFSET(8)
  CFI_REL_OFFSET(%r11, 0)
  # Align stack frame.
  push %rbx  # non-scratch
  CFI_ADJUST_CFA_OFFSET(8)
  CFI_REL_OFFSET(%rbx, 0)
  mov %rsp, %rbx  # save current rsp
  CFI_DEF_CFA_REGISTER(%rbx)
  shr $4, %rsp  # clear 4 lsb, align to 16
  shl $4, %rsp

  call ASM_SYMBOL(__tsan_report_race)

  # Unalign stack frame back.
  mov %rbx, %rsp  # restore the original rsp
  CFI_DEF_CFA_REGISTER(%rsp)
  pop %rbx
  CFI_ADJUST_CFA_OFFSET(-8)
  # Restore scratch registers.
  pop %r11
  CFI_ADJUST_CFA_OFFSET(-8)
  pop %r10
  CFI_ADJUST_CFA_OFFSET(-8)
  pop %r9
  CFI_ADJUST_CFA_OFFSET(-8)
  pop %r8
  CFI_ADJUST_CFA_OFFSET(-8)
  pop %rdi
  CFI_ADJUST_CFA_OFFSET(-8)
  pop %rsi
  CFI_ADJUST_CFA_OFFSET(-8)
  pop %rdx
  CFI_ADJUST_CFA_OFFSET(-8)
  pop %rcx
  CFI_ADJUST_CFA_OFFSET(-8)
  pop %rax
  CFI_ADJUST_CFA_OFFSET(-8)
  CFI_RESTORE(%rax)
  CFI_RESTORE(%rbx)
  CFI_RESTORE(%rcx)
  CFI_RESTORE(%rdx)
  CFI_RESTORE(%rsi)
  CFI_RESTORE(%rdi)
  CFI_RESTORE(%r8)
  CFI_RESTORE(%r9)
  CFI_RESTORE(%r10)
  CFI_RESTORE(%r11)
  ret
  CFI_ENDPROC

ASM_HIDDEN(__tsan_setjmp)
#if defined(__NetBSD__)
.comm _ZN14__interception15real___setjmp14E,8,8
#elif !defined(__APPLE__)
.comm _ZN14__interception11real_setjmpE,8,8
#endif
#if defined(__NetBSD__)
.globl ASM_SYMBOL_INTERCEPTOR(__setjmp14)
ASM_TYPE_FUNCTION(ASM_SYMBOL_INTERCEPTOR(__setjmp14))
ASM_SYMBOL_INTERCEPTOR(__setjmp14):
#else
.globl ASM_SYMBOL_INTERCEPTOR(setjmp)
ASM_TYPE_FUNCTION(ASM_SYMBOL_INTERCEPTOR(setjmp))
ASM_SYMBOL_INTERCEPTOR(setjmp):
#endif
  CFI_STARTPROC
  // save env parameter
  push %rdi
  CFI_ADJUST_CFA_OFFSET(8)
  CFI_REL_OFFSET(%rdi, 0)
  // obtain %rsp
#if defined(__FreeBSD__) || defined(__NetBSD__)
  lea 8(%rsp), %rdi
  mov %rdi, %rsi
#elif defined(__APPLE__)
  lea 16(%rsp), %rdi
  mov %rdi, %rsi
  xorq ___tsan_darwin_setjmp_xor_key(%rip), %rsi
#elif defined(__linux__)
  lea 16(%rsp), %rdi
  mov %rdi, %rsi
  xor %fs:0x30, %rsi  // magic mangling of rsp (see libc setjmp)
  rol $0x11, %rsi
#else
# error "Unknown platform"
#endif
  // call tsan interceptor
  call ASM_SYMBOL(__tsan_setjmp)
  // restore env parameter
  pop %rdi
  CFI_ADJUST_CFA_OFFSET(-8)
  CFI_RESTORE(%rdi)
  // tail jump to libc setjmp
  movl $0, %eax
#if defined(__NetBSD__)
  movq _ZN14__interception15real___setjmp14E@GOTPCREL(%rip), %rdx
  jmp *(%rdx)
#elif !defined(__APPLE__)
  movq _ZN14__interception11real_setjmpE@GOTPCREL(%rip), %rdx
  jmp *(%rdx)
#else
  jmp ASM_SYMBOL(setjmp)
#endif
  CFI_ENDPROC
#if defined(__NetBSD__)
ASM_SIZE(ASM_SYMBOL_INTERCEPTOR(__setjmp14))
#else
ASM_SIZE(ASM_SYMBOL_INTERCEPTOR(setjmp))
#endif

.comm _ZN14__interception12real__setjmpE,8,8
.globl ASM_SYMBOL_INTERCEPTOR(_setjmp)
ASM_TYPE_FUNCTION(ASM_SYMBOL_INTERCEPTOR(_setjmp))
ASM_SYMBOL_INTERCEPTOR(_setjmp):
  CFI_STARTPROC
  // save env parameter
  push %rdi
  CFI_ADJUST_CFA_OFFSET(8)
  CFI_REL_OFFSET(%rdi, 0)
  // obtain %rsp
#if defined(__FreeBSD__) || defined(__NetBSD__)
  lea 8(%rsp), %rdi
  mov %rdi, %rsi
#elif defined(__APPLE__)
  lea 16(%rsp), %rdi
  mov %rdi, %rsi
  xorq ___tsan_darwin_setjmp_xor_key(%rip), %rsi
#elif defined(__linux__)
  lea 16(%rsp), %rdi
  mov %rdi, %rsi
  xor %fs:0x30, %rsi  // magic mangling of rsp (see libc setjmp)
  rol $0x11, %rsi
#else
# error "Unknown platform"
#endif
  // call tsan interceptor
  call ASM_SYMBOL(__tsan_setjmp)
  // restore env parameter
  pop %rdi
  CFI_ADJUST_CFA_OFFSET(-8)
  CFI_RESTORE(%rdi)
  // tail jump to libc setjmp
  movl $0, %eax
#if !defined(__APPLE__)
  movq _ZN14__interception12real__setjmpE@GOTPCREL(%rip), %rdx
  jmp *(%rdx)
#else
  jmp ASM_SYMBOL(_setjmp)
#endif
  CFI_ENDPROC
ASM_SIZE(ASM_SYMBOL_INTERCEPTOR(_setjmp))

#if defined(__NetBSD__)
.comm _ZN14__interception18real___sigsetjmp14E,8,8
.globl ASM_SYMBOL_INTERCEPTOR(__sigsetjmp14)
ASM_TYPE_FUNCTION(ASM_SYMBOL_INTERCEPTOR(__sigsetjmp14))
ASM_SYMBOL_INTERCEPTOR(__sigsetjmp14):
#else
.comm _ZN14__interception14real_sigsetjmpE,8,8
.globl ASM_SYMBOL_INTERCEPTOR(sigsetjmp)
ASM_TYPE_FUNCTION(ASM_SYMBOL_INTERCEPTOR(sigsetjmp))
ASM_SYMBOL_INTERCEPTOR(sigsetjmp):
#endif
  CFI_STARTPROC
  // save env parameter
  push %rdi
  CFI_ADJUST_CFA_OFFSET(8)
  CFI_REL_OFFSET(%rdi, 0)
  // save savesigs parameter
  push %rsi
  CFI_ADJUST_CFA_OFFSET(8)
  CFI_REL_OFFSET(%rsi, 0)
  // align stack frame
  sub $8, %rsp
  CFI_ADJUST_CFA_OFFSET(8)
  // obtain %rsp
#if defined(__FreeBSD__) || defined(__NetBSD__)
  lea 24(%rsp), %rdi
  mov %rdi, %rsi
#elif defined(__APPLE__)
  lea 32(%rsp), %rdi
  mov %rdi, %rsi
  xorq ___tsan_darwin_setjmp_xor_key(%rip), %rsi
#elif defined(__linux__)
  lea 32(%rsp), %rdi
  mov %rdi, %rsi
  xor %fs:0x30, %rsi  // magic mangling of rsp (see libc setjmp)
  rol $0x11, %rsi
#else
# error "Unknown platform"
#endif
  // call tsan interceptor
  call ASM_SYMBOL(__tsan_setjmp)
  // unalign stack frame
  add $8, %rsp
  CFI_ADJUST_CFA_OFFSET(-8)
  // restore savesigs parameter
  pop %rsi
  CFI_ADJUST_CFA_OFFSET(-8)
  CFI_RESTORE(%rsi)
  // restore env parameter
  pop %rdi
  CFI_ADJUST_CFA_OFFSET(-8)
  CFI_RESTORE(%rdi)
  // tail jump to libc sigsetjmp
  movl $0, %eax
#if defined(__NetBSD__)
  movq _ZN14__interception18real___sigsetjmp14E@GOTPCREL(%rip), %rdx
  jmp *(%rdx)
#elif !defined(__APPLE__)
  movq _ZN14__interception14real_sigsetjmpE@GOTPCREL(%rip), %rdx
  jmp *(%rdx)
#else
  jmp ASM_SYMBOL(sigsetjmp)
#endif
  CFI_ENDPROC
#if defined(__NetBSD__)
ASM_SIZE(ASM_SYMBOL_INTERCEPTOR(__sigsetjmp14))
#else
ASM_SIZE(ASM_SYMBOL_INTERCEPTOR(sigsetjmp))
#endif

#if !defined(__APPLE__) && !defined(__NetBSD__)
.comm _ZN14__interception16real___sigsetjmpE,8,8
.globl ASM_SYMBOL_INTERCEPTOR(__sigsetjmp)
ASM_TYPE_FUNCTION(ASM_SYMBOL_INTERCEPTOR(__sigsetjmp))
ASM_SYMBOL_INTERCEPTOR(__sigsetjmp):
  CFI_STARTPROC
  // save env parameter
  push %rdi
  CFI_ADJUST_CFA_OFFSET(8)
  CFI_REL_OFFSET(%rdi, 0)
  // save savesigs parameter
  push %rsi
  CFI_ADJUST_CFA_OFFSET(8)
  CFI_REL_OFFSET(%rsi, 0)
  // align stack frame
  sub $8, %rsp
  CFI_ADJUST_CFA_OFFSET(8)
  // obtain %rsp
#if defined(__FreeBSD__)
  lea 24(%rsp), %rdi
  mov %rdi, %rsi
#else
  lea 32(%rsp), %rdi
  mov %rdi, %rsi
  xor %fs:0x30, %rsi  // magic mangling of rsp (see libc setjmp)
  rol $0x11, %rsi
#endif
  // call tsan interceptor
  call ASM_SYMBOL(__tsan_setjmp)
  // unalign stack frame
  add $8, %rsp
  CFI_ADJUST_CFA_OFFSET(-8)
  // restore savesigs parameter
  pop %rsi
  CFI_ADJUST_CFA_OFFSET(-8)
  CFI_RESTORE(%rsi)
  // restore env parameter
  pop %rdi
  CFI_ADJUST_CFA_OFFSET(-8)
  CFI_RESTORE(%rdi)
  // tail jump to libc sigsetjmp
  movl $0, %eax
  movq _ZN14__interception16real___sigsetjmpE@GOTPCREL(%rip), %rdx
  jmp *(%rdx)
  CFI_ENDPROC
ASM_SIZE(ASM_SYMBOL_INTERCEPTOR(__sigsetjmp))
#endif  // !defined(__APPLE__) && !defined(__NetBSD__)

#if defined(__FreeBSD__) || defined(__linux__)
/* We do not need executable stack.  */
/* This note is not needed on NetBSD. */
.section        .note.GNU-stack,"",@progbits
#endif

#endif
