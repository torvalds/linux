/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2015 - 2016 Thomas Körper, esd electronic system design gmbh
 * Copyright (C) 2017 - 2023 Stefan Mätje, esd electronics gmbh
 */

#include <linux/bits.h>
#include <linux/can/dev.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/units.h>

#define ACC_TS_FREQ_80MHZ (80 * HZ_PER_MHZ)
#define ACC_I2C_ADDON_DETECT_DELAY_MS 10

/* esdACC Overview Module */
#define ACC_OV_OF_PROBE	0x0000
#define ACC_OV_OF_VERSION 0x0004
#define ACC_OV_OF_INFO 0x0008
#define ACC_OV_OF_CANCORE_FREQ 0x000c
#define ACC_OV_OF_TS_FREQ_LO 0x0010
#define ACC_OV_OF_TS_FREQ_HI 0x0014
#define ACC_OV_OF_IRQ_STATUS_CORES 0x0018
#define ACC_OV_OF_TS_CURR_LO 0x001c
#define ACC_OV_OF_TS_CURR_HI 0x0020
#define ACC_OV_OF_IRQ_STATUS 0x0028
#define ACC_OV_OF_MODE 0x002c
#define ACC_OV_OF_BM_IRQ_COUNTER 0x0070
#define ACC_OV_OF_BM_IRQ_MASK 0x0074
#define ACC_OV_OF_MSI_DATA 0x0080
#define ACC_OV_OF_MSI_ADDRESSOFFSET 0x0084

/* Feature flags are contained in the upper 16 bit of the version
 * register at ACC_OV_OF_VERSION but only used with these masks after
 * extraction into an extra variable => (xx - 16).
 */
#define ACC_OV_REG_FEAT_MASK_CANFD BIT(27 - 16)
#define ACC_OV_REG_FEAT_MASK_NEW_PSC BIT(28 - 16)
#define ACC_OV_REG_FEAT_MASK_DAR BIT(30 - 16)

#define ACC_OV_REG_MODE_MASK_ENDIAN_LITTLE BIT(0)
#define ACC_OV_REG_MODE_MASK_BM_ENABLE BIT(1)
#define ACC_OV_REG_MODE_MASK_MODE_LED BIT(2)
#define ACC_OV_REG_MODE_MASK_TIMER_ENABLE BIT(4)
#define ACC_OV_REG_MODE_MASK_TIMER_ONE_SHOT BIT(5)
#define ACC_OV_REG_MODE_MASK_TIMER_ABSOLUTE BIT(6)
#define ACC_OV_REG_MODE_MASK_TIMER GENMASK(6, 4)
#define ACC_OV_REG_MODE_MASK_TS_SRC GENMASK(8, 7)
#define ACC_OV_REG_MODE_MASK_I2C_ENABLE BIT(11)
#define ACC_OV_REG_MODE_MASK_MSI_ENABLE BIT(14)
#define ACC_OV_REG_MODE_MASK_NEW_PSC_ENABLE BIT(15)
#define ACC_OV_REG_MODE_MASK_FPGA_RESET BIT(31)

/* esdACC CAN Core Module */
#define ACC_CORE_OF_CTRL 0x0000
#define ACC_CORE_OF_STATUS_IRQ 0x0008
#define ACC_CORE_OF_BRP	0x000c
#define ACC_CORE_OF_BTR	0x0010
#define ACC_CORE_OF_FBTR 0x0014
#define ACC_CORE_OF_STATUS 0x0030
#define ACC_CORE_OF_TXFIFO_CONFIG 0x0048
#define ACC_CORE_OF_TXFIFO_STATUS 0x004c
#define ACC_CORE_OF_TX_STATUS_IRQ 0x0050
#define ACC_CORE_OF_TX_ABORT_MASK 0x0054
#define ACC_CORE_OF_BM_IRQ_COUNTER 0x0070
#define ACC_CORE_OF_TXFIFO_ID 0x00c0
#define ACC_CORE_OF_TXFIFO_DLC 0x00c4
#define ACC_CORE_OF_TXFIFO_DATA_0 0x00c8
#define ACC_CORE_OF_TXFIFO_DATA_1 0x00cc

/* CTRL register layout */
#define ACC_REG_CTRL_MASK_RESETMODE BIT(0)
#define ACC_REG_CTRL_MASK_LOM BIT(1)
#define ACC_REG_CTRL_MASK_STM BIT(2)
#define ACC_REG_CTRL_MASK_TRANSEN BIT(5)
#define ACC_REG_CTRL_MASK_TS BIT(6)
#define ACC_REG_CTRL_MASK_SCHEDULE BIT(7)

