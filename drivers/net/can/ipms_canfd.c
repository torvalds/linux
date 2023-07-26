// SPDX-License-Identifier: GPL-2.0
/*
 * StarFive Controller Area Network Host Controller Driver
 *
 * Copyright (c) 2022 StarFive Technology Co., Ltd.
 */

#include <linux/clk.h>
#include <linux/reset.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/skbuff.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/can/dev.h>
#include <linux/can/error.h>
#include <linux/can/led.h>
#include <linux/pm_runtime.h>
#include <linux/of_device.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>

#define DRIVER_NAME "ipms_canfd"

/* CAN registers set */
enum canfd_device_reg {
	CANFD_RUBF_OFFSET           =   0x00,	/* Receive Buffer Registers 0x00-0x4f */
	CANFD_RUBF_ID_OFFSET        =   0x00,
	CANFD_RBUF_CTL_OFFSET       =   0x04,
	CANFD_RBUF_DATA_OFFSET      =   0x08,
	CANFD_TBUF_OFFSET           =   0x50,	/* Transmit Buffer Registers 0x50-0x97 */
	CANFD_TBUF_ID_OFFSET        =   0x50,
	CANFD_TBUF_CTL_OFFSET       =   0x54,
	CANFD_TBUF_DATA_OFFSET      =   0x58,
	CANFD_TTS_OFFSET            =   0x98,	/* Transmission Time Stamp 0x98-0x9f */
	CANFD_CFG_STAT_OFFSET       =   0xa0,
	CANFD_TCMD_OFFSET           =   0xa1,
	CANFD_TCTRL_OFFSET          =   0xa2,
	CANFD_RCTRL_OFFSET          =   0xa3,
	CANFD_RTIE_OFFSET           =   0xa4,
	CANFD_RTIF_OFFSET           =   0xa5,
	CANFD_ERRINT_OFFSET         =   0xa6,
	CANFD_LIMIT_OFFSET          =   0xa7,
	CANFD_S_SEG_1_OFFSET        =   0xa8,
	CANFD_S_SEG_2_OFFSET        =   0xa9,
	CANFD_S_SJW_OFFSET          =   0xaa,
	CANFD_S_PRESC_OFFSET        =   0xab,
	CANFD_F_SEG_1_OFFSET        =   0xac,
	CANFD_F_SEG_2_OFFSET        =   0xad,
	CANFD_F_SJW_OFFSET          =   0xae,
	CANFD_F_PRESC_OFFSET        =   0xaf,
	CANFD_EALCAP_OFFSET         =   0xb0,
	CANFD_RECNT_OFFSET          =   0xb2,
	CANFD_TECNT_OFFSET          =   0xb3,
};

enum canfd_reg_bitchange {
	CAN_FD_SET_RST_MASK         =   0x80,	/* Set Reset Bit */
	CAN_FD_OFF_RST_MASK         =   0x7f,	/* Reset Off Bit */
	CAN_FD_SET_FULLCAN_MASK     =   0x10,	/* set TTTBM as 1->full TTCAN mode */
	CAN_FD_OFF_FULLCAN_MASK     =   0xef,	/* set TTTBM as 0->separate PTB and STB mode */
	CAN_FD_SET_FIFO_MASK        =   0x20,	/* set TSMODE as 1->FIFO mode */
	CAN_FD_OFF_FIFO_MASK        =   0xdf,	/* set TSMODE as 0->Priority mode */
	CAN_FD_SET_TSONE_MASK       =   0x04,
	CAN_FD_OFF_TSONE_MASK       =   0xfb,
	CAN_FD_SET_TSALL_MASK       =   0x02,
	CAN_FD_OFF_TSALL_MASK       =   0xfd,
	CAN_FD_LBMEMOD_MASK         =   0x40,	/* set loop back mode, external */
	CAN_FD_LBMIMOD_MASK         =   0x20,	/* set loopback internal mode */
	CAN_FD_SET_BUSOFF_MASK      =   0x01,
	CAN_FD_OFF_BUSOFF_MASK      =   0xfe,
	CAN_FD_SET_TTSEN_MASK       =   0x80,	/* set ttsen, tts update enable */
	CAN_FD_SET_BRS_MASK         =   0x10,	/* can fd Bit Rate Switch mask */
	CAN_FD_OFF_BRS_MASK         =   0xef,
	CAN_FD_SET_EDL_MASK         =   0x20,	/* Extended Data Length */
	CAN_FD_OFF_EDL_MASK         =   0xdf,
	CAN_FD_SET_DLC_MASK         =   0x0f,
	CAN_FD_SET_TENEXT_MASK      =   0x40,
	CAN_FD_SET_IDE_MASK         =   0x80,
	CAN_FD_OFF_IDE_MASK         =   0x7f,
	CAN_FD_SET_RTR_MASK         =   0x40,
	CAN_FD_OFF_RTR_MASK         =   0xbf,
	CAN_FD_INTR_ALL_MASK        =   0xff,	/* all interrupts enable mask */
	CAN_FD_SET_RIE_MASK         =   0x80,
	CAN_FD_OFF_RIE_MASK         =   0x7f,
	CAN_FD_SET_RFIE_MASK        =   0x20,
	CAN_FD_OFF_RFIE_MASK        =   0xdf,
	CAN_FD_SET_RAFIE_MASK       =   0x10,
	CAN_FD_OFF_RAFIE_MASK       =   0xef,
	CAN_FD_SET_EIE_MASK         =   0x02,
	CAN_FD_OFF_EIE_MASK         =   0xfd,
	CAN_FD_TASCTIVE_MASK        =   0x02,
	CAN_FD_RASCTIVE_MASK        =   0x04,
	CAN_FD_SET_TBSEL_MASK       =   0x80,	/* message writen in STB */
	CAN_FD_OFF_TBSEL_MASK       =   0x7f,	/* message writen in PTB */
	CAN_FD_SET_STBY_MASK        =   0x20,
	CAN_FD_OFF_STBY_MASK        =   0xdf,
	CAN_FD_SET_TPE_MASK         =   0x10,	/* Transmit primary enable */
	CAN_FD_SET_TPA_MASK         =   0x08,
	CAN_FD_SET_SACK_MASK        =   0x80,
	CAN_FD_SET_RREL_MASK        =   0x10,
	CAN_FD_RSTAT_NOT_EMPTY_MASK =   0x03,
	CAN_FD_SET_RIF_MASK         =   0x80,
	CAN_FD_OFF_RIF_MASK         =   0x7f,
	CAN_FD_SET_RAFIF_MASK       =   0x10,
	CAN_FD_SET_RFIF_MASK        =   0x20,
	CAN_FD_SET_TPIF_MASK        =   0x08,	/* Transmission Primary Interrupt Flag */
	CAN_FD_SET_TSIF_MASK        =   0x04,
	CAN_FD_SET_EIF_MASK         =   0x02,
	CAN_FD_SET_AIF_MASK         =   0x01,
	CAN_FD_SET_EWARN_MASK       =   0x80,
	CAN_FD_SET_EPASS_MASK       =   0x40,
	CAN_FD_SET_EPIE_MASK        =   0x20,
	CAN_FD_SET_EPIF_MASK        =   0x10,
	CAN_FD_SET_ALIE_MASK        =   0x08,
	CAN_FD_SET_ALIF_MASK        =   0x04,
	CAN_FD_SET_BEIE_MASK        =   0x02,
	CAN_FD_SET_BEIF_MASK        =   0x01,
	CAN_FD_OFF_EPIE_MASK        =   0xdf,
	CAN_FD_OFF_BEIE_MASK        =   0xfd,
	CAN_FD_SET_AFWL_MASK        =   0x40,
	CAN_FD_SET_EWL_MASK         =   0x0b,
	CAN_FD_SET_KOER_MASK        =   0xe0,
	CAN_FD_SET_BIT_ERROR_MASK   =   0x20,
	CAN_FD_SET_FORM_ERROR_MASK  =   0x40,
	CAN_FD_SET_STUFF_ERROR_MASK =   0x60,
	CAN_FD_SET_ACK_ERROR_MASK   =   0x80,
	CAN_FD_SET_CRC_ERROR_MASK   =   0xa0,
	CAN_FD_SET_OTH_ERROR_MASK   =   0xc0,
};

