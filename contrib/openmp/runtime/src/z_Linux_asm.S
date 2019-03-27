//  z_Linux_asm.S:  - microtasking routines specifically
//                    written for Intel platforms running Linux* OS

//
////===----------------------------------------------------------------------===//
////
////                     The LLVM Compiler Infrastructure
////
//// This file is dual licensed under the MIT and the University of Illinois Open
//// Source Licenses. See LICENSE.txt for details.
////
////===----------------------------------------------------------------------===//
//

// -----------------------------------------------------------------------
// macros
// -----------------------------------------------------------------------

#include "kmp_config.h"

#if KMP_ARCH_X86 || KMP_ARCH_X86_64

# if KMP_MIC
// the 'delay r16/r32/r64' should be used instead of the 'pause'.
// The delay operation has the effect of removing the current thread from
// the round-robin HT mechanism, and therefore speeds up the issue rate of
// the other threads on the same core.
//
// A value of 0 works fine for <= 2 threads per core, but causes the EPCC
// barrier time to increase greatly for 3 or more threads per core.
//
// A value of 100 works pretty well for up to 4 threads per core, but isn't
// quite as fast as 0 for 2 threads per core.
//
// We need to check what happens for oversubscription / > 4 threads per core.
// It is possible that we need to pass the delay value in as a parameter
// that the caller determines based on the total # threads / # cores.
//
//.macro pause_op
//	mov    $100, %rax
//	delay  %rax
//.endm
# else
#  define pause_op   .byte 0xf3,0x90
# endif // KMP_MIC

# if KMP_OS_DARWIN
#  define KMP_PREFIX_UNDERSCORE(x) _##x  // extra underscore for OS X* symbols
#  define KMP_LABEL(x) L_##x             // form the name of label
.macro KMP_CFI_DEF_OFFSET
.endmacro
.macro KMP_CFI_OFFSET
.endmacro
.macro KMP_CFI_REGISTER
.endmacro
.macro KMP_CFI_DEF
.endmacro
.macro ALIGN
	.align $0
.endmacro
.macro DEBUG_INFO
/* Not sure what .size does in icc, not sure if we need to do something
   similar for OS X*.
*/
.endmacro
.macro PROC
	ALIGN  4
	.globl KMP_PREFIX_UNDERSCORE($0)
KMP_PREFIX_UNDERSCORE($0):
.endmacro
# else // KMP_OS_DARWIN
#  define KMP_PREFIX_UNDERSCORE(x) x //no extra underscore for Linux* OS symbols
// Format labels so that they don't override function names in gdb's backtraces
// MIC assembler doesn't accept .L syntax, the L works fine there (as well as
// on OS X*)
# if KMP_MIC
#  define KMP_LABEL(x) L_##x          // local label
# else
#  define KMP_LABEL(x) .L_##x         // local label hidden from backtraces
# endif // KMP_MIC
.macro ALIGN size
	.align 1<<(\size)
.endm
.macro DEBUG_INFO proc
	.cfi_endproc
// Not sure why we need .type and .size for the functions
	.align 16
	.type  \proc,@function
        .size  \proc,.-\proc
.endm
.macro PROC proc
	ALIGN  4
        .globl KMP_PREFIX_UNDERSCORE(\proc)
KMP_PREFIX_UNDERSCORE(\proc):
	.cfi_startproc
.endm
.macro KMP_CFI_DEF_OFFSET sz
	.cfi_def_cfa_offset	\sz
.endm
.macro KMP_CFI_OFFSET reg, sz
	.cfi_offset	\reg,\sz
.endm
.macro KMP_CFI_REGISTER reg
	.cfi_def_cfa_register	\reg
.endm
.macro KMP_CFI_DEF reg, sz
	.cfi_def_cfa	\reg,\sz
.endm
# endif // KMP_OS_DARWIN
#endif // KMP_ARCH_X86 || KMP_ARCH_x86_64

#if (KMP_OS_LINUX || KMP_OS_DARWIN) && KMP_ARCH_AARCH64

# if KMP_OS_DARWIN
#  define KMP_PREFIX_UNDERSCORE(x) _##x  // extra underscore for OS X* symbols
#  define KMP_LABEL(x) L_##x             // form the name of label

.macro ALIGN
	.align $0
.endmacro

.macro DEBUG_INFO
/* Not sure what .size does in icc, not sure if we need to do something
   similar for OS X*.
*/
.endmacro

.macro PROC
	ALIGN  4
	.globl KMP_PREFIX_UNDERSCORE($0)
KMP_PREFIX_UNDERSCORE($0):
.endmacro
# else // KMP_OS_DARWIN
#  define KMP_PREFIX_UNDERSCORE(x) x  // no extra underscore for Linux* OS symbols
// Format labels so that they don't override function names in gdb's backtraces
#  define KMP_LABEL(x) .L_##x         // local label hidden from backtraces

.macro ALIGN size
	.align 1<<(\size)
.endm

.macro DEBUG_INFO proc
	.cfi_endproc
// Not sure why we need .type and .size for the functions
	ALIGN 2
	.type  \proc,@function
	.size  \proc,.-\proc
.endm

.macro PROC proc
	ALIGN 2
	.globl KMP_PREFIX_UNDERSCORE(\proc)
KMP_PREFIX_UNDERSCORE(\proc):
	.cfi_startproc
.endm
# endif // KMP_OS_DARWIN

#endif // (KMP_OS_LINUX || KMP_OS_DARWIN) && KMP_ARCH_AARCH64

// -----------------------------------------------------------------------
// data
// -----------------------------------------------------------------------

#ifdef KMP_GOMP_COMPAT

// Support for unnamed common blocks.
//
// Because the symbol ".gomp_critical_user_" contains a ".", we have to
// put this stuff in assembly.

# if KMP_ARCH_X86
#  if KMP_OS_DARWIN
        .data
        .comm .gomp_critical_user_,32
        .data
        .globl ___kmp_unnamed_critical_addr
___kmp_unnamed_critical_addr:
        .long .gomp_critical_user_
#  else /* Linux* OS */
        .data
        .comm .gomp_critical_user_,32,8
        .data
	ALIGN 4
        .global __kmp_unnamed_critical_addr
__kmp_unnamed_critical_addr:
        .4byte .gomp_critical_user_
        .type __kmp_unnamed_critical_addr,@object
        .size __kmp_unnamed_critical_addr,4
#  endif /* KMP_OS_DARWIN */
# endif /* KMP_ARCH_X86 */

# if KMP_ARCH_X86_64
#  if KMP_OS_DARWIN
        .data
        .comm .gomp_critical_user_,32
        .data
        .globl ___kmp_unnamed_critical_addr
