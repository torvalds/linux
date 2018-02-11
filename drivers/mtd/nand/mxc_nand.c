/*
 * Copyright 2004-2007 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright 2008 Sascha Hauer, kernel@pengutronix.de
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/rawnand.h>
#include <linux/mtd/partitions.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/completion.h>
#include <linux/of.h>
#include <linux/of_device.h>

#include <asm/mach/flash.h>
#include <linux/platform_data/mtd-mxc_nand.h>

#define DRIVER_NAME "mxc_nand"

/* Addresses for NFC registers */
#define NFC_V1_V2_BUF_SIZE		(host->regs + 0x00)
#define NFC_V1_V2_BUF_ADDR		(host->regs + 0x04)
#define NFC_V1_V2_FLASH_ADDR		(host->regs + 0x06)
#define NFC_V1_V2_FLASH_CMD		(host->regs + 0x08)
#define NFC_V1_V2_CONFIG		(host->regs + 0x0a)
#define NFC_V1_V2_ECC_STATUS_RESULT	(host->regs + 0x0c)
#define NFC_V1_V2_RSLTMAIN_AREA		(host->regs + 0x0e)
#define NFC_V1_V2_RSLTSPARE_AREA	(host->regs + 0x10)
#define NFC_V1_V2_WRPROT		(host->regs + 0x12)
#define NFC_V1_UNLOCKSTART_BLKADDR	(host->regs + 0x14)
#define NFC_V1_UNLOCKEND_BLKADDR	(host->regs + 0x16)
#define NFC_V21_UNLOCKSTART_BLKADDR0	(host->regs + 0x20)
#define NFC_V21_UNLOCKSTART_BLKADDR1	(host->regs + 0x24)
#define NFC_V21_UNLOCKSTART_BLKADDR2	(host->regs + 0x28)
#define NFC_V21_UNLOCKSTART_BLKADDR3	(host->regs + 0x2c)
#define NFC_V21_UNLOCKEND_BLKADDR0	(host->regs + 0x22)
#define NFC_V21_UNLOCKEND_BLKADDR1	(host->regs + 0x26)
#define NFC_V21_UNLOCKEND_BLKADDR2	(host->regs + 0x2a)
#define NFC_V21_UNLOCKEND_BLKADDR3	(host->regs + 0x2e)
#define NFC_V1_V2_NF_WRPRST		(host->regs + 0x18)
#define NFC_V1_V2_CONFIG1		(host->regs + 0x1a)
#define NFC_V1_V2_CONFIG2		(host->regs + 0x1c)

#define NFC_V2_CONFIG1_ECC_MODE_4	(1 << 0)
#define NFC_V1_V2_CONFIG1_SP_EN		(1 << 2)
#define NFC_V1_V2_CONFIG1_ECC_EN	(1 << 3)
#define NFC_V1_V2_CONFIG1_INT_MSK	(1 << 4)
#define NFC_V1_V2_CONFIG1_BIG		(1 << 5)
#define NFC_V1_V2_CONFIG1_RST		(1 << 6)
#define NFC_V1_V2_CONFIG1_CE		(1 << 7)
#define NFC_V2_CONFIG1_ONE_CYCLE	(1 << 8)
#define NFC_V2_CONFIG1_PPB(x)		(((x) & 0x3) << 9)
#define NFC_V2_CONFIG1_FP_INT		(1 << 11)

#define NFC_V1_V2_CONFIG2_INT		(1 << 15)

/*
 * Operation modes for the NFC. Valid for v1, v2 and v3
 * type controllers.
 */
#define NFC_CMD				(1 << 0)
#define NFC_ADDR			(1 << 1)
#define NFC_INPUT			(1 << 2)
#define NFC_OUTPUT			(1 << 3)
#define NFC_ID				(1 << 4)
#define NFC_STATUS			(1 << 5)

#define NFC_V3_FLASH_CMD		(host->regs_axi + 0x00)
#define NFC_V3_FLASH_ADDR0		(host->regs_axi + 0x04)

#define NFC_V3_CONFIG1			(host->regs_axi + 0x34)
#define NFC_V3_CONFIG1_SP_EN		(1 << 0)
#define NFC_V3_CONFIG1_RBA(x)		(((x) & 0x7 ) << 4)

#define NFC_V3_ECC_STATUS_RESULT	(host->regs_axi + 0x38)

#define NFC_V3_LAUNCH			(host->regs_axi + 0x40)

#define NFC_V3_WRPROT			(host->regs_ip + 0x0)
#define NFC_V3_WRPROT_LOCK_TIGHT	(1 << 0)
#define NFC_V3_WRPROT_LOCK		(1 << 1)
#define NFC_V3_WRPROT_UNLOCK		(1 << 2)
#define NFC_V3_WRPROT_BLS_UNLOCK	(2 << 6)

#define NFC_V3_WRPROT_UNLOCK_BLK_ADD0   (host->regs_ip + 0x04)

#define NFC_V3_CONFIG2			(host->regs_ip + 0x24)
#define NFC_V3_CONFIG2_PS_512			(0 << 0)
#define NFC_V3_CONFIG2_PS_2048			(1 << 0)
#define NFC_V3_CONFIG2_PS_4096			(2 << 0)
#define NFC_V3_CONFIG2_ONE_CYCLE		(1 << 2)
#define NFC_V3_CONFIG2_ECC_EN			(1 << 3)
#define NFC_V3_CONFIG2_2CMD_PHASES		(1 << 4)
#define NFC_V3_CONFIG2_NUM_ADDR_PHASE0		(1 << 5)
#define NFC_V3_CONFIG2_ECC_MODE_8		(1 << 6)
#define NFC_V3_CONFIG2_PPB(x, shift)		(((x) & 0x3) << shift)
#define NFC_V3_CONFIG2_NUM_ADDR_PHASE1(x)	(((x) & 0x3) << 12)
#define NFC_V3_CONFIG2_INT_MSK			(1 << 15)
#define NFC_V3_CONFIG2_ST_CMD(x)		(((x) & 0xff) << 24)
#define NFC_V3_CONFIG2_SPAS(x)			(((x) & 0xff) << 16)

#define NFC_V3_CONFIG3				(host->regs_ip + 0x28)
#define NFC_V3_CONFIG3_ADD_OP(x)		(((x) & 0x3) << 0)
#define NFC_V3_CONFIG3_FW8			(1 << 3)
#define NFC_V3_CONFIG3_SBB(x)			(((x) & 0x7) << 8)
#define NFC_V3_CONFIG3_NUM_OF_DEVICES(x)	(((x) & 0x7) << 12)
#define NFC_V3_CONFIG3_RBB_MODE			(1 << 15)
#define NFC_V3_CONFIG3_NO_SDMA			(1 << 20)

#define NFC_V3_IPC			(host->regs_ip + 0x2C)
#define NFC_V3_IPC_CREQ			(1 << 0)
#define NFC_V3_IPC_INT			(1 << 31)

#define NFC_V3_DELAY_LINE		(host->regs_ip + 0x34)

struct mxc_nand_host;

struct mxc_nand_devtype_data {
	void (*preset)(struct mtd_info *);
	void (*send_cmd)(struct mxc_nand_host *, uint16_t, int);
	void (*send_addr)(struct mxc_nand_host *, uint16_t, int);
	void (*send_page)(struct mtd_info *, unsigned int);
	void (*send_read_id)(struct mxc_nand_host *);
	uint16_t (*get_dev_status)(struct mxc_nand_host *);
	int (*check_int)(struct mxc_nand_host *);
	void (*irq_control)(struct mxc_nand_host *, int);
	u32 (*get_ecc_status)(struct mxc_nand_host *);
	const struct mtd_ooblayout_ops *ooblayout;
	void (*select_chip)(struct mtd_info *mtd, int chip);
	int (*correct_data)(struct mtd_info *mtd, u_char *dat,
			u_char *read_ecc, u_char *calc_ecc);
	int (*setup_data_interface)(struct mtd_info *mtd, int csline,
				    const struct nand_data_interface *conf);

	/*
	 * On i.MX21 the CONFIG2:INT bit cannot be read if interrupts are masked
	 * (CONFIG1:INT_MSK is set). To handle this the driver uses
	 * enable_irq/disable_irq_nosync instead of CONFIG1:INT_MSK
	 */
	int irqpending_quirk;
	int needs_ip;

