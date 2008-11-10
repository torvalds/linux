#ifndef _ASM_X86_UNWIND_H
#define _ASM_X86_UNWIND_H

#define UNW_PC(frame) ((void)(frame), 0UL)
#define UNW_SP(frame) ((void)(frame), 0UL)
#define UNW_FP(frame) ((void)(frame), 0UL)

static inline int arch_unw_user_mode(const void *info)
{
	return 0;
}

#endif /* _ASM_X86_UNWIND_H */
