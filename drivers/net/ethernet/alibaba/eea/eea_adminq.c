// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for Alibaba Elastic Ethernet Adapter.
 *
 * Copyright (C) 2025 Alibaba Inc.
 */

#include <linux/etherdevice.h>
#include <linux/iopoll.h>
#include <linux/utsname.h>
#include <linux/version.h>

#include "eea_adminq.h"
#include "eea_net.h"
#include "eea_pci.h"
#include "eea_ring.h"

#define EEA_AQ_CMD_CFG_QUERY         ((0 << 8) | 0)

#define EEA_AQ_CMD_QUEUE_CREATE      ((1 << 8) | 0)
#define EEA_AQ_CMD_QUEUE_DESTROY_ALL ((1 << 8) | 1)

#define EEA_AQ_CMD_HOST_INFO         ((2 << 8) | 0)

#define EEA_AQ_CMD_DEV_STATUS        ((3 << 8) | 0)

#define EEA_RING_DESC_F_AQ_PHASE     (BIT(15) | BIT(7))

#define EEA_QUEUE_FLAGS_HW_SPLIT_HDR BIT(0)
#define EEA_QUEUE_FLAGS_SQCQ         BIT(1)
#define EEA_QUEUE_FLAGS_HWTS         BIT(2)

struct eea_aq_create {
	__le32 flags;
	/* queue index.
	 * rx: 0 == qidx % 2
	 * tx: 1 == qidx % 2
	 */
	__le16 qidx;
	/* the depth of the queue */
	__le16 depth;
	/*  0: without SPLIT HDR
	 *  1: 128B
	 *  2: 256B
	 *  3: 512B
	 */
	u8 hdr_buf_size;
	u8 sq_desc_size;
	u8 cq_desc_size;
	u8 reserve0;
	/* The vector for the irq. rx,tx share the same vector */
	__le16 msix_vector;
	__le16 reserve;
	/* sq ring cfg. */
	__le32 sq_addr_low;
	__le32 sq_addr_high;
	/* cq ring cfg. Just valid when flags include EEA_QUEUE_FLAGS_SQCQ. */
	__le32 cq_addr_low;
	__le32 cq_addr_high;
};

struct eea_aq_queue_drv_status {
	__le16 qidx;

	__le16 sq_head;
	__le16 cq_head;
	__le16 reserved;
};

#define EEA_OS_DISTRO		0
#define EEA_DRV_TYPE		0
#define EEA_OS_LINUX		1
#define EEA_SPEC_VER_MAJOR	1
#define EEA_SPEC_VER_MINOR	0

struct eea_aq_host_info_cfg {
	__le16	os_type;
	__le16	os_dist;
	__le16	drv_type;

	__le16	kern_ver_major;
	__le16	kern_ver_minor;
	__le16	kern_ver_sub_minor;

	__le16	drv_ver_major;
	__le16	drv_ver_minor;
	__le16	drv_ver_sub_minor;

	__le16	spec_ver_major;
	__le16	spec_ver_minor;
	__le16	pci_bdf;
	__le32	pci_domain;

	u8      os_ver_str[64];
	u8      isa_str[64];
};

#define EEA_HINFO_MAX_REP_LEN	1024
#define EEA_HINFO_REP_BAD	2

struct eea_aq_host_info_rep {
	u8	op_code;
	u8	has_reply;
	u8	reply_str[EEA_HINFO_MAX_REP_LEN];
};

static struct eea_ring *qid_to_ering(struct eea_net *enet, u32 qid)
{
	struct eea_ring *ering;

	if (qid % 2 == 0)
		ering = enet->rx[qid / 2]->ering;
	else
		ering = enet->tx[qid / 2].ering;

	return ering;
}

#define EEA_AQ_TIMEOUT_US (60 * 1000 * 1000)

static void eea_device_broken(struct eea_net *enet)
{
	if (enet->adminq.broken)
		return;

	eea_device_reset(enet->edev);
	enet->adminq.broken = true;
}

