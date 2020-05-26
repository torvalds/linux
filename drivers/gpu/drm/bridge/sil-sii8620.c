// SPDX-License-Identifier: GPL-2.0-only
/*
 * Silicon Image SiI8620 HDMI/MHL bridge driver
 *
 * Copyright (C) 2015, Samsung Electronics Co., Ltd.
 * Andrzej Hajda <a.hajda@samsung.com>
 */

#include <asm/unaligned.h>

#include <drm/bridge/mhl.h>
#include <drm/drm_bridge.h>
#include <drm/drm_crtc.h>
#include <drm/drm_edid.h>
#include <drm/drm_encoder.h>

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/extcon.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_graph.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>

#include <media/rc-core.h>

#include "sil-sii8620.h"

#define SII8620_BURST_BUF_LEN 288
#define VAL_RX_HDMI_CTRL2_DEFVAL VAL_RX_HDMI_CTRL2_IDLE_CNT(3)

#define MHL1_MAX_PCLK 75000
#define MHL1_MAX_PCLK_PP_MODE 150000
#define MHL3_MAX_PCLK 200000
#define MHL3_MAX_PCLK_PP_MODE 300000

enum sii8620_mode {
	CM_DISCONNECTED,
	CM_DISCOVERY,
	CM_MHL1,
	CM_MHL3,
	CM_ECBUS_S
};

enum sii8620_sink_type {
	SINK_NONE,
	SINK_HDMI,
	SINK_DVI
};

enum sii8620_mt_state {
	MT_STATE_READY,
	MT_STATE_BUSY,
	MT_STATE_DONE
};

struct sii8620 {
	struct drm_bridge bridge;
	struct device *dev;
	struct rc_dev *rc_dev;
	struct clk *clk_xtal;
	struct gpio_desc *gpio_reset;
	struct gpio_desc *gpio_int;
	struct regulator_bulk_data supplies[2];
	struct mutex lock; /* context lock, protects fields below */
	int error;
	unsigned int use_packed_pixel:1;
	enum sii8620_mode mode;
	enum sii8620_sink_type sink_type;
	u8 cbus_status;
	u8 stat[MHL_DST_SIZE];
	u8 xstat[MHL_XDS_SIZE];
	u8 devcap[MHL_DCAP_SIZE];
	u8 xdevcap[MHL_XDC_SIZE];
	bool feature_complete;
	bool devcap_read;
	bool sink_detected;
	struct edid *edid;
	unsigned int gen2_write_burst:1;
	enum sii8620_mt_state mt_state;
	struct extcon_dev *extcon;
	struct notifier_block extcon_nb;
	struct work_struct extcon_wq;
	int cable_state;
	struct list_head mt_queue;
	struct {
		int r_size;
		int r_count;
		int rx_ack;
		int rx_count;
		u8 rx_buf[32];
		int tx_count;
		u8 tx_buf[32];
	} burst;
};

struct sii8620_mt_msg;

typedef void (*sii8620_mt_msg_cb)(struct sii8620 *ctx,
				  struct sii8620_mt_msg *msg);

typedef void (*sii8620_cb)(struct sii8620 *ctx, int ret);

struct sii8620_mt_msg {
	struct list_head node;
	u8 reg[4];
	u8 ret;
	sii8620_mt_msg_cb send;
	sii8620_mt_msg_cb recv;
	sii8620_cb continuation;
};

static const u8 sii8620_i2c_page[] = {
	0x39, /* Main System */
	0x3d, /* TDM and HSIC */
	0x49, /* TMDS Receiver, MHL EDID */
	0x4d, /* eMSC, HDCP, HSIC */
	0x5d, /* MHL Spec */
	0x64, /* MHL CBUS */
	0x59, /* Hardware TPI (Transmitter Programming Interface) */
	0x61, /* eCBUS-S, eCBUS-D */
};

static void sii8620_fetch_edid(struct sii8620 *ctx);
static void sii8620_set_upstream_edid(struct sii8620 *ctx);
static void sii8620_enable_hpd(struct sii8620 *ctx);
static void sii8620_mhl_disconnected(struct sii8620 *ctx);
static void sii8620_disconnect(struct sii8620 *ctx);

static int sii8620_clear_error(struct sii8620 *ctx)
{
	int ret = ctx->error;

	ctx->error = 0;
	return ret;
}

static void sii8620_read_buf(struct sii8620 *ctx, u16 addr, u8 *buf, int len)
{
	struct device *dev = ctx->dev;
	struct i2c_client *client = to_i2c_client(dev);
	u8 data = addr;
	struct i2c_msg msg[] = {
		{
			.addr = sii8620_i2c_page[addr >> 8],
			.flags = client->flags,
			.len = 1,
			.buf = &data
		},
		{
			.addr = sii8620_i2c_page[addr >> 8],
			.flags = client->flags | I2C_M_RD,
			.len = len,
			.buf = buf
		},
	};
	int ret;

	if (ctx->error)
		return;

	ret = i2c_transfer(client->adapter, msg, 2);
	dev_dbg(dev, "read at %04x: %*ph, %d\n", addr, len, buf, ret);

	if (ret != 2) {
		dev_err(dev, "Read at %#06x of %d bytes failed with code %d.\n",
			addr, len, ret);
		ctx->error = ret < 0 ? ret : -EIO;
	}
}

static u8 sii8620_readb(struct sii8620 *ctx, u16 addr)
{
	u8 ret;

	sii8620_read_buf(ctx, addr, &ret, 1);
	return ret;
}

static void sii8620_write_buf(struct sii8620 *ctx, u16 addr, const u8 *buf,
			      int len)
{
	struct device *dev = ctx->dev;
	struct i2c_client *client = to_i2c_client(dev);
	u8 data[2];
	struct i2c_msg msg = {
		.addr = sii8620_i2c_page[addr >> 8],
		.flags = client->flags,
		.len = len + 1,
	};
	int ret;

	if (ctx->error)
		return;

	if (len > 1) {
		msg.buf = kmalloc(len + 1, GFP_KERNEL);
		if (!msg.buf) {
			ctx->error = -ENOMEM;
			return;
		}
		memcpy(msg.buf + 1, buf, len);
	} else {
		msg.buf = data;
		msg.buf[1] = *buf;
	}

	msg.buf[0] = addr;

	ret = i2c_transfer(client->adapter, &msg, 1);
	dev_dbg(dev, "write at %04x: %*ph, %d\n", addr, len, buf, ret);

	if (ret != 1) {
		dev_err(dev, "Write at %#06x of %*ph failed with code %d.\n",
			addr, len, buf, ret);
		ctx->error = ret ?: -EIO;
	}

	if (len > 1)
		kfree(msg.buf);
}

#define sii8620_write(ctx, addr, arr...) \
({\
	u8 d[] = { arr }; \
	sii8620_write_buf(ctx, addr, d, ARRAY_SIZE(d)); \
})

static void __sii8620_write_seq(struct sii8620 *ctx, const u16 *seq, int len)
{
	int i;

	for (i = 0; i < len; i += 2)
		sii8620_write(ctx, seq[i], seq[i + 1]);
}

#define sii8620_write_seq(ctx, seq...) \
({\
	const u16 d[] = { seq }; \
	__sii8620_write_seq(ctx, d, ARRAY_SIZE(d)); \
})

#define sii8620_write_seq_static(ctx, seq...) \
({\
	static const u16 d[] = { seq }; \
	__sii8620_write_seq(ctx, d, ARRAY_SIZE(d)); \
})

static void sii8620_setbits(struct sii8620 *ctx, u16 addr, u8 mask, u8 val)
{
	val = (val & mask) | (sii8620_readb(ctx, addr) & ~mask);
	sii8620_write(ctx, addr, val);
}

static inline bool sii8620_is_mhl3(struct sii8620 *ctx)
{
	return ctx->mode >= CM_MHL3;
}

static void sii8620_mt_cleanup(struct sii8620 *ctx)
{
	struct sii8620_mt_msg *msg, *n;

	list_for_each_entry_safe(msg, n, &ctx->mt_queue, node) {
		list_del(&msg->node);
		kfree(msg);
	}
	ctx->mt_state = MT_STATE_READY;
}

static void sii8620_mt_work(struct sii8620 *ctx)
{
	struct sii8620_mt_msg *msg;

	if (ctx->error)
		return;
	if (ctx->mt_state == MT_STATE_BUSY || list_empty(&ctx->mt_queue))
		return;

	if (ctx->mt_state == MT_STATE_DONE) {
		ctx->mt_state = MT_STATE_READY;
		msg = list_first_entry(&ctx->mt_queue, struct sii8620_mt_msg,
				       node);
		list_del(&msg->node);
		if (msg->recv)
			msg->recv(ctx, msg);
		if (msg->continuation)
			msg->continuation(ctx, msg->ret);
		kfree(msg);
	}

	if (ctx->mt_state != MT_STATE_READY || list_empty(&ctx->mt_queue))
		return;

	ctx->mt_state = MT_STATE_BUSY;
	msg = list_first_entry(&ctx->mt_queue, struct sii8620_mt_msg, node);
	if (msg->send)
		msg->send(ctx, msg);
}

static void sii8620_enable_gen2_write_burst(struct sii8620 *ctx)
{
	u8 ctrl = BIT_MDT_RCV_CTRL_MDT_RCV_EN;

	if (ctx->gen2_write_burst)
		return;

	if (ctx->mode >= CM_MHL1)
		ctrl |= BIT_MDT_RCV_CTRL_MDT_DELAY_RCV_EN;

	sii8620_write_seq(ctx,
		REG_MDT_RCV_TIMEOUT, 100,
		REG_MDT_RCV_CTRL, ctrl
	);
	ctx->gen2_write_burst = 1;
}

static void sii8620_disable_gen2_write_burst(struct sii8620 *ctx)
{
	if (!ctx->gen2_write_burst)
		return;

	sii8620_write_seq_static(ctx,
		REG_MDT_XMIT_CTRL, 0,
		REG_MDT_RCV_CTRL, 0
	);
	ctx->gen2_write_burst = 0;
}

static void sii8620_start_gen2_write_burst(struct sii8620 *ctx)
{
	sii8620_write_seq_static(ctx,
		REG_MDT_INT_1_MASK, BIT_MDT_RCV_TIMEOUT
			| BIT_MDT_RCV_SM_ABORT_PKT_RCVD | BIT_MDT_RCV_SM_ERROR
			| BIT_MDT_XMIT_TIMEOUT | BIT_MDT_XMIT_SM_ABORT_PKT_RCVD
			| BIT_MDT_XMIT_SM_ERROR,
		REG_MDT_INT_0_MASK, BIT_MDT_XFIFO_EMPTY
			| BIT_MDT_IDLE_AFTER_HAWB_DISABLE
			| BIT_MDT_RFIFO_DATA_RDY
	);
	sii8620_enable_gen2_write_burst(ctx);
}

static void sii8620_mt_msc_cmd_send(struct sii8620 *ctx,
				    struct sii8620_mt_msg *msg)
{
	if (msg->reg[0] == MHL_SET_INT &&
	    msg->reg[1] == MHL_INT_REG(RCHANGE) &&
	    msg->reg[2] == MHL_INT_RC_FEAT_REQ)
		sii8620_enable_gen2_write_burst(ctx);
	else
		sii8620_disable_gen2_write_burst(ctx);

