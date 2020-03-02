// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2012 - 2018 Microchip Technology Inc., and its subsidiaries.
 * All rights reserved.
 */

#include <linux/clk.h>
#include <linux/spi/spi.h>

#include "netdev.h"
#include "cfg80211.h"

struct wilc_spi {
	int crc_off;
};

static const struct wilc_hif_func wilc_hif_spi;

/********************************************
 *
 *      Crc7
 *
 ********************************************/

static const u8 crc7_syndrome_table[256] = {
	0x00, 0x09, 0x12, 0x1b, 0x24, 0x2d, 0x36, 0x3f,
	0x48, 0x41, 0x5a, 0x53, 0x6c, 0x65, 0x7e, 0x77,
	0x19, 0x10, 0x0b, 0x02, 0x3d, 0x34, 0x2f, 0x26,
	0x51, 0x58, 0x43, 0x4a, 0x75, 0x7c, 0x67, 0x6e,
	0x32, 0x3b, 0x20, 0x29, 0x16, 0x1f, 0x04, 0x0d,
	0x7a, 0x73, 0x68, 0x61, 0x5e, 0x57, 0x4c, 0x45,
	0x2b, 0x22, 0x39, 0x30, 0x0f, 0x06, 0x1d, 0x14,
	0x63, 0x6a, 0x71, 0x78, 0x47, 0x4e, 0x55, 0x5c,
	0x64, 0x6d, 0x76, 0x7f, 0x40, 0x49, 0x52, 0x5b,
	0x2c, 0x25, 0x3e, 0x37, 0x08, 0x01, 0x1a, 0x13,
	0x7d, 0x74, 0x6f, 0x66, 0x59, 0x50, 0x4b, 0x42,
	0x35, 0x3c, 0x27, 0x2e, 0x11, 0x18, 0x03, 0x0a,
	0x56, 0x5f, 0x44, 0x4d, 0x72, 0x7b, 0x60, 0x69,
	0x1e, 0x17, 0x0c, 0x05, 0x3a, 0x33, 0x28, 0x21,
	0x4f, 0x46, 0x5d, 0x54, 0x6b, 0x62, 0x79, 0x70,
	0x07, 0x0e, 0x15, 0x1c, 0x23, 0x2a, 0x31, 0x38,
	0x41, 0x48, 0x53, 0x5a, 0x65, 0x6c, 0x77, 0x7e,
	0x09, 0x00, 0x1b, 0x12, 0x2d, 0x24, 0x3f, 0x36,
	0x58, 0x51, 0x4a, 0x43, 0x7c, 0x75, 0x6e, 0x67,
	0x10, 0x19, 0x02, 0x0b, 0x34, 0x3d, 0x26, 0x2f,
	0x73, 0x7a, 0x61, 0x68, 0x57, 0x5e, 0x45, 0x4c,
	0x3b, 0x32, 0x29, 0x20, 0x1f, 0x16, 0x0d, 0x04,
	0x6a, 0x63, 0x78, 0x71, 0x4e, 0x47, 0x5c, 0x55,
	0x22, 0x2b, 0x30, 0x39, 0x06, 0x0f, 0x14, 0x1d,
	0x25, 0x2c, 0x37, 0x3e, 0x01, 0x08, 0x13, 0x1a,
	0x6d, 0x64, 0x7f, 0x76, 0x49, 0x40, 0x5b, 0x52,
	0x3c, 0x35, 0x2e, 0x27, 0x18, 0x11, 0x0a, 0x03,
	0x74, 0x7d, 0x66, 0x6f, 0x50, 0x59, 0x42, 0x4b,
	0x17, 0x1e, 0x05, 0x0c, 0x33, 0x3a, 0x21, 0x28,
	0x5f, 0x56, 0x4d, 0x44, 0x7b, 0x72, 0x69, 0x60,
	0x0e, 0x07, 0x1c, 0x15, 0x2a, 0x23, 0x38, 0x31,
	0x46, 0x4f, 0x54, 0x5d, 0x62, 0x6b, 0x70, 0x79
};

static u8 crc7_byte(u8 crc, u8 data)
{
	return crc7_syndrome_table[(crc << 1) ^ data];
}

static u8 crc7(u8 crc, const u8 *buffer, u32 len)
{
	while (len--)
		crc = crc7_byte(crc, *buffer++);
	return crc;
}

