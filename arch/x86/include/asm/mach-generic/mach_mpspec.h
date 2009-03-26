#ifndef _ASM_X86_MACH_GENERIC_MACH_MPSPEC_H
#define _ASM_X86_MACH_GENERIC_MACH_MPSPEC_H

#define MAX_IRQ_SOURCES 256

/* Summit or generic (i.e. installer) kernels need lots of bus entries. */
/* Maximum 256 PCI busses, plus 1 ISA bus in each of 4 cabinets. */
#define MAX_MP_BUSSES 260

extern void numaq_mps_oem_check(struct mpc_table *, char *, char *);

#endif /* _ASM_X86_MACH_GENERIC_MACH_MPSPEC_H */
