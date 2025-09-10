// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2025 Cix Technology Group Co., Ltd.
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mailbox_controller.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include "mailbox.h"

/*
 * The maximum transmission size is 32 words or 128 bytes.
 */
#define CIX_MBOX_MSG_WORDS	32	/* Max length = 32 words */
#define CIX_MBOX_MSG_LEN_MASK	0x7fL	/* Max length = 128 bytes */

/* [0~7] Fast channel
 * [8] doorbell base channel
 * [9]fifo base channel
 * [10] register base channel
 */
#define CIX_MBOX_FAST_IDX	7
#define CIX_MBOX_DB_IDX		8
#define CIX_MBOX_FIFO_IDX	9
#define CIX_MBOX_REG_IDX	10
#define CIX_MBOX_CHANS	11

/* Register define */
#define CIX_REG_MSG(n)	(0x0 + 0x4*(n))			/* 0x0~0x7c */
#define CIX_REG_DB_ACK	CIX_REG_MSG(CIX_MBOX_MSG_WORDS)	/* 0x80 */
#define CIX_ERR_COMP	(CIX_REG_DB_ACK + 0x4)		/* 0x84 */
#define CIX_ERR_COMP_CLR	(CIX_REG_DB_ACK + 0x8)		/* 0x88 */
#define CIX_REG_F_INT(IDX)	(CIX_ERR_COMP_CLR + 0x4*(IDX+1))	/* 0x8c~0xa8 */
#define CIX_FIFO_WR		(CIX_REG_F_INT(CIX_MBOX_FAST_IDX+1))	/* 0xac */
#define CIX_FIFO_RD		(CIX_FIFO_WR + 0x4)			/* 0xb0 */
#define CIX_FIFO_STAS	(CIX_FIFO_WR + 0x8)			/* 0xb4 */
#define CIX_FIFO_WM		(CIX_FIFO_WR + 0xc)			/* 0xb8 */
#define CIX_INT_ENABLE	(CIX_FIFO_WR + 0x10)		/* 0xbc */
#define CIX_INT_ENABLE_SIDE_B	(CIX_FIFO_WR + 0x14)	/* 0xc0 */
#define CIX_INT_CLEAR	(CIX_FIFO_WR + 0x18)		/* 0xc4 */
#define CIX_INT_STATUS	(CIX_FIFO_WR + 0x1c)		/* 0xc8 */
#define CIX_FIFO_RST	(CIX_FIFO_WR + 0x20)		/* 0xcc */

#define CIX_MBOX_TX		0
#define CIX_MBOX_RX		1

#define CIX_DB_INT_BIT	BIT(0)
#define CIX_DB_ACK_INT_BIT	BIT(1)

#define CIX_FIFO_WM_DEFAULT		CIX_MBOX_MSG_WORDS
#define CIX_FIFO_STAS_WMK		BIT(0)
#define CIX_FIFO_STAS_FULL		BIT(1)
#define CIX_FIFO_STAS_EMPTY		BIT(2)
#define CIX_FIFO_STAS_UFLOW		BIT(3)
#define CIX_FIFO_STAS_OFLOW		BIT(4)

#define CIX_FIFO_RST_BIT		BIT(0)

#define CIX_DB_INT			BIT(0)
#define CIX_ACK_INT			BIT(1)
#define CIX_FIFO_FULL_INT		BIT(2)
#define CIX_FIFO_EMPTY_INT		BIT(3)
#define CIX_FIFO_WM01_INT		BIT(4)
#define CIX_FIFO_WM10_INT		BIT(5)
#define CIX_FIFO_OFLOW_INT		BIT(6)
#define CIX_FIFO_UFLOW_INT		BIT(7)
#define CIX_FIFO_N_EMPTY_INT	BIT(8)
#define CIX_FAST_CH_INT(IDX)	BIT((IDX)+9)

#define CIX_SHMEM_OFFSET 0x80

enum cix_mbox_chan_type {
	CIX_MBOX_TYPE_DB,
	CIX_MBOX_TYPE_REG,
	CIX_MBOX_TYPE_FIFO,
	CIX_MBOX_TYPE_FAST,
};

struct cix_mbox_con_priv {
	enum cix_mbox_chan_type type;
	struct mbox_chan	*chan;
	int index;
};

