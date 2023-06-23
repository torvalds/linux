// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2005 - 2016 Broadcom
 * All rights reserved.
 *
 * Contact Information:
 * linux-drivers@emulex.com
 *
 * Emulex
 * 3333 Susan Street
 * Costa Mesa, CA 92626
 */

#include <linux/module.h>
#include "be.h"
#include "be_cmds.h"

const char * const be_misconfig_evt_port_state[] = {
	"Physical Link is functional",
	"Optics faulted/incorrectly installed/not installed - Reseat optics. If issue not resolved, replace.",
	"Optics of two types installed – Remove one optic or install matching pair of optics.",
	"Incompatible optics – Replace with compatible optics for card to function.",
	"Unqualified optics – Replace with Avago optics for Warranty and Technical Support.",
	"Uncertified optics – Replace with Avago-certified optics to enable link operation."
};

static char *be_port_misconfig_evt_severity[] = {
	"KERN_WARN",
	"KERN_INFO",
	"KERN_ERR",
	"KERN_WARN"
};

static char *phy_state_oper_desc[] = {
	"Link is non-operational",
	"Link is operational",
	""
};

static struct be_cmd_priv_map cmd_priv_map[] = {
	{
		OPCODE_ETH_ACPI_WOL_MAGIC_CONFIG,
		CMD_SUBSYSTEM_ETH,
		BE_PRIV_LNKMGMT | BE_PRIV_VHADM |
		BE_PRIV_DEVCFG | BE_PRIV_DEVSEC
	},
	{
		OPCODE_COMMON_GET_FLOW_CONTROL,
		CMD_SUBSYSTEM_COMMON,
		BE_PRIV_LNKQUERY | BE_PRIV_VHADM |
		BE_PRIV_DEVCFG | BE_PRIV_DEVSEC
	},
	{
		OPCODE_COMMON_SET_FLOW_CONTROL,
		CMD_SUBSYSTEM_COMMON,
		BE_PRIV_LNKMGMT | BE_PRIV_VHADM |
		BE_PRIV_DEVCFG | BE_PRIV_DEVSEC
	},
	{
		OPCODE_ETH_GET_PPORT_STATS,
		CMD_SUBSYSTEM_ETH,
		BE_PRIV_LNKMGMT | BE_PRIV_VHADM |
		BE_PRIV_DEVCFG | BE_PRIV_DEVSEC
	},
	{
		OPCODE_COMMON_GET_PHY_DETAILS,
		CMD_SUBSYSTEM_COMMON,
		BE_PRIV_LNKMGMT | BE_PRIV_VHADM |
		BE_PRIV_DEVCFG | BE_PRIV_DEVSEC
	},
	{
		OPCODE_LOWLEVEL_HOST_DDR_DMA,
		CMD_SUBSYSTEM_LOWLEVEL,
		BE_PRIV_DEVCFG | BE_PRIV_DEVSEC
	},
	{
		OPCODE_LOWLEVEL_LOOPBACK_TEST,
		CMD_SUBSYSTEM_LOWLEVEL,
		BE_PRIV_DEVCFG | BE_PRIV_DEVSEC
	},
	{
		OPCODE_LOWLEVEL_SET_LOOPBACK_MODE,
		CMD_SUBSYSTEM_LOWLEVEL,
		BE_PRIV_DEVCFG | BE_PRIV_DEVSEC
	},
	{
		OPCODE_COMMON_SET_HSW_CONFIG,
		CMD_SUBSYSTEM_COMMON,
		BE_PRIV_DEVCFG | BE_PRIV_VHADM |
		BE_PRIV_DEVSEC
	},
	{
		OPCODE_COMMON_GET_EXT_FAT_CAPABILITIES,
		CMD_SUBSYSTEM_COMMON,
		BE_PRIV_DEVCFG
	}
};

static bool be_cmd_allowed(struct be_adapter *adapter, u8 opcode, u8 subsystem)
{
	int i;
	int num_entries = ARRAY_SIZE(cmd_priv_map);
	u32 cmd_privileges = adapter->cmd_privileges;

	for (i = 0; i < num_entries; i++)
		if (opcode == cmd_priv_map[i].opcode &&
		    subsystem == cmd_priv_map[i].subsystem)
			if (!(cmd_privileges & cmd_priv_map[i].priv_mask))
				return false;

	return true;
}

static inline void *embedded_payload(struct be_mcc_wrb *wrb)
{
	return wrb->payload.embedded_payload;
}

static int be_mcc_notify(struct be_adapter *adapter)
{
	struct be_queue_info *mccq = &adapter->mcc_obj.q;
	u32 val = 0;

	if (be_check_error(adapter, BE_ERROR_ANY))
		return -EIO;

	val |= mccq->id & DB_MCCQ_RING_ID_MASK;
	val |= 1 << DB_MCCQ_NUM_POSTED_SHIFT;

	wmb();
	iowrite32(val, adapter->db + DB_MCCQ_OFFSET);

	return 0;
}

/* To check if valid bit is set, check the entire word as we don't know
 * the endianness of the data (old entry is host endian while a new entry is
 * little endian) */
static inline bool be_mcc_compl_is_new(struct be_mcc_compl *compl)
{
	u32 flags;

	if (compl->flags != 0) {
		flags = le32_to_cpu(compl->flags);
		if (flags & CQE_FLAGS_VALID_MASK) {
			compl->flags = flags;
			return true;
		}
	}
	return false;
}

/* Need to reset the entire word that houses the valid bit */
static inline void be_mcc_compl_use(struct be_mcc_compl *compl)
{
	compl->flags = 0;
}

static struct be_cmd_resp_hdr *be_decode_resp_hdr(u32 tag0, u32 tag1)
{
	unsigned long addr;

	addr = tag1;
	addr = ((addr << 16) << 16) | tag0;
	return (void *)addr;
}

static bool be_skip_err_log(u8 opcode, u16 base_status, u16 addl_status)
{
	if (base_status == MCC_STATUS_NOT_SUPPORTED ||
	    base_status == MCC_STATUS_ILLEGAL_REQUEST ||
	    addl_status == MCC_ADDL_STATUS_TOO_MANY_INTERFACES ||
	    addl_status == MCC_ADDL_STATUS_INSUFFICIENT_VLANS ||
	    (opcode == OPCODE_COMMON_WRITE_FLASHROM &&
	    (base_status == MCC_STATUS_ILLEGAL_FIELD ||
	     addl_status == MCC_ADDL_STATUS_FLASH_IMAGE_CRC_MISMATCH)))
		return true;
	else
		return false;
}

/* Place holder for all the async MCC cmds wherein the caller is not in a busy
 * loop (has not issued be_mcc_notify_wait())
 */
static void be_async_cmd_process(struct be_adapter *adapter,
				 struct be_mcc_compl *compl,
				 struct be_cmd_resp_hdr *resp_hdr)
{
	enum mcc_base_status base_status = base_status(compl->status);
	u8 opcode = 0, subsystem = 0;

	if (resp_hdr) {
		opcode = resp_hdr->opcode;
		subsystem = resp_hdr->subsystem;
	}

	if (opcode == OPCODE_LOWLEVEL_LOOPBACK_TEST &&
	    subsystem == CMD_SUBSYSTEM_LOWLEVEL) {
		complete(&adapter->et_cmd_compl);
		return;
	}

	if (opcode == OPCODE_LOWLEVEL_SET_LOOPBACK_MODE &&
	    subsystem == CMD_SUBSYSTEM_LOWLEVEL) {
		complete(&adapter->et_cmd_compl);
		return;
	}

	if ((opcode == OPCODE_COMMON_WRITE_FLASHROM ||
	     opcode == OPCODE_COMMON_WRITE_OBJECT) &&
	    subsystem == CMD_SUBSYSTEM_COMMON) {
		adapter->flash_status = compl->status;
		complete(&adapter->et_cmd_compl);
		return;
	}

	if ((opcode == OPCODE_ETH_GET_STATISTICS ||
	     opcode == OPCODE_ETH_GET_PPORT_STATS) &&
	    subsystem == CMD_SUBSYSTEM_ETH &&
	    base_status == MCC_STATUS_SUCCESS) {
		be_parse_stats(adapter);
		adapter->stats_cmd_sent = false;
		return;
	}

	if (opcode == OPCODE_COMMON_GET_CNTL_ADDITIONAL_ATTRIBUTES &&
	    subsystem == CMD_SUBSYSTEM_COMMON) {
		if (base_status == MCC_STATUS_SUCCESS) {
			struct be_cmd_resp_get_cntl_addnl_attribs *resp =
							(void *)resp_hdr;
			adapter->hwmon_info.be_on_die_temp =
						resp->on_die_temperature;
		} else {
			adapter->be_get_temp_freq = 0;
			adapter->hwmon_info.be_on_die_temp =
						BE_INVALID_DIE_TEMP;
		}
		return;
	}
}

static int be_mcc_compl_process(struct be_adapter *adapter,
				struct be_mcc_compl *compl)
{
	enum mcc_base_status base_status;
	enum mcc_addl_status addl_status;
	struct be_cmd_resp_hdr *resp_hdr;
	u8 opcode = 0, subsystem = 0;

	/* Just swap the status to host endian; mcc tag is opaquely copied
	 * from mcc_wrb */
	be_dws_le_to_cpu(compl, 4);

	base_status = base_status(compl->status);
	addl_status = addl_status(compl->status);

	resp_hdr = be_decode_resp_hdr(compl->tag0, compl->tag1);
	if (resp_hdr) {
		opcode = resp_hdr->opcode;
		subsystem = resp_hdr->subsystem;
	}

	be_async_cmd_process(adapter, compl, resp_hdr);

	if (base_status != MCC_STATUS_SUCCESS &&
	    !be_skip_err_log(opcode, base_status, addl_status)) {
		if (base_status == MCC_STATUS_UNAUTHORIZED_REQUEST ||
		    addl_status == MCC_ADDL_STATUS_INSUFFICIENT_PRIVILEGES) {
			dev_warn(&adapter->pdev->dev,
				 "VF is not privileged to issue opcode %d-%d\n",
				 opcode, subsystem);
		} else {
			dev_err(&adapter->pdev->dev,
				"opcode %d-%d failed:status %d-%d\n",
				opcode, subsystem, base_status, addl_status);
		}
	}
	return compl->status;
}

/* Link state evt is a string of bytes; no need for endian swapping */
static void be_async_link_state_process(struct be_adapter *adapter,
					struct be_mcc_compl *compl)
{
	struct be_async_event_link_state *evt =
			(struct be_async_event_link_state *)compl;

	/* When link status changes, link speed must be re-queried from FW */
	adapter->phy.link_speed = -1;

	/* On BEx the FW does not send a separate link status
	 * notification for physical and logical link.
	 * On other chips just process the logical link
	 * status notification
	 */
	if (!BEx_chip(adapter) &&
	    !(evt->port_link_status & LOGICAL_LINK_STATUS_MASK))
		return;

	/* For the initial link status do not rely on the ASYNC event as
	 * it may not be received in some cases.
	 */
	if (adapter->flags & BE_FLAGS_LINK_STATUS_INIT)
		be_link_status_update(adapter,
				      evt->port_link_status & LINK_STATUS_MASK);
}

static void be_async_port_misconfig_event_process(struct be_adapter *adapter,
						  struct be_mcc_compl *compl)
{
	struct be_async_event_misconfig_port *evt =
			(struct be_async_event_misconfig_port *)compl;
	u32 sfp_misconfig_evt_word1 = le32_to_cpu(evt->event_data_word1);
	u32 sfp_misconfig_evt_word2 = le32_to_cpu(evt->event_data_word2);
	u8 phy_oper_state = PHY_STATE_OPER_MSG_NONE;
	struct device *dev = &adapter->pdev->dev;
	u8 msg_severity = DEFAULT_MSG_SEVERITY;
	u8 phy_state_info;
	u8 new_phy_state;

	new_phy_state =
		(sfp_misconfig_evt_word1 >> (adapter->hba_port_num * 8)) & 0xff;

	if (new_phy_state == adapter->phy_state)
		return;

	adapter->phy_state = new_phy_state;

	/* for older fw that doesn't populate link effect data */
	if (!sfp_misconfig_evt_word2)
		goto log_message;

	phy_state_info =
		(sfp_misconfig_evt_word2 >> (adapter->hba_port_num * 8)) & 0xff;

	if (phy_state_info & PHY_STATE_INFO_VALID) {
		msg_severity = (phy_state_info & PHY_STATE_MSG_SEVERITY) >> 1;

		if (be_phy_unqualified(new_phy_state))
			phy_oper_state = (phy_state_info & PHY_STATE_OPER);
	}

log_message:
	/* Log an error message that would allow a user to determine
	 * whether the SFPs have an issue
	 */
	if (be_phy_state_unknown(new_phy_state))
		dev_printk(be_port_misconfig_evt_severity[msg_severity], dev,
			   "Port %c: Unrecognized Optics state: 0x%x. %s",
			   adapter->port_name,
			   new_phy_state,
			   phy_state_oper_desc[phy_oper_state]);
	else
		dev_printk(be_port_misconfig_evt_severity[msg_severity], dev,
			   "Port %c: %s %s",
			   adapter->port_name,
			   be_misconfig_evt_port_state[new_phy_state],
			   phy_state_oper_desc[phy_oper_state]);

	/* Log Vendor name and part no. if a misconfigured SFP is detected */
	if (be_phy_misconfigured(new_phy_state))
		adapter->flags |= BE_FLAGS_PHY_MISCONFIGURED;
}

/* Grp5 CoS Priority evt */
static void be_async_grp5_cos_priority_process(struct be_adapter *adapter,
					       struct be_mcc_compl *compl)
{
	struct be_async_event_grp5_cos_priority *evt =
			(struct be_async_event_grp5_cos_priority *)compl;

	if (evt->valid) {
		adapter->vlan_prio_bmap = evt->available_priority_bmap;
		adapter->recommended_prio_bits =
			evt->reco_default_priority << VLAN_PRIO_SHIFT;
	}
}

/* Grp5 QOS Speed evt: qos_link_speed is in units of 10 Mbps */
static void be_async_grp5_qos_speed_process(struct be_adapter *adapter,
					    struct be_mcc_compl *compl)
{
	struct be_async_event_grp5_qos_link_speed *evt =
			(struct be_async_event_grp5_qos_link_speed *)compl;

	if (adapter->phy.link_speed >= 0 &&
	    evt->physical_port == adapter->port_num)
		adapter->phy.link_speed = le16_to_cpu(evt->qos_link_speed) * 10;
}

/*Grp5 PVID evt*/
static void be_async_grp5_pvid_state_process(struct be_adapter *adapter,
					     struct be_mcc_compl *compl)
{
	struct be_async_event_grp5_pvid_state *evt =
			(struct be_async_event_grp5_pvid_state *)compl;

	if (evt->enabled) {
		adapter->pvid = le16_to_cpu(evt->tag) & VLAN_VID_MASK;
		dev_info(&adapter->pdev->dev, "LPVID: %d\n", adapter->pvid);
	} else {
		adapter->pvid = 0;
	}
}

#define MGMT_ENABLE_MASK	0x4
static void be_async_grp5_fw_control_process(struct be_adapter *adapter,
					     struct be_mcc_compl *compl)
{
	struct be_async_fw_control *evt = (struct be_async_fw_control *)compl;
	u32 evt_dw1 = le32_to_cpu(evt->event_data_word1);

	if (evt_dw1 & MGMT_ENABLE_MASK) {
		adapter->flags |= BE_FLAGS_OS2BMC;
		adapter->bmc_filt_mask = le32_to_cpu(evt->event_data_word2);
	} else {
		adapter->flags &= ~BE_FLAGS_OS2BMC;
	}
}

static void be_async_grp5_evt_process(struct be_adapter *adapter,
				      struct be_mcc_compl *compl)
{
	u8 event_type = (compl->flags >> ASYNC_EVENT_TYPE_SHIFT) &
				ASYNC_EVENT_TYPE_MASK;

	switch (event_type) {
	case ASYNC_EVENT_COS_PRIORITY:
		be_async_grp5_cos_priority_process(adapter, compl);
		break;
	case ASYNC_EVENT_QOS_SPEED:
		be_async_grp5_qos_speed_process(adapter, compl);
		break;
	case ASYNC_EVENT_PVID_STATE:
		be_async_grp5_pvid_state_process(adapter, compl);
		break;
	/* Async event to disable/enable os2bmc and/or mac-learning */
	case ASYNC_EVENT_FW_CONTROL:
		be_async_grp5_fw_control_process(adapter, compl);
		break;
	default:
		break;
	}
}

static void be_async_dbg_evt_process(struct be_adapter *adapter,
				     struct be_mcc_compl *cmp)
{
	u8 event_type = 0;
	struct be_async_event_qnq *evt = (struct be_async_event_qnq *)cmp;

	event_type = (cmp->flags >> ASYNC_EVENT_TYPE_SHIFT) &
			ASYNC_EVENT_TYPE_MASK;

	switch (event_type) {
	case ASYNC_DEBUG_EVENT_TYPE_QNQ:
		if (evt->valid)
			adapter->qnq_vid = le16_to_cpu(evt->vlan_tag);
		adapter->flags |= BE_FLAGS_QNQ_ASYNC_EVT_RCVD;
	break;
	default:
		dev_warn(&adapter->pdev->dev, "Unknown debug event 0x%x!\n",
			 event_type);
	break;
	}
}

static void be_async_sliport_evt_process(struct be_adapter *adapter,
					 struct be_mcc_compl *cmp)
{
	u8 event_type = (cmp->flags >> ASYNC_EVENT_TYPE_SHIFT) &
			ASYNC_EVENT_TYPE_MASK;

	if (event_type == ASYNC_EVENT_PORT_MISCONFIG)
		be_async_port_misconfig_event_process(adapter, cmp);
}

static inline bool is_link_state_evt(u32 flags)
{
	return ((flags >> ASYNC_EVENT_CODE_SHIFT) & ASYNC_EVENT_CODE_MASK) ==
			ASYNC_EVENT_CODE_LINK_STATE;
}

static inline bool is_grp5_evt(u32 flags)
{
	return ((flags >> ASYNC_EVENT_CODE_SHIFT) & ASYNC_EVENT_CODE_MASK) ==
			ASYNC_EVENT_CODE_GRP_5;
}

static inline bool is_dbg_evt(u32 flags)
{
	return ((flags >> ASYNC_EVENT_CODE_SHIFT) & ASYNC_EVENT_CODE_MASK) ==
			ASYNC_EVENT_CODE_QNQ;
}

static inline bool is_sliport_evt(u32 flags)
{
	return ((flags >> ASYNC_EVENT_CODE_SHIFT) & ASYNC_EVENT_CODE_MASK) ==
		ASYNC_EVENT_CODE_SLIPORT;
}

static void be_mcc_event_process(struct be_adapter *adapter,
				 struct be_mcc_compl *compl)
{
	if (is_link_state_evt(compl->flags))
		be_async_link_state_process(adapter, compl);
	else if (is_grp5_evt(compl->flags))
		be_async_grp5_evt_process(adapter, compl);
	else if (is_dbg_evt(compl->flags))
		be_async_dbg_evt_process(adapter, compl);
	else if (is_sliport_evt(compl->flags))
		be_async_sliport_evt_process(adapter, compl);
}

static struct be_mcc_compl *be_mcc_compl_get(struct be_adapter *adapter)
{
	struct be_queue_info *mcc_cq = &adapter->mcc_obj.cq;
	struct be_mcc_compl *compl = queue_tail_node(mcc_cq);

	if (be_mcc_compl_is_new(compl)) {
		queue_tail_inc(mcc_cq);
		return compl;
	}
	return NULL;
}

void be_async_mcc_enable(struct be_adapter *adapter)
{
	spin_lock_bh(&adapter->mcc_cq_lock);

	be_cq_notify(adapter, adapter->mcc_obj.cq.id, true, 0);
	adapter->mcc_obj.rearm_cq = true;

	spin_unlock_bh(&adapter->mcc_cq_lock);
}

void be_async_mcc_disable(struct be_adapter *adapter)
{
	spin_lock_bh(&adapter->mcc_cq_lock);

	adapter->mcc_obj.rearm_cq = false;
	be_cq_notify(adapter, adapter->mcc_obj.cq.id, false, 0);

	spin_unlock_bh(&adapter->mcc_cq_lock);
}

