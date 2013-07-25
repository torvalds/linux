/*
 *  Copyright (c) 2013 Realtek Semiconductor Corp. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 */

#include <linux/init.h>
#include <linux/signal.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/mii.h>
#include <linux/ethtool.h>
#include <linux/usb.h>
#include <linux/crc32.h>
#include <linux/if_vlan.h>
#include <linux/uaccess.h>

/* Version Information */
#define DRIVER_VERSION "v1.0.0 (2013/05/03)"
#define DRIVER_AUTHOR "Realtek linux nic maintainers <nic_swsd@realtek.com>"
#define DRIVER_DESC "Realtek RTL8152 Based USB 2.0 Ethernet Adapters"
#define MODULENAME "r8152"

#define R8152_PHY_ID		32

#define PLA_IDR			0xc000
#define PLA_RCR			0xc010
#define PLA_RMS			0xc016
#define PLA_RXFIFO_CTRL0	0xc0a0
#define PLA_RXFIFO_CTRL1	0xc0a4
#define PLA_RXFIFO_CTRL2	0xc0a8
#define PLA_FMC			0xc0b4
#define PLA_CFG_WOL		0xc0b6
#define PLA_MAR			0xcd00
#define PAL_BDC_CR		0xd1a0
#define PLA_LEDSEL		0xdd90
#define PLA_LED_FEATURE		0xdd92
#define PLA_PHYAR		0xde00
#define PLA_GPHY_INTR_IMR	0xe022
#define PLA_EEE_CR		0xe040
#define PLA_EEEP_CR		0xe080
#define PLA_MAC_PWR_CTRL	0xe0c0
#define PLA_TCR0		0xe610
#define PLA_TCR1		0xe612
#define PLA_TXFIFO_CTRL		0xe618
#define PLA_RSTTELLY		0xe800
#define PLA_CR			0xe813
#define PLA_CRWECR		0xe81c
#define PLA_CONFIG5		0xe822
#define PLA_PHY_PWR		0xe84c
#define PLA_OOB_CTRL		0xe84f
#define PLA_CPCR		0xe854
#define PLA_MISC_0		0xe858
#define PLA_MISC_1		0xe85a
#define PLA_OCP_GPHY_BASE	0xe86c
#define PLA_TELLYCNT		0xe890
#define PLA_SFF_STS_7		0xe8de
#define PLA_PHYSTATUS		0xe908
#define PLA_BP_BA		0xfc26
#define PLA_BP_0		0xfc28
#define PLA_BP_1		0xfc2a
#define PLA_BP_2		0xfc2c
#define PLA_BP_3		0xfc2e
#define PLA_BP_4		0xfc30
#define PLA_BP_5		0xfc32
#define PLA_BP_6		0xfc34
#define PLA_BP_7		0xfc36

#define USB_DEV_STAT		0xb808
#define USB_USB_CTRL		0xd406
#define USB_PHY_CTRL		0xd408
#define USB_TX_AGG		0xd40a
#define USB_RX_BUF_TH		0xd40c
#define USB_USB_TIMER		0xd428
#define USB_PM_CTRL_STATUS	0xd432
#define USB_TX_DMA		0xd434
#define USB_UPS_CTRL		0xd800
#define USB_BP_BA		0xfc26
#define USB_BP_0		0xfc28
#define USB_BP_1		0xfc2a
#define USB_BP_2		0xfc2c
#define USB_BP_3		0xfc2e
#define USB_BP_4		0xfc30
#define USB_BP_5		0xfc32
#define USB_BP_6		0xfc34
#define USB_BP_7		0xfc36

/* OCP Registers */
#define OCP_ALDPS_CONFIG	0x2010
#define OCP_EEE_CONFIG1		0x2080
#define OCP_EEE_CONFIG2		0x2092
#define OCP_EEE_CONFIG3		0x2094
#define OCP_EEE_AR		0xa41a
#define OCP_EEE_DATA		0xa41c

/* PLA_RCR */
#define RCR_AAP			0x00000001
#define RCR_APM			0x00000002
#define RCR_AM			0x00000004
#define RCR_AB			0x00000008
#define RCR_ACPT_ALL		(RCR_AAP | RCR_APM | RCR_AM | RCR_AB)

/* PLA_RXFIFO_CTRL0 */
#define RXFIFO_THR1_NORMAL	0x00080002
#define RXFIFO_THR1_OOB		0x01800003

/* PLA_RXFIFO_CTRL1 */
#define RXFIFO_THR2_FULL	0x00000060
#define RXFIFO_THR2_HIGH	0x00000038
#define RXFIFO_THR2_OOB		0x0000004a

/* PLA_RXFIFO_CTRL2 */
#define RXFIFO_THR3_FULL	0x00000078
#define RXFIFO_THR3_HIGH	0x00000048
#define RXFIFO_THR3_OOB		0x0000005a

/* PLA_TXFIFO_CTRL */
#define TXFIFO_THR_NORMAL	0x00400008

/* PLA_FMC */
#define FMC_FCR_MCU_EN		0x0001

/* PLA_EEEP_CR */
#define EEEP_CR_EEEP_TX		0x0002

/* PLA_TCR0 */
#define TCR0_TX_EMPTY		0x0800
#define TCR0_AUTO_FIFO		0x0080

/* PLA_TCR1 */
#define VERSION_MASK		0x7cf0

/* PLA_CR */
#define CR_RST			0x10
#define CR_RE			0x08
#define CR_TE			0x04

/* PLA_CRWECR */
#define CRWECR_NORAML		0x00
#define CRWECR_CONFIG		0xc0

/* PLA_OOB_CTRL */
#define NOW_IS_OOB		0x80
#define TXFIFO_EMPTY		0x20
#define RXFIFO_EMPTY		0x10
#define LINK_LIST_READY		0x02
#define DIS_MCU_CLROOB		0x01
#define FIFO_EMPTY		(TXFIFO_EMPTY | RXFIFO_EMPTY)

/* PLA_MISC_1 */
#define RXDY_GATED_EN		0x0008

/* PLA_SFF_STS_7 */
#define RE_INIT_LL		0x8000
#define MCU_BORW_EN		0x4000

/* PLA_CPCR */
#define CPCR_RX_VLAN		0x0040

/* PLA_CFG_WOL */
#define MAGIC_EN		0x0001

/* PAL_BDC_CR */
#define ALDPS_PROXY_MODE	0x0001

/* PLA_CONFIG5 */
#define LAN_WAKE_EN		0x0002

/* PLA_LED_FEATURE */
#define LED_MODE_MASK		0x0700

/* PLA_PHY_PWR */
#define TX_10M_IDLE_EN		0x0080
#define PFM_PWM_SWITCH		0x0040

/* PLA_MAC_PWR_CTRL */
#define D3_CLK_GATED_EN		0x00004000
#define MCU_CLK_RATIO		0x07010f07
#define MCU_CLK_RATIO_MASK	0x0f0f0f0f

/* PLA_GPHY_INTR_IMR */
#define GPHY_STS_MSK		0x0001
#define SPEED_DOWN_MSK		0x0002
#define SPDWN_RXDV_MSK		0x0004
#define SPDWN_LINKCHG_MSK	0x0008

/* PLA_PHYAR */
#define PHYAR_FLAG		0x80000000

/* PLA_EEE_CR */
#define EEE_RX_EN		0x0001
#define EEE_TX_EN		0x0002

/* USB_DEV_STAT */
#define STAT_SPEED_MASK		0x0006
#define STAT_SPEED_HIGH		0x0000
#define STAT_SPEED_FULL		0x0001

/* USB_TX_AGG */
#define TX_AGG_MAX_THRESHOLD	0x03

/* USB_RX_BUF_TH */
#define RX_BUF_THR		0x7a120180

/* USB_TX_DMA */
#define TEST_MODE_DISABLE	0x00000001
#define TX_SIZE_ADJUST1		0x00000100

/* USB_UPS_CTRL */
#define POWER_CUT		0x0100

/* USB_PM_CTRL_STATUS */
#define RWSUME_INDICATE		0x0001

