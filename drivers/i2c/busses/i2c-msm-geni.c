// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
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
#include <linux/soc/qcom/geni-se.h>
#include <linux/qcom-geni-se-common.h>
#include <linux/ipc_logging.h>
#include <linux/dmaengine.h>
#include <linux/msm_gpi.h>
#include <linux/ioctl.h>
#include <linux/pinctrl/consumer.h>
#include <linux/slab.h>

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
#define I2C_ADDR_NACK		10
#define I2C_DATA_NACK		11
#define GSI_TRE_FULL		12

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
#define MAX_NUM_TRE_MSGS	4

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

struct dbg_buf_ctxt {
	void *virt_buf;
	void *map_buf;
};

struct gsi_tre_queue {
	u32 msg_wr_idx; /* i2c msg write index */
	u32 msg_rd_idx; /* i2c msg read index */
	u8 dma_wr_idx; /* i2c dma buf write index */
	u8 dma_rd_idx; /* i2c dma buf red index */
	u32 unmap_msg_cnt; /* i2c msg unmap count */
	atomic_t msg_cnt; /* available tre msg count */
	u8 *dma_buf[MAX_NUM_TRE_MSGS];
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
	struct i2c_msg *cur;
	struct i2c_msg *msgs;
	atomic_t msg_wait;
	struct gsi_tre_queue gsi_tx;
	spinlock_t multi_tre_lock; /* multi tre spin lock */
	struct geni_se i2c_rsc;
	struct clk *m_ahb_clk;
	struct clk *s_ahb_clk;
	struct clk *core_clk;
	int cur_wr;
	int cur_rd;
	struct device *wrapper_dev;
	void *ipcl;
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
	bool pm_ctrl_client;
	bool req_chan;
	bool first_xfer_done; /* for le-vm doing lock/unlock, after first xfer initiated. */
	bool gpi_reset;
	bool is_i2c_hub;
	bool prev_cancel_pending; //Halt cancel till IOS in good state
	bool gsi_err; /* For every gsi error performing gsi reset */
	bool is_i2c_rtl_based; /* doing pending cancel only for rtl based SE's */
	bool skip_bw_vote; /* Used for PMIC over i2c use case to skip the BW vote */
	atomic_t is_xfer_in_progress; /* Used to maintain xfer inprogress status */
	bool bus_recovery_enable; /* To be enabled by client if needed */
};

static struct geni_i2c_dev *gi2c_dev_dbg[MAX_SE];
static int arr_idx;
static int geni_i2c_runtime_suspend(struct device *dev);

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
	{KHz(100), 7, 10, 11, 26},
	{KHz(400), 2,  7, 10, 24},
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
	geni_dma_tx_irq_en, geni_dma_rx_irq_en, geni_m_irq_en,
	geni_s_irq_en);
}

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
 * @gi2c:	I2C dev handle
 *
 * This is a generic function to stop serial engine, to be called as required.
 *
 * Return: 0 if Success, non zero value if failed.
 */
static int geni_i2c_stop_with_cancel(struct geni_i2c_dev *gi2c)
{
	int timeout = 0;

	reinit_completion(&gi2c->xfer);
	geni_se_cancel_m_cmd(&gi2c->i2c_rsc);
	timeout = wait_for_completion_timeout(&gi2c->xfer, HZ);
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
	} else {
		reinit_completion(&gi2c->xfer);
		geni_se_cancel_m_cmd(&gi2c->i2c_rsc);
		timeout = wait_for_completion_timeout(&gi2c->xfer, HZ);
		if (!timeout) {
			I2C_LOG_DBG(gi2c->ipcl, true, gi2c->dev,
				"%s:Pending Cancel failed\n", __func__);
			reinit_completion(&gi2c->xfer);
			geni_se_abort_m_cmd(&gi2c->i2c_rsc);
			timeout = wait_for_completion_timeout(&gi2c->xfer, HZ);
			if (!timeout)
				I2C_LOG_DBG(gi2c->ipcl, true, gi2c->dev,
					"%s:Abort failed\n", __func__);
		}
	}
	gi2c->prev_cancel_pending = false;
	I2C_LOG_DBG(gi2c->ipcl, true, gi2c->dev,
		"%s: Pending Cancel done\n", __func__);
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
				    "SMP: %s: pm_runtime_get_sync bypassed\n", __func__);
	}
	return 0;
}

