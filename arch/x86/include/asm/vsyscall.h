#ifndef _ASM_X86_VSYSCALL_H
#define _ASM_X86_VSYSCALL_H

enum vsyscall_num {
	__NR_vgettimeofday,
	__NR_vtime,
	__NR_vgetcpu,
};

#define VSYSCALL_START (-10UL << 20)
#define VSYSCALL_SIZE 1024
#define VSYSCALL_END (-2UL << 20)
#define VSYSCALL_MAPPED_PAGES 1
#define VSYSCALL_ADDR(vsyscall_nr) (VSYSCALL_START+VSYSCALL_SIZE*(vsyscall_nr))

#ifdef __KERNEL__
#include <linux/seqlock.h>

#define VGETCPU_RDTSCP	1
#define VGETCPU_LSL	2

/* kernel space (writeable) */
extern int vgetcpu_mode;
extern struct timezone sys_tz;

#include <asm/vvar.h>

extern void map_vsyscall(void);

/*
 * Called on instruction fetch fault in vsyscall page.
 * Returns true if handled.
 */
extern bool emulate_vsyscall(struct pt_regs *regs, unsigned long address);

#ifdef CONFIG_X86_64

#define VGETCPU_CPU_MASK 0xfff

static inline unsigned int __getcpu(void)
{
	unsigned int p;

	if (VVAR(vgetcpu_mode) == VGETCPU_RDTSCP) {
		/* Load per CPU data from RDTSCP */
		native_read_tscp(&p);
	} else {
		/* Load per CPU data from GDT */
		asm("lsl %1,%0" : "=r" (p) : "r" (__PER_CPU_SEG));
	}

	return p;
}
#endif /* CONFIG_X86_64 */

#endif /* __KERNEL__ */

#endif /* _ASM_X86_VSYSCALL_H */
