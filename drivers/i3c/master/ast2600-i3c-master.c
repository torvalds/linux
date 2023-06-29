// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2021 ASPEED Technology Inc.
 *
 * Derived from dw-i3c-master.c by Vitor Soares <vitor.soares@synopsys.com>
 */

#include <linux/bitops.h>
#include <linux/bitfield.h>
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
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>

#define I3C_CHANNEL_MAX 5

#define DEVICE_CTRL			0x0
#define DEV_CTRL_ENABLE			BIT(31)
#define DEV_CTRL_RESUME			BIT(30)
#define DEV_CTRL_AUTO_HJ_DISABLE	BIT(27)
#define DEV_CTRL_SLAVE_MDB		GENMASK(23, 16)
#define DEV_CTRL_SLAVE_PEC_EN		BIT(10)
#define DEV_CRTL_IBI_PAYLOAD_EN		BIT(9)
#define DEV_CTRL_HOT_JOIN_NACK		BIT(8)
#define DEV_CTRL_I2C_SLAVE_PRESENT	BIT(7)
#define DEV_CTRL_IBA_INCLUDE		BIT(0)

#define DEVICE_ADDR			0x4
#define DEV_ADDR_DYNAMIC_ADDR_VALID	BIT(31)
#define DEV_ADDR_DYNAMIC(x)		(((x) << 16) & GENMASK(22, 16))
#define DEV_ADDR_STATIC_ADDR_VALID	BIT(15)
#define DEV_ADDR_STATIC(x)		(((x) << 0) & GENMASK(6, 0))

#define HW_CAPABILITY			0x8
#define COMMAND_QUEUE_PORT		0xc
#define COMMAND_PORT_PEC		BIT(31)
#define COMMAND_PORT_TOC		BIT(30)
#define COMMAND_PORT_READ_TRANSFER	BIT(28)
#define COMMAND_PORT_SDAP		BIT(27)
#define COMMAND_PORT_ROC		BIT(26)
#define COMMAND_PORT_DBP(x)		((x) << 25)
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

#define COMMAND_PORT_ARG_DBP(x)		(((x) << 8) & GENMASK(15, 8))
#define COMMAND_PORT_ARG_DATA_LEN(x)	(((x) << 16) & GENMASK(31, 16))
#define COMMAND_PORT_ARG_DATA_LEN_MAX	65536
#define COMMAND_PORT_TRANSFER_ARG	0x01

#define COMMAND_ATTR_SLAVE_DATA		0x0
#define COMMAND_PORT_SLAVE_TID(x)      (((x) << 3) & GENMASK(5, 3))
#define COMMAND_PORT_SLAVE_DATA_LEN	GENMASK(31, 16)

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
#define RESPONSE_ERROR_EARLY_TERMINATE	10
#define RESPONSE_ERROR_PEC_ERR		12
#define RESPONSE_PORT_TID(x)		(((x) & GENMASK(27, 24)) >> 24)
#define   TID_SLAVE_IBI_DONE		0b0001
#define   TID_MASTER_READ_DATA		0b0010
#define   TID_MASTER_WRITE_DATA		0b1000
#define   TID_CCC_WRITE_DATA		0b1111
#define RESPONSE_PORT_DATA_LEN(x)	((x) & GENMASK(15, 0))

#define RX_TX_DATA_PORT			0x14
#define IBI_QUEUE_STATUS		0x18
#define IBI_QUEUE_STATUS_RSP_NACK	BIT(31)
#define IBI_QUEUE_STATUS_PEC_ERR	BIT(30)
#define IBI_QUEUE_STATUS_LAST_FRAG	BIT(24)
#define IBI_QUEUE_STATUS_IBI_ID(x)	(((x) & GENMASK(15, 8)) >> 8)
#define IBI_QUEUE_STATUS_DATA_LEN(x)	((x) & GENMASK(7, 0))

#define IBI_QUEUE_IBI_ADDR(x)		(IBI_QUEUE_STATUS_IBI_ID(x) >> 1)
#define IBI_QUEUE_IBI_RNW(x)		(IBI_QUEUE_STATUS_IBI_ID(x) & BIT(0))
#define IBI_TYPE_MR(x)                                                         \
	((IBI_QUEUE_IBI_ADDR(x) != I3C_HOT_JOIN_ADDR) && !IBI_QUEUE_IBI_RNW(x))
#define IBI_TYPE_HJ(x)                                                         \
	((IBI_QUEUE_IBI_ADDR(x) == I3C_HOT_JOIN_ADDR) && !IBI_QUEUE_IBI_RNW(x))
#define IBI_TYPE_SIR(x)                                                        \
	((IBI_QUEUE_IBI_ADDR(x) != I3C_HOT_JOIN_ADDR) && IBI_QUEUE_IBI_RNW(x))

#define IBI_QUEUE_DATA			0x18
#define QUEUE_THLD_CTRL			0x1c
#define QUEUE_THLD_CTRL_IBI_STA_MASK	GENMASK(31, 24)
#define QUEUE_THLD_CTRL_IBI_STA(x)	(((x) - 1) << 24)
#define QUEUE_THLD_CTRL_IBI_DAT_MASK	GENMASK(23, 16)
#define QUEUE_THLD_CTRL_IBI_DAT(x)	((x) << 16)
#define QUEUE_THLD_CTRL_RESP_BUF_MASK	GENMASK(15, 8)
#define QUEUE_THLD_CTRL_RESP_BUF(x)	(((x) - 1) << 8)

#define DATA_BUFFER_THLD_CTRL		0x20
#define DATA_BUFFER_THLD_CTRL_RX_BUF	GENMASK(11, 8)

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
#define RESET_CTRL_ALL                  (RESET_CTRL_IBI_QUEUE	              |\
					 RESET_CTRL_RX_FIFO	              |\
					 RESET_CTRL_TX_FIFO	              |\
					 RESET_CTRL_RESP_QUEUE	              |\
					 RESET_CTRL_CMD_QUEUE	              |\
					 RESET_CTRL_SOFT)
#define RESET_CTRL_QUEUES		(RESET_CTRL_IBI_QUEUE	              |\
					 RESET_CTRL_RX_FIFO	              |\
					 RESET_CTRL_TX_FIFO	              |\
					 RESET_CTRL_RESP_QUEUE	              |\
					 RESET_CTRL_CMD_QUEUE)

#define SLV_EVENT_CTRL			0x38
#define SLV_EVENT_CTRL_MWL_UPD		BIT(7)
#define SLV_EVENT_CTRL_MRL_UPD		BIT(6)
#define SLV_EVENT_CTRL_SIR_EN		BIT(0)
#define SLV_EVETN_CTRL_W1C_MASK		(SLV_EVENT_CTRL_MWL_UPD |\
					 SLV_EVENT_CTRL_MRL_UPD)

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
#define INTR_2ND_MASTER_MASK		(INTR_TRANSFER_ERR_STAT |	\
					 INTR_RESP_READY_STAT	|	\
					 INTR_IBI_UPDATED_STAT  |	\
					 INTR_CCC_UPDATED_STAT)
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
#define SLV_MIPI_PID_VALUE		0x70
#define PID_MANUF_ID_ASPEED		0x03f6

#define SLV_PID_VALUE			0x74
#define SLV_PID_PART_ID(x)		(((x) << 16) & GENMASK(31, 16))
#define SLV_PID_INST_ID(x)		(((x) << 12) & GENMASK(15, 12))
#define SLV_PID_DCR(x)			((x) & GENMASK(11, 0))

#define PID_PART_ID_AST2600_SERIES	0x0500
#define PID_PART_ID_AST1030_A0		0x8000

#define SLV_CHAR_CTRL			0x78
#define SLV_CHAR_GET_DCR(x)		(((x) & GENMASK(15, 8)) >> 8)
#define SLV_CHAR_GET_BCR(x)		(((x) & GENMASK(7, 0)) >> 0)
#define SLV_MAX_LEN			0x7c
#define MAX_READ_TURNAROUND		0x80
#define MAX_DATA_SPEED			0x84
#define SLV_DEBUG_STATUS		0x88
#define SLV_INTR_REQ			0x8c
#define SLV_INTR_REQ_IBI_STS(x)		((x) & GENMASK(9, 8) >> 8)
#define SLV_IBI_STS_OK			0x1

#define DEVICE_CTRL_EXTENDED		0xb0
#define DEVICE_CTRL_ROLE_MASK		GENMASK(1, 0)
#define DEVICE_CTRL_ROLE_MASTER		0
#define DEVICE_CTRL_ROLE_SLAVE		1
#define SCL_I3C_OD_TIMING		0xb4
#define SCL_I3C_PP_TIMING		0xb8
#define SCL_I3C_TIMING_HCNT		GENMASK(23, 16)
#define SCL_I3C_TIMING_LCNT		GENMASK(7, 0)
#define SCL_I3C_TIMING_CNT_MIN		5

#define SCL_I2C_FM_TIMING		0xbc
#define SCL_I2C_FM_TIMING_HCNT		GENMASK(31, 16)
#define SCL_I2C_FM_TIMING_LCNT		GENMASK(15, 0)

#define SCL_I2C_FMP_TIMING		0xc0
#define SCL_I2C_FMP_TIMING_HCNT		GENMASK(23, 16)
#define SCL_I2C_FMP_TIMING_LCNT		GENMASK(15, 0)

#define SCL_EXT_LCNT_TIMING		0xc8
#define SCL_EXT_LCNT_4(x)		(((x) << 24) & GENMASK(31, 24))
#define SCL_EXT_LCNT_3(x)		(((x) << 16) & GENMASK(23, 16))
#define SCL_EXT_LCNT_2(x)		(((x) << 8) & GENMASK(15, 8))
#define SCL_EXT_LCNT_1(x)		((x) & GENMASK(7, 0))

#define SCL_EXT_TERMN_LCNT_TIMING	0xcc
#define SDA_HOLD_SWITCH_DLY_TIMING	0xd0
#define SDA_TX_HOLD			GENMASK(18, 16)
#define   SDA_TX_HOLD_MIN		0b001
#define   SDA_TX_HOLD_MAX		0b111
#define SDA_PP_OD_SWITCH_DLY		GENMASK(10, 8)
#define SDA_OD_PP_SWITCH_DLY		GENMASK(2, 0)
#define BUS_FREE_TIMING			0xd4
#define BUS_I3C_AVAILABLE_TIME(x)	(((x) << 16) & GENMASK(31, 16))
#define BUS_I3C_MST_FREE(x)		((x) & GENMASK(15, 0))

#define BUS_IDLE_TIMING			0xd8
#define SCL_LOW_MST_EXT_TIMEOUT		0xdc
#define I3C_VER_ID			0xe0
#define I3C_VER_TYPE			0xe4
#define EXTENDED_CAPABILITY		0xe8
#define SLAVE_CONFIG			0xec

#define DEV_ADDR_TABLE_LEGACY_I2C_DEV	BIT(31)
#define DEV_ADDR_TABLE_DEV_NACK_RETRY	GENMASK(30, 29)
#define DEV_ADDR_TABLE_IBI_ADDR_MASK	GENMASK(25, 24)
#define   IBI_ADDR_MASK_OFF		0b00
#define   IBI_ADDR_MASK_LAST_3BITS	0b01
#define   IBI_ADDR_MASK_LAST_4BITS	0b10
#define DEV_ADDR_TABLE_DA_PARITY	BIT(23)
#define DEV_ADDR_TABLE_DYNAMIC_ADDR	GENMASK(22, 16)
#define DEV_ADDR_TABLE_MR_REJECT	BIT(14)
#define DEV_ADDR_TABLE_SIR_REJECT	BIT(13)
#define DEV_ADDR_TABLE_IBI_WITH_DATA	BIT(12)
#define DEV_ADDR_TABLE_IBI_PEC_EN	BIT(11)
#define DEV_ADDR_TABLE_STATIC_ADDR	GENMASK(6, 0)

#define DEV_ADDR_TABLE_LOC(start, idx)	((start) + ((idx) << 2))
#define GET_DAT_FROM_POS(_master, _pos)                                        \
	(readl(_master->regs + DEV_ADDR_TABLE_LOC(_master->datstartaddr, _pos)))

#define MAX_DEVS			128
#define MAX_IBI_FRAG_SIZE		124

#define I3C_BUS_SDR1_SCL_RATE		8000000
#define I3C_BUS_SDR2_SCL_RATE		6000000
#define I3C_BUS_SDR3_SCL_RATE		4000000
#define I3C_BUS_SDR4_SCL_RATE		2000000
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
#define I3C_BUS_JESD403_PP_TLOW_MIN_NS	35
#define I3C_BUS_JESD403_PP_THIGH_MIN_NS	35
#define I3C_BUS_JESD403_PP_TR_MAX_NS	5
#define I3C_BUS_JESD403_PP_TF_MAX_NS	5
#define I3C_BUS_THIGH_MAX_NS		41

#define I3C_BUS_EXT_TERMN_CNT		4
#define JESD403_TIMED_RESET_NS_DEF	52428800

#define XFER_TIMEOUT			(msecs_to_jiffies(1000))

#define ast_setbits(x, set)		writel(readl(x) | (set), x)
#define ast_clrbits(x, clr)		writel(readl(x) & ~(clr), x)
#define ast_clrsetbits(x, clr, set)	writel((readl(x) & ~(clr)) | (set), x)

