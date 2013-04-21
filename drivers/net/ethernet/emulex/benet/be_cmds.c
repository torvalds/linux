/*
 * Copyright (C) 2005 - 2013 Emulex
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.  The full GNU General
 * Public License is included in this distribution in the file called COPYING.
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
	}
};

static bool be_cmd_allowed(struct be_adapter *adapter, u8 opcode,
			   u8 subsystem)
{
	int i;
	int num_entries = sizeof(cmd_priv_map)/sizeof(struct be_cmd_priv_map);
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

static void be_mcc_notify(struct be_adapter *adapter)
{
	struct be_queue_info *mccq = &adapter->mcc_obj.q;
	u32 val = 0;

	if (be_error(adapter))
		return;

	val |= mccq->id & DB_MCCQ_RING_ID_MASK;
	val |= 1 << DB_MCCQ_NUM_POSTED_SHIFT;

	wmb();
	iowrite32(val, adapter->db + DB_MCCQ_OFFSET);
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

static int be_mcc_compl_process(struct be_adapter *adapter,
				struct be_mcc_compl *compl)
{
	u16 compl_status, extd_status;
	struct be_cmd_resp_hdr *resp_hdr;
	u8 opcode = 0, subsystem = 0;

	/* Just swap the status to host endian; mcc tag is opaquely copied
	 * from mcc_wrb */
	be_dws_le_to_cpu(compl, 4);

	compl_status = (compl->status >> CQE_STATUS_COMPL_SHIFT) &
				CQE_STATUS_COMPL_MASK;

	resp_hdr = be_decode_resp_hdr(compl->tag0, compl->tag1);

	if (resp_hdr) {
		opcode = resp_hdr->opcode;
		subsystem = resp_hdr->subsystem;
	}

	if (((opcode == OPCODE_COMMON_WRITE_FLASHROM) ||
	     (opcode == OPCODE_COMMON_WRITE_OBJECT)) &&
	    (subsystem == CMD_SUBSYSTEM_COMMON)) {
		adapter->flash_status = compl_status;
		complete(&adapter->flash_compl);
	}

	if (compl_status == MCC_STATUS_SUCCESS) {
		if (((opcode == OPCODE_ETH_GET_STATISTICS) ||
		     (opcode == OPCODE_ETH_GET_PPORT_STATS)) &&
		    (subsystem == CMD_SUBSYSTEM_ETH)) {
			be_parse_stats(adapter);
			adapter->stats_cmd_sent = false;
		}
		if (opcode == OPCODE_COMMON_GET_CNTL_ADDITIONAL_ATTRIBUTES &&
		    subsystem == CMD_SUBSYSTEM_COMMON) {
			struct be_cmd_resp_get_cntl_addnl_attribs *resp =
				(void *)resp_hdr;
			adapter->drv_stats.be_on_die_temperature =
				resp->on_die_temperature;
		}
	} else {
		if (opcode == OPCODE_COMMON_GET_CNTL_ADDITIONAL_ATTRIBUTES)
			adapter->be_get_temp_freq = 0;

		if (compl_status == MCC_STATUS_NOT_SUPPORTED ||
			compl_status == MCC_STATUS_ILLEGAL_REQUEST)
			goto done;

		if (compl_status == MCC_STATUS_UNAUTHORIZED_REQUEST) {
			dev_warn(&adapter->pdev->dev,
				 "VF is not privileged to issue opcode %d-%d\n",
				 opcode, subsystem);
		} else {
			extd_status = (compl->status >> CQE_STATUS_EXTD_SHIFT) &
					CQE_STATUS_EXTD_MASK;
			dev_err(&adapter->pdev->dev,
				"opcode %d-%d failed:status %d-%d\n",
				opcode, subsystem, compl_status, extd_status);
		}
	}
done:
	return compl_status;
}

/* Link state evt is a string of bytes; no need for endian swapping */
static void be_async_link_state_process(struct be_adapter *adapter,
		struct be_async_event_link_state *evt)
{
	/* When link status changes, link speed must be re-queried from FW */
	adapter->phy.link_speed = -1;

	/* Ignore physical link event */
	if (lancer_chip(adapter) &&
	    !(evt->port_link_status & LOGICAL_LINK_STATUS_MASK))
		return;

	/* For the initial link status do not rely on the ASYNC event as
	 * it may not be received in some cases.
	 */
	if (adapter->flags & BE_FLAGS_LINK_STATUS_INIT)
		be_link_status_update(adapter, evt->port_link_status);
}

/* Grp5 CoS Priority evt */
static void be_async_grp5_cos_priority_process(struct be_adapter *adapter,
		struct be_async_event_grp5_cos_priority *evt)
{
	if (evt->valid) {
		adapter->vlan_prio_bmap = evt->available_priority_bmap;
		adapter->recommended_prio &= ~VLAN_PRIO_MASK;
		adapter->recommended_prio =
			evt->reco_default_priority << VLAN_PRIO_SHIFT;
	}
}

/* Grp5 QOS Speed evt: qos_link_speed is in units of 10 Mbps */
static void be_async_grp5_qos_speed_process(struct be_adapter *adapter,
		struct be_async_event_grp5_qos_link_speed *evt)
{
	if (adapter->phy.link_speed >= 0 &&
	    evt->physical_port == adapter->port_num)
		adapter->phy.link_speed = le16_to_cpu(evt->qos_link_speed) * 10;
}

/*Grp5 PVID evt*/
static void be_async_grp5_pvid_state_process(struct be_adapter *adapter,
		struct be_async_event_grp5_pvid_state *evt)
{
	if (evt->enabled)
		adapter->pvid = le16_to_cpu(evt->tag) & VLAN_VID_MASK;
	else
		adapter->pvid = 0;
}

static void be_async_grp5_evt_process(struct be_adapter *adapter,
		u32 trailer, struct be_mcc_compl *evt)
{
	u8 event_type = 0;

	event_type = (trailer >> ASYNC_TRAILER_EVENT_TYPE_SHIFT) &
		ASYNC_TRAILER_EVENT_TYPE_MASK;

	switch (event_type) {
	case ASYNC_EVENT_COS_PRIORITY:
		be_async_grp5_cos_priority_process(adapter,
		(struct be_async_event_grp5_cos_priority *)evt);
	break;
	case ASYNC_EVENT_QOS_SPEED:
		be_async_grp5_qos_speed_process(adapter,
		(struct be_async_event_grp5_qos_link_speed *)evt);
	break;
	case ASYNC_EVENT_PVID_STATE:
		be_async_grp5_pvid_state_process(adapter,
		(struct be_async_event_grp5_pvid_state *)evt);
	break;
	default:
		dev_warn(&adapter->pdev->dev, "Unknown grp5 event!\n");
		break;
	}
}

static inline bool is_link_state_evt(u32 trailer)
{
	return ((trailer >> ASYNC_TRAILER_EVENT_CODE_SHIFT) &
		ASYNC_TRAILER_EVENT_CODE_MASK) ==
				ASYNC_EVENT_CODE_LINK_STATE;
}