int be_process_mcc(struct be_adapter *adapter)
{
	struct be_mcc_compl *compl;
	int num = 0, status = 0;
	struct be_mcc_obj *mcc_obj = &adapter->mcc_obj;

	spin_lock(&adapter->mcc_cq_lock);

	while ((compl = be_mcc_compl_get(adapter))) {
		if (compl->flags & CQE_FLAGS_ASYNC_MASK) {
			be_mcc_event_process(adapter, compl);
		} else if (compl->flags & CQE_FLAGS_COMPLETED_MASK) {
			status = be_mcc_compl_process(adapter, compl);
			atomic_dec(&mcc_obj->q.used);
		}
		be_mcc_compl_use(compl);
		num++;
	}

	if (num)
		be_cq_notify(adapter, mcc_obj->cq.id, mcc_obj->rearm_cq, num);

	spin_unlock(&adapter->mcc_cq_lock);
	return status;
}

/* Wait till no more pending mcc requests are present */
static int be_mcc_wait_compl(struct be_adapter *adapter)
{
#define mcc_timeout		12000 /* 12s timeout */
	int i, status = 0;
	struct be_mcc_obj *mcc_obj = &adapter->mcc_obj;

	for (i = 0; i < mcc_timeout; i++) {
		if (be_check_error(adapter, BE_ERROR_ANY))
			return -EIO;

		local_bh_disable();
		status = be_process_mcc(adapter);
		local_bh_enable();

		if (atomic_read(&mcc_obj->q.used) == 0)
			break;
		usleep_range(500, 1000);
	}
	if (i == mcc_timeout) {
		dev_err(&adapter->pdev->dev, "FW not responding\n");
		be_set_error(adapter, BE_ERROR_FW);
		return -EIO;
	}
	return status;
}

/* Notify MCC requests and wait for completion */
static int be_mcc_notify_wait(struct be_adapter *adapter)
{
	int status;
	struct be_mcc_wrb *wrb;
	struct be_mcc_obj *mcc_obj = &adapter->mcc_obj;
	u32 index = mcc_obj->q.head;
	struct be_cmd_resp_hdr *resp;

	index_dec(&index, mcc_obj->q.len);
	wrb = queue_index_node(&mcc_obj->q, index);

	resp = be_decode_resp_hdr(wrb->tag0, wrb->tag1);

	status = be_mcc_notify(adapter);
	if (status)
		goto out;

	status = be_mcc_wait_compl(adapter);
	if (status == -EIO)
		goto out;

	status = (resp->base_status |
		  ((resp->addl_status & CQE_ADDL_STATUS_MASK) <<
		   CQE_ADDL_STATUS_SHIFT));
out:
	return status;
}

static int be_mbox_db_ready_wait(struct be_adapter *adapter, void __iomem *db)
{
	int msecs = 0;
	u32 ready;

	do {
		if (be_check_error(adapter, BE_ERROR_ANY))
			return -EIO;

		ready = ioread32(db);
		if (ready == 0xffffffff)
			return -1;

		ready &= MPU_MAILBOX_DB_RDY_MASK;
		if (ready)
			break;

		if (msecs > 4000) {
			dev_err(&adapter->pdev->dev, "FW not responding\n");
			be_set_error(adapter, BE_ERROR_FW);
			be_detect_error(adapter);
			return -1;
		}

		msleep(1);
		msecs++;
	} while (true);

	return 0;
}

/*
 * Insert the mailbox address into the doorbell in two steps
 * Polls on the mbox doorbell till a command completion (or a timeout) occurs
 */
static int be_mbox_notify_wait(struct be_adapter *adapter)
{
	int status;
	u32 val = 0;
	void __iomem *db = adapter->db + MPU_MAILBOX_DB_OFFSET;
	struct be_dma_mem *mbox_mem = &adapter->mbox_mem;
	struct be_mcc_mailbox *mbox = mbox_mem->va;
	struct be_mcc_compl *compl = &mbox->compl;

	/* wait for ready to be set */
	status = be_mbox_db_ready_wait(adapter, db);
	if (status != 0)
		return status;

	val |= MPU_MAILBOX_DB_HI_MASK;
	/* at bits 2 - 31 place mbox dma addr msb bits 34 - 63 */
	val |= (upper_32_bits(mbox_mem->dma) >> 2) << 2;
	iowrite32(val, db);

	/* wait for ready to be set */
	status = be_mbox_db_ready_wait(adapter, db);
	if (status != 0)
		return status;

	val = 0;
	/* at bits 2 - 31 place mbox dma addr lsb bits 4 - 33 */
	val |= (u32)(mbox_mem->dma >> 4) << 2;
	iowrite32(val, db);

	status = be_mbox_db_ready_wait(adapter, db);
	if (status != 0)
		return status;

	/* A cq entry has been made now */
	if (be_mcc_compl_is_new(compl)) {
		status = be_mcc_compl_process(adapter, &mbox->compl);
		be_mcc_compl_use(compl);
		if (status)
			return status;
	} else {
		dev_err(&adapter->pdev->dev, "invalid mailbox completion\n");
		return -1;
	}
	return 0;
}

u16 be_POST_stage_get(struct be_adapter *adapter)
{
	u32 sem;

	if (BEx_chip(adapter))
		sem  = ioread32(adapter->csr + SLIPORT_SEMAPHORE_OFFSET_BEx);
	else
		pci_read_config_dword(adapter->pdev,
				      SLIPORT_SEMAPHORE_OFFSET_SH, &sem);

	return sem & POST_STAGE_MASK;
}

static int lancer_wait_ready(struct be_adapter *adapter)
{
#define SLIPORT_READY_TIMEOUT 30
	u32 sliport_status;
	int i;

	for (i = 0; i < SLIPORT_READY_TIMEOUT; i++) {
		sliport_status = ioread32(adapter->db + SLIPORT_STATUS_OFFSET);
		if (sliport_status & SLIPORT_STATUS_RDY_MASK)
			return 0;

		if (sliport_status & SLIPORT_STATUS_ERR_MASK &&
		    !(sliport_status & SLIPORT_STATUS_RN_MASK))
			return -EIO;

		msleep(1000);
	}

	return sliport_status ? : -1;
}

int be_fw_wait_ready(struct be_adapter *adapter)
{
	u16 stage;
	int status, timeout = 0;
	struct device *dev = &adapter->pdev->dev;

	if (lancer_chip(adapter)) {
		status = lancer_wait_ready(adapter);
		if (status) {
			stage = status;
			goto err;
		}
		return 0;
	}

	do {
		/* There's no means to poll POST state on BE2/3 VFs */
		if (BEx_chip(adapter) && be_virtfn(adapter))
			return 0;

		stage = be_POST_stage_get(adapter);
		if (stage == POST_STAGE_ARMFW_RDY)
			return 0;

		dev_info(dev, "Waiting for POST, %ds elapsed\n", timeout);
		if (msleep_interruptible(2000)) {
			dev_err(dev, "Waiting for POST aborted\n");
			return -EINTR;
		}
		timeout += 2;
	} while (timeout < 60);

err:
	dev_err(dev, "POST timeout; stage=%#x\n", stage);
	return -ETIMEDOUT;
}

static inline struct be_sge *nonembedded_sgl(struct be_mcc_wrb *wrb)
{
	return &wrb->payload.sgl[0];
}

static inline void fill_wrb_tags(struct be_mcc_wrb *wrb, unsigned long addr)
{
	wrb->tag0 = addr & 0xFFFFFFFF;
	wrb->tag1 = upper_32_bits(addr);
}

/* Don't touch the hdr after it's prepared */
/* mem will be NULL for embedded commands */
static void be_wrb_cmd_hdr_prepare(struct be_cmd_req_hdr *req_hdr,
				   u8 subsystem, u8 opcode, int cmd_len,
				   struct be_mcc_wrb *wrb,
				   struct be_dma_mem *mem)
{
	struct be_sge *sge;

	req_hdr->opcode = opcode;
	req_hdr->subsystem = subsystem;
	req_hdr->request_length = cpu_to_le32(cmd_len - sizeof(*req_hdr));
	req_hdr->version = 0;
	fill_wrb_tags(wrb, (ulong) req_hdr);
	wrb->payload_length = cmd_len;
	if (mem) {
		wrb->embedded |= (1 & MCC_WRB_SGE_CNT_MASK) <<
			MCC_WRB_SGE_CNT_SHIFT;
		sge = nonembedded_sgl(wrb);
		sge->pa_hi = cpu_to_le32(upper_32_bits(mem->dma));
		sge->pa_lo = cpu_to_le32(mem->dma & 0xFFFFFFFF);
		sge->len = cpu_to_le32(mem->size);
	} else
		wrb->embedded |= MCC_WRB_EMBEDDED_MASK;
	be_dws_cpu_to_le(wrb, 8);
}

static void be_cmd_page_addrs_prepare(struct phys_addr *pages, u32 max_pages,
				      struct be_dma_mem *mem)
{
	int i, buf_pages = min(PAGES_4K_SPANNED(mem->va, mem->size), max_pages);
	u64 dma = (u64)mem->dma;

	for (i = 0; i < buf_pages; i++) {
		pages[i].lo = cpu_to_le32(dma & 0xFFFFFFFF);
		pages[i].hi = cpu_to_le32(upper_32_bits(dma));
		dma += PAGE_SIZE_4K;
	}
}

static inline struct be_mcc_wrb *wrb_from_mbox(struct be_adapter *adapter)
{
	struct be_dma_mem *mbox_mem = &adapter->mbox_mem;
	struct be_mcc_wrb *wrb
		= &((struct be_mcc_mailbox *)(mbox_mem->va))->wrb;
	memset(wrb, 0, sizeof(*wrb));
	return wrb;
}

static struct be_mcc_wrb *wrb_from_mccq(struct be_adapter *adapter)
{
	struct be_queue_info *mccq = &adapter->mcc_obj.q;
	struct be_mcc_wrb *wrb;

	if (!mccq->created)
		return NULL;

	if (atomic_read(&mccq->used) >= mccq->len)
		return NULL;

	wrb = queue_head_node(mccq);
	queue_head_inc(mccq);
	atomic_inc(&mccq->used);
	memset(wrb, 0, sizeof(*wrb));
	return wrb;
}

static bool use_mcc(struct be_adapter *adapter)
{
	return adapter->mcc_obj.q.created;
}

/* Must be used only in process context */
static int be_cmd_lock(struct be_adapter *adapter)
{
	if (use_mcc(adapter)) {
		mutex_lock(&adapter->mcc_lock);
		return 0;
	} else {
		return mutex_lock_interruptible(&adapter->mbox_lock);
	}
}

/* Must be used only in process context */
static void be_cmd_unlock(struct be_adapter *adapter)
{
	if (use_mcc(adapter))
		return mutex_unlock(&adapter->mcc_lock);
	else
		return mutex_unlock(&adapter->mbox_lock);
}

static struct be_mcc_wrb *be_cmd_copy(struct be_adapter *adapter,
				      struct be_mcc_wrb *wrb)
{
	struct be_mcc_wrb *dest_wrb;

	if (use_mcc(adapter)) {
		dest_wrb = wrb_from_mccq(adapter);
		if (!dest_wrb)
			return NULL;
	} else {
		dest_wrb = wrb_from_mbox(adapter);
	}

	memcpy(dest_wrb, wrb, sizeof(*wrb));
	if (wrb->embedded & cpu_to_le32(MCC_WRB_EMBEDDED_MASK))
		fill_wrb_tags(dest_wrb, (ulong) embedded_payload(wrb));

	return dest_wrb;
}

/* Must be used only in process context */
static int be_cmd_notify_wait(struct be_adapter *adapter,
			      struct be_mcc_wrb *wrb)
{
	struct be_mcc_wrb *dest_wrb;
	int status;

	status = be_cmd_lock(adapter);
	if (status)
		return status;

	dest_wrb = be_cmd_copy(adapter, wrb);
	if (!dest_wrb) {
		status = -EBUSY;
		goto unlock;
	}

	if (use_mcc(adapter))
		status = be_mcc_notify_wait(adapter);
	else
		status = be_mbox_notify_wait(adapter);

	if (!status)
		memcpy(wrb, dest_wrb, sizeof(*wrb));

unlock:
	be_cmd_unlock(adapter);
	return status;
}

/* Tell fw we're about to start firing cmds by writing a
 * special pattern across the wrb hdr; uses mbox
 */
int be_cmd_fw_init(struct be_adapter *adapter)
{
	u8 *wrb;
	int status;

	if (lancer_chip(adapter))
		return 0;

	if (mutex_lock_interruptible(&adapter->mbox_lock))
		return -1;

	wrb = (u8 *)wrb_from_mbox(adapter);
	*wrb++ = 0xFF;
	*wrb++ = 0x12;
	*wrb++ = 0x34;
	*wrb++ = 0xFF;
	*wrb++ = 0xFF;
	*wrb++ = 0x56;
	*wrb++ = 0x78;
	*wrb = 0xFF;

	status = be_mbox_notify_wait(adapter);

	mutex_unlock(&adapter->mbox_lock);
	return status;
}

/* Tell fw we're done with firing cmds by writing a
 * special pattern across the wrb hdr; uses mbox
 */
int be_cmd_fw_clean(struct be_adapter *adapter)
{
	u8 *wrb;
	int status;

	if (lancer_chip(adapter))
		return 0;

	if (mutex_lock_interruptible(&adapter->mbox_lock))
		return -1;

	wrb = (u8 *)wrb_from_mbox(adapter);
	*wrb++ = 0xFF;
	*wrb++ = 0xAA;
	*wrb++ = 0xBB;
	*wrb++ = 0xFF;
	*wrb++ = 0xFF;
	*wrb++ = 0xCC;
	*wrb++ = 0xDD;
	*wrb = 0xFF;

	status = be_mbox_notify_wait(adapter);

	mutex_unlock(&adapter->mbox_lock);
	return status;
}

int be_cmd_eq_create(struct be_adapter *adapter, struct be_eq_obj *eqo)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_eq_create *req;
	struct be_dma_mem *q_mem = &eqo->q.dma_mem;
	int status, ver = 0;

	if (mutex_lock_interruptible(&adapter->mbox_lock))
		return -1;

	wrb = wrb_from_mbox(adapter);
	req = embedded_payload(wrb);

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
			       OPCODE_COMMON_EQ_CREATE, sizeof(*req), wrb,
			       NULL);

	/* Support for EQ_CREATEv2 available only SH-R onwards */
	if (!(BEx_chip(adapter) || lancer_chip(adapter)))
		ver = 2;

	req->hdr.version = ver;
	req->num_pages =  cpu_to_le16(PAGES_4K_SPANNED(q_mem->va, q_mem->size));

	AMAP_SET_BITS(struct amap_eq_context, valid, req->context, 1);
	/* 4byte eqe*/
	AMAP_SET_BITS(struct amap_eq_context, size, req->context, 0);
	AMAP_SET_BITS(struct amap_eq_context, count, req->context,
		      __ilog2_u32(eqo->q.len / 256));
	be_dws_cpu_to_le(req->context, sizeof(req->context));

	be_cmd_page_addrs_prepare(req->pages, ARRAY_SIZE(req->pages), q_mem);

	status = be_mbox_notify_wait(adapter);
	if (!status) {
		struct be_cmd_resp_eq_create *resp = embedded_payload(wrb);

		eqo->q.id = le16_to_cpu(resp->eq_id);
		eqo->msix_idx =
			(ver == 2) ? le16_to_cpu(resp->msix_idx) : eqo->idx;
		eqo->q.created = true;
	}

	mutex_unlock(&adapter->mbox_lock);
	return status;
}

/* Use MCC */
int be_cmd_mac_addr_query(struct be_adapter *adapter, u8 *mac_addr,
			  bool permanent, u32 if_handle, u32 pmac_id)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_mac_query *req;
	int status;

	mutex_lock(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}
	req = embedded_payload(wrb);

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
			       OPCODE_COMMON_NTWK_MAC_QUERY, sizeof(*req), wrb,
			       NULL);
	req->type = MAC_ADDRESS_TYPE_NETWORK;
	if (permanent) {
		req->permanent = 1;
	} else {
		req->if_id = cpu_to_le16((u16)if_handle);
		req->pmac_id = cpu_to_le32(pmac_id);
		req->permanent = 0;
	}

	status = be_mcc_notify_wait(adapter);
	if (!status) {
		struct be_cmd_resp_mac_query *resp = embedded_payload(wrb);

		memcpy(mac_addr, resp->mac.addr, ETH_ALEN);
	}

err:
	mutex_unlock(&adapter->mcc_lock);
	return status;
}

/* Uses synchronous MCCQ */
int be_cmd_pmac_add(struct be_adapter *adapter, const u8 *mac_addr,
		    u32 if_id, u32 *pmac_id, u32 domain)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_pmac_add *req;
	int status;

	mutex_lock(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}
	req = embedded_payload(wrb);

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
			       OPCODE_COMMON_NTWK_PMAC_ADD, sizeof(*req), wrb,
			       NULL);

	req->hdr.domain = domain;
	req->if_id = cpu_to_le32(if_id);
	memcpy(req->mac_address, mac_addr, ETH_ALEN);

	status = be_mcc_notify_wait(adapter);
	if (!status) {
		struct be_cmd_resp_pmac_add *resp = embedded_payload(wrb);

		*pmac_id = le32_to_cpu(resp->pmac_id);
	}

err:
	mutex_unlock(&adapter->mcc_lock);

	 if (base_status(status) == MCC_STATUS_UNAUTHORIZED_REQUEST)
		status = -EPERM;

	return status;
}

/* Uses synchronous MCCQ */
int be_cmd_pmac_del(struct be_adapter *adapter, u32 if_id, int pmac_id, u32 dom)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_pmac_del *req;
	int status;

	if (pmac_id == -1)
		return 0;

	mutex_lock(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}
	req = embedded_payload(wrb);

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
			       OPCODE_COMMON_NTWK_PMAC_DEL, sizeof(*req),
			       wrb, NULL);

	req->hdr.domain = dom;
	req->if_id = cpu_to_le32(if_id);
	req->pmac_id = cpu_to_le32(pmac_id);

	status = be_mcc_notify_wait(adapter);

err:
	mutex_unlock(&adapter->mcc_lock);
	return status;
}

/* Uses Mbox */
int be_cmd_cq_create(struct be_adapter *adapter, struct be_queue_info *cq,
		     struct be_queue_info *eq, bool no_delay, int coalesce_wm)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_cq_create *req;
	struct be_dma_mem *q_mem = &cq->dma_mem;
	void *ctxt;
	int status;

	if (mutex_lock_interruptible(&adapter->mbox_lock))
		return -1;

	wrb = wrb_from_mbox(adapter);
	req = embedded_payload(wrb);
	ctxt = &req->context;

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
			       OPCODE_COMMON_CQ_CREATE, sizeof(*req), wrb,
			       NULL);

	req->num_pages =  cpu_to_le16(PAGES_4K_SPANNED(q_mem->va, q_mem->size));

	if (BEx_chip(adapter)) {
		AMAP_SET_BITS(struct amap_cq_context_be, coalescwm, ctxt,
			      coalesce_wm);
		AMAP_SET_BITS(struct amap_cq_context_be, nodelay,
			      ctxt, no_delay);
		AMAP_SET_BITS(struct amap_cq_context_be, count, ctxt,
			      __ilog2_u32(cq->len / 256));
		AMAP_SET_BITS(struct amap_cq_context_be, valid, ctxt, 1);
		AMAP_SET_BITS(struct amap_cq_context_be, eventable, ctxt, 1);
		AMAP_SET_BITS(struct amap_cq_context_be, eqid, ctxt, eq->id);
	} else {
		req->hdr.version = 2;
		req->page_size = 1; /* 1 for 4K */

		/* coalesce-wm field in this cmd is not relevant to Lancer.
		 * Lancer uses COMMON_MODIFY_CQ to set this field
		 */
		if (!lancer_chip(adapter))
			AMAP_SET_BITS(struct amap_cq_context_v2, coalescwm,
				      ctxt, coalesce_wm);
		AMAP_SET_BITS(struct amap_cq_context_v2, nodelay, ctxt,
			      no_delay);
		AMAP_SET_BITS(struct amap_cq_context_v2, count, ctxt,
			      __ilog2_u32(cq->len / 256));
		AMAP_SET_BITS(struct amap_cq_context_v2, valid, ctxt, 1);
		AMAP_SET_BITS(struct amap_cq_context_v2, eventable, ctxt, 1);
		AMAP_SET_BITS(struct amap_cq_context_v2, eqid, ctxt, eq->id);
	}

	be_dws_cpu_to_le(ctxt, sizeof(req->context));

	be_cmd_page_addrs_prepare(req->pages, ARRAY_SIZE(req->pages), q_mem);

	status = be_mbox_notify_wait(adapter);
	if (!status) {
		struct be_cmd_resp_cq_create *resp = embedded_payload(wrb);

		cq->id = le16_to_cpu(resp->cq_id);
		cq->created = true;
	}

	mutex_unlock(&adapter->mbox_lock);

	return status;
}