static int eea_adminq_submit(struct eea_net *enet, u16 cmd,
			     dma_addr_t req_addr, dma_addr_t res_addr,
			     u32 req_size, u32 res_size, u32 *reply_len)
{
	struct eea_aq_cdesc *cdesc;
	struct eea_aq_desc *desc;
	int ret;

	if (enet->adminq.broken)
		return -EIO;

	desc = eea_ering_aq_alloc_desc(enet->adminq.ring);

	desc->classid = cmd >> 8;
	desc->command = cmd & 0xff;

	desc->data_addr = cpu_to_le64(req_addr);
	desc->data_len = cpu_to_le32(req_size);

	desc->reply_addr = cpu_to_le64(res_addr);
	desc->reply_len = cpu_to_le32(res_size);

	/* for update flags */
	dma_wmb();

	desc->flags = cpu_to_le16(enet->adminq.phase);

	eea_ering_sq_commit_desc(enet->adminq.ring);

	eea_ering_kick(enet->adminq.ring);

	++enet->adminq.num;

	if ((enet->adminq.num % enet->adminq.ring->num) == 0)
		enet->adminq.phase ^= EEA_RING_DESC_F_AQ_PHASE;

	ret = read_poll_timeout(eea_ering_cq_get_desc, cdesc, cdesc, 10,
				EEA_AQ_TIMEOUT_US, false, enet->adminq.ring);
	if (ret) {
		netdev_err(enet->netdev,
			   "adminq exec timeout. cmd: %d reset device.\n",
			   cmd);
		/* The device must be reset before unmapping buffers to avoid
		 * potential DMA writes after the memory is freed.
		 */
		eea_device_broken(enet);
		return ret;
	}

	/* Returns 0 on success, or a negative error code on failure. */
	ret = le32_to_cpu(cdesc->status);

	eea_ering_cq_ack_desc(enet->adminq.ring, 1);

	if (ret)
		netdev_err(enet->netdev,
			   "adminq exec failed. cmd: %d ret %d\n", cmd, ret);
	else
		*reply_len = le32_to_cpu(cdesc->reply_len);

	return ret;
}

static int eea_adminq_exec(struct eea_net *enet, u16 cmd,
			   void *req, u32 req_size,
			   void *res, u32 res_size,
			   u32 *reply)
{
	dma_addr_t req_addr = 0, res_addr = 0;
	struct device *dma;
	u32 reply_len = 0;
	int ret;

	if (reply)
		*reply = 0;

	dma = enet->edev->dma_dev;

	if (req) {
		req_addr = dma_map_single(dma, req, req_size, DMA_TO_DEVICE);
		if (unlikely(dma_mapping_error(dma, req_addr)))
			return -ENOMEM;
	}

	if (res) {
		res_addr = dma_map_single(dma, res, res_size, DMA_FROM_DEVICE);
		if (unlikely(dma_mapping_error(dma, res_addr))) {
			ret = -ENOMEM;
			goto err_unmap_req;
		}
	}

	mutex_lock(&enet->adminq.lock);
	ret = eea_adminq_submit(enet, cmd, req_addr, res_addr,
				req_size, res_size, &reply_len);
	mutex_unlock(&enet->adminq.lock);
	if (res) {
		dma_unmap_single(dma, res_addr, res_size, DMA_FROM_DEVICE);

		if (ret)
			memset(res, 0, res_size);
		else if (res_size > reply_len)
			memset(res + reply_len, 0, res_size - reply_len);

		if (reply)
			*reply = reply_len;
	}

err_unmap_req:
	if (req)
		dma_unmap_single(dma, req_addr, req_size, DMA_TO_DEVICE);

	return ret;
}

void eea_destroy_adminq(struct eea_net *enet)
{
	struct eea_aq *aq;

	aq = &enet->adminq;

	if (aq->ring) {
		eea_ering_free(aq->ring);
		aq->ring = NULL;
		aq->phase = 0;
	}

	kfree(aq->q_req_buf);
	kfree(aq->q_res_buf);

	aq->q_req_buf = NULL;
	aq->q_res_buf = NULL;
}

int eea_create_adminq(struct eea_net *enet, u32 qid)
{
	u32 db_size, q_size, num;
	struct eea_ring *ering;
	struct eea_aq *aq;
	int err = -ENOMEM;

	num = enet->edev->rx_num + enet->edev->tx_num;
	aq = &enet->adminq;

	ering = eea_ering_alloc(qid, 64, enet->edev, sizeof(struct eea_aq_desc),
				sizeof(struct eea_aq_cdesc), "adminq");
	if (!ering)
		return -ENOMEM;

	aq->ring = ering;

	err = eea_pci_active_aq(ering, qid / 2 + 1);
	if (err)
		goto err;

	aq->phase = BIT(7);
	aq->num = 0;

	q_size = sizeof(*aq->q_req_buf) * num;
	db_size = sizeof(*aq->q_res_buf) * num;

	aq->q_req_size = q_size;
	aq->q_res_size = db_size;

	err = -ENOMEM;

	aq->q_req_buf = kzalloc(q_size, GFP_KERNEL);
	if (!aq->q_req_buf)
		goto err;

	aq->q_res_buf = kzalloc(db_size, GFP_KERNEL);
	if (!aq->q_res_buf)
		goto err;

	/* Before we set up the AQ, the device remains in an inactive state, so
	 * there will be no DMA operations. If the 'set up AQ' process fails, we
	 * can safely free the DMA-related memory.
	 */
	err = eea_pci_set_aq_up(enet->edev);
	if (err)
		goto err;

	aq->broken = false;

	mutex_init(&aq->lock);

	return 0;

err:
	eea_destroy_adminq(enet);
	return err;
}

