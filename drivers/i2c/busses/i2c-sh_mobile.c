/*
 * SuperH Mobile I2C Controller
 *
 * Copyright (C) 2014 Wolfram Sang <wsa@sang-engineering.com>
 *
 * Copyright (C) 2008 Magnus Damm
 *
 * Portions of the code based on out-of-tree driver i2c-sh7343.c
 * Copyright (c) 2006 Carlos Munoz <carlos@kenati.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>

/* Transmit operation:                                                      */
/*                                                                          */
/* 0 byte transmit                                                          */
/* BUS:     S     A8     ACK   P(*)                                         */
/* IRQ:       DTE   WAIT                                                    */
/* ICIC:                                                                    */
/* ICCR: 0x94 0x90                                                          */
/* ICDR:      A8                                                            */
/*                                                                          */
/* 1 byte transmit                                                          */
/* BUS:     S     A8     ACK   D8(1)   ACK   P(*)                           */
/* IRQ:       DTE   WAIT         WAIT                                       */
/* ICIC:      -DTE                                                          */
/* ICCR: 0x94       0x90                                                    */
/* ICDR:      A8    D8(1)                                                   */
/*                                                                          */
/* 2 byte transmit                                                          */
/* BUS:     S     A8     ACK   D8(1)   ACK   D8(2)   ACK   P(*)             */
/* IRQ:       DTE   WAIT         WAIT          WAIT                         */
/* ICIC:      -DTE                                                          */
/* ICCR: 0x94                    0x90                                       */
/* ICDR:      A8    D8(1)        D8(2)                                      */
/*                                                                          */
/* 3 bytes or more, +---------+ gets repeated                               */
/*                                                                          */
/*                                                                          */
/* Receive operation:                                                       */
/*                                                                          */
/* 0 byte receive - not supported since slave may hold SDA low              */
/*                                                                          */
/* 1 byte receive       [TX] | [RX]                                         */
/* BUS:     S     A8     ACK | D8(1)   ACK   P(*)                           */
/* IRQ:       DTE   WAIT     |   WAIT     DTE                               */
/* ICIC:      -DTE           |   +DTE                                       */
/* ICCR: 0x94       0x81     |   0xc0                                       */
/* ICDR:      A8             |            D8(1)                             */
/*                                                                          */
/* 2 byte receive        [TX]| [RX]                                         */
/* BUS:     S     A8     ACK | D8(1)   ACK   D8(2)   ACK   P(*)             */
/* IRQ:       DTE   WAIT     |   WAIT          WAIT     DTE                 */
/* ICIC:      -DTE           |                 +DTE                         */
/* ICCR: 0x94       0x81     |                 0xc0                         */
/* ICDR:      A8             |                 D8(1)    D8(2)               */
/*                                                                          */
/* 3 byte receive       [TX] | [RX]                                     (*) */
/* BUS:     S     A8     ACK | D8(1)   ACK   D8(2)   ACK   D8(3)   ACK    P */
/* IRQ:       DTE   WAIT     |   WAIT          WAIT         WAIT      DTE   */
/* ICIC:      -DTE           |                              +DTE            */
/* ICCR: 0x94       0x81     |                              0xc0            */
/* ICDR:      A8             |                 D8(1)        D8(2)     D8(3) */
/*                                                                          */
/* 4 bytes or more, this part is repeated    +---------+                    */
/*                                                                          */
/*                                                                          */
/* Interrupt order and BUSY flag                                            */
/*     ___                                                 _                */
/* SDA ___\___XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXAAAAAAAAA___/                 */
/* SCL      \_/1\_/2\_/3\_/4\_/5\_/6\_/7\_/8\___/9\_____/                   */
/*                                                                          */
/*        S   D7  D6  D5  D4  D3  D2  D1  D0              P(*)              */
/*                                           ___                            */
/* WAIT IRQ ________________________________/   \___________                */
/* TACK IRQ ____________________________________/   \_______                */
/* DTE  IRQ __________________________________________/   \_                */
/* AL   IRQ XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX                */
/*         _______________________________________________                  */
/* BUSY __/                                               \_                */
/*                                                                          */
/* (*) The STOP condition is only sent by the master at the end of the last */
/* I2C message or if the I2C_M_STOP flag is set. Similarly, the BUSY bit is */
/* only cleared after the STOP condition, so, between messages we have to   */
/* poll for the DTE bit.                                                    */
/*                                                                          */

enum sh_mobile_i2c_op {
	OP_START = 0,
	OP_TX_FIRST,
	OP_TX,
	OP_TX_STOP,
	OP_TX_STOP_DATA,
	OP_TX_TO_RX,
	OP_RX,
	OP_RX_STOP,
	OP_RX_STOP_DATA,
};

