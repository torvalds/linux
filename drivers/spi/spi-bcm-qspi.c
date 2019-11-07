// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for Broadcom BRCMSTB, NSP,  NS2, Cygnus SPI Controllers
 *
 * Copyright 2016 Broadcom
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi-mem.h>
#include <linux/sysfs.h>
#include <linux/types.h>
#include "spi-bcm-qspi.h"

#define DRIVER_NAME "bcm_qspi"


/* BSPI register offsets */
#define BSPI_REVISION_ID			0x000
#define BSPI_SCRATCH				0x004
#define BSPI_MAST_N_BOOT_CTRL			0x008
#define BSPI_BUSY_STATUS			0x00c
#define BSPI_INTR_STATUS			0x010
#define BSPI_B0_STATUS				0x014
#define BSPI_B0_CTRL				0x018
#define BSPI_B1_STATUS				0x01c
#define BSPI_B1_CTRL				0x020
#define BSPI_STRAP_OVERRIDE_CTRL		0x024
#define BSPI_FLEX_MODE_ENABLE			0x028
#define BSPI_BITS_PER_CYCLE			0x02c
#define BSPI_BITS_PER_PHASE			0x030
#define BSPI_CMD_AND_MODE_BYTE			0x034
#define BSPI_BSPI_FLASH_UPPER_ADDR_BYTE	0x038
#define BSPI_BSPI_XOR_VALUE			0x03c
#define BSPI_BSPI_XOR_ENABLE			0x040
#define BSPI_BSPI_PIO_MODE_ENABLE		0x044
#define BSPI_BSPI_PIO_IODIR			0x048
#define BSPI_BSPI_PIO_DATA			0x04c

/* RAF register offsets */
#define BSPI_RAF_START_ADDR			0x100
#define BSPI_RAF_NUM_WORDS			0x104
#define BSPI_RAF_CTRL				0x108
#define BSPI_RAF_FULLNESS			0x10c
#define BSPI_RAF_WATERMARK			0x110
#define BSPI_RAF_STATUS			0x114
#define BSPI_RAF_READ_DATA			0x118
#define BSPI_RAF_WORD_CNT			0x11c
#define BSPI_RAF_CURR_ADDR			0x120

/* Override mode masks */
#define BSPI_STRAP_OVERRIDE_CTRL_OVERRIDE	BIT(0)
#define BSPI_STRAP_OVERRIDE_CTRL_DATA_DUAL	BIT(1)
#define BSPI_STRAP_OVERRIDE_CTRL_ADDR_4BYTE	BIT(2)
#define BSPI_STRAP_OVERRIDE_CTRL_DATA_QUAD	BIT(3)
#define BSPI_STRAP_OVERRIDE_CTRL_ENDAIN_MODE	BIT(4)

#define BSPI_ADDRLEN_3BYTES			3
#define BSPI_ADDRLEN_4BYTES			4

#define BSPI_RAF_STATUS_FIFO_EMPTY_MASK	BIT(1)

#define BSPI_RAF_CTRL_START_MASK		BIT(0)
#define BSPI_RAF_CTRL_CLEAR_MASK		BIT(1)

#define BSPI_BPP_MODE_SELECT_MASK		BIT(8)
#define BSPI_BPP_ADDR_SELECT_MASK		BIT(16)

#define BSPI_READ_LENGTH			256

/* MSPI register offsets */
#define MSPI_SPCR0_LSB				0x000
#define MSPI_SPCR0_MSB				0x004
#define MSPI_SPCR1_LSB				0x008
#define MSPI_SPCR1_MSB				0x00c
#define MSPI_NEWQP				0x010
#define MSPI_ENDQP				0x014
#define MSPI_SPCR2				0x018
#define MSPI_MSPI_STATUS			0x020
#define MSPI_CPTQP				0x024
#define MSPI_SPCR3				0x028
#define MSPI_TXRAM				0x040
#define MSPI_RXRAM				0x0c0
#define MSPI_CDRAM				0x140
#define MSPI_WRITE_LOCK			0x180

#define MSPI_MASTER_BIT			BIT(7)

#define MSPI_NUM_CDRAM				16
#define MSPI_CDRAM_CONT_BIT			BIT(7)
#define MSPI_CDRAM_BITSE_BIT			BIT(6)
#define MSPI_CDRAM_PCS				0xf

#define MSPI_SPCR2_SPE				BIT(6)
#define MSPI_SPCR2_CONT_AFTER_CMD		BIT(7)

#define MSPI_MSPI_STATUS_SPIF			BIT(0)

#define INTR_BASE_BIT_SHIFT			0x02
#define INTR_COUNT				0x07

#define NUM_CHIPSELECT				4
#define QSPI_SPBR_MIN				8U
#define QSPI_SPBR_MAX				255U

#define OPCODE_DIOR				0xBB
#define OPCODE_QIOR				0xEB
#define OPCODE_DIOR_4B				0xBC
#define OPCODE_QIOR_4B				0xEC

#define MAX_CMD_SIZE				6

#define ADDR_4MB_MASK				GENMASK(22, 0)

/* stop at end of transfer, no other reason */
#define TRANS_STATUS_BREAK_NONE		0
/* stop at end of spi_message */
#define TRANS_STATUS_BREAK_EOM			1
/* stop at end of spi_transfer if delay */
#define TRANS_STATUS_BREAK_DELAY		2
/* stop at end of spi_transfer if cs_change */
#define TRANS_STATUS_BREAK_CS_CHANGE		4
/* stop if we run out of bytes */
#define TRANS_STATUS_BREAK_NO_BYTES		8

/* events that make us stop filling TX slots */
#define TRANS_STATUS_BREAK_TX (TRANS_STATUS_BREAK_EOM |		\
			       TRANS_STATUS_BREAK_DELAY |		\
			       TRANS_STATUS_BREAK_CS_CHANGE)

/* events that make us deassert CS */
#define TRANS_STATUS_BREAK_DESELECT (TRANS_STATUS_BREAK_EOM |		\
				     TRANS_STATUS_BREAK_CS_CHANGE)

struct bcm_qspi_parms {
	u32 speed_hz;
	u8 mode;
	u8 bits_per_word;
};

struct bcm_xfer_mode {
	bool flex_mode;
	unsigned int width;
	unsigned int addrlen;
	unsigned int hp;
};

enum base_type {
	MSPI,
	BSPI,
	CHIP_SELECT,
	BASEMAX,
};

enum irq_source {
	SINGLE_L2,
	MUXED_L1,
};

struct bcm_qspi_irq {
	const char *irq_name;
	const irq_handler_t irq_handler;
	int irq_source;
	u32 mask;
};

struct bcm_qspi_dev_id {
	const struct bcm_qspi_irq *irqp;
	void *dev;
};


struct qspi_trans {
	struct spi_transfer *trans;
	int byte;
	bool mspi_last_trans;
};

