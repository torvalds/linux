// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2019-2020 Marvell International Ltd. All rights reserved */

#include <linux/bitfield.h>
#include <linux/circ_buf.h>
#include <linux/device.h>
#include <linux/firmware.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>

#include "prestera.h"

#define PRESTERA_MSG_MAX_SIZE 1500

#define PRESTERA_SUPP_FW_MAJ_VER	3
#define PRESTERA_SUPP_FW_MIN_VER	0

#define PRESTERA_PREV_FW_MAJ_VER	2
#define PRESTERA_PREV_FW_MIN_VER	0

#define PRESTERA_FW_PATH_FMT	"mrvl/prestera/mvsw_prestera_fw-v%u.%u.img"

#define PRESTERA_FW_HDR_MAGIC		0x351D9D06
#define PRESTERA_FW_DL_TIMEOUT_MS	50000
#define PRESTERA_FW_BLK_SZ		1024

#define PRESTERA_FW_VER_MAJ_MUL 1000000
#define PRESTERA_FW_VER_MIN_MUL 1000

#define PRESTERA_FW_VER_MAJ(v)	((v) / PRESTERA_FW_VER_MAJ_MUL)

#define PRESTERA_FW_VER_MIN(v) \
	(((v) - (PRESTERA_FW_VER_MAJ(v) * PRESTERA_FW_VER_MAJ_MUL)) / \
			PRESTERA_FW_VER_MIN_MUL)

#define PRESTERA_FW_VER_PATCH(v) \
	((v) - (PRESTERA_FW_VER_MAJ(v) * PRESTERA_FW_VER_MAJ_MUL) - \
			(PRESTERA_FW_VER_MIN(v) * PRESTERA_FW_VER_MIN_MUL))

enum prestera_pci_bar_t {
	PRESTERA_PCI_BAR_FW = 2,
	PRESTERA_PCI_BAR_PP = 4,
};

struct prestera_fw_header {
	__be32 magic_number;
	__be32 version_value;
	u8 reserved[8];
};

struct prestera_ldr_regs {
	u32 ldr_ready;
	u32 pad1;

	u32 ldr_img_size;
	u32 ldr_ctl_flags;

	u32 ldr_buf_offs;
	u32 ldr_buf_size;

	u32 ldr_buf_rd;
	u32 pad2;
	u32 ldr_buf_wr;

	u32 ldr_status;
};

#define PRESTERA_LDR_REG_OFFSET(f)	offsetof(struct prestera_ldr_regs, f)

#define PRESTERA_LDR_READY_MAGIC	0xf00dfeed

#define PRESTERA_LDR_STATUS_IMG_DL	BIT(0)
#define PRESTERA_LDR_STATUS_START_FW	BIT(1)
#define PRESTERA_LDR_STATUS_INVALID_IMG	BIT(2)
#define PRESTERA_LDR_STATUS_NOMEM	BIT(3)

#define PRESTERA_LDR_REG_BASE(fw)	((fw)->ldr_regs)
#define PRESTERA_LDR_REG_ADDR(fw, reg)	(PRESTERA_LDR_REG_BASE(fw) + (reg))

/* fw loader registers */
#define PRESTERA_LDR_READY_REG		PRESTERA_LDR_REG_OFFSET(ldr_ready)
#define PRESTERA_LDR_IMG_SIZE_REG	PRESTERA_LDR_REG_OFFSET(ldr_img_size)
#define PRESTERA_LDR_CTL_REG		PRESTERA_LDR_REG_OFFSET(ldr_ctl_flags)
#define PRESTERA_LDR_BUF_SIZE_REG	PRESTERA_LDR_REG_OFFSET(ldr_buf_size)
#define PRESTERA_LDR_BUF_OFFS_REG	PRESTERA_LDR_REG_OFFSET(ldr_buf_offs)
#define PRESTERA_LDR_BUF_RD_REG		PRESTERA_LDR_REG_OFFSET(ldr_buf_rd)
#define PRESTERA_LDR_BUF_WR_REG		PRESTERA_LDR_REG_OFFSET(ldr_buf_wr)
#define PRESTERA_LDR_STATUS_REG		PRESTERA_LDR_REG_OFFSET(ldr_status)