int eea_adminq_query_cfg(struct eea_net *enet, struct eea_aq_cfg *cfg)
{
	return eea_adminq_exec(enet, EEA_AQ_CMD_CFG_QUERY, NULL, 0, cfg,
			       sizeof(*cfg), NULL);
}

static void qcfg_fill(struct eea_aq_create *qcfg, struct eea_ring *ering,
		      u32 flags)
{
	qcfg->flags = cpu_to_le32(flags);
	qcfg->qidx = cpu_to_le16(ering->index);
	qcfg->depth = cpu_to_le16(ering->num);

	qcfg->hdr_buf_size = flags & EEA_QUEUE_FLAGS_HW_SPLIT_HDR ? 1 : 0;
	qcfg->sq_desc_size = ering->sq.desc_size;
	qcfg->cq_desc_size = ering->cq.desc_size;
	qcfg->msix_vector = cpu_to_le16(ering->msix_vec);

	qcfg->sq_addr_low = cpu_to_le32(lower_32_bits(ering->sq.dma_addr));
	qcfg->sq_addr_high = cpu_to_le32(upper_32_bits(ering->sq.dma_addr));

	qcfg->cq_addr_low = cpu_to_le32(lower_32_bits(ering->cq.dma_addr));
	qcfg->cq_addr_high = cpu_to_le32(upper_32_bits(ering->cq.dma_addr));
}

int eea_adminq_create_q(struct eea_net *enet, u32 num, u32 flags)
{
	int i, db_size, q_size, err = -ENOMEM;
	struct eea_net_cfg *cfg;
	struct eea_ring *ering;
	struct eea_aq *aq;
	u32 reply_len;

	cfg = &enet->cfg;
	aq = &enet->adminq;

	if (cfg->split_hdr)
		flags |= EEA_QUEUE_FLAGS_HW_SPLIT_HDR;

	flags |= EEA_QUEUE_FLAGS_SQCQ;
	flags |= EEA_QUEUE_FLAGS_HWTS;

	q_size = sizeof(*aq->q_req_buf) * num;
	db_size = sizeof(*aq->q_res_buf) * num;

	for (i = 0; i < num; i++) {
		ering = qid_to_ering(enet, i);
		qcfg_fill(aq->q_req_buf + i, ering, flags);
	}

	err = eea_adminq_exec(enet, EEA_AQ_CMD_QUEUE_CREATE,
			      aq->q_req_buf, q_size,
			      aq->q_res_buf, db_size,
			      &reply_len);
	if (err)
		return err;

	if (reply_len != db_size) {
		eea_adminq_destroy_all_q(enet);
		netdev_err(enet->netdev, "invalid reply len %u\n", reply_len);
		return -EINVAL;
	}

	for (i = 0; i < num; i++) {
		ering = qid_to_ering(enet, i);
		ering->db = eea_pci_db_addr(ering->edev,
					    le32_to_cpu(aq->q_res_buf[i]));
		if (!ering->db) {
			netdev_err(enet->netdev, "invalid db off %u\n",
				   le32_to_cpu(aq->q_res_buf[i]));
			goto err;
		}
	}

	return err;

err:
	eea_adminq_destroy_all_q(enet);
	for (i = 0; i < num; i++) {
		ering = qid_to_ering(enet, i);
		ering->db = NULL;
	}

	return -EIO;
}

int eea_adminq_destroy_all_q(struct eea_net *enet)
{
	int err;

	err = eea_adminq_exec(enet, EEA_AQ_CMD_QUEUE_DESTROY_ALL, NULL, 0,
			      NULL, 0, NULL);
	if (err) {
		/* The device must be reset before unmapping buffers to avoid
		 * potential DMA writes after the memory is freed.
		 */
		mutex_lock(&enet->adminq.lock);
		eea_device_broken(enet);
		mutex_unlock(&enet->adminq.lock);

		netdev_err(enet->netdev, "QUEUE_DESTROY fail: reset device.\n");
	}

	return err;
}

