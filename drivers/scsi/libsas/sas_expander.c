// SPDX-License-Identifier: GPL-2.0
/*
 * Serial Attached SCSI (SAS) Expander discovery and configuration
 *
 * Copyright (C) 2005 Adaptec, Inc.  All rights reserved.
 * Copyright (C) 2005 Luben Tuikov <luben_tuikov@adaptec.com>
 *
 * This file is licensed under GPLv2.
 */

#include <linux/scatterlist.h>
#include <linux/blkdev.h>
#include <linux/slab.h>
#include <asm/unaligned.h>

#include "sas_internal.h"

#include <scsi/sas_ata.h>
#include <scsi/scsi_transport.h>
#include <scsi/scsi_transport_sas.h>
#include "../scsi_sas_internal.h"

static int sas_discover_expander(struct domain_device *dev);
static int sas_configure_routing(struct domain_device *dev, u8 *sas_addr);
static int sas_configure_phy(struct domain_device *dev, int phy_id,
			     u8 *sas_addr, int include);
static int sas_disable_routing(struct domain_device *dev,  u8 *sas_addr);

/* ---------- SMP task management ---------- */

static void smp_task_timedout(struct timer_list *t)
{
	struct sas_task_slow *slow = from_timer(slow, t, timer);
	struct sas_task *task = slow->task;
	unsigned long flags;

	spin_lock_irqsave(&task->task_state_lock, flags);
	if (!(task->task_state_flags & SAS_TASK_STATE_DONE)) {
		task->task_state_flags |= SAS_TASK_STATE_ABORTED;
		complete(&task->slow_task->completion);
	}
	spin_unlock_irqrestore(&task->task_state_lock, flags);
}

static void smp_task_done(struct sas_task *task)
{
	del_timer(&task->slow_task->timer);
	complete(&task->slow_task->completion);
}

/* Give it some long enough timeout. In seconds. */
#define SMP_TIMEOUT 10

static int smp_execute_task_sg(struct domain_device *dev,
		struct scatterlist *req, struct scatterlist *resp)
{
	int res, retry;
	struct sas_task *task = NULL;
	struct sas_internal *i =
		to_sas_internal(dev->port->ha->core.shost->transportt);

	mutex_lock(&dev->ex_dev.cmd_mutex);
	for (retry = 0; retry < 3; retry++) {
		if (test_bit(SAS_DEV_GONE, &dev->state)) {
			res = -ECOMM;
			break;
		}

		task = sas_alloc_slow_task(GFP_KERNEL);
		if (!task) {
			res = -ENOMEM;
			break;
		}
		task->dev = dev;
		task->task_proto = dev->tproto;
		task->smp_task.smp_req = *req;
		task->smp_task.smp_resp = *resp;

		task->task_done = smp_task_done;

		task->slow_task->timer.function = smp_task_timedout;
		task->slow_task->timer.expires = jiffies + SMP_TIMEOUT*HZ;
		add_timer(&task->slow_task->timer);

		res = i->dft->lldd_execute_task(task, GFP_KERNEL);

		if (res) {
			del_timer(&task->slow_task->timer);
			pr_notice("executing SMP task failed:%d\n", res);
			break;
		}

		wait_for_completion(&task->slow_task->completion);
		res = -ECOMM;
		if ((task->task_state_flags & SAS_TASK_STATE_ABORTED)) {
			pr_notice("smp task timed out or aborted\n");
			i->dft->lldd_abort_task(task);
			if (!(task->task_state_flags & SAS_TASK_STATE_DONE)) {
				pr_notice("SMP task aborted and not done\n");
				break;
			}
		}
		if (task->task_status.resp == SAS_TASK_COMPLETE &&
		    task->task_status.stat == SAM_STAT_GOOD) {
			res = 0;
			break;
		}
		if (task->task_status.resp == SAS_TASK_COMPLETE &&
		    task->task_status.stat == SAS_DATA_UNDERRUN) {
			/* no error, but return the number of bytes of
			 * underrun */
			res = task->task_status.residual;
			break;
		}
		if (task->task_status.resp == SAS_TASK_COMPLETE &&
		    task->task_status.stat == SAS_DATA_OVERRUN) {
			res = -EMSGSIZE;
			break;
		}
		if (task->task_status.resp == SAS_TASK_UNDELIVERED &&
		    task->task_status.stat == SAS_DEVICE_UNKNOWN)
			break;
		else {
			pr_notice("%s: task to dev %016llx response: 0x%x status 0x%x\n",
				  __func__,
				  SAS_ADDR(dev->sas_addr),
				  task->task_status.resp,
				  task->task_status.stat);
			sas_free_task(task);
			task = NULL;
		}
	}
	mutex_unlock(&dev->ex_dev.cmd_mutex);

	BUG_ON(retry == 3 && task != NULL);
	sas_free_task(task);
	return res;
}

static int smp_execute_task(struct domain_device *dev, void *req, int req_size,
			    void *resp, int resp_size)
{
	struct scatterlist req_sg;
	struct scatterlist resp_sg;

	sg_init_one(&req_sg, req, req_size);
	sg_init_one(&resp_sg, resp, resp_size);
	return smp_execute_task_sg(dev, &req_sg, &resp_sg);
}

/* ---------- Allocations ---------- */

static inline void *alloc_smp_req(int size)
{
	u8 *p = kzalloc(size, GFP_KERNEL);
	if (p)
		p[0] = SMP_REQUEST;
	return p;
}

static inline void *alloc_smp_resp(int size)
{
	return kzalloc(size, GFP_KERNEL);
}

static char sas_route_char(struct domain_device *dev, struct ex_phy *phy)
{
	switch (phy->routing_attr) {
	case TABLE_ROUTING:
		if (dev->ex_dev.t2t_supp)
			return 'U';
		else
			return 'T';
	case DIRECT_ROUTING:
		return 'D';
	case SUBTRACTIVE_ROUTING:
		return 'S';
	default:
		return '?';
	}
}

static enum sas_device_type to_dev_type(struct discover_resp *dr)
{
	/* This is detecting a failure to transmit initial dev to host
	 * FIS as described in section J.5 of sas-2 r16
	 */
	if (dr->attached_dev_type == SAS_PHY_UNUSED && dr->attached_sata_dev &&
	    dr->linkrate >= SAS_LINK_RATE_1_5_GBPS)
		return SAS_SATA_PENDING;
	else
		return dr->attached_dev_type;
}