	switch (msg->reg[0]) {
	case MHL_WRITE_STAT:
	case MHL_SET_INT:
		sii8620_write_buf(ctx, REG_MSC_CMD_OR_OFFSET, msg->reg + 1, 2);
		sii8620_write(ctx, REG_MSC_COMMAND_START,
			      BIT_MSC_COMMAND_START_WRITE_STAT);
		break;
	case MHL_MSC_MSG:
		sii8620_write_buf(ctx, REG_MSC_CMD_OR_OFFSET, msg->reg, 3);
		sii8620_write(ctx, REG_MSC_COMMAND_START,
			      BIT_MSC_COMMAND_START_MSC_MSG);
		break;
	case MHL_READ_DEVCAP_REG:
	case MHL_READ_XDEVCAP_REG:
		sii8620_write(ctx, REG_MSC_CMD_OR_OFFSET, msg->reg[1]);
		sii8620_write(ctx, REG_MSC_COMMAND_START,
			      BIT_MSC_COMMAND_START_READ_DEVCAP);
		break;
	default:
		dev_err(ctx->dev, "%s: command %#x not supported\n", __func__,
			msg->reg[0]);
	}
}

static struct sii8620_mt_msg *sii8620_mt_msg_new(struct sii8620 *ctx)
{
	struct sii8620_mt_msg *msg = kzalloc(sizeof(*msg), GFP_KERNEL);

	if (!msg)
		ctx->error = -ENOMEM;
	else
		list_add_tail(&msg->node, &ctx->mt_queue);

	return msg;
}

static void sii8620_mt_set_cont(struct sii8620 *ctx, sii8620_cb cont)
{
	struct sii8620_mt_msg *msg;

	if (ctx->error)
		return;

	if (list_empty(&ctx->mt_queue)) {
		ctx->error = -EINVAL;
		return;
	}
	msg = list_last_entry(&ctx->mt_queue, struct sii8620_mt_msg, node);
	msg->continuation = cont;
}

static void sii8620_mt_msc_cmd(struct sii8620 *ctx, u8 cmd, u8 arg1, u8 arg2)
{
	struct sii8620_mt_msg *msg = sii8620_mt_msg_new(ctx);

	if (!msg)
		return;

	msg->reg[0] = cmd;
	msg->reg[1] = arg1;
	msg->reg[2] = arg2;
	msg->send = sii8620_mt_msc_cmd_send;
}

static void sii8620_mt_write_stat(struct sii8620 *ctx, u8 reg, u8 val)
{
	sii8620_mt_msc_cmd(ctx, MHL_WRITE_STAT, reg, val);
}

static inline void sii8620_mt_set_int(struct sii8620 *ctx, u8 irq, u8 mask)
{
	sii8620_mt_msc_cmd(ctx, MHL_SET_INT, irq, mask);
}

static void sii8620_mt_msc_msg(struct sii8620 *ctx, u8 cmd, u8 data)
{
	sii8620_mt_msc_cmd(ctx, MHL_MSC_MSG, cmd, data);
}

static void sii8620_mt_rap(struct sii8620 *ctx, u8 code)
{
	sii8620_mt_msc_msg(ctx, MHL_MSC_MSG_RAP, code);
}

static void sii8620_mt_rcpk(struct sii8620 *ctx, u8 code)
{
	sii8620_mt_msc_msg(ctx, MHL_MSC_MSG_RCPK, code);
}

static void sii8620_mt_rcpe(struct sii8620 *ctx, u8 code)
{
	sii8620_mt_msc_msg(ctx, MHL_MSC_MSG_RCPE, code);
}

static void sii8620_mt_read_devcap_send(struct sii8620 *ctx,
					struct sii8620_mt_msg *msg)
{
	u8 ctrl = BIT_EDID_CTRL_DEVCAP_SELECT_DEVCAP
			| BIT_EDID_CTRL_EDID_FIFO_ADDR_AUTO
			| BIT_EDID_CTRL_EDID_MODE_EN;

	if (msg->reg[0] == MHL_READ_XDEVCAP)
		ctrl |= BIT_EDID_CTRL_XDEVCAP_EN;

	sii8620_write_seq(ctx,
		REG_INTR9_MASK, BIT_INTR9_DEVCAP_DONE,
		REG_EDID_CTRL, ctrl,
		REG_TPI_CBUS_START, BIT_TPI_CBUS_START_GET_DEVCAP_START
	);
}

/* copy src to dst and set changed bits in src */
static void sii8620_update_array(u8 *dst, u8 *src, int count)
{
	while (--count >= 0) {
		*src ^= *dst;
		*dst++ ^= *src++;
	}
}

static void sii8620_identify_sink(struct sii8620 *ctx)
{
	static const char * const sink_str[] = {
		[SINK_NONE] = "NONE",
		[SINK_HDMI] = "HDMI",
		[SINK_DVI] = "DVI"
	};

	char sink_name[20];
	struct device *dev = ctx->dev;

	if (!ctx->sink_detected || !ctx->devcap_read)
		return;

	sii8620_fetch_edid(ctx);
	if (!ctx->edid) {
		dev_err(ctx->dev, "Cannot fetch EDID\n");
		sii8620_mhl_disconnected(ctx);
		return;
	}
	sii8620_set_upstream_edid(ctx);

	if (drm_detect_hdmi_monitor(ctx->edid))
		ctx->sink_type = SINK_HDMI;
	else
		ctx->sink_type = SINK_DVI;

	drm_edid_get_monitor_name(ctx->edid, sink_name, ARRAY_SIZE(sink_name));

	dev_info(dev, "detected sink(type: %s): %s\n",
		 sink_str[ctx->sink_type], sink_name);
}

static void sii8620_mr_devcap(struct sii8620 *ctx)
{
	u8 dcap[MHL_DCAP_SIZE];
	struct device *dev = ctx->dev;

	sii8620_read_buf(ctx, REG_EDID_FIFO_RD_DATA, dcap, MHL_DCAP_SIZE);
	if (ctx->error < 0)
		return;

	dev_info(dev, "detected dongle MHL %d.%d, ChipID %02x%02x:%02x%02x\n",
		 dcap[MHL_DCAP_MHL_VERSION] / 16,
		 dcap[MHL_DCAP_MHL_VERSION] % 16,
		 dcap[MHL_DCAP_ADOPTER_ID_H], dcap[MHL_DCAP_ADOPTER_ID_L],
		 dcap[MHL_DCAP_DEVICE_ID_H], dcap[MHL_DCAP_DEVICE_ID_L]);
	sii8620_update_array(ctx->devcap, dcap, MHL_DCAP_SIZE);
	ctx->devcap_read = true;
	sii8620_identify_sink(ctx);
}

static void sii8620_mr_xdevcap(struct sii8620 *ctx)
{
	sii8620_read_buf(ctx, REG_EDID_FIFO_RD_DATA, ctx->xdevcap,
			 MHL_XDC_SIZE);
}

static void sii8620_mt_read_devcap_recv(struct sii8620 *ctx,
					struct sii8620_mt_msg *msg)
{
	u8 ctrl = BIT_EDID_CTRL_DEVCAP_SELECT_DEVCAP
		| BIT_EDID_CTRL_EDID_FIFO_ADDR_AUTO
		| BIT_EDID_CTRL_EDID_MODE_EN;

	if (msg->reg[0] == MHL_READ_XDEVCAP)
		ctrl |= BIT_EDID_CTRL_XDEVCAP_EN;

	sii8620_write_seq(ctx,
		REG_INTR9_MASK, BIT_INTR9_DEVCAP_DONE | BIT_INTR9_EDID_DONE
			| BIT_INTR9_EDID_ERROR,
		REG_EDID_CTRL, ctrl,
		REG_EDID_FIFO_ADDR, 0
	);

	if (msg->reg[0] == MHL_READ_XDEVCAP)
		sii8620_mr_xdevcap(ctx);
	else
		sii8620_mr_devcap(ctx);
}

static void sii8620_mt_read_devcap(struct sii8620 *ctx, bool xdevcap)
{
	struct sii8620_mt_msg *msg = sii8620_mt_msg_new(ctx);

	if (!msg)
		return;

	msg->reg[0] = xdevcap ? MHL_READ_XDEVCAP : MHL_READ_DEVCAP;
	msg->send = sii8620_mt_read_devcap_send;
	msg->recv = sii8620_mt_read_devcap_recv;
}

static void sii8620_mt_read_devcap_reg_recv(struct sii8620 *ctx,
		struct sii8620_mt_msg *msg)
{
	u8 reg = msg->reg[1] & 0x7f;

	if (msg->reg[1] & 0x80)
		ctx->xdevcap[reg] = msg->ret;
	else
		ctx->devcap[reg] = msg->ret;
}

static void sii8620_mt_read_devcap_reg(struct sii8620 *ctx, u8 reg)
{
	struct sii8620_mt_msg *msg = sii8620_mt_msg_new(ctx);

	if (!msg)
		return;

	msg->reg[0] = (reg & 0x80) ? MHL_READ_XDEVCAP_REG : MHL_READ_DEVCAP_REG;
	msg->reg[1] = reg;
	msg->send = sii8620_mt_msc_cmd_send;
	msg->recv = sii8620_mt_read_devcap_reg_recv;
}

static inline void sii8620_mt_read_xdevcap_reg(struct sii8620 *ctx, u8 reg)
{
	sii8620_mt_read_devcap_reg(ctx, reg | 0x80);
}

static void *sii8620_burst_get_tx_buf(struct sii8620 *ctx, int len)
{
	u8 *buf = &ctx->burst.tx_buf[ctx->burst.tx_count];
	int size = len + 2;

	if (ctx->burst.tx_count + size > ARRAY_SIZE(ctx->burst.tx_buf)) {
		dev_err(ctx->dev, "TX-BLK buffer exhausted\n");
		ctx->error = -EINVAL;
		return NULL;
	}

	ctx->burst.tx_count += size;
	buf[1] = len;

	return buf + 2;
}

static u8 *sii8620_burst_get_rx_buf(struct sii8620 *ctx, int len)
{
	u8 *buf = &ctx->burst.rx_buf[ctx->burst.rx_count];
	int size = len + 1;

	if (ctx->burst.tx_count + size > ARRAY_SIZE(ctx->burst.tx_buf)) {
		dev_err(ctx->dev, "RX-BLK buffer exhausted\n");
		ctx->error = -EINVAL;
		return NULL;
	}

	ctx->burst.rx_count += size;
	buf[0] = len;

	return buf + 1;
}

static void sii8620_burst_send(struct sii8620 *ctx)
{
	int tx_left = ctx->burst.tx_count;
	u8 *d = ctx->burst.tx_buf;

	while (tx_left > 0) {
		int len = d[1] + 2;

		if (ctx->burst.r_count + len > ctx->burst.r_size)
			break;
		d[0] = min(ctx->burst.rx_ack, 255);
		ctx->burst.rx_ack -= d[0];
		sii8620_write_buf(ctx, REG_EMSC_XMIT_WRITE_PORT, d, len);
		ctx->burst.r_count += len;
		tx_left -= len;
		d += len;
	}

	ctx->burst.tx_count = tx_left;

	while (ctx->burst.rx_ack > 0) {
		u8 b[2] = { min(ctx->burst.rx_ack, 255), 0 };

		if (ctx->burst.r_count + 2 > ctx->burst.r_size)
			break;
		ctx->burst.rx_ack -= b[0];
		sii8620_write_buf(ctx, REG_EMSC_XMIT_WRITE_PORT, b, 2);
		ctx->burst.r_count += 2;
	}
}

static void sii8620_burst_receive(struct sii8620 *ctx)
{
	u8 buf[3], *d;
	int count;

	sii8620_read_buf(ctx, REG_EMSCRFIFOBCNTL, buf, 2);
	count = get_unaligned_le16(buf);
	while (count > 0) {
		int len = min(count, 3);

		sii8620_read_buf(ctx, REG_EMSC_RCV_READ_PORT, buf, len);
		count -= len;
		ctx->burst.rx_ack += len - 1;
		ctx->burst.r_count -= buf[1];
		if (ctx->burst.r_count < 0)
			ctx->burst.r_count = 0;

		if (len < 3 || !buf[2])
			continue;

		len = buf[2];
		d = sii8620_burst_get_rx_buf(ctx, len);
		if (!d)
			continue;
		sii8620_read_buf(ctx, REG_EMSC_RCV_READ_PORT, d, len);
		count -= len;
		ctx->burst.rx_ack += len;
	}
}

