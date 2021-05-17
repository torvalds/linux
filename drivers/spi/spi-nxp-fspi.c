// SPDX-License-Identifier: GPL-2.0+

/*
 * NXP FlexSPI(FSPI) controller driver.
 *
 * Copyright 2019-2020 NXP
 * Copyright 2020 Puresoftware Ltd.
 *
 * FlexSPI is a flexsible SPI host controller which supports two SPI
 * channels and up to 4 external devices. Each channel supports
 * Single/Dual/Quad/Octal mode data transfer (1/2/4/8 bidirectional
 * data lines).
 *
 * FlexSPI controller is driven by the LUT(Look-up Table) registers
 * LUT registers are a look-up-table for sequences of instructions.
 * A valid sequence consists of four LUT registers.
 * Maximum 32 LUT sequences can be programmed simultaneously.
 *
 * LUTs are being created at run-time based on the commands passed
 * from the spi-mem framework, thus using single LUT index.
 *
 * Software triggered Flash read/write access by IP Bus.
 *
 * Memory mapped read access by AHB Bus.
 *
 * Based on SPI MEM interface and spi-fsl-qspi.c driver.
 *
 * Author:
 *     Yogesh Narayan Gaur <yogeshnarayan.gaur@nxp.com>
 *     Boris Brezillon <bbrezillon@kernel.org>
 *     Frieder Schrempf <frieder.schrempf@kontron.de>
 */

#include <linux/acpi.h>
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
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_qos.h>
#include <linux/regmap.h>
#include <linux/sizes.h>
#include <linux/sys_soc.h>

#include <linux/mfd/syscon.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi-mem.h>

/*
 * The driver only uses one single LUT entry, that is updated on
 * each call of exec_op(). Index 0 is preset at boot with a basic
 * read operation, so let's use the last entry (31).
 */
#define	SEQID_LUT			31

/* Registers used by the driver */
#define FSPI_MCR0			0x00
#define FSPI_MCR0_AHB_TIMEOUT(x)	((x) << 24)
#define FSPI_MCR0_IP_TIMEOUT(x)		((x) << 16)
#define FSPI_MCR0_LEARN_EN		BIT(15)
#define FSPI_MCR0_SCRFRUN_EN		BIT(14)
#define FSPI_MCR0_OCTCOMB_EN		BIT(13)
#define FSPI_MCR0_DOZE_EN		BIT(12)
#define FSPI_MCR0_HSEN			BIT(11)
#define FSPI_MCR0_SERCLKDIV		BIT(8)
#define FSPI_MCR0_ATDF_EN		BIT(7)
#define FSPI_MCR0_ARDF_EN		BIT(6)
#define FSPI_MCR0_RXCLKSRC(x)		((x) << 4)
#define FSPI_MCR0_END_CFG(x)		((x) << 2)
#define FSPI_MCR0_MDIS			BIT(1)
#define FSPI_MCR0_SWRST			BIT(0)

#define FSPI_MCR1			0x04
#define FSPI_MCR1_SEQ_TIMEOUT(x)	((x) << 16)
#define FSPI_MCR1_AHB_TIMEOUT(x)	(x)

#define FSPI_MCR2			0x08
#define FSPI_MCR2_IDLE_WAIT(x)		((x) << 24)
#define FSPI_MCR2_SAMEDEVICEEN		BIT(15)
#define FSPI_MCR2_CLRLRPHS		BIT(14)
#define FSPI_MCR2_ABRDATSZ		BIT(8)
#define FSPI_MCR2_ABRLEARN		BIT(7)
#define FSPI_MCR2_ABR_READ		BIT(6)
#define FSPI_MCR2_ABRWRITE		BIT(5)
#define FSPI_MCR2_ABRDUMMY		BIT(4)
#define FSPI_MCR2_ABR_MODE		BIT(3)
#define FSPI_MCR2_ABRCADDR		BIT(2)
#define FSPI_MCR2_ABRRADDR		BIT(1)
#define FSPI_MCR2_ABR_CMD		BIT(0)

#define FSPI_AHBCR			0x0c
#define FSPI_AHBCR_RDADDROPT		BIT(6)
#define FSPI_AHBCR_PREF_EN		BIT(5)
#define FSPI_AHBCR_BUFF_EN		BIT(4)
#define FSPI_AHBCR_CACH_EN		BIT(3)
#define FSPI_AHBCR_CLRTXBUF		BIT(2)
#define FSPI_AHBCR_CLRRXBUF		BIT(1)
#define FSPI_AHBCR_PAR_EN		BIT(0)

#define FSPI_INTEN			0x10
#define FSPI_INTEN_SCLKSBWR		BIT(9)
#define FSPI_INTEN_SCLKSBRD		BIT(8)
#define FSPI_INTEN_DATALRNFL		BIT(7)
#define FSPI_INTEN_IPTXWE		BIT(6)
#define FSPI_INTEN_IPRXWA		BIT(5)
#define FSPI_INTEN_AHBCMDERR		BIT(4)
#define FSPI_INTEN_IPCMDERR		BIT(3)
#define FSPI_INTEN_AHBCMDGE		BIT(2)
#define FSPI_INTEN_IPCMDGE		BIT(1)
#define FSPI_INTEN_IPCMDDONE		BIT(0)

#define FSPI_INTR			0x14
#define FSPI_INTR_SCLKSBWR		BIT(9)
#define FSPI_INTR_SCLKSBRD		BIT(8)
#define FSPI_INTR_DATALRNFL		BIT(7)
#define FSPI_INTR_IPTXWE		BIT(6)
#define FSPI_INTR_IPRXWA		BIT(5)
#define FSPI_INTR_AHBCMDERR		BIT(4)
#define FSPI_INTR_IPCMDERR		BIT(3)
#define FSPI_INTR_AHBCMDGE		BIT(2)
#define FSPI_INTR_IPCMDGE		BIT(1)
#define FSPI_INTR_IPCMDDONE		BIT(0)

#define FSPI_LUTKEY			0x18
#define FSPI_LUTKEY_VALUE		0x5AF05AF0

#define FSPI_LCKCR			0x1C

