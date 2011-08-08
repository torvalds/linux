#ifndef _ASM_X86_RWLOCK_H
#define _ASM_X86_RWLOCK_H

#include <asm/asm.h>

#if CONFIG_NR_CPUS <= 2048

#ifndef __ASSEMBLY__
typedef union {
	s32 lock;
	s32 write;
} arch_rwlock_t;
#endif

#define RW_LOCK_BIAS		0x00100000
#define READ_LOCK_SIZE(insn)	__ASM_FORM(insn##l)
#define READ_LOCK_ATOMIC(n)	atomic_##n
#define WRITE_LOCK_ADD(n)	__ASM_FORM_COMMA(addl n)
#define WRITE_LOCK_SUB(n)	__ASM_FORM_COMMA(subl n)
#define WRITE_LOCK_CMP		RW_LOCK_BIAS

#else /* CONFIG_NR_CPUS > 2048 */

#include <linux/const.h>

#ifndef __ASSEMBLY__
typedef union {
	s64 lock;
	struct {
		u32 read;
		s32 write;
	};
} arch_rwlock_t;
#endif

#define RW_LOCK_BIAS		(_AC(1,L) << 32)
#define READ_LOCK_SIZE(insn)	__ASM_FORM(insn##q)
#define READ_LOCK_ATOMIC(n)	atomic64_##n
#define WRITE_LOCK_ADD(n)	__ASM_FORM(incl)
#define WRITE_LOCK_SUB(n)	__ASM_FORM(decl)
#define WRITE_LOCK_CMP		1

#endif /* CONFIG_NR_CPUS */

#define __ARCH_RW_LOCK_UNLOCKED		{ RW_LOCK_BIAS }

/* Actual code is in asm/spinlock.h or in arch/x86/lib/rwlock.S */

#endif /* _ASM_X86_RWLOCK_H */
