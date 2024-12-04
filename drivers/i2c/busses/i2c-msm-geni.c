// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/dma-mapping.h>
#include <linux/qcom-geni-se-common.h>
#include <linux/ipc_logging.h>
#include <linux/dmaengine.h>
#include <linux/msm_gpi.h>
#include <linux/ioctl.h>
#include <linux/pinctrl/consumer.h>
#include <linux/sched/clock.h>
#include <linux/slab.h>
#include <linux/bootmarker_kernel.h>

#define SE_GENI_TEST_BUS_CTRL	0x44
#define SE_NUM_FOR_TEST_BUS	5

#define SE_GENI_CFG_REG68		(0x210)
#define SE_I2C_TX_TRANS_LEN		(0x26C)
#define SE_I2C_RX_TRANS_LEN		(0x270)
#define SE_I2C_SCL_COUNTERS		(0x278)
#define SE_GENI_M_GP_LENGTH		(0x910)

/* M_CMD OP codes for I2C */
#define I2C_WRITE		(0x1)
#define I2C_READ		(0x2)
#define I2C_WRITE_READ		(0x3)
#define I2C_ADDR_ONLY		(0x4)
#define I2C_BUS_CLEAR		(0x6)
#define I2C_STOP_ON_BUS		(0x7)
/* M_CMD params for I2C */
#define PRE_CMD_DELAY		(BIT(0))
#define TIMESTAMP_BEFORE	(BIT(1))
#define STOP_STRETCH		(BIT(2))
#define TIMESTAMP_AFTER		(BIT(3))
#define POST_COMMAND_DELAY	(BIT(4))
#define IGNORE_ADD_NACK		(BIT(6))
#define READ_FINISHED_WITH_ACK	(BIT(7))
#define BYPASS_ADDR_PHASE	(BIT(8))
#define SLV_ADDR_MSK		(GENMASK(15, 9))
#define SLV_ADDR_SHFT		(9)

#define I2C_PACK_EN		(BIT(0) | BIT(1))
#define GP_IRQ0			0
#define GP_IRQ1			1
#define GP_IRQ2			2
#define GP_IRQ3			3
#define GP_IRQ4			4
#define GP_IRQ5			5
#define GENI_OVERRUN		6
#define GENI_ILLEGAL_CMD	7
#define GENI_ABORT_DONE		8
#define GENI_TIMEOUT		9

#define GENI_HW_PARAM			0x50
#define GENI_SPURIOUS_IRQ	10
#define I2C_ADDR_NACK		11
#define I2C_DATA_NACK		12
#define GENI_M_CMD_FAILURE	13
#define GSI_TRE_FULL		14

#define I2C_NACK		GP_IRQ1
#define I2C_BUS_PROTO		GP_IRQ3
#define I2C_ARB_LOST		GP_IRQ4
#define DM_I2C_CB_ERR		((BIT(GP_IRQ1) | BIT(GP_IRQ3) | BIT(GP_IRQ4)) \
									<< 5)

#define I2C_MASTER_HUB		(BIT(0))

#define KHz(freq)		(1000 * freq)
#define I2C_AUTO_SUSPEND_DELAY	250

#define I2C_TIMEOUT_SAFETY_COEFFICIENT	10

#define I2C_TIMEOUT_MIN_USEC	500000

#define MAX_SE	20

#define I2C_LOG_DBG(log_ctx, print, dev, x...) do { \
GENI_SE_DBG(log_ctx, print, dev, x); \
if (dev) \
	i2c_trace_log(dev, x); \
} while (0)

#define I2C_LOG_ERR(log_ctx, print, dev, x...) do { \
GENI_SE_ERR(log_ctx, print, dev, x); \
if (dev) \
	i2c_trace_log(dev, x); \
} while (0)

#define CREATE_TRACE_POINTS
#include "i2c-qup-trace.h"

#define I2C_HUB_DEF	0

/* As per dtsi max tre's configured as 1024
 * for multi descriptor usecase we can submit upto 512 tre's
 * this includes go tre and dma tre.
 * we need additional space for config tre's and lock/unlock tre's sometimes.
 * so to support other config related tre's provided 64 tre's extra space
 * for data xfers using 512 - 64 = 448 tre's, rest 64 for config/lock/unlock etc.
 */
#define MAX_NUM_TRE_MSGS	448
#define NUM_TRE_MSGS_PER_INTR	64
#define IMMEDIATE_DMA_LEN	8

#define MIN_NUM_MSGS_FOR_MULTI_DESC_MODE	4
#define BOOT_MARKER_SIZE	50

/* FTRACE Logging */
void i2c_trace_log(struct device *dev, const char *fmt, ...)
{
	struct va_format vaf = {
		.fmt = fmt,
	};

	va_list args;

	va_start(args, fmt);
	vaf.va = &args;
	trace_i2c_log_info(dev_name(dev), &vaf);
	va_end(args);
}

enum i2c_se_mode {
	UNINITIALIZED,
	FIFO_SE_DMA,
	GSI_ONLY,
};

enum gsi_error {
	GENI_I2C_SUCCESS,
	GENI_I2C_GSI_XFER_OUT,
	GENI_I2C_ERR_PREP_SG,
};

struct dbg_buf_ctxt {
	void *virt_buf;
	void *map_buf;
};

struct gsi_i2c_tre_queue {
	u32 msg_cnt; /* transmitted tre msg count */
	u32 tre_freed_cnt;
	bool is_multi_descriptor;
	atomic_t irq_cnt;
	u32 unmap_cnt;
	u8 *dma_buf[MAX_NUM_TRE_MSGS];
};

struct geni_i2c_ssr {
	struct mutex ssr_lock; /* SSR execution process */
	bool is_ssr_down;
};

struct geni_i2c_dev {
	struct device *dev;
	void __iomem *base;
	unsigned int tx_wm;
	int irq;
	int err;
	u32 xfer_timeout;
	struct i2c_adapter adap;
	struct completion xfer;
	struct completion m_cancel_cmd;
	struct i2c_msg *cur;
	struct i2c_msg *msgs;
	struct gsi_i2c_tre_queue gsi_tx;
	struct geni_se i2c_rsc;
	struct clk *m_ahb_clk;
	struct clk *s_ahb_clk;
	struct clk *core_clk;
	int cur_wr;
	int cur_rd;
	struct device *wrapper_dev;
	void *ipcl;
	void *ipc_log_kpi;
	int i2c_kpi;
	int clk_fld_idx;
	struct dma_chan *tx_c;
	struct dma_chan *rx_c;
	struct msm_gpi_tre lock_t;
	struct msm_gpi_tre unlock_t;
	struct msm_gpi_tre cfg0_t;
	struct msm_gpi_tre go_t;
	struct msm_gpi_tre tx_t;
	struct msm_gpi_tre rx_t;
	dma_addr_t tx_ph[MAX_NUM_TRE_MSGS];
	dma_addr_t rx_ph;
	struct msm_gpi_ctrl tx_ev;
	struct msm_gpi_ctrl rx_ev;
	struct scatterlist *tx_sg; /* lock, cfg0, go, TX, unlock */
	struct scatterlist *rx_sg;
	dma_addr_t tx_sg_dma;
	dma_addr_t rx_sg_dma;
	int cfg_sent;
	int clk_freq_out;
	struct dma_async_tx_descriptor *tx_desc;
	struct dma_async_tx_descriptor *rx_desc;
	struct msm_gpi_dma_async_tx_cb_param tx_cb;
	struct msm_gpi_dma_async_tx_cb_param rx_cb;
	enum i2c_se_mode se_mode;
	bool cmd_done;
	bool is_shared;
	bool is_high_perf; /* To increase the performance voting for higher BW valuest */
	u32 dbg_num;
	struct dbg_buf_ctxt *dbg_buf_ptr;
	bool is_le_vm;
	bool is_gsi_cmd;
	bool pm_ctrl_client;
	bool req_chan;
	bool first_xfer_done; /* for le-vm doing lock/unlock, after first xfer initiated. */
	bool le_gpi_reset_done;
	bool is_i2c_hub;
	bool prev_cancel_pending; //Halt cancel till IOS in good state
	bool gsi_err; /* For every gsi error performing gsi reset */
	bool is_i2c_rtl_based; /* doing pending cancel only for rtl based SE's */
	bool skip_bw_vote; /* Used for PMIC over i2c use case to skip the BW vote */
	atomic_t is_xfer_in_progress; /* Used to maintain xfer inprogress status */
	bool bus_recovery_enable; /* To be enabled by client if needed */
	bool i2c_test_dev; /* Set this DT flag to enable test bus dump for an SE */
	bool is_deep_sleep; /* For deep sleep restore the config similar to the probe. */
	struct geni_i2c_ssr i2c_ssr;
	struct geni_se_rsc  rsc;
};

static struct geni_i2c_dev *gi2c_dev_dbg[MAX_SE];
static int arr_idx;
static int geni_i2c_runtime_suspend(struct device *dev);

static void ssr_i2c_force_suspend(struct device *dev);
static void ssr_i2c_force_resume(struct device *dev);

struct geni_i2c_err_log {
	int err;
	const char *msg;
};

static struct geni_i2c_err_log gi2c_log[] = {
	[GP_IRQ0] = {-EINVAL, "Unknown I2C err GP_IRQ0"},
	[I2C_ADDR_NACK] = {-ENOTCONN,
			"Address NACK: slv unresponsive, check its power/reset-ln"},
	[I2C_DATA_NACK] = {-ENOTCONN,
			"Data NACK: Device NACK before end of TX transfer"},
	[GP_IRQ2] = {-EINVAL, "Unknown I2C err GP IRQ2"},
	[I2C_BUS_PROTO] = {-EPROTO,
				"Bus proto err, noisy/unepxected start/stop"},
	[I2C_ARB_LOST] = {-EBUSY,
				"Bus arbitration lost, clock line undriveable"},
	[GP_IRQ5] = {-EINVAL, "Unknown I2C err GP IRQ5"},
	[GENI_OVERRUN] = {-EIO, "Cmd overrun, check GENI cmd-state machine"},
	[GENI_ILLEGAL_CMD] = {-EILSEQ,
				"Illegal cmd, check GENI cmd-state machine"},
	[GENI_ABORT_DONE] = {-ETIMEDOUT, "Abort after timeout successful"},
	[GENI_TIMEOUT] = {-ETIMEDOUT, "I2C TXN timed out"},
	[GENI_SPURIOUS_IRQ] = {-EINVAL, "Received unexpected interrupt"},
	[GENI_M_CMD_FAILURE] = {-EINVAL, "Master command failure"},
	[GSI_TRE_FULL] = {-EINVAL, "GSI TRE FULL NO SPACE"},
};

struct geni_i2c_clk_fld {
	u32	clk_freq_out;
	u8	clk_div;
	u8	t_high;
	u8	t_low;
	u8	t_cycle;
};

static struct geni_i2c_clk_fld geni_i2c_clk_map[] = {
	{KHz(100), 7, 10, 12, 26},
	{KHz(400), 2,  3, 11, 22},
	{KHz(1000), 1, 2,  8, 18},
};

static struct geni_i2c_clk_fld geni_i2c_hub_clk_map[] = {
	{KHz(100), 7, 10, 11, 26},
	{KHz(400), 2,  7, 10, 24},
	{KHz(1000), 1, 3,  9, 18},
};

static int geni_i2c_clk_map_idx(struct geni_i2c_dev *gi2c)
{
	int i;
	int ret = 0;
	bool clk_map_present = false;
	struct geni_i2c_clk_fld *itr;

	itr = (gi2c->is_i2c_rtl_based) ? geni_i2c_hub_clk_map : geni_i2c_clk_map;

	for (i = 0; i < ARRAY_SIZE(geni_i2c_clk_map); i++, itr++) {
		if (itr->clk_freq_out == gi2c->clk_freq_out) {
			clk_map_present = true;
			break;
		}
	}

	if (clk_map_present)
		gi2c->clk_fld_idx = i;
	else
		ret = -EINVAL;

	return ret;
}

/**
 * geni_i2c_se_dump_dbg_regs() - Print relevant registers that capture most
 *			accurately the state of an SE.
 * @se:			Pointer to the concerned serial engine.
 * @iomem:		Base address of the SE's register space.
 * @ipc:		IPC log context handle.
 *
 * This function is used to print out all the registers that capture the state
 * of an SE to help debug any errors.
 *
 * Return:	None
 */
void geni_i2c_se_dump_dbg_regs(struct geni_se *se, void __iomem *base,
				void *ipc)
{
	u32 m_cmd0 = 0;
	u32 m_irq_status = 0;
	u32 s_cmd0 = 0;
	u32 s_irq_status = 0;
	u32 geni_status = 0;
	u32 geni_ios = 0;
	u32 dma_rx_irq = 0;
	u32 dma_tx_irq = 0;
	u32 rx_fifo_status = 0;
	u32 tx_fifo_status = 0;
	u32 se_dma_dbg = 0;
	u32 m_cmd_ctrl = 0;
	u32 se_dma_rx_len = 0;
	u32 se_dma_rx_len_in = 0;
	u32 se_dma_tx_len = 0;
	u32 se_dma_tx_len_in = 0;
	u32 geni_m_irq_en = 0;
	u32 geni_s_irq_en = 0;
	u32 geni_dma_tx_irq_en = 0;
	u32 geni_dma_rx_irq_en = 0;
	u32 geni_general_cfg = 0;
	u32 geni_output_ctrl = 0;
	u32 geni_clk_ctrl_ro = 0;
	u32 fifo_if_disable_ro = 0;
	u32 geni_fw_multilock_msa_ro = 0;
	u32 geni_clk_sel = 0;
	u32 m_irq_en = 0;
	u32 se_dma_tx_attr = 0;
	u32 se_dma_tx_irq_stat = 0;
	u32 se_dma_rx_attr = 0;
	u32 se_dma_rx_irq_stat = 0;
	u32 se_gsi_event_en = 0;
	u32 se_irq_en = 0;
	u32 dma_if_en_ro = 0;
	u32 dma_general_cfg = 0;
	u32 dma_debug_reg0 = 0;

	m_cmd0 = geni_read_reg(base, SE_GENI_M_CMD0);
	m_irq_status = geni_read_reg(base, SE_GENI_M_IRQ_STATUS);
	s_cmd0 = geni_read_reg(base, SE_GENI_S_CMD0);
	s_irq_status = geni_read_reg(base, SE_GENI_S_IRQ_STATUS);
	geni_status = geni_read_reg(base, SE_GENI_STATUS);
	geni_ios = geni_read_reg(base, SE_GENI_IOS);
	dma_tx_irq = geni_read_reg(base, SE_DMA_TX_IRQ_STAT);
	dma_rx_irq = geni_read_reg(base, SE_DMA_RX_IRQ_STAT);
	rx_fifo_status = geni_read_reg(base, SE_GENI_RX_FIFO_STATUS);
	tx_fifo_status = geni_read_reg(base, SE_GENI_TX_FIFO_STATUS);
	se_dma_dbg = geni_read_reg(base, SE_DMA_DEBUG_REG0);
	m_cmd_ctrl = geni_read_reg(base, SE_GENI_M_CMD_CTRL_REG);
	se_dma_rx_len = geni_read_reg(base, SE_DMA_RX_LEN);
	se_dma_rx_len_in = geni_read_reg(base, SE_DMA_RX_LEN_IN);
	se_dma_tx_len = geni_read_reg(base, SE_DMA_TX_LEN);
	se_dma_tx_len_in = geni_read_reg(base, SE_DMA_TX_LEN_IN);
	geni_m_irq_en = geni_read_reg(base, SE_GENI_M_IRQ_EN);
	geni_s_irq_en = geni_read_reg(base, SE_GENI_S_IRQ_EN);
	geni_dma_tx_irq_en = geni_read_reg(base, SE_DMA_TX_IRQ_EN);
	geni_dma_rx_irq_en = geni_read_reg(base, SE_DMA_RX_IRQ_EN);
	geni_general_cfg = geni_read_reg(base, GENI_GENERAL_CFG);
	geni_output_ctrl = geni_read_reg(base, GENI_OUTPUT_CTRL);
	geni_clk_ctrl_ro = geni_read_reg(base, GENI_CLK_CTRL_RO);
	fifo_if_disable_ro = geni_read_reg(base, GENI_IF_DISABLE_RO);
	geni_fw_multilock_msa_ro = geni_read_reg(base, GENI_FW_MULTILOCK_MSA_RO);
	geni_clk_sel = geni_read_reg(base, SE_GENI_CLK_SEL);
	m_irq_en = geni_read_reg(base, SE_GENI_M_IRQ_EN);
	se_dma_tx_attr = geni_read_reg(base, SE_DMA_TX_ATTR);
	se_dma_tx_irq_stat = geni_read_reg(base, SE_DMA_TX_IRQ_STAT);
	se_dma_rx_attr = geni_read_reg(base, SE_DMA_RX_ATTR);
	se_dma_rx_irq_stat = geni_read_reg(base, SE_DMA_RX_IRQ_STAT);
	se_gsi_event_en = geni_read_reg(base, SE_GSI_EVENT_EN);
	se_irq_en = geni_read_reg(base, SE_IRQ_EN);
	dma_if_en_ro = geni_read_reg(base, DMA_IF_EN_RO);
	dma_general_cfg = geni_read_reg(base, DMA_GENERAL_CFG);
	dma_debug_reg0 = geni_read_reg(base, SE_DMA_DEBUG_REG0);

	I2C_LOG_DBG(ipc, false, se->dev,
		    "%s: m_cmd0:0x%x, m_irq_status:0x%x, geni_status:0x%x, geni_ios:0x%x\n",
		    __func__, m_cmd0, m_irq_status, geni_status, geni_ios);
	I2C_LOG_DBG(ipc, false, se->dev,
		    "dma_rx_irq:0x%x, dma_tx_irq:0x%x, rx_fifo_sts:0x%x, tx_fifo_sts:0x%x\n",
		    dma_rx_irq, dma_tx_irq, rx_fifo_status, tx_fifo_status);
	I2C_LOG_DBG(ipc, false, se->dev,
		    "se_dma_dbg:0x%x, m_cmd_ctrl:0x%x, dma_rxlen:0x%x, dma_rxlen_in:0x%x\n",
		    se_dma_dbg, m_cmd_ctrl, se_dma_rx_len, se_dma_rx_len_in);
	I2C_LOG_DBG(ipc, false, se->dev,
		    "dma_txlen:0x%x, dma_txlen_in:0x%x s_irq_status:0x%x\n",
		    se_dma_tx_len, se_dma_tx_len_in, s_irq_status);
	I2C_LOG_DBG(ipc, false, se->dev,
		    "dma_txirq_en:0x%x, dma_rxirq_en:0x%x geni_m_irq_en:0x%x geni_s_irq_en:0x%x\n",
		    geni_dma_tx_irq_en, geni_dma_rx_irq_en, geni_m_irq_en, geni_s_irq_en);
	I2C_LOG_DBG(ipc, false, se->dev,
		    "geni_dma_tx_irq_en:0x%x, geni_dma_rx_irq_en:0x%x, geni_general_cfg:0x%x\n",
		    geni_dma_tx_irq_en, geni_dma_rx_irq_en, geni_general_cfg);
	I2C_LOG_DBG(ipc, false, se->dev,
		    "geni_clk_ctrl_ro:0x%x, fifo_if_disable_ro:0x%x, geni_fw_multilock_msa_ro:0x%x\n",
		    geni_clk_ctrl_ro, fifo_if_disable_ro, geni_fw_multilock_msa_ro);
	I2C_LOG_DBG(ipc, false, se->dev,
		    "m_irq_en:0x%x, se_dma_tx_attr:0x%x se_dma_tx_irq_stat:0x%x, geni_output_ctrl:0x%x\n",
		     m_irq_en, se_dma_tx_attr, se_dma_tx_irq_stat, geni_output_ctrl);
	I2C_LOG_DBG(ipc, false, se->dev,
		    "se_dma_rx_attr:0x%x, se_dma_rx_irq_stat:0x%x se_gsi_event_en:0x%x se_irq_en:0x%x\n",
		    se_dma_rx_attr, se_dma_rx_irq_stat, se_gsi_event_en, se_irq_en);
	I2C_LOG_DBG(ipc, false, se->dev,
		    "dma_if_en_ro:0x%x, dma_general_cfg:0x%x dma_debug_reg0:0x%x\n, geni_clk_sel:0x%x",
		    dma_if_en_ro, dma_general_cfg, dma_debug_reg0, geni_clk_sel);
}