#define PRESTERA_LDR_CTL_DL_START	BIT(0)

#define PRESTERA_EVT_QNUM_MAX	4

struct prestera_fw_evtq_regs {
	u32 rd_idx;
	u32 pad1;
	u32 wr_idx;
	u32 pad2;
	u32 offs;
	u32 len;
};

struct prestera_fw_regs {
	u32 fw_ready;
	u32 pad;
	u32 cmd_offs;
	u32 cmd_len;
	u32 evt_offs;
	u32 evt_qnum;

	u32 cmd_req_ctl;
	u32 cmd_req_len;
	u32 cmd_rcv_ctl;
	u32 cmd_rcv_len;

	u32 fw_status;
	u32 rx_status;

	struct prestera_fw_evtq_regs evtq_list[PRESTERA_EVT_QNUM_MAX];
};

#define PRESTERA_FW_REG_OFFSET(f)	offsetof(struct prestera_fw_regs, f)

#define PRESTERA_FW_READY_MAGIC		0xcafebabe

/* fw registers */
#define PRESTERA_FW_READY_REG		PRESTERA_FW_REG_OFFSET(fw_ready)

#define PRESTERA_CMD_BUF_OFFS_REG	PRESTERA_FW_REG_OFFSET(cmd_offs)
#define PRESTERA_CMD_BUF_LEN_REG	PRESTERA_FW_REG_OFFSET(cmd_len)
#define PRESTERA_EVT_BUF_OFFS_REG	PRESTERA_FW_REG_OFFSET(evt_offs)
#define PRESTERA_EVT_QNUM_REG		PRESTERA_FW_REG_OFFSET(evt_qnum)

#define PRESTERA_CMD_REQ_CTL_REG	PRESTERA_FW_REG_OFFSET(cmd_req_ctl)
#define PRESTERA_CMD_REQ_LEN_REG	PRESTERA_FW_REG_OFFSET(cmd_req_len)

#define PRESTERA_CMD_RCV_CTL_REG	PRESTERA_FW_REG_OFFSET(cmd_rcv_ctl)
#define PRESTERA_CMD_RCV_LEN_REG	PRESTERA_FW_REG_OFFSET(cmd_rcv_len)
#define PRESTERA_FW_STATUS_REG		PRESTERA_FW_REG_OFFSET(fw_status)
#define PRESTERA_RX_STATUS_REG		PRESTERA_FW_REG_OFFSET(rx_status)

/* PRESTERA_CMD_REQ_CTL_REG flags */
#define PRESTERA_CMD_F_REQ_SENT		BIT(0)
#define PRESTERA_CMD_F_REPL_RCVD	BIT(1)

/* PRESTERA_CMD_RCV_CTL_REG flags */
#define PRESTERA_CMD_F_REPL_SENT	BIT(0)

#define PRESTERA_FW_EVT_CTL_STATUS_MASK	GENMASK(1, 0)

#define PRESTERA_FW_EVT_CTL_STATUS_ON	0
#define PRESTERA_FW_EVT_CTL_STATUS_OFF	1

#define PRESTERA_EVTQ_REG_OFFSET(q, f)			\
	(PRESTERA_FW_REG_OFFSET(evtq_list) +		\
	 (q) * sizeof(struct prestera_fw_evtq_regs) +	\
	 offsetof(struct prestera_fw_evtq_regs, f))

#define PRESTERA_EVTQ_RD_IDX_REG(q)	PRESTERA_EVTQ_REG_OFFSET(q, rd_idx)
#define PRESTERA_EVTQ_WR_IDX_REG(q)	PRESTERA_EVTQ_REG_OFFSET(q, wr_idx)
#define PRESTERA_EVTQ_OFFS_REG(q)	PRESTERA_EVTQ_REG_OFFSET(q, offs)
#define PRESTERA_EVTQ_LEN_REG(q)	PRESTERA_EVTQ_REG_OFFSET(q, len)