/* The caller must ensure that both the 'rx' and 'tx' arrays are valid. */
int eea_adminq_dev_status(struct eea_net *enet,
			  struct eea_aq_dev_status *dstatus)
{
	struct eea_aq_queue_drv_status *drv_status;
	struct __eea_aq_dev_status *dev_status;
	int err, i, io_num, size, q_num;
	struct eea_ring *ering;
	void *rep, *req;

	q_num = enet->cfg.rx_ring_num + enet->cfg.tx_ring_num + 1;
	io_num = enet->cfg.rx_ring_num + enet->cfg.tx_ring_num;

	req = kcalloc(q_num, sizeof(struct eea_aq_queue_drv_status),
		      GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	size = struct_size(dev_status, q_status, q_num);

	rep = kzalloc(size, GFP_KERNEL);
	if (!rep) {
		kfree(req);
		return -ENOMEM;
	}

	drv_status = req;
	for (i = 0; i < io_num; ++i, ++drv_status) {
		ering = qid_to_ering(enet, i);
		drv_status->qidx = cpu_to_le16(i);
		drv_status->cq_head = cpu_to_le16(ering->cq.head);
		drv_status->sq_head = cpu_to_le16(ering->sq.head);
	}

	drv_status->qidx = cpu_to_le16(i);
	drv_status->cq_head = cpu_to_le16(enet->adminq.ring->cq.head);
	drv_status->sq_head = cpu_to_le16(enet->adminq.ring->sq.head);

	err = eea_adminq_exec(enet, EEA_AQ_CMD_DEV_STATUS, req,
			      q_num * sizeof(struct eea_aq_queue_drv_status),
			      rep, size, NULL);
	kfree(req);
	if (err) {
		kfree(rep);
		return err;
	}

	dstatus->num = q_num;
	dstatus->status = rep;

	return 0;
}

void eea_adminq_config_host_info(struct eea_net *enet)
{
	struct device *dev = enet->edev->dma_dev;
	struct eea_aq_host_info_cfg *cfg;
	struct eea_aq_host_info_rep *rep;
	int rc = -ENOMEM;

	cfg = kzalloc(sizeof(*cfg), GFP_KERNEL);
	if (!cfg)
		return;

	rep = kzalloc(sizeof(*rep), GFP_KERNEL);
	if (!rep)
		goto err_free_cfg;

	cfg->os_type            = cpu_to_le16(EEA_OS_LINUX);
	cfg->os_dist            = cpu_to_le16(EEA_OS_DISTRO);
	cfg->drv_type           = cpu_to_le16(EEA_DRV_TYPE);

	cfg->kern_ver_major     = cpu_to_le16(LINUX_VERSION_MAJOR);
	cfg->kern_ver_minor     = cpu_to_le16(LINUX_VERSION_PATCHLEVEL);
	cfg->kern_ver_sub_minor = cpu_to_le16(LINUX_VERSION_SUBLEVEL);

	cfg->drv_ver_major      = cpu_to_le16(EEA_VER_MAJOR);
	cfg->drv_ver_minor      = cpu_to_le16(EEA_VER_MINOR);
	cfg->drv_ver_sub_minor  = cpu_to_le16(EEA_VER_SUB_MINOR);

	cfg->spec_ver_major     = cpu_to_le16(EEA_SPEC_VER_MAJOR);
	cfg->spec_ver_minor     = cpu_to_le16(EEA_SPEC_VER_MINOR);

	cfg->pci_bdf            = cpu_to_le16(eea_pci_bdf(enet->edev));
	cfg->pci_domain         = cpu_to_le32(eea_pci_domain_nr(enet->edev));

	strscpy(cfg->os_ver_str, utsname()->release, sizeof(cfg->os_ver_str));
	strscpy(cfg->isa_str, utsname()->machine, sizeof(cfg->isa_str));

	rc = eea_adminq_exec(enet, EEA_AQ_CMD_HOST_INFO,
			     cfg, sizeof(*cfg), rep, sizeof(*rep), NULL);

	if (!rc) {
		if (rep->op_code == EEA_HINFO_REP_BAD)
			dev_warn(dev, "The hardware-driven state validation may be abnormal.\n");

		if (rep->has_reply) {
			char buf[EEA_HINFO_MAX_REP_LEN] = {0};

			rep->reply_str[EEA_HINFO_MAX_REP_LEN - 1] = '\0';

			string_escape_str(rep->reply_str, buf, sizeof(buf),
					  ESCAPE_NP, NULL);

			buf[EEA_HINFO_MAX_REP_LEN - 1] = '\0';

			dev_warn(dev, "Device replied: %s\n", buf);
		}
	}

	kfree(rep);
err_free_cfg:
	kfree(cfg);
}