	size_t regs_offset;
	size_t spare0_offset;
	size_t axi_offset;

	int spare_len;
	int eccbytes;
	int eccsize;
	int ppb_shift;
};

struct mxc_nand_host {
	struct nand_chip	nand;
	struct device		*dev;

	void __iomem		*spare0;
	void __iomem		*main_area0;

	void __iomem		*base;
	void __iomem		*regs;
	void __iomem		*regs_axi;
	void __iomem		*regs_ip;
	int			status_request;
	struct clk		*clk;
	int			clk_act;
	int			irq;
	int			eccsize;
	int			used_oobsize;
	int			active_cs;

	struct completion	op_completion;

	uint8_t			*data_buf;
	unsigned int		buf_start;

	const struct mxc_nand_devtype_data *devtype_data;
	struct mxc_nand_platform_data pdata;
};

static const char * const part_probes[] = {
	"cmdlinepart", "RedBoot", "ofpart", NULL };

static void memcpy32_fromio(void *trg, const void __iomem  *src, size_t size)
{
	int i;
	u32 *t = trg;
	const __iomem u32 *s = src;

	for (i = 0; i < (size >> 2); i++)
		*t++ = __raw_readl(s++);
}

static void memcpy16_fromio(void *trg, const void __iomem  *src, size_t size)
{
	int i;
	u16 *t = trg;
	const __iomem u16 *s = src;

	/* We assume that src (IO) is always 32bit aligned */
	if (PTR_ALIGN(trg, 4) == trg && IS_ALIGNED(size, 4)) {
		memcpy32_fromio(trg, src, size);
		return;
	}

	for (i = 0; i < (size >> 1); i++)
		*t++ = __raw_readw(s++);
}

static inline void memcpy32_toio(void __iomem *trg, const void *src, int size)
{
	/* __iowrite32_copy use 32bit size values so divide by 4 */
	__iowrite32_copy(trg, src, size / 4);
}

static void memcpy16_toio(void __iomem *trg, const void *src, int size)
{
	int i;
	__iomem u16 *t = trg;
	const u16 *s = src;

	/* We assume that trg (IO) is always 32bit aligned */
	if (PTR_ALIGN(src, 4) == src && IS_ALIGNED(size, 4)) {
		memcpy32_toio(trg, src, size);
		return;
	}

	for (i = 0; i < (size >> 1); i++)
		__raw_writew(*s++, t++);
}

static int check_int_v3(struct mxc_nand_host *host)
{
	uint32_t tmp;

	tmp = readl(NFC_V3_IPC);
	if (!(tmp & NFC_V3_IPC_INT))
		return 0;

	tmp &= ~NFC_V3_IPC_INT;
	writel(tmp, NFC_V3_IPC);

	return 1;
}

static int check_int_v1_v2(struct mxc_nand_host *host)
{
	uint32_t tmp;

	tmp = readw(NFC_V1_V2_CONFIG2);
	if (!(tmp & NFC_V1_V2_CONFIG2_INT))
		return 0;

	if (!host->devtype_data->irqpending_quirk)
		writew(tmp & ~NFC_V1_V2_CONFIG2_INT, NFC_V1_V2_CONFIG2);

	return 1;
}

static void irq_control_v1_v2(struct mxc_nand_host *host, int activate)
{
	uint16_t tmp;

	tmp = readw(NFC_V1_V2_CONFIG1);

	if (activate)
		tmp &= ~NFC_V1_V2_CONFIG1_INT_MSK;
	else
		tmp |= NFC_V1_V2_CONFIG1_INT_MSK;

	writew(tmp, NFC_V1_V2_CONFIG1);
}

static void irq_control_v3(struct mxc_nand_host *host, int activate)
{
	uint32_t tmp;

	tmp = readl(NFC_V3_CONFIG2);

	if (activate)
		tmp &= ~NFC_V3_CONFIG2_INT_MSK;
	else
		tmp |= NFC_V3_CONFIG2_INT_MSK;

	writel(tmp, NFC_V3_CONFIG2);
}

static void irq_control(struct mxc_nand_host *host, int activate)
{
	if (host->devtype_data->irqpending_quirk) {
		if (activate)
			enable_irq(host->irq);
		else
			disable_irq_nosync(host->irq);
	} else {
		host->devtype_data->irq_control(host, activate);
	}
}

static u32 get_ecc_status_v1(struct mxc_nand_host *host)
{
	return readw(NFC_V1_V2_ECC_STATUS_RESULT);
}

static u32 get_ecc_status_v2(struct mxc_nand_host *host)
{
	return readl(NFC_V1_V2_ECC_STATUS_RESULT);
}

static u32 get_ecc_status_v3(struct mxc_nand_host *host)
{
	return readl(NFC_V3_ECC_STATUS_RESULT);
}

static irqreturn_t mxc_nfc_irq(int irq, void *dev_id)
{
	struct mxc_nand_host *host = dev_id;

	if (!host->devtype_data->check_int(host))
		return IRQ_NONE;

	irq_control(host, 0);

	complete(&host->op_completion);

	return IRQ_HANDLED;
}

/* This function polls the NANDFC to wait for the basic operation to
 * complete by checking the INT bit of config2 register.
 */
static int wait_op_done(struct mxc_nand_host *host, int useirq)
{
	int ret = 0;

	/*
	 * If operation is already complete, don't bother to setup an irq or a
	 * loop.
	 */
	if (host->devtype_data->check_int(host))
		return 0;

	if (useirq) {
		unsigned long timeout;

		reinit_completion(&host->op_completion);

		irq_control(host, 1);

		timeout = wait_for_completion_timeout(&host->op_completion, HZ);
		if (!timeout && !host->devtype_data->check_int(host)) {
			dev_dbg(host->dev, "timeout waiting for irq\n");
			ret = -ETIMEDOUT;
		}
	} else {
		int max_retries = 8000;
		int done;

		do {
			udelay(1);

			done = host->devtype_data->check_int(host);
			if (done)
				break;

		} while (--max_retries);

		if (!done) {
			dev_dbg(host->dev, "timeout polling for completion\n");
			ret = -ETIMEDOUT;
		}
	}

	WARN_ONCE(ret < 0, "timeout! useirq=%d\n", useirq);

	return ret;
}

static void send_cmd_v3(struct mxc_nand_host *host, uint16_t cmd, int useirq)
{
	/* fill command */
	writel(cmd, NFC_V3_FLASH_CMD);

	/* send out command */
	writel(NFC_CMD, NFC_V3_LAUNCH);

	/* Wait for operation to complete */
	wait_op_done(host, useirq);
}

/* This function issues the specified command to the NAND device and
 * waits for completion. */
static void send_cmd_v1_v2(struct mxc_nand_host *host, uint16_t cmd, int useirq)
{
	dev_dbg(host->dev, "send_cmd(host, 0x%x, %d)\n", cmd, useirq);

	writew(cmd, NFC_V1_V2_FLASH_CMD);
	writew(NFC_CMD, NFC_V1_V2_CONFIG2);

	if (host->devtype_data->irqpending_quirk && (cmd == NAND_CMD_RESET)) {
		int max_retries = 100;
		/* Reset completion is indicated by NFC_CONFIG2 */
		/* being set to 0 */
		while (max_retries-- > 0) {
			if (readw(NFC_V1_V2_CONFIG2) == 0) {
				break;
			}
			udelay(1);
		}
		if (max_retries < 0)
			dev_dbg(host->dev, "%s: RESET failed\n", __func__);
	} else {
		/* Wait for operation to complete */
		wait_op_done(host, useirq);
	}
}

static void send_addr_v3(struct mxc_nand_host *host, uint16_t addr, int islast)
{
	/* fill address */
	writel(addr, NFC_V3_FLASH_ADDR0);

	/* send out address */
	writel(NFC_ADDR, NFC_V3_LAUNCH);

	wait_op_done(host, 0);
}

/* This function sends an address (or partial address) to the
 * NAND device. The address is used to select the source/destination for
 * a NAND command. */