static u32 be_encoded_q_len(int q_len)
{
	u32 len_encoded = fls(q_len); /* log2(len) + 1 */

	if (len_encoded == 16)
		len_encoded = 0;
	return len_encoded;
}

static int be_cmd_mccq_ext_create(struct be_adapter *adapter,
				  struct be_queue_info *mccq,
				  struct be_queue_info *cq)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_mcc_ext_create *req;
	struct be_dma_mem *q_mem = &mccq->dma_mem;
	void *ctxt;
	int status;

	if (mutex_lock_interruptible(&adapter->mbox_lock))
		return -1;

	wrb = wrb_from_mbox(adapter);
	req = embedded_payload(wrb);
	ctxt = &req->context;

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
			       OPCODE_COMMON_MCC_CREATE_EXT, sizeof(*req), wrb,
			       NULL);

	req->num_pages = cpu_to_le16(PAGES_4K_SPANNED(q_mem->va, q_mem->size));
	if (BEx_chip(adapter)) {
		AMAP_SET_BITS(struct amap_mcc_context_be, valid, ctxt, 1);
		AMAP_SET_BITS(struct amap_mcc_context_be, ring_size, ctxt,
			      be_encoded_q_len(mccq->len));
		AMAP_SET_BITS(struct amap_mcc_context_be, cq_id, ctxt, cq->id);
	} else {
		req->hdr.version = 1;
		req->cq_id = cpu_to_le16(cq->id);

		AMAP_SET_BITS(struct amap_mcc_context_v1, ring_size, ctxt,
			      be_encoded_q_len(mccq->len));
		AMAP_SET_BITS(struct amap_mcc_context_v1, valid, ctxt, 1);
		AMAP_SET_BITS(struct amap_mcc_context_v1, async_cq_id,
			      ctxt, cq->id);
		AMAP_SET_BITS(struct amap_mcc_context_v1, async_cq_valid,
			      ctxt, 1);
	}

	/* Subscribe to Link State, Sliport Event and Group 5 Events
	 * (bits 1, 5 and 17 set)
	 */
	req->async_event_bitmap[0] =
			cpu_to_le32(BIT(ASYNC_EVENT_CODE_LINK_STATE) |
				    BIT(ASYNC_EVENT_CODE_GRP_5) |
				    BIT(ASYNC_EVENT_CODE_QNQ) |
				    BIT(ASYNC_EVENT_CODE_SLIPORT));

	be_dws_cpu_to_le(ctxt, sizeof(req->context));

	be_cmd_page_addrs_prepare(req->pages, ARRAY_SIZE(req->pages), q_mem);

	status = be_mbox_notify_wait(adapter);
	if (!status) {
		struct be_cmd_resp_mcc_create *resp = embedded_payload(wrb);

		mccq->id = le16_to_cpu(resp->id);
		mccq->created = true;
	}
	mutex_unlock(&adapter->mbox_lock);

	return status;
}

static int be_cmd_mccq_org_create(struct be_adapter *adapter,
				  struct be_queue_info *mccq,
				  struct be_queue_info *cq)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_mcc_create *req;
	struct be_dma_mem *q_mem = &mccq->dma_mem;
	void *ctxt;
	int status;

	if (mutex_lock_interruptible(&adapter->mbox_lock))
		return -1;

	wrb = wrb_from_mbox(adapter);
	req = embedded_payload(wrb);
	ctxt = &req->context;

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
			       OPCODE_COMMON_MCC_CREATE, sizeof(*req), wrb,
			       NULL);

	req->num_pages = cpu_to_le16(PAGES_4K_SPANNED(q_mem->va, q_mem->size));

	AMAP_SET_BITS(struct amap_mcc_context_be, valid, ctxt, 1);
	AMAP_SET_BITS(struct amap_mcc_context_be, ring_size, ctxt,
		      be_encoded_q_len(mccq->len));
	AMAP_SET_BITS(struct amap_mcc_context_be, cq_id, ctxt, cq->id);

	be_dws_cpu_to_le(ctxt, sizeof(req->context));

	be_cmd_page_addrs_prepare(req->pages, ARRAY_SIZE(req->pages), q_mem);

	status = be_mbox_notify_wait(adapter);
	if (!status) {
		struct be_cmd_resp_mcc_create *resp = embedded_payload(wrb);

		mccq->id = le16_to_cpu(resp->id);
		mccq->created = true;
	}

	mutex_unlock(&adapter->mbox_lock);
	return status;
}

int be_cmd_mccq_create(struct be_adapter *adapter,
		       struct be_queue_info *mccq, struct be_queue_info *cq)
{
	int status;

	status = be_cmd_mccq_ext_create(adapter, mccq, cq);
	if (status && BEx_chip(adapter)) {
		dev_warn(&adapter->pdev->dev, "Upgrade to F/W ver 2.102.235.0 "
			"or newer to avoid conflicting priorities between NIC "
			"and FCoE traffic");
		status = be_cmd_mccq_org_create(adapter, mccq, cq);
	}
	return status;
}

int be_cmd_txq_create(struct be_adapter *adapter, struct be_tx_obj *txo)
{
	struct be_mcc_wrb wrb = {0};
	struct be_cmd_req_eth_tx_create *req;
	struct be_queue_info *txq = &txo->q;
	struct be_queue_info *cq = &txo->cq;
	struct be_dma_mem *q_mem = &txq->dma_mem;
	int status, ver = 0;

	req = embedded_payload(&wrb);
	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_ETH,
			       OPCODE_ETH_TX_CREATE, sizeof(*req), &wrb, NULL);

	if (lancer_chip(adapter)) {
		req->hdr.version = 1;
	} else if (BEx_chip(adapter)) {
		if (adapter->function_caps & BE_FUNCTION_CAPS_SUPER_NIC)
			req->hdr.version = 2;
	} else { /* For SH */
		req->hdr.version = 2;
	}

	if (req->hdr.version > 0)
		req->if_id = cpu_to_le16(adapter->if_handle);
	req->num_pages = PAGES_4K_SPANNED(q_mem->va, q_mem->size);
	req->ulp_num = BE_ULP1_NUM;
	req->type = BE_ETH_TX_RING_TYPE_STANDARD;
	req->cq_id = cpu_to_le16(cq->id);
	req->queue_size = be_encoded_q_len(txq->len);
	be_cmd_page_addrs_prepare(req->pages, ARRAY_SIZE(req->pages), q_mem);
	ver = req->hdr.version;

	status = be_cmd_notify_wait(adapter, &wrb);
	if (!status) {
		struct be_cmd_resp_eth_tx_create *resp = embedded_payload(&wrb);

		txq->id = le16_to_cpu(resp->cid);
		if (ver == 2)
			txo->db_offset = le32_to_cpu(resp->db_offset);
		else
			txo->db_offset = DB_TXULP1_OFFSET;
		txq->created = true;
	}

	return status;
}

/* Uses MCC */
int be_cmd_rxq_create(struct be_adapter *adapter,
		      struct be_queue_info *rxq, u16 cq_id, u16 frag_size,
		      u32 if_id, u32 rss, u8 *rss_id)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_eth_rx_create *req;
	struct be_dma_mem *q_mem = &rxq->dma_mem;
	int status;

	mutex_lock(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}
	req = embedded_payload(wrb);

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_ETH,
			       OPCODE_ETH_RX_CREATE, sizeof(*req), wrb, NULL);

	req->cq_id = cpu_to_le16(cq_id);
	req->frag_size = fls(frag_size) - 1;
	req->num_pages = 2;
	be_cmd_page_addrs_prepare(req->pages, ARRAY_SIZE(req->pages), q_mem);
	req->interface_id = cpu_to_le32(if_id);
	req->max_frame_size = cpu_to_le16(BE_MAX_JUMBO_FRAME_SIZE);
	req->rss_queue = cpu_to_le32(rss);

	status = be_mcc_notify_wait(adapter);
	if (!status) {
		struct be_cmd_resp_eth_rx_create *resp = embedded_payload(wrb);

		rxq->id = le16_to_cpu(resp->id);
		rxq->created = true;
		*rss_id = resp->rss_id;
	}

err:
	mutex_unlock(&adapter->mcc_lock);
	return status;
}

/* Generic destroyer function for all types of queues
 * Uses Mbox
 */
int be_cmd_q_destroy(struct be_adapter *adapter, struct be_queue_info *q,
		     int queue_type)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_q_destroy *req;
	u8 subsys = 0, opcode = 0;
	int status;

	if (mutex_lock_interruptible(&adapter->mbox_lock))
		return -1;

	wrb = wrb_from_mbox(adapter);
	req = embedded_payload(wrb);

	switch (queue_type) {
	case QTYPE_EQ:
		subsys = CMD_SUBSYSTEM_COMMON;
		opcode = OPCODE_COMMON_EQ_DESTROY;
		break;
	case QTYPE_CQ:
		subsys = CMD_SUBSYSTEM_COMMON;
		opcode = OPCODE_COMMON_CQ_DESTROY;
		break;
	case QTYPE_TXQ:
		subsys = CMD_SUBSYSTEM_ETH;
		opcode = OPCODE_ETH_TX_DESTROY;
		break;
	case QTYPE_RXQ:
		subsys = CMD_SUBSYSTEM_ETH;
		opcode = OPCODE_ETH_RX_DESTROY;
		break;
	case QTYPE_MCCQ:
		subsys = CMD_SUBSYSTEM_COMMON;
		opcode = OPCODE_COMMON_MCC_DESTROY;
		break;
	default:
		BUG();
	}

	be_wrb_cmd_hdr_prepare(&req->hdr, subsys, opcode, sizeof(*req), wrb,
			       NULL);
	req->id = cpu_to_le16(q->id);

	status = be_mbox_notify_wait(adapter);
	q->created = false;

	mutex_unlock(&adapter->mbox_lock);
	return status;
}

/* Uses MCC */
int be_cmd_rxq_destroy(struct be_adapter *adapter, struct be_queue_info *q)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_q_destroy *req;
	int status;

	mutex_lock(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}
	req = embedded_payload(wrb);

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_ETH,
			       OPCODE_ETH_RX_DESTROY, sizeof(*req), wrb, NULL);
	req->id = cpu_to_le16(q->id);

	status = be_mcc_notify_wait(adapter);
	q->created = false;

err:
	mutex_unlock(&adapter->mcc_lock);
	return status;
}

/* Create an rx filtering policy configuration on an i/f
 * Will use MBOX only if MCCQ has not been created.
 */
int be_cmd_if_create(struct be_adapter *adapter, u32 cap_flags, u32 en_flags,
		     u32 *if_handle, u32 domain)
{
	struct be_mcc_wrb wrb = {0};
	struct be_cmd_req_if_create *req;
	int status;

	req = embedded_payload(&wrb);
	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
			       OPCODE_COMMON_NTWK_INTERFACE_CREATE,
			       sizeof(*req), &wrb, NULL);
	req->hdr.domain = domain;
	req->capability_flags = cpu_to_le32(cap_flags);
	req->enable_flags = cpu_to_le32(en_flags);
	req->pmac_invalid = true;

	status = be_cmd_notify_wait(adapter, &wrb);
	if (!status) {
		struct be_cmd_resp_if_create *resp = embedded_payload(&wrb);

		*if_handle = le32_to_cpu(resp->interface_id);

		/* Hack to retrieve VF's pmac-id on BE3 */
		if (BE3_chip(adapter) && be_virtfn(adapter))
			adapter->pmac_id[0] = le32_to_cpu(resp->pmac_id);
	}
	return status;
}

/* Uses MCCQ if available else MBOX */
int be_cmd_if_destroy(struct be_adapter *adapter, int interface_id, u32 domain)
{
	struct be_mcc_wrb wrb = {0};
	struct be_cmd_req_if_destroy *req;
	int status;

	if (interface_id == -1)
		return 0;

	req = embedded_payload(&wrb);

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
			       OPCODE_COMMON_NTWK_INTERFACE_DESTROY,
			       sizeof(*req), &wrb, NULL);
	req->hdr.domain = domain;
	req->interface_id = cpu_to_le32(interface_id);

	status = be_cmd_notify_wait(adapter, &wrb);
	return status;
}

/* Get stats is a non embedded command: the request is not embedded inside
 * WRB but is a separate dma memory block
 * Uses asynchronous MCC
 */
int be_cmd_get_stats(struct be_adapter *adapter, struct be_dma_mem *nonemb_cmd)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_hdr *hdr;
	int status = 0;

	mutex_lock(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}
	hdr = nonemb_cmd->va;

	be_wrb_cmd_hdr_prepare(hdr, CMD_SUBSYSTEM_ETH,
			       OPCODE_ETH_GET_STATISTICS, nonemb_cmd->size, wrb,
			       nonemb_cmd);

	/* version 1 of the cmd is not supported only by BE2 */
	if (BE2_chip(adapter))
		hdr->version = 0;
	if (BE3_chip(adapter) || lancer_chip(adapter))
		hdr->version = 1;
	else
		hdr->version = 2;

	status = be_mcc_notify(adapter);
	if (status)
		goto err;

	adapter->stats_cmd_sent = true;

err:
	mutex_unlock(&adapter->mcc_lock);
	return status;
}

/* Lancer Stats */
int lancer_cmd_get_pport_stats(struct be_adapter *adapter,
			       struct be_dma_mem *nonemb_cmd)
{
	struct be_mcc_wrb *wrb;
	struct lancer_cmd_req_pport_stats *req;
	int status = 0;

	if (!be_cmd_allowed(adapter, OPCODE_ETH_GET_PPORT_STATS,
			    CMD_SUBSYSTEM_ETH))
		return -EPERM;

	mutex_lock(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}
	req = nonemb_cmd->va;

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_ETH,
			       OPCODE_ETH_GET_PPORT_STATS, nonemb_cmd->size,
			       wrb, nonemb_cmd);

	req->cmd_params.params.pport_num = cpu_to_le16(adapter->hba_port_num);
	req->cmd_params.params.reset_stats = 0;

	status = be_mcc_notify(adapter);
	if (status)
		goto err;

	adapter->stats_cmd_sent = true;

err:
	mutex_unlock(&adapter->mcc_lock);
	return status;
}

static int be_mac_to_link_speed(int mac_speed)
{
	switch (mac_speed) {
	case PHY_LINK_SPEED_ZERO:
		return 0;
	case PHY_LINK_SPEED_10MBPS:
		return 10;
	case PHY_LINK_SPEED_100MBPS:
		return 100;
	case PHY_LINK_SPEED_1GBPS:
		return 1000;
	case PHY_LINK_SPEED_10GBPS:
		return 10000;
	case PHY_LINK_SPEED_20GBPS:
		return 20000;
	case PHY_LINK_SPEED_25GBPS:
		return 25000;
	case PHY_LINK_SPEED_40GBPS:
		return 40000;
	}
	return 0;
}

/* Uses synchronous mcc
 * Returns link_speed in Mbps
 */
int be_cmd_link_status_query(struct be_adapter *adapter, u16 *link_speed,
			     u8 *link_status, u32 dom)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_link_status *req;
	int status;

	mutex_lock(&adapter->mcc_lock);

	if (link_status)
		*link_status = LINK_DOWN;

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}
	req = embedded_payload(wrb);

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
			       OPCODE_COMMON_NTWK_LINK_STATUS_QUERY,
			       sizeof(*req), wrb, NULL);

	/* version 1 of the cmd is not supported only by BE2 */
	if (!BE2_chip(adapter))
		req->hdr.version = 1;

	req->hdr.domain = dom;

	status = be_mcc_notify_wait(adapter);
	if (!status) {
		struct be_cmd_resp_link_status *resp = embedded_payload(wrb);

		if (link_speed) {
			*link_speed = resp->link_speed ?
				      le16_to_cpu(resp->link_speed) * 10 :
				      be_mac_to_link_speed(resp->mac_speed);

			if (!resp->logical_link_status)
				*link_speed = 0;
		}
		if (link_status)
			*link_status = resp->logical_link_status;
	}

err:
	mutex_unlock(&adapter->mcc_lock);
	return status;
}

/* Uses synchronous mcc */
int be_cmd_get_die_temperature(struct be_adapter *adapter)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_get_cntl_addnl_attribs *req;
	int status = 0;

	mutex_lock(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}
	req = embedded_payload(wrb);

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
			       OPCODE_COMMON_GET_CNTL_ADDITIONAL_ATTRIBUTES,
			       sizeof(*req), wrb, NULL);

	status = be_mcc_notify(adapter);
err:
	mutex_unlock(&adapter->mcc_lock);
	return status;
}

/* Uses synchronous mcc */
int be_cmd_get_fat_dump_len(struct be_adapter *adapter, u32 *dump_size)
{
	struct be_mcc_wrb wrb = {0};
	struct be_cmd_req_get_fat *req;
	int status;

	req = embedded_payload(&wrb);

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
			       OPCODE_COMMON_MANAGE_FAT, sizeof(*req),
			       &wrb, NULL);
	req->fat_operation = cpu_to_le32(QUERY_FAT);
	status = be_cmd_notify_wait(adapter, &wrb);
	if (!status) {
		struct be_cmd_resp_get_fat *resp = embedded_payload(&wrb);

		if (dump_size && resp->log_size)
			*dump_size = le32_to_cpu(resp->log_size) -
					sizeof(u32);
	}
	return status;
}

int be_cmd_get_fat_dump(struct be_adapter *adapter, u32 buf_len, void *buf)
{
	struct be_dma_mem get_fat_cmd;
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_get_fat *req;
	u32 offset = 0, total_size, buf_size,
				log_offset = sizeof(u32), payload_len;
	int status;

	if (buf_len == 0)
		return 0;

	total_size = buf_len;

	get_fat_cmd.size = sizeof(struct be_cmd_req_get_fat) + 60*1024;
	get_fat_cmd.va = dma_alloc_coherent(&adapter->pdev->dev,
					    get_fat_cmd.size,
					    &get_fat_cmd.dma, GFP_ATOMIC);
	if (!get_fat_cmd.va)
		return -ENOMEM;

	mutex_lock(&adapter->mcc_lock);

	while (total_size) {
		buf_size = min(total_size, (u32)60*1024);
		total_size -= buf_size;

		wrb = wrb_from_mccq(adapter);
		if (!wrb) {
			status = -EBUSY;
			goto err;
		}
		req = get_fat_cmd.va;

		payload_len = sizeof(struct be_cmd_req_get_fat) + buf_size;
		be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
				       OPCODE_COMMON_MANAGE_FAT, payload_len,
				       wrb, &get_fat_cmd);

		req->fat_operation = cpu_to_le32(RETRIEVE_FAT);
		req->read_log_offset = cpu_to_le32(log_offset);
		req->read_log_length = cpu_to_le32(buf_size);
		req->data_buffer_size = cpu_to_le32(buf_size);

		status = be_mcc_notify_wait(adapter);
		if (!status) {
			struct be_cmd_resp_get_fat *resp = get_fat_cmd.va;

			memcpy(buf + offset,
			       resp->data_buffer,
			       le32_to_cpu(resp->read_log_length));
		} else {
			dev_err(&adapter->pdev->dev, "FAT Table Retrieve error\n");
			goto err;
		}
		offset += buf_size;
		log_offset += buf_size;
	}
err:
	dma_free_coherent(&adapter->pdev->dev, get_fat_cmd.size,
			  get_fat_cmd.va, get_fat_cmd.dma);
	mutex_unlock(&adapter->mcc_lock);
	return status;
}

/* Uses synchronous mcc */
int be_cmd_get_fw_ver(struct be_adapter *adapter)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_get_fw_version *req;
	int status;

	mutex_lock(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}

	req = embedded_payload(wrb);

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
			       OPCODE_COMMON_GET_FW_VERSION, sizeof(*req), wrb,
			       NULL);
	status = be_mcc_notify_wait(adapter);
	if (!status) {
		struct be_cmd_resp_get_fw_version *resp = embedded_payload(wrb);

		strscpy(adapter->fw_ver, resp->firmware_version_string,
			sizeof(adapter->fw_ver));
		strscpy(adapter->fw_on_flash, resp->fw_on_flash_version_string,
			sizeof(adapter->fw_on_flash));
	}
