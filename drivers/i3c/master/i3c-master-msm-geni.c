// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2019-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/i3c/master.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/pm_runtime.h>
#include <linux/soc/qcom/geni-se.h>
#include <linux/qcom-geni-se-common.h>
#include <linux/pinctrl/consumer.h>
#include <linux/ipc_logging.h>
#include <linux/msm_gpi.h>
#include <linux/dmaengine.h>
#include <linux/pinctrl/qcom-pinctrl.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/pm_wakeup.h>
#include <linux/workqueue.h>
#include <linux/sched/clock.h>

#define SE_I3C_SCL_HIGH			0x268
#define SE_I3C_TX_TRANS_LEN		0x26C
#define SE_I3C_RX_TRANS_LEN		0x270
#define SE_I3C_DELAY_COUNTER		0x274
#define SE_I2C_SCL_COUNTERS		0x278
#define SE_I3C_SCL_CYCLE		0x27C
#define SE_GENI_HW_IRQ_EN		0x920
#define SE_GENI_HW_IRQ_IGNORE_ON_ACTIVE	0x924
#define SE_GENI_HW_IRQ_CMD_PARAM_0	0x930
/* IBI_C registers */
#define IBI_GEN_CONFIG			0x0000
#define IBI_SCL_OD_TYPE			0x0004
#define IBI_SCL_PP_TIMING_CONFIG	0x0008
#define IBI_GPII_IBI_EN			0x000c
#define IBI_GEN_IRQ_STATUS		0x0010
#define IBI_GEN_IRQ_EN			0x0014
#define IBI_GEN_IRQ_CLR			0x0018
#define IBI_HW_PARAM			0x001c
#define IBI_HW_VERSION			0x0020
#define IBI_RX_DATA_DELAY		0x0024
#define IBI_UNEXPECT_IBI_INFO		0x0028
#define IBI_LEGACY_MODE			0x002c
#define IBI_SW_RESET			0x0030
#define IBI_TEST_BUS_SEL		0x0100
#define IBI_TEST_BUS_EN			0x0104
#define IBI_TEST_BUS_REG		0x0108
#define IBI_HW_EVENTS_MUX_CFG		0x010c
#define IBI_CHAR_CFG			0x0180
#define IBI_CHAR_DATA			0x0184
#define IBI_CHAR_OE			0x0188
#define IBI_CMD(n)			(0x1000 + (0x1000*n))
#define IBI_IRQ_STATUS(n)		(0x1004 + (0x1000*n))
#define IBI_IRQ_EN(n)			(0x1008 + (0x1000*n))
#define IBI_IRQ_CLR(n)			(0x100C + (0x1000*n))
#define IBI_RCVD_IBI_STATUS(n)		(0x1010 + (0x1000*n))
#define IBI_RCVD_IBI_CLR(n)		(0x1014 + (0x1000*n))
#define IBI_ALLOCATED_ENTRIES_GPII(n)	(0x1018 + (0x1000*n))
#define IBI_CONFIG_ENTRY(n, k)		(0x1800 + (0x1000*n) + (0x80*k))
#define IBI_RCVD_IBI_INFO_ENTRY(n, k)	(0x1804 + (0x1000*n) + (0x80*k))
#define IBI_RCVD_IBI_DATA_ENTRY_REG0(n, k)	(0x1808 + (0x1000*n) + (0x80*k))
#define IBI_RCVD_IBI_DATA_ENTRY_REG1(n, k)	(0x180C + (0x1000*n) + (0x80*k))
#define IBI_RCVD_IBI_DATA_ENTRY_REG2(n, k)	(0x1810 + (0x1000*n) + (0x80*k))
#define IBI_RCVD_IBI_DATA_ENTRY_REG3(n, k)	(0x1814 + (0x1000*n) + (0x80*k))
#define IBI_RCVD_IBI_DATA_ENTRY_REG4(n, k)	(0x1818 + (0x1000*n) + (0x80*k))
#define IBI_RCVD_IBI_DATA_ENTRY_REG5(n, k)	(0x181C + (0x1000*n) + (0x80*k))
#define IBI_RCVD_IBI_DATA_ENTRY_REG6(n, k)	(0x1820 + (0x1000*n) + (0x80*k))
#define IBI_RCVD_IBI_DATA_ENTRY_REG7(n, k)	(0x1828 + (0x1000*n) + (0x80*k))
#define IBI_RCVD_IBI_TS_LSB_ENTRY(n, k)	(0x1828 + (0x1000*n) + (0x80*k))
#define IBI_RCVD_IBI_TS_MSB_ENTRY(n, k)	(0x182C + (0x1000*n) + (0x80*k))

/* SE_GENI_M_CLK_CFG field shifts */
#define CLK_DEV_VALUE_SHFT	4
#define SER_CLK_EN_SHFT		0

/* SE_GENI_HW_IRQ_CMD_PARAM_0 field shifts */
#define M_IBI_IRQ_PARAM_7E_SHFT		0
#define M_IBI_IRQ_PARAM_STOP_STALL_SHFT	1

#define GEN_I3C_IBI_CTRL    (BIT(7))

/* SE_I2C_SCL_COUNTERS field shifts */
#define I2C_SCL_HIGH_COUNTER_SHFT	20
#define I2C_SCL_LOW_COUNTER_SHFT	10

#define	SE_I3C_ERR  (M_CMD_OVERRUN_EN | M_ILLEGAL_CMD_EN | M_CMD_FAILURE_EN |\
	M_CMD_ABORT_EN | M_GP_IRQ_0_EN | M_GP_IRQ_1_EN | M_GP_IRQ_2_EN | \
	M_GP_IRQ_3_EN | M_GP_IRQ_4_EN)

/* M_CMD OP codes for I2C/I3C */
#define I3C_READ_IBI_HW			0
#define I2C_WRITE			1
#define I2C_READ			2
#define I2C_WRITE_READ			3
#define I2C_ADDR_ONLY			4
#define I3C_INBAND_RESET		5
#define I2C_BUS_CLEAR			6
#define I2C_STOP_ON_BUS			7
#define I3C_HDR_DDR_EXIT		8
#define I3C_PRIVATE_WRITE		9
#define I3C_PRIVATE_READ		10
#define I3C_HDR_DDR_WRITE		11
#define I3C_HDR_DDR_READ		12
#define I3C_DIRECT_CCC_ADDR_ONLY	13
#define I3C_BCAST_CCC_ADDR_ONLY		14
#define I3C_READ_IBI			15
#define I3C_BCAST_CCC_WRITE		16
#define I3C_DIRECT_CCC_WRITE		17
#define I3C_DIRECT_CCC_READ		18
/* M_CMD params for I3C */
#define PRE_CMD_DELAY		BIT(0)
#define TIMESTAMP_BEFORE	BIT(1)
#define STOP_STRETCH		BIT(2)
#define TIMESTAMP_AFTER		BIT(3)
#define POST_COMMAND_DELAY	BIT(4)
#define IGNORE_ADD_NACK		BIT(6)
#define READ_FINISHED_WITH_ACK	BIT(7)
#define CONTINUOUS_MODE_DAA	BIT(8)
#define SLV_ADDR_MSK		GENMASK(15, 9)
#define SLV_ADDR_SHFT		9
#define CCC_HDR_CMD_MSK		GENMASK(23, 16)
#define CCC_HDR_CMD_SHFT	16
#define IBI_NACK_TBL_CTRL	BIT(24)
#define USE_7E			BIT(25)
#define BYPASS_ADDR_PHASE	BIT(26)

/* IBI_HW_PARAM fields */
#define I3C_IBI_NUM_GPII_MSK	(GENMASK(11, 8))
#define I3C_IBI_NUM_GPII_SHFT	(8)
#define I3C_IBI_TABLE_DEPTH_MSK	(GENMASK(4, 0))

/* IBI_IRQ_STATUS(n) fields */
#define COMMAND_DONE			BIT(0)
#define CFG_TABLE_FULL			BIT(2)
#define IBI_RECEIVED			BIT(8)
#define ADDR_ASSOCIATED_W_OTHER_GPII	BIT(21)

/* IBI_GEN_IRQ_EN fields */
#define ENABLE_CHANGE_IRQ_EN		BIT(0)
#define UNEXPECT_IBI_ADDR_IRQ_EN	BIT(1)
#define HOT_JOIN_IRQ_EN			BIT(2)
#define SW_RESET_DONE_EN		BIT(3)
#define BUS_ERROR_EN			BIT(4)

/* IBI_IRQ_EN fields */
#define COMMAND_DONE_IRQ_EN		BIT(0)
#define INVALID_I3C_SLAVE_ADDR_IRQ_EN	BIT(1)
#define CFG_TABLE_FULL_IRQ_EN		BIT(2)
#define CFG_FAIL_IRQ_EN			BIT(3)
#define CFG_W_IBI_DIS_IRQ_EN		BIT(4)
#define IBI_RECEIVED_IRQ_EN		BIT(8)
#define CFG_FAIL_ZERO_NUM_MDB_EN	BIT(16)
#define CFG_FAIL_MASK_EN_DIFF_EN	BIT(17)
#define CFG_FAIL_NUM_MDB_DIFF_EN	BIT(18)
#define CFG_FAIL_NACK_DIFF_EN		BIT(19)
#define CFG_FAIL_STALL_DIFF_EN		BIT(20)
#define ADDR_ASSOCIATED_W_OTHER_GPII_EN	BIT(21)

/* Enable bits for GPIIn,  n:[0-11] */
#define GPIIn_IBI_EN(n)	      BIT(n)

/* IBI_CMD fields */
#define IBI_CMD_OPCODE        BIT(0)
#define I3C_SLAVE_RW          BIT(12)
#define STALL                 BIT(21)
#define I3C_SLAVE_ADDR_SHIFT  5
#define I3C_SLAVE_MASK        0x7f
#define NUM_OF_MDB_SHIFT      13
#define IBI_NUM_OF_MDB_MSK    GENMASK(18, 13)

#define I3C_PACK_EN	(BIT(0) | BIT(1))
/* GSI cb error fields */
#define GP_IRQ0	0
#define GP_IRQ1	1
#define GP_IRQ2	2
#define GP_IRQ3	3
#define GP_IRQ4	4
#define GP_IRQ5	5

/* IBI_GEN_CONFIG fields */
#define IBI_C_ENABLE	BIT(0)

/* IBI_CONFIG_ENTRY fields */
#define IBI_VALID	BIT(0)

#define SE_I3C_IBI_ERR  (INVALID_I3C_SLAVE_ADDR_IRQ_EN |\
			CFG_TABLE_FULL_IRQ_EN | CFG_FAIL_IRQ_EN |\
			CFG_W_IBI_DIS_IRQ_EN | CFG_FAIL_ZERO_NUM_MDB_EN |\
			CFG_FAIL_MASK_EN_DIFF_EN | CFG_FAIL_NUM_MDB_DIFF_EN |\
			CFG_FAIL_NACK_DIFF_EN | CFG_FAIL_STALL_DIFF_EN |\
			ADDR_ASSOCIATED_W_OTHER_GPII_EN)

#define DM_I3C_CB_ERR   ((BIT(GP_IRQ0) | BIT(GP_IRQ1) | BIT(GP_IRQ2) | \
			  BIT(GP_IRQ3) | BIT(GP_IRQ4) | BIT(GP_IRQ5)) << 5)

#define I3C_AUTO_SUSPEND_DELAY	250
#define KHZ(freq)		(1000 * freq)
#define I3C_DDR_VOTE_FACTOR		2
#define PACKING_BYTES_PW	4
#define XFER_TIMEOUT		250
#define DFS_INDEX_MAX		7

#define I3C_DDR_READ_CMD BIT(7)
#define I3C_ADDR_MASK	0x7f
#define I3C_MAX_GPII_NUM 12
#define TLMM_I3C_MODE	0x24
#define IBI_SW_RESET_MIN_SLEEP 1000
#define IBI_SW_RESET_MAX_SLEEP 2000

#define MAX_I3C_SE		2

/* For multi descriptor, gsi irq will generate for every 64 tre's */
#define NUM_I3C_TRE_MSGS_PER_INTR (64)

enum geni_i3c_err_code {
	RD_TERM,
	NACK,
	CRC_ERR,
	BUS_PROTO,
	NACK_7E,
	NACK_IBI,
	GENI_OVERRUN,
	GENI_ILLEGAL_CMD,
	GENI_ABORT_DONE,
	GENI_TIMEOUT,
};

enum i3c_trans_dir {
	WRITE_TRANSACTION = 0,
	READ_TRANSACTION = 1
};

enum i3c_bus_phase {
	OPEN_DRAIN_MODE  = 0,
	PUSH_PULL_MODE   = 1
};

struct rcvd_ibi_data {
	union {
		struct {
			u32 slave_add   : 7;
			u32 rw          : 1;
			u32 num_bytes   : 3;
			u32 resvd1      : 1;
			u32 nack        : 1;
			u32 resvd2      : 18;
			u32 valid       : 1;
		} fields;
		u32 info;
	} info;
	u32 ts;
	u32 payload;
};

struct geni_i3c_ver_info {
	int hw_major_ver;
	int hw_minor_ver;
	int hw_step_ver;
	int m_fw_ver;
	int s_fw_ver;
};

struct geni_ibi {
	bool hw_support;
	bool is_init;
	void __iomem *ibi_base;
	unsigned int num_slots;
	unsigned int num_gpi;
	struct i3c_dev_desc **slots;
	spinlock_t lock;
	int mngr_irq;
	struct completion done;
	int gpii_irq[I3C_MAX_GPII_NUM];
	int err;
	u32 ctrl_id;
	struct rcvd_ibi_data data;
	bool ibic_naon;
	bool naon_clk_en;
	struct clk *core_clk;
	struct clk *ahb_clk;
	struct clk *src_clk;
};

struct msm_geni_i3c_rsc {
	struct device *wrapper;
	struct clk *se_clk;
	struct clk *m_ahb_clk;
	struct clk *s_ahb_clk;
	struct pinctrl *i3c_pinctrl;
	struct pinctrl_state *i3c_gpio_active;
	struct pinctrl_state *i3c_gpio_sleep;
	enum geni_se_protocol_type proto;
};

struct geni_i3c_dev {
	struct geni_se se;
	unsigned int tx_wm;
	int irq;
	int err;
	u32 se_mode;
	struct i3c_master_controller ctrlr;
	void *ipcl;
	struct completion done;
	struct mutex lock;
	struct gsi_common gsi;
	dma_addr_t rx_phy;
	bool gsi_err;
	bool cfg_sent; /* gsi config sent flag */
	bool disable_free_run_clks;
	spinlock_t spinlock;
	u32 clk_src_freq;
	u32 dfs_idx;
	u32 prev_dfs_idx;
	u8 *cur_buf;
	enum i3c_trans_dir cur_rnw;
	int cur_len;
	int cur_idx;
	unsigned long newaddrslots[(I3C_ADDR_MASK + 1) / BITS_PER_LONG];
	const struct geni_i3c_clk_fld *clk_fld;
	const struct geni_i3c_clk_fld *clk_od_fld;
	struct geni_ibi ibi;
	struct workqueue_struct *hj_wq;
	struct work_struct hj_wd;
	struct wakeup_source *hj_wl;
	struct pinctrl_state *i3c_gpio_disable;
	struct geni_i3c_ver_info ver_info;
	struct msm_geni_i3c_rsc i3c_rsc;
	struct device *wrapper_dev;
};

struct geni_i3c_i2c_dev_data {
	u16 id;
	s16 ibi;
	struct i3c_generic_ibi_pool *ibi_pool;
};

struct geni_i3c_xfer_params {
	enum geni_se_xfer_mode mode;
	u32 m_cmd;
	u32 m_param;
	bool gsi_bei;
	int tx_idx;
};

struct geni_i3c_err_log {
	int err;
	const char *msg;
};

static struct geni_i3c_err_log gi3c_log[] = {
	[RD_TERM] = { -EINVAL, "I3C slave early read termination" },
	[NACK] = { -ENOTCONN, "NACK: slave unresponsive, check power/reset" },
	[CRC_ERR] = { -EINVAL, "CRC or parity error" },
	[BUS_PROTO] = { -EPROTO, "Bus proto err, noisy/unexpected start/stop" },
	[NACK_7E] = { -EBUSY, "NACK on 7E, unexpected protocol error" },
	[NACK_IBI] = { -EINVAL, "NACK on IBI" },
	[GENI_OVERRUN] = { -EIO, "Cmd overrun, check GENI cmd-state machine" },
	[GENI_ILLEGAL_CMD] = { -EILSEQ,
				"Illegal cmd, check GENI cmd-state machine" },
	[GENI_ABORT_DONE] = { -ETIMEDOUT, "Abort after timeout successful" },
	[GENI_TIMEOUT] = { -ETIMEDOUT, "I3C transaction timed out" },
};

struct geni_i3c_clk_fld {
	u32 clk_freq_out;
	u32 clk_src_freq;
	u8  clk_div;
	u8  i2c_t_high_cnt;
	u8  i2c_t_low_cnt;
	u8  i3c_t_high_cnt;
	u8  i3c_t_cycle_cnt;
	u32 i2c_t_cycle_cnt;
};

static int geni_i3c_gsi_stop_on_bus(struct geni_i3c_dev *gi3c);
static void geni_i3c_enable_ibi_ctrl(struct geni_i3c_dev *gi3c, bool enable);
static void geni_i3c_enable_ibi_irq(struct geni_i3c_dev *gi3c, bool enable);
static int geni_i3c_enable_naon_ibi_clks(struct geni_i3c_dev *gi3c, bool enable);

static struct geni_i3c_dev *i3c_geni_dev[MAX_I3C_SE];
static int i3c_nos;

static struct geni_i3c_dev*
to_geni_i3c_master(struct i3c_master_controller *master)
{
	return container_of(master, struct geni_i3c_dev, ctrlr);
}

/*
 * Hardware uses the underlying formula to calculate time periods of
 * SCL clock cycle. Firmware uses some additional cycles excluded from the
 * below formula and it is confirmed that the time periods are within
 * specification limits.
 *
 * time of high period of I2C SCL:
 *         i2c_t_high = (i2c_t_high_cnt * clk_div) / source_clock
 * time of low period of I2C SCL:
 *         i2c_t_low = (i2c_t_low_cnt * clk_div) / source_clock
 * time of full period of I2C SCL:
 *         i2c_t_cycle = (i2c_t_cycle_cnt * clk_div) / source_clock
 * time of high period of I3C SCL:
 *         i3c_t_high = (i3c_t_high_cnt * clk_div) / source_clock
 * time of full period of I3C SCL:
 *         i3c_t_cycle = (i3c_t_cycle_cnt * clk_div) / source_clock
 * clk_freq_out = t / t_cycle
 */
static const struct geni_i3c_clk_fld geni_i3c_clk_map[] = {
/* op-freq, src-freq,  div,  i2c_high,  i2c_low, i3c_high, i3c_cyc i2c_cyc */
	{ KHZ(100),    19200,  1, 76, 90,  7,  8,  192},
	{ KHZ(400),    19200,  1, 12, 24,  7,  8,  48},
	{ KHZ(1000),   19200,  1,  4,  9,  7,  8,  19},
	{ KHZ(1920),   19200,  1,  4,  9,  7,  8,  19},
	{ KHZ(3500),   19200,  1, 72, 168, 3, 4,  300},
	{ KHZ(370),   100000, 20,  4,  7,  8, 14,  14},
	{ KHZ(12500), 100000,  1, 72, 168, 6,  7, 300},
};

#define GENI_SE_I3C_ERR(log_ctx, print, dev, x...) do { \
ipc_log_string(log_ctx, x); \
if (print) { \
	if (dev) \
		dev_err((dev), x); \
	else \
		pr_err(x); \
	} \
} while (0)

#define GENI_SE_I3C_DBG(log_ctx, print, dev, x...) do { \
ipc_log_string(log_ctx, x); \
if (print) { \
	if (dev) \
		dev_dbg((dev), x); \
	else \
		pr_debug(x); \
	} \
} while (0)

#define I3C_LOG_DBG(log_ctx, print, dev, x...) do { \
GENI_SE_I3C_DBG(log_ctx, print, dev, x);\
if (dev) \
	i3c_trace_log(dev, x); \
} while (0)