static void sas_set_ex_phy(struct domain_device *dev, int phy_id, void *rsp)
{
	enum sas_device_type dev_type;
	enum sas_linkrate linkrate;
	u8 sas_addr[SAS_ADDR_SIZE];
	struct smp_resp *resp = rsp;
	struct discover_resp *dr = &resp->disc;
	struct sas_ha_struct *ha = dev->port->ha;
	struct expander_device *ex = &dev->ex_dev;
	struct ex_phy *phy = &ex->ex_phy[phy_id];
	struct sas_rphy *rphy = dev->rphy;
	bool new_phy = !phy->phy;
	char *type;

	if (new_phy) {
		if (WARN_ON_ONCE(test_bit(SAS_HA_ATA_EH_ACTIVE, &ha->state)))
			return;
		phy->phy = sas_phy_alloc(&rphy->dev, phy_id);

		/* FIXME: error_handling */
		BUG_ON(!phy->phy);
	}

	switch (resp->result) {
	case SMP_RESP_PHY_VACANT:
		phy->phy_state = PHY_VACANT;
		break;
	default:
		phy->phy_state = PHY_NOT_PRESENT;
		break;
	case SMP_RESP_FUNC_ACC:
		phy->phy_state = PHY_EMPTY; /* do not know yet */
		break;
	}

	/* check if anything important changed to squelch debug */
	dev_type = phy->attached_dev_type;
	linkrate  = phy->linkrate;
	memcpy(sas_addr, phy->attached_sas_addr, SAS_ADDR_SIZE);

	/* Handle vacant phy - rest of dr data is not valid so skip it */
	if (phy->phy_state == PHY_VACANT) {
		memset(phy->attached_sas_addr, 0, SAS_ADDR_SIZE);
		phy->attached_dev_type = SAS_PHY_UNUSED;
		if (!test_bit(SAS_HA_ATA_EH_ACTIVE, &ha->state)) {
			phy->phy_id = phy_id;
			goto skip;
		} else
			goto out;
	}

	phy->attached_dev_type = to_dev_type(dr);
	if (test_bit(SAS_HA_ATA_EH_ACTIVE, &ha->state))
		goto out;
	phy->phy_id = phy_id;
	phy->linkrate = dr->linkrate;
	phy->attached_sata_host = dr->attached_sata_host;
	phy->attached_sata_dev  = dr->attached_sata_dev;
	phy->attached_sata_ps   = dr->attached_sata_ps;
	phy->attached_iproto = dr->iproto << 1;
	phy->attached_tproto = dr->tproto << 1;
	/* help some expanders that fail to zero sas_address in the 'no
	 * device' case
	 */
	if (phy->attached_dev_type == SAS_PHY_UNUSED ||
	    phy->linkrate < SAS_LINK_RATE_1_5_GBPS)
		memset(phy->attached_sas_addr, 0, SAS_ADDR_SIZE);
	else
		memcpy(phy->attached_sas_addr, dr->attached_sas_addr, SAS_ADDR_SIZE);
	phy->attached_phy_id = dr->attached_phy_id;
	phy->phy_change_count = dr->change_count;
	phy->routing_attr = dr->routing_attr;
	phy->virtual = dr->virtual;
	phy->last_da_index = -1;

	phy->phy->identify.sas_address = SAS_ADDR(phy->attached_sas_addr);
	phy->phy->identify.device_type = dr->attached_dev_type;
	phy->phy->identify.initiator_port_protocols = phy->attached_iproto;
	phy->phy->identify.target_port_protocols = phy->attached_tproto;
	if (!phy->attached_tproto && dr->attached_sata_dev)
		phy->phy->identify.target_port_protocols = SAS_PROTOCOL_SATA;
	phy->phy->identify.phy_identifier = phy_id;
	phy->phy->minimum_linkrate_hw = dr->hmin_linkrate;
	phy->phy->maximum_linkrate_hw = dr->hmax_linkrate;
	phy->phy->minimum_linkrate = dr->pmin_linkrate;
	phy->phy->maximum_linkrate = dr->pmax_linkrate;
	phy->phy->negotiated_linkrate = phy->linkrate;
	phy->phy->enabled = (phy->linkrate != SAS_PHY_DISABLED);

 skip:
	if (new_phy)
		if (sas_phy_add(phy->phy)) {
			sas_phy_free(phy->phy);
			return;
		}

 out:
	switch (phy->attached_dev_type) {
	case SAS_SATA_PENDING:
		type = "stp pending";
		break;
	case SAS_PHY_UNUSED:
		type = "no device";
		break;
	case SAS_END_DEVICE:
		if (phy->attached_iproto) {
			if (phy->attached_tproto)
				type = "host+target";
			else
				type = "host";
		} else {
			if (dr->attached_sata_dev)
				type = "stp";
			else
				type = "ssp";
		}
		break;
	case SAS_EDGE_EXPANDER_DEVICE:
	case SAS_FANOUT_EXPANDER_DEVICE:
		type = "smp";
		break;
	default:
		type = "unknown";
	}

	/* this routine is polled by libata error recovery so filter
	 * unimportant messages
	 */
	if (new_phy || phy->attached_dev_type != dev_type ||
	    phy->linkrate != linkrate ||
	    SAS_ADDR(phy->attached_sas_addr) != SAS_ADDR(sas_addr))
		/* pass */;
	else
		return;

	/* if the attached device type changed and ata_eh is active,
	 * make sure we run revalidation when eh completes (see:
	 * sas_enable_revalidation)
	 */
	if (test_bit(SAS_HA_ATA_EH_ACTIVE, &ha->state))
		set_bit(DISCE_REVALIDATE_DOMAIN, &dev->port->disc.pending);

	pr_debug("%sex %016llx phy%02d:%c:%X attached: %016llx (%s)\n",
		 test_bit(SAS_HA_ATA_EH_ACTIVE, &ha->state) ? "ata: " : "",
		 SAS_ADDR(dev->sas_addr), phy->phy_id,
		 sas_route_char(dev, phy), phy->linkrate,
		 SAS_ADDR(phy->attached_sas_addr), type);
}

/* check if we have an existing attached ata device on this expander phy */
struct domain_device *sas_ex_to_ata(struct domain_device *ex_dev, int phy_id)
{
	struct ex_phy *ex_phy = &ex_dev->ex_dev.ex_phy[phy_id];
	struct domain_device *dev;
	struct sas_rphy *rphy;

	if (!ex_phy->port)
		return NULL;

	rphy = ex_phy->port->rphy;
	if (!rphy)
		return NULL;

	dev = sas_find_dev_by_rphy(rphy);

	if (dev && dev_is_sata(dev))
		return dev;

	return NULL;
}

#define DISCOVER_REQ_SIZE  16
#define DISCOVER_RESP_SIZE 56

static int sas_ex_phy_discover_helper(struct domain_device *dev, u8 *disc_req,
				      u8 *disc_resp, int single)
{
	struct discover_resp *dr;
	int res;

	disc_req[9] = single;

	res = smp_execute_task(dev, disc_req, DISCOVER_REQ_SIZE,
			       disc_resp, DISCOVER_RESP_SIZE);
	if (res)
		return res;
	dr = &((struct smp_resp *)disc_resp)->disc;
	if (memcmp(dev->sas_addr, dr->attached_sas_addr, SAS_ADDR_SIZE) == 0) {
		pr_notice("Found loopback topology, just ignore it!\n");
		return 0;
	}
	sas_set_ex_phy(dev, single, disc_resp);
	return 0;
}

int sas_ex_phy_discover(struct domain_device *dev, int single)
{
	struct expander_device *ex = &dev->ex_dev;
	int  res = 0;
	u8   *disc_req;
	u8   *disc_resp;

	disc_req = alloc_smp_req(DISCOVER_REQ_SIZE);
	if (!disc_req)
		return -ENOMEM;

	disc_resp = alloc_smp_resp(DISCOVER_RESP_SIZE);
	if (!disc_resp) {
		kfree(disc_req);
		return -ENOMEM;
	}

	disc_req[1] = SMP_DISCOVER;

	if (0 <= single && single < ex->num_phys) {
		res = sas_ex_phy_discover_helper(dev, disc_req, disc_resp, single);
	} else {
		int i;

		for (i = 0; i < ex->num_phys; i++) {
			res = sas_ex_phy_discover_helper(dev, disc_req,
							 disc_resp, i);
			if (res)
				goto out_err;
		}
	}
out_err:
	kfree(disc_resp);
	kfree(disc_req);
	return res;
}

static int sas_expander_discover(struct domain_device *dev)
{
	struct expander_device *ex = &dev->ex_dev;
	int res = -ENOMEM;

	ex->ex_phy = kcalloc(ex->num_phys, sizeof(*ex->ex_phy), GFP_KERNEL);
	if (!ex->ex_phy)
		return -ENOMEM;

	res = sas_ex_phy_discover(dev, -1);
	if (res)
		goto out_err;

	return 0;
 out_err:
	kfree(ex->ex_phy);
	ex->ex_phy = NULL;
	return res;
}

#define MAX_EXPANDER_PHYS 128

static void ex_assign_report_general(struct domain_device *dev,
					    struct smp_resp *resp)
{
	struct report_general_resp *rg = &resp->rg;

	dev->ex_dev.ex_change_count = be16_to_cpu(rg->change_count);
	dev->ex_dev.max_route_indexes = be16_to_cpu(rg->route_indexes);
	dev->ex_dev.num_phys = min(rg->num_phys, (u8)MAX_EXPANDER_PHYS);
	dev->ex_dev.t2t_supp = rg->t2t_supp;
	dev->ex_dev.conf_route_table = rg->conf_route_table;
	dev->ex_dev.configuring = rg->configuring;
	memcpy(dev->ex_dev.enclosure_logical_id, rg->enclosure_logical_id, 8);
}

#define RG_REQ_SIZE   8
#define RG_RESP_SIZE 32

static int sas_ex_general(struct domain_device *dev)
{
	u8 *rg_req;
	struct smp_resp *rg_resp;
	int res;
	int i;

	rg_req = alloc_smp_req(RG_REQ_SIZE);
	if (!rg_req)
		return -ENOMEM;

	rg_resp = alloc_smp_resp(RG_RESP_SIZE);
	if (!rg_resp) {
		kfree(rg_req);
		return -ENOMEM;
	}

	rg_req[1] = SMP_REPORT_GENERAL;

	for (i = 0; i < 5; i++) {
		res = smp_execute_task(dev, rg_req, RG_REQ_SIZE, rg_resp,
				       RG_RESP_SIZE);

		if (res) {
			pr_notice("RG to ex %016llx failed:0x%x\n",
				  SAS_ADDR(dev->sas_addr), res);
			goto out;
		} else if (rg_resp->result != SMP_RESP_FUNC_ACC) {
			pr_debug("RG:ex %016llx returned SMP result:0x%x\n",
				 SAS_ADDR(dev->sas_addr), rg_resp->result);
			res = rg_resp->result;
			goto out;
		}

		ex_assign_report_general(dev, rg_resp);

		if (dev->ex_dev.configuring) {
			pr_debug("RG: ex %llx self-configuring...\n",
				 SAS_ADDR(dev->sas_addr));
			schedule_timeout_interruptible(5*HZ);
		} else
			break;
	}
out:
	kfree(rg_req);
	kfree(rg_resp);
	return res;
}

static void ex_assign_manuf_info(struct domain_device *dev, void
					*_mi_resp)
{
	u8 *mi_resp = _mi_resp;
	struct sas_rphy *rphy = dev->rphy;
	struct sas_expander_device *edev = rphy_to_expander_device(rphy);

	memcpy(edev->vendor_id, mi_resp + 12, SAS_EXPANDER_VENDOR_ID_LEN);
	memcpy(edev->product_id, mi_resp + 20, SAS_EXPANDER_PRODUCT_ID_LEN);
	memcpy(edev->product_rev, mi_resp + 36,
	       SAS_EXPANDER_PRODUCT_REV_LEN);

	if (mi_resp[8] & 1) {
		memcpy(edev->component_vendor_id, mi_resp + 40,
		       SAS_EXPANDER_COMPONENT_VENDOR_ID_LEN);
		edev->component_id = mi_resp[48] << 8 | mi_resp[49];
		edev->component_revision_id = mi_resp[50];
	}
}

