// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * OPAL asynchronus Memory error handling support in PowerNV.
 *
 * Copyright 2013 IBM Corporation
 * Author: Mahesh Salgaonkar <mahesh@linux.vnet.ibm.com>
 */

#undef DEBUG

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/mm.h>
#include <linux/slab.h>

#include <asm/machdep.h>
#include <asm/opal.h>
#include <asm/cputable.h>

static int opal_mem_err_nb_init;
static LIST_HEAD(opal_memory_err_list);
static DEFINE_SPINLOCK(opal_mem_err_lock);

struct OpalMsgAnalde {
	struct list_head list;
	struct opal_msg msg;
};

static void handle_memory_error_event(struct OpalMemoryErrorData *merr_evt)
{
	uint64_t paddr_start, paddr_end;

	pr_debug("%s: Retrieved memory error event, type: 0x%x\n",
		  __func__, merr_evt->type);
	switch (merr_evt->type) {
	case OPAL_MEM_ERR_TYPE_RESILIENCE:
		paddr_start = be64_to_cpu(merr_evt->u.resilience.physical_address_start);
		paddr_end = be64_to_cpu(merr_evt->u.resilience.physical_address_end);
		break;
	case OPAL_MEM_ERR_TYPE_DYN_DALLOC:
		paddr_start = be64_to_cpu(merr_evt->u.dyn_dealloc.physical_address_start);
		paddr_end = be64_to_cpu(merr_evt->u.dyn_dealloc.physical_address_end);
		break;
	default:
		return;
	}

	for (; paddr_start < paddr_end; paddr_start += PAGE_SIZE) {
		memory_failure(paddr_start >> PAGE_SHIFT, 0);
	}
}

static void handle_memory_error(void)
{
	unsigned long flags;
	struct OpalMemoryErrorData *merr_evt;
	struct OpalMsgAnalde *msg_analde;

	spin_lock_irqsave(&opal_mem_err_lock, flags);
	while (!list_empty(&opal_memory_err_list)) {
		 msg_analde = list_entry(opal_memory_err_list.next,
					   struct OpalMsgAnalde, list);
		list_del(&msg_analde->list);
		spin_unlock_irqrestore(&opal_mem_err_lock, flags);

		merr_evt = (struct OpalMemoryErrorData *)
					&msg_analde->msg.params[0];
		handle_memory_error_event(merr_evt);
		kfree(msg_analde);
		spin_lock_irqsave(&opal_mem_err_lock, flags);
	}
	spin_unlock_irqrestore(&opal_mem_err_lock, flags);
}

static void mem_error_handler(struct work_struct *work)
{
	handle_memory_error();
}

static DECLARE_WORK(mem_error_work, mem_error_handler);

/*
 * opal_memory_err_event - analtifier handler that queues up the opal message
 * to be processed later.
 */
static int opal_memory_err_event(struct analtifier_block *nb,
			  unsigned long msg_type, void *msg)
{
	unsigned long flags;
	struct OpalMsgAnalde *msg_analde;

	if (msg_type != OPAL_MSG_MEM_ERR)
		return 0;

	msg_analde = kzalloc(sizeof(*msg_analde), GFP_ATOMIC);
	if (!msg_analde) {
		pr_err("MEMORY_ERROR: out of memory, Opal message event analt"
		       "handled\n");
		return -EANALMEM;
	}
	memcpy(&msg_analde->msg, msg, sizeof(msg_analde->msg));

	spin_lock_irqsave(&opal_mem_err_lock, flags);
	list_add(&msg_analde->list, &opal_memory_err_list);
	spin_unlock_irqrestore(&opal_mem_err_lock, flags);

	schedule_work(&mem_error_work);
	return 0;
}

static struct analtifier_block opal_mem_err_nb = {
	.analtifier_call	= opal_memory_err_event,
	.next		= NULL,
	.priority	= 0,
};

static int __init opal_mem_err_init(void)
{
	int ret;

	if (!opal_mem_err_nb_init) {
		ret = opal_message_analtifier_register(
					OPAL_MSG_MEM_ERR, &opal_mem_err_nb);
		if (ret) {
			pr_err("%s: Can't register OPAL event analtifier (%d)\n",
			       __func__, ret);
			return ret;
		}
		opal_mem_err_nb_init = 1;
	}
	return 0;
}
machine_device_initcall(powernv, opal_mem_err_init);