#define I3C_LOG_ERR(log_ctx, print, dev, x...) do { \
GENI_SE_I3C_ERR(log_ctx, print, dev, x);\
if (dev) \
	i3c_trace_log(dev, x); \
} while (0)

#define CREATE_TRACE_POINTS
#include "i3c-qup-trace.h"

/* FTRACE Logging */
void i3c_trace_log(struct device *dev, const char *fmt, ...)
{
	struct va_format vaf = {
		.fmt = fmt,
	};

	va_list args;

	va_start(args, fmt);
	vaf.va = &args;
	trace_i3c_log_info(dev_name(dev), &vaf);
	va_end(args);
}

/**
 * geni_i3c_se_dump_dbg_regs() - Print relevant registers that capture most
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
void geni_i3c_se_dump_dbg_regs(struct geni_se *se, void __iomem *base,
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
	u32 geni_dma_tx_ptr_l = 0;
	u32 geni_dma_tx_ptr_h = 0;

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
	geni_dma_tx_ptr_l = geni_read_reg(base, SE_DMA_TX_PTR_L);
	geni_dma_tx_ptr_h = geni_read_reg(base, SE_DMA_TX_PTR_H);

	I3C_LOG_DBG(ipc, false, se->dev,
	"%s: m_cmd0:0x%x, m_irq_status:0x%x, geni_status:0x%x, geni_ios:0x%x\n",
	__func__, m_cmd0, m_irq_status, geni_status, geni_ios);
	I3C_LOG_DBG(ipc, false, se->dev,
	"dma_rx_irq:0x%x, dma_tx_irq:0x%x, rx_fifo_sts:0x%x, tx_fifo_sts:0x%x\n",
	dma_rx_irq, dma_tx_irq, rx_fifo_status, tx_fifo_status);
	I3C_LOG_DBG(ipc, false, se->dev,
	"se_dma_dbg:0x%x, m_cmd_ctrl:0x%x, dma_rxlen:0x%x, dma_rxlen_in:0x%x\n",
	se_dma_dbg, m_cmd_ctrl, se_dma_rx_len, se_dma_rx_len_in);
	I3C_LOG_DBG(ipc, false, se->dev,
	"dma_txlen:0x%x, dma_txlen_in:0x%x s_irq_status:0x%x\n",
	se_dma_tx_len, se_dma_tx_len_in, s_irq_status);
	I3C_LOG_DBG(ipc, false, se->dev,
	"dma_txirq_en:0x%x, dma_rxirq_en:0x%x geni_m_irq_en:0x%x geni_s_irq_en:0x%x\n",
	geni_dma_tx_irq_en, geni_dma_rx_irq_en, geni_m_irq_en,
	geni_s_irq_en);
	I3C_LOG_DBG(ipc, false, se->dev,
	"geni_dma_tx_ptr_l:0x%x, geni_dma_tx_ptr_h:0x%x\n",
	geni_dma_tx_ptr_l, geni_dma_tx_ptr_h);
}

/*
 * geni_i3c_err() - updates i3c global gsi error
 *
 * @gi3c: i3c master device handle
 * @err: error index
 *
 * Return: None
 */
static void geni_i3c_err(struct geni_i3c_dev *gi3c, int err)
{
	if (gi3c->cur_rnw == WRITE_TRANSACTION)
		I3C_LOG_DBG(gi3c->ipcl, false, gi3c->se.dev,
			    "%s:Error: Write, len:%d\n", __func__, gi3c->cur_len);
	else
		I3C_LOG_DBG(gi3c->ipcl, false, gi3c->se.dev,
			    "%s:Error: Read, len:%d\n", __func__, gi3c->cur_len);

	I3C_LOG_DBG(gi3c->ipcl, false, gi3c->se.dev, "%s\n", gi3c_log[err].msg);
	gi3c->err = gi3c_log[err].err;

	geni_i3c_se_dump_dbg_regs(&gi3c->se, gi3c->se.base, gi3c->ipcl);
}

/*
 * geni_i3c_handle_err() - updates i3c gsi errors based on gsi callback status
 *
 * @gi3c: i3c master device handle
 * @status: status from gsi callback
 *
 * Return: None
 */
static void geni_i3c_handle_err(struct geni_i3c_dev *gi3c, u32 status)
{
	if (status & M_GP_IRQ_0_EN)
		geni_i3c_err(gi3c, RD_TERM);
	if (status & M_GP_IRQ_1_EN)
		geni_i3c_err(gi3c, NACK);
	if (status & M_GP_IRQ_2_EN)
		geni_i3c_err(gi3c, CRC_ERR);
	if (status & M_GP_IRQ_3_EN)
		geni_i3c_err(gi3c, BUS_PROTO);
	if (status & M_GP_IRQ_4_EN)
		geni_i3c_err(gi3c, NACK_7E);
	if (status & M_GP_IRQ_5_EN)
		geni_i3c_err(gi3c, NACK_IBI);
	if (status & M_CMD_OVERRUN_EN)
		geni_i3c_err(gi3c, GENI_OVERRUN);
	if (status & M_ILLEGAL_CMD_EN)
		geni_i3c_err(gi3c, GENI_ILLEGAL_CMD);
	if (status & M_CMD_ABORT_EN)
		geni_i3c_err(gi3c, GENI_ABORT_DONE);
}

/*
 * gi3c_gsi_cb_err() - updates i3c gsi errors from callback function
 *
 * @cb: callback param
 * @xfer: transfer direction string Tx/Rx
 *
 * Return: None
 */
static void gi3c_gsi_cb_err(struct msm_gpi_dma_async_tx_cb_param *cb, char *xfer)
{
	struct gsi_common *gsi = cb->userdata;
	struct geni_i3c_dev *gi3c = (struct geni_i3c_dev *)gsi->dev_node;

	if (cb->status & DM_I3C_CB_ERR) {
		I3C_LOG_DBG(gi3c->ipcl, false, gi3c->se.dev,
			    "%s TCE Unexpected Err, stat:0x%x\n", xfer, cb->status);
		if (cb->status & (BIT(GP_IRQ0) << 5))
			geni_i3c_err(gi3c, RD_TERM);
		if (cb->status & (BIT(GP_IRQ1) << 5))
			geni_i3c_err(gi3c, NACK);
		if (cb->status & (BIT(GP_IRQ2) << 5))
			geni_i3c_err(gi3c, CRC_ERR);
		if (cb->status & (BIT(GP_IRQ3) << 5))
			geni_i3c_err(gi3c, BUS_PROTO);
		if (cb->status & (BIT(GP_IRQ4) << 5))
			geni_i3c_err(gi3c, NACK_7E);
		if (cb->status & (BIT(GP_IRQ5) << 5))
			geni_i3c_err(gi3c, NACK_IBI);
	}
}

/*
 * gi3c_ev_cb() - I3C GSI Event Callback function
 *
 * @ch: pointer to dma event channel
 * @cb_str: gpi callback
 * @ptr: private pointer pointing to gsi_common
 *
 * Return: None
 */
static void gi3c_ev_cb(struct dma_chan *ch, struct msm_gpi_cb const *cb_str, void *ptr)
{
	struct gsi_common *gsi = (struct gsi_common *)ptr;
	struct geni_i3c_dev *gi3c = (struct geni_i3c_dev *)gsi->dev_node;

	I3C_LOG_DBG(gi3c->ipcl, false, gi3c->se.dev, "%s cb_str->cb_event=0x%x\n",
		    __func__, cb_str->cb_event);
	switch (cb_str->cb_event) {
	case MSM_GPI_QUP_ERROR:
	case MSM_GPI_QUP_SW_ERROR:
	case MSM_GPI_QUP_CH_ERROR:
	case MSM_GPI_QUP_MAX_EVENT:
	case MSM_GPI_QUP_FW_ERROR:
	case MSM_GPI_QUP_PENDING_EVENT:
	case MSM_GPI_QUP_EOT_DESC_MISMATCH:
		break;
	case MSM_GPI_QUP_NOTIFY:
		geni_i3c_handle_err(gi3c, cb_str->status);
		complete_all(&gi3c->done);
		break;
	default:
		break;
	}

	if (cb_str->cb_event != MSM_GPI_QUP_NOTIFY) {
		I3C_LOG_ERR(gi3c->ipcl, false, gi3c->se.dev,
			    "GSI QN err:0x%x, status:0x%x, err:%d\n",
			    cb_str->error_log.error_code, cb_str->status, cb_str->cb_event);
		gi3c->gsi_err = true;
		complete_all(&gi3c->done);
	}
}

/*
 * gi3c_gsi_tx_cb() - I3C GSI Tx Callback function
 *
 * @ptr: pointer to tx callback param
 *
 * Return: None
 */
static void gi3c_gsi_tx_cb(void *ptr)
{
	struct msm_gpi_dma_async_tx_cb_param *tx_cb = ptr;
	struct gsi_common *gsi;
	struct geni_i3c_dev *gi3c;

	if (!(tx_cb && tx_cb->userdata)) {
		pr_err("%s: Invalid tx_cb buffer\n", __func__);
		return;
	}

	gsi = tx_cb->userdata;
	gi3c = (struct geni_i3c_dev *)gsi->dev_node;

	gi3c_gsi_cb_err(tx_cb, "TX");
	atomic_inc(&gi3c->gsi.tx.tre_queue.irq_cnt);
	I3C_LOG_DBG(gi3c->ipcl, false, gi3c->se.dev, "%s rnw:%d irq_cnt:%d\n",
		    __func__, gi3c->cur_rnw, atomic_read(&gi3c->gsi.tx.tre_queue.irq_cnt));
	complete_all(&gi3c->done);
}

/*
 * gi3c_gsi_rx_cb() - I3C GSI Rx Callback function
 *
 * @ptr: pointer to rx callback param
 *
 * Return: None
 */
static void gi3c_gsi_rx_cb(void *ptr)
{
	struct msm_gpi_dma_async_tx_cb_param *rx_cb = ptr;
	struct gsi_common *gsi;
	struct geni_i3c_dev *gi3c;

	if (!(rx_cb && rx_cb->userdata)) {
		pr_err("%s: Invalid rx_cb buffer\n", __func__);
		return;
	}

	gsi = rx_cb->userdata;
	gi3c = (struct geni_i3c_dev *)gsi->dev_node;

	I3C_LOG_DBG(gi3c->ipcl, false, gi3c->se.dev, "%s rnw:%d\n", __func__, gi3c->cur_rnw);
	if (gi3c->cur_rnw & READ_TRANSACTION) {
		gi3c_gsi_cb_err(rx_cb, "RX");
		complete_all(&gi3c->done);
	} else {
		I3C_LOG_ERR(gi3c->ipcl, false, gi3c->se.dev, "%s:Unexpected gsi rx cb\n", __func__);
	}
}

/*
 * i3c_setup_cfg0_tre() - Populates gsi config tre parameters
 *
 * @gi3c: i3c master device handle
 * @xfer: i3c transfer parameters pointer
 * @idx: idx of message under transfer
 * @gsi_bei: flag to enable gsi block event interrupt
 * @multi_tre_tx_xfer: flag indicating if transfer is part of multi tre transfer
 *
 * Return: None
 */
static void i3c_setup_cfg0_tre(struct geni_i3c_dev *gi3c, struct geni_i3c_xfer_params *xfer,
			       int idx, bool gsi_bei, bool multi_tre_tx_xfer)
{
	const struct geni_i3c_clk_fld *itr = gi3c->clk_fld;
	struct msm_gpi_tre *cfg0_t = &gi3c->gsi.tx.tre.config_t;
	struct gsi_tre_queue *tx_tre_q = &gi3c->gsi.tx.tre_queue;
	u32 cur_len;

	if (multi_tre_tx_xfer)
		cur_len = tx_tre_q->len[idx % GSI_MAX_NUM_TRE_MSGS];
	else
		cur_len = gi3c->cur_len;

	/* config0 */
	cfg0_t->dword[0] = MSM_GPI_I3C_CONFIG0_TRE_DWORD0(I3C_PACK_EN, itr->i2c_t_cycle_cnt,
							  itr->i2c_t_high_cnt, itr->i2c_t_low_cnt);
	cfg0_t->dword[1] = MSM_GPI_I3C_CONFIG0_TRE_DWORD1(0, itr->i3c_t_cycle_cnt,
							  itr->i3c_t_high_cnt);
	cfg0_t->dword[2] = MSM_GPI_I3C_CONFIG0_TRE_DWORD2(gi3c->dfs_idx, itr->clk_div);
	cfg0_t->dword[3] = MSM_GPI_I3C_CONFIG0_TRE_DWORD3(0, gsi_bei, 0, 0, 1);

	I3C_LOG_DBG(gi3c->ipcl, false, gi3c->se.dev, "%s: dfs:%d div:%d len:%d\n",
		    __func__, gi3c->dfs_idx, itr->clk_div, cur_len);
	I3C_LOG_DBG(gi3c->ipcl, false, gi3c->se.dev,
		    "%s: dword[0]:0x%x dword[1]:0x%x dword[2]:0x%x dword[3]:0x%x\n", __func__,
		    cfg0_t->dword[0], cfg0_t->dword[1], cfg0_t->dword[2], cfg0_t->dword[3]);
}

/*
 * i3c_setup_go_tre() - Populates gsi go tre parameters for tx/rx
 *
 * @gi3c: i3c master device handle
 * @xfer: i3c transfer parameters pointer
 * @idx: idx of message under transfer
 * @gsi_bei: flag to enable gsi block event interrupt
 * @multi_tre_tx_xfer: flag indicating if transfer is part of multi tre transfer
 *
 * Return: None
 */
static void i3c_setup_go_tre(struct geni_i3c_dev *gi3c, struct geni_i3c_xfer_params *xfer,
			     int idx, bool gsi_bei, bool multi_tre_tx_xfer)
{
	struct msm_gpi_tre *go_t = &gi3c->gsi.tx.tre.go_t;
	struct gsi_tre_queue *tx_tre_q = &gi3c->gsi.tx.tre_queue;
	bool use_7e = (xfer->m_param & USE_7E) ? 1 : 0;
	bool nack_ibi = (xfer->m_param & IBI_NACK_TBL_CTRL) ? 1 : 0;
	bool cont_mode = (xfer->m_param & CONTINUOUS_MODE_DAA) ? 1 : 0;
	bool bypass_addrspace = (xfer->m_param & BYPASS_ADDR_PHASE) ? 1 : 0;
	u8 addr =  (xfer->m_param >> SLV_ADDR_SHFT) & I3C_ADDR_MASK;
	u8 ccc = (xfer->m_param & CCC_HDR_CMD_MSK) >> CCC_HDR_CMD_SHFT;
	u32 cur_len;

	if (multi_tre_tx_xfer)
		cur_len = tx_tre_q->len[idx % GSI_MAX_NUM_TRE_MSGS];
	else
		cur_len = gi3c->cur_len;

	go_t->dword[0] = MSM_GPI_I3C_GO_TRE_DWORD0((1 << 2 | bypass_addrspace << 7), ccc,
						   addr, xfer->m_cmd);
	go_t->dword[1] = MSM_GPI_I3C_GO_TRE_DWORD1(use_7e << 0 | nack_ibi << 1 | cont_mode << 2);
	if (gi3c->cur_rnw == READ_TRANSACTION) {
		go_t->dword[2] = MSM_GPI_I3C_GO_TRE_DWORD2(cur_len);
		go_t->dword[3] = MSM_GPI_I3C_GO_TRE_DWORD3(1, 0, 0, 1, 0);
	} else {
		/* For Tx Go tre: ieob is not set, chain bit is set */
		go_t->dword[2] = MSM_GPI_I3C_GO_TRE_DWORD2(cur_len);
		if (cur_len)
			go_t->dword[3] = MSM_GPI_I3C_GO_TRE_DWORD3(0, gsi_bei, 0, 0, 1);
		else
			/* for ccc commands which doesn't have data, chain bit not needed */
			go_t->dword[3] = MSM_GPI_I3C_GO_TRE_DWORD3(0, 0, 1, 0, 0);
	}

	I3C_LOG_DBG(gi3c->ipcl, false, gi3c->se.dev, "%s cmd:0x%x param0x%x ccc:0x%x addr:0x%x\n",
		    __func__, xfer->m_cmd, xfer->m_param, ccc, addr);
	I3C_LOG_DBG(gi3c->ipcl, false, gi3c->se.dev,
		    "%s:use_7e:%d nack_ibi:%d cont_mod:%d, bypass addrspace:%d idx:%d gsi_bei:%d\n",
		    __func__, use_7e, nack_ibi, cont_mode, bypass_addrspace, idx, gsi_bei);
	I3C_LOG_DBG(gi3c->ipcl, false, gi3c->se.dev,
		    "%s: dword[0]:0x%x dword[1]:0x%x dword[2]:0x%x dword[3]:0x%x\n",
		    __func__, go_t->dword[0], go_t->dword[1], go_t->dword[2], go_t->dword[3]);
}

/*
 * i3c_setup_rx_tre() - Populates gsi rx dma tre parameters
 *
 * @gi3c: i3c master device handle
 *
 * Return: None
 */
static void i3c_setup_rx_tre(struct geni_i3c_dev *gi3c)
{
	struct msm_gpi_tre *rx_t = &gi3c->gsi.rx.tre.dma_t;

	rx_t->dword[0] = MSM_GPI_DMA_W_BUFFER_TRE_DWORD0(gi3c->rx_phy);
	rx_t->dword[1] = MSM_GPI_DMA_W_BUFFER_TRE_DWORD1(gi3c->rx_phy);
	rx_t->dword[2] = MSM_GPI_DMA_W_BUFFER_TRE_DWORD2(gi3c->cur_len);
	/* Set ieot for all Rx/Tx DMA tres */
	rx_t->dword[3] = MSM_GPI_DMA_W_BUFFER_TRE_DWORD3(0, 0, 1, 0, 0);

	I3C_LOG_DBG(gi3c->ipcl, false, gi3c->se.dev,
		    "%s: dword[0]:0x%x dword[1]:0x%x dword[2]:0x%x dword[3]:0x%x\n",
		    __func__, rx_t->dword[0], rx_t->dword[1],  rx_t->dword[2], rx_t->dword[3]);
}

/*
 * geni_i3c_fill_immediate_dma_data() - fills i3c data payload in provided tre buffer
 *
 * @dest: pointer to tre destination buffer to copy data
 * @src: pointer to i3c data payload
 * @len: length of data to copy, max GSI_MAX_IMMEDIATE_DMA_LEN
 *
 * Return: None
 */
static void geni_i3c_fill_immediate_dma_data(u8 *dest, u8 *src, int len)
{
	int i;

	if (len <= GSI_MAX_IMMEDIATE_DMA_LEN)
		for (i = 0; i < len; i++)
			dest[i] = src[i];
}

/*
 * i3c_setup_tx_tre() - Populates gsi tx dma tre parameters
 *
 * @gi3c: i3c master device handle
 * @tx_idx: idx of tx message under transfer
 * @gsi_bei: flag to enable gsi block event interrupt
 * @multi_tre_tx_xfer: flag indicating if transfer is part of multi tre transfer
 *
 * Return: None
 */