struct sh_mobile_i2c_data {
	struct device *dev;
	void __iomem *reg;
	struct i2c_adapter adap;
	unsigned long bus_speed;
	unsigned int clks_per_count;
	struct clk *clk;
	u_int8_t icic;
	u_int8_t flags;
	u_int16_t iccl;
	u_int16_t icch;

	spinlock_t lock;
	wait_queue_head_t wait;
	struct i2c_msg *msg;
	int pos;
	int sr;
	bool send_stop;
	bool stop_after_dma;

	struct resource *res;
	struct dma_chan *dma_tx;
	struct dma_chan *dma_rx;
	struct scatterlist sg;
	enum dma_data_direction dma_direction;
};

struct sh_mobile_dt_config {
	int clks_per_count;
	void (*setup)(struct sh_mobile_i2c_data *pd);
};

#define IIC_FLAG_HAS_ICIC67	(1 << 0)

#define STANDARD_MODE		100000
#define FAST_MODE		400000

/* Register offsets */
#define ICDR			0x00
#define ICCR			0x04
#define ICSR			0x08
#define ICIC			0x0c
#define ICCL			0x10
#define ICCH			0x14
#define ICSTART			0x70

/* Register bits */
#define ICCR_ICE		0x80
#define ICCR_RACK		0x40
#define ICCR_TRS		0x10
#define ICCR_BBSY		0x04
#define ICCR_SCP		0x01

#define ICSR_SCLM		0x80
#define ICSR_SDAM		0x40
#define SW_DONE			0x20
#define ICSR_BUSY		0x10
#define ICSR_AL			0x08
#define ICSR_TACK		0x04
#define ICSR_WAIT		0x02
#define ICSR_DTE		0x01

#define ICIC_ICCLB8		0x80
#define ICIC_ICCHB8		0x40
#define ICIC_TDMAE		0x20
#define ICIC_RDMAE		0x10
#define ICIC_ALE		0x08
#define ICIC_TACKE		0x04
#define ICIC_WAITE		0x02
#define ICIC_DTEE		0x01

#define ICSTART_ICSTART		0x10

static void iic_wr(struct sh_mobile_i2c_data *pd, int offs, unsigned char data)
{
	if (offs == ICIC)
		data |= pd->icic;

	iowrite8(data, pd->reg + offs);
}

static unsigned char iic_rd(struct sh_mobile_i2c_data *pd, int offs)
{
	return ioread8(pd->reg + offs);
}

static void iic_set_clr(struct sh_mobile_i2c_data *pd, int offs,
			unsigned char set, unsigned char clr)
{
	iic_wr(pd, offs, (iic_rd(pd, offs) | set) & ~clr);
}

static u32 sh_mobile_i2c_iccl(unsigned long count_khz, u32 tLOW, u32 tf)
{
	/*
	 * Conditional expression:
	 *   ICCL >= COUNT_CLK * (tLOW + tf)
	 *
	 * SH-Mobile IIC hardware starts counting the LOW period of
	 * the SCL signal (tLOW) as soon as it pulls the SCL line.
	 * In order to meet the tLOW timing spec, we need to take into
	 * account the fall time of SCL signal (tf).  Default tf value
	 * should be 0.3 us, for safety.
	 */
	return (((count_khz * (tLOW + tf)) + 5000) / 10000);
}

static u32 sh_mobile_i2c_icch(unsigned long count_khz, u32 tHIGH, u32 tf)
{
	/*
	 * Conditional expression:
	 *   ICCH >= COUNT_CLK * (tHIGH + tf)
	 *
	 * SH-Mobile IIC hardware is aware of SCL transition period 'tr',
	 * and can ignore it.  SH-Mobile IIC controller starts counting
	 * the HIGH period of the SCL signal (tHIGH) after the SCL input
	 * voltage increases at VIH.
	 *
	 * Afterward it turned out calculating ICCH using only tHIGH spec
	 * will result in violation of the tHD;STA timing spec.  We need
	 * to take into account the fall time of SDA signal (tf) at START
	 * condition, in order to meet both tHIGH and tHD;STA specs.
	 */
	return (((count_khz * (tHIGH + tf)) + 5000) / 10000);
}