#define FSPI_LCKER_LOCK			0x1
#define FSPI_LCKER_UNLOCK		0x2

#define FSPI_BUFXCR_INVALID_MSTRID	0xE
#define FSPI_AHBRX_BUF0CR0		0x20
#define FSPI_AHBRX_BUF1CR0		0x24
#define FSPI_AHBRX_BUF2CR0		0x28
#define FSPI_AHBRX_BUF3CR0		0x2C
#define FSPI_AHBRX_BUF4CR0		0x30
#define FSPI_AHBRX_BUF5CR0		0x34
#define FSPI_AHBRX_BUF6CR0		0x38
#define FSPI_AHBRX_BUF7CR0		0x3C
#define FSPI_AHBRXBUF0CR7_PREF		BIT(31)

#define FSPI_AHBRX_BUF0CR1		0x40
#define FSPI_AHBRX_BUF1CR1		0x44
#define FSPI_AHBRX_BUF2CR1		0x48
#define FSPI_AHBRX_BUF3CR1		0x4C
#define FSPI_AHBRX_BUF4CR1		0x50
#define FSPI_AHBRX_BUF5CR1		0x54
#define FSPI_AHBRX_BUF6CR1		0x58
#define FSPI_AHBRX_BUF7CR1		0x5C

#define FSPI_FLSHA1CR0			0x60
#define FSPI_FLSHA2CR0			0x64
#define FSPI_FLSHB1CR0			0x68
#define FSPI_FLSHB2CR0			0x6C
#define FSPI_FLSHXCR0_SZ_KB		10
#define FSPI_FLSHXCR0_SZ(x)		((x) >> FSPI_FLSHXCR0_SZ_KB)

#define FSPI_FLSHA1CR1			0x70
#define FSPI_FLSHA2CR1			0x74
#define FSPI_FLSHB1CR1			0x78
#define FSPI_FLSHB2CR1			0x7C
#define FSPI_FLSHXCR1_CSINTR(x)		((x) << 16)
#define FSPI_FLSHXCR1_CAS(x)		((x) << 11)
#define FSPI_FLSHXCR1_WA		BIT(10)
#define FSPI_FLSHXCR1_TCSH(x)		((x) << 5)
#define FSPI_FLSHXCR1_TCSS(x)		(x)

#define FSPI_FLSHA1CR2			0x80
#define FSPI_FLSHA2CR2			0x84
#define FSPI_FLSHB1CR2			0x88
#define FSPI_FLSHB2CR2			0x8C
#define FSPI_FLSHXCR2_CLRINSP		BIT(24)
#define FSPI_FLSHXCR2_AWRWAIT		BIT(16)
#define FSPI_FLSHXCR2_AWRSEQN_SHIFT	13
#define FSPI_FLSHXCR2_AWRSEQI_SHIFT	8
#define FSPI_FLSHXCR2_ARDSEQN_SHIFT	5
#define FSPI_FLSHXCR2_ARDSEQI_SHIFT	0

#define FSPI_IPCR0			0xA0

#define FSPI_IPCR1			0xA4
#define FSPI_IPCR1_IPAREN		BIT(31)
#define FSPI_IPCR1_SEQNUM_SHIFT		24
#define FSPI_IPCR1_SEQID_SHIFT		16
#define FSPI_IPCR1_IDATSZ(x)		(x)

#define FSPI_IPCMD			0xB0
#define FSPI_IPCMD_TRG			BIT(0)

#define FSPI_DLPR			0xB4

#define FSPI_IPRXFCR			0xB8
#define FSPI_IPRXFCR_CLR		BIT(0)
#define FSPI_IPRXFCR_DMA_EN		BIT(1)
#define FSPI_IPRXFCR_WMRK(x)		((x) << 2)

#define FSPI_IPTXFCR			0xBC
#define FSPI_IPTXFCR_CLR		BIT(0)
#define FSPI_IPTXFCR_DMA_EN		BIT(1)
#define FSPI_IPTXFCR_WMRK(x)		((x) << 2)

#define FSPI_DLLACR			0xC0
#define FSPI_DLLACR_OVRDEN		BIT(8)

#define FSPI_DLLBCR			0xC4
#define FSPI_DLLBCR_OVRDEN		BIT(8)

#define FSPI_STS0			0xE0
#define FSPI_STS0_DLPHB(x)		((x) << 8)
#define FSPI_STS0_DLPHA(x)		((x) << 4)
#define FSPI_STS0_CMD_SRC(x)		((x) << 2)
#define FSPI_STS0_ARB_IDLE		BIT(1)
#define FSPI_STS0_SEQ_IDLE		BIT(0)

#define FSPI_STS1			0xE4
#define FSPI_STS1_IP_ERRCD(x)		((x) << 24)
#define FSPI_STS1_IP_ERRID(x)		((x) << 16)
#define FSPI_STS1_AHB_ERRCD(x)		((x) << 8)
#define FSPI_STS1_AHB_ERRID(x)		(x)

#define FSPI_AHBSPNST			0xEC
#define FSPI_AHBSPNST_DATLFT(x)		((x) << 16)
#define FSPI_AHBSPNST_BUFID(x)		((x) << 1)
#define FSPI_AHBSPNST_ACTIVE		BIT(0)

#define FSPI_IPRXFSTS			0xF0
#define FSPI_IPRXFSTS_RDCNTR(x)		((x) << 16)
#define FSPI_IPRXFSTS_FILL(x)		(x)

#define FSPI_IPTXFSTS			0xF4
#define FSPI_IPTXFSTS_WRCNTR(x)		((x) << 16)
#define FSPI_IPTXFSTS_FILL(x)		(x)

#define FSPI_RFDR			0x100
#define FSPI_TFDR			0x180

#define FSPI_LUT_BASE			0x200
#define FSPI_LUT_OFFSET			(SEQID_LUT * 4 * 4)
#define FSPI_LUT_REG(idx) \
	(FSPI_LUT_BASE + FSPI_LUT_OFFSET + (idx) * 4)

/* register map end */