static void i3c_setup_tx_tre(struct geni_i3c_dev *gi3c, int tx_idx, bool gsi_bei,
			     bool multi_tre_tx_xfer)
{
	struct msm_gpi_tre *tx_t = &gi3c->gsi.tx.tre.dma_t;
	struct gsi_tre_queue *tx_tre_q = &gi3c->gsi.tx.tre_queue;
	u32 cur_len = 0;
	int xfer_tx_idx = tx_idx % GSI_MAX_NUM_TRE_MSGS;

	if (multi_tre_tx_xfer)
		cur_len = tx_tre_q->len[xfer_tx_idx];
	else
		cur_len = gi3c->cur_len;

	if (multi_tre_tx_xfer && cur_len <= GSI_MAX_IMMEDIATE_DMA_LEN) {
		tx_t->dword[0] = 0;
		tx_t->dword[1] = 0;
		geni_i3c_fill_immediate_dma_data((u8 *)&tx_t->dword[0],
						 (u8 *)tx_tre_q->virt_buf[xfer_tx_idx], cur_len);
		tx_t->dword[2] = MSM_GPI_DMA_IMMEDIATE_TRE_DWORD2(cur_len);
		tx_t->dword[3] = MSM_GPI_DMA_IMMEDIATE_TRE_DWORD3(0, gsi_bei, 1, 0, 0);
	} else {
		tx_t->dword[0] = MSM_GPI_DMA_W_BUFFER_TRE_DWORD0(tx_tre_q->dma_buf[xfer_tx_idx]);
		tx_t->dword[1] = MSM_GPI_DMA_W_BUFFER_TRE_DWORD1(tx_tre_q->dma_buf[xfer_tx_idx]);
		tx_t->dword[2] = MSM_GPI_DMA_W_BUFFER_TRE_DWORD2(cur_len);
		tx_t->dword[3] = MSM_GPI_DMA_W_BUFFER_TRE_DWORD3(0, gsi_bei, 1, 0, 0);
	}
	I3C_LOG_DBG(gi3c->ipcl, false, gi3c->se.dev,
		    "%s: dword[0]:0x%x dword[1]:0x%x dword[2]:0x%x dword[3]:0x%x tx_idx:%d gsi_bei:%d\n",
		    __func__, tx_t->dword[0], tx_t->dword[1], tx_t->dword[2],
		    tx_t->dword[3], tx_idx, gsi_bei);
}

/*
 * geni_i3c_err_prep_sg() - terminates dma transfers when there is a gsi error
 *
 * @gi3c: i3c master device handle
 *
 * Return: 0 on success, error code on failure
 */
static int geni_i3c_err_prep_sg(struct geni_i3c_dev *gi3c)
{
	int ret = 0;

	if (gi3c->err || gi3c->gsi_err) {
		ret = dmaengine_terminate_all(gi3c->gsi.tx.ch);
		if (ret)
			I3C_LOG_ERR(gi3c->ipcl, true, gi3c->se.dev,
				    "%s:gpi dma terminate failed ret:%d\n", __func__, ret);
		gi3c->cfg_sent = false;
	}

	if (gi3c->gsi_err) {
		/* if i3c error already present, no need to update error values */
		if (!gi3c->err) {
			gi3c->err = -EIO;
			ret = gi3c->err;
		}
		gi3c->gsi_err = false;
	}
	return ret;
}

/*
 * geni_i3c_gsi_multi_write() - Does gsi multiple writes using multiple tre's for i3c tx messages
 *
 * @gi3c: i3c master device handle
 * @xfer: i3c tx transfer parameters pointer
 * @num_xfers: total number of tx transfers
 *
 * Return: 0 on success, error code on failure
 */
static int geni_i3c_gsi_multi_write(struct geni_i3c_dev *gi3c,
				    struct geni_i3c_xfer_params *xfer, int num_xfers)
{
	struct gsi_tre_queue *tx_tre_q = &gi3c->gsi.tx.tre_queue;
	bool tx_chan = true, skip_callbacks = false;
	int tre_cnt = 0, ret = 0, time_remaining = 0;
	int xfer_tx_idx = xfer->tx_idx % GSI_MAX_NUM_TRE_MSGS;

	I3C_LOG_DBG(gi3c->ipcl, true, gi3c->se.dev,
		    "%s Enter num_xfer=%d idx=%d len=%d\n", __func__,
		    num_xfers, xfer->tx_idx, tx_tre_q->len[xfer_tx_idx]);

	gi3c->err = 0;
	gi3c->gsi_err = false;
	gi3c->gsi.tx.tre.flags = 0;

	if (!gi3c->gsi.req_chan) {
		ret = geni_gsi_common_request_channel(&gi3c->gsi);
		if (ret)
			return ret;
	}

	xfer->gsi_bei = false;
	if (((xfer->tx_idx + 1) % NUM_I3C_TRE_MSGS_PER_INTR) && (xfer->tx_idx != num_xfers - 1)) {
		xfer->gsi_bei = true;
		skip_callbacks = true;
	}

	/* Send cfg tre when cfg not sent already */
	if (!gi3c->cfg_sent) {
		i3c_setup_cfg0_tre(gi3c, xfer, xfer->tx_idx, true, true);
		gi3c->gsi.tx.tre.flags |= CONFIG_TRE_SET;
	}

	i3c_setup_go_tre(gi3c, xfer, xfer->tx_idx, true, true);
	gi3c->gsi.tx.tre.flags |= GO_TRE_SET;

	if (tx_tre_q->len[xfer_tx_idx] > GSI_MAX_IMMEDIATE_DMA_LEN) {
		ret = geni_se_common_iommu_map_buf(gi3c->wrapper_dev,
						   &tx_tre_q->dma_buf[xfer_tx_idx],
						   tx_tre_q->virt_buf[xfer_tx_idx],
						   tx_tre_q->len[xfer_tx_idx], DMA_TO_DEVICE);
		if (ret) {
			I3C_LOG_ERR(gi3c->ipcl, true, gi3c->se.dev,
				    "%s:geni_se_common_iommu_map_buf fail ret:%d\n", __func__, ret);
			goto geni_i3c_gsi_write_xfer_out;
		}
	}

	if (tx_tre_q->len[xfer_tx_idx]) {
		i3c_setup_tx_tre(gi3c, xfer->tx_idx, xfer->gsi_bei, true);
		gi3c->gsi.tx.tre.flags |= DMA_TRE_SET;
	}

	tre_cnt = gsi_common_fill_tre_buf(&gi3c->gsi, tx_chan);
	gi3c->gsi.tx.tre_queue.msg_cnt++;
	ret = gsi_common_prep_desc_and_submit(&gi3c->gsi, tre_cnt, tx_chan, skip_callbacks);
	if (ret < 0) {
		gi3c->err = ret;
		goto geni_i3c_err_prep;
	}

	if (!gi3c->cfg_sent)
		gi3c->cfg_sent = true;

	if ((xfer->tx_idx != num_xfers - 1) &&
	    (gi3c->gsi.tx.tre_queue.msg_cnt <
	     GSI_MAX_NUM_TRE_MSGS + gi3c->gsi.tx.tre_queue.freed_msg_cnt))
		return 0;

	time_remaining = gsi_common_tx_tre_optimization(&gi3c->gsi, num_xfers,
							NUM_I3C_TRE_MSGS_PER_INTR,
							msecs_to_jiffies(XFER_TIMEOUT),
							gi3c->wrapper_dev);
	if (!time_remaining) {
		I3C_LOG_ERR(gi3c->ipcl, true, gi3c->se.dev,
			    "%s:wait_for_completion timedout\n", __func__);
		geni_i3c_err(gi3c, GENI_TIMEOUT);
		geni_i3c_se_dump_dbg_regs(&gi3c->se, gi3c->se.base, gi3c->ipcl);
		reinit_completion(&gi3c->done);
		goto geni_i3c_err_prep;
	}
	I3C_LOG_DBG(gi3c->ipcl, true, gi3c->se.dev,
		    "%s Completed xfer->tx_idx=%d num_xfers=%d gsi_bei=%d\n",
		    __func__, xfer->tx_idx, num_xfers, xfer->gsi_bei);
geni_i3c_err_prep:
	geni_i3c_err_prep_sg(gi3c);
	if (gi3c->err) {
		gsi_common_tre_process(&gi3c->gsi, num_xfers, NUM_I3C_TRE_MSGS_PER_INTR,
				       gi3c->wrapper_dev);
		ret = (gi3c->err == -EBUSY) ? I3C_ERROR_M2 : gi3c->err;
		I3C_LOG_DBG(gi3c->ipcl, false, gi3c->se.dev,
			    "%s:I3C transaction error :%d\n", __func__, gi3c->err);
	}

geni_i3c_gsi_write_xfer_out:
	if (!ret && gi3c->err)
		ret = gi3c->err;
	return ret;
}

/*
 * geni_i3c_gsi_write() - Does single gsi tx operation for a i3c write msg
 *
 * @gi3c: i3c master device handle
 * @xfer: i3c tx transfer parameters pointer
 *
 * Return: 0 on success, error code on failure
 */
static int geni_i3c_gsi_write(struct geni_i3c_dev *gi3c, struct geni_i3c_xfer_params *xfer)
{
	struct gsi_tre_queue *tx_tre_q = &gi3c->gsi.tx.tre_queue;
	bool tx_chan = true;
	int tre_cnt = 0, ret = 0, time_remaining = 0;

	if (!gi3c->gsi.req_chan) {
		ret = geni_gsi_common_request_channel(&gi3c->gsi);
		if (ret)
			return ret;
	}

	gi3c->err = 0;
	gi3c->gsi_err = false;
	gi3c->gsi.tx.tre.flags = 0;
	reinit_completion(&gi3c->done);

	/* Send cfg tre when cfg not sent already */
	if (!gi3c->cfg_sent) {
		i3c_setup_cfg0_tre(gi3c, xfer, 0, false, false);
		gi3c->gsi.tx.tre.flags |= CONFIG_TRE_SET;
	}

	i3c_setup_go_tre(gi3c, xfer, 0, false, false);
	gi3c->gsi.tx.tre.flags |= GO_TRE_SET;

	ret = geni_se_common_iommu_map_buf(gi3c->wrapper_dev, &tx_tre_q->dma_buf[0],
					   gi3c->cur_buf, gi3c->cur_len, DMA_TO_DEVICE);
	if (ret) {
		I3C_LOG_ERR(gi3c->ipcl, true, gi3c->se.dev,
			    "%s:geni_se_common_iommu_map_buf failed ret:%d\n", __func__, ret);
		goto geni_i3c_gsi_write_xfer_out;
	}

	if (gi3c->cur_len) {
		i3c_setup_tx_tre(gi3c, 0, false, false);
		gi3c->gsi.tx.tre.flags |= DMA_TRE_SET;
	}
	tre_cnt = gsi_common_fill_tre_buf(&gi3c->gsi, tx_chan);
	ret = gsi_common_prep_desc_and_submit(&gi3c->gsi,  tre_cnt, tx_chan, false);
	if (ret < 0) {
		gi3c->err = ret;
		goto geni_i3c_err_prep;
	}

	time_remaining = wait_for_completion_timeout(&gi3c->done, XFER_TIMEOUT);
	if (!time_remaining) {
		I3C_LOG_ERR(gi3c->ipcl, true, gi3c->se.dev,
			    "%s:wait_for_completion timed out\n", __func__);
		geni_i3c_err(gi3c, GENI_TIMEOUT);
		geni_i3c_se_dump_dbg_regs(&gi3c->se, gi3c->se.base, gi3c->ipcl);
		gi3c->cur_buf = NULL;
		gi3c->cur_len = 0;
		gi3c->cur_idx = 0;
		gi3c->cur_rnw = 0;
		reinit_completion(&gi3c->done);
		goto geni_i3c_err_prep;
	}

	if (!gi3c->cfg_sent)
		gi3c->cfg_sent = true;
geni_i3c_err_prep:
	ret = geni_i3c_err_prep_sg(gi3c);
	geni_se_common_iommu_unmap_buf(gi3c->wrapper_dev, &tx_tre_q->dma_buf[0],
				       gi3c->cur_len, DMA_TO_DEVICE);
	if (gi3c->err) {
		ret = (gi3c->err == -EBUSY) ? I3C_ERROR_M2 : gi3c->err;
		I3C_LOG_DBG(gi3c->ipcl, false, gi3c->se.dev,
			    "%s:I3C transaction error :%d\n", __func__, gi3c->err);
	}

geni_i3c_gsi_write_xfer_out:
	if (!ret && gi3c->err)
		ret = gi3c->err;
	return ret;
}

/*
 * geni_i3c_gsi_read() - Does single gsi rx operation for a i3c read msg
 *
 * @gi3c: i3c master device handle
 * @xfer: i3c rx transfer parameters pointer
 *
 * Return: 0 on success, error code on failure
 */
static int geni_i3c_gsi_read(struct geni_i3c_dev *gi3c, struct geni_i3c_xfer_params *xfer)
{
	bool tx_chan = true;
	int tre_cnt = 0, ret = 0, time_remaining = 0;

	if (!gi3c->gsi.req_chan) {
		ret = geni_gsi_common_request_channel(&gi3c->gsi);
		if (ret)
			return ret;
	}

	gi3c->err = 0;
	gi3c->gsi_err = false;
	gi3c->gsi.tx.tre.flags = 0;
	gi3c->gsi.rx.tre.flags = 0;
	reinit_completion(&gi3c->done);

	/* Send cfg tre only once */
	if (!gi3c->cfg_sent) {
		i3c_setup_cfg0_tre(gi3c, xfer, 0, false, false);
		gi3c->gsi.tx.tre.flags |= CONFIG_TRE_SET;
	}

	i3c_setup_go_tre(gi3c, xfer, 0, false, false);
	gi3c->gsi.tx.tre.flags |= GO_TRE_SET;

	ret = geni_se_common_iommu_map_buf(gi3c->wrapper_dev, &gi3c->rx_phy, gi3c->cur_buf,
					   gi3c->cur_len, DMA_FROM_DEVICE);
	if (ret) {
		I3C_LOG_ERR(gi3c->ipcl, true, gi3c->se.dev,
			    "%s:geni_se_common_iommu_map_buf failed ret:%d\n", __func__, ret);
		goto geni_i3c_gsi_read_xfer_out;
	}

	i3c_setup_rx_tre(gi3c);
	gi3c->gsi.rx.tre.flags |= DMA_TRE_SET;
	tre_cnt = gsi_common_fill_tre_buf(&gi3c->gsi, !tx_chan);
	ret = gsi_common_prep_desc_and_submit(&gi3c->gsi, tre_cnt, !tx_chan, false);
	if (ret < 0) {
		gi3c->err = ret;
		goto geni_i3c_err_prep;
	}

	/* submit config/go tre through tx channel */
	tre_cnt = gsi_common_fill_tre_buf(&gi3c->gsi, tx_chan);
	ret = gsi_common_prep_desc_and_submit(&gi3c->gsi, tre_cnt, tx_chan, false);
	if (ret < 0) {
		gi3c->err = ret;
		goto geni_i3c_err_prep;
	}

	time_remaining = wait_for_completion_timeout(&gi3c->done, XFER_TIMEOUT);
	if (!time_remaining) {
		I3C_LOG_ERR(gi3c->ipcl, true, gi3c->se.dev,
			    "%s:wait_for_completion timed out\n", __func__);
		geni_i3c_err(gi3c, GENI_TIMEOUT);
		geni_i3c_se_dump_dbg_regs(&gi3c->se, gi3c->se.base, gi3c->ipcl);
		gi3c->cur_buf = NULL;
		gi3c->cur_len = 0;
		gi3c->cur_idx = 0;
		gi3c->cur_rnw = 0;
		reinit_completion(&gi3c->done);
		goto geni_i3c_err_prep;
	}

	if (!gi3c->cfg_sent)
		gi3c->cfg_sent = true;
geni_i3c_err_prep:
	ret = geni_i3c_err_prep_sg(gi3c);
	geni_se_common_iommu_unmap_buf(gi3c->wrapper_dev, &gi3c->rx_phy,
				       gi3c->cur_len, DMA_FROM_DEVICE);
	if (gi3c->err) {
		ret = (gi3c->err == -EBUSY) ? I3C_ERROR_M2 : gi3c->err;
		I3C_LOG_DBG(gi3c->ipcl, false, gi3c->se.dev,
			    "%s:I3C transaction error:%d\n", __func__, gi3c->err);
	}

geni_i3c_gsi_read_xfer_out:
	if (!ret && gi3c->err)
		ret = gi3c->err;
	return ret;
}

/*
 * geni_i3c_fifo_dma_xfer() - Does single fifo/dma tx/rx operation for a i3c msg
 *
 * @gi3c: i3c master device handle
 * @xfer: i3c rx transfer parameters pointer
 *
 * Return: 0 on success, error code on failure
 */
static int geni_i3c_fifo_dma_xfer(struct geni_i3c_dev *gi3c, struct geni_i3c_xfer_params *xfer)
{
	dma_addr_t tx_dma = 0;
	dma_addr_t rx_dma = 0;
	int ret = 0, time_remaining = 0;
	enum i3c_trans_dir rnw = gi3c->cur_rnw;
	u32 len = gi3c->cur_len;
	unsigned long flags;

	reinit_completion(&gi3c->done);
	geni_se_select_mode(&gi3c->se, xfer->mode);

	gi3c->err = 0;
	gi3c->cur_idx = 0;

	if (rnw == READ_TRANSACTION) {
		writel_relaxed(len, gi3c->se.base + SE_I3C_RX_TRANS_LEN);
		geni_se_setup_m_cmd(&gi3c->se, xfer->m_cmd, xfer->m_param);

		I3C_LOG_DBG(gi3c->ipcl, false, gi3c->se.dev,
			    "I3C cmd:0x%x param:0x%x READ len:%d, m_cmd: 0x%x\n",
			    xfer->m_cmd, xfer->m_param, len,
			    geni_read_reg(gi3c->se.base, SE_GENI_M_CMD0));

		if (xfer->mode == GENI_SE_DMA) {
			ret = geni_se_rx_dma_prep(&gi3c->se, gi3c->cur_buf, len, &rx_dma);
			if (ret) {
				I3C_LOG_ERR(gi3c->ipcl, true, gi3c->se.dev,
					    "DMA Err:%d FIFO mode enabled\n", ret);
				xfer->mode = GENI_SE_FIFO;
				geni_se_select_mode(&gi3c->se, xfer->mode);
			}
		}
	} else {
		writel_relaxed(len, gi3c->se.base + SE_I3C_TX_TRANS_LEN);
		geni_se_setup_m_cmd(&gi3c->se, xfer->m_cmd, xfer->m_param);

		I3C_LOG_DBG(gi3c->ipcl, false, gi3c->se.dev,
			    "I3C cmd:0x%x param:0x%x WRITE len:%d, m_cmd: 0x%x\n",
			    xfer->m_cmd, xfer->m_param, len,
			    geni_read_reg(gi3c->se.base, SE_GENI_M_CMD0));

		if (xfer->mode == GENI_SE_DMA) {
			ret = geni_se_tx_dma_prep(&gi3c->se, gi3c->cur_buf, len, &tx_dma);
			if (ret) {
				I3C_LOG_ERR(gi3c->ipcl, true, gi3c->se.dev,
					    "DMA Err:%d FIFO mode enabled\n", ret);
				xfer->mode = GENI_SE_FIFO;
				geni_se_select_mode(&gi3c->se, xfer->mode);
			}
		}
		if (xfer->mode == GENI_SE_FIFO && len > 0) /* Get FIFO IRQ */
			writel_relaxed(1, gi3c->se.base + SE_GENI_TX_WATERMARK_REG);
	}

	time_remaining = wait_for_completion_timeout(&gi3c->done, XFER_TIMEOUT);
	if (!time_remaining) {
		I3C_LOG_ERR(gi3c->ipcl, true, gi3c->se.dev, "wait_for_completion timed out\n");
		geni_i3c_err(gi3c, GENI_TIMEOUT);
		geni_i3c_se_dump_dbg_regs(&gi3c->se, gi3c->se.base, gi3c->ipcl);
		gi3c->cur_buf = NULL;
		gi3c->cur_len = 0;
		gi3c->cur_idx = 0;
		gi3c->cur_rnw = 0;

		reinit_completion(&gi3c->done);

		spin_lock_irqsave(&gi3c->spinlock, flags);
		geni_se_cancel_m_cmd(&gi3c->se);
		spin_unlock_irqrestore(&gi3c->spinlock, flags);

		time_remaining = wait_for_completion_timeout(&gi3c->done, HZ);
		if (!time_remaining) {
			I3C_LOG_ERR(gi3c->ipcl, true, gi3c->se.dev,
				    "%s:Cancel failed: Aborting\n", __func__);
			geni_i3c_se_dump_dbg_regs(&gi3c->se, gi3c->se.base, gi3c->ipcl);
			reinit_completion(&gi3c->done);
			spin_lock_irqsave(&gi3c->spinlock, flags);
			geni_se_abort_m_cmd(&gi3c->se);
			spin_unlock_irqrestore(&gi3c->spinlock, flags);
			time_remaining = wait_for_completion_timeout(&gi3c->done, XFER_TIMEOUT);
			if (!time_remaining) {
				I3C_LOG_ERR(gi3c->ipcl, true, gi3c->se.dev,
					    "%s:Abort Failed\n", __func__);
				geni_i3c_se_dump_dbg_regs(&gi3c->se, gi3c->se.base, gi3c->ipcl);
			}
		}
	}

	if (xfer->mode == GENI_SE_DMA) {
		if (gi3c->err) {
			reinit_completion(&gi3c->done);
			if (rnw == READ_TRANSACTION)
				writel_relaxed(1, gi3c->se.base + SE_DMA_RX_FSM_RST);
			else
				writel_relaxed(1, gi3c->se.base + SE_DMA_TX_FSM_RST);
			time_remaining =
			wait_for_completion_timeout(&gi3c->done, XFER_TIMEOUT);
			if (!time_remaining) {
				I3C_LOG_ERR(gi3c->ipcl, true, gi3c->se.dev,
					    "Timeout:FSM Reset, rnw:%d\n", rnw);
				geni_i3c_se_dump_dbg_regs(&gi3c->se, gi3c->se.base, gi3c->ipcl);
			}
		}

		if (rnw == READ_TRANSACTION)
			geni_se_rx_dma_unprep(&gi3c->se, rx_dma, len);
		else
			geni_se_tx_dma_unprep(&gi3c->se, tx_dma, len);
	}

	if (gi3c->err) {
		ret = (gi3c->err == -EBUSY) ? I3C_ERROR_M2 : gi3c->err;
		I3C_LOG_DBG(gi3c->ipcl, false, gi3c->se.dev,
			    "I3C transaction error:%d\n", gi3c->err);
	}

	gi3c->cur_buf = NULL;
	gi3c->cur_len = 0;
	gi3c->cur_idx = 0;
	gi3c->cur_rnw = 0;
	gi3c->err = 0;

	return ret;
}