#define MI_REQ_SIZE   8
#define MI_RESP_SIZE 64

static int sas_ex_manuf_info(struct domain_device *dev)
{
	u8 *mi_req;
	u8 *mi_resp;
	int res;

	mi_req = alloc_smp_req(MI_REQ_SIZE);
	if (!mi_req)
		return -ENOMEM;

	mi_resp = alloc_smp_resp(MI_RESP_SIZE);
	if (!mi_resp) {
		kfree(mi_req);
		return -ENOMEM;
	}

	mi_req[1] = SMP_REPORT_MANUF_INFO;

	res = smp_execute_task(dev, mi_req, MI_REQ_SIZE, mi_resp,MI_RESP_SIZE);
	if (res) {
		pr_notice("MI: ex %016llx failed:0x%x\n",
			  SAS_ADDR(dev->sas_addr), res);
		goto out;
	} else if (mi_resp[2] != SMP_RESP_FUNC_ACC) {
		pr_debug("MI ex %016llx returned SMP result:0x%x\n",
			 SAS_ADDR(dev->sas_addr), mi_resp[2]);
		goto out;
	}

	ex_assign_manuf_info(dev, mi_resp);
out:
	kfree(mi_req);
	kfree(mi_resp);
	return res;
}

#define PC_REQ_SIZE  44
#define PC_RESP_SIZE 8

int sas_smp_phy_control(struct domain_device *dev, int phy_id,
			enum phy_func phy_func,
			struct sas_phy_linkrates *rates)
{
	u8 *pc_req;
	u8 *pc_resp;
	int res;

	pc_req = alloc_smp_req(PC_REQ_SIZE);
	if (!pc_req)
		return -ENOMEM;

	pc_resp = alloc_smp_resp(PC_RESP_SIZE);
	if (!pc_resp) {
		kfree(pc_req);
		return -ENOMEM;
	}

	pc_req[1] = SMP_PHY_CONTROL;
	pc_req[9] = phy_id;
	pc_req[10]= phy_func;
	if (rates) {
		pc_req[32] = rates->minimum_linkrate << 4;
		pc_req[33] = rates->maximum_linkrate << 4;
	}

	res = smp_execute_task(dev, pc_req, PC_REQ_SIZE, pc_resp,PC_RESP_SIZE);
	if (res) {
		pr_err("ex %016llx phy%02d PHY control failed: %d\n",
		       SAS_ADDR(dev->sas_addr), phy_id, res);
	} else if (pc_resp[2] != SMP_RESP_FUNC_ACC) {
		pr_err("ex %016llx phy%02d PHY control failed: function result 0x%x\n",
		       SAS_ADDR(dev->sas_addr), phy_id, pc_resp[2]);
		res = pc_resp[2];
	}
	kfree(pc_resp);
	kfree(pc_req);
	return res;
}

static void sas_ex_disable_phy(struct domain_device *dev, int phy_id)
{
	struct expander_device *ex = &dev->ex_dev;
	struct ex_phy *phy = &ex->ex_phy[phy_id];

	sas_smp_phy_control(dev, phy_id, PHY_FUNC_DISABLE, NULL);
	phy->linkrate = SAS_PHY_DISABLED;
}

static void sas_ex_disable_port(struct domain_device *dev, u8 *sas_addr)
{
	struct expander_device *ex = &dev->ex_dev;
	int i;

	for (i = 0; i < ex->num_phys; i++) {
		struct ex_phy *phy = &ex->ex_phy[i];

		if (phy->phy_state == PHY_VACANT ||
		    phy->phy_state == PHY_NOT_PRESENT)
			continue;

		if (SAS_ADDR(phy->attached_sas_addr) == SAS_ADDR(sas_addr))
			sas_ex_disable_phy(dev, i);
	}
}

static int sas_dev_present_in_domain(struct asd_sas_port *port,
					    u8 *sas_addr)
{
	struct domain_device *dev;

	if (SAS_ADDR(port->sas_addr) == SAS_ADDR(sas_addr))
		return 1;
	list_for_each_entry(dev, &port->dev_list, dev_list_node) {
		if (SAS_ADDR(dev->sas_addr) == SAS_ADDR(sas_addr))
			return 1;
	}
	return 0;
}

#define RPEL_REQ_SIZE	16
#define RPEL_RESP_SIZE	32
int sas_smp_get_phy_events(struct sas_phy *phy)
{
	int res;
	u8 *req;
	u8 *resp;
	struct sas_rphy *rphy = dev_to_rphy(phy->dev.parent);
	struct domain_device *dev = sas_find_dev_by_rphy(rphy);

	req = alloc_smp_req(RPEL_REQ_SIZE);
	if (!req)
		return -ENOMEM;

	resp = alloc_smp_resp(RPEL_RESP_SIZE);
	if (!resp) {
		kfree(req);
		return -ENOMEM;
	}

	req[1] = SMP_REPORT_PHY_ERR_LOG;
	req[9] = phy->number;

	res = smp_execute_task(dev, req, RPEL_REQ_SIZE,
			            resp, RPEL_RESP_SIZE);

	if (res)
		goto out;

	phy->invalid_dword_count = get_unaligned_be32(&resp[12]);
	phy->running_disparity_error_count = get_unaligned_be32(&resp[16]);
	phy->loss_of_dword_sync_count = get_unaligned_be32(&resp[20]);
	phy->phy_reset_problem_count = get_unaligned_be32(&resp[24]);

 out:
	kfree(req);
	kfree(resp);
	return res;

}

#ifdef CONFIG_SCSI_SAS_ATA

#define RPS_REQ_SIZE  16
#define RPS_RESP_SIZE 60

int sas_get_report_phy_sata(struct domain_device *dev, int phy_id,
			    struct smp_resp *rps_resp)
{
	int res;
	u8 *rps_req = alloc_smp_req(RPS_REQ_SIZE);
	u8 *resp = (u8 *)rps_resp;

	if (!rps_req)
		return -ENOMEM;

	rps_req[1] = SMP_REPORT_PHY_SATA;
	rps_req[9] = phy_id;

	res = smp_execute_task(dev, rps_req, RPS_REQ_SIZE,
			            rps_resp, RPS_RESP_SIZE);

	/* 0x34 is the FIS type for the D2H fis.  There's a potential
	 * standards cockup here.  sas-2 explicitly specifies the FIS
	 * should be encoded so that FIS type is in resp[24].
	 * However, some expanders endian reverse this.  Undo the
	 * reversal here */
	if (!res && resp[27] == 0x34 && resp[24] != 0x34) {
		int i;

		for (i = 0; i < 5; i++) {
			int j = 24 + (i*4);
			u8 a, b;
			a = resp[j + 0];
			b = resp[j + 1];
			resp[j + 0] = resp[j + 3];
			resp[j + 1] = resp[j + 2];
			resp[j + 2] = b;
			resp[j + 3] = a;
		}
	}

	kfree(rps_req);
	return res;
}
#endif

static void sas_ex_get_linkrate(struct domain_device *parent,
				       struct domain_device *child,
				       struct ex_phy *parent_phy)
{
	struct expander_device *parent_ex = &parent->ex_dev;
	struct sas_port *port;
	int i;

	child->pathways = 0;

	port = parent_phy->port;

	for (i = 0; i < parent_ex->num_phys; i++) {
		struct ex_phy *phy = &parent_ex->ex_phy[i];

		if (phy->phy_state == PHY_VACANT ||
		    phy->phy_state == PHY_NOT_PRESENT)
			continue;

		if (SAS_ADDR(phy->attached_sas_addr) ==
		    SAS_ADDR(child->sas_addr)) {

			child->min_linkrate = min(parent->min_linkrate,
						  phy->linkrate);
			child->max_linkrate = max(parent->max_linkrate,
						  phy->linkrate);
			child->pathways++;
			sas_port_add_phy(port, phy->phy);
		}
	}
	child->linkrate = min(parent_phy->linkrate, child->max_linkrate);
	child->pathways = min(child->pathways, parent->pathways);
}

static struct domain_device *sas_ex_discover_end_dev(
	struct domain_device *parent, int phy_id)
{
	struct expander_device *parent_ex = &parent->ex_dev;
	struct ex_phy *phy = &parent_ex->ex_phy[phy_id];
	struct domain_device *child = NULL;
	struct sas_rphy *rphy;
	int res;

	if (phy->attached_sata_host || phy->attached_sata_ps)
		return NULL;

	child = sas_alloc_device();
	if (!child)
		return NULL;

