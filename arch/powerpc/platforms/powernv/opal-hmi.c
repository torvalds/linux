/*
 * OPAL hypervisor Maintenance interrupt handling support in PowerNV.
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

#include "powernv.h"

static int opal_hmi_handler_nb_init;
struct OpalHmiEvtNode {
	struct list_head list;
	struct OpalHMIEvent hmi_evt;
};

struct xstop_reason {
	uint32_t xstop_reason;
	const char *unit_failed;
	const char *description;
};

static LIST_HEAD(opal_hmi_evt_list);
static DEFINE_SPINLOCK(opal_hmi_evt_lock);

static void print_core_checkstop_reason(const char *level,
					struct OpalHMIEvent *hmi_evt)
{
	int i;
	static const struct xstop_reason xstop_reason[] = {
		{ CORE_CHECKSTOP_IFU_REGFILE, "IFU",
				"RegFile core check stop" },
		{ CORE_CHECKSTOP_IFU_LOGIC, "IFU", "Logic core check stop" },
		{ CORE_CHECKSTOP_PC_DURING_RECOV, "PC",
				"Core checkstop during recovery" },
		{ CORE_CHECKSTOP_ISU_REGFILE, "ISU",
				"RegFile core check stop (mapper error)" },
		{ CORE_CHECKSTOP_ISU_LOGIC, "ISU", "Logic core check stop" },
		{ CORE_CHECKSTOP_FXU_LOGIC, "FXU", "Logic core check stop" },
		{ CORE_CHECKSTOP_VSU_LOGIC, "VSU", "Logic core check stop" },
		{ CORE_CHECKSTOP_PC_RECOV_IN_MAINT_MODE, "PC",
				"Recovery in maintenance mode" },
		{ CORE_CHECKSTOP_LSU_REGFILE, "LSU",
				"RegFile core check stop" },
		{ CORE_CHECKSTOP_PC_FWD_PROGRESS, "PC",
				"Forward Progress Error" },
		{ CORE_CHECKSTOP_LSU_LOGIC, "LSU", "Logic core check stop" },
		{ CORE_CHECKSTOP_PC_LOGIC, "PC", "Logic core check stop" },
		{ CORE_CHECKSTOP_PC_HYP_RESOURCE, "PC",
				"Hypervisor Resource error - core check stop" },
		{ CORE_CHECKSTOP_PC_HANG_RECOV_FAILED, "PC",
				"Hang Recovery Failed (core check stop)" },
		{ CORE_CHECKSTOP_PC_AMBI_HANG_DETECTED, "PC",
				"Ambiguous Hang Detected (unknown source)" },
		{ CORE_CHECKSTOP_PC_DEBUG_TRIG_ERR_INJ, "PC",
				"Debug Trigger Error inject" },
		{ CORE_CHECKSTOP_PC_SPRD_HYP_ERR_INJ, "PC",
				"Hypervisor check stop via SPRC/SPRD" },
	};

	/* Validity check */
	if (!hmi_evt->u.xstop_error.xstop_reason) {
		printk("%s	Unknown Core check stop.\n", level);
		return;
	}

	printk("%s	CPU PIR: %08x\n", level,
			be32_to_cpu(hmi_evt->u.xstop_error.u.pir));
	for (i = 0; i < ARRAY_SIZE(xstop_reason); i++)
		if (be32_to_cpu(hmi_evt->u.xstop_error.xstop_reason) &
					xstop_reason[i].xstop_reason)
			printk("%s	[Unit: %-3s] %s\n", level,
					xstop_reason[i].unit_failed,
					xstop_reason[i].description);
}