/* Instruction set for the LUT register. */
#define LUT_STOP			0x00
#define LUT_CMD				0x01
#define LUT_ADDR			0x02
#define LUT_CADDR_SDR			0x03
#define LUT_MODE			0x04
#define LUT_MODE2			0x05
#define LUT_MODE4			0x06
#define LUT_MODE8			0x07
#define LUT_NXP_WRITE			0x08
#define LUT_NXP_READ			0x09
#define LUT_LEARN_SDR			0x0A
#define LUT_DATSZ_SDR			0x0B
#define LUT_DUMMY			0x0C
#define LUT_DUMMY_RWDS_SDR		0x0D
#define LUT_JMP_ON_CS			0x1F
#define LUT_CMD_DDR			0x21
#define LUT_ADDR_DDR			0x22
#define LUT_CADDR_DDR			0x23
#define LUT_MODE_DDR			0x24
#define LUT_MODE2_DDR			0x25
#define LUT_MODE4_DDR			0x26
#define LUT_MODE8_DDR			0x27
#define LUT_WRITE_DDR			0x28
#define LUT_READ_DDR			0x29
#define LUT_LEARN_DDR			0x2A
#define LUT_DATSZ_DDR			0x2B
#define LUT_DUMMY_DDR			0x2C
#define LUT_DUMMY_RWDS_DDR		0x2D

/*
 * Calculate number of required PAD bits for LUT register.
 *
 * The pad stands for the number of IO lines [0:7].
 * For example, the octal read needs eight IO lines,
 * so you should use LUT_PAD(8). This macro
 * returns 3 i.e. use eight (2^3) IP lines for read.
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
#define PAD_SHIFT		8
#define INSTR_SHIFT		10
#define OPRND_SHIFT		16

/* Macros for constructing the LUT register. */
#define LUT_DEF(idx, ins, pad, opr)			  \
	((((ins) << INSTR_SHIFT) | ((pad) << PAD_SHIFT) | \
	(opr)) << (((idx) % 2) * OPRND_SHIFT))

#define POLL_TOUT		5000
#define NXP_FSPI_MAX_CHIPSELECT		4
#define NXP_FSPI_MIN_IOMAP	SZ_4M

#define DCFG_RCWSR1		0x100

/* Access flash memory using IP bus only */
#define FSPI_QUIRK_USE_IP_ONLY	BIT(0)

struct nxp_fspi_devtype_data {
	unsigned int rxfifo;
	unsigned int txfifo;
	unsigned int ahb_buf_size;
	unsigned int quirks;
	bool little_endian;
};

static struct nxp_fspi_devtype_data lx2160a_data = {
	.rxfifo = SZ_512,       /* (64  * 64 bits)  */
	.txfifo = SZ_1K,        /* (128 * 64 bits)  */
	.ahb_buf_size = SZ_2K,  /* (256 * 64 bits)  */
	.quirks = 0,
	.little_endian = true,  /* little-endian    */
};

static struct nxp_fspi_devtype_data imx8mm_data = {
	.rxfifo = SZ_512,       /* (64  * 64 bits)  */
	.txfifo = SZ_1K,        /* (128 * 64 bits)  */
	.ahb_buf_size = SZ_2K,  /* (256 * 64 bits)  */
	.quirks = 0,
	.little_endian = true,  /* little-endian    */
};

static struct nxp_fspi_devtype_data imx8qxp_data = {
	.rxfifo = SZ_512,       /* (64  * 64 bits)  */
	.txfifo = SZ_1K,        /* (128 * 64 bits)  */
	.ahb_buf_size = SZ_2K,  /* (256 * 64 bits)  */
	.quirks = 0,
	.little_endian = true,  /* little-endian    */
};

static struct nxp_fspi_devtype_data imx8dxl_data = {
	.rxfifo = SZ_512,       /* (64  * 64 bits)  */
	.txfifo = SZ_1K,        /* (128 * 64 bits)  */
	.ahb_buf_size = SZ_2K,  /* (256 * 64 bits)  */
	.quirks = FSPI_QUIRK_USE_IP_ONLY,
	.little_endian = true,  /* little-endian    */
};

struct nxp_fspi {
	void __iomem *iobase;
	void __iomem *ahb_addr;
	u32 memmap_phy;
	u32 memmap_phy_size;
	u32 memmap_start;
	u32 memmap_len;
	struct clk *clk, *clk_en;
	struct device *dev;
	struct completion c;
	struct nxp_fspi_devtype_data *devtype_data;
	struct mutex lock;
	struct pm_qos_request pm_qos_req;
	int selected;
};

static inline int needs_ip_only(struct nxp_fspi *f)
{
	return f->devtype_data->quirks & FSPI_QUIRK_USE_IP_ONLY;
}

/*
 * R/W functions for big- or little-endian registers:
 * The FSPI controller's endianness is independent of
 * the CPU core's endianness. So far, although the CPU
 * core is little-endian the FSPI controller can use
 * big-endian or little-endian.
 */
static void fspi_writel(struct nxp_fspi *f, u32 val, void __iomem *addr)
{
	if (f->devtype_data->little_endian)
		iowrite32(val, addr);
	else
		iowrite32be(val, addr);
}

static u32 fspi_readl(struct nxp_fspi *f, void __iomem *addr)
{
	if (f->devtype_data->little_endian)
		return ioread32(addr);
	else
		return ioread32be(addr);
}

static irqreturn_t nxp_fspi_irq_handler(int irq, void *dev_id)
{
	struct nxp_fspi *f = dev_id;
	u32 reg;

	/* clear interrupt */
	reg = fspi_readl(f, f->iobase + FSPI_INTR);
	fspi_writel(f, FSPI_INTR_IPCMDDONE, f->iobase + FSPI_INTR);

	if (reg & FSPI_INTR_IPCMDDONE)
		complete(&f->c);

	return IRQ_HANDLED;
}

static int nxp_fspi_check_buswidth(struct nxp_fspi *f, u8 width)
{
	switch (width) {
	case 1:
	case 2:
	case 4:
	case 8:
		return 0;
	}

	return -ENOTSUPP;
}

