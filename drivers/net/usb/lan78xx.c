// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2015 Microchip Technology
 */
#include <linux/version.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/usb.h>
#include <linux/crc32.h>
#include <linux/signal.h>
#include <linux/slab.h>
#include <linux/if_vlan.h>
#include <linux/uaccess.h>
#include <linux/linkmode.h>
#include <linux/list.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/mdio.h>
#include <linux/phy.h>
#include <net/ip6_checksum.h>
#include <linux/interrupt.h>
#include <linux/irqdomain.h>
#include <linux/irq.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/microchipphy.h>
#include <linux/phy_fixed.h>
#include <linux/of_mdio.h>
#include <linux/of_net.h>
#include "lan78xx.h"

#define DRIVER_AUTHOR	"WOOJUNG HUH <woojung.huh@microchip.com>"
#define DRIVER_DESC	"LAN78XX USB 3.0 Gigabit Ethernet Devices"
#define DRIVER_NAME	"lan78xx"

#define TX_TIMEOUT_JIFFIES		(5 * HZ)
#define THROTTLE_JIFFIES		(HZ / 8)
#define UNLINK_TIMEOUT_MS		3

#define RX_MAX_QUEUE_MEMORY		(60 * 1518)

#define SS_USB_PKT_SIZE			(1024)
#define HS_USB_PKT_SIZE			(512)
#define FS_USB_PKT_SIZE			(64)

#define MAX_RX_FIFO_SIZE		(12 * 1024)
#define MAX_TX_FIFO_SIZE		(12 * 1024)
#define DEFAULT_BURST_CAP_SIZE		(MAX_TX_FIFO_SIZE)
#define DEFAULT_BULK_IN_DELAY		(0x0800)
#define MAX_SINGLE_PACKET_SIZE		(9000)
#define DEFAULT_TX_CSUM_ENABLE		(true)
#define DEFAULT_RX_CSUM_ENABLE		(true)
#define DEFAULT_TSO_CSUM_ENABLE		(true)
#define DEFAULT_VLAN_FILTER_ENABLE	(true)
#define DEFAULT_VLAN_RX_OFFLOAD		(true)
#define TX_OVERHEAD			(8)
#define RXW_PADDING			2

#define LAN78XX_USB_VENDOR_ID		(0x0424)
#define LAN7800_USB_PRODUCT_ID		(0x7800)
#define LAN7850_USB_PRODUCT_ID		(0x7850)
#define LAN7801_USB_PRODUCT_ID		(0x7801)
#define LAN78XX_EEPROM_MAGIC		(0x78A5)
#define LAN78XX_OTP_MAGIC		(0x78F3)

#define	MII_READ			1
#define	MII_WRITE			0

#define EEPROM_INDICATOR		(0xA5)
#define EEPROM_MAC_OFFSET		(0x01)
#define MAX_EEPROM_SIZE			512
#define OTP_INDICATOR_1			(0xF3)
#define OTP_INDICATOR_2			(0xF7)

#define WAKE_ALL			(WAKE_PHY | WAKE_UCAST | \
					 WAKE_MCAST | WAKE_BCAST | \
					 WAKE_ARP | WAKE_MAGIC)

/* USB related defines */
#define BULK_IN_PIPE			1
#define BULK_OUT_PIPE			2

/* default autosuspend delay (mSec)*/
#define DEFAULT_AUTOSUSPEND_DELAY	(10 * 1000)

/* statistic update interval (mSec) */
#define STAT_UPDATE_TIMER		(1 * 1000)

/* defines interrupts from interrupt EP */
#define MAX_INT_EP			(32)
#define INT_EP_INTEP			(31)
#define INT_EP_OTP_WR_DONE		(28)
#define INT_EP_EEE_TX_LPI_START		(26)
#define INT_EP_EEE_TX_LPI_STOP		(25)
#define INT_EP_EEE_RX_LPI		(24)
#define INT_EP_MAC_RESET_TIMEOUT	(23)
#define INT_EP_RDFO			(22)
#define INT_EP_TXE			(21)
#define INT_EP_USB_STATUS		(20)
#define INT_EP_TX_DIS			(19)
#define INT_EP_RX_DIS			(18)
#define INT_EP_PHY			(17)
#define INT_EP_DP			(16)
#define INT_EP_MAC_ERR			(15)
#define INT_EP_TDFU			(14)
#define INT_EP_TDFO			(13)
#define INT_EP_UTX			(12)
#define INT_EP_GPIO_11			(11)
#define INT_EP_GPIO_10			(10)
#define INT_EP_GPIO_9			(9)
#define INT_EP_GPIO_8			(8)
#define INT_EP_GPIO_7			(7)
#define INT_EP_GPIO_6			(6)
#define INT_EP_GPIO_5			(5)
#define INT_EP_GPIO_4			(4)
#define INT_EP_GPIO_3			(3)
#define INT_EP_GPIO_2			(2)
#define INT_EP_GPIO_1			(1)
#define INT_EP_GPIO_0			(0)

static const char lan78xx_gstrings[][ETH_GSTRING_LEN] = {
	"RX FCS Errors",
	"RX Alignment Errors",
	"Rx Fragment Errors",
	"RX Jabber Errors",
	"RX Undersize Frame Errors",
	"RX Oversize Frame Errors",
	"RX Dropped Frames",
	"RX Unicast Byte Count",
	"RX Broadcast Byte Count",
	"RX Multicast Byte Count",
	"RX Unicast Frames",
	"RX Broadcast Frames",
	"RX Multicast Frames",
	"RX Pause Frames",
	"RX 64 Byte Frames",
	"RX 65 - 127 Byte Frames",
	"RX 128 - 255 Byte Frames",
	"RX 256 - 511 Bytes Frames",
	"RX 512 - 1023 Byte Frames",
	"RX 1024 - 1518 Byte Frames",
	"RX Greater 1518 Byte Frames",
	"EEE RX LPI Transitions",
	"EEE RX LPI Time",
	"TX FCS Errors",
	"TX Excess Deferral Errors",
	"TX Carrier Errors",
	"TX Bad Byte Count",
	"TX Single Collisions",
	"TX Multiple Collisions",
	"TX Excessive Collision",
	"TX Late Collisions",
	"TX Unicast Byte Count",
	"TX Broadcast Byte Count",
	"TX Multicast Byte Count",
	"TX Unicast Frames",
	"TX Broadcast Frames",
	"TX Multicast Frames",
	"TX Pause Frames",
	"TX 64 Byte Frames",
	"TX 65 - 127 Byte Frames",
	"TX 128 - 255 Byte Frames",
	"TX 256 - 511 Bytes Frames",
	"TX 512 - 1023 Byte Frames",
	"TX 1024 - 1518 Byte Frames",
	"TX Greater 1518 Byte Frames",
	"EEE TX LPI Transitions",
	"EEE TX LPI Time",
};

struct lan78xx_statstage {
	u32 rx_fcs_errors;
	u32 rx_alignment_errors;
	u32 rx_fragment_errors;
	u32 rx_jabber_errors;
	u32 rx_undersize_frame_errors;
	u32 rx_oversize_frame_errors;
	u32 rx_dropped_frames;
	u32 rx_unicast_byte_count;
	u32 rx_broadcast_byte_count;
	u32 rx_multicast_byte_count;
	u32 rx_unicast_frames;
	u32 rx_broadcast_frames;
	u32 rx_multicast_frames;
	u32 rx_pause_frames;
	u32 rx_64_byte_frames;
	u32 rx_65_127_byte_frames;
	u32 rx_128_255_byte_frames;
	u32 rx_256_511_bytes_frames;
	u32 rx_512_1023_byte_frames;
	u32 rx_1024_1518_byte_frames;
	u32 rx_greater_1518_byte_frames;
	u32 eee_rx_lpi_transitions;
	u32 eee_rx_lpi_time;
	u32 tx_fcs_errors;
	u32 tx_excess_deferral_errors;
	u32 tx_carrier_errors;
	u32 tx_bad_byte_count;
	u32 tx_single_collisions;
	u32 tx_multiple_collisions;
	u32 tx_excessive_collision;
	u32 tx_late_collisions;
	u32 tx_unicast_byte_count;
	u32 tx_broadcast_byte_count;
	u32 tx_multicast_byte_count;
	u32 tx_unicast_frames;
	u32 tx_broadcast_frames;
	u32 tx_multicast_frames;
	u32 tx_pause_frames;
	u32 tx_64_byte_frames;
	u32 tx_65_127_byte_frames;
	u32 tx_128_255_byte_frames;
	u32 tx_256_511_bytes_frames;
	u32 tx_512_1023_byte_frames;
	u32 tx_1024_1518_byte_frames;
	u32 tx_greater_1518_byte_frames;
	u32 eee_tx_lpi_transitions;
	u32 eee_tx_lpi_time;
};

struct lan78xx_statstage64 {
	u64 rx_fcs_errors;
	u64 rx_alignment_errors;
	u64 rx_fragment_errors;
	u64 rx_jabber_errors;
	u64 rx_undersize_frame_errors;
	u64 rx_oversize_frame_errors;
	u64 rx_dropped_frames;
	u64 rx_unicast_byte_count;
	u64 rx_broadcast_byte_count;
	u64 rx_multicast_byte_count;
	u64 rx_unicast_frames;
	u64 rx_broadcast_frames;
	u64 rx_multicast_frames;
	u64 rx_pause_frames;
	u64 rx_64_byte_frames;
	u64 rx_65_127_byte_frames;
	u64 rx_128_255_byte_frames;
	u64 rx_256_511_bytes_frames;
	u64 rx_512_1023_byte_frames;
	u64 rx_1024_1518_byte_frames;
	u64 rx_greater_1518_byte_frames;
	u64 eee_rx_lpi_transitions;
	u64 eee_rx_lpi_time;
	u64 tx_fcs_errors;
	u64 tx_excess_deferral_errors;
	u64 tx_carrier_errors;
	u64 tx_bad_byte_count;
	u64 tx_single_collisions;
	u64 tx_multiple_collisions;
	u64 tx_excessive_collision;
	u64 tx_late_collisions;
	u64 tx_unicast_byte_count;
	u64 tx_broadcast_byte_count;
	u64 tx_multicast_byte_count;
	u64 tx_unicast_frames;
	u64 tx_broadcast_frames;
	u64 tx_multicast_frames;
	u64 tx_pause_frames;
	u64 tx_64_byte_frames;
	u64 tx_65_127_byte_frames;
	u64 tx_128_255_byte_frames;
	u64 tx_256_511_bytes_frames;
	u64 tx_512_1023_byte_frames;
	u64 tx_1024_1518_byte_frames;
	u64 tx_greater_1518_byte_frames;
	u64 eee_tx_lpi_transitions;
	u64 eee_tx_lpi_time;
};

static u32 lan78xx_regs[] = {
	ID_REV,
	INT_STS,
	HW_CFG,
	PMT_CTL,
	E2P_CMD,
	E2P_DATA,
	USB_STATUS,
	VLAN_TYPE,
	MAC_CR,
	MAC_RX,
	MAC_TX,
	FLOW,
	ERR_STS,
	MII_ACC,
	MII_DATA,
	EEE_TX_LPI_REQ_DLY,
	EEE_TW_TX_SYS,
	EEE_TX_LPI_REM_DLY,
	WUCSR
};

#define PHY_REG_SIZE (32 * sizeof(u32))

struct lan78xx_net;

struct lan78xx_priv {
	struct lan78xx_net *dev;
	u32 rfe_ctl;
	u32 mchash_table[DP_SEL_VHF_HASH_LEN]; /* multicat hash table */
	u32 pfilter_table[NUM_OF_MAF][2]; /* perfect filter table */
	u32 vlan_table[DP_SEL_VHF_VLAN_LEN];
	struct mutex dataport_mutex; /* for dataport access */
	spinlock_t rfe_ctl_lock; /* for rfe register access */
	struct work_struct set_multicast;
	struct work_struct set_vlan;
	u32 wol;
};

enum skb_state {
	illegal = 0,
	tx_start,
	tx_done,
	rx_start,
	rx_done,
	rx_cleanup,
	unlink_start
};

struct skb_data {		/* skb->cb is one of these */
	struct urb *urb;
	struct lan78xx_net *dev;
	enum skb_state state;
	size_t length;
	int num_of_packet;
};

struct usb_context {
	struct usb_ctrlrequest req;
	struct lan78xx_net *dev;
};

#define EVENT_TX_HALT			0
#define EVENT_RX_HALT			1
#define EVENT_RX_MEMORY			2
#define EVENT_STS_SPLIT			3
#define EVENT_LINK_RESET		4
#define EVENT_RX_PAUSED			5
#define EVENT_DEV_WAKING		6
#define EVENT_DEV_ASLEEP		7
#define EVENT_DEV_OPEN			8
#define EVENT_STAT_UPDATE		9

struct statstage {
	struct mutex			access_lock;	/* for stats access */
	struct lan78xx_statstage	saved;
	struct lan78xx_statstage	rollover_count;
	struct lan78xx_statstage	rollover_max;
	struct lan78xx_statstage64	curr_stat;
};

struct irq_domain_data {
	struct irq_domain	*irqdomain;
	unsigned int		phyirq;
	struct irq_chip		*irqchip;
	irq_flow_handler_t	irq_handler;
	u32			irqenable;
	struct mutex		irq_lock;		/* for irq bus access */
};

struct lan78xx_net {
	struct net_device	*net;
	struct usb_device	*udev;
	struct usb_interface	*intf;
	void			*driver_priv;

	int			rx_qlen;
	int			tx_qlen;
	struct sk_buff_head	rxq;
	struct sk_buff_head	txq;
	struct sk_buff_head	done;
	struct sk_buff_head	rxq_pause;
	struct sk_buff_head	txq_pend;

	struct tasklet_struct	bh;
	struct delayed_work	wq;

	struct usb_host_endpoint *ep_blkin;
	struct usb_host_endpoint *ep_blkout;
	struct usb_host_endpoint *ep_intr;

	int			msg_enable;

	struct urb		*urb_intr;
	struct usb_anchor	deferred;

	struct mutex		phy_mutex; /* for phy access */
	unsigned		pipe_in, pipe_out, pipe_intr;

	u32			hard_mtu;	/* count any extra framing */
	size_t			rx_urb_size;	/* size for rx urbs */

	unsigned long		flags;

	wait_queue_head_t	*wait;
	unsigned char		suspend_count;

	unsigned		maxpacket;
	struct timer_list	delay;
	struct timer_list	stat_monitor;

	unsigned long		data[5];

	int			link_on;
	u8			mdix_ctrl;

	u32			chipid;
	u32			chiprev;
	struct mii_bus		*mdiobus;
	phy_interface_t		interface;

	int			fc_autoneg;
	u8			fc_request_control;

	int			delta;
	struct statstage	stats;

	struct irq_domain_data	domain_data;
};

/* define external phy id */
#define	PHY_LAN8835			(0x0007C130)
#define	PHY_KSZ9031RNX			(0x00221620)

/* use ethtool to change the level for any given device */
static int msg_level = -1;
module_param(msg_level, int, 0);
MODULE_PARM_DESC(msg_level, "Override default message level");

static int lan78xx_read_reg(struct lan78xx_net *dev, u32 index, u32 *data)
{
	u32 *buf = kmalloc(sizeof(u32), GFP_KERNEL);
	int ret;

	if (!buf)
		return -ENOMEM;

	ret = usb_control_msg(dev->udev, usb_rcvctrlpipe(dev->udev, 0),
			      USB_VENDOR_REQUEST_READ_REGISTER,
			      USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			      0, index, buf, 4, USB_CTRL_GET_TIMEOUT);
	if (likely(ret >= 0)) {
		le32_to_cpus(buf);
		*data = *buf;
	} else {
		netdev_warn(dev->net,
			    "Failed to read register index 0x%08x. ret = %d",
			    index, ret);
	}

	kfree(buf);

	return ret;
}

static int lan78xx_write_reg(struct lan78xx_net *dev, u32 index, u32 data)
{
	u32 *buf = kmalloc(sizeof(u32), GFP_KERNEL);
	int ret;

	if (!buf)
		return -ENOMEM;

	*buf = data;
	cpu_to_le32s(buf);

	ret = usb_control_msg(dev->udev, usb_sndctrlpipe(dev->udev, 0),
			      USB_VENDOR_REQUEST_WRITE_REGISTER,
			      USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			      0, index, buf, 4, USB_CTRL_SET_TIMEOUT);
	if (unlikely(ret < 0)) {
		netdev_warn(dev->net,
			    "Failed to write register index 0x%08x. ret = %d",
			    index, ret);
	}

	kfree(buf);

	return ret;
}

static int lan78xx_read_stats(struct lan78xx_net *dev,
			      struct lan78xx_statstage *data)
{
	int ret = 0;
	int i;
	struct lan78xx_statstage *stats;
	u32 *src;
	u32 *dst;

	stats = kmalloc(sizeof(*stats), GFP_KERNEL);
	if (!stats)
		return -ENOMEM;

	ret = usb_control_msg(dev->udev,
			      usb_rcvctrlpipe(dev->udev, 0),
			      USB_VENDOR_REQUEST_GET_STATS,
			      USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			      0,
			      0,
			      (void *)stats,
			      sizeof(*stats),
			      USB_CTRL_SET_TIMEOUT);
	if (likely(ret >= 0)) {
		src = (u32 *)stats;
		dst = (u32 *)data;
		for (i = 0; i < sizeof(*stats)/sizeof(u32); i++) {
			le32_to_cpus(&src[i]);
			dst[i] = src[i];
		}
	} else {
		netdev_warn(dev->net,
			    "Failed to read stat ret = 0x%x", ret);
	}

	kfree(stats);

	return ret;
}

#define check_counter_rollover(struct1, dev_stats, member) {	\
	if (struct1->member < dev_stats.saved.member)		\
		dev_stats.rollover_count.member++;		\
	}