___kmp_unnamed_critical_addr:
        .quad .gomp_critical_user_
#  else /* Linux* OS */
        .data
        .comm .gomp_critical_user_,32,8
        .data
	ALIGN 8
        .global __kmp_unnamed_critical_addr
__kmp_unnamed_critical_addr:
        .8byte .gomp_critical_user_
        .type __kmp_unnamed_critical_addr,@object
        .size __kmp_unnamed_critical_addr,8
#  endif /* KMP_OS_DARWIN */
# endif /* KMP_ARCH_X86_64 */

#endif /* KMP_GOMP_COMPAT */


#if KMP_ARCH_X86 && !KMP_ARCH_PPC64

// -----------------------------------------------------------------------
// microtasking routines specifically written for IA-32 architecture
// running Linux* OS
// -----------------------------------------------------------------------

	.ident "Intel Corporation"
	.data
	ALIGN 4
// void
// __kmp_x86_pause( void );

        .text
	PROC  __kmp_x86_pause

        pause_op
        ret

	DEBUG_INFO __kmp_x86_pause

// void
// __kmp_x86_cpuid( int mode, int mode2, void *cpuid_buffer );

	PROC  __kmp_x86_cpuid

	pushl %ebp
	movl  %esp,%ebp
        pushl %edi
        pushl %ebx
        pushl %ecx
        pushl %edx

	movl  8(%ebp), %eax
	movl  12(%ebp), %ecx
	cpuid		// Query the CPUID for the current processor

	movl  16(%ebp), %edi
	movl  %eax, 0(%edi)
	movl  %ebx, 4(%edi)
	movl  %ecx, 8(%edi)
	movl  %edx, 12(%edi)

        popl  %edx
        popl  %ecx
        popl  %ebx
        popl  %edi
        movl  %ebp, %esp
        popl  %ebp
	ret

	DEBUG_INFO __kmp_x86_cpuid


# if !KMP_ASM_INTRINS

//------------------------------------------------------------------------
// kmp_int32
// __kmp_test_then_add32( volatile kmp_int32 *p, kmp_int32 d );

        PROC      __kmp_test_then_add32

        movl      4(%esp), %ecx
        movl      8(%esp), %eax
        lock
        xaddl     %eax,(%ecx)
        ret

	DEBUG_INFO __kmp_test_then_add32

//------------------------------------------------------------------------
// FUNCTION __kmp_xchg_fixed8
//
// kmp_int32
// __kmp_xchg_fixed8( volatile kmp_int8 *p, kmp_int8 d );
//
// parameters:
// 	p:	4(%esp)
// 	d:	8(%esp)
//
// return:	%al
        PROC  __kmp_xchg_fixed8

        movl      4(%esp), %ecx    // "p"
        movb      8(%esp), %al	// "d"

        lock
        xchgb     %al,(%ecx)
        ret

        DEBUG_INFO __kmp_xchg_fixed8


//------------------------------------------------------------------------
// FUNCTION __kmp_xchg_fixed16
//
// kmp_int16
// __kmp_xchg_fixed16( volatile kmp_int16 *p, kmp_int16 d );
//
// parameters:
// 	p:	4(%esp)
// 	d:	8(%esp)
// return:     %ax
        PROC  __kmp_xchg_fixed16

        movl      4(%esp), %ecx    // "p"
        movw      8(%esp), %ax	// "d"

        lock
        xchgw     %ax,(%ecx)
        ret

        DEBUG_INFO __kmp_xchg_fixed16


//------------------------------------------------------------------------
// FUNCTION __kmp_xchg_fixed32
//
// kmp_int32
// __kmp_xchg_fixed32( volatile kmp_int32 *p, kmp_int32 d );
//
// parameters:
// 	p:	4(%esp)
// 	d:	8(%esp)
//
// return:	%eax
        PROC  __kmp_xchg_fixed32

        movl      4(%esp), %ecx    // "p"
        movl      8(%esp), %eax	// "d"

        lock
        xchgl     %eax,(%ecx)
        ret

        DEBUG_INFO __kmp_xchg_fixed32


// kmp_int8
// __kmp_compare_and_store8( volatile kmp_int8 *p, kmp_int8 cv, kmp_int8 sv );
        PROC  __kmp_compare_and_store8

        movl      4(%esp), %ecx
        movb      8(%esp), %al
        movb      12(%esp), %dl
        lock
        cmpxchgb  %dl,(%ecx)
        sete      %al           // if %al == (%ecx) set %al = 1 else set %al = 0
        and       $1, %eax      // sign extend previous instruction
        ret

        DEBUG_INFO __kmp_compare_and_store8

// kmp_int16
// __kmp_compare_and_store16(volatile kmp_int16 *p, kmp_int16 cv, kmp_int16 sv);
        PROC  __kmp_compare_and_store16

        movl      4(%esp), %ecx
        movw      8(%esp), %ax
        movw      12(%esp), %dx
        lock
        cmpxchgw  %dx,(%ecx)
        sete      %al           // if %ax == (%ecx) set %al = 1 else set %al = 0
        and       $1, %eax      // sign extend previous instruction
        ret

        DEBUG_INFO __kmp_compare_and_store16

// kmp_int32
// __kmp_compare_and_store32(volatile kmp_int32 *p, kmp_int32 cv, kmp_int32 sv);
        PROC  __kmp_compare_and_store32

        movl      4(%esp), %ecx
        movl      8(%esp), %eax
        movl      12(%esp), %edx
        lock
        cmpxchgl  %edx,(%ecx)
        sete      %al          // if %eax == (%ecx) set %al = 1 else set %al = 0
        and       $1, %eax     // sign extend previous instruction
        ret

        DEBUG_INFO __kmp_compare_and_store32

// kmp_int32
// __kmp_compare_and_store64(volatile kmp_int64 *p, kmp_int64 cv, kmp_int64 s );
        PROC  __kmp_compare_and_store64

        pushl     %ebp
        movl      %esp, %ebp
        pushl     %ebx
        pushl     %edi
        movl      8(%ebp), %edi
        movl      12(%ebp), %eax        // "cv" low order word
        movl      16(%ebp), %edx        // "cv" high order word
        movl      20(%ebp), %ebx        // "sv" low order word
        movl      24(%ebp), %ecx        // "sv" high order word
        lock
        cmpxchg8b (%edi)
        sete      %al      // if %edx:eax == (%edi) set %al = 1 else set %al = 0
        and       $1, %eax // sign extend previous instruction
        popl      %edi
        popl      %ebx
        movl      %ebp, %esp
        popl      %ebp
        ret

        DEBUG_INFO __kmp_compare_and_store64

