#ifndef _ASM_X86_VSYSCALL_H
#define _ASM_X86_VSYSCALL_H

#include <linux/seqlock.h>
#include <uapi/asm/vsyscall.h>

#ifdef CONFIG_X86_VSYSCALL_EMULATION
extern void map_vsyscall(void);

/*
 * Called on instruction fetch fault in vsyscall page.
 * Returns true if handled.
 */
extern bool emulate_vsyscall(struct pt_regs *regs, unsigned long address);
extern bool vsyscall_enabled(void);
#else
static inline void map_vsyscall(void) {}
static inline bool emulate_vsyscall(struct pt_regs *regs, unsigned long address)
{
	return false;
}
static inline bool vsyscall_enabled(void) { return false; }
#endif

#endif /* _ASM_X86_VSYSCALL_H */
