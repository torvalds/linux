// SPDX-License-Identifier: GPL-2.0
/*
 * Marvell NAND flash controller driver
 *
 * Copyright (C) 2017 Marvell
 * Author: Miquel RAYNAL <miquel.raynal@free-electrons.com>
 *
 *
 * This NAND controller driver handles two versions of the hardware,
 * one is called NFCv1 and is available on PXA SoCs and the other is
 * called NFCv2 and is available on Armada SoCs.
 *
 * The main visible difference is that NFCv1 only has Hamming ECC
 * capabilities, while NFCv2 also embeds a BCH ECC engine. Also, DMA
 * is not used with NFCv2.
 *
 * The ECC layouts are depicted in details in Marvell AN-379, but here
 * is a brief description.
 *
 * When using Hamming, the data is split in 512B chunks (either 1, 2
 * or 4) and each chunk will have its own ECC "digest" of 6B at the
 * beginning of the OOB area and eventually the remaining free OOB
 * bytes (also called "spare" bytes in the driver). This engine
 * corrects up to 1 bit per chunk and detects reliably an error if
 * there are at most 2 bitflips. Here is the page layout used by the
 * controller when Hamming is chosen:
 *
 * +-------------------------------------------------------------+
 * | Data 1 | ... | Data N | ECC 1 | ... | ECCN | Free OOB bytes |
 * +-------------------------------------------------------------+
 *
 * When using the BCH engine, there are N identical (data + free OOB +
 * ECC) sections and potentially an extra one to deal with
 * configurations where the chosen (data + free OOB + ECC) sizes do
 * not align with the page (data + OOB) size. ECC bytes are always
 * 30B per ECC chunk. Here is the page layout used by the controller
 * when BCH is chosen:
 *
 * +-----------------------------------------
 * | Data 1 | Free OOB bytes 1 | ECC 1 | ...
 * +-----------------------------------------
 *
 *      -------------------------------------------
 *       ... | Data N | Free OOB bytes N | ECC N |
 *      -------------------------------------------
 *
 *           --------------------------------------------+
 *            Last Data | Last Free OOB bytes | Last ECC |
 *           --------------------------------------------+
 *
 * In both cases, the layout seen by the user is always: all data
 * first, then all free OOB bytes and finally all ECC bytes. With BCH,
 * ECC bytes are 30B long and are padded with 0xFF to align on 32
 * bytes.
 *
 * The controller has certain limitations that are handled by the
 * driver:
 *   - It can only read 2k at a time. To overcome this limitation, the
 *     driver issues data cycles on the bus, without issuing new
 *     CMD + ADDR cycles. The Marvell term is "naked" operations.
 *   - The ECC strength in BCH mode cannot be tuned. It is fixed 16
 *     bits. What can be tuned is the ECC block size as long as it
 *     stays between 512B and 2kiB. It's usually chosen based on the
 *     chip ECC requirements. For instance, using 2kiB ECC chunks
 *     provides 4b/512B correctability.
 *   - The controller will always treat data bytes, free OOB bytes
 *     and ECC bytes in that order, no matter what the real layout is
 *     (which is usually all data then all OOB bytes). The
 *     marvell_nfc_layouts array below contains the currently
 *     supported layouts.
 *   - Because of these weird layouts, the Bad Block Markers can be
 *     located in data section. In this case, the NAND_BBT_NO_OOB_BBM
 *     option must be set to prevent scanning/writing bad block
 *     markers.
 */

#include <linux/module.h>
#include <linux/clk.h>
#include <linux/mtd/rawnand.h>
#include <linux/of_platform.h>
#include <linux/iopoll.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <asm/unaligned.h>

#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/dma/pxa-dma.h>
#include <linux/platform_data/mtd-nand-pxa3xx.h>

/* Data FIFO granularity, FIFO reads/writes must be a multiple of this length */
#define FIFO_DEPTH		8
#define FIFO_REP(x)		(x / sizeof(u32))
#define BCH_SEQ_READS		(32 / FIFO_DEPTH)
/* NFC does not support transfers of larger chunks at a time */
#define MAX_CHUNK_SIZE		2112
/* NFCv1 cannot read more that 7 bytes of ID */
#define NFCV1_READID_LEN	7
/* Polling is done at a pace of POLL_PERIOD us until POLL_TIMEOUT is reached */
#define POLL_PERIOD		0
#define POLL_TIMEOUT		100000
/* Interrupt maximum wait period in ms */
#define IRQ_TIMEOUT		1000
/* Latency in clock cycles between SoC pins and NFC logic */
#define MIN_RD_DEL_CNT		3
/* Maximum number of contiguous address cycles */
#define MAX_ADDRESS_CYC_NFCV1	5
#define MAX_ADDRESS_CYC_NFCV2	7
/* System control registers/bits to enable the NAND controller on some SoCs */
#define GENCONF_SOC_DEVICE_MUX	0x208
#define GENCONF_SOC_DEVICE_MUX_NFC_EN BIT(0)
#define GENCONF_SOC_DEVICE_MUX_ECC_CLK_RST BIT(20)
#define GENCONF_SOC_DEVICE_MUX_ECC_CORE_RST BIT(21)
#define GENCONF_SOC_DEVICE_MUX_NFC_INT_EN BIT(25)
#define GENCONF_CLK_GATING_CTRL	0x220
#define GENCONF_CLK_GATING_CTRL_ND_GATE BIT(2)
#define GENCONF_ND_CLK_CTRL	0x700
#define GENCONF_ND_CLK_CTRL_EN	BIT(0)

/* NAND controller data flash control register */
#define NDCR			0x00
#define NDCR_ALL_INT		GENMASK(11, 0)
#define NDCR_CS1_CMDDM		BIT(7)
#define NDCR_CS0_CMDDM		BIT(8)
#define NDCR_RDYM		BIT(11)
#define NDCR_ND_ARB_EN		BIT(12)
#define NDCR_RA_START		BIT(15)
#define NDCR_RD_ID_CNT(x)	(min_t(unsigned int, x, 0x7) << 16)
#define NDCR_PAGE_SZ(x)		(x >= 2048 ? BIT(24) : 0)
#define NDCR_DWIDTH_M		BIT(26)
#define NDCR_DWIDTH_C		BIT(27)
#define NDCR_ND_RUN		BIT(28)
#define NDCR_DMA_EN		BIT(29)
#define NDCR_ECC_EN		BIT(30)
#define NDCR_SPARE_EN		BIT(31)
#define NDCR_GENERIC_FIELDS_MASK (~(NDCR_RA_START | NDCR_PAGE_SZ(2048) | \
				    NDCR_DWIDTH_M | NDCR_DWIDTH_C))

/* NAND interface timing parameter 0 register */
#define NDTR0			0x04
#define NDTR0_TRP(x)		((min_t(unsigned int, x, 0xF) & 0x7) << 0)
#define NDTR0_TRH(x)		(min_t(unsigned int, x, 0x7) << 3)
#define NDTR0_ETRP(x)		((min_t(unsigned int, x, 0xF) & 0x8) << 3)
#define NDTR0_SEL_NRE_EDGE	BIT(7)
#define NDTR0_TWP(x)		(min_t(unsigned int, x, 0x7) << 8)
#define NDTR0_TWH(x)		(min_t(unsigned int, x, 0x7) << 11)
#define NDTR0_TCS(x)		(min_t(unsigned int, x, 0x7) << 16)
#define NDTR0_TCH(x)		(min_t(unsigned int, x, 0x7) << 19)
#define NDTR0_RD_CNT_DEL(x)	(min_t(unsigned int, x, 0xF) << 22)
#define NDTR0_SELCNTR		BIT(26)
#define NDTR0_TADL(x)		(min_t(unsigned int, x, 0x1F) << 27)

/* NAND interface timing parameter 1 register */
#define NDTR1			0x0C
#define NDTR1_TAR(x)		(min_t(unsigned int, x, 0xF) << 0)
#define NDTR1_TWHR(x)		(min_t(unsigned int, x, 0xF) << 4)
#define NDTR1_TRHW(x)		(min_t(unsigned int, x / 16, 0x3) << 8)
#define NDTR1_PRESCALE		BIT(14)
#define NDTR1_WAIT_MODE		BIT(15)
#define NDTR1_TR(x)		(min_t(unsigned int, x, 0xFFFF) << 16)

/* NAND controller status register */
#define NDSR			0x14
#define NDSR_WRCMDREQ		BIT(0)
#define NDSR_RDDREQ		BIT(1)
#define NDSR_WRDREQ		BIT(2)
#define NDSR_CORERR		BIT(3)
#define NDSR_UNCERR		BIT(4)
#define NDSR_CMDD(cs)		BIT(8 - cs)
#define NDSR_RDY(rb)		BIT(11 + rb)
#define NDSR_ERRCNT(x)		((x >> 16) & 0x1F)

/* NAND ECC control register */
#define NDECCCTRL		0x28
#define NDECCCTRL_BCH_EN	BIT(0)

/* NAND controller data buffer register */
#define NDDB			0x40

/* NAND controller command buffer 0 register */
#define NDCB0			0x48
#define NDCB0_CMD1(x)		((x & 0xFF) << 0)
#define NDCB0_CMD2(x)		((x & 0xFF) << 8)
#define NDCB0_ADDR_CYC(x)	((x & 0x7) << 16)
#define NDCB0_ADDR_GET_NUM_CYC(x) (((x) >> 16) & 0x7)
#define NDCB0_DBC		BIT(19)
#define NDCB0_CMD_TYPE(x)	((x & 0x7) << 21)
#define NDCB0_CSEL		BIT(24)
#define NDCB0_RDY_BYP		BIT(27)
#define NDCB0_LEN_OVRD		BIT(28)
#define NDCB0_CMD_XTYPE(x)	((x & 0x7) << 29)

/* NAND controller command buffer 1 register */
#define NDCB1			0x4C
#define NDCB1_COLS(x)		((x & 0xFFFF) << 0)
#define NDCB1_ADDRS_PAGE(x)	(x << 16)

/* NAND controller command buffer 2 register */
#define NDCB2			0x50
#define NDCB2_ADDR5_PAGE(x)	(((x >> 16) & 0xFF) << 0)
#define NDCB2_ADDR5_CYC(x)	((x & 0xFF) << 0)

/* NAND controller command buffer 3 register */
#define NDCB3			0x54
#define NDCB3_ADDR6_CYC(x)	((x & 0xFF) << 16)
#define NDCB3_ADDR7_CYC(x)	((x & 0xFF) << 24)

/* NAND controller command buffer 0 register 'type' and 'xtype' fields */
#define TYPE_READ		0
#define TYPE_WRITE		1
#define TYPE_ERASE		2
#define TYPE_READ_ID		3
#define TYPE_STATUS		4
#define TYPE_RESET		5
#define TYPE_NAKED_CMD		6
#define TYPE_NAKED_ADDR		7
#define TYPE_MASK		7
#define XTYPE_MONOLITHIC_RW	0
#define XTYPE_LAST_NAKED_RW	1
#define XTYPE_FINAL_COMMAND	3
#define XTYPE_READ		4
#define XTYPE_WRITE_DISPATCH	4
#define XTYPE_NAKED_RW		5
#define XTYPE_COMMAND_DISPATCH	6
#define XTYPE_MASK		7

/**
 * Marvell ECC engine works differently than the others, in order to limit the
 * size of the IP, hardware engineers chose to set a fixed strength at 16 bits
 * per subpage, and depending on a the desired strength needed by the NAND chip,
 * a particular layout mixing data/spare/ecc is defined, with a possible last
 * chunk smaller that the others.
 *
 * @writesize:		Full page size on which the layout applies
 * @chunk:		Desired ECC chunk size on which the layout applies
 * @strength:		Desired ECC strength (per chunk size bytes) on which the
 *			layout applies
 * @nchunks:		Total number of chunks
 * @full_chunk_cnt:	Number of full-sized chunks, which is the number of
 *			repetitions of the pattern:
 *			(data_bytes + spare_bytes + ecc_bytes).
 * @data_bytes:		Number of data bytes per chunk
 * @spare_bytes:	Number of spare bytes per chunk
 * @ecc_bytes:		Number of ecc bytes per chunk
 * @last_data_bytes:	Number of data bytes in the last chunk
 * @last_spare_bytes:	Number of spare bytes in the last chunk
 * @last_ecc_bytes:	Number of ecc bytes in the last chunk
 */
struct marvell_hw_ecc_layout {
	/* Constraints */
	int writesize;
	int chunk;
	int strength;
	/* Corresponding layout */
	int nchunks;
	int full_chunk_cnt;
	int data_bytes;
	int spare_bytes;
	int ecc_bytes;
	int last_data_bytes;
	int last_spare_bytes;
	int last_ecc_bytes;
};

#define MARVELL_LAYOUT(ws, dc, ds, nc, fcc, db, sb, eb, ldb, lsb, leb)	\
	{								\
		.writesize = ws,					\
		.chunk = dc,						\
		.strength = ds,						\
		.nchunks = nc,						\
		.full_chunk_cnt = fcc,					\
		.data_bytes = db,					\
		.spare_bytes = sb,					\
		.ecc_bytes = eb,					\
		.last_data_bytes = ldb,					\
		.last_spare_bytes = lsb,				\
		.last_ecc_bytes = leb,					\
	}

/* Layouts explained in AN-379_Marvell_SoC_NFC_ECC */
static const struct marvell_hw_ecc_layout marvell_nfc_layouts[] = {
	MARVELL_LAYOUT(  512,   512,  1,  1,  1,  512,  8,  8,  0,  0,  0),
	MARVELL_LAYOUT( 2048,   512,  1,  1,  1, 2048, 40, 24,  0,  0,  0),
	MARVELL_LAYOUT( 2048,   512,  4,  1,  1, 2048, 32, 30,  0,  0,  0),
	MARVELL_LAYOUT( 2048,   512,  8,  2,  1, 1024,  0, 30,1024,32, 30),
	MARVELL_LAYOUT( 4096,   512,  4,  2,  2, 2048, 32, 30,  0,  0,  0),
	MARVELL_LAYOUT( 4096,   512,  8,  5,  4, 1024,  0, 30,  0, 64, 30),
	MARVELL_LAYOUT( 8192,   512,  4,  4,  4, 2048,  0, 30,  0,  0,  0),
	MARVELL_LAYOUT( 8192,   512,  8,  9,  8, 1024,  0, 30,  0, 160, 30),
};

/**
 * The Nand Flash Controller has up to 4 CE and 2 RB pins. The CE selection
 * is made by a field in NDCB0 register, and in another field in NDCB2 register.
 * The datasheet describes the logic with an error: ADDR5 field is once
 * declared at the beginning of NDCB2, and another time at its end. Because the
 * ADDR5 field of NDCB2 may be used by other bytes, it would be more logical
 * to use the last bit of this field instead of the first ones.
 *
 * @cs:			Wanted CE lane.
 * @ndcb0_csel:		Value of the NDCB0 register with or without the flag
 *			selecting the wanted CE lane. This is set once when
 *			the Device Tree is probed.
 * @rb:			Ready/Busy pin for the flash chip
 */