/*
 * capture_kpi_show() - Prints the value stored in capture_kpi sysfs entry
 *
 * @dev: pointer to device
 * @attr: device attributes
 * @buf: buffer to store the capture_kpi_value
 *
 * Return: prints capture_kpi value or error value
 */
static ssize_t capture_kpi_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct geni_i2c_dev *gi2c = platform_get_drvdata(pdev);

	if (!gi2c)
		return -EINVAL;

	return scnprintf(buf, sizeof(int), "%d\n", gi2c->i2c_kpi);
}

/*
 * capture_kpi_store() - store the capture_kpi sysfs value
 *
 * @dev: pointer to device
 * @attr: device attributes
 * @buf: buffer to store the capture_kpi_value
 * @size: returns the value of size.
 *
 * Return: Size copied in the buffer or error value
 */
static ssize_t capture_kpi_store(struct device *dev,
				 struct device_attribute *attr, const char *buf,
				 size_t size)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct geni_i2c_dev *gi2c = platform_get_drvdata(pdev);
	char name[36];

	if (!gi2c)
		return -EINVAL;

	if (kstrtoint(buf, 0, &gi2c->i2c_kpi)) {
		dev_err(dev, "Invalid input\n");
		return -EINVAL;
	}

	/* ipc logs for kpi's measure */
	if (gi2c->i2c_kpi && !gi2c->ipc_log_kpi) {
		memset(name, 0, sizeof(name));
		scnprintf(name, sizeof(name), "%s%s", dev_name(gi2c->dev), "_kpi");
		gi2c->ipc_log_kpi = ipc_log_context_create(IPC_LOG_KPI_PAGES, name, 0);
		if (!gi2c->ipc_log_kpi && IS_ENABLED(CONFIG_IPC_LOGGING))
			dev_err(&pdev->dev, "Error creating kpi IPC logs\n");
	}

	return size;
}
static DEVICE_ATTR_RW(capture_kpi);

static inline void qcom_geni_i2c_conf(struct geni_i2c_dev *gi2c, int dfs)
{
	struct geni_i2c_clk_fld *itr;

	if (gi2c->is_i2c_rtl_based)
		itr = geni_i2c_hub_clk_map + gi2c->clk_fld_idx;
	else
		itr = geni_i2c_clk_map + gi2c->clk_fld_idx;

	/* do not configure the dfs index for i2c hub master */
	if (!gi2c->is_i2c_hub)
		geni_write_reg(dfs, gi2c->base, SE_GENI_CLK_SEL);

	geni_write_reg((itr->clk_div << 4) | 1, gi2c->base, GENI_SER_M_CLK_CFG);
	geni_write_reg(((itr->t_high << 20) | (itr->t_low << 10) |
			itr->t_cycle), gi2c->base, SE_I2C_SCL_COUNTERS);

	/*
	 * Ensure Clk config completes before return.
	 */
	mb();
}

static inline void qcom_geni_i2c_calc_timeout(struct geni_i2c_dev *gi2c)
{

	struct geni_i2c_clk_fld *clk_itr;

	size_t bit_cnt = gi2c->cur->len*9;
	size_t bit_usec = 0;
	size_t xfer_max_usec = 0;

	if (gi2c->is_i2c_rtl_based)
		clk_itr = geni_i2c_hub_clk_map + gi2c->clk_fld_idx;
	else
		clk_itr = geni_i2c_clk_map + gi2c->clk_fld_idx;

	bit_usec = (bit_cnt * USEC_PER_SEC) / clk_itr->clk_freq_out;
	xfer_max_usec = (bit_usec * I2C_TIMEOUT_SAFETY_COEFFICIENT) +
							I2C_TIMEOUT_MIN_USEC;
	gi2c->xfer_timeout = usecs_to_jiffies(xfer_max_usec);
	I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev,
		    "%s: us:%d jiffies:%d\n",
		    __func__, xfer_max_usec, gi2c->xfer_timeout);
}

/*
 * geni_se_select_test_bus: Selects the test bus as required
 *
 * @gi2c_dev: Geni I2C device handle
 * test_bus_num: Test bus number to select (1 to 16)
 *
 * Return: Nogeni_se_select_test_busne
 */
static void geni_se_select_test_bus(struct geni_i2c_dev *gi2c, u8 test_bus_num)
{
	I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev,
		    "%s: test_bus:%d\n", __func__, test_bus_num);
	writel_relaxed(test_bus_num, gi2c->base + SE_GENI_TEST_BUS_CTRL);
}

static void geni_i2c_err(struct geni_i2c_dev *gi2c, int err)
{
	if (err == I2C_DATA_NACK || err == I2C_ADDR_NACK
			|| err == GENI_ABORT_DONE) {
		I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev, "%s\n",
			    gi2c_log[err].msg);
		goto err_ret;
	} else {
		I2C_LOG_ERR(gi2c->ipcl, false, gi2c->dev, "%s\n",
			    gi2c_log[err].msg);
	}
	geni_i2c_se_dump_dbg_regs(&gi2c->i2c_rsc, gi2c->base, gi2c->ipcl);
err_ret:
	gi2c->err = gi2c_log[err].err;
}

/*
 * geni_i2c_test_bus_dump(): Dumps or reads test bus for selected SE test bus.
 *
 * @gi2c_i2c_dev: Handle to SE device
 * @se_num: SE number, which start from 0.
 *
 * Return: None
 *
 * Note: This function has added extra test buses for refrences.
 */
static void geni_i2c_test_bus_dump(struct geni_i2c_dev *gi2c, u8 se_num)
{
	/* Select test bus number and test bus, then read test bus.*/

	/* geni_m_comp_sig_test_bus */
	geni_se_select_test_bus(gi2c, 8);
	test_bus_select_per_qupv3(gi2c->wrapper_dev, se_num, gi2c->ipcl);
	test_bus_read_per_qupv3(gi2c->wrapper_dev, gi2c->ipcl);

	/* geni_m_branch_cond_1_test_bus */
	geni_se_select_test_bus(gi2c, 5);
	test_bus_select_per_qupv3(gi2c->wrapper_dev, se_num, gi2c->ipcl);
	test_bus_read_per_qupv3(gi2c->wrapper_dev, gi2c->ipcl);

	/* Can Add more here based on debug ask. */
}

static void do_reg68_war_for_rtl_se(struct geni_i2c_dev *gi2c)
{
	u32 status;

	//Add REG68 WAR if stretch bit is set
	status = geni_read_reg(gi2c->base, SE_GENI_M_CMD0);
	GENI_SE_DBG(gi2c->ipcl, false, gi2c->dev,
		    "%s: SE_GENI_M_CMD0:0x%x\n", __func__,  status);

	//BIT(2) - STOP/STRETCH set then configure REG68 register
	if ((status & 0x4) && gi2c->is_i2c_rtl_based) {
		status = geni_read_reg(gi2c->base, SE_GENI_CFG_REG68);
		GENI_SE_DBG(gi2c->ipcl, false, gi2c->dev,
			    "%s: Before WAR REG68:0x%x\n", __func__, status);
		if (status & 0x20) {
			//Toggle Bit#4, Bit#5 of REG68 to disable/enable stretch
			geni_write_reg(0x00100110, gi2c->base,
				       SE_GENI_CFG_REG68);
		} else {
			//Restore FW to suggested value i.e. 0x00100120
			geni_write_reg(0x00100120, gi2c->base,
				       SE_GENI_CFG_REG68);
		}
		status = geni_read_reg(gi2c->base, SE_GENI_CFG_REG68);
		GENI_SE_DBG(gi2c->ipcl, false, gi2c->dev,
			    "%s: After WAR REG68:0x%x\n", __func__, status);
	}
}

/**
 * geni_i2c_stop_with_cancel(): stops GENI SE with cancel command.
 * @gi2c: I2C dev handle
 *
 * This is a generic function to stop serial engine, to be called as required.
 *
 * Return: 0 if Success, non zero value if failed.
 */
static int geni_i2c_stop_with_cancel(struct geni_i2c_dev *gi2c)
{
	int timeout = 0;

	/* Issue point for e.g.: dump test bus/read test bus */
	if (gi2c->i2c_test_dev)
		/* For se4, its 5 as SE num starts from 0 */
		geni_i2c_test_bus_dump(gi2c, SE_NUM_FOR_TEST_BUS);

	reinit_completion(&gi2c->m_cancel_cmd);
	geni_se_cancel_m_cmd(&gi2c->i2c_rsc);
	timeout = wait_for_completion_timeout(&gi2c->m_cancel_cmd, HZ);
	if (!timeout) {
		I2C_LOG_DBG(gi2c->ipcl, true, gi2c->dev,
			    "%s:Cancel failed\n", __func__);
		reinit_completion(&gi2c->xfer);
		geni_se_abort_m_cmd(&gi2c->i2c_rsc);
		timeout = wait_for_completion_timeout(&gi2c->xfer, HZ);
		if (!timeout) {
			I2C_LOG_DBG(gi2c->ipcl, true, gi2c->dev,
				    "%s:Abort failed\n", __func__);
			return !timeout;
		}
	}

	return 0;
}

/**
 * geni_i2c_is_bus_recovery_required: Checks if Bus recovery enabled/required ?
 * @gi2c: Handle of the I2C device
 *
 * Return: TRUE if SDA is stuck LOW due to some issue else false.
 *
 */
static bool geni_i2c_is_bus_recovery_required(struct geni_i2c_dev *gi2c)
{
	u32 geni_ios = readl_relaxed(gi2c->base + SE_GENI_IOS);

	/*
	 * SE_GENI_IOS will show I2C CLK/SDA line status, BIT 0 is SDA and
	 * BIT 1 is clk status. SE_GENI_IOS register set when CLK/SDA line
	 * is pulled high.
	 */
	return (((geni_ios & 1) == 0) && (gi2c->err == -EPROTO ||
					  gi2c->err == -EBUSY ||
					  gi2c->err == -ETIMEDOUT));
}

/**
 * geni_i2c_bus_recovery(): Function to recover i2c bus when required
 * @gi2c:	I2C device handle
 *
 * Use this function only when bus is bad for some reason and need to
 * reset the slave to bring slave into proper state. This should put
 * bus into proper state once executed successfully.
 *
 * Return: Success OR Respective error code/value.
 */
static int geni_i2c_bus_recovery(struct geni_i2c_dev *gi2c)
{
	int timeout = 0, ret = 0;
	u32 m_param = 0, m_cmd = 0;
	unsigned long long start_time;

	start_time = geni_capture_start_time(&gi2c->i2c_rsc, gi2c->ipc_log_kpi, __func__,
					     gi2c->i2c_kpi);

	/* Must be enabled by client "only" if required. */
	if (gi2c->bus_recovery_enable &&
	    geni_i2c_is_bus_recovery_required(gi2c)) {
		GENI_SE_ERR(gi2c->ipcl, false, gi2c->dev,
			    "%d:SDA Line stuck\n", gi2c->err);
	} else {
		GENI_SE_DBG(gi2c->ipcl, false, gi2c->dev,
			    "Bus Recovery not required/enabled\n");
		return 0;
	}

	I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev, "%s: start recovery\n",
		    __func__);
	/* BUS_CLEAR */
	reinit_completion(&gi2c->xfer);
	m_cmd = I2C_BUS_CLEAR;
	geni_se_setup_m_cmd(&gi2c->i2c_rsc, m_cmd, m_param);
	timeout = wait_for_completion_timeout(&gi2c->xfer, HZ);
	if (!timeout) {
		geni_i2c_err(gi2c, GENI_TIMEOUT);
		gi2c->cur = NULL;
		ret = geni_i2c_stop_with_cancel(gi2c);
		if (ret) {
			I2C_LOG_DBG(gi2c->ipcl, true, gi2c->dev,
				    "%s: Bus clear Failed\n", __func__);
			return ret;
		}
	}
	I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev,
		    "%s: BUS_CLEAR success\n", __func__);

	/* BUS_STOP */
	reinit_completion(&gi2c->xfer);
	m_cmd = I2C_STOP_ON_BUS;
	geni_se_setup_m_cmd(&gi2c->i2c_rsc, m_cmd, m_param);
	timeout = wait_for_completion_timeout(&gi2c->xfer, HZ);
	if (!timeout) {
		geni_i2c_err(gi2c, GENI_TIMEOUT);
		gi2c->cur = NULL;
		ret = geni_i2c_stop_with_cancel(gi2c);
		if (ret) {
			I2C_LOG_DBG(gi2c->ipcl, true, gi2c->dev,
				    "%s:Bus Stop Failed\n", __func__);
			return ret;
		}
	}
	I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev,
		    "%s: success\n", __func__);
	geni_capture_stop_time(&gi2c->i2c_rsc, gi2c->ipc_log_kpi, __func__,
			       gi2c->i2c_kpi, start_time, 0, 0);
	return 0;
}

static int do_pending_cancel(struct geni_i2c_dev *gi2c)
{
	int timeout = 0;
	u32 geni_ios = 0;

	/* doing pending cancel only rtl based SE's */
	if (!gi2c->is_i2c_rtl_based)
		return 0;

	geni_ios = geni_read_reg(gi2c->base, SE_GENI_IOS);
	if ((geni_ios & 0x3) != 0x3) {
		/* Try to restore IOS with FORCE_DEFAULT */
		GENI_SE_ERR(gi2c->ipcl, true, gi2c->dev,
			    "%s: IOS:0x%x, bad state\n", __func__, geni_ios);

		geni_write_reg(FORCE_DEFAULT,
			       gi2c->base, GENI_FORCE_DEFAULT_REG);
		geni_ios = geni_read_reg(gi2c->base, SE_GENI_IOS);
		if ((geni_ios & 0x3) != 0x3) {
			GENI_SE_ERR(gi2c->ipcl, true, gi2c->dev,
				    "%s: IOS:0x%x, Fix from Slave side\n",
				    __func__, geni_ios);
			return -EINVAL;
		}
		GENI_SE_ERR(gi2c->ipcl, true, gi2c->dev,
			    "%s: IOS:0x%x restored properly\n", __func__, geni_ios);
	}

	if (gi2c->se_mode == GSI_ONLY) {
		dmaengine_terminate_all(gi2c->tx_c);
		gi2c->cfg_sent = 0;
	} else if (geni_i2c_stop_with_cancel(gi2c)) {
		I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev,
			    "%s: geni_i2c_stop_with_cancel failed\n", __func__);
	}
	gi2c->prev_cancel_pending = false;
	I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev, "%s: Pending Cancel done\n", __func__);
	return timeout;
}