struct cix_mbox_priv {
	struct device *dev;
	int irq;
	int dir;
	void __iomem *base;	/* region for mailbox */
	struct cix_mbox_con_priv con_priv[CIX_MBOX_CHANS];
	struct mbox_chan mbox_chans[CIX_MBOX_CHANS];
	struct mbox_controller mbox;
	bool use_shmem;
};

/*
 * The CIX mailbox supports four types of transfers:
 * CIX_MBOX_TYPE_DB, CIX_MBOX_TYPE_FAST, CIX_MBOX_TYPE_REG, and CIX_MBOX_TYPE_FIFO.
 * For the REG and FIFO types of transfers, the message format is as follows:
 */
union cix_mbox_msg_reg_fifo {
	u32 length;	/* unit is byte */
	u32 buf[CIX_MBOX_MSG_WORDS]; /* buf[0] must be the byte length of this array */
};

static struct cix_mbox_priv *to_cix_mbox_priv(struct mbox_controller *mbox)
{
	return container_of(mbox, struct cix_mbox_priv, mbox);
}

static void cix_mbox_write(struct cix_mbox_priv *priv, u32 val, u32 offset)
{
	if (priv->use_shmem)
		iowrite32(val, priv->base + offset - CIX_SHMEM_OFFSET);
	else
		iowrite32(val, priv->base + offset);
}

static u32 cix_mbox_read(struct cix_mbox_priv *priv, u32 offset)
{
	if (priv->use_shmem)
		return ioread32(priv->base + offset - CIX_SHMEM_OFFSET);
	else
		return ioread32(priv->base + offset);
}

static bool mbox_fifo_empty(struct mbox_chan *chan)
{
	struct cix_mbox_priv *priv = to_cix_mbox_priv(chan->mbox);

	return ((cix_mbox_read(priv, CIX_FIFO_STAS) & CIX_FIFO_STAS_EMPTY) ? true : false);
}

/*
 *The transmission unit of the CIX mailbox is word.
 *The byte length should be converted into the word length.
 */
static inline u32 mbox_get_msg_size(void *msg)
{
	u32 len;

	len = ((u32 *)msg)[0] & CIX_MBOX_MSG_LEN_MASK;
	return DIV_ROUND_UP(len, 4);
}

static int cix_mbox_send_data_db(struct mbox_chan *chan, void *data)
{
	struct cix_mbox_priv *priv = to_cix_mbox_priv(chan->mbox);

	/* trigger doorbell irq */
	cix_mbox_write(priv, CIX_DB_INT_BIT, CIX_REG_DB_ACK);

	return 0;
}

static int cix_mbox_send_data_reg(struct mbox_chan *chan, void *data)
{
	struct cix_mbox_priv *priv = to_cix_mbox_priv(chan->mbox);
	union cix_mbox_msg_reg_fifo *msg = data;
	u32 len, i;

	if (!data)
		return -EINVAL;

	len = mbox_get_msg_size(data);
	for (i = 0; i < len; i++)
		cix_mbox_write(priv, msg->buf[i], CIX_REG_MSG(i));

	/* trigger doorbell irq */
	cix_mbox_write(priv, CIX_DB_INT_BIT, CIX_REG_DB_ACK);

	return 0;
}

static int cix_mbox_send_data_fifo(struct mbox_chan *chan, void *data)
{
	struct cix_mbox_priv *priv = to_cix_mbox_priv(chan->mbox);
	union cix_mbox_msg_reg_fifo *msg = data;
	u32 len, val, i;

	if (!data)
		return -EINVAL;

	len = mbox_get_msg_size(data);
	cix_mbox_write(priv, len, CIX_FIFO_WM);
	for (i = 0; i < len; i++)
		cix_mbox_write(priv, msg->buf[i], CIX_FIFO_WR);

	/* Enable fifo empty interrupt */
	val = cix_mbox_read(priv, CIX_INT_ENABLE);
	val |= CIX_FIFO_EMPTY_INT;
	cix_mbox_write(priv, val, CIX_INT_ENABLE);

	return 0;
}

