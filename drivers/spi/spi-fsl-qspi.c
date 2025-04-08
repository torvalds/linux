// SPDX-License-Identifier: GPL-2.0+

/*
 * Freescale QuadSPI driver.
 *
 * Copyright (C) 2013 Freescale Semiconductor, Inc.
 * Copyright (C) 2018 Bootlin
 * Copyright (C) 2018 exceet electronics GmbH
 * Copyright (C) 2018 Kontron Electronics GmbH
 *
 * Transition to SPI MEM interface:
 * Authors:
 *     Boris Brezillon <bbrezillon@kernel.org>
 *     Frieder Schrempf <frieder.schrempf@kontron.de>
 *     Yogesh Gaur <yogeshnarayan.gaur@nxp.com>
 *     Suresh Gupta <suresh.gupta@nxp.com>
 *
 * Based on the original fsl-quadspi.c SPI NOR driver:
 * Author: Freescale Semiconductor, Inc.
 *
 */

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_qos.h>
#include <linux/sizes.h>

#include <linux/spi/spi.h>
#include <linux/spi/spi-mem.h>

/*
 * The driver only uses one single LUT entry, that is updated on
 * each call of exec_op(). Index 0 is preset at boot with a basic
 * read operation, so let's use the last entry (15).
 */
#define	SEQID_LUT			15

/* Registers used by the driver */
#define QUADSPI_MCR			0x00
#define QUADSPI_MCR_RESERVED_MASK	GENMASK(19, 16)
#define QUADSPI_MCR_MDIS_MASK		BIT(14)
#define QUADSPI_MCR_CLR_TXF_MASK	BIT(11)
#define QUADSPI_MCR_CLR_RXF_MASK	BIT(10)
#define QUADSPI_MCR_DDR_EN_MASK		BIT(7)
#define QUADSPI_MCR_END_CFG_MASK	GENMASK(3, 2)
#define QUADSPI_MCR_SWRSTHD_MASK	BIT(1)
#define QUADSPI_MCR_SWRSTSD_MASK	BIT(0)

#define QUADSPI_IPCR			0x08
#define QUADSPI_IPCR_SEQID(x)		((x) << 24)

#define QUADSPI_FLSHCR			0x0c
#define QUADSPI_FLSHCR_TCSS_MASK	GENMASK(3, 0)
#define QUADSPI_FLSHCR_TCSH_MASK	GENMASK(11, 8)
#define QUADSPI_FLSHCR_TDH_MASK		GENMASK(17, 16)

#define QUADSPI_BUF0CR                  0x10
#define QUADSPI_BUF1CR                  0x14
#define QUADSPI_BUF2CR                  0x18
#define QUADSPI_BUFXCR_INVALID_MSTRID   0xe

#define QUADSPI_BUF3CR			0x1c
#define QUADSPI_BUF3CR_ALLMST_MASK	BIT(31)
#define QUADSPI_BUF3CR_ADATSZ(x)	((x) << 8)
#define QUADSPI_BUF3CR_ADATSZ_MASK	GENMASK(15, 8)

#define QUADSPI_BFGENCR			0x20
#define QUADSPI_BFGENCR_SEQID(x)	((x) << 12)

#define QUADSPI_BUF0IND			0x30
#define QUADSPI_BUF1IND			0x34
#define QUADSPI_BUF2IND			0x38
#define QUADSPI_SFAR			0x100

#define QUADSPI_SMPR			0x108
#define QUADSPI_SMPR_DDRSMP_MASK	GENMASK(18, 16)
#define QUADSPI_SMPR_FSDLY_MASK		BIT(6)
#define QUADSPI_SMPR_FSPHS_MASK		BIT(5)
#define QUADSPI_SMPR_HSENA_MASK		BIT(0)

#define QUADSPI_RBCT			0x110
#define QUADSPI_RBCT_WMRK_MASK		GENMASK(4, 0)
#define QUADSPI_RBCT_RXBRD_USEIPS	BIT(8)

#define QUADSPI_TBDR			0x154

#define QUADSPI_SR			0x15c
#define QUADSPI_SR_IP_ACC_MASK		BIT(1)
#define QUADSPI_SR_AHB_ACC_MASK		BIT(2)

#define QUADSPI_FR			0x160
#define QUADSPI_FR_TFF_MASK		BIT(0)

#define QUADSPI_RSER			0x164
#define QUADSPI_RSER_TFIE		BIT(0)

#define QUADSPI_SPTRCLR			0x16c
#define QUADSPI_SPTRCLR_IPPTRC		BIT(8)
#define QUADSPI_SPTRCLR_BFPTRC		BIT(0)

#define QUADSPI_SFA1AD			0x180
#define QUADSPI_SFA2AD			0x184
#define QUADSPI_SFB1AD			0x188
#define QUADSPI_SFB2AD			0x18c
#define QUADSPI_RBDR(x)			(0x200 + ((x) * 4))