#define PRESTERA_FW_REG_BASE(fw)	((fw)->dev.ctl_regs)
#define PRESTERA_FW_REG_ADDR(fw, reg)	PRESTERA_FW_REG_BASE((fw)) + (reg)

#define PRESTERA_FW_CMD_DEFAULT_WAIT_MS	30000
#define PRESTERA_FW_READY_WAIT_MS	20000

struct prestera_fw_evtq {
	u8 __iomem *addr;
	size_t len;
};

struct prestera_fw {
	struct prestera_fw_rev rev_supp;
	const struct firmware *bin;
	struct workqueue_struct *wq;
	struct prestera_device dev;
	u8 __iomem *ldr_regs;
	u8 __iomem *ldr_ring_buf;
	u32 ldr_buf_len;
	u32 ldr_wr_idx;
	struct mutex cmd_mtx; /* serialize access to dev->send_req */
	size_t cmd_mbox_len;
	u8 __iomem *cmd_mbox;
	struct prestera_fw_evtq evt_queue[PRESTERA_EVT_QNUM_MAX];
	u8 evt_qnum;
	struct work_struct evt_work;
	u8 __iomem *evt_buf;
	u8 *evt_msg;
};

static int prestera_fw_load(struct prestera_fw *fw);

static void prestera_fw_write(struct prestera_fw *fw, u32 reg, u32 val)
{
	writel(val, PRESTERA_FW_REG_ADDR(fw, reg));
}

static u32 prestera_fw_read(struct prestera_fw *fw, u32 reg)
{
	return readl(PRESTERA_FW_REG_ADDR(fw, reg));
}

static u32 prestera_fw_evtq_len(struct prestera_fw *fw, u8 qid)
{
	return fw->evt_queue[qid].len;
}

static u32 prestera_fw_evtq_avail(struct prestera_fw *fw, u8 qid)
{
	u32 wr_idx = prestera_fw_read(fw, PRESTERA_EVTQ_WR_IDX_REG(qid));
	u32 rd_idx = prestera_fw_read(fw, PRESTERA_EVTQ_RD_IDX_REG(qid));

	return CIRC_CNT(wr_idx, rd_idx, prestera_fw_evtq_len(fw, qid));
}

static void prestera_fw_evtq_rd_set(struct prestera_fw *fw,
				    u8 qid, u32 idx)
{
	u32 rd_idx = idx & (prestera_fw_evtq_len(fw, qid) - 1);

	prestera_fw_write(fw, PRESTERA_EVTQ_RD_IDX_REG(qid), rd_idx);
}

static u8 __iomem *prestera_fw_evtq_buf(struct prestera_fw *fw, u8 qid)
{
	return fw->evt_queue[qid].addr;
}

static u32 prestera_fw_evtq_read32(struct prestera_fw *fw, u8 qid)
{
	u32 rd_idx = prestera_fw_read(fw, PRESTERA_EVTQ_RD_IDX_REG(qid));
	u32 val;

	val = readl(prestera_fw_evtq_buf(fw, qid) + rd_idx);
	prestera_fw_evtq_rd_set(fw, qid, rd_idx + 4);
	return val;
}

static ssize_t prestera_fw_evtq_read_buf(struct prestera_fw *fw,
					 u8 qid, void *buf, size_t len)
{
	u32 idx = prestera_fw_read(fw, PRESTERA_EVTQ_RD_IDX_REG(qid));
	u8 __iomem *evtq_addr = prestera_fw_evtq_buf(fw, qid);
	u32 *buf32 = buf;
	int i;

	for (i = 0; i < len / 4; buf32++, i++) {
		*buf32 = readl_relaxed(evtq_addr + idx);
		idx = (idx + 4) & (prestera_fw_evtq_len(fw, qid) - 1);
	}

	prestera_fw_evtq_rd_set(fw, qid, idx);

	return i;
}

static u8 prestera_fw_evtq_pick(struct prestera_fw *fw)
{
	int qid;

	for (qid = 0; qid < fw->evt_qnum; qid++) {
		if (prestera_fw_evtq_avail(fw, qid) >= 4)
			return qid;
	}

	return PRESTERA_EVT_QNUM_MAX;
}