static int sh_mobile_i2c_init(struct sh_mobile_i2c_data *pd)
{
	unsigned long i2c_clk_khz;
	u32 tHIGH, tLOW, tf;
	uint16_t max_val;

	/* Get clock rate after clock is enabled */
	clk_prepare_enable(pd->clk);
	i2c_clk_khz = clk_get_rate(pd->clk) / 1000;
	clk_disable_unprepare(pd->clk);
	i2c_clk_khz /= pd->clks_per_count;

	if (pd->bus_speed == STANDARD_MODE) {
		tLOW	= 47;	/* tLOW = 4.7 us */
		tHIGH	= 40;	/* tHD;STA = tHIGH = 4.0 us */
		tf	= 3;	/* tf = 0.3 us */
	} else if (pd->bus_speed == FAST_MODE) {
		tLOW	= 13;	/* tLOW = 1.3 us */
		tHIGH	= 6;	/* tHD;STA = tHIGH = 0.6 us */
		tf	= 3;	/* tf = 0.3 us */
	} else {
		dev_err(pd->dev, "unrecognized bus speed %lu Hz\n",
			pd->bus_speed);
		return -EINVAL;
	}

	pd->iccl = sh_mobile_i2c_iccl(i2c_clk_khz, tLOW, tf);
	pd->icch = sh_mobile_i2c_icch(i2c_clk_khz, tHIGH, tf);

	max_val = pd->flags & IIC_FLAG_HAS_ICIC67 ? 0x1ff : 0xff;
	if (pd->iccl > max_val || pd->icch > max_val) {
		dev_err(pd->dev, "timing values out of range: L/H=0x%x/0x%x\n",
			pd->iccl, pd->icch);
		return -EINVAL;
	}

	/* one more bit of ICCL in ICIC */
	if (pd->iccl & 0x100)
		pd->icic |= ICIC_ICCLB8;
	else
		pd->icic &= ~ICIC_ICCLB8;

	/* one more bit of ICCH in ICIC */
	if (pd->icch & 0x100)
		pd->icic |= ICIC_ICCHB8;
	else
		pd->icic &= ~ICIC_ICCHB8;

	dev_dbg(pd->dev, "timing values: L/H=0x%x/0x%x\n", pd->iccl, pd->icch);
	return 0;
}

static void activate_ch(struct sh_mobile_i2c_data *pd)
{
	/* Wake up device and enable clock */
	pm_runtime_get_sync(pd->dev);
	clk_prepare_enable(pd->clk);
}

static void deactivate_ch(struct sh_mobile_i2c_data *pd)
{
	/* Disable channel */
	iic_set_clr(pd, ICCR, 0, ICCR_ICE);

	/* Disable clock and mark device as idle */
	clk_disable_unprepare(pd->clk);
	pm_runtime_put_sync(pd->dev);
}

static unsigned char i2c_op(struct sh_mobile_i2c_data *pd,
			    enum sh_mobile_i2c_op op, unsigned char data)
{
	unsigned char ret = 0;
	unsigned long flags;

	dev_dbg(pd->dev, "op %d, data in 0x%02x\n", op, data);

	spin_lock_irqsave(&pd->lock, flags);

	switch (op) {
	case OP_START: /* issue start and trigger DTE interrupt */
		iic_wr(pd, ICCR, ICCR_ICE | ICCR_TRS | ICCR_BBSY);
		break;
	case OP_TX_FIRST: /* disable DTE interrupt and write data */
		iic_wr(pd, ICIC, ICIC_WAITE | ICIC_ALE | ICIC_TACKE);
		iic_wr(pd, ICDR, data);
		break;
	case OP_TX: /* write data */
		iic_wr(pd, ICDR, data);
		break;
	case OP_TX_STOP_DATA: /* write data and issue a stop afterwards */
		iic_wr(pd, ICDR, data);
		/* fallthrough */
	case OP_TX_STOP: /* issue a stop */
		iic_wr(pd, ICCR, pd->send_stop ? ICCR_ICE | ICCR_TRS
					       : ICCR_ICE | ICCR_TRS | ICCR_BBSY);
		break;
	case OP_TX_TO_RX: /* select read mode */
		iic_wr(pd, ICCR, ICCR_ICE | ICCR_SCP);
		break;
	case OP_RX: /* just read data */
		ret = iic_rd(pd, ICDR);
		break;
	case OP_RX_STOP: /* enable DTE interrupt, issue stop */
		iic_wr(pd, ICIC,
		       ICIC_DTEE | ICIC_WAITE | ICIC_ALE | ICIC_TACKE);
		iic_wr(pd, ICCR, ICCR_ICE | ICCR_RACK);
		break;
	case OP_RX_STOP_DATA: /* enable DTE interrupt, read data, issue stop */
		iic_wr(pd, ICIC,
		       ICIC_DTEE | ICIC_WAITE | ICIC_ALE | ICIC_TACKE);
		ret = iic_rd(pd, ICDR);
		iic_wr(pd, ICCR, ICCR_ICE | ICCR_RACK);
		break;
	}