static int geni_i2c_prepare(struct geni_i2c_dev *gi2c)
{
	if (gi2c->se_mode == UNINITIALIZED) {
		int proto = geni_se_read_proto(&gi2c->i2c_rsc);
		u32 se_mode, geni_se_hw_param_2;

		if (proto != GENI_SE_I2C) {
			dev_err(gi2c->dev, "Invalid proto %d\n", proto);
			if (!gi2c->is_le_vm) {
				geni_se_resources_off(&gi2c->i2c_rsc);
				geni_icc_disable(&gi2c->i2c_rsc);
				if (gi2c->is_i2c_hub)
					clk_disable_unprepare(gi2c->core_clk);
			}
			return -ENXIO;
		}

		se_mode = readl_relaxed(gi2c->base +
					GENI_IF_DISABLE_RO);
		if (se_mode) {
			gi2c->se_mode = GSI_ONLY;
			geni_se_select_mode(&gi2c->i2c_rsc, GENI_GPI_DMA);
			I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev,
					"i2c in GSI ONLY mode\n");
		} else {
			int gi2c_tx_depth;

			if (!gi2c->is_i2c_hub)
				gi2c_tx_depth = geni_se_get_tx_fifo_depth(&gi2c->i2c_rsc);
			else
				gi2c_tx_depth = 16; /* i2c hub depth is fixed to 16 */

			gi2c->se_mode = FIFO_SE_DMA;

			gi2c->tx_wm = gi2c_tx_depth - 1;
			geni_se_init(&gi2c->i2c_rsc, gi2c->tx_wm, gi2c_tx_depth);
			qcom_geni_i2c_conf(gi2c, 0);
			geni_se_config_packing(&gi2c->i2c_rsc, 8, 4, true, true, true);
			I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev,
					"i2c fifo/se-dma mode. fifo depth:%d\n",
					gi2c_tx_depth);
		}

		if (!gi2c->is_i2c_hub) {
			/* Check if SE is RTL based SE */
			geni_se_hw_param_2 = readl_relaxed(gi2c->base + SE_HW_PARAM_2);
			if (geni_se_hw_param_2 & GEN_HW_FSM_I2C) {
				gi2c->is_i2c_rtl_based  = true;
				dev_info(gi2c->dev, "%s: RTL based SE\n", __func__);
			}
		}

		if (gi2c->pm_ctrl_client)
			I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev,
				"%s: pm_runtime_get_sync bypassed\n", __func__);
	}
	return 0;
}

static void geni_i2c_irq_handle_watermark(struct geni_i2c_dev *gi2c, u32 m_stat)
{
	struct i2c_msg *cur = gi2c->cur;
	int i, j;
	u32 rx_st = readl_relaxed(gi2c->base + SE_GENI_RX_FIFO_STATUS);

	if (!cur) {
		I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev, "%s: Spurious irq\n", __func__);
		geni_i2c_err(gi2c, GENI_SPURIOUS_IRQ);
		return;
	}

	if (((m_stat & M_RX_FIFO_WATERMARK_EN) ||
		(m_stat & M_RX_FIFO_LAST_EN)) && (cur->flags & I2C_M_RD)) {
		u32 rxcnt = rx_st & RX_FIFO_WC_MSK;

		for (j = 0; j < rxcnt; j++) {
			u32 temp;
			int p;

			if (gi2c->i2c_ssr.is_ssr_down)
				break;

			temp = readl_relaxed(gi2c->base + SE_GENI_RX_FIFOn);
			for (i = gi2c->cur_rd, p = 0; (i < cur->len && p < 4);
				i++, p++)
				cur->buf[i] = (u8) ((temp >> (p * 8)) & 0xff);
			gi2c->cur_rd = i;
			if (gi2c->cur_rd == cur->len) {
				dev_dbg(gi2c->dev, "FIFO i:%d,read 0x%x\n",
					i, temp);
				break;
			}
		}
	} else if ((m_stat & M_TX_FIFO_WATERMARK_EN) &&
					!(cur->flags & I2C_M_RD)) {
		for (j = 0; j < gi2c->tx_wm; j++) {
			u32 temp = 0;
			int p;

			for (i = gi2c->cur_wr, p = 0; (i < cur->len && p < 4);
				i++, p++)
				temp |= (((u32)(cur->buf[i]) << (p * 8)));

			if (gi2c->i2c_ssr.is_ssr_down)
				break;

			writel_relaxed(temp, gi2c->base + SE_GENI_TX_FIFOn);
			gi2c->cur_wr = i;
			dev_dbg(gi2c->dev, "FIFO i:%d,wrote 0x%x\n", i, temp);
			if (gi2c->cur_wr == cur->len) {
				dev_dbg(gi2c->dev, "FIFO i2c bytes done writing\n");
				writel_relaxed(0, (gi2c->base + SE_GENI_TX_WATERMARK_REG));
				break;
			}
		}
	} else {
		I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev,
			"%s: m_irq_status:0x%x cur->flags:%d\n", __func__, m_stat, cur->flags);
	}
}

/*
 * geni_i2c_check_addr_data_nack() - checks wheather it is Address Nack or Data Nack
 *
 * @gi2c: I2C device handle
 * @flags: gi2c cur flags
 *
 * Return: None
 */

static void geni_i2c_check_addr_data_nack(struct geni_i2c_dev *gi2c, __u16 flags)
{
	if (readl_relaxed(gi2c->base + SE_GENI_M_GP_LENGTH)) {
		/* only process for write operation. */
		if (!(flags & I2C_M_RD))
			geni_i2c_err(gi2c, I2C_DATA_NACK);
	} else {
		geni_i2c_err(gi2c, I2C_ADDR_NACK);
	}
}

static irqreturn_t geni_i2c_irq(int irq, void *dev)
{
	struct geni_i2c_dev *gi2c = dev;
	bool is_clear_watermark = false;
	bool m_cancel_done = false;
	u32 m_stat, dm_tx_st, dm_rx_st, dma;

	struct i2c_msg *cur = gi2c->cur;
	unsigned long long start_time;

	if (gi2c->i2c_ssr.is_ssr_down)
		goto irqret;

	start_time = geni_capture_start_time(&gi2c->i2c_rsc, gi2c->ipc_log_kpi, __func__,
					     gi2c->i2c_kpi);

	m_stat = readl_relaxed(gi2c->base + SE_GENI_M_IRQ_STATUS);
	dm_tx_st = readl_relaxed(gi2c->base + SE_DMA_TX_IRQ_STAT);
	dm_rx_st = readl_relaxed(gi2c->base + SE_DMA_RX_IRQ_STAT);
	dma = readl_relaxed(gi2c->base + SE_GENI_DMA_MODE_EN);

	I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev,
		    "%s: m_irq_status:0x%x\n", __func__, m_stat);

	if (!cur) {
		I2C_LOG_ERR(gi2c->ipcl, false, gi2c->dev, "Spurious irq\n");
		geni_i2c_err(gi2c, GENI_SPURIOUS_IRQ);
		gi2c->cmd_done = true;
		is_clear_watermark = true;
		goto irqret;
	}

	if ((m_stat & M_CMD_FAILURE_EN) ||
		(dm_rx_st & (DM_I2C_CB_ERR)) ||
		(m_stat & M_CMD_CANCEL_EN) ||
		(m_stat & M_CMD_ABORT_EN) ||
		(m_stat & M_GP_IRQ_1_EN) ||
		(m_stat & M_GP_IRQ_3_EN) ||
		(m_stat & M_GP_IRQ_4_EN)) {
		if (m_stat & M_GP_IRQ_1_EN)
			geni_i2c_check_addr_data_nack(gi2c, gi2c->cur->flags);
		if (m_stat & M_GP_IRQ_3_EN)
			geni_i2c_err(gi2c, I2C_BUS_PROTO);
		if (m_stat & M_GP_IRQ_4_EN)
			geni_i2c_err(gi2c, I2C_ARB_LOST);
		if (m_stat & M_CMD_OVERRUN_EN)
			geni_i2c_err(gi2c, GENI_OVERRUN);
		if (m_stat & M_ILLEGAL_CMD_EN)
			geni_i2c_err(gi2c, GENI_ILLEGAL_CMD);
		if (m_stat & M_CMD_ABORT_EN)
			geni_i2c_err(gi2c, GENI_ABORT_DONE);

		/*
		 * This bit(M_CMD_FAILURE_EN) is set when command execution has been
		 * completed with failure.
		 */
		if (m_stat & M_CMD_FAILURE_EN) {
			/* Log error else do not override previous set error */
			if (!gi2c->err)
				geni_i2c_err(gi2c, GENI_M_CMD_FAILURE);
			else
				I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev,
					    "%s:GENI_M_CMD_FAILURE\n", __func__);
		}

		/* This bit is set when command cancel request by SW is completed */
		if (m_stat & M_CMD_CANCEL_EN)
			m_cancel_done = true;

		gi2c->cmd_done = true;
		is_clear_watermark = true;
		goto irqret;
	}

	geni_i2c_irq_handle_watermark(gi2c, m_stat);

irqret:
	if (gi2c->i2c_ssr.is_ssr_down) {
		gi2c->cmd_done = false;
		complete(&gi2c->xfer);
		I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev,
			    "%s: SSR down\n", __func__);
		return IRQ_HANDLED;
	}
	if (!dma && is_clear_watermark)
		writel_relaxed(0, (gi2c->base + SE_GENI_TX_WATERMARK_REG));

	if (m_stat)
		writel_relaxed(m_stat, gi2c->base + SE_GENI_M_IRQ_CLEAR);

	if (dma) {
		if (dm_tx_st)
			writel_relaxed(dm_tx_st, gi2c->base +
				       SE_DMA_TX_IRQ_CLR);

		if (dm_rx_st)
			writel_relaxed(dm_rx_st, gi2c->base +
				       SE_DMA_RX_IRQ_CLR);
		/* Ensure all writes are done before returning from ISR. */
		wmb();

		if ((dm_tx_st & TX_DMA_DONE) || (dm_rx_st & RX_DMA_DONE))
			gi2c->cmd_done = true;
	} else if (m_stat & M_CMD_DONE_EN) {
		gi2c->cmd_done = true;
	}

	if (gi2c->cmd_done) {
		gi2c->cmd_done = false;
		complete(&gi2c->xfer);
	}

	if (m_cancel_done)
		complete(&gi2c->m_cancel_cmd);

	geni_capture_stop_time(&gi2c->i2c_rsc, gi2c->ipc_log_kpi, __func__,
			       gi2c->i2c_kpi, start_time, 0, gi2c->clk_freq_out);
	return IRQ_HANDLED;
}

static void gi2c_ev_cb(struct dma_chan *ch, struct msm_gpi_cb const *cb_str,
		       void *ptr)
{
	struct geni_i2c_dev *gi2c;
	u32 m_stat;

	if (!ptr || !cb_str) {
		pr_err("%s: Invalid ev_cb buffer\n", __func__);
		return;
	}

	gi2c = (struct geni_i2c_dev *)ptr;
	m_stat = cb_str->status;

	switch (cb_str->cb_event) {
	case MSM_GPI_QUP_ERROR:
	case MSM_GPI_QUP_SW_ERROR:
	case MSM_GPI_QUP_MAX_EVENT:
	case MSM_GPI_QUP_FW_ERROR:
	case MSM_GPI_QUP_PENDING_EVENT:
	case MSM_GPI_QUP_EOT_DESC_MISMATCH:
		break;
	case MSM_GPI_QUP_NOTIFY:
	case MSM_GPI_QUP_CH_ERROR:
		if (m_stat & M_GP_IRQ_1_EN)
			geni_i2c_check_addr_data_nack(gi2c, gi2c->cur->flags);
		if (m_stat & M_GP_IRQ_3_EN)
			geni_i2c_err(gi2c, I2C_BUS_PROTO);
		if (m_stat & M_GP_IRQ_4_EN)
			geni_i2c_err(gi2c, I2C_ARB_LOST);
		complete(&gi2c->xfer);
		break;
	default:
		break;
	}
	if (cb_str->cb_event != MSM_GPI_QUP_NOTIFY) {
		I2C_LOG_ERR(gi2c->ipcl, false, gi2c->dev,
			    "GSI QN err:0x%x, status:0x%x, err:%d\n",
			     cb_str->error_log.error_code,
			     m_stat, cb_str->cb_event);
		gi2c->gsi_err = true;
		complete(&gi2c->xfer);
	}
}

static void gi2c_gsi_cb_err(struct msm_gpi_dma_async_tx_cb_param *cb,
								char *xfer)
{
	struct geni_i2c_dev *gi2c;

	if (!cb || !cb->userdata) {
		pr_err("%s: Invalid gsi_cb\n", __func__);
		return;
	}

	gi2c = cb->userdata;

	if (!gi2c->cur) {
		geni_i2c_err(gi2c, GENI_SPURIOUS_IRQ);
		I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev, "%s: Invalid gi2c dev\n", __func__);
		return;
	}

	if (cb->status & DM_I2C_CB_ERR) {
		I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev,
			    "%s TCE Unexpected Err, stat:0x%x\n",
				xfer, cb->status);
		if (cb->status & (BIT(GP_IRQ1) << 5))
			geni_i2c_check_addr_data_nack(gi2c, gi2c->cur->flags);
		if (cb->status & (BIT(GP_IRQ3) << 5))
			geni_i2c_err(gi2c, I2C_BUS_PROTO);
		if (cb->status & (BIT(GP_IRQ4) << 5))
			geni_i2c_err(gi2c, I2C_ARB_LOST);
	}
}

/**
 * gi2c_gsi_tx_unmap() - unmap gi2c gsi tx message
 * @gi2c: Base address of the gi2c dev structure.
 * @msg_idx: gi2c message index.
 * @wr_idx: gi2c buffer write index.
 *
 * This function is used to unmap gi2c gsi tx messages.
 *
 * Return:  None.
 */
void gi2c_gsi_tx_unmap(struct geni_i2c_dev *gi2c, u32 msg_idx, u32 wr_idx)
{
	if (gi2c->msgs[msg_idx].len > IMMEDIATE_DMA_LEN) {
		geni_se_common_iommu_unmap_buf(gi2c->wrapper_dev, &gi2c->tx_ph[wr_idx],
					       gi2c->msgs[msg_idx].len, DMA_TO_DEVICE);
		i2c_put_dma_safe_msg_buf(gi2c->gsi_tx.dma_buf[wr_idx],
					 &gi2c->msgs[msg_idx], !gi2c->err);
	}
}

/**
 * gi2c_gsi_tre_process() - Process received TRE's from GSI HW
 * @gi2c: Base address of the gi2c dev structure.
 * @num: number of messages count.
 *
 * This function is used to process received TRE's from GSI HW.
 * And also used for error case, it will clear and unmap all pending transfers.
 *
 * Return:  None.
 */
static void gi2c_gsi_tre_process(struct geni_i2c_dev *gi2c, int num)
{
	u32 msg_xfer_cnt;
	int wr_idx = 0;
	unsigned long long start_time;

	start_time = geni_capture_start_time(&gi2c->i2c_rsc, gi2c->ipc_log_kpi, __func__,
					     gi2c->i2c_kpi);

	/* Error case we need to unmap all messages.
	 * Regular working case unmapping only processed messages.
	 */
	if (gi2c->err)
		msg_xfer_cnt = gi2c->gsi_tx.msg_cnt;
	else
		msg_xfer_cnt = atomic_read(&gi2c->gsi_tx.irq_cnt) * NUM_TRE_MSGS_PER_INTR;

	for (; gi2c->gsi_tx.unmap_cnt < msg_xfer_cnt; gi2c->gsi_tx.unmap_cnt++) {
		if (gi2c->gsi_tx.unmap_cnt == num) {
			I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev,
				    "%s:last %d msg unmapped\n", __func__, num);
			break;
		}
		gi2c->gsi_tx.tre_freed_cnt++;

		if (gi2c->msgs[gi2c->gsi_tx.unmap_cnt].len > IMMEDIATE_DMA_LEN) {
			wr_idx = gi2c->gsi_tx.unmap_cnt % MAX_NUM_TRE_MSGS;
			gi2c_gsi_tx_unmap(gi2c, gi2c->gsi_tx.unmap_cnt, wr_idx);
		}
		I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev,
			    "%s:unmap_cnt %d freed_cnt:%d wr_idx:%d\n",
			    __func__, gi2c->gsi_tx.unmap_cnt, gi2c->gsi_tx.tre_freed_cnt, wr_idx);
	}
	geni_capture_stop_time(&gi2c->i2c_rsc, gi2c->ipc_log_kpi, __func__,
			       gi2c->i2c_kpi, start_time, 0, 0);
}

static void gi2c_gsi_tx_cb(void *ptr)
{
	struct msm_gpi_dma_async_tx_cb_param *tx_cb = ptr;
	struct geni_i2c_dev *gi2c;

	if (!(tx_cb && tx_cb->userdata)) {
		pr_err("%s: Invalid tx_cb buffer\n", __func__);
		return;
	}

	gi2c = tx_cb->userdata;

	/* For gsi lock/unlock commands, no need to check bus related error */
	if (!gi2c->is_gsi_cmd)
		gi2c_gsi_cb_err(tx_cb, "TX");

	atomic_inc(&gi2c->gsi_tx.irq_cnt);
	I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev,
		    "%s:tx_cnt:%d gsi_err:%d gi2c_err:%d irq_cnt:%d\n",
		    __func__, gi2c->gsi_tx.msg_cnt, gi2c->gsi_err, gi2c->err,
			atomic_read(&gi2c->gsi_tx.irq_cnt));
	complete_all(&gi2c->xfer);
}

static void gi2c_gsi_rx_cb(void *ptr)
{
	struct msm_gpi_dma_async_tx_cb_param *rx_cb = ptr;
	struct geni_i2c_dev *gi2c;

	if (!(rx_cb && rx_cb->userdata)) {
		pr_err("%s: Invalid rx_cb buffer\n", __func__);
		return;
	}

	gi2c = rx_cb->userdata;
	if (!gi2c->cur) {
		geni_i2c_err(gi2c, GENI_SPURIOUS_IRQ);
		complete(&gi2c->xfer);
		return;
	}

	if (gi2c->cur->flags & I2C_M_RD) {
		gi2c_gsi_cb_err(rx_cb, "RX");
		complete(&gi2c->xfer);
	}
}