static irqreturn_t geni_i2c_irq(int irq, void *dev)
{
	struct geni_i2c_dev *gi2c = dev;
	int i, j;
	u32 m_stat = readl_relaxed(gi2c->base + SE_GENI_M_IRQ_STATUS);
	u32 rx_st = readl_relaxed(gi2c->base + SE_GENI_RX_FIFO_STATUS);
	u32 dm_tx_st = readl_relaxed(gi2c->base + SE_DMA_TX_IRQ_STAT);
	u32 dm_rx_st = readl_relaxed(gi2c->base + SE_DMA_RX_IRQ_STAT);
	u32 dma = readl_relaxed(gi2c->base + SE_GENI_DMA_MODE_EN);
	struct i2c_msg *cur = gi2c->cur;

	I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev,
		    "%s: m_irq_status:0x%x\n", __func__, m_stat);

	if (!cur) {
		geni_i2c_se_dump_dbg_regs(&gi2c->i2c_rsc, gi2c->base, gi2c->ipcl);
		I2C_LOG_ERR(gi2c->ipcl, false, gi2c->dev, "Spurious irq\n");
		goto irqret;
	}

	if ((m_stat & M_CMD_FAILURE_EN) ||
		(dm_rx_st & (DM_I2C_CB_ERR)) ||
		(m_stat & M_CMD_CANCEL_EN) ||
		(m_stat & M_CMD_ABORT_EN) ||
		(m_stat & M_GP_IRQ_1_EN) ||
		(m_stat & M_GP_IRQ_3_EN) ||
		(m_stat & M_GP_IRQ_4_EN)) {

		if (m_stat & M_GP_IRQ_1_EN) {
			if (readl_relaxed(gi2c->base + SE_GENI_M_GP_LENGTH))
				geni_i2c_err(gi2c, I2C_DATA_NACK);
			else
				geni_i2c_err(gi2c, I2C_ADDR_NACK);
		}
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

		if (!dma)
			writel_relaxed(0, (gi2c->base +
					   SE_GENI_TX_WATERMARK_REG));
		gi2c->cmd_done = true;
		goto irqret;
	}

	if (((m_stat & M_RX_FIFO_WATERMARK_EN) ||
		(m_stat & M_RX_FIFO_LAST_EN)) && (cur->flags & I2C_M_RD)) {
		u32 rxcnt = rx_st & RX_FIFO_WC_MSK;

		for (j = 0; j < rxcnt; j++) {
			u32 temp;
			int p;

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
			writel_relaxed(temp, gi2c->base + SE_GENI_TX_FIFOn);
			gi2c->cur_wr = i;
			dev_dbg(gi2c->dev, "FIFO i:%d,wrote 0x%x\n", i, temp);
			if (gi2c->cur_wr == cur->len) {
				dev_dbg(gi2c->dev, "FIFO i2c bytes done writing\n");
				writel_relaxed(0,
				(gi2c->base + SE_GENI_TX_WATERMARK_REG));
				break;
			}
		}
	}
irqret:
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
	}

	else if (m_stat & M_CMD_DONE_EN)
		gi2c->cmd_done = true;

	if (gi2c->cmd_done) {
		gi2c->cmd_done = false;
		complete(&gi2c->xfer);
	}

	return IRQ_HANDLED;
}

static void gi2c_ev_cb(struct dma_chan *ch, struct msm_gpi_cb const *cb_str,
		       void *ptr)
{
	struct geni_i2c_dev *gi2c = ptr;
	u32 m_stat = cb_str->status;

	switch (cb_str->cb_event) {
	case MSM_GPI_QUP_ERROR:
	case MSM_GPI_QUP_SW_ERROR:
	case MSM_GPI_QUP_MAX_EVENT:
	case MSM_GPI_QUP_CH_ERROR:
	case MSM_GPI_QUP_FW_ERROR:
	case MSM_GPI_QUP_PENDING_EVENT:
	case MSM_GPI_QUP_EOT_DESC_MISMATCH:
		break;
	case MSM_GPI_QUP_NOTIFY:
		if (m_stat & M_GP_IRQ_1_EN) {
			if (readl_relaxed(gi2c->base + SE_GENI_M_GP_LENGTH))
				geni_i2c_err(gi2c, I2C_DATA_NACK);
			else
				geni_i2c_err(gi2c, I2C_ADDR_NACK);
		}
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
	struct geni_i2c_dev *gi2c = cb->userdata;

	if (cb->status & DM_I2C_CB_ERR) {
		I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev,
			    "%s TCE Unexpected Err, stat:0x%x\n",
				xfer, cb->status);
		if (cb->status & (BIT(GP_IRQ1) << 5)) {
			if (readl_relaxed(gi2c->base + SE_GENI_M_GP_LENGTH))
				geni_i2c_err(gi2c, I2C_DATA_NACK);
			else
				geni_i2c_err(gi2c, I2C_ADDR_NACK);
		}
		if (cb->status & (BIT(GP_IRQ3) << 5))
			geni_i2c_err(gi2c, I2C_BUS_PROTO);
		if (cb->status & (BIT(GP_IRQ4) << 5))
			geni_i2c_err(gi2c, I2C_ARB_LOST);
	}
}

/**
 * gi2c_gsi_tre_process() - Process received TRE's from GSI HW
 * @gi2c: Base address of the gi2c dev structure.
 * @gsi_tre: Base address of the gsi_tre queue.
 *
 * This function is used to process received TRE's from GSI HW.
 * And also used for error case, it will clear and unmap all pending transfers.
 *
 * Return:  None.
 */
static void gi2c_gsi_tre_process(struct geni_i2c_dev *gi2c, struct gsi_tre_queue *gsi_tre)
{
	int wait_cnt = 0;

	I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev,
		    "%s:start unmap_cnt:%d rd_idx:%d\n",
		    __func__, gsi_tre->unmap_msg_cnt, gsi_tre->msg_rd_idx);

	if (gsi_tre->unmap_msg_cnt == gsi_tre->msg_rd_idx)
		return;

	/**
	 * When irq context and thread context are running independently
	 * on different cpu cores, read index is incremenated in irq context
	 * by one core while thread context which is being processed on
	 * another core is submitting quickly another descriptor for gsi hw.
	 * In this scenario previous irq context execution is still in
	 * progress and current descriptor wait for completion is cleared by
	 * previous descriptor in irq context, resulting in race condition.
	 * To solve this added explicit wait until irq context is processed
	 * when descriptors reached to maximum.
	 */
	if (atomic_read(&gi2c->gsi_tx.msg_cnt) == MAX_NUM_TRE_MSGS) {
		while (spin_is_locked(&gi2c->multi_tre_lock)) {
			if (wait_cnt % 10 == 0)
				I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev,
					    "%s: wait_cnt:%d\n", __func__, wait_cnt);
			wait_cnt++;
		}
	}

	while (gsi_tre->unmap_msg_cnt < gsi_tre->msg_rd_idx) {
		geni_se_common_iommu_unmap_buf(gi2c->wrapper_dev,
					       &gi2c->tx_ph[gsi_tre->dma_rd_idx],
					       gi2c->msgs[gsi_tre->unmap_msg_cnt].len,
					       DMA_TO_DEVICE);
		i2c_put_dma_safe_msg_buf(gsi_tre->dma_buf[gsi_tre->dma_rd_idx],
					 &gi2c->msgs[gsi_tre->unmap_msg_cnt],
					 !gi2c->err);
		gsi_tre->unmap_msg_cnt++;
		atomic_dec(&gi2c->gsi_tx.msg_cnt);
		gi2c->gsi_tx.dma_rd_idx = (gi2c->gsi_tx.dma_rd_idx + 1) % MAX_NUM_TRE_MSGS;
		I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev,
			    "%s: unmap_cnt:%d rd_idx:%d tx_cnt:%d\n",
			    __func__, gsi_tre->unmap_msg_cnt, gsi_tre->msg_rd_idx,
			    atomic_read(&gi2c->gsi_tx.msg_cnt));
	}
}