err:
	mutex_unlock(&adapter->mcc_lock);
	return status;
}

/* set the EQ delay interval of an EQ to specified value
 * Uses async mcc
 */
static int __be_cmd_modify_eqd(struct be_adapter *adapter,
			       struct be_set_eqd *set_eqd, int num)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_modify_eq_delay *req;
	int status = 0, i;

	mutex_lock(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}
	req = embedded_payload(wrb);

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
			       OPCODE_COMMON_MODIFY_EQ_DELAY, sizeof(*req), wrb,
			       NULL);

	req->num_eq = cpu_to_le32(num);
	for (i = 0; i < num; i++) {
		req->set_eqd[i].eq_id = cpu_to_le32(set_eqd[i].eq_id);
		req->set_eqd[i].phase = 0;
		req->set_eqd[i].delay_multiplier =
				cpu_to_le32(set_eqd[i].delay_multiplier);
	}

	status = be_mcc_notify(adapter);
err:
	mutex_unlock(&adapter->mcc_lock);
	return status;
}

int be_cmd_modify_eqd(struct be_adapter *adapter, struct be_set_eqd *set_eqd,
		      int num)
{
	int num_eqs, i = 0;

	while (num) {
		num_eqs = min(num, 8);
		__be_cmd_modify_eqd(adapter, &set_eqd[i], num_eqs);
		i += num_eqs;
		num -= num_eqs;
	}

	return 0;
}

/* Uses sycnhronous mcc */
int be_cmd_vlan_config(struct be_adapter *adapter, u32 if_id, u16 *vtag_array,
		       u32 num, u32 domain)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_vlan_config *req;
	int status;

	mutex_lock(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}
	req = embedded_payload(wrb);

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
			       OPCODE_COMMON_NTWK_VLAN_CONFIG, sizeof(*req),
			       wrb, NULL);
	req->hdr.domain = domain;

	req->interface_id = if_id;
	req->untagged = BE_IF_FLAGS_UNTAGGED & be_if_cap_flags(adapter) ? 1 : 0;
	req->num_vlan = num;
	memcpy(req->normal_vlan, vtag_array,
	       req->num_vlan * sizeof(vtag_array[0]));

	status = be_mcc_notify_wait(adapter);
err:
	mutex_unlock(&adapter->mcc_lock);
	return status;
}

static int __be_cmd_rx_filter(struct be_adapter *adapter, u32 flags, u32 value)
{
	struct be_mcc_wrb *wrb;
	struct be_dma_mem *mem = &adapter->rx_filter;
	struct be_cmd_req_rx_filter *req = mem->va;
	int status;

	mutex_lock(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}
	memset(req, 0, sizeof(*req));
	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
			       OPCODE_COMMON_NTWK_RX_FILTER, sizeof(*req),
			       wrb, mem);

	req->if_id = cpu_to_le32(adapter->if_handle);
	req->if_flags_mask = cpu_to_le32(flags);
	req->if_flags = (value == ON) ? req->if_flags_mask : 0;

	if (flags & BE_IF_FLAGS_MULTICAST) {
		int i;

		/* Reset mcast promisc mode if already set by setting mask
		 * and not setting flags field
		 */
		req->if_flags_mask |=
			cpu_to_le32(BE_IF_FLAGS_MCAST_PROMISCUOUS &
				    be_if_cap_flags(adapter));
		req->mcast_num = cpu_to_le32(adapter->mc_count);
		for (i = 0; i < adapter->mc_count; i++)
			ether_addr_copy(req->mcast_mac[i].byte,
					adapter->mc_list[i].mac);
	}

	status = be_mcc_notify_wait(adapter);
err:
	mutex_unlock(&adapter->mcc_lock);
	return status;
}

int be_cmd_rx_filter(struct be_adapter *adapter, u32 flags, u32 value)
{
	struct device *dev = &adapter->pdev->dev;

	if ((flags & be_if_cap_flags(adapter)) != flags) {
		dev_warn(dev, "Cannot set rx filter flags 0x%x\n", flags);
		dev_warn(dev, "Interface is capable of 0x%x flags only\n",
			 be_if_cap_flags(adapter));
	}
	flags &= be_if_cap_flags(adapter);
	if (!flags)
		return -ENOTSUPP;

	return __be_cmd_rx_filter(adapter, flags, value);
}

/* Uses synchrounous mcc */
int be_cmd_set_flow_control(struct be_adapter *adapter, u32 tx_fc, u32 rx_fc)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_set_flow_control *req;
	int status;

	if (!be_cmd_allowed(adapter, OPCODE_COMMON_SET_FLOW_CONTROL,
			    CMD_SUBSYSTEM_COMMON))
		return -EPERM;

	mutex_lock(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}
	req = embedded_payload(wrb);

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
			       OPCODE_COMMON_SET_FLOW_CONTROL, sizeof(*req),
			       wrb, NULL);

	req->hdr.version = 1;
	req->tx_flow_control = cpu_to_le16((u16)tx_fc);
	req->rx_flow_control = cpu_to_le16((u16)rx_fc);

	status = be_mcc_notify_wait(adapter);

err:
	mutex_unlock(&adapter->mcc_lock);

	if (base_status(status) == MCC_STATUS_FEATURE_NOT_SUPPORTED)
		return  -EOPNOTSUPP;

	return status;
}

/* Uses sycn mcc */
int be_cmd_get_flow_control(struct be_adapter *adapter, u32 *tx_fc, u32 *rx_fc)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_get_flow_control *req;
	int status;

	if (!be_cmd_allowed(adapter, OPCODE_COMMON_GET_FLOW_CONTROL,
			    CMD_SUBSYSTEM_COMMON))
		return -EPERM;

	mutex_lock(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}
	req = embedded_payload(wrb);

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
			       OPCODE_COMMON_GET_FLOW_CONTROL, sizeof(*req),
			       wrb, NULL);

	status = be_mcc_notify_wait(adapter);
	if (!status) {
		struct be_cmd_resp_get_flow_control *resp =
						embedded_payload(wrb);

		*tx_fc = le16_to_cpu(resp->tx_flow_control);
		*rx_fc = le16_to_cpu(resp->rx_flow_control);
	}

err:
	mutex_unlock(&adapter->mcc_lock);
	return status;
}

/* Uses mbox */
int be_cmd_query_fw_cfg(struct be_adapter *adapter)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_query_fw_cfg *req;
	int status;

	if (mutex_lock_interruptible(&adapter->mbox_lock))
		return -1;

	wrb = wrb_from_mbox(adapter);
	req = embedded_payload(wrb);

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
			       OPCODE_COMMON_QUERY_FIRMWARE_CONFIG,
			       sizeof(*req), wrb, NULL);

	status = be_mbox_notify_wait(adapter);
	if (!status) {
		struct be_cmd_resp_query_fw_cfg *resp = embedded_payload(wrb);

		adapter->port_num = le32_to_cpu(resp->phys_port);
		adapter->function_mode = le32_to_cpu(resp->function_mode);
		adapter->function_caps = le32_to_cpu(resp->function_caps);
		adapter->asic_rev = le32_to_cpu(resp->asic_revision) & 0xFF;
		dev_info(&adapter->pdev->dev,
			 "FW config: function_mode=0x%x, function_caps=0x%x\n",
			 adapter->function_mode, adapter->function_caps);
	}

	mutex_unlock(&adapter->mbox_lock);
	return status;
}

/* Uses mbox */
int be_cmd_reset_function(struct be_adapter *adapter)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_hdr *req;
	int status;

	if (lancer_chip(adapter)) {
		iowrite32(SLI_PORT_CONTROL_IP_MASK,
			  adapter->db + SLIPORT_CONTROL_OFFSET);
		status = lancer_wait_ready(adapter);
		if (status)
			dev_err(&adapter->pdev->dev,
				"Adapter in non recoverable error\n");
		return status;
	}

	if (mutex_lock_interruptible(&adapter->mbox_lock))
		return -1;

	wrb = wrb_from_mbox(adapter);
	req = embedded_payload(wrb);

	be_wrb_cmd_hdr_prepare(req, CMD_SUBSYSTEM_COMMON,
			       OPCODE_COMMON_FUNCTION_RESET, sizeof(*req), wrb,
			       NULL);

	status = be_mbox_notify_wait(adapter);

	mutex_unlock(&adapter->mbox_lock);
	return status;
}

int be_cmd_rss_config(struct be_adapter *adapter, u8 *rsstable,
		      u32 rss_hash_opts, u16 table_size, const u8 *rss_hkey)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_rss_config *req;
	int status;

	if (!(be_if_cap_flags(adapter) & BE_IF_FLAGS_RSS))
		return 0;

	mutex_lock(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}
	req = embedded_payload(wrb);

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_ETH,
			       OPCODE_ETH_RSS_CONFIG, sizeof(*req), wrb, NULL);

	req->if_id = cpu_to_le32(adapter->if_handle);
	req->enable_rss = cpu_to_le16(rss_hash_opts);
	req->cpu_table_size_log2 = cpu_to_le16(fls(table_size) - 1);

	if (!BEx_chip(adapter))
		req->hdr.version = 1;

	memcpy(req->cpu_table, rsstable, table_size);
	memcpy(req->hash, rss_hkey, RSS_HASH_KEY_LEN);
	be_dws_cpu_to_le(req->hash, sizeof(req->hash));

	status = be_mcc_notify_wait(adapter);
err:
	mutex_unlock(&adapter->mcc_lock);
	return status;
}

/* Uses sync mcc */
int be_cmd_set_beacon_state(struct be_adapter *adapter, u8 port_num,
			    u8 bcn, u8 sts, u8 state)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_enable_disable_beacon *req;
	int status;

	mutex_lock(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}
	req = embedded_payload(wrb);

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
			       OPCODE_COMMON_ENABLE_DISABLE_BEACON,
			       sizeof(*req), wrb, NULL);

	req->port_num = port_num;
	req->beacon_state = state;
	req->beacon_duration = bcn;
	req->status_duration = sts;

	status = be_mcc_notify_wait(adapter);

err:
	mutex_unlock(&adapter->mcc_lock);
	return status;
}

/* Uses sync mcc */
int be_cmd_get_beacon_state(struct be_adapter *adapter, u8 port_num, u32 *state)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_get_beacon_state *req;
	int status;

	mutex_lock(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}
	req = embedded_payload(wrb);

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
			       OPCODE_COMMON_GET_BEACON_STATE, sizeof(*req),
			       wrb, NULL);

	req->port_num = port_num;

	status = be_mcc_notify_wait(adapter);
	if (!status) {
		struct be_cmd_resp_get_beacon_state *resp =
						embedded_payload(wrb);

		*state = resp->beacon_state;
	}

err:
	mutex_unlock(&adapter->mcc_lock);
	return status;
}

/* Uses sync mcc */
int be_cmd_read_port_transceiver_data(struct be_adapter *adapter,
				      u8 page_num, u32 off, u32 len, u8 *data)
{
	struct be_dma_mem cmd;
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_port_type *req;
	int status;

	if (page_num > TR_PAGE_A2)
		return -EINVAL;

	cmd.size = sizeof(struct be_cmd_resp_port_type);
	cmd.va = dma_alloc_coherent(&adapter->pdev->dev, cmd.size, &cmd.dma,
				    GFP_ATOMIC);
	if (!cmd.va) {
		dev_err(&adapter->pdev->dev, "Memory allocation failed\n");
		return -ENOMEM;
	}

	mutex_lock(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}
	req = cmd.va;

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
			       OPCODE_COMMON_READ_TRANSRECV_DATA,
			       cmd.size, wrb, &cmd);

	req->port = cpu_to_le32(adapter->hba_port_num);
	req->page_num = cpu_to_le32(page_num);
	status = be_mcc_notify_wait(adapter);
	if (!status && len > 0) {
		struct be_cmd_resp_port_type *resp = cmd.va;

		memcpy(data, resp->page_data + off, len);
	}
err:
	mutex_unlock(&adapter->mcc_lock);
	dma_free_coherent(&adapter->pdev->dev, cmd.size, cmd.va, cmd.dma);
	return status;
}

static int lancer_cmd_write_object(struct be_adapter *adapter,
				   struct be_dma_mem *cmd, u32 data_size,
				   u32 data_offset, const char *obj_name,
				   u32 *data_written, u8 *change_status,
				   u8 *addn_status)
{
	struct be_mcc_wrb *wrb;
	struct lancer_cmd_req_write_object *req;
	struct lancer_cmd_resp_write_object *resp;
	void *ctxt = NULL;
	int status;

	mutex_lock(&adapter->mcc_lock);
	adapter->flash_status = 0;

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err_unlock;
	}

	req = embedded_payload(wrb);

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
			       OPCODE_COMMON_WRITE_OBJECT,
			       sizeof(struct lancer_cmd_req_write_object), wrb,
			       NULL);

	ctxt = &req->context;
	AMAP_SET_BITS(struct amap_lancer_write_obj_context,
		      write_length, ctxt, data_size);

	if (data_size == 0)
		AMAP_SET_BITS(struct amap_lancer_write_obj_context,
			      eof, ctxt, 1);
	else
		AMAP_SET_BITS(struct amap_lancer_write_obj_context,
			      eof, ctxt, 0);

	be_dws_cpu_to_le(ctxt, sizeof(req->context));
	req->write_offset = cpu_to_le32(data_offset);
	strscpy(req->object_name, obj_name, sizeof(req->object_name));
	req->descriptor_count = cpu_to_le32(1);
	req->buf_len = cpu_to_le32(data_size);
	req->addr_low = cpu_to_le32((cmd->dma +
				     sizeof(struct lancer_cmd_req_write_object))
				    & 0xFFFFFFFF);
	req->addr_high = cpu_to_le32(upper_32_bits(cmd->dma +
				sizeof(struct lancer_cmd_req_write_object)));

	status = be_mcc_notify(adapter);
	if (status)
		goto err_unlock;

	mutex_unlock(&adapter->mcc_lock);

	if (!wait_for_completion_timeout(&adapter->et_cmd_compl,
					 msecs_to_jiffies(60000)))
		status = -ETIMEDOUT;
	else
		status = adapter->flash_status;

	resp = embedded_payload(wrb);
	if (!status) {
		*data_written = le32_to_cpu(resp->actual_write_len);
		*change_status = resp->change_status;
	} else {
		*addn_status = resp->additional_status;
	}

	return status;

err_unlock:
	mutex_unlock(&adapter->mcc_lock);
	return status;
}

int be_cmd_query_cable_type(struct be_adapter *adapter)
{
	u8 page_data[PAGE_DATA_LEN];
	int status;

	status = be_cmd_read_port_transceiver_data(adapter, TR_PAGE_A0,
						   0, PAGE_DATA_LEN, page_data);
	if (!status) {
		switch (adapter->phy.interface_type) {
		case PHY_TYPE_QSFP:
			adapter->phy.cable_type =
				page_data[QSFP_PLUS_CABLE_TYPE_OFFSET];
			break;
		case PHY_TYPE_SFP_PLUS_10GB:
			adapter->phy.cable_type =
				page_data[SFP_PLUS_CABLE_TYPE_OFFSET];
			break;
		default:
			adapter->phy.cable_type = 0;
			break;
		}
	}
	return status;
}

int be_cmd_query_sfp_info(struct be_adapter *adapter)
{
	u8 page_data[PAGE_DATA_LEN];
	int status;

	status = be_cmd_read_port_transceiver_data(adapter, TR_PAGE_A0,
						   0, PAGE_DATA_LEN, page_data);
	if (!status) {
		strscpy(adapter->phy.vendor_name, page_data +
			SFP_VENDOR_NAME_OFFSET, SFP_VENDOR_NAME_LEN - 1);
		strscpy(adapter->phy.vendor_pn,
			page_data + SFP_VENDOR_PN_OFFSET,
			SFP_VENDOR_NAME_LEN - 1);
	}

	return status;
}

static int lancer_cmd_delete_object(struct be_adapter *adapter,
				    const char *obj_name)
{
	struct lancer_cmd_req_delete_object *req;
	struct be_mcc_wrb *wrb;
	int status;

	mutex_lock(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}

	req = embedded_payload(wrb);

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
			       OPCODE_COMMON_DELETE_OBJECT,
			       sizeof(*req), wrb, NULL);

	strscpy(req->object_name, obj_name, sizeof(req->object_name));

	status = be_mcc_notify_wait(adapter);
err:
	mutex_unlock(&adapter->mcc_lock);
	return status;
}

int lancer_cmd_read_object(struct be_adapter *adapter, struct be_dma_mem *cmd,
			   u32 data_size, u32 data_offset, const char *obj_name,
			   u32 *data_read, u32 *eof, u8 *addn_status)
{
	struct be_mcc_wrb *wrb;
	struct lancer_cmd_req_read_object *req;
	struct lancer_cmd_resp_read_object *resp;
	int status;

	mutex_lock(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err_unlock;
	}

	req = embedded_payload(wrb);

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
			       OPCODE_COMMON_READ_OBJECT,
			       sizeof(struct lancer_cmd_req_read_object), wrb,
			       NULL);

	req->desired_read_len = cpu_to_le32(data_size);
	req->read_offset = cpu_to_le32(data_offset);
	strcpy(req->object_name, obj_name);
	req->descriptor_count = cpu_to_le32(1);
	req->buf_len = cpu_to_le32(data_size);
	req->addr_low = cpu_to_le32((cmd->dma & 0xFFFFFFFF));
	req->addr_high = cpu_to_le32(upper_32_bits(cmd->dma));

	status = be_mcc_notify_wait(adapter);

	resp = embedded_payload(wrb);
	if (!status) {
		*data_read = le32_to_cpu(resp->actual_read_len);
		*eof = le32_to_cpu(resp->eof);
	} else {
		*addn_status = resp->additional_status;
	}

err_unlock:
	mutex_unlock(&adapter->mcc_lock);
	return status;
}

static int be_cmd_write_flashrom(struct be_adapter *adapter,
				 struct be_dma_mem *cmd, u32 flash_type,
				 u32 flash_opcode, u32 img_offset, u32 buf_size)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_write_flashrom *req;
	int status;

	mutex_lock(&adapter->mcc_lock);
	adapter->flash_status = 0;

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err_unlock;
	}
	req = cmd->va;

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
			       OPCODE_COMMON_WRITE_FLASHROM, cmd->size, wrb,
			       cmd);

	req->params.op_type = cpu_to_le32(flash_type);
	if (flash_type == OPTYPE_OFFSET_SPECIFIED)
		req->params.offset = cpu_to_le32(img_offset);

	req->params.op_code = cpu_to_le32(flash_opcode);
	req->params.data_buf_size = cpu_to_le32(buf_size);

	status = be_mcc_notify(adapter);
	if (status)
		goto err_unlock;

	mutex_unlock(&adapter->mcc_lock);

	if (!wait_for_completion_timeout(&adapter->et_cmd_compl,
					 msecs_to_jiffies(40000)))
		status = -ETIMEDOUT;
	else
		status = adapter->flash_status;

	return status;

err_unlock:
	mutex_unlock(&adapter->mcc_lock);
	return status;
}

static int be_cmd_get_flash_crc(struct be_adapter *adapter, u8 *flashed_crc,
				u16 img_optype, u32 img_offset, u32 crc_offset)
{
	struct be_cmd_read_flash_crc *req;
	struct be_mcc_wrb *wrb;
	int status;

	mutex_lock(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}
	req = embedded_payload(wrb);

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
			       OPCODE_COMMON_READ_FLASHROM, sizeof(*req),
			       wrb, NULL);

	req->params.op_type = cpu_to_le32(img_optype);
	if (img_optype == OPTYPE_OFFSET_SPECIFIED)
		req->params.offset = cpu_to_le32(img_offset + crc_offset);
	else
		req->params.offset = cpu_to_le32(crc_offset);

	req->params.op_code = cpu_to_le32(FLASHROM_OPER_REPORT);
	req->params.data_buf_size = cpu_to_le32(0x4);

	status = be_mcc_notify_wait(adapter);
	if (!status)
		memcpy(flashed_crc, req->crc, 4);