static inline bool is_grp5_evt(u32 trailer)
{
	return (((trailer >> ASYNC_TRAILER_EVENT_CODE_SHIFT) &
		ASYNC_TRAILER_EVENT_CODE_MASK) ==
				ASYNC_EVENT_CODE_GRP_5);
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
			/* Interpret flags as an async trailer */
			if (is_link_state_evt(compl->flags))
				be_async_link_state_process(adapter,
				(struct be_async_event_link_state *) compl);
			else if (is_grp5_evt(compl->flags))
				be_async_grp5_evt_process(adapter,
				compl->flags, compl);
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
#define mcc_timeout		120000 /* 12s timeout */
	int i, status = 0;
	struct be_mcc_obj *mcc_obj = &adapter->mcc_obj;

	for (i = 0; i < mcc_timeout; i++) {
		if (be_error(adapter))
			return -EIO;

		local_bh_disable();
		status = be_process_mcc(adapter);
		local_bh_enable();

		if (atomic_read(&mcc_obj->q.used) == 0)
			break;
		udelay(100);
	}
	if (i == mcc_timeout) {
		dev_err(&adapter->pdev->dev, "FW not responding\n");
		adapter->fw_timeout = true;
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
	u16 index = mcc_obj->q.head;
	struct be_cmd_resp_hdr *resp;

	index_dec(&index, mcc_obj->q.len);
	wrb = queue_index_node(&mcc_obj->q, index);

	resp = be_decode_resp_hdr(wrb->tag0, wrb->tag1);

	be_mcc_notify(adapter);

	status = be_mcc_wait_compl(adapter);
	if (status == -EIO)
		goto out;

	status = resp->status;
out:
	return status;
}

static int be_mbox_db_ready_wait(struct be_adapter *adapter, void __iomem *db)
{
	int msecs = 0;
	u32 ready;

	do {
		if (be_error(adapter))
			return -EIO;

		ready = ioread32(db);
		if (ready == 0xffffffff)
			return -1;

		ready &= MPU_MAILBOX_DB_RDY_MASK;
		if (ready)
			break;

		if (msecs > 4000) {
			dev_err(&adapter->pdev->dev, "FW not responding\n");
			adapter->fw_timeout = true;
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

static u16 be_POST_stage_get(struct be_adapter *adapter)
{
	u32 sem;

	if (BEx_chip(adapter))
		sem  = ioread32(adapter->csr + SLIPORT_SEMAPHORE_OFFSET_BEx);
	else
		pci_read_config_dword(adapter->pdev,
				      SLIPORT_SEMAPHORE_OFFSET_SH, &sem);

	return sem & POST_STAGE_MASK;
}

int lancer_wait_ready(struct be_adapter *adapter)
{
#define SLIPORT_READY_TIMEOUT 30
	u32 sliport_status;
	int status = 0, i;

	for (i = 0; i < SLIPORT_READY_TIMEOUT; i++) {
		sliport_status = ioread32(adapter->db + SLIPORT_STATUS_OFFSET);
		if (sliport_status & SLIPORT_STATUS_RDY_MASK)
			break;

		msleep(1000);
	}

	if (i == SLIPORT_READY_TIMEOUT)
		status = -1;

	return status;
}

static bool lancer_provisioning_error(struct be_adapter *adapter)
{
	u32 sliport_status = 0, sliport_err1 = 0, sliport_err2 = 0;
	sliport_status = ioread32(adapter->db + SLIPORT_STATUS_OFFSET);
	if (sliport_status & SLIPORT_STATUS_ERR_MASK) {
		sliport_err1 = ioread32(adapter->db +
					SLIPORT_ERROR1_OFFSET);
		sliport_err2 = ioread32(adapter->db +
					SLIPORT_ERROR2_OFFSET);

		if (sliport_err1 == SLIPORT_ERROR_NO_RESOURCE1 &&
		    sliport_err2 == SLIPORT_ERROR_NO_RESOURCE2)
			return true;
	}
	return false;
}

int lancer_test_and_set_rdy_state(struct be_adapter *adapter)
{
	int status;
	u32 sliport_status, err, reset_needed;
	bool resource_error;

	resource_error = lancer_provisioning_error(adapter);
	if (resource_error)
		return -1;

	status = lancer_wait_ready(adapter);
	if (!status) {
		sliport_status = ioread32(adapter->db + SLIPORT_STATUS_OFFSET);
		err = sliport_status & SLIPORT_STATUS_ERR_MASK;
		reset_needed = sliport_status & SLIPORT_STATUS_RN_MASK;
		if (err && reset_needed) {
			iowrite32(SLI_PORT_CONTROL_IP_MASK,
				  adapter->db + SLIPORT_CONTROL_OFFSET);

			/* check adapter has corrected the error */
			status = lancer_wait_ready(adapter);
			sliport_status = ioread32(adapter->db +
						  SLIPORT_STATUS_OFFSET);
			sliport_status &= (SLIPORT_STATUS_ERR_MASK |
						SLIPORT_STATUS_RN_MASK);
			if (status || sliport_status)
				status = -1;
		} else if (err || reset_needed) {
			status = -1;
		}
	}
	/* Stop error recovery if error is not recoverable.
	 * No resource error is temporary errors and will go away
	 * when PF provisions resources.
	 */
	resource_error = lancer_provisioning_error(adapter);
	if (status == -1 && !resource_error)
		adapter->eeh_error = true;

	return status;
}

int be_fw_wait_ready(struct be_adapter *adapter)
{
	u16 stage;
	int status, timeout = 0;
	struct device *dev = &adapter->pdev->dev;

	if (lancer_chip(adapter)) {
		status = lancer_wait_ready(adapter);
		return status;
	}

	do {
		stage = be_POST_stage_get(adapter);
		if (stage == POST_STAGE_ARMFW_RDY)
			return 0;

		dev_info(dev, "Waiting for POST, %ds elapsed\n",
			 timeout);
		if (msleep_interruptible(2000)) {
			dev_err(dev, "Waiting for POST aborted\n");
			return -EINTR;
		}
		timeout += 2;
	} while (timeout < 60);

	dev_err(dev, "POST timeout; stage=0x%x\n", stage);
	return -1;
}


static inline struct be_sge *nonembedded_sgl(struct be_mcc_wrb *wrb)
{
	return &wrb->payload.sgl[0];
}


/* Don't touch the hdr after it's prepared */
/* mem will be NULL for embedded commands */
static void be_wrb_cmd_hdr_prepare(struct be_cmd_req_hdr *req_hdr,
				u8 subsystem, u8 opcode, int cmd_len,
				struct be_mcc_wrb *wrb, struct be_dma_mem *mem)
{
	struct be_sge *sge;
	unsigned long addr = (unsigned long)req_hdr;
	u64 req_addr = addr;

	req_hdr->opcode = opcode;
	req_hdr->subsystem = subsystem;
	req_hdr->request_length = cpu_to_le32(cmd_len - sizeof(*req_hdr));
	req_hdr->version = 0;

	wrb->tag0 = req_addr & 0xFFFFFFFF;
	wrb->tag1 = upper_32_bits(req_addr);

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

/* Converts interrupt delay in microseconds to multiplier value */
static u32 eq_delay_to_mult(u32 usec_delay)
{
#define MAX_INTR_RATE			651042
	const u32 round = 10;
	u32 multiplier;

	if (usec_delay == 0)
		multiplier = 0;
	else {
		u32 interrupt_rate = 1000000 / usec_delay;
		/* Max delay, corresponding to the lowest interrupt rate */
		if (interrupt_rate == 0)
			multiplier = 1023;
		else {
			multiplier = (MAX_INTR_RATE - interrupt_rate) * round;
			multiplier /= interrupt_rate;
			/* Round the multiplier to the closest value.*/
			multiplier = (multiplier + round/2) / round;
			multiplier = min(multiplier, (u32)1023);
		}
	}
	return multiplier;
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

int be_cmd_eq_create(struct be_adapter *adapter,
		struct be_queue_info *eq, int eq_delay)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_eq_create *req;
	struct be_dma_mem *q_mem = &eq->dma_mem;
	int status;

	if (mutex_lock_interruptible(&adapter->mbox_lock))
		return -1;

	wrb = wrb_from_mbox(adapter);
	req = embedded_payload(wrb);

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
		OPCODE_COMMON_EQ_CREATE, sizeof(*req), wrb, NULL);

	req->num_pages =  cpu_to_le16(PAGES_4K_SPANNED(q_mem->va, q_mem->size));

	AMAP_SET_BITS(struct amap_eq_context, valid, req->context, 1);
	/* 4byte eqe*/
	AMAP_SET_BITS(struct amap_eq_context, size, req->context, 0);
	AMAP_SET_BITS(struct amap_eq_context, count, req->context,
			__ilog2_u32(eq->len/256));
	AMAP_SET_BITS(struct amap_eq_context, delaymult, req->context,
			eq_delay_to_mult(eq_delay));
	be_dws_cpu_to_le(req->context, sizeof(req->context));

	be_cmd_page_addrs_prepare(req->pages, ARRAY_SIZE(req->pages), q_mem);

	status = be_mbox_notify_wait(adapter);
	if (!status) {
		struct be_cmd_resp_eq_create *resp = embedded_payload(wrb);
		eq->id = le16_to_cpu(resp->eq_id);
		eq->created = true;
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

	spin_lock_bh(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}
	req = embedded_payload(wrb);

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
		OPCODE_COMMON_NTWK_MAC_QUERY, sizeof(*req), wrb, NULL);
	req->type = MAC_ADDRESS_TYPE_NETWORK;
	if (permanent) {
		req->permanent = 1;
	} else {
		req->if_id = cpu_to_le16((u16) if_handle);
		req->pmac_id = cpu_to_le32(pmac_id);
		req->permanent = 0;
	}

	status = be_mcc_notify_wait(adapter);
	if (!status) {
		struct be_cmd_resp_mac_query *resp = embedded_payload(wrb);
		memcpy(mac_addr, resp->mac.addr, ETH_ALEN);
	}

err:
	spin_unlock_bh(&adapter->mcc_lock);
	return status;
}

/* Uses synchronous MCCQ */
int be_cmd_pmac_add(struct be_adapter *adapter, u8 *mac_addr,
		u32 if_id, u32 *pmac_id, u32 domain)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_pmac_add *req;
	int status;

	spin_lock_bh(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}
	req = embedded_payload(wrb);

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
		OPCODE_COMMON_NTWK_PMAC_ADD, sizeof(*req), wrb, NULL);

	req->hdr.domain = domain;
	req->if_id = cpu_to_le32(if_id);
	memcpy(req->mac_address, mac_addr, ETH_ALEN);

	status = be_mcc_notify_wait(adapter);
	if (!status) {
		struct be_cmd_resp_pmac_add *resp = embedded_payload(wrb);
		*pmac_id = le32_to_cpu(resp->pmac_id);
	}

err:
	spin_unlock_bh(&adapter->mcc_lock);

	 if (status == MCC_STATUS_UNAUTHORIZED_REQUEST)
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

	spin_lock_bh(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}
	req = embedded_payload(wrb);

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
		OPCODE_COMMON_NTWK_PMAC_DEL, sizeof(*req), wrb, NULL);

	req->hdr.domain = dom;
	req->if_id = cpu_to_le32(if_id);
	req->pmac_id = cpu_to_le32(pmac_id);

	status = be_mcc_notify_wait(adapter);

err:
	spin_unlock_bh(&adapter->mcc_lock);
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
		OPCODE_COMMON_CQ_CREATE, sizeof(*req), wrb, NULL);

	req->num_pages =  cpu_to_le16(PAGES_4K_SPANNED(q_mem->va, q_mem->size));
	if (lancer_chip(adapter)) {
		req->hdr.version = 2;
		req->page_size = 1; /* 1 for 4K */
		AMAP_SET_BITS(struct amap_cq_context_lancer, nodelay, ctxt,
								no_delay);
		AMAP_SET_BITS(struct amap_cq_context_lancer, count, ctxt,
						__ilog2_u32(cq->len/256));
		AMAP_SET_BITS(struct amap_cq_context_lancer, valid, ctxt, 1);
		AMAP_SET_BITS(struct amap_cq_context_lancer, eventable,
								ctxt, 1);
		AMAP_SET_BITS(struct amap_cq_context_lancer, eqid,
								ctxt, eq->id);
	} else {
		AMAP_SET_BITS(struct amap_cq_context_be, coalescwm, ctxt,
								coalesce_wm);
		AMAP_SET_BITS(struct amap_cq_context_be, nodelay,
								ctxt, no_delay);
		AMAP_SET_BITS(struct amap_cq_context_be, count, ctxt,
						__ilog2_u32(cq->len/256));
		AMAP_SET_BITS(struct amap_cq_context_be, valid, ctxt, 1);
		AMAP_SET_BITS(struct amap_cq_context_be, eventable, ctxt, 1);
		AMAP_SET_BITS(struct amap_cq_context_be, eqid, ctxt, eq->id);
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

int be_cmd_mccq_ext_create(struct be_adapter *adapter,
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
			OPCODE_COMMON_MCC_CREATE_EXT, sizeof(*req), wrb, NULL);

	req->num_pages = cpu_to_le16(PAGES_4K_SPANNED(q_mem->va, q_mem->size));
	if (lancer_chip(adapter)) {
		req->hdr.version = 1;
		req->cq_id = cpu_to_le16(cq->id);

		AMAP_SET_BITS(struct amap_mcc_context_lancer, ring_size, ctxt,
						be_encoded_q_len(mccq->len));
		AMAP_SET_BITS(struct amap_mcc_context_lancer, valid, ctxt, 1);
		AMAP_SET_BITS(struct amap_mcc_context_lancer, async_cq_id,
								ctxt, cq->id);
		AMAP_SET_BITS(struct amap_mcc_context_lancer, async_cq_valid,
								 ctxt, 1);

	} else {
		AMAP_SET_BITS(struct amap_mcc_context_be, valid, ctxt, 1);
		AMAP_SET_BITS(struct amap_mcc_context_be, ring_size, ctxt,
						be_encoded_q_len(mccq->len));
		AMAP_SET_BITS(struct amap_mcc_context_be, cq_id, ctxt, cq->id);
	}

	/* Subscribe to Link State and Group 5 Events(bits 1 and 5 set) */
	req->async_event_bitmap[0] = cpu_to_le32(0x00000022);
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

int be_cmd_mccq_org_create(struct be_adapter *adapter,
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
			OPCODE_COMMON_MCC_CREATE, sizeof(*req), wrb, NULL);

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
			struct be_queue_info *mccq,
			struct be_queue_info *cq)
{
	int status;

	status = be_cmd_mccq_ext_create(adapter, mccq, cq);
	if (status && !lancer_chip(adapter)) {
		dev_warn(&adapter->pdev->dev, "Upgrade to F/W ver 2.102.235.0 "
			"or newer to avoid conflicting priorities between NIC "
			"and FCoE traffic");
		status = be_cmd_mccq_org_create(adapter, mccq, cq);
	}
	return status;
}

int be_cmd_txq_create(struct be_adapter *adapter, struct be_tx_obj *txo)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_eth_tx_create *req;
	struct be_queue_info *txq = &txo->q;
	struct be_queue_info *cq = &txo->cq;
	struct be_dma_mem *q_mem = &txq->dma_mem;
	int status, ver = 0;

	spin_lock_bh(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}

	req = embedded_payload(wrb);

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_ETH,
		OPCODE_ETH_TX_CREATE, sizeof(*req), wrb, NULL);

	if (lancer_chip(adapter)) {
		req->hdr.version = 1;
		req->if_id = cpu_to_le16(adapter->if_handle);
	} else if (BEx_chip(adapter)) {
		if (adapter->function_caps & BE_FUNCTION_CAPS_SUPER_NIC)
			req->hdr.version = 2;
	} else { /* For SH */
		req->hdr.version = 2;
	}

	req->num_pages = PAGES_4K_SPANNED(q_mem->va, q_mem->size);
	req->ulp_num = BE_ULP1_NUM;
	req->type = BE_ETH_TX_RING_TYPE_STANDARD;
	req->cq_id = cpu_to_le16(cq->id);
	req->queue_size = be_encoded_q_len(txq->len);
	be_cmd_page_addrs_prepare(req->pages, ARRAY_SIZE(req->pages), q_mem);

	ver = req->hdr.version;

	status = be_mcc_notify_wait(adapter);
	if (!status) {
		struct be_cmd_resp_eth_tx_create *resp = embedded_payload(wrb);
		txq->id = le16_to_cpu(resp->cid);
		if (ver == 2)
			txo->db_offset = le32_to_cpu(resp->db_offset);
		else
			txo->db_offset = DB_TXULP1_OFFSET;
		txq->created = true;
	}