struct marvell_nand_chip_sel {
	unsigned int cs;
	u32 ndcb0_csel;
	unsigned int rb;
};

/**
 * NAND chip structure: stores NAND chip device related information
 *
 * @chip:		Base NAND chip structure
 * @node:		Used to store NAND chips into a list
 * @layout		NAND layout when using hardware ECC
 * @ndcr:		Controller register value for this NAND chip
 * @ndtr0:		Timing registers 0 value for this NAND chip
 * @ndtr1:		Timing registers 1 value for this NAND chip
 * @selected_die:	Current active CS
 * @nsels:		Number of CS lines required by the NAND chip
 * @sels:		Array of CS lines descriptions
 */
struct marvell_nand_chip {
	struct nand_chip chip;
	struct list_head node;
	const struct marvell_hw_ecc_layout *layout;
	u32 ndcr;
	u32 ndtr0;
	u32 ndtr1;
	int addr_cyc;
	int selected_die;
	unsigned int nsels;
	struct marvell_nand_chip_sel sels[];
};

static inline struct marvell_nand_chip *to_marvell_nand(struct nand_chip *chip)
{
	return container_of(chip, struct marvell_nand_chip, chip);
}

static inline struct marvell_nand_chip_sel *to_nand_sel(struct marvell_nand_chip
							*nand)
{
	return &nand->sels[nand->selected_die];
}

/**
 * NAND controller capabilities for distinction between compatible strings
 *
 * @max_cs_nb:		Number of Chip Select lines available
 * @max_rb_nb:		Number of Ready/Busy lines available
 * @need_system_controller: Indicates if the SoC needs to have access to the
 *                      system controller (ie. to enable the NAND controller)
 * @legacy_of_bindings:	Indicates if DT parsing must be done using the old
 *			fashion way
 * @is_nfcv2:		NFCv2 has numerous enhancements compared to NFCv1, ie.
 *			BCH error detection and correction algorithm,
 *			NDCB3 register has been added
 * @use_dma:		Use dma for data transfers
 */
struct marvell_nfc_caps {
	unsigned int max_cs_nb;
	unsigned int max_rb_nb;
	bool need_system_controller;
	bool legacy_of_bindings;
	bool is_nfcv2;
	bool use_dma;
};

/**
 * NAND controller structure: stores Marvell NAND controller information
 *
 * @controller:		Base controller structure
 * @dev:		Parent device (used to print error messages)
 * @regs:		NAND controller registers
 * @core_clk:		Core clock
 * @reg_clk:		Registers clock
 * @complete:		Completion object to wait for NAND controller events
 * @assigned_cs:	Bitmask describing already assigned CS lines
 * @chips:		List containing all the NAND chips attached to
 *			this NAND controller
 * @caps:		NAND controller capabilities for each compatible string
 * @dma_chan:		DMA channel (NFCv1 only)
 * @dma_buf:		32-bit aligned buffer for DMA transfers (NFCv1 only)
 */
struct marvell_nfc {
	struct nand_controller controller;
	struct device *dev;
	void __iomem *regs;
	struct clk *core_clk;
	struct clk *reg_clk;
	struct completion complete;
	unsigned long assigned_cs;
	struct list_head chips;
	struct nand_chip *selected_chip;
	const struct marvell_nfc_caps *caps;

	/* DMA (NFCv1 only) */
	bool use_dma;
	struct dma_chan *dma_chan;
	u8 *dma_buf;
};

static inline struct marvell_nfc *to_marvell_nfc(struct nand_controller *ctrl)
{
	return container_of(ctrl, struct marvell_nfc, controller);
}

/**
 * NAND controller timings expressed in NAND Controller clock cycles
 *
 * @tRP:		ND_nRE pulse width
 * @tRH:		ND_nRE high duration
 * @tWP:		ND_nWE pulse time
 * @tWH:		ND_nWE high duration
 * @tCS:		Enable signal setup time
 * @tCH:		Enable signal hold time
 * @tADL:		Address to write data delay
 * @tAR:		ND_ALE low to ND_nRE low delay
 * @tWHR:		ND_nWE high to ND_nRE low for status read
 * @tRHW:		ND_nRE high duration, read to write delay
 * @tR:			ND_nWE high to ND_nRE low for read
 */
struct marvell_nfc_timings {
	/* NDTR0 fields */
	unsigned int tRP;
	unsigned int tRH;
	unsigned int tWP;
	unsigned int tWH;
	unsigned int tCS;
	unsigned int tCH;
	unsigned int tADL;
	/* NDTR1 fields */
	unsigned int tAR;
	unsigned int tWHR;
	unsigned int tRHW;
	unsigned int tR;
};

/**
 * Derives a duration in numbers of clock cycles.
 *
 * @ps: Duration in pico-seconds
 * @period_ns:  Clock period in nano-seconds
 *
 * Convert the duration in nano-seconds, then divide by the period and
 * return the number of clock periods.
 */
#define TO_CYCLES(ps, period_ns) (DIV_ROUND_UP(ps / 1000, period_ns))
#define TO_CYCLES64(ps, period_ns) (DIV_ROUND_UP_ULL(div_u64(ps, 1000), \
						     period_ns))

/**
 * NAND driver structure filled during the parsing of the ->exec_op() subop
 * subset of instructions.
 *
 * @ndcb:		Array of values written to NDCBx registers
 * @cle_ale_delay_ns:	Optional delay after the last CMD or ADDR cycle
 * @rdy_timeout_ms:	Timeout for waits on Ready/Busy pin
 * @rdy_delay_ns:	Optional delay after waiting for the RB pin
 * @data_delay_ns:	Optional delay after the data xfer
 * @data_instr_idx:	Index of the data instruction in the subop
 * @data_instr:		Pointer to the data instruction in the subop
 */
struct marvell_nfc_op {
	u32 ndcb[4];
	unsigned int cle_ale_delay_ns;
	unsigned int rdy_timeout_ms;
	unsigned int rdy_delay_ns;
	unsigned int data_delay_ns;
	unsigned int data_instr_idx;
	const struct nand_op_instr *data_instr;
};

/*
 * Internal helper to conditionnally apply a delay (from the above structure,
 * most of the time).
 */
static void cond_delay(unsigned int ns)
{
	if (!ns)
		return;

	if (ns < 10000)
		ndelay(ns);
	else
		udelay(DIV_ROUND_UP(ns, 1000));
}

/*
 * The controller has many flags that could generate interrupts, most of them
 * are disabled and polling is used. For the very slow signals, using interrupts
 * may relax the CPU charge.
 */
static void marvell_nfc_disable_int(struct marvell_nfc *nfc, u32 int_mask)
{
	u32 reg;

	/* Writing 1 disables the interrupt */
	reg = readl_relaxed(nfc->regs + NDCR);
	writel_relaxed(reg | int_mask, nfc->regs + NDCR);
}

static void marvell_nfc_enable_int(struct marvell_nfc *nfc, u32 int_mask)
{
	u32 reg;

	/* Writing 0 enables the interrupt */
	reg = readl_relaxed(nfc->regs + NDCR);
	writel_relaxed(reg & ~int_mask, nfc->regs + NDCR);
}

static u32 marvell_nfc_clear_int(struct marvell_nfc *nfc, u32 int_mask)
{
	u32 reg;

	reg = readl_relaxed(nfc->regs + NDSR);
	writel_relaxed(int_mask, nfc->regs + NDSR);

	return reg & int_mask;
}

static void marvell_nfc_force_byte_access(struct nand_chip *chip,
					  bool force_8bit)
{
	struct marvell_nfc *nfc = to_marvell_nfc(chip->controller);
	u32 ndcr;

	/*
	 * Callers of this function do not verify if the NAND is using a 16-bit
	 * an 8-bit bus for normal operations, so we need to take care of that
	 * here by leaving the configuration unchanged if the NAND does not have
	 * the NAND_BUSWIDTH_16 flag set.
	 */
	if (!(chip->options & NAND_BUSWIDTH_16))
		return;

	ndcr = readl_relaxed(nfc->regs + NDCR);

	if (force_8bit)
		ndcr &= ~(NDCR_DWIDTH_M | NDCR_DWIDTH_C);
	else
		ndcr |= NDCR_DWIDTH_M | NDCR_DWIDTH_C;

	writel_relaxed(ndcr, nfc->regs + NDCR);
}

static int marvell_nfc_wait_ndrun(struct nand_chip *chip)
{
	struct marvell_nfc *nfc = to_marvell_nfc(chip->controller);
	u32 val;
	int ret;

	/*
	 * The command is being processed, wait for the ND_RUN bit to be
	 * cleared by the NFC. If not, we must clear it by hand.
	 */
	ret = readl_relaxed_poll_timeout(nfc->regs + NDCR, val,
					 (val & NDCR_ND_RUN) == 0,
					 POLL_PERIOD, POLL_TIMEOUT);
	if (ret) {
		dev_err(nfc->dev, "Timeout on NAND controller run mode\n");
		writel_relaxed(readl(nfc->regs + NDCR) & ~NDCR_ND_RUN,
			       nfc->regs + NDCR);
		return ret;
	}

	return 0;
}

/*
 * Any time a command has to be sent to the controller, the following sequence
 * has to be followed:
 * - call marvell_nfc_prepare_cmd()
 *      -> activate the ND_RUN bit that will kind of 'start a job'
 *      -> wait the signal indicating the NFC is waiting for a command
 * - send the command (cmd and address cycles)
 * - enventually send or receive the data
 * - call marvell_nfc_end_cmd() with the corresponding flag
 *      -> wait the flag to be triggered or cancel the job with a timeout
 *
 * The following helpers are here to factorize the code a bit so that
 * specialized functions responsible for executing the actual NAND
 * operations do not have to replicate the same code blocks.
 */
static int marvell_nfc_prepare_cmd(struct nand_chip *chip)
{
	struct marvell_nfc *nfc = to_marvell_nfc(chip->controller);
	u32 ndcr, val;
	int ret;

	/* Poll ND_RUN and clear NDSR before issuing any command */
	ret = marvell_nfc_wait_ndrun(chip);
	if (ret) {
		dev_err(nfc->dev, "Last operation did not succeed\n");
		return ret;
	}

	ndcr = readl_relaxed(nfc->regs + NDCR);
	writel_relaxed(readl(nfc->regs + NDSR), nfc->regs + NDSR);

	/* Assert ND_RUN bit and wait the NFC to be ready */
	writel_relaxed(ndcr | NDCR_ND_RUN, nfc->regs + NDCR);
	ret = readl_relaxed_poll_timeout(nfc->regs + NDSR, val,
					 val & NDSR_WRCMDREQ,
					 POLL_PERIOD, POLL_TIMEOUT);
	if (ret) {
		dev_err(nfc->dev, "Timeout on WRCMDRE\n");
		return -ETIMEDOUT;
	}

	/* Command may be written, clear WRCMDREQ status bit */
	writel_relaxed(NDSR_WRCMDREQ, nfc->regs + NDSR);

	return 0;
}

static void marvell_nfc_send_cmd(struct nand_chip *chip,
				 struct marvell_nfc_op *nfc_op)
{
	struct marvell_nand_chip *marvell_nand = to_marvell_nand(chip);
	struct marvell_nfc *nfc = to_marvell_nfc(chip->controller);

	dev_dbg(nfc->dev, "\nNDCR:  0x%08x\n"
		"NDCB0: 0x%08x\nNDCB1: 0x%08x\nNDCB2: 0x%08x\nNDCB3: 0x%08x\n",
		(u32)readl_relaxed(nfc->regs + NDCR), nfc_op->ndcb[0],
		nfc_op->ndcb[1], nfc_op->ndcb[2], nfc_op->ndcb[3]);

	writel_relaxed(to_nand_sel(marvell_nand)->ndcb0_csel | nfc_op->ndcb[0],
		       nfc->regs + NDCB0);
	writel_relaxed(nfc_op->ndcb[1], nfc->regs + NDCB0);
	writel(nfc_op->ndcb[2], nfc->regs + NDCB0);

	/*
	 * Write NDCB0 four times only if LEN_OVRD is set or if ADDR6 or ADDR7
	 * fields are used (only available on NFCv2).
	 */
	if (nfc_op->ndcb[0] & NDCB0_LEN_OVRD ||
	    NDCB0_ADDR_GET_NUM_CYC(nfc_op->ndcb[0]) >= 6) {
		if (!WARN_ON_ONCE(!nfc->caps->is_nfcv2))
			writel(nfc_op->ndcb[3], nfc->regs + NDCB0);
	}
}

static int marvell_nfc_end_cmd(struct nand_chip *chip, int flag,
			       const char *label)
{
	struct marvell_nfc *nfc = to_marvell_nfc(chip->controller);
	u32 val;
	int ret;

	ret = readl_relaxed_poll_timeout(nfc->regs + NDSR, val,
					 val & flag,
					 POLL_PERIOD, POLL_TIMEOUT);

	if (ret) {
		dev_err(nfc->dev, "Timeout on %s (NDSR: 0x%08x)\n",
			label, val);
		if (nfc->dma_chan)
			dmaengine_terminate_all(nfc->dma_chan);
		return ret;
	}

	/*
	 * DMA function uses this helper to poll on CMDD bits without wanting
	 * them to be cleared.
	 */
	if (nfc->use_dma && (readl_relaxed(nfc->regs + NDCR) & NDCR_DMA_EN))
		return 0;

	writel_relaxed(flag, nfc->regs + NDSR);

	return 0;
}

static int marvell_nfc_wait_cmdd(struct nand_chip *chip)
{
	struct marvell_nand_chip *marvell_nand = to_marvell_nand(chip);
	int cs_flag = NDSR_CMDD(to_nand_sel(marvell_nand)->ndcb0_csel);

	return marvell_nfc_end_cmd(chip, cs_flag, "CMDD");
}

static int marvell_nfc_wait_op(struct nand_chip *chip, unsigned int timeout_ms)
{
	struct marvell_nfc *nfc = to_marvell_nfc(chip->controller);
	u32 pending;
	int ret;

	/* Timeout is expressed in ms */
	if (!timeout_ms)
		timeout_ms = IRQ_TIMEOUT;

	init_completion(&nfc->complete);

	marvell_nfc_enable_int(nfc, NDCR_RDYM);
	ret = wait_for_completion_timeout(&nfc->complete,
					  msecs_to_jiffies(timeout_ms));
	marvell_nfc_disable_int(nfc, NDCR_RDYM);
	pending = marvell_nfc_clear_int(nfc, NDSR_RDY(0) | NDSR_RDY(1));

	/*
	 * In case the interrupt was not served in the required time frame,
	 * check if the ISR was not served or if something went actually wrong.
	 */
	if (!ret && !pending) {
		dev_err(nfc->dev, "Timeout waiting for RB signal\n");
		return -ETIMEDOUT;
	}

	return 0;
}

