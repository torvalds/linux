#ifndef ASM_X86__UNWIND_H
#define ASM_X86__UNWIND_H

#define UNW_PC(frame) ((void)(frame), 0UL)
#define UNW_SP(frame) ((void)(frame), 0UL)
#define UNW_FP(frame) ((void)(frame), 0UL)

static inline int arch_unw_user_mode(const void *info)
{
	return 0;
}

#endif /* ASM_X86__UNWIND_H */