static bool nxp_fspi_supports_op(struct spi_mem *mem,
				 const struct spi_mem_op *op)
{
	struct nxp_fspi *f = spi_controller_get_devdata(mem->spi->master);
	int ret;

	ret = nxp_fspi_check_buswidth(f, op->cmd.buswidth);

	if (op->addr.nbytes)
		ret |= nxp_fspi_check_buswidth(f, op->addr.buswidth);

	if (op->dummy.nbytes)
		ret |= nxp_fspi_check_buswidth(f, op->dummy.buswidth);

	if (op->data.nbytes)
		ret |= nxp_fspi_check_buswidth(f, op->data.buswidth);

	if (ret)
		return false;

	/*
	 * The number of address bytes should be equal to or less than 4 bytes.
	 */
	if (op->addr.nbytes > 4)
		return false;

	/*
	 * If requested address value is greater than controller assigned
	 * memory mapped space, return error as it didn't fit in the range
	 * of assigned address space.
	 */
	if (op->addr.val >= f->memmap_phy_size)
		return false;

	/* Max 64 dummy clock cycles supported */
	if (op->dummy.buswidth &&
	    (op->dummy.nbytes * 8 / op->dummy.buswidth > 64))
		return false;

	/* Max data length, check controller limits and alignment */
	if (op->data.dir == SPI_MEM_DATA_IN &&
	    (op->data.nbytes > f->devtype_data->ahb_buf_size ||
	     (op->data.nbytes > f->devtype_data->rxfifo - 4 &&
	      !IS_ALIGNED(op->data.nbytes, 8))))
		return false;

	if (op->data.dir == SPI_MEM_DATA_OUT &&
	    op->data.nbytes > f->devtype_data->txfifo)
		return false;

	return spi_mem_default_supports_op(mem, op);
}

/* Instead of busy looping invoke readl_poll_timeout functionality. */
static int fspi_readl_poll_tout(struct nxp_fspi *f, void __iomem *base,
				u32 mask, u32 delay_us,
				u32 timeout_us, bool c)
{
	u32 reg;

	if (!f->devtype_data->little_endian)
		mask = (u32)cpu_to_be32(mask);

	if (c)
		return readl_poll_timeout(base, reg, (reg & mask),
					  delay_us, timeout_us);
	else
		return readl_poll_timeout(base, reg, !(reg & mask),
					  delay_us, timeout_us);
}

/*
 * If the slave device content being changed by Write/Erase, need to
 * invalidate the AHB buffer. This can be achieved by doing the reset
 * of controller after setting MCR0[SWRESET] bit.
 */
static inline void nxp_fspi_invalid(struct nxp_fspi *f)
{
	u32 reg;
	int ret;

	reg = fspi_readl(f, f->iobase + FSPI_MCR0);
	fspi_writel(f, reg | FSPI_MCR0_SWRST, f->iobase + FSPI_MCR0);

	/* w1c register, wait unit clear */
	ret = fspi_readl_poll_tout(f, f->iobase + FSPI_MCR0,
				   FSPI_MCR0_SWRST, 0, POLL_TOUT, false);
	WARN_ON(ret);
}

static void nxp_fspi_prepare_lut(struct nxp_fspi *f,
				 const struct spi_mem_op *op)
{
	void __iomem *base = f->iobase;
	u32 lutval[4] = {};
	int lutidx = 1, i;

	/* cmd */
	lutval[0] |= LUT_DEF(0, LUT_CMD, LUT_PAD(op->cmd.buswidth),
			     op->cmd.opcode);

	/* addr bytes */
	if (op->addr.nbytes) {
		lutval[lutidx / 2] |= LUT_DEF(lutidx, LUT_ADDR,
					      LUT_PAD(op->addr.buswidth),
					      op->addr.nbytes * 8);
		lutidx++;
	}

	/* dummy bytes, if needed */
	if (op->dummy.nbytes) {
		lutval[lutidx / 2] |= LUT_DEF(lutidx, LUT_DUMMY,
		/*
		 * Due to FlexSPI controller limitation number of PAD for dummy
		 * buswidth needs to be programmed as equal to data buswidth.
		 */
					      LUT_PAD(op->data.buswidth),
					      op->dummy.nbytes * 8 /
					      op->dummy.buswidth);
		lutidx++;
	}

	/* read/write data bytes */
	if (op->data.nbytes) {
		lutval[lutidx / 2] |= LUT_DEF(lutidx,
					      op->data.dir == SPI_MEM_DATA_IN ?
					      LUT_NXP_READ : LUT_NXP_WRITE,
					      LUT_PAD(op->data.buswidth),
					      0);
		lutidx++;
	}

	/* stop condition. */
	lutval[lutidx / 2] |= LUT_DEF(lutidx, LUT_STOP, 0, 0);

	/* unlock LUT */
	fspi_writel(f, FSPI_LUTKEY_VALUE, f->iobase + FSPI_LUTKEY);
	fspi_writel(f, FSPI_LCKER_UNLOCK, f->iobase + FSPI_LCKCR);

	/* fill LUT */
	for (i = 0; i < ARRAY_SIZE(lutval); i++)
		fspi_writel(f, lutval[i], base + FSPI_LUT_REG(i));

	dev_dbg(f->dev, "CMD[%x] lutval[0:%x \t 1:%x \t 2:%x \t 3:%x], size: 0x%08x\n",
		op->cmd.opcode, lutval[0], lutval[1], lutval[2], lutval[3], op->data.nbytes);

	/* lock LUT */
	fspi_writel(f, FSPI_LUTKEY_VALUE, f->iobase + FSPI_LUTKEY);
	fspi_writel(f, FSPI_LCKER_LOCK, f->iobase + FSPI_LCKCR);
}

static int nxp_fspi_clk_prep_enable(struct nxp_fspi *f)
{
	int ret;

	if (is_acpi_node(f->dev->fwnode))
		return 0;

	ret = clk_prepare_enable(f->clk_en);
	if (ret)
		return ret;

	ret = clk_prepare_enable(f->clk);
	if (ret) {
		clk_disable_unprepare(f->clk_en);
		return ret;
	}

	return 0;
}

static int nxp_fspi_clk_disable_unprep(struct nxp_fspi *f)
{
	if (is_acpi_node(f->dev->fwnode))
		return 0;

	clk_disable_unprepare(f->clk);
	clk_disable_unprepare(f->clk_en);

	return 0;
}