// kmp_int8
// __kmp_compare_and_store_ret8(volatile kmp_int8 *p, kmp_int8 cv, kmp_int8 sv);
        PROC  __kmp_compare_and_store_ret8

        movl      4(%esp), %ecx
        movb      8(%esp), %al
        movb      12(%esp), %dl
        lock
        cmpxchgb  %dl,(%ecx)
        ret

        DEBUG_INFO __kmp_compare_and_store_ret8

// kmp_int16
// __kmp_compare_and_store_ret16(volatile kmp_int16 *p, kmp_int16 cv,
//                               kmp_int16 sv);
        PROC  __kmp_compare_and_store_ret16

        movl      4(%esp), %ecx
        movw      8(%esp), %ax
        movw      12(%esp), %dx
        lock
        cmpxchgw  %dx,(%ecx)
        ret

        DEBUG_INFO __kmp_compare_and_store_ret16

// kmp_int32
// __kmp_compare_and_store_ret32(volatile kmp_int32 *p, kmp_int32 cv,
//                               kmp_int32 sv);
        PROC  __kmp_compare_and_store_ret32

        movl      4(%esp), %ecx
        movl      8(%esp), %eax
        movl      12(%esp), %edx
        lock
        cmpxchgl  %edx,(%ecx)
        ret

        DEBUG_INFO __kmp_compare_and_store_ret32

// kmp_int64
// __kmp_compare_and_store_ret64(volatile kmp_int64 *p, kmp_int64 cv,
//                               kmp_int64 sv);
        PROC  __kmp_compare_and_store_ret64

        pushl     %ebp
        movl      %esp, %ebp
        pushl     %ebx
        pushl     %edi
        movl      8(%ebp), %edi
        movl      12(%ebp), %eax        // "cv" low order word
        movl      16(%ebp), %edx        // "cv" high order word
        movl      20(%ebp), %ebx        // "sv" low order word
        movl      24(%ebp), %ecx        // "sv" high order word
        lock
        cmpxchg8b (%edi)
        popl      %edi
        popl      %ebx
        movl      %ebp, %esp
        popl      %ebp
        ret

        DEBUG_INFO __kmp_compare_and_store_ret64


//------------------------------------------------------------------------
// FUNCTION __kmp_xchg_real32
//
// kmp_real32
// __kmp_xchg_real32( volatile kmp_real32 *addr, kmp_real32 data );
//
// parameters:
// 	addr:	4(%esp)
// 	data:	8(%esp)
//
// return:	%eax
        PROC  __kmp_xchg_real32

        pushl   %ebp
        movl    %esp, %ebp
        subl    $4, %esp
        pushl   %esi

        movl    4(%ebp), %esi
        flds    (%esi)
                        // load <addr>
        fsts    -4(%ebp)
                        // store old value

        movl    8(%ebp), %eax

        lock
        xchgl   %eax, (%esi)

        flds    -4(%ebp)
                        // return old value

        popl    %esi
        movl    %ebp, %esp
        popl    %ebp
        ret

        DEBUG_INFO __kmp_xchg_real32

# endif /* !KMP_ASM_INTRINS */


//------------------------------------------------------------------------
// FUNCTION __kmp_load_x87_fpu_control_word
//
// void
// __kmp_load_x87_fpu_control_word( kmp_int16 *p );
//
// parameters:
// 	p:	4(%esp)
        PROC  __kmp_load_x87_fpu_control_word

        movl  4(%esp), %eax
        fldcw (%eax)
        ret

        DEBUG_INFO __kmp_load_x87_fpu_control_word


//------------------------------------------------------------------------
// FUNCTION __kmp_store_x87_fpu_control_word
//
// void
// __kmp_store_x87_fpu_control_word( kmp_int16 *p );
//
// parameters:
// 	p:	4(%esp)
        PROC  __kmp_store_x87_fpu_control_word

        movl  4(%esp), %eax
        fstcw (%eax)
        ret

        DEBUG_INFO __kmp_store_x87_fpu_control_word


//------------------------------------------------------------------------
// FUNCTION __kmp_clear_x87_fpu_status_word
//
// void
// __kmp_clear_x87_fpu_status_word();
        PROC  __kmp_clear_x87_fpu_status_word

        fnclex
        ret

        DEBUG_INFO __kmp_clear_x87_fpu_status_word


//------------------------------------------------------------------------
// typedef void	(*microtask_t)( int *gtid, int *tid, ... );
//
// int
// __kmp_invoke_microtask( microtask_t pkfn, int gtid, int tid,
//                         int argc, void *p_argv[] ) {
//    (*pkfn)( & gtid, & gtid, argv[0], ... );
//    return 1;
// }

// -- Begin __kmp_invoke_microtask
// mark_begin;
	PROC  __kmp_invoke_microtask

	pushl %ebp
	KMP_CFI_DEF_OFFSET 8
	KMP_CFI_OFFSET ebp,-8
	movl %esp,%ebp		// establish the base pointer for this routine.
	KMP_CFI_REGISTER ebp
	subl $8,%esp		// allocate space for two local variables.
				// These varibales are:
				//	argv: -4(%ebp)
				//	temp: -8(%ebp)
				//
	pushl %ebx		// save %ebx to use during this routine
				//
#if OMPT_SUPPORT
	movl 28(%ebp),%ebx	// get exit_frame address
	movl %ebp,(%ebx)	// save exit_frame
#endif

	movl 20(%ebp),%ebx	// Stack alignment - # args
	addl $2,%ebx		// #args +2  Always pass at least 2 args (gtid and tid)
	shll $2,%ebx		// Number of bytes used on stack: (#args+2)*4
	movl %esp,%eax		//
	subl %ebx,%eax		// %esp-((#args+2)*4) -> %eax -- without mods, stack ptr would be this
	movl %eax,%ebx		// Save to %ebx
	andl $0xFFFFFF80,%eax	// mask off 7 bits
	subl %eax,%ebx		// Amount to subtract from %esp
	subl %ebx,%esp		// Prepare the stack ptr --
				//   now it will be aligned on 128-byte boundary at the call

	movl 24(%ebp),%eax	// copy from p_argv[]
	movl %eax,-4(%ebp)	// into the local variable *argv.

	movl 20(%ebp),%ebx	// argc is 20(%ebp)
	shll $2,%ebx

KMP_LABEL(invoke_2):
	cmpl $0,%ebx
	jg  KMP_LABEL(invoke_4)
	jmp KMP_LABEL(invoke_3)
	ALIGN 2
KMP_LABEL(invoke_4):
	movl -4(%ebp),%eax
	subl $4,%ebx			// decrement argc.
	addl %ebx,%eax			// index into argv.
	movl (%eax),%edx
	pushl %edx

	jmp KMP_LABEL(invoke_2)
	ALIGN 2