/* seg1,seg2,sjw,prescaler all have 8 bits */
#define BITS_OF_BITTIMING_REG		8

/* in can_bittiming strucure every field has 32 bits---->u32 */
#define FBITS_IN_BITTIMING_STR		32
#define SEG_1_SHIFT			0
#define SEG_2_SHIFT			8
#define SJW_SHIFT			16
#define PRESC_SHIFT			24

/* TTSEN bit used for 32 bit register read or write */
#define TTSEN_8_32_SHIFT		24
#define RTR_32_8_SHIFT			24

/* transmit mode */
#define XMIT_FULL			0
#define XMIT_SEP_FIFO			1
#define XMIT_SEP_PRIO			2
#define XMIT_PTB_MODE			3

enum  IPMS_CAN_TYPE {
	IPMS_CAN_TYPY_CAN 	= 0,
	IPMS_CAN_TYPE_CANFD,
};

struct ipms_canfd_priv {
	struct can_priv can;
	struct napi_struct napi;
	struct device *dev;
	struct regmap *reg_syscon;
	void __iomem *reg_base;
	u32 (*read_reg)(const struct ipms_canfd_priv *priv, enum canfd_device_reg reg);
	void (*write_reg)(const struct ipms_canfd_priv *priv, enum canfd_device_reg reg, u32 val);
	struct clk *can_clk;
	u32 tx_mode;
	struct reset_control *resets;
	struct clk_bulk_data *clks;
	int nr_clks;
	u32 can_or_canfd;
};

static struct can_bittiming_const canfd_bittiming_const = {
	.name = DRIVER_NAME,
	.tseg1_min = 2,
	.tseg1_max = 16,
	.tseg2_min = 2,
	.tseg2_max = 8,
	.sjw_max = 4,
	.brp_min = 1,
	.brp_max = 512,
	.brp_inc = 1,

};

static struct can_bittiming_const canfd_data_bittiming_const = {
	.name = DRIVER_NAME,
	.tseg1_min = 1,
	.tseg1_max = 16,
	.tseg2_min = 2,
	.tseg2_max = 8,
	.sjw_max = 8,
	.brp_min = 1,
	.brp_max = 512,
	.brp_inc = 1,
};

static void canfd_write_reg_le(const struct ipms_canfd_priv *priv,
				enum canfd_device_reg reg, u32 val)
{
	iowrite32(val, priv->reg_base + reg);
}

static u32 canfd_read_reg_le(const struct ipms_canfd_priv *priv,
				enum canfd_device_reg reg)
{
	return ioread32(priv->reg_base + reg);
}

static inline unsigned char can_ioread8(const void  *addr)
{
	void  *addr_down;
	union val {
		u8 val_8[4];
		u32 val_32;
	} val;
	u32 offset = 0;

	addr_down = (void  *)ALIGN_DOWN((unsigned long)addr, 4);
	offset = addr - addr_down;
	val.val_32 = ioread32(addr_down);
	return val.val_8[offset];
}

static inline void can_iowrite8(unsigned char value, void  *addr)
{
	void  *addr_down;
	union val {
		u8 val_8[4];
		u32 val_32;
	} val;
	u8 offset = 0;

	addr_down = (void *)ALIGN_DOWN((unsigned long)addr, 4);
	offset = addr - addr_down;
	val.val_32 = ioread32(addr_down);
	val.val_8[offset] = value;
	iowrite32(val.val_32, addr_down);
}

static void canfd_reigister_set_bit(const struct ipms_canfd_priv *priv,
					enum canfd_device_reg reg,
					enum canfd_reg_bitchange set_mask)
{
	void  *addr_down;
	union val {
		u8 val_8[4];
		u32 val_32;
	} val;
	u8 offset = 0;

	addr_down = (void *)ALIGN_DOWN((unsigned long)(priv->reg_base + reg), 4);
	offset = (priv->reg_base + reg) - addr_down;
	val.val_32 = ioread32(addr_down);
	val.val_8[offset] |= set_mask;
	iowrite32(val.val_32, addr_down);
}

static void canfd_reigister_off_bit(const struct ipms_canfd_priv *priv,
					enum canfd_device_reg reg,
					enum canfd_reg_bitchange set_mask)
{
	void  *addr_down;
	union val {
		u8 val_8[4];
		u32 val_32;
	} val;
	u8 offset = 0;

	addr_down = (void *)ALIGN_DOWN((unsigned long)(priv->reg_base + reg), 4);
	offset = (priv->reg_base + reg) - addr_down;
	val.val_32 = ioread32(addr_down);
	val.val_8[offset] &= set_mask;
	iowrite32(val.val_32, addr_down);
}