/*
 * In FlexSPI controller, flash access is based on value of FSPI_FLSHXXCR0
 * register and start base address of the slave device.
 *
 *							    (Higher address)
 *				--------    <-- FLSHB2CR0
 *				|  B2  |
 *				|      |
 *	B2 start address -->	--------    <-- FLSHB1CR0
 *				|  B1  |
 *				|      |
 *	B1 start address -->	--------    <-- FLSHA2CR0
 *				|  A2  |
 *				|      |
 *	A2 start address -->	--------    <-- FLSHA1CR0
 *				|  A1  |
 *				|      |
 *	A1 start address -->	--------		    (Lower address)
 *
 *
 * Start base address defines the starting address range for given CS and
 * FSPI_FLSHXXCR0 defines the size of the slave device connected at given CS.
 *
 * But, different targets are having different combinations of number of CS,
 * some targets only have single CS or two CS covering controller's full
 * memory mapped space area.
 * Thus, implementation is being done as independent of the size and number
 * of the connected slave device.
 * Assign controller memory mapped space size as the size to the connected
 * slave device.
 * Mark FLSHxxCR0 as zero initially and then assign value only to the selected
 * chip-select Flash configuration register.
 *
 * For e.g. to access CS2 (B1), FLSHB1CR0 register would be equal to the
 * memory mapped size of the controller.
 * Value for rest of the CS FLSHxxCR0 register would be zero.
 *
 */
static void nxp_fspi_select_mem(struct nxp_fspi *f, struct spi_device *spi)
{
	unsigned long rate = spi->max_speed_hz;
	int ret;
	uint64_t size_kb;

	/*
	 * Return, if previously selected slave device is same as current
	 * requested slave device.
	 */
	if (f->selected == spi->chip_select)
		return;

	/* Reset FLSHxxCR0 registers */
	fspi_writel(f, 0, f->iobase + FSPI_FLSHA1CR0);
	fspi_writel(f, 0, f->iobase + FSPI_FLSHA2CR0);
	fspi_writel(f, 0, f->iobase + FSPI_FLSHB1CR0);
	fspi_writel(f, 0, f->iobase + FSPI_FLSHB2CR0);

	/* Assign controller memory mapped space as size, KBytes, of flash. */
	size_kb = FSPI_FLSHXCR0_SZ(f->memmap_phy_size);

	fspi_writel(f, size_kb, f->iobase + FSPI_FLSHA1CR0 +
		    4 * spi->chip_select);

	dev_dbg(f->dev, "Slave device [CS:%x] selected\n", spi->chip_select);

	nxp_fspi_clk_disable_unprep(f);

	ret = clk_set_rate(f->clk, rate);
	if (ret)
		return;

	ret = nxp_fspi_clk_prep_enable(f);
	if (ret)
		return;

	f->selected = spi->chip_select;
}

static int nxp_fspi_read_ahb(struct nxp_fspi *f, const struct spi_mem_op *op)
{
	u32 start = op->addr.val;
	u32 len = op->data.nbytes;

	/* if necessary, ioremap before AHB read */
	if ((!f->ahb_addr) || start < f->memmap_start ||
	     start + len > f->memmap_start + f->memmap_len) {
		if (f->ahb_addr)
			iounmap(f->ahb_addr);

		f->memmap_start = start;
		f->memmap_len = len > NXP_FSPI_MIN_IOMAP ?
				len : NXP_FSPI_MIN_IOMAP;

		f->ahb_addr = ioremap_wc(f->memmap_phy + f->memmap_start,
					 f->memmap_len);

		if (!f->ahb_addr) {
			dev_err(f->dev, "failed to alloc memory\n");
			return -ENOMEM;
		}
	}

	/* Read out the data directly from the AHB buffer. */
	memcpy_fromio(op->data.buf.in,
		      f->ahb_addr + start - f->memmap_start, len);

	return 0;
}

static void nxp_fspi_fill_txfifo(struct nxp_fspi *f,
				 const struct spi_mem_op *op)
{
	void __iomem *base = f->iobase;
	int i, ret;
	u8 *buf = (u8 *) op->data.buf.out;

	/* clear the TX FIFO. */
	fspi_writel(f, FSPI_IPTXFCR_CLR, base + FSPI_IPTXFCR);

	/*
	 * Default value of water mark level is 8 bytes, hence in single
	 * write request controller can write max 8 bytes of data.
	 */

	for (i = 0; i < ALIGN_DOWN(op->data.nbytes, 8); i += 8) {
		/* Wait for TXFIFO empty */
		ret = fspi_readl_poll_tout(f, f->iobase + FSPI_INTR,
					   FSPI_INTR_IPTXWE, 0,
					   POLL_TOUT, true);
		WARN_ON(ret);

		fspi_writel(f, *(u32 *) (buf + i), base + FSPI_TFDR);
		fspi_writel(f, *(u32 *) (buf + i + 4), base + FSPI_TFDR + 4);
		fspi_writel(f, FSPI_INTR_IPTXWE, base + FSPI_INTR);
	}

	if (i < op->data.nbytes) {
		u32 data = 0;
		int j;
		/* Wait for TXFIFO empty */
		ret = fspi_readl_poll_tout(f, f->iobase + FSPI_INTR,
					   FSPI_INTR_IPTXWE, 0,
					   POLL_TOUT, true);
		WARN_ON(ret);

		for (j = 0; j < ALIGN(op->data.nbytes - i, 4); j += 4) {
			memcpy(&data, buf + i + j, 4);
			fspi_writel(f, data, base + FSPI_TFDR + j);
		}
		fspi_writel(f, FSPI_INTR_IPTXWE, base + FSPI_INTR);
	}
}