KMP_LABEL(invoke_3):
	leal 16(%ebp),%eax		// push & tid
	pushl %eax

	leal 12(%ebp),%eax		// push & gtid
	pushl %eax

	movl 8(%ebp),%ebx
	call *%ebx			// call (*pkfn)();

	movl $1,%eax			// return 1;

	movl -12(%ebp),%ebx		// restore %ebx
	leave
	KMP_CFI_DEF esp,4
	ret

	DEBUG_INFO __kmp_invoke_microtask
// -- End  __kmp_invoke_microtask


// kmp_uint64
// __kmp_hardware_timestamp(void)
	PROC  __kmp_hardware_timestamp
	rdtsc
	ret

	DEBUG_INFO __kmp_hardware_timestamp
// -- End  __kmp_hardware_timestamp

#endif /* KMP_ARCH_X86 */


#if KMP_ARCH_X86_64

// -----------------------------------------------------------------------
// microtasking routines specifically written for IA-32 architecture and
// Intel(R) 64 running Linux* OS
// -----------------------------------------------------------------------

// -- Machine type P
// mark_description "Intel Corporation";
	.ident "Intel Corporation"
// --	.file "z_Linux_asm.S"
	.data
	ALIGN 4

// To prevent getting our code into .data section .text added to every routine
// definition for x86_64.
//------------------------------------------------------------------------
// FUNCTION __kmp_x86_cpuid
//
// void
// __kmp_x86_cpuid( int mode, int mode2, void *cpuid_buffer );
//
// parameters:
// 	mode:		%edi
// 	mode2:		%esi
// 	cpuid_buffer:	%rdx
        .text
	PROC  __kmp_x86_cpuid

	pushq  %rbp
	movq   %rsp,%rbp
        pushq  %rbx			// callee-save register

	movl   %esi, %ecx		// "mode2"
	movl   %edi, %eax		// "mode"
        movq   %rdx, %rsi               // cpuid_buffer
	cpuid				// Query the CPUID for the current processor

	movl   %eax, 0(%rsi)		// store results into buffer
	movl   %ebx, 4(%rsi)
	movl   %ecx, 8(%rsi)
	movl   %edx, 12(%rsi)

        popq   %rbx			// callee-save register
        movq   %rbp, %rsp
        popq   %rbp
	ret

        DEBUG_INFO __kmp_x86_cpuid



# if !KMP_ASM_INTRINS

//------------------------------------------------------------------------
// FUNCTION __kmp_test_then_add32
//
// kmp_int32
// __kmp_test_then_add32( volatile kmp_int32 *p, kmp_int32 d );
//
// parameters:
// 	p:	%rdi
// 	d:	%esi
//
// return:	%eax
        .text
        PROC  __kmp_test_then_add32

        movl      %esi, %eax	// "d"
        lock
        xaddl     %eax,(%rdi)
        ret

        DEBUG_INFO __kmp_test_then_add32


//------------------------------------------------------------------------
// FUNCTION __kmp_test_then_add64
//
// kmp_int64
// __kmp_test_then_add64( volatile kmp_int64 *p, kmp_int64 d );
//
// parameters:
// 	p:	%rdi
// 	d:	%rsi
//	return:	%rax
        .text
        PROC  __kmp_test_then_add64

        movq      %rsi, %rax	// "d"
        lock
        xaddq     %rax,(%rdi)
        ret

        DEBUG_INFO __kmp_test_then_add64


//------------------------------------------------------------------------
// FUNCTION __kmp_xchg_fixed8
//
// kmp_int32
// __kmp_xchg_fixed8( volatile kmp_int8 *p, kmp_int8 d );
//
// parameters:
// 	p:	%rdi
// 	d:	%sil
//
// return:	%al
        .text
        PROC  __kmp_xchg_fixed8

        movb      %sil, %al	// "d"

        lock
        xchgb     %al,(%rdi)
        ret

        DEBUG_INFO __kmp_xchg_fixed8


//------------------------------------------------------------------------
// FUNCTION __kmp_xchg_fixed16
//
// kmp_int16
// __kmp_xchg_fixed16( volatile kmp_int16 *p, kmp_int16 d );
//
// parameters:
// 	p:	%rdi
// 	d:	%si
// return:     %ax
        .text
        PROC  __kmp_xchg_fixed16

        movw      %si, %ax	// "d"

        lock
        xchgw     %ax,(%rdi)
        ret

        DEBUG_INFO __kmp_xchg_fixed16


//------------------------------------------------------------------------
// FUNCTION __kmp_xchg_fixed32
//
// kmp_int32
// __kmp_xchg_fixed32( volatile kmp_int32 *p, kmp_int32 d );
//
// parameters:
// 	p:	%rdi
// 	d:	%esi
//
// return:	%eax
        .text
        PROC  __kmp_xchg_fixed32

        movl      %esi, %eax	// "d"

        lock
        xchgl     %eax,(%rdi)
        ret

        DEBUG_INFO __kmp_xchg_fixed32


//------------------------------------------------------------------------
// FUNCTION __kmp_xchg_fixed64
//
// kmp_int64
// __kmp_xchg_fixed64( volatile kmp_int64 *p, kmp_int64 d );
//
// parameters:
// 	p:	%rdi
// 	d:	%rsi
// return:	%rax
        .text
        PROC  __kmp_xchg_fixed64

        movq      %rsi, %rax	// "d"

        lock
        xchgq     %rax,(%rdi)
        ret

        DEBUG_INFO __kmp_xchg_fixed64


//------------------------------------------------------------------------
// FUNCTION __kmp_compare_and_store8
//
// kmp_int8
// __kmp_compare_and_store8( volatile kmp_int8 *p, kmp_int8 cv, kmp_int8 sv );
//
// parameters:
// 	p:	%rdi
// 	cv:	%esi
//	sv:	%edx
//
// return:	%eax
        .text
        PROC  __kmp_compare_and_store8

        movb      %sil, %al	// "cv"
        lock
        cmpxchgb  %dl,(%rdi)
        sete      %al           // if %al == (%rdi) set %al = 1 else set %al = 0
        andq      $1, %rax      // sign extend previous instruction for return value
        ret

        DEBUG_INFO __kmp_compare_and_store8