/* USB_USB_CTRL */
#define RX_AGG_DISABLE		0x0010

/* OCP_ALDPS_CONFIG */
#define ENPWRSAVE		0x8000
#define ENPDNPS			0x0200
#define LINKENA			0x0100
#define DIS_SDSAVE		0x0010

/* OCP_EEE_CONFIG1 */
#define RG_TXLPI_MSK_HFDUP	0x8000
#define RG_MATCLR_EN		0x4000
#define EEE_10_CAP		0x2000
#define EEE_NWAY_EN		0x1000
#define TX_QUIET_EN		0x0200
#define RX_QUIET_EN		0x0100
#define SDRISETIME		0x0010	/* bit 4 ~ 6 */
#define RG_RXLPI_MSK_HFDUP	0x0008
#define SDFALLTIME		0x0007	/* bit 0 ~ 2 */

/* OCP_EEE_CONFIG2 */
#define RG_LPIHYS_NUM		0x7000	/* bit 12 ~ 15 */
#define RG_DACQUIET_EN		0x0400
#define RG_LDVQUIET_EN		0x0200
#define RG_CKRSEL		0x0020
#define RG_EEEPRG_EN		0x0010

/* OCP_EEE_CONFIG3 */
#define FST_SNR_EYE_R		0x1500	/* bit 7 ~ 15 */
#define RG_LFS_SEL		0x0060	/* bit 6 ~ 5 */
#define MSK_PH			0x0006	/* bit 0 ~ 3 */

/* OCP_EEE_AR */
/* bit[15:14] function */
#define FUN_ADDR		0x0000
#define FUN_DATA		0x4000
/* bit[4:0] device addr */
#define DEVICE_ADDR		0x0007

/* OCP_EEE_DATA */
#define EEE_ADDR		0x003C
#define EEE_DATA		0x0002

enum rtl_register_content {
	_100bps		= 0x08,
	_10bps		= 0x04,
	LINK_STATUS	= 0x02,
	FULL_DUP	= 0x01,
};

#define RTL8152_REQT_READ	0xc0
#define RTL8152_REQT_WRITE	0x40
#define RTL8152_REQ_GET_REGS	0x05
#define RTL8152_REQ_SET_REGS	0x05

#define BYTE_EN_DWORD		0xff
#define BYTE_EN_WORD		0x33
#define BYTE_EN_BYTE		0x11
#define BYTE_EN_SIX_BYTES	0x3f
#define BYTE_EN_START_MASK	0x0f
#define BYTE_EN_END_MASK	0xf0

#define RTL8152_RMS		(VLAN_ETH_FRAME_LEN + VLAN_HLEN)
#define RTL8152_TX_TIMEOUT	(HZ)

/* rtl8152 flags */
enum rtl8152_flags {
	RTL8152_UNPLUG = 0,
	RX_URB_FAIL,
	RTL8152_SET_RX_MODE,
	WORK_ENABLE
};

/* Define these values to match your device */
#define VENDOR_ID_REALTEK		0x0bda
#define PRODUCT_ID_RTL8152		0x8152

#define MCU_TYPE_PLA			0x0100
#define MCU_TYPE_USB			0x0000

struct rx_desc {
	u32 opts1;
#define RX_LEN_MASK			0x7fff
	u32 opts2;
	u32 opts3;
	u32 opts4;
	u32 opts5;
	u32 opts6;
};

struct tx_desc {
	u32 opts1;
#define TX_FS			(1 << 31) /* First segment of a packet */
#define TX_LS			(1 << 30) /* Final segment of a packet */
#define TX_LEN_MASK		0xffff
	u32 opts2;
};

struct r8152 {
	unsigned long flags;
	struct usb_device *udev;
	struct tasklet_struct tl;
	struct net_device *netdev;
	struct urb *rx_urb, *tx_urb;
	struct sk_buff *tx_skb, *rx_skb;
	struct delayed_work schedule;
	struct mii_if_info mii;
	u32 msg_enable;
	u16 ocp_base;
	u8 version;
	u8 speed;
};

enum rtl_version {
	RTL_VER_UNKNOWN = 0,
	RTL_VER_01,
	RTL_VER_02
};

/* Maximum number of multicast addresses to filter (vs. Rx-all-multicast).
 * The RTL chips use a 64 element hash table based on the Ethernet CRC.
 */
static const int multicast_filter_limit = 32;

static
int get_registers(struct r8152 *tp, u16 value, u16 index, u16 size, void *data)
{
	return usb_control_msg(tp->udev, usb_rcvctrlpipe(tp->udev, 0),
			       RTL8152_REQ_GET_REGS, RTL8152_REQT_READ,
			       value, index, data, size, 500);
}

static
int set_registers(struct r8152 *tp, u16 value, u16 index, u16 size, void *data)
{
	return usb_control_msg(tp->udev, usb_sndctrlpipe(tp->udev, 0),
			       RTL8152_REQ_SET_REGS, RTL8152_REQT_WRITE,
			       value, index, data, size, 500);
}

static int generic_ocp_read(struct r8152 *tp, u16 index, u16 size,
				void *data, u16 type)
{
	u16	limit = 64;
	int	ret = 0;

	if (test_bit(RTL8152_UNPLUG, &tp->flags))
		return -ENODEV;

	/* both size and indix must be 4 bytes align */
	if ((size & 3) || !size || (index & 3) || !data)
		return -EPERM;

	if ((u32)index + (u32)size > 0xffff)
		return -EPERM;

	while (size) {
		if (size > limit) {
			ret = get_registers(tp, index, type, limit, data);
			if (ret < 0)
				break;

			index += limit;
			data += limit;
			size -= limit;
		} else {
			ret = get_registers(tp, index, type, size, data);
			if (ret < 0)
				break;

			index += size;
			data += size;
			size = 0;
			break;
		}
	}

	return ret;
}

static int generic_ocp_write(struct r8152 *tp, u16 index, u16 byteen,
				u16 size, void *data, u16 type)
{
	int	ret;
	u16	byteen_start, byteen_end, byen;
	u16	limit = 512;

	if (test_bit(RTL8152_UNPLUG, &tp->flags))
		return -ENODEV;

	/* both size and indix must be 4 bytes align */
	if ((size & 3) || !size || (index & 3) || !data)
		return -EPERM;

	if ((u32)index + (u32)size > 0xffff)
		return -EPERM;

	byteen_start = byteen & BYTE_EN_START_MASK;
	byteen_end = byteen & BYTE_EN_END_MASK;

	byen = byteen_start | (byteen_start << 4);
	ret = set_registers(tp, index, type | byen, 4, data);
	if (ret < 0)
		goto error1;

	index += 4;
	data += 4;
	size -= 4;

	if (size) {
		size -= 4;

		while (size) {
			if (size > limit) {
				ret = set_registers(tp, index,
					type | BYTE_EN_DWORD,
					limit, data);
				if (ret < 0)
					goto error1;

				index += limit;
				data += limit;
				size -= limit;
			} else {
				ret = set_registers(tp, index,
					type | BYTE_EN_DWORD,
					size, data);
				if (ret < 0)
					goto error1;

				index += size;
				data += size;
				size = 0;
				break;
			}
		}

		byen = byteen_end | (byteen_end >> 4);
		ret = set_registers(tp, index, type | byen, 4, data);
		if (ret < 0)
			goto error1;
	}

error1:
	return ret;
}

static inline
int pla_ocp_read(struct r8152 *tp, u16 index, u16 size, void *data)
{
	return generic_ocp_read(tp, index, size, data, MCU_TYPE_PLA);
}

static inline
int pla_ocp_write(struct r8152 *tp, u16 index, u16 byteen, u16 size, void *data)
{
	return generic_ocp_write(tp, index, byteen, size, data, MCU_TYPE_PLA);
}

static inline
int usb_ocp_read(struct r8152 *tp, u16 index, u16 size, void *data)
{
	return generic_ocp_read(tp, index, size, data, MCU_TYPE_USB);
}