#define MAX_GROUPS			(1 << 4)
#define MAX_DEVS_IN_GROUP		(1 << 3)
#define ALL_DEVS_IN_GROUP_ARE_FREE	((1 << MAX_DEVS_IN_GROUP) - 1)
#define ADDR_GRP_SHIFT			3
#define ADDR_GRP_MASK			GENMASK(6, ADDR_GRP_SHIFT)
#define ADDR_GRP(x)			(((x) & ADDR_GRP_MASK) >> ADDR_GRP_SHIFT)
#define ADDR_HID_MASK			GENMASK(ADDR_GRP_SHIFT - 1, 0)
#define ADDR_HID(x)			((x) & ADDR_HID_MASK)

struct aspeed_i3c_master_caps {
	u8 cmdfifodepth;
	u8 datafifodepth;
};

struct aspeed_i3c_cmd {
	u32 cmd_lo;
	u32 cmd_hi;
	u16 tx_len;
	const void *tx_buf;
	u16 rx_len;
	void *rx_buf;
	u8 error;
};

struct aspeed_i3c_xfer {
	struct list_head node;
	struct completion comp;
	int ret;
	unsigned int ncmds;
	struct aspeed_i3c_cmd cmds[];
};

struct aspeed_i3c_dev_group {
	u32 dat[8];
	u32 free_pos;
	int hw_index;
	struct {
		u32 set;
		u32 clr;
	} mask;
};

struct aspeed_i3c_master {
	struct device *dev;
	struct i3c_master_controller base;
	struct regmap *i3cg;
	u16 maxdevs;
	u16 datstartaddr;
	u32 free_pos;
	struct aspeed_i3c_dev_group dev_group[MAX_GROUPS];
	struct {
		struct list_head list;
		struct aspeed_i3c_xfer *cur;
		spinlock_t lock;
	} xferqueue;
	struct {
		struct i3c_dev_desc *slots[MAX_DEVS];
		u32 received_ibi_len[MAX_DEVS];
		spinlock_t lock;
	} ibi;
	struct aspeed_i3c_master_caps caps;
	void __iomem *regs;
	struct reset_control *core_rst;
	struct clk *core_clk;
	char version[5];
	char type[5];
	u8 addrs[MAX_DEVS];
	u32 channel;
	bool secondary;
	struct {
		u32 *buf;
		void (*callback)(struct i3c_master_controller *m,
				 const struct i3c_slave_payload *payload);
	} slave_data;
	struct completion sir_complete;
	struct completion data_read_complete;

	struct {
		unsigned long core_rate;
		unsigned long core_period;
		u32 i3c_od_scl_freq;
		u32 i3c_od_scl_low;
		u32 i3c_od_scl_high;
		u32 i3c_pp_scl_freq;
		u32 i3c_pp_scl_low;
		u32 i3c_pp_scl_high;
	} timing;
	struct work_struct hj_work;
};

struct aspeed_i3c_i2c_dev_data {
	struct i3c_generic_ibi_pool *ibi_pool;
	u8 index;
	s8 ibi;
};

static u8 even_parity(u8 p)
{
	p ^= p >> 4;
	p &= 0xf;

	return (0x9669 >> p) & 1;
}

#define I3CG_REG1(x)			((x * 0x10) + 0x14)
#define SDA_OUT_SW_MODE_EN		BIT(31)
#define SCL_OUT_SW_MODE_EN		BIT(30)
#define SDA_IN_SW_MODE_EN		BIT(29)
#define SCL_IN_SW_MODE_EN		BIT(28)
#define SDA_IN_SW_MODE_VAL		BIT(27)
#define SDA_OUT_SW_MODE_VAL		BIT(25)
#define SDA_SW_MODE_OE			BIT(24)
#define SCL_IN_SW_MODE_VAL		BIT(23)
#define SCL_OUT_SW_MODE_VAL		BIT(21)
#define SCL_SW_MODE_OE			BIT(20)

static void aspeed_i3c_isolate_scl_sda(struct aspeed_i3c_master *master, bool iso)
{
	if (iso) {
		regmap_write_bits(master->i3cg, I3CG_REG1(master->channel),
				  SCL_IN_SW_MODE_VAL | SDA_IN_SW_MODE_VAL,
				  SCL_IN_SW_MODE_VAL | SDA_IN_SW_MODE_VAL);
		regmap_write_bits(master->i3cg, I3CG_REG1(master->channel),
				  SCL_IN_SW_MODE_EN | SDA_IN_SW_MODE_EN,
				  SCL_IN_SW_MODE_EN | SDA_IN_SW_MODE_EN);
	} else {
		regmap_write_bits(master->i3cg, I3CG_REG1(master->channel),
				  SCL_IN_SW_MODE_EN | SDA_IN_SW_MODE_EN, 0);
	}
}

static void aspeed_i3c_toggle_scl_in(struct aspeed_i3c_master *master, u32 times)
{
	for (; times; times--) {
		regmap_write_bits(master->i3cg, I3CG_REG1(master->channel),
				  SCL_IN_SW_MODE_VAL, 0);
		regmap_write_bits(master->i3cg, I3CG_REG1(master->channel),
				  SCL_IN_SW_MODE_VAL, SCL_IN_SW_MODE_VAL);
	}
}

static void aspeed_i3c_gen_stop_to_internal(struct aspeed_i3c_master *master)
{
	regmap_write_bits(master->i3cg, I3CG_REG1(master->channel),
			  SCL_IN_SW_MODE_VAL, SCL_IN_SW_MODE_VAL);
	regmap_write_bits(master->i3cg, I3CG_REG1(master->channel),
			  SDA_IN_SW_MODE_VAL, 0);
	regmap_write_bits(master->i3cg, I3CG_REG1(master->channel),
			  SDA_IN_SW_MODE_VAL, SDA_IN_SW_MODE_VAL);
}

static bool aspeed_i3c_fsm_is_idle(struct aspeed_i3c_master *master)
{
	/*
	 * Clear the IBI queue to enable the hardware to generate SCL and
	 * begin detecting the T-bit low to stop reading IBI data.
	 */
	readl(master->regs + IBI_QUEUE_DATA);
	if (FIELD_GET(CM_TFR_STS, readl(master->regs + PRESENT_STATE)))
		return false;
	return true;
}

static void aspeed_i3c_gen_tbits_in(struct aspeed_i3c_master *master)
{
	bool is_idle;
	int ret;

	regmap_write_bits(master->i3cg, I3CG_REG1(master->channel),
			  SDA_IN_SW_MODE_VAL, SDA_IN_SW_MODE_VAL);
	regmap_write_bits(master->i3cg, I3CG_REG1(master->channel),
			  SDA_IN_SW_MODE_EN, SDA_IN_SW_MODE_EN);

	regmap_write_bits(master->i3cg, I3CG_REG1(master->channel),
			  SDA_IN_SW_MODE_VAL, 0);
	ret = readx_poll_timeout_atomic(aspeed_i3c_fsm_is_idle, master, is_idle,
					is_idle, 0, 2000000);
	regmap_write_bits(master->i3cg, I3CG_REG1(master->channel),
			  SDA_IN_SW_MODE_EN, 0);
	if (ret)
		dev_err(master->dev,
			"Failed to recovery the i3c fsm from %lx to idle: %d",
			FIELD_GET(CM_TFR_STS,
				  readl(master->regs + PRESENT_STATE)),
			ret);
}