//------------------------------------------------------------------------
// FUNCTION __kmp_compare_and_store16
//
// kmp_int16
// __kmp_compare_and_store16( volatile kmp_int16 *p, kmp_int16 cv, kmp_int16 sv );
//
// parameters:
// 	p:	%rdi
// 	cv:	%si
//	sv:	%dx
//
// return:	%eax
        .text
        PROC  __kmp_compare_and_store16

        movw      %si, %ax	// "cv"
        lock
        cmpxchgw  %dx,(%rdi)
        sete      %al           // if %ax == (%rdi) set %al = 1 else set %al = 0
        andq      $1, %rax      // sign extend previous instruction for return value
        ret

        DEBUG_INFO __kmp_compare_and_store16


//------------------------------------------------------------------------
// FUNCTION __kmp_compare_and_store32
//
// kmp_int32
// __kmp_compare_and_store32( volatile kmp_int32 *p, kmp_int32 cv, kmp_int32 sv );
//
// parameters:
// 	p:	%rdi
// 	cv:	%esi
//	sv:	%edx
//
// return:	%eax
        .text
        PROC  __kmp_compare_and_store32

        movl      %esi, %eax	// "cv"
        lock
        cmpxchgl  %edx,(%rdi)
        sete      %al           // if %eax == (%rdi) set %al = 1 else set %al = 0
        andq      $1, %rax      // sign extend previous instruction for return value
        ret

        DEBUG_INFO __kmp_compare_and_store32


//------------------------------------------------------------------------
// FUNCTION __kmp_compare_and_store64
//
// kmp_int32
// __kmp_compare_and_store64( volatile kmp_int64 *p, kmp_int64 cv, kmp_int64 sv );
//
// parameters:
// 	p:	%rdi
// 	cv:	%rsi
//	sv:	%rdx
//	return:	%eax
        .text
        PROC  __kmp_compare_and_store64

        movq      %rsi, %rax    // "cv"
        lock
        cmpxchgq  %rdx,(%rdi)
        sete      %al           // if %rax == (%rdi) set %al = 1 else set %al = 0
        andq      $1, %rax      // sign extend previous instruction for return value
        ret

        DEBUG_INFO __kmp_compare_and_store64

//------------------------------------------------------------------------
// FUNCTION __kmp_compare_and_store_ret8
//
// kmp_int8
// __kmp_compare_and_store_ret8( volatile kmp_int8 *p, kmp_int8 cv, kmp_int8 sv );
//
// parameters:
// 	p:	%rdi
// 	cv:	%esi
//	sv:	%edx
//
// return:	%eax
        .text
        PROC  __kmp_compare_and_store_ret8

        movb      %sil, %al	// "cv"
        lock
        cmpxchgb  %dl,(%rdi)
        ret

        DEBUG_INFO __kmp_compare_and_store_ret8


//------------------------------------------------------------------------
// FUNCTION __kmp_compare_and_store_ret16
//
// kmp_int16
// __kmp_compare_and_store16_ret( volatile kmp_int16 *p, kmp_int16 cv, kmp_int16 sv );
//
// parameters:
// 	p:	%rdi
// 	cv:	%si
//	sv:	%dx
//
// return:	%eax
        .text
        PROC  __kmp_compare_and_store_ret16

        movw      %si, %ax	// "cv"
        lock
        cmpxchgw  %dx,(%rdi)
        ret

        DEBUG_INFO __kmp_compare_and_store_ret16


//------------------------------------------------------------------------
// FUNCTION __kmp_compare_and_store_ret32
//
// kmp_int32
// __kmp_compare_and_store_ret32( volatile kmp_int32 *p, kmp_int32 cv, kmp_int32 sv );
//
// parameters:
// 	p:	%rdi
// 	cv:	%esi
//	sv:	%edx
//
// return:	%eax
        .text
        PROC  __kmp_compare_and_store_ret32

        movl      %esi, %eax	// "cv"
        lock
        cmpxchgl  %edx,(%rdi)
        ret

        DEBUG_INFO __kmp_compare_and_store_ret32


//------------------------------------------------------------------------
// FUNCTION __kmp_compare_and_store_ret64
//
// kmp_int64
// __kmp_compare_and_store_ret64( volatile kmp_int64 *p, kmp_int64 cv, kmp_int64 sv );
//
// parameters:
// 	p:	%rdi
// 	cv:	%rsi
//	sv:	%rdx
//	return:	%eax
        .text
        PROC  __kmp_compare_and_store_ret64

        movq      %rsi, %rax    // "cv"
        lock
        cmpxchgq  %rdx,(%rdi)
        ret

        DEBUG_INFO __kmp_compare_and_store_ret64

# endif /* !KMP_ASM_INTRINS */


# if !KMP_MIC

# if !KMP_ASM_INTRINS

//------------------------------------------------------------------------
// FUNCTION __kmp_xchg_real32
//
// kmp_real32
// __kmp_xchg_real32( volatile kmp_real32 *addr, kmp_real32 data );
//
// parameters:
// 	addr:	%rdi
// 	data:	%xmm0 (lower 4 bytes)
//
// return:	%xmm0 (lower 4 bytes)
        .text
        PROC  __kmp_xchg_real32

	movd	%xmm0, %eax	// load "data" to eax

         lock
         xchgl %eax, (%rdi)

	movd	%eax, %xmm0	// load old value into return register

        ret

        DEBUG_INFO __kmp_xchg_real32


//------------------------------------------------------------------------
// FUNCTION __kmp_xchg_real64
//
// kmp_real64
// __kmp_xchg_real64( volatile kmp_real64 *addr, kmp_real64 data );
//
// parameters:
//      addr:   %rdi
//      data:   %xmm0 (lower 8 bytes)
//      return: %xmm0 (lower 8 bytes)
        .text
        PROC  __kmp_xchg_real64

	movd	%xmm0, %rax	// load "data" to rax

         lock
	xchgq  %rax, (%rdi)

	movd	%rax, %xmm0	// load old value into return register
        ret

        DEBUG_INFO __kmp_xchg_real64


# endif /* !KMP_MIC */

# endif /* !KMP_ASM_INTRINS */


//------------------------------------------------------------------------
// FUNCTION __kmp_load_x87_fpu_control_word
//
// void
// __kmp_load_x87_fpu_control_word( kmp_int16 *p );
//
// parameters:
// 	p:	%rdi
        .text
        PROC  __kmp_load_x87_fpu_control_word

        fldcw (%rdi)
        ret

        DEBUG_INFO __kmp_load_x87_fpu_control_word


//------------------------------------------------------------------------
// FUNCTION __kmp_store_x87_fpu_control_word
//
// void
// __kmp_store_x87_fpu_control_word( kmp_int16 *p );
//
// parameters:
// 	p:	%rdi
        .text
        PROC  __kmp_store_x87_fpu_control_word

        fstcw (%rdi)
        ret

        DEBUG_INFO __kmp_store_x87_fpu_control_word


