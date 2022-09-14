// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2017-2018, The Linux Foundation. All rights reserved.

/* Disable MMIO tracing to prevent excessive logging of unwanted MMIO traces */
#define __DISABLE_TRACE_MMIO__

#include <linux/acpi.h>
#include <linux/clk.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/qcom-geni-se.h>

/**
 * DOC: Overview
 *
 * Generic Interface (GENI) Serial Engine (SE) Wrapper driver is introduced
 * to manage GENI firmware based Qualcomm Universal Peripheral (QUP) Wrapper
 * controller. QUP Wrapper is designed to support various serial bus protocols
 * like UART, SPI, I2C, I3C, etc.
 */

/**
 * DOC: Hardware description
 *
 * GENI based QUP is a highly-flexible and programmable module for supporting
 * a wide range of serial interfaces like UART, SPI, I2C, I3C, etc. A single
 * QUP module can provide upto 8 serial interfaces, using its internal
 * serial engines. The actual configuration is determined by the target
 * platform configuration. The protocol supported by each interface is
 * determined by the firmware loaded to the serial engine. Each SE consists
 * of a DMA Engine and GENI sub modules which enable serial engines to
 * support FIFO and DMA modes of operation.
 *
 *
 *                      +-----------------------------------------+
 *                      |QUP Wrapper                              |
 *                      |         +----------------------------+  |
 *   --QUP & SE Clocks-->         | Serial Engine N            |  +-IO------>
 *                      |         | ...                        |  | Interface
 *   <---Clock Perf.----+    +----+-----------------------+    |  |
 *     State Interface  |    | Serial Engine 1            |    |  |
 *                      |    |                            |    |  |
 *                      |    |                            |    |  |
 *   <--------AHB------->    |                            |    |  |
 *                      |    |                            +----+  |
 *                      |    |                            |       |
 *                      |    |                            |       |
 *   <------SE IRQ------+    +----------------------------+       |
 *                      |                                         |
 *                      +-----------------------------------------+
 *
 *                         Figure 1: GENI based QUP Wrapper
 *
 * The GENI submodules include primary and secondary sequencers which are
 * used to drive TX & RX operations. On serial interfaces that operate using
 * master-slave model, primary sequencer drives both TX & RX operations. On
 * serial interfaces that operate using peer-to-peer model, primary sequencer
 * drives TX operation and secondary sequencer drives RX operation.
 */

/**
 * DOC: Software description
 *
 * GENI SE Wrapper driver is structured into 2 parts:
 *
 * geni_wrapper represents QUP Wrapper controller. This part of the driver
 * manages QUP Wrapper information such as hardware version, clock
 * performance table that is common to all the internal serial engines.
 *
 * geni_se represents serial engine. This part of the driver manages serial
 * engine information such as clocks, containing QUP Wrapper, etc. This part
 * of driver also supports operations (eg. initialize the concerned serial
 * engine, select between FIFO and DMA mode of operation etc.) that are
 * common to all the serial engines and are independent of serial interfaces.
 */

#define MAX_CLK_PERF_LEVEL 32
#define NUM_AHB_CLKS 2

/**
 * struct geni_wrapper - Data structure to represent the QUP Wrapper Core
 * @dev:		Device pointer of the QUP wrapper core
 * @base:		Base address of this instance of QUP wrapper core
 * @ahb_clks:		Handle to the primary & secondary AHB clocks
 * @to_core:		Core ICC path
 */
struct geni_wrapper {
	struct device *dev;
	void __iomem *base;
	struct clk_bulk_data ahb_clks[NUM_AHB_CLKS];
};

static const char * const icc_path_names[] = {"qup-core", "qup-config",
						"qup-memory"};

#define QUP_HW_VER_REG			0x4