	kref_get(&parent->kref);
	child->parent = parent;
	child->port   = parent->port;
	child->iproto = phy->attached_iproto;
	memcpy(child->sas_addr, phy->attached_sas_addr, SAS_ADDR_SIZE);
	sas_hash_addr(child->hashed_sas_addr, child->sas_addr);
	if (!phy->port) {
		phy->port = sas_port_alloc(&parent->rphy->dev, phy_id);
		if (unlikely(!phy->port))
			goto out_err;
		if (unlikely(sas_port_add(phy->port) != 0)) {
			sas_port_free(phy->port);
			goto out_err;
		}
	}
	sas_ex_get_linkrate(parent, child, phy);
	sas_device_set_phy(child, phy->port);

#ifdef CONFIG_SCSI_SAS_ATA
	if ((phy->attached_tproto & SAS_PROTOCOL_STP) || phy->attached_sata_dev) {
		if (child->linkrate > parent->min_linkrate) {
			struct sas_phy *cphy = child->phy;
			enum sas_linkrate min_prate = cphy->minimum_linkrate,
				parent_min_lrate = parent->min_linkrate,
				min_linkrate = (min_prate > parent_min_lrate) ?
					       parent_min_lrate : 0;
			struct sas_phy_linkrates rates = {
				.maximum_linkrate = parent->min_linkrate,
				.minimum_linkrate = min_linkrate,
			};
			int ret;

			pr_notice("ex %016llx phy%02d SATA device linkrate > min pathway connection rate, attempting to lower device linkrate\n",
				   SAS_ADDR(child->sas_addr), phy_id);
			ret = sas_smp_phy_control(parent, phy_id,
						  PHY_FUNC_LINK_RESET, &rates);
			if (ret) {
				pr_err("ex %016llx phy%02d SATA device could not set linkrate (%d)\n",
				       SAS_ADDR(child->sas_addr), phy_id, ret);
				goto out_free;
			}
			pr_notice("ex %016llx phy%02d SATA device set linkrate successfully\n",
				  SAS_ADDR(child->sas_addr), phy_id);
			child->linkrate = child->min_linkrate;
		}
		res = sas_get_ata_info(child, phy);
		if (res)
			goto out_free;

		sas_init_dev(child);
		res = sas_ata_init(child);
		if (res)
			goto out_free;
		rphy = sas_end_device_alloc(phy->port);
		if (!rphy)
			goto out_free;
		rphy->identify.phy_identifier = phy_id;

		child->rphy = rphy;
		get_device(&rphy->dev);

		list_add_tail(&child->disco_list_node, &parent->port->disco_list);

		res = sas_discover_sata(child);
		if (res) {
			pr_notice("sas_discover_sata() for device %16llx at %016llx:%02d returned 0x%x\n",
				  SAS_ADDR(child->sas_addr),
				  SAS_ADDR(parent->sas_addr), phy_id, res);
			goto out_list_del;
		}
	} else
#endif
	  if (phy->attached_tproto & SAS_PROTOCOL_SSP) {
		child->dev_type = SAS_END_DEVICE;
		rphy = sas_end_device_alloc(phy->port);
		/* FIXME: error handling */
		if (unlikely(!rphy))
			goto out_free;
		child->tproto = phy->attached_tproto;
		sas_init_dev(child);

		child->rphy = rphy;
		get_device(&rphy->dev);
		rphy->identify.phy_identifier = phy_id;
		sas_fill_in_rphy(child, rphy);

		list_add_tail(&child->disco_list_node, &parent->port->disco_list);

		res = sas_discover_end_dev(child);
		if (res) {
			pr_notice("sas_discover_end_dev() for device %16llx at %016llx:%02d returned 0x%x\n",
				  SAS_ADDR(child->sas_addr),
				  SAS_ADDR(parent->sas_addr), phy_id, res);
			goto out_list_del;
		}
	} else {
		pr_notice("target proto 0x%x at %016llx:0x%x not handled\n",
			  phy->attached_tproto, SAS_ADDR(parent->sas_addr),
			  phy_id);
		goto out_free;
	}

	list_add_tail(&child->siblings, &parent_ex->children);
	return child;

 out_list_del:
	sas_rphy_free(child->rphy);
	list_del(&child->disco_list_node);
	spin_lock_irq(&parent->port->dev_list_lock);
	list_del(&child->dev_list_node);
	spin_unlock_irq(&parent->port->dev_list_lock);
 out_free:
	sas_port_delete(phy->port);
 out_err:
	phy->port = NULL;
	sas_put_device(child);
	return NULL;
}

/* See if this phy is part of a wide port */
static bool sas_ex_join_wide_port(struct domain_device *parent, int phy_id)
{
	struct ex_phy *phy = &parent->ex_dev.ex_phy[phy_id];
	int i;

	for (i = 0; i < parent->ex_dev.num_phys; i++) {
		struct ex_phy *ephy = &parent->ex_dev.ex_phy[i];

		if (ephy == phy)
			continue;

		if (!memcmp(phy->attached_sas_addr, ephy->attached_sas_addr,
			    SAS_ADDR_SIZE) && ephy->port) {
			sas_port_add_phy(ephy->port, phy->phy);
			phy->port = ephy->port;
			phy->phy_state = PHY_DEVICE_DISCOVERED;
			return true;
		}
	}

	return false;
}

static struct domain_device *sas_ex_discover_expander(
	struct domain_device *parent, int phy_id)
{
	struct sas_expander_device *parent_ex = rphy_to_expander_device(parent->rphy);
	struct ex_phy *phy = &parent->ex_dev.ex_phy[phy_id];
	struct domain_device *child = NULL;
	struct sas_rphy *rphy;
	struct sas_expander_device *edev;
	struct asd_sas_port *port;
	int res;

	if (phy->routing_attr == DIRECT_ROUTING) {
		pr_warn("ex %016llx:%02d:D <--> ex %016llx:0x%x is not allowed\n",
			SAS_ADDR(parent->sas_addr), phy_id,
			SAS_ADDR(phy->attached_sas_addr),
			phy->attached_phy_id);
		return NULL;
	}
	child = sas_alloc_device();
	if (!child)
		return NULL;

	phy->port = sas_port_alloc(&parent->rphy->dev, phy_id);
	/* FIXME: better error handling */
	BUG_ON(sas_port_add(phy->port) != 0);


	switch (phy->attached_dev_type) {
	case SAS_EDGE_EXPANDER_DEVICE:
		rphy = sas_expander_alloc(phy->port,
					  SAS_EDGE_EXPANDER_DEVICE);
		break;
	case SAS_FANOUT_EXPANDER_DEVICE:
		rphy = sas_expander_alloc(phy->port,
					  SAS_FANOUT_EXPANDER_DEVICE);
		break;
	default:
		rphy = NULL;	/* shut gcc up */
		BUG();
	}
	port = parent->port;
	child->rphy = rphy;
	get_device(&rphy->dev);
	edev = rphy_to_expander_device(rphy);
	child->dev_type = phy->attached_dev_type;
	kref_get(&parent->kref);
	child->parent = parent;
	child->port = port;
	child->iproto = phy->attached_iproto;
	child->tproto = phy->attached_tproto;
	memcpy(child->sas_addr, phy->attached_sas_addr, SAS_ADDR_SIZE);
	sas_hash_addr(child->hashed_sas_addr, child->sas_addr);
	sas_ex_get_linkrate(parent, child, phy);
	edev->level = parent_ex->level + 1;
	parent->port->disc.max_level = max(parent->port->disc.max_level,
					   edev->level);
	sas_init_dev(child);
	sas_fill_in_rphy(child, rphy);
	sas_rphy_add(rphy);

	spin_lock_irq(&parent->port->dev_list_lock);
	list_add_tail(&child->dev_list_node, &parent->port->dev_list);
	spin_unlock_irq(&parent->port->dev_list_lock);

	res = sas_discover_expander(child);
	if (res) {
		sas_rphy_delete(rphy);
		spin_lock_irq(&parent->port->dev_list_lock);
		list_del(&child->dev_list_node);
		spin_unlock_irq(&parent->port->dev_list_lock);
		sas_put_device(child);
		sas_port_delete(phy->port);
		phy->port = NULL;
		return NULL;
	}
	list_add_tail(&child->siblings, &parent->ex_dev.children);
	return child;
}