static int canfd_device_driver_bittime_configuration(struct net_device *ndev)
{
	struct ipms_canfd_priv *priv = netdev_priv(ndev);
	struct can_bittiming *bt = &priv->can.bittiming;
	struct can_bittiming *dbt = &priv->can.data_bittiming;
	u32 reset_test, bittiming_temp, dat_bittiming;

	reset_test = can_ioread8(priv->reg_base + CANFD_CFG_STAT_OFFSET);

	if (!(reset_test & CAN_FD_SET_RST_MASK)) {
		netdev_alert(ndev, "Not in reset mode, cannot set bit timing\n");
		return -EPERM;
	}

	bittiming_temp = ((bt->phase_seg1 + bt->prop_seg + 1 - 2) << SEG_1_SHIFT) |
			 ((bt->phase_seg2 - 1) << SEG_2_SHIFT) |
			 ((bt->sjw - 1) << SJW_SHIFT) |
			 ((bt->brp - 1) << PRESC_SHIFT);

	/* Check the bittime parameter */
	if ((((int)(bt->phase_seg1 + bt->prop_seg + 1) - 2) < 0) ||
		(((int)(bt->phase_seg2) - 1) < 0) ||
		(((int)(bt->sjw) - 1) < 0) ||
		(((int)(bt->brp) - 1) < 0))
		return -EINVAL;

	priv->write_reg(priv, CANFD_S_SEG_1_OFFSET, bittiming_temp);

	if (priv->can_or_canfd == IPMS_CAN_TYPE_CANFD) {
		dat_bittiming = ((dbt->phase_seg1 + dbt->prop_seg + 1 - 2) << SEG_1_SHIFT) |
				((dbt->phase_seg2 - 1) << SEG_2_SHIFT) |
				((dbt->sjw - 1) << SJW_SHIFT) |
				((dbt->brp - 1) << PRESC_SHIFT);

		if ((((int)(dbt->phase_seg1 + dbt->prop_seg + 1) - 2) < 0) ||
			(((int)(dbt->phase_seg2) - 1) < 0) ||
			(((int)(dbt->sjw) - 1) < 0) ||
			(((int)(dbt->brp) - 1) < 0))
			return -EINVAL;

		priv->write_reg(priv, CANFD_F_SEG_1_OFFSET, dat_bittiming);
	}

	canfd_reigister_off_bit(priv, CANFD_CFG_STAT_OFFSET, CAN_FD_OFF_RST_MASK);

	netdev_dbg(ndev, "Slow bit rate: %08x\n", priv->read_reg(priv, CANFD_S_SEG_1_OFFSET));
	netdev_dbg(ndev, "Fast bit rate: %08x\n", priv->read_reg(priv, CANFD_F_SEG_1_OFFSET));

	return 0;
}

int canfd_get_freebuffer(struct ipms_canfd_priv *priv)
{
	/* Get next transmit buffer */
	canfd_reigister_set_bit(priv, CANFD_TCTRL_OFFSET, CAN_FD_SET_TENEXT_MASK);

	if (can_ioread8(priv->reg_base + CANFD_TCTRL_OFFSET) & CAN_FD_SET_TENEXT_MASK)
		return -1;

	return 0;
}

static void canfd_tx_interrupt(struct net_device *ndev, u8 isr)
{
	struct ipms_canfd_priv *priv = netdev_priv(ndev);

	/* wait till transmission of the PTB or STB finished */
	while (isr & (CAN_FD_SET_TPIF_MASK | CAN_FD_SET_TSIF_MASK)) {
		if (isr & CAN_FD_SET_TPIF_MASK)
			canfd_reigister_set_bit(priv, CANFD_RTIF_OFFSET, CAN_FD_SET_TPIF_MASK);

		if (isr & CAN_FD_SET_TSIF_MASK)
			canfd_reigister_set_bit(priv, CANFD_RTIF_OFFSET, CAN_FD_SET_TSIF_MASK);

		isr = can_ioread8(priv->reg_base + CANFD_RTIF_OFFSET);
	}
	netif_wake_queue(ndev);
}

static int can_rx(struct net_device *ndev)
{
	struct ipms_canfd_priv *priv = netdev_priv(ndev);
	struct net_device_stats *stats = &ndev->stats;
	struct can_frame *cf;
	struct sk_buff *skb;
	u32 can_id;
	u8  dlc, control, rx_status;

	rx_status = can_ioread8(priv->reg_base + CANFD_RCTRL_OFFSET);

	if (!(rx_status & CAN_FD_RSTAT_NOT_EMPTY_MASK))
		return 0;
	control = can_ioread8(priv->reg_base + CANFD_RBUF_CTL_OFFSET);
	can_id = priv->read_reg(priv, CANFD_RUBF_ID_OFFSET);
	dlc = can_ioread8(priv->reg_base + CANFD_RBUF_CTL_OFFSET) & CAN_FD_SET_DLC_MASK;

	skb = alloc_can_skb(ndev, (struct can_frame **)&cf);
	if (!skb) {
		stats->rx_dropped++;
		return 0;
	}
	cf->can_dlc = can_cc_dlc2len(dlc);

	/* change the CANFD id into socketcan id format */
	if (control & CAN_FD_SET_IDE_MASK) {
		cf->can_id = can_id;
		cf->can_id |= CAN_EFF_FLAG;
	} else {
		cf->can_id = can_id;
		cf->can_id &= (~CAN_EFF_FLAG);
	}

	if (control & CAN_FD_SET_RTR_MASK)
		cf->can_id |= CAN_RTR_FLAG;

	if (!(control & CAN_FD_SET_RTR_MASK)) {
		*((u32 *)(cf->data + 0)) = priv->read_reg(priv, CANFD_RBUF_DATA_OFFSET);
		*((u32 *)(cf->data + 4)) = priv->read_reg(priv, CANFD_RBUF_DATA_OFFSET + 4);
	}

	canfd_reigister_set_bit(priv, CANFD_RCTRL_OFFSET, CAN_FD_SET_RREL_MASK);
	stats->rx_bytes += can_fd_dlc2len(cf->can_dlc);
	stats->rx_packets++;
	netif_receive_skb(skb);

	return 1;
}

