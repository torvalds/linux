// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 Cadence Design Systems Inc.
 *
 * Author: Boris Brezillon <boris.brezillon@bootlin.com>
 */

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/i3c/master.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>

#define DEV_ID				0x0
#define DEV_ID_I3C_MASTER		0x5034

#define CONF_STATUS0			0x4
#define CONF_STATUS0_CMDR_DEPTH(x)	(4 << (((x) & GENMASK(31, 29)) >> 29))
#define CONF_STATUS0_ECC_CHK		BIT(28)
#define CONF_STATUS0_INTEG_CHK		BIT(27)
#define CONF_STATUS0_CSR_DAP_CHK	BIT(26)
#define CONF_STATUS0_TRANS_TOUT_CHK	BIT(25)
#define CONF_STATUS0_PROT_FAULTS_CHK	BIT(24)
#define CONF_STATUS0_GPO_NUM(x)		(((x) & GENMASK(23, 16)) >> 16)
#define CONF_STATUS0_GPI_NUM(x)		(((x) & GENMASK(15, 8)) >> 8)
#define CONF_STATUS0_IBIR_DEPTH(x)	(4 << (((x) & GENMASK(7, 6)) >> 7))
#define CONF_STATUS0_SUPPORTS_DDR	BIT(5)
#define CONF_STATUS0_SEC_MASTER		BIT(4)
#define CONF_STATUS0_DEVS_NUM(x)	((x) & GENMASK(3, 0))

#define CONF_STATUS1			0x8
#define CONF_STATUS1_IBI_HW_RES(x)	((((x) & GENMASK(31, 28)) >> 28) + 1)
#define CONF_STATUS1_CMD_DEPTH(x)	(4 << (((x) & GENMASK(27, 26)) >> 26))
#define CONF_STATUS1_SLVDDR_RX_DEPTH(x)	(8 << (((x) & GENMASK(25, 21)) >> 21))
#define CONF_STATUS1_SLVDDR_TX_DEPTH(x)	(8 << (((x) & GENMASK(20, 16)) >> 16))
#define CONF_STATUS1_IBI_DEPTH(x)	(2 << (((x) & GENMASK(12, 10)) >> 10))
#define CONF_STATUS1_RX_DEPTH(x)	(8 << (((x) & GENMASK(9, 5)) >> 5))
#define CONF_STATUS1_TX_DEPTH(x)	(8 << ((x) & GENMASK(4, 0)))

#define REV_ID				0xc
#define REV_ID_VID(id)			(((id) & GENMASK(31, 20)) >> 20)
#define REV_ID_PID(id)			(((id) & GENMASK(19, 8)) >> 8)
#define REV_ID_REV_MAJOR(id)		(((id) & GENMASK(7, 4)) >> 4)
#define REV_ID_REV_MINOR(id)		((id) & GENMASK(3, 0))

#define CTRL				0x10
#define CTRL_DEV_EN			BIT(31)
#define CTRL_HALT_EN			BIT(30)
#define CTRL_MCS			BIT(29)
#define CTRL_MCS_EN			BIT(28)
#define CTRL_THD_DELAY(x)		(((x) << 24) & GENMASK(25, 24))
#define CTRL_HJ_DISEC			BIT(8)
#define CTRL_MST_ACK			BIT(7)
#define CTRL_HJ_ACK			BIT(6)
#define CTRL_HJ_INIT			BIT(5)
#define CTRL_MST_INIT			BIT(4)
#define CTRL_AHDR_OPT			BIT(3)
#define CTRL_PURE_BUS_MODE		0
#define CTRL_MIXED_FAST_BUS_MODE	2
#define CTRL_MIXED_SLOW_BUS_MODE	3
#define CTRL_BUS_MODE_MASK		GENMASK(1, 0)
#define THD_DELAY_MAX			3

#define PRESCL_CTRL0			0x14
#define PRESCL_CTRL0_I2C(x)		((x) << 16)
#define PRESCL_CTRL0_I3C(x)		(x)
#define PRESCL_CTRL0_I3C_MAX		GENMASK(9, 0)
#define PRESCL_CTRL0_I2C_MAX		GENMASK(15, 0)

#define PRESCL_CTRL1			0x18
#define PRESCL_CTRL1_PP_LOW_MASK	GENMASK(15, 8)
#define PRESCL_CTRL1_PP_LOW(x)		((x) << 8)
#define PRESCL_CTRL1_OD_LOW_MASK	GENMASK(7, 0)
#define PRESCL_CTRL1_OD_LOW(x)		(x)

#define MST_IER				0x20
#define MST_IDR				0x24
#define MST_IMR				0x28
#define MST_ICR				0x2c
#define MST_ISR				0x30
#define MST_INT_HALTED			BIT(18)
#define MST_INT_MR_DONE			BIT(17)
#define MST_INT_IMM_COMP		BIT(16)
#define MST_INT_TX_THR			BIT(15)
#define MST_INT_TX_OVF			BIT(14)
#define MST_INT_IBID_THR		BIT(12)
#define MST_INT_IBID_UNF		BIT(11)
#define MST_INT_IBIR_THR		BIT(10)
#define MST_INT_IBIR_UNF		BIT(9)
#define MST_INT_IBIR_OVF		BIT(8)
#define MST_INT_RX_THR			BIT(7)
#define MST_INT_RX_UNF			BIT(6)
#define MST_INT_CMDD_EMP		BIT(5)
#define MST_INT_CMDD_THR		BIT(4)
#define MST_INT_CMDD_OVF		BIT(3)
#define MST_INT_CMDR_THR		BIT(2)
#define MST_INT_CMDR_UNF		BIT(1)
#define MST_INT_CMDR_OVF		BIT(0)

#define MST_STATUS0			0x34
#define MST_STATUS0_IDLE		BIT(18)
#define MST_STATUS0_HALTED		BIT(17)
#define MST_STATUS0_MASTER_MODE		BIT(16)
#define MST_STATUS0_TX_FULL		BIT(13)
#define MST_STATUS0_IBID_FULL		BIT(12)
#define MST_STATUS0_IBIR_FULL		BIT(11)
#define MST_STATUS0_RX_FULL		BIT(10)
#define MST_STATUS0_CMDD_FULL		BIT(9)
#define MST_STATUS0_CMDR_FULL		BIT(8)
#define MST_STATUS0_TX_EMP		BIT(5)
#define MST_STATUS0_IBID_EMP		BIT(4)
#define MST_STATUS0_IBIR_EMP		BIT(3)
#define MST_STATUS0_RX_EMP		BIT(2)
#define MST_STATUS0_CMDD_EMP		BIT(1)
#define MST_STATUS0_CMDR_EMP		BIT(0)

#define CMDR				0x38
#define CMDR_NO_ERROR			0
#define CMDR_DDR_PREAMBLE_ERROR		1
#define CMDR_DDR_PARITY_ERROR		2
#define CMDR_DDR_RX_FIFO_OVF		3
#define CMDR_DDR_TX_FIFO_UNF		4
#define CMDR_M0_ERROR			5
#define CMDR_M1_ERROR			6
#define CMDR_M2_ERROR			7
#define CMDR_MST_ABORT			8
#define CMDR_NACK_RESP			9
#define CMDR_INVALID_DA			10
#define CMDR_DDR_DROPPED		11
#define CMDR_ERROR(x)			(((x) & GENMASK(27, 24)) >> 24)
#define CMDR_XFER_BYTES(x)		(((x) & GENMASK(19, 8)) >> 8)
#define CMDR_CMDID_HJACK_DISEC		0xfe
#define CMDR_CMDID_HJACK_ENTDAA		0xff
#define CMDR_CMDID(x)			((x) & GENMASK(7, 0))

#define IBIR				0x3c
#define IBIR_ACKED			BIT(12)
#define IBIR_SLVID(x)			(((x) & GENMASK(11, 8)) >> 8)
#define IBIR_ERROR			BIT(7)
#define IBIR_XFER_BYTES(x)		(((x) & GENMASK(6, 2)) >> 2)
#define IBIR_TYPE_IBI			0
#define IBIR_TYPE_HJ			1
#define IBIR_TYPE_MR			2
#define IBIR_TYPE(x)			((x) & GENMASK(1, 0))

