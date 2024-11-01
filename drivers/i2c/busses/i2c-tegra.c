// SPDX-License-Identifier: GPL-2.0
/*
 * drivers/i2c/busses/i2c-tegra.c
 *
 * Copyright (C) 2010 Google, Inc.
 * Author: Colin Cross <ccross@android.com>
 */

#include <linux/acpi.h>
#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>

#define BYTES_PER_FIFO_WORD 4

#define I2C_CNFG				0x000
#define I2C_CNFG_DEBOUNCE_CNT			GENMASK(14, 12)
#define I2C_CNFG_PACKET_MODE_EN			BIT(10)
#define I2C_CNFG_NEW_MASTER_FSM			BIT(11)
#define I2C_CNFG_MULTI_MASTER_MODE		BIT(17)
#define I2C_STATUS				0x01c
#define I2C_SL_CNFG				0x020
#define I2C_SL_CNFG_NACK			BIT(1)
#define I2C_SL_CNFG_NEWSL			BIT(2)
#define I2C_SL_ADDR1				0x02c
#define I2C_SL_ADDR2				0x030
#define I2C_TLOW_SEXT				0x034
#define I2C_TX_FIFO				0x050
#define I2C_RX_FIFO				0x054
#define I2C_PACKET_TRANSFER_STATUS		0x058
#define I2C_FIFO_CONTROL			0x05c
#define I2C_FIFO_CONTROL_TX_FLUSH		BIT(1)
#define I2C_FIFO_CONTROL_RX_FLUSH		BIT(0)
#define I2C_FIFO_CONTROL_TX_TRIG(x)		(((x) - 1) << 5)
#define I2C_FIFO_CONTROL_RX_TRIG(x)		(((x) - 1) << 2)
#define I2C_FIFO_STATUS				0x060
#define I2C_FIFO_STATUS_TX			GENMASK(7, 4)
#define I2C_FIFO_STATUS_RX			GENMASK(3, 0)
#define I2C_INT_MASK				0x064
#define I2C_INT_STATUS				0x068
#define I2C_INT_BUS_CLR_DONE			BIT(11)
#define I2C_INT_PACKET_XFER_COMPLETE		BIT(7)
#define I2C_INT_NO_ACK				BIT(3)
#define I2C_INT_ARBITRATION_LOST		BIT(2)
#define I2C_INT_TX_FIFO_DATA_REQ		BIT(1)
#define I2C_INT_RX_FIFO_DATA_REQ		BIT(0)
#define I2C_CLK_DIVISOR				0x06c
#define I2C_CLK_DIVISOR_STD_FAST_MODE		GENMASK(31, 16)
#define I2C_CLK_DIVISOR_HSMODE			GENMASK(15, 0)

#define DVC_CTRL_REG1				0x000
#define DVC_CTRL_REG1_INTR_EN			BIT(10)
#define DVC_CTRL_REG3				0x008
#define DVC_CTRL_REG3_SW_PROG			BIT(26)
#define DVC_CTRL_REG3_I2C_DONE_INTR_EN		BIT(30)
#define DVC_STATUS				0x00c
#define DVC_STATUS_I2C_DONE_INTR		BIT(30)

#define I2C_ERR_NONE				0x00
#define I2C_ERR_NO_ACK				BIT(0)
#define I2C_ERR_ARBITRATION_LOST		BIT(1)
#define I2C_ERR_UNKNOWN_INTERRUPT		BIT(2)
#define I2C_ERR_RX_BUFFER_OVERFLOW		BIT(3)

#define PACKET_HEADER0_HEADER_SIZE		GENMASK(29, 28)
#define PACKET_HEADER0_PACKET_ID		GENMASK(23, 16)
#define PACKET_HEADER0_CONT_ID			GENMASK(15, 12)
#define PACKET_HEADER0_PROTOCOL			GENMASK(7, 4)
#define PACKET_HEADER0_PROTOCOL_I2C		1

#define I2C_HEADER_CONT_ON_NAK			BIT(21)
#define I2C_HEADER_READ				BIT(19)
#define I2C_HEADER_10BIT_ADDR			BIT(18)
#define I2C_HEADER_IE_ENABLE			BIT(17)
#define I2C_HEADER_REPEAT_START			BIT(16)
#define I2C_HEADER_CONTINUE_XFER		BIT(15)
#define I2C_HEADER_SLAVE_ADDR_SHIFT		1

#define I2C_BUS_CLEAR_CNFG			0x084
#define I2C_BC_SCLK_THRESHOLD			GENMASK(23, 16)
#define I2C_BC_STOP_COND			BIT(2)
#define I2C_BC_TERMINATE			BIT(1)
#define I2C_BC_ENABLE				BIT(0)
#define I2C_BUS_CLEAR_STATUS			0x088
#define I2C_BC_STATUS				BIT(0)

#define I2C_CONFIG_LOAD				0x08c
#define I2C_MSTR_CONFIG_LOAD			BIT(0)

#define I2C_CLKEN_OVERRIDE			0x090
#define I2C_MST_CORE_CLKEN_OVR			BIT(0)

#define I2C_INTERFACE_TIMING_0			0x094
#define  I2C_INTERFACE_TIMING_THIGH		GENMASK(13, 8)
#define  I2C_INTERFACE_TIMING_TLOW		GENMASK(5, 0)
#define I2C_INTERFACE_TIMING_1			0x098
#define  I2C_INTERFACE_TIMING_TBUF		GENMASK(29, 24)
#define  I2C_INTERFACE_TIMING_TSU_STO		GENMASK(21, 16)
#define  I2C_INTERFACE_TIMING_THD_STA		GENMASK(13, 8)
#define  I2C_INTERFACE_TIMING_TSU_STA		GENMASK(5, 0)

#define I2C_HS_INTERFACE_TIMING_0		0x09c
#define  I2C_HS_INTERFACE_TIMING_THIGH		GENMASK(13, 8)
#define  I2C_HS_INTERFACE_TIMING_TLOW		GENMASK(5, 0)
#define I2C_HS_INTERFACE_TIMING_1		0x0a0
#define  I2C_HS_INTERFACE_TIMING_TSU_STO	GENMASK(21, 16)
#define  I2C_HS_INTERFACE_TIMING_THD_STA	GENMASK(13, 8)
#define  I2C_HS_INTERFACE_TIMING_TSU_STA	GENMASK(5, 0)

#define I2C_MST_FIFO_CONTROL			0x0b4
#define I2C_MST_FIFO_CONTROL_RX_FLUSH		BIT(0)
#define I2C_MST_FIFO_CONTROL_TX_FLUSH		BIT(1)
#define I2C_MST_FIFO_CONTROL_RX_TRIG(x)		(((x) - 1) <<  4)
#define I2C_MST_FIFO_CONTROL_TX_TRIG(x)		(((x) - 1) << 16)

#define I2C_MST_FIFO_STATUS			0x0b8
#define I2C_MST_FIFO_STATUS_TX			GENMASK(23, 16)
#define I2C_MST_FIFO_STATUS_RX			GENMASK(7, 0)

/* configuration load timeout in microseconds */
#define I2C_CONFIG_LOAD_TIMEOUT			1000000

/* packet header size in bytes */
#define I2C_PACKET_HEADER_SIZE			12

/*
 * I2C Controller will use PIO mode for transfers up to 32 bytes in order to
 * avoid DMA overhead, otherwise external APB DMA controller will be used.
 * Note that the actual MAX PIO length is 20 bytes because 32 bytes include
 * I2C_PACKET_HEADER_SIZE.
 */
#define I2C_PIO_MODE_PREFERRED_LEN		32

/*
 * msg_end_type: The bus control which needs to be sent at end of transfer.
 * @MSG_END_STOP: Send stop pulse.
 * @MSG_END_REPEAT_START: Send repeat-start.
 * @MSG_END_CONTINUE: Don't send stop or repeat-start.
 */
enum msg_end_type {
	MSG_END_STOP,
	MSG_END_REPEAT_START,
	MSG_END_CONTINUE,
};

/**
 * struct tegra_i2c_hw_feature : per hardware generation features
 * @has_continue_xfer_support: continue-transfer supported
 * @has_per_pkt_xfer_complete_irq: Has enable/disable capability for transfer
 *		completion interrupt on per packet basis.
 * @has_config_load_reg: Has the config load register to load the new
 *		configuration.
 * @clk_divisor_hs_mode: Clock divisor in HS mode.
 * @clk_divisor_std_mode: Clock divisor in standard mode. It is
 *		applicable if there is no fast clock source i.e. single clock
 *		source.
 * @clk_divisor_fast_mode: Clock divisor in fast mode. It is
 *		applicable if there is no fast clock source i.e. single clock
 *		source.
 * @clk_divisor_fast_plus_mode: Clock divisor in fast mode plus. It is
 *		applicable if there is no fast clock source (i.e. single
 *		clock source).
 * @has_multi_master_mode: The I2C controller supports running in single-master
 *		or multi-master mode.
 * @has_slcg_override_reg: The I2C controller supports a register that
 *		overrides the second level clock gating.
 * @has_mst_fifo: The I2C controller contains the new MST FIFO interface that
 *		provides additional features and allows for longer messages to
 *		be transferred in one go.
 * @quirks: I2C adapter quirks for limiting write/read transfer size and not
 *		allowing 0 length transfers.
 * @supports_bus_clear: Bus Clear support to recover from bus hang during
 *		SDA stuck low from device for some unknown reasons.
 * @has_apb_dma: Support of APBDMA on corresponding Tegra chip.
 * @tlow_std_mode: Low period of the clock in standard mode.
 * @thigh_std_mode: High period of the clock in standard mode.
 * @tlow_fast_fastplus_mode: Low period of the clock in fast/fast-plus modes.
 * @thigh_fast_fastplus_mode: High period of the clock in fast/fast-plus modes.
 * @setup_hold_time_std_mode: Setup and hold time for start and stop conditions
 *		in standard mode.
 * @setup_hold_time_fast_fast_plus_mode: Setup and hold time for start and stop
 *		conditions in fast/fast-plus modes.
 * @setup_hold_time_hs_mode: Setup and hold time for start and stop conditions
 *		in HS mode.
 * @has_interface_timing_reg: Has interface timing register to program the tuned
 *		timing settings.
 */