static void lan78xx_check_stat_rollover(struct lan78xx_net *dev,
					struct lan78xx_statstage *stats)
{
	check_counter_rollover(stats, dev->stats, rx_fcs_errors);
	check_counter_rollover(stats, dev->stats, rx_alignment_errors);
	check_counter_rollover(stats, dev->stats, rx_fragment_errors);
	check_counter_rollover(stats, dev->stats, rx_jabber_errors);
	check_counter_rollover(stats, dev->stats, rx_undersize_frame_errors);
	check_counter_rollover(stats, dev->stats, rx_oversize_frame_errors);
	check_counter_rollover(stats, dev->stats, rx_dropped_frames);
	check_counter_rollover(stats, dev->stats, rx_unicast_byte_count);
	check_counter_rollover(stats, dev->stats, rx_broadcast_byte_count);
	check_counter_rollover(stats, dev->stats, rx_multicast_byte_count);
	check_counter_rollover(stats, dev->stats, rx_unicast_frames);
	check_counter_rollover(stats, dev->stats, rx_broadcast_frames);
	check_counter_rollover(stats, dev->stats, rx_multicast_frames);
	check_counter_rollover(stats, dev->stats, rx_pause_frames);
	check_counter_rollover(stats, dev->stats, rx_64_byte_frames);
	check_counter_rollover(stats, dev->stats, rx_65_127_byte_frames);
	check_counter_rollover(stats, dev->stats, rx_128_255_byte_frames);
	check_counter_rollover(stats, dev->stats, rx_256_511_bytes_frames);
	check_counter_rollover(stats, dev->stats, rx_512_1023_byte_frames);
	check_counter_rollover(stats, dev->stats, rx_1024_1518_byte_frames);
	check_counter_rollover(stats, dev->stats, rx_greater_1518_byte_frames);
	check_counter_rollover(stats, dev->stats, eee_rx_lpi_transitions);
	check_counter_rollover(stats, dev->stats, eee_rx_lpi_time);
	check_counter_rollover(stats, dev->stats, tx_fcs_errors);
	check_counter_rollover(stats, dev->stats, tx_excess_deferral_errors);
	check_counter_rollover(stats, dev->stats, tx_carrier_errors);
	check_counter_rollover(stats, dev->stats, tx_bad_byte_count);
	check_counter_rollover(stats, dev->stats, tx_single_collisions);
	check_counter_rollover(stats, dev->stats, tx_multiple_collisions);
	check_counter_rollover(stats, dev->stats, tx_excessive_collision);
	check_counter_rollover(stats, dev->stats, tx_late_collisions);
	check_counter_rollover(stats, dev->stats, tx_unicast_byte_count);
	check_counter_rollover(stats, dev->stats, tx_broadcast_byte_count);
	check_counter_rollover(stats, dev->stats, tx_multicast_byte_count);
	check_counter_rollover(stats, dev->stats, tx_unicast_frames);
	check_counter_rollover(stats, dev->stats, tx_broadcast_frames);
	check_counter_rollover(stats, dev->stats, tx_multicast_frames);
	check_counter_rollover(stats, dev->stats, tx_pause_frames);
	check_counter_rollover(stats, dev->stats, tx_64_byte_frames);
	check_counter_rollover(stats, dev->stats, tx_65_127_byte_frames);
	check_counter_rollover(stats, dev->stats, tx_128_255_byte_frames);
	check_counter_rollover(stats, dev->stats, tx_256_511_bytes_frames);
	check_counter_rollover(stats, dev->stats, tx_512_1023_byte_frames);
	check_counter_rollover(stats, dev->stats, tx_1024_1518_byte_frames);
	check_counter_rollover(stats, dev->stats, tx_greater_1518_byte_frames);
	check_counter_rollover(stats, dev->stats, eee_tx_lpi_transitions);
	check_counter_rollover(stats, dev->stats, eee_tx_lpi_time);

	memcpy(&dev->stats.saved, stats, sizeof(struct lan78xx_statstage));
}

static void lan78xx_update_stats(struct lan78xx_net *dev)
{
	u32 *p, *count, *max;
	u64 *data;
	int i;
	struct lan78xx_statstage lan78xx_stats;

	if (usb_autopm_get_interface(dev->intf) < 0)
		return;

	p = (u32 *)&lan78xx_stats;
	count = (u32 *)&dev->stats.rollover_count;
	max = (u32 *)&dev->stats.rollover_max;
	data = (u64 *)&dev->stats.curr_stat;

	mutex_lock(&dev->stats.access_lock);

	if (lan78xx_read_stats(dev, &lan78xx_stats) > 0)
		lan78xx_check_stat_rollover(dev, &lan78xx_stats);

	for (i = 0; i < (sizeof(lan78xx_stats) / (sizeof(u32))); i++)
		data[i] = (u64)p[i] + ((u64)count[i] * ((u64)max[i] + 1));

	mutex_unlock(&dev->stats.access_lock);

	usb_autopm_put_interface(dev->intf);
}

/* Loop until the read is completed with timeout called with phy_mutex held */
static int lan78xx_phy_wait_not_busy(struct lan78xx_net *dev)
{
	unsigned long start_time = jiffies;
	u32 val;
	int ret;

	do {
		ret = lan78xx_read_reg(dev, MII_ACC, &val);
		if (unlikely(ret < 0))
			return -EIO;

		if (!(val & MII_ACC_MII_BUSY_))
			return 0;
	} while (!time_after(jiffies, start_time + HZ));

	return -EIO;
}

static inline u32 mii_access(int id, int index, int read)
{
	u32 ret;

	ret = ((u32)id << MII_ACC_PHY_ADDR_SHIFT_) & MII_ACC_PHY_ADDR_MASK_;
	ret |= ((u32)index << MII_ACC_MIIRINDA_SHIFT_) & MII_ACC_MIIRINDA_MASK_;
	if (read)
		ret |= MII_ACC_MII_READ_;
	else
		ret |= MII_ACC_MII_WRITE_;
	ret |= MII_ACC_MII_BUSY_;

	return ret;
}

static int lan78xx_wait_eeprom(struct lan78xx_net *dev)
{
	unsigned long start_time = jiffies;
	u32 val;
	int ret;

	do {
		ret = lan78xx_read_reg(dev, E2P_CMD, &val);
		if (unlikely(ret < 0))
			return -EIO;

		if (!(val & E2P_CMD_EPC_BUSY_) ||
		    (val & E2P_CMD_EPC_TIMEOUT_))
			break;
		usleep_range(40, 100);
	} while (!time_after(jiffies, start_time + HZ));

	if (val & (E2P_CMD_EPC_TIMEOUT_ | E2P_CMD_EPC_BUSY_)) {
		netdev_warn(dev->net, "EEPROM read operation timeout");
		return -EIO;
	}

	return 0;
}

static int lan78xx_eeprom_confirm_not_busy(struct lan78xx_net *dev)
{
	unsigned long start_time = jiffies;
	u32 val;
	int ret;

	do {
		ret = lan78xx_read_reg(dev, E2P_CMD, &val);
		if (unlikely(ret < 0))
			return -EIO;

		if (!(val & E2P_CMD_EPC_BUSY_))
			return 0;

		usleep_range(40, 100);
	} while (!time_after(jiffies, start_time + HZ));

	netdev_warn(dev->net, "EEPROM is busy");
	return -EIO;
}

static int lan78xx_read_raw_eeprom(struct lan78xx_net *dev, u32 offset,
				   u32 length, u8 *data)
{
	u32 val;
	u32 saved;
	int i, ret;
	int retval;

	/* depends on chip, some EEPROM pins are muxed with LED function.
	 * disable & restore LED function to access EEPROM.
	 */
	ret = lan78xx_read_reg(dev, HW_CFG, &val);
	saved = val;
	if (dev->chipid == ID_REV_CHIP_ID_7800_) {
		val &= ~(HW_CFG_LED1_EN_ | HW_CFG_LED0_EN_);
		ret = lan78xx_write_reg(dev, HW_CFG, val);
	}

	retval = lan78xx_eeprom_confirm_not_busy(dev);
	if (retval)
		return retval;

	for (i = 0; i < length; i++) {
		val = E2P_CMD_EPC_BUSY_ | E2P_CMD_EPC_CMD_READ_;
		val |= (offset & E2P_CMD_EPC_ADDR_MASK_);
		ret = lan78xx_write_reg(dev, E2P_CMD, val);
		if (unlikely(ret < 0)) {
			retval = -EIO;
			goto exit;
		}

		retval = lan78xx_wait_eeprom(dev);
		if (retval < 0)
			goto exit;

		ret = lan78xx_read_reg(dev, E2P_DATA, &val);
		if (unlikely(ret < 0)) {
			retval = -EIO;
			goto exit;
		}

		data[i] = val & 0xFF;
		offset++;
	}

	retval = 0;
exit:
	if (dev->chipid == ID_REV_CHIP_ID_7800_)
		ret = lan78xx_write_reg(dev, HW_CFG, saved);

	return retval;
}

static int lan78xx_read_eeprom(struct lan78xx_net *dev, u32 offset,
			       u32 length, u8 *data)
{
	u8 sig;
	int ret;

	ret = lan78xx_read_raw_eeprom(dev, 0, 1, &sig);
	if ((ret == 0) && (sig == EEPROM_INDICATOR))
		ret = lan78xx_read_raw_eeprom(dev, offset, length, data);
	else
		ret = -EINVAL;

	return ret;
}

static int lan78xx_write_raw_eeprom(struct lan78xx_net *dev, u32 offset,
				    u32 length, u8 *data)
{
	u32 val;
	u32 saved;
	int i, ret;
	int retval;

	/* depends on chip, some EEPROM pins are muxed with LED function.
	 * disable & restore LED function to access EEPROM.
	 */
	ret = lan78xx_read_reg(dev, HW_CFG, &val);
	saved = val;
	if (dev->chipid == ID_REV_CHIP_ID_7800_) {
		val &= ~(HW_CFG_LED1_EN_ | HW_CFG_LED0_EN_);
		ret = lan78xx_write_reg(dev, HW_CFG, val);
	}

	retval = lan78xx_eeprom_confirm_not_busy(dev);
	if (retval)
		goto exit;

	/* Issue write/erase enable command */
	val = E2P_CMD_EPC_BUSY_ | E2P_CMD_EPC_CMD_EWEN_;
	ret = lan78xx_write_reg(dev, E2P_CMD, val);
	if (unlikely(ret < 0)) {
		retval = -EIO;
		goto exit;
	}

	retval = lan78xx_wait_eeprom(dev);
	if (retval < 0)
		goto exit;

	for (i = 0; i < length; i++) {
		/* Fill data register */
		val = data[i];
		ret = lan78xx_write_reg(dev, E2P_DATA, val);
		if (ret < 0) {
			retval = -EIO;
			goto exit;
		}

		/* Send "write" command */
		val = E2P_CMD_EPC_BUSY_ | E2P_CMD_EPC_CMD_WRITE_;
		val |= (offset & E2P_CMD_EPC_ADDR_MASK_);
		ret = lan78xx_write_reg(dev, E2P_CMD, val);
		if (ret < 0) {
			retval = -EIO;
			goto exit;
		}

		retval = lan78xx_wait_eeprom(dev);
		if (retval < 0)
			goto exit;

		offset++;
	}

	retval = 0;
exit:
	if (dev->chipid == ID_REV_CHIP_ID_7800_)
		ret = lan78xx_write_reg(dev, HW_CFG, saved);

	return retval;
}

static int lan78xx_read_raw_otp(struct lan78xx_net *dev, u32 offset,
				u32 length, u8 *data)
{
	int i;
	int ret;
	u32 buf;
	unsigned long timeout;

	ret = lan78xx_read_reg(dev, OTP_PWR_DN, &buf);

	if (buf & OTP_PWR_DN_PWRDN_N_) {
		/* clear it and wait to be cleared */
		ret = lan78xx_write_reg(dev, OTP_PWR_DN, 0);

		timeout = jiffies + HZ;
		do {
			usleep_range(1, 10);
			ret = lan78xx_read_reg(dev, OTP_PWR_DN, &buf);
			if (time_after(jiffies, timeout)) {
				netdev_warn(dev->net,
					    "timeout on OTP_PWR_DN");
				return -EIO;
			}
		} while (buf & OTP_PWR_DN_PWRDN_N_);
	}

	for (i = 0; i < length; i++) {
		ret = lan78xx_write_reg(dev, OTP_ADDR1,
					((offset + i) >> 8) & OTP_ADDR1_15_11);
		ret = lan78xx_write_reg(dev, OTP_ADDR2,
					((offset + i) & OTP_ADDR2_10_3));

		ret = lan78xx_write_reg(dev, OTP_FUNC_CMD, OTP_FUNC_CMD_READ_);
		ret = lan78xx_write_reg(dev, OTP_CMD_GO, OTP_CMD_GO_GO_);

		timeout = jiffies + HZ;
		do {
			udelay(1);
			ret = lan78xx_read_reg(dev, OTP_STATUS, &buf);
			if (time_after(jiffies, timeout)) {
				netdev_warn(dev->net,
					    "timeout on OTP_STATUS");
				return -EIO;
			}
		} while (buf & OTP_STATUS_BUSY_);

		ret = lan78xx_read_reg(dev, OTP_RD_DATA, &buf);

		data[i] = (u8)(buf & 0xFF);
	}

	return 0;
}

static int lan78xx_write_raw_otp(struct lan78xx_net *dev, u32 offset,
				 u32 length, u8 *data)
{
	int i;
	int ret;
	u32 buf;
	unsigned long timeout;

	ret = lan78xx_read_reg(dev, OTP_PWR_DN, &buf);

	if (buf & OTP_PWR_DN_PWRDN_N_) {
		/* clear it and wait to be cleared */
		ret = lan78xx_write_reg(dev, OTP_PWR_DN, 0);

		timeout = jiffies + HZ;
		do {
			udelay(1);
			ret = lan78xx_read_reg(dev, OTP_PWR_DN, &buf);
			if (time_after(jiffies, timeout)) {
				netdev_warn(dev->net,
					    "timeout on OTP_PWR_DN completion");
				return -EIO;
			}
		} while (buf & OTP_PWR_DN_PWRDN_N_);
	}

	/* set to BYTE program mode */
	ret = lan78xx_write_reg(dev, OTP_PRGM_MODE, OTP_PRGM_MODE_BYTE_);

	for (i = 0; i < length; i++) {
		ret = lan78xx_write_reg(dev, OTP_ADDR1,
					((offset + i) >> 8) & OTP_ADDR1_15_11);
		ret = lan78xx_write_reg(dev, OTP_ADDR2,
					((offset + i) & OTP_ADDR2_10_3));
		ret = lan78xx_write_reg(dev, OTP_PRGM_DATA, data[i]);
		ret = lan78xx_write_reg(dev, OTP_TST_CMD, OTP_TST_CMD_PRGVRFY_);
		ret = lan78xx_write_reg(dev, OTP_CMD_GO, OTP_CMD_GO_GO_);

		timeout = jiffies + HZ;
		do {
			udelay(1);
			ret = lan78xx_read_reg(dev, OTP_STATUS, &buf);
			if (time_after(jiffies, timeout)) {
				netdev_warn(dev->net,
					    "Timeout on OTP_STATUS completion");
				return -EIO;
			}
		} while (buf & OTP_STATUS_BUSY_);
	}

	return 0;
}

static int lan78xx_read_otp(struct lan78xx_net *dev, u32 offset,
			    u32 length, u8 *data)
{
	u8 sig;
	int ret;

	ret = lan78xx_read_raw_otp(dev, 0, 1, &sig);

	if (ret == 0) {
		if (sig == OTP_INDICATOR_2)
			offset += 0x100;
		else if (sig != OTP_INDICATOR_1)
			ret = -EINVAL;
		if (!ret)
			ret = lan78xx_read_raw_otp(dev, offset, length, data);
	}

	return ret;
}

static int lan78xx_dataport_wait_not_busy(struct lan78xx_net *dev)
{
	int i, ret;

	for (i = 0; i < 100; i++) {
		u32 dp_sel;

		ret = lan78xx_read_reg(dev, DP_SEL, &dp_sel);
		if (unlikely(ret < 0))
			return -EIO;

		if (dp_sel & DP_SEL_DPRDY_)
			return 0;

		usleep_range(40, 100);
	}

	netdev_warn(dev->net, "lan78xx_dataport_wait_not_busy timed out");

	return -EIO;
}

static int lan78xx_dataport_write(struct lan78xx_net *dev, u32 ram_select,
				  u32 addr, u32 length, u32 *buf)
{
	struct lan78xx_priv *pdata = (struct lan78xx_priv *)(dev->data[0]);
	u32 dp_sel;
	int i, ret;

	if (usb_autopm_get_interface(dev->intf) < 0)
			return 0;

	mutex_lock(&pdata->dataport_mutex);

	ret = lan78xx_dataport_wait_not_busy(dev);
	if (ret < 0)
		goto done;

	ret = lan78xx_read_reg(dev, DP_SEL, &dp_sel);

	dp_sel &= ~DP_SEL_RSEL_MASK_;
	dp_sel |= ram_select;
	ret = lan78xx_write_reg(dev, DP_SEL, dp_sel);

	for (i = 0; i < length; i++) {
		ret = lan78xx_write_reg(dev, DP_ADDR, addr + i);

		ret = lan78xx_write_reg(dev, DP_DATA, buf[i]);

		ret = lan78xx_write_reg(dev, DP_CMD, DP_CMD_WRITE_);

		ret = lan78xx_dataport_wait_not_busy(dev);
		if (ret < 0)
			goto done;
	}

done:
	mutex_unlock(&pdata->dataport_mutex);
	usb_autopm_put_interface(dev->intf);

	return ret;
}

static void lan78xx_set_addr_filter(struct lan78xx_priv *pdata,
				    int index, u8 addr[ETH_ALEN])
{
	u32 temp;

	if ((pdata) && (index > 0) && (index < NUM_OF_MAF)) {
		temp = addr[3];
		temp = addr[2] | (temp << 8);
		temp = addr[1] | (temp << 8);
		temp = addr[0] | (temp << 8);
		pdata->pfilter_table[index][1] = temp;
		temp = addr[5];
		temp = addr[4] | (temp << 8);
		temp |= MAF_HI_VALID_ | MAF_HI_TYPE_DST_;
		pdata->pfilter_table[index][0] = temp;
	}
}

/* returns hash bit number for given MAC address */
static inline u32 lan78xx_hash(char addr[ETH_ALEN])
{
	return (ether_crc(ETH_ALEN, addr) >> 23) & 0x1ff;
}

static void lan78xx_deferred_multicast_write(struct work_struct *param)
{
	struct lan78xx_priv *pdata =
			container_of(param, struct lan78xx_priv, set_multicast);
	struct lan78xx_net *dev = pdata->dev;
	int i;
	int ret;

	netif_dbg(dev, drv, dev->net, "deferred multicast write 0x%08x\n",
		  pdata->rfe_ctl);

	lan78xx_dataport_write(dev, DP_SEL_RSEL_VLAN_DA_, DP_SEL_VHF_VLAN_LEN,
			       DP_SEL_VHF_HASH_LEN, pdata->mchash_table);

	for (i = 1; i < NUM_OF_MAF; i++) {
		ret = lan78xx_write_reg(dev, MAF_HI(i), 0);
		ret = lan78xx_write_reg(dev, MAF_LO(i),
					pdata->pfilter_table[i][1]);
		ret = lan78xx_write_reg(dev, MAF_HI(i),
					pdata->pfilter_table[i][0]);
	}

	ret = lan78xx_write_reg(dev, RFE_CTL, pdata->rfe_ctl);
}