err:
	mutex_unlock(&adapter->mcc_lock);
	return status;
}

static char flash_cookie[2][16] = {"*** SE FLAS", "H DIRECTORY *** "};

static bool phy_flashing_required(struct be_adapter *adapter)
{
	return (adapter->phy.phy_type == PHY_TYPE_TN_8022 &&
		adapter->phy.interface_type == PHY_TYPE_BASET_10GB);
}

static bool is_comp_in_ufi(struct be_adapter *adapter,
			   struct flash_section_info *fsec, int type)
{
	int i = 0, img_type = 0;
	struct flash_section_info_g2 *fsec_g2 = NULL;

	if (BE2_chip(adapter))
		fsec_g2 = (struct flash_section_info_g2 *)fsec;

	for (i = 0; i < MAX_FLASH_COMP; i++) {
		if (fsec_g2)
			img_type = le32_to_cpu(fsec_g2->fsec_entry[i].type);
		else
			img_type = le32_to_cpu(fsec->fsec_entry[i].type);

		if (img_type == type)
			return true;
	}
	return false;
}

static struct flash_section_info *get_fsec_info(struct be_adapter *adapter,
						int header_size,
						const struct firmware *fw)
{
	struct flash_section_info *fsec = NULL;
	const u8 *p = fw->data;

	p += header_size;
	while (p < (fw->data + fw->size)) {
		fsec = (struct flash_section_info *)p;
		if (!memcmp(flash_cookie, fsec->cookie, sizeof(flash_cookie)))
			return fsec;
		p += 32;
	}
	return NULL;
}

static int be_check_flash_crc(struct be_adapter *adapter, const u8 *p,
			      u32 img_offset, u32 img_size, int hdr_size,
			      u16 img_optype, bool *crc_match)
{
	u32 crc_offset;
	int status;
	u8 crc[4];

	status = be_cmd_get_flash_crc(adapter, crc, img_optype, img_offset,
				      img_size - 4);
	if (status)
		return status;

	crc_offset = hdr_size + img_offset + img_size - 4;

	/* Skip flashing, if crc of flashed region matches */
	if (!memcmp(crc, p + crc_offset, 4))
		*crc_match = true;
	else
		*crc_match = false;

	return status;
}

static int be_flash(struct be_adapter *adapter, const u8 *img,
		    struct be_dma_mem *flash_cmd, int optype, int img_size,
		    u32 img_offset)
{
	u32 flash_op, num_bytes, total_bytes = img_size, bytes_sent = 0;
	struct be_cmd_write_flashrom *req = flash_cmd->va;
	int status;

	while (total_bytes) {
		num_bytes = min_t(u32, 32 * 1024, total_bytes);

		total_bytes -= num_bytes;

		if (!total_bytes) {
			if (optype == OPTYPE_PHY_FW)
				flash_op = FLASHROM_OPER_PHY_FLASH;
			else
				flash_op = FLASHROM_OPER_FLASH;
		} else {
			if (optype == OPTYPE_PHY_FW)
				flash_op = FLASHROM_OPER_PHY_SAVE;
			else
				flash_op = FLASHROM_OPER_SAVE;
		}

		memcpy(req->data_buf, img, num_bytes);
		img += num_bytes;
		status = be_cmd_write_flashrom(adapter, flash_cmd, optype,
					       flash_op, img_offset +
					       bytes_sent, num_bytes);
		if (base_status(status) == MCC_STATUS_ILLEGAL_REQUEST &&
		    optype == OPTYPE_PHY_FW)
			break;
		else if (status)
			return status;

		bytes_sent += num_bytes;
	}
	return 0;
}

#define NCSI_UPDATE_LOG	"NCSI section update is not supported in FW ver %s\n"
static bool be_fw_ncsi_supported(char *ver)
{
	int v1[4] = {3, 102, 148, 0}; /* Min ver that supports NCSI FW */
	int v2[4];
	int i;

	if (sscanf(ver, "%d.%d.%d.%d", &v2[0], &v2[1], &v2[2], &v2[3]) != 4)
		return false;

	for (i = 0; i < 4; i++) {
		if (v1[i] < v2[i])
			return true;
		else if (v1[i] > v2[i])
			return false;
	}

	return true;
}

/* For BE2, BE3 and BE3-R */
static int be_flash_BEx(struct be_adapter *adapter,
			const struct firmware *fw,
			struct be_dma_mem *flash_cmd, int num_of_images)
{
	int img_hdrs_size = (num_of_images * sizeof(struct image_hdr));
	struct device *dev = &adapter->pdev->dev;
	struct flash_section_info *fsec = NULL;
	int status, i, filehdr_size, num_comp;
	const struct flash_comp *pflashcomp;
	bool crc_match;
	const u8 *p;

	static const struct flash_comp gen3_flash_types[] = {
		{ BE3_ISCSI_PRIMARY_IMAGE_START, OPTYPE_ISCSI_ACTIVE,
			BE3_COMP_MAX_SIZE, IMAGE_FIRMWARE_ISCSI},
		{ BE3_REDBOOT_START, OPTYPE_REDBOOT,
			BE3_REDBOOT_COMP_MAX_SIZE, IMAGE_BOOT_CODE},
		{ BE3_ISCSI_BIOS_START, OPTYPE_BIOS,
			BE3_BIOS_COMP_MAX_SIZE, IMAGE_OPTION_ROM_ISCSI},
		{ BE3_PXE_BIOS_START, OPTYPE_PXE_BIOS,
			BE3_BIOS_COMP_MAX_SIZE, IMAGE_OPTION_ROM_PXE},
		{ BE3_FCOE_BIOS_START, OPTYPE_FCOE_BIOS,
			BE3_BIOS_COMP_MAX_SIZE, IMAGE_OPTION_ROM_FCOE},
		{ BE3_ISCSI_BACKUP_IMAGE_START, OPTYPE_ISCSI_BACKUP,
			BE3_COMP_MAX_SIZE, IMAGE_FIRMWARE_BACKUP_ISCSI},
		{ BE3_FCOE_PRIMARY_IMAGE_START, OPTYPE_FCOE_FW_ACTIVE,
			BE3_COMP_MAX_SIZE, IMAGE_FIRMWARE_FCOE},
		{ BE3_FCOE_BACKUP_IMAGE_START, OPTYPE_FCOE_FW_BACKUP,
			BE3_COMP_MAX_SIZE, IMAGE_FIRMWARE_BACKUP_FCOE},
		{ BE3_NCSI_START, OPTYPE_NCSI_FW,
			BE3_NCSI_COMP_MAX_SIZE, IMAGE_NCSI},
		{ BE3_PHY_FW_START, OPTYPE_PHY_FW,
			BE3_PHY_FW_COMP_MAX_SIZE, IMAGE_FIRMWARE_PHY}
	};

	static const struct flash_comp gen2_flash_types[] = {
		{ BE2_ISCSI_PRIMARY_IMAGE_START, OPTYPE_ISCSI_ACTIVE,
			BE2_COMP_MAX_SIZE, IMAGE_FIRMWARE_ISCSI},
		{ BE2_REDBOOT_START, OPTYPE_REDBOOT,
			BE2_REDBOOT_COMP_MAX_SIZE, IMAGE_BOOT_CODE},
		{ BE2_ISCSI_BIOS_START, OPTYPE_BIOS,
			BE2_BIOS_COMP_MAX_SIZE, IMAGE_OPTION_ROM_ISCSI},
		{ BE2_PXE_BIOS_START, OPTYPE_PXE_BIOS,
			BE2_BIOS_COMP_MAX_SIZE, IMAGE_OPTION_ROM_PXE},
		{ BE2_FCOE_BIOS_START, OPTYPE_FCOE_BIOS,
			BE2_BIOS_COMP_MAX_SIZE, IMAGE_OPTION_ROM_FCOE},
		{ BE2_ISCSI_BACKUP_IMAGE_START, OPTYPE_ISCSI_BACKUP,
			BE2_COMP_MAX_SIZE, IMAGE_FIRMWARE_BACKUP_ISCSI},
		{ BE2_FCOE_PRIMARY_IMAGE_START, OPTYPE_FCOE_FW_ACTIVE,
			BE2_COMP_MAX_SIZE, IMAGE_FIRMWARE_FCOE},
		{ BE2_FCOE_BACKUP_IMAGE_START, OPTYPE_FCOE_FW_BACKUP,
			 BE2_COMP_MAX_SIZE, IMAGE_FIRMWARE_BACKUP_FCOE}
	};

	if (BE3_chip(adapter)) {
		pflashcomp = gen3_flash_types;
		filehdr_size = sizeof(struct flash_file_hdr_g3);
		num_comp = ARRAY_SIZE(gen3_flash_types);
	} else {
		pflashcomp = gen2_flash_types;
		filehdr_size = sizeof(struct flash_file_hdr_g2);
		num_comp = ARRAY_SIZE(gen2_flash_types);
		img_hdrs_size = 0;
	}

	/* Get flash section info*/
	fsec = get_fsec_info(adapter, filehdr_size + img_hdrs_size, fw);
	if (!fsec) {
		dev_err(dev, "Invalid Cookie. FW image may be corrupted\n");
		return -1;
	}
	for (i = 0; i < num_comp; i++) {
		if (!is_comp_in_ufi(adapter, fsec, pflashcomp[i].img_type))
			continue;

		if ((pflashcomp[i].optype == OPTYPE_NCSI_FW) &&
		    !be_fw_ncsi_supported(adapter->fw_ver)) {
			dev_info(dev, NCSI_UPDATE_LOG, adapter->fw_ver);
			continue;
		}

		if (pflashcomp[i].optype == OPTYPE_PHY_FW  &&
		    !phy_flashing_required(adapter))
			continue;

		if (pflashcomp[i].optype == OPTYPE_REDBOOT) {
			status = be_check_flash_crc(adapter, fw->data,
						    pflashcomp[i].offset,
						    pflashcomp[i].size,
						    filehdr_size +
						    img_hdrs_size,
						    OPTYPE_REDBOOT, &crc_match);
			if (status) {
				dev_err(dev,
					"Could not get CRC for 0x%x region\n",
					pflashcomp[i].optype);
				continue;
			}

			if (crc_match)
				continue;
		}

		p = fw->data + filehdr_size + pflashcomp[i].offset +
			img_hdrs_size;
		if (p + pflashcomp[i].size > fw->data + fw->size)
			return -1;

		status = be_flash(adapter, p, flash_cmd, pflashcomp[i].optype,
				  pflashcomp[i].size, 0);
		if (status) {
			dev_err(dev, "Flashing section type 0x%x failed\n",
				pflashcomp[i].img_type);
			return status;
		}
	}
	return 0;
}

static u16 be_get_img_optype(struct flash_section_entry fsec_entry)
{
	u32 img_type = le32_to_cpu(fsec_entry.type);
	u16 img_optype = le16_to_cpu(fsec_entry.optype);

	if (img_optype != 0xFFFF)
		return img_optype;

	switch (img_type) {
	case IMAGE_FIRMWARE_ISCSI:
		img_optype = OPTYPE_ISCSI_ACTIVE;
		break;
	case IMAGE_BOOT_CODE:
		img_optype = OPTYPE_REDBOOT;
		break;
	case IMAGE_OPTION_ROM_ISCSI:
		img_optype = OPTYPE_BIOS;
		break;
	case IMAGE_OPTION_ROM_PXE:
		img_optype = OPTYPE_PXE_BIOS;
		break;
	case IMAGE_OPTION_ROM_FCOE:
		img_optype = OPTYPE_FCOE_BIOS;
		break;
	case IMAGE_FIRMWARE_BACKUP_ISCSI:
		img_optype = OPTYPE_ISCSI_BACKUP;
		break;
	case IMAGE_NCSI:
		img_optype = OPTYPE_NCSI_FW;
		break;
	case IMAGE_FLASHISM_JUMPVECTOR:
		img_optype = OPTYPE_FLASHISM_JUMPVECTOR;
		break;
	case IMAGE_FIRMWARE_PHY:
		img_optype = OPTYPE_SH_PHY_FW;
		break;
	case IMAGE_REDBOOT_DIR:
		img_optype = OPTYPE_REDBOOT_DIR;
		break;
	case IMAGE_REDBOOT_CONFIG:
		img_optype = OPTYPE_REDBOOT_CONFIG;
		break;
	case IMAGE_UFI_DIR:
		img_optype = OPTYPE_UFI_DIR;
		break;
	default:
		break;
	}

	return img_optype;
}

static int be_flash_skyhawk(struct be_adapter *adapter,
			    const struct firmware *fw,
			    struct be_dma_mem *flash_cmd, int num_of_images)
{
	int img_hdrs_size = num_of_images * sizeof(struct image_hdr);
	bool crc_match, old_fw_img, flash_offset_support = true;
	struct device *dev = &adapter->pdev->dev;
	struct flash_section_info *fsec = NULL;
	u32 img_offset, img_size, img_type;
	u16 img_optype, flash_optype;
	int status, i, filehdr_size;
	const u8 *p;

	filehdr_size = sizeof(struct flash_file_hdr_g3);
	fsec = get_fsec_info(adapter, filehdr_size + img_hdrs_size, fw);
	if (!fsec) {
		dev_err(dev, "Invalid Cookie. FW image may be corrupted\n");
		return -EINVAL;
	}

retry_flash:
	for (i = 0; i < le32_to_cpu(fsec->fsec_hdr.num_images); i++) {
		img_offset = le32_to_cpu(fsec->fsec_entry[i].offset);
		img_size   = le32_to_cpu(fsec->fsec_entry[i].pad_size);
		img_type   = le32_to_cpu(fsec->fsec_entry[i].type);
		img_optype = be_get_img_optype(fsec->fsec_entry[i]);
		old_fw_img = fsec->fsec_entry[i].optype == 0xFFFF;

		if (img_optype == 0xFFFF)
			continue;

		if (flash_offset_support)
			flash_optype = OPTYPE_OFFSET_SPECIFIED;
		else
			flash_optype = img_optype;

		/* Don't bother verifying CRC if an old FW image is being
		 * flashed
		 */
		if (old_fw_img)
			goto flash;

		status = be_check_flash_crc(adapter, fw->data, img_offset,
					    img_size, filehdr_size +
					    img_hdrs_size, flash_optype,
					    &crc_match);
		if (base_status(status) == MCC_STATUS_ILLEGAL_REQUEST ||
		    base_status(status) == MCC_STATUS_ILLEGAL_FIELD) {
			/* The current FW image on the card does not support
			 * OFFSET based flashing. Retry using older mechanism
			 * of OPTYPE based flashing
			 */
			if (flash_optype == OPTYPE_OFFSET_SPECIFIED) {
				flash_offset_support = false;
				goto retry_flash;
			}

			/* The current FW image on the card does not recognize
			 * the new FLASH op_type. The FW download is partially
			 * complete. Reboot the server now to enable FW image
			 * to recognize the new FLASH op_type. To complete the
			 * remaining process, download the same FW again after
			 * the reboot.
			 */
			dev_err(dev, "Flash incomplete. Reset the server\n");
			dev_err(dev, "Download FW image again after reset\n");
			return -EAGAIN;
		} else if (status) {
			dev_err(dev, "Could not get CRC for 0x%x region\n",
				img_optype);
			return -EFAULT;
		}

		if (crc_match)
			continue;

flash:
		p = fw->data + filehdr_size + img_offset + img_hdrs_size;
		if (p + img_size > fw->data + fw->size)
			return -1;

		status = be_flash(adapter, p, flash_cmd, flash_optype, img_size,
				  img_offset);

		/* The current FW image on the card does not support OFFSET
		 * based flashing. Retry using older mechanism of OPTYPE based
		 * flashing
		 */
		if (base_status(status) == MCC_STATUS_ILLEGAL_FIELD &&
		    flash_optype == OPTYPE_OFFSET_SPECIFIED) {
			flash_offset_support = false;
			goto retry_flash;
		}

		/* For old FW images ignore ILLEGAL_FIELD error or errors on
		 * UFI_DIR region
		 */
		if (old_fw_img &&
		    (base_status(status) == MCC_STATUS_ILLEGAL_FIELD ||
		     (img_optype == OPTYPE_UFI_DIR &&
		      base_status(status) == MCC_STATUS_FAILED))) {
			continue;
		} else if (status) {
			dev_err(dev, "Flashing section type 0x%x failed\n",
				img_type);

			switch (addl_status(status)) {
			case MCC_ADDL_STATUS_MISSING_SIGNATURE:
				dev_err(dev,
					"Digital signature missing in FW\n");
				return -EINVAL;
			case MCC_ADDL_STATUS_INVALID_SIGNATURE:
				dev_err(dev,
					"Invalid digital signature in FW\n");
				return -EINVAL;
			default:
				return -EFAULT;
			}
		}
	}
	return 0;
}

int lancer_fw_download(struct be_adapter *adapter,
		       const struct firmware *fw)
{
	struct device *dev = &adapter->pdev->dev;
	struct be_dma_mem flash_cmd;
	const u8 *data_ptr = NULL;
	u8 *dest_image_ptr = NULL;
	size_t image_size = 0;
	u32 chunk_size = 0;
	u32 data_written = 0;
	u32 offset = 0;
	int status = 0;
	u8 add_status = 0;
	u8 change_status;

	if (!IS_ALIGNED(fw->size, sizeof(u32))) {
		dev_err(dev, "FW image size should be multiple of 4\n");
		return -EINVAL;
	}

	flash_cmd.size = sizeof(struct lancer_cmd_req_write_object)
				+ LANCER_FW_DOWNLOAD_CHUNK;
	flash_cmd.va = dma_alloc_coherent(dev, flash_cmd.size, &flash_cmd.dma,
					  GFP_KERNEL);
	if (!flash_cmd.va)
		return -ENOMEM;

	dest_image_ptr = flash_cmd.va +
				sizeof(struct lancer_cmd_req_write_object);
	image_size = fw->size;
	data_ptr = fw->data;

	while (image_size) {
		chunk_size = min_t(u32, image_size, LANCER_FW_DOWNLOAD_CHUNK);

		/* Copy the image chunk content. */
		memcpy(dest_image_ptr, data_ptr, chunk_size);

		status = lancer_cmd_write_object(adapter, &flash_cmd,
						 chunk_size, offset,
						 LANCER_FW_DOWNLOAD_LOCATION,
						 &data_written, &change_status,
						 &add_status);
		if (status)
			break;

		offset += data_written;
		data_ptr += data_written;
		image_size -= data_written;
	}

	if (!status) {
		/* Commit the FW written */
		status = lancer_cmd_write_object(adapter, &flash_cmd,
						 0, offset,
						 LANCER_FW_DOWNLOAD_LOCATION,
						 &data_written, &change_status,
						 &add_status);
	}

	dma_free_coherent(dev, flash_cmd.size, flash_cmd.va, flash_cmd.dma);
	if (status) {
		dev_err(dev, "Firmware load error\n");
		return be_cmd_status(status);
	}

	dev_info(dev, "Firmware flashed successfully\n");

	if (change_status == LANCER_FW_RESET_NEEDED) {
		dev_info(dev, "Resetting adapter to activate new FW\n");
		status = lancer_physdev_ctrl(adapter,
					     PHYSDEV_CONTROL_FW_RESET_MASK);
		if (status) {
			dev_err(dev, "Adapter busy, could not reset FW\n");
			dev_err(dev, "Reboot server to activate new FW\n");
		}
	} else if (change_status != LANCER_NO_RESET_NEEDED) {
		dev_info(dev, "Reboot server to activate new FW\n");
	}

	return 0;
}

/* Check if the flash image file is compatible with the adapter that
 * is being flashed.
 */
static bool be_check_ufi_compatibility(struct be_adapter *adapter,
				       struct flash_file_hdr_g3 *fhdr)
{
	if (!fhdr) {
		dev_err(&adapter->pdev->dev, "Invalid FW UFI file");
		return false;
	}

	/* First letter of the build version is used to identify
	 * which chip this image file is meant for.
	 */
	switch (fhdr->build[0]) {
	case BLD_STR_UFI_TYPE_SH:
		if (!skyhawk_chip(adapter))
			return false;
		break;
	case BLD_STR_UFI_TYPE_BE3:
		if (!BE3_chip(adapter))
			return false;
		break;
	case BLD_STR_UFI_TYPE_BE2:
		if (!BE2_chip(adapter))
			return false;
		break;
	default:
		return false;
	}