struct tegra_i2c_hw_feature {
	bool has_continue_xfer_support;
	bool has_per_pkt_xfer_complete_irq;
	bool has_config_load_reg;
	u32 clk_divisor_hs_mode;
	u32 clk_divisor_std_mode;
	u32 clk_divisor_fast_mode;
	u32 clk_divisor_fast_plus_mode;
	bool has_multi_master_mode;
	bool has_slcg_override_reg;
	bool has_mst_fifo;
	const struct i2c_adapter_quirks *quirks;
	bool supports_bus_clear;
	bool has_apb_dma;
	u32 tlow_std_mode;
	u32 thigh_std_mode;
	u32 tlow_fast_fastplus_mode;
	u32 thigh_fast_fastplus_mode;
	u32 setup_hold_time_std_mode;
	u32 setup_hold_time_fast_fast_plus_mode;
	u32 setup_hold_time_hs_mode;
	bool has_interface_timing_reg;
};

/**
 * struct tegra_i2c_dev - per device I2C context
 * @dev: device reference for power management
 * @hw: Tegra I2C HW feature
 * @adapter: core I2C layer adapter information
 * @div_clk: clock reference for div clock of I2C controller
 * @clocks: array of I2C controller clocks
 * @nclocks: number of clocks in the array
 * @rst: reset control for the I2C controller
 * @base: ioremapped registers cookie
 * @base_phys: physical base address of the I2C controller
 * @cont_id: I2C controller ID, used for packet header
 * @irq: IRQ number of transfer complete interrupt
 * @is_dvc: identifies the DVC I2C controller, has a different register layout
 * @is_vi: identifies the VI I2C controller, has a different register layout
 * @msg_complete: transfer completion notifier
 * @msg_buf_remaining: size of unsent data in the message buffer
 * @msg_len: length of message in current transfer
 * @msg_err: error code for completed message
 * @msg_buf: pointer to current message data
 * @msg_read: indicates that the transfer is a read access
 * @timings: i2c timings information like bus frequency
 * @multimaster_mode: indicates that I2C controller is in multi-master mode
 * @tx_dma_chan: DMA transmit channel
 * @rx_dma_chan: DMA receive channel
 * @dma_phys: handle to DMA resources
 * @dma_buf: pointer to allocated DMA buffer
 * @dma_buf_size: DMA buffer size
 * @dma_mode: indicates active DMA transfer
 * @dma_complete: DMA completion notifier
 * @atomic_mode: indicates active atomic transfer
 */
struct tegra_i2c_dev {
	struct device *dev;
	struct i2c_adapter adapter;

	const struct tegra_i2c_hw_feature *hw;
	struct reset_control *rst;
	unsigned int cont_id;
	unsigned int irq;

	phys_addr_t base_phys;
	void __iomem *base;

	struct clk_bulk_data clocks[2];
	unsigned int nclocks;

	struct clk *div_clk;
	struct i2c_timings timings;

	struct completion msg_complete;
	size_t msg_buf_remaining;
	unsigned int msg_len;
	int msg_err;
	u8 *msg_buf;

	struct completion dma_complete;
	struct dma_chan *tx_dma_chan;
	struct dma_chan *rx_dma_chan;
	unsigned int dma_buf_size;
	struct device *dma_dev;
	dma_addr_t dma_phys;
	void *dma_buf;

	bool multimaster_mode;
	bool atomic_mode;
	bool dma_mode;
	bool msg_read;
	bool is_dvc;
	bool is_vi;
};

#define IS_DVC(dev) (IS_ENABLED(CONFIG_ARCH_TEGRA_2x_SOC) && (dev)->is_dvc)
#define IS_VI(dev)  (IS_ENABLED(CONFIG_ARCH_TEGRA_210_SOC) && (dev)->is_vi)

static void dvc_writel(struct tegra_i2c_dev *i2c_dev, u32 val,
		       unsigned int reg)
{
	writel_relaxed(val, i2c_dev->base + reg);
}

static u32 dvc_readl(struct tegra_i2c_dev *i2c_dev, unsigned int reg)
{
	return readl_relaxed(i2c_dev->base + reg);
}

/*
 * If necessary, i2c_writel() and i2c_readl() will offset the register
 * in order to talk to the I2C block inside the DVC block.
 */
static u32 tegra_i2c_reg_addr(struct tegra_i2c_dev *i2c_dev, unsigned int reg)
{
	if (IS_DVC(i2c_dev))
		reg += (reg >= I2C_TX_FIFO) ? 0x10 : 0x40;
	else if (IS_VI(i2c_dev))
		reg = 0xc00 + (reg << 2);

	return reg;
}

static void i2c_writel(struct tegra_i2c_dev *i2c_dev, u32 val, unsigned int reg)
{
	writel_relaxed(val, i2c_dev->base + tegra_i2c_reg_addr(i2c_dev, reg));

	/* read back register to make sure that register writes completed */
	if (reg != I2C_TX_FIFO)
		readl_relaxed(i2c_dev->base + tegra_i2c_reg_addr(i2c_dev, reg));
	else if (IS_VI(i2c_dev))
		readl_relaxed(i2c_dev->base + tegra_i2c_reg_addr(i2c_dev, I2C_INT_STATUS));
}

static u32 i2c_readl(struct tegra_i2c_dev *i2c_dev, unsigned int reg)
{
	return readl_relaxed(i2c_dev->base + tegra_i2c_reg_addr(i2c_dev, reg));
}

static void i2c_writesl(struct tegra_i2c_dev *i2c_dev, void *data,
			unsigned int reg, unsigned int len)
{
	writesl(i2c_dev->base + tegra_i2c_reg_addr(i2c_dev, reg), data, len);
}

static void i2c_writesl_vi(struct tegra_i2c_dev *i2c_dev, void *data,
			   unsigned int reg, unsigned int len)
{
	u32 *data32 = data;

	/*
	 * VI I2C controller has known hardware bug where writes get stuck
	 * when immediate multiple writes happen to TX_FIFO register.
	 * Recommended software work around is to read I2C register after
	 * each write to TX_FIFO register to flush out the data.
	 */
	while (len--)
		i2c_writel(i2c_dev, *data32++, reg);
}

static void i2c_readsl(struct tegra_i2c_dev *i2c_dev, void *data,
		       unsigned int reg, unsigned int len)
{
	readsl(i2c_dev->base + tegra_i2c_reg_addr(i2c_dev, reg), data, len);
}

static void tegra_i2c_mask_irq(struct tegra_i2c_dev *i2c_dev, u32 mask)
{
	u32 int_mask;

	int_mask = i2c_readl(i2c_dev, I2C_INT_MASK) & ~mask;
	i2c_writel(i2c_dev, int_mask, I2C_INT_MASK);
}

static void tegra_i2c_unmask_irq(struct tegra_i2c_dev *i2c_dev, u32 mask)
{
	u32 int_mask;

	int_mask = i2c_readl(i2c_dev, I2C_INT_MASK) | mask;
	i2c_writel(i2c_dev, int_mask, I2C_INT_MASK);
}

static void tegra_i2c_dma_complete(void *args)
{
	struct tegra_i2c_dev *i2c_dev = args;

	complete(&i2c_dev->dma_complete);
}

static int tegra_i2c_dma_submit(struct tegra_i2c_dev *i2c_dev, size_t len)
{
	struct dma_async_tx_descriptor *dma_desc;
	enum dma_transfer_direction dir;
	struct dma_chan *chan;

	dev_dbg(i2c_dev->dev, "starting DMA for length: %zu\n", len);

	reinit_completion(&i2c_dev->dma_complete);

	dir = i2c_dev->msg_read ? DMA_DEV_TO_MEM : DMA_MEM_TO_DEV;
	chan = i2c_dev->msg_read ? i2c_dev->rx_dma_chan : i2c_dev->tx_dma_chan;

	dma_desc = dmaengine_prep_slave_single(chan, i2c_dev->dma_phys,
					       len, dir, DMA_PREP_INTERRUPT |
					       DMA_CTRL_ACK);
	if (!dma_desc) {
		dev_err(i2c_dev->dev, "failed to get %s DMA descriptor\n",
			i2c_dev->msg_read ? "RX" : "TX");
		return -EINVAL;
	}

	dma_desc->callback = tegra_i2c_dma_complete;
	dma_desc->callback_param = i2c_dev;

	dmaengine_submit(dma_desc);
	dma_async_issue_pending(chan);

	return 0;
}

static void tegra_i2c_release_dma(struct tegra_i2c_dev *i2c_dev)
{
	if (i2c_dev->dma_buf) {
		dma_free_coherent(i2c_dev->dma_dev, i2c_dev->dma_buf_size,
				  i2c_dev->dma_buf, i2c_dev->dma_phys);
		i2c_dev->dma_buf = NULL;
	}

	if (i2c_dev->tx_dma_chan) {
		dma_release_channel(i2c_dev->tx_dma_chan);
		i2c_dev->tx_dma_chan = NULL;
	}

	if (i2c_dev->rx_dma_chan) {
		dma_release_channel(i2c_dev->rx_dma_chan);
		i2c_dev->rx_dma_chan = NULL;
	}
}