static u8 wilc_get_crc7(u8 *buffer, u32 len)
{
	return crc7(0x7f, (const u8 *)buffer, len) << 1;
}

/********************************************
 *
 *      Spi protocol Function
 *
 ********************************************/

#define CMD_DMA_WRITE				0xc1
#define CMD_DMA_READ				0xc2
#define CMD_INTERNAL_WRITE			0xc3
#define CMD_INTERNAL_READ			0xc4
#define CMD_TERMINATE				0xc5
#define CMD_REPEAT				0xc6
#define CMD_DMA_EXT_WRITE			0xc7
#define CMD_DMA_EXT_READ			0xc8
#define CMD_SINGLE_WRITE			0xc9
#define CMD_SINGLE_READ				0xca
#define CMD_RESET				0xcf

#define DATA_PKT_SZ_256				256
#define DATA_PKT_SZ_512				512
#define DATA_PKT_SZ_1K				1024
#define DATA_PKT_SZ_4K				(4 * 1024)
#define DATA_PKT_SZ_8K				(8 * 1024)
#define DATA_PKT_SZ				DATA_PKT_SZ_8K

#define USE_SPI_DMA				0

#define WILC_SPI_COMMAND_STAT_SUCCESS		0
#define WILC_GET_RESP_HDR_START(h)		(((h) >> 4) & 0xf)

struct wilc_spi_cmd {
	u8 cmd_type;
	union {
		struct {
			u8 addr[3];
			u8 crc[];
		} __packed simple_cmd;
		struct {
			u8 addr[3];
			u8 size[2];
			u8 crc[];
		} __packed dma_cmd;
		struct {
			u8 addr[3];
			u8 size[3];
			u8 crc[];
		} __packed dma_cmd_ext;
		struct {
			u8 addr[2];
			__be32 data;
			u8 crc[];
		} __packed internal_w_cmd;
		struct {
			u8 addr[3];
			__be32 data;
			u8 crc[];
		} __packed w_cmd;
	} u;
} __packed;

struct wilc_spi_read_rsp_data {
	u8 rsp_cmd_type;
	u8 status;
	u8 resp_header;
	u8 resp_data[4];
	u8 crc[];
} __packed;

struct wilc_spi_rsp_data {
	u8 rsp_cmd_type;
	u8 status;
} __packed;

static int wilc_bus_probe(struct spi_device *spi)
{
	int ret;
	struct wilc *wilc;
	struct gpio_desc *gpio;
	struct wilc_spi *spi_priv;

	spi_priv = kzalloc(sizeof(*spi_priv), GFP_KERNEL);
	if (!spi_priv)
		return -ENOMEM;

	gpio = gpiod_get(&spi->dev, "irq", GPIOD_IN);
	if (IS_ERR(gpio)) {
		/* get the GPIO descriptor from hardcode GPIO number */
		gpio = gpio_to_desc(GPIO_NUM);
		if (!gpio)
			dev_err(&spi->dev, "failed to get the irq gpio\n");
	}

	ret = wilc_cfg80211_init(&wilc, &spi->dev, WILC_HIF_SPI, &wilc_hif_spi);
	if (ret) {
		kfree(spi_priv);
		return ret;
	}

	spi_set_drvdata(spi, wilc);
	wilc->dev = &spi->dev;
	wilc->bus_data = spi_priv;
	wilc->gpio_irq = gpio;

	wilc->rtc_clk = devm_clk_get(&spi->dev, "rtc_clk");
	if (PTR_ERR_OR_ZERO(wilc->rtc_clk) == -EPROBE_DEFER)
		return -EPROBE_DEFER;
	else if (!IS_ERR(wilc->rtc_clk))
		clk_prepare_enable(wilc->rtc_clk);

	return 0;
}

static int wilc_bus_remove(struct spi_device *spi)
{
	struct wilc *wilc = spi_get_drvdata(spi);

	/* free the GPIO in module remove */
	if (wilc->gpio_irq)
		gpiod_put(wilc->gpio_irq);

	if (!IS_ERR(wilc->rtc_clk))
		clk_disable_unprepare(wilc->rtc_clk);

	wilc_netdev_cleanup(wilc);
	return 0;
}

