#ifndef	_I386_HW_BREAKPOINT_H
#define	_I386_HW_BREAKPOINT_H

#ifdef	__KERNEL__
#define	__ARCH_HW_BREAKPOINT_H

struct arch_hw_breakpoint {
	char		*name; /* Contains name of the symbol to set bkpt */
	unsigned long	address;
	u8		len;
	u8		type;
};

#include <linux/kdebug.h>
#include <linux/hw_breakpoint.h>

/* Available HW breakpoint length encodings */
#define HW_BREAKPOINT_LEN_1		0x40
#define HW_BREAKPOINT_LEN_2		0x44
#define HW_BREAKPOINT_LEN_4		0x4c
#define HW_BREAKPOINT_LEN_EXECUTE	0x40

#ifdef CONFIG_X86_64
#define HW_BREAKPOINT_LEN_8		0x48
#endif

/* Available HW breakpoint type encodings */

/* trigger on instruction execute */
#define HW_BREAKPOINT_EXECUTE	0x80
/* trigger on memory write */
#define HW_BREAKPOINT_WRITE	0x81
/* trigger on memory read or write */
#define HW_BREAKPOINT_RW	0x83

/* Total number of available HW breakpoint registers */
#define HBP_NUM 4

extern struct hw_breakpoint *hbp_kernel[HBP_NUM];
DECLARE_PER_CPU(struct hw_breakpoint*, this_hbp_kernel[HBP_NUM]);
extern unsigned int hbp_user_refcount[HBP_NUM];

extern void arch_install_thread_hw_breakpoint(struct task_struct *tsk);
extern void arch_uninstall_thread_hw_breakpoint(void);
extern int arch_check_va_in_userspace(unsigned long va, u8 hbp_len);
extern int arch_validate_hwbkpt_settings(struct hw_breakpoint *bp,
						struct task_struct *tsk);
extern void arch_update_user_hw_breakpoint(int pos, struct task_struct *tsk);
extern void arch_flush_thread_hw_breakpoint(struct task_struct *tsk);
extern void arch_update_kernel_hw_breakpoint(void *);
extern int hw_breakpoint_exceptions_notify(struct notifier_block *unused,
				     unsigned long val, void *data);
#endif	/* __KERNEL__ */
#endif	/* _I386_HW_BREAKPOINT_H */