static int cix_mbox_send_data_fast(struct mbox_chan *chan, void *data)
{
	struct cix_mbox_priv *priv = to_cix_mbox_priv(chan->mbox);
	struct cix_mbox_con_priv *cp = chan->con_priv;
	u32 *arg = (u32 *)data;
	int index = cp->index;

	if (!data)
		return -EINVAL;

	if (index < 0 || index > CIX_MBOX_FAST_IDX) {
		dev_err(priv->dev, "Invalid Mbox index %d\n", index);
		return -EINVAL;
	}

	cix_mbox_write(priv, arg[0], CIX_REG_F_INT(index));

	return 0;
}

static int cix_mbox_send_data(struct mbox_chan *chan, void *data)
{
	struct cix_mbox_priv *priv = to_cix_mbox_priv(chan->mbox);
	struct cix_mbox_con_priv *cp = chan->con_priv;

	if (priv->dir != CIX_MBOX_TX) {
		dev_err(priv->dev, "Invalid Mbox dir %d\n", priv->dir);
		return -EINVAL;
	}

	switch (cp->type) {
	case CIX_MBOX_TYPE_DB:
		cix_mbox_send_data_db(chan, data);
		break;
	case CIX_MBOX_TYPE_REG:
		cix_mbox_send_data_reg(chan, data);
		break;
	case CIX_MBOX_TYPE_FIFO:
		cix_mbox_send_data_fifo(chan, data);
		break;
	case CIX_MBOX_TYPE_FAST:
		cix_mbox_send_data_fast(chan, data);
		break;
	default:
		dev_err(priv->dev, "Invalid channel type: %d\n", cp->type);
		return -EINVAL;
	}
	return 0;
}

static void cix_mbox_isr_db(struct mbox_chan *chan)
{
	struct cix_mbox_priv *priv = to_cix_mbox_priv(chan->mbox);
	u32 int_status;

	int_status = cix_mbox_read(priv, CIX_INT_STATUS);

	if (priv->dir == CIX_MBOX_RX) {
		/* rx interrupt is triggered */
		if (int_status & CIX_DB_INT) {
			cix_mbox_write(priv, CIX_DB_INT, CIX_INT_CLEAR);
			mbox_chan_received_data(chan, NULL);
			/* trigger ack interrupt */
			cix_mbox_write(priv, CIX_DB_ACK_INT_BIT, CIX_REG_DB_ACK);
		}
	} else {
		/* tx ack interrupt is triggered */
		if (int_status & CIX_ACK_INT) {
			cix_mbox_write(priv, CIX_ACK_INT, CIX_INT_CLEAR);
			mbox_chan_received_data(chan, NULL);
		}
	}
}

static void cix_mbox_isr_reg(struct mbox_chan *chan)
{
	struct cix_mbox_priv *priv = to_cix_mbox_priv(chan->mbox);
	u32 int_status;

	int_status = cix_mbox_read(priv, CIX_INT_STATUS);

	if (priv->dir == CIX_MBOX_RX) {
		/* rx interrupt is triggered */
		if (int_status & CIX_DB_INT) {
			u32 data[CIX_MBOX_MSG_WORDS], len, i;

			cix_mbox_write(priv, CIX_DB_INT, CIX_INT_CLEAR);
			data[0] = cix_mbox_read(priv, CIX_REG_MSG(0));
			len = mbox_get_msg_size(data);
			for (i = 1; i < len; i++)
				data[i] = cix_mbox_read(priv, CIX_REG_MSG(i));

			/* trigger ack interrupt */
			cix_mbox_write(priv, CIX_DB_ACK_INT_BIT, CIX_REG_DB_ACK);
			mbox_chan_received_data(chan, data);
		}
	} else {
		/* tx ack interrupt is triggered */
		if (int_status & CIX_ACK_INT) {
			cix_mbox_write(priv, CIX_ACK_INT, CIX_INT_CLEAR);
			mbox_chan_txdone(chan, 0);
		}
	}
}