static void prestera_fw_evt_ctl_status_set(struct prestera_fw *fw, u32 val)
{
	u32 status = prestera_fw_read(fw, PRESTERA_FW_STATUS_REG);

	u32p_replace_bits(&status, val, PRESTERA_FW_EVT_CTL_STATUS_MASK);

	prestera_fw_write(fw, PRESTERA_FW_STATUS_REG, status);
}

static void prestera_fw_evt_work_fn(struct work_struct *work)
{
	struct prestera_fw *fw;
	void *msg;
	u8 qid;

	fw = container_of(work, struct prestera_fw, evt_work);
	msg = fw->evt_msg;

	prestera_fw_evt_ctl_status_set(fw, PRESTERA_FW_EVT_CTL_STATUS_OFF);

	while ((qid = prestera_fw_evtq_pick(fw)) < PRESTERA_EVT_QNUM_MAX) {
		u32 idx;
		u32 len;

		len = prestera_fw_evtq_read32(fw, qid);
		idx = prestera_fw_read(fw, PRESTERA_EVTQ_RD_IDX_REG(qid));

		WARN_ON(prestera_fw_evtq_avail(fw, qid) < len);

		if (WARN_ON(len > PRESTERA_MSG_MAX_SIZE)) {
			prestera_fw_evtq_rd_set(fw, qid, idx + len);
			continue;
		}

		prestera_fw_evtq_read_buf(fw, qid, msg, len);

		if (fw->dev.recv_msg)
			fw->dev.recv_msg(&fw->dev, msg, len);
	}

	prestera_fw_evt_ctl_status_set(fw, PRESTERA_FW_EVT_CTL_STATUS_ON);
}

static int prestera_fw_wait_reg32(struct prestera_fw *fw, u32 reg, u32 cmp,
				  unsigned int waitms)
{
	u8 __iomem *addr = PRESTERA_FW_REG_ADDR(fw, reg);
	u32 val;

	return readl_poll_timeout(addr, val, cmp == val,
				  1 * USEC_PER_MSEC, waitms * USEC_PER_MSEC);
}

static int prestera_fw_cmd_send(struct prestera_fw *fw,
				void *in_msg, size_t in_size,
				void *out_msg, size_t out_size,
				unsigned int waitms)
{
	u32 ret_size;
	int err;

	if (!waitms)
		waitms = PRESTERA_FW_CMD_DEFAULT_WAIT_MS;

	if (ALIGN(in_size, 4) > fw->cmd_mbox_len)
		return -EMSGSIZE;

	/* wait for finish previous reply from FW */
	err = prestera_fw_wait_reg32(fw, PRESTERA_CMD_RCV_CTL_REG, 0, 30);
	if (err) {
		dev_err(fw->dev.dev, "finish reply from FW is timed out\n");
		return err;
	}

	prestera_fw_write(fw, PRESTERA_CMD_REQ_LEN_REG, in_size);
	memcpy_toio(fw->cmd_mbox, in_msg, in_size);

	prestera_fw_write(fw, PRESTERA_CMD_REQ_CTL_REG, PRESTERA_CMD_F_REQ_SENT);

	/* wait for reply from FW */
	err = prestera_fw_wait_reg32(fw, PRESTERA_CMD_RCV_CTL_REG,
				     PRESTERA_CMD_F_REPL_SENT, waitms);
	if (err) {
		dev_err(fw->dev.dev, "reply from FW is timed out\n");
		goto cmd_exit;
	}

	ret_size = prestera_fw_read(fw, PRESTERA_CMD_RCV_LEN_REG);
	if (ret_size > out_size) {
		dev_err(fw->dev.dev, "ret_size (%u) > out_len(%zu)\n",
			ret_size, out_size);
		err = -EMSGSIZE;
		goto cmd_exit;
	}

	memcpy_fromio(out_msg, fw->cmd_mbox + in_size, ret_size);

cmd_exit:
	prestera_fw_write(fw, PRESTERA_CMD_REQ_CTL_REG, PRESTERA_CMD_F_REPL_RCVD);
	return err;
}