static void marvell_nfc_select_target(struct nand_chip *chip,
				      unsigned int die_nr)
{
	struct marvell_nand_chip *marvell_nand = to_marvell_nand(chip);
	struct marvell_nfc *nfc = to_marvell_nfc(chip->controller);
	u32 ndcr_generic;

	/*
	 * Reset the NDCR register to a clean state for this particular chip,
	 * also clear ND_RUN bit.
	 */
	ndcr_generic = readl_relaxed(nfc->regs + NDCR) &
		       NDCR_GENERIC_FIELDS_MASK & ~NDCR_ND_RUN;
	writel_relaxed(ndcr_generic | marvell_nand->ndcr, nfc->regs + NDCR);

	/* Also reset the interrupt status register */
	marvell_nfc_clear_int(nfc, NDCR_ALL_INT);

	if (chip == nfc->selected_chip && die_nr == marvell_nand->selected_die)
		return;

	writel_relaxed(marvell_nand->ndtr0, nfc->regs + NDTR0);
	writel_relaxed(marvell_nand->ndtr1, nfc->regs + NDTR1);

	nfc->selected_chip = chip;
	marvell_nand->selected_die = die_nr;
}

static irqreturn_t marvell_nfc_isr(int irq, void *dev_id)
{
	struct marvell_nfc *nfc = dev_id;
	u32 st = readl_relaxed(nfc->regs + NDSR);
	u32 ien = (~readl_relaxed(nfc->regs + NDCR)) & NDCR_ALL_INT;

	/*
	 * RDY interrupt mask is one bit in NDCR while there are two status
	 * bit in NDSR (RDY[cs0/cs2] and RDY[cs1/cs3]).
	 */
	if (st & NDSR_RDY(1))
		st |= NDSR_RDY(0);

	if (!(st & ien))
		return IRQ_NONE;

	marvell_nfc_disable_int(nfc, st & NDCR_ALL_INT);

	if (st & (NDSR_RDY(0) | NDSR_RDY(1)))
		complete(&nfc->complete);

	return IRQ_HANDLED;
}

/* HW ECC related functions */
static void marvell_nfc_enable_hw_ecc(struct nand_chip *chip)
{
	struct marvell_nfc *nfc = to_marvell_nfc(chip->controller);
	u32 ndcr = readl_relaxed(nfc->regs + NDCR);

	if (!(ndcr & NDCR_ECC_EN)) {
		writel_relaxed(ndcr | NDCR_ECC_EN, nfc->regs + NDCR);

		/*
		 * When enabling BCH, set threshold to 0 to always know the
		 * number of corrected bitflips.
		 */
		if (chip->ecc.algo == NAND_ECC_BCH)
			writel_relaxed(NDECCCTRL_BCH_EN, nfc->regs + NDECCCTRL);
	}
}

static void marvell_nfc_disable_hw_ecc(struct nand_chip *chip)
{
	struct marvell_nfc *nfc = to_marvell_nfc(chip->controller);
	u32 ndcr = readl_relaxed(nfc->regs + NDCR);

	if (ndcr & NDCR_ECC_EN) {
		writel_relaxed(ndcr & ~NDCR_ECC_EN, nfc->regs + NDCR);
		if (chip->ecc.algo == NAND_ECC_BCH)
			writel_relaxed(0, nfc->regs + NDECCCTRL);
	}
}

/* DMA related helpers */
static void marvell_nfc_enable_dma(struct marvell_nfc *nfc)
{
	u32 reg;

	reg = readl_relaxed(nfc->regs + NDCR);
	writel_relaxed(reg | NDCR_DMA_EN, nfc->regs + NDCR);
}

static void marvell_nfc_disable_dma(struct marvell_nfc *nfc)
{
	u32 reg;

	reg = readl_relaxed(nfc->regs + NDCR);
	writel_relaxed(reg & ~NDCR_DMA_EN, nfc->regs + NDCR);
}

/* Read/write PIO/DMA accessors */
static int marvell_nfc_xfer_data_dma(struct marvell_nfc *nfc,
				     enum dma_data_direction direction,
				     unsigned int len)
{
	unsigned int dma_len = min_t(int, ALIGN(len, 32), MAX_CHUNK_SIZE);
	struct dma_async_tx_descriptor *tx;
	struct scatterlist sg;
	dma_cookie_t cookie;
	int ret;

	marvell_nfc_enable_dma(nfc);
	/* Prepare the DMA transfer */
	sg_init_one(&sg, nfc->dma_buf, dma_len);
	dma_map_sg(nfc->dma_chan->device->dev, &sg, 1, direction);
	tx = dmaengine_prep_slave_sg(nfc->dma_chan, &sg, 1,
				     direction == DMA_FROM_DEVICE ?
				     DMA_DEV_TO_MEM : DMA_MEM_TO_DEV,
				     DMA_PREP_INTERRUPT);
	if (!tx) {
		dev_err(nfc->dev, "Could not prepare DMA S/G list\n");
		return -ENXIO;
	}

	/* Do the task and wait for it to finish */
	cookie = dmaengine_submit(tx);
	ret = dma_submit_error(cookie);
	if (ret)
		return -EIO;

	dma_async_issue_pending(nfc->dma_chan);
	ret = marvell_nfc_wait_cmdd(nfc->selected_chip);
	dma_unmap_sg(nfc->dma_chan->device->dev, &sg, 1, direction);
	marvell_nfc_disable_dma(nfc);
	if (ret) {
		dev_err(nfc->dev, "Timeout waiting for DMA (status: %d)\n",
			dmaengine_tx_status(nfc->dma_chan, cookie, NULL));
		dmaengine_terminate_all(nfc->dma_chan);
		return -ETIMEDOUT;
	}

	return 0;
}

static int marvell_nfc_xfer_data_in_pio(struct marvell_nfc *nfc, u8 *in,
					unsigned int len)
{
	unsigned int last_len = len % FIFO_DEPTH;
	unsigned int last_full_offset = round_down(len, FIFO_DEPTH);
	int i;

	for (i = 0; i < last_full_offset; i += FIFO_DEPTH)
		ioread32_rep(nfc->regs + NDDB, in + i, FIFO_REP(FIFO_DEPTH));

	if (last_len) {
		u8 tmp_buf[FIFO_DEPTH];

		ioread32_rep(nfc->regs + NDDB, tmp_buf, FIFO_REP(FIFO_DEPTH));
		memcpy(in + last_full_offset, tmp_buf, last_len);
	}

	return 0;
}

static int marvell_nfc_xfer_data_out_pio(struct marvell_nfc *nfc, const u8 *out,
					 unsigned int len)
{
	unsigned int last_len = len % FIFO_DEPTH;
	unsigned int last_full_offset = round_down(len, FIFO_DEPTH);
	int i;

	for (i = 0; i < last_full_offset; i += FIFO_DEPTH)
		iowrite32_rep(nfc->regs + NDDB, out + i, FIFO_REP(FIFO_DEPTH));

	if (last_len) {
		u8 tmp_buf[FIFO_DEPTH];

		memcpy(tmp_buf, out + last_full_offset, last_len);
		iowrite32_rep(nfc->regs + NDDB, tmp_buf, FIFO_REP(FIFO_DEPTH));
	}

	return 0;
}

static void marvell_nfc_check_empty_chunk(struct nand_chip *chip,
					  u8 *data, int data_len,
					  u8 *spare, int spare_len,
					  u8 *ecc, int ecc_len,
					  unsigned int *max_bitflips)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	int bf;

	/*
	 * Blank pages (all 0xFF) that have not been written may be recognized
	 * as bad if bitflips occur, so whenever an uncorrectable error occurs,
	 * check if the entire page (with ECC bytes) is actually blank or not.
	 */
	if (!data)
		data_len = 0;
	if (!spare)
		spare_len = 0;
	if (!ecc)
		ecc_len = 0;

	bf = nand_check_erased_ecc_chunk(data, data_len, ecc, ecc_len,
					 spare, spare_len, chip->ecc.strength);
	if (bf < 0) {
		mtd->ecc_stats.failed++;
		return;
	}

	/* Update the stats and max_bitflips */
	mtd->ecc_stats.corrected += bf;
	*max_bitflips = max_t(unsigned int, *max_bitflips, bf);
}

/*
 * Check if a chunk is correct or not according to the hardware ECC engine.
 * mtd->ecc_stats.corrected is updated, as well as max_bitflips, however
 * mtd->ecc_stats.failure is not, the function will instead return a non-zero
 * value indicating that a check on the emptyness of the subpage must be
 * performed before actually declaring the subpage as "corrupted".
 */
static int marvell_nfc_hw_ecc_check_bitflips(struct nand_chip *chip,
					     unsigned int *max_bitflips)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct marvell_nfc *nfc = to_marvell_nfc(chip->controller);
	int bf = 0;
	u32 ndsr;

	ndsr = readl_relaxed(nfc->regs + NDSR);

	/* Check uncorrectable error flag */
	if (ndsr & NDSR_UNCERR) {
		writel_relaxed(ndsr, nfc->regs + NDSR);

		/*
		 * Do not increment ->ecc_stats.failed now, instead, return a
		 * non-zero value to indicate that this chunk was apparently
		 * bad, and it should be check to see if it empty or not. If
		 * the chunk (with ECC bytes) is not declared empty, the calling
		 * function must increment the failure count.
		 */
		return -EBADMSG;
	}

	/* Check correctable error flag */
	if (ndsr & NDSR_CORERR) {
		writel_relaxed(ndsr, nfc->regs + NDSR);

		if (chip->ecc.algo == NAND_ECC_BCH)
			bf = NDSR_ERRCNT(ndsr);
		else
			bf = 1;
	}

	/* Update the stats and max_bitflips */
	mtd->ecc_stats.corrected += bf;
	*max_bitflips = max_t(unsigned int, *max_bitflips, bf);

	return 0;
}

/* Hamming read helpers */
static int marvell_nfc_hw_ecc_hmg_do_read_page(struct nand_chip *chip,
					       u8 *data_buf, u8 *oob_buf,
					       bool raw, int page)
{
	struct marvell_nand_chip *marvell_nand = to_marvell_nand(chip);
	struct marvell_nfc *nfc = to_marvell_nfc(chip->controller);
	const struct marvell_hw_ecc_layout *lt = to_marvell_nand(chip)->layout;
	struct marvell_nfc_op nfc_op = {
		.ndcb[0] = NDCB0_CMD_TYPE(TYPE_READ) |
			   NDCB0_ADDR_CYC(marvell_nand->addr_cyc) |
			   NDCB0_DBC |
			   NDCB0_CMD1(NAND_CMD_READ0) |
			   NDCB0_CMD2(NAND_CMD_READSTART),
		.ndcb[1] = NDCB1_ADDRS_PAGE(page),
		.ndcb[2] = NDCB2_ADDR5_PAGE(page),
	};
	unsigned int oob_bytes = lt->spare_bytes + (raw ? lt->ecc_bytes : 0);
	int ret;

	/* NFCv2 needs more information about the operation being executed */
	if (nfc->caps->is_nfcv2)
		nfc_op.ndcb[0] |= NDCB0_CMD_XTYPE(XTYPE_MONOLITHIC_RW);

	ret = marvell_nfc_prepare_cmd(chip);
	if (ret)
		return ret;

	marvell_nfc_send_cmd(chip, &nfc_op);
	ret = marvell_nfc_end_cmd(chip, NDSR_RDDREQ,
				  "RDDREQ while draining FIFO (data/oob)");
	if (ret)
		return ret;

	/*
	 * Read the page then the OOB area. Unlike what is shown in current
	 * documentation, spare bytes are protected by the ECC engine, and must
	 * be at the beginning of the OOB area or running this driver on legacy
	 * systems will prevent the discovery of the BBM/BBT.
	 */
	if (nfc->use_dma) {
		marvell_nfc_xfer_data_dma(nfc, DMA_FROM_DEVICE,
					  lt->data_bytes + oob_bytes);
		memcpy(data_buf, nfc->dma_buf, lt->data_bytes);
		memcpy(oob_buf, nfc->dma_buf + lt->data_bytes, oob_bytes);
	} else {
		marvell_nfc_xfer_data_in_pio(nfc, data_buf, lt->data_bytes);
		marvell_nfc_xfer_data_in_pio(nfc, oob_buf, oob_bytes);
	}

	ret = marvell_nfc_wait_cmdd(chip);
	return ret;
}

static int marvell_nfc_hw_ecc_hmg_read_page_raw(struct nand_chip *chip, u8 *buf,
						int oob_required, int page)
{
	marvell_nfc_select_target(chip, chip->cur_cs);
	return marvell_nfc_hw_ecc_hmg_do_read_page(chip, buf, chip->oob_poi,
						   true, page);
}

static int marvell_nfc_hw_ecc_hmg_read_page(struct nand_chip *chip, u8 *buf,
					    int oob_required, int page)
{
	const struct marvell_hw_ecc_layout *lt = to_marvell_nand(chip)->layout;
	unsigned int full_sz = lt->data_bytes + lt->spare_bytes + lt->ecc_bytes;
	int max_bitflips = 0, ret;
	u8 *raw_buf;

	marvell_nfc_select_target(chip, chip->cur_cs);
	marvell_nfc_enable_hw_ecc(chip);
	marvell_nfc_hw_ecc_hmg_do_read_page(chip, buf, chip->oob_poi, false,
					    page);
	ret = marvell_nfc_hw_ecc_check_bitflips(chip, &max_bitflips);
	marvell_nfc_disable_hw_ecc(chip);

	if (!ret)
		return max_bitflips;

	/*
	 * When ECC failures are detected, check if the full page has been
	 * written or not. Ignore the failure if it is actually empty.
	 */
	raw_buf = kmalloc(full_sz, GFP_KERNEL);
	if (!raw_buf)
		return -ENOMEM;

	marvell_nfc_hw_ecc_hmg_do_read_page(chip, raw_buf, raw_buf +
					    lt->data_bytes, true, page);
	marvell_nfc_check_empty_chunk(chip, raw_buf, full_sz, NULL, 0, NULL, 0,
				      &max_bitflips);
	kfree(raw_buf);

	return max_bitflips;
}

/*
 * Spare area in Hamming layouts is not protected by the ECC engine (even if
 * it appears before the ECC bytes when reading), the ->read_oob_raw() function
 * also stands for ->read_oob().
 */
static int marvell_nfc_hw_ecc_hmg_read_oob_raw(struct nand_chip *chip, int page)
{
	u8 *buf = nand_get_data_buf(chip);

	marvell_nfc_select_target(chip, chip->cur_cs);
	return marvell_nfc_hw_ecc_hmg_do_read_page(chip, buf, chip->oob_poi,
						   true, page);
}