static inline
int usb_ocp_write(struct r8152 *tp, u16 index, u16 byteen, u16 size, void *data)
{
	return generic_ocp_write(tp, index, byteen, size, data, MCU_TYPE_USB);
}

static u32 ocp_read_dword(struct r8152 *tp, u16 type, u16 index)
{
	u32 data;

	if (type == MCU_TYPE_PLA)
		pla_ocp_read(tp, index, sizeof(data), &data);
	else
		usb_ocp_read(tp, index, sizeof(data), &data);

	return __le32_to_cpu(data);
}

static void ocp_write_dword(struct r8152 *tp, u16 type, u16 index, u32 data)
{
	if (type == MCU_TYPE_PLA)
		pla_ocp_write(tp, index, BYTE_EN_DWORD, sizeof(data), &data);
	else
		usb_ocp_write(tp, index, BYTE_EN_DWORD, sizeof(data), &data);
}

static u16 ocp_read_word(struct r8152 *tp, u16 type, u16 index)
{
	u32 data;
	u8 shift = index & 2;

	index &= ~3;

	if (type == MCU_TYPE_PLA)
		pla_ocp_read(tp, index, sizeof(data), &data);
	else
		usb_ocp_read(tp, index, sizeof(data), &data);

	data = __le32_to_cpu(data);
	data >>= (shift * 8);
	data &= 0xffff;

	return (u16)data;
}

static void ocp_write_word(struct r8152 *tp, u16 type, u16 index, u32 data)
{
	u32 tmp, mask = 0xffff;
	u16 byen = BYTE_EN_WORD;
	u8 shift = index & 2;

	data &= mask;

	if (index & 2) {
		byen <<= shift;
		mask <<= (shift * 8);
		data <<= (shift * 8);
		index &= ~3;
	}

	if (type == MCU_TYPE_PLA)
		pla_ocp_read(tp, index, sizeof(tmp), &tmp);
	else
		usb_ocp_read(tp, index, sizeof(tmp), &tmp);

	tmp = __le32_to_cpu(tmp) & ~mask;
	tmp |= data;
	tmp = __cpu_to_le32(tmp);

	if (type == MCU_TYPE_PLA)
		pla_ocp_write(tp, index, byen, sizeof(tmp), &tmp);
	else
		usb_ocp_write(tp, index, byen, sizeof(tmp), &tmp);
}

static u8 ocp_read_byte(struct r8152 *tp, u16 type, u16 index)
{
	u32 data;
	u8 shift = index & 3;

	index &= ~3;

	if (type == MCU_TYPE_PLA)
		pla_ocp_read(tp, index, sizeof(data), &data);
	else
		usb_ocp_read(tp, index, sizeof(data), &data);

	data = __le32_to_cpu(data);
	data >>= (shift * 8);
	data &= 0xff;

	return (u8)data;
}

static void ocp_write_byte(struct r8152 *tp, u16 type, u16 index, u32 data)
{
	u32 tmp, mask = 0xff;
	u16 byen = BYTE_EN_BYTE;
	u8 shift = index & 3;

	data &= mask;

	if (index & 3) {
		byen <<= shift;
		mask <<= (shift * 8);
		data <<= (shift * 8);
		index &= ~3;
	}

	if (type == MCU_TYPE_PLA)
		pla_ocp_read(tp, index, sizeof(tmp), &tmp);
	else
		usb_ocp_read(tp, index, sizeof(tmp), &tmp);

	tmp = __le32_to_cpu(tmp) & ~mask;
	tmp |= data;
	tmp = __cpu_to_le32(tmp);

	if (type == MCU_TYPE_PLA)
		pla_ocp_write(tp, index, byen, sizeof(tmp), &tmp);
	else
		usb_ocp_write(tp, index, byen, sizeof(tmp), &tmp);
}

static void r8152_mdio_write(struct r8152 *tp, u32 reg_addr, u32 value)
{
	u32	ocp_data;
	int	i;

	ocp_data = PHYAR_FLAG | ((reg_addr & 0x1f) << 16) |
		   (value & 0xffff);

	ocp_write_dword(tp, MCU_TYPE_PLA, PLA_PHYAR, ocp_data);

	for (i = 20; i > 0; i--) {
		udelay(25);
		ocp_data = ocp_read_dword(tp, MCU_TYPE_PLA, PLA_PHYAR);
		if (!(ocp_data & PHYAR_FLAG))
			break;
	}
	udelay(20);
}

static int r8152_mdio_read(struct r8152 *tp, u32 reg_addr)
{
	u32	ocp_data;
	int	i;

	ocp_data = (reg_addr & 0x1f) << 16;
	ocp_write_dword(tp, MCU_TYPE_PLA, PLA_PHYAR, ocp_data);

	for (i = 20; i > 0; i--) {
		udelay(25);
		ocp_data = ocp_read_dword(tp, MCU_TYPE_PLA, PLA_PHYAR);
		if (ocp_data & PHYAR_FLAG)
			break;
	}
	udelay(20);

	if (!(ocp_data & PHYAR_FLAG))
		return -EAGAIN;

	return (u16)(ocp_data & 0xffff);
}

static int read_mii_word(struct net_device *netdev, int phy_id, int reg)
{
	struct r8152 *tp = netdev_priv(netdev);

	if (phy_id != R8152_PHY_ID)
		return -EINVAL;

	return r8152_mdio_read(tp, reg);
}

static
void write_mii_word(struct net_device *netdev, int phy_id, int reg, int val)
{
	struct r8152 *tp = netdev_priv(netdev);

	if (phy_id != R8152_PHY_ID)
		return;

	r8152_mdio_write(tp, reg, val);
}

static void ocp_reg_write(struct r8152 *tp, u16 addr, u16 data)
{
	u16 ocp_base, ocp_index;

	ocp_base = addr & 0xf000;
	if (ocp_base != tp->ocp_base) {
		ocp_write_word(tp, MCU_TYPE_PLA, PLA_OCP_GPHY_BASE, ocp_base);
		tp->ocp_base = ocp_base;
	}

	ocp_index = (addr & 0x0fff) | 0xb000;
	ocp_write_word(tp, MCU_TYPE_PLA, ocp_index, data);
}

static inline void set_ethernet_addr(struct r8152 *tp)
{
	struct net_device *dev = tp->netdev;
	u8 *node_id;

	node_id = kmalloc(sizeof(u8) * 8, GFP_KERNEL);
	if (!node_id) {
		netif_err(tp, probe, dev, "out of memory");
		return;
	}

	if (pla_ocp_read(tp, PLA_IDR, sizeof(u8) * 8, node_id) < 0)
		netif_notice(tp, probe, dev, "inet addr fail\n");
	else {
		memcpy(dev->dev_addr, node_id, dev->addr_len);
		memcpy(dev->perm_addr, dev->dev_addr, dev->addr_len);
	}
	kfree(node_id);
}

static int rtl8152_set_mac_address(struct net_device *netdev, void *p)
{
	struct r8152 *tp = netdev_priv(netdev);
	struct sockaddr *addr = p;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	memcpy(netdev->dev_addr, addr->sa_data, netdev->addr_len);

	ocp_write_byte(tp, MCU_TYPE_PLA, PLA_CRWECR, CRWECR_CONFIG);
	pla_ocp_write(tp, PLA_IDR, BYTE_EN_SIX_BYTES, 8, addr->sa_data);
	ocp_write_byte(tp, MCU_TYPE_PLA, PLA_CRWECR, CRWECR_NORAML);

	return 0;
}

static int alloc_all_urbs(struct r8152 *tp)
{
	tp->rx_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!tp->rx_urb)
		return 0;
	tp->tx_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!tp->tx_urb) {
		usb_free_urb(tp->rx_urb);
		return 0;
	}

	return 1;
}

static void free_all_urbs(struct r8152 *tp)
{
	usb_free_urb(tp->rx_urb);
	usb_free_urb(tp->tx_urb);
}

static struct net_device_stats *rtl8152_get_stats(struct net_device *dev)
{
	return &dev->stats;
}