static void send_addr_v1_v2(struct mxc_nand_host *host, uint16_t addr, int islast)
{
	dev_dbg(host->dev, "send_addr(host, 0x%x %d)\n", addr, islast);

	writew(addr, NFC_V1_V2_FLASH_ADDR);
	writew(NFC_ADDR, NFC_V1_V2_CONFIG2);

	/* Wait for operation to complete */
	wait_op_done(host, islast);
}

static void send_page_v3(struct mtd_info *mtd, unsigned int ops)
{
	struct nand_chip *nand_chip = mtd_to_nand(mtd);
	struct mxc_nand_host *host = nand_get_controller_data(nand_chip);
	uint32_t tmp;

	tmp = readl(NFC_V3_CONFIG1);
	tmp &= ~(7 << 4);
	writel(tmp, NFC_V3_CONFIG1);

	/* transfer data from NFC ram to nand */
	writel(ops, NFC_V3_LAUNCH);

	wait_op_done(host, false);
}

static void send_page_v2(struct mtd_info *mtd, unsigned int ops)
{
	struct nand_chip *nand_chip = mtd_to_nand(mtd);
	struct mxc_nand_host *host = nand_get_controller_data(nand_chip);

	/* NANDFC buffer 0 is used for page read/write */
	writew(host->active_cs << 4, NFC_V1_V2_BUF_ADDR);

	writew(ops, NFC_V1_V2_CONFIG2);

	/* Wait for operation to complete */
	wait_op_done(host, true);
}

static void send_page_v1(struct mtd_info *mtd, unsigned int ops)
{
	struct nand_chip *nand_chip = mtd_to_nand(mtd);
	struct mxc_nand_host *host = nand_get_controller_data(nand_chip);
	int bufs, i;

	if (mtd->writesize > 512)
		bufs = 4;
	else
		bufs = 1;

	for (i = 0; i < bufs; i++) {

		/* NANDFC buffer 0 is used for page read/write */
		writew((host->active_cs << 4) | i, NFC_V1_V2_BUF_ADDR);

		writew(ops, NFC_V1_V2_CONFIG2);

		/* Wait for operation to complete */
		wait_op_done(host, true);
	}
}

static void send_read_id_v3(struct mxc_nand_host *host)
{
	/* Read ID into main buffer */
	writel(NFC_ID, NFC_V3_LAUNCH);

	wait_op_done(host, true);

	memcpy32_fromio(host->data_buf, host->main_area0, 16);
}

/* Request the NANDFC to perform a read of the NAND device ID. */
static void send_read_id_v1_v2(struct mxc_nand_host *host)
{
	/* NANDFC buffer 0 is used for device ID output */
	writew(host->active_cs << 4, NFC_V1_V2_BUF_ADDR);

	writew(NFC_ID, NFC_V1_V2_CONFIG2);

	/* Wait for operation to complete */
	wait_op_done(host, true);

	memcpy32_fromio(host->data_buf, host->main_area0, 16);
}

static uint16_t get_dev_status_v3(struct mxc_nand_host *host)
{
	writew(NFC_STATUS, NFC_V3_LAUNCH);
	wait_op_done(host, true);

	return readl(NFC_V3_CONFIG1) >> 16;
}

/* This function requests the NANDFC to perform a read of the
 * NAND device status and returns the current status. */
static uint16_t get_dev_status_v1_v2(struct mxc_nand_host *host)
{
	void __iomem *main_buf = host->main_area0;
	uint32_t store;
	uint16_t ret;

	writew(host->active_cs << 4, NFC_V1_V2_BUF_ADDR);

	/*
	 * The device status is stored in main_area0. To
	 * prevent corruption of the buffer save the value
	 * and restore it afterwards.
	 */
	store = readl(main_buf);

	writew(NFC_STATUS, NFC_V1_V2_CONFIG2);
	wait_op_done(host, true);

	ret = readw(main_buf);

	writel(store, main_buf);

	return ret;
}

/* This functions is used by upper layer to checks if device is ready */
static int mxc_nand_dev_ready(struct mtd_info *mtd)
{
	/*
	 * NFC handles R/B internally. Therefore, this function
	 * always returns status as ready.
	 */
	return 1;
}

static void mxc_nand_enable_hwecc(struct mtd_info *mtd, int mode)
{
	/*
	 * If HW ECC is enabled, we turn it on during init. There is
	 * no need to enable again here.
	 */
}

static int mxc_nand_correct_data_v1(struct mtd_info *mtd, u_char *dat,
				 u_char *read_ecc, u_char *calc_ecc)
{
	struct nand_chip *nand_chip = mtd_to_nand(mtd);
	struct mxc_nand_host *host = nand_get_controller_data(nand_chip);

	/*
	 * 1-Bit errors are automatically corrected in HW.  No need for
	 * additional correction.  2-Bit errors cannot be corrected by
	 * HW ECC, so we need to return failure
	 */
	uint16_t ecc_status = get_ecc_status_v1(host);

	if (((ecc_status & 0x3) == 2) || ((ecc_status >> 2) == 2)) {
		dev_dbg(host->dev, "HWECC uncorrectable 2-bit ECC error\n");
		return -EBADMSG;
	}

	return 0;
}

static int mxc_nand_correct_data_v2_v3(struct mtd_info *mtd, u_char *dat,
				 u_char *read_ecc, u_char *calc_ecc)
{
	struct nand_chip *nand_chip = mtd_to_nand(mtd);
	struct mxc_nand_host *host = nand_get_controller_data(nand_chip);
	u32 ecc_stat, err;
	int no_subpages = 1;
	int ret = 0;
	u8 ecc_bit_mask, err_limit;

	ecc_bit_mask = (host->eccsize == 4) ? 0x7 : 0xf;
	err_limit = (host->eccsize == 4) ? 0x4 : 0x8;

	no_subpages = mtd->writesize >> 9;

	ecc_stat = host->devtype_data->get_ecc_status(host);

	do {
		err = ecc_stat & ecc_bit_mask;
		if (err > err_limit) {
			dev_dbg(host->dev, "UnCorrectable RS-ECC Error\n");
			return -EBADMSG;
		} else {
			ret += err;
		}
		ecc_stat >>= 4;
	} while (--no_subpages);

	dev_dbg(host->dev, "%d Symbol Correctable RS-ECC Error\n", ret);

	return ret;
}

static int mxc_nand_calculate_ecc(struct mtd_info *mtd, const u_char *dat,
				  u_char *ecc_code)
{
	return 0;
}

static u_char mxc_nand_read_byte(struct mtd_info *mtd)
{
	struct nand_chip *nand_chip = mtd_to_nand(mtd);
	struct mxc_nand_host *host = nand_get_controller_data(nand_chip);
	uint8_t ret;

	/* Check for status request */
	if (host->status_request)
		return host->devtype_data->get_dev_status(host) & 0xFF;

	if (nand_chip->options & NAND_BUSWIDTH_16) {
		/* only take the lower byte of each word */
		ret = *(uint16_t *)(host->data_buf + host->buf_start);

		host->buf_start += 2;
	} else {
		ret = *(uint8_t *)(host->data_buf + host->buf_start);
		host->buf_start++;
	}

	dev_dbg(host->dev, "%s: ret=0x%hhx (start=%u)\n", __func__, ret, host->buf_start);
	return ret;
}

static uint16_t mxc_nand_read_word(struct mtd_info *mtd)
{
	struct nand_chip *nand_chip = mtd_to_nand(mtd);
	struct mxc_nand_host *host = nand_get_controller_data(nand_chip);
	uint16_t ret;

	ret = *(uint16_t *)(host->data_buf + host->buf_start);
	host->buf_start += 2;

	return ret;
}

/* Write data of length len to buffer buf. The data to be
 * written on NAND Flash is first copied to RAMbuffer. After the Data Input
 * Operation by the NFC, the data is written to NAND Flash */
static void mxc_nand_write_buf(struct mtd_info *mtd,
				const u_char *buf, int len)
{
	struct nand_chip *nand_chip = mtd_to_nand(mtd);
	struct mxc_nand_host *host = nand_get_controller_data(nand_chip);
	u16 col = host->buf_start;
	int n = mtd->oobsize + mtd->writesize - col;

	n = min(n, len);

	memcpy(host->data_buf + col, buf, n);

	host->buf_start += n;
}

