// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2017 - 2019 Pensando Systems, Inc */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/etherdevice.h>
#include "ionic.h"
#include "ionic_dev.h"
#include "ionic_lif.h"

static void ionic_watchdog_cb(struct timer_list *t)
{
	struct ionic *ionic = from_timer(ionic, t, watchdog_timer);
	int hb;

	mod_timer(&ionic->watchdog_timer,
		  round_jiffies(jiffies + ionic->watchdog_period));

	hb = ionic_heartbeat_check(ionic);

	if (hb >= 0 && ionic->master_lif)
		ionic_link_status_check_request(ionic->master_lif);
}

void ionic_init_devinfo(struct ionic *ionic)
{
	struct ionic_dev *idev = &ionic->idev;

	idev->dev_info.asic_type = ioread8(&idev->dev_info_regs->asic_type);
	idev->dev_info.asic_rev = ioread8(&idev->dev_info_regs->asic_rev);

	memcpy_fromio(idev->dev_info.fw_version,
		      idev->dev_info_regs->fw_version,
		      IONIC_DEVINFO_FWVERS_BUFLEN);

	memcpy_fromio(idev->dev_info.serial_num,
		      idev->dev_info_regs->serial_num,
		      IONIC_DEVINFO_SERIAL_BUFLEN);

	idev->dev_info.fw_version[IONIC_DEVINFO_FWVERS_BUFLEN] = 0;
	idev->dev_info.serial_num[IONIC_DEVINFO_SERIAL_BUFLEN] = 0;

	dev_dbg(ionic->dev, "fw_version %s\n", idev->dev_info.fw_version);
}

int ionic_dev_setup(struct ionic *ionic)
{
	struct ionic_dev_bar *bar = ionic->bars;
	unsigned int num_bars = ionic->num_bars;
	struct ionic_dev *idev = &ionic->idev;
	struct device *dev = ionic->dev;
	u32 sig;

	/* BAR0: dev_cmd and interrupts */
	if (num_bars < 1) {
		dev_err(dev, "No bars found, aborting\n");
		return -EFAULT;
	}

	if (bar->len < IONIC_BAR0_SIZE) {
		dev_err(dev, "Resource bar size %lu too small, aborting\n",
			bar->len);
		return -EFAULT;
	}

	idev->dev_info_regs = bar->vaddr + IONIC_BAR0_DEV_INFO_REGS_OFFSET;
	idev->dev_cmd_regs = bar->vaddr + IONIC_BAR0_DEV_CMD_REGS_OFFSET;
	idev->intr_status = bar->vaddr + IONIC_BAR0_INTR_STATUS_OFFSET;
	idev->intr_ctrl = bar->vaddr + IONIC_BAR0_INTR_CTRL_OFFSET;

	sig = ioread32(&idev->dev_info_regs->signature);
	if (sig != IONIC_DEV_INFO_SIGNATURE) {
		dev_err(dev, "Incompatible firmware signature %x", sig);
		return -EFAULT;
	}

	ionic_init_devinfo(ionic);

	/* BAR1: doorbells */
	bar++;
	if (num_bars < 2) {
		dev_err(dev, "Doorbell bar missing, aborting\n");
		return -EFAULT;
	}

	idev->last_fw_status = 0xff;
	timer_setup(&ionic->watchdog_timer, ionic_watchdog_cb, 0);
	ionic->watchdog_period = IONIC_WATCHDOG_SECS * HZ;
	mod_timer(&ionic->watchdog_timer,
		  round_jiffies(jiffies + ionic->watchdog_period));

	idev->db_pages = bar->vaddr;
	idev->phy_db_pages = bar->bus_addr;

	return 0;
}

void ionic_dev_teardown(struct ionic *ionic)
{
	del_timer_sync(&ionic->watchdog_timer);
}