	spin_unlock_irqrestore(&pd->lock, flags);

	dev_dbg(pd->dev, "op %d, data out 0x%02x\n", op, ret);
	return ret;
}

static bool sh_mobile_i2c_is_first_byte(struct sh_mobile_i2c_data *pd)
{
	return pd->pos == -1;
}

static bool sh_mobile_i2c_is_last_byte(struct sh_mobile_i2c_data *pd)
{
	return pd->pos == pd->msg->len - 1;
}

static void sh_mobile_i2c_get_data(struct sh_mobile_i2c_data *pd,
				   unsigned char *buf)
{
	switch (pd->pos) {
	case -1:
		*buf = i2c_8bit_addr_from_msg(pd->msg);
		break;
	default:
		*buf = pd->msg->buf[pd->pos];
	}
}

static int sh_mobile_i2c_isr_tx(struct sh_mobile_i2c_data *pd)
{
	unsigned char data;

	if (pd->pos == pd->msg->len) {
		/* Send stop if we haven't yet (DMA case) */
		if (pd->send_stop && pd->stop_after_dma)
			i2c_op(pd, OP_TX_STOP, 0);
		return 1;
	}

	sh_mobile_i2c_get_data(pd, &data);

	if (sh_mobile_i2c_is_last_byte(pd))
		i2c_op(pd, OP_TX_STOP_DATA, data);
	else if (sh_mobile_i2c_is_first_byte(pd))
		i2c_op(pd, OP_TX_FIRST, data);
	else
		i2c_op(pd, OP_TX, data);

	pd->pos++;
	return 0;
}

static int sh_mobile_i2c_isr_rx(struct sh_mobile_i2c_data *pd)
{
	unsigned char data;
	int real_pos;

	do {
		if (pd->pos <= -1) {
			sh_mobile_i2c_get_data(pd, &data);

			if (sh_mobile_i2c_is_first_byte(pd))
				i2c_op(pd, OP_TX_FIRST, data);
			else
				i2c_op(pd, OP_TX, data);
			break;
		}

		if (pd->pos == 0) {
			i2c_op(pd, OP_TX_TO_RX, 0);
			break;
		}

		real_pos = pd->pos - 2;

		if (pd->pos == pd->msg->len) {
			if (pd->stop_after_dma) {
				/* Simulate PIO end condition after DMA transfer */
				i2c_op(pd, OP_RX_STOP, 0);
				pd->pos++;
				break;
			}

			if (real_pos < 0) {
				i2c_op(pd, OP_RX_STOP, 0);
				break;
			}
			data = i2c_op(pd, OP_RX_STOP_DATA, 0);
		} else
			data = i2c_op(pd, OP_RX, 0);

		if (real_pos >= 0)
			pd->msg->buf[real_pos] = data;
	} while (0);

	pd->pos++;
	return pd->pos == (pd->msg->len + 2);
}

static irqreturn_t sh_mobile_i2c_isr(int irq, void *dev_id)
{
	struct sh_mobile_i2c_data *pd = dev_id;
	unsigned char sr;
	int wakeup = 0;

	sr = iic_rd(pd, ICSR);
	pd->sr |= sr; /* remember state */

	dev_dbg(pd->dev, "i2c_isr 0x%02x 0x%02x %s %d %d!\n", sr, pd->sr,
	       (pd->msg->flags & I2C_M_RD) ? "read" : "write",
	       pd->pos, pd->msg->len);

	/* Kick off TxDMA after preface was done */
	if (pd->dma_direction == DMA_TO_DEVICE && pd->pos == 0)
		iic_set_clr(pd, ICIC, ICIC_TDMAE, 0);
	else if (sr & (ICSR_AL | ICSR_TACK))
		/* don't interrupt transaction - continue to issue stop */
		iic_wr(pd, ICSR, sr & ~(ICSR_AL | ICSR_TACK));
	else if (pd->msg->flags & I2C_M_RD)
		wakeup = sh_mobile_i2c_isr_rx(pd);
	else
		wakeup = sh_mobile_i2c_isr_tx(pd);

	/* Kick off RxDMA after preface was done */
	if (pd->dma_direction == DMA_FROM_DEVICE && pd->pos == 1)
		iic_set_clr(pd, ICIC, ICIC_RDMAE, 0);

	if (sr & ICSR_WAIT) /* TODO: add delay here to support slow acks */
		iic_wr(pd, ICSR, sr & ~ICSR_WAIT);

	if (wakeup) {
		pd->sr |= SW_DONE;
		wake_up(&pd->wait);
	}

	/* defeat write posting to avoid spurious WAIT interrupts */
	iic_rd(pd, ICSR);

	return IRQ_HANDLED;
}

