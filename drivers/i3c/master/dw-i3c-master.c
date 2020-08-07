// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018 Synopsys, Inc. and/or its affiliates.
 *
 * Author: Vitor Soares <vitor.soares@synopsys.com>
 */

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/i3c/master.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/iopoll.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/slab.h>

#define DEVICE_CTRL			0x0
#define DEV_CTRL_ENABLE			BIT(31)
#define DEV_CTRL_RESUME			BIT(30)
#define DEV_CTRL_HOT_JOIN_NACK		BIT(8)
#define DEV_CTRL_I2C_SLAVE_PRESENT	BIT(7)

#define DEVICE_ADDR			0x4
#define DEV_ADDR_DYNAMIC_ADDR_VALID	BIT(31)
#define DEV_ADDR_DYNAMIC(x)		(((x) << 16) & GENMASK(22, 16))

#define HW_CAPABILITY			0x8
#define COMMAND_QUEUE_PORT		0xc
#define COMMAND_PORT_TOC		BIT(30)
#define COMMAND_PORT_READ_TRANSFER	BIT(28)
#define COMMAND_PORT_SDAP		BIT(27)
#define COMMAND_PORT_ROC		BIT(26)
#define COMMAND_PORT_SPEED(x)		(((x) << 21) & GENMASK(23, 21))
#define COMMAND_PORT_DEV_INDEX(x)	(((x) << 16) & GENMASK(20, 16))
#define COMMAND_PORT_CP			BIT(15)
#define COMMAND_PORT_CMD(x)		(((x) << 7) & GENMASK(14, 7))
#define COMMAND_PORT_TID(x)		(((x) << 3) & GENMASK(6, 3))

#define COMMAND_PORT_ARG_DATA_LEN(x)	(((x) << 16) & GENMASK(31, 16))
#define COMMAND_PORT_ARG_DATA_LEN_MAX	65536
#define COMMAND_PORT_TRANSFER_ARG	0x01

#define COMMAND_PORT_SDA_DATA_BYTE_3(x)	(((x) << 24) & GENMASK(31, 24))
#define COMMAND_PORT_SDA_DATA_BYTE_2(x)	(((x) << 16) & GENMASK(23, 16))
#define COMMAND_PORT_SDA_DATA_BYTE_1(x)	(((x) << 8) & GENMASK(15, 8))
#define COMMAND_PORT_SDA_BYTE_STRB_3	BIT(5)
#define COMMAND_PORT_SDA_BYTE_STRB_2	BIT(4)
#define COMMAND_PORT_SDA_BYTE_STRB_1	BIT(3)
#define COMMAND_PORT_SHORT_DATA_ARG	0x02

#define COMMAND_PORT_DEV_COUNT(x)	(((x) << 21) & GENMASK(25, 21))
#define COMMAND_PORT_ADDR_ASSGN_CMD	0x03

#define RESPONSE_QUEUE_PORT		0x10
#define RESPONSE_PORT_ERR_STATUS(x)	(((x) & GENMASK(31, 28)) >> 28)
#define RESPONSE_NO_ERROR		0
#define RESPONSE_ERROR_CRC		1
#define RESPONSE_ERROR_PARITY		2
#define RESPONSE_ERROR_FRAME		3
#define RESPONSE_ERROR_IBA_NACK		4
#define RESPONSE_ERROR_ADDRESS_NACK	5
#define RESPONSE_ERROR_OVER_UNDER_FLOW	6
#define RESPONSE_ERROR_TRANSF_ABORT	8
#define RESPONSE_ERROR_I2C_W_NACK_ERR	9
#define RESPONSE_PORT_TID(x)		(((x) & GENMASK(27, 24)) >> 24)
#define RESPONSE_PORT_DATA_LEN(x)	((x) & GENMASK(15, 0))

#define RX_TX_DATA_PORT			0x14
#define IBI_QUEUE_STATUS		0x18
#define QUEUE_THLD_CTRL			0x1c
#define QUEUE_THLD_CTRL_RESP_BUF_MASK	GENMASK(15, 8)
#define QUEUE_THLD_CTRL_RESP_BUF(x)	(((x) - 1) << 8)

#define DATA_BUFFER_THLD_CTRL		0x20
#define DATA_BUFFER_THLD_CTRL_RX_BUF	GENMASK(11, 8)

#define IBI_QUEUE_CTRL			0x24
#define IBI_MR_REQ_REJECT		0x2C
#define IBI_SIR_REQ_REJECT		0x30
#define IBI_REQ_REJECT_ALL		GENMASK(31, 0)

#define RESET_CTRL			0x34
#define RESET_CTRL_IBI_QUEUE		BIT(5)
#define RESET_CTRL_RX_FIFO		BIT(4)
#define RESET_CTRL_TX_FIFO		BIT(3)
#define RESET_CTRL_RESP_QUEUE		BIT(2)
#define RESET_CTRL_CMD_QUEUE		BIT(1)
#define RESET_CTRL_SOFT			BIT(0)

#define SLV_EVENT_CTRL			0x38
#define INTR_STATUS			0x3c
#define INTR_STATUS_EN			0x40
#define INTR_SIGNAL_EN			0x44
#define INTR_FORCE			0x48
#define INTR_BUSOWNER_UPDATE_STAT	BIT(13)
#define INTR_IBI_UPDATED_STAT		BIT(12)
#define INTR_READ_REQ_RECV_STAT		BIT(11)
#define INTR_DEFSLV_STAT		BIT(10)
#define INTR_TRANSFER_ERR_STAT		BIT(9)
#define INTR_DYN_ADDR_ASSGN_STAT	BIT(8)
#define INTR_CCC_UPDATED_STAT		BIT(6)
#define INTR_TRANSFER_ABORT_STAT	BIT(5)
#define INTR_RESP_READY_STAT		BIT(4)
#define INTR_CMD_QUEUE_READY_STAT	BIT(3)
#define INTR_IBI_THLD_STAT		BIT(2)
#define INTR_RX_THLD_STAT		BIT(1)
#define INTR_TX_THLD_STAT		BIT(0)
#define INTR_ALL			(INTR_BUSOWNER_UPDATE_STAT |	\
					INTR_IBI_UPDATED_STAT |		\
					INTR_READ_REQ_RECV_STAT |	\
					INTR_DEFSLV_STAT |		\
					INTR_TRANSFER_ERR_STAT |	\
					INTR_DYN_ADDR_ASSGN_STAT |	\
					INTR_CCC_UPDATED_STAT |		\
					INTR_TRANSFER_ABORT_STAT |	\
					INTR_RESP_READY_STAT |		\
					INTR_CMD_QUEUE_READY_STAT |	\
					INTR_IBI_THLD_STAT |		\
					INTR_TX_THLD_STAT |		\
					INTR_RX_THLD_STAT)

