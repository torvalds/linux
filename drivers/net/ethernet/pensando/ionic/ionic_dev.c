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
	struct ionic_lif *lif = ionic->lif;
	struct ionic_deferred_work *work;
	int hb;

	mod_timer(&ionic->watchdog_timer,
		  round_jiffies(jiffies + ionic->watchdog_period));

	if (!lif)
		return;

	hb = ionic_heartbeat_check(ionic);
	dev_dbg(ionic->dev, "%s: hb %d running %d UP %d\n",
		__func__, hb, netif_running(lif->netdev),
		test_bit(IONIC_LIF_F_UP, lif->state));

	if (hb >= 0 &&
	    !test_bit(IONIC_LIF_F_FW_RESET, lif->state))
		ionic_link_status_check_request(lif, CAN_NOT_SLEEP);

	if (test_bit(IONIC_LIF_F_FILTER_SYNC_NEEDED, lif->state) &&
	    !test_bit(IONIC_LIF_F_FW_RESET, lif->state)) {
		work = kzalloc(sizeof(*work), GFP_ATOMIC);
		if (!work) {
			netdev_err(lif->netdev, "rxmode change dropped\n");
			return;
		}

		work->type = IONIC_DW_TYPE_RX_MODE;
		netdev_dbg(lif->netdev, "deferred: rx_mode\n");
		ionic_lif_deferred_enqueue(&lif->deferred, work);
	}
}

static void ionic_watchdog_init(struct ionic *ionic)
{
	struct ionic_dev *idev = &ionic->idev;

	timer_setup(&ionic->watchdog_timer, ionic_watchdog_cb, 0);
	ionic->watchdog_period = IONIC_WATCHDOG_SECS * HZ;

	/* set times to ensure the first check will proceed */
	atomic_long_set(&idev->last_check_time, jiffies - 2 * HZ);
	idev->last_hb_time = jiffies - 2 * ionic->watchdog_period;
	/* init as ready, so no transition if the first check succeeds */
	idev->last_fw_hb = 0;
	idev->fw_hb_ready = true;
	idev->fw_status_ready = true;
	idev->fw_generation = IONIC_FW_STS_F_GENERATION &
			      ioread8(&idev->dev_info_regs->fw_status);
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
	int size;
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

	idev->hwstamp_regs = &idev->dev_info_regs->hwstamp;

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

	ionic_watchdog_init(ionic);

	idev->db_pages = bar->vaddr;
	idev->phy_db_pages = bar->bus_addr;

	/* BAR2: optional controller memory mapping */
	bar++;
	mutex_init(&idev->cmb_inuse_lock);
	if (num_bars < 3 || !ionic->bars[IONIC_PCI_BAR_CMB].len) {
		idev->cmb_inuse = NULL;
		return 0;
	}

	idev->phy_cmb_pages = bar->bus_addr;
	idev->cmb_npages = bar->len / PAGE_SIZE;
	size = BITS_TO_LONGS(idev->cmb_npages) * sizeof(long);
	idev->cmb_inuse = kzalloc(size, GFP_KERNEL);
	if (!idev->cmb_inuse)
		dev_warn(dev, "No memory for CMB, disabling\n");

	return 0;
}

void ionic_dev_teardown(struct ionic *ionic)
{
	struct ionic_dev *idev = &ionic->idev;

	kfree(idev->cmb_inuse);
	idev->cmb_inuse = NULL;
	idev->phy_cmb_pages = 0;
	idev->cmb_npages = 0;

	mutex_destroy(&idev->cmb_inuse_lock);
}

/* Devcmd Interface */
bool ionic_is_fw_running(struct ionic_dev *idev)
{
	u8 fw_status = ioread8(&idev->dev_info_regs->fw_status);

	/* firmware is useful only if the running bit is set and
	 * fw_status != 0xff (bad PCI read)
	 */
	return (fw_status != 0xff) && (fw_status & IONIC_FW_STS_F_RUNNING);
}