/* Common SE registers */
#define GENI_INIT_CFG_REVISION		0x0
#define GENI_S_INIT_CFG_REVISION	0x4
#define GENI_OUTPUT_CTRL		0x24
#define GENI_CGC_CTRL			0x28
#define GENI_CLK_CTRL_RO		0x60
#define GENI_FW_S_REVISION_RO		0x6c
#define SE_GENI_BYTE_GRAN		0x254
#define SE_GENI_TX_PACKING_CFG0		0x260
#define SE_GENI_TX_PACKING_CFG1		0x264
#define SE_GENI_RX_PACKING_CFG0		0x284
#define SE_GENI_RX_PACKING_CFG1		0x288
#define SE_GENI_M_GP_LENGTH		0x910
#define SE_GENI_S_GP_LENGTH		0x914
#define SE_DMA_TX_PTR_L			0xc30
#define SE_DMA_TX_PTR_H			0xc34
#define SE_DMA_TX_ATTR			0xc38
#define SE_DMA_TX_LEN			0xc3c
#define SE_DMA_TX_IRQ_EN		0xc48
#define SE_DMA_TX_IRQ_EN_SET		0xc4c
#define SE_DMA_TX_IRQ_EN_CLR		0xc50
#define SE_DMA_TX_LEN_IN		0xc54
#define SE_DMA_TX_MAX_BURST		0xc5c
#define SE_DMA_RX_PTR_L			0xd30
#define SE_DMA_RX_PTR_H			0xd34
#define SE_DMA_RX_ATTR			0xd38
#define SE_DMA_RX_LEN			0xd3c
#define SE_DMA_RX_IRQ_EN		0xd48
#define SE_DMA_RX_IRQ_EN_SET		0xd4c
#define SE_DMA_RX_IRQ_EN_CLR		0xd50
#define SE_DMA_RX_LEN_IN		0xd54
#define SE_DMA_RX_MAX_BURST		0xd5c
#define SE_DMA_RX_FLUSH			0xd60
#define SE_GSI_EVENT_EN			0xe18
#define SE_IRQ_EN			0xe1c
#define SE_DMA_GENERAL_CFG		0xe30

/* GENI_OUTPUT_CTRL fields */
#define DEFAULT_IO_OUTPUT_CTRL_MSK	GENMASK(6, 0)

/* GENI_CGC_CTRL fields */
#define CFG_AHB_CLK_CGC_ON		BIT(0)
#define CFG_AHB_WR_ACLK_CGC_ON		BIT(1)
#define DATA_AHB_CLK_CGC_ON		BIT(2)
#define SCLK_CGC_ON			BIT(3)
#define TX_CLK_CGC_ON			BIT(4)
#define RX_CLK_CGC_ON			BIT(5)
#define EXT_CLK_CGC_ON			BIT(6)
#define PROG_RAM_HCLK_OFF		BIT(8)
#define PROG_RAM_SCLK_OFF		BIT(9)
#define DEFAULT_CGC_EN			GENMASK(6, 0)

/* SE_GSI_EVENT_EN fields */
#define DMA_RX_EVENT_EN			BIT(0)
#define DMA_TX_EVENT_EN			BIT(1)
#define GENI_M_EVENT_EN			BIT(2)
#define GENI_S_EVENT_EN			BIT(3)

/* SE_IRQ_EN fields */
#define DMA_RX_IRQ_EN			BIT(0)
#define DMA_TX_IRQ_EN			BIT(1)
#define GENI_M_IRQ_EN			BIT(2)
#define GENI_S_IRQ_EN			BIT(3)

/* SE_DMA_GENERAL_CFG */
#define DMA_RX_CLK_CGC_ON		BIT(0)
#define DMA_TX_CLK_CGC_ON		BIT(1)
#define DMA_AHB_SLV_CFG_ON		BIT(2)
#define AHB_SEC_SLV_CLK_CGC_ON		BIT(3)
#define DUMMY_RX_NON_BUFFERABLE		BIT(4)
#define RX_DMA_ZERO_PADDING_EN		BIT(5)
#define RX_DMA_IRQ_DELAY_MSK		GENMASK(8, 6)
#define RX_DMA_IRQ_DELAY_SHFT		6

/**
 * geni_se_get_qup_hw_version() - Read the QUP wrapper Hardware version
 * @se:	Pointer to the corresponding serial engine.
 *
 * Return: Hardware Version of the wrapper.
 */
u32 geni_se_get_qup_hw_version(struct geni_se *se)
{
	struct geni_wrapper *wrapper = se->wrapper;

	return readl_relaxed(wrapper->base + QUP_HW_VER_REG);
}
EXPORT_SYMBOL(geni_se_get_qup_hw_version);

static void geni_se_io_set_mode(void __iomem *base)
{
	u32 val;

	val = readl_relaxed(base + SE_IRQ_EN);
	val |= GENI_M_IRQ_EN | GENI_S_IRQ_EN;
	val |= DMA_TX_IRQ_EN | DMA_RX_IRQ_EN;
	writel_relaxed(val, base + SE_IRQ_EN);

	val = readl_relaxed(base + SE_GENI_DMA_MODE_EN);
	val &= ~GENI_DMA_MODE_EN;
	writel_relaxed(val, base + SE_GENI_DMA_MODE_EN);

	writel_relaxed(0, base + SE_GSI_EVENT_EN);
}