	/* In BE3 FW images the "asic_type_rev" field doesn't track the
	 * asic_rev of the chips it is compatible with.
	 * When asic_type_rev is 0 the image is compatible only with
	 * pre-BE3-R chips (asic_rev < 0x10)
	 */
	if (BEx_chip(adapter) && fhdr->asic_type_rev == 0)
		return adapter->asic_rev < 0x10;
	else
		return (fhdr->asic_type_rev >= adapter->asic_rev);
}

int be_fw_download(struct be_adapter *adapter, const struct firmware *fw)
{
	struct device *dev = &adapter->pdev->dev;
	struct flash_file_hdr_g3 *fhdr3;
	struct image_hdr *img_hdr_ptr;
	int status = 0, i, num_imgs;
	struct be_dma_mem flash_cmd;

	fhdr3 = (struct flash_file_hdr_g3 *)fw->data;
	if (!be_check_ufi_compatibility(adapter, fhdr3)) {
		dev_err(dev, "Flash image is not compatible with adapter\n");
		return -EINVAL;
	}

	flash_cmd.size = sizeof(struct be_cmd_write_flashrom);
	flash_cmd.va = dma_alloc_coherent(dev, flash_cmd.size, &flash_cmd.dma,
					  GFP_KERNEL);
	if (!flash_cmd.va)
		return -ENOMEM;

	num_imgs = le32_to_cpu(fhdr3->num_imgs);
	for (i = 0; i < num_imgs; i++) {
		img_hdr_ptr = (struct image_hdr *)(fw->data +
				(sizeof(struct flash_file_hdr_g3) +
				 i * sizeof(struct image_hdr)));
		if (!BE2_chip(adapter) &&
		    le32_to_cpu(img_hdr_ptr->imageid) != 1)
			continue;

		if (skyhawk_chip(adapter))
			status = be_flash_skyhawk(adapter, fw, &flash_cmd,
						  num_imgs);
		else
			status = be_flash_BEx(adapter, fw, &flash_cmd,
					      num_imgs);
	}

	dma_free_coherent(dev, flash_cmd.size, flash_cmd.va, flash_cmd.dma);
	if (!status)
		dev_info(dev, "Firmware flashed successfully\n");

	return status;
}

int be_cmd_enable_magic_wol(struct be_adapter *adapter, u8 *mac,
			    struct be_dma_mem *nonemb_cmd)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_acpi_wol_magic_config *req;
	int status;

	mutex_lock(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}
	req = nonemb_cmd->va;

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_ETH,
			       OPCODE_ETH_ACPI_WOL_MAGIC_CONFIG, sizeof(*req),
			       wrb, nonemb_cmd);
	memcpy(req->magic_mac, mac, ETH_ALEN);

	status = be_mcc_notify_wait(adapter);

err:
	mutex_unlock(&adapter->mcc_lock);
	return status;
}

int be_cmd_set_loopback(struct be_adapter *adapter, u8 port_num,
			u8 loopback_type, u8 enable)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_set_lmode *req;
	int status;

	if (!be_cmd_allowed(adapter, OPCODE_LOWLEVEL_SET_LOOPBACK_MODE,
			    CMD_SUBSYSTEM_LOWLEVEL))
		return -EPERM;

	mutex_lock(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err_unlock;
	}

	req = embedded_payload(wrb);

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_LOWLEVEL,
			       OPCODE_LOWLEVEL_SET_LOOPBACK_MODE, sizeof(*req),
			       wrb, NULL);

	req->src_port = port_num;
	req->dest_port = port_num;
	req->loopback_type = loopback_type;
	req->loopback_state = enable;

	status = be_mcc_notify(adapter);
	if (status)
		goto err_unlock;

	mutex_unlock(&adapter->mcc_lock);

	if (!wait_for_completion_timeout(&adapter->et_cmd_compl,
					 msecs_to_jiffies(SET_LB_MODE_TIMEOUT)))
		status = -ETIMEDOUT;

	return status;

err_unlock:
	mutex_unlock(&adapter->mcc_lock);
	return status;
}

int be_cmd_loopback_test(struct be_adapter *adapter, u32 port_num,
			 u32 loopback_type, u32 pkt_size, u32 num_pkts,
			 u64 pattern)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_loopback_test *req;
	struct be_cmd_resp_loopback_test *resp;
	int status;

	if (!be_cmd_allowed(adapter, OPCODE_LOWLEVEL_LOOPBACK_TEST,
			    CMD_SUBSYSTEM_LOWLEVEL))
		return -EPERM;

	mutex_lock(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}

	req = embedded_payload(wrb);

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_LOWLEVEL,
			       OPCODE_LOWLEVEL_LOOPBACK_TEST, sizeof(*req), wrb,
			       NULL);

	req->hdr.timeout = cpu_to_le32(15);
	req->pattern = cpu_to_le64(pattern);
	req->src_port = cpu_to_le32(port_num);
	req->dest_port = cpu_to_le32(port_num);
	req->pkt_size = cpu_to_le32(pkt_size);
	req->num_pkts = cpu_to_le32(num_pkts);
	req->loopback_type = cpu_to_le32(loopback_type);

	status = be_mcc_notify(adapter);
	if (status)
		goto err;

	mutex_unlock(&adapter->mcc_lock);

	wait_for_completion(&adapter->et_cmd_compl);
	resp = embedded_payload(wrb);
	status = le32_to_cpu(resp->status);

	return status;
err:
	mutex_unlock(&adapter->mcc_lock);
	return status;
}

int be_cmd_ddr_dma_test(struct be_adapter *adapter, u64 pattern,
			u32 byte_cnt, struct be_dma_mem *cmd)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_ddrdma_test *req;
	int status;
	int i, j = 0;

	if (!be_cmd_allowed(adapter, OPCODE_LOWLEVEL_HOST_DDR_DMA,
			    CMD_SUBSYSTEM_LOWLEVEL))
		return -EPERM;

	mutex_lock(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}
	req = cmd->va;
	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_LOWLEVEL,
			       OPCODE_LOWLEVEL_HOST_DDR_DMA, cmd->size, wrb,
			       cmd);

	req->pattern = cpu_to_le64(pattern);
	req->byte_count = cpu_to_le32(byte_cnt);
	for (i = 0; i < byte_cnt; i++) {
		req->snd_buff[i] = (u8)(pattern >> (j*8));
		j++;
		if (j > 7)
			j = 0;
	}

	status = be_mcc_notify_wait(adapter);

	if (!status) {
		struct be_cmd_resp_ddrdma_test *resp;

		resp = cmd->va;
		if ((memcmp(resp->rcv_buff, req->snd_buff, byte_cnt) != 0) ||
		    resp->snd_err) {
			status = -1;
		}
	}

err:
	mutex_unlock(&adapter->mcc_lock);
	return status;
}

int be_cmd_get_seeprom_data(struct be_adapter *adapter,
			    struct be_dma_mem *nonemb_cmd)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_seeprom_read *req;
	int status;

	mutex_lock(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}
	req = nonemb_cmd->va;

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
			       OPCODE_COMMON_SEEPROM_READ, sizeof(*req), wrb,
			       nonemb_cmd);

	status = be_mcc_notify_wait(adapter);

err:
	mutex_unlock(&adapter->mcc_lock);
	return status;
}

int be_cmd_get_phy_info(struct be_adapter *adapter)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_get_phy_info *req;
	struct be_dma_mem cmd;
	int status;

	if (!be_cmd_allowed(adapter, OPCODE_COMMON_GET_PHY_DETAILS,
			    CMD_SUBSYSTEM_COMMON))
		return -EPERM;

	mutex_lock(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}
	cmd.size = sizeof(struct be_cmd_req_get_phy_info);
	cmd.va = dma_alloc_coherent(&adapter->pdev->dev, cmd.size, &cmd.dma,
				    GFP_ATOMIC);
	if (!cmd.va) {
		dev_err(&adapter->pdev->dev, "Memory alloc failure\n");
		status = -ENOMEM;
		goto err;
	}

	req = cmd.va;

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
			       OPCODE_COMMON_GET_PHY_DETAILS, sizeof(*req),
			       wrb, &cmd);

	status = be_mcc_notify_wait(adapter);
	if (!status) {
		struct be_phy_info *resp_phy_info =
				cmd.va + sizeof(struct be_cmd_req_hdr);

		adapter->phy.phy_type = le16_to_cpu(resp_phy_info->phy_type);
		adapter->phy.interface_type =
			le16_to_cpu(resp_phy_info->interface_type);
		adapter->phy.auto_speeds_supported =
			le16_to_cpu(resp_phy_info->auto_speeds_supported);
		adapter->phy.fixed_speeds_supported =
			le16_to_cpu(resp_phy_info->fixed_speeds_supported);
		adapter->phy.misc_params =
			le32_to_cpu(resp_phy_info->misc_params);

		if (BE2_chip(adapter)) {
			adapter->phy.fixed_speeds_supported =
				BE_SUPPORTED_SPEED_10GBPS |
				BE_SUPPORTED_SPEED_1GBPS;
		}
	}
	dma_free_coherent(&adapter->pdev->dev, cmd.size, cmd.va, cmd.dma);
err:
	mutex_unlock(&adapter->mcc_lock);
	return status;
}

static int be_cmd_set_qos(struct be_adapter *adapter, u32 bps, u32 domain)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_set_qos *req;
	int status;

	mutex_lock(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}

	req = embedded_payload(wrb);

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
			       OPCODE_COMMON_SET_QOS, sizeof(*req), wrb, NULL);

	req->hdr.domain = domain;
	req->valid_bits = cpu_to_le32(BE_QOS_BITS_NIC);
	req->max_bps_nic = cpu_to_le32(bps);

	status = be_mcc_notify_wait(adapter);

err:
	mutex_unlock(&adapter->mcc_lock);
	return status;
}

int be_cmd_get_cntl_attributes(struct be_adapter *adapter)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_cntl_attribs *req;
	struct be_cmd_resp_cntl_attribs *resp;
	int status, i;
	int payload_len = max(sizeof(*req), sizeof(*resp));
	struct mgmt_controller_attrib *attribs;
	struct be_dma_mem attribs_cmd;
	u32 *serial_num;

	if (mutex_lock_interruptible(&adapter->mbox_lock))
		return -1;

	memset(&attribs_cmd, 0, sizeof(struct be_dma_mem));
	attribs_cmd.size = sizeof(struct be_cmd_resp_cntl_attribs);
	attribs_cmd.va = dma_alloc_coherent(&adapter->pdev->dev,
					    attribs_cmd.size,
					    &attribs_cmd.dma, GFP_ATOMIC);
	if (!attribs_cmd.va) {
		dev_err(&adapter->pdev->dev, "Memory allocation failure\n");
		status = -ENOMEM;
		goto err;
	}

	wrb = wrb_from_mbox(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}
	req = attribs_cmd.va;

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
			       OPCODE_COMMON_GET_CNTL_ATTRIBUTES, payload_len,
			       wrb, &attribs_cmd);

	status = be_mbox_notify_wait(adapter);
	if (!status) {
		attribs = attribs_cmd.va + sizeof(struct be_cmd_resp_hdr);
		adapter->hba_port_num = attribs->hba_attribs.phy_port;
		serial_num = attribs->hba_attribs.controller_serial_number;
		for (i = 0; i < CNTL_SERIAL_NUM_WORDS; i++)
			adapter->serial_num[i] = le32_to_cpu(serial_num[i]) &
				(BIT_MASK(16) - 1);
		/* For BEx, since GET_FUNC_CONFIG command is not
		 * supported, we read funcnum here as a workaround.
		 */
		if (BEx_chip(adapter))
			adapter->pf_num = attribs->hba_attribs.pci_funcnum;
	}

err:
	mutex_unlock(&adapter->mbox_lock);
	if (attribs_cmd.va)
		dma_free_coherent(&adapter->pdev->dev, attribs_cmd.size,
				  attribs_cmd.va, attribs_cmd.dma);
	return status;
}

/* Uses mbox */
int be_cmd_req_native_mode(struct be_adapter *adapter)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_set_func_cap *req;
	int status;

	if (mutex_lock_interruptible(&adapter->mbox_lock))
		return -1;

	wrb = wrb_from_mbox(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}

	req = embedded_payload(wrb);

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
			       OPCODE_COMMON_SET_DRIVER_FUNCTION_CAP,
			       sizeof(*req), wrb, NULL);

	req->valid_cap_flags = cpu_to_le32(CAPABILITY_SW_TIMESTAMPS |
				CAPABILITY_BE3_NATIVE_ERX_API);
	req->cap_flags = cpu_to_le32(CAPABILITY_BE3_NATIVE_ERX_API);

	status = be_mbox_notify_wait(adapter);
	if (!status) {
		struct be_cmd_resp_set_func_cap *resp = embedded_payload(wrb);

		adapter->be3_native = le32_to_cpu(resp->cap_flags) &
					CAPABILITY_BE3_NATIVE_ERX_API;
		if (!adapter->be3_native)
			dev_warn(&adapter->pdev->dev,
				 "adapter not in advanced mode\n");
	}
err:
	mutex_unlock(&adapter->mbox_lock);
	return status;
}

/* Get privilege(s) for a function */
int be_cmd_get_fn_privileges(struct be_adapter *adapter, u32 *privilege,
			     u32 domain)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_get_fn_privileges *req;
	int status;

	mutex_lock(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}

	req = embedded_payload(wrb);

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
			       OPCODE_COMMON_GET_FN_PRIVILEGES, sizeof(*req),
			       wrb, NULL);

	req->hdr.domain = domain;

	status = be_mcc_notify_wait(adapter);
	if (!status) {
		struct be_cmd_resp_get_fn_privileges *resp =
						embedded_payload(wrb);

		*privilege = le32_to_cpu(resp->privilege_mask);

		/* In UMC mode FW does not return right privileges.
		 * Override with correct privilege equivalent to PF.
		 */
		if (BEx_chip(adapter) && be_is_mc(adapter) &&
		    be_physfn(adapter))
			*privilege = MAX_PRIVILEGES;
	}

err:
	mutex_unlock(&adapter->mcc_lock);
	return status;
}

/* Set privilege(s) for a function */
int be_cmd_set_fn_privileges(struct be_adapter *adapter, u32 privileges,
			     u32 domain)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_set_fn_privileges *req;
	int status;

	mutex_lock(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}

	req = embedded_payload(wrb);
	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
			       OPCODE_COMMON_SET_FN_PRIVILEGES, sizeof(*req),
			       wrb, NULL);
	req->hdr.domain = domain;
	if (lancer_chip(adapter))
		req->privileges_lancer = cpu_to_le32(privileges);
	else
		req->privileges = cpu_to_le32(privileges);

	status = be_mcc_notify_wait(adapter);
err:
	mutex_unlock(&adapter->mcc_lock);
	return status;
}

/* pmac_id_valid: true => pmac_id is supplied and MAC address is requested.
 * pmac_id_valid: false => pmac_id or MAC address is requested.
 *		  If pmac_id is returned, pmac_id_valid is returned as true
 */
int be_cmd_get_mac_from_list(struct be_adapter *adapter, u8 *mac,
			     bool *pmac_id_valid, u32 *pmac_id, u32 if_handle,
			     u8 domain)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_get_mac_list *req;
	int status;
	int mac_count;
	struct be_dma_mem get_mac_list_cmd;
	int i;

	memset(&get_mac_list_cmd, 0, sizeof(struct be_dma_mem));
	get_mac_list_cmd.size = sizeof(struct be_cmd_resp_get_mac_list);
	get_mac_list_cmd.va = dma_alloc_coherent(&adapter->pdev->dev,
						 get_mac_list_cmd.size,
						 &get_mac_list_cmd.dma,
						 GFP_ATOMIC);

	if (!get_mac_list_cmd.va) {
		dev_err(&adapter->pdev->dev,
			"Memory allocation failure during GET_MAC_LIST\n");
		return -ENOMEM;
	}

	mutex_lock(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto out;
	}

	req = get_mac_list_cmd.va;

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
			       OPCODE_COMMON_GET_MAC_LIST,
			       get_mac_list_cmd.size, wrb, &get_mac_list_cmd);
	req->hdr.domain = domain;
	req->mac_type = MAC_ADDRESS_TYPE_NETWORK;
	if (*pmac_id_valid) {
		req->mac_id = cpu_to_le32(*pmac_id);
		req->iface_id = cpu_to_le16(if_handle);
		req->perm_override = 0;
	} else {
		req->perm_override = 1;
	}

	status = be_mcc_notify_wait(adapter);
	if (!status) {
		struct be_cmd_resp_get_mac_list *resp =
						get_mac_list_cmd.va;

		if (*pmac_id_valid) {
			memcpy(mac, resp->macid_macaddr.mac_addr_id.macaddr,
			       ETH_ALEN);
			goto out;
		}

		mac_count = resp->true_mac_count + resp->pseudo_mac_count;
		/* Mac list returned could contain one or more active mac_ids
		 * or one or more true or pseudo permanent mac addresses.
		 * If an active mac_id is present, return first active mac_id
		 * found.
		 */
		for (i = 0; i < mac_count; i++) {
			struct get_list_macaddr *mac_entry;
			u16 mac_addr_size;
			u32 mac_id;

			mac_entry = &resp->macaddr_list[i];
			mac_addr_size = le16_to_cpu(mac_entry->mac_addr_size);
			/* mac_id is a 32 bit value and mac_addr size
			 * is 6 bytes
			 */
			if (mac_addr_size == sizeof(u32)) {
				*pmac_id_valid = true;
				mac_id = mac_entry->mac_addr_id.s_mac_id.mac_id;
				*pmac_id = le32_to_cpu(mac_id);
				goto out;
			}
		}
		/* If no active mac_id found, return first mac addr */
		*pmac_id_valid = false;
		memcpy(mac, resp->macaddr_list[0].mac_addr_id.macaddr,
		       ETH_ALEN);
	}

out:
	mutex_unlock(&adapter->mcc_lock);
	dma_free_coherent(&adapter->pdev->dev, get_mac_list_cmd.size,
			  get_mac_list_cmd.va, get_mac_list_cmd.dma);
	return status;
}

int be_cmd_get_active_mac(struct be_adapter *adapter, u32 curr_pmac_id,
			  u8 *mac, u32 if_handle, bool active, u32 domain)
{
	if (!active)
		be_cmd_get_mac_from_list(adapter, mac, &active, &curr_pmac_id,
					 if_handle, domain);
	if (BEx_chip(adapter))
		return be_cmd_mac_addr_query(adapter, mac, false,
					     if_handle, curr_pmac_id);
	else
		/* Fetch the MAC address using pmac_id */
		return be_cmd_get_mac_from_list(adapter, mac, &active,
						&curr_pmac_id,
						if_handle, domain);
}

int be_cmd_get_perm_mac(struct be_adapter *adapter, u8 *mac)
{
	int status;
	bool pmac_valid = false;

	eth_zero_addr(mac);

	if (BEx_chip(adapter)) {
		if (be_physfn(adapter))
			status = be_cmd_mac_addr_query(adapter, mac, true, 0,
						       0);
		else
			status = be_cmd_mac_addr_query(adapter, mac, false,
						       adapter->if_handle, 0);
	} else {
		status = be_cmd_get_mac_from_list(adapter, mac, &pmac_valid,
						  NULL, adapter->if_handle, 0);
	}

	return status;
}

/* Uses synchronous MCCQ */
int be_cmd_set_mac_list(struct be_adapter *adapter, u8 *mac_array,
			u8 mac_count, u32 domain)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_set_mac_list *req;
	int status;
	struct be_dma_mem cmd;

	memset(&cmd, 0, sizeof(struct be_dma_mem));
	cmd.size = sizeof(struct be_cmd_req_set_mac_list);
	cmd.va = dma_alloc_coherent(&adapter->pdev->dev, cmd.size, &cmd.dma,
				    GFP_KERNEL);
	if (!cmd.va)
		return -ENOMEM;

	mutex_lock(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}

	req = cmd.va;
	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
			       OPCODE_COMMON_SET_MAC_LIST, sizeof(*req),
			       wrb, &cmd);

	req->hdr.domain = domain;
	req->mac_count = mac_count;
	if (mac_count)
		memcpy(req->mac, mac_array, ETH_ALEN*mac_count);

	status = be_mcc_notify_wait(adapter);

err:
	dma_free_coherent(&adapter->pdev->dev, cmd.size, cmd.va, cmd.dma);
	mutex_unlock(&adapter->mcc_lock);
	return status;
}