static void nxp_fspi_read_rxfifo(struct nxp_fspi *f,
			  const struct spi_mem_op *op)
{
	void __iomem *base = f->iobase;
	int i, ret;
	int len = op->data.nbytes;
	u8 *buf = (u8 *) op->data.buf.in;

	/*
	 * Default value of water mark level is 8 bytes, hence in single
	 * read request controller can read max 8 bytes of data.
	 */
	for (i = 0; i < ALIGN_DOWN(len, 8); i += 8) {
		/* Wait for RXFIFO available */
		ret = fspi_readl_poll_tout(f, f->iobase + FSPI_INTR,
					   FSPI_INTR_IPRXWA, 0,
					   POLL_TOUT, true);
		WARN_ON(ret);

		*(u32 *)(buf + i) = fspi_readl(f, base + FSPI_RFDR);
		*(u32 *)(buf + i + 4) = fspi_readl(f, base + FSPI_RFDR + 4);
		/* move the FIFO pointer */
		fspi_writel(f, FSPI_INTR_IPRXWA, base + FSPI_INTR);
	}

	if (i < len) {
		u32 tmp;
		int size, j;

		buf = op->data.buf.in + i;
		/* Wait for RXFIFO available */
		ret = fspi_readl_poll_tout(f, f->iobase + FSPI_INTR,
					   FSPI_INTR_IPRXWA, 0,
					   POLL_TOUT, true);
		WARN_ON(ret);

		len = op->data.nbytes - i;
		for (j = 0; j < op->data.nbytes - i; j += 4) {
			tmp = fspi_readl(f, base + FSPI_RFDR + j);
			size = min(len, 4);
			memcpy(buf + j, &tmp, size);
			len -= size;
		}
	}

	/* invalid the RXFIFO */
	fspi_writel(f, FSPI_IPRXFCR_CLR, base + FSPI_IPRXFCR);
	/* move the FIFO pointer */
	fspi_writel(f, FSPI_INTR_IPRXWA, base + FSPI_INTR);
}

static int nxp_fspi_do_op(struct nxp_fspi *f, const struct spi_mem_op *op)
{
	void __iomem *base = f->iobase;
	int seqnum = 0;
	int err = 0;
	u32 reg;

	reg = fspi_readl(f, base + FSPI_IPRXFCR);
	/* invalid RXFIFO first */
	reg &= ~FSPI_IPRXFCR_DMA_EN;
	reg = reg | FSPI_IPRXFCR_CLR;
	fspi_writel(f, reg, base + FSPI_IPRXFCR);

	init_completion(&f->c);

	fspi_writel(f, op->addr.val, base + FSPI_IPCR0);
	/*
	 * Always start the sequence at the same index since we update
	 * the LUT at each exec_op() call. And also specify the DATA
	 * length, since it's has not been specified in the LUT.
	 */
	fspi_writel(f, op->data.nbytes |
		 (SEQID_LUT << FSPI_IPCR1_SEQID_SHIFT) |
		 (seqnum << FSPI_IPCR1_SEQNUM_SHIFT),
		 base + FSPI_IPCR1);

	/* Trigger the LUT now. */
	fspi_writel(f, FSPI_IPCMD_TRG, base + FSPI_IPCMD);

	/* Wait for the interrupt. */
	if (!wait_for_completion_timeout(&f->c, msecs_to_jiffies(1000)))
		err = -ETIMEDOUT;

	/* Invoke IP data read, if request is of data read. */
	if (!err && op->data.nbytes && op->data.dir == SPI_MEM_DATA_IN)
		nxp_fspi_read_rxfifo(f, op);

	return err;
}

static int nxp_fspi_exec_op(struct spi_mem *mem, const struct spi_mem_op *op)
{
	struct nxp_fspi *f = spi_controller_get_devdata(mem->spi->master);
	int err = 0;

	mutex_lock(&f->lock);

	/* Wait for controller being ready. */
	err = fspi_readl_poll_tout(f, f->iobase + FSPI_STS0,
				   FSPI_STS0_ARB_IDLE, 1, POLL_TOUT, true);
	WARN_ON(err);

	nxp_fspi_select_mem(f, mem->spi);

	nxp_fspi_prepare_lut(f, op);
	/*
	 * If we have large chunks of data, we read them through the AHB bus by
	 * accessing the mapped memory. In all other cases we use IP commands
	 * to access the flash. Read via AHB bus may be corrupted due to
	 * existence of an errata and therefore discard AHB read in such cases.
	 */
	if (op->data.nbytes > (f->devtype_data->rxfifo - 4) &&
	    op->data.dir == SPI_MEM_DATA_IN &&
	    !needs_ip_only(f)) {
		err = nxp_fspi_read_ahb(f, op);
	} else {
		if (op->data.nbytes && op->data.dir == SPI_MEM_DATA_OUT)
			nxp_fspi_fill_txfifo(f, op);

		err = nxp_fspi_do_op(f, op);
	}

	/* Invalidate the data in the AHB buffer. */
	nxp_fspi_invalid(f);

	mutex_unlock(&f->lock);

	return err;
}

static int nxp_fspi_adjust_op_size(struct spi_mem *mem, struct spi_mem_op *op)
{
	struct nxp_fspi *f = spi_controller_get_devdata(mem->spi->master);

	if (op->data.dir == SPI_MEM_DATA_OUT) {
		if (op->data.nbytes > f->devtype_data->txfifo)
			op->data.nbytes = f->devtype_data->txfifo;
	} else {
		if (op->data.nbytes > f->devtype_data->ahb_buf_size)
			op->data.nbytes = f->devtype_data->ahb_buf_size;
		else if (op->data.nbytes > (f->devtype_data->rxfifo - 4))
			op->data.nbytes = ALIGN_DOWN(op->data.nbytes, 8);
	}

	/* Limit data bytes to RX FIFO in case of IP read only */
	if (op->data.dir == SPI_MEM_DATA_IN &&
	    needs_ip_only(f) &&
	    op->data.nbytes > f->devtype_data->rxfifo)
		op->data.nbytes = f->devtype_data->rxfifo;

	return 0;
}