err:
	spin_unlock_bh(&adapter->mcc_lock);

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

	spin_lock_bh(&adapter->mcc_lock);

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
	spin_unlock_bh(&adapter->mcc_lock);
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

	spin_lock_bh(&adapter->mcc_lock);

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
	spin_unlock_bh(&adapter->mcc_lock);
	return status;
}

/* Create an rx filtering policy configuration on an i/f
 * Uses MCCQ
 */
int be_cmd_if_create(struct be_adapter *adapter, u32 cap_flags, u32 en_flags,
		     u32 *if_handle, u32 domain)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_if_create *req;
	int status;

	spin_lock_bh(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}
	req = embedded_payload(wrb);

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
		OPCODE_COMMON_NTWK_INTERFACE_CREATE, sizeof(*req), wrb, NULL);
	req->hdr.domain = domain;
	req->capability_flags = cpu_to_le32(cap_flags);
	req->enable_flags = cpu_to_le32(en_flags);

	req->pmac_invalid = true;

	status = be_mcc_notify_wait(adapter);
	if (!status) {
		struct be_cmd_resp_if_create *resp = embedded_payload(wrb);
		*if_handle = le32_to_cpu(resp->interface_id);
	}

err:
	spin_unlock_bh(&adapter->mcc_lock);
	return status;
}