#define INTR_MASTER_MASK		(INTR_TRANSFER_ERR_STAT |	\
					 INTR_RESP_READY_STAT)

#define QUEUE_STATUS_LEVEL		0x4c
#define QUEUE_STATUS_IBI_STATUS_CNT(x)	(((x) & GENMASK(28, 24)) >> 24)
#define QUEUE_STATUS_IBI_BUF_BLR(x)	(((x) & GENMASK(23, 16)) >> 16)
#define QUEUE_STATUS_LEVEL_RESP(x)	(((x) & GENMASK(15, 8)) >> 8)
#define QUEUE_STATUS_LEVEL_CMD(x)	((x) & GENMASK(7, 0))

#define DATA_BUFFER_STATUS_LEVEL	0x50
#define DATA_BUFFER_STATUS_LEVEL_TX(x)	((x) & GENMASK(7, 0))

#define PRESENT_STATE			0x54
#define CCC_DEVICE_STATUS		0x58
#define DEVICE_ADDR_TABLE_POINTER	0x5c
#define DEVICE_ADDR_TABLE_DEPTH(x)	(((x) & GENMASK(31, 16)) >> 16)
#define DEVICE_ADDR_TABLE_ADDR(x)	((x) & GENMASK(7, 0))

#define DEV_CHAR_TABLE_POINTER		0x60
#define VENDOR_SPECIFIC_REG_POINTER	0x6c
#define SLV_PID_VALUE			0x74
#define SLV_CHAR_CTRL			0x78
#define SLV_MAX_LEN			0x7c
#define MAX_READ_TURNAROUND		0x80
#define MAX_DATA_SPEED			0x84
#define SLV_DEBUG_STATUS		0x88
#define SLV_INTR_REQ			0x8c
#define DEVICE_CTRL_EXTENDED		0xb0
#define SCL_I3C_OD_TIMING		0xb4
#define SCL_I3C_PP_TIMING		0xb8
#define SCL_I3C_TIMING_HCNT(x)		(((x) << 16) & GENMASK(23, 16))
#define SCL_I3C_TIMING_LCNT(x)		((x) & GENMASK(7, 0))
#define SCL_I3C_TIMING_CNT_MIN		5

#define SCL_I2C_FM_TIMING		0xbc
#define SCL_I2C_FM_TIMING_HCNT(x)	(((x) << 16) & GENMASK(31, 16))
#define SCL_I2C_FM_TIMING_LCNT(x)	((x) & GENMASK(15, 0))

#define SCL_I2C_FMP_TIMING		0xc0
#define SCL_I2C_FMP_TIMING_HCNT(x)	(((x) << 16) & GENMASK(23, 16))
#define SCL_I2C_FMP_TIMING_LCNT(x)	((x) & GENMASK(15, 0))

#define SCL_EXT_LCNT_TIMING		0xc8
#define SCL_EXT_LCNT_4(x)		(((x) << 24) & GENMASK(31, 24))
#define SCL_EXT_LCNT_3(x)		(((x) << 16) & GENMASK(23, 16))
#define SCL_EXT_LCNT_2(x)		(((x) << 8) & GENMASK(15, 8))
#define SCL_EXT_LCNT_1(x)		((x) & GENMASK(7, 0))

#define SCL_EXT_TERMN_LCNT_TIMING	0xcc
#define BUS_FREE_TIMING			0xd4
#define BUS_I3C_MST_FREE(x)		((x) & GENMASK(15, 0))

#define BUS_IDLE_TIMING			0xd8
#define I3C_VER_ID			0xe0
#define I3C_VER_TYPE			0xe4
#define EXTENDED_CAPABILITY		0xe8
#define SLAVE_CONFIG			0xec

#define DEV_ADDR_TABLE_LEGACY_I2C_DEV	BIT(31)
#define DEV_ADDR_TABLE_DYNAMIC_ADDR(x)	(((x) << 16) & GENMASK(23, 16))
#define DEV_ADDR_TABLE_STATIC_ADDR(x)	((x) & GENMASK(6, 0))
#define DEV_ADDR_TABLE_LOC(start, idx)	((start) + ((idx) << 2))

#define MAX_DEVS 32

#define I3C_BUS_SDR1_SCL_RATE		8000000
#define I3C_BUS_SDR2_SCL_RATE		6000000
#define I3C_BUS_SDR3_SCL_RATE		4000000
#define I3C_BUS_SDR4_SCL_RATE		2000000
#define I3C_BUS_I2C_FM_TLOW_MIN_NS	1300
#define I3C_BUS_I2C_FMP_TLOW_MIN_NS	500
#define I3C_BUS_THIGH_MAX_NS		41

#define XFER_TIMEOUT (msecs_to_jiffies(1000))

struct dw_i3c_master_caps {
	u8 cmdfifodepth;
	u8 datafifodepth;
};

struct dw_i3c_cmd {
	u32 cmd_lo;
	u32 cmd_hi;
	u16 tx_len;
	const void *tx_buf;
	u16 rx_len;
	void *rx_buf;
	u8 error;
};

struct dw_i3c_xfer {
	struct list_head node;
	struct completion comp;
	int ret;
	unsigned int ncmds;
	struct dw_i3c_cmd cmds[];
};

struct dw_i3c_master {
	struct i3c_master_controller base;
	u16 maxdevs;
	u16 datstartaddr;
	u32 free_pos;
	struct {
		struct list_head list;
		struct dw_i3c_xfer *cur;
		spinlock_t lock;
	} xferqueue;
	struct dw_i3c_master_caps caps;
	void __iomem *regs;
	struct reset_control *core_rst;
	struct clk *core_clk;
	char version[5];
	char type[5];
	u8 addrs[MAX_DEVS];
};