struct bcm_qspi {
	struct platform_device *pdev;
	struct spi_master *master;
	struct clk *clk;
	u32 base_clk;
	u32 max_speed_hz;
	void __iomem *base[BASEMAX];

	/* Some SoCs provide custom interrupt status register(s) */
	struct bcm_qspi_soc_intc	*soc_intc;

	struct bcm_qspi_parms last_parms;
	struct qspi_trans  trans_pos;
	int curr_cs;
	int bspi_maj_rev;
	int bspi_min_rev;
	int bspi_enabled;
	const struct spi_mem_op *bspi_rf_op;
	u32 bspi_rf_op_idx;
	u32 bspi_rf_op_len;
	u32 bspi_rf_op_status;
	struct bcm_xfer_mode xfer_mode;
	u32 s3_strap_override_ctrl;
	bool bspi_mode;
	bool big_endian;
	int num_irqs;
	struct bcm_qspi_dev_id *dev_ids;
	struct completion mspi_done;
	struct completion bspi_done;
};

static inline bool has_bspi(struct bcm_qspi *qspi)
{
	return qspi->bspi_mode;
}

/* Read qspi controller register*/
static inline u32 bcm_qspi_read(struct bcm_qspi *qspi, enum base_type type,
				unsigned int offset)
{
	return bcm_qspi_readl(qspi->big_endian, qspi->base[type] + offset);
}

/* Write qspi controller register*/
static inline void bcm_qspi_write(struct bcm_qspi *qspi, enum base_type type,
				  unsigned int offset, unsigned int data)
{
	bcm_qspi_writel(qspi->big_endian, data, qspi->base[type] + offset);
}

/* BSPI helpers */
static int bcm_qspi_bspi_busy_poll(struct bcm_qspi *qspi)
{
	int i;

	/* this should normally finish within 10us */
	for (i = 0; i < 1000; i++) {
		if (!(bcm_qspi_read(qspi, BSPI, BSPI_BUSY_STATUS) & 1))
			return 0;
		udelay(1);
	}
	dev_warn(&qspi->pdev->dev, "timeout waiting for !busy_status\n");
	return -EIO;
}

static inline bool bcm_qspi_bspi_ver_three(struct bcm_qspi *qspi)
{
	if (qspi->bspi_maj_rev < 4)
		return true;
	return false;
}

static void bcm_qspi_bspi_flush_prefetch_buffers(struct bcm_qspi *qspi)
{
	bcm_qspi_bspi_busy_poll(qspi);
	/* Force rising edge for the b0/b1 'flush' field */
	bcm_qspi_write(qspi, BSPI, BSPI_B0_CTRL, 1);
	bcm_qspi_write(qspi, BSPI, BSPI_B1_CTRL, 1);
	bcm_qspi_write(qspi, BSPI, BSPI_B0_CTRL, 0);
	bcm_qspi_write(qspi, BSPI, BSPI_B1_CTRL, 0);
}

static int bcm_qspi_bspi_lr_is_fifo_empty(struct bcm_qspi *qspi)
{
	return (bcm_qspi_read(qspi, BSPI, BSPI_RAF_STATUS) &
				BSPI_RAF_STATUS_FIFO_EMPTY_MASK);
}

static inline u32 bcm_qspi_bspi_lr_read_fifo(struct bcm_qspi *qspi)
{
	u32 data = bcm_qspi_read(qspi, BSPI, BSPI_RAF_READ_DATA);

	/* BSPI v3 LR is LE only, convert data to host endianness */
	if (bcm_qspi_bspi_ver_three(qspi))
		data = le32_to_cpu(data);

	return data;
}

static inline void bcm_qspi_bspi_lr_start(struct bcm_qspi *qspi)
{
	bcm_qspi_bspi_busy_poll(qspi);
	bcm_qspi_write(qspi, BSPI, BSPI_RAF_CTRL,
		       BSPI_RAF_CTRL_START_MASK);
}

static inline void bcm_qspi_bspi_lr_clear(struct bcm_qspi *qspi)
{
	bcm_qspi_write(qspi, BSPI, BSPI_RAF_CTRL,
		       BSPI_RAF_CTRL_CLEAR_MASK);
	bcm_qspi_bspi_flush_prefetch_buffers(qspi);
}

static void bcm_qspi_bspi_lr_data_read(struct bcm_qspi *qspi)
{
	u32 *buf = (u32 *)qspi->bspi_rf_op->data.buf.in;
	u32 data = 0;

	dev_dbg(&qspi->pdev->dev, "xfer %p rx %p rxlen %d\n", qspi->bspi_rf_op,
		qspi->bspi_rf_op->data.buf.in, qspi->bspi_rf_op_len);
	while (!bcm_qspi_bspi_lr_is_fifo_empty(qspi)) {
		data = bcm_qspi_bspi_lr_read_fifo(qspi);
		if (likely(qspi->bspi_rf_op_len >= 4) &&
		    IS_ALIGNED((uintptr_t)buf, 4)) {
			buf[qspi->bspi_rf_op_idx++] = data;
			qspi->bspi_rf_op_len -= 4;
		} else {
			/* Read out remaining bytes, make sure*/
			u8 *cbuf = (u8 *)&buf[qspi->bspi_rf_op_idx];

			data = cpu_to_le32(data);
			while (qspi->bspi_rf_op_len) {
				*cbuf++ = (u8)data;
				data >>= 8;
				qspi->bspi_rf_op_len--;
			}
		}
	}
}

static void bcm_qspi_bspi_set_xfer_params(struct bcm_qspi *qspi, u8 cmd_byte,
					  int bpp, int bpc, int flex_mode)
{
	bcm_qspi_write(qspi, BSPI, BSPI_FLEX_MODE_ENABLE, 0);
	bcm_qspi_write(qspi, BSPI, BSPI_BITS_PER_CYCLE, bpc);
	bcm_qspi_write(qspi, BSPI, BSPI_BITS_PER_PHASE, bpp);
	bcm_qspi_write(qspi, BSPI, BSPI_CMD_AND_MODE_BYTE, cmd_byte);
	bcm_qspi_write(qspi, BSPI, BSPI_FLEX_MODE_ENABLE, flex_mode);
}