#define SLV_IER				0x40
#define SLV_IDR				0x44
#define SLV_IMR				0x48
#define SLV_ICR				0x4c
#define SLV_ISR				0x50
#define SLV_INT_TM			BIT(20)
#define SLV_INT_ERROR			BIT(19)
#define SLV_INT_EVENT_UP		BIT(18)
#define SLV_INT_HJ_DONE			BIT(17)
#define SLV_INT_MR_DONE			BIT(16)
#define SLV_INT_DA_UPD			BIT(15)
#define SLV_INT_SDR_FAIL		BIT(14)
#define SLV_INT_DDR_FAIL		BIT(13)
#define SLV_INT_M_RD_ABORT		BIT(12)
#define SLV_INT_DDR_RX_THR		BIT(11)
#define SLV_INT_DDR_TX_THR		BIT(10)
#define SLV_INT_SDR_RX_THR		BIT(9)
#define SLV_INT_SDR_TX_THR		BIT(8)
#define SLV_INT_DDR_RX_UNF		BIT(7)
#define SLV_INT_DDR_TX_OVF		BIT(6)
#define SLV_INT_SDR_RX_UNF		BIT(5)
#define SLV_INT_SDR_TX_OVF		BIT(4)
#define SLV_INT_DDR_RD_COMP		BIT(3)
#define SLV_INT_DDR_WR_COMP		BIT(2)
#define SLV_INT_SDR_RD_COMP		BIT(1)
#define SLV_INT_SDR_WR_COMP		BIT(0)

#define SLV_STATUS0			0x54
#define SLV_STATUS0_REG_ADDR(s)		(((s) & GENMASK(23, 16)) >> 16)
#define SLV_STATUS0_XFRD_BYTES(s)	((s) & GENMASK(15, 0))

#define SLV_STATUS1			0x58
#define SLV_STATUS1_AS(s)		(((s) & GENMASK(21, 20)) >> 20)
#define SLV_STATUS1_VEN_TM		BIT(19)
#define SLV_STATUS1_HJ_DIS		BIT(18)
#define SLV_STATUS1_MR_DIS		BIT(17)
#define SLV_STATUS1_PROT_ERR		BIT(16)
#define SLV_STATUS1_DA(s)		(((s) & GENMASK(15, 9)) >> 9)
#define SLV_STATUS1_HAS_DA		BIT(8)
#define SLV_STATUS1_DDR_RX_FULL		BIT(7)
#define SLV_STATUS1_DDR_TX_FULL		BIT(6)
#define SLV_STATUS1_DDR_RX_EMPTY	BIT(5)
#define SLV_STATUS1_DDR_TX_EMPTY	BIT(4)
#define SLV_STATUS1_SDR_RX_FULL		BIT(3)
#define SLV_STATUS1_SDR_TX_FULL		BIT(2)
#define SLV_STATUS1_SDR_RX_EMPTY	BIT(1)
#define SLV_STATUS1_SDR_TX_EMPTY	BIT(0)

#define CMD0_FIFO			0x60
#define CMD0_FIFO_IS_DDR		BIT(31)
#define CMD0_FIFO_IS_CCC		BIT(30)
#define CMD0_FIFO_BCH			BIT(29)
#define XMIT_BURST_STATIC_SUBADDR	0
#define XMIT_SINGLE_INC_SUBADDR		1
#define XMIT_SINGLE_STATIC_SUBADDR	2
#define XMIT_BURST_WITHOUT_SUBADDR	3
#define CMD0_FIFO_PRIV_XMIT_MODE(m)	((m) << 27)
#define CMD0_FIFO_SBCA			BIT(26)
#define CMD0_FIFO_RSBC			BIT(25)
#define CMD0_FIFO_IS_10B		BIT(24)
#define CMD0_FIFO_PL_LEN(l)		((l) << 12)
#define CMD0_FIFO_PL_LEN_MAX		4095
#define CMD0_FIFO_DEV_ADDR(a)		((a) << 1)
#define CMD0_FIFO_RNW			BIT(0)

#define CMD1_FIFO			0x64
#define CMD1_FIFO_CMDID(id)		((id) << 24)
#define CMD1_FIFO_CSRADDR(a)		(a)
#define CMD1_FIFO_CCC(id)		(id)

#define TX_FIFO				0x68

#define IMD_CMD0			0x70
#define IMD_CMD0_PL_LEN(l)		((l) << 12)
#define IMD_CMD0_DEV_ADDR(a)		((a) << 1)
#define IMD_CMD0_RNW			BIT(0)

#define IMD_CMD1			0x74
#define IMD_CMD1_CCC(id)		(id)

#define IMD_DATA			0x78
#define RX_FIFO				0x80
#define IBI_DATA_FIFO			0x84
#define SLV_DDR_TX_FIFO			0x88
#define SLV_DDR_RX_FIFO			0x8c

#define CMD_IBI_THR_CTRL		0x90
#define IBIR_THR(t)			((t) << 24)
#define CMDR_THR(t)			((t) << 16)
#define IBI_THR(t)			((t) << 8)
#define CMD_THR(t)			(t)

#define TX_RX_THR_CTRL			0x94
#define RX_THR(t)			((t) << 16)
#define TX_THR(t)			(t)

#define SLV_DDR_TX_RX_THR_CTRL		0x98
#define SLV_DDR_RX_THR(t)		((t) << 16)
#define SLV_DDR_TX_THR(t)		(t)

#define FLUSH_CTRL			0x9c
#define FLUSH_IBI_RESP			BIT(23)
#define FLUSH_CMD_RESP			BIT(22)
#define FLUSH_SLV_DDR_RX_FIFO		BIT(22)
#define FLUSH_SLV_DDR_TX_FIFO		BIT(21)
#define FLUSH_IMM_FIFO			BIT(20)
#define FLUSH_IBI_FIFO			BIT(19)
#define FLUSH_RX_FIFO			BIT(18)
#define FLUSH_TX_FIFO			BIT(17)
#define FLUSH_CMD_FIFO			BIT(16)

#define TTO_PRESCL_CTRL0		0xb0
#define TTO_PRESCL_CTRL0_DIVB(x)	((x) << 16)
#define TTO_PRESCL_CTRL0_DIVA(x)	(x)

#define TTO_PRESCL_CTRL1		0xb4
#define TTO_PRESCL_CTRL1_DIVB(x)	((x) << 16)
#define TTO_PRESCL_CTRL1_DIVA(x)	(x)

#define DEVS_CTRL			0xb8
#define DEVS_CTRL_DEV_CLR_SHIFT		16
#define DEVS_CTRL_DEV_CLR_ALL		GENMASK(31, 16)
#define DEVS_CTRL_DEV_CLR(dev)		BIT(16 + (dev))
#define DEVS_CTRL_DEV_ACTIVE(dev)	BIT(dev)
#define DEVS_CTRL_DEVS_ACTIVE_MASK	GENMASK(15, 0)
#define MAX_DEVS			16

#define DEV_ID_RR0(d)			(0xc0 + ((d) * 0x10))
#define DEV_ID_RR0_LVR_EXT_ADDR		BIT(11)
#define DEV_ID_RR0_HDR_CAP		BIT(10)
#define DEV_ID_RR0_IS_I3C		BIT(9)
#define DEV_ID_RR0_DEV_ADDR_MASK	(GENMASK(6, 0) | GENMASK(15, 13))
#define DEV_ID_RR0_SET_DEV_ADDR(a)	(((a) & GENMASK(6, 0)) |	\
					 (((a) & GENMASK(9, 7)) << 6))
#define DEV_ID_RR0_GET_DEV_ADDR(x)	((((x) >> 1) & GENMASK(6, 0)) |	\
					 (((x) >> 6) & GENMASK(9, 7)))

#define DEV_ID_RR1(d)			(0xc4 + ((d) * 0x10))
#define DEV_ID_RR1_PID_MSB(pid)		(pid)

#define DEV_ID_RR2(d)			(0xc8 + ((d) * 0x10))
#define DEV_ID_RR2_PID_LSB(pid)		((pid) << 16)
#define DEV_ID_RR2_BCR(bcr)		((bcr) << 8)
#define DEV_ID_RR2_DCR(dcr)		(dcr)
#define DEV_ID_RR2_LVR(lvr)		(lvr)

#define SIR_MAP(x)			(0x180 + ((x) * 4))
#define SIR_MAP_DEV_REG(d)		SIR_MAP((d) / 2)
#define SIR_MAP_DEV_SHIFT(d, fs)	((fs) + (((d) % 2) ? 16 : 0))
#define SIR_MAP_DEV_CONF_MASK(d)	(GENMASK(15, 0) << (((d) % 2) ? 16 : 0))
#define SIR_MAP_DEV_CONF(d, c)		((c) << (((d) % 2) ? 16 : 0))
#define DEV_ROLE_SLAVE			0
#define DEV_ROLE_MASTER			1
#define SIR_MAP_DEV_ROLE(role)		((role) << 14)
#define SIR_MAP_DEV_SLOW		BIT(13)
#define SIR_MAP_DEV_PL(l)		((l) << 8)
#define SIR_MAP_PL_MAX			GENMASK(4, 0)
#define SIR_MAP_DEV_DA(a)		((a) << 1)
#define SIR_MAP_DEV_ACK			BIT(0)