#define QUADSPI_LUTKEY			0x300
#define QUADSPI_LUTKEY_VALUE		0x5AF05AF0

#define QUADSPI_LCKCR			0x304
#define QUADSPI_LCKER_LOCK		BIT(0)
#define QUADSPI_LCKER_UNLOCK		BIT(1)

#define QUADSPI_LUT_BASE		0x310
#define QUADSPI_LUT_OFFSET		(SEQID_LUT * 4 * 4)
#define QUADSPI_LUT_REG(idx) \
	(QUADSPI_LUT_BASE + QUADSPI_LUT_OFFSET + (idx) * 4)

/* Instruction set for the LUT register */
#define LUT_STOP		0
#define LUT_CMD			1
#define LUT_ADDR		2
#define LUT_DUMMY		3
#define LUT_MODE		4
#define LUT_MODE2		5
#define LUT_MODE4		6
#define LUT_FSL_READ		7
#define LUT_FSL_WRITE		8
#define LUT_JMP_ON_CS		9
#define LUT_ADDR_DDR		10
#define LUT_MODE_DDR		11
#define LUT_MODE2_DDR		12
#define LUT_MODE4_DDR		13
#define LUT_FSL_READ_DDR	14
#define LUT_FSL_WRITE_DDR	15
#define LUT_DATA_LEARN		16

/*
 * The PAD definitions for LUT register.
 *
 * The pad stands for the number of IO lines [0:3].
 * For example, the quad read needs four IO lines,
 * so you should use LUT_PAD(4).
 */
#define LUT_PAD(x) (fls(x) - 1)

/*
 * Macro for constructing the LUT entries with the following
 * register layout:
 *
 *  ---------------------------------------------------
 *  | INSTR1 | PAD1 | OPRND1 | INSTR0 | PAD0 | OPRND0 |
 *  ---------------------------------------------------
 */
#define LUT_DEF(idx, ins, pad, opr)					\
	((((ins) << 10) | ((pad) << 8) | (opr)) << (((idx) % 2) * 16))

/* Controller needs driver to swap endianness */
#define QUADSPI_QUIRK_SWAP_ENDIAN	BIT(0)

/* Controller needs 4x internal clock */
#define QUADSPI_QUIRK_4X_INT_CLK	BIT(1)

/*
 * TKT253890, the controller needs the driver to fill the txfifo with
 * 16 bytes at least to trigger a data transfer, even though the extra
 * data won't be transferred.
 */
#define QUADSPI_QUIRK_TKT253890		BIT(2)

/* TKT245618, the controller cannot wake up from wait mode */
#define QUADSPI_QUIRK_TKT245618		BIT(3)

/*
 * Controller adds QSPI_AMBA_BASE (base address of the mapped memory)
 * internally. No need to add it when setting SFXXAD and SFAR registers
 */
#define QUADSPI_QUIRK_BASE_INTERNAL	BIT(4)

/*
 * Controller uses TDH bits in register QUADSPI_FLSHCR.
 * They need to be set in accordance with the DDR/SDR mode.
 */
#define QUADSPI_QUIRK_USE_TDH_SETTING	BIT(5)

struct fsl_qspi_devtype_data {
	unsigned int rxfifo;
	unsigned int txfifo;
	int invalid_mstrid;
	unsigned int ahb_buf_size;
	unsigned int quirks;
	bool little_endian;
};

static const struct fsl_qspi_devtype_data vybrid_data = {
	.rxfifo = SZ_128,
	.txfifo = SZ_64,
	.invalid_mstrid = QUADSPI_BUFXCR_INVALID_MSTRID,
	.ahb_buf_size = SZ_1K,
	.quirks = QUADSPI_QUIRK_SWAP_ENDIAN,
	.little_endian = true,
};

static const struct fsl_qspi_devtype_data imx6sx_data = {
	.rxfifo = SZ_128,
	.txfifo = SZ_512,
	.invalid_mstrid = QUADSPI_BUFXCR_INVALID_MSTRID,
	.ahb_buf_size = SZ_1K,
	.quirks = QUADSPI_QUIRK_4X_INT_CLK | QUADSPI_QUIRK_TKT245618,
	.little_endian = true,
};

static const struct fsl_qspi_devtype_data imx7d_data = {
	.rxfifo = SZ_128,
	.txfifo = SZ_512,
	.invalid_mstrid = QUADSPI_BUFXCR_INVALID_MSTRID,
	.ahb_buf_size = SZ_1K,
	.quirks = QUADSPI_QUIRK_TKT253890 | QUADSPI_QUIRK_4X_INT_CLK |
		  QUADSPI_QUIRK_USE_TDH_SETTING,
	.little_endian = true,
};