static int bcm_qspi_bspi_set_flex_mode(struct bcm_qspi *qspi,
				       const struct spi_mem_op *op, int hp)
{
	int bpc = 0, bpp = 0;
	u8 command = op->cmd.opcode;
	int width = op->data.buswidth ? op->data.buswidth : SPI_NBITS_SINGLE;
	int addrlen = op->addr.nbytes;
	int flex_mode = 1;

	dev_dbg(&qspi->pdev->dev, "set flex mode w %x addrlen %x hp %d\n",
		width, addrlen, hp);

	if (addrlen == BSPI_ADDRLEN_4BYTES)
		bpp = BSPI_BPP_ADDR_SELECT_MASK;

	bpp |= (op->dummy.nbytes * 8) / op->dummy.buswidth;

	switch (width) {
	case SPI_NBITS_SINGLE:
		if (addrlen == BSPI_ADDRLEN_3BYTES)
			/* default mode, does not need flex_cmd */
			flex_mode = 0;
		break;
	case SPI_NBITS_DUAL:
		bpc = 0x00000001;
		if (hp) {
			bpc |= 0x00010100; /* address and mode are 2-bit */
			bpp = BSPI_BPP_MODE_SELECT_MASK;
		}
		break;
	case SPI_NBITS_QUAD:
		bpc = 0x00000002;
		if (hp) {
			bpc |= 0x00020200; /* address and mode are 4-bit */
			bpp |= BSPI_BPP_MODE_SELECT_MASK;
		}
		break;
	default:
		return -EINVAL;
	}

	bcm_qspi_bspi_set_xfer_params(qspi, command, bpp, bpc, flex_mode);

	return 0;
}

static int bcm_qspi_bspi_set_override(struct bcm_qspi *qspi,
				      const struct spi_mem_op *op, int hp)
{
	int width = op->data.buswidth ? op->data.buswidth : SPI_NBITS_SINGLE;
	int addrlen = op->addr.nbytes;
	u32 data = bcm_qspi_read(qspi, BSPI, BSPI_STRAP_OVERRIDE_CTRL);

	dev_dbg(&qspi->pdev->dev, "set override mode w %x addrlen %x hp %d\n",
		width, addrlen, hp);

	switch (width) {
	case SPI_NBITS_SINGLE:
		/* clear quad/dual mode */
		data &= ~(BSPI_STRAP_OVERRIDE_CTRL_DATA_QUAD |
			  BSPI_STRAP_OVERRIDE_CTRL_DATA_DUAL);
		break;
	case SPI_NBITS_QUAD:
		/* clear dual mode and set quad mode */
		data &= ~BSPI_STRAP_OVERRIDE_CTRL_DATA_DUAL;
		data |= BSPI_STRAP_OVERRIDE_CTRL_DATA_QUAD;
		break;
	case SPI_NBITS_DUAL:
		/* clear quad mode set dual mode */
		data &= ~BSPI_STRAP_OVERRIDE_CTRL_DATA_QUAD;
		data |= BSPI_STRAP_OVERRIDE_CTRL_DATA_DUAL;
		break;
	default:
		return -EINVAL;
	}

	if (addrlen == BSPI_ADDRLEN_4BYTES)
		/* set 4byte mode*/
		data |= BSPI_STRAP_OVERRIDE_CTRL_ADDR_4BYTE;
	else
		/* clear 4 byte mode */
		data &= ~BSPI_STRAP_OVERRIDE_CTRL_ADDR_4BYTE;

	/* set the override mode */
	data |=	BSPI_STRAP_OVERRIDE_CTRL_OVERRIDE;
	bcm_qspi_write(qspi, BSPI, BSPI_STRAP_OVERRIDE_CTRL, data);
	bcm_qspi_bspi_set_xfer_params(qspi, op->cmd.opcode, 0, 0, 0);

	return 0;
}

static int bcm_qspi_bspi_set_mode(struct bcm_qspi *qspi,
				  const struct spi_mem_op *op, int hp)
{
	int error = 0;
	int width = op->data.buswidth ? op->data.buswidth : SPI_NBITS_SINGLE;
	int addrlen = op->addr.nbytes;

	/* default mode */
	qspi->xfer_mode.flex_mode = true;

	if (!bcm_qspi_bspi_ver_three(qspi)) {
		u32 val, mask;

		val = bcm_qspi_read(qspi, BSPI, BSPI_STRAP_OVERRIDE_CTRL);
		mask = BSPI_STRAP_OVERRIDE_CTRL_OVERRIDE;
		if (val & mask || qspi->s3_strap_override_ctrl & mask) {
			qspi->xfer_mode.flex_mode = false;
			bcm_qspi_write(qspi, BSPI, BSPI_FLEX_MODE_ENABLE, 0);
			error = bcm_qspi_bspi_set_override(qspi, op, hp);
		}
	}

	if (qspi->xfer_mode.flex_mode)
		error = bcm_qspi_bspi_set_flex_mode(qspi, op, hp);

	if (error) {
		dev_warn(&qspi->pdev->dev,
			 "INVALID COMBINATION: width=%d addrlen=%d hp=%d\n",
			 width, addrlen, hp);
	} else if (qspi->xfer_mode.width != width ||
		   qspi->xfer_mode.addrlen != addrlen ||
		   qspi->xfer_mode.hp != hp) {
		qspi->xfer_mode.width = width;
		qspi->xfer_mode.addrlen = addrlen;
		qspi->xfer_mode.hp = hp;
		dev_dbg(&qspi->pdev->dev,
			"cs:%d %d-lane output, %d-byte address%s\n",
			qspi->curr_cs,
			qspi->xfer_mode.width,
			qspi->xfer_mode.addrlen,
			qspi->xfer_mode.hp != -1 ? ", hp mode" : "");
	}

	return error;
}

static void bcm_qspi_enable_bspi(struct bcm_qspi *qspi)
{
	if (!has_bspi(qspi))
		return;

	qspi->bspi_enabled = 1;
	if ((bcm_qspi_read(qspi, BSPI, BSPI_MAST_N_BOOT_CTRL) & 1) == 0)
		return;

	bcm_qspi_bspi_flush_prefetch_buffers(qspi);
	udelay(1);
	bcm_qspi_write(qspi, BSPI, BSPI_MAST_N_BOOT_CTRL, 0);
	udelay(1);
}

static void bcm_qspi_disable_bspi(struct bcm_qspi *qspi)
{
	if (!has_bspi(qspi))
		return;

	qspi->bspi_enabled = 0;
	if ((bcm_qspi_read(qspi, BSPI, BSPI_MAST_N_BOOT_CTRL) & 1))
		return;

	bcm_qspi_bspi_busy_poll(qspi);
	bcm_qspi_write(qspi, BSPI, BSPI_MAST_N_BOOT_CTRL, 1);
	udelay(1);
}

static void bcm_qspi_chip_select(struct bcm_qspi *qspi, int cs)
{
	u32 rd = 0;
	u32 wr = 0;

	if (qspi->base[CHIP_SELECT]) {
		rd = bcm_qspi_read(qspi, CHIP_SELECT, 0);
		wr = (rd & ~0xff) | (1 << cs);
		if (rd == wr)
			return;
		bcm_qspi_write(qspi, CHIP_SELECT, 0, wr);
		usleep_range(10, 20);
	}

	dev_dbg(&qspi->pdev->dev, "using cs:%d\n", cs);
	qspi->curr_cs = cs;
}