/* Uses MCCQ */
int be_cmd_if_destroy(struct be_adapter *adapter, int interface_id, u32 domain)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_if_destroy *req;
	int status;

	if (interface_id == -1)
		return 0;

	spin_lock_bh(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}
	req = embedded_payload(wrb);

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
		OPCODE_COMMON_NTWK_INTERFACE_DESTROY, sizeof(*req), wrb, NULL);
	req->hdr.domain = domain;
	req->interface_id = cpu_to_le32(interface_id);

	status = be_mcc_notify_wait(adapter);
err:
	spin_unlock_bh(&adapter->mcc_lock);
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

	spin_lock_bh(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}
	hdr = nonemb_cmd->va;

	be_wrb_cmd_hdr_prepare(hdr, CMD_SUBSYSTEM_ETH,
		OPCODE_ETH_GET_STATISTICS, nonemb_cmd->size, wrb, nonemb_cmd);

	/* version 1 of the cmd is not supported only by BE2 */
	if (!BE2_chip(adapter))
		hdr->version = 1;

	be_mcc_notify(adapter);
	adapter->stats_cmd_sent = true;

err:
	spin_unlock_bh(&adapter->mcc_lock);
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

	spin_lock_bh(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}
	req = nonemb_cmd->va;

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_ETH,
			OPCODE_ETH_GET_PPORT_STATS, nonemb_cmd->size, wrb,
			nonemb_cmd);

	req->cmd_params.params.pport_num = cpu_to_le16(adapter->hba_port_num);
	req->cmd_params.params.reset_stats = 0;

	be_mcc_notify(adapter);
	adapter->stats_cmd_sent = true;

err:
	spin_unlock_bh(&adapter->mcc_lock);
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

	spin_lock_bh(&adapter->mcc_lock);

	if (link_status)
		*link_status = LINK_DOWN;

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}
	req = embedded_payload(wrb);

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
		OPCODE_COMMON_NTWK_LINK_STATUS_QUERY, sizeof(*req), wrb, NULL);

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
	spin_unlock_bh(&adapter->mcc_lock);
	return status;
}

/* Uses synchronous mcc */
int be_cmd_get_die_temperature(struct be_adapter *adapter)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_get_cntl_addnl_attribs *req;
	int status;

	spin_lock_bh(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}
	req = embedded_payload(wrb);

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
		OPCODE_COMMON_GET_CNTL_ADDITIONAL_ATTRIBUTES, sizeof(*req),
		wrb, NULL);

	be_mcc_notify(adapter);

err:
	spin_unlock_bh(&adapter->mcc_lock);
	return status;
}

/* Uses synchronous mcc */
int be_cmd_get_reg_len(struct be_adapter *adapter, u32 *log_size)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_get_fat *req;
	int status;

	spin_lock_bh(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}
	req = embedded_payload(wrb);

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
		OPCODE_COMMON_MANAGE_FAT, sizeof(*req), wrb, NULL);
	req->fat_operation = cpu_to_le32(QUERY_FAT);
	status = be_mcc_notify_wait(adapter);
	if (!status) {
		struct be_cmd_resp_get_fat *resp = embedded_payload(wrb);
		if (log_size && resp->log_size)
			*log_size = le32_to_cpu(resp->log_size) -
					sizeof(u32);
	}
err:
	spin_unlock_bh(&adapter->mcc_lock);
	return status;
}

void be_cmd_get_regs(struct be_adapter *adapter, u32 buf_len, void *buf)
{
	struct be_dma_mem get_fat_cmd;
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_get_fat *req;
	u32 offset = 0, total_size, buf_size,
				log_offset = sizeof(u32), payload_len;
	int status;

	if (buf_len == 0)
		return;

	total_size = buf_len;

	get_fat_cmd.size = sizeof(struct be_cmd_req_get_fat) + 60*1024;
	get_fat_cmd.va = pci_alloc_consistent(adapter->pdev,
			get_fat_cmd.size,
			&get_fat_cmd.dma);
	if (!get_fat_cmd.va) {
		status = -ENOMEM;
		dev_err(&adapter->pdev->dev,
		"Memory allocation failure while retrieving FAT data\n");
		return;
	}

	spin_lock_bh(&adapter->mcc_lock);

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
				OPCODE_COMMON_MANAGE_FAT, payload_len, wrb,
				&get_fat_cmd);

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
	pci_free_consistent(adapter->pdev, get_fat_cmd.size,
			get_fat_cmd.va,
			get_fat_cmd.dma);
	spin_unlock_bh(&adapter->mcc_lock);
}

/* Uses synchronous mcc */
int be_cmd_get_fw_ver(struct be_adapter *adapter, char *fw_ver,
			char *fw_on_flash)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_get_fw_version *req;
	int status;

	spin_lock_bh(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}

	req = embedded_payload(wrb);

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
		OPCODE_COMMON_GET_FW_VERSION, sizeof(*req), wrb, NULL);
	status = be_mcc_notify_wait(adapter);
	if (!status) {
		struct be_cmd_resp_get_fw_version *resp = embedded_payload(wrb);
		strcpy(fw_ver, resp->firmware_version_string);
		if (fw_on_flash)
			strcpy(fw_on_flash, resp->fw_on_flash_version_string);
	}
err:
	spin_unlock_bh(&adapter->mcc_lock);
	return status;
}

/* set the EQ delay interval of an EQ to specified value
 * Uses async mcc
 */
int be_cmd_modify_eqd(struct be_adapter *adapter, u32 eq_id, u32 eqd)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_modify_eq_delay *req;
	int status = 0;

	spin_lock_bh(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}
	req = embedded_payload(wrb);

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
		OPCODE_COMMON_MODIFY_EQ_DELAY, sizeof(*req), wrb, NULL);

	req->num_eq = cpu_to_le32(1);
	req->delay[0].eq_id = cpu_to_le32(eq_id);
	req->delay[0].phase = 0;
	req->delay[0].delay_multiplier = cpu_to_le32(eqd);

	be_mcc_notify(adapter);

err:
	spin_unlock_bh(&adapter->mcc_lock);
	return status;
}

/* Uses sycnhronous mcc */
int be_cmd_vlan_config(struct be_adapter *adapter, u32 if_id, u16 *vtag_array,
			u32 num, bool untagged, bool promiscuous)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_vlan_config *req;
	int status;

	spin_lock_bh(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}
	req = embedded_payload(wrb);

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
		OPCODE_COMMON_NTWK_VLAN_CONFIG, sizeof(*req), wrb, NULL);

	req->interface_id = if_id;
	req->promiscuous = promiscuous;
	req->untagged = untagged;
	req->num_vlan = num;
	if (!promiscuous) {
		memcpy(req->normal_vlan, vtag_array,
			req->num_vlan * sizeof(vtag_array[0]));
	}

	status = be_mcc_notify_wait(adapter);

err:
	spin_unlock_bh(&adapter->mcc_lock);
	return status;
}