static const struct fsl_qspi_devtype_data imx6ul_data = {
	.rxfifo = SZ_128,
	.txfifo = SZ_512,
	.invalid_mstrid = QUADSPI_BUFXCR_INVALID_MSTRID,
	.ahb_buf_size = SZ_1K,
	.quirks = QUADSPI_QUIRK_TKT253890 | QUADSPI_QUIRK_4X_INT_CLK |
		  QUADSPI_QUIRK_USE_TDH_SETTING,
	.little_endian = true,
};

static const struct fsl_qspi_devtype_data ls1021a_data = {
	.rxfifo = SZ_128,
	.txfifo = SZ_64,
	.invalid_mstrid = QUADSPI_BUFXCR_INVALID_MSTRID,
	.ahb_buf_size = SZ_1K,
	.quirks = 0,
	.little_endian = false,
};

static const struct fsl_qspi_devtype_data ls2080a_data = {
	.rxfifo = SZ_128,
	.txfifo = SZ_64,
	.ahb_buf_size = SZ_1K,
	.invalid_mstrid = 0x0,
	.quirks = QUADSPI_QUIRK_TKT253890 | QUADSPI_QUIRK_BASE_INTERNAL,
	.little_endian = true,
};

struct fsl_qspi {
	void __iomem *iobase;
	void __iomem *ahb_addr;
	u32 memmap_phy;
	struct clk *clk, *clk_en;
	struct device *dev;
	struct completion c;
	const struct fsl_qspi_devtype_data *devtype_data;
	struct mutex lock;
	struct pm_qos_request pm_qos_req;
	int selected;
};

static inline int needs_swap_endian(struct fsl_qspi *q)
{
	return q->devtype_data->quirks & QUADSPI_QUIRK_SWAP_ENDIAN;
}

static inline int needs_4x_clock(struct fsl_qspi *q)
{
	return q->devtype_data->quirks & QUADSPI_QUIRK_4X_INT_CLK;
}

static inline int needs_fill_txfifo(struct fsl_qspi *q)
{
	return q->devtype_data->quirks & QUADSPI_QUIRK_TKT253890;
}

static inline int needs_wakeup_wait_mode(struct fsl_qspi *q)
{
	return q->devtype_data->quirks & QUADSPI_QUIRK_TKT245618;
}

static inline int needs_amba_base_offset(struct fsl_qspi *q)
{
	return !(q->devtype_data->quirks & QUADSPI_QUIRK_BASE_INTERNAL);
}

static inline int needs_tdh_setting(struct fsl_qspi *q)
{
	return q->devtype_data->quirks & QUADSPI_QUIRK_USE_TDH_SETTING;
}

/*
 * An IC bug makes it necessary to rearrange the 32-bit data.
 * Later chips, such as IMX6SLX, have fixed this bug.
 */
static inline u32 fsl_qspi_endian_xchg(struct fsl_qspi *q, u32 a)
{
	return needs_swap_endian(q) ? __swab32(a) : a;
}

/*
 * R/W functions for big- or little-endian registers:
 * The QSPI controller's endianness is independent of
 * the CPU core's endianness. So far, although the CPU
 * core is little-endian the QSPI controller can use
 * big-endian or little-endian.
 */
static void qspi_writel(struct fsl_qspi *q, u32 val, void __iomem *addr)
{
	if (q->devtype_data->little_endian)
		iowrite32(val, addr);
	else
		iowrite32be(val, addr);
}

static u32 qspi_readl(struct fsl_qspi *q, void __iomem *addr)
{
	if (q->devtype_data->little_endian)
		return ioread32(addr);

	return ioread32be(addr);
}

static irqreturn_t fsl_qspi_irq_handler(int irq, void *dev_id)
{
	struct fsl_qspi *q = dev_id;
	u32 reg;

	/* clear interrupt */
	reg = qspi_readl(q, q->iobase + QUADSPI_FR);
	qspi_writel(q, reg, q->iobase + QUADSPI_FR);

	if (reg & QUADSPI_FR_TFF_MASK)
		complete(&q->c);

	dev_dbg(q->dev, "QUADSPI_FR : 0x%.8x:0x%.8x\n", 0, reg);
	return IRQ_HANDLED;
}

static int fsl_qspi_check_buswidth(struct fsl_qspi *q, u8 width)
{
	switch (width) {
	case 1:
	case 2:
	case 4:
		return 0;
	}

	return -ENOTSUPP;
}