static void cix_mbox_isr_fifo(struct mbox_chan *chan)
{
	struct cix_mbox_priv *priv = to_cix_mbox_priv(chan->mbox);
	u32 int_status, status;

	int_status = cix_mbox_read(priv, CIX_INT_STATUS);

	if (priv->dir == CIX_MBOX_RX) {
		/* FIFO waterMark interrupt is generated */
		if (int_status & (CIX_FIFO_FULL_INT | CIX_FIFO_WM01_INT)) {
			u32 data[CIX_MBOX_MSG_WORDS] = { 0 }, i = 0;

			cix_mbox_write(priv, (CIX_FIFO_FULL_INT | CIX_FIFO_WM01_INT),
						CIX_INT_CLEAR);
			do {
				data[i++] = cix_mbox_read(priv, CIX_FIFO_RD);
			} while (!mbox_fifo_empty(chan) && i < CIX_MBOX_MSG_WORDS);

			mbox_chan_received_data(chan, data);
		}
		/* FIFO underflow is generated */
		if (int_status & CIX_FIFO_UFLOW_INT) {
			status = cix_mbox_read(priv, CIX_FIFO_STAS);
			dev_err(priv->dev, "fifo underflow: int_stats %d\n", status);
			cix_mbox_write(priv, CIX_FIFO_UFLOW_INT, CIX_INT_CLEAR);
		}
	} else {
		/* FIFO empty interrupt is generated */
		if (int_status & CIX_FIFO_EMPTY_INT) {
			u32 val;

			cix_mbox_write(priv, CIX_FIFO_EMPTY_INT, CIX_INT_CLEAR);
			/* Disable empty irq*/
			val = cix_mbox_read(priv, CIX_INT_ENABLE);
			val &= ~CIX_FIFO_EMPTY_INT;
			cix_mbox_write(priv, val, CIX_INT_ENABLE);
			mbox_chan_txdone(chan, 0);
		}
		/* FIFO overflow is generated */
		if (int_status & CIX_FIFO_OFLOW_INT) {
			status = cix_mbox_read(priv, CIX_FIFO_STAS);
			dev_err(priv->dev, "fifo overlow: int_stats %d\n", status);
			cix_mbox_write(priv, CIX_FIFO_OFLOW_INT, CIX_INT_CLEAR);
		}
	}
}

static void cix_mbox_isr_fast(struct mbox_chan *chan)
{
	struct cix_mbox_priv *priv = to_cix_mbox_priv(chan->mbox);
	struct cix_mbox_con_priv *cp = chan->con_priv;
	u32 int_status, data;

	/* no irq will be trigger for TX dir mbox */
	if (priv->dir != CIX_MBOX_RX)
		return;

	int_status = cix_mbox_read(priv, CIX_INT_STATUS);

	if (int_status & CIX_FAST_CH_INT(cp->index)) {
		cix_mbox_write(priv, CIX_FAST_CH_INT(cp->index), CIX_INT_CLEAR);
		data = cix_mbox_read(priv, CIX_REG_F_INT(cp->index));
		mbox_chan_received_data(chan, &data);
	}
}

static irqreturn_t cix_mbox_isr(int irq, void *arg)
{
	struct mbox_chan *chan = arg;
	struct cix_mbox_priv *priv = to_cix_mbox_priv(chan->mbox);
	struct cix_mbox_con_priv *cp = chan->con_priv;

	switch (cp->type) {
	case CIX_MBOX_TYPE_DB:
		cix_mbox_isr_db(chan);
		break;
	case CIX_MBOX_TYPE_REG:
		cix_mbox_isr_reg(chan);
		break;
	case CIX_MBOX_TYPE_FIFO:
		cix_mbox_isr_fifo(chan);
		break;
	case CIX_MBOX_TYPE_FAST:
		cix_mbox_isr_fast(chan);
		break;
	default:
		dev_err(priv->dev, "Invalid channel type: %d\n", cp->type);
		return IRQ_NONE;
	}

	return IRQ_HANDLED;
}