/* Hamming write helpers */
static int marvell_nfc_hw_ecc_hmg_do_write_page(struct nand_chip *chip,
						const u8 *data_buf,
						const u8 *oob_buf, bool raw,
						int page)
{
	struct marvell_nand_chip *marvell_nand = to_marvell_nand(chip);
	struct marvell_nfc *nfc = to_marvell_nfc(chip->controller);
	const struct marvell_hw_ecc_layout *lt = to_marvell_nand(chip)->layout;
	struct marvell_nfc_op nfc_op = {
		.ndcb[0] = NDCB0_CMD_TYPE(TYPE_WRITE) |
			   NDCB0_ADDR_CYC(marvell_nand->addr_cyc) |
			   NDCB0_CMD1(NAND_CMD_SEQIN) |
			   NDCB0_CMD2(NAND_CMD_PAGEPROG) |
			   NDCB0_DBC,
		.ndcb[1] = NDCB1_ADDRS_PAGE(page),
		.ndcb[2] = NDCB2_ADDR5_PAGE(page),
	};
	unsigned int oob_bytes = lt->spare_bytes + (raw ? lt->ecc_bytes : 0);
	int ret;

	/* NFCv2 needs more information about the operation being executed */
	if (nfc->caps->is_nfcv2)
		nfc_op.ndcb[0] |= NDCB0_CMD_XTYPE(XTYPE_MONOLITHIC_RW);

	ret = marvell_nfc_prepare_cmd(chip);
	if (ret)
		return ret;

	marvell_nfc_send_cmd(chip, &nfc_op);
	ret = marvell_nfc_end_cmd(chip, NDSR_WRDREQ,
				  "WRDREQ while loading FIFO (data)");
	if (ret)
		return ret;

	/* Write the page then the OOB area */
	if (nfc->use_dma) {
		memcpy(nfc->dma_buf, data_buf, lt->data_bytes);
		memcpy(nfc->dma_buf + lt->data_bytes, oob_buf, oob_bytes);
		marvell_nfc_xfer_data_dma(nfc, DMA_TO_DEVICE, lt->data_bytes +
					  lt->ecc_bytes + lt->spare_bytes);
	} else {
		marvell_nfc_xfer_data_out_pio(nfc, data_buf, lt->data_bytes);
		marvell_nfc_xfer_data_out_pio(nfc, oob_buf, oob_bytes);
	}

	ret = marvell_nfc_wait_cmdd(chip);
	if (ret)
		return ret;

	ret = marvell_nfc_wait_op(chip,
				  PSEC_TO_MSEC(chip->data_interface.timings.sdr.tPROG_max));
	return ret;
}

static int marvell_nfc_hw_ecc_hmg_write_page_raw(struct nand_chip *chip,
						 const u8 *buf,
						 int oob_required, int page)
{
	marvell_nfc_select_target(chip, chip->cur_cs);
	return marvell_nfc_hw_ecc_hmg_do_write_page(chip, buf, chip->oob_poi,
						    true, page);
}

static int marvell_nfc_hw_ecc_hmg_write_page(struct nand_chip *chip,
					     const u8 *buf,
					     int oob_required, int page)
{
	int ret;

	marvell_nfc_select_target(chip, chip->cur_cs);
	marvell_nfc_enable_hw_ecc(chip);
	ret = marvell_nfc_hw_ecc_hmg_do_write_page(chip, buf, chip->oob_poi,
						   false, page);
	marvell_nfc_disable_hw_ecc(chip);

	return ret;
}

/*
 * Spare area in Hamming layouts is not protected by the ECC engine (even if
 * it appears before the ECC bytes when reading), the ->write_oob_raw() function
 * also stands for ->write_oob().
 */
static int marvell_nfc_hw_ecc_hmg_write_oob_raw(struct nand_chip *chip,
						int page)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	u8 *buf = nand_get_data_buf(chip);

	memset(buf, 0xFF, mtd->writesize);

	marvell_nfc_select_target(chip, chip->cur_cs);
	return marvell_nfc_hw_ecc_hmg_do_write_page(chip, buf, chip->oob_poi,
						    true, page);
}

/* BCH read helpers */
static int marvell_nfc_hw_ecc_bch_read_page_raw(struct nand_chip *chip, u8 *buf,
						int oob_required, int page)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	const struct marvell_hw_ecc_layout *lt = to_marvell_nand(chip)->layout;
	u8 *oob = chip->oob_poi;
	int chunk_size = lt->data_bytes + lt->spare_bytes + lt->ecc_bytes;
	int ecc_offset = (lt->full_chunk_cnt * lt->spare_bytes) +
		lt->last_spare_bytes;
	int data_len = lt->data_bytes;
	int spare_len = lt->spare_bytes;
	int ecc_len = lt->ecc_bytes;
	int chunk;

	marvell_nfc_select_target(chip, chip->cur_cs);

	if (oob_required)
		memset(chip->oob_poi, 0xFF, mtd->oobsize);

	nand_read_page_op(chip, page, 0, NULL, 0);

	for (chunk = 0; chunk < lt->nchunks; chunk++) {
		/* Update last chunk length */
		if (chunk >= lt->full_chunk_cnt) {
			data_len = lt->last_data_bytes;
			spare_len = lt->last_spare_bytes;
			ecc_len = lt->last_ecc_bytes;
		}

		/* Read data bytes*/
		nand_change_read_column_op(chip, chunk * chunk_size,
					   buf + (lt->data_bytes * chunk),
					   data_len, false);

		/* Read spare bytes */
		nand_read_data_op(chip, oob + (lt->spare_bytes * chunk),
				  spare_len, false, false);

		/* Read ECC bytes */
		nand_read_data_op(chip, oob + ecc_offset +
				  (ALIGN(lt->ecc_bytes, 32) * chunk),
				  ecc_len, false, false);
	}

	return 0;
}

static void marvell_nfc_hw_ecc_bch_read_chunk(struct nand_chip *chip, int chunk,
					      u8 *data, unsigned int data_len,
					      u8 *spare, unsigned int spare_len,
					      int page)
{
	struct marvell_nand_chip *marvell_nand = to_marvell_nand(chip);
	struct marvell_nfc *nfc = to_marvell_nfc(chip->controller);
	const struct marvell_hw_ecc_layout *lt = to_marvell_nand(chip)->layout;
	int i, ret;
	struct marvell_nfc_op nfc_op = {
		.ndcb[0] = NDCB0_CMD_TYPE(TYPE_READ) |
			   NDCB0_ADDR_CYC(marvell_nand->addr_cyc) |
			   NDCB0_LEN_OVRD,
		.ndcb[1] = NDCB1_ADDRS_PAGE(page),
		.ndcb[2] = NDCB2_ADDR5_PAGE(page),
		.ndcb[3] = data_len + spare_len,
	};

	ret = marvell_nfc_prepare_cmd(chip);
	if (ret)
		return;

	if (chunk == 0)
		nfc_op.ndcb[0] |= NDCB0_DBC |
				  NDCB0_CMD1(NAND_CMD_READ0) |
				  NDCB0_CMD2(NAND_CMD_READSTART);

	/*
	 * Trigger the monolithic read on the first chunk, then naked read on
	 * intermediate chunks and finally a last naked read on the last chunk.
	 */
	if (chunk == 0)
		nfc_op.ndcb[0] |= NDCB0_CMD_XTYPE(XTYPE_MONOLITHIC_RW);
	else if (chunk < lt->nchunks - 1)
		nfc_op.ndcb[0] |= NDCB0_CMD_XTYPE(XTYPE_NAKED_RW);
	else
		nfc_op.ndcb[0] |= NDCB0_CMD_XTYPE(XTYPE_LAST_NAKED_RW);

	marvell_nfc_send_cmd(chip, &nfc_op);

	/*
	 * According to the datasheet, when reading from NDDB
	 * with BCH enabled, after each 32 bytes reads, we
	 * have to make sure that the NDSR.RDDREQ bit is set.
	 *
	 * Drain the FIFO, 8 32-bit reads at a time, and skip
	 * the polling on the last read.
	 *
	 * Length is a multiple of 32 bytes, hence it is a multiple of 8 too.
	 */
	for (i = 0; i < data_len; i += FIFO_DEPTH * BCH_SEQ_READS) {
		marvell_nfc_end_cmd(chip, NDSR_RDDREQ,
				    "RDDREQ while draining FIFO (data)");
		marvell_nfc_xfer_data_in_pio(nfc, data,
					     FIFO_DEPTH * BCH_SEQ_READS);
		data += FIFO_DEPTH * BCH_SEQ_READS;
	}

	for (i = 0; i < spare_len; i += FIFO_DEPTH * BCH_SEQ_READS) {
		marvell_nfc_end_cmd(chip, NDSR_RDDREQ,
				    "RDDREQ while draining FIFO (OOB)");
		marvell_nfc_xfer_data_in_pio(nfc, spare,
					     FIFO_DEPTH * BCH_SEQ_READS);
		spare += FIFO_DEPTH * BCH_SEQ_READS;
	}
}

static int marvell_nfc_hw_ecc_bch_read_page(struct nand_chip *chip,
					    u8 *buf, int oob_required,
					    int page)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	const struct marvell_hw_ecc_layout *lt = to_marvell_nand(chip)->layout;
	int data_len = lt->data_bytes, spare_len = lt->spare_bytes;
	u8 *data = buf, *spare = chip->oob_poi;
	int max_bitflips = 0;
	u32 failure_mask = 0;
	int chunk, ret;

	marvell_nfc_select_target(chip, chip->cur_cs);

	/*
	 * With BCH, OOB is not fully used (and thus not read entirely), not
	 * expected bytes could show up at the end of the OOB buffer if not
	 * explicitly erased.
	 */
	if (oob_required)
		memset(chip->oob_poi, 0xFF, mtd->oobsize);

	marvell_nfc_enable_hw_ecc(chip);

	for (chunk = 0; chunk < lt->nchunks; chunk++) {
		/* Update length for the last chunk */
		if (chunk >= lt->full_chunk_cnt) {
			data_len = lt->last_data_bytes;
			spare_len = lt->last_spare_bytes;
		}

		/* Read the chunk and detect number of bitflips */
		marvell_nfc_hw_ecc_bch_read_chunk(chip, chunk, data, data_len,
						  spare, spare_len, page);
		ret = marvell_nfc_hw_ecc_check_bitflips(chip, &max_bitflips);
		if (ret)
			failure_mask |= BIT(chunk);

		data += data_len;
		spare += spare_len;
	}

	marvell_nfc_disable_hw_ecc(chip);

	if (!failure_mask)
		return max_bitflips;

	/*
	 * Please note that dumping the ECC bytes during a normal read with OOB
	 * area would add a significant overhead as ECC bytes are "consumed" by
	 * the controller in normal mode and must be re-read in raw mode. To
	 * avoid dropping the performances, we prefer not to include them. The
	 * user should re-read the page in raw mode if ECC bytes are required.
	 */

	/*
	 * In case there is any subpage read error, we usually re-read only ECC
	 * bytes in raw mode and check if the whole page is empty. In this case,
	 * it is normal that the ECC check failed and we just ignore the error.
	 *
	 * However, it has been empirically observed that for some layouts (e.g
	 * 2k page, 8b strength per 512B chunk), the controller tries to correct
	 * bits and may create itself bitflips in the erased area. To overcome
	 * this strange behavior, the whole page is re-read in raw mode, not
	 * only the ECC bytes.
	 */
	for (chunk = 0; chunk < lt->nchunks; chunk++) {
		int data_off_in_page, spare_off_in_page, ecc_off_in_page;
		int data_off, spare_off, ecc_off;
		int data_len, spare_len, ecc_len;

		/* No failure reported for this chunk, move to the next one */
		if (!(failure_mask & BIT(chunk)))
			continue;

		data_off_in_page = chunk * (lt->data_bytes + lt->spare_bytes +
					    lt->ecc_bytes);
		spare_off_in_page = data_off_in_page +
			(chunk < lt->full_chunk_cnt ? lt->data_bytes :
						      lt->last_data_bytes);
		ecc_off_in_page = spare_off_in_page +
			(chunk < lt->full_chunk_cnt ? lt->spare_bytes :
						      lt->last_spare_bytes);

		data_off = chunk * lt->data_bytes;
		spare_off = chunk * lt->spare_bytes;
		ecc_off = (lt->full_chunk_cnt * lt->spare_bytes) +
			  lt->last_spare_bytes +
			  (chunk * (lt->ecc_bytes + 2));

		data_len = chunk < lt->full_chunk_cnt ? lt->data_bytes :
							lt->last_data_bytes;
		spare_len = chunk < lt->full_chunk_cnt ? lt->spare_bytes :
							 lt->last_spare_bytes;
		ecc_len = chunk < lt->full_chunk_cnt ? lt->ecc_bytes :
						       lt->last_ecc_bytes;

		/*
		 * Only re-read the ECC bytes, unless we are using the 2k/8b
		 * layout which is buggy in the sense that the ECC engine will
		 * try to correct data bytes anyway, creating bitflips. In this
		 * case, re-read the entire page.
		 */
		if (lt->writesize == 2048 && lt->strength == 8) {
			nand_change_read_column_op(chip, data_off_in_page,
						   buf + data_off, data_len,
						   false);
			nand_change_read_column_op(chip, spare_off_in_page,
						   chip->oob_poi + spare_off, spare_len,
						   false);
		}

		nand_change_read_column_op(chip, ecc_off_in_page,
					   chip->oob_poi + ecc_off, ecc_len,
					   false);

		/* Check the entire chunk (data + spare + ecc) for emptyness */
		marvell_nfc_check_empty_chunk(chip, buf + data_off, data_len,
					      chip->oob_poi + spare_off, spare_len,
					      chip->oob_poi + ecc_off, ecc_len,
					      &max_bitflips);
	}

	return max_bitflips;
}

static int marvell_nfc_hw_ecc_bch_read_oob_raw(struct nand_chip *chip, int page)
{
	u8 *buf = nand_get_data_buf(chip);

	return chip->ecc.read_page_raw(chip, buf, true, page);
}

static int marvell_nfc_hw_ecc_bch_read_oob(struct nand_chip *chip, int page)
{
	u8 *buf = nand_get_data_buf(chip);

	return chip->ecc.read_page(chip, buf, true, page);
}