static void geni_se_io_init(void __iomem *base)
{
	u32 val;

	val = readl_relaxed(base + GENI_CGC_CTRL);
	val |= DEFAULT_CGC_EN;
	writel_relaxed(val, base + GENI_CGC_CTRL);

	val = readl_relaxed(base + SE_DMA_GENERAL_CFG);
	val |= AHB_SEC_SLV_CLK_CGC_ON | DMA_AHB_SLV_CFG_ON;
	val |= DMA_TX_CLK_CGC_ON | DMA_RX_CLK_CGC_ON;
	writel_relaxed(val, base + SE_DMA_GENERAL_CFG);

	writel_relaxed(DEFAULT_IO_OUTPUT_CTRL_MSK, base + GENI_OUTPUT_CTRL);
	writel_relaxed(FORCE_DEFAULT, base + GENI_FORCE_DEFAULT_REG);
}

static void geni_se_irq_clear(struct geni_se *se)
{
	writel_relaxed(0, se->base + SE_GSI_EVENT_EN);
	writel_relaxed(0xffffffff, se->base + SE_GENI_M_IRQ_CLEAR);
	writel_relaxed(0xffffffff, se->base + SE_GENI_S_IRQ_CLEAR);
	writel_relaxed(0xffffffff, se->base + SE_DMA_TX_IRQ_CLR);
	writel_relaxed(0xffffffff, se->base + SE_DMA_RX_IRQ_CLR);
	writel_relaxed(0xffffffff, se->base + SE_IRQ_EN);
}

/**
 * geni_se_init() - Initialize the GENI serial engine
 * @se:		Pointer to the concerned serial engine.
 * @rx_wm:	Receive watermark, in units of FIFO words.
 * @rx_rfr:	Ready-for-receive watermark, in units of FIFO words.
 *
 * This function is used to initialize the GENI serial engine, configure
 * receive watermark and ready-for-receive watermarks.
 */
void geni_se_init(struct geni_se *se, u32 rx_wm, u32 rx_rfr)
{
	u32 val;

	geni_se_irq_clear(se);
	geni_se_io_init(se->base);
	geni_se_io_set_mode(se->base);

	writel_relaxed(rx_wm, se->base + SE_GENI_RX_WATERMARK_REG);
	writel_relaxed(rx_rfr, se->base + SE_GENI_RX_RFR_WATERMARK_REG);

	val = readl_relaxed(se->base + SE_GENI_M_IRQ_EN);
	val |= M_COMMON_GENI_M_IRQ_EN;
	writel_relaxed(val, se->base + SE_GENI_M_IRQ_EN);

	val = readl_relaxed(se->base + SE_GENI_S_IRQ_EN);
	val |= S_COMMON_GENI_S_IRQ_EN;
	writel_relaxed(val, se->base + SE_GENI_S_IRQ_EN);
}
EXPORT_SYMBOL(geni_se_init);

static void geni_se_select_fifo_mode(struct geni_se *se)
{
	u32 proto = geni_se_read_proto(se);
	u32 val, val_old;

	geni_se_irq_clear(se);

	/*
	 * The RX path for the UART is asynchronous and so needs more
	 * complex logic for enabling / disabling its interrupts.
	 *
	 * Specific notes:
	 * - The done and TX-related interrupts are managed manually.
	 * - We don't RX from the main sequencer (we use the secondary) so
	 *   we don't need the RX-related interrupts enabled in the main
	 *   sequencer for UART.
	 */
	if (proto != GENI_SE_UART) {
		val_old = val = readl_relaxed(se->base + SE_GENI_M_IRQ_EN);
		val |= M_CMD_DONE_EN | M_TX_FIFO_WATERMARK_EN;
		val |= M_RX_FIFO_WATERMARK_EN | M_RX_FIFO_LAST_EN;
		if (val != val_old)
			writel_relaxed(val, se->base + SE_GENI_M_IRQ_EN);

		val_old = val = readl_relaxed(se->base + SE_GENI_S_IRQ_EN);
		val |= S_CMD_DONE_EN;
		if (val != val_old)
			writel_relaxed(val, se->base + SE_GENI_S_IRQ_EN);
	}

	val_old = val = readl_relaxed(se->base + SE_GENI_DMA_MODE_EN);
	val &= ~GENI_DMA_MODE_EN;
	if (val != val_old)
		writel_relaxed(val, se->base + SE_GENI_DMA_MODE_EN);
}

