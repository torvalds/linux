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

#define OCTEP_CTRL_MBOX_INFO_MAGIC_NUM_OFFSET(m)	(m)
#define OCTEP_CTRL_MBOX_INFO_BARMEM_SZ_OFFSET(m)	((m) + 8)
#define OCTEP_CTRL_MBOX_INFO_HOST_STATUS_OFFSET(m)	((m) + 24)
#define OCTEP_CTRL_MBOX_INFO_FW_STATUS_OFFSET(m)	((m) + 144)

#define OCTEP_CTRL_MBOX_H2FQ_INFO_OFFSET(m)		((m) + OCTEP_CTRL_MBOX_INFO_SZ)
#define OCTEP_CTRL_MBOX_H2FQ_PROD_OFFSET(m)		(OCTEP_CTRL_MBOX_H2FQ_INFO_OFFSET(m))
#define OCTEP_CTRL_MBOX_H2FQ_CONS_OFFSET(m)		((OCTEP_CTRL_MBOX_H2FQ_INFO_OFFSET(m)) + 4)
#define OCTEP_CTRL_MBOX_H2FQ_ELEM_SZ_OFFSET(m)		((OCTEP_CTRL_MBOX_H2FQ_INFO_OFFSET(m)) + 8)
#define OCTEP_CTRL_MBOX_H2FQ_ELEM_CNT_OFFSET(m)		((OCTEP_CTRL_MBOX_H2FQ_INFO_OFFSET(m)) + 12)

#define OCTEP_CTRL_MBOX_F2HQ_INFO_OFFSET(m)		((m) + \
							 OCTEP_CTRL_MBOX_INFO_SZ + \
							 OCTEP_CTRL_MBOX_H2FQ_INFO_SZ)
#define OCTEP_CTRL_MBOX_F2HQ_PROD_OFFSET(m)		(OCTEP_CTRL_MBOX_F2HQ_INFO_OFFSET(m))
#define OCTEP_CTRL_MBOX_F2HQ_CONS_OFFSET(m)		((OCTEP_CTRL_MBOX_F2HQ_INFO_OFFSET(m)) + 4)
#define OCTEP_CTRL_MBOX_F2HQ_ELEM_SZ_OFFSET(m)		((OCTEP_CTRL_MBOX_F2HQ_INFO_OFFSET(m)) + 8)
#define OCTEP_CTRL_MBOX_F2HQ_ELEM_CNT_OFFSET(m)		((OCTEP_CTRL_MBOX_F2HQ_INFO_OFFSET(m)) + 12)

#define OCTEP_CTRL_MBOX_Q_OFFSET(m, i)			((m) + \
							 (sizeof(struct octep_ctrl_mbox_msg) * (i)))

static u32 octep_ctrl_mbox_circq_inc(u32 index, u32 mask)
{
	return (index + 1) & mask;
}

static u32 octep_ctrl_mbox_circq_space(u32 pi, u32 ci, u32 mask)
{
	return mask - ((pi - ci) & mask);
}

static u32 octep_ctrl_mbox_circq_depth(u32 pi, u32 ci, u32 mask)
{
	return ((pi - ci) & mask);
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

	magic_num = readq(OCTEP_CTRL_MBOX_INFO_MAGIC_NUM_OFFSET(mbox->barmem));
	if (magic_num != OCTEP_CTRL_MBOX_MAGIC_NUMBER) {
		pr_info("octep_ctrl_mbox : Invalid magic number %llx\n", magic_num);
		return -EINVAL;
	}

	status = readq(OCTEP_CTRL_MBOX_INFO_FW_STATUS_OFFSET(mbox->barmem));
	if (status != OCTEP_CTRL_MBOX_STATUS_READY) {
		pr_info("octep_ctrl_mbox : Firmware is not ready.\n");
		return -EINVAL;
	}

	mbox->barmem_sz = readl(OCTEP_CTRL_MBOX_INFO_BARMEM_SZ_OFFSET(mbox->barmem));

	writeq(OCTEP_CTRL_MBOX_STATUS_INIT, OCTEP_CTRL_MBOX_INFO_HOST_STATUS_OFFSET(mbox->barmem));

	mbox->h2fq.elem_cnt = readl(OCTEP_CTRL_MBOX_H2FQ_ELEM_CNT_OFFSET(mbox->barmem));
	mbox->h2fq.elem_sz = readl(OCTEP_CTRL_MBOX_H2FQ_ELEM_SZ_OFFSET(mbox->barmem));
	mbox->h2fq.mask = (mbox->h2fq.elem_cnt - 1);
	mutex_init(&mbox->h2fq_lock);

	mbox->f2hq.elem_cnt = readl(OCTEP_CTRL_MBOX_F2HQ_ELEM_CNT_OFFSET(mbox->barmem));
	mbox->f2hq.elem_sz = readl(OCTEP_CTRL_MBOX_F2HQ_ELEM_SZ_OFFSET(mbox->barmem));
	mbox->f2hq.mask = (mbox->f2hq.elem_cnt - 1);
	mutex_init(&mbox->f2hq_lock);

	mbox->h2fq.hw_prod = OCTEP_CTRL_MBOX_H2FQ_PROD_OFFSET(mbox->barmem);
	mbox->h2fq.hw_cons = OCTEP_CTRL_MBOX_H2FQ_CONS_OFFSET(mbox->barmem);
	mbox->h2fq.hw_q = mbox->barmem +
			  OCTEP_CTRL_MBOX_INFO_SZ +
			  OCTEP_CTRL_MBOX_H2FQ_INFO_SZ +
			  OCTEP_CTRL_MBOX_F2HQ_INFO_SZ;

	mbox->f2hq.hw_prod = OCTEP_CTRL_MBOX_F2HQ_PROD_OFFSET(mbox->barmem);
	mbox->f2hq.hw_cons = OCTEP_CTRL_MBOX_F2HQ_CONS_OFFSET(mbox->barmem);
	mbox->f2hq.hw_q = mbox->h2fq.hw_q +
			  ((mbox->h2fq.elem_sz + sizeof(union octep_ctrl_mbox_msg_hdr)) *
			   mbox->h2fq.elem_cnt);

	/* ensure ready state is seen after everything is initialized */
	wmb();
	writeq(OCTEP_CTRL_MBOX_STATUS_READY, OCTEP_CTRL_MBOX_INFO_HOST_STATUS_OFFSET(mbox->barmem));

	pr_info("Octep ctrl mbox : Init successful.\n");

	return 0;
}