static bool fsl_qspi_supports_op(struct spi_mem *mem,
				 const struct spi_mem_op *op)
{
	struct fsl_qspi *q = spi_controller_get_devdata(mem->spi->controller);
	int ret;

	ret = fsl_qspi_check_buswidth(q, op->cmd.buswidth);

	if (op->addr.nbytes)
		ret |= fsl_qspi_check_buswidth(q, op->addr.buswidth);

	if (op->dummy.nbytes)
		ret |= fsl_qspi_check_buswidth(q, op->dummy.buswidth);

	if (op->data.nbytes)
		ret |= fsl_qspi_check_buswidth(q, op->data.buswidth);

	if (ret)
		return false;

	/*
	 * The number of instructions needed for the op, needs
	 * to fit into a single LUT entry.
	 */
	if (op->addr.nbytes +
	   (op->dummy.nbytes ? 1:0) +
	   (op->data.nbytes ? 1:0) > 6)
		return false;

	/* Max 64 dummy clock cycles supported */
	if (op->dummy.nbytes &&
	    (op->dummy.nbytes * 8 / op->dummy.buswidth > 64))
		return false;

	/* Max data length, check controller limits and alignment */
	if (op->data.dir == SPI_MEM_DATA_IN &&
	    (op->data.nbytes > q->devtype_data->ahb_buf_size ||
	     (op->data.nbytes > q->devtype_data->rxfifo - 4 &&
	      !IS_ALIGNED(op->data.nbytes, 8))))
		return false;

	if (op->data.dir == SPI_MEM_DATA_OUT &&
	    op->data.nbytes > q->devtype_data->txfifo)
		return false;

	return spi_mem_default_supports_op(mem, op);
}

static void fsl_qspi_prepare_lut(struct fsl_qspi *q,
				 const struct spi_mem_op *op)
{
	void __iomem *base = q->iobase;
	u32 lutval[4] = {};
	int lutidx = 1, i;

	lutval[0] |= LUT_DEF(0, LUT_CMD, LUT_PAD(op->cmd.buswidth),
			     op->cmd.opcode);

	/*
	 * For some unknown reason, using LUT_ADDR doesn't work in some
	 * cases (at least with only one byte long addresses), so
	 * let's use LUT_MODE to write the address bytes one by one
	 */
	for (i = 0; i < op->addr.nbytes; i++) {
		u8 addrbyte = op->addr.val >> (8 * (op->addr.nbytes - i - 1));

		lutval[lutidx / 2] |= LUT_DEF(lutidx, LUT_MODE,
					      LUT_PAD(op->addr.buswidth),
					      addrbyte);
		lutidx++;
	}

	if (op->dummy.nbytes) {
		lutval[lutidx / 2] |= LUT_DEF(lutidx, LUT_DUMMY,
					      LUT_PAD(op->dummy.buswidth),
					      op->dummy.nbytes * 8 /
					      op->dummy.buswidth);
		lutidx++;
	}

	if (op->data.nbytes) {
		lutval[lutidx / 2] |= LUT_DEF(lutidx,
					      op->data.dir == SPI_MEM_DATA_IN ?
					      LUT_FSL_READ : LUT_FSL_WRITE,
					      LUT_PAD(op->data.buswidth),
					      0);
		lutidx++;
	}

	lutval[lutidx / 2] |= LUT_DEF(lutidx, LUT_STOP, 0, 0);

	/* unlock LUT */
	qspi_writel(q, QUADSPI_LUTKEY_VALUE, q->iobase + QUADSPI_LUTKEY);
	qspi_writel(q, QUADSPI_LCKER_UNLOCK, q->iobase + QUADSPI_LCKCR);

	/* fill LUT */
	for (i = 0; i < ARRAY_SIZE(lutval); i++)
		qspi_writel(q, lutval[i], base + QUADSPI_LUT_REG(i));

	/* lock LUT */
	qspi_writel(q, QUADSPI_LUTKEY_VALUE, q->iobase + QUADSPI_LUTKEY);
	qspi_writel(q, QUADSPI_LCKER_LOCK, q->iobase + QUADSPI_LCKCR);
}

static int fsl_qspi_clk_prep_enable(struct fsl_qspi *q)
{
	int ret;

	ret = clk_prepare_enable(q->clk_en);
	if (ret)
		return ret;

	ret = clk_prepare_enable(q->clk);
	if (ret) {
		clk_disable_unprepare(q->clk_en);
		return ret;
	}

	if (needs_wakeup_wait_mode(q))
		cpu_latency_qos_add_request(&q->pm_qos_req, 0);

	return 0;
}

static void fsl_qspi_clk_disable_unprep(struct fsl_qspi *q)
{
	if (needs_wakeup_wait_mode(q))
		cpu_latency_qos_remove_request(&q->pm_qos_req);

	clk_disable_unprepare(q->clk);
	clk_disable_unprepare(q->clk_en);
}

/*
 * If we have changed the content of the flash by writing or erasing, or if we
 * read from flash with a different offset into the page buffer, we need to
 * invalidate the AHB buffer. If we do not do so, we may read out the wrong
 * data. The spec tells us reset the AHB domain and Serial Flash domain at
 * the same time.
 */