static int canfd_rx(struct net_device *ndev)
{
	struct ipms_canfd_priv *priv = netdev_priv(ndev);
	struct net_device_stats *stats = &ndev->stats;
	struct canfd_frame *cf;
	struct sk_buff *skb;
	u32 can_id;
	u8  dlc, control, rx_status;
	int i;

	rx_status = can_ioread8(priv->reg_base + CANFD_RCTRL_OFFSET);

	if (!(rx_status & CAN_FD_RSTAT_NOT_EMPTY_MASK))
		return 0;
	control = can_ioread8(priv->reg_base + CANFD_RBUF_CTL_OFFSET);
	can_id = priv->read_reg(priv, CANFD_RUBF_ID_OFFSET);
	dlc = can_ioread8(priv->reg_base + CANFD_RBUF_CTL_OFFSET) & CAN_FD_SET_DLC_MASK;

	if (control & CAN_FD_SET_EDL_MASK)
		/* allocate sk_buffer for canfd frame */
		skb = alloc_canfd_skb(ndev, &cf);
	else
		/* allocate sk_buffer for can frame */
		skb = alloc_can_skb(ndev, (struct can_frame **)&cf);

	if (!skb) {
		stats->rx_dropped++;
		return 0;
	}

	/* change the CANFD or CAN2.0 data into socketcan data format */
	if (control & CAN_FD_SET_EDL_MASK)
		cf->len = can_fd_dlc2len(dlc);
	else
		cf->len = can_cc_dlc2len(dlc);

	/* change the CANFD id into socketcan id format */
	if (control & CAN_FD_SET_EDL_MASK) {
		cf->can_id = can_id;
		if (control & CAN_FD_SET_IDE_MASK)
			cf->can_id |= CAN_EFF_FLAG;
		else
			cf->can_id &= (~CAN_EFF_FLAG);
	} else {
		cf->can_id = can_id;
		if (control & CAN_FD_SET_IDE_MASK)
			cf->can_id |= CAN_EFF_FLAG;
		else
			cf->can_id &= (~CAN_EFF_FLAG);

		if (control & CAN_FD_SET_RTR_MASK)
			cf->can_id |= CAN_RTR_FLAG;
	}

	/* CANFD frames handed over to SKB */
	if (control & CAN_FD_SET_EDL_MASK) {
		for (i = 0; i < cf->len; i += 4)
			*((u32 *)(cf->data + i)) = priv->read_reg(priv, CANFD_RBUF_DATA_OFFSET + i);
	} else {
		/* skb reads the received datas, if the RTR bit not set */
		if (!(control & CAN_FD_SET_RTR_MASK)) {
			*((u32 *)(cf->data + 0)) = priv->read_reg(priv, CANFD_RBUF_DATA_OFFSET);
			*((u32 *)(cf->data + 4)) = priv->read_reg(priv, CANFD_RBUF_DATA_OFFSET + 4);
		}
	}

	canfd_reigister_set_bit(priv, CANFD_RCTRL_OFFSET, CAN_FD_SET_RREL_MASK);

	stats->rx_bytes += cf->len;
	stats->rx_packets++;
	netif_receive_skb(skb);

	return 1;
}

static int canfd_rx_poll(struct napi_struct *napi, int quota)
{
	struct net_device *ndev = napi->dev;
	struct ipms_canfd_priv *priv = netdev_priv(ndev);
	int work_done = 0;
	u8 rx_status = 0, control = 0;

	control = can_ioread8(priv->reg_base + CANFD_RBUF_CTL_OFFSET);
	rx_status = can_ioread8(priv->reg_base + CANFD_RCTRL_OFFSET);

	/* clear receive interrupt and deal with all the received frames */
	while ((rx_status & CAN_FD_RSTAT_NOT_EMPTY_MASK) && (work_done < quota)) {
		(control & CAN_FD_SET_EDL_MASK) ? (work_done += canfd_rx(ndev)) : (work_done += can_rx(ndev));

		control = can_ioread8(priv->reg_base + CANFD_RBUF_CTL_OFFSET);
		rx_status = can_ioread8(priv->reg_base + CANFD_RCTRL_OFFSET);
	}
	napi_complete(napi);
	canfd_reigister_set_bit(priv, CANFD_RTIE_OFFSET, CAN_FD_SET_RIE_MASK);
	return work_done;
}

static void canfd_rxfull_interrupt(struct net_device *ndev, u8 isr)
{
	struct ipms_canfd_priv *priv = netdev_priv(ndev);

	if (isr & CAN_FD_SET_RAFIF_MASK)
		canfd_reigister_set_bit(priv, CANFD_RTIF_OFFSET, CAN_FD_SET_RAFIF_MASK);

	if (isr & (CAN_FD_SET_RAFIF_MASK | CAN_FD_SET_RFIF_MASK))
		canfd_reigister_set_bit(priv, CANFD_RTIF_OFFSET,
					(CAN_FD_SET_RAFIF_MASK | CAN_FD_SET_RFIF_MASK));
}

static int set_canfd_xmit_mode(struct net_device *ndev)
{
	struct ipms_canfd_priv *priv = netdev_priv(ndev);

	switch (priv->tx_mode) {
	case XMIT_FULL:
		canfd_reigister_set_bit(priv, CANFD_TCTRL_OFFSET, CAN_FD_SET_FULLCAN_MASK);
		break;
	case XMIT_SEP_FIFO:
		canfd_reigister_off_bit(priv, CANFD_TCTRL_OFFSET, CAN_FD_OFF_FULLCAN_MASK);
		canfd_reigister_set_bit(priv, CANFD_TCTRL_OFFSET, CAN_FD_SET_FIFO_MASK);
		canfd_reigister_off_bit(priv, CANFD_TCMD_OFFSET, CAN_FD_SET_TBSEL_MASK);
		break;
	case XMIT_SEP_PRIO:
		canfd_reigister_off_bit(priv, CANFD_TCTRL_OFFSET, CAN_FD_OFF_FULLCAN_MASK);
		canfd_reigister_off_bit(priv, CANFD_TCTRL_OFFSET, CAN_FD_OFF_FIFO_MASK);
		canfd_reigister_off_bit(priv, CANFD_TCMD_OFFSET, CAN_FD_SET_TBSEL_MASK);
		break;
	case XMIT_PTB_MODE:
		canfd_reigister_off_bit(priv, CANFD_TCMD_OFFSET, CAN_FD_OFF_TBSEL_MASK);
		break;
	default:
		break;
	}
	return 0;
}