/* Read the data buffer from the NAND Flash. To read the data from NAND
 * Flash first the data output cycle is initiated by the NFC, which copies
 * the data to RAMbuffer. This data of length len is then copied to buffer buf.
 */
static void mxc_nand_read_buf(struct mtd_info *mtd, u_char *buf, int len)
{
	struct nand_chip *nand_chip = mtd_to_nand(mtd);
	struct mxc_nand_host *host = nand_get_controller_data(nand_chip);
	u16 col = host->buf_start;
	int n = mtd->oobsize + mtd->writesize - col;

	n = min(n, len);

	memcpy(buf, host->data_buf + col, n);

	host->buf_start += n;
}

/* This function is used by upper layer for select and
 * deselect of the NAND chip */
static void mxc_nand_select_chip_v1_v3(struct mtd_info *mtd, int chip)
{
	struct nand_chip *nand_chip = mtd_to_nand(mtd);
	struct mxc_nand_host *host = nand_get_controller_data(nand_chip);

	if (chip == -1) {
		/* Disable the NFC clock */
		if (host->clk_act) {
			clk_disable_unprepare(host->clk);
			host->clk_act = 0;
		}
		return;
	}

	if (!host->clk_act) {
		/* Enable the NFC clock */
		clk_prepare_enable(host->clk);
		host->clk_act = 1;
	}
}

static void mxc_nand_select_chip_v2(struct mtd_info *mtd, int chip)
{
	struct nand_chip *nand_chip = mtd_to_nand(mtd);
	struct mxc_nand_host *host = nand_get_controller_data(nand_chip);

	if (chip == -1) {
		/* Disable the NFC clock */
		if (host->clk_act) {
			clk_disable_unprepare(host->clk);
			host->clk_act = 0;
		}
		return;
	}

	if (!host->clk_act) {
		/* Enable the NFC clock */
		clk_prepare_enable(host->clk);
		host->clk_act = 1;
	}

	host->active_cs = chip;
	writew(host->active_cs << 4, NFC_V1_V2_BUF_ADDR);
}

/*
 * The controller splits a page into data chunks of 512 bytes + partial oob.
 * There are writesize / 512 such chunks, the size of the partial oob parts is
 * oobsize / #chunks rounded down to a multiple of 2. The last oob chunk then
 * contains additionally the byte lost by rounding (if any).
 * This function handles the needed shuffling between host->data_buf (which
 * holds a page in natural order, i.e. writesize bytes data + oobsize bytes
 * spare) and the NFC buffer.
 */
static void copy_spare(struct mtd_info *mtd, bool bfrom)
{
	struct nand_chip *this = mtd_to_nand(mtd);
	struct mxc_nand_host *host = nand_get_controller_data(this);
	u16 i, oob_chunk_size;
	u16 num_chunks = mtd->writesize / 512;

	u8 *d = host->data_buf + mtd->writesize;
	u8 __iomem *s = host->spare0;
	u16 sparebuf_size = host->devtype_data->spare_len;

	/* size of oob chunk for all but possibly the last one */
	oob_chunk_size = (host->used_oobsize / num_chunks) & ~1;

	if (bfrom) {
		for (i = 0; i < num_chunks - 1; i++)
			memcpy16_fromio(d + i * oob_chunk_size,
					s + i * sparebuf_size,
					oob_chunk_size);

		/* the last chunk */
		memcpy16_fromio(d + i * oob_chunk_size,
				s + i * sparebuf_size,
				host->used_oobsize - i * oob_chunk_size);
	} else {
		for (i = 0; i < num_chunks - 1; i++)
			memcpy16_toio(&s[i * sparebuf_size],
				      &d[i * oob_chunk_size],
				      oob_chunk_size);

		/* the last chunk */
		memcpy16_toio(&s[i * sparebuf_size],
			      &d[i * oob_chunk_size],
			      host->used_oobsize - i * oob_chunk_size);
	}
}

/*
 * MXC NANDFC can only perform full page+spare or spare-only read/write.  When
 * the upper layers perform a read/write buf operation, the saved column address
 * is used to index into the full page. So usually this function is called with
 * column == 0 (unless no column cycle is needed indicated by column == -1)
 */
static void mxc_do_addr_cycle(struct mtd_info *mtd, int column, int page_addr)
{
	struct nand_chip *nand_chip = mtd_to_nand(mtd);
	struct mxc_nand_host *host = nand_get_controller_data(nand_chip);

	/* Write out column address, if necessary */
	if (column != -1) {
		host->devtype_data->send_addr(host, column & 0xff,
					      page_addr == -1);
		if (mtd->writesize > 512)
			/* another col addr cycle for 2k page */
			host->devtype_data->send_addr(host,
						      (column >> 8) & 0xff,
						      false);
	}

	/* Write out page address, if necessary */
	if (page_addr != -1) {
		/* paddr_0 - p_addr_7 */
		host->devtype_data->send_addr(host, (page_addr & 0xff), false);

		if (mtd->writesize > 512) {
			if (mtd->size >= 0x10000000) {
				/* paddr_8 - paddr_15 */
				host->devtype_data->send_addr(host,
						(page_addr >> 8) & 0xff,
						false);
				host->devtype_data->send_addr(host,
						(page_addr >> 16) & 0xff,
						true);
			} else
				/* paddr_8 - paddr_15 */
				host->devtype_data->send_addr(host,
						(page_addr >> 8) & 0xff, true);
		} else {
			if (nand_chip->options & NAND_ROW_ADDR_3) {
				/* paddr_8 - paddr_15 */
				host->devtype_data->send_addr(host,
						(page_addr >> 8) & 0xff,
						false);
				host->devtype_data->send_addr(host,
						(page_addr >> 16) & 0xff,
						true);
			} else
				/* paddr_8 - paddr_15 */
				host->devtype_data->send_addr(host,
						(page_addr >> 8) & 0xff, true);
		}
	}
}

#define MXC_V1_ECCBYTES		5

static int mxc_v1_ooblayout_ecc(struct mtd_info *mtd, int section,
				struct mtd_oob_region *oobregion)
{
	struct nand_chip *nand_chip = mtd_to_nand(mtd);

	if (section >= nand_chip->ecc.steps)
		return -ERANGE;

	oobregion->offset = (section * 16) + 6;
	oobregion->length = MXC_V1_ECCBYTES;

	return 0;
}

static int mxc_v1_ooblayout_free(struct mtd_info *mtd, int section,
				 struct mtd_oob_region *oobregion)
{
	struct nand_chip *nand_chip = mtd_to_nand(mtd);

	if (section > nand_chip->ecc.steps)
		return -ERANGE;

	if (!section) {
		if (mtd->writesize <= 512) {
			oobregion->offset = 0;
			oobregion->length = 5;
		} else {
			oobregion->offset = 2;
			oobregion->length = 4;
		}
	} else {
		oobregion->offset = ((section - 1) * 16) + MXC_V1_ECCBYTES + 6;
		if (section < nand_chip->ecc.steps)
			oobregion->length = (section * 16) + 6 -
					    oobregion->offset;
		else
			oobregion->length = mtd->oobsize - oobregion->offset;
	}

	return 0;
}

static const struct mtd_ooblayout_ops mxc_v1_ooblayout_ops = {
	.ecc = mxc_v1_ooblayout_ecc,
	.free = mxc_v1_ooblayout_free,
};

static int mxc_v2_ooblayout_ecc(struct mtd_info *mtd, int section,
				struct mtd_oob_region *oobregion)
{
	struct nand_chip *nand_chip = mtd_to_nand(mtd);
	int stepsize = nand_chip->ecc.bytes == 9 ? 16 : 26;

	if (section >= nand_chip->ecc.steps)
		return -ERANGE;

	oobregion->offset = (section * stepsize) + 7;
	oobregion->length = nand_chip->ecc.bytes;

	return 0;
}

static int mxc_v2_ooblayout_free(struct mtd_info *mtd, int section,
				 struct mtd_oob_region *oobregion)
{
	struct nand_chip *nand_chip = mtd_to_nand(mtd);
	int stepsize = nand_chip->ecc.bytes == 9 ? 16 : 26;

	if (section >= nand_chip->ecc.steps)
		return -ERANGE;