static void geni_se_select_dma_mode(struct geni_se *se)
{
	u32 proto = geni_se_read_proto(se);
	u32 val, val_old;

	geni_se_irq_clear(se);

	if (proto != GENI_SE_UART) {
		val_old = val = readl_relaxed(se->base + SE_GENI_M_IRQ_EN);
		val &= ~(M_CMD_DONE_EN | M_TX_FIFO_WATERMARK_EN);
		val &= ~(M_RX_FIFO_WATERMARK_EN | M_RX_FIFO_LAST_EN);
		if (val != val_old)
			writel_relaxed(val, se->base + SE_GENI_M_IRQ_EN);

		val_old = val = readl_relaxed(se->base + SE_GENI_S_IRQ_EN);
		val &= ~S_CMD_DONE_EN;
		if (val != val_old)
			writel_relaxed(val, se->base + SE_GENI_S_IRQ_EN);
	}

	val_old = val = readl_relaxed(se->base + SE_GENI_DMA_MODE_EN);
	val |= GENI_DMA_MODE_EN;
	if (val != val_old)
		writel_relaxed(val, se->base + SE_GENI_DMA_MODE_EN);
}

static void geni_se_select_gpi_mode(struct geni_se *se)
{
	u32 val;

	geni_se_irq_clear(se);

	writel(0, se->base + SE_IRQ_EN);

	val = readl(se->base + SE_GENI_S_IRQ_EN);
	val &= ~S_CMD_DONE_EN;
	writel(val, se->base + SE_GENI_S_IRQ_EN);

	val = readl(se->base + SE_GENI_M_IRQ_EN);
	val &= ~(M_CMD_DONE_EN | M_TX_FIFO_WATERMARK_EN |
		 M_RX_FIFO_WATERMARK_EN | M_RX_FIFO_LAST_EN);
	writel(val, se->base + SE_GENI_M_IRQ_EN);

	writel(GENI_DMA_MODE_EN, se->base + SE_GENI_DMA_MODE_EN);

	val = readl(se->base + SE_GSI_EVENT_EN);
	val |= (DMA_RX_EVENT_EN | DMA_TX_EVENT_EN | GENI_M_EVENT_EN | GENI_S_EVENT_EN);
	writel(val, se->base + SE_GSI_EVENT_EN);
}

/**
 * geni_se_select_mode() - Select the serial engine transfer mode
 * @se:		Pointer to the concerned serial engine.
 * @mode:	Transfer mode to be selected.
 */
void geni_se_select_mode(struct geni_se *se, enum geni_se_xfer_mode mode)
{
	WARN_ON(mode != GENI_SE_FIFO && mode != GENI_SE_DMA && mode != GENI_GPI_DMA);

	switch (mode) {
	case GENI_SE_FIFO:
		geni_se_select_fifo_mode(se);
		break;
	case GENI_SE_DMA:
		geni_se_select_dma_mode(se);
		break;
	case GENI_GPI_DMA:
		geni_se_select_gpi_mode(se);
		break;
	case GENI_SE_INVALID:
	default:
		break;
	}
}
EXPORT_SYMBOL(geni_se_select_mode);

/**
 * DOC: Overview
 *
 * GENI FIFO packing is highly configurable. TX/RX packing/unpacking consist
 * of up to 4 operations, each operation represented by 4 configuration vectors
 * of 10 bits programmed in GENI_TX_PACKING_CFG0 and GENI_TX_PACKING_CFG1 for
 * TX FIFO and in GENI_RX_PACKING_CFG0 and GENI_RX_PACKING_CFG1 for RX FIFO.
 * Refer to below examples for detailed bit-field description.
 *
 * Example 1: word_size = 7, packing_mode = 4 x 8, msb_to_lsb = 1
 *
 *        +-----------+-------+-------+-------+-------+
 *        |           | vec_0 | vec_1 | vec_2 | vec_3 |
 *        +-----------+-------+-------+-------+-------+
 *        | start     | 0x6   | 0xe   | 0x16  | 0x1e  |
 *        | direction | 1     | 1     | 1     | 1     |
 *        | length    | 6     | 6     | 6     | 6     |
 *        | stop      | 0     | 0     | 0     | 1     |
 *        +-----------+-------+-------+-------+-------+
 *
 * Example 2: word_size = 15, packing_mode = 2 x 16, msb_to_lsb = 0
 *
 *        +-----------+-------+-------+-------+-------+
 *        |           | vec_0 | vec_1 | vec_2 | vec_3 |
 *        +-----------+-------+-------+-------+-------+
 *        | start     | 0x0   | 0x8   | 0x10  | 0x18  |
 *        | direction | 0     | 0     | 0     | 0     |
 *        | length    | 7     | 6     | 7     | 6     |
 *        | stop      | 0     | 0     | 0     | 1     |
 *        +-----------+-------+-------+-------+-------+
 *
 * Example 3: word_size = 23, packing_mode = 1 x 32, msb_to_lsb = 1
 *
 *        +-----------+-------+-------+-------+-------+
 *        |           | vec_0 | vec_1 | vec_2 | vec_3 |
 *        +-----------+-------+-------+-------+-------+
 *        | start     | 0x16  | 0xe   | 0x6   | 0x0   |
 *        | direction | 1     | 1     | 1     | 1     |
 *        | length    | 7     | 7     | 6     | 0     |
 *        | stop      | 0     | 0     | 1     | 0     |
 *        +-----------+-------+-------+-------+-------+
 *
 */