/* Devcmd Interface */
int ionic_heartbeat_check(struct ionic *ionic)
{
	struct ionic_dev *idev = &ionic->idev;
	unsigned long hb_time;
	u8 fw_status;
	u32 hb;

	/* wait a little more than one second before testing again */
	hb_time = jiffies;
	if (time_before(hb_time, (idev->last_hb_time + ionic->watchdog_period)))
		return 0;

	/* firmware is useful only if the running bit is set and
	 * fw_status != 0xff (bad PCI read)
	 */
	fw_status = ioread8(&idev->dev_info_regs->fw_status);
	if (fw_status != 0xff)
		fw_status &= IONIC_FW_STS_F_RUNNING;  /* use only the run bit */

	/* is this a transition? */
	if (fw_status != idev->last_fw_status &&
	    idev->last_fw_status != 0xff) {
		struct ionic_lif *lif = ionic->master_lif;
		bool trigger = false;

		if (!fw_status || fw_status == 0xff) {
			dev_info(ionic->dev, "FW stopped %u\n", fw_status);
			if (lif && !test_bit(IONIC_LIF_F_FW_RESET, lif->state))
				trigger = true;
		} else {
			dev_info(ionic->dev, "FW running %u\n", fw_status);
			if (lif && test_bit(IONIC_LIF_F_FW_RESET, lif->state))
				trigger = true;
		}

		if (trigger) {
			struct ionic_deferred_work *work;

			work = kzalloc(sizeof(*work), GFP_ATOMIC);
			if (!work) {
				dev_err(ionic->dev, "%s OOM\n", __func__);
			} else {
				work->type = IONIC_DW_TYPE_LIF_RESET;
				if (fw_status & IONIC_FW_STS_F_RUNNING &&
				    fw_status != 0xff)
					work->fw_status = 1;
				ionic_lif_deferred_enqueue(&lif->deferred, work);
			}
		}
	}
	idev->last_fw_status = fw_status;

	if (!fw_status || fw_status == 0xff)
		return -ENXIO;

	/* early FW has no heartbeat, else FW will return non-zero */
	hb = ioread32(&idev->dev_info_regs->fw_heartbeat);
	if (!hb)
		return 0;

	/* are we stalled? */
	if (hb == idev->last_hb) {
		/* only complain once for each stall seen */
		if (idev->last_hb_time != 1) {
			dev_info(ionic->dev, "FW heartbeat stalled at %d\n",
				 idev->last_hb);
			idev->last_hb_time = 1;
		}

		return -ENXIO;
	}

	if (idev->last_hb_time == 1)
		dev_info(ionic->dev, "FW heartbeat restored at %d\n", hb);

	idev->last_hb = hb;
	idev->last_hb_time = hb_time;

	return 0;
}

u8 ionic_dev_cmd_status(struct ionic_dev *idev)
{
	return ioread8(&idev->dev_cmd_regs->comp.comp.status);
}

bool ionic_dev_cmd_done(struct ionic_dev *idev)
{
	return ioread32(&idev->dev_cmd_regs->done) & IONIC_DEV_CMD_DONE;
}

void ionic_dev_cmd_comp(struct ionic_dev *idev, union ionic_dev_cmd_comp *comp)
{
	memcpy_fromio(comp, &idev->dev_cmd_regs->comp, sizeof(*comp));
}

void ionic_dev_cmd_go(struct ionic_dev *idev, union ionic_dev_cmd *cmd)
{
	memcpy_toio(&idev->dev_cmd_regs->cmd, cmd, sizeof(*cmd));
	iowrite32(0, &idev->dev_cmd_regs->done);
	iowrite32(1, &idev->dev_cmd_regs->doorbell);
}

/* Device commands */
void ionic_dev_cmd_identify(struct ionic_dev *idev, u8 ver)
{
	union ionic_dev_cmd cmd = {
		.identify.opcode = IONIC_CMD_IDENTIFY,
		.identify.ver = ver,
	};

	ionic_dev_cmd_go(idev, &cmd);
}