static void sh_mobile_i2c_dma_unmap(struct sh_mobile_i2c_data *pd)
{
	struct dma_chan *chan = pd->dma_direction == DMA_FROM_DEVICE
				? pd->dma_rx : pd->dma_tx;

	dma_unmap_single(chan->device->dev, sg_dma_address(&pd->sg),
			 pd->msg->len, pd->dma_direction);

	pd->dma_direction = DMA_NONE;
}

static void sh_mobile_i2c_cleanup_dma(struct sh_mobile_i2c_data *pd)
{
	if (pd->dma_direction == DMA_NONE)
		return;
	else if (pd->dma_direction == DMA_FROM_DEVICE)
		dmaengine_terminate_all(pd->dma_rx);
	else if (pd->dma_direction == DMA_TO_DEVICE)
		dmaengine_terminate_all(pd->dma_tx);

	sh_mobile_i2c_dma_unmap(pd);
}

static void sh_mobile_i2c_dma_callback(void *data)
{
	struct sh_mobile_i2c_data *pd = data;

	sh_mobile_i2c_dma_unmap(pd);
	pd->pos = pd->msg->len;
	pd->stop_after_dma = true;

	iic_set_clr(pd, ICIC, 0, ICIC_TDMAE | ICIC_RDMAE);
}

static struct dma_chan *sh_mobile_i2c_request_dma_chan(struct device *dev,
				enum dma_transfer_direction dir, dma_addr_t port_addr)
{
	struct dma_chan *chan;
	struct dma_slave_config cfg;
	char *chan_name = dir == DMA_MEM_TO_DEV ? "tx" : "rx";
	int ret;

	chan = dma_request_slave_channel_reason(dev, chan_name);
	if (IS_ERR(chan)) {
		dev_dbg(dev, "request_channel failed for %s (%ld)\n", chan_name,
			PTR_ERR(chan));
		return chan;
	}

	memset(&cfg, 0, sizeof(cfg));
	cfg.direction = dir;
	if (dir == DMA_MEM_TO_DEV) {
		cfg.dst_addr = port_addr;
		cfg.dst_addr_width = DMA_SLAVE_BUSWIDTH_1_BYTE;
	} else {
		cfg.src_addr = port_addr;
		cfg.src_addr_width = DMA_SLAVE_BUSWIDTH_1_BYTE;
	}

	ret = dmaengine_slave_config(chan, &cfg);
	if (ret) {
		dev_dbg(dev, "slave_config failed for %s (%d)\n", chan_name, ret);
		dma_release_channel(chan);
		return ERR_PTR(ret);
	}

	dev_dbg(dev, "got DMA channel for %s\n", chan_name);
	return chan;
}

static void sh_mobile_i2c_xfer_dma(struct sh_mobile_i2c_data *pd)
{
	bool read = pd->msg->flags & I2C_M_RD;
	enum dma_data_direction dir = read ? DMA_FROM_DEVICE : DMA_TO_DEVICE;
	struct dma_chan *chan = read ? pd->dma_rx : pd->dma_tx;
	struct dma_async_tx_descriptor *txdesc;
	dma_addr_t dma_addr;
	dma_cookie_t cookie;

	if (PTR_ERR(chan) == -EPROBE_DEFER) {
		if (read)
			chan = pd->dma_rx = sh_mobile_i2c_request_dma_chan(pd->dev, DMA_DEV_TO_MEM,
									   pd->res->start + ICDR);
		else
			chan = pd->dma_tx = sh_mobile_i2c_request_dma_chan(pd->dev, DMA_MEM_TO_DEV,
									   pd->res->start + ICDR);
	}

	if (IS_ERR(chan))
		return;

	dma_addr = dma_map_single(chan->device->dev, pd->msg->buf, pd->msg->len, dir);
	if (dma_mapping_error(chan->device->dev, dma_addr)) {
		dev_dbg(pd->dev, "dma map failed, using PIO\n");
		return;
	}

	sg_dma_len(&pd->sg) = pd->msg->len;
	sg_dma_address(&pd->sg) = dma_addr;

	pd->dma_direction = dir;

	txdesc = dmaengine_prep_slave_sg(chan, &pd->sg, 1,
					 read ? DMA_DEV_TO_MEM : DMA_MEM_TO_DEV,
					 DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!txdesc) {
		dev_dbg(pd->dev, "dma prep slave sg failed, using PIO\n");
		sh_mobile_i2c_cleanup_dma(pd);
		return;
	}

	txdesc->callback = sh_mobile_i2c_dma_callback;
	txdesc->callback_param = pd;

	cookie = dmaengine_submit(txdesc);
	if (dma_submit_error(cookie)) {
		dev_dbg(pd->dev, "submitting dma failed, using PIO\n");
		sh_mobile_i2c_cleanup_dma(pd);
		return;
	}

	dma_async_issue_pending(chan);
}