	if (!section) {
		if (mtd->writesize <= 512) {
			oobregion->offset = 0;
			oobregion->length = 5;
		} else {
			oobregion->offset = 2;
			oobregion->length = 4;
		}
	} else {
		oobregion->offset = section * stepsize;
		oobregion->length = 7;
	}

	return 0;
}

static const struct mtd_ooblayout_ops mxc_v2_ooblayout_ops = {
	.ecc = mxc_v2_ooblayout_ecc,
	.free = mxc_v2_ooblayout_free,
};

/*
 * v2 and v3 type controllers can do 4bit or 8bit ecc depending
 * on how much oob the nand chip has. For 8bit ecc we need at least
 * 26 bytes of oob data per 512 byte block.
 */
static int get_eccsize(struct mtd_info *mtd)
{
	int oobbytes_per_512 = 0;

	oobbytes_per_512 = mtd->oobsize * 512 / mtd->writesize;

	if (oobbytes_per_512 < 26)
		return 4;
	else
		return 8;
}

static void preset_v1(struct mtd_info *mtd)
{
	struct nand_chip *nand_chip = mtd_to_nand(mtd);
	struct mxc_nand_host *host = nand_get_controller_data(nand_chip);
	uint16_t config1 = 0;

	if (nand_chip->ecc.mode == NAND_ECC_HW && mtd->writesize)
		config1 |= NFC_V1_V2_CONFIG1_ECC_EN;

	if (!host->devtype_data->irqpending_quirk)
		config1 |= NFC_V1_V2_CONFIG1_INT_MSK;

	host->eccsize = 1;

	writew(config1, NFC_V1_V2_CONFIG1);
	/* preset operation */

	/* Unlock the internal RAM Buffer */
	writew(0x2, NFC_V1_V2_CONFIG);

	/* Blocks to be unlocked */
	writew(0x0, NFC_V1_UNLOCKSTART_BLKADDR);
	writew(0xffff, NFC_V1_UNLOCKEND_BLKADDR);

	/* Unlock Block Command for given address range */
	writew(0x4, NFC_V1_V2_WRPROT);
}

static int mxc_nand_v2_setup_data_interface(struct mtd_info *mtd, int csline,
					const struct nand_data_interface *conf)
{
	struct nand_chip *nand_chip = mtd_to_nand(mtd);
	struct mxc_nand_host *host = nand_get_controller_data(nand_chip);
	int tRC_min_ns, tRC_ps, ret;
	unsigned long rate, rate_round;
	const struct nand_sdr_timings *timings;
	u16 config1;

	timings = nand_get_sdr_timings(conf);
	if (IS_ERR(timings))
		return -ENOTSUPP;

	config1 = readw(NFC_V1_V2_CONFIG1);

	tRC_min_ns = timings->tRC_min / 1000;
	rate = 1000000000 / tRC_min_ns;

	/*
	 * For tRC < 30ns we have to use EDO mode. In this case the controller
	 * does one access per clock cycle. Otherwise the controller does one
	 * access in two clock cycles, thus we have to double the rate to the
	 * controller.
	 */
	if (tRC_min_ns < 30) {
		rate_round = clk_round_rate(host->clk, rate);
		config1 |= NFC_V2_CONFIG1_ONE_CYCLE;
		tRC_ps = 1000000000 / (rate_round / 1000);
	} else {
		rate *= 2;
		rate_round = clk_round_rate(host->clk, rate);
		config1 &= ~NFC_V2_CONFIG1_ONE_CYCLE;
		tRC_ps = 1000000000 / (rate_round / 1000 / 2);
	}

	/*
	 * The timing values compared against are from the i.MX25 Automotive
	 * datasheet, Table 50. NFC Timing Parameters
	 */
	if (timings->tCLS_min > tRC_ps - 1000 ||
	    timings->tCLH_min > tRC_ps - 2000 ||
	    timings->tCS_min > tRC_ps - 1000 ||
	    timings->tCH_min > tRC_ps - 2000 ||
	    timings->tWP_min > tRC_ps - 1500 ||
	    timings->tALS_min > tRC_ps ||
	    timings->tALH_min > tRC_ps - 3000 ||
	    timings->tDS_min > tRC_ps ||
	    timings->tDH_min > tRC_ps - 5000 ||
	    timings->tWC_min > 2 * tRC_ps ||
	    timings->tWH_min > tRC_ps - 2500 ||
	    timings->tRR_min > 6 * tRC_ps ||
	    timings->tRP_min > 3 * tRC_ps / 2 ||
	    timings->tRC_min > 2 * tRC_ps ||
	    timings->tREH_min > (tRC_ps / 2) - 2500) {
		dev_dbg(host->dev, "Timing out of bounds\n");
		return -EINVAL;
	}

	if (csline == NAND_DATA_IFACE_CHECK_ONLY)
		return 0;

	ret = clk_set_rate(host->clk, rate);
	if (ret)
		return ret;

	writew(config1, NFC_V1_V2_CONFIG1);

	dev_dbg(host->dev, "Setting rate to %ldHz, %s mode\n", rate_round,
		config1 & NFC_V2_CONFIG1_ONE_CYCLE ? "One cycle (EDO)" :
		"normal");

	return 0;
}

static void preset_v2(struct mtd_info *mtd)
{
	struct nand_chip *nand_chip = mtd_to_nand(mtd);
	struct mxc_nand_host *host = nand_get_controller_data(nand_chip);
	uint16_t config1 = 0;

	config1 |= NFC_V2_CONFIG1_FP_INT;

	if (!host->devtype_data->irqpending_quirk)
		config1 |= NFC_V1_V2_CONFIG1_INT_MSK;

	if (mtd->writesize) {
		uint16_t pages_per_block = mtd->erasesize / mtd->writesize;

		if (nand_chip->ecc.mode == NAND_ECC_HW)
			config1 |= NFC_V1_V2_CONFIG1_ECC_EN;

		host->eccsize = get_eccsize(mtd);
		if (host->eccsize == 4)
			config1 |= NFC_V2_CONFIG1_ECC_MODE_4;

		config1 |= NFC_V2_CONFIG1_PPB(ffs(pages_per_block) - 6);
	} else {
		host->eccsize = 1;
	}

	writew(config1, NFC_V1_V2_CONFIG1);
	/* preset operation */

	/* Unlock the internal RAM Buffer */
	writew(0x2, NFC_V1_V2_CONFIG);

	/* Blocks to be unlocked */
	writew(0x0, NFC_V21_UNLOCKSTART_BLKADDR0);
	writew(0x0, NFC_V21_UNLOCKSTART_BLKADDR1);
	writew(0x0, NFC_V21_UNLOCKSTART_BLKADDR2);
	writew(0x0, NFC_V21_UNLOCKSTART_BLKADDR3);
	writew(0xffff, NFC_V21_UNLOCKEND_BLKADDR0);
	writew(0xffff, NFC_V21_UNLOCKEND_BLKADDR1);
	writew(0xffff, NFC_V21_UNLOCKEND_BLKADDR2);
	writew(0xffff, NFC_V21_UNLOCKEND_BLKADDR3);

	/* Unlock Block Command for given address range */
	writew(0x4, NFC_V1_V2_WRPROT);
}