static void sii8620_burst_tx_rbuf_info(struct sii8620 *ctx, int size)
{
	struct mhl_burst_blk_rcv_buffer_info *d =
		sii8620_burst_get_tx_buf(ctx, sizeof(*d));
	if (!d)
		return;

	d->id = cpu_to_be16(MHL_BURST_ID_BLK_RCV_BUFFER_INFO);
	d->size = cpu_to_le16(size);
}

static u8 sii8620_checksum(void *ptr, int size)
{
	u8 *d = ptr, sum = 0;

	while (size--)
		sum += *d++;

	return sum;
}

static void sii8620_mhl_burst_hdr_set(struct mhl3_burst_header *h,
	enum mhl_burst_id id)
{
	h->id = cpu_to_be16(id);
	h->total_entries = 1;
	h->sequence_index = 1;
}

static void sii8620_burst_tx_bits_per_pixel_fmt(struct sii8620 *ctx, u8 fmt)
{
	struct mhl_burst_bits_per_pixel_fmt *d;
	const int size = sizeof(*d) + sizeof(d->desc[0]);

	d = sii8620_burst_get_tx_buf(ctx, size);
	if (!d)
		return;

	sii8620_mhl_burst_hdr_set(&d->hdr, MHL_BURST_ID_BITS_PER_PIXEL_FMT);
	d->num_entries = 1;
	d->desc[0].stream_id = 0;
	d->desc[0].pixel_format = fmt;
	d->hdr.checksum -= sii8620_checksum(d, size);
}

static void sii8620_burst_rx_all(struct sii8620 *ctx)
{
	u8 *d = ctx->burst.rx_buf;
	int count = ctx->burst.rx_count;

	while (count-- > 0) {
		int len = *d++;
		int id = get_unaligned_be16(&d[0]);

		switch (id) {
		case MHL_BURST_ID_BLK_RCV_BUFFER_INFO:
			ctx->burst.r_size = get_unaligned_le16(&d[2]);
			break;
		default:
			break;
		}
		count -= len;
		d += len;
	}
	ctx->burst.rx_count = 0;
}

static void sii8620_fetch_edid(struct sii8620 *ctx)
{
	u8 lm_ddc, ddc_cmd, int3, cbus;
	unsigned long timeout;
	int fetched, i;
	int edid_len = EDID_LENGTH;
	u8 *edid;

	sii8620_readb(ctx, REG_CBUS_STATUS);
	lm_ddc = sii8620_readb(ctx, REG_LM_DDC);
	ddc_cmd = sii8620_readb(ctx, REG_DDC_CMD);

	sii8620_write_seq(ctx,
		REG_INTR9_MASK, 0,
		REG_EDID_CTRL, BIT_EDID_CTRL_EDID_FIFO_ADDR_AUTO,
		REG_HDCP2X_POLL_CS, 0x71,
		REG_HDCP2X_CTRL_0, BIT_HDCP2X_CTRL_0_HDCP2X_HDCPTX,
		REG_LM_DDC, lm_ddc | BIT_LM_DDC_SW_TPI_EN_DISABLED,
	);

	for (i = 0; i < 256; ++i) {
		u8 ddc_stat = sii8620_readb(ctx, REG_DDC_STATUS);

		if (!(ddc_stat & BIT_DDC_STATUS_DDC_I2C_IN_PROG))
			break;
		sii8620_write(ctx, REG_DDC_STATUS,
			      BIT_DDC_STATUS_DDC_FIFO_EMPTY);
	}

	sii8620_write(ctx, REG_DDC_ADDR, 0x50 << 1);

	edid = kmalloc(EDID_LENGTH, GFP_KERNEL);
	if (!edid) {
		ctx->error = -ENOMEM;
		return;
	}

#define FETCH_SIZE 16
	for (fetched = 0; fetched < edid_len; fetched += FETCH_SIZE) {
		sii8620_readb(ctx, REG_DDC_STATUS);
		sii8620_write_seq(ctx,
			REG_DDC_CMD, ddc_cmd | VAL_DDC_CMD_DDC_CMD_ABORT,
			REG_DDC_CMD, ddc_cmd | VAL_DDC_CMD_DDC_CMD_CLEAR_FIFO,
			REG_DDC_STATUS, BIT_DDC_STATUS_DDC_FIFO_EMPTY
		);
		sii8620_write_seq(ctx,
			REG_DDC_SEGM, fetched >> 8,
			REG_DDC_OFFSET, fetched & 0xff,
			REG_DDC_DIN_CNT1, FETCH_SIZE,
			REG_DDC_DIN_CNT2, 0,
			REG_DDC_CMD, ddc_cmd | VAL_DDC_CMD_ENH_DDC_READ_NO_ACK
		);

		int3 = 0;
		timeout = jiffies + msecs_to_jiffies(200);
		for (;;) {
			cbus = sii8620_readb(ctx, REG_CBUS_STATUS);
			if (~cbus & BIT_CBUS_STATUS_CBUS_CONNECTED) {
				kfree(edid);
				edid = NULL;
				goto end;
			}
			if (int3 & BIT_DDC_CMD_DONE) {
				if (sii8620_readb(ctx, REG_DDC_DOUT_CNT)
				    >= FETCH_SIZE)
					break;
			} else {
				int3 = sii8620_readb(ctx, REG_INTR3);
			}
			if (time_is_before_jiffies(timeout)) {
				ctx->error = -ETIMEDOUT;
				dev_err(ctx->dev, "timeout during EDID read\n");
				kfree(edid);
				edid = NULL;
				goto end;
			}
			usleep_range(10, 20);
		}

		sii8620_read_buf(ctx, REG_DDC_DATA, edid + fetched, FETCH_SIZE);
		if (fetched + FETCH_SIZE == EDID_LENGTH) {
			u8 ext = ((struct edid *)edid)->extensions;

			if (ext) {
				u8 *new_edid;

				edid_len += ext * EDID_LENGTH;
				new_edid = krealloc(edid, edid_len, GFP_KERNEL);
				if (!new_edid) {
					kfree(edid);
					ctx->error = -ENOMEM;
					return;
				}
				edid = new_edid;
			}
		}
	}

	sii8620_write_seq(ctx,
		REG_INTR3_MASK, BIT_DDC_CMD_DONE,
		REG_LM_DDC, lm_ddc
	);

end:
	kfree(ctx->edid);
	ctx->edid = (struct edid *)edid;
}

static void sii8620_set_upstream_edid(struct sii8620 *ctx)
{
	sii8620_setbits(ctx, REG_DPD, BIT_DPD_PDNRX12 | BIT_DPD_PDIDCK_N
			| BIT_DPD_PD_MHL_CLK_N, 0xff);

	sii8620_write_seq_static(ctx,
		REG_RX_HDMI_CTRL3, 0x00,
		REG_PKT_FILTER_0, 0xFF,
		REG_PKT_FILTER_1, 0xFF,
		REG_ALICE0_BW_I2C, 0x06
	);

	sii8620_setbits(ctx, REG_RX_HDMI_CLR_BUFFER,
			BIT_RX_HDMI_CLR_BUFFER_VSI_CLR_EN, 0xff);

	sii8620_write_seq_static(ctx,
		REG_EDID_CTRL, BIT_EDID_CTRL_EDID_FIFO_ADDR_AUTO
			| BIT_EDID_CTRL_EDID_MODE_EN,
		REG_EDID_FIFO_ADDR, 0,
	);

	sii8620_write_buf(ctx, REG_EDID_FIFO_WR_DATA, (u8 *)ctx->edid,
			  (ctx->edid->extensions + 1) * EDID_LENGTH);

	sii8620_write_seq_static(ctx,
		REG_EDID_CTRL, BIT_EDID_CTRL_EDID_PRIME_VALID
			| BIT_EDID_CTRL_EDID_FIFO_ADDR_AUTO
			| BIT_EDID_CTRL_EDID_MODE_EN,
		REG_INTR5_MASK, BIT_INTR_SCDT_CHANGE,
		REG_INTR9_MASK, 0
	);
}

static void sii8620_xtal_set_rate(struct sii8620 *ctx)
{
	static const struct {
		unsigned int rate;
		u8 div;
		u8 tp1;
	} rates[] = {
		{ 19200, 0x04, 0x53 },
		{ 20000, 0x04, 0x62 },
		{ 24000, 0x05, 0x75 },
		{ 30000, 0x06, 0x92 },
		{ 38400, 0x0c, 0xbc },
	};
	unsigned long rate = clk_get_rate(ctx->clk_xtal) / 1000;
	int i;

	for (i = 0; i < ARRAY_SIZE(rates) - 1; ++i)
		if (rate <= rates[i].rate)
			break;

	if (rate != rates[i].rate)
		dev_err(ctx->dev, "xtal clock rate(%lukHz) not supported, setting MHL for %ukHz.\n",
			rate, rates[i].rate);

	sii8620_write(ctx, REG_DIV_CTL_MAIN, rates[i].div);
	sii8620_write(ctx, REG_HDCP2X_TP1, rates[i].tp1);
}

static int sii8620_hw_on(struct sii8620 *ctx)
{
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
	if (ret)
		return ret;

	usleep_range(10000, 20000);
	ret = clk_prepare_enable(ctx->clk_xtal);
	if (ret)
		return ret;

	msleep(100);
	gpiod_set_value(ctx->gpio_reset, 0);
	msleep(100);

	return 0;
}

static int sii8620_hw_off(struct sii8620 *ctx)
{
	clk_disable_unprepare(ctx->clk_xtal);
	gpiod_set_value(ctx->gpio_reset, 1);
	return regulator_bulk_disable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
}

static void sii8620_cbus_reset(struct sii8620 *ctx)
{
	sii8620_write(ctx, REG_PWD_SRST, BIT_PWD_SRST_CBUS_RST
		      | BIT_PWD_SRST_CBUS_RST_SW_EN);
	usleep_range(10000, 20000);
	sii8620_write(ctx, REG_PWD_SRST, BIT_PWD_SRST_CBUS_RST_SW_EN);
}

static void sii8620_set_auto_zone(struct sii8620 *ctx)
{
	if (ctx->mode != CM_MHL1) {
		sii8620_write_seq_static(ctx,
			REG_TX_ZONE_CTL1, 0x0,
			REG_MHL_PLL_CTL0, VAL_MHL_PLL_CTL0_HDMI_CLK_RATIO_1X
				| BIT_MHL_PLL_CTL0_CRYSTAL_CLK_SEL
				| BIT_MHL_PLL_CTL0_ZONE_MASK_OE
		);
	} else {
		sii8620_write_seq_static(ctx,
			REG_TX_ZONE_CTL1, VAL_TX_ZONE_CTL1_TX_ZONE_CTRL_MODE,
			REG_MHL_PLL_CTL0, VAL_MHL_PLL_CTL0_HDMI_CLK_RATIO_1X
				| BIT_MHL_PLL_CTL0_ZONE_MASK_OE
		);
	}
}

static void sii8620_stop_video(struct sii8620 *ctx)
{
	u8 uninitialized_var(val);

	sii8620_write_seq_static(ctx,
		REG_TPI_INTR_EN, 0,
		REG_HDCP2X_INTR0_MASK, 0,
		REG_TPI_COPP_DATA2, 0,
		REG_TPI_INTR_ST0, ~0,
	);

	switch (ctx->sink_type) {
	case SINK_DVI:
		val = BIT_TPI_SC_REG_TMDS_OE_POWER_DOWN
			| BIT_TPI_SC_TPI_AV_MUTE;
		break;
	case SINK_HDMI:
	default:
		val = BIT_TPI_SC_REG_TMDS_OE_POWER_DOWN
			| BIT_TPI_SC_TPI_AV_MUTE
			| BIT_TPI_SC_TPI_OUTPUT_MODE_0_HDMI;
		break;
	}

	sii8620_write(ctx, REG_TPI_SC, val);
}