void ionic_dev_cmd_init(struct ionic_dev *idev)
{
	union ionic_dev_cmd cmd = {
		.init.opcode = IONIC_CMD_INIT,
		.init.type = 0,
	};

	ionic_dev_cmd_go(idev, &cmd);
}

void ionic_dev_cmd_reset(struct ionic_dev *idev)
{
	union ionic_dev_cmd cmd = {
		.reset.opcode = IONIC_CMD_RESET,
	};

	ionic_dev_cmd_go(idev, &cmd);
}

/* Port commands */
void ionic_dev_cmd_port_identify(struct ionic_dev *idev)
{
	union ionic_dev_cmd cmd = {
		.port_init.opcode = IONIC_CMD_PORT_IDENTIFY,
		.port_init.index = 0,
	};

	ionic_dev_cmd_go(idev, &cmd);
}

void ionic_dev_cmd_port_init(struct ionic_dev *idev)
{
	union ionic_dev_cmd cmd = {
		.port_init.opcode = IONIC_CMD_PORT_INIT,
		.port_init.index = 0,
		.port_init.info_pa = cpu_to_le64(idev->port_info_pa),
	};

	ionic_dev_cmd_go(idev, &cmd);
}

void ionic_dev_cmd_port_reset(struct ionic_dev *idev)
{
	union ionic_dev_cmd cmd = {
		.port_reset.opcode = IONIC_CMD_PORT_RESET,
		.port_reset.index = 0,
	};

	ionic_dev_cmd_go(idev, &cmd);
}

void ionic_dev_cmd_port_state(struct ionic_dev *idev, u8 state)
{
	union ionic_dev_cmd cmd = {
		.port_setattr.opcode = IONIC_CMD_PORT_SETATTR,
		.port_setattr.index = 0,
		.port_setattr.attr = IONIC_PORT_ATTR_STATE,
		.port_setattr.state = state,
	};

	ionic_dev_cmd_go(idev, &cmd);
}

void ionic_dev_cmd_port_speed(struct ionic_dev *idev, u32 speed)
{
	union ionic_dev_cmd cmd = {
		.port_setattr.opcode = IONIC_CMD_PORT_SETATTR,
		.port_setattr.index = 0,
		.port_setattr.attr = IONIC_PORT_ATTR_SPEED,
		.port_setattr.speed = cpu_to_le32(speed),
	};

	ionic_dev_cmd_go(idev, &cmd);
}

void ionic_dev_cmd_port_autoneg(struct ionic_dev *idev, u8 an_enable)
{
	union ionic_dev_cmd cmd = {
		.port_setattr.opcode = IONIC_CMD_PORT_SETATTR,
		.port_setattr.index = 0,
		.port_setattr.attr = IONIC_PORT_ATTR_AUTONEG,
		.port_setattr.an_enable = an_enable,
	};

	ionic_dev_cmd_go(idev, &cmd);
}

void ionic_dev_cmd_port_fec(struct ionic_dev *idev, u8 fec_type)
{
	union ionic_dev_cmd cmd = {
		.port_setattr.opcode = IONIC_CMD_PORT_SETATTR,
		.port_setattr.index = 0,
		.port_setattr.attr = IONIC_PORT_ATTR_FEC,
		.port_setattr.fec_type = fec_type,
	};

	ionic_dev_cmd_go(idev, &cmd);
}

void ionic_dev_cmd_port_pause(struct ionic_dev *idev, u8 pause_type)
{
	union ionic_dev_cmd cmd = {
		.port_setattr.opcode = IONIC_CMD_PORT_SETATTR,
		.port_setattr.index = 0,
		.port_setattr.attr = IONIC_PORT_ATTR_PAUSE,
		.port_setattr.pause_type = pause_type,
	};

	ionic_dev_cmd_go(idev, &cmd);
}