static void fsl_qspi_invalidate(struct fsl_qspi *q)
{
	u32 reg;

	reg = qspi_readl(q, q->iobase + QUADSPI_MCR);
	reg |= QUADSPI_MCR_SWRSTHD_MASK | QUADSPI_MCR_SWRSTSD_MASK;
	qspi_writel(q, reg, q->iobase + QUADSPI_MCR);

	/*
	 * The minimum delay : 1 AHB + 2 SFCK clocks.
	 * Delay 1 us is enough.
	 */
	udelay(1);

	reg &= ~(QUADSPI_MCR_SWRSTHD_MASK | QUADSPI_MCR_SWRSTSD_MASK);
	qspi_writel(q, reg, q->iobase + QUADSPI_MCR);
}

static void fsl_qspi_select_mem(struct fsl_qspi *q, struct spi_device *spi,
				const struct spi_mem_op *op)
{
	unsigned long rate = op->max_freq;
	int ret;

	if (q->selected == spi_get_chipselect(spi, 0))
		return;

	if (needs_4x_clock(q))
		rate *= 4;

	fsl_qspi_clk_disable_unprep(q);

	ret = clk_set_rate(q->clk, rate);
	if (ret)
		return;

	ret = fsl_qspi_clk_prep_enable(q);
	if (ret)
		return;

	q->selected = spi_get_chipselect(spi, 0);

	fsl_qspi_invalidate(q);
}

static void fsl_qspi_read_ahb(struct fsl_qspi *q, const struct spi_mem_op *op)
{
	memcpy_fromio(op->data.buf.in,
		      q->ahb_addr + q->selected * q->devtype_data->ahb_buf_size,
		      op->data.nbytes);
}

static void fsl_qspi_fill_txfifo(struct fsl_qspi *q,
				 const struct spi_mem_op *op)
{
	void __iomem *base = q->iobase;
	int i;
	u32 val;

	for (i = 0; i < ALIGN_DOWN(op->data.nbytes, 4); i += 4) {
		memcpy(&val, op->data.buf.out + i, 4);
		val = fsl_qspi_endian_xchg(q, val);
		qspi_writel(q, val, base + QUADSPI_TBDR);
	}

	if (i < op->data.nbytes) {
		memcpy(&val, op->data.buf.out + i, op->data.nbytes - i);
		val = fsl_qspi_endian_xchg(q, val);
		qspi_writel(q, val, base + QUADSPI_TBDR);
	}

	if (needs_fill_txfifo(q)) {
		for (i = op->data.nbytes; i < 16; i += 4)
			qspi_writel(q, 0, base + QUADSPI_TBDR);
	}
}

static void fsl_qspi_read_rxfifo(struct fsl_qspi *q,
			  const struct spi_mem_op *op)
{
	void __iomem *base = q->iobase;
	int i;
	u8 *buf = op->data.buf.in;
	u32 val;

	for (i = 0; i < ALIGN_DOWN(op->data.nbytes, 4); i += 4) {
		val = qspi_readl(q, base + QUADSPI_RBDR(i / 4));
		val = fsl_qspi_endian_xchg(q, val);
		memcpy(buf + i, &val, 4);
	}

	if (i < op->data.nbytes) {
		val = qspi_readl(q, base + QUADSPI_RBDR(i / 4));
		val = fsl_qspi_endian_xchg(q, val);
		memcpy(buf + i, &val, op->data.nbytes - i);
	}
}

static int fsl_qspi_do_op(struct fsl_qspi *q, const struct spi_mem_op *op)
{
	void __iomem *base = q->iobase;
	int err = 0;

	init_completion(&q->c);

	/*
	 * Always start the sequence at the same index since we update
	 * the LUT at each exec_op() call. And also specify the DATA
	 * length, since it's has not been specified in the LUT.
	 */
	qspi_writel(q, op->data.nbytes | QUADSPI_IPCR_SEQID(SEQID_LUT),
		    base + QUADSPI_IPCR);

	/* Wait for the interrupt. */
	if (!wait_for_completion_timeout(&q->c, msecs_to_jiffies(1000)))
		err = -ETIMEDOUT;

	if (!err && op->data.nbytes && op->data.dir == SPI_MEM_DATA_IN)
		fsl_qspi_read_rxfifo(q, op);

	return err;
}

static int fsl_qspi_readl_poll_tout(struct fsl_qspi *q, void __iomem *base,
				    u32 mask, u32 delay_us, u32 timeout_us)
{
	u32 reg;

	if (!q->devtype_data->little_endian)
		mask = (u32)cpu_to_be32(mask);

	return readl_poll_timeout(base, reg, !(reg & mask), delay_us,
				  timeout_us);
}