static int tegra_i2c_init_dma(struct tegra_i2c_dev *i2c_dev)
{
	struct dma_chan *chan;
	dma_addr_t dma_phys;
	u32 *dma_buf;
	int err;

	if (IS_VI(i2c_dev))
		return 0;

	if (i2c_dev->hw->has_apb_dma) {
		if (!IS_ENABLED(CONFIG_TEGRA20_APB_DMA)) {
			dev_dbg(i2c_dev->dev, "APB DMA support not enabled\n");
			return 0;
		}
	} else if (!IS_ENABLED(CONFIG_TEGRA186_GPC_DMA)) {
		dev_dbg(i2c_dev->dev, "GPC DMA support not enabled\n");
		return 0;
	}

	chan = dma_request_chan(i2c_dev->dev, "rx");
	if (IS_ERR(chan)) {
		err = PTR_ERR(chan);
		goto err_out;
	}

	i2c_dev->rx_dma_chan = chan;

	chan = dma_request_chan(i2c_dev->dev, "tx");
	if (IS_ERR(chan)) {
		err = PTR_ERR(chan);
		goto err_out;
	}

	i2c_dev->tx_dma_chan = chan;

	WARN_ON(i2c_dev->tx_dma_chan->device != i2c_dev->rx_dma_chan->device);
	i2c_dev->dma_dev = chan->device->dev;

	i2c_dev->dma_buf_size = i2c_dev->hw->quirks->max_write_len +
				I2C_PACKET_HEADER_SIZE;

	dma_buf = dma_alloc_coherent(i2c_dev->dma_dev, i2c_dev->dma_buf_size,
				     &dma_phys, GFP_KERNEL | __GFP_NOWARN);
	if (!dma_buf) {
		dev_err(i2c_dev->dev, "failed to allocate DMA buffer\n");
		err = -ENOMEM;
		goto err_out;
	}

	i2c_dev->dma_buf = dma_buf;
	i2c_dev->dma_phys = dma_phys;

	return 0;

err_out:
	tegra_i2c_release_dma(i2c_dev);
	if (err != -EPROBE_DEFER) {
		dev_err(i2c_dev->dev, "cannot use DMA: %d\n", err);
		dev_err(i2c_dev->dev, "falling back to PIO\n");
		return 0;
	}

	return err;
}

/*
 * One of the Tegra I2C blocks is inside the DVC (Digital Voltage Controller)
 * block.  This block is identical to the rest of the I2C blocks, except that
 * it only supports master mode, it has registers moved around, and it needs
 * some extra init to get it into I2C mode.  The register moves are handled
 * by i2c_readl() and i2c_writel().
 */
static void tegra_dvc_init(struct tegra_i2c_dev *i2c_dev)
{
	u32 val;

	val = dvc_readl(i2c_dev, DVC_CTRL_REG3);
	val |= DVC_CTRL_REG3_SW_PROG;
	val |= DVC_CTRL_REG3_I2C_DONE_INTR_EN;
	dvc_writel(i2c_dev, val, DVC_CTRL_REG3);

	val = dvc_readl(i2c_dev, DVC_CTRL_REG1);
	val |= DVC_CTRL_REG1_INTR_EN;
	dvc_writel(i2c_dev, val, DVC_CTRL_REG1);
}

static void tegra_i2c_vi_init(struct tegra_i2c_dev *i2c_dev)
{
	u32 value;

	value = FIELD_PREP(I2C_INTERFACE_TIMING_THIGH, 2) |
		FIELD_PREP(I2C_INTERFACE_TIMING_TLOW, 4);
	i2c_writel(i2c_dev, value, I2C_INTERFACE_TIMING_0);

	value = FIELD_PREP(I2C_INTERFACE_TIMING_TBUF, 4) |
		FIELD_PREP(I2C_INTERFACE_TIMING_TSU_STO, 7) |
		FIELD_PREP(I2C_INTERFACE_TIMING_THD_STA, 4) |
		FIELD_PREP(I2C_INTERFACE_TIMING_TSU_STA, 4);
	i2c_writel(i2c_dev, value, I2C_INTERFACE_TIMING_1);

	value = FIELD_PREP(I2C_HS_INTERFACE_TIMING_THIGH, 3) |
		FIELD_PREP(I2C_HS_INTERFACE_TIMING_TLOW, 8);
	i2c_writel(i2c_dev, value, I2C_HS_INTERFACE_TIMING_0);

	value = FIELD_PREP(I2C_HS_INTERFACE_TIMING_TSU_STO, 11) |
		FIELD_PREP(I2C_HS_INTERFACE_TIMING_THD_STA, 11) |
		FIELD_PREP(I2C_HS_INTERFACE_TIMING_TSU_STA, 11);
	i2c_writel(i2c_dev, value, I2C_HS_INTERFACE_TIMING_1);

	value = FIELD_PREP(I2C_BC_SCLK_THRESHOLD, 9) | I2C_BC_STOP_COND;
	i2c_writel(i2c_dev, value, I2C_BUS_CLEAR_CNFG);

	i2c_writel(i2c_dev, 0x0, I2C_TLOW_SEXT);
}

static int tegra_i2c_poll_register(struct tegra_i2c_dev *i2c_dev,
				   u32 reg, u32 mask, u32 delay_us,
				   u32 timeout_us)
{
	void __iomem *addr = i2c_dev->base + tegra_i2c_reg_addr(i2c_dev, reg);
	u32 val;

	if (!i2c_dev->atomic_mode)
		return readl_relaxed_poll_timeout(addr, val, !(val & mask),
						  delay_us, timeout_us);

	return readl_relaxed_poll_timeout_atomic(addr, val, !(val & mask),
						 delay_us, timeout_us);
}

static int tegra_i2c_flush_fifos(struct tegra_i2c_dev *i2c_dev)
{
	u32 mask, val, offset;
	int err;

	if (i2c_dev->hw->has_mst_fifo) {
		mask = I2C_MST_FIFO_CONTROL_TX_FLUSH |
		       I2C_MST_FIFO_CONTROL_RX_FLUSH;
		offset = I2C_MST_FIFO_CONTROL;
	} else {
		mask = I2C_FIFO_CONTROL_TX_FLUSH |
		       I2C_FIFO_CONTROL_RX_FLUSH;
		offset = I2C_FIFO_CONTROL;
	}

	val = i2c_readl(i2c_dev, offset);
	val |= mask;
	i2c_writel(i2c_dev, val, offset);

	err = tegra_i2c_poll_register(i2c_dev, offset, mask, 1000, 1000000);
	if (err) {
		dev_err(i2c_dev->dev, "failed to flush FIFO\n");
		return err;
	}

	return 0;
}

static int tegra_i2c_wait_for_config_load(struct tegra_i2c_dev *i2c_dev)
{
	int err;

	if (!i2c_dev->hw->has_config_load_reg)
		return 0;

	i2c_writel(i2c_dev, I2C_MSTR_CONFIG_LOAD, I2C_CONFIG_LOAD);

	err = tegra_i2c_poll_register(i2c_dev, I2C_CONFIG_LOAD, 0xffffffff,
				      1000, I2C_CONFIG_LOAD_TIMEOUT);
	if (err) {
		dev_err(i2c_dev->dev, "failed to load config\n");
		return err;
	}

	return 0;
}

static int tegra_i2c_init(struct tegra_i2c_dev *i2c_dev)
{
	u32 val, clk_divisor, clk_multiplier, tsu_thd, tlow, thigh, non_hs_mode;
	acpi_handle handle = ACPI_HANDLE(i2c_dev->dev);
	struct i2c_timings *t = &i2c_dev->timings;
	int err;

	/*
	 * The reset shouldn't ever fail in practice. The failure will be a
	 * sign of a severe problem that needs to be resolved. Still we don't
	 * want to fail the initialization completely because this may break
	 * kernel boot up since voltage regulators use I2C. Hence, we will
	 * emit a noisy warning on error, which won't stay unnoticed and
	 * won't hose machine entirely.
	 */
	if (handle)
		err = acpi_evaluate_object(handle, "_RST", NULL, NULL);
	else
		err = reset_control_reset(i2c_dev->rst);

	WARN_ON_ONCE(err);

	if (IS_DVC(i2c_dev))
		tegra_dvc_init(i2c_dev);

	val = I2C_CNFG_NEW_MASTER_FSM | I2C_CNFG_PACKET_MODE_EN |
	      FIELD_PREP(I2C_CNFG_DEBOUNCE_CNT, 2);

	if (i2c_dev->hw->has_multi_master_mode)
		val |= I2C_CNFG_MULTI_MASTER_MODE;

	i2c_writel(i2c_dev, val, I2C_CNFG);
	i2c_writel(i2c_dev, 0, I2C_INT_MASK);

	if (IS_VI(i2c_dev))
		tegra_i2c_vi_init(i2c_dev);

	switch (t->bus_freq_hz) {
	case I2C_MAX_STANDARD_MODE_FREQ + 1 ... I2C_MAX_FAST_MODE_PLUS_FREQ:
	default:
		tlow = i2c_dev->hw->tlow_fast_fastplus_mode;
		thigh = i2c_dev->hw->thigh_fast_fastplus_mode;
		tsu_thd = i2c_dev->hw->setup_hold_time_fast_fast_plus_mode;

		if (t->bus_freq_hz > I2C_MAX_FAST_MODE_FREQ)
			non_hs_mode = i2c_dev->hw->clk_divisor_fast_plus_mode;
		else
			non_hs_mode = i2c_dev->hw->clk_divisor_fast_mode;
		break;

	case 0 ... I2C_MAX_STANDARD_MODE_FREQ:
		tlow = i2c_dev->hw->tlow_std_mode;
		thigh = i2c_dev->hw->thigh_std_mode;
		tsu_thd = i2c_dev->hw->setup_hold_time_std_mode;
		non_hs_mode = i2c_dev->hw->clk_divisor_std_mode;
		break;
	}

	/* make sure clock divisor programmed correctly */
	clk_divisor = FIELD_PREP(I2C_CLK_DIVISOR_HSMODE,
				 i2c_dev->hw->clk_divisor_hs_mode) |
		      FIELD_PREP(I2C_CLK_DIVISOR_STD_FAST_MODE, non_hs_mode);
	i2c_writel(i2c_dev, clk_divisor, I2C_CLK_DIVISOR);

	if (i2c_dev->hw->has_interface_timing_reg) {
		val = FIELD_PREP(I2C_INTERFACE_TIMING_THIGH, thigh) |
		      FIELD_PREP(I2C_INTERFACE_TIMING_TLOW, tlow);
		i2c_writel(i2c_dev, val, I2C_INTERFACE_TIMING_0);
	}

	/*
	 * Configure setup and hold times only when tsu_thd is non-zero.
	 * Otherwise, preserve the chip default values.
	 */
	if (i2c_dev->hw->has_interface_timing_reg && tsu_thd)
		i2c_writel(i2c_dev, tsu_thd, I2C_INTERFACE_TIMING_1);

	clk_multiplier = (tlow + thigh + 2) * (non_hs_mode + 1);

	err = clk_set_rate(i2c_dev->div_clk,
			   t->bus_freq_hz * clk_multiplier);
	if (err) {
		dev_err(i2c_dev->dev, "failed to set div-clk rate: %d\n", err);
		return err;
	}

	if (!IS_DVC(i2c_dev) && !IS_VI(i2c_dev)) {
		u32 sl_cfg = i2c_readl(i2c_dev, I2C_SL_CNFG);

		sl_cfg |= I2C_SL_CNFG_NACK | I2C_SL_CNFG_NEWSL;
		i2c_writel(i2c_dev, sl_cfg, I2C_SL_CNFG);
		i2c_writel(i2c_dev, 0xfc, I2C_SL_ADDR1);
		i2c_writel(i2c_dev, 0x00, I2C_SL_ADDR2);
	}

	err = tegra_i2c_flush_fifos(i2c_dev);
	if (err)
		return err;

	if (i2c_dev->multimaster_mode && i2c_dev->hw->has_slcg_override_reg)
		i2c_writel(i2c_dev, I2C_MST_CORE_CLKEN_OVR, I2C_CLKEN_OVERRIDE);

	err = tegra_i2c_wait_for_config_load(i2c_dev);
	if (err)
		return err;

	return 0;
}