static void read_bulk_callback(struct urb *urb)
{
	struct r8152 *tp;
	unsigned pkt_len;
	struct sk_buff *skb;
	struct net_device *netdev;
	struct net_device_stats *stats;
	int status = urb->status;
	int result;
	struct rx_desc *rx_desc;

	tp = urb->context;
	if (!tp)
		return;
	if (test_bit(RTL8152_UNPLUG, &tp->flags))
		return;
	netdev = tp->netdev;
	if (!netif_device_present(netdev))
		return;

	stats = rtl8152_get_stats(netdev);
	switch (status) {
	case 0:
		break;
	case -ESHUTDOWN:
		set_bit(RTL8152_UNPLUG, &tp->flags);
		netif_device_detach(tp->netdev);
	case -ENOENT:
		return;	/* the urb is in unlink state */
	case -ETIME:
		pr_warn_ratelimited("may be reset is needed?..\n");
		goto goon;
	default:
		pr_warn_ratelimited("Rx status %d\n", status);
		goto goon;
	}

	/* protect against short packets (tell me why we got some?!?) */
	if (urb->actual_length < sizeof(*rx_desc))
		goto goon;


	rx_desc = (struct rx_desc *)urb->transfer_buffer;
	pkt_len = le32_to_cpu(rx_desc->opts1) & RX_LEN_MASK;
	if (urb->actual_length < sizeof(struct rx_desc) + pkt_len)
		goto goon;

	skb = netdev_alloc_skb_ip_align(netdev, pkt_len);
	if (!skb)
		goto goon;

	memcpy(skb->data, tp->rx_skb->data + sizeof(struct rx_desc), pkt_len);
	skb_put(skb, pkt_len);
	skb->protocol = eth_type_trans(skb, netdev);
	netif_rx(skb);
	stats->rx_packets++;
	stats->rx_bytes += pkt_len;
goon:
	usb_fill_bulk_urb(tp->rx_urb, tp->udev, usb_rcvbulkpipe(tp->udev, 1),
		      tp->rx_skb->data, RTL8152_RMS + sizeof(struct rx_desc),
		      (usb_complete_t)read_bulk_callback, tp);
	result = usb_submit_urb(tp->rx_urb, GFP_ATOMIC);
	if (result == -ENODEV) {
		netif_device_detach(tp->netdev);
	} else if (result) {
		set_bit(RX_URB_FAIL, &tp->flags);
		goto resched;
	} else {
		clear_bit(RX_URB_FAIL, &tp->flags);
	}

	return;
resched:
	tasklet_schedule(&tp->tl);
}

static void rx_fixup(unsigned long data)
{
	struct r8152 *tp;
	int status;

	tp = (struct r8152 *)data;
	if (!test_bit(WORK_ENABLE, &tp->flags))
		return;

	status = usb_submit_urb(tp->rx_urb, GFP_ATOMIC);
	if (status == -ENODEV) {
		netif_device_detach(tp->netdev);
	} else if (status) {
		set_bit(RX_URB_FAIL, &tp->flags);
		goto tlsched;
	} else {
		clear_bit(RX_URB_FAIL, &tp->flags);
	}

	return;
tlsched:
	tasklet_schedule(&tp->tl);
}

static void write_bulk_callback(struct urb *urb)
{
	struct r8152 *tp;
	int status = urb->status;

	tp = urb->context;
	if (!tp)
		return;
	dev_kfree_skb_irq(tp->tx_skb);
	if (!netif_device_present(tp->netdev))
		return;
	if (status)
		dev_info(&urb->dev->dev, "%s: Tx status %d\n",
			 tp->netdev->name, status);
	tp->netdev->trans_start = jiffies;
	netif_wake_queue(tp->netdev);
}

static void rtl8152_tx_timeout(struct net_device *netdev)
{
	struct r8152 *tp = netdev_priv(netdev);
	struct net_device_stats *stats = rtl8152_get_stats(netdev);
	netif_warn(tp, tx_err, netdev, "Tx timeout.\n");
	usb_unlink_urb(tp->tx_urb);
	stats->tx_errors++;
}

static void rtl8152_set_rx_mode(struct net_device *netdev)
{
	struct r8152 *tp = netdev_priv(netdev);

	if (tp->speed & LINK_STATUS)
		set_bit(RTL8152_SET_RX_MODE, &tp->flags);
}

static void _rtl8152_set_rx_mode(struct net_device *netdev)
{
	struct r8152 *tp = netdev_priv(netdev);
	u32 tmp, *mc_filter;	/* Multicast hash filter */
	u32 ocp_data;

	mc_filter = kmalloc(sizeof(u32) * 2, GFP_KERNEL);
	if (!mc_filter) {
		netif_err(tp, link, netdev, "out of memory");
		return;
	}

	clear_bit(RTL8152_SET_RX_MODE, &tp->flags);
	netif_stop_queue(netdev);
	ocp_data = ocp_read_dword(tp, MCU_TYPE_PLA, PLA_RCR);
	ocp_data &= ~RCR_ACPT_ALL;
	ocp_data |= RCR_AB | RCR_APM;

	if (netdev->flags & IFF_PROMISC) {
		/* Unconditionally log net taps. */
		netif_notice(tp, link, netdev, "Promiscuous mode enabled\n");
		ocp_data |= RCR_AM | RCR_AAP;
		mc_filter[1] = mc_filter[0] = 0xffffffff;
	} else if ((netdev_mc_count(netdev) > multicast_filter_limit) ||
		   (netdev->flags & IFF_ALLMULTI)) {
		/* Too many to filter perfectly -- accept all multicasts. */
		ocp_data |= RCR_AM;
		mc_filter[1] = mc_filter[0] = 0xffffffff;
	} else {
		struct netdev_hw_addr *ha;

		mc_filter[1] = mc_filter[0] = 0;
		netdev_for_each_mc_addr(ha, netdev) {
			int bit_nr = ether_crc(ETH_ALEN, ha->addr) >> 26;
			mc_filter[bit_nr >> 5] |= 1 << (bit_nr & 31);
			ocp_data |= RCR_AM;
		}
	}

	tmp = mc_filter[0];
	mc_filter[0] = __cpu_to_le32(swab32(mc_filter[1]));
	mc_filter[1] = __cpu_to_le32(swab32(tmp));

	pla_ocp_write(tp, PLA_MAR, BYTE_EN_DWORD, sizeof(u32) * 2, mc_filter);
	ocp_write_dword(tp, MCU_TYPE_PLA, PLA_RCR, ocp_data);
	netif_wake_queue(netdev);
	kfree(mc_filter);
}

static netdev_tx_t rtl8152_start_xmit(struct sk_buff *skb,
					    struct net_device *netdev)
{
	struct r8152 *tp = netdev_priv(netdev);
	struct net_device_stats *stats = rtl8152_get_stats(netdev);
	struct tx_desc *tx_desc;
	unsigned int len;
	int res;

	netif_stop_queue(netdev);
	len = skb->len;
	if (skb_header_cloned(skb) || skb_headroom(skb) < sizeof(*tx_desc)) {
		struct sk_buff *tx_skb;

		tx_skb = skb_copy_expand(skb, sizeof(*tx_desc), 0, GFP_ATOMIC);
		dev_kfree_skb_any(skb);
		if (!tx_skb) {
			stats->tx_dropped++;
			netif_wake_queue(netdev);
			return NETDEV_TX_OK;
		}
		skb = tx_skb;
	}
	tx_desc = (struct tx_desc *)skb_push(skb, sizeof(*tx_desc));
	memset(tx_desc, 0, sizeof(*tx_desc));
	tx_desc->opts1 = cpu_to_le32((len & TX_LEN_MASK) | TX_FS | TX_LS);
	tp->tx_skb = skb;
	skb_tx_timestamp(skb);
	usb_fill_bulk_urb(tp->tx_urb, tp->udev, usb_sndbulkpipe(tp->udev, 2),
			  skb->data, skb->len,
			  (usb_complete_t)write_bulk_callback, tp);
	res = usb_submit_urb(tp->tx_urb, GFP_ATOMIC);
	if (res) {
		/* Can we get/handle EPIPE here? */
		if (res == -ENODEV) {
			netif_device_detach(tp->netdev);
		} else {
			netif_warn(tp, tx_err, netdev,
				   "failed tx_urb %d\n", res);
			stats->tx_errors++;
			netif_start_queue(netdev);
		}
	} else {
		stats->tx_packets++;
		stats->tx_bytes += skb->len;
	}