static int geni_i2c_gsi_request_channel(struct geni_i2c_dev *gi2c)
{
	int ret = 0;

	if (!gi2c->tx_c) {
		gi2c->tx_c = dma_request_slave_channel(gi2c->dev, "tx");
		if (!gi2c->tx_c) {
			I2C_LOG_ERR(gi2c->ipcl, true, gi2c->dev,
				    "tx dma req slv chan ret :%d\n", ret);
			return -EIO;
		}
	}

	if (!gi2c->rx_c) {
		gi2c->rx_c = dma_request_slave_channel(gi2c->dev, "rx");
		if (!gi2c->rx_c) {
			I2C_LOG_ERR(gi2c->ipcl, true, gi2c->dev,
				    "rx dma req slv chan ret :%d\n", ret);
			dma_release_channel(gi2c->tx_c);
			return -EIO;
		}
	}

	gi2c->tx_ev.init.callback = gi2c_ev_cb;
	gi2c->tx_ev.init.cb_param = gi2c;
	gi2c->tx_ev.cmd = MSM_GPI_INIT;
	gi2c->tx_c->private = &gi2c->tx_ev;
	ret = dmaengine_slave_config(gi2c->tx_c, NULL);
	if (ret) {
		I2C_LOG_ERR(gi2c->ipcl, true, gi2c->dev,
				"tx dma slave config ret :%d\n", ret);
		goto dmaengine_slave_config_fail;
	}

	gi2c->rx_ev.init.cb_param = gi2c;
	gi2c->rx_ev.init.callback = gi2c_ev_cb;
	gi2c->rx_ev.cmd = MSM_GPI_INIT;
	gi2c->rx_c->private = &gi2c->rx_ev;
	ret = dmaengine_slave_config(gi2c->rx_c, NULL);
	if (ret) {
		I2C_LOG_ERR(gi2c->ipcl, true, gi2c->dev,
				"rx dma slave config ret :%d\n", ret);
		goto dmaengine_slave_config_fail;
	}

	gi2c->tx_cb.userdata = gi2c;
	gi2c->rx_cb.userdata = gi2c;
	gi2c->req_chan = true;

	return ret;

dmaengine_slave_config_fail:
	dma_release_channel(gi2c->tx_c);
	dma_release_channel(gi2c->rx_c);
	gi2c->tx_c = NULL;
	gi2c->rx_c = NULL;
	return ret;
}

static struct msm_gpi_tre *setup_lock_tre(struct geni_i2c_dev *gi2c)
{
	struct msm_gpi_tre *lock_t = &gi2c->lock_t;
	bool gsi_bei = false;

	/* lock: chain bit set */
	lock_t->dword[0] = MSM_GPI_LOCK_TRE_DWORD0;
	lock_t->dword[1] = MSM_GPI_LOCK_TRE_DWORD1;
	lock_t->dword[2] = MSM_GPI_LOCK_TRE_DWORD2;

	if (gi2c->gsi_tx.is_multi_descriptor)
		gsi_bei = true;

	/* ieob for le-vm and chain for shared se */
	if (gi2c->is_shared)
		lock_t->dword[3] = MSM_GPI_LOCK_TRE_DWORD3(0, gsi_bei, 0, 0, 1);
	else if (gi2c->is_le_vm)
		lock_t->dword[3] = MSM_GPI_LOCK_TRE_DWORD3(0, 0, 0, 1, 0);

	return lock_t;
}

static struct msm_gpi_tre *setup_cfg0_tre(struct geni_i2c_dev *gi2c)
{
	struct geni_i2c_clk_fld *itr;
	struct msm_gpi_tre *cfg0_t = &gi2c->cfg0_t;
	bool gsi_bei = false;

	if (gi2c->gsi_tx.is_multi_descriptor)
		gsi_bei = true;

	if (gi2c->is_i2c_rtl_based)
		itr = geni_i2c_hub_clk_map + gi2c->clk_fld_idx;
	else
		itr = geni_i2c_clk_map + gi2c->clk_fld_idx;

	/* config0 */
	cfg0_t->dword[0] = MSM_GPI_I2C_CONFIG0_TRE_DWORD0(I2C_PACK_EN,
				itr->t_cycle, itr->t_high, itr->t_low);
	cfg0_t->dword[1] = MSM_GPI_I2C_CONFIG0_TRE_DWORD1(0, 0);
	cfg0_t->dword[2] = MSM_GPI_I2C_CONFIG0_TRE_DWORD2(0, itr->clk_div);
	cfg0_t->dword[3] = MSM_GPI_I2C_CONFIG0_TRE_DWORD3(0, gsi_bei, 0, 0, 1);
	I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev, "cfg_tre 0x%x 0x%x 0x%x 0x%x\n",
		    cfg0_t->dword[0], cfg0_t->dword[1], cfg0_t->dword[2], cfg0_t->dword[3]);

	return cfg0_t;
}

static struct msm_gpi_tre *setup_go_tre(struct geni_i2c_dev *gi2c,
				struct i2c_msg msgs[], int i, int num)
{
	struct msm_gpi_tre *go_t = &gi2c->go_t;
	u8 op = (msgs[i].flags & I2C_M_RD) ? 2 : 1;
	int stretch = (i < (num - 1));
	bool gsi_bei = false;

	if (gi2c->gsi_tx.is_multi_descriptor)
		gsi_bei = true;

	go_t->dword[0] = MSM_GPI_I2C_GO_TRE_DWORD0((stretch << 2),
							   msgs[i].addr, op);
	go_t->dword[1] = MSM_GPI_I2C_GO_TRE_DWORD1;

	if (msgs[i].flags & I2C_M_RD) {
		go_t->dword[2] = MSM_GPI_I2C_GO_TRE_DWORD2(msgs[i].len);
		/*
		 * For Rx Go tre: Set ieob for non-shared se and for all
		 * but last transfer in shared se
		 */
		if (!gi2c->is_shared || (gi2c->is_shared && i != num-1))
			go_t->dword[3] = MSM_GPI_I2C_GO_TRE_DWORD3(1, 0, 0, 1, 0);
		else
			go_t->dword[3] = MSM_GPI_I2C_GO_TRE_DWORD3(1, 0, 0, 0, 0);
	} else {
		/* For Tx Go tre: ieob is not set, chain bit is set */
		go_t->dword[2] = MSM_GPI_I2C_GO_TRE_DWORD2(0);
		go_t->dword[3] = MSM_GPI_I2C_GO_TRE_DWORD3(0, gsi_bei, 0, 0, 1);
	}

	I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev, "go_tre 0x%x 0x%x 0x%x 0x%x\n",
		    go_t->dword[0], go_t->dword[1], go_t->dword[2], go_t->dword[3]);

	return go_t;
}

static struct msm_gpi_tre *setup_rx_tre(struct geni_i2c_dev *gi2c,
				struct i2c_msg msgs[], int i, int num)
{
	struct msm_gpi_tre *rx_t = &gi2c->rx_t;

	rx_t->dword[0] = MSM_GPI_DMA_W_BUFFER_TRE_DWORD0(gi2c->rx_ph);
	rx_t->dword[1] = MSM_GPI_DMA_W_BUFFER_TRE_DWORD1(gi2c->rx_ph);
	rx_t->dword[2] = MSM_GPI_DMA_W_BUFFER_TRE_DWORD2(msgs[i].len);
	/* Set ieot for all Rx/Tx DMA tres */
	rx_t->dword[3] = MSM_GPI_DMA_W_BUFFER_TRE_DWORD3(0, 0, 1, 0, 0);

	return rx_t;
}

void geni_i2c_get_immediate_dma_data(u8 *dword, int len, uint8_t *buf)
{
	int i = 0;

	for (i = 0; i < len; i++)
		dword[i] =  buf[i];
}

static struct msm_gpi_tre *setup_tx_tre(struct geni_i2c_dev *gi2c,
			struct i2c_msg msgs[], int i, int num, bool *gsi_bei, int wr_idx)
{
	struct msm_gpi_tre *tx_t = &gi2c->tx_t;
	bool is_immediate_dma = false;

	if (msgs[i].len <= IMMEDIATE_DMA_LEN)
		is_immediate_dma = true;

	if (gi2c->gsi_tx.is_multi_descriptor) {
		if ((i + 1) % NUM_TRE_MSGS_PER_INTR)
			*gsi_bei = true;
		else
			*gsi_bei = false;

		/*
		 * Keep BEI = 0, for all last TREs
		 * Shared SE : Last is unlock TRE, hence continue to have BEI = TRUE for DMA TX TRE.
		 * BEI = 0, taken cared by setup_unlock_tre().
		 * Rest all/non shared/Multi descriptor TREs : BEI = 0 for last transfer TRE.
		 */
		if (i == (num - 1)) {
			/* For Tx: for shared usecase unlock tre is send
			 * for last transfer so set bei bit for last transfer
			 * DMA tre
			 */
			if (gi2c->is_shared)
				*gsi_bei = true;
			else
				*gsi_bei = false;
		}
	}

	if (is_immediate_dma) {
		/* dowrd[0], dword[1] filled as per data length */
		tx_t->dword[0] = 0;
		tx_t->dword[1] = 0;
		geni_i2c_get_immediate_dma_data((uint8_t *)&tx_t->dword[0],
						msgs[i].len, msgs[i].buf);
		tx_t->dword[2] = MSM_GPI_DMA_IMMEDIATE_TRE_DWORD2(msgs[i].len);
		if (gi2c->is_shared && i == (num - 1))
			/*
			 * For Tx: unlock tre is send for last transfer
			 * so set chain bit for last transfer DMA tre.
			 */
			tx_t->dword[3] = MSM_GPI_DMA_IMMEDIATE_TRE_DWORD3(0, *gsi_bei, 1, 0, 1);
		else
			tx_t->dword[3] = MSM_GPI_DMA_IMMEDIATE_TRE_DWORD3(0, *gsi_bei, 1, 0, 0);
	} else {
		tx_t->dword[0] = MSM_GPI_DMA_W_BUFFER_TRE_DWORD0(gi2c->tx_ph[wr_idx]);
		tx_t->dword[1] = MSM_GPI_DMA_W_BUFFER_TRE_DWORD1(gi2c->tx_ph[wr_idx]);
		tx_t->dword[2] = MSM_GPI_DMA_W_BUFFER_TRE_DWORD2(msgs[i].len);

		if (gi2c->is_shared && (i == num - 1))
			/*
			 * For Tx: unlock tre is send for last transfer
			 * so set chain bit for last transfer DMA tre.
			 */
			tx_t->dword[3] = MSM_GPI_DMA_W_BUFFER_TRE_DWORD3(0, *gsi_bei, 1, 0, 1);
		else
			tx_t->dword[3] = MSM_GPI_DMA_W_BUFFER_TRE_DWORD3(0, *gsi_bei, 1, 0, 0);
	}

	I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev, "tx_tre 0x%x 0x%x 0x%x 0x%x imm_dma:%d bei:%d\n",
		    tx_t->dword[0], tx_t->dword[1], tx_t->dword[2], tx_t->dword[3],
		    is_immediate_dma, *gsi_bei);
	return tx_t;
}

static struct msm_gpi_tre *setup_unlock_tre(struct geni_i2c_dev *gi2c)
{
	struct msm_gpi_tre *unlock_t = &gi2c->unlock_t;

	/* unlock tre: ieob set */
	unlock_t->dword[0] = MSM_GPI_UNLOCK_TRE_DWORD0;
	unlock_t->dword[1] = MSM_GPI_UNLOCK_TRE_DWORD1;
	unlock_t->dword[2] = MSM_GPI_UNLOCK_TRE_DWORD2;
	unlock_t->dword[3] = MSM_GPI_UNLOCK_TRE_DWORD3(0, 0, 0, 1, 0);

	return unlock_t;
}

static struct dma_async_tx_descriptor *geni_i2c_prep_desc
(struct geni_i2c_dev *gi2c, struct dma_chan *chan, int segs, bool tx_chan)
{
	struct dma_async_tx_descriptor *geni_desc = NULL;

	if (tx_chan) {
		geni_desc = dmaengine_prep_slave_sg(gi2c->tx_c, gi2c->tx_sg,
					segs, DMA_MEM_TO_DEV,
					(DMA_PREP_INTERRUPT | DMA_CTRL_ACK));
		if (!geni_desc) {
			I2C_LOG_ERR(gi2c->ipcl, true, gi2c->dev,
					"prep_slave_sg for tx failed\n");
			gi2c->err = -ENOMEM;
			return NULL;
		}
		geni_desc->callback = gi2c_gsi_tx_cb;
		geni_desc->callback_param = &gi2c->tx_cb;
	} else {
		geni_desc = dmaengine_prep_slave_sg(gi2c->rx_c,
					gi2c->rx_sg, 1, DMA_DEV_TO_MEM,
					(DMA_PREP_INTERRUPT | DMA_CTRL_ACK));
		if (!geni_desc) {
			I2C_LOG_ERR(gi2c->ipcl, true, gi2c->dev,
					"prep_slave_sg for rx failed\n");
			gi2c->err = -ENOMEM;
			return NULL;
		}
		geni_desc->callback = gi2c_gsi_rx_cb;
		geni_desc->callback_param = &gi2c->rx_cb;
	}

	return geni_desc;
}

static int geni_i2c_lock_bus(struct geni_i2c_dev *gi2c)
{
	struct msm_gpi_tre *lock_t = NULL;
	int ret = 0, timeout = 0;
	dma_cookie_t tx_cookie;
	bool tx_chan = true;
	unsigned long long start_time;

	start_time = geni_capture_start_time(&gi2c->i2c_rsc, gi2c->ipc_log_kpi, __func__,
					     gi2c->i2c_kpi);

	if (!gi2c->req_chan) {
		ret = geni_i2c_gsi_request_channel(gi2c);
		if (ret)
			return ret;
	}

	lock_t = setup_lock_tre(gi2c);
	sg_init_table(gi2c->tx_sg, 1);
	sg_set_buf(&gi2c->tx_sg[0], lock_t,
					sizeof(gi2c->lock_t));

	gi2c->tx_desc = geni_i2c_prep_desc(gi2c, gi2c->tx_c, 1, tx_chan);
	if (!gi2c->tx_desc) {
		gi2c->err = -ENOMEM;
		goto geni_i2c_err_lock_bus;
	}

	gi2c->is_gsi_cmd = true;
	reinit_completion(&gi2c->xfer);
	/* Issue TX */
	tx_cookie = dmaengine_submit(gi2c->tx_desc);
	if (dma_submit_error(tx_cookie)) {
		I2C_LOG_ERR(gi2c->ipcl, true, gi2c->dev,
			    "%s: dmaengine_submit failed (%d)\n", __func__, tx_cookie);
		gi2c->err = -EINVAL;
		goto geni_i2c_err_lock_bus;
	}

	dma_async_issue_pending(gi2c->tx_c);

	timeout = wait_for_completion_timeout(&gi2c->xfer, HZ);
	if (!timeout) {
		I2C_LOG_ERR(gi2c->ipcl, false, gi2c->dev,
			    "%s timedout\n", __func__);
		geni_i2c_se_dump_dbg_regs(&gi2c->i2c_rsc, gi2c->base,
					gi2c->ipcl);
		gi2c->err = -ETIMEDOUT;
		goto geni_i2c_err_lock_bus;
	}
	gi2c->is_gsi_cmd = false;
	return 0;

geni_i2c_err_lock_bus:
	if (gi2c->err) {
		dmaengine_terminate_all(gi2c->tx_c);
		gi2c->cfg_sent = 0;
	}
	gi2c->is_gsi_cmd = false;
	geni_capture_stop_time(&gi2c->i2c_rsc, gi2c->ipc_log_kpi, __func__,
			       gi2c->i2c_kpi, start_time, 0, 0);
	return gi2c->err;
}

static void geni_i2c_unlock_bus(struct geni_i2c_dev *gi2c)
{
	struct msm_gpi_tre *unlock_t = NULL;
	int timeout = 0;
	dma_cookie_t tx_cookie;
	bool tx_chan = true;
	unsigned long long start_time;

	start_time = geni_capture_start_time(&gi2c->i2c_rsc, gi2c->ipc_log_kpi, __func__,
					     gi2c->i2c_kpi);

	/* if gpi reset happened for levm, no need to do unlock */
	if (gi2c->is_le_vm && gi2c->le_gpi_reset_done) {
		I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev,
			    "%s:gpi reset happened for levm, no need to do unlock\n", __func__);
		return;
	}

	unlock_t = setup_unlock_tre(gi2c);
	sg_init_table(gi2c->tx_sg, 1);
	sg_set_buf(&gi2c->tx_sg[0], unlock_t,
					sizeof(gi2c->unlock_t));

	gi2c->tx_desc = geni_i2c_prep_desc(gi2c, gi2c->tx_c, 1, tx_chan);
	if (!gi2c->tx_desc) {
		gi2c->err = -ENOMEM;
		goto geni_i2c_err_unlock_bus;
	}

	gi2c->is_gsi_cmd = true;
	reinit_completion(&gi2c->xfer);
	/* Issue TX */
	tx_cookie = dmaengine_submit(gi2c->tx_desc);
	if (dma_submit_error(tx_cookie)) {
		I2C_LOG_ERR(gi2c->ipcl, true, gi2c->dev,
			    "%s: dmaengine_submit failed (%d)\n", __func__, tx_cookie);
		gi2c->err = -EINVAL;
		goto geni_i2c_err_unlock_bus;
	}

	dma_async_issue_pending(gi2c->tx_c);

	timeout = wait_for_completion_timeout(&gi2c->xfer, HZ);
	if (!timeout) {
		I2C_LOG_ERR(gi2c->ipcl, false, gi2c->dev,
			    "%s failed\n", __func__);
		geni_i2c_se_dump_dbg_regs(&gi2c->i2c_rsc, gi2c->base,
					gi2c->ipcl);
		gi2c->err = -ETIMEDOUT;
		goto geni_i2c_err_unlock_bus;
	}