struct dw_i3c_i2c_dev_data {
	u8 index;
};

static u8 even_parity(u8 p)
{
	p ^= p >> 4;
	p &= 0xf;

	return (0x9669 >> p) & 1;
}

static bool dw_i3c_master_supports_ccc_cmd(struct i3c_master_controller *m,
					   const struct i3c_ccc_cmd *cmd)
{
	if (cmd->ndests > 1)
		return false;

	switch (cmd->id) {
	case I3C_CCC_ENEC(true):
	case I3C_CCC_ENEC(false):
	case I3C_CCC_DISEC(true):
	case I3C_CCC_DISEC(false):
	case I3C_CCC_ENTAS(0, true):
	case I3C_CCC_ENTAS(0, false):
	case I3C_CCC_RSTDAA(true):
	case I3C_CCC_RSTDAA(false):
	case I3C_CCC_ENTDAA:
	case I3C_CCC_SETMWL(true):
	case I3C_CCC_SETMWL(false):
	case I3C_CCC_SETMRL(true):
	case I3C_CCC_SETMRL(false):
	case I3C_CCC_ENTHDR(0):
	case I3C_CCC_SETDASA:
	case I3C_CCC_SETNEWDA:
	case I3C_CCC_GETMWL:
	case I3C_CCC_GETMRL:
	case I3C_CCC_GETPID:
	case I3C_CCC_GETBCR:
	case I3C_CCC_GETDCR:
	case I3C_CCC_GETSTATUS:
	case I3C_CCC_GETMXDS:
	case I3C_CCC_GETHDRCAP:
		return true;
	default:
		return false;
	}
}

static inline struct dw_i3c_master *
to_dw_i3c_master(struct i3c_master_controller *master)
{
	return container_of(master, struct dw_i3c_master, base);
}

static void dw_i3c_master_disable(struct dw_i3c_master *master)
{
	writel(readl(master->regs + DEVICE_CTRL) & ~DEV_CTRL_ENABLE,
	       master->regs + DEVICE_CTRL);
}

static void dw_i3c_master_enable(struct dw_i3c_master *master)
{
	writel(readl(master->regs + DEVICE_CTRL) | DEV_CTRL_ENABLE,
	       master->regs + DEVICE_CTRL);
}

static int dw_i3c_master_get_addr_pos(struct dw_i3c_master *master, u8 addr)
{
	int pos;

	for (pos = 0; pos < master->maxdevs; pos++) {
		if (addr == master->addrs[pos])
			return pos;
	}

	return -EINVAL;
}

static int dw_i3c_master_get_free_pos(struct dw_i3c_master *master)
{
	if (!(master->free_pos & GENMASK(master->maxdevs - 1, 0)))
		return -ENOSPC;

	return ffs(master->free_pos) - 1;
}

static void dw_i3c_master_wr_tx_fifo(struct dw_i3c_master *master,
				     const u8 *bytes, int nbytes)
{
	writesl(master->regs + RX_TX_DATA_PORT, bytes, nbytes / 4);
	if (nbytes & 3) {
		u32 tmp = 0;

		memcpy(&tmp, bytes + (nbytes & ~3), nbytes & 3);
		writesl(master->regs + RX_TX_DATA_PORT, &tmp, 1);
	}
}

static void dw_i3c_master_read_rx_fifo(struct dw_i3c_master *master,
				       u8 *bytes, int nbytes)
{
	readsl(master->regs + RX_TX_DATA_PORT, bytes, nbytes / 4);
	if (nbytes & 3) {
		u32 tmp;

		readsl(master->regs + RX_TX_DATA_PORT, &tmp, 1);
		memcpy(bytes + (nbytes & ~3), &tmp, nbytes & 3);
	}
}

static struct dw_i3c_xfer *
dw_i3c_master_alloc_xfer(struct dw_i3c_master *master, unsigned int ncmds)
{
	struct dw_i3c_xfer *xfer;

	xfer = kzalloc(struct_size(xfer, cmds, ncmds), GFP_KERNEL);
	if (!xfer)
		return NULL;

	INIT_LIST_HEAD(&xfer->node);
	xfer->ncmds = ncmds;
	xfer->ret = -ETIMEDOUT;

	return xfer;
}

static void dw_i3c_master_free_xfer(struct dw_i3c_xfer *xfer)
{
	kfree(xfer);
}

static void dw_i3c_master_start_xfer_locked(struct dw_i3c_master *master)
{
	struct dw_i3c_xfer *xfer = master->xferqueue.cur;
	unsigned int i;
	u32 thld_ctrl;

	if (!xfer)
		return;

	for (i = 0; i < xfer->ncmds; i++) {
		struct dw_i3c_cmd *cmd = &xfer->cmds[i];

		dw_i3c_master_wr_tx_fifo(master, cmd->tx_buf, cmd->tx_len);
	}

	thld_ctrl = readl(master->regs + QUEUE_THLD_CTRL);
	thld_ctrl &= ~QUEUE_THLD_CTRL_RESP_BUF_MASK;
	thld_ctrl |= QUEUE_THLD_CTRL_RESP_BUF(xfer->ncmds);
	writel(thld_ctrl, master->regs + QUEUE_THLD_CTRL);

	for (i = 0; i < xfer->ncmds; i++) {
		struct dw_i3c_cmd *cmd = &xfer->cmds[i];

		writel(cmd->cmd_hi, master->regs + COMMAND_QUEUE_PORT);
		writel(cmd->cmd_lo, master->regs + COMMAND_QUEUE_PORT);
	}
}

static void dw_i3c_master_enqueue_xfer(struct dw_i3c_master *master,
				       struct dw_i3c_xfer *xfer)
{
	unsigned long flags;

	init_completion(&xfer->comp);
	spin_lock_irqsave(&master->xferqueue.lock, flags);
	if (master->xferqueue.cur) {
		list_add_tail(&xfer->node, &master->xferqueue.list);
	} else {
		master->xferqueue.cur = xfer;
		dw_i3c_master_start_xfer_locked(master);
	}
	spin_unlock_irqrestore(&master->xferqueue.lock, flags);
}