static void lan78xx_set_multicast(struct net_device *netdev)
{
	struct lan78xx_net *dev = netdev_priv(netdev);
	struct lan78xx_priv *pdata = (struct lan78xx_priv *)(dev->data[0]);
	unsigned long flags;
	int i;

	spin_lock_irqsave(&pdata->rfe_ctl_lock, flags);

	pdata->rfe_ctl &= ~(RFE_CTL_UCAST_EN_ | RFE_CTL_MCAST_EN_ |
			    RFE_CTL_DA_PERFECT_ | RFE_CTL_MCAST_HASH_);

	for (i = 0; i < DP_SEL_VHF_HASH_LEN; i++)
			pdata->mchash_table[i] = 0;
	/* pfilter_table[0] has own HW address */
	for (i = 1; i < NUM_OF_MAF; i++) {
			pdata->pfilter_table[i][0] =
			pdata->pfilter_table[i][1] = 0;
	}

	pdata->rfe_ctl |= RFE_CTL_BCAST_EN_;

	if (dev->net->flags & IFF_PROMISC) {
		netif_dbg(dev, drv, dev->net, "promiscuous mode enabled");
		pdata->rfe_ctl |= RFE_CTL_MCAST_EN_ | RFE_CTL_UCAST_EN_;
	} else {
		if (dev->net->flags & IFF_ALLMULTI) {
			netif_dbg(dev, drv, dev->net,
				  "receive all multicast enabled");
			pdata->rfe_ctl |= RFE_CTL_MCAST_EN_;
		}
	}

	if (netdev_mc_count(dev->net)) {
		struct netdev_hw_addr *ha;
		int i;

		netif_dbg(dev, drv, dev->net, "receive multicast hash filter");

		pdata->rfe_ctl |= RFE_CTL_DA_PERFECT_;

		i = 1;
		netdev_for_each_mc_addr(ha, netdev) {
			/* set first 32 into Perfect Filter */
			if (i < 33) {
				lan78xx_set_addr_filter(pdata, i, ha->addr);
			} else {
				u32 bitnum = lan78xx_hash(ha->addr);

				pdata->mchash_table[bitnum / 32] |=
							(1 << (bitnum % 32));
				pdata->rfe_ctl |= RFE_CTL_MCAST_HASH_;
			}
			i++;
		}
	}

	spin_unlock_irqrestore(&pdata->rfe_ctl_lock, flags);

	/* defer register writes to a sleepable context */
	schedule_work(&pdata->set_multicast);
}

static int lan78xx_update_flowcontrol(struct lan78xx_net *dev, u8 duplex,
				      u16 lcladv, u16 rmtadv)
{
	u32 flow = 0, fct_flow = 0;
	int ret;
	u8 cap;

	if (dev->fc_autoneg)
		cap = mii_resolve_flowctrl_fdx(lcladv, rmtadv);
	else
		cap = dev->fc_request_control;

	if (cap & FLOW_CTRL_TX)
		flow |= (FLOW_CR_TX_FCEN_ | 0xFFFF);

	if (cap & FLOW_CTRL_RX)
		flow |= FLOW_CR_RX_FCEN_;

	if (dev->udev->speed == USB_SPEED_SUPER)
		fct_flow = 0x817;
	else if (dev->udev->speed == USB_SPEED_HIGH)
		fct_flow = 0x211;

	netif_dbg(dev, link, dev->net, "rx pause %s, tx pause %s",
		  (cap & FLOW_CTRL_RX ? "enabled" : "disabled"),
		  (cap & FLOW_CTRL_TX ? "enabled" : "disabled"));

	ret = lan78xx_write_reg(dev, FCT_FLOW, fct_flow);

	/* threshold value should be set before enabling flow */
	ret = lan78xx_write_reg(dev, FLOW, flow);

	return 0;
}

static int lan78xx_link_reset(struct lan78xx_net *dev)
{
	struct phy_device *phydev = dev->net->phydev;
	struct ethtool_link_ksettings ecmd;
	int ladv, radv, ret;
	u32 buf;

	/* clear LAN78xx interrupt status */
	ret = lan78xx_write_reg(dev, INT_STS, INT_STS_PHY_INT_);
	if (unlikely(ret < 0))
		return -EIO;

	phy_read_status(phydev);

	if (!phydev->link && dev->link_on) {
		dev->link_on = false;

		/* reset MAC */
		ret = lan78xx_read_reg(dev, MAC_CR, &buf);
		if (unlikely(ret < 0))
			return -EIO;
		buf |= MAC_CR_RST_;
		ret = lan78xx_write_reg(dev, MAC_CR, buf);
		if (unlikely(ret < 0))
			return -EIO;

		del_timer(&dev->stat_monitor);
	} else if (phydev->link && !dev->link_on) {
		dev->link_on = true;

		phy_ethtool_ksettings_get(phydev, &ecmd);

		if (dev->udev->speed == USB_SPEED_SUPER) {
			if (ecmd.base.speed == 1000) {
				/* disable U2 */
				ret = lan78xx_read_reg(dev, USB_CFG1, &buf);
				buf &= ~USB_CFG1_DEV_U2_INIT_EN_;
				ret = lan78xx_write_reg(dev, USB_CFG1, buf);
				/* enable U1 */
				ret = lan78xx_read_reg(dev, USB_CFG1, &buf);
				buf |= USB_CFG1_DEV_U1_INIT_EN_;
				ret = lan78xx_write_reg(dev, USB_CFG1, buf);
			} else {
				/* enable U1 & U2 */
				ret = lan78xx_read_reg(dev, USB_CFG1, &buf);
				buf |= USB_CFG1_DEV_U2_INIT_EN_;
				buf |= USB_CFG1_DEV_U1_INIT_EN_;
				ret = lan78xx_write_reg(dev, USB_CFG1, buf);
			}
		}

		ladv = phy_read(phydev, MII_ADVERTISE);
		if (ladv < 0)
			return ladv;

		radv = phy_read(phydev, MII_LPA);
		if (radv < 0)
			return radv;

		netif_dbg(dev, link, dev->net,
			  "speed: %u duplex: %d anadv: 0x%04x anlpa: 0x%04x",
			  ecmd.base.speed, ecmd.base.duplex, ladv, radv);

		ret = lan78xx_update_flowcontrol(dev, ecmd.base.duplex, ladv,
						 radv);

		if (!timer_pending(&dev->stat_monitor)) {
			dev->delta = 1;
			mod_timer(&dev->stat_monitor,
				  jiffies + STAT_UPDATE_TIMER);
		}

		tasklet_schedule(&dev->bh);
	}

	return ret;
}

/* some work can't be done in tasklets, so we use keventd
 *
 * NOTE:  annoying asymmetry:  if it's active, schedule_work() fails,
 * but tasklet_schedule() doesn't.	hope the failure is rare.
 */
static void lan78xx_defer_kevent(struct lan78xx_net *dev, int work)
{
	set_bit(work, &dev->flags);
	if (!schedule_delayed_work(&dev->wq, 0))
		netdev_err(dev->net, "kevent %d may have been dropped\n", work);
}

static void lan78xx_status(struct lan78xx_net *dev, struct urb *urb)
{
	u32 intdata;

	if (urb->actual_length != 4) {
		netdev_warn(dev->net,
			    "unexpected urb length %d", urb->actual_length);
		return;
	}

	intdata = get_unaligned_le32(urb->transfer_buffer);

	if (intdata & INT_ENP_PHY_INT) {
		netif_dbg(dev, link, dev->net, "PHY INTR: 0x%08x\n", intdata);
		lan78xx_defer_kevent(dev, EVENT_LINK_RESET);

		if (dev->domain_data.phyirq > 0)
			generic_handle_irq(dev->domain_data.phyirq);
	} else
		netdev_warn(dev->net,
			    "unexpected interrupt: 0x%08x\n", intdata);
}

static int lan78xx_ethtool_get_eeprom_len(struct net_device *netdev)
{
	return MAX_EEPROM_SIZE;
}

static int lan78xx_ethtool_get_eeprom(struct net_device *netdev,
				      struct ethtool_eeprom *ee, u8 *data)
{
	struct lan78xx_net *dev = netdev_priv(netdev);
	int ret;

	ret = usb_autopm_get_interface(dev->intf);
	if (ret)
		return ret;

	ee->magic = LAN78XX_EEPROM_MAGIC;

	ret = lan78xx_read_raw_eeprom(dev, ee->offset, ee->len, data);

	usb_autopm_put_interface(dev->intf);

	return ret;
}

static int lan78xx_ethtool_set_eeprom(struct net_device *netdev,
				      struct ethtool_eeprom *ee, u8 *data)
{
	struct lan78xx_net *dev = netdev_priv(netdev);
	int ret;

	ret = usb_autopm_get_interface(dev->intf);
	if (ret)
		return ret;

	/* Invalid EEPROM_INDICATOR at offset zero will result in a failure
	 * to load data from EEPROM
	 */
	if (ee->magic == LAN78XX_EEPROM_MAGIC)
		ret = lan78xx_write_raw_eeprom(dev, ee->offset, ee->len, data);
	else if ((ee->magic == LAN78XX_OTP_MAGIC) &&
		 (ee->offset == 0) &&
		 (ee->len == 512) &&
		 (data[0] == OTP_INDICATOR_1))
		ret = lan78xx_write_raw_otp(dev, ee->offset, ee->len, data);

	usb_autopm_put_interface(dev->intf);

	return ret;
}

static void lan78xx_get_strings(struct net_device *netdev, u32 stringset,
				u8 *data)
{
	if (stringset == ETH_SS_STATS)
		memcpy(data, lan78xx_gstrings, sizeof(lan78xx_gstrings));
}

static int lan78xx_get_sset_count(struct net_device *netdev, int sset)
{
	if (sset == ETH_SS_STATS)
		return ARRAY_SIZE(lan78xx_gstrings);
	else
		return -EOPNOTSUPP;
}

static void lan78xx_get_stats(struct net_device *netdev,
			      struct ethtool_stats *stats, u64 *data)
{
	struct lan78xx_net *dev = netdev_priv(netdev);

	lan78xx_update_stats(dev);

	mutex_lock(&dev->stats.access_lock);
	memcpy(data, &dev->stats.curr_stat, sizeof(dev->stats.curr_stat));
	mutex_unlock(&dev->stats.access_lock);
}

static void lan78xx_get_wol(struct net_device *netdev,
			    struct ethtool_wolinfo *wol)
{
	struct lan78xx_net *dev = netdev_priv(netdev);
	int ret;
	u32 buf;
	struct lan78xx_priv *pdata = (struct lan78xx_priv *)(dev->data[0]);

	if (usb_autopm_get_interface(dev->intf) < 0)
			return;

	ret = lan78xx_read_reg(dev, USB_CFG0, &buf);
	if (unlikely(ret < 0)) {
		wol->supported = 0;
		wol->wolopts = 0;
	} else {
		if (buf & USB_CFG_RMT_WKP_) {
			wol->supported = WAKE_ALL;
			wol->wolopts = pdata->wol;
		} else {
			wol->supported = 0;
			wol->wolopts = 0;
		}
	}

	usb_autopm_put_interface(dev->intf);
}

static int lan78xx_set_wol(struct net_device *netdev,
			   struct ethtool_wolinfo *wol)
{
	struct lan78xx_net *dev = netdev_priv(netdev);
	struct lan78xx_priv *pdata = (struct lan78xx_priv *)(dev->data[0]);
	int ret;

	ret = usb_autopm_get_interface(dev->intf);
	if (ret < 0)
		return ret;

	if (wol->wolopts & ~WAKE_ALL)
		return -EINVAL;

	pdata->wol = wol->wolopts;

	device_set_wakeup_enable(&dev->udev->dev, (bool)wol->wolopts);

	phy_ethtool_set_wol(netdev->phydev, wol);

	usb_autopm_put_interface(dev->intf);

	return ret;
}

static int lan78xx_get_eee(struct net_device *net, struct ethtool_eee *edata)
{
	struct lan78xx_net *dev = netdev_priv(net);
	struct phy_device *phydev = net->phydev;
	int ret;
	u32 buf;

	ret = usb_autopm_get_interface(dev->intf);
	if (ret < 0)
		return ret;

	ret = phy_ethtool_get_eee(phydev, edata);
	if (ret < 0)
		goto exit;

	ret = lan78xx_read_reg(dev, MAC_CR, &buf);
	if (buf & MAC_CR_EEE_EN_) {
		edata->eee_enabled = true;
		edata->eee_active = !!(edata->advertised &
				       edata->lp_advertised);
		edata->tx_lpi_enabled = true;
		/* EEE_TX_LPI_REQ_DLY & tx_lpi_timer are same uSec unit */
		ret = lan78xx_read_reg(dev, EEE_TX_LPI_REQ_DLY, &buf);
		edata->tx_lpi_timer = buf;
	} else {
		edata->eee_enabled = false;
		edata->eee_active = false;
		edata->tx_lpi_enabled = false;
		edata->tx_lpi_timer = 0;
	}

	ret = 0;
exit:
	usb_autopm_put_interface(dev->intf);

	return ret;
}

static int lan78xx_set_eee(struct net_device *net, struct ethtool_eee *edata)
{
	struct lan78xx_net *dev = netdev_priv(net);
	int ret;
	u32 buf;

	ret = usb_autopm_get_interface(dev->intf);
	if (ret < 0)
		return ret;

	if (edata->eee_enabled) {
		ret = lan78xx_read_reg(dev, MAC_CR, &buf);
		buf |= MAC_CR_EEE_EN_;
		ret = lan78xx_write_reg(dev, MAC_CR, buf);

		phy_ethtool_set_eee(net->phydev, edata);

		buf = (u32)edata->tx_lpi_timer;
		ret = lan78xx_write_reg(dev, EEE_TX_LPI_REQ_DLY, buf);
	} else {
		ret = lan78xx_read_reg(dev, MAC_CR, &buf);
		buf &= ~MAC_CR_EEE_EN_;
		ret = lan78xx_write_reg(dev, MAC_CR, buf);
	}

	usb_autopm_put_interface(dev->intf);

	return 0;
}

static u32 lan78xx_get_link(struct net_device *net)
{
	phy_read_status(net->phydev);

	return net->phydev->link;
}

static void lan78xx_get_drvinfo(struct net_device *net,
				struct ethtool_drvinfo *info)
{
	struct lan78xx_net *dev = netdev_priv(net);

	strncpy(info->driver, DRIVER_NAME, sizeof(info->driver));
	usb_make_path(dev->udev, info->bus_info, sizeof(info->bus_info));
}

static u32 lan78xx_get_msglevel(struct net_device *net)
{
	struct lan78xx_net *dev = netdev_priv(net);

	return dev->msg_enable;
}

static void lan78xx_set_msglevel(struct net_device *net, u32 level)
{
	struct lan78xx_net *dev = netdev_priv(net);

	dev->msg_enable = level;
}

static int lan78xx_get_link_ksettings(struct net_device *net,
				      struct ethtool_link_ksettings *cmd)
{
	struct lan78xx_net *dev = netdev_priv(net);
	struct phy_device *phydev = net->phydev;
	int ret;

	ret = usb_autopm_get_interface(dev->intf);
	if (ret < 0)
		return ret;

	phy_ethtool_ksettings_get(phydev, cmd);

	usb_autopm_put_interface(dev->intf);

	return ret;
}

static int lan78xx_set_link_ksettings(struct net_device *net,
				      const struct ethtool_link_ksettings *cmd)
{
	struct lan78xx_net *dev = netdev_priv(net);
	struct phy_device *phydev = net->phydev;
	int ret = 0;
	int temp;

	ret = usb_autopm_get_interface(dev->intf);
	if (ret < 0)
		return ret;

	/* change speed & duplex */
	ret = phy_ethtool_ksettings_set(phydev, cmd);

	if (!cmd->base.autoneg) {
		/* force link down */
		temp = phy_read(phydev, MII_BMCR);
		phy_write(phydev, MII_BMCR, temp | BMCR_LOOPBACK);
		mdelay(1);
		phy_write(phydev, MII_BMCR, temp);
	}

	usb_autopm_put_interface(dev->intf);

	return ret;
}

static void lan78xx_get_pause(struct net_device *net,
			      struct ethtool_pauseparam *pause)
{
	struct lan78xx_net *dev = netdev_priv(net);
	struct phy_device *phydev = net->phydev;
	struct ethtool_link_ksettings ecmd;

	phy_ethtool_ksettings_get(phydev, &ecmd);

	pause->autoneg = dev->fc_autoneg;

	if (dev->fc_request_control & FLOW_CTRL_TX)
		pause->tx_pause = 1;

	if (dev->fc_request_control & FLOW_CTRL_RX)
		pause->rx_pause = 1;
}

static int lan78xx_set_pause(struct net_device *net,
			     struct ethtool_pauseparam *pause)
{
	struct lan78xx_net *dev = netdev_priv(net);
	struct phy_device *phydev = net->phydev;
	struct ethtool_link_ksettings ecmd;
	int ret;

	phy_ethtool_ksettings_get(phydev, &ecmd);

	if (pause->autoneg && !ecmd.base.autoneg) {
		ret = -EINVAL;
		goto exit;
	}

	dev->fc_request_control = 0;
	if (pause->rx_pause)
		dev->fc_request_control |= FLOW_CTRL_RX;

	if (pause->tx_pause)
		dev->fc_request_control |= FLOW_CTRL_TX;

	if (ecmd.base.autoneg) {
		__ETHTOOL_DECLARE_LINK_MODE_MASK(fc) = { 0, };
		u32 mii_adv;

		linkmode_clear_bit(ETHTOOL_LINK_MODE_Pause_BIT,
				   ecmd.link_modes.advertising);
		linkmode_clear_bit(ETHTOOL_LINK_MODE_Asym_Pause_BIT,
				   ecmd.link_modes.advertising);
		mii_adv = (u32)mii_advertise_flowctrl(dev->fc_request_control);
		mii_adv_to_linkmode_adv_t(fc, mii_adv);
		linkmode_or(ecmd.link_modes.advertising, fc,
			    ecmd.link_modes.advertising);

		phy_ethtool_ksettings_set(phydev, &ecmd);
	}

	dev->fc_autoneg = pause->autoneg;

	ret = 0;
exit:
	return ret;
}

static int lan78xx_get_regs_len(struct net_device *netdev)
{
	if (!netdev->phydev)
		return (sizeof(lan78xx_regs));
	else
		return (sizeof(lan78xx_regs) + PHY_REG_SIZE);
}