static void sii8620_set_format(struct sii8620 *ctx)
{
	u8 out_fmt;

	if (sii8620_is_mhl3(ctx)) {
		sii8620_setbits(ctx, REG_M3_P0CTRL,
				BIT_M3_P0CTRL_MHL3_P0_PIXEL_MODE_PACKED,
				ctx->use_packed_pixel ? ~0 : 0);
	} else {
		if (ctx->use_packed_pixel) {
			sii8620_write_seq_static(ctx,
				REG_VID_MODE, BIT_VID_MODE_M1080P,
				REG_MHL_TOP_CTL, BIT_MHL_TOP_CTL_MHL_PP_SEL | 1,
				REG_MHLTX_CTL6, 0x60
			);
		} else {
			sii8620_write_seq_static(ctx,
				REG_VID_MODE, 0,
				REG_MHL_TOP_CTL, 1,
				REG_MHLTX_CTL6, 0xa0
			);
		}
	}

	if (ctx->use_packed_pixel)
		out_fmt = VAL_TPI_FORMAT(YCBCR422, FULL);
	else
		out_fmt = VAL_TPI_FORMAT(RGB, FULL);

	sii8620_write_seq(ctx,
		REG_TPI_INPUT, VAL_TPI_FORMAT(RGB, FULL),
		REG_TPI_OUTPUT, out_fmt,
	);
}

static int mhl3_infoframe_init(struct mhl3_infoframe *frame)
{
	memset(frame, 0, sizeof(*frame));

	frame->version = 3;
	frame->hev_format = -1;
	return 0;
}

static ssize_t mhl3_infoframe_pack(struct mhl3_infoframe *frame,
		 void *buffer, size_t size)
{
	const int frm_len = HDMI_INFOFRAME_HEADER_SIZE + MHL3_INFOFRAME_SIZE;
	u8 *ptr = buffer;

	if (size < frm_len)
		return -ENOSPC;

	memset(buffer, 0, size);
	ptr[0] = HDMI_INFOFRAME_TYPE_VENDOR;
	ptr[1] = frame->version;
	ptr[2] = MHL3_INFOFRAME_SIZE;
	ptr[4] = MHL3_IEEE_OUI & 0xff;
	ptr[5] = (MHL3_IEEE_OUI >> 8) & 0xff;
	ptr[6] = (MHL3_IEEE_OUI >> 16) & 0xff;
	ptr[7] = frame->video_format & 0x3;
	ptr[7] |= (frame->format_type & 0x7) << 2;
	ptr[7] |= frame->sep_audio ? BIT(5) : 0;
	if (frame->hev_format >= 0) {
		ptr[9] = 1;
		ptr[10] = (frame->hev_format >> 8) & 0xff;
		ptr[11] = frame->hev_format & 0xff;
	}
	if (frame->av_delay) {
		bool sign = frame->av_delay < 0;
		int delay = sign ? -frame->av_delay : frame->av_delay;

		ptr[12] = (delay >> 16) & 0xf;
		if (sign)
			ptr[12] |= BIT(4);
		ptr[13] = (delay >> 8) & 0xff;
		ptr[14] = delay & 0xff;
	}
	ptr[3] -= sii8620_checksum(buffer, frm_len);
	return frm_len;
}

static void sii8620_set_infoframes(struct sii8620 *ctx,
				   struct drm_display_mode *mode)
{
	struct mhl3_infoframe mhl_frm;
	union hdmi_infoframe frm;
	u8 buf[31];
	int ret;

	ret = drm_hdmi_avi_infoframe_from_display_mode(&frm.avi,
						       NULL, mode);
	if (ctx->use_packed_pixel)
		frm.avi.colorspace = HDMI_COLORSPACE_YUV422;

	if (!ret)
		ret = hdmi_avi_infoframe_pack(&frm.avi, buf, ARRAY_SIZE(buf));
	if (ret > 0)
		sii8620_write_buf(ctx, REG_TPI_AVI_CHSUM, buf + 3, ret - 3);

	if (!sii8620_is_mhl3(ctx) || !ctx->use_packed_pixel) {
		sii8620_write(ctx, REG_TPI_SC,
			BIT_TPI_SC_TPI_OUTPUT_MODE_0_HDMI);
		sii8620_write(ctx, REG_PKT_FILTER_0,
			BIT_PKT_FILTER_0_DROP_CEA_GAMUT_PKT |
			BIT_PKT_FILTER_0_DROP_MPEG_PKT |
			BIT_PKT_FILTER_0_DROP_GCP_PKT,
			BIT_PKT_FILTER_1_DROP_GEN_PKT);
		return;
	}

	sii8620_write(ctx, REG_PKT_FILTER_0,
		BIT_PKT_FILTER_0_DROP_CEA_GAMUT_PKT |
		BIT_PKT_FILTER_0_DROP_MPEG_PKT |
		BIT_PKT_FILTER_0_DROP_AVI_PKT |
		BIT_PKT_FILTER_0_DROP_GCP_PKT,
		BIT_PKT_FILTER_1_VSI_OVERRIDE_DIS |
		BIT_PKT_FILTER_1_DROP_GEN_PKT |
		BIT_PKT_FILTER_1_DROP_VSIF_PKT);

	sii8620_write(ctx, REG_TPI_INFO_FSEL, BIT_TPI_INFO_FSEL_EN
		| BIT_TPI_INFO_FSEL_RPT | VAL_TPI_INFO_FSEL_VSI);
	ret = mhl3_infoframe_init(&mhl_frm);
	if (!ret)
		ret = mhl3_infoframe_pack(&mhl_frm, buf, ARRAY_SIZE(buf));
	sii8620_write_buf(ctx, REG_TPI_INFO_B0, buf, ret);
}

static void sii8620_start_video(struct sii8620 *ctx)
{
	struct drm_display_mode *mode =
		&ctx->bridge.encoder->crtc->state->adjusted_mode;

	if (!sii8620_is_mhl3(ctx))
		sii8620_stop_video(ctx);

	if (ctx->sink_type == SINK_DVI && !sii8620_is_mhl3(ctx)) {
		sii8620_write(ctx, REG_RX_HDMI_CTRL2,
			      VAL_RX_HDMI_CTRL2_DEFVAL);
		sii8620_write(ctx, REG_TPI_SC, 0);
		return;
	}

	sii8620_write_seq_static(ctx,
		REG_RX_HDMI_CTRL2, VAL_RX_HDMI_CTRL2_DEFVAL
			| BIT_RX_HDMI_CTRL2_USE_AV_MUTE,
		REG_VID_OVRRD, BIT_VID_OVRRD_PP_AUTO_DISABLE
			| BIT_VID_OVRRD_M1080P_OVRRD);
	sii8620_set_format(ctx);

	if (!sii8620_is_mhl3(ctx)) {
		u8 link_mode = MHL_DST_LM_PATH_ENABLED;

		if (ctx->use_packed_pixel)
			link_mode |= MHL_DST_LM_CLK_MODE_PACKED_PIXEL;
		else
			link_mode |= MHL_DST_LM_CLK_MODE_NORMAL;

		sii8620_mt_write_stat(ctx, MHL_DST_REG(LINK_MODE), link_mode);
		sii8620_set_auto_zone(ctx);
	} else {
		static const struct {
			int max_clk;
			u8 zone;
			u8 link_rate;
			u8 rrp_decode;
		} clk_spec[] = {
			{ 150000, VAL_TX_ZONE_CTL3_TX_ZONE_1_5GBPS,
			  MHL_XDS_LINK_RATE_1_5_GBPS, 0x38 },
			{ 300000, VAL_TX_ZONE_CTL3_TX_ZONE_3GBPS,
			  MHL_XDS_LINK_RATE_3_0_GBPS, 0x40 },
			{ 600000, VAL_TX_ZONE_CTL3_TX_ZONE_6GBPS,
			  MHL_XDS_LINK_RATE_6_0_GBPS, 0x40 },
		};
		u8 p0_ctrl = BIT_M3_P0CTRL_MHL3_P0_PORT_EN;
		int clk = mode->clock * (ctx->use_packed_pixel ? 2 : 3);
		int i;

		for (i = 0; i < ARRAY_SIZE(clk_spec) - 1; ++i)
			if (clk < clk_spec[i].max_clk)
				break;

		if (100 * clk >= 98 * clk_spec[i].max_clk)
			p0_ctrl |= BIT_M3_P0CTRL_MHL3_P0_UNLIMIT_EN;

		sii8620_burst_tx_bits_per_pixel_fmt(ctx, ctx->use_packed_pixel);
		sii8620_burst_send(ctx);
		sii8620_write_seq(ctx,
			REG_MHL_DP_CTL0, 0xf0,
			REG_MHL3_TX_ZONE_CTL, clk_spec[i].zone);
		sii8620_setbits(ctx, REG_M3_P0CTRL,
			BIT_M3_P0CTRL_MHL3_P0_PORT_EN
			| BIT_M3_P0CTRL_MHL3_P0_UNLIMIT_EN, p0_ctrl);
		sii8620_setbits(ctx, REG_M3_POSTM, MSK_M3_POSTM_RRP_DECODE,
			clk_spec[i].rrp_decode);
		sii8620_write_seq_static(ctx,
			REG_M3_CTRL, VAL_M3_CTRL_MHL3_VALUE
				| BIT_M3_CTRL_H2M_SWRST,
			REG_M3_CTRL, VAL_M3_CTRL_MHL3_VALUE
		);
		sii8620_mt_write_stat(ctx, MHL_XDS_REG(AVLINK_MODE_CONTROL),
			clk_spec[i].link_rate);
	}

	sii8620_set_infoframes(ctx, mode);
}

static void sii8620_disable_hpd(struct sii8620 *ctx)
{
	sii8620_setbits(ctx, REG_EDID_CTRL, BIT_EDID_CTRL_EDID_PRIME_VALID, 0);
	sii8620_write_seq_static(ctx,
		REG_HPD_CTRL, BIT_HPD_CTRL_HPD_OUT_OVR_EN,
		REG_INTR8_MASK, 0
	);
}

static void sii8620_enable_hpd(struct sii8620 *ctx)
{
	sii8620_setbits(ctx, REG_TMDS_CSTAT_P3,
			BIT_TMDS_CSTAT_P3_SCDT_CLR_AVI_DIS
			| BIT_TMDS_CSTAT_P3_CLR_AVI, ~0);
	sii8620_write_seq_static(ctx,
		REG_HPD_CTRL, BIT_HPD_CTRL_HPD_OUT_OVR_EN
			| BIT_HPD_CTRL_HPD_HIGH,
	);
}