static void gi2c_gsi_tx_cb(void *ptr)
{
	struct msm_gpi_dma_async_tx_cb_param *tx_cb = ptr;
	struct geni_i2c_dev *gi2c = tx_cb->userdata;
	unsigned long flags;

	spin_lock_irqsave(&gi2c->multi_tre_lock, flags);
	gi2c_gsi_cb_err(tx_cb, "TX");

	/* error cases returning without processing tre's */
	if (gi2c->gsi_err || gi2c->err) {
		I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev,
			    "%s:rd_idx:%d wr_idx:%d tx_cnt:%d gsi_err:%d gi2c_err:%d\n",
			    __func__, gi2c->gsi_tx.msg_rd_idx,
			    gi2c->gsi_tx.msg_wr_idx,
			    atomic_read(&gi2c->gsi_tx.msg_cnt),
			    gi2c->gsi_err, gi2c->err);
		complete(&gi2c->xfer);
		spin_unlock_irqrestore(&gi2c->multi_tre_lock, flags);
		return;
	}

	if (atomic_read(&gi2c->gsi_tx.msg_cnt)) {
		gi2c->gsi_tx.msg_rd_idx++;
		I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev,
			    "%s:rd_idx:%d wr_idx:%d tx_cnt:%d\n",
			    __func__, gi2c->gsi_tx.msg_rd_idx,
			    gi2c->gsi_tx.msg_wr_idx,
			    atomic_read(&gi2c->gsi_tx.msg_cnt));
	} else {
		I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev,
			    "%s:else rd_idx:%d wr_idx:%d tx_cnt:%d\n",
			    __func__, gi2c->gsi_tx.msg_rd_idx,
			    gi2c->gsi_tx.msg_wr_idx,
			    atomic_read(&gi2c->gsi_tx.msg_cnt));
	}

	/**
	 * wait_for_completion and complete should be in balance mode.
	 * to balance this added msg_wait flag.
	 */
	if (atomic_read(&gi2c->msg_wait) ||
	    gi2c->gsi_tx.msg_rd_idx == gi2c->gsi_tx.msg_wr_idx) {
		atomic_set(&gi2c->msg_wait, 0);
		I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev,
			    "before complete %s:rd_idx:%d wr_idx:%d tx_cnt:%d\n",
			    __func__, gi2c->gsi_tx.msg_rd_idx,
			    gi2c->gsi_tx.msg_wr_idx,
			    atomic_read(&gi2c->gsi_tx.msg_cnt));
		complete(&gi2c->xfer);
		I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev,
			    "after complete %s:rd_idx:%d wr_idx:%d tx_cnt:%d\n",
			    __func__, gi2c->gsi_tx.msg_rd_idx,
			    gi2c->gsi_tx.msg_wr_idx,
			    atomic_read(&gi2c->gsi_tx.msg_cnt));
	}
	spin_unlock_irqrestore(&gi2c->multi_tre_lock, flags);
}

static void gi2c_gsi_rx_cb(void *ptr)
{
	struct msm_gpi_dma_async_tx_cb_param *rx_cb = ptr;
	struct geni_i2c_dev *gi2c = rx_cb->userdata;

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

	/* lock: chain bit set */
	lock_t->dword[0] = MSM_GPI_LOCK_TRE_DWORD0;
	lock_t->dword[1] = MSM_GPI_LOCK_TRE_DWORD1;
	lock_t->dword[2] = MSM_GPI_LOCK_TRE_DWORD2;
	/* ieob for le-vm and chain for shared se */
	if (gi2c->is_shared)
		lock_t->dword[3] = MSM_GPI_LOCK_TRE_DWORD3(0, 0, 0, 0, 1);
	else if (gi2c->is_le_vm)
		lock_t->dword[3] = MSM_GPI_LOCK_TRE_DWORD3(0, 0, 0, 1, 0);

	return lock_t;
}

static struct msm_gpi_tre *setup_cfg0_tre(struct geni_i2c_dev *gi2c)
{
	struct geni_i2c_clk_fld *itr;
	struct msm_gpi_tre *cfg0_t = &gi2c->cfg0_t;

	if (gi2c->is_i2c_rtl_based)
		itr = geni_i2c_hub_clk_map + gi2c->clk_fld_idx;
	else
		itr = geni_i2c_clk_map + gi2c->clk_fld_idx;

	/* config0 */
	cfg0_t->dword[0] = MSM_GPI_I2C_CONFIG0_TRE_DWORD0(I2C_PACK_EN,
				itr->t_cycle, itr->t_high, itr->t_low);
	cfg0_t->dword[1] = MSM_GPI_I2C_CONFIG0_TRE_DWORD1(0, 0);
	cfg0_t->dword[2] = MSM_GPI_I2C_CONFIG0_TRE_DWORD2(0,
							itr->clk_div);
	cfg0_t->dword[3] = MSM_GPI_I2C_CONFIG0_TRE_DWORD3(0, 0, 0, 0, 1);