static int cix_mbox_startup(struct mbox_chan *chan)
{
	struct cix_mbox_priv *priv = to_cix_mbox_priv(chan->mbox);
	struct cix_mbox_con_priv *cp = chan->con_priv;
	int index = cp->index, ret;
	u32 val;

	ret = request_irq(priv->irq, cix_mbox_isr, 0,
			  dev_name(priv->dev), chan);
	if (ret) {
		dev_err(priv->dev, "Unable to acquire IRQ %d\n", priv->irq);
		return ret;
	}

	switch (cp->type) {
	case CIX_MBOX_TYPE_DB:
		/* Overwrite txdone_method for DB channel */
		chan->txdone_method = TXDONE_BY_ACK;
		fallthrough;
	case CIX_MBOX_TYPE_REG:
		if (priv->dir == CIX_MBOX_TX) {
			/* Enable ACK interrupt */
			val = cix_mbox_read(priv, CIX_INT_ENABLE);
			val |= CIX_ACK_INT;
			cix_mbox_write(priv, val, CIX_INT_ENABLE);
		} else {
			/* Enable Doorbell interrupt */
			val = cix_mbox_read(priv, CIX_INT_ENABLE_SIDE_B);
			val |= CIX_DB_INT;
			cix_mbox_write(priv, val, CIX_INT_ENABLE_SIDE_B);
		}
		break;
	case CIX_MBOX_TYPE_FIFO:
		/* reset fifo */
		cix_mbox_write(priv, CIX_FIFO_RST_BIT, CIX_FIFO_RST);
		/* set default watermark */
		cix_mbox_write(priv, CIX_FIFO_WM_DEFAULT, CIX_FIFO_WM);
		if (priv->dir == CIX_MBOX_TX) {
			/* Enable fifo overflow interrupt */
			val = cix_mbox_read(priv, CIX_INT_ENABLE);
			val |= CIX_FIFO_OFLOW_INT;
			cix_mbox_write(priv, val, CIX_INT_ENABLE);
		} else {
			/* Enable fifo full/underflow interrupt */
			val = cix_mbox_read(priv, CIX_INT_ENABLE_SIDE_B);
			val |= CIX_FIFO_UFLOW_INT|CIX_FIFO_WM01_INT;
			cix_mbox_write(priv, val, CIX_INT_ENABLE_SIDE_B);
		}
		break;
	case CIX_MBOX_TYPE_FAST:
		/* Only RX channel has intterupt */
		if (priv->dir == CIX_MBOX_RX) {
			if (index < 0 || index > CIX_MBOX_FAST_IDX) {
				dev_err(priv->dev, "Invalid index %d\n", index);
				ret = -EINVAL;
				goto failed;
			}
			/* enable fast channel interrupt */
			val = cix_mbox_read(priv, CIX_INT_ENABLE_SIDE_B);
			val |= CIX_FAST_CH_INT(index);
			cix_mbox_write(priv, val, CIX_INT_ENABLE_SIDE_B);
		}
		break;
	default:
		dev_err(priv->dev, "Invalid channel type: %d\n", cp->type);
		ret = -EINVAL;
		goto failed;
	}
	return 0;

failed:
	free_irq(priv->irq, chan);
	return ret;
}

static void cix_mbox_shutdown(struct mbox_chan *chan)
{
	struct cix_mbox_priv *priv = to_cix_mbox_priv(chan->mbox);
	struct cix_mbox_con_priv *cp = chan->con_priv;
	int index = cp->index;
	u32 val;

	switch (cp->type) {
	case CIX_MBOX_TYPE_DB:
	case CIX_MBOX_TYPE_REG:
		if (priv->dir == CIX_MBOX_TX) {
			/* Disable ACK interrupt */
			val = cix_mbox_read(priv, CIX_INT_ENABLE);
			val &= ~CIX_ACK_INT;
			cix_mbox_write(priv, val, CIX_INT_ENABLE);
		} else if (priv->dir == CIX_MBOX_RX) {
			/* Disable Doorbell interrupt */
			val = cix_mbox_read(priv, CIX_INT_ENABLE_SIDE_B);
			val &= ~CIX_DB_INT;
			cix_mbox_write(priv, val, CIX_INT_ENABLE_SIDE_B);
		}
		break;
	case CIX_MBOX_TYPE_FIFO:
		if (priv->dir == CIX_MBOX_TX) {
			/* Disable empty/fifo overflow irq*/
			val = cix_mbox_read(priv, CIX_INT_ENABLE);
			val &= ~(CIX_FIFO_EMPTY_INT | CIX_FIFO_OFLOW_INT);
			cix_mbox_write(priv, val, CIX_INT_ENABLE);
		} else if (priv->dir == CIX_MBOX_RX) {
			/* Disable fifo WM01/underflow interrupt */
			val = cix_mbox_read(priv, CIX_INT_ENABLE_SIDE_B);
			val &= ~(CIX_FIFO_UFLOW_INT | CIX_FIFO_WM01_INT);
			cix_mbox_write(priv, val, CIX_INT_ENABLE_SIDE_B);
		}
		break;
	case CIX_MBOX_TYPE_FAST:
		if (priv->dir == CIX_MBOX_RX) {
			if (index < 0 || index > CIX_MBOX_FAST_IDX) {
				dev_err(priv->dev, "Invalid index %d\n", index);
				break;
			}
			/* Disable fast channel interrupt */
			val = cix_mbox_read(priv, CIX_INT_ENABLE_SIDE_B);
			val &= ~CIX_FAST_CH_INT(index);
			cix_mbox_write(priv, val, CIX_INT_ENABLE_SIDE_B);
		}
		break;

	default:
		dev_err(priv->dev, "Invalid channel type: %d\n", cp->type);
		break;
	}

	free_irq(priv->irq, chan);
}