	return NETDEV_TX_OK;
}

static void r8152b_reset_packet_filter(struct r8152 *tp)
{
	u32	ocp_data;

	ocp_data = ocp_read_word(tp, MCU_TYPE_PLA, PLA_FMC);
	ocp_data &= ~FMC_FCR_MCU_EN;
	ocp_write_word(tp, MCU_TYPE_PLA, PLA_FMC, ocp_data);
	ocp_data |= FMC_FCR_MCU_EN;
	ocp_write_word(tp, MCU_TYPE_PLA, PLA_FMC, ocp_data);
}

static void rtl8152_nic_reset(struct r8152 *tp)
{
	int	i;

	ocp_write_byte(tp, MCU_TYPE_PLA, PLA_CR, CR_RST);

	for (i = 0; i < 1000; i++) {
		if (!(ocp_read_byte(tp, MCU_TYPE_PLA, PLA_CR) & CR_RST))
			break;
		udelay(100);
	}
}

static inline u8 rtl8152_get_speed(struct r8152 *tp)
{
	return ocp_read_byte(tp, MCU_TYPE_PLA, PLA_PHYSTATUS);
}

static int rtl8152_enable(struct r8152 *tp)
{
	u32	ocp_data;
	u8 speed;

	speed = rtl8152_get_speed(tp);
	if (speed & _100bps) {
		ocp_data = ocp_read_word(tp, MCU_TYPE_PLA, PLA_EEEP_CR);
		ocp_data &= ~EEEP_CR_EEEP_TX;
		ocp_write_word(tp, MCU_TYPE_PLA, PLA_EEEP_CR, ocp_data);
	} else {
		ocp_data = ocp_read_word(tp, MCU_TYPE_PLA, PLA_EEEP_CR);
		ocp_data |= EEEP_CR_EEEP_TX;
		ocp_write_word(tp, MCU_TYPE_PLA, PLA_EEEP_CR, ocp_data);
	}

	r8152b_reset_packet_filter(tp);

	ocp_data = ocp_read_byte(tp, MCU_TYPE_PLA, PLA_CR);
	ocp_data |= CR_RE | CR_TE;
	ocp_write_byte(tp, MCU_TYPE_PLA, PLA_CR, ocp_data);

	ocp_data = ocp_read_word(tp, MCU_TYPE_PLA, PLA_MISC_1);
	ocp_data &= ~RXDY_GATED_EN;
	ocp_write_word(tp, MCU_TYPE_PLA, PLA_MISC_1, ocp_data);

	usb_fill_bulk_urb(tp->rx_urb, tp->udev, usb_rcvbulkpipe(tp->udev, 1),
		      tp->rx_skb->data, RTL8152_RMS + sizeof(struct rx_desc),
		      (usb_complete_t)read_bulk_callback, tp);

	return usb_submit_urb(tp->rx_urb, GFP_KERNEL);
}

static void rtl8152_disable(struct r8152 *tp)
{
	u32	ocp_data;
	int	i;

	ocp_data = ocp_read_dword(tp, MCU_TYPE_PLA, PLA_RCR);
	ocp_data &= ~RCR_ACPT_ALL;
	ocp_write_dword(tp, MCU_TYPE_PLA, PLA_RCR, ocp_data);

	usb_kill_urb(tp->tx_urb);

	ocp_data = ocp_read_word(tp, MCU_TYPE_PLA, PLA_MISC_1);
	ocp_data |= RXDY_GATED_EN;
	ocp_write_word(tp, MCU_TYPE_PLA, PLA_MISC_1, ocp_data);

	for (i = 0; i < 1000; i++) {
		ocp_data = ocp_read_byte(tp, MCU_TYPE_PLA, PLA_OOB_CTRL);
		if ((ocp_data & FIFO_EMPTY) == FIFO_EMPTY)
			break;
		mdelay(1);
	}

	for (i = 0; i < 1000; i++) {
		if (ocp_read_word(tp, MCU_TYPE_PLA, PLA_TCR0) & TCR0_TX_EMPTY)
			break;
		mdelay(1);
	}

	usb_kill_urb(tp->rx_urb);

	rtl8152_nic_reset(tp);
}

static void r8152b_exit_oob(struct r8152 *tp)
{
	u32	ocp_data;
	int	i;

	ocp_data = ocp_read_dword(tp, MCU_TYPE_PLA, PLA_RCR);
	ocp_data &= ~RCR_ACPT_ALL;
	ocp_write_dword(tp, MCU_TYPE_PLA, PLA_RCR, ocp_data);

	ocp_data = ocp_read_word(tp, MCU_TYPE_PLA, PLA_MISC_1);
	ocp_data |= RXDY_GATED_EN;
	ocp_write_word(tp, MCU_TYPE_PLA, PLA_MISC_1, ocp_data);

	ocp_write_byte(tp, MCU_TYPE_PLA, PLA_CRWECR, CRWECR_NORAML);
	ocp_write_byte(tp, MCU_TYPE_PLA, PLA_CR, 0x00);

	ocp_data = ocp_read_byte(tp, MCU_TYPE_PLA, PLA_OOB_CTRL);
	ocp_data &= ~NOW_IS_OOB;
	ocp_write_byte(tp, MCU_TYPE_PLA, PLA_OOB_CTRL, ocp_data);

	ocp_data = ocp_read_word(tp, MCU_TYPE_PLA, PLA_SFF_STS_7);
	ocp_data &= ~MCU_BORW_EN;
	ocp_write_word(tp, MCU_TYPE_PLA, PLA_SFF_STS_7, ocp_data);

	for (i = 0; i < 1000; i++) {
		ocp_data = ocp_read_byte(tp, MCU_TYPE_PLA, PLA_OOB_CTRL);
		if (ocp_data & LINK_LIST_READY)
			break;
		mdelay(1);
	}

	ocp_data = ocp_read_word(tp, MCU_TYPE_PLA, PLA_SFF_STS_7);
	ocp_data |= RE_INIT_LL;
	ocp_write_word(tp, MCU_TYPE_PLA, PLA_SFF_STS_7, ocp_data);

	for (i = 0; i < 1000; i++) {
		ocp_data = ocp_read_byte(tp, MCU_TYPE_PLA, PLA_OOB_CTRL);
		if (ocp_data & LINK_LIST_READY)
			break;
		mdelay(1);
	}

	rtl8152_nic_reset(tp);

	/* rx share fifo credit full threshold */
	ocp_write_dword(tp, MCU_TYPE_PLA, PLA_RXFIFO_CTRL0, RXFIFO_THR1_NORMAL);

	ocp_data = ocp_read_word(tp, MCU_TYPE_USB, USB_DEV_STAT);
	ocp_data &= STAT_SPEED_MASK;
	if (ocp_data == STAT_SPEED_FULL) {
		/* rx share fifo credit near full threshold */
		ocp_write_dword(tp, MCU_TYPE_PLA, PLA_RXFIFO_CTRL1,
				RXFIFO_THR2_FULL);
		ocp_write_dword(tp, MCU_TYPE_PLA, PLA_RXFIFO_CTRL2,
				RXFIFO_THR3_FULL);
	} else {
		/* rx share fifo credit near full threshold */
		ocp_write_dword(tp, MCU_TYPE_PLA, PLA_RXFIFO_CTRL1,
				RXFIFO_THR2_HIGH);
		ocp_write_dword(tp, MCU_TYPE_PLA, PLA_RXFIFO_CTRL2,
				RXFIFO_THR3_HIGH);
	}

	/* TX share fifo free credit full threshold */
	ocp_write_dword(tp, MCU_TYPE_PLA, PLA_TXFIFO_CTRL, TXFIFO_THR_NORMAL);

	ocp_write_byte(tp, MCU_TYPE_USB, USB_TX_AGG, TX_AGG_MAX_THRESHOLD);
	ocp_write_dword(tp, MCU_TYPE_USB, USB_RX_BUF_TH, RX_BUF_THR);
	ocp_write_dword(tp, MCU_TYPE_USB, USB_TX_DMA,
			TEST_MODE_DISABLE | TX_SIZE_ADJUST1);

	ocp_data = ocp_read_word(tp, MCU_TYPE_PLA, PLA_CPCR);
	ocp_data &= ~CPCR_RX_VLAN;
	ocp_write_word(tp, MCU_TYPE_PLA, PLA_CPCR, ocp_data);

	ocp_write_word(tp, MCU_TYPE_PLA, PLA_RMS, RTL8152_RMS);

	ocp_data = ocp_read_word(tp, MCU_TYPE_PLA, PLA_TCR0);
	ocp_data |= TCR0_AUTO_FIFO;
	ocp_write_word(tp, MCU_TYPE_PLA, PLA_TCR0, ocp_data);
}

