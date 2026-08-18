// SPDX-License-Identifier: GPL-2.0-only
#include <vdso/futex.h>

/*
 * Assembly template for the try unlock functions. The basic functionality is:
 *
 *		mov		esi, %eax	Move the TID into EAX
 *		xor		%ecx, %ecx	Clear ECX
 *		lock_cmpxchgl	%ecx, (%rdi)	Attempt the TID -> 0 transition
 * .Lcs_start:					Start of the critical section
 *		jnz		.Lcs_end	If cmpxchl failed jump to the end
 * .Lcs_success:				Start of the success section
 *		movq		%rcx, (%rdx)	Set the pending op pointer to 0
 * .Lcs_end:					End of the critical section
 *
 * .Lcs_start and .Lcs_end establish the critical section range. .Lcs_success is
 * technically not required, but there for illustration, debugging and testing.
 *
 * When CONFIG_COMPAT is enabled then the 64-bit VDSO provides two functions.
 * One for the regular 64-bit sized pending operation pointer and one for a
 * 32-bit sized pointer to support gaming emulators.
 *
 * The 32-bit VDSO provides only the one for 32-bit sized pointers.
 */
#define __stringify_1(x...)	#x
#define __stringify(x...)	__stringify_1(x)

#define LABEL(prefix, which)	__stringify(prefix##_try_unlock_cs_##which:)

#define JNZ_END(prefix)		"jnz " __stringify(prefix) "_try_unlock_cs_end\n"

#define CLEAR_POPQ		"movq	%[zero],  %a[pop]\n"
#define CLEAR_POPL		"movl	%k[zero], %a[pop]\n"

#define futex_robust_try_unlock(prefix, clear_pop, __lock, __tid, __pop)\
({									\
	asm volatile (							\
		"						\n"	\
		"	lock cmpxchgl	%k[zero], %a[lock]	\n"	\
		"						\n"	\
		LABEL(prefix, start)					\
		"						\n"	\
		JNZ_END(prefix)						\
		"						\n"	\
		LABEL(prefix, success)					\
		"						\n"	\
			clear_pop					\
		"						\n"	\
		LABEL(prefix, end)					\
		: [tid]   "+&a" (__tid)					\
		: [lock]  "D"   (__lock),				\
		  [pop]   "d"   (__pop),				\
		  [zero]  "r"   (0UL)					\
		: "memory"						\
	);								\
	__tid;								\
})

#ifdef __x86_64__
__u32 __vdso_futex_robust_list64_try_unlock(__u32 *lock, __u32 tid, __u64 *pop)
{
	return futex_robust_try_unlock(__futex_list64, CLEAR_POPQ, lock, tid, pop);
}
#endif /* __x86_64__ */

#if defined(CONFIG_X86_32) || defined(CONFIG_COMPAT)
__u32 __vdso_futex_robust_list32_try_unlock(__u32 *lock, __u32 tid, __u32 *pop)
{
	return futex_robust_try_unlock(__futex_list32, CLEAR_POPL, lock, tid, pop);
}
#endif /* CONFIG_X86_32 || CONFIG_COMPAT */