static const struct mbox_chan_ops cix_mbox_chan_ops = {
	.send_data = cix_mbox_send_data,
	.startup = cix_mbox_startup,
	.shutdown = cix_mbox_shutdown,
};

static void cix_mbox_init(struct cix_mbox_priv *priv)
{
	struct cix_mbox_con_priv *cp;
	int i;

	for (i = 0; i < CIX_MBOX_CHANS; i++) {
		cp = &priv->con_priv[i];
		cp->index = i;
		cp->chan = &priv->mbox_chans[i];
		priv->mbox_chans[i].con_priv = cp;
		if (cp->index <= CIX_MBOX_FAST_IDX)
			cp->type = CIX_MBOX_TYPE_FAST;
		if (cp->index == CIX_MBOX_DB_IDX)
			cp->type = CIX_MBOX_TYPE_DB;
		if (cp->index == CIX_MBOX_FIFO_IDX)
			cp->type = CIX_MBOX_TYPE_FIFO;
		if (cp->index == CIX_MBOX_REG_IDX)
			cp->type = CIX_MBOX_TYPE_REG;
	}
}

static int cix_mbox_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct cix_mbox_priv *priv;
	struct resource *res;
	const char *dir_str;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = dev;
	priv->base = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	/*
	 * The first 0x80 bytes of the register space of the cix mailbox controller
	 * can be used as shared memory for clients. When this shared memory is in
	 * use, the base address of the mailbox is offset by 0x80. Therefore, when
	 * performing subsequent read/write operations, it is necessary to subtract
	 * the offset CIX_SHMEM_OFFSET.
	 *
	 * When the base address of the mailbox is offset by 0x80, it indicates
	 * that shmem is in use.
	 */
	priv->use_shmem = !!(res->start & CIX_SHMEM_OFFSET);

	priv->irq = platform_get_irq(pdev, 0);
	if (priv->irq < 0)
		return priv->irq;

	if (device_property_read_string(dev, "cix,mbox-dir", &dir_str)) {
		dev_err(priv->dev, "cix,mbox_dir property not found\n");
		return -EINVAL;
	}

	if (!strcmp(dir_str, "tx"))
		priv->dir = 0;
	else if (!strcmp(dir_str, "rx"))
		priv->dir = 1;
	else {
		dev_err(priv->dev, "cix,mbox_dir=%s is not expected\n", dir_str);
		return -EINVAL;
	}

	cix_mbox_init(priv);

	priv->mbox.dev = dev;
	priv->mbox.ops = &cix_mbox_chan_ops;
	priv->mbox.chans = priv->mbox_chans;
	priv->mbox.txdone_irq = true;
	priv->mbox.num_chans = CIX_MBOX_CHANS;
	priv->mbox.of_xlate = NULL;

	platform_set_drvdata(pdev, priv);
	ret = devm_mbox_controller_register(dev, &priv->mbox);
	if (ret)
		dev_err(dev, "Failed to register mailbox %d\n", ret);

	return ret;
}

static const struct of_device_id cix_mbox_dt_ids[] = {
	{ .compatible = "cix,sky1-mbox" },
	{ },
};
MODULE_DEVICE_TABLE(of, cix_mbox_dt_ids);

static struct platform_driver cix_mbox_driver = {
	.probe = cix_mbox_probe,
	.driver = {
		.name = "cix_mbox",
		.of_match_table = cix_mbox_dt_ids,
	},
};

static int __init cix_mailbox_init(void)
{
	return platform_driver_register(&cix_mbox_driver);
}
arch_initcall(cix_mailbox_init);

MODULE_AUTHOR("Cix Technology Group Co., Ltd.");
MODULE_DESCRIPTION("CIX mailbox driver");
MODULE_LICENSE("GPL");