static void r8152b_enter_oob(struct r8152 *tp)
{
	u32	ocp_data;
	int	i;

	ocp_data = ocp_read_byte(tp, MCU_TYPE_PLA, PLA_OOB_CTRL);
	ocp_data &= ~NOW_IS_OOB;
	ocp_write_byte(tp, MCU_TYPE_PLA, PLA_OOB_CTRL, ocp_data);

	ocp_write_dword(tp, MCU_TYPE_PLA, PLA_RXFIFO_CTRL0, RXFIFO_THR1_OOB);
	ocp_write_dword(tp, MCU_TYPE_PLA, PLA_RXFIFO_CTRL1, RXFIFO_THR2_OOB);
	ocp_write_dword(tp, MCU_TYPE_PLA, PLA_RXFIFO_CTRL2, RXFIFO_THR3_OOB);

	rtl8152_disable(tp);

	for (i = 0; i < 1000; i++) {
		ocp_data = ocp_read_byte(tp, MCU_TYPE_PLA, PLA_OOB_CTRL);
		if (ocp_data & LINK_LIST_READY)
			break;
		mdelay(1);
	}

	ocp_data = ocp_read_word(tp, MCU_TYPE_PLA, PLA_SFF_STS_7);
	ocp_data |= RE_INIT_LL;
	ocp_write_word(tp, MCU_TYPE_PLA, PLA_SFF_STS_7, ocp_data);

	for (i = 0; i < 1000; i++) {
		ocp_data = ocp_read_byte(tp, MCU_TYPE_PLA, PLA_OOB_CTRL);
		if (ocp_data & LINK_LIST_READY)
			break;
		mdelay(1);
	}

	ocp_write_word(tp, MCU_TYPE_PLA, PLA_RMS, RTL8152_RMS);

	ocp_data = ocp_read_word(tp, MCU_TYPE_PLA, PLA_CFG_WOL);
	ocp_data |= MAGIC_EN;
	ocp_write_word(tp, MCU_TYPE_PLA, PLA_CFG_WOL, ocp_data);

	ocp_data = ocp_read_word(tp, MCU_TYPE_PLA, PLA_CPCR);
	ocp_data |= CPCR_RX_VLAN;
	ocp_write_word(tp, MCU_TYPE_PLA, PLA_CPCR, ocp_data);

	ocp_data = ocp_read_word(tp, MCU_TYPE_PLA, PAL_BDC_CR);
	ocp_data |= ALDPS_PROXY_MODE;
	ocp_write_word(tp, MCU_TYPE_PLA, PAL_BDC_CR, ocp_data);

	ocp_data = ocp_read_byte(tp, MCU_TYPE_PLA, PLA_OOB_CTRL);
	ocp_data |= NOW_IS_OOB | DIS_MCU_CLROOB;
	ocp_write_byte(tp, MCU_TYPE_PLA, PLA_OOB_CTRL, ocp_data);

	ocp_write_byte(tp, MCU_TYPE_PLA, PLA_CONFIG5, LAN_WAKE_EN);

	ocp_data = ocp_read_word(tp, MCU_TYPE_PLA, PLA_MISC_1);
	ocp_data &= ~RXDY_GATED_EN;
	ocp_write_word(tp, MCU_TYPE_PLA, PLA_MISC_1, ocp_data);

	ocp_data = ocp_read_dword(tp, MCU_TYPE_PLA, PLA_RCR);
	ocp_data |= RCR_APM | RCR_AM | RCR_AB;
	ocp_write_dword(tp, MCU_TYPE_PLA, PLA_RCR, ocp_data);
}

static void r8152b_disable_aldps(struct r8152 *tp)
{
	ocp_reg_write(tp, OCP_ALDPS_CONFIG, ENPDNPS | LINKENA | DIS_SDSAVE);
	msleep(20);
}

static inline void r8152b_enable_aldps(struct r8152 *tp)
{
	ocp_reg_write(tp, OCP_ALDPS_CONFIG, ENPWRSAVE | ENPDNPS |
					    LINKENA | DIS_SDSAVE);
}

static int rtl8152_set_speed(struct r8152 *tp, u8 autoneg, u16 speed, u8 duplex)
{
	u16 bmcr, anar;
	int ret = 0;

	cancel_delayed_work_sync(&tp->schedule);
	anar = r8152_mdio_read(tp, MII_ADVERTISE);
	anar &= ~(ADVERTISE_10HALF | ADVERTISE_10FULL |
		  ADVERTISE_100HALF | ADVERTISE_100FULL);

	if (autoneg == AUTONEG_DISABLE) {
		if (speed == SPEED_10) {
			bmcr = 0;
			anar |= ADVERTISE_10HALF | ADVERTISE_10FULL;
		} else if (speed == SPEED_100) {
			bmcr = BMCR_SPEED100;
			anar |= ADVERTISE_100HALF | ADVERTISE_100FULL;
		} else {
			ret = -EINVAL;
			goto out;
		}

		if (duplex == DUPLEX_FULL)
			bmcr |= BMCR_FULLDPLX;
	} else {
		if (speed == SPEED_10) {
			if (duplex == DUPLEX_FULL)
				anar |= ADVERTISE_10HALF | ADVERTISE_10FULL;
			else
				anar |= ADVERTISE_10HALF;
		} else if (speed == SPEED_100) {
			if (duplex == DUPLEX_FULL) {
				anar |= ADVERTISE_10HALF | ADVERTISE_10FULL;
				anar |= ADVERTISE_100HALF | ADVERTISE_100FULL;
			} else {
				anar |= ADVERTISE_10HALF;
				anar |= ADVERTISE_100HALF;
			}
		} else {
			ret = -EINVAL;
			goto out;
		}

		bmcr = BMCR_ANENABLE | BMCR_ANRESTART;
	}

	r8152_mdio_write(tp, MII_ADVERTISE, anar);
	r8152_mdio_write(tp, MII_BMCR, bmcr);

out:
	schedule_delayed_work(&tp->schedule, 5 * HZ);

	return ret;
}

static void rtl8152_down(struct r8152 *tp)
{
	u32	ocp_data;

	ocp_data = ocp_read_word(tp, MCU_TYPE_USB, USB_UPS_CTRL);
	ocp_data &= ~POWER_CUT;
	ocp_write_word(tp, MCU_TYPE_USB, USB_UPS_CTRL, ocp_data);

	r8152b_disable_aldps(tp);
	r8152b_enter_oob(tp);
	r8152b_enable_aldps(tp);
}

static void set_carrier(struct r8152 *tp)
{
	struct net_device *netdev = tp->netdev;
	u8 speed;

	speed = rtl8152_get_speed(tp);

	if (speed & LINK_STATUS) {
		if (!(tp->speed & LINK_STATUS)) {
			rtl8152_enable(tp);
			set_bit(RTL8152_SET_RX_MODE, &tp->flags);
			netif_carrier_on(netdev);
		}
	} else {
		if (tp->speed & LINK_STATUS) {
			netif_carrier_off(netdev);
			rtl8152_disable(tp);
		}
	}
	tp->speed = speed;
}