geni_i2c_err_unlock_bus:
	if (gi2c->err) {
		dmaengine_terminate_all(gi2c->tx_c);
		gi2c->cfg_sent = 0;
		gi2c->err = 0;
	}
	gi2c->is_gsi_cmd = false;
	geni_capture_stop_time(&gi2c->i2c_rsc, gi2c->ipc_log_kpi, __func__,
			       gi2c->i2c_kpi, start_time, 0, 0);
}

/**
 * geni_i2c_check_for_gsi_multi_desc_mode() - check for i2c multi descriptor mode.
 * @gi2c: Geni I2C device handle
 * @msgs: Base address of i2c msgs
 * @num: Number of messages
 *
 * This function check for multi desc mode.
 *
 * Return: None
 */
static void geni_i2c_check_for_gsi_multi_desc_mode(struct geni_i2c_dev *gi2c, struct i2c_msg msgs[],
						   int num)
{
	u32 i = 0;

	if (num >= MIN_NUM_MSGS_FOR_MULTI_DESC_MODE) {
		gi2c->gsi_tx.is_multi_descriptor = true;
		/* assumes multi descriptor supports only for continuous writes */
		for (i = 0; i < num; i++)
			if (msgs[i].flags & I2C_M_RD)
				gi2c->gsi_tx.is_multi_descriptor = false;
	} else {
		gi2c->gsi_tx.is_multi_descriptor = false;
	}
	#if IS_ENABLED(CONFIG_MSM_GPI_DMA)
	gpi_update_multi_desc_flag(gi2c->tx_c, gi2c->gsi_tx.is_multi_descriptor, num);
	#endif
}
/**
 * geni_i2c_gsi_read() - Perform gsi i2c read
 * @gi2c: Geni I2C device handle
 * @dma_buf: Pointer to DMA buffer
 * @msgs: Base address of i2c msgs
 * @msg_index: Message index
 * @unlock_t: Unlock tre handle
 * @num: Number of messages
 * @segs: Segment number
 * @sg_index: Scatter gather index
 *
 * This function perform i2c gsi read
 *
 * Return: 0 for success or error code for failure
 */

static int geni_i2c_gsi_read(struct geni_i2c_dev *gi2c, u8 **dma_buf, struct i2c_msg msgs[],
			     int msg_index, struct msm_gpi_tre *unlock_t, int num, int segs,
			     int *sg_index)
{
	struct msm_gpi_tre *rx_t = NULL;
	int ret = 0;
	dma_cookie_t tx_cookie, rx_cookie;
	int index = *sg_index;
	u8 *rd_dma_buf = NULL;

	reinit_completion(&gi2c->xfer);
	rd_dma_buf = i2c_get_dma_safe_msg_buf(&msgs[msg_index], 1);
	if (!rd_dma_buf) {
		ret = -ENOMEM;
		I2C_LOG_ERR(gi2c->ipcl, true, gi2c->dev,
			    "i2c_get_dma_safe_msg_buf failed :%d\n",
			    ret);
		return GENI_I2C_GSI_XFER_OUT;
	}
	I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev,
		    "msg[%d].len:%d R\n", msg_index, gi2c->cur->len);
	sg_init_table(gi2c->rx_sg, 1);
	ret = geni_se_common_iommu_map_buf(gi2c->wrapper_dev,
					   &gi2c->rx_ph,
					   rd_dma_buf,
					   msgs[msg_index].len,
					   DMA_FROM_DEVICE);
	if (ret) {
		I2C_LOG_ERR(gi2c->ipcl, true, gi2c->dev,
			    "geni_se_common_iommu_map_buf for rx failed :%d\n", ret);
		i2c_put_dma_safe_msg_buf(rd_dma_buf, &msgs[msg_index], false);
		return GENI_I2C_GSI_XFER_OUT;

	} else if (gi2c->dbg_buf_ptr) {
		gi2c->dbg_buf_ptr[msg_index].virt_buf =
					(void *)rd_dma_buf;
		gi2c->dbg_buf_ptr[msg_index].map_buf =
					(void *)&gi2c->rx_ph;
	}
	rx_t = setup_rx_tre(gi2c, msgs, msg_index, num);
	sg_set_buf(gi2c->rx_sg, rx_t,
		   sizeof(gi2c->rx_t));
	gi2c->rx_desc =
	geni_i2c_prep_desc(gi2c, gi2c->rx_c, segs, false);
	if (!gi2c->rx_desc) {
		gi2c->err = -ENOMEM;
		return GENI_I2C_ERR_PREP_SG;
	}

	/* Issue RX */
	rx_cookie = dmaengine_submit(gi2c->rx_desc);
	if (dma_submit_error(rx_cookie)) {
		pr_err("%s: dmaengine_submit failed (%d)\n", __func__, rx_cookie);
		gi2c->err = -EINVAL;
		return GENI_I2C_ERR_PREP_SG;
	}

	dma_async_issue_pending(gi2c->rx_c);
	/* submit config/go tre through tx channel */
	if (gi2c->is_shared && (msg_index == (num - 1))) {
		/* Send unlock tre at the end of last transfer */
		sg_set_buf(&gi2c->tx_sg[index++],
			   unlock_t, sizeof(gi2c->unlock_t));
	}

	gi2c->tx_desc = geni_i2c_prep_desc(gi2c, gi2c->tx_c, segs, true);
	if (!gi2c->tx_desc) {
		gi2c->err = -ENOMEM;
		I2C_LOG_ERR(gi2c->ipcl, true, gi2c->dev,
			    "geni_i2c_prep_desc failed\n");
		return GENI_I2C_ERR_PREP_SG;
	}

	/* Issue TX */
	tx_cookie = dmaengine_submit(gi2c->tx_desc);
	if (dma_submit_error(tx_cookie)) {
		I2C_LOG_ERR(gi2c->ipcl, true, gi2c->dev,
			    "%s: dmaengine_submit failed (%d)\n",
			    __func__, tx_cookie);
		gi2c->err = -EINVAL;
		return GENI_I2C_ERR_PREP_SG;
	}
	dma_async_issue_pending(gi2c->tx_c);
	*dma_buf = rd_dma_buf;
	*sg_index = index;
	return ret;
}

/**
 * geni_i2c_gsi_write() - Perform gsi i2c write
 * @gi2c: Geni I2C device handle
 * @msgs: Base address of i2c msgs
 * @msg_index: Message index
 * @unlock_t: Unlock tre handle
 * @num: Number of messages
 * @segs: Segment number
 * @sg_index: Scatter gather index
 * @wr_index: Write index
 *
 * This function perfrom gsi i2c write
 *
 * Return: 0 for success or error code for failure
 */
static int geni_i2c_gsi_write(struct geni_i2c_dev *gi2c, struct i2c_msg msgs[],
			      int msg_index, struct msm_gpi_tre *unlock_t,
			      int num, int segs, int *sg_index, u32 *wr_index)
{
	struct msm_gpi_tre *tx_t = NULL;
	int ret = 0;
	int index = *sg_index;
	dma_cookie_t tx_cookie;
	bool gsi_bei = false;

	if (msgs[msg_index].len > IMMEDIATE_DMA_LEN) {
		gi2c->gsi_tx.dma_buf[*wr_index] =
			i2c_get_dma_safe_msg_buf(&msgs[msg_index], 1);
		if (!gi2c->gsi_tx.dma_buf[*wr_index]) {
			gi2c->err = -ENOMEM;
			I2C_LOG_ERR(gi2c->ipcl, true, gi2c->dev,
				    "i2c_get_dma_safe_msg_buf failed :%d\n", ret);
			return GENI_I2C_GSI_XFER_OUT;
		}
		ret = geni_se_common_iommu_map_buf
			(gi2c->wrapper_dev,
			 &gi2c->tx_ph[*wr_index],
			 gi2c->gsi_tx.dma_buf[*wr_index],
			 msgs[msg_index].len,
			 DMA_TO_DEVICE);
		if (ret) {
			I2C_LOG_ERR(gi2c->ipcl, true, gi2c->dev,
				    "geni iommu_map_buf for tx failed :%d\n", ret);
			i2c_put_dma_safe_msg_buf
			(gi2c->gsi_tx.dma_buf[*wr_index],
			 &msgs[msg_index],
			 false);
			gi2c->err = ret;
			return GENI_I2C_GSI_XFER_OUT;

		} else if (gi2c->dbg_buf_ptr) {
			gi2c->dbg_buf_ptr[*wr_index].virt_buf =
				(void *)gi2c->gsi_tx.dma_buf[*wr_index];
			gi2c->dbg_buf_ptr[*wr_index].map_buf =
				(void *)&gi2c->tx_ph[*wr_index];
		}
	}
	I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev,
		    "msg[%d].len:%d W cnt:%d idx:%d\n",
		     msg_index, gi2c->cur->len, gi2c->gsi_tx.msg_cnt, *wr_index);
	tx_t = setup_tx_tre(gi2c, msgs, msg_index, num, &gsi_bei, *wr_index);
	sg_set_buf(&gi2c->tx_sg[index++], tx_t, sizeof(gi2c->tx_t));
	if (gi2c->is_shared && (msg_index == (num - 1))) {
		/* Send unlock tre at the end of last transfer */
		sg_set_buf(&gi2c->tx_sg[index++],
			   unlock_t, sizeof(gi2c->unlock_t));
		/* to enable call back for unlock tre */
		if (gi2c->gsi_tx.is_multi_descriptor)
			gsi_bei = false;
	}

	gi2c->tx_desc = geni_i2c_prep_desc(gi2c, gi2c->tx_c, segs, true);
	if (!gi2c->tx_desc) {
		gi2c->err = -ENOMEM;
		I2C_LOG_ERR(gi2c->ipcl, true, gi2c->dev,
			    "geni_i2c_prep_desc failed\n");
		return GENI_I2C_ERR_PREP_SG;
	}

	/* we don't need call back if bei bit is set */
	if (gsi_bei) {
		I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev,
			    "geni tx desc call back null %d\n", msg_index);
		gi2c->tx_desc->callback = NULL;
		gi2c->tx_desc->callback_param = NULL;
	}
	gi2c->gsi_tx.msg_cnt++;
	*wr_index = (msg_index + 1) % MAX_NUM_TRE_MSGS;
	I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev,
		    "tx_cnt:%d", gi2c->gsi_tx.msg_cnt);
	/* Issue TX */
	tx_cookie = dmaengine_submit(gi2c->tx_desc);
	if (dma_submit_error(tx_cookie)) {
		I2C_LOG_ERR(gi2c->ipcl, true, gi2c->dev,
			    "%s: dmaengine_submit failed (%d)\n",
			    __func__, tx_cookie);
		gi2c->err = -EINVAL;
		return GENI_I2C_ERR_PREP_SG;
	}
	dma_async_issue_pending(gi2c->tx_c);
	*sg_index = index;
	return ret;
}

/**
 * geni_i2c_gsi_tx_tre_optimization() - Process received TRE's from GSI HW
 * @gi2c: Base address of the gi2c dev structure.
 * @num: number of messages count.
 * @msg_idx: gi2c message index.
 * @wr_idx: gi2c buffer write index.
 *
 * This function is used to optimize dma tre's, it keeps always HW busy.
 *
 * Return: Returning timeout value
 */
static int geni_i2c_gsi_tx_tre_optimization(struct geni_i2c_dev *gi2c, u32 num, u32 msg_idx,
					    u32 wr_idx)
{
	int timeout = 1, i;
	int max_irq_cnt;
	unsigned long long start_time;

	start_time = geni_capture_start_time(&gi2c->i2c_rsc, gi2c->ipc_log_kpi, __func__,
					     gi2c->i2c_kpi);

	max_irq_cnt = num / NUM_TRE_MSGS_PER_INTR;
	if (num % NUM_TRE_MSGS_PER_INTR)
		max_irq_cnt++;

	/**
	 * if it's last message, waiting for all pending tre's
	 * including last submitted tre as well.
	 */
	if (gi2c->gsi_tx.is_multi_descriptor) {
		for (i = 0; i < max_irq_cnt; i++) {
			if (max_irq_cnt != atomic_read(&gi2c->gsi_tx.irq_cnt)) {
				I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev,
					"%s: calling wait for_completion %d\n", __func__, i);
				timeout = wait_for_completion_timeout(&gi2c->xfer,
								      gi2c->xfer_timeout);
				reinit_completion(&gi2c->xfer);
				if (!timeout) {
					I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev,
						    "%s: msg xfer timeout\n", __func__);
					return timeout;
				}
			}
			I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev,
				    "%s: maxirq_cnt:%d i:%d\n", __func__, max_irq_cnt, i);
			/* GSI HW creates an error during callback, so error check handling here */
			if (gi2c->gsi_err) {
				I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev,
					    "%s: gsi error\n", __func__);
				return -EIO;
			}
			gi2c_gsi_tre_process(gi2c, num);
			if (num > gi2c->gsi_tx.msg_cnt)
				return timeout;
		}
	} else {
		/**
		 * For shared SE and num of msgs < MAX_NUM_TRE_MSGS,
		 * go with regular approach
		 */
		timeout = wait_for_completion_timeout(&gi2c->xfer, gi2c->xfer_timeout);

		reinit_completion(&gi2c->xfer);
		I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev,
			    "%s: msg_idx:%d wr_idx:%d\n", __func__, msg_idx, wr_idx);
		 /* GSI HW creates an error during callback, so error check handling here */
		if (gi2c->gsi_err) {
			I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev,
				    "%s: gsi error\n", __func__);
			return -EIO;
		}
		/* if tre processed without errors doing unmap here */
		if (timeout && !gi2c->err)
			gi2c_gsi_tx_unmap(gi2c, msg_idx, wr_idx);
	}

	/* process received tre's */
	if (timeout) {
		if (gi2c->gsi_tx.is_multi_descriptor)
			gi2c_gsi_tre_process(gi2c, num);
	}

	I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev,
		    "%s:  timeout :%d\n", __func__, timeout);
	geni_capture_stop_time(&gi2c->i2c_rsc, gi2c->ipc_log_kpi, __func__,
			       gi2c->i2c_kpi, start_time, 0, 0);
	return timeout;
}

/**
 * geni_i2c_calc_xfer_time() - Caluclate transfer time
 * @gi2c:geni i2c structure as a pointer
 * @msgs[]: i2c_msg structure as a pointer
 * @start_time: start time of the function
 * @msg_idx: gi2c message index.
 * @func: for which function kpi capture is used.
 *
 * Return: None.
 */
static void geni_i2c_calc_xfer_time(struct geni_i2c_dev *gi2c, struct i2c_msg msgs[],
				    unsigned long long start_time, u32 msg_idx, const char *func)
{
	char fname[32];

	if (msgs[msg_idx].flags & I2C_M_RD)
		scnprintf(fname, sizeof(fname), "%s%s", func, "_rd");
	else
		scnprintf(fname, sizeof(fname), "%s%s", func, "_wr");

	geni_capture_stop_time(&gi2c->i2c_rsc, gi2c->ipc_log_kpi, fname, gi2c->i2c_kpi,
			       start_time, msgs[msg_idx].len, gi2c->clk_freq_out);
}