#define NUM_PACKING_VECTORS 4
#define PACKING_START_SHIFT 5
#define PACKING_DIR_SHIFT 4
#define PACKING_LEN_SHIFT 1
#define PACKING_STOP_BIT BIT(0)
#define PACKING_VECTOR_SHIFT 10
/**
 * geni_se_config_packing() - Packing configuration of the serial engine
 * @se:		Pointer to the concerned serial engine
 * @bpw:	Bits of data per transfer word.
 * @pack_words:	Number of words per fifo element.
 * @msb_to_lsb:	Transfer from MSB to LSB or vice-versa.
 * @tx_cfg:	Flag to configure the TX Packing.
 * @rx_cfg:	Flag to configure the RX Packing.
 *
 * This function is used to configure the packing rules for the current
 * transfer.
 */
void geni_se_config_packing(struct geni_se *se, int bpw, int pack_words,
			    bool msb_to_lsb, bool tx_cfg, bool rx_cfg)
{
	u32 cfg0, cfg1, cfg[NUM_PACKING_VECTORS] = {0};
	int len;
	int temp_bpw = bpw;
	int idx_start = msb_to_lsb ? bpw - 1 : 0;
	int idx = idx_start;
	int idx_delta = msb_to_lsb ? -BITS_PER_BYTE : BITS_PER_BYTE;
	int ceil_bpw = ALIGN(bpw, BITS_PER_BYTE);
	int iter = (ceil_bpw * pack_words) / BITS_PER_BYTE;
	int i;

	if (iter <= 0 || iter > NUM_PACKING_VECTORS)
		return;

	for (i = 0; i < iter; i++) {
		len = min_t(int, temp_bpw, BITS_PER_BYTE) - 1;
		cfg[i] = idx << PACKING_START_SHIFT;
		cfg[i] |= msb_to_lsb << PACKING_DIR_SHIFT;
		cfg[i] |= len << PACKING_LEN_SHIFT;

		if (temp_bpw <= BITS_PER_BYTE) {
			idx = ((i + 1) * BITS_PER_BYTE) + idx_start;
			temp_bpw = bpw;
		} else {
			idx = idx + idx_delta;
			temp_bpw = temp_bpw - BITS_PER_BYTE;
		}
	}
	cfg[iter - 1] |= PACKING_STOP_BIT;
	cfg0 = cfg[0] | (cfg[1] << PACKING_VECTOR_SHIFT);
	cfg1 = cfg[2] | (cfg[3] << PACKING_VECTOR_SHIFT);

	if (tx_cfg) {
		writel_relaxed(cfg0, se->base + SE_GENI_TX_PACKING_CFG0);
		writel_relaxed(cfg1, se->base + SE_GENI_TX_PACKING_CFG1);
	}
	if (rx_cfg) {
		writel_relaxed(cfg0, se->base + SE_GENI_RX_PACKING_CFG0);
		writel_relaxed(cfg1, se->base + SE_GENI_RX_PACKING_CFG1);
	}

	/*
	 * Number of protocol words in each FIFO entry
	 * 0 - 4x8, four words in each entry, max word size of 8 bits
	 * 1 - 2x16, two words in each entry, max word size of 16 bits
	 * 2 - 1x32, one word in each entry, max word size of 32 bits
	 * 3 - undefined
	 */
	if (pack_words || bpw == 32)
		writel_relaxed(bpw / 16, se->base + SE_GENI_BYTE_GRAN);
}
EXPORT_SYMBOL(geni_se_config_packing);

static void geni_se_clks_off(struct geni_se *se)
{
	struct geni_wrapper *wrapper = se->wrapper;

	clk_disable_unprepare(se->clk);
	clk_bulk_disable_unprepare(ARRAY_SIZE(wrapper->ahb_clks),
						wrapper->ahb_clks);
}

/**
 * geni_se_resources_off() - Turn off resources associated with the serial
 *                           engine
 * @se:	Pointer to the concerned serial engine.
 *
 * Return: 0 on success, standard Linux error codes on failure/error.
 */
