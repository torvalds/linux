// SPDX-License-Identifier: GPL-2.0
/* Marvell Octeon EP (EndPoint) Ethernet Driver
 *
 * Copyright (C) 2020 Marvell.
 *
 */
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mutex.h>
#include <linux/jiffies.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/io.h>
#include <linux/pci.h>
#include <linux/etherdevice.h>

#include "octep_ctrl_mbox.h"
#include "octep_config.h"
#include "octep_main.h"

/* Timeout in msecs for message response */
#define OCTEP_CTRL_MBOX_MSG_TIMEOUT_MS			100
/* Time in msecs to wait for message response */
#define OCTEP_CTRL_MBOX_MSG_WAIT_MS			10

/* Size of mbox info in bytes */
#define OCTEP_CTRL_MBOX_INFO_SZ				256
/* Size of mbox host to fw queue info in bytes */
#define OCTEP_CTRL_MBOX_H2FQ_INFO_SZ			16
/* Size of mbox fw to host queue info in bytes */
#define OCTEP_CTRL_MBOX_F2HQ_INFO_SZ			16

#define OCTEP_CTRL_MBOX_TOTAL_INFO_SZ	(OCTEP_CTRL_MBOX_INFO_SZ + \
					 OCTEP_CTRL_MBOX_H2FQ_INFO_SZ + \
					 OCTEP_CTRL_MBOX_F2HQ_INFO_SZ)

#define OCTEP_CTRL_MBOX_INFO_MAGIC_NUM(m)	(m)
#define OCTEP_CTRL_MBOX_INFO_BARMEM_SZ(m)	((m) + 8)
#define OCTEP_CTRL_MBOX_INFO_HOST_STATUS(m)	((m) + 24)
#define OCTEP_CTRL_MBOX_INFO_FW_STATUS(m)	((m) + 144)

#define OCTEP_CTRL_MBOX_H2FQ_INFO(m)	((m) + OCTEP_CTRL_MBOX_INFO_SZ)
#define OCTEP_CTRL_MBOX_H2FQ_PROD(m)	(OCTEP_CTRL_MBOX_H2FQ_INFO(m))
#define OCTEP_CTRL_MBOX_H2FQ_CONS(m)	((OCTEP_CTRL_MBOX_H2FQ_INFO(m)) + 4)
#define OCTEP_CTRL_MBOX_H2FQ_SZ(m)	((OCTEP_CTRL_MBOX_H2FQ_INFO(m)) + 8)

#define OCTEP_CTRL_MBOX_F2HQ_INFO(m)	((m) + \
					 OCTEP_CTRL_MBOX_INFO_SZ + \
					 OCTEP_CTRL_MBOX_H2FQ_INFO_SZ)
#define OCTEP_CTRL_MBOX_F2HQ_PROD(m)	(OCTEP_CTRL_MBOX_F2HQ_INFO(m))
#define OCTEP_CTRL_MBOX_F2HQ_CONS(m)	((OCTEP_CTRL_MBOX_F2HQ_INFO(m)) + 4)
#define OCTEP_CTRL_MBOX_F2HQ_SZ(m)	((OCTEP_CTRL_MBOX_F2HQ_INFO(m)) + 8)

static const u32 mbox_hdr_sz = sizeof(union octep_ctrl_mbox_msg_hdr);

static u32 octep_ctrl_mbox_circq_inc(u32 index, u32 inc, u32 sz)
{
	return (index + inc) % sz;
}

static u32 octep_ctrl_mbox_circq_space(u32 pi, u32 ci, u32 sz)
{
	return sz - (abs(pi - ci) % sz);
}

static u32 octep_ctrl_mbox_circq_depth(u32 pi, u32 ci, u32 sz)
{
	return (abs(pi - ci) % sz);
}