int be_cmd_rx_filter(struct be_adapter *adapter, u32 flags, u32 value)
{
	struct be_mcc_wrb *wrb;
	struct be_dma_mem *mem = &adapter->rx_filter;
	struct be_cmd_req_rx_filter *req = mem->va;
	int status;

	spin_lock_bh(&adapter->mcc_lock);

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
	if (flags & IFF_PROMISC) {
		req->if_flags_mask = cpu_to_le32(BE_IF_FLAGS_PROMISCUOUS |
					BE_IF_FLAGS_VLAN_PROMISCUOUS);
		if (value == ON)
			req->if_flags = cpu_to_le32(BE_IF_FLAGS_PROMISCUOUS |
						BE_IF_FLAGS_VLAN_PROMISCUOUS);
	} else if (flags & IFF_ALLMULTI) {
		req->if_flags_mask = req->if_flags =
				cpu_to_le32(BE_IF_FLAGS_MCAST_PROMISCUOUS);
	} else {
		struct netdev_hw_addr *ha;
		int i = 0;

		req->if_flags_mask = req->if_flags =
				cpu_to_le32(BE_IF_FLAGS_MULTICAST);

		/* Reset mcast promisc mode if already set by setting mask
		 * and not setting flags field
		 */
		req->if_flags_mask |=
			cpu_to_le32(BE_IF_FLAGS_MCAST_PROMISCUOUS &
				    adapter->if_cap_flags);

		req->mcast_num = cpu_to_le32(netdev_mc_count(adapter->netdev));
		netdev_for_each_mc_addr(ha, adapter->netdev)
			memcpy(req->mcast_mac[i++].byte, ha->addr, ETH_ALEN);
	}

	status = be_mcc_notify_wait(adapter);
err:
	spin_unlock_bh(&adapter->mcc_lock);
	return status;
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

	spin_lock_bh(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}
	req = embedded_payload(wrb);

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
		OPCODE_COMMON_SET_FLOW_CONTROL, sizeof(*req), wrb, NULL);

	req->tx_flow_control = cpu_to_le16((u16)tx_fc);
	req->rx_flow_control = cpu_to_le16((u16)rx_fc);

	status = be_mcc_notify_wait(adapter);

err:
	spin_unlock_bh(&adapter->mcc_lock);
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

	spin_lock_bh(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}
	req = embedded_payload(wrb);

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
		OPCODE_COMMON_GET_FLOW_CONTROL, sizeof(*req), wrb, NULL);

	status = be_mcc_notify_wait(adapter);
	if (!status) {
		struct be_cmd_resp_get_flow_control *resp =
						embedded_payload(wrb);
		*tx_fc = le16_to_cpu(resp->tx_flow_control);
		*rx_fc = le16_to_cpu(resp->rx_flow_control);
	}

err:
	spin_unlock_bh(&adapter->mcc_lock);
	return status;
}

/* Uses mbox */
int be_cmd_query_fw_cfg(struct be_adapter *adapter, u32 *port_num,
			u32 *mode, u32 *caps, u16 *asic_rev)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_query_fw_cfg *req;
	int status;

	if (mutex_lock_interruptible(&adapter->mbox_lock))
		return -1;

	wrb = wrb_from_mbox(adapter);
	req = embedded_payload(wrb);

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
		OPCODE_COMMON_QUERY_FIRMWARE_CONFIG, sizeof(*req), wrb, NULL);

	status = be_mbox_notify_wait(adapter);
	if (!status) {
		struct be_cmd_resp_query_fw_cfg *resp = embedded_payload(wrb);
		*port_num = le32_to_cpu(resp->phys_port);
		*mode = le32_to_cpu(resp->function_mode);
		*caps = le32_to_cpu(resp->function_caps);
		*asic_rev = le32_to_cpu(resp->asic_revision) & 0xFF;
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
		status = lancer_wait_ready(adapter);
		if (!status) {
			iowrite32(SLI_PORT_CONTROL_IP_MASK,
				  adapter->db + SLIPORT_CONTROL_OFFSET);
			status = lancer_test_and_set_rdy_state(adapter);
		}
		if (status) {
			dev_err(&adapter->pdev->dev,
				"Adapter in non recoverable error\n");
		}
		return status;
	}

	if (mutex_lock_interruptible(&adapter->mbox_lock))
		return -1;

	wrb = wrb_from_mbox(adapter);
	req = embedded_payload(wrb);

	be_wrb_cmd_hdr_prepare(req, CMD_SUBSYSTEM_COMMON,
		OPCODE_COMMON_FUNCTION_RESET, sizeof(*req), wrb, NULL);

	status = be_mbox_notify_wait(adapter);

	mutex_unlock(&adapter->mbox_lock);
	return status;
}

int be_cmd_rss_config(struct be_adapter *adapter, u8 *rsstable, u16 table_size)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_rss_config *req;
	u32 myhash[10] = {0x15d43fa5, 0x2534685a, 0x5f87693a, 0x5668494e,
			0x33cf6a53, 0x383334c6, 0x76ac4257, 0x59b242b2,
			0x3ea83c02, 0x4a110304};
	int status;

	if (mutex_lock_interruptible(&adapter->mbox_lock))
		return -1;

	wrb = wrb_from_mbox(adapter);
	req = embedded_payload(wrb);

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_ETH,
		OPCODE_ETH_RSS_CONFIG, sizeof(*req), wrb, NULL);

	req->if_id = cpu_to_le32(adapter->if_handle);
	req->enable_rss = cpu_to_le16(RSS_ENABLE_TCP_IPV4 | RSS_ENABLE_IPV4 |
				      RSS_ENABLE_TCP_IPV6 | RSS_ENABLE_IPV6);

	if (lancer_chip(adapter) || skyhawk_chip(adapter)) {
		req->hdr.version = 1;
		req->enable_rss |= cpu_to_le16(RSS_ENABLE_UDP_IPV4 |
					       RSS_ENABLE_UDP_IPV6);
	}

	req->cpu_table_size_log2 = cpu_to_le16(fls(table_size) - 1);
	memcpy(req->cpu_table, rsstable, table_size);
	memcpy(req->hash, myhash, sizeof(myhash));
	be_dws_cpu_to_le(req->hash, sizeof(req->hash));

	status = be_mbox_notify_wait(adapter);

	mutex_unlock(&adapter->mbox_lock);
	return status;
}

/* Uses sync mcc */
int be_cmd_set_beacon_state(struct be_adapter *adapter, u8 port_num,
			u8 bcn, u8 sts, u8 state)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_enable_disable_beacon *req;
	int status;

	spin_lock_bh(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}
	req = embedded_payload(wrb);

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
		OPCODE_COMMON_ENABLE_DISABLE_BEACON, sizeof(*req), wrb, NULL);

	req->port_num = port_num;
	req->beacon_state = state;
	req->beacon_duration = bcn;
	req->status_duration = sts;

	status = be_mcc_notify_wait(adapter);

err:
	spin_unlock_bh(&adapter->mcc_lock);
	return status;
}

/* Uses sync mcc */
int be_cmd_get_beacon_state(struct be_adapter *adapter, u8 port_num, u32 *state)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_get_beacon_state *req;
	int status;

	spin_lock_bh(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}
	req = embedded_payload(wrb);

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
		OPCODE_COMMON_GET_BEACON_STATE, sizeof(*req), wrb, NULL);

	req->port_num = port_num;

	status = be_mcc_notify_wait(adapter);
	if (!status) {
		struct be_cmd_resp_get_beacon_state *resp =
						embedded_payload(wrb);
		*state = resp->beacon_state;
	}

err:
	spin_unlock_bh(&adapter->mcc_lock);
	return status;
}

int lancer_cmd_write_object(struct be_adapter *adapter, struct be_dma_mem *cmd,
			    u32 data_size, u32 data_offset,
			    const char *obj_name, u32 *data_written,
			    u8 *change_status, u8 *addn_status)
{
	struct be_mcc_wrb *wrb;
	struct lancer_cmd_req_write_object *req;
	struct lancer_cmd_resp_write_object *resp;
	void *ctxt = NULL;
	int status;

	spin_lock_bh(&adapter->mcc_lock);
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
	strcpy(req->object_name, obj_name);
	req->descriptor_count = cpu_to_le32(1);
	req->buf_len = cpu_to_le32(data_size);
	req->addr_low = cpu_to_le32((cmd->dma +
				sizeof(struct lancer_cmd_req_write_object))
				& 0xFFFFFFFF);
	req->addr_high = cpu_to_le32(upper_32_bits(cmd->dma +
				sizeof(struct lancer_cmd_req_write_object)));

	be_mcc_notify(adapter);
	spin_unlock_bh(&adapter->mcc_lock);

	if (!wait_for_completion_timeout(&adapter->flash_compl,
					 msecs_to_jiffies(30000)))
		status = -1;
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
	spin_unlock_bh(&adapter->mcc_lock);
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

	spin_lock_bh(&adapter->mcc_lock);

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
	spin_unlock_bh(&adapter->mcc_lock);
	return status;
}