/* BCH write helpers */
static int marvell_nfc_hw_ecc_bch_write_page_raw(struct nand_chip *chip,
						 const u8 *buf,
						 int oob_required, int page)
{
	const struct marvell_hw_ecc_layout *lt = to_marvell_nand(chip)->layout;
	int full_chunk_size = lt->data_bytes + lt->spare_bytes + lt->ecc_bytes;
	int data_len = lt->data_bytes;
	int spare_len = lt->spare_bytes;
	int ecc_len = lt->ecc_bytes;
	int spare_offset = 0;
	int ecc_offset = (lt->full_chunk_cnt * lt->spare_bytes) +
		lt->last_spare_bytes;
	int chunk;

	marvell_nfc_select_target(chip, chip->cur_cs);

	nand_prog_page_begin_op(chip, page, 0, NULL, 0);

	for (chunk = 0; chunk < lt->nchunks; chunk++) {
		if (chunk >= lt->full_chunk_cnt) {
			data_len = lt->last_data_bytes;
			spare_len = lt->last_spare_bytes;
			ecc_len = lt->last_ecc_bytes;
		}

		/* Point to the column of the next chunk */
		nand_change_write_column_op(chip, chunk * full_chunk_size,
					    NULL, 0, false);

		/* Write the data */
		nand_write_data_op(chip, buf + (chunk * lt->data_bytes),
				   data_len, false);

		if (!oob_required)
			continue;

		/* Write the spare bytes */
		if (spare_len)
			nand_write_data_op(chip, chip->oob_poi + spare_offset,
					   spare_len, false);

		/* Write the ECC bytes */
		if (ecc_len)
			nand_write_data_op(chip, chip->oob_poi + ecc_offset,
					   ecc_len, false);

		spare_offset += spare_len;
		ecc_offset += ALIGN(ecc_len, 32);
	}

	return nand_prog_page_end_op(chip);
}

static int
marvell_nfc_hw_ecc_bch_write_chunk(struct nand_chip *chip, int chunk,
				   const u8 *data, unsigned int data_len,
				   const u8 *spare, unsigned int spare_len,
				   int page)
{
	struct marvell_nand_chip *marvell_nand = to_marvell_nand(chip);
	struct marvell_nfc *nfc = to_marvell_nfc(chip->controller);
	const struct marvell_hw_ecc_layout *lt = to_marvell_nand(chip)->layout;
	u32 xtype;
	int ret;
	struct marvell_nfc_op nfc_op = {
		.ndcb[0] = NDCB0_CMD_TYPE(TYPE_WRITE) | NDCB0_LEN_OVRD,
		.ndcb[3] = data_len + spare_len,
	};

	/*
	 * First operation dispatches the CMD_SEQIN command, issue the address
	 * cycles and asks for the first chunk of data.
	 * All operations in the middle (if any) will issue a naked write and
	 * also ask for data.
	 * Last operation (if any) asks for the last chunk of data through a
	 * last naked write.
	 */
	if (chunk == 0) {
		if (lt->nchunks == 1)
			xtype = XTYPE_MONOLITHIC_RW;
		else
			xtype = XTYPE_WRITE_DISPATCH;

		nfc_op.ndcb[0] |= NDCB0_CMD_XTYPE(xtype) |
				  NDCB0_ADDR_CYC(marvell_nand->addr_cyc) |
				  NDCB0_CMD1(NAND_CMD_SEQIN);
		nfc_op.ndcb[1] |= NDCB1_ADDRS_PAGE(page);
		nfc_op.ndcb[2] |= NDCB2_ADDR5_PAGE(page);
	} else if (chunk < lt->nchunks - 1) {
		nfc_op.ndcb[0] |= NDCB0_CMD_XTYPE(XTYPE_NAKED_RW);
	} else {
		nfc_op.ndcb[0] |= NDCB0_CMD_XTYPE(XTYPE_LAST_NAKED_RW);
	}

	/* Always dispatch the PAGEPROG command on the last chunk */
	if (chunk == lt->nchunks - 1)
		nfc_op.ndcb[0] |= NDCB0_CMD2(NAND_CMD_PAGEPROG) | NDCB0_DBC;

	ret = marvell_nfc_prepare_cmd(chip);
	if (ret)
		return ret;

	marvell_nfc_send_cmd(chip, &nfc_op);
	ret = marvell_nfc_end_cmd(chip, NDSR_WRDREQ,
				  "WRDREQ while loading FIFO (data)");
	if (ret)
		return ret;

	/* Transfer the contents */
	iowrite32_rep(nfc->regs + NDDB, data, FIFO_REP(data_len));
	iowrite32_rep(nfc->regs + NDDB, spare, FIFO_REP(spare_len));

	return 0;
}

static int marvell_nfc_hw_ecc_bch_write_page(struct nand_chip *chip,
					     const u8 *buf,
					     int oob_required, int page)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	const struct marvell_hw_ecc_layout *lt = to_marvell_nand(chip)->layout;
	const u8 *data = buf;
	const u8 *spare = chip->oob_poi;
	int data_len = lt->data_bytes;
	int spare_len = lt->spare_bytes;
	int chunk, ret;

	marvell_nfc_select_target(chip, chip->cur_cs);

	/* Spare data will be written anyway, so clear it to avoid garbage */
	if (!oob_required)
		memset(chip->oob_poi, 0xFF, mtd->oobsize);

	marvell_nfc_enable_hw_ecc(chip);

	for (chunk = 0; chunk < lt->nchunks; chunk++) {
		if (chunk >= lt->full_chunk_cnt) {
			data_len = lt->last_data_bytes;
			spare_len = lt->last_spare_bytes;
		}

		marvell_nfc_hw_ecc_bch_write_chunk(chip, chunk, data, data_len,
						   spare, spare_len, page);
		data += data_len;
		spare += spare_len;

		/*
		 * Waiting only for CMDD or PAGED is not enough, ECC are
		 * partially written. No flag is set once the operation is
		 * really finished but the ND_RUN bit is cleared, so wait for it
		 * before stepping into the next command.
		 */
		marvell_nfc_wait_ndrun(chip);
	}

	ret = marvell_nfc_wait_op(chip,
				  PSEC_TO_MSEC(chip->data_interface.timings.sdr.tPROG_max));

	marvell_nfc_disable_hw_ecc(chip);

	if (ret)
		return ret;

	return 0;
}

static int marvell_nfc_hw_ecc_bch_write_oob_raw(struct nand_chip *chip,
						int page)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	u8 *buf = nand_get_data_buf(chip);

	memset(buf, 0xFF, mtd->writesize);

	return chip->ecc.write_page_raw(chip, buf, true, page);
}

static int marvell_nfc_hw_ecc_bch_write_oob(struct nand_chip *chip, int page)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	u8 *buf = nand_get_data_buf(chip);

	memset(buf, 0xFF, mtd->writesize);

	return chip->ecc.write_page(chip, buf, true, page);
}

/* NAND framework ->exec_op() hooks and related helpers */
static void marvell_nfc_parse_instructions(struct nand_chip *chip,
					   const struct nand_subop *subop,
					   struct marvell_nfc_op *nfc_op)
{
	const struct nand_op_instr *instr = NULL;
	struct marvell_nfc *nfc = to_marvell_nfc(chip->controller);
	bool first_cmd = true;
	unsigned int op_id;
	int i;

	/* Reset the input structure as most of its fields will be OR'ed */
	memset(nfc_op, 0, sizeof(struct marvell_nfc_op));

	for (op_id = 0; op_id < subop->ninstrs; op_id++) {
		unsigned int offset, naddrs;
		const u8 *addrs;
		int len;

		instr = &subop->instrs[op_id];

		switch (instr->type) {
		case NAND_OP_CMD_INSTR:
			if (first_cmd)
				nfc_op->ndcb[0] |=
					NDCB0_CMD1(instr->ctx.cmd.opcode);
			else
				nfc_op->ndcb[0] |=
					NDCB0_CMD2(instr->ctx.cmd.opcode) |
					NDCB0_DBC;

			nfc_op->cle_ale_delay_ns = instr->delay_ns;
			first_cmd = false;
			break;

		case NAND_OP_ADDR_INSTR:
			offset = nand_subop_get_addr_start_off(subop, op_id);
			naddrs = nand_subop_get_num_addr_cyc(subop, op_id);
			addrs = &instr->ctx.addr.addrs[offset];

			nfc_op->ndcb[0] |= NDCB0_ADDR_CYC(naddrs);

			for (i = 0; i < min_t(unsigned int, 4, naddrs); i++)
				nfc_op->ndcb[1] |= addrs[i] << (8 * i);

			if (naddrs >= 5)
				nfc_op->ndcb[2] |= NDCB2_ADDR5_CYC(addrs[4]);
			if (naddrs >= 6)
				nfc_op->ndcb[3] |= NDCB3_ADDR6_CYC(addrs[5]);
			if (naddrs == 7)
				nfc_op->ndcb[3] |= NDCB3_ADDR7_CYC(addrs[6]);

			nfc_op->cle_ale_delay_ns = instr->delay_ns;
			break;

		case NAND_OP_DATA_IN_INSTR:
			nfc_op->data_instr = instr;
			nfc_op->data_instr_idx = op_id;
			nfc_op->ndcb[0] |= NDCB0_CMD_TYPE(TYPE_READ);
			if (nfc->caps->is_nfcv2) {
				nfc_op->ndcb[0] |=
					NDCB0_CMD_XTYPE(XTYPE_MONOLITHIC_RW) |
					NDCB0_LEN_OVRD;
				len = nand_subop_get_data_len(subop, op_id);
				nfc_op->ndcb[3] |= round_up(len, FIFO_DEPTH);
			}
			nfc_op->data_delay_ns = instr->delay_ns;
			break;

		case NAND_OP_DATA_OUT_INSTR:
			nfc_op->data_instr = instr;
			nfc_op->data_instr_idx = op_id;
			nfc_op->ndcb[0] |= NDCB0_CMD_TYPE(TYPE_WRITE);
			if (nfc->caps->is_nfcv2) {
				nfc_op->ndcb[0] |=
					NDCB0_CMD_XTYPE(XTYPE_MONOLITHIC_RW) |
					NDCB0_LEN_OVRD;
				len = nand_subop_get_data_len(subop, op_id);
				nfc_op->ndcb[3] |= round_up(len, FIFO_DEPTH);
			}
			nfc_op->data_delay_ns = instr->delay_ns;
			break;

		case NAND_OP_WAITRDY_INSTR:
			nfc_op->rdy_timeout_ms = instr->ctx.waitrdy.timeout_ms;
			nfc_op->rdy_delay_ns = instr->delay_ns;
			break;
		}
	}
}

static int marvell_nfc_xfer_data_pio(struct nand_chip *chip,
				     const struct nand_subop *subop,
				     struct marvell_nfc_op *nfc_op)
{
	struct marvell_nfc *nfc = to_marvell_nfc(chip->controller);
	const struct nand_op_instr *instr = nfc_op->data_instr;
	unsigned int op_id = nfc_op->data_instr_idx;
	unsigned int len = nand_subop_get_data_len(subop, op_id);
	unsigned int offset = nand_subop_get_data_start_off(subop, op_id);
	bool reading = (instr->type == NAND_OP_DATA_IN_INSTR);
	int ret;

	if (instr->ctx.data.force_8bit)
		marvell_nfc_force_byte_access(chip, true);

	if (reading) {
		u8 *in = instr->ctx.data.buf.in + offset;

		ret = marvell_nfc_xfer_data_in_pio(nfc, in, len);
	} else {
		const u8 *out = instr->ctx.data.buf.out + offset;

		ret = marvell_nfc_xfer_data_out_pio(nfc, out, len);
	}

	if (instr->ctx.data.force_8bit)
		marvell_nfc_force_byte_access(chip, false);

	return ret;
}

static int marvell_nfc_monolithic_access_exec(struct nand_chip *chip,
					      const struct nand_subop *subop)
{
	struct marvell_nfc_op nfc_op;
	bool reading;
	int ret;

	marvell_nfc_parse_instructions(chip, subop, &nfc_op);
	reading = (nfc_op.data_instr->type == NAND_OP_DATA_IN_INSTR);

	ret = marvell_nfc_prepare_cmd(chip);
	if (ret)
		return ret;

	marvell_nfc_send_cmd(chip, &nfc_op);
	ret = marvell_nfc_end_cmd(chip, NDSR_RDDREQ | NDSR_WRDREQ,
				  "RDDREQ/WRDREQ while draining raw data");
	if (ret)
		return ret;

	cond_delay(nfc_op.cle_ale_delay_ns);

	if (reading) {
		if (nfc_op.rdy_timeout_ms) {
			ret = marvell_nfc_wait_op(chip, nfc_op.rdy_timeout_ms);
			if (ret)
				return ret;
		}

		cond_delay(nfc_op.rdy_delay_ns);
	}

	marvell_nfc_xfer_data_pio(chip, subop, &nfc_op);
	ret = marvell_nfc_wait_cmdd(chip);
	if (ret)
		return ret;

	cond_delay(nfc_op.data_delay_ns);

	if (!reading) {
		if (nfc_op.rdy_timeout_ms) {
			ret = marvell_nfc_wait_op(chip, nfc_op.rdy_timeout_ms);
			if (ret)
				return ret;
		}

		cond_delay(nfc_op.rdy_delay_ns);
	}

	/*
	 * NDCR ND_RUN bit should be cleared automatically at the end of each
	 * operation but experience shows that the behavior is buggy when it
	 * comes to writes (with LEN_OVRD). Clear it by hand in this case.
	 */
	if (!reading) {
		struct marvell_nfc *nfc = to_marvell_nfc(chip->controller);

		writel_relaxed(readl(nfc->regs + NDCR) & ~NDCR_ND_RUN,
			       nfc->regs + NDCR);
	}

	return 0;
}

static int marvell_nfc_naked_access_exec(struct nand_chip *chip,
					 const struct nand_subop *subop)
{
	struct marvell_nfc_op nfc_op;
	int ret;

	marvell_nfc_parse_instructions(chip, subop, &nfc_op);

	/*
	 * Naked access are different in that they need to be flagged as naked
	 * by the controller. Reset the controller registers fields that inform
	 * on the type and refill them according to the ongoing operation.
	 */
	nfc_op.ndcb[0] &= ~(NDCB0_CMD_TYPE(TYPE_MASK) |
			    NDCB0_CMD_XTYPE(XTYPE_MASK));
	switch (subop->instrs[0].type) {
	case NAND_OP_CMD_INSTR:
		nfc_op.ndcb[0] |= NDCB0_CMD_TYPE(TYPE_NAKED_CMD);
		break;
	case NAND_OP_ADDR_INSTR:
		nfc_op.ndcb[0] |= NDCB0_CMD_TYPE(TYPE_NAKED_ADDR);
		break;
	case NAND_OP_DATA_IN_INSTR:
		nfc_op.ndcb[0] |= NDCB0_CMD_TYPE(TYPE_READ) |
				  NDCB0_CMD_XTYPE(XTYPE_LAST_NAKED_RW);
		break;
	case NAND_OP_DATA_OUT_INSTR:
		nfc_op.ndcb[0] |= NDCB0_CMD_TYPE(TYPE_WRITE) |
				  NDCB0_CMD_XTYPE(XTYPE_LAST_NAKED_RW);
		break;
	default:
		/* This should never happen */
		break;
	}

	ret = marvell_nfc_prepare_cmd(chip);
	if (ret)
		return ret;

	marvell_nfc_send_cmd(chip, &nfc_op);

	if (!nfc_op.data_instr) {
		ret = marvell_nfc_wait_cmdd(chip);
		cond_delay(nfc_op.cle_ale_delay_ns);
		return ret;
	}

	ret = marvell_nfc_end_cmd(chip, NDSR_RDDREQ | NDSR_WRDREQ,
				  "RDDREQ/WRDREQ while draining raw data");
	if (ret)
		return ret;

	marvell_nfc_xfer_data_pio(chip, subop, &nfc_op);
	ret = marvell_nfc_wait_cmdd(chip);
	if (ret)
		return ret;

	/*
	 * NDCR ND_RUN bit should be cleared automatically at the end of each
	 * operation but experience shows that the behavior is buggy when it
	 * comes to writes (with LEN_OVRD). Clear it by hand in this case.
	 */
	if (subop->instrs[0].type == NAND_OP_DATA_OUT_INSTR) {
		struct marvell_nfc *nfc = to_marvell_nfc(chip->controller);

		writel_relaxed(readl(nfc->regs + NDCR) & ~NDCR_ND_RUN,
			       nfc->regs + NDCR);
	}

	return 0;
}