static void rtl_work_func_t(struct work_struct *work)
{
	struct r8152 *tp = container_of(work, struct r8152, schedule.work);

	if (!test_bit(WORK_ENABLE, &tp->flags))
		goto out1;

	if (test_bit(RTL8152_UNPLUG, &tp->flags))
		goto out1;

	set_carrier(tp);

	if (test_bit(RTL8152_SET_RX_MODE, &tp->flags))
		_rtl8152_set_rx_mode(tp->netdev);

	schedule_delayed_work(&tp->schedule, HZ);

out1:
	return;
}

static int rtl8152_open(struct net_device *netdev)
{
	struct r8152 *tp = netdev_priv(netdev);
	int res = 0;

	tp->speed = rtl8152_get_speed(tp);
	if (tp->speed & LINK_STATUS) {
		res = rtl8152_enable(tp);
		if (res) {
			if (res == -ENODEV)
				netif_device_detach(tp->netdev);

			netif_err(tp, ifup, netdev,
				  "rtl8152_open failed: %d\n", res);
			return res;
		}

		netif_carrier_on(netdev);
	} else {
		netif_stop_queue(netdev);
		netif_carrier_off(netdev);
	}

	rtl8152_set_speed(tp, AUTONEG_ENABLE, SPEED_100, DUPLEX_FULL);
	netif_start_queue(netdev);
	set_bit(WORK_ENABLE, &tp->flags);
	schedule_delayed_work(&tp->schedule, 0);

	return res;
}

static int rtl8152_close(struct net_device *netdev)
{
	struct r8152 *tp = netdev_priv(netdev);
	int res = 0;

	clear_bit(WORK_ENABLE, &tp->flags);
	cancel_delayed_work_sync(&tp->schedule);
	netif_stop_queue(netdev);
	rtl8152_disable(tp);

	return res;
}

static void rtl_clear_bp(struct r8152 *tp)
{
	ocp_write_dword(tp, MCU_TYPE_PLA, PLA_BP_0, 0);
	ocp_write_dword(tp, MCU_TYPE_PLA, PLA_BP_2, 0);
	ocp_write_dword(tp, MCU_TYPE_PLA, PLA_BP_4, 0);
	ocp_write_dword(tp, MCU_TYPE_PLA, PLA_BP_6, 0);
	ocp_write_dword(tp, MCU_TYPE_USB, USB_BP_0, 0);
	ocp_write_dword(tp, MCU_TYPE_USB, USB_BP_2, 0);
	ocp_write_dword(tp, MCU_TYPE_USB, USB_BP_4, 0);
	ocp_write_dword(tp, MCU_TYPE_USB, USB_BP_6, 0);
	mdelay(3);
	ocp_write_word(tp, MCU_TYPE_PLA, PLA_BP_BA, 0);
	ocp_write_word(tp, MCU_TYPE_USB, USB_BP_BA, 0);
}

static void r8152b_enable_eee(struct r8152 *tp)
{
	u32	ocp_data;

	ocp_data = ocp_read_word(tp, MCU_TYPE_PLA, PLA_EEE_CR);
	ocp_data |= EEE_RX_EN | EEE_TX_EN;
	ocp_write_word(tp, MCU_TYPE_PLA, PLA_EEE_CR, ocp_data);
	ocp_reg_write(tp, OCP_EEE_CONFIG1, RG_TXLPI_MSK_HFDUP | RG_MATCLR_EN |
					   EEE_10_CAP | EEE_NWAY_EN |
					   TX_QUIET_EN | RX_QUIET_EN |
					   SDRISETIME | RG_RXLPI_MSK_HFDUP |
					   SDFALLTIME);
	ocp_reg_write(tp, OCP_EEE_CONFIG2, RG_LPIHYS_NUM | RG_DACQUIET_EN |
					   RG_LDVQUIET_EN | RG_CKRSEL |
					   RG_EEEPRG_EN);
	ocp_reg_write(tp, OCP_EEE_CONFIG3, FST_SNR_EYE_R | RG_LFS_SEL | MSK_PH);
	ocp_reg_write(tp, OCP_EEE_AR, FUN_ADDR | DEVICE_ADDR);
	ocp_reg_write(tp, OCP_EEE_DATA, EEE_ADDR);
	ocp_reg_write(tp, OCP_EEE_AR, FUN_DATA | DEVICE_ADDR);
	ocp_reg_write(tp, OCP_EEE_DATA, EEE_DATA);
	ocp_reg_write(tp, OCP_EEE_AR, 0x0000);
}

static void r8152b_enable_fc(struct r8152 *tp)
{
	u16 anar;

	anar = r8152_mdio_read(tp, MII_ADVERTISE);
	anar |= ADVERTISE_PAUSE_CAP | ADVERTISE_PAUSE_ASYM;
	r8152_mdio_write(tp, MII_ADVERTISE, anar);
}

static void r8152b_hw_phy_cfg(struct r8152 *tp)
{
	r8152_mdio_write(tp, MII_BMCR, BMCR_ANENABLE);
	r8152b_disable_aldps(tp);
}

static void r8152b_init(struct r8152 *tp)
{
	u32	ocp_data;
	int	i;

	rtl_clear_bp(tp);

	if (tp->version == RTL_VER_01) {
		ocp_data = ocp_read_word(tp, MCU_TYPE_PLA, PLA_LED_FEATURE);
		ocp_data &= ~LED_MODE_MASK;
		ocp_write_word(tp, MCU_TYPE_PLA, PLA_LED_FEATURE, ocp_data);
	}

	r8152b_hw_phy_cfg(tp);

	ocp_data = ocp_read_word(tp, MCU_TYPE_USB, USB_UPS_CTRL);
	ocp_data &= ~POWER_CUT;
	ocp_write_word(tp, MCU_TYPE_USB, USB_UPS_CTRL, ocp_data);

	ocp_data = ocp_read_word(tp, MCU_TYPE_USB, USB_PM_CTRL_STATUS);
	ocp_data &= ~RWSUME_INDICATE;
	ocp_write_word(tp, MCU_TYPE_USB, USB_PM_CTRL_STATUS, ocp_data);

	r8152b_exit_oob(tp);

	ocp_data = ocp_read_word(tp, MCU_TYPE_PLA, PLA_PHY_PWR);
	ocp_data |= TX_10M_IDLE_EN | PFM_PWM_SWITCH;
	ocp_write_word(tp, MCU_TYPE_PLA, PLA_PHY_PWR, ocp_data);
	ocp_data = ocp_read_dword(tp, MCU_TYPE_PLA, PLA_MAC_PWR_CTRL);
	ocp_data &= ~MCU_CLK_RATIO_MASK;
	ocp_data |= MCU_CLK_RATIO | D3_CLK_GATED_EN;
	ocp_write_dword(tp, MCU_TYPE_PLA, PLA_MAC_PWR_CTRL, ocp_data);
	ocp_data = GPHY_STS_MSK | SPEED_DOWN_MSK |
		   SPDWN_RXDV_MSK | SPDWN_LINKCHG_MSK;
	ocp_write_word(tp, MCU_TYPE_PLA, PLA_GPHY_INTR_IMR, ocp_data);

	r8152b_enable_eee(tp);
	r8152b_enable_aldps(tp);
	r8152b_enable_fc(tp);

	r8152_mdio_write(tp, MII_BMCR, BMCR_RESET | BMCR_ANENABLE |
				       BMCR_ANRESTART);
	for (i = 0; i < 100; i++) {
		udelay(100);
		if (!(r8152_mdio_read(tp, MII_BMCR) & BMCR_RESET))
			break;
	}

	/* disable rx aggregation */
	ocp_data = ocp_read_word(tp, MCU_TYPE_USB, USB_USB_CTRL);
	ocp_data |= RX_AGG_DISABLE;
	ocp_write_word(tp, MCU_TYPE_USB, USB_USB_CTRL, ocp_data);
}

static int rtl8152_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct r8152 *tp = usb_get_intfdata(intf);

	netif_device_detach(tp->netdev);

	if (netif_running(tp->netdev)) {
		clear_bit(WORK_ENABLE, &tp->flags);
		cancel_delayed_work_sync(&tp->schedule);
	}

	rtl8152_down(tp);

	return 0;
}