/* Wrapper to delete any active MACs and provision the new mac.
 * Changes to MAC_LIST are allowed iff none of the MAC addresses in the
 * current list are active.
 */
int be_cmd_set_mac(struct be_adapter *adapter, u8 *mac, int if_id, u32 dom)
{
	bool active_mac = false;
	u8 old_mac[ETH_ALEN];
	u32 pmac_id;
	int status;

	status = be_cmd_get_mac_from_list(adapter, old_mac, &active_mac,
					  &pmac_id, if_id, dom);

	if (!status && active_mac)
		be_cmd_pmac_del(adapter, if_id, pmac_id, dom);

	return be_cmd_set_mac_list(adapter, mac, mac ? 1 : 0, dom);
}

int be_cmd_set_hsw_config(struct be_adapter *adapter, u16 pvid,
			  u32 domain, u16 intf_id, u16 hsw_mode, u8 spoofchk)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_set_hsw_config *req;
	void *ctxt;
	int status;

	if (!be_cmd_allowed(adapter, OPCODE_COMMON_SET_HSW_CONFIG,
			    CMD_SUBSYSTEM_COMMON))
		return -EPERM;

	mutex_lock(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}

	req = embedded_payload(wrb);
	ctxt = &req->context;

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
			       OPCODE_COMMON_SET_HSW_CONFIG, sizeof(*req), wrb,
			       NULL);

	req->hdr.domain = domain;
	AMAP_SET_BITS(struct amap_set_hsw_context, interface_id, ctxt, intf_id);
	if (pvid) {
		AMAP_SET_BITS(struct amap_set_hsw_context, pvid_valid, ctxt, 1);
		AMAP_SET_BITS(struct amap_set_hsw_context, pvid, ctxt, pvid);
	}
	if (hsw_mode) {
		AMAP_SET_BITS(struct amap_set_hsw_context, interface_id,
			      ctxt, adapter->hba_port_num);
		AMAP_SET_BITS(struct amap_set_hsw_context, pport, ctxt, 1);
		AMAP_SET_BITS(struct amap_set_hsw_context, port_fwd_type,
			      ctxt, hsw_mode);
	}

	/* Enable/disable both mac and vlan spoof checking */
	if (!BEx_chip(adapter) && spoofchk) {
		AMAP_SET_BITS(struct amap_set_hsw_context, mac_spoofchk,
			      ctxt, spoofchk);
		AMAP_SET_BITS(struct amap_set_hsw_context, vlan_spoofchk,
			      ctxt, spoofchk);
	}

	be_dws_cpu_to_le(req->context, sizeof(req->context));
	status = be_mcc_notify_wait(adapter);

err:
	mutex_unlock(&adapter->mcc_lock);
	return status;
}

/* Get Hyper switch config */
int be_cmd_get_hsw_config(struct be_adapter *adapter, u16 *pvid,
			  u32 domain, u16 intf_id, u8 *mode, bool *spoofchk)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_get_hsw_config *req;
	void *ctxt;
	int status;
	u16 vid;

	mutex_lock(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}

	req = embedded_payload(wrb);
	ctxt = &req->context;

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
			       OPCODE_COMMON_GET_HSW_CONFIG, sizeof(*req), wrb,
			       NULL);

	req->hdr.domain = domain;
	AMAP_SET_BITS(struct amap_get_hsw_req_context, interface_id,
		      ctxt, intf_id);
	AMAP_SET_BITS(struct amap_get_hsw_req_context, pvid_valid, ctxt, 1);

	if (!BEx_chip(adapter) && mode) {
		AMAP_SET_BITS(struct amap_get_hsw_req_context, interface_id,
			      ctxt, adapter->hba_port_num);
		AMAP_SET_BITS(struct amap_get_hsw_req_context, pport, ctxt, 1);
	}
	be_dws_cpu_to_le(req->context, sizeof(req->context));

	status = be_mcc_notify_wait(adapter);
	if (!status) {
		struct be_cmd_resp_get_hsw_config *resp =
						embedded_payload(wrb);

		be_dws_le_to_cpu(&resp->context, sizeof(resp->context));
		vid = AMAP_GET_BITS(struct amap_get_hsw_resp_context,
				    pvid, &resp->context);
		if (pvid)
			*pvid = le16_to_cpu(vid);
		if (mode)
			*mode = AMAP_GET_BITS(struct amap_get_hsw_resp_context,
					      port_fwd_type, &resp->context);
		if (spoofchk)
			*spoofchk =
				AMAP_GET_BITS(struct amap_get_hsw_resp_context,
					      spoofchk, &resp->context);
	}

err:
	mutex_unlock(&adapter->mcc_lock);
	return status;
}

static bool be_is_wol_excluded(struct be_adapter *adapter)
{
	struct pci_dev *pdev = adapter->pdev;

	if (be_virtfn(adapter))
		return true;

	switch (pdev->subsystem_device) {
	case OC_SUBSYS_DEVICE_ID1:
	case OC_SUBSYS_DEVICE_ID2:
	case OC_SUBSYS_DEVICE_ID3:
	case OC_SUBSYS_DEVICE_ID4:
		return true;
	default:
		return false;
	}
}

int be_cmd_get_acpi_wol_cap(struct be_adapter *adapter)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_acpi_wol_magic_config_v1 *req;
	int status = 0;
	struct be_dma_mem cmd;

	if (!be_cmd_allowed(adapter, OPCODE_ETH_ACPI_WOL_MAGIC_CONFIG,
			    CMD_SUBSYSTEM_ETH))
		return -EPERM;

	if (be_is_wol_excluded(adapter))
		return status;

	if (mutex_lock_interruptible(&adapter->mbox_lock))
		return -1;

	memset(&cmd, 0, sizeof(struct be_dma_mem));
	cmd.size = sizeof(struct be_cmd_resp_acpi_wol_magic_config_v1);
	cmd.va = dma_alloc_coherent(&adapter->pdev->dev, cmd.size, &cmd.dma,
				    GFP_ATOMIC);
	if (!cmd.va) {
		dev_err(&adapter->pdev->dev, "Memory allocation failure\n");
		status = -ENOMEM;
		goto err;
	}

	wrb = wrb_from_mbox(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}

	req = cmd.va;

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_ETH,
			       OPCODE_ETH_ACPI_WOL_MAGIC_CONFIG,
			       sizeof(*req), wrb, &cmd);

	req->hdr.version = 1;
	req->query_options = BE_GET_WOL_CAP;

	status = be_mbox_notify_wait(adapter);
	if (!status) {
		struct be_cmd_resp_acpi_wol_magic_config_v1 *resp;

		resp = (struct be_cmd_resp_acpi_wol_magic_config_v1 *)cmd.va;

		adapter->wol_cap = resp->wol_settings;

		/* Non-zero macaddr indicates WOL is enabled */
		if (adapter->wol_cap & BE_WOL_CAP &&
		    !is_zero_ether_addr(resp->magic_mac))
			adapter->wol_en = true;
	}
err:
	mutex_unlock(&adapter->mbox_lock);
	if (cmd.va)
		dma_free_coherent(&adapter->pdev->dev, cmd.size, cmd.va,
				  cmd.dma);
	return status;

}

int be_cmd_set_fw_log_level(struct be_adapter *adapter, u32 level)
{
	struct be_dma_mem extfat_cmd;
	struct be_fat_conf_params *cfgs;
	int status;
	int i, j;

	memset(&extfat_cmd, 0, sizeof(struct be_dma_mem));
	extfat_cmd.size = sizeof(struct be_cmd_resp_get_ext_fat_caps);
	extfat_cmd.va = dma_alloc_coherent(&adapter->pdev->dev,
					   extfat_cmd.size, &extfat_cmd.dma,
					   GFP_ATOMIC);
	if (!extfat_cmd.va)
		return -ENOMEM;

	status = be_cmd_get_ext_fat_capabilites(adapter, &extfat_cmd);
	if (status)
		goto err;

	cfgs = (struct be_fat_conf_params *)
			(extfat_cmd.va + sizeof(struct be_cmd_resp_hdr));
	for (i = 0; i < le32_to_cpu(cfgs->num_modules); i++) {
		u32 num_modes = le32_to_cpu(cfgs->module[i].num_modes);

		for (j = 0; j < num_modes; j++) {
			if (cfgs->module[i].trace_lvl[j].mode == MODE_UART)
				cfgs->module[i].trace_lvl[j].dbg_lvl =
							cpu_to_le32(level);
		}
	}

	status = be_cmd_set_ext_fat_capabilites(adapter, &extfat_cmd, cfgs);
err:
	dma_free_coherent(&adapter->pdev->dev, extfat_cmd.size, extfat_cmd.va,
			  extfat_cmd.dma);
	return status;
}

int be_cmd_get_fw_log_level(struct be_adapter *adapter)
{
	struct be_dma_mem extfat_cmd;
	struct be_fat_conf_params *cfgs;
	int status, j;
	int level = 0;

	memset(&extfat_cmd, 0, sizeof(struct be_dma_mem));
	extfat_cmd.size = sizeof(struct be_cmd_resp_get_ext_fat_caps);
	extfat_cmd.va = dma_alloc_coherent(&adapter->pdev->dev,
					   extfat_cmd.size, &extfat_cmd.dma,
					   GFP_ATOMIC);

	if (!extfat_cmd.va) {
		dev_err(&adapter->pdev->dev, "%s: Memory allocation failure\n",
			__func__);
		goto err;
	}

	status = be_cmd_get_ext_fat_capabilites(adapter, &extfat_cmd);
	if (!status) {
		cfgs = (struct be_fat_conf_params *)(extfat_cmd.va +
						sizeof(struct be_cmd_resp_hdr));

		for (j = 0; j < le32_to_cpu(cfgs->module[0].num_modes); j++) {
			if (cfgs->module[0].trace_lvl[j].mode == MODE_UART)
				level = cfgs->module[0].trace_lvl[j].dbg_lvl;
		}
	}
	dma_free_coherent(&adapter->pdev->dev, extfat_cmd.size, extfat_cmd.va,
			  extfat_cmd.dma);
err:
	return level;
}

int be_cmd_get_ext_fat_capabilites(struct be_adapter *adapter,
				   struct be_dma_mem *cmd)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_get_ext_fat_caps *req;
	int status;

	if (!be_cmd_allowed(adapter, OPCODE_COMMON_GET_EXT_FAT_CAPABILITIES,
			    CMD_SUBSYSTEM_COMMON))
		return -EPERM;

	if (mutex_lock_interruptible(&adapter->mbox_lock))
		return -1;

	wrb = wrb_from_mbox(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}

	req = cmd->va;
	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
			       OPCODE_COMMON_GET_EXT_FAT_CAPABILITIES,
			       cmd->size, wrb, cmd);
	req->parameter_type = cpu_to_le32(1);

	status = be_mbox_notify_wait(adapter);
err:
	mutex_unlock(&adapter->mbox_lock);
	return status;
}

int be_cmd_set_ext_fat_capabilites(struct be_adapter *adapter,
				   struct be_dma_mem *cmd,
				   struct be_fat_conf_params *configs)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_set_ext_fat_caps *req;
	int status;

	mutex_lock(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}

	req = cmd->va;
	memcpy(&req->set_params, configs, sizeof(struct be_fat_conf_params));
	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
			       OPCODE_COMMON_SET_EXT_FAT_CAPABILITIES,
			       cmd->size, wrb, cmd);

	status = be_mcc_notify_wait(adapter);
err:
	mutex_unlock(&adapter->mcc_lock);
	return status;
}

int be_cmd_query_port_name(struct be_adapter *adapter)
{
	struct be_cmd_req_get_port_name *req;
	struct be_mcc_wrb *wrb;
	int status;

	if (mutex_lock_interruptible(&adapter->mbox_lock))
		return -1;

	wrb = wrb_from_mbox(adapter);
	req = embedded_payload(wrb);

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
			       OPCODE_COMMON_GET_PORT_NAME, sizeof(*req), wrb,
			       NULL);
	if (!BEx_chip(adapter))
		req->hdr.version = 1;

	status = be_mbox_notify_wait(adapter);
	if (!status) {
		struct be_cmd_resp_get_port_name *resp = embedded_payload(wrb);

		adapter->port_name = resp->port_name[adapter->hba_port_num];
	} else {
		adapter->port_name = adapter->hba_port_num + '0';
	}

	mutex_unlock(&adapter->mbox_lock);
	return status;
}

/* When more than 1 NIC descriptor is present in the descriptor list,
 * the caller must specify the pf_num to obtain the NIC descriptor
 * corresponding to its pci function.
 * get_vft must be true when the caller wants the VF-template desc of the
 * PF-pool.
 * The pf_num should be set to PF_NUM_IGNORE when the caller knows
 * that only it's NIC descriptor is present in the descriptor list.
 */
static struct be_nic_res_desc *be_get_nic_desc(u8 *buf, u32 desc_count,
					       bool get_vft, u8 pf_num)
{
	struct be_res_desc_hdr *hdr = (struct be_res_desc_hdr *)buf;
	struct be_nic_res_desc *nic;
	int i;

	for (i = 0; i < desc_count; i++) {
		if (hdr->desc_type == NIC_RESOURCE_DESC_TYPE_V0 ||
		    hdr->desc_type == NIC_RESOURCE_DESC_TYPE_V1) {
			nic = (struct be_nic_res_desc *)hdr;

			if ((pf_num == PF_NUM_IGNORE ||
			     nic->pf_num == pf_num) &&
			    (!get_vft || nic->flags & BIT(VFT_SHIFT)))
				return nic;
		}
		hdr->desc_len = hdr->desc_len ? : RESOURCE_DESC_SIZE_V0;
		hdr = (void *)hdr + hdr->desc_len;
	}
	return NULL;
}

static struct be_nic_res_desc *be_get_vft_desc(u8 *buf, u32 desc_count,
					       u8 pf_num)
{
	return be_get_nic_desc(buf, desc_count, true, pf_num);
}

static struct be_nic_res_desc *be_get_func_nic_desc(u8 *buf, u32 desc_count,
						    u8 pf_num)
{
	return be_get_nic_desc(buf, desc_count, false, pf_num);
}

static struct be_pcie_res_desc *be_get_pcie_desc(u8 *buf, u32 desc_count,
						 u8 pf_num)
{
	struct be_res_desc_hdr *hdr = (struct be_res_desc_hdr *)buf;
	struct be_pcie_res_desc *pcie;
	int i;

	for (i = 0; i < desc_count; i++) {
		if (hdr->desc_type == PCIE_RESOURCE_DESC_TYPE_V0 ||
		    hdr->desc_type == PCIE_RESOURCE_DESC_TYPE_V1) {
			pcie = (struct be_pcie_res_desc *)hdr;
			if (pcie->pf_num == pf_num)
				return pcie;
		}

		hdr->desc_len = hdr->desc_len ? : RESOURCE_DESC_SIZE_V0;
		hdr = (void *)hdr + hdr->desc_len;
	}
	return NULL;
}

static struct be_port_res_desc *be_get_port_desc(u8 *buf, u32 desc_count)
{
	struct be_res_desc_hdr *hdr = (struct be_res_desc_hdr *)buf;
	int i;

	for (i = 0; i < desc_count; i++) {
		if (hdr->desc_type == PORT_RESOURCE_DESC_TYPE_V1)
			return (struct be_port_res_desc *)hdr;

		hdr->desc_len = hdr->desc_len ? : RESOURCE_DESC_SIZE_V0;
		hdr = (void *)hdr + hdr->desc_len;
	}
	return NULL;
}

static void be_copy_nic_desc(struct be_resources *res,
			     struct be_nic_res_desc *desc)
{
	res->max_uc_mac = le16_to_cpu(desc->unicast_mac_count);
	res->max_vlans = le16_to_cpu(desc->vlan_count);
	res->max_mcast_mac = le16_to_cpu(desc->mcast_mac_count);
	res->max_tx_qs = le16_to_cpu(desc->txq_count);
	res->max_rss_qs = le16_to_cpu(desc->rssq_count);
	res->max_rx_qs = le16_to_cpu(desc->rq_count);
	res->max_evt_qs = le16_to_cpu(desc->eq_count);
	res->max_cq_count = le16_to_cpu(desc->cq_count);
	res->max_iface_count = le16_to_cpu(desc->iface_count);
	res->max_mcc_count = le16_to_cpu(desc->mcc_count);
	/* Clear flags that driver is not interested in */
	res->if_cap_flags = le32_to_cpu(desc->cap_flags) &
				BE_IF_CAP_FLAGS_WANT;
}

/* Uses Mbox */
int be_cmd_get_func_config(struct be_adapter *adapter, struct be_resources *res)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_get_func_config *req;
	int status;
	struct be_dma_mem cmd;

	if (mutex_lock_interruptible(&adapter->mbox_lock))
		return -1;

	memset(&cmd, 0, sizeof(struct be_dma_mem));
	cmd.size = sizeof(struct be_cmd_resp_get_func_config);
	cmd.va = dma_alloc_coherent(&adapter->pdev->dev, cmd.size, &cmd.dma,
				    GFP_ATOMIC);
	if (!cmd.va) {
		dev_err(&adapter->pdev->dev, "Memory alloc failure\n");
		status = -ENOMEM;
		goto err;
	}

	wrb = wrb_from_mbox(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}

	req = cmd.va;

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
			       OPCODE_COMMON_GET_FUNC_CONFIG,
			       cmd.size, wrb, &cmd);

	if (skyhawk_chip(adapter))
		req->hdr.version = 1;

	status = be_mbox_notify_wait(adapter);
	if (!status) {
		struct be_cmd_resp_get_func_config *resp = cmd.va;
		u32 desc_count = le32_to_cpu(resp->desc_count);
		struct be_nic_res_desc *desc;

		/* GET_FUNC_CONFIG returns resource descriptors of the
		 * current function only. So, pf_num should be set to
		 * PF_NUM_IGNORE.
		 */
		desc = be_get_func_nic_desc(resp->func_param, desc_count,
					    PF_NUM_IGNORE);
		if (!desc) {
			status = -EINVAL;
			goto err;
		}

		/* Store pf_num & vf_num for later use in GET_PROFILE_CONFIG */
		adapter->pf_num = desc->pf_num;
		adapter->vf_num = desc->vf_num;

		if (res)
			be_copy_nic_desc(res, desc);
	}
err:
	mutex_unlock(&adapter->mbox_lock);
	if (cmd.va)
		dma_free_coherent(&adapter->pdev->dev, cmd.size, cmd.va,
				  cmd.dma);
	return status;
}

/* This routine returns a list of all the NIC PF_nums in the adapter */
static u16 be_get_nic_pf_num_list(u8 *buf, u32 desc_count, u16 *nic_pf_nums)
{
	struct be_res_desc_hdr *hdr = (struct be_res_desc_hdr *)buf;
	struct be_pcie_res_desc *pcie = NULL;
	int i;
	u16 nic_pf_count = 0;

	for (i = 0; i < desc_count; i++) {
		if (hdr->desc_type == PCIE_RESOURCE_DESC_TYPE_V0 ||
		    hdr->desc_type == PCIE_RESOURCE_DESC_TYPE_V1) {
			pcie = (struct be_pcie_res_desc *)hdr;
			if (pcie->pf_state && (pcie->pf_type == MISSION_NIC ||
					       pcie->pf_type == MISSION_RDMA)) {
				nic_pf_nums[nic_pf_count++] = pcie->pf_num;
			}
		}

		hdr->desc_len = hdr->desc_len ? : RESOURCE_DESC_SIZE_V0;
		hdr = (void *)hdr + hdr->desc_len;
	}
	return nic_pf_count;
}