#define GPIR_WORD(x)			(0x200 + ((x) * 4))
#define GPI_REG(val, id)		\
	(((val) >> (((id) % 4) * 8)) & GENMASK(7, 0))

#define GPOR_WORD(x)			(0x220 + ((x) * 4))
#define GPO_REG(val, id)		\
	(((val) >> (((id) % 4) * 8)) & GENMASK(7, 0))

#define ASF_INT_STATUS			0x300
#define ASF_INT_RAW_STATUS		0x304
#define ASF_INT_MASK			0x308
#define ASF_INT_TEST			0x30c
#define ASF_INT_FATAL_SELECT		0x310
#define ASF_INTEGRITY_ERR		BIT(6)
#define ASF_PROTOCOL_ERR		BIT(5)
#define ASF_TRANS_TIMEOUT_ERR		BIT(4)
#define ASF_CSR_ERR			BIT(3)
#define ASF_DAP_ERR			BIT(2)
#define ASF_SRAM_UNCORR_ERR		BIT(1)
#define ASF_SRAM_CORR_ERR		BIT(0)

#define ASF_SRAM_CORR_FAULT_STATUS	0x320
#define ASF_SRAM_UNCORR_FAULT_STATUS	0x324
#define ASF_SRAM_CORR_FAULT_INSTANCE(x)	((x) >> 24)
#define ASF_SRAM_CORR_FAULT_ADDR(x)	((x) & GENMASK(23, 0))

#define ASF_SRAM_FAULT_STATS		0x328
#define ASF_SRAM_FAULT_UNCORR_STATS(x)	((x) >> 16)
#define ASF_SRAM_FAULT_CORR_STATS(x)	((x) & GENMASK(15, 0))

#define ASF_TRANS_TOUT_CTRL		0x330
#define ASF_TRANS_TOUT_EN		BIT(31)
#define ASF_TRANS_TOUT_VAL(x)	(x)

#define ASF_TRANS_TOUT_FAULT_MASK	0x334
#define ASF_TRANS_TOUT_FAULT_STATUS	0x338
#define ASF_TRANS_TOUT_FAULT_APB	BIT(3)
#define ASF_TRANS_TOUT_FAULT_SCL_LOW	BIT(2)
#define ASF_TRANS_TOUT_FAULT_SCL_HIGH	BIT(1)
#define ASF_TRANS_TOUT_FAULT_FSCL_HIGH	BIT(0)

#define ASF_PROTO_FAULT_MASK		0x340
#define ASF_PROTO_FAULT_STATUS		0x344
#define ASF_PROTO_FAULT_SLVSDR_RD_ABORT	BIT(31)
#define ASF_PROTO_FAULT_SLVDDR_FAIL	BIT(30)
#define ASF_PROTO_FAULT_S(x)		BIT(16 + (x))
#define ASF_PROTO_FAULT_MSTSDR_RD_ABORT	BIT(15)
#define ASF_PROTO_FAULT_MSTDDR_FAIL	BIT(14)
#define ASF_PROTO_FAULT_M(x)		BIT(x)

struct cdns_i3c_master_caps {
	u32 cmdfifodepth;
	u32 cmdrfifodepth;
	u32 txfifodepth;
	u32 rxfifodepth;
	u32 ibirfifodepth;
};

struct cdns_i3c_cmd {
	u32 cmd0;
	u32 cmd1;
	u32 tx_len;
	const void *tx_buf;
	u32 rx_len;
	void *rx_buf;
	u32 error;
};

struct cdns_i3c_xfer {
	struct list_head node;
	struct completion comp;
	int ret;
	unsigned int ncmds;
	struct cdns_i3c_cmd cmds[] __counted_by(ncmds);
};

struct cdns_i3c_data {
	u8 thd_delay_ns;
};

struct cdns_i3c_master {
	struct work_struct hj_work;
	struct i3c_master_controller base;
	u32 free_rr_slots;
	unsigned int maxdevs;
	struct {
		unsigned int num_slots;
		struct i3c_dev_desc **slots;
		spinlock_t lock;
	} ibi;
	struct {
		struct list_head list;
		struct cdns_i3c_xfer *cur;
		spinlock_t lock;
	} xferqueue;
	void __iomem *regs;
	struct clk *sysclk;
	struct clk *pclk;
	struct cdns_i3c_master_caps caps;
	unsigned long i3c_scl_lim;
	const struct cdns_i3c_data *devdata;
};

static inline struct cdns_i3c_master *
to_cdns_i3c_master(struct i3c_master_controller *master)
{
	return container_of(master, struct cdns_i3c_master, base);
}

static void cdns_i3c_master_wr_to_tx_fifo(struct cdns_i3c_master *master,
					  const u8 *bytes, int nbytes)
{
	writesl(master->regs + TX_FIFO, bytes, nbytes / 4);
	if (nbytes & 3) {
		u32 tmp = 0;

		memcpy(&tmp, bytes + (nbytes & ~3), nbytes & 3);
		writesl(master->regs + TX_FIFO, &tmp, 1);
	}
}

static void cdns_i3c_master_rd_from_rx_fifo(struct cdns_i3c_master *master,
					    u8 *bytes, int nbytes)
{
	readsl(master->regs + RX_FIFO, bytes, nbytes / 4);
	if (nbytes & 3) {
		u32 tmp;

		readsl(master->regs + RX_FIFO, &tmp, 1);
		memcpy(bytes + (nbytes & ~3), &tmp, nbytes & 3);
	}
}

static bool cdns_i3c_master_supports_ccc_cmd(struct i3c_master_controller *m,
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
	case I3C_CCC_DEFSLVS:
	case I3C_CCC_ENTHDR(0):
	case I3C_CCC_SETDASA:
	case I3C_CCC_SETNEWDA:
	case I3C_CCC_GETMWL:
	case I3C_CCC_GETMRL:
	case I3C_CCC_GETPID:
	case I3C_CCC_GETBCR:
	case I3C_CCC_GETDCR:
	case I3C_CCC_GETSTATUS:
	case I3C_CCC_GETACCMST:
	case I3C_CCC_GETMXDS:
	case I3C_CCC_GETHDRCAP:
		return true;
	default:
		break;
	}

	return false;
}

static int cdns_i3c_master_disable(struct cdns_i3c_master *master)
{
	u32 status;

	writel(readl(master->regs + CTRL) & ~CTRL_DEV_EN, master->regs + CTRL);

	return readl_poll_timeout(master->regs + MST_STATUS0, status,
				  status & MST_STATUS0_IDLE, 10, 1000000);
}

static void cdns_i3c_master_enable(struct cdns_i3c_master *master)
{
	writel(readl(master->regs + CTRL) | CTRL_DEV_EN, master->regs + CTRL);
}

static struct cdns_i3c_xfer *
cdns_i3c_master_alloc_xfer(struct cdns_i3c_master *master, unsigned int ncmds)
{
	struct cdns_i3c_xfer *xfer;

	xfer = kzalloc(struct_size(xfer, cmds, ncmds), GFP_KERNEL);
	if (!xfer)
		return NULL;

	INIT_LIST_HEAD(&xfer->node);
	xfer->ncmds = ncmds;
	xfer->ret = -ETIMEDOUT;

	return xfer;
}

static void cdns_i3c_master_free_xfer(struct cdns_i3c_xfer *xfer)
{
	kfree(xfer);
}

static void cdns_i3c_master_start_xfer_locked(struct cdns_i3c_master *master)
{
	struct cdns_i3c_xfer *xfer = master->xferqueue.cur;
	unsigned int i;

	if (!xfer)
		return;

	writel(MST_INT_CMDD_EMP, master->regs + MST_ICR);
	for (i = 0; i < xfer->ncmds; i++) {
		struct cdns_i3c_cmd *cmd = &xfer->cmds[i];

		cdns_i3c_master_wr_to_tx_fifo(master, cmd->tx_buf,
					      cmd->tx_len);
	}

	for (i = 0; i < xfer->ncmds; i++) {
		struct cdns_i3c_cmd *cmd = &xfer->cmds[i];

		writel(cmd->cmd1 | CMD1_FIFO_CMDID(i),
		       master->regs + CMD1_FIFO);
		writel(cmd->cmd0, master->regs + CMD0_FIFO);
	}

	writel(readl(master->regs + CTRL) | CTRL_MCS,
	       master->regs + CTRL);
	writel(MST_INT_CMDD_EMP, master->regs + MST_IER);
}