static int fsl_qspi_exec_op(struct spi_mem *mem, const struct spi_mem_op *op)
{
	struct fsl_qspi *q = spi_controller_get_devdata(mem->spi->controller);
	void __iomem *base = q->iobase;
	u32 addr_offset = 0;
	int err = 0;
	int invalid_mstrid = q->devtype_data->invalid_mstrid;

	mutex_lock(&q->lock);

	/* wait for the controller being ready */
	fsl_qspi_readl_poll_tout(q, base + QUADSPI_SR, (QUADSPI_SR_IP_ACC_MASK |
				 QUADSPI_SR_AHB_ACC_MASK), 10, 1000);

	fsl_qspi_select_mem(q, mem->spi, op);

	if (needs_amba_base_offset(q))
		addr_offset = q->memmap_phy;

	qspi_writel(q,
		    q->selected * q->devtype_data->ahb_buf_size + addr_offset,
		    base + QUADSPI_SFAR);

	qspi_writel(q, qspi_readl(q, base + QUADSPI_MCR) |
		    QUADSPI_MCR_CLR_RXF_MASK | QUADSPI_MCR_CLR_TXF_MASK,
		    base + QUADSPI_MCR);

	qspi_writel(q, QUADSPI_SPTRCLR_BFPTRC | QUADSPI_SPTRCLR_IPPTRC,
		    base + QUADSPI_SPTRCLR);

	qspi_writel(q, invalid_mstrid, base + QUADSPI_BUF0CR);
	qspi_writel(q, invalid_mstrid, base + QUADSPI_BUF1CR);
	qspi_writel(q, invalid_mstrid, base + QUADSPI_BUF2CR);

	fsl_qspi_prepare_lut(q, op);

	/*
	 * If we have large chunks of data, we read them through the AHB bus
	 * by accessing the mapped memory. In all other cases we use
	 * IP commands to access the flash.
	 */
	if (op->data.nbytes > (q->devtype_data->rxfifo - 4) &&
	    op->data.dir == SPI_MEM_DATA_IN) {
		fsl_qspi_read_ahb(q, op);
	} else {
		qspi_writel(q, QUADSPI_RBCT_WMRK_MASK |
			    QUADSPI_RBCT_RXBRD_USEIPS, base + QUADSPI_RBCT);

		if (op->data.nbytes && op->data.dir == SPI_MEM_DATA_OUT)
			fsl_qspi_fill_txfifo(q, op);

		err = fsl_qspi_do_op(q, op);
	}

	/* Invalidate the data in the AHB buffer. */
	fsl_qspi_invalidate(q);

	mutex_unlock(&q->lock);

	return err;
}

static int fsl_qspi_adjust_op_size(struct spi_mem *mem, struct spi_mem_op *op)
{
	struct fsl_qspi *q = spi_controller_get_devdata(mem->spi->controller);

	if (op->data.dir == SPI_MEM_DATA_OUT) {
		if (op->data.nbytes > q->devtype_data->txfifo)
			op->data.nbytes = q->devtype_data->txfifo;
	} else {
		if (op->data.nbytes > q->devtype_data->ahb_buf_size)
			op->data.nbytes = q->devtype_data->ahb_buf_size;
		else if (op->data.nbytes > (q->devtype_data->rxfifo - 4))
			op->data.nbytes = ALIGN_DOWN(op->data.nbytes, 8);
	}

	return 0;
}