static void preset_v3(struct mtd_info *mtd)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct mxc_nand_host *host = nand_get_controller_data(chip);
	uint32_t config2, config3;
	int i, addr_phases;

	writel(NFC_V3_CONFIG1_RBA(0), NFC_V3_CONFIG1);
	writel(NFC_V3_IPC_CREQ, NFC_V3_IPC);

	/* Unlock the internal RAM Buffer */
	writel(NFC_V3_WRPROT_BLS_UNLOCK | NFC_V3_WRPROT_UNLOCK,
			NFC_V3_WRPROT);

	/* Blocks to be unlocked */
	for (i = 0; i < NAND_MAX_CHIPS; i++)
		writel(0xffff << 16, NFC_V3_WRPROT_UNLOCK_BLK_ADD0 + (i << 2));

	writel(0, NFC_V3_IPC);

	config2 = NFC_V3_CONFIG2_ONE_CYCLE |
		NFC_V3_CONFIG2_2CMD_PHASES |
		NFC_V3_CONFIG2_SPAS(mtd->oobsize >> 1) |
		NFC_V3_CONFIG2_ST_CMD(0x70) |
		NFC_V3_CONFIG2_INT_MSK |
		NFC_V3_CONFIG2_NUM_ADDR_PHASE0;

	addr_phases = fls(chip->pagemask) >> 3;

	if (mtd->writesize == 2048) {
		config2 |= NFC_V3_CONFIG2_PS_2048;
		config2 |= NFC_V3_CONFIG2_NUM_ADDR_PHASE1(addr_phases);
	} else if (mtd->writesize == 4096) {
		config2 |= NFC_V3_CONFIG2_PS_4096;
		config2 |= NFC_V3_CONFIG2_NUM_ADDR_PHASE1(addr_phases);
	} else {
		config2 |= NFC_V3_CONFIG2_PS_512;
		config2 |= NFC_V3_CONFIG2_NUM_ADDR_PHASE1(addr_phases - 1);
	}

	if (mtd->writesize) {
		if (chip->ecc.mode == NAND_ECC_HW)
			config2 |= NFC_V3_CONFIG2_ECC_EN;

		config2 |= NFC_V3_CONFIG2_PPB(
				ffs(mtd->erasesize / mtd->writesize) - 6,
				host->devtype_data->ppb_shift);
		host->eccsize = get_eccsize(mtd);
		if (host->eccsize == 8)
			config2 |= NFC_V3_CONFIG2_ECC_MODE_8;
	}

	writel(config2, NFC_V3_CONFIG2);

	config3 = NFC_V3_CONFIG3_NUM_OF_DEVICES(0) |
			NFC_V3_CONFIG3_NO_SDMA |
			NFC_V3_CONFIG3_RBB_MODE |
			NFC_V3_CONFIG3_SBB(6) | /* Reset default */
			NFC_V3_CONFIG3_ADD_OP(0);

	if (!(chip->options & NAND_BUSWIDTH_16))
		config3 |= NFC_V3_CONFIG3_FW8;

	writel(config3, NFC_V3_CONFIG3);

	writel(0, NFC_V3_DELAY_LINE);
}

/* Used by the upper layer to write command to NAND Flash for
 * different operations to be carried out on NAND Flash */
static void mxc_nand_command(struct mtd_info *mtd, unsigned command,
				int column, int page_addr)
{
	struct nand_chip *nand_chip = mtd_to_nand(mtd);
	struct mxc_nand_host *host = nand_get_controller_data(nand_chip);

	dev_dbg(host->dev, "mxc_nand_command (cmd = 0x%x, col = 0x%x, page = 0x%x)\n",
	      command, column, page_addr);

	/* Reset command state information */
	host->status_request = false;

	/* Command pre-processing step */
	switch (command) {
	case NAND_CMD_RESET:
		host->devtype_data->preset(mtd);
		host->devtype_data->send_cmd(host, command, false);
		break;

	case NAND_CMD_STATUS:
		host->buf_start = 0;
		host->status_request = true;

		host->devtype_data->send_cmd(host, command, true);
		WARN_ONCE(column != -1 || page_addr != -1,
			  "Unexpected column/row value (cmd=%u, col=%d, row=%d)\n",
			  command, column, page_addr);
		mxc_do_addr_cycle(mtd, column, page_addr);
		break;

	case NAND_CMD_READ0:
	case NAND_CMD_READOOB:
		if (command == NAND_CMD_READ0)
			host->buf_start = column;
		else
			host->buf_start = column + mtd->writesize;

		command = NAND_CMD_READ0; /* only READ0 is valid */

		host->devtype_data->send_cmd(host, command, false);
		WARN_ONCE(column < 0,
			  "Unexpected column/row value (cmd=%u, col=%d, row=%d)\n",
			  command, column, page_addr);
		mxc_do_addr_cycle(mtd, 0, page_addr);

		if (mtd->writesize > 512)
			host->devtype_data->send_cmd(host,
					NAND_CMD_READSTART, true);

		host->devtype_data->send_page(mtd, NFC_OUTPUT);

		memcpy32_fromio(host->data_buf, host->main_area0,
				mtd->writesize);
		copy_spare(mtd, true);
		break;

	case NAND_CMD_SEQIN:
		if (column >= mtd->writesize)
			/* call ourself to read a page */
			mxc_nand_command(mtd, NAND_CMD_READ0, 0, page_addr);

		host->buf_start = column;

		host->devtype_data->send_cmd(host, command, false);
		WARN_ONCE(column < -1,
			  "Unexpected column/row value (cmd=%u, col=%d, row=%d)\n",
			  command, column, page_addr);
		mxc_do_addr_cycle(mtd, 0, page_addr);
		break;

	case NAND_CMD_PAGEPROG:
		memcpy32_toio(host->main_area0, host->data_buf, mtd->writesize);
		copy_spare(mtd, false);
		host->devtype_data->send_page(mtd, NFC_INPUT);
		host->devtype_data->send_cmd(host, command, true);
		WARN_ONCE(column != -1 || page_addr != -1,
			  "Unexpected column/row value (cmd=%u, col=%d, row=%d)\n",
			  command, column, page_addr);
		mxc_do_addr_cycle(mtd, column, page_addr);
		break;

	case NAND_CMD_READID:
		host->devtype_data->send_cmd(host, command, true);
		mxc_do_addr_cycle(mtd, column, page_addr);
		host->devtype_data->send_read_id(host);
		host->buf_start = 0;
		break;

	case NAND_CMD_ERASE1:
	case NAND_CMD_ERASE2:
		host->devtype_data->send_cmd(host, command, false);
		WARN_ONCE(column != -1,
			  "Unexpected column value (cmd=%u, col=%d)\n",
			  command, column);
		mxc_do_addr_cycle(mtd, column, page_addr);

		break;
	case NAND_CMD_PARAM:
		host->devtype_data->send_cmd(host, command, false);
		mxc_do_addr_cycle(mtd, column, page_addr);
		host->devtype_data->send_page(mtd, NFC_OUTPUT);
		memcpy32_fromio(host->data_buf, host->main_area0, 512);
		host->buf_start = 0;
		break;
	default:
		WARN_ONCE(1, "Unimplemented command (cmd=%u)\n",
			  command);
		break;
	}
}

static int mxc_nand_onfi_set_features(struct mtd_info *mtd,
				      struct nand_chip *chip, int addr,
				      u8 *subfeature_param)
{
	struct nand_chip *nand_chip = mtd_to_nand(mtd);
	struct mxc_nand_host *host = nand_get_controller_data(nand_chip);
	int i;

	if (!chip->onfi_version ||
	    !(le16_to_cpu(chip->onfi_params.opt_cmd)
	      & ONFI_OPT_CMD_SET_GET_FEATURES))
		return -EINVAL;

	host->buf_start = 0;

	for (i = 0; i < ONFI_SUBFEATURE_PARAM_LEN; ++i)
		chip->write_byte(mtd, subfeature_param[i]);

	memcpy32_toio(host->main_area0, host->data_buf, mtd->writesize);
	host->devtype_data->send_cmd(host, NAND_CMD_SET_FEATURES, false);
	mxc_do_addr_cycle(mtd, addr, -1);
	host->devtype_data->send_page(mtd, NFC_INPUT);

	return 0;
}

static int mxc_nand_onfi_get_features(struct mtd_info *mtd,
				      struct nand_chip *chip, int addr,
				      u8 *subfeature_param)
{
	struct nand_chip *nand_chip = mtd_to_nand(mtd);
	struct mxc_nand_host *host = nand_get_controller_data(nand_chip);
	int i;

	if (!chip->onfi_version ||
	    !(le16_to_cpu(chip->onfi_params.opt_cmd)
	      & ONFI_OPT_CMD_SET_GET_FEATURES))
		return -EINVAL;

	host->devtype_data->send_cmd(host, NAND_CMD_GET_FEATURES, false);
	mxc_do_addr_cycle(mtd, addr, -1);
	host->devtype_data->send_page(mtd, NFC_OUTPUT);
	memcpy32_fromio(host->data_buf, host->main_area0, 512);
	host->buf_start = 0;

	for (i = 0; i < ONFI_SUBFEATURE_PARAM_LEN; ++i)
		*subfeature_param++ = chip->read_byte(mtd);

	return 0;
}