static void
lan78xx_get_regs(struct net_device *netdev, struct ethtool_regs *regs,
		 void *buf)
{
	u32 *data = buf;
	int i, j;
	struct lan78xx_net *dev = netdev_priv(netdev);

	/* Read Device/MAC registers */
	for (i = 0; i < ARRAY_SIZE(lan78xx_regs); i++)
		lan78xx_read_reg(dev, lan78xx_regs[i], &data[i]);

	if (!netdev->phydev)
		return;

	/* Read PHY registers */
	for (j = 0; j < 32; i++, j++)
		data[i] = phy_read(netdev->phydev, j);
}

static const struct ethtool_ops lan78xx_ethtool_ops = {
	.get_link	= lan78xx_get_link,
	.nway_reset	= phy_ethtool_nway_reset,
	.get_drvinfo	= lan78xx_get_drvinfo,
	.get_msglevel	= lan78xx_get_msglevel,
	.set_msglevel	= lan78xx_set_msglevel,
	.get_eeprom_len = lan78xx_ethtool_get_eeprom_len,
	.get_eeprom	= lan78xx_ethtool_get_eeprom,
	.set_eeprom	= lan78xx_ethtool_set_eeprom,
	.get_ethtool_stats = lan78xx_get_stats,
	.get_sset_count = lan78xx_get_sset_count,
	.get_strings	= lan78xx_get_strings,
	.get_wol	= lan78xx_get_wol,
	.set_wol	= lan78xx_set_wol,
	.get_eee	= lan78xx_get_eee,
	.set_eee	= lan78xx_set_eee,
	.get_pauseparam	= lan78xx_get_pause,
	.set_pauseparam	= lan78xx_set_pause,
	.get_link_ksettings = lan78xx_get_link_ksettings,
	.set_link_ksettings = lan78xx_set_link_ksettings,
	.get_regs_len	= lan78xx_get_regs_len,
	.get_regs	= lan78xx_get_regs,
};

static int lan78xx_ioctl(struct net_device *netdev, struct ifreq *rq, int cmd)
{
	if (!netif_running(netdev))
		return -EINVAL;

	return phy_mii_ioctl(netdev->phydev, rq, cmd);
}

static void lan78xx_init_mac_address(struct lan78xx_net *dev)
{
	u32 addr_lo, addr_hi;
	int ret;
	u8 addr[6];

	ret = lan78xx_read_reg(dev, RX_ADDRL, &addr_lo);
	ret = lan78xx_read_reg(dev, RX_ADDRH, &addr_hi);

	addr[0] = addr_lo & 0xFF;
	addr[1] = (addr_lo >> 8) & 0xFF;
	addr[2] = (addr_lo >> 16) & 0xFF;
	addr[3] = (addr_lo >> 24) & 0xFF;
	addr[4] = addr_hi & 0xFF;
	addr[5] = (addr_hi >> 8) & 0xFF;

	if (!is_valid_ether_addr(addr)) {
		if (!eth_platform_get_mac_address(&dev->udev->dev, addr)) {
			/* valid address present in Device Tree */
			netif_dbg(dev, ifup, dev->net,
				  "MAC address read from Device Tree");
		} else if (((lan78xx_read_eeprom(dev, EEPROM_MAC_OFFSET,
						 ETH_ALEN, addr) == 0) ||
			    (lan78xx_read_otp(dev, EEPROM_MAC_OFFSET,
					      ETH_ALEN, addr) == 0)) &&
			   is_valid_ether_addr(addr)) {
			/* eeprom values are valid so use them */
			netif_dbg(dev, ifup, dev->net,
				  "MAC address read from EEPROM");
		} else {
			/* generate random MAC */
			eth_random_addr(addr);
			netif_dbg(dev, ifup, dev->net,
				  "MAC address set to random addr");
		}

		addr_lo = addr[0] | (addr[1] << 8) |
			  (addr[2] << 16) | (addr[3] << 24);
		addr_hi = addr[4] | (addr[5] << 8);

		ret = lan78xx_write_reg(dev, RX_ADDRL, addr_lo);
		ret = lan78xx_write_reg(dev, RX_ADDRH, addr_hi);
	}

	ret = lan78xx_write_reg(dev, MAF_LO(0), addr_lo);
	ret = lan78xx_write_reg(dev, MAF_HI(0), addr_hi | MAF_HI_VALID_);

	ether_addr_copy(dev->net->dev_addr, addr);
}

/* MDIO read and write wrappers for phylib */
static int lan78xx_mdiobus_read(struct mii_bus *bus, int phy_id, int idx)
{
	struct lan78xx_net *dev = bus->priv;
	u32 val, addr;
	int ret;

	ret = usb_autopm_get_interface(dev->intf);
	if (ret < 0)
		return ret;

	mutex_lock(&dev->phy_mutex);

	/* confirm MII not busy */
	ret = lan78xx_phy_wait_not_busy(dev);
	if (ret < 0)
		goto done;

	/* set the address, index & direction (read from PHY) */
	addr = mii_access(phy_id, idx, MII_READ);
	ret = lan78xx_write_reg(dev, MII_ACC, addr);

	ret = lan78xx_phy_wait_not_busy(dev);
	if (ret < 0)
		goto done;

	ret = lan78xx_read_reg(dev, MII_DATA, &val);

	ret = (int)(val & 0xFFFF);

done:
	mutex_unlock(&dev->phy_mutex);
	usb_autopm_put_interface(dev->intf);

	return ret;
}

static int lan78xx_mdiobus_write(struct mii_bus *bus, int phy_id, int idx,
				 u16 regval)
{
	struct lan78xx_net *dev = bus->priv;
	u32 val, addr;
	int ret;

	ret = usb_autopm_get_interface(dev->intf);
	if (ret < 0)
		return ret;

	mutex_lock(&dev->phy_mutex);

	/* confirm MII not busy */
	ret = lan78xx_phy_wait_not_busy(dev);
	if (ret < 0)
		goto done;

	val = (u32)regval;
	ret = lan78xx_write_reg(dev, MII_DATA, val);

	/* set the address, index & direction (write to PHY) */
	addr = mii_access(phy_id, idx, MII_WRITE);
	ret = lan78xx_write_reg(dev, MII_ACC, addr);

	ret = lan78xx_phy_wait_not_busy(dev);
	if (ret < 0)
		goto done;

done:
	mutex_unlock(&dev->phy_mutex);
	usb_autopm_put_interface(dev->intf);
	return 0;
}

static int lan78xx_mdio_init(struct lan78xx_net *dev)
{
	struct device_node *node;
	int ret;

	dev->mdiobus = mdiobus_alloc();
	if (!dev->mdiobus) {
		netdev_err(dev->net, "can't allocate MDIO bus\n");
		return -ENOMEM;
	}

	dev->mdiobus->priv = (void *)dev;
	dev->mdiobus->read = lan78xx_mdiobus_read;
	dev->mdiobus->write = lan78xx_mdiobus_write;
	dev->mdiobus->name = "lan78xx-mdiobus";

	snprintf(dev->mdiobus->id, MII_BUS_ID_SIZE, "usb-%03d:%03d",
		 dev->udev->bus->busnum, dev->udev->devnum);

	switch (dev->chipid) {
	case ID_REV_CHIP_ID_7800_:
	case ID_REV_CHIP_ID_7850_:
		/* set to internal PHY id */
		dev->mdiobus->phy_mask = ~(1 << 1);
		break;
	case ID_REV_CHIP_ID_7801_:
		/* scan thru PHYAD[2..0] */
		dev->mdiobus->phy_mask = ~(0xFF);
		break;
	}

	node = of_get_child_by_name(dev->udev->dev.of_node, "mdio");
	ret = of_mdiobus_register(dev->mdiobus, node);
	of_node_put(node);
	if (ret) {
		netdev_err(dev->net, "can't register MDIO bus\n");
		goto exit1;
	}

	netdev_dbg(dev->net, "registered mdiobus bus %s\n", dev->mdiobus->id);
	return 0;
exit1:
	mdiobus_free(dev->mdiobus);
	return ret;
}

static void lan78xx_remove_mdio(struct lan78xx_net *dev)
{
	mdiobus_unregister(dev->mdiobus);
	mdiobus_free(dev->mdiobus);
}

static void lan78xx_link_status_change(struct net_device *net)
{
	struct phy_device *phydev = net->phydev;
	int ret, temp;

	/* At forced 100 F/H mode, chip may fail to set mode correctly
	 * when cable is switched between long(~50+m) and short one.
	 * As workaround, set to 10 before setting to 100
	 * at forced 100 F/H mode.
	 */
	if (!phydev->autoneg && (phydev->speed == 100)) {
		/* disable phy interrupt */
		temp = phy_read(phydev, LAN88XX_INT_MASK);
		temp &= ~LAN88XX_INT_MASK_MDINTPIN_EN_;
		ret = phy_write(phydev, LAN88XX_INT_MASK, temp);

		temp = phy_read(phydev, MII_BMCR);
		temp &= ~(BMCR_SPEED100 | BMCR_SPEED1000);
		phy_write(phydev, MII_BMCR, temp); /* set to 10 first */
		temp |= BMCR_SPEED100;
		phy_write(phydev, MII_BMCR, temp); /* set to 100 later */

		/* clear pending interrupt generated while workaround */
		temp = phy_read(phydev, LAN88XX_INT_STS);

		/* enable phy interrupt back */
		temp = phy_read(phydev, LAN88XX_INT_MASK);
		temp |= LAN88XX_INT_MASK_MDINTPIN_EN_;
		ret = phy_write(phydev, LAN88XX_INT_MASK, temp);
	}
}

static int irq_map(struct irq_domain *d, unsigned int irq,
		   irq_hw_number_t hwirq)
{
	struct irq_domain_data *data = d->host_data;

	irq_set_chip_data(irq, data);
	irq_set_chip_and_handler(irq, data->irqchip, data->irq_handler);
	irq_set_noprobe(irq);

	return 0;
}

static void irq_unmap(struct irq_domain *d, unsigned int irq)
{
	irq_set_chip_and_handler(irq, NULL, NULL);
	irq_set_chip_data(irq, NULL);
}

static const struct irq_domain_ops chip_domain_ops = {
	.map	= irq_map,
	.unmap	= irq_unmap,
};

static void lan78xx_irq_mask(struct irq_data *irqd)
{
	struct irq_domain_data *data = irq_data_get_irq_chip_data(irqd);

	data->irqenable &= ~BIT(irqd_to_hwirq(irqd));
}

static void lan78xx_irq_unmask(struct irq_data *irqd)
{
	struct irq_domain_data *data = irq_data_get_irq_chip_data(irqd);

	data->irqenable |= BIT(irqd_to_hwirq(irqd));
}

static void lan78xx_irq_bus_lock(struct irq_data *irqd)
{
	struct irq_domain_data *data = irq_data_get_irq_chip_data(irqd);

	mutex_lock(&data->irq_lock);
}

static void lan78xx_irq_bus_sync_unlock(struct irq_data *irqd)
{
	struct irq_domain_data *data = irq_data_get_irq_chip_data(irqd);
	struct lan78xx_net *dev =
			container_of(data, struct lan78xx_net, domain_data);
	u32 buf;
	int ret;

	/* call register access here because irq_bus_lock & irq_bus_sync_unlock
	 * are only two callbacks executed in non-atomic contex.
	 */
	ret = lan78xx_read_reg(dev, INT_EP_CTL, &buf);
	if (buf != data->irqenable)
		ret = lan78xx_write_reg(dev, INT_EP_CTL, data->irqenable);

	mutex_unlock(&data->irq_lock);
}

static struct irq_chip lan78xx_irqchip = {
	.name			= "lan78xx-irqs",
	.irq_mask		= lan78xx_irq_mask,
	.irq_unmask		= lan78xx_irq_unmask,
	.irq_bus_lock		= lan78xx_irq_bus_lock,
	.irq_bus_sync_unlock	= lan78xx_irq_bus_sync_unlock,
};

static int lan78xx_setup_irq_domain(struct lan78xx_net *dev)
{
	struct device_node *of_node;
	struct irq_domain *irqdomain;
	unsigned int irqmap = 0;
	u32 buf;
	int ret = 0;

	of_node = dev->udev->dev.parent->of_node;

	mutex_init(&dev->domain_data.irq_lock);

	lan78xx_read_reg(dev, INT_EP_CTL, &buf);
	dev->domain_data.irqenable = buf;

	dev->domain_data.irqchip = &lan78xx_irqchip;
	dev->domain_data.irq_handler = handle_simple_irq;

	irqdomain = irq_domain_add_simple(of_node, MAX_INT_EP, 0,
					  &chip_domain_ops, &dev->domain_data);
	if (irqdomain) {
		/* create mapping for PHY interrupt */
		irqmap = irq_create_mapping(irqdomain, INT_EP_PHY);
		if (!irqmap) {
			irq_domain_remove(irqdomain);

			irqdomain = NULL;
			ret = -EINVAL;
		}
	} else {
		ret = -EINVAL;
	}

	dev->domain_data.irqdomain = irqdomain;
	dev->domain_data.phyirq = irqmap;

	return ret;
}

static void lan78xx_remove_irq_domain(struct lan78xx_net *dev)
{
	if (dev->domain_data.phyirq > 0) {
		irq_dispose_mapping(dev->domain_data.phyirq);

		if (dev->domain_data.irqdomain)
			irq_domain_remove(dev->domain_data.irqdomain);
	}
	dev->domain_data.phyirq = 0;
	dev->domain_data.irqdomain = NULL;
}

static int lan8835_fixup(struct phy_device *phydev)
{
	int buf;
	int ret;
	struct lan78xx_net *dev = netdev_priv(phydev->attached_dev);

	/* LED2/PME_N/IRQ_N/RGMII_ID pin to IRQ_N mode */
	buf = phy_read_mmd(phydev, MDIO_MMD_PCS, 0x8010);
	buf &= ~0x1800;
	buf |= 0x0800;
	phy_write_mmd(phydev, MDIO_MMD_PCS, 0x8010, buf);

	/* RGMII MAC TXC Delay Enable */
	ret = lan78xx_write_reg(dev, MAC_RGMII_ID,
				MAC_RGMII_ID_TXC_DELAY_EN_);

	/* RGMII TX DLL Tune Adjust */
	ret = lan78xx_write_reg(dev, RGMII_TX_BYP_DLL, 0x3D00);

	dev->interface = PHY_INTERFACE_MODE_RGMII_TXID;

	return 1;
}

static int ksz9031rnx_fixup(struct phy_device *phydev)
{
	struct lan78xx_net *dev = netdev_priv(phydev->attached_dev);

	/* Micrel9301RNX PHY configuration */
	/* RGMII Control Signal Pad Skew */
	phy_write_mmd(phydev, MDIO_MMD_WIS, 4, 0x0077);
	/* RGMII RX Data Pad Skew */
	phy_write_mmd(phydev, MDIO_MMD_WIS, 5, 0x7777);
	/* RGMII RX Clock Pad Skew */
	phy_write_mmd(phydev, MDIO_MMD_WIS, 8, 0x1FF);

	dev->interface = PHY_INTERFACE_MODE_RGMII_RXID;

	return 1;
}

static struct phy_device *lan7801_phy_init(struct lan78xx_net *dev)
{
	u32 buf;
	int ret;
	struct fixed_phy_status fphy_status = {
		.link = 1,
		.speed = SPEED_1000,
		.duplex = DUPLEX_FULL,
	};
	struct phy_device *phydev;

	phydev = phy_find_first(dev->mdiobus);
	if (!phydev) {
		netdev_dbg(dev->net, "PHY Not Found!! Registering Fixed PHY\n");
		phydev = fixed_phy_register(PHY_POLL, &fphy_status, NULL);
		if (IS_ERR(phydev)) {
			netdev_err(dev->net, "No PHY/fixed_PHY found\n");
			return NULL;
		}
		netdev_dbg(dev->net, "Registered FIXED PHY\n");
		dev->interface = PHY_INTERFACE_MODE_RGMII;
		ret = lan78xx_write_reg(dev, MAC_RGMII_ID,
					MAC_RGMII_ID_TXC_DELAY_EN_);
		ret = lan78xx_write_reg(dev, RGMII_TX_BYP_DLL, 0x3D00);
		ret = lan78xx_read_reg(dev, HW_CFG, &buf);
		buf |= HW_CFG_CLK125_EN_;
		buf |= HW_CFG_REFCLK25_EN_;
		ret = lan78xx_write_reg(dev, HW_CFG, buf);
	} else {
		if (!phydev->drv) {
			netdev_err(dev->net, "no PHY driver found\n");
			return NULL;
		}
		dev->interface = PHY_INTERFACE_MODE_RGMII;
		/* external PHY fixup for KSZ9031RNX */
		ret = phy_register_fixup_for_uid(PHY_KSZ9031RNX, 0xfffffff0,
						 ksz9031rnx_fixup);
		if (ret < 0) {
			netdev_err(dev->net, "Failed to register fixup for PHY_KSZ9031RNX\n");
			return NULL;
		}
		/* external PHY fixup for LAN8835 */
		ret = phy_register_fixup_for_uid(PHY_LAN8835, 0xfffffff0,
						 lan8835_fixup);
		if (ret < 0) {
			netdev_err(dev->net, "Failed to register fixup for PHY_LAN8835\n");
			return NULL;
		}
		/* add more external PHY fixup here if needed */

		phydev->is_internal = false;
	}
	return phydev;
}