static int fsl_qspi_default_setup(struct fsl_qspi *q)
{
	void __iomem *base = q->iobase;
	u32 reg, addr_offset = 0;
	int ret;

	/* disable and unprepare clock to avoid glitch pass to controller */
	fsl_qspi_clk_disable_unprep(q);

	/* the default frequency, we will change it later if necessary. */
	ret = clk_set_rate(q->clk, 66000000);
	if (ret)
		return ret;

	ret = fsl_qspi_clk_prep_enable(q);
	if (ret)
		return ret;

	/* Reset the module */
	qspi_writel(q, QUADSPI_MCR_SWRSTSD_MASK | QUADSPI_MCR_SWRSTHD_MASK,
		    base + QUADSPI_MCR);
	udelay(1);

	/* Disable the module */
	qspi_writel(q, QUADSPI_MCR_MDIS_MASK | QUADSPI_MCR_RESERVED_MASK,
		    base + QUADSPI_MCR);

	/*
	 * Previous boot stages (BootROM, bootloader) might have used DDR
	 * mode and did not clear the TDH bits. As we currently use SDR mode
	 * only, clear the TDH bits if necessary.
	 */
	if (needs_tdh_setting(q))
		qspi_writel(q, qspi_readl(q, base + QUADSPI_FLSHCR) &
			    ~QUADSPI_FLSHCR_TDH_MASK,
			    base + QUADSPI_FLSHCR);

	reg = qspi_readl(q, base + QUADSPI_SMPR);
	qspi_writel(q, reg & ~(QUADSPI_SMPR_FSDLY_MASK
			| QUADSPI_SMPR_FSPHS_MASK
			| QUADSPI_SMPR_HSENA_MASK
			| QUADSPI_SMPR_DDRSMP_MASK), base + QUADSPI_SMPR);

	/* We only use the buffer3 for AHB read */
	qspi_writel(q, 0, base + QUADSPI_BUF0IND);
	qspi_writel(q, 0, base + QUADSPI_BUF1IND);
	qspi_writel(q, 0, base + QUADSPI_BUF2IND);

	qspi_writel(q, QUADSPI_BFGENCR_SEQID(SEQID_LUT),
		    q->iobase + QUADSPI_BFGENCR);
	qspi_writel(q, QUADSPI_RBCT_WMRK_MASK, base + QUADSPI_RBCT);
	qspi_writel(q, QUADSPI_BUF3CR_ALLMST_MASK |
		    QUADSPI_BUF3CR_ADATSZ(q->devtype_data->ahb_buf_size / 8),
		    base + QUADSPI_BUF3CR);

	if (needs_amba_base_offset(q))
		addr_offset = q->memmap_phy;

	/*
	 * In HW there can be a maximum of four chips on two buses with
	 * two chip selects on each bus. We use four chip selects in SW
	 * to differentiate between the four chips.
	 * We use ahb_buf_size for each chip and set SFA1AD, SFA2AD, SFB1AD,
	 * SFB2AD accordingly.
	 */
	qspi_writel(q, q->devtype_data->ahb_buf_size + addr_offset,
		    base + QUADSPI_SFA1AD);
	qspi_writel(q, q->devtype_data->ahb_buf_size * 2 + addr_offset,
		    base + QUADSPI_SFA2AD);
	qspi_writel(q, q->devtype_data->ahb_buf_size * 3 + addr_offset,
		    base + QUADSPI_SFB1AD);
	qspi_writel(q, q->devtype_data->ahb_buf_size * 4 + addr_offset,
		    base + QUADSPI_SFB2AD);

	q->selected = -1;

	/* Enable the module */
	qspi_writel(q, QUADSPI_MCR_RESERVED_MASK | QUADSPI_MCR_END_CFG_MASK,
		    base + QUADSPI_MCR);

	/* clear all interrupt status */
	qspi_writel(q, 0xffffffff, q->iobase + QUADSPI_FR);

	/* enable the interrupt */
	qspi_writel(q, QUADSPI_RSER_TFIE, q->iobase + QUADSPI_RSER);

	return 0;
}

static const char *fsl_qspi_get_name(struct spi_mem *mem)
{
	struct fsl_qspi *q = spi_controller_get_devdata(mem->spi->controller);
	struct device *dev = &mem->spi->dev;
	const char *name;

	/*
	 * In order to keep mtdparts compatible with the old MTD driver at
	 * mtd/spi-nor/fsl-quadspi.c, we set a custom name derived from the
	 * platform_device of the controller.
	 */
	if (of_get_available_child_count(q->dev->of_node) == 1)
		return dev_name(q->dev);

	name = devm_kasprintf(dev, GFP_KERNEL,
			      "%s-%d", dev_name(q->dev),
			      spi_get_chipselect(mem->spi, 0));

	if (!name) {
		dev_err(dev, "failed to get memory for custom flash name\n");
		return ERR_PTR(-ENOMEM);
	}

	return name;
}

static const struct spi_controller_mem_ops fsl_qspi_mem_ops = {
	.adjust_op_size = fsl_qspi_adjust_op_size,
	.supports_op = fsl_qspi_supports_op,
	.exec_op = fsl_qspi_exec_op,
	.get_name = fsl_qspi_get_name,
};

static const struct spi_controller_mem_caps fsl_qspi_mem_caps = {
	.per_op_freq = true,
};

static void fsl_qspi_cleanup(void *data)
{
	struct fsl_qspi *q = data;

	/* disable the hardware */
	qspi_writel(q, QUADSPI_MCR_MDIS_MASK, q->iobase + QUADSPI_MCR);
	qspi_writel(q, 0x0, q->iobase + QUADSPI_RSER);

	fsl_qspi_clk_disable_unprep(q);

	mutex_destroy(&q->lock);
}