int be_cmd_write_flashrom(struct be_adapter *adapter, struct be_dma_mem *cmd,
			u32 flash_type, u32 flash_opcode, u32 buf_size)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_write_flashrom *req;
	int status;

	spin_lock_bh(&adapter->mcc_lock);
	adapter->flash_status = 0;

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err_unlock;
	}
	req = cmd->va;

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
		OPCODE_COMMON_WRITE_FLASHROM, cmd->size, wrb, cmd);

	req->params.op_type = cpu_to_le32(flash_type);
	req->params.op_code = cpu_to_le32(flash_opcode);
	req->params.data_buf_size = cpu_to_le32(buf_size);

	be_mcc_notify(adapter);
	spin_unlock_bh(&adapter->mcc_lock);

	if (!wait_for_completion_timeout(&adapter->flash_compl,
			msecs_to_jiffies(40000)))
		status = -1;
	else
		status = adapter->flash_status;

	return status;

err_unlock:
	spin_unlock_bh(&adapter->mcc_lock);
	return status;
}

int be_cmd_get_flash_crc(struct be_adapter *adapter, u8 *flashed_crc,
			 int offset)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_read_flash_crc *req;
	int status;

	spin_lock_bh(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}
	req = embedded_payload(wrb);

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
			       OPCODE_COMMON_READ_FLASHROM, sizeof(*req),
			       wrb, NULL);

	req->params.op_type = cpu_to_le32(OPTYPE_REDBOOT);
	req->params.op_code = cpu_to_le32(FLASHROM_OPER_REPORT);
	req->params.offset = cpu_to_le32(offset);
	req->params.data_buf_size = cpu_to_le32(0x4);

	status = be_mcc_notify_wait(adapter);
	if (!status)
		memcpy(flashed_crc, req->crc, 4);

err:
	spin_unlock_bh(&adapter->mcc_lock);
	return status;
}

int be_cmd_enable_magic_wol(struct be_adapter *adapter, u8 *mac,
				struct be_dma_mem *nonemb_cmd)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_acpi_wol_magic_config *req;
	int status;

	spin_lock_bh(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}
	req = nonemb_cmd->va;

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_ETH,
		OPCODE_ETH_ACPI_WOL_MAGIC_CONFIG, sizeof(*req), wrb,
		nonemb_cmd);
	memcpy(req->magic_mac, mac, ETH_ALEN);

	status = be_mcc_notify_wait(adapter);

err:
	spin_unlock_bh(&adapter->mcc_lock);
	return status;
}

int be_cmd_set_loopback(struct be_adapter *adapter, u8 port_num,
			u8 loopback_type, u8 enable)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_set_lmode *req;
	int status;

	spin_lock_bh(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}

	req = embedded_payload(wrb);

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_LOWLEVEL,
			OPCODE_LOWLEVEL_SET_LOOPBACK_MODE, sizeof(*req), wrb,
			NULL);

	req->src_port = port_num;
	req->dest_port = port_num;
	req->loopback_type = loopback_type;
	req->loopback_state = enable;

	status = be_mcc_notify_wait(adapter);
err:
	spin_unlock_bh(&adapter->mcc_lock);
	return status;
}

int be_cmd_loopback_test(struct be_adapter *adapter, u32 port_num,
		u32 loopback_type, u32 pkt_size, u32 num_pkts, u64 pattern)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_loopback_test *req;
	int status;

	spin_lock_bh(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}

	req = embedded_payload(wrb);

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_LOWLEVEL,
			OPCODE_LOWLEVEL_LOOPBACK_TEST, sizeof(*req), wrb, NULL);
	req->hdr.timeout = cpu_to_le32(4);

	req->pattern = cpu_to_le64(pattern);
	req->src_port = cpu_to_le32(port_num);
	req->dest_port = cpu_to_le32(port_num);
	req->pkt_size = cpu_to_le32(pkt_size);
	req->num_pkts = cpu_to_le32(num_pkts);
	req->loopback_type = cpu_to_le32(loopback_type);

	status = be_mcc_notify_wait(adapter);
	if (!status) {
		struct be_cmd_resp_loopback_test *resp = embedded_payload(wrb);
		status = le32_to_cpu(resp->status);
	}

err:
	spin_unlock_bh(&adapter->mcc_lock);
	return status;
}

int be_cmd_ddr_dma_test(struct be_adapter *adapter, u64 pattern,
				u32 byte_cnt, struct be_dma_mem *cmd)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_ddrdma_test *req;
	int status;
	int i, j = 0;

	spin_lock_bh(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}
	req = cmd->va;
	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_LOWLEVEL,
			OPCODE_LOWLEVEL_HOST_DDR_DMA, cmd->size, wrb, cmd);

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
	spin_unlock_bh(&adapter->mcc_lock);
	return status;
}

int be_cmd_get_seeprom_data(struct be_adapter *adapter,
				struct be_dma_mem *nonemb_cmd)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_seeprom_read *req;
	int status;

	spin_lock_bh(&adapter->mcc_lock);

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
	spin_unlock_bh(&adapter->mcc_lock);
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

	spin_lock_bh(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}
	cmd.size = sizeof(struct be_cmd_req_get_phy_info);
	cmd.va = pci_alloc_consistent(adapter->pdev, cmd.size,
					&cmd.dma);
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
	}
	pci_free_consistent(adapter->pdev, cmd.size,
				cmd.va, cmd.dma);
err:
	spin_unlock_bh(&adapter->mcc_lock);
	return status;
}

int be_cmd_set_qos(struct be_adapter *adapter, u32 bps, u32 domain)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_set_qos *req;
	int status;

	spin_lock_bh(&adapter->mcc_lock);

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
	spin_unlock_bh(&adapter->mcc_lock);
	return status;
}

int be_cmd_get_cntl_attributes(struct be_adapter *adapter)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_cntl_attribs *req;
	struct be_cmd_resp_cntl_attribs *resp;
	int status;
	int payload_len = max(sizeof(*req), sizeof(*resp));
	struct mgmt_controller_attrib *attribs;
	struct be_dma_mem attribs_cmd;

	memset(&attribs_cmd, 0, sizeof(struct be_dma_mem));
	attribs_cmd.size = sizeof(struct be_cmd_resp_cntl_attribs);
	attribs_cmd.va = pci_alloc_consistent(adapter->pdev, attribs_cmd.size,
						&attribs_cmd.dma);
	if (!attribs_cmd.va) {
		dev_err(&adapter->pdev->dev,
				"Memory allocation failure\n");
		return -ENOMEM;
	}

	if (mutex_lock_interruptible(&adapter->mbox_lock))
		return -1;

	wrb = wrb_from_mbox(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}
	req = attribs_cmd.va;

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
			 OPCODE_COMMON_GET_CNTL_ATTRIBUTES, payload_len, wrb,
			&attribs_cmd);

	status = be_mbox_notify_wait(adapter);
	if (!status) {
		attribs = attribs_cmd.va + sizeof(struct be_cmd_resp_hdr);
		adapter->hba_port_num = attribs->hba_attribs.phy_port;
	}

err:
	mutex_unlock(&adapter->mbox_lock);
	pci_free_consistent(adapter->pdev, attribs_cmd.size, attribs_cmd.va,
					attribs_cmd.dma);
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
		OPCODE_COMMON_SET_DRIVER_FUNCTION_CAP, sizeof(*req), wrb, NULL);

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

	spin_lock_bh(&adapter->mcc_lock);

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
	}

err:
	spin_unlock_bh(&adapter->mcc_lock);
	return status;
}