/* VF commands */
int ionic_set_vf_config(struct ionic *ionic, int vf, u8 attr, u8 *data)
{
	union ionic_dev_cmd cmd = {
		.vf_setattr.opcode = IONIC_CMD_VF_SETATTR,
		.vf_setattr.attr = attr,
		.vf_setattr.vf_index = vf,
	};
	int err;

	switch (attr) {
	case IONIC_VF_ATTR_SPOOFCHK:
		cmd.vf_setattr.spoofchk = *data;
		dev_dbg(ionic->dev, "%s: vf %d spoof %d\n",
			__func__, vf, *data);
		break;
	case IONIC_VF_ATTR_TRUST:
		cmd.vf_setattr.trust = *data;
		dev_dbg(ionic->dev, "%s: vf %d trust %d\n",
			__func__, vf, *data);
		break;
	case IONIC_VF_ATTR_LINKSTATE:
		cmd.vf_setattr.linkstate = *data;
		dev_dbg(ionic->dev, "%s: vf %d linkstate %d\n",
			__func__, vf, *data);
		break;
	case IONIC_VF_ATTR_MAC:
		ether_addr_copy(cmd.vf_setattr.macaddr, data);
		dev_dbg(ionic->dev, "%s: vf %d macaddr %pM\n",
			__func__, vf, data);
		break;
	case IONIC_VF_ATTR_VLAN:
		cmd.vf_setattr.vlanid = cpu_to_le16(*(u16 *)data);
		dev_dbg(ionic->dev, "%s: vf %d vlan %d\n",
			__func__, vf, *(u16 *)data);
		break;
	case IONIC_VF_ATTR_RATE:
		cmd.vf_setattr.maxrate = cpu_to_le32(*(u32 *)data);
		dev_dbg(ionic->dev, "%s: vf %d maxrate %d\n",
			__func__, vf, *(u32 *)data);
		break;
	case IONIC_VF_ATTR_STATSADDR:
		cmd.vf_setattr.stats_pa = cpu_to_le64(*(u64 *)data);
		dev_dbg(ionic->dev, "%s: vf %d stats_pa 0x%08llx\n",
			__func__, vf, *(u64 *)data);
		break;
	default:
		return -EINVAL;
	}

	mutex_lock(&ionic->dev_cmd_lock);
	ionic_dev_cmd_go(&ionic->idev, &cmd);
	err = ionic_dev_cmd_wait(ionic, DEVCMD_TIMEOUT);
	mutex_unlock(&ionic->dev_cmd_lock);

	return err;
}

/* LIF commands */
void ionic_dev_cmd_queue_identify(struct ionic_dev *idev,
				  u16 lif_type, u8 qtype, u8 qver)
{
	union ionic_dev_cmd cmd = {
		.q_identify.opcode = IONIC_CMD_Q_IDENTIFY,
		.q_identify.lif_type = lif_type,
		.q_identify.type = qtype,
		.q_identify.ver = qver,
	};

	ionic_dev_cmd_go(idev, &cmd);
}

void ionic_dev_cmd_lif_identify(struct ionic_dev *idev, u8 type, u8 ver)
{
	union ionic_dev_cmd cmd = {
		.lif_identify.opcode = IONIC_CMD_LIF_IDENTIFY,
		.lif_identify.type = type,
		.lif_identify.ver = ver,
	};

	ionic_dev_cmd_go(idev, &cmd);
}

void ionic_dev_cmd_lif_init(struct ionic_dev *idev, u16 lif_index,
			    dma_addr_t info_pa)
{
	union ionic_dev_cmd cmd = {
		.lif_init.opcode = IONIC_CMD_LIF_INIT,
		.lif_init.index = cpu_to_le16(lif_index),
		.lif_init.info_pa = cpu_to_le64(info_pa),
	};

	ionic_dev_cmd_go(idev, &cmd);
}