static int tegra_i2c_disable_packet_mode(struct tegra_i2c_dev *i2c_dev)
{
	u32 cnfg;

	/*
	 * NACK interrupt is generated before the I2C controller generates
	 * the STOP condition on the bus.  So, wait for 2 clock periods
	 * before disabling the controller so that the STOP condition has
	 * been delivered properly.
	 */
	udelay(DIV_ROUND_UP(2 * 1000000, i2c_dev->timings.bus_freq_hz));

	cnfg = i2c_readl(i2c_dev, I2C_CNFG);
	if (cnfg & I2C_CNFG_PACKET_MODE_EN)
		i2c_writel(i2c_dev, cnfg & ~I2C_CNFG_PACKET_MODE_EN, I2C_CNFG);

	return tegra_i2c_wait_for_config_load(i2c_dev);
}

static int tegra_i2c_empty_rx_fifo(struct tegra_i2c_dev *i2c_dev)
{
	size_t buf_remaining = i2c_dev->msg_buf_remaining;
	unsigned int words_to_transfer, rx_fifo_avail;
	u8 *buf = i2c_dev->msg_buf;
	u32 val;

	/*
	 * Catch overflow due to message fully sent before the check for
	 * RX FIFO availability.
	 */
	if (WARN_ON_ONCE(!(i2c_dev->msg_buf_remaining)))
		return -EINVAL;

	if (i2c_dev->hw->has_mst_fifo) {
		val = i2c_readl(i2c_dev, I2C_MST_FIFO_STATUS);
		rx_fifo_avail = FIELD_GET(I2C_MST_FIFO_STATUS_RX, val);
	} else {
		val = i2c_readl(i2c_dev, I2C_FIFO_STATUS);
		rx_fifo_avail = FIELD_GET(I2C_FIFO_STATUS_RX, val);
	}

	/* round down to exclude partial word at the end of buffer */
	words_to_transfer = buf_remaining / BYTES_PER_FIFO_WORD;
	if (words_to_transfer > rx_fifo_avail)
		words_to_transfer = rx_fifo_avail;

	i2c_readsl(i2c_dev, buf, I2C_RX_FIFO, words_to_transfer);

	buf += words_to_transfer * BYTES_PER_FIFO_WORD;
	buf_remaining -= words_to_transfer * BYTES_PER_FIFO_WORD;
	rx_fifo_avail -= words_to_transfer;

	/*
	 * If there is a partial word at the end of buffer, handle it
	 * manually to prevent overwriting past the end of buffer.
	 */
	if (rx_fifo_avail > 0 && buf_remaining > 0) {
		/*
		 * buf_remaining > 3 check not needed as rx_fifo_avail == 0
		 * when (words_to_transfer was > rx_fifo_avail) earlier
		 * in this function.
		 */
		val = i2c_readl(i2c_dev, I2C_RX_FIFO);
		val = cpu_to_le32(val);
		memcpy(buf, &val, buf_remaining);
		buf_remaining = 0;
		rx_fifo_avail--;
	}

	/* RX FIFO must be drained, otherwise it's an Overflow case. */
	if (WARN_ON_ONCE(rx_fifo_avail))
		return -EINVAL;

	i2c_dev->msg_buf_remaining = buf_remaining;
	i2c_dev->msg_buf = buf;

	return 0;
}

static int tegra_i2c_fill_tx_fifo(struct tegra_i2c_dev *i2c_dev)
{
	size_t buf_remaining = i2c_dev->msg_buf_remaining;
	unsigned int words_to_transfer, tx_fifo_avail;
	u8 *buf = i2c_dev->msg_buf;
	u32 val;

	if (i2c_dev->hw->has_mst_fifo) {
		val = i2c_readl(i2c_dev, I2C_MST_FIFO_STATUS);
		tx_fifo_avail = FIELD_GET(I2C_MST_FIFO_STATUS_TX, val);
	} else {
		val = i2c_readl(i2c_dev, I2C_FIFO_STATUS);
		tx_fifo_avail = FIELD_GET(I2C_FIFO_STATUS_TX, val);
	}

	/* round down to exclude partial word at the end of buffer */
	words_to_transfer = buf_remaining / BYTES_PER_FIFO_WORD;

	/*
	 * This hunk pushes 4 bytes at a time into the TX FIFO.
	 *
	 * It's very common to have < 4 bytes, hence there is no word
	 * to push if we have less than 4 bytes to transfer.
	 */
	if (words_to_transfer) {
		if (words_to_transfer > tx_fifo_avail)
			words_to_transfer = tx_fifo_avail;

		/*
		 * Update state before writing to FIFO.  Note that this may
		 * cause us to finish writing all bytes (AKA buf_remaining
		 * goes to 0), hence we have a potential for an interrupt
		 * (PACKET_XFER_COMPLETE is not maskable), but GIC interrupt
		 * is disabled at this point.
		 */
		buf_remaining -= words_to_transfer * BYTES_PER_FIFO_WORD;
		tx_fifo_avail -= words_to_transfer;

		i2c_dev->msg_buf_remaining = buf_remaining;
		i2c_dev->msg_buf = buf + words_to_transfer * BYTES_PER_FIFO_WORD;

		if (IS_VI(i2c_dev))
			i2c_writesl_vi(i2c_dev, buf, I2C_TX_FIFO, words_to_transfer);
		else
			i2c_writesl(i2c_dev, buf, I2C_TX_FIFO, words_to_transfer);

		buf += words_to_transfer * BYTES_PER_FIFO_WORD;
	}

	/*
	 * If there is a partial word at the end of buffer, handle it manually
	 * to prevent reading past the end of buffer, which could cross a page
	 * boundary and fault.
	 */
	if (tx_fifo_avail > 0 && buf_remaining > 0) {
		/*
		 * buf_remaining > 3 check not needed as tx_fifo_avail == 0
		 * when (words_to_transfer was > tx_fifo_avail) earlier
		 * in this function for non-zero words_to_transfer.
		 */
		memcpy(&val, buf, buf_remaining);
		val = le32_to_cpu(val);

		i2c_dev->msg_buf_remaining = 0;
		i2c_dev->msg_buf = NULL;

		i2c_writel(i2c_dev, val, I2C_TX_FIFO);
	}

	return 0;
}

static irqreturn_t tegra_i2c_isr(int irq, void *dev_id)
{
	const u32 status_err = I2C_INT_NO_ACK | I2C_INT_ARBITRATION_LOST;
	struct tegra_i2c_dev *i2c_dev = dev_id;
	u32 status;

	status = i2c_readl(i2c_dev, I2C_INT_STATUS);

	if (status == 0) {
		dev_warn(i2c_dev->dev, "IRQ status 0 %08x %08x %08x\n",
			 i2c_readl(i2c_dev, I2C_PACKET_TRANSFER_STATUS),
			 i2c_readl(i2c_dev, I2C_STATUS),
			 i2c_readl(i2c_dev, I2C_CNFG));
		i2c_dev->msg_err |= I2C_ERR_UNKNOWN_INTERRUPT;
		goto err;
	}

	if (status & status_err) {
		tegra_i2c_disable_packet_mode(i2c_dev);
		if (status & I2C_INT_NO_ACK)
			i2c_dev->msg_err |= I2C_ERR_NO_ACK;
		if (status & I2C_INT_ARBITRATION_LOST)
			i2c_dev->msg_err |= I2C_ERR_ARBITRATION_LOST;
		goto err;
	}

	/*
	 * I2C transfer is terminated during the bus clear, so skip
	 * processing the other interrupts.
	 */
	if (i2c_dev->hw->supports_bus_clear && (status & I2C_INT_BUS_CLR_DONE))
		goto err;

	if (!i2c_dev->dma_mode) {
		if (i2c_dev->msg_read && (status & I2C_INT_RX_FIFO_DATA_REQ)) {
			if (tegra_i2c_empty_rx_fifo(i2c_dev)) {
				/*
				 * Overflow error condition: message fully sent,
				 * with no XFER_COMPLETE interrupt but hardware
				 * asks to transfer more.
				 */
				i2c_dev->msg_err |= I2C_ERR_RX_BUFFER_OVERFLOW;
				goto err;
			}
		}

		if (!i2c_dev->msg_read && (status & I2C_INT_TX_FIFO_DATA_REQ)) {
			if (i2c_dev->msg_buf_remaining)
				tegra_i2c_fill_tx_fifo(i2c_dev);
			else
				tegra_i2c_mask_irq(i2c_dev,
						   I2C_INT_TX_FIFO_DATA_REQ);
		}
	}

	i2c_writel(i2c_dev, status, I2C_INT_STATUS);
	if (IS_DVC(i2c_dev))
		dvc_writel(i2c_dev, DVC_STATUS_I2C_DONE_INTR, DVC_STATUS);

	/*
	 * During message read XFER_COMPLETE interrupt is triggered prior to
	 * DMA completion and during message write XFER_COMPLETE interrupt is
	 * triggered after DMA completion.
	 *
	 * PACKETS_XFER_COMPLETE indicates completion of all bytes of transfer,
	 * so forcing msg_buf_remaining to 0 in DMA mode.
	 */
	if (status & I2C_INT_PACKET_XFER_COMPLETE) {
		if (i2c_dev->dma_mode)
			i2c_dev->msg_buf_remaining = 0;
		/*
		 * Underflow error condition: XFER_COMPLETE before message
		 * fully sent.
		 */
		if (WARN_ON_ONCE(i2c_dev->msg_buf_remaining)) {
			i2c_dev->msg_err |= I2C_ERR_UNKNOWN_INTERRUPT;
			goto err;
		}
		complete(&i2c_dev->msg_complete);
	}
	goto done;
err:
	/* mask all interrupts on error */
	tegra_i2c_mask_irq(i2c_dev,
			   I2C_INT_NO_ACK |
			   I2C_INT_ARBITRATION_LOST |
			   I2C_INT_PACKET_XFER_COMPLETE |
			   I2C_INT_TX_FIFO_DATA_REQ |
			   I2C_INT_RX_FIFO_DATA_REQ);

	if (i2c_dev->hw->supports_bus_clear)
		tegra_i2c_mask_irq(i2c_dev, I2C_INT_BUS_CLR_DONE);

	i2c_writel(i2c_dev, status, I2C_INT_STATUS);

	if (IS_DVC(i2c_dev))
		dvc_writel(i2c_dev, DVC_STATUS_I2C_DONE_INTR, DVC_STATUS);

	if (i2c_dev->dma_mode) {
		if (i2c_dev->msg_read)
			dmaengine_terminate_async(i2c_dev->rx_dma_chan);
		else
			dmaengine_terminate_async(i2c_dev->tx_dma_chan);

		complete(&i2c_dev->dma_complete);
	}

	complete(&i2c_dev->msg_complete);
done:
	return IRQ_HANDLED;
}