int ionic_heartbeat_check(struct ionic *ionic)
{
	unsigned long check_time, last_check_time;
	struct ionic_dev *idev = &ionic->idev;
	struct ionic_lif *lif = ionic->lif;
	bool fw_status_ready = true;
	bool fw_hb_ready;
	u8 fw_generation;
	u8 fw_status;
	u32 fw_hb;

	/* wait a least one second before testing again */
	check_time = jiffies;
	last_check_time = atomic_long_read(&idev->last_check_time);
do_check_time:
	if (time_before(check_time, last_check_time + HZ))
		return 0;
	if (!atomic_long_try_cmpxchg_relaxed(&idev->last_check_time,
					     &last_check_time, check_time)) {
		/* if called concurrently, only the first should proceed. */
		dev_dbg(ionic->dev, "%s: do_check_time again\n", __func__);
		goto do_check_time;
	}

	fw_status = ioread8(&idev->dev_info_regs->fw_status);

	/* If fw_status is not ready don't bother with the generation */
	if (!ionic_is_fw_running(idev)) {
		fw_status_ready = false;
	} else {
		fw_generation = fw_status & IONIC_FW_STS_F_GENERATION;
		if (idev->fw_generation != fw_generation) {
			dev_info(ionic->dev, "FW generation 0x%02x -> 0x%02x\n",
				 idev->fw_generation, fw_generation);

			idev->fw_generation = fw_generation;

			/* If the generation changed, the fw status is not
			 * ready so we need to trigger a fw-down cycle.  After
			 * the down, the next watchdog will see the fw is up
			 * and the generation value stable, so will trigger
			 * the fw-up activity.
			 *
			 * If we had already moved to FW_RESET from a RESET event,
			 * it is possible that we never saw the fw_status go to 0,
			 * so we fake the current idev->fw_status_ready here to
			 * force the transition and get FW up again.
			 */
			if (test_bit(IONIC_LIF_F_FW_RESET, lif->state))
				idev->fw_status_ready = false;	/* go to running */
			else
				fw_status_ready = false;	/* go to down */
		}
	}

	dev_dbg(ionic->dev, "fw_status 0x%02x ready %d idev->ready %d last_hb 0x%x state 0x%02lx\n",
		fw_status, fw_status_ready, idev->fw_status_ready,
		idev->last_fw_hb, lif->state[0]);

	/* is this a transition? */
	if (fw_status_ready != idev->fw_status_ready &&
	    !test_bit(IONIC_LIF_F_FW_STOPPING, lif->state)) {
		bool trigger = false;

		idev->fw_status_ready = fw_status_ready;

		if (!fw_status_ready &&
		    !test_bit(IONIC_LIF_F_FW_RESET, lif->state) &&
		    !test_and_set_bit(IONIC_LIF_F_FW_STOPPING, lif->state)) {
			dev_info(ionic->dev, "FW stopped 0x%02x\n", fw_status);
			trigger = true;

		} else if (fw_status_ready &&
			   test_bit(IONIC_LIF_F_FW_RESET, lif->state)) {
			dev_info(ionic->dev, "FW running 0x%02x\n", fw_status);
			trigger = true;
		}

		if (trigger) {
			struct ionic_deferred_work *work;

			work = kzalloc(sizeof(*work), GFP_ATOMIC);
			if (work) {
				work->type = IONIC_DW_TYPE_LIF_RESET;
				work->fw_status = fw_status_ready;
				ionic_lif_deferred_enqueue(&lif->deferred, work);
			}
		}
	}

	if (!idev->fw_status_ready)
		return -ENXIO;

	/* Because of some variability in the actual FW heartbeat, we
	 * wait longer than the DEVCMD_TIMEOUT before checking again.
	 */
	last_check_time = idev->last_hb_time;
	if (time_before(check_time, last_check_time + DEVCMD_TIMEOUT * 2 * HZ))
		return 0;

	fw_hb = ioread32(&idev->dev_info_regs->fw_heartbeat);
	fw_hb_ready = fw_hb != idev->last_fw_hb;

	/* early FW version had no heartbeat, so fake it */
	if (!fw_hb_ready && !fw_hb)
		fw_hb_ready = true;

	dev_dbg(ionic->dev, "%s: fw_hb %u last_fw_hb %u ready %u\n",
		__func__, fw_hb, idev->last_fw_hb, fw_hb_ready);

	idev->last_fw_hb = fw_hb;

	/* log a transition */
	if (fw_hb_ready != idev->fw_hb_ready) {
		idev->fw_hb_ready = fw_hb_ready;
		if (!fw_hb_ready)
			dev_info(ionic->dev, "FW heartbeat stalled at %d\n", fw_hb);
		else
			dev_info(ionic->dev, "FW heartbeat restored at %d\n", fw_hb);
	}

	if (!fw_hb_ready)
		return -ENXIO;

	idev->last_hb_time = check_time;

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
int ionic_set_vf_config(struct ionic *ionic, int vf,
			struct ionic_vf_setattr_cmd *vfc)
{
	union ionic_dev_cmd cmd = {
		.vf_setattr.opcode = IONIC_CMD_VF_SETATTR,
		.vf_setattr.attr = vfc->attr,
		.vf_setattr.vf_index = cpu_to_le16(vf),
	};
	int err;

	memcpy(cmd.vf_setattr.pad, vfc->pad, sizeof(vfc->pad));

	mutex_lock(&ionic->dev_cmd_lock);
	ionic_dev_cmd_go(&ionic->idev, &cmd);
	err = ionic_dev_cmd_wait(ionic, DEVCMD_TIMEOUT);
	mutex_unlock(&ionic->dev_cmd_lock);

	return err;
}

int ionic_dev_cmd_vf_getattr(struct ionic *ionic, int vf, u8 attr,
			     struct ionic_vf_getattr_comp *comp)
{
	union ionic_dev_cmd cmd = {
		.vf_getattr.opcode = IONIC_CMD_VF_GETATTR,
		.vf_getattr.attr = attr,
		.vf_getattr.vf_index = cpu_to_le16(vf),
	};
	int err;

	if (vf >= ionic->num_vfs)
		return -EINVAL;

	switch (attr) {
	case IONIC_VF_ATTR_SPOOFCHK:
	case IONIC_VF_ATTR_TRUST:
	case IONIC_VF_ATTR_LINKSTATE:
	case IONIC_VF_ATTR_MAC:
	case IONIC_VF_ATTR_VLAN:
	case IONIC_VF_ATTR_RATE:
		break;
	case IONIC_VF_ATTR_STATSADDR:
	default:
		return -EINVAL;
	}

	mutex_lock(&ionic->dev_cmd_lock);
	ionic_dev_cmd_go(&ionic->idev, &cmd);
	err = ionic_dev_cmd_wait_nomsg(ionic, DEVCMD_TIMEOUT);
	memcpy_fromio(comp, &ionic->idev.dev_cmd_regs->comp.vf_getattr,
		      sizeof(*comp));
	mutex_unlock(&ionic->dev_cmd_lock);

	if (err && comp->status != IONIC_RC_ENOSUPP)
		ionic_dev_cmd_dev_err_print(ionic, cmd.vf_getattr.opcode,
					    comp->status, err);

	return err;
}

void ionic_vf_start(struct ionic *ionic)
{
	union ionic_dev_cmd cmd = {
		.vf_ctrl.opcode = IONIC_CMD_VF_CTRL,
		.vf_ctrl.ctrl_opcode = IONIC_VF_CTRL_START_ALL,
	};

	if (!(ionic->ident.dev.capabilities & cpu_to_le64(IONIC_DEV_CAP_VF_CTRL)))
		return;

	ionic_dev_cmd_go(&ionic->idev, &cmd);
	ionic_dev_cmd_wait(ionic, DEVCMD_TIMEOUT);
}

/* LIF commands */
void ionic_dev_cmd_queue_identify(struct ionic_dev *idev,
				  u16 lif_type, u8 qtype, u8 qver)
{
	union ionic_dev_cmd cmd = {
		.q_identify.opcode = IONIC_CMD_Q_IDENTIFY,
		.q_identify.lif_type = cpu_to_le16(lif_type),
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

int ionic_get_cmb(struct ionic_lif *lif, u32 *pgid, phys_addr_t *pgaddr, int order)
{
	struct ionic_dev *idev = &lif->ionic->idev;
	int ret;

	mutex_lock(&idev->cmb_inuse_lock);
	ret = bitmap_find_free_region(idev->cmb_inuse, idev->cmb_npages, order);
	mutex_unlock(&idev->cmb_inuse_lock);

	if (ret < 0)
		return ret;

	*pgid = ret;
	*pgaddr = idev->phy_cmb_pages + ret * PAGE_SIZE;

	return 0;
}

void ionic_put_cmb(struct ionic_lif *lif, u32 pgid, int order)
{
	struct ionic_dev *idev = &lif->ionic->idev;

	mutex_lock(&idev->cmb_inuse_lock);
	bitmap_release_region(idev->cmb_inuse, pgid, order);
	mutex_unlock(&idev->cmb_inuse_lock);
}

int ionic_cq_init(struct ionic_lif *lif, struct ionic_cq *cq,
		  struct ionic_intr_info *intr,
		  unsigned int num_descs, size_t desc_size)
{
	unsigned int ring_size;

	if (desc_size == 0 || !is_power_of_2(num_descs))
		return -EINVAL;

	ring_size = ilog2(num_descs);
	if (ring_size < 2 || ring_size > 16)
		return -EINVAL;

	cq->lif = lif;
	cq->bound_intr = intr;
	cq->num_descs = num_descs;
	cq->desc_size = desc_size;
	cq->tail_idx = 0;
	cq->done_color = 1;

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
	struct ionic_cq_info *cq_info;
	unsigned int work_done = 0;

	if (work_to_do == 0)
		return 0;

	cq_info = &cq->info[cq->tail_idx];
	while (cb(cq, cq_info)) {
		if (cq->tail_idx == cq->num_descs - 1)
			cq->done_color = !cq->done_color;
		cq->tail_idx = (cq->tail_idx + 1) & (cq->num_descs - 1);
		cq_info = &cq->info[cq->tail_idx];

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
	unsigned int ring_size;

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
	q->tail_idx = 0;
	q->head_idx = 0;
	q->pid = pid;

	snprintf(q->name, sizeof(q->name), "L%d-%s%u", lif->index, name, index);

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

void ionic_q_cmb_map(struct ionic_queue *q, void __iomem *base, dma_addr_t base_pa)
{
	struct ionic_desc_info *cur;
	unsigned int i;

	q->cmb_base = base;
	q->cmb_base_pa = base_pa;

	for (i = 0, cur = q->info; i < q->num_descs; i++, cur++)
		cur->cmb_desc = base + (i * q->desc_size);
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
	struct ionic_desc_info *desc_info;
	struct ionic_lif *lif = q->lif;
	struct device *dev = q->dev;

	desc_info = &q->info[q->head_idx];
	desc_info->cb = cb;
	desc_info->cb_arg = cb_arg;

	q->head_idx = (q->head_idx + 1) & (q->num_descs - 1);

	dev_dbg(dev, "lif=%d qname=%s qid=%d qtype=%d p_index=%d ringdb=%d\n",
		q->lif->index, q->name, q->hw_type, q->hw_index,
		q->head_idx, ring_doorbell);

	if (ring_doorbell) {
		ionic_dbell_ring(lif->kern_dbpage, q->hw_type,
				 q->dbval | q->head_idx);

		q->dbell_jiffies = jiffies;

		if (q_to_qcq(q)->napi_qcq)
			mod_timer(&q_to_qcq(q)->napi_qcq->napi_deadline,
				  jiffies + IONIC_NAPI_DEADLINE);
	}
}

static bool ionic_q_is_posted(struct ionic_queue *q, unsigned int pos)
{
	unsigned int mask, tail, head;

	mask = q->num_descs - 1;
	tail = q->tail_idx;
	head = q->head_idx;

	return ((pos - tail) & mask) < ((head - tail) & mask);
}

void ionic_q_service(struct ionic_queue *q, struct ionic_cq_info *cq_info,
		     unsigned int stop_index)
{
	struct ionic_desc_info *desc_info;
	ionic_desc_cb cb;
	void *cb_arg;
	u16 index;

	/* check for empty queue */
	if (q->tail_idx == q->head_idx)
		return;

	/* stop index must be for a descriptor that is not yet completed */
	if (unlikely(!ionic_q_is_posted(q, stop_index)))
		dev_err(q->dev,
			"ionic stop is not posted %s stop %u tail %u head %u\n",
			q->name, stop_index, q->tail_idx, q->head_idx);

	do {
		desc_info = &q->info[q->tail_idx];
		index = q->tail_idx;
		q->tail_idx = (q->tail_idx + 1) & (q->num_descs - 1);

		cb = desc_info->cb;
		cb_arg = desc_info->cb_arg;

		desc_info->cb = NULL;
		desc_info->cb_arg = NULL;

		if (cb)
			cb(q, desc_info, cq_info, cb_arg);
	} while (index != stop_index);
}