static int start_ch(struct sh_mobile_i2c_data *pd, struct i2c_msg *usr_msg,
		    bool do_init)
{
	if (usr_msg->len == 0 && (usr_msg->flags & I2C_M_RD)) {
		dev_err(pd->dev, "Unsupported zero length i2c read\n");
		return -EOPNOTSUPP;
	}

	if (do_init) {
		/* Initialize channel registers */
		iic_set_clr(pd, ICCR, 0, ICCR_ICE);

		/* Enable channel and configure rx ack */
		iic_set_clr(pd, ICCR, ICCR_ICE, 0);

		/* Set the clock */
		iic_wr(pd, ICCL, pd->iccl & 0xff);
		iic_wr(pd, ICCH, pd->icch & 0xff);
	}

	pd->msg = usr_msg;
	pd->pos = -1;
	pd->sr = 0;

	if (pd->msg->len > 8)
		sh_mobile_i2c_xfer_dma(pd);

	/* Enable all interrupts to begin with */
	iic_wr(pd, ICIC, ICIC_DTEE | ICIC_WAITE | ICIC_ALE | ICIC_TACKE);
	return 0;
}

static int poll_dte(struct sh_mobile_i2c_data *pd)
{
	int i;

	for (i = 1000; i; i--) {
		u_int8_t val = iic_rd(pd, ICSR);

		if (val & ICSR_DTE)
			break;

		if (val & ICSR_TACK)
			return -ENXIO;

		udelay(10);
	}

	return i ? 0 : -ETIMEDOUT;
}

static int poll_busy(struct sh_mobile_i2c_data *pd)
{
	int i;

	for (i = 1000; i; i--) {
		u_int8_t val = iic_rd(pd, ICSR);

		dev_dbg(pd->dev, "val 0x%02x pd->sr 0x%02x\n", val, pd->sr);

		/* the interrupt handler may wake us up before the
		 * transfer is finished, so poll the hardware
		 * until we're done.
		 */
		if (!(val & ICSR_BUSY)) {
			/* handle missing acknowledge and arbitration lost */
			val |= pd->sr;
			if (val & ICSR_TACK)
				return -ENXIO;
			if (val & ICSR_AL)
				return -EAGAIN;
			break;
		}

		udelay(10);
	}

	return i ? 0 : -ETIMEDOUT;
}

static int sh_mobile_i2c_xfer(struct i2c_adapter *adapter,
			      struct i2c_msg *msgs,
			      int num)
{
	struct sh_mobile_i2c_data *pd = i2c_get_adapdata(adapter);
	struct i2c_msg	*msg;
	int err = 0;
	int i;
	long timeout;

	activate_ch(pd);

	/* Process all messages */
	for (i = 0; i < num; i++) {
		bool do_start = pd->send_stop || !i;
		msg = &msgs[i];
		pd->send_stop = i == num - 1 || msg->flags & I2C_M_STOP;
		pd->stop_after_dma = false;

		err = start_ch(pd, msg, do_start);
		if (err)
			break;

		if (do_start)
			i2c_op(pd, OP_START, 0);

		/* The interrupt handler takes care of the rest... */
		timeout = wait_event_timeout(pd->wait,
				       pd->sr & (ICSR_TACK | SW_DONE),
				       adapter->timeout);
		if (!timeout) {
			dev_err(pd->dev, "Transfer request timed out\n");
			if (pd->dma_direction != DMA_NONE)
				sh_mobile_i2c_cleanup_dma(pd);

			err = -ETIMEDOUT;
			break;
		}

		if (pd->send_stop)
			err = poll_busy(pd);
		else
			err = poll_dte(pd);
		if (err < 0)
			break;
	}

	deactivate_ch(pd);

	if (!err)
		err = num;
	return err;
}

static u32 sh_mobile_i2c_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL | I2C_FUNC_PROTOCOL_MANGLING;
}