/* MSPI helpers */
static void bcm_qspi_hw_set_parms(struct bcm_qspi *qspi,
				  const struct bcm_qspi_parms *xp)
{
	u32 spcr, spbr = 0;

	if (xp->speed_hz)
		spbr = qspi->base_clk / (2 * xp->speed_hz);

	spcr = clamp_val(spbr, QSPI_SPBR_MIN, QSPI_SPBR_MAX);
	bcm_qspi_write(qspi, MSPI, MSPI_SPCR0_LSB, spcr);

	spcr = MSPI_MASTER_BIT;
	/* for 16 bit the data should be zero */
	if (xp->bits_per_word != 16)
		spcr |= xp->bits_per_word << 2;
	spcr |= xp->mode & 3;
	bcm_qspi_write(qspi, MSPI, MSPI_SPCR0_MSB, spcr);

	qspi->last_parms = *xp;
}

static void bcm_qspi_update_parms(struct bcm_qspi *qspi,
				  struct spi_device *spi,
				  struct spi_transfer *trans)
{
	struct bcm_qspi_parms xp;

	xp.speed_hz = trans->speed_hz;
	xp.bits_per_word = trans->bits_per_word;
	xp.mode = spi->mode;

	bcm_qspi_hw_set_parms(qspi, &xp);
}

static int bcm_qspi_setup(struct spi_device *spi)
{
	struct bcm_qspi_parms *xp;

	if (spi->bits_per_word > 16)
		return -EINVAL;

	xp = spi_get_ctldata(spi);
	if (!xp) {
		xp = kzalloc(sizeof(*xp), GFP_KERNEL);
		if (!xp)
			return -ENOMEM;
		spi_set_ctldata(spi, xp);
	}
	xp->speed_hz = spi->max_speed_hz;
	xp->mode = spi->mode;

	if (spi->bits_per_word)
		xp->bits_per_word = spi->bits_per_word;
	else
		xp->bits_per_word = 8;

	return 0;
}

static bool bcm_qspi_mspi_transfer_is_last(struct bcm_qspi *qspi,
					   struct qspi_trans *qt)
{
	if (qt->mspi_last_trans &&
	    spi_transfer_is_last(qspi->master, qt->trans))
		return true;
	else
		return false;
}

static int update_qspi_trans_byte_count(struct bcm_qspi *qspi,
					struct qspi_trans *qt, int flags)
{
	int ret = TRANS_STATUS_BREAK_NONE;

	/* count the last transferred bytes */
	if (qt->trans->bits_per_word <= 8)
		qt->byte++;
	else
		qt->byte += 2;

	if (qt->byte >= qt->trans->len) {
		/* we're at the end of the spi_transfer */
		/* in TX mode, need to pause for a delay or CS change */
		if (qt->trans->delay_usecs &&
		    (flags & TRANS_STATUS_BREAK_DELAY))
			ret |= TRANS_STATUS_BREAK_DELAY;
		if (qt->trans->cs_change &&
		    (flags & TRANS_STATUS_BREAK_CS_CHANGE))
			ret |= TRANS_STATUS_BREAK_CS_CHANGE;
		if (ret)
			goto done;

		dev_dbg(&qspi->pdev->dev, "advance msg exit\n");
		if (bcm_qspi_mspi_transfer_is_last(qspi, qt))
			ret = TRANS_STATUS_BREAK_EOM;
		else
			ret = TRANS_STATUS_BREAK_NO_BYTES;

		qt->trans = NULL;
	}

done:
	dev_dbg(&qspi->pdev->dev, "trans %p len %d byte %d ret %x\n",
		qt->trans, qt->trans ? qt->trans->len : 0, qt->byte, ret);
	return ret;
}

static inline u8 read_rxram_slot_u8(struct bcm_qspi *qspi, int slot)
{
	u32 slot_offset = MSPI_RXRAM + (slot << 3) + 0x4;

	/* mask out reserved bits */
	return bcm_qspi_read(qspi, MSPI, slot_offset) & 0xff;
}

static inline u16 read_rxram_slot_u16(struct bcm_qspi *qspi, int slot)
{
	u32 reg_offset = MSPI_RXRAM;
	u32 lsb_offset = reg_offset + (slot << 3) + 0x4;
	u32 msb_offset = reg_offset + (slot << 3);

	return (bcm_qspi_read(qspi, MSPI, lsb_offset) & 0xff) |
		((bcm_qspi_read(qspi, MSPI, msb_offset) & 0xff) << 8);
}

static void read_from_hw(struct bcm_qspi *qspi, int slots)
{
	struct qspi_trans tp;
	int slot;

	bcm_qspi_disable_bspi(qspi);

	if (slots > MSPI_NUM_CDRAM) {
		/* should never happen */
		dev_err(&qspi->pdev->dev, "%s: too many slots!\n", __func__);
		return;
	}

	tp = qspi->trans_pos;

	for (slot = 0; slot < slots; slot++) {
		if (tp.trans->bits_per_word <= 8) {
			u8 *buf = tp.trans->rx_buf;

			if (buf)
				buf[tp.byte] = read_rxram_slot_u8(qspi, slot);
			dev_dbg(&qspi->pdev->dev, "RD %02x\n",
				buf ? buf[tp.byte] : 0xff);
		} else {
			u16 *buf = tp.trans->rx_buf;

			if (buf)
				buf[tp.byte / 2] = read_rxram_slot_u16(qspi,
								      slot);
			dev_dbg(&qspi->pdev->dev, "RD %04x\n",
				buf ? buf[tp.byte] : 0xffff);
		}

		update_qspi_trans_byte_count(qspi, &tp,
					     TRANS_STATUS_BREAK_NONE);
	}

	qspi->trans_pos = tp;
}

static inline void write_txram_slot_u8(struct bcm_qspi *qspi, int slot,
				       u8 val)
{
	u32 reg_offset = MSPI_TXRAM + (slot << 3);

	/* mask out reserved bits */
	bcm_qspi_write(qspi, MSPI, reg_offset, val);
}

static inline void write_txram_slot_u16(struct bcm_qspi *qspi, int slot,
					u16 val)
{
	u32 reg_offset = MSPI_TXRAM;
	u32 msb_offset = reg_offset + (slot << 3);
	u32 lsb_offset = reg_offset + (slot << 3) + 0x4;

	bcm_qspi_write(qspi, MSPI, msb_offset, (val >> 8));
	bcm_qspi_write(qspi, MSPI, lsb_offset, (val & 0xff));
}

static inline u32 read_cdram_slot(struct bcm_qspi *qspi, int slot)
{
	return bcm_qspi_read(qspi, MSPI, MSPI_CDRAM + (slot << 2));
}

static inline void write_cdram_slot(struct bcm_qspi *qspi, int slot, u32 val)
{
	bcm_qspi_write(qspi, MSPI, (MSPI_CDRAM + (slot << 2)), val);
}

