/*
 * Copyright (c) 2003-2012 Broadcom Corporation
 * All Rights Reserved
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the Broadcom
 * license below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY BROADCOM ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL BROADCOM OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <linux/kernel.h>
#include <linux/irqreturn.h>
#include <linux/irq.h>
#include <linux/interrupt.h>

#include <asm/mipsregs.h>
#include <asm/netlogic/interrupt.h>
#include <asm/netlogic/xlr/fmn.h>
#include <asm/netlogic/common.h>

#define COP2_CC_INIT_CPU_DEST(dest, conf) \
do { \
	nlm_write_c2_cc##dest(0, conf[(dest * 8) + 0]); \
	nlm_write_c2_cc##dest(1, conf[(dest * 8) + 1]); \
	nlm_write_c2_cc##dest(2, conf[(dest * 8) + 2]); \
	nlm_write_c2_cc##dest(3, conf[(dest * 8) + 3]); \
	nlm_write_c2_cc##dest(4, conf[(dest * 8) + 4]); \
	nlm_write_c2_cc##dest(5, conf[(dest * 8) + 5]); \
	nlm_write_c2_cc##dest(6, conf[(dest * 8) + 6]); \
	nlm_write_c2_cc##dest(7, conf[(dest * 8) + 7]); \
} while (0)

struct fmn_message_handler {
	void (*action)(int, int, int, int, struct nlm_fmn_msg *, void *);
	void *arg;
} msg_handlers[128];

/*
 * FMN interrupt handler. We configure the FMN so that any messages in
 * any of the CPU buckets will trigger an interrupt on the CPU.
 * The message can be from any device on the FMN (like NAE/SAE/DMA).
 * The source station id is used to figure out which of the registered
 * handlers have to be called.
 */
static irqreturn_t fmn_message_handler(int irq, void *data)
{
	struct fmn_message_handler *hndlr;
	int bucket, rv;
	int size = 0, code = 0, src_stnid = 0;
	struct nlm_fmn_msg msg;
	uint32_t mflags, bkt_status;

	mflags = nlm_cop2_enable();
	/* Disable message ring interrupt */
	nlm_fmn_setup_intr(irq, 0);
	while (1) {
		/* 8 bkts per core, [24:31] each bit represents one bucket
		 * Bit is Zero if bucket is not empty */
		bkt_status = (nlm_read_c2_status() >> 24) & 0xff;
		if (bkt_status == 0xff)
			break;
		for (bucket = 0; bucket < 8; bucket++) {
			/* Continue on empty bucket */
			if (bkt_status & (1 << bucket))
				continue;
			rv = nlm_fmn_receive(bucket, &size, &code, &src_stnid,
						&msg);
			if (rv != 0)
				continue;

			hndlr = &msg_handlers[src_stnid];
			if (hndlr->action == NULL)
				pr_warn("No msgring handler for stnid %d\n",
						src_stnid);
			else {
				nlm_cop2_restore(mflags);
				hndlr->action(bucket, src_stnid, size, code,
					&msg, hndlr->arg);
				mflags = nlm_cop2_enable();
			}
		}
	};
	/* Enable message ring intr, to any thread in core */
	nlm_fmn_setup_intr(irq, (1 << nlm_threads_per_core) - 1);
	nlm_cop2_restore(mflags);
	return IRQ_HANDLED;
}

struct irqaction fmn_irqaction = {
	.handler = fmn_message_handler,
	.flags = IRQF_PERCPU,
	.name = "fmn",
};

void xlr_percpu_fmn_init(void)
{
	struct xlr_fmn_info *cpu_fmn_info;
	int *bucket_sizes;
	uint32_t flags;
	int id;

	BUG_ON(nlm_thread_id() != 0);
	id = nlm_core_id();

	bucket_sizes = xlr_board_fmn_config.bucket_size;
	cpu_fmn_info = &xlr_board_fmn_config.cpu[id];
	flags = nlm_cop2_enable();

	/* Setup bucket sizes for the core. */
	nlm_write_c2_bucksize(0, bucket_sizes[id * 8 + 0]);
	nlm_write_c2_bucksize(1, bucket_sizes[id * 8 + 1]);
	nlm_write_c2_bucksize(2, bucket_sizes[id * 8 + 2]);
	nlm_write_c2_bucksize(3, bucket_sizes[id * 8 + 3]);
	nlm_write_c2_bucksize(4, bucket_sizes[id * 8 + 4]);
	nlm_write_c2_bucksize(5, bucket_sizes[id * 8 + 5]);
	nlm_write_c2_bucksize(6, bucket_sizes[id * 8 + 6]);
	nlm_write_c2_bucksize(7, bucket_sizes[id * 8 + 7]);

	/*
	 * For sending FMN messages, we need credits on the destination
	 * bucket. Program the credits this core has on the 128 possible
	 * destination buckets.
	 * We cannot use a loop here, because the the first argument has
	 * to be a constant integer value.
	 */
	COP2_CC_INIT_CPU_DEST(0, cpu_fmn_info->credit_config);
	COP2_CC_INIT_CPU_DEST(1, cpu_fmn_info->credit_config);
	COP2_CC_INIT_CPU_DEST(2, cpu_fmn_info->credit_config);
	COP2_CC_INIT_CPU_DEST(3, cpu_fmn_info->credit_config);
	COP2_CC_INIT_CPU_DEST(4, cpu_fmn_info->credit_config);
	COP2_CC_INIT_CPU_DEST(5, cpu_fmn_info->credit_config);
	COP2_CC_INIT_CPU_DEST(6, cpu_fmn_info->credit_config);
	COP2_CC_INIT_CPU_DEST(7, cpu_fmn_info->credit_config);
	COP2_CC_INIT_CPU_DEST(8, cpu_fmn_info->credit_config);
	COP2_CC_INIT_CPU_DEST(9, cpu_fmn_info->credit_config);
	COP2_CC_INIT_CPU_DEST(10, cpu_fmn_info->credit_config);
	COP2_CC_INIT_CPU_DEST(11, cpu_fmn_info->credit_config);
	COP2_CC_INIT_CPU_DEST(12, cpu_fmn_info->credit_config);
	COP2_CC_INIT_CPU_DEST(13, cpu_fmn_info->credit_config);
	COP2_CC_INIT_CPU_DEST(14, cpu_fmn_info->credit_config);
	COP2_CC_INIT_CPU_DEST(15, cpu_fmn_info->credit_config);

	/* enable FMN interrupts on this CPU */
	nlm_fmn_setup_intr(IRQ_FMN, (1 << nlm_threads_per_core) - 1);
	nlm_cop2_restore(flags);
}


/*
 * Register a FMN message handler with respect to the source station id
 * @stnid: source station id
 * @action: Handler function pointer
 */
int nlm_register_fmn_handler(int start_stnid, int end_stnid,
	void (*action)(int, int, int, int, struct nlm_fmn_msg *, void *),
	void *arg)
{
	int sstnid;

	for (sstnid = start_stnid; sstnid <= end_stnid; sstnid++) {
		msg_handlers[sstnid].arg = arg;
		smp_wmb();
		msg_handlers[sstnid].action = action;
	}
	pr_debug("Registered FMN msg handler for stnid %d-%d\n",
			start_stnid, end_stnid);
	return 0;
}

void nlm_setup_fmn_irq(void)
{
	uint32_t flags;

	/* setup irq only once */
	setup_irq(IRQ_FMN, &fmn_irqaction);

	flags = nlm_cop2_enable();
	nlm_fmn_setup_intr(IRQ_FMN, (1 << nlm_threads_per_core) - 1);
	nlm_cop2_restore(flags);
}