/*
 * The generic flash bbt decriptors overlap with our ecc
 * hardware, so define some i.MX specific ones.
 */
static uint8_t bbt_pattern[] = { 'B', 'b', 't', '0' };
static uint8_t mirror_pattern[] = { '1', 't', 'b', 'B' };

static struct nand_bbt_descr bbt_main_descr = {
	.options = NAND_BBT_LASTBLOCK | NAND_BBT_CREATE | NAND_BBT_WRITE
	    | NAND_BBT_2BIT | NAND_BBT_VERSION | NAND_BBT_PERCHIP,
	.offs = 0,
	.len = 4,
	.veroffs = 4,
	.maxblocks = 4,
	.pattern = bbt_pattern,
};

static struct nand_bbt_descr bbt_mirror_descr = {
	.options = NAND_BBT_LASTBLOCK | NAND_BBT_CREATE | NAND_BBT_WRITE
	    | NAND_BBT_2BIT | NAND_BBT_VERSION | NAND_BBT_PERCHIP,
	.offs = 0,
	.len = 4,
	.veroffs = 4,
	.maxblocks = 4,
	.pattern = mirror_pattern,
};

/* v1 + irqpending_quirk: i.MX21 */
static const struct mxc_nand_devtype_data imx21_nand_devtype_data = {
	.preset = preset_v1,
	.send_cmd = send_cmd_v1_v2,
	.send_addr = send_addr_v1_v2,
	.send_page = send_page_v1,
	.send_read_id = send_read_id_v1_v2,
	.get_dev_status = get_dev_status_v1_v2,
	.check_int = check_int_v1_v2,
	.irq_control = irq_control_v1_v2,
	.get_ecc_status = get_ecc_status_v1,
	.ooblayout = &mxc_v1_ooblayout_ops,
	.select_chip = mxc_nand_select_chip_v1_v3,
	.correct_data = mxc_nand_correct_data_v1,
	.irqpending_quirk = 1,
	.needs_ip = 0,
	.regs_offset = 0xe00,
	.spare0_offset = 0x800,
	.spare_len = 16,
	.eccbytes = 3,
	.eccsize = 1,
};

/* v1 + !irqpending_quirk: i.MX27, i.MX31 */
static const struct mxc_nand_devtype_data imx27_nand_devtype_data = {
	.preset = preset_v1,
	.send_cmd = send_cmd_v1_v2,
	.send_addr = send_addr_v1_v2,
	.send_page = send_page_v1,
	.send_read_id = send_read_id_v1_v2,
	.get_dev_status = get_dev_status_v1_v2,
	.check_int = check_int_v1_v2,
	.irq_control = irq_control_v1_v2,
	.get_ecc_status = get_ecc_status_v1,
	.ooblayout = &mxc_v1_ooblayout_ops,
	.select_chip = mxc_nand_select_chip_v1_v3,
	.correct_data = mxc_nand_correct_data_v1,
	.irqpending_quirk = 0,
	.needs_ip = 0,
	.regs_offset = 0xe00,
	.spare0_offset = 0x800,
	.axi_offset = 0,
	.spare_len = 16,
	.eccbytes = 3,
	.eccsize = 1,
};

/* v21: i.MX25, i.MX35 */
static const struct mxc_nand_devtype_data imx25_nand_devtype_data = {
	.preset = preset_v2,
	.send_cmd = send_cmd_v1_v2,
	.send_addr = send_addr_v1_v2,
	.send_page = send_page_v2,
	.send_read_id = send_read_id_v1_v2,
	.get_dev_status = get_dev_status_v1_v2,
	.check_int = check_int_v1_v2,
	.irq_control = irq_control_v1_v2,
	.get_ecc_status = get_ecc_status_v2,
	.ooblayout = &mxc_v2_ooblayout_ops,
	.select_chip = mxc_nand_select_chip_v2,
	.correct_data = mxc_nand_correct_data_v2_v3,
	.setup_data_interface = mxc_nand_v2_setup_data_interface,
	.irqpending_quirk = 0,
	.needs_ip = 0,
	.regs_offset = 0x1e00,
	.spare0_offset = 0x1000,
	.axi_offset = 0,
	.spare_len = 64,
	.eccbytes = 9,
	.eccsize = 0,
};

/* v3.2a: i.MX51 */
static const struct mxc_nand_devtype_data imx51_nand_devtype_data = {
	.preset = preset_v3,
	.send_cmd = send_cmd_v3,
	.send_addr = send_addr_v3,
	.send_page = send_page_v3,
	.send_read_id = send_read_id_v3,
	.get_dev_status = get_dev_status_v3,
	.check_int = check_int_v3,
	.irq_control = irq_control_v3,
	.get_ecc_status = get_ecc_status_v3,
	.ooblayout = &mxc_v2_ooblayout_ops,
	.select_chip = mxc_nand_select_chip_v1_v3,
	.correct_data = mxc_nand_correct_data_v2_v3,
	.irqpending_quirk = 0,
	.needs_ip = 1,
	.regs_offset = 0,
	.spare0_offset = 0x1000,
	.axi_offset = 0x1e00,
	.spare_len = 64,
	.eccbytes = 0,
	.eccsize = 0,
	.ppb_shift = 7,
};

/* v3.2b: i.MX53 */
static const struct mxc_nand_devtype_data imx53_nand_devtype_data = {
	.preset = preset_v3,
	.send_cmd = send_cmd_v3,
	.send_addr = send_addr_v3,
	.send_page = send_page_v3,
	.send_read_id = send_read_id_v3,
	.get_dev_status = get_dev_status_v3,
	.check_int = check_int_v3,
	.irq_control = irq_control_v3,
	.get_ecc_status = get_ecc_status_v3,
	.ooblayout = &mxc_v2_ooblayout_ops,
	.select_chip = mxc_nand_select_chip_v1_v3,
	.correct_data = mxc_nand_correct_data_v2_v3,
	.irqpending_quirk = 0,
	.needs_ip = 1,
	.regs_offset = 0,
	.spare0_offset = 0x1000,
	.axi_offset = 0x1e00,
	.spare_len = 64,
	.eccbytes = 0,
	.eccsize = 0,
	.ppb_shift = 8,
};

static inline int is_imx21_nfc(struct mxc_nand_host *host)
{
	return host->devtype_data == &imx21_nand_devtype_data;
}

static inline int is_imx27_nfc(struct mxc_nand_host *host)
{
	return host->devtype_data == &imx27_nand_devtype_data;
}

static inline int is_imx25_nfc(struct mxc_nand_host *host)
{
	return host->devtype_data == &imx25_nand_devtype_data;
}

static inline int is_imx51_nfc(struct mxc_nand_host *host)
{
	return host->devtype_data == &imx51_nand_devtype_data;
}

static inline int is_imx53_nfc(struct mxc_nand_host *host)
{
	return host->devtype_data == &imx53_nand_devtype_data;
}