/* Uses synchronous MCCQ */
int be_cmd_get_mac_from_list(struct be_adapter *adapter, u8 *mac,
			     bool *pmac_id_active, u32 *pmac_id, u8 domain)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_get_mac_list *req;
	int status;
	int mac_count;
	struct be_dma_mem get_mac_list_cmd;
	int i;

	memset(&get_mac_list_cmd, 0, sizeof(struct be_dma_mem));
	get_mac_list_cmd.size = sizeof(struct be_cmd_resp_get_mac_list);
	get_mac_list_cmd.va = pci_alloc_consistent(adapter->pdev,
			get_mac_list_cmd.size,
			&get_mac_list_cmd.dma);

	if (!get_mac_list_cmd.va) {
		dev_err(&adapter->pdev->dev,
				"Memory allocation failure during GET_MAC_LIST\n");
		return -ENOMEM;
	}

	spin_lock_bh(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto out;
	}

	req = get_mac_list_cmd.va;

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
				OPCODE_COMMON_GET_MAC_LIST, sizeof(*req),
				wrb, &get_mac_list_cmd);

	req->hdr.domain = domain;
	req->mac_type = MAC_ADDRESS_TYPE_NETWORK;
	req->perm_override = 1;

	status = be_mcc_notify_wait(adapter);
	if (!status) {
		struct be_cmd_resp_get_mac_list *resp =
						get_mac_list_cmd.va;
		mac_count = resp->true_mac_count + resp->pseudo_mac_count;
		/* Mac list returned could contain one or more active mac_ids
		 * or one or more true or pseudo permanant mac addresses.
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
				*pmac_id_active = true;
				mac_id = mac_entry->mac_addr_id.s_mac_id.mac_id;
				*pmac_id = le32_to_cpu(mac_id);
				goto out;
			}
		}
		/* If no active mac_id found, return first mac addr */
		*pmac_id_active = false;
		memcpy(mac, resp->macaddr_list[0].mac_addr_id.macaddr,
								ETH_ALEN);
	}

out:
	spin_unlock_bh(&adapter->mcc_lock);
	pci_free_consistent(adapter->pdev, get_mac_list_cmd.size,
			get_mac_list_cmd.va, get_mac_list_cmd.dma);
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
	cmd.va = dma_alloc_coherent(&adapter->pdev->dev, cmd.size,
			&cmd.dma, GFP_KERNEL);
	if (!cmd.va)
		return -ENOMEM;

	spin_lock_bh(&adapter->mcc_lock);

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
	dma_free_coherent(&adapter->pdev->dev, cmd.size,
				cmd.va, cmd.dma);
	spin_unlock_bh(&adapter->mcc_lock);
	return status;
}

int be_cmd_set_hsw_config(struct be_adapter *adapter, u16 pvid,
			u32 domain, u16 intf_id)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_set_hsw_config *req;
	void *ctxt;
	int status;

	spin_lock_bh(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}

	req = embedded_payload(wrb);
	ctxt = &req->context;

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
			OPCODE_COMMON_SET_HSW_CONFIG, sizeof(*req), wrb, NULL);

	req->hdr.domain = domain;
	AMAP_SET_BITS(struct amap_set_hsw_context, interface_id, ctxt, intf_id);
	if (pvid) {
		AMAP_SET_BITS(struct amap_set_hsw_context, pvid_valid, ctxt, 1);
		AMAP_SET_BITS(struct amap_set_hsw_context, pvid, ctxt, pvid);
	}

	be_dws_cpu_to_le(req->context, sizeof(req->context));
	status = be_mcc_notify_wait(adapter);

err:
	spin_unlock_bh(&adapter->mcc_lock);
	return status;
}

/* Get Hyper switch config */
int be_cmd_get_hsw_config(struct be_adapter *adapter, u16 *pvid,
			u32 domain, u16 intf_id)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_get_hsw_config *req;
	void *ctxt;
	int status;
	u16 vid;

	spin_lock_bh(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}

	req = embedded_payload(wrb);
	ctxt = &req->context;

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
			OPCODE_COMMON_GET_HSW_CONFIG, sizeof(*req), wrb, NULL);

	req->hdr.domain = domain;
	AMAP_SET_BITS(struct amap_get_hsw_req_context, interface_id, ctxt,
								intf_id);
	AMAP_SET_BITS(struct amap_get_hsw_req_context, pvid_valid, ctxt, 1);
	be_dws_cpu_to_le(req->context, sizeof(req->context));

	status = be_mcc_notify_wait(adapter);
	if (!status) {
		struct be_cmd_resp_get_hsw_config *resp =
						embedded_payload(wrb);
		be_dws_le_to_cpu(&resp->context,
						sizeof(resp->context));
		vid = AMAP_GET_BITS(struct amap_get_hsw_resp_context,
							pvid, &resp->context);
		*pvid = le16_to_cpu(vid);
	}

err:
	spin_unlock_bh(&adapter->mcc_lock);
	return status;
}

int be_cmd_get_acpi_wol_cap(struct be_adapter *adapter)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_acpi_wol_magic_config_v1 *req;
	int status;
	int payload_len = sizeof(*req);
	struct be_dma_mem cmd;

	if (!be_cmd_allowed(adapter, OPCODE_ETH_ACPI_WOL_MAGIC_CONFIG,
			    CMD_SUBSYSTEM_ETH))
		return -EPERM;

	memset(&cmd, 0, sizeof(struct be_dma_mem));
	cmd.size = sizeof(struct be_cmd_resp_acpi_wol_magic_config_v1);
	cmd.va = pci_alloc_consistent(adapter->pdev, cmd.size,
					       &cmd.dma);
	if (!cmd.va) {
		dev_err(&adapter->pdev->dev,
				"Memory allocation failure\n");
		return -ENOMEM;
	}

	if (mutex_lock_interruptible(&adapter->mbox_lock))
		return -1;

	wrb = wrb_from_mbox(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}

	req = cmd.va;

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_ETH,
			       OPCODE_ETH_ACPI_WOL_MAGIC_CONFIG,
			       payload_len, wrb, &cmd);

	req->hdr.version = 1;
	req->query_options = BE_GET_WOL_CAP;

	status = be_mbox_notify_wait(adapter);
	if (!status) {
		struct be_cmd_resp_acpi_wol_magic_config_v1 *resp;
		resp = (struct be_cmd_resp_acpi_wol_magic_config_v1 *) cmd.va;

		/* the command could succeed misleadingly on old f/w
		 * which is not aware of the V1 version. fake an error. */
		if (resp->hdr.response_length < payload_len) {
			status = -1;
			goto err;
		}
		adapter->wol_cap = resp->wol_settings;
	}
err:
	mutex_unlock(&adapter->mbox_lock);
	pci_free_consistent(adapter->pdev, cmd.size, cmd.va, cmd.dma);
	return status;

}
int be_cmd_get_ext_fat_capabilites(struct be_adapter *adapter,
				   struct be_dma_mem *cmd)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_get_ext_fat_caps *req;
	int status;

	if (mutex_lock_interruptible(&adapter->mbox_lock))
		return -1;

	wrb = wrb_from_mbox(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}

	req = cmd->va;
	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
			       OPCODE_COMMON_GET_EXT_FAT_CAPABILITES,
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

	spin_lock_bh(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}

	req = cmd->va;
	memcpy(&req->set_params, configs, sizeof(struct be_fat_conf_params));
	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
			       OPCODE_COMMON_SET_EXT_FAT_CAPABILITES,
			       cmd->size, wrb, cmd);

	status = be_mcc_notify_wait(adapter);
err:
	spin_unlock_bh(&adapter->mcc_lock);
	return status;
}

int be_cmd_query_port_name(struct be_adapter *adapter, u8 *port_name)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_get_port_name *req;
	int status;

	if (!lancer_chip(adapter)) {
		*port_name = adapter->hba_port_num + '0';
		return 0;
	}

	spin_lock_bh(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}

	req = embedded_payload(wrb);

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
			       OPCODE_COMMON_GET_PORT_NAME, sizeof(*req), wrb,
			       NULL);
	req->hdr.version = 1;

	status = be_mcc_notify_wait(adapter);
	if (!status) {
		struct be_cmd_resp_get_port_name *resp = embedded_payload(wrb);
		*port_name = resp->port_name[adapter->hba_port_num];
	} else {
		*port_name = adapter->hba_port_num + '0';
	}