static int geni_i2c_gsi_xfer(struct i2c_adapter *adap, struct i2c_msg msgs[],
			     int num)
{
	struct geni_i2c_dev *gi2c = i2c_get_adapdata(adap);
	u32 i = 0;
	int ret = 0, timeout = 0;
	struct msm_gpi_tre *lock_t = NULL;
	struct msm_gpi_tre *unlock_t = NULL;
	struct msm_gpi_tre *cfg0_t = NULL;
	u8 *rd_dma_buf = NULL;
	u8 op;
	int segs;
	u32 index = 0, wr_idx = 0;
	struct msm_gpi_tre *go_t = NULL;
	unsigned long long start_time;
	unsigned long long start_time_xfer = sched_clock();

	gi2c->gsi_err = false;
	if (!gi2c->req_chan) {
		ret = geni_i2c_gsi_request_channel(gi2c);
		if (ret)
			return ret;
	}

	if (gi2c->is_le_vm && gi2c->le_gpi_reset_done) {
		I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev,
			    "%s doing gsi lock, due to levm gsi reset\n", __func__);
		ret = geni_i2c_lock_bus(gi2c);
		if (ret) {
			I2C_LOG_ERR(gi2c->ipcl, true, gi2c->dev,
				    "%s lock bus failed: %d\n", __func__, ret);
			return ret;
		}
		gi2c->le_gpi_reset_done = false;
	}

	if (gi2c->is_shared) {
		lock_t = setup_lock_tre(gi2c);
		unlock_t = setup_unlock_tre(gi2c);
	}

	geni_i2c_check_for_gsi_multi_desc_mode(gi2c, msgs, num);

	if (!gi2c->cfg_sent)
		cfg0_t = setup_cfg0_tre(gi2c);

	gi2c->msgs = msgs;
	gi2c->gsi_tx.msg_cnt = 0;
	gi2c->gsi_tx.unmap_cnt = 0;
	gi2c->gsi_tx.tre_freed_cnt = 0;
	atomic_set(&gi2c->gsi_tx.irq_cnt, 0);
	reinit_completion(&gi2c->xfer);

	for (i = 0; i < num; i++) {
		start_time = geni_capture_start_time(&gi2c->i2c_rsc, gi2c->ipc_log_kpi,
						     __func__, gi2c->i2c_kpi);
		op = (msgs[i].flags & I2C_M_RD) ? 2 : 1;
		segs = 3 - op;
		index = 0;
		/**
		 * sometimes all tre's may process without
		 * waiting for timer thread, so declared
		 * timeout is non-zero value;
		 */
		timeout = 1;

		gi2c->cur = &msgs[i];
		if (!gi2c->cur) {
			I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev,
				    "%s: Invalid buffer\n", __func__);
			ret = -ENOMEM;
			goto geni_i2c_gsi_xfer_out;
		}

		qcom_geni_i2c_calc_timeout(gi2c);

		if (!gi2c->cfg_sent)
			segs++;
		if (gi2c->is_shared && (i == 0 || i == num-1)) {
			segs++;
			if (num == 1)
				segs++;
			sg_init_table(gi2c->tx_sg, segs);
			if (i == 0)
				/* Send lock tre for first transfer in a msg */
				sg_set_buf(&gi2c->tx_sg[index++], lock_t,
					sizeof(gi2c->lock_t));
		} else {
			sg_init_table(gi2c->tx_sg, segs);
		}

		/* Send cfg tre when cfg not sent already */
		if (!gi2c->cfg_sent) {
			sg_set_buf(&gi2c->tx_sg[index++], cfg0_t,
						sizeof(gi2c->cfg0_t));
			gi2c->cfg_sent = 1;
		}

		go_t = setup_go_tre(gi2c, msgs, i, num);
		sg_set_buf(&gi2c->tx_sg[index++], go_t, sizeof(gi2c->go_t));

		if (msgs[i].flags & I2C_M_RD) {
			ret = geni_i2c_gsi_read(gi2c, &rd_dma_buf, msgs, i, unlock_t,
						num, segs, &index);
			if (ret == GENI_I2C_ERR_PREP_SG) {
				ret = gi2c->err;
				goto  geni_i2c_err_prep_sg;

			} else if (ret == GENI_I2C_GSI_XFER_OUT) {
				ret = gi2c->err;
				goto geni_i2c_gsi_xfer_out;
			}
			timeout = wait_for_completion_timeout(&gi2c->xfer,
							      gi2c->xfer_timeout);
		} else {

			ret = geni_i2c_gsi_write(gi2c, msgs, i, unlock_t,
						 num, segs, &index, &wr_idx);
			if (ret == GENI_I2C_GSI_XFER_OUT) {
				ret = gi2c->err;
				goto geni_i2c_gsi_xfer_out;
			} else if (ret == GENI_I2C_ERR_PREP_SG) {
				ret = gi2c->err;
				goto  geni_i2c_err_prep_sg;
			}
			/**
			 * if it's not last message, submitting MAX_NUM_TRE_MSGS
			 * continuously without waiting, in b/w if any one of the
			 * tre is received processing and queuing next tre.
			 */
			if (gi2c->gsi_tx.is_multi_descriptor && (i != (num - 1)) &&
			    (gi2c->gsi_tx.msg_cnt < MAX_NUM_TRE_MSGS + gi2c->gsi_tx.tre_freed_cnt))
				continue;

			timeout = geni_i2c_gsi_tx_tre_optimization(gi2c, num, i, wr_idx - 1);
		}

		if (gi2c->i2c_ssr.is_ssr_down) {
			I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev, "%s: SSR Down\n", __func__);
			return 0;
		}

		if (!timeout) {
			u32 geni_ios = 0;
			I2C_LOG_ERR(gi2c->ipcl, true, gi2c->dev,
				"I2C gsi xfer timeout:%u flags:%d addr:0x%x\n",
				gi2c->xfer_timeout, gi2c->cur->flags,
				gi2c->cur->addr);
			geni_i2c_se_dump_dbg_regs(&gi2c->i2c_rsc, gi2c->base,
						gi2c->ipcl);
			gi2c->err = -ETIMEDOUT;

			/* WAR: Set flag to mark cancel pending if IOS stuck */
			geni_ios = geni_read_reg(gi2c->base, SE_GENI_IOS);
			if ((geni_ios & 0x3) != 0x3) { //SCL:b'1, SDA:b'0
				I2C_LOG_ERR(gi2c->ipcl, false, gi2c->dev,
					    "%s: IO lines not in good state\n",
					    __func__);
					/* doing pending cancel only rtl based SE's */
					if (gi2c->is_i2c_rtl_based) {
						gi2c->prev_cancel_pending = true;
						goto geni_i2c_gsi_cancel_pending;
					}
			}
		}
geni_i2c_err_prep_sg:
		if (gi2c->err || gi2c->gsi_err) {
			ret = dmaengine_terminate_all(gi2c->tx_c);
			if (gi2c->i2c_ssr.is_ssr_down) {
				I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev,
					    "%s: SSR Down\n", __func__);
				return 0;
			}
			if (ret)
				I2C_LOG_ERR(gi2c->ipcl, false, gi2c->dev,
					    "%s: gpi terminate failed ret:%d\n", __func__, ret);
			gi2c->cfg_sent = 0;
			if (gi2c->is_le_vm)
				gi2c->le_gpi_reset_done = true;
		}

		if (gi2c->gsi_err) {
			/* if i2c error already present, no need to update error values */
			if (!gi2c->err) {
				gi2c->err = -EIO;
				ret = gi2c->err;
			}
			gi2c->gsi_err = false;
		}

		if (gi2c->is_shared)
			/* Resend cfg tre for every new message on shared se */
			gi2c->cfg_sent = 0;

geni_i2c_gsi_cancel_pending:
		if (msgs[i].flags & I2C_M_RD) {
			geni_se_common_iommu_unmap_buf(gi2c->wrapper_dev, &gi2c->rx_ph,
						       msgs[i].len, DMA_FROM_DEVICE);
			i2c_put_dma_safe_msg_buf(rd_dma_buf, &msgs[i], !gi2c->err);
		} else if (gi2c->err) {
			/* for multi descriptor unmap all submitted tre's */
			if (gi2c->gsi_tx.is_multi_descriptor)
				gi2c_gsi_tre_process(gi2c, num);
			else
				gi2c_gsi_tx_unmap(gi2c, i, wr_idx - 1);
		}
		if (gi2c->err)
			goto geni_i2c_gsi_xfer_out;

		geni_i2c_calc_xfer_time(gi2c, msgs, start_time, i, __func__);
	}

geni_i2c_gsi_xfer_out:
	/* clearing the gpi multi descriptor flag */
	#if IS_ENABLED(CONFIG_MSM_GPI_DMA)
	if (gi2c->gsi_tx.is_multi_descriptor)
		gpi_update_multi_desc_flag(gi2c->tx_c, false, 0);
	#endif
	if (!ret && gi2c->err)
		ret = gi2c->err;
	I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev,
		    "%s Time took for %d xfers = %llu nsecs\n",
		    __func__, num, (sched_clock() - start_time_xfer));
	return ret;
}

/**
 * geni_i2c_execute_xfer() - Performs non GSI mode data transfer
 * @adap: Master controller handle
 * @msgs[]: i2c_msg structure as a pointer
 * @num: Nos messages to sent as an arg.
 *
 * Return: 0 on success OR negative error code for failure.
 */

static int geni_i2c_execute_xfer(struct geni_i2c_dev *gi2c,
				struct i2c_msg msgs[], int num)
{
	int i, ret = 0, timeout = 0;
	u32 geni_ios = 0;
	unsigned long long start_time;
	unsigned long long start_time_xfer = sched_clock();

	for (i = 0; i < num; i++) {
		int stretch = (i < (num - 1));
		u32 m_param = 0;
		u32 m_cmd = 0;
		u8 *dma_buf = NULL;
		dma_addr_t tx_dma = 0;
		dma_addr_t rx_dma = 0;
		enum geni_se_xfer_mode mode = GENI_SE_FIFO;

		start_time = geni_capture_start_time(&gi2c->i2c_rsc, gi2c->ipc_log_kpi, __func__,
						     gi2c->i2c_kpi);
		reinit_completion(&gi2c->xfer);

		m_param |= (stretch ? STOP_STRETCH : 0);
		m_param |= ((msgs[i].addr & 0x7F) << SLV_ADDR_SHFT);

		gi2c->cur = &msgs[i];
		if (!gi2c->cur) {
			I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev,
				    "%s: Invalid buffer\n", __func__);
			ret = -ENOMEM;
			goto geni_i2c_execute_xfer_exit;
		}
		qcom_geni_i2c_calc_timeout(gi2c);

		if (!gi2c->is_i2c_hub)
			mode = msgs[i].len > 32 ? GENI_SE_DMA : GENI_SE_FIFO;
		else
			mode = GENI_SE_FIFO; /* i2c hub has only FIFO mode */

		geni_se_select_mode(&gi2c->i2c_rsc, mode);

		if (mode == GENI_SE_DMA) {
			dma_buf = i2c_get_dma_safe_msg_buf(&msgs[i], 1);
			if (!dma_buf) {
				ret = -ENOMEM;
				goto geni_i2c_execute_xfer_exit;
			}
		}

		I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev,
			    "%s: stretch:%d, m_param:0x%x\n",
			    __func__, stretch, m_param);

		if (msgs[i].flags & I2C_M_RD) {
			I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev,
				"msgs[%d].len:%d R\n", i, gi2c->cur->len);
			geni_write_reg(msgs[i].len,
				       gi2c->base, SE_I2C_RX_TRANS_LEN);
			m_cmd = I2C_READ;
			geni_se_setup_m_cmd(&gi2c->i2c_rsc, m_cmd, m_param);
			if (mode == GENI_SE_DMA) {
				ret = geni_se_rx_dma_prep(&gi2c->i2c_rsc,
							dma_buf, msgs[i].len,
							&rx_dma);
				if (ret) {
					i2c_put_dma_safe_msg_buf(dma_buf,
							&msgs[i], false);
					mode = GENI_SE_FIFO;
					geni_se_select_mode(&gi2c->i2c_rsc,
								mode);
				} else if (gi2c->dbg_buf_ptr) {
					gi2c->dbg_buf_ptr[i].virt_buf =
								(void *)dma_buf;
					gi2c->dbg_buf_ptr[i].map_buf =
								(void *)&rx_dma;
				}
			}
		} else {
			I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev,
				"msgs[%d].len:%d W\n", i, gi2c->cur->len);
			geni_write_reg(msgs[i].len, gi2c->base,
						SE_I2C_TX_TRANS_LEN);
			m_cmd = I2C_WRITE;
			geni_se_setup_m_cmd(&gi2c->i2c_rsc, m_cmd, m_param);
			if (mode == GENI_SE_DMA) {
				ret = geni_se_tx_dma_prep(&gi2c->i2c_rsc,
							dma_buf, msgs[i].len,
							&tx_dma);
				if (ret) {
					i2c_put_dma_safe_msg_buf(dma_buf,
							&msgs[i], false);
					mode = GENI_SE_FIFO;
					geni_se_select_mode(&gi2c->i2c_rsc,
								mode);
				} else if (gi2c->dbg_buf_ptr) {
					gi2c->dbg_buf_ptr[i].virt_buf =
								(void *)dma_buf;
					gi2c->dbg_buf_ptr[i].map_buf =
								(void *)&tx_dma;
				}
			}
			if (mode == GENI_SE_FIFO) /* Get FIFO IRQ */
				geni_write_reg(1, gi2c->base,
						SE_GENI_TX_WATERMARK_REG);
		}
		/* Ensure FIFO write go through before waiting for Done evet */
		mb();
		mutex_unlock(&gi2c->i2c_ssr.ssr_lock);
		timeout = wait_for_completion_timeout(&gi2c->xfer,
						gi2c->xfer_timeout);
		mutex_lock(&gi2c->i2c_ssr.ssr_lock);
		if (gi2c->i2c_ssr.is_ssr_down) {
			I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev,
				    "%s: SSR Down\n", __func__);
			return 0;
		}
		if (!timeout) {
			u32 geni_ios = 0;
			u32 m_stat = readl_relaxed(gi2c->base + SE_GENI_M_IRQ_STATUS);

			/* clearing tx water mark and m_irq_status during delayed irq */
			writel_relaxed(0, (gi2c->base + SE_GENI_TX_WATERMARK_REG));
			if (m_stat)
				writel_relaxed(m_stat, gi2c->base + SE_GENI_M_IRQ_CLEAR);

			I2C_LOG_ERR(gi2c->ipcl, true, gi2c->dev,
				"I2C xfer timeout: %d\n", gi2c->xfer_timeout);
			geni_i2c_err(gi2c, GENI_TIMEOUT);

			/* WAR: Set flag to mark cancel pending if IOS bad */
			geni_ios = geni_read_reg(gi2c->base, SE_GENI_IOS);
			if ((geni_ios & 0x3) != 0x3) { //SCL:b'1, SDA:b'0
				I2C_LOG_DBG(gi2c->ipcl, true, gi2c->dev,
					"%s: IO lines not good: 0x%x\n",
					__func__, geni_ios);
				/* doing pending cancel only rtl based SE's */
				if (gi2c->is_i2c_rtl_based) {
					gi2c->prev_cancel_pending = true;
					goto geni_i2c_execute_xfer_exit;
				}
			}
		} else {
			if (msgs[i].flags & I2C_M_RD)
				I2C_LOG_DBG(gi2c->ipcl, true, gi2c->dev,
					    "%s: Read operation completed for len:%d\n",
					    __func__, msgs[i].len);
			else
				I2C_LOG_DBG(gi2c->ipcl, true, gi2c->dev,
					    "%s:Write operation completed for len:%d\n",
					    __func__,  msgs[i].len);
		}

		if (gi2c->err) {
			if (gi2c->is_i2c_rtl_based) {
				/* WAR: Set flag to mark cancel pending if IOS bad */
				geni_ios = geni_read_reg(gi2c->base, SE_GENI_IOS);
				if ((geni_ios & 0x3) != 0x3) { //SCL:b'1, SDA:b'0
					I2C_LOG_DBG(gi2c->ipcl, true, gi2c->dev,
						    "%s: IO lines not in good state\n",
						    __func__);
					gi2c->prev_cancel_pending = true;
					goto geni_i2c_execute_xfer_exit;
				}

				/* EBUSY set by ARB_LOST error condition */
				if (gi2c->err == -EBUSY) {
					I2C_LOG_DBG(gi2c->ipcl, true, gi2c->dev,
						    "%s:run reg68 war\n", __func__);
					do_reg68_war_for_rtl_se(gi2c);
				}
			}
			mutex_unlock(&gi2c->i2c_ssr.ssr_lock);
			geni_i2c_stop_with_cancel(gi2c);
			mutex_lock(&gi2c->i2c_ssr.ssr_lock);
			if (gi2c->i2c_ssr.is_ssr_down) {
				I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev,
					    "%s: SSR Down\n", __func__);
				return 0;
			}
		}
		gi2c->cur_wr = 0;
		gi2c->cur_rd = 0;
		if (mode == GENI_SE_DMA) {
			if (gi2c->err) {
				reinit_completion(&gi2c->xfer);
				if (msgs[i].flags != I2C_M_RD)
					writel_relaxed(1, gi2c->base +
							SE_DMA_TX_FSM_RST);
				else
					writel_relaxed(1, gi2c->base +
							SE_DMA_RX_FSM_RST);
				mutex_unlock(&gi2c->i2c_ssr.ssr_lock);
				wait_for_completion_timeout(&gi2c->xfer, HZ);
				mutex_lock(&gi2c->i2c_ssr.ssr_lock);
				if (gi2c->i2c_ssr.is_ssr_down) {
					I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev,
						    "%s: SSR Down\n", __func__);
					return 0;
				}
			}
			if (rx_dma)
				geni_se_rx_dma_unprep(&gi2c->i2c_rsc, rx_dma,
						      msgs[i].len);
			if (tx_dma)
				geni_se_tx_dma_unprep(&gi2c->i2c_rsc, tx_dma,
					      msgs[i].len);
			i2c_put_dma_safe_msg_buf(dma_buf, &msgs[i], !gi2c->err);
		}
		ret = gi2c->err;
		if (gi2c->err) {
			I2C_LOG_ERR(gi2c->ipcl, false, gi2c->dev,
				    "i2c error :%d\n", gi2c->err);
			mutex_unlock(&gi2c->i2c_ssr.ssr_lock);
			if (geni_i2c_bus_recovery(gi2c))
				GENI_SE_ERR(gi2c->ipcl, true, gi2c->dev,
					    "%s:Bus Recovery failed\n", __func__);
			mutex_lock(&gi2c->i2c_ssr.ssr_lock);
			if (gi2c->i2c_ssr.is_ssr_down) {
				I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev,
					    "%s: SSR Down\n", __func__);
				return 0;
			}
			break;
		}

		geni_i2c_calc_xfer_time(gi2c, msgs, start_time, i, __func__);
	}

geni_i2c_execute_xfer_exit:
	I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev,
		    "%s Time took for %d xfers = %llu nsecs\n",
		    __func__, num, (sched_clock() - start_time_xfer));
	return ret;
}

/**
 * geni_i2c_xfer() - Performs non GSI mode data transfer
 * @adap: Master controller handle
 * @msgs[]: i2c_msg structure as a pointer
 * @num: Nos messages to sent as an arg.
 *
 * Return: 0 on success OR negative error code for failure.
 */