static int geni_i3c_clk_map_idx(struct geni_i3c_dev *gi3c)
{
	int i;
	struct i3c_master_controller *m = &gi3c->ctrlr;
	const struct geni_i3c_clk_fld *itr = geni_i3c_clk_map;
	struct i3c_bus *bus = i3c_master_get_bus(m);

	for (i = 0; i < ARRAY_SIZE(geni_i3c_clk_map); i++, itr++) {
		if ((!bus || itr->clk_freq_out == bus->scl_rate.i3c) &&
		    KHZ(itr->clk_src_freq) == gi3c->clk_src_freq) {
			gi3c->clk_fld = itr;
		}

		if (itr->clk_freq_out == bus->scl_rate.i2c)
			gi3c->clk_od_fld = itr;
	}

	if (!gi3c->clk_fld || !gi3c->clk_od_fld) {
		I3C_LOG_ERR(gi3c->ipcl, true, gi3c->se.dev, "%s : clk mapping failed", __func__);
		return -EINVAL;
	}

	return 0;
}

static void set_new_addr_slot(unsigned long *addrslot, u8 addr)
{
	unsigned long *ptr;

	if (addr > I3C_ADDR_MASK)
		return;

	ptr = addrslot + (addr / BITS_PER_LONG);
	*ptr |= 1 << (addr % BITS_PER_LONG);
}

static void clear_new_addr_slot(unsigned long *addrslot, u8 addr)
{
	unsigned long *ptr;

	if (addr > I3C_ADDR_MASK)
		return;

	ptr = addrslot + (addr / BITS_PER_LONG);
	*ptr &= ~(1 << (addr % BITS_PER_LONG));
}

static bool is_new_addr_slot_set(unsigned long *addrslot, u8 addr)
{
	unsigned long *ptr;

	if (addr > I3C_ADDR_MASK)
		return false;

	ptr = addrslot + (addr / BITS_PER_LONG);
	return ((*ptr & (1 << (addr % BITS_PER_LONG))) != 0);
}

static void qcom_geni_i3c_conf(struct geni_i3c_dev *gi3c, enum i3c_bus_phase bus_phase)
{
	const struct geni_i3c_clk_fld *itr = gi3c->clk_fld;
	u32 val;
	unsigned long freq;
	int ret = 0;

	if (bus_phase == OPEN_DRAIN_MODE)
		itr = gi3c->clk_od_fld;

	ret = geni_se_clk_freq_match(&gi3c->se, KHZ(itr->clk_src_freq),
				     &gi3c->dfs_idx, &freq, false);
	if (ret)
		gi3c->dfs_idx = 0;

	if (gi3c->dfs_idx != gi3c->prev_dfs_idx) {
		if (gi3c->se_mode == GENI_GPI_DMA)
			gi3c->cfg_sent = false;

		I3C_LOG_DBG(gi3c->ipcl, false, gi3c->se.dev,
			    "%s:dfs index:%d, prev_dfs_idx:%d\n",
			    __func__, gi3c->dfs_idx, gi3c->prev_dfs_idx);
	}

	gi3c->prev_dfs_idx = gi3c->dfs_idx;
	if (gi3c->se_mode == GENI_GPI_DMA)
		return;
	writel_relaxed(gi3c->dfs_idx, gi3c->se.base + SE_GENI_CLK_SEL);

	val = itr->clk_div << CLK_DEV_VALUE_SHFT;
	val |= 1 << SER_CLK_EN_SHFT;
	writel_relaxed(val, gi3c->se.base + GENI_SER_M_CLK_CFG);

	val = itr->i2c_t_high_cnt << I2C_SCL_HIGH_COUNTER_SHFT;
	val |= itr->i2c_t_low_cnt << I2C_SCL_LOW_COUNTER_SHFT;
	val |= itr->i2c_t_cycle_cnt;
	writel_relaxed(val, gi3c->se.base + SE_I2C_SCL_COUNTERS);

	writel_relaxed(itr->i3c_t_cycle_cnt, gi3c->se.base + SE_I3C_SCL_CYCLE);
	writel_relaxed(itr->i3c_t_high_cnt, gi3c->se.base + SE_I3C_SCL_HIGH);
}

static void geni_i3c_hotjoin(struct work_struct *work)
{
	int ret;
	struct geni_i3c_dev *gi3c = container_of(work, struct geni_i3c_dev, hj_wd);

	pm_stay_awake(gi3c->se.dev);

	ret = i3c_master_do_daa(&gi3c->ctrlr);
	if (ret)
		I3C_LOG_ERR(gi3c->ipcl, true, gi3c->se.dev, "hotjoin:daa failed %d\n", ret);

	pm_relax(gi3c->se.dev);
}

static void geni_i3c_handle_received_ibi(struct geni_i3c_dev *gi3c)
{
	struct geni_i3c_i2c_dev_data *data;
	struct i3c_ibi_slot *slot;
	struct i3c_dev_desc *dev = gi3c->ibi.slots[0];
	u32 val, i;

	val = readl_relaxed(gi3c->ibi.ibi_base + IBI_RCVD_IBI_STATUS(0));

	if (!dev || !dev->ibi) {
		I3C_LOG_ERR(gi3c->ipcl, true, gi3c->se.dev, "Invalid IBI device\n");
		goto no_free_slot;
	}

	data = i3c_dev_get_master_data(dev);
	slot = i3c_generic_ibi_get_free_slot(data->ibi_pool);
	if (!slot) {
		I3C_LOG_ERR(gi3c->ipcl, true, gi3c->se.dev, "no free slot\n");
		goto no_free_slot;
	}

	for (i = 0; i < gi3c->ibi.num_slots; i++) {
		if (!(val & (1u << i)))
			continue;

		gi3c->ibi.data.info.info =
			readl_relaxed(gi3c->ibi.ibi_base + IBI_RCVD_IBI_INFO_ENTRY(0, i));
		gi3c->ibi.data.ts =
			readl_relaxed(gi3c->ibi.ibi_base + IBI_RCVD_IBI_TS_LSB_ENTRY(0, i));
		gi3c->ibi.data.payload =
			readl_relaxed(gi3c->ibi.ibi_base + IBI_RCVD_IBI_DATA_ENTRY_REG0(0, i));

		if (slot->data)
			memcpy(slot->data, &gi3c->ibi.data.payload, dev->ibi->max_payload_len);

		slot->len = min_t(unsigned int, gi3c->ibi.data.info.fields.num_bytes,
				  dev->ibi->max_payload_len);

		I3C_LOG_DBG(gi3c->ipcl, false, gi3c->se.dev,
			    "IBI: info: 0x%x, ts: 0x%x, Data: 0x%x\n",
			    gi3c->ibi.data.info.info, gi3c->ibi.data.ts, gi3c->ibi.data.payload);
	}

	i3c_master_queue_ibi(dev, slot);
no_free_slot:
	writel_relaxed(val, gi3c->ibi.ibi_base + IBI_RCVD_IBI_CLR(0));
}

static irqreturn_t geni_i3c_ibi_irq(int irq, void *dev)
{
	struct geni_i3c_dev *gi3c = dev;
	unsigned long flags;
	u32 m_stat = 0, m_stat_mask = 0;
	bool cmd_done = false;

	spin_lock_irqsave(&gi3c->ibi.lock, flags);

	if (irq == gi3c->ibi.mngr_irq) {
		m_stat_mask = readl_relaxed(gi3c->ibi.ibi_base + IBI_GEN_IRQ_EN);
		m_stat = readl_relaxed(gi3c->ibi.ibi_base
				+ IBI_GEN_IRQ_STATUS) & m_stat_mask;
		I3C_LOG_DBG(gi3c->ipcl, false, gi3c->se.dev,
			    "IBI MGR IRQ IBI_GEN_IRQ_STATUS:0x%x\n", m_stat);

		if ((m_stat & UNEXPECT_IBI_ADDR_IRQ_EN) || (m_stat & BUS_ERROR_EN))
			gi3c->ibi.err = m_stat;

		if ((m_stat & ENABLE_CHANGE_IRQ_EN) || (m_stat & SW_RESET_DONE_EN))
			cmd_done = true;

		if (m_stat & HOT_JOIN_IRQ_EN) {
			/* Queue worker to service hot-join request*/
			queue_work(gi3c->hj_wq, &gi3c->hj_wd);
		}
		/* clear interrupts */
		if (m_stat)
			writel_relaxed(m_stat, gi3c->ibi.ibi_base + IBI_GEN_IRQ_CLR);
	} else if (irq == gi3c->ibi.gpii_irq[0]) {
		m_stat = readl_relaxed(gi3c->ibi.ibi_base + IBI_IRQ_STATUS(0));
		I3C_LOG_DBG(gi3c->ipcl, false, gi3c->se.dev,
			    "IBI GPII IRQ, IBI_IRQ_STATUS:0x%x\n", m_stat);

		if (m_stat & SE_I3C_IBI_ERR)
			gi3c->ibi.err = m_stat;

		if (m_stat & IBI_RECEIVED)
			geni_i3c_handle_received_ibi(gi3c);

		if (m_stat & COMMAND_DONE)
			cmd_done = true;

		/* clear interrupts */
		if (m_stat)
			writel_relaxed(m_stat, gi3c->ibi.ibi_base + IBI_IRQ_CLR(0));
	}

	if (cmd_done)
		complete(&gi3c->ibi.done);
	spin_unlock_irqrestore(&gi3c->ibi.lock, flags);
	return IRQ_HANDLED;
}

static irqreturn_t geni_i3c_irq(int irq, void *dev)
{
	struct geni_i3c_dev *gi3c = dev;
	int j;
	u32 m_stat, m_stat_mask, rx_st;
	u32 dm_tx_st, dm_rx_st, dma;
	unsigned long flags;

	spin_lock_irqsave(&gi3c->spinlock, flags);

	m_stat = readl_relaxed(gi3c->se.base + SE_GENI_M_IRQ_STATUS);
	m_stat_mask = readl_relaxed(gi3c->se.base + SE_GENI_M_IRQ_EN);
	rx_st = readl_relaxed(gi3c->se.base + SE_GENI_RX_FIFO_STATUS);
	dm_tx_st = readl_relaxed(gi3c->se.base + SE_DMA_TX_IRQ_STAT);
	dm_rx_st = readl_relaxed(gi3c->se.base + SE_DMA_RX_IRQ_STAT);
	dma = readl_relaxed(gi3c->se.base + SE_GENI_DMA_MODE_EN);

	if ((m_stat & SE_I3C_ERR) || (dm_rx_st & DM_I3C_CB_ERR)) {
		geni_i3c_handle_err(gi3c, m_stat);

		/* Disable the TX Watermark interrupt to stop TX */
		if (!dma)
			writel_relaxed(0, gi3c->se.base + SE_GENI_TX_WATERMARK_REG);
		goto irqret;
	}

	if (dma) {
		I3C_LOG_DBG(gi3c->ipcl, false, gi3c->se.dev,
			    "i3c dma tx:0x%x, dma rx:0x%x\n", dm_tx_st, dm_rx_st);
		goto irqret;
	}

	if ((m_stat & (M_RX_FIFO_WATERMARK_EN | M_RX_FIFO_LAST_EN)) &&
	    gi3c->cur_rnw == READ_TRANSACTION && gi3c->cur_buf) {
		u32 rxcnt = rx_st & RX_FIFO_WC_MSK;

		for (j = 0; j < rxcnt; j++) {
			u32 val;
			int p = 0;

			val = readl_relaxed(gi3c->se.base + SE_GENI_RX_FIFOn);
			while (gi3c->cur_idx < gi3c->cur_len && p < sizeof(val)) {
				gi3c->cur_buf[gi3c->cur_idx++] = val & 0xff;
				val >>= 8;
				p++;
			}
			if (gi3c->cur_idx == gi3c->cur_len)
				break;
		}
	} else if ((m_stat & M_TX_FIFO_WATERMARK_EN) &&
		(gi3c->cur_rnw == WRITE_TRANSACTION) && (gi3c->cur_buf)) {
		for (j = 0; j < gi3c->tx_wm; j++) {
			u32 temp;
			u32 val = 0;
			int p = 0;

			while (gi3c->cur_idx < gi3c->cur_len && p < sizeof(val)) {
				temp = gi3c->cur_buf[gi3c->cur_idx++];
				val |= temp << (p * 8);
				p++;
			}
			writel_relaxed(val, gi3c->se.base + SE_GENI_TX_FIFOn);
			if (gi3c->cur_idx == gi3c->cur_len) {
				writel_relaxed(0, gi3c->se.base + SE_GENI_TX_WATERMARK_REG);
				break;
			}
		}
	}
irqret:
	if (m_stat)
		writel_relaxed(m_stat, gi3c->se.base + SE_GENI_M_IRQ_CLEAR);

	if (dma) {
		if (dm_tx_st)
			writel_relaxed(dm_tx_st, gi3c->se.base + SE_DMA_TX_IRQ_CLR);
		if (dm_rx_st)
			writel_relaxed(dm_rx_st, gi3c->se.base + SE_DMA_RX_IRQ_CLR);
	}
	/* if this is err with done-bit not set, handle that through timeout. */
	if (m_stat & M_CMD_DONE_EN || m_stat & M_CMD_ABORT_EN) {
		writel_relaxed(0, gi3c->se.base + SE_GENI_TX_WATERMARK_REG);
		complete(&gi3c->done);
	} else if ((dm_tx_st & TX_DMA_DONE) ||
		(dm_rx_st & RX_DMA_DONE) ||
		(dm_rx_st & RX_RESET_DONE) ||
		(dm_tx_st & TX_RESET_DONE)) {
		I3C_LOG_DBG(gi3c->ipcl, false, gi3c->se.dev,
			    "%s: DMA mode xfer completed\n", __func__);
		complete(&gi3c->done);
	}

	spin_unlock_irqrestore(&gi3c->spinlock, flags);
	return IRQ_HANDLED;
}

static int i3c_geni_runtime_get_mutex_lock(struct geni_i3c_dev *gi3c)
{
	int ret;

	mutex_lock(&gi3c->lock);

	reinit_completion(&gi3c->done);
	if (!pm_runtime_enabled(gi3c->se.dev))
		I3C_LOG_ERR(gi3c->ipcl, true, gi3c->se.dev, "PM runtime disabled\n");

	ret = pm_runtime_get_sync(gi3c->se.dev);
	if (ret < 0) {
		I3C_LOG_ERR(gi3c->ipcl, true, gi3c->se.dev,
			    "error turning on SE resources:%d\n", ret);
		pm_runtime_put_noidle(gi3c->se.dev);
		/* Set device in suspended since resume failed */
		pm_runtime_set_suspended(gi3c->se.dev);

		mutex_unlock(&gi3c->lock);
		return ret;
	}

	return 0; /* return 0 to indicate SUCCESS */
}

static void i3c_geni_runtime_put_mutex_unlock(struct geni_i3c_dev *gi3c)
{
	pm_runtime_mark_last_busy(gi3c->se.dev);
	pm_runtime_put_autosuspend(gi3c->se.dev);
	mutex_unlock(&gi3c->lock);
}

/*
 * i3c_geni_gsi_multi_write() - Does gsi multiple writes using multiple tre's for i3c tx messages
 *
 * @gi3c: i3c master device handle
 * @xfer: i3c tx transfer parameters pointer
 * @priv_xfers: priv xfers handle
 * @num_xfers: number of xfers
 *
 * Return: 0 on success, error code on failure
 */
static int
i3c_geni_gsi_multi_write(struct geni_i3c_dev *gi3c, struct geni_i3c_xfer_params *xfer,
			 struct i3c_priv_xfer *priv_xfers, int num_xfers)
{
	struct gsi_tre_queue *tx_tre_q = &gi3c->gsi.tx.tre_queue;
	int xfer_tx_idx = xfer->tx_idx % GSI_MAX_NUM_TRE_MSGS;

	gi3c->cur_rnw = WRITE_TRANSACTION;
	tx_tre_q->virt_buf[xfer_tx_idx] = (u8 *)priv_xfers[xfer->tx_idx].data.out;
	tx_tre_q->len[xfer_tx_idx] = priv_xfers[xfer->tx_idx].len;
	return geni_i3c_gsi_multi_write(gi3c, xfer, num_xfers);
}

/*
 * i3c_geni_execute_read_command() - Does i3c read for fifo and gsi modes
 *
 * @gi3c: i3c master device handle
 * @xfer: i3c rx transfer parameters pointer
 * @buf: read buffer pointer
 * @len: read data length
 *
 * Return: 0 on success, error code on failure
 */
static int
i3c_geni_execute_read_command(struct geni_i3c_dev *gi3c, struct geni_i3c_xfer_params *xfer,
			      u8 *buf, u32 len)
{
	gi3c->cur_rnw = READ_TRANSACTION;
	gi3c->cur_buf = buf;
	gi3c->cur_len = len;
	if (gi3c->se_mode == GENI_GPI_DMA)
		return geni_i3c_gsi_read(gi3c, xfer);
	else
		return geni_i3c_fifo_dma_xfer(gi3c, xfer);
}

/*
 * i3c_geni_execute_write_command() - Does i3c write for fifo and gsi modes
 *
 * @gi3c: i3c master device handle
 * @xfer: i3c tx transfer parameters pointer
 * @buf: write buffer pointer
 * @len: write data length
 *
 * Return: 0 on success, error code on failure
 */
static int
i3c_geni_execute_write_command(struct geni_i3c_dev *gi3c, struct geni_i3c_xfer_params *xfer,
			       u8 *buf, u32 len)
{
	gi3c->cur_rnw = WRITE_TRANSACTION;
	gi3c->cur_buf = buf;
	gi3c->cur_len = len;
	if (gi3c->se_mode == GENI_GPI_DMA)
		return geni_i3c_gsi_write(gi3c, xfer);
	else
		return geni_i3c_fifo_dma_xfer(gi3c, xfer);
}

/*
 * geni_i3c_master_gsi_priv_xfers() - Does i3c master gsi private transfers
 *
 * @gi3c: i3c master device handle
 * @xfer: i3c private transfer handle
 * @dyn_addr: dynamic address of the slave
 * @num_xfers: number of xfers
 *
 * Return: 0 on success, error code on failure
 */