static int sas_ex_discover_dev(struct domain_device *dev, int phy_id)
{
	struct expander_device *ex = &dev->ex_dev;
	struct ex_phy *ex_phy = &ex->ex_phy[phy_id];
	struct domain_device *child = NULL;
	int res = 0;

	/* Phy state */
	if (ex_phy->linkrate == SAS_SATA_SPINUP_HOLD) {
		if (!sas_smp_phy_control(dev, phy_id, PHY_FUNC_LINK_RESET, NULL))
			res = sas_ex_phy_discover(dev, phy_id);
		if (res)
			return res;
	}

	/* Parent and domain coherency */
	if (!dev->parent && (SAS_ADDR(ex_phy->attached_sas_addr) ==
			     SAS_ADDR(dev->port->sas_addr))) {
		sas_add_parent_port(dev, phy_id);
		return 0;
	}
	if (dev->parent && (SAS_ADDR(ex_phy->attached_sas_addr) ==
			    SAS_ADDR(dev->parent->sas_addr))) {
		sas_add_parent_port(dev, phy_id);
		if (ex_phy->routing_attr == TABLE_ROUTING)
			sas_configure_phy(dev, phy_id, dev->port->sas_addr, 1);
		return 0;
	}

	if (sas_dev_present_in_domain(dev->port, ex_phy->attached_sas_addr))
		sas_ex_disable_port(dev, ex_phy->attached_sas_addr);

	if (ex_phy->attached_dev_type == SAS_PHY_UNUSED) {
		if (ex_phy->routing_attr == DIRECT_ROUTING) {
			memset(ex_phy->attached_sas_addr, 0, SAS_ADDR_SIZE);
			sas_configure_routing(dev, ex_phy->attached_sas_addr);
		}
		return 0;
	} else if (ex_phy->linkrate == SAS_LINK_RATE_UNKNOWN)
		return 0;

	if (ex_phy->attached_dev_type != SAS_END_DEVICE &&
	    ex_phy->attached_dev_type != SAS_FANOUT_EXPANDER_DEVICE &&
	    ex_phy->attached_dev_type != SAS_EDGE_EXPANDER_DEVICE &&
	    ex_phy->attached_dev_type != SAS_SATA_PENDING) {
		pr_warn("unknown device type(0x%x) attached to ex %016llx phy%02d\n",
			ex_phy->attached_dev_type,
			SAS_ADDR(dev->sas_addr),
			phy_id);
		return 0;
	}

	res = sas_configure_routing(dev, ex_phy->attached_sas_addr);
	if (res) {
		pr_notice("configure routing for dev %016llx reported 0x%x. Forgotten\n",
			  SAS_ADDR(ex_phy->attached_sas_addr), res);
		sas_disable_routing(dev, ex_phy->attached_sas_addr);
		return res;
	}

	if (sas_ex_join_wide_port(dev, phy_id)) {
		pr_debug("Attaching ex phy%02d to wide port %016llx\n",
			 phy_id, SAS_ADDR(ex_phy->attached_sas_addr));
		return res;
	}

	switch (ex_phy->attached_dev_type) {
	case SAS_END_DEVICE:
	case SAS_SATA_PENDING:
		child = sas_ex_discover_end_dev(dev, phy_id);
		break;
	case SAS_FANOUT_EXPANDER_DEVICE:
		if (SAS_ADDR(dev->port->disc.fanout_sas_addr)) {
			pr_debug("second fanout expander %016llx phy%02d attached to ex %016llx phy%02d\n",
				 SAS_ADDR(ex_phy->attached_sas_addr),
				 ex_phy->attached_phy_id,
				 SAS_ADDR(dev->sas_addr),
				 phy_id);
			sas_ex_disable_phy(dev, phy_id);
			return res;
		} else
			memcpy(dev->port->disc.fanout_sas_addr,
			       ex_phy->attached_sas_addr, SAS_ADDR_SIZE);
		/* fallthrough */
	case SAS_EDGE_EXPANDER_DEVICE:
		child = sas_ex_discover_expander(dev, phy_id);
		break;
	default:
		break;
	}

	if (!child)
		pr_notice("ex %016llx phy%02d failed to discover\n",
			  SAS_ADDR(dev->sas_addr), phy_id);
	return res;
}

static int sas_find_sub_addr(struct domain_device *dev, u8 *sub_addr)
{
	struct expander_device *ex = &dev->ex_dev;
	int i;

	for (i = 0; i < ex->num_phys; i++) {
		struct ex_phy *phy = &ex->ex_phy[i];

		if (phy->phy_state == PHY_VACANT ||
		    phy->phy_state == PHY_NOT_PRESENT)
			continue;

		if (dev_is_expander(phy->attached_dev_type) &&
		    phy->routing_attr == SUBTRACTIVE_ROUTING) {

			memcpy(sub_addr, phy->attached_sas_addr, SAS_ADDR_SIZE);

			return 1;
		}
	}
	return 0;
}

static int sas_check_level_subtractive_boundary(struct domain_device *dev)
{
	struct expander_device *ex = &dev->ex_dev;
	struct domain_device *child;
	u8 sub_addr[SAS_ADDR_SIZE] = {0, };

	list_for_each_entry(child, &ex->children, siblings) {
		if (!dev_is_expander(child->dev_type))
			continue;
		if (sub_addr[0] == 0) {
			sas_find_sub_addr(child, sub_addr);
			continue;
		} else {
			u8 s2[SAS_ADDR_SIZE];

			if (sas_find_sub_addr(child, s2) &&
			    (SAS_ADDR(sub_addr) != SAS_ADDR(s2))) {

				pr_notice("ex %016llx->%016llx-?->%016llx diverges from subtractive boundary %016llx\n",
					  SAS_ADDR(dev->sas_addr),
					  SAS_ADDR(child->sas_addr),
					  SAS_ADDR(s2),
					  SAS_ADDR(sub_addr));

				sas_ex_disable_port(child, s2);
			}
		}
	}
	return 0;
}
/**
 * sas_ex_discover_devices - discover devices attached to this expander
 * @dev: pointer to the expander domain device
 * @single: if you want to do a single phy, else set to -1;
 *
 * Configure this expander for use with its devices and register the
 * devices of this expander.
 */
static int sas_ex_discover_devices(struct domain_device *dev, int single)
{
	struct expander_device *ex = &dev->ex_dev;
	int i = 0, end = ex->num_phys;
	int res = 0;

	if (0 <= single && single < end) {
		i = single;
		end = i+1;
	}

	for ( ; i < end; i++) {
		struct ex_phy *ex_phy = &ex->ex_phy[i];

		if (ex_phy->phy_state == PHY_VACANT ||
		    ex_phy->phy_state == PHY_NOT_PRESENT ||
		    ex_phy->phy_state == PHY_DEVICE_DISCOVERED)
			continue;

		switch (ex_phy->linkrate) {
		case SAS_PHY_DISABLED:
		case SAS_PHY_RESET_PROBLEM:
		case SAS_SATA_PORT_SELECTOR:
			continue;
		default:
			res = sas_ex_discover_dev(dev, i);
			if (res)
				break;
			continue;
		}
	}

	if (!res)
		sas_check_level_subtractive_boundary(dev);

	return res;
}

static int sas_check_ex_subtractive_boundary(struct domain_device *dev)
{
	struct expander_device *ex = &dev->ex_dev;
	int i;
	u8  *sub_sas_addr = NULL;

	if (dev->dev_type != SAS_EDGE_EXPANDER_DEVICE)
		return 0;

	for (i = 0; i < ex->num_phys; i++) {
		struct ex_phy *phy = &ex->ex_phy[i];

		if (phy->phy_state == PHY_VACANT ||
		    phy->phy_state == PHY_NOT_PRESENT)
			continue;

		if (dev_is_expander(phy->attached_dev_type) &&
		    phy->routing_attr == SUBTRACTIVE_ROUTING) {

			if (!sub_sas_addr)
				sub_sas_addr = &phy->attached_sas_addr[0];
			else if (SAS_ADDR(sub_sas_addr) !=
				 SAS_ADDR(phy->attached_sas_addr)) {

				pr_notice("ex %016llx phy%02d diverges(%016llx) on subtractive boundary(%016llx). Disabled\n",
					  SAS_ADDR(dev->sas_addr), i,
					  SAS_ADDR(phy->attached_sas_addr),
					  SAS_ADDR(sub_sas_addr));
				sas_ex_disable_phy(dev, i);
			}
		}
	}
	return 0;
}

static void sas_print_parent_topology_bug(struct domain_device *child,
						 struct ex_phy *parent_phy,
						 struct ex_phy *child_phy)
{
	static const char *ex_type[] = {
		[SAS_EDGE_EXPANDER_DEVICE] = "edge",
		[SAS_FANOUT_EXPANDER_DEVICE] = "fanout",
	};
	struct domain_device *parent = child->parent;

	pr_notice("%s ex %016llx phy%02d <--> %s ex %016llx phy%02d has %c:%c routing link!\n",
		  ex_type[parent->dev_type],
		  SAS_ADDR(parent->sas_addr),
		  parent_phy->phy_id,

		  ex_type[child->dev_type],
		  SAS_ADDR(child->sas_addr),
		  child_phy->phy_id,

		  sas_route_char(parent, parent_phy),
		  sas_route_char(child, child_phy));
}

static int sas_check_eeds(struct domain_device *child,
				 struct ex_phy *parent_phy,
				 struct ex_phy *child_phy)
{
	int res = 0;
	struct domain_device *parent = child->parent;

	if (SAS_ADDR(parent->port->disc.fanout_sas_addr) != 0) {
		res = -ENODEV;
		pr_warn("edge ex %016llx phy S:%02d <--> edge ex %016llx phy S:%02d, while there is a fanout ex %016llx\n",
			SAS_ADDR(parent->sas_addr),
			parent_phy->phy_id,
			SAS_ADDR(child->sas_addr),
			child_phy->phy_id,
			SAS_ADDR(parent->port->disc.fanout_sas_addr));
	} else if (SAS_ADDR(parent->port->disc.eeds_a) == 0) {
		memcpy(parent->port->disc.eeds_a, parent->sas_addr,
		       SAS_ADDR_SIZE);
		memcpy(parent->port->disc.eeds_b, child->sas_addr,
		       SAS_ADDR_SIZE);
	} else if (((SAS_ADDR(parent->port->disc.eeds_a) ==
		    SAS_ADDR(parent->sas_addr)) ||
		   (SAS_ADDR(parent->port->disc.eeds_a) ==
		    SAS_ADDR(child->sas_addr)))
		   &&
		   ((SAS_ADDR(parent->port->disc.eeds_b) ==
		     SAS_ADDR(parent->sas_addr)) ||
		    (SAS_ADDR(parent->port->disc.eeds_b) ==
		     SAS_ADDR(child->sas_addr))))
		;
	else {
		res = -ENODEV;
		pr_warn("edge ex %016llx phy%02d <--> edge ex %016llx phy%02d link forms a third EEDS!\n",
			SAS_ADDR(parent->sas_addr),
			parent_phy->phy_id,
			SAS_ADDR(child->sas_addr),
			child_phy->phy_id);
	}