static int geni_i2c_xfer(struct i2c_adapter *adap, struct i2c_msg msgs[],
								int num)
{
	struct geni_i2c_dev *gi2c = i2c_get_adapdata(adap);
	int ret = 0;
	u32 geni_ios = 0;
	unsigned long long start_time;

	start_time = geni_capture_start_time(&gi2c->i2c_rsc, gi2c->ipc_log_kpi, __func__,
					     gi2c->i2c_kpi);
	gi2c->err = 0;
	atomic_set(&gi2c->is_xfer_in_progress, 1);
	mutex_lock(&gi2c->i2c_ssr.ssr_lock);
	if (gi2c->i2c_ssr.is_ssr_down) {
		mutex_unlock(&gi2c->i2c_ssr.ssr_lock);
		atomic_set(&gi2c->is_xfer_in_progress, 0);
		I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev,
			    "%s: SSR Down\n", __func__);
		return -EINVAL;
	}

	/* Client to respect system suspend */
	if (!pm_runtime_enabled(gi2c->dev)) {
		mutex_unlock(&gi2c->i2c_ssr.ssr_lock);
		atomic_set(&gi2c->is_xfer_in_progress, 0);
		I2C_LOG_ERR(gi2c->ipcl, false, gi2c->dev,
			"%s: System suspended\n", __func__);
		return -EACCES;
	}

	/* Do Not vote if is_le_vm: LA votes and pm_ctrl_client: client votes */
	if (!gi2c->is_le_vm && !gi2c->pm_ctrl_client) {
		ret = pm_runtime_get_sync(gi2c->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(gi2c->dev);
			/* Set device in suspended since resume failed */
			pm_runtime_set_suspended(gi2c->dev);
			mutex_unlock(&gi2c->i2c_ssr.ssr_lock);
			atomic_set(&gi2c->is_xfer_in_progress, 0);
			I2C_LOG_ERR(gi2c->ipcl, true, gi2c->dev,
				    "error turning SE resources:%d\n", ret);
			return ret;
		}
	}

	// WAR : Complete previous pending cancel cmd
	if (gi2c->prev_cancel_pending) {
		mutex_unlock(&gi2c->i2c_ssr.ssr_lock);
		ret = do_pending_cancel(gi2c);
		mutex_lock(&gi2c->i2c_ssr.ssr_lock);
		if (gi2c->i2c_ssr.is_ssr_down) {
			I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev,
				    "%s: SSR Down\n", __func__);
			mutex_unlock(&gi2c->i2c_ssr.ssr_lock);
			return 0;
		}
		if (ret) {
			/* for levm skip auto suspend timer */
			if (!gi2c->is_le_vm) {
				pm_runtime_mark_last_busy(gi2c->dev);
				pm_runtime_put_autosuspend(gi2c->dev);
			}
			mutex_unlock(&gi2c->i2c_ssr.ssr_lock);
			atomic_set(&gi2c->is_xfer_in_progress, 0);
			return ret; //Don't perform xfer is cancel failed
		}
	}

	geni_ios = geni_read_reg(gi2c->base, SE_GENI_IOS);
	if (!gi2c->is_shared && ((geni_ios & 0x3) != 0x3)) {//SCL:b'1, SDA:b'0
		I2C_LOG_ERR(gi2c->ipcl, false, gi2c->dev,
			    "IO lines in bad state, Power the slave\n");
		/* for levm skip auto suspend timer */
		if (!gi2c->is_le_vm) {
			pm_runtime_mark_last_busy(gi2c->dev);
			pm_runtime_put_autosuspend(gi2c->dev);
		}
		mutex_unlock(&gi2c->i2c_ssr.ssr_lock);
		atomic_set(&gi2c->is_xfer_in_progress, 0);
		return -ENXIO;
	}

	if (gi2c->is_le_vm && (!gi2c->first_xfer_done)) {
		/*
		 * For le-vm we are doing resume operations during
		 * the first xfer, because we are seeing probe sequence
		 * issues from client and i2c-master driver, due to this
		 * multiple times i2c_resume invoking and we are seeing
		 * unclocked access. To avoid this added resume operations
		 * here very first time.
		 */
		gi2c->first_xfer_done = true;
		ret = geni_i2c_prepare(gi2c);
		if (ret) {
			I2C_LOG_ERR(gi2c->ipcl, true, gi2c->dev,
				"%s I2C prepare failed: %d\n", __func__, ret);
			atomic_set(&gi2c->is_xfer_in_progress, 0);
			return ret;
		}

		ret = geni_i2c_lock_bus(gi2c);
		if (ret) {
			I2C_LOG_ERR(gi2c->ipcl, true, gi2c->dev,
				"%s lock failed: %d\n", __func__, ret);
			atomic_set(&gi2c->is_xfer_in_progress, 0);
			return ret;
		}
		I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev,
			"LE-VM first xfer\n");
	}

	I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev,
		"n:%d addr:0x%x\n", num, msgs[0].addr);

	gi2c->dbg_num = num;
	kfree(gi2c->dbg_buf_ptr);
	gi2c->dbg_buf_ptr =
		kcalloc(num, sizeof(struct dbg_buf_ctxt), GFP_KERNEL);
	if (!gi2c->dbg_buf_ptr)
		I2C_LOG_ERR(gi2c->ipcl, false, gi2c->dev,
			"Buf logging pointer not available\n");

	if (gi2c->se_mode == GSI_ONLY) {
		mutex_unlock(&gi2c->i2c_ssr.ssr_lock);
		ret = geni_i2c_gsi_xfer(adap, msgs, num);
		mutex_lock(&gi2c->i2c_ssr.ssr_lock);
		goto geni_i2c_txn_ret;
	} else {
		/* Don't set shared flag in non-GSI mode */
		gi2c->is_shared = false;
	}

	ret = geni_i2c_execute_xfer(gi2c, msgs, num);
geni_i2c_txn_ret:
	if (ret == 0)
		ret = num;

	/* Don't unvote if is_le_vm:LA voted and pm_ctrl_client:client voted
	 * Meaning autosuspend timer is only for regular usecase, not for the
	 * cases with is_le_vm and pm_ctrl_client flags.
	 */
	if (!gi2c->is_le_vm && !gi2c->pm_ctrl_client) {
		pm_runtime_mark_last_busy(gi2c->dev);
		pm_runtime_put_autosuspend(gi2c->dev);
	}

	atomic_set(&gi2c->is_xfer_in_progress, 0);
	gi2c->cur = NULL;
	gi2c->err = 0;
	mutex_unlock(&gi2c->i2c_ssr.ssr_lock);
	I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev,
			"i2c txn ret:%d freq=%dHz\n", ret, gi2c->clk_freq_out);
	geni_capture_stop_time(&gi2c->i2c_rsc, gi2c->ipc_log_kpi, __func__,
			       gi2c->i2c_kpi, start_time, 0, gi2c->clk_freq_out);
	return ret;
}

static u32 geni_i2c_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | (I2C_FUNC_SMBUS_EMUL & ~I2C_FUNC_SMBUS_QUICK);
}

static const struct i2c_algorithm geni_i2c_algo = {
	.master_xfer	= geni_i2c_xfer,
	.functionality	= geni_i2c_func,
};

#if I2C_HUB_DEF
static int get_geni_se_i2c_hub(struct geni_i2c_dev *gi2c)
{
	int ret = 0;
	int geni_hw_param;

	ret =  geni_se_common_clks_on(gi2c->i2c_rsc.clk, gi2c->m_ahb_clk, gi2c->s_ahb_clk);
	if (ret) {
		dev_err(gi2c->dev, "%s: Err in geni_se_clks_on %d\n", __func__, ret);
		return ret;
	}

	geni_hw_param = geni_read_reg(gi2c->base, GENI_HW_PARAM);
	if (geni_hw_param & I2C_MASTER_HUB)
		gi2c->is_i2c_hub = true;
	else
		gi2c->is_i2c_hub = false;

	geni_se_common_clks_off(gi2c->i2c_rsc.clk, gi2c->m_ahb_clk, gi2c->s_ahb_clk);
	return ret;
}
#endif

/**
 * geni_i2c_resources_init: initialize clk, icc vote, read dt property
 * @pdev: Platform driver handle
 * @gi2c: geni i2c structure as a pointer
 *
 * Function to initialize clock and icc vote configuration and read require
 * DTSI property.
 *
 * Return: 0 on success OR negative error code for failure.
 */
static int geni_i2c_resources_init(struct platform_device *pdev, struct geni_i2c_dev *gi2c)
{
	int ret;

	/*
	 * For LE, clocks, gpio and icb voting will be provided by
	 * LA. The I2C operates in GSI mode only for LE usecase,
	 * se irq not required. Below properties will not be present
	 * in I2C LE dt.
	 */
	if (gi2c->is_le_vm)
		return 0;

	gi2c->i2c_rsc.clk = devm_clk_get(&pdev->dev, "se-clk");
	if (IS_ERR(gi2c->i2c_rsc.clk)) {
		ret = PTR_ERR(gi2c->i2c_rsc.clk);
		dev_err(&pdev->dev, "Err getting SE Core clk %d\n",
			ret);
		return ret;
	}

	gi2c->m_ahb_clk = devm_clk_get(gi2c->dev->parent, "m-ahb");
	if (IS_ERR(gi2c->m_ahb_clk)) {
		ret = PTR_ERR(gi2c->m_ahb_clk);
		dev_err(&pdev->dev, "Err getting M AHB clk %d\n", ret);
		return ret;
	}

	gi2c->s_ahb_clk = devm_clk_get(gi2c->dev->parent, "s-ahb");
	if (IS_ERR(gi2c->s_ahb_clk)) {
		ret = PTR_ERR(gi2c->s_ahb_clk);
		dev_err(&pdev->dev, "Err getting S AHB clk %d\n", ret);
		return ret;
	}

	gi2c->is_i2c_hub = of_property_read_bool(pdev->dev.of_node,
						 "qcom,i2c-hub");

	gi2c->is_high_perf = of_property_read_bool(pdev->dev.of_node,
						   "qcom,high-perf");
	/*
	 * For I2C_HUB, qup-ddr voting not required and
	 * core clk should be voted explicitly.
	 */
	if (gi2c->is_i2c_hub) {
		gi2c->core_clk = devm_clk_get(&pdev->dev, "core-clk");
		if (IS_ERR(gi2c->core_clk)) {
			ret = PTR_ERR(gi2c->core_clk);
			dev_err(&pdev->dev, "Err getting core-clk %d\n", ret);
			return ret;
		}
		ret = geni_icc_get(&gi2c->i2c_rsc, NULL);
		if (ret) {
			dev_err(&pdev->dev, "%s: Error - geni_icc_get ret:%d\n",
				__func__, ret);
			return ret;
		}
		gi2c->i2c_rsc.icc_paths[GENI_TO_CORE].avg_bw = GENI_DEFAULT_BW;
		gi2c->i2c_rsc.icc_paths[CPU_TO_GENI].avg_bw = GENI_DEFAULT_BW;

		/* For I2C HUB, we don't have HW reg to identify RTL/SW base SE.
		 * Hence setting flag for all I2C HUB instances.
		 */
		gi2c->is_i2c_rtl_based  = true;
		dev_info(gi2c->dev, "%s: RTL based SE\n", __func__);
	} else {
		if (gi2c->is_high_perf)
			ret =
			geni_se_common_rsc_init(&gi2c->rsc,
						I2C_CORE2X_VOTE, GENI_DEFAULT_BW,
						(DEFAULT_SE_CLK * DEFAULT_BUS_WIDTH));
		else
			ret =
			geni_se_common_rsc_init(&gi2c->rsc,
						GENI_DEFAULT_BW, GENI_DEFAULT_BW,
						Bps_to_icc(gi2c->clk_freq_out));
		if (ret) {
			dev_err(&pdev->dev, "%s: Error - resources_init ret:%d\n",
				__func__, ret);
			return ret;
		}
	}

	gi2c->irq = platform_get_irq(pdev, 0);
	if (gi2c->irq < 0)
		return gi2c->irq;

	irq_set_status_flags(gi2c->irq, IRQ_NOAUTOEN);
	ret = devm_request_irq(gi2c->dev, gi2c->irq, geni_i2c_irq,
			       0, "i2c_geni", gi2c);
	if (ret) {
		dev_err(gi2c->dev, "Request_irq failed:%d: err:%d\n",
			gi2c->irq, ret);
		return ret;
	}

	return 0;
}
static int geni_i2c_probe(struct platform_device *pdev)
{
	struct geni_i2c_dev *gi2c;
	struct resource *res;
	int ret;
	struct device *dev = &pdev->dev;
	#if (IS_ENABLED(CONFIG_BOOTMARKER_PROXY))
	char boot_marker[BOOT_MARKER_SIZE];
	#endif

	gi2c = devm_kzalloc(&pdev->dev, sizeof(*gi2c), GFP_KERNEL);
	if (!gi2c)
		return -ENOMEM;

	if (arr_idx < MAX_SE)
		/* Debug purpose */
		gi2c_dev_dbg[arr_idx++] = gi2c;

	gi2c->dev = dev;

	#if (IS_ENABLED(CONFIG_BOOTMARKER_PROXY))
		snprintf(boot_marker, sizeof(boot_marker),
					"M - DRIVER GENI_I2C Init");
		bootmarker_place_marker(boot_marker);
	#else
		dev_dbg(&pdev->dev, "M - DRIVER GENI_I2C Init\n");
	#endif

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -EINVAL;

	gi2c->base = devm_ioremap_resource(gi2c->dev, res);
	if (IS_ERR(gi2c->base))
		return PTR_ERR(gi2c->base);

	if (of_property_read_bool(pdev->dev.of_node, "qcom,le-vm")) {
		gi2c->is_le_vm = true;
		gi2c->first_xfer_done = false;
		dev_info(&pdev->dev, "LE-VM usecase\n");
	}

	if (of_property_read_bool(pdev->dev.of_node, "qcom,pm-ctrl-client")) {
		gi2c->pm_ctrl_client = true;
		dev_info(&pdev->dev, "Client controls the I2C PM\n");
	}

	if (of_property_read_bool(pdev->dev.of_node, "qcom,leica-used-i2c"))
		gi2c->skip_bw_vote = true;

	gi2c->i2c_test_dev = false;
	if (of_property_read_bool(pdev->dev.of_node, "qcom,i2c-test-dev")) {
		gi2c->i2c_test_dev = true;
		dev_info(&pdev->dev, "%s: This is I2C device under test\n", __func__);
	}

	gi2c->i2c_rsc.dev = dev;
	gi2c->i2c_rsc.wrapper = dev_get_drvdata(dev->parent);
	gi2c->i2c_rsc.base = gi2c->base;
	gi2c->wrapper_dev = dev->parent;

	gi2c->rsc.se_rsc = &gi2c->i2c_rsc;
	gi2c->rsc.ctrl_dev = dev;

	if (!gi2c->i2c_rsc.wrapper) {
		dev_err(&pdev->dev, "SE Wrapper is NULL, deferring probe\n");
		return -EPROBE_DEFER;
	}

	if (of_property_read_u32(pdev->dev.of_node, "qcom,clk-freq-out",
				&gi2c->clk_freq_out))
		gi2c->clk_freq_out = KHz(400);
	dev_info(&pdev->dev, "Bus frequency is set to %dHz.\n",
						gi2c->clk_freq_out);
	gi2c->is_deep_sleep = false;

	ret = geni_i2c_clk_map_idx(gi2c);
	if (ret) {
		dev_err(gi2c->dev, "Invalid clk frequency %d KHz: %d\n",
				gi2c->clk_freq_out, ret);
		return ret;
	}
	gi2c->rsc.rsc_ssr.ssr_enable =
		of_property_read_bool(pdev->dev.of_node, "qcom,ssr-enable");
	if (gi2c->rsc.rsc_ssr.ssr_enable) {
		gi2c->rsc.rsc_ssr.force_suspend = ssr_i2c_force_suspend;
		gi2c->rsc.rsc_ssr.force_resume = ssr_i2c_force_resume;
	}
	mutex_init(&gi2c->i2c_ssr.ssr_lock);

	/*
	 * For LE, clocks, gpio and icb voting will be provided by
	 * LA. The I2C operates in GSI mode only for LE usecase,
	 * se irq not required. Below properties will not be present
	 * in I2C LE dt.
	 */
	ret = geni_i2c_resources_init(pdev, gi2c);
	if (ret)
		return ret;

	if (of_property_read_bool(pdev->dev.of_node, "qcom,shared")) {
		gi2c->is_shared = true;
		dev_info(&pdev->dev, "Multi-EE usecase\n");
	}

	//Strictly only for debug, it's client/slave device decision for an SE.
	if (of_property_read_bool(pdev->dev.of_node, "qcom,bus-recovery")) {
		gi2c->bus_recovery_enable  = true;
		dev_dbg(&pdev->dev, "%s:I2C Bus recovery enabled\n", __func__);
	}

	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (ret) {
		ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
		if (ret) {
			dev_err(&pdev->dev, "could not set DMA mask\n");
			return ret;
		}
	}

	gi2c->tx_sg = dmam_alloc_coherent(gi2c->dev, 5*sizeof(struct scatterlist),
					&gi2c->tx_sg_dma, GFP_KERNEL);
	if (!gi2c->tx_sg) {
		dev_err(&pdev->dev, "could not allocate for tx_sg\n");
		return -ENOMEM;
	}

	gi2c->rx_sg = dmam_alloc_coherent(gi2c->dev, sizeof(struct scatterlist),
					&gi2c->rx_sg_dma, GFP_KERNEL);
	if (!gi2c->rx_sg) {
		dev_err(&pdev->dev, "could not allocate for rx_sg\n");
		return -ENOMEM;
	}

	gi2c->adap.algo = &geni_i2c_algo;
	init_completion(&gi2c->xfer);
	init_completion(&gi2c->m_cancel_cmd);
	platform_set_drvdata(pdev, gi2c);
	i2c_set_adapdata(&gi2c->adap, gi2c);
	gi2c->adap.dev.parent = gi2c->dev;
	gi2c->adap.dev.of_node = pdev->dev.of_node;

	strscpy(gi2c->adap.name, "Geni-I2C", sizeof(gi2c->adap.name));

	pm_runtime_set_suspended(gi2c->dev);
	/* for levm skip auto suspend timer */
	if (!gi2c->is_le_vm) {
		pm_runtime_set_autosuspend_delay(gi2c->dev, I2C_AUTO_SUSPEND_DELAY);
		pm_runtime_use_autosuspend(gi2c->dev);
	}
	pm_runtime_enable(gi2c->dev);
	ret = i2c_add_adapter(&gi2c->adap);
	if (ret) {
		dev_err(gi2c->dev, "Add adapter failed, ret=%d\n", ret);
		return ret;
	}

	device_create_file(gi2c->dev, &dev_attr_capture_kpi);
	atomic_set(&gi2c->is_xfer_in_progress, 0);
	if (gi2c->i2c_test_dev) {
		/* configure Test bus to dump test bus later, only once */
		test_bus_enable_per_qupv3(gi2c->wrapper_dev, gi2c->ipcl);
	}
	#if (IS_ENABLED(CONFIG_BOOTMARKER_PROXY))
		snprintf(boot_marker, sizeof(boot_marker),
					"M - DRIVER GENI_I2C_%d Ready", gi2c->adap.nr);
		bootmarker_place_marker(boot_marker);
	#else
		dev_dbg(&pdev->dev, "M - DRIVER GENI_I2C_%d Ready\n", gi2c->adap.nr);
	#endif

	dev_info(gi2c->dev, "I2C probed\n");
	return 0;
}