int octep_ctrl_mbox_init(struct octep_ctrl_mbox *mbox)
{
	u64 magic_num, status;

	if (!mbox)
		return -EINVAL;

	if (!mbox->barmem) {
		pr_info("octep_ctrl_mbox : Invalid barmem %p\n", mbox->barmem);
		return -EINVAL;
	}

	magic_num = readq(OCTEP_CTRL_MBOX_INFO_MAGIC_NUM(mbox->barmem));
	if (magic_num != OCTEP_CTRL_MBOX_MAGIC_NUMBER) {
		pr_info("octep_ctrl_mbox : Invalid magic number %llx\n", magic_num);
		return -EINVAL;
	}

	status = readq(OCTEP_CTRL_MBOX_INFO_FW_STATUS(mbox->barmem));
	if (status != OCTEP_CTRL_MBOX_STATUS_READY) {
		pr_info("octep_ctrl_mbox : Firmware is not ready.\n");
		return -EINVAL;
	}

	mbox->barmem_sz = readl(OCTEP_CTRL_MBOX_INFO_BARMEM_SZ(mbox->barmem));

	writeq(OCTEP_CTRL_MBOX_STATUS_INIT,
	       OCTEP_CTRL_MBOX_INFO_HOST_STATUS(mbox->barmem));

	mutex_init(&mbox->h2fq_lock);
	mutex_init(&mbox->f2hq_lock);

	mbox->h2fq.sz = readl(OCTEP_CTRL_MBOX_H2FQ_SZ(mbox->barmem));
	mbox->h2fq.hw_prod = OCTEP_CTRL_MBOX_H2FQ_PROD(mbox->barmem);
	mbox->h2fq.hw_cons = OCTEP_CTRL_MBOX_H2FQ_CONS(mbox->barmem);
	mbox->h2fq.hw_q = mbox->barmem + OCTEP_CTRL_MBOX_TOTAL_INFO_SZ;

	mbox->f2hq.sz = readl(OCTEP_CTRL_MBOX_F2HQ_SZ(mbox->barmem));
	mbox->f2hq.hw_prod = OCTEP_CTRL_MBOX_F2HQ_PROD(mbox->barmem);
	mbox->f2hq.hw_cons = OCTEP_CTRL_MBOX_F2HQ_CONS(mbox->barmem);
	mbox->f2hq.hw_q = mbox->barmem +
			  OCTEP_CTRL_MBOX_TOTAL_INFO_SZ +
			  mbox->h2fq.sz;

	/* ensure ready state is seen after everything is initialized */
	wmb();
	writeq(OCTEP_CTRL_MBOX_STATUS_READY,
	       OCTEP_CTRL_MBOX_INFO_HOST_STATUS(mbox->barmem));

	pr_info("Octep ctrl mbox : Init successful.\n");

	return 0;
}

static void
octep_write_mbox_data(struct octep_ctrl_mbox_q *q, u32 *pi, u32 ci, void *buf, u32 w_sz)
{
	u8 __iomem *qbuf;
	u32 cp_sz;

	/* Assumption: Caller has ensured enough write space */
	qbuf = (q->hw_q + *pi);
	if (*pi < ci) {
		/* copy entire w_sz */
		memcpy_toio(qbuf, buf, w_sz);
		*pi = octep_ctrl_mbox_circq_inc(*pi, w_sz, q->sz);
	} else {
		/* copy up to end of queue */
		cp_sz = min((q->sz - *pi), w_sz);
		memcpy_toio(qbuf, buf, cp_sz);
		w_sz -= cp_sz;
		*pi = octep_ctrl_mbox_circq_inc(*pi, cp_sz, q->sz);
		if (w_sz) {
			/* roll over and copy remaining w_sz */
			buf += cp_sz;
			qbuf = (q->hw_q + *pi);
			memcpy_toio(qbuf, buf, w_sz);
			*pi = octep_ctrl_mbox_circq_inc(*pi, w_sz, q->sz);
		}
	}
}