static int marvell_nfc_naked_waitrdy_exec(struct nand_chip *chip,
					  const struct nand_subop *subop)
{
	struct marvell_nfc_op nfc_op;
	int ret;

	marvell_nfc_parse_instructions(chip, subop, &nfc_op);

	ret = marvell_nfc_wait_op(chip, nfc_op.rdy_timeout_ms);
	cond_delay(nfc_op.rdy_delay_ns);

	return ret;
}

static int marvell_nfc_read_id_type_exec(struct nand_chip *chip,
					 const struct nand_subop *subop)
{
	struct marvell_nfc_op nfc_op;
	int ret;

	marvell_nfc_parse_instructions(chip, subop, &nfc_op);
	nfc_op.ndcb[0] &= ~NDCB0_CMD_TYPE(TYPE_READ);
	nfc_op.ndcb[0] |= NDCB0_CMD_TYPE(TYPE_READ_ID);

	ret = marvell_nfc_prepare_cmd(chip);
	if (ret)
		return ret;

	marvell_nfc_send_cmd(chip, &nfc_op);
	ret = marvell_nfc_end_cmd(chip, NDSR_RDDREQ,
				  "RDDREQ while reading ID");
	if (ret)
		return ret;

	cond_delay(nfc_op.cle_ale_delay_ns);

	if (nfc_op.rdy_timeout_ms) {
		ret = marvell_nfc_wait_op(chip, nfc_op.rdy_timeout_ms);
		if (ret)
			return ret;
	}

	cond_delay(nfc_op.rdy_delay_ns);

	marvell_nfc_xfer_data_pio(chip, subop, &nfc_op);
	ret = marvell_nfc_wait_cmdd(chip);
	if (ret)
		return ret;

	cond_delay(nfc_op.data_delay_ns);

	return 0;
}

static int marvell_nfc_read_status_exec(struct nand_chip *chip,
					const struct nand_subop *subop)
{
	struct marvell_nfc_op nfc_op;
	int ret;

	marvell_nfc_parse_instructions(chip, subop, &nfc_op);
	nfc_op.ndcb[0] &= ~NDCB0_CMD_TYPE(TYPE_READ);
	nfc_op.ndcb[0] |= NDCB0_CMD_TYPE(TYPE_STATUS);

	ret = marvell_nfc_prepare_cmd(chip);
	if (ret)
		return ret;

	marvell_nfc_send_cmd(chip, &nfc_op);
	ret = marvell_nfc_end_cmd(chip, NDSR_RDDREQ,
				  "RDDREQ while reading status");
	if (ret)
		return ret;

	cond_delay(nfc_op.cle_ale_delay_ns);

	if (nfc_op.rdy_timeout_ms) {
		ret = marvell_nfc_wait_op(chip, nfc_op.rdy_timeout_ms);
		if (ret)
			return ret;
	}

	cond_delay(nfc_op.rdy_delay_ns);

	marvell_nfc_xfer_data_pio(chip, subop, &nfc_op);
	ret = marvell_nfc_wait_cmdd(chip);
	if (ret)
		return ret;

	cond_delay(nfc_op.data_delay_ns);

	return 0;
}

static int marvell_nfc_reset_cmd_type_exec(struct nand_chip *chip,
					   const struct nand_subop *subop)
{
	struct marvell_nfc_op nfc_op;
	int ret;

	marvell_nfc_parse_instructions(chip, subop, &nfc_op);
	nfc_op.ndcb[0] |= NDCB0_CMD_TYPE(TYPE_RESET);

	ret = marvell_nfc_prepare_cmd(chip);
	if (ret)
		return ret;

	marvell_nfc_send_cmd(chip, &nfc_op);
	ret = marvell_nfc_wait_cmdd(chip);
	if (ret)
		return ret;

	cond_delay(nfc_op.cle_ale_delay_ns);

	ret = marvell_nfc_wait_op(chip, nfc_op.rdy_timeout_ms);
	if (ret)
		return ret;

	cond_delay(nfc_op.rdy_delay_ns);

	return 0;
}

static int marvell_nfc_erase_cmd_type_exec(struct nand_chip *chip,
					   const struct nand_subop *subop)
{
	struct marvell_nfc_op nfc_op;
	int ret;

	marvell_nfc_parse_instructions(chip, subop, &nfc_op);
	nfc_op.ndcb[0] |= NDCB0_CMD_TYPE(TYPE_ERASE);

	ret = marvell_nfc_prepare_cmd(chip);
	if (ret)
		return ret;

	marvell_nfc_send_cmd(chip, &nfc_op);
	ret = marvell_nfc_wait_cmdd(chip);
	if (ret)
		return ret;

	cond_delay(nfc_op.cle_ale_delay_ns);

	ret = marvell_nfc_wait_op(chip, nfc_op.rdy_timeout_ms);
	if (ret)
		return ret;

	cond_delay(nfc_op.rdy_delay_ns);

	return 0;
}

static const struct nand_op_parser marvell_nfcv2_op_parser = NAND_OP_PARSER(
	/* Monolithic reads/writes */
	NAND_OP_PARSER_PATTERN(
		marvell_nfc_monolithic_access_exec,
		NAND_OP_PARSER_PAT_CMD_ELEM(false),
		NAND_OP_PARSER_PAT_ADDR_ELEM(true, MAX_ADDRESS_CYC_NFCV2),
		NAND_OP_PARSER_PAT_CMD_ELEM(true),
		NAND_OP_PARSER_PAT_WAITRDY_ELEM(true),
		NAND_OP_PARSER_PAT_DATA_IN_ELEM(false, MAX_CHUNK_SIZE)),
	NAND_OP_PARSER_PATTERN(
		marvell_nfc_monolithic_access_exec,
		NAND_OP_PARSER_PAT_CMD_ELEM(false),
		NAND_OP_PARSER_PAT_ADDR_ELEM(false, MAX_ADDRESS_CYC_NFCV2),
		NAND_OP_PARSER_PAT_DATA_OUT_ELEM(false, MAX_CHUNK_SIZE),
		NAND_OP_PARSER_PAT_CMD_ELEM(true),
		NAND_OP_PARSER_PAT_WAITRDY_ELEM(true)),
	/* Naked commands */
	NAND_OP_PARSER_PATTERN(
		marvell_nfc_naked_access_exec,
		NAND_OP_PARSER_PAT_CMD_ELEM(false)),
	NAND_OP_PARSER_PATTERN(
		marvell_nfc_naked_access_exec,
		NAND_OP_PARSER_PAT_ADDR_ELEM(false, MAX_ADDRESS_CYC_NFCV2)),
	NAND_OP_PARSER_PATTERN(
		marvell_nfc_naked_access_exec,
		NAND_OP_PARSER_PAT_DATA_IN_ELEM(false, MAX_CHUNK_SIZE)),
	NAND_OP_PARSER_PATTERN(
		marvell_nfc_naked_access_exec,
		NAND_OP_PARSER_PAT_DATA_OUT_ELEM(false, MAX_CHUNK_SIZE)),
	NAND_OP_PARSER_PATTERN(
		marvell_nfc_naked_waitrdy_exec,
		NAND_OP_PARSER_PAT_WAITRDY_ELEM(false)),
	);

static const struct nand_op_parser marvell_nfcv1_op_parser = NAND_OP_PARSER(
	/* Naked commands not supported, use a function for each pattern */
	NAND_OP_PARSER_PATTERN(
		marvell_nfc_read_id_type_exec,
		NAND_OP_PARSER_PAT_CMD_ELEM(false),
		NAND_OP_PARSER_PAT_ADDR_ELEM(false, MAX_ADDRESS_CYC_NFCV1),
		NAND_OP_PARSER_PAT_DATA_IN_ELEM(false, 8)),
	NAND_OP_PARSER_PATTERN(
		marvell_nfc_erase_cmd_type_exec,
		NAND_OP_PARSER_PAT_CMD_ELEM(false),
		NAND_OP_PARSER_PAT_ADDR_ELEM(false, MAX_ADDRESS_CYC_NFCV1),
		NAND_OP_PARSER_PAT_CMD_ELEM(false),
		NAND_OP_PARSER_PAT_WAITRDY_ELEM(false)),
	NAND_OP_PARSER_PATTERN(
		marvell_nfc_read_status_exec,
		NAND_OP_PARSER_PAT_CMD_ELEM(false),
		NAND_OP_PARSER_PAT_DATA_IN_ELEM(false, 1)),
	NAND_OP_PARSER_PATTERN(
		marvell_nfc_reset_cmd_type_exec,
		NAND_OP_PARSER_PAT_CMD_ELEM(false),
		NAND_OP_PARSER_PAT_WAITRDY_ELEM(false)),
	NAND_OP_PARSER_PATTERN(
		marvell_nfc_naked_waitrdy_exec,
		NAND_OP_PARSER_PAT_WAITRDY_ELEM(false)),
	);

static int marvell_nfc_exec_op(struct nand_chip *chip,
			       const struct nand_operation *op,
			       bool check_only)
{
	struct marvell_nfc *nfc = to_marvell_nfc(chip->controller);

	if (!check_only)
		marvell_nfc_select_target(chip, op->cs);

	if (nfc->caps->is_nfcv2)
		return nand_op_parser_exec_op(chip, &marvell_nfcv2_op_parser,
					      op, check_only);
	else
		return nand_op_parser_exec_op(chip, &marvell_nfcv1_op_parser,
					      op, check_only);
}

/*
 * Layouts were broken in old pxa3xx_nand driver, these are supposed to be
 * usable.
 */
static int marvell_nand_ooblayout_ecc(struct mtd_info *mtd, int section,
				      struct mtd_oob_region *oobregion)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	const struct marvell_hw_ecc_layout *lt = to_marvell_nand(chip)->layout;

	if (section)
		return -ERANGE;

	oobregion->length = (lt->full_chunk_cnt * lt->ecc_bytes) +
			    lt->last_ecc_bytes;
	oobregion->offset = mtd->oobsize - oobregion->length;

	return 0;
}

static int marvell_nand_ooblayout_free(struct mtd_info *mtd, int section,
				       struct mtd_oob_region *oobregion)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	const struct marvell_hw_ecc_layout *lt = to_marvell_nand(chip)->layout;

	if (section)
		return -ERANGE;

	/*
	 * Bootrom looks in bytes 0 & 5 for bad blocks for the
	 * 4KB page / 4bit BCH combination.
	 */
	if (mtd->writesize == SZ_4K && lt->data_bytes == SZ_2K)
		oobregion->offset = 6;
	else
		oobregion->offset = 2;

	oobregion->length = (lt->full_chunk_cnt * lt->spare_bytes) +
			    lt->last_spare_bytes - oobregion->offset;

	return 0;
}

static const struct mtd_ooblayout_ops marvell_nand_ooblayout_ops = {
	.ecc = marvell_nand_ooblayout_ecc,
	.free = marvell_nand_ooblayout_free,
};

static int marvell_nand_hw_ecc_controller_init(struct mtd_info *mtd,
					       struct nand_ecc_ctrl *ecc)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct marvell_nfc *nfc = to_marvell_nfc(chip->controller);
	const struct marvell_hw_ecc_layout *l;
	int i;

	if (!nfc->caps->is_nfcv2 &&
	    (mtd->writesize + mtd->oobsize > MAX_CHUNK_SIZE)) {
		dev_err(nfc->dev,
			"NFCv1: writesize (%d) cannot be bigger than a chunk (%d)\n",
			mtd->writesize, MAX_CHUNK_SIZE - mtd->oobsize);
		return -ENOTSUPP;
	}

	to_marvell_nand(chip)->layout = NULL;
	for (i = 0; i < ARRAY_SIZE(marvell_nfc_layouts); i++) {
		l = &marvell_nfc_layouts[i];
		if (mtd->writesize == l->writesize &&
		    ecc->size == l->chunk && ecc->strength == l->strength) {
			to_marvell_nand(chip)->layout = l;
			break;
		}
	}

	if (!to_marvell_nand(chip)->layout ||
	    (!nfc->caps->is_nfcv2 && ecc->strength > 1)) {
		dev_err(nfc->dev,
			"ECC strength %d at page size %d is not supported\n",
			ecc->strength, mtd->writesize);
		return -ENOTSUPP;
	}

	/* Special care for the layout 2k/8-bit/512B  */
	if (l->writesize == 2048 && l->strength == 8) {
		if (mtd->oobsize < 128) {
			dev_err(nfc->dev, "Requested layout needs at least 128 OOB bytes\n");
			return -ENOTSUPP;
		} else {
			chip->bbt_options |= NAND_BBT_NO_OOB_BBM;
		}
	}

	mtd_set_ooblayout(mtd, &marvell_nand_ooblayout_ops);
	ecc->steps = l->nchunks;
	ecc->size = l->data_bytes;

	if (ecc->strength == 1) {
		chip->ecc.algo = NAND_ECC_HAMMING;
		ecc->read_page_raw = marvell_nfc_hw_ecc_hmg_read_page_raw;
		ecc->read_page = marvell_nfc_hw_ecc_hmg_read_page;
		ecc->read_oob_raw = marvell_nfc_hw_ecc_hmg_read_oob_raw;
		ecc->read_oob = ecc->read_oob_raw;
		ecc->write_page_raw = marvell_nfc_hw_ecc_hmg_write_page_raw;
		ecc->write_page = marvell_nfc_hw_ecc_hmg_write_page;
		ecc->write_oob_raw = marvell_nfc_hw_ecc_hmg_write_oob_raw;
		ecc->write_oob = ecc->write_oob_raw;
	} else {
		chip->ecc.algo = NAND_ECC_BCH;
		ecc->strength = 16;
		ecc->read_page_raw = marvell_nfc_hw_ecc_bch_read_page_raw;
		ecc->read_page = marvell_nfc_hw_ecc_bch_read_page;
		ecc->read_oob_raw = marvell_nfc_hw_ecc_bch_read_oob_raw;
		ecc->read_oob = marvell_nfc_hw_ecc_bch_read_oob;
		ecc->write_page_raw = marvell_nfc_hw_ecc_bch_write_page_raw;
		ecc->write_page = marvell_nfc_hw_ecc_bch_write_page;
		ecc->write_oob_raw = marvell_nfc_hw_ecc_bch_write_oob_raw;
		ecc->write_oob = marvell_nfc_hw_ecc_bch_write_oob;
	}

	return 0;
}