static void tegra_i2c_config_fifo_trig(struct tegra_i2c_dev *i2c_dev,
				       size_t len)
{
	struct dma_slave_config slv_config = {0};
	u32 val, reg, dma_burst, reg_offset;
	struct dma_chan *chan;
	int err;

	if (i2c_dev->hw->has_mst_fifo)
		reg = I2C_MST_FIFO_CONTROL;
	else
		reg = I2C_FIFO_CONTROL;

	if (i2c_dev->dma_mode) {
		if (len & 0xF)
			dma_burst = 1;
		else if (len & 0x10)
			dma_burst = 4;
		else
			dma_burst = 8;

		if (i2c_dev->msg_read) {
			chan = i2c_dev->rx_dma_chan;
			reg_offset = tegra_i2c_reg_addr(i2c_dev, I2C_RX_FIFO);

			slv_config.src_addr = i2c_dev->base_phys + reg_offset;
			slv_config.src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
			slv_config.src_maxburst = dma_burst;

			if (i2c_dev->hw->has_mst_fifo)
				val = I2C_MST_FIFO_CONTROL_RX_TRIG(dma_burst);
			else
				val = I2C_FIFO_CONTROL_RX_TRIG(dma_burst);
		} else {
			chan = i2c_dev->tx_dma_chan;
			reg_offset = tegra_i2c_reg_addr(i2c_dev, I2C_TX_FIFO);

			slv_config.dst_addr = i2c_dev->base_phys + reg_offset;
			slv_config.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
			slv_config.dst_maxburst = dma_burst;

			if (i2c_dev->hw->has_mst_fifo)
				val = I2C_MST_FIFO_CONTROL_TX_TRIG(dma_burst);
			else
				val = I2C_FIFO_CONTROL_TX_TRIG(dma_burst);
		}

		slv_config.device_fc = true;
		err = dmaengine_slave_config(chan, &slv_config);
		if (err) {
			dev_err(i2c_dev->dev, "DMA config failed: %d\n", err);
			dev_err(i2c_dev->dev, "falling back to PIO\n");

			tegra_i2c_release_dma(i2c_dev);
			i2c_dev->dma_mode = false;
		} else {
			goto out;
		}
	}

	if (i2c_dev->hw->has_mst_fifo)
		val = I2C_MST_FIFO_CONTROL_TX_TRIG(8) |
		      I2C_MST_FIFO_CONTROL_RX_TRIG(1);
	else
		val = I2C_FIFO_CONTROL_TX_TRIG(8) |
		      I2C_FIFO_CONTROL_RX_TRIG(1);
out:
	i2c_writel(i2c_dev, val, reg);
}

static unsigned long tegra_i2c_poll_completion(struct tegra_i2c_dev *i2c_dev,
					       struct completion *complete,
					       unsigned int timeout_ms)
{
	ktime_t ktime = ktime_get();
	ktime_t ktimeout = ktime_add_ms(ktime, timeout_ms);

	do {
		u32 status = i2c_readl(i2c_dev, I2C_INT_STATUS);

		if (status)
			tegra_i2c_isr(i2c_dev->irq, i2c_dev);

		if (completion_done(complete)) {
			s64 delta = ktime_ms_delta(ktimeout, ktime);

			return msecs_to_jiffies(delta) ?: 1;
		}

		ktime = ktime_get();

	} while (ktime_before(ktime, ktimeout));

	return 0;
}

static unsigned long tegra_i2c_wait_completion(struct tegra_i2c_dev *i2c_dev,
					       struct completion *complete,
					       unsigned int timeout_ms)
{
	unsigned long ret;

	if (i2c_dev->atomic_mode) {
		ret = tegra_i2c_poll_completion(i2c_dev, complete, timeout_ms);
	} else {
		enable_irq(i2c_dev->irq);
		ret = wait_for_completion_timeout(complete,
						  msecs_to_jiffies(timeout_ms));
		disable_irq(i2c_dev->irq);

		/*
		 * Under some rare circumstances (like running KASAN +
		 * NFS root) CPU, which handles interrupt, may stuck in
		 * uninterruptible state for a significant time.  In this
		 * case we will get timeout if I2C transfer is running on
		 * a sibling CPU, despite of IRQ being raised.
		 *
		 * In order to handle this rare condition, the IRQ status
		 * needs to be checked after timeout.
		 */
		if (ret == 0)
			ret = tegra_i2c_poll_completion(i2c_dev, complete, 0);
	}

	return ret;
}

static int tegra_i2c_issue_bus_clear(struct i2c_adapter *adap)
{
	struct tegra_i2c_dev *i2c_dev = i2c_get_adapdata(adap);
	u32 val, time_left;
	int err;

	reinit_completion(&i2c_dev->msg_complete);

	val = FIELD_PREP(I2C_BC_SCLK_THRESHOLD, 9) | I2C_BC_STOP_COND |
	      I2C_BC_TERMINATE;
	i2c_writel(i2c_dev, val, I2C_BUS_CLEAR_CNFG);

	err = tegra_i2c_wait_for_config_load(i2c_dev);
	if (err)
		return err;

	val |= I2C_BC_ENABLE;
	i2c_writel(i2c_dev, val, I2C_BUS_CLEAR_CNFG);
	tegra_i2c_unmask_irq(i2c_dev, I2C_INT_BUS_CLR_DONE);

	time_left = tegra_i2c_wait_completion(i2c_dev, &i2c_dev->msg_complete, 50);
	tegra_i2c_mask_irq(i2c_dev, I2C_INT_BUS_CLR_DONE);

	if (time_left == 0) {
		dev_err(i2c_dev->dev, "failed to clear bus\n");
		return -ETIMEDOUT;
	}

	val = i2c_readl(i2c_dev, I2C_BUS_CLEAR_STATUS);
	if (!(val & I2C_BC_STATUS)) {
		dev_err(i2c_dev->dev, "un-recovered arbitration lost\n");
		return -EIO;
	}

	return -EAGAIN;
}

static void tegra_i2c_push_packet_header(struct tegra_i2c_dev *i2c_dev,
					 struct i2c_msg *msg,
					 enum msg_end_type end_state)
{
	u32 *dma_buf = i2c_dev->dma_buf;
	u32 packet_header;

	packet_header = FIELD_PREP(PACKET_HEADER0_HEADER_SIZE, 0) |
			FIELD_PREP(PACKET_HEADER0_PROTOCOL,
				   PACKET_HEADER0_PROTOCOL_I2C) |
			FIELD_PREP(PACKET_HEADER0_CONT_ID, i2c_dev->cont_id) |
			FIELD_PREP(PACKET_HEADER0_PACKET_ID, 1);

	if (i2c_dev->dma_mode && !i2c_dev->msg_read)
		*dma_buf++ = packet_header;
	else
		i2c_writel(i2c_dev, packet_header, I2C_TX_FIFO);

	packet_header = i2c_dev->msg_len - 1;

	if (i2c_dev->dma_mode && !i2c_dev->msg_read)
		*dma_buf++ = packet_header;
	else
		i2c_writel(i2c_dev, packet_header, I2C_TX_FIFO);

	packet_header = I2C_HEADER_IE_ENABLE;

	if (end_state == MSG_END_CONTINUE)
		packet_header |= I2C_HEADER_CONTINUE_XFER;
	else if (end_state == MSG_END_REPEAT_START)
		packet_header |= I2C_HEADER_REPEAT_START;

	if (msg->flags & I2C_M_TEN) {
		packet_header |= msg->addr;
		packet_header |= I2C_HEADER_10BIT_ADDR;
	} else {
		packet_header |= msg->addr << I2C_HEADER_SLAVE_ADDR_SHIFT;
	}

	if (msg->flags & I2C_M_IGNORE_NAK)
		packet_header |= I2C_HEADER_CONT_ON_NAK;

	if (msg->flags & I2C_M_RD)
		packet_header |= I2C_HEADER_READ;

	if (i2c_dev->dma_mode && !i2c_dev->msg_read)
		*dma_buf++ = packet_header;
	else
		i2c_writel(i2c_dev, packet_header, I2C_TX_FIFO);
}

static int tegra_i2c_error_recover(struct tegra_i2c_dev *i2c_dev,
				   struct i2c_msg *msg)
{
	if (i2c_dev->msg_err == I2C_ERR_NONE)
		return 0;