static int fsl_qspi_probe(struct platform_device *pdev)
{
	struct spi_controller *ctlr;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct resource *res;
	struct fsl_qspi *q;
	int ret;

	ctlr = spi_alloc_host(&pdev->dev, sizeof(*q));
	if (!ctlr)
		return -ENOMEM;

	ctlr->mode_bits = SPI_RX_DUAL | SPI_RX_QUAD |
			  SPI_TX_DUAL | SPI_TX_QUAD;

	q = spi_controller_get_devdata(ctlr);
	q->dev = dev;
	q->devtype_data = of_device_get_match_data(dev);
	if (!q->devtype_data) {
		ret = -ENODEV;
		goto err_put_ctrl;
	}

	platform_set_drvdata(pdev, q);

	/* find the resources */
	q->iobase = devm_platform_ioremap_resource_byname(pdev, "QuadSPI");
	if (IS_ERR(q->iobase)) {
		ret = PTR_ERR(q->iobase);
		goto err_put_ctrl;
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
					"QuadSPI-memory");
	if (!res) {
		ret = -EINVAL;
		goto err_put_ctrl;
	}
	q->memmap_phy = res->start;
	/* Since there are 4 cs, map size required is 4 times ahb_buf_size */
	q->ahb_addr = devm_ioremap(dev, q->memmap_phy,
				   (q->devtype_data->ahb_buf_size * 4));
	if (!q->ahb_addr) {
		ret = -ENOMEM;
		goto err_put_ctrl;
	}

	/* find the clocks */
	q->clk_en = devm_clk_get(dev, "qspi_en");
	if (IS_ERR(q->clk_en)) {
		ret = PTR_ERR(q->clk_en);
		goto err_put_ctrl;
	}

	q->clk = devm_clk_get(dev, "qspi");
	if (IS_ERR(q->clk)) {
		ret = PTR_ERR(q->clk);
		goto err_put_ctrl;
	}

	ret = fsl_qspi_clk_prep_enable(q);
	if (ret) {
		dev_err(dev, "can not enable the clock\n");
		goto err_put_ctrl;
	}

	/* find the irq */
	ret = platform_get_irq(pdev, 0);
	if (ret < 0)
		goto err_disable_clk;

	ret = devm_request_irq(dev, ret,
			fsl_qspi_irq_handler, 0, pdev->name, q);
	if (ret) {
		dev_err(dev, "failed to request irq: %d\n", ret);
		goto err_disable_clk;
	}

	mutex_init(&q->lock);

	ctlr->bus_num = -1;
	ctlr->num_chipselect = 4;
	ctlr->mem_ops = &fsl_qspi_mem_ops;
	ctlr->mem_caps = &fsl_qspi_mem_caps;

	fsl_qspi_default_setup(q);

	ctlr->dev.of_node = np;

	ret = devm_add_action_or_reset(dev, fsl_qspi_cleanup, q);
	if (ret)
		goto err_destroy_mutex;

	ret = devm_spi_register_controller(dev, ctlr);
	if (ret)
		goto err_destroy_mutex;

	return 0;

err_destroy_mutex:
	mutex_destroy(&q->lock);

err_disable_clk:
	fsl_qspi_clk_disable_unprep(q);

err_put_ctrl:
	spi_controller_put(ctlr);

	dev_err(dev, "Freescale QuadSPI probe failed\n");
	return ret;
}

static int fsl_qspi_suspend(struct device *dev)
{
	return 0;
}

static int fsl_qspi_resume(struct device *dev)
{
	struct fsl_qspi *q = dev_get_drvdata(dev);

	fsl_qspi_default_setup(q);

	return 0;
}

static const struct of_device_id fsl_qspi_dt_ids[] = {
	{ .compatible = "fsl,vf610-qspi", .data = &vybrid_data, },
	{ .compatible = "fsl,imx6sx-qspi", .data = &imx6sx_data, },
	{ .compatible = "fsl,imx7d-qspi", .data = &imx7d_data, },
	{ .compatible = "fsl,imx6ul-qspi", .data = &imx6ul_data, },
	{ .compatible = "fsl,ls1021a-qspi", .data = &ls1021a_data, },
	{ .compatible = "fsl,ls2080a-qspi", .data = &ls2080a_data, },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, fsl_qspi_dt_ids);

static const struct dev_pm_ops fsl_qspi_pm_ops = {
	.suspend	= fsl_qspi_suspend,
	.resume		= fsl_qspi_resume,
};

static struct platform_driver fsl_qspi_driver = {
	.driver = {
		.name	= "fsl-quadspi",
		.of_match_table = fsl_qspi_dt_ids,
		.pm =   &fsl_qspi_pm_ops,
	},
	.probe          = fsl_qspi_probe,
};
module_platform_driver(fsl_qspi_driver);

MODULE_DESCRIPTION("Freescale QuadSPI Controller Driver");
MODULE_AUTHOR("Freescale Semiconductor Inc.");
MODULE_AUTHOR("Boris Brezillon <bbrezillon@kernel.org>");
MODULE_AUTHOR("Frieder Schrempf <frieder.schrempf@kontron.de>");
MODULE_AUTHOR("Yogesh Gaur <yogeshnarayan.gaur@nxp.com>");
MODULE_AUTHOR("Suresh Gupta <suresh.gupta@nxp.com>");
MODULE_LICENSE("GPL v2");