static bool aspeed_i3c_master_supports_ccc_cmd(struct i3c_master_controller *m,
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

static inline struct aspeed_i3c_master *
to_aspeed_i3c_master(struct i3c_master_controller *master)
{
	return container_of(master, struct aspeed_i3c_master, base);
}

static void aspeed_i3c_master_iba_ctrl(struct aspeed_i3c_master *master, bool ctrl)
{
	ctrl ? writel(readl(master->regs + DEVICE_CTRL) | DEV_CTRL_IBA_INCLUDE,
		      master->regs + DEVICE_CTRL) :
	       writel(readl(master->regs + DEVICE_CTRL) & ~DEV_CTRL_IBA_INCLUDE,
		      master->regs + DEVICE_CTRL);
}

static int aspeed_i3c_master_disable(struct aspeed_i3c_master *master)
{
	if (master->secondary)
		aspeed_i3c_isolate_scl_sda(master, true);
	writel(readl(master->regs + DEVICE_CTRL) & ~DEV_CTRL_ENABLE,
	       master->regs + DEVICE_CTRL);
	if (master->secondary) {
		aspeed_i3c_toggle_scl_in(master, 8);
		if (readl(master->regs + DEVICE_CTRL) & DEV_CTRL_ENABLE) {
			dev_warn(master->dev,
					"Failed to disable controller");
			aspeed_i3c_isolate_scl_sda(master, false);
			return -EACCES;
		}
		aspeed_i3c_isolate_scl_sda(master, false);
	}
	return 0;
}

static int aspeed_i3c_master_enable(struct aspeed_i3c_master *master)
{
	u32 wait_enable_us;

	if (master->secondary)
		aspeed_i3c_isolate_scl_sda(master, true);
	writel(readl(master->regs + DEVICE_CTRL) | DEV_CTRL_ENABLE,
	       master->regs + DEVICE_CTRL);
	if (master->secondary) {
		wait_enable_us =
			DIV_ROUND_UP(master->timing.core_period *
					     FIELD_GET(GENMASK(31, 16),
						       readl(master->regs +
							     BUS_FREE_TIMING)),
				     NSEC_PER_USEC);
		udelay(wait_enable_us);
		aspeed_i3c_toggle_scl_in(master, 8);
		if (!(readl(master->regs + DEVICE_CTRL) & DEV_CTRL_ENABLE)) {
			dev_warn(master->dev, "Failed to enable controller");
			aspeed_i3c_isolate_scl_sda(master, false);
			return -EACCES;
		}
		aspeed_i3c_gen_stop_to_internal(master);
		aspeed_i3c_isolate_scl_sda(master, false);
	}

	return 0;
}

static void aspeed_i3c_master_resume(struct aspeed_i3c_master *master)
{
	writel(readl(master->regs + DEVICE_CTRL) | DEV_CTRL_RESUME,
	       master->regs + DEVICE_CTRL);
}

static void aspeed_i3c_master_set_role(struct aspeed_i3c_master *master)
{
	u32 reg;
	u32 role = DEVICE_CTRL_ROLE_MASTER;

	if (master->secondary)
		role = DEVICE_CTRL_ROLE_SLAVE;

	reg = readl(master->regs + DEVICE_CTRL_EXTENDED);
	reg = (reg & ~DEVICE_CTRL_ROLE_MASK) | role;
	writel(reg, master->regs + DEVICE_CTRL_EXTENDED);
}

static int aspeed_i3c_master_get_free_pos(struct aspeed_i3c_master *master)
{
	if (!(master->free_pos & GENMASK(master->maxdevs - 1, 0)))
		return -ENOSPC;

	return ffs(master->free_pos) - 1;
}

static void aspeed_i3c_master_init_group_dat(struct aspeed_i3c_master *master)
{
	struct aspeed_i3c_dev_group *dev_grp;
	int i, j;
	u32 def_set, def_clr;

	def_clr = DEV_ADDR_TABLE_IBI_ADDR_MASK;

	/* For now don't support Hot-Join */
	def_set = DEV_ADDR_TABLE_MR_REJECT | DEV_ADDR_TABLE_SIR_REJECT;

	for (i = 0; i < MAX_GROUPS; i++) {
		dev_grp = &master->dev_group[i];
		dev_grp->hw_index = -1;
		dev_grp->free_pos = ALL_DEVS_IN_GROUP_ARE_FREE;
		dev_grp->mask.set = def_set;
		dev_grp->mask.clr = def_clr;
		for (j = 0; j < MAX_DEVS_IN_GROUP; j++)
			dev_grp->dat[j] = 0;
	}

	for (i = 0; i < master->maxdevs; i++)
		writel(def_set,
		       master->regs +
			       DEV_ADDR_TABLE_LOC(master->datstartaddr, i));
}

static int aspeed_i3c_master_set_group_dat(struct aspeed_i3c_master *master, u8 addr,
				       u32 val)
{
	struct aspeed_i3c_dev_group *dev_grp = &master->dev_group[ADDR_GRP(addr)];
	u8 idx = ADDR_HID(addr);

	val &= ~DEV_ADDR_TABLE_DA_PARITY;
	val |= FIELD_PREP(DEV_ADDR_TABLE_DA_PARITY, even_parity(addr));
	dev_grp->dat[idx] = val;

	if (val) {
		dev_grp->free_pos &= ~BIT(idx);

		/*
		 * reserve the hw dat resource for the first member of the
		 * group. all the members in the group share the same hw dat.
		 */
		if (dev_grp->hw_index == -1) {
			dev_grp->hw_index = aspeed_i3c_master_get_free_pos(master);
			if (dev_grp->hw_index < 0)
				goto out;

			master->free_pos &= ~BIT(dev_grp->hw_index);
			val &= dev_grp->mask.clr;
			val |= dev_grp->mask.set;
			writel(val, master->regs + DEV_ADDR_TABLE_LOC(
							   master->datstartaddr,
							   dev_grp->hw_index));
		}
	} else {
		dev_grp->free_pos |= BIT(idx);

		/*
		 * release the hw dat resource if all the members in the group
		 * are free.
		 */
		if (dev_grp->free_pos == ALL_DEVS_IN_GROUP_ARE_FREE) {
			writel(dev_grp->mask.set,
			       master->regs +
				       DEV_ADDR_TABLE_LOC(master->datstartaddr,
							  dev_grp->hw_index));
			master->free_pos |= BIT(dev_grp->hw_index);
			dev_grp->hw_index = -1;
		}
	}
out:
	return dev_grp->hw_index;
}

static u32 aspeed_i3c_master_get_group_dat(struct aspeed_i3c_master *master, u8 addr)
{
	struct aspeed_i3c_dev_group *dev_grp = &master->dev_group[ADDR_GRP(addr)];

	return dev_grp->dat[ADDR_HID(addr)];
}

static int aspeed_i3c_master_get_group_hw_index(struct aspeed_i3c_master *master,
					    u8 addr)
{
	struct aspeed_i3c_dev_group *dev_grp = &master->dev_group[ADDR_GRP(addr)];

	return dev_grp->hw_index;
}

static struct aspeed_i3c_dev_group *
aspeed_i3c_master_get_group(struct aspeed_i3c_master *master, u8 addr)
{
	return &master->dev_group[ADDR_GRP(addr)];
}

static int aspeed_i3c_master_sync_hw_dat(struct aspeed_i3c_master *master, u8 addr)
{
	struct aspeed_i3c_dev_group *dev_grp = &master->dev_group[ADDR_GRP(addr)];
	u32 dat = dev_grp->dat[ADDR_HID(addr)];
	int hw_index = dev_grp->hw_index;

	if (!dat || hw_index < 0)
		return -1;

	dat &= ~dev_grp->mask.clr;
	dat |= dev_grp->mask.set;
	writel(dat, master->regs +
			    DEV_ADDR_TABLE_LOC(master->datstartaddr, hw_index));
	return hw_index;
}

static void aspeed_i3c_master_wr_tx_fifo(struct aspeed_i3c_master *master,
				     const u8 *bytes, int nbytes)
{
	/*
	 * ensure all memory accesses are done before we move the data from
	 * memory to the hardware FIFO
	 */
	wmb();

	writesl(master->regs + RX_TX_DATA_PORT, bytes, nbytes / 4);
	if (nbytes & 3) {
		u32 tmp = 0;

		memcpy(&tmp, bytes + (nbytes & ~3), nbytes & 3);
		writesl(master->regs + RX_TX_DATA_PORT, &tmp, 1);
		dev_dbg(master->dev, "TX data = %08x\n", tmp);
	}
}

static void aspeed_i3c_master_read_fifo(struct aspeed_i3c_master *master, u32 fifo_reg,
				    u8 *bytes, int nbytes)
{
	readsl(master->regs + fifo_reg, bytes, nbytes / 4);
	if (nbytes & 3) {
		u32 tmp;

		readsl(master->regs + fifo_reg, &tmp, 1);
		memcpy(bytes + (nbytes & ~3), &tmp, nbytes & 3);
	}
}

static void aspeed_i3c_master_read_rx_fifo(struct aspeed_i3c_master *master,
					      u8 *bytes, int nbytes)
{
	aspeed_i3c_master_read_fifo(master, RX_TX_DATA_PORT, bytes, nbytes);
}

static void aspeed_i3c_master_read_ibi_fifo(struct aspeed_i3c_master *master,
					       u8 *bytes, int nbytes)
{
	aspeed_i3c_master_read_fifo(master, IBI_QUEUE_DATA, bytes, nbytes);
}

static struct aspeed_i3c_xfer *
aspeed_i3c_master_alloc_xfer(struct aspeed_i3c_master *master, unsigned int ncmds)
{
	struct aspeed_i3c_xfer *xfer;

	xfer = kzalloc(struct_size(xfer, cmds, ncmds), GFP_KERNEL);
	if (!xfer)
		return NULL;

	INIT_LIST_HEAD(&xfer->node);
	xfer->ncmds = ncmds;
	xfer->ret = -ETIMEDOUT;

	return xfer;
}

static void aspeed_i3c_master_free_xfer(struct aspeed_i3c_xfer *xfer)
{
	kfree(xfer);
}

static void aspeed_i3c_master_start_xfer_locked(struct aspeed_i3c_master *master)
{
	struct aspeed_i3c_xfer *xfer = master->xferqueue.cur;
	unsigned int i;
	u32 thld_ctrl;

	if (!xfer)
		return;

	for (i = 0; i < xfer->ncmds; i++) {
		struct aspeed_i3c_cmd *cmd = &xfer->cmds[i];

		aspeed_i3c_master_wr_tx_fifo(master, cmd->tx_buf, cmd->tx_len);
	}

	thld_ctrl = readl(master->regs + QUEUE_THLD_CTRL);
	thld_ctrl &= ~QUEUE_THLD_CTRL_RESP_BUF_MASK;
	thld_ctrl |= QUEUE_THLD_CTRL_RESP_BUF(xfer->ncmds);
	writel(thld_ctrl, master->regs + QUEUE_THLD_CTRL);

	for (i = 0; i < xfer->ncmds; i++) {
		struct aspeed_i3c_cmd *cmd = &xfer->cmds[i];

		writel(cmd->cmd_hi, master->regs + COMMAND_QUEUE_PORT);
		writel(cmd->cmd_lo, master->regs + COMMAND_QUEUE_PORT);
	}
}

static void aspeed_i3c_master_enqueue_xfer(struct aspeed_i3c_master *master,
				       struct aspeed_i3c_xfer *xfer)
{
	unsigned long flags;

	init_completion(&xfer->comp);
	spin_lock_irqsave(&master->xferqueue.lock, flags);
	if (master->xferqueue.cur) {
		list_add_tail(&xfer->node, &master->xferqueue.list);
	} else {
		master->xferqueue.cur = xfer;
		aspeed_i3c_master_start_xfer_locked(master);
	}
	spin_unlock_irqrestore(&master->xferqueue.lock, flags);
}

static void aspeed_i3c_master_dequeue_xfer_locked(struct aspeed_i3c_master *master,
					      struct aspeed_i3c_xfer *xfer)
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

static void aspeed_i3c_master_dequeue_xfer(struct aspeed_i3c_master *master,
				       struct aspeed_i3c_xfer *xfer)
{
	unsigned long flags;

	spin_lock_irqsave(&master->xferqueue.lock, flags);
	aspeed_i3c_master_dequeue_xfer_locked(master, xfer);
	spin_unlock_irqrestore(&master->xferqueue.lock, flags);
}

static void aspeed_i3c_master_sir_handler(struct aspeed_i3c_master *master,
				      u32 ibi_status)
{
	struct aspeed_i3c_i2c_dev_data *data;
	struct i3c_dev_desc *dev;
	struct i3c_ibi_slot *slot;
	u8 addr = IBI_QUEUE_IBI_ADDR(ibi_status);
	u8 length = IBI_QUEUE_STATUS_DATA_LEN(ibi_status);
	u8 *buf;
	bool data_consumed = false;

	dev = master->ibi.slots[addr];
	if (!dev) {
		pr_warn("no matching dev\n");
		goto out;
	}

	spin_lock(&master->ibi.lock);
	data = i3c_dev_get_master_data(dev);
	slot = i3c_generic_ibi_get_free_slot(data->ibi_pool);
	if (!slot) {
		pr_err("no free ibi slot\n");
		goto out_unlock;
	}
	master->ibi.received_ibi_len[addr] += length;
	if (master->ibi.received_ibi_len[addr] >
	    slot->dev->ibi->max_payload_len) {
		pr_err("received ibi payload %d > device requested buffer %d",
		       master->ibi.received_ibi_len[addr],
		       slot->dev->ibi->max_payload_len);
		goto out_unlock;
	}
	if (ibi_status & IBI_QUEUE_STATUS_LAST_FRAG)
		master->ibi.received_ibi_len[addr] = 0;
	buf = slot->data;
	/* prepend ibi status */
	memcpy(buf, &ibi_status, sizeof(ibi_status));
	buf += sizeof(ibi_status);

	aspeed_i3c_master_read_ibi_fifo(master, buf, length);
	slot->len = length + sizeof(ibi_status);
	i3c_master_queue_ibi(dev, slot);
	data_consumed = true;
out_unlock:
	spin_unlock(&master->ibi.lock);

out:
	/* Consume data from the FIFO if it's not been done already. */
	if (!data_consumed) {
		int nwords = (length + 3) >> 2;
		int i;

		for (i = 0; i < nwords; i++)
			readl(master->regs + IBI_QUEUE_DATA);
		if (FIELD_GET(CM_TFR_STS,
			      readl(master->regs + PRESENT_STATE)) ==
		    CM_TFR_STS_MASTER_SERV_IBI)
			aspeed_i3c_gen_tbits_in(master);
		master->ibi.received_ibi_len[addr] = 0;
	}
}

static void aspeed_i3c_master_demux_ibis(struct aspeed_i3c_master *master)
{
	u32 nibi, status;
	int i;
	u8 addr;

	nibi = readl(master->regs + QUEUE_STATUS_LEVEL);
	nibi = QUEUE_STATUS_IBI_STATUS_CNT(nibi);
	if (!nibi)
		return;

	for (i = 0; i < nibi; i++) {
		status = readl(master->regs + IBI_QUEUE_STATUS);
		addr = IBI_QUEUE_IBI_ADDR(status);

		/* FIXME: how to handle the unrecognized slave? */
		if (status & IBI_QUEUE_STATUS_RSP_NACK)
			pr_warn_once("ibi from unrecognized slave %02x\n",
				     addr);

		if (IBI_TYPE_SIR(status))
			aspeed_i3c_master_sir_handler(master, status);

		if (IBI_TYPE_HJ(status))
			queue_work(master->base.wq, &master->hj_work);

		if (IBI_TYPE_MR(status))
			pr_info("get mr from %02x\n", addr);
	}
}

static void aspeed_i3c_master_end_xfer_locked(struct aspeed_i3c_master *master, u32 isr)
{
	struct aspeed_i3c_xfer *xfer = master->xferqueue.cur;
	int i, ret = 0;
	u32 nresp;

	if (!xfer)
		return;

	nresp = readl(master->regs + QUEUE_STATUS_LEVEL);
	nresp = QUEUE_STATUS_LEVEL_RESP(nresp);

	for (i = 0; i < nresp; i++) {
		struct aspeed_i3c_cmd *cmd;
		u32 resp;

		resp = readl(master->regs + RESPONSE_QUEUE_PORT);

		cmd = &xfer->cmds[RESPONSE_PORT_TID(resp)];
		cmd->rx_len = RESPONSE_PORT_DATA_LEN(resp);
		cmd->error = RESPONSE_PORT_ERR_STATUS(resp);
		if (cmd->rx_len && !cmd->error)
			aspeed_i3c_master_read_rx_fifo(master, cmd->rx_buf,
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
		case RESPONSE_ERROR_PEC_ERR:
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
		aspeed_i3c_master_dequeue_xfer_locked(master, xfer);
		aspeed_i3c_master_resume(master);
	}

	xfer = list_first_entry_or_null(&master->xferqueue.list,
					struct aspeed_i3c_xfer,
					node);
	if (xfer)
		list_del_init(&xfer->node);

	master->xferqueue.cur = xfer;
	aspeed_i3c_master_start_xfer_locked(master);
}

struct i3c_scl_timing_cfg {
	unsigned long fscl;
	u16 period_hi;
	u16 period_lo;
};

static struct i3c_scl_timing_cfg jesd403_timing_cfg[5] = {
	{ .fscl = I3C_BUS_TYP_I3C_SCL_RATE, .period_hi = 40, .period_lo = 40 },
	{ .fscl = I3C_BUS_SDR1_SCL_RATE, .period_hi = 50, .period_lo = 75 },
	{ .fscl = I3C_BUS_SDR2_SCL_RATE, .period_hi = 65, .period_lo = 100 },
	{ .fscl = I3C_BUS_SDR3_SCL_RATE, .period_hi = 100, .period_lo = 150 },
	{ .fscl = I3C_BUS_SDR4_SCL_RATE, .period_hi = 200, .period_lo = 300 }
};

struct i3c_scl_timing_cfg *ast2600_i3c_jesd403_scl_search(unsigned long fscl)
{
	int i;

	for (i = 0; i < 5; i++) {
		if (fscl == jesd403_timing_cfg[i].fscl)
			return &jesd403_timing_cfg[i];
	}

	/* use typical 12.5M SCL if not found */
	return &jesd403_timing_cfg[0];
}

static int calc_i2c_clk(struct aspeed_i3c_master *master, unsigned long fscl,
			u16 *hcnt, u16 *lcnt)
{
	unsigned long core_rate, core_period;
	u32 period_cnt, margin;
	u32 hcnt_min, lcnt_min;

	core_rate = master->timing.core_rate;
	core_period = master->timing.core_period;

	if (fscl <= 100000) {
		lcnt_min = DIV_ROUND_UP(I3C_BUS_I2C_STD_TLOW_MIN_NS +
						I3C_BUS_I2C_STD_TF_MAX_NS,
					core_period);
		hcnt_min = DIV_ROUND_UP(I3C_BUS_I2C_STD_THIGH_MIN_NS +
						I3C_BUS_I2C_STD_TR_MAX_NS,
					core_period);
	} else if (fscl <= 400000) {
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

static int aspeed_i3c_clk_cfg(struct aspeed_i3c_master *master)
{
	unsigned long core_rate, core_period;
	u32 scl_timing;
	u16 hcnt, lcnt;

	core_rate = master->timing.core_rate;
	core_period = master->timing.core_period;

	/* I3C PP mode */
	if (master->timing.i3c_pp_scl_high && master->timing.i3c_pp_scl_low) {
		hcnt = DIV_ROUND_CLOSEST(master->timing.i3c_pp_scl_high,
					 core_period);
		lcnt = DIV_ROUND_CLOSEST(master->timing.i3c_pp_scl_low,
					 core_period);
	} else if (master->base.jdec_spd) {
		struct i3c_scl_timing_cfg *pp_timing;

		pp_timing = ast2600_i3c_jesd403_scl_search(
			master->base.bus.scl_rate.i3c);
		hcnt = DIV_ROUND_UP(pp_timing->period_hi, core_period);
		lcnt = DIV_ROUND_UP(pp_timing->period_lo, core_period);
	} else {
		hcnt = DIV_ROUND_UP(I3C_BUS_THIGH_MAX_NS, core_period) - 1;
		if (hcnt < SCL_I3C_TIMING_CNT_MIN)
			hcnt = SCL_I3C_TIMING_CNT_MIN;

		lcnt = DIV_ROUND_UP(core_rate, I3C_BUS_TYP_I3C_SCL_RATE) - hcnt;
		if (lcnt < SCL_I3C_TIMING_CNT_MIN)
			lcnt = SCL_I3C_TIMING_CNT_MIN;
	}
	hcnt = min_t(u16, hcnt, FIELD_MAX(SCL_I3C_TIMING_HCNT));
	lcnt = min_t(u16, lcnt, FIELD_MAX(SCL_I3C_TIMING_LCNT));
	scl_timing = FIELD_PREP(SCL_I3C_TIMING_HCNT, hcnt) |
		     FIELD_PREP(SCL_I3C_TIMING_LCNT, lcnt);
	writel(scl_timing, master->regs + SCL_I3C_PP_TIMING);

	/* I3C OD mode:
	 * User defined
	 *     check if hcnt/lcnt exceed the max value of the register
	 *
	 * JESD403 timing constrain for I2C/I3C OP mode
	 *     tHIGH > 260, tLOW > 500 (same with MIPI 1.1 FMP constrain)
	 *
	 * MIPI 1.1 timing constrain for I3C OP mode
	 *     tHIGH < 41, tLOW > 200
	 */
	if (master->timing.i3c_od_scl_high && master->timing.i3c_od_scl_low) {
		hcnt = DIV_ROUND_CLOSEST(master->timing.i3c_od_scl_high,
					 core_period);
		lcnt = DIV_ROUND_CLOSEST(master->timing.i3c_od_scl_low,
					 core_period);
	} else if (master->base.jdec_spd) {
		calc_i2c_clk(master, I3C_BUS_I2C_FM_PLUS_SCL_RATE, &hcnt, &lcnt);
	} else {
		lcnt = DIV_ROUND_UP(I3C_BUS_TLOW_OD_MIN_NS, core_period);
		scl_timing = readl(master->regs + SCL_I3C_PP_TIMING);
		hcnt = FIELD_GET(SCL_I3C_TIMING_HCNT, scl_timing);
	}
	hcnt = min_t(u16, hcnt, FIELD_MAX(SCL_I3C_TIMING_HCNT));
	lcnt = min_t(u16, lcnt, FIELD_MAX(SCL_I3C_TIMING_LCNT));
	scl_timing = FIELD_PREP(SCL_I3C_TIMING_HCNT, hcnt) |
		     FIELD_PREP(SCL_I3C_TIMING_LCNT, lcnt);
	writel(scl_timing, master->regs + SCL_I3C_OD_TIMING);

	/* I2C FM mode */
	calc_i2c_clk(master, master->base.bus.scl_rate.i2c, &hcnt, &lcnt);
	scl_timing = FIELD_PREP(SCL_I2C_FM_TIMING_HCNT, hcnt) |
		     FIELD_PREP(SCL_I2C_FM_TIMING_LCNT, lcnt);
	writel(scl_timing, master->regs + SCL_I2C_FM_TIMING);

	/*
	 * I3C register 0xd4[15:0] BUS_FREE_TIMING used to control several parameters:
	 * - tCAS & tCASr (tHD_STA in JESD403)
	 * - tCBP & tCBPr (tSU_STO in JESD403)
	 * - bus free time between a STOP condition and a START condition
	 *
	 * The constraints of these two parameters are different in different bus contexts
	 * - MIPI I3C, mixed bus: 0xd4[15:0] = I2C SCL low period (handled in aspeed_i2c_clk_cfg)
	 * - MIPI I3C, pure bus : 0xd4[15:0] = I3C SCL PP low period
	 * - JESD403            : 0xd4[15:0] = I3C SCL OD low period
	 */
	if (!(readl(master->regs + DEVICE_CTRL) & DEV_CTRL_I2C_SLAVE_PRESENT)) {
		if (master->base.jdec_spd)
			scl_timing = readl(master->regs + SCL_I3C_OD_TIMING);
		else
			scl_timing = readl(master->regs + SCL_I3C_PP_TIMING);

		lcnt = FIELD_GET(SCL_I3C_TIMING_LCNT, scl_timing);
		scl_timing = BUS_I3C_AVAILABLE_TIME(0xffff);
		scl_timing |= BUS_I3C_MST_FREE(lcnt);
		writel(scl_timing, master->regs + BUS_FREE_TIMING);
	}

	/* Extend SDR: use PP mode hcnt */
	scl_timing = readl(master->regs + SCL_I3C_PP_TIMING);
	hcnt = scl_timing >> 16;
	lcnt = DIV_ROUND_UP(core_rate, I3C_BUS_SDR1_SCL_RATE) - hcnt;
	scl_timing = SCL_EXT_LCNT_1(lcnt);
	lcnt = DIV_ROUND_UP(core_rate, I3C_BUS_SDR2_SCL_RATE) - hcnt;
	scl_timing |= SCL_EXT_LCNT_2(lcnt);
	lcnt = DIV_ROUND_UP(core_rate, I3C_BUS_SDR3_SCL_RATE) - hcnt;
	scl_timing |= SCL_EXT_LCNT_3(lcnt);
	lcnt = DIV_ROUND_UP(core_rate, I3C_BUS_SDR4_SCL_RATE) - hcnt;
	scl_timing |= SCL_EXT_LCNT_4(lcnt);
	writel(scl_timing, master->regs + SCL_EXT_LCNT_TIMING);

	ast_clrsetbits(master->regs + SCL_EXT_TERMN_LCNT_TIMING, GENMASK(3, 0),
		      I3C_BUS_EXT_TERMN_CNT);

	return 0;
}

static int aspeed_i2c_clk_cfg(struct aspeed_i3c_master *master)
{
	unsigned long core_rate, core_period;
	u16 hcnt, lcnt;
	u32 scl_timing;

	core_rate = master->timing.core_rate;
	core_period = master->timing.core_period;

	calc_i2c_clk(master, I3C_BUS_I2C_FM_PLUS_SCL_RATE, &hcnt, &lcnt);
	hcnt = min_t(u16, hcnt, FIELD_MAX(SCL_I2C_FMP_TIMING_HCNT));
	lcnt = min_t(u16, lcnt, FIELD_MAX(SCL_I2C_FMP_TIMING_LCNT));
	scl_timing = FIELD_PREP(SCL_I2C_FMP_TIMING_HCNT, hcnt) |
		     FIELD_PREP(SCL_I2C_FMP_TIMING_LCNT, lcnt);
	writel(scl_timing, master->regs + SCL_I2C_FMP_TIMING);

	calc_i2c_clk(master, master->base.bus.scl_rate.i2c, &hcnt, &lcnt);
	scl_timing = FIELD_PREP(SCL_I2C_FM_TIMING_HCNT, hcnt) |
		     FIELD_PREP(SCL_I2C_FM_TIMING_LCNT, lcnt);
	writel(scl_timing, master->regs + SCL_I2C_FM_TIMING);

	scl_timing = BUS_I3C_AVAILABLE_TIME(0xffff);
	scl_timing |= BUS_I3C_MST_FREE(lcnt);
	writel(scl_timing, master->regs + BUS_FREE_TIMING);
	writel(readl(master->regs + DEVICE_CTRL) | DEV_CTRL_I2C_SLAVE_PRESENT,
	       master->regs + DEVICE_CTRL);

	return 0;
}

static int aspeed_i3c_master_set_info(struct aspeed_i3c_master *master,
				       struct i3c_device_info *info)
{
#define ASPEED_SCU_REV_ID_REG 0x14
#define ASPEED_HW_REV(x) (((x)&GENMASK(31, 16)) >> 16)

	struct regmap *scu;
	unsigned int reg;
	u32 part_id, inst_id;

	writel(PID_MANUF_ID_ASPEED << 1, master->regs + SLV_MIPI_PID_VALUE);

	scu = syscon_regmap_lookup_by_phandle(master->dev->of_node, "aspeed,scu");
	if (IS_ERR(scu)) {
		dev_err(master->dev, "cannot to find SCU regmap\n");
		return -ENODEV;
	}
	regmap_read(scu, ASPEED_SCU_REV_ID_REG, &reg);
	part_id = ASPEED_HW_REV(reg);
	inst_id = master->base.bus.id;

	reg = SLV_PID_PART_ID(part_id) | SLV_PID_INST_ID(inst_id) |
	      SLV_PID_DCR(0);
	writel(reg, master->regs + SLV_PID_VALUE);

	reg = readl(master->regs + SLV_CHAR_CTRL);
	info->dcr = SLV_CHAR_GET_DCR(reg);
	info->bcr = SLV_CHAR_GET_BCR(reg);
	info->pid = (u64)readl(master->regs + SLV_MIPI_PID_VALUE) << 32;
	info->pid |= readl(master->regs + SLV_PID_VALUE);
	info->hdr_cap = I3C_CCC_HDR_MODE(I3C_HDR_DDR);

	return 0;
};

static int aspeed_i3c_master_bus_init(struct i3c_master_controller *m)
{
	struct aspeed_i3c_master *master = to_aspeed_i3c_master(m);
	struct i3c_bus *bus = i3c_master_get_bus(m);
	struct i3c_device_info info = { };
	u32 thld_ctrl;
	int ret;

	aspeed_i3c_master_set_role(master);

	switch (bus->mode) {
	case I3C_BUS_MODE_MIXED_FAST:
	case I3C_BUS_MODE_MIXED_LIMITED:
		ret = aspeed_i2c_clk_cfg(master);
		if (ret)
			return ret;
		fallthrough;
	case I3C_BUS_MODE_PURE:
		ret = aspeed_i3c_clk_cfg(master);
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
	if (master->secondary) {
		writel(INTR_2ND_MASTER_MASK, master->regs + INTR_STATUS_EN);
		/*
		 * No need for INTR_IBI_UPDATED_STAT signal, check this bit
		 * when INTR_RESP_READY_STAT signal is up.  This can guarantee
		 * the SIR payload is ACKed by the master.
		 */
		writel(INTR_2ND_MASTER_MASK & ~INTR_IBI_UPDATED_STAT,
		       master->regs + INTR_SIGNAL_EN);
	} else {
		writel(INTR_MASTER_MASK, master->regs + INTR_STATUS_EN);
		writel(INTR_MASTER_MASK, master->regs + INTR_SIGNAL_EN);
	}

	memset(&info, 0, sizeof(info));
	ret = aspeed_i3c_master_set_info(master, &info);
	if (ret < 0)
		return ret;

	ret = i3c_master_get_free_addr(m, 0);
	if (ret < 0)
		return ret;

	if (master->secondary)
		writel(DEV_ADDR_STATIC_ADDR_VALID | DEV_ADDR_STATIC(ret),
		       master->regs + DEVICE_ADDR);
	else
		writel(DEV_ADDR_DYNAMIC_ADDR_VALID | DEV_ADDR_DYNAMIC(ret),
		       master->regs + DEVICE_ADDR);

	info.dyn_addr = ret;

	ret = i3c_master_set_info(&master->base, &info);
	if (ret)
		return ret;

	thld_ctrl = readl(master->regs + QUEUE_THLD_CTRL);
	thld_ctrl &=
		~(QUEUE_THLD_CTRL_IBI_STA_MASK | QUEUE_THLD_CTRL_IBI_DAT_MASK);
	thld_ctrl |= QUEUE_THLD_CTRL_IBI_STA(1);
	thld_ctrl |= QUEUE_THLD_CTRL_IBI_DAT(MAX_IBI_FRAG_SIZE >> 2);
	writel(thld_ctrl, master->regs + QUEUE_THLD_CTRL);

	writel(IBI_REQ_REJECT_ALL, master->regs + IBI_SIR_REQ_REJECT);
	writel(IBI_REQ_REJECT_ALL, master->regs + IBI_MR_REQ_REJECT);

	/* For now don't support Hot-Join */
	ast_setbits(master->regs + DEVICE_CTRL,
		   DEV_CTRL_AUTO_HJ_DISABLE |
		   DEV_CTRL_HOT_JOIN_NACK |
		   DEV_CRTL_IBI_PAYLOAD_EN);

	ret = aspeed_i3c_master_enable(master);
	if (ret)
		return ret;
	/* workaround for aspeed slave devices.  The aspeed slave devices need
	 * for a dummy ccc and resume before accessing. Hide this workarond here
	 * and later the i3c subsystem code will do the rstdaa again.
	 */
	if (!master->secondary)
		i3c_master_rstdaa_locked(m, I3C_BROADCAST_ADDR);

	return 0;
}

static void aspeed_i3c_master_bus_cleanup(struct i3c_master_controller *m)
{
	struct aspeed_i3c_master *master = to_aspeed_i3c_master(m);

	aspeed_i3c_master_disable(master);
}

static void aspeed_i3c_master_bus_reset(struct i3c_master_controller *m)
{
	struct aspeed_i3c_master *master = to_aspeed_i3c_master(m);
	u32 reset;
	int i;

	if (master->base.jdec_spd) {
		reset = RESET_CTRL_BUS |
			FIELD_PREP(RESET_CTRL_BUS_RESET_TYPE, BUS_RESET_TYPE_SCL_LOW);

		writel(reset, master->regs + RESET_CTRL);
	} else {
		regmap_write_bits(master->i3cg, I3CG_REG1(master->channel),
				  SDA_OUT_SW_MODE_VAL | SCL_OUT_SW_MODE_VAL,
				  SDA_OUT_SW_MODE_VAL | SCL_OUT_SW_MODE_VAL);
		regmap_write_bits(master->i3cg, I3CG_REG1(master->channel),
				  SDA_SW_MODE_OE | SCL_SW_MODE_OE,
				  SDA_SW_MODE_OE | SCL_SW_MODE_OE);
		regmap_write_bits(master->i3cg, I3CG_REG1(master->channel),
				  SDA_OUT_SW_MODE_EN | SCL_OUT_SW_MODE_EN,
				  SDA_OUT_SW_MODE_EN | SCL_OUT_SW_MODE_EN);
		regmap_write_bits(master->i3cg, I3CG_REG1(master->channel),
				  SDA_IN_SW_MODE_VAL | SCL_IN_SW_MODE_VAL,
				  SDA_IN_SW_MODE_VAL | SCL_IN_SW_MODE_VAL);
		regmap_write_bits(master->i3cg, I3CG_REG1(master->channel),
				  SDA_IN_SW_MODE_EN | SCL_IN_SW_MODE_EN,
				  SDA_IN_SW_MODE_EN | SCL_IN_SW_MODE_EN);
		regmap_write_bits(master->i3cg, I3CG_REG1(master->channel),
				  SCL_OUT_SW_MODE_VAL, 0);
		for (i = 0; i < 7; i++) {
			regmap_write_bits(master->i3cg, I3CG_REG1(master->channel),
					  SDA_OUT_SW_MODE_VAL, 0);
			regmap_write_bits(master->i3cg, I3CG_REG1(master->channel),
					  SDA_OUT_SW_MODE_VAL,
					  SDA_OUT_SW_MODE_VAL);
		}
		regmap_write_bits(master->i3cg, I3CG_REG1(master->channel),
				  SCL_OUT_SW_MODE_VAL, SCL_OUT_SW_MODE_VAL);
		regmap_write_bits(master->i3cg, I3CG_REG1(master->channel),
				  SDA_OUT_SW_MODE_VAL, 0);
		regmap_write_bits(master->i3cg, I3CG_REG1(master->channel),
				  SDA_OUT_SW_MODE_VAL, SDA_OUT_SW_MODE_VAL);
		regmap_write_bits(master->i3cg, I3CG_REG1(master->channel),
				  SDA_OUT_SW_MODE_EN | SCL_OUT_SW_MODE_EN, 0);
		regmap_write_bits(master->i3cg, I3CG_REG1(master->channel),
				  SDA_IN_SW_MODE_EN | SCL_IN_SW_MODE_EN, 0);
	}
}

static int aspeed_i3c_ccc_set(struct aspeed_i3c_master *master,
			  struct i3c_ccc_cmd *ccc)
{
	struct aspeed_i3c_xfer *xfer;
	struct aspeed_i3c_cmd *cmd;
	int ret, pos = 0;

	if (ccc->id & I3C_CCC_DIRECT) {
		pos = aspeed_i3c_master_sync_hw_dat(master, ccc->dests[0].addr);
		if (pos < 0)
			return pos;
	}

	xfer = aspeed_i3c_master_alloc_xfer(master, 1);
	if (!xfer)
		return -ENOMEM;

	cmd = xfer->cmds;
	cmd->tx_buf = ccc->dests[0].payload.data;
	cmd->tx_len = ccc->dests[0].payload.len;

	cmd->cmd_hi = COMMAND_PORT_ARG_DATA_LEN(ccc->dests[0].payload.len) |
		      COMMAND_PORT_TRANSFER_ARG | COMMAND_PORT_ARG_DBP(ccc->db);

	cmd->cmd_lo = COMMAND_PORT_CP |
		      COMMAND_PORT_DEV_INDEX(pos) |
		      COMMAND_PORT_CMD(ccc->id) |
		      COMMAND_PORT_TOC |
		      COMMAND_PORT_ROC |
		      COMMAND_PORT_DBP(ccc->dbp);

	if (ccc->id == I3C_CCC_SETHID || ccc->id == I3C_CCC_DEVCTRL)
		cmd->cmd_lo |= COMMAND_PORT_SPEED(SPEED_I3C_I2C_FM);

	dev_dbg(master->dev, "%s:cmd_hi=0x%08x cmd_lo=0x%08x tx_len=%d id=%x\n",
		__func__, cmd->cmd_hi, cmd->cmd_lo, cmd->tx_len, ccc->id);

	aspeed_i3c_master_enqueue_xfer(master, xfer);
	if (!wait_for_completion_timeout(&xfer->comp, XFER_TIMEOUT))
		aspeed_i3c_master_dequeue_xfer(master, xfer);

	ret = xfer->ret;
	if (ret)
		dev_err(master->dev, "xfer error: %x\n", xfer->cmds[0].error);
	if (xfer->cmds[0].error == RESPONSE_ERROR_IBA_NACK)
		ccc->err = I3C_ERROR_M2;

	aspeed_i3c_master_free_xfer(xfer);

	return ret;
}

static int aspeed_i3c_ccc_get(struct aspeed_i3c_master *master, struct i3c_ccc_cmd *ccc)
{
	struct aspeed_i3c_xfer *xfer;
	struct aspeed_i3c_cmd *cmd;
	int ret, pos;

	pos = aspeed_i3c_master_sync_hw_dat(master, ccc->dests[0].addr);
	if (pos < 0)
		return pos;

	xfer = aspeed_i3c_master_alloc_xfer(master, 1);
	if (!xfer)
		return -ENOMEM;

	cmd = xfer->cmds;
	cmd->rx_buf = ccc->dests[0].payload.data;
	cmd->rx_len = ccc->dests[0].payload.len;

	cmd->cmd_hi = COMMAND_PORT_ARG_DATA_LEN(ccc->dests[0].payload.len) |
		      COMMAND_PORT_TRANSFER_ARG | COMMAND_PORT_ARG_DBP(ccc->db);

	cmd->cmd_lo = COMMAND_PORT_READ_TRANSFER |
		      COMMAND_PORT_CP |
		      COMMAND_PORT_DEV_INDEX(pos) |
		      COMMAND_PORT_CMD(ccc->id) |
		      COMMAND_PORT_TOC |
		      COMMAND_PORT_ROC |
		      COMMAND_PORT_DBP(ccc->dbp);

	dev_dbg(master->dev, "%s:cmd_hi=0x%08x cmd_lo=0x%08x rx_len=%d id=%x\n",
		__func__, cmd->cmd_hi, cmd->cmd_lo, cmd->rx_len, ccc->id);

	aspeed_i3c_master_enqueue_xfer(master, xfer);
	if (!wait_for_completion_timeout(&xfer->comp, XFER_TIMEOUT))
		aspeed_i3c_master_dequeue_xfer(master, xfer);

	ret = xfer->ret;
	if (ret)
		dev_err(master->dev, "xfer error: %x\n", xfer->cmds[0].error);
	if (xfer->cmds[0].error == RESPONSE_ERROR_IBA_NACK)
		ccc->err = I3C_ERROR_M2;
	aspeed_i3c_master_free_xfer(xfer);

	return ret;
}

static int aspeed_i3c_master_send_ccc_cmd(struct i3c_master_controller *m,
				      struct i3c_ccc_cmd *ccc)
{
	struct aspeed_i3c_master *master = to_aspeed_i3c_master(m);
	int ret = 0;
	u32 i3c_pp_timing, i3c_od_timing;

	if (ccc->id == I3C_CCC_ENTDAA)
		return -EINVAL;

	i3c_od_timing = readl(master->regs + SCL_I3C_OD_TIMING);
	i3c_pp_timing = readl(master->regs + SCL_I3C_PP_TIMING);

	dev_dbg(master->dev, "ccc-id %02x rnw=%d\n", ccc->id, ccc->rnw);

	if (ccc->rnw)
		ret = aspeed_i3c_ccc_get(master, ccc);
	else
		ret = aspeed_i3c_ccc_set(master, ccc);

	return ret;
}

#define IS_MANUF_ID_ASPEED(x) (I3C_PID_MANUF_ID(x) == PID_MANUF_ID_ASPEED)
#define IS_PART_ID_AST2600_SERIES(x)                                           \
	((I3C_PID_PART_ID(x) & PID_PART_ID_AST2600_SERIES) ==                  \
	 PID_PART_ID_AST2600_SERIES)
#define IS_PART_ID_AST1030_A0(x)                                               \
	((I3C_PID_PART_ID(x) & PID_PART_ID_AST1030_A0) ==                      \
	 PID_PART_ID_AST1030_A0)

static int aspeed_i3c_master_daa(struct i3c_master_controller *m)
{
	struct aspeed_i3c_master *master = to_aspeed_i3c_master(m);
	struct aspeed_i3c_xfer *xfer;
	struct aspeed_i3c_cmd *cmd;
	u32 olddevs, newdevs, dat;
	u8 p, last_addr = 0, last_grp = 0;
	int ret, pos, ndevs;

	olddevs = ~(master->free_pos);
	ndevs = 0;

	/* Prepare DAT before launching DAA. */
	for (pos = 0; pos < master->maxdevs; pos++) {
		if (olddevs & BIT(pos))
			continue;

		ret = i3c_master_get_free_addr(m, (last_grp + 1) << ADDR_GRP_SHIFT);
		if (ret < 0)
			break;

		ndevs++;

		master->addrs[pos] = ret;
		p = even_parity(ret);
		last_addr = ret;
		last_grp = ADDR_GRP(last_addr);

		dat = readl(master->regs +
			    DEV_ADDR_TABLE_LOC(master->datstartaddr, pos));
		dat &= ~(DEV_ADDR_TABLE_DYNAMIC_ADDR |
			 DEV_ADDR_TABLE_DA_PARITY);
		dat |= FIELD_PREP(DEV_ADDR_TABLE_DYNAMIC_ADDR, ret) |
		       FIELD_PREP(DEV_ADDR_TABLE_DA_PARITY, p);
		writel(dat, master->regs + DEV_ADDR_TABLE_LOC(
						   master->datstartaddr, pos));
	}

	if (!ndevs)
		return -ENOSPC;

	xfer = aspeed_i3c_master_alloc_xfer(master, 1);
	if (!xfer)
		return -ENOMEM;

	pos = aspeed_i3c_master_get_free_pos(master);
	if (pos < 0) {
		aspeed_i3c_master_free_xfer(xfer);
		return pos;
	}
	cmd = &xfer->cmds[0];
	cmd->cmd_hi = 0x1;
	cmd->cmd_lo = COMMAND_PORT_DEV_COUNT(ndevs) |
		      COMMAND_PORT_DEV_INDEX(pos) |
		      COMMAND_PORT_CMD(I3C_CCC_ENTDAA) |
		      COMMAND_PORT_ADDR_ASSGN_CMD |
		      COMMAND_PORT_TOC |
		      COMMAND_PORT_ROC;

	aspeed_i3c_master_enqueue_xfer(master, xfer);
	if (!wait_for_completion_timeout(&xfer->comp, XFER_TIMEOUT))
		aspeed_i3c_master_dequeue_xfer(master, xfer);

	newdevs = GENMASK(ndevs - cmd->rx_len - 1, 0) << pos;
	for (pos = 0; pos < master->maxdevs; pos++) {
		if (newdevs & BIT(pos)) {
			u32 addr;

			dat = GET_DAT_FROM_POS(master, pos);
			addr = FIELD_GET(DEV_ADDR_TABLE_DYNAMIC_ADDR, dat);

			aspeed_i3c_master_set_group_dat(master, addr, dat);
			i3c_master_add_i3c_dev_locked(m, addr);
		}

		/* cleanup the free HW DATs */
		if (master->free_pos & BIT(pos)) {
			dat = readl(
				master->regs +
				DEV_ADDR_TABLE_LOC(master->datstartaddr, pos));
			dat &= ~(DEV_ADDR_TABLE_DYNAMIC_ADDR |
				 DEV_ADDR_TABLE_DA_PARITY);
			dat |= FIELD_PREP(DEV_ADDR_TABLE_DA_PARITY,
					  even_parity(0));
			writel(dat, master->regs +
					    DEV_ADDR_TABLE_LOC(
						    master->datstartaddr, pos));
		}
	}

	aspeed_i3c_master_free_xfer(xfer);

	return 0;
}
#ifdef CONFIG_AST2600_I3C_CCC_WORKAROUND
/*
 * Provide an interface for sending CCC from userspace.  Especially for the
 * transfers with PEC and direct CCC.
 */
static int aspeed_i3c_master_ccc_xfers(struct i3c_dev_desc *dev,
				    struct i3c_priv_xfer *i3c_xfers,
				    int i3c_nxfers)
{
	struct aspeed_i3c_i2c_dev_data *data = i3c_dev_get_master_data(dev);
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct aspeed_i3c_master *master = to_aspeed_i3c_master(m);
	struct aspeed_i3c_xfer *xfer;
	int i, ret = 0;
	struct aspeed_i3c_cmd *cmd_ccc;

	xfer = aspeed_i3c_master_alloc_xfer(master, i3c_nxfers);
	if (!xfer)
		return -ENOMEM;

	/* i3c_xfers[0] handles the CCC data */
	cmd_ccc = &xfer->cmds[0];
	cmd_ccc->cmd_hi = COMMAND_PORT_ARG_DATA_LEN(i3c_xfers[0].len - 1) |
			  COMMAND_PORT_TRANSFER_ARG;
	cmd_ccc->tx_buf = i3c_xfers[0].data.out + 1;
	cmd_ccc->tx_len = i3c_xfers[0].len - 1;
	cmd_ccc->cmd_lo = COMMAND_PORT_SPEED(dev->info.max_write_ds);
	cmd_ccc->cmd_lo |= COMMAND_PORT_TID(0) |
			   COMMAND_PORT_DEV_INDEX(master->maxdevs - 1) |
			   COMMAND_PORT_ROC;
	if (i3c_nxfers == 1)
		cmd_ccc->cmd_lo |= COMMAND_PORT_TOC;

	dev_dbg(master->dev,
		"%s:cmd_ccc_hi=0x%08x cmd_ccc_lo=0x%08x tx_len=%d\n", __func__,
		cmd_ccc->cmd_hi, cmd_ccc->cmd_lo, cmd_ccc->tx_len);

	for (i = 1; i < i3c_nxfers; i++) {
		struct aspeed_i3c_cmd *cmd = &xfer->cmds[i];

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

		dev_dbg(master->dev,
			"%s:cmd_hi=0x%08x cmd_lo=0x%08x tx_len=%d rx_len=%d\n",
			__func__, cmd->cmd_hi, cmd->cmd_lo, cmd->tx_len,
			cmd->rx_len);
	}

	aspeed_i3c_master_enqueue_xfer(master, xfer);
	if (!wait_for_completion_timeout(&xfer->comp, XFER_TIMEOUT))
		aspeed_i3c_master_dequeue_xfer(master, xfer);

	ret = xfer->ret;
	aspeed_i3c_master_free_xfer(xfer);

	return ret;
}
#endif
static int aspeed_i3c_master_priv_xfers(struct i3c_dev_desc *dev,
				    struct i3c_priv_xfer *i3c_xfers,
				    int i3c_nxfers)
{
	struct aspeed_i3c_i2c_dev_data *data = i3c_dev_get_master_data(dev);
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct aspeed_i3c_master *master = to_aspeed_i3c_master(m);
	unsigned int nrxwords = 0, ntxwords = 0;
	struct aspeed_i3c_xfer *xfer;
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

#ifdef CONFIG_AST2600_I3C_CCC_WORKAROUND
	if (i3c_xfers[0].rnw == 0) {
		/* write command: check if hit special address */
		u8 tmp;

		memcpy(&tmp, i3c_xfers[0].data.out, 1);
		if (tmp == 0xff)
			return aspeed_i3c_master_ccc_xfers(dev, i3c_xfers, i3c_nxfers);
	}
#endif

	xfer = aspeed_i3c_master_alloc_xfer(master, i3c_nxfers);
	if (!xfer)
		return -ENOMEM;

	data->index = aspeed_i3c_master_sync_hw_dat(master, dev->info.dyn_addr);

	for (i = 0; i < i3c_nxfers; i++) {
		struct aspeed_i3c_cmd *cmd = &xfer->cmds[i];

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

		if (dev->info.pec)
			cmd->cmd_lo |= COMMAND_PORT_PEC;

		dev_dbg(master->dev,
			"%s:cmd_hi=0x%08x cmd_lo=0x%08x tx_len=%d rx_len=%d\n",
			__func__, cmd->cmd_hi, cmd->cmd_lo, cmd->tx_len,
			cmd->rx_len);
	}

	aspeed_i3c_master_enqueue_xfer(master, xfer);
	if (!wait_for_completion_timeout(&xfer->comp, XFER_TIMEOUT))
		aspeed_i3c_master_dequeue_xfer(master, xfer);

	for (i = 0; i < i3c_nxfers; i++) {
		struct aspeed_i3c_cmd *cmd = &xfer->cmds[i];

		if (i3c_xfers[i].rnw)
			i3c_xfers[i].len = cmd->rx_len;
	}

	ret = xfer->ret;
	if (ret)
		dev_err(master->dev, "xfer error: %x\n", xfer->cmds[0].error);
	aspeed_i3c_master_free_xfer(xfer);

	return ret;
}

static int aspeed_i3c_master_send_hdr_cmd(struct i3c_master_controller *m,
					  struct i3c_hdr_cmd *cmds,
					  int ncmds)
{
	struct aspeed_i3c_master *master = to_aspeed_i3c_master(m);
	u8 dat_index;
	int ret, i, ntxwords = 0, nrxwords = 0;
	struct aspeed_i3c_xfer *xfer;

	if (ncmds < 1)
		return 0;

	dev_dbg(master->dev, "ncmds = %x", ncmds);

	if (ncmds > master->caps.cmdfifodepth)
		return -ENOTSUPP;

	for (i = 0; i < ncmds; i++) {
		dev_dbg(master->dev, "cmds[%d] mode = %x", i, cmds[i].mode);
		if (cmds[i].mode != I3C_HDR_DDR)
			return -ENOTSUPP;
		if (cmds[i].code & 0x80)
			nrxwords += DIV_ROUND_UP(cmds[i].ndatawords, 2);
		else
			ntxwords += DIV_ROUND_UP(cmds[i].ndatawords, 2);
	}
	dev_dbg(master->dev, "ntxwords = %x, nrxwords = %x", ntxwords,
		 nrxwords);
	if (ntxwords > master->caps.datafifodepth ||
	    nrxwords > master->caps.datafifodepth)
		return -ENOTSUPP;

	xfer = aspeed_i3c_master_alloc_xfer(master, ncmds);
	if (!xfer)
		return -ENOMEM;

	for (i = 0; i < ncmds; i++) {
		struct aspeed_i3c_cmd *cmd = &xfer->cmds[i];

		dat_index = aspeed_i3c_master_sync_hw_dat(master, cmds[i].addr);

		cmd->cmd_hi = COMMAND_PORT_ARG_DATA_LEN(cmds[i].ndatawords << 1) |
			      COMMAND_PORT_TRANSFER_ARG;

		if (cmds[i].code & 0x80) {
			cmd->rx_buf = cmds[i].data.in;
			cmd->rx_len = cmds[i].ndatawords << 1;
			cmd->cmd_lo = COMMAND_PORT_READ_TRANSFER |
				      COMMAND_PORT_CP |
				      COMMAND_PORT_CMD(cmds[i].code) |
				      COMMAND_PORT_SPEED(SPEED_I3C_HDR_DDR);

		} else {
			cmd->tx_buf = cmds[i].data.out;
			cmd->tx_len = cmds[i].ndatawords << 1;
			cmd->cmd_lo = COMMAND_PORT_CP |
				      COMMAND_PORT_CMD(cmds[i].code) |
				      COMMAND_PORT_SPEED(SPEED_I3C_HDR_DDR);
		}

		cmd->cmd_lo |= COMMAND_PORT_TID(i) |
			       COMMAND_PORT_DEV_INDEX(dat_index) |
			       COMMAND_PORT_ROC;

		if (i == (ncmds - 1))
			cmd->cmd_lo |= COMMAND_PORT_TOC;

		dev_dbg(master->dev,
			 "%s:cmd_hi=0x%08x cmd_lo=0x%08x tx_len=%d rx_len=%d\n",
			 __func__, cmd->cmd_hi, cmd->cmd_lo, cmd->tx_len,
			 cmd->rx_len);
	}

	aspeed_i3c_master_enqueue_xfer(master, xfer);
	if (!wait_for_completion_timeout(&xfer->comp, XFER_TIMEOUT))
		aspeed_i3c_master_dequeue_xfer(master, xfer);

	for (i = 0; i < ncmds; i++) {
		struct aspeed_i3c_cmd *cmd = &xfer->cmds[i];

		if (cmds[i].code & 0x80)
			cmds[i].ndatawords = DIV_ROUND_UP(cmd->rx_len, 2);
	}

	ret = xfer->ret;
	if (ret)
		dev_err(master->dev, "xfer error: %x\n", xfer->cmds[0].error);
	aspeed_i3c_master_free_xfer(xfer);

	return ret;
}

static int aspeed_i3c_master_reattach_i3c_dev(struct i3c_dev_desc *dev,
					  u8 old_dyn_addr)
{
	struct aspeed_i3c_i2c_dev_data *data = i3c_dev_get_master_data(dev);
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct aspeed_i3c_master *master = to_aspeed_i3c_master(m);

	aspeed_i3c_master_set_group_dat(
		master, dev->info.dyn_addr,
		FIELD_PREP(DEV_ADDR_TABLE_DYNAMIC_ADDR, dev->info.dyn_addr));

	master->addrs[data->index] = dev->info.dyn_addr;

	return 0;
}

static int aspeed_i3c_master_attach_i3c_dev(struct i3c_dev_desc *dev)
{
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct aspeed_i3c_master *master = to_aspeed_i3c_master(m);
	struct aspeed_i3c_i2c_dev_data *data;
	int pos;
	u8 addr = dev->info.dyn_addr ? : dev->info.static_addr;

	pos = aspeed_i3c_master_set_group_dat(
		master, addr, FIELD_PREP(DEV_ADDR_TABLE_DYNAMIC_ADDR, addr));
	if (pos < 0)
		return pos;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->index = aspeed_i3c_master_get_group_hw_index(master, addr);
	master->addrs[pos] = addr;
	i3c_dev_set_master_data(dev, data);

	if (master->base.jdec_spd)
		dev->info.max_write_ds = dev->info.max_read_ds = 0;

	return 0;
}

static void aspeed_i3c_master_detach_i3c_dev(struct i3c_dev_desc *dev)
{
	struct aspeed_i3c_i2c_dev_data *data = i3c_dev_get_master_data(dev);
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct aspeed_i3c_master *master = to_aspeed_i3c_master(m);

	aspeed_i3c_master_set_group_dat(master, dev->info.dyn_addr, 0);

	i3c_dev_set_master_data(dev, NULL);
	master->addrs[data->index] = 0;
	kfree(data);
}

static int aspeed_i3c_master_i2c_xfers(struct i2c_dev_desc *dev,
				   const struct i2c_msg *i2c_xfers,
				   int i2c_nxfers)
{
	struct aspeed_i3c_i2c_dev_data *data = i2c_dev_get_master_data(dev);
	struct i3c_master_controller *m = i2c_dev_get_master(dev);
	struct aspeed_i3c_master *master = to_aspeed_i3c_master(m);
	unsigned int nrxwords = 0, ntxwords = 0;
	struct aspeed_i3c_xfer *xfer;
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

	xfer = aspeed_i3c_master_alloc_xfer(master, i2c_nxfers);
	if (!xfer)
		return -ENOMEM;

	data->index = aspeed_i3c_master_sync_hw_dat(master, dev->addr);

	for (i = 0; i < i2c_nxfers; i++) {
		struct aspeed_i3c_cmd *cmd = &xfer->cmds[i];

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

		dev_dbg(master->dev,
			"%s:cmd_hi=0x%08x cmd_lo=0x%08x tx_len=%d rx_len=%d\n",
			__func__, cmd->cmd_hi, cmd->cmd_lo, cmd->tx_len,
			cmd->rx_len);
	}

	aspeed_i3c_master_enqueue_xfer(master, xfer);
	if (!wait_for_completion_timeout(&xfer->comp, XFER_TIMEOUT))
		aspeed_i3c_master_dequeue_xfer(master, xfer);

	ret = xfer->ret;
	if (ret)
		dev_err(master->dev, "xfer error: %x\n", xfer->cmds[0].error);
	aspeed_i3c_master_free_xfer(xfer);

	return ret;
}

static int aspeed_i3c_master_attach_i2c_dev(struct i2c_dev_desc *dev)
{
	struct i3c_master_controller *m = i2c_dev_get_master(dev);
	struct aspeed_i3c_master *master = to_aspeed_i3c_master(m);
	struct aspeed_i3c_i2c_dev_data *data;
	int pos;

	pos = aspeed_i3c_master_set_group_dat(
		master, dev->addr,
		DEV_ADDR_TABLE_LEGACY_I2C_DEV |
			FIELD_PREP(DEV_ADDR_TABLE_STATIC_ADDR, dev->addr));

	if (pos < 0)
		return pos;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->index = aspeed_i3c_master_get_group_hw_index(master, dev->addr);
	master->addrs[data->index] = dev->addr;
	i2c_dev_set_master_data(dev, data);


	return 0;
}

static void aspeed_i3c_master_detach_i2c_dev(struct i2c_dev_desc *dev)
{
	struct aspeed_i3c_i2c_dev_data *data = i2c_dev_get_master_data(dev);
	struct i3c_master_controller *m = i2c_dev_get_master(dev);
	struct aspeed_i3c_master *master = to_aspeed_i3c_master(m);

	aspeed_i3c_master_set_group_dat(master, dev->addr, 0);

	i2c_dev_set_master_data(dev, NULL);
	master->addrs[data->index] = 0;
	kfree(data);
}

static void aspeed_i3c_slave_event_handler(struct aspeed_i3c_master *master)
{
	u32 event = readl(master->regs + SLV_EVENT_CTRL);
	u32 reg = readl(master->regs + PRESENT_STATE);
	u32 cm_state = FIELD_GET(CM_TFR_STS, reg);

	if (cm_state == CM_TFR_STS_SLAVE_HALT) {
		dev_dbg(master->dev, "slave in halt state\n");
		aspeed_i3c_master_resume(master);
	}

	dev_dbg(master->dev, "slave event=%08x\n", event);
	if (event & SLV_EVENT_CTRL_MRL_UPD)
		dev_dbg(master->dev, "isr: master set mrl=%d\n",
			readl(master->regs + SLV_MAX_LEN) >> 16);

	if (event & SLV_EVENT_CTRL_MWL_UPD)
		dev_dbg(master->dev, "isr: master set mwl=%ld\n",
			readl(master->regs + SLV_MAX_LEN) & GENMASK(15, 0));

	writel(event, master->regs + SLV_EVENT_CTRL);
}

static void aspeed_i3c_slave_resp_handler(struct aspeed_i3c_master *master,
					  u32 status)
{
	int i, has_error = 0;
	u32 resp, nbytes, nresp;
	u8 error, tid;
	u32 *buf = master->slave_data.buf;
	struct i3c_slave_payload payload;

	nresp = readl(master->regs + QUEUE_STATUS_LEVEL);
	nresp = QUEUE_STATUS_LEVEL_RESP(nresp);

	for (i = 0; i < nresp; i++) {
		resp = readl(master->regs + RESPONSE_QUEUE_PORT);
		error = RESPONSE_PORT_ERR_STATUS(resp);
		nbytes = RESPONSE_PORT_DATA_LEN(resp);
		tid = RESPONSE_PORT_TID(resp);

		if (error) {
			has_error = 1;
			if (error == RESPONSE_ERROR_EARLY_TERMINATE)
				dev_dbg(master->dev,
					"early termination, remain length %d\n",
					nbytes);
		}

		if (!error && nbytes) {
			aspeed_i3c_master_read_rx_fifo(master, (u8 *)buf, nbytes);

			payload.len = nbytes;
			payload.data = buf;
			/* currently only support master write transfer */
			if (master->slave_data.callback && (tid == TID_MASTER_WRITE_DATA))
				master->slave_data.callback(&master->base, &payload);
		}

		if (!error && !nbytes) {
			if (status & INTR_IBI_UPDATED_STAT && tid == TID_SLAVE_IBI_DONE)
				complete(&master->sir_complete);
			else if (tid == TID_MASTER_READ_DATA)
				complete(&master->data_read_complete);
			else
				dev_warn(master->dev, "Unreogized response %x",
					 resp);
		}
	}

	if (has_error) {
		writel(RESET_CTRL_QUEUES, master->regs + RESET_CTRL);
		aspeed_i3c_master_resume(master);
	}
}

static irqreturn_t aspeed_i3c_master_irq_handler(int irq, void *dev_id)
{
	struct aspeed_i3c_master *master = dev_id;
	u32 status;

	status = readl(master->regs + INTR_STATUS);

	if (!(status & readl(master->regs + INTR_STATUS_EN))) {
		writel(INTR_ALL, master->regs + INTR_STATUS);
		return IRQ_NONE;
	}

	if (master->secondary) {
		if (status & INTR_READ_REQ_RECV_STAT)
			dev_dbg(master->dev, "read queue received\n");

		if (status & INTR_RESP_READY_STAT)
			aspeed_i3c_slave_resp_handler(master, status);

		if (status & INTR_CCC_UPDATED_STAT)
			aspeed_i3c_slave_event_handler(master);
	} else {
		u32 reg, cm_state, xfr_state;

		if (status & INTR_RESP_READY_STAT ||
		    status & INTR_TRANSFER_ERR_STAT) {
			spin_lock(&master->xferqueue.lock);
			aspeed_i3c_master_end_xfer_locked(master, status);
			if (status & INTR_TRANSFER_ERR_STAT)
				writel(INTR_TRANSFER_ERR_STAT, master->regs + INTR_STATUS);
			spin_unlock(&master->xferqueue.lock);
		}

		if (status & INTR_IBI_THLD_STAT)
			aspeed_i3c_master_demux_ibis(master);

		/*
		 * check whether the controller is in halt state and resume the
		 * controller if it is in halt state
		 */
		reg = readl(master->regs + PRESENT_STATE);
		cm_state = FIELD_GET(CM_TFR_ST_STS, reg);
		xfr_state = FIELD_GET(CM_TFR_STS, reg);

		if (cm_state == CM_TFR_ST_STS_HALT ||
		    xfr_state == CM_TFR_STS_MASTER_HALT) {
			dev_dbg(master->dev, "master in halt state, resume\n");
			writel(RESET_CTRL_QUEUES, master->regs + RESET_CTRL);
			aspeed_i3c_master_resume(master);
		}
	}

	writel(status, master->regs + INTR_STATUS);

	return IRQ_HANDLED;
}

static void aspeed_i3c_master_enable_ibi_irq(struct aspeed_i3c_master *master)
{
	u32 reg;

	reg = readl(master->regs + INTR_STATUS_EN);
	reg |= INTR_IBI_THLD_STAT;
	writel(reg, master->regs + INTR_STATUS_EN);

	reg = readl(master->regs + INTR_SIGNAL_EN);
	reg |= INTR_IBI_THLD_STAT;
	writel(reg, master->regs + INTR_SIGNAL_EN);
}

static void aspeed_i3c_master_disable_ibi_irq(struct aspeed_i3c_master *master)
{
	u32 reg;

	reg = readl(master->regs + INTR_STATUS_EN);
	reg &= ~INTR_IBI_THLD_STAT;
	writel(reg, master->regs + INTR_STATUS_EN);

	reg = readl(master->regs + INTR_SIGNAL_EN);
	reg &= ~INTR_IBI_THLD_STAT;
	writel(reg, master->regs + INTR_SIGNAL_EN);
}

static int aspeed_i3c_master_disable_ibi(struct i3c_dev_desc *dev)
{
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct aspeed_i3c_master *master = to_aspeed_i3c_master(m);
	struct aspeed_i3c_i2c_dev_data *data = i3c_dev_get_master_data(dev);
	struct aspeed_i3c_dev_group *dev_grp =
		aspeed_i3c_master_get_group(master, dev->info.dyn_addr);
	unsigned long flags;
	u32 sirmap, dat, hj_nack;
	int ret, i;
	bool ibi_enable = false;

	ret = i3c_master_disec_locked(m, dev->info.dyn_addr,
				      I3C_CCC_EVENT_SIR);
	if (ret)
		return ret;

	spin_lock_irqsave(&master->ibi.lock, flags);
	dat = aspeed_i3c_master_get_group_dat(master, dev->info.dyn_addr);
	dat |= DEV_ADDR_TABLE_SIR_REJECT;
	dat &= ~DEV_ADDR_TABLE_IBI_WITH_DATA;
	aspeed_i3c_master_set_group_dat(master, dev->info.dyn_addr, dat);

	/*
	 * if any available device in this group still needs to enable ibi, then
	 * just keep the hw setting until all of the devices agree to disable ibi
	 */
	for (i = 0; i < MAX_DEVS_IN_GROUP; i++) {
		if ((!(dev_grp->free_pos & BIT(i))) &&
		    (!(dev_grp->dat[i] & DEV_ADDR_TABLE_SIR_REJECT))) {
			ibi_enable = true;
			break;
		}
	}

	if (!ibi_enable) {
		sirmap = readl(master->regs + IBI_SIR_REQ_REJECT);
		sirmap |= BIT(data->ibi);
		writel(sirmap, master->regs + IBI_SIR_REQ_REJECT);

		dev_grp->mask.clr |= DEV_ADDR_TABLE_IBI_WITH_DATA |
				     DEV_ADDR_TABLE_IBI_ADDR_MASK;
		dev_grp->mask.set &= ~DEV_ADDR_TABLE_IBI_WITH_DATA;
		dev_grp->mask.set |= DEV_ADDR_TABLE_SIR_REJECT;
	}

	sirmap = readl(master->regs + IBI_SIR_REQ_REJECT);
	hj_nack = readl(master->regs + DEVICE_CTRL) & DEV_CTRL_HOT_JOIN_NACK;
	if (sirmap == IBI_REQ_REJECT_ALL && hj_nack)
		aspeed_i3c_master_disable_ibi_irq(master);
	else
		aspeed_i3c_master_enable_ibi_irq(master);

	spin_unlock_irqrestore(&master->ibi.lock, flags);

	return ret;
}

static int aspeed_i3c_master_enable_ibi(struct i3c_dev_desc *dev)
{
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct aspeed_i3c_master *master = to_aspeed_i3c_master(m);
	struct aspeed_i3c_i2c_dev_data *data = i3c_dev_get_master_data(dev);
	struct aspeed_i3c_dev_group *dev_grp =
		aspeed_i3c_master_get_group(master, dev->info.dyn_addr);
	unsigned long flags;
	u32 sirmap, hj_nack;
	u32 sirmap_backup, mask_clr_backup, mask_set_backup;
	int ret;

	spin_lock_irqsave(&master->ibi.lock, flags);
	sirmap_backup = readl(master->regs + IBI_SIR_REQ_REJECT);
	sirmap = sirmap_backup & ~BIT(data->ibi);
	writel(sirmap, master->regs + IBI_SIR_REQ_REJECT);

	mask_clr_backup = dev_grp->mask.clr;
	mask_set_backup = dev_grp->mask.set;
	dev_grp->mask.clr |= DEV_ADDR_TABLE_SIR_REJECT | DEV_ADDR_TABLE_IBI_ADDR_MASK;
	dev_grp->mask.set &= ~DEV_ADDR_TABLE_SIR_REJECT;
	dev_grp->mask.set |= FIELD_PREP(DEV_ADDR_TABLE_IBI_ADDR_MASK,
					IBI_ADDR_MASK_LAST_3BITS);
	if (IS_MANUF_ID_ASPEED(dev->info.pid))
		dev_grp->mask.set |= DEV_ADDR_TABLE_IBI_PEC_EN;
	if (dev->info.bcr & I3C_BCR_IBI_PAYLOAD)
		dev_grp->mask.set |= DEV_ADDR_TABLE_IBI_WITH_DATA;

	spin_unlock_irqrestore(&master->ibi.lock, flags);

	dev_dbg(master->dev, "addr:%x, hw_index:%d, data->ibi:%d, mask: %08x %08x\n",
		dev->info.dyn_addr, dev_grp->hw_index, data->ibi, dev_grp->mask.set,
		dev_grp->mask.clr);

	/* Dat will be synchronized before sending the CCC */
	ret = i3c_master_enec_locked(m, dev->info.dyn_addr,
				     I3C_CCC_EVENT_SIR);

	if (ret) {
		spin_lock_irqsave(&master->ibi.lock, flags);
		writel(sirmap_backup, master->regs + IBI_SIR_REQ_REJECT);

		dev_grp->mask.clr = mask_clr_backup;
		dev_grp->mask.set = mask_set_backup;
		aspeed_i3c_master_sync_hw_dat(master, dev->info.dyn_addr);
		spin_unlock_irqrestore(&master->ibi.lock, flags);
	}

	sirmap = readl(master->regs + IBI_SIR_REQ_REJECT);
	hj_nack = readl(master->regs + DEVICE_CTRL) & DEV_CTRL_HOT_JOIN_NACK;
	if (sirmap == IBI_REQ_REJECT_ALL && hj_nack)
		aspeed_i3c_master_disable_ibi_irq(master);
	else
		aspeed_i3c_master_enable_ibi_irq(master);

	return ret;
}

static int aspeed_i3c_master_request_ibi(struct i3c_dev_desc *dev,
				       const struct i3c_ibi_setup *req)
{
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct aspeed_i3c_master *master = to_aspeed_i3c_master(m);
	struct aspeed_i3c_i2c_dev_data *data = i3c_dev_get_master_data(dev);
	unsigned long flags;
	unsigned int i;

	data->ibi_pool = i3c_generic_ibi_alloc_pool(dev, req);
	if (IS_ERR(data->ibi_pool))
		return PTR_ERR(data->ibi_pool);

	spin_lock_irqsave(&master->ibi.lock, flags);
	master->ibi.slots[dev->info.dyn_addr & 0x7f] = dev;
	master->ibi.received_ibi_len[dev->info.dyn_addr & 0x7f] = 0;
	data->ibi = aspeed_i3c_master_get_group_hw_index(master,
							 dev->info.dyn_addr);
	spin_unlock_irqrestore(&master->ibi.lock, flags);

	if (i < MAX_DEVS)
		return 0;

	i3c_generic_ibi_free_pool(data->ibi_pool);
	data->ibi_pool = NULL;

	return -ENOSPC;
}

static void aspeed_i3c_master_free_ibi(struct i3c_dev_desc *dev)
{
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct aspeed_i3c_master *master = to_aspeed_i3c_master(m);
	struct aspeed_i3c_i2c_dev_data *data = i3c_dev_get_master_data(dev);
	unsigned long flags;

	spin_lock_irqsave(&master->ibi.lock, flags);
	master->ibi.slots[dev->info.dyn_addr] = NULL;
	data->ibi = -1;
	spin_unlock_irqrestore(&master->ibi.lock, flags);

	i3c_generic_ibi_free_pool(data->ibi_pool);
}

static void aspeed_i3c_master_recycle_ibi_slot(struct i3c_dev_desc *dev,
					     struct i3c_ibi_slot *slot)
{
	struct aspeed_i3c_i2c_dev_data *data = i3c_dev_get_master_data(dev);

	i3c_generic_ibi_recycle_slot(data->ibi_pool, slot);
}

static int aspeed_i3c_master_register_slave(struct i3c_master_controller *m,
			      const struct i3c_slave_setup *req)
{
	struct aspeed_i3c_master *master = to_aspeed_i3c_master(m);
	u32 *buf;

	buf = kzalloc(req->max_payload_len, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	master->slave_data.callback = req->handler;
	master->slave_data.buf = buf;

	return 0;
}

static int aspeed_i3c_master_unregister_slave(struct i3c_master_controller *m)
{
	struct aspeed_i3c_master *master = to_aspeed_i3c_master(m);

	master->slave_data.callback = NULL;
	kfree(master->slave_data.buf);

	return 0;
}

static int aspeed_i3c_master_send_sir(struct i3c_master_controller *m,
				      struct i3c_slave_payload *payload)
{
	struct aspeed_i3c_master *master = to_aspeed_i3c_master(m);
	uint32_t slv_event, intr_req, reg, thld_ctrl;
	uint8_t *data = (uint8_t *)payload->data;

	slv_event = readl(master->regs + SLV_EVENT_CTRL);
	if ((slv_event & SLV_EVENT_CTRL_SIR_EN) == 0)
		return -EPERM;

	if (!payload)
		return -ENXIO;

	if (payload->len > (CONFIG_AST2600_I3C_IBI_MAX_PAYLOAD + 1)) {
		dev_err(master->dev,
			"input length %d exceeds max ibi payload size %d\n",
			payload->len, CONFIG_AST2600_I3C_IBI_MAX_PAYLOAD + 1);
		return -E2BIG;
	}

	init_completion(&master->sir_complete);

	reg = readl(master->regs + DEVICE_CTRL);
	reg &= ~DEV_CTRL_SLAVE_MDB;
	reg |= FIELD_PREP(DEV_CTRL_SLAVE_MDB, data[0]);
	writel(reg, master->regs + DEVICE_CTRL);

	aspeed_i3c_master_wr_tx_fifo(master, data, payload->len);

	reg = FIELD_PREP(COMMAND_PORT_SLAVE_DATA_LEN, payload->len);
	writel(reg, master->regs + COMMAND_QUEUE_PORT);

	thld_ctrl = readl(master->regs + QUEUE_THLD_CTRL);
	thld_ctrl &= ~QUEUE_THLD_CTRL_RESP_BUF_MASK;
	thld_ctrl |= QUEUE_THLD_CTRL_RESP_BUF(1);
	writel(thld_ctrl, master->regs + QUEUE_THLD_CTRL);

	writel(1, master->regs + SLV_INTR_REQ);
	if (!wait_for_completion_timeout(&master->sir_complete, XFER_TIMEOUT)) {
		dev_err(master->dev, "send sir timeout\n");
		writel(RESET_CTRL_RX_FIFO | RESET_CTRL_TX_FIFO |
			       RESET_CTRL_RESP_QUEUE | RESET_CTRL_CMD_QUEUE,
		       master->regs + RESET_CTRL);
	}

	intr_req = readl(master->regs + SLV_INTR_REQ);
	if (SLV_INTR_REQ_IBI_STS(intr_req) != SLV_IBI_STS_OK) {
		slv_event = readl(master->regs + SLV_EVENT_CTRL);
		if ((slv_event & SLV_EVENT_CTRL_SIR_EN) == 0)
			pr_warn("sir is disabled by master\n");
		return -EACCES;
	}

	return 0;
}

static int aspeed_i3c_slave_reset_queue(struct aspeed_i3c_master *master)
{
	int ret = 0;

	ret = aspeed_i3c_master_disable(master);
	if (ret)
		return ret;
	writel(RESET_CTRL_QUEUES, master->regs + RESET_CTRL);
	ret = aspeed_i3c_master_enable(master);
	if (ret)
		return ret;
	return ret;
}

static int aspeed_i3c_master_put_read_data(struct i3c_master_controller *m,
					   struct i3c_slave_payload *data,
					   struct i3c_slave_payload *ibi_notify)
{
	struct aspeed_i3c_master *master = to_aspeed_i3c_master(m);
	u32 reg, thld_ctrl;
	u8 *buf;
	int ret;

	if (!data)
		return -ENXIO;

	if (ibi_notify) {
		buf = (u8 *)ibi_notify->data;
		init_completion(&master->sir_complete);

		reg = readl(master->regs + DEVICE_CTRL);
		reg &= ~DEV_CTRL_SLAVE_MDB;
		reg |= FIELD_PREP(DEV_CTRL_SLAVE_MDB, buf[0]);
		writel(reg, master->regs + DEVICE_CTRL);

		aspeed_i3c_master_wr_tx_fifo(master, buf, ibi_notify->len);

		reg = FIELD_PREP(COMMAND_PORT_SLAVE_DATA_LEN, ibi_notify->len) |
		      COMMAND_PORT_SLAVE_TID(TID_SLAVE_IBI_DONE);
		writel(reg, master->regs + COMMAND_QUEUE_PORT);

		thld_ctrl = readl(master->regs + QUEUE_THLD_CTRL);
		thld_ctrl &= ~QUEUE_THLD_CTRL_RESP_BUF_MASK;
		thld_ctrl |= QUEUE_THLD_CTRL_RESP_BUF(1);
		writel(thld_ctrl, master->regs + QUEUE_THLD_CTRL);
	}

	buf = (u8 *)data->data;
	init_completion(&master->data_read_complete);
	aspeed_i3c_master_wr_tx_fifo(master, buf, data->len);

	reg = FIELD_PREP(COMMAND_PORT_SLAVE_DATA_LEN, data->len) |
	      COMMAND_PORT_SLAVE_TID(TID_MASTER_READ_DATA);
	writel(reg, master->regs + COMMAND_QUEUE_PORT);

	if (ibi_notify) {
		writel(1, master->regs + SLV_INTR_REQ);
		if (!wait_for_completion_timeout(&master->sir_complete,
						 XFER_TIMEOUT)) {
			dev_err(master->dev, "send sir timeout\n");
			writel(RESET_CTRL_QUEUES, master->regs + RESET_CTRL);
		}
	}

	/* Wait data to be read */
	if (!wait_for_completion_timeout(&master->data_read_complete,
						 XFER_TIMEOUT)) {
		dev_err(master->dev, "wait master read timeout\n");
		ret = aspeed_i3c_slave_reset_queue(master);
		if (ret) {
			dev_err(master->dev, "i3c queue reset failed");
			return ret;
		}
	}

	return 0;
}

static int aspeed_i3c_master_timing_config(struct aspeed_i3c_master *master,
					   struct device_node *np)
{
	u32 val, reg;
	u32 timed_reset_scl_low_ns;
	u32 sda_tx_hold_ns;

	master->timing.core_rate = clk_get_rate(master->core_clk);
	if (!master->timing.core_rate) {
		dev_err(master->dev, "core clock rate not found\n");
		return -EINVAL;
	}

	/* core_period is in nanosecond */
	master->timing.core_period =
		DIV_ROUND_UP(1000000000, master->timing.core_rate);

	/* setup default timing configuration */
	sda_tx_hold_ns = SDA_TX_HOLD_MIN * master->timing.core_period;
	timed_reset_scl_low_ns = JESD403_TIMED_RESET_NS_DEF;

	/* parse configurations from DT */
	if (!of_property_read_u32(np, "i3c-pp-scl-hi-period-ns", &val))
		master->timing.i3c_pp_scl_high = val;

	if (!of_property_read_u32(np, "i3c-pp-scl-lo-period-ns", &val))
		master->timing.i3c_pp_scl_low = val;

	if (!of_property_read_u32(np, "i3c-od-scl-hi-period-ns", &val))
		master->timing.i3c_od_scl_high = val;

	if (!of_property_read_u32(np, "i3c-od-scl-lo-period-ns", &val))
		master->timing.i3c_od_scl_low = val;

	if (!of_property_read_u32(np, "sda-tx-hold-ns", &val))
		sda_tx_hold_ns = val;

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

static const struct i3c_master_controller_ops aspeed_i3c_ops = {
	.bus_init = aspeed_i3c_master_bus_init,
	.bus_cleanup = aspeed_i3c_master_bus_cleanup,
	.bus_reset = aspeed_i3c_master_bus_reset,
	.attach_i3c_dev = aspeed_i3c_master_attach_i3c_dev,
	.reattach_i3c_dev = aspeed_i3c_master_reattach_i3c_dev,
	.detach_i3c_dev = aspeed_i3c_master_detach_i3c_dev,
	.do_daa = aspeed_i3c_master_daa,
	.supports_ccc_cmd = aspeed_i3c_master_supports_ccc_cmd,
	.send_ccc_cmd = aspeed_i3c_master_send_ccc_cmd,
	.send_hdr_cmds = aspeed_i3c_master_send_hdr_cmd,
	.priv_xfers = aspeed_i3c_master_priv_xfers,
	.attach_i2c_dev = aspeed_i3c_master_attach_i2c_dev,
	.detach_i2c_dev = aspeed_i3c_master_detach_i2c_dev,
	.i2c_xfers = aspeed_i3c_master_i2c_xfers,
	.enable_ibi = aspeed_i3c_master_enable_ibi,
	.disable_ibi = aspeed_i3c_master_disable_ibi,
	.request_ibi = aspeed_i3c_master_request_ibi,
	.free_ibi = aspeed_i3c_master_free_ibi,
	.recycle_ibi_slot = aspeed_i3c_master_recycle_ibi_slot,
	.register_slave = aspeed_i3c_master_register_slave,
	.unregister_slave = aspeed_i3c_master_unregister_slave,
	.send_sir = aspeed_i3c_master_send_sir,
	.put_read_data = aspeed_i3c_master_put_read_data,
};

static void aspeed_i3c_master_hj(struct work_struct *work)
{
	struct aspeed_i3c_master *master =
		container_of(work, typeof(*master), hj_work);

	i3c_master_do_daa(&master->base);
}

static int aspeed_i3c_master_enable_hj(struct aspeed_i3c_master *master)
{
	int ret;

	aspeed_i3c_master_enable_ibi_irq(master);
	ast_clrbits(master->regs + DEVICE_CTRL, DEV_CTRL_HOT_JOIN_NACK);
	ret = i3c_master_enable_hj(&master->base);

	return ret;
}

static int aspeed_i3c_probe(struct platform_device *pdev)
{
	struct aspeed_i3c_master *master;
	struct device_node *np;
	int ret, irq;

	master = devm_kzalloc(&pdev->dev, sizeof(*master), GFP_KERNEL);
	if (!master)
		return -ENOMEM;

	master->i3cg = syscon_regmap_lookup_by_phandle(pdev->dev.of_node,
						       "aspeed,i3cg");
	if (IS_ERR(master->i3cg)) {
		dev_err(master->dev,
			"i3c controller missing 'aspeed,i3cg' property\n");
		return PTR_ERR(master->i3cg);
	}

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

	spin_lock_init(&master->ibi.lock);
	spin_lock_init(&master->xferqueue.lock);
	INIT_LIST_HEAD(&master->xferqueue.list);

	writel(RESET_CTRL_ALL, master->regs + RESET_CTRL);
	while (readl(master->regs + RESET_CTRL))
		;

	writel(INTR_ALL, master->regs + INTR_STATUS);
	irq = platform_get_irq(pdev, 0);
	ret = devm_request_irq(&pdev->dev, irq,
			       aspeed_i3c_master_irq_handler, 0,
			       dev_name(&pdev->dev), master);
	if (ret)
		goto err_assert_rst;

	platform_set_drvdata(pdev, master);

	np = pdev->dev.of_node;
	if (of_get_property(np, "secondary", NULL))
		master->secondary = true;
	else
		master->secondary = false;

	ret = of_property_read_u32(np, "i3c_chan", &master->channel);
	if ((ret != 0) || (master->channel > I3C_CHANNEL_MAX)) {
		dev_err(&pdev->dev, "no valid 'i3c_chan' %d %d configured\n", ret, master->channel);
		return -EINVAL;
	}

	ret = aspeed_i3c_master_timing_config(master, np);
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
	aspeed_i3c_master_init_group_dat(master);
#ifdef CONFIG_AST2600_I3C_CCC_WORKAROUND
	master->free_pos &= ~BIT(master->maxdevs - 1);
	ret = (even_parity(I3C_BROADCAST_ADDR) << 7) | I3C_BROADCAST_ADDR;
	master->addrs[master->maxdevs - 1] = ret;
	writel(FIELD_PREP(DEV_ADDR_TABLE_DYNAMIC_ADDR, ret),
	       master->regs + DEV_ADDR_TABLE_LOC(master->datstartaddr,
						 master->maxdevs - 1));
#endif
	master->dev = &pdev->dev;
	master->base.pec_supported = true;
	INIT_WORK(&master->hj_work, aspeed_i3c_master_hj);
	ret = i3c_master_register(&master->base, &pdev->dev,
				  &aspeed_i3c_ops, master->secondary);
	if (ret)
		goto err_assert_rst;

	if (!master->secondary && !master->base.jdec_spd) {
		aspeed_i3c_master_iba_ctrl(master, true);
		ret = aspeed_i3c_master_enable_hj(master);
		if (ret)
			goto err_master_register;
	}

	return 0;

err_master_register:
	i3c_master_unregister(&master->base);

err_assert_rst:
	reset_control_assert(master->core_rst);

err_disable_core_clk:
	clk_disable_unprepare(master->core_clk);

	return ret;
}

static int aspeed_i3c_remove(struct platform_device *pdev)
{
	struct aspeed_i3c_master *master = platform_get_drvdata(pdev);
	int ret;

	ret = i3c_master_unregister(&master->base);
	if (ret)
		return ret;

	reset_control_assert(master->core_rst);

	clk_disable_unprepare(master->core_clk);

	return 0;
}

static const struct of_device_id aspeed_i3c_master_of_match[] = {
	{ .compatible = "aspeed,ast2600-i3c", },
	{},
};
MODULE_DEVICE_TABLE(of, aspeed_i3c_master_of_match);

static struct platform_driver aspeed_i3c_driver = {
	.probe = aspeed_i3c_probe,
	.remove = aspeed_i3c_remove,
	.driver = {
		.name = "ast2600-i3c-master",
		.of_match_table = of_match_ptr(aspeed_i3c_master_of_match),
	},
};
module_platform_driver(aspeed_i3c_driver);

MODULE_AUTHOR("Dylan Hung <dylan_hung@aspeedtech.com>");
MODULE_DESCRIPTION("Aspeed MIPI I3C driver");
MODULE_LICENSE("GPL v2");
