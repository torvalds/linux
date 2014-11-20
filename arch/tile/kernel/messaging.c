/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

#include <linux/percpu.h>
#include <linux/smp.h>
#include <linux/hardirq.h>
#include <linux/ptrace.h>
#include <asm/hv_driver.h>
#include <asm/irq_regs.h>
#include <asm/traps.h>
#include <hv/hypervisor.h>
#include <arch/interrupts.h>

/* All messages are stored here */
static DEFINE_PER_CPU(HV_MsgState, msg_state);

void init_messaging(void)
{
	/* Allocate storage for messages in kernel space */
	HV_MsgState *state = this_cpu_ptr(&msg_state);
	int rc = hv_register_message_state(state);
	if (rc != HV_OK)
		panic("hv_register_message_state: error %d", rc);

	/* Make sure downcall interrupts will be enabled. */
	arch_local_irq_unmask(INT_INTCTRL_K);
}

void hv_message_intr(struct pt_regs *regs, int intnum)
{
	/*
	 * We enter with interrupts disabled and leave them disabled,
	 * to match expectations of called functions (e.g.
	 * do_ccupdate_local() in mm/slab.c).  This is also consistent
	 * with normal call entry for device interrupts.
	 */

	int message[HV_MAX_MESSAGE_SIZE/sizeof(int)];
	HV_RcvMsgInfo rmi;
	int nmsgs = 0;

	/* Track time spent here in an interrupt context */
	struct pt_regs *old_regs = set_irq_regs(regs);
	irq_enter();

#ifdef CONFIG_DEBUG_STACKOVERFLOW
	/* Debugging check for stack overflow: less than 1/8th stack free? */
	{
		long sp = stack_pointer - (long) current_thread_info();
		if (unlikely(sp < (sizeof(struct thread_info) + STACK_WARN))) {
			pr_emerg("hv_message_intr: "
			       "stack overflow: %ld\n",
			       sp - sizeof(struct thread_info));
			dump_stack();
		}
	}
#endif

	while (1) {
		HV_MsgState *state = this_cpu_ptr(&msg_state);
		rmi = hv_receive_message(*state, (HV_VirtAddr) message,
					 sizeof(message));
		if (rmi.msglen == 0)
			break;

		if (rmi.msglen < 0)
			panic("hv_receive_message failed: %d", rmi.msglen);

		++nmsgs;

		if (rmi.source == HV_MSG_TILE) {
			int tag;

			/* we just send tags for now */
			BUG_ON(rmi.msglen != sizeof(int));

			tag = message[0];
#ifdef CONFIG_SMP
			evaluate_message(message[0]);
#else
			panic("Received IPI message %d in UP mode", tag);
#endif
		} else if (rmi.source == HV_MSG_INTR) {
			HV_IntrMsg *him = (HV_IntrMsg *)message;
			struct hv_driver_cb *cb =
				(struct hv_driver_cb *)him->intarg;
			cb->callback(cb, him->intdata);
			__this_cpu_inc(irq_stat.irq_hv_msg_count);
		}
	}

	/*
	 * We shouldn't have gotten a message downcall with no
	 * messages available.
	 */
	if (nmsgs == 0)
		panic("Message downcall invoked with no messages!");

	/*
	 * Track time spent against the current process again and
	 * process any softirqs if they are waiting.
	 */
	irq_exit();
	set_irq_regs(old_regs);
}