	return res;
}

/* Here we spill over 80 columns.  It is intentional.
 */
static int sas_check_parent_topology(struct domain_device *child)
{
	struct expander_device *child_ex = &child->ex_dev;
	struct expander_device *parent_ex;
	int i;
	int res = 0;

	if (!child->parent)
		return 0;

	if (!dev_is_expander(child->parent->dev_type))
		return 0;

	parent_ex = &child->parent->ex_dev;

	for (i = 0; i < parent_ex->num_phys; i++) {
		struct ex_phy *parent_phy = &parent_ex->ex_phy[i];
		struct ex_phy *child_phy;

		if (parent_phy->phy_state == PHY_VACANT ||
		    parent_phy->phy_state == PHY_NOT_PRESENT)
			continue;

		if (SAS_ADDR(parent_phy->attached_sas_addr) != SAS_ADDR(child->sas_addr))
			continue;

		child_phy = &child_ex->ex_phy[parent_phy->attached_phy_id];

		switch (child->parent->dev_type) {
		case SAS_EDGE_EXPANDER_DEVICE:
			if (child->dev_type == SAS_FANOUT_EXPANDER_DEVICE) {
				if (parent_phy->routing_attr != SUBTRACTIVE_ROUTING ||
				    child_phy->routing_attr != TABLE_ROUTING) {
					sas_print_parent_topology_bug(child, parent_phy, child_phy);
					res = -ENODEV;
				}
			} else if (parent_phy->routing_attr == SUBTRACTIVE_ROUTING) {
				if (child_phy->routing_attr == SUBTRACTIVE_ROUTING) {
					res = sas_check_eeds(child, parent_phy, child_phy);
				} else if (child_phy->routing_attr != TABLE_ROUTING) {
					sas_print_parent_topology_bug(child, parent_phy, child_phy);
					res = -ENODEV;
				}
			} else if (parent_phy->routing_attr == TABLE_ROUTING) {
				if (child_phy->routing_attr == SUBTRACTIVE_ROUTING ||
				    (child_phy->routing_attr == TABLE_ROUTING &&
				     child_ex->t2t_supp && parent_ex->t2t_supp)) {
					/* All good */;
				} else {
					sas_print_parent_topology_bug(child, parent_phy, child_phy);
					res = -ENODEV;
				}
			}
			break;
		case SAS_FANOUT_EXPANDER_DEVICE:
			if (parent_phy->routing_attr != TABLE_ROUTING ||
			    child_phy->routing_attr != SUBTRACTIVE_ROUTING) {
				sas_print_parent_topology_bug(child, parent_phy, child_phy);
				res = -ENODEV;
			}
			break;
		default:
			break;
		}
	}

	return res;
}

#define RRI_REQ_SIZE  16
#define RRI_RESP_SIZE 44

static int sas_configure_present(struct domain_device *dev, int phy_id,
				 u8 *sas_addr, int *index, int *present)
{
	int i, res = 0;
	struct expander_device *ex = &dev->ex_dev;
	struct ex_phy *phy = &ex->ex_phy[phy_id];
	u8 *rri_req;
	u8 *rri_resp;

	*present = 0;
	*index = 0;

	rri_req = alloc_smp_req(RRI_REQ_SIZE);
	if (!rri_req)
		return -ENOMEM;

	rri_resp = alloc_smp_resp(RRI_RESP_SIZE);
	if (!rri_resp) {
		kfree(rri_req);
		return -ENOMEM;
	}

	rri_req[1] = SMP_REPORT_ROUTE_INFO;
	rri_req[9] = phy_id;

	for (i = 0; i < ex->max_route_indexes ; i++) {
		*(__be16 *)(rri_req+6) = cpu_to_be16(i);
		res = smp_execute_task(dev, rri_req, RRI_REQ_SIZE, rri_resp,
				       RRI_RESP_SIZE);
		if (res)
			goto out;
		res = rri_resp[2];
		if (res == SMP_RESP_NO_INDEX) {
			pr_warn("overflow of indexes: dev %016llx phy%02d index 0x%x\n",
				SAS_ADDR(dev->sas_addr), phy_id, i);
			goto out;
		} else if (res != SMP_RESP_FUNC_ACC) {
			pr_notice("%s: dev %016llx phy%02d index 0x%x result 0x%x\n",
				  __func__, SAS_ADDR(dev->sas_addr), phy_id,
				  i, res);
			goto out;
		}
		if (SAS_ADDR(sas_addr) != 0) {
			if (SAS_ADDR(rri_resp+16) == SAS_ADDR(sas_addr)) {
				*index = i;
				if ((rri_resp[12] & 0x80) == 0x80)
					*present = 0;
				else
					*present = 1;
				goto out;
			} else if (SAS_ADDR(rri_resp+16) == 0) {
				*index = i;
				*present = 0;
				goto out;
			}
		} else if (SAS_ADDR(rri_resp+16) == 0 &&
			   phy->last_da_index < i) {
			phy->last_da_index = i;
			*index = i;
			*present = 0;
			goto out;
		}
	}
	res = -1;
out:
	kfree(rri_req);
	kfree(rri_resp);
	return res;
}

#define CRI_REQ_SIZE  44
#define CRI_RESP_SIZE  8

static int sas_configure_set(struct domain_device *dev, int phy_id,
			     u8 *sas_addr, int index, int include)
{
	int res;
	u8 *cri_req;
	u8 *cri_resp;

	cri_req = alloc_smp_req(CRI_REQ_SIZE);
	if (!cri_req)
		return -ENOMEM;

	cri_resp = alloc_smp_resp(CRI_RESP_SIZE);
	if (!cri_resp) {
		kfree(cri_req);
		return -ENOMEM;
	}

	cri_req[1] = SMP_CONF_ROUTE_INFO;
	*(__be16 *)(cri_req+6) = cpu_to_be16(index);
	cri_req[9] = phy_id;
	if (SAS_ADDR(sas_addr) == 0 || !include)
		cri_req[12] |= 0x80;
	memcpy(cri_req+16, sas_addr, SAS_ADDR_SIZE);

	res = smp_execute_task(dev, cri_req, CRI_REQ_SIZE, cri_resp,
			       CRI_RESP_SIZE);
	if (res)
		goto out;
	res = cri_resp[2];
	if (res == SMP_RESP_NO_INDEX) {
		pr_warn("overflow of indexes: dev %016llx phy%02d index 0x%x\n",
			SAS_ADDR(dev->sas_addr), phy_id, index);
	}
out:
	kfree(cri_req);
	kfree(cri_resp);
	return res;
}

static int sas_configure_phy(struct domain_device *dev, int phy_id,
				    u8 *sas_addr, int include)
{
	int index;
	int present;
	int res;

	res = sas_configure_present(dev, phy_id, sas_addr, &index, &present);
	if (res)
		return res;
	if (include ^ present)
		return sas_configure_set(dev, phy_id, sas_addr, index,include);

	return res;
}

/**
 * sas_configure_parent - configure routing table of parent
 * @parent: parent expander
 * @child: child expander
 * @sas_addr: SAS port identifier of device directly attached to child
 * @include: whether or not to include @child in the expander routing table
 */
static int sas_configure_parent(struct domain_device *parent,
				struct domain_device *child,
				u8 *sas_addr, int include)
{
	struct expander_device *ex_parent = &parent->ex_dev;
	int res = 0;
	int i;

	if (parent->parent) {
		res = sas_configure_parent(parent->parent, parent, sas_addr,
					   include);
		if (res)
			return res;
	}

	if (ex_parent->conf_route_table == 0) {
		pr_debug("ex %016llx has self-configuring routing table\n",
			 SAS_ADDR(parent->sas_addr));
		return 0;
	}

	for (i = 0; i < ex_parent->num_phys; i++) {
		struct ex_phy *phy = &ex_parent->ex_phy[i];

		if ((phy->routing_attr == TABLE_ROUTING) &&
		    (SAS_ADDR(phy->attached_sas_addr) ==
		     SAS_ADDR(child->sas_addr))) {
			res = sas_configure_phy(parent, i, sas_addr, include);
			if (res)
				return res;
		}
	}

	return res;
}

/**
 * sas_configure_routing - configure routing
 * @dev: expander device
 * @sas_addr: port identifier of device directly attached to the expander device
 */
static int sas_configure_routing(struct domain_device *dev, u8 *sas_addr)
{
	if (dev->parent)
		return sas_configure_parent(dev->parent, dev, sas_addr, 1);
	return 0;
}

static int sas_disable_routing(struct domain_device *dev,  u8 *sas_addr)
{
	if (dev->parent)
		return sas_configure_parent(dev->parent, dev, sas_addr, 0);
	return 0;
}

/**
 * sas_discover_expander - expander discovery
 * @dev: pointer to expander domain device
 *
 * See comment in sas_discover_sata().
 */