static void dw_i3c_master_dequeue_xfer_locked(struct dw_i3c_master *master,
					      struct dw_i3c_xfer *xfer)
{
	if (master->xferqueue.cur == xfer) {
		u32 status;

		master->xferqueue.cur = NULL;

		writel(RESET_CTRL_RX_FIFO | RESET_CTRL_TX_FIFO |
		       RESET_CTRL_RESP_QUEUE | RESET_CTRL_CMD_QUEUE,
		       master->regs + RESET_CTRL);

		readl_poll_timeout_atomic(master->regs + RESET_CTRL, status,
					  !status, 10, 1000000);
	} else {
		list_del_init(&xfer->node);
	}
}

static void dw_i3c_master_dequeue_xfer(struct dw_i3c_master *master,
				       struct dw_i3c_xfer *xfer)
{
	unsigned long flags;

	spin_lock_irqsave(&master->xferqueue.lock, flags);
	dw_i3c_master_dequeue_xfer_locked(master, xfer);
	spin_unlock_irqrestore(&master->xferqueue.lock, flags);
}

static void dw_i3c_master_end_xfer_locked(struct dw_i3c_master *master, u32 isr)
{
	struct dw_i3c_xfer *xfer = master->xferqueue.cur;
	int i, ret = 0;
	u32 nresp;

	if (!xfer)
		return;

	nresp = readl(master->regs + QUEUE_STATUS_LEVEL);
	nresp = QUEUE_STATUS_LEVEL_RESP(nresp);

	for (i = 0; i < nresp; i++) {
		struct dw_i3c_cmd *cmd;
		u32 resp;

		resp = readl(master->regs + RESPONSE_QUEUE_PORT);

		cmd = &xfer->cmds[RESPONSE_PORT_TID(resp)];
		cmd->rx_len = RESPONSE_PORT_DATA_LEN(resp);
		cmd->error = RESPONSE_PORT_ERR_STATUS(resp);
		if (cmd->rx_len && !cmd->error)
			dw_i3c_master_read_rx_fifo(master, cmd->rx_buf,
						   cmd->rx_len);
	}

	for (i = 0; i < nresp; i++) {
		switch (xfer->cmds[i].error) {
		case RESPONSE_NO_ERROR:
			break;
		case RESPONSE_ERROR_PARITY:
		case RESPONSE_ERROR_IBA_NACK:
		case RESPONSE_ERROR_TRANSF_ABORT:
		case RESPONSE_ERROR_CRC:
		case RESPONSE_ERROR_FRAME:
			ret = -EIO;
			break;
		case RESPONSE_ERROR_OVER_UNDER_FLOW:
			ret = -ENOSPC;
			break;
		case RESPONSE_ERROR_I2C_W_NACK_ERR:
		case RESPONSE_ERROR_ADDRESS_NACK:
		default:
			ret = -EINVAL;
			break;
		}
	}

	xfer->ret = ret;
	complete(&xfer->comp);

	if (ret < 0) {
		dw_i3c_master_dequeue_xfer_locked(master, xfer);
		writel(readl(master->regs + DEVICE_CTRL) | DEV_CTRL_RESUME,
		       master->regs + DEVICE_CTRL);
	}

	xfer = list_first_entry_or_null(&master->xferqueue.list,
					struct dw_i3c_xfer,
					node);
	if (xfer)
		list_del_init(&xfer->node);

	master->xferqueue.cur = xfer;
	dw_i3c_master_start_xfer_locked(master);
}

static int dw_i3c_clk_cfg(struct dw_i3c_master *master)
{
	unsigned long core_rate, core_period;
	u32 scl_timing;
	u8 hcnt, lcnt;

	core_rate = clk_get_rate(master->core_clk);
	if (!core_rate)
		return -EINVAL;

	core_period = DIV_ROUND_UP(1000000000, core_rate);

	hcnt = DIV_ROUND_UP(I3C_BUS_THIGH_MAX_NS, core_period) - 1;
	if (hcnt < SCL_I3C_TIMING_CNT_MIN)
		hcnt = SCL_I3C_TIMING_CNT_MIN;

	lcnt = DIV_ROUND_UP(core_rate, I3C_BUS_TYP_I3C_SCL_RATE) - hcnt;
	if (lcnt < SCL_I3C_TIMING_CNT_MIN)
		lcnt = SCL_I3C_TIMING_CNT_MIN;

	scl_timing = SCL_I3C_TIMING_HCNT(hcnt) | SCL_I3C_TIMING_LCNT(lcnt);
	writel(scl_timing, master->regs + SCL_I3C_PP_TIMING);

	if (!(readl(master->regs + DEVICE_CTRL) & DEV_CTRL_I2C_SLAVE_PRESENT))
		writel(BUS_I3C_MST_FREE(lcnt), master->regs + BUS_FREE_TIMING);

	lcnt = DIV_ROUND_UP(I3C_BUS_TLOW_OD_MIN_NS, core_period);
	scl_timing = SCL_I3C_TIMING_HCNT(hcnt) | SCL_I3C_TIMING_LCNT(lcnt);
	writel(scl_timing, master->regs + SCL_I3C_OD_TIMING);

	lcnt = DIV_ROUND_UP(core_rate, I3C_BUS_SDR1_SCL_RATE) - hcnt;
	scl_timing = SCL_EXT_LCNT_1(lcnt);
	lcnt = DIV_ROUND_UP(core_rate, I3C_BUS_SDR2_SCL_RATE) - hcnt;
	scl_timing |= SCL_EXT_LCNT_2(lcnt);
	lcnt = DIV_ROUND_UP(core_rate, I3C_BUS_SDR3_SCL_RATE) - hcnt;
	scl_timing |= SCL_EXT_LCNT_3(lcnt);
	lcnt = DIV_ROUND_UP(core_rate, I3C_BUS_SDR4_SCL_RATE) - hcnt;
	scl_timing |= SCL_EXT_LCNT_4(lcnt);
	writel(scl_timing, master->regs + SCL_EXT_LCNT_TIMING);

	return 0;
}

