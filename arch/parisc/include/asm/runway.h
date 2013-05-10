#ifndef ASM_PARISC_RUNWAY_H
#define ASM_PARISC_RUNWAY_H
#ifdef __KERNEL__

/* declared in arch/parisc/kernel/setup.c */
extern struct proc_dir_entry * proc_runway_root;

#define RUNWAY_STATUS	0x10
#define RUNWAY_DEBUG	0x40

#endif /* __KERNEL__ */
#endif /* ASM_PARISC_RUNWAY_H */