#define ACC_REG_CTRL_MASK_IE_RXTX BIT(8)
#define ACC_REG_CTRL_MASK_IE_TXERROR BIT(9)
#define ACC_REG_CTRL_MASK_IE_ERRWARN BIT(10)
#define ACC_REG_CTRL_MASK_IE_OVERRUN BIT(11)
#define ACC_REG_CTRL_MASK_IE_TSI BIT(12)
#define ACC_REG_CTRL_MASK_IE_ERRPASS BIT(13)
#define ACC_REG_CTRL_MASK_IE_ALI BIT(14)
#define ACC_REG_CTRL_MASK_IE_BUSERR BIT(15)

/* BRP and BTR register layout for CAN-Classic version */
#define ACC_REG_BRP_CL_MASK_BRP GENMASK(8, 0)
#define ACC_REG_BTR_CL_MASK_TSEG1 GENMASK(3, 0)
#define ACC_REG_BTR_CL_MASK_TSEG2 GENMASK(18, 16)
#define ACC_REG_BTR_CL_MASK_SJW GENMASK(25, 24)

/* BRP and BTR register layout for CAN-FD version */
#define ACC_REG_BRP_FD_MASK_BRP GENMASK(7, 0)
#define ACC_REG_BTR_FD_MASK_TSEG1 GENMASK(7, 0)
#define ACC_REG_BTR_FD_MASK_TSEG2 GENMASK(22, 16)
#define ACC_REG_BTR_FD_MASK_SJW GENMASK(30, 24)

/* 256 BM_MSGs of 32 byte size */
#define ACC_CORE_DMAMSG_SIZE 32U
#define ACC_CORE_DMABUF_SIZE (256U * ACC_CORE_DMAMSG_SIZE)

enum acc_bmmsg_id {
	BM_MSG_ID_RXTXDONE = 0x01,
	BM_MSG_ID_TXABORT = 0x02,
	BM_MSG_ID_OVERRUN = 0x03,
	BM_MSG_ID_BUSERR = 0x04,
	BM_MSG_ID_ERRPASSIVE = 0x05,
	BM_MSG_ID_ERRWARN = 0x06,
	BM_MSG_ID_TIMESLICE = 0x07,
	BM_MSG_ID_HWTIMER = 0x08,
	BM_MSG_ID_HOTPLUG = 0x09,
};

/* The struct acc_bmmsg_* structure declarations that follow here provide
 * access to the ring buffer of bus master messages maintained by the FPGA
 * bus master engine. All bus master messages have the same size of
 * ACC_CORE_DMAMSG_SIZE and a minimum alignment of ACC_CORE_DMAMSG_SIZE in
 * memory.
 *
 * All structure members are natural aligned. Therefore we should not need
 * a __packed attribute. All struct acc_bmmsg_* declarations have at least
 * reserved* members to fill the structure to the full ACC_CORE_DMAMSG_SIZE.
 *
 * A failure of this property due padding will be detected at compile time
 * by static_assert(sizeof(union acc_bmmsg) == ACC_CORE_DMAMSG_SIZE).
 */

struct acc_bmmsg_rxtxdone {
	u8 msg_id;
	u8 txfifo_level;
	u8 reserved1[2];
	u8 txtsfifo_level;
	u8 reserved2[3];
	u32 id;
	struct {
		u8 len;
		u8 txdfifo_idx;
		u8 zeroes8;
		u8 reserved;
	} acc_dlc;
	u8 data[CAN_MAX_DLEN];
	/* Time stamps in struct acc_ov::timestamp_frequency ticks. */
	u64 ts;
};

struct acc_bmmsg_txabort {
	u8 msg_id;
	u8 txfifo_level;
	u16 abort_mask;
	u8 txtsfifo_level;
	u8 reserved2[1];
	u16 abort_mask_txts;
	u64 ts;
	u32 reserved3[4];
};

struct acc_bmmsg_overrun {
	u8 msg_id;
	u8 txfifo_level;
	u8 lost_cnt;
	u8 reserved1;
	u8 txtsfifo_level;
	u8 reserved2[3];
	u64 ts;
	u32 reserved3[4];
};

struct acc_bmmsg_buserr {
	u8 msg_id;
	u8 txfifo_level;
	u8 ecc;
	u8 reserved1;
	u8 txtsfifo_level;
	u8 reserved2[3];
	u64 ts;
	u32 reg_status;
	u32 reg_btr;
	u32 reserved3[2];
};

struct acc_bmmsg_errstatechange {
	u8 msg_id;
	u8 txfifo_level;
	u8 reserved1[2];
	u8 txtsfifo_level;
	u8 reserved2[3];
	u64 ts;
	u32 reg_status;
	u32 reserved3[3];
};

struct acc_bmmsg_timeslice {
	u8 msg_id;
	u8 txfifo_level;
	u8 reserved1[2];
	u8 txtsfifo_level;
	u8 reserved2[3];
	u64 ts;
	u32 reserved3[4];
};

struct acc_bmmsg_hwtimer {
	u8 msg_id;
	u8 reserved1[3];
	u32 reserved2[1];
	u64 timer;
	u32 reserved3[4];
};