static int prestera_fw_send_req(struct prestera_device *dev,
				void *in_msg, size_t in_size, void *out_msg,
				size_t out_size, unsigned int waitms)
{
	struct prestera_fw *fw;
	ssize_t ret;

	fw = container_of(dev, struct prestera_fw, dev);

	mutex_lock(&fw->cmd_mtx);
	ret = prestera_fw_cmd_send(fw, in_msg, in_size, out_msg, out_size, waitms);
	mutex_unlock(&fw->cmd_mtx);

	return ret;
}

static int prestera_fw_init(struct prestera_fw *fw)
{
	u8 __iomem *base;
	int err;
	u8 qid;

	fw->dev.send_req = prestera_fw_send_req;
	fw->ldr_regs = fw->dev.ctl_regs;

	err = prestera_fw_load(fw);
	if (err)
		return err;

	err = prestera_fw_wait_reg32(fw, PRESTERA_FW_READY_REG,
				     PRESTERA_FW_READY_MAGIC,
				     PRESTERA_FW_READY_WAIT_MS);
	if (err) {
		dev_err(fw->dev.dev, "FW failed to start\n");
		return err;
	}

	base = fw->dev.ctl_regs;

	fw->cmd_mbox = base + prestera_fw_read(fw, PRESTERA_CMD_BUF_OFFS_REG);
	fw->cmd_mbox_len = prestera_fw_read(fw, PRESTERA_CMD_BUF_LEN_REG);
	mutex_init(&fw->cmd_mtx);

	fw->evt_buf = base + prestera_fw_read(fw, PRESTERA_EVT_BUF_OFFS_REG);
	fw->evt_qnum = prestera_fw_read(fw, PRESTERA_EVT_QNUM_REG);
	fw->evt_msg = kmalloc(PRESTERA_MSG_MAX_SIZE, GFP_KERNEL);
	if (!fw->evt_msg)
		return -ENOMEM;

	for (qid = 0; qid < fw->evt_qnum; qid++) {
		u32 offs = prestera_fw_read(fw, PRESTERA_EVTQ_OFFS_REG(qid));
		struct prestera_fw_evtq *evtq = &fw->evt_queue[qid];

		evtq->len = prestera_fw_read(fw, PRESTERA_EVTQ_LEN_REG(qid));
		evtq->addr = fw->evt_buf + offs;
	}

	return 0;
}

static void prestera_fw_uninit(struct prestera_fw *fw)
{
	kfree(fw->evt_msg);
}

static irqreturn_t prestera_pci_irq_handler(int irq, void *dev_id)
{
	struct prestera_fw *fw = dev_id;

	if (prestera_fw_read(fw, PRESTERA_RX_STATUS_REG)) {
		prestera_fw_write(fw, PRESTERA_RX_STATUS_REG, 0);

		if (fw->dev.recv_pkt)
			fw->dev.recv_pkt(&fw->dev);
	}

	queue_work(fw->wq, &fw->evt_work);

	return IRQ_HANDLED;
}

static void prestera_ldr_write(struct prestera_fw *fw, u32 reg, u32 val)
{
	writel(val, PRESTERA_LDR_REG_ADDR(fw, reg));
}

static u32 prestera_ldr_read(struct prestera_fw *fw, u32 reg)
{
	return readl(PRESTERA_LDR_REG_ADDR(fw, reg));
}

static int prestera_ldr_wait_reg32(struct prestera_fw *fw,
				   u32 reg, u32 cmp, unsigned int waitms)
{
	u8 __iomem *addr = PRESTERA_LDR_REG_ADDR(fw, reg);
	u32 val;

	return readl_poll_timeout(addr, val, cmp == val,
				  10 * USEC_PER_MSEC, waitms * USEC_PER_MSEC);
}

