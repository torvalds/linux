/* SPDX-License-Identifier: GPL-2.0 */

struct pt_regs;

int do_page_fault(struct pt_regs *regs, unsigned long address,
		  unsigned long error_code);
int send_fault_sig(struct pt_regs *regs);