/* Return number of slots written */
static int write_to_hw(struct bcm_qspi *qspi, struct spi_device *spi)
{
	struct qspi_trans tp;
	int slot = 0, tstatus = 0;
	u32 mspi_cdram = 0;

	bcm_qspi_disable_bspi(qspi);
	tp = qspi->trans_pos;
	bcm_qspi_update_parms(qspi, spi, tp.trans);

	/* Run until end of transfer or reached the max data */
	while (!tstatus && slot < MSPI_NUM_CDRAM) {
		if (tp.trans->bits_per_word <= 8) {
			const u8 *buf = tp.trans->tx_buf;
			u8 val = buf ? buf[tp.byte] : 0xff;

			write_txram_slot_u8(qspi, slot, val);
			dev_dbg(&qspi->pdev->dev, "WR %02x\n", val);
		} else {
			const u16 *buf = tp.trans->tx_buf;
			u16 val = buf ? buf[tp.byte / 2] : 0xffff;

			write_txram_slot_u16(qspi, slot, val);
			dev_dbg(&qspi->pdev->dev, "WR %04x\n", val);
		}
		mspi_cdram = MSPI_CDRAM_CONT_BIT;

		if (has_bspi(qspi))
			mspi_cdram &= ~1;
		else
			mspi_cdram |= (~(1 << spi->chip_select) &
				       MSPI_CDRAM_PCS);

		mspi_cdram |= ((tp.trans->bits_per_word <= 8) ? 0 :
				MSPI_CDRAM_BITSE_BIT);

		write_cdram_slot(qspi, slot, mspi_cdram);

		tstatus = update_qspi_trans_byte_count(qspi, &tp,
						       TRANS_STATUS_BREAK_TX);
		slot++;
	}

	if (!slot) {
		dev_err(&qspi->pdev->dev, "%s: no data to send?", __func__);
		goto done;
	}

	dev_dbg(&qspi->pdev->dev, "submitting %d slots\n", slot);
	bcm_qspi_write(qspi, MSPI, MSPI_NEWQP, 0);
	bcm_qspi_write(qspi, MSPI, MSPI_ENDQP, slot - 1);

	if (tstatus & TRANS_STATUS_BREAK_DESELECT) {
		mspi_cdram = read_cdram_slot(qspi, slot - 1) &
			~MSPI_CDRAM_CONT_BIT;
		write_cdram_slot(qspi, slot - 1, mspi_cdram);
	}

	if (has_bspi(qspi))
		bcm_qspi_write(qspi, MSPI, MSPI_WRITE_LOCK, 1);

	/* Must flush previous writes before starting MSPI operation */
	mb();
	/* Set cont | spe | spifie */
	bcm_qspi_write(qspi, MSPI, MSPI_SPCR2, 0xe0);

done:
	return slot;
}

static int bcm_qspi_bspi_exec_mem_op(struct spi_device *spi,
				     const struct spi_mem_op *op)
{
	struct bcm_qspi *qspi = spi_master_get_devdata(spi->master);
	u32 addr = 0, len, rdlen, len_words, from = 0;
	int ret = 0;
	unsigned long timeo = msecs_to_jiffies(100);
	struct bcm_qspi_soc_intc *soc_intc = qspi->soc_intc;

	if (bcm_qspi_bspi_ver_three(qspi))
		if (op->addr.nbytes == BSPI_ADDRLEN_4BYTES)
			return -EIO;

	from = op->addr.val;
	if (!spi->cs_gpiod)
		bcm_qspi_chip_select(qspi, spi->chip_select);
	bcm_qspi_write(qspi, MSPI, MSPI_WRITE_LOCK, 0);

	/*
	 * when using flex mode we need to send
	 * the upper address byte to bspi
	 */
	if (bcm_qspi_bspi_ver_three(qspi) == false) {
		addr = from & 0xff000000;
		bcm_qspi_write(qspi, BSPI,
			       BSPI_BSPI_FLASH_UPPER_ADDR_BYTE, addr);
	}

	if (!qspi->xfer_mode.flex_mode)
		addr = from;
	else
		addr = from & 0x00ffffff;

	if (bcm_qspi_bspi_ver_three(qspi) == true)
		addr = (addr + 0xc00000) & 0xffffff;

	/*
	 * read into the entire buffer by breaking the reads
	 * into RAF buffer read lengths
	 */
	len = op->data.nbytes;
	qspi->bspi_rf_op_idx = 0;

	do {
		if (len > BSPI_READ_LENGTH)
			rdlen = BSPI_READ_LENGTH;
		else
			rdlen = len;

		reinit_completion(&qspi->bspi_done);
		bcm_qspi_enable_bspi(qspi);
		len_words = (rdlen + 3) >> 2;
		qspi->bspi_rf_op = op;
		qspi->bspi_rf_op_status = 0;
		qspi->bspi_rf_op_len = rdlen;
		dev_dbg(&qspi->pdev->dev,
			"bspi xfr addr 0x%x len 0x%x", addr, rdlen);
		bcm_qspi_write(qspi, BSPI, BSPI_RAF_START_ADDR, addr);
		bcm_qspi_write(qspi, BSPI, BSPI_RAF_NUM_WORDS, len_words);
		bcm_qspi_write(qspi, BSPI, BSPI_RAF_WATERMARK, 0);
		if (qspi->soc_intc) {
			/*
			 * clear soc MSPI and BSPI interrupts and enable
			 * BSPI interrupts.
			 */
			soc_intc->bcm_qspi_int_ack(soc_intc, MSPI_BSPI_DONE);
			soc_intc->bcm_qspi_int_set(soc_intc, BSPI_DONE, true);
		}

		/* Must flush previous writes before starting BSPI operation */
		mb();
		bcm_qspi_bspi_lr_start(qspi);
		if (!wait_for_completion_timeout(&qspi->bspi_done, timeo)) {
			dev_err(&qspi->pdev->dev, "timeout waiting for BSPI\n");
			ret = -ETIMEDOUT;
			break;
		}

		/* set msg return length */
		addr += rdlen;
		len -= rdlen;
	} while (len);

	return ret;
}

static int bcm_qspi_transfer_one(struct spi_master *master,
				 struct spi_device *spi,
				 struct spi_transfer *trans)
{
	struct bcm_qspi *qspi = spi_master_get_devdata(master);
	int slots;
	unsigned long timeo = msecs_to_jiffies(100);

	if (!spi->cs_gpiod)
		bcm_qspi_chip_select(qspi, spi->chip_select);
	qspi->trans_pos.trans = trans;
	qspi->trans_pos.byte = 0;