	return cfg0_t;
}

static struct msm_gpi_tre *setup_go_tre(struct geni_i2c_dev *gi2c,
				struct i2c_msg msgs[], int i, int num)
{
	struct msm_gpi_tre *go_t = &gi2c->go_t;
	u8 op = (msgs[i].flags & I2C_M_RD) ? 2 : 1;
	int stretch = (i < (num - 1));

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
			go_t->dword[3] = MSM_GPI_I2C_GO_TRE_DWORD3(1, 0,
							0, 1, 0);
		else
			go_t->dword[3] = MSM_GPI_I2C_GO_TRE_DWORD3(1, 0,
							0, 0, 0);
	} else {
		/* For Tx Go tre: ieob is not set, chain bit is set */
		go_t->dword[2] = MSM_GPI_I2C_GO_TRE_DWORD2(0);
		go_t->dword[3] = MSM_GPI_I2C_GO_TRE_DWORD3(0, 0, 0, 0,
								1);
	}

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

static struct msm_gpi_tre *setup_tx_tre(struct geni_i2c_dev *gi2c,
				struct i2c_msg msgs[], int i, int num, int tx_idx)
{
	struct msm_gpi_tre *tx_t = &gi2c->tx_t;

	tx_t->dword[0] = MSM_GPI_DMA_W_BUFFER_TRE_DWORD0(gi2c->tx_ph[tx_idx]);
	tx_t->dword[1] = MSM_GPI_DMA_W_BUFFER_TRE_DWORD1(gi2c->tx_ph[tx_idx]);
	tx_t->dword[2] = MSM_GPI_DMA_W_BUFFER_TRE_DWORD2(msgs[i].len);
	if (gi2c->is_shared && i == num-1)
		/*
		 * For Tx: unlock tre is send for last transfer
		 * so set chain bit for last transfer DMA tre.
		 */
		tx_t->dword[3] =
		MSM_GPI_DMA_W_BUFFER_TRE_DWORD3(0, 0, 1, 0, 1);
	else
		tx_t->dword[3] =
		MSM_GPI_DMA_W_BUFFER_TRE_DWORD3(0, 0, 1, 0, 0);

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
	return 0;

geni_i2c_err_lock_bus:
	if (gi2c->err) {
		dmaengine_terminate_all(gi2c->tx_c);
		gi2c->cfg_sent = 0;
	}
	return gi2c->err;
}

static void geni_i2c_unlock_bus(struct geni_i2c_dev *gi2c)
{
	struct msm_gpi_tre *unlock_t = NULL;
	int timeout = 0;
	dma_cookie_t tx_cookie;
	bool tx_chan = true;

	if (gi2c->gpi_reset)
		goto geni_i2c_err_unlock_bus;

	unlock_t = setup_unlock_tre(gi2c);
	sg_init_table(gi2c->tx_sg, 1);
	sg_set_buf(&gi2c->tx_sg[0], unlock_t,
					sizeof(gi2c->unlock_t));

	gi2c->tx_desc = geni_i2c_prep_desc(gi2c, gi2c->tx_c, 1, tx_chan);
	if (!gi2c->tx_desc) {
		gi2c->err = -ENOMEM;
		goto geni_i2c_err_unlock_bus;
	}

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
	if (gi2c->gpi_reset || gi2c->err) {
		dmaengine_terminate_all(gi2c->tx_c);
		gi2c->cfg_sent = 0;
		gi2c->err = 0;
		gi2c->gpi_reset = false;
	}
}

/**
 * geni_i2c_gsi_tx_tre_optimization() - Process received TRE's from GSI HW
 * @gi2c: Base address of the gi2c dev structure.
 * @i: i2c message index.
 * @num: number of messages count.
 *
 * This function is used to optimize dma tre's, it keeps always HW busy.
 *
 * Return: Returning timeout value
 */
static int geni_i2c_gsi_tx_tre_optimization(struct geni_i2c_dev *gi2c, int i, int num)
{
	int timeout = 1;

	/**
	 * if it's last message, waiting for all pending tre's
	 * including last submitted tre as well.
	 */
	if ((i == (num - 1)) && num >= MAX_NUM_TRE_MSGS && !gi2c->is_shared) {
		while (gi2c->gsi_tx.msg_rd_idx != gi2c->gsi_tx.msg_wr_idx) {
			atomic_set(&gi2c->msg_wait, 1);
			timeout = wait_for_completion_timeout(&gi2c->xfer,
							      gi2c->xfer_timeout);
			atomic_set(&gi2c->msg_wait, 0);
			if (!timeout) {
				I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev,
					    "%s: last msg xfer timeout\n",
					    __func__);
				break;
			}
		}
	} else if (num < MAX_NUM_TRE_MSGS || gi2c->is_shared ||
		   (atomic_read(&gi2c->gsi_tx.msg_cnt) == MAX_NUM_TRE_MSGS)) {
		/**
		 * For shared SE and num of msgs < MAX_NUM_TRE_MSGS,
		 * go with regular approach
		 */
		atomic_set(&gi2c->msg_wait, 1);
		timeout = wait_for_completion_timeout(&gi2c->xfer,
						      gi2c->xfer_timeout);
		atomic_set(&gi2c->msg_wait, 0);
	}

	/* in b/w if any tre's received, process received tre's */
	gi2c_gsi_tre_process(gi2c, &gi2c->gsi_tx);

	if (num >= MAX_NUM_TRE_MSGS && atomic_read(&gi2c->gsi_tx.msg_cnt) == MAX_NUM_TRE_MSGS) {
		/**
		 * If all TRE's full and irq not received,
		 * will log error and will clear all pending tre buffers
		 */
		geni_i2c_err(gi2c, GSI_TRE_FULL);
		I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev, "timeout:%d\n", timeout);
	}

	I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev,
		    "%s: rd_idx:%d wr_idx:%d tx_cnt:%d timeout:%d\n",
		    __func__, gi2c->gsi_tx.msg_rd_idx,  gi2c->gsi_tx.msg_wr_idx,
		    atomic_read(&gi2c->gsi_tx.msg_cnt), timeout);
	return timeout;
}

