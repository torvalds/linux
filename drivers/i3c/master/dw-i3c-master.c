// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018 Synopsys, Inc. and/or its affiliates.
 *
 * Author: Vitor Soares <vitor.soares@synopsys.com>
 */

#include <linux/bitops.h>
#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/i3c/master.h>
#include <linux/i3c/target.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/iopoll.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/slab.h>
#include <dt-bindings/i3c/i3c.h>

#include "dw-i3c-master.h"

#define DEVICE_CTRL			0x0
#define DEV_CTRL_ENABLE			BIT(31)
#define DEV_CTRL_RESUME			BIT(30)
#define DEV_CTRL_ABORT			BIT(29)
#define DEV_CTRL_IBI_PAYLOAD_EN		BIT(9)
#define DEV_CTRL_HOT_JOIN_NACK		BIT(8)
#define DEV_CTRL_I2C_SLAVE_PRESENT	BIT(7)
#define DEV_CTRL_IBA_INCLUDE		BIT(0)

#define DEVICE_ADDR			0x4
#define DEV_ADDR_DYNAMIC_ADDR_VALID	BIT(31)
#define DEV_ADDR_DYNAMIC		GENMASK(22, 16)

#define HW_CAPABILITY			0x8
#define COMMAND_QUEUE_PORT		0xc
#define COMMAND_PORT_TOC		BIT(30)
#define COMMAND_PORT_READ_TRANSFER	BIT(28)
#define COMMAND_PORT_SDAP		BIT(27)
#define COMMAND_PORT_ROC		BIT(26)
#define COMMAND_PORT_SPEED(x)		(((x) << 21) & GENMASK(23, 21))
#define   SPEED_I3C_SDR0		0x0
#define   SPEED_I3C_SDR1		0x1
#define   SPEED_I3C_SDR2		0x2
#define   SPEED_I3C_SDR3		0x3
#define   SPEED_I3C_SDR4		0x4
#define   SPEED_I3C_HDR_TS		0x5
#define   SPEED_I3C_HDR_DDR		0x6
#define   SPEED_I3C_I2C_FM		0x7
#define   SPEED_I2C_FM			0x0
#define   SPEED_I2C_FMP			0x1
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
#define   TID_TARGET_IBI		0b0001
#define   TID_TARGET_RD_DATA		0b0010
#define   TID_TARGET_MASTER_WR_DATA	0b1000
#define   TID_TARGET_MASTER_DEFSLVS	0b1111
#define RESPONSE_PORT_DATA_LEN(x)	((x) & GENMASK(15, 0))

#define RX_TX_DATA_PORT			0x14
#define IBI_QUEUE_STATUS		0x18
#define IBI_QUEUE_STATUS_IBI_ID(x)	(((x) & GENMASK(15, 8)) >> 8)
#define IBI_QUEUE_STATUS_DATA_LEN(x)	((x) & GENMASK(7, 0))
#define IBI_QUEUE_IBI_ADDR(x)		(IBI_QUEUE_STATUS_IBI_ID(x) >> 1)
#define IBI_QUEUE_IBI_RNW(x)		(IBI_QUEUE_STATUS_IBI_ID(x) & BIT(0))
#define IBI_TYPE_MR(x)                                                         \
	((IBI_QUEUE_IBI_ADDR(x) != I3C_HOT_JOIN_ADDR) && !IBI_QUEUE_IBI_RNW(x))
#define IBI_TYPE_HJ(x)                                                         \
	((IBI_QUEUE_IBI_ADDR(x) == I3C_HOT_JOIN_ADDR) && !IBI_QUEUE_IBI_RNW(x))
#define IBI_TYPE_SIRQ(x)                                                        \
	((IBI_QUEUE_IBI_ADDR(x) != I3C_HOT_JOIN_ADDR) && IBI_QUEUE_IBI_RNW(x))

#define QUEUE_THLD_CTRL			0x1c
#define QUEUE_THLD_CTRL_IBI_STAT_MASK	GENMASK(31, 24)
#define QUEUE_THLD_CTRL_IBI_STAT(x)	(((x) - 1) << 24)
#define QUEUE_THLD_CTRL_IBI_DATA_MASK	GENMASK(20, 16)
#define QUEUE_THLD_CTRL_IBI_DATA(x)	((x) << 16)
#define QUEUE_THLD_CTRL_RESP_BUF_MASK	GENMASK(15, 8)
#define QUEUE_THLD_CTRL_RESP_BUF(x)	(((x) - 1) << 8)

#define DATA_BUFFER_THLD_CTRL		0x20
#define DATA_BUFFER_THLD_TX_START	GENMASK(18, 16)
#define DATA_BUFFER_THLD_CTRL_RX_BUF	GENMASK(10, 8)

#define IBI_QUEUE_CTRL			0x24
#define IBI_MR_REQ_REJECT		0x2C
#define IBI_SIR_REQ_REJECT		0x30
#define IBI_REQ_REJECT_ALL		GENMASK(31, 0)

#define RESET_CTRL			0x34
#define RESET_CTRL_BUS			BIT(31)
#define RESET_CTRL_BUS_RESET_TYPE	GENMASK(30, 29)
#define   BUS_RESET_TYPE_EXIT		0b00
#define   BUS_RESET_TYPE_SCL_LOW	0b11
#define RESET_CTRL_IBI_QUEUE		BIT(5)
#define RESET_CTRL_RX_FIFO		BIT(4)
#define RESET_CTRL_TX_FIFO		BIT(3)
#define RESET_CTRL_RESP_QUEUE		BIT(2)
#define RESET_CTRL_CMD_QUEUE		BIT(1)
#define RESET_CTRL_SOFT			BIT(0)
#define RESET_CTRL_XFER_QUEUES		(RESET_CTRL_RX_FIFO |                 \
					 RESET_CTRL_TX_FIFO |                 \
					 RESET_CTRL_RESP_QUEUE |              \
					 RESET_CTRL_CMD_QUEUE)
#define RESET_CTRL_QUEUES		(RESET_CTRL_IBI_QUEUE |               \
					 RESET_CTRL_XFER_QUEUES)

#define SLV_EVENT_CTRL			0x38
#define   SLV_EVENT_CTRL_MWL_UPD	BIT(7)
#define   SLV_EVENT_CTRL_MRL_UPD	BIT(6)
#define   SLV_EVENT_CTRL_SIR_EN		BIT(0)

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

#define INTR_TARGET_MASK		(INTR_READ_REQ_RECV_STAT |	\
					INTR_RESP_READY_STAT |		\
					INTR_IBI_UPDATED_STAT  |	\
					INTR_TRANSFER_ERR_STAT |	\
					INTR_CCC_UPDATED_STAT |         \
					INTR_DYN_ADDR_ASSGN_STAT)

#define QUEUE_STATUS_LEVEL		0x4c
#define QUEUE_STATUS_IBI_STATUS_CNT(x)	(((x) & GENMASK(28, 24)) >> 24)
#define QUEUE_STATUS_IBI_BUF_BLR(x)	(((x) & GENMASK(23, 16)) >> 16)
#define QUEUE_STATUS_LEVEL_RESP(x)	(((x) & GENMASK(15, 8)) >> 8)
#define QUEUE_STATUS_LEVEL_CMD(x)	((x) & GENMASK(7, 0))

#define DATA_BUFFER_STATUS_LEVEL	0x50
#define DATA_BUFFER_STATUS_LEVEL_TX(x)	((x) & GENMASK(7, 0))

#define PRESENT_STATE			0x54
#define   CM_TFR_ST_STS			GENMASK(21, 16)
#define     CM_TFR_ST_STS_HALT		0x13
#define   CM_TFR_STS			GENMASK(13, 8)
#define     CM_TFR_STS_MASTER_SERV_IBI	0xe
#define     CM_TFR_STS_MASTER_HALT	0xf
#define     CM_TFR_STS_SLAVE_HALT	0x6

#define CCC_DEVICE_STATUS		0x58
#define DEVICE_ADDR_TABLE_POINTER	0x5c
#define DEVICE_ADDR_TABLE_DEPTH(x)	(((x) & GENMASK(31, 16)) >> 16)
#define DEVICE_ADDR_TABLE_ADDR(x)	((x) & GENMASK(7, 0))