static int sas_discover_expander(struct domain_device *dev)
{
	int res;

	res = sas_notify_lldd_dev_found(dev);
	if (res)
		return res;

	res = sas_ex_general(dev);
	if (res)
		goto out_err;
	res = sas_ex_manuf_info(dev);
	if (res)
		goto out_err;

	res = sas_expander_discover(dev);
	if (res) {
		pr_warn("expander %016llx discovery failed(0x%x)\n",
			SAS_ADDR(dev->sas_addr), res);
		goto out_err;
	}

	sas_check_ex_subtractive_boundary(dev);
	res = sas_check_parent_topology(dev);
	if (res)
		goto out_err;
	return 0;
out_err:
	sas_notify_lldd_dev_gone(dev);
	return res;
}

static int sas_ex_level_discovery(struct asd_sas_port *port, const int level)
{
	int res = 0;
	struct domain_device *dev;

	list_for_each_entry(dev, &port->dev_list, dev_list_node) {
		if (dev_is_expander(dev->dev_type)) {
			struct sas_expander_device *ex =
				rphy_to_expander_device(dev->rphy);

			if (level == ex->level)
				res = sas_ex_discover_devices(dev, -1);
			else if (level > 0)
				res = sas_ex_discover_devices(port->port_dev, -1);

		}
	}

	return res;
}

static int sas_ex_bfs_disc(struct asd_sas_port *port)
{
	int res;
	int level;

	do {
		level = port->disc.max_level;
		res = sas_ex_level_discovery(port, level);
		mb();
	} while (level < port->disc.max_level);

	return res;
}

int sas_discover_root_expander(struct domain_device *dev)
{
	int res;
	struct sas_expander_device *ex = rphy_to_expander_device(dev->rphy);

	res = sas_rphy_add(dev->rphy);
	if (res)
		goto out_err;

	ex->level = dev->port->disc.max_level; /* 0 */
	res = sas_discover_expander(dev);
	if (res)
		goto out_err2;

	sas_ex_bfs_disc(dev->port);

	return res;

out_err2:
	sas_rphy_remove(dev->rphy);
out_err:
	return res;
}

/* ---------- Domain revalidation ---------- */

static int sas_get_phy_discover(struct domain_device *dev,
				int phy_id, struct smp_resp *disc_resp)
{
	int res;
	u8 *disc_req;

	disc_req = alloc_smp_req(DISCOVER_REQ_SIZE);
	if (!disc_req)
		return -ENOMEM;

	disc_req[1] = SMP_DISCOVER;
	disc_req[9] = phy_id;

	res = smp_execute_task(dev, disc_req, DISCOVER_REQ_SIZE,
			       disc_resp, DISCOVER_RESP_SIZE);
	if (res)
		goto out;
	else if (disc_resp->result != SMP_RESP_FUNC_ACC) {
		res = disc_resp->result;
		goto out;
	}
out:
	kfree(disc_req);
	return res;
}

static int sas_get_phy_change_count(struct domain_device *dev,
				    int phy_id, int *pcc)
{
	int res;
	struct smp_resp *disc_resp;

	disc_resp = alloc_smp_resp(DISCOVER_RESP_SIZE);
	if (!disc_resp)
		return -ENOMEM;

	res = sas_get_phy_discover(dev, phy_id, disc_resp);
	if (!res)
		*pcc = disc_resp->disc.change_count;

	kfree(disc_resp);
	return res;
}

static int sas_get_phy_attached_dev(struct domain_device *dev, int phy_id,
				    u8 *sas_addr, enum sas_device_type *type)
{
	int res;
	struct smp_resp *disc_resp;
	struct discover_resp *dr;

	disc_resp = alloc_smp_resp(DISCOVER_RESP_SIZE);
	if (!disc_resp)
		return -ENOMEM;
	dr = &disc_resp->disc;

	res = sas_get_phy_discover(dev, phy_id, disc_resp);
	if (res == 0) {
		memcpy(sas_addr, disc_resp->disc.attached_sas_addr,
		       SAS_ADDR_SIZE);
		*type = to_dev_type(dr);
		if (*type == 0)
			memset(sas_addr, 0, SAS_ADDR_SIZE);
	}
	kfree(disc_resp);
	return res;
}

static int sas_find_bcast_phy(struct domain_device *dev, int *phy_id,
			      int from_phy, bool update)
{
	struct expander_device *ex = &dev->ex_dev;
	int res = 0;
	int i;

	for (i = from_phy; i < ex->num_phys; i++) {
		int phy_change_count = 0;

		res = sas_get_phy_change_count(dev, i, &phy_change_count);
		switch (res) {
		case SMP_RESP_PHY_VACANT:
		case SMP_RESP_NO_PHY:
			continue;
		case SMP_RESP_FUNC_ACC:
			break;
		default:
			return res;
		}

		if (phy_change_count != ex->ex_phy[i].phy_change_count) {
			if (update)
				ex->ex_phy[i].phy_change_count =
					phy_change_count;
			*phy_id = i;
			return 0;
		}
	}
	return 0;
}

static int sas_get_ex_change_count(struct domain_device *dev, int *ecc)
{
	int res;
	u8  *rg_req;
	struct smp_resp  *rg_resp;

	rg_req = alloc_smp_req(RG_REQ_SIZE);
	if (!rg_req)
		return -ENOMEM;

	rg_resp = alloc_smp_resp(RG_RESP_SIZE);
	if (!rg_resp) {
		kfree(rg_req);
		return -ENOMEM;
	}

	rg_req[1] = SMP_REPORT_GENERAL;

	res = smp_execute_task(dev, rg_req, RG_REQ_SIZE, rg_resp,
			       RG_RESP_SIZE);
	if (res)
		goto out;
	if (rg_resp->result != SMP_RESP_FUNC_ACC) {
		res = rg_resp->result;
		goto out;
	}

	*ecc = be16_to_cpu(rg_resp->rg.change_count);
out:
	kfree(rg_resp);
	kfree(rg_req);
	return res;
}
/**
 * sas_find_bcast_dev -  find the device issue BROADCAST(CHANGE).
 * @dev:domain device to be detect.
 * @src_dev: the device which originated BROADCAST(CHANGE).
 *
 * Add self-configuration expander support. Suppose two expander cascading,
 * when the first level expander is self-configuring, hotplug the disks in
 * second level expander, BROADCAST(CHANGE) will not only be originated
 * in the second level expander, but also be originated in the first level
 * expander (see SAS protocol SAS 2r-14, 7.11 for detail), it is to say,
 * expander changed count in two level expanders will all increment at least
 * once, but the phy which chang count has changed is the source device which
 * we concerned.
 */

static int sas_find_bcast_dev(struct domain_device *dev,
			      struct domain_device **src_dev)
{
	struct expander_device *ex = &dev->ex_dev;
	int ex_change_count = -1;
	int phy_id = -1;
	int res;
	struct domain_device *ch;

	res = sas_get_ex_change_count(dev, &ex_change_count);
	if (res)
		goto out;
	if (ex_change_count != -1 && ex_change_count != ex->ex_change_count) {
		/* Just detect if this expander phys phy change count changed,
		* in order to determine if this expander originate BROADCAST,
		* and do not update phy change count field in our structure.
		*/
		res = sas_find_bcast_phy(dev, &phy_id, 0, false);
		if (phy_id != -1) {
			*src_dev = dev;
			ex->ex_change_count = ex_change_count;
			pr_info("ex %016llx phy%02d change count has changed\n",
				SAS_ADDR(dev->sas_addr), phy_id);
			return res;
		} else
			pr_info("ex %016llx phys DID NOT change\n",
				SAS_ADDR(dev->sas_addr));
	}
	list_for_each_entry(ch, &ex->children, siblings) {
		if (dev_is_expander(ch->dev_type)) {
			res = sas_find_bcast_dev(ch, src_dev);
			if (*src_dev)
				return res;
		}
	}
out:
	return res;
}

static void sas_unregister_ex_tree(struct asd_sas_port *port, struct domain_device *dev)
{
	struct expander_device *ex = &dev->ex_dev;
	struct domain_device *child, *n;

	list_for_each_entry_safe(child, n, &ex->children, siblings) {
		set_bit(SAS_DEV_GONE, &child->state);
		if (dev_is_expander(child->dev_type))
			sas_unregister_ex_tree(port, child);
		else
			sas_unregister_dev(port, child);
	}
	sas_unregister_dev(port, dev);
}

static void sas_unregister_devs_sas_addr(struct domain_device *parent,
					 int phy_id, bool last)
{
	struct expander_device *ex_dev = &parent->ex_dev;
	struct ex_phy *phy = &ex_dev->ex_phy[phy_id];
	struct domain_device *child, *n, *found = NULL;
	if (last) {
		list_for_each_entry_safe(child, n,
			&ex_dev->children, siblings) {
			if (SAS_ADDR(child->sas_addr) ==
			    SAS_ADDR(phy->attached_sas_addr)) {
				set_bit(SAS_DEV_GONE, &child->state);
				if (dev_is_expander(child->dev_type))
					sas_unregister_ex_tree(parent->port, child);
				else
					sas_unregister_dev(parent->port, child);
				found = child;
				break;
			}
		}
		sas_disable_routing(parent, phy->attached_sas_addr);
	}
	memset(phy->attached_sas_addr, 0, SAS_ADDR_SIZE);
	if (phy->port) {
		sas_port_delete_phy(phy->port, phy->phy);
		sas_device_set_phy(found, phy->port);
		if (phy->port->num_phys == 0)
			list_add_tail(&phy->port->del_list,
				&parent->port->sas_port_del_list);
		phy->port = NULL;
	}
}