static netdev_tx_t canfd_driver_start_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	struct ipms_canfd_priv *priv = netdev_priv(ndev);
	struct canfd_frame *cf = (struct canfd_frame *)skb->data;
	struct net_device_stats *stats = &ndev->stats;
	u32 ttsen, id, ctl, addr_off;
	int i;

	priv->tx_mode = XMIT_PTB_MODE;

	if (can_dropped_invalid_skb(ndev, skb))
		return NETDEV_TX_OK;

	netif_stop_queue(ndev);

	switch (priv->tx_mode) {
	case XMIT_FULL:
		return NETDEV_TX_BUSY;
	case XMIT_PTB_MODE:
		set_canfd_xmit_mode(ndev);
		canfd_reigister_off_bit(priv, CANFD_TCMD_OFFSET, CAN_FD_OFF_STBY_MASK);

		if (cf->can_id & CAN_EFF_FLAG) {
			id = (cf->can_id & CAN_EFF_MASK);
			ttsen = 0 << TTSEN_8_32_SHIFT;
			id |= ttsen;
		} else {
			id = (cf->can_id & CAN_SFF_MASK);
			ttsen = 0 << TTSEN_8_32_SHIFT;
			id |= ttsen;
		}

		ctl = can_fd_len2dlc(cf->len);

		/* transmit can fd frame */
		if (priv->can_or_canfd == IPMS_CAN_TYPE_CANFD) {
			if (can_is_canfd_skb(skb)) {
				if (cf->can_id & CAN_EFF_FLAG)
					ctl |= CAN_FD_SET_IDE_MASK;
				else
					ctl &= CAN_FD_OFF_IDE_MASK;

				if (cf->flags & CANFD_BRS)
					ctl |= CAN_FD_SET_BRS_MASK;

				ctl |= CAN_FD_SET_EDL_MASK;

				addr_off = CANFD_TBUF_DATA_OFFSET;

				for (i = 0; i < cf->len; i += 4) {
					priv->write_reg(priv, addr_off,
							*((u32 *)(cf->data + i)));
					addr_off += 4;
				}
			} else {
				ctl &= CAN_FD_OFF_EDL_MASK;
				ctl &= CAN_FD_OFF_BRS_MASK;

				if (cf->can_id & CAN_EFF_FLAG)
					ctl |= CAN_FD_SET_IDE_MASK;
				else
					ctl &= CAN_FD_OFF_IDE_MASK;

				if (cf->can_id & CAN_RTR_FLAG) {
					ctl |= CAN_FD_SET_RTR_MASK;
					priv->write_reg(priv,
						CANFD_TBUF_ID_OFFSET, id);
					priv->write_reg(priv,
						CANFD_TBUF_CTL_OFFSET, ctl);
				} else {
					ctl &= CAN_FD_OFF_RTR_MASK;
					addr_off = CANFD_TBUF_DATA_OFFSET;
					priv->write_reg(priv, addr_off,
							*((u32 *)(cf->data + 0)));
					priv->write_reg(priv, addr_off + 4,
							*((u32 *)(cf->data + 4)));
				}
			}
			priv->write_reg(priv, CANFD_TBUF_ID_OFFSET, id);
			priv->write_reg(priv, CANFD_TBUF_CTL_OFFSET, ctl);
			addr_off = CANFD_TBUF_DATA_OFFSET;
		} else {
			ctl &= CAN_FD_OFF_EDL_MASK;
			ctl &= CAN_FD_OFF_BRS_MASK;

			if (cf->can_id & CAN_EFF_FLAG)
				ctl |= CAN_FD_SET_IDE_MASK;
			else
				ctl &= CAN_FD_OFF_IDE_MASK;

			if (cf->can_id & CAN_RTR_FLAG) {
				ctl |= CAN_FD_SET_RTR_MASK;
				priv->write_reg(priv, CANFD_TBUF_ID_OFFSET, id);
				priv->write_reg(priv, CANFD_TBUF_CTL_OFFSET, ctl);
			} else {
				ctl &= CAN_FD_OFF_RTR_MASK;
				priv->write_reg(priv, CANFD_TBUF_ID_OFFSET, id);
				priv->write_reg(priv, CANFD_TBUF_CTL_OFFSET, ctl);
				addr_off = CANFD_TBUF_DATA_OFFSET;
				priv->write_reg(priv, addr_off,
						*((u32 *)(cf->data + 0)));
				priv->write_reg(priv, addr_off + 4,
						*((u32 *)(cf->data + 4)));
			}
		}
		canfd_reigister_set_bit(priv, CANFD_TCMD_OFFSET, CAN_FD_SET_TPE_MASK);
		stats->tx_bytes += cf->len;
		break;
	default:
		break;
	}

	if (!(ndev->flags & IFF_ECHO) ||
		    (skb->protocol != htons(ETH_P_CAN) &&
		     skb->protocol != htons(ETH_P_CANFD))) {
			kfree_skb(skb);
			return 0;
	}

	skb = can_create_echo_skb(skb);
	if (!skb)
		return -ENOMEM;

	/* make settings for echo to reduce code in irq context */
	skb->ip_summed = CHECKSUM_UNNECESSARY;
	skb->dev = ndev;

	skb_tx_timestamp(skb);

	return NETDEV_TX_OK;
}

static int set_reset_mode(struct net_device *ndev)
{
	struct ipms_canfd_priv *priv = netdev_priv(ndev);
	u8 ret;

	ret = can_ioread8(priv->reg_base + CANFD_CFG_STAT_OFFSET);
	ret |= CAN_FD_SET_RST_MASK;
	can_iowrite8(ret, priv->reg_base + CANFD_CFG_STAT_OFFSET);

	return 0;
}

static void canfd_driver_stop(struct net_device *ndev)
{
	struct ipms_canfd_priv *priv = netdev_priv(ndev);
	int ret;

	ret = set_reset_mode(ndev);
	if (ret)
		netdev_err(ndev, "Mode Resetting Failed!\n");

	priv->can.state = CAN_STATE_STOPPED;
}

static int canfd_driver_close(struct net_device *ndev)
{
	struct ipms_canfd_priv *priv = netdev_priv(ndev);

	netif_stop_queue(ndev);
	napi_disable(&priv->napi);
	canfd_driver_stop(ndev);

	free_irq(ndev->irq, ndev);
	close_candev(ndev);

	pm_runtime_put(priv->dev);

	return 0;
}

static enum can_state get_of_chip_status(struct net_device *ndev)
{
	struct ipms_canfd_priv *priv = netdev_priv(ndev);
	u8 can_stat, eir;

	can_stat = can_ioread8(priv->reg_base + CANFD_CFG_STAT_OFFSET);
	eir = can_ioread8(priv->reg_base + CANFD_ERRINT_OFFSET);

	if (can_stat & CAN_FD_SET_BUSOFF_MASK)
		return CAN_STATE_BUS_OFF;

	if ((eir & CAN_FD_SET_EPASS_MASK) && ~(can_stat & CAN_FD_SET_BUSOFF_MASK))
		return CAN_STATE_ERROR_PASSIVE;

	if (eir & CAN_FD_SET_EWARN_MASK && ~(eir & CAN_FD_SET_EPASS_MASK))
		return CAN_STATE_ERROR_WARNING;