static void sii8620_mhl_discover(struct sii8620 *ctx)
{
	sii8620_write_seq_static(ctx,
		REG_DISC_CTRL9, BIT_DISC_CTRL9_WAKE_DRVFLT
			| BIT_DISC_CTRL9_DISC_PULSE_PROCEED,
		REG_DISC_CTRL4, VAL_DISC_CTRL4(VAL_PUP_5K, VAL_PUP_20K),
		REG_CBUS_DISC_INTR0_MASK, BIT_MHL3_EST_INT
			| BIT_MHL_EST_INT
			| BIT_NOT_MHL_EST_INT
			| BIT_CBUS_MHL3_DISCON_INT
			| BIT_CBUS_MHL12_DISCON_INT
			| BIT_RGND_READY_INT,
		REG_MHL_PLL_CTL0, VAL_MHL_PLL_CTL0_HDMI_CLK_RATIO_1X
			| BIT_MHL_PLL_CTL0_CRYSTAL_CLK_SEL
			| BIT_MHL_PLL_CTL0_ZONE_MASK_OE,
		REG_MHL_DP_CTL0, BIT_MHL_DP_CTL0_DP_OE
			| BIT_MHL_DP_CTL0_TX_OE_OVR,
		REG_M3_CTRL, VAL_M3_CTRL_MHL3_VALUE,
		REG_MHL_DP_CTL1, 0xA2,
		REG_MHL_DP_CTL2, 0x03,
		REG_MHL_DP_CTL3, 0x35,
		REG_MHL_DP_CTL5, 0x02,
		REG_MHL_DP_CTL6, 0x02,
		REG_MHL_DP_CTL7, 0x03,
		REG_COC_CTLC, 0xFF,
		REG_DPD, BIT_DPD_PWRON_PLL | BIT_DPD_PDNTX12
			| BIT_DPD_OSC_EN | BIT_DPD_PWRON_HSIC,
		REG_COC_INTR_MASK, BIT_COC_PLL_LOCK_STATUS_CHANGE
			| BIT_COC_CALIBRATION_DONE,
		REG_CBUS_INT_1_MASK, BIT_CBUS_MSC_ABORT_RCVD
			| BIT_CBUS_CMD_ABORT,
		REG_CBUS_INT_0_MASK, BIT_CBUS_MSC_MT_DONE
			| BIT_CBUS_HPD_CHG
			| BIT_CBUS_MSC_MR_WRITE_STAT
			| BIT_CBUS_MSC_MR_MSC_MSG
			| BIT_CBUS_MSC_MR_WRITE_BURST
			| BIT_CBUS_MSC_MR_SET_INT
			| BIT_CBUS_MSC_MT_DONE_NACK
	);
}

static void sii8620_peer_specific_init(struct sii8620 *ctx)
{
	if (sii8620_is_mhl3(ctx))
		sii8620_write_seq_static(ctx,
			REG_SYS_CTRL1, BIT_SYS_CTRL1_BLOCK_DDC_BY_HPD,
			REG_EMSCINTRMASK1,
				BIT_EMSCINTR1_EMSC_TRAINING_COMMA_ERR
		);
	else
		sii8620_write_seq_static(ctx,
			REG_HDCP2X_INTR0_MASK, 0x00,
			REG_EMSCINTRMASK1, 0x00,
			REG_HDCP2X_INTR0, 0xFF,
			REG_INTR1, 0xFF,
			REG_SYS_CTRL1, BIT_SYS_CTRL1_BLOCK_DDC_BY_HPD
				| BIT_SYS_CTRL1_TX_CTRL_HDMI
		);
}

#define SII8620_MHL_VERSION			0x32
#define SII8620_SCRATCHPAD_SIZE			16
#define SII8620_INT_STAT_SIZE			0x33

static void sii8620_set_dev_cap(struct sii8620 *ctx)
{
	static const u8 devcap[MHL_DCAP_SIZE] = {
		[MHL_DCAP_MHL_VERSION] = SII8620_MHL_VERSION,
		[MHL_DCAP_CAT] = MHL_DCAP_CAT_SOURCE | MHL_DCAP_CAT_POWER,
		[MHL_DCAP_ADOPTER_ID_H] = 0x01,
		[MHL_DCAP_ADOPTER_ID_L] = 0x41,
		[MHL_DCAP_VID_LINK_MODE] = MHL_DCAP_VID_LINK_RGB444
			| MHL_DCAP_VID_LINK_PPIXEL
			| MHL_DCAP_VID_LINK_16BPP,
		[MHL_DCAP_AUD_LINK_MODE] = MHL_DCAP_AUD_LINK_2CH,
		[MHL_DCAP_VIDEO_TYPE] = MHL_DCAP_VT_GRAPHICS,
		[MHL_DCAP_LOG_DEV_MAP] = MHL_DCAP_LD_GUI,
		[MHL_DCAP_BANDWIDTH] = 0x0f,
		[MHL_DCAP_FEATURE_FLAG] = MHL_DCAP_FEATURE_RCP_SUPPORT
			| MHL_DCAP_FEATURE_RAP_SUPPORT
			| MHL_DCAP_FEATURE_SP_SUPPORT,
		[MHL_DCAP_SCRATCHPAD_SIZE] = SII8620_SCRATCHPAD_SIZE,
		[MHL_DCAP_INT_STAT_SIZE] = SII8620_INT_STAT_SIZE,
	};
	static const u8 xdcap[MHL_XDC_SIZE] = {
		[MHL_XDC_ECBUS_SPEEDS] = MHL_XDC_ECBUS_S_075
			| MHL_XDC_ECBUS_S_8BIT,
		[MHL_XDC_TMDS_SPEEDS] = MHL_XDC_TMDS_150
			| MHL_XDC_TMDS_300 | MHL_XDC_TMDS_600,
		[MHL_XDC_ECBUS_ROLES] = MHL_XDC_DEV_HOST,
		[MHL_XDC_LOG_DEV_MAPX] = MHL_XDC_LD_PHONE,
	};

	sii8620_write_buf(ctx, REG_MHL_DEVCAP_0, devcap, ARRAY_SIZE(devcap));
	sii8620_write_buf(ctx, REG_MHL_EXTDEVCAP_0, xdcap, ARRAY_SIZE(xdcap));
}

static void sii8620_mhl_init(struct sii8620 *ctx)
{
	sii8620_write_seq_static(ctx,
		REG_DISC_CTRL4, VAL_DISC_CTRL4(VAL_PUP_OFF, VAL_PUP_20K),
		REG_CBUS_MSC_COMPAT_CTRL,
			BIT_CBUS_MSC_COMPAT_CTRL_XDEVCAP_EN,
	);

	sii8620_peer_specific_init(ctx);

	sii8620_disable_hpd(ctx);

	sii8620_write_seq_static(ctx,
		REG_EDID_CTRL, BIT_EDID_CTRL_EDID_FIFO_ADDR_AUTO,
		REG_DISC_CTRL9, BIT_DISC_CTRL9_WAKE_DRVFLT
			| BIT_DISC_CTRL9_WAKE_PULSE_BYPASS,
		REG_TMDS0_CCTRL1, 0x90,
		REG_TMDS_CLK_EN, 0x01,
		REG_TMDS_CH_EN, 0x11,
		REG_BGR_BIAS, 0x87,
		REG_ALICE0_ZONE_CTRL, 0xE8,
		REG_ALICE0_MODE_CTRL, 0x04,
	);
	sii8620_setbits(ctx, REG_LM_DDC, BIT_LM_DDC_SW_TPI_EN_DISABLED, 0);
	sii8620_write_seq_static(ctx,
		REG_TPI_HW_OPT3, 0x76,
		REG_TMDS_CCTRL, BIT_TMDS_CCTRL_TMDS_OE,
		REG_TPI_DTD_B2, 79,
	);
	sii8620_set_dev_cap(ctx);
	sii8620_write_seq_static(ctx,
		REG_MDT_XMIT_TIMEOUT, 100,
		REG_MDT_XMIT_CTRL, 0x03,
		REG_MDT_XFIFO_STAT, 0x00,
		REG_MDT_RCV_TIMEOUT, 100,
		REG_CBUS_LINK_CTRL_8, 0x1D,
	);

	sii8620_start_gen2_write_burst(ctx);
	sii8620_write_seq_static(ctx,
		REG_BIST_CTRL, 0x00,
		REG_COC_CTL1, 0x10,
		REG_COC_CTL2, 0x18,
		REG_COC_CTLF, 0x07,
		REG_COC_CTL11, 0xF8,
		REG_COC_CTL17, 0x61,
		REG_COC_CTL18, 0x46,
		REG_COC_CTL19, 0x15,
		REG_COC_CTL1A, 0x01,
		REG_MHL_COC_CTL3, BIT_MHL_COC_CTL3_COC_AECHO_EN,
		REG_MHL_COC_CTL4, 0x2D,
		REG_MHL_COC_CTL5, 0xF9,
		REG_MSC_HEARTBEAT_CTRL, 0x27,
	);
	sii8620_disable_gen2_write_burst(ctx);

	sii8620_mt_write_stat(ctx, MHL_DST_REG(VERSION), SII8620_MHL_VERSION);
	sii8620_mt_write_stat(ctx, MHL_DST_REG(CONNECTED_RDY),
			      MHL_DST_CONN_DCAP_RDY | MHL_DST_CONN_XDEVCAPP_SUPP
			      | MHL_DST_CONN_POW_STAT);
	sii8620_mt_set_int(ctx, MHL_INT_REG(RCHANGE), MHL_INT_RC_DCAP_CHG);
}

static void sii8620_emsc_enable(struct sii8620 *ctx)
{
	u8 reg;

	sii8620_setbits(ctx, REG_GENCTL, BIT_GENCTL_EMSC_EN
					 | BIT_GENCTL_CLR_EMSC_RFIFO
					 | BIT_GENCTL_CLR_EMSC_XFIFO, ~0);
	sii8620_setbits(ctx, REG_GENCTL, BIT_GENCTL_CLR_EMSC_RFIFO
					 | BIT_GENCTL_CLR_EMSC_XFIFO, 0);
	sii8620_setbits(ctx, REG_COMMECNT, BIT_COMMECNT_I2C_TO_EMSC_EN, ~0);
	reg = sii8620_readb(ctx, REG_EMSCINTR);
	sii8620_write(ctx, REG_EMSCINTR, reg);
	sii8620_write(ctx, REG_EMSCINTRMASK, BIT_EMSCINTR_SPI_DVLD);
}

static int sii8620_wait_for_fsm_state(struct sii8620 *ctx, u8 state)
{
	int i;

	for (i = 0; i < 10; ++i) {
		u8 s = sii8620_readb(ctx, REG_COC_STAT_0);

		if ((s & MSK_COC_STAT_0_FSM_STATE) == state)
			return 0;
		if (!(s & BIT_COC_STAT_0_PLL_LOCKED))
			return -EBUSY;
		usleep_range(4000, 6000);
	}
	return -ETIMEDOUT;
}

static void sii8620_set_mode(struct sii8620 *ctx, enum sii8620_mode mode)
{
	int ret;

	if (ctx->mode == mode)
		return;

	switch (mode) {
	case CM_MHL1:
		sii8620_write_seq_static(ctx,
			REG_CBUS_MSC_COMPAT_CTRL, 0x02,
			REG_M3_CTRL, VAL_M3_CTRL_MHL1_2_VALUE,
			REG_DPD, BIT_DPD_PWRON_PLL | BIT_DPD_PDNTX12
				| BIT_DPD_OSC_EN,
			REG_COC_INTR_MASK, 0
		);
		ctx->mode = mode;
		break;
	case CM_MHL3:
		sii8620_write(ctx, REG_M3_CTRL, VAL_M3_CTRL_MHL3_VALUE);
		ctx->mode = mode;
		return;
	case CM_ECBUS_S:
		sii8620_emsc_enable(ctx);
		sii8620_write_seq_static(ctx,
			REG_TTXSPINUMS, 4,
			REG_TRXSPINUMS, 4,
			REG_TTXHSICNUMS, 0x14,
			REG_TRXHSICNUMS, 0x14,
			REG_TTXTOTNUMS, 0x18,
			REG_TRXTOTNUMS, 0x18,
			REG_PWD_SRST, BIT_PWD_SRST_COC_DOC_RST
				      | BIT_PWD_SRST_CBUS_RST_SW_EN,
			REG_MHL_COC_CTL1, 0xbd,
			REG_PWD_SRST, BIT_PWD_SRST_CBUS_RST_SW_EN,
			REG_COC_CTLB, 0x01,
			REG_COC_CTL0, 0x5c,
			REG_COC_CTL14, 0x03,
			REG_COC_CTL15, 0x80,
			REG_MHL_DP_CTL6, BIT_MHL_DP_CTL6_DP_TAP1_SGN
					 | BIT_MHL_DP_CTL6_DP_TAP1_EN
					 | BIT_MHL_DP_CTL6_DT_PREDRV_FEEDCAP_EN,
			REG_MHL_DP_CTL8, 0x03
		);
		ret = sii8620_wait_for_fsm_state(ctx, 0x03);
		sii8620_write_seq_static(ctx,
			REG_COC_CTL14, 0x00,
			REG_COC_CTL15, 0x80
		);
		if (!ret)
			sii8620_write(ctx, REG_CBUS3_CNVT, 0x85);
		else
			sii8620_disconnect(ctx);
		return;
	case CM_DISCONNECTED:
		ctx->mode = mode;
		break;
	default:
		dev_err(ctx->dev, "%s mode %d not supported\n", __func__, mode);
		break;
	}

	sii8620_set_auto_zone(ctx);

	if (mode != CM_MHL1)
		return;

	sii8620_write_seq_static(ctx,
		REG_MHL_DP_CTL0, 0xBC,
		REG_MHL_DP_CTL1, 0xBB,
		REG_MHL_DP_CTL3, 0x48,
		REG_MHL_DP_CTL5, 0x39,
		REG_MHL_DP_CTL2, 0x2A,
		REG_MHL_DP_CTL6, 0x2A,
		REG_MHL_DP_CTL7, 0x08
	);
}