static u32 prestera_ldr_wait_buf(struct prestera_fw *fw, size_t len)
{
	u8 __iomem *addr = PRESTERA_LDR_REG_ADDR(fw, PRESTERA_LDR_BUF_RD_REG);
	u32 buf_len = fw->ldr_buf_len;
	u32 wr_idx = fw->ldr_wr_idx;
	u32 rd_idx;

	return readl_poll_timeout(addr, rd_idx,
				 CIRC_SPACE(wr_idx, rd_idx, buf_len) >= len,
				 1 * USEC_PER_MSEC, 100 * USEC_PER_MSEC);
}

static int prestera_ldr_wait_dl_finish(struct prestera_fw *fw)
{
	u8 __iomem *addr = PRESTERA_LDR_REG_ADDR(fw, PRESTERA_LDR_STATUS_REG);
	unsigned long mask = ~(PRESTERA_LDR_STATUS_IMG_DL);
	u32 val;
	int err;

	err = readl_poll_timeout(addr, val, val & mask, 10 * USEC_PER_MSEC,
				 PRESTERA_FW_DL_TIMEOUT_MS * USEC_PER_MSEC);
	if (err) {
		dev_err(fw->dev.dev, "Timeout to load FW img [state=%d]",
			prestera_ldr_read(fw, PRESTERA_LDR_STATUS_REG));
		return err;
	}

	return 0;
}

static void prestera_ldr_wr_idx_move(struct prestera_fw *fw, unsigned int n)
{
	fw->ldr_wr_idx = (fw->ldr_wr_idx + (n)) & (fw->ldr_buf_len - 1);
}

static void prestera_ldr_wr_idx_commit(struct prestera_fw *fw)
{
	prestera_ldr_write(fw, PRESTERA_LDR_BUF_WR_REG, fw->ldr_wr_idx);
}

static u8 __iomem *prestera_ldr_wr_ptr(struct prestera_fw *fw)
{
	return fw->ldr_ring_buf + fw->ldr_wr_idx;
}

static int prestera_ldr_send(struct prestera_fw *fw, const u8 *buf, size_t len)
{
	int err;
	int i;

	err = prestera_ldr_wait_buf(fw, len);
	if (err) {
		dev_err(fw->dev.dev, "failed wait for sending firmware\n");
		return err;
	}

	for (i = 0; i < len; i += 4) {
		writel_relaxed(*(u32 *)(buf + i), prestera_ldr_wr_ptr(fw));
		prestera_ldr_wr_idx_move(fw, 4);
	}

	prestera_ldr_wr_idx_commit(fw);
	return 0;
}

static int prestera_ldr_fw_send(struct prestera_fw *fw,
				const char *img, u32 fw_size)
{
	u32 status;
	u32 pos;
	int err;

	err = prestera_ldr_wait_reg32(fw, PRESTERA_LDR_STATUS_REG,
				      PRESTERA_LDR_STATUS_IMG_DL,
				      5 * MSEC_PER_SEC);
	if (err) {
		dev_err(fw->dev.dev, "Loader is not ready to load image\n");
		return err;
	}

	for (pos = 0; pos < fw_size; pos += PRESTERA_FW_BLK_SZ) {
		if (pos + PRESTERA_FW_BLK_SZ > fw_size)
			break;

		err = prestera_ldr_send(fw, img + pos, PRESTERA_FW_BLK_SZ);
		if (err)
			return err;
	}

	if (pos < fw_size) {
		err = prestera_ldr_send(fw, img + pos, fw_size - pos);
		if (err)
			return err;
	}

	err = prestera_ldr_wait_dl_finish(fw);
	if (err)
		return err;

	status = prestera_ldr_read(fw, PRESTERA_LDR_STATUS_REG);

	switch (status) {
	case PRESTERA_LDR_STATUS_INVALID_IMG:
		dev_err(fw->dev.dev, "FW img has bad CRC\n");
		return -EINVAL;
	case PRESTERA_LDR_STATUS_NOMEM:
		dev_err(fw->dev.dev, "Loader has no enough mem\n");
		return -ENOMEM;
	}

	return 0;
}

static void prestera_fw_rev_parse(const struct prestera_fw_header *hdr,
				  struct prestera_fw_rev *rev)
{
	u32 version = be32_to_cpu(hdr->version_value);