static void print_nx_checkstop_reason(const char *level,
					struct OpalHMIEvent *hmi_evt)
{
	int i;
	static const struct xstop_reason xstop_reason[] = {
		{ NX_CHECKSTOP_SHM_INVAL_STATE_ERR, "DMA & Engine",
					"SHM invalid state error" },
		{ NX_CHECKSTOP_DMA_INVAL_STATE_ERR_1, "DMA & Engine",
					"DMA invalid state error bit 15" },
		{ NX_CHECKSTOP_DMA_INVAL_STATE_ERR_2, "DMA & Engine",
					"DMA invalid state error bit 16" },
		{ NX_CHECKSTOP_DMA_CH0_INVAL_STATE_ERR, "DMA & Engine",
					"Channel 0 invalid state error" },
		{ NX_CHECKSTOP_DMA_CH1_INVAL_STATE_ERR, "DMA & Engine",
					"Channel 1 invalid state error" },
		{ NX_CHECKSTOP_DMA_CH2_INVAL_STATE_ERR, "DMA & Engine",
					"Channel 2 invalid state error" },
		{ NX_CHECKSTOP_DMA_CH3_INVAL_STATE_ERR, "DMA & Engine",
					"Channel 3 invalid state error" },
		{ NX_CHECKSTOP_DMA_CH4_INVAL_STATE_ERR, "DMA & Engine",
					"Channel 4 invalid state error" },
		{ NX_CHECKSTOP_DMA_CH5_INVAL_STATE_ERR, "DMA & Engine",
					"Channel 5 invalid state error" },
		{ NX_CHECKSTOP_DMA_CH6_INVAL_STATE_ERR, "DMA & Engine",
					"Channel 6 invalid state error" },
		{ NX_CHECKSTOP_DMA_CH7_INVAL_STATE_ERR, "DMA & Engine",
					"Channel 7 invalid state error" },
		{ NX_CHECKSTOP_DMA_CRB_UE, "DMA & Engine",
					"UE error on CRB(CSB address, CCB)" },
		{ NX_CHECKSTOP_DMA_CRB_SUE, "DMA & Engine",
					"SUE error on CRB(CSB address, CCB)" },
		{ NX_CHECKSTOP_PBI_ISN_UE, "PowerBus Interface",
		"CRB Kill ISN received while holding ISN with UE error" },
	};

	/* Validity check */
	if (!hmi_evt->u.xstop_error.xstop_reason) {
		printk("%s	Unknown NX check stop.\n", level);
		return;
	}

	printk("%s	NX checkstop on CHIP ID: %x\n", level,
			be32_to_cpu(hmi_evt->u.xstop_error.u.chip_id));
	for (i = 0; i < ARRAY_SIZE(xstop_reason); i++)
		if (be32_to_cpu(hmi_evt->u.xstop_error.xstop_reason) &
					xstop_reason[i].xstop_reason)
			printk("%s	[Unit: %-3s] %s\n", level,
					xstop_reason[i].unit_failed,
					xstop_reason[i].description);
}

static void print_checkstop_reason(const char *level,
					struct OpalHMIEvent *hmi_evt)
{
	uint8_t type = hmi_evt->u.xstop_error.xstop_type;
	switch (type) {
	case CHECKSTOP_TYPE_CORE:
		print_core_checkstop_reason(level, hmi_evt);
		break;
	case CHECKSTOP_TYPE_NX:
		print_nx_checkstop_reason(level, hmi_evt);
		break;
	default:
		printk("%s	Unknown Malfunction Alert of type %d\n",
		       level, type);
		break;
	}
}

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
		"UPS (Uninterrupted Power System) Overflow indication",
		"An XSCOM operation failure",
		"An XSCOM operation completed",
		"SCOM has set a reserved FIR bit to cause recovery",
		"Debug trigger has set a reserved FIR bit to cause recovery",
		"A hypervisor resource error occurred",
		"CAPP recovery process is in progress",
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

	if (hmi_evt->version < OpalHMIEvt_V2)
		return;

	/* OpalHMIEvt_V2 and above provides reason for malfunction alert. */
	if (hmi_evt->type == OpalHMI_ERROR_MALFUNC_ALERT)
		print_checkstop_reason(level, hmi_evt);
}

static void hmi_event_handler(struct work_struct *work)
{
	unsigned long flags;
	struct OpalHMIEvent *hmi_evt;
	struct OpalHmiEvtNode *msg_node;
	uint8_t disposition;
	struct opal_msg msg;
	int unrecoverable = 0;

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
		 * then kernel can't continue, we need to panic.
		 * But before we do that, display all the HMI event
		 * available on the list and set unrecoverable flag to 1.
		 */
		if (disposition != OpalHMI_DISPOSITION_RECOVERED)
			unrecoverable = 1;

		spin_lock_irqsave(&opal_hmi_evt_lock, flags);
	}
	spin_unlock_irqrestore(&opal_hmi_evt_lock, flags);

	if (unrecoverable) {
		/* Pull all HMI events from OPAL before we panic. */
		while (opal_get_msg(__pa(&msg), sizeof(msg)) == OPAL_SUCCESS) {
			u32 type;

			type = be32_to_cpu(msg.msg_type);

			/* skip if not HMI event */
			if (type != OPAL_MSG_HMI_EVT)
				continue;

			/* HMI event info starts from param[0] */
			hmi_evt = (struct OpalHMIEvent *)&msg.params[0];
			print_hmi_event_info(hmi_evt);
		}

		pnv_platform_error_reboot(NULL, "Unrecoverable HMI exception");
	}
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
	memcpy(&msg_node->hmi_evt, hmi_evt, sizeof(*hmi_evt));

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

int __init opal_hmi_handler_init(void)
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