static int dw_i2c_clk_cfg(struct dw_i3c_master *master)
{
	unsigned long core_rate, core_period;
	u16 hcnt, lcnt;
	u32 scl_timing;

	core_rate = clk_get_rate(master->core_clk);
	if (!core_rate)
		return -EINVAL;

	core_period = DIV_ROUND_UP(1000000000, core_rate);

	lcnt = DIV_ROUND_UP(I3C_BUS_I2C_FMP_TLOW_MIN_NS, core_period);
	hcnt = DIV_ROUND_UP(core_rate, I3C_BUS_I2C_FM_PLUS_SCL_RATE) - lcnt;
	scl_timing = SCL_I2C_FMP_TIMING_HCNT(hcnt) |
		     SCL_I2C_FMP_TIMING_LCNT(lcnt);
	writel(scl_timing, master->regs + SCL_I2C_FMP_TIMING);

	lcnt = DIV_ROUND_UP(I3C_BUS_I2C_FM_TLOW_MIN_NS, core_period);
	hcnt = DIV_ROUND_UP(core_rate, I3C_BUS_I2C_FM_SCL_RATE) - lcnt;
	scl_timing = SCL_I2C_FM_TIMING_HCNT(hcnt) |
		     SCL_I2C_FM_TIMING_LCNT(lcnt);
	writel(scl_timing, master->regs + SCL_I2C_FM_TIMING);

	writel(BUS_I3C_MST_FREE(lcnt), master->regs + BUS_FREE_TIMING);
	writel(readl(master->regs + DEVICE_CTRL) | DEV_CTRL_I2C_SLAVE_PRESENT,
	       master->regs + DEVICE_CTRL);

	return 0;
}

static int dw_i3c_master_bus_init(struct i3c_master_controller *m)
{
	struct dw_i3c_master *master = to_dw_i3c_master(m);
	struct i3c_bus *bus = i3c_master_get_bus(m);
	struct i3c_device_info info = { };
	u32 thld_ctrl;
	int ret;

	switch (bus->mode) {
	case I3C_BUS_MODE_MIXED_FAST:
	case I3C_BUS_MODE_MIXED_LIMITED:
		ret = dw_i2c_clk_cfg(master);
		if (ret)
			return ret;
		/* fall through */
	case I3C_BUS_MODE_PURE:
		ret = dw_i3c_clk_cfg(master);
		if (ret)
			return ret;
		break;
	default:
		return -EINVAL;
	}

	thld_ctrl = readl(master->regs + QUEUE_THLD_CTRL);
	thld_ctrl &= ~QUEUE_THLD_CTRL_RESP_BUF_MASK;
	writel(thld_ctrl, master->regs + QUEUE_THLD_CTRL);

	thld_ctrl = readl(master->regs + DATA_BUFFER_THLD_CTRL);
	thld_ctrl &= ~DATA_BUFFER_THLD_CTRL_RX_BUF;
	writel(thld_ctrl, master->regs + DATA_BUFFER_THLD_CTRL);

	writel(INTR_ALL, master->regs + INTR_STATUS);
	writel(INTR_MASTER_MASK, master->regs + INTR_STATUS_EN);
	writel(INTR_MASTER_MASK, master->regs + INTR_SIGNAL_EN);

	ret = i3c_master_get_free_addr(m, 0);
	if (ret < 0)
		return ret;

	writel(DEV_ADDR_DYNAMIC_ADDR_VALID | DEV_ADDR_DYNAMIC(ret),
	       master->regs + DEVICE_ADDR);

	memset(&info, 0, sizeof(info));
	info.dyn_addr = ret;

	ret = i3c_master_set_info(&master->base, &info);
	if (ret)
		return ret;

	writel(IBI_REQ_REJECT_ALL, master->regs + IBI_SIR_REQ_REJECT);
	writel(IBI_REQ_REJECT_ALL, master->regs + IBI_MR_REQ_REJECT);

	/* For now don't support Hot-Join */
	writel(readl(master->regs + DEVICE_CTRL) | DEV_CTRL_HOT_JOIN_NACK,
	       master->regs + DEVICE_CTRL);

	dw_i3c_master_enable(master);

	return 0;
}

static void dw_i3c_master_bus_cleanup(struct i3c_master_controller *m)
{
	struct dw_i3c_master *master = to_dw_i3c_master(m);

	dw_i3c_master_disable(master);
}

static int dw_i3c_ccc_set(struct dw_i3c_master *master,
			  struct i3c_ccc_cmd *ccc)
{
	struct dw_i3c_xfer *xfer;
	struct dw_i3c_cmd *cmd;
	int ret, pos = 0;

	if (ccc->id & I3C_CCC_DIRECT) {
		pos = dw_i3c_master_get_addr_pos(master, ccc->dests[0].addr);
		if (pos < 0)
			return pos;
	}

	xfer = dw_i3c_master_alloc_xfer(master, 1);
	if (!xfer)
		return -ENOMEM;

	cmd = xfer->cmds;
	cmd->tx_buf = ccc->dests[0].payload.data;
	cmd->tx_len = ccc->dests[0].payload.len;

	cmd->cmd_hi = COMMAND_PORT_ARG_DATA_LEN(ccc->dests[0].payload.len) |
		      COMMAND_PORT_TRANSFER_ARG;

	cmd->cmd_lo = COMMAND_PORT_CP |
		      COMMAND_PORT_DEV_INDEX(pos) |
		      COMMAND_PORT_CMD(ccc->id) |
		      COMMAND_PORT_TOC |
		      COMMAND_PORT_ROC;

	dw_i3c_master_enqueue_xfer(master, xfer);
	if (!wait_for_completion_timeout(&xfer->comp, XFER_TIMEOUT))
		dw_i3c_master_dequeue_xfer(master, xfer);

	ret = xfer->ret;
	if (xfer->cmds[0].error == RESPONSE_ERROR_IBA_NACK)
		ccc->err = I3C_ERROR_M2;

	dw_i3c_master_free_xfer(xfer);

	return ret;
}