	if (~(eir & CAN_FD_SET_EPASS_MASK))
		return CAN_STATE_ERROR_ACTIVE;

	return CAN_STATE_ERROR_ACTIVE;
}

static void canfd_error_interrupt(struct net_device *ndev, u8 isr, u8 eir)
{
	struct ipms_canfd_priv *priv = netdev_priv(ndev);
	struct net_device_stats *stats = &ndev->stats;
	struct can_frame *cf;
	struct sk_buff *skb;
	u8 koer, recnt = 0, tecnt = 0, can_stat = 0;

	skb = alloc_can_err_skb(ndev, &cf);

	koer = can_ioread8(priv->reg_base + CANFD_EALCAP_OFFSET) & CAN_FD_SET_KOER_MASK;
	recnt = can_ioread8(priv->reg_base + CANFD_RECNT_OFFSET);
	tecnt = can_ioread8(priv->reg_base + CANFD_TECNT_OFFSET);

	/*Read can status*/
	can_stat = can_ioread8(priv->reg_base + CANFD_CFG_STAT_OFFSET);

	/* Bus off --->active error mode */
	if ((isr & CAN_FD_SET_EIF_MASK) && priv->can.state == CAN_STATE_BUS_OFF)
		priv->can.state = get_of_chip_status(ndev);

	/* State selection */
	if (can_stat & CAN_FD_SET_BUSOFF_MASK) {
		priv->can.state = get_of_chip_status(ndev);
		priv->can.can_stats.bus_off++;
		canfd_reigister_set_bit(priv, CANFD_CFG_STAT_OFFSET, CAN_FD_SET_BUSOFF_MASK);
		can_bus_off(ndev);
		if (skb)
			cf->can_id |= CAN_ERR_BUSOFF;

	} else if ((eir & CAN_FD_SET_EPASS_MASK) && ~(can_stat & CAN_FD_SET_BUSOFF_MASK)) {
		priv->can.state = get_of_chip_status(ndev);
		priv->can.can_stats.error_passive++;
		if (skb) {
			cf->can_id |= CAN_ERR_CRTL;
			cf->data[1] |= (recnt > 127) ? CAN_ERR_CRTL_RX_PASSIVE : 0;
			cf->data[1] |= (tecnt > 127) ? CAN_ERR_CRTL_TX_PASSIVE : 0;
			cf->data[6] = tecnt;
			cf->data[7] = recnt;
		}
	} else if (eir & CAN_FD_SET_EWARN_MASK && ~(eir & CAN_FD_SET_EPASS_MASK)) {
		priv->can.state = get_of_chip_status(ndev);
		priv->can.can_stats.error_warning++;
		if (skb) {
			cf->can_id |= CAN_ERR_CRTL;
			cf->data[1] |= (recnt > 95) ? CAN_ERR_CRTL_RX_WARNING : 0;
			cf->data[1] |= (tecnt > 95) ? CAN_ERR_CRTL_TX_WARNING : 0;
			cf->data[6] = tecnt;
			cf->data[7] = recnt;
		}
	}

	/* Check for in protocol defined error interrupt */
	if (eir & CAN_FD_SET_BEIF_MASK) {
		if (skb)
			cf->can_id |= CAN_ERR_BUSERROR | CAN_ERR_PROT;

		/* bit error interrupt */
		if (koer == CAN_FD_SET_BIT_ERROR_MASK) {
			stats->tx_errors++;
			if (skb) {
				cf->can_id |= CAN_ERR_PROT;
				cf->data[2] = CAN_ERR_PROT_BIT;
			}
		}
		/* format error interrupt */
		if (koer == CAN_FD_SET_FORM_ERROR_MASK) {
			stats->rx_errors++;
			if (skb) {
				cf->can_id |= CAN_ERR_PROT;
				cf->data[2] = CAN_ERR_PROT_FORM;
			}
		}
		/* stuffing error interrupt */
		if (koer == CAN_FD_SET_STUFF_ERROR_MASK) {
			stats->rx_errors++;
			if (skb) {
				cf->can_id |= CAN_ERR_PROT;
				cf->data[3] = CAN_ERR_PROT_STUFF;
			}
		}
		/* ack error interrupt */
		if (koer == CAN_FD_SET_ACK_ERROR_MASK) {
			stats->tx_errors++;
			if (skb) {
				cf->can_id |= CAN_ERR_PROT;
				cf->data[2] = CAN_ERR_PROT_LOC_ACK;
			}
		}
		/* crc error interrupt */
		if (koer == CAN_FD_SET_CRC_ERROR_MASK) {
			stats->rx_errors++;
			if (skb) {
				cf->can_id |= CAN_ERR_PROT;
				cf->data[2] = CAN_ERR_PROT_LOC_CRC_SEQ;
			}
		}
		priv->can.can_stats.bus_error++;
	}
	if (skb) {
		stats->rx_packets++;
		stats->rx_bytes += cf->can_dlc;
		netif_rx(skb);
	}

	netdev_dbg(ndev, "Recnt is 0x%02x", can_ioread8(priv->reg_base + CANFD_RECNT_OFFSET));
	netdev_dbg(ndev, "Tecnt is 0x%02x", can_ioread8(priv->reg_base + CANFD_TECNT_OFFSET));
}

static irqreturn_t canfd_interrupt(int irq, void *dev_id)
{
	struct net_device *ndev = (struct net_device *)dev_id;
	struct ipms_canfd_priv *priv = netdev_priv(ndev);
	u8 isr, eir;
	u8 isr_handled = 0, eir_handled = 0;

	/* read the value of interrupt status register */
	isr = can_ioread8(priv->reg_base + CANFD_RTIF_OFFSET);

	/* read the value of error interrupt register */
	eir = can_ioread8(priv->reg_base + CANFD_ERRINT_OFFSET);

	/* Check for Tx interrupt and Processing it */
	if (isr & (CAN_FD_SET_TPIF_MASK | CAN_FD_SET_TSIF_MASK)) {
		canfd_tx_interrupt(ndev, isr);
		isr_handled |= (CAN_FD_SET_TPIF_MASK | CAN_FD_SET_TSIF_MASK);
	}
	if (isr & (CAN_FD_SET_RAFIF_MASK | CAN_FD_SET_RFIF_MASK)) {
		canfd_rxfull_interrupt(ndev, isr);
		isr_handled |= (CAN_FD_SET_RAFIF_MASK | CAN_FD_SET_RFIF_MASK);
	}
	/* Check Rx interrupt and Processing the receive interrupt routine */
	if (isr & CAN_FD_SET_RIF_MASK) {
		canfd_reigister_off_bit(priv, CANFD_RTIE_OFFSET, CAN_FD_OFF_RIE_MASK);
		canfd_reigister_set_bit(priv, CANFD_RTIF_OFFSET, CAN_FD_SET_RIF_MASK);

		napi_schedule(&priv->napi);
		isr_handled |= CAN_FD_SET_RIF_MASK;
	}
	if ((isr & CAN_FD_SET_EIF_MASK) | (eir & (CAN_FD_SET_EPIF_MASK | CAN_FD_SET_BEIF_MASK))) {
		/* reset EPIF and BEIF. Reset EIF */
		canfd_reigister_set_bit(priv, CANFD_ERRINT_OFFSET,
					eir & (CAN_FD_SET_EPIF_MASK | CAN_FD_SET_BEIF_MASK));
		canfd_reigister_set_bit(priv, CANFD_RTIF_OFFSET,
					isr & CAN_FD_SET_EIF_MASK);

		canfd_error_interrupt(ndev, isr, eir);

		isr_handled |= CAN_FD_SET_EIF_MASK;
		eir_handled |= (CAN_FD_SET_EPIF_MASK | CAN_FD_SET_BEIF_MASK);
	}
	if ((isr_handled == 0) && (eir_handled == 0)) {
		netdev_err(ndev, "Unhandled interrupt!\n");
		return IRQ_NONE;
	}

	return IRQ_HANDLED;
}