static int marvell_nand_ecc_init(struct mtd_info *mtd,
				 struct nand_ecc_ctrl *ecc)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct marvell_nfc *nfc = to_marvell_nfc(chip->controller);
	int ret;

	if (ecc->mode != NAND_ECC_NONE && (!ecc->size || !ecc->strength)) {
		if (chip->base.eccreq.step_size && chip->base.eccreq.strength) {
			ecc->size = chip->base.eccreq.step_size;
			ecc->strength = chip->base.eccreq.strength;
		} else {
			dev_info(nfc->dev,
				 "No minimum ECC strength, using 1b/512B\n");
			ecc->size = 512;
			ecc->strength = 1;
		}
	}

	switch (ecc->mode) {
	case NAND_ECC_HW:
		ret = marvell_nand_hw_ecc_controller_init(mtd, ecc);
		if (ret)
			return ret;
		break;
	case NAND_ECC_NONE:
	case NAND_ECC_SOFT:
	case NAND_ECC_ON_DIE:
		if (!nfc->caps->is_nfcv2 && mtd->writesize != SZ_512 &&
		    mtd->writesize != SZ_2K) {
			dev_err(nfc->dev, "NFCv1 cannot write %d bytes pages\n",
				mtd->writesize);
			return -EINVAL;
		}
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static u8 bbt_pattern[] = {'M', 'V', 'B', 'b', 't', '0' };
static u8 bbt_mirror_pattern[] = {'1', 't', 'b', 'B', 'V', 'M' };

static struct nand_bbt_descr bbt_main_descr = {
	.options = NAND_BBT_LASTBLOCK | NAND_BBT_CREATE | NAND_BBT_WRITE |
		   NAND_BBT_2BIT | NAND_BBT_VERSION,
	.offs =	8,
	.len = 6,
	.veroffs = 14,
	.maxblocks = 8,	/* Last 8 blocks in each chip */
	.pattern = bbt_pattern
};

static struct nand_bbt_descr bbt_mirror_descr = {
	.options = NAND_BBT_LASTBLOCK | NAND_BBT_CREATE | NAND_BBT_WRITE |
		   NAND_BBT_2BIT | NAND_BBT_VERSION,
	.offs =	8,
	.len = 6,
	.veroffs = 14,
	.maxblocks = 8,	/* Last 8 blocks in each chip */
	.pattern = bbt_mirror_pattern
};

static int marvell_nfc_setup_data_interface(struct nand_chip *chip, int chipnr,
					    const struct nand_data_interface
					    *conf)
{
	struct marvell_nand_chip *marvell_nand = to_marvell_nand(chip);
	struct marvell_nfc *nfc = to_marvell_nfc(chip->controller);
	unsigned int period_ns = 1000000000 / clk_get_rate(nfc->core_clk) * 2;
	const struct nand_sdr_timings *sdr;
	struct marvell_nfc_timings nfc_tmg;
	int read_delay;

	sdr = nand_get_sdr_timings(conf);
	if (IS_ERR(sdr))
		return PTR_ERR(sdr);

	/*
	 * SDR timings are given in pico-seconds while NFC timings must be
	 * expressed in NAND controller clock cycles, which is half of the
	 * frequency of the accessible ECC clock retrieved by clk_get_rate().
	 * This is not written anywhere in the datasheet but was observed
	 * with an oscilloscope.
	 *
	 * NFC datasheet gives equations from which thoses calculations
	 * are derived, they tend to be slightly more restrictives than the
	 * given core timings and may improve the overall speed.
	 */
	nfc_tmg.tRP = TO_CYCLES(DIV_ROUND_UP(sdr->tRC_min, 2), period_ns) - 1;
	nfc_tmg.tRH = nfc_tmg.tRP;
	nfc_tmg.tWP = TO_CYCLES(DIV_ROUND_UP(sdr->tWC_min, 2), period_ns) - 1;
	nfc_tmg.tWH = nfc_tmg.tWP;
	nfc_tmg.tCS = TO_CYCLES(sdr->tCS_min, period_ns);
	nfc_tmg.tCH = TO_CYCLES(sdr->tCH_min, period_ns) - 1;
	nfc_tmg.tADL = TO_CYCLES(sdr->tADL_min, period_ns);
	/*
	 * Read delay is the time of propagation from SoC pins to NFC internal
	 * logic. With non-EDO timings, this is MIN_RD_DEL_CNT clock cycles. In
	 * EDO mode, an additional delay of tRH must be taken into account so
	 * the data is sampled on the falling edge instead of the rising edge.
	 */
	read_delay = sdr->tRC_min >= 30000 ?
		MIN_RD_DEL_CNT : MIN_RD_DEL_CNT + nfc_tmg.tRH;

	nfc_tmg.tAR = TO_CYCLES(sdr->tAR_min, period_ns);
	/*
	 * tWHR and tRHW are supposed to be read to write delays (and vice
	 * versa) but in some cases, ie. when doing a change column, they must
	 * be greater than that to be sure tCCS delay is respected.
	 */
	nfc_tmg.tWHR = TO_CYCLES(max_t(int, sdr->tWHR_min, sdr->tCCS_min),
				 period_ns) - 2,
	nfc_tmg.tRHW = TO_CYCLES(max_t(int, sdr->tRHW_min, sdr->tCCS_min),
				 period_ns);

	/*
	 * NFCv2: Use WAIT_MODE (wait for RB line), do not rely only on delays.
	 * NFCv1: No WAIT_MODE, tR must be maximal.
	 */
	if (nfc->caps->is_nfcv2) {
		nfc_tmg.tR = TO_CYCLES(sdr->tWB_max, period_ns);
	} else {
		nfc_tmg.tR = TO_CYCLES64(sdr->tWB_max + sdr->tR_max,
					 period_ns);
		if (nfc_tmg.tR + 3 > nfc_tmg.tCH)
			nfc_tmg.tR = nfc_tmg.tCH - 3;
		else
			nfc_tmg.tR = 0;
	}

	if (chipnr < 0)
		return 0;

	marvell_nand->ndtr0 =
		NDTR0_TRP(nfc_tmg.tRP) |
		NDTR0_TRH(nfc_tmg.tRH) |
		NDTR0_ETRP(nfc_tmg.tRP) |
		NDTR0_TWP(nfc_tmg.tWP) |
		NDTR0_TWH(nfc_tmg.tWH) |
		NDTR0_TCS(nfc_tmg.tCS) |
		NDTR0_TCH(nfc_tmg.tCH);

	marvell_nand->ndtr1 =
		NDTR1_TAR(nfc_tmg.tAR) |
		NDTR1_TWHR(nfc_tmg.tWHR) |
		NDTR1_TR(nfc_tmg.tR);

	if (nfc->caps->is_nfcv2) {
		marvell_nand->ndtr0 |=
			NDTR0_RD_CNT_DEL(read_delay) |
			NDTR0_SELCNTR |
			NDTR0_TADL(nfc_tmg.tADL);

		marvell_nand->ndtr1 |=
			NDTR1_TRHW(nfc_tmg.tRHW) |
			NDTR1_WAIT_MODE;
	}

	return 0;
}

static int marvell_nand_attach_chip(struct nand_chip *chip)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct marvell_nand_chip *marvell_nand = to_marvell_nand(chip);
	struct marvell_nfc *nfc = to_marvell_nfc(chip->controller);
	struct pxa3xx_nand_platform_data *pdata = dev_get_platdata(nfc->dev);
	int ret;

	if (pdata && pdata->flash_bbt)
		chip->bbt_options |= NAND_BBT_USE_FLASH;

	if (chip->bbt_options & NAND_BBT_USE_FLASH) {
		/*
		 * We'll use a bad block table stored in-flash and don't
		 * allow writing the bad block marker to the flash.
		 */
		chip->bbt_options |= NAND_BBT_NO_OOB_BBM;
		chip->bbt_td = &bbt_main_descr;
		chip->bbt_md = &bbt_mirror_descr;
	}

	/* Save the chip-specific fields of NDCR */
	marvell_nand->ndcr = NDCR_PAGE_SZ(mtd->writesize);
	if (chip->options & NAND_BUSWIDTH_16)
		marvell_nand->ndcr |= NDCR_DWIDTH_M | NDCR_DWIDTH_C;

	/*
	 * On small page NANDs, only one cycle is needed to pass the
	 * column address.
	 */
	if (mtd->writesize <= 512) {
		marvell_nand->addr_cyc = 1;
	} else {
		marvell_nand->addr_cyc = 2;
		marvell_nand->ndcr |= NDCR_RA_START;
	}

	/*
	 * Now add the number of cycles needed to pass the row
	 * address.
	 *
	 * Addressing a chip using CS 2 or 3 should also need the third row
	 * cycle but due to inconsistance in the documentation and lack of
	 * hardware to test this situation, this case is not supported.
	 */
	if (chip->options & NAND_ROW_ADDR_3)
		marvell_nand->addr_cyc += 3;
	else
		marvell_nand->addr_cyc += 2;

	if (pdata) {
		chip->ecc.size = pdata->ecc_step_size;
		chip->ecc.strength = pdata->ecc_strength;
	}

	ret = marvell_nand_ecc_init(mtd, &chip->ecc);
	if (ret) {
		dev_err(nfc->dev, "ECC init failed: %d\n", ret);
		return ret;
	}

	if (chip->ecc.mode == NAND_ECC_HW) {
		/*
		 * Subpage write not available with hardware ECC, prohibit also
		 * subpage read as in userspace subpage access would still be
		 * allowed and subpage write, if used, would lead to numerous
		 * uncorrectable ECC errors.
		 */
		chip->options |= NAND_NO_SUBPAGE_WRITE;
	}

	if (pdata || nfc->caps->legacy_of_bindings) {
		/*
		 * We keep the MTD name unchanged to avoid breaking platforms
		 * where the MTD cmdline parser is used and the bootloader
		 * has not been updated to use the new naming scheme.
		 */
		mtd->name = "pxa3xx_nand-0";
	} else if (!mtd->name) {
		/*
		 * If the new bindings are used and the bootloader has not been
		 * updated to pass a new mtdparts parameter on the cmdline, you
		 * should define the following property in your NAND node, ie:
		 *
		 *	label = "main-storage";
		 *
		 * This way, mtd->name will be set by the core when
		 * nand_set_flash_node() is called.
		 */
		mtd->name = devm_kasprintf(nfc->dev, GFP_KERNEL,
					   "%s:nand.%d", dev_name(nfc->dev),
					   marvell_nand->sels[0].cs);
		if (!mtd->name) {
			dev_err(nfc->dev, "Failed to allocate mtd->name\n");
			return -ENOMEM;
		}
	}

	return 0;
}

static const struct nand_controller_ops marvell_nand_controller_ops = {
	.attach_chip = marvell_nand_attach_chip,
	.exec_op = marvell_nfc_exec_op,
	.setup_data_interface = marvell_nfc_setup_data_interface,
};

static int marvell_nand_chip_init(struct device *dev, struct marvell_nfc *nfc,
				  struct device_node *np)
{
	struct pxa3xx_nand_platform_data *pdata = dev_get_platdata(dev);
	struct marvell_nand_chip *marvell_nand;
	struct mtd_info *mtd;
	struct nand_chip *chip;
	int nsels, ret, i;
	u32 cs, rb;

	/*
	 * The legacy "num-cs" property indicates the number of CS on the only
	 * chip connected to the controller (legacy bindings does not support
	 * more than one chip). The CS and RB pins are always the #0.
	 *
	 * When not using legacy bindings, a couple of "reg" and "nand-rb"
	 * properties must be filled. For each chip, expressed as a subnode,
	 * "reg" points to the CS lines and "nand-rb" to the RB line.
	 */
	if (pdata || nfc->caps->legacy_of_bindings) {
		nsels = 1;
	} else {
		nsels = of_property_count_elems_of_size(np, "reg", sizeof(u32));
		if (nsels <= 0) {
			dev_err(dev, "missing/invalid reg property\n");
			return -EINVAL;
		}
	}

	/* Alloc the nand chip structure */
	marvell_nand = devm_kzalloc(dev,
				    struct_size(marvell_nand, sels, nsels),
				    GFP_KERNEL);
	if (!marvell_nand) {
		dev_err(dev, "could not allocate chip structure\n");
		return -ENOMEM;
	}

	marvell_nand->nsels = nsels;
	marvell_nand->selected_die = -1;

	for (i = 0; i < nsels; i++) {
		if (pdata || nfc->caps->legacy_of_bindings) {
			/*
			 * Legacy bindings use the CS lines in natural
			 * order (0, 1, ...)
			 */
			cs = i;
		} else {
			/* Retrieve CS id */
			ret = of_property_read_u32_index(np, "reg", i, &cs);
			if (ret) {
				dev_err(dev, "could not retrieve reg property: %d\n",
					ret);
				return ret;
			}
		}

		if (cs >= nfc->caps->max_cs_nb) {
			dev_err(dev, "invalid reg value: %u (max CS = %d)\n",
				cs, nfc->caps->max_cs_nb);
			return -EINVAL;
		}

		if (test_and_set_bit(cs, &nfc->assigned_cs)) {
			dev_err(dev, "CS %d already assigned\n", cs);
			return -EINVAL;
		}

		/*
		 * The cs variable represents the chip select id, which must be
		 * converted in bit fields for NDCB0 and NDCB2 to select the
		 * right chip. Unfortunately, due to a lack of information on
		 * the subject and incoherent documentation, the user should not
		 * use CS1 and CS3 at all as asserting them is not supported in
		 * a reliable way (due to multiplexing inside ADDR5 field).
		 */
		marvell_nand->sels[i].cs = cs;
		switch (cs) {
		case 0:
		case 2:
			marvell_nand->sels[i].ndcb0_csel = 0;
			break;
		case 1:
		case 3:
			marvell_nand->sels[i].ndcb0_csel = NDCB0_CSEL;
			break;
		default:
			return -EINVAL;
		}

		/* Retrieve RB id */
		if (pdata || nfc->caps->legacy_of_bindings) {
			/* Legacy bindings always use RB #0 */
			rb = 0;
		} else {
			ret = of_property_read_u32_index(np, "nand-rb", i,
							 &rb);
			if (ret) {
				dev_err(dev,
					"could not retrieve RB property: %d\n",
					ret);
				return ret;
			}
		}

		if (rb >= nfc->caps->max_rb_nb) {
			dev_err(dev, "invalid reg value: %u (max RB = %d)\n",
				rb, nfc->caps->max_rb_nb);
			return -EINVAL;
		}

		marvell_nand->sels[i].rb = rb;
	}