static int geni_i2c_gsi_xfer(struct i2c_adapter *adap, struct i2c_msg msgs[],
			     int num)
{
	struct geni_i2c_dev *gi2c = i2c_get_adapdata(adap);
	int i = 0, ret = 0, timeout = 0;
	struct msm_gpi_tre *lock_t = NULL;
	struct msm_gpi_tre *unlock_t = NULL;
	struct msm_gpi_tre *cfg0_t = NULL;
	u8 *rd_dma_buf = NULL;
	u8 op;
	int segs;
	int index = 0;
	dma_cookie_t tx_cookie, rx_cookie;
	struct msm_gpi_tre *go_t = NULL;
	struct msm_gpi_tre *rx_t = NULL;
	struct msm_gpi_tre *tx_t = NULL;
	bool tx_chan = true;

	gi2c->gsi_err = false;
	if (!gi2c->req_chan) {
		ret = geni_i2c_gsi_request_channel(gi2c);
		if (ret)
			return ret;
	}

	if (gi2c->is_shared) {
		lock_t = setup_lock_tre(gi2c);
		unlock_t = setup_unlock_tre(gi2c);
	}

	if (!gi2c->cfg_sent)
		cfg0_t = setup_cfg0_tre(gi2c);

	gi2c->gsi_tx.msg_wr_idx = 0;
	gi2c->gsi_tx.msg_rd_idx = 0;
	gi2c->gsi_tx.dma_rd_idx = 0;
	gi2c->gsi_tx.dma_wr_idx = 0;
	gi2c->gsi_tx.unmap_msg_cnt = 0;
	gi2c->msgs = msgs;
	atomic_set(&gi2c->msg_wait, 0);
	atomic_set(&gi2c->gsi_tx.msg_cnt, 0);

	for (i = 0; i < num; i++) {
		op = (msgs[i].flags & I2C_M_RD) ? 2 : 1;
		segs = 3 - op;
		index = 0;
		/**
		 * sometimes all tre's may process without
		 * waiting for timer thread, so declared
		 * timeout is non-zero value;
		 */
		timeout = 1;

		reinit_completion(&gi2c->xfer);
		gi2c->cur = &msgs[i];
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
		sg_set_buf(&gi2c->tx_sg[index++], go_t,
						  sizeof(gi2c->go_t));

		if (msgs[i].flags & I2C_M_RD) {
			rd_dma_buf = i2c_get_dma_safe_msg_buf(&msgs[i], 1);
			if (!rd_dma_buf) {
				ret = -ENOMEM;
				I2C_LOG_ERR(gi2c->ipcl, true, gi2c->dev,
					    "i2c_get_dma_safe_msg_buf failed :%d\n",
					    ret);
				goto geni_i2c_gsi_xfer_out;
			}
			I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev,
				"msg[%d].len:%d R\n", i, gi2c->cur->len);
			sg_init_table(gi2c->rx_sg, 1);
			ret = geni_se_common_iommu_map_buf(gi2c->wrapper_dev,
							   &gi2c->rx_ph,
							   rd_dma_buf,
							   msgs[i].len,
							   DMA_FROM_DEVICE);
			if (ret) {
				I2C_LOG_ERR(gi2c->ipcl, true, gi2c->dev,
					"geni_se_common_iommu_map_buf for rx failed :%d\n", ret);
				i2c_put_dma_safe_msg_buf(rd_dma_buf, &msgs[i], false);
				goto geni_i2c_gsi_xfer_out;

			} else if (gi2c->dbg_buf_ptr) {
				gi2c->dbg_buf_ptr[i].virt_buf =
							(void *)rd_dma_buf;
				gi2c->dbg_buf_ptr[i].map_buf =
							(void *)&gi2c->rx_ph;
			}

			rx_t = setup_rx_tre(gi2c, msgs, i, num);
			sg_set_buf(gi2c->rx_sg, rx_t,
						 sizeof(gi2c->rx_t));

			gi2c->rx_desc =
			geni_i2c_prep_desc(gi2c, gi2c->rx_c, segs, !tx_chan);
			if (!gi2c->rx_desc) {
				gi2c->err = -ENOMEM;
				goto geni_i2c_err_prep_sg;
			}

			/* Issue RX */
			rx_cookie = dmaengine_submit(gi2c->rx_desc);
			if (dma_submit_error(rx_cookie)) {
				I2C_LOG_ERR(gi2c->ipcl, true, gi2c->dev,
					"%s: dmaengine_submit failed (%d)\n", __func__, rx_cookie);
				gi2c->err = -EINVAL;
				goto geni_i2c_err_prep_sg;
			}

			dma_async_issue_pending(gi2c->rx_c);

			/* submit config/go tre through tx channel */
			if (gi2c->is_shared && (i == (num - 1))) {
				/* Send unlock tre at the end of last transfer */
				sg_set_buf(&gi2c->tx_sg[index++],
					   unlock_t, sizeof(gi2c->unlock_t));
			}

			gi2c->tx_desc = geni_i2c_prep_desc(gi2c, gi2c->tx_c, segs, tx_chan);
			if (!gi2c->tx_desc) {
				gi2c->err = -ENOMEM;
				I2C_LOG_ERR(gi2c->ipcl, true, gi2c->dev,
					    "geni_i2c_prep_desc failed\n");
				goto geni_i2c_err_prep_sg;
			}

			/* Issue TX */
			tx_cookie = dmaengine_submit(gi2c->tx_desc);
			if (dma_submit_error(tx_cookie)) {
				I2C_LOG_ERR(gi2c->ipcl, true, gi2c->dev,
					    "%s: dmaengine_submit failed (%d)\n",
					    __func__, tx_cookie);
				gi2c->err = -EINVAL;
				goto geni_i2c_err_prep_sg;
			}
			dma_async_issue_pending(gi2c->tx_c);
			timeout = wait_for_completion_timeout(&gi2c->xfer,
							gi2c->xfer_timeout);
		} else {
			gi2c->gsi_tx.dma_buf[gi2c->gsi_tx.dma_wr_idx] =
					i2c_get_dma_safe_msg_buf(&msgs[gi2c->gsi_tx.msg_wr_idx], 1);
			if (!gi2c->gsi_tx.dma_buf[gi2c->gsi_tx.dma_wr_idx]) {
				ret = -ENOMEM;
				I2C_LOG_ERR(gi2c->ipcl, true, gi2c->dev,
					    "i2c_get_dma_safe_msg_buf failed :%d\n", ret);
				goto geni_i2c_gsi_xfer_out;
			}
			I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev,
				    "msg[%d].len:%d W cnt:%d\n",
				    i, gi2c->cur->len,
				    atomic_read(&gi2c->gsi_tx.msg_cnt));

			ret = geni_se_common_iommu_map_buf(gi2c->wrapper_dev,
					&gi2c->tx_ph[gi2c->gsi_tx.dma_wr_idx],
					gi2c->gsi_tx.dma_buf[gi2c->gsi_tx.dma_wr_idx],
					msgs[gi2c->gsi_tx.msg_wr_idx].len, DMA_TO_DEVICE);
			if (ret) {
				I2C_LOG_ERR(gi2c->ipcl, true, gi2c->dev,
					"geni_se_common_iommu_map_buf for tx failed :%d\n", ret);
				i2c_put_dma_safe_msg_buf(
						gi2c->gsi_tx.dma_buf[gi2c->gsi_tx.dma_wr_idx],
						&msgs[gi2c->gsi_tx.msg_wr_idx],
						false);
				goto geni_i2c_gsi_xfer_out;

			} else if (gi2c->dbg_buf_ptr) {
				gi2c->dbg_buf_ptr[i].virt_buf =
					(void *)gi2c->gsi_tx.dma_buf[gi2c->gsi_tx.dma_wr_idx];
				gi2c->dbg_buf_ptr[i].map_buf =
					(void *)&gi2c->tx_ph[gi2c->gsi_tx.dma_wr_idx];
			}

			tx_t = setup_tx_tre(gi2c, msgs, i, num, gi2c->gsi_tx.dma_wr_idx);
			sg_set_buf(&gi2c->tx_sg[index++], tx_t, sizeof(gi2c->tx_t));
			if (gi2c->is_shared && (i == (num - 1))) {
				/* Send unlock tre at the end of last transfer */
				sg_set_buf(&gi2c->tx_sg[index++],
					   unlock_t, sizeof(gi2c->unlock_t));
			}

			gi2c->tx_desc = geni_i2c_prep_desc(gi2c, gi2c->tx_c, segs, tx_chan);
			if (!gi2c->tx_desc) {
				gi2c->err = -ENOMEM;
				I2C_LOG_ERR(gi2c->ipcl, true, gi2c->dev,
					    "geni_i2c_prep_desc failed\n");
				goto geni_i2c_err_prep_sg;
			}

			gi2c->gsi_tx.dma_wr_idx = (gi2c->gsi_tx.dma_wr_idx + 1) % MAX_NUM_TRE_MSGS;
			atomic_inc(&gi2c->gsi_tx.msg_cnt);
			gi2c->gsi_tx.msg_wr_idx++;
			I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev,
				    "before dma rd_idx:%d wr_idx:%d tx_cnt:%d timeout:%d\n",
				    gi2c->gsi_tx.msg_rd_idx, gi2c->gsi_tx.msg_wr_idx,
				    atomic_read(&gi2c->gsi_tx.msg_cnt), timeout);

			/* Issue TX */
			tx_cookie = dmaengine_submit(gi2c->tx_desc);
			if (dma_submit_error(tx_cookie)) {
				I2C_LOG_ERR(gi2c->ipcl, true, gi2c->dev,
					    "%s: dmaengine_submit failed (%d)\n",
					    __func__, tx_cookie);
				gi2c->err = -EINVAL;
				goto geni_i2c_err_prep_sg;
			}
			dma_async_issue_pending(gi2c->tx_c);
			I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev,
				    "after dma rd_idx:%d wr_idx:%d tx_cnt:%d timeout:%d\n",
				    gi2c->gsi_tx.msg_rd_idx, gi2c->gsi_tx.msg_wr_idx,
				    atomic_read(&gi2c->gsi_tx.msg_cnt), timeout);
			/* process received tre's */
			gi2c_gsi_tre_process(gi2c, &gi2c->gsi_tx);

			/**
			 * if it's not last message, submitting MAX_NUM_TRE_MSGS
			 * continuously without waiting, in b/w if any one of the
			 * tre is received processing and queuing next tre.
			 */
			if ((i != (num - 1)) &&
			    num >= MAX_NUM_TRE_MSGS && !gi2c->is_shared &&
			    (atomic_read(&gi2c->gsi_tx.msg_cnt) < MAX_NUM_TRE_MSGS))
				continue;

			timeout = geni_i2c_gsi_tx_tre_optimization(gi2c, i, num);
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
			if (!gi2c->is_le_vm) {
				dmaengine_terminate_all(gi2c->tx_c);
				gi2c->cfg_sent = 0;
			} else {
				/* Stop channel in case of error in LE-VM */
				ret = dmaengine_pause(gi2c->tx_c);
				if (ret) {
					gi2c->gpi_reset = true;
					gi2c->err = ret;
					I2C_LOG_ERR(gi2c->ipcl, true, gi2c->dev,
						"Channel cancel failed\n");
					goto geni_i2c_gsi_xfer_out;
				}
			}
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
			/**
			 * for write operation, success case handling below logic in gi2c_gsi_tx_cb,
			 * failure cases, clearing all submitted tre's
			 * gi2c->gsi_tx.msg_cnt will be decremented in gi2c_gsi_tre_process.
			 */
			while (gi2c->gsi_tx.unmap_msg_cnt != gi2c->gsi_tx.msg_wr_idx) {
				gi2c_gsi_tre_process(gi2c, &gi2c->gsi_tx);
				gi2c->gsi_tx.msg_rd_idx++;
			}
		}
		if (gi2c->err)
			goto geni_i2c_gsi_xfer_out;
	}