#define DEV_CHAR_TABLE_POINTER		0x60
#define VENDOR_SPECIFIC_REG_POINTER	0x6c
#define SLV_MIPI_ID_VALUE		0x70
#define SLV_PID_VALUE			0x74
#define SLV_PID_HI(x)			(((x) >> 32) & GENMASK(15, 0))
#define SLV_PID_LO(x)			((x) & GENMASK(31, 0))
#define SLV_CHAR_CTRL			0x78
#define   SLV_DCR			GENMASK(15, 8)
#define   SLV_BCR			GENMASK(7, 0)
#define     SLV_BCR_DEVICE_ROLE		GENMASK(7, 6)

#define SLV_MAX_LEN			0x7c
#define SLV_MAX_RD_LEN(x)		(((x) & GENMASK(31, 16)) >> 16)
#define SLV_MAX_WR_LEN(x)		((x) & GENMASK(15, 0))

#define MAX_READ_TURNAROUND		0x80
#define MAX_DATA_SPEED			0x84
#define SLV_DEBUG_STATUS		0x88
#define SLV_INTR_REQ			0x8c
#define SLV_INTR_REQ_IBI_STS(x)		(((x) & GENMASK(9, 8)) >> 8)
#define IBI_STS_ACCEPTED		0x01
#define IBI_STS_NOT_ATTEMPTED		0x11

#define DEVICE_CTRL_EXTENDED		0xb0
#define DEVICE_CTRL_EXTENDED_MODE_MASK	GENMASK(1, 0)
#define DEVICE_CTRL_EXTENDED_MODE(x)	((x) & DEVICE_CTRL_EXTENDED_MODE_MASK)
#define DEV_OPERATION_MODE_CONTROLLER	0x00
#define DEV_OPERATION_MODE_TARGET	0x01

#define SCL_I3C_OD_TIMING		0xb4
#define SCL_I3C_PP_TIMING		0xb8
#define   SCL_I3C_TIMING_HCNT		GENMASK(23, 16)
#define   SCL_I3C_TIMING_LCNT		GENMASK(7, 0)
#define     SCL_I3C_TIMING_CNT_MIN	5

#define SCL_I2C_FM_TIMING		0xbc
#define   SCL_I2C_FM_TIMING_HCNT	GENMASK(31, 16)
#define   SCL_I2C_FM_TIMING_LCNT	GENMASK(15, 0)

#define SCL_I2C_FMP_TIMING		0xc0
#define   SCL_I2C_FMP_TIMING_HCNT	GENMASK(23, 16)
#define   SCL_I2C_FMP_TIMING_LCNT	GENMASK(15, 0)

#define SCL_EXT_LCNT_TIMING		0xc8
#define SCL_EXT_LCNT_4(x)		(((x) << 24) & GENMASK(31, 24))
#define SCL_EXT_LCNT_3(x)		(((x) << 16) & GENMASK(23, 16))
#define SCL_EXT_LCNT_2(x)		(((x) << 8) & GENMASK(15, 8))
#define SCL_EXT_LCNT_1(x)		((x) & GENMASK(7, 0))

#define SCL_EXT_TERMN_LCNT_TIMING	0xcc
#define SDA_HOLD_SWITCH_DLY_TIMING	0xd0
#define   SDA_TX_HOLD			GENMASK(18, 16)
#define     SDA_TX_HOLD_MIN		0b001
#define     SDA_TX_HOLD_MAX		0b111
#define BUS_FREE_TIMING			0xd4
#define   BUS_AVAIL_TIME		GENMASK(31, 16)
#define     MAX_BUS_AVAIL_CNT		0xffffU
#define   BUS_I3C_MST_FREE		GENMASK(15, 0)

#define BUS_IDLE_TIMING			0xd8
#define SCL_LOW_MST_EXT_TIMEOUT		0xdc
#define I3C_VER_ID			0xe0
#define I3C_VER_TYPE			0xe4
#define EXTENDED_CAPABILITY		0xe8
#define SLAVE_CONFIG			0xec

#define DEV_ADDR_TABLE_LEGACY_I2C_DEV	BIT(31)
#define DEV_ADDR_TABLE_DYNAMIC_ADDR	GENMASK(23, 16)
#define DEV_ADDR_TABLE_SIR_REJECT	BIT(13)
#define DEV_ADDR_TABLE_IBI_MDB		BIT(12)
#define DEV_ADDR_TABLE_STATIC_ADDR	GENMASK(6, 0)
#define DEV_ADDR_TABLE_LOC(start, idx)	((start) + ((idx) << 2))

#define I3C_BUS_SDR1_SCL_RATE		8000000
#define I3C_BUS_SDR2_SCL_RATE		6000000
#define I3C_BUS_SDR3_SCL_RATE		4000000
#define I3C_BUS_SDR4_SCL_RATE		2000000
#define I3C_BUS_I2C_STD_SCL_RATE	100000
#define I3C_BUS_I2C_STD_TLOW_MIN_NS	4700
#define I3C_BUS_I2C_STD_THIGH_MIN_NS	4000
#define I3C_BUS_I2C_STD_TR_MAX_NS	1000
#define I3C_BUS_I2C_STD_TF_MAX_NS	300
#define I3C_BUS_I2C_FM_TLOW_MIN_NS	1300
#define I3C_BUS_I2C_FM_THIGH_MIN_NS	600
#define I3C_BUS_I2C_FM_TR_MAX_NS	300
#define I3C_BUS_I2C_FM_TF_MAX_NS	300
#define I3C_BUS_I2C_FMP_TLOW_MIN_NS	500
#define I3C_BUS_I2C_FMP_THIGH_MIN_NS	260
#define I3C_BUS_I2C_FMP_TR_MAX_NS	120
#define I3C_BUS_I2C_FMP_TF_MAX_NS	120
#define I3C_BUS_THIGH_MAX_NS		41

#define XFER_TIMEOUT (msecs_to_jiffies(1000))

#define JESD403_TIMED_RESET_NS_DEF	52428800

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
	struct dw_i3c_cmd cmds[] __counted_by(ncmds);
};