static int
geni_i3c_master_gsi_priv_xfers(struct geni_i3c_dev *gi3c, struct i3c_priv_xfer *xfers,
			       u8 dyn_addr, int num_xfers)
{
	struct gsi_tre_queue *tx_tre_q = &gi3c->gsi.tx.tre_queue;
	struct geni_i3c_xfer_params xfer;
	bool use_7e = true, stall = false, multi_tre_wr_xfer = false;
	int i, ret = 0;
	unsigned long long start_time = sched_clock();

	if (num_xfers >= 4) {
		/*
		 * Do multi tre xfer write only when there are
		 * consecutive write transactions greater than four
		 */
		multi_tre_wr_xfer = true;
		for (i = 0; i < num_xfers; i++)
			if (xfers[i].rnw)
				multi_tre_wr_xfer = false;
	}

	tx_tre_q->unmap_msg_cnt = 0;
	atomic_set(&tx_tre_q->irq_cnt, 0);
	tx_tre_q->msg_cnt = 0;
	tx_tre_q->unmap_msg_cnt = 0;
	tx_tre_q->freed_msg_cnt = 0;

	for (i = 0; i < num_xfers; i++) {
		stall = (i < (num_xfers - 1));

		xfer.m_param  = (stall ? STOP_STRETCH : 0);
		xfer.m_param |= ((dyn_addr & I3C_ADDR_MASK) << SLV_ADDR_SHFT);
		xfer.m_param |= (use_7e) ? USE_7E : 0;
		xfer.tx_idx = i;

		I3C_LOG_ERR(gi3c->ipcl, false, gi3c->se.dev,
			    "%s: stall:%d,use_7e:%d, num_xfers:%d,i:%d,m_param:0x%x,rnw:%d\n",
			    __func__, stall, use_7e, num_xfers, i, xfer.m_param, xfers[i].rnw);

		/* Update use_7e status for next loop iteration */
		use_7e = !stall;

		if (xfers[i].rnw) {
			xfer.m_cmd = I3C_PRIVATE_READ;
			ret = i3c_geni_execute_read_command(gi3c, &xfer, (u8 *)xfers[i].data.in,
							    xfers[i].len);
		} else {
			xfer.m_cmd = I3C_PRIVATE_WRITE;
			if (multi_tre_wr_xfer)
				ret = i3c_geni_gsi_multi_write(gi3c, &xfer, xfers, num_xfers);
			else
				ret = i3c_geni_execute_write_command(gi3c, &xfer,
								     (u8 *)xfers[i].data.out,
								     xfers[i].len);
		}

		if (ret)
			break;
	}

	I3C_LOG_DBG(gi3c->ipcl, false, gi3c->se.dev, "%s Time took for %d xfers = %llu nsecs\n",
		    __func__, num_xfers, (sched_clock() - start_time));
	geni_i3c_gsi_stop_on_bus(gi3c);
	return ret;
}

/*
 * geni_i3c_master_fifo_dma_priv_xfers() - Does i3c master fifo and dma private transfers
 *
 * @gi3c: i3c master device handle
 * @xfer: i3c private transfer handle
 * @dyn_addr: dynamic address of the slave
 * @num_xfers: number of xfers
 *
 * Return: 0 on success, error code on failure
 */
static int
geni_i3c_master_fifo_dma_priv_xfers(struct geni_i3c_dev *gi3c, struct i3c_priv_xfer *xfers,
				    u8 dyn_addr, int num_xfers)
{
	struct geni_i3c_xfer_params xfer;
	bool use_7e = true, stall = false;
	int i, ret = 0;

	for (i = 0; i < num_xfers; i++) {
		stall = (i < (num_xfers - 1));

		xfer.mode = xfers[i].len > 64 ? GENI_SE_DMA : GENI_SE_FIFO;
		xfer.m_param  = (stall ? STOP_STRETCH : 0);
		xfer.m_param |= ((dyn_addr & I3C_ADDR_MASK) << SLV_ADDR_SHFT);
		xfer.m_param |= (use_7e) ? USE_7E : 0;

		I3C_LOG_DBG(gi3c->ipcl, false, gi3c->se.dev,
			    "%s: stall:%d,use_7e:%d, num_xfers:%d,i:%d,m_param:0x%x,rnw:%d\n",
			    __func__, stall, use_7e, num_xfers, i, xfer.m_param, xfers[i].rnw);

		/* Update use_7e status for next loop iteration */
		use_7e = !stall;

		if (xfers[i].rnw) {
			xfer.m_cmd = I3C_PRIVATE_READ;
			ret = i3c_geni_execute_read_command(gi3c, &xfer, (u8 *)xfers[i].data.in,
							    xfers[i].len);
		} else {
			xfer.m_cmd = I3C_PRIVATE_WRITE;
			ret = i3c_geni_execute_write_command(gi3c, &xfer, (u8 *)xfers[i].data.out,
							     xfers[i].len);
		}
		if (ret)
			break;
	}
	return ret;
}

/*
 * geni_i3c_master_priv_xfers() - Does i3c master private transfers
 *
 * @gi3c: i3c master device handle
 * @xfer: i3c private transfer handle
 * @num_xfers: number of xfers
 *
 * Return: 0 on success, error code on failure
 */
static int
geni_i3c_master_priv_xfers(struct i3c_dev_desc *dev, struct i3c_priv_xfer *xfers, int num_xfers)
{
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct geni_i3c_dev *gi3c = to_geni_i3c_master(m);
	int ret;
	u32 geni_ios;

	I3C_LOG_DBG(gi3c->ipcl, false, gi3c->se.dev, "Enter %s num_xfer=%d\n", __func__, num_xfers);
	if (num_xfers <= 0)
		return 0;

	ret = i3c_geni_runtime_get_mutex_lock(gi3c);
	if (ret)
		return ret;

	geni_ios = geni_read_reg(gi3c->se.base, SE_GENI_IOS);
	if ((geni_ios & 0x3) != 0x3) //SCL:b'1, SDA:b'0
		I3C_LOG_DBG(gi3c->ipcl, false, gi3c->se.dev,
			    "%s:IO lines:0x%x not in good state\n", __func__, geni_ios);

	qcom_geni_i3c_conf(gi3c, PUSH_PULL_MODE);

	if (gi3c->se_mode == GENI_GPI_DMA)
		ret = geni_i3c_master_gsi_priv_xfers(gi3c, xfers, dev->info.dyn_addr, num_xfers);
	else
		ret = geni_i3c_master_fifo_dma_priv_xfers(gi3c, xfers, dev->info.dyn_addr,
							  num_xfers);

	I3C_LOG_DBG(gi3c->ipcl, false, gi3c->se.dev, "%s ret:%d\n", __func__, ret);
	i3c_geni_runtime_put_mutex_unlock(gi3c);

	return ret;
}

/*
 * geni_i3c_master_i2c_xfers() - Does i3c master i2c transfers
 *
 * @dev: i2c device handle
 * @msgs: i2c message pointer
 * @num: number of xfers
 *
 * Return: 0 on success, error code on failure
 */
static int geni_i3c_master_i2c_xfers(struct i2c_dev_desc *dev, const struct i2c_msg *msgs, int num)
{
	struct i3c_master_controller *m = i2c_dev_get_master(dev);
	struct geni_i3c_dev *gi3c = to_geni_i3c_master(m);
	struct geni_i3c_xfer_params xfer;
	int i, ret;

	I3C_LOG_DBG(gi3c->ipcl, false, gi3c->se.dev, "Enter %s num xfers=%d\n", __func__, num);
	if (!msgs) {
		I3C_LOG_ERR(gi3c->ipcl, false, gi3c->se.dev, "%s: client msg is NULL\n", __func__);
		return 0;
	}

	ret = i3c_geni_runtime_get_mutex_lock(gi3c);
	if (ret)
		return ret;

	qcom_geni_i3c_conf(gi3c, PUSH_PULL_MODE);

	I3C_LOG_DBG(gi3c->ipcl, false, gi3c->se.dev, "i2c xfer:num:%d, msgs:len:%d,flg:%d\n",
		    num, msgs[0].len, msgs[0].flags);

	for (i = 0; i < num; i++) {
		xfer.m_cmd    = (msgs[i].flags & I2C_M_RD) ? I2C_READ : I2C_WRITE;
		xfer.m_param  = (i < (num - 1)) ? STOP_STRETCH : 0;
		xfer.m_param |= ((msgs[i].addr & I3C_ADDR_MASK) << SLV_ADDR_SHFT);
		xfer.mode     = msgs[i].len > 32 ? GENI_SE_DMA : GENI_SE_FIFO;
		if (msgs[i].flags & I2C_M_RD)
			ret = i3c_geni_execute_read_command(gi3c, &xfer, msgs[i].buf, msgs[i].len);
		else
			ret = i3c_geni_execute_write_command(gi3c, &xfer, msgs[i].buf, msgs[i].len);
		if (ret)
			break;
	}

	I3C_LOG_DBG(gi3c->ipcl, false, gi3c->se.dev, "i2c: txn ret:%d\n", ret);

	i3c_geni_runtime_put_mutex_unlock(gi3c);

	return ret;
}

/*
 * geni_i3c_perform_daa() - peforms i3c dynamic address assigning
 *
 * @gi3c: i3c master device handle
 *
 * Return: None
 */
static void geni_i3c_perform_daa(struct geni_i3c_dev *gi3c)
{
	struct i3c_master_controller *m = &gi3c->ctrlr;
	int ret;
	u8 *rx_buf, *tx_buf;

	rx_buf = kzalloc(8, GFP_DMA);
	if (!rx_buf) {
		I3C_LOG_ERR(gi3c->ipcl, true, gi3c->se.dev, "%s: rx no memory\n", __func__);
		return;
	}

	tx_buf = kzalloc(8, GFP_DMA);
	if (!tx_buf) {
		kfree(rx_buf);
		I3C_LOG_ERR(gi3c->ipcl, true, gi3c->se.dev, "%s: tx no memory\n", __func__);
		return;
	}

	while (1) {
		struct geni_i3c_xfer_params xfer = { GENI_SE_FIFO };
		struct i3c_dev_boardinfo *i3cboardinfo = NULL;
		struct i3c_dev_desc *i3cdev = NULL;
		u64 pid;
		u16 mid;
		u8 bcr, dcr, init_dyn_addr = 0, addr = 0;
		bool enum_slv = false;

		I3C_LOG_DBG(gi3c->ipcl, false, gi3c->se.dev, "i3c entdaa read\n");
		memset(rx_buf, 0, 8);
		memset(tx_buf, 0, 8);

		xfer.m_cmd = I2C_READ;
		xfer.m_param = STOP_STRETCH | CONTINUOUS_MODE_DAA | USE_7E | IBI_NACK_TBL_CTRL;

		ret = i3c_geni_execute_read_command(gi3c, &xfer, rx_buf, 8);
		if (ret)
			break;

		dcr = rx_buf[7];
		bcr = rx_buf[6];
		pid = ((u64)rx_buf[0] << 40) | ((u64)rx_buf[1] << 32) | ((u64)rx_buf[2] << 24) |
			((u64)rx_buf[3] << 16) | ((u64)rx_buf[4] <<  8) | ((u64)rx_buf[5]);
		mid = ((u16)rx_buf[0] << 8) | ((u16)rx_buf[1]);

		list_for_each_entry(i3cboardinfo, &m->boardinfo.i3c, node) {
			if (pid == i3cboardinfo->pid) {
				I3C_LOG_DBG(gi3c->ipcl, false, gi3c->se.dev,
					    "PID 0x:%x matched with boardinfo\n", pid);
				break;
			}
		}

		if (!i3cboardinfo) {
			I3C_LOG_DBG(gi3c->ipcl, false, gi3c->se.dev, "Invalid i3cboardinfo\n");
			goto daa_err;
		}

		/* If DA is specified in DTSI, use it */
		if (i3cboardinfo->init_dyn_addr && i3cboardinfo->init_dyn_addr < I3C_MAX_ADDR)
			addr = init_dyn_addr = i3cboardinfo->init_dyn_addr;

		addr = ret = i3c_master_get_free_addr(m, addr);

		if (ret < 0) {
			I3C_LOG_DBG(gi3c->ipcl, false, gi3c->se.dev,
				    "error:%d during get_free_addr, pid:0x:%x, mid:0x%x\n",
				    ret, pid, mid);
			goto daa_err;
		} else if (ret == init_dyn_addr) {
			I3C_LOG_DBG(gi3c->ipcl, false, gi3c->se.dev,
				    "assign requested addr:0x%x for pid:0x:%x, mid:0x%x\n",
				    ret, pid, mid);
		} else if (init_dyn_addr) {
			i3c_bus_for_each_i3cdev(&m->bus, i3cdev) {
				if (i3cdev->info.pid == pid) {
					enum_slv = true;
					break;
				}
			}
			if (enum_slv) {
				addr = i3cdev->info.dyn_addr;
				I3C_LOG_DBG(gi3c->ipcl, false, gi3c->se.dev,
					    "assigning requested addr:0x%x for pid:0x:%x\n",
					    addr, pid);
			} else {
				I3C_LOG_DBG(gi3c->ipcl, false, gi3c->se.dev,
					    "new dev: assigning addr:0x%x for pid:x:%x\n",
					    ret, pid);
			}
		} else {
			I3C_LOG_DBG(gi3c->ipcl, false, gi3c->se.dev,
				    "assigning addr:0x%x for pid:x:%x\n", ret, pid);
		}

		if (!i3cboardinfo->init_dyn_addr)
			i3cboardinfo->init_dyn_addr = addr;

		if (!enum_slv)
			set_new_addr_slot(gi3c->newaddrslots, addr);

		tx_buf[0] = (addr & I3C_ADDR_MASK) << 1;
		tx_buf[0] |= ~(hweight8(addr & I3C_ADDR_MASK) & 1);

		/* calculate crc */
		if (tx_buf[0]) {
			u32 slaveid = addr;
			u32 ret = slaveid & 1u;
			u32 final = 0;

			while (slaveid) {
				slaveid >>= 1;
				ret = ret ^ (slaveid & 1u);
			}

			ret = ret ^ 1u;
			final = (addr << 1) | ret;
			tx_buf[0] = final;
		}

		xfer.m_cmd = I2C_WRITE;
		xfer.m_param = STOP_STRETCH | BYPASS_ADDR_PHASE | IBI_NACK_TBL_CTRL;

		ret = i3c_geni_execute_write_command(gi3c, &xfer, tx_buf, 1);
		if (ret)
			break;
	}
daa_err:
	kfree(tx_buf);
	kfree(rx_buf);
}

/*
 * geni_i3c_gsi_stop_on_bus() - Does gsi i3c stop command on the bus
 *
 * @gi3c: i3c master device handle
 *
 * Return: 0 on success, error code on failure
 */
static int geni_i3c_gsi_stop_on_bus(struct geni_i3c_dev *gi3c)
{
	struct msm_gpi_tre *go_t = &gi3c->gsi.tx.tre.go_t;
	int tre_cnt = 0, ret = 0, time_remaining = 0;
	bool tx_chan = true;

	gi3c->err = 0;
	gi3c->gsi_err = false;
	gi3c->gsi.tx.tre.flags = 0;
	reinit_completion(&gi3c->done);

	go_t->dword[0] = MSM_GPI_I3C_GO_TRE_DWORD0(0, 0, 0, I2C_STOP_ON_BUS);
	go_t->dword[1] = 0x0;
	go_t->dword[2] = 0x0;
	go_t->dword[3] = MSM_GPI_I3C_GO_TRE_DWORD3(0, 0, 1, 0, 0);

	I3C_LOG_DBG(gi3c->ipcl, false, gi3c->se.dev,
		    "%s: dword[0]:0x%x dword[1]:0x%x dword[2]:0x%x dword[3]:0x%x\n",
		    __func__, go_t->dword[0], go_t->dword[1], go_t->dword[2], go_t->dword[3]);

	gi3c->gsi.tx.tre.flags |= GO_TRE_SET;
	tre_cnt = gsi_common_fill_tre_buf(&gi3c->gsi, tx_chan);
	ret = gsi_common_prep_desc_and_submit(&gi3c->gsi,  tre_cnt, tx_chan, false);
	if (ret < 0)
		gi3c->err = ret;

	time_remaining = wait_for_completion_timeout(&gi3c->done, XFER_TIMEOUT);
	if (!time_remaining) {
		I3C_LOG_ERR(gi3c->ipcl, false, gi3c->se.dev,
			    "%s:wait_for_completion timed out\n", __func__);
		geni_i3c_err(gi3c, GENI_TIMEOUT);
		gi3c->cur_buf = NULL;
		gi3c->cur_len = 0;
		gi3c->cur_idx = 0;
		gi3c->cur_rnw = 0;
		reinit_completion(&gi3c->done);
	}

	I3C_LOG_DBG(gi3c->ipcl, false, gi3c->se.dev, "%s: ret:%d\n", __func__, ret);
	return ret;
}

/*
 * geni_i3c_master_send_ccc_cmd() - Does i3c master send ccc commands
 *
 * @m: i3c master controller handle
 * @cmd: ccc command handle
 *
 * Return: 0 on success, error code on failure
 */
static int geni_i3c_master_send_ccc_cmd(struct i3c_master_controller *m, struct i3c_ccc_cmd *cmd)
{
	struct geni_i3c_dev *gi3c = to_geni_i3c_master(m);
	int i, ret;

	if (!(cmd->id & I3C_CCC_DIRECT) && (cmd->ndests != 1))
		return -EINVAL;

	ret = i3c_geni_runtime_get_mutex_lock(gi3c);
	if (ret)
		return ret;

	qcom_geni_i3c_conf(gi3c, OPEN_DRAIN_MODE);

	for (i = 0; i < cmd->ndests; i++) {
		int stall = (i < (cmd->ndests - 1)) ||
			(cmd->id == I3C_CCC_ENTDAA);
		struct geni_i3c_xfer_params xfer = { GENI_SE_FIFO };

		xfer.m_param  = (stall ? STOP_STRETCH : 0);
		xfer.m_param |= (cmd->id << CCC_HDR_CMD_SHFT);
		xfer.m_param |= IBI_NACK_TBL_CTRL;
		if (cmd->id & I3C_CCC_DIRECT) {
			xfer.m_param |= ((cmd->dests[i].addr & I3C_ADDR_MASK)
					<< SLV_ADDR_SHFT);
			if (cmd->rnw) {
				if (i == 0)
					xfer.m_cmd = I3C_DIRECT_CCC_READ;
				else
					xfer.m_cmd = I3C_PRIVATE_READ;
			} else {
				if (i == 0)
					xfer.m_cmd =
					   (cmd->dests[i].payload.len > 0) ?
						I3C_DIRECT_CCC_WRITE :
						I3C_DIRECT_CCC_ADDR_ONLY;
				else
					xfer.m_cmd = I3C_PRIVATE_WRITE;
			}
		} else {
			if (cmd->dests[i].payload.len > 0)
				xfer.m_cmd = I3C_BCAST_CCC_WRITE;
			else
				xfer.m_cmd = I3C_BCAST_CCC_ADDR_ONLY;
		}

		if (i == 0)
			xfer.m_param |= USE_7E;

		if (cmd->rnw)
			ret = i3c_geni_execute_read_command(gi3c, &xfer,
							    cmd->dests[i].payload.data,
							    cmd->dests[i].payload.len);
		else
			ret = i3c_geni_execute_write_command(gi3c, &xfer,
							     cmd->dests[i].payload.data,
							     cmd->dests[i].payload.len);
		if (ret)
			break;

		if (cmd->id == I3C_CCC_ENTDAA)
			geni_i3c_perform_daa(gi3c);
	}

	I3C_LOG_DBG(gi3c->ipcl, false, gi3c->se.dev, "i3c ccc: txn ret:%d\n", ret);

	i3c_geni_runtime_put_mutex_unlock(gi3c);

	return ret;
}