static int lan78xx_phy_init(struct lan78xx_net *dev)
{
	__ETHTOOL_DECLARE_LINK_MODE_MASK(fc) = { 0, };
	int ret;
	u32 mii_adv;
	struct phy_device *phydev;

	switch (dev->chipid) {
	case ID_REV_CHIP_ID_7801_:
		phydev = lan7801_phy_init(dev);
		if (!phydev) {
			netdev_err(dev->net, "lan7801: PHY Init Failed");
			return -EIO;
		}
		break;

	case ID_REV_CHIP_ID_7800_:
	case ID_REV_CHIP_ID_7850_:
		phydev = phy_find_first(dev->mdiobus);
		if (!phydev) {
			netdev_err(dev->net, "no PHY found\n");
			return -EIO;
		}
		phydev->is_internal = true;
		dev->interface = PHY_INTERFACE_MODE_GMII;
		break;

	default:
		netdev_err(dev->net, "Unknown CHIP ID found\n");
		return -EIO;
	}

	/* if phyirq is not set, use polling mode in phylib */
	if (dev->domain_data.phyirq > 0)
		phydev->irq = dev->domain_data.phyirq;
	else
		phydev->irq = 0;
	netdev_dbg(dev->net, "phydev->irq = %d\n", phydev->irq);

	/* set to AUTOMDIX */
	phydev->mdix = ETH_TP_MDI_AUTO;

	ret = phy_connect_direct(dev->net, phydev,
				 lan78xx_link_status_change,
				 dev->interface);
	if (ret) {
		netdev_err(dev->net, "can't attach PHY to %s\n",
			   dev->mdiobus->id);
		if (dev->chipid == ID_REV_CHIP_ID_7801_) {
			if (phy_is_pseudo_fixed_link(phydev)) {
				fixed_phy_unregister(phydev);
			} else {
				phy_unregister_fixup_for_uid(PHY_KSZ9031RNX,
							     0xfffffff0);
				phy_unregister_fixup_for_uid(PHY_LAN8835,
							     0xfffffff0);
			}
		}
		return -EIO;
	}

	/* MAC doesn't support 1000T Half */
	phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_1000baseT_Half_BIT);

	/* support both flow controls */
	dev->fc_request_control = (FLOW_CTRL_RX | FLOW_CTRL_TX);
	linkmode_clear_bit(ETHTOOL_LINK_MODE_Pause_BIT,
			   phydev->advertising);
	linkmode_clear_bit(ETHTOOL_LINK_MODE_Asym_Pause_BIT,
			   phydev->advertising);
	mii_adv = (u32)mii_advertise_flowctrl(dev->fc_request_control);
	mii_adv_to_linkmode_adv_t(fc, mii_adv);
	linkmode_or(phydev->advertising, fc, phydev->advertising);

	if (phydev->mdio.dev.of_node) {
		u32 reg;
		int len;

		len = of_property_count_elems_of_size(phydev->mdio.dev.of_node,
						      "microchip,led-modes",
						      sizeof(u32));
		if (len >= 0) {
			/* Ensure the appropriate LEDs are enabled */
			lan78xx_read_reg(dev, HW_CFG, &reg);
			reg &= ~(HW_CFG_LED0_EN_ |
				 HW_CFG_LED1_EN_ |
				 HW_CFG_LED2_EN_ |
				 HW_CFG_LED3_EN_);
			reg |= (len > 0) * HW_CFG_LED0_EN_ |
				(len > 1) * HW_CFG_LED1_EN_ |
				(len > 2) * HW_CFG_LED2_EN_ |
				(len > 3) * HW_CFG_LED3_EN_;
			lan78xx_write_reg(dev, HW_CFG, reg);
		}
	}

	genphy_config_aneg(phydev);

	dev->fc_autoneg = phydev->autoneg;

	return 0;
}

static int lan78xx_set_rx_max_frame_length(struct lan78xx_net *dev, int size)
{
	int ret = 0;
	u32 buf;
	bool rxenabled;

	ret = lan78xx_read_reg(dev, MAC_RX, &buf);

	rxenabled = ((buf & MAC_RX_RXEN_) != 0);

	if (rxenabled) {
		buf &= ~MAC_RX_RXEN_;
		ret = lan78xx_write_reg(dev, MAC_RX, buf);
	}

	/* add 4 to size for FCS */
	buf &= ~MAC_RX_MAX_SIZE_MASK_;
	buf |= (((size + 4) << MAC_RX_MAX_SIZE_SHIFT_) & MAC_RX_MAX_SIZE_MASK_);

	ret = lan78xx_write_reg(dev, MAC_RX, buf);

	if (rxenabled) {
		buf |= MAC_RX_RXEN_;
		ret = lan78xx_write_reg(dev, MAC_RX, buf);
	}

	return 0;
}

static int unlink_urbs(struct lan78xx_net *dev, struct sk_buff_head *q)
{
	struct sk_buff *skb;
	unsigned long flags;
	int count = 0;

	spin_lock_irqsave(&q->lock, flags);
	while (!skb_queue_empty(q)) {
		struct skb_data	*entry;
		struct urb *urb;
		int ret;

		skb_queue_walk(q, skb) {
			entry = (struct skb_data *)skb->cb;
			if (entry->state != unlink_start)
				goto found;
		}
		break;
found:
		entry->state = unlink_start;
		urb = entry->urb;

		/* Get reference count of the URB to avoid it to be
		 * freed during usb_unlink_urb, which may trigger
		 * use-after-free problem inside usb_unlink_urb since
		 * usb_unlink_urb is always racing with .complete
		 * handler(include defer_bh).
		 */
		usb_get_urb(urb);
		spin_unlock_irqrestore(&q->lock, flags);
		/* during some PM-driven resume scenarios,
		 * these (async) unlinks complete immediately
		 */
		ret = usb_unlink_urb(urb);
		if (ret != -EINPROGRESS && ret != 0)
			netdev_dbg(dev->net, "unlink urb err, %d\n", ret);
		else
			count++;
		usb_put_urb(urb);
		spin_lock_irqsave(&q->lock, flags);
	}
	spin_unlock_irqrestore(&q->lock, flags);
	return count;
}

static int lan78xx_change_mtu(struct net_device *netdev, int new_mtu)
{
	struct lan78xx_net *dev = netdev_priv(netdev);
	int ll_mtu = new_mtu + netdev->hard_header_len;
	int old_hard_mtu = dev->hard_mtu;
	int old_rx_urb_size = dev->rx_urb_size;
	int ret;

	/* no second zero-length packet read wanted after mtu-sized packets */
	if ((ll_mtu % dev->maxpacket) == 0)
		return -EDOM;

	ret = lan78xx_set_rx_max_frame_length(dev, new_mtu + VLAN_ETH_HLEN);

	netdev->mtu = new_mtu;

	dev->hard_mtu = netdev->mtu + netdev->hard_header_len;
	if (dev->rx_urb_size == old_hard_mtu) {
		dev->rx_urb_size = dev->hard_mtu;
		if (dev->rx_urb_size > old_rx_urb_size) {
			if (netif_running(dev->net)) {
				unlink_urbs(dev, &dev->rxq);
				tasklet_schedule(&dev->bh);
			}
		}
	}

	return 0;
}

static int lan78xx_set_mac_addr(struct net_device *netdev, void *p)
{
	struct lan78xx_net *dev = netdev_priv(netdev);
	struct sockaddr *addr = p;
	u32 addr_lo, addr_hi;
	int ret;

	if (netif_running(netdev))
		return -EBUSY;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	ether_addr_copy(netdev->dev_addr, addr->sa_data);

	addr_lo = netdev->dev_addr[0] |
		  netdev->dev_addr[1] << 8 |
		  netdev->dev_addr[2] << 16 |
		  netdev->dev_addr[3] << 24;
	addr_hi = netdev->dev_addr[4] |
		  netdev->dev_addr[5] << 8;

	ret = lan78xx_write_reg(dev, RX_ADDRL, addr_lo);
	ret = lan78xx_write_reg(dev, RX_ADDRH, addr_hi);

	/* Added to support MAC address changes */
	ret = lan78xx_write_reg(dev, MAF_LO(0), addr_lo);
	ret = lan78xx_write_reg(dev, MAF_HI(0), addr_hi | MAF_HI_VALID_);

	return 0;
}

/* Enable or disable Rx checksum offload engine */
static int lan78xx_set_features(struct net_device *netdev,
				netdev_features_t features)
{
	struct lan78xx_net *dev = netdev_priv(netdev);
	struct lan78xx_priv *pdata = (struct lan78xx_priv *)(dev->data[0]);
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&pdata->rfe_ctl_lock, flags);

	if (features & NETIF_F_RXCSUM) {
		pdata->rfe_ctl |= RFE_CTL_TCPUDP_COE_ | RFE_CTL_IP_COE_;
		pdata->rfe_ctl |= RFE_CTL_ICMP_COE_ | RFE_CTL_IGMP_COE_;
	} else {
		pdata->rfe_ctl &= ~(RFE_CTL_TCPUDP_COE_ | RFE_CTL_IP_COE_);
		pdata->rfe_ctl &= ~(RFE_CTL_ICMP_COE_ | RFE_CTL_IGMP_COE_);
	}

	if (features & NETIF_F_HW_VLAN_CTAG_RX)
		pdata->rfe_ctl |= RFE_CTL_VLAN_STRIP_;
	else
		pdata->rfe_ctl &= ~RFE_CTL_VLAN_STRIP_;

	if (features & NETIF_F_HW_VLAN_CTAG_FILTER)
		pdata->rfe_ctl |= RFE_CTL_VLAN_FILTER_;
	else
		pdata->rfe_ctl &= ~RFE_CTL_VLAN_FILTER_;

	spin_unlock_irqrestore(&pdata->rfe_ctl_lock, flags);

	ret = lan78xx_write_reg(dev, RFE_CTL, pdata->rfe_ctl);

	return 0;
}

static void lan78xx_deferred_vlan_write(struct work_struct *param)
{
	struct lan78xx_priv *pdata =
			container_of(param, struct lan78xx_priv, set_vlan);
	struct lan78xx_net *dev = pdata->dev;

	lan78xx_dataport_write(dev, DP_SEL_RSEL_VLAN_DA_, 0,
			       DP_SEL_VHF_VLAN_LEN, pdata->vlan_table);
}

static int lan78xx_vlan_rx_add_vid(struct net_device *netdev,
				   __be16 proto, u16 vid)
{
	struct lan78xx_net *dev = netdev_priv(netdev);
	struct lan78xx_priv *pdata = (struct lan78xx_priv *)(dev->data[0]);
	u16 vid_bit_index;
	u16 vid_dword_index;

	vid_dword_index = (vid >> 5) & 0x7F;
	vid_bit_index = vid & 0x1F;

	pdata->vlan_table[vid_dword_index] |= (1 << vid_bit_index);

	/* defer register writes to a sleepable context */
	schedule_work(&pdata->set_vlan);

	return 0;
}

static int lan78xx_vlan_rx_kill_vid(struct net_device *netdev,
				    __be16 proto, u16 vid)
{
	struct lan78xx_net *dev = netdev_priv(netdev);
	struct lan78xx_priv *pdata = (struct lan78xx_priv *)(dev->data[0]);
	u16 vid_bit_index;
	u16 vid_dword_index;

	vid_dword_index = (vid >> 5) & 0x7F;
	vid_bit_index = vid & 0x1F;

	pdata->vlan_table[vid_dword_index] &= ~(1 << vid_bit_index);

	/* defer register writes to a sleepable context */
	schedule_work(&pdata->set_vlan);

	return 0;
}

static void lan78xx_init_ltm(struct lan78xx_net *dev)
{
	int ret;
	u32 buf;
	u32 regs[6] = { 0 };

	ret = lan78xx_read_reg(dev, USB_CFG1, &buf);
	if (buf & USB_CFG1_LTM_ENABLE_) {
		u8 temp[2];
		/* Get values from EEPROM first */
		if (lan78xx_read_eeprom(dev, 0x3F, 2, temp) == 0) {
			if (temp[0] == 24) {
				ret = lan78xx_read_raw_eeprom(dev,
							      temp[1] * 2,
							      24,
							      (u8 *)regs);
				if (ret < 0)
					return;
			}
		} else if (lan78xx_read_otp(dev, 0x3F, 2, temp) == 0) {
			if (temp[0] == 24) {
				ret = lan78xx_read_raw_otp(dev,
							   temp[1] * 2,
							   24,
							   (u8 *)regs);
				if (ret < 0)
					return;
			}
		}
	}

	lan78xx_write_reg(dev, LTM_BELT_IDLE0, regs[0]);
	lan78xx_write_reg(dev, LTM_BELT_IDLE1, regs[1]);
	lan78xx_write_reg(dev, LTM_BELT_ACT0, regs[2]);
	lan78xx_write_reg(dev, LTM_BELT_ACT1, regs[3]);
	lan78xx_write_reg(dev, LTM_INACTIVE0, regs[4]);
	lan78xx_write_reg(dev, LTM_INACTIVE1, regs[5]);
}

static int lan78xx_reset(struct lan78xx_net *dev)
{
	struct lan78xx_priv *pdata = (struct lan78xx_priv *)(dev->data[0]);
	u32 buf;
	int ret = 0;
	unsigned long timeout;
	u8 sig;

	ret = lan78xx_read_reg(dev, HW_CFG, &buf);
	buf |= HW_CFG_LRST_;
	ret = lan78xx_write_reg(dev, HW_CFG, buf);

	timeout = jiffies + HZ;
	do {
		mdelay(1);
		ret = lan78xx_read_reg(dev, HW_CFG, &buf);
		if (time_after(jiffies, timeout)) {
			netdev_warn(dev->net,
				    "timeout on completion of LiteReset");
			return -EIO;
		}
	} while (buf & HW_CFG_LRST_);

	lan78xx_init_mac_address(dev);

	/* save DEVID for later usage */
	ret = lan78xx_read_reg(dev, ID_REV, &buf);
	dev->chipid = (buf & ID_REV_CHIP_ID_MASK_) >> 16;
	dev->chiprev = buf & ID_REV_CHIP_REV_MASK_;

	/* Respond to the IN token with a NAK */
	ret = lan78xx_read_reg(dev, USB_CFG0, &buf);
	buf |= USB_CFG_BIR_;
	ret = lan78xx_write_reg(dev, USB_CFG0, buf);

	/* Init LTM */
	lan78xx_init_ltm(dev);

	if (dev->udev->speed == USB_SPEED_SUPER) {
		buf = DEFAULT_BURST_CAP_SIZE / SS_USB_PKT_SIZE;
		dev->rx_urb_size = DEFAULT_BURST_CAP_SIZE;
		dev->rx_qlen = 4;
		dev->tx_qlen = 4;
	} else if (dev->udev->speed == USB_SPEED_HIGH) {
		buf = DEFAULT_BURST_CAP_SIZE / HS_USB_PKT_SIZE;
		dev->rx_urb_size = DEFAULT_BURST_CAP_SIZE;
		dev->rx_qlen = RX_MAX_QUEUE_MEMORY / dev->rx_urb_size;
		dev->tx_qlen = RX_MAX_QUEUE_MEMORY / dev->hard_mtu;
	} else {
		buf = DEFAULT_BURST_CAP_SIZE / FS_USB_PKT_SIZE;
		dev->rx_urb_size = DEFAULT_BURST_CAP_SIZE;
		dev->rx_qlen = 4;
		dev->tx_qlen = 4;
	}

	ret = lan78xx_write_reg(dev, BURST_CAP, buf);
	ret = lan78xx_write_reg(dev, BULK_IN_DLY, DEFAULT_BULK_IN_DELAY);

	ret = lan78xx_read_reg(dev, HW_CFG, &buf);
	buf |= HW_CFG_MEF_;
	ret = lan78xx_write_reg(dev, HW_CFG, buf);

	ret = lan78xx_read_reg(dev, USB_CFG0, &buf);
	buf |= USB_CFG_BCE_;
	ret = lan78xx_write_reg(dev, USB_CFG0, buf);

	/* set FIFO sizes */
	buf = (MAX_RX_FIFO_SIZE - 512) / 512;
	ret = lan78xx_write_reg(dev, FCT_RX_FIFO_END, buf);

	buf = (MAX_TX_FIFO_SIZE - 512) / 512;
	ret = lan78xx_write_reg(dev, FCT_TX_FIFO_END, buf);

	ret = lan78xx_write_reg(dev, INT_STS, INT_STS_CLEAR_ALL_);
	ret = lan78xx_write_reg(dev, FLOW, 0);
	ret = lan78xx_write_reg(dev, FCT_FLOW, 0);

	/* Don't need rfe_ctl_lock during initialisation */
	ret = lan78xx_read_reg(dev, RFE_CTL, &pdata->rfe_ctl);
	pdata->rfe_ctl |= RFE_CTL_BCAST_EN_ | RFE_CTL_DA_PERFECT_;
	ret = lan78xx_write_reg(dev, RFE_CTL, pdata->rfe_ctl);

	/* Enable or disable checksum offload engines */
	lan78xx_set_features(dev->net, dev->net->features);

	lan78xx_set_multicast(dev->net);

	/* reset PHY */
	ret = lan78xx_read_reg(dev, PMT_CTL, &buf);
	buf |= PMT_CTL_PHY_RST_;
	ret = lan78xx_write_reg(dev, PMT_CTL, buf);

	timeout = jiffies + HZ;
	do {
		mdelay(1);
		ret = lan78xx_read_reg(dev, PMT_CTL, &buf);
		if (time_after(jiffies, timeout)) {
			netdev_warn(dev->net, "timeout waiting for PHY Reset");
			return -EIO;
		}
	} while ((buf & PMT_CTL_PHY_RST_) || !(buf & PMT_CTL_READY_));

	ret = lan78xx_read_reg(dev, MAC_CR, &buf);
	/* LAN7801 only has RGMII mode */
	if (dev->chipid == ID_REV_CHIP_ID_7801_)
		buf &= ~MAC_CR_GMII_EN_;

	if (dev->chipid == ID_REV_CHIP_ID_7800_) {
		ret = lan78xx_read_raw_eeprom(dev, 0, 1, &sig);
		if (!ret && sig != EEPROM_INDICATOR) {
			/* Implies there is no external eeprom. Set mac speed */
			netdev_info(dev->net, "No External EEPROM. Setting MAC Speed\n");
			buf |= MAC_CR_AUTO_DUPLEX_ | MAC_CR_AUTO_SPEED_;
		}
	}
	ret = lan78xx_write_reg(dev, MAC_CR, buf);

	ret = lan78xx_read_reg(dev, MAC_TX, &buf);
	buf |= MAC_TX_TXEN_;
	ret = lan78xx_write_reg(dev, MAC_TX, buf);

	ret = lan78xx_read_reg(dev, FCT_TX_CTL, &buf);
	buf |= FCT_TX_CTL_EN_;
	ret = lan78xx_write_reg(dev, FCT_TX_CTL, buf);

	ret = lan78xx_set_rx_max_frame_length(dev,
					      dev->net->mtu + VLAN_ETH_HLEN);

	ret = lan78xx_read_reg(dev, MAC_RX, &buf);
	buf |= MAC_RX_RXEN_;
	ret = lan78xx_write_reg(dev, MAC_RX, buf);

	ret = lan78xx_read_reg(dev, FCT_RX_CTL, &buf);
	buf |= FCT_RX_CTL_EN_;
	ret = lan78xx_write_reg(dev, FCT_RX_CTL, buf);

	return 0;
}