static const struct i2c_algorithm sh_mobile_i2c_algorithm = {
	.functionality	= sh_mobile_i2c_func,
	.master_xfer	= sh_mobile_i2c_xfer,
};

/*
 * r8a7740 chip has lasting errata on I2C I/O pad reset.
 * this is work-around for it.
 */
static void sh_mobile_i2c_r8a7740_workaround(struct sh_mobile_i2c_data *pd)
{
	iic_set_clr(pd, ICCR, ICCR_ICE, 0);
	iic_rd(pd, ICCR); /* dummy read */

	iic_set_clr(pd, ICSTART, ICSTART_ICSTART, 0);
	iic_rd(pd, ICSTART); /* dummy read */

	udelay(10);

	iic_wr(pd, ICCR, ICCR_SCP);
	iic_wr(pd, ICSTART, 0);

	udelay(10);

	iic_wr(pd, ICCR, ICCR_TRS);
	udelay(10);
	iic_wr(pd, ICCR, 0);
	udelay(10);
	iic_wr(pd, ICCR, ICCR_TRS);
	udelay(10);
}

static const struct sh_mobile_dt_config default_dt_config = {
	.clks_per_count = 1,
};

static const struct sh_mobile_dt_config fast_clock_dt_config = {
	.clks_per_count = 2,
};

static const struct sh_mobile_dt_config r8a7740_dt_config = {
	.clks_per_count = 1,
	.setup = sh_mobile_i2c_r8a7740_workaround,
};

static const struct of_device_id sh_mobile_i2c_dt_ids[] = {
	{ .compatible = "renesas,iic-r8a73a4", .data = &fast_clock_dt_config },
	{ .compatible = "renesas,iic-r8a7740", .data = &r8a7740_dt_config },
	{ .compatible = "renesas,iic-r8a7790", .data = &fast_clock_dt_config },
	{ .compatible = "renesas,iic-r8a7791", .data = &fast_clock_dt_config },
	{ .compatible = "renesas,iic-r8a7792", .data = &fast_clock_dt_config },
	{ .compatible = "renesas,iic-r8a7793", .data = &fast_clock_dt_config },
	{ .compatible = "renesas,iic-r8a7794", .data = &fast_clock_dt_config },
	{ .compatible = "renesas,rcar-gen2-iic", .data = &fast_clock_dt_config },
	{ .compatible = "renesas,iic-r8a7795", .data = &fast_clock_dt_config },
	{ .compatible = "renesas,rcar-gen3-iic", .data = &fast_clock_dt_config },
	{ .compatible = "renesas,iic-sh73a0", .data = &fast_clock_dt_config },
	{ .compatible = "renesas,rmobile-iic", .data = &default_dt_config },
	{},
};
MODULE_DEVICE_TABLE(of, sh_mobile_i2c_dt_ids);

static void sh_mobile_i2c_release_dma(struct sh_mobile_i2c_data *pd)
{
	if (!IS_ERR(pd->dma_tx)) {
		dma_release_channel(pd->dma_tx);
		pd->dma_tx = ERR_PTR(-EPROBE_DEFER);
	}

	if (!IS_ERR(pd->dma_rx)) {
		dma_release_channel(pd->dma_rx);
		pd->dma_rx = ERR_PTR(-EPROBE_DEFER);
	}
}

static int sh_mobile_i2c_hook_irqs(struct platform_device *dev, struct sh_mobile_i2c_data *pd)
{
	struct resource *res;
	resource_size_t n;
	int k = 0, ret;

	while ((res = platform_get_resource(dev, IORESOURCE_IRQ, k))) {
		for (n = res->start; n <= res->end; n++) {
			ret = devm_request_irq(&dev->dev, n, sh_mobile_i2c_isr,
					  0, dev_name(&dev->dev), pd);
			if (ret) {
				dev_err(&dev->dev, "cannot request IRQ %pa\n", &n);
				return ret;
			}
		}
		k++;
	}

	return k > 0 ? 0 : -ENOENT;
}