static int geni_i3c_master_attach_i2c_dev(struct i2c_dev_desc *dev)
{
	struct i3c_master_controller *m = i2c_dev_get_master(dev);
	struct geni_i3c_dev *gi3c = to_geni_i3c_master(m);
	struct geni_i3c_i2c_dev_data *data;

	data = devm_kzalloc(gi3c->se.dev, sizeof(*data), GFP_KERNEL);
	if (!data) {
		I3C_LOG_DBG(gi3c->ipcl, false, gi3c->se.dev, "%s alloc fail return\n", __func__);
		return -ENOMEM;
	}

	I3C_LOG_DBG(gi3c->ipcl, false, gi3c->se.dev, "%s %d\n", __func__, true);
	i2c_dev_set_master_data(dev, data);

	return 0;
}

static void geni_i3c_master_detach_i2c_dev(struct i2c_dev_desc *dev)
{
	i2c_dev_set_master_data(dev, NULL);
}

static int geni_i3c_master_attach_i3c_dev(struct i3c_dev_desc *dev)
{
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct geni_i3c_dev *gi3c = to_geni_i3c_master(m);
	struct geni_i3c_i2c_dev_data *data;
	struct i3c_dev_boardinfo *i3cboardinfo;

	data = devm_kzalloc(gi3c->se.dev, sizeof(*data), GFP_KERNEL);
	if (!data) {
		I3C_LOG_DBG(gi3c->ipcl, false, gi3c->se.dev, "%s alloc fail return\n", __func__);
		return -ENOMEM;
	}

	data->ibi = -1;
	i3c_dev_set_master_data(dev, data);
	if (!dev->boardinfo) {
		list_for_each_entry(i3cboardinfo, &m->boardinfo.i3c, node) {
			if (dev->info.pid == i3cboardinfo->pid)
				dev->boardinfo = i3cboardinfo;
		}
	}

	I3C_LOG_DBG(gi3c->ipcl, false, gi3c->se.dev, "%s %d\n", __func__, true);
	return 0;
}

static int geni_i3c_master_reattach_i3c_dev
(
	struct i3c_dev_desc *dev,
	u8 old_dyn_addr
)
{
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct i3c_dev_boardinfo *i3cboardinfo;

	if (!dev->boardinfo) {
		list_for_each_entry(i3cboardinfo, &m->boardinfo.i3c, node) {
			if (dev->info.pid == i3cboardinfo->pid)
				dev->boardinfo = i3cboardinfo;
		}
	}

	return 0;
}

static void geni_i3c_master_detach_i3c_dev(struct i3c_dev_desc *dev)
{
	i3c_dev_set_master_data(dev, NULL);
}

static int geni_i3c_master_entdaa_locked(struct geni_i3c_dev *gi3c)
{
	struct i3c_master_controller *m = &gi3c->ctrlr;
	u8 addr;
	int ret;

	ret = i3c_master_entdaa_locked(m);
	if (ret && ret != I3C_ERROR_M2)
		return ret;

	for (addr = 0; addr <= I3C_ADDR_MASK; addr++) {
		if (is_new_addr_slot_set(gi3c->newaddrslots, addr)) {
			clear_new_addr_slot(gi3c->newaddrslots, addr);
			i3c_master_add_i3c_dev_locked(m, addr);
		}
	}

	i3c_master_enec_locked(m, I3C_BROADCAST_ADDR,
				      I3C_CCC_EVENT_MR |
				      I3C_CCC_EVENT_HJ);

	return 0;
}

static int geni_i3c_master_do_daa(struct i3c_master_controller *m)
{
	struct geni_i3c_dev *gi3c = to_geni_i3c_master(m);

	return geni_i3c_master_entdaa_locked(gi3c);
}

static int geni_i3c_master_bus_init(struct i3c_master_controller *m)
{
	struct geni_i3c_dev *gi3c = to_geni_i3c_master(m);
	struct i3c_bus *bus = i3c_master_get_bus(m);
	struct i3c_device_info info = { };
	int ret;

	ret = pm_runtime_get_sync(gi3c->se.dev);
	if (ret < 0) {
		I3C_LOG_ERR(gi3c->ipcl, true, gi3c->se.dev,
			"%s: error turning SE resources:%d\n", __func__, ret);
		pm_runtime_put_noidle(gi3c->se.dev);
		/* Set device in suspended since resume failed */
		pm_runtime_set_suspended(gi3c->se.dev);
		return ret;
	}

	ret = geni_i3c_clk_map_idx(gi3c);
	if (ret) {
		I3C_LOG_ERR(gi3c->ipcl, true, gi3c->se.dev,
			"Invalid clk frequency %d Hz src or %ld Hz bus: %d\n",
			gi3c->clk_src_freq, bus->scl_rate.i3c,
			ret);
		goto err_cleanup;
	}

	qcom_geni_i3c_conf(gi3c, OPEN_DRAIN_MODE);

	/* Get an address for the master. */
	ret = i3c_master_get_free_addr(m, 0);
	if (ret < 0) {
		I3C_LOG_ERR(gi3c->ipcl, true, gi3c->se.dev,
				"%s: error No free addr:%d\n", __func__, ret);
		goto err_cleanup;
	}

	info.dyn_addr = ret;
	info.dcr = I3C_DCR_GENERIC_DEVICE;
	info.bcr = I3C_BCR_I3C_MASTER | I3C_BCR_HDR_CAP;
	info.pid = 0;

	ret = i3c_master_set_info(&gi3c->ctrlr, &info);

err_cleanup:
	/*As framework calls multiple exposed API's after this API, we cannot
	 *use mutex protected internal put/get sync API. Hence forcefully
	 *disabling clocks and decrementing usage count.
	 */
	disable_irq(gi3c->irq);
	ret = geni_se_resources_off(&gi3c->se);
	if (ret)
		I3C_LOG_ERR(gi3c->ipcl, true, gi3c->se.dev,
			"%s: geni_se_resources_off failed%d\n", __func__, ret);
	ret = geni_icc_disable(&gi3c->se);
	if (ret)
		I3C_LOG_ERR(gi3c->ipcl, true, gi3c->se.dev,
			"%s: geni_icc_disable failed%d\n", __func__, ret);
	pm_runtime_disable(gi3c->se.dev);
	pm_runtime_put_noidle(gi3c->se.dev);
	pm_runtime_set_suspended(gi3c->se.dev);
	pm_runtime_enable(gi3c->se.dev);

	return ret;
}

static void geni_i3c_master_bus_cleanup(struct i3c_master_controller *m)
{
}

static bool geni_i3c_master_supports_ccc_cmd
(
	struct i3c_master_controller *m,
	const struct i3c_ccc_cmd *cmd
)
{
	struct geni_i3c_dev *gi3c = to_geni_i3c_master(m);

	I3C_LOG_DBG(gi3c->ipcl, false, gi3c->se.dev, "Enter %s cmd->id:0x%x\n", __func__, cmd->id);

	switch (cmd->id) {
	case I3C_CCC_ENEC(true):
	fallthrough;
	case I3C_CCC_ENEC(false):
	fallthrough;
	case I3C_CCC_DISEC(true):
	fallthrough;
	case I3C_CCC_DISEC(false):
	fallthrough;
	case I3C_CCC_ENTAS(0, true):
	fallthrough;
	case I3C_CCC_ENTAS(0, false):
	fallthrough;
	case I3C_CCC_RSTDAA(true):
	fallthrough;
	case I3C_CCC_RSTDAA(false):
	fallthrough;
	case I3C_CCC_ENTDAA:
	fallthrough;
	case I3C_CCC_SETMWL(true):
	fallthrough;
	case I3C_CCC_SETMWL(false):
	fallthrough;
	case I3C_CCC_SETMRL(true):
	fallthrough;
	case I3C_CCC_SETMRL(false):
	fallthrough;
	case I3C_CCC_DEFSLVS:
	fallthrough;
	case I3C_CCC_ENTHDR(0):
	fallthrough;
	case I3C_CCC_SETDASA:
	fallthrough;
	case I3C_CCC_SETNEWDA:
	fallthrough;
	case I3C_CCC_GETMWL:
	fallthrough;
	case I3C_CCC_GETMRL:
	fallthrough;
	case I3C_CCC_GETPID:
	fallthrough;
	case I3C_CCC_GETBCR:
	fallthrough;
	case I3C_CCC_GETDCR:
	fallthrough;
	case I3C_CCC_GETSTATUS:
	fallthrough;
	case I3C_CCC_GETACCMST:
	fallthrough;
	case I3C_CCC_GETMXDS:
	fallthrough;
	case I3C_CCC_GETHDRCAP:
		return true;
	default:
		break;
	}

	I3C_LOG_DBG(gi3c->ipcl, false, gi3c->se.dev, "%s: Unsupported cmd\n", __func__);
	return false;
}

static int geni_i3c_master_enable_ibi(struct i3c_dev_desc *dev)
{
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct geni_i3c_dev *gi3c = to_geni_i3c_master(m);
	int ret = 0;

	if (!gi3c->ibi.hw_support && !gi3c->ibi.is_init)
		return -EPERM;

	ret = i3c_master_enec_locked(m, dev->info.dyn_addr,
				      I3C_CCC_EVENT_SIR);
	if (ret)
		I3C_LOG_ERR(gi3c->ipcl, true, gi3c->se.dev,
			"%s: error while i3c_master_enec_locked\n", __func__);

	return ret;
}

static int geni_i3c_master_disable_ibi(struct i3c_dev_desc *dev)
{
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct geni_i3c_dev *gi3c = to_geni_i3c_master(m);
	int ret = 0;

	if (!gi3c->ibi.hw_support && !gi3c->ibi.is_init)
		return -EPERM;

	ret = i3c_master_disec_locked(m, dev->info.dyn_addr, I3C_CCC_EVENT_SIR);
	if (ret)
		I3C_LOG_ERR(gi3c->ipcl, true, gi3c->se.dev,
			"%s: error while i3c_master_disec_locked\n", __func__);

	return ret;
}

static void qcom_geni_i3c_ibi_conf(struct geni_i3c_dev *gi3c)
{
	gi3c->ibi.err = 0;
	reinit_completion(&gi3c->ibi.done);

	/* set the configuration for 100Khz OD speed */
	geni_write_reg(0x5FD74322, gi3c->ibi.ibi_base, IBI_SCL_PP_TIMING_CONFIG);


	/* Balance NAON Clock enable/disable between ibi_conf & ibi_unconf */
	if (gi3c->ibi.ibic_naon && !gi3c->ibi.naon_clk_en) {
		if (geni_i3c_enable_naon_ibi_clks(gi3c, true)) {
			I3C_LOG_ERR(gi3c->ipcl, true, gi3c->se.dev,
				"%s:  NAON clock failure\n", __func__);
			return;
		}
	}

	/* set the configuration for 100Khz OD speed */
	geni_write_reg(0x5FD74322, gi3c->ibi.ibi_base, IBI_SCL_PP_TIMING_CONFIG);

	geni_i3c_enable_ibi_ctrl(gi3c, true);
	geni_i3c_enable_ibi_irq(gi3c, true);
	gi3c->ibi.is_init = true;
}

static int geni_i3c_master_request_ibi(struct i3c_dev_desc *dev,
	const struct i3c_ibi_setup *req)
{
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct geni_i3c_dev *gi3c = to_geni_i3c_master(m);
	struct geni_i3c_i2c_dev_data *data = i3c_dev_get_master_data(dev);
	unsigned long i, flags;
	unsigned int payload_len = req->max_payload_len;

	if (!gi3c->ibi.hw_support)
		return -EPERM;

	if (!gi3c->ibi.is_init)
		qcom_geni_i3c_ibi_conf(gi3c);

	data->ibi_pool = i3c_generic_ibi_alloc_pool(dev, req);
	if (IS_ERR(data->ibi_pool)) {
		I3C_LOG_ERR(gi3c->ipcl, true, gi3c->se.dev,
			"Error creating a generic IBI pool %d\n",
			PTR_ERR(data->ibi_pool));
		return PTR_ERR(data->ibi_pool);
	}

	spin_lock_irqsave(&gi3c->ibi.lock, flags);
	for (i = 0; i < gi3c->ibi.num_slots; i++) {
		if (!gi3c->ibi.slots[i]) {
			data->ibi = i;
			gi3c->ibi.slots[i] = dev;
			break;
		}
	}
	spin_unlock_irqrestore(&gi3c->ibi.lock, flags);

	if (i < gi3c->ibi.num_slots) {
		u32 cmd, timeout;

		gi3c->ibi.err = 0;
		reinit_completion(&gi3c->ibi.done);

		cmd = ((dev->info.dyn_addr & I3C_SLAVE_MASK)
			<< I3C_SLAVE_ADDR_SHIFT) | I3C_SLAVE_RW | STALL;
		cmd |= ((payload_len << NUM_OF_MDB_SHIFT) & IBI_NUM_OF_MDB_MSK);
		geni_write_reg(cmd, gi3c->ibi.ibi_base, IBI_CMD(0));

		/* wait for adding slave IBI */
		timeout = wait_for_completion_timeout(&gi3c->ibi.done,
				XFER_TIMEOUT);
		if (!timeout) {
			gi3c->ibi.err = -ETIMEDOUT;
			I3C_LOG_ERR(gi3c->ipcl, true, gi3c->se.dev,
				"timeout while adding slave IBI\n");
		}

		if (!gi3c->ibi.err)
			return 0;

		I3C_LOG_ERR(gi3c->ipcl, true, gi3c->se.dev,
			"error while adding slave IBI 0x%x\n", gi3c->ibi.err);
	}

	I3C_LOG_ERR(gi3c->ipcl, true, gi3c->se.dev,
		"ibi.num_slots ran out %d: %d\n", i, gi3c->ibi.num_slots);

	i3c_generic_ibi_free_pool(data->ibi_pool);
	data->ibi_pool = NULL;

	return -ENOSPC;
}

static int qcom_deallocate_ibi_table_entry(struct geni_i3c_dev *gi3c)
{
	u32 i, timeout;

	for (i = 0; i < gi3c->ibi.num_slots; i++) {
		u32 entry;

		gi3c->ibi.err = 0;
		reinit_completion(&gi3c->ibi.done);

		entry = geni_read_reg(gi3c->ibi.ibi_base,
				IBI_CONFIG_ENTRY(0, i));

		/* if valid entry */
		if (entry & IBI_VALID) {
			/* send remove command */
			entry &= ~IBI_CMD_OPCODE;
			geni_write_reg(entry, gi3c->ibi.ibi_base, IBI_CMD(0));

			/* wait for removing slave IBI */
			timeout = wait_for_completion_timeout(&gi3c->ibi.done,
					XFER_TIMEOUT);
			if (!timeout) {
				I3C_LOG_ERR(gi3c->ipcl, true, gi3c->se.dev,
					"timeout while adding slave IBI\n");
				return -ETIMEDOUT;
			}
		}
	}

	return 0;
}

static void geni_i3c_enable_hotjoin_irq(struct geni_i3c_dev *gi3c, bool enable)
{
	u32 val;

	//Disable hot-join, until next probe happens
	val = geni_read_reg(gi3c->ibi.ibi_base, IBI_GEN_IRQ_EN);
	if (enable)
		val |= HOT_JOIN_IRQ_EN;
	else
		val &= ~HOT_JOIN_IRQ_EN;
	geni_write_reg(val, gi3c->ibi.ibi_base, IBI_GEN_IRQ_EN);

	I3C_LOG_ERR(gi3c->ipcl, true, gi3c->se.dev,
		"%s:%s\n", __func__, (enable) ? "Enabled" : "Disabled");
}

static void geni_i3c_enable_ibi_irq(struct geni_i3c_dev *gi3c, bool enable)
{
	u32 val;

	if (enable) {
		/* enable manager interrupts : HPG sec 4.1 */
		val = geni_read_reg(gi3c->ibi.ibi_base, IBI_GEN_IRQ_EN);
		val |= (val & 0x1B);
		geni_write_reg(val, gi3c->ibi.ibi_base, IBI_GEN_IRQ_EN);

		/* Enable GPII0 interrupts */
		geni_write_reg(GPIIn_IBI_EN(0), gi3c->ibi.ibi_base,
							IBI_GPII_IBI_EN);
		geni_write_reg(~0u, gi3c->ibi.ibi_base, IBI_IRQ_EN(0));
	} else {
		geni_write_reg(0, gi3c->ibi.ibi_base, IBI_GPII_IBI_EN);
		geni_write_reg(0, gi3c->ibi.ibi_base, IBI_IRQ_EN(0));
		geni_write_reg(0, gi3c->ibi.ibi_base, IBI_GEN_IRQ_EN);
	}
}

/*
 * geni_i3c_disable_free_running_clock() - fix free running clock
 *
 * @gi3c: i3c master device handle
 *
 * Return: None
 */
static void geni_i3c_disable_free_running_clock(struct geni_i3c_dev *gi3c)
{
	/*
	 * Currently implemented as SWA.
	 * Fix is present from qup-core version 4.0.0 onwards[major = 4, minor = 0].
	 * So below SWA is not applicable from qup-core version 4.0.0 onwards.
	 */
	if (gi3c->ver_info.hw_major_ver < 4) {
		I3C_LOG_DBG(gi3c->ipcl, false, gi3c->se.dev, "Force default\n");
		writel(FORCE_DEFAULT, gi3c->se.base + GENI_FORCE_DEFAULT_REG);
		writel(0x7f, gi3c->se.base + GENI_OUTPUT_CTRL);
	}
	gi3c->disable_free_run_clks = true;
}

static void geni_i3c_enable_ibi_ctrl(struct geni_i3c_dev *gi3c, bool enable)
{
	u32 val, timeout;

	if (enable) {
		reinit_completion(&gi3c->ibi.done);

		/* enable ENABLE_CHANGE */
		val = geni_read_reg(gi3c->ibi.ibi_base, IBI_GEN_IRQ_EN);
		val |= IBI_C_ENABLE;
		geni_write_reg(val, gi3c->ibi.ibi_base, IBI_GEN_IRQ_EN);

		/* Enable I3C IBI controller, if not in enabled state */
		val = geni_read_reg(gi3c->ibi.ibi_base, IBI_GEN_CONFIG);
		if (!(val & IBI_C_ENABLE)) {
			/* SW WAR for HW BUG - Execute only once */
			if (!gi3c->disable_free_run_clks)
				geni_i3c_disable_free_running_clock(gi3c);

			val |= IBI_C_ENABLE;
			geni_write_reg(val, gi3c->ibi.ibi_base, IBI_GEN_CONFIG);

			/* wait for ENABLE_CHANGE */
			timeout = wait_for_completion_timeout(&gi3c->ibi.done,
								XFER_TIMEOUT);
			if (!timeout) {
				I3C_LOG_ERR(gi3c->ipcl, true, gi3c->se.dev,
					"timeout while ENABLE_CHANGE bit\n");
				return;
			}
			I3C_LOG_ERR(gi3c->ipcl, true, gi3c->se.dev,
					"%s: IBI ctrl enabled\n", __func__);
		}
	} else {
		 /* Disable IBI controller */

		/* check if any IBI is enabled, if not then disable IBI ctrl */
		val = geni_read_reg(gi3c->ibi.ibi_base, IBI_GPII_IBI_EN);
		if (!val) {
			gi3c->ibi.err = 0;
			reinit_completion(&gi3c->ibi.done);

			val = geni_read_reg(gi3c->ibi.ibi_base, IBI_GEN_CONFIG);
			val &= ~IBI_C_ENABLE;
			geni_write_reg(val, gi3c->ibi.ibi_base, IBI_GEN_CONFIG);

			/* wait for ENABLE change */
			timeout = wait_for_completion_timeout(&gi3c->ibi.done,
					XFER_TIMEOUT);
			if (!timeout) {
				I3C_LOG_ERR(gi3c->ipcl, true, gi3c->se.dev,
				"timeout disabling IBI: 0x%x\n", gi3c->ibi.err);
				return;
			}
			I3C_LOG_ERR(gi3c->ipcl, true, gi3c->se.dev,
					"%s: IBI ctrl disabled\n",  __func__);
		}
	}
}

/**
 * geni_i3c_enable_naon_ibi_clks() - Enable/Disable clocks for NAON IBI ctrlr
 * @gi3c: I3C device handle
 * @clk_en: True if clks to be enabled, false to disable
 *
 * Call this function to enable/disable NAON based IBI controller as required.
 * Return: True OR respective failure code/value.
 */