static void cdns_i3c_master_end_xfer_locked(struct cdns_i3c_master *master,
					    u32 isr)
{
	struct cdns_i3c_xfer *xfer = master->xferqueue.cur;
	int i, ret = 0;
	u32 status0;

	if (!xfer)
		return;

	if (!(isr & MST_INT_CMDD_EMP))
		return;

	writel(MST_INT_CMDD_EMP, master->regs + MST_IDR);

	for (status0 = readl(master->regs + MST_STATUS0);
	     !(status0 & MST_STATUS0_CMDR_EMP);
	     status0 = readl(master->regs + MST_STATUS0)) {
		struct cdns_i3c_cmd *cmd;
		u32 cmdr, rx_len, id;

		cmdr = readl(master->regs + CMDR);
		id = CMDR_CMDID(cmdr);
		if (id == CMDR_CMDID_HJACK_DISEC ||
		    id == CMDR_CMDID_HJACK_ENTDAA ||
		    WARN_ON(id >= xfer->ncmds))
			continue;

		cmd = &xfer->cmds[CMDR_CMDID(cmdr)];
		rx_len = min_t(u32, CMDR_XFER_BYTES(cmdr), cmd->rx_len);
		cdns_i3c_master_rd_from_rx_fifo(master, cmd->rx_buf, rx_len);
		cmd->error = CMDR_ERROR(cmdr);
	}

	for (i = 0; i < xfer->ncmds; i++) {
		switch (xfer->cmds[i].error) {
		case CMDR_NO_ERROR:
			break;

		case CMDR_DDR_PREAMBLE_ERROR:
		case CMDR_DDR_PARITY_ERROR:
		case CMDR_M0_ERROR:
		case CMDR_M1_ERROR:
		case CMDR_M2_ERROR:
		case CMDR_MST_ABORT:
		case CMDR_NACK_RESP:
		case CMDR_DDR_DROPPED:
			ret = -EIO;
			break;

		case CMDR_DDR_RX_FIFO_OVF:
		case CMDR_DDR_TX_FIFO_UNF:
			ret = -ENOSPC;
			break;

		case CMDR_INVALID_DA:
		default:
			ret = -EINVAL;
			break;
		}
	}

	xfer->ret = ret;
	complete(&xfer->comp);

	xfer = list_first_entry_or_null(&master->xferqueue.list,
					struct cdns_i3c_xfer, node);
	if (xfer)
		list_del_init(&xfer->node);

	master->xferqueue.cur = xfer;
	cdns_i3c_master_start_xfer_locked(master);
}

static void cdns_i3c_master_queue_xfer(struct cdns_i3c_master *master,
				       struct cdns_i3c_xfer *xfer)
{
	unsigned long flags;

	init_completion(&xfer->comp);
	spin_lock_irqsave(&master->xferqueue.lock, flags);
	if (master->xferqueue.cur) {
		list_add_tail(&xfer->node, &master->xferqueue.list);
	} else {
		master->xferqueue.cur = xfer;
		cdns_i3c_master_start_xfer_locked(master);
	}
	spin_unlock_irqrestore(&master->xferqueue.lock, flags);
}

static void cdns_i3c_master_unqueue_xfer(struct cdns_i3c_master *master,
					 struct cdns_i3c_xfer *xfer)
{
	unsigned long flags;

	spin_lock_irqsave(&master->xferqueue.lock, flags);
	if (master->xferqueue.cur == xfer) {
		u32 status;

		writel(readl(master->regs + CTRL) & ~CTRL_DEV_EN,
		       master->regs + CTRL);
		readl_poll_timeout_atomic(master->regs + MST_STATUS0, status,
					  status & MST_STATUS0_IDLE, 10,
					  1000000);
		master->xferqueue.cur = NULL;
		writel(FLUSH_RX_FIFO | FLUSH_TX_FIFO | FLUSH_CMD_FIFO |
		       FLUSH_CMD_RESP,
		       master->regs + FLUSH_CTRL);
		writel(MST_INT_CMDD_EMP, master->regs + MST_IDR);
		writel(readl(master->regs + CTRL) | CTRL_DEV_EN,
		       master->regs + CTRL);
	} else {
		list_del_init(&xfer->node);
	}
	spin_unlock_irqrestore(&master->xferqueue.lock, flags);
}

static enum i3c_error_code cdns_i3c_cmd_get_err(struct cdns_i3c_cmd *cmd)
{
	switch (cmd->error) {
	case CMDR_M0_ERROR:
		return I3C_ERROR_M0;

	case CMDR_M1_ERROR:
		return I3C_ERROR_M1;

	case CMDR_M2_ERROR:
	case CMDR_NACK_RESP:
		return I3C_ERROR_M2;

	default:
		break;
	}

	return I3C_ERROR_UNKNOWN;
}

static int cdns_i3c_master_send_ccc_cmd(struct i3c_master_controller *m,
					struct i3c_ccc_cmd *cmd)
{
	struct cdns_i3c_master *master = to_cdns_i3c_master(m);
	struct cdns_i3c_xfer *xfer;
	struct cdns_i3c_cmd *ccmd;
	int ret;

	xfer = cdns_i3c_master_alloc_xfer(master, 1);
	if (!xfer)
		return -ENOMEM;

	ccmd = xfer->cmds;
	ccmd->cmd1 = CMD1_FIFO_CCC(cmd->id);
	ccmd->cmd0 = CMD0_FIFO_IS_CCC |
		     CMD0_FIFO_PL_LEN(cmd->dests[0].payload.len);

	if (cmd->id & I3C_CCC_DIRECT)
		ccmd->cmd0 |= CMD0_FIFO_DEV_ADDR(cmd->dests[0].addr);

	if (cmd->rnw) {
		ccmd->cmd0 |= CMD0_FIFO_RNW;
		ccmd->rx_buf = cmd->dests[0].payload.data;
		ccmd->rx_len = cmd->dests[0].payload.len;
	} else {
		ccmd->tx_buf = cmd->dests[0].payload.data;
		ccmd->tx_len = cmd->dests[0].payload.len;
	}

	cdns_i3c_master_queue_xfer(master, xfer);
	if (!wait_for_completion_timeout(&xfer->comp, msecs_to_jiffies(1000)))
		cdns_i3c_master_unqueue_xfer(master, xfer);

	ret = xfer->ret;
	cmd->err = cdns_i3c_cmd_get_err(&xfer->cmds[0]);
	cdns_i3c_master_free_xfer(xfer);

	return ret;
}

static int cdns_i3c_master_priv_xfers(struct i3c_dev_desc *dev,
				      struct i3c_priv_xfer *xfers,
				      int nxfers)
{
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct cdns_i3c_master *master = to_cdns_i3c_master(m);
	int txslots = 0, rxslots = 0, i, ret;
	struct cdns_i3c_xfer *cdns_xfer;

	for (i = 0; i < nxfers; i++) {
		if (xfers[i].len > CMD0_FIFO_PL_LEN_MAX)
			return -ENOTSUPP;
	}

	if (!nxfers)
		return 0;

	if (nxfers > master->caps.cmdfifodepth ||
	    nxfers > master->caps.cmdrfifodepth)
		return -ENOTSUPP;

	/*
	 * First make sure that all transactions (block of transfers separated
	 * by a STOP marker) fit in the FIFOs.
	 */
	for (i = 0; i < nxfers; i++) {
		if (xfers[i].rnw)
			rxslots += DIV_ROUND_UP(xfers[i].len, 4);
		else
			txslots += DIV_ROUND_UP(xfers[i].len, 4);
	}

	if (rxslots > master->caps.rxfifodepth ||
	    txslots > master->caps.txfifodepth)
		return -ENOTSUPP;

	cdns_xfer = cdns_i3c_master_alloc_xfer(master, nxfers);
	if (!cdns_xfer)
		return -ENOMEM;

	for (i = 0; i < nxfers; i++) {
		struct cdns_i3c_cmd *ccmd = &cdns_xfer->cmds[i];
		u32 pl_len = xfers[i].len;

		ccmd->cmd0 = CMD0_FIFO_DEV_ADDR(dev->info.dyn_addr) |
			CMD0_FIFO_PRIV_XMIT_MODE(XMIT_BURST_WITHOUT_SUBADDR);

		if (xfers[i].rnw) {
			ccmd->cmd0 |= CMD0_FIFO_RNW;
			ccmd->rx_buf = xfers[i].data.in;
			ccmd->rx_len = xfers[i].len;
			pl_len++;
		} else {
			ccmd->tx_buf = xfers[i].data.out;
			ccmd->tx_len = xfers[i].len;
		}

		ccmd->cmd0 |= CMD0_FIFO_PL_LEN(pl_len);

		if (i < nxfers - 1)
			ccmd->cmd0 |= CMD0_FIFO_RSBC;

		if (!i)
			ccmd->cmd0 |= CMD0_FIFO_BCH;
	}

	cdns_i3c_master_queue_xfer(master, cdns_xfer);
	if (!wait_for_completion_timeout(&cdns_xfer->comp,
					 msecs_to_jiffies(1000)))
		cdns_i3c_master_unqueue_xfer(master, cdns_xfer);

	ret = cdns_xfer->ret;

	for (i = 0; i < nxfers; i++)
		xfers[i].err = cdns_i3c_cmd_get_err(&cdns_xfer->cmds[i]);

	cdns_i3c_master_free_xfer(cdns_xfer);

	return ret;
}

