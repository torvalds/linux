/*
 * OPAL hypervisor Maintenance interrupt handling support in PowreNV.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; If not, see <http://www.gnu.org/licenses/>.
 *
 * Copyright 2014 IBM Corporation
 * Author: Mahesh Salgaonkar <mahesh@linux.vnet.ibm.com>
 */

#undef DEBUG

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/mm.h>
#include <linux/slab.h>

#include <asm/opal.h>
#include <asm/cputable.h>
#include <asm/machdep.h>

static int opal_hmi_handler_nb_init;
struct OpalHmiEvtNode {
	struct list_head list;
	struct OpalHMIEvent hmi_evt;
};
static LIST_HEAD(opal_hmi_evt_list);
static DEFINE_SPINLOCK(opal_hmi_evt_lock);

static void print_hmi_event_info(struct OpalHMIEvent *hmi_evt)
{
	const char *level, *sevstr, *error_info;
	static const char *hmi_error_types[] = {
		"Malfunction Alert",
		"Processor Recovery done",
		"Processor recovery occurred again",
		"Processor recovery occurred for masked error",
		"Timer facility experienced an error",
		"TFMR SPR is corrupted",
		"UPS (Uniterrupted Power System) Overflow indication",
		"An XSCOM operation failure",
		"An XSCOM operation completed",
		"SCOM has set a reserved FIR bit to cause recovery",
		"Debug trigger has set a reserved FIR bit to cause recovery",
		"A hypervisor resource error occurred"
	};

	/* Print things out */
	if (hmi_evt->version < OpalHMIEvt_V1) {
		pr_err("HMI Interrupt, Unknown event version %d !\n",
			hmi_evt->version);
		return;
	}
	switch (hmi_evt->severity) {
	case OpalHMI_SEV_NO_ERROR:
		level = KERN_INFO;
		sevstr = "Harmless";
		break;
	case OpalHMI_SEV_WARNING:
		level = KERN_WARNING;
		sevstr = "";
		break;
	case OpalHMI_SEV_ERROR_SYNC:
		level = KERN_ERR;
		sevstr = "Severe";
		break;
	case OpalHMI_SEV_FATAL:
	default:
		level = KERN_ERR;
		sevstr = "Fatal";
		break;
	}

	printk("%s%s Hypervisor Maintenance interrupt [%s]\n",
		level, sevstr,
		hmi_evt->disposition == OpalHMI_DISPOSITION_RECOVERED ?
		"Recovered" : "Not recovered");
	error_info = hmi_evt->type < ARRAY_SIZE(hmi_error_types) ?
			hmi_error_types[hmi_evt->type]
			: "Unknown";
	printk("%s Error detail: %s\n", level, error_info);
	printk("%s	HMER: %016llx\n", level, be64_to_cpu(hmi_evt->hmer));
	if ((hmi_evt->type == OpalHMI_ERROR_TFAC) ||
		(hmi_evt->type == OpalHMI_ERROR_TFMR_PARITY))
		printk("%s	TFMR: %016llx\n", level,
						be64_to_cpu(hmi_evt->tfmr));
}

static void hmi_event_handler(struct work_struct *work)
{
	unsigned long flags;
	struct OpalHMIEvent *hmi_evt;
	struct OpalHmiEvtNode *msg_node;
	uint8_t disposition;

	spin_lock_irqsave(&opal_hmi_evt_lock, flags);
	while (!list_empty(&opal_hmi_evt_list)) {
		msg_node = list_entry(opal_hmi_evt_list.next,
					   struct OpalHmiEvtNode, list);
		list_del(&msg_node->list);
		spin_unlock_irqrestore(&opal_hmi_evt_lock, flags);

		hmi_evt = (struct OpalHMIEvent *) &msg_node->hmi_evt;
		print_hmi_event_info(hmi_evt);
		disposition = hmi_evt->disposition;
		kfree(msg_node);

		/*
		 * Check if HMI event has been recovered or not. If not
		 * then we can't continue, invoke panic.
		 */
		if (disposition != OpalHMI_DISPOSITION_RECOVERED)
			panic("Unrecoverable HMI exception");

		spin_lock_irqsave(&opal_hmi_evt_lock, flags);
	}
	spin_unlock_irqrestore(&opal_hmi_evt_lock, flags);
}

static DECLARE_WORK(hmi_event_work, hmi_event_handler);
/*
 * opal_handle_hmi_event - notifier handler that queues up HMI events
 * to be preocessed later.
 */
static int opal_handle_hmi_event(struct notifier_block *nb,
			  unsigned long msg_type, void *msg)
{
	unsigned long flags;
	struct OpalHMIEvent *hmi_evt;
	struct opal_msg *hmi_msg = msg;
	struct OpalHmiEvtNode *msg_node;

	/* Sanity Checks */
	if (msg_type != OPAL_MSG_HMI_EVT)
		return 0;

	/* HMI event info starts from param[0] */
	hmi_evt = (struct OpalHMIEvent *)&hmi_msg->params[0];

	/* Delay the logging of HMI events to workqueue. */
	msg_node = kzalloc(sizeof(*msg_node), GFP_ATOMIC);
	if (!msg_node) {
		pr_err("HMI: out of memory, Opal message event not handled\n");
		return -ENOMEM;
	}
	memcpy(&msg_node->hmi_evt, hmi_evt, sizeof(struct OpalHMIEvent));

	spin_lock_irqsave(&opal_hmi_evt_lock, flags);
	list_add(&msg_node->list, &opal_hmi_evt_list);
	spin_unlock_irqrestore(&opal_hmi_evt_lock, flags);

	schedule_work(&hmi_event_work);
	return 0;
}

static struct notifier_block opal_hmi_handler_nb = {
	.notifier_call	= opal_handle_hmi_event,
	.next		= NULL,
	.priority	= 0,
};

static int __init opal_hmi_handler_init(void)
{
	int ret;

	if (!opal_hmi_handler_nb_init) {
		ret = opal_message_notifier_register(
				OPAL_MSG_HMI_EVT, &opal_hmi_handler_nb);
		if (ret) {
			pr_err("%s: Can't register OPAL event notifier (%d)\n",
			       __func__, ret);
			return ret;
		}
		opal_hmi_handler_nb_init = 1;
	}
	return 0;
}
machine_subsys_initcall(powernv, opal_hmi_handler_init);