static int sh_mobile_i2c_probe(struct platform_device *dev)
{
	struct sh_mobile_i2c_data *pd;
	struct i2c_adapter *adap;
	struct resource *res;
	const struct sh_mobile_dt_config *config;
	int ret;
	u32 bus_speed;

	pd = devm_kzalloc(&dev->dev, sizeof(struct sh_mobile_i2c_data), GFP_KERNEL);
	if (!pd)
		return -ENOMEM;

	pd->clk = devm_clk_get(&dev->dev, NULL);
	if (IS_ERR(pd->clk)) {
		dev_err(&dev->dev, "cannot get clock\n");
		return PTR_ERR(pd->clk);
	}

	ret = sh_mobile_i2c_hook_irqs(dev, pd);
	if (ret)
		return ret;

	pd->dev = &dev->dev;
	platform_set_drvdata(dev, pd);

	res = platform_get_resource(dev, IORESOURCE_MEM, 0);

	pd->res = res;
	pd->reg = devm_ioremap_resource(&dev->dev, res);
	if (IS_ERR(pd->reg))
		return PTR_ERR(pd->reg);

	ret = of_property_read_u32(dev->dev.of_node, "clock-frequency", &bus_speed);
	pd->bus_speed = ret ? STANDARD_MODE : bus_speed;
	pd->clks_per_count = 1;

	config = of_device_get_match_data(&dev->dev);
	if (config) {
		pd->clks_per_count = config->clks_per_count;

		if (config->setup)
			config->setup(pd);
	}

	/* The IIC blocks on SH-Mobile ARM processors
	 * come with two new bits in ICIC.
	 */
	if (resource_size(res) > 0x17)
		pd->flags |= IIC_FLAG_HAS_ICIC67;

	ret = sh_mobile_i2c_init(pd);
	if (ret)
		return ret;

	/* Init DMA */
	sg_init_table(&pd->sg, 1);
	pd->dma_direction = DMA_NONE;
	pd->dma_rx = pd->dma_tx = ERR_PTR(-EPROBE_DEFER);

	/* Enable Runtime PM for this device.
	 *
	 * Also tell the Runtime PM core to ignore children
	 * for this device since it is valid for us to suspend
	 * this I2C master driver even though the slave devices
	 * on the I2C bus may not be suspended.
	 *
	 * The state of the I2C hardware bus is unaffected by
	 * the Runtime PM state.
	 */
	pm_suspend_ignore_children(&dev->dev, true);
	pm_runtime_enable(&dev->dev);

	/* setup the private data */
	adap = &pd->adap;
	i2c_set_adapdata(adap, pd);

	adap->owner = THIS_MODULE;
	adap->algo = &sh_mobile_i2c_algorithm;
	adap->dev.parent = &dev->dev;
	adap->retries = 5;
	adap->nr = dev->id;
	adap->dev.of_node = dev->dev.of_node;

	strlcpy(adap->name, dev->name, sizeof(adap->name));

	spin_lock_init(&pd->lock);
	init_waitqueue_head(&pd->wait);

	ret = i2c_add_numbered_adapter(adap);
	if (ret < 0) {
		sh_mobile_i2c_release_dma(pd);
		return ret;
	}

	dev_info(&dev->dev, "I2C adapter %d, bus speed %lu Hz\n", adap->nr, pd->bus_speed);

	return 0;
}

static int sh_mobile_i2c_remove(struct platform_device *dev)
{
	struct sh_mobile_i2c_data *pd = platform_get_drvdata(dev);

	i2c_del_adapter(&pd->adap);
	sh_mobile_i2c_release_dma(pd);
	pm_runtime_disable(&dev->dev);
	return 0;
}

static int sh_mobile_i2c_runtime_nop(struct device *dev)
{
	/* Runtime PM callback shared between ->runtime_suspend()
	 * and ->runtime_resume(). Simply returns success.
	 *
	 * This driver re-initializes all registers after
	 * pm_runtime_get_sync() anyway so there is no need
	 * to save and restore registers here.
	 */
	return 0;
}

static const struct dev_pm_ops sh_mobile_i2c_dev_pm_ops = {
	.runtime_suspend = sh_mobile_i2c_runtime_nop,
	.runtime_resume = sh_mobile_i2c_runtime_nop,
};

static struct platform_driver sh_mobile_i2c_driver = {
	.driver		= {
		.name		= "i2c-sh_mobile",
		.pm		= &sh_mobile_i2c_dev_pm_ops,
		.of_match_table = sh_mobile_i2c_dt_ids,
	},
	.probe		= sh_mobile_i2c_probe,
	.remove		= sh_mobile_i2c_remove,
};

static int __init sh_mobile_i2c_adap_init(void)
{
	return platform_driver_register(&sh_mobile_i2c_driver);
}
subsys_initcall(sh_mobile_i2c_adap_init);

static void __exit sh_mobile_i2c_adap_exit(void)
{
	platform_driver_unregister(&sh_mobile_i2c_driver);
}
module_exit(sh_mobile_i2c_adap_exit);

MODULE_DESCRIPTION("SuperH Mobile I2C Bus Controller driver");
MODULE_AUTHOR("Magnus Damm and Wolfram Sang");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:i2c-sh_mobile");