static void erratum_err050568(struct nxp_fspi *f)
{
	const struct soc_device_attribute ls1028a_soc_attr[] = {
		{ .family = "QorIQ LS1028A" },
		{ /* sentinel */ }
	};
	struct device_node *np;
	struct regmap *map;
	u32 val = 0, sysclk = 0;
	int ret;

	/* Check for LS1028A family */
	if (!soc_device_match(ls1028a_soc_attr)) {
		dev_dbg(f->dev, "Errata applicable only for LS1028A\n");
		return;
	}

	/* Compute system clock frequency multiplier ratio */
	map = syscon_regmap_lookup_by_compatible("fsl,ls1028a-dcfg");
	if (IS_ERR(map)) {
		dev_err(f->dev, "No syscon regmap\n");
		goto err;
	}

	ret = regmap_read(map, DCFG_RCWSR1, &val);
	if (ret < 0)
		goto err;

	/* Strap bits 6:2 define SYS_PLL_RAT i.e frequency multiplier ratio */
	val = (val >> 2) & 0x1F;
	WARN(val == 0, "Strapping is zero: Cannot determine ratio");

	/* Compute system clock frequency */
	np = of_find_node_by_name(NULL, "clock-sysclk");
	if (!np)
		goto err;

	if (of_property_read_u32(np, "clock-frequency", &sysclk))
		goto err;

	sysclk = (sysclk * val) / 1000000; /* Convert sysclk to Mhz */
	dev_dbg(f->dev, "val: 0x%08x, sysclk: %dMhz\n", val, sysclk);

	/* Use IP bus only if PLL is 300MHz */
	if (sysclk == 300)
		f->devtype_data->quirks |= FSPI_QUIRK_USE_IP_ONLY;

	return;

err:
	dev_err(f->dev, "Errata cannot be executed. Read via IP bus may not work\n");
}

static int nxp_fspi_default_setup(struct nxp_fspi *f)
{
	void __iomem *base = f->iobase;
	int ret, i;
	u32 reg;

	/* disable and unprepare clock to avoid glitch pass to controller */
	nxp_fspi_clk_disable_unprep(f);

	/* the default frequency, we will change it later if necessary. */
	ret = clk_set_rate(f->clk, 20000000);
	if (ret)
		return ret;

	ret = nxp_fspi_clk_prep_enable(f);
	if (ret)
		return ret;

	/*
	 * ERR050568: Flash access by FlexSPI AHB command may not work with
	 * platform frequency equal to 300 MHz on LS1028A.
	 * LS1028A reuses LX2160A compatible entry. Make errata applicable for
	 * Layerscape LS1028A platform.
	 */
	if (of_device_is_compatible(f->dev->of_node, "nxp,lx2160a-fspi"))
		erratum_err050568(f);

	/* Reset the module */
	/* w1c register, wait unit clear */
	ret = fspi_readl_poll_tout(f, f->iobase + FSPI_MCR0,
				   FSPI_MCR0_SWRST, 0, POLL_TOUT, false);
	WARN_ON(ret);

	/* Disable the module */
	fspi_writel(f, FSPI_MCR0_MDIS, base + FSPI_MCR0);

	/* Reset the DLL register to default value */
	fspi_writel(f, FSPI_DLLACR_OVRDEN, base + FSPI_DLLACR);
	fspi_writel(f, FSPI_DLLBCR_OVRDEN, base + FSPI_DLLBCR);

	/* enable module */
	fspi_writel(f, FSPI_MCR0_AHB_TIMEOUT(0xFF) |
		    FSPI_MCR0_IP_TIMEOUT(0xFF) | (u32) FSPI_MCR0_OCTCOMB_EN,
		    base + FSPI_MCR0);

	/*
	 * Disable same device enable bit and configure all slave devices
	 * independently.
	 */
	reg = fspi_readl(f, f->iobase + FSPI_MCR2);
	reg = reg & ~(FSPI_MCR2_SAMEDEVICEEN);
	fspi_writel(f, reg, base + FSPI_MCR2);

	/* AHB configuration for access buffer 0~7. */
	for (i = 0; i < 7; i++)
		fspi_writel(f, 0, base + FSPI_AHBRX_BUF0CR0 + 4 * i);

	/*
	 * Set ADATSZ with the maximum AHB buffer size to improve the read
	 * performance.
	 */
	fspi_writel(f, (f->devtype_data->ahb_buf_size / 8 |
		  FSPI_AHBRXBUF0CR7_PREF), base + FSPI_AHBRX_BUF7CR0);

	/* prefetch and no start address alignment limitation */
	fspi_writel(f, FSPI_AHBCR_PREF_EN | FSPI_AHBCR_RDADDROPT,
		 base + FSPI_AHBCR);

	/* AHB Read - Set lut sequence ID for all CS. */
	fspi_writel(f, SEQID_LUT, base + FSPI_FLSHA1CR2);
	fspi_writel(f, SEQID_LUT, base + FSPI_FLSHA2CR2);
	fspi_writel(f, SEQID_LUT, base + FSPI_FLSHB1CR2);
	fspi_writel(f, SEQID_LUT, base + FSPI_FLSHB2CR2);

	f->selected = -1;

	/* enable the interrupt */
	fspi_writel(f, FSPI_INTEN_IPCMDDONE, base + FSPI_INTEN);

	return 0;
}

static const char *nxp_fspi_get_name(struct spi_mem *mem)
{
	struct nxp_fspi *f = spi_controller_get_devdata(mem->spi->master);
	struct device *dev = &mem->spi->dev;
	const char *name;

	// Set custom name derived from the platform_device of the controller.
	if (of_get_available_child_count(f->dev->of_node) == 1)
		return dev_name(f->dev);

	name = devm_kasprintf(dev, GFP_KERNEL,
			      "%s-%d", dev_name(f->dev),
			      mem->spi->chip_select);

	if (!name) {
		dev_err(dev, "failed to get memory for custom flash name\n");
		return ERR_PTR(-ENOMEM);
	}

	return name;
}

static const struct spi_controller_mem_ops nxp_fspi_mem_ops = {
	.adjust_op_size = nxp_fspi_adjust_op_size,
	.supports_op = nxp_fspi_supports_op,
	.exec_op = nxp_fspi_exec_op,
	.get_name = nxp_fspi_get_name,
};