static void sii8620_hpd_unplugged(struct sii8620 *ctx)
{
	sii8620_disable_hpd(ctx);
	ctx->sink_type = SINK_NONE;
	ctx->sink_detected = false;
	ctx->feature_complete = false;
	kfree(ctx->edid);
	ctx->edid = NULL;
}

static void sii8620_disconnect(struct sii8620 *ctx)
{
	sii8620_disable_gen2_write_burst(ctx);
	sii8620_stop_video(ctx);
	msleep(100);
	sii8620_cbus_reset(ctx);
	sii8620_set_mode(ctx, CM_DISCONNECTED);
	sii8620_write_seq_static(ctx,
		REG_TX_ZONE_CTL1, 0,
		REG_MHL_PLL_CTL0, 0x07,
		REG_COC_CTL0, 0x40,
		REG_CBUS3_CNVT, 0x84,
		REG_COC_CTL14, 0x00,
		REG_COC_CTL0, 0x40,
		REG_HRXCTRL3, 0x07,
		REG_MHL_PLL_CTL0, VAL_MHL_PLL_CTL0_HDMI_CLK_RATIO_1X
			| BIT_MHL_PLL_CTL0_CRYSTAL_CLK_SEL
			| BIT_MHL_PLL_CTL0_ZONE_MASK_OE,
		REG_MHL_DP_CTL0, BIT_MHL_DP_CTL0_DP_OE
			| BIT_MHL_DP_CTL0_TX_OE_OVR,
		REG_MHL_DP_CTL1, 0xBB,
		REG_MHL_DP_CTL3, 0x48,
		REG_MHL_DP_CTL5, 0x3F,
		REG_MHL_DP_CTL2, 0x2F,
		REG_MHL_DP_CTL6, 0x2A,
		REG_MHL_DP_CTL7, 0x03
	);
	sii8620_hpd_unplugged(ctx);
	sii8620_write_seq_static(ctx,
		REG_M3_CTRL, VAL_M3_CTRL_MHL3_VALUE,
		REG_MHL_COC_CTL1, 0x07,
		REG_DISC_CTRL4, VAL_DISC_CTRL4(VAL_PUP_OFF, VAL_PUP_20K),
		REG_DISC_CTRL8, 0x00,
		REG_DISC_CTRL9, BIT_DISC_CTRL9_WAKE_DRVFLT
			| BIT_DISC_CTRL9_WAKE_PULSE_BYPASS,
		REG_INT_CTRL, 0x00,
		REG_MSC_HEARTBEAT_CTRL, 0x27,
		REG_DISC_CTRL1, 0x25,
		REG_CBUS_DISC_INTR0, (u8)~BIT_RGND_READY_INT,
		REG_CBUS_DISC_INTR0_MASK, BIT_RGND_READY_INT,
		REG_MDT_INT_1, 0xff,
		REG_MDT_INT_1_MASK, 0x00,
		REG_MDT_INT_0, 0xff,
		REG_MDT_INT_0_MASK, 0x00,
		REG_COC_INTR, 0xff,
		REG_COC_INTR_MASK, 0x00,
		REG_TRXINTH, 0xff,
		REG_TRXINTMH, 0x00,
		REG_CBUS_INT_0, 0xff,
		REG_CBUS_INT_0_MASK, 0x00,
		REG_CBUS_INT_1, 0xff,
		REG_CBUS_INT_1_MASK, 0x00,
		REG_EMSCINTR, 0xff,
		REG_EMSCINTRMASK, 0x00,
		REG_EMSCINTR1, 0xff,
		REG_EMSCINTRMASK1, 0x00,
		REG_INTR8, 0xff,
		REG_INTR8_MASK, 0x00,
		REG_TPI_INTR_ST0, 0xff,
		REG_TPI_INTR_EN, 0x00,
		REG_HDCP2X_INTR0, 0xff,
		REG_HDCP2X_INTR0_MASK, 0x00,
		REG_INTR9, 0xff,
		REG_INTR9_MASK, 0x00,
		REG_INTR3, 0xff,
		REG_INTR3_MASK, 0x00,
		REG_INTR5, 0xff,
		REG_INTR5_MASK, 0x00,
		REG_INTR2, 0xff,
		REG_INTR2_MASK, 0x00,
	);
	memset(ctx->stat, 0, sizeof(ctx->stat));
	memset(ctx->xstat, 0, sizeof(ctx->xstat));
	memset(ctx->devcap, 0, sizeof(ctx->devcap));
	memset(ctx->xdevcap, 0, sizeof(ctx->xdevcap));
	ctx->devcap_read = false;
	ctx->cbus_status = 0;
	sii8620_mt_cleanup(ctx);
}

static void sii8620_mhl_disconnected(struct sii8620 *ctx)
{
	sii8620_write_seq_static(ctx,
		REG_DISC_CTRL4, VAL_DISC_CTRL4(VAL_PUP_OFF, VAL_PUP_20K),
		REG_CBUS_MSC_COMPAT_CTRL,
			BIT_CBUS_MSC_COMPAT_CTRL_XDEVCAP_EN
	);
	sii8620_disconnect(ctx);
}

static void sii8620_irq_disc(struct sii8620 *ctx)
{
	u8 stat = sii8620_readb(ctx, REG_CBUS_DISC_INTR0);

	if (stat & VAL_CBUS_MHL_DISCON)
		sii8620_mhl_disconnected(ctx);

	if (stat & BIT_RGND_READY_INT) {
		u8 stat2 = sii8620_readb(ctx, REG_DISC_STAT2);

		if ((stat2 & MSK_DISC_STAT2_RGND) == VAL_RGND_1K) {
			sii8620_mhl_discover(ctx);
		} else {
			sii8620_write_seq_static(ctx,
				REG_DISC_CTRL9, BIT_DISC_CTRL9_WAKE_DRVFLT
					| BIT_DISC_CTRL9_NOMHL_EST
					| BIT_DISC_CTRL9_WAKE_PULSE_BYPASS,
				REG_CBUS_DISC_INTR0_MASK, BIT_RGND_READY_INT
					| BIT_CBUS_MHL3_DISCON_INT
					| BIT_CBUS_MHL12_DISCON_INT
					| BIT_NOT_MHL_EST_INT
			);
		}
	}
	if (stat & BIT_MHL_EST_INT)
		sii8620_mhl_init(ctx);

	sii8620_write(ctx, REG_CBUS_DISC_INTR0, stat);
}

static void sii8620_read_burst(struct sii8620 *ctx)
{
	u8 buf[17];

	sii8620_read_buf(ctx, REG_MDT_RCV_READ_PORT, buf, ARRAY_SIZE(buf));
	sii8620_write(ctx, REG_MDT_RCV_CTRL, BIT_MDT_RCV_CTRL_MDT_RCV_EN |
		      BIT_MDT_RCV_CTRL_MDT_DELAY_RCV_EN |
		      BIT_MDT_RCV_CTRL_MDT_RFIFO_CLR_CUR);
	sii8620_readb(ctx, REG_MDT_RFIFO_STAT);
}

static void sii8620_irq_g2wb(struct sii8620 *ctx)
{
	u8 stat = sii8620_readb(ctx, REG_MDT_INT_0);

	if (stat & BIT_MDT_IDLE_AFTER_HAWB_DISABLE)
		if (sii8620_is_mhl3(ctx))
			sii8620_mt_set_int(ctx, MHL_INT_REG(RCHANGE),
				MHL_INT_RC_FEAT_COMPLETE);

	if (stat & BIT_MDT_RFIFO_DATA_RDY)
		sii8620_read_burst(ctx);

	if (stat & BIT_MDT_XFIFO_EMPTY)
		sii8620_write(ctx, REG_MDT_XMIT_CTRL, 0);

	sii8620_write(ctx, REG_MDT_INT_0, stat);
}

static void sii8620_status_dcap_ready(struct sii8620 *ctx)
{
	enum sii8620_mode mode;

	mode = ctx->stat[MHL_DST_VERSION] >= 0x30 ? CM_MHL3 : CM_MHL1;
	if (mode > ctx->mode)
		sii8620_set_mode(ctx, mode);
	sii8620_peer_specific_init(ctx);
	sii8620_write(ctx, REG_INTR9_MASK, BIT_INTR9_DEVCAP_DONE
		      | BIT_INTR9_EDID_DONE | BIT_INTR9_EDID_ERROR);
}

static void sii8620_status_changed_path(struct sii8620 *ctx)
{
	u8 link_mode;

	if (ctx->use_packed_pixel)
		link_mode = MHL_DST_LM_CLK_MODE_PACKED_PIXEL;
	else
		link_mode = MHL_DST_LM_CLK_MODE_NORMAL;

	if (ctx->stat[MHL_DST_LINK_MODE] & MHL_DST_LM_PATH_ENABLED)
		link_mode |= MHL_DST_LM_PATH_ENABLED;

	sii8620_mt_write_stat(ctx, MHL_DST_REG(LINK_MODE),
			      link_mode);
}

static void sii8620_msc_mr_write_stat(struct sii8620 *ctx)
{
	u8 st[MHL_DST_SIZE], xst[MHL_XDS_SIZE];

	sii8620_read_buf(ctx, REG_MHL_STAT_0, st, MHL_DST_SIZE);
	sii8620_read_buf(ctx, REG_MHL_EXTSTAT_0, xst, MHL_XDS_SIZE);

	sii8620_update_array(ctx->stat, st, MHL_DST_SIZE);
	sii8620_update_array(ctx->xstat, xst, MHL_XDS_SIZE);

	if (ctx->stat[MHL_DST_CONNECTED_RDY] & st[MHL_DST_CONNECTED_RDY] &
	    MHL_DST_CONN_DCAP_RDY) {
		sii8620_status_dcap_ready(ctx);

		if (!sii8620_is_mhl3(ctx))
			sii8620_mt_read_devcap(ctx, false);
	}

	if (st[MHL_DST_LINK_MODE] & MHL_DST_LM_PATH_ENABLED)
		sii8620_status_changed_path(ctx);
}

static void sii8620_ecbus_up(struct sii8620 *ctx, int ret)
{
	if (ret < 0)
		return;

	sii8620_set_mode(ctx, CM_ECBUS_S);
}

static void sii8620_got_ecbus_speed(struct sii8620 *ctx, int ret)
{
	if (ret < 0)
		return;

	sii8620_mt_write_stat(ctx, MHL_XDS_REG(CURR_ECBUS_MODE),
			      MHL_XDS_ECBUS_S | MHL_XDS_SLOT_MODE_8BIT);
	sii8620_mt_rap(ctx, MHL_RAP_CBUS_MODE_UP);
	sii8620_mt_set_cont(ctx, sii8620_ecbus_up);
}