static int geni_i3c_enable_naon_ibi_clks(struct geni_i3c_dev *gi3c, bool clk_en)
{
	int ret = 0;

	if (!gi3c->ibi.ibic_naon)
		return -EINVAL;

	/* if naon clocks are disabled, then only enable all these clocks */
	if (clk_en)
		clk_en = (!gi3c->ibi.naon_clk_en) ? true : false;

	if (clk_en) {
		ret = clk_prepare_enable(gi3c->ibi.core_clk);
		if (ret) {
			I3C_LOG_ERR(gi3c->ipcl, true, gi3c->se.dev,
				"%s failed at NAON core clk enable ret=%d\n",
				__func__, ret);
			return ret;
		}

		ret = clk_prepare_enable(gi3c->ibi.ahb_clk);
		if (ret) {
			I3C_LOG_ERR(gi3c->ipcl, true, gi3c->se.dev,
				"%s failed at NAON ahb clk enable ret=%d\n",
				__func__, ret);
			clk_disable_unprepare(gi3c->ibi.core_clk);
			return ret;
		}

		ret = clk_prepare_enable(gi3c->ibi.src_clk);
		if (ret) {
			I3C_LOG_ERR(gi3c->ipcl, true, gi3c->se.dev,
				"%s failed at NAON src clk enable ret=%d\n",
				__func__, ret);
			clk_disable_unprepare(gi3c->ibi.core_clk);
			clk_disable_unprepare(gi3c->ibi.ahb_clk);
			return ret;
		}
		gi3c->ibi.naon_clk_en = true;
		I3C_LOG_DBG(gi3c->ipcl, false, gi3c->se.dev,
			"%s: Enable Clock success\n", __func__);
	} else {
		clk_disable_unprepare(gi3c->ibi.core_clk);
		clk_disable_unprepare(gi3c->ibi.ahb_clk);
		clk_disable_unprepare(gi3c->ibi.src_clk);
		gi3c->ibi.naon_clk_en = false;
		I3C_LOG_DBG(gi3c->ipcl, false, gi3c->se.dev,
			 "%s: Disable clock success\n", __func__);
	}

	return ret;
}

static void qcom_geni_i3c_ibi_unconf(struct geni_i3c_dev *gi3c)
{
	u32 val;
	int ret = 0;

	val = geni_read_reg(gi3c->ibi.ibi_base, IBI_ALLOCATED_ENTRIES_GPII(0));
	if (val) {
		ret = qcom_deallocate_ibi_table_entry(gi3c);
		if (ret)
			return;
	}

	geni_i3c_enable_ibi_ctrl(gi3c, false);
	geni_i3c_enable_ibi_irq(gi3c, false);
	if (gi3c->ibi.ibic_naon && gi3c->ibi.naon_clk_en) {
		if (geni_i3c_enable_naon_ibi_clks(gi3c, false)) {
			I3C_LOG_ERR(gi3c->ipcl, true, gi3c->se.dev,
				"%s:  NAON clock failure\n", __func__);
			return;
		}
	}
	gi3c->ibi.is_init = false;
}

static void geni_i3c_master_free_ibi(struct i3c_dev_desc *dev)
{
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct geni_i3c_dev *gi3c = to_geni_i3c_master(m);
	struct geni_i3c_i2c_dev_data *data = i3c_dev_get_master_data(dev);
	unsigned long flags;

	if (!gi3c->ibi.hw_support && !gi3c->ibi.is_init)
		return;

	qcom_geni_i3c_ibi_unconf(gi3c);

	spin_lock_irqsave(&gi3c->ibi.lock, flags);
	gi3c->ibi.slots[data->ibi] = NULL;
	data->ibi = -1;
	spin_unlock_irqrestore(&gi3c->ibi.lock, flags);

	i3c_generic_ibi_free_pool(data->ibi_pool);
}

static void geni_i3c_master_recycle_ibi_slot
(
	struct i3c_dev_desc *dev,
	struct i3c_ibi_slot *slot
)
{
	struct geni_i3c_i2c_dev_data *data = i3c_dev_get_master_data(dev);

	i3c_generic_ibi_recycle_slot(data->ibi_pool, slot);
}

static const struct i3c_master_controller_ops geni_i3c_master_ops = {
	.bus_init = geni_i3c_master_bus_init,
	.bus_cleanup = geni_i3c_master_bus_cleanup,
	.do_daa = geni_i3c_master_do_daa,
	.attach_i3c_dev = geni_i3c_master_attach_i3c_dev,
	.reattach_i3c_dev = geni_i3c_master_reattach_i3c_dev,
	.detach_i3c_dev = geni_i3c_master_detach_i3c_dev,
	.attach_i2c_dev = geni_i3c_master_attach_i2c_dev,
	.detach_i2c_dev = geni_i3c_master_detach_i2c_dev,
	.supports_ccc_cmd = geni_i3c_master_supports_ccc_cmd,
	.send_ccc_cmd = geni_i3c_master_send_ccc_cmd,
	.priv_xfers = geni_i3c_master_priv_xfers,
	.i2c_xfers = geni_i3c_master_i2c_xfers,
	.enable_ibi = geni_i3c_master_enable_ibi,
	.disable_ibi = geni_i3c_master_disable_ibi,
	.request_ibi = geni_i3c_master_request_ibi,
	.free_ibi = geni_i3c_master_free_ibi,
	.recycle_ibi_slot = geni_i3c_master_recycle_ibi_slot,
};

/*
 * i3c_naon_ibi_clk_init: Read DTSI property and get clk handles
 * @gi3c: Device handle for i3c master
 *
 * return: returns 0 for success and nonzero for failure.
 */
static int i3c_naon_ibi_clk_init(struct geni_i3c_dev *gi3c)
{
	int ret = 0;

	if (gi3c->ibi.ibic_naon) {
		gi3c->ibi.core_clk = devm_clk_get(gi3c->se.dev,
							"ibic-core-clk");
		if (IS_ERR(gi3c->ibi.core_clk)) {
			ret = PTR_ERR(gi3c->ibi.core_clk);
			I3C_LOG_ERR(gi3c->ipcl, true, gi3c->se.dev,
				"Error getting NAON IBI Core clk %d\n", ret);
			return ret;
		}

		ret = clk_set_rate(gi3c->ibi.core_clk, 37500000);
		if (ret)
			I3C_LOG_ERR(gi3c->ipcl, true, gi3c->se.dev,
				"%s:Error Setting the clock rate: %d\n",
				 __func__, ret);

		gi3c->ibi.ahb_clk = devm_clk_get(gi3c->se.dev, "ibic-ahb-clk");
		if (IS_ERR(gi3c->ibi.ahb_clk)) {
			ret = PTR_ERR(gi3c->ibi.ahb_clk);
			I3C_LOG_ERR(gi3c->ipcl, true, gi3c->se.dev,
				"Error getting NAON AHB clk %d\n", ret);
			return ret;
		}

		gi3c->ibi.src_clk = devm_clk_get(gi3c->se.dev, "ibic-src-clk");
		if (IS_ERR(gi3c->ibi.src_clk)) {
			ret = PTR_ERR(gi3c->ibi.src_clk);
			I3C_LOG_ERR(gi3c->ipcl, true, gi3c->se.dev,
				"Error getting NAON src clk %d\n", ret);
			return ret;
		}
	}
	return ret;
}

static int i3c_geni_rsrcs_clk_init(struct geni_i3c_dev *gi3c)
{
	int ret;
	struct device *dev = gi3c->se.dev;

	gi3c->se.clk = devm_clk_get(gi3c->se.dev, "se-clk");
	if (IS_ERR(gi3c->se.clk)) {
		ret = PTR_ERR(gi3c->se.clk);
		I3C_LOG_ERR(gi3c->ipcl, true, gi3c->se.dev,
			"Error getting SE Core clk %d\n", ret);
		return ret;
	}

	gi3c->i3c_rsc.m_ahb_clk = devm_clk_get(dev->parent, "m-ahb");
	if (IS_ERR(gi3c->i3c_rsc.m_ahb_clk)) {
		ret = PTR_ERR(gi3c->i3c_rsc.m_ahb_clk);
		I3C_LOG_ERR(gi3c->ipcl, true, gi3c->se.dev,
			"Error getting M AHB clk %d\n", ret);
		return ret;
	}

	gi3c->i3c_rsc.s_ahb_clk = devm_clk_get(dev->parent, "s-ahb");
	if (IS_ERR(gi3c->i3c_rsc.s_ahb_clk)) {
		ret = PTR_ERR(gi3c->i3c_rsc.s_ahb_clk);
		I3C_LOG_ERR(gi3c->ipcl, true, gi3c->se.dev,
			"Error getting S AHB clk %d\n", ret);
		return ret;
	}

	return 0;
}

static int i3c_geni_rsrcs_init(struct geni_i3c_dev *gi3c,
			struct platform_device *pdev)
{
	int ret;
	struct resource *res;
	struct device *dev = &pdev->dev;

	/* base register address */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		I3C_LOG_ERR(gi3c->ipcl, false, gi3c->se.dev,
			"Err getting IO region\n");
		return -EINVAL;
	}

	gi3c->se.base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(gi3c->se.base))
		return PTR_ERR(gi3c->se.base);

	gi3c->se.dev = dev;
	gi3c->se.wrapper = dev_get_drvdata(dev->parent);
	gi3c->wrapper_dev = dev->parent;
	if (!gi3c->se.wrapper) {
		I3C_LOG_ERR(gi3c->ipcl, false, gi3c->se.dev,
			"SE Wrapper is NULL, deferring probe\n");
		return -EPROBE_DEFER;
	}

	ret = device_property_read_u32(&pdev->dev, "se-clock-frequency",
		&gi3c->clk_src_freq);
	if (ret) {
		I3C_LOG_DBG(gi3c->ipcl, false, gi3c->se.dev,
			"SE clk freq not specified, default to 100 MHz.\n");
		gi3c->clk_src_freq = 100000000;
	}

	ret = geni_se_common_resources_init(&gi3c->se,
			I3C_CORE2X_VOTE, GENI_DEFAULT_BW,
			(DEFAULT_SE_CLK * DEFAULT_BUS_WIDTH));
	if (ret) {
		I3C_LOG_DBG(gi3c->ipcl, false, gi3c->se.dev,
				"geni_se_common_resources_init Failed:%d\n", ret);
		return ret;
	}
	I3C_LOG_ERR(gi3c->ipcl, false, gi3c->se.dev,
		"%s: GENI_TO_CORE:%d CPU_TO_GENI:%d GENI_TO_DDR:%d\n", __func__,
		gi3c->se.icc_paths[GENI_TO_CORE].avg_bw,
		gi3c->se.icc_paths[CPU_TO_GENI].avg_bw,
		gi3c->se.icc_paths[GENI_TO_DDR].avg_bw);

	 /* call set_bw for once, then do icc_enable/disable */
	ret = geni_icc_set_bw(&gi3c->se);
	if (ret) {
		dev_err(&pdev->dev, "%s: icc set bw failed ret:%d\n",
							__func__, ret);
		return ret;
	}

	ret = device_property_read_u32(&pdev->dev, "dfs-index", &gi3c->dfs_idx);
	if (ret)
		gi3c->dfs_idx = 0xf;

	gi3c->i3c_rsc.i3c_pinctrl = devm_pinctrl_get(&pdev->dev);
	if (IS_ERR(gi3c->i3c_rsc.i3c_pinctrl)) {
		I3C_LOG_ERR(gi3c->ipcl, false, gi3c->se.dev,
			"Error no pinctrl config specified\n");
		ret = PTR_ERR(gi3c->i3c_rsc.i3c_pinctrl);
		return ret;
	}
	gi3c->i3c_rsc.i3c_gpio_active =
		pinctrl_lookup_state(gi3c->i3c_rsc.i3c_pinctrl, "default");
	if (IS_ERR(gi3c->i3c_rsc.i3c_gpio_active)) {
		I3C_LOG_ERR(gi3c->ipcl, false, gi3c->se.dev,
			"Error no pinctr default config specified\n");
		ret = PTR_ERR(gi3c->i3c_rsc.i3c_gpio_active);
		return ret;
	}
	gi3c->i3c_rsc.i3c_gpio_sleep =
		pinctrl_lookup_state(gi3c->i3c_rsc.i3c_pinctrl, "sleep");
	if (IS_ERR(gi3c->i3c_rsc.i3c_gpio_sleep)) {
		I3C_LOG_ERR(gi3c->ipcl, false, gi3c->se.dev,
			"Error no pinctrl sleep config specified\n");
		ret = PTR_ERR(gi3c->i3c_rsc.i3c_gpio_sleep);
		return ret;
	}
	gi3c->i3c_gpio_disable =
		pinctrl_lookup_state(gi3c->i3c_rsc.i3c_pinctrl, "disable");
	if (IS_ERR(gi3c->i3c_gpio_disable)) {
		I3C_LOG_ERR(gi3c->ipcl, false, gi3c->se.dev,
			"Error no pinctrl disable config specified\n");
		ret = PTR_ERR(gi3c->i3c_gpio_disable);
		return ret;
	}

	return 0;
}

static int i3c_ibi_rsrcs_init(struct geni_i3c_dev *gi3c,
		struct platform_device *pdev)
{
	struct resource *res;
	int ret;

	if (of_property_read_u32(pdev->dev.of_node, "qcom,ibi-ctrl-id",
		&gi3c->ibi.ctrl_id)) {
		I3C_LOG_ERR(gi3c->ipcl, true, gi3c->se.dev,
			"IBI controller instance id is not defined\n");
		return -ENXIO;
	}

	if (of_property_read_bool(pdev->dev.of_node, "qcom,ibic-naon")) {
		gi3c->ibi.ibic_naon = true;
		dev_info(&pdev->dev, "%s:I3C IBI is NAON cntrl\n", __func__);
		ret = i3c_naon_ibi_clk_init(gi3c);
		if (ret)
			return -EINVAL;

		if (geni_i3c_enable_naon_ibi_clks(gi3c, true))
			return -EINVAL;
	}

	/* Enable TLMM I3C MODE registers */
	msm_qup_write(gi3c->ibi.ctrl_id, TLMM_I3C_MODE);

	/* IBI register address */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (!res)
		return -EINVAL;

	gi3c->ibi.ibi_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(gi3c->ibi.ibi_base))
		return PTR_ERR(gi3c->ibi.ibi_base);

	gi3c->ibi.hw_support = (geni_read_reg(gi3c->se.base, SE_HW_PARAM_0)
				& GEN_I3C_IBI_CTRL);
	if (!gi3c->ibi.hw_support) {
		I3C_LOG_ERR(gi3c->ipcl, true, gi3c->se.dev,
			"IBI controller support not present\n");
		return -ENODEV;
	}

	init_completion(&gi3c->ibi.done);
	spin_lock_init(&gi3c->ibi.lock);
	gi3c->ibi.num_slots = ((geni_read_reg(gi3c->ibi.ibi_base, IBI_HW_PARAM)
				& I3C_IBI_TABLE_DEPTH_MSK));
	if (gi3c->ibi.num_slots == 0) {
		I3C_LOG_ERR(gi3c->ipcl, true, gi3c->se.dev,
			"Invalid num_slots:%d\n", gi3c->ibi.num_slots);
		return -EINVAL;
	}

	gi3c->ibi.slots = devm_kcalloc(&pdev->dev, gi3c->ibi.num_slots,
				sizeof(*gi3c->ibi.slots), GFP_KERNEL);
	if (!gi3c->ibi.slots)
		return -ENOMEM;

	if (of_property_read_bool(pdev->dev.of_node, "qcom,ibic-naon")) {
		gi3c->ibi.ibic_naon = true;
		dev_info(&pdev->dev, "IBI is NAON controller\n");
		ret = i3c_naon_ibi_clk_init(gi3c);
		if (ret)
			I3C_LOG_ERR(gi3c->ipcl, true, gi3c->se.dev,
				"NAON IBI clock failed to init:%d\n", ret);
	}

	/* Register IBI_C manager interrupt */
	gi3c->ibi.mngr_irq = platform_get_irq(pdev, 1);
	if (gi3c->ibi.mngr_irq < 0) {
		I3C_LOG_ERR(gi3c->ipcl, false, gi3c->se.dev,
			"IRQ error for ibi_c manager\n");
		return gi3c->ibi.mngr_irq;
	}

	ret = devm_request_irq(&pdev->dev, gi3c->ibi.mngr_irq, geni_i3c_ibi_irq,
			IRQF_TRIGGER_HIGH, dev_name(&pdev->dev), gi3c);
	if (ret) {
		I3C_LOG_ERR(gi3c->ipcl, false, gi3c->se.dev,
			"Request_irq:%d: err:%d\n", gi3c->ibi.mngr_irq, ret);
		return ret;
	}

	/* set mngr irq as wake-up irq */
	if (!gi3c->ibi.ibic_naon) {
		ret = irq_set_irq_wake(gi3c->ibi.mngr_irq, 1);
		if (ret) {
			I3C_LOG_ERR(gi3c->ipcl, false, gi3c->se.dev,
				"Failed to set mngr IRQ(%d) wake: err:%d\n",
				gi3c->ibi.mngr_irq, ret);
			return ret;
		}
	}

	/* Register GPII interrupt */
	gi3c->ibi.gpii_irq[0] = platform_get_irq(pdev, 2);
	if (gi3c->ibi.gpii_irq[0] < 0) {
		I3C_LOG_ERR(gi3c->ipcl, false, gi3c->se.dev,
			"IRQ error for ibi_c gpii\n");
		return gi3c->ibi.gpii_irq[0];
	}

	ret = devm_request_irq(&pdev->dev, gi3c->ibi.gpii_irq[0],
				geni_i3c_ibi_irq, IRQF_TRIGGER_HIGH,
				dev_name(&pdev->dev), gi3c);
	if (ret) {
		I3C_LOG_ERR(gi3c->ipcl, false, gi3c->se.dev,
		"Request_irq failed:%d: err:%d\n", gi3c->ibi.gpii_irq[0], ret);
		return ret;
	}

	/* set gpii irq as wake-up irq */
	if (!gi3c->ibi.ibic_naon) {
		ret = irq_set_irq_wake(gi3c->ibi.gpii_irq[0], 1);
		if (ret) {
			I3C_LOG_ERR(gi3c->ipcl, false, gi3c->se.dev,
				"Failed to set gpii IRQ(%d) wake: err:%d\n",
				gi3c->ibi.gpii_irq[0], ret);
			return ret;
		}
	}

	qcom_geni_i3c_ibi_conf(gi3c);

	return 0;
}

static void geni_i3c_get_ver_info(struct geni_i3c_dev *gi3c)
{
	unsigned int hw_ver;
	unsigned int major, minor, step;

	hw_ver = geni_se_get_qup_hw_version(&gi3c->se);

	geni_se_common_get_major_minor_num(hw_ver, &major, &minor, &step);

	I3C_LOG_DBG(gi3c->ipcl, false, gi3c->se.dev,
		"%s hw_ver: 0x%x Major:%d Minor:%d step:%d\n",
		__func__, hw_ver, major, minor, step);

	gi3c->ver_info.hw_major_ver = major;
	gi3c->ver_info.hw_minor_ver = minor;
	gi3c->ver_info.hw_step_ver = step;
	gi3c->ver_info.m_fw_ver = geni_se_common_get_m_fw(gi3c->se.base);
	gi3c->ver_info.s_fw_ver = geni_se_common_get_s_fw(gi3c->se.base);
	I3C_LOG_DBG(gi3c->ipcl, false, gi3c->se.dev, "%s:FW Ver:0x%x%x\n",
		__func__, gi3c->ver_info.m_fw_ver, gi3c->ver_info.s_fw_ver);
}

/*
 * geni_i3c_init_gsi_common_param() - initializes gsi common parameters
 *
 * @gi3c: i3c master device handle
 *
 * Return: None
 */