int geni_se_resources_off(struct geni_se *se)
{
	int ret;

	if (has_acpi_companion(se->dev))
		return 0;

	ret = pinctrl_pm_select_sleep_state(se->dev);
	if (ret)
		return ret;

	geni_se_clks_off(se);
	return 0;
}
EXPORT_SYMBOL(geni_se_resources_off);

static int geni_se_clks_on(struct geni_se *se)
{
	int ret;
	struct geni_wrapper *wrapper = se->wrapper;

	ret = clk_bulk_prepare_enable(ARRAY_SIZE(wrapper->ahb_clks),
						wrapper->ahb_clks);
	if (ret)
		return ret;

	ret = clk_prepare_enable(se->clk);
	if (ret)
		clk_bulk_disable_unprepare(ARRAY_SIZE(wrapper->ahb_clks),
							wrapper->ahb_clks);
	return ret;
}

/**
 * geni_se_resources_on() - Turn on resources associated with the serial
 *                          engine
 * @se:	Pointer to the concerned serial engine.
 *
 * Return: 0 on success, standard Linux error codes on failure/error.
 */
int geni_se_resources_on(struct geni_se *se)
{
	int ret;

	if (has_acpi_companion(se->dev))
		return 0;

	ret = geni_se_clks_on(se);
	if (ret)
		return ret;

	ret = pinctrl_pm_select_default_state(se->dev);
	if (ret)
		geni_se_clks_off(se);

	return ret;
}
EXPORT_SYMBOL(geni_se_resources_on);

/**
 * geni_se_clk_tbl_get() - Get the clock table to program DFS
 * @se:		Pointer to the concerned serial engine.
 * @tbl:	Table in which the output is returned.
 *
 * This function is called by the protocol drivers to determine the different
 * clock frequencies supported by serial engine core clock. The protocol
 * drivers use the output to determine the clock frequency index to be
 * programmed into DFS.
 *
 * Return: number of valid performance levels in the table on success,
 *	   standard Linux error codes on failure.
 */
int geni_se_clk_tbl_get(struct geni_se *se, unsigned long **tbl)
{
	long freq = 0;
	int i;

	if (se->clk_perf_tbl) {
		*tbl = se->clk_perf_tbl;
		return se->num_clk_levels;
	}

	se->clk_perf_tbl = devm_kcalloc(se->dev, MAX_CLK_PERF_LEVEL,
					sizeof(*se->clk_perf_tbl),
					GFP_KERNEL);
	if (!se->clk_perf_tbl)
		return -ENOMEM;

	for (i = 0; i < MAX_CLK_PERF_LEVEL; i++) {
		freq = clk_round_rate(se->clk, freq + 1);
		if (freq <= 0 || freq == se->clk_perf_tbl[i - 1])
			break;
		se->clk_perf_tbl[i] = freq;
	}
	se->num_clk_levels = i;
	*tbl = se->clk_perf_tbl;
	return se->num_clk_levels;
}
EXPORT_SYMBOL(geni_se_clk_tbl_get);

/**
 * geni_se_clk_freq_match() - Get the matching or closest SE clock frequency
 * @se:		Pointer to the concerned serial engine.
 * @req_freq:	Requested clock frequency.
 * @index:	Index of the resultant frequency in the table.
 * @res_freq:	Resultant frequency of the source clock.
 * @exact:	Flag to indicate exact multiple requirement of the requested
 *		frequency.
 *
 * This function is called by the protocol drivers to determine the best match
 * of the requested frequency as provided by the serial engine clock in order
 * to meet the performance requirements.
 *
 * If we return success:
 * - if @exact is true  then @res_freq / <an_integer> == @req_freq
 * - if @exact is false then @res_freq / <an_integer> <= @req_freq
 *
 * Return: 0 on success, standard Linux error codes on failure.
 */
int geni_se_clk_freq_match(struct geni_se *se, unsigned long req_freq,
			   unsigned int *index, unsigned long *res_freq,
			   bool exact)
{
	unsigned long *tbl;
	int num_clk_levels;
	int i;
	unsigned long best_delta;
	unsigned long new_delta;
	unsigned int divider;

	num_clk_levels = geni_se_clk_tbl_get(se, &tbl);
	if (num_clk_levels < 0)
		return num_clk_levels;

	if (num_clk_levels == 0)
		return -EINVAL;

	best_delta = ULONG_MAX;
	for (i = 0; i < num_clk_levels; i++) {
		divider = DIV_ROUND_UP(tbl[i], req_freq);
		new_delta = req_freq - tbl[i] / divider;
		if (new_delta < best_delta) {
			/* We have a new best! */
			*index = i;
			*res_freq = tbl[i];

			/* If the new best is exact then we're done */
			if (new_delta == 0)
				return 0;

			/* Record how close we got */
			best_delta = new_delta;
		}
	}

	if (exact)
		return -EINVAL;

	return 0;
}
EXPORT_SYMBOL(geni_se_clk_freq_match);