static const struct platform_device_id mxcnd_devtype[] = {
	{
		.name = "imx21-nand",
		.driver_data = (kernel_ulong_t) &imx21_nand_devtype_data,
	}, {
		.name = "imx27-nand",
		.driver_data = (kernel_ulong_t) &imx27_nand_devtype_data,
	}, {
		.name = "imx25-nand",
		.driver_data = (kernel_ulong_t) &imx25_nand_devtype_data,
	}, {
		.name = "imx51-nand",
		.driver_data = (kernel_ulong_t) &imx51_nand_devtype_data,
	}, {
		.name = "imx53-nand",
		.driver_data = (kernel_ulong_t) &imx53_nand_devtype_data,
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(platform, mxcnd_devtype);

#ifdef CONFIG_OF
static const struct of_device_id mxcnd_dt_ids[] = {
	{
		.compatible = "fsl,imx21-nand",
		.data = &imx21_nand_devtype_data,
	}, {
		.compatible = "fsl,imx27-nand",
		.data = &imx27_nand_devtype_data,
	}, {
		.compatible = "fsl,imx25-nand",
		.data = &imx25_nand_devtype_data,
	}, {
		.compatible = "fsl,imx51-nand",
		.data = &imx51_nand_devtype_data,
	}, {
		.compatible = "fsl,imx53-nand",
		.data = &imx53_nand_devtype_data,
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mxcnd_dt_ids);

static int __init mxcnd_probe_dt(struct mxc_nand_host *host)
{
	struct device_node *np = host->dev->of_node;
	const struct of_device_id *of_id =
		of_match_device(mxcnd_dt_ids, host->dev);

	if (!np)
		return 1;

	host->devtype_data = of_id->data;

	return 0;
}
#else
static int __init mxcnd_probe_dt(struct mxc_nand_host *host)
{
	return 1;
}
#endif

static int mxcnd_probe(struct platform_device *pdev)
{
	struct nand_chip *this;
	struct mtd_info *mtd;
	struct mxc_nand_host *host;
	struct resource *res;
	int err = 0;

	/* Allocate memory for MTD device structure and private data */
	host = devm_kzalloc(&pdev->dev, sizeof(struct mxc_nand_host),
			GFP_KERNEL);
	if (!host)
		return -ENOMEM;

	/* allocate a temporary buffer for the nand_scan_ident() */
	host->data_buf = devm_kzalloc(&pdev->dev, PAGE_SIZE, GFP_KERNEL);
	if (!host->data_buf)
		return -ENOMEM;

	host->dev = &pdev->dev;
	/* structures must be linked */
	this = &host->nand;
	mtd = nand_to_mtd(this);
	mtd->dev.parent = &pdev->dev;
	mtd->name = DRIVER_NAME;

	/* 50 us command delay time */
	this->chip_delay = 5;

	nand_set_controller_data(this, host);
	nand_set_flash_node(this, pdev->dev.of_node),
	this->dev_ready = mxc_nand_dev_ready;
	this->cmdfunc = mxc_nand_command;
	this->read_byte = mxc_nand_read_byte;
	this->read_word = mxc_nand_read_word;
	this->write_buf = mxc_nand_write_buf;
	this->read_buf = mxc_nand_read_buf;
	this->onfi_set_features = mxc_nand_onfi_set_features;
	this->onfi_get_features = mxc_nand_onfi_get_features;

	host->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(host->clk))
		return PTR_ERR(host->clk);

	err = mxcnd_probe_dt(host);
	if (err > 0) {
		struct mxc_nand_platform_data *pdata =
					dev_get_platdata(&pdev->dev);
		if (pdata) {
			host->pdata = *pdata;
			host->devtype_data = (struct mxc_nand_devtype_data *)
						pdev->id_entry->driver_data;
		} else {
			err = -ENODEV;
		}
	}
	if (err < 0)
		return err;

	this->setup_data_interface = host->devtype_data->setup_data_interface;

	if (host->devtype_data->needs_ip) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
		host->regs_ip = devm_ioremap_resource(&pdev->dev, res);
		if (IS_ERR(host->regs_ip))
			return PTR_ERR(host->regs_ip);

		res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	} else {
		res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	}

	host->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(host->base))
		return PTR_ERR(host->base);

	host->main_area0 = host->base;

	if (host->devtype_data->regs_offset)
		host->regs = host->base + host->devtype_data->regs_offset;
	host->spare0 = host->base + host->devtype_data->spare0_offset;
	if (host->devtype_data->axi_offset)
		host->regs_axi = host->base + host->devtype_data->axi_offset;

	this->ecc.bytes = host->devtype_data->eccbytes;
	host->eccsize = host->devtype_data->eccsize;

	this->select_chip = host->devtype_data->select_chip;
	this->ecc.size = 512;
	mtd_set_ooblayout(mtd, host->devtype_data->ooblayout);

	if (host->pdata.hw_ecc) {
		this->ecc.mode = NAND_ECC_HW;
	} else {
		this->ecc.mode = NAND_ECC_SOFT;
		this->ecc.algo = NAND_ECC_HAMMING;
	}

	/* NAND bus width determines access functions used by upper layer */
	if (host->pdata.width == 2)
		this->options |= NAND_BUSWIDTH_16;

	/* update flash based bbt */
	if (host->pdata.flash_bbt)
		this->bbt_options |= NAND_BBT_USE_FLASH;

	init_completion(&host->op_completion);

	host->irq = platform_get_irq(pdev, 0);
	if (host->irq < 0)
		return host->irq;

	/*
	 * Use host->devtype_data->irq_control() here instead of irq_control()
	 * because we must not disable_irq_nosync without having requested the
	 * irq.
	 */
	host->devtype_data->irq_control(host, 0);

	err = devm_request_irq(&pdev->dev, host->irq, mxc_nfc_irq,
			0, DRIVER_NAME, host);
	if (err)
		return err;

	err = clk_prepare_enable(host->clk);
	if (err)
		return err;
	host->clk_act = 1;

	/*
	 * Now that we "own" the interrupt make sure the interrupt mask bit is
	 * cleared on i.MX21. Otherwise we can't read the interrupt status bit
	 * on this machine.
	 */
	if (host->devtype_data->irqpending_quirk) {
		disable_irq_nosync(host->irq);
		host->devtype_data->irq_control(host, 1);
	}

	/* first scan to find the device and get the page size */
	err = nand_scan_ident(mtd, is_imx25_nfc(host) ? 4 : 1, NULL);
	if (err)
		goto escan;

	switch (this->ecc.mode) {
	case NAND_ECC_HW:
		this->ecc.calculate = mxc_nand_calculate_ecc;
		this->ecc.hwctl = mxc_nand_enable_hwecc;
		this->ecc.correct = host->devtype_data->correct_data;
		break;

	case NAND_ECC_SOFT:
		break;

	default:
		err = -EINVAL;
		goto escan;
	}

	if (this->bbt_options & NAND_BBT_USE_FLASH) {
		this->bbt_td = &bbt_main_descr;
		this->bbt_md = &bbt_mirror_descr;
	}

	/* allocate the right size buffer now */
	devm_kfree(&pdev->dev, (void *)host->data_buf);
	host->data_buf = devm_kzalloc(&pdev->dev, mtd->writesize + mtd->oobsize,
					GFP_KERNEL);
	if (!host->data_buf) {
		err = -ENOMEM;
		goto escan;
	}

	/* Call preset again, with correct writesize this time */
	host->devtype_data->preset(mtd);

	if (!this->ecc.bytes) {
		if (host->eccsize == 8)
			this->ecc.bytes = 18;
		else if (host->eccsize == 4)
			this->ecc.bytes = 9;
	}

	/*
	 * Experimentation shows that i.MX NFC can only handle up to 218 oob
	 * bytes. Limit used_oobsize to 218 so as to not confuse copy_spare()
	 * into copying invalid data to/from the spare IO buffer, as this
	 * might cause ECC data corruption when doing sub-page write to a
	 * partially written page.
	 */
	host->used_oobsize = min(mtd->oobsize, 218U);

	if (this->ecc.mode == NAND_ECC_HW) {
		if (is_imx21_nfc(host) || is_imx27_nfc(host))
			this->ecc.strength = 1;
		else
			this->ecc.strength = (host->eccsize == 4) ? 4 : 8;
	}

	/* second phase scan */
	err = nand_scan_tail(mtd);
	if (err)
		goto escan;

	/* Register the partitions */
	mtd_device_parse_register(mtd, part_probes,
			NULL,
			host->pdata.parts,
			host->pdata.nr_parts);

	platform_set_drvdata(pdev, host);

	return 0;

escan:
	if (host->clk_act)
		clk_disable_unprepare(host->clk);

	return err;
}

static int mxcnd_remove(struct platform_device *pdev)
{
	struct mxc_nand_host *host = platform_get_drvdata(pdev);

	nand_release(nand_to_mtd(&host->nand));
	if (host->clk_act)
		clk_disable_unprepare(host->clk);

	return 0;
}

static struct platform_driver mxcnd_driver = {
	.driver = {
		   .name = DRIVER_NAME,
		   .of_match_table = of_match_ptr(mxcnd_dt_ids),
	},
	.id_table = mxcnd_devtype,
	.probe = mxcnd_probe,
	.remove = mxcnd_remove,
};
module_platform_driver(mxcnd_driver);

MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_DESCRIPTION("MXC NAND MTD driver");
MODULE_LICENSE("GPL");