	chip = &marvell_nand->chip;
	chip->controller = &nfc->controller;
	nand_set_flash_node(chip, np);

	if (!of_property_read_bool(np, "marvell,nand-keep-config"))
		chip->options |= NAND_KEEP_TIMINGS;

	mtd = nand_to_mtd(chip);
	mtd->dev.parent = dev;

	/*
	 * Default to HW ECC engine mode. If the nand-ecc-mode property is given
	 * in the DT node, this entry will be overwritten in nand_scan_ident().
	 */
	chip->ecc.mode = NAND_ECC_HW;

	/*
	 * Save a reference value for timing registers before
	 * ->setup_data_interface() is called.
	 */
	marvell_nand->ndtr0 = readl_relaxed(nfc->regs + NDTR0);
	marvell_nand->ndtr1 = readl_relaxed(nfc->regs + NDTR1);

	chip->options |= NAND_BUSWIDTH_AUTO;

	ret = nand_scan(chip, marvell_nand->nsels);
	if (ret) {
		dev_err(dev, "could not scan the nand chip\n");
		return ret;
	}

	if (pdata)
		/* Legacy bindings support only one chip */
		ret = mtd_device_register(mtd, pdata->parts, pdata->nr_parts);
	else
		ret = mtd_device_register(mtd, NULL, 0);
	if (ret) {
		dev_err(dev, "failed to register mtd device: %d\n", ret);
		nand_cleanup(chip);
		return ret;
	}

	list_add_tail(&marvell_nand->node, &nfc->chips);

	return 0;
}

static void marvell_nand_chips_cleanup(struct marvell_nfc *nfc)
{
	struct marvell_nand_chip *entry, *temp;
	struct nand_chip *chip;
	int ret;

	list_for_each_entry_safe(entry, temp, &nfc->chips, node) {
		chip = &entry->chip;
		ret = mtd_device_unregister(nand_to_mtd(chip));
		WARN_ON(ret);
		nand_cleanup(chip);
		list_del(&entry->node);
	}
}

static int marvell_nand_chips_init(struct device *dev, struct marvell_nfc *nfc)
{
	struct device_node *np = dev->of_node;
	struct device_node *nand_np;
	int max_cs = nfc->caps->max_cs_nb;
	int nchips;
	int ret;

	if (!np)
		nchips = 1;
	else
		nchips = of_get_child_count(np);

	if (nchips > max_cs) {
		dev_err(dev, "too many NAND chips: %d (max = %d CS)\n", nchips,
			max_cs);
		return -EINVAL;
	}

	/*
	 * Legacy bindings do not use child nodes to exhibit NAND chip
	 * properties and layout. Instead, NAND properties are mixed with the
	 * controller ones, and partitions are defined as direct subnodes of the
	 * NAND controller node.
	 */
	if (nfc->caps->legacy_of_bindings) {
		ret = marvell_nand_chip_init(dev, nfc, np);
		return ret;
	}

	for_each_child_of_node(np, nand_np) {
		ret = marvell_nand_chip_init(dev, nfc, nand_np);
		if (ret) {
			of_node_put(nand_np);
			goto cleanup_chips;
		}
	}

	return 0;

cleanup_chips:
	marvell_nand_chips_cleanup(nfc);

	return ret;
}

static int marvell_nfc_init_dma(struct marvell_nfc *nfc)
{
	struct platform_device *pdev = container_of(nfc->dev,
						    struct platform_device,
						    dev);
	struct dma_slave_config config = {};
	struct resource *r;
	int ret;

	if (!IS_ENABLED(CONFIG_PXA_DMA)) {
		dev_warn(nfc->dev,
			 "DMA not enabled in configuration\n");
		return -ENOTSUPP;
	}

	ret = dma_set_mask_and_coherent(nfc->dev, DMA_BIT_MASK(32));
	if (ret)
		return ret;

	nfc->dma_chan =	dma_request_chan(nfc->dev, "data");
	if (IS_ERR(nfc->dma_chan)) {
		ret = PTR_ERR(nfc->dma_chan);
		nfc->dma_chan = NULL;
		if (ret != -EPROBE_DEFER)
			dev_err(nfc->dev, "DMA channel request failed: %d\n",
				ret);
		return ret;
	}

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!r) {
		ret = -ENXIO;
		goto release_channel;
	}

	config.src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	config.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	config.src_addr = r->start + NDDB;
	config.dst_addr = r->start + NDDB;
	config.src_maxburst = 32;
	config.dst_maxburst = 32;
	ret = dmaengine_slave_config(nfc->dma_chan, &config);
	if (ret < 0) {
		dev_err(nfc->dev, "Failed to configure DMA channel\n");
		goto release_channel;
	}

	/*
	 * DMA must act on length multiple of 32 and this length may be
	 * bigger than the destination buffer. Use this buffer instead
	 * for DMA transfers and then copy the desired amount of data to
	 * the provided buffer.
	 */
	nfc->dma_buf = kmalloc(MAX_CHUNK_SIZE, GFP_KERNEL | GFP_DMA);
	if (!nfc->dma_buf) {
		ret = -ENOMEM;
		goto release_channel;
	}

	nfc->use_dma = true;

	return 0;

release_channel:
	dma_release_channel(nfc->dma_chan);
	nfc->dma_chan = NULL;

	return ret;
}

static void marvell_nfc_reset(struct marvell_nfc *nfc)
{
	/*
	 * ECC operations and interruptions are only enabled when specifically
	 * needed. ECC shall not be activated in the early stages (fails probe).
	 * Arbiter flag, even if marked as "reserved", must be set (empirical).
	 * SPARE_EN bit must always be set or ECC bytes will not be at the same
	 * offset in the read page and this will fail the protection.
	 */
	writel_relaxed(NDCR_ALL_INT | NDCR_ND_ARB_EN | NDCR_SPARE_EN |
		       NDCR_RD_ID_CNT(NFCV1_READID_LEN), nfc->regs + NDCR);
	writel_relaxed(0xFFFFFFFF, nfc->regs + NDSR);
	writel_relaxed(0, nfc->regs + NDECCCTRL);
}

static int marvell_nfc_init(struct marvell_nfc *nfc)
{
	struct device_node *np = nfc->dev->of_node;

	/*
	 * Some SoCs like A7k/A8k need to enable manually the NAND
	 * controller, gated clocks and reset bits to avoid being bootloader
	 * dependent. This is done through the use of the System Functions
	 * registers.
	 */
	if (nfc->caps->need_system_controller) {
		struct regmap *sysctrl_base =
			syscon_regmap_lookup_by_phandle(np,
							"marvell,system-controller");

		if (IS_ERR(sysctrl_base))
			return PTR_ERR(sysctrl_base);

		regmap_write(sysctrl_base, GENCONF_SOC_DEVICE_MUX,
			     GENCONF_SOC_DEVICE_MUX_NFC_EN |
			     GENCONF_SOC_DEVICE_MUX_ECC_CLK_RST |
			     GENCONF_SOC_DEVICE_MUX_ECC_CORE_RST |
			     GENCONF_SOC_DEVICE_MUX_NFC_INT_EN);

		regmap_update_bits(sysctrl_base, GENCONF_CLK_GATING_CTRL,
				   GENCONF_CLK_GATING_CTRL_ND_GATE,
				   GENCONF_CLK_GATING_CTRL_ND_GATE);

		regmap_update_bits(sysctrl_base, GENCONF_ND_CLK_CTRL,
				   GENCONF_ND_CLK_CTRL_EN,
				   GENCONF_ND_CLK_CTRL_EN);
	}

	/* Configure the DMA if appropriate */
	if (!nfc->caps->is_nfcv2)
		marvell_nfc_init_dma(nfc);

	marvell_nfc_reset(nfc);

	return 0;
}

static int marvell_nfc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct marvell_nfc *nfc;
	int ret;
	int irq;

	nfc = devm_kzalloc(&pdev->dev, sizeof(struct marvell_nfc),
			   GFP_KERNEL);
	if (!nfc)
		return -ENOMEM;

	nfc->dev = dev;
	nand_controller_init(&nfc->controller);
	nfc->controller.ops = &marvell_nand_controller_ops;
	INIT_LIST_HEAD(&nfc->chips);

	nfc->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(nfc->regs))
		return PTR_ERR(nfc->regs);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	nfc->core_clk = devm_clk_get(&pdev->dev, "core");

	/* Managed the legacy case (when the first clock was not named) */
	if (nfc->core_clk == ERR_PTR(-ENOENT))
		nfc->core_clk = devm_clk_get(&pdev->dev, NULL);

	if (IS_ERR(nfc->core_clk))
		return PTR_ERR(nfc->core_clk);

	ret = clk_prepare_enable(nfc->core_clk);
	if (ret)
		return ret;

	nfc->reg_clk = devm_clk_get(&pdev->dev, "reg");
	if (IS_ERR(nfc->reg_clk)) {
		if (PTR_ERR(nfc->reg_clk) != -ENOENT) {
			ret = PTR_ERR(nfc->reg_clk);
			goto unprepare_core_clk;
		}

		nfc->reg_clk = NULL;
	}

	ret = clk_prepare_enable(nfc->reg_clk);
	if (ret)
		goto unprepare_core_clk;

	marvell_nfc_disable_int(nfc, NDCR_ALL_INT);
	marvell_nfc_clear_int(nfc, NDCR_ALL_INT);
	ret = devm_request_irq(dev, irq, marvell_nfc_isr,
			       0, "marvell-nfc", nfc);
	if (ret)
		goto unprepare_reg_clk;

	/* Get NAND controller capabilities */
	if (pdev->id_entry)
		nfc->caps = (void *)pdev->id_entry->driver_data;
	else
		nfc->caps = of_device_get_match_data(&pdev->dev);

	if (!nfc->caps) {
		dev_err(dev, "Could not retrieve NFC caps\n");
		ret = -EINVAL;
		goto unprepare_reg_clk;
	}

	/* Init the controller and then probe the chips */
	ret = marvell_nfc_init(nfc);
	if (ret)
		goto unprepare_reg_clk;

	platform_set_drvdata(pdev, nfc);

	ret = marvell_nand_chips_init(dev, nfc);
	if (ret)
		goto release_dma;

	return 0;

release_dma:
	if (nfc->use_dma)
		dma_release_channel(nfc->dma_chan);
unprepare_reg_clk:
	clk_disable_unprepare(nfc->reg_clk);
unprepare_core_clk:
	clk_disable_unprepare(nfc->core_clk);

	return ret;
}

static int marvell_nfc_remove(struct platform_device *pdev)
{
	struct marvell_nfc *nfc = platform_get_drvdata(pdev);

	marvell_nand_chips_cleanup(nfc);

	if (nfc->use_dma) {
		dmaengine_terminate_all(nfc->dma_chan);
		dma_release_channel(nfc->dma_chan);
	}

	clk_disable_unprepare(nfc->reg_clk);
	clk_disable_unprepare(nfc->core_clk);

	return 0;
}

static int __maybe_unused marvell_nfc_suspend(struct device *dev)
{
	struct marvell_nfc *nfc = dev_get_drvdata(dev);
	struct marvell_nand_chip *chip;

	list_for_each_entry(chip, &nfc->chips, node)
		marvell_nfc_wait_ndrun(&chip->chip);

	clk_disable_unprepare(nfc->reg_clk);
	clk_disable_unprepare(nfc->core_clk);

	return 0;
}

static int __maybe_unused marvell_nfc_resume(struct device *dev)
{
	struct marvell_nfc *nfc = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(nfc->core_clk);
	if (ret < 0)
		return ret;

	ret = clk_prepare_enable(nfc->reg_clk);
	if (ret < 0)
		return ret;

	/*
	 * Reset nfc->selected_chip so the next command will cause the timing
	 * registers to be restored in marvell_nfc_select_target().
	 */
	nfc->selected_chip = NULL;

	/* Reset registers that have lost their contents */
	marvell_nfc_reset(nfc);

	return 0;
}

static const struct dev_pm_ops marvell_nfc_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(marvell_nfc_suspend, marvell_nfc_resume)
};

static const struct marvell_nfc_caps marvell_armada_8k_nfc_caps = {
	.max_cs_nb = 4,
	.max_rb_nb = 2,
	.need_system_controller = true,
	.is_nfcv2 = true,
};

static const struct marvell_nfc_caps marvell_armada370_nfc_caps = {
	.max_cs_nb = 4,
	.max_rb_nb = 2,
	.is_nfcv2 = true,
};

static const struct marvell_nfc_caps marvell_pxa3xx_nfc_caps = {
	.max_cs_nb = 2,
	.max_rb_nb = 1,
	.use_dma = true,
};

static const struct marvell_nfc_caps marvell_armada_8k_nfc_legacy_caps = {
	.max_cs_nb = 4,
	.max_rb_nb = 2,
	.need_system_controller = true,
	.legacy_of_bindings = true,
	.is_nfcv2 = true,
};

static const struct marvell_nfc_caps marvell_armada370_nfc_legacy_caps = {
	.max_cs_nb = 4,
	.max_rb_nb = 2,
	.legacy_of_bindings = true,
	.is_nfcv2 = true,
};

static const struct marvell_nfc_caps marvell_pxa3xx_nfc_legacy_caps = {
	.max_cs_nb = 2,
	.max_rb_nb = 1,
	.legacy_of_bindings = true,
	.use_dma = true,
};

static const struct platform_device_id marvell_nfc_platform_ids[] = {
	{
		.name = "pxa3xx-nand",
		.driver_data = (kernel_ulong_t)&marvell_pxa3xx_nfc_legacy_caps,
	},
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(platform, marvell_nfc_platform_ids);

static const struct of_device_id marvell_nfc_of_ids[] = {
	{
		.compatible = "marvell,armada-8k-nand-controller",
		.data = &marvell_armada_8k_nfc_caps,
	},
	{
		.compatible = "marvell,armada370-nand-controller",
		.data = &marvell_armada370_nfc_caps,
	},
	{
		.compatible = "marvell,pxa3xx-nand-controller",
		.data = &marvell_pxa3xx_nfc_caps,
	},
	/* Support for old/deprecated bindings: */
	{
		.compatible = "marvell,armada-8k-nand",
		.data = &marvell_armada_8k_nfc_legacy_caps,
	},
	{
		.compatible = "marvell,armada370-nand",
		.data = &marvell_armada370_nfc_legacy_caps,
	},
	{
		.compatible = "marvell,pxa3xx-nand",
		.data = &marvell_pxa3xx_nfc_legacy_caps,
	},
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, marvell_nfc_of_ids);

static struct platform_driver marvell_nfc_driver = {
	.driver	= {
		.name		= "marvell-nfc",
		.of_match_table = marvell_nfc_of_ids,
		.pm		= &marvell_nfc_pm_ops,
	},
	.id_table = marvell_nfc_platform_ids,
	.probe = marvell_nfc_probe,
	.remove	= marvell_nfc_remove,
};
module_platform_driver(marvell_nfc_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Marvell NAND controller driver");