#define GENI_SE_DMA_DONE_EN BIT(0)
#define GENI_SE_DMA_EOT_EN BIT(1)
#define GENI_SE_DMA_AHB_ERR_EN BIT(2)
#define GENI_SE_DMA_EOT_BUF BIT(0)
/**
 * geni_se_tx_dma_prep() - Prepare the serial engine for TX DMA transfer
 * @se:			Pointer to the concerned serial engine.
 * @buf:		Pointer to the TX buffer.
 * @len:		Length of the TX buffer.
 * @iova:		Pointer to store the mapped DMA address.
 *
 * This function is used to prepare the buffers for DMA TX.
 *
 * Return: 0 on success, standard Linux error codes on failure.
 */
int geni_se_tx_dma_prep(struct geni_se *se, void *buf, size_t len,
			dma_addr_t *iova)
{
	struct geni_wrapper *wrapper = se->wrapper;
	u32 val;

	if (!wrapper)
		return -EINVAL;

	*iova = dma_map_single(wrapper->dev, buf, len, DMA_TO_DEVICE);
	if (dma_mapping_error(wrapper->dev, *iova))
		return -EIO;

	val = GENI_SE_DMA_DONE_EN;
	val |= GENI_SE_DMA_EOT_EN;
	val |= GENI_SE_DMA_AHB_ERR_EN;
	writel_relaxed(val, se->base + SE_DMA_TX_IRQ_EN_SET);
	writel_relaxed(lower_32_bits(*iova), se->base + SE_DMA_TX_PTR_L);
	writel_relaxed(upper_32_bits(*iova), se->base + SE_DMA_TX_PTR_H);
	writel_relaxed(GENI_SE_DMA_EOT_BUF, se->base + SE_DMA_TX_ATTR);
	writel(len, se->base + SE_DMA_TX_LEN);
	return 0;
}
EXPORT_SYMBOL(geni_se_tx_dma_prep);

/**
 * geni_se_rx_dma_prep() - Prepare the serial engine for RX DMA transfer
 * @se:			Pointer to the concerned serial engine.
 * @buf:		Pointer to the RX buffer.
 * @len:		Length of the RX buffer.
 * @iova:		Pointer to store the mapped DMA address.
 *
 * This function is used to prepare the buffers for DMA RX.
 *
 * Return: 0 on success, standard Linux error codes on failure.
 */
int geni_se_rx_dma_prep(struct geni_se *se, void *buf, size_t len,
			dma_addr_t *iova)
{
	struct geni_wrapper *wrapper = se->wrapper;
	u32 val;

	if (!wrapper)
		return -EINVAL;

	*iova = dma_map_single(wrapper->dev, buf, len, DMA_FROM_DEVICE);
	if (dma_mapping_error(wrapper->dev, *iova))
		return -EIO;

	val = GENI_SE_DMA_DONE_EN;
	val |= GENI_SE_DMA_EOT_EN;
	val |= GENI_SE_DMA_AHB_ERR_EN;
	writel_relaxed(val, se->base + SE_DMA_RX_IRQ_EN_SET);
	writel_relaxed(lower_32_bits(*iova), se->base + SE_DMA_RX_PTR_L);
	writel_relaxed(upper_32_bits(*iova), se->base + SE_DMA_RX_PTR_H);
	/* RX does not have EOT buffer type bit. So just reset RX_ATTR */
	writel_relaxed(0, se->base + SE_DMA_RX_ATTR);
	writel(len, se->base + SE_DMA_RX_LEN);
	return 0;
}
EXPORT_SYMBOL(geni_se_rx_dma_prep);

/**
 * geni_se_tx_dma_unprep() - Unprepare the serial engine after TX DMA transfer
 * @se:			Pointer to the concerned serial engine.
 * @iova:		DMA address of the TX buffer.
 * @len:		Length of the TX buffer.
 *
 * This function is used to unprepare the DMA buffers after DMA TX.
 */
void geni_se_tx_dma_unprep(struct geni_se *se, dma_addr_t iova, size_t len)
{
	struct geni_wrapper *wrapper = se->wrapper;

	if (!dma_mapping_error(wrapper->dev, iova))
		dma_unmap_single(wrapper->dev, iova, len, DMA_TO_DEVICE);
}
EXPORT_SYMBOL(geni_se_tx_dma_unprep);