static int cdns_i3c_master_i2c_xfers(struct i2c_dev_desc *dev,
				     const struct i2c_msg *xfers, int nxfers)
{
	struct i3c_master_controller *m = i2c_dev_get_master(dev);
	struct cdns_i3c_master *master = to_cdns_i3c_master(m);
	unsigned int nrxwords = 0, ntxwords = 0;
	struct cdns_i3c_xfer *xfer;
	int i, ret = 0;

	if (nxfers > master->caps.cmdfifodepth)
		return -ENOTSUPP;

	for (i = 0; i < nxfers; i++) {
		if (xfers[i].len > CMD0_FIFO_PL_LEN_MAX)
			return -ENOTSUPP;

		if (xfers[i].flags & I2C_M_RD)
			nrxwords += DIV_ROUND_UP(xfers[i].len, 4);
		else
			ntxwords += DIV_ROUND_UP(xfers[i].len, 4);
	}

	if (ntxwords > master->caps.txfifodepth ||
	    nrxwords > master->caps.rxfifodepth)
		return -ENOTSUPP;

	xfer = cdns_i3c_master_alloc_xfer(master, nxfers);
	if (!xfer)
		return -ENOMEM;

	for (i = 0; i < nxfers; i++) {
		struct cdns_i3c_cmd *ccmd = &xfer->cmds[i];

		ccmd->cmd0 = CMD0_FIFO_DEV_ADDR(xfers[i].addr) |
			CMD0_FIFO_PL_LEN(xfers[i].len) |
			CMD0_FIFO_PRIV_XMIT_MODE(XMIT_BURST_WITHOUT_SUBADDR);

		if (xfers[i].flags & I2C_M_TEN)
			ccmd->cmd0 |= CMD0_FIFO_IS_10B;

		if (xfers[i].flags & I2C_M_RD) {
			ccmd->cmd0 |= CMD0_FIFO_RNW;
			ccmd->rx_buf = xfers[i].buf;
			ccmd->rx_len = xfers[i].len;
		} else {
			ccmd->tx_buf = xfers[i].buf;
			ccmd->tx_len = xfers[i].len;
		}
	}

	cdns_i3c_master_queue_xfer(master, xfer);
	if (!wait_for_completion_timeout(&xfer->comp, msecs_to_jiffies(1000)))
		cdns_i3c_master_unqueue_xfer(master, xfer);

	ret = xfer->ret;
	cdns_i3c_master_free_xfer(xfer);

	return ret;
}

struct cdns_i3c_i2c_dev_data {
	u16 id;
	s16 ibi;
	struct i3c_generic_ibi_pool *ibi_pool;
};

static u32 prepare_rr0_dev_address(u32 addr)
{
	u32 ret = (addr << 1) & 0xff;

	/* RR0[7:1] = addr[6:0] */
	ret |= (addr & GENMASK(6, 0)) << 1;

	/* RR0[15:13] = addr[9:7] */
	ret |= (addr & GENMASK(9, 7)) << 6;

	/* RR0[0] = ~XOR(addr[6:0]) */
	if (!(hweight8(addr & 0x7f) & 1))
		ret |= 1;

	return ret;
}

static void cdns_i3c_master_upd_i3c_addr(struct i3c_dev_desc *dev)
{
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct cdns_i3c_master *master = to_cdns_i3c_master(m);
	struct cdns_i3c_i2c_dev_data *data = i3c_dev_get_master_data(dev);
	u32 rr;

	rr = prepare_rr0_dev_address(dev->info.dyn_addr ?
				     dev->info.dyn_addr :
				     dev->info.static_addr);
	writel(DEV_ID_RR0_IS_I3C | rr, master->regs + DEV_ID_RR0(data->id));
}

static int cdns_i3c_master_get_rr_slot(struct cdns_i3c_master *master,
				       u8 dyn_addr)
{
	unsigned long activedevs;
	u32 rr;
	int i;

	if (!dyn_addr) {
		if (!master->free_rr_slots)
			return -ENOSPC;

		return ffs(master->free_rr_slots) - 1;
	}

	activedevs = readl(master->regs + DEVS_CTRL) & DEVS_CTRL_DEVS_ACTIVE_MASK;
	activedevs &= ~BIT(0);

	for_each_set_bit(i, &activedevs, master->maxdevs + 1) {
		rr = readl(master->regs + DEV_ID_RR0(i));
		if (!(rr & DEV_ID_RR0_IS_I3C) ||
		    DEV_ID_RR0_GET_DEV_ADDR(rr) != dyn_addr)
			continue;

		return i;
	}

	return -EINVAL;
}

static int cdns_i3c_master_reattach_i3c_dev(struct i3c_dev_desc *dev,
					    u8 old_dyn_addr)
{
	cdns_i3c_master_upd_i3c_addr(dev);

	return 0;
}

static int cdns_i3c_master_attach_i3c_dev(struct i3c_dev_desc *dev)
{
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct cdns_i3c_master *master = to_cdns_i3c_master(m);
	struct cdns_i3c_i2c_dev_data *data;
	int slot;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	slot = cdns_i3c_master_get_rr_slot(master, dev->info.dyn_addr);
	if (slot < 0) {
		kfree(data);
		return slot;
	}

	data->ibi = -1;
	data->id = slot;
	i3c_dev_set_master_data(dev, data);
	master->free_rr_slots &= ~BIT(slot);

	if (!dev->info.dyn_addr) {
		cdns_i3c_master_upd_i3c_addr(dev);
		writel(readl(master->regs + DEVS_CTRL) |
		       DEVS_CTRL_DEV_ACTIVE(data->id),
		       master->regs + DEVS_CTRL);
	}

	return 0;
}

static void cdns_i3c_master_detach_i3c_dev(struct i3c_dev_desc *dev)
{
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct cdns_i3c_master *master = to_cdns_i3c_master(m);
	struct cdns_i3c_i2c_dev_data *data = i3c_dev_get_master_data(dev);

	writel(readl(master->regs + DEVS_CTRL) |
	       DEVS_CTRL_DEV_CLR(data->id),
	       master->regs + DEVS_CTRL);

	i3c_dev_set_master_data(dev, NULL);
	master->free_rr_slots |= BIT(data->id);
	kfree(data);
}

static int cdns_i3c_master_attach_i2c_dev(struct i2c_dev_desc *dev)
{
	struct i3c_master_controller *m = i2c_dev_get_master(dev);
	struct cdns_i3c_master *master = to_cdns_i3c_master(m);
	struct cdns_i3c_i2c_dev_data *data;
	int slot;

	slot = cdns_i3c_master_get_rr_slot(master, 0);
	if (slot < 0)
		return slot;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->id = slot;
	master->free_rr_slots &= ~BIT(slot);
	i2c_dev_set_master_data(dev, data);

	writel(prepare_rr0_dev_address(dev->addr),
	       master->regs + DEV_ID_RR0(data->id));
	writel(dev->lvr, master->regs + DEV_ID_RR2(data->id));
	writel(readl(master->regs + DEVS_CTRL) |
	       DEVS_CTRL_DEV_ACTIVE(data->id),
	       master->regs + DEVS_CTRL);

	return 0;
}