//------------------------------------------------------------------------
// FUNCTION __kmp_clear_x87_fpu_status_word
//
// void
// __kmp_clear_x87_fpu_status_word();
        .text
        PROC  __kmp_clear_x87_fpu_status_word

#if KMP_MIC
// TODO: remove the workaround for problem with fnclex instruction (no CQ known)
        fstenv  -32(%rsp)              // store FP env
        andw    $~0x80ff, 4-32(%rsp)   // clear 0-7,15 bits of FP SW
        fldenv  -32(%rsp)              // load FP env back
        ret
#else
        fnclex
        ret
#endif

        DEBUG_INFO __kmp_clear_x87_fpu_status_word


//------------------------------------------------------------------------
// typedef void	(*microtask_t)( int *gtid, int *tid, ... );
//
// int
// __kmp_invoke_microtask( void (*pkfn) (int gtid, int tid, ...),
//		           int gtid, int tid,
//                         int argc, void *p_argv[] ) {
//    (*pkfn)( & gtid, & tid, argv[0], ... );
//    return 1;
// }
//
// note: at call to pkfn must have %rsp 128-byte aligned for compiler
//
// parameters:
//      %rdi:  	pkfn
//	%esi:	gtid
//	%edx:	tid
//	%ecx:	argc
//	%r8:	p_argv
//	%r9:	&exit_frame
//
// locals:
//	__gtid:	gtid parm pushed on stack so can pass &gtid to pkfn
//	__tid:	tid parm pushed on stack so can pass &tid to pkfn
//
// reg temps:
//	%rax:	used all over the place
//	%rdx:	used in stack pointer alignment calculation
//	%r11:	used to traverse p_argv array
//	%rsi:	used as temporary for stack parameters
//		used as temporary for number of pkfn parms to push
//	%rbx:	used to hold pkfn address, and zero constant, callee-save
//
// return:	%eax 	(always 1/TRUE)
__gtid = -16
__tid = -24

// -- Begin __kmp_invoke_microtask
// mark_begin;
        .text
	PROC  __kmp_invoke_microtask

	pushq 	%rbp		// save base pointer
	KMP_CFI_DEF_OFFSET 16
	KMP_CFI_OFFSET rbp,-16
	movq 	%rsp,%rbp	// establish the base pointer for this routine.
	KMP_CFI_REGISTER rbp

#if OMPT_SUPPORT
	movq	%rbp, (%r9)	// save exit_frame
#endif

	pushq 	%rbx		// %rbx is callee-saved register
	pushq	%rsi		// Put gtid on stack so can pass &tgid to pkfn
	pushq	%rdx		// Put tid on stack so can pass &tid to pkfn

	movq	%rcx, %rax	// Stack alignment calculation begins; argc -> %rax
	movq	$0, %rbx	// constant for cmovs later
	subq	$4, %rax	// subtract four args passed in registers to pkfn
#if KMP_MIC
	js	KMP_LABEL(kmp_0)	// jump to movq
	jmp	KMP_LABEL(kmp_0_exit)	// jump ahead
KMP_LABEL(kmp_0):
	movq	%rbx, %rax	// zero negative value in %rax <- max(0, argc-4)
KMP_LABEL(kmp_0_exit):
#else
	cmovsq	%rbx, %rax	// zero negative value in %rax <- max(0, argc-4)
#endif // KMP_MIC

	movq	%rax, %rsi	// save max(0, argc-4) -> %rsi for later
	shlq 	$3, %rax	// Number of bytes used on stack: max(0, argc-4)*8

	movq 	%rsp, %rdx	//
	subq 	%rax, %rdx	// %rsp-(max(0,argc-4)*8) -> %rdx --
				// without align, stack ptr would be this
	movq 	%rdx, %rax	// Save to %rax

	andq 	$0xFFFFFFFFFFFFFF80, %rax  // mask off lower 7 bits (128 bytes align)
	subq 	%rax, %rdx	// Amount to subtract from %rsp
	subq 	%rdx, %rsp	// Prepare the stack ptr --
				// now %rsp will align to 128-byte boundary at call site

				// setup pkfn parameter reg and stack
	movq	%rcx, %rax	// argc -> %rax
	cmpq	$0, %rsi
	je	KMP_LABEL(kmp_invoke_pass_parms)	// jump ahead if no parms to push
	shlq	$3, %rcx	// argc*8 -> %rcx
	movq 	%r8, %rdx	// p_argv -> %rdx
	addq	%rcx, %rdx	// &p_argv[argc] -> %rdx

	movq	%rsi, %rcx	// max (0, argc-4) -> %rcx

KMP_LABEL(kmp_invoke_push_parms):
	// push nth - 7th parms to pkfn on stack
	subq	$8, %rdx	// decrement p_argv pointer to previous parm
	movq	(%rdx), %rsi	// p_argv[%rcx-1] -> %rsi
	pushq	%rsi		// push p_argv[%rcx-1] onto stack (reverse order)
	subl	$1, %ecx

// C69570: "X86_64_RELOC_BRANCH not supported" error at linking on mac_32e
//		if the name of the label that is an operand of this jecxz starts with a dot (".");
//	   Apple's linker does not support 1-byte length relocation;
//         Resolution: replace all .labelX entries with L_labelX.

	jecxz   KMP_LABEL(kmp_invoke_pass_parms)  // stop when four p_argv[] parms left
	jmp	KMP_LABEL(kmp_invoke_push_parms)
	ALIGN 3
KMP_LABEL(kmp_invoke_pass_parms):	// put 1st - 6th parms to pkfn in registers.
				// order here is important to avoid trashing
				// registers used for both input and output parms!
	movq	%rdi, %rbx	// pkfn -> %rbx
	leaq	__gtid(%rbp), %rdi // &gtid -> %rdi (store 1st parm to pkfn)
	leaq	__tid(%rbp), %rsi  // &tid -> %rsi (store 2nd parm to pkfn)

	movq	%r8, %r11	// p_argv -> %r11

#if KMP_MIC
	cmpq	$4, %rax	// argc >= 4?
	jns	KMP_LABEL(kmp_4)	// jump to movq
	jmp	KMP_LABEL(kmp_4_exit)	// jump ahead
KMP_LABEL(kmp_4):
	movq	24(%r11), %r9	// p_argv[3] -> %r9 (store 6th parm to pkfn)
KMP_LABEL(kmp_4_exit):

	cmpq	$3, %rax	// argc >= 3?
	jns	KMP_LABEL(kmp_3)	// jump to movq
	jmp	KMP_LABEL(kmp_3_exit)	// jump ahead
KMP_LABEL(kmp_3):
	movq	16(%r11), %r8	// p_argv[2] -> %r8 (store 5th parm to pkfn)