	rev->maj = PRESTERA_FW_VER_MAJ(version);
	rev->min = PRESTERA_FW_VER_MIN(version);
	rev->sub = PRESTERA_FW_VER_PATCH(version);
}

static int prestera_fw_rev_check(struct prestera_fw *fw)
{
	struct prestera_fw_rev *rev = &fw->dev.fw_rev;

	if (rev->maj == fw->rev_supp.maj && rev->min >= fw->rev_supp.min)
		return 0;

	dev_err(fw->dev.dev, "Driver supports FW version only '%u.%u.x'",
		fw->rev_supp.maj, fw->rev_supp.min);

	return -EINVAL;
}

static int prestera_fw_hdr_parse(struct prestera_fw *fw)
{
	struct prestera_fw_rev *rev = &fw->dev.fw_rev;
	struct prestera_fw_header *hdr;
	u32 magic;

	hdr = (struct prestera_fw_header *)fw->bin->data;

	magic = be32_to_cpu(hdr->magic_number);
	if (magic != PRESTERA_FW_HDR_MAGIC) {
		dev_err(fw->dev.dev, "FW img hdr magic is invalid");
		return -EINVAL;
	}

	prestera_fw_rev_parse(hdr, rev);

	dev_info(fw->dev.dev, "FW version '%u.%u.%u'\n",
		 rev->maj, rev->min, rev->sub);

	return prestera_fw_rev_check(fw);
}

static int prestera_fw_get(struct prestera_fw *fw)
{
	int ver_maj = PRESTERA_SUPP_FW_MAJ_VER;
	int ver_min = PRESTERA_SUPP_FW_MIN_VER;
	char fw_path[128];
	int err;

pick_fw_ver:
	snprintf(fw_path, sizeof(fw_path), PRESTERA_FW_PATH_FMT,
		 ver_maj, ver_min);

	err = request_firmware_direct(&fw->bin, fw_path, fw->dev.dev);
	if (err) {
		if (ver_maj == PRESTERA_SUPP_FW_MAJ_VER) {
			ver_maj = PRESTERA_PREV_FW_MAJ_VER;
			ver_min = PRESTERA_PREV_FW_MIN_VER;

			dev_warn(fw->dev.dev,
				 "missing latest %s firmware, fall-back to previous %u.%u version\n",
				 fw_path, ver_maj, ver_min);

			goto pick_fw_ver;
		} else {
			dev_err(fw->dev.dev, "failed to request previous firmware: %s\n",
				fw_path);
			return err;
		}
	}

	dev_info(fw->dev.dev, "Loading %s ...", fw_path);

	fw->rev_supp.maj = ver_maj;
	fw->rev_supp.min = ver_min;
	fw->rev_supp.sub = 0;

	return 0;
}

static void prestera_fw_put(struct prestera_fw *fw)
{
	release_firmware(fw->bin);
}

static int prestera_fw_load(struct prestera_fw *fw)
{
	size_t hlen = sizeof(struct prestera_fw_header);
	int err;

	err = prestera_ldr_wait_reg32(fw, PRESTERA_LDR_READY_REG,
				      PRESTERA_LDR_READY_MAGIC,
				      5 * MSEC_PER_SEC);
	if (err) {
		dev_err(fw->dev.dev, "waiting for FW loader is timed out");
		return err;
	}

	fw->ldr_ring_buf = fw->ldr_regs +
		prestera_ldr_read(fw, PRESTERA_LDR_BUF_OFFS_REG);

	fw->ldr_buf_len =
		prestera_ldr_read(fw, PRESTERA_LDR_BUF_SIZE_REG);

	fw->ldr_wr_idx = 0;

	err = prestera_fw_get(fw);
	if (err)
		return err;

	err = prestera_fw_hdr_parse(fw);
	if (err) {
		dev_err(fw->dev.dev, "FW image header is invalid\n");
		goto out_release;
	}

	prestera_ldr_write(fw, PRESTERA_LDR_IMG_SIZE_REG, fw->bin->size - hlen);
	prestera_ldr_write(fw, PRESTERA_LDR_CTL_REG, PRESTERA_LDR_CTL_DL_START);

	err = prestera_ldr_fw_send(fw, fw->bin->data + hlen,
				   fw->bin->size - hlen);

out_release:
	prestera_fw_put(fw);
	return err;
}