geni_i2c_gsi_xfer_out:
	if (!ret && gi2c->err)
		ret = gi2c->err;
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

	for (i = 0; i < num; i++) {
		int stretch = (i < (num - 1));
		u32 m_param = 0;
		u32 m_cmd = 0;
		u8 *dma_buf = NULL;
		dma_addr_t tx_dma = 0;
		dma_addr_t rx_dma = 0;
		enum geni_se_xfer_mode mode = GENI_SE_FIFO;

		reinit_completion(&gi2c->xfer);

		m_param |= (stretch ? STOP_STRETCH : 0);
		m_param |= ((msgs[i].addr & 0x7F) << SLV_ADDR_SHFT);

		gi2c->cur = &msgs[i];
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
				goto exit;
			}
		}

		I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev,
			    "%s: stretch:%d, m_param:0x%x\n",
			    __func__, stretch, m_param);

		if (msgs[i].flags & I2C_M_RD) {
			I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev,
				"msgs[%d].len:%d R\n", i, msgs[i].len);
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
				"msgs[%d].len:%d W\n", i, msgs[i].len);
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
		timeout = wait_for_completion_timeout(&gi2c->xfer,
						gi2c->xfer_timeout);
		if (!timeout) {
			u32 geni_ios = 0;

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
					goto exit;
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
					goto exit;
				}

				/* EBUSY set by ARB_LOST error condition */
				if (gi2c->err == -EBUSY) {
					I2C_LOG_DBG(gi2c->ipcl, true, gi2c->dev,
						    "%s:run reg68 war\n", __func__);
					do_reg68_war_for_rtl_se(gi2c);
				}
			}
			reinit_completion(&gi2c->xfer);
			geni_se_cancel_m_cmd(&gi2c->i2c_rsc);
			timeout = wait_for_completion_timeout(&gi2c->xfer, HZ);
			if (!timeout) {
				I2C_LOG_ERR(gi2c->ipcl, true, gi2c->dev,
					"Abort\n");
				geni_se_abort_m_cmd(&gi2c->i2c_rsc);
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
				wait_for_completion_timeout(&gi2c->xfer, HZ);
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
			if (geni_i2c_bus_recovery(gi2c))
				GENI_SE_ERR(gi2c->ipcl, true, gi2c->dev,
					    "%s:Bus Recovery failed\n", __func__);
			break;
		}
	}