	while (qspi->trans_pos.byte < trans->len) {
		reinit_completion(&qspi->mspi_done);

		slots = write_to_hw(qspi, spi);
		if (!wait_for_completion_timeout(&qspi->mspi_done, timeo)) {
			dev_err(&qspi->pdev->dev, "timeout waiting for MSPI\n");
			return -ETIMEDOUT;
		}

		read_from_hw(qspi, slots);
	}
	bcm_qspi_enable_bspi(qspi);

	return 0;
}

static int bcm_qspi_mspi_exec_mem_op(struct spi_device *spi,
				     const struct spi_mem_op *op)
{
	struct spi_master *master = spi->master;
	struct bcm_qspi *qspi = spi_master_get_devdata(master);
	struct spi_transfer t[2];
	u8 cmd[6] = { };
	int ret, i;

	memset(cmd, 0, sizeof(cmd));
	memset(t, 0, sizeof(t));

	/* tx */
	/* opcode is in cmd[0] */
	cmd[0] = op->cmd.opcode;
	for (i = 0; i < op->addr.nbytes; i++)
		cmd[1 + i] = op->addr.val >> (8 * (op->addr.nbytes - i - 1));

	t[0].tx_buf = cmd;
	t[0].len = op->addr.nbytes + op->dummy.nbytes + 1;
	t[0].bits_per_word = spi->bits_per_word;
	t[0].tx_nbits = op->cmd.buswidth;
	/* lets mspi know that this is not last transfer */
	qspi->trans_pos.mspi_last_trans = false;
	ret = bcm_qspi_transfer_one(master, spi, &t[0]);

	/* rx */
	qspi->trans_pos.mspi_last_trans = true;
	if (!ret) {
		/* rx */
		t[1].rx_buf = op->data.buf.in;
		t[1].len = op->data.nbytes;
		t[1].rx_nbits =  op->data.buswidth;
		t[1].bits_per_word = spi->bits_per_word;
		ret = bcm_qspi_transfer_one(master, spi, &t[1]);
	}

	return ret;
}

static int bcm_qspi_exec_mem_op(struct spi_mem *mem,
				const struct spi_mem_op *op)
{
	struct spi_device *spi = mem->spi;
	struct bcm_qspi *qspi = spi_master_get_devdata(spi->master);
	int ret = 0;
	bool mspi_read = false;
	u32 addr = 0, len;
	u_char *buf;

	if (!op->data.nbytes || !op->addr.nbytes || op->addr.nbytes > 4 ||
	    op->data.dir != SPI_MEM_DATA_IN)
		return -ENOTSUPP;

	buf = op->data.buf.in;
	addr = op->addr.val;
	len = op->data.nbytes;

	if (bcm_qspi_bspi_ver_three(qspi) == true) {
		/*
		 * The address coming into this function is a raw flash offset.
		 * But for BSPI <= V3, we need to convert it to a remapped BSPI
		 * address. If it crosses a 4MB boundary, just revert back to
		 * using MSPI.
		 */
		addr = (addr + 0xc00000) & 0xffffff;

		if ((~ADDR_4MB_MASK & addr) ^
		    (~ADDR_4MB_MASK & (addr + len - 1)))
			mspi_read = true;
	}

	/* non-aligned and very short transfers are handled by MSPI */
	if (!IS_ALIGNED((uintptr_t)addr, 4) || !IS_ALIGNED((uintptr_t)buf, 4) ||
	    len < 4)
		mspi_read = true;

	if (mspi_read)
		return bcm_qspi_mspi_exec_mem_op(spi, op);

	ret = bcm_qspi_bspi_set_mode(qspi, op, 0);

	if (!ret)
		ret = bcm_qspi_bspi_exec_mem_op(spi, op);

	return ret;
}

static void bcm_qspi_cleanup(struct spi_device *spi)
{
	struct bcm_qspi_parms *xp = spi_get_ctldata(spi);

	kfree(xp);
}