/**
 * geni_se_rx_dma_unprep() - Unprepare the serial engine after RX DMA transfer
 * @se:			Pointer to the concerned serial engine.
 * @iova:		DMA address of the RX buffer.
 * @len:		Length of the RX buffer.
 *
 * This function is used to unprepare the DMA buffers after DMA RX.
 */
void geni_se_rx_dma_unprep(struct geni_se *se, dma_addr_t iova, size_t len)
{
	struct geni_wrapper *wrapper = se->wrapper;

	if (!dma_mapping_error(wrapper->dev, iova))
		dma_unmap_single(wrapper->dev, iova, len, DMA_FROM_DEVICE);
}
EXPORT_SYMBOL(geni_se_rx_dma_unprep);

int geni_icc_get(struct geni_se *se, const char *icc_ddr)
{
	int i, err;
	const char *icc_names[] = {"qup-core", "qup-config", icc_ddr};

	if (has_acpi_companion(se->dev))
		return 0;

	for (i = 0; i < ARRAY_SIZE(se->icc_paths); i++) {
		if (!icc_names[i])
			continue;

		se->icc_paths[i].path = devm_of_icc_get(se->dev, icc_names[i]);
		if (IS_ERR(se->icc_paths[i].path))
			goto err;
	}

	return 0;

err:
	err = PTR_ERR(se->icc_paths[i].path);
	if (err != -EPROBE_DEFER)
		dev_err_ratelimited(se->dev, "Failed to get ICC path '%s': %d\n",
					icc_names[i], err);
	return err;

}
EXPORT_SYMBOL(geni_icc_get);

int geni_icc_set_bw(struct geni_se *se)
{
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(se->icc_paths); i++) {
		ret = icc_set_bw(se->icc_paths[i].path,
			se->icc_paths[i].avg_bw, se->icc_paths[i].avg_bw);
		if (ret) {
			dev_err_ratelimited(se->dev, "ICC BW voting failed on path '%s': %d\n",
					icc_path_names[i], ret);
			return ret;
		}
	}

	return 0;
}
EXPORT_SYMBOL(geni_icc_set_bw);

void geni_icc_set_tag(struct geni_se *se, u32 tag)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(se->icc_paths); i++)
		icc_set_tag(se->icc_paths[i].path, tag);
}
EXPORT_SYMBOL(geni_icc_set_tag);

/* To do: Replace this by icc_bulk_enable once it's implemented in ICC core */
int geni_icc_enable(struct geni_se *se)
{
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(se->icc_paths); i++) {
		ret = icc_enable(se->icc_paths[i].path);
		if (ret) {
			dev_err_ratelimited(se->dev, "ICC enable failed on path '%s': %d\n",
					icc_path_names[i], ret);
			return ret;
		}
	}

	return 0;
}
EXPORT_SYMBOL(geni_icc_enable);

int geni_icc_disable(struct geni_se *se)
{
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(se->icc_paths); i++) {
		ret = icc_disable(se->icc_paths[i].path);
		if (ret) {
			dev_err_ratelimited(se->dev, "ICC disable failed on path '%s': %d\n",
					icc_path_names[i], ret);
			return ret;
		}
	}

	return 0;
}
EXPORT_SYMBOL(geni_icc_disable);

static int geni_se_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct geni_wrapper *wrapper;
	int ret;

	wrapper = devm_kzalloc(dev, sizeof(*wrapper), GFP_KERNEL);
	if (!wrapper)
		return -ENOMEM;

	wrapper->dev = dev;
	wrapper->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(wrapper->base))
		return PTR_ERR(wrapper->base);

	if (!has_acpi_companion(&pdev->dev)) {
		wrapper->ahb_clks[0].id = "m-ahb";
		wrapper->ahb_clks[1].id = "s-ahb";
		ret = devm_clk_bulk_get(dev, NUM_AHB_CLKS, wrapper->ahb_clks);
		if (ret) {
			dev_err(dev, "Err getting AHB clks %d\n", ret);
			return ret;
		}
	}

	dev_set_drvdata(dev, wrapper);
	dev_dbg(dev, "GENI SE Driver probed\n");
	return devm_of_platform_populate(dev);
}

static const struct of_device_id geni_se_dt_match[] = {
	{ .compatible = "qcom,geni-se-qup", },
	{}
};
MODULE_DEVICE_TABLE(of, geni_se_dt_match);

static struct platform_driver geni_se_driver = {
	.driver = {
		.name = "geni_se_qup",
		.of_match_table = geni_se_dt_match,
	},
	.probe = geni_se_probe,
};
module_platform_driver(geni_se_driver);

MODULE_DESCRIPTION("GENI Serial Engine Driver");
MODULE_LICENSE("GPL v2");