	tegra_i2c_init(i2c_dev);

	/* start recovery upon arbitration loss in single master mode */
	if (i2c_dev->msg_err == I2C_ERR_ARBITRATION_LOST) {
		if (!i2c_dev->multimaster_mode)
			return i2c_recover_bus(&i2c_dev->adapter);

		return -EAGAIN;
	}

	if (i2c_dev->msg_err == I2C_ERR_NO_ACK) {
		if (msg->flags & I2C_M_IGNORE_NAK)
			return 0;

		return -EREMOTEIO;
	}

	return -EIO;
}

static int tegra_i2c_xfer_msg(struct tegra_i2c_dev *i2c_dev,
			      struct i2c_msg *msg,
			      enum msg_end_type end_state)
{
	unsigned long time_left, xfer_time = 100;
	size_t xfer_size;
	u32 int_mask;
	int err;

	err = tegra_i2c_flush_fifos(i2c_dev);
	if (err)
		return err;

	i2c_dev->msg_buf = msg->buf;
	i2c_dev->msg_len = msg->len;

	i2c_dev->msg_err = I2C_ERR_NONE;
	i2c_dev->msg_read = !!(msg->flags & I2C_M_RD);
	reinit_completion(&i2c_dev->msg_complete);

	/*
	 * For SMBUS block read command, read only 1 byte in the first transfer.
	 * Adjust that 1 byte for the next transfer in the msg buffer and msg
	 * length.
	 */
	if (msg->flags & I2C_M_RECV_LEN) {
		if (end_state == MSG_END_CONTINUE) {
			i2c_dev->msg_len = 1;
		} else {
			i2c_dev->msg_buf += 1;
			i2c_dev->msg_len -= 1;
		}
	}

	i2c_dev->msg_buf_remaining = i2c_dev->msg_len;

	if (i2c_dev->msg_read)
		xfer_size = i2c_dev->msg_len;
	else
		xfer_size = i2c_dev->msg_len + I2C_PACKET_HEADER_SIZE;

	xfer_size = ALIGN(xfer_size, BYTES_PER_FIFO_WORD);

	i2c_dev->dma_mode = xfer_size > I2C_PIO_MODE_PREFERRED_LEN &&
			    i2c_dev->dma_buf && !i2c_dev->atomic_mode;

	tegra_i2c_config_fifo_trig(i2c_dev, xfer_size);

	/*
	 * Transfer time in mSec = Total bits / transfer rate
	 * Total bits = 9 bits per byte (including ACK bit) + Start & stop bits
	 */
	xfer_time += DIV_ROUND_CLOSEST(((xfer_size * 9) + 2) * MSEC_PER_SEC,
				       i2c_dev->timings.bus_freq_hz);

	int_mask = I2C_INT_NO_ACK | I2C_INT_ARBITRATION_LOST;
	tegra_i2c_unmask_irq(i2c_dev, int_mask);

	if (i2c_dev->dma_mode) {
		if (i2c_dev->msg_read) {
			dma_sync_single_for_device(i2c_dev->dma_dev,
						   i2c_dev->dma_phys,
						   xfer_size, DMA_FROM_DEVICE);

			err = tegra_i2c_dma_submit(i2c_dev, xfer_size);
			if (err)
				return err;
		} else {
			dma_sync_single_for_cpu(i2c_dev->dma_dev,
						i2c_dev->dma_phys,
						xfer_size, DMA_TO_DEVICE);
		}
	}

	tegra_i2c_push_packet_header(i2c_dev, msg, end_state);

	if (!i2c_dev->msg_read) {
		if (i2c_dev->dma_mode) {
			memcpy(i2c_dev->dma_buf + I2C_PACKET_HEADER_SIZE,
			       msg->buf, i2c_dev->msg_len);

			dma_sync_single_for_device(i2c_dev->dma_dev,
						   i2c_dev->dma_phys,
						   xfer_size, DMA_TO_DEVICE);

			err = tegra_i2c_dma_submit(i2c_dev, xfer_size);
			if (err)
				return err;
		} else {
			tegra_i2c_fill_tx_fifo(i2c_dev);
		}
	}

	if (i2c_dev->hw->has_per_pkt_xfer_complete_irq)
		int_mask |= I2C_INT_PACKET_XFER_COMPLETE;

	if (!i2c_dev->dma_mode) {
		if (msg->flags & I2C_M_RD)
			int_mask |= I2C_INT_RX_FIFO_DATA_REQ;
		else if (i2c_dev->msg_buf_remaining)
			int_mask |= I2C_INT_TX_FIFO_DATA_REQ;
	}

	tegra_i2c_unmask_irq(i2c_dev, int_mask);
	dev_dbg(i2c_dev->dev, "unmasked IRQ: %02x\n",
		i2c_readl(i2c_dev, I2C_INT_MASK));

	if (i2c_dev->dma_mode) {
		time_left = tegra_i2c_wait_completion(i2c_dev,
						      &i2c_dev->dma_complete,
						      xfer_time);

		/*
		 * Synchronize DMA first, since dmaengine_terminate_sync()
		 * performs synchronization after the transfer's termination
		 * and we want to get a completion if transfer succeeded.
		 */
		dmaengine_synchronize(i2c_dev->msg_read ?
				      i2c_dev->rx_dma_chan :
				      i2c_dev->tx_dma_chan);

		dmaengine_terminate_sync(i2c_dev->msg_read ?
					 i2c_dev->rx_dma_chan :
					 i2c_dev->tx_dma_chan);

		if (!time_left && !completion_done(&i2c_dev->dma_complete)) {
			dev_err(i2c_dev->dev, "DMA transfer timed out\n");
			tegra_i2c_init(i2c_dev);
			return -ETIMEDOUT;
		}

		if (i2c_dev->msg_read && i2c_dev->msg_err == I2C_ERR_NONE) {
			dma_sync_single_for_cpu(i2c_dev->dma_dev,
						i2c_dev->dma_phys,
						xfer_size, DMA_FROM_DEVICE);

			memcpy(i2c_dev->msg_buf, i2c_dev->dma_buf, i2c_dev->msg_len);
		}
	}

	time_left = tegra_i2c_wait_completion(i2c_dev, &i2c_dev->msg_complete,
					      xfer_time);

	tegra_i2c_mask_irq(i2c_dev, int_mask);

	if (time_left == 0) {
		dev_err(i2c_dev->dev, "I2C transfer timed out\n");
		tegra_i2c_init(i2c_dev);
		return -ETIMEDOUT;
	}

	dev_dbg(i2c_dev->dev, "transfer complete: %lu %d %d\n",
		time_left, completion_done(&i2c_dev->msg_complete),
		i2c_dev->msg_err);

	i2c_dev->dma_mode = false;

	err = tegra_i2c_error_recover(i2c_dev, msg);
	if (err)
		return err;

	return 0;
}

static int tegra_i2c_xfer(struct i2c_adapter *adap, struct i2c_msg msgs[],
			  int num)
{
	struct tegra_i2c_dev *i2c_dev = i2c_get_adapdata(adap);
	int i, ret;

	ret = pm_runtime_get_sync(i2c_dev->dev);
	if (ret < 0) {
		dev_err(i2c_dev->dev, "runtime resume failed %d\n", ret);
		pm_runtime_put_noidle(i2c_dev->dev);
		return ret;
	}

	for (i = 0; i < num; i++) {
		enum msg_end_type end_type = MSG_END_STOP;

		if (i < (num - 1)) {
			/* check whether follow up message is coming */
			if (msgs[i + 1].flags & I2C_M_NOSTART)
				end_type = MSG_END_CONTINUE;
			else
				end_type = MSG_END_REPEAT_START;
		}
		/* If M_RECV_LEN use ContinueXfer to read the first byte */
		if (msgs[i].flags & I2C_M_RECV_LEN) {
			ret = tegra_i2c_xfer_msg(i2c_dev, &msgs[i], MSG_END_CONTINUE);
			if (ret)
				break;
			/* Set the msg length from first byte */
			msgs[i].len += msgs[i].buf[0];
			dev_dbg(i2c_dev->dev, "reading %d bytes\n", msgs[i].len);
		}
		ret = tegra_i2c_xfer_msg(i2c_dev, &msgs[i], end_type);
		if (ret)
			break;
	}

	pm_runtime_put(i2c_dev->dev);

	return ret ?: i;
}

static int tegra_i2c_xfer_atomic(struct i2c_adapter *adap,
				 struct i2c_msg msgs[], int num)
{
	struct tegra_i2c_dev *i2c_dev = i2c_get_adapdata(adap);
	int ret;

	i2c_dev->atomic_mode = true;
	ret = tegra_i2c_xfer(adap, msgs, num);
	i2c_dev->atomic_mode = false;

	return ret;
}

static u32 tegra_i2c_func(struct i2c_adapter *adap)
{
	struct tegra_i2c_dev *i2c_dev = i2c_get_adapdata(adap);
	u32 ret = I2C_FUNC_I2C | (I2C_FUNC_SMBUS_EMUL & ~I2C_FUNC_SMBUS_QUICK) |
		  I2C_FUNC_10BIT_ADDR | I2C_FUNC_PROTOCOL_MANGLING;

	if (i2c_dev->hw->has_continue_xfer_support)
		ret |= I2C_FUNC_NOSTART | I2C_FUNC_SMBUS_READ_BLOCK_DATA;

	return ret;
}

static const struct i2c_algorithm tegra_i2c_algo = {
	.master_xfer		= tegra_i2c_xfer,
	.master_xfer_atomic	= tegra_i2c_xfer_atomic,
	.functionality		= tegra_i2c_func,
};

/* payload size is only 12 bit */
static const struct i2c_adapter_quirks tegra_i2c_quirks = {
	.flags = I2C_AQ_NO_ZERO_LEN,
	.max_read_len = SZ_4K,
	.max_write_len = SZ_4K - I2C_PACKET_HEADER_SIZE,
};

static const struct i2c_adapter_quirks tegra194_i2c_quirks = {
	.flags = I2C_AQ_NO_ZERO_LEN,
	.max_write_len = SZ_64K - I2C_PACKET_HEADER_SIZE,
};