static irqreturn_t bcm_qspi_mspi_l2_isr(int irq, void *dev_id)
{
	struct bcm_qspi_dev_id *qspi_dev_id = dev_id;
	struct bcm_qspi *qspi = qspi_dev_id->dev;
	u32 status = bcm_qspi_read(qspi, MSPI, MSPI_MSPI_STATUS);

	if (status & MSPI_MSPI_STATUS_SPIF) {
		struct bcm_qspi_soc_intc *soc_intc = qspi->soc_intc;
		/* clear interrupt */
		status &= ~MSPI_MSPI_STATUS_SPIF;
		bcm_qspi_write(qspi, MSPI, MSPI_MSPI_STATUS, status);
		if (qspi->soc_intc)
			soc_intc->bcm_qspi_int_ack(soc_intc, MSPI_DONE);
		complete(&qspi->mspi_done);
		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

static irqreturn_t bcm_qspi_bspi_lr_l2_isr(int irq, void *dev_id)
{
	struct bcm_qspi_dev_id *qspi_dev_id = dev_id;
	struct bcm_qspi *qspi = qspi_dev_id->dev;
	struct bcm_qspi_soc_intc *soc_intc = qspi->soc_intc;
	u32 status = qspi_dev_id->irqp->mask;

	if (qspi->bspi_enabled && qspi->bspi_rf_op) {
		bcm_qspi_bspi_lr_data_read(qspi);
		if (qspi->bspi_rf_op_len == 0) {
			qspi->bspi_rf_op = NULL;
			if (qspi->soc_intc) {
				/* disable soc BSPI interrupt */
				soc_intc->bcm_qspi_int_set(soc_intc, BSPI_DONE,
							   false);
				/* indicate done */
				status = INTR_BSPI_LR_SESSION_DONE_MASK;
			}

			if (qspi->bspi_rf_op_status)
				bcm_qspi_bspi_lr_clear(qspi);
			else
				bcm_qspi_bspi_flush_prefetch_buffers(qspi);
		}

		if (qspi->soc_intc)
			/* clear soc BSPI interrupt */
			soc_intc->bcm_qspi_int_ack(soc_intc, BSPI_DONE);
	}

	status &= INTR_BSPI_LR_SESSION_DONE_MASK;
	if (qspi->bspi_enabled && status && qspi->bspi_rf_op_len == 0)
		complete(&qspi->bspi_done);

	return IRQ_HANDLED;
}

static irqreturn_t bcm_qspi_bspi_lr_err_l2_isr(int irq, void *dev_id)
{
	struct bcm_qspi_dev_id *qspi_dev_id = dev_id;
	struct bcm_qspi *qspi = qspi_dev_id->dev;
	struct bcm_qspi_soc_intc *soc_intc = qspi->soc_intc;

	dev_err(&qspi->pdev->dev, "BSPI INT error\n");
	qspi->bspi_rf_op_status = -EIO;
	if (qspi->soc_intc)
		/* clear soc interrupt */
		soc_intc->bcm_qspi_int_ack(soc_intc, BSPI_ERR);

	complete(&qspi->bspi_done);
	return IRQ_HANDLED;
}

static irqreturn_t bcm_qspi_l1_isr(int irq, void *dev_id)
{
	struct bcm_qspi_dev_id *qspi_dev_id = dev_id;
	struct bcm_qspi *qspi = qspi_dev_id->dev;
	struct bcm_qspi_soc_intc *soc_intc = qspi->soc_intc;
	irqreturn_t ret = IRQ_NONE;

	if (soc_intc) {
		u32 status = soc_intc->bcm_qspi_get_int_status(soc_intc);

		if (status & MSPI_DONE)
			ret = bcm_qspi_mspi_l2_isr(irq, dev_id);
		else if (status & BSPI_DONE)
			ret = bcm_qspi_bspi_lr_l2_isr(irq, dev_id);
		else if (status & BSPI_ERR)
			ret = bcm_qspi_bspi_lr_err_l2_isr(irq, dev_id);
	}

	return ret;
}

static const struct bcm_qspi_irq qspi_irq_tab[] = {
	{
		.irq_name = "spi_lr_fullness_reached",
		.irq_handler = bcm_qspi_bspi_lr_l2_isr,
		.mask = INTR_BSPI_LR_FULLNESS_REACHED_MASK,
	},
	{
		.irq_name = "spi_lr_session_aborted",
		.irq_handler = bcm_qspi_bspi_lr_err_l2_isr,
		.mask = INTR_BSPI_LR_SESSION_ABORTED_MASK,
	},
	{
		.irq_name = "spi_lr_impatient",
		.irq_handler = bcm_qspi_bspi_lr_err_l2_isr,
		.mask = INTR_BSPI_LR_IMPATIENT_MASK,
	},
	{
		.irq_name = "spi_lr_session_done",
		.irq_handler = bcm_qspi_bspi_lr_l2_isr,
		.mask = INTR_BSPI_LR_SESSION_DONE_MASK,
	},
#ifdef QSPI_INT_DEBUG
	/* this interrupt is for debug purposes only, dont request irq */
	{
		.irq_name = "spi_lr_overread",
		.irq_handler = bcm_qspi_bspi_lr_err_l2_isr,
		.mask = INTR_BSPI_LR_OVERREAD_MASK,
	},
#endif
	{
		.irq_name = "mspi_done",
		.irq_handler = bcm_qspi_mspi_l2_isr,
		.mask = INTR_MSPI_DONE_MASK,
	},
	{
		.irq_name = "mspi_halted",
		.irq_handler = bcm_qspi_mspi_l2_isr,
		.mask = INTR_MSPI_HALTED_MASK,
	},
	{
		/* single muxed L1 interrupt source */
		.irq_name = "spi_l1_intr",
		.irq_handler = bcm_qspi_l1_isr,
		.irq_source = MUXED_L1,
		.mask = QSPI_INTERRUPTS_ALL,
	},
};

static void bcm_qspi_bspi_init(struct bcm_qspi *qspi)
{
	u32 val = 0;

	val = bcm_qspi_read(qspi, BSPI, BSPI_REVISION_ID);
	qspi->bspi_maj_rev = (val >> 8) & 0xff;
	qspi->bspi_min_rev = val & 0xff;
	if (!(bcm_qspi_bspi_ver_three(qspi))) {
		/* Force mapping of BSPI address -> flash offset */
		bcm_qspi_write(qspi, BSPI, BSPI_BSPI_XOR_VALUE, 0);
		bcm_qspi_write(qspi, BSPI, BSPI_BSPI_XOR_ENABLE, 1);
	}
	qspi->bspi_enabled = 1;
	bcm_qspi_disable_bspi(qspi);
	bcm_qspi_write(qspi, BSPI, BSPI_B0_CTRL, 0);
	bcm_qspi_write(qspi, BSPI, BSPI_B1_CTRL, 0);
}

static void bcm_qspi_hw_init(struct bcm_qspi *qspi)
{
	struct bcm_qspi_parms parms;

	bcm_qspi_write(qspi, MSPI, MSPI_SPCR1_LSB, 0);
	bcm_qspi_write(qspi, MSPI, MSPI_SPCR1_MSB, 0);
	bcm_qspi_write(qspi, MSPI, MSPI_NEWQP, 0);
	bcm_qspi_write(qspi, MSPI, MSPI_ENDQP, 0);
	bcm_qspi_write(qspi, MSPI, MSPI_SPCR2, 0x20);

	parms.mode = SPI_MODE_3;
	parms.bits_per_word = 8;
	parms.speed_hz = qspi->max_speed_hz;
	bcm_qspi_hw_set_parms(qspi, &parms);

	if (has_bspi(qspi))
		bcm_qspi_bspi_init(qspi);
}

static void bcm_qspi_hw_uninit(struct bcm_qspi *qspi)
{
	bcm_qspi_write(qspi, MSPI, MSPI_SPCR2, 0);
	if (has_bspi(qspi))
		bcm_qspi_write(qspi, MSPI, MSPI_WRITE_LOCK, 0);

}

static const struct spi_controller_mem_ops bcm_qspi_mem_ops = {
	.exec_op = bcm_qspi_exec_mem_op,
};

static const struct of_device_id bcm_qspi_of_match[] = {
	{ .compatible = "brcm,spi-bcm-qspi" },
	{},
};
MODULE_DEVICE_TABLE(of, bcm_qspi_of_match);

int bcm_qspi_probe(struct platform_device *pdev,
		   struct bcm_qspi_soc_intc *soc_intc)
{
	struct device *dev = &pdev->dev;
	struct bcm_qspi *qspi;
	struct spi_master *master;
	struct resource *res;
	int irq, ret = 0, num_ints = 0;
	u32 val;
	const char *name = NULL;
	int num_irqs = ARRAY_SIZE(qspi_irq_tab);

	/* We only support device-tree instantiation */
	if (!dev->of_node)
		return -ENODEV;

	if (!of_match_node(bcm_qspi_of_match, dev->of_node))
		return -ENODEV;

	master = spi_alloc_master(dev, sizeof(struct bcm_qspi));
	if (!master) {
		dev_err(dev, "error allocating spi_master\n");
		return -ENOMEM;
	}

	qspi = spi_master_get_devdata(master);
	qspi->pdev = pdev;
	qspi->trans_pos.trans = NULL;
	qspi->trans_pos.byte = 0;
	qspi->trans_pos.mspi_last_trans = true;
	qspi->master = master;

	master->bus_num = -1;
	master->mode_bits = SPI_CPHA | SPI_CPOL | SPI_RX_DUAL | SPI_RX_QUAD;
	master->setup = bcm_qspi_setup;
	master->transfer_one = bcm_qspi_transfer_one;
	master->mem_ops = &bcm_qspi_mem_ops;
	master->cleanup = bcm_qspi_cleanup;
	master->dev.of_node = dev->of_node;
	master->num_chipselect = NUM_CHIPSELECT;
	master->use_gpio_descriptors = true;

	qspi->big_endian = of_device_is_big_endian(dev->of_node);

	if (!of_property_read_u32(dev->of_node, "num-cs", &val))
		master->num_chipselect = val;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "hif_mspi");
	if (!res)
		res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						   "mspi");

	if (res) {
		qspi->base[MSPI]  = devm_ioremap_resource(dev, res);
		if (IS_ERR(qspi->base[MSPI])) {
			ret = PTR_ERR(qspi->base[MSPI]);
			goto qspi_resource_err;
		}
	} else {
		goto qspi_resource_err;
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "bspi");
	if (res) {
		qspi->base[BSPI]  = devm_ioremap_resource(dev, res);
		if (IS_ERR(qspi->base[BSPI])) {
			ret = PTR_ERR(qspi->base[BSPI]);
			goto qspi_resource_err;
		}
		qspi->bspi_mode = true;
	} else {
		qspi->bspi_mode = false;
	}

	dev_info(dev, "using %smspi mode\n", qspi->bspi_mode ? "bspi-" : "");

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "cs_reg");
	if (res) {
		qspi->base[CHIP_SELECT]  = devm_ioremap_resource(dev, res);
		if (IS_ERR(qspi->base[CHIP_SELECT])) {
			ret = PTR_ERR(qspi->base[CHIP_SELECT]);
			goto qspi_resource_err;
		}
	}

	qspi->dev_ids = kcalloc(num_irqs, sizeof(struct bcm_qspi_dev_id),
				GFP_KERNEL);
	if (!qspi->dev_ids) {
		ret = -ENOMEM;
		goto qspi_resource_err;
	}

	for (val = 0; val < num_irqs; val++) {
		irq = -1;
		name = qspi_irq_tab[val].irq_name;
		if (qspi_irq_tab[val].irq_source == SINGLE_L2) {
			/* get the l2 interrupts */
			irq = platform_get_irq_byname(pdev, name);
		} else if (!num_ints && soc_intc) {
			/* all mspi, bspi intrs muxed to one L1 intr */
			irq = platform_get_irq(pdev, 0);
		}

		if (irq  >= 0) {
			ret = devm_request_irq(&pdev->dev, irq,
					       qspi_irq_tab[val].irq_handler, 0,
					       name,
					       &qspi->dev_ids[val]);
			if (ret < 0) {
				dev_err(&pdev->dev, "IRQ %s not found\n", name);
				goto qspi_probe_err;
			}

			qspi->dev_ids[val].dev = qspi;
			qspi->dev_ids[val].irqp = &qspi_irq_tab[val];
			num_ints++;
			dev_dbg(&pdev->dev, "registered IRQ %s %d\n",
				qspi_irq_tab[val].irq_name,
				irq);
		}
	}

	if (!num_ints) {
		dev_err(&pdev->dev, "no IRQs registered, cannot init driver\n");
		ret = -EINVAL;
		goto qspi_probe_err;
	}

	/*
	 * Some SoCs integrate spi controller (e.g., its interrupt bits)
	 * in specific ways
	 */
	if (soc_intc) {
		qspi->soc_intc = soc_intc;
		soc_intc->bcm_qspi_int_set(soc_intc, MSPI_DONE, true);
	} else {
		qspi->soc_intc = NULL;
	}

	qspi->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(qspi->clk)) {
		dev_warn(dev, "unable to get clock\n");
		ret = PTR_ERR(qspi->clk);
		goto qspi_probe_err;
	}

	ret = clk_prepare_enable(qspi->clk);
	if (ret) {
		dev_err(dev, "failed to prepare clock\n");
		goto qspi_probe_err;
	}

	qspi->base_clk = clk_get_rate(qspi->clk);
	qspi->max_speed_hz = qspi->base_clk / (QSPI_SPBR_MIN * 2);

	bcm_qspi_hw_init(qspi);
	init_completion(&qspi->mspi_done);
	init_completion(&qspi->bspi_done);
	qspi->curr_cs = -1;

	platform_set_drvdata(pdev, qspi);

	qspi->xfer_mode.width = -1;
	qspi->xfer_mode.addrlen = -1;
	qspi->xfer_mode.hp = -1;

	ret = devm_spi_register_master(&pdev->dev, master);
	if (ret < 0) {
		dev_err(dev, "can't register master\n");
		goto qspi_reg_err;
	}

	return 0;