static void cdns_i3c_master_detach_i2c_dev(struct i2c_dev_desc *dev)
{
	struct i3c_master_controller *m = i2c_dev_get_master(dev);
	struct cdns_i3c_master *master = to_cdns_i3c_master(m);
	struct cdns_i3c_i2c_dev_data *data = i2c_dev_get_master_data(dev);

	writel(readl(master->regs + DEVS_CTRL) |
	       DEVS_CTRL_DEV_CLR(data->id),
	       master->regs + DEVS_CTRL);
	master->free_rr_slots |= BIT(data->id);

	i2c_dev_set_master_data(dev, NULL);
	kfree(data);
}

static void cdns_i3c_master_bus_cleanup(struct i3c_master_controller *m)
{
	struct cdns_i3c_master *master = to_cdns_i3c_master(m);

	cdns_i3c_master_disable(master);
}

static void cdns_i3c_master_dev_rr_to_info(struct cdns_i3c_master *master,
					   unsigned int slot,
					   struct i3c_device_info *info)
{
	u32 rr;

	memset(info, 0, sizeof(*info));
	rr = readl(master->regs + DEV_ID_RR0(slot));
	info->dyn_addr = DEV_ID_RR0_GET_DEV_ADDR(rr);
	rr = readl(master->regs + DEV_ID_RR2(slot));
	info->dcr = rr;
	info->bcr = rr >> 8;
	info->pid = rr >> 16;
	info->pid |= (u64)readl(master->regs + DEV_ID_RR1(slot)) << 16;
}

static void cdns_i3c_master_upd_i3c_scl_lim(struct cdns_i3c_master *master)
{
	struct i3c_master_controller *m = &master->base;
	unsigned long i3c_lim_period, pres_step, ncycles;
	struct i3c_bus *bus = i3c_master_get_bus(m);
	unsigned long new_i3c_scl_lim = 0;
	struct i3c_dev_desc *dev;
	u32 prescl1, ctrl;

	i3c_bus_for_each_i3cdev(bus, dev) {
		unsigned long max_fscl;

		max_fscl = max(I3C_CCC_MAX_SDR_FSCL(dev->info.max_read_ds),
			       I3C_CCC_MAX_SDR_FSCL(dev->info.max_write_ds));
		switch (max_fscl) {
		case I3C_SDR1_FSCL_8MHZ:
			max_fscl = 8000000;
			break;
		case I3C_SDR2_FSCL_6MHZ:
			max_fscl = 6000000;
			break;
		case I3C_SDR3_FSCL_4MHZ:
			max_fscl = 4000000;
			break;
		case I3C_SDR4_FSCL_2MHZ:
			max_fscl = 2000000;
			break;
		case I3C_SDR0_FSCL_MAX:
		default:
			max_fscl = 0;
			break;
		}

		if (max_fscl &&
		    (new_i3c_scl_lim > max_fscl || !new_i3c_scl_lim))
			new_i3c_scl_lim = max_fscl;
	}

	/* Only update PRESCL_CTRL1 if the I3C SCL limitation has changed. */
	if (new_i3c_scl_lim == master->i3c_scl_lim)
		return;
	master->i3c_scl_lim = new_i3c_scl_lim;
	if (!new_i3c_scl_lim)
		return;
	pres_step = 1000000000UL / (bus->scl_rate.i3c * 4);

	/* Configure PP_LOW to meet I3C slave limitations. */
	prescl1 = readl(master->regs + PRESCL_CTRL1) &
		  ~PRESCL_CTRL1_PP_LOW_MASK;
	ctrl = readl(master->regs + CTRL);

	i3c_lim_period = DIV_ROUND_UP(1000000000, master->i3c_scl_lim);
	ncycles = DIV_ROUND_UP(i3c_lim_period, pres_step);
	if (ncycles < 4)
		ncycles = 0;
	else
		ncycles -= 4;

	prescl1 |= PRESCL_CTRL1_PP_LOW(ncycles);

	/* Disable I3C master before updating PRESCL_CTRL1. */
	if (ctrl & CTRL_DEV_EN)
		cdns_i3c_master_disable(master);

	writel(prescl1, master->regs + PRESCL_CTRL1);

	if (ctrl & CTRL_DEV_EN)
		cdns_i3c_master_enable(master);
}

static int cdns_i3c_master_do_daa(struct i3c_master_controller *m)
{
	struct cdns_i3c_master *master = to_cdns_i3c_master(m);
	unsigned long olddevs, newdevs;
	int ret, slot;
	u8 addrs[MAX_DEVS] = { };
	u8 last_addr = 0;

	olddevs = readl(master->regs + DEVS_CTRL) & DEVS_CTRL_DEVS_ACTIVE_MASK;
	olddevs |= BIT(0);

	/* Prepare RR slots before launching DAA. */
	for_each_clear_bit(slot, &olddevs, master->maxdevs + 1) {
		ret = i3c_master_get_free_addr(m, last_addr + 1);
		if (ret < 0)
			return -ENOSPC;

		last_addr = ret;
		addrs[slot] = last_addr;
		writel(prepare_rr0_dev_address(last_addr) | DEV_ID_RR0_IS_I3C,
		       master->regs + DEV_ID_RR0(slot));
		writel(0, master->regs + DEV_ID_RR1(slot));
		writel(0, master->regs + DEV_ID_RR2(slot));
	}

	ret = i3c_master_entdaa_locked(&master->base);
	if (ret && ret != I3C_ERROR_M2)
		return ret;

	newdevs = readl(master->regs + DEVS_CTRL) & DEVS_CTRL_DEVS_ACTIVE_MASK;
	newdevs &= ~olddevs;

	/*
	 * Clear all retaining registers filled during DAA. We already
	 * have the addressed assigned to them in the addrs array.
	 */
	for_each_set_bit(slot, &newdevs, master->maxdevs + 1)
		i3c_master_add_i3c_dev_locked(m, addrs[slot]);

	/*
	 * Clear slots that ended up not being used. Can be caused by I3C
	 * device creation failure or when the I3C device was already known
	 * by the system but with a different address (in this case the device
	 * already has a slot and does not need a new one).
	 */
	writel(readl(master->regs + DEVS_CTRL) |
	       master->free_rr_slots << DEVS_CTRL_DEV_CLR_SHIFT,
	       master->regs + DEVS_CTRL);

	i3c_master_defslvs_locked(&master->base);

	cdns_i3c_master_upd_i3c_scl_lim(master);

	/* Unmask Hot-Join and Mastership request interrupts. */
	i3c_master_enec_locked(m, I3C_BROADCAST_ADDR,
			       I3C_CCC_EVENT_HJ | I3C_CCC_EVENT_MR);

	return 0;
}

static u8 cdns_i3c_master_calculate_thd_delay(struct cdns_i3c_master *master)
{
	unsigned long sysclk_rate = clk_get_rate(master->sysclk);
	u8 thd_delay = DIV_ROUND_UP(master->devdata->thd_delay_ns,
				    (NSEC_PER_SEC / sysclk_rate));

	/* Every value greater than 3 is not valid. */
	if (thd_delay > THD_DELAY_MAX)
		thd_delay = THD_DELAY_MAX;

	/* CTLR_THD_DEL value is encoded. */
	return (THD_DELAY_MAX - thd_delay);
}