int octep_ctrl_mbox_send(struct octep_ctrl_mbox *mbox, struct octep_ctrl_mbox_msg *msg)
{
	struct octep_ctrl_mbox_msg_buf *sg;
	struct octep_ctrl_mbox_q *q;
	u32 pi, ci, buf_sz, w_sz;
	int s;

	if (!mbox || !msg)
		return -EINVAL;

	if (readq(OCTEP_CTRL_MBOX_INFO_FW_STATUS(mbox->barmem)) != OCTEP_CTRL_MBOX_STATUS_READY)
		return -EIO;

	mutex_lock(&mbox->h2fq_lock);
	q = &mbox->h2fq;
	pi = readl(q->hw_prod);
	ci = readl(q->hw_cons);

	if (octep_ctrl_mbox_circq_space(pi, ci, q->sz) < (msg->hdr.s.sz + mbox_hdr_sz)) {
		mutex_unlock(&mbox->h2fq_lock);
		return -EAGAIN;
	}

	octep_write_mbox_data(q, &pi, ci, (void *)&msg->hdr, mbox_hdr_sz);
	buf_sz = msg->hdr.s.sz;
	for (s = 0; ((s < msg->sg_num) && (buf_sz > 0)); s++) {
		sg = &msg->sg_list[s];
		w_sz = (sg->sz <= buf_sz) ? sg->sz : buf_sz;
		octep_write_mbox_data(q, &pi, ci, sg->msg, w_sz);
		buf_sz -= w_sz;
	}
	writel(pi, q->hw_prod);
	mutex_unlock(&mbox->h2fq_lock);

	return 0;
}

static void
octep_read_mbox_data(struct octep_ctrl_mbox_q *q, u32 pi, u32 *ci, void *buf, u32 r_sz)
{
	u8 __iomem *qbuf;
	u32 cp_sz;

	/* Assumption: Caller has ensured enough read space */
	qbuf = (q->hw_q + *ci);
	if (*ci < pi) {
		/* copy entire r_sz */
		memcpy_fromio(buf, qbuf, r_sz);
		*ci = octep_ctrl_mbox_circq_inc(*ci, r_sz, q->sz);
	} else {
		/* copy up to end of queue */
		cp_sz = min((q->sz - *ci), r_sz);
		memcpy_fromio(buf, qbuf, cp_sz);
		r_sz -= cp_sz;
		*ci = octep_ctrl_mbox_circq_inc(*ci, cp_sz, q->sz);
		if (r_sz) {
			/* roll over and copy remaining r_sz */
			buf += cp_sz;
			qbuf = (q->hw_q + *ci);
			memcpy_fromio(buf, qbuf, r_sz);
			*ci = octep_ctrl_mbox_circq_inc(*ci, r_sz, q->sz);
		}
	}
}

int octep_ctrl_mbox_recv(struct octep_ctrl_mbox *mbox, struct octep_ctrl_mbox_msg *msg)
{
	struct octep_ctrl_mbox_msg_buf *sg;
	u32 pi, ci, r_sz, buf_sz, q_depth;
	struct octep_ctrl_mbox_q *q;
	int s;

	if (readq(OCTEP_CTRL_MBOX_INFO_FW_STATUS(mbox->barmem)) != OCTEP_CTRL_MBOX_STATUS_READY)
		return -EIO;

	mutex_lock(&mbox->f2hq_lock);
	q = &mbox->f2hq;
	pi = readl(q->hw_prod);
	ci = readl(q->hw_cons);

	q_depth = octep_ctrl_mbox_circq_depth(pi, ci, q->sz);
	if (q_depth < mbox_hdr_sz) {
		mutex_unlock(&mbox->f2hq_lock);
		return -EAGAIN;
	}

	octep_read_mbox_data(q, pi, &ci, (void *)&msg->hdr, mbox_hdr_sz);
	buf_sz = msg->hdr.s.sz;
	for (s = 0; ((s < msg->sg_num) && (buf_sz > 0)); s++) {
		sg = &msg->sg_list[s];
		r_sz = (sg->sz <= buf_sz) ? sg->sz : buf_sz;
		octep_read_mbox_data(q, pi, &ci, sg->msg, r_sz);
		buf_sz -= r_sz;
	}
	writel(ci, q->hw_cons);
	mutex_unlock(&mbox->f2hq_lock);

	return 0;
}

int octep_ctrl_mbox_uninit(struct octep_ctrl_mbox *mbox)
{
	if (!mbox)
		return -EINVAL;
	if (!mbox->barmem)
		return -EINVAL;

	writeq(OCTEP_CTRL_MBOX_STATUS_INVALID,
	       OCTEP_CTRL_MBOX_INFO_HOST_STATUS(mbox->barmem));
	/* ensure uninit state is written before uninitialization */
	wmb();

	mutex_destroy(&mbox->h2fq_lock);
	mutex_destroy(&mbox->f2hq_lock);

	pr_info("Octep ctrl mbox : Uninit successful.\n");

	return 0;
}