static void sii8620_mhl_burst_emsc_support_set(struct mhl_burst_emsc_support *d,
	enum mhl_burst_id id)
{
	sii8620_mhl_burst_hdr_set(&d->hdr, MHL_BURST_ID_EMSC_SUPPORT);
	d->num_entries = 1;
	d->burst_id[0] = cpu_to_be16(id);
}

static void sii8620_send_features(struct sii8620 *ctx)
{
	u8 buf[16];

	sii8620_write(ctx, REG_MDT_XMIT_CTRL, BIT_MDT_XMIT_CTRL_EN
		| BIT_MDT_XMIT_CTRL_FIXED_BURST_LEN);
	sii8620_mhl_burst_emsc_support_set((void *)buf,
		MHL_BURST_ID_HID_PAYLOAD);
	sii8620_write_buf(ctx, REG_MDT_XMIT_WRITE_PORT, buf, ARRAY_SIZE(buf));
}

static bool sii8620_rcp_consume(struct sii8620 *ctx, u8 scancode)
{
	bool pressed = !(scancode & MHL_RCP_KEY_RELEASED_MASK);

	scancode &= MHL_RCP_KEY_ID_MASK;

	if (!IS_ENABLED(CONFIG_RC_CORE) || !ctx->rc_dev)
		return false;

	if (pressed)
		rc_keydown(ctx->rc_dev, RC_PROTO_CEC, scancode, 0);
	else
		rc_keyup(ctx->rc_dev);

	return true;
}

static void sii8620_msc_mr_set_int(struct sii8620 *ctx)
{
	u8 ints[MHL_INT_SIZE];

	sii8620_read_buf(ctx, REG_MHL_INT_0, ints, MHL_INT_SIZE);
	sii8620_write_buf(ctx, REG_MHL_INT_0, ints, MHL_INT_SIZE);

	if (ints[MHL_INT_RCHANGE] & MHL_INT_RC_DCAP_CHG) {
		switch (ctx->mode) {
		case CM_MHL3:
			sii8620_mt_read_xdevcap_reg(ctx, MHL_XDC_ECBUS_SPEEDS);
			sii8620_mt_set_cont(ctx, sii8620_got_ecbus_speed);
			break;
		case CM_ECBUS_S:
			sii8620_mt_read_devcap(ctx, true);
			break;
		default:
			break;
		}
	}
	if (ints[MHL_INT_RCHANGE] & MHL_INT_RC_FEAT_REQ)
		sii8620_send_features(ctx);
	if (ints[MHL_INT_RCHANGE] & MHL_INT_RC_FEAT_COMPLETE) {
		ctx->feature_complete = true;
		if (ctx->edid)
			sii8620_enable_hpd(ctx);
	}
}

static struct sii8620_mt_msg *sii8620_msc_msg_first(struct sii8620 *ctx)
{
	struct device *dev = ctx->dev;

	if (list_empty(&ctx->mt_queue)) {
		dev_err(dev, "unexpected MSC MT response\n");
		return NULL;
	}

	return list_first_entry(&ctx->mt_queue, struct sii8620_mt_msg, node);
}

static void sii8620_msc_mt_done(struct sii8620 *ctx)
{
	struct sii8620_mt_msg *msg = sii8620_msc_msg_first(ctx);

	if (!msg)
		return;

	msg->ret = sii8620_readb(ctx, REG_MSC_MT_RCVD_DATA0);
	ctx->mt_state = MT_STATE_DONE;
}

static void sii8620_msc_mr_msc_msg(struct sii8620 *ctx)
{
	struct sii8620_mt_msg *msg;
	u8 buf[2];

	sii8620_read_buf(ctx, REG_MSC_MR_MSC_MSG_RCVD_1ST_DATA, buf, 2);

	switch (buf[0]) {
	case MHL_MSC_MSG_RAPK:
		msg = sii8620_msc_msg_first(ctx);
		if (!msg)
			return;
		msg->ret = buf[1];
		ctx->mt_state = MT_STATE_DONE;
		break;
	case MHL_MSC_MSG_RCP:
		if (!sii8620_rcp_consume(ctx, buf[1]))
			sii8620_mt_rcpe(ctx,
					MHL_RCPE_STATUS_INEFFECTIVE_KEY_CODE);
		sii8620_mt_rcpk(ctx, buf[1]);
		break;
	default:
		dev_err(ctx->dev, "%s message type %d,%d not supported",
			__func__, buf[0], buf[1]);
	}
}

static void sii8620_irq_msc(struct sii8620 *ctx)
{
	u8 stat = sii8620_readb(ctx, REG_CBUS_INT_0);

	if (stat & ~BIT_CBUS_HPD_CHG)
		sii8620_write(ctx, REG_CBUS_INT_0, stat & ~BIT_CBUS_HPD_CHG);

	if (stat & BIT_CBUS_HPD_CHG) {
		u8 cbus_stat = sii8620_readb(ctx, REG_CBUS_STATUS);

		if ((cbus_stat ^ ctx->cbus_status) & BIT_CBUS_STATUS_CBUS_HPD) {
			sii8620_write(ctx, REG_CBUS_INT_0, BIT_CBUS_HPD_CHG);
		} else {
			stat ^= BIT_CBUS_STATUS_CBUS_HPD;
			cbus_stat ^= BIT_CBUS_STATUS_CBUS_HPD;
		}
		ctx->cbus_status = cbus_stat;
	}

	if (stat & BIT_CBUS_MSC_MR_WRITE_STAT)
		sii8620_msc_mr_write_stat(ctx);

	if (stat & BIT_CBUS_HPD_CHG) {
		if (ctx->cbus_status & BIT_CBUS_STATUS_CBUS_HPD) {
			ctx->sink_detected = true;
			sii8620_identify_sink(ctx);
		} else {
			sii8620_hpd_unplugged(ctx);
		}
	}

	if (stat & BIT_CBUS_MSC_MR_SET_INT)
		sii8620_msc_mr_set_int(ctx);

	if (stat & BIT_CBUS_MSC_MT_DONE)
		sii8620_msc_mt_done(ctx);

	if (stat & BIT_CBUS_MSC_MR_MSC_MSG)
		sii8620_msc_mr_msc_msg(ctx);
}

static void sii8620_irq_coc(struct sii8620 *ctx)
{
	u8 stat = sii8620_readb(ctx, REG_COC_INTR);

	if (stat & BIT_COC_CALIBRATION_DONE) {
		u8 cstat = sii8620_readb(ctx, REG_COC_STAT_0);

		cstat &= BIT_COC_STAT_0_PLL_LOCKED | MSK_COC_STAT_0_FSM_STATE;
		if (cstat == (BIT_COC_STAT_0_PLL_LOCKED | 0x02)) {
			sii8620_write_seq_static(ctx,
				REG_COC_CTLB, 0,
				REG_TRXINTMH, BIT_TDM_INTR_SYNC_DATA
					      | BIT_TDM_INTR_SYNC_WAIT
			);
		}
	}

	sii8620_write(ctx, REG_COC_INTR, stat);
}

static void sii8620_irq_merr(struct sii8620 *ctx)
{
	u8 stat = sii8620_readb(ctx, REG_CBUS_INT_1);

	sii8620_write(ctx, REG_CBUS_INT_1, stat);
}

static void sii8620_irq_edid(struct sii8620 *ctx)
{
	u8 stat = sii8620_readb(ctx, REG_INTR9);

	sii8620_write(ctx, REG_INTR9, stat);

	if (stat & BIT_INTR9_DEVCAP_DONE)
		ctx->mt_state = MT_STATE_DONE;
}

static void sii8620_irq_scdt(struct sii8620 *ctx)
{
	u8 stat = sii8620_readb(ctx, REG_INTR5);

	if (stat & BIT_INTR_SCDT_CHANGE) {
		u8 cstat = sii8620_readb(ctx, REG_TMDS_CSTAT_P3);

		if (cstat & BIT_TMDS_CSTAT_P3_SCDT)
			sii8620_start_video(ctx);
	}

	sii8620_write(ctx, REG_INTR5, stat);
}

static void sii8620_got_xdevcap(struct sii8620 *ctx, int ret)
{
	if (ret < 0)
		return;

	sii8620_mt_read_devcap(ctx, false);
}

static void sii8620_irq_tdm(struct sii8620 *ctx)
{
	u8 stat = sii8620_readb(ctx, REG_TRXINTH);
	u8 tdm = sii8620_readb(ctx, REG_TRXSTA2);

	if ((tdm & MSK_TDM_SYNCHRONIZED) == VAL_TDM_SYNCHRONIZED) {
		ctx->mode = CM_ECBUS_S;
		ctx->burst.rx_ack = 0;
		ctx->burst.r_size = SII8620_BURST_BUF_LEN;
		sii8620_burst_tx_rbuf_info(ctx, SII8620_BURST_BUF_LEN);
		sii8620_mt_read_devcap(ctx, true);
		sii8620_mt_set_cont(ctx, sii8620_got_xdevcap);
	} else {
		sii8620_write_seq_static(ctx,
			REG_MHL_PLL_CTL2, 0,
			REG_MHL_PLL_CTL2, BIT_MHL_PLL_CTL2_CLKDETECT_EN
		);
	}

	sii8620_write(ctx, REG_TRXINTH, stat);
}

static void sii8620_irq_block(struct sii8620 *ctx)
{
	u8 stat = sii8620_readb(ctx, REG_EMSCINTR);

	if (stat & BIT_EMSCINTR_SPI_DVLD) {
		u8 bstat = sii8620_readb(ctx, REG_SPIBURSTSTAT);

		if (bstat & BIT_SPIBURSTSTAT_EMSC_NORMAL_MODE)
			sii8620_burst_receive(ctx);
	}

	sii8620_write(ctx, REG_EMSCINTR, stat);
}

static void sii8620_irq_ddc(struct sii8620 *ctx)
{
	u8 stat = sii8620_readb(ctx, REG_INTR3);

	if (stat & BIT_DDC_CMD_DONE) {
		sii8620_write(ctx, REG_INTR3_MASK, 0);
		if (sii8620_is_mhl3(ctx) && !ctx->feature_complete)
			sii8620_mt_set_int(ctx, MHL_INT_REG(RCHANGE),
					   MHL_INT_RC_FEAT_REQ);
		else
			sii8620_enable_hpd(ctx);
	}
	sii8620_write(ctx, REG_INTR3, stat);
}

/* endian agnostic, non-volatile version of test_bit */
static bool sii8620_test_bit(unsigned int nr, const u8 *addr)
{
	return 1 & (addr[nr / BITS_PER_BYTE] >> (nr % BITS_PER_BYTE));
}