static int cdns_i3c_master_bus_init(struct i3c_master_controller *m)
{
	struct cdns_i3c_master *master = to_cdns_i3c_master(m);
	unsigned long pres_step, sysclk_rate, max_i2cfreq;
	struct i3c_bus *bus = i3c_master_get_bus(m);
	u32 ctrl, prescl0, prescl1, pres, low;
	struct i3c_device_info info = { };
	int ret, ncycles;

	switch (bus->mode) {
	case I3C_BUS_MODE_PURE:
		ctrl = CTRL_PURE_BUS_MODE;
		break;

	case I3C_BUS_MODE_MIXED_FAST:
		ctrl = CTRL_MIXED_FAST_BUS_MODE;
		break;

	case I3C_BUS_MODE_MIXED_SLOW:
		ctrl = CTRL_MIXED_SLOW_BUS_MODE;
		break;

	default:
		return -EINVAL;
	}

	sysclk_rate = clk_get_rate(master->sysclk);
	if (!sysclk_rate)
		return -EINVAL;

	pres = DIV_ROUND_UP(sysclk_rate, (bus->scl_rate.i3c * 4)) - 1;
	if (pres > PRESCL_CTRL0_I3C_MAX)
		return -ERANGE;

	bus->scl_rate.i3c = sysclk_rate / ((pres + 1) * 4);

	prescl0 = PRESCL_CTRL0_I3C(pres);

	low = ((I3C_BUS_TLOW_OD_MIN_NS * sysclk_rate) / (pres + 1)) - 2;
	prescl1 = PRESCL_CTRL1_OD_LOW(low);

	max_i2cfreq = bus->scl_rate.i2c;

	pres = (sysclk_rate / (max_i2cfreq * 5)) - 1;
	if (pres > PRESCL_CTRL0_I2C_MAX)
		return -ERANGE;

	bus->scl_rate.i2c = sysclk_rate / ((pres + 1) * 5);

	prescl0 |= PRESCL_CTRL0_I2C(pres);
	writel(prescl0, master->regs + PRESCL_CTRL0);

	/* Calculate OD and PP low. */
	pres_step = 1000000000 / (bus->scl_rate.i3c * 4);
	ncycles = DIV_ROUND_UP(I3C_BUS_TLOW_OD_MIN_NS, pres_step) - 2;
	if (ncycles < 0)
		ncycles = 0;
	prescl1 = PRESCL_CTRL1_OD_LOW(ncycles);
	writel(prescl1, master->regs + PRESCL_CTRL1);

	/* Get an address for the master. */
	ret = i3c_master_get_free_addr(m, 0);
	if (ret < 0)
		return ret;

	writel(prepare_rr0_dev_address(ret) | DEV_ID_RR0_IS_I3C,
	       master->regs + DEV_ID_RR0(0));

	cdns_i3c_master_dev_rr_to_info(master, 0, &info);
	if (info.bcr & I3C_BCR_HDR_CAP)
		info.hdr_cap = I3C_CCC_HDR_MODE(I3C_HDR_DDR);

	ret = i3c_master_set_info(&master->base, &info);
	if (ret)
		return ret;

	/*
	 * Enable Hot-Join, and, when a Hot-Join request happens, disable all
	 * events coming from this device.
	 *
	 * We will issue ENTDAA afterwards from the threaded IRQ handler.
	 */
	ctrl |= CTRL_HJ_ACK | CTRL_HJ_DISEC | CTRL_HALT_EN | CTRL_MCS_EN;

	/*
	 * Configure data hold delay based on device-specific data.
	 *
	 * MIPI I3C Specification 1.0 defines non-zero minimal tHD_PP timing on
	 * master output. This setting allows to meet this timing on master's
	 * SoC outputs, regardless of PCB balancing.
	 */
	ctrl |= CTRL_THD_DELAY(cdns_i3c_master_calculate_thd_delay(master));
	writel(ctrl, master->regs + CTRL);

	cdns_i3c_master_enable(master);

	return 0;
}

static void cdns_i3c_master_handle_ibi(struct cdns_i3c_master *master,
				       u32 ibir)
{
	struct cdns_i3c_i2c_dev_data *data;
	bool data_consumed = false;
	struct i3c_ibi_slot *slot;
	u32 id = IBIR_SLVID(ibir);
	struct i3c_dev_desc *dev;
	size_t nbytes;
	u8 *buf;

	/*
	 * FIXME: maybe we should report the FIFO OVF errors to the upper
	 * layer.
	 */
	if (id >= master->ibi.num_slots || (ibir & IBIR_ERROR))
		goto out;

	dev = master->ibi.slots[id];
	spin_lock(&master->ibi.lock);

	data = i3c_dev_get_master_data(dev);
	slot = i3c_generic_ibi_get_free_slot(data->ibi_pool);
	if (!slot)
		goto out_unlock;

	buf = slot->data;

	nbytes = IBIR_XFER_BYTES(ibir);
	readsl(master->regs + IBI_DATA_FIFO, buf, nbytes / 4);
	if (nbytes % 3) {
		u32 tmp = __raw_readl(master->regs + IBI_DATA_FIFO);

		memcpy(buf + (nbytes & ~3), &tmp, nbytes & 3);
	}

	slot->len = min_t(unsigned int, IBIR_XFER_BYTES(ibir),
			  dev->ibi->max_payload_len);
	i3c_master_queue_ibi(dev, slot);
	data_consumed = true;

out_unlock:
	spin_unlock(&master->ibi.lock);

out:
	/* Consume data from the FIFO if it's not been done already. */
	if (!data_consumed) {
		int i;

		for (i = 0; i < IBIR_XFER_BYTES(ibir); i += 4)
			readl(master->regs + IBI_DATA_FIFO);
	}
}

static void cnds_i3c_master_demux_ibis(struct cdns_i3c_master *master)
{
	u32 status0;

	writel(MST_INT_IBIR_THR, master->regs + MST_ICR);

	for (status0 = readl(master->regs + MST_STATUS0);
	     !(status0 & MST_STATUS0_IBIR_EMP);
	     status0 = readl(master->regs + MST_STATUS0)) {
		u32 ibir = readl(master->regs + IBIR);

		switch (IBIR_TYPE(ibir)) {
		case IBIR_TYPE_IBI:
			cdns_i3c_master_handle_ibi(master, ibir);
			break;

		case IBIR_TYPE_HJ:
			WARN_ON(IBIR_XFER_BYTES(ibir) || (ibir & IBIR_ERROR));
			queue_work(master->base.wq, &master->hj_work);
			break;

		case IBIR_TYPE_MR:
			WARN_ON(IBIR_XFER_BYTES(ibir) || (ibir & IBIR_ERROR));
			break;

		default:
			break;
		}
	}
}

static irqreturn_t cdns_i3c_master_interrupt(int irq, void *data)
{
	struct cdns_i3c_master *master = data;
	u32 status;

	status = readl(master->regs + MST_ISR);
	if (!(status & readl(master->regs + MST_IMR)))
		return IRQ_NONE;

	spin_lock(&master->xferqueue.lock);
	cdns_i3c_master_end_xfer_locked(master, status);
	spin_unlock(&master->xferqueue.lock);

	if (status & MST_INT_IBIR_THR)
		cnds_i3c_master_demux_ibis(master);

	return IRQ_HANDLED;
}

static int cdns_i3c_master_disable_ibi(struct i3c_dev_desc *dev)
{
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct cdns_i3c_master *master = to_cdns_i3c_master(m);
	struct cdns_i3c_i2c_dev_data *data = i3c_dev_get_master_data(dev);
	unsigned long flags;
	u32 sirmap;
	int ret;

	ret = i3c_master_disec_locked(m, dev->info.dyn_addr,
				      I3C_CCC_EVENT_SIR);
	if (ret)
		return ret;

	spin_lock_irqsave(&master->ibi.lock, flags);
	sirmap = readl(master->regs + SIR_MAP_DEV_REG(data->ibi));
	sirmap &= ~SIR_MAP_DEV_CONF_MASK(data->ibi);
	sirmap |= SIR_MAP_DEV_CONF(data->ibi,
				   SIR_MAP_DEV_DA(I3C_BROADCAST_ADDR));
	writel(sirmap, master->regs + SIR_MAP_DEV_REG(data->ibi));
	spin_unlock_irqrestore(&master->ibi.lock, flags);

	return ret;
}

static int cdns_i3c_master_enable_ibi(struct i3c_dev_desc *dev)
{
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct cdns_i3c_master *master = to_cdns_i3c_master(m);
	struct cdns_i3c_i2c_dev_data *data = i3c_dev_get_master_data(dev);
	unsigned long flags;
	u32 sircfg, sirmap;
	int ret;

	spin_lock_irqsave(&master->ibi.lock, flags);
	sirmap = readl(master->regs + SIR_MAP_DEV_REG(data->ibi));
	sirmap &= ~SIR_MAP_DEV_CONF_MASK(data->ibi);
	sircfg = SIR_MAP_DEV_ROLE(dev->info.bcr >> 6) |
		 SIR_MAP_DEV_DA(dev->info.dyn_addr) |
		 SIR_MAP_DEV_PL(dev->info.max_ibi_len) |
		 SIR_MAP_DEV_ACK;

	if (dev->info.bcr & I3C_BCR_MAX_DATA_SPEED_LIM)
		sircfg |= SIR_MAP_DEV_SLOW;

	sirmap |= SIR_MAP_DEV_CONF(data->ibi, sircfg);
	writel(sirmap, master->regs + SIR_MAP_DEV_REG(data->ibi));
	spin_unlock_irqrestore(&master->ibi.lock, flags);

	ret = i3c_master_enec_locked(m, dev->info.dyn_addr,
				     I3C_CCC_EVENT_SIR);
	if (ret) {
		spin_lock_irqsave(&master->ibi.lock, flags);
		sirmap = readl(master->regs + SIR_MAP_DEV_REG(data->ibi));
		sirmap &= ~SIR_MAP_DEV_CONF_MASK(data->ibi);
		sirmap |= SIR_MAP_DEV_CONF(data->ibi,
					   SIR_MAP_DEV_DA(I3C_BROADCAST_ADDR));
		writel(sirmap, master->regs + SIR_MAP_DEV_REG(data->ibi));
		spin_unlock_irqrestore(&master->ibi.lock, flags);
	}

	return ret;
}