err:
	spin_unlock_bh(&adapter->mcc_lock);
	return status;
}

static struct be_nic_resource_desc *be_get_nic_desc(u8 *buf, u32 desc_count,
						    u32 max_buf_size)
{
	struct be_nic_resource_desc *desc = (struct be_nic_resource_desc *)buf;
	int i;

	for (i = 0; i < desc_count; i++) {
		desc->desc_len = RESOURCE_DESC_SIZE;
		if (((void *)desc + desc->desc_len) >
		    (void *)(buf + max_buf_size)) {
			desc = NULL;
			break;
		}

		if (desc->desc_type == NIC_RESOURCE_DESC_TYPE_ID)
			break;

		desc = (void *)desc + desc->desc_len;
	}

	if (!desc || i == MAX_RESOURCE_DESC)
		return NULL;

	return desc;
}

/* Uses Mbox */
int be_cmd_get_func_config(struct be_adapter *adapter)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_get_func_config *req;
	int status;
	struct be_dma_mem cmd;

	memset(&cmd, 0, sizeof(struct be_dma_mem));
	cmd.size = sizeof(struct be_cmd_resp_get_func_config);
	cmd.va = pci_alloc_consistent(adapter->pdev, cmd.size,
				      &cmd.dma);
	if (!cmd.va) {
		dev_err(&adapter->pdev->dev, "Memory alloc failure\n");
		return -ENOMEM;
	}
	if (mutex_lock_interruptible(&adapter->mbox_lock))
		return -1;

	wrb = wrb_from_mbox(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}

	req = cmd.va;

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
			       OPCODE_COMMON_GET_FUNC_CONFIG,
			       cmd.size, wrb, &cmd);

	status = be_mbox_notify_wait(adapter);
	if (!status) {
		struct be_cmd_resp_get_func_config *resp = cmd.va;
		u32 desc_count = le32_to_cpu(resp->desc_count);
		struct be_nic_resource_desc *desc;

		desc = be_get_nic_desc(resp->func_param, desc_count,
				       sizeof(resp->func_param));
		if (!desc) {
			status = -EINVAL;
			goto err;
		}

		adapter->pf_number = desc->pf_num;
		adapter->max_pmac_cnt = le16_to_cpu(desc->unicast_mac_count);
		adapter->max_vlans = le16_to_cpu(desc->vlan_count);
		adapter->max_mcast_mac = le16_to_cpu(desc->mcast_mac_count);
		adapter->max_tx_queues = le16_to_cpu(desc->txq_count);
		adapter->max_rss_queues = le16_to_cpu(desc->rssq_count);
		adapter->max_rx_queues = le16_to_cpu(desc->rq_count);

		adapter->max_event_queues = le16_to_cpu(desc->eq_count);
		adapter->if_cap_flags = le32_to_cpu(desc->cap_flags);
	}
err:
	mutex_unlock(&adapter->mbox_lock);
	pci_free_consistent(adapter->pdev, cmd.size,
			    cmd.va, cmd.dma);
	return status;
}

 /* Uses sync mcc */
int be_cmd_get_profile_config(struct be_adapter *adapter, u32 *cap_flags,
			      u8 domain)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_get_profile_config *req;
	int status;
	struct be_dma_mem cmd;

	memset(&cmd, 0, sizeof(struct be_dma_mem));
	cmd.size = sizeof(struct be_cmd_resp_get_profile_config);
	cmd.va = pci_alloc_consistent(adapter->pdev, cmd.size,
				      &cmd.dma);
	if (!cmd.va) {
		dev_err(&adapter->pdev->dev, "Memory alloc failure\n");
		return -ENOMEM;
	}

	spin_lock_bh(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}

	req = cmd.va;

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
			       OPCODE_COMMON_GET_PROFILE_CONFIG,
			       cmd.size, wrb, &cmd);

	req->type = ACTIVE_PROFILE_TYPE;
	req->hdr.domain = domain;

	status = be_mcc_notify_wait(adapter);
	if (!status) {
		struct be_cmd_resp_get_profile_config *resp = cmd.va;
		u32 desc_count = le32_to_cpu(resp->desc_count);
		struct be_nic_resource_desc *desc;

		desc = be_get_nic_desc(resp->func_param, desc_count,
				       sizeof(resp->func_param));

		if (!desc) {
			status = -EINVAL;
			goto err;
		}
		*cap_flags = le32_to_cpu(desc->cap_flags);
	}
err:
	spin_unlock_bh(&adapter->mcc_lock);
	pci_free_consistent(adapter->pdev, cmd.size,
			    cmd.va, cmd.dma);
	return status;
}

/* Uses sync mcc */
int be_cmd_set_profile_config(struct be_adapter *adapter, u32 bps,
			      u8 domain)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_set_profile_config *req;
	int status;

	spin_lock_bh(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}

	req = embedded_payload(wrb);

	be_wrb_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
			       OPCODE_COMMON_SET_PROFILE_CONFIG, sizeof(*req),
			       wrb, NULL);

	req->hdr.domain = domain;
	req->desc_count = cpu_to_le32(1);

	req->nic_desc.desc_type = NIC_RESOURCE_DESC_TYPE_ID;
	req->nic_desc.desc_len = RESOURCE_DESC_SIZE;
	req->nic_desc.flags = (1 << QUN) | (1 << IMM) | (1 << NOSV);
	req->nic_desc.pf_num = adapter->pf_number;
	req->nic_desc.vf_num = domain;

	/* Mark fields invalid */
	req->nic_desc.unicast_mac_count = 0xFFFF;
	req->nic_desc.mcc_count = 0xFFFF;
	req->nic_desc.vlan_count = 0xFFFF;
	req->nic_desc.mcast_mac_count = 0xFFFF;
	req->nic_desc.txq_count = 0xFFFF;
	req->nic_desc.rq_count = 0xFFFF;
	req->nic_desc.rssq_count = 0xFFFF;
	req->nic_desc.lro_count = 0xFFFF;
	req->nic_desc.cq_count = 0xFFFF;
	req->nic_desc.toe_conn_count = 0xFFFF;
	req->nic_desc.eq_count = 0xFFFF;
	req->nic_desc.link_param = 0xFF;
	req->nic_desc.bw_min = 0xFFFFFFFF;
	req->nic_desc.acpi_params = 0xFF;
	req->nic_desc.wol_param = 0x0F;

	/* Change BW */
	req->nic_desc.bw_min = cpu_to_le32(bps);
	req->nic_desc.bw_max = cpu_to_le32(bps);
	status = be_mcc_notify_wait(adapter);
err:
	spin_unlock_bh(&adapter->mcc_lock);
	return status;
}

int be_cmd_get_if_id(struct be_adapter *adapter, struct be_vf_cfg *vf_cfg,
		     int vf_num)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_get_iface_list *req;
	struct be_cmd_resp_get_iface_list *resp;
	int status;

	spin_lock_bh(&adapter->mcc_lock);

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
	spin_unlock_bh(&adapter->mcc_lock);
	return status;
}

/* Uses sync mcc */
int be_cmd_enable_vf(struct be_adapter *adapter, u8 domain)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_enable_disable_vf *req;
	int status;

	if (!lancer_chip(adapter))
		return 0;

	spin_lock_bh(&adapter->mcc_lock);

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
	spin_unlock_bh(&adapter->mcc_lock);
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

int be_roce_mcc_cmd(void *netdev_handle, void *wrb_payload,
			int wrb_payload_size, u16 *cmd_status, u16 *ext_status)
{
	struct be_adapter *adapter = netdev_priv(netdev_handle);
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_hdr *hdr = (struct be_cmd_req_hdr *) wrb_payload;
	struct be_cmd_req_hdr *req;
	struct be_cmd_resp_hdr *resp;
	int status;

	spin_lock_bh(&adapter->mcc_lock);

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
	spin_unlock_bh(&adapter->mcc_lock);
	return status;
}
EXPORT_SYMBOL(be_roce_mcc_cmd);