exit:
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

	gi2c->err = 0;
	atomic_set(&gi2c->is_xfer_in_progress, 1);

	/* Client to respect system suspend */
	if (!pm_runtime_enabled(gi2c->dev)) {
		I2C_LOG_ERR(gi2c->ipcl, false, gi2c->dev,
			"%s: System suspended\n", __func__);
		atomic_set(&gi2c->is_xfer_in_progress, 0);
		return -EACCES;
	}

	/* Do Not vote if is_le_vm: LA votes and pm_ctrl_client: client votes */
	if (!gi2c->is_le_vm && !gi2c->pm_ctrl_client) {
		ret = pm_runtime_get_sync(gi2c->dev);
		if (ret < 0) {
			I2C_LOG_ERR(gi2c->ipcl, true, gi2c->dev,
					"error turning SE resources:%d\n", ret);
			pm_runtime_put_noidle(gi2c->dev);
			/* Set device in suspended since resume failed */
			pm_runtime_set_suspended(gi2c->dev);
			atomic_set(&gi2c->is_xfer_in_progress, 0);
			return ret;
		}
	}

	// WAR : Complete previous pending cancel cmd
	if (gi2c->prev_cancel_pending) {
		ret = do_pending_cancel(gi2c);
		if (ret) {
			pm_runtime_mark_last_busy(gi2c->dev);
			pm_runtime_put_autosuspend(gi2c->dev);
			atomic_set(&gi2c->is_xfer_in_progress, 0);
			return ret; //Don't perform xfer is cancel failed
		}
	}

	geni_ios = geni_read_reg(gi2c->base, SE_GENI_IOS);
	if ((geni_ios & 0x3) != 0x3) { //SCL:b'1, SDA:b'0
		I2C_LOG_ERR(gi2c->ipcl, false, gi2c->dev,
			    "IO lines in bad state, Power the slave\n");
		pm_runtime_mark_last_busy(gi2c->dev);
		pm_runtime_put_autosuspend(gi2c->dev);
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
		ret = geni_i2c_gsi_xfer(adap, msgs, num);
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
	I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev,
			"i2c txn ret:%d freq=%dHz\n", ret, gi2c->clk_freq_out);
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