static struct i2c_bus_recovery_info tegra_i2c_recovery_info = {
	.recover_bus = tegra_i2c_issue_bus_clear,
};

static const struct tegra_i2c_hw_feature tegra20_i2c_hw = {
	.has_continue_xfer_support = false,
	.has_per_pkt_xfer_complete_irq = false,
	.clk_divisor_hs_mode = 3,
	.clk_divisor_std_mode = 0,
	.clk_divisor_fast_mode = 0,
	.clk_divisor_fast_plus_mode = 0,
	.has_config_load_reg = false,
	.has_multi_master_mode = false,
	.has_slcg_override_reg = false,
	.has_mst_fifo = false,
	.quirks = &tegra_i2c_quirks,
	.supports_bus_clear = false,
	.has_apb_dma = true,
	.tlow_std_mode = 0x4,
	.thigh_std_mode = 0x2,
	.tlow_fast_fastplus_mode = 0x4,
	.thigh_fast_fastplus_mode = 0x2,
	.setup_hold_time_std_mode = 0x0,
	.setup_hold_time_fast_fast_plus_mode = 0x0,
	.setup_hold_time_hs_mode = 0x0,
	.has_interface_timing_reg = false,
};

static const struct tegra_i2c_hw_feature tegra30_i2c_hw = {
	.has_continue_xfer_support = true,
	.has_per_pkt_xfer_complete_irq = false,
	.clk_divisor_hs_mode = 3,
	.clk_divisor_std_mode = 0,
	.clk_divisor_fast_mode = 0,
	.clk_divisor_fast_plus_mode = 0,
	.has_config_load_reg = false,
	.has_multi_master_mode = false,
	.has_slcg_override_reg = false,
	.has_mst_fifo = false,
	.quirks = &tegra_i2c_quirks,
	.supports_bus_clear = false,
	.has_apb_dma = true,
	.tlow_std_mode = 0x4,
	.thigh_std_mode = 0x2,
	.tlow_fast_fastplus_mode = 0x4,
	.thigh_fast_fastplus_mode = 0x2,
	.setup_hold_time_std_mode = 0x0,
	.setup_hold_time_fast_fast_plus_mode = 0x0,
	.setup_hold_time_hs_mode = 0x0,
	.has_interface_timing_reg = false,
};

static const struct tegra_i2c_hw_feature tegra114_i2c_hw = {
	.has_continue_xfer_support = true,
	.has_per_pkt_xfer_complete_irq = true,
	.clk_divisor_hs_mode = 1,
	.clk_divisor_std_mode = 0x19,
	.clk_divisor_fast_mode = 0x19,
	.clk_divisor_fast_plus_mode = 0x10,
	.has_config_load_reg = false,
	.has_multi_master_mode = false,
	.has_slcg_override_reg = false,
	.has_mst_fifo = false,
	.quirks = &tegra_i2c_quirks,
	.supports_bus_clear = true,
	.has_apb_dma = true,
	.tlow_std_mode = 0x4,
	.thigh_std_mode = 0x2,
	.tlow_fast_fastplus_mode = 0x4,
	.thigh_fast_fastplus_mode = 0x2,
	.setup_hold_time_std_mode = 0x0,
	.setup_hold_time_fast_fast_plus_mode = 0x0,
	.setup_hold_time_hs_mode = 0x0,
	.has_interface_timing_reg = false,
};

static const struct tegra_i2c_hw_feature tegra124_i2c_hw = {
	.has_continue_xfer_support = true,
	.has_per_pkt_xfer_complete_irq = true,
	.clk_divisor_hs_mode = 1,
	.clk_divisor_std_mode = 0x19,
	.clk_divisor_fast_mode = 0x19,
	.clk_divisor_fast_plus_mode = 0x10,
	.has_config_load_reg = true,
	.has_multi_master_mode = false,
	.has_slcg_override_reg = true,
	.has_mst_fifo = false,
	.quirks = &tegra_i2c_quirks,
	.supports_bus_clear = true,
	.has_apb_dma = true,
	.tlow_std_mode = 0x4,
	.thigh_std_mode = 0x2,
	.tlow_fast_fastplus_mode = 0x4,
	.thigh_fast_fastplus_mode = 0x2,
	.setup_hold_time_std_mode = 0x0,
	.setup_hold_time_fast_fast_plus_mode = 0x0,
	.setup_hold_time_hs_mode = 0x0,
	.has_interface_timing_reg = true,
};

static const struct tegra_i2c_hw_feature tegra210_i2c_hw = {
	.has_continue_xfer_support = true,
	.has_per_pkt_xfer_complete_irq = true,
	.clk_divisor_hs_mode = 1,
	.clk_divisor_std_mode = 0x19,
	.clk_divisor_fast_mode = 0x19,
	.clk_divisor_fast_plus_mode = 0x10,
	.has_config_load_reg = true,
	.has_multi_master_mode = false,
	.has_slcg_override_reg = true,
	.has_mst_fifo = false,
	.quirks = &tegra_i2c_quirks,
	.supports_bus_clear = true,
	.has_apb_dma = true,
	.tlow_std_mode = 0x4,
	.thigh_std_mode = 0x2,
	.tlow_fast_fastplus_mode = 0x4,
	.thigh_fast_fastplus_mode = 0x2,
	.setup_hold_time_std_mode = 0,
	.setup_hold_time_fast_fast_plus_mode = 0,
	.setup_hold_time_hs_mode = 0,
	.has_interface_timing_reg = true,
};

static const struct tegra_i2c_hw_feature tegra186_i2c_hw = {
	.has_continue_xfer_support = true,
	.has_per_pkt_xfer_complete_irq = true,
	.clk_divisor_hs_mode = 1,
	.clk_divisor_std_mode = 0x16,
	.clk_divisor_fast_mode = 0x19,
	.clk_divisor_fast_plus_mode = 0x10,
	.has_config_load_reg = true,
	.has_multi_master_mode = false,
	.has_slcg_override_reg = true,
	.has_mst_fifo = false,
	.quirks = &tegra_i2c_quirks,
	.supports_bus_clear = true,
	.has_apb_dma = false,
	.tlow_std_mode = 0x4,
	.thigh_std_mode = 0x3,
	.tlow_fast_fastplus_mode = 0x4,
	.thigh_fast_fastplus_mode = 0x2,
	.setup_hold_time_std_mode = 0,
	.setup_hold_time_fast_fast_plus_mode = 0,
	.setup_hold_time_hs_mode = 0,
	.has_interface_timing_reg = true,
};

static const struct tegra_i2c_hw_feature tegra194_i2c_hw = {
	.has_continue_xfer_support = true,
	.has_per_pkt_xfer_complete_irq = true,
	.clk_divisor_hs_mode = 1,
	.clk_divisor_std_mode = 0x4f,
	.clk_divisor_fast_mode = 0x3c,
	.clk_divisor_fast_plus_mode = 0x16,
	.has_config_load_reg = true,
	.has_multi_master_mode = true,
	.has_slcg_override_reg = true,
	.has_mst_fifo = true,
	.quirks = &tegra194_i2c_quirks,
	.supports_bus_clear = true,
	.has_apb_dma = false,
	.tlow_std_mode = 0x8,
	.thigh_std_mode = 0x7,
	.tlow_fast_fastplus_mode = 0x2,
	.thigh_fast_fastplus_mode = 0x2,
	.setup_hold_time_std_mode = 0x08080808,
	.setup_hold_time_fast_fast_plus_mode = 0x02020202,
	.setup_hold_time_hs_mode = 0x090909,
	.has_interface_timing_reg = true,
};

static const struct of_device_id tegra_i2c_of_match[] = {
	{ .compatible = "nvidia,tegra194-i2c", .data = &tegra194_i2c_hw, },
	{ .compatible = "nvidia,tegra186-i2c", .data = &tegra186_i2c_hw, },
#if IS_ENABLED(CONFIG_ARCH_TEGRA_210_SOC)
	{ .compatible = "nvidia,tegra210-i2c-vi", .data = &tegra210_i2c_hw, },
#endif
	{ .compatible = "nvidia,tegra210-i2c", .data = &tegra210_i2c_hw, },
	{ .compatible = "nvidia,tegra124-i2c", .data = &tegra124_i2c_hw, },
	{ .compatible = "nvidia,tegra114-i2c", .data = &tegra114_i2c_hw, },
	{ .compatible = "nvidia,tegra30-i2c", .data = &tegra30_i2c_hw, },
	{ .compatible = "nvidia,tegra20-i2c", .data = &tegra20_i2c_hw, },
#if IS_ENABLED(CONFIG_ARCH_TEGRA_2x_SOC)
	{ .compatible = "nvidia,tegra20-i2c-dvc", .data = &tegra20_i2c_hw, },
#endif
	{},
};
MODULE_DEVICE_TABLE(of, tegra_i2c_of_match);

static void tegra_i2c_parse_dt(struct tegra_i2c_dev *i2c_dev)
{
	struct device_node *np = i2c_dev->dev->of_node;
	bool multi_mode;

	i2c_parse_fw_timings(i2c_dev->dev, &i2c_dev->timings, true);

	multi_mode = device_property_read_bool(i2c_dev->dev, "multi-master");
	i2c_dev->multimaster_mode = multi_mode;

	if (IS_ENABLED(CONFIG_ARCH_TEGRA_2x_SOC) &&
	    of_device_is_compatible(np, "nvidia,tegra20-i2c-dvc"))
		i2c_dev->is_dvc = true;

	if (IS_ENABLED(CONFIG_ARCH_TEGRA_210_SOC) &&
	    of_device_is_compatible(np, "nvidia,tegra210-i2c-vi"))
		i2c_dev->is_vi = true;
}

static int tegra_i2c_init_reset(struct tegra_i2c_dev *i2c_dev)
{
	if (ACPI_HANDLE(i2c_dev->dev))
		return 0;

	i2c_dev->rst = devm_reset_control_get_exclusive(i2c_dev->dev, "i2c");
	if (IS_ERR(i2c_dev->rst))
		return dev_err_probe(i2c_dev->dev, PTR_ERR(i2c_dev->rst),
				      "failed to get reset control\n");

	return 0;
}