struct acc_bmmsg_hotplug {
	u8 msg_id;
	u8 reserved1[3];
	u32 reserved2[7];
};

union acc_bmmsg {
	u8 msg_id;
	struct acc_bmmsg_rxtxdone rxtxdone;
	struct acc_bmmsg_txabort txabort;
	struct acc_bmmsg_overrun overrun;
	struct acc_bmmsg_buserr buserr;
	struct acc_bmmsg_errstatechange errstatechange;
	struct acc_bmmsg_timeslice timeslice;
	struct acc_bmmsg_hwtimer hwtimer;
};

/* Check size of union acc_bmmsg to be of expected size. */
static_assert(sizeof(union acc_bmmsg) == ACC_CORE_DMAMSG_SIZE);

struct acc_bmfifo {
	const union acc_bmmsg *messages;
	/* irq_cnt points to an u32 value where the esdACC FPGA deposits
	 * the bm_fifo head index in coherent DMA memory. Only bits 7..0
	 * are valid. Use READ_ONCE() to access this memory location.
	 */
	const u32 *irq_cnt;
	u32 local_irq_cnt;
	u32 msg_fifo_tail;
};

struct acc_core {
	void __iomem *addr;
	struct net_device *netdev;
	struct acc_bmfifo bmfifo;
	u8 tx_fifo_size;
	u8 tx_fifo_head;
	u8 tx_fifo_tail;
};

struct acc_ov {
	void __iomem *addr;
	struct acc_bmfifo bmfifo;
	u32 timestamp_frequency;
	u32 core_frequency;
	u16 version;
	u16 features;
	u8 total_cores;
	u8 active_cores;
};

struct acc_net_priv {
	struct can_priv can; /* must be the first member! */
	struct acc_core *core;
	struct acc_ov *ov;
};

static inline u32 acc_read32(struct acc_core *core, unsigned short offs)
{
	return ioread32be(core->addr + offs);
}

static inline void acc_write32(struct acc_core *core,
			       unsigned short offs, u32 v)
{
	iowrite32be(v, core->addr + offs);
}

static inline void acc_write32_noswap(struct acc_core *core,
				      unsigned short offs, u32 v)
{
	iowrite32(v, core->addr + offs);
}

static inline void acc_set_bits(struct acc_core *core,
				unsigned short offs, u32 mask)
{
	u32 v = acc_read32(core, offs);

	v |= mask;
	acc_write32(core, offs, v);
}

static inline void acc_clear_bits(struct acc_core *core,
				  unsigned short offs, u32 mask)
{
	u32 v = acc_read32(core, offs);

	v &= ~mask;
	acc_write32(core, offs, v);
}

static inline int acc_resetmode_entered(struct acc_core *core)
{
	u32 ctrl = acc_read32(core, ACC_CORE_OF_CTRL);

	return (ctrl & ACC_REG_CTRL_MASK_RESETMODE) != 0;
}

static inline u32 acc_ov_read32(struct acc_ov *ov, unsigned short offs)
{
	return ioread32be(ov->addr + offs);
}

static inline void acc_ov_write32(struct acc_ov *ov,
				  unsigned short offs, u32 v)
{
	iowrite32be(v, ov->addr + offs);
}

static inline void acc_ov_set_bits(struct acc_ov *ov,
				   unsigned short offs, u32 b)
{
	u32 v = acc_ov_read32(ov, offs);

	v |= b;
	acc_ov_write32(ov, offs, v);
}

static inline void acc_ov_clear_bits(struct acc_ov *ov,
				     unsigned short offs, u32 b)
{
	u32 v = acc_ov_read32(ov, offs);

	v &= ~b;
	acc_ov_write32(ov, offs, v);
}

static inline void acc_reset_fpga(struct acc_ov *ov)
{
	acc_ov_write32(ov, ACC_OV_OF_MODE, ACC_OV_REG_MODE_MASK_FPGA_RESET);

	/* (Re-)start and wait for completion of addon detection on the I^2C bus */
	acc_ov_set_bits(ov, ACC_OV_OF_MODE, ACC_OV_REG_MODE_MASK_I2C_ENABLE);
	mdelay(ACC_I2C_ADDON_DETECT_DELAY_MS);
}

void acc_init_ov(struct acc_ov *ov, struct device *dev);
void acc_init_bm_ptr(struct acc_ov *ov, struct acc_core *cores,
		     const void *mem);
int acc_open(struct net_device *netdev);
int acc_close(struct net_device *netdev);
netdev_tx_t acc_start_xmit(struct sk_buff *skb, struct net_device *netdev);
int acc_get_berr_counter(const struct net_device *netdev,
			 struct can_berr_counter *bec);
int acc_set_mode(struct net_device *netdev, enum can_mode mode);
int acc_set_bittiming(struct net_device *netdev);
irqreturn_t acc_card_interrupt(struct acc_ov *ov, struct acc_core *cores);