static const struct of_device_id wilc_of_match[] = {
	{ .compatible = "microchip,wilc1000-spi", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, wilc_of_match);

static struct spi_driver wilc_spi_driver = {
	.driver = {
		.name = MODALIAS,
		.of_match_table = wilc_of_match,
	},
	.probe =  wilc_bus_probe,
	.remove = wilc_bus_remove,
};
module_spi_driver(wilc_spi_driver);
MODULE_LICENSE("GPL");

static int wilc_spi_tx(struct wilc *wilc, u8 *b, u32 len)
{
	struct spi_device *spi = to_spi_device(wilc->dev);
	int ret;
	struct spi_message msg;

	if (len > 0 && b) {
		struct spi_transfer tr = {
			.tx_buf = b,
			.len = len,
			.delay_usecs = 0,
		};
		char *r_buffer = kzalloc(len, GFP_KERNEL);

		if (!r_buffer)
			return -ENOMEM;

		tr.rx_buf = r_buffer;
		dev_dbg(&spi->dev, "Request writing %d bytes\n", len);

		memset(&msg, 0, sizeof(msg));
		spi_message_init(&msg);
		msg.spi = spi;
		msg.is_dma_mapped = USE_SPI_DMA;
		spi_message_add_tail(&tr, &msg);

		ret = spi_sync(spi, &msg);
		if (ret < 0)
			dev_err(&spi->dev, "SPI transaction failed\n");

		kfree(r_buffer);
	} else {
		dev_err(&spi->dev,
			"can't write data with the following length: %d\n",
			len);
		ret = -EINVAL;
	}

	return ret;
}

static int wilc_spi_rx(struct wilc *wilc, u8 *rb, u32 rlen)
{
	struct spi_device *spi = to_spi_device(wilc->dev);
	int ret;

	if (rlen > 0) {
		struct spi_message msg;
		struct spi_transfer tr = {
			.rx_buf = rb,
			.len = rlen,
			.delay_usecs = 0,

		};
		char *t_buffer = kzalloc(rlen, GFP_KERNEL);

		if (!t_buffer)
			return -ENOMEM;

		tr.tx_buf = t_buffer;

		memset(&msg, 0, sizeof(msg));
		spi_message_init(&msg);
		msg.spi = spi;
		msg.is_dma_mapped = USE_SPI_DMA;
		spi_message_add_tail(&tr, &msg);

		ret = spi_sync(spi, &msg);
		if (ret < 0)
			dev_err(&spi->dev, "SPI transaction failed\n");
		kfree(t_buffer);
	} else {
		dev_err(&spi->dev,
			"can't read data with the following length: %u\n",
			rlen);
		ret = -EINVAL;
	}

	return ret;
}

static int wilc_spi_tx_rx(struct wilc *wilc, u8 *wb, u8 *rb, u32 rlen)
{
	struct spi_device *spi = to_spi_device(wilc->dev);
	int ret;

	if (rlen > 0) {
		struct spi_message msg;
		struct spi_transfer tr = {
			.rx_buf = rb,
			.tx_buf = wb,
			.len = rlen,
			.bits_per_word = 8,
			.delay_usecs = 0,

		};

		memset(&msg, 0, sizeof(msg));
		spi_message_init(&msg);
		msg.spi = spi;
		msg.is_dma_mapped = USE_SPI_DMA;

		spi_message_add_tail(&tr, &msg);
		ret = spi_sync(spi, &msg);
		if (ret < 0)
			dev_err(&spi->dev, "SPI transaction failed\n");
	} else {
		dev_err(&spi->dev,
			"can't read data with the following length: %u\n",
			rlen);
		ret = -EINVAL;
	}

	return ret;
}

static int spi_data_write(struct wilc *wilc, u8 *b, u32 sz)
{
	struct spi_device *spi = to_spi_device(wilc->dev);
	struct wilc_spi *spi_priv = wilc->bus_data;
	int ix, nbytes;
	int result = 0;
	u8 cmd, order, crc[2] = {0};

	/*
	 * Data
	 */
	ix = 0;
	do {
		if (sz <= DATA_PKT_SZ) {
			nbytes = sz;
			order = 0x3;
		} else {
			nbytes = DATA_PKT_SZ;
			if (ix == 0)
				order = 0x1;
			else
				order = 0x02;
		}

		/*
		 * Write command
		 */
		cmd = 0xf0;
		cmd |= order;

		if (wilc_spi_tx(wilc, &cmd, 1)) {
			dev_err(&spi->dev,
				"Failed data block cmd write, bus error...\n");
			result = -EINVAL;
			break;
		}

		/*
		 * Write data
		 */
		if (wilc_spi_tx(wilc, &b[ix], nbytes)) {
			dev_err(&spi->dev,
				"Failed data block write, bus error...\n");
			result = -EINVAL;
			break;
		}

		/*
		 * Write Crc
		 */
		if (!spi_priv->crc_off) {
			if (wilc_spi_tx(wilc, crc, 2)) {
				dev_err(&spi->dev, "Failed data block crc write, bus error...\n");
				result = -EINVAL;
				break;
			}
		}

		/*
		 * No need to wait for response
		 */
		ix += nbytes;
		sz -= nbytes;
	} while (sz);

	return result;
}

/********************************************
 *
 *      Spi Internal Read/Write Function
 *
 ********************************************/
static int wilc_spi_single_read(struct wilc *wilc, u8 cmd, u32 adr, void *b,
				u8 clockless)
{
	struct spi_device *spi = to_spi_device(wilc->dev);
	struct wilc_spi *spi_priv = wilc->bus_data;
	u8 wb[32], rb[32];
	int cmd_len, resp_len;
	u8 crc[2];
	struct wilc_spi_cmd *c;
	struct wilc_spi_read_rsp_data *r;

	memset(wb, 0x0, sizeof(wb));
	memset(rb, 0x0, sizeof(rb));
	c = (struct wilc_spi_cmd *)wb;
	c->cmd_type = cmd;
	if (cmd == CMD_SINGLE_READ) {
		c->u.simple_cmd.addr[0] = adr >> 16;
		c->u.simple_cmd.addr[1] = adr >> 8;
		c->u.simple_cmd.addr[2] = adr;
	} else if (cmd == CMD_INTERNAL_READ) {
		c->u.simple_cmd.addr[0] = adr >> 8;
		if (clockless == 1)
			c->u.simple_cmd.addr[0] |= BIT(7);
		c->u.simple_cmd.addr[1] = adr;
		c->u.simple_cmd.addr[2] = 0x0;
	} else {
		dev_err(&spi->dev, "cmd [%x] not supported\n", cmd);
		return -EINVAL;
	}

	cmd_len = offsetof(struct wilc_spi_cmd, u.simple_cmd.crc);
	resp_len = sizeof(*r);
	if (!spi_priv->crc_off) {
		c->u.simple_cmd.crc[0] = wilc_get_crc7(wb, cmd_len);
		cmd_len += 1;
		resp_len += 2;
	}

	if (cmd_len + resp_len > ARRAY_SIZE(wb)) {
		dev_err(&spi->dev,
			"spi buffer size too small (%d) (%d) (%zu)\n",
			cmd_len, resp_len, ARRAY_SIZE(wb));
		return -EINVAL;
	}

	if (wilc_spi_tx_rx(wilc, wb, rb, cmd_len + resp_len)) {
		dev_err(&spi->dev, "Failed cmd write, bus error...\n");
		return -EINVAL;
	}

	r = (struct wilc_spi_read_rsp_data *)&rb[cmd_len];
	if (r->rsp_cmd_type != cmd) {
		dev_err(&spi->dev,
			"Failed cmd response, cmd (%02x), resp (%02x)\n",
			cmd, r->rsp_cmd_type);
		return -EINVAL;
	}

	if (r->status != WILC_SPI_COMMAND_STAT_SUCCESS) {
		dev_err(&spi->dev, "Failed cmd state response state (%02x)\n",
			r->status);
		return -EINVAL;
	}

	if (WILC_GET_RESP_HDR_START(r->resp_header) != 0xf) {
		dev_err(&spi->dev, "Error, data read response (%02x)\n",
			r->resp_header);
		return -EINVAL;
	}

	if (b)
		memcpy(b, r->resp_data, 4);

	if (!spi_priv->crc_off)
		memcpy(crc, r->crc, 2);

	return 0;
}

static int wilc_spi_write_cmd(struct wilc *wilc, u8 cmd, u32 adr, u32 data,
			      u8 clockless)
{
	struct spi_device *spi = to_spi_device(wilc->dev);
	struct wilc_spi *spi_priv = wilc->bus_data;
	u8 wb[32], rb[32];
	int cmd_len, resp_len;
	struct wilc_spi_cmd *c;
	struct wilc_spi_rsp_data *r;

	memset(wb, 0x0, sizeof(wb));
	memset(rb, 0x0, sizeof(rb));
	c = (struct wilc_spi_cmd *)wb;
	c->cmd_type = cmd;
	if (cmd == CMD_INTERNAL_WRITE) {
		c->u.internal_w_cmd.addr[0] = adr >> 8;
		if (clockless == 1)
			c->u.internal_w_cmd.addr[0] |= BIT(7);

		c->u.internal_w_cmd.addr[1] = adr;
		c->u.internal_w_cmd.data = cpu_to_be32(data);
		cmd_len = offsetof(struct wilc_spi_cmd, u.internal_w_cmd.crc);
		if (!spi_priv->crc_off)
			c->u.internal_w_cmd.crc[0] = wilc_get_crc7(wb, cmd_len);
	} else if (cmd == CMD_SINGLE_WRITE) {
		c->u.w_cmd.addr[0] = adr >> 16;
		c->u.w_cmd.addr[1] = adr >> 8;
		c->u.w_cmd.addr[2] = adr;
		c->u.w_cmd.data = cpu_to_be32(data);
		cmd_len = offsetof(struct wilc_spi_cmd, u.w_cmd.crc);
		if (!spi_priv->crc_off)
			c->u.w_cmd.crc[0] = wilc_get_crc7(wb, cmd_len);
	} else {
		dev_err(&spi->dev, "write cmd [%x] not supported\n", cmd);
		return -EINVAL;
	}

	if (!spi_priv->crc_off)
		cmd_len += 1;

	resp_len = sizeof(*r);

	if (cmd_len + resp_len > ARRAY_SIZE(wb)) {
		dev_err(&spi->dev,
			"spi buffer size too small (%d) (%d) (%zu)\n",
			cmd_len, resp_len, ARRAY_SIZE(wb));
		return -EINVAL;
	}

	if (wilc_spi_tx_rx(wilc, wb, rb, cmd_len + resp_len)) {
		dev_err(&spi->dev, "Failed cmd write, bus error...\n");
		return -EINVAL;
	}

	r = (struct wilc_spi_rsp_data *)&rb[cmd_len];
	if (r->rsp_cmd_type != cmd) {
		dev_err(&spi->dev,
			"Failed cmd response, cmd (%02x), resp (%02x)\n",
			cmd, r->rsp_cmd_type);
		return -EINVAL;
	}

	if (r->status != WILC_SPI_COMMAND_STAT_SUCCESS) {
		dev_err(&spi->dev, "Failed cmd state response state (%02x)\n",
			r->status);
		return -EINVAL;
	}

	return 0;
}

static int wilc_spi_dma_rw(struct wilc *wilc, u8 cmd, u32 adr, u8 *b, u32 sz)
{
	struct spi_device *spi = to_spi_device(wilc->dev);
	struct wilc_spi *spi_priv = wilc->bus_data;
	u8 wb[32], rb[32];
	int cmd_len, resp_len;
	int retry, ix = 0;
	u8 crc[2];
	struct wilc_spi_cmd *c;
	struct wilc_spi_rsp_data *r;

	memset(wb, 0x0, sizeof(wb));
	memset(rb, 0x0, sizeof(rb));
	c = (struct wilc_spi_cmd *)wb;
	c->cmd_type = cmd;
	if (cmd == CMD_DMA_WRITE || cmd == CMD_DMA_READ) {
		c->u.dma_cmd.addr[0] = adr >> 16;
		c->u.dma_cmd.addr[1] = adr >> 8;
		c->u.dma_cmd.addr[2] = adr;
		c->u.dma_cmd.size[0] = sz >> 8;
		c->u.dma_cmd.size[1] = sz;
		cmd_len = offsetof(struct wilc_spi_cmd, u.dma_cmd.crc);
		if (!spi_priv->crc_off)
			c->u.dma_cmd.crc[0] = wilc_get_crc7(wb, cmd_len);
	} else if (cmd == CMD_DMA_EXT_WRITE || cmd == CMD_DMA_EXT_READ) {
		c->u.dma_cmd_ext.addr[0] = adr >> 16;
		c->u.dma_cmd_ext.addr[1] = adr >> 8;
		c->u.dma_cmd_ext.addr[2] = adr;
		c->u.dma_cmd_ext.size[0] = sz >> 16;
		c->u.dma_cmd_ext.size[1] = sz >> 8;
		c->u.dma_cmd_ext.size[2] = sz;
		cmd_len = offsetof(struct wilc_spi_cmd, u.dma_cmd_ext.crc);
		if (!spi_priv->crc_off)
			c->u.dma_cmd_ext.crc[0] = wilc_get_crc7(wb, cmd_len);
	} else {
		dev_err(&spi->dev, "dma read write cmd [%x] not supported\n",
			cmd);
		return -EINVAL;
	}
	if (!spi_priv->crc_off)
		cmd_len += 1;

	resp_len = sizeof(*r);

	if (cmd_len + resp_len > ARRAY_SIZE(wb)) {
		dev_err(&spi->dev, "spi buffer size too small (%d)(%d) (%zu)\n",
			cmd_len, resp_len, ARRAY_SIZE(wb));
		return -EINVAL;
	}

	if (wilc_spi_tx_rx(wilc, wb, rb, cmd_len + resp_len)) {
		dev_err(&spi->dev, "Failed cmd write, bus error...\n");
		return -EINVAL;
	}

	r = (struct wilc_spi_rsp_data *)&rb[cmd_len];
	if (r->rsp_cmd_type != cmd) {
		dev_err(&spi->dev,
			"Failed cmd response, cmd (%02x), resp (%02x)\n",
			cmd, r->rsp_cmd_type);
		return -EINVAL;
	}

	if (r->status != WILC_SPI_COMMAND_STAT_SUCCESS) {
		dev_err(&spi->dev, "Failed cmd state response state (%02x)\n",
			r->status);
		return -EINVAL;
	}

	if (cmd == CMD_DMA_WRITE || cmd == CMD_DMA_EXT_WRITE)
		return 0;

	while (sz > 0) {
		int nbytes;
		u8 rsp;

		if (sz <= DATA_PKT_SZ)
			nbytes = sz;
		else
			nbytes = DATA_PKT_SZ;

		/*
		 * Data Response header
		 */
		retry = 100;
		do {
			if (wilc_spi_rx(wilc, &rsp, 1)) {
				dev_err(&spi->dev,
					"Failed resp read, bus err\n");
				return -EINVAL;
			}
			if (WILC_GET_RESP_HDR_START(rsp) == 0xf)
				break;
		} while (retry--);

		/*
		 * Read bytes
		 */
		if (wilc_spi_rx(wilc, &b[ix], nbytes)) {
			dev_err(&spi->dev,
				"Failed block read, bus err\n");
			return -EINVAL;
		}

		/*
		 * Read Crc
		 */
		if (!spi_priv->crc_off && wilc_spi_rx(wilc, crc, 2)) {
			dev_err(&spi->dev,
				"Failed block crc read, bus err\n");
			return -EINVAL;
		}

		ix += nbytes;
		sz -= nbytes;
	}
	return 0;
}

static int wilc_spi_read_reg(struct wilc *wilc, u32 addr, u32 *data)
{
	struct spi_device *spi = to_spi_device(wilc->dev);
	int result;
	u8 cmd = CMD_SINGLE_READ;
	u8 clockless = 0;

	if (addr < WILC_SPI_CLOCKLESS_ADDR_LIMIT) {
		/* Clockless register */
		cmd = CMD_INTERNAL_READ;
		clockless = 1;
	}

	result = wilc_spi_single_read(wilc, cmd, addr, data, clockless);
	if (result) {
		dev_err(&spi->dev, "Failed cmd, read reg (%08x)...\n", addr);
		return result;
	}

	le32_to_cpus(data);

	return 0;
}

static int wilc_spi_read(struct wilc *wilc, u32 addr, u8 *buf, u32 size)
{
	struct spi_device *spi = to_spi_device(wilc->dev);
	int result;

	if (size <= 4)
		return -EINVAL;

	result = wilc_spi_dma_rw(wilc, CMD_DMA_EXT_READ, addr, buf, size);
	if (result) {
		dev_err(&spi->dev, "Failed cmd, read block (%08x)...\n", addr);
		return result;
	}

	return 0;
}

static int spi_internal_write(struct wilc *wilc, u32 adr, u32 dat)
{
	struct spi_device *spi = to_spi_device(wilc->dev);
	int result;

	result = wilc_spi_write_cmd(wilc, CMD_INTERNAL_WRITE, adr, dat, 0);
	if (result) {
		dev_err(&spi->dev, "Failed internal write cmd...\n");
		return result;
	}

	return 0;
}

static int spi_internal_read(struct wilc *wilc, u32 adr, u32 *data)
{
	struct spi_device *spi = to_spi_device(wilc->dev);
	int result;

	result = wilc_spi_single_read(wilc, CMD_INTERNAL_READ, adr, data, 0);
	if (result) {
		dev_err(&spi->dev, "Failed internal read cmd...\n");
		return result;
	}

	le32_to_cpus(data);

	return 0;
}

/********************************************
 *
 *      Spi interfaces
 *
 ********************************************/

static int wilc_spi_write_reg(struct wilc *wilc, u32 addr, u32 data)
{
	struct spi_device *spi = to_spi_device(wilc->dev);
	int result;
	u8 cmd = CMD_SINGLE_WRITE;
	u8 clockless = 0;

	if (addr < WILC_SPI_CLOCKLESS_ADDR_LIMIT) {
		/* Clockless register */
		cmd = CMD_INTERNAL_WRITE;
		clockless = 1;
	}

	result = wilc_spi_write_cmd(wilc, cmd, addr, data, clockless);
	if (result) {
		dev_err(&spi->dev, "Failed cmd, write reg (%08x)...\n", addr);
		return result;
	}

	return 0;
}

static int wilc_spi_write(struct wilc *wilc, u32 addr, u8 *buf, u32 size)
{
	struct spi_device *spi = to_spi_device(wilc->dev);
	int result;

	/*
	 * has to be greated than 4
	 */
	if (size <= 4)
		return -EINVAL;

	result = wilc_spi_dma_rw(wilc, CMD_DMA_EXT_WRITE, addr, NULL, size);
	if (result) {
		dev_err(&spi->dev,
			"Failed cmd, write block (%08x)...\n", addr);
		return result;
	}

	/*
	 * Data
	 */
	result = spi_data_write(wilc, buf, size);
	if (result) {
		dev_err(&spi->dev, "Failed block data write...\n");
		return result;
	}

	return 0;
}

/********************************************
 *
 *      Bus interfaces
 *
 ********************************************/

static int wilc_spi_deinit(struct wilc *wilc)
{
	/*
	 * TODO:
	 */
	return 0;
}

static int wilc_spi_init(struct wilc *wilc, bool resume)
{
	struct spi_device *spi = to_spi_device(wilc->dev);
	struct wilc_spi *spi_priv = wilc->bus_data;
	u32 reg;
	u32 chipid;
	static int isinit;
	int ret;

	if (isinit) {
		ret = wilc_spi_read_reg(wilc, WILC_CHIPID, &chipid);
		if (ret)
			dev_err(&spi->dev, "Fail cmd read chip id...\n");

		return ret;
	}

	/*
	 * configure protocol
	 */

	/*
	 * TODO: We can remove the CRC trials if there is a definite
	 * way to reset
	 */
	/* the SPI to it's initial value. */
	ret = spi_internal_read(wilc, WILC_SPI_PROTOCOL_OFFSET, &reg);
	if (ret) {
		/*
		 * Read failed. Try with CRC off. This might happen when module
		 * is removed but chip isn't reset
		 */
		spi_priv->crc_off = 1;
		dev_err(&spi->dev,
			"Failed read with CRC on, retrying with CRC off\n");
		ret = spi_internal_read(wilc, WILC_SPI_PROTOCOL_OFFSET, &reg);
		if (ret) {
			/*
			 * Read failed with both CRC on and off,
			 * something went bad
			 */
			dev_err(&spi->dev, "Failed internal read protocol\n");
			return ret;
		}
	}
	if (spi_priv->crc_off == 0) {
		reg &= ~0xc; /* disable crc checking */
		reg &= ~0x70;
		reg |= (0x5 << 4);
		ret = spi_internal_write(wilc, WILC_SPI_PROTOCOL_OFFSET, reg);
		if (ret) {
			dev_err(&spi->dev,
				"[wilc spi %d]: Failed internal write reg\n",
				__LINE__);
			return ret;
		}
		spi_priv->crc_off = 1;
	}

	/*
	 * make sure can read back chip id correctly
	 */
	ret = wilc_spi_read_reg(wilc, WILC_CHIPID, &chipid);
	if (ret) {
		dev_err(&spi->dev, "Fail cmd read chip id...\n");
		return ret;
	}

	isinit = 1;

	return 0;
}

static int wilc_spi_read_size(struct wilc *wilc, u32 *size)
{
	int ret;

	ret = spi_internal_read(wilc,
				WILC_SPI_INT_STATUS - WILC_SPI_REG_BASE, size);
	*size = FIELD_GET(IRQ_DMA_WD_CNT_MASK, *size);

	return ret;
}

static int wilc_spi_read_int(struct wilc *wilc, u32 *int_status)
{
	return spi_internal_read(wilc, WILC_SPI_INT_STATUS - WILC_SPI_REG_BASE,
				 int_status);
}

static int wilc_spi_clear_int_ext(struct wilc *wilc, u32 val)
{
	return spi_internal_write(wilc, WILC_SPI_INT_CLEAR - WILC_SPI_REG_BASE,
				  val);
}

static int wilc_spi_sync_ext(struct wilc *wilc, int nint)
{
	struct spi_device *spi = to_spi_device(wilc->dev);
	u32 reg;
	int ret, i;

	if (nint > MAX_NUM_INT) {
		dev_err(&spi->dev, "Too many interrupts (%d)...\n", nint);
		return -EINVAL;
	}

	/*
	 * interrupt pin mux select
	 */
	ret = wilc_spi_read_reg(wilc, WILC_PIN_MUX_0, &reg);
	if (ret) {
		dev_err(&spi->dev, "Failed read reg (%08x)...\n",
			WILC_PIN_MUX_0);
		return ret;
	}
	reg |= BIT(8);
	ret = wilc_spi_write_reg(wilc, WILC_PIN_MUX_0, reg);
	if (ret) {
		dev_err(&spi->dev, "Failed write reg (%08x)...\n",
			WILC_PIN_MUX_0);
		return ret;
	}

	/*
	 * interrupt enable
	 */
	ret = wilc_spi_read_reg(wilc, WILC_INTR_ENABLE, &reg);
	if (ret) {
		dev_err(&spi->dev, "Failed read reg (%08x)...\n",
			WILC_INTR_ENABLE);
		return ret;
	}

	for (i = 0; (i < 5) && (nint > 0); i++, nint--)
		reg |= (BIT((27 + i)));

	ret = wilc_spi_write_reg(wilc, WILC_INTR_ENABLE, reg);
	if (ret) {
		dev_err(&spi->dev, "Failed write reg (%08x)...\n",
			WILC_INTR_ENABLE);
		return ret;
	}
	if (nint) {
		ret = wilc_spi_read_reg(wilc, WILC_INTR2_ENABLE, &reg);
		if (ret) {
			dev_err(&spi->dev, "Failed read reg (%08x)...\n",
				WILC_INTR2_ENABLE);
			return ret;
		}

		for (i = 0; (i < 3) && (nint > 0); i++, nint--)
			reg |= BIT(i);

		ret = wilc_spi_read_reg(wilc, WILC_INTR2_ENABLE, &reg);
		if (ret) {
			dev_err(&spi->dev, "Failed write reg (%08x)...\n",
				WILC_INTR2_ENABLE);
			return ret;
		}
	}

	return 0;
}

/* Global spi HIF function table */
static const struct wilc_hif_func wilc_hif_spi = {
	.hif_init = wilc_spi_init,
	.hif_deinit = wilc_spi_deinit,
	.hif_read_reg = wilc_spi_read_reg,
	.hif_write_reg = wilc_spi_write_reg,
	.hif_block_rx = wilc_spi_read,
	.hif_block_tx = wilc_spi_write,
	.hif_read_int = wilc_spi_read_int,
	.hif_clear_int_ext = wilc_spi_clear_int_ext,
	.hif_read_size = wilc_spi_read_size,
	.hif_block_tx_ext = wilc_spi_write,
	.hif_block_rx_ext = wilc_spi_read,
	.hif_sync_ext = wilc_spi_sync_ext,
};