static int cdns_i3c_master_request_ibi(struct i3c_dev_desc *dev,
				       const struct i3c_ibi_setup *req)
{
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct cdns_i3c_master *master = to_cdns_i3c_master(m);
	struct cdns_i3c_i2c_dev_data *data = i3c_dev_get_master_data(dev);
	unsigned long flags;
	unsigned int i;

	data->ibi_pool = i3c_generic_ibi_alloc_pool(dev, req);
	if (IS_ERR(data->ibi_pool))
		return PTR_ERR(data->ibi_pool);

	spin_lock_irqsave(&master->ibi.lock, flags);
	for (i = 0; i < master->ibi.num_slots; i++) {
		if (!master->ibi.slots[i]) {
			data->ibi = i;
			master->ibi.slots[i] = dev;
			break;
		}
	}
	spin_unlock_irqrestore(&master->ibi.lock, flags);

	if (i < master->ibi.num_slots)
		return 0;

	i3c_generic_ibi_free_pool(data->ibi_pool);
	data->ibi_pool = NULL;

	return -ENOSPC;
}

static void cdns_i3c_master_free_ibi(struct i3c_dev_desc *dev)
{
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct cdns_i3c_master *master = to_cdns_i3c_master(m);
	struct cdns_i3c_i2c_dev_data *data = i3c_dev_get_master_data(dev);
	unsigned long flags;

	spin_lock_irqsave(&master->ibi.lock, flags);
	master->ibi.slots[data->ibi] = NULL;
	data->ibi = -1;
	spin_unlock_irqrestore(&master->ibi.lock, flags);

	i3c_generic_ibi_free_pool(data->ibi_pool);
}

static void cdns_i3c_master_recycle_ibi_slot(struct i3c_dev_desc *dev,
					     struct i3c_ibi_slot *slot)
{
	struct cdns_i3c_i2c_dev_data *data = i3c_dev_get_master_data(dev);

	i3c_generic_ibi_recycle_slot(data->ibi_pool, slot);
}

static const struct i3c_master_controller_ops cdns_i3c_master_ops = {
	.bus_init = cdns_i3c_master_bus_init,
	.bus_cleanup = cdns_i3c_master_bus_cleanup,
	.do_daa = cdns_i3c_master_do_daa,
	.attach_i3c_dev = cdns_i3c_master_attach_i3c_dev,
	.reattach_i3c_dev = cdns_i3c_master_reattach_i3c_dev,
	.detach_i3c_dev = cdns_i3c_master_detach_i3c_dev,
	.attach_i2c_dev = cdns_i3c_master_attach_i2c_dev,
	.detach_i2c_dev = cdns_i3c_master_detach_i2c_dev,
	.supports_ccc_cmd = cdns_i3c_master_supports_ccc_cmd,
	.send_ccc_cmd = cdns_i3c_master_send_ccc_cmd,
	.priv_xfers = cdns_i3c_master_priv_xfers,
	.i2c_xfers = cdns_i3c_master_i2c_xfers,
	.enable_ibi = cdns_i3c_master_enable_ibi,
	.disable_ibi = cdns_i3c_master_disable_ibi,
	.request_ibi = cdns_i3c_master_request_ibi,
	.free_ibi = cdns_i3c_master_free_ibi,
	.recycle_ibi_slot = cdns_i3c_master_recycle_ibi_slot,
};

static void cdns_i3c_master_hj(struct work_struct *work)
{
	struct cdns_i3c_master *master = container_of(work,
						      struct cdns_i3c_master,
						      hj_work);

	i3c_master_do_daa(&master->base);
}

static struct cdns_i3c_data cdns_i3c_devdata = {
	.thd_delay_ns = 10,
};

static const struct of_device_id cdns_i3c_master_of_ids[] = {
	{ .compatible = "cdns,i3c-master", .data = &cdns_i3c_devdata },
	{ /* sentinel */ },
};

static int cdns_i3c_master_probe(struct platform_device *pdev)
{
	struct cdns_i3c_master *master;
	int ret, irq;
	u32 val;

	master = devm_kzalloc(&pdev->dev, sizeof(*master), GFP_KERNEL);
	if (!master)
		return -ENOMEM;

	master->devdata = of_device_get_match_data(&pdev->dev);
	if (!master->devdata)
		return -EINVAL;

	master->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(master->regs))
		return PTR_ERR(master->regs);

	master->pclk = devm_clk_get(&pdev->dev, "pclk");
	if (IS_ERR(master->pclk))
		return PTR_ERR(master->pclk);

	master->sysclk = devm_clk_get(&pdev->dev, "sysclk");
	if (IS_ERR(master->sysclk))
		return PTR_ERR(master->sysclk);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	ret = clk_prepare_enable(master->pclk);
	if (ret)
		return ret;

	ret = clk_prepare_enable(master->sysclk);
	if (ret)
		goto err_disable_pclk;

	if (readl(master->regs + DEV_ID) != DEV_ID_I3C_MASTER) {
		ret = -EINVAL;
		goto err_disable_sysclk;
	}

	spin_lock_init(&master->xferqueue.lock);
	INIT_LIST_HEAD(&master->xferqueue.list);

	INIT_WORK(&master->hj_work, cdns_i3c_master_hj);
	writel(0xffffffff, master->regs + MST_IDR);
	writel(0xffffffff, master->regs + SLV_IDR);
	ret = devm_request_irq(&pdev->dev, irq, cdns_i3c_master_interrupt, 0,
			       dev_name(&pdev->dev), master);
	if (ret)
		goto err_disable_sysclk;

	platform_set_drvdata(pdev, master);

	val = readl(master->regs + CONF_STATUS0);

	/* Device ID0 is reserved to describe this master. */
	master->maxdevs = CONF_STATUS0_DEVS_NUM(val);
	master->free_rr_slots = GENMASK(master->maxdevs, 1);
	master->caps.ibirfifodepth = CONF_STATUS0_IBIR_DEPTH(val);
	master->caps.cmdrfifodepth = CONF_STATUS0_CMDR_DEPTH(val);

	val = readl(master->regs + CONF_STATUS1);
	master->caps.cmdfifodepth = CONF_STATUS1_CMD_DEPTH(val);
	master->caps.rxfifodepth = CONF_STATUS1_RX_DEPTH(val);
	master->caps.txfifodepth = CONF_STATUS1_TX_DEPTH(val);

	spin_lock_init(&master->ibi.lock);
	master->ibi.num_slots = CONF_STATUS1_IBI_HW_RES(val);
	master->ibi.slots = devm_kcalloc(&pdev->dev, master->ibi.num_slots,
					 sizeof(*master->ibi.slots),
					 GFP_KERNEL);
	if (!master->ibi.slots) {
		ret = -ENOMEM;
		goto err_disable_sysclk;
	}

	writel(IBIR_THR(1), master->regs + CMD_IBI_THR_CTRL);
	writel(MST_INT_IBIR_THR, master->regs + MST_IER);
	writel(DEVS_CTRL_DEV_CLR_ALL, master->regs + DEVS_CTRL);

	ret = i3c_master_register(&master->base, &pdev->dev,
				  &cdns_i3c_master_ops, false);
	if (ret)
		goto err_disable_sysclk;

	return 0;

err_disable_sysclk:
	clk_disable_unprepare(master->sysclk);

err_disable_pclk:
	clk_disable_unprepare(master->pclk);

	return ret;
}

static void cdns_i3c_master_remove(struct platform_device *pdev)
{
	struct cdns_i3c_master *master = platform_get_drvdata(pdev);

	i3c_master_unregister(&master->base);

	clk_disable_unprepare(master->sysclk);
	clk_disable_unprepare(master->pclk);
}

static struct platform_driver cdns_i3c_master = {
	.probe = cdns_i3c_master_probe,
	.remove_new = cdns_i3c_master_remove,
	.driver = {
		.name = "cdns-i3c-master",
		.of_match_table = cdns_i3c_master_of_ids,
	},
};
module_platform_driver(cdns_i3c_master);

MODULE_AUTHOR("Boris Brezillon <boris.brezillon@bootlin.com>");
MODULE_DESCRIPTION("Cadence I3C master driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:cdns-i3c-master");