static irqreturn_t sii8620_irq_thread(int irq, void *data)
{
	static const struct {
		int bit;
		void (*handler)(struct sii8620 *ctx);
	} irq_vec[] = {
		{ BIT_FAST_INTR_STAT_DISC, sii8620_irq_disc },
		{ BIT_FAST_INTR_STAT_G2WB, sii8620_irq_g2wb },
		{ BIT_FAST_INTR_STAT_COC, sii8620_irq_coc },
		{ BIT_FAST_INTR_STAT_TDM, sii8620_irq_tdm },
		{ BIT_FAST_INTR_STAT_MSC, sii8620_irq_msc },
		{ BIT_FAST_INTR_STAT_MERR, sii8620_irq_merr },
		{ BIT_FAST_INTR_STAT_BLOCK, sii8620_irq_block },
		{ BIT_FAST_INTR_STAT_EDID, sii8620_irq_edid },
		{ BIT_FAST_INTR_STAT_DDC, sii8620_irq_ddc },
		{ BIT_FAST_INTR_STAT_SCDT, sii8620_irq_scdt },
	};
	struct sii8620 *ctx = data;
	u8 stats[LEN_FAST_INTR_STAT];
	int i, ret;

	mutex_lock(&ctx->lock);

	sii8620_read_buf(ctx, REG_FAST_INTR_STAT, stats, ARRAY_SIZE(stats));
	for (i = 0; i < ARRAY_SIZE(irq_vec); ++i)
		if (sii8620_test_bit(irq_vec[i].bit, stats))
			irq_vec[i].handler(ctx);

	sii8620_burst_rx_all(ctx);
	sii8620_mt_work(ctx);
	sii8620_burst_send(ctx);

	ret = sii8620_clear_error(ctx);
	if (ret) {
		dev_err(ctx->dev, "Error during IRQ handling, %d.\n", ret);
		sii8620_mhl_disconnected(ctx);
	}
	mutex_unlock(&ctx->lock);

	return IRQ_HANDLED;
}

static void sii8620_cable_in(struct sii8620 *ctx)
{
	struct device *dev = ctx->dev;
	u8 ver[5];
	int ret;

	ret = sii8620_hw_on(ctx);
	if (ret) {
		dev_err(dev, "Error powering on, %d.\n", ret);
		return;
	}

	sii8620_read_buf(ctx, REG_VND_IDL, ver, ARRAY_SIZE(ver));
	ret = sii8620_clear_error(ctx);
	if (ret) {
		dev_err(dev, "Error accessing I2C bus, %d.\n", ret);
		return;
	}

	dev_info(dev, "ChipID %02x%02x:%02x%02x rev %02x.\n", ver[1], ver[0],
		 ver[3], ver[2], ver[4]);

	sii8620_write(ctx, REG_DPD,
		      BIT_DPD_PWRON_PLL | BIT_DPD_PDNTX12 | BIT_DPD_OSC_EN);

	sii8620_xtal_set_rate(ctx);
	sii8620_disconnect(ctx);

	sii8620_write_seq_static(ctx,
		REG_MHL_CBUS_CTL0, VAL_MHL_CBUS_CTL0_CBUS_DRV_SEL_STRONG
			| VAL_MHL_CBUS_CTL0_CBUS_RGND_VBIAS_734,
		REG_MHL_CBUS_CTL1, VAL_MHL_CBUS_CTL1_1115_OHM,
		REG_DPD, BIT_DPD_PWRON_PLL | BIT_DPD_PDNTX12 | BIT_DPD_OSC_EN,
	);

	ret = sii8620_clear_error(ctx);
	if (ret) {
		dev_err(dev, "Error accessing I2C bus, %d.\n", ret);
		return;
	}

	enable_irq(to_i2c_client(ctx->dev)->irq);
}

static void sii8620_init_rcp_input_dev(struct sii8620 *ctx)
{
	struct rc_dev *rc_dev;
	int ret;

	if (!IS_ENABLED(CONFIG_RC_CORE))
		return;

	rc_dev = rc_allocate_device(RC_DRIVER_SCANCODE);
	if (!rc_dev) {
		dev_err(ctx->dev, "Failed to allocate RC device\n");
		ctx->error = -ENOMEM;
		return;
	}

	rc_dev->input_phys = "sii8620/input0";
	rc_dev->input_id.bustype = BUS_VIRTUAL;
	rc_dev->map_name = RC_MAP_CEC;
	rc_dev->allowed_protocols = RC_PROTO_BIT_CEC;
	rc_dev->driver_name = "sii8620";
	rc_dev->device_name = "sii8620";

	ret = rc_register_device(rc_dev);

	if (ret) {
		dev_err(ctx->dev, "Failed to register RC device\n");
		ctx->error = ret;
		rc_free_device(ctx->rc_dev);
		return;
	}
	ctx->rc_dev = rc_dev;
}

static void sii8620_cable_out(struct sii8620 *ctx)
{
	disable_irq(to_i2c_client(ctx->dev)->irq);
	sii8620_hw_off(ctx);
}

static void sii8620_extcon_work(struct work_struct *work)
{
	struct sii8620 *ctx =
		container_of(work, struct sii8620, extcon_wq);
	int state = extcon_get_state(ctx->extcon, EXTCON_DISP_MHL);

	if (state == ctx->cable_state)
		return;

	ctx->cable_state = state;

	if (state > 0)
		sii8620_cable_in(ctx);
	else
		sii8620_cable_out(ctx);
}

static int sii8620_extcon_notifier(struct notifier_block *self,
			unsigned long event, void *ptr)
{
	struct sii8620 *ctx =
		container_of(self, struct sii8620, extcon_nb);

	schedule_work(&ctx->extcon_wq);

	return NOTIFY_DONE;
}

static int sii8620_extcon_init(struct sii8620 *ctx)
{
	struct extcon_dev *edev;
	struct device_node *musb, *muic;
	int ret;

	/* get micro-USB connector node */
	musb = of_graph_get_remote_node(ctx->dev->of_node, 1, -1);
	/* next get micro-USB Interface Controller node */
	muic = of_get_next_parent(musb);

	if (!muic) {
		dev_info(ctx->dev, "no extcon found, switching to 'always on' mode\n");
		return 0;
	}

	edev = extcon_find_edev_by_node(muic);
	of_node_put(muic);
	if (IS_ERR(edev)) {
		if (PTR_ERR(edev) == -EPROBE_DEFER)
			return -EPROBE_DEFER;
		dev_err(ctx->dev, "Invalid or missing extcon\n");
		return PTR_ERR(edev);
	}

	ctx->extcon = edev;
	ctx->extcon_nb.notifier_call = sii8620_extcon_notifier;
	INIT_WORK(&ctx->extcon_wq, sii8620_extcon_work);
	ret = extcon_register_notifier(edev, EXTCON_DISP_MHL, &ctx->extcon_nb);
	if (ret) {
		dev_err(ctx->dev, "failed to register notifier for MHL\n");
		return ret;
	}

	return 0;
}

static inline struct sii8620 *bridge_to_sii8620(struct drm_bridge *bridge)
{
	return container_of(bridge, struct sii8620, bridge);
}

static int sii8620_attach(struct drm_bridge *bridge,
			  enum drm_bridge_attach_flags flags)
{
	struct sii8620 *ctx = bridge_to_sii8620(bridge);

	sii8620_init_rcp_input_dev(ctx);

	return sii8620_clear_error(ctx);
}

static void sii8620_detach(struct drm_bridge *bridge)
{
	struct sii8620 *ctx = bridge_to_sii8620(bridge);

	if (!IS_ENABLED(CONFIG_RC_CORE))
		return;

	rc_unregister_device(ctx->rc_dev);
}

static int sii8620_is_packing_required(struct sii8620 *ctx,
				       const struct drm_display_mode *mode)
{
	int max_pclk, max_pclk_pp_mode;

	if (sii8620_is_mhl3(ctx)) {
		max_pclk = MHL3_MAX_PCLK;
		max_pclk_pp_mode = MHL3_MAX_PCLK_PP_MODE;
	} else {
		max_pclk = MHL1_MAX_PCLK;
		max_pclk_pp_mode = MHL1_MAX_PCLK_PP_MODE;
	}

	if (mode->clock < max_pclk)
		return 0;
	else if (mode->clock < max_pclk_pp_mode)
		return 1;
	else
		return -1;
}

static enum drm_mode_status sii8620_mode_valid(struct drm_bridge *bridge,
					 const struct drm_display_info *info,
					 const struct drm_display_mode *mode)
{
	struct sii8620 *ctx = bridge_to_sii8620(bridge);
	int pack_required = sii8620_is_packing_required(ctx, mode);
	bool can_pack = ctx->devcap[MHL_DCAP_VID_LINK_MODE] &
			MHL_DCAP_VID_LINK_PPIXEL;

	switch (pack_required) {
	case 0:
		return MODE_OK;
	case 1:
		return (can_pack) ? MODE_OK : MODE_CLOCK_HIGH;
	default:
		return MODE_CLOCK_HIGH;
	}
}

static bool sii8620_mode_fixup(struct drm_bridge *bridge,
			       const struct drm_display_mode *mode,
			       struct drm_display_mode *adjusted_mode)
{
	struct sii8620 *ctx = bridge_to_sii8620(bridge);

	mutex_lock(&ctx->lock);

	ctx->use_packed_pixel = sii8620_is_packing_required(ctx, adjusted_mode);

	mutex_unlock(&ctx->lock);

	return true;
}

static const struct drm_bridge_funcs sii8620_bridge_funcs = {
	.attach = sii8620_attach,
	.detach = sii8620_detach,
	.mode_fixup = sii8620_mode_fixup,
	.mode_valid = sii8620_mode_valid,
};

static int sii8620_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct sii8620 *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->dev = dev;
	mutex_init(&ctx->lock);
	INIT_LIST_HEAD(&ctx->mt_queue);

	ctx->clk_xtal = devm_clk_get(dev, "xtal");
	if (IS_ERR(ctx->clk_xtal)) {
		dev_err(dev, "failed to get xtal clock from DT\n");
		return PTR_ERR(ctx->clk_xtal);
	}

	if (!client->irq) {
		dev_err(dev, "no irq provided\n");
		return -EINVAL;
	}
	irq_set_status_flags(client->irq, IRQ_NOAUTOEN);
	ret = devm_request_threaded_irq(dev, client->irq, NULL,
					sii8620_irq_thread,
					IRQF_TRIGGER_HIGH | IRQF_ONESHOT,
					"sii8620", ctx);
	if (ret < 0) {
		dev_err(dev, "failed to install IRQ handler\n");
		return ret;
	}

	ctx->gpio_reset = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->gpio_reset)) {
		dev_err(dev, "failed to get reset gpio from DT\n");
		return PTR_ERR(ctx->gpio_reset);
	}

	ctx->supplies[0].supply = "cvcc10";
	ctx->supplies[1].supply = "iovcc18";
	ret = devm_regulator_bulk_get(dev, 2, ctx->supplies);
	if (ret)
		return ret;

	ret = sii8620_extcon_init(ctx);
	if (ret < 0) {
		dev_err(ctx->dev, "failed to initialize EXTCON\n");
		return ret;
	}

	i2c_set_clientdata(client, ctx);

	ctx->bridge.funcs = &sii8620_bridge_funcs;
	ctx->bridge.of_node = dev->of_node;
	drm_bridge_add(&ctx->bridge);

	if (!ctx->extcon)
		sii8620_cable_in(ctx);

	return 0;
}

static int sii8620_remove(struct i2c_client *client)
{
	struct sii8620 *ctx = i2c_get_clientdata(client);

	if (ctx->extcon) {
		extcon_unregister_notifier(ctx->extcon, EXTCON_DISP_MHL,
					   &ctx->extcon_nb);
		flush_work(&ctx->extcon_wq);
		if (ctx->cable_state > 0)
			sii8620_cable_out(ctx);
	} else {
		sii8620_cable_out(ctx);
	}
	drm_bridge_remove(&ctx->bridge);

	return 0;
}

static const struct of_device_id sii8620_dt_match[] = {
	{ .compatible = "sil,sii8620" },
	{ },
};
MODULE_DEVICE_TABLE(of, sii8620_dt_match);

static const struct i2c_device_id sii8620_id[] = {
	{ "sii8620", 0 },
	{ },
};

MODULE_DEVICE_TABLE(i2c, sii8620_id);
static struct i2c_driver sii8620_driver = {
	.driver = {
		.name	= "sii8620",
		.of_match_table = of_match_ptr(sii8620_dt_match),
	},
	.probe		= sii8620_probe,
	.remove		= sii8620_remove,
	.id_table = sii8620_id,
};

module_i2c_driver(sii8620_driver);
MODULE_LICENSE("GPL v2");
