#ifndef __ARCH_ARM_FAULT_H
#define __ARCH_ARM_FAULT_H

/*
 * Fault status register encodings.  We steal bit 31 for our own purposes.
 */
#define FSR_LNX_PF		(1 << 31)
#define FSR_WRITE		(1 << 11)
#define FSR_FS4			(1 << 10)
#define FSR_FS3_0		(15)

static inline int fsr_fs(unsigned int fsr)
{
	return (fsr & FSR_FS3_0) | (fsr & FSR_FS4) >> 6;
}

void do_bad_area(unsigned long addr, unsigned int fsr, struct pt_regs *regs);
unsigned long search_exception_table(unsigned long addr);

#endif	/* __ARCH_ARM_FAULT_H */