static int prestera_pci_probe(struct pci_dev *pdev,
			      const struct pci_device_id *id)
{
	const char *driver_name = pdev->driver->name;
	struct prestera_fw *fw;
	int err;

	err = pcim_enable_device(pdev);
	if (err)
		return err;

	err = pcim_iomap_regions(pdev, BIT(PRESTERA_PCI_BAR_FW) |
				 BIT(PRESTERA_PCI_BAR_PP),
				 pci_name(pdev));
	if (err)
		return err;

	err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(30));
	if (err) {
		dev_err(&pdev->dev, "fail to set DMA mask\n");
		goto err_dma_mask;
	}

	pci_set_master(pdev);

	fw = devm_kzalloc(&pdev->dev, sizeof(*fw), GFP_KERNEL);
	if (!fw) {
		err = -ENOMEM;
		goto err_pci_dev_alloc;
	}

	fw->dev.ctl_regs = pcim_iomap_table(pdev)[PRESTERA_PCI_BAR_FW];
	fw->dev.pp_regs = pcim_iomap_table(pdev)[PRESTERA_PCI_BAR_PP];
	fw->dev.dev = &pdev->dev;

	pci_set_drvdata(pdev, fw);

	err = prestera_fw_init(fw);
	if (err)
		goto err_prestera_fw_init;

	dev_info(fw->dev.dev, "Prestera FW is ready\n");

	fw->wq = alloc_workqueue("prestera_fw_wq", WQ_HIGHPRI, 1);
	if (!fw->wq) {
		err = -ENOMEM;
		goto err_wq_alloc;
	}

	INIT_WORK(&fw->evt_work, prestera_fw_evt_work_fn);

	err = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_MSI);
	if (err < 0) {
		dev_err(&pdev->dev, "MSI IRQ init failed\n");
		goto err_irq_alloc;
	}

	err = request_irq(pci_irq_vector(pdev, 0), prestera_pci_irq_handler,
			  0, driver_name, fw);
	if (err) {
		dev_err(&pdev->dev, "fail to request IRQ\n");
		goto err_request_irq;
	}

	err = prestera_device_register(&fw->dev);
	if (err)
		goto err_prestera_dev_register;

	return 0;

err_prestera_dev_register:
	free_irq(pci_irq_vector(pdev, 0), fw);
err_request_irq:
	pci_free_irq_vectors(pdev);
err_irq_alloc:
	destroy_workqueue(fw->wq);
err_wq_alloc:
	prestera_fw_uninit(fw);
err_prestera_fw_init:
err_pci_dev_alloc:
err_dma_mask:
	return err;
}

static void prestera_pci_remove(struct pci_dev *pdev)
{
	struct prestera_fw *fw = pci_get_drvdata(pdev);

	prestera_device_unregister(&fw->dev);
	free_irq(pci_irq_vector(pdev, 0), fw);
	pci_free_irq_vectors(pdev);
	destroy_workqueue(fw->wq);
	prestera_fw_uninit(fw);
}

static const struct pci_device_id prestera_pci_devices[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_MARVELL, 0xC804) },
	{ PCI_DEVICE(PCI_VENDOR_ID_MARVELL, 0xC80C) },
	{ PCI_DEVICE(PCI_VENDOR_ID_MARVELL, 0xCC1E) },
	{ }
};
MODULE_DEVICE_TABLE(pci, prestera_pci_devices);

static struct pci_driver prestera_pci_driver = {
	.name     = "Prestera DX",
	.id_table = prestera_pci_devices,
	.probe    = prestera_pci_probe,
	.remove   = prestera_pci_remove,
};
module_pci_driver(prestera_pci_driver);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("Marvell Prestera switch PCI interface");