static int sas_discover_bfs_by_root_level(struct domain_device *root,
					  const int level)
{
	struct expander_device *ex_root = &root->ex_dev;
	struct domain_device *child;
	int res = 0;

	list_for_each_entry(child, &ex_root->children, siblings) {
		if (dev_is_expander(child->dev_type)) {
			struct sas_expander_device *ex =
				rphy_to_expander_device(child->rphy);

			if (level > ex->level)
				res = sas_discover_bfs_by_root_level(child,
								     level);
			else if (level == ex->level)
				res = sas_ex_discover_devices(child, -1);
		}
	}
	return res;
}

static int sas_discover_bfs_by_root(struct domain_device *dev)
{
	int res;
	struct sas_expander_device *ex = rphy_to_expander_device(dev->rphy);
	int level = ex->level+1;

	res = sas_ex_discover_devices(dev, -1);
	if (res)
		goto out;
	do {
		res = sas_discover_bfs_by_root_level(dev, level);
		mb();
		level += 1;
	} while (level <= dev->port->disc.max_level);
out:
	return res;
}

static int sas_discover_new(struct domain_device *dev, int phy_id)
{
	struct ex_phy *ex_phy = &dev->ex_dev.ex_phy[phy_id];
	struct domain_device *child;
	int res;

	pr_debug("ex %016llx phy%02d new device attached\n",
		 SAS_ADDR(dev->sas_addr), phy_id);
	res = sas_ex_phy_discover(dev, phy_id);
	if (res)
		return res;

	if (sas_ex_join_wide_port(dev, phy_id))
		return 0;

	res = sas_ex_discover_devices(dev, phy_id);
	if (res)
		return res;
	list_for_each_entry(child, &dev->ex_dev.children, siblings) {
		if (SAS_ADDR(child->sas_addr) ==
		    SAS_ADDR(ex_phy->attached_sas_addr)) {
			if (dev_is_expander(child->dev_type))
				res = sas_discover_bfs_by_root(child);
			break;
		}
	}
	return res;
}

static bool dev_type_flutter(enum sas_device_type new, enum sas_device_type old)
{
	if (old == new)
		return true;

	/* treat device directed resets as flutter, if we went
	 * SAS_END_DEVICE to SAS_SATA_PENDING the link needs recovery
	 */
	if ((old == SAS_SATA_PENDING && new == SAS_END_DEVICE) ||
	    (old == SAS_END_DEVICE && new == SAS_SATA_PENDING))
		return true;

	return false;
}

static int sas_rediscover_dev(struct domain_device *dev, int phy_id,
			      bool last, int sibling)
{
	struct expander_device *ex = &dev->ex_dev;
	struct ex_phy *phy = &ex->ex_phy[phy_id];
	enum sas_device_type type = SAS_PHY_UNUSED;
	u8 sas_addr[SAS_ADDR_SIZE];
	char msg[80] = "";
	int res;

	if (!last)
		sprintf(msg, ", part of a wide port with phy%02d", sibling);

	pr_debug("ex %016llx rediscovering phy%02d%s\n",
		 SAS_ADDR(dev->sas_addr), phy_id, msg);

	memset(sas_addr, 0, SAS_ADDR_SIZE);
	res = sas_get_phy_attached_dev(dev, phy_id, sas_addr, &type);
	switch (res) {
	case SMP_RESP_NO_PHY:
		phy->phy_state = PHY_NOT_PRESENT;
		sas_unregister_devs_sas_addr(dev, phy_id, last);
		return res;
	case SMP_RESP_PHY_VACANT:
		phy->phy_state = PHY_VACANT;
		sas_unregister_devs_sas_addr(dev, phy_id, last);
		return res;
	case SMP_RESP_FUNC_ACC:
		break;
	case -ECOMM:
		break;
	default:
		return res;
	}

	if ((SAS_ADDR(sas_addr) == 0) || (res == -ECOMM)) {
		phy->phy_state = PHY_EMPTY;
		sas_unregister_devs_sas_addr(dev, phy_id, last);
		/*
		 * Even though the PHY is empty, for convenience we discover
		 * the PHY to update the PHY info, like negotiated linkrate.
		 */
		sas_ex_phy_discover(dev, phy_id);
		return res;
	} else if (SAS_ADDR(sas_addr) == SAS_ADDR(phy->attached_sas_addr) &&
		   dev_type_flutter(type, phy->attached_dev_type)) {
		struct domain_device *ata_dev = sas_ex_to_ata(dev, phy_id);
		char *action = "";

		sas_ex_phy_discover(dev, phy_id);

		if (ata_dev && phy->attached_dev_type == SAS_SATA_PENDING)
			action = ", needs recovery";
		pr_debug("ex %016llx phy%02d broadcast flutter%s\n",
			 SAS_ADDR(dev->sas_addr), phy_id, action);
		return res;
	}

	/* we always have to delete the old device when we went here */
	pr_info("ex %016llx phy%02d replace %016llx\n",
		SAS_ADDR(dev->sas_addr), phy_id,
		SAS_ADDR(phy->attached_sas_addr));
	sas_unregister_devs_sas_addr(dev, phy_id, last);

	return sas_discover_new(dev, phy_id);
}

/**
 * sas_rediscover - revalidate the domain.
 * @dev:domain device to be detect.
 * @phy_id: the phy id will be detected.
 *
 * NOTE: this process _must_ quit (return) as soon as any connection
 * errors are encountered.  Connection recovery is done elsewhere.
 * Discover process only interrogates devices in order to discover the
 * domain.For plugging out, we un-register the device only when it is
 * the last phy in the port, for other phys in this port, we just delete it
 * from the port.For inserting, we do discovery when it is the
 * first phy,for other phys in this port, we add it to the port to
 * forming the wide-port.
 */
static int sas_rediscover(struct domain_device *dev, const int phy_id)
{
	struct expander_device *ex = &dev->ex_dev;
	struct ex_phy *changed_phy = &ex->ex_phy[phy_id];
	int res = 0;
	int i;
	bool last = true;	/* is this the last phy of the port */

	pr_debug("ex %016llx phy%02d originated BROADCAST(CHANGE)\n",
		 SAS_ADDR(dev->sas_addr), phy_id);

	if (SAS_ADDR(changed_phy->attached_sas_addr) != 0) {
		for (i = 0; i < ex->num_phys; i++) {
			struct ex_phy *phy = &ex->ex_phy[i];

			if (i == phy_id)
				continue;
			if (SAS_ADDR(phy->attached_sas_addr) ==
			    SAS_ADDR(changed_phy->attached_sas_addr)) {
				last = false;
				break;
			}
		}
		res = sas_rediscover_dev(dev, phy_id, last, i);
	} else
		res = sas_discover_new(dev, phy_id);
	return res;
}

/**
 * sas_ex_revalidate_domain - revalidate the domain
 * @port_dev: port domain device.
 *
 * NOTE: this process _must_ quit (return) as soon as any connection
 * errors are encountered.  Connection recovery is done elsewhere.
 * Discover process only interrogates devices in order to discover the
 * domain.
 */
int sas_ex_revalidate_domain(struct domain_device *port_dev)
{
	int res;
	struct domain_device *dev = NULL;

	res = sas_find_bcast_dev(port_dev, &dev);
	if (res == 0 && dev) {
		struct expander_device *ex = &dev->ex_dev;
		int i = 0, phy_id;

		do {
			phy_id = -1;
			res = sas_find_bcast_phy(dev, &phy_id, i, true);
			if (phy_id == -1)
				break;
			res = sas_rediscover(dev, phy_id);
			i = phy_id + 1;
		} while (i < ex->num_phys);
	}
	return res;
}

void sas_smp_handler(struct bsg_job *job, struct Scsi_Host *shost,
		struct sas_rphy *rphy)
{
	struct domain_device *dev;
	unsigned int rcvlen = 0;
	int ret = -EINVAL;

	/* no rphy means no smp target support (ie aic94xx host) */
	if (!rphy)
		return sas_smp_host_handler(job, shost);

	switch (rphy->identify.device_type) {
	case SAS_EDGE_EXPANDER_DEVICE:
	case SAS_FANOUT_EXPANDER_DEVICE:
		break;
	default:
		pr_err("%s: can we send a smp request to a device?\n",
		       __func__);
		goto out;
	}

	dev = sas_find_dev_by_rphy(rphy);
	if (!dev) {
		pr_err("%s: fail to find a domain_device?\n", __func__);
		goto out;
	}

	/* do we need to support multiple segments? */
	if (job->request_payload.sg_cnt > 1 ||
	    job->reply_payload.sg_cnt > 1) {
		pr_info("%s: multiple segments req %u, rsp %u\n",
			__func__, job->request_payload.payload_len,
			job->reply_payload.payload_len);
		goto out;
	}

	ret = smp_execute_task_sg(dev, job->request_payload.sg_list,
			job->reply_payload.sg_list);
	if (ret >= 0) {
		/* bsg_job_done() requires the length received  */
		rcvlen = job->reply_payload.payload_len - ret;
		ret = 0;
	}

out:
	bsg_job_done(job, ret, rcvlen);
}