/* Will use MBOX only if MCCQ has not been created */
int be_cmd_get_profile_config(struct be_adapter *adapter,
			      struct be_resources *res,
			      struct be_port_resources *port_res,
			      u8 profile_type, u8 query, u8 domain)
{
	struct be_cmd_resp_get_profile_config *resp;
	struct be_cmd_req_get_profile_config *req;
	struct be_nic_res_desc *vf_res;
	struct be_pcie_res_desc *pcie;
	struct be_port_res_desc *port;
	struct be_nic_res_desc *nic;
	struct be_mcc_wrb wrb = {0};
	struct be_dma_mem cmd;
	u16 desc_count;
	int status;

	memset(&cmd, 0, sizeof(struct be_dma_mem));
	cmd.size = sizeof(struct be_cmd_resp_get_profile_config);
	cmd.va = dma_alloc_coherent(&adapter->pdev->dev, cmd.size, &cmd.dma,
				    GFP_ATOMIC);
	if (!cmd.va)
		return -ENOMEM;

	req = cmd.va;
	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
			       OPCODE_COMMON_GET_PROFILE_CONFIG,
			       cmd.size, &wrb, &cmd);

	if (!lancer_chip(adapter))
		req->hdr.version = 1;
	req->type = profile_type;
	req->hdr.domain = domain;

	/* When QUERY_MODIFIABLE_FIELDS_TYPE bit is set, cmd returns the
	 * descriptors with all bits set to "1" for the fields which can be
	 * modified using SET_PROFILE_CONFIG cmd.
	 */
	if (query == RESOURCE_MODIFIABLE)
		req->type |= QUERY_MODIFIABLE_FIELDS_TYPE;

	status = be_cmd_notify_wait(adapter, &wrb);
	if (status)
		goto err;

	resp = cmd.va;
	desc_count = le16_to_cpu(resp->desc_count);

	if (port_res) {
		u16 nic_pf_cnt = 0, i;
		u16 nic_pf_num_list[MAX_NIC_FUNCS];

		nic_pf_cnt = be_get_nic_pf_num_list(resp->func_param,
						    desc_count,
						    nic_pf_num_list);

		for (i = 0; i < nic_pf_cnt; i++) {
			nic = be_get_func_nic_desc(resp->func_param, desc_count,
						   nic_pf_num_list[i]);
			if (nic->link_param == adapter->port_num) {
				port_res->nic_pfs++;
				pcie = be_get_pcie_desc(resp->func_param,
							desc_count,
							nic_pf_num_list[i]);
				port_res->max_vfs += le16_to_cpu(pcie->num_vfs);
			}
		}
		goto err;
	}

	pcie = be_get_pcie_desc(resp->func_param, desc_count,
				adapter->pf_num);
	if (pcie)
		res->max_vfs = le16_to_cpu(pcie->num_vfs);

	port = be_get_port_desc(resp->func_param, desc_count);
	if (port)
		adapter->mc_type = port->mc_type;

	nic = be_get_func_nic_desc(resp->func_param, desc_count,
				   adapter->pf_num);
	if (nic)
		be_copy_nic_desc(res, nic);

	vf_res = be_get_vft_desc(resp->func_param, desc_count,
				 adapter->pf_num);
	if (vf_res)
		res->vf_if_cap_flags = vf_res->cap_flags;
err:
	if (cmd.va)
		dma_free_coherent(&adapter->pdev->dev, cmd.size, cmd.va,
				  cmd.dma);
	return status;
}

/* Will use MBOX only if MCCQ has not been created */
static int be_cmd_set_profile_config(struct be_adapter *adapter, void *desc,
				     int size, int count, u8 version, u8 domain)
{
	struct be_cmd_req_set_profile_config *req;
	struct be_mcc_wrb wrb = {0};
	struct be_dma_mem cmd;
	int status;

	memset(&cmd, 0, sizeof(struct be_dma_mem));
	cmd.size = sizeof(struct be_cmd_req_set_profile_config);
	cmd.va = dma_alloc_coherent(&adapter->pdev->dev, cmd.size, &cmd.dma,
				    GFP_ATOMIC);
	if (!cmd.va)
		return -ENOMEM;

	req = cmd.va;
	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
			       OPCODE_COMMON_SET_PROFILE_CONFIG, cmd.size,
			       &wrb, &cmd);
	req->hdr.version = version;
	req->hdr.domain = domain;
	req->desc_count = cpu_to_le32(count);
	memcpy(req->desc, desc, size);

	status = be_cmd_notify_wait(adapter, &wrb);

	if (cmd.va)
		dma_free_coherent(&adapter->pdev->dev, cmd.size, cmd.va,
				  cmd.dma);
	return status;
}

/* Mark all fields invalid */
static void be_reset_nic_desc(struct be_nic_res_desc *nic)
{
	memset(nic, 0, sizeof(*nic));
	nic->unicast_mac_count = 0xFFFF;
	nic->mcc_count = 0xFFFF;
	nic->vlan_count = 0xFFFF;
	nic->mcast_mac_count = 0xFFFF;
	nic->txq_count = 0xFFFF;
	nic->rq_count = 0xFFFF;
	nic->rssq_count = 0xFFFF;
	nic->lro_count = 0xFFFF;
	nic->cq_count = 0xFFFF;
	nic->toe_conn_count = 0xFFFF;
	nic->eq_count = 0xFFFF;
	nic->iface_count = 0xFFFF;
	nic->link_param = 0xFF;
	nic->channel_id_param = cpu_to_le16(0xF000);
	nic->acpi_params = 0xFF;
	nic->wol_param = 0x0F;
	nic->tunnel_iface_count = 0xFFFF;
	nic->direct_tenant_iface_count = 0xFFFF;
	nic->bw_min = 0xFFFFFFFF;
	nic->bw_max = 0xFFFFFFFF;
}

/* Mark all fields invalid */
static void be_reset_pcie_desc(struct be_pcie_res_desc *pcie)
{
	memset(pcie, 0, sizeof(*pcie));
	pcie->sriov_state = 0xFF;
	pcie->pf_state = 0xFF;
	pcie->pf_type = 0xFF;
	pcie->num_vfs = 0xFFFF;
}

int be_cmd_config_qos(struct be_adapter *adapter, u32 max_rate, u16 link_speed,
		      u8 domain)
{
	struct be_nic_res_desc nic_desc;
	u32 bw_percent;
	u16 version = 0;

	if (BE3_chip(adapter))
		return be_cmd_set_qos(adapter, max_rate / 10, domain);

	be_reset_nic_desc(&nic_desc);
	nic_desc.pf_num = adapter->pf_num;
	nic_desc.vf_num = domain;
	nic_desc.bw_min = 0;
	if (lancer_chip(adapter)) {
		nic_desc.hdr.desc_type = NIC_RESOURCE_DESC_TYPE_V0;
		nic_desc.hdr.desc_len = RESOURCE_DESC_SIZE_V0;
		nic_desc.flags = (1 << QUN_SHIFT) | (1 << IMM_SHIFT) |
					(1 << NOSV_SHIFT);
		nic_desc.bw_max = cpu_to_le32(max_rate / 10);
	} else {
		version = 1;
		nic_desc.hdr.desc_type = NIC_RESOURCE_DESC_TYPE_V1;
		nic_desc.hdr.desc_len = RESOURCE_DESC_SIZE_V1;
		nic_desc.flags = (1 << IMM_SHIFT) | (1 << NOSV_SHIFT);
		bw_percent = max_rate ? (max_rate * 100) / link_speed : 100;
		nic_desc.bw_max = cpu_to_le32(bw_percent);
	}

	return be_cmd_set_profile_config(adapter, &nic_desc,
					 nic_desc.hdr.desc_len,
					 1, version, domain);
}

int be_cmd_set_sriov_config(struct be_adapter *adapter,
			    struct be_resources pool_res, u16 num_vfs,
			    struct be_resources *vft_res)
{
	struct {
		struct be_pcie_res_desc pcie;
		struct be_nic_res_desc nic_vft;
	} __packed desc;

	/* PF PCIE descriptor */
	be_reset_pcie_desc(&desc.pcie);
	desc.pcie.hdr.desc_type = PCIE_RESOURCE_DESC_TYPE_V1;
	desc.pcie.hdr.desc_len = RESOURCE_DESC_SIZE_V1;
	desc.pcie.flags = BIT(IMM_SHIFT) | BIT(NOSV_SHIFT);
	desc.pcie.pf_num = adapter->pdev->devfn;
	desc.pcie.sriov_state = num_vfs ? 1 : 0;
	desc.pcie.num_vfs = cpu_to_le16(num_vfs);

	/* VF NIC Template descriptor */
	be_reset_nic_desc(&desc.nic_vft);
	desc.nic_vft.hdr.desc_type = NIC_RESOURCE_DESC_TYPE_V1;
	desc.nic_vft.hdr.desc_len = RESOURCE_DESC_SIZE_V1;
	desc.nic_vft.flags = vft_res->flags | BIT(VFT_SHIFT) |
			     BIT(IMM_SHIFT) | BIT(NOSV_SHIFT);
	desc.nic_vft.pf_num = adapter->pdev->devfn;
	desc.nic_vft.vf_num = 0;
	desc.nic_vft.cap_flags = cpu_to_le32(vft_res->vf_if_cap_flags);
	desc.nic_vft.rq_count = cpu_to_le16(vft_res->max_rx_qs);
	desc.nic_vft.txq_count = cpu_to_le16(vft_res->max_tx_qs);
	desc.nic_vft.rssq_count = cpu_to_le16(vft_res->max_rss_qs);
	desc.nic_vft.cq_count = cpu_to_le16(vft_res->max_cq_count);

	if (vft_res->max_uc_mac)
		desc.nic_vft.unicast_mac_count =
					cpu_to_le16(vft_res->max_uc_mac);
	if (vft_res->max_vlans)
		desc.nic_vft.vlan_count = cpu_to_le16(vft_res->max_vlans);
	if (vft_res->max_iface_count)
		desc.nic_vft.iface_count =
				cpu_to_le16(vft_res->max_iface_count);
	if (vft_res->max_mcc_count)
		desc.nic_vft.mcc_count = cpu_to_le16(vft_res->max_mcc_count);

	return be_cmd_set_profile_config(adapter, &desc,
					 2 * RESOURCE_DESC_SIZE_V1, 2, 1, 0);
}

int be_cmd_manage_iface(struct be_adapter *adapter, u32 iface, u8 op)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_manage_iface_filters *req;
	int status;

	if (iface == 0xFFFFFFFF)
		return -1;

	mutex_lock(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}
	req = embedded_payload(wrb);

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
			       OPCODE_COMMON_MANAGE_IFACE_FILTERS, sizeof(*req),
			       wrb, NULL);
	req->op = op;
	req->target_iface_id = cpu_to_le32(iface);

	status = be_mcc_notify_wait(adapter);
err:
	mutex_unlock(&adapter->mcc_lock);
	return status;
}

int be_cmd_set_vxlan_port(struct be_adapter *adapter, __be16 port)
{
	struct be_port_res_desc port_desc;

	memset(&port_desc, 0, sizeof(port_desc));
	port_desc.hdr.desc_type = PORT_RESOURCE_DESC_TYPE_V1;
	port_desc.hdr.desc_len = RESOURCE_DESC_SIZE_V1;
	port_desc.flags = (1 << IMM_SHIFT) | (1 << NOSV_SHIFT);
	port_desc.link_num = adapter->hba_port_num;
	if (port) {
		port_desc.nv_flags = NV_TYPE_VXLAN | (1 << SOCVID_SHIFT) |
					(1 << RCVID_SHIFT);
		port_desc.nv_port = swab16(port);
	} else {
		port_desc.nv_flags = NV_TYPE_DISABLED;
		port_desc.nv_port = 0;
	}

	return be_cmd_set_profile_config(adapter, &port_desc,
					 RESOURCE_DESC_SIZE_V1, 1, 1, 0);
}

int be_cmd_get_if_id(struct be_adapter *adapter, struct be_vf_cfg *vf_cfg,
		     int vf_num)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_get_iface_list *req;
	struct be_cmd_resp_get_iface_list *resp;
	int status;

	mutex_lock(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}
	req = embedded_payload(wrb);

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
			       OPCODE_COMMON_GET_IFACE_LIST, sizeof(*resp),
			       wrb, NULL);
	req->hdr.domain = vf_num + 1;

	status = be_mcc_notify_wait(adapter);
	if (!status) {
		resp = (struct be_cmd_resp_get_iface_list *)req;
		vf_cfg->if_handle = le32_to_cpu(resp->if_desc.if_id);
	}

err:
	mutex_unlock(&adapter->mcc_lock);
	return status;
}

static int lancer_wait_idle(struct be_adapter *adapter)
{
#define SLIPORT_IDLE_TIMEOUT 30
	u32 reg_val;
	int status = 0, i;

	for (i = 0; i < SLIPORT_IDLE_TIMEOUT; i++) {
		reg_val = ioread32(adapter->db + PHYSDEV_CONTROL_OFFSET);
		if ((reg_val & PHYSDEV_CONTROL_INP_MASK) == 0)
			break;

		ssleep(1);
	}

	if (i == SLIPORT_IDLE_TIMEOUT)
		status = -1;

	return status;
}

int lancer_physdev_ctrl(struct be_adapter *adapter, u32 mask)
{
	int status = 0;

	status = lancer_wait_idle(adapter);
	if (status)
		return status;

	iowrite32(mask, adapter->db + PHYSDEV_CONTROL_OFFSET);

	return status;
}

/* Routine to check whether dump image is present or not */
bool dump_present(struct be_adapter *adapter)
{
	u32 sliport_status = 0;

	sliport_status = ioread32(adapter->db + SLIPORT_STATUS_OFFSET);
	return !!(sliport_status & SLIPORT_STATUS_DIP_MASK);
}

int lancer_initiate_dump(struct be_adapter *adapter)
{
	struct device *dev = &adapter->pdev->dev;
	int status;

	if (dump_present(adapter)) {
		dev_info(dev, "Previous dump not cleared, not forcing dump\n");
		return -EEXIST;
	}

	/* give firmware reset and diagnostic dump */
	status = lancer_physdev_ctrl(adapter, PHYSDEV_CONTROL_FW_RESET_MASK |
				     PHYSDEV_CONTROL_DD_MASK);
	if (status < 0) {
		dev_err(dev, "FW reset failed\n");
		return status;
	}

	status = lancer_wait_idle(adapter);
	if (status)
		return status;

	if (!dump_present(adapter)) {
		dev_err(dev, "FW dump not generated\n");
		return -EIO;
	}

	return 0;
}

int lancer_delete_dump(struct be_adapter *adapter)
{
	int status;

	status = lancer_cmd_delete_object(adapter, LANCER_FW_DUMP_FILE);
	return be_cmd_status(status);
}

/* Uses sync mcc */
int be_cmd_enable_vf(struct be_adapter *adapter, u8 domain)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_enable_disable_vf *req;
	int status;

	if (BEx_chip(adapter))
		return 0;

	mutex_lock(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}

	req = embedded_payload(wrb);

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
			       OPCODE_COMMON_ENABLE_DISABLE_VF, sizeof(*req),
			       wrb, NULL);

	req->hdr.domain = domain;
	req->enable = 1;
	status = be_mcc_notify_wait(adapter);
err:
	mutex_unlock(&adapter->mcc_lock);
	return status;
}

int be_cmd_intr_set(struct be_adapter *adapter, bool intr_enable)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_intr_set *req;
	int status;

	if (mutex_lock_interruptible(&adapter->mbox_lock))
		return -1;

	wrb = wrb_from_mbox(adapter);

	req = embedded_payload(wrb);

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
			       OPCODE_COMMON_SET_INTERRUPT_ENABLE, sizeof(*req),
			       wrb, NULL);

	req->intr_enabled = intr_enable;

	status = be_mbox_notify_wait(adapter);

	mutex_unlock(&adapter->mbox_lock);
	return status;
}

/* Uses MBOX */
int be_cmd_get_active_profile(struct be_adapter *adapter, u16 *profile_id)
{
	struct be_cmd_req_get_active_profile *req;
	struct be_mcc_wrb *wrb;
	int status;

	if (mutex_lock_interruptible(&adapter->mbox_lock))
		return -1;

	wrb = wrb_from_mbox(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}

	req = embedded_payload(wrb);

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
			       OPCODE_COMMON_GET_ACTIVE_PROFILE, sizeof(*req),
			       wrb, NULL);

	status = be_mbox_notify_wait(adapter);
	if (!status) {
		struct be_cmd_resp_get_active_profile *resp =
							embedded_payload(wrb);

		*profile_id = le16_to_cpu(resp->active_profile_id);
	}

err:
	mutex_unlock(&adapter->mbox_lock);
	return status;
}

static int
__be_cmd_set_logical_link_config(struct be_adapter *adapter,
				 int link_state, int version, u8 domain)
{
	struct be_cmd_req_set_ll_link *req;
	struct be_mcc_wrb *wrb;
	u32 link_config = 0;
	int status;

	mutex_lock(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}

	req = embedded_payload(wrb);

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
			       OPCODE_COMMON_SET_LOGICAL_LINK_CONFIG,
			       sizeof(*req), wrb, NULL);

	req->hdr.version = version;
	req->hdr.domain = domain;

	if (link_state == IFLA_VF_LINK_STATE_ENABLE ||
	    link_state == IFLA_VF_LINK_STATE_AUTO)
		link_config |= PLINK_ENABLE;

	if (link_state == IFLA_VF_LINK_STATE_AUTO)
		link_config |= PLINK_TRACK;

	req->link_config = cpu_to_le32(link_config);

	status = be_mcc_notify_wait(adapter);
err:
	mutex_unlock(&adapter->mcc_lock);
	return status;
}

int be_cmd_set_logical_link_config(struct be_adapter *adapter,
				   int link_state, u8 domain)
{
	int status;

	if (BE2_chip(adapter))
		return -EOPNOTSUPP;

	status = __be_cmd_set_logical_link_config(adapter, link_state,
						  2, domain);

	/* Version 2 of the command will not be recognized by older FW.
	 * On such a failure issue version 1 of the command.
	 */
	if (base_status(status) == MCC_STATUS_ILLEGAL_REQUEST)
		status = __be_cmd_set_logical_link_config(adapter, link_state,
							  1, domain);
	return status;
}

int be_cmd_set_features(struct be_adapter *adapter)
{
	struct be_cmd_resp_set_features *resp;
	struct be_cmd_req_set_features *req;
	struct be_mcc_wrb *wrb;
	int status;

	if (mutex_lock_interruptible(&adapter->mcc_lock))
		return -1;

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}

	req = embedded_payload(wrb);

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
			       OPCODE_COMMON_SET_FEATURES,
			       sizeof(*req), wrb, NULL);

	req->features = cpu_to_le32(BE_FEATURE_UE_RECOVERY);
	req->parameter_len = cpu_to_le32(sizeof(struct be_req_ue_recovery));
	req->parameter.req.uer = cpu_to_le32(BE_UE_RECOVERY_UER_MASK);

	status = be_mcc_notify_wait(adapter);
	if (status)
		goto err;

	resp = embedded_payload(wrb);

	adapter->error_recovery.ue_to_poll_time =
		le16_to_cpu(resp->parameter.resp.ue2rp);
	adapter->error_recovery.ue_to_reset_time =
		le16_to_cpu(resp->parameter.resp.ue2sr);
	adapter->error_recovery.recovery_supported = true;
err:
	/* Checking "MCC_STATUS_INVALID_LENGTH" for SKH as FW
	 * returns this error in older firmware versions
	 */
	if (base_status(status) == MCC_STATUS_ILLEGAL_REQUEST ||
	    base_status(status) == MCC_STATUS_INVALID_LENGTH)
		dev_info(&adapter->pdev->dev,
			 "Adapter does not support HW error recovery\n");

	mutex_unlock(&adapter->mcc_lock);
	return status;
}

int be_roce_mcc_cmd(void *netdev_handle, void *wrb_payload,
		    int wrb_payload_size, u16 *cmd_status, u16 *ext_status)
{
	struct be_adapter *adapter = netdev_priv(netdev_handle);
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_hdr *hdr = (struct be_cmd_req_hdr *)wrb_payload;
	struct be_cmd_req_hdr *req;
	struct be_cmd_resp_hdr *resp;
	int status;

	mutex_lock(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}
	req = embedded_payload(wrb);
	resp = embedded_payload(wrb);

	be_wrb_cmd_hdr_prepare(req, hdr->subsystem,
			       hdr->opcode, wrb_payload_size, wrb, NULL);
	memcpy(req, wrb_payload, wrb_payload_size);
	be_dws_cpu_to_le(req, wrb_payload_size);

	status = be_mcc_notify_wait(adapter);
	if (cmd_status)
		*cmd_status = (status & 0xffff);
	if (ext_status)
		*ext_status = 0;
	memcpy(wrb_payload, resp, sizeof(*resp) + resp->response_length);
	be_dws_le_to_cpu(wrb_payload, sizeof(*resp) + resp->response_length);
err:
	mutex_unlock(&adapter->mcc_lock);
	return status;
}
EXPORT_SYMBOL(be_roce_mcc_cmd);