void ionic_dev_cmd_lif_reset(struct ionic_dev *idev, u16 lif_index)
{
	union ionic_dev_cmd cmd = {
		.lif_init.opcode = IONIC_CMD_LIF_RESET,
		.lif_init.index = cpu_to_le16(lif_index),
	};

	ionic_dev_cmd_go(idev, &cmd);
}

void ionic_dev_cmd_adminq_init(struct ionic_dev *idev, struct ionic_qcq *qcq,
			       u16 lif_index, u16 intr_index)
{
	struct ionic_queue *q = &qcq->q;
	struct ionic_cq *cq = &qcq->cq;

	union ionic_dev_cmd cmd = {
		.q_init.opcode = IONIC_CMD_Q_INIT,
		.q_init.lif_index = cpu_to_le16(lif_index),
		.q_init.type = q->type,
		.q_init.ver = qcq->q.lif->qtype_info[q->type].version,
		.q_init.index = cpu_to_le32(q->index),
		.q_init.flags = cpu_to_le16(IONIC_QINIT_F_IRQ |
					    IONIC_QINIT_F_ENA),
		.q_init.pid = cpu_to_le16(q->pid),
		.q_init.intr_index = cpu_to_le16(intr_index),
		.q_init.ring_size = ilog2(q->num_descs),
		.q_init.ring_base = cpu_to_le64(q->base_pa),
		.q_init.cq_ring_base = cpu_to_le64(cq->base_pa),
	};

	ionic_dev_cmd_go(idev, &cmd);
}

int ionic_db_page_num(struct ionic_lif *lif, int pid)
{
	return (lif->hw_index * lif->dbid_count) + pid;
}

int ionic_cq_init(struct ionic_lif *lif, struct ionic_cq *cq,
		  struct ionic_intr_info *intr,
		  unsigned int num_descs, size_t desc_size)
{
	struct ionic_cq_info *cur;
	unsigned int ring_size;
	unsigned int i;

	if (desc_size == 0 || !is_power_of_2(num_descs))
		return -EINVAL;

	ring_size = ilog2(num_descs);
	if (ring_size < 2 || ring_size > 16)
		return -EINVAL;

	cq->lif = lif;
	cq->bound_intr = intr;
	cq->num_descs = num_descs;
	cq->desc_size = desc_size;
	cq->tail = cq->info;
	cq->done_color = 1;

	cur = cq->info;

	for (i = 0; i < num_descs; i++) {
		if (i + 1 == num_descs) {
			cur->next = cq->info;
			cur->last = true;
		} else {
			cur->next = cur + 1;
		}
		cur->index = i;
		cur++;
	}

	return 0;
}

void ionic_cq_map(struct ionic_cq *cq, void *base, dma_addr_t base_pa)
{
	struct ionic_cq_info *cur;
	unsigned int i;

	cq->base = base;
	cq->base_pa = base_pa;

	for (i = 0, cur = cq->info; i < cq->num_descs; i++, cur++)
		cur->cq_desc = base + (i * cq->desc_size);
}

void ionic_cq_bind(struct ionic_cq *cq, struct ionic_queue *q)
{
	cq->bound_q = q;
}

unsigned int ionic_cq_service(struct ionic_cq *cq, unsigned int work_to_do,
			      ionic_cq_cb cb, ionic_cq_done_cb done_cb,
			      void *done_arg)
{
	unsigned int work_done = 0;

	if (work_to_do == 0)
		return 0;

	while (cb(cq, cq->tail)) {
		if (cq->tail->last)
			cq->done_color = !cq->done_color;
		cq->tail = cq->tail->next;
		DEBUG_STATS_CQE_CNT(cq);

		if (++work_done >= work_to_do)
			break;
	}

	if (work_done && done_cb)
		done_cb(done_arg);

	return work_done;
}