KMP_LABEL(kmp_3_exit):

	cmpq	$2, %rax	// argc >= 2?
	jns	KMP_LABEL(kmp_2)	// jump to movq
	jmp	KMP_LABEL(kmp_2_exit)	// jump ahead
KMP_LABEL(kmp_2):
	movq	8(%r11), %rcx	// p_argv[1] -> %rcx (store 4th parm to pkfn)
KMP_LABEL(kmp_2_exit):

	cmpq	$1, %rax	// argc >= 1?
	jns	KMP_LABEL(kmp_1)	// jump to movq
	jmp	KMP_LABEL(kmp_1_exit)	// jump ahead
KMP_LABEL(kmp_1):
	movq	(%r11), %rdx	// p_argv[0] -> %rdx (store 3rd parm to pkfn)
KMP_LABEL(kmp_1_exit):
#else
	cmpq	$4, %rax	// argc >= 4?
	cmovnsq	24(%r11), %r9	// p_argv[3] -> %r9 (store 6th parm to pkfn)

	cmpq	$3, %rax	// argc >= 3?
	cmovnsq	16(%r11), %r8	// p_argv[2] -> %r8 (store 5th parm to pkfn)

	cmpq	$2, %rax	// argc >= 2?
	cmovnsq	8(%r11), %rcx	// p_argv[1] -> %rcx (store 4th parm to pkfn)

	cmpq	$1, %rax	// argc >= 1?
	cmovnsq	(%r11), %rdx	// p_argv[0] -> %rdx (store 3rd parm to pkfn)
#endif // KMP_MIC

	call	*%rbx		// call (*pkfn)();
	movq	$1, %rax	// move 1 into return register;

	movq	-8(%rbp), %rbx	// restore %rbx	using %rbp since %rsp was modified
	movq 	%rbp, %rsp	// restore stack pointer
	popq 	%rbp		// restore frame pointer
	KMP_CFI_DEF rsp,8
	ret

	DEBUG_INFO __kmp_invoke_microtask
// -- End  __kmp_invoke_microtask

// kmp_uint64
// __kmp_hardware_timestamp(void)
        .text
	PROC  __kmp_hardware_timestamp
	rdtsc
	shlq    $32, %rdx
	orq     %rdx, %rax
	ret

	DEBUG_INFO __kmp_hardware_timestamp
// -- End  __kmp_hardware_timestamp

//------------------------------------------------------------------------
// FUNCTION __kmp_bsr32
//
// int
// __kmp_bsr32( int );
        .text
        PROC  __kmp_bsr32

        bsr    %edi,%eax
        ret

        DEBUG_INFO __kmp_bsr32


// -----------------------------------------------------------------------
#endif /* KMP_ARCH_X86_64 */

// '
#if (KMP_OS_LINUX || KMP_OS_DARWIN) && KMP_ARCH_AARCH64

//------------------------------------------------------------------------
//
// typedef void	(*microtask_t)( int *gtid, int *tid, ... );
//
// int
// __kmp_invoke_microtask( void (*pkfn) (int gtid, int tid, ...),
//		           int gtid, int tid,
//                         int argc, void *p_argv[] ) {
//    (*pkfn)( & gtid, & tid, argv[0], ... );
//    return 1;
// }
//
// parameters:
//	x0:	pkfn
//	w1:	gtid
//	w2:	tid
//	w3:	argc
//	x4:	p_argv
//	x5:	&exit_frame
//
// locals:
//	__gtid:	gtid parm pushed on stack so can pass &gtid to pkfn
//	__tid:	tid parm pushed on stack so can pass &tid to pkfn
//
// reg temps:
//	 x8:	used to hold pkfn address
//	 w9:	used as temporary for number of pkfn parms
//	x10:	used to traverse p_argv array
//	x11:	used as temporary for stack placement calculation
//	x12:	used as temporary for stack parameters
//	x19:	used to preserve exit_frame_ptr, callee-save
//
// return:	w0	(always 1/TRUE)
//

__gtid = 4
__tid = 8