static int geni_i2c_remove(struct platform_device *pdev)
{
	struct geni_i2c_dev *gi2c = platform_get_drvdata(pdev);
	int i;

	if (atomic_read(&gi2c->is_xfer_in_progress)) {
		I2C_LOG_ERR(gi2c->ipcl, true, gi2c->dev,
			    "%s: Xfer is in progress\n", __func__);
		return -EBUSY;
	}

	if (!pm_runtime_status_suspended(gi2c->dev)) {
		if (geni_i2c_runtime_suspend(gi2c->dev))
			I2C_LOG_ERR(gi2c->ipcl, true, gi2c->dev,
				    "%s: runtime suspend failed\n", __func__);
	}

	if (gi2c->se_mode == GSI_ONLY) {
		if (gi2c->tx_c) {
			I2C_LOG_ERR(gi2c->ipcl, true, gi2c->dev,
				    "%s: clearing tx dma resource\n", __func__);
			dma_release_channel(gi2c->tx_c);
		}
		if (gi2c->rx_c) {
			I2C_LOG_ERR(gi2c->ipcl, true, gi2c->dev,
				    "%s: clearing rx dma resource\n", __func__);
			dma_release_channel(gi2c->rx_c);
		}
	}

	pm_runtime_put_noidle(gi2c->dev);
	pm_runtime_set_suspended(gi2c->dev);
	pm_runtime_disable(gi2c->dev);
	i2c_del_adapter(&gi2c->adap);

	for (i = 0; i < arr_idx; i++)
		gi2c_dev_dbg[i] = NULL;
	arr_idx = 0;

	device_remove_file(gi2c->dev, &dev_attr_capture_kpi);

	if (gi2c->ipc_log_kpi)
		ipc_log_context_destroy(gi2c->ipc_log_kpi);

	if (gi2c->ipcl)
		ipc_log_context_destroy(gi2c->ipcl);
	return 0;
}

/**
 * geni_i2c_shutdown():shutdown call back function for i2c bus
 * @pdev: platform device
 *
 * This function will be called as a part of device reboot or shutdown
 *
 * Return: None
 */
static void geni_i2c_shutdown(struct platform_device *pdev)
{
	struct geni_i2c_dev *gi2c = platform_get_drvdata(pdev);

	I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev, "Enter %s:%d\n", __func__, true);
	/* Make client i2c transfers start failing */
	i2c_mark_adapter_suspended(&gi2c->adap);
}

static int geni_i2c_resume_early(struct device *device)
{
	struct geni_i2c_dev *gi2c = dev_get_drvdata(device);

	geni_se_ssc_clk_enable(&gi2c->rsc, true);
	I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev, "%s ret=%d\n", __func__, true);

	if (!gi2c->is_le_vm && pm_suspend_target_state == PM_SUSPEND_MEM) {
		gi2c->se_mode = UNINITIALIZED;
		gi2c->is_deep_sleep = true;
	}
	return 0;
}

static int geni_i2c_hib_resume_noirq(struct device *device)
{
	struct geni_i2c_dev *gi2c = dev_get_drvdata(device);

	I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev, "%s\n", __func__);
	gi2c->se_mode = UNINITIALIZED;
	geni_se_ssc_clk_enable(&gi2c->rsc, true);

	return 0;
}

/*
 * get sync/put sync in LA-VM -> do resources on/off
 * get sync/put sync in LE-VM -> do lock/unlock gpii
 */
#if IS_ENABLED(CONFIG_PM)
static int geni_i2c_gpi_pause_resume(struct geni_i2c_dev *gi2c, bool is_suspend)
{
	int tx_ret = 0;

	/* Do dma operations only for tx channel here, as it takes care of rx channel
	 * also internally from the GPI driver functions. if we call for both channels,
	 * will see channels in wrong state due to double operations.
	 */

	if (gi2c->tx_c) {
		if (is_suspend) {
			tx_ret = dmaengine_pause(gi2c->tx_c);
		} else {
			/* For deep sleep need to restore the config similar to the probe,
			 * hence using MSM_GPI_DEEP_SLEEP_INIT flag, in gpi_resume it will
			 * do similar to the probe. After this we should set this flag to
			 * MSM_GPI_DEFAULT, means gpi probe state is restored.
			 */
			if (gi2c->is_deep_sleep)
				gi2c->tx_ev.cmd = MSM_GPI_DEEP_SLEEP_INIT;

			tx_ret = dmaengine_resume(gi2c->tx_c);
			if (gi2c->is_deep_sleep) {
				gi2c->tx_ev.cmd = MSM_GPI_DEFAULT;
				gi2c->is_deep_sleep = false;
			}
		}

		if (tx_ret) {
			I2C_LOG_ERR(gi2c->ipcl, true, gi2c->dev,
				    "%s failed: tx:%d status:%d\n",
				    __func__, tx_ret, is_suspend);
			return -EINVAL;
		}
	}
	return 0;
}

static int geni_i2c_runtime_suspend(struct device *dev)
{
	int ret = 0;
	struct geni_i2c_dev *gi2c = dev_get_drvdata(dev);
	unsigned long long start_time;

	start_time = geni_capture_start_time(&gi2c->i2c_rsc, gi2c->ipc_log_kpi, __func__,
					     gi2c->i2c_kpi);

	if (gi2c->se_mode == FIFO_SE_DMA)
		disable_irq(gi2c->irq);

	if (gi2c->se_mode == GSI_ONLY) {
		if (!gi2c->is_le_vm) {
			ret = geni_i2c_gpi_pause_resume(gi2c, true);
			if (ret) {
				I2C_LOG_ERR(gi2c->ipcl, false, gi2c->dev,
					"%s: ret:%d\n", __func__, ret);
				return ret;
			}
		}
	}

	if (gi2c->skip_bw_vote) {
		if (gi2c->is_shared) {
			/* Do not unconfigure GPIOs if shared se */
			geni_se_common_clks_off(gi2c->i2c_rsc.clk,
						gi2c->m_ahb_clk, gi2c->s_ahb_clk);
		} else if (!gi2c->is_le_vm) {
			geni_se_resources_off(&gi2c->i2c_rsc);
		}

		goto skip_bw_vote;
	}

	if (gi2c->is_le_vm && gi2c->first_xfer_done) {
		geni_i2c_unlock_bus(gi2c);

		if (gi2c->se_mode == GSI_ONLY) {
			ret = geni_i2c_gpi_pause_resume(gi2c, true);
			if (ret) {
				I2C_LOG_ERR(gi2c->ipcl, false, gi2c->dev,
					"%s: ret:%d\n", __func__, ret);
				return ret;
			}
		}
	}
	else if (gi2c->is_shared) {
		/* Do not unconfigure GPIOs if shared se */
		geni_se_common_clks_off(gi2c->i2c_rsc.clk, gi2c->m_ahb_clk, gi2c->s_ahb_clk);
		ret = geni_icc_disable(&gi2c->i2c_rsc);
		if (ret)
			I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev,
			"%s failing at geni_icc_disable ret=%d\n", __func__, ret);
	} else if (!gi2c->is_le_vm) {
		geni_se_resources_off(&gi2c->i2c_rsc);
		ret = geni_icc_disable(&gi2c->i2c_rsc);
		if (ret)
			I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev,
			"%s failing at geni_icc_disable ret=%d\n", __func__, ret);
	}

skip_bw_vote:
	if (gi2c->is_i2c_hub)
		clk_disable_unprepare(gi2c->core_clk);

	I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev, "%s ret=%d\n", __func__, ret);
	geni_capture_stop_time(&gi2c->i2c_rsc, gi2c->ipc_log_kpi, __func__,
			       gi2c->i2c_kpi, start_time, 0, 0);
	return 0;
}

static int geni_i2c_runtime_resume(struct device *dev)
{
	int ret = 0;
	struct geni_i2c_dev *gi2c = dev_get_drvdata(dev);
	unsigned long long start_time;

	start_time = geni_capture_start_time(&gi2c->i2c_rsc, gi2c->ipc_log_kpi, __func__,
					     gi2c->i2c_kpi);

	if (!gi2c->ipcl) {
		char ipc_name[I2C_NAME_SIZE];

		snprintf(ipc_name, I2C_NAME_SIZE, "%s", dev_name(gi2c->dev));
		gi2c->ipcl = ipc_log_context_create(2, ipc_name, 0);
	}

	if (!gi2c->is_le_vm) {
		if (gi2c->skip_bw_vote)
			goto skip_bw_vote;

		ret = geni_icc_enable(&gi2c->i2c_rsc);
		if (ret) {
			I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev,
			"%s failing at geni icc enable ret=%d\n", __func__, ret);
			return ret;
		}

		ret = geni_icc_set_bw(&gi2c->i2c_rsc);
		if (ret) {
			I2C_LOG_ERR(gi2c->ipcl, true, gi2c->dev,
			"%s failing at icc set bw ret=%d\n", __func__, ret);
			return ret;
		}
		I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev,
			"%s: GENI_TO_CORE:%d CPU_TO_GENI:%d GENI_TO_DDR:%d\n",
			__func__, gi2c->i2c_rsc.icc_paths[GENI_TO_CORE].avg_bw,
			gi2c->i2c_rsc.icc_paths[CPU_TO_GENI].avg_bw,
			gi2c->i2c_rsc.icc_paths[GENI_TO_DDR].avg_bw);

skip_bw_vote:
		if (gi2c->is_i2c_hub) {
			ret = clk_prepare_enable(gi2c->core_clk);
			if (ret) {
				I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev,
				"%s failing at core clk prepare enable ret=%d\n", __func__, ret);
				return ret;
			}
		}

		/* Do not control clk/gpio/icb for LE-VM */
		ret = geni_se_resources_on(&gi2c->i2c_rsc);
		if (ret)
			return ret;

		ret = geni_i2c_prepare(gi2c);
		if (ret) {
			dev_err(gi2c->dev, "I2C prepare failed: %d\n", ret);
			return ret;
		}

		geni_write_reg(0x7f, gi2c->base, GENI_OUTPUT_CTRL);
		/*
		 * Added 10 us delay to settle the write of the register as per
		 * HW team recommendation
		 */
		udelay(10);

		if (gi2c->se_mode == FIFO_SE_DMA)
			enable_irq(gi2c->irq);

		if (gi2c->se_mode == GSI_ONLY) {
			ret = geni_i2c_gpi_pause_resume(gi2c, false);
			if (ret) {
				I2C_LOG_ERR(gi2c->ipcl, false, gi2c->dev,
					"%s: ret:%d\n", __func__, ret);
				return ret;
			}
		}

	} else if (gi2c->is_le_vm && gi2c->first_xfer_done) {
		/*
		 * For le-vm we are doing resume operations during
		 * the first xfer, because we are seeing probe
		 * sequence issues from client and i2c-master driver,
		 * due to thils multiple times i2c_resume invoking
		 * and we are seeing unclocked access. To avoid this
		 * below opeations we are doing in i2c_xfer very first
		 * time, after first xfer below logic will continue.
		 */
		ret = geni_i2c_prepare(gi2c);
		if (ret) {
			dev_err(gi2c->dev, "I2C prepare failed:%d\n", ret);
			return ret;
		}

		if (gi2c->se_mode == GSI_ONLY) {
			ret = geni_i2c_gpi_pause_resume(gi2c, false);
			if (ret) {
				I2C_LOG_ERR(gi2c->ipcl, false, gi2c->dev,
					"%s: ret:%d\n", __func__, ret);
				return ret;
			}
		}

		ret = geni_i2c_lock_bus(gi2c);
		if (ret) {
			I2C_LOG_ERR(gi2c->ipcl, true, gi2c->dev,
				"%s failed: %d\n", __func__, ret);
			return ret;
		}
	}

	I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev, "%s ret=%d\n", __func__, ret);
	geni_capture_stop_time(&gi2c->i2c_rsc, gi2c->ipc_log_kpi, __func__,
			       gi2c->i2c_kpi, start_time, 0, 0);
	return 0;
}

static int geni_i2c_suspend_late(struct device *device)
{
	struct geni_i2c_dev *gi2c = dev_get_drvdata(device);
	int ret;
	unsigned long long start_time;

	start_time = geni_capture_start_time(&gi2c->i2c_rsc, gi2c->ipc_log_kpi, __func__,
					     gi2c->i2c_kpi);

	if (atomic_read(&gi2c->is_xfer_in_progress)) {
		if (!pm_runtime_status_suspended(gi2c->dev)) {
			I2C_LOG_ERR(gi2c->ipcl, true, gi2c->dev,
				    ":%s: runtime PM is active\n", __func__);
			return -EBUSY;
		}
		I2C_LOG_ERR(gi2c->ipcl, true, gi2c->dev,
			    "%s System suspend not allowed while xfer in progress\n",
			    __func__);
		return -EBUSY;
	}
	/* Make sure no transactions are pending */
	ret = i2c_trylock_bus(&gi2c->adap, I2C_LOCK_SEGMENT);
	if (!ret) {
		I2C_LOG_ERR(gi2c->ipcl, true, gi2c->dev,
				"late I2C transaction request\n");
		return -EBUSY;
	}
	if (!pm_runtime_status_suspended(device)) {
		I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev,
			"%s: Force suspend\n", __func__);
		geni_i2c_runtime_suspend(device);
		pm_runtime_disable(device);
		pm_runtime_set_suspended(device);
		pm_runtime_enable(device);
	}

	geni_se_ssc_clk_enable(&gi2c->rsc, false);
	i2c_unlock_bus(&gi2c->adap, I2C_LOCK_SEGMENT);
	I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev, "%s ret=%d\n", __func__, ret);
	geni_capture_stop_time(&gi2c->i2c_rsc, gi2c->ipc_log_kpi, __func__,
			       gi2c->i2c_kpi, start_time, 0, 0);
	return 0;
}
#else
static int geni_i2c_runtime_suspend(struct device *dev)
{
	return 0;
}

static int geni_i2c_runtime_resume(struct device *dev)
{
	return 0;
}

static int geni_i2c_suspend_late(struct device *device)
{
	return 0;
}
#endif

static void ssr_i2c_force_suspend(struct device *dev)
{
	struct geni_i2c_dev *gi2c = dev_get_drvdata(dev);
	int ret = 0;

	mutex_lock(&gi2c->i2c_ssr.ssr_lock);
	gi2c->i2c_ssr.is_ssr_down = true;
	if (!pm_runtime_status_suspended(gi2c->dev)) {
		mutex_unlock(&gi2c->i2c_ssr.ssr_lock);
		ret =  geni_i2c_runtime_suspend(gi2c->dev);
		mutex_lock(&gi2c->i2c_ssr.ssr_lock);
		if (ret) {
			I2C_LOG_ERR(gi2c->ipcl, true, gi2c->dev, "runtime suspend failed\n");
		} else {
			pm_runtime_disable(gi2c->dev);
			pm_runtime_set_suspended(gi2c->dev);
			pm_runtime_enable(gi2c->dev);
		}
	}

	mutex_unlock(&gi2c->i2c_ssr.ssr_lock);
	I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev, "SSR force suspend done\n");
}

static void ssr_i2c_force_resume(struct device *dev)
{
	struct geni_i2c_dev *gi2c = dev_get_drvdata(dev);

	mutex_lock(&gi2c->i2c_ssr.ssr_lock);
	gi2c->i2c_ssr.is_ssr_down = false;
	gi2c->se_mode = UNINITIALIZED;
	mutex_unlock(&gi2c->i2c_ssr.ssr_lock);
	I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev, "SSR force resume done\n");
}

static const struct dev_pm_ops geni_i2c_pm_ops = {
	.suspend_late		= geni_i2c_suspend_late,
	.resume_early		= geni_i2c_resume_early,
	.runtime_suspend	= geni_i2c_runtime_suspend,
	.runtime_resume		= geni_i2c_runtime_resume,
	.freeze                 = geni_i2c_suspend_late,
	.restore                = geni_i2c_hib_resume_noirq,
	.thaw			= geni_i2c_hib_resume_noirq,
};

static const struct of_device_id geni_i2c_dt_match[] = {
	{ .compatible = "qcom,i2c-geni" },
	{}
};
MODULE_DEVICE_TABLE(of, geni_i2c_dt_match);

static struct platform_driver geni_i2c_driver = {
	.probe  = geni_i2c_probe,
	.remove = geni_i2c_remove,
	.shutdown = geni_i2c_shutdown,
	.driver = {
		.name = "i2c_geni",
		.pm = &geni_i2c_pm_ops,
		.of_match_table = geni_i2c_dt_match,
	},
};

static int __init i2c_dev_init(void)
{
	return platform_driver_register(&geni_i2c_driver);
}

static void __exit i2c_dev_exit(void)
{
	platform_driver_unregister(&geni_i2c_driver);
}

module_init(i2c_dev_init);
module_exit(i2c_dev_exit);
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:i2c_geni");