static int nxp_fspi_probe(struct platform_device *pdev)
{
	struct spi_controller *ctlr;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct resource *res;
	struct nxp_fspi *f;
	int ret;
	u32 reg;

	ctlr = spi_alloc_master(&pdev->dev, sizeof(*f));
	if (!ctlr)
		return -ENOMEM;

	ctlr->mode_bits = SPI_RX_DUAL | SPI_RX_QUAD | SPI_RX_OCTAL |
			  SPI_TX_DUAL | SPI_TX_QUAD | SPI_TX_OCTAL;

	f = spi_controller_get_devdata(ctlr);
	f->dev = dev;
	f->devtype_data = (struct nxp_fspi_devtype_data *)device_get_match_data(dev);
	if (!f->devtype_data) {
		ret = -ENODEV;
		goto err_put_ctrl;
	}

	platform_set_drvdata(pdev, f);

	/* find the resources - configuration register address space */
	if (is_acpi_node(f->dev->fwnode))
		res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	else
		res = platform_get_resource_byname(pdev,
				IORESOURCE_MEM, "fspi_base");

	f->iobase = devm_ioremap_resource(dev, res);
	if (IS_ERR(f->iobase)) {
		ret = PTR_ERR(f->iobase);
		goto err_put_ctrl;
	}

	/* Clear potential interrupts */
	reg = fspi_readl(f, f->iobase + FSPI_INTR);
	if (reg)
		fspi_writel(f, reg, f->iobase + FSPI_INTR);


	/* find the resources - controller memory mapped space */
	if (is_acpi_node(f->dev->fwnode))
		res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	else
		res = platform_get_resource_byname(pdev,
				IORESOURCE_MEM, "fspi_mmap");

	if (!res) {
		ret = -ENODEV;
		goto err_put_ctrl;
	}

	/* assign memory mapped starting address and mapped size. */
	f->memmap_phy = res->start;
	f->memmap_phy_size = resource_size(res);

	/* find the clocks */
	if (dev_of_node(&pdev->dev)) {
		f->clk_en = devm_clk_get(dev, "fspi_en");
		if (IS_ERR(f->clk_en)) {
			ret = PTR_ERR(f->clk_en);
			goto err_put_ctrl;
		}

		f->clk = devm_clk_get(dev, "fspi");
		if (IS_ERR(f->clk)) {
			ret = PTR_ERR(f->clk);
			goto err_put_ctrl;
		}

		ret = nxp_fspi_clk_prep_enable(f);
		if (ret) {
			dev_err(dev, "can not enable the clock\n");
			goto err_put_ctrl;
		}
	}

	/* find the irq */
	ret = platform_get_irq(pdev, 0);
	if (ret < 0)
		goto err_disable_clk;

	ret = devm_request_irq(dev, ret,
			nxp_fspi_irq_handler, 0, pdev->name, f);
	if (ret) {
		dev_err(dev, "failed to request irq: %d\n", ret);
		goto err_disable_clk;
	}

	mutex_init(&f->lock);

	ctlr->bus_num = -1;
	ctlr->num_chipselect = NXP_FSPI_MAX_CHIPSELECT;
	ctlr->mem_ops = &nxp_fspi_mem_ops;

	nxp_fspi_default_setup(f);

	ctlr->dev.of_node = np;

	ret = devm_spi_register_controller(&pdev->dev, ctlr);
	if (ret)
		goto err_destroy_mutex;

	return 0;

err_destroy_mutex:
	mutex_destroy(&f->lock);

err_disable_clk:
	nxp_fspi_clk_disable_unprep(f);

err_put_ctrl:
	spi_controller_put(ctlr);

	dev_err(dev, "NXP FSPI probe failed\n");
	return ret;
}

static int nxp_fspi_remove(struct platform_device *pdev)
{
	struct nxp_fspi *f = platform_get_drvdata(pdev);

	/* disable the hardware */
	fspi_writel(f, FSPI_MCR0_MDIS, f->iobase + FSPI_MCR0);

	nxp_fspi_clk_disable_unprep(f);

	mutex_destroy(&f->lock);

	if (f->ahb_addr)
		iounmap(f->ahb_addr);

	return 0;
}

static int nxp_fspi_suspend(struct device *dev)
{
	return 0;
}

static int nxp_fspi_resume(struct device *dev)
{
	struct nxp_fspi *f = dev_get_drvdata(dev);

	nxp_fspi_default_setup(f);

	return 0;
}

static const struct of_device_id nxp_fspi_dt_ids[] = {
	{ .compatible = "nxp,lx2160a-fspi", .data = (void *)&lx2160a_data, },
	{ .compatible = "nxp,imx8mm-fspi", .data = (void *)&imx8mm_data, },
	{ .compatible = "nxp,imx8mp-fspi", .data = (void *)&imx8mm_data, },
	{ .compatible = "nxp,imx8qxp-fspi", .data = (void *)&imx8qxp_data, },
	{ .compatible = "nxp,imx8dxl-fspi", .data = (void *)&imx8dxl_data, },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, nxp_fspi_dt_ids);

#ifdef CONFIG_ACPI
static const struct acpi_device_id nxp_fspi_acpi_ids[] = {
	{ "NXP0009", .driver_data = (kernel_ulong_t)&lx2160a_data, },
	{}
};
MODULE_DEVICE_TABLE(acpi, nxp_fspi_acpi_ids);
#endif

static const struct dev_pm_ops nxp_fspi_pm_ops = {
	.suspend	= nxp_fspi_suspend,
	.resume		= nxp_fspi_resume,
};

static struct platform_driver nxp_fspi_driver = {
	.driver = {
		.name	= "nxp-fspi",
		.of_match_table = nxp_fspi_dt_ids,
		.acpi_match_table = ACPI_PTR(nxp_fspi_acpi_ids),
		.pm =   &nxp_fspi_pm_ops,
	},
	.probe          = nxp_fspi_probe,
	.remove		= nxp_fspi_remove,
};
module_platform_driver(nxp_fspi_driver);

MODULE_DESCRIPTION("NXP FSPI Controller Driver");
MODULE_AUTHOR("NXP Semiconductor");
MODULE_AUTHOR("Yogesh Narayan Gaur <yogeshnarayan.gaur@nxp.com>");
MODULE_AUTHOR("Boris Brezillon <bbrezillon@kernel.org>");
MODULE_AUTHOR("Frieder Schrempf <frieder.schrempf@kontron.de>");
MODULE_LICENSE("GPL v2");
