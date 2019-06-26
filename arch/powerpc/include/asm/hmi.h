/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Hypervisor Maintenance Interrupt header file.
 *
 * Copyright 2015 IBM Corporation
 * Author: Mahesh Salgaonkar <mahesh@linux.vnet.ibm.com>
 */

#ifndef __ASM_PPC64_HMI_H__
#define __ASM_PPC64_HMI_H__

#ifdef CONFIG_KVM_BOOK3S_HV_POSSIBLE

#define	CORE_TB_RESYNC_REQ_BIT		63
#define MAX_SUBCORE_PER_CORE		4

/*
 * sibling_subcore_state structure is used to co-ordinate all threads
 * during HMI to avoid TB corruption. This structure is allocated once
 * per each core and shared by all threads on that core.
 */
struct sibling_subcore_state {
	unsigned long	flags;
	u8		in_guest[MAX_SUBCORE_PER_CORE];
};

extern void wait_for_subcore_guest_exit(void);
extern void wait_for_tb_resync(void);
#else
static inline void wait_for_subcore_guest_exit(void) { }
static inline void wait_for_tb_resync(void) { }
#endif

struct pt_regs;
extern long hmi_handle_debugtrig(struct pt_regs *regs);

#endif /* __ASM_PPC64_HMI_H__ */