static int rtl8152_resume(struct usb_interface *intf)
{
	struct r8152 *tp = usb_get_intfdata(intf);

	r8152b_init(tp);
	netif_device_attach(tp->netdev);
	if (netif_running(tp->netdev)) {
		rtl8152_enable(tp);
		set_bit(WORK_ENABLE, &tp->flags);
		set_bit(RTL8152_SET_RX_MODE, &tp->flags);
		schedule_delayed_work(&tp->schedule, 0);
	}

	return 0;
}

static void rtl8152_get_drvinfo(struct net_device *netdev,
				struct ethtool_drvinfo *info)
{
	struct r8152 *tp = netdev_priv(netdev);

	strncpy(info->driver, MODULENAME, ETHTOOL_BUSINFO_LEN);
	strncpy(info->version, DRIVER_VERSION, ETHTOOL_BUSINFO_LEN);
	usb_make_path(tp->udev, info->bus_info, sizeof(info->bus_info));
}

static
int rtl8152_get_settings(struct net_device *netdev, struct ethtool_cmd *cmd)
{
	struct r8152 *tp = netdev_priv(netdev);

	if (!tp->mii.mdio_read)
		return -EOPNOTSUPP;

	return mii_ethtool_gset(&tp->mii, cmd);
}

static int rtl8152_set_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct r8152 *tp = netdev_priv(dev);

	return rtl8152_set_speed(tp, cmd->autoneg, cmd->speed, cmd->duplex);
}

static struct ethtool_ops ops = {
	.get_drvinfo = rtl8152_get_drvinfo,
	.get_settings = rtl8152_get_settings,
	.set_settings = rtl8152_set_settings,
	.get_link = ethtool_op_get_link,
};

static int rtl8152_ioctl(struct net_device *netdev, struct ifreq *rq, int cmd)
{
	struct r8152 *tp = netdev_priv(netdev);
	struct mii_ioctl_data *data = if_mii(rq);
	int res = 0;

	switch (cmd) {
	case SIOCGMIIPHY:
		data->phy_id = R8152_PHY_ID; /* Internal PHY */
		break;

	case SIOCGMIIREG:
		data->val_out = r8152_mdio_read(tp, data->reg_num);
		break;

	case SIOCSMIIREG:
		if (!capable(CAP_NET_ADMIN)) {
			res = -EPERM;
			break;
		}
		r8152_mdio_write(tp, data->reg_num, data->val_in);
		break;

	default:
		res = -EOPNOTSUPP;
	}

	return res;
}

static const struct net_device_ops rtl8152_netdev_ops = {
	.ndo_open		= rtl8152_open,
	.ndo_stop		= rtl8152_close,
	.ndo_do_ioctl		= rtl8152_ioctl,
	.ndo_start_xmit		= rtl8152_start_xmit,
	.ndo_tx_timeout		= rtl8152_tx_timeout,
	.ndo_set_rx_mode	= rtl8152_set_rx_mode,
	.ndo_set_mac_address	= rtl8152_set_mac_address,

	.ndo_change_mtu		= eth_change_mtu,
	.ndo_validate_addr	= eth_validate_addr,
};

static void r8152b_get_version(struct r8152 *tp)
{
	u32	ocp_data;
	u16	version;

	ocp_data = ocp_read_word(tp, MCU_TYPE_PLA, PLA_TCR1);
	version = (u16)(ocp_data & VERSION_MASK);

	switch (version) {
	case 0x4c00:
		tp->version = RTL_VER_01;
		break;
	case 0x4c10:
		tp->version = RTL_VER_02;
		break;
	default:
		netif_info(tp, probe, tp->netdev,
			   "Unknown version 0x%04x\n", version);
		break;
	}
}

static int rtl8152_probe(struct usb_interface *intf,
			 const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	struct r8152 *tp;
	struct net_device *netdev;

	if (udev->actconfig->desc.bConfigurationValue != 1) {
		usb_driver_set_configuration(udev, 1);
		return -ENODEV;
	}

	netdev = alloc_etherdev(sizeof(struct r8152));
	if (!netdev) {
		dev_err(&intf->dev, "Out of memory");
		return -ENOMEM;
	}

	tp = netdev_priv(netdev);
	tp->msg_enable = 0x7FFF;

	tasklet_init(&tp->tl, rx_fixup, (unsigned long)tp);
	INIT_DELAYED_WORK(&tp->schedule, rtl_work_func_t);

	tp->udev = udev;
	tp->netdev = netdev;
	netdev->netdev_ops = &rtl8152_netdev_ops;
	netdev->watchdog_timeo = RTL8152_TX_TIMEOUT;
	netdev->features &= ~NETIF_F_IP_CSUM;
	SET_ETHTOOL_OPS(netdev, &ops);
	tp->speed = 0;

	tp->mii.dev = netdev;
	tp->mii.mdio_read = read_mii_word;
	tp->mii.mdio_write = write_mii_word;
	tp->mii.phy_id_mask = 0x3f;
	tp->mii.reg_num_mask = 0x1f;
	tp->mii.phy_id = R8152_PHY_ID;
	tp->mii.supports_gmii = 0;

	r8152b_get_version(tp);
	r8152b_init(tp);
	set_ethernet_addr(tp);

	if (!alloc_all_urbs(tp)) {
		netif_err(tp, probe, netdev, "out of memory");
		goto out;
	}

	tp->rx_skb = netdev_alloc_skb(netdev,
			RTL8152_RMS + sizeof(struct rx_desc));
	if (!tp->rx_skb)
		goto out1;

	usb_set_intfdata(intf, tp);
	SET_NETDEV_DEV(netdev, &intf->dev);


	if (register_netdev(netdev) != 0) {
		netif_err(tp, probe, netdev, "couldn't register the device");
		goto out2;
	}

	netif_info(tp, probe, netdev, "%s", DRIVER_VERSION);

	return 0;

out2:
	usb_set_intfdata(intf, NULL);
	dev_kfree_skb(tp->rx_skb);
out1:
	free_all_urbs(tp);
out:
	free_netdev(netdev);
	return -EIO;
}

static void rtl8152_unload(struct r8152 *tp)
{
	u32	ocp_data;

	if (tp->version != RTL_VER_01) {
		ocp_data = ocp_read_word(tp, MCU_TYPE_USB, USB_UPS_CTRL);
		ocp_data |= POWER_CUT;
		ocp_write_word(tp, MCU_TYPE_USB, USB_UPS_CTRL, ocp_data);
	}

	ocp_data = ocp_read_word(tp, MCU_TYPE_USB, USB_PM_CTRL_STATUS);
	ocp_data &= ~RWSUME_INDICATE;
	ocp_write_word(tp, MCU_TYPE_USB, USB_PM_CTRL_STATUS, ocp_data);
}

static void rtl8152_disconnect(struct usb_interface *intf)
{
	struct r8152 *tp = usb_get_intfdata(intf);

	usb_set_intfdata(intf, NULL);
	if (tp) {
		set_bit(RTL8152_UNPLUG, &tp->flags);
		tasklet_kill(&tp->tl);
		unregister_netdev(tp->netdev);
		rtl8152_unload(tp);
		free_all_urbs(tp);
		if (tp->rx_skb)
			dev_kfree_skb(tp->rx_skb);
		free_netdev(tp->netdev);
	}
}

/* table of devices that work with this driver */
static struct usb_device_id rtl8152_table[] = {
	{USB_DEVICE(VENDOR_ID_REALTEK, PRODUCT_ID_RTL8152)},
	{}
};

MODULE_DEVICE_TABLE(usb, rtl8152_table);

static struct usb_driver rtl8152_driver = {
	.name =		MODULENAME,
	.probe =	rtl8152_probe,
	.disconnect =	rtl8152_disconnect,
	.id_table =	rtl8152_table,
	.suspend =	rtl8152_suspend,
	.resume =	rtl8152_resume
};

module_usb_driver(rtl8152_driver);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