int ionic_q_init(struct ionic_lif *lif, struct ionic_dev *idev,
		 struct ionic_queue *q, unsigned int index, const char *name,
		 unsigned int num_descs, size_t desc_size,
		 size_t sg_desc_size, unsigned int pid)
{
	struct ionic_desc_info *cur;
	unsigned int ring_size;
	unsigned int i;

	if (desc_size == 0 || !is_power_of_2(num_descs))
		return -EINVAL;

	ring_size = ilog2(num_descs);
	if (ring_size < 2 || ring_size > 16)
		return -EINVAL;

	q->lif = lif;
	q->idev = idev;
	q->index = index;
	q->num_descs = num_descs;
	q->desc_size = desc_size;
	q->sg_desc_size = sg_desc_size;
	q->tail = q->info;
	q->head = q->tail;
	q->pid = pid;

	snprintf(q->name, sizeof(q->name), "L%d-%s%u", lif->index, name, index);

	cur = q->info;

	for (i = 0; i < num_descs; i++) {
		if (i + 1 == num_descs)
			cur->next = q->info;
		else
			cur->next = cur + 1;
		cur->index = i;
		cur->left = num_descs - i;
		cur++;
	}

	return 0;
}

void ionic_q_map(struct ionic_queue *q, void *base, dma_addr_t base_pa)
{
	struct ionic_desc_info *cur;
	unsigned int i;

	q->base = base;
	q->base_pa = base_pa;

	for (i = 0, cur = q->info; i < q->num_descs; i++, cur++)
		cur->desc = base + (i * q->desc_size);
}

void ionic_q_sg_map(struct ionic_queue *q, void *base, dma_addr_t base_pa)
{
	struct ionic_desc_info *cur;
	unsigned int i;

	q->sg_base = base;
	q->sg_base_pa = base_pa;

	for (i = 0, cur = q->info; i < q->num_descs; i++, cur++)
		cur->sg_desc = base + (i * q->sg_desc_size);
}

void ionic_q_post(struct ionic_queue *q, bool ring_doorbell, ionic_desc_cb cb,
		  void *cb_arg)
{
	struct device *dev = q->lif->ionic->dev;
	struct ionic_lif *lif = q->lif;

	q->head->cb = cb;
	q->head->cb_arg = cb_arg;
	q->head = q->head->next;

	dev_dbg(dev, "lif=%d qname=%s qid=%d qtype=%d p_index=%d ringdb=%d\n",
		q->lif->index, q->name, q->hw_type, q->hw_index,
		q->head->index, ring_doorbell);

	if (ring_doorbell)
		ionic_dbell_ring(lif->kern_dbpage, q->hw_type,
				 q->dbval | q->head->index);
}

static bool ionic_q_is_posted(struct ionic_queue *q, unsigned int pos)
{
	unsigned int mask, tail, head;

	mask = q->num_descs - 1;
	tail = q->tail->index;
	head = q->head->index;

	return ((pos - tail) & mask) < ((head - tail) & mask);
}

void ionic_q_service(struct ionic_queue *q, struct ionic_cq_info *cq_info,
		     unsigned int stop_index)
{
	struct ionic_desc_info *desc_info;
	ionic_desc_cb cb;
	void *cb_arg;

	/* check for empty queue */
	if (q->tail->index == q->head->index)
		return;

	/* stop index must be for a descriptor that is not yet completed */
	if (unlikely(!ionic_q_is_posted(q, stop_index)))
		dev_err(q->lif->ionic->dev,
			"ionic stop is not posted %s stop %u tail %u head %u\n",
			q->name, stop_index, q->tail->index, q->head->index);

	do {
		desc_info = q->tail;
		q->tail = desc_info->next;

		cb = desc_info->cb;
		cb_arg = desc_info->cb_arg;

		desc_info->cb = NULL;
		desc_info->cb_arg = NULL;

		if (cb)
			cb(q, desc_info, cq_info, cb_arg);
	} while (desc_info->index != stop_index);
}
