/* SPDX-License-Identifier: GPL-2.0 */
#ifndef	_I386_HW_BREAKPOINT_H
#define	_I386_HW_BREAKPOINT_H

#include <uapi/asm/hw_breakpoint.h>

#define	__ARCH_HW_BREAKPOINT_H

/*
 * The name should probably be something dealt in
 * a higher level. While dealing with the user
 * (display/resolving)
 */
struct arch_hw_breakpoint {
	unsigned long	address;
	unsigned long	mask;
	u8		len;
	u8		type;
};

#include <linux/kdebug.h>
#include <linux/percpu.h>
#include <linux/list.h>

/* Available HW breakpoint length encodings */
#define X86_BREAKPOINT_LEN_X		0x40
#define X86_BREAKPOINT_LEN_1		0x40
#define X86_BREAKPOINT_LEN_2		0x44
#define X86_BREAKPOINT_LEN_4		0x4c

#ifdef CONFIG_X86_64
#define X86_BREAKPOINT_LEN_8		0x48
#endif

/* Available HW breakpoint type encodings */

/* trigger on instruction execute */
#define X86_BREAKPOINT_EXECUTE	0x80
/* trigger on memory write */
#define X86_BREAKPOINT_WRITE	0x81
/* trigger on memory read or write */
#define X86_BREAKPOINT_RW	0x83

/* Total number of available HW breakpoint registers */
#define HBP_NUM 4

#define hw_breakpoint_slots(type) (HBP_NUM)

struct perf_event_attr;
struct perf_event;
struct pmu;

extern int arch_check_bp_in_kernelspace(struct arch_hw_breakpoint *hw);
extern int hw_breakpoint_arch_parse(struct perf_event *bp,
				    const struct perf_event_attr *attr,
				    struct arch_hw_breakpoint *hw);
extern int hw_breakpoint_exceptions_notify(struct notifier_block *unused,
					   unsigned long val, void *data);


int arch_install_hw_breakpoint(struct perf_event *bp);
void arch_uninstall_hw_breakpoint(struct perf_event *bp);
void hw_breakpoint_pmu_read(struct perf_event *bp);
void hw_breakpoint_pmu_unthrottle(struct perf_event *bp);

extern void
arch_fill_perf_breakpoint(struct perf_event *bp);

unsigned long encode_dr7(int drnum, unsigned int len, unsigned int type);
int decode_dr7(unsigned long dr7, int bpnum, unsigned *len, unsigned *type);

extern int arch_bp_generic_fields(int x86_len, int x86_type,
				  int *gen_len, int *gen_type);

extern struct pmu perf_ops_bp;

#endif	/* _I386_HW_BREAKPOINT_H */