qspi_reg_err:
	bcm_qspi_hw_uninit(qspi);
	clk_disable_unprepare(qspi->clk);
qspi_probe_err:
	kfree(qspi->dev_ids);
qspi_resource_err:
	spi_master_put(master);
	return ret;
}
/* probe function to be called by SoC specific platform driver probe */
EXPORT_SYMBOL_GPL(bcm_qspi_probe);

int bcm_qspi_remove(struct platform_device *pdev)
{
	struct bcm_qspi *qspi = platform_get_drvdata(pdev);

	bcm_qspi_hw_uninit(qspi);
	clk_disable_unprepare(qspi->clk);
	kfree(qspi->dev_ids);
	spi_unregister_master(qspi->master);

	return 0;
}
/* function to be called by SoC specific platform driver remove() */
EXPORT_SYMBOL_GPL(bcm_qspi_remove);

static int __maybe_unused bcm_qspi_suspend(struct device *dev)
{
	struct bcm_qspi *qspi = dev_get_drvdata(dev);

	/* store the override strap value */
	if (!bcm_qspi_bspi_ver_three(qspi))
		qspi->s3_strap_override_ctrl =
			bcm_qspi_read(qspi, BSPI, BSPI_STRAP_OVERRIDE_CTRL);

	spi_master_suspend(qspi->master);
	clk_disable(qspi->clk);
	bcm_qspi_hw_uninit(qspi);

	return 0;
};

static int __maybe_unused bcm_qspi_resume(struct device *dev)
{
	struct bcm_qspi *qspi = dev_get_drvdata(dev);
	int ret = 0;

	bcm_qspi_hw_init(qspi);
	bcm_qspi_chip_select(qspi, qspi->curr_cs);
	if (qspi->soc_intc)
		/* enable MSPI interrupt */
		qspi->soc_intc->bcm_qspi_int_set(qspi->soc_intc, MSPI_DONE,
						 true);

	ret = clk_enable(qspi->clk);
	if (!ret)
		spi_master_resume(qspi->master);

	return ret;
}

SIMPLE_DEV_PM_OPS(bcm_qspi_pm_ops, bcm_qspi_suspend, bcm_qspi_resume);

/* pm_ops to be called by SoC specific platform driver */
EXPORT_SYMBOL_GPL(bcm_qspi_pm_ops);

MODULE_AUTHOR("Kamal Dasu");
MODULE_DESCRIPTION("Broadcom QSPI driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRIVER_NAME);