// -- Begin __kmp_invoke_microtask
// mark_begin;
	.text
	PROC __kmp_invoke_microtask

	stp	x29, x30, [sp, #-16]!
# if OMPT_SUPPORT
	stp	x19, x20, [sp, #-16]!
# endif
	mov	x29, sp

	orr	w9, wzr, #1
	add	w9, w9, w3, lsr #1
	sub	sp, sp, w9, lsl #4
	mov	x11, sp

	mov	x8, x0
	str	w1, [x29, #-__gtid]
	str	w2, [x29, #-__tid]
	mov	w9, w3
	mov	x10, x4
# if OMPT_SUPPORT
	mov	x19, x5
	str	x29, [x19]
# endif

	sub	x0, x29, #__gtid
	sub	x1, x29, #__tid

	cbz	w9, KMP_LABEL(kmp_1)
	ldr	x2, [x10]

	sub	w9, w9, #1
	cbz	w9, KMP_LABEL(kmp_1)
	ldr	x3, [x10, #8]!

	sub	w9, w9, #1
	cbz	w9, KMP_LABEL(kmp_1)
	ldr	x4, [x10, #8]!

	sub	w9, w9, #1
	cbz	w9, KMP_LABEL(kmp_1)
	ldr	x5, [x10, #8]!

	sub	w9, w9, #1
	cbz	w9, KMP_LABEL(kmp_1)
	ldr	x6, [x10, #8]!

	sub	w9, w9, #1
	cbz	w9, KMP_LABEL(kmp_1)
	ldr	x7, [x10, #8]!

KMP_LABEL(kmp_0):
	sub	w9, w9, #1
	cbz	w9, KMP_LABEL(kmp_1)
	ldr	x12, [x10, #8]!
	str	x12, [x11], #8
	b	KMP_LABEL(kmp_0)
KMP_LABEL(kmp_1):
	blr	x8
	orr	w0, wzr, #1
	mov	sp, x29
# if OMPT_SUPPORT
	str	xzr, [x19]
	ldp	x19, x20, [sp], #16
# endif
	ldp	x29, x30, [sp], #16
	ret

	DEBUG_INFO __kmp_invoke_microtask
// -- End  __kmp_invoke_microtask

#endif /* (KMP_OS_LINUX || KMP_OS_DARWIN) && KMP_ARCH_AARCH64 */

#if KMP_ARCH_PPC64

//------------------------------------------------------------------------
//
// typedef void	(*microtask_t)( int *gtid, int *tid, ... );
//
// int
// __kmp_invoke_microtask( void (*pkfn) (int gtid, int tid, ...),
//		           int gtid, int tid,
//                         int argc, void *p_argv[] ) {
//    (*pkfn)( & gtid, & tid, argv[0], ... );
//    return 1;
// }
//
// parameters:
//	r3:	pkfn
//	r4:	gtid
//	r5:	tid
//	r6:	argc
//	r7:	p_argv
//	r8:	&exit_frame
//
// return:	r3	(always 1/TRUE)
//
	.text
# if KMP_ARCH_PPC64_LE
	.abiversion 2
# endif
	.globl	__kmp_invoke_microtask

# if KMP_ARCH_PPC64_LE
	.p2align	4
# else
	.p2align	2
# endif

	.type	__kmp_invoke_microtask,@function

# if KMP_ARCH_PPC64_LE
__kmp_invoke_microtask:
.Lfunc_begin0:
.Lfunc_gep0:
	addis 2, 12, .TOC.-.Lfunc_gep0@ha
	addi 2, 2, .TOC.-.Lfunc_gep0@l
.Lfunc_lep0:
	.localentry	__kmp_invoke_microtask, .Lfunc_lep0-.Lfunc_gep0
# else
	.section	.opd,"aw",@progbits
__kmp_invoke_microtask:
	.p2align	3
	.quad	.Lfunc_begin0
	.quad	.TOC.@tocbase
	.quad	0
	.text
.Lfunc_begin0:
# endif

// -- Begin __kmp_invoke_microtask
// mark_begin;

// We need to allocate a stack frame large enough to hold all of the parameters
// on the stack for the microtask plus what this function needs. That's 48
// bytes under the ELFv1 ABI (32 bytes under ELFv2), plus 8*(2 + argc) for the
// parameters to the microtask, plus 8 bytes to store the values of r4 and r5,
// and 8 bytes to store r31. With OMP-T support, we need an additional 8 bytes
// to save r30 to hold a copy of r8.

	.cfi_startproc
	mflr 0
	std 31, -8(1)
	std 0, 16(1)

// This is unusual because normally we'd set r31 equal to r1 after the stack
// frame is established. In this case, however, we need to dynamically compute
// the stack frame size, and so we keep a direct copy of r1 to access our
// register save areas and restore the r1 value before returning.
	mr 31, 1
	.cfi_def_cfa_register r31
	.cfi_offset r31, -8
	.cfi_offset lr, 16

// Compute the size necessary for the local stack frame.
# if KMP_ARCH_PPC64_LE
	li 12, 72
# else
	li 12, 88
# endif
	sldi 0, 6, 3
	add 12, 0, 12
	neg 12, 12

// We need to make sure that the stack frame stays aligned (to 16 bytes, except
// under the BG/Q CNK, where it must be to 32 bytes).
# if KMP_OS_CNK
	li 0, -32
# else
	li 0, -16
# endif
	and 12, 0, 12

// Establish the local stack frame.
	stdux 1, 1, 12

# if OMPT_SUPPORT
	.cfi_offset r30, -16
	std 30, -16(31)
	std 1, 0(8)
	mr 30, 8
# endif

// Store gtid and tid to the stack because they're passed by reference to the microtask.
	stw 4, -20(31)
	stw 5, -24(31)

	mr 12, 6
	mr 4, 7

	cmpwi 0, 12, 1
	blt	 0, .Lcall

	ld 5, 0(4)

	cmpwi 0, 12, 2
	blt	 0, .Lcall

	ld 6, 8(4)

	cmpwi 0, 12, 3
	blt	 0, .Lcall

	ld 7, 16(4)

	cmpwi 0, 12, 4
	blt	 0, .Lcall

	ld 8, 24(4)

	cmpwi 0, 12, 5
	blt	 0, .Lcall

	ld 9, 32(4)

	cmpwi 0, 12, 6
	blt	 0, .Lcall

	ld 10, 40(4)

	cmpwi 0, 12, 7
	blt	 0, .Lcall

// There are more than 6 microtask parameters, so we need to store the
// remainder to the stack.
	addi 12, 12, -6
	mtctr 12

// These are set to 8 bytes before the first desired store address (we're using
// pre-increment loads and stores in the loop below). The parameter save area
// for the microtask begins 48 + 8*8 == 112 bytes above r1 for ELFv1 and
// 32 + 8*8 == 96 bytes above r1 for ELFv2.
	addi 4, 4, 40
# if KMP_ARCH_PPC64_LE
	addi 12, 1, 88
# else
	addi 12, 1, 104
# endif

.Lnext:
	ldu 0, 8(4)
	stdu 0, 8(12)
	bdnz .Lnext

.Lcall:
# if KMP_ARCH_PPC64_LE
	std 2, 24(1)
	mr 12, 3
#else
	std 2, 40(1)
// For ELFv1, we need to load the actual function address from the function descriptor.
	ld 12, 0(3)
	ld 2, 8(3)
	ld 11, 16(3)
#endif

	addi 3, 31, -20
	addi 4, 31, -24

	mtctr 12
	bctrl
# if KMP_ARCH_PPC64_LE
	ld 2, 24(1)
# else
	ld 2, 40(1)
# endif

# if OMPT_SUPPORT
	li 3, 0
	std 3, 0(30)
# endif

	li 3, 1

# if OMPT_SUPPORT
	ld 30, -16(31)
# endif

	mr 1, 31
	ld 0, 16(1)
	ld 31, -8(1)
	mtlr 0
	blr

	.long	0
	.quad	0
.Lfunc_end0:
	.size	__kmp_invoke_microtask, .Lfunc_end0-.Lfunc_begin0
	.cfi_endproc

// -- End  __kmp_invoke_microtask

#endif /* KMP_ARCH_PPC64 */

#if KMP_ARCH_ARM || KMP_ARCH_MIPS
    .data
    .comm .gomp_critical_user_,32,8
    .data
    .align 4
    .global __kmp_unnamed_critical_addr
__kmp_unnamed_critical_addr:
    .4byte .gomp_critical_user_
    .size __kmp_unnamed_critical_addr,4
#endif /* KMP_ARCH_ARM */

#if KMP_ARCH_PPC64 || KMP_ARCH_AARCH64 || KMP_ARCH_MIPS64
    .data
    .comm .gomp_critical_user_,32,8
    .data
    .align 8
    .global __kmp_unnamed_critical_addr
__kmp_unnamed_critical_addr:
    .8byte .gomp_critical_user_
    .size __kmp_unnamed_critical_addr,8
#endif /* KMP_ARCH_PPC64 || KMP_ARCH_AARCH64 */

#if KMP_OS_LINUX
# if KMP_ARCH_ARM
.section .note.GNU-stack,"",%progbits
# else
.section .note.GNU-stack,"",@progbits
# endif
#endif