static int geni_i2c_probe(struct platform_device *pdev)
{
	struct geni_i2c_dev *gi2c;
	struct resource *res;
	int ret;
	struct device *dev = &pdev->dev;

	gi2c = devm_kzalloc(&pdev->dev, sizeof(*gi2c), GFP_KERNEL);
	if (!gi2c)
		return -ENOMEM;

	if (arr_idx < MAX_SE)
		/* Debug purpose */
		gi2c_dev_dbg[arr_idx++] = gi2c;

	gi2c->dev = dev;

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

	gi2c->i2c_rsc.dev = dev;
	gi2c->i2c_rsc.wrapper = dev_get_drvdata(dev->parent);
	gi2c->i2c_rsc.base = gi2c->base;
	gi2c->wrapper_dev = dev->parent;

	if (!gi2c->i2c_rsc.wrapper) {
		dev_err(&pdev->dev, "SE Wrapper is NULL, deferring probe\n");
		return -EPROBE_DEFER;
	}

	if (of_property_read_u32(pdev->dev.of_node, "qcom,clk-freq-out",
				&gi2c->clk_freq_out))
		gi2c->clk_freq_out = KHz(400);

	dev_info(&pdev->dev, "Bus frequency is set to %dHz.\n",
						gi2c->clk_freq_out);

	ret = geni_i2c_clk_map_idx(gi2c);
	if (ret) {
		dev_err(gi2c->dev, "Invalid clk frequency %d KHz: %d\n",
				gi2c->clk_freq_out, ret);
		return ret;
	}

	/*
	 * For LE, clocks, gpio and icb voting will be provided by
	 * LA. The I2C operates in GSI mode only for LE usecase,
	 * se irq not required. Below properties will not be present
	 * in I2C LE dt.
	 */
	if (!gi2c->is_le_vm) {
		gi2c->i2c_rsc.clk = devm_clk_get(&pdev->dev, "se-clk");
		if (IS_ERR(gi2c->i2c_rsc.clk)) {
			ret = PTR_ERR(gi2c->i2c_rsc.clk);
			dev_err(&pdev->dev, "Err getting SE Core clk %d\n",
				ret);
			return ret;
		}

		gi2c->m_ahb_clk = devm_clk_get(dev->parent, "m-ahb");
		if (IS_ERR(gi2c->m_ahb_clk)) {
			ret = PTR_ERR(gi2c->m_ahb_clk);
			dev_err(&pdev->dev, "Err getting M AHB clk %d\n", ret);
			return ret;
		}

		gi2c->s_ahb_clk = devm_clk_get(dev->parent, "s-ahb");
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
				ret = geni_se_common_resources_init(&gi2c->i2c_rsc,
						I2C_CORE2X_VOTE, GENI_DEFAULT_BW,
						(DEFAULT_SE_CLK * DEFAULT_BUS_WIDTH));
			else
				ret = geni_se_common_resources_init(&gi2c->i2c_rsc,
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
	}

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

	spin_lock_init(&gi2c->multi_tre_lock);

	gi2c->adap.algo = &geni_i2c_algo;
	init_completion(&gi2c->xfer);
	platform_set_drvdata(pdev, gi2c);
	i2c_set_adapdata(&gi2c->adap, gi2c);
	gi2c->adap.dev.parent = gi2c->dev;
	gi2c->adap.dev.of_node = pdev->dev.of_node;

	strscpy(gi2c->adap.name, "Geni-I2C", sizeof(gi2c->adap.name));

	pm_runtime_set_suspended(gi2c->dev);
	pm_runtime_set_autosuspend_delay(gi2c->dev, I2C_AUTO_SUSPEND_DELAY);
	pm_runtime_use_autosuspend(gi2c->dev);
	pm_runtime_enable(gi2c->dev);
	ret = i2c_add_adapter(&gi2c->adap);
	if (ret) {
		dev_err(gi2c->dev, "Add adapter failed, ret=%d\n", ret);
		return ret;
	}

	atomic_set(&gi2c->is_xfer_in_progress, 0);
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

	if (gi2c->ipcl)
		ipc_log_context_destroy(gi2c->ipcl);
	return 0;
}

static int geni_i2c_resume_early(struct device *device)
{
	struct geni_i2c_dev *gi2c = dev_get_drvdata(device);

	I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev, "%s ret=%d\n", __func__, true);
	return 0;
}

static int geni_i2c_hib_resume_noirq(struct device *device)
{
	struct geni_i2c_dev *gi2c = dev_get_drvdata(device);

	I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev, "%s\n", __func__);
	gi2c->se_mode = UNINITIALIZED;
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

	if (gi2c->tx_c) {
		if (is_suspend)
			tx_ret = dmaengine_pause(gi2c->tx_c);
		else
			tx_ret = dmaengine_resume(gi2c->tx_c);

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
	return 0;
}

static int geni_i2c_runtime_resume(struct device *dev)
{
	int ret = 0;
	struct geni_i2c_dev *gi2c = dev_get_drvdata(dev);

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

	return 0;
}

static int geni_i2c_suspend_late(struct device *device)
{
	struct geni_i2c_dev *gi2c = dev_get_drvdata(device);
	int ret;

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
	i2c_unlock_bus(&gi2c->adap, I2C_LOCK_SEGMENT);
	I2C_LOG_DBG(gi2c->ipcl, false, gi2c->dev, "%s ret=%d\n", __func__, ret);
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