struct dw_i3c_i2c_dev_data {
	u8 index;
	struct i3c_generic_ibi_pool *ibi_pool;
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
	case I3C_CCC_SETAASA:
	case I3C_CCC_SETHID:
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

static void dw_i3c_master_set_iba(struct dw_i3c_master *master, bool enable)
{
	u32 reg;

	reg = readl(master->regs + DEVICE_CTRL);
	reg &= ~DEV_CTRL_IBA_INCLUDE;
	if (enable)
		reg |= DEV_CTRL_IBA_INCLUDE;

	writel(reg, master->regs + DEVICE_CTRL);
}

static void dw_i3c_master_disable(struct dw_i3c_master *master)
{
	if (!(readl(master->regs + DEVICE_CTRL) & DEV_CTRL_ENABLE))
		return;

	if (master->base.target)
		master->platform_ops->enter_sw_mode(master);

	writel(readl(master->regs + DEVICE_CTRL) & ~DEV_CTRL_ENABLE,
	       master->regs + DEVICE_CTRL);

	if (master->base.target) {
		master->platform_ops->toggle_scl_in(master, 8);
		master->platform_ops->gen_internal_stop(master);
		if (readl(master->regs + DEVICE_CTRL) & DEV_CTRL_ENABLE) {
			dev_warn(&master->base.dev,
				 "Failed to disable controller");
			master->platform_ops->exit_sw_mode(master);
			return;
		}
		master->platform_ops->exit_sw_mode(master);
	}
}

static void dw_i3c_master_enable(struct dw_i3c_master *master)
{
	u32 wait_enable_ns;

	if (master->base.target)
		master->platform_ops->enter_sw_mode(master);

	writel(readl(master->regs + DEVICE_CTRL) | DEV_CTRL_ENABLE,
	       master->regs + DEVICE_CTRL);

	if (master->base.target) {
		wait_enable_ns =
			master->timing.core_period *
			FIELD_GET(BUS_AVAIL_TIME,
				  readl(master->regs + BUS_FREE_TIMING));
		udelay(DIV_ROUND_UP(wait_enable_ns, NSEC_PER_USEC));

		master->platform_ops->toggle_scl_in(master, 8);
		if (!(readl(master->regs + DEVICE_CTRL) & DEV_CTRL_ENABLE)) {
			dev_warn(&master->base.dev,
				 "Failed to enable controller");
			master->platform_ops->exit_sw_mode(master);
			return;
		}

		master->platform_ops->gen_internal_stop(master);
		master->platform_ops->exit_sw_mode(master);
	}
}

static int dw_i3c_master_exit_halt(struct dw_i3c_master *master)
{
	u32 status;
	u32 halt_state = CM_TFR_STS_MASTER_HALT;
	int ret;

	if (master->base.target)
		halt_state = CM_TFR_STS_SLAVE_HALT;

	writel(readl(master->regs + DEVICE_CTRL) | DEV_CTRL_RESUME,
	       master->regs + DEVICE_CTRL);

	ret = readl_poll_timeout_atomic(master->regs + PRESENT_STATE, status,
					FIELD_GET(CM_TFR_STS, status) != halt_state,
					10, 1000000);

	if (ret)
		dev_err(&master->base.dev,
			"Exit halt state failed: %d %#x %#x\n", ret,
			readl(master->regs + PRESENT_STATE),
			readl(master->regs + QUEUE_STATUS_LEVEL));
	return ret;
}

static int dw_i3c_master_enter_halt(struct dw_i3c_master *master, bool by_sw)
{
	u32 status;
	u32 halt_state = CM_TFR_STS_MASTER_HALT;
	int ret;

	if (master->base.target)
		halt_state = CM_TFR_STS_SLAVE_HALT;

	if (by_sw)
		writel(readl(master->regs + DEVICE_CTRL) | DEV_CTRL_ABORT,
		       master->regs + DEVICE_CTRL);

	ret = readl_poll_timeout_atomic(master->regs + PRESENT_STATE, status,
					FIELD_GET(CM_TFR_STS, status) == halt_state,
					10, 1000000);

	if (ret)
		dev_err(&master->base.dev,
			"Enter halt state failed: %d %#x %#x\n", ret,
			readl(master->regs + PRESENT_STATE),
			readl(master->regs + QUEUE_STATUS_LEVEL));

	return ret;
}

static int dw_i3c_master_get_addr_pos(struct dw_i3c_master *master, u8 addr)
{
	int pos;

	for (pos = 0; pos < master->maxdevs; pos++) {
		if (addr == master->devs[pos].addr)
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

static void dw_i3c_master_read_fifo(struct dw_i3c_master *master,
				    int reg,  u8 *bytes, int nbytes)
{
	readsl(master->regs + reg, bytes, nbytes / 4);
	if (nbytes & 3) {
		u32 tmp;

		readsl(master->regs + reg, &tmp, 1);
		memcpy(bytes + (nbytes & ~3), &tmp, nbytes & 3);
	}
}

static void dw_i3c_master_read_rx_fifo(struct dw_i3c_master *master,
				       u8 *bytes, int nbytes)
{
	return dw_i3c_master_read_fifo(master, RX_TX_DATA_PORT, bytes, nbytes);
}

static void dw_i3c_master_read_ibi_fifo(struct dw_i3c_master *master,
					u8 *bytes, int nbytes)
{
	return dw_i3c_master_read_fifo(master, IBI_QUEUE_STATUS, bytes, nbytes);
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
		/*
		 * The controller will enter the HALT state if an error occurs.
		 * Therefore, there is no need to manually halt the controller
		 * through software.
		 */
		dw_i3c_master_enter_halt(master, false);
		dw_i3c_master_dequeue_xfer_locked(master, xfer);
		dw_i3c_master_exit_halt(master);
	}

	xfer = list_first_entry_or_null(&master->xferqueue.list,
					struct dw_i3c_xfer,
					node);
	if (xfer)
		list_del_init(&xfer->node);

	master->xferqueue.cur = xfer;
	dw_i3c_master_start_xfer_locked(master);
}

static int calc_i2c_clk(struct dw_i3c_master *master, unsigned long fscl,
			u16 *hcnt, u16 *lcnt)
{
	unsigned long core_rate, core_period;
	u32 period_cnt, margin;
	u32 hcnt_min, lcnt_min;

	core_rate = master->timing.core_rate;
	core_period = master->timing.core_period;

	if (fscl <= I3C_BUS_I2C_STD_SCL_RATE) {
		lcnt_min = DIV_ROUND_UP(I3C_BUS_I2C_STD_TLOW_MIN_NS +
						I3C_BUS_I2C_STD_TF_MAX_NS,
					core_period);
		hcnt_min = DIV_ROUND_UP(I3C_BUS_I2C_STD_THIGH_MIN_NS +
						I3C_BUS_I2C_STD_TR_MAX_NS,
					core_period);
	} else if (fscl <= I3C_BUS_I2C_FM_SCL_RATE) {
		lcnt_min = DIV_ROUND_UP(I3C_BUS_I2C_FM_TLOW_MIN_NS +
						I3C_BUS_I2C_FM_TF_MAX_NS,
					core_period);
		hcnt_min = DIV_ROUND_UP(I3C_BUS_I2C_FM_THIGH_MIN_NS +
						I3C_BUS_I2C_FM_TR_MAX_NS,
					core_period);
	} else {
		lcnt_min = DIV_ROUND_UP(I3C_BUS_I2C_FMP_TLOW_MIN_NS +
						I3C_BUS_I2C_FMP_TF_MAX_NS,
					core_period);
		hcnt_min = DIV_ROUND_UP(I3C_BUS_I2C_FMP_THIGH_MIN_NS +
						I3C_BUS_I2C_FMP_TR_MAX_NS,
					core_period);
	}

	period_cnt = DIV_ROUND_UP(core_rate, fscl);
	margin = (period_cnt - hcnt_min - lcnt_min) >> 1;
	*lcnt = lcnt_min + margin;
	*hcnt = max(period_cnt - *lcnt, hcnt_min);

	return 0;
}

static int dw_i3c_clk_cfg(struct dw_i3c_master *master)
{
	unsigned long core_rate, core_period;
	u32 scl_timing;
	u8 hcnt, lcnt;

	core_rate = master->timing.core_rate;
	core_period = master->timing.core_period;

	if (master->timing.i3c_pp_scl_high && master->timing.i3c_pp_scl_low) {
		hcnt = DIV_ROUND_CLOSEST(master->timing.i3c_pp_scl_high,
					 core_period);
		lcnt = DIV_ROUND_CLOSEST(master->timing.i3c_pp_scl_low,
					 core_period);
	} else {
		hcnt = DIV_ROUND_UP(I3C_BUS_THIGH_MAX_NS, core_period) - 1;
		if (hcnt < SCL_I3C_TIMING_CNT_MIN)
			hcnt = SCL_I3C_TIMING_CNT_MIN;

		lcnt = DIV_ROUND_UP(core_rate, master->base.bus.scl_rate.i3c) -
		       hcnt;
		if (lcnt < SCL_I3C_TIMING_CNT_MIN)
			lcnt = SCL_I3C_TIMING_CNT_MIN;
	}

	scl_timing = FIELD_PREP(SCL_I3C_TIMING_HCNT, hcnt) |
		     FIELD_PREP(SCL_I3C_TIMING_LCNT, lcnt);
	writel(scl_timing, master->regs + SCL_I3C_PP_TIMING);

	lcnt = DIV_ROUND_UP(core_rate, I3C_BUS_SDR1_SCL_RATE) - hcnt;
	scl_timing = SCL_EXT_LCNT_1(lcnt);
	lcnt = DIV_ROUND_UP(core_rate, I3C_BUS_SDR2_SCL_RATE) - hcnt;
	scl_timing |= SCL_EXT_LCNT_2(lcnt);
	lcnt = DIV_ROUND_UP(core_rate, I3C_BUS_SDR3_SCL_RATE) - hcnt;
	scl_timing |= SCL_EXT_LCNT_3(lcnt);
	lcnt = DIV_ROUND_UP(core_rate, I3C_BUS_SDR4_SCL_RATE) - hcnt;
	scl_timing |= SCL_EXT_LCNT_4(lcnt);
	writel(scl_timing, master->regs + SCL_EXT_LCNT_TIMING);

	if (master->timing.i3c_od_scl_high && master->timing.i3c_od_scl_low) {
		hcnt = DIV_ROUND_CLOSEST(master->timing.i3c_od_scl_high,
					 core_period);
		lcnt = DIV_ROUND_CLOSEST(master->timing.i3c_od_scl_low,
					 core_period);
	} else if (master->base.bus.context == I3C_BUS_CONTEXT_JESD403) {
		u16 hcnt_fmp, lcnt_fmp;

		calc_i2c_clk(master, I3C_BUS_I2C_FM_PLUS_SCL_RATE, &hcnt_fmp,
			     &lcnt_fmp);
		hcnt = min_t(u8, hcnt_fmp, FIELD_MAX(SCL_I3C_TIMING_HCNT));
		lcnt = min_t(u8, lcnt_fmp, FIELD_MAX(SCL_I3C_TIMING_LCNT));
	} else {
		lcnt = max_t(u8,
			     DIV_ROUND_UP(I3C_BUS_TLOW_OD_MIN_NS, core_period),
			     lcnt);
	}
	scl_timing = FIELD_PREP(SCL_I3C_TIMING_HCNT, hcnt) |
		     FIELD_PREP(SCL_I3C_TIMING_LCNT, lcnt);
	writel(scl_timing, master->regs + SCL_I3C_OD_TIMING);

	return 0;
}

static int dw_i2c_clk_cfg(struct dw_i3c_master *master)
{
	unsigned long core_rate, core_period;
	u16 hcnt, lcnt;
	u32 scl_timing;

	core_rate = master->timing.core_rate;
	core_period = master->timing.core_period;

	calc_i2c_clk(master, I3C_BUS_I2C_FM_PLUS_SCL_RATE, &hcnt, &lcnt);
	scl_timing = FIELD_PREP(SCL_I2C_FMP_TIMING_HCNT, hcnt) |
		     FIELD_PREP(SCL_I2C_FMP_TIMING_LCNT, lcnt);
	writel(scl_timing, master->regs + SCL_I2C_FMP_TIMING);

	calc_i2c_clk(master, master->base.bus.scl_rate.i2c, &hcnt, &lcnt);
	scl_timing = FIELD_PREP(SCL_I2C_FM_TIMING_HCNT, hcnt) |
		     FIELD_PREP(SCL_I2C_FM_TIMING_LCNT, lcnt);
	writel(scl_timing, master->regs + SCL_I2C_FM_TIMING);

	return 0;
}

static int dw_i3c_bus_clk_cfg(struct i3c_master_controller *m)
{
	struct dw_i3c_master *master = to_dw_i3c_master(m);
	struct i3c_bus *bus = i3c_master_get_bus(m);
	int ret;
	u16 lcnt;

	ret = dw_i2c_clk_cfg(master);
	if (ret)
		return ret;

	ret = dw_i3c_clk_cfg(master);
	if (ret)
		return ret;

	/*
	 * I3C register 0xd4[15:0] BUS_FREE_TIMING used to control several parameters:
	 * - tCAS & tCASr (tHD_STA in JESD403)
	 * - tCBP & tCBPr (tSU_STO in JESD403)
	 * - bus free time between a STOP condition and a START condition
	 *
	 * The constraints of these parameters differ in various bus contexts:
	 * MIPI I3C, mixed bus: BUS_FREE_TIMING = I2C FM SCL low period
	 * MIPI I3C, pure bus : BUS_FREE_TIMING = I3C PP SCL low period
	 * JESD403            : BUS_FREE_TIMING = I3C OD SCL low period
	 */
	if (bus->mode == I3C_BUS_MODE_PURE) {
		lcnt = FIELD_GET(SCL_I3C_TIMING_LCNT,
				 readl(master->regs + SCL_I3C_PP_TIMING));
	} else {
		writel(readl(master->regs + DEVICE_CTRL) | DEV_CTRL_I2C_SLAVE_PRESENT,
		       master->regs + DEVICE_CTRL);

		if (bus->context == I3C_BUS_CONTEXT_JESD403)
			lcnt = FIELD_GET(SCL_I3C_TIMING_LCNT,
					 readl(master->regs + SCL_I3C_OD_TIMING));
		else
			lcnt = FIELD_GET(SCL_I2C_FM_TIMING_LCNT,
					 readl(master->regs + SCL_I2C_FM_TIMING));
	}
	writel(FIELD_PREP(BUS_I3C_MST_FREE, lcnt),
	       master->regs + BUS_FREE_TIMING);

	return 0;
}

static int dw_i3c_target_bus_init(struct i3c_master_controller *m)
{
	struct dw_i3c_master *master = to_dw_i3c_master(m);
	struct i3c_dev_desc *desc = master->base.this;
	void *rx_buf;
	u32 reg;
	int ret;

	ret = dw_i3c_bus_clk_cfg(m);
	if (ret)
		return ret;

	reg = readl(master->regs + SLV_MAX_LEN);
	/*
	 * Set max private write length value based on read-only register.
	 * TODO: Handle updates after receiving SETMWL CCC.
	 */
	master->target.rx.max_len = SLV_MAX_WR_LEN(reg);

	rx_buf = kzalloc(master->target.rx.max_len, GFP_KERNEL);
	if (!rx_buf)
		return -ENOMEM;

	master->target.rx.buf = rx_buf;

	dw_i3c_master_disable(master);

	reg = readl(master->regs + QUEUE_THLD_CTRL) & ~QUEUE_THLD_CTRL_RESP_BUF_MASK;
	writel(reg, master->regs + QUEUE_THLD_CTRL);

	reg = readl(master->regs + DATA_BUFFER_THLD_CTRL) & ~DATA_BUFFER_THLD_CTRL_RX_BUF;
	writel(reg, master->regs + DATA_BUFFER_THLD_CTRL);

	writel(INTR_ALL, master->regs + INTR_STATUS);
	writel(INTR_TARGET_MASK, master->regs + INTR_STATUS_EN);
	writel(INTR_TARGET_MASK, master->regs + INTR_SIGNAL_EN);

	reg = readl(master->regs + DEVICE_CTRL_EXTENDED) & ~DEVICE_CTRL_EXTENDED_MODE_MASK;
	reg |= DEVICE_CTRL_EXTENDED_MODE(DEV_OPERATION_MODE_TARGET);
	writel(reg, master->regs + DEVICE_CTRL_EXTENDED);

	writel(SLV_PID_LO(desc->info.pid), master->regs + SLV_PID_VALUE);
	writel(SLV_PID_HI(desc->info.pid), master->regs + SLV_MIPI_ID_VALUE);

	reg = readl(master->regs + SLV_CHAR_CTRL);
	reg &= ~(SLV_DCR | SLV_BCR_DEVICE_ROLE);
	reg |= FIELD_PREP(SLV_DCR, desc->info.dcr) |
	       FIELD_PREP(SLV_BCR_DEVICE_ROLE, 0);
	writel(reg, master->regs + SLV_CHAR_CTRL);

	reg = FIELD_GET(SLV_BCR, reg);
	if (reg & I3C_BCR_IBI_PAYLOAD) {
		reg = readl(master->regs + DEVICE_CTRL);
		reg |= DEV_CTRL_IBI_PAYLOAD_EN;
		writel(reg, master->regs + DEVICE_CTRL);
	}

	reg = readl(master->regs + BUS_FREE_TIMING) |
	      FIELD_PREP(BUS_AVAIL_TIME, MAX_BUS_AVAIL_CNT);
	writel(reg, master->regs + BUS_FREE_TIMING);

	dw_i3c_master_enable(master);

	return 0;
}

static void dw_i3c_target_bus_cleanup(struct i3c_master_controller *m)
{
	struct dw_i3c_master *master = to_dw_i3c_master(m);

	dw_i3c_master_disable(master);
	kfree(master->target.rx.buf);
}

static int dw_i3c_master_bus_init(struct i3c_master_controller *m)
{
	struct dw_i3c_master *master = to_dw_i3c_master(m);
	struct i3c_device_info info = { };
	u32 thld_ctrl;
	int ret;

	ret = master->platform_ops->init(master);
	if (ret)
		return ret;

	spin_lock_init(&master->devs_lock);

	ret = dw_i3c_bus_clk_cfg(m);
	if (ret)
		return ret;

	thld_ctrl = readl(master->regs + QUEUE_THLD_CTRL);
	thld_ctrl &= ~(QUEUE_THLD_CTRL_RESP_BUF_MASK |
		       QUEUE_THLD_CTRL_IBI_STAT_MASK |
		       QUEUE_THLD_CTRL_IBI_STAT_MASK);
	thld_ctrl |= QUEUE_THLD_CTRL_IBI_STAT(1) |
		QUEUE_THLD_CTRL_IBI_DATA(31);
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

	writel(DEV_ADDR_DYNAMIC_ADDR_VALID | FIELD_PREP(DEV_ADDR_DYNAMIC, ret),
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

static void dw_i3c_master_bus_reset(struct i3c_master_controller *m)
{
	struct dw_i3c_master *master = to_dw_i3c_master(m);

	if (master->base.bus.context == I3C_BUS_CONTEXT_JESD403) {
		u32 reset = RESET_CTRL_BUS |
			    FIELD_PREP(RESET_CTRL_BUS_RESET_TYPE,
				       BUS_RESET_TYPE_SCL_LOW);
		u32 timeout = readl(master->regs + SCL_LOW_MST_EXT_TIMEOUT);
		u32 status;

		timeout = timeout * master->timing.core_period + 1000000;
		writel(reset, master->regs + RESET_CTRL);
		readl_poll_timeout_atomic(master->regs + RESET_CTRL, status,
					  !(status & RESET_CTRL_BUS), 10,
					  timeout);
	} else {
		master->platform_ops->gen_target_reset_pattern(master);
	}
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

	if (ccc->id == I3C_CCC_SETHID || ccc->id == I3C_CCC_DEVCTRL)
		cmd->cmd_lo |= COMMAND_PORT_SPEED(SPEED_I3C_I2C_FM);

	dw_i3c_master_enqueue_xfer(master, xfer);
	if (!wait_for_completion_timeout(&xfer->comp, XFER_TIMEOUT)) {
		dw_i3c_master_enter_halt(master, true);
		dw_i3c_master_dequeue_xfer(master, xfer);
		dw_i3c_master_exit_halt(master);
	}

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
	if (!wait_for_completion_timeout(&xfer->comp, XFER_TIMEOUT)) {
		dw_i3c_master_enter_halt(master, true);
		dw_i3c_master_dequeue_xfer(master, xfer);
		dw_i3c_master_exit_halt(master);
	}

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

		master->devs[pos].addr = ret;
		p = even_parity(ret);
		last_addr = ret;
		ret |= (p << 7);

		writel(FIELD_PREP(DEV_ADDR_TABLE_DYNAMIC_ADDR, ret),
		       master->regs +
		       DEV_ADDR_TABLE_LOC(master->datstartaddr, pos));
	}

	xfer = dw_i3c_master_alloc_xfer(master, 1);
	if (!xfer)
		return -ENOMEM;

	pos = dw_i3c_master_get_free_pos(master);
	if (pos < 0) {
		dw_i3c_master_free_xfer(xfer);
		return pos;
	}
	cmd = &xfer->cmds[0];
	cmd->cmd_hi = 0x1;
	cmd->cmd_lo = COMMAND_PORT_DEV_COUNT(master->maxdevs - pos) |
		      COMMAND_PORT_DEV_INDEX(pos) |
		      COMMAND_PORT_CMD(I3C_CCC_ENTDAA) |
		      COMMAND_PORT_ADDR_ASSGN_CMD |
		      COMMAND_PORT_TOC |
		      COMMAND_PORT_ROC;

	dw_i3c_master_enqueue_xfer(master, xfer);
	if (!wait_for_completion_timeout(&xfer->comp, XFER_TIMEOUT)) {
		dw_i3c_master_enter_halt(master, true);
		dw_i3c_master_dequeue_xfer(master, xfer);
		dw_i3c_master_exit_halt(master);
	}

	newdevs = GENMASK(master->maxdevs - cmd->rx_len - 1, 0);
	newdevs &= ~olddevs;

	for (pos = 0; pos < master->maxdevs; pos++) {
		if (newdevs & BIT(pos))
			i3c_master_add_i3c_dev_locked(m, master->devs[pos].addr);
	}

	dw_i3c_master_free_xfer(xfer);

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
	if (!wait_for_completion_timeout(&xfer->comp, XFER_TIMEOUT)) {
		dw_i3c_master_enter_halt(master, true);
		dw_i3c_master_dequeue_xfer(master, xfer);
		dw_i3c_master_exit_halt(master);
	}

	for (i = 0; i < i3c_nxfers; i++) {
		struct dw_i3c_cmd *cmd = &xfer->cmds[i];

		if (i3c_xfers[i].rnw)
			i3c_xfers[i].len = cmd->rx_len;
	}

	ret = xfer->ret;
	dw_i3c_master_free_xfer(xfer);

	return ret;
}

static int dw_i3c_target_priv_xfers(struct i3c_dev_desc *dev,
				    struct i3c_priv_xfer *i3c_xfers,
				    int i3c_nxfers)
{
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct dw_i3c_master *master = to_dw_i3c_master(m);
	struct dw_i3c_xfer *xfer;
	int i;

	if (!i3c_nxfers)
		return 0;

	xfer = dw_i3c_master_alloc_xfer(master, i3c_nxfers);
	if (!xfer)
		return -ENOMEM;

	for (i = 0; i < i3c_nxfers; i++) {
		struct dw_i3c_cmd *cmd = &xfer->cmds[i];

		if (!i3c_xfers[i].rnw) {
			cmd->tx_buf = i3c_xfers[i].data.out;
			cmd->tx_len = i3c_xfers[i].len;
			cmd->cmd_lo = 0 | (i << 3) | (cmd->tx_len << 16);

			dw_i3c_master_wr_tx_fifo(master, cmd->tx_buf, cmd->tx_len);
			writel(cmd->cmd_lo, master->regs + COMMAND_QUEUE_PORT);
		}
	}

	dw_i3c_master_free_xfer(xfer);

	return 0;
}

static int dw_i3c_target_generate_ibi(struct i3c_dev_desc *dev, const u8 *data, int len)
{
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct dw_i3c_master *master = to_dw_i3c_master(m);
	u32 reg;

	if (data || len != 0)
		return -EOPNOTSUPP;

	reg = readl(master->regs + SLV_EVENT_CTRL);
	if ((reg & SLV_EVENT_CTRL_SIR_EN) == 0)
		return -EPERM;

	init_completion(&master->target.comp);
	writel(1, master->regs + SLV_INTR_REQ);

	if (!wait_for_completion_timeout(&master->target.comp, XFER_TIMEOUT)) {
		dev_warn(&master->base.dev, "Timeout waiting for completion\n");
		return -EINVAL;
	}

	reg = readl(master->regs + SLV_INTR_REQ);
	if (SLV_INTR_REQ_IBI_STS(reg) != IBI_STS_ACCEPTED) {
		reg = readl(master->regs + SLV_EVENT_CTRL);
		if ((reg & SLV_EVENT_CTRL_SIR_EN) == 0)
			dev_warn(&master->base.dev, "SIR is disabled by master\n");
		return -EACCES;
	}

	return 0;
}

static int dw_i3c_target_reset_queue(struct dw_i3c_master *master)
{
	int ret;
	u32 status;

	dw_i3c_master_disable(master);
	writel(RESET_CTRL_XFER_QUEUES, master->regs + RESET_CTRL);
	ret = readl_poll_timeout_atomic(master->regs + RESET_CTRL, status,
					!status, 10, 1000000);
	if (ret)
		dev_err(&master->base.dev, "Reset %#x failed: %d\n", status,
			ret);

	dw_i3c_master_enable(master);

	return ret;
}

static int dw_i3c_target_pending_read_notify(struct i3c_dev_desc *dev,
					     struct i3c_priv_xfer *pending_read,
					     struct i3c_priv_xfer *ibi_notify)
{
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct dw_i3c_master *master = to_dw_i3c_master(m);
	struct dw_i3c_xfer *xfer;
	struct dw_i3c_cmd *cmd;
	int ret;
	u32 reg;
	u8 mdb;

	if (!pending_read || !ibi_notify)
		return -EINVAL;

	reg = readl(master->regs + SLV_EVENT_CTRL);
	if ((reg & SLV_EVENT_CTRL_SIR_EN) == 0)
		return -EPERM;

	xfer = dw_i3c_master_alloc_xfer(master, 2);
	if (!xfer)
		return -ENOMEM;

	mdb = *(u8 *)ibi_notify->data.out;
	master->platform_ops->set_ibi_mdb(master, mdb);

	/* Put IBI command & data into the command & data queues */
	cmd = &xfer->cmds[0];
	cmd->tx_buf = ibi_notify->data.out;
	cmd->tx_len = ibi_notify->len;
	cmd->cmd_lo = COMMAND_PORT_TID(TID_TARGET_IBI) | (cmd->tx_len << 16);
	dw_i3c_master_wr_tx_fifo(master, cmd->tx_buf, cmd->tx_len);
	writel(cmd->cmd_lo, master->regs + COMMAND_QUEUE_PORT);

	/* Put pending-read command & data into the command & data queues */
	cmd = &xfer->cmds[1];
	cmd->tx_buf = pending_read->data.out;
	cmd->tx_len = pending_read->len;
	cmd->cmd_lo = COMMAND_PORT_TID(TID_TARGET_RD_DATA) |
		      (cmd->tx_len << 16);
	dw_i3c_master_wr_tx_fifo(master, cmd->tx_buf, cmd->tx_len);
	writel(cmd->cmd_lo, master->regs + COMMAND_QUEUE_PORT);

	dw_i3c_master_free_xfer(xfer);
	init_completion(&master->target.rdata_comp);

	ret = dw_i3c_target_generate_ibi(dev, NULL, 0);
	if (ret) {
		dev_warn(&master->base.dev, "Timeout waiting for completion: IBI MDB\n");
		dw_i3c_target_reset_queue(master);
		return -EINVAL;
	}

	if (!wait_for_completion_timeout(&master->target.rdata_comp,
					 XFER_TIMEOUT)) {
		dev_warn(&master->base.dev, "Timeout waiting for completion: pending read data\n");
		dw_i3c_target_reset_queue(master);
		return -EINVAL;
	}

	return 0;
}

static bool dw_i3c_target_is_ibi_enabled(struct i3c_dev_desc *dev)
{
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct dw_i3c_master *master = to_dw_i3c_master(m);
	u32 reg;

	reg = readl(master->regs + SLV_EVENT_CTRL);
	return !!(reg & SLV_EVENT_CTRL_SIR_EN);
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

		master->devs[data->index].addr = 0;
		master->free_pos |= BIT(data->index);

		data->index = pos;
		master->devs[pos].addr = dev->info.dyn_addr;
		master->free_pos &= ~BIT(pos);
	}

	writel(FIELD_PREP(DEV_ADDR_TABLE_DYNAMIC_ADDR, dev->info.dyn_addr),
	       master->regs +
	       DEV_ADDR_TABLE_LOC(master->datstartaddr, data->index));

	master->devs[data->index].addr = dev->info.dyn_addr;

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
	master->devs[pos].addr = dev->info.dyn_addr ? : dev->info.static_addr;
	master->free_pos &= ~BIT(pos);
	i3c_dev_set_master_data(dev, data);

	writel(FIELD_PREP(DEV_ADDR_TABLE_DYNAMIC_ADDR, master->devs[pos].addr),
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
	master->devs[data->index].addr = 0;
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

	if (ntxwords == 0 && nrxwords == 0) {
		dev_warn(&master->base.dev,
			 "Transfers w/o data bytes are not supported");
		return -ENOTSUPP;
	}

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
	if (!wait_for_completion_timeout(&xfer->comp, XFER_TIMEOUT)) {
		dw_i3c_master_enter_halt(master, true);
		dw_i3c_master_dequeue_xfer(master, xfer);
		dw_i3c_master_exit_halt(master);
	}

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
	master->devs[pos].addr = dev->addr;
	master->free_pos &= ~BIT(pos);
	i2c_dev_set_master_data(dev, data);

	writel(DEV_ADDR_TABLE_LEGACY_I2C_DEV |
	       FIELD_PREP(DEV_ADDR_TABLE_STATIC_ADDR, dev->addr),
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
	master->devs[data->index].addr = 0;
	master->free_pos |= BIT(data->index);
	kfree(data);
}

static int dw_i3c_master_request_ibi(struct i3c_dev_desc *dev,
				     const struct i3c_ibi_setup *req)
{
	struct dw_i3c_i2c_dev_data *data = i3c_dev_get_master_data(dev);
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct dw_i3c_master *master = to_dw_i3c_master(m);
	unsigned long flags;

	data->ibi_pool = i3c_generic_ibi_alloc_pool(dev, req);
	if (IS_ERR(data->ibi_pool))
		return PTR_ERR(data->ibi_pool);

	spin_lock_irqsave(&master->devs_lock, flags);
	master->devs[data->index].ibi_dev = dev;
	spin_unlock_irqrestore(&master->devs_lock, flags);

	return 0;
}

static void dw_i3c_master_free_ibi(struct i3c_dev_desc *dev)
{
	struct dw_i3c_i2c_dev_data *data = i3c_dev_get_master_data(dev);
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct dw_i3c_master *master = to_dw_i3c_master(m);
	unsigned long flags;

	spin_lock_irqsave(&master->devs_lock, flags);
	master->devs[data->index].ibi_dev = NULL;
	spin_unlock_irqrestore(&master->devs_lock, flags);

	i3c_generic_ibi_free_pool(data->ibi_pool);
	data->ibi_pool = NULL;
}

/* Enable/Disable the IBI interrupt signal and status */
static void dw_i3c_master_set_ibi_signal(struct dw_i3c_master *master, bool enable)
{
	u32 reg;

	reg = readl(master->regs + INTR_STATUS_EN);
	reg &= ~INTR_IBI_THLD_STAT;
	if (enable)
		reg |= INTR_IBI_THLD_STAT;
	writel(reg, master->regs + INTR_STATUS_EN);

	reg = readl(master->regs + INTR_SIGNAL_EN);
	reg &= ~INTR_IBI_THLD_STAT;
	if (enable)
		reg |= INTR_IBI_THLD_STAT;
	writel(reg, master->regs + INTR_SIGNAL_EN);
}

static void dw_i3c_master_set_sir_enabled(struct dw_i3c_master *master,
					  struct i3c_dev_desc *dev,
					  u8 idx, bool enable)
{
	unsigned long flags;
	u32 dat_entry, reg;
	bool global;

	dat_entry = DEV_ADDR_TABLE_LOC(master->datstartaddr, idx);

	spin_lock_irqsave(&master->devs_lock, flags);
	reg = readl(master->regs + dat_entry);
	if (enable) {
		reg &= ~DEV_ADDR_TABLE_SIR_REJECT;
		if (dev->info.bcr & I3C_BCR_IBI_PAYLOAD)
			reg |= DEV_ADDR_TABLE_IBI_MDB;
	} else {
		reg |= DEV_ADDR_TABLE_SIR_REJECT;
	}
	master->platform_ops->set_dat_ibi(master, dev, enable, &reg);
	writel(reg, master->regs + dat_entry);

	reg = readl(master->regs + IBI_SIR_REQ_REJECT);
	if (enable) {
		global = reg == 0xffffffff;
		reg &= ~BIT(idx);
	} else {
		bool hj_rejected = !!(readl(master->regs + DEVICE_CTRL) & DEV_CTRL_HOT_JOIN_NACK);

		reg |= BIT(idx);
		global = (reg == 0xffffffff) && hj_rejected;
	}
	writel(reg, master->regs + IBI_SIR_REQ_REJECT);

	if (global)
		dw_i3c_master_set_ibi_signal(master, enable);


	spin_unlock_irqrestore(&master->devs_lock, flags);
}

static int dw_i3c_master_enable_hotjoin(struct i3c_master_controller *m)
{
	struct dw_i3c_master *master = to_dw_i3c_master(m);

	dw_i3c_master_set_ibi_signal(master, true);
	writel(readl(master->regs + DEVICE_CTRL) & ~DEV_CTRL_HOT_JOIN_NACK,
	       master->regs + DEVICE_CTRL);

	return 0;
}

static int dw_i3c_master_disable_hotjoin(struct i3c_master_controller *m)
{
	struct dw_i3c_master *master = to_dw_i3c_master(m);

	writel(readl(master->regs + DEVICE_CTRL) | DEV_CTRL_HOT_JOIN_NACK,
	       master->regs + DEVICE_CTRL);

	return 0;
}

static int dw_i3c_master_enable_ibi(struct i3c_dev_desc *dev)
{
	struct dw_i3c_i2c_dev_data *data = i3c_dev_get_master_data(dev);
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct dw_i3c_master *master = to_dw_i3c_master(m);
	int rc;

	dw_i3c_master_set_sir_enabled(master, dev, data->index, true);

	rc = i3c_master_enec_locked(m, dev->info.dyn_addr, I3C_CCC_EVENT_SIR);

	if (rc)
		dw_i3c_master_set_sir_enabled(master, dev, data->index, false);

	return rc;
}

static int dw_i3c_master_disable_ibi(struct i3c_dev_desc *dev)
{
	struct dw_i3c_i2c_dev_data *data = i3c_dev_get_master_data(dev);
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct dw_i3c_master *master = to_dw_i3c_master(m);

	i3c_master_disec_locked(m, dev->info.dyn_addr, I3C_CCC_EVENT_SIR);
	dw_i3c_master_set_sir_enabled(master, dev, data->index, false);

	return 0;
}

static void dw_i3c_master_recycle_ibi_slot(struct i3c_dev_desc *dev,
					   struct i3c_ibi_slot *slot)
{
	struct dw_i3c_i2c_dev_data *data = i3c_dev_get_master_data(dev);

	i3c_generic_ibi_recycle_slot(data->ibi_pool, slot);
}

static void dw_i3c_master_drain_ibi_queue(struct dw_i3c_master *master,
					  int len)
{
	int i;

	for (i = 0; i < DIV_ROUND_UP(len, 4); i++)
		readl(master->regs + IBI_QUEUE_STATUS);
}

static void dw_i3c_master_handle_ibi_sir(struct dw_i3c_master *master,
					 u32 status)
{
	struct dw_i3c_i2c_dev_data *data;
	struct i3c_ibi_slot *slot;
	struct i3c_dev_desc *dev;
	unsigned long flags;
	u8 addr, len;
	int idx;

	addr = IBI_QUEUE_IBI_ADDR(status);
	len = IBI_QUEUE_STATUS_DATA_LEN(status);

	/*
	 * We be tempted to check the error status in bit 30; however, due
	 * to the PEC errata workaround on some platform implementations (see
	 * ast2600_i3c_set_dat_ibi()), those will almost always have a PEC
	 * error on IBI payload data, as well as losing the last byte of
	 * payload.
	 *
	 * If we implement error status checking on that bit, we may need
	 * a new platform op to validate it.
	 */

	spin_lock_irqsave(&master->devs_lock, flags);
	idx = dw_i3c_master_get_addr_pos(master, addr);
	if (idx < 0) {
		dev_dbg_ratelimited(&master->base.dev,
			 "IBI from unknown addr 0x%x\n", addr);
		goto err_drain;
	}

	dev = master->devs[idx].ibi_dev;
	if (!dev || !dev->ibi) {
		dev_dbg_ratelimited(&master->base.dev,
			 "IBI from non-requested dev idx %d\n", idx);
		goto err_drain;
	}

	data = i3c_dev_get_master_data(dev);
	slot = i3c_generic_ibi_get_free_slot(data->ibi_pool);
	if (!slot) {
		dev_dbg_ratelimited(&master->base.dev,
				    "No IBI slots available\n");
		goto err_drain;
	}

	if (dev->ibi->max_payload_len < len) {
		dev_dbg_ratelimited(&master->base.dev,
				    "IBI payload len %d greater than max %d\n",
				    len, dev->ibi->max_payload_len);
		goto err_drain;
	}

	if (len) {
		dw_i3c_master_read_ibi_fifo(master, slot->data, len);
		slot->len = len;
	}
	i3c_master_queue_ibi(dev, slot);

	spin_unlock_irqrestore(&master->devs_lock, flags);

	return;

err_drain:
	dw_i3c_master_drain_ibi_queue(master, len);

	spin_unlock_irqrestore(&master->devs_lock, flags);
}

/* "ibis": referring to In-Band Interrupts, and not
 * https://en.wikipedia.org/wiki/Australian_white_ibis. The latter should
 * not be handled.
 */
static void dw_i3c_master_irq_handle_ibis(struct dw_i3c_master *master)
{
	unsigned int i, len, n_ibis;
	u32 reg;

	reg = readl(master->regs + QUEUE_STATUS_LEVEL);
	n_ibis = QUEUE_STATUS_IBI_STATUS_CNT(reg);
	if (!n_ibis)
		return;

	for (i = 0; i < n_ibis; i++) {
		reg = readl(master->regs + IBI_QUEUE_STATUS);

		if (IBI_TYPE_SIRQ(reg)) {
			dw_i3c_master_handle_ibi_sir(master, reg);
		} else if (IBI_TYPE_HJ(reg)) {
			queue_work(master->base.wq, &master->hj_work);
		} else {
			len = IBI_QUEUE_STATUS_DATA_LEN(reg);
			dev_info(&master->base.dev,
				 "unsupported IBI type 0x%lx len %d\n",
				 IBI_QUEUE_STATUS_IBI_ID(reg), len);
			dw_i3c_master_drain_ibi_queue(master, len);
		}
	}
}

static void dw_i3c_target_handle_ccc_update(struct dw_i3c_master *master)
{
	u32 event = readl(master->regs + SLV_EVENT_CTRL);
	u32 reg = readl(master->regs + SLV_MAX_LEN);
	u32 present_state = readl(master->regs + PRESENT_STATE);

	if (event & SLV_EVENT_CTRL_MRL_UPD)
		master->base.this->info.max_read_len = SLV_MAX_RD_LEN(reg);

	if (event & SLV_EVENT_CTRL_MWL_UPD) {
		master->base.this->info.max_write_len = SLV_MAX_WR_LEN(reg);
		master->target.rx.max_len =
			master->base.this->info.max_write_len;
	}
	writel(event, master->regs + SLV_EVENT_CTRL);

	/* The I3C engine would get into halt-state if it receives SETMRL/MWL CCCs */
	if (FIELD_GET(CM_TFR_STS, present_state) == CM_TFR_STS_SLAVE_HALT)
		dw_i3c_master_exit_halt(master);

	writel(INTR_CCC_UPDATED_STAT, master->regs + INTR_STATUS);
}

static void dw_i3c_target_handle_response_ready(struct dw_i3c_master *master)
{
	struct i3c_dev_desc *desc = master->base.this;
	u32 reg = readl(master->regs + QUEUE_STATUS_LEVEL);
	u32 nresp = QUEUE_STATUS_LEVEL_RESP(reg);
	int i;

	for (i = 0; i < nresp; i++) {
		u32 resp = readl(master->regs + RESPONSE_QUEUE_PORT);
		u32 nbytes = RESPONSE_PORT_DATA_LEN(resp);
		u8 tid = RESPONSE_PORT_TID(resp);

		if (nbytes > master->target.rx.max_len) {
			dev_warn(&master->base.dev, "private write data length is larger than max\n");
			return;
		}

		dw_i3c_master_read_rx_fifo(master, master->target.rx.buf, nbytes);

		if (tid == TID_TARGET_MASTER_WR_DATA && desc->target_info.read_handler)
			desc->target_info.read_handler(desc->dev, master->target.rx.buf, nbytes);
		else if (tid == TID_TARGET_RD_DATA)
			complete(&master->target.rdata_comp);
	}
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

	if (master->base.target) {
		if (status & INTR_DYN_ADDR_ASSGN_STAT) {
			u32 reg = readl(master->regs + DEVICE_ADDR);

			master->base.this->info.dyn_addr =
				FIELD_GET(DEV_ADDR_DYNAMIC, reg);
			writel(INTR_DYN_ADDR_ASSGN_STAT,
			       master->regs + INTR_STATUS);
		}

		if (status & INTR_CCC_UPDATED_STAT)
			dw_i3c_target_handle_ccc_update(master);

		if (status & INTR_IBI_UPDATED_STAT) {
			writel(INTR_IBI_UPDATED_STAT, master->regs + INTR_STATUS);
			complete(&master->target.comp);
		}

		if (status & INTR_READ_REQ_RECV_STAT) {
			/*
			 * TODO: Pass this information to the driver to take
			 * appropriate action.
			 */
			dev_dbg(&master->base.dev,
				"private read received from controller when cmd queue is empty\n");
			writel(INTR_READ_REQ_RECV_STAT, master->regs + INTR_STATUS);
		}

		if (status & INTR_RESP_READY_STAT)
			dw_i3c_target_handle_response_ready(master);
	}

	spin_lock(&master->xferqueue.lock);
	dw_i3c_master_end_xfer_locked(master, status);
	if (status & INTR_TRANSFER_ERR_STAT)
		writel(INTR_TRANSFER_ERR_STAT, master->regs + INTR_STATUS);
	spin_unlock(&master->xferqueue.lock);

	if (status & INTR_IBI_THLD_STAT)
		dw_i3c_master_irq_handle_ibis(master);

	return IRQ_HANDLED;
}

static const struct i3c_target_ops dw_mipi_i3c_target_ops = {
	.bus_init = dw_i3c_target_bus_init,
	.bus_cleanup = dw_i3c_target_bus_cleanup,
	.priv_xfers = dw_i3c_target_priv_xfers,
	.generate_ibi = dw_i3c_target_generate_ibi,
	.pending_read_notify = dw_i3c_target_pending_read_notify,
	.is_ibi_enabled = dw_i3c_target_is_ibi_enabled,
};

static const struct i3c_master_controller_ops dw_mipi_i3c_ops = {
	.bus_init = dw_i3c_master_bus_init,
	.bus_cleanup = dw_i3c_master_bus_cleanup,
	.bus_reset = dw_i3c_master_bus_reset,
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

static const struct i3c_master_controller_ops dw_mipi_i3c_ibi_ops = {
	.bus_init = dw_i3c_master_bus_init,
	.bus_cleanup = dw_i3c_master_bus_cleanup,
	.bus_reset = dw_i3c_master_bus_reset,
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
	.request_ibi = dw_i3c_master_request_ibi,
	.free_ibi = dw_i3c_master_free_ibi,
	.enable_ibi = dw_i3c_master_enable_ibi,
	.disable_ibi = dw_i3c_master_disable_ibi,
	.recycle_ibi_slot = dw_i3c_master_recycle_ibi_slot,
	.enable_hotjoin = dw_i3c_master_enable_hotjoin,
	.disable_hotjoin = dw_i3c_master_disable_hotjoin,
};

/* default platform ops implementations */
static int dw_i3c_platform_init_nop(struct dw_i3c_master *i3c)
{
	return 0;
}

static void dw_i3c_platform_set_dat_ibi_nop(struct dw_i3c_master *i3c,
					struct i3c_dev_desc *dev,
					bool enable, u32 *dat)
{
}

static void dw_i3c_platform_enter_sw_mode_nop(struct dw_i3c_master *i3c)
{
}

static void dw_i3c_platform_exit_sw_mode_nop(struct dw_i3c_master *i3c)
{
}

static void dw_i3c_toggle_scl_in_nop(struct dw_i3c_master *i3c, int count)
{
}

static void dw_i3c_gen_internal_stop_nop(struct dw_i3c_master *i3c)
{
}

static void dw_i3c_gen_target_reset_pattern_nop(struct dw_i3c_master *i3c)
{
}

static void dw_i3c_set_ibi_mdb_nop(struct dw_i3c_master *i3c, u8 mdb)
{
}

static const struct dw_i3c_platform_ops dw_i3c_platform_ops_default = {
	.init = dw_i3c_platform_init_nop,
	.set_dat_ibi = dw_i3c_platform_set_dat_ibi_nop,
	.enter_sw_mode = dw_i3c_platform_enter_sw_mode_nop,
	.exit_sw_mode = dw_i3c_platform_exit_sw_mode_nop,
	.toggle_scl_in = dw_i3c_toggle_scl_in_nop,
	.gen_internal_stop = dw_i3c_gen_internal_stop_nop,
	.gen_target_reset_pattern = dw_i3c_gen_target_reset_pattern_nop,
	.set_ibi_mdb = dw_i3c_set_ibi_mdb_nop,
};

static int dw_i3c_of_populate_bus_timing(struct dw_i3c_master *master,
					 struct device_node *np)
{
	u32 val, reg, sda_tx_hold_ns, timed_reset_scl_low_ns;

	master->timing.core_rate = clk_get_rate(master->core_clk);
	if (!master->timing.core_rate) {
		dev_err(&master->base.dev, "core clock rate not found\n");
		return -EINVAL;
	}

	/* core_period is in nanosecond */
	master->timing.core_period =
		DIV_ROUND_UP(1000000000, master->timing.core_rate);

	/* Parse configurations from the device tree */
	if (!of_property_read_u32(np, "i3c-pp-scl-hi-period-ns", &val))
		master->timing.i3c_pp_scl_high = val;

	if (!of_property_read_u32(np, "i3c-pp-scl-lo-period-ns", &val))
		master->timing.i3c_pp_scl_low = val;

	if (!of_property_read_u32(np, "i3c-od-scl-hi-period-ns", &val))
		master->timing.i3c_od_scl_high = val;

	if (!of_property_read_u32(np, "i3c-od-scl-lo-period-ns", &val))
		master->timing.i3c_od_scl_low = val;

	sda_tx_hold_ns = SDA_TX_HOLD_MIN * master->timing.core_period;
	if (!of_property_read_u32(np, "sda-tx-hold-ns", &val))
		sda_tx_hold_ns = val;

	timed_reset_scl_low_ns = JESD403_TIMED_RESET_NS_DEF;
	if (!of_property_read_u32(np, "timed-reset-scl-low-ns", &val))
		timed_reset_scl_low_ns = val;

	val = clamp((u32)DIV_ROUND_CLOSEST(sda_tx_hold_ns,
					   master->timing.core_period),
		    (u32)SDA_TX_HOLD_MIN, (u32)SDA_TX_HOLD_MAX);
	reg = readl(master->regs + SDA_HOLD_SWITCH_DLY_TIMING);
	reg &= ~SDA_TX_HOLD;
	reg |= FIELD_PREP(SDA_TX_HOLD, val);
	writel(reg, master->regs + SDA_HOLD_SWITCH_DLY_TIMING);

	val = DIV_ROUND_CLOSEST(timed_reset_scl_low_ns,
				master->timing.core_period);
	writel(val, master->regs + SCL_LOW_MST_EXT_TIMEOUT);

	return 0;
}

static void dw_i3c_hj_work(struct work_struct *work)
{
	struct dw_i3c_master *master =
		container_of(work, typeof(*master), hj_work);

	i3c_master_do_daa(&master->base);
}

int dw_i3c_common_probe(struct dw_i3c_master *master,
			struct platform_device *pdev)
{
	const struct i3c_master_controller_ops *ops;
	struct device_node *np;
	int ret, irq;

	if (!master->platform_ops)
		master->platform_ops = &dw_i3c_platform_ops_default;

	master->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(master->regs))
		return PTR_ERR(master->regs);

	master->core_clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(master->core_clk))
		return PTR_ERR(master->core_clk);

	master->core_rst = devm_reset_control_get_optional_exclusive(&pdev->dev,
								    NULL);
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

	np = pdev->dev.of_node;
	ret = dw_i3c_of_populate_bus_timing(master, np);
	if (ret)
		goto err_assert_rst;

	/* Information regarding the FIFOs/QUEUEs depth */
	ret = readl(master->regs + QUEUE_STATUS_LEVEL);
	master->caps.cmdfifodepth = QUEUE_STATUS_LEVEL_CMD(ret);

	ret = readl(master->regs + DATA_BUFFER_STATUS_LEVEL);
	master->caps.datafifodepth = DATA_BUFFER_STATUS_LEVEL_TX(ret);

	ret = readl(master->regs + DEVICE_ADDR_TABLE_POINTER);
	master->datstartaddr = ret;
	master->maxdevs = ret >> 16;
	master->free_pos = GENMASK(master->maxdevs - 1, 0);

	ops = &dw_mipi_i3c_ops;
	if (master->ibi_capable)
		ops = &dw_mipi_i3c_ibi_ops;

	INIT_WORK(&master->hj_work, dw_i3c_hj_work);
	ret = i3c_register(&master->base, &pdev->dev, ops,
			   &dw_mipi_i3c_target_ops, false);
	if (ret)
		goto err_assert_rst;

	if (!master->base.target && master->base.bus.context != I3C_BUS_CONTEXT_JESD403) {
		dw_i3c_master_set_iba(master, true);
		dw_i3c_master_enable_hotjoin(&master->base);
	}

	return 0;

err_assert_rst:
	reset_control_assert(master->core_rst);

err_disable_core_clk:
	clk_disable_unprepare(master->core_clk);

	return ret;
}
EXPORT_SYMBOL_GPL(dw_i3c_common_probe);

void dw_i3c_common_remove(struct dw_i3c_master *master)
{
	i3c_unregister(&master->base);

	reset_control_assert(master->core_rst);

	clk_disable_unprepare(master->core_clk);
}
EXPORT_SYMBOL_GPL(dw_i3c_common_remove);

/* base platform implementation */

static int dw_i3c_probe(struct platform_device *pdev)
{
	struct dw_i3c_master *master;

	master = devm_kzalloc(&pdev->dev, sizeof(*master), GFP_KERNEL);
	if (!master)
		return -ENOMEM;

	return dw_i3c_common_probe(master, pdev);
}

static void dw_i3c_remove(struct platform_device *pdev)
{
	struct dw_i3c_master *master = platform_get_drvdata(pdev);

	dw_i3c_common_remove(master);
}

static const struct of_device_id dw_i3c_master_of_match[] = {
	{ .compatible = "snps,dw-i3c-master-1.00a", },
	{},
};
MODULE_DEVICE_TABLE(of, dw_i3c_master_of_match);

static struct platform_driver dw_i3c_driver = {
	.probe = dw_i3c_probe,
	.remove_new = dw_i3c_remove,
	.driver = {
		.name = "dw-i3c-master",
		.of_match_table = dw_i3c_master_of_match,
	},
};
module_platform_driver(dw_i3c_driver);

MODULE_AUTHOR("Vitor Soares <vitor.soares@synopsys.com>");
MODULE_DESCRIPTION("DesignWare MIPI I3C driver");
MODULE_LICENSE("GPL v2");