static int canfd_chip_start(struct net_device *ndev)
{
	struct ipms_canfd_priv *priv = netdev_priv(ndev);
	int err;
	u8 ret;

	err = set_reset_mode(ndev);
	if (err) {
		netdev_err(ndev, "Mode Resetting Failed!\n");
		return err;
	}

	err = canfd_device_driver_bittime_configuration(ndev);
	if (err) {
		netdev_err(ndev, "Bittime Setting Failed!\n");
		return err;
	}

	/* Set Almost Full Warning Limit */
	canfd_reigister_set_bit(priv, CANFD_LIMIT_OFFSET, CAN_FD_SET_AFWL_MASK);

	/* Programmable Error Warning Limit = (EWL+1)*8. Set EWL=11->Error Warning=96 */
	canfd_reigister_set_bit(priv, CANFD_LIMIT_OFFSET, CAN_FD_SET_EWL_MASK);

	/* Interrupts enable */
	can_iowrite8(CAN_FD_INTR_ALL_MASK, priv->reg_base + CANFD_RTIE_OFFSET);

	/* Error Interrupts enable(Error Passive and Bus Error) */
	canfd_reigister_set_bit(priv, CANFD_ERRINT_OFFSET, CAN_FD_SET_EPIE_MASK);

	ret = can_ioread8(priv->reg_base + CANFD_CFG_STAT_OFFSET);

	/* Check whether it is loopback mode or normal mode */
	if (priv->can.ctrlmode & CAN_CTRLMODE_LOOPBACK) {
		ret |= CAN_FD_LBMIMOD_MASK;
	} else {
		ret &= ~CAN_FD_LBMEMOD_MASK;
		ret &= ~CAN_FD_LBMIMOD_MASK;
	}

	can_iowrite8(ret, priv->reg_base + CANFD_CFG_STAT_OFFSET);

	priv->can.state = CAN_STATE_ERROR_ACTIVE;

	return 0;
}

static int  canfd_do_set_mode(struct net_device *ndev, enum can_mode mode)
{
	int ret;

	switch (mode) {
	case CAN_MODE_START:
		ret = canfd_chip_start(ndev);
		if (ret) {
			netdev_err(ndev, "Could Not Start CAN device !!\n");
			return ret;
		}
		netif_wake_queue(ndev);
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}

	return ret;
}

static int canfd_driver_open(struct net_device *ndev)
{
	struct ipms_canfd_priv *priv = netdev_priv(ndev);
	int ret;

	ret = pm_runtime_get_sync(priv->dev);
	if (ret < 0) {
		netdev_err(ndev, "%s: pm_runtime_get failed(%d)\n",
			   __func__, ret);
		goto err;
	}

	/* Set chip into reset mode */
	ret = set_reset_mode(ndev);
	if (ret) {
		netdev_err(ndev, "Mode Resetting Failed!\n");
		return ret;
	}

	/* Common open */
	ret = open_candev(ndev);
	if (ret)
		return ret;

	/* Register interrupt handler */
	ret = request_irq(ndev->irq, canfd_interrupt, IRQF_SHARED, ndev->name, ndev);
	if (ret) {
		netdev_err(ndev, "Request_irq err: %d\n", ret);
		goto exit_irq;
	}

	ret = canfd_chip_start(ndev);
	if (ret) {
		netdev_err(ndev, "Could Not Start CAN device !\n");
		goto exit_can_start;
	}

	napi_enable(&priv->napi);
	netif_start_queue(ndev);

	return 0;

exit_can_start:
	free_irq(ndev->irq, ndev);
err:
	pm_runtime_put(priv->dev);
exit_irq:
	close_candev(ndev);
	return ret;
}

static int canfd_control_parse_dt(struct ipms_canfd_priv *priv)
{
	struct of_phandle_args args;
	u32 syscon_mask, syscon_shift;
	u32 can_or_canfd;
	u32 syscon_offset, regval;
	int ret;

	ret = of_parse_phandle_with_fixed_args(priv->dev->of_node,
						"starfive,sys-syscon", 3, 0, &args);
	if (ret) {
		dev_err(priv->dev, "Failed to parse starfive,sys-syscon\n");
		return -EINVAL;
	}

	priv->reg_syscon = syscon_node_to_regmap(args.np);
	of_node_put(args.np);
	if (IS_ERR(priv->reg_syscon))
		return PTR_ERR(priv->reg_syscon);

	syscon_offset = args.args[0];
	syscon_shift  = args.args[1];
	syscon_mask   = args.args[2];

	ret = device_property_read_u32(priv->dev, "syscon,can_or_canfd", &can_or_canfd);
	if (ret)
		goto exit_parse;

	priv->can_or_canfd = can_or_canfd;

	/* enable can2.0/canfd function */
	regval = can_or_canfd << syscon_shift;
	ret = regmap_update_bits(priv->reg_syscon, syscon_offset, syscon_mask, regval);
	if (ret)
		return ret;
	return 0;
exit_parse:
	return ret;
}

static const struct net_device_ops canfd_netdev_ops = {
	.ndo_open = canfd_driver_open,
	.ndo_stop = canfd_driver_close,
	.ndo_start_xmit = canfd_driver_start_xmit,
	.ndo_change_mtu = can_change_mtu,
};