int octep_ctrl_mbox_send(struct octep_ctrl_mbox *mbox, struct octep_ctrl_mbox_msg *msg)
{
	unsigned long timeout = msecs_to_jiffies(OCTEP_CTRL_MBOX_MSG_TIMEOUT_MS);
	unsigned long period = msecs_to_jiffies(OCTEP_CTRL_MBOX_MSG_WAIT_MS);
	struct octep_ctrl_mbox_q *q;
	unsigned long expire;
	u64 *mbuf, *word0;
	u8 __iomem *qidx;
	u16 pi, ci;
	int i;

	if (!mbox || !msg)
		return -EINVAL;

	q = &mbox->h2fq;
	pi = readl(q->hw_prod);
	ci = readl(q->hw_cons);

	if (!octep_ctrl_mbox_circq_space(pi, ci, q->mask))
		return -ENOMEM;

	qidx = OCTEP_CTRL_MBOX_Q_OFFSET(q->hw_q, pi);
	mbuf = (u64 *)msg->msg;
	word0 = &msg->hdr.word0;

	mutex_lock(&mbox->h2fq_lock);
	for (i = 1; i <= msg->hdr.sizew; i++)
		writeq(*mbuf++, (qidx + (i * 8)));

	writeq(*word0, qidx);

	pi = octep_ctrl_mbox_circq_inc(pi, q->mask);
	writel(pi, q->hw_prod);
	mutex_unlock(&mbox->h2fq_lock);

	/* don't check for notification response */
	if (msg->hdr.flags & OCTEP_CTRL_MBOX_MSG_HDR_FLAG_NOTIFY)
		return 0;

	expire = jiffies + timeout;
	while (true) {
		*word0 = readq(qidx);
		if (msg->hdr.flags == OCTEP_CTRL_MBOX_MSG_HDR_FLAG_RESP)
			break;
		schedule_timeout_interruptible(period);
		if (signal_pending(current) || time_after(jiffies, expire)) {
			pr_info("octep_ctrl_mbox: Timed out\n");
			return -EBUSY;
		}
	}
	mbuf = (u64 *)msg->msg;
	for (i = 1; i <= msg->hdr.sizew; i++)
		*mbuf++ = readq(qidx + (i * 8));

	return 0;
}

int octep_ctrl_mbox_recv(struct octep_ctrl_mbox *mbox, struct octep_ctrl_mbox_msg *msg)
{
	struct octep_ctrl_mbox_q *q;
	u32 count, pi, ci;
	u8 __iomem *qidx;
	u64 *mbuf;
	int i;

	if (!mbox || !msg)
		return -EINVAL;

	q = &mbox->f2hq;
	pi = readl(q->hw_prod);
	ci = readl(q->hw_cons);
	count = octep_ctrl_mbox_circq_depth(pi, ci, q->mask);
	if (!count)
		return -EAGAIN;

	qidx = OCTEP_CTRL_MBOX_Q_OFFSET(q->hw_q, ci);
	mbuf = (u64 *)msg->msg;

	mutex_lock(&mbox->f2hq_lock);

	msg->hdr.word0 = readq(qidx);
	for (i = 1; i <= msg->hdr.sizew; i++)
		*mbuf++ = readq(qidx + (i * 8));

	ci = octep_ctrl_mbox_circq_inc(ci, q->mask);
	writel(ci, q->hw_cons);

	mutex_unlock(&mbox->f2hq_lock);

	if (msg->hdr.flags != OCTEP_CTRL_MBOX_MSG_HDR_FLAG_REQ || !mbox->process_req)
		return 0;

	mbox->process_req(mbox->user_ctx, msg);
	mbuf = (u64 *)msg->msg;
	for (i = 1; i <= msg->hdr.sizew; i++)
		writeq(*mbuf++, (qidx + (i * 8)));

	writeq(msg->hdr.word0, qidx);

	return 0;
}

int octep_ctrl_mbox_uninit(struct octep_ctrl_mbox *mbox)
{
	if (!mbox)
		return -EINVAL;

	writeq(OCTEP_CTRL_MBOX_STATUS_UNINIT,
	       OCTEP_CTRL_MBOX_INFO_HOST_STATUS_OFFSET(mbox->barmem));
	/* ensure uninit state is written before uninitialization */
	wmb();

	mutex_destroy(&mbox->h2fq_lock);
	mutex_destroy(&mbox->f2hq_lock);

	writeq(OCTEP_CTRL_MBOX_STATUS_INVALID,
	       OCTEP_CTRL_MBOX_INFO_HOST_STATUS_OFFSET(mbox->barmem));

	pr_info("Octep ctrl mbox : Uninit successful.\n");

	return 0;
}