static void lan78xx_init_stats(struct lan78xx_net *dev)
{
	u32 *p;
	int i;

	/* initialize for stats update
	 * some counters are 20bits and some are 32bits
	 */
	p = (u32 *)&dev->stats.rollover_max;
	for (i = 0; i < (sizeof(dev->stats.rollover_max) / (sizeof(u32))); i++)
		p[i] = 0xFFFFF;

	dev->stats.rollover_max.rx_unicast_byte_count = 0xFFFFFFFF;
	dev->stats.rollover_max.rx_broadcast_byte_count = 0xFFFFFFFF;
	dev->stats.rollover_max.rx_multicast_byte_count = 0xFFFFFFFF;
	dev->stats.rollover_max.eee_rx_lpi_transitions = 0xFFFFFFFF;
	dev->stats.rollover_max.eee_rx_lpi_time = 0xFFFFFFFF;
	dev->stats.rollover_max.tx_unicast_byte_count = 0xFFFFFFFF;
	dev->stats.rollover_max.tx_broadcast_byte_count = 0xFFFFFFFF;
	dev->stats.rollover_max.tx_multicast_byte_count = 0xFFFFFFFF;
	dev->stats.rollover_max.eee_tx_lpi_transitions = 0xFFFFFFFF;
	dev->stats.rollover_max.eee_tx_lpi_time = 0xFFFFFFFF;

	set_bit(EVENT_STAT_UPDATE, &dev->flags);
}

static int lan78xx_open(struct net_device *net)
{
	struct lan78xx_net *dev = netdev_priv(net);
	int ret;

	ret = usb_autopm_get_interface(dev->intf);
	if (ret < 0)
		goto out;

	phy_start(net->phydev);

	netif_dbg(dev, ifup, dev->net, "phy initialised successfully");

	/* for Link Check */
	if (dev->urb_intr) {
		ret = usb_submit_urb(dev->urb_intr, GFP_KERNEL);
		if (ret < 0) {
			netif_err(dev, ifup, dev->net,
				  "intr submit %d\n", ret);
			goto done;
		}
	}

	lan78xx_init_stats(dev);

	set_bit(EVENT_DEV_OPEN, &dev->flags);

	netif_start_queue(net);

	dev->link_on = false;

	lan78xx_defer_kevent(dev, EVENT_LINK_RESET);
done:
	usb_autopm_put_interface(dev->intf);

out:
	return ret;
}

static void lan78xx_terminate_urbs(struct lan78xx_net *dev)
{
	DECLARE_WAIT_QUEUE_HEAD_ONSTACK(unlink_wakeup);
	DECLARE_WAITQUEUE(wait, current);
	int temp;

	/* ensure there are no more active urbs */
	add_wait_queue(&unlink_wakeup, &wait);
	set_current_state(TASK_UNINTERRUPTIBLE);
	dev->wait = &unlink_wakeup;
	temp = unlink_urbs(dev, &dev->txq) + unlink_urbs(dev, &dev->rxq);

	/* maybe wait for deletions to finish. */
	while (!skb_queue_empty(&dev->rxq) &&
	       !skb_queue_empty(&dev->txq) &&
	       !skb_queue_empty(&dev->done)) {
		schedule_timeout(msecs_to_jiffies(UNLINK_TIMEOUT_MS));
		set_current_state(TASK_UNINTERRUPTIBLE);
		netif_dbg(dev, ifdown, dev->net,
			  "waited for %d urb completions\n", temp);
	}
	set_current_state(TASK_RUNNING);
	dev->wait = NULL;
	remove_wait_queue(&unlink_wakeup, &wait);
}

static int lan78xx_stop(struct net_device *net)
{
	struct lan78xx_net *dev = netdev_priv(net);

	if (timer_pending(&dev->stat_monitor))
		del_timer_sync(&dev->stat_monitor);

	if (net->phydev)
		phy_stop(net->phydev);

	clear_bit(EVENT_DEV_OPEN, &dev->flags);
	netif_stop_queue(net);

	netif_info(dev, ifdown, dev->net,
		   "stop stats: rx/tx %lu/%lu, errs %lu/%lu\n",
		   net->stats.rx_packets, net->stats.tx_packets,
		   net->stats.rx_errors, net->stats.tx_errors);

	lan78xx_terminate_urbs(dev);

	usb_kill_urb(dev->urb_intr);

	skb_queue_purge(&dev->rxq_pause);

	/* deferred work (task, timer, softirq) must also stop.
	 * can't flush_scheduled_work() until we drop rtnl (later),
	 * else workers could deadlock; so make workers a NOP.
	 */
	dev->flags = 0;
	cancel_delayed_work_sync(&dev->wq);
	tasklet_kill(&dev->bh);

	usb_autopm_put_interface(dev->intf);

	return 0;
}

static int lan78xx_linearize(struct sk_buff *skb)
{
	return skb_linearize(skb);
}

static struct sk_buff *lan78xx_tx_prep(struct lan78xx_net *dev,
				       struct sk_buff *skb, gfp_t flags)
{
	u32 tx_cmd_a, tx_cmd_b;
	void *ptr;

	if (skb_cow_head(skb, TX_OVERHEAD)) {
		dev_kfree_skb_any(skb);
		return NULL;
	}

	if (lan78xx_linearize(skb) < 0)
		return NULL;

	tx_cmd_a = (u32)(skb->len & TX_CMD_A_LEN_MASK_) | TX_CMD_A_FCS_;

	if (skb->ip_summed == CHECKSUM_PARTIAL)
		tx_cmd_a |= TX_CMD_A_IPE_ | TX_CMD_A_TPE_;

	tx_cmd_b = 0;
	if (skb_is_gso(skb)) {
		u16 mss = max(skb_shinfo(skb)->gso_size, TX_CMD_B_MSS_MIN_);

		tx_cmd_b = (mss << TX_CMD_B_MSS_SHIFT_) & TX_CMD_B_MSS_MASK_;

		tx_cmd_a |= TX_CMD_A_LSO_;
	}

	if (skb_vlan_tag_present(skb)) {
		tx_cmd_a |= TX_CMD_A_IVTG_;
		tx_cmd_b |= skb_vlan_tag_get(skb) & TX_CMD_B_VTAG_MASK_;
	}

	ptr = skb_push(skb, 8);
	put_unaligned_le32(tx_cmd_a, ptr);
	put_unaligned_le32(tx_cmd_b, ptr + 4);

	return skb;
}

static enum skb_state defer_bh(struct lan78xx_net *dev, struct sk_buff *skb,
			       struct sk_buff_head *list, enum skb_state state)
{
	unsigned long flags;
	enum skb_state old_state;
	struct skb_data *entry = (struct skb_data *)skb->cb;

	spin_lock_irqsave(&list->lock, flags);
	old_state = entry->state;
	entry->state = state;

	__skb_unlink(skb, list);
	spin_unlock(&list->lock);
	spin_lock(&dev->done.lock);

	__skb_queue_tail(&dev->done, skb);
	if (skb_queue_len(&dev->done) == 1)
		tasklet_schedule(&dev->bh);
	spin_unlock_irqrestore(&dev->done.lock, flags);

	return old_state;
}

static void tx_complete(struct urb *urb)
{
	struct sk_buff *skb = (struct sk_buff *)urb->context;
	struct skb_data *entry = (struct skb_data *)skb->cb;
	struct lan78xx_net *dev = entry->dev;

	if (urb->status == 0) {
		dev->net->stats.tx_packets += entry->num_of_packet;
		dev->net->stats.tx_bytes += entry->length;
	} else {
		dev->net->stats.tx_errors++;

		switch (urb->status) {
		case -EPIPE:
			lan78xx_defer_kevent(dev, EVENT_TX_HALT);
			break;

		/* software-driven interface shutdown */
		case -ECONNRESET:
		case -ESHUTDOWN:
			break;

		case -EPROTO:
		case -ETIME:
		case -EILSEQ:
			netif_stop_queue(dev->net);
			break;
		default:
			netif_dbg(dev, tx_err, dev->net,
				  "tx err %d\n", entry->urb->status);
			break;
		}
	}

	usb_autopm_put_interface_async(dev->intf);

	defer_bh(dev, skb, &dev->txq, tx_done);
}

static void lan78xx_queue_skb(struct sk_buff_head *list,
			      struct sk_buff *newsk, enum skb_state state)
{
	struct skb_data *entry = (struct skb_data *)newsk->cb;

	__skb_queue_tail(list, newsk);
	entry->state = state;
}

static netdev_tx_t
lan78xx_start_xmit(struct sk_buff *skb, struct net_device *net)
{
	struct lan78xx_net *dev = netdev_priv(net);
	struct sk_buff *skb2 = NULL;

	if (skb) {
		skb_tx_timestamp(skb);
		skb2 = lan78xx_tx_prep(dev, skb, GFP_ATOMIC);
	}

	if (skb2) {
		skb_queue_tail(&dev->txq_pend, skb2);

		/* throttle TX patch at slower than SUPER SPEED USB */
		if ((dev->udev->speed < USB_SPEED_SUPER) &&
		    (skb_queue_len(&dev->txq_pend) > 10))
			netif_stop_queue(net);
	} else {
		netif_dbg(dev, tx_err, dev->net,
			  "lan78xx_tx_prep return NULL\n");
		dev->net->stats.tx_errors++;
		dev->net->stats.tx_dropped++;
	}

	tasklet_schedule(&dev->bh);

	return NETDEV_TX_OK;
}

static int
lan78xx_get_endpoints(struct lan78xx_net *dev, struct usb_interface *intf)
{
	int tmp;
	struct usb_host_interface *alt = NULL;
	struct usb_host_endpoint *in = NULL, *out = NULL;
	struct usb_host_endpoint *status = NULL;

	for (tmp = 0; tmp < intf->num_altsetting; tmp++) {
		unsigned ep;

		in = NULL;
		out = NULL;
		status = NULL;
		alt = intf->altsetting + tmp;

		for (ep = 0; ep < alt->desc.bNumEndpoints; ep++) {
			struct usb_host_endpoint *e;
			int intr = 0;

			e = alt->endpoint + ep;
			switch (e->desc.bmAttributes) {
			case USB_ENDPOINT_XFER_INT:
				if (!usb_endpoint_dir_in(&e->desc))
					continue;
				intr = 1;
				/* FALLTHROUGH */
			case USB_ENDPOINT_XFER_BULK:
				break;
			default:
				continue;
			}
			if (usb_endpoint_dir_in(&e->desc)) {
				if (!intr && !in)
					in = e;
				else if (intr && !status)
					status = e;
			} else {
				if (!out)
					out = e;
			}
		}
		if (in && out)
			break;
	}
	if (!alt || !in || !out)
		return -EINVAL;

	dev->pipe_in = usb_rcvbulkpipe(dev->udev,
				       in->desc.bEndpointAddress &
				       USB_ENDPOINT_NUMBER_MASK);
	dev->pipe_out = usb_sndbulkpipe(dev->udev,
					out->desc.bEndpointAddress &
					USB_ENDPOINT_NUMBER_MASK);
	dev->ep_intr = status;

	return 0;
}

static int lan78xx_bind(struct lan78xx_net *dev, struct usb_interface *intf)
{
	struct lan78xx_priv *pdata = NULL;
	int ret;
	int i;

	ret = lan78xx_get_endpoints(dev, intf);
	if (ret) {
		netdev_warn(dev->net, "lan78xx_get_endpoints failed: %d\n",
			    ret);
		return ret;
	}

	dev->data[0] = (unsigned long)kzalloc(sizeof(*pdata), GFP_KERNEL);

	pdata = (struct lan78xx_priv *)(dev->data[0]);
	if (!pdata) {
		netdev_warn(dev->net, "Unable to allocate lan78xx_priv");
		return -ENOMEM;
	}

	pdata->dev = dev;

	spin_lock_init(&pdata->rfe_ctl_lock);
	mutex_init(&pdata->dataport_mutex);

	INIT_WORK(&pdata->set_multicast, lan78xx_deferred_multicast_write);

	for (i = 0; i < DP_SEL_VHF_VLAN_LEN; i++)
		pdata->vlan_table[i] = 0;

	INIT_WORK(&pdata->set_vlan, lan78xx_deferred_vlan_write);

	dev->net->features = 0;

	if (DEFAULT_TX_CSUM_ENABLE)
		dev->net->features |= NETIF_F_HW_CSUM;

	if (DEFAULT_RX_CSUM_ENABLE)
		dev->net->features |= NETIF_F_RXCSUM;

	if (DEFAULT_TSO_CSUM_ENABLE)
		dev->net->features |= NETIF_F_TSO | NETIF_F_TSO6 | NETIF_F_SG;

	if (DEFAULT_VLAN_RX_OFFLOAD)
		dev->net->features |= NETIF_F_HW_VLAN_CTAG_RX;

	if (DEFAULT_VLAN_FILTER_ENABLE)
		dev->net->features |= NETIF_F_HW_VLAN_CTAG_FILTER;

	dev->net->hw_features = dev->net->features;

	ret = lan78xx_setup_irq_domain(dev);
	if (ret < 0) {
		netdev_warn(dev->net,
			    "lan78xx_setup_irq_domain() failed : %d", ret);
		goto out1;
	}

	dev->net->hard_header_len += TX_OVERHEAD;
	dev->hard_mtu = dev->net->mtu + dev->net->hard_header_len;

	/* Init all registers */
	ret = lan78xx_reset(dev);
	if (ret) {
		netdev_warn(dev->net, "Registers INIT FAILED....");
		goto out2;
	}

	ret = lan78xx_mdio_init(dev);
	if (ret) {
		netdev_warn(dev->net, "MDIO INIT FAILED.....");
		goto out2;
	}

	dev->net->flags |= IFF_MULTICAST;

	pdata->wol = WAKE_MAGIC;

	return ret;

out2:
	lan78xx_remove_irq_domain(dev);

out1:
	netdev_warn(dev->net, "Bind routine FAILED");
	cancel_work_sync(&pdata->set_multicast);
	cancel_work_sync(&pdata->set_vlan);
	kfree(pdata);
	return ret;
}

static void lan78xx_unbind(struct lan78xx_net *dev, struct usb_interface *intf)
{
	struct lan78xx_priv *pdata = (struct lan78xx_priv *)(dev->data[0]);

	lan78xx_remove_irq_domain(dev);

	lan78xx_remove_mdio(dev);

	if (pdata) {
		cancel_work_sync(&pdata->set_multicast);
		cancel_work_sync(&pdata->set_vlan);
		netif_dbg(dev, ifdown, dev->net, "free pdata");
		kfree(pdata);
		pdata = NULL;
		dev->data[0] = 0;
	}
}

static void lan78xx_rx_csum_offload(struct lan78xx_net *dev,
				    struct sk_buff *skb,
				    u32 rx_cmd_a, u32 rx_cmd_b)
{
	/* HW Checksum offload appears to be flawed if used when not stripping
	 * VLAN headers. Drop back to S/W checksums under these conditions.
	 */
	if (!(dev->net->features & NETIF_F_RXCSUM) ||
	    unlikely(rx_cmd_a & RX_CMD_A_ICSM_) ||
	    ((rx_cmd_a & RX_CMD_A_FVTG_) &&
	     !(dev->net->features & NETIF_F_HW_VLAN_CTAG_RX))) {
		skb->ip_summed = CHECKSUM_NONE;
	} else {
		skb->csum = ntohs((u16)(rx_cmd_b >> RX_CMD_B_CSUM_SHIFT_));
		skb->ip_summed = CHECKSUM_COMPLETE;
	}
}

static void lan78xx_rx_vlan_offload(struct lan78xx_net *dev,
				    struct sk_buff *skb,
				    u32 rx_cmd_a, u32 rx_cmd_b)
{
	if ((dev->net->features & NETIF_F_HW_VLAN_CTAG_RX) &&
	    (rx_cmd_a & RX_CMD_A_FVTG_))
		__vlan_hwaccel_put_tag(skb, htons(ETH_P_8021Q),
				       (rx_cmd_b & 0xffff));
}

static void lan78xx_skb_return(struct lan78xx_net *dev, struct sk_buff *skb)
{
	int status;

	if (test_bit(EVENT_RX_PAUSED, &dev->flags)) {
		skb_queue_tail(&dev->rxq_pause, skb);
		return;
	}

	dev->net->stats.rx_packets++;
	dev->net->stats.rx_bytes += skb->len;

	skb->protocol = eth_type_trans(skb, dev->net);

	netif_dbg(dev, rx_status, dev->net, "< rx, len %zu, type 0x%x\n",
		  skb->len + sizeof(struct ethhdr), skb->protocol);
	memset(skb->cb, 0, sizeof(struct skb_data));

	if (skb_defer_rx_timestamp(skb))
		return;

	status = netif_rx(skb);
	if (status != NET_RX_SUCCESS)
		netif_dbg(dev, rx_err, dev->net,
			  "netif_rx status %d\n", status);
}

static int lan78xx_rx(struct lan78xx_net *dev, struct sk_buff *skb)
{
	if (skb->len < dev->net->hard_header_len)
		return 0;

	while (skb->len > 0) {
		u32 rx_cmd_a, rx_cmd_b, align_count, size;
		u16 rx_cmd_c;
		struct sk_buff *skb2;
		unsigned char *packet;

		rx_cmd_a = get_unaligned_le32(skb->data);
		skb_pull(skb, sizeof(rx_cmd_a));

		rx_cmd_b = get_unaligned_le32(skb->data);
		skb_pull(skb, sizeof(rx_cmd_b));

		rx_cmd_c = get_unaligned_le16(skb->data);
		skb_pull(skb, sizeof(rx_cmd_c));

		packet = skb->data;

		/* get the packet length */
		size = (rx_cmd_a & RX_CMD_A_LEN_MASK_);
		align_count = (4 - ((size + RXW_PADDING) % 4)) % 4;

		if (unlikely(rx_cmd_a & RX_CMD_A_RED_)) {
			netif_dbg(dev, rx_err, dev->net,
				  "Error rx_cmd_a=0x%08x", rx_cmd_a);
		} else {
			/* last frame in this batch */
			if (skb->len == size) {
				lan78xx_rx_csum_offload(dev, skb,
							rx_cmd_a, rx_cmd_b);
				lan78xx_rx_vlan_offload(dev, skb,
							rx_cmd_a, rx_cmd_b);

				skb_trim(skb, skb->len - 4); /* remove fcs */
				skb->truesize = size + sizeof(struct sk_buff);

				return 1;
			}

			skb2 = skb_clone(skb, GFP_ATOMIC);
			if (unlikely(!skb2)) {
				netdev_warn(dev->net, "Error allocating skb");
				return 0;
			}

			skb2->len = size;
			skb2->data = packet;
			skb_set_tail_pointer(skb2, size);

			lan78xx_rx_csum_offload(dev, skb2, rx_cmd_a, rx_cmd_b);
			lan78xx_rx_vlan_offload(dev, skb2, rx_cmd_a, rx_cmd_b);

			skb_trim(skb2, skb2->len - 4); /* remove fcs */
			skb2->truesize = size + sizeof(struct sk_buff);

			lan78xx_skb_return(dev, skb2);
		}

		skb_pull(skb, size);

		/* padding bytes before the next frame starts */
		if (skb->len)
			skb_pull(skb, align_count);
	}

	return 1;
}