static int dw_i3c_ccc_get(struct dw_i3c_master *master, struct i3c_ccc_cmd *ccc)
{
	struct dw_i3c_xfer *xfer;
	struct dw_i3c_cmd *cmd;
	int ret, pos;

	pos = dw_i3c_master_get_addr_pos(master, ccc->dests[0].addr);
	if (pos < 0)
		return pos;

	xfer = dw_i3c_master_alloc_xfer(master, 1);
	if (!xfer)
		return -ENOMEM;

	cmd = xfer->cmds;
	cmd->rx_buf = ccc->dests[0].payload.data;
	cmd->rx_len = ccc->dests[0].payload.len;

	cmd->cmd_hi = COMMAND_PORT_ARG_DATA_LEN(ccc->dests[0].payload.len) |
		      COMMAND_PORT_TRANSFER_ARG;

	cmd->cmd_lo = COMMAND_PORT_READ_TRANSFER |
		      COMMAND_PORT_CP |
		      COMMAND_PORT_DEV_INDEX(pos) |
		      COMMAND_PORT_CMD(ccc->id) |
		      COMMAND_PORT_TOC |
		      COMMAND_PORT_ROC;

	dw_i3c_master_enqueue_xfer(master, xfer);
	if (!wait_for_completion_timeout(&xfer->comp, XFER_TIMEOUT))
		dw_i3c_master_dequeue_xfer(master, xfer);

	ret = xfer->ret;
	if (xfer->cmds[0].error == RESPONSE_ERROR_IBA_NACK)
		ccc->err = I3C_ERROR_M2;
	dw_i3c_master_free_xfer(xfer);

	return ret;
}

static int dw_i3c_master_send_ccc_cmd(struct i3c_master_controller *m,
				      struct i3c_ccc_cmd *ccc)
{
	struct dw_i3c_master *master = to_dw_i3c_master(m);
	int ret = 0;

	if (ccc->id == I3C_CCC_ENTDAA)
		return -EINVAL;

	if (ccc->rnw)
		ret = dw_i3c_ccc_get(master, ccc);
	else
		ret = dw_i3c_ccc_set(master, ccc);

	return ret;
}

static int dw_i3c_master_daa(struct i3c_master_controller *m)
{
	struct dw_i3c_master *master = to_dw_i3c_master(m);
	struct dw_i3c_xfer *xfer;
	struct dw_i3c_cmd *cmd;
	u32 olddevs, newdevs;
	u8 p, last_addr = 0;
	int ret, pos;

	olddevs = ~(master->free_pos);

	/* Prepare DAT before launching DAA. */
	for (pos = 0; pos < master->maxdevs; pos++) {
		if (olddevs & BIT(pos))
			continue;

		ret = i3c_master_get_free_addr(m, last_addr + 1);
		if (ret < 0)
			return -ENOSPC;

		master->addrs[pos] = ret;
		p = even_parity(ret);
		last_addr = ret;
		ret |= (p << 7);

		writel(DEV_ADDR_TABLE_DYNAMIC_ADDR(ret),
		       master->regs +
		       DEV_ADDR_TABLE_LOC(master->datstartaddr, pos));
	}

	xfer = dw_i3c_master_alloc_xfer(master, 1);
	if (!xfer)
		return -ENOMEM;

	pos = dw_i3c_master_get_free_pos(master);
	cmd = &xfer->cmds[0];
	cmd->cmd_hi = 0x1;
	cmd->cmd_lo = COMMAND_PORT_DEV_COUNT(master->maxdevs - pos) |
		      COMMAND_PORT_DEV_INDEX(pos) |
		      COMMAND_PORT_CMD(I3C_CCC_ENTDAA) |
		      COMMAND_PORT_ADDR_ASSGN_CMD |
		      COMMAND_PORT_TOC |
		      COMMAND_PORT_ROC;

	dw_i3c_master_enqueue_xfer(master, xfer);
	if (!wait_for_completion_timeout(&xfer->comp, XFER_TIMEOUT))
		dw_i3c_master_dequeue_xfer(master, xfer);

	newdevs = GENMASK(master->maxdevs - cmd->rx_len - 1, 0);
	newdevs &= ~olddevs;

	for (pos = 0; pos < master->maxdevs; pos++) {
		if (newdevs & BIT(pos))
			i3c_master_add_i3c_dev_locked(m, master->addrs[pos]);
	}

	dw_i3c_master_free_xfer(xfer);

	i3c_master_disec_locked(m, I3C_BROADCAST_ADDR,
				I3C_CCC_EVENT_HJ |
				I3C_CCC_EVENT_MR |
				I3C_CCC_EVENT_SIR);

	return 0;
}

static int dw_i3c_master_priv_xfers(struct i3c_dev_desc *dev,
				    struct i3c_priv_xfer *i3c_xfers,
				    int i3c_nxfers)
{
	struct dw_i3c_i2c_dev_data *data = i3c_dev_get_master_data(dev);
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct dw_i3c_master *master = to_dw_i3c_master(m);
	unsigned int nrxwords = 0, ntxwords = 0;
	struct dw_i3c_xfer *xfer;
	int i, ret = 0;

	if (!i3c_nxfers)
		return 0;

	if (i3c_nxfers > master->caps.cmdfifodepth)
		return -ENOTSUPP;

	for (i = 0; i < i3c_nxfers; i++) {
		if (i3c_xfers[i].rnw)
			nrxwords += DIV_ROUND_UP(i3c_xfers[i].len, 4);
		else
			ntxwords += DIV_ROUND_UP(i3c_xfers[i].len, 4);
	}

	if (ntxwords > master->caps.datafifodepth ||
	    nrxwords > master->caps.datafifodepth)
		return -ENOTSUPP;

	xfer = dw_i3c_master_alloc_xfer(master, i3c_nxfers);
	if (!xfer)
		return -ENOMEM;

	for (i = 0; i < i3c_nxfers; i++) {
		struct dw_i3c_cmd *cmd = &xfer->cmds[i];

		cmd->cmd_hi = COMMAND_PORT_ARG_DATA_LEN(i3c_xfers[i].len) |
			COMMAND_PORT_TRANSFER_ARG;

		if (i3c_xfers[i].rnw) {
			cmd->rx_buf = i3c_xfers[i].data.in;
			cmd->rx_len = i3c_xfers[i].len;
			cmd->cmd_lo = COMMAND_PORT_READ_TRANSFER |
				      COMMAND_PORT_SPEED(dev->info.max_read_ds);

		} else {
			cmd->tx_buf = i3c_xfers[i].data.out;
			cmd->tx_len = i3c_xfers[i].len;
			cmd->cmd_lo =
				COMMAND_PORT_SPEED(dev->info.max_write_ds);
		}

		cmd->cmd_lo |= COMMAND_PORT_TID(i) |
			       COMMAND_PORT_DEV_INDEX(data->index) |
			       COMMAND_PORT_ROC;

		if (i == (i3c_nxfers - 1))
			cmd->cmd_lo |= COMMAND_PORT_TOC;
	}

	dw_i3c_master_enqueue_xfer(master, xfer);
	if (!wait_for_completion_timeout(&xfer->comp, XFER_TIMEOUT))
		dw_i3c_master_dequeue_xfer(master, xfer);

	ret = xfer->ret;
	dw_i3c_master_free_xfer(xfer);

	return ret;
}