static int canfd_driver_probe(struct platform_device *pdev)
{
	struct net_device *ndev;
	struct ipms_canfd_priv *priv;
	void __iomem *addr;
	int ret;
	u32 frq;

	addr = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(addr)) {
		ret = PTR_ERR(addr);
		goto exit;
	}

	ndev = alloc_candev(sizeof(struct ipms_canfd_priv), 1);
	if (!ndev) {
		ret = -ENOMEM;
		goto exit;
	}

	priv = netdev_priv(ndev);
	priv->dev = &pdev->dev;

	ret = canfd_control_parse_dt(priv);
	if (ret)
		goto free_exit;

	priv->nr_clks = devm_clk_bulk_get_all(priv->dev, &priv->clks);
	if (priv->nr_clks < 0) {
		dev_err(priv->dev, "Failed to get can clocks\n");
		ret = -ENODEV;
		goto free_exit;
	}

	ret = clk_bulk_prepare_enable(priv->nr_clks, priv->clks);
	if (ret) {
		dev_err(priv->dev, "Failed to enable clocks\n");
		goto free_exit;
	}

	priv->resets = devm_reset_control_array_get_exclusive(priv->dev);
	if (IS_ERR(priv->resets)) {
		ret = PTR_ERR(priv->resets);
		dev_err(priv->dev, "Failed to get can resets");
		goto clk_exit;
	}

	ret = reset_control_deassert(priv->resets);
	if (ret)
		goto clk_exit;
	priv->can.bittiming_const = &canfd_bittiming_const;
	priv->can.data_bittiming_const = &canfd_data_bittiming_const;
	priv->can.do_set_mode = canfd_do_set_mode;

	/* in user space the execution mode can be chosen */
	if (priv->can_or_canfd == IPMS_CAN_TYPE_CANFD)
		priv->can.ctrlmode_supported = CAN_CTRLMODE_LOOPBACK | CAN_CTRLMODE_FD;
	else
		priv->can.ctrlmode_supported = CAN_CTRLMODE_LOOPBACK;
	priv->reg_base = addr;
	priv->write_reg = canfd_write_reg_le;
	priv->read_reg = canfd_read_reg_le;

	pm_runtime_enable(&pdev->dev);

	priv->can_clk = devm_clk_get(&pdev->dev, "core_clk");
	if (IS_ERR(priv->can_clk)) {
		dev_err(&pdev->dev, "Device clock not found.\n");
		ret = PTR_ERR(priv->can_clk);
		goto reset_exit;
	}

	device_property_read_u32(priv->dev, "frequency", &frq);
	clk_set_rate(priv->can_clk, frq);

	priv->can.clock.freq = clk_get_rate(priv->can_clk);
	ndev->irq = platform_get_irq(pdev, 0);

	/* we support local echo */
	ndev->flags |= IFF_ECHO;
	ndev->netdev_ops = &canfd_netdev_ops;

	platform_set_drvdata(pdev, ndev);
	SET_NETDEV_DEV(ndev, &pdev->dev);

	netif_napi_add(ndev, &priv->napi, canfd_rx_poll, 16);
	ret = register_candev(ndev);
	if (ret) {
		dev_err(&pdev->dev, "Fail to register failed (err=%d)\n", ret);
		goto reset_exit;
	}

	dev_dbg(&pdev->dev, "Driver registered: regs=%p, irp=%d, clock=%d\n",
		priv->reg_base, ndev->irq, priv->can.clock.freq);

	return 0;

reset_exit:
	reset_control_assert(priv->resets);
clk_exit:
	clk_bulk_disable_unprepare(priv->nr_clks, priv->clks);
free_exit:
	free_candev(ndev);
exit:
	return ret;
}

static int canfd_driver_remove(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct ipms_canfd_priv *priv = netdev_priv(ndev);

	reset_control_assert(priv->resets);
	clk_bulk_disable_unprepare(priv->nr_clks, priv->clks);
	pm_runtime_disable(&pdev->dev);

	unregister_candev(ndev);
	netif_napi_del(&priv->napi);
	free_candev(ndev);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int __maybe_unused canfd_suspend(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);

	if (netif_running(ndev)) {
		netif_stop_queue(ndev);
		netif_device_detach(ndev);
		canfd_driver_stop(ndev);
	}

	return pm_runtime_force_suspend(dev);
}

static int __maybe_unused canfd_resume(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	int ret;

	ret = pm_runtime_force_resume(dev);
	if (ret) {
		dev_err(dev, "pm_runtime_force_resume failed on resume\n");
		return ret;
	}

	if (netif_running(ndev)) {
		ret = canfd_chip_start(ndev);
		if (ret) {
			dev_err(dev, "canfd_chip_start failed on resume\n");
			return ret;
		}

		netif_device_attach(ndev);
		netif_start_queue(ndev);
	}

	return 0;
}
#endif

#ifdef CONFIG_PM
static int canfd_runtime_suspend(struct device *dev)
{
	struct ipms_canfd_priv *priv = dev_get_drvdata(dev);

	reset_control_assert(priv->resets);
	clk_bulk_disable_unprepare(priv->nr_clks, priv->clks);

	return 0;
}

static int canfd_runtime_resume(struct device *dev)
{
	struct ipms_canfd_priv *priv = dev_get_drvdata(dev);
	int ret;

	ret = clk_bulk_prepare_enable(priv->nr_clks, priv->clks);
	if (ret) {
		dev_err(dev, "Failed to  prepare_enable clk\n");
		return ret;
	}

	ret = reset_control_deassert(priv->resets);
	if (ret) {
		dev_err(dev, "Failed to deassert reset\n");
		return ret;
	}

	return 0;
}
#endif

static const struct dev_pm_ops canfd_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(canfd_suspend, canfd_resume)
	SET_RUNTIME_PM_OPS(canfd_runtime_suspend,
			   canfd_runtime_resume, NULL)
};

static const struct of_device_id canfd_of_match[] = {
	{ .compatible = "ipms,can" },
	{ }
};
MODULE_DEVICE_TABLE(of, canfd_of_match);

static struct platform_driver can_driver = {
	.probe          = canfd_driver_probe,
	.remove         = canfd_driver_remove,
	.driver = {
		.name  = DRIVER_NAME,
		.pm    = &canfd_pm_ops,
		.of_match_table = canfd_of_match,
	},
};

module_platform_driver(can_driver);

MODULE_DESCRIPTION("ipms can controller driver for StarFive jh7110 SoC");
MODULE_AUTHOR("William Qiu<william.qiu@starfivetech.com");
MODULE_LICENSE("GPL");