static int tegra_i2c_init_clocks(struct tegra_i2c_dev *i2c_dev)
{
	int err;

	if (ACPI_HANDLE(i2c_dev->dev))
		return 0;

	i2c_dev->clocks[i2c_dev->nclocks++].id = "div-clk";

	if (i2c_dev->hw == &tegra20_i2c_hw || i2c_dev->hw == &tegra30_i2c_hw)
		i2c_dev->clocks[i2c_dev->nclocks++].id = "fast-clk";

	if (IS_VI(i2c_dev))
		i2c_dev->clocks[i2c_dev->nclocks++].id = "slow";

	err = devm_clk_bulk_get(i2c_dev->dev, i2c_dev->nclocks,
				i2c_dev->clocks);
	if (err)
		return err;

	err = clk_bulk_prepare(i2c_dev->nclocks, i2c_dev->clocks);
	if (err)
		return err;

	i2c_dev->div_clk = i2c_dev->clocks[0].clk;

	if (!i2c_dev->multimaster_mode)
		return 0;

	err = clk_enable(i2c_dev->div_clk);
	if (err) {
		dev_err(i2c_dev->dev, "failed to enable div-clk: %d\n", err);
		goto unprepare_clocks;
	}

	return 0;

unprepare_clocks:
	clk_bulk_unprepare(i2c_dev->nclocks, i2c_dev->clocks);

	return err;
}

static void tegra_i2c_release_clocks(struct tegra_i2c_dev *i2c_dev)
{
	if (i2c_dev->multimaster_mode)
		clk_disable(i2c_dev->div_clk);

	clk_bulk_unprepare(i2c_dev->nclocks, i2c_dev->clocks);
}

static int tegra_i2c_init_hardware(struct tegra_i2c_dev *i2c_dev)
{
	int ret;

	ret = pm_runtime_get_sync(i2c_dev->dev);
	if (ret < 0)
		dev_err(i2c_dev->dev, "runtime resume failed: %d\n", ret);
	else
		ret = tegra_i2c_init(i2c_dev);

	pm_runtime_put_sync(i2c_dev->dev);

	return ret;
}

static int tegra_i2c_probe(struct platform_device *pdev)
{
	struct tegra_i2c_dev *i2c_dev;
	struct resource *res;
	int err;

	i2c_dev = devm_kzalloc(&pdev->dev, sizeof(*i2c_dev), GFP_KERNEL);
	if (!i2c_dev)
		return -ENOMEM;

	platform_set_drvdata(pdev, i2c_dev);

	init_completion(&i2c_dev->msg_complete);
	init_completion(&i2c_dev->dma_complete);

	i2c_dev->hw = device_get_match_data(&pdev->dev);
	i2c_dev->cont_id = pdev->id;
	i2c_dev->dev = &pdev->dev;

	i2c_dev->base = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(i2c_dev->base))
		return PTR_ERR(i2c_dev->base);

	i2c_dev->base_phys = res->start;

	err = platform_get_irq(pdev, 0);
	if (err < 0)
		return err;

	i2c_dev->irq = err;

	/* interrupt will be enabled during of transfer time */
	irq_set_status_flags(i2c_dev->irq, IRQ_NOAUTOEN);

	err = devm_request_threaded_irq(i2c_dev->dev, i2c_dev->irq,
					NULL, tegra_i2c_isr,
					IRQF_NO_SUSPEND | IRQF_ONESHOT,
					dev_name(i2c_dev->dev), i2c_dev);
	if (err)
		return err;

	tegra_i2c_parse_dt(i2c_dev);

	err = tegra_i2c_init_reset(i2c_dev);
	if (err)
		return err;

	err = tegra_i2c_init_clocks(i2c_dev);
	if (err)
		return err;

	err = tegra_i2c_init_dma(i2c_dev);
	if (err)
		goto release_clocks;

	/*
	 * VI I2C is in VE power domain which is not always ON and not
	 * IRQ-safe.  Thus, IRQ-safe device shouldn't be attached to a
	 * non IRQ-safe domain because this prevents powering off the power
	 * domain.
	 *
	 * VI I2C device shouldn't be marked as IRQ-safe because VI I2C won't
	 * be used for atomic transfers. ACPI device is not IRQ safe also.
	 */
	if (!IS_VI(i2c_dev) && !has_acpi_companion(i2c_dev->dev))
		pm_runtime_irq_safe(i2c_dev->dev);

	pm_runtime_enable(i2c_dev->dev);

	err = tegra_i2c_init_hardware(i2c_dev);
	if (err)
		goto release_rpm;

	i2c_set_adapdata(&i2c_dev->adapter, i2c_dev);
	i2c_dev->adapter.dev.of_node = i2c_dev->dev->of_node;
	i2c_dev->adapter.dev.parent = i2c_dev->dev;
	i2c_dev->adapter.retries = 1;
	i2c_dev->adapter.timeout = 6 * HZ;
	i2c_dev->adapter.quirks = i2c_dev->hw->quirks;
	i2c_dev->adapter.owner = THIS_MODULE;
	i2c_dev->adapter.class = I2C_CLASS_DEPRECATED;
	i2c_dev->adapter.algo = &tegra_i2c_algo;
	i2c_dev->adapter.nr = pdev->id;

	if (i2c_dev->hw->supports_bus_clear)
		i2c_dev->adapter.bus_recovery_info = &tegra_i2c_recovery_info;

	strscpy(i2c_dev->adapter.name, dev_name(i2c_dev->dev),
		sizeof(i2c_dev->adapter.name));

	err = i2c_add_numbered_adapter(&i2c_dev->adapter);
	if (err)
		goto release_rpm;

	return 0;

release_rpm:
	pm_runtime_disable(i2c_dev->dev);

	tegra_i2c_release_dma(i2c_dev);
release_clocks:
	tegra_i2c_release_clocks(i2c_dev);

	return err;
}

static int tegra_i2c_remove(struct platform_device *pdev)
{
	struct tegra_i2c_dev *i2c_dev = platform_get_drvdata(pdev);

	i2c_del_adapter(&i2c_dev->adapter);
	pm_runtime_force_suspend(i2c_dev->dev);

	tegra_i2c_release_dma(i2c_dev);
	tegra_i2c_release_clocks(i2c_dev);

	return 0;
}

static int __maybe_unused tegra_i2c_runtime_resume(struct device *dev)
{
	struct tegra_i2c_dev *i2c_dev = dev_get_drvdata(dev);
	int err;

	err = pinctrl_pm_select_default_state(dev);
	if (err)
		return err;

	err = clk_bulk_enable(i2c_dev->nclocks, i2c_dev->clocks);
	if (err)
		return err;

	/*
	 * VI I2C device is attached to VE power domain which goes through
	 * power ON/OFF during runtime PM resume/suspend, meaning that
	 * controller needs to be re-initialized after power ON.
	 */
	if (IS_VI(i2c_dev)) {
		err = tegra_i2c_init(i2c_dev);
		if (err)
			goto disable_clocks;
	}

	return 0;

disable_clocks:
	clk_bulk_disable(i2c_dev->nclocks, i2c_dev->clocks);

	return err;
}

static int __maybe_unused tegra_i2c_runtime_suspend(struct device *dev)
{
	struct tegra_i2c_dev *i2c_dev = dev_get_drvdata(dev);

	clk_bulk_disable(i2c_dev->nclocks, i2c_dev->clocks);

	return pinctrl_pm_select_idle_state(dev);
}

static int __maybe_unused tegra_i2c_suspend(struct device *dev)
{
	struct tegra_i2c_dev *i2c_dev = dev_get_drvdata(dev);
	int err;

	i2c_mark_adapter_suspended(&i2c_dev->adapter);

	if (!pm_runtime_status_suspended(dev)) {
		err = tegra_i2c_runtime_suspend(dev);
		if (err)
			return err;
	}

	return 0;
}

static int __maybe_unused tegra_i2c_resume(struct device *dev)
{
	struct tegra_i2c_dev *i2c_dev = dev_get_drvdata(dev);
	int err;

	/*
	 * We need to ensure that clocks are enabled so that registers can be
	 * restored in tegra_i2c_init().
	 */
	err = tegra_i2c_runtime_resume(dev);
	if (err)
		return err;

	err = tegra_i2c_init(i2c_dev);
	if (err)
		return err;

	/*
	 * In case we are runtime suspended, disable clocks again so that we
	 * don't unbalance the clock reference counts during the next runtime
	 * resume transition.
	 */
	if (pm_runtime_status_suspended(dev)) {
		err = tegra_i2c_runtime_suspend(dev);
		if (err)
			return err;
	}

	i2c_mark_adapter_resumed(&i2c_dev->adapter);

	return 0;
}

static const struct dev_pm_ops tegra_i2c_pm = {
	SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(tegra_i2c_suspend, tegra_i2c_resume)
	SET_RUNTIME_PM_OPS(tegra_i2c_runtime_suspend, tegra_i2c_runtime_resume,
			   NULL)
};

static const struct acpi_device_id tegra_i2c_acpi_match[] = {
	{.id = "NVDA0101", .driver_data = (kernel_ulong_t)&tegra210_i2c_hw},
	{.id = "NVDA0201", .driver_data = (kernel_ulong_t)&tegra186_i2c_hw},
	{.id = "NVDA0301", .driver_data = (kernel_ulong_t)&tegra194_i2c_hw},
	{ }
};
MODULE_DEVICE_TABLE(acpi, tegra_i2c_acpi_match);

static struct platform_driver tegra_i2c_driver = {
	.probe = tegra_i2c_probe,
	.remove = tegra_i2c_remove,
	.driver = {
		.name = "tegra-i2c",
		.of_match_table = tegra_i2c_of_match,
		.acpi_match_table = tegra_i2c_acpi_match,
		.pm = &tegra_i2c_pm,
	},
};
module_platform_driver(tegra_i2c_driver);

MODULE_DESCRIPTION("NVIDIA Tegra I2C Bus Controller driver");
MODULE_AUTHOR("Colin Cross");
MODULE_LICENSE("GPL v2");