static inline void rx_process(struct lan78xx_net *dev, struct sk_buff *skb)
{
	if (!lan78xx_rx(dev, skb)) {
		dev->net->stats.rx_errors++;
		goto done;
	}

	if (skb->len) {
		lan78xx_skb_return(dev, skb);
		return;
	}

	netif_dbg(dev, rx_err, dev->net, "drop\n");
	dev->net->stats.rx_errors++;
done:
	skb_queue_tail(&dev->done, skb);
}

static void rx_complete(struct urb *urb);

static int rx_submit(struct lan78xx_net *dev, struct urb *urb, gfp_t flags)
{
	struct sk_buff *skb;
	struct skb_data *entry;
	unsigned long lockflags;
	size_t size = dev->rx_urb_size;
	int ret = 0;

	skb = netdev_alloc_skb_ip_align(dev->net, size);
	if (!skb) {
		usb_free_urb(urb);
		return -ENOMEM;
	}

	entry = (struct skb_data *)skb->cb;
	entry->urb = urb;
	entry->dev = dev;
	entry->length = 0;

	usb_fill_bulk_urb(urb, dev->udev, dev->pipe_in,
			  skb->data, size, rx_complete, skb);

	spin_lock_irqsave(&dev->rxq.lock, lockflags);

	if (netif_device_present(dev->net) &&
	    netif_running(dev->net) &&
	    !test_bit(EVENT_RX_HALT, &dev->flags) &&
	    !test_bit(EVENT_DEV_ASLEEP, &dev->flags)) {
		ret = usb_submit_urb(urb, GFP_ATOMIC);
		switch (ret) {
		case 0:
			lan78xx_queue_skb(&dev->rxq, skb, rx_start);
			break;
		case -EPIPE:
			lan78xx_defer_kevent(dev, EVENT_RX_HALT);
			break;
		case -ENODEV:
			netif_dbg(dev, ifdown, dev->net, "device gone\n");
			netif_device_detach(dev->net);
			break;
		case -EHOSTUNREACH:
			ret = -ENOLINK;
			break;
		default:
			netif_dbg(dev, rx_err, dev->net,
				  "rx submit, %d\n", ret);
			tasklet_schedule(&dev->bh);
		}
	} else {
		netif_dbg(dev, ifdown, dev->net, "rx: stopped\n");
		ret = -ENOLINK;
	}
	spin_unlock_irqrestore(&dev->rxq.lock, lockflags);
	if (ret) {
		dev_kfree_skb_any(skb);
		usb_free_urb(urb);
	}
	return ret;
}

static void rx_complete(struct urb *urb)
{
	struct sk_buff	*skb = (struct sk_buff *)urb->context;
	struct skb_data	*entry = (struct skb_data *)skb->cb;
	struct lan78xx_net *dev = entry->dev;
	int urb_status = urb->status;
	enum skb_state state;

	skb_put(skb, urb->actual_length);
	state = rx_done;
	entry->urb = NULL;

	switch (urb_status) {
	case 0:
		if (skb->len < dev->net->hard_header_len) {
			state = rx_cleanup;
			dev->net->stats.rx_errors++;
			dev->net->stats.rx_length_errors++;
			netif_dbg(dev, rx_err, dev->net,
				  "rx length %d\n", skb->len);
		}
		usb_mark_last_busy(dev->udev);
		break;
	case -EPIPE:
		dev->net->stats.rx_errors++;
		lan78xx_defer_kevent(dev, EVENT_RX_HALT);
		/* FALLTHROUGH */
	case -ECONNRESET:				/* async unlink */
	case -ESHUTDOWN:				/* hardware gone */
		netif_dbg(dev, ifdown, dev->net,
			  "rx shutdown, code %d\n", urb_status);
		state = rx_cleanup;
		entry->urb = urb;
		urb = NULL;
		break;
	case -EPROTO:
	case -ETIME:
	case -EILSEQ:
		dev->net->stats.rx_errors++;
		state = rx_cleanup;
		entry->urb = urb;
		urb = NULL;
		break;

	/* data overrun ... flush fifo? */
	case -EOVERFLOW:
		dev->net->stats.rx_over_errors++;
		/* FALLTHROUGH */

	default:
		state = rx_cleanup;
		dev->net->stats.rx_errors++;
		netif_dbg(dev, rx_err, dev->net, "rx status %d\n", urb_status);
		break;
	}

	state = defer_bh(dev, skb, &dev->rxq, state);

	if (urb) {
		if (netif_running(dev->net) &&
		    !test_bit(EVENT_RX_HALT, &dev->flags) &&
		    state != unlink_start) {
			rx_submit(dev, urb, GFP_ATOMIC);
			return;
		}
		usb_free_urb(urb);
	}
	netif_dbg(dev, rx_err, dev->net, "no read resubmitted\n");
}

static void lan78xx_tx_bh(struct lan78xx_net *dev)
{
	int length;
	struct urb *urb = NULL;
	struct skb_data *entry;
	unsigned long flags;
	struct sk_buff_head *tqp = &dev->txq_pend;
	struct sk_buff *skb, *skb2;
	int ret;
	int count, pos;
	int skb_totallen, pkt_cnt;

	skb_totallen = 0;
	pkt_cnt = 0;
	count = 0;
	length = 0;
	spin_lock_irqsave(&tqp->lock, flags);
	skb_queue_walk(tqp, skb) {
		if (skb_is_gso(skb)) {
			if (!skb_queue_is_first(tqp, skb)) {
				/* handle previous packets first */
				break;
			}
			count = 1;
			length = skb->len - TX_OVERHEAD;
			__skb_unlink(skb, tqp);
			spin_unlock_irqrestore(&tqp->lock, flags);
			goto gso_skb;
		}

		if ((skb_totallen + skb->len) > MAX_SINGLE_PACKET_SIZE)
			break;
		skb_totallen = skb->len + roundup(skb_totallen, sizeof(u32));
		pkt_cnt++;
	}
	spin_unlock_irqrestore(&tqp->lock, flags);

	/* copy to a single skb */
	skb = alloc_skb(skb_totallen, GFP_ATOMIC);
	if (!skb)
		goto drop;

	skb_put(skb, skb_totallen);

	for (count = pos = 0; count < pkt_cnt; count++) {
		skb2 = skb_dequeue(tqp);
		if (skb2) {
			length += (skb2->len - TX_OVERHEAD);
			memcpy(skb->data + pos, skb2->data, skb2->len);
			pos += roundup(skb2->len, sizeof(u32));
			dev_kfree_skb(skb2);
		}
	}

gso_skb:
	urb = usb_alloc_urb(0, GFP_ATOMIC);
	if (!urb)
		goto drop;

	entry = (struct skb_data *)skb->cb;
	entry->urb = urb;
	entry->dev = dev;
	entry->length = length;
	entry->num_of_packet = count;

	spin_lock_irqsave(&dev->txq.lock, flags);
	ret = usb_autopm_get_interface_async(dev->intf);
	if (ret < 0) {
		spin_unlock_irqrestore(&dev->txq.lock, flags);
		goto drop;
	}

	usb_fill_bulk_urb(urb, dev->udev, dev->pipe_out,
			  skb->data, skb->len, tx_complete, skb);

	if (length % dev->maxpacket == 0) {
		/* send USB_ZERO_PACKET */
		urb->transfer_flags |= URB_ZERO_PACKET;
	}

#ifdef CONFIG_PM
	/* if this triggers the device is still a sleep */
	if (test_bit(EVENT_DEV_ASLEEP, &dev->flags)) {
		/* transmission will be done in resume */
		usb_anchor_urb(urb, &dev->deferred);
		/* no use to process more packets */
		netif_stop_queue(dev->net);
		usb_put_urb(urb);
		spin_unlock_irqrestore(&dev->txq.lock, flags);
		netdev_dbg(dev->net, "Delaying transmission for resumption\n");
		return;
	}
#endif

	ret = usb_submit_urb(urb, GFP_ATOMIC);
	switch (ret) {
	case 0:
		netif_trans_update(dev->net);
		lan78xx_queue_skb(&dev->txq, skb, tx_start);
		if (skb_queue_len(&dev->txq) >= dev->tx_qlen)
			netif_stop_queue(dev->net);
		break;
	case -EPIPE:
		netif_stop_queue(dev->net);
		lan78xx_defer_kevent(dev, EVENT_TX_HALT);
		usb_autopm_put_interface_async(dev->intf);
		break;
	default:
		usb_autopm_put_interface_async(dev->intf);
		netif_dbg(dev, tx_err, dev->net,
			  "tx: submit urb err %d\n", ret);
		break;
	}

	spin_unlock_irqrestore(&dev->txq.lock, flags);

	if (ret) {
		netif_dbg(dev, tx_err, dev->net, "drop, code %d\n", ret);
drop:
		dev->net->stats.tx_dropped++;
		if (skb)
			dev_kfree_skb_any(skb);
		usb_free_urb(urb);
	} else
		netif_dbg(dev, tx_queued, dev->net,
			  "> tx, len %d, type 0x%x\n", length, skb->protocol);
}

static void lan78xx_rx_bh(struct lan78xx_net *dev)
{
	struct urb *urb;
	int i;

	if (skb_queue_len(&dev->rxq) < dev->rx_qlen) {
		for (i = 0; i < 10; i++) {
			if (skb_queue_len(&dev->rxq) >= dev->rx_qlen)
				break;
			urb = usb_alloc_urb(0, GFP_ATOMIC);
			if (urb)
				if (rx_submit(dev, urb, GFP_ATOMIC) == -ENOLINK)
					return;
		}

		if (skb_queue_len(&dev->rxq) < dev->rx_qlen)
			tasklet_schedule(&dev->bh);
	}
	if (skb_queue_len(&dev->txq) < dev->tx_qlen)
		netif_wake_queue(dev->net);
}

static void lan78xx_bh(unsigned long param)
{
	struct lan78xx_net *dev = (struct lan78xx_net *)param;
	struct sk_buff *skb;
	struct skb_data *entry;

	while ((skb = skb_dequeue(&dev->done))) {
		entry = (struct skb_data *)(skb->cb);
		switch (entry->state) {
		case rx_done:
			entry->state = rx_cleanup;
			rx_process(dev, skb);
			continue;
		case tx_done:
			usb_free_urb(entry->urb);
			dev_kfree_skb(skb);
			continue;
		case rx_cleanup:
			usb_free_urb(entry->urb);
			dev_kfree_skb(skb);
			continue;
		default:
			netdev_dbg(dev->net, "skb state %d\n", entry->state);
			return;
		}
	}

	if (netif_device_present(dev->net) && netif_running(dev->net)) {
		/* reset update timer delta */
		if (timer_pending(&dev->stat_monitor) && (dev->delta != 1)) {
			dev->delta = 1;
			mod_timer(&dev->stat_monitor,
				  jiffies + STAT_UPDATE_TIMER);
		}

		if (!skb_queue_empty(&dev->txq_pend))
			lan78xx_tx_bh(dev);

		if (!timer_pending(&dev->delay) &&
		    !test_bit(EVENT_RX_HALT, &dev->flags))
			lan78xx_rx_bh(dev);
	}
}

static void lan78xx_delayedwork(struct work_struct *work)
{
	int status;
	struct lan78xx_net *dev;

	dev = container_of(work, struct lan78xx_net, wq.work);

	if (test_bit(EVENT_TX_HALT, &dev->flags)) {
		unlink_urbs(dev, &dev->txq);
		status = usb_autopm_get_interface(dev->intf);
		if (status < 0)
			goto fail_pipe;
		status = usb_clear_halt(dev->udev, dev->pipe_out);
		usb_autopm_put_interface(dev->intf);
		if (status < 0 &&
		    status != -EPIPE &&
		    status != -ESHUTDOWN) {
			if (netif_msg_tx_err(dev))
fail_pipe:
				netdev_err(dev->net,
					   "can't clear tx halt, status %d\n",
					   status);
		} else {
			clear_bit(EVENT_TX_HALT, &dev->flags);
			if (status != -ESHUTDOWN)
				netif_wake_queue(dev->net);
		}
	}
	if (test_bit(EVENT_RX_HALT, &dev->flags)) {
		unlink_urbs(dev, &dev->rxq);
		status = usb_autopm_get_interface(dev->intf);
		if (status < 0)
				goto fail_halt;
		status = usb_clear_halt(dev->udev, dev->pipe_in);
		usb_autopm_put_interface(dev->intf);
		if (status < 0 &&
		    status != -EPIPE &&
		    status != -ESHUTDOWN) {
			if (netif_msg_rx_err(dev))
fail_halt:
				netdev_err(dev->net,
					   "can't clear rx halt, status %d\n",
					   status);
		} else {
			clear_bit(EVENT_RX_HALT, &dev->flags);
			tasklet_schedule(&dev->bh);
		}
	}

	if (test_bit(EVENT_LINK_RESET, &dev->flags)) {
		int ret = 0;

		clear_bit(EVENT_LINK_RESET, &dev->flags);
		status = usb_autopm_get_interface(dev->intf);
		if (status < 0)
			goto skip_reset;
		if (lan78xx_link_reset(dev) < 0) {
			usb_autopm_put_interface(dev->intf);
skip_reset:
			netdev_info(dev->net, "link reset failed (%d)\n",
				    ret);
		} else {
			usb_autopm_put_interface(dev->intf);
		}
	}

	if (test_bit(EVENT_STAT_UPDATE, &dev->flags)) {
		lan78xx_update_stats(dev);

		clear_bit(EVENT_STAT_UPDATE, &dev->flags);

		mod_timer(&dev->stat_monitor,
			  jiffies + (STAT_UPDATE_TIMER * dev->delta));

		dev->delta = min((dev->delta * 2), 50);
	}
}

static void intr_complete(struct urb *urb)
{
	struct lan78xx_net *dev = urb->context;
	int status = urb->status;

	switch (status) {
	/* success */
	case 0:
		lan78xx_status(dev, urb);
		break;

	/* software-driven interface shutdown */
	case -ENOENT:			/* urb killed */
	case -ESHUTDOWN:		/* hardware gone */
		netif_dbg(dev, ifdown, dev->net,
			  "intr shutdown, code %d\n", status);
		return;

	/* NOTE:  not throttling like RX/TX, since this endpoint
	 * already polls infrequently
	 */
	default:
		netdev_dbg(dev->net, "intr status %d\n", status);
		break;
	}

	if (!netif_running(dev->net))
		return;

	memset(urb->transfer_buffer, 0, urb->transfer_buffer_length);
	status = usb_submit_urb(urb, GFP_ATOMIC);
	if (status != 0)
		netif_err(dev, timer, dev->net,
			  "intr resubmit --> %d\n", status);
}

static void lan78xx_disconnect(struct usb_interface *intf)
{
	struct lan78xx_net *dev;
	struct usb_device *udev;
	struct net_device *net;
	struct phy_device *phydev;

	dev = usb_get_intfdata(intf);
	usb_set_intfdata(intf, NULL);
	if (!dev)
		return;

	udev = interface_to_usbdev(intf);
	net = dev->net;
	phydev = net->phydev;

	phy_unregister_fixup_for_uid(PHY_KSZ9031RNX, 0xfffffff0);
	phy_unregister_fixup_for_uid(PHY_LAN8835, 0xfffffff0);

	phy_disconnect(net->phydev);

	if (phy_is_pseudo_fixed_link(phydev))
		fixed_phy_unregister(phydev);

	unregister_netdev(net);

	cancel_delayed_work_sync(&dev->wq);

	usb_scuttle_anchored_urbs(&dev->deferred);

	lan78xx_unbind(dev, intf);

	usb_kill_urb(dev->urb_intr);
	usb_free_urb(dev->urb_intr);

	free_netdev(net);
	usb_put_dev(udev);
}

static void lan78xx_tx_timeout(struct net_device *net)
{
	struct lan78xx_net *dev = netdev_priv(net);

	unlink_urbs(dev, &dev->txq);
	tasklet_schedule(&dev->bh);
}

static const struct net_device_ops lan78xx_netdev_ops = {
	.ndo_open		= lan78xx_open,
	.ndo_stop		= lan78xx_stop,
	.ndo_start_xmit		= lan78xx_start_xmit,
	.ndo_tx_timeout		= lan78xx_tx_timeout,
	.ndo_change_mtu		= lan78xx_change_mtu,
	.ndo_set_mac_address	= lan78xx_set_mac_addr,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_do_ioctl		= lan78xx_ioctl,
	.ndo_set_rx_mode	= lan78xx_set_multicast,
	.ndo_set_features	= lan78xx_set_features,
	.ndo_vlan_rx_add_vid	= lan78xx_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid	= lan78xx_vlan_rx_kill_vid,
};

static void lan78xx_stat_monitor(struct timer_list *t)
{
	struct lan78xx_net *dev = from_timer(dev, t, stat_monitor);

	lan78xx_defer_kevent(dev, EVENT_STAT_UPDATE);
}