static int dw_i3c_master_reattach_i3c_dev(struct i3c_dev_desc *dev,
					  u8 old_dyn_addr)
{
	struct dw_i3c_i2c_dev_data *data = i3c_dev_get_master_data(dev);
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct dw_i3c_master *master = to_dw_i3c_master(m);
	int pos;

	pos = dw_i3c_master_get_free_pos(master);

	if (data->index > pos && pos > 0) {
		writel(0,
		       master->regs +
		       DEV_ADDR_TABLE_LOC(master->datstartaddr, data->index));

		master->addrs[data->index] = 0;
		master->free_pos |= BIT(data->index);

		data->index = pos;
		master->addrs[pos] = dev->info.dyn_addr;
		master->free_pos &= ~BIT(pos);
	}

	writel(DEV_ADDR_TABLE_DYNAMIC_ADDR(dev->info.dyn_addr),
	       master->regs +
	       DEV_ADDR_TABLE_LOC(master->datstartaddr, data->index));

	master->addrs[data->index] = dev->info.dyn_addr;

	return 0;
}

static int dw_i3c_master_attach_i3c_dev(struct i3c_dev_desc *dev)
{
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct dw_i3c_master *master = to_dw_i3c_master(m);
	struct dw_i3c_i2c_dev_data *data;
	int pos;

	pos = dw_i3c_master_get_free_pos(master);
	if (pos < 0)
		return pos;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->index = pos;
	master->addrs[pos] = dev->info.dyn_addr ? : dev->info.static_addr;
	master->free_pos &= ~BIT(pos);
	i3c_dev_set_master_data(dev, data);

	writel(DEV_ADDR_TABLE_DYNAMIC_ADDR(master->addrs[pos]),
	       master->regs +
	       DEV_ADDR_TABLE_LOC(master->datstartaddr, data->index));

	return 0;
}

static void dw_i3c_master_detach_i3c_dev(struct i3c_dev_desc *dev)
{
	struct dw_i3c_i2c_dev_data *data = i3c_dev_get_master_data(dev);
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct dw_i3c_master *master = to_dw_i3c_master(m);

	writel(0,
	       master->regs +
	       DEV_ADDR_TABLE_LOC(master->datstartaddr, data->index));

	i3c_dev_set_master_data(dev, NULL);
	master->addrs[data->index] = 0;
	master->free_pos |= BIT(data->index);
	kfree(data);
}

static int dw_i3c_master_i2c_xfers(struct i2c_dev_desc *dev,
				   const struct i2c_msg *i2c_xfers,
				   int i2c_nxfers)
{
	struct dw_i3c_i2c_dev_data *data = i2c_dev_get_master_data(dev);
	struct i3c_master_controller *m = i2c_dev_get_master(dev);
	struct dw_i3c_master *master = to_dw_i3c_master(m);
	unsigned int nrxwords = 0, ntxwords = 0;
	struct dw_i3c_xfer *xfer;
	int i, ret = 0;

	if (!i2c_nxfers)
		return 0;

	if (i2c_nxfers > master->caps.cmdfifodepth)
		return -ENOTSUPP;

	for (i = 0; i < i2c_nxfers; i++) {
		if (i2c_xfers[i].flags & I2C_M_RD)
			nrxwords += DIV_ROUND_UP(i2c_xfers[i].len, 4);
		else
			ntxwords += DIV_ROUND_UP(i2c_xfers[i].len, 4);
	}

	if (ntxwords > master->caps.datafifodepth ||
	    nrxwords > master->caps.datafifodepth)
		return -ENOTSUPP;

	xfer = dw_i3c_master_alloc_xfer(master, i2c_nxfers);
	if (!xfer)
		return -ENOMEM;

	for (i = 0; i < i2c_nxfers; i++) {
		struct dw_i3c_cmd *cmd = &xfer->cmds[i];

		cmd->cmd_hi = COMMAND_PORT_ARG_DATA_LEN(i2c_xfers[i].len) |
			COMMAND_PORT_TRANSFER_ARG;

		cmd->cmd_lo = COMMAND_PORT_TID(i) |
			      COMMAND_PORT_DEV_INDEX(data->index) |
			      COMMAND_PORT_ROC;

		if (i2c_xfers[i].flags & I2C_M_RD) {
			cmd->cmd_lo |= COMMAND_PORT_READ_TRANSFER;
			cmd->rx_buf = i2c_xfers[i].buf;
			cmd->rx_len = i2c_xfers[i].len;
		} else {
			cmd->tx_buf = i2c_xfers[i].buf;
			cmd->tx_len = i2c_xfers[i].len;
		}

		if (i == (i2c_nxfers - 1))
			cmd->cmd_lo |= COMMAND_PORT_TOC;
	}

	dw_i3c_master_enqueue_xfer(master, xfer);
	if (!wait_for_completion_timeout(&xfer->comp, XFER_TIMEOUT))
		dw_i3c_master_dequeue_xfer(master, xfer);

	ret = xfer->ret;
	dw_i3c_master_free_xfer(xfer);

	return ret;
}