static void geni_i3c_init_gsi_common_param(struct geni_i3c_dev *gi3c)
{
	gi3c->gsi.protocol = GENI_SE_I3C;
	gi3c->gsi.dev = gi3c->se.dev;
	gi3c->gsi.xfer = &gi3c->done;
	gi3c->gsi.dev_node = gi3c;
	gi3c->gsi.ipc = gi3c->ipcl;
	gi3c->gsi.tx.cb_fun = gi3c_gsi_tx_cb;
	gi3c->gsi.rx.cb_fun = gi3c_gsi_rx_cb;
	gi3c->gsi.ev_cb_fun = gi3c_ev_cb;
	gi3c->gsi.protocol_err = &gi3c->err;
}

/*
 * geni_i3c_gsi_se_init() - Does gsi se initialization
 *
 * @gi3c: i3c master device handle
 *
 * Return: 0 on success, error code on failure
 */
static int geni_i3c_gsi_se_init(struct geni_i3c_dev *gi3c)
{
	gi3c->se_mode = GENI_GPI_DMA;
	geni_se_select_mode(&gi3c->se, GENI_GPI_DMA);
	I3C_LOG_DBG(gi3c->ipcl, false, gi3c->se.dev, "I3C in GSI ONLY mode\n");
	gi3c->gsi.tx.sg = devm_kzalloc(gi3c->se.dev, 5 * sizeof(struct scatterlist), GFP_KERNEL);
	if (!gi3c->gsi.tx.sg) {
		I3C_LOG_ERR(gi3c->ipcl, true, gi3c->se.dev, "could not allocate for tx_sg\n");
		return -ENOMEM;
	}

	gi3c->gsi.rx.sg = devm_kzalloc(gi3c->se.dev, sizeof(struct scatterlist), GFP_KERNEL);
	if (!gi3c->gsi.rx.sg) {
		I3C_LOG_ERR(gi3c->ipcl, true, gi3c->se.dev, "could not allocate for rx_sg\n");
		return -ENOMEM;
	}

	geni_i3c_init_gsi_common_param(gi3c);
	return 0;
}

/*
 * geni_i3c_enable_regulator() - enables i3c bus regulators
 *
 * @gi3c: i3c master device handle
 * @pdev: platform device pdev handle
 * @enable: enable flag
 *
 * Return: 0 on success, error code on failure
 */
#define MAX_REGULATOR	5
static int geni_i3c_enable_regulator(struct geni_i3c_dev *gi3c,
				     struct platform_device *pdev, bool enable)
{
	int i = 0;
	int ret = -EINVAL;
	struct regulator *reg[MAX_REGULATOR] = { NULL };
	const char *regulator_name[20] = {"i3c_rgltr1", "i3c_rgltr2", "i3c_rgltr3",
					  "i3c_rgltr4", "i3c_rgltr5"};

	for (i = 0; i < MAX_REGULATOR; i++) {
		if (enable) {
			reg[i] = devm_regulator_get(&pdev->dev, regulator_name[i]);
			if (!IS_ERR_OR_NULL(reg[i])) {
				ret = regulator_enable(reg[i]);
				if (ret) {
					I3C_LOG_DBG(gi3c->ipcl, false, gi3c->se.dev,
						    "%s regulator enable fail: %d\n",
						    regulator_name[i], ret);
				} else {
					I3C_LOG_DBG(gi3c->ipcl, false, gi3c->se.dev,
						    "%s regulator enabled: %d\n",
						    regulator_name[i], ret);
					ret = regulator_set_voltage(reg[i], 1800000, 1800000);
					if (ret)
						I3C_LOG_DBG(gi3c->ipcl, false, gi3c->se.dev,
							    "%s:set_volt fail: %d\n",
							    regulator_name[i], ret);
					else
						I3C_LOG_DBG(gi3c->ipcl, false,
							    gi3c->se.dev, "%s:set_volt done:%d\n",
							    regulator_name[i], ret);
				}
			}
		} else {
			ret = regulator_disable(reg[i]);
			if (ret)
				I3C_LOG_DBG(gi3c->ipcl, false, gi3c->se.dev,
					    "%s regulator disable fail:%d\n",
					    regulator_name[i], ret);
			else
				I3C_LOG_DBG(gi3c->ipcl, false, gi3c->se.dev,
					    "%s regulator disabled: %d\n",
					    regulator_name[i], ret);
		}
	}

	return ret;
}

static int geni_i3c_probe(struct platform_device *pdev)
{
	struct geni_i3c_dev *gi3c;
	u32 proto, tx_depth;
	int ret;
	u32 se_mode, geni_ios;

	gi3c = devm_kzalloc(&pdev->dev, sizeof(*gi3c), GFP_KERNEL);
	if (!gi3c)
		return -ENOMEM;

	gi3c->se.dev = &pdev->dev;
	gi3c->ipcl = ipc_log_context_create(4, dev_name(gi3c->se.dev), 0);
	if (!gi3c->ipcl)
		dev_info(&pdev->dev, "Error creating IPC Log\n");

	if (i3c_nos < MAX_I3C_SE)
		i3c_geni_dev[i3c_nos++] = gi3c;
	geni_i3c_enable_regulator(gi3c, pdev, true);
	ret = i3c_geni_rsrcs_init(gi3c, pdev);
	if (ret) {
		I3C_LOG_ERR(gi3c->ipcl, false, gi3c->se.dev,
			"Error:%d i3c_geni_rsrcs_init\n", ret);
		goto cleanup_init;
	}

	ret = i3c_geni_rsrcs_clk_init(gi3c);
	if (ret) {
		I3C_LOG_ERR(gi3c->ipcl, false, gi3c->se.dev,
			"Error:%d i3c_geni_rsrcs_clk_init\n", ret);
		goto cleanup_init;
	}

	gi3c->irq = platform_get_irq(pdev, 0);
	if (gi3c->irq < 0) {
		ret = gi3c->irq;
		I3C_LOG_ERR(gi3c->ipcl, false, gi3c->se.dev,
			"IRQ error=%d for i3c-master-geni\n", ret);
		goto cleanup_init;
	}

	init_completion(&gi3c->done);
	mutex_init(&gi3c->lock);
	spin_lock_init(&gi3c->spinlock);
	platform_set_drvdata(pdev, gi3c);

	/* Keep interrupt disabled so the system can enter low-power mode */
	irq_set_status_flags(gi3c->irq, IRQ_NOAUTOEN);
	ret = devm_request_irq(&pdev->dev, gi3c->irq, geni_i3c_irq,
		IRQF_TRIGGER_HIGH, dev_name(&pdev->dev), gi3c);
	if (ret) {
		I3C_LOG_ERR(gi3c->ipcl, false, gi3c->se.dev,
			"i3c irq failed:%d: err:%d\n", gi3c->irq, ret);
		goto cleanup_init;
	}

	ret = geni_icc_enable(&gi3c->se);
	if (ret) {
		I3C_LOG_ERR(gi3c->ipcl, false, gi3c->se.dev,
			"%s geni_icc_enable failed %d\n", __func__, ret);
		goto cleanup_init;
	}

	ret = geni_se_resources_on(&gi3c->se);
	if (ret) {
		I3C_LOG_ERR(gi3c->ipcl, false, gi3c->se.dev,
			"Error turning on resources %d\n", ret);
		goto cleanup_icc_init;
	}

	proto = geni_se_common_get_proto(gi3c->se.base);
	if (proto != GENI_SE_I3C) {
		I3C_LOG_ERR(gi3c->ipcl, false, gi3c->se.dev,
			"Invalid proto %d\n", proto);
		dev_err(gi3c->se.dev,
			"Invalid proto %d\n", proto);

		ret = -ENXIO;
		goto geni_resources_off;
	} else {
		geni_i3c_get_ver_info(gi3c);
	}

	gi3c->i3c_rsc.proto = GENI_SE_I3C;
	gi3c->disable_free_run_clks = false;

	se_mode = geni_read_reg(gi3c->se.base, GENI_IF_DISABLE_RO);
	if (se_mode) {
		ret = geni_i3c_gsi_se_init(gi3c);
		if (ret)
			goto geni_resources_off;
	}

	I3C_LOG_DBG(gi3c->ipcl, false, gi3c->se.dev,
			"%s: i3c_ibi_rsrcs_init()\n", __func__);
	ret = i3c_ibi_rsrcs_init(gi3c, pdev);
	if (ret) {
		I3C_LOG_ERR(gi3c->ipcl, false, gi3c->se.dev,
			"Error: %d, i3c_ibi_rsrcs_init\n", ret);
		goto geni_resources_off;
	}

	if (gi3c->se_mode != GENI_GPI_DMA) {
		tx_depth = geni_se_get_tx_fifo_depth(&gi3c->se);
		gi3c->tx_wm = tx_depth - 1;
		geni_se_init(&gi3c->se, gi3c->tx_wm, tx_depth);
		geni_se_config_packing(&gi3c->se, BITS_PER_BYTE, PACKING_BYTES_PW,
				       true, true, true);
		I3C_LOG_DBG(gi3c->ipcl, false, gi3c->se.dev,
			    "i3c fifo/se-dma mode. fifo depth:%d mode=%d\n",
			    tx_depth, gi3c->se_mode);
	}

	ret = geni_se_resources_off(&gi3c->se);
	if (ret)
		I3C_LOG_ERR(gi3c->ipcl, true, gi3c->se.dev,
			"%s: geni_se_resources_off failed%d\n", __func__, ret);
	ret = geni_icc_disable(&gi3c->se);
	if (ret)
		I3C_LOG_ERR(gi3c->ipcl, true, gi3c->se.dev,
			"%s: geni_icc_disable failed%d\n", __func__, ret);

	pm_runtime_set_suspended(gi3c->se.dev);
	pm_runtime_set_autosuspend_delay(gi3c->se.dev, I3C_AUTO_SUSPEND_DELAY);
	pm_runtime_use_autosuspend(gi3c->se.dev);
	pm_runtime_enable(gi3c->se.dev);

	geni_ios = geni_read_reg(gi3c->se.base, SE_GENI_IOS);
	if ((geni_ios & 0x3) != 0x3) { //SCL:b'1, SDA:b'0
		I3C_LOG_ERR(gi3c->ipcl, false, gi3c->se.dev,
		"%s: IO lines:0x%x, Ensure bus power up\n", __func__, geni_ios);
	}

	ret = i3c_master_register(&gi3c->ctrlr, &pdev->dev,
		&geni_i3c_master_ops, false);
	if (ret) {
		I3C_LOG_ERR(gi3c->ipcl, true, gi3c->se.dev,
			"I3C master registration failed=%d, continue\n", ret);

		/* NOTE : This may fail on 7E NACK, but should return 0 */
		ret = 0;
	}
	I3C_LOG_ERR(gi3c->ipcl, false, gi3c->se.dev,
		"I3C bus freq:%ld, I2C bus fres:%ld\n",
		gi3c->ctrlr.bus.scl_rate.i3c,  gi3c->ctrlr.bus.scl_rate.i2c);

	if (gi3c->se_mode == GENI_GPI_DMA) {
		if (geni_i3c_gsi_stop_on_bus(gi3c)) {
			I3C_LOG_ERR(gi3c->ipcl, false, gi3c->se.dev,
				    "I3C gsi stop on bus failed\n");
			return -EINVAL;
		}
	}

	// hot-join
	gi3c->hj_wl = wakeup_source_register(gi3c->se.dev,
					     dev_name(gi3c->se.dev));
	if (!gi3c->hj_wl) {
		I3C_LOG_ERR(gi3c->ipcl, false, gi3c->se.dev,
					"wakeup source registration failed\n");
		geni_se_resources_off(&gi3c->se);
		return -ENOMEM;
	}

	INIT_WORK(&gi3c->hj_wd, geni_i3c_hotjoin);
	gi3c->hj_wq = alloc_workqueue("%s", 0, 0, dev_name(gi3c->se.dev));
	geni_i3c_enable_hotjoin_irq(gi3c, true);

	I3C_LOG_ERR(gi3c->ipcl, true, gi3c->se.dev, "I3C probed:%d\n", ret);
	return ret;

geni_resources_off:
	ret = geni_se_resources_off(&gi3c->se);
	if (ret)
		I3C_LOG_ERR(gi3c->ipcl, true, gi3c->se.dev,
			"%s: geni_se_resources_off failed%d\n", __func__, ret);

cleanup_icc_init:
	ret = geni_icc_disable(&gi3c->se);
	if (ret)
		I3C_LOG_ERR(gi3c->ipcl, true, gi3c->se.dev,
			"%s: geni_icc_disable failed%d\n", __func__, ret);

cleanup_init:
	I3C_LOG_ERR(gi3c->ipcl, false, gi3c->se.dev, "I3C probe failed\n");
	return ret;
}

static int geni_i3c_remove(struct platform_device *pdev)
{
	struct geni_i3c_dev *gi3c = platform_get_drvdata(pdev);
	int ret = 0, i;

	//Disable hot-join, until next probe happens
	geni_i3c_enable_hotjoin_irq(gi3c, false);
	destroy_workqueue(gi3c->hj_wq);
	wakeup_source_unregister(gi3c->hj_wl);

	if (gi3c->ibi.is_init)
		qcom_geni_i3c_ibi_unconf(gi3c);

	/*force suspend to avoid the auto suspend caused by driver removal*/
	pm_runtime_force_suspend(gi3c->se.dev);
	ret = pinctrl_select_state(gi3c->i3c_rsc.i3c_pinctrl,
			gi3c->i3c_gpio_disable);
	if (ret)
		I3C_LOG_DBG(gi3c->ipcl, false, gi3c->se.dev,
			" i3c: pinctrl_select_state failed\n");

	ret = i3c_master_unregister(&gi3c->ctrlr);
	/* TBD : If we need debug for previous session, Don't delete logs */
	if (gi3c->ipcl)
		ipc_log_context_destroy(gi3c->ipcl);

	for (i = 0; i < i3c_nos; i++)
		i3c_geni_dev[i] = NULL;
	i3c_nos = 0;

	return ret;
}

static int geni_i3c_resume_early(struct device *dev)
{
	struct geni_i3c_dev *gi3c = dev_get_drvdata(dev);

	if (gi3c->ibi.ibic_naon && !gi3c->ibi.naon_clk_en) {
		if (geni_i3c_enable_naon_ibi_clks(gi3c, true)) {
			I3C_LOG_ERR(gi3c->ipcl, true, gi3c->se.dev,
				"%s:  NAON clock failure\n", __func__);
			return -EAGAIN;
		}
	}

	return 0;
}

#if IS_ENABLED(CONFIG_PM)
/*
 * geni_i3c_gpi_pause_resume - Does gsi suspend and gsi resume
 *
 * @gi3c: i3c master device handle
 * @is_suspend: suspend status boolean flag
 *
 * Return: 0 on success, error code on failure
 */
static int geni_i3c_gpi_pause_resume(struct geni_i3c_dev *gi3c, bool is_suspend)
{
	int tx_ret = 0;

	if (gi3c->gsi.tx.ch) {
		if (is_suspend)
			tx_ret = dmaengine_pause(gi3c->gsi.tx.ch);
		else
			tx_ret = dmaengine_resume(gi3c->gsi.tx.ch);

		if (tx_ret) {
			I3C_LOG_ERR(gi3c->ipcl, false, gi3c->se.dev,
				    "%s failed: tx:%d is_suspend:%d\n",
				    __func__, tx_ret, is_suspend);
			return -EINVAL;
		}
	}
	return 0;
}

static int geni_i3c_runtime_suspend(struct device *dev)
{
	struct geni_i3c_dev *gi3c = dev_get_drvdata(dev);
	int ret;

	if (gi3c->se_mode != GENI_GPI_DMA) {
		disable_irq(gi3c->irq);
	} else {
		geni_i3c_gsi_stop_on_bus(gi3c);
		ret = geni_i3c_gpi_pause_resume(gi3c, true);
		if (ret) {
			I3C_LOG_ERR(gi3c->ipcl, false, gi3c->se.dev,
				    "%s: ret:%d\n", __func__, ret);
			return ret;
		}
	}

	ret = geni_se_resources_off(&gi3c->se);
	if (ret)
		I3C_LOG_ERR(gi3c->ipcl, false, gi3c->se.dev,
			"%s geni_se_resources_off failed %d\n", __func__, ret);
	ret = geni_icc_disable(&gi3c->se);
	if (ret)
		I3C_LOG_ERR(gi3c->ipcl, false, gi3c->se.dev,
			"%s geni_icc_disable failed %d\n", __func__, ret);

	I3C_LOG_DBG(gi3c->ipcl, false, gi3c->se.dev, "%s():ret:%d\n",
			 __func__, ret);
	return 0;
}

static int geni_i3c_runtime_resume(struct device *dev)
{
	int ret;
	struct geni_i3c_dev *gi3c = dev_get_drvdata(dev);

	ret = geni_icc_enable(&gi3c->se);
	if (ret) {
		I3C_LOG_ERR(gi3c->ipcl, false, gi3c->se.dev,
			    "%s geni_icc_enable failed %d\n", __func__, ret);
		return ret;
	}

	ret = geni_se_resources_on(&gi3c->se);
	if (ret) {
		I3C_LOG_ERR(gi3c->ipcl, false, gi3c->se.dev,
			"%s geni_se_resources_on failed %d\n", __func__, ret);
		return ret;
	}

	geni_write_reg(0x7f, gi3c->se.base, GENI_OUTPUT_CTRL);
	/* Added 10 us delay to settle the write of the register as per HW team recommendation */
	udelay(10);

	if (gi3c->se_mode != GENI_GPI_DMA) {
		enable_irq(gi3c->irq);
	} else {
		ret = geni_i3c_gpi_pause_resume(gi3c, false);
		if (ret) {
			I3C_LOG_ERR(gi3c->ipcl, false, gi3c->se.dev,
				    "%s: ret:%d\n", __func__, ret);
			return ret;
		}
	}
	I3C_LOG_DBG(gi3c->ipcl, false, gi3c->se.dev, "%s(): ret:%d\n",
			__func__, ret);
	/* Enable TLMM I3C MODE registers */
	return 0;
}

static int geni_i3c_suspend_late(struct device *dev)
{
	struct geni_i3c_dev *gi3c = dev_get_drvdata(dev);

	if (gi3c->ibi.ibic_naon && gi3c->ibi.naon_clk_en) {
		if (geni_i3c_enable_naon_ibi_clks(gi3c, false)) {
			I3C_LOG_ERR(gi3c->ipcl, true, gi3c->se.dev,
				"%s:  NAON clock failure\n", __func__);
			return -EAGAIN;
		}
	}

	if (!pm_runtime_status_suspended(dev)) {
		I3C_LOG_DBG(gi3c->ipcl, false, gi3c->se.dev,
				"%s: Forced suspend\n", __func__);
		geni_i3c_runtime_suspend(dev);
		pm_runtime_disable(dev);
		pm_runtime_put_noidle(dev);
		pm_runtime_set_suspended(dev);
		pm_runtime_enable(dev);
	}
	return 0;
}
#else
static int geni_i3c_runtime_suspend(struct device *dev)
{
	return 0;
}

static int geni_i3c_runtime_resume(struct device *dev)
{
	return 0;
}

static int geni_i3c_suspend_late(struct device *dev)
{
	return 0;
}
#endif

static const struct dev_pm_ops geni_i3c_pm_ops = {
	.suspend_late = geni_i3c_suspend_late,
	.resume_early = geni_i3c_resume_early,
	.runtime_suspend = geni_i3c_runtime_suspend,
	.runtime_resume  = geni_i3c_runtime_resume,
};

static const struct of_device_id geni_i3c_dt_match[] = {
	{ .compatible = "qcom,geni-i3c" },
	{ }
};
MODULE_DEVICE_TABLE(of, geni_i3c_dt_match);

static struct platform_driver geni_i3c_master = {
	.probe  = geni_i3c_probe,
	.remove = geni_i3c_remove,
	.driver = {
		.name = "geni_i3c_master",
		.pm = &geni_i3c_pm_ops,
		.of_match_table = geni_i3c_dt_match,
	},
};

static int __init i3c_dev_init(void)
{
	return platform_driver_register(&geni_i3c_master);
}

static void __exit i3c_dev_exit(void)
{
	platform_driver_unregister(&geni_i3c_master);
}

module_init(i3c_dev_init);
module_exit(i3c_dev_exit);

MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:geni_i3c_master");