static int lan78xx_probe(struct usb_interface *intf,
			 const struct usb_device_id *id)
{
	struct lan78xx_net *dev;
	struct net_device *netdev;
	struct usb_device *udev;
	int ret;
	unsigned maxp;
	unsigned period;
	u8 *buf = NULL;

	udev = interface_to_usbdev(intf);
	udev = usb_get_dev(udev);

	netdev = alloc_etherdev(sizeof(struct lan78xx_net));
	if (!netdev) {
		dev_err(&intf->dev, "Error: OOM\n");
		ret = -ENOMEM;
		goto out1;
	}

	/* netdev_printk() needs this */
	SET_NETDEV_DEV(netdev, &intf->dev);

	dev = netdev_priv(netdev);
	dev->udev = udev;
	dev->intf = intf;
	dev->net = netdev;
	dev->msg_enable = netif_msg_init(msg_level, NETIF_MSG_DRV
					| NETIF_MSG_PROBE | NETIF_MSG_LINK);

	skb_queue_head_init(&dev->rxq);
	skb_queue_head_init(&dev->txq);
	skb_queue_head_init(&dev->done);
	skb_queue_head_init(&dev->rxq_pause);
	skb_queue_head_init(&dev->txq_pend);
	mutex_init(&dev->phy_mutex);

	tasklet_init(&dev->bh, lan78xx_bh, (unsigned long)dev);
	INIT_DELAYED_WORK(&dev->wq, lan78xx_delayedwork);
	init_usb_anchor(&dev->deferred);

	netdev->netdev_ops = &lan78xx_netdev_ops;
	netdev->watchdog_timeo = TX_TIMEOUT_JIFFIES;
	netdev->ethtool_ops = &lan78xx_ethtool_ops;

	dev->delta = 1;
	timer_setup(&dev->stat_monitor, lan78xx_stat_monitor, 0);

	mutex_init(&dev->stats.access_lock);

	ret = lan78xx_bind(dev, intf);
	if (ret < 0)
		goto out2;

	if (netdev->mtu > (dev->hard_mtu - netdev->hard_header_len))
		netdev->mtu = dev->hard_mtu - netdev->hard_header_len;

	/* MTU range: 68 - 9000 */
	netdev->max_mtu = MAX_SINGLE_PACKET_SIZE;

	dev->ep_blkin = (intf->cur_altsetting)->endpoint + 0;
	dev->ep_blkout = (intf->cur_altsetting)->endpoint + 1;
	dev->ep_intr = (intf->cur_altsetting)->endpoint + 2;

	dev->pipe_in = usb_rcvbulkpipe(udev, BULK_IN_PIPE);
	dev->pipe_out = usb_sndbulkpipe(udev, BULK_OUT_PIPE);

	dev->pipe_intr = usb_rcvintpipe(dev->udev,
					dev->ep_intr->desc.bEndpointAddress &
					USB_ENDPOINT_NUMBER_MASK);
	period = dev->ep_intr->desc.bInterval;

	maxp = usb_maxpacket(dev->udev, dev->pipe_intr, 0);
	buf = kmalloc(maxp, GFP_KERNEL);
	if (buf) {
		dev->urb_intr = usb_alloc_urb(0, GFP_KERNEL);
		if (!dev->urb_intr) {
			ret = -ENOMEM;
			kfree(buf);
			goto out3;
		} else {
			usb_fill_int_urb(dev->urb_intr, dev->udev,
					 dev->pipe_intr, buf, maxp,
					 intr_complete, dev, period);
		}
	}

	dev->maxpacket = usb_maxpacket(dev->udev, dev->pipe_out, 1);

	/* driver requires remote-wakeup capability during autosuspend. */
	intf->needs_remote_wakeup = 1;

	ret = register_netdev(netdev);
	if (ret != 0) {
		netif_err(dev, probe, netdev, "couldn't register the device\n");
		goto out3;
	}

	usb_set_intfdata(intf, dev);

	ret = device_set_wakeup_enable(&udev->dev, true);

	 /* Default delay of 2sec has more overhead than advantage.
	  * Set to 10sec as default.
	  */
	pm_runtime_set_autosuspend_delay(&udev->dev,
					 DEFAULT_AUTOSUSPEND_DELAY);

	ret = lan78xx_phy_init(dev);
	if (ret < 0)
		goto out4;

	return 0;

out4:
	unregister_netdev(netdev);
out3:
	lan78xx_unbind(dev, intf);
out2:
	free_netdev(netdev);
out1:
	usb_put_dev(udev);

	return ret;
}

static u16 lan78xx_wakeframe_crc16(const u8 *buf, int len)
{
	const u16 crc16poly = 0x8005;
	int i;
	u16 bit, crc, msb;
	u8 data;

	crc = 0xFFFF;
	for (i = 0; i < len; i++) {
		data = *buf++;
		for (bit = 0; bit < 8; bit++) {
			msb = crc >> 15;
			crc <<= 1;

			if (msb ^ (u16)(data & 1)) {
				crc ^= crc16poly;
				crc |= (u16)0x0001U;
			}
			data >>= 1;
		}
	}

	return crc;
}

static int lan78xx_set_suspend(struct lan78xx_net *dev, u32 wol)
{
	u32 buf;
	int ret;
	int mask_index;
	u16 crc;
	u32 temp_wucsr;
	u32 temp_pmt_ctl;
	const u8 ipv4_multicast[3] = { 0x01, 0x00, 0x5E };
	const u8 ipv6_multicast[3] = { 0x33, 0x33 };
	const u8 arp_type[2] = { 0x08, 0x06 };

	ret = lan78xx_read_reg(dev, MAC_TX, &buf);
	buf &= ~MAC_TX_TXEN_;
	ret = lan78xx_write_reg(dev, MAC_TX, buf);
	ret = lan78xx_read_reg(dev, MAC_RX, &buf);
	buf &= ~MAC_RX_RXEN_;
	ret = lan78xx_write_reg(dev, MAC_RX, buf);

	ret = lan78xx_write_reg(dev, WUCSR, 0);
	ret = lan78xx_write_reg(dev, WUCSR2, 0);
	ret = lan78xx_write_reg(dev, WK_SRC, 0xFFF1FF1FUL);

	temp_wucsr = 0;

	temp_pmt_ctl = 0;
	ret = lan78xx_read_reg(dev, PMT_CTL, &temp_pmt_ctl);
	temp_pmt_ctl &= ~PMT_CTL_RES_CLR_WKP_EN_;
	temp_pmt_ctl |= PMT_CTL_RES_CLR_WKP_STS_;

	for (mask_index = 0; mask_index < NUM_OF_WUF_CFG; mask_index++)
		ret = lan78xx_write_reg(dev, WUF_CFG(mask_index), 0);

	mask_index = 0;
	if (wol & WAKE_PHY) {
		temp_pmt_ctl |= PMT_CTL_PHY_WAKE_EN_;

		temp_pmt_ctl |= PMT_CTL_WOL_EN_;
		temp_pmt_ctl &= ~PMT_CTL_SUS_MODE_MASK_;
		temp_pmt_ctl |= PMT_CTL_SUS_MODE_0_;
	}
	if (wol & WAKE_MAGIC) {
		temp_wucsr |= WUCSR_MPEN_;

		temp_pmt_ctl |= PMT_CTL_WOL_EN_;
		temp_pmt_ctl &= ~PMT_CTL_SUS_MODE_MASK_;
		temp_pmt_ctl |= PMT_CTL_SUS_MODE_3_;
	}
	if (wol & WAKE_BCAST) {
		temp_wucsr |= WUCSR_BCST_EN_;

		temp_pmt_ctl |= PMT_CTL_WOL_EN_;
		temp_pmt_ctl &= ~PMT_CTL_SUS_MODE_MASK_;
		temp_pmt_ctl |= PMT_CTL_SUS_MODE_0_;
	}
	if (wol & WAKE_MCAST) {
		temp_wucsr |= WUCSR_WAKE_EN_;

		/* set WUF_CFG & WUF_MASK for IPv4 Multicast */
		crc = lan78xx_wakeframe_crc16(ipv4_multicast, 3);
		ret = lan78xx_write_reg(dev, WUF_CFG(mask_index),
					WUF_CFGX_EN_ |
					WUF_CFGX_TYPE_MCAST_ |
					(0 << WUF_CFGX_OFFSET_SHIFT_) |
					(crc & WUF_CFGX_CRC16_MASK_));

		ret = lan78xx_write_reg(dev, WUF_MASK0(mask_index), 7);
		ret = lan78xx_write_reg(dev, WUF_MASK1(mask_index), 0);
		ret = lan78xx_write_reg(dev, WUF_MASK2(mask_index), 0);
		ret = lan78xx_write_reg(dev, WUF_MASK3(mask_index), 0);
		mask_index++;

		/* for IPv6 Multicast */
		crc = lan78xx_wakeframe_crc16(ipv6_multicast, 2);
		ret = lan78xx_write_reg(dev, WUF_CFG(mask_index),
					WUF_CFGX_EN_ |
					WUF_CFGX_TYPE_MCAST_ |
					(0 << WUF_CFGX_OFFSET_SHIFT_) |
					(crc & WUF_CFGX_CRC16_MASK_));

		ret = lan78xx_write_reg(dev, WUF_MASK0(mask_index), 3);
		ret = lan78xx_write_reg(dev, WUF_MASK1(mask_index), 0);
		ret = lan78xx_write_reg(dev, WUF_MASK2(mask_index), 0);
		ret = lan78xx_write_reg(dev, WUF_MASK3(mask_index), 0);
		mask_index++;

		temp_pmt_ctl |= PMT_CTL_WOL_EN_;
		temp_pmt_ctl &= ~PMT_CTL_SUS_MODE_MASK_;
		temp_pmt_ctl |= PMT_CTL_SUS_MODE_0_;
	}
	if (wol & WAKE_UCAST) {
		temp_wucsr |= WUCSR_PFDA_EN_;

		temp_pmt_ctl |= PMT_CTL_WOL_EN_;
		temp_pmt_ctl &= ~PMT_CTL_SUS_MODE_MASK_;
		temp_pmt_ctl |= PMT_CTL_SUS_MODE_0_;
	}
	if (wol & WAKE_ARP) {
		temp_wucsr |= WUCSR_WAKE_EN_;

		/* set WUF_CFG & WUF_MASK
		 * for packettype (offset 12,13) = ARP (0x0806)
		 */
		crc = lan78xx_wakeframe_crc16(arp_type, 2);
		ret = lan78xx_write_reg(dev, WUF_CFG(mask_index),
					WUF_CFGX_EN_ |
					WUF_CFGX_TYPE_ALL_ |
					(0 << WUF_CFGX_OFFSET_SHIFT_) |
					(crc & WUF_CFGX_CRC16_MASK_));

		ret = lan78xx_write_reg(dev, WUF_MASK0(mask_index), 0x3000);
		ret = lan78xx_write_reg(dev, WUF_MASK1(mask_index), 0);
		ret = lan78xx_write_reg(dev, WUF_MASK2(mask_index), 0);
		ret = lan78xx_write_reg(dev, WUF_MASK3(mask_index), 0);
		mask_index++;

		temp_pmt_ctl |= PMT_CTL_WOL_EN_;
		temp_pmt_ctl &= ~PMT_CTL_SUS_MODE_MASK_;
		temp_pmt_ctl |= PMT_CTL_SUS_MODE_0_;
	}

	ret = lan78xx_write_reg(dev, WUCSR, temp_wucsr);

	/* when multiple WOL bits are set */
	if (hweight_long((unsigned long)wol) > 1) {
		temp_pmt_ctl |= PMT_CTL_WOL_EN_;
		temp_pmt_ctl &= ~PMT_CTL_SUS_MODE_MASK_;
		temp_pmt_ctl |= PMT_CTL_SUS_MODE_0_;
	}
	ret = lan78xx_write_reg(dev, PMT_CTL, temp_pmt_ctl);

	/* clear WUPS */
	ret = lan78xx_read_reg(dev, PMT_CTL, &buf);
	buf |= PMT_CTL_WUPS_MASK_;
	ret = lan78xx_write_reg(dev, PMT_CTL, buf);

	ret = lan78xx_read_reg(dev, MAC_RX, &buf);
	buf |= MAC_RX_RXEN_;
	ret = lan78xx_write_reg(dev, MAC_RX, buf);

	return 0;
}

static int lan78xx_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct lan78xx_net *dev = usb_get_intfdata(intf);
	struct lan78xx_priv *pdata = (struct lan78xx_priv *)(dev->data[0]);
	u32 buf;
	int ret;
	int event;

	event = message.event;

	if (!dev->suspend_count++) {
		spin_lock_irq(&dev->txq.lock);
		/* don't autosuspend while transmitting */
		if ((skb_queue_len(&dev->txq) ||
		     skb_queue_len(&dev->txq_pend)) &&
			PMSG_IS_AUTO(message)) {
			spin_unlock_irq(&dev->txq.lock);
			ret = -EBUSY;
			goto out;
		} else {
			set_bit(EVENT_DEV_ASLEEP, &dev->flags);
			spin_unlock_irq(&dev->txq.lock);
		}

		/* stop TX & RX */
		ret = lan78xx_read_reg(dev, MAC_TX, &buf);
		buf &= ~MAC_TX_TXEN_;
		ret = lan78xx_write_reg(dev, MAC_TX, buf);
		ret = lan78xx_read_reg(dev, MAC_RX, &buf);
		buf &= ~MAC_RX_RXEN_;
		ret = lan78xx_write_reg(dev, MAC_RX, buf);

		/* empty out the rx and queues */
		netif_device_detach(dev->net);
		lan78xx_terminate_urbs(dev);
		usb_kill_urb(dev->urb_intr);

		/* reattach */
		netif_device_attach(dev->net);
	}

	if (test_bit(EVENT_DEV_ASLEEP, &dev->flags)) {
		del_timer(&dev->stat_monitor);

		if (PMSG_IS_AUTO(message)) {
			/* auto suspend (selective suspend) */
			ret = lan78xx_read_reg(dev, MAC_TX, &buf);
			buf &= ~MAC_TX_TXEN_;
			ret = lan78xx_write_reg(dev, MAC_TX, buf);
			ret = lan78xx_read_reg(dev, MAC_RX, &buf);
			buf &= ~MAC_RX_RXEN_;
			ret = lan78xx_write_reg(dev, MAC_RX, buf);

			ret = lan78xx_write_reg(dev, WUCSR, 0);
			ret = lan78xx_write_reg(dev, WUCSR2, 0);
			ret = lan78xx_write_reg(dev, WK_SRC, 0xFFF1FF1FUL);

			/* set goodframe wakeup */
			ret = lan78xx_read_reg(dev, WUCSR, &buf);

			buf |= WUCSR_RFE_WAKE_EN_;
			buf |= WUCSR_STORE_WAKE_;

			ret = lan78xx_write_reg(dev, WUCSR, buf);

			ret = lan78xx_read_reg(dev, PMT_CTL, &buf);

			buf &= ~PMT_CTL_RES_CLR_WKP_EN_;
			buf |= PMT_CTL_RES_CLR_WKP_STS_;

			buf |= PMT_CTL_PHY_WAKE_EN_;
			buf |= PMT_CTL_WOL_EN_;
			buf &= ~PMT_CTL_SUS_MODE_MASK_;
			buf |= PMT_CTL_SUS_MODE_3_;

			ret = lan78xx_write_reg(dev, PMT_CTL, buf);

			ret = lan78xx_read_reg(dev, PMT_CTL, &buf);

			buf |= PMT_CTL_WUPS_MASK_;

			ret = lan78xx_write_reg(dev, PMT_CTL, buf);

			ret = lan78xx_read_reg(dev, MAC_RX, &buf);
			buf |= MAC_RX_RXEN_;
			ret = lan78xx_write_reg(dev, MAC_RX, buf);
		} else {
			lan78xx_set_suspend(dev, pdata->wol);
		}
	}

	ret = 0;
out:
	return ret;
}

static int lan78xx_resume(struct usb_interface *intf)
{
	struct lan78xx_net *dev = usb_get_intfdata(intf);
	struct sk_buff *skb;
	struct urb *res;
	int ret;
	u32 buf;

	if (!timer_pending(&dev->stat_monitor)) {
		dev->delta = 1;
		mod_timer(&dev->stat_monitor,
			  jiffies + STAT_UPDATE_TIMER);
	}

	if (!--dev->suspend_count) {
		/* resume interrupt URBs */
		if (dev->urb_intr && test_bit(EVENT_DEV_OPEN, &dev->flags))
				usb_submit_urb(dev->urb_intr, GFP_NOIO);

		spin_lock_irq(&dev->txq.lock);
		while ((res = usb_get_from_anchor(&dev->deferred))) {
			skb = (struct sk_buff *)res->context;
			ret = usb_submit_urb(res, GFP_ATOMIC);
			if (ret < 0) {
				dev_kfree_skb_any(skb);
				usb_free_urb(res);
				usb_autopm_put_interface_async(dev->intf);
			} else {
				netif_trans_update(dev->net);
				lan78xx_queue_skb(&dev->txq, skb, tx_start);
			}
		}

		clear_bit(EVENT_DEV_ASLEEP, &dev->flags);
		spin_unlock_irq(&dev->txq.lock);

		if (test_bit(EVENT_DEV_OPEN, &dev->flags)) {
			if (!(skb_queue_len(&dev->txq) >= dev->tx_qlen))
				netif_start_queue(dev->net);
			tasklet_schedule(&dev->bh);
		}
	}

	ret = lan78xx_write_reg(dev, WUCSR2, 0);
	ret = lan78xx_write_reg(dev, WUCSR, 0);
	ret = lan78xx_write_reg(dev, WK_SRC, 0xFFF1FF1FUL);

	ret = lan78xx_write_reg(dev, WUCSR2, WUCSR2_NS_RCD_ |
					     WUCSR2_ARP_RCD_ |
					     WUCSR2_IPV6_TCPSYN_RCD_ |
					     WUCSR2_IPV4_TCPSYN_RCD_);

	ret = lan78xx_write_reg(dev, WUCSR, WUCSR_EEE_TX_WAKE_ |
					    WUCSR_EEE_RX_WAKE_ |
					    WUCSR_PFDA_FR_ |
					    WUCSR_RFE_WAKE_FR_ |
					    WUCSR_WUFR_ |
					    WUCSR_MPR_ |
					    WUCSR_BCST_FR_);

	ret = lan78xx_read_reg(dev, MAC_TX, &buf);
	buf |= MAC_TX_TXEN_;
	ret = lan78xx_write_reg(dev, MAC_TX, buf);

	return 0;
}

static int lan78xx_reset_resume(struct usb_interface *intf)
{
	struct lan78xx_net *dev = usb_get_intfdata(intf);

	lan78xx_reset(dev);

	phy_start(dev->net->phydev);

	return lan78xx_resume(intf);
}

static const struct usb_device_id products[] = {
	{
	/* LAN7800 USB Gigabit Ethernet Device */
	USB_DEVICE(LAN78XX_USB_VENDOR_ID, LAN7800_USB_PRODUCT_ID),
	},
	{
	/* LAN7850 USB Gigabit Ethernet Device */
	USB_DEVICE(LAN78XX_USB_VENDOR_ID, LAN7850_USB_PRODUCT_ID),
	},
	{
	/* LAN7801 USB Gigabit Ethernet Device */
	USB_DEVICE(LAN78XX_USB_VENDOR_ID, LAN7801_USB_PRODUCT_ID),
	},
	{},
};
MODULE_DEVICE_TABLE(usb, products);

static struct usb_driver lan78xx_driver = {
	.name			= DRIVER_NAME,
	.id_table		= products,
	.probe			= lan78xx_probe,
	.disconnect		= lan78xx_disconnect,
	.suspend		= lan78xx_suspend,
	.resume			= lan78xx_resume,
	.reset_resume		= lan78xx_reset_resume,
	.supports_autosuspend	= 1,
	.disable_hub_initiated_lpm = 1,
};

module_usb_driver(lan78xx_driver);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