static int dw_i3c_master_attach_i2c_dev(struct i2c_dev_desc *dev)
{
	struct i3c_master_controller *m = i2c_dev_get_master(dev);
	struct dw_i3c_master *master = to_dw_i3c_master(m);
	struct dw_i3c_i2c_dev_data *data;
	int pos;

	pos = dw_i3c_master_get_free_pos(master);
	if (pos < 0)
		return pos;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->index = pos;
	master->addrs[pos] = dev->addr;
	master->free_pos &= ~BIT(pos);
	i2c_dev_set_master_data(dev, data);

	writel(DEV_ADDR_TABLE_LEGACY_I2C_DEV |
	       DEV_ADDR_TABLE_STATIC_ADDR(dev->addr),
	       master->regs +
	       DEV_ADDR_TABLE_LOC(master->datstartaddr, data->index));

	return 0;
}

static void dw_i3c_master_detach_i2c_dev(struct i2c_dev_desc *dev)
{
	struct dw_i3c_i2c_dev_data *data = i2c_dev_get_master_data(dev);
	struct i3c_master_controller *m = i2c_dev_get_master(dev);
	struct dw_i3c_master *master = to_dw_i3c_master(m);

	writel(0,
	       master->regs +
	       DEV_ADDR_TABLE_LOC(master->datstartaddr, data->index));

	i2c_dev_set_master_data(dev, NULL);
	master->addrs[data->index] = 0;
	master->free_pos |= BIT(data->index);
	kfree(data);
}

static irqreturn_t dw_i3c_master_irq_handler(int irq, void *dev_id)
{
	struct dw_i3c_master *master = dev_id;
	u32 status;

	status = readl(master->regs + INTR_STATUS);

	if (!(status & readl(master->regs + INTR_STATUS_EN))) {
		writel(INTR_ALL, master->regs + INTR_STATUS);
		return IRQ_NONE;
	}

	spin_lock(&master->xferqueue.lock);
	dw_i3c_master_end_xfer_locked(master, status);
	if (status & INTR_TRANSFER_ERR_STAT)
		writel(INTR_TRANSFER_ERR_STAT, master->regs + INTR_STATUS);
	spin_unlock(&master->xferqueue.lock);

	return IRQ_HANDLED;
}

static const struct i3c_master_controller_ops dw_mipi_i3c_ops = {
	.bus_init = dw_i3c_master_bus_init,
	.bus_cleanup = dw_i3c_master_bus_cleanup,
	.attach_i3c_dev = dw_i3c_master_attach_i3c_dev,
	.reattach_i3c_dev = dw_i3c_master_reattach_i3c_dev,
	.detach_i3c_dev = dw_i3c_master_detach_i3c_dev,
	.do_daa = dw_i3c_master_daa,
	.supports_ccc_cmd = dw_i3c_master_supports_ccc_cmd,
	.send_ccc_cmd = dw_i3c_master_send_ccc_cmd,
	.priv_xfers = dw_i3c_master_priv_xfers,
	.attach_i2c_dev = dw_i3c_master_attach_i2c_dev,
	.detach_i2c_dev = dw_i3c_master_detach_i2c_dev,
	.i2c_xfers = dw_i3c_master_i2c_xfers,
};

static int dw_i3c_probe(struct platform_device *pdev)
{
	struct dw_i3c_master *master;
	int ret, irq;

	master = devm_kzalloc(&pdev->dev, sizeof(*master), GFP_KERNEL);
	if (!master)
		return -ENOMEM;

	master->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(master->regs))
		return PTR_ERR(master->regs);

	master->core_clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(master->core_clk))
		return PTR_ERR(master->core_clk);

	master->core_rst = devm_reset_control_get_optional_exclusive(&pdev->dev,
								    "core_rst");
	if (IS_ERR(master->core_rst))
		return PTR_ERR(master->core_rst);

	ret = clk_prepare_enable(master->core_clk);
	if (ret)
		goto err_disable_core_clk;

	reset_control_deassert(master->core_rst);

	spin_lock_init(&master->xferqueue.lock);
	INIT_LIST_HEAD(&master->xferqueue.list);

	writel(INTR_ALL, master->regs + INTR_STATUS);
	irq = platform_get_irq(pdev, 0);
	ret = devm_request_irq(&pdev->dev, irq,
			       dw_i3c_master_irq_handler, 0,
			       dev_name(&pdev->dev), master);
	if (ret)
		goto err_assert_rst;

	platform_set_drvdata(pdev, master);

	/* Information regarding the FIFOs/QUEUEs depth */
	ret = readl(master->regs + QUEUE_STATUS_LEVEL);
	master->caps.cmdfifodepth = QUEUE_STATUS_LEVEL_CMD(ret);

	ret = readl(master->regs + DATA_BUFFER_STATUS_LEVEL);
	master->caps.datafifodepth = DATA_BUFFER_STATUS_LEVEL_TX(ret);

	ret = readl(master->regs + DEVICE_ADDR_TABLE_POINTER);
	master->datstartaddr = ret;
	master->maxdevs = ret >> 16;
	master->free_pos = GENMASK(master->maxdevs - 1, 0);

	ret = i3c_master_register(&master->base, &pdev->dev,
				  &dw_mipi_i3c_ops, false);
	if (ret)
		goto err_assert_rst;

	return 0;

err_assert_rst:
	reset_control_assert(master->core_rst);

err_disable_core_clk:
	clk_disable_unprepare(master->core_clk);

	return ret;
}

static int dw_i3c_remove(struct platform_device *pdev)
{
	struct dw_i3c_master *master = platform_get_drvdata(pdev);
	int ret;

	ret = i3c_master_unregister(&master->base);
	if (ret)
		return ret;

	reset_control_assert(master->core_rst);

	clk_disable_unprepare(master->core_clk);

	return 0;
}

static const struct of_device_id dw_i3c_master_of_match[] = {
	{ .compatible = "snps,dw-i3c-master-1.00a", },
	{},
};
MODULE_DEVICE_TABLE(of, dw_i3c_master_of_match);

static struct platform_driver dw_i3c_driver = {
	.probe = dw_i3c_probe,
	.remove = dw_i3c_remove,
	.driver = {
		.name = "dw-i3c-master",
		.of_match_table = of_match_ptr(dw_i3c_master_of_match),
	},
};
module_platform_driver(dw_i3c_driver);

MODULE_AUTHOR("Vitor Soares <vitor.soares@synopsys.com>");
MODULE_DESCRIPTION("DesignWare MIPI I3C driver");
MODULE_LICENSE("GPL v2");
