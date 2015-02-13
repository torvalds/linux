/*
 * tg3.c: Broadcom Tigon3 ethernet driver.
 *
 * Copyright (C) 2001, 2002, 2003, 2004 David S. Miller (davem@redhat.com)
 * Copyright (C) 2001, 2002, 2003 Jeff Garzik (jgarzik@pobox.com)
 * Copyright (C) 2004 Sun Microsystems Inc.
 * Copyright (C) 2005-2013 Broadcom Corporation.
 *
 * Firmware is:
 *	Derived from proprietary unpublished source code,
 *	Copyright (C) 2000-2003 Broadcom Corporation.
 *
 *	Permission is hereby granted for the distribution of this firmware
 *	data in hexadecimal or equivalent format, provided this copyright
 *	notice is accompanying it.
 */


#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/stringify.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/compiler.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/in.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/ethtool.h>
#include <linux/mdio.h>
#include <linux/mii.h>
#include <linux/phy.h>
#include <linux/brcmphy.h>
#include <linux/if_vlan.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/workqueue.h>
#include <linux/prefetch.h>
#include <linux/dma-mapping.h>
#include <linux/firmware.h>
#include <linux/ssb/ssb_driver_gige.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>

#include <net/checksum.h>
#include <net/ip.h>

#include <linux/io.h>
#include <asm/byteorder.h>
#include <linux/uaccess.h>

#include <uapi/linux/net_tstamp.h>
#include <linux/ptp_clock_kernel.h>

#ifdef CONFIG_SPARC
#include <asm/idprom.h>
#include <asm/prom.h>
#endif

#define BAR_0	0
#define BAR_2	2

#include "tg3.h"

/* Functions & macros to verify TG3_FLAGS types */

static inline int _tg3_flag(enum TG3_FLAGS flag, unsigned long *bits)
{
	return test_bit(flag, bits);
}

static inline void _tg3_flag_set(enum TG3_FLAGS flag, unsigned long *bits)
{
	set_bit(flag, bits);
}

static inline void _tg3_flag_clear(enum TG3_FLAGS flag, unsigned long *bits)
{
	clear_bit(flag, bits);
}

#define tg3_flag(tp, flag)				\
	_tg3_flag(TG3_FLAG_##flag, (tp)->tg3_flags)
#define tg3_flag_set(tp, flag)				\
	_tg3_flag_set(TG3_FLAG_##flag, (tp)->tg3_flags)
#define tg3_flag_clear(tp, flag)			\
	_tg3_flag_clear(TG3_FLAG_##flag, (tp)->tg3_flags)

#define DRV_MODULE_NAME		"tg3"
#define TG3_MAJ_NUM			3
#define TG3_MIN_NUM			132
#define DRV_MODULE_VERSION	\
	__stringify(TG3_MAJ_NUM) "." __stringify(TG3_MIN_NUM)
#define DRV_MODULE_RELDATE	"May 21, 2013"

#define RESET_KIND_SHUTDOWN	0
#define RESET_KIND_INIT		1
#define RESET_KIND_SUSPEND	2

#define TG3_DEF_RX_MODE		0
#define TG3_DEF_TX_MODE		0
#define TG3_DEF_MSG_ENABLE	  \
	(NETIF_MSG_DRV		| \
	 NETIF_MSG_PROBE	| \
	 NETIF_MSG_LINK		| \
	 NETIF_MSG_TIMER	| \
	 NETIF_MSG_IFDOWN	| \
	 NETIF_MSG_IFUP		| \
	 NETIF_MSG_RX_ERR	| \
	 NETIF_MSG_TX_ERR)

#define TG3_GRC_LCLCTL_PWRSW_DELAY	100

/* length of time before we decide the hardware is borked,
 * and dev->tx_timeout() should be called to fix the problem
 */

#define TG3_TX_TIMEOUT			(5 * HZ)

/* hardware minimum and maximum for a single frame's data payload */
#define TG3_MIN_MTU			60
#define TG3_MAX_MTU(tp)	\
	(tg3_flag(tp, JUMBO_CAPABLE) ? 9000 : 1500)

/* These numbers seem to be hard coded in the NIC firmware somehow.
 * You can't change the ring sizes, but you can change where you place
 * them in the NIC onboard memory.
 */
#define TG3_RX_STD_RING_SIZE(tp) \
	(tg3_flag(tp, LRG_PROD_RING_CAP) ? \
	 TG3_RX_STD_MAX_SIZE_5717 : TG3_RX_STD_MAX_SIZE_5700)
#define TG3_DEF_RX_RING_PENDING		200
#define TG3_RX_JMB_RING_SIZE(tp) \
	(tg3_flag(tp, LRG_PROD_RING_CAP) ? \
	 TG3_RX_JMB_MAX_SIZE_5717 : TG3_RX_JMB_MAX_SIZE_5700)
#define TG3_DEF_RX_JUMBO_RING_PENDING	100

/* Do not place this n-ring entries value into the tp struct itself,
 * we really want to expose these constants to GCC so that modulo et
 * al.  operations are done with shifts and masks instead of with
 * hw multiply/modulo instructions.  Another solution would be to
 * replace things like '% foo' with '& (foo - 1)'.
 */

#define TG3_TX_RING_SIZE		512
#define TG3_DEF_TX_RING_PENDING		(TG3_TX_RING_SIZE - 1)

#define TG3_RX_STD_RING_BYTES(tp) \
	(sizeof(struct tg3_rx_buffer_desc) * TG3_RX_STD_RING_SIZE(tp))
#define TG3_RX_JMB_RING_BYTES(tp) \
	(sizeof(struct tg3_ext_rx_buffer_desc) * TG3_RX_JMB_RING_SIZE(tp))
#define TG3_RX_RCB_RING_BYTES(tp) \
	(sizeof(struct tg3_rx_buffer_desc) * (tp->rx_ret_ring_mask + 1))
#define TG3_TX_RING_BYTES	(sizeof(struct tg3_tx_buffer_desc) * \
				 TG3_TX_RING_SIZE)
#define NEXT_TX(N)		(((N) + 1) & (TG3_TX_RING_SIZE - 1))

#define TG3_DMA_BYTE_ENAB		64

#define TG3_RX_STD_DMA_SZ		1536
#define TG3_RX_JMB_DMA_SZ		9046

#define TG3_RX_DMA_TO_MAP_SZ(x)		((x) + TG3_DMA_BYTE_ENAB)

#define TG3_RX_STD_MAP_SZ		TG3_RX_DMA_TO_MAP_SZ(TG3_RX_STD_DMA_SZ)
#define TG3_RX_JMB_MAP_SZ		TG3_RX_DMA_TO_MAP_SZ(TG3_RX_JMB_DMA_SZ)

#define TG3_RX_STD_BUFF_RING_SIZE(tp) \
	(sizeof(struct ring_info) * TG3_RX_STD_RING_SIZE(tp))

#define TG3_RX_JMB_BUFF_RING_SIZE(tp) \
	(sizeof(struct ring_info) * TG3_RX_JMB_RING_SIZE(tp))

/* Due to a hardware bug, the 5701 can only DMA to memory addresses
 * that are at least dword aligned when used in PCIX mode.  The driver
 * works around this bug by double copying the packet.  This workaround
 * is built into the normal double copy length check for efficiency.
 *
 * However, the double copy is only necessary on those architectures
 * where unaligned memory accesses are inefficient.  For those architectures
 * where unaligned memory accesses incur little penalty, we can reintegrate
 * the 5701 in the normal rx path.  Doing so saves a device structure
 * dereference by hardcoding the double copy threshold in place.
 */
#define TG3_RX_COPY_THRESHOLD		256
#if NET_IP_ALIGN == 0 || defined(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS)
	#define TG3_RX_COPY_THRESH(tp)	TG3_RX_COPY_THRESHOLD
#else
	#define TG3_RX_COPY_THRESH(tp)	((tp)->rx_copy_thresh)
#endif

#if (NET_IP_ALIGN != 0)
#define TG3_RX_OFFSET(tp)	((tp)->rx_offset)
#else
#define TG3_RX_OFFSET(tp)	(NET_SKB_PAD)
#endif

/* minimum number of free TX descriptors required to wake up TX process */
#define TG3_TX_WAKEUP_THRESH(tnapi)		((tnapi)->tx_pending / 4)
#define TG3_TX_BD_DMA_MAX_2K		2048
#define TG3_TX_BD_DMA_MAX_4K		4096

#define TG3_RAW_IP_ALIGN 2

#define TG3_FW_UPDATE_TIMEOUT_SEC	5
#define TG3_FW_UPDATE_FREQ_SEC		(TG3_FW_UPDATE_TIMEOUT_SEC / 2)

#define FIRMWARE_TG3		"tigon/tg3.bin"
#define FIRMWARE_TG357766	"tigon/tg357766.bin"
#define FIRMWARE_TG3TSO		"tigon/tg3_tso.bin"
#define FIRMWARE_TG3TSO5	"tigon/tg3_tso5.bin"

static char version[] =
	DRV_MODULE_NAME ".c:v" DRV_MODULE_VERSION " (" DRV_MODULE_RELDATE ")";

MODULE_AUTHOR("David S. Miller (davem@redhat.com) and Jeff Garzik (jgarzik@pobox.com)");
MODULE_DESCRIPTION("Broadcom Tigon3 ethernet driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_MODULE_VERSION);
MODULE_FIRMWARE(FIRMWARE_TG3);
MODULE_FIRMWARE(FIRMWARE_TG3TSO);
MODULE_FIRMWARE(FIRMWARE_TG3TSO5);

static int tg3_debug = -1;	/* -1 == use TG3_DEF_MSG_ENABLE as value */
module_param(tg3_debug, int, 0);
MODULE_PARM_DESC(tg3_debug, "Tigon3 bitmapped debugging message enable value");

#define TG3_DRV_DATA_FLAG_10_100_ONLY	0x0001
#define TG3_DRV_DATA_FLAG_5705_10_100	0x0002

static DEFINE_PCI_DEVICE_TABLE(tg3_pci_tbl) = {
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_TIGON3_5700)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_TIGON3_5701)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_TIGON3_5702)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_TIGON3_5703)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_TIGON3_5704)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_TIGON3_5702FE)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_TIGON3_5705)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_TIGON3_5705_2)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_TIGON3_5705M)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_TIGON3_5705M_2)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_TIGON3_5702X)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_TIGON3_5703X)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_TIGON3_5704S)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_TIGON3_5702A3)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_TIGON3_5703A3)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_TIGON3_5782)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_TIGON3_5788)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_TIGON3_5789)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_TIGON3_5901),
	 .driver_data = TG3_DRV_DATA_FLAG_10_100_ONLY |
			TG3_DRV_DATA_FLAG_5705_10_100},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_TIGON3_5901_2),
	 .driver_data = TG3_DRV_DATA_FLAG_10_100_ONLY |
			TG3_DRV_DATA_FLAG_5705_10_100},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_TIGON3_5704S_2)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_TIGON3_5705F),
	 .driver_data = TG3_DRV_DATA_FLAG_10_100_ONLY |
			TG3_DRV_DATA_FLAG_5705_10_100},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_TIGON3_5721)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_TIGON3_5722)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_TIGON3_5750)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_TIGON3_5751)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_TIGON3_5751M)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_TIGON3_5751F),
	 .driver_data = TG3_DRV_DATA_FLAG_10_100_ONLY},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_TIGON3_5752)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_TIGON3_5752M)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_TIGON3_5753)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_TIGON3_5753M)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_TIGON3_5753F),
	 .driver_data = TG3_DRV_DATA_FLAG_10_100_ONLY},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_TIGON3_5754)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_TIGON3_5754M)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_TIGON3_5755)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_TIGON3_5755M)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_TIGON3_5756)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_TIGON3_5786)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_TIGON3_5787)},
	{PCI_DEVICE_SUB(PCI_VENDOR_ID_BROADCOM, TG3PCI_DEVICE_TIGON3_5787M,
			PCI_VENDOR_ID_LENOVO,
			TG3PCI_SUBDEVICE_ID_LENOVO_5787M),
	 .driver_data = TG3_DRV_DATA_FLAG_10_100_ONLY},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_TIGON3_5787M)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_TIGON3_5787F),
	 .driver_data = TG3_DRV_DATA_FLAG_10_100_ONLY},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_TIGON3_5714)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_TIGON3_5714S)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_TIGON3_5715)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_TIGON3_5715S)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_TIGON3_5780)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_TIGON3_5780S)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_TIGON3_5781)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_TIGON3_5906)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_TIGON3_5906M)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_TIGON3_5784)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_TIGON3_5764)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_TIGON3_5723)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_TIGON3_5761)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_TIGON3_5761E)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, TG3PCI_DEVICE_TIGON3_5761S)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, TG3PCI_DEVICE_TIGON3_5761SE)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, TG3PCI_DEVICE_TIGON3_5785_G)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, TG3PCI_DEVICE_TIGON3_5785_F)},
	{PCI_DEVICE_SUB(PCI_VENDOR_ID_BROADCOM, TG3PCI_DEVICE_TIGON3_57780,
			PCI_VENDOR_ID_AI, TG3PCI_SUBDEVICE_ID_ACER_57780_A),
	 .driver_data = TG3_DRV_DATA_FLAG_10_100_ONLY},
	{PCI_DEVICE_SUB(PCI_VENDOR_ID_BROADCOM, TG3PCI_DEVICE_TIGON3_57780,
			PCI_VENDOR_ID_AI, TG3PCI_SUBDEVICE_ID_ACER_57780_B),
	 .driver_data = TG3_DRV_DATA_FLAG_10_100_ONLY},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, TG3PCI_DEVICE_TIGON3_57780)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, TG3PCI_DEVICE_TIGON3_57760)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, TG3PCI_DEVICE_TIGON3_57790),
	 .driver_data = TG3_DRV_DATA_FLAG_10_100_ONLY},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, TG3PCI_DEVICE_TIGON3_57788)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, TG3PCI_DEVICE_TIGON3_5717)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, TG3PCI_DEVICE_TIGON3_5717_C)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, TG3PCI_DEVICE_TIGON3_5718)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, TG3PCI_DEVICE_TIGON3_57781)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, TG3PCI_DEVICE_TIGON3_57785)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, TG3PCI_DEVICE_TIGON3_57761)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, TG3PCI_DEVICE_TIGON3_57765)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, TG3PCI_DEVICE_TIGON3_57791),
	 .driver_data = TG3_DRV_DATA_FLAG_10_100_ONLY},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, TG3PCI_DEVICE_TIGON3_57795),
	 .driver_data = TG3_DRV_DATA_FLAG_10_100_ONLY},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, TG3PCI_DEVICE_TIGON3_5719)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, TG3PCI_DEVICE_TIGON3_5720)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, TG3PCI_DEVICE_TIGON3_57762)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, TG3PCI_DEVICE_TIGON3_57766)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, TG3PCI_DEVICE_TIGON3_5762)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, TG3PCI_DEVICE_TIGON3_5725)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, TG3PCI_DEVICE_TIGON3_5727)},
	{PCI_DEVICE(PCI_VENDOR_ID_SYSKONNECT, PCI_DEVICE_ID_SYSKONNECT_9DXX)},
	{PCI_DEVICE(PCI_VENDOR_ID_SYSKONNECT, PCI_DEVICE_ID_SYSKONNECT_9MXX)},
	{PCI_DEVICE(PCI_VENDOR_ID_ALTIMA, PCI_DEVICE_ID_ALTIMA_AC1000)},
	{PCI_DEVICE(PCI_VENDOR_ID_ALTIMA, PCI_DEVICE_ID_ALTIMA_AC1001)},
	{PCI_DEVICE(PCI_VENDOR_ID_ALTIMA, PCI_DEVICE_ID_ALTIMA_AC1003)},
	{PCI_DEVICE(PCI_VENDOR_ID_ALTIMA, PCI_DEVICE_ID_ALTIMA_AC9100)},
	{PCI_DEVICE(PCI_VENDOR_ID_APPLE, PCI_DEVICE_ID_APPLE_TIGON3)},
	{PCI_DEVICE(0x10cf, 0x11a2)}, /* Fujitsu 1000base-SX with BCM5703SKHB */
	{}
};

MODULE_DEVICE_TABLE(pci, tg3_pci_tbl);

static const struct {
	const char string[ETH_GSTRING_LEN];
} ethtool_stats_keys[] = {
	{ "rx_octets" },
	{ "rx_fragments" },
	{ "rx_ucast_packets" },
	{ "rx_mcast_packets" },
	{ "rx_bcast_packets" },
	{ "rx_fcs_errors" },
	{ "rx_align_errors" },
	{ "rx_xon_pause_rcvd" },
	{ "rx_xoff_pause_rcvd" },
	{ "rx_mac_ctrl_rcvd" },
	{ "rx_xoff_entered" },
	{ "rx_frame_too_long_errors" },
	{ "rx_jabbers" },
	{ "rx_undersize_packets" },
	{ "rx_in_length_errors" },
	{ "rx_out_length_errors" },
	{ "rx_64_or_less_octet_packets" },
	{ "rx_65_to_127_octet_packets" },
	{ "rx_128_to_255_octet_packets" },
	{ "rx_256_to_511_octet_packets" },
	{ "rx_512_to_1023_octet_packets" },
	{ "rx_1024_to_1522_octet_packets" },
	{ "rx_1523_to_2047_octet_packets" },
	{ "rx_2048_to_4095_octet_packets" },
	{ "rx_4096_to_8191_octet_packets" },
	{ "rx_8192_to_9022_octet_packets" },

	{ "tx_octets" },
	{ "tx_collisions" },

	{ "tx_xon_sent" },
	{ "tx_xoff_sent" },
	{ "tx_flow_control" },
	{ "tx_mac_errors" },
	{ "tx_single_collisions" },
	{ "tx_mult_collisions" },
	{ "tx_deferred" },
	{ "tx_excessive_collisions" },
	{ "tx_late_collisions" },
	{ "tx_collide_2times" },
	{ "tx_collide_3times" },
	{ "tx_collide_4times" },
	{ "tx_collide_5times" },
	{ "tx_collide_6times" },
	{ "tx_collide_7times" },
	{ "tx_collide_8times" },
	{ "tx_collide_9times" },
	{ "tx_collide_10times" },
	{ "tx_collide_11times" },
	{ "tx_collide_12times" },
	{ "tx_collide_13times" },
	{ "tx_collide_14times" },
	{ "tx_collide_15times" },
	{ "tx_ucast_packets" },
	{ "tx_mcast_packets" },
	{ "tx_bcast_packets" },
	{ "tx_carrier_sense_errors" },
	{ "tx_discards" },
	{ "tx_errors" },

	{ "dma_writeq_full" },
	{ "dma_write_prioq_full" },
	{ "rxbds_empty" },
	{ "rx_discards" },
	{ "rx_errors" },
	{ "rx_threshold_hit" },

	{ "dma_readq_full" },
	{ "dma_read_prioq_full" },
	{ "tx_comp_queue_full" },

	{ "ring_set_send_prod_index" },
	{ "ring_status_update" },
	{ "nic_irqs" },
	{ "nic_avoided_irqs" },
	{ "nic_tx_threshold_hit" },

	{ "mbuf_lwm_thresh_hit" },
};

#define TG3_NUM_STATS	ARRAY_SIZE(ethtool_stats_keys)
#define TG3_NVRAM_TEST		0
#define TG3_LINK_TEST		1
#define TG3_REGISTER_TEST	2
#define TG3_MEMORY_TEST		3
#define TG3_MAC_LOOPB_TEST	4
#define TG3_PHY_LOOPB_TEST	5
#define TG3_EXT_LOOPB_TEST	6
#define TG3_INTERRUPT_TEST	7


static const struct {
	const char string[ETH_GSTRING_LEN];
} ethtool_test_keys[] = {
	[TG3_NVRAM_TEST]	= { "nvram test        (online) " },
	[TG3_LINK_TEST]		= { "link test         (online) " },
	[TG3_REGISTER_TEST]	= { "register test     (offline)" },
	[TG3_MEMORY_TEST]	= { "memory test       (offline)" },
	[TG3_MAC_LOOPB_TEST]	= { "mac loopback test (offline)" },
	[TG3_PHY_LOOPB_TEST]	= { "phy loopback test (offline)" },
	[TG3_EXT_LOOPB_TEST]	= { "ext loopback test (offline)" },
	[TG3_INTERRUPT_TEST]	= { "interrupt test    (offline)" },
};

#define TG3_NUM_TEST	ARRAY_SIZE(ethtool_test_keys)


static void tg3_write32(struct tg3 *tp, u32 off, u32 val)
{
	writel(val, tp->regs + off);
}

static u32 tg3_read32(struct tg3 *tp, u32 off)
{
	return readl(tp->regs + off);
}

static void tg3_ape_write32(struct tg3 *tp, u32 off, u32 val)
{
	writel(val, tp->aperegs + off);
}

static u32 tg3_ape_read32(struct tg3 *tp, u32 off)
{
	return readl(tp->aperegs + off);
}

static void tg3_write_indirect_reg32(struct tg3 *tp, u32 off, u32 val)
{
	unsigned long flags;

	spin_lock_irqsave(&tp->indirect_lock, flags);
	pci_write_config_dword(tp->pdev, TG3PCI_REG_BASE_ADDR, off);
	pci_write_config_dword(tp->pdev, TG3PCI_REG_DATA, val);
	spin_unlock_irqrestore(&tp->indirect_lock, flags);
}

static void tg3_write_flush_reg32(struct tg3 *tp, u32 off, u32 val)
{
	writel(val, tp->regs + off);
	readl(tp->regs + off);
}

static u32 tg3_read_indirect_reg32(struct tg3 *tp, u32 off)
{
	unsigned long flags;
	u32 val;

	spin_lock_irqsave(&tp->indirect_lock, flags);
	pci_write_config_dword(tp->pdev, TG3PCI_REG_BASE_ADDR, off);
	pci_read_config_dword(tp->pdev, TG3PCI_REG_DATA, &val);
	spin_unlock_irqrestore(&tp->indirect_lock, flags);
	return val;
}

static void tg3_write_indirect_mbox(struct tg3 *tp, u32 off, u32 val)
{
	unsigned long flags;

	if (off == (MAILBOX_RCVRET_CON_IDX_0 + TG3_64BIT_REG_LOW)) {
		pci_write_config_dword(tp->pdev, TG3PCI_RCV_RET_RING_CON_IDX +
				       TG3_64BIT_REG_LOW, val);
		return;
	}
	if (off == TG3_RX_STD_PROD_IDX_REG) {
		pci_write_config_dword(tp->pdev, TG3PCI_STD_RING_PROD_IDX +
				       TG3_64BIT_REG_LOW, val);
		return;
	}

	spin_lock_irqsave(&tp->indirect_lock, flags);
	pci_write_config_dword(tp->pdev, TG3PCI_REG_BASE_ADDR, off + 0x5600);
	pci_write_config_dword(tp->pdev, TG3PCI_REG_DATA, val);
	spin_unlock_irqrestore(&tp->indirect_lock, flags);

	/* In indirect mode when disabling interrupts, we also need
	 * to clear the interrupt bit in the GRC local ctrl register.
	 */
	if ((off == (MAILBOX_INTERRUPT_0 + TG3_64BIT_REG_LOW)) &&
	    (val == 0x1)) {
		pci_write_config_dword(tp->pdev, TG3PCI_MISC_LOCAL_CTRL,
				       tp->grc_local_ctrl|GRC_LCLCTRL_CLEARINT);
	}
}

static u32 tg3_read_indirect_mbox(struct tg3 *tp, u32 off)
{
	unsigned long flags;
	u32 val;

	spin_lock_irqsave(&tp->indirect_lock, flags);
	pci_write_config_dword(tp->pdev, TG3PCI_REG_BASE_ADDR, off + 0x5600);
	pci_read_config_dword(tp->pdev, TG3PCI_REG_DATA, &val);
	spin_unlock_irqrestore(&tp->indirect_lock, flags);
	return val;
}

/* usec_wait specifies the wait time in usec when writing to certain registers
 * where it is unsafe to read back the register without some delay.
 * GRC_LOCAL_CTRL is one example if the GPIOs are toggled to switch power.
 * TG3PCI_CLOCK_CTRL is another example if the clock frequencies are changed.
 */
static void _tw32_flush(struct tg3 *tp, u32 off, u32 val, u32 usec_wait)
{
	if (tg3_flag(tp, PCIX_TARGET_HWBUG) || tg3_flag(tp, ICH_WORKAROUND))
		/* Non-posted methods */
		tp->write32(tp, off, val);
	else {
		/* Posted method */
		tg3_write32(tp, off, val);
		if (usec_wait)
			udelay(usec_wait);
		tp->read32(tp, off);
	}
	/* Wait again after the read for the posted method to guarantee that
	 * the wait time is met.
	 */
	if (usec_wait)
		udelay(usec_wait);
}

static inline void tw32_mailbox_flush(struct tg3 *tp, u32 off, u32 val)
{
	tp->write32_mbox(tp, off, val);
	if (tg3_flag(tp, FLUSH_POSTED_WRITES) ||
	    (!tg3_flag(tp, MBOX_WRITE_REORDER) &&
	     !tg3_flag(tp, ICH_WORKAROUND)))
		tp->read32_mbox(tp, off);
}

static void tg3_write32_tx_mbox(struct tg3 *tp, u32 off, u32 val)
{
	void __iomem *mbox = tp->regs + off;
	writel(val, mbox);
	if (tg3_flag(tp, TXD_MBOX_HWBUG))
		writel(val, mbox);
	if (tg3_flag(tp, MBOX_WRITE_REORDER) ||
	    tg3_flag(tp, FLUSH_POSTED_WRITES))
		readl(mbox);
}

static u32 tg3_read32_mbox_5906(struct tg3 *tp, u32 off)
{
	return readl(tp->regs + off + GRCMBOX_BASE);
}

static void tg3_write32_mbox_5906(struct tg3 *tp, u32 off, u32 val)
{
	writel(val, tp->regs + off + GRCMBOX_BASE);
}

#define tw32_mailbox(reg, val)		tp->write32_mbox(tp, reg, val)
#define tw32_mailbox_f(reg, val)	tw32_mailbox_flush(tp, (reg), (val))
#define tw32_rx_mbox(reg, val)		tp->write32_rx_mbox(tp, reg, val)
#define tw32_tx_mbox(reg, val)		tp->write32_tx_mbox(tp, reg, val)
#define tr32_mailbox(reg)		tp->read32_mbox(tp, reg)

#define tw32(reg, val)			tp->write32(tp, reg, val)
#define tw32_f(reg, val)		_tw32_flush(tp, (reg), (val), 0)
#define tw32_wait_f(reg, val, us)	_tw32_flush(tp, (reg), (val), (us))
#define tr32(reg)			tp->read32(tp, reg)

static void tg3_write_mem(struct tg3 *tp, u32 off, u32 val)
{
	unsigned long flags;

	if (tg3_asic_rev(tp) == ASIC_REV_5906 &&
	    (off >= NIC_SRAM_STATS_BLK) && (off < NIC_SRAM_TX_BUFFER_DESC))
		return;

	spin_lock_irqsave(&tp->indirect_lock, flags);
	if (tg3_flag(tp, SRAM_USE_CONFIG)) {
		pci_write_config_dword(tp->pdev, TG3PCI_MEM_WIN_BASE_ADDR, off);
		pci_write_config_dword(tp->pdev, TG3PCI_MEM_WIN_DATA, val);

		/* Always leave this as zero. */
		pci_write_config_dword(tp->pdev, TG3PCI_MEM_WIN_BASE_ADDR, 0);
	} else {
		tw32_f(TG3PCI_MEM_WIN_BASE_ADDR, off);
		tw32_f(TG3PCI_MEM_WIN_DATA, val);

		/* Always leave this as zero. */
		tw32_f(TG3PCI_MEM_WIN_BASE_ADDR, 0);
	}
	spin_unlock_irqrestore(&tp->indirect_lock, flags);
}

static void tg3_read_mem(struct tg3 *tp, u32 off, u32 *val)
{
	unsigned long flags;

	if (tg3_asic_rev(tp) == ASIC_REV_5906 &&
	    (off >= NIC_SRAM_STATS_BLK) && (off < NIC_SRAM_TX_BUFFER_DESC)) {
		*val = 0;
		return;
	}

	spin_lock_irqsave(&tp->indirect_lock, flags);
	if (tg3_flag(tp, SRAM_USE_CONFIG)) {
		pci_write_config_dword(tp->pdev, TG3PCI_MEM_WIN_BASE_ADDR, off);
		pci_read_config_dword(tp->pdev, TG3PCI_MEM_WIN_DATA, val);

		/* Always leave this as zero. */
		pci_write_config_dword(tp->pdev, TG3PCI_MEM_WIN_BASE_ADDR, 0);
	} else {
		tw32_f(TG3PCI_MEM_WIN_BASE_ADDR, off);
		*val = tr32(TG3PCI_MEM_WIN_DATA);

		/* Always leave this as zero. */
		tw32_f(TG3PCI_MEM_WIN_BASE_ADDR, 0);
	}
	spin_unlock_irqrestore(&tp->indirect_lock, flags);
}

static void tg3_ape_lock_init(struct tg3 *tp)
{
	int i;
	u32 regbase, bit;

	if (tg3_asic_rev(tp) == ASIC_REV_5761)
		regbase = TG3_APE_LOCK_GRANT;
	else
		regbase = TG3_APE_PER_LOCK_GRANT;

	/* Make sure the driver hasn't any stale locks. */
	for (i = TG3_APE_LOCK_PHY0; i <= TG3_APE_LOCK_GPIO; i++) {
		switch (i) {
		case TG3_APE_LOCK_PHY0:
		case TG3_APE_LOCK_PHY1:
		case TG3_APE_LOCK_PHY2:
		case TG3_APE_LOCK_PHY3:
			bit = APE_LOCK_GRANT_DRIVER;
			break;
		default:
			if (!tp->pci_fn)
				bit = APE_LOCK_GRANT_DRIVER;
			else
				bit = 1 << tp->pci_fn;
		}
		tg3_ape_write32(tp, regbase + 4 * i, bit);
	}

}

static int tg3_ape_lock(struct tg3 *tp, int locknum)
{
	int i, off;
	int ret = 0;
	u32 status, req, gnt, bit;

	if (!tg3_flag(tp, ENABLE_APE))
		return 0;

	switch (locknum) {
	case TG3_APE_LOCK_GPIO:
		if (tg3_asic_rev(tp) == ASIC_REV_5761)
			return 0;
	case TG3_APE_LOCK_GRC:
	case TG3_APE_LOCK_MEM:
		if (!tp->pci_fn)
			bit = APE_LOCK_REQ_DRIVER;
		else
			bit = 1 << tp->pci_fn;
		break;
	case TG3_APE_LOCK_PHY0:
	case TG3_APE_LOCK_PHY1:
	case TG3_APE_LOCK_PHY2:
	case TG3_APE_LOCK_PHY3:
		bit = APE_LOCK_REQ_DRIVER;
		break;
	default:
		return -EINVAL;
	}

	if (tg3_asic_rev(tp) == ASIC_REV_5761) {
		req = TG3_APE_LOCK_REQ;
		gnt = TG3_APE_LOCK_GRANT;
	} else {
		req = TG3_APE_PER_LOCK_REQ;
		gnt = TG3_APE_PER_LOCK_GRANT;
	}

	off = 4 * locknum;

	tg3_ape_write32(tp, req + off, bit);

	/* Wait for up to 1 millisecond to acquire lock. */
	for (i = 0; i < 100; i++) {
		status = tg3_ape_read32(tp, gnt + off);
		if (status == bit)
			break;
		if (pci_channel_offline(tp->pdev))
			break;

		udelay(10);
	}

	if (status != bit) {
		/* Revoke the lock request. */
		tg3_ape_write32(tp, gnt + off, bit);
		ret = -EBUSY;
	}

	return ret;
}

static void tg3_ape_unlock(struct tg3 *tp, int locknum)
{
	u32 gnt, bit;

	if (!tg3_flag(tp, ENABLE_APE))
		return;

	switch (locknum) {
	case TG3_APE_LOCK_GPIO:
		if (tg3_asic_rev(tp) == ASIC_REV_5761)
			return;
	case TG3_APE_LOCK_GRC:
	case TG3_APE_LOCK_MEM:
		if (!tp->pci_fn)
			bit = APE_LOCK_GRANT_DRIVER;
		else
			bit = 1 << tp->pci_fn;
		break;
	case TG3_APE_LOCK_PHY0:
	case TG3_APE_LOCK_PHY1:
	case TG3_APE_LOCK_PHY2:
	case TG3_APE_LOCK_PHY3:
		bit = APE_LOCK_GRANT_DRIVER;
		break;
	default:
		return;
	}

	if (tg3_asic_rev(tp) == ASIC_REV_5761)
		gnt = TG3_APE_LOCK_GRANT;
	else
		gnt = TG3_APE_PER_LOCK_GRANT;

	tg3_ape_write32(tp, gnt + 4 * locknum, bit);
}

static int tg3_ape_event_lock(struct tg3 *tp, u32 timeout_us)
{
	u32 apedata;

	while (timeout_us) {
		if (tg3_ape_lock(tp, TG3_APE_LOCK_MEM))
			return -EBUSY;

		apedata = tg3_ape_read32(tp, TG3_APE_EVENT_STATUS);
		if (!(apedata & APE_EVENT_STATUS_EVENT_PENDING))
			break;

		tg3_ape_unlock(tp, TG3_APE_LOCK_MEM);

		udelay(10);
		timeout_us -= (timeout_us > 10) ? 10 : timeout_us;
	}

	return timeout_us ? 0 : -EBUSY;
}

static int tg3_ape_wait_for_event(struct tg3 *tp, u32 timeout_us)
{
	u32 i, apedata;

	for (i = 0; i < timeout_us / 10; i++) {
		apedata = tg3_ape_read32(tp, TG3_APE_EVENT_STATUS);

		if (!(apedata & APE_EVENT_STATUS_EVENT_PENDING))
			break;

		udelay(10);
	}

	return i == timeout_us / 10;
}

static int tg3_ape_scratchpad_read(struct tg3 *tp, u32 *data, u32 base_off,
				   u32 len)
{
	int err;
	u32 i, bufoff, msgoff, maxlen, apedata;

	if (!tg3_flag(tp, APE_HAS_NCSI))
		return 0;

	apedata = tg3_ape_read32(tp, TG3_APE_SEG_SIG);
	if (apedata != APE_SEG_SIG_MAGIC)
		return -ENODEV;

	apedata = tg3_ape_read32(tp, TG3_APE_FW_STATUS);
	if (!(apedata & APE_FW_STATUS_READY))
		return -EAGAIN;

	bufoff = tg3_ape_read32(tp, TG3_APE_SEG_MSG_BUF_OFF) +
		 TG3_APE_SHMEM_BASE;
	msgoff = bufoff + 2 * sizeof(u32);
	maxlen = tg3_ape_read32(tp, TG3_APE_SEG_MSG_BUF_LEN);

	while (len) {
		u32 length;

		/* Cap xfer sizes to scratchpad limits. */
		length = (len > maxlen) ? maxlen : len;
		len -= length;

		apedata = tg3_ape_read32(tp, TG3_APE_FW_STATUS);
		if (!(apedata & APE_FW_STATUS_READY))
			return -EAGAIN;

		/* Wait for up to 1 msec for APE to service previous event. */
		err = tg3_ape_event_lock(tp, 1000);
		if (err)
			return err;

		apedata = APE_EVENT_STATUS_DRIVER_EVNT |
			  APE_EVENT_STATUS_SCRTCHPD_READ |
			  APE_EVENT_STATUS_EVENT_PENDING;
		tg3_ape_write32(tp, TG3_APE_EVENT_STATUS, apedata);

		tg3_ape_write32(tp, bufoff, base_off);
		tg3_ape_write32(tp, bufoff + sizeof(u32), length);

		tg3_ape_unlock(tp, TG3_APE_LOCK_MEM);
		tg3_ape_write32(tp, TG3_APE_EVENT, APE_EVENT_1);

		base_off += length;

		if (tg3_ape_wait_for_event(tp, 30000))
			return -EAGAIN;

		for (i = 0; length; i += 4, length -= 4) {
			u32 val = tg3_ape_read32(tp, msgoff + i);
			memcpy(data, &val, sizeof(u32));
			data++;
		}
	}

	return 0;
}

static int tg3_ape_send_event(struct tg3 *tp, u32 event)
{
	int err;
	u32 apedata;

	apedata = tg3_ape_read32(tp, TG3_APE_SEG_SIG);
	if (apedata != APE_SEG_SIG_MAGIC)
		return -EAGAIN;

	apedata = tg3_ape_read32(tp, TG3_APE_FW_STATUS);
	if (!(apedata & APE_FW_STATUS_READY))
		return -EAGAIN;

	/* Wait for up to 1 millisecond for APE to service previous event. */
	err = tg3_ape_event_lock(tp, 1000);
	if (err)
		return err;

	tg3_ape_write32(tp, TG3_APE_EVENT_STATUS,
			event | APE_EVENT_STATUS_EVENT_PENDING);

	tg3_ape_unlock(tp, TG3_APE_LOCK_MEM);
	tg3_ape_write32(tp, TG3_APE_EVENT, APE_EVENT_1);

	return 0;
}

static void tg3_ape_driver_state_change(struct tg3 *tp, int kind)
{
	u32 event;
	u32 apedata;

	if (!tg3_flag(tp, ENABLE_APE))
		return;

	switch (kind) {
	case RESET_KIND_INIT:
		tg3_ape_write32(tp, TG3_APE_HOST_SEG_SIG,
				APE_HOST_SEG_SIG_MAGIC);
		tg3_ape_write32(tp, TG3_APE_HOST_SEG_LEN,
				APE_HOST_SEG_LEN_MAGIC);
		apedata = tg3_ape_read32(tp, TG3_APE_HOST_INIT_COUNT);
		tg3_ape_write32(tp, TG3_APE_HOST_INIT_COUNT, ++apedata);
		tg3_ape_write32(tp, TG3_APE_HOST_DRIVER_ID,
			APE_HOST_DRIVER_ID_MAGIC(TG3_MAJ_NUM, TG3_MIN_NUM));
		tg3_ape_write32(tp, TG3_APE_HOST_BEHAVIOR,
				APE_HOST_BEHAV_NO_PHYLOCK);
		tg3_ape_write32(tp, TG3_APE_HOST_DRVR_STATE,
				    TG3_APE_HOST_DRVR_STATE_START);

		event = APE_EVENT_STATUS_STATE_START;
		break;
	case RESET_KIND_SHUTDOWN:
		/* With the interface we are currently using,
		 * APE does not track driver state.  Wiping
		 * out the HOST SEGMENT SIGNATURE forces
		 * the APE to assume OS absent status.
		 */
		tg3_ape_write32(tp, TG3_APE_HOST_SEG_SIG, 0x0);

		if (device_may_wakeup(&tp->pdev->dev) &&
		    tg3_flag(tp, WOL_ENABLE)) {
			tg3_ape_write32(tp, TG3_APE_HOST_WOL_SPEED,
					    TG3_APE_HOST_WOL_SPEED_AUTO);
			apedata = TG3_APE_HOST_DRVR_STATE_WOL;
		} else
			apedata = TG3_APE_HOST_DRVR_STATE_UNLOAD;

		tg3_ape_write32(tp, TG3_APE_HOST_DRVR_STATE, apedata);

		event = APE_EVENT_STATUS_STATE_UNLOAD;
		break;
	case RESET_KIND_SUSPEND:
		event = APE_EVENT_STATUS_STATE_SUSPEND;
		break;
	default:
		return;
	}

	event |= APE_EVENT_STATUS_DRIVER_EVNT | APE_EVENT_STATUS_STATE_CHNGE;

	tg3_ape_send_event(tp, event);
}

static void tg3_disable_ints(struct tg3 *tp)
{
	int i;

	tw32(TG3PCI_MISC_HOST_CTRL,
	     (tp->misc_host_ctrl | MISC_HOST_CTRL_MASK_PCI_INT));
	for (i = 0; i < tp->irq_max; i++)
		tw32_mailbox_f(tp->napi[i].int_mbox, 0x00000001);
}

static void tg3_enable_ints(struct tg3 *tp)
{
	int i;

	tp->irq_sync = 0;
	wmb();

	tw32(TG3PCI_MISC_HOST_CTRL,
	     (tp->misc_host_ctrl & ~MISC_HOST_CTRL_MASK_PCI_INT));

	tp->coal_now = tp->coalesce_mode | HOSTCC_MODE_ENABLE;
	for (i = 0; i < tp->irq_cnt; i++) {
		struct tg3_napi *tnapi = &tp->napi[i];

		tw32_mailbox_f(tnapi->int_mbox, tnapi->last_tag << 24);
		if (tg3_flag(tp, 1SHOT_MSI))
			tw32_mailbox_f(tnapi->int_mbox, tnapi->last_tag << 24);

		tp->coal_now |= tnapi->coal_now;
	}

	/* Force an initial interrupt */
	if (!tg3_flag(tp, TAGGED_STATUS) &&
	    (tp->napi[0].hw_status->status & SD_STATUS_UPDATED))
		tw32(GRC_LOCAL_CTRL, tp->grc_local_ctrl | GRC_LCLCTRL_SETINT);
	else
		tw32(HOSTCC_MODE, tp->coal_now);

	tp->coal_now &= ~(tp->napi[0].coal_now | tp->napi[1].coal_now);
}

static inline unsigned int tg3_has_work(struct tg3_napi *tnapi)
{
	struct tg3 *tp = tnapi->tp;
	struct tg3_hw_status *sblk = tnapi->hw_status;
	unsigned int work_exists = 0;

	/* check for phy events */
	if (!(tg3_flag(tp, USE_LINKCHG_REG) || tg3_flag(tp, POLL_SERDES))) {
		if (sblk->status & SD_STATUS_LINK_CHG)
			work_exists = 1;
	}

	/* check for TX work to do */
	if (sblk->idx[0].tx_consumer != tnapi->tx_cons)
		work_exists = 1;

	/* check for RX work to do */
	if (tnapi->rx_rcb_prod_idx &&
	    *(tnapi->rx_rcb_prod_idx) != tnapi->rx_rcb_ptr)
		work_exists = 1;

	return work_exists;
}

/* tg3_int_reenable
 *  similar to tg3_enable_ints, but it accurately determines whether there
 *  is new work pending and can return without flushing the PIO write
 *  which reenables interrupts
 */
static void tg3_int_reenable(struct tg3_napi *tnapi)
{
	struct tg3 *tp = tnapi->tp;

	tw32_mailbox(tnapi->int_mbox, tnapi->last_tag << 24);
	mmiowb();

	/* When doing tagged status, this work check is unnecessary.
	 * The last_tag we write above tells the chip which piece of
	 * work we've completed.
	 */
	if (!tg3_flag(tp, TAGGED_STATUS) && tg3_has_work(tnapi))
		tw32(HOSTCC_MODE, tp->coalesce_mode |
		     HOSTCC_MODE_ENABLE | tnapi->coal_now);
}

static void tg3_switch_clocks(struct tg3 *tp)
{
	u32 clock_ctrl;
	u32 orig_clock_ctrl;

	if (tg3_flag(tp, CPMU_PRESENT) || tg3_flag(tp, 5780_CLASS))
		return;

	clock_ctrl = tr32(TG3PCI_CLOCK_CTRL);

	orig_clock_ctrl = clock_ctrl;
	clock_ctrl &= (CLOCK_CTRL_FORCE_CLKRUN |
		       CLOCK_CTRL_CLKRUN_OENABLE |
		       0x1f);
	tp->pci_clock_ctrl = clock_ctrl;

	if (tg3_flag(tp, 5705_PLUS)) {
		if (orig_clock_ctrl & CLOCK_CTRL_625_CORE) {
			tw32_wait_f(TG3PCI_CLOCK_CTRL,
				    clock_ctrl | CLOCK_CTRL_625_CORE, 40);
		}
	} else if ((orig_clock_ctrl & CLOCK_CTRL_44MHZ_CORE) != 0) {
		tw32_wait_f(TG3PCI_CLOCK_CTRL,
			    clock_ctrl |
			    (CLOCK_CTRL_44MHZ_CORE | CLOCK_CTRL_ALTCLK),
			    40);
		tw32_wait_f(TG3PCI_CLOCK_CTRL,
			    clock_ctrl | (CLOCK_CTRL_ALTCLK),
			    40);
	}
	tw32_wait_f(TG3PCI_CLOCK_CTRL, clock_ctrl, 40);
}

#define PHY_BUSY_LOOPS	5000

static int __tg3_readphy(struct tg3 *tp, unsigned int phy_addr, int reg,
			 u32 *val)
{
	u32 frame_val;
	unsigned int loops;
	int ret;

	if ((tp->mi_mode & MAC_MI_MODE_AUTO_POLL) != 0) {
		tw32_f(MAC_MI_MODE,
		     (tp->mi_mode & ~MAC_MI_MODE_AUTO_POLL));
		udelay(80);
	}

	tg3_ape_lock(tp, tp->phy_ape_lock);

	*val = 0x0;

	frame_val  = ((phy_addr << MI_COM_PHY_ADDR_SHIFT) &
		      MI_COM_PHY_ADDR_MASK);
	frame_val |= ((reg << MI_COM_REG_ADDR_SHIFT) &
		      MI_COM_REG_ADDR_MASK);
	frame_val |= (MI_COM_CMD_READ | MI_COM_START);

	tw32_f(MAC_MI_COM, frame_val);

	loops = PHY_BUSY_LOOPS;
	while (loops != 0) {
		udelay(10);
		frame_val = tr32(MAC_MI_COM);

		if ((frame_val & MI_COM_BUSY) == 0) {
			udelay(5);
			frame_val = tr32(MAC_MI_COM);
			break;
		}
		loops -= 1;
	}

	ret = -EBUSY;
	if (loops != 0) {
		*val = frame_val & MI_COM_DATA_MASK;
		ret = 0;
	}

	if ((tp->mi_mode & MAC_MI_MODE_AUTO_POLL) != 0) {
		tw32_f(MAC_MI_MODE, tp->mi_mode);
		udelay(80);
	}

	tg3_ape_unlock(tp, tp->phy_ape_lock);

	return ret;
}

static int tg3_readphy(struct tg3 *tp, int reg, u32 *val)
{
	return __tg3_readphy(tp, tp->phy_addr, reg, val);
}

static int __tg3_writephy(struct tg3 *tp, unsigned int phy_addr, int reg,
			  u32 val)
{
	u32 frame_val;
	unsigned int loops;
	int ret;

	if ((tp->phy_flags & TG3_PHYFLG_IS_FET) &&
	    (reg == MII_CTRL1000 || reg == MII_TG3_AUX_CTRL))
		return 0;

	if ((tp->mi_mode & MAC_MI_MODE_AUTO_POLL) != 0) {
		tw32_f(MAC_MI_MODE,
		     (tp->mi_mode & ~MAC_MI_MODE_AUTO_POLL));
		udelay(80);
	}

	tg3_ape_lock(tp, tp->phy_ape_lock);

	frame_val  = ((phy_addr << MI_COM_PHY_ADDR_SHIFT) &
		      MI_COM_PHY_ADDR_MASK);
	frame_val |= ((reg << MI_COM_REG_ADDR_SHIFT) &
		      MI_COM_REG_ADDR_MASK);
	frame_val |= (val & MI_COM_DATA_MASK);
	frame_val |= (MI_COM_CMD_WRITE | MI_COM_START);

	tw32_f(MAC_MI_COM, frame_val);

	loops = PHY_BUSY_LOOPS;
	while (loops != 0) {
		udelay(10);
		frame_val = tr32(MAC_MI_COM);
		if ((frame_val & MI_COM_BUSY) == 0) {
			udelay(5);
			frame_val = tr32(MAC_MI_COM);
			break;
		}
		loops -= 1;
	}

	ret = -EBUSY;
	if (loops != 0)
		ret = 0;

	if ((tp->mi_mode & MAC_MI_MODE_AUTO_POLL) != 0) {
		tw32_f(MAC_MI_MODE, tp->mi_mode);
		udelay(80);
	}

	tg3_ape_unlock(tp, tp->phy_ape_lock);

	return ret;
}

static int tg3_writephy(struct tg3 *tp, int reg, u32 val)
{
	return __tg3_writephy(tp, tp->phy_addr, reg, val);
}

static int tg3_phy_cl45_write(struct tg3 *tp, u32 devad, u32 addr, u32 val)
{
	int err;

	err = tg3_writephy(tp, MII_TG3_MMD_CTRL, devad);
	if (err)
		goto done;

	err = tg3_writephy(tp, MII_TG3_MMD_ADDRESS, addr);
	if (err)
		goto done;

	err = tg3_writephy(tp, MII_TG3_MMD_CTRL,
			   MII_TG3_MMD_CTRL_DATA_NOINC | devad);
	if (err)
		goto done;

	err = tg3_writephy(tp, MII_TG3_MMD_ADDRESS, val);

done:
	return err;
}

static int tg3_phy_cl45_read(struct tg3 *tp, u32 devad, u32 addr, u32 *val)
{
	int err;

	err = tg3_writephy(tp, MII_TG3_MMD_CTRL, devad);
	if (err)
		goto done;

	err = tg3_writephy(tp, MII_TG3_MMD_ADDRESS, addr);
	if (err)
		goto done;

	err = tg3_writephy(tp, MII_TG3_MMD_CTRL,
			   MII_TG3_MMD_CTRL_DATA_NOINC | devad);
	if (err)
		goto done;

	err = tg3_readphy(tp, MII_TG3_MMD_ADDRESS, val);

done:
	return err;
}

static int tg3_phydsp_read(struct tg3 *tp, u32 reg, u32 *val)
{
	int err;

	err = tg3_writephy(tp, MII_TG3_DSP_ADDRESS, reg);
	if (!err)
		err = tg3_readphy(tp, MII_TG3_DSP_RW_PORT, val);

	return err;
}

static int tg3_phydsp_write(struct tg3 *tp, u32 reg, u32 val)
{
	int err;

	err = tg3_writephy(tp, MII_TG3_DSP_ADDRESS, reg);
	if (!err)
		err = tg3_writephy(tp, MII_TG3_DSP_RW_PORT, val);

	return err;
}

static int tg3_phy_auxctl_read(struct tg3 *tp, int reg, u32 *val)
{
	int err;

	err = tg3_writephy(tp, MII_TG3_AUX_CTRL,
			   (reg << MII_TG3_AUXCTL_MISC_RDSEL_SHIFT) |
			   MII_TG3_AUXCTL_SHDWSEL_MISC);
	if (!err)
		err = tg3_readphy(tp, MII_TG3_AUX_CTRL, val);

	return err;
}

static int tg3_phy_auxctl_write(struct tg3 *tp, int reg, u32 set)
{
	if (reg == MII_TG3_AUXCTL_SHDWSEL_MISC)
		set |= MII_TG3_AUXCTL_MISC_WREN;

	return tg3_writephy(tp, MII_TG3_AUX_CTRL, set | reg);
}

static int tg3_phy_toggle_auxctl_smdsp(struct tg3 *tp, bool enable)
{
	u32 val;
	int err;

	err = tg3_phy_auxctl_read(tp, MII_TG3_AUXCTL_SHDWSEL_AUXCTL, &val);

	if (err)
		return err;
	if (enable)

		val |= MII_TG3_AUXCTL_ACTL_SMDSP_ENA;
	else
		val &= ~MII_TG3_AUXCTL_ACTL_SMDSP_ENA;

	err = tg3_phy_auxctl_write((tp), MII_TG3_AUXCTL_SHDWSEL_AUXCTL,
				   val | MII_TG3_AUXCTL_ACTL_TX_6DB);

	return err;
}

static int tg3_bmcr_reset(struct tg3 *tp)
{
	u32 phy_control;
	int limit, err;

	/* OK, reset it, and poll the BMCR_RESET bit until it
	 * clears or we time out.
	 */
	phy_control = BMCR_RESET;
	err = tg3_writephy(tp, MII_BMCR, phy_control);
	if (err != 0)
		return -EBUSY;

	limit = 5000;
	while (limit--) {
		err = tg3_readphy(tp, MII_BMCR, &phy_control);
		if (err != 0)
			return -EBUSY;

		if ((phy_control & BMCR_RESET) == 0) {
			udelay(40);
			break;
		}
		udelay(10);
	}
	if (limit < 0)
		return -EBUSY;

	return 0;
}

static int tg3_mdio_read(struct mii_bus *bp, int mii_id, int reg)
{
	struct tg3 *tp = bp->priv;
	u32 val;

	spin_lock_bh(&tp->lock);

	if (tg3_readphy(tp, reg, &val))
		val = -EIO;

	spin_unlock_bh(&tp->lock);

	return val;
}

static int tg3_mdio_write(struct mii_bus *bp, int mii_id, int reg, u16 val)
{
	struct tg3 *tp = bp->priv;
	u32 ret = 0;

	spin_lock_bh(&tp->lock);

	if (tg3_writephy(tp, reg, val))
		ret = -EIO;

	spin_unlock_bh(&tp->lock);

	return ret;
}

static int tg3_mdio_reset(struct mii_bus *bp)
{
	return 0;
}

static void tg3_mdio_config_5785(struct tg3 *tp)
{
	u32 val;
	struct phy_device *phydev;

	phydev = tp->mdio_bus->phy_map[TG3_PHY_MII_ADDR];
	switch (phydev->drv->phy_id & phydev->drv->phy_id_mask) {
	case PHY_ID_BCM50610:
	case PHY_ID_BCM50610M:
		val = MAC_PHYCFG2_50610_LED_MODES;
		break;
	case PHY_ID_BCMAC131:
		val = MAC_PHYCFG2_AC131_LED_MODES;
		break;
	case PHY_ID_RTL8211C:
		val = MAC_PHYCFG2_RTL8211C_LED_MODES;
		break;
	case PHY_ID_RTL8201E:
		val = MAC_PHYCFG2_RTL8201E_LED_MODES;
		break;
	default:
		return;
	}

	if (phydev->interface != PHY_INTERFACE_MODE_RGMII) {
		tw32(MAC_PHYCFG2, val);

		val = tr32(MAC_PHYCFG1);
		val &= ~(MAC_PHYCFG1_RGMII_INT |
			 MAC_PHYCFG1_RXCLK_TO_MASK | MAC_PHYCFG1_TXCLK_TO_MASK);
		val |= MAC_PHYCFG1_RXCLK_TIMEOUT | MAC_PHYCFG1_TXCLK_TIMEOUT;
		tw32(MAC_PHYCFG1, val);

		return;
	}

	if (!tg3_flag(tp, RGMII_INBAND_DISABLE))
		val |= MAC_PHYCFG2_EMODE_MASK_MASK |
		       MAC_PHYCFG2_FMODE_MASK_MASK |
		       MAC_PHYCFG2_GMODE_MASK_MASK |
		       MAC_PHYCFG2_ACT_MASK_MASK   |
		       MAC_PHYCFG2_QUAL_MASK_MASK |
		       MAC_PHYCFG2_INBAND_ENABLE;

	tw32(MAC_PHYCFG2, val);

	val = tr32(MAC_PHYCFG1);
	val &= ~(MAC_PHYCFG1_RXCLK_TO_MASK | MAC_PHYCFG1_TXCLK_TO_MASK |
		 MAC_PHYCFG1_RGMII_EXT_RX_DEC | MAC_PHYCFG1_RGMII_SND_STAT_EN);
	if (!tg3_flag(tp, RGMII_INBAND_DISABLE)) {
		if (tg3_flag(tp, RGMII_EXT_IBND_RX_EN))
			val |= MAC_PHYCFG1_RGMII_EXT_RX_DEC;
		if (tg3_flag(tp, RGMII_EXT_IBND_TX_EN))
			val |= MAC_PHYCFG1_RGMII_SND_STAT_EN;
	}
	val |= MAC_PHYCFG1_RXCLK_TIMEOUT | MAC_PHYCFG1_TXCLK_TIMEOUT |
	       MAC_PHYCFG1_RGMII_INT | MAC_PHYCFG1_TXC_DRV;
	tw32(MAC_PHYCFG1, val);

	val = tr32(MAC_EXT_RGMII_MODE);
	val &= ~(MAC_RGMII_MODE_RX_INT_B |
		 MAC_RGMII_MODE_RX_QUALITY |
		 MAC_RGMII_MODE_RX_ACTIVITY |
		 MAC_RGMII_MODE_RX_ENG_DET |
		 MAC_RGMII_MODE_TX_ENABLE |
		 MAC_RGMII_MODE_TX_LOWPWR |
		 MAC_RGMII_MODE_TX_RESET);
	if (!tg3_flag(tp, RGMII_INBAND_DISABLE)) {
		if (tg3_flag(tp, RGMII_EXT_IBND_RX_EN))
			val |= MAC_RGMII_MODE_RX_INT_B |
			       MAC_RGMII_MODE_RX_QUALITY |
			       MAC_RGMII_MODE_RX_ACTIVITY |
			       MAC_RGMII_MODE_RX_ENG_DET;
		if (tg3_flag(tp, RGMII_EXT_IBND_TX_EN))
			val |= MAC_RGMII_MODE_TX_ENABLE |
			       MAC_RGMII_MODE_TX_LOWPWR |
			       MAC_RGMII_MODE_TX_RESET;
	}
	tw32(MAC_EXT_RGMII_MODE, val);
}

static void tg3_mdio_start(struct tg3 *tp)
{
	tp->mi_mode &= ~MAC_MI_MODE_AUTO_POLL;
	tw32_f(MAC_MI_MODE, tp->mi_mode);
	udelay(80);

	if (tg3_flag(tp, MDIOBUS_INITED) &&
	    tg3_asic_rev(tp) == ASIC_REV_5785)
		tg3_mdio_config_5785(tp);
}

static int tg3_mdio_init(struct tg3 *tp)
{
	int i;
	u32 reg;
	struct phy_device *phydev;

	if (tg3_flag(tp, 5717_PLUS)) {
		u32 is_serdes;

		tp->phy_addr = tp->pci_fn + 1;

		if (tg3_chip_rev_id(tp) != CHIPREV_ID_5717_A0)
			is_serdes = tr32(SG_DIG_STATUS) & SG_DIG_IS_SERDES;
		else
			is_serdes = tr32(TG3_CPMU_PHY_STRAP) &
				    TG3_CPMU_PHY_STRAP_IS_SERDES;
		if (is_serdes)
			tp->phy_addr += 7;
	} else
		tp->phy_addr = TG3_PHY_MII_ADDR;

	tg3_mdio_start(tp);

	if (!tg3_flag(tp, USE_PHYLIB) || tg3_flag(tp, MDIOBUS_INITED))
		return 0;

	tp->mdio_bus = mdiobus_alloc();
	if (tp->mdio_bus == NULL)
		return -ENOMEM;

	tp->mdio_bus->name     = "tg3 mdio bus";
	snprintf(tp->mdio_bus->id, MII_BUS_ID_SIZE, "%x",
		 (tp->pdev->bus->number << 8) | tp->pdev->devfn);
	tp->mdio_bus->priv     = tp;
	tp->mdio_bus->parent   = &tp->pdev->dev;
	tp->mdio_bus->read     = &tg3_mdio_read;
	tp->mdio_bus->write    = &tg3_mdio_write;
	tp->mdio_bus->reset    = &tg3_mdio_reset;
	tp->mdio_bus->phy_mask = ~(1 << TG3_PHY_MII_ADDR);
	tp->mdio_bus->irq      = &tp->mdio_irq[0];

	for (i = 0; i < PHY_MAX_ADDR; i++)
		tp->mdio_bus->irq[i] = PHY_POLL;

	/* The bus registration will look for all the PHYs on the mdio bus.
	 * Unfortunately, it does not ensure the PHY is powered up before
	 * accessing the PHY ID registers.  A chip reset is the
	 * quickest way to bring the device back to an operational state..
	 */
	if (tg3_readphy(tp, MII_BMCR, &reg) || (reg & BMCR_PDOWN))
		tg3_bmcr_reset(tp);

	i = mdiobus_register(tp->mdio_bus);
	if (i) {
		dev_warn(&tp->pdev->dev, "mdiobus_reg failed (0x%x)\n", i);
		mdiobus_free(tp->mdio_bus);
		return i;
	}

	phydev = tp->mdio_bus->phy_map[TG3_PHY_MII_ADDR];

	if (!phydev || !phydev->drv) {
		dev_warn(&tp->pdev->dev, "No PHY devices\n");
		mdiobus_unregister(tp->mdio_bus);
		mdiobus_free(tp->mdio_bus);
		return -ENODEV;
	}

	switch (phydev->drv->phy_id & phydev->drv->phy_id_mask) {
	case PHY_ID_BCM57780:
		phydev->interface = PHY_INTERFACE_MODE_GMII;
		phydev->dev_flags |= PHY_BRCM_AUTO_PWRDWN_ENABLE;
		break;
	case PHY_ID_BCM50610:
	case PHY_ID_BCM50610M:
		phydev->dev_flags |= PHY_BRCM_CLEAR_RGMII_MODE |
				     PHY_BRCM_RX_REFCLK_UNUSED |
				     PHY_BRCM_DIS_TXCRXC_NOENRGY |
				     PHY_BRCM_AUTO_PWRDWN_ENABLE;
		if (tg3_flag(tp, RGMII_INBAND_DISABLE))
			phydev->dev_flags |= PHY_BRCM_STD_IBND_DISABLE;
		if (tg3_flag(tp, RGMII_EXT_IBND_RX_EN))
			phydev->dev_flags |= PHY_BRCM_EXT_IBND_RX_ENABLE;
		if (tg3_flag(tp, RGMII_EXT_IBND_TX_EN))
			phydev->dev_flags |= PHY_BRCM_EXT_IBND_TX_ENABLE;
		/* fallthru */
	case PHY_ID_RTL8211C:
		phydev->interface = PHY_INTERFACE_MODE_RGMII;
		break;
	case PHY_ID_RTL8201E:
	case PHY_ID_BCMAC131:
		phydev->interface = PHY_INTERFACE_MODE_MII;
		phydev->dev_flags |= PHY_BRCM_AUTO_PWRDWN_ENABLE;
		tp->phy_flags |= TG3_PHYFLG_IS_FET;
		break;
	}

	tg3_flag_set(tp, MDIOBUS_INITED);

	if (tg3_asic_rev(tp) == ASIC_REV_5785)
		tg3_mdio_config_5785(tp);

	return 0;
}

static void tg3_mdio_fini(struct tg3 *tp)
{
	if (tg3_flag(tp, MDIOBUS_INITED)) {
		tg3_flag_clear(tp, MDIOBUS_INITED);
		mdiobus_unregister(tp->mdio_bus);
		mdiobus_free(tp->mdio_bus);
	}
}

/* tp->lock is held. */
static inline void tg3_generate_fw_event(struct tg3 *tp)
{
	u32 val;

	val = tr32(GRC_RX_CPU_EVENT);
	val |= GRC_RX_CPU_DRIVER_EVENT;
	tw32_f(GRC_RX_CPU_EVENT, val);

	tp->last_event_jiffies = jiffies;
}

#define TG3_FW_EVENT_TIMEOUT_USEC 2500

/* tp->lock is held. */
static void tg3_wait_for_event_ack(struct tg3 *tp)
{
	int i;
	unsigned int delay_cnt;
	long time_remain;

	/* If enough time has passed, no wait is necessary. */
	time_remain = (long)(tp->last_event_jiffies + 1 +
		      usecs_to_jiffies(TG3_FW_EVENT_TIMEOUT_USEC)) -
		      (long)jiffies;
	if (time_remain < 0)
		return;

	/* Check if we can shorten the wait time. */
	delay_cnt = jiffies_to_usecs(time_remain);
	if (delay_cnt > TG3_FW_EVENT_TIMEOUT_USEC)
		delay_cnt = TG3_FW_EVENT_TIMEOUT_USEC;
	delay_cnt = (delay_cnt >> 3) + 1;

	for (i = 0; i < delay_cnt; i++) {
		if (!(tr32(GRC_RX_CPU_EVENT) & GRC_RX_CPU_DRIVER_EVENT))
			break;
		if (pci_channel_offline(tp->pdev))
			break;

		udelay(8);
	}
}

/* tp->lock is held. */
static void tg3_phy_gather_ump_data(struct tg3 *tp, u32 *data)
{
	u32 reg, val;

	val = 0;
	if (!tg3_readphy(tp, MII_BMCR, &reg))
		val = reg << 16;
	if (!tg3_readphy(tp, MII_BMSR, &reg))
		val |= (reg & 0xffff);
	*data++ = val;

	val = 0;
	if (!tg3_readphy(tp, MII_ADVERTISE, &reg))
		val = reg << 16;
	if (!tg3_readphy(tp, MII_LPA, &reg))
		val |= (reg & 0xffff);
	*data++ = val;

	val = 0;
	if (!(tp->phy_flags & TG3_PHYFLG_MII_SERDES)) {
		if (!tg3_readphy(tp, MII_CTRL1000, &reg))
			val = reg << 16;
		if (!tg3_readphy(tp, MII_STAT1000, &reg))
			val |= (reg & 0xffff);
	}
	*data++ = val;

	if (!tg3_readphy(tp, MII_PHYADDR, &reg))
		val = reg << 16;
	else
		val = 0;
	*data++ = val;
}

/* tp->lock is held. */
static void tg3_ump_link_report(struct tg3 *tp)
{
	u32 data[4];

	if (!tg3_flag(tp, 5780_CLASS) || !tg3_flag(tp, ENABLE_ASF))
		return;

	tg3_phy_gather_ump_data(tp, data);

	tg3_wait_for_event_ack(tp);

	tg3_write_mem(tp, NIC_SRAM_FW_CMD_MBOX, FWCMD_NICDRV_LINK_UPDATE);
	tg3_write_mem(tp, NIC_SRAM_FW_CMD_LEN_MBOX, 14);
	tg3_write_mem(tp, NIC_SRAM_FW_CMD_DATA_MBOX + 0x0, data[0]);
	tg3_write_mem(tp, NIC_SRAM_FW_CMD_DATA_MBOX + 0x4, data[1]);
	tg3_write_mem(tp, NIC_SRAM_FW_CMD_DATA_MBOX + 0x8, data[2]);
	tg3_write_mem(tp, NIC_SRAM_FW_CMD_DATA_MBOX + 0xc, data[3]);

	tg3_generate_fw_event(tp);
}

/* tp->lock is held. */
static void tg3_stop_fw(struct tg3 *tp)
{
	if (tg3_flag(tp, ENABLE_ASF) && !tg3_flag(tp, ENABLE_APE)) {
		/* Wait for RX cpu to ACK the previous event. */
		tg3_wait_for_event_ack(tp);

		tg3_write_mem(tp, NIC_SRAM_FW_CMD_MBOX, FWCMD_NICDRV_PAUSE_FW);

		tg3_generate_fw_event(tp);

		/* Wait for RX cpu to ACK this event. */
		tg3_wait_for_event_ack(tp);
	}
}

/* tp->lock is held. */
static void tg3_write_sig_pre_reset(struct tg3 *tp, int kind)
{
	tg3_write_mem(tp, NIC_SRAM_FIRMWARE_MBOX,
		      NIC_SRAM_FIRMWARE_MBOX_MAGIC1);

	if (tg3_flag(tp, ASF_NEW_HANDSHAKE)) {
		switch (kind) {
		case RESET_KIND_INIT:
			tg3_write_mem(tp, NIC_SRAM_FW_DRV_STATE_MBOX,
				      DRV_STATE_START);
			break;

		case RESET_KIND_SHUTDOWN:
			tg3_write_mem(tp, NIC_SRAM_FW_DRV_STATE_MBOX,
				      DRV_STATE_UNLOAD);
			break;

		case RESET_KIND_SUSPEND:
			tg3_write_mem(tp, NIC_SRAM_FW_DRV_STATE_MBOX,
				      DRV_STATE_SUSPEND);
			break;

		default:
			break;
		}
	}

	if (kind == RESET_KIND_INIT ||
	    kind == RESET_KIND_SUSPEND)
		tg3_ape_driver_state_change(tp, kind);
}

/* tp->lock is held. */
static void tg3_write_sig_post_reset(struct tg3 *tp, int kind)
{
	if (tg3_flag(tp, ASF_NEW_HANDSHAKE)) {
		switch (kind) {
		case RESET_KIND_INIT:
			tg3_write_mem(tp, NIC_SRAM_FW_DRV_STATE_MBOX,
				      DRV_STATE_START_DONE);
			break;

		case RESET_KIND_SHUTDOWN:
			tg3_write_mem(tp, NIC_SRAM_FW_DRV_STATE_MBOX,
				      DRV_STATE_UNLOAD_DONE);
			break;

		default:
			break;
		}
	}

	if (kind == RESET_KIND_SHUTDOWN)
		tg3_ape_driver_state_change(tp, kind);
}

/* tp->lock is held. */
static void tg3_write_sig_legacy(struct tg3 *tp, int kind)
{
	if (tg3_flag(tp, ENABLE_ASF)) {
		switch (kind) {
		case RESET_KIND_INIT:
			tg3_write_mem(tp, NIC_SRAM_FW_DRV_STATE_MBOX,
				      DRV_STATE_START);
			break;

		case RESET_KIND_SHUTDOWN:
			tg3_write_mem(tp, NIC_SRAM_FW_DRV_STATE_MBOX,
				      DRV_STATE_UNLOAD);
			break;

		case RESET_KIND_SUSPEND:
			tg3_write_mem(tp, NIC_SRAM_FW_DRV_STATE_MBOX,
				      DRV_STATE_SUSPEND);
			break;

		default:
			break;
		}
	}
}

static int tg3_poll_fw(struct tg3 *tp)
{
	int i;
	u32 val;

	if (tg3_flag(tp, NO_FWARE_REPORTED))
		return 0;

	if (tg3_flag(tp, IS_SSB_CORE)) {
		/* We don't use firmware. */
		return 0;
	}

	if (tg3_asic_rev(tp) == ASIC_REV_5906) {
		/* Wait up to 20ms for init done. */
		for (i = 0; i < 200; i++) {
			if (tr32(VCPU_STATUS) & VCPU_STATUS_INIT_DONE)
				return 0;
			if (pci_channel_offline(tp->pdev))
				return -ENODEV;

			udelay(100);
		}
		return -ENODEV;
	}

	/* Wait for firmware initialization to complete. */
	for (i = 0; i < 100000; i++) {
		tg3_read_mem(tp, NIC_SRAM_FIRMWARE_MBOX, &val);
		if (val == ~NIC_SRAM_FIRMWARE_MBOX_MAGIC1)
			break;
		if (pci_channel_offline(tp->pdev)) {
			if (!tg3_flag(tp, NO_FWARE_REPORTED)) {
				tg3_flag_set(tp, NO_FWARE_REPORTED);
				netdev_info(tp->dev, "No firmware running\n");
			}

			break;
		}

		udelay(10);
	}

	/* Chip might not be fitted with firmware.  Some Sun onboard
	 * parts are configured like that.  So don't signal the timeout
	 * of the above loop as an error, but do report the lack of
	 * running firmware once.
	 */
	if (i >= 100000 && !tg3_flag(tp, NO_FWARE_REPORTED)) {
		tg3_flag_set(tp, NO_FWARE_REPORTED);

		netdev_info(tp->dev, "No firmware running\n");
	}

	if (tg3_chip_rev_id(tp) == CHIPREV_ID_57765_A0) {
		/* The 57765 A0 needs a little more
		 * time to do some important work.
		 */
		mdelay(10);
	}

	return 0;
}

static void tg3_link_report(struct tg3 *tp)
{
	if (!netif_carrier_ok(tp->dev)) {
		netif_info(tp, link, tp->dev, "Link is down\n");
		tg3_ump_link_report(tp);
	} else if (netif_msg_link(tp)) {
		netdev_info(tp->dev, "Link is up at %d Mbps, %s duplex\n",
			    (tp->link_config.active_speed == SPEED_1000 ?
			     1000 :
			     (tp->link_config.active_speed == SPEED_100 ?
			      100 : 10)),
			    (tp->link_config.active_duplex == DUPLEX_FULL ?
			     "full" : "half"));

		netdev_info(tp->dev, "Flow control is %s for TX and %s for RX\n",
			    (tp->link_config.active_flowctrl & FLOW_CTRL_TX) ?
			    "on" : "off",
			    (tp->link_config.active_flowctrl & FLOW_CTRL_RX) ?
			    "on" : "off");

		if (tp->phy_flags & TG3_PHYFLG_EEE_CAP)
			netdev_info(tp->dev, "EEE is %s\n",
				    tp->setlpicnt ? "enabled" : "disabled");

		tg3_ump_link_report(tp);
	}

	tp->link_up = netif_carrier_ok(tp->dev);
}

static u32 tg3_decode_flowctrl_1000T(u32 adv)
{
	u32 flowctrl = 0;

	if (adv & ADVERTISE_PAUSE_CAP) {
		flowctrl |= FLOW_CTRL_RX;
		if (!(adv & ADVERTISE_PAUSE_ASYM))
			flowctrl |= FLOW_CTRL_TX;
	} else if (adv & ADVERTISE_PAUSE_ASYM)
		flowctrl |= FLOW_CTRL_TX;

	return flowctrl;
}

static u16 tg3_advert_flowctrl_1000X(u8 flow_ctrl)
{
	u16 miireg;

	if ((flow_ctrl & FLOW_CTRL_TX) && (flow_ctrl & FLOW_CTRL_RX))
		miireg = ADVERTISE_1000XPAUSE;
	else if (flow_ctrl & FLOW_CTRL_TX)
		miireg = ADVERTISE_1000XPSE_ASYM;
	else if (flow_ctrl & FLOW_CTRL_RX)
		miireg = ADVERTISE_1000XPAUSE | ADVERTISE_1000XPSE_ASYM;
	else
		miireg = 0;

	return miireg;
}

static u32 tg3_decode_flowctrl_1000X(u32 adv)
{
	u32 flowctrl = 0;

	if (adv & ADVERTISE_1000XPAUSE) {
		flowctrl |= FLOW_CTRL_RX;
		if (!(adv & ADVERTISE_1000XPSE_ASYM))
			flowctrl |= FLOW_CTRL_TX;
	} else if (adv & ADVERTISE_1000XPSE_ASYM)
		flowctrl |= FLOW_CTRL_TX;

	return flowctrl;
}

static u8 tg3_resolve_flowctrl_1000X(u16 lcladv, u16 rmtadv)
{
	u8 cap = 0;

	if (lcladv & rmtadv & ADVERTISE_1000XPAUSE) {
		cap = FLOW_CTRL_TX | FLOW_CTRL_RX;
	} else if (lcladv & rmtadv & ADVERTISE_1000XPSE_ASYM) {
		if (lcladv & ADVERTISE_1000XPAUSE)
			cap = FLOW_CTRL_RX;
		if (rmtadv & ADVERTISE_1000XPAUSE)
			cap = FLOW_CTRL_TX;
	}

	return cap;
}

static void tg3_setup_flow_control(struct tg3 *tp, u32 lcladv, u32 rmtadv)
{
	u8 autoneg;
	u8 flowctrl = 0;
	u32 old_rx_mode = tp->rx_mode;
	u32 old_tx_mode = tp->tx_mode;

	if (tg3_flag(tp, USE_PHYLIB))
		autoneg = tp->mdio_bus->phy_map[TG3_PHY_MII_ADDR]->autoneg;
	else
		autoneg = tp->link_config.autoneg;

	if (autoneg == AUTONEG_ENABLE && tg3_flag(tp, PAUSE_AUTONEG)) {
		if (tp->phy_flags & TG3_PHYFLG_ANY_SERDES)
			flowctrl = tg3_resolve_flowctrl_1000X(lcladv, rmtadv);
		else
			flowctrl = mii_resolve_flowctrl_fdx(lcladv, rmtadv);
	} else
		flowctrl = tp->link_config.flowctrl;

	tp->link_config.active_flowctrl = flowctrl;

	if (flowctrl & FLOW_CTRL_RX)
		tp->rx_mode |= RX_MODE_FLOW_CTRL_ENABLE;
	else
		tp->rx_mode &= ~RX_MODE_FLOW_CTRL_ENABLE;

	if (old_rx_mode != tp->rx_mode)
		tw32_f(MAC_RX_MODE, tp->rx_mode);

	if (flowctrl & FLOW_CTRL_TX)
		tp->tx_mode |= TX_MODE_FLOW_CTRL_ENABLE;
	else
		tp->tx_mode &= ~TX_MODE_FLOW_CTRL_ENABLE;

	if (old_tx_mode != tp->tx_mode)
		tw32_f(MAC_TX_MODE, tp->tx_mode);
}

static void tg3_adjust_link(struct net_device *dev)
{
	u8 oldflowctrl, linkmesg = 0;
	u32 mac_mode, lcl_adv, rmt_adv;
	struct tg3 *tp = netdev_priv(dev);
	struct phy_device *phydev = tp->mdio_bus->phy_map[TG3_PHY_MII_ADDR];

	spin_lock_bh(&tp->lock);

	mac_mode = tp->mac_mode & ~(MAC_MODE_PORT_MODE_MASK |
				    MAC_MODE_HALF_DUPLEX);

	oldflowctrl = tp->link_config.active_flowctrl;

	if (phydev->link) {
		lcl_adv = 0;
		rmt_adv = 0;

		if (phydev->speed == SPEED_100 || phydev->speed == SPEED_10)
			mac_mode |= MAC_MODE_PORT_MODE_MII;
		else if (phydev->speed == SPEED_1000 ||
			 tg3_asic_rev(tp) != ASIC_REV_5785)
			mac_mode |= MAC_MODE_PORT_MODE_GMII;
		else
			mac_mode |= MAC_MODE_PORT_MODE_MII;

		if (phydev->duplex == DUPLEX_HALF)
			mac_mode |= MAC_MODE_HALF_DUPLEX;
		else {
			lcl_adv = mii_advertise_flowctrl(
				  tp->link_config.flowctrl);

			if (phydev->pause)
				rmt_adv = LPA_PAUSE_CAP;
			if (phydev->asym_pause)
				rmt_adv |= LPA_PAUSE_ASYM;
		}

		tg3_setup_flow_control(tp, lcl_adv, rmt_adv);
	} else
		mac_mode |= MAC_MODE_PORT_MODE_GMII;

	if (mac_mode != tp->mac_mode) {
		tp->mac_mode = mac_mode;
		tw32_f(MAC_MODE, tp->mac_mode);
		udelay(40);
	}

	if (tg3_asic_rev(tp) == ASIC_REV_5785) {
		if (phydev->speed == SPEED_10)
			tw32(MAC_MI_STAT,
			     MAC_MI_STAT_10MBPS_MODE |
			     MAC_MI_STAT_LNKSTAT_ATTN_ENAB);
		else
			tw32(MAC_MI_STAT, MAC_MI_STAT_LNKSTAT_ATTN_ENAB);
	}

	if (phydev->speed == SPEED_1000 && phydev->duplex == DUPLEX_HALF)
		tw32(MAC_TX_LENGTHS,
		     ((2 << TX_LENGTHS_IPG_CRS_SHIFT) |
		      (6 << TX_LENGTHS_IPG_SHIFT) |
		      (0xff << TX_LENGTHS_SLOT_TIME_SHIFT)));
	else
		tw32(MAC_TX_LENGTHS,
		     ((2 << TX_LENGTHS_IPG_CRS_SHIFT) |
		      (6 << TX_LENGTHS_IPG_SHIFT) |
		      (32 << TX_LENGTHS_SLOT_TIME_SHIFT)));

	if (phydev->link != tp->old_link ||
	    phydev->speed != tp->link_config.active_speed ||
	    phydev->duplex != tp->link_config.active_duplex ||
	    oldflowctrl != tp->link_config.active_flowctrl)
		linkmesg = 1;

	tp->old_link = phydev->link;
	tp->link_config.active_speed = phydev->speed;
	tp->link_config.active_duplex = phydev->duplex;

	spin_unlock_bh(&tp->lock);

	if (linkmesg)
		tg3_link_report(tp);
}

static int tg3_phy_init(struct tg3 *tp)
{
	struct phy_device *phydev;

	if (tp->phy_flags & TG3_PHYFLG_IS_CONNECTED)
		return 0;

	/* Bring the PHY back to a known state. */
	tg3_bmcr_reset(tp);

	phydev = tp->mdio_bus->phy_map[TG3_PHY_MII_ADDR];

	/* Attach the MAC to the PHY. */
	phydev = phy_connect(tp->dev, dev_name(&phydev->dev),
			     tg3_adjust_link, phydev->interface);
	if (IS_ERR(phydev)) {
		dev_err(&tp->pdev->dev, "Could not attach to PHY\n");
		return PTR_ERR(phydev);
	}

	/* Mask with MAC supported features. */
	switch (phydev->interface) {
	case PHY_INTERFACE_MODE_GMII:
	case PHY_INTERFACE_MODE_RGMII:
		if (!(tp->phy_flags & TG3_PHYFLG_10_100_ONLY)) {
			phydev->supported &= (PHY_GBIT_FEATURES |
					      SUPPORTED_Pause |
					      SUPPORTED_Asym_Pause);
			break;
		}
		/* fallthru */
	case PHY_INTERFACE_MODE_MII:
		phydev->supported &= (PHY_BASIC_FEATURES |
				      SUPPORTED_Pause |
				      SUPPORTED_Asym_Pause);
		break;
	default:
		phy_disconnect(tp->mdio_bus->phy_map[TG3_PHY_MII_ADDR]);
		return -EINVAL;
	}

	tp->phy_flags |= TG3_PHYFLG_IS_CONNECTED;

	phydev->advertising = phydev->supported;

	return 0;
}

static void tg3_phy_start(struct tg3 *tp)
{
	struct phy_device *phydev;

	if (!(tp->phy_flags & TG3_PHYFLG_IS_CONNECTED))
		return;

	phydev = tp->mdio_bus->phy_map[TG3_PHY_MII_ADDR];

	if (tp->phy_flags & TG3_PHYFLG_IS_LOW_POWER) {
		tp->phy_flags &= ~TG3_PHYFLG_IS_LOW_POWER;
		phydev->speed = tp->link_config.speed;
		phydev->duplex = tp->link_config.duplex;
		phydev->autoneg = tp->link_config.autoneg;
		phydev->advertising = tp->link_config.advertising;
	}

	phy_start(phydev);

	phy_start_aneg(phydev);
}

static void tg3_phy_stop(struct tg3 *tp)
{
	if (!(tp->phy_flags & TG3_PHYFLG_IS_CONNECTED))
		return;

	phy_stop(tp->mdio_bus->phy_map[TG3_PHY_MII_ADDR]);
}

static void tg3_phy_fini(struct tg3 *tp)
{
	if (tp->phy_flags & TG3_PHYFLG_IS_CONNECTED) {
		phy_disconnect(tp->mdio_bus->phy_map[TG3_PHY_MII_ADDR]);
		tp->phy_flags &= ~TG3_PHYFLG_IS_CONNECTED;
	}
}

static int tg3_phy_set_extloopbk(struct tg3 *tp)
{
	int err;
	u32 val;

	if (tp->phy_flags & TG3_PHYFLG_IS_FET)
		return 0;

	if ((tp->phy_id & TG3_PHY_ID_MASK) == TG3_PHY_ID_BCM5401) {
		/* Cannot do read-modify-write on 5401 */
		err = tg3_phy_auxctl_write(tp,
					   MII_TG3_AUXCTL_SHDWSEL_AUXCTL,
					   MII_TG3_AUXCTL_ACTL_EXTLOOPBK |
					   0x4c20);
		goto done;
	}

	err = tg3_phy_auxctl_read(tp,
				  MII_TG3_AUXCTL_SHDWSEL_AUXCTL, &val);
	if (err)
		return err;

	val |= MII_TG3_AUXCTL_ACTL_EXTLOOPBK;
	err = tg3_phy_auxctl_write(tp,
				   MII_TG3_AUXCTL_SHDWSEL_AUXCTL, val);

done:
	return err;
}

static void tg3_phy_fet_toggle_apd(struct tg3 *tp, bool enable)
{
	u32 phytest;

	if (!tg3_readphy(tp, MII_TG3_FET_TEST, &phytest)) {
		u32 phy;

		tg3_writephy(tp, MII_TG3_FET_TEST,
			     phytest | MII_TG3_FET_SHADOW_EN);
		if (!tg3_readphy(tp, MII_TG3_FET_SHDW_AUXSTAT2, &phy)) {
			if (enable)
				phy |= MII_TG3_FET_SHDW_AUXSTAT2_APD;
			else
				phy &= ~MII_TG3_FET_SHDW_AUXSTAT2_APD;
			tg3_writephy(tp, MII_TG3_FET_SHDW_AUXSTAT2, phy);
		}
		tg3_writephy(tp, MII_TG3_FET_TEST, phytest);
	}
}

static void tg3_phy_toggle_apd(struct tg3 *tp, bool enable)
{
	u32 reg;

	if (!tg3_flag(tp, 5705_PLUS) ||
	    (tg3_flag(tp, 5717_PLUS) &&
	     (tp->phy_flags & TG3_PHYFLG_MII_SERDES)))
		return;

	if (tp->phy_flags & TG3_PHYFLG_IS_FET) {
		tg3_phy_fet_toggle_apd(tp, enable);
		return;
	}

	reg = MII_TG3_MISC_SHDW_WREN |
	      MII_TG3_MISC_SHDW_SCR5_SEL |
	      MII_TG3_MISC_SHDW_SCR5_LPED |
	      MII_TG3_MISC_SHDW_SCR5_DLPTLM |
	      MII_TG3_MISC_SHDW_SCR5_SDTL |
	      MII_TG3_MISC_SHDW_SCR5_C125OE;
	if (tg3_asic_rev(tp) != ASIC_REV_5784 || !enable)
		reg |= MII_TG3_MISC_SHDW_SCR5_DLLAPD;

	tg3_writephy(tp, MII_TG3_MISC_SHDW, reg);


	reg = MII_TG3_MISC_SHDW_WREN |
	      MII_TG3_MISC_SHDW_APD_SEL |
	      MII_TG3_MISC_SHDW_APD_WKTM_84MS;
	if (enable)
		reg |= MII_TG3_MISC_SHDW_APD_ENABLE;

	tg3_writephy(tp, MII_TG3_MISC_SHDW, reg);
}

static void tg3_phy_toggle_automdix(struct tg3 *tp, bool enable)
{
	u32 phy;

	if (!tg3_flag(tp, 5705_PLUS) ||
	    (tp->phy_flags & TG3_PHYFLG_ANY_SERDES))
		return;

	if (tp->phy_flags & TG3_PHYFLG_IS_FET) {
		u32 ephy;

		if (!tg3_readphy(tp, MII_TG3_FET_TEST, &ephy)) {
			u32 reg = MII_TG3_FET_SHDW_MISCCTRL;

			tg3_writephy(tp, MII_TG3_FET_TEST,
				     ephy | MII_TG3_FET_SHADOW_EN);
			if (!tg3_readphy(tp, reg, &phy)) {
				if (enable)
					phy |= MII_TG3_FET_SHDW_MISCCTRL_MDIX;
				else
					phy &= ~MII_TG3_FET_SHDW_MISCCTRL_MDIX;
				tg3_writephy(tp, reg, phy);
			}
			tg3_writephy(tp, MII_TG3_FET_TEST, ephy);
		}
	} else {
		int ret;

		ret = tg3_phy_auxctl_read(tp,
					  MII_TG3_AUXCTL_SHDWSEL_MISC, &phy);
		if (!ret) {
			if (enable)
				phy |= MII_TG3_AUXCTL_MISC_FORCE_AMDIX;
			else
				phy &= ~MII_TG3_AUXCTL_MISC_FORCE_AMDIX;
			tg3_phy_auxctl_write(tp,
					     MII_TG3_AUXCTL_SHDWSEL_MISC, phy);
		}
	}
}

static void tg3_phy_set_wirespeed(struct tg3 *tp)
{
	int ret;
	u32 val;

	if (tp->phy_flags & TG3_PHYFLG_NO_ETH_WIRE_SPEED)
		return;

	ret = tg3_phy_auxctl_read(tp, MII_TG3_AUXCTL_SHDWSEL_MISC, &val);
	if (!ret)
		tg3_phy_auxctl_write(tp, MII_TG3_AUXCTL_SHDWSEL_MISC,
				     val | MII_TG3_AUXCTL_MISC_WIRESPD_EN);
}

static void tg3_phy_apply_otp(struct tg3 *tp)
{
	u32 otp, phy;

	if (!tp->phy_otp)
		return;

	otp = tp->phy_otp;

	if (tg3_phy_toggle_auxctl_smdsp(tp, true))
		return;

	phy = ((otp & TG3_OTP_AGCTGT_MASK) >> TG3_OTP_AGCTGT_SHIFT);
	phy |= MII_TG3_DSP_TAP1_AGCTGT_DFLT;
	tg3_phydsp_write(tp, MII_TG3_DSP_TAP1, phy);

	phy = ((otp & TG3_OTP_HPFFLTR_MASK) >> TG3_OTP_HPFFLTR_SHIFT) |
	      ((otp & TG3_OTP_HPFOVER_MASK) >> TG3_OTP_HPFOVER_SHIFT);
	tg3_phydsp_write(tp, MII_TG3_DSP_AADJ1CH0, phy);

	phy = ((otp & TG3_OTP_LPFDIS_MASK) >> TG3_OTP_LPFDIS_SHIFT);
	phy |= MII_TG3_DSP_AADJ1CH3_ADCCKADJ;
	tg3_phydsp_write(tp, MII_TG3_DSP_AADJ1CH3, phy);

	phy = ((otp & TG3_OTP_VDAC_MASK) >> TG3_OTP_VDAC_SHIFT);
	tg3_phydsp_write(tp, MII_TG3_DSP_EXP75, phy);

	phy = ((otp & TG3_OTP_10BTAMP_MASK) >> TG3_OTP_10BTAMP_SHIFT);
	tg3_phydsp_write(tp, MII_TG3_DSP_EXP96, phy);

	phy = ((otp & TG3_OTP_ROFF_MASK) >> TG3_OTP_ROFF_SHIFT) |
	      ((otp & TG3_OTP_RCOFF_MASK) >> TG3_OTP_RCOFF_SHIFT);
	tg3_phydsp_write(tp, MII_TG3_DSP_EXP97, phy);

	tg3_phy_toggle_auxctl_smdsp(tp, false);
}

static void tg3_phy_eee_adjust(struct tg3 *tp, bool current_link_up)
{
	u32 val;

	if (!(tp->phy_flags & TG3_PHYFLG_EEE_CAP))
		return;

	tp->setlpicnt = 0;

	if (tp->link_config.autoneg == AUTONEG_ENABLE &&
	    current_link_up &&
	    tp->link_config.active_duplex == DUPLEX_FULL &&
	    (tp->link_config.active_speed == SPEED_100 ||
	     tp->link_config.active_speed == SPEED_1000)) {
		u32 eeectl;

		if (tp->link_config.active_speed == SPEED_1000)
			eeectl = TG3_CPMU_EEE_CTRL_EXIT_16_5_US;
		else
			eeectl = TG3_CPMU_EEE_CTRL_EXIT_36_US;

		tw32(TG3_CPMU_EEE_CTRL, eeectl);

		tg3_phy_cl45_read(tp, MDIO_MMD_AN,
				  TG3_CL45_D7_EEERES_STAT, &val);

		if (val == TG3_CL45_D7_EEERES_STAT_LP_1000T ||
		    val == TG3_CL45_D7_EEERES_STAT_LP_100TX)
			tp->setlpicnt = 2;
	}

	if (!tp->setlpicnt) {
		if (current_link_up &&
		   !tg3_phy_toggle_auxctl_smdsp(tp, true)) {
			tg3_phydsp_write(tp, MII_TG3_DSP_TAP26, 0x0000);
			tg3_phy_toggle_auxctl_smdsp(tp, false);
		}

		val = tr32(TG3_CPMU_EEE_MODE);
		tw32(TG3_CPMU_EEE_MODE, val & ~TG3_CPMU_EEEMD_LPI_ENABLE);
	}
}

static void tg3_phy_eee_enable(struct tg3 *tp)
{
	u32 val;

	if (tp->link_config.active_speed == SPEED_1000 &&
	    (tg3_asic_rev(tp) == ASIC_REV_5717 ||
	     tg3_asic_rev(tp) == ASIC_REV_5719 ||
	     tg3_flag(tp, 57765_CLASS)) &&
	    !tg3_phy_toggle_auxctl_smdsp(tp, true)) {
		val = MII_TG3_DSP_TAP26_ALNOKO |
		      MII_TG3_DSP_TAP26_RMRXSTO;
		tg3_phydsp_write(tp, MII_TG3_DSP_TAP26, val);
		tg3_phy_toggle_auxctl_smdsp(tp, false);
	}

	val = tr32(TG3_CPMU_EEE_MODE);
	tw32(TG3_CPMU_EEE_MODE, val | TG3_CPMU_EEEMD_LPI_ENABLE);
}

static int tg3_wait_macro_done(struct tg3 *tp)
{
	int limit = 100;

	while (limit--) {
		u32 tmp32;

		if (!tg3_readphy(tp, MII_TG3_DSP_CONTROL, &tmp32)) {
			if ((tmp32 & 0x1000) == 0)
				break;
		}
	}
	if (limit < 0)
		return -EBUSY;

	return 0;
}

static int tg3_phy_write_and_check_testpat(struct tg3 *tp, int *resetp)
{
	static const u32 test_pat[4][6] = {
	{ 0x00005555, 0x00000005, 0x00002aaa, 0x0000000a, 0x00003456, 0x00000003 },
	{ 0x00002aaa, 0x0000000a, 0x00003333, 0x00000003, 0x0000789a, 0x00000005 },
	{ 0x00005a5a, 0x00000005, 0x00002a6a, 0x0000000a, 0x00001bcd, 0x00000003 },
	{ 0x00002a5a, 0x0000000a, 0x000033c3, 0x00000003, 0x00002ef1, 0x00000005 }
	};
	int chan;

	for (chan = 0; chan < 4; chan++) {
		int i;

		tg3_writephy(tp, MII_TG3_DSP_ADDRESS,
			     (chan * 0x2000) | 0x0200);
		tg3_writephy(tp, MII_TG3_DSP_CONTROL, 0x0002);

		for (i = 0; i < 6; i++)
			tg3_writephy(tp, MII_TG3_DSP_RW_PORT,
				     test_pat[chan][i]);

		tg3_writephy(tp, MII_TG3_DSP_CONTROL, 0x0202);
		if (tg3_wait_macro_done(tp)) {
			*resetp = 1;
			return -EBUSY;
		}

		tg3_writephy(tp, MII_TG3_DSP_ADDRESS,
			     (chan * 0x2000) | 0x0200);
		tg3_writephy(tp, MII_TG3_DSP_CONTROL, 0x0082);
		if (tg3_wait_macro_done(tp)) {
			*resetp = 1;
			return -EBUSY;
		}

		tg3_writephy(tp, MII_TG3_DSP_CONTROL, 0x0802);
		if (tg3_wait_macro_done(tp)) {
			*resetp = 1;
			return -EBUSY;
		}

		for (i = 0; i < 6; i += 2) {
			u32 low, high;

			if (tg3_readphy(tp, MII_TG3_DSP_RW_PORT, &low) ||
			    tg3_readphy(tp, MII_TG3_DSP_RW_PORT, &high) ||
			    tg3_wait_macro_done(tp)) {
				*resetp = 1;
				return -EBUSY;
			}
			low &= 0x7fff;
			high &= 0x000f;
			if (low != test_pat[chan][i] ||
			    high != test_pat[chan][i+1]) {
				tg3_writephy(tp, MII_TG3_DSP_ADDRESS, 0x000b);
				tg3_writephy(tp, MII_TG3_DSP_RW_PORT, 0x4001);
				tg3_writephy(tp, MII_TG3_DSP_RW_PORT, 0x4005);

				return -EBUSY;
			}
		}
	}

	return 0;
}

static int tg3_phy_reset_chanpat(struct tg3 *tp)
{
	int chan;

	for (chan = 0; chan < 4; chan++) {
		int i;

		tg3_writephy(tp, MII_TG3_DSP_ADDRESS,
			     (chan * 0x2000) | 0x0200);
		tg3_writephy(tp, MII_TG3_DSP_CONTROL, 0x0002);
		for (i = 0; i < 6; i++)
			tg3_writephy(tp, MII_TG3_DSP_RW_PORT, 0x000);
		tg3_writephy(tp, MII_TG3_DSP_CONTROL, 0x0202);
		if (tg3_wait_macro_done(tp))
			return -EBUSY;
	}

	return 0;
}

static int tg3_phy_reset_5703_4_5(struct tg3 *tp)
{
	u32 reg32, phy9_orig;
	int retries, do_phy_reset, err;

	retries = 10;
	do_phy_reset = 1;
	do {
		if (do_phy_reset) {
			err = tg3_bmcr_reset(tp);
			if (err)
				return err;
			do_phy_reset = 0;
		}

		/* Disable transmitter and interrupt.  */
		if (tg3_readphy(tp, MII_TG3_EXT_CTRL, &reg32))
			continue;

		reg32 |= 0x3000;
		tg3_writephy(tp, MII_TG3_EXT_CTRL, reg32);

		/* Set full-duplex, 1000 mbps.  */
		tg3_writephy(tp, MII_BMCR,
			     BMCR_FULLDPLX | BMCR_SPEED1000);

		/* Set to master mode.  */
		if (tg3_readphy(tp, MII_CTRL1000, &phy9_orig))
			continue;

		tg3_writephy(tp, MII_CTRL1000,
			     CTL1000_AS_MASTER | CTL1000_ENABLE_MASTER);

		err = tg3_phy_toggle_auxctl_smdsp(tp, true);
		if (err)
			return err;

		/* Block the PHY control access.  */
		tg3_phydsp_write(tp, 0x8005, 0x0800);

		err = tg3_phy_write_and_check_testpat(tp, &do_phy_reset);
		if (!err)
			break;
	} while (--retries);

	err = tg3_phy_reset_chanpat(tp);
	if (err)
		return err;

	tg3_phydsp_write(tp, 0x8005, 0x0000);

	tg3_writephy(tp, MII_TG3_DSP_ADDRESS, 0x8200);
	tg3_writephy(tp, MII_TG3_DSP_CONTROL, 0x0000);

	tg3_phy_toggle_auxctl_smdsp(tp, false);

	tg3_writephy(tp, MII_CTRL1000, phy9_orig);

	if (!tg3_readphy(tp, MII_TG3_EXT_CTRL, &reg32)) {
		reg32 &= ~0x3000;
		tg3_writephy(tp, MII_TG3_EXT_CTRL, reg32);
	} else if (!err)
		err = -EBUSY;

	return err;
}

static void tg3_carrier_off(struct tg3 *tp)
{
	netif_carrier_off(tp->dev);
	tp->link_up = false;
}

static void tg3_warn_mgmt_link_flap(struct tg3 *tp)
{
	if (tg3_flag(tp, ENABLE_ASF))
		netdev_warn(tp->dev,
			    "Management side-band traffic will be interrupted during phy settings change\n");
}

/* This will reset the tigon3 PHY if there is no valid
 * link unless the FORCE argument is non-zero.
 */
static int tg3_phy_reset(struct tg3 *tp)
{
	u32 val, cpmuctrl;
	int err;

	if (tg3_asic_rev(tp) == ASIC_REV_5906) {
		val = tr32(GRC_MISC_CFG);
		tw32_f(GRC_MISC_CFG, val & ~GRC_MISC_CFG_EPHY_IDDQ);
		udelay(40);
	}
	err  = tg3_readphy(tp, MII_BMSR, &val);
	err |= tg3_readphy(tp, MII_BMSR, &val);
	if (err != 0)
		return -EBUSY;

	if (netif_running(tp->dev) && tp->link_up) {
		netif_carrier_off(tp->dev);
		tg3_link_report(tp);
	}

	if (tg3_asic_rev(tp) == ASIC_REV_5703 ||
	    tg3_asic_rev(tp) == ASIC_REV_5704 ||
	    tg3_asic_rev(tp) == ASIC_REV_5705) {
		err = tg3_phy_reset_5703_4_5(tp);
		if (err)
			return err;
		goto out;
	}

	cpmuctrl = 0;
	if (tg3_asic_rev(tp) == ASIC_REV_5784 &&
	    tg3_chip_rev(tp) != CHIPREV_5784_AX) {
		cpmuctrl = tr32(TG3_CPMU_CTRL);
		if (cpmuctrl & CPMU_CTRL_GPHY_10MB_RXONLY)
			tw32(TG3_CPMU_CTRL,
			     cpmuctrl & ~CPMU_CTRL_GPHY_10MB_RXONLY);
	}

	err = tg3_bmcr_reset(tp);
	if (err)
		return err;

	if (cpmuctrl & CPMU_CTRL_GPHY_10MB_RXONLY) {
		val = MII_TG3_DSP_EXP8_AEDW | MII_TG3_DSP_EXP8_REJ2MHz;
		tg3_phydsp_write(tp, MII_TG3_DSP_EXP8, val);

		tw32(TG3_CPMU_CTRL, cpmuctrl);
	}

	if (tg3_chip_rev(tp) == CHIPREV_5784_AX ||
	    tg3_chip_rev(tp) == CHIPREV_5761_AX) {
		val = tr32(TG3_CPMU_LSPD_1000MB_CLK);
		if ((val & CPMU_LSPD_1000MB_MACCLK_MASK) ==
		    CPMU_LSPD_1000MB_MACCLK_12_5) {
			val &= ~CPMU_LSPD_1000MB_MACCLK_MASK;
			udelay(40);
			tw32_f(TG3_CPMU_LSPD_1000MB_CLK, val);
		}
	}

	if (tg3_flag(tp, 5717_PLUS) &&
	    (tp->phy_flags & TG3_PHYFLG_MII_SERDES))
		return 0;

	tg3_phy_apply_otp(tp);

	if (tp->phy_flags & TG3_PHYFLG_ENABLE_APD)
		tg3_phy_toggle_apd(tp, true);
	else
		tg3_phy_toggle_apd(tp, false);

out:
	if ((tp->phy_flags & TG3_PHYFLG_ADC_BUG) &&
	    !tg3_phy_toggle_auxctl_smdsp(tp, true)) {
		tg3_phydsp_write(tp, 0x201f, 0x2aaa);
		tg3_phydsp_write(tp, 0x000a, 0x0323);
		tg3_phy_toggle_auxctl_smdsp(tp, false);
	}

	if (tp->phy_flags & TG3_PHYFLG_5704_A0_BUG) {
		tg3_writephy(tp, MII_TG3_MISC_SHDW, 0x8d68);
		tg3_writephy(tp, MII_TG3_MISC_SHDW, 0x8d68);
	}

	if (tp->phy_flags & TG3_PHYFLG_BER_BUG) {
		if (!tg3_phy_toggle_auxctl_smdsp(tp, true)) {
			tg3_phydsp_write(tp, 0x000a, 0x310b);
			tg3_phydsp_write(tp, 0x201f, 0x9506);
			tg3_phydsp_write(tp, 0x401f, 0x14e2);
			tg3_phy_toggle_auxctl_smdsp(tp, false);
		}
	} else if (tp->phy_flags & TG3_PHYFLG_JITTER_BUG) {
		if (!tg3_phy_toggle_auxctl_smdsp(tp, true)) {
			tg3_writephy(tp, MII_TG3_DSP_ADDRESS, 0x000a);
			if (tp->phy_flags & TG3_PHYFLG_ADJUST_TRIM) {
				tg3_writephy(tp, MII_TG3_DSP_RW_PORT, 0x110b);
				tg3_writephy(tp, MII_TG3_TEST1,
					     MII_TG3_TEST1_TRIM_EN | 0x4);
			} else
				tg3_writephy(tp, MII_TG3_DSP_RW_PORT, 0x010b);

			tg3_phy_toggle_auxctl_smdsp(tp, false);
		}
	}

	/* Set Extended packet length bit (bit 14) on all chips that */
	/* support jumbo frames */
	if ((tp->phy_id & TG3_PHY_ID_MASK) == TG3_PHY_ID_BCM5401) {
		/* Cannot do read-modify-write on 5401 */
		tg3_phy_auxctl_write(tp, MII_TG3_AUXCTL_SHDWSEL_AUXCTL, 0x4c20);
	} else if (tg3_flag(tp, JUMBO_CAPABLE)) {
		/* Set bit 14 with read-modify-write to preserve other bits */
		err = tg3_phy_auxctl_read(tp,
					  MII_TG3_AUXCTL_SHDWSEL_AUXCTL, &val);
		if (!err)
			tg3_phy_auxctl_write(tp, MII_TG3_AUXCTL_SHDWSEL_AUXCTL,
					   val | MII_TG3_AUXCTL_ACTL_EXTPKTLEN);
	}

	/* Set phy register 0x10 bit 0 to high fifo elasticity to support
	 * jumbo frames transmission.
	 */
	if (tg3_flag(tp, JUMBO_CAPABLE)) {
		if (!tg3_readphy(tp, MII_TG3_EXT_CTRL, &val))
			tg3_writephy(tp, MII_TG3_EXT_CTRL,
				     val | MII_TG3_EXT_CTRL_FIFO_ELASTIC);
	}

	if (tg3_asic_rev(tp) == ASIC_REV_5906) {
		/* adjust output voltage */
		tg3_writephy(tp, MII_TG3_FET_PTEST, 0x12);
	}

	if (tg3_chip_rev_id(tp) == CHIPREV_ID_5762_A0)
		tg3_phydsp_write(tp, 0xffb, 0x4000);

	tg3_phy_toggle_automdix(tp, true);
	tg3_phy_set_wirespeed(tp);
	return 0;
}

#define TG3_GPIO_MSG_DRVR_PRES		 0x00000001
#define TG3_GPIO_MSG_NEED_VAUX		 0x00000002
#define TG3_GPIO_MSG_MASK		 (TG3_GPIO_MSG_DRVR_PRES | \
					  TG3_GPIO_MSG_NEED_VAUX)
#define TG3_GPIO_MSG_ALL_DRVR_PRES_MASK \
	((TG3_GPIO_MSG_DRVR_PRES << 0) | \
	 (TG3_GPIO_MSG_DRVR_PRES << 4) | \
	 (TG3_GPIO_MSG_DRVR_PRES << 8) | \
	 (TG3_GPIO_MSG_DRVR_PRES << 12))

#define TG3_GPIO_MSG_ALL_NEED_VAUX_MASK \
	((TG3_GPIO_MSG_NEED_VAUX << 0) | \
	 (TG3_GPIO_MSG_NEED_VAUX << 4) | \
	 (TG3_GPIO_MSG_NEED_VAUX << 8) | \
	 (TG3_GPIO_MSG_NEED_VAUX << 12))

static inline u32 tg3_set_function_status(struct tg3 *tp, u32 newstat)
{
	u32 status, shift;

	if (tg3_asic_rev(tp) == ASIC_REV_5717 ||
	    tg3_asic_rev(tp) == ASIC_REV_5719)
		status = tg3_ape_read32(tp, TG3_APE_GPIO_MSG);
	else
		status = tr32(TG3_CPMU_DRV_STATUS);

	shift = TG3_APE_GPIO_MSG_SHIFT + 4 * tp->pci_fn;
	status &= ~(TG3_GPIO_MSG_MASK << shift);
	status |= (newstat << shift);

	if (tg3_asic_rev(tp) == ASIC_REV_5717 ||
	    tg3_asic_rev(tp) == ASIC_REV_5719)
		tg3_ape_write32(tp, TG3_APE_GPIO_MSG, status);
	else
		tw32(TG3_CPMU_DRV_STATUS, status);

	return status >> TG3_APE_GPIO_MSG_SHIFT;
}

static inline int tg3_pwrsrc_switch_to_vmain(struct tg3 *tp)
{
	if (!tg3_flag(tp, IS_NIC))
		return 0;

	if (tg3_asic_rev(tp) == ASIC_REV_5717 ||
	    tg3_asic_rev(tp) == ASIC_REV_5719 ||
	    tg3_asic_rev(tp) == ASIC_REV_5720) {
		if (tg3_ape_lock(tp, TG3_APE_LOCK_GPIO))
			return -EIO;

		tg3_set_function_status(tp, TG3_GPIO_MSG_DRVR_PRES);

		tw32_wait_f(GRC_LOCAL_CTRL, tp->grc_local_ctrl,
			    TG3_GRC_LCLCTL_PWRSW_DELAY);

		tg3_ape_unlock(tp, TG3_APE_LOCK_GPIO);
	} else {
		tw32_wait_f(GRC_LOCAL_CTRL, tp->grc_local_ctrl,
			    TG3_GRC_LCLCTL_PWRSW_DELAY);
	}

	return 0;
}

static void tg3_pwrsrc_die_with_vmain(struct tg3 *tp)
{
	u32 grc_local_ctrl;

	if (!tg3_flag(tp, IS_NIC) ||
	    tg3_asic_rev(tp) == ASIC_REV_5700 ||
	    tg3_asic_rev(tp) == ASIC_REV_5701)
		return;

	grc_local_ctrl = tp->grc_local_ctrl | GRC_LCLCTRL_GPIO_OE1;

	tw32_wait_f(GRC_LOCAL_CTRL,
		    grc_local_ctrl | GRC_LCLCTRL_GPIO_OUTPUT1,
		    TG3_GRC_LCLCTL_PWRSW_DELAY);

	tw32_wait_f(GRC_LOCAL_CTRL,
		    grc_local_ctrl,
		    TG3_GRC_LCLCTL_PWRSW_DELAY);

	tw32_wait_f(GRC_LOCAL_CTRL,
		    grc_local_ctrl | GRC_LCLCTRL_GPIO_OUTPUT1,
		    TG3_GRC_LCLCTL_PWRSW_DELAY);
}

static void tg3_pwrsrc_switch_to_vaux(struct tg3 *tp)
{
	if (!tg3_flag(tp, IS_NIC))
		return;

	if (tg3_asic_rev(tp) == ASIC_REV_5700 ||
	    tg3_asic_rev(tp) == ASIC_REV_5701) {
		tw32_wait_f(GRC_LOCAL_CTRL, tp->grc_local_ctrl |
			    (GRC_LCLCTRL_GPIO_OE0 |
			     GRC_LCLCTRL_GPIO_OE1 |
			     GRC_LCLCTRL_GPIO_OE2 |
			     GRC_LCLCTRL_GPIO_OUTPUT0 |
			     GRC_LCLCTRL_GPIO_OUTPUT1),
			    TG3_GRC_LCLCTL_PWRSW_DELAY);
	} else if (tp->pdev->device == PCI_DEVICE_ID_TIGON3_5761 ||
		   tp->pdev->device == TG3PCI_DEVICE_TIGON3_5761S) {
		/* The 5761 non-e device swaps GPIO 0 and GPIO 2. */
		u32 grc_local_ctrl = GRC_LCLCTRL_GPIO_OE0 |
				     GRC_LCLCTRL_GPIO_OE1 |
				     GRC_LCLCTRL_GPIO_OE2 |
				     GRC_LCLCTRL_GPIO_OUTPUT0 |
				     GRC_LCLCTRL_GPIO_OUTPUT1 |
				     tp->grc_local_ctrl;
		tw32_wait_f(GRC_LOCAL_CTRL, grc_local_ctrl,
			    TG3_GRC_LCLCTL_PWRSW_DELAY);

		grc_local_ctrl |= GRC_LCLCTRL_GPIO_OUTPUT2;
		tw32_wait_f(GRC_LOCAL_CTRL, grc_local_ctrl,
			    TG3_GRC_LCLCTL_PWRSW_DELAY);

		grc_local_ctrl &= ~GRC_LCLCTRL_GPIO_OUTPUT0;
		tw32_wait_f(GRC_LOCAL_CTRL, grc_local_ctrl,
			    TG3_GRC_LCLCTL_PWRSW_DELAY);
	} else {
		u32 no_gpio2;
		u32 grc_local_ctrl = 0;

		/* Workaround to prevent overdrawing Amps. */
		if (tg3_asic_rev(tp) == ASIC_REV_5714) {
			grc_local_ctrl |= GRC_LCLCTRL_GPIO_OE3;
			tw32_wait_f(GRC_LOCAL_CTRL, tp->grc_local_ctrl |
				    grc_local_ctrl,
				    TG3_GRC_LCLCTL_PWRSW_DELAY);
		}

		/* On 5753 and variants, GPIO2 cannot be used. */
		no_gpio2 = tp->nic_sram_data_cfg &
			   NIC_SRAM_DATA_CFG_NO_GPIO2;

		grc_local_ctrl |= GRC_LCLCTRL_GPIO_OE0 |
				  GRC_LCLCTRL_GPIO_OE1 |
				  GRC_LCLCTRL_GPIO_OE2 |
				  GRC_LCLCTRL_GPIO_OUTPUT1 |
				  GRC_LCLCTRL_GPIO_OUTPUT2;
		if (no_gpio2) {
			grc_local_ctrl &= ~(GRC_LCLCTRL_GPIO_OE2 |
					    GRC_LCLCTRL_GPIO_OUTPUT2);
		}
		tw32_wait_f(GRC_LOCAL_CTRL,
			    tp->grc_local_ctrl | grc_local_ctrl,
			    TG3_GRC_LCLCTL_PWRSW_DELAY);

		grc_local_ctrl |= GRC_LCLCTRL_GPIO_OUTPUT0;

		tw32_wait_f(GRC_LOCAL_CTRL,
			    tp->grc_local_ctrl | grc_local_ctrl,
			    TG3_GRC_LCLCTL_PWRSW_DELAY);

		if (!no_gpio2) {
			grc_local_ctrl &= ~GRC_LCLCTRL_GPIO_OUTPUT2;
			tw32_wait_f(GRC_LOCAL_CTRL,
				    tp->grc_local_ctrl | grc_local_ctrl,
				    TG3_GRC_LCLCTL_PWRSW_DELAY);
		}
	}
}

static void tg3_frob_aux_power_5717(struct tg3 *tp, bool wol_enable)
{
	u32 msg = 0;

	/* Serialize power state transitions */
	if (tg3_ape_lock(tp, TG3_APE_LOCK_GPIO))
		return;

	if (tg3_flag(tp, ENABLE_ASF) || tg3_flag(tp, ENABLE_APE) || wol_enable)
		msg = TG3_GPIO_MSG_NEED_VAUX;

	msg = tg3_set_function_status(tp, msg);

	if (msg & TG3_GPIO_MSG_ALL_DRVR_PRES_MASK)
		goto done;

	if (msg & TG3_GPIO_MSG_ALL_NEED_VAUX_MASK)
		tg3_pwrsrc_switch_to_vaux(tp);
	else
		tg3_pwrsrc_die_with_vmain(tp);

done:
	tg3_ape_unlock(tp, TG3_APE_LOCK_GPIO);
}

static void tg3_frob_aux_power(struct tg3 *tp, bool include_wol)
{
	bool need_vaux = false;

	/* The GPIOs do something completely different on 57765. */
	if (!tg3_flag(tp, IS_NIC) || tg3_flag(tp, 57765_CLASS))
		return;

	if (tg3_asic_rev(tp) == ASIC_REV_5717 ||
	    tg3_asic_rev(tp) == ASIC_REV_5719 ||
	    tg3_asic_rev(tp) == ASIC_REV_5720) {
		tg3_frob_aux_power_5717(tp, include_wol ?
					tg3_flag(tp, WOL_ENABLE) != 0 : 0);
		return;
	}

	if (tp->pdev_peer && tp->pdev_peer != tp->pdev) {
		struct net_device *dev_peer;

		dev_peer = pci_get_drvdata(tp->pdev_peer);

		/* remove_one() may have been run on the peer. */
		if (dev_peer) {
			struct tg3 *tp_peer = netdev_priv(dev_peer);

			if (tg3_flag(tp_peer, INIT_COMPLETE))
				return;

			if ((include_wol && tg3_flag(tp_peer, WOL_ENABLE)) ||
			    tg3_flag(tp_peer, ENABLE_ASF))
				need_vaux = true;
		}
	}

	if ((include_wol && tg3_flag(tp, WOL_ENABLE)) ||
	    tg3_flag(tp, ENABLE_ASF))
		need_vaux = true;

	if (need_vaux)
		tg3_pwrsrc_switch_to_vaux(tp);
	else
		tg3_pwrsrc_die_with_vmain(tp);
}

static int tg3_5700_link_polarity(struct tg3 *tp, u32 speed)
{
	if (tp->led_ctrl == LED_CTRL_MODE_PHY_2)
		return 1;
	else if ((tp->phy_id & TG3_PHY_ID_MASK) == TG3_PHY_ID_BCM5411) {
		if (speed != SPEED_10)
			return 1;
	} else if (speed == SPEED_10)
		return 1;

	return 0;
}

static bool tg3_phy_power_bug(struct tg3 *tp)
{
	switch (tg3_asic_rev(tp)) {
	case ASIC_REV_5700:
	case ASIC_REV_5704:
		return true;
	case ASIC_REV_5780:
		if (tp->phy_flags & TG3_PHYFLG_MII_SERDES)
			return true;
		return false;
	case ASIC_REV_5717:
		if (!tp->pci_fn)
			return true;
		return false;
	case ASIC_REV_5719:
	case ASIC_REV_5720:
		if ((tp->phy_flags & TG3_PHYFLG_PHY_SERDES) &&
		    !tp->pci_fn)
			return true;
		return false;
	}

	return false;
}

static bool tg3_phy_led_bug(struct tg3 *tp)
{
	switch (tg3_asic_rev(tp)) {
	case ASIC_REV_5719:
		if ((tp->phy_flags & TG3_PHYFLG_MII_SERDES) &&
		    !tp->pci_fn)
			return true;
		return false;
	}

	return false;
}

static void tg3_power_down_phy(struct tg3 *tp, bool do_low_power)
{
	u32 val;

	if (tp->phy_flags & TG3_PHYFLG_KEEP_LINK_ON_PWRDN)
		return;

	if (tp->phy_flags & TG3_PHYFLG_PHY_SERDES) {
		if (tg3_asic_rev(tp) == ASIC_REV_5704) {
			u32 sg_dig_ctrl = tr32(SG_DIG_CTRL);
			u32 serdes_cfg = tr32(MAC_SERDES_CFG);

			sg_dig_ctrl |=
				SG_DIG_USING_HW_AUTONEG | SG_DIG_SOFT_RESET;
			tw32(SG_DIG_CTRL, sg_dig_ctrl);
			tw32(MAC_SERDES_CFG, serdes_cfg | (1 << 15));
		}
		return;
	}

	if (tg3_asic_rev(tp) == ASIC_REV_5906) {
		tg3_bmcr_reset(tp);
		val = tr32(GRC_MISC_CFG);
		tw32_f(GRC_MISC_CFG, val | GRC_MISC_CFG_EPHY_IDDQ);
		udelay(40);
		return;
	} else if (tp->phy_flags & TG3_PHYFLG_IS_FET) {
		u32 phytest;
		if (!tg3_readphy(tp, MII_TG3_FET_TEST, &phytest)) {
			u32 phy;

			tg3_writephy(tp, MII_ADVERTISE, 0);
			tg3_writephy(tp, MII_BMCR,
				     BMCR_ANENABLE | BMCR_ANRESTART);

			tg3_writephy(tp, MII_TG3_FET_TEST,
				     phytest | MII_TG3_FET_SHADOW_EN);
			if (!tg3_readphy(tp, MII_TG3_FET_SHDW_AUXMODE4, &phy)) {
				phy |= MII_TG3_FET_SHDW_AUXMODE4_SBPD;
				tg3_writephy(tp,
					     MII_TG3_FET_SHDW_AUXMODE4,
					     phy);
			}
			tg3_writephy(tp, MII_TG3_FET_TEST, phytest);
		}
		return;
	} else if (do_low_power) {
		if (!tg3_phy_led_bug(tp))
			tg3_writephy(tp, MII_TG3_EXT_CTRL,
				     MII_TG3_EXT_CTRL_FORCE_LED_OFF);

		val = MII_TG3_AUXCTL_PCTL_100TX_LPWR |
		      MII_TG3_AUXCTL_PCTL_SPR_ISOLATE |
		      MII_TG3_AUXCTL_PCTL_VREG_11V;
		tg3_phy_auxctl_write(tp, MII_TG3_AUXCTL_SHDWSEL_PWRCTL, val);
	}

	/* The PHY should not be powered down on some chips because
	 * of bugs.
	 */
	if (tg3_phy_power_bug(tp))
		return;

	if (tg3_chip_rev(tp) == CHIPREV_5784_AX ||
	    tg3_chip_rev(tp) == CHIPREV_5761_AX) {
		val = tr32(TG3_CPMU_LSPD_1000MB_CLK);
		val &= ~CPMU_LSPD_1000MB_MACCLK_MASK;
		val |= CPMU_LSPD_1000MB_MACCLK_12_5;
		tw32_f(TG3_CPMU_LSPD_1000MB_CLK, val);
	}

	tg3_writephy(tp, MII_BMCR, BMCR_PDOWN);
}

/* tp->lock is held. */
static int tg3_nvram_lock(struct tg3 *tp)
{
	if (tg3_flag(tp, NVRAM)) {
		int i;

		if (tp->nvram_lock_cnt == 0) {
			tw32(NVRAM_SWARB, SWARB_REQ_SET1);
			for (i = 0; i < 8000; i++) {
				if (tr32(NVRAM_SWARB) & SWARB_GNT1)
					break;
				udelay(20);
			}
			if (i == 8000) {
				tw32(NVRAM_SWARB, SWARB_REQ_CLR1);
				return -ENODEV;
			}
		}
		tp->nvram_lock_cnt++;
	}
	return 0;
}

/* tp->lock is held. */
static void tg3_nvram_unlock(struct tg3 *tp)
{
	if (tg3_flag(tp, NVRAM)) {
		if (tp->nvram_lock_cnt > 0)
			tp->nvram_lock_cnt--;
		if (tp->nvram_lock_cnt == 0)
			tw32_f(NVRAM_SWARB, SWARB_REQ_CLR1);
	}
}

/* tp->lock is held. */
static void tg3_enable_nvram_access(struct tg3 *tp)
{
	if (tg3_flag(tp, 5750_PLUS) && !tg3_flag(tp, PROTECTED_NVRAM)) {
		u32 nvaccess = tr32(NVRAM_ACCESS);

		tw32(NVRAM_ACCESS, nvaccess | ACCESS_ENABLE);
	}
}

/* tp->lock is held. */
static void tg3_disable_nvram_access(struct tg3 *tp)
{
	if (tg3_flag(tp, 5750_PLUS) && !tg3_flag(tp, PROTECTED_NVRAM)) {
		u32 nvaccess = tr32(NVRAM_ACCESS);

		tw32(NVRAM_ACCESS, nvaccess & ~ACCESS_ENABLE);
	}
}

static int tg3_nvram_read_using_eeprom(struct tg3 *tp,
					u32 offset, u32 *val)
{
	u32 tmp;
	int i;

	if (offset > EEPROM_ADDR_ADDR_MASK || (offset % 4) != 0)
		return -EINVAL;

	tmp = tr32(GRC_EEPROM_ADDR) & ~(EEPROM_ADDR_ADDR_MASK |
					EEPROM_ADDR_DEVID_MASK |
					EEPROM_ADDR_READ);
	tw32(GRC_EEPROM_ADDR,
	     tmp |
	     (0 << EEPROM_ADDR_DEVID_SHIFT) |
	     ((offset << EEPROM_ADDR_ADDR_SHIFT) &
	      EEPROM_ADDR_ADDR_MASK) |
	     EEPROM_ADDR_READ | EEPROM_ADDR_START);

	for (i = 0; i < 1000; i++) {
		tmp = tr32(GRC_EEPROM_ADDR);

		if (tmp & EEPROM_ADDR_COMPLETE)
			break;
		msleep(1);
	}
	if (!(tmp & EEPROM_ADDR_COMPLETE))
		return -EBUSY;

	tmp = tr32(GRC_EEPROM_DATA);

	/*
	 * The data will always be opposite the native endian
	 * format.  Perform a blind byteswap to compensate.
	 */
	*val = swab32(tmp);

	return 0;
}

#define NVRAM_CMD_TIMEOUT 10000

static int tg3_nvram_exec_cmd(struct tg3 *tp, u32 nvram_cmd)
{
	int i;

	tw32(NVRAM_CMD, nvram_cmd);
	for (i = 0; i < NVRAM_CMD_TIMEOUT; i++) {
		udelay(10);
		if (tr32(NVRAM_CMD) & NVRAM_CMD_DONE) {
			udelay(10);
			break;
		}
	}

	if (i == NVRAM_CMD_TIMEOUT)
		return -EBUSY;

	return 0;
}

static u32 tg3_nvram_phys_addr(struct tg3 *tp, u32 addr)
{
	if (tg3_flag(tp, NVRAM) &&
	    tg3_flag(tp, NVRAM_BUFFERED) &&
	    tg3_flag(tp, FLASH) &&
	    !tg3_flag(tp, NO_NVRAM_ADDR_TRANS) &&
	    (tp->nvram_jedecnum == JEDEC_ATMEL))

		addr = ((addr / tp->nvram_pagesize) <<
			ATMEL_AT45DB0X1B_PAGE_POS) +
		       (addr % tp->nvram_pagesize);

	return addr;
}

static u32 tg3_nvram_logical_addr(struct tg3 *tp, u32 addr)
{
	if (tg3_flag(tp, NVRAM) &&
	    tg3_flag(tp, NVRAM_BUFFERED) &&
	    tg3_flag(tp, FLASH) &&
	    !tg3_flag(tp, NO_NVRAM_ADDR_TRANS) &&
	    (tp->nvram_jedecnum == JEDEC_ATMEL))

		addr = ((addr >> ATMEL_AT45DB0X1B_PAGE_POS) *
			tp->nvram_pagesize) +
		       (addr & ((1 << ATMEL_AT45DB0X1B_PAGE_POS) - 1));

	return addr;
}

/* NOTE: Data read in from NVRAM is byteswapped according to
 * the byteswapping settings for all other register accesses.
 * tg3 devices are BE devices, so on a BE machine, the data
 * returned will be exactly as it is seen in NVRAM.  On a LE
 * machine, the 32-bit value will be byteswapped.
 */
static int tg3_nvram_read(struct tg3 *tp, u32 offset, u32 *val)
{
	int ret;

	if (!tg3_flag(tp, NVRAM))
		return tg3_nvram_read_using_eeprom(tp, offset, val);

	offset = tg3_nvram_phys_addr(tp, offset);

	if (offset > NVRAM_ADDR_MSK)
		return -EINVAL;

	ret = tg3_nvram_lock(tp);
	if (ret)
		return ret;

	tg3_enable_nvram_access(tp);

	tw32(NVRAM_ADDR, offset);
	ret = tg3_nvram_exec_cmd(tp, NVRAM_CMD_RD | NVRAM_CMD_GO |
		NVRAM_CMD_FIRST | NVRAM_CMD_LAST | NVRAM_CMD_DONE);

	if (ret == 0)
		*val = tr32(NVRAM_RDDATA);

	tg3_disable_nvram_access(tp);

	tg3_nvram_unlock(tp);

	return ret;
}

/* Ensures NVRAM data is in bytestream format. */
static int tg3_nvram_read_be32(struct tg3 *tp, u32 offset, __be32 *val)
{
	u32 v;
	int res = tg3_nvram_read(tp, offset, &v);
	if (!res)
		*val = cpu_to_be32(v);
	return res;
}

static int tg3_nvram_write_block_using_eeprom(struct tg3 *tp,
				    u32 offset, u32 len, u8 *buf)
{
	int i, j, rc = 0;
	u32 val;

	for (i = 0; i < len; i += 4) {
		u32 addr;
		__be32 data;

		addr = offset + i;

		memcpy(&data, buf + i, 4);

		/*
		 * The SEEPROM interface expects the data to always be opposite
		 * the native endian format.  We accomplish this by reversing
		 * all the operations that would have been performed on the
		 * data from a call to tg3_nvram_read_be32().
		 */
		tw32(GRC_EEPROM_DATA, swab32(be32_to_cpu(data)));

		val = tr32(GRC_EEPROM_ADDR);
		tw32(GRC_EEPROM_ADDR, val | EEPROM_ADDR_COMPLETE);

		val &= ~(EEPROM_ADDR_ADDR_MASK | EEPROM_ADDR_DEVID_MASK |
			EEPROM_ADDR_READ);
		tw32(GRC_EEPROM_ADDR, val |
			(0 << EEPROM_ADDR_DEVID_SHIFT) |
			(addr & EEPROM_ADDR_ADDR_MASK) |
			EEPROM_ADDR_START |
			EEPROM_ADDR_WRITE);

		for (j = 0; j < 1000; j++) {
			val = tr32(GRC_EEPROM_ADDR);

			if (val & EEPROM_ADDR_COMPLETE)
				break;
			msleep(1);
		}
		if (!(val & EEPROM_ADDR_COMPLETE)) {
			rc = -EBUSY;
			break;
		}
	}

	return rc;
}

/* offset and length are dword aligned */
static int tg3_nvram_write_block_unbuffered(struct tg3 *tp, u32 offset, u32 len,
		u8 *buf)
{
	int ret = 0;
	u32 pagesize = tp->nvram_pagesize;
	u32 pagemask = pagesize - 1;
	u32 nvram_cmd;
	u8 *tmp;

	tmp = kmalloc(pagesize, GFP_KERNEL);
	if (tmp == NULL)
		return -ENOMEM;

	while (len) {
		int j;
		u32 phy_addr, page_off, size;

		phy_addr = offset & ~pagemask;

		for (j = 0; j < pagesize; j += 4) {
			ret = tg3_nvram_read_be32(tp, phy_addr + j,
						  (__be32 *) (tmp + j));
			if (ret)
				break;
		}
		if (ret)
			break;

		page_off = offset & pagemask;
		size = pagesize;
		if (len < size)
			size = len;

		len -= size;

		memcpy(tmp + page_off, buf, size);

		offset = offset + (pagesize - page_off);

		tg3_enable_nvram_access(tp);

		/*
		 * Before we can erase the flash page, we need
		 * to issue a special "write enable" command.
		 */
		nvram_cmd = NVRAM_CMD_WREN | NVRAM_CMD_GO | NVRAM_CMD_DONE;

		if (tg3_nvram_exec_cmd(tp, nvram_cmd))
			break;

		/* Erase the target page */
		tw32(NVRAM_ADDR, phy_addr);

		nvram_cmd = NVRAM_CMD_GO | NVRAM_CMD_DONE | NVRAM_CMD_WR |
			NVRAM_CMD_FIRST | NVRAM_CMD_LAST | NVRAM_CMD_ERASE;

		if (tg3_nvram_exec_cmd(tp, nvram_cmd))
			break;

		/* Issue another write enable to start the write. */
		nvram_cmd = NVRAM_CMD_WREN | NVRAM_CMD_GO | NVRAM_CMD_DONE;

		if (tg3_nvram_exec_cmd(tp, nvram_cmd))
			break;

		for (j = 0; j < pagesize; j += 4) {
			__be32 data;

			data = *((__be32 *) (tmp + j));

			tw32(NVRAM_WRDATA, be32_to_cpu(data));

			tw32(NVRAM_ADDR, phy_addr + j);

			nvram_cmd = NVRAM_CMD_GO | NVRAM_CMD_DONE |
				NVRAM_CMD_WR;

			if (j == 0)
				nvram_cmd |= NVRAM_CMD_FIRST;
			else if (j == (pagesize - 4))
				nvram_cmd |= NVRAM_CMD_LAST;

			ret = tg3_nvram_exec_cmd(tp, nvram_cmd);
			if (ret)
				break;
		}
		if (ret)
			break;
	}

	nvram_cmd = NVRAM_CMD_WRDI | NVRAM_CMD_GO | NVRAM_CMD_DONE;
	tg3_nvram_exec_cmd(tp, nvram_cmd);

	kfree(tmp);

	return ret;
}

/* offset and length are dword aligned */
static int tg3_nvram_write_block_buffered(struct tg3 *tp, u32 offset, u32 len,
		u8 *buf)
{
	int i, ret = 0;

	for (i = 0; i < len; i += 4, offset += 4) {
		u32 page_off, phy_addr, nvram_cmd;
		__be32 data;

		memcpy(&data, buf + i, 4);
		tw32(NVRAM_WRDATA, be32_to_cpu(data));

		page_off = offset % tp->nvram_pagesize;

		phy_addr = tg3_nvram_phys_addr(tp, offset);

		nvram_cmd = NVRAM_CMD_GO | NVRAM_CMD_DONE | NVRAM_CMD_WR;

		if (page_off == 0 || i == 0)
			nvram_cmd |= NVRAM_CMD_FIRST;
		if (page_off == (tp->nvram_pagesize - 4))
			nvram_cmd |= NVRAM_CMD_LAST;

		if (i == (len - 4))
			nvram_cmd |= NVRAM_CMD_LAST;

		if ((nvram_cmd & NVRAM_CMD_FIRST) ||
		    !tg3_flag(tp, FLASH) ||
		    !tg3_flag(tp, 57765_PLUS))
			tw32(NVRAM_ADDR, phy_addr);

		if (tg3_asic_rev(tp) != ASIC_REV_5752 &&
		    !tg3_flag(tp, 5755_PLUS) &&
		    (tp->nvram_jedecnum == JEDEC_ST) &&
		    (nvram_cmd & NVRAM_CMD_FIRST)) {
			u32 cmd;

			cmd = NVRAM_CMD_WREN | NVRAM_CMD_GO | NVRAM_CMD_DONE;
			ret = tg3_nvram_exec_cmd(tp, cmd);
			if (ret)
				break;
		}
		if (!tg3_flag(tp, FLASH)) {
			/* We always do complete word writes to eeprom. */
			nvram_cmd |= (NVRAM_CMD_FIRST | NVRAM_CMD_LAST);
		}

		ret = tg3_nvram_exec_cmd(tp, nvram_cmd);
		if (ret)
			break;
	}
	return ret;
}

/* offset and length are dword aligned */
static int tg3_nvram_write_block(struct tg3 *tp, u32 offset, u32 len, u8 *buf)
{
	int ret;

	if (tg3_flag(tp, EEPROM_WRITE_PROT)) {
		tw32_f(GRC_LOCAL_CTRL, tp->grc_local_ctrl &
		       ~GRC_LCLCTRL_GPIO_OUTPUT1);
		udelay(40);
	}

	if (!tg3_flag(tp, NVRAM)) {
		ret = tg3_nvram_write_block_using_eeprom(tp, offset, len, buf);
	} else {
		u32 grc_mode;

		ret = tg3_nvram_lock(tp);
		if (ret)
			return ret;

		tg3_enable_nvram_access(tp);
		if (tg3_flag(tp, 5750_PLUS) && !tg3_flag(tp, PROTECTED_NVRAM))
			tw32(NVRAM_WRITE1, 0x406);

		grc_mode = tr32(GRC_MODE);
		tw32(GRC_MODE, grc_mode | GRC_MODE_NVRAM_WR_ENABLE);

		if (tg3_flag(tp, NVRAM_BUFFERED) || !tg3_flag(tp, FLASH)) {
			ret = tg3_nvram_write_block_buffered(tp, offset, len,
				buf);
		} else {
			ret = tg3_nvram_write_block_unbuffered(tp, offset, len,
				buf);
		}

		grc_mode = tr32(GRC_MODE);
		tw32(GRC_MODE, grc_mode & ~GRC_MODE_NVRAM_WR_ENABLE);

		tg3_disable_nvram_access(tp);
		tg3_nvram_unlock(tp);
	}

	if (tg3_flag(tp, EEPROM_WRITE_PROT)) {
		tw32_f(GRC_LOCAL_CTRL, tp->grc_local_ctrl);
		udelay(40);
	}

	return ret;
}

#define RX_CPU_SCRATCH_BASE	0x30000
#define RX_CPU_SCRATCH_SIZE	0x04000
#define TX_CPU_SCRATCH_BASE	0x34000
#define TX_CPU_SCRATCH_SIZE	0x04000

/* tp->lock is held. */
static int tg3_pause_cpu(struct tg3 *tp, u32 cpu_base)
{
	int i;
	const int iters = 10000;

	for (i = 0; i < iters; i++) {
		tw32(cpu_base + CPU_STATE, 0xffffffff);
		tw32(cpu_base + CPU_MODE,  CPU_MODE_HALT);
		if (tr32(cpu_base + CPU_MODE) & CPU_MODE_HALT)
			break;
		if (pci_channel_offline(tp->pdev))
			return -EBUSY;
	}

	return (i == iters) ? -EBUSY : 0;
}

/* tp->lock is held. */
static int tg3_rxcpu_pause(struct tg3 *tp)
{
	int rc = tg3_pause_cpu(tp, RX_CPU_BASE);

	tw32(RX_CPU_BASE + CPU_STATE, 0xffffffff);
	tw32_f(RX_CPU_BASE + CPU_MODE,  CPU_MODE_HALT);
	udelay(10);

	return rc;
}

/* tp->lock is held. */
static int tg3_txcpu_pause(struct tg3 *tp)
{
	return tg3_pause_cpu(tp, TX_CPU_BASE);
}

/* tp->lock is held. */
static void tg3_resume_cpu(struct tg3 *tp, u32 cpu_base)
{
	tw32(cpu_base + CPU_STATE, 0xffffffff);
	tw32_f(cpu_base + CPU_MODE,  0x00000000);
}

/* tp->lock is held. */
static void tg3_rxcpu_resume(struct tg3 *tp)
{
	tg3_resume_cpu(tp, RX_CPU_BASE);
}

/* tp->lock is held. */
static int tg3_halt_cpu(struct tg3 *tp, u32 cpu_base)
{
	int rc;

	BUG_ON(cpu_base == TX_CPU_BASE && tg3_flag(tp, 5705_PLUS));

	if (tg3_asic_rev(tp) == ASIC_REV_5906) {
		u32 val = tr32(GRC_VCPU_EXT_CTRL);

		tw32(GRC_VCPU_EXT_CTRL, val | GRC_VCPU_EXT_CTRL_HALT_CPU);
		return 0;
	}
	if (cpu_base == RX_CPU_BASE) {
		rc = tg3_rxcpu_pause(tp);
	} else {
		/*
		 * There is only an Rx CPU for the 5750 derivative in the
		 * BCM4785.
		 */
		if (tg3_flag(tp, IS_SSB_CORE))
			return 0;

		rc = tg3_txcpu_pause(tp);
	}

	if (rc) {
		netdev_err(tp->dev, "%s timed out, %s CPU\n",
			   __func__, cpu_base == RX_CPU_BASE ? "RX" : "TX");
		return -ENODEV;
	}

	/* Clear firmware's nvram arbitration. */
	if (tg3_flag(tp, NVRAM))
		tw32(NVRAM_SWARB, SWARB_REQ_CLR0);
	return 0;
}

static int tg3_fw_data_len(struct tg3 *tp,
			   const struct tg3_firmware_hdr *fw_hdr)
{
	int fw_len;

	/* Non fragmented firmware have one firmware header followed by a
	 * contiguous chunk of data to be written. The length field in that
	 * header is not the length of data to be written but the complete
	 * length of the bss. The data length is determined based on
	 * tp->fw->size minus headers.
	 *
	 * Fragmented firmware have a main header followed by multiple
	 * fragments. Each fragment is identical to non fragmented firmware
	 * with a firmware header followed by a contiguous chunk of data. In
	 * the main header, the length field is unused and set to 0xffffffff.
	 * In each fragment header the length is the entire size of that
	 * fragment i.e. fragment data + header length. Data length is
	 * therefore length field in the header minus TG3_FW_HDR_LEN.
	 */
	if (tp->fw_len == 0xffffffff)
		fw_len = be32_to_cpu(fw_hdr->len);
	else
		fw_len = tp->fw->size;

	return (fw_len - TG3_FW_HDR_LEN) / sizeof(u32);
}

/* tp->lock is held. */
static int tg3_load_firmware_cpu(struct tg3 *tp, u32 cpu_base,
				 u32 cpu_scratch_base, int cpu_scratch_size,
				 const struct tg3_firmware_hdr *fw_hdr)
{
	int err, i;
	void (*write_op)(struct tg3 *, u32, u32);
	int total_len = tp->fw->size;

	if (cpu_base == TX_CPU_BASE && tg3_flag(tp, 5705_PLUS)) {
		netdev_err(tp->dev,
			   "%s: Trying to load TX cpu firmware which is 5705\n",
			   __func__);
		return -EINVAL;
	}

	if (tg3_flag(tp, 5705_PLUS) && tg3_asic_rev(tp) != ASIC_REV_57766)
		write_op = tg3_write_mem;
	else
		write_op = tg3_write_indirect_reg32;

	if (tg3_asic_rev(tp) != ASIC_REV_57766) {
		/* It is possible that bootcode is still loading at this point.
		 * Get the nvram lock first before halting the cpu.
		 */
		int lock_err = tg3_nvram_lock(tp);
		err = tg3_halt_cpu(tp, cpu_base);
		if (!lock_err)
			tg3_nvram_unlock(tp);
		if (err)
			goto out;

		for (i = 0; i < cpu_scratch_size; i += sizeof(u32))
			write_op(tp, cpu_scratch_base + i, 0);
		tw32(cpu_base + CPU_STATE, 0xffffffff);
		tw32(cpu_base + CPU_MODE,
		     tr32(cpu_base + CPU_MODE) | CPU_MODE_HALT);
	} else {
		/* Subtract additional main header for fragmented firmware and
		 * advance to the first fragment
		 */
		total_len -= TG3_FW_HDR_LEN;
		fw_hdr++;
	}

	do {
		u32 *fw_data = (u32 *)(fw_hdr + 1);
		for (i = 0; i < tg3_fw_data_len(tp, fw_hdr); i++)
			write_op(tp, cpu_scratch_base +
				     (be32_to_cpu(fw_hdr->base_addr) & 0xffff) +
				     (i * sizeof(u32)),
				 be32_to_cpu(fw_data[i]));

		total_len -= be32_to_cpu(fw_hdr->len);

		/* Advance to next fragment */
		fw_hdr = (struct tg3_firmware_hdr *)
			 ((void *)fw_hdr + be32_to_cpu(fw_hdr->len));
	} while (total_len > 0);

	err = 0;

out:
	return err;
}

/* tp->lock is held. */
static int tg3_pause_cpu_and_set_pc(struct tg3 *tp, u32 cpu_base, u32 pc)
{
	int i;
	const int iters = 5;

	tw32(cpu_base + CPU_STATE, 0xffffffff);
	tw32_f(cpu_base + CPU_PC, pc);

	for (i = 0; i < iters; i++) {
		if (tr32(cpu_base + CPU_PC) == pc)
			break;
		tw32(cpu_base + CPU_STATE, 0xffffffff);
		tw32(cpu_base + CPU_MODE,  CPU_MODE_HALT);
		tw32_f(cpu_base + CPU_PC, pc);
		udelay(1000);
	}

	return (i == iters) ? -EBUSY : 0;
}

/* tp->lock is held. */
static int tg3_load_5701_a0_firmware_fix(struct tg3 *tp)
{
	const struct tg3_firmware_hdr *fw_hdr;
	int err;

	fw_hdr = (struct tg3_firmware_hdr *)tp->fw->data;

	/* Firmware blob starts with version numbers, followed by
	   start address and length. We are setting complete length.
	   length = end_address_of_bss - start_address_of_text.
	   Remainder is the blob to be loaded contiguously
	   from start address. */

	err = tg3_load_firmware_cpu(tp, RX_CPU_BASE,
				    RX_CPU_SCRATCH_BASE, RX_CPU_SCRATCH_SIZE,
				    fw_hdr);
	if (err)
		return err;

	err = tg3_load_firmware_cpu(tp, TX_CPU_BASE,
				    TX_CPU_SCRATCH_BASE, TX_CPU_SCRATCH_SIZE,
				    fw_hdr);
	if (err)
		return err;

	/* Now startup only the RX cpu. */
	err = tg3_pause_cpu_and_set_pc(tp, RX_CPU_BASE,
				       be32_to_cpu(fw_hdr->base_addr));
	if (err) {
		netdev_err(tp->dev, "%s fails to set RX CPU PC, is %08x "
			   "should be %08x\n", __func__,
			   tr32(RX_CPU_BASE + CPU_PC),
				be32_to_cpu(fw_hdr->base_addr));
		return -ENODEV;
	}

	tg3_rxcpu_resume(tp);

	return 0;
}

static int tg3_validate_rxcpu_state(struct tg3 *tp)
{
	const int iters = 1000;
	int i;
	u32 val;

	/* Wait for boot code to complete initialization and enter service
	 * loop. It is then safe to download service patches
	 */
	for (i = 0; i < iters; i++) {
		if (tr32(RX_CPU_HWBKPT) == TG3_SBROM_IN_SERVICE_LOOP)
			break;

		udelay(10);
	}

	if (i == iters) {
		netdev_err(tp->dev, "Boot code not ready for service patches\n");
		return -EBUSY;
	}

	val = tg3_read_indirect_reg32(tp, TG3_57766_FW_HANDSHAKE);
	if (val & 0xff) {
		netdev_warn(tp->dev,
			    "Other patches exist. Not downloading EEE patch\n");
		return -EEXIST;
	}

	return 0;
}

/* tp->lock is held. */
static void tg3_load_57766_firmware(struct tg3 *tp)
{
	struct tg3_firmware_hdr *fw_hdr;

	if (!tg3_flag(tp, NO_NVRAM))
		return;

	if (tg3_validate_rxcpu_state(tp))
		return;

	if (!tp->fw)
		return;

	/* This firmware blob has a different format than older firmware
	 * releases as given below. The main difference is we have fragmented
	 * data to be written to non-contiguous locations.
	 *
	 * In the beginning we have a firmware header identical to other
	 * firmware which consists of version, base addr and length. The length
	 * here is unused and set to 0xffffffff.
	 *
	 * This is followed by a series of firmware fragments which are
	 * individually identical to previous firmware. i.e. they have the
	 * firmware header and followed by data for that fragment. The version
	 * field of the individual fragment header is unused.
	 */

	fw_hdr = (struct tg3_firmware_hdr *)tp->fw->data;
	if (be32_to_cpu(fw_hdr->base_addr) != TG3_57766_FW_BASE_ADDR)
		return;

	if (tg3_rxcpu_pause(tp))
		return;

	/* tg3_load_firmware_cpu() will always succeed for the 57766 */
	tg3_load_firmware_cpu(tp, 0, TG3_57766_FW_BASE_ADDR, 0, fw_hdr);

	tg3_rxcpu_resume(tp);
}

/* tp->lock is held. */
static int tg3_load_tso_firmware(struct tg3 *tp)
{
	const struct tg3_firmware_hdr *fw_hdr;
	unsigned long cpu_base, cpu_scratch_base, cpu_scratch_size;
	int err;

	if (!tg3_flag(tp, FW_TSO))
		return 0;

	fw_hdr = (struct tg3_firmware_hdr *)tp->fw->data;

	/* Firmware blob starts with version numbers, followed by
	   start address and length. We are setting complete length.
	   length = end_address_of_bss - start_address_of_text.
	   Remainder is the blob to be loaded contiguously
	   from start address. */

	cpu_scratch_size = tp->fw_len;

	if (tg3_asic_rev(tp) == ASIC_REV_5705) {
		cpu_base = RX_CPU_BASE;
		cpu_scratch_base = NIC_SRAM_MBUF_POOL_BASE5705;
	} else {
		cpu_base = TX_CPU_BASE;
		cpu_scratch_base = TX_CPU_SCRATCH_BASE;
		cpu_scratch_size = TX_CPU_SCRATCH_SIZE;
	}

	err = tg3_load_firmware_cpu(tp, cpu_base,
				    cpu_scratch_base, cpu_scratch_size,
				    fw_hdr);
	if (err)
		return err;

	/* Now startup the cpu. */
	err = tg3_pause_cpu_and_set_pc(tp, cpu_base,
				       be32_to_cpu(fw_hdr->base_addr));
	if (err) {
		netdev_err(tp->dev,
			   "%s fails to set CPU PC, is %08x should be %08x\n",
			   __func__, tr32(cpu_base + CPU_PC),
			   be32_to_cpu(fw_hdr->base_addr));
		return -ENODEV;
	}

	tg3_resume_cpu(tp, cpu_base);
	return 0;
}


/* tp->lock is held. */
static void __tg3_set_mac_addr(struct tg3 *tp, bool skip_mac_1)
{
	u32 addr_high, addr_low;
	int i;

	addr_high = ((tp->dev->dev_addr[0] << 8) |
		     tp->dev->dev_addr[1]);
	addr_low = ((tp->dev->dev_addr[2] << 24) |
		    (tp->dev->dev_addr[3] << 16) |
		    (tp->dev->dev_addr[4] <<  8) |
		    (tp->dev->dev_addr[5] <<  0));
	for (i = 0; i < 4; i++) {
		if (i == 1 && skip_mac_1)
			continue;
		tw32(MAC_ADDR_0_HIGH + (i * 8), addr_high);
		tw32(MAC_ADDR_0_LOW + (i * 8), addr_low);
	}

	if (tg3_asic_rev(tp) == ASIC_REV_5703 ||
	    tg3_asic_rev(tp) == ASIC_REV_5704) {
		for (i = 0; i < 12; i++) {
			tw32(MAC_EXTADDR_0_HIGH + (i * 8), addr_high);
			tw32(MAC_EXTADDR_0_LOW + (i * 8), addr_low);
		}
	}

	addr_high = (tp->dev->dev_addr[0] +
		     tp->dev->dev_addr[1] +
		     tp->dev->dev_addr[2] +
		     tp->dev->dev_addr[3] +
		     tp->dev->dev_addr[4] +
		     tp->dev->dev_addr[5]) &
		TX_BACKOFF_SEED_MASK;
	tw32(MAC_TX_BACKOFF_SEED, addr_high);
}

static void tg3_enable_register_access(struct tg3 *tp)
{
	/*
	 * Make sure register accesses (indirect or otherwise) will function
	 * correctly.
	 */
	pci_write_config_dword(tp->pdev,
			       TG3PCI_MISC_HOST_CTRL, tp->misc_host_ctrl);
}

static int tg3_power_up(struct tg3 *tp)
{
	int err;

	tg3_enable_register_access(tp);

	err = pci_set_power_state(tp->pdev, PCI_D0);
	if (!err) {
		/* Switch out of Vaux if it is a NIC */
		tg3_pwrsrc_switch_to_vmain(tp);
	} else {
		netdev_err(tp->dev, "Transition to D0 failed\n");
	}

	return err;
}

static int tg3_setup_phy(struct tg3 *, bool);

static int tg3_power_down_prepare(struct tg3 *tp)
{
	u32 misc_host_ctrl;
	bool device_should_wake, do_low_power;

	tg3_enable_register_access(tp);

	/* Restore the CLKREQ setting. */
	if (tg3_flag(tp, CLKREQ_BUG))
		pcie_capability_set_word(tp->pdev, PCI_EXP_LNKCTL,
					 PCI_EXP_LNKCTL_CLKREQ_EN);

	misc_host_ctrl = tr32(TG3PCI_MISC_HOST_CTRL);
	tw32(TG3PCI_MISC_HOST_CTRL,
	     misc_host_ctrl | MISC_HOST_CTRL_MASK_PCI_INT);

	device_should_wake = device_may_wakeup(&tp->pdev->dev) &&
			     tg3_flag(tp, WOL_ENABLE);

	if (tg3_flag(tp, USE_PHYLIB)) {
		do_low_power = false;
		if ((tp->phy_flags & TG3_PHYFLG_IS_CONNECTED) &&
		    !(tp->phy_flags & TG3_PHYFLG_IS_LOW_POWER)) {
			struct phy_device *phydev;
			u32 phyid, advertising;

			phydev = tp->mdio_bus->phy_map[TG3_PHY_MII_ADDR];

			tp->phy_flags |= TG3_PHYFLG_IS_LOW_POWER;

			tp->link_config.speed = phydev->speed;
			tp->link_config.duplex = phydev->duplex;
			tp->link_config.autoneg = phydev->autoneg;
			tp->link_config.advertising = phydev->advertising;

			advertising = ADVERTISED_TP |
				      ADVERTISED_Pause |
				      ADVERTISED_Autoneg |
				      ADVERTISED_10baseT_Half;

			if (tg3_flag(tp, ENABLE_ASF) || device_should_wake) {
				if (tg3_flag(tp, WOL_SPEED_100MB))
					advertising |=
						ADVERTISED_100baseT_Half |
						ADVERTISED_100baseT_Full |
						ADVERTISED_10baseT_Full;
				else
					advertising |= ADVERTISED_10baseT_Full;
			}

			phydev->advertising = advertising;

			phy_start_aneg(phydev);

			phyid = phydev->drv->phy_id & phydev->drv->phy_id_mask;
			if (phyid != PHY_ID_BCMAC131) {
				phyid &= PHY_BCM_OUI_MASK;
				if (phyid == PHY_BCM_OUI_1 ||
				    phyid == PHY_BCM_OUI_2 ||
				    phyid == PHY_BCM_OUI_3)
					do_low_power = true;
			}
		}
	} else {
		do_low_power = true;

		if (!(tp->phy_flags & TG3_PHYFLG_IS_LOW_POWER))
			tp->phy_flags |= TG3_PHYFLG_IS_LOW_POWER;

		if (!(tp->phy_flags & TG3_PHYFLG_ANY_SERDES))
			tg3_setup_phy(tp, false);
	}

	if (tg3_asic_rev(tp) == ASIC_REV_5906) {
		u32 val;

		val = tr32(GRC_VCPU_EXT_CTRL);
		tw32(GRC_VCPU_EXT_CTRL, val | GRC_VCPU_EXT_CTRL_DISABLE_WOL);
	} else if (!tg3_flag(tp, ENABLE_ASF)) {
		int i;
		u32 val;

		for (i = 0; i < 200; i++) {
			tg3_read_mem(tp, NIC_SRAM_FW_ASF_STATUS_MBOX, &val);
			if (val == ~NIC_SRAM_FIRMWARE_MBOX_MAGIC1)
				break;
			msleep(1);
		}
	}
	if (tg3_flag(tp, WOL_CAP))
		tg3_write_mem(tp, NIC_SRAM_WOL_MBOX, WOL_SIGNATURE |
						     WOL_DRV_STATE_SHUTDOWN |
						     WOL_DRV_WOL |
						     WOL_SET_MAGIC_PKT);

	if (device_should_wake) {
		u32 mac_mode;

		if (!(tp->phy_flags & TG3_PHYFLG_PHY_SERDES)) {
			if (do_low_power &&
			    !(tp->phy_flags & TG3_PHYFLG_IS_FET)) {
				tg3_phy_auxctl_write(tp,
					       MII_TG3_AUXCTL_SHDWSEL_PWRCTL,
					       MII_TG3_AUXCTL_PCTL_WOL_EN |
					       MII_TG3_AUXCTL_PCTL_100TX_LPWR |
					       MII_TG3_AUXCTL_PCTL_CL_AB_TXDAC);
				udelay(40);
			}

			if (tp->phy_flags & TG3_PHYFLG_MII_SERDES)
				mac_mode = MAC_MODE_PORT_MODE_GMII;
			else if (tp->phy_flags &
				 TG3_PHYFLG_KEEP_LINK_ON_PWRDN) {
				if (tp->link_config.active_speed == SPEED_1000)
					mac_mode = MAC_MODE_PORT_MODE_GMII;
				else
					mac_mode = MAC_MODE_PORT_MODE_MII;
			} else
				mac_mode = MAC_MODE_PORT_MODE_MII;

			mac_mode |= tp->mac_mode & MAC_MODE_LINK_POLARITY;
			if (tg3_asic_rev(tp) == ASIC_REV_5700) {
				u32 speed = tg3_flag(tp, WOL_SPEED_100MB) ?
					     SPEED_100 : SPEED_10;
				if (tg3_5700_link_polarity(tp, speed))
					mac_mode |= MAC_MODE_LINK_POLARITY;
				else
					mac_mode &= ~MAC_MODE_LINK_POLARITY;
			}
		} else {
			mac_mode = MAC_MODE_PORT_MODE_TBI;
		}

		if (!tg3_flag(tp, 5750_PLUS))
			tw32(MAC_LED_CTRL, tp->led_ctrl);

		mac_mode |= MAC_MODE_MAGIC_PKT_ENABLE;
		if ((tg3_flag(tp, 5705_PLUS) && !tg3_flag(tp, 5780_CLASS)) &&
		    (tg3_flag(tp, ENABLE_ASF) || tg3_flag(tp, ENABLE_APE)))
			mac_mode |= MAC_MODE_KEEP_FRAME_IN_WOL;

		if (tg3_flag(tp, ENABLE_APE))
			mac_mode |= MAC_MODE_APE_TX_EN |
				    MAC_MODE_APE_RX_EN |
				    MAC_MODE_TDE_ENABLE;

		tw32_f(MAC_MODE, mac_mode);
		udelay(100);

		tw32_f(MAC_RX_MODE, RX_MODE_ENABLE);
		udelay(10);
	}

	if (!tg3_flag(tp, WOL_SPEED_100MB) &&
	    (tg3_asic_rev(tp) == ASIC_REV_5700 ||
	     tg3_asic_rev(tp) == ASIC_REV_5701)) {
		u32 base_val;

		base_val = tp->pci_clock_ctrl;
		base_val |= (CLOCK_CTRL_RXCLK_DISABLE |
			     CLOCK_CTRL_TXCLK_DISABLE);

		tw32_wait_f(TG3PCI_CLOCK_CTRL, base_val | CLOCK_CTRL_ALTCLK |
			    CLOCK_CTRL_PWRDOWN_PLL133, 40);
	} else if (tg3_flag(tp, 5780_CLASS) ||
		   tg3_flag(tp, CPMU_PRESENT) ||
		   tg3_asic_rev(tp) == ASIC_REV_5906) {
		/* do nothing */
	} else if (!(tg3_flag(tp, 5750_PLUS) && tg3_flag(tp, ENABLE_ASF))) {
		u32 newbits1, newbits2;

		if (tg3_asic_rev(tp) == ASIC_REV_5700 ||
		    tg3_asic_rev(tp) == ASIC_REV_5701) {
			newbits1 = (CLOCK_CTRL_RXCLK_DISABLE |
				    CLOCK_CTRL_TXCLK_DISABLE |
				    CLOCK_CTRL_ALTCLK);
			newbits2 = newbits1 | CLOCK_CTRL_44MHZ_CORE;
		} else if (tg3_flag(tp, 5705_PLUS)) {
			newbits1 = CLOCK_CTRL_625_CORE;
			newbits2 = newbits1 | CLOCK_CTRL_ALTCLK;
		} else {
			newbits1 = CLOCK_CTRL_ALTCLK;
			newbits2 = newbits1 | CLOCK_CTRL_44MHZ_CORE;
		}

		tw32_wait_f(TG3PCI_CLOCK_CTRL, tp->pci_clock_ctrl | newbits1,
			    40);

		tw32_wait_f(TG3PCI_CLOCK_CTRL, tp->pci_clock_ctrl | newbits2,
			    40);

		if (!tg3_flag(tp, 5705_PLUS)) {
			u32 newbits3;

			if (tg3_asic_rev(tp) == ASIC_REV_5700 ||
			    tg3_asic_rev(tp) == ASIC_REV_5701) {
				newbits3 = (CLOCK_CTRL_RXCLK_DISABLE |
					    CLOCK_CTRL_TXCLK_DISABLE |
					    CLOCK_CTRL_44MHZ_CORE);
			} else {
				newbits3 = CLOCK_CTRL_44MHZ_CORE;
			}

			tw32_wait_f(TG3PCI_CLOCK_CTRL,
				    tp->pci_clock_ctrl | newbits3, 40);
		}
	}

	if (!(device_should_wake) && !tg3_flag(tp, ENABLE_ASF))
		tg3_power_down_phy(tp, do_low_power);

	tg3_frob_aux_power(tp, true);

	/* Workaround for unstable PLL clock */
	if ((!tg3_flag(tp, IS_SSB_CORE)) &&
	    ((tg3_chip_rev(tp) == CHIPREV_5750_AX) ||
	     (tg3_chip_rev(tp) == CHIPREV_5750_BX))) {
		u32 val = tr32(0x7d00);

		val &= ~((1 << 16) | (1 << 4) | (1 << 2) | (1 << 1) | 1);
		tw32(0x7d00, val);
		if (!tg3_flag(tp, ENABLE_ASF)) {
			int err;

			err = tg3_nvram_lock(tp);
			tg3_halt_cpu(tp, RX_CPU_BASE);
			if (!err)
				tg3_nvram_unlock(tp);
		}
	}

	tg3_write_sig_post_reset(tp, RESET_KIND_SHUTDOWN);

	return 0;
}

static void tg3_power_down(struct tg3 *tp)
{
	tg3_power_down_prepare(tp);

	pci_wake_from_d3(tp->pdev, tg3_flag(tp, WOL_ENABLE));
	pci_set_power_state(tp->pdev, PCI_D3hot);
}

static void tg3_aux_stat_to_speed_duplex(struct tg3 *tp, u32 val, u16 *speed, u8 *duplex)
{
	switch (val & MII_TG3_AUX_STAT_SPDMASK) {
	case MII_TG3_AUX_STAT_10HALF:
		*speed = SPEED_10;
		*duplex = DUPLEX_HALF;
		break;

	case MII_TG3_AUX_STAT_10FULL:
		*speed = SPEED_10;
		*duplex = DUPLEX_FULL;
		break;

	case MII_TG3_AUX_STAT_100HALF:
		*speed = SPEED_100;
		*duplex = DUPLEX_HALF;
		break;

	case MII_TG3_AUX_STAT_100FULL:
		*speed = SPEED_100;
		*duplex = DUPLEX_FULL;
		break;

	case MII_TG3_AUX_STAT_1000HALF:
		*speed = SPEED_1000;
		*duplex = DUPLEX_HALF;
		break;

	case MII_TG3_AUX_STAT_1000FULL:
		*speed = SPEED_1000;
		*duplex = DUPLEX_FULL;
		break;

	default:
		if (tp->phy_flags & TG3_PHYFLG_IS_FET) {
			*speed = (val & MII_TG3_AUX_STAT_100) ? SPEED_100 :
				 SPEED_10;
			*duplex = (val & MII_TG3_AUX_STAT_FULL) ? DUPLEX_FULL :
				  DUPLEX_HALF;
			break;
		}
		*speed = SPEED_UNKNOWN;
		*duplex = DUPLEX_UNKNOWN;
		break;
	}
}

static int tg3_phy_autoneg_cfg(struct tg3 *tp, u32 advertise, u32 flowctrl)
{
	int err = 0;
	u32 val, new_adv;

	new_adv = ADVERTISE_CSMA;
	new_adv |= ethtool_adv_to_mii_adv_t(advertise) & ADVERTISE_ALL;
	new_adv |= mii_advertise_flowctrl(flowctrl);

	err = tg3_writephy(tp, MII_ADVERTISE, new_adv);
	if (err)
		goto done;

	if (!(tp->phy_flags & TG3_PHYFLG_10_100_ONLY)) {
		new_adv = ethtool_adv_to_mii_ctrl1000_t(advertise);

		if (tg3_chip_rev_id(tp) == CHIPREV_ID_5701_A0 ||
		    tg3_chip_rev_id(tp) == CHIPREV_ID_5701_B0)
			new_adv |= CTL1000_AS_MASTER | CTL1000_ENABLE_MASTER;

		err = tg3_writephy(tp, MII_CTRL1000, new_adv);
		if (err)
			goto done;
	}

	if (!(tp->phy_flags & TG3_PHYFLG_EEE_CAP))
		goto done;

	tw32(TG3_CPMU_EEE_MODE,
	     tr32(TG3_CPMU_EEE_MODE) & ~TG3_CPMU_EEEMD_LPI_ENABLE);

	err = tg3_phy_toggle_auxctl_smdsp(tp, true);
	if (!err) {
		u32 err2;

		val = 0;
		/* Advertise 100-BaseTX EEE ability */
		if (advertise & ADVERTISED_100baseT_Full)
			val |= MDIO_AN_EEE_ADV_100TX;
		/* Advertise 1000-BaseT EEE ability */
		if (advertise & ADVERTISED_1000baseT_Full)
			val |= MDIO_AN_EEE_ADV_1000T;
		err = tg3_phy_cl45_write(tp, MDIO_MMD_AN, MDIO_AN_EEE_ADV, val);
		if (err)
			val = 0;

		switch (tg3_asic_rev(tp)) {
		case ASIC_REV_5717:
		case ASIC_REV_57765:
		case ASIC_REV_57766:
		case ASIC_REV_5719:
			/* If we advertised any eee advertisements above... */
			if (val)
				val = MII_TG3_DSP_TAP26_ALNOKO |
				      MII_TG3_DSP_TAP26_RMRXSTO |
				      MII_TG3_DSP_TAP26_OPCSINPT;
			tg3_phydsp_write(tp, MII_TG3_DSP_TAP26, val);
			/* Fall through */
		case ASIC_REV_5720:
		case ASIC_REV_5762:
			if (!tg3_phydsp_read(tp, MII_TG3_DSP_CH34TP2, &val))
				tg3_phydsp_write(tp, MII_TG3_DSP_CH34TP2, val |
						 MII_TG3_DSP_CH34TP2_HIBW01);
		}

		err2 = tg3_phy_toggle_auxctl_smdsp(tp, false);
		if (!err)
			err = err2;
	}

done:
	return err;
}

static void tg3_phy_copper_begin(struct tg3 *tp)
{
	if (tp->link_config.autoneg == AUTONEG_ENABLE ||
	    (tp->phy_flags & TG3_PHYFLG_IS_LOW_POWER)) {
		u32 adv, fc;

		if ((tp->phy_flags & TG3_PHYFLG_IS_LOW_POWER) &&
		    !(tp->phy_flags & TG3_PHYFLG_KEEP_LINK_ON_PWRDN)) {
			adv = ADVERTISED_10baseT_Half |
			      ADVERTISED_10baseT_Full;
			if (tg3_flag(tp, WOL_SPEED_100MB))
				adv |= ADVERTISED_100baseT_Half |
				       ADVERTISED_100baseT_Full;
			if (tp->phy_flags & TG3_PHYFLG_1G_ON_VAUX_OK)
				adv |= ADVERTISED_1000baseT_Half |
				       ADVERTISED_1000baseT_Full;

			fc = FLOW_CTRL_TX | FLOW_CTRL_RX;
		} else {
			adv = tp->link_config.advertising;
			if (tp->phy_flags & TG3_PHYFLG_10_100_ONLY)
				adv &= ~(ADVERTISED_1000baseT_Half |
					 ADVERTISED_1000baseT_Full);

			fc = tp->link_config.flowctrl;
		}

		tg3_phy_autoneg_cfg(tp, adv, fc);

		if ((tp->phy_flags & TG3_PHYFLG_IS_LOW_POWER) &&
		    (tp->phy_flags & TG3_PHYFLG_KEEP_LINK_ON_PWRDN)) {
			/* Normally during power down we want to autonegotiate
			 * the lowest possible speed for WOL. However, to avoid
			 * link flap, we leave it untouched.
			 */
			return;
		}

		tg3_writephy(tp, MII_BMCR,
			     BMCR_ANENABLE | BMCR_ANRESTART);
	} else {
		int i;
		u32 bmcr, orig_bmcr;

		tp->link_config.active_speed = tp->link_config.speed;
		tp->link_config.active_duplex = tp->link_config.duplex;

		if (tg3_asic_rev(tp) == ASIC_REV_5714) {
			/* With autoneg disabled, 5715 only links up when the
			 * advertisement register has the configured speed
			 * enabled.
			 */
			tg3_writephy(tp, MII_ADVERTISE, ADVERTISE_ALL);
		}

		bmcr = 0;
		switch (tp->link_config.speed) {
		default:
		case SPEED_10:
			break;

		case SPEED_100:
			bmcr |= BMCR_SPEED100;
			break;

		case SPEED_1000:
			bmcr |= BMCR_SPEED1000;
			break;
		}

		if (tp->link_config.duplex == DUPLEX_FULL)
			bmcr |= BMCR_FULLDPLX;

		if (!tg3_readphy(tp, MII_BMCR, &orig_bmcr) &&
		    (bmcr != orig_bmcr)) {
			tg3_writephy(tp, MII_BMCR, BMCR_LOOPBACK);
			for (i = 0; i < 1500; i++) {
				u32 tmp;

				udelay(10);
				if (tg3_readphy(tp, MII_BMSR, &tmp) ||
				    tg3_readphy(tp, MII_BMSR, &tmp))
					continue;
				if (!(tmp & BMSR_LSTATUS)) {
					udelay(40);
					break;
				}
			}
			tg3_writephy(tp, MII_BMCR, bmcr);
			udelay(40);
		}
	}
}

static int tg3_phy_pull_config(struct tg3 *tp)
{
	int err;
	u32 val;

	err = tg3_readphy(tp, MII_BMCR, &val);
	if (err)
		goto done;

	if (!(val & BMCR_ANENABLE)) {
		tp->link_config.autoneg = AUTONEG_DISABLE;
		tp->link_config.advertising = 0;
		tg3_flag_clear(tp, PAUSE_AUTONEG);

		err = -EIO;

		switch (val & (BMCR_SPEED1000 | BMCR_SPEED100)) {
		case 0:
			if (tp->phy_flags & TG3_PHYFLG_ANY_SERDES)
				goto done;

			tp->link_config.speed = SPEED_10;
			break;
		case BMCR_SPEED100:
			if (tp->phy_flags & TG3_PHYFLG_ANY_SERDES)
				goto done;

			tp->link_config.speed = SPEED_100;
			break;
		case BMCR_SPEED1000:
			if (!(tp->phy_flags & TG3_PHYFLG_10_100_ONLY)) {
				tp->link_config.speed = SPEED_1000;
				break;
			}
			/* Fall through */
		default:
			goto done;
		}

		if (val & BMCR_FULLDPLX)
			tp->link_config.duplex = DUPLEX_FULL;
		else
			tp->link_config.duplex = DUPLEX_HALF;

		tp->link_config.flowctrl = FLOW_CTRL_RX | FLOW_CTRL_TX;

		err = 0;
		goto done;
	}

	tp->link_config.autoneg = AUTONEG_ENABLE;
	tp->link_config.advertising = ADVERTISED_Autoneg;
	tg3_flag_set(tp, PAUSE_AUTONEG);

	if (!(tp->phy_flags & TG3_PHYFLG_ANY_SERDES)) {
		u32 adv;

		err = tg3_readphy(tp, MII_ADVERTISE, &val);
		if (err)
			goto done;

		adv = mii_adv_to_ethtool_adv_t(val & ADVERTISE_ALL);
		tp->link_config.advertising |= adv | ADVERTISED_TP;

		tp->link_config.flowctrl = tg3_decode_flowctrl_1000T(val);
	} else {
		tp->link_config.advertising |= ADVERTISED_FIBRE;
	}

	if (!(tp->phy_flags & TG3_PHYFLG_10_100_ONLY)) {
		u32 adv;

		if (!(tp->phy_flags & TG3_PHYFLG_ANY_SERDES)) {
			err = tg3_readphy(tp, MII_CTRL1000, &val);
			if (err)
				goto done;

			adv = mii_ctrl1000_to_ethtool_adv_t(val);
		} else {
			err = tg3_readphy(tp, MII_ADVERTISE, &val);
			if (err)
				goto done;

			adv = tg3_decode_flowctrl_1000X(val);
			tp->link_config.flowctrl = adv;

			val &= (ADVERTISE_1000XHALF | ADVERTISE_1000XFULL);
			adv = mii_adv_to_ethtool_adv_x(val);
		}

		tp->link_config.advertising |= adv;
	}

done:
	return err;
}

static int tg3_init_5401phy_dsp(struct tg3 *tp)
{
	int err;

	/* Turn off tap power management. */
	/* Set Extended packet length bit */
	err = tg3_phy_auxctl_write(tp, MII_TG3_AUXCTL_SHDWSEL_AUXCTL, 0x4c20);

	err |= tg3_phydsp_write(tp, 0x0012, 0x1804);
	err |= tg3_phydsp_write(tp, 0x0013, 0x1204);
	err |= tg3_phydsp_write(tp, 0x8006, 0x0132);
	err |= tg3_phydsp_write(tp, 0x8006, 0x0232);
	err |= tg3_phydsp_write(tp, 0x201f, 0x0a20);

	udelay(40);

	return err;
}

static bool tg3_phy_eee_config_ok(struct tg3 *tp)
{
	u32 val;
	u32 tgtadv = 0;
	u32 advertising = tp->link_config.advertising;

	if (!(tp->phy_flags & TG3_PHYFLG_EEE_CAP))
		return true;

	if (tg3_phy_cl45_read(tp, MDIO_MMD_AN, MDIO_AN_EEE_ADV, &val))
		return false;

	val &= (MDIO_AN_EEE_ADV_100TX | MDIO_AN_EEE_ADV_1000T);


	if (advertising & ADVERTISED_100baseT_Full)
		tgtadv |= MDIO_AN_EEE_ADV_100TX;
	if (advertising & ADVERTISED_1000baseT_Full)
		tgtadv |= MDIO_AN_EEE_ADV_1000T;

	if (val != tgtadv)
		return false;

	return true;
}

static bool tg3_phy_copper_an_config_ok(struct tg3 *tp, u32 *lcladv)
{
	u32 advmsk, tgtadv, advertising;

	advertising = tp->link_config.advertising;
	tgtadv = ethtool_adv_to_mii_adv_t(advertising) & ADVERTISE_ALL;

	advmsk = ADVERTISE_ALL;
	if (tp->link_config.active_duplex == DUPLEX_FULL) {
		tgtadv |= mii_advertise_flowctrl(tp->link_config.flowctrl);
		advmsk |= ADVERTISE_PAUSE_CAP | ADVERTISE_PAUSE_ASYM;
	}

	if (tg3_readphy(tp, MII_ADVERTISE, lcladv))
		return false;

	if ((*lcladv & advmsk) != tgtadv)
		return false;

	if (!(tp->phy_flags & TG3_PHYFLG_10_100_ONLY)) {
		u32 tg3_ctrl;

		tgtadv = ethtool_adv_to_mii_ctrl1000_t(advertising);

		if (tg3_readphy(tp, MII_CTRL1000, &tg3_ctrl))
			return false;

		if (tgtadv &&
		    (tg3_chip_rev_id(tp) == CHIPREV_ID_5701_A0 ||
		     tg3_chip_rev_id(tp) == CHIPREV_ID_5701_B0)) {
			tgtadv |= CTL1000_AS_MASTER | CTL1000_ENABLE_MASTER;
			tg3_ctrl &= (ADVERTISE_1000HALF | ADVERTISE_1000FULL |
				     CTL1000_AS_MASTER | CTL1000_ENABLE_MASTER);
		} else {
			tg3_ctrl &= (ADVERTISE_1000HALF | ADVERTISE_1000FULL);
		}

		if (tg3_ctrl != tgtadv)
			return false;
	}

	return true;
}

static bool tg3_phy_copper_fetch_rmtadv(struct tg3 *tp, u32 *rmtadv)
{
	u32 lpeth = 0;

	if (!(tp->phy_flags & TG3_PHYFLG_10_100_ONLY)) {
		u32 val;

		if (tg3_readphy(tp, MII_STAT1000, &val))
			return false;

		lpeth = mii_stat1000_to_ethtool_lpa_t(val);
	}

	if (tg3_readphy(tp, MII_LPA, rmtadv))
		return false;

	lpeth |= mii_lpa_to_ethtool_lpa_t(*rmtadv);
	tp->link_config.rmt_adv = lpeth;

	return true;
}

static bool tg3_test_and_report_link_chg(struct tg3 *tp, bool curr_link_up)
{
	if (curr_link_up != tp->link_up) {
		if (curr_link_up) {
			netif_carrier_on(tp->dev);
		} else {
			netif_carrier_off(tp->dev);
			if (tp->phy_flags & TG3_PHYFLG_MII_SERDES)
				tp->phy_flags &= ~TG3_PHYFLG_PARALLEL_DETECT;
		}

		tg3_link_report(tp);
		return true;
	}

	return false;
}

static void tg3_clear_mac_status(struct tg3 *tp)
{
	tw32(MAC_EVENT, 0);

	tw32_f(MAC_STATUS,
	       MAC_STATUS_SYNC_CHANGED |
	       MAC_STATUS_CFG_CHANGED |
	       MAC_STATUS_MI_COMPLETION |
	       MAC_STATUS_LNKSTATE_CHANGED);
	udelay(40);
}

static int tg3_setup_copper_phy(struct tg3 *tp, bool force_reset)
{
	bool current_link_up;
	u32 bmsr, val;
	u32 lcl_adv, rmt_adv;
	u16 current_speed;
	u8 current_duplex;
	int i, err;

	tg3_clear_mac_status(tp);

	if ((tp->mi_mode & MAC_MI_MODE_AUTO_POLL) != 0) {
		tw32_f(MAC_MI_MODE,
		     (tp->mi_mode & ~MAC_MI_MODE_AUTO_POLL));
		udelay(80);
	}

	tg3_phy_auxctl_write(tp, MII_TG3_AUXCTL_SHDWSEL_PWRCTL, 0);

	/* Some third-party PHYs need to be reset on link going
	 * down.
	 */
	if ((tg3_asic_rev(tp) == ASIC_REV_5703 ||
	     tg3_asic_rev(tp) == ASIC_REV_5704 ||
	     tg3_asic_rev(tp) == ASIC_REV_5705) &&
	    tp->link_up) {
		tg3_readphy(tp, MII_BMSR, &bmsr);
		if (!tg3_readphy(tp, MII_BMSR, &bmsr) &&
		    !(bmsr & BMSR_LSTATUS))
			force_reset = true;
	}
	if (force_reset)
		tg3_phy_reset(tp);

	if ((tp->phy_id & TG3_PHY_ID_MASK) == TG3_PHY_ID_BCM5401) {
		tg3_readphy(tp, MII_BMSR, &bmsr);
		if (tg3_readphy(tp, MII_BMSR, &bmsr) ||
		    !tg3_flag(tp, INIT_COMPLETE))
			bmsr = 0;

		if (!(bmsr & BMSR_LSTATUS)) {
			err = tg3_init_5401phy_dsp(tp);
			if (err)
				return err;

			tg3_readphy(tp, MII_BMSR, &bmsr);
			for (i = 0; i < 1000; i++) {
				udelay(10);
				if (!tg3_readphy(tp, MII_BMSR, &bmsr) &&
				    (bmsr & BMSR_LSTATUS)) {
					udelay(40);
					break;
				}
			}

			if ((tp->phy_id & TG3_PHY_ID_REV_MASK) ==
			    TG3_PHY_REV_BCM5401_B0 &&
			    !(bmsr & BMSR_LSTATUS) &&
			    tp->link_config.active_speed == SPEED_1000) {
				err = tg3_phy_reset(tp);
				if (!err)
					err = tg3_init_5401phy_dsp(tp);
				if (err)
					return err;
			}
		}
	} else if (tg3_chip_rev_id(tp) == CHIPREV_ID_5701_A0 ||
		   tg3_chip_rev_id(tp) == CHIPREV_ID_5701_B0) {
		/* 5701 {A0,B0} CRC bug workaround */
		tg3_writephy(tp, 0x15, 0x0a75);
		tg3_writephy(tp, MII_TG3_MISC_SHDW, 0x8c68);
		tg3_writephy(tp, MII_TG3_MISC_SHDW, 0x8d68);
		tg3_writephy(tp, MII_TG3_MISC_SHDW, 0x8c68);
	}

	/* Clear pending interrupts... */
	tg3_readphy(tp, MII_TG3_ISTAT, &val);
	tg3_readphy(tp, MII_TG3_ISTAT, &val);

	if (tp->phy_flags & TG3_PHYFLG_USE_MI_INTERRUPT)
		tg3_writephy(tp, MII_TG3_IMASK, ~MII_TG3_INT_LINKCHG);
	else if (!(tp->phy_flags & TG3_PHYFLG_IS_FET))
		tg3_writephy(tp, MII_TG3_IMASK, ~0);

	if (tg3_asic_rev(tp) == ASIC_REV_5700 ||
	    tg3_asic_rev(tp) == ASIC_REV_5701) {
		if (tp->led_ctrl == LED_CTRL_MODE_PHY_1)
			tg3_writephy(tp, MII_TG3_EXT_CTRL,
				     MII_TG3_EXT_CTRL_LNK3_LED_MODE);
		else
			tg3_writephy(tp, MII_TG3_EXT_CTRL, 0);
	}

	current_link_up = false;
	current_speed = SPEED_UNKNOWN;
	current_duplex = DUPLEX_UNKNOWN;
	tp->phy_flags &= ~TG3_PHYFLG_MDIX_STATE;
	tp->link_config.rmt_adv = 0;

	if (tp->phy_flags & TG3_PHYFLG_CAPACITIVE_COUPLING) {
		err = tg3_phy_auxctl_read(tp,
					  MII_TG3_AUXCTL_SHDWSEL_MISCTEST,
					  &val);
		if (!err && !(val & (1 << 10))) {
			tg3_phy_auxctl_write(tp,
					     MII_TG3_AUXCTL_SHDWSEL_MISCTEST,
					     val | (1 << 10));
			goto relink;
		}
	}

	bmsr = 0;
	for (i = 0; i < 100; i++) {
		tg3_readphy(tp, MII_BMSR, &bmsr);
		if (!tg3_readphy(tp, MII_BMSR, &bmsr) &&
		    (bmsr & BMSR_LSTATUS))
			break;
		udelay(40);
	}

	if (bmsr & BMSR_LSTATUS) {
		u32 aux_stat, bmcr;

		tg3_readphy(tp, MII_TG3_AUX_STAT, &aux_stat);
		for (i = 0; i < 2000; i++) {
			udelay(10);
			if (!tg3_readphy(tp, MII_TG3_AUX_STAT, &aux_stat) &&
			    aux_stat)
				break;
		}

		tg3_aux_stat_to_speed_duplex(tp, aux_stat,
					     &current_speed,
					     &current_duplex);

		bmcr = 0;
		for (i = 0; i < 200; i++) {
			tg3_readphy(tp, MII_BMCR, &bmcr);
			if (tg3_readphy(tp, MII_BMCR, &bmcr))
				continue;
			if (bmcr && bmcr != 0x7fff)
				break;
			udelay(10);
		}

		lcl_adv = 0;
		rmt_adv = 0;

		tp->link_config.active_speed = current_speed;
		tp->link_config.active_duplex = current_duplex;

		if (tp->link_config.autoneg == AUTONEG_ENABLE) {
			bool eee_config_ok = tg3_phy_eee_config_ok(tp);

			if ((bmcr & BMCR_ANENABLE) &&
			    eee_config_ok &&
			    tg3_phy_copper_an_config_ok(tp, &lcl_adv) &&
			    tg3_phy_copper_fetch_rmtadv(tp, &rmt_adv))
				current_link_up = true;

			/* EEE settings changes take effect only after a phy
			 * reset.  If we have skipped a reset due to Link Flap
			 * Avoidance being enabled, do it now.
			 */
			if (!eee_config_ok &&
			    (tp->phy_flags & TG3_PHYFLG_KEEP_LINK_ON_PWRDN) &&
			    !force_reset)
				tg3_phy_reset(tp);
		} else {
			if (!(bmcr & BMCR_ANENABLE) &&
			    tp->link_config.speed == current_speed &&
			    tp->link_config.duplex == current_duplex) {
				current_link_up = true;
			}
		}

		if (current_link_up &&
		    tp->link_config.active_duplex == DUPLEX_FULL) {
			u32 reg, bit;

			if (tp->phy_flags & TG3_PHYFLG_IS_FET) {
				reg = MII_TG3_FET_GEN_STAT;
				bit = MII_TG3_FET_GEN_STAT_MDIXSTAT;
			} else {
				reg = MII_TG3_EXT_STAT;
				bit = MII_TG3_EXT_STAT_MDIX;
			}

			if (!tg3_readphy(tp, reg, &val) && (val & bit))
				tp->phy_flags |= TG3_PHYFLG_MDIX_STATE;

			tg3_setup_flow_control(tp, lcl_adv, rmt_adv);
		}
	}

relink:
	if (!current_link_up || (tp->phy_flags & TG3_PHYFLG_IS_LOW_POWER)) {
		tg3_phy_copper_begin(tp);

		if (tg3_flag(tp, ROBOSWITCH)) {
			current_link_up = true;
			/* FIXME: when BCM5325 switch is used use 100 MBit/s */
			current_speed = SPEED_1000;
			current_duplex = DUPLEX_FULL;
			tp->link_config.active_speed = current_speed;
			tp->link_config.active_duplex = current_duplex;
		}

		tg3_readphy(tp, MII_BMSR, &bmsr);
		if ((!tg3_readphy(tp, MII_BMSR, &bmsr) && (bmsr & BMSR_LSTATUS)) ||
		    (tp->mac_mode & MAC_MODE_PORT_INT_LPBACK))
			current_link_up = true;
	}

	tp->mac_mode &= ~MAC_MODE_PORT_MODE_MASK;
	if (current_link_up) {
		if (tp->link_config.active_speed == SPEED_100 ||
		    tp->link_config.active_speed == SPEED_10)
			tp->mac_mode |= MAC_MODE_PORT_MODE_MII;
		else
			tp->mac_mode |= MAC_MODE_PORT_MODE_GMII;
	} else if (tp->phy_flags & TG3_PHYFLG_IS_FET)
		tp->mac_mode |= MAC_MODE_PORT_MODE_MII;
	else
		tp->mac_mode |= MAC_MODE_PORT_MODE_GMII;

	/* In order for the 5750 core in BCM4785 chip to work properly
	 * in RGMII mode, the Led Control Register must be set up.
	 */
	if (tg3_flag(tp, RGMII_MODE)) {
		u32 led_ctrl = tr32(MAC_LED_CTRL);
		led_ctrl &= ~(LED_CTRL_1000MBPS_ON | LED_CTRL_100MBPS_ON);

		if (tp->link_config.active_speed == SPEED_10)
			led_ctrl |= LED_CTRL_LNKLED_OVERRIDE;
		else if (tp->link_config.active_speed == SPEED_100)
			led_ctrl |= (LED_CTRL_LNKLED_OVERRIDE |
				     LED_CTRL_100MBPS_ON);
		else if (tp->link_config.active_speed == SPEED_1000)
			led_ctrl |= (LED_CTRL_LNKLED_OVERRIDE |
				     LED_CTRL_1000MBPS_ON);

		tw32(MAC_LED_CTRL, led_ctrl);
		udelay(40);
	}

	tp->mac_mode &= ~MAC_MODE_HALF_DUPLEX;
	if (tp->link_config.active_duplex == DUPLEX_HALF)
		tp->mac_mode |= MAC_MODE_HALF_DUPLEX;

	if (tg3_asic_rev(tp) == ASIC_REV_5700) {
		if (current_link_up &&
		    tg3_5700_link_polarity(tp, tp->link_config.active_speed))
			tp->mac_mode |= MAC_MODE_LINK_POLARITY;
		else
			tp->mac_mode &= ~MAC_MODE_LINK_POLARITY;
	}

	/* ??? Without this setting Netgear GA302T PHY does not
	 * ??? send/receive packets...
	 */
	if ((tp->phy_id & TG3_PHY_ID_MASK) == TG3_PHY_ID_BCM5411 &&
	    tg3_chip_rev_id(tp) == CHIPREV_ID_5700_ALTIMA) {
		tp->mi_mode |= MAC_MI_MODE_AUTO_POLL;
		tw32_f(MAC_MI_MODE, tp->mi_mode);
		udelay(80);
	}

	tw32_f(MAC_MODE, tp->mac_mode);
	udelay(40);

	tg3_phy_eee_adjust(tp, current_link_up);

	if (tg3_flag(tp, USE_LINKCHG_REG)) {
		/* Polled via timer. */
		tw32_f(MAC_EVENT, 0);
	} else {
		tw32_f(MAC_EVENT, MAC_EVENT_LNKSTATE_CHANGED);
	}
	udelay(40);

	if (tg3_asic_rev(tp) == ASIC_REV_5700 &&
	    current_link_up &&
	    tp->link_config.active_speed == SPEED_1000 &&
	    (tg3_flag(tp, PCIX_MODE) || tg3_flag(tp, PCI_HIGH_SPEED))) {
		udelay(120);
		tw32_f(MAC_STATUS,
		     (MAC_STATUS_SYNC_CHANGED |
		      MAC_STATUS_CFG_CHANGED));
		udelay(40);
		tg3_write_mem(tp,
			      NIC_SRAM_FIRMWARE_MBOX,
			      NIC_SRAM_FIRMWARE_MBOX_MAGIC2);
	}

	/* Prevent send BD corruption. */
	if (tg3_flag(tp, CLKREQ_BUG)) {
		if (tp->link_config.active_speed == SPEED_100 ||
		    tp->link_config.active_speed == SPEED_10)
			pcie_capability_clear_word(tp->pdev, PCI_EXP_LNKCTL,
						   PCI_EXP_LNKCTL_CLKREQ_EN);
		else
			pcie_capability_set_word(tp->pdev, PCI_EXP_LNKCTL,
						 PCI_EXP_LNKCTL_CLKREQ_EN);
	}

	tg3_test_and_report_link_chg(tp, current_link_up);

	return 0;
}

struct tg3_fiber_aneginfo {
	int state;
#define ANEG_STATE_UNKNOWN		0
#define ANEG_STATE_AN_ENABLE		1
#define ANEG_STATE_RESTART_INIT		2
#define ANEG_STATE_RESTART		3
#define ANEG_STATE_DISABLE_LINK_OK	4
#define ANEG_STATE_ABILITY_DETECT_INIT	5
#define ANEG_STATE_ABILITY_DETECT	6
#define ANEG_STATE_ACK_DETECT_INIT	7
#define ANEG_STATE_ACK_DETECT		8
#define ANEG_STATE_COMPLETE_ACK_INIT	9
#define ANEG_STATE_COMPLETE_ACK		10
#define ANEG_STATE_IDLE_DETECT_INIT	11
#define ANEG_STATE_IDLE_DETECT		12
#define ANEG_STATE_LINK_OK		13
#define ANEG_STATE_NEXT_PAGE_WAIT_INIT	14
#define ANEG_STATE_NEXT_PAGE_WAIT	15

	u32 flags;
#define MR_AN_ENABLE		0x00000001
#define MR_RESTART_AN		0x00000002
#define MR_AN_COMPLETE		0x00000004
#define MR_PAGE_RX		0x00000008
#define MR_NP_LOADED		0x00000010
#define MR_TOGGLE_TX		0x00000020
#define MR_LP_ADV_FULL_DUPLEX	0x00000040
#define MR_LP_ADV_HALF_DUPLEX	0x00000080
#define MR_LP_ADV_SYM_PAUSE	0x00000100
#define MR_LP_ADV_ASYM_PAUSE	0x00000200
#define MR_LP_ADV_REMOTE_FAULT1	0x00000400
#define MR_LP_ADV_REMOTE_FAULT2	0x00000800
#define MR_LP_ADV_NEXT_PAGE	0x00001000
#define MR_TOGGLE_RX		0x00002000
#define MR_NP_RX		0x00004000

#define MR_LINK_OK		0x80000000

	unsigned long link_time, cur_time;

	u32 ability_match_cfg;
	int ability_match_count;

	char ability_match, idle_match, ack_match;

	u32 txconfig, rxconfig;
#define ANEG_CFG_NP		0x00000080
#define ANEG_CFG_ACK		0x00000040
#define ANEG_CFG_RF2		0x00000020
#define ANEG_CFG_RF1		0x00000010
#define ANEG_CFG_PS2		0x00000001
#define ANEG_CFG_PS1		0x00008000
#define ANEG_CFG_HD		0x00004000
#define ANEG_CFG_FD		0x00002000
#define ANEG_CFG_INVAL		0x00001f06

};
#define ANEG_OK		0
#define ANEG_DONE	1
#define ANEG_TIMER_ENAB	2
#define ANEG_FAILED	-1

#define ANEG_STATE_SETTLE_TIME	10000

static int tg3_fiber_aneg_smachine(struct tg3 *tp,
				   struct tg3_fiber_aneginfo *ap)
{
	u16 flowctrl;
	unsigned long delta;
	u32 rx_cfg_reg;
	int ret;

	if (ap->state == ANEG_STATE_UNKNOWN) {
		ap->rxconfig = 0;
		ap->link_time = 0;
		ap->cur_time = 0;
		ap->ability_match_cfg = 0;
		ap->ability_match_count = 0;
		ap->ability_match = 0;
		ap->idle_match = 0;
		ap->ack_match = 0;
	}
	ap->cur_time++;

	if (tr32(MAC_STATUS) & MAC_STATUS_RCVD_CFG) {
		rx_cfg_reg = tr32(MAC_RX_AUTO_NEG);

		if (rx_cfg_reg != ap->ability_match_cfg) {
			ap->ability_match_cfg = rx_cfg_reg;
			ap->ability_match = 0;
			ap->ability_match_count = 0;
		} else {
			if (++ap->ability_match_count > 1) {
				ap->ability_match = 1;
				ap->ability_match_cfg = rx_cfg_reg;
			}
		}
		if (rx_cfg_reg & ANEG_CFG_ACK)
			ap->ack_match = 1;
		else
			ap->ack_match = 0;

		ap->idle_match = 0;
	} else {
		ap->idle_match = 1;
		ap->ability_match_cfg = 0;
		ap->ability_match_count = 0;
		ap->ability_match = 0;
		ap->ack_match = 0;

		rx_cfg_reg = 0;
	}

	ap->rxconfig = rx_cfg_reg;
	ret = ANEG_OK;

	switch (ap->state) {
	case ANEG_STATE_UNKNOWN:
		if (ap->flags & (MR_AN_ENABLE | MR_RESTART_AN))
			ap->state = ANEG_STATE_AN_ENABLE;

		/* fallthru */
	case ANEG_STATE_AN_ENABLE:
		ap->flags &= ~(MR_AN_COMPLETE | MR_PAGE_RX);
		if (ap->flags & MR_AN_ENABLE) {
			ap->link_time = 0;
			ap->cur_time = 0;
			ap->ability_match_cfg = 0;
			ap->ability_match_count = 0;
			ap->ability_match = 0;
			ap->idle_match = 0;
			ap->ack_match = 0;

			ap->state = ANEG_STATE_RESTART_INIT;
		} else {
			ap->state = ANEG_STATE_DISABLE_LINK_OK;
		}
		break;

	case ANEG_STATE_RESTART_INIT:
		ap->link_time = ap->cur_time;
		ap->flags &= ~(MR_NP_LOADED);
		ap->txconfig = 0;
		tw32(MAC_TX_AUTO_NEG, 0);
		tp->mac_mode |= MAC_MODE_SEND_CONFIGS;
		tw32_f(MAC_MODE, tp->mac_mode);
		udelay(40);

		ret = ANEG_TIMER_ENAB;
		ap->state = ANEG_STATE_RESTART;

		/* fallthru */
	case ANEG_STATE_RESTART:
		delta = ap->cur_time - ap->link_time;
		if (delta > ANEG_STATE_SETTLE_TIME)
			ap->state = ANEG_STATE_ABILITY_DETECT_INIT;
		else
			ret = ANEG_TIMER_ENAB;
		break;

	case ANEG_STATE_DISABLE_LINK_OK:
		ret = ANEG_DONE;
		break;

	case ANEG_STATE_ABILITY_DETECT_INIT:
		ap->flags &= ~(MR_TOGGLE_TX);
		ap->txconfig = ANEG_CFG_FD;
		flowctrl = tg3_advert_flowctrl_1000X(tp->link_config.flowctrl);
		if (flowctrl & ADVERTISE_1000XPAUSE)
			ap->txconfig |= ANEG_CFG_PS1;
		if (flowctrl & ADVERTISE_1000XPSE_ASYM)
			ap->txconfig |= ANEG_CFG_PS2;
		tw32(MAC_TX_AUTO_NEG, ap->txconfig);
		tp->mac_mode |= MAC_MODE_SEND_CONFIGS;
		tw32_f(MAC_MODE, tp->mac_mode);
		udelay(40);

		ap->state = ANEG_STATE_ABILITY_DETECT;
		break;

	case ANEG_STATE_ABILITY_DETECT:
		if (ap->ability_match != 0 && ap->rxconfig != 0)
			ap->state = ANEG_STATE_ACK_DETECT_INIT;
		break;

	case ANEG_STATE_ACK_DETECT_INIT:
		ap->txconfig |= ANEG_CFG_ACK;
		tw32(MAC_TX_AUTO_NEG, ap->txconfig);
		tp->mac_mode |= MAC_MODE_SEND_CONFIGS;
		tw32_f(MAC_MODE, tp->mac_mode);
		udelay(40);

		ap->state = ANEG_STATE_ACK_DETECT;

		/* fallthru */
	case ANEG_STATE_ACK_DETECT:
		if (ap->ack_match != 0) {
			if ((ap->rxconfig & ~ANEG_CFG_ACK) ==
			    (ap->ability_match_cfg & ~ANEG_CFG_ACK)) {
				ap->state = ANEG_STATE_COMPLETE_ACK_INIT;
			} else {
				ap->state = ANEG_STATE_AN_ENABLE;
			}
		} else if (ap->ability_match != 0 &&
			   ap->rxconfig == 0) {
			ap->state = ANEG_STATE_AN_ENABLE;
		}
		break;

	case ANEG_STATE_COMPLETE_ACK_INIT:
		if (ap->rxconfig & ANEG_CFG_INVAL) {
			ret = ANEG_FAILED;
			break;
		}
		ap->flags &= ~(MR_LP_ADV_FULL_DUPLEX |
			       MR_LP_ADV_HALF_DUPLEX |
			       MR_LP_ADV_SYM_PAUSE |
			       MR_LP_ADV_ASYM_PAUSE |
			       MR_LP_ADV_REMOTE_FAULT1 |
			       MR_LP_ADV_REMOTE_FAULT2 |
			       MR_LP_ADV_NEXT_PAGE |
			       MR_TOGGLE_RX |
			       MR_NP_RX);
		if (ap->rxconfig & ANEG_CFG_FD)
			ap->flags |= MR_LP_ADV_FULL_DUPLEX;
		if (ap->rxconfig & ANEG_CFG_HD)
			ap->flags |= MR_LP_ADV_HALF_DUPLEX;
		if (ap->rxconfig & ANEG_CFG_PS1)
			ap->flags |= MR_LP_ADV_SYM_PAUSE;
		if (ap->rxconfig & ANEG_CFG_PS2)
			ap->flags |= MR_LP_ADV_ASYM_PAUSE;
		if (ap->rxconfig & ANEG_CFG_RF1)
			ap->flags |= MR_LP_ADV_REMOTE_FAULT1;
		if (ap->rxconfig & ANEG_CFG_RF2)
			ap->flags |= MR_LP_ADV_REMOTE_FAULT2;
		if (ap->rxconfig & ANEG_CFG_NP)
			ap->flags |= MR_LP_ADV_NEXT_PAGE;

		ap->link_time = ap->cur_time;

		ap->flags ^= (MR_TOGGLE_TX);
		if (ap->rxconfig & 0x0008)
			ap->flags |= MR_TOGGLE_RX;
		if (ap->rxconfig & ANEG_CFG_NP)
			ap->flags |= MR_NP_RX;
		ap->flags |= MR_PAGE_RX;

		ap->state = ANEG_STATE_COMPLETE_ACK;
		ret = ANEG_TIMER_ENAB;
		break;

	case ANEG_STATE_COMPLETE_ACK:
		if (ap->ability_match != 0 &&
		    ap->rxconfig == 0) {
			ap->state = ANEG_STATE_AN_ENABLE;
			break;
		}
		delta = ap->cur_time - ap->link_time;
		if (delta > ANEG_STATE_SETTLE_TIME) {
			if (!(ap->flags & (MR_LP_ADV_NEXT_PAGE))) {
				ap->state = ANEG_STATE_IDLE_DETECT_INIT;
			} else {
				if ((ap->txconfig & ANEG_CFG_NP) == 0 &&
				    !(ap->flags & MR_NP_RX)) {
					ap->state = ANEG_STATE_IDLE_DETECT_INIT;
				} else {
					ret = ANEG_FAILED;
				}
			}
		}
		break;

	case ANEG_STATE_IDLE_DETECT_INIT:
		ap->link_time = ap->cur_time;
		tp->mac_mode &= ~MAC_MODE_SEND_CONFIGS;
		tw32_f(MAC_MODE, tp->mac_mode);
		udelay(40);

		ap->state = ANEG_STATE_IDLE_DETECT;
		ret = ANEG_TIMER_ENAB;
		break;

	case ANEG_STATE_IDLE_DETECT:
		if (ap->ability_match != 0 &&
		    ap->rxconfig == 0) {
			ap->state = ANEG_STATE_AN_ENABLE;
			break;
		}
		delta = ap->cur_time - ap->link_time;
		if (delta > ANEG_STATE_SETTLE_TIME) {
			/* XXX another gem from the Broadcom driver :( */
			ap->state = ANEG_STATE_LINK_OK;
		}
		break;

	case ANEG_STATE_LINK_OK:
		ap->flags |= (MR_AN_COMPLETE | MR_LINK_OK);
		ret = ANEG_DONE;
		break;

	case ANEG_STATE_NEXT_PAGE_WAIT_INIT:
		/* ??? unimplemented */
		break;

	case ANEG_STATE_NEXT_PAGE_WAIT:
		/* ??? unimplemented */
		break;

	default:
		ret = ANEG_FAILED;
		break;
	}

	return ret;
}

static int fiber_autoneg(struct tg3 *tp, u32 *txflags, u32 *rxflags)
{
	int res = 0;
	struct tg3_fiber_aneginfo aninfo;
	int status = ANEG_FAILED;
	unsigned int tick;
	u32 tmp;

	tw32_f(MAC_TX_AUTO_NEG, 0);

	tmp = tp->mac_mode & ~MAC_MODE_PORT_MODE_MASK;
	tw32_f(MAC_MODE, tmp | MAC_MODE_PORT_MODE_GMII);
	udelay(40);

	tw32_f(MAC_MODE, tp->mac_mode | MAC_MODE_SEND_CONFIGS);
	udelay(40);

	memset(&aninfo, 0, sizeof(aninfo));
	aninfo.flags |= MR_AN_ENABLE;
	aninfo.state = ANEG_STATE_UNKNOWN;
	aninfo.cur_time = 0;
	tick = 0;
	while (++tick < 195000) {
		status = tg3_fiber_aneg_smachine(tp, &aninfo);
		if (status == ANEG_DONE || status == ANEG_FAILED)
			break;

		udelay(1);
	}

	tp->mac_mode &= ~MAC_MODE_SEND_CONFIGS;
	tw32_f(MAC_MODE, tp->mac_mode);
	udelay(40);

	*txflags = aninfo.txconfig;
	*rxflags = aninfo.flags;

	if (status == ANEG_DONE &&
	    (aninfo.flags & (MR_AN_COMPLETE | MR_LINK_OK |
			     MR_LP_ADV_FULL_DUPLEX)))
		res = 1;

	return res;
}

static void tg3_init_bcm8002(struct tg3 *tp)
{
	u32 mac_status = tr32(MAC_STATUS);
	int i;

	/* Reset when initting first time or we have a link. */
	if (tg3_flag(tp, INIT_COMPLETE) &&
	    !(mac_status & MAC_STATUS_PCS_SYNCED))
		return;

	/* Set PLL lock range. */
	tg3_writephy(tp, 0x16, 0x8007);

	/* SW reset */
	tg3_writephy(tp, MII_BMCR, BMCR_RESET);

	/* Wait for reset to complete. */
	/* XXX schedule_timeout() ... */
	for (i = 0; i < 500; i++)
		udelay(10);

	/* Config mode; select PMA/Ch 1 regs. */
	tg3_writephy(tp, 0x10, 0x8411);

	/* Enable auto-lock and comdet, select txclk for tx. */
	tg3_writephy(tp, 0x11, 0x0a10);

	tg3_writephy(tp, 0x18, 0x00a0);
	tg3_writephy(tp, 0x16, 0x41ff);

	/* Assert and deassert POR. */
	tg3_writephy(tp, 0x13, 0x0400);
	udelay(40);
	tg3_writephy(tp, 0x13, 0x0000);

	tg3_writephy(tp, 0x11, 0x0a50);
	udelay(40);
	tg3_writephy(tp, 0x11, 0x0a10);

	/* Wait for signal to stabilize */
	/* XXX schedule_timeout() ... */
	for (i = 0; i < 15000; i++)
		udelay(10);

	/* Deselect the channel register so we can read the PHYID
	 * later.
	 */
	tg3_writephy(tp, 0x10, 0x8011);
}

static bool tg3_setup_fiber_hw_autoneg(struct tg3 *tp, u32 mac_status)
{
	u16 flowctrl;
	bool current_link_up;
	u32 sg_dig_ctrl, sg_dig_status;
	u32 serdes_cfg, expected_sg_dig_ctrl;
	int workaround, port_a;

	serdes_cfg = 0;
	expected_sg_dig_ctrl = 0;
	workaround = 0;
	port_a = 1;
	current_link_up = false;

	if (tg3_chip_rev_id(tp) != CHIPREV_ID_5704_A0 &&
	    tg3_chip_rev_id(tp) != CHIPREV_ID_5704_A1) {
		workaround = 1;
		if (tr32(TG3PCI_DUAL_MAC_CTRL) & DUAL_MAC_CTRL_ID)
			port_a = 0;

		/* preserve bits 0-11,13,14 for signal pre-emphasis */
		/* preserve bits 20-23 for voltage regulator */
		serdes_cfg = tr32(MAC_SERDES_CFG) & 0x00f06fff;
	}

	sg_dig_ctrl = tr32(SG_DIG_CTRL);

	if (tp->link_config.autoneg != AUTONEG_ENABLE) {
		if (sg_dig_ctrl & SG_DIG_USING_HW_AUTONEG) {
			if (workaround) {
				u32 val = serdes_cfg;

				if (port_a)
					val |= 0xc010000;
				else
					val |= 0x4010000;
				tw32_f(MAC_SERDES_CFG, val);
			}

			tw32_f(SG_DIG_CTRL, SG_DIG_COMMON_SETUP);
		}
		if (mac_status & MAC_STATUS_PCS_SYNCED) {
			tg3_setup_flow_control(tp, 0, 0);
			current_link_up = true;
		}
		goto out;
	}

	/* Want auto-negotiation.  */
	expected_sg_dig_ctrl = SG_DIG_USING_HW_AUTONEG | SG_DIG_COMMON_SETUP;

	flowctrl = tg3_advert_flowctrl_1000X(tp->link_config.flowctrl);
	if (flowctrl & ADVERTISE_1000XPAUSE)
		expected_sg_dig_ctrl |= SG_DIG_PAUSE_CAP;
	if (flowctrl & ADVERTISE_1000XPSE_ASYM)
		expected_sg_dig_ctrl |= SG_DIG_ASYM_PAUSE;

	if (sg_dig_ctrl != expected_sg_dig_ctrl) {
		if ((tp->phy_flags & TG3_PHYFLG_PARALLEL_DETECT) &&
		    tp->serdes_counter &&
		    ((mac_status & (MAC_STATUS_PCS_SYNCED |
				    MAC_STATUS_RCVD_CFG)) ==
		     MAC_STATUS_PCS_SYNCED)) {
			tp->serdes_counter--;
			current_link_up = true;
			goto out;
		}
restart_autoneg:
		if (workaround)
			tw32_f(MAC_SERDES_CFG, serdes_cfg | 0xc011000);
		tw32_f(SG_DIG_CTRL, expected_sg_dig_ctrl | SG_DIG_SOFT_RESET);
		udelay(5);
		tw32_f(SG_DIG_CTRL, expected_sg_dig_ctrl);

		tp->serdes_counter = SERDES_AN_TIMEOUT_5704S;
		tp->phy_flags &= ~TG3_PHYFLG_PARALLEL_DETECT;
	} else if (mac_status & (MAC_STATUS_PCS_SYNCED |
				 MAC_STATUS_SIGNAL_DET)) {
		sg_dig_status = tr32(SG_DIG_STATUS);
		mac_status = tr32(MAC_STATUS);

		if ((sg_dig_status & SG_DIG_AUTONEG_COMPLETE) &&
		    (mac_status & MAC_STATUS_PCS_SYNCED)) {
			u32 local_adv = 0, remote_adv = 0;

			if (sg_dig_ctrl & SG_DIG_PAUSE_CAP)
				local_adv |= ADVERTISE_1000XPAUSE;
			if (sg_dig_ctrl & SG_DIG_ASYM_PAUSE)
				local_adv |= ADVERTISE_1000XPSE_ASYM;

			if (sg_dig_status & SG_DIG_PARTNER_PAUSE_CAPABLE)
				remote_adv |= LPA_1000XPAUSE;
			if (sg_dig_status & SG_DIG_PARTNER_ASYM_PAUSE)
				remote_adv |= LPA_1000XPAUSE_ASYM;

			tp->link_config.rmt_adv =
					   mii_adv_to_ethtool_adv_x(remote_adv);

			tg3_setup_flow_control(tp, local_adv, remote_adv);
			current_link_up = true;
			tp->serdes_counter = 0;
			tp->phy_flags &= ~TG3_PHYFLG_PARALLEL_DETECT;
		} else if (!(sg_dig_status & SG_DIG_AUTONEG_COMPLETE)) {
			if (tp->serdes_counter)
				tp->serdes_counter--;
			else {
				if (workaround) {
					u32 val = serdes_cfg;

					if (port_a)
						val |= 0xc010000;
					else
						val |= 0x4010000;

					tw32_f(MAC_SERDES_CFG, val);
				}

				tw32_f(SG_DIG_CTRL, SG_DIG_COMMON_SETUP);
				udelay(40);

				/* Link parallel detection - link is up */
				/* only if we have PCS_SYNC and not */
				/* receiving config code words */
				mac_status = tr32(MAC_STATUS);
				if ((mac_status & MAC_STATUS_PCS_SYNCED) &&
				    !(mac_status & MAC_STATUS_RCVD_CFG)) {
					tg3_setup_flow_control(tp, 0, 0);
					current_link_up = true;
					tp->phy_flags |=
						TG3_PHYFLG_PARALLEL_DETECT;
					tp->serdes_counter =
						SERDES_PARALLEL_DET_TIMEOUT;
				} else
					goto restart_autoneg;
			}
		}
	} else {
		tp->serdes_counter = SERDES_AN_TIMEOUT_5704S;
		tp->phy_flags &= ~TG3_PHYFLG_PARALLEL_DETECT;
	}

out:
	return current_link_up;
}

static bool tg3_setup_fiber_by_hand(struct tg3 *tp, u32 mac_status)
{
	bool current_link_up = false;

	if (!(mac_status & MAC_STATUS_PCS_SYNCED))
		goto out;

	if (tp->link_config.autoneg == AUTONEG_ENABLE) {
		u32 txflags, rxflags;
		int i;

		if (fiber_autoneg(tp, &txflags, &rxflags)) {
			u32 local_adv = 0, remote_adv = 0;

			if (txflags & ANEG_CFG_PS1)
				local_adv |= ADVERTISE_1000XPAUSE;
			if (txflags & ANEG_CFG_PS2)
				local_adv |= ADVERTISE_1000XPSE_ASYM;

			if (rxflags & MR_LP_ADV_SYM_PAUSE)
				remote_adv |= LPA_1000XPAUSE;
			if (rxflags & MR_LP_ADV_ASYM_PAUSE)
				remote_adv |= LPA_1000XPAUSE_ASYM;

			tp->link_config.rmt_adv =
					   mii_adv_to_ethtool_adv_x(remote_adv);

			tg3_setup_flow_control(tp, local_adv, remote_adv);

			current_link_up = true;
		}
		for (i = 0; i < 30; i++) {
			udelay(20);
			tw32_f(MAC_STATUS,
			       (MAC_STATUS_SYNC_CHANGED |
				MAC_STATUS_CFG_CHANGED));
			udelay(40);
			if ((tr32(MAC_STATUS) &
			     (MAC_STATUS_SYNC_CHANGED |
			      MAC_STATUS_CFG_CHANGED)) == 0)
				break;
		}

		mac_status = tr32(MAC_STATUS);
		if (!current_link_up &&
		    (mac_status & MAC_STATUS_PCS_SYNCED) &&
		    !(mac_status & MAC_STATUS_RCVD_CFG))
			current_link_up = true;
	} else {
		tg3_setup_flow_control(tp, 0, 0);

		/* Forcing 1000FD link up. */
		current_link_up = true;

		tw32_f(MAC_MODE, (tp->mac_mode | MAC_MODE_SEND_CONFIGS));
		udelay(40);

		tw32_f(MAC_MODE, tp->mac_mode);
		udelay(40);
	}

out:
	return current_link_up;
}

static int tg3_setup_fiber_phy(struct tg3 *tp, bool force_reset)
{
	u32 orig_pause_cfg;
	u16 orig_active_speed;
	u8 orig_active_duplex;
	u32 mac_status;
	bool current_link_up;
	int i;

	orig_pause_cfg = tp->link_config.active_flowctrl;
	orig_active_speed = tp->link_config.active_speed;
	orig_active_duplex = tp->link_config.active_duplex;

	if (!tg3_flag(tp, HW_AUTONEG) &&
	    tp->link_up &&
	    tg3_flag(tp, INIT_COMPLETE)) {
		mac_status = tr32(MAC_STATUS);
		mac_status &= (MAC_STATUS_PCS_SYNCED |
			       MAC_STATUS_SIGNAL_DET |
			       MAC_STATUS_CFG_CHANGED |
			       MAC_STATUS_RCVD_CFG);
		if (mac_status == (MAC_STATUS_PCS_SYNCED |
				   MAC_STATUS_SIGNAL_DET)) {
			tw32_f(MAC_STATUS, (MAC_STATUS_SYNC_CHANGED |
					    MAC_STATUS_CFG_CHANGED));
			return 0;
		}
	}

	tw32_f(MAC_TX_AUTO_NEG, 0);

	tp->mac_mode &= ~(MAC_MODE_PORT_MODE_MASK | MAC_MODE_HALF_DUPLEX);
	tp->mac_mode |= MAC_MODE_PORT_MODE_TBI;
	tw32_f(MAC_MODE, tp->mac_mode);
	udelay(40);

	if (tp->phy_id == TG3_PHY_ID_BCM8002)
		tg3_init_bcm8002(tp);

	/* Enable link change event even when serdes polling.  */
	tw32_f(MAC_EVENT, MAC_EVENT_LNKSTATE_CHANGED);
	udelay(40);

	current_link_up = false;
	tp->link_config.rmt_adv = 0;
	mac_status = tr32(MAC_STATUS);

	if (tg3_flag(tp, HW_AUTONEG))
		current_link_up = tg3_setup_fiber_hw_autoneg(tp, mac_status);
	else
		current_link_up = tg3_setup_fiber_by_hand(tp, mac_status);

	tp->napi[0].hw_status->status =
		(SD_STATUS_UPDATED |
		 (tp->napi[0].hw_status->status & ~SD_STATUS_LINK_CHG));

	for (i = 0; i < 100; i++) {
		tw32_f(MAC_STATUS, (MAC_STATUS_SYNC_CHANGED |
				    MAC_STATUS_CFG_CHANGED));
		udelay(5);
		if ((tr32(MAC_STATUS) & (MAC_STATUS_SYNC_CHANGED |
					 MAC_STATUS_CFG_CHANGED |
					 MAC_STATUS_LNKSTATE_CHANGED)) == 0)
			break;
	}

	mac_status = tr32(MAC_STATUS);
	if ((mac_status & MAC_STATUS_PCS_SYNCED) == 0) {
		current_link_up = false;
		if (tp->link_config.autoneg == AUTONEG_ENABLE &&
		    tp->serdes_counter == 0) {
			tw32_f(MAC_MODE, (tp->mac_mode |
					  MAC_MODE_SEND_CONFIGS));
			udelay(1);
			tw32_f(MAC_MODE, tp->mac_mode);
		}
	}

	if (current_link_up) {
		tp->link_config.active_speed = SPEED_1000;
		tp->link_config.active_duplex = DUPLEX_FULL;
		tw32(MAC_LED_CTRL, (tp->led_ctrl |
				    LED_CTRL_LNKLED_OVERRIDE |
				    LED_CTRL_1000MBPS_ON));
	} else {
		tp->link_config.active_speed = SPEED_UNKNOWN;
		tp->link_config.active_duplex = DUPLEX_UNKNOWN;
		tw32(MAC_LED_CTRL, (tp->led_ctrl |
				    LED_CTRL_LNKLED_OVERRIDE |
				    LED_CTRL_TRAFFIC_OVERRIDE));
	}

	if (!tg3_test_and_report_link_chg(tp, current_link_up)) {
		u32 now_pause_cfg = tp->link_config.active_flowctrl;
		if (orig_pause_cfg != now_pause_cfg ||
		    orig_active_speed != tp->link_config.active_speed ||
		    orig_active_duplex != tp->link_config.active_duplex)
			tg3_link_report(tp);
	}

	return 0;
}

static int tg3_setup_fiber_mii_phy(struct tg3 *tp, bool force_reset)
{
	int err = 0;
	u32 bmsr, bmcr;
	u16 current_speed = SPEED_UNKNOWN;
	u8 current_duplex = DUPLEX_UNKNOWN;
	bool current_link_up = false;
	u32 local_adv, remote_adv, sgsr;

	if ((tg3_asic_rev(tp) == ASIC_REV_5719 ||
	     tg3_asic_rev(tp) == ASIC_REV_5720) &&
	     !tg3_readphy(tp, SERDES_TG3_1000X_STATUS, &sgsr) &&
	     (sgsr & SERDES_TG3_SGMII_MODE)) {

		if (force_reset)
			tg3_phy_reset(tp);

		tp->mac_mode &= ~MAC_MODE_PORT_MODE_MASK;

		if (!(sgsr & SERDES_TG3_LINK_UP)) {
			tp->mac_mode |= MAC_MODE_PORT_MODE_GMII;
		} else {
			current_link_up = true;
			if (sgsr & SERDES_TG3_SPEED_1000) {
				current_speed = SPEED_1000;
				tp->mac_mode |= MAC_MODE_PORT_MODE_GMII;
			} else if (sgsr & SERDES_TG3_SPEED_100) {
				current_speed = SPEED_100;
				tp->mac_mode |= MAC_MODE_PORT_MODE_MII;
			} else {
				current_speed = SPEED_10;
				tp->mac_mode |= MAC_MODE_PORT_MODE_MII;
			}

			if (sgsr & SERDES_TG3_FULL_DUPLEX)
				current_duplex = DUPLEX_FULL;
			else
				current_duplex = DUPLEX_HALF;
		}

		tw32_f(MAC_MODE, tp->mac_mode);
		udelay(40);

		tg3_clear_mac_status(tp);

		goto fiber_setup_done;
	}

	tp->mac_mode |= MAC_MODE_PORT_MODE_GMII;
	tw32_f(MAC_MODE, tp->mac_mode);
	udelay(40);

	tg3_clear_mac_status(tp);

	if (force_reset)
		tg3_phy_reset(tp);

	tp->link_config.rmt_adv = 0;

	err |= tg3_readphy(tp, MII_BMSR, &bmsr);
	err |= tg3_readphy(tp, MII_BMSR, &bmsr);
	if (tg3_asic_rev(tp) == ASIC_REV_5714) {
		if (tr32(MAC_TX_STATUS) & TX_STATUS_LINK_UP)
			bmsr |= BMSR_LSTATUS;
		else
			bmsr &= ~BMSR_LSTATUS;
	}

	err |= tg3_readphy(tp, MII_BMCR, &bmcr);

	if ((tp->link_config.autoneg == AUTONEG_ENABLE) && !force_reset &&
	    (tp->phy_flags & TG3_PHYFLG_PARALLEL_DETECT)) {
		/* do nothing, just check for link up at the end */
	} else if (tp->link_config.autoneg == AUTONEG_ENABLE) {
		u32 adv, newadv;

		err |= tg3_readphy(tp, MII_ADVERTISE, &adv);
		newadv = adv & ~(ADVERTISE_1000XFULL | ADVERTISE_1000XHALF |
				 ADVERTISE_1000XPAUSE |
				 ADVERTISE_1000XPSE_ASYM |
				 ADVERTISE_SLCT);

		newadv |= tg3_advert_flowctrl_1000X(tp->link_config.flowctrl);
		newadv |= ethtool_adv_to_mii_adv_x(tp->link_config.advertising);

		if ((newadv != adv) || !(bmcr & BMCR_ANENABLE)) {
			tg3_writephy(tp, MII_ADVERTISE, newadv);
			bmcr |= BMCR_ANENABLE | BMCR_ANRESTART;
			tg3_writephy(tp, MII_BMCR, bmcr);

			tw32_f(MAC_EVENT, MAC_EVENT_LNKSTATE_CHANGED);
			tp->serdes_counter = SERDES_AN_TIMEOUT_5714S;
			tp->phy_flags &= ~TG3_PHYFLG_PARALLEL_DETECT;

			return err;
		}
	} else {
		u32 new_bmcr;

		bmcr &= ~BMCR_SPEED1000;
		new_bmcr = bmcr & ~(BMCR_ANENABLE | BMCR_FULLDPLX);

		if (tp->link_config.duplex == DUPLEX_FULL)
			new_bmcr |= BMCR_FULLDPLX;

		if (new_bmcr != bmcr) {
			/* BMCR_SPEED1000 is a reserved bit that needs
			 * to be set on write.
			 */
			new_bmcr |= BMCR_SPEED1000;

			/* Force a linkdown */
			if (tp->link_up) {
				u32 adv;

				err |= tg3_readphy(tp, MII_ADVERTISE, &adv);
				adv &= ~(ADVERTISE_1000XFULL |
					 ADVERTISE_1000XHALF |
					 ADVERTISE_SLCT);
				tg3_writephy(tp, MII_ADVERTISE, adv);
				tg3_writephy(tp, MII_BMCR, bmcr |
							   BMCR_ANRESTART |
							   BMCR_ANENABLE);
				udelay(10);
				tg3_carrier_off(tp);
			}
			tg3_writephy(tp, MII_BMCR, new_bmcr);
			bmcr = new_bmcr;
			err |= tg3_readphy(tp, MII_BMSR, &bmsr);
			err |= tg3_readphy(tp, MII_BMSR, &bmsr);
			if (tg3_asic_rev(tp) == ASIC_REV_5714) {
				if (tr32(MAC_TX_STATUS) & TX_STATUS_LINK_UP)
					bmsr |= BMSR_LSTATUS;
				else
					bmsr &= ~BMSR_LSTATUS;
			}
			tp->phy_flags &= ~TG3_PHYFLG_PARALLEL_DETECT;
		}
	}

	if (bmsr & BMSR_LSTATUS) {
		current_speed = SPEED_1000;
		current_link_up = true;
		if (bmcr & BMCR_FULLDPLX)
			current_duplex = DUPLEX_FULL;
		else
			current_duplex = DUPLEX_HALF;

		local_adv = 0;
		remote_adv = 0;

		if (bmcr & BMCR_ANENABLE) {
			u32 common;

			err |= tg3_readphy(tp, MII_ADVERTISE, &local_adv);
			err |= tg3_readphy(tp, MII_LPA, &remote_adv);
			common = local_adv & remote_adv;
			if (common & (ADVERTISE_1000XHALF |
				      ADVERTISE_1000XFULL)) {
				if (common & ADVERTISE_1000XFULL)
					current_duplex = DUPLEX_FULL;
				else
					current_duplex = DUPLEX_HALF;

				tp->link_config.rmt_adv =
					   mii_adv_to_ethtool_adv_x(remote_adv);
			} else if (!tg3_flag(tp, 5780_CLASS)) {
				/* Link is up via parallel detect */
			} else {
				current_link_up = false;
			}
		}
	}

fiber_setup_done:
	if (current_link_up && current_duplex == DUPLEX_FULL)
		tg3_setup_flow_control(tp, local_adv, remote_adv);

	tp->mac_mode &= ~MAC_MODE_HALF_DUPLEX;
	if (tp->link_config.active_duplex == DUPLEX_HALF)
		tp->mac_mode |= MAC_MODE_HALF_DUPLEX;

	tw32_f(MAC_MODE, tp->mac_mode);
	udelay(40);

	tw32_f(MAC_EVENT, MAC_EVENT_LNKSTATE_CHANGED);

	tp->link_config.active_speed = current_speed;
	tp->link_config.active_duplex = current_duplex;

	tg3_test_and_report_link_chg(tp, current_link_up);
	return err;
}

static void tg3_serdes_parallel_detect(struct tg3 *tp)
{
	if (tp->serdes_counter) {
		/* Give autoneg time to complete. */
		tp->serdes_counter--;
		return;
	}

	if (!tp->link_up &&
	    (tp->link_config.autoneg == AUTONEG_ENABLE)) {
		u32 bmcr;

		tg3_readphy(tp, MII_BMCR, &bmcr);
		if (bmcr & BMCR_ANENABLE) {
			u32 phy1, phy2;

			/* Select shadow register 0x1f */
			tg3_writephy(tp, MII_TG3_MISC_SHDW, 0x7c00);
			tg3_readphy(tp, MII_TG3_MISC_SHDW, &phy1);

			/* Select expansion interrupt status register */
			tg3_writephy(tp, MII_TG3_DSP_ADDRESS,
					 MII_TG3_DSP_EXP1_INT_STAT);
			tg3_readphy(tp, MII_TG3_DSP_RW_PORT, &phy2);
			tg3_readphy(tp, MII_TG3_DSP_RW_PORT, &phy2);

			if ((phy1 & 0x10) && !(phy2 & 0x20)) {
				/* We have signal detect and not receiving
				 * config code words, link is up by parallel
				 * detection.
				 */

				bmcr &= ~BMCR_ANENABLE;
				bmcr |= BMCR_SPEED1000 | BMCR_FULLDPLX;
				tg3_writephy(tp, MII_BMCR, bmcr);
				tp->phy_flags |= TG3_PHYFLG_PARALLEL_DETECT;
			}
		}
	} else if (tp->link_up &&
		   (tp->link_config.autoneg == AUTONEG_ENABLE) &&
		   (tp->phy_flags & TG3_PHYFLG_PARALLEL_DETECT)) {
		u32 phy2;

		/* Select expansion interrupt status register */
		tg3_writephy(tp, MII_TG3_DSP_ADDRESS,
				 MII_TG3_DSP_EXP1_INT_STAT);
		tg3_readphy(tp, MII_TG3_DSP_RW_PORT, &phy2);
		if (phy2 & 0x20) {
			u32 bmcr;

			/* Config code words received, turn on autoneg. */
			tg3_readphy(tp, MII_BMCR, &bmcr);
			tg3_writephy(tp, MII_BMCR, bmcr | BMCR_ANENABLE);

			tp->phy_flags &= ~TG3_PHYFLG_PARALLEL_DETECT;

		}
	}
}

static int tg3_setup_phy(struct tg3 *tp, bool force_reset)
{
	u32 val;
	int err;

	if (tp->phy_flags & TG3_PHYFLG_PHY_SERDES)
		err = tg3_setup_fiber_phy(tp, force_reset);
	else if (tp->phy_flags & TG3_PHYFLG_MII_SERDES)
		err = tg3_setup_fiber_mii_phy(tp, force_reset);
	else
		err = tg3_setup_copper_phy(tp, force_reset);

	if (tg3_chip_rev(tp) == CHIPREV_5784_AX) {
		u32 scale;

		val = tr32(TG3_CPMU_CLCK_STAT) & CPMU_CLCK_STAT_MAC_CLCK_MASK;
		if (val == CPMU_CLCK_STAT_MAC_CLCK_62_5)
			scale = 65;
		else if (val == CPMU_CLCK_STAT_MAC_CLCK_6_25)
			scale = 6;
		else
			scale = 12;

		val = tr32(GRC_MISC_CFG) & ~GRC_MISC_CFG_PRESCALAR_MASK;
		val |= (scale << GRC_MISC_CFG_PRESCALAR_SHIFT);
		tw32(GRC_MISC_CFG, val);
	}

	val = (2 << TX_LENGTHS_IPG_CRS_SHIFT) |
	      (6 << TX_LENGTHS_IPG_SHIFT);
	if (tg3_asic_rev(tp) == ASIC_REV_5720 ||
	    tg3_asic_rev(tp) == ASIC_REV_5762)
		val |= tr32(MAC_TX_LENGTHS) &
		       (TX_LENGTHS_JMB_FRM_LEN_MSK |
			TX_LENGTHS_CNT_DWN_VAL_MSK);

	if (tp->link_config.active_speed == SPEED_1000 &&
	    tp->link_config.active_duplex == DUPLEX_HALF)
		tw32(MAC_TX_LENGTHS, val |
		     (0xff << TX_LENGTHS_SLOT_TIME_SHIFT));
	else
		tw32(MAC_TX_LENGTHS, val |
		     (32 << TX_LENGTHS_SLOT_TIME_SHIFT));

	if (!tg3_flag(tp, 5705_PLUS)) {
		if (tp->link_up) {
			tw32(HOSTCC_STAT_COAL_TICKS,
			     tp->coal.stats_block_coalesce_usecs);
		} else {
			tw32(HOSTCC_STAT_COAL_TICKS, 0);
		}
	}

	if (tg3_flag(tp, ASPM_WORKAROUND)) {
		val = tr32(PCIE_PWR_MGMT_THRESH);
		if (!tp->link_up)
			val = (val & ~PCIE_PWR_MGMT_L1_THRESH_MSK) |
			      tp->pwrmgmt_thresh;
		else
			val |= PCIE_PWR_MGMT_L1_THRESH_MSK;
		tw32(PCIE_PWR_MGMT_THRESH, val);
	}

	return err;
}

/* tp->lock must be held */
static u64 tg3_refclk_read(struct tg3 *tp)
{
	u64 stamp = tr32(TG3_EAV_REF_CLCK_LSB);
	return stamp | (u64)tr32(TG3_EAV_REF_CLCK_MSB) << 32;
}

/* tp->lock must be held */
static void tg3_refclk_write(struct tg3 *tp, u64 newval)
{
	tw32(TG3_EAV_REF_CLCK_CTL, TG3_EAV_REF_CLCK_CTL_STOP);
	tw32(TG3_EAV_REF_CLCK_LSB, newval & 0xffffffff);
	tw32(TG3_EAV_REF_CLCK_MSB, newval >> 32);
	tw32_f(TG3_EAV_REF_CLCK_CTL, TG3_EAV_REF_CLCK_CTL_RESUME);
}

static inline void tg3_full_lock(struct tg3 *tp, int irq_sync);
static inline void tg3_full_unlock(struct tg3 *tp);
static int tg3_get_ts_info(struct net_device *dev, struct ethtool_ts_info *info)
{
	struct tg3 *tp = netdev_priv(dev);

	info->so_timestamping = SOF_TIMESTAMPING_TX_SOFTWARE |
				SOF_TIMESTAMPING_RX_SOFTWARE |
				SOF_TIMESTAMPING_SOFTWARE;

	if (tg3_flag(tp, PTP_CAPABLE)) {
		info->so_timestamping |= SOF_TIMESTAMPING_TX_HARDWARE |
					SOF_TIMESTAMPING_RX_HARDWARE |
					SOF_TIMESTAMPING_RAW_HARDWARE;
	}

	if (tp->ptp_clock)
		info->phc_index = ptp_clock_index(tp->ptp_clock);
	else
		info->phc_index = -1;

	info->tx_types = (1 << HWTSTAMP_TX_OFF) | (1 << HWTSTAMP_TX_ON);

	info->rx_filters = (1 << HWTSTAMP_FILTER_NONE) |
			   (1 << HWTSTAMP_FILTER_PTP_V1_L4_EVENT) |
			   (1 << HWTSTAMP_FILTER_PTP_V2_L2_EVENT) |
			   (1 << HWTSTAMP_FILTER_PTP_V2_L4_EVENT);
	return 0;
}

static int tg3_ptp_adjfreq(struct ptp_clock_info *ptp, s32 ppb)
{
	struct tg3 *tp = container_of(ptp, struct tg3, ptp_info);
	bool neg_adj = false;
	u32 correction = 0;

	if (ppb < 0) {
		neg_adj = true;
		ppb = -ppb;
	}

	/* Frequency adjustment is performed using hardware with a 24 bit
	 * accumulator and a programmable correction value. On each clk, the
	 * correction value gets added to the accumulator and when it
	 * overflows, the time counter is incremented/decremented.
	 *
	 * So conversion from ppb to correction value is
	 *		ppb * (1 << 24) / 1000000000
	 */
	correction = div_u64((u64)ppb * (1 << 24), 1000000000ULL) &
		     TG3_EAV_REF_CLK_CORRECT_MASK;

	tg3_full_lock(tp, 0);

	if (correction)
		tw32(TG3_EAV_REF_CLK_CORRECT_CTL,
		     TG3_EAV_REF_CLK_CORRECT_EN |
		     (neg_adj ? TG3_EAV_REF_CLK_CORRECT_NEG : 0) | correction);
	else
		tw32(TG3_EAV_REF_CLK_CORRECT_CTL, 0);

	tg3_full_unlock(tp);

	return 0;
}

static int tg3_ptp_adjtime(struct ptp_clock_info *ptp, s64 delta)
{
	struct tg3 *tp = container_of(ptp, struct tg3, ptp_info);

	tg3_full_lock(tp, 0);
	tp->ptp_adjust += delta;
	tg3_full_unlock(tp);

	return 0;
}

static int tg3_ptp_gettime(struct ptp_clock_info *ptp, struct timespec *ts)
{
	u64 ns;
	u32 remainder;
	struct tg3 *tp = container_of(ptp, struct tg3, ptp_info);

	tg3_full_lock(tp, 0);
	ns = tg3_refclk_read(tp);
	ns += tp->ptp_adjust;
	tg3_full_unlock(tp);

	ts->tv_sec = div_u64_rem(ns, 1000000000, &remainder);
	ts->tv_nsec = remainder;

	return 0;
}

static int tg3_ptp_settime(struct ptp_clock_info *ptp,
			   const struct timespec *ts)
{
	u64 ns;
	struct tg3 *tp = container_of(ptp, struct tg3, ptp_info);

	ns = timespec_to_ns(ts);

	tg3_full_lock(tp, 0);
	tg3_refclk_write(tp, ns);
	tp->ptp_adjust = 0;
	tg3_full_unlock(tp);

	return 0;
}

static int tg3_ptp_enable(struct ptp_clock_info *ptp,
			  struct ptp_clock_request *rq, int on)
{
	return -EOPNOTSUPP;
}

static const struct ptp_clock_info tg3_ptp_caps = {
	.owner		= THIS_MODULE,
	.name		= "tg3 clock",
	.max_adj	= 250000000,
	.n_alarm	= 0,
	.n_ext_ts	= 0,
	.n_per_out	= 0,
	.pps		= 0,
	.adjfreq	= tg3_ptp_adjfreq,
	.adjtime	= tg3_ptp_adjtime,
	.gettime	= tg3_ptp_gettime,
	.settime	= tg3_ptp_settime,
	.enable		= tg3_ptp_enable,
};

static void tg3_hwclock_to_timestamp(struct tg3 *tp, u64 hwclock,
				     struct skb_shared_hwtstamps *timestamp)
{
	memset(timestamp, 0, sizeof(struct skb_shared_hwtstamps));
	timestamp->hwtstamp  = ns_to_ktime((hwclock & TG3_TSTAMP_MASK) +
					   tp->ptp_adjust);
}

/* tp->lock must be held */
static void tg3_ptp_init(struct tg3 *tp)
{
	if (!tg3_flag(tp, PTP_CAPABLE))
		return;

	/* Initialize the hardware clock to the system time. */
	tg3_refclk_write(tp, ktime_to_ns(ktime_get_real()));
	tp->ptp_adjust = 0;
	tp->ptp_info = tg3_ptp_caps;
}

/* tp->lock must be held */
static void tg3_ptp_resume(struct tg3 *tp)
{
	if (!tg3_flag(tp, PTP_CAPABLE))
		return;

	tg3_refclk_write(tp, ktime_to_ns(ktime_get_real()) + tp->ptp_adjust);
	tp->ptp_adjust = 0;
}

static void tg3_ptp_fini(struct tg3 *tp)
{
	if (!tg3_flag(tp, PTP_CAPABLE) || !tp->ptp_clock)
		return;

	ptp_clock_unregister(tp->ptp_clock);
	tp->ptp_clock = NULL;
	tp->ptp_adjust = 0;
}

static inline int tg3_irq_sync(struct tg3 *tp)
{
	return tp->irq_sync;
}

static inline void tg3_rd32_loop(struct tg3 *tp, u32 *dst, u32 off, u32 len)
{
	int i;

	dst = (u32 *)((u8 *)dst + off);
	for (i = 0; i < len; i += sizeof(u32))
		*dst++ = tr32(off + i);
}

static void tg3_dump_legacy_regs(struct tg3 *tp, u32 *regs)
{
	tg3_rd32_loop(tp, regs, TG3PCI_VENDOR, 0xb0);
	tg3_rd32_loop(tp, regs, MAILBOX_INTERRUPT_0, 0x200);
	tg3_rd32_loop(tp, regs, MAC_MODE, 0x4f0);
	tg3_rd32_loop(tp, regs, SNDDATAI_MODE, 0xe0);
	tg3_rd32_loop(tp, regs, SNDDATAC_MODE, 0x04);
	tg3_rd32_loop(tp, regs, SNDBDS_MODE, 0x80);
	tg3_rd32_loop(tp, regs, SNDBDI_MODE, 0x48);
	tg3_rd32_loop(tp, regs, SNDBDC_MODE, 0x04);
	tg3_rd32_loop(tp, regs, RCVLPC_MODE, 0x20);
	tg3_rd32_loop(tp, regs, RCVLPC_SELLST_BASE, 0x15c);
	tg3_rd32_loop(tp, regs, RCVDBDI_MODE, 0x0c);
	tg3_rd32_loop(tp, regs, RCVDBDI_JUMBO_BD, 0x3c);
	tg3_rd32_loop(tp, regs, RCVDBDI_BD_PROD_IDX_0, 0x44);
	tg3_rd32_loop(tp, regs, RCVDCC_MODE, 0x04);
	tg3_rd32_loop(tp, regs, RCVBDI_MODE, 0x20);
	tg3_rd32_loop(tp, regs, RCVCC_MODE, 0x14);
	tg3_rd32_loop(tp, regs, RCVLSC_MODE, 0x08);
	tg3_rd32_loop(tp, regs, MBFREE_MODE, 0x08);
	tg3_rd32_loop(tp, regs, HOSTCC_MODE, 0x100);

	if (tg3_flag(tp, SUPPORT_MSIX))
		tg3_rd32_loop(tp, regs, HOSTCC_RXCOL_TICKS_VEC1, 0x180);

	tg3_rd32_loop(tp, regs, MEMARB_MODE, 0x10);
	tg3_rd32_loop(tp, regs, BUFMGR_MODE, 0x58);
	tg3_rd32_loop(tp, regs, RDMAC_MODE, 0x08);
	tg3_rd32_loop(tp, regs, WDMAC_MODE, 0x08);
	tg3_rd32_loop(tp, regs, RX_CPU_MODE, 0x04);
	tg3_rd32_loop(tp, regs, RX_CPU_STATE, 0x04);
	tg3_rd32_loop(tp, regs, RX_CPU_PGMCTR, 0x04);
	tg3_rd32_loop(tp, regs, RX_CPU_HWBKPT, 0x04);

	if (!tg3_flag(tp, 5705_PLUS)) {
		tg3_rd32_loop(tp, regs, TX_CPU_MODE, 0x04);
		tg3_rd32_loop(tp, regs, TX_CPU_STATE, 0x04);
		tg3_rd32_loop(tp, regs, TX_CPU_PGMCTR, 0x04);
	}

	tg3_rd32_loop(tp, regs, GRCMBOX_INTERRUPT_0, 0x110);
	tg3_rd32_loop(tp, regs, FTQ_RESET, 0x120);
	tg3_rd32_loop(tp, regs, MSGINT_MODE, 0x0c);
	tg3_rd32_loop(tp, regs, DMAC_MODE, 0x04);
	tg3_rd32_loop(tp, regs, GRC_MODE, 0x4c);

	if (tg3_flag(tp, NVRAM))
		tg3_rd32_loop(tp, regs, NVRAM_CMD, 0x24);
}

static void tg3_dump_state(struct tg3 *tp)
{
	int i;
	u32 *regs;

	regs = kzalloc(TG3_REG_BLK_SIZE, GFP_ATOMIC);
	if (!regs)
		return;

	if (tg3_flag(tp, PCI_EXPRESS)) {
		/* Read up to but not including private PCI registers */
		for (i = 0; i < TG3_PCIE_TLDLPL_PORT; i += sizeof(u32))
			regs[i / sizeof(u32)] = tr32(i);
	} else
		tg3_dump_legacy_regs(tp, regs);

	for (i = 0; i < TG3_REG_BLK_SIZE / sizeof(u32); i += 4) {
		if (!regs[i + 0] && !regs[i + 1] &&
		    !regs[i + 2] && !regs[i + 3])
			continue;

		netdev_err(tp->dev, "0x%08x: 0x%08x, 0x%08x, 0x%08x, 0x%08x\n",
			   i * 4,
			   regs[i + 0], regs[i + 1], regs[i + 2], regs[i + 3]);
	}

	kfree(regs);

	for (i = 0; i < tp->irq_cnt; i++) {
		struct tg3_napi *tnapi = &tp->napi[i];

		/* SW status block */
		netdev_err(tp->dev,
			 "%d: Host status block [%08x:%08x:(%04x:%04x:%04x):(%04x:%04x)]\n",
			   i,
			   tnapi->hw_status->status,
			   tnapi->hw_status->status_tag,
			   tnapi->hw_status->rx_jumbo_consumer,
			   tnapi->hw_status->rx_consumer,
			   tnapi->hw_status->rx_mini_consumer,
			   tnapi->hw_status->idx[0].rx_producer,
			   tnapi->hw_status->idx[0].tx_consumer);

		netdev_err(tp->dev,
		"%d: NAPI info [%08x:%08x:(%04x:%04x:%04x):%04x:(%04x:%04x:%04x:%04x)]\n",
			   i,
			   tnapi->last_tag, tnapi->last_irq_tag,
			   tnapi->tx_prod, tnapi->tx_cons, tnapi->tx_pending,
			   tnapi->rx_rcb_ptr,
			   tnapi->prodring.rx_std_prod_idx,
			   tnapi->prodring.rx_std_cons_idx,
			   tnapi->prodring.rx_jmb_prod_idx,
			   tnapi->prodring.rx_jmb_cons_idx);
	}
}

/* This is called whenever we suspect that the system chipset is re-
 * ordering the sequence of MMIO to the tx send mailbox. The symptom
 * is bogus tx completions. We try to recover by setting the
 * TG3_FLAG_MBOX_WRITE_REORDER flag and resetting the chip later
 * in the workqueue.
 */
static void tg3_tx_recover(struct tg3 *tp)
{
	BUG_ON(tg3_flag(tp, MBOX_WRITE_REORDER) ||
	       tp->write32_tx_mbox == tg3_write_indirect_mbox);

	netdev_warn(tp->dev,
		    "The system may be re-ordering memory-mapped I/O "
		    "cycles to the network device, attempting to recover. "
		    "Please report the problem to the driver maintainer "
		    "and include system chipset information.\n");

	spin_lock(&tp->lock);
	tg3_flag_set(tp, TX_RECOVERY_PENDING);
	spin_unlock(&tp->lock);
}

static inline u32 tg3_tx_avail(struct tg3_napi *tnapi)
{
	/* Tell compiler to fetch tx indices from memory. */
	barrier();
	return tnapi->tx_pending -
	       ((tnapi->tx_prod - tnapi->tx_cons) & (TG3_TX_RING_SIZE - 1));
}

/* Tigon3 never reports partial packet sends.  So we do not
 * need special logic to handle SKBs that have not had all
 * of their frags sent yet, like SunGEM does.
 */
static void tg3_tx(struct tg3_napi *tnapi)
{
	struct tg3 *tp = tnapi->tp;
	u32 hw_idx = tnapi->hw_status->idx[0].tx_consumer;
	u32 sw_idx = tnapi->tx_cons;
	struct netdev_queue *txq;
	int index = tnapi - tp->napi;
	unsigned int pkts_compl = 0, bytes_compl = 0;

	if (tg3_flag(tp, ENABLE_TSS))
		index--;

	txq = netdev_get_tx_queue(tp->dev, index);

	while (sw_idx != hw_idx) {
		struct tg3_tx_ring_info *ri = &tnapi->tx_buffers[sw_idx];
		struct sk_buff *skb = ri->skb;
		int i, tx_bug = 0;

		if (unlikely(skb == NULL)) {
			tg3_tx_recover(tp);
			return;
		}

		if (tnapi->tx_ring[sw_idx].len_flags & TXD_FLAG_HWTSTAMP) {
			struct skb_shared_hwtstamps timestamp;
			u64 hwclock = tr32(TG3_TX_TSTAMP_LSB);
			hwclock |= (u64)tr32(TG3_TX_TSTAMP_MSB) << 32;

			tg3_hwclock_to_timestamp(tp, hwclock, &timestamp);

			skb_tstamp_tx(skb, &timestamp);
		}

		pci_unmap_single(tp->pdev,
				 dma_unmap_addr(ri, mapping),
				 skb_headlen(skb),
				 PCI_DMA_TODEVICE);

		ri->skb = NULL;

		while (ri->fragmented) {
			ri->fragmented = false;
			sw_idx = NEXT_TX(sw_idx);
			ri = &tnapi->tx_buffers[sw_idx];
		}

		sw_idx = NEXT_TX(sw_idx);

		for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
			ri = &tnapi->tx_buffers[sw_idx];
			if (unlikely(ri->skb != NULL || sw_idx == hw_idx))
				tx_bug = 1;

			pci_unmap_page(tp->pdev,
				       dma_unmap_addr(ri, mapping),
				       skb_frag_size(&skb_shinfo(skb)->frags[i]),
				       PCI_DMA_TODEVICE);

			while (ri->fragmented) {
				ri->fragmented = false;
				sw_idx = NEXT_TX(sw_idx);
				ri = &tnapi->tx_buffers[sw_idx];
			}

			sw_idx = NEXT_TX(sw_idx);
		}

		pkts_compl++;
		bytes_compl += skb->len;

		dev_kfree_skb(skb);

		if (unlikely(tx_bug)) {
			tg3_tx_recover(tp);
			return;
		}
	}

	netdev_tx_completed_queue(txq, pkts_compl, bytes_compl);

	tnapi->tx_cons = sw_idx;

	/* Need to make the tx_cons update visible to tg3_start_xmit()
	 * before checking for netif_queue_stopped().  Without the
	 * memory barrier, there is a small possibility that tg3_start_xmit()
	 * will miss it and cause the queue to be stopped forever.
	 */
	smp_mb();

	if (unlikely(netif_tx_queue_stopped(txq) &&
		     (tg3_tx_avail(tnapi) > TG3_TX_WAKEUP_THRESH(tnapi)))) {
		__netif_tx_lock(txq, smp_processor_id());
		if (netif_tx_queue_stopped(txq) &&
		    (tg3_tx_avail(tnapi) > TG3_TX_WAKEUP_THRESH(tnapi)))
			netif_tx_wake_queue(txq);
		__netif_tx_unlock(txq);
	}
}

static void tg3_frag_free(bool is_frag, void *data)
{
	if (is_frag)
		put_page(virt_to_head_page(data));
	else
		kfree(data);
}

static void tg3_rx_data_free(struct tg3 *tp, struct ring_info *ri, u32 map_sz)
{
	unsigned int skb_size = SKB_DATA_ALIGN(map_sz + TG3_RX_OFFSET(tp)) +
		   SKB_DATA_ALIGN(sizeof(struct skb_shared_info));

	if (!ri->data)
		return;

	pci_unmap_single(tp->pdev, dma_unmap_addr(ri, mapping),
			 map_sz, PCI_DMA_FROMDEVICE);
	tg3_frag_free(skb_size <= PAGE_SIZE, ri->data);
	ri->data = NULL;
}


/* Returns size of skb allocated or < 0 on error.
 *
 * We only need to fill in the address because the other members
 * of the RX descriptor are invariant, see tg3_init_rings.
 *
 * Note the purposeful assymetry of cpu vs. chip accesses.  For
 * posting buffers we only dirty the first cache line of the RX
 * descriptor (containing the address).  Whereas for the RX status
 * buffers the cpu only reads the last cacheline of the RX descriptor
 * (to fetch the error flags, vlan tag, checksum, and opaque cookie).
 */
static int tg3_alloc_rx_data(struct tg3 *tp, struct tg3_rx_prodring_set *tpr,
			     u32 opaque_key, u32 dest_idx_unmasked,
			     unsigned int *frag_size)
{
	struct tg3_rx_buffer_desc *desc;
	struct ring_info *map;
	u8 *data;
	dma_addr_t mapping;
	int skb_size, data_size, dest_idx;

	switch (opaque_key) {
	case RXD_OPAQUE_RING_STD:
		dest_idx = dest_idx_unmasked & tp->rx_std_ring_mask;
		desc = &tpr->rx_std[dest_idx];
		map = &tpr->rx_std_buffers[dest_idx];
		data_size = tp->rx_pkt_map_sz;
		break;

	case RXD_OPAQUE_RING_JUMBO:
		dest_idx = dest_idx_unmasked & tp->rx_jmb_ring_mask;
		desc = &tpr->rx_jmb[dest_idx].std;
		map = &tpr->rx_jmb_buffers[dest_idx];
		data_size = TG3_RX_JMB_MAP_SZ;
		break;

	default:
		return -EINVAL;
	}

	/* Do not overwrite any of the map or rp information
	 * until we are sure we can commit to a new buffer.
	 *
	 * Callers depend upon this behavior and assume that
	 * we leave everything unchanged if we fail.
	 */
	skb_size = SKB_DATA_ALIGN(data_size + TG3_RX_OFFSET(tp)) +
		   SKB_DATA_ALIGN(sizeof(struct skb_shared_info));
	if (skb_size <= PAGE_SIZE) {
		data = netdev_alloc_frag(skb_size);
		*frag_size = skb_size;
	} else {
		data = kmalloc(skb_size, GFP_ATOMIC);
		*frag_size = 0;
	}
	if (!data)
		return -ENOMEM;

	mapping = pci_map_single(tp->pdev,
				 data + TG3_RX_OFFSET(tp),
				 data_size,
				 PCI_DMA_FROMDEVICE);
	if (unlikely(pci_dma_mapping_error(tp->pdev, mapping))) {
		tg3_frag_free(skb_size <= PAGE_SIZE, data);
		return -EIO;
	}

	map->data = data;
	dma_unmap_addr_set(map, mapping, mapping);

	desc->addr_hi = ((u64)mapping >> 32);
	desc->addr_lo = ((u64)mapping & 0xffffffff);

	return data_size;
}

/* We only need to move over in the address because the other
 * members of the RX descriptor are invariant.  See notes above
 * tg3_alloc_rx_data for full details.
 */
static void tg3_recycle_rx(struct tg3_napi *tnapi,
			   struct tg3_rx_prodring_set *dpr,
			   u32 opaque_key, int src_idx,
			   u32 dest_idx_unmasked)
{
	struct tg3 *tp = tnapi->tp;
	struct tg3_rx_buffer_desc *src_desc, *dest_desc;
	struct ring_info *src_map, *dest_map;
	struct tg3_rx_prodring_set *spr = &tp->napi[0].prodring;
	int dest_idx;

	switch (opaque_key) {
	case RXD_OPAQUE_RING_STD:
		dest_idx = dest_idx_unmasked & tp->rx_std_ring_mask;
		dest_desc = &dpr->rx_std[dest_idx];
		dest_map = &dpr->rx_std_buffers[dest_idx];
		src_desc = &spr->rx_std[src_idx];
		src_map = &spr->rx_std_buffers[src_idx];
		break;

	case RXD_OPAQUE_RING_JUMBO:
		dest_idx = dest_idx_unmasked & tp->rx_jmb_ring_mask;
		dest_desc = &dpr->rx_jmb[dest_idx].std;
		dest_map = &dpr->rx_jmb_buffers[dest_idx];
		src_desc = &spr->rx_jmb[src_idx].std;
		src_map = &spr->rx_jmb_buffers[src_idx];
		break;

	default:
		return;
	}

	dest_map->data = src_map->data;
	dma_unmap_addr_set(dest_map, mapping,
			   dma_unmap_addr(src_map, mapping));
	dest_desc->addr_hi = src_desc->addr_hi;
	dest_desc->addr_lo = src_desc->addr_lo;

	/* Ensure that the update to the skb happens after the physical
	 * addresses have been transferred to the new BD location.
	 */
	smp_wmb();

	src_map->data = NULL;
}

/* The RX ring scheme is composed of multiple rings which post fresh
 * buffers to the chip, and one special ring the chip uses to report
 * status back to the host.
 *
 * The special ring reports the status of received packets to the
 * host.  The chip does not write into the original descriptor the
 * RX buffer was obtained from.  The chip simply takes the original
 * descriptor as provided by the host, updates the status and length
 * field, then writes this into the next status ring entry.
 *
 * Each ring the host uses to post buffers to the chip is described
 * by a TG3_BDINFO entry in the chips SRAM area.  When a packet arrives,
 * it is first placed into the on-chip ram.  When the packet's length
 * is known, it walks down the TG3_BDINFO entries to select the ring.
 * Each TG3_BDINFO specifies a MAXLEN field and the first TG3_BDINFO
 * which is within the range of the new packet's length is chosen.
 *
 * The "separate ring for rx status" scheme may sound queer, but it makes
 * sense from a cache coherency perspective.  If only the host writes
 * to the buffer post rings, and only the chip writes to the rx status
 * rings, then cache lines never move beyond shared-modified state.
 * If both the host and chip were to write into the same ring, cache line
 * eviction could occur since both entities want it in an exclusive state.
 */
static int tg3_rx(struct tg3_napi *tnapi, int budget)
{
	struct tg3 *tp = tnapi->tp;
	u32 work_mask, rx_std_posted = 0;
	u32 std_prod_idx, jmb_prod_idx;
	u32 sw_idx = tnapi->rx_rcb_ptr;
	u16 hw_idx;
	int received;
	struct tg3_rx_prodring_set *tpr = &tnapi->prodring;

	hw_idx = *(tnapi->rx_rcb_prod_idx);
	/*
	 * We need to order the read of hw_idx and the read of
	 * the opaque cookie.
	 */
	rmb();
	work_mask = 0;
	received = 0;
	std_prod_idx = tpr->rx_std_prod_idx;
	jmb_prod_idx = tpr->rx_jmb_prod_idx;
	while (sw_idx != hw_idx && budget > 0) {
		struct ring_info *ri;
		struct tg3_rx_buffer_desc *desc = &tnapi->rx_rcb[sw_idx];
		unsigned int len;
		struct sk_buff *skb;
		dma_addr_t dma_addr;
		u32 opaque_key, desc_idx, *post_ptr;
		u8 *data;
		u64 tstamp = 0;

		desc_idx = desc->opaque & RXD_OPAQUE_INDEX_MASK;
		opaque_key = desc->opaque & RXD_OPAQUE_RING_MASK;
		if (opaque_key == RXD_OPAQUE_RING_STD) {
			ri = &tp->napi[0].prodring.rx_std_buffers[desc_idx];
			dma_addr = dma_unmap_addr(ri, mapping);
			data = ri->data;
			post_ptr = &std_prod_idx;
			rx_std_posted++;
		} else if (opaque_key == RXD_OPAQUE_RING_JUMBO) {
			ri = &tp->napi[0].prodring.rx_jmb_buffers[desc_idx];
			dma_addr = dma_unmap_addr(ri, mapping);
			data = ri->data;
			post_ptr = &jmb_prod_idx;
		} else
			goto next_pkt_nopost;

		work_mask |= opaque_key;

		if (desc->err_vlan & RXD_ERR_MASK) {
		drop_it:
			tg3_recycle_rx(tnapi, tpr, opaque_key,
				       desc_idx, *post_ptr);
		drop_it_no_recycle:
			/* Other statistics kept track of by card. */
			tp->rx_dropped++;
			goto next_pkt;
		}

		prefetch(data + TG3_RX_OFFSET(tp));
		len = ((desc->idx_len & RXD_LEN_MASK) >> RXD_LEN_SHIFT) -
		      ETH_FCS_LEN;

		if ((desc->type_flags & RXD_FLAG_PTPSTAT_MASK) ==
		     RXD_FLAG_PTPSTAT_PTPV1 ||
		    (desc->type_flags & RXD_FLAG_PTPSTAT_MASK) ==
		     RXD_FLAG_PTPSTAT_PTPV2) {
			tstamp = tr32(TG3_RX_TSTAMP_LSB);
			tstamp |= (u64)tr32(TG3_RX_TSTAMP_MSB) << 32;
		}

		if (len > TG3_RX_COPY_THRESH(tp)) {
			int skb_size;
			unsigned int frag_size;

			skb_size = tg3_alloc_rx_data(tp, tpr, opaque_key,
						    *post_ptr, &frag_size);
			if (skb_size < 0)
				goto drop_it;

			pci_unmap_single(tp->pdev, dma_addr, skb_size,
					 PCI_DMA_FROMDEVICE);

			/* Ensure that the update to the data happens
			 * after the usage of the old DMA mapping.
			 */
			smp_wmb();

			ri->data = NULL;

			skb = build_skb(data, frag_size);
			if (!skb) {
				tg3_frag_free(frag_size != 0, data);
				goto drop_it_no_recycle;
			}
			skb_reserve(skb, TG3_RX_OFFSET(tp));
		} else {
			tg3_recycle_rx(tnapi, tpr, opaque_key,
				       desc_idx, *post_ptr);

			skb = netdev_alloc_skb(tp->dev,
					       len + TG3_RAW_IP_ALIGN);
			if (skb == NULL)
				goto drop_it_no_recycle;

			skb_reserve(skb, TG3_RAW_IP_ALIGN);
			pci_dma_sync_single_for_cpu(tp->pdev, dma_addr, len, PCI_DMA_FROMDEVICE);
			memcpy(skb->data,
			       data + TG3_RX_OFFSET(tp),
			       len);
			pci_dma_sync_single_for_device(tp->pdev, dma_addr, len, PCI_DMA_FROMDEVICE);
		}

		skb_put(skb, len);
		if (tstamp)
			tg3_hwclock_to_timestamp(tp, tstamp,
						 skb_hwtstamps(skb));

		if ((tp->dev->features & NETIF_F_RXCSUM) &&
		    (desc->type_flags & RXD_FLAG_TCPUDP_CSUM) &&
		    (((desc->ip_tcp_csum & RXD_TCPCSUM_MASK)
		      >> RXD_TCPCSUM_SHIFT) == 0xffff))
			skb->ip_summed = CHECKSUM_UNNECESSARY;
		else
			skb_checksum_none_assert(skb);

		skb->protocol = eth_type_trans(skb, tp->dev);

		if (len > (tp->dev->mtu + ETH_HLEN) &&
		    skb->protocol != htons(ETH_P_8021Q) &&
		    skb->protocol != htons(ETH_P_8021AD)) {
			dev_kfree_skb(skb);
			goto drop_it_no_recycle;
		}

		if (desc->type_flags & RXD_FLAG_VLAN &&
		    !(tp->rx_mode & RX_MODE_KEEP_VLAN_TAG))
			__vlan_hwaccel_put_tag(skb, htons(ETH_P_8021Q),
					       desc->err_vlan & RXD_VLAN_MASK);

		napi_gro_receive(&tnapi->napi, skb);

		received++;
		budget--;

next_pkt:
		(*post_ptr)++;

		if (unlikely(rx_std_posted >= tp->rx_std_max_post)) {
			tpr->rx_std_prod_idx = std_prod_idx &
					       tp->rx_std_ring_mask;
			tw32_rx_mbox(TG3_RX_STD_PROD_IDX_REG,
				     tpr->rx_std_prod_idx);
			work_mask &= ~RXD_OPAQUE_RING_STD;
			rx_std_posted = 0;
		}
next_pkt_nopost:
		sw_idx++;
		sw_idx &= tp->rx_ret_ring_mask;

		/* Refresh hw_idx to see if there is new work */
		if (sw_idx == hw_idx) {
			hw_idx = *(tnapi->rx_rcb_prod_idx);
			rmb();
		}
	}

	/* ACK the status ring. */
	tnapi->rx_rcb_ptr = sw_idx;
	tw32_rx_mbox(tnapi->consmbox, sw_idx);

	/* Refill RX ring(s). */
	if (!tg3_flag(tp, ENABLE_RSS)) {
		/* Sync BD data before updating mailbox */
		wmb();

		if (work_mask & RXD_OPAQUE_RING_STD) {
			tpr->rx_std_prod_idx = std_prod_idx &
					       tp->rx_std_ring_mask;
			tw32_rx_mbox(TG3_RX_STD_PROD_IDX_REG,
				     tpr->rx_std_prod_idx);
		}
		if (work_mask & RXD_OPAQUE_RING_JUMBO) {
			tpr->rx_jmb_prod_idx = jmb_prod_idx &
					       tp->rx_jmb_ring_mask;
			tw32_rx_mbox(TG3_RX_JMB_PROD_IDX_REG,
				     tpr->rx_jmb_prod_idx);
		}
		mmiowb();
	} else if (work_mask) {
		/* rx_std_buffers[] and rx_jmb_buffers[] entries must be
		 * updated before the producer indices can be updated.
		 */
		smp_wmb();

		tpr->rx_std_prod_idx = std_prod_idx & tp->rx_std_ring_mask;
		tpr->rx_jmb_prod_idx = jmb_prod_idx & tp->rx_jmb_ring_mask;

		if (tnapi != &tp->napi[1]) {
			tp->rx_refill = true;
			napi_schedule(&tp->napi[1].napi);
		}
	}

	return received;
}

static void tg3_poll_link(struct tg3 *tp)
{
	/* handle link change and other phy events */
	if (!(tg3_flag(tp, USE_LINKCHG_REG) || tg3_flag(tp, POLL_SERDES))) {
		struct tg3_hw_status *sblk = tp->napi[0].hw_status;

		if (sblk->status & SD_STATUS_LINK_CHG) {
			sblk->status = SD_STATUS_UPDATED |
				       (sblk->status & ~SD_STATUS_LINK_CHG);
			spin_lock(&tp->lock);
			if (tg3_flag(tp, USE_PHYLIB)) {
				tw32_f(MAC_STATUS,
				     (MAC_STATUS_SYNC_CHANGED |
				      MAC_STATUS_CFG_CHANGED |
				      MAC_STATUS_MI_COMPLETION |
				      MAC_STATUS_LNKSTATE_CHANGED));
				udelay(40);
			} else
				tg3_setup_phy(tp, false);
			spin_unlock(&tp->lock);
		}
	}
}

static int tg3_rx_prodring_xfer(struct tg3 *tp,
				struct tg3_rx_prodring_set *dpr,
				struct tg3_rx_prodring_set *spr)
{
	u32 si, di, cpycnt, src_prod_idx;
	int i, err = 0;

	while (1) {
		src_prod_idx = spr->rx_std_prod_idx;

		/* Make sure updates to the rx_std_buffers[] entries and the
		 * standard producer index are seen in the correct order.
		 */
		smp_rmb();

		if (spr->rx_std_cons_idx == src_prod_idx)
			break;

		if (spr->rx_std_cons_idx < src_prod_idx)
			cpycnt = src_prod_idx - spr->rx_std_cons_idx;
		else
			cpycnt = tp->rx_std_ring_mask + 1 -
				 spr->rx_std_cons_idx;

		cpycnt = min(cpycnt,
			     tp->rx_std_ring_mask + 1 - dpr->rx_std_prod_idx);

		si = spr->rx_std_cons_idx;
		di = dpr->rx_std_prod_idx;

		for (i = di; i < di + cpycnt; i++) {
			if (dpr->rx_std_buffers[i].data) {
				cpycnt = i - di;
				err = -ENOSPC;
				break;
			}
		}

		if (!cpycnt)
			break;

		/* Ensure that updates to the rx_std_buffers ring and the
		 * shadowed hardware producer ring from tg3_recycle_skb() are
		 * ordered correctly WRT the skb check above.
		 */
		smp_rmb();

		memcpy(&dpr->rx_std_buffers[di],
		       &spr->rx_std_buffers[si],
		       cpycnt * sizeof(struct ring_info));

		for (i = 0; i < cpycnt; i++, di++, si++) {
			struct tg3_rx_buffer_desc *sbd, *dbd;
			sbd = &spr->rx_std[si];
			dbd = &dpr->rx_std[di];
			dbd->addr_hi = sbd->addr_hi;
			dbd->addr_lo = sbd->addr_lo;
		}

		spr->rx_std_cons_idx = (spr->rx_std_cons_idx + cpycnt) &
				       tp->rx_std_ring_mask;
		dpr->rx_std_prod_idx = (dpr->rx_std_prod_idx + cpycnt) &
				       tp->rx_std_ring_mask;
	}

	while (1) {
		src_prod_idx = spr->rx_jmb_prod_idx;

		/* Make sure updates to the rx_jmb_buffers[] entries and
		 * the jumbo producer index are seen in the correct order.
		 */
		smp_rmb();

		if (spr->rx_jmb_cons_idx == src_prod_idx)
			break;

		if (spr->rx_jmb_cons_idx < src_prod_idx)
			cpycnt = src_prod_idx - spr->rx_jmb_cons_idx;
		else
			cpycnt = tp->rx_jmb_ring_mask + 1 -
				 spr->rx_jmb_cons_idx;

		cpycnt = min(cpycnt,
			     tp->rx_jmb_ring_mask + 1 - dpr->rx_jmb_prod_idx);

		si = spr->rx_jmb_cons_idx;
		di = dpr->rx_jmb_prod_idx;

		for (i = di; i < di + cpycnt; i++) {
			if (dpr->rx_jmb_buffers[i].data) {
				cpycnt = i - di;
				err = -ENOSPC;
				break;
			}
		}

		if (!cpycnt)
			break;

		/* Ensure that updates to the rx_jmb_buffers ring and the
		 * shadowed hardware producer ring from tg3_recycle_skb() are
		 * ordered correctly WRT the skb check above.
		 */
		smp_rmb();

		memcpy(&dpr->rx_jmb_buffers[di],
		       &spr->rx_jmb_buffers[si],
		       cpycnt * sizeof(struct ring_info));

		for (i = 0; i < cpycnt; i++, di++, si++) {
			struct tg3_rx_buffer_desc *sbd, *dbd;
			sbd = &spr->rx_jmb[si].std;
			dbd = &dpr->rx_jmb[di].std;
			dbd->addr_hi = sbd->addr_hi;
			dbd->addr_lo = sbd->addr_lo;
		}

		spr->rx_jmb_cons_idx = (spr->rx_jmb_cons_idx + cpycnt) &
				       tp->rx_jmb_ring_mask;
		dpr->rx_jmb_prod_idx = (dpr->rx_jmb_prod_idx + cpycnt) &
				       tp->rx_jmb_ring_mask;
	}

	return err;
}

static int tg3_poll_work(struct tg3_napi *tnapi, int work_done, int budget)
{
	struct tg3 *tp = tnapi->tp;

	/* run TX completion thread */
	if (tnapi->hw_status->idx[0].tx_consumer != tnapi->tx_cons) {
		tg3_tx(tnapi);
		if (unlikely(tg3_flag(tp, TX_RECOVERY_PENDING)))
			return work_done;
	}

	if (!tnapi->rx_rcb_prod_idx)
		return work_done;

	/* run RX thread, within the bounds set by NAPI.
	 * All RX "locking" is done by ensuring outside
	 * code synchronizes with tg3->napi.poll()
	 */
	if (*(tnapi->rx_rcb_prod_idx) != tnapi->rx_rcb_ptr)
		work_done += tg3_rx(tnapi, budget - work_done);

	if (tg3_flag(tp, ENABLE_RSS) && tnapi == &tp->napi[1]) {
		struct tg3_rx_prodring_set *dpr = &tp->napi[0].prodring;
		int i, err = 0;
		u32 std_prod_idx = dpr->rx_std_prod_idx;
		u32 jmb_prod_idx = dpr->rx_jmb_prod_idx;

		tp->rx_refill = false;
		for (i = 1; i <= tp->rxq_cnt; i++)
			err |= tg3_rx_prodring_xfer(tp, dpr,
						    &tp->napi[i].prodring);

		wmb();

		if (std_prod_idx != dpr->rx_std_prod_idx)
			tw32_rx_mbox(TG3_RX_STD_PROD_IDX_REG,
				     dpr->rx_std_prod_idx);

		if (jmb_prod_idx != dpr->rx_jmb_prod_idx)
			tw32_rx_mbox(TG3_RX_JMB_PROD_IDX_REG,
				     dpr->rx_jmb_prod_idx);

		mmiowb();

		if (err)
			tw32_f(HOSTCC_MODE, tp->coal_now);
	}

	return work_done;
}

static inline void tg3_reset_task_schedule(struct tg3 *tp)
{
	if (!test_and_set_bit(TG3_FLAG_RESET_TASK_PENDING, tp->tg3_flags))
		schedule_work(&tp->reset_task);
}

static inline void tg3_reset_task_cancel(struct tg3 *tp)
{
	cancel_work_sync(&tp->reset_task);
	tg3_flag_clear(tp, RESET_TASK_PENDING);
	tg3_flag_clear(tp, TX_RECOVERY_PENDING);
}

static int tg3_poll_msix(struct napi_struct *napi, int budget)
{
	struct tg3_napi *tnapi = container_of(napi, struct tg3_napi, napi);
	struct tg3 *tp = tnapi->tp;
	int work_done = 0;
	struct tg3_hw_status *sblk = tnapi->hw_status;

	while (1) {
		work_done = tg3_poll_work(tnapi, work_done, budget);

		if (unlikely(tg3_flag(tp, TX_RECOVERY_PENDING)))
			goto tx_recovery;

		if (unlikely(work_done >= budget))
			break;

		/* tp->last_tag is used in tg3_int_reenable() below
		 * to tell the hw how much work has been processed,
		 * so we must read it before checking for more work.
		 */
		tnapi->last_tag = sblk->status_tag;
		tnapi->last_irq_tag = tnapi->last_tag;
		rmb();

		/* check for RX/TX work to do */
		if (likely(sblk->idx[0].tx_consumer == tnapi->tx_cons &&
			   *(tnapi->rx_rcb_prod_idx) == tnapi->rx_rcb_ptr)) {

			/* This test here is not race free, but will reduce
			 * the number of interrupts by looping again.
			 */
			if (tnapi == &tp->napi[1] && tp->rx_refill)
				continue;

			napi_complete(napi);
			/* Reenable interrupts. */
			tw32_mailbox(tnapi->int_mbox, tnapi->last_tag << 24);

			/* This test here is synchronized by napi_schedule()
			 * and napi_complete() to close the race condition.
			 */
			if (unlikely(tnapi == &tp->napi[1] && tp->rx_refill)) {
				tw32(HOSTCC_MODE, tp->coalesce_mode |
						  HOSTCC_MODE_ENABLE |
						  tnapi->coal_now);
			}
			mmiowb();
			break;
		}
	}

	return work_done;

tx_recovery:
	/* work_done is guaranteed to be less than budget. */
	napi_complete(napi);
	tg3_reset_task_schedule(tp);
	return work_done;
}

static void tg3_process_error(struct tg3 *tp)
{
	u32 val;
	bool real_error = false;

	if (tg3_flag(tp, ERROR_PROCESSED))
		return;

	/* Check Flow Attention register */
	val = tr32(HOSTCC_FLOW_ATTN);
	if (val & ~HOSTCC_FLOW_ATTN_MBUF_LWM) {
		netdev_err(tp->dev, "FLOW Attention error.  Resetting chip.\n");
		real_error = true;
	}

	if (tr32(MSGINT_STATUS) & ~MSGINT_STATUS_MSI_REQ) {
		netdev_err(tp->dev, "MSI Status error.  Resetting chip.\n");
		real_error = true;
	}

	if (tr32(RDMAC_STATUS) || tr32(WDMAC_STATUS)) {
		netdev_err(tp->dev, "DMA Status error.  Resetting chip.\n");
		real_error = true;
	}

	if (!real_error)
		return;

	tg3_dump_state(tp);

	tg3_flag_set(tp, ERROR_PROCESSED);
	tg3_reset_task_schedule(tp);
}

static int tg3_poll(struct napi_struct *napi, int budget)
{
	struct tg3_napi *tnapi = container_of(napi, struct tg3_napi, napi);
	struct tg3 *tp = tnapi->tp;
	int work_done = 0;
	struct tg3_hw_status *sblk = tnapi->hw_status;

	while (1) {
		if (sblk->status & SD_STATUS_ERROR)
			tg3_process_error(tp);

		tg3_poll_link(tp);

		work_done = tg3_poll_work(tnapi, work_done, budget);

		if (unlikely(tg3_flag(tp, TX_RECOVERY_PENDING)))
			goto tx_recovery;

		if (unlikely(work_done >= budget))
			break;

		if (tg3_flag(tp, TAGGED_STATUS)) {
			/* tp->last_tag is used in tg3_int_reenable() below
			 * to tell the hw how much work has been processed,
			 * so we must read it before checking for more work.
			 */
			tnapi->last_tag = sblk->status_tag;
			tnapi->last_irq_tag = tnapi->last_tag;
			rmb();
		} else
			sblk->status &= ~SD_STATUS_UPDATED;

		if (likely(!tg3_has_work(tnapi))) {
			napi_complete(napi);
			tg3_int_reenable(tnapi);
			break;
		}
	}

	return work_done;

tx_recovery:
	/* work_done is guaranteed to be less than budget. */
	napi_complete(napi);
	tg3_reset_task_schedule(tp);
	return work_done;
}

static void tg3_napi_disable(struct tg3 *tp)
{
	int i;

	for (i = tp->irq_cnt - 1; i >= 0; i--)
		napi_disable(&tp->napi[i].napi);
}

static void tg3_napi_enable(struct tg3 *tp)
{
	int i;

	for (i = 0; i < tp->irq_cnt; i++)
		napi_enable(&tp->napi[i].napi);
}

static void tg3_napi_init(struct tg3 *tp)
{
	int i;

	netif_napi_add(tp->dev, &tp->napi[0].napi, tg3_poll, 64);
	for (i = 1; i < tp->irq_cnt; i++)
		netif_napi_add(tp->dev, &tp->napi[i].napi, tg3_poll_msix, 64);
}

static void tg3_napi_fini(struct tg3 *tp)
{
	int i;

	for (i = 0; i < tp->irq_cnt; i++)
		netif_napi_del(&tp->napi[i].napi);
}

static inline void tg3_netif_stop(struct tg3 *tp)
{
	tp->dev->trans_start = jiffies;	/* prevent tx timeout */
	tg3_napi_disable(tp);
	netif_carrier_off(tp->dev);
	netif_tx_disable(tp->dev);
}

/* tp->lock must be held */
static inline void tg3_netif_start(struct tg3 *tp)
{
	tg3_ptp_resume(tp);

	/* NOTE: unconditional netif_tx_wake_all_queues is only
	 * appropriate so long as all callers are assured to
	 * have free tx slots (such as after tg3_init_hw)
	 */
	netif_tx_wake_all_queues(tp->dev);

	if (tp->link_up)
		netif_carrier_on(tp->dev);

	tg3_napi_enable(tp);
	tp->napi[0].hw_status->status |= SD_STATUS_UPDATED;
	tg3_enable_ints(tp);
}

static void tg3_irq_quiesce(struct tg3 *tp)
{
	int i;

	BUG_ON(tp->irq_sync);

	tp->irq_sync = 1;
	smp_mb();

	for (i = 0; i < tp->irq_cnt; i++)
		synchronize_irq(tp->napi[i].irq_vec);
}

/* Fully shutdown all tg3 driver activity elsewhere in the system.
 * If irq_sync is non-zero, then the IRQ handler must be synchronized
 * with as well.  Most of the time, this is not necessary except when
 * shutting down the device.
 */
static inline void tg3_full_lock(struct tg3 *tp, int irq_sync)
{
	spin_lock_bh(&tp->lock);
	if (irq_sync)
		tg3_irq_quiesce(tp);
}

static inline void tg3_full_unlock(struct tg3 *tp)
{
	spin_unlock_bh(&tp->lock);
}

/* One-shot MSI handler - Chip automatically disables interrupt
 * after sending MSI so driver doesn't have to do it.
 */
static irqreturn_t tg3_msi_1shot(int irq, void *dev_id)
{
	struct tg3_napi *tnapi = dev_id;
	struct tg3 *tp = tnapi->tp;

	prefetch(tnapi->hw_status);
	if (tnapi->rx_rcb)
		prefetch(&tnapi->rx_rcb[tnapi->rx_rcb_ptr]);

	if (likely(!tg3_irq_sync(tp)))
		napi_schedule(&tnapi->napi);

	return IRQ_HANDLED;
}

/* MSI ISR - No need to check for interrupt sharing and no need to
 * flush status block and interrupt mailbox. PCI ordering rules
 * guarantee that MSI will arrive after the status block.
 */
static irqreturn_t tg3_msi(int irq, void *dev_id)
{
	struct tg3_napi *tnapi = dev_id;
	struct tg3 *tp = tnapi->tp;

	prefetch(tnapi->hw_status);
	if (tnapi->rx_rcb)
		prefetch(&tnapi->rx_rcb[tnapi->rx_rcb_ptr]);
	/*
	 * Writing any value to intr-mbox-0 clears PCI INTA# and
	 * chip-internal interrupt pending events.
	 * Writing non-zero to intr-mbox-0 additional tells the
	 * NIC to stop sending us irqs, engaging "in-intr-handler"
	 * event coalescing.
	 */
	tw32_mailbox(tnapi->int_mbox, 0x00000001);
	if (likely(!tg3_irq_sync(tp)))
		napi_schedule(&tnapi->napi);

	return IRQ_RETVAL(1);
}

static irqreturn_t tg3_interrupt(int irq, void *dev_id)
{
	struct tg3_napi *tnapi = dev_id;
	struct tg3 *tp = tnapi->tp;
	struct tg3_hw_status *sblk = tnapi->hw_status;
	unsigned int handled = 1;

	/* In INTx mode, it is possible for the interrupt to arrive at
	 * the CPU before the status block posted prior to the interrupt.
	 * Reading the PCI State register will confirm whether the
	 * interrupt is ours and will flush the status block.
	 */
	if (unlikely(!(sblk->status & SD_STATUS_UPDATED))) {
		if (tg3_flag(tp, CHIP_RESETTING) ||
		    (tr32(TG3PCI_PCISTATE) & PCISTATE_INT_NOT_ACTIVE)) {
			handled = 0;
			goto out;
		}
	}

	/*
	 * Writing any value to intr-mbox-0 clears PCI INTA# and
	 * chip-internal interrupt pending events.
	 * Writing non-zero to intr-mbox-0 additional tells the
	 * NIC to stop sending us irqs, engaging "in-intr-handler"
	 * event coalescing.
	 *
	 * Flush the mailbox to de-assert the IRQ immediately to prevent
	 * spurious interrupts.  The flush impacts performance but
	 * excessive spurious interrupts can be worse in some cases.
	 */
	tw32_mailbox_f(MAILBOX_INTERRUPT_0 + TG3_64BIT_REG_LOW, 0x00000001);
	if (tg3_irq_sync(tp))
		goto out;
	sblk->status &= ~SD_STATUS_UPDATED;
	if (likely(tg3_has_work(tnapi))) {
		prefetch(&tnapi->rx_rcb[tnapi->rx_rcb_ptr]);
		napi_schedule(&tnapi->napi);
	} else {
		/* No work, shared interrupt perhaps?  re-enable
		 * interrupts, and flush that PCI write
		 */
		tw32_mailbox_f(MAILBOX_INTERRUPT_0 + TG3_64BIT_REG_LOW,
			       0x00000000);
	}
out:
	return IRQ_RETVAL(handled);
}

static irqreturn_t tg3_interrupt_tagged(int irq, void *dev_id)
{
	struct tg3_napi *tnapi = dev_id;
	struct tg3 *tp = tnapi->tp;
	struct tg3_hw_status *sblk = tnapi->hw_status;
	unsigned int handled = 1;

	/* In INTx mode, it is possible for the interrupt to arrive at
	 * the CPU before the status block posted prior to the interrupt.
	 * Reading the PCI State register will confirm whether the
	 * interrupt is ours and will flush the status block.
	 */
	if (unlikely(sblk->status_tag == tnapi->last_irq_tag)) {
		if (tg3_flag(tp, CHIP_RESETTING) ||
		    (tr32(TG3PCI_PCISTATE) & PCISTATE_INT_NOT_ACTIVE)) {
			handled = 0;
			goto out;
		}
	}

	/*
	 * writing any value to intr-mbox-0 clears PCI INTA# and
	 * chip-internal interrupt pending events.
	 * writing non-zero to intr-mbox-0 additional tells the
	 * NIC to stop sending us irqs, engaging "in-intr-handler"
	 * event coalescing.
	 *
	 * Flush the mailbox to de-assert the IRQ immediately to prevent
	 * spurious interrupts.  The flush impacts performance but
	 * excessive spurious interrupts can be worse in some cases.
	 */
	tw32_mailbox_f(MAILBOX_INTERRUPT_0 + TG3_64BIT_REG_LOW, 0x00000001);

	/*
	 * In a shared interrupt configuration, sometimes other devices'
	 * interrupts will scream.  We record the current status tag here
	 * so that the above check can report that the screaming interrupts
	 * are unhandled.  Eventually they will be silenced.
	 */
	tnapi->last_irq_tag = sblk->status_tag;

	if (tg3_irq_sync(tp))
		goto out;

	prefetch(&tnapi->rx_rcb[tnapi->rx_rcb_ptr]);

	napi_schedule(&tnapi->napi);

out:
	return IRQ_RETVAL(handled);
}

/* ISR for interrupt test */
static irqreturn_t tg3_test_isr(int irq, void *dev_id)
{
	struct tg3_napi *tnapi = dev_id;
	struct tg3 *tp = tnapi->tp;
	struct tg3_hw_status *sblk = tnapi->hw_status;

	if ((sblk->status & SD_STATUS_UPDATED) ||
	    !(tr32(TG3PCI_PCISTATE) & PCISTATE_INT_NOT_ACTIVE)) {
		tg3_disable_ints(tp);
		return IRQ_RETVAL(1);
	}
	return IRQ_RETVAL(0);
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void tg3_poll_controller(struct net_device *dev)
{
	int i;
	struct tg3 *tp = netdev_priv(dev);

	if (tg3_irq_sync(tp))
		return;

	for (i = 0; i < tp->irq_cnt; i++)
		tg3_interrupt(tp->napi[i].irq_vec, &tp->napi[i]);
}
#endif

static void tg3_tx_timeout(struct net_device *dev)
{
	struct tg3 *tp = netdev_priv(dev);

	if (netif_msg_tx_err(tp)) {
		netdev_err(dev, "transmit timed out, resetting\n");
		tg3_dump_state(tp);
	}

	tg3_reset_task_schedule(tp);
}

/* Test for DMA buffers crossing any 4GB boundaries: 4G, 8G, etc */
static inline int tg3_4g_overflow_test(dma_addr_t mapping, int len)
{
	u32 base = (u32) mapping & 0xffffffff;

	return base + len + 8 < base;
}

/* Test for TSO DMA buffers that cross into regions which are within MSS bytes
 * of any 4GB boundaries: 4G, 8G, etc
 */
static inline int tg3_4g_tso_overflow_test(struct tg3 *tp, dma_addr_t mapping,
					   u32 len, u32 mss)
{
	if (tg3_asic_rev(tp) == ASIC_REV_5762 && mss) {
		u32 base = (u32) mapping & 0xffffffff;

		return ((base + len + (mss & 0x3fff)) < base);
	}
	return 0;
}

/* Test for DMA addresses > 40-bit */
static inline int tg3_40bit_overflow_test(struct tg3 *tp, dma_addr_t mapping,
					  int len)
{
#if defined(CONFIG_HIGHMEM) && (BITS_PER_LONG == 64)
	if (tg3_flag(tp, 40BIT_DMA_BUG))
		return ((u64) mapping + len) > DMA_BIT_MASK(40);
	return 0;
#else
	return 0;
#endif
}

static inline void tg3_tx_set_bd(struct tg3_tx_buffer_desc *txbd,
				 dma_addr_t mapping, u32 len, u32 flags,
				 u32 mss, u32 vlan)
{
	txbd->addr_hi = ((u64) mapping >> 32);
	txbd->addr_lo = ((u64) mapping & 0xffffffff);
	txbd->len_flags = (len << TXD_LEN_SHIFT) | (flags & 0x0000ffff);
	txbd->vlan_tag = (mss << TXD_MSS_SHIFT) | (vlan << TXD_VLAN_TAG_SHIFT);
}

static bool tg3_tx_frag_set(struct tg3_napi *tnapi, u32 *entry, u32 *budget,
			    dma_addr_t map, u32 len, u32 flags,
			    u32 mss, u32 vlan)
{
	struct tg3 *tp = tnapi->tp;
	bool hwbug = false;

	if (tg3_flag(tp, SHORT_DMA_BUG) && len <= 8)
		hwbug = true;

	if (tg3_4g_overflow_test(map, len))
		hwbug = true;

	if (tg3_4g_tso_overflow_test(tp, map, len, mss))
		hwbug = true;

	if (tg3_40bit_overflow_test(tp, map, len))
		hwbug = true;

	if (tp->dma_limit) {
		u32 prvidx = *entry;
		u32 tmp_flag = flags & ~TXD_FLAG_END;
		while (len > tp->dma_limit && *budget) {
			u32 frag_len = tp->dma_limit;
			len -= tp->dma_limit;

			/* Avoid the 8byte DMA problem */
			if (len <= 8) {
				len += tp->dma_limit / 2;
				frag_len = tp->dma_limit / 2;
			}

			tnapi->tx_buffers[*entry].fragmented = true;

			tg3_tx_set_bd(&tnapi->tx_ring[*entry], map,
				      frag_len, tmp_flag, mss, vlan);
			*budget -= 1;
			prvidx = *entry;
			*entry = NEXT_TX(*entry);

			map += frag_len;
		}

		if (len) {
			if (*budget) {
				tg3_tx_set_bd(&tnapi->tx_ring[*entry], map,
					      len, flags, mss, vlan);
				*budget -= 1;
				*entry = NEXT_TX(*entry);
			} else {
				hwbug = true;
				tnapi->tx_buffers[prvidx].fragmented = false;
			}
		}
	} else {
		tg3_tx_set_bd(&tnapi->tx_ring[*entry], map,
			      len, flags, mss, vlan);
		*entry = NEXT_TX(*entry);
	}

	return hwbug;
}

static void tg3_tx_skb_unmap(struct tg3_napi *tnapi, u32 entry, int last)
{
	int i;
	struct sk_buff *skb;
	struct tg3_tx_ring_info *txb = &tnapi->tx_buffers[entry];

	skb = txb->skb;
	txb->skb = NULL;

	pci_unmap_single(tnapi->tp->pdev,
			 dma_unmap_addr(txb, mapping),
			 skb_headlen(skb),
			 PCI_DMA_TODEVICE);

	while (txb->fragmented) {
		txb->fragmented = false;
		entry = NEXT_TX(entry);
		txb = &tnapi->tx_buffers[entry];
	}

	for (i = 0; i <= last; i++) {
		const skb_frag_t *frag = &skb_shinfo(skb)->frags[i];

		entry = NEXT_TX(entry);
		txb = &tnapi->tx_buffers[entry];

		pci_unmap_page(tnapi->tp->pdev,
			       dma_unmap_addr(txb, mapping),
			       skb_frag_size(frag), PCI_DMA_TODEVICE);

		while (txb->fragmented) {
			txb->fragmented = false;
			entry = NEXT_TX(entry);
			txb = &tnapi->tx_buffers[entry];
		}
	}
}

/* Workaround 4GB and 40-bit hardware DMA bugs. */
static int tigon3_dma_hwbug_workaround(struct tg3_napi *tnapi,
				       struct sk_buff **pskb,
				       u32 *entry, u32 *budget,
				       u32 base_flags, u32 mss, u32 vlan)
{
	struct tg3 *tp = tnapi->tp;
	struct sk_buff *new_skb, *skb = *pskb;
	dma_addr_t new_addr = 0;
	int ret = 0;

	if (tg3_asic_rev(tp) != ASIC_REV_5701)
		new_skb = skb_copy(skb, GFP_ATOMIC);
	else {
		int more_headroom = 4 - ((unsigned long)skb->data & 3);

		new_skb = skb_copy_expand(skb,
					  skb_headroom(skb) + more_headroom,
					  skb_tailroom(skb), GFP_ATOMIC);
	}

	if (!new_skb) {
		ret = -1;
	} else {
		/* New SKB is guaranteed to be linear. */
		new_addr = pci_map_single(tp->pdev, new_skb->data, new_skb->len,
					  PCI_DMA_TODEVICE);
		/* Make sure the mapping succeeded */
		if (pci_dma_mapping_error(tp->pdev, new_addr)) {
			dev_kfree_skb(new_skb);
			ret = -1;
		} else {
			u32 save_entry = *entry;

			base_flags |= TXD_FLAG_END;

			tnapi->tx_buffers[*entry].skb = new_skb;
			dma_unmap_addr_set(&tnapi->tx_buffers[*entry],
					   mapping, new_addr);

			if (tg3_tx_frag_set(tnapi, entry, budget, new_addr,
					    new_skb->len, base_flags,
					    mss, vlan)) {
				tg3_tx_skb_unmap(tnapi, save_entry, -1);
				dev_kfree_skb(new_skb);
				ret = -1;
			}
		}
	}

	dev_kfree_skb(skb);
	*pskb = new_skb;
	return ret;
}

static netdev_tx_t tg3_start_xmit(struct sk_buff *, struct net_device *);

/* Use GSO to workaround a rare TSO bug that may be triggered when the
 * TSO header is greater than 80 bytes.
 */
static int tg3_tso_bug(struct tg3 *tp, struct sk_buff *skb)
{
	struct sk_buff *segs, *nskb;
	u32 frag_cnt_est = skb_shinfo(skb)->gso_segs * 3;

	/* Estimate the number of fragments in the worst case */
	if (unlikely(tg3_tx_avail(&tp->napi[0]) <= frag_cnt_est)) {
		netif_stop_queue(tp->dev);

		/* netif_tx_stop_queue() must be done before checking
		 * checking tx index in tg3_tx_avail() below, because in
		 * tg3_tx(), we update tx index before checking for
		 * netif_tx_queue_stopped().
		 */
		smp_mb();
		if (tg3_tx_avail(&tp->napi[0]) <= frag_cnt_est)
			return NETDEV_TX_BUSY;

		netif_wake_queue(tp->dev);
	}

	segs = skb_gso_segment(skb, tp->dev->features & ~NETIF_F_TSO);
	if (IS_ERR(segs))
		goto tg3_tso_bug_end;

	do {
		nskb = segs;
		segs = segs->next;
		nskb->next = NULL;
		tg3_start_xmit(nskb, tp->dev);
	} while (segs);

tg3_tso_bug_end:
	dev_kfree_skb(skb);

	return NETDEV_TX_OK;
}

/* hard_start_xmit for devices that have the 4G bug and/or 40-bit bug and
 * support TG3_FLAG_HW_TSO_1 or firmware TSO only.
 */
static netdev_tx_t tg3_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct tg3 *tp = netdev_priv(dev);
	u32 len, entry, base_flags, mss, vlan = 0;
	u32 budget;
	int i = -1, would_hit_hwbug;
	dma_addr_t mapping;
	struct tg3_napi *tnapi;
	struct netdev_queue *txq;
	unsigned int last;

	txq = netdev_get_tx_queue(dev, skb_get_queue_mapping(skb));
	tnapi = &tp->napi[skb_get_queue_mapping(skb)];
	if (tg3_flag(tp, ENABLE_TSS))
		tnapi++;

	budget = tg3_tx_avail(tnapi);

	/* We are running in BH disabled context with netif_tx_lock
	 * and TX reclaim runs via tp->napi.poll inside of a software
	 * interrupt.  Furthermore, IRQ processing runs lockless so we have
	 * no IRQ context deadlocks to worry about either.  Rejoice!
	 */
	if (unlikely(budget <= (skb_shinfo(skb)->nr_frags + 1))) {
		if (!netif_tx_queue_stopped(txq)) {
			netif_tx_stop_queue(txq);

			/* This is a hard error, log it. */
			netdev_err(dev,
				   "BUG! Tx Ring full when queue awake!\n");
		}
		return NETDEV_TX_BUSY;
	}

	entry = tnapi->tx_prod;
	base_flags = 0;

	mss = skb_shinfo(skb)->gso_size;
	if (mss) {
		struct iphdr *iph;
		u32 tcp_opt_len, hdr_len;

		if (skb_header_cloned(skb) &&
		    pskb_expand_head(skb, 0, 0, GFP_ATOMIC))
			goto drop;

		iph = ip_hdr(skb);
		tcp_opt_len = tcp_optlen(skb);

		hdr_len = skb_transport_offset(skb) + tcp_hdrlen(skb) - ETH_HLEN;

		/* HW/FW can not correctly segment packets that have been
		 * vlan encapsulated.
		 */
		if (skb->protocol == htons(ETH_P_8021Q) ||
		    skb->protocol == htons(ETH_P_8021AD))
			return tg3_tso_bug(tp, skb);

		if (!skb_is_gso_v6(skb)) {
			iph->check = 0;
			iph->tot_len = htons(mss + hdr_len);
		}

		if (unlikely((ETH_HLEN + hdr_len) > 80) &&
		    tg3_flag(tp, TSO_BUG))
			return tg3_tso_bug(tp, skb);

		base_flags |= (TXD_FLAG_CPU_PRE_DMA |
			       TXD_FLAG_CPU_POST_DMA);

		if (tg3_flag(tp, HW_TSO_1) ||
		    tg3_flag(tp, HW_TSO_2) ||
		    tg3_flag(tp, HW_TSO_3)) {
			tcp_hdr(skb)->check = 0;
			base_flags &= ~TXD_FLAG_TCPUDP_CSUM;
		} else
			tcp_hdr(skb)->check = ~csum_tcpudp_magic(iph->saddr,
								 iph->daddr, 0,
								 IPPROTO_TCP,
								 0);

		if (tg3_flag(tp, HW_TSO_3)) {
			mss |= (hdr_len & 0xc) << 12;
			if (hdr_len & 0x10)
				base_flags |= 0x00000010;
			base_flags |= (hdr_len & 0x3e0) << 5;
		} else if (tg3_flag(tp, HW_TSO_2))
			mss |= hdr_len << 9;
		else if (tg3_flag(tp, HW_TSO_1) ||
			 tg3_asic_rev(tp) == ASIC_REV_5705) {
			if (tcp_opt_len || iph->ihl > 5) {
				int tsflags;

				tsflags = (iph->ihl - 5) + (tcp_opt_len >> 2);
				mss |= (tsflags << 11);
			}
		} else {
			if (tcp_opt_len || iph->ihl > 5) {
				int tsflags;

				tsflags = (iph->ihl - 5) + (tcp_opt_len >> 2);
				base_flags |= tsflags << 12;
			}
		}
	} else if (skb->ip_summed == CHECKSUM_PARTIAL) {
		/* HW/FW can not correctly checksum packets that have been
		 * vlan encapsulated.
		 */
		if (skb->protocol == htons(ETH_P_8021Q) ||
		    skb->protocol == htons(ETH_P_8021AD)) {
			if (skb_checksum_help(skb))
				goto drop;
		} else  {
			base_flags |= TXD_FLAG_TCPUDP_CSUM;
		}
	}

	if (tg3_flag(tp, USE_JUMBO_BDFLAG) &&
	    !mss && skb->len > VLAN_ETH_FRAME_LEN)
		base_flags |= TXD_FLAG_JMB_PKT;

	if (vlan_tx_tag_present(skb)) {
		base_flags |= TXD_FLAG_VLAN;
		vlan = vlan_tx_tag_get(skb);
	}

	if ((unlikely(skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP)) &&
	    tg3_flag(tp, TX_TSTAMP_EN)) {
		skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;
		base_flags |= TXD_FLAG_HWTSTAMP;
	}

	len = skb_headlen(skb);

	mapping = pci_map_single(tp->pdev, skb->data, len, PCI_DMA_TODEVICE);
	if (pci_dma_mapping_error(tp->pdev, mapping))
		goto drop;


	tnapi->tx_buffers[entry].skb = skb;
	dma_unmap_addr_set(&tnapi->tx_buffers[entry], mapping, mapping);

	would_hit_hwbug = 0;

	if (tg3_flag(tp, 5701_DMA_BUG))
		would_hit_hwbug = 1;

	if (tg3_tx_frag_set(tnapi, &entry, &budget, mapping, len, base_flags |
			  ((skb_shinfo(skb)->nr_frags == 0) ? TXD_FLAG_END : 0),
			    mss, vlan)) {
		would_hit_hwbug = 1;
	} else if (skb_shinfo(skb)->nr_frags > 0) {
		u32 tmp_mss = mss;

		if (!tg3_flag(tp, HW_TSO_1) &&
		    !tg3_flag(tp, HW_TSO_2) &&
		    !tg3_flag(tp, HW_TSO_3))
			tmp_mss = 0;

		/* Now loop through additional data
		 * fragments, and queue them.
		 */
		last = skb_shinfo(skb)->nr_frags - 1;
		for (i = 0; i <= last; i++) {
			skb_frag_t *frag = &skb_shinfo(skb)->frags[i];

			len = skb_frag_size(frag);
			mapping = skb_frag_dma_map(&tp->pdev->dev, frag, 0,
						   len, DMA_TO_DEVICE);

			tnapi->tx_buffers[entry].skb = NULL;
			dma_unmap_addr_set(&tnapi->tx_buffers[entry], mapping,
					   mapping);
			if (dma_mapping_error(&tp->pdev->dev, mapping))
				goto dma_error;

			if (!budget ||
			    tg3_tx_frag_set(tnapi, &entry, &budget, mapping,
					    len, base_flags |
					    ((i == last) ? TXD_FLAG_END : 0),
					    tmp_mss, vlan)) {
				would_hit_hwbug = 1;
				break;
			}
		}
	}

	if (would_hit_hwbug) {
		tg3_tx_skb_unmap(tnapi, tnapi->tx_prod, i);

		/* If the workaround fails due to memory/mapping
		 * failure, silently drop this packet.
		 */
		entry = tnapi->tx_prod;
		budget = tg3_tx_avail(tnapi);
		if (tigon3_dma_hwbug_workaround(tnapi, &skb, &entry, &budget,
						base_flags, mss, vlan))
			goto drop_nofree;
	}

	skb_tx_timestamp(skb);
	netdev_tx_sent_queue(txq, skb->len);

	/* Sync BD data before updating mailbox */
	wmb();

	/* Packets are ready, update Tx producer idx local and on card. */
	tw32_tx_mbox(tnapi->prodmbox, entry);

	tnapi->tx_prod = entry;
	if (unlikely(tg3_tx_avail(tnapi) <= (MAX_SKB_FRAGS + 1))) {
		netif_tx_stop_queue(txq);

		/* netif_tx_stop_queue() must be done before checking
		 * checking tx index in tg3_tx_avail() below, because in
		 * tg3_tx(), we update tx index before checking for
		 * netif_tx_queue_stopped().
		 */
		smp_mb();
		if (tg3_tx_avail(tnapi) > TG3_TX_WAKEUP_THRESH(tnapi))
			netif_tx_wake_queue(txq);
	}

	mmiowb();
	return NETDEV_TX_OK;

dma_error:
	tg3_tx_skb_unmap(tnapi, tnapi->tx_prod, --i);
	tnapi->tx_buffers[tnapi->tx_prod].skb = NULL;
drop:
	dev_kfree_skb(skb);
drop_nofree:
	tp->tx_dropped++;
	return NETDEV_TX_OK;
}

static void tg3_mac_loopback(struct tg3 *tp, bool enable)
{
	if (enable) {
		tp->mac_mode &= ~(MAC_MODE_HALF_DUPLEX |
				  MAC_MODE_PORT_MODE_MASK);

		tp->mac_mode |= MAC_MODE_PORT_INT_LPBACK;

		if (!tg3_flag(tp, 5705_PLUS))
			tp->mac_mode |= MAC_MODE_LINK_POLARITY;

		if (tp->phy_flags & TG3_PHYFLG_10_100_ONLY)
			tp->mac_mode |= MAC_MODE_PORT_MODE_MII;
		else
			tp->mac_mode |= MAC_MODE_PORT_MODE_GMII;
	} else {
		tp->mac_mode &= ~MAC_MODE_PORT_INT_LPBACK;

		if (tg3_flag(tp, 5705_PLUS) ||
		    (tp->phy_flags & TG3_PHYFLG_PHY_SERDES) ||
		    tg3_asic_rev(tp) == ASIC_REV_5700)
			tp->mac_mode &= ~MAC_MODE_LINK_POLARITY;
	}

	tw32(MAC_MODE, tp->mac_mode);
	udelay(40);
}

static int tg3_phy_lpbk_set(struct tg3 *tp, u32 speed, bool extlpbk)
{
	u32 val, bmcr, mac_mode, ptest = 0;

	tg3_phy_toggle_apd(tp, false);
	tg3_phy_toggle_automdix(tp, false);

	if (extlpbk && tg3_phy_set_extloopbk(tp))
		return -EIO;

	bmcr = BMCR_FULLDPLX;
	switch (speed) {
	case SPEED_10:
		break;
	case SPEED_100:
		bmcr |= BMCR_SPEED100;
		break;
	case SPEED_1000:
	default:
		if (tp->phy_flags & TG3_PHYFLG_IS_FET) {
			speed = SPEED_100;
			bmcr |= BMCR_SPEED100;
		} else {
			speed = SPEED_1000;
			bmcr |= BMCR_SPEED1000;
		}
	}

	if (extlpbk) {
		if (!(tp->phy_flags & TG3_PHYFLG_IS_FET)) {
			tg3_readphy(tp, MII_CTRL1000, &val);
			val |= CTL1000_AS_MASTER |
			       CTL1000_ENABLE_MASTER;
			tg3_writephy(tp, MII_CTRL1000, val);
		} else {
			ptest = MII_TG3_FET_PTEST_TRIM_SEL |
				MII_TG3_FET_PTEST_TRIM_2;
			tg3_writephy(tp, MII_TG3_FET_PTEST, ptest);
		}
	} else
		bmcr |= BMCR_LOOPBACK;

	tg3_writephy(tp, MII_BMCR, bmcr);

	/* The write needs to be flushed for the FETs */
	if (tp->phy_flags & TG3_PHYFLG_IS_FET)
		tg3_readphy(tp, MII_BMCR, &bmcr);

	udelay(40);

	if ((tp->phy_flags & TG3_PHYFLG_IS_FET) &&
	    tg3_asic_rev(tp) == ASIC_REV_5785) {
		tg3_writephy(tp, MII_TG3_FET_PTEST, ptest |
			     MII_TG3_FET_PTEST_FRC_TX_LINK |
			     MII_TG3_FET_PTEST_FRC_TX_LOCK);

		/* The write needs to be flushed for the AC131 */
		tg3_readphy(tp, MII_TG3_FET_PTEST, &val);
	}

	/* Reset to prevent losing 1st rx packet intermittently */
	if ((tp->phy_flags & TG3_PHYFLG_MII_SERDES) &&
	    tg3_flag(tp, 5780_CLASS)) {
		tw32_f(MAC_RX_MODE, RX_MODE_RESET);
		udelay(10);
		tw32_f(MAC_RX_MODE, tp->rx_mode);
	}

	mac_mode = tp->mac_mode &
		   ~(MAC_MODE_PORT_MODE_MASK | MAC_MODE_HALF_DUPLEX);
	if (speed == SPEED_1000)
		mac_mode |= MAC_MODE_PORT_MODE_GMII;
	else
		mac_mode |= MAC_MODE_PORT_MODE_MII;

	if (tg3_asic_rev(tp) == ASIC_REV_5700) {
		u32 masked_phy_id = tp->phy_id & TG3_PHY_ID_MASK;

		if (masked_phy_id == TG3_PHY_ID_BCM5401)
			mac_mode &= ~MAC_MODE_LINK_POLARITY;
		else if (masked_phy_id == TG3_PHY_ID_BCM5411)
			mac_mode |= MAC_MODE_LINK_POLARITY;

		tg3_writephy(tp, MII_TG3_EXT_CTRL,
			     MII_TG3_EXT_CTRL_LNK3_LED_MODE);
	}

	tw32(MAC_MODE, mac_mode);
	udelay(40);

	return 0;
}

static void tg3_set_loopback(struct net_device *dev, netdev_features_t features)
{
	struct tg3 *tp = netdev_priv(dev);

	if (features & NETIF_F_LOOPBACK) {
		if (tp->mac_mode & MAC_MODE_PORT_INT_LPBACK)
			return;

		spin_lock_bh(&tp->lock);
		tg3_mac_loopback(tp, true);
		netif_carrier_on(tp->dev);
		spin_unlock_bh(&tp->lock);
		netdev_info(dev, "Internal MAC loopback mode enabled.\n");
	} else {
		if (!(tp->mac_mode & MAC_MODE_PORT_INT_LPBACK))
			return;

		spin_lock_bh(&tp->lock);
		tg3_mac_loopback(tp, false);
		/* Force link status check */
		tg3_setup_phy(tp, true);
		spin_unlock_bh(&tp->lock);
		netdev_info(dev, "Internal MAC loopback mode disabled.\n");
	}
}

static netdev_features_t tg3_fix_features(struct net_device *dev,
	netdev_features_t features)
{
	struct tg3 *tp = netdev_priv(dev);

	if (dev->mtu > ETH_DATA_LEN && tg3_flag(tp, 5780_CLASS))
		features &= ~NETIF_F_ALL_TSO;

	return features;
}

static int tg3_set_features(struct net_device *dev, netdev_features_t features)
{
	netdev_features_t changed = dev->features ^ features;

	if ((changed & NETIF_F_LOOPBACK) && netif_running(dev))
		tg3_set_loopback(dev, features);

	return 0;
}

static void tg3_rx_prodring_free(struct tg3 *tp,
				 struct tg3_rx_prodring_set *tpr)
{
	int i;

	if (tpr != &tp->napi[0].prodring) {
		for (i = tpr->rx_std_cons_idx; i != tpr->rx_std_prod_idx;
		     i = (i + 1) & tp->rx_std_ring_mask)
			tg3_rx_data_free(tp, &tpr->rx_std_buffers[i],
					tp->rx_pkt_map_sz);

		if (tg3_flag(tp, JUMBO_CAPABLE)) {
			for (i = tpr->rx_jmb_cons_idx;
			     i != tpr->rx_jmb_prod_idx;
			     i = (i + 1) & tp->rx_jmb_ring_mask) {
				tg3_rx_data_free(tp, &tpr->rx_jmb_buffers[i],
						TG3_RX_JMB_MAP_SZ);
			}
		}

		return;
	}

	for (i = 0; i <= tp->rx_std_ring_mask; i++)
		tg3_rx_data_free(tp, &tpr->rx_std_buffers[i],
				tp->rx_pkt_map_sz);

	if (tg3_flag(tp, JUMBO_CAPABLE) && !tg3_flag(tp, 5780_CLASS)) {
		for (i = 0; i <= tp->rx_jmb_ring_mask; i++)
			tg3_rx_data_free(tp, &tpr->rx_jmb_buffers[i],
					TG3_RX_JMB_MAP_SZ);
	}
}

/* Initialize rx rings for packet processing.
 *
 * The chip has been shut down and the driver detached from
 * the networking, so no interrupts or new tx packets will
 * end up in the driver.  tp->{tx,}lock are held and thus
 * we may not sleep.
 */
static int tg3_rx_prodring_alloc(struct tg3 *tp,
				 struct tg3_rx_prodring_set *tpr)
{
	u32 i, rx_pkt_dma_sz;

	tpr->rx_std_cons_idx = 0;
	tpr->rx_std_prod_idx = 0;
	tpr->rx_jmb_cons_idx = 0;
	tpr->rx_jmb_prod_idx = 0;

	if (tpr != &tp->napi[0].prodring) {
		memset(&tpr->rx_std_buffers[0], 0,
		       TG3_RX_STD_BUFF_RING_SIZE(tp));
		if (tpr->rx_jmb_buffers)
			memset(&tpr->rx_jmb_buffers[0], 0,
			       TG3_RX_JMB_BUFF_RING_SIZE(tp));
		goto done;
	}

	/* Zero out all descriptors. */
	memset(tpr->rx_std, 0, TG3_RX_STD_RING_BYTES(tp));

	rx_pkt_dma_sz = TG3_RX_STD_DMA_SZ;
	if (tg3_flag(tp, 5780_CLASS) &&
	    tp->dev->mtu > ETH_DATA_LEN)
		rx_pkt_dma_sz = TG3_RX_JMB_DMA_SZ;
	tp->rx_pkt_map_sz = TG3_RX_DMA_TO_MAP_SZ(rx_pkt_dma_sz);

	/* Initialize invariants of the rings, we only set this
	 * stuff once.  This works because the card does not
	 * write into the rx buffer posting rings.
	 */
	for (i = 0; i <= tp->rx_std_ring_mask; i++) {
		struct tg3_rx_buffer_desc *rxd;

		rxd = &tpr->rx_std[i];
		rxd->idx_len = rx_pkt_dma_sz << RXD_LEN_SHIFT;
		rxd->type_flags = (RXD_FLAG_END << RXD_FLAGS_SHIFT);
		rxd->opaque = (RXD_OPAQUE_RING_STD |
			       (i << RXD_OPAQUE_INDEX_SHIFT));
	}

	/* Now allocate fresh SKBs for each rx ring. */
	for (i = 0; i < tp->rx_pending; i++) {
		unsigned int frag_size;

		if (tg3_alloc_rx_data(tp, tpr, RXD_OPAQUE_RING_STD, i,
				      &frag_size) < 0) {
			netdev_warn(tp->dev,
				    "Using a smaller RX standard ring. Only "
				    "%d out of %d buffers were allocated "
				    "successfully\n", i, tp->rx_pending);
			if (i == 0)
				goto initfail;
			tp->rx_pending = i;
			break;
		}
	}

	if (!tg3_flag(tp, JUMBO_CAPABLE) || tg3_flag(tp, 5780_CLASS))
		goto done;

	memset(tpr->rx_jmb, 0, TG3_RX_JMB_RING_BYTES(tp));

	if (!tg3_flag(tp, JUMBO_RING_ENABLE))
		goto done;

	for (i = 0; i <= tp->rx_jmb_ring_mask; i++) {
		struct tg3_rx_buffer_desc *rxd;

		rxd = &tpr->rx_jmb[i].std;
		rxd->idx_len = TG3_RX_JMB_DMA_SZ << RXD_LEN_SHIFT;
		rxd->type_flags = (RXD_FLAG_END << RXD_FLAGS_SHIFT) |
				  RXD_FLAG_JUMBO;
		rxd->opaque = (RXD_OPAQUE_RING_JUMBO |
		       (i << RXD_OPAQUE_INDEX_SHIFT));
	}

	for (i = 0; i < tp->rx_jumbo_pending; i++) {
		unsigned int frag_size;

		if (tg3_alloc_rx_data(tp, tpr, RXD_OPAQUE_RING_JUMBO, i,
				      &frag_size) < 0) {
			netdev_warn(tp->dev,
				    "Using a smaller RX jumbo ring. Only %d "
				    "out of %d buffers were allocated "
				    "successfully\n", i, tp->rx_jumbo_pending);
			if (i == 0)
				goto initfail;
			tp->rx_jumbo_pending = i;
			break;
		}
	}

done:
	return 0;

initfail:
	tg3_rx_prodring_free(tp, tpr);
	return -ENOMEM;
}

static void tg3_rx_prodring_fini(struct tg3 *tp,
				 struct tg3_rx_prodring_set *tpr)
{
	kfree(tpr->rx_std_buffers);
	tpr->rx_std_buffers = NULL;
	kfree(tpr->rx_jmb_buffers);
	tpr->rx_jmb_buffers = NULL;
	if (tpr->rx_std) {
		dma_free_coherent(&tp->pdev->dev, TG3_RX_STD_RING_BYTES(tp),
				  tpr->rx_std, tpr->rx_std_mapping);
		tpr->rx_std = NULL;
	}
	if (tpr->rx_jmb) {
		dma_free_coherent(&tp->pdev->dev, TG3_RX_JMB_RING_BYTES(tp),
				  tpr->rx_jmb, tpr->rx_jmb_mapping);
		tpr->rx_jmb = NULL;
	}
}

static int tg3_rx_prodring_init(struct tg3 *tp,
				struct tg3_rx_prodring_set *tpr)
{
	tpr->rx_std_buffers = kzalloc(TG3_RX_STD_BUFF_RING_SIZE(tp),
				      GFP_KERNEL);
	if (!tpr->rx_std_buffers)
		return -ENOMEM;

	tpr->rx_std = dma_alloc_coherent(&tp->pdev->dev,
					 TG3_RX_STD_RING_BYTES(tp),
					 &tpr->rx_std_mapping,
					 GFP_KERNEL);
	if (!tpr->rx_std)
		goto err_out;

	if (tg3_flag(tp, JUMBO_CAPABLE) && !tg3_flag(tp, 5780_CLASS)) {
		tpr->rx_jmb_buffers = kzalloc(TG3_RX_JMB_BUFF_RING_SIZE(tp),
					      GFP_KERNEL);
		if (!tpr->rx_jmb_buffers)
			goto err_out;

		tpr->rx_jmb = dma_alloc_coherent(&tp->pdev->dev,
						 TG3_RX_JMB_RING_BYTES(tp),
						 &tpr->rx_jmb_mapping,
						 GFP_KERNEL);
		if (!tpr->rx_jmb)
			goto err_out;
	}

	return 0;

err_out:
	tg3_rx_prodring_fini(tp, tpr);
	return -ENOMEM;
}

/* Free up pending packets in all rx/tx rings.
 *
 * The chip has been shut down and the driver detached from
 * the networking, so no interrupts or new tx packets will
 * end up in the driver.  tp->{tx,}lock is not held and we are not
 * in an interrupt context and thus may sleep.
 */
static void tg3_free_rings(struct tg3 *tp)
{
	int i, j;

	for (j = 0; j < tp->irq_cnt; j++) {
		struct tg3_napi *tnapi = &tp->napi[j];

		tg3_rx_prodring_free(tp, &tnapi->prodring);

		if (!tnapi->tx_buffers)
			continue;

		for (i = 0; i < TG3_TX_RING_SIZE; i++) {
			struct sk_buff *skb = tnapi->tx_buffers[i].skb;

			if (!skb)
				continue;

			tg3_tx_skb_unmap(tnapi, i,
					 skb_shinfo(skb)->nr_frags - 1);

			dev_kfree_skb_any(skb);
		}
		netdev_tx_reset_queue(netdev_get_tx_queue(tp->dev, j));
	}
}

/* Initialize tx/rx rings for packet processing.
 *
 * The chip has been shut down and the driver detached from
 * the networking, so no interrupts or new tx packets will
 * end up in the driver.  tp->{tx,}lock are held and thus
 * we may not sleep.
 */
static int tg3_init_rings(struct tg3 *tp)
{
	int i;

	/* Free up all the SKBs. */
	tg3_free_rings(tp);

	for (i = 0; i < tp->irq_cnt; i++) {
		struct tg3_napi *tnapi = &tp->napi[i];

		tnapi->last_tag = 0;
		tnapi->last_irq_tag = 0;
		tnapi->hw_status->status = 0;
		tnapi->hw_status->status_tag = 0;
		memset(tnapi->hw_status, 0, TG3_HW_STATUS_SIZE);

		tnapi->tx_prod = 0;
		tnapi->tx_cons = 0;
		if (tnapi->tx_ring)
			memset(tnapi->tx_ring, 0, TG3_TX_RING_BYTES);

		tnapi->rx_rcb_ptr = 0;
		if (tnapi->rx_rcb)
			memset(tnapi->rx_rcb, 0, TG3_RX_RCB_RING_BYTES(tp));

		if (tnapi->prodring.rx_std &&
		    tg3_rx_prodring_alloc(tp, &tnapi->prodring)) {
			tg3_free_rings(tp);
			return -ENOMEM;
		}
	}

	return 0;
}

static void tg3_mem_tx_release(struct tg3 *tp)
{
	int i;

	for (i = 0; i < tp->irq_max; i++) {
		struct tg3_napi *tnapi = &tp->napi[i];

		if (tnapi->tx_ring) {
			dma_free_coherent(&tp->pdev->dev, TG3_TX_RING_BYTES,
				tnapi->tx_ring, tnapi->tx_desc_mapping);
			tnapi->tx_ring = NULL;
		}

		kfree(tnapi->tx_buffers);
		tnapi->tx_buffers = NULL;
	}
}

static int tg3_mem_tx_acquire(struct tg3 *tp)
{
	int i;
	struct tg3_napi *tnapi = &tp->napi[0];

	/* If multivector TSS is enabled, vector 0 does not handle
	 * tx interrupts.  Don't allocate any resources for it.
	 */
	if (tg3_flag(tp, ENABLE_TSS))
		tnapi++;

	for (i = 0; i < tp->txq_cnt; i++, tnapi++) {
		tnapi->tx_buffers = kzalloc(sizeof(struct tg3_tx_ring_info) *
					    TG3_TX_RING_SIZE, GFP_KERNEL);
		if (!tnapi->tx_buffers)
			goto err_out;

		tnapi->tx_ring = dma_alloc_coherent(&tp->pdev->dev,
						    TG3_TX_RING_BYTES,
						    &tnapi->tx_desc_mapping,
						    GFP_KERNEL);
		if (!tnapi->tx_ring)
			goto err_out;
	}

	return 0;

err_out:
	tg3_mem_tx_release(tp);
	return -ENOMEM;
}

static void tg3_mem_rx_release(struct tg3 *tp)
{
	int i;

	for (i = 0; i < tp->irq_max; i++) {
		struct tg3_napi *tnapi = &tp->napi[i];

		tg3_rx_prodring_fini(tp, &tnapi->prodring);

		if (!tnapi->rx_rcb)
			continue;

		dma_free_coherent(&tp->pdev->dev,
				  TG3_RX_RCB_RING_BYTES(tp),
				  tnapi->rx_rcb,
				  tnapi->rx_rcb_mapping);
		tnapi->rx_rcb = NULL;
	}
}

static int tg3_mem_rx_acquire(struct tg3 *tp)
{
	unsigned int i, limit;

	limit = tp->rxq_cnt;

	/* If RSS is enabled, we need a (dummy) producer ring
	 * set on vector zero.  This is the true hw prodring.
	 */
	if (tg3_flag(tp, ENABLE_RSS))
		limit++;

	for (i = 0; i < limit; i++) {
		struct tg3_napi *tnapi = &tp->napi[i];

		if (tg3_rx_prodring_init(tp, &tnapi->prodring))
			goto err_out;

		/* If multivector RSS is enabled, vector 0
		 * does not handle rx or tx interrupts.
		 * Don't allocate any resources for it.
		 */
		if (!i && tg3_flag(tp, ENABLE_RSS))
			continue;

		tnapi->rx_rcb = dma_alloc_coherent(&tp->pdev->dev,
						   TG3_RX_RCB_RING_BYTES(tp),
						   &tnapi->rx_rcb_mapping,
						   GFP_KERNEL | __GFP_ZERO);
		if (!tnapi->rx_rcb)
			goto err_out;
	}

	return 0;

err_out:
	tg3_mem_rx_release(tp);
	return -ENOMEM;
}

/*
 * Must not be invoked with interrupt sources disabled and
 * the hardware shutdown down.
 */
static void tg3_free_consistent(struct tg3 *tp)
{
	int i;

	for (i = 0; i < tp->irq_cnt; i++) {
		struct tg3_napi *tnapi = &tp->napi[i];

		if (tnapi->hw_status) {
			dma_free_coherent(&tp->pdev->dev, TG3_HW_STATUS_SIZE,
					  tnapi->hw_status,
					  tnapi->status_mapping);
			tnapi->hw_status = NULL;
		}
	}

	tg3_mem_rx_release(tp);
	tg3_mem_tx_release(tp);

	if (tp->hw_stats) {
		dma_free_coherent(&tp->pdev->dev, sizeof(struct tg3_hw_stats),
				  tp->hw_stats, tp->stats_mapping);
		tp->hw_stats = NULL;
	}
}

/*
 * Must not be invoked with interrupt sources disabled and
 * the hardware shutdown down.  Can sleep.
 */
static int tg3_alloc_consistent(struct tg3 *tp)
{
	int i;

	tp->hw_stats = dma_alloc_coherent(&tp->pdev->dev,
					  sizeof(struct tg3_hw_stats),
					  &tp->stats_mapping,
					  GFP_KERNEL | __GFP_ZERO);
	if (!tp->hw_stats)
		goto err_out;

	for (i = 0; i < tp->irq_cnt; i++) {
		struct tg3_napi *tnapi = &tp->napi[i];
		struct tg3_hw_status *sblk;

		tnapi->hw_status = dma_alloc_coherent(&tp->pdev->dev,
						      TG3_HW_STATUS_SIZE,
						      &tnapi->status_mapping,
						      GFP_KERNEL | __GFP_ZERO);
		if (!tnapi->hw_status)
			goto err_out;

		sblk = tnapi->hw_status;

		if (tg3_flag(tp, ENABLE_RSS)) {
			u16 *prodptr = NULL;

			/*
			 * When RSS is enabled, the status block format changes
			 * slightly.  The "rx_jumbo_consumer", "reserved",
			 * and "rx_mini_consumer" members get mapped to the
			 * other three rx return ring producer indexes.
			 */
			switch (i) {
			case 1:
				prodptr = &sblk->idx[0].rx_producer;
				break;
			case 2:
				prodptr = &sblk->rx_jumbo_consumer;
				break;
			case 3:
				prodptr = &sblk->reserved;
				break;
			case 4:
				prodptr = &sblk->rx_mini_consumer;
				break;
			}
			tnapi->rx_rcb_prod_idx = prodptr;
		} else {
			tnapi->rx_rcb_prod_idx = &sblk->idx[0].rx_producer;
		}
	}

	if (tg3_mem_tx_acquire(tp) || tg3_mem_rx_acquire(tp))
		goto err_out;

	return 0;

err_out:
	tg3_free_consistent(tp);
	return -ENOMEM;
}

#define MAX_WAIT_CNT 1000

/* To stop a block, clear the enable bit and poll till it
 * clears.  tp->lock is held.
 */
static int tg3_stop_block(struct tg3 *tp, unsigned long ofs, u32 enable_bit, bool silent)
{
	unsigned int i;
	u32 val;

	if (tg3_flag(tp, 5705_PLUS)) {
		switch (ofs) {
		case RCVLSC_MODE:
		case DMAC_MODE:
		case MBFREE_MODE:
		case BUFMGR_MODE:
		case MEMARB_MODE:
			/* We can't enable/disable these bits of the
			 * 5705/5750, just say success.
			 */
			return 0;

		default:
			break;
		}
	}

	val = tr32(ofs);
	val &= ~enable_bit;
	tw32_f(ofs, val);

	for (i = 0; i < MAX_WAIT_CNT; i++) {
		if (pci_channel_offline(tp->pdev)) {
			dev_err(&tp->pdev->dev,
				"tg3_stop_block device offline, "
				"ofs=%lx enable_bit=%x\n",
				ofs, enable_bit);
			return -ENODEV;
		}

		udelay(100);
		val = tr32(ofs);
		if ((val & enable_bit) == 0)
			break;
	}

	if (i == MAX_WAIT_CNT && !silent) {
		dev_err(&tp->pdev->dev,
			"tg3_stop_block timed out, ofs=%lx enable_bit=%x\n",
			ofs, enable_bit);
		return -ENODEV;
	}

	return 0;
}

/* tp->lock is held. */
static int tg3_abort_hw(struct tg3 *tp, bool silent)
{
	int i, err;

	tg3_disable_ints(tp);

	if (pci_channel_offline(tp->pdev)) {
		tp->rx_mode &= ~(RX_MODE_ENABLE | TX_MODE_ENABLE);
		tp->mac_mode &= ~MAC_MODE_TDE_ENABLE;
		err = -ENODEV;
		goto err_no_dev;
	}

	tp->rx_mode &= ~RX_MODE_ENABLE;
	tw32_f(MAC_RX_MODE, tp->rx_mode);
	udelay(10);

	err  = tg3_stop_block(tp, RCVBDI_MODE, RCVBDI_MODE_ENABLE, silent);
	err |= tg3_stop_block(tp, RCVLPC_MODE, RCVLPC_MODE_ENABLE, silent);
	err |= tg3_stop_block(tp, RCVLSC_MODE, RCVLSC_MODE_ENABLE, silent);
	err |= tg3_stop_block(tp, RCVDBDI_MODE, RCVDBDI_MODE_ENABLE, silent);
	err |= tg3_stop_block(tp, RCVDCC_MODE, RCVDCC_MODE_ENABLE, silent);
	err |= tg3_stop_block(tp, RCVCC_MODE, RCVCC_MODE_ENABLE, silent);

	err |= tg3_stop_block(tp, SNDBDS_MODE, SNDBDS_MODE_ENABLE, silent);
	err |= tg3_stop_block(tp, SNDBDI_MODE, SNDBDI_MODE_ENABLE, silent);
	err |= tg3_stop_block(tp, SNDDATAI_MODE, SNDDATAI_MODE_ENABLE, silent);
	err |= tg3_stop_block(tp, RDMAC_MODE, RDMAC_MODE_ENABLE, silent);
	err |= tg3_stop_block(tp, SNDDATAC_MODE, SNDDATAC_MODE_ENABLE, silent);
	err |= tg3_stop_block(tp, DMAC_MODE, DMAC_MODE_ENABLE, silent);
	err |= tg3_stop_block(tp, SNDBDC_MODE, SNDBDC_MODE_ENABLE, silent);

	tp->mac_mode &= ~MAC_MODE_TDE_ENABLE;
	tw32_f(MAC_MODE, tp->mac_mode);
	udelay(40);

	tp->tx_mode &= ~TX_MODE_ENABLE;
	tw32_f(MAC_TX_MODE, tp->tx_mode);

	for (i = 0; i < MAX_WAIT_CNT; i++) {
		udelay(100);
		if (!(tr32(MAC_TX_MODE) & TX_MODE_ENABLE))
			break;
	}
	if (i >= MAX_WAIT_CNT) {
		dev_err(&tp->pdev->dev,
			"%s timed out, TX_MODE_ENABLE will not clear "
			"MAC_TX_MODE=%08x\n", __func__, tr32(MAC_TX_MODE));
		err |= -ENODEV;
	}

	err |= tg3_stop_block(tp, HOSTCC_MODE, HOSTCC_MODE_ENABLE, silent);
	err |= tg3_stop_block(tp, WDMAC_MODE, WDMAC_MODE_ENABLE, silent);
	err |= tg3_stop_block(tp, MBFREE_MODE, MBFREE_MODE_ENABLE, silent);

	tw32(FTQ_RESET, 0xffffffff);
	tw32(FTQ_RESET, 0x00000000);

	err |= tg3_stop_block(tp, BUFMGR_MODE, BUFMGR_MODE_ENABLE, silent);
	err |= tg3_stop_block(tp, MEMARB_MODE, MEMARB_MODE_ENABLE, silent);

err_no_dev:
	for (i = 0; i < tp->irq_cnt; i++) {
		struct tg3_napi *tnapi = &tp->napi[i];
		if (tnapi->hw_status)
			memset(tnapi->hw_status, 0, TG3_HW_STATUS_SIZE);
	}

	return err;
}

/* Save PCI command register before chip reset */
static void tg3_save_pci_state(struct tg3 *tp)
{
	pci_read_config_word(tp->pdev, PCI_COMMAND, &tp->pci_cmd);
}

/* Restore PCI state after chip reset */
static void tg3_restore_pci_state(struct tg3 *tp)
{
	u32 val;

	/* Re-enable indirect register accesses. */
	pci_write_config_dword(tp->pdev, TG3PCI_MISC_HOST_CTRL,
			       tp->misc_host_ctrl);

	/* Set MAX PCI retry to zero. */
	val = (PCISTATE_ROM_ENABLE | PCISTATE_ROM_RETRY_ENABLE);
	if (tg3_chip_rev_id(tp) == CHIPREV_ID_5704_A0 &&
	    tg3_flag(tp, PCIX_MODE))
		val |= PCISTATE_RETRY_SAME_DMA;
	/* Allow reads and writes to the APE register and memory space. */
	if (tg3_flag(tp, ENABLE_APE))
		val |= PCISTATE_ALLOW_APE_CTLSPC_WR |
		       PCISTATE_ALLOW_APE_SHMEM_WR |
		       PCISTATE_ALLOW_APE_PSPACE_WR;
	pci_write_config_dword(tp->pdev, TG3PCI_PCISTATE, val);

	pci_write_config_word(tp->pdev, PCI_COMMAND, tp->pci_cmd);

	if (!tg3_flag(tp, PCI_EXPRESS)) {
		pci_write_config_byte(tp->pdev, PCI_CACHE_LINE_SIZE,
				      tp->pci_cacheline_sz);
		pci_write_config_byte(tp->pdev, PCI_LATENCY_TIMER,
				      tp->pci_lat_timer);
	}

	/* Make sure PCI-X relaxed ordering bit is clear. */
	if (tg3_flag(tp, PCIX_MODE)) {
		u16 pcix_cmd;

		pci_read_config_word(tp->pdev, tp->pcix_cap + PCI_X_CMD,
				     &pcix_cmd);
		pcix_cmd &= ~PCI_X_CMD_ERO;
		pci_write_config_word(tp->pdev, tp->pcix_cap + PCI_X_CMD,
				      pcix_cmd);
	}

	if (tg3_flag(tp, 5780_CLASS)) {

		/* Chip reset on 5780 will reset MSI enable bit,
		 * so need to restore it.
		 */
		if (tg3_flag(tp, USING_MSI)) {
			u16 ctrl;

			pci_read_config_word(tp->pdev,
					     tp->msi_cap + PCI_MSI_FLAGS,
					     &ctrl);
			pci_write_config_word(tp->pdev,
					      tp->msi_cap + PCI_MSI_FLAGS,
					      ctrl | PCI_MSI_FLAGS_ENABLE);
			val = tr32(MSGINT_MODE);
			tw32(MSGINT_MODE, val | MSGINT_MODE_ENABLE);
		}
	}
}

/* tp->lock is held. */
static int tg3_chip_reset(struct tg3 *tp)
{
	u32 val;
	void (*write_op)(struct tg3 *, u32, u32);
	int i, err;

	tg3_nvram_lock(tp);

	tg3_ape_lock(tp, TG3_APE_LOCK_GRC);

	/* No matching tg3_nvram_unlock() after this because
	 * chip reset below will undo the nvram lock.
	 */
	tp->nvram_lock_cnt = 0;

	/* GRC_MISC_CFG core clock reset will clear the memory
	 * enable bit in PCI register 4 and the MSI enable bit
	 * on some chips, so we save relevant registers here.
	 */
	tg3_save_pci_state(tp);

	if (tg3_asic_rev(tp) == ASIC_REV_5752 ||
	    tg3_flag(tp, 5755_PLUS))
		tw32(GRC_FASTBOOT_PC, 0);

	/*
	 * We must avoid the readl() that normally takes place.
	 * It locks machines, causes machine checks, and other
	 * fun things.  So, temporarily disable the 5701
	 * hardware workaround, while we do the reset.
	 */
	write_op = tp->write32;
	if (write_op == tg3_write_flush_reg32)
		tp->write32 = tg3_write32;

	/* Prevent the irq handler from reading or writing PCI registers
	 * during chip reset when the memory enable bit in the PCI command
	 * register may be cleared.  The chip does not generate interrupt
	 * at this time, but the irq handler may still be called due to irq
	 * sharing or irqpoll.
	 */
	tg3_flag_set(tp, CHIP_RESETTING);
	for (i = 0; i < tp->irq_cnt; i++) {
		struct tg3_napi *tnapi = &tp->napi[i];
		if (tnapi->hw_status) {
			tnapi->hw_status->status = 0;
			tnapi->hw_status->status_tag = 0;
		}
		tnapi->last_tag = 0;
		tnapi->last_irq_tag = 0;
	}
	smp_mb();

	for (i = 0; i < tp->irq_cnt; i++)
		synchronize_irq(tp->napi[i].irq_vec);

	if (tg3_asic_rev(tp) == ASIC_REV_57780) {
		val = tr32(TG3_PCIE_LNKCTL) & ~TG3_PCIE_LNKCTL_L1_PLL_PD_EN;
		tw32(TG3_PCIE_LNKCTL, val | TG3_PCIE_LNKCTL_L1_PLL_PD_DIS);
	}

	/* do the reset */
	val = GRC_MISC_CFG_CORECLK_RESET;

	if (tg3_flag(tp, PCI_EXPRESS)) {
		/* Force PCIe 1.0a mode */
		if (tg3_asic_rev(tp) != ASIC_REV_5785 &&
		    !tg3_flag(tp, 57765_PLUS) &&
		    tr32(TG3_PCIE_PHY_TSTCTL) ==
		    (TG3_PCIE_PHY_TSTCTL_PCIE10 | TG3_PCIE_PHY_TSTCTL_PSCRAM))
			tw32(TG3_PCIE_PHY_TSTCTL, TG3_PCIE_PHY_TSTCTL_PSCRAM);

		if (tg3_chip_rev_id(tp) != CHIPREV_ID_5750_A0) {
			tw32(GRC_MISC_CFG, (1 << 29));
			val |= (1 << 29);
		}
	}

	if (tg3_asic_rev(tp) == ASIC_REV_5906) {
		tw32(VCPU_STATUS, tr32(VCPU_STATUS) | VCPU_STATUS_DRV_RESET);
		tw32(GRC_VCPU_EXT_CTRL,
		     tr32(GRC_VCPU_EXT_CTRL) & ~GRC_VCPU_EXT_CTRL_HALT_CPU);
	}

	/* Manage gphy power for all CPMU absent PCIe devices. */
	if (tg3_flag(tp, 5705_PLUS) && !tg3_flag(tp, CPMU_PRESENT))
		val |= GRC_MISC_CFG_KEEP_GPHY_POWER;

	tw32(GRC_MISC_CFG, val);

	/* restore 5701 hardware bug workaround write method */
	tp->write32 = write_op;

	/* Unfortunately, we have to delay before the PCI read back.
	 * Some 575X chips even will not respond to a PCI cfg access
	 * when the reset command is given to the chip.
	 *
	 * How do these hardware designers expect things to work
	 * properly if the PCI write is posted for a long period
	 * of time?  It is always necessary to have some method by
	 * which a register read back can occur to push the write
	 * out which does the reset.
	 *
	 * For most tg3 variants the trick below was working.
	 * Ho hum...
	 */
	udelay(120);

	/* Flush PCI posted writes.  The normal MMIO registers
	 * are inaccessible at this time so this is the only
	 * way to make this reliably (actually, this is no longer
	 * the case, see above).  I tried to use indirect
	 * register read/write but this upset some 5701 variants.
	 */
	pci_read_config_dword(tp->pdev, PCI_COMMAND, &val);

	udelay(120);

	if (tg3_flag(tp, PCI_EXPRESS) && pci_is_pcie(tp->pdev)) {
		u16 val16;

		if (tg3_chip_rev_id(tp) == CHIPREV_ID_5750_A0) {
			int j;
			u32 cfg_val;

			/* Wait for link training to complete.  */
			for (j = 0; j < 5000; j++)
				udelay(100);

			pci_read_config_dword(tp->pdev, 0xc4, &cfg_val);
			pci_write_config_dword(tp->pdev, 0xc4,
					       cfg_val | (1 << 15));
		}

		/* Clear the "no snoop" and "relaxed ordering" bits. */
		val16 = PCI_EXP_DEVCTL_RELAX_EN | PCI_EXP_DEVCTL_NOSNOOP_EN;
		/*
		 * Older PCIe devices only support the 128 byte
		 * MPS setting.  Enforce the restriction.
		 */
		if (!tg3_flag(tp, CPMU_PRESENT))
			val16 |= PCI_EXP_DEVCTL_PAYLOAD;
		pcie_capability_clear_word(tp->pdev, PCI_EXP_DEVCTL, val16);

		/* Clear error status */
		pcie_capability_write_word(tp->pdev, PCI_EXP_DEVSTA,
				      PCI_EXP_DEVSTA_CED |
				      PCI_EXP_DEVSTA_NFED |
				      PCI_EXP_DEVSTA_FED |
				      PCI_EXP_DEVSTA_URD);
	}

	tg3_restore_pci_state(tp);

	tg3_flag_clear(tp, CHIP_RESETTING);
	tg3_flag_clear(tp, ERROR_PROCESSED);

	val = 0;
	if (tg3_flag(tp, 5780_CLASS))
		val = tr32(MEMARB_MODE);
	tw32(MEMARB_MODE, val | MEMARB_MODE_ENABLE);

	if (tg3_chip_rev_id(tp) == CHIPREV_ID_5750_A3) {
		tg3_stop_fw(tp);
		tw32(0x5000, 0x400);
	}

	if (tg3_flag(tp, IS_SSB_CORE)) {
		/*
		 * BCM4785: In order to avoid repercussions from using
		 * potentially defective internal ROM, stop the Rx RISC CPU,
		 * which is not required.
		 */
		tg3_stop_fw(tp);
		tg3_halt_cpu(tp, RX_CPU_BASE);
	}

	err = tg3_poll_fw(tp);
	if (err)
		return err;

	tw32(GRC_MODE, tp->grc_mode);

	if (tg3_chip_rev_id(tp) == CHIPREV_ID_5705_A0) {
		val = tr32(0xc4);

		tw32(0xc4, val | (1 << 15));
	}

	if ((tp->nic_sram_data_cfg & NIC_SRAM_DATA_CFG_MINI_PCI) != 0 &&
	    tg3_asic_rev(tp) == ASIC_REV_5705) {
		tp->pci_clock_ctrl |= CLOCK_CTRL_CLKRUN_OENABLE;
		if (tg3_chip_rev_id(tp) == CHIPREV_ID_5705_A0)
			tp->pci_clock_ctrl |= CLOCK_CTRL_FORCE_CLKRUN;
		tw32(TG3PCI_CLOCK_CTRL, tp->pci_clock_ctrl);
	}

	if (tp->phy_flags & TG3_PHYFLG_PHY_SERDES) {
		tp->mac_mode = MAC_MODE_PORT_MODE_TBI;
		val = tp->mac_mode;
	} else if (tp->phy_flags & TG3_PHYFLG_MII_SERDES) {
		tp->mac_mode = MAC_MODE_PORT_MODE_GMII;
		val = tp->mac_mode;
	} else
		val = 0;

	tw32_f(MAC_MODE, val);
	udelay(40);

	tg3_ape_unlock(tp, TG3_APE_LOCK_GRC);

	tg3_mdio_start(tp);

	if (tg3_flag(tp, PCI_EXPRESS) &&
	    tg3_chip_rev_id(tp) != CHIPREV_ID_5750_A0 &&
	    tg3_asic_rev(tp) != ASIC_REV_5785 &&
	    !tg3_flag(tp, 57765_PLUS)) {
		val = tr32(0x7c00);

		tw32(0x7c00, val | (1 << 25));
	}

	if (tg3_asic_rev(tp) == ASIC_REV_5720) {
		val = tr32(TG3_CPMU_CLCK_ORIDE);
		tw32(TG3_CPMU_CLCK_ORIDE, val & ~CPMU_CLCK_ORIDE_MAC_ORIDE_EN);
	}

	/* Reprobe ASF enable state.  */
	tg3_flag_clear(tp, ENABLE_ASF);
	tp->phy_flags &= ~(TG3_PHYFLG_1G_ON_VAUX_OK |
			   TG3_PHYFLG_KEEP_LINK_ON_PWRDN);

	tg3_flag_clear(tp, ASF_NEW_HANDSHAKE);
	tg3_read_mem(tp, NIC_SRAM_DATA_SIG, &val);
	if (val == NIC_SRAM_DATA_SIG_MAGIC) {
		u32 nic_cfg;

		tg3_read_mem(tp, NIC_SRAM_DATA_CFG, &nic_cfg);
		if (nic_cfg & NIC_SRAM_DATA_CFG_ASF_ENABLE) {
			tg3_flag_set(tp, ENABLE_ASF);
			tp->last_event_jiffies = jiffies;
			if (tg3_flag(tp, 5750_PLUS))
				tg3_flag_set(tp, ASF_NEW_HANDSHAKE);

			tg3_read_mem(tp, NIC_SRAM_DATA_CFG_3, &nic_cfg);
			if (nic_cfg & NIC_SRAM_1G_ON_VAUX_OK)
				tp->phy_flags |= TG3_PHYFLG_1G_ON_VAUX_OK;
			if (nic_cfg & NIC_SRAM_LNK_FLAP_AVOID)
				tp->phy_flags |= TG3_PHYFLG_KEEP_LINK_ON_PWRDN;
		}
	}

	return 0;
}

static void tg3_get_nstats(struct tg3 *, struct rtnl_link_stats64 *);
static void tg3_get_estats(struct tg3 *, struct tg3_ethtool_stats *);

/* tp->lock is held. */
static int tg3_halt(struct tg3 *tp, int kind, bool silent)
{
	int err;

	tg3_stop_fw(tp);

	tg3_write_sig_pre_reset(tp, kind);

	tg3_abort_hw(tp, silent);
	err = tg3_chip_reset(tp);

	__tg3_set_mac_addr(tp, false);

	tg3_write_sig_legacy(tp, kind);
	tg3_write_sig_post_reset(tp, kind);

	if (tp->hw_stats) {
		/* Save the stats across chip resets... */
		tg3_get_nstats(tp, &tp->net_stats_prev);
		tg3_get_estats(tp, &tp->estats_prev);

		/* And make sure the next sample is new data */
		memset(tp->hw_stats, 0, sizeof(struct tg3_hw_stats));
	}

	if (err)
		return err;

	return 0;
}

static int tg3_set_mac_addr(struct net_device *dev, void *p)
{
	struct tg3 *tp = netdev_priv(dev);
	struct sockaddr *addr = p;
	int err = 0;
	bool skip_mac_1 = false;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	memcpy(dev->dev_addr, addr->sa_data, dev->addr_len);

	if (!netif_running(dev))
		return 0;

	if (tg3_flag(tp, ENABLE_ASF)) {
		u32 addr0_high, addr0_low, addr1_high, addr1_low;

		addr0_high = tr32(MAC_ADDR_0_HIGH);
		addr0_low = tr32(MAC_ADDR_0_LOW);
		addr1_high = tr32(MAC_ADDR_1_HIGH);
		addr1_low = tr32(MAC_ADDR_1_LOW);

		/* Skip MAC addr 1 if ASF is using it. */
		if ((addr0_high != addr1_high || addr0_low != addr1_low) &&
		    !(addr1_high == 0 && addr1_low == 0))
			skip_mac_1 = true;
	}
	spin_lock_bh(&tp->lock);
	__tg3_set_mac_addr(tp, skip_mac_1);
	spin_unlock_bh(&tp->lock);

	return err;
}

/* tp->lock is held. */
static void tg3_set_bdinfo(struct tg3 *tp, u32 bdinfo_addr,
			   dma_addr_t mapping, u32 maxlen_flags,
			   u32 nic_addr)
{
	tg3_write_mem(tp,
		      (bdinfo_addr + TG3_BDINFO_HOST_ADDR + TG3_64BIT_REG_HIGH),
		      ((u64) mapping >> 32));
	tg3_write_mem(tp,
		      (bdinfo_addr + TG3_BDINFO_HOST_ADDR + TG3_64BIT_REG_LOW),
		      ((u64) mapping & 0xffffffff));
	tg3_write_mem(tp,
		      (bdinfo_addr + TG3_BDINFO_MAXLEN_FLAGS),
		       maxlen_flags);

	if (!tg3_flag(tp, 5705_PLUS))
		tg3_write_mem(tp,
			      (bdinfo_addr + TG3_BDINFO_NIC_ADDR),
			      nic_addr);
}


static void tg3_coal_tx_init(struct tg3 *tp, struct ethtool_coalesce *ec)
{
	int i = 0;

	if (!tg3_flag(tp, ENABLE_TSS)) {
		tw32(HOSTCC_TXCOL_TICKS, ec->tx_coalesce_usecs);
		tw32(HOSTCC_TXMAX_FRAMES, ec->tx_max_coalesced_frames);
		tw32(HOSTCC_TXCOAL_MAXF_INT, ec->tx_max_coalesced_frames_irq);
	} else {
		tw32(HOSTCC_TXCOL_TICKS, 0);
		tw32(HOSTCC_TXMAX_FRAMES, 0);
		tw32(HOSTCC_TXCOAL_MAXF_INT, 0);

		for (; i < tp->txq_cnt; i++) {
			u32 reg;

			reg = HOSTCC_TXCOL_TICKS_VEC1 + i * 0x18;
			tw32(reg, ec->tx_coalesce_usecs);
			reg = HOSTCC_TXMAX_FRAMES_VEC1 + i * 0x18;
			tw32(reg, ec->tx_max_coalesced_frames);
			reg = HOSTCC_TXCOAL_MAXF_INT_VEC1 + i * 0x18;
			tw32(reg, ec->tx_max_coalesced_frames_irq);
		}
	}

	for (; i < tp->irq_max - 1; i++) {
		tw32(HOSTCC_TXCOL_TICKS_VEC1 + i * 0x18, 0);
		tw32(HOSTCC_TXMAX_FRAMES_VEC1 + i * 0x18, 0);
		tw32(HOSTCC_TXCOAL_MAXF_INT_VEC1 + i * 0x18, 0);
	}
}

static void tg3_coal_rx_init(struct tg3 *tp, struct ethtool_coalesce *ec)
{
	int i = 0;
	u32 limit = tp->rxq_cnt;

	if (!tg3_flag(tp, ENABLE_RSS)) {
		tw32(HOSTCC_RXCOL_TICKS, ec->rx_coalesce_usecs);
		tw32(HOSTCC_RXMAX_FRAMES, ec->rx_max_coalesced_frames);
		tw32(HOSTCC_RXCOAL_MAXF_INT, ec->rx_max_coalesced_frames_irq);
		limit--;
	} else {
		tw32(HOSTCC_RXCOL_TICKS, 0);
		tw32(HOSTCC_RXMAX_FRAMES, 0);
		tw32(HOSTCC_RXCOAL_MAXF_INT, 0);
	}

	for (; i < limit; i++) {
		u32 reg;

		reg = HOSTCC_RXCOL_TICKS_VEC1 + i * 0x18;
		tw32(reg, ec->rx_coalesce_usecs);
		reg = HOSTCC_RXMAX_FRAMES_VEC1 + i * 0x18;
		tw32(reg, ec->rx_max_coalesced_frames);
		reg = HOSTCC_RXCOAL_MAXF_INT_VEC1 + i * 0x18;
		tw32(reg, ec->rx_max_coalesced_frames_irq);
	}

	for (; i < tp->irq_max - 1; i++) {
		tw32(HOSTCC_RXCOL_TICKS_VEC1 + i * 0x18, 0);
		tw32(HOSTCC_RXMAX_FRAMES_VEC1 + i * 0x18, 0);
		tw32(HOSTCC_RXCOAL_MAXF_INT_VEC1 + i * 0x18, 0);
	}
}

static void __tg3_set_coalesce(struct tg3 *tp, struct ethtool_coalesce *ec)
{
	tg3_coal_tx_init(tp, ec);
	tg3_coal_rx_init(tp, ec);

	if (!tg3_flag(tp, 5705_PLUS)) {
		u32 val = ec->stats_block_coalesce_usecs;

		tw32(HOSTCC_RXCOAL_TICK_INT, ec->rx_coalesce_usecs_irq);
		tw32(HOSTCC_TXCOAL_TICK_INT, ec->tx_coalesce_usecs_irq);

		if (!tp->link_up)
			val = 0;

		tw32(HOSTCC_STAT_COAL_TICKS, val);
	}
}

/* tp->lock is held. */
static void tg3_rings_reset(struct tg3 *tp)
{
	int i;
	u32 stblk, txrcb, rxrcb, limit;
	struct tg3_napi *tnapi = &tp->napi[0];

	/* Disable all transmit rings but the first. */
	if (!tg3_flag(tp, 5705_PLUS))
		limit = NIC_SRAM_SEND_RCB + TG3_BDINFO_SIZE * 16;
	else if (tg3_flag(tp, 5717_PLUS))
		limit = NIC_SRAM_SEND_RCB + TG3_BDINFO_SIZE * 4;
	else if (tg3_flag(tp, 57765_CLASS) ||
		 tg3_asic_rev(tp) == ASIC_REV_5762)
		limit = NIC_SRAM_SEND_RCB + TG3_BDINFO_SIZE * 2;
	else
		limit = NIC_SRAM_SEND_RCB + TG3_BDINFO_SIZE;

	for (txrcb = NIC_SRAM_SEND_RCB + TG3_BDINFO_SIZE;
	     txrcb < limit; txrcb += TG3_BDINFO_SIZE)
		tg3_write_mem(tp, txrcb + TG3_BDINFO_MAXLEN_FLAGS,
			      BDINFO_FLAGS_DISABLED);


	/* Disable all receive return rings but the first. */
	if (tg3_flag(tp, 5717_PLUS))
		limit = NIC_SRAM_RCV_RET_RCB + TG3_BDINFO_SIZE * 17;
	else if (!tg3_flag(tp, 5705_PLUS))
		limit = NIC_SRAM_RCV_RET_RCB + TG3_BDINFO_SIZE * 16;
	else if (tg3_asic_rev(tp) == ASIC_REV_5755 ||
		 tg3_asic_rev(tp) == ASIC_REV_5762 ||
		 tg3_flag(tp, 57765_CLASS))
		limit = NIC_SRAM_RCV_RET_RCB + TG3_BDINFO_SIZE * 4;
	else
		limit = NIC_SRAM_RCV_RET_RCB + TG3_BDINFO_SIZE;

	for (rxrcb = NIC_SRAM_RCV_RET_RCB + TG3_BDINFO_SIZE;
	     rxrcb < limit; rxrcb += TG3_BDINFO_SIZE)
		tg3_write_mem(tp, rxrcb + TG3_BDINFO_MAXLEN_FLAGS,
			      BDINFO_FLAGS_DISABLED);

	/* Disable interrupts */
	tw32_mailbox_f(tp->napi[0].int_mbox, 1);
	tp->napi[0].chk_msi_cnt = 0;
	tp->napi[0].last_rx_cons = 0;
	tp->napi[0].last_tx_cons = 0;

	/* Zero mailbox registers. */
	if (tg3_flag(tp, SUPPORT_MSIX)) {
		for (i = 1; i < tp->irq_max; i++) {
			tp->napi[i].tx_prod = 0;
			tp->napi[i].tx_cons = 0;
			if (tg3_flag(tp, ENABLE_TSS))
				tw32_mailbox(tp->napi[i].prodmbox, 0);
			tw32_rx_mbox(tp->napi[i].consmbox, 0);
			tw32_mailbox_f(tp->napi[i].int_mbox, 1);
			tp->napi[i].chk_msi_cnt = 0;
			tp->napi[i].last_rx_cons = 0;
			tp->napi[i].last_tx_cons = 0;
		}
		if (!tg3_flag(tp, ENABLE_TSS))
			tw32_mailbox(tp->napi[0].prodmbox, 0);
	} else {
		tp->napi[0].tx_prod = 0;
		tp->napi[0].tx_cons = 0;
		tw32_mailbox(tp->napi[0].prodmbox, 0);
		tw32_rx_mbox(tp->napi[0].consmbox, 0);
	}

	/* Make sure the NIC-based send BD rings are disabled. */
	if (!tg3_flag(tp, 5705_PLUS)) {
		u32 mbox = MAILBOX_SNDNIC_PROD_IDX_0 + TG3_64BIT_REG_LOW;
		for (i = 0; i < 16; i++)
			tw32_tx_mbox(mbox + i * 8, 0);
	}

	txrcb = NIC_SRAM_SEND_RCB;
	rxrcb = NIC_SRAM_RCV_RET_RCB;

	/* Clear status block in ram. */
	memset(tnapi->hw_status, 0, TG3_HW_STATUS_SIZE);

	/* Set status block DMA address */
	tw32(HOSTCC_STATUS_BLK_HOST_ADDR + TG3_64BIT_REG_HIGH,
	     ((u64) tnapi->status_mapping >> 32));
	tw32(HOSTCC_STATUS_BLK_HOST_ADDR + TG3_64BIT_REG_LOW,
	     ((u64) tnapi->status_mapping & 0xffffffff));

	if (tnapi->tx_ring) {
		tg3_set_bdinfo(tp, txrcb, tnapi->tx_desc_mapping,
			       (TG3_TX_RING_SIZE <<
				BDINFO_FLAGS_MAXLEN_SHIFT),
			       NIC_SRAM_TX_BUFFER_DESC);
		txrcb += TG3_BDINFO_SIZE;
	}

	if (tnapi->rx_rcb) {
		tg3_set_bdinfo(tp, rxrcb, tnapi->rx_rcb_mapping,
			       (tp->rx_ret_ring_mask + 1) <<
				BDINFO_FLAGS_MAXLEN_SHIFT, 0);
		rxrcb += TG3_BDINFO_SIZE;
	}

	stblk = HOSTCC_STATBLCK_RING1;

	for (i = 1, tnapi++; i < tp->irq_cnt; i++, tnapi++) {
		u64 mapping = (u64)tnapi->status_mapping;
		tw32(stblk + TG3_64BIT_REG_HIGH, mapping >> 32);
		tw32(stblk + TG3_64BIT_REG_LOW, mapping & 0xffffffff);

		/* Clear status block in ram. */
		memset(tnapi->hw_status, 0, TG3_HW_STATUS_SIZE);

		if (tnapi->tx_ring) {
			tg3_set_bdinfo(tp, txrcb, tnapi->tx_desc_mapping,
				       (TG3_TX_RING_SIZE <<
					BDINFO_FLAGS_MAXLEN_SHIFT),
				       NIC_SRAM_TX_BUFFER_DESC);
			txrcb += TG3_BDINFO_SIZE;
		}

		tg3_set_bdinfo(tp, rxrcb, tnapi->rx_rcb_mapping,
			       ((tp->rx_ret_ring_mask + 1) <<
				BDINFO_FLAGS_MAXLEN_SHIFT), 0);

		stblk += 8;
		rxrcb += TG3_BDINFO_SIZE;
	}
}

static void tg3_setup_rxbd_thresholds(struct tg3 *tp)
{
	u32 val, bdcache_maxcnt, host_rep_thresh, nic_rep_thresh;

	if (!tg3_flag(tp, 5750_PLUS) ||
	    tg3_flag(tp, 5780_CLASS) ||
	    tg3_asic_rev(tp) == ASIC_REV_5750 ||
	    tg3_asic_rev(tp) == ASIC_REV_5752 ||
	    tg3_flag(tp, 57765_PLUS))
		bdcache_maxcnt = TG3_SRAM_RX_STD_BDCACHE_SIZE_5700;
	else if (tg3_asic_rev(tp) == ASIC_REV_5755 ||
		 tg3_asic_rev(tp) == ASIC_REV_5787)
		bdcache_maxcnt = TG3_SRAM_RX_STD_BDCACHE_SIZE_5755;
	else
		bdcache_maxcnt = TG3_SRAM_RX_STD_BDCACHE_SIZE_5906;

	nic_rep_thresh = min(bdcache_maxcnt / 2, tp->rx_std_max_post);
	host_rep_thresh = max_t(u32, tp->rx_pending / 8, 1);

	val = min(nic_rep_thresh, host_rep_thresh);
	tw32(RCVBDI_STD_THRESH, val);

	if (tg3_flag(tp, 57765_PLUS))
		tw32(STD_REPLENISH_LWM, bdcache_maxcnt);

	if (!tg3_flag(tp, JUMBO_CAPABLE) || tg3_flag(tp, 5780_CLASS))
		return;

	bdcache_maxcnt = TG3_SRAM_RX_JMB_BDCACHE_SIZE_5700;

	host_rep_thresh = max_t(u32, tp->rx_jumbo_pending / 8, 1);

	val = min(bdcache_maxcnt / 2, host_rep_thresh);
	tw32(RCVBDI_JUMBO_THRESH, val);

	if (tg3_flag(tp, 57765_PLUS))
		tw32(JMB_REPLENISH_LWM, bdcache_maxcnt);
}

static inline u32 calc_crc(unsigned char *buf, int len)
{
	u32 reg;
	u32 tmp;
	int j, k;

	reg = 0xffffffff;

	for (j = 0; j < len; j++) {
		reg ^= buf[j];

		for (k = 0; k < 8; k++) {
			tmp = reg & 0x01;

			reg >>= 1;

			if (tmp)
				reg ^= 0xedb88320;
		}
	}

	return ~reg;
}

static void tg3_set_multi(struct tg3 *tp, unsigned int accept_all)
{
	/* accept or reject all multicast frames */
	tw32(MAC_HASH_REG_0, accept_all ? 0xffffffff : 0);
	tw32(MAC_HASH_REG_1, accept_all ? 0xffffffff : 0);
	tw32(MAC_HASH_REG_2, accept_all ? 0xffffffff : 0);
	tw32(MAC_HASH_REG_3, accept_all ? 0xffffffff : 0);
}

static void __tg3_set_rx_mode(struct net_device *dev)
{
	struct tg3 *tp = netdev_priv(dev);
	u32 rx_mode;

	rx_mode = tp->rx_mode & ~(RX_MODE_PROMISC |
				  RX_MODE_KEEP_VLAN_TAG);

#if !defined(CONFIG_VLAN_8021Q) && !defined(CONFIG_VLAN_8021Q_MODULE)
	/* When ASF is in use, we always keep the RX_MODE_KEEP_VLAN_TAG
	 * flag clear.
	 */
	if (!tg3_flag(tp, ENABLE_ASF))
		rx_mode |= RX_MODE_KEEP_VLAN_TAG;
#endif

	if (dev->flags & IFF_PROMISC) {
		/* Promiscuous mode. */
		rx_mode |= RX_MODE_PROMISC;
	} else if (dev->flags & IFF_ALLMULTI) {
		/* Accept all multicast. */
		tg3_set_multi(tp, 1);
	} else if (netdev_mc_empty(dev)) {
		/* Reject all multicast. */
		tg3_set_multi(tp, 0);
	} else {
		/* Accept one or more multicast(s). */
		struct netdev_hw_addr *ha;
		u32 mc_filter[4] = { 0, };
		u32 regidx;
		u32 bit;
		u32 crc;

		netdev_for_each_mc_addr(ha, dev) {
			crc = calc_crc(ha->addr, ETH_ALEN);
			bit = ~crc & 0x7f;
			regidx = (bit & 0x60) >> 5;
			bit &= 0x1f;
			mc_filter[regidx] |= (1 << bit);
		}

		tw32(MAC_HASH_REG_0, mc_filter[0]);
		tw32(MAC_HASH_REG_1, mc_filter[1]);
		tw32(MAC_HASH_REG_2, mc_filter[2]);
		tw32(MAC_HASH_REG_3, mc_filter[3]);
	}

	if (rx_mode != tp->rx_mode) {
		tp->rx_mode = rx_mode;
		tw32_f(MAC_RX_MODE, rx_mode);
		udelay(10);
	}
}

static void tg3_rss_init_dflt_indir_tbl(struct tg3 *tp, u32 qcnt)
{
	int i;

	for (i = 0; i < TG3_RSS_INDIR_TBL_SIZE; i++)
		tp->rss_ind_tbl[i] = ethtool_rxfh_indir_default(i, qcnt);
}

static void tg3_rss_check_indir_tbl(struct tg3 *tp)
{
	int i;

	if (!tg3_flag(tp, SUPPORT_MSIX))
		return;

	if (tp->rxq_cnt == 1) {
		memset(&tp->rss_ind_tbl[0], 0, sizeof(tp->rss_ind_tbl));
		return;
	}

	/* Validate table against current IRQ count */
	for (i = 0; i < TG3_RSS_INDIR_TBL_SIZE; i++) {
		if (tp->rss_ind_tbl[i] >= tp->rxq_cnt)
			break;
	}

	if (i != TG3_RSS_INDIR_TBL_SIZE)
		tg3_rss_init_dflt_indir_tbl(tp, tp->rxq_cnt);
}

static void tg3_rss_write_indir_tbl(struct tg3 *tp)
{
	int i = 0;
	u32 reg = MAC_RSS_INDIR_TBL_0;

	while (i < TG3_RSS_INDIR_TBL_SIZE) {
		u32 val = tp->rss_ind_tbl[i];
		i++;
		for (; i % 8; i++) {
			val <<= 4;
			val |= tp->rss_ind_tbl[i];
		}
		tw32(reg, val);
		reg += 4;
	}
}

static inline u32 tg3_lso_rd_dma_workaround_bit(struct tg3 *tp)
{
	if (tg3_asic_rev(tp) == ASIC_REV_5719)
		return TG3_LSO_RD_DMA_TX_LENGTH_WA_5719;
	else
		return TG3_LSO_RD_DMA_TX_LENGTH_WA_5720;
}

/* tp->lock is held. */
static int tg3_reset_hw(struct tg3 *tp, bool reset_phy)
{
	u32 val, rdmac_mode;
	int i, err, limit;
	struct tg3_rx_prodring_set *tpr = &tp->napi[0].prodring;

	tg3_disable_ints(tp);

	tg3_stop_fw(tp);

	tg3_write_sig_pre_reset(tp, RESET_KIND_INIT);

	if (tg3_flag(tp, INIT_COMPLETE))
		tg3_abort_hw(tp, 1);

	/* Enable MAC control of LPI */
	if (tp->phy_flags & TG3_PHYFLG_EEE_CAP) {
		val = TG3_CPMU_EEE_LNKIDL_PCIE_NL0 |
		      TG3_CPMU_EEE_LNKIDL_UART_IDL;
		if (tg3_chip_rev_id(tp) == CHIPREV_ID_57765_A0)
			val |= TG3_CPMU_EEE_LNKIDL_APE_TX_MT;

		tw32_f(TG3_CPMU_EEE_LNKIDL_CTRL, val);

		tw32_f(TG3_CPMU_EEE_CTRL,
		       TG3_CPMU_EEE_CTRL_EXIT_20_1_US);

		val = TG3_CPMU_EEEMD_ERLY_L1_XIT_DET |
		      TG3_CPMU_EEEMD_LPI_IN_TX |
		      TG3_CPMU_EEEMD_LPI_IN_RX |
		      TG3_CPMU_EEEMD_EEE_ENABLE;

		if (tg3_asic_rev(tp) != ASIC_REV_5717)
			val |= TG3_CPMU_EEEMD_SND_IDX_DET_EN;

		if (tg3_flag(tp, ENABLE_APE))
			val |= TG3_CPMU_EEEMD_APE_TX_DET_EN;

		tw32_f(TG3_CPMU_EEE_MODE, val);

		tw32_f(TG3_CPMU_EEE_DBTMR1,
		       TG3_CPMU_DBTMR1_PCIEXIT_2047US |
		       TG3_CPMU_DBTMR1_LNKIDLE_2047US);

		tw32_f(TG3_CPMU_EEE_DBTMR2,
		       TG3_CPMU_DBTMR2_APE_TX_2047US |
		       TG3_CPMU_DBTMR2_TXIDXEQ_2047US);
	}

	if ((tp->phy_flags & TG3_PHYFLG_KEEP_LINK_ON_PWRDN) &&
	    !(tp->phy_flags & TG3_PHYFLG_USER_CONFIGURED)) {
		tg3_phy_pull_config(tp);
		tp->phy_flags |= TG3_PHYFLG_USER_CONFIGURED;
	}

	if (reset_phy)
		tg3_phy_reset(tp);

	err = tg3_chip_reset(tp);
	if (err)
		return err;

	tg3_write_sig_legacy(tp, RESET_KIND_INIT);

	if (tg3_chip_rev(tp) == CHIPREV_5784_AX) {
		val = tr32(TG3_CPMU_CTRL);
		val &= ~(CPMU_CTRL_LINK_AWARE_MODE | CPMU_CTRL_LINK_IDLE_MODE);
		tw32(TG3_CPMU_CTRL, val);

		val = tr32(TG3_CPMU_LSPD_10MB_CLK);
		val &= ~CPMU_LSPD_10MB_MACCLK_MASK;
		val |= CPMU_LSPD_10MB_MACCLK_6_25;
		tw32(TG3_CPMU_LSPD_10MB_CLK, val);

		val = tr32(TG3_CPMU_LNK_AWARE_PWRMD);
		val &= ~CPMU_LNK_AWARE_MACCLK_MASK;
		val |= CPMU_LNK_AWARE_MACCLK_6_25;
		tw32(TG3_CPMU_LNK_AWARE_PWRMD, val);

		val = tr32(TG3_CPMU_HST_ACC);
		val &= ~CPMU_HST_ACC_MACCLK_MASK;
		val |= CPMU_HST_ACC_MACCLK_6_25;
		tw32(TG3_CPMU_HST_ACC, val);
	}

	if (tg3_asic_rev(tp) == ASIC_REV_57780) {
		val = tr32(PCIE_PWR_MGMT_THRESH) & ~PCIE_PWR_MGMT_L1_THRESH_MSK;
		val |= PCIE_PWR_MGMT_EXT_ASPM_TMR_EN |
		       PCIE_PWR_MGMT_L1_THRESH_4MS;
		tw32(PCIE_PWR_MGMT_THRESH, val);

		val = tr32(TG3_PCIE_EIDLE_DELAY) & ~TG3_PCIE_EIDLE_DELAY_MASK;
		tw32(TG3_PCIE_EIDLE_DELAY, val | TG3_PCIE_EIDLE_DELAY_13_CLKS);

		tw32(TG3_CORR_ERR_STAT, TG3_CORR_ERR_STAT_CLEAR);

		val = tr32(TG3_PCIE_LNKCTL) & ~TG3_PCIE_LNKCTL_L1_PLL_PD_EN;
		tw32(TG3_PCIE_LNKCTL, val | TG3_PCIE_LNKCTL_L1_PLL_PD_DIS);
	}

	if (tg3_flag(tp, L1PLLPD_EN)) {
		u32 grc_mode = tr32(GRC_MODE);

		/* Access the lower 1K of PL PCIE block registers. */
		val = grc_mode & ~GRC_MODE_PCIE_PORT_MASK;
		tw32(GRC_MODE, val | GRC_MODE_PCIE_PL_SEL);

		val = tr32(TG3_PCIE_TLDLPL_PORT + TG3_PCIE_PL_LO_PHYCTL1);
		tw32(TG3_PCIE_TLDLPL_PORT + TG3_PCIE_PL_LO_PHYCTL1,
		     val | TG3_PCIE_PL_LO_PHYCTL1_L1PLLPD_EN);

		tw32(GRC_MODE, grc_mode);
	}

	if (tg3_flag(tp, 57765_CLASS)) {
		if (tg3_chip_rev_id(tp) == CHIPREV_ID_57765_A0) {
			u32 grc_mode = tr32(GRC_MODE);

			/* Access the lower 1K of PL PCIE block registers. */
			val = grc_mode & ~GRC_MODE_PCIE_PORT_MASK;
			tw32(GRC_MODE, val | GRC_MODE_PCIE_PL_SEL);

			val = tr32(TG3_PCIE_TLDLPL_PORT +
				   TG3_PCIE_PL_LO_PHYCTL5);
			tw32(TG3_PCIE_TLDLPL_PORT + TG3_PCIE_PL_LO_PHYCTL5,
			     val | TG3_PCIE_PL_LO_PHYCTL5_DIS_L2CLKREQ);

			tw32(GRC_MODE, grc_mode);
		}

		if (tg3_chip_rev(tp) != CHIPREV_57765_AX) {
			u32 grc_mode;

			/* Fix transmit hangs */
			val = tr32(TG3_CPMU_PADRNG_CTL);
			val |= TG3_CPMU_PADRNG_CTL_RDIV2;
			tw32(TG3_CPMU_PADRNG_CTL, val);

			grc_mode = tr32(GRC_MODE);

			/* Access the lower 1K of DL PCIE block registers. */
			val = grc_mode & ~GRC_MODE_PCIE_PORT_MASK;
			tw32(GRC_MODE, val | GRC_MODE_PCIE_DL_SEL);

			val = tr32(TG3_PCIE_TLDLPL_PORT +
				   TG3_PCIE_DL_LO_FTSMAX);
			val &= ~TG3_PCIE_DL_LO_FTSMAX_MSK;
			tw32(TG3_PCIE_TLDLPL_PORT + TG3_PCIE_DL_LO_FTSMAX,
			     val | TG3_PCIE_DL_LO_FTSMAX_VAL);

			tw32(GRC_MODE, grc_mode);
		}

		val = tr32(TG3_CPMU_LSPD_10MB_CLK);
		val &= ~CPMU_LSPD_10MB_MACCLK_MASK;
		val |= CPMU_LSPD_10MB_MACCLK_6_25;
		tw32(TG3_CPMU_LSPD_10MB_CLK, val);
	}

	/* This works around an issue with Athlon chipsets on
	 * B3 tigon3 silicon.  This bit has no effect on any
	 * other revision.  But do not set this on PCI Express
	 * chips and don't even touch the clocks if the CPMU is present.
	 */
	if (!tg3_flag(tp, CPMU_PRESENT)) {
		if (!tg3_flag(tp, PCI_EXPRESS))
			tp->pci_clock_ctrl |= CLOCK_CTRL_DELAY_PCI_GRANT;
		tw32_f(TG3PCI_CLOCK_CTRL, tp->pci_clock_ctrl);
	}

	if (tg3_chip_rev_id(tp) == CHIPREV_ID_5704_A0 &&
	    tg3_flag(tp, PCIX_MODE)) {
		val = tr32(TG3PCI_PCISTATE);
		val |= PCISTATE_RETRY_SAME_DMA;
		tw32(TG3PCI_PCISTATE, val);
	}

	if (tg3_flag(tp, ENABLE_APE)) {
		/* Allow reads and writes to the
		 * APE register and memory space.
		 */
		val = tr32(TG3PCI_PCISTATE);
		val |= PCISTATE_ALLOW_APE_CTLSPC_WR |
		       PCISTATE_ALLOW_APE_SHMEM_WR |
		       PCISTATE_ALLOW_APE_PSPACE_WR;
		tw32(TG3PCI_PCISTATE, val);
	}

	if (tg3_chip_rev(tp) == CHIPREV_5704_BX) {
		/* Enable some hw fixes.  */
		val = tr32(TG3PCI_MSI_DATA);
		val |= (1 << 26) | (1 << 28) | (1 << 29);
		tw32(TG3PCI_MSI_DATA, val);
	}

	/* Descriptor ring init may make accesses to the
	 * NIC SRAM area to setup the TX descriptors, so we
	 * can only do this after the hardware has been
	 * successfully reset.
	 */
	err = tg3_init_rings(tp);
	if (err)
		return err;

	if (tg3_flag(tp, 57765_PLUS)) {
		val = tr32(TG3PCI_DMA_RW_CTRL) &
		      ~DMA_RWCTRL_DIS_CACHE_ALIGNMENT;
		if (tg3_chip_rev_id(tp) == CHIPREV_ID_57765_A0)
			val &= ~DMA_RWCTRL_CRDRDR_RDMA_MRRS_MSK;
		if (!tg3_flag(tp, 57765_CLASS) &&
		    tg3_asic_rev(tp) != ASIC_REV_5717 &&
		    tg3_asic_rev(tp) != ASIC_REV_5762)
			val |= DMA_RWCTRL_TAGGED_STAT_WA;
		tw32(TG3PCI_DMA_RW_CTRL, val | tp->dma_rwctrl);
	} else if (tg3_asic_rev(tp) != ASIC_REV_5784 &&
		   tg3_asic_rev(tp) != ASIC_REV_5761) {
		/* This value is determined during the probe time DMA
		 * engine test, tg3_test_dma.
		 */
		tw32(TG3PCI_DMA_RW_CTRL, tp->dma_rwctrl);
	}

	tp->grc_mode &= ~(GRC_MODE_HOST_SENDBDS |
			  GRC_MODE_4X_NIC_SEND_RINGS |
			  GRC_MODE_NO_TX_PHDR_CSUM |
			  GRC_MODE_NO_RX_PHDR_CSUM);
	tp->grc_mode |= GRC_MODE_HOST_SENDBDS;

	/* Pseudo-header checksum is done by hardware logic and not
	 * the offload processers, so make the chip do the pseudo-
	 * header checksums on receive.  For transmit it is more
	 * convenient to do the pseudo-header checksum in software
	 * as Linux does that on transmit for us in all cases.
	 */
	tp->grc_mode |= GRC_MODE_NO_TX_PHDR_CSUM;

	val = GRC_MODE_IRQ_ON_MAC_ATTN | GRC_MODE_HOST_STACKUP;
	if (tp->rxptpctl)
		tw32(TG3_RX_PTP_CTL,
		     tp->rxptpctl | TG3_RX_PTP_CTL_HWTS_INTERLOCK);

	if (tg3_flag(tp, PTP_CAPABLE))
		val |= GRC_MODE_TIME_SYNC_ENABLE;

	tw32(GRC_MODE, tp->grc_mode | val);

	/* Setup the timer prescalar register.  Clock is always 66Mhz. */
	val = tr32(GRC_MISC_CFG);
	val &= ~0xff;
	val |= (65 << GRC_MISC_CFG_PRESCALAR_SHIFT);
	tw32(GRC_MISC_CFG, val);

	/* Initialize MBUF/DESC pool. */
	if (tg3_flag(tp, 5750_PLUS)) {
		/* Do nothing.  */
	} else if (tg3_asic_rev(tp) != ASIC_REV_5705) {
		tw32(BUFMGR_MB_POOL_ADDR, NIC_SRAM_MBUF_POOL_BASE);
		if (tg3_asic_rev(tp) == ASIC_REV_5704)
			tw32(BUFMGR_MB_POOL_SIZE, NIC_SRAM_MBUF_POOL_SIZE64);
		else
			tw32(BUFMGR_MB_POOL_SIZE, NIC_SRAM_MBUF_POOL_SIZE96);
		tw32(BUFMGR_DMA_DESC_POOL_ADDR, NIC_SRAM_DMA_DESC_POOL_BASE);
		tw32(BUFMGR_DMA_DESC_POOL_SIZE, NIC_SRAM_DMA_DESC_POOL_SIZE);
	} else if (tg3_flag(tp, TSO_CAPABLE)) {
		int fw_len;

		fw_len = tp->fw_len;
		fw_len = (fw_len + (0x80 - 1)) & ~(0x80 - 1);
		tw32(BUFMGR_MB_POOL_ADDR,
		     NIC_SRAM_MBUF_POOL_BASE5705 + fw_len);
		tw32(BUFMGR_MB_POOL_SIZE,
		     NIC_SRAM_MBUF_POOL_SIZE5705 - fw_len - 0xa00);
	}

	if (tp->dev->mtu <= ETH_DATA_LEN) {
		tw32(BUFMGR_MB_RDMA_LOW_WATER,
		     tp->bufmgr_config.mbuf_read_dma_low_water);
		tw32(BUFMGR_MB_MACRX_LOW_WATER,
		     tp->bufmgr_config.mbuf_mac_rx_low_water);
		tw32(BUFMGR_MB_HIGH_WATER,
		     tp->bufmgr_config.mbuf_high_water);
	} else {
		tw32(BUFMGR_MB_RDMA_LOW_WATER,
		     tp->bufmgr_config.mbuf_read_dma_low_water_jumbo);
		tw32(BUFMGR_MB_MACRX_LOW_WATER,
		     tp->bufmgr_config.mbuf_mac_rx_low_water_jumbo);
		tw32(BUFMGR_MB_HIGH_WATER,
		     tp->bufmgr_config.mbuf_high_water_jumbo);
	}
	tw32(BUFMGR_DMA_LOW_WATER,
	     tp->bufmgr_config.dma_low_water);
	tw32(BUFMGR_DMA_HIGH_WATER,
	     tp->bufmgr_config.dma_high_water);

	val = BUFMGR_MODE_ENABLE | BUFMGR_MODE_ATTN_ENABLE;
	if (tg3_asic_rev(tp) == ASIC_REV_5719)
		val |= BUFMGR_MODE_NO_TX_UNDERRUN;
	if (tg3_asic_rev(tp) == ASIC_REV_5717 ||
	    tg3_chip_rev_id(tp) == CHIPREV_ID_5719_A0 ||
	    tg3_chip_rev_id(tp) == CHIPREV_ID_5720_A0)
		val |= BUFMGR_MODE_MBLOW_ATTN_ENAB;
	tw32(BUFMGR_MODE, val);
	for (i = 0; i < 2000; i++) {
		if (tr32(BUFMGR_MODE) & BUFMGR_MODE_ENABLE)
			break;
		udelay(10);
	}
	if (i >= 2000) {
		netdev_err(tp->dev, "%s cannot enable BUFMGR\n", __func__);
		return -ENODEV;
	}

	if (tg3_chip_rev_id(tp) == CHIPREV_ID_5906_A1)
		tw32(ISO_PKT_TX, (tr32(ISO_PKT_TX) & ~0x3) | 0x2);

	tg3_setup_rxbd_thresholds(tp);

	/* Initialize TG3_BDINFO's at:
	 *  RCVDBDI_STD_BD:	standard eth size rx ring
	 *  RCVDBDI_JUMBO_BD:	jumbo frame rx ring
	 *  RCVDBDI_MINI_BD:	small frame rx ring (??? does not work)
	 *
	 * like so:
	 *  TG3_BDINFO_HOST_ADDR:	high/low parts of DMA address of ring
	 *  TG3_BDINFO_MAXLEN_FLAGS:	(rx max buffer size << 16) |
	 *                              ring attribute flags
	 *  TG3_BDINFO_NIC_ADDR:	location of descriptors in nic SRAM
	 *
	 * Standard receive ring @ NIC_SRAM_RX_BUFFER_DESC, 512 entries.
	 * Jumbo receive ring @ NIC_SRAM_RX_JUMBO_BUFFER_DESC, 256 entries.
	 *
	 * The size of each ring is fixed in the firmware, but the location is
	 * configurable.
	 */
	tw32(RCVDBDI_STD_BD + TG3_BDINFO_HOST_ADDR + TG3_64BIT_REG_HIGH,
	     ((u64) tpr->rx_std_mapping >> 32));
	tw32(RCVDBDI_STD_BD + TG3_BDINFO_HOST_ADDR + TG3_64BIT_REG_LOW,
	     ((u64) tpr->rx_std_mapping & 0xffffffff));
	if (!tg3_flag(tp, 5717_PLUS))
		tw32(RCVDBDI_STD_BD + TG3_BDINFO_NIC_ADDR,
		     NIC_SRAM_RX_BUFFER_DESC);

	/* Disable the mini ring */
	if (!tg3_flag(tp, 5705_PLUS))
		tw32(RCVDBDI_MINI_BD + TG3_BDINFO_MAXLEN_FLAGS,
		     BDINFO_FLAGS_DISABLED);

	/* Program the jumbo buffer descriptor ring control
	 * blocks on those devices that have them.
	 */
	if (tg3_chip_rev_id(tp) == CHIPREV_ID_5719_A0 ||
	    (tg3_flag(tp, JUMBO_CAPABLE) && !tg3_flag(tp, 5780_CLASS))) {

		if (tg3_flag(tp, JUMBO_RING_ENABLE)) {
			tw32(RCVDBDI_JUMBO_BD + TG3_BDINFO_HOST_ADDR + TG3_64BIT_REG_HIGH,
			     ((u64) tpr->rx_jmb_mapping >> 32));
			tw32(RCVDBDI_JUMBO_BD + TG3_BDINFO_HOST_ADDR + TG3_64BIT_REG_LOW,
			     ((u64) tpr->rx_jmb_mapping & 0xffffffff));
			val = TG3_RX_JMB_RING_SIZE(tp) <<
			      BDINFO_FLAGS_MAXLEN_SHIFT;
			tw32(RCVDBDI_JUMBO_BD + TG3_BDINFO_MAXLEN_FLAGS,
			     val | BDINFO_FLAGS_USE_EXT_RECV);
			if (!tg3_flag(tp, USE_JUMBO_BDFLAG) ||
			    tg3_flag(tp, 57765_CLASS) ||
			    tg3_asic_rev(tp) == ASIC_REV_5762)
				tw32(RCVDBDI_JUMBO_BD + TG3_BDINFO_NIC_ADDR,
				     NIC_SRAM_RX_JUMBO_BUFFER_DESC);
		} else {
			tw32(RCVDBDI_JUMBO_BD + TG3_BDINFO_MAXLEN_FLAGS,
			     BDINFO_FLAGS_DISABLED);
		}

		if (tg3_flag(tp, 57765_PLUS)) {
			val = TG3_RX_STD_RING_SIZE(tp);
			val <<= BDINFO_FLAGS_MAXLEN_SHIFT;
			val |= (TG3_RX_STD_DMA_SZ << 2);
		} else
			val = TG3_RX_STD_DMA_SZ << BDINFO_FLAGS_MAXLEN_SHIFT;
	} else
		val = TG3_RX_STD_MAX_SIZE_5700 << BDINFO_FLAGS_MAXLEN_SHIFT;

	tw32(RCVDBDI_STD_BD + TG3_BDINFO_MAXLEN_FLAGS, val);

	tpr->rx_std_prod_idx = tp->rx_pending;
	tw32_rx_mbox(TG3_RX_STD_PROD_IDX_REG, tpr->rx_std_prod_idx);

	tpr->rx_jmb_prod_idx =
		tg3_flag(tp, JUMBO_RING_ENABLE) ? tp->rx_jumbo_pending : 0;
	tw32_rx_mbox(TG3_RX_JMB_PROD_IDX_REG, tpr->rx_jmb_prod_idx);

	tg3_rings_reset(tp);

	/* Initialize MAC address and backoff seed. */
	__tg3_set_mac_addr(tp, false);

	/* MTU + ethernet header + FCS + optional VLAN tag */
	tw32(MAC_RX_MTU_SIZE,
	     tp->dev->mtu + ETH_HLEN + ETH_FCS_LEN + VLAN_HLEN);

	/* The slot time is changed by tg3_setup_phy if we
	 * run at gigabit with half duplex.
	 */
	val = (2 << TX_LENGTHS_IPG_CRS_SHIFT) |
	      (6 << TX_LENGTHS_IPG_SHIFT) |
	      (32 << TX_LENGTHS_SLOT_TIME_SHIFT);

	if (tg3_asic_rev(tp) == ASIC_REV_5720 ||
	    tg3_asic_rev(tp) == ASIC_REV_5762)
		val |= tr32(MAC_TX_LENGTHS) &
		       (TX_LENGTHS_JMB_FRM_LEN_MSK |
			TX_LENGTHS_CNT_DWN_VAL_MSK);

	tw32(MAC_TX_LENGTHS, val);

	/* Receive rules. */
	tw32(MAC_RCV_RULE_CFG, RCV_RULE_CFG_DEFAULT_CLASS);
	tw32(RCVLPC_CONFIG, 0x0181);

	/* Calculate RDMAC_MODE setting early, we need it to determine
	 * the RCVLPC_STATE_ENABLE mask.
	 */
	rdmac_mode = (RDMAC_MODE_ENABLE | RDMAC_MODE_TGTABORT_ENAB |
		      RDMAC_MODE_MSTABORT_ENAB | RDMAC_MODE_PARITYERR_ENAB |
		      RDMAC_MODE_ADDROFLOW_ENAB | RDMAC_MODE_FIFOOFLOW_ENAB |
		      RDMAC_MODE_FIFOURUN_ENAB | RDMAC_MODE_FIFOOREAD_ENAB |
		      RDMAC_MODE_LNGREAD_ENAB);

	if (tg3_asic_rev(tp) == ASIC_REV_5717)
		rdmac_mode |= RDMAC_MODE_MULT_DMA_RD_DIS;

	if (tg3_asic_rev(tp) == ASIC_REV_5784 ||
	    tg3_asic_rev(tp) == ASIC_REV_5785 ||
	    tg3_asic_rev(tp) == ASIC_REV_57780)
		rdmac_mode |= RDMAC_MODE_BD_SBD_CRPT_ENAB |
			      RDMAC_MODE_MBUF_RBD_CRPT_ENAB |
			      RDMAC_MODE_MBUF_SBD_CRPT_ENAB;

	if (tg3_asic_rev(tp) == ASIC_REV_5705 &&
	    tg3_chip_rev_id(tp) != CHIPREV_ID_5705_A0) {
		if (tg3_flag(tp, TSO_CAPABLE) &&
		    tg3_asic_rev(tp) == ASIC_REV_5705) {
			rdmac_mode |= RDMAC_MODE_FIFO_SIZE_128;
		} else if (!(tr32(TG3PCI_PCISTATE) & PCISTATE_BUS_SPEED_HIGH) &&
			   !tg3_flag(tp, IS_5788)) {
			rdmac_mode |= RDMAC_MODE_FIFO_LONG_BURST;
		}
	}

	if (tg3_flag(tp, PCI_EXPRESS))
		rdmac_mode |= RDMAC_MODE_FIFO_LONG_BURST;

	if (tg3_asic_rev(tp) == ASIC_REV_57766) {
		tp->dma_limit = 0;
		if (tp->dev->mtu <= ETH_DATA_LEN) {
			rdmac_mode |= RDMAC_MODE_JMB_2K_MMRR;
			tp->dma_limit = TG3_TX_BD_DMA_MAX_2K;
		}
	}

	if (tg3_flag(tp, HW_TSO_1) ||
	    tg3_flag(tp, HW_TSO_2) ||
	    tg3_flag(tp, HW_TSO_3))
		rdmac_mode |= RDMAC_MODE_IPV4_LSO_EN;

	if (tg3_flag(tp, 57765_PLUS) ||
	    tg3_asic_rev(tp) == ASIC_REV_5785 ||
	    tg3_asic_rev(tp) == ASIC_REV_57780)
		rdmac_mode |= RDMAC_MODE_IPV6_LSO_EN;

	if (tg3_asic_rev(tp) == ASIC_REV_5720 ||
	    tg3_asic_rev(tp) == ASIC_REV_5762)
		rdmac_mode |= tr32(RDMAC_MODE) & RDMAC_MODE_H2BNC_VLAN_DET;

	if (tg3_asic_rev(tp) == ASIC_REV_5761 ||
	    tg3_asic_rev(tp) == ASIC_REV_5784 ||
	    tg3_asic_rev(tp) == ASIC_REV_5785 ||
	    tg3_asic_rev(tp) == ASIC_REV_57780 ||
	    tg3_flag(tp, 57765_PLUS)) {
		u32 tgtreg;

		if (tg3_asic_rev(tp) == ASIC_REV_5762)
			tgtreg = TG3_RDMA_RSRVCTRL_REG2;
		else
			tgtreg = TG3_RDMA_RSRVCTRL_REG;

		val = tr32(tgtreg);
		if (tg3_chip_rev_id(tp) == CHIPREV_ID_5719_A0 ||
		    tg3_asic_rev(tp) == ASIC_REV_5762) {
			val &= ~(TG3_RDMA_RSRVCTRL_TXMRGN_MASK |
				 TG3_RDMA_RSRVCTRL_FIFO_LWM_MASK |
				 TG3_RDMA_RSRVCTRL_FIFO_HWM_MASK);
			val |= TG3_RDMA_RSRVCTRL_TXMRGN_320B |
			       TG3_RDMA_RSRVCTRL_FIFO_LWM_1_5K |
			       TG3_RDMA_RSRVCTRL_FIFO_HWM_1_5K;
		}
		tw32(tgtreg, val | TG3_RDMA_RSRVCTRL_FIFO_OFLW_FIX);
	}

	if (tg3_asic_rev(tp) == ASIC_REV_5719 ||
	    tg3_asic_rev(tp) == ASIC_REV_5720 ||
	    tg3_asic_rev(tp) == ASIC_REV_5762) {
		u32 tgtreg;

		if (tg3_asic_rev(tp) == ASIC_REV_5762)
			tgtreg = TG3_LSO_RD_DMA_CRPTEN_CTRL2;
		else
			tgtreg = TG3_LSO_RD_DMA_CRPTEN_CTRL;

		val = tr32(tgtreg);
		tw32(tgtreg, val |
		     TG3_LSO_RD_DMA_CRPTEN_CTRL_BLEN_BD_4K |
		     TG3_LSO_RD_DMA_CRPTEN_CTRL_BLEN_LSO_4K);
	}

	/* Receive/send statistics. */
	if (tg3_flag(tp, 5750_PLUS)) {
		val = tr32(RCVLPC_STATS_ENABLE);
		val &= ~RCVLPC_STATSENAB_DACK_FIX;
		tw32(RCVLPC_STATS_ENABLE, val);
	} else if ((rdmac_mode & RDMAC_MODE_FIFO_SIZE_128) &&
		   tg3_flag(tp, TSO_CAPABLE)) {
		val = tr32(RCVLPC_STATS_ENABLE);
		val &= ~RCVLPC_STATSENAB_LNGBRST_RFIX;
		tw32(RCVLPC_STATS_ENABLE, val);
	} else {
		tw32(RCVLPC_STATS_ENABLE, 0xffffff);
	}
	tw32(RCVLPC_STATSCTRL, RCVLPC_STATSCTRL_ENABLE);
	tw32(SNDDATAI_STATSENAB, 0xffffff);
	tw32(SNDDATAI_STATSCTRL,
	     (SNDDATAI_SCTRL_ENABLE |
	      SNDDATAI_SCTRL_FASTUPD));

	/* Setup host coalescing engine. */
	tw32(HOSTCC_MODE, 0);
	for (i = 0; i < 2000; i++) {
		if (!(tr32(HOSTCC_MODE) & HOSTCC_MODE_ENABLE))
			break;
		udelay(10);
	}

	__tg3_set_coalesce(tp, &tp->coal);

	if (!tg3_flag(tp, 5705_PLUS)) {
		/* Status/statistics block address.  See tg3_timer,
		 * the tg3_periodic_fetch_stats call there, and
		 * tg3_get_stats to see how this works for 5705/5750 chips.
		 */
		tw32(HOSTCC_STATS_BLK_HOST_ADDR + TG3_64BIT_REG_HIGH,
		     ((u64) tp->stats_mapping >> 32));
		tw32(HOSTCC_STATS_BLK_HOST_ADDR + TG3_64BIT_REG_LOW,
		     ((u64) tp->stats_mapping & 0xffffffff));
		tw32(HOSTCC_STATS_BLK_NIC_ADDR, NIC_SRAM_STATS_BLK);

		tw32(HOSTCC_STATUS_BLK_NIC_ADDR, NIC_SRAM_STATUS_BLK);

		/* Clear statistics and status block memory areas */
		for (i = NIC_SRAM_STATS_BLK;
		     i < NIC_SRAM_STATUS_BLK + TG3_HW_STATUS_SIZE;
		     i += sizeof(u32)) {
			tg3_write_mem(tp, i, 0);
			udelay(40);
		}
	}

	tw32(HOSTCC_MODE, HOSTCC_MODE_ENABLE | tp->coalesce_mode);

	tw32(RCVCC_MODE, RCVCC_MODE_ENABLE | RCVCC_MODE_ATTN_ENABLE);
	tw32(RCVLPC_MODE, RCVLPC_MODE_ENABLE);
	if (!tg3_flag(tp, 5705_PLUS))
		tw32(RCVLSC_MODE, RCVLSC_MODE_ENABLE | RCVLSC_MODE_ATTN_ENABLE);

	if (tp->phy_flags & TG3_PHYFLG_MII_SERDES) {
		tp->phy_flags &= ~TG3_PHYFLG_PARALLEL_DETECT;
		/* reset to prevent losing 1st rx packet intermittently */
		tw32_f(MAC_RX_MODE, RX_MODE_RESET);
		udelay(10);
	}

	tp->mac_mode |= MAC_MODE_TXSTAT_ENABLE | MAC_MODE_RXSTAT_ENABLE |
			MAC_MODE_TDE_ENABLE | MAC_MODE_RDE_ENABLE |
			MAC_MODE_FHDE_ENABLE;
	if (tg3_flag(tp, ENABLE_APE))
		tp->mac_mode |= MAC_MODE_APE_TX_EN | MAC_MODE_APE_RX_EN;
	if (!tg3_flag(tp, 5705_PLUS) &&
	    !(tp->phy_flags & TG3_PHYFLG_PHY_SERDES) &&
	    tg3_asic_rev(tp) != ASIC_REV_5700)
		tp->mac_mode |= MAC_MODE_LINK_POLARITY;
	tw32_f(MAC_MODE, tp->mac_mode | MAC_MODE_RXSTAT_CLEAR | MAC_MODE_TXSTAT_CLEAR);
	udelay(40);

	/* tp->grc_local_ctrl is partially set up during tg3_get_invariants().
	 * If TG3_FLAG_IS_NIC is zero, we should read the
	 * register to preserve the GPIO settings for LOMs. The GPIOs,
	 * whether used as inputs or outputs, are set by boot code after
	 * reset.
	 */
	if (!tg3_flag(tp, IS_NIC)) {
		u32 gpio_mask;

		gpio_mask = GRC_LCLCTRL_GPIO_OE0 | GRC_LCLCTRL_GPIO_OE1 |
			    GRC_LCLCTRL_GPIO_OE2 | GRC_LCLCTRL_GPIO_OUTPUT0 |
			    GRC_LCLCTRL_GPIO_OUTPUT1 | GRC_LCLCTRL_GPIO_OUTPUT2;

		if (tg3_asic_rev(tp) == ASIC_REV_5752)
			gpio_mask |= GRC_LCLCTRL_GPIO_OE3 |
				     GRC_LCLCTRL_GPIO_OUTPUT3;

		if (tg3_asic_rev(tp) == ASIC_REV_5755)
			gpio_mask |= GRC_LCLCTRL_GPIO_UART_SEL;

		tp->grc_local_ctrl &= ~gpio_mask;
		tp->grc_local_ctrl |= tr32(GRC_LOCAL_CTRL) & gpio_mask;

		/* GPIO1 must be driven high for eeprom write protect */
		if (tg3_flag(tp, EEPROM_WRITE_PROT))
			tp->grc_local_ctrl |= (GRC_LCLCTRL_GPIO_OE1 |
					       GRC_LCLCTRL_GPIO_OUTPUT1);
	}
	tw32_f(GRC_LOCAL_CTRL, tp->grc_local_ctrl);
	udelay(100);

	if (tg3_flag(tp, USING_MSIX)) {
		val = tr32(MSGINT_MODE);
		val |= MSGINT_MODE_ENABLE;
		if (tp->irq_cnt > 1)
			val |= MSGINT_MODE_MULTIVEC_EN;
		if (!tg3_flag(tp, 1SHOT_MSI))
			val |= MSGINT_MODE_ONE_SHOT_DISABLE;
		tw32(MSGINT_MODE, val);
	}

	if (!tg3_flag(tp, 5705_PLUS)) {
		tw32_f(DMAC_MODE, DMAC_MODE_ENABLE);
		udelay(40);
	}

	val = (WDMAC_MODE_ENABLE | WDMAC_MODE_TGTABORT_ENAB |
	       WDMAC_MODE_MSTABORT_ENAB | WDMAC_MODE_PARITYERR_ENAB |
	       WDMAC_MODE_ADDROFLOW_ENAB | WDMAC_MODE_FIFOOFLOW_ENAB |
	       WDMAC_MODE_FIFOURUN_ENAB | WDMAC_MODE_FIFOOREAD_ENAB |
	       WDMAC_MODE_LNGREAD_ENAB);

	if (tg3_asic_rev(tp) == ASIC_REV_5705 &&
	    tg3_chip_rev_id(tp) != CHIPREV_ID_5705_A0) {
		if (tg3_flag(tp, TSO_CAPABLE) &&
		    (tg3_chip_rev_id(tp) == CHIPREV_ID_5705_A1 ||
		     tg3_chip_rev_id(tp) == CHIPREV_ID_5705_A2)) {
			/* nothing */
		} else if (!(tr32(TG3PCI_PCISTATE) & PCISTATE_BUS_SPEED_HIGH) &&
			   !tg3_flag(tp, IS_5788)) {
			val |= WDMAC_MODE_RX_ACCEL;
		}
	}

	/* Enable host coalescing bug fix */
	if (tg3_flag(tp, 5755_PLUS))
		val |= WDMAC_MODE_STATUS_TAG_FIX;

	if (tg3_asic_rev(tp) == ASIC_REV_5785)
		val |= WDMAC_MODE_BURST_ALL_DATA;

	tw32_f(WDMAC_MODE, val);
	udelay(40);

	if (tg3_flag(tp, PCIX_MODE)) {
		u16 pcix_cmd;

		pci_read_config_word(tp->pdev, tp->pcix_cap + PCI_X_CMD,
				     &pcix_cmd);
		if (tg3_asic_rev(tp) == ASIC_REV_5703) {
			pcix_cmd &= ~PCI_X_CMD_MAX_READ;
			pcix_cmd |= PCI_X_CMD_READ_2K;
		} else if (tg3_asic_rev(tp) == ASIC_REV_5704) {
			pcix_cmd &= ~(PCI_X_CMD_MAX_SPLIT | PCI_X_CMD_MAX_READ);
			pcix_cmd |= PCI_X_CMD_READ_2K;
		}
		pci_write_config_word(tp->pdev, tp->pcix_cap + PCI_X_CMD,
				      pcix_cmd);
	}

	tw32_f(RDMAC_MODE, rdmac_mode);
	udelay(40);

	if (tg3_asic_rev(tp) == ASIC_REV_5719 ||
	    tg3_asic_rev(tp) == ASIC_REV_5720) {
		for (i = 0; i < TG3_NUM_RDMA_CHANNELS; i++) {
			if (tr32(TG3_RDMA_LENGTH + (i << 2)) > TG3_MAX_MTU(tp))
				break;
		}
		if (i < TG3_NUM_RDMA_CHANNELS) {
			val = tr32(TG3_LSO_RD_DMA_CRPTEN_CTRL);
			val |= tg3_lso_rd_dma_workaround_bit(tp);
			tw32(TG3_LSO_RD_DMA_CRPTEN_CTRL, val);
			tg3_flag_set(tp, 5719_5720_RDMA_BUG);
		}
	}

	tw32(RCVDCC_MODE, RCVDCC_MODE_ENABLE | RCVDCC_MODE_ATTN_ENABLE);
	if (!tg3_flag(tp, 5705_PLUS))
		tw32(MBFREE_MODE, MBFREE_MODE_ENABLE);

	if (tg3_asic_rev(tp) == ASIC_REV_5761)
		tw32(SNDDATAC_MODE,
		     SNDDATAC_MODE_ENABLE | SNDDATAC_MODE_CDELAY);
	else
		tw32(SNDDATAC_MODE, SNDDATAC_MODE_ENABLE);

	tw32(SNDBDC_MODE, SNDBDC_MODE_ENABLE | SNDBDC_MODE_ATTN_ENABLE);
	tw32(RCVBDI_MODE, RCVBDI_MODE_ENABLE | RCVBDI_MODE_RCB_ATTN_ENAB);
	val = RCVDBDI_MODE_ENABLE | RCVDBDI_MODE_INV_RING_SZ;
	if (tg3_flag(tp, LRG_PROD_RING_CAP))
		val |= RCVDBDI_MODE_LRG_RING_SZ;
	tw32(RCVDBDI_MODE, val);
	tw32(SNDDATAI_MODE, SNDDATAI_MODE_ENABLE);
	if (tg3_flag(tp, HW_TSO_1) ||
	    tg3_flag(tp, HW_TSO_2) ||
	    tg3_flag(tp, HW_TSO_3))
		tw32(SNDDATAI_MODE, SNDDATAI_MODE_ENABLE | 0x8);
	val = SNDBDI_MODE_ENABLE | SNDBDI_MODE_ATTN_ENABLE;
	if (tg3_flag(tp, ENABLE_TSS))
		val |= SNDBDI_MODE_MULTI_TXQ_EN;
	tw32(SNDBDI_MODE, val);
	tw32(SNDBDS_MODE, SNDBDS_MODE_ENABLE | SNDBDS_MODE_ATTN_ENABLE);

	if (tg3_chip_rev_id(tp) == CHIPREV_ID_5701_A0) {
		err = tg3_load_5701_a0_firmware_fix(tp);
		if (err)
			return err;
	}

	if (tg3_asic_rev(tp) == ASIC_REV_57766) {
		/* Ignore any errors for the firmware download. If download
		 * fails, the device will operate with EEE disabled
		 */
		tg3_load_57766_firmware(tp);
	}

	if (tg3_flag(tp, TSO_CAPABLE)) {
		err = tg3_load_tso_firmware(tp);
		if (err)
			return err;
	}

	tp->tx_mode = TX_MODE_ENABLE;

	if (tg3_flag(tp, 5755_PLUS) ||
	    tg3_asic_rev(tp) == ASIC_REV_5906)
		tp->tx_mode |= TX_MODE_MBUF_LOCKUP_FIX;

	if (tg3_asic_rev(tp) == ASIC_REV_5720 ||
	    tg3_asic_rev(tp) == ASIC_REV_5762) {
		val = TX_MODE_JMB_FRM_LEN | TX_MODE_CNT_DN_MODE;
		tp->tx_mode &= ~val;
		tp->tx_mode |= tr32(MAC_TX_MODE) & val;
	}

	tw32_f(MAC_TX_MODE, tp->tx_mode);
	udelay(100);

	if (tg3_flag(tp, ENABLE_RSS)) {
		tg3_rss_write_indir_tbl(tp);

		/* Setup the "secret" hash key. */
		tw32(MAC_RSS_HASH_KEY_0, 0x5f865437);
		tw32(MAC_RSS_HASH_KEY_1, 0xe4ac62cc);
		tw32(MAC_RSS_HASH_KEY_2, 0x50103a45);
		tw32(MAC_RSS_HASH_KEY_3, 0x36621985);
		tw32(MAC_RSS_HASH_KEY_4, 0xbf14c0e8);
		tw32(MAC_RSS_HASH_KEY_5, 0x1bc27a1e);
		tw32(MAC_RSS_HASH_KEY_6, 0x84f4b556);
		tw32(MAC_RSS_HASH_KEY_7, 0x094ea6fe);
		tw32(MAC_RSS_HASH_KEY_8, 0x7dda01e7);
		tw32(MAC_RSS_HASH_KEY_9, 0xc04d7481);
	}

	tp->rx_mode = RX_MODE_ENABLE;
	if (tg3_flag(tp, 5755_PLUS))
		tp->rx_mode |= RX_MODE_IPV6_CSUM_ENABLE;

	if (tg3_flag(tp, ENABLE_RSS))
		tp->rx_mode |= RX_MODE_RSS_ENABLE |
			       RX_MODE_RSS_ITBL_HASH_BITS_7 |
			       RX_MODE_RSS_IPV6_HASH_EN |
			       RX_MODE_RSS_TCP_IPV6_HASH_EN |
			       RX_MODE_RSS_IPV4_HASH_EN |
			       RX_MODE_RSS_TCP_IPV4_HASH_EN;

	tw32_f(MAC_RX_MODE, tp->rx_mode);
	udelay(10);

	tw32(MAC_LED_CTRL, tp->led_ctrl);

	tw32(MAC_MI_STAT, MAC_MI_STAT_LNKSTAT_ATTN_ENAB);
	if (tp->phy_flags & TG3_PHYFLG_PHY_SERDES) {
		tw32_f(MAC_RX_MODE, RX_MODE_RESET);
		udelay(10);
	}
	tw32_f(MAC_RX_MODE, tp->rx_mode);
	udelay(10);

	if (tp->phy_flags & TG3_PHYFLG_PHY_SERDES) {
		if ((tg3_asic_rev(tp) == ASIC_REV_5704) &&
		    !(tp->phy_flags & TG3_PHYFLG_SERDES_PREEMPHASIS)) {
			/* Set drive transmission level to 1.2V  */
			/* only if the signal pre-emphasis bit is not set  */
			val = tr32(MAC_SERDES_CFG);
			val &= 0xfffff000;
			val |= 0x880;
			tw32(MAC_SERDES_CFG, val);
		}
		if (tg3_chip_rev_id(tp) == CHIPREV_ID_5703_A1)
			tw32(MAC_SERDES_CFG, 0x616000);
	}

	/* Prevent chip from dropping frames when flow control
	 * is enabled.
	 */
	if (tg3_flag(tp, 57765_CLASS))
		val = 1;
	else
		val = 2;
	tw32_f(MAC_LOW_WMARK_MAX_RX_FRAME, val);

	if (tg3_asic_rev(tp) == ASIC_REV_5704 &&
	    (tp->phy_flags & TG3_PHYFLG_PHY_SERDES)) {
		/* Use hardware link auto-negotiation */
		tg3_flag_set(tp, HW_AUTONEG);
	}

	if ((tp->phy_flags & TG3_PHYFLG_MII_SERDES) &&
	    tg3_asic_rev(tp) == ASIC_REV_5714) {
		u32 tmp;

		tmp = tr32(SERDES_RX_CTRL);
		tw32(SERDES_RX_CTRL, tmp | SERDES_RX_SIG_DETECT);
		tp->grc_local_ctrl &= ~GRC_LCLCTRL_USE_EXT_SIG_DETECT;
		tp->grc_local_ctrl |= GRC_LCLCTRL_USE_SIG_DETECT;
		tw32(GRC_LOCAL_CTRL, tp->grc_local_ctrl);
	}

	if (!tg3_flag(tp, USE_PHYLIB)) {
		if (tp->phy_flags & TG3_PHYFLG_IS_LOW_POWER)
			tp->phy_flags &= ~TG3_PHYFLG_IS_LOW_POWER;

		err = tg3_setup_phy(tp, false);
		if (err)
			return err;

		if (!(tp->phy_flags & TG3_PHYFLG_PHY_SERDES) &&
		    !(tp->phy_flags & TG3_PHYFLG_IS_FET)) {
			u32 tmp;

			/* Clear CRC stats. */
			if (!tg3_readphy(tp, MII_TG3_TEST1, &tmp)) {
				tg3_writephy(tp, MII_TG3_TEST1,
					     tmp | MII_TG3_TEST1_CRC_EN);
				tg3_readphy(tp, MII_TG3_RXR_COUNTERS, &tmp);
			}
		}
	}

	__tg3_set_rx_mode(tp->dev);

	/* Initialize receive rules. */
	tw32(MAC_RCV_RULE_0,  0xc2000000 & RCV_RULE_DISABLE_MASK);
	tw32(MAC_RCV_VALUE_0, 0xffffffff & RCV_RULE_DISABLE_MASK);
	tw32(MAC_RCV_RULE_1,  0x86000004 & RCV_RULE_DISABLE_MASK);
	tw32(MAC_RCV_VALUE_1, 0xffffffff & RCV_RULE_DISABLE_MASK);

	if (tg3_flag(tp, 5705_PLUS) && !tg3_flag(tp, 5780_CLASS))
		limit = 8;
	else
		limit = 16;
	if (tg3_flag(tp, ENABLE_ASF))
		limit -= 4;
	switch (limit) {
	case 16:
		tw32(MAC_RCV_RULE_15,  0); tw32(MAC_RCV_VALUE_15,  0);
	case 15:
		tw32(MAC_RCV_RULE_14,  0); tw32(MAC_RCV_VALUE_14,  0);
	case 14:
		tw32(MAC_RCV_RULE_13,  0); tw32(MAC_RCV_VALUE_13,  0);
	case 13:
		tw32(MAC_RCV_RULE_12,  0); tw32(MAC_RCV_VALUE_12,  0);
	case 12:
		tw32(MAC_RCV_RULE_11,  0); tw32(MAC_RCV_VALUE_11,  0);
	case 11:
		tw32(MAC_RCV_RULE_10,  0); tw32(MAC_RCV_VALUE_10,  0);
	case 10:
		tw32(MAC_RCV_RULE_9,  0); tw32(MAC_RCV_VALUE_9,  0);
	case 9:
		tw32(MAC_RCV_RULE_8,  0); tw32(MAC_RCV_VALUE_8,  0);
	case 8:
		tw32(MAC_RCV_RULE_7,  0); tw32(MAC_RCV_VALUE_7,  0);
	case 7:
		tw32(MAC_RCV_RULE_6,  0); tw32(MAC_RCV_VALUE_6,  0);
	case 6:
		tw32(MAC_RCV_RULE_5,  0); tw32(MAC_RCV_VALUE_5,  0);
	case 5:
		tw32(MAC_RCV_RULE_4,  0); tw32(MAC_RCV_VALUE_4,  0);
	case 4:
		/* tw32(MAC_RCV_RULE_3,  0); tw32(MAC_RCV_VALUE_3,  0); */
	case 3:
		/* tw32(MAC_RCV_RULE_2,  0); tw32(MAC_RCV_VALUE_2,  0); */
	case 2:
	case 1:

	default:
		break;
	}

	if (tg3_flag(tp, ENABLE_APE))
		/* Write our heartbeat update interval to APE. */
		tg3_ape_write32(tp, TG3_APE_HOST_HEARTBEAT_INT_MS,
				APE_HOST_HEARTBEAT_INT_DISABLE);

	tg3_write_sig_post_reset(tp, RESET_KIND_INIT);

	return 0;
}

/* Called at device open time to get the chip ready for
 * packet processing.  Invoked with tp->lock held.
 */
static int tg3_init_hw(struct tg3 *tp, bool reset_phy)
{
	/* Chip may have been just powered on. If so, the boot code may still
	 * be running initialization. Wait for it to finish to avoid races in
	 * accessing the hardware.
	 */
	tg3_enable_register_access(tp);
	tg3_poll_fw(tp);

	tg3_switch_clocks(tp);

	tw32(TG3PCI_MEM_WIN_BASE_ADDR, 0);

	return tg3_reset_hw(tp, reset_phy);
}

static void tg3_sd_scan_scratchpad(struct tg3 *tp, struct tg3_ocir *ocir)
{
	int i;

	for (i = 0; i < TG3_SD_NUM_RECS; i++, ocir++) {
		u32 off = i * TG3_OCIR_LEN, len = TG3_OCIR_LEN;

		tg3_ape_scratchpad_read(tp, (u32 *) ocir, off, len);
		off += len;

		if (ocir->signature != TG3_OCIR_SIG_MAGIC ||
		    !(ocir->version_flags & TG3_OCIR_FLAG_ACTIVE))
			memset(ocir, 0, TG3_OCIR_LEN);
	}
}

/* sysfs attributes for hwmon */
static ssize_t tg3_show_temp(struct device *dev,
			     struct device_attribute *devattr, char *buf)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct tg3 *tp = netdev_priv(netdev);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	u32 temperature;

	spin_lock_bh(&tp->lock);
	tg3_ape_scratchpad_read(tp, &temperature, attr->index,
				sizeof(temperature));
	spin_unlock_bh(&tp->lock);
	return sprintf(buf, "%u\n", temperature);
}


static SENSOR_DEVICE_ATTR(temp1_input, S_IRUGO, tg3_show_temp, NULL,
			  TG3_TEMP_SENSOR_OFFSET);
static SENSOR_DEVICE_ATTR(temp1_crit, S_IRUGO, tg3_show_temp, NULL,
			  TG3_TEMP_CAUTION_OFFSET);
static SENSOR_DEVICE_ATTR(temp1_max, S_IRUGO, tg3_show_temp, NULL,
			  TG3_TEMP_MAX_OFFSET);

static struct attribute *tg3_attributes[] = {
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp1_crit.dev_attr.attr,
	&sensor_dev_attr_temp1_max.dev_attr.attr,
	NULL
};

static const struct attribute_group tg3_group = {
	.attrs = tg3_attributes,
};

static void tg3_hwmon_close(struct tg3 *tp)
{
	if (tp->hwmon_dev) {
		hwmon_device_unregister(tp->hwmon_dev);
		tp->hwmon_dev = NULL;
		sysfs_remove_group(&tp->pdev->dev.kobj, &tg3_group);
	}
}

static void tg3_hwmon_open(struct tg3 *tp)
{
	int i, err;
	u32 size = 0;
	struct pci_dev *pdev = tp->pdev;
	struct tg3_ocir ocirs[TG3_SD_NUM_RECS];

	tg3_sd_scan_scratchpad(tp, ocirs);

	for (i = 0; i < TG3_SD_NUM_RECS; i++) {
		if (!ocirs[i].src_data_length)
			continue;

		size += ocirs[i].src_hdr_length;
		size += ocirs[i].src_data_length;
	}

	if (!size)
		return;

	/* Register hwmon sysfs hooks */
	err = sysfs_create_group(&pdev->dev.kobj, &tg3_group);
	if (err) {
		dev_err(&pdev->dev, "Cannot create sysfs group, aborting\n");
		return;
	}

	tp->hwmon_dev = hwmon_device_register(&pdev->dev);
	if (IS_ERR(tp->hwmon_dev)) {
		tp->hwmon_dev = NULL;
		dev_err(&pdev->dev, "Cannot register hwmon device, aborting\n");
		sysfs_remove_group(&pdev->dev.kobj, &tg3_group);
	}
}


#define TG3_STAT_ADD32(PSTAT, REG) \
do {	u32 __val = tr32(REG); \
	(PSTAT)->low += __val; \
	if ((PSTAT)->low < __val) \
		(PSTAT)->high += 1; \
} while (0)

static void tg3_periodic_fetch_stats(struct tg3 *tp)
{
	struct tg3_hw_stats *sp = tp->hw_stats;

	if (!tp->link_up)
		return;

	TG3_STAT_ADD32(&sp->tx_octets, MAC_TX_STATS_OCTETS);
	TG3_STAT_ADD32(&sp->tx_collisions, MAC_TX_STATS_COLLISIONS);
	TG3_STAT_ADD32(&sp->tx_xon_sent, MAC_TX_STATS_XON_SENT);
	TG3_STAT_ADD32(&sp->tx_xoff_sent, MAC_TX_STATS_XOFF_SENT);
	TG3_STAT_ADD32(&sp->tx_mac_errors, MAC_TX_STATS_MAC_ERRORS);
	TG3_STAT_ADD32(&sp->tx_single_collisions, MAC_TX_STATS_SINGLE_COLLISIONS);
	TG3_STAT_ADD32(&sp->tx_mult_collisions, MAC_TX_STATS_MULT_COLLISIONS);
	TG3_STAT_ADD32(&sp->tx_deferred, MAC_TX_STATS_DEFERRED);
	TG3_STAT_ADD32(&sp->tx_excessive_collisions, MAC_TX_STATS_EXCESSIVE_COL);
	TG3_STAT_ADD32(&sp->tx_late_collisions, MAC_TX_STATS_LATE_COL);
	TG3_STAT_ADD32(&sp->tx_ucast_packets, MAC_TX_STATS_UCAST);
	TG3_STAT_ADD32(&sp->tx_mcast_packets, MAC_TX_STATS_MCAST);
	TG3_STAT_ADD32(&sp->tx_bcast_packets, MAC_TX_STATS_BCAST);
	if (unlikely(tg3_flag(tp, 5719_5720_RDMA_BUG) &&
		     (sp->tx_ucast_packets.low + sp->tx_mcast_packets.low +
		      sp->tx_bcast_packets.low) > TG3_NUM_RDMA_CHANNELS)) {
		u32 val;

		val = tr32(TG3_LSO_RD_DMA_CRPTEN_CTRL);
		val &= ~tg3_lso_rd_dma_workaround_bit(tp);
		tw32(TG3_LSO_RD_DMA_CRPTEN_CTRL, val);
		tg3_flag_clear(tp, 5719_5720_RDMA_BUG);
	}

	TG3_STAT_ADD32(&sp->rx_octets, MAC_RX_STATS_OCTETS);
	TG3_STAT_ADD32(&sp->rx_fragments, MAC_RX_STATS_FRAGMENTS);
	TG3_STAT_ADD32(&sp->rx_ucast_packets, MAC_RX_STATS_UCAST);
	TG3_STAT_ADD32(&sp->rx_mcast_packets, MAC_RX_STATS_MCAST);
	TG3_STAT_ADD32(&sp->rx_bcast_packets, MAC_RX_STATS_BCAST);
	TG3_STAT_ADD32(&sp->rx_fcs_errors, MAC_RX_STATS_FCS_ERRORS);
	TG3_STAT_ADD32(&sp->rx_align_errors, MAC_RX_STATS_ALIGN_ERRORS);
	TG3_STAT_ADD32(&sp->rx_xon_pause_rcvd, MAC_RX_STATS_XON_PAUSE_RECVD);
	TG3_STAT_ADD32(&sp->rx_xoff_pause_rcvd, MAC_RX_STATS_XOFF_PAUSE_RECVD);
	TG3_STAT_ADD32(&sp->rx_mac_ctrl_rcvd, MAC_RX_STATS_MAC_CTRL_RECVD);
	TG3_STAT_ADD32(&sp->rx_xoff_entered, MAC_RX_STATS_XOFF_ENTERED);
	TG3_STAT_ADD32(&sp->rx_frame_too_long_errors, MAC_RX_STATS_FRAME_TOO_LONG);
	TG3_STAT_ADD32(&sp->rx_jabbers, MAC_RX_STATS_JABBERS);
	TG3_STAT_ADD32(&sp->rx_undersize_packets, MAC_RX_STATS_UNDERSIZE);

	TG3_STAT_ADD32(&sp->rxbds_empty, RCVLPC_NO_RCV_BD_CNT);
	if (tg3_asic_rev(tp) != ASIC_REV_5717 &&
	    tg3_chip_rev_id(tp) != CHIPREV_ID_5719_A0 &&
	    tg3_chip_rev_id(tp) != CHIPREV_ID_5720_A0) {
		TG3_STAT_ADD32(&sp->rx_discards, RCVLPC_IN_DISCARDS_CNT);
	} else {
		u32 val = tr32(HOSTCC_FLOW_ATTN);
		val = (val & HOSTCC_FLOW_ATTN_MBUF_LWM) ? 1 : 0;
		if (val) {
			tw32(HOSTCC_FLOW_ATTN, HOSTCC_FLOW_ATTN_MBUF_LWM);
			sp->rx_discards.low += val;
			if (sp->rx_discards.low < val)
				sp->rx_discards.high += 1;
		}
		sp->mbuf_lwm_thresh_hit = sp->rx_discards;
	}
	TG3_STAT_ADD32(&sp->rx_errors, RCVLPC_IN_ERRORS_CNT);
}

static void tg3_chk_missed_msi(struct tg3 *tp)
{
	u32 i;

	for (i = 0; i < tp->irq_cnt; i++) {
		struct tg3_napi *tnapi = &tp->napi[i];

		if (tg3_has_work(tnapi)) {
			if (tnapi->last_rx_cons == tnapi->rx_rcb_ptr &&
			    tnapi->last_tx_cons == tnapi->tx_cons) {
				if (tnapi->chk_msi_cnt < 1) {
					tnapi->chk_msi_cnt++;
					return;
				}
				tg3_msi(0, tnapi);
			}
		}
		tnapi->chk_msi_cnt = 0;
		tnapi->last_rx_cons = tnapi->rx_rcb_ptr;
		tnapi->last_tx_cons = tnapi->tx_cons;
	}
}

static void tg3_timer(unsigned long __opaque)
{
	struct tg3 *tp = (struct tg3 *) __opaque;

	if (tp->irq_sync || tg3_flag(tp, RESET_TASK_PENDING))
		goto restart_timer;

	spin_lock(&tp->lock);

	if (tg3_asic_rev(tp) == ASIC_REV_5717 ||
	    tg3_flag(tp, 57765_CLASS))
		tg3_chk_missed_msi(tp);

	if (tg3_flag(tp, FLUSH_POSTED_WRITES)) {
		/* BCM4785: Flush posted writes from GbE to host memory. */
		tr32(HOSTCC_MODE);
	}

	if (!tg3_flag(tp, TAGGED_STATUS)) {
		/* All of this garbage is because when using non-tagged
		 * IRQ status the mailbox/status_block protocol the chip
		 * uses with the cpu is race prone.
		 */
		if (tp->napi[0].hw_status->status & SD_STATUS_UPDATED) {
			tw32(GRC_LOCAL_CTRL,
			     tp->grc_local_ctrl | GRC_LCLCTRL_SETINT);
		} else {
			tw32(HOSTCC_MODE, tp->coalesce_mode |
			     HOSTCC_MODE_ENABLE | HOSTCC_MODE_NOW);
		}

		if (!(tr32(WDMAC_MODE) & WDMAC_MODE_ENABLE)) {
			spin_unlock(&tp->lock);
			tg3_reset_task_schedule(tp);
			goto restart_timer;
		}
	}

	/* This part only runs once per second. */
	if (!--tp->timer_counter) {
		if (tg3_flag(tp, 5705_PLUS))
			tg3_periodic_fetch_stats(tp);

		if (tp->setlpicnt && !--tp->setlpicnt)
			tg3_phy_eee_enable(tp);

		if (tg3_flag(tp, USE_LINKCHG_REG)) {
			u32 mac_stat;
			int phy_event;

			mac_stat = tr32(MAC_STATUS);

			phy_event = 0;
			if (tp->phy_flags & TG3_PHYFLG_USE_MI_INTERRUPT) {
				if (mac_stat & MAC_STATUS_MI_INTERRUPT)
					phy_event = 1;
			} else if (mac_stat & MAC_STATUS_LNKSTATE_CHANGED)
				phy_event = 1;

			if (phy_event)
				tg3_setup_phy(tp, false);
		} else if (tg3_flag(tp, POLL_SERDES)) {
			u32 mac_stat = tr32(MAC_STATUS);
			int need_setup = 0;

			if (tp->link_up &&
			    (mac_stat & MAC_STATUS_LNKSTATE_CHANGED)) {
				need_setup = 1;
			}
			if (!tp->link_up &&
			    (mac_stat & (MAC_STATUS_PCS_SYNCED |
					 MAC_STATUS_SIGNAL_DET))) {
				need_setup = 1;
			}
			if (need_setup) {
				if (!tp->serdes_counter) {
					tw32_f(MAC_MODE,
					     (tp->mac_mode &
					      ~MAC_MODE_PORT_MODE_MASK));
					udelay(40);
					tw32_f(MAC_MODE, tp->mac_mode);
					udelay(40);
				}
				tg3_setup_phy(tp, false);
			}
		} else if ((tp->phy_flags & TG3_PHYFLG_MII_SERDES) &&
			   tg3_flag(tp, 5780_CLASS)) {
			tg3_serdes_parallel_detect(tp);
		}

		tp->timer_counter = tp->timer_multiplier;
	}

	/* Heartbeat is only sent once every 2 seconds.
	 *
	 * The heartbeat is to tell the ASF firmware that the host
	 * driver is still alive.  In the event that the OS crashes,
	 * ASF needs to reset the hardware to free up the FIFO space
	 * that may be filled with rx packets destined for the host.
	 * If the FIFO is full, ASF will no longer function properly.
	 *
	 * Unintended resets have been reported on real time kernels
	 * where the timer doesn't run on time.  Netpoll will also have
	 * same problem.
	 *
	 * The new FWCMD_NICDRV_ALIVE3 command tells the ASF firmware
	 * to check the ring condition when the heartbeat is expiring
	 * before doing the reset.  This will prevent most unintended
	 * resets.
	 */
	if (!--tp->asf_counter) {
		if (tg3_flag(tp, ENABLE_ASF) && !tg3_flag(tp, ENABLE_APE)) {
			tg3_wait_for_event_ack(tp);

			tg3_write_mem(tp, NIC_SRAM_FW_CMD_MBOX,
				      FWCMD_NICDRV_ALIVE3);
			tg3_write_mem(tp, NIC_SRAM_FW_CMD_LEN_MBOX, 4);
			tg3_write_mem(tp, NIC_SRAM_FW_CMD_DATA_MBOX,
				      TG3_FW_UPDATE_TIMEOUT_SEC);

			tg3_generate_fw_event(tp);
		}
		tp->asf_counter = tp->asf_multiplier;
	}

	spin_unlock(&tp->lock);

restart_timer:
	tp->timer.expires = jiffies + tp->timer_offset;
	add_timer(&tp->timer);
}

static void tg3_timer_init(struct tg3 *tp)
{
	if (tg3_flag(tp, TAGGED_STATUS) &&
	    tg3_asic_rev(tp) != ASIC_REV_5717 &&
	    !tg3_flag(tp, 57765_CLASS))
		tp->timer_offset = HZ;
	else
		tp->timer_offset = HZ / 10;

	BUG_ON(tp->timer_offset > HZ);

	tp->timer_multiplier = (HZ / tp->timer_offset);
	tp->asf_multiplier = (HZ / tp->timer_offset) *
			     TG3_FW_UPDATE_FREQ_SEC;

	init_timer(&tp->timer);
	tp->timer.data = (unsigned long) tp;
	tp->timer.function = tg3_timer;
}

static void tg3_timer_start(struct tg3 *tp)
{
	tp->asf_counter   = tp->asf_multiplier;
	tp->timer_counter = tp->timer_multiplier;

	tp->timer.expires = jiffies + tp->timer_offset;
	add_timer(&tp->timer);
}

static void tg3_timer_stop(struct tg3 *tp)
{
	del_timer_sync(&tp->timer);
}

/* Restart hardware after configuration changes, self-test, etc.
 * Invoked with tp->lock held.
 */
static int tg3_restart_hw(struct tg3 *tp, bool reset_phy)
	__releases(tp->lock)
	__acquires(tp->lock)
{
	int err;

	err = tg3_init_hw(tp, reset_phy);
	if (err) {
		netdev_err(tp->dev,
			   "Failed to re-initialize device, aborting\n");
		tg3_halt(tp, RESET_KIND_SHUTDOWN, 1);
		tg3_full_unlock(tp);
		tg3_timer_stop(tp);
		tp->irq_sync = 0;
		tg3_napi_enable(tp);
		dev_close(tp->dev);
		tg3_full_lock(tp, 0);
	}
	return err;
}

static void tg3_reset_task(struct work_struct *work)
{
	struct tg3 *tp = container_of(work, struct tg3, reset_task);
	int err;

	tg3_full_lock(tp, 0);

	if (!netif_running(tp->dev)) {
		tg3_flag_clear(tp, RESET_TASK_PENDING);
		tg3_full_unlock(tp);
		return;
	}

	tg3_full_unlock(tp);

	tg3_phy_stop(tp);

	tg3_netif_stop(tp);

	tg3_full_lock(tp, 1);

	if (tg3_flag(tp, TX_RECOVERY_PENDING)) {
		tp->write32_tx_mbox = tg3_write32_tx_mbox;
		tp->write32_rx_mbox = tg3_write_flush_reg32;
		tg3_flag_set(tp, MBOX_WRITE_REORDER);
		tg3_flag_clear(tp, TX_RECOVERY_PENDING);
	}

	tg3_halt(tp, RESET_KIND_SHUTDOWN, 0);
	err = tg3_init_hw(tp, true);
	if (err)
		goto out;

	tg3_netif_start(tp);

out:
	tg3_full_unlock(tp);

	if (!err)
		tg3_phy_start(tp);

	tg3_flag_clear(tp, RESET_TASK_PENDING);
}

static int tg3_request_irq(struct tg3 *tp, int irq_num)
{
	irq_handler_t fn;
	unsigned long flags;
	char *name;
	struct tg3_napi *tnapi = &tp->napi[irq_num];

	if (tp->irq_cnt == 1)
		name = tp->dev->name;
	else {
		name = &tnapi->irq_lbl[0];
		snprintf(name, IFNAMSIZ, "%s-%d", tp->dev->name, irq_num);
		name[IFNAMSIZ-1] = 0;
	}

	if (tg3_flag(tp, USING_MSI) || tg3_flag(tp, USING_MSIX)) {
		fn = tg3_msi;
		if (tg3_flag(tp, 1SHOT_MSI))
			fn = tg3_msi_1shot;
		flags = 0;
	} else {
		fn = tg3_interrupt;
		if (tg3_flag(tp, TAGGED_STATUS))
			fn = tg3_interrupt_tagged;
		flags = IRQF_SHARED;
	}

	return request_irq(tnapi->irq_vec, fn, flags, name, tnapi);
}

static int tg3_test_interrupt(struct tg3 *tp)
{
	struct tg3_napi *tnapi = &tp->napi[0];
	struct net_device *dev = tp->dev;
	int err, i, intr_ok = 0;
	u32 val;

	if (!netif_running(dev))
		return -ENODEV;

	tg3_disable_ints(tp);

	free_irq(tnapi->irq_vec, tnapi);

	/*
	 * Turn off MSI one shot mode.  Otherwise this test has no
	 * observable way to know whether the interrupt was delivered.
	 */
	if (tg3_flag(tp, 57765_PLUS)) {
		val = tr32(MSGINT_MODE) | MSGINT_MODE_ONE_SHOT_DISABLE;
		tw32(MSGINT_MODE, val);
	}

	err = request_irq(tnapi->irq_vec, tg3_test_isr,
			  IRQF_SHARED, dev->name, tnapi);
	if (err)
		return err;

	tnapi->hw_status->status &= ~SD_STATUS_UPDATED;
	tg3_enable_ints(tp);

	tw32_f(HOSTCC_MODE, tp->coalesce_mode | HOSTCC_MODE_ENABLE |
	       tnapi->coal_now);

	for (i = 0; i < 5; i++) {
		u32 int_mbox, misc_host_ctrl;

		int_mbox = tr32_mailbox(tnapi->int_mbox);
		misc_host_ctrl = tr32(TG3PCI_MISC_HOST_CTRL);

		if ((int_mbox != 0) ||
		    (misc_host_ctrl & MISC_HOST_CTRL_MASK_PCI_INT)) {
			intr_ok = 1;
			break;
		}

		if (tg3_flag(tp, 57765_PLUS) &&
		    tnapi->hw_status->status_tag != tnapi->last_tag)
			tw32_mailbox_f(tnapi->int_mbox, tnapi->last_tag << 24);

		msleep(10);
	}

	tg3_disable_ints(tp);

	free_irq(tnapi->irq_vec, tnapi);

	err = tg3_request_irq(tp, 0);

	if (err)
		return err;

	if (intr_ok) {
		/* Reenable MSI one shot mode. */
		if (tg3_flag(tp, 57765_PLUS) && tg3_flag(tp, 1SHOT_MSI)) {
			val = tr32(MSGINT_MODE) & ~MSGINT_MODE_ONE_SHOT_DISABLE;
			tw32(MSGINT_MODE, val);
		}
		return 0;
	}

	return -EIO;
}

/* Returns 0 if MSI test succeeds or MSI test fails and INTx mode is
 * successfully restored
 */
static int tg3_test_msi(struct tg3 *tp)
{
	int err;
	u16 pci_cmd;

	if (!tg3_flag(tp, USING_MSI))
		return 0;

	/* Turn off SERR reporting in case MSI terminates with Master
	 * Abort.
	 */
	pci_read_config_word(tp->pdev, PCI_COMMAND, &pci_cmd);
	pci_write_config_word(tp->pdev, PCI_COMMAND,
			      pci_cmd & ~PCI_COMMAND_SERR);

	err = tg3_test_interrupt(tp);

	pci_write_config_word(tp->pdev, PCI_COMMAND, pci_cmd);

	if (!err)
		return 0;

	/* other failures */
	if (err != -EIO)
		return err;

	/* MSI test failed, go back to INTx mode */
	netdev_warn(tp->dev, "No interrupt was generated using MSI. Switching "
		    "to INTx mode. Please report this failure to the PCI "
		    "maintainer and include system chipset information\n");

	free_irq(tp->napi[0].irq_vec, &tp->napi[0]);

	pci_disable_msi(tp->pdev);

	tg3_flag_clear(tp, USING_MSI);
	tp->napi[0].irq_vec = tp->pdev->irq;

	err = tg3_request_irq(tp, 0);
	if (err)
		return err;

	/* Need to reset the chip because the MSI cycle may have terminated
	 * with Master Abort.
	 */
	tg3_full_lock(tp, 1);

	tg3_halt(tp, RESET_KIND_SHUTDOWN, 1);
	err = tg3_init_hw(tp, true);

	tg3_full_unlock(tp);

	if (err)
		free_irq(tp->napi[0].irq_vec, &tp->napi[0]);

	return err;
}

static int tg3_request_firmware(struct tg3 *tp)
{
	const struct tg3_firmware_hdr *fw_hdr;

	if (request_firmware(&tp->fw, tp->fw_needed, &tp->pdev->dev)) {
		netdev_err(tp->dev, "Failed to load firmware \"%s\"\n",
			   tp->fw_needed);
		return -ENOENT;
	}

	fw_hdr = (struct tg3_firmware_hdr *)tp->fw->data;

	/* Firmware blob starts with version numbers, followed by
	 * start address and _full_ length including BSS sections
	 * (which must be longer than the actual data, of course
	 */

	tp->fw_len = be32_to_cpu(fw_hdr->len);	/* includes bss */
	if (tp->fw_len < (tp->fw->size - TG3_FW_HDR_LEN)) {
		netdev_err(tp->dev, "bogus length %d in \"%s\"\n",
			   tp->fw_len, tp->fw_needed);
		release_firmware(tp->fw);
		tp->fw = NULL;
		return -EINVAL;
	}

	/* We no longer need firmware; we have it. */
	tp->fw_needed = NULL;
	return 0;
}

static u32 tg3_irq_count(struct tg3 *tp)
{
	u32 irq_cnt = max(tp->rxq_cnt, tp->txq_cnt);

	if (irq_cnt > 1) {
		/* We want as many rx rings enabled as there are cpus.
		 * In multiqueue MSI-X mode, the first MSI-X vector
		 * only deals with link interrupts, etc, so we add
		 * one to the number of vectors we are requesting.
		 */
		irq_cnt = min_t(unsigned, irq_cnt + 1, tp->irq_max);
	}

	return irq_cnt;
}

static bool tg3_enable_msix(struct tg3 *tp)
{
	int i, rc;
	struct msix_entry msix_ent[TG3_IRQ_MAX_VECS];

	tp->txq_cnt = tp->txq_req;
	tp->rxq_cnt = tp->rxq_req;
	if (!tp->rxq_cnt)
		tp->rxq_cnt = netif_get_num_default_rss_queues();
	if (tp->rxq_cnt > tp->rxq_max)
		tp->rxq_cnt = tp->rxq_max;

	/* Disable multiple TX rings by default.  Simple round-robin hardware
	 * scheduling of the TX rings can cause starvation of rings with
	 * small packets when other rings have TSO or jumbo packets.
	 */
	if (!tp->txq_req)
		tp->txq_cnt = 1;

	tp->irq_cnt = tg3_irq_count(tp);

	for (i = 0; i < tp->irq_max; i++) {
		msix_ent[i].entry  = i;
		msix_ent[i].vector = 0;
	}

	rc = pci_enable_msix(tp->pdev, msix_ent, tp->irq_cnt);
	if (rc < 0) {
		return false;
	} else if (rc != 0) {
		if (pci_enable_msix(tp->pdev, msix_ent, rc))
			return false;
		netdev_notice(tp->dev, "Requested %d MSI-X vectors, received %d\n",
			      tp->irq_cnt, rc);
		tp->irq_cnt = rc;
		tp->rxq_cnt = max(rc - 1, 1);
		if (tp->txq_cnt)
			tp->txq_cnt = min(tp->rxq_cnt, tp->txq_max);
	}

	for (i = 0; i < tp->irq_max; i++)
		tp->napi[i].irq_vec = msix_ent[i].vector;

	if (netif_set_real_num_rx_queues(tp->dev, tp->rxq_cnt)) {
		pci_disable_msix(tp->pdev);
		return false;
	}

	if (tp->irq_cnt == 1)
		return true;

	tg3_flag_set(tp, ENABLE_RSS);

	if (tp->txq_cnt > 1)
		tg3_flag_set(tp, ENABLE_TSS);

	netif_set_real_num_tx_queues(tp->dev, tp->txq_cnt);

	return true;
}

static void tg3_ints_init(struct tg3 *tp)
{
	if ((tg3_flag(tp, SUPPORT_MSI) || tg3_flag(tp, SUPPORT_MSIX)) &&
	    !tg3_flag(tp, TAGGED_STATUS)) {
		/* All MSI supporting chips should support tagged
		 * status.  Assert that this is the case.
		 */
		netdev_warn(tp->dev,
			    "MSI without TAGGED_STATUS? Not using MSI\n");
		goto defcfg;
	}

	if (tg3_flag(tp, SUPPORT_MSIX) && tg3_enable_msix(tp))
		tg3_flag_set(tp, USING_MSIX);
	else if (tg3_flag(tp, SUPPORT_MSI) && pci_enable_msi(tp->pdev) == 0)
		tg3_flag_set(tp, USING_MSI);

	if (tg3_flag(tp, USING_MSI) || tg3_flag(tp, USING_MSIX)) {
		u32 msi_mode = tr32(MSGINT_MODE);
		if (tg3_flag(tp, USING_MSIX) && tp->irq_cnt > 1)
			msi_mode |= MSGINT_MODE_MULTIVEC_EN;
		if (!tg3_flag(tp, 1SHOT_MSI))
			msi_mode |= MSGINT_MODE_ONE_SHOT_DISABLE;
		tw32(MSGINT_MODE, msi_mode | MSGINT_MODE_ENABLE);
	}
defcfg:
	if (!tg3_flag(tp, USING_MSIX)) {
		tp->irq_cnt = 1;
		tp->napi[0].irq_vec = tp->pdev->irq;
	}

	if (tp->irq_cnt == 1) {
		tp->txq_cnt = 1;
		tp->rxq_cnt = 1;
		netif_set_real_num_tx_queues(tp->dev, 1);
		netif_set_real_num_rx_queues(tp->dev, 1);
	}
}

static void tg3_ints_fini(struct tg3 *tp)
{
	if (tg3_flag(tp, USING_MSIX))
		pci_disable_msix(tp->pdev);
	else if (tg3_flag(tp, USING_MSI))
		pci_disable_msi(tp->pdev);
	tg3_flag_clear(tp, USING_MSI);
	tg3_flag_clear(tp, USING_MSIX);
	tg3_flag_clear(tp, ENABLE_RSS);
	tg3_flag_clear(tp, ENABLE_TSS);
}

static int tg3_start(struct tg3 *tp, bool reset_phy, bool test_irq,
		     bool init)
{
	struct net_device *dev = tp->dev;
	int i, err;

	/*
	 * Setup interrupts first so we know how
	 * many NAPI resources to allocate
	 */
	tg3_ints_init(tp);

	tg3_rss_check_indir_tbl(tp);

	/* The placement of this call is tied
	 * to the setup and use of Host TX descriptors.
	 */
	err = tg3_alloc_consistent(tp);
	if (err)
		goto err_out1;

	tg3_napi_init(tp);

	tg3_napi_enable(tp);

	for (i = 0; i < tp->irq_cnt; i++) {
		struct tg3_napi *tnapi = &tp->napi[i];
		err = tg3_request_irq(tp, i);
		if (err) {
			for (i--; i >= 0; i--) {
				tnapi = &tp->napi[i];
				free_irq(tnapi->irq_vec, tnapi);
			}
			goto err_out2;
		}
	}

	tg3_full_lock(tp, 0);

	err = tg3_init_hw(tp, reset_phy);
	if (err) {
		tg3_halt(tp, RESET_KIND_SHUTDOWN, 1);
		tg3_free_rings(tp);
	}

	tg3_full_unlock(tp);

	if (err)
		goto err_out3;

	if (test_irq && tg3_flag(tp, USING_MSI)) {
		err = tg3_test_msi(tp);

		if (err) {
			tg3_full_lock(tp, 0);
			tg3_halt(tp, RESET_KIND_SHUTDOWN, 1);
			tg3_free_rings(tp);
			tg3_full_unlock(tp);

			goto err_out2;
		}

		if (!tg3_flag(tp, 57765_PLUS) && tg3_flag(tp, USING_MSI)) {
			u32 val = tr32(PCIE_TRANSACTION_CFG);

			tw32(PCIE_TRANSACTION_CFG,
			     val | PCIE_TRANS_CFG_1SHOT_MSI);
		}
	}

	tg3_phy_start(tp);

	tg3_hwmon_open(tp);

	tg3_full_lock(tp, 0);

	tg3_timer_start(tp);
	tg3_flag_set(tp, INIT_COMPLETE);
	tg3_enable_ints(tp);

	if (init)
		tg3_ptp_init(tp);
	else
		tg3_ptp_resume(tp);


	tg3_full_unlock(tp);

	netif_tx_start_all_queues(dev);

	/*
	 * Reset loopback feature if it was turned on while the device was down
	 * make sure that it's installed properly now.
	 */
	if (dev->features & NETIF_F_LOOPBACK)
		tg3_set_loopback(dev, dev->features);

	return 0;

err_out3:
	for (i = tp->irq_cnt - 1; i >= 0; i--) {
		struct tg3_napi *tnapi = &tp->napi[i];
		free_irq(tnapi->irq_vec, tnapi);
	}

err_out2:
	tg3_napi_disable(tp);
	tg3_napi_fini(tp);
	tg3_free_consistent(tp);

err_out1:
	tg3_ints_fini(tp);

	return err;
}

static void tg3_stop(struct tg3 *tp)
{
	int i;

	tg3_reset_task_cancel(tp);
	tg3_netif_stop(tp);

	tg3_timer_stop(tp);

	tg3_hwmon_close(tp);

	tg3_phy_stop(tp);

	tg3_full_lock(tp, 1);

	tg3_disable_ints(tp);

	tg3_halt(tp, RESET_KIND_SHUTDOWN, 1);
	tg3_free_rings(tp);
	tg3_flag_clear(tp, INIT_COMPLETE);

	tg3_full_unlock(tp);

	for (i = tp->irq_cnt - 1; i >= 0; i--) {
		struct tg3_napi *tnapi = &tp->napi[i];
		free_irq(tnapi->irq_vec, tnapi);
	}

	tg3_ints_fini(tp);

	tg3_napi_fini(tp);

	tg3_free_consistent(tp);
}

static int tg3_open(struct net_device *dev)
{
	struct tg3 *tp = netdev_priv(dev);
	int err;

	if (tp->fw_needed) {
		err = tg3_request_firmware(tp);
		if (tg3_asic_rev(tp) == ASIC_REV_57766) {
			if (err) {
				netdev_warn(tp->dev, "EEE capability disabled\n");
				tp->phy_flags &= ~TG3_PHYFLG_EEE_CAP;
			} else if (!(tp->phy_flags & TG3_PHYFLG_EEE_CAP)) {
				netdev_warn(tp->dev, "EEE capability restored\n");
				tp->phy_flags |= TG3_PHYFLG_EEE_CAP;
			}
		} else if (tg3_chip_rev_id(tp) == CHIPREV_ID_5701_A0) {
			if (err)
				return err;
		} else if (err) {
			netdev_warn(tp->dev, "TSO capability disabled\n");
			tg3_flag_clear(tp, TSO_CAPABLE);
		} else if (!tg3_flag(tp, TSO_CAPABLE)) {
			netdev_notice(tp->dev, "TSO capability restored\n");
			tg3_flag_set(tp, TSO_CAPABLE);
		}
	}

	tg3_carrier_off(tp);

	err = tg3_power_up(tp);
	if (err)
		return err;

	tg3_full_lock(tp, 0);

	tg3_disable_ints(tp);
	tg3_flag_clear(tp, INIT_COMPLETE);

	tg3_full_unlock(tp);

	err = tg3_start(tp,
			!(tp->phy_flags & TG3_PHYFLG_KEEP_LINK_ON_PWRDN),
			true, true);
	if (err) {
		tg3_frob_aux_power(tp, false);
		pci_set_power_state(tp->pdev, PCI_D3hot);
	}

	if (tg3_flag(tp, PTP_CAPABLE)) {
		tp->ptp_clock = ptp_clock_register(&tp->ptp_info,
						   &tp->pdev->dev);
		if (IS_ERR(tp->ptp_clock))
			tp->ptp_clock = NULL;
	}

	return err;
}

static int tg3_close(struct net_device *dev)
{
	struct tg3 *tp = netdev_priv(dev);

	tg3_ptp_fini(tp);

	tg3_stop(tp);

	/* Clear stats across close / open calls */
	memset(&tp->net_stats_prev, 0, sizeof(tp->net_stats_prev));
	memset(&tp->estats_prev, 0, sizeof(tp->estats_prev));

	tg3_power_down(tp);

	tg3_carrier_off(tp);

	return 0;
}

static inline u64 get_stat64(tg3_stat64_t *val)
{
       return ((u64)val->high << 32) | ((u64)val->low);
}

static u64 tg3_calc_crc_errors(struct tg3 *tp)
{
	struct tg3_hw_stats *hw_stats = tp->hw_stats;

	if (!(tp->phy_flags & TG3_PHYFLG_PHY_SERDES) &&
	    (tg3_asic_rev(tp) == ASIC_REV_5700 ||
	     tg3_asic_rev(tp) == ASIC_REV_5701)) {
		u32 val;

		if (!tg3_readphy(tp, MII_TG3_TEST1, &val)) {
			tg3_writephy(tp, MII_TG3_TEST1,
				     val | MII_TG3_TEST1_CRC_EN);
			tg3_readphy(tp, MII_TG3_RXR_COUNTERS, &val);
		} else
			val = 0;

		tp->phy_crc_errors += val;

		return tp->phy_crc_errors;
	}

	return get_stat64(&hw_stats->rx_fcs_errors);
}

#define ESTAT_ADD(member) \
	estats->member =	old_estats->member + \
				get_stat64(&hw_stats->member)

static void tg3_get_estats(struct tg3 *tp, struct tg3_ethtool_stats *estats)
{
	struct tg3_ethtool_stats *old_estats = &tp->estats_prev;
	struct tg3_hw_stats *hw_stats = tp->hw_stats;

	ESTAT_ADD(rx_octets);
	ESTAT_ADD(rx_fragments);
	ESTAT_ADD(rx_ucast_packets);
	ESTAT_ADD(rx_mcast_packets);
	ESTAT_ADD(rx_bcast_packets);
	ESTAT_ADD(rx_fcs_errors);
	ESTAT_ADD(rx_align_errors);
	ESTAT_ADD(rx_xon_pause_rcvd);
	ESTAT_ADD(rx_xoff_pause_rcvd);
	ESTAT_ADD(rx_mac_ctrl_rcvd);
	ESTAT_ADD(rx_xoff_entered);
	ESTAT_ADD(rx_frame_too_long_errors);
	ESTAT_ADD(rx_jabbers);
	ESTAT_ADD(rx_undersize_packets);
	ESTAT_ADD(rx_in_length_errors);
	ESTAT_ADD(rx_out_length_errors);
	ESTAT_ADD(rx_64_or_less_octet_packets);
	ESTAT_ADD(rx_65_to_127_octet_packets);
	ESTAT_ADD(rx_128_to_255_octet_packets);
	ESTAT_ADD(rx_256_to_511_octet_packets);
	ESTAT_ADD(rx_512_to_1023_octet_packets);
	ESTAT_ADD(rx_1024_to_1522_octet_packets);
	ESTAT_ADD(rx_1523_to_2047_octet_packets);
	ESTAT_ADD(rx_2048_to_4095_octet_packets);
	ESTAT_ADD(rx_4096_to_8191_octet_packets);
	ESTAT_ADD(rx_8192_to_9022_octet_packets);

	ESTAT_ADD(tx_octets);
	ESTAT_ADD(tx_collisions);
	ESTAT_ADD(tx_xon_sent);
	ESTAT_ADD(tx_xoff_sent);
	ESTAT_ADD(tx_flow_control);
	ESTAT_ADD(tx_mac_errors);
	ESTAT_ADD(tx_single_collisions);
	ESTAT_ADD(tx_mult_collisions);
	ESTAT_ADD(tx_deferred);
	ESTAT_ADD(tx_excessive_collisions);
	ESTAT_ADD(tx_late_collisions);
	ESTAT_ADD(tx_collide_2times);
	ESTAT_ADD(tx_collide_3times);
	ESTAT_ADD(tx_collide_4times);
	ESTAT_ADD(tx_collide_5times);
	ESTAT_ADD(tx_collide_6times);
	ESTAT_ADD(tx_collide_7times);
	ESTAT_ADD(tx_collide_8times);
	ESTAT_ADD(tx_collide_9times);
	ESTAT_ADD(tx_collide_10times);
	ESTAT_ADD(tx_collide_11times);
	ESTAT_ADD(tx_collide_12times);
	ESTAT_ADD(tx_collide_13times);
	ESTAT_ADD(tx_collide_14times);
	ESTAT_ADD(tx_collide_15times);
	ESTAT_ADD(tx_ucast_packets);
	ESTAT_ADD(tx_mcast_packets);
	ESTAT_ADD(tx_bcast_packets);
	ESTAT_ADD(tx_carrier_sense_errors);
	ESTAT_ADD(tx_discards);
	ESTAT_ADD(tx_errors);

	ESTAT_ADD(dma_writeq_full);
	ESTAT_ADD(dma_write_prioq_full);
	ESTAT_ADD(rxbds_empty);
	ESTAT_ADD(rx_discards);
	ESTAT_ADD(rx_errors);
	ESTAT_ADD(rx_threshold_hit);

	ESTAT_ADD(dma_readq_full);
	ESTAT_ADD(dma_read_prioq_full);
	ESTAT_ADD(tx_comp_queue_full);

	ESTAT_ADD(ring_set_send_prod_index);
	ESTAT_ADD(ring_status_update);
	ESTAT_ADD(nic_irqs);
	ESTAT_ADD(nic_avoided_irqs);
	ESTAT_ADD(nic_tx_threshold_hit);

	ESTAT_ADD(mbuf_lwm_thresh_hit);
}

static void tg3_get_nstats(struct tg3 *tp, struct rtnl_link_stats64 *stats)
{
	struct rtnl_link_stats64 *old_stats = &tp->net_stats_prev;
	struct tg3_hw_stats *hw_stats = tp->hw_stats;

	stats->rx_packets = old_stats->rx_packets +
		get_stat64(&hw_stats->rx_ucast_packets) +
		get_stat64(&hw_stats->rx_mcast_packets) +
		get_stat64(&hw_stats->rx_bcast_packets);

	stats->tx_packets = old_stats->tx_packets +
		get_stat64(&hw_stats->tx_ucast_packets) +
		get_stat64(&hw_stats->tx_mcast_packets) +
		get_stat64(&hw_stats->tx_bcast_packets);

	stats->rx_bytes = old_stats->rx_bytes +
		get_stat64(&hw_stats->rx_octets);
	stats->tx_bytes = old_stats->tx_bytes +
		get_stat64(&hw_stats->tx_octets);

	stats->rx_errors = old_stats->rx_errors +
		get_stat64(&hw_stats->rx_errors);
	stats->tx_errors = old_stats->tx_errors +
		get_stat64(&hw_stats->tx_errors) +
		get_stat64(&hw_stats->tx_mac_errors) +
		get_stat64(&hw_stats->tx_carrier_sense_errors) +
		get_stat64(&hw_stats->tx_discards);

	stats->multicast = old_stats->multicast +
		get_stat64(&hw_stats->rx_mcast_packets);
	stats->collisions = old_stats->collisions +
		get_stat64(&hw_stats->tx_collisions);

	stats->rx_length_errors = old_stats->rx_length_errors +
		get_stat64(&hw_stats->rx_frame_too_long_errors) +
		get_stat64(&hw_stats->rx_undersize_packets);

	stats->rx_over_errors = old_stats->rx_over_errors +
		get_stat64(&hw_stats->rxbds_empty);
	stats->rx_frame_errors = old_stats->rx_frame_errors +
		get_stat64(&hw_stats->rx_align_errors);
	stats->tx_aborted_errors = old_stats->tx_aborted_errors +
		get_stat64(&hw_stats->tx_discards);
	stats->tx_carrier_errors = old_stats->tx_carrier_errors +
		get_stat64(&hw_stats->tx_carrier_sense_errors);

	stats->rx_crc_errors = old_stats->rx_crc_errors +
		tg3_calc_crc_errors(tp);

	stats->rx_missed_errors = old_stats->rx_missed_errors +
		get_stat64(&hw_stats->rx_discards);

	stats->rx_dropped = tp->rx_dropped;
	stats->tx_dropped = tp->tx_dropped;
}

static int tg3_get_regs_len(struct net_device *dev)
{
	return TG3_REG_BLK_SIZE;
}

static void tg3_get_regs(struct net_device *dev,
		struct ethtool_regs *regs, void *_p)
{
	struct tg3 *tp = netdev_priv(dev);

	regs->version = 0;

	memset(_p, 0, TG3_REG_BLK_SIZE);

	if (tp->phy_flags & TG3_PHYFLG_IS_LOW_POWER)
		return;

	tg3_full_lock(tp, 0);

	tg3_dump_legacy_regs(tp, (u32 *)_p);

	tg3_full_unlock(tp);
}

static int tg3_get_eeprom_len(struct net_device *dev)
{
	struct tg3 *tp = netdev_priv(dev);

	return tp->nvram_size;
}

static int tg3_get_eeprom(struct net_device *dev, struct ethtool_eeprom *eeprom, u8 *data)
{
	struct tg3 *tp = netdev_priv(dev);
	int ret;
	u8  *pd;
	u32 i, offset, len, b_offset, b_count;
	__be32 val;

	if (tg3_flag(tp, NO_NVRAM))
		return -EINVAL;

	if (tp->phy_flags & TG3_PHYFLG_IS_LOW_POWER)
		return -EAGAIN;

	offset = eeprom->offset;
	len = eeprom->len;
	eeprom->len = 0;

	eeprom->magic = TG3_EEPROM_MAGIC;

	if (offset & 3) {
		/* adjustments to start on required 4 byte boundary */
		b_offset = offset & 3;
		b_count = 4 - b_offset;
		if (b_count > len) {
			/* i.e. offset=1 len=2 */
			b_count = len;
		}
		ret = tg3_nvram_read_be32(tp, offset-b_offset, &val);
		if (ret)
			return ret;
		memcpy(data, ((char *)&val) + b_offset, b_count);
		len -= b_count;
		offset += b_count;
		eeprom->len += b_count;
	}

	/* read bytes up to the last 4 byte boundary */
	pd = &data[eeprom->len];
	for (i = 0; i < (len - (len & 3)); i += 4) {
		ret = tg3_nvram_read_be32(tp, offset + i, &val);
		if (ret) {
			eeprom->len += i;
			return ret;
		}
		memcpy(pd + i, &val, 4);
	}
	eeprom->len += i;

	if (len & 3) {
		/* read last bytes not ending on 4 byte boundary */
		pd = &data[eeprom->len];
		b_count = len & 3;
		b_offset = offset + len - b_count;
		ret = tg3_nvram_read_be32(tp, b_offset, &val);
		if (ret)
			return ret;
		memcpy(pd, &val, b_count);
		eeprom->len += b_count;
	}
	return 0;
}

static int tg3_set_eeprom(struct net_device *dev, struct ethtool_eeprom *eeprom, u8 *data)
{
	struct tg3 *tp = netdev_priv(dev);
	int ret;
	u32 offset, len, b_offset, odd_len;
	u8 *buf;
	__be32 start, end;

	if (tp->phy_flags & TG3_PHYFLG_IS_LOW_POWER)
		return -EAGAIN;

	if (tg3_flag(tp, NO_NVRAM) ||
	    eeprom->magic != TG3_EEPROM_MAGIC)
		return -EINVAL;

	offset = eeprom->offset;
	len = eeprom->len;

	if ((b_offset = (offset & 3))) {
		/* adjustments to start on required 4 byte boundary */
		ret = tg3_nvram_read_be32(tp, offset-b_offset, &start);
		if (ret)
			return ret;
		len += b_offset;
		offset &= ~3;
		if (len < 4)
			len = 4;
	}

	odd_len = 0;
	if (len & 3) {
		/* adjustments to end on required 4 byte boundary */
		odd_len = 1;
		len = (len + 3) & ~3;
		ret = tg3_nvram_read_be32(tp, offset+len-4, &end);
		if (ret)
			return ret;
	}

	buf = data;
	if (b_offset || odd_len) {
		buf = kmalloc(len, GFP_KERNEL);
		if (!buf)
			return -ENOMEM;
		if (b_offset)
			memcpy(buf, &start, 4);
		if (odd_len)
			memcpy(buf+len-4, &end, 4);
		memcpy(buf + b_offset, data, eeprom->len);
	}

	ret = tg3_nvram_write_block(tp, offset, len, buf);

	if (buf != data)
		kfree(buf);

	return ret;
}

static int tg3_get_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct tg3 *tp = netdev_priv(dev);

	if (tg3_flag(tp, USE_PHYLIB)) {
		struct phy_device *phydev;
		if (!(tp->phy_flags & TG3_PHYFLG_IS_CONNECTED))
			return -EAGAIN;
		phydev = tp->mdio_bus->phy_map[TG3_PHY_MII_ADDR];
		return phy_ethtool_gset(phydev, cmd);
	}

	cmd->supported = (SUPPORTED_Autoneg);

	if (!(tp->phy_flags & TG3_PHYFLG_10_100_ONLY))
		cmd->supported |= (SUPPORTED_1000baseT_Half |
				   SUPPORTED_1000baseT_Full);

	if (!(tp->phy_flags & TG3_PHYFLG_ANY_SERDES)) {
		cmd->supported |= (SUPPORTED_100baseT_Half |
				  SUPPORTED_100baseT_Full |
				  SUPPORTED_10baseT_Half |
				  SUPPORTED_10baseT_Full |
				  SUPPORTED_TP);
		cmd->port = PORT_TP;
	} else {
		cmd->supported |= SUPPORTED_FIBRE;
		cmd->port = PORT_FIBRE;
	}

	cmd->advertising = tp->link_config.advertising;
	if (tg3_flag(tp, PAUSE_AUTONEG)) {
		if (tp->link_config.flowctrl & FLOW_CTRL_RX) {
			if (tp->link_config.flowctrl & FLOW_CTRL_TX) {
				cmd->advertising |= ADVERTISED_Pause;
			} else {
				cmd->advertising |= ADVERTISED_Pause |
						    ADVERTISED_Asym_Pause;
			}
		} else if (tp->link_config.flowctrl & FLOW_CTRL_TX) {
			cmd->advertising |= ADVERTISED_Asym_Pause;
		}
	}
	if (netif_running(dev) && tp->link_up) {
		ethtool_cmd_speed_set(cmd, tp->link_config.active_speed);
		cmd->duplex = tp->link_config.active_duplex;
		cmd->lp_advertising = tp->link_config.rmt_adv;
		if (!(tp->phy_flags & TG3_PHYFLG_ANY_SERDES)) {
			if (tp->phy_flags & TG3_PHYFLG_MDIX_STATE)
				cmd->eth_tp_mdix = ETH_TP_MDI_X;
			else
				cmd->eth_tp_mdix = ETH_TP_MDI;
		}
	} else {
		ethtool_cmd_speed_set(cmd, SPEED_UNKNOWN);
		cmd->duplex = DUPLEX_UNKNOWN;
		cmd->eth_tp_mdix = ETH_TP_MDI_INVALID;
	}
	cmd->phy_address = tp->phy_addr;
	cmd->transceiver = XCVR_INTERNAL;
	cmd->autoneg = tp->link_config.autoneg;
	cmd->maxtxpkt = 0;
	cmd->maxrxpkt = 0;
	return 0;
}

static int tg3_set_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct tg3 *tp = netdev_priv(dev);
	u32 speed = ethtool_cmd_speed(cmd);

	if (tg3_flag(tp, USE_PHYLIB)) {
		struct phy_device *phydev;
		if (!(tp->phy_flags & TG3_PHYFLG_IS_CONNECTED))
			return -EAGAIN;
		phydev = tp->mdio_bus->phy_map[TG3_PHY_MII_ADDR];
		return phy_ethtool_sset(phydev, cmd);
	}

	if (cmd->autoneg != AUTONEG_ENABLE &&
	    cmd->autoneg != AUTONEG_DISABLE)
		return -EINVAL;

	if (cmd->autoneg == AUTONEG_DISABLE &&
	    cmd->duplex != DUPLEX_FULL &&
	    cmd->duplex != DUPLEX_HALF)
		return -EINVAL;

	if (cmd->autoneg == AUTONEG_ENABLE) {
		u32 mask = ADVERTISED_Autoneg |
			   ADVERTISED_Pause |
			   ADVERTISED_Asym_Pause;

		if (!(tp->phy_flags & TG3_PHYFLG_10_100_ONLY))
			mask |= ADVERTISED_1000baseT_Half |
				ADVERTISED_1000baseT_Full;

		if (!(tp->phy_flags & TG3_PHYFLG_ANY_SERDES))
			mask |= ADVERTISED_100baseT_Half |
				ADVERTISED_100baseT_Full |
				ADVERTISED_10baseT_Half |
				ADVERTISED_10baseT_Full |
				ADVERTISED_TP;
		else
			mask |= ADVERTISED_FIBRE;

		if (cmd->advertising & ~mask)
			return -EINVAL;

		mask &= (ADVERTISED_1000baseT_Half |
			 ADVERTISED_1000baseT_Full |
			 ADVERTISED_100baseT_Half |
			 ADVERTISED_100baseT_Full |
			 ADVERTISED_10baseT_Half |
			 ADVERTISED_10baseT_Full);

		cmd->advertising &= mask;
	} else {
		if (tp->phy_flags & TG3_PHYFLG_ANY_SERDES) {
			if (speed != SPEED_1000)
				return -EINVAL;

			if (cmd->duplex != DUPLEX_FULL)
				return -EINVAL;
		} else {
			if (speed != SPEED_100 &&
			    speed != SPEED_10)
				return -EINVAL;
		}
	}

	tg3_full_lock(tp, 0);

	tp->link_config.autoneg = cmd->autoneg;
	if (cmd->autoneg == AUTONEG_ENABLE) {
		tp->link_config.advertising = (cmd->advertising |
					      ADVERTISED_Autoneg);
		tp->link_config.speed = SPEED_UNKNOWN;
		tp->link_config.duplex = DUPLEX_UNKNOWN;
	} else {
		tp->link_config.advertising = 0;
		tp->link_config.speed = speed;
		tp->link_config.duplex = cmd->duplex;
	}

	tp->phy_flags |= TG3_PHYFLG_USER_CONFIGURED;

	tg3_warn_mgmt_link_flap(tp);

	if (netif_running(dev))
		tg3_setup_phy(tp, true);

	tg3_full_unlock(tp);

	return 0;
}

static void tg3_get_drvinfo(struct net_device *dev, struct ethtool_drvinfo *info)
{
	struct tg3 *tp = netdev_priv(dev);

	strlcpy(info->driver, DRV_MODULE_NAME, sizeof(info->driver));
	strlcpy(info->version, DRV_MODULE_VERSION, sizeof(info->version));
	strlcpy(info->fw_version, tp->fw_ver, sizeof(info->fw_version));
	strlcpy(info->bus_info, pci_name(tp->pdev), sizeof(info->bus_info));
}

static void tg3_get_wol(struct net_device *dev, struct ethtool_wolinfo *wol)
{
	struct tg3 *tp = netdev_priv(dev);

	if (tg3_flag(tp, WOL_CAP) && device_can_wakeup(&tp->pdev->dev))
		wol->supported = WAKE_MAGIC;
	else
		wol->supported = 0;
	wol->wolopts = 0;
	if (tg3_flag(tp, WOL_ENABLE) && device_can_wakeup(&tp->pdev->dev))
		wol->wolopts = WAKE_MAGIC;
	memset(&wol->sopass, 0, sizeof(wol->sopass));
}

static int tg3_set_wol(struct net_device *dev, struct ethtool_wolinfo *wol)
{
	struct tg3 *tp = netdev_priv(dev);
	struct device *dp = &tp->pdev->dev;

	if (wol->wolopts & ~WAKE_MAGIC)
		return -EINVAL;
	if ((wol->wolopts & WAKE_MAGIC) &&
	    !(tg3_flag(tp, WOL_CAP) && device_can_wakeup(dp)))
		return -EINVAL;

	device_set_wakeup_enable(dp, wol->wolopts & WAKE_MAGIC);

	spin_lock_bh(&tp->lock);
	if (device_may_wakeup(dp))
		tg3_flag_set(tp, WOL_ENABLE);
	else
		tg3_flag_clear(tp, WOL_ENABLE);
	spin_unlock_bh(&tp->lock);

	return 0;
}

static u32 tg3_get_msglevel(struct net_device *dev)
{
	struct tg3 *tp = netdev_priv(dev);
	return tp->msg_enable;
}

static void tg3_set_msglevel(struct net_device *dev, u32 value)
{
	struct tg3 *tp = netdev_priv(dev);
	tp->msg_enable = value;
}

static int tg3_nway_reset(struct net_device *dev)
{
	struct tg3 *tp = netdev_priv(dev);
	int r;

	if (!netif_running(dev))
		return -EAGAIN;

	if (tp->phy_flags & TG3_PHYFLG_PHY_SERDES)
		return -EINVAL;

	tg3_warn_mgmt_link_flap(tp);

	if (tg3_flag(tp, USE_PHYLIB)) {
		if (!(tp->phy_flags & TG3_PHYFLG_IS_CONNECTED))
			return -EAGAIN;
		r = phy_start_aneg(tp->mdio_bus->phy_map[TG3_PHY_MII_ADDR]);
	} else {
		u32 bmcr;

		spin_lock_bh(&tp->lock);
		r = -EINVAL;
		tg3_readphy(tp, MII_BMCR, &bmcr);
		if (!tg3_readphy(tp, MII_BMCR, &bmcr) &&
		    ((bmcr & BMCR_ANENABLE) ||
		     (tp->phy_flags & TG3_PHYFLG_PARALLEL_DETECT))) {
			tg3_writephy(tp, MII_BMCR, bmcr | BMCR_ANRESTART |
						   BMCR_ANENABLE);
			r = 0;
		}
		spin_unlock_bh(&tp->lock);
	}

	return r;
}

static void tg3_get_ringparam(struct net_device *dev, struct ethtool_ringparam *ering)
{
	struct tg3 *tp = netdev_priv(dev);

	ering->rx_max_pending = tp->rx_std_ring_mask;
	if (tg3_flag(tp, JUMBO_RING_ENABLE))
		ering->rx_jumbo_max_pending = tp->rx_jmb_ring_mask;
	else
		ering->rx_jumbo_max_pending = 0;

	ering->tx_max_pending = TG3_TX_RING_SIZE - 1;

	ering->rx_pending = tp->rx_pending;
	if (tg3_flag(tp, JUMBO_RING_ENABLE))
		ering->rx_jumbo_pending = tp->rx_jumbo_pending;
	else
		ering->rx_jumbo_pending = 0;

	ering->tx_pending = tp->napi[0].tx_pending;
}

static int tg3_set_ringparam(struct net_device *dev, struct ethtool_ringparam *ering)
{
	struct tg3 *tp = netdev_priv(dev);
	int i, irq_sync = 0, err = 0;

	if ((ering->rx_pending > tp->rx_std_ring_mask) ||
	    (ering->rx_jumbo_pending > tp->rx_jmb_ring_mask) ||
	    (ering->tx_pending > TG3_TX_RING_SIZE - 1) ||
	    (ering->tx_pending <= MAX_SKB_FRAGS) ||
	    (tg3_flag(tp, TSO_BUG) &&
	     (ering->tx_pending <= (MAX_SKB_FRAGS * 3))))
		return -EINVAL;

	if (netif_running(dev)) {
		tg3_phy_stop(tp);
		tg3_netif_stop(tp);
		irq_sync = 1;
	}

	tg3_full_lock(tp, irq_sync);

	tp->rx_pending = ering->rx_pending;

	if (tg3_flag(tp, MAX_RXPEND_64) &&
	    tp->rx_pending > 63)
		tp->rx_pending = 63;

	if (tg3_flag(tp, JUMBO_RING_ENABLE))
		tp->rx_jumbo_pending = ering->rx_jumbo_pending;

	for (i = 0; i < tp->irq_max; i++)
		tp->napi[i].tx_pending = ering->tx_pending;

	if (netif_running(dev)) {
		tg3_halt(tp, RESET_KIND_SHUTDOWN, 1);
		err = tg3_restart_hw(tp, false);
		if (!err)
			tg3_netif_start(tp);
	}

	tg3_full_unlock(tp);

	if (irq_sync && !err)
		tg3_phy_start(tp);

	return err;
}

static void tg3_get_pauseparam(struct net_device *dev, struct ethtool_pauseparam *epause)
{
	struct tg3 *tp = netdev_priv(dev);

	epause->autoneg = !!tg3_flag(tp, PAUSE_AUTONEG);

	if (tp->link_config.flowctrl & FLOW_CTRL_RX)
		epause->rx_pause = 1;
	else
		epause->rx_pause = 0;

	if (tp->link_config.flowctrl & FLOW_CTRL_TX)
		epause->tx_pause = 1;
	else
		epause->tx_pause = 0;
}

static int tg3_set_pauseparam(struct net_device *dev, struct ethtool_pauseparam *epause)
{
	struct tg3 *tp = netdev_priv(dev);
	int err = 0;

	if (tp->link_config.autoneg == AUTONEG_ENABLE)
		tg3_warn_mgmt_link_flap(tp);

	if (tg3_flag(tp, USE_PHYLIB)) {
		u32 newadv;
		struct phy_device *phydev;

		phydev = tp->mdio_bus->phy_map[TG3_PHY_MII_ADDR];

		if (!(phydev->supported & SUPPORTED_Pause) ||
		    (!(phydev->supported & SUPPORTED_Asym_Pause) &&
		     (epause->rx_pause != epause->tx_pause)))
			return -EINVAL;

		tp->link_config.flowctrl = 0;
		if (epause->rx_pause) {
			tp->link_config.flowctrl |= FLOW_CTRL_RX;

			if (epause->tx_pause) {
				tp->link_config.flowctrl |= FLOW_CTRL_TX;
				newadv = ADVERTISED_Pause;
			} else
				newadv = ADVERTISED_Pause |
					 ADVERTISED_Asym_Pause;
		} else if (epause->tx_pause) {
			tp->link_config.flowctrl |= FLOW_CTRL_TX;
			newadv = ADVERTISED_Asym_Pause;
		} else
			newadv = 0;

		if (epause->autoneg)
			tg3_flag_set(tp, PAUSE_AUTONEG);
		else
			tg3_flag_clear(tp, PAUSE_AUTONEG);

		if (tp->phy_flags & TG3_PHYFLG_IS_CONNECTED) {
			u32 oldadv = phydev->advertising &
				     (ADVERTISED_Pause | ADVERTISED_Asym_Pause);
			if (oldadv != newadv) {
				phydev->advertising &=
					~(ADVERTISED_Pause |
					  ADVERTISED_Asym_Pause);
				phydev->advertising |= newadv;
				if (phydev->autoneg) {
					/*
					 * Always renegotiate the link to
					 * inform our link partner of our
					 * flow control settings, even if the
					 * flow control is forced.  Let
					 * tg3_adjust_link() do the final
					 * flow control setup.
					 */
					return phy_start_aneg(phydev);
				}
			}

			if (!epause->autoneg)
				tg3_setup_flow_control(tp, 0, 0);
		} else {
			tp->link_config.advertising &=
					~(ADVERTISED_Pause |
					  ADVERTISED_Asym_Pause);
			tp->link_config.advertising |= newadv;
		}
	} else {
		int irq_sync = 0;

		if (netif_running(dev)) {
			tg3_netif_stop(tp);
			irq_sync = 1;
		}

		tg3_full_lock(tp, irq_sync);

		if (epause->autoneg)
			tg3_flag_set(tp, PAUSE_AUTONEG);
		else
			tg3_flag_clear(tp, PAUSE_AUTONEG);
		if (epause->rx_pause)
			tp->link_config.flowctrl |= FLOW_CTRL_RX;
		else
			tp->link_config.flowctrl &= ~FLOW_CTRL_RX;
		if (epause->tx_pause)
			tp->link_config.flowctrl |= FLOW_CTRL_TX;
		else
			tp->link_config.flowctrl &= ~FLOW_CTRL_TX;

		if (netif_running(dev)) {
			tg3_halt(tp, RESET_KIND_SHUTDOWN, 1);
			err = tg3_restart_hw(tp, false);
			if (!err)
				tg3_netif_start(tp);
		}

		tg3_full_unlock(tp);
	}

	tp->phy_flags |= TG3_PHYFLG_USER_CONFIGURED;

	return err;
}

static int tg3_get_sset_count(struct net_device *dev, int sset)
{
	switch (sset) {
	case ETH_SS_TEST:
		return TG3_NUM_TEST;
	case ETH_SS_STATS:
		return TG3_NUM_STATS;
	default:
		return -EOPNOTSUPP;
	}
}

static int tg3_get_rxnfc(struct net_device *dev, struct ethtool_rxnfc *info,
			 u32 *rules __always_unused)
{
	struct tg3 *tp = netdev_priv(dev);

	if (!tg3_flag(tp, SUPPORT_MSIX))
		return -EOPNOTSUPP;

	switch (info->cmd) {
	case ETHTOOL_GRXRINGS:
		if (netif_running(tp->dev))
			info->data = tp->rxq_cnt;
		else {
			info->data = num_online_cpus();
			if (info->data > TG3_RSS_MAX_NUM_QS)
				info->data = TG3_RSS_MAX_NUM_QS;
		}

		/* The first interrupt vector only
		 * handles link interrupts.
		 */
		info->data -= 1;
		return 0;

	default:
		return -EOPNOTSUPP;
	}
}

static u32 tg3_get_rxfh_indir_size(struct net_device *dev)
{
	u32 size = 0;
	struct tg3 *tp = netdev_priv(dev);

	if (tg3_flag(tp, SUPPORT_MSIX))
		size = TG3_RSS_INDIR_TBL_SIZE;

	return size;
}

static int tg3_get_rxfh_indir(struct net_device *dev, u32 *indir)
{
	struct tg3 *tp = netdev_priv(dev);
	int i;

	for (i = 0; i < TG3_RSS_INDIR_TBL_SIZE; i++)
		indir[i] = tp->rss_ind_tbl[i];

	return 0;
}

static int tg3_set_rxfh_indir(struct net_device *dev, const u32 *indir)
{
	struct tg3 *tp = netdev_priv(dev);
	size_t i;

	for (i = 0; i < TG3_RSS_INDIR_TBL_SIZE; i++)
		tp->rss_ind_tbl[i] = indir[i];

	if (!netif_running(dev) || !tg3_flag(tp, ENABLE_RSS))
		return 0;

	/* It is legal to write the indirection
	 * table while the device is running.
	 */
	tg3_full_lock(tp, 0);
	tg3_rss_write_indir_tbl(tp);
	tg3_full_unlock(tp);

	return 0;
}

static void tg3_get_channels(struct net_device *dev,
			     struct ethtool_channels *channel)
{
	struct tg3 *tp = netdev_priv(dev);
	u32 deflt_qs = netif_get_num_default_rss_queues();

	channel->max_rx = tp->rxq_max;
	channel->max_tx = tp->txq_max;

	if (netif_running(dev)) {
		channel->rx_count = tp->rxq_cnt;
		channel->tx_count = tp->txq_cnt;
	} else {
		if (tp->rxq_req)
			channel->rx_count = tp->rxq_req;
		else
			channel->rx_count = min(deflt_qs, tp->rxq_max);

		if (tp->txq_req)
			channel->tx_count = tp->txq_req;
		else
			channel->tx_count = min(deflt_qs, tp->txq_max);
	}
}

static int tg3_set_channels(struct net_device *dev,
			    struct ethtool_channels *channel)
{
	struct tg3 *tp = netdev_priv(dev);

	if (!tg3_flag(tp, SUPPORT_MSIX))
		return -EOPNOTSUPP;

	if (channel->rx_count > tp->rxq_max ||
	    channel->tx_count > tp->txq_max)
		return -EINVAL;

	tp->rxq_req = channel->rx_count;
	tp->txq_req = channel->tx_count;

	if (!netif_running(dev))
		return 0;

	tg3_stop(tp);

	tg3_carrier_off(tp);

	tg3_start(tp, true, false, false);

	return 0;
}

static void tg3_get_strings(struct net_device *dev, u32 stringset, u8 *buf)
{
	switch (stringset) {
	case ETH_SS_STATS:
		memcpy(buf, &ethtool_stats_keys, sizeof(ethtool_stats_keys));
		break;
	case ETH_SS_TEST:
		memcpy(buf, &ethtool_test_keys, sizeof(ethtool_test_keys));
		break;
	default:
		WARN_ON(1);	/* we need a WARN() */
		break;
	}
}

static int tg3_set_phys_id(struct net_device *dev,
			    enum ethtool_phys_id_state state)
{
	struct tg3 *tp = netdev_priv(dev);

	if (!netif_running(tp->dev))
		return -EAGAIN;

	switch (state) {
	case ETHTOOL_ID_ACTIVE:
		return 1;	/* cycle on/off once per second */

	case ETHTOOL_ID_ON:
		tw32(MAC_LED_CTRL, LED_CTRL_LNKLED_OVERRIDE |
		     LED_CTRL_1000MBPS_ON |
		     LED_CTRL_100MBPS_ON |
		     LED_CTRL_10MBPS_ON |
		     LED_CTRL_TRAFFIC_OVERRIDE |
		     LED_CTRL_TRAFFIC_BLINK |
		     LED_CTRL_TRAFFIC_LED);
		break;

	case ETHTOOL_ID_OFF:
		tw32(MAC_LED_CTRL, LED_CTRL_LNKLED_OVERRIDE |
		     LED_CTRL_TRAFFIC_OVERRIDE);
		break;

	case ETHTOOL_ID_INACTIVE:
		tw32(MAC_LED_CTRL, tp->led_ctrl);
		break;
	}

	return 0;
}

static void tg3_get_ethtool_stats(struct net_device *dev,
				   struct ethtool_stats *estats, u64 *tmp_stats)
{
	struct tg3 *tp = netdev_priv(dev);

	if (tp->hw_stats)
		tg3_get_estats(tp, (struct tg3_ethtool_stats *)tmp_stats);
	else
		memset(tmp_stats, 0, sizeof(struct tg3_ethtool_stats));
}

static __be32 *tg3_vpd_readblock(struct tg3 *tp, u32 *vpdlen)
{
	int i;
	__be32 *buf;
	u32 offset = 0, len = 0;
	u32 magic, val;

	if (tg3_flag(tp, NO_NVRAM) || tg3_nvram_read(tp, 0, &magic))
		return NULL;

	if (magic == TG3_EEPROM_MAGIC) {
		for (offset = TG3_NVM_DIR_START;
		     offset < TG3_NVM_DIR_END;
		     offset += TG3_NVM_DIRENT_SIZE) {
			if (tg3_nvram_read(tp, offset, &val))
				return NULL;

			if ((val >> TG3_NVM_DIRTYPE_SHIFT) ==
			    TG3_NVM_DIRTYPE_EXTVPD)
				break;
		}

		if (offset != TG3_NVM_DIR_END) {
			len = (val & TG3_NVM_DIRTYPE_LENMSK) * 4;
			if (tg3_nvram_read(tp, offset + 4, &offset))
				return NULL;

			offset = tg3_nvram_logical_addr(tp, offset);
		}
	}

	if (!offset || !len) {
		offset = TG3_NVM_VPD_OFF;
		len = TG3_NVM_VPD_LEN;
	}

	buf = kmalloc(len, GFP_KERNEL);
	if (buf == NULL)
		return NULL;

	if (magic == TG3_EEPROM_MAGIC) {
		for (i = 0; i < len; i += 4) {
			/* The data is in little-endian format in NVRAM.
			 * Use the big-endian read routines to preserve
			 * the byte order as it exists in NVRAM.
			 */
			if (tg3_nvram_read_be32(tp, offset + i, &buf[i/4]))
				goto error;
		}
	} else {
		u8 *ptr;
		ssize_t cnt;
		unsigned int pos = 0;

		ptr = (u8 *)&buf[0];
		for (i = 0; pos < len && i < 3; i++, pos += cnt, ptr += cnt) {
			cnt = pci_read_vpd(tp->pdev, pos,
					   len - pos, ptr);
			if (cnt == -ETIMEDOUT || cnt == -EINTR)
				cnt = 0;
			else if (cnt < 0)
				goto error;
		}
		if (pos != len)
			goto error;
	}

	*vpdlen = len;

	return buf;

error:
	kfree(buf);
	return NULL;
}

#define NVRAM_TEST_SIZE 0x100
#define NVRAM_SELFBOOT_FORMAT1_0_SIZE	0x14
#define NVRAM_SELFBOOT_FORMAT1_2_SIZE	0x18
#define NVRAM_SELFBOOT_FORMAT1_3_SIZE	0x1c
#define NVRAM_SELFBOOT_FORMAT1_4_SIZE	0x20
#define NVRAM_SELFBOOT_FORMAT1_5_SIZE	0x24
#define NVRAM_SELFBOOT_FORMAT1_6_SIZE	0x50
#define NVRAM_SELFBOOT_HW_SIZE 0x20
#define NVRAM_SELFBOOT_DATA_SIZE 0x1c

static int tg3_test_nvram(struct tg3 *tp)
{
	u32 csum, magic, len;
	__be32 *buf;
	int i, j, k, err = 0, size;

	if (tg3_flag(tp, NO_NVRAM))
		return 0;

	if (tg3_nvram_read(tp, 0, &magic) != 0)
		return -EIO;

	if (magic == TG3_EEPROM_MAGIC)
		size = NVRAM_TEST_SIZE;
	else if ((magic & TG3_EEPROM_MAGIC_FW_MSK) == TG3_EEPROM_MAGIC_FW) {
		if ((magic & TG3_EEPROM_SB_FORMAT_MASK) ==
		    TG3_EEPROM_SB_FORMAT_1) {
			switch (magic & TG3_EEPROM_SB_REVISION_MASK) {
			case TG3_EEPROM_SB_REVISION_0:
				size = NVRAM_SELFBOOT_FORMAT1_0_SIZE;
				break;
			case TG3_EEPROM_SB_REVISION_2:
				size = NVRAM_SELFBOOT_FORMAT1_2_SIZE;
				break;
			case TG3_EEPROM_SB_REVISION_3:
				size = NVRAM_SELFBOOT_FORMAT1_3_SIZE;
				break;
			case TG3_EEPROM_SB_REVISION_4:
				size = NVRAM_SELFBOOT_FORMAT1_4_SIZE;
				break;
			case TG3_EEPROM_SB_REVISION_5:
				size = NVRAM_SELFBOOT_FORMAT1_5_SIZE;
				break;
			case TG3_EEPROM_SB_REVISION_6:
				size = NVRAM_SELFBOOT_FORMAT1_6_SIZE;
				break;
			default:
				return -EIO;
			}
		} else
			return 0;
	} else if ((magic & TG3_EEPROM_MAGIC_HW_MSK) == TG3_EEPROM_MAGIC_HW)
		size = NVRAM_SELFBOOT_HW_SIZE;
	else
		return -EIO;

	buf = kmalloc(size, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	err = -EIO;
	for (i = 0, j = 0; i < size; i += 4, j++) {
		err = tg3_nvram_read_be32(tp, i, &buf[j]);
		if (err)
			break;
	}
	if (i < size)
		goto out;

	/* Selfboot format */
	magic = be32_to_cpu(buf[0]);
	if ((magic & TG3_EEPROM_MAGIC_FW_MSK) ==
	    TG3_EEPROM_MAGIC_FW) {
		u8 *buf8 = (u8 *) buf, csum8 = 0;

		if ((magic & TG3_EEPROM_SB_REVISION_MASK) ==
		    TG3_EEPROM_SB_REVISION_2) {
			/* For rev 2, the csum doesn't include the MBA. */
			for (i = 0; i < TG3_EEPROM_SB_F1R2_MBA_OFF; i++)
				csum8 += buf8[i];
			for (i = TG3_EEPROM_SB_F1R2_MBA_OFF + 4; i < size; i++)
				csum8 += buf8[i];
		} else {
			for (i = 0; i < size; i++)
				csum8 += buf8[i];
		}

		if (csum8 == 0) {
			err = 0;
			goto out;
		}

		err = -EIO;
		goto out;
	}

	if ((magic & TG3_EEPROM_MAGIC_HW_MSK) ==
	    TG3_EEPROM_MAGIC_HW) {
		u8 data[NVRAM_SELFBOOT_DATA_SIZE];
		u8 parity[NVRAM_SELFBOOT_DATA_SIZE];
		u8 *buf8 = (u8 *) buf;

		/* Separate the parity bits and the data bytes.  */
		for (i = 0, j = 0, k = 0; i < NVRAM_SELFBOOT_HW_SIZE; i++) {
			if ((i == 0) || (i == 8)) {
				int l;
				u8 msk;

				for (l = 0, msk = 0x80; l < 7; l++, msk >>= 1)
					parity[k++] = buf8[i] & msk;
				i++;
			} else if (i == 16) {
				int l;
				u8 msk;

				for (l = 0, msk = 0x20; l < 6; l++, msk >>= 1)
					parity[k++] = buf8[i] & msk;
				i++;

				for (l = 0, msk = 0x80; l < 8; l++, msk >>= 1)
					parity[k++] = buf8[i] & msk;
				i++;
			}
			data[j++] = buf8[i];
		}

		err = -EIO;
		for (i = 0; i < NVRAM_SELFBOOT_DATA_SIZE; i++) {
			u8 hw8 = hweight8(data[i]);

			if ((hw8 & 0x1) && parity[i])
				goto out;
			else if (!(hw8 & 0x1) && !parity[i])
				goto out;
		}
		err = 0;
		goto out;
	}

	err = -EIO;

	/* Bootstrap checksum at offset 0x10 */
	csum = calc_crc((unsigned char *) buf, 0x10);
	if (csum != le32_to_cpu(buf[0x10/4]))
		goto out;

	/* Manufacturing block starts at offset 0x74, checksum at 0xfc */
	csum = calc_crc((unsigned char *) &buf[0x74/4], 0x88);
	if (csum != le32_to_cpu(buf[0xfc/4]))
		goto out;

	kfree(buf);

	buf = tg3_vpd_readblock(tp, &len);
	if (!buf)
		return -ENOMEM;

	i = pci_vpd_find_tag((u8 *)buf, 0, len, PCI_VPD_LRDT_RO_DATA);
	if (i > 0) {
		j = pci_vpd_lrdt_size(&((u8 *)buf)[i]);
		if (j < 0)
			goto out;

		if (i + PCI_VPD_LRDT_TAG_SIZE + j > len)
			goto out;

		i += PCI_VPD_LRDT_TAG_SIZE;
		j = pci_vpd_find_info_keyword((u8 *)buf, i, j,
					      PCI_VPD_RO_KEYWORD_CHKSUM);
		if (j > 0) {
			u8 csum8 = 0;

			j += PCI_VPD_INFO_FLD_HDR_SIZE;

			for (i = 0; i <= j; i++)
				csum8 += ((u8 *)buf)[i];

			if (csum8)
				goto out;
		}
	}

	err = 0;

out:
	kfree(buf);
	return err;
}

#define TG3_SERDES_TIMEOUT_SEC	2
#define TG3_COPPER_TIMEOUT_SEC	6

static int tg3_test_link(struct tg3 *tp)
{
	int i, max;

	if (!netif_running(tp->dev))
		return -ENODEV;

	if (tp->phy_flags & TG3_PHYFLG_ANY_SERDES)
		max = TG3_SERDES_TIMEOUT_SEC;
	else
		max = TG3_COPPER_TIMEOUT_SEC;

	for (i = 0; i < max; i++) {
		if (tp->link_up)
			return 0;

		if (msleep_interruptible(1000))
			break;
	}

	return -EIO;
}

/* Only test the commonly used registers */
static int tg3_test_registers(struct tg3 *tp)
{
	int i, is_5705, is_5750;
	u32 offset, read_mask, write_mask, val, save_val, read_val;
	static struct {
		u16 offset;
		u16 flags;
#define TG3_FL_5705	0x1
#define TG3_FL_NOT_5705	0x2
#define TG3_FL_NOT_5788	0x4
#define TG3_FL_NOT_5750	0x8
		u32 read_mask;
		u32 write_mask;
	} reg_tbl[] = {
		/* MAC Control Registers */
		{ MAC_MODE, TG3_FL_NOT_5705,
			0x00000000, 0x00ef6f8c },
		{ MAC_MODE, TG3_FL_5705,
			0x00000000, 0x01ef6b8c },
		{ MAC_STATUS, TG3_FL_NOT_5705,
			0x03800107, 0x00000000 },
		{ MAC_STATUS, TG3_FL_5705,
			0x03800100, 0x00000000 },
		{ MAC_ADDR_0_HIGH, 0x0000,
			0x00000000, 0x0000ffff },
		{ MAC_ADDR_0_LOW, 0x0000,
			0x00000000, 0xffffffff },
		{ MAC_RX_MTU_SIZE, 0x0000,
			0x00000000, 0x0000ffff },
		{ MAC_TX_MODE, 0x0000,
			0x00000000, 0x00000070 },
		{ MAC_TX_LENGTHS, 0x0000,
			0x00000000, 0x00003fff },
		{ MAC_RX_MODE, TG3_FL_NOT_5705,
			0x00000000, 0x000007fc },
		{ MAC_RX_MODE, TG3_FL_5705,
			0x00000000, 0x000007dc },
		{ MAC_HASH_REG_0, 0x0000,
			0x00000000, 0xffffffff },
		{ MAC_HASH_REG_1, 0x0000,
			0x00000000, 0xffffffff },
		{ MAC_HASH_REG_2, 0x0000,
			0x00000000, 0xffffffff },
		{ MAC_HASH_REG_3, 0x0000,
			0x00000000, 0xffffffff },

		/* Receive Data and Receive BD Initiator Control Registers. */
		{ RCVDBDI_JUMBO_BD+0, TG3_FL_NOT_5705,
			0x00000000, 0xffffffff },
		{ RCVDBDI_JUMBO_BD+4, TG3_FL_NOT_5705,
			0x00000000, 0xffffffff },
		{ RCVDBDI_JUMBO_BD+8, TG3_FL_NOT_5705,
			0x00000000, 0x00000003 },
		{ RCVDBDI_JUMBO_BD+0xc, TG3_FL_NOT_5705,
			0x00000000, 0xffffffff },
		{ RCVDBDI_STD_BD+0, 0x0000,
			0x00000000, 0xffffffff },
		{ RCVDBDI_STD_BD+4, 0x0000,
			0x00000000, 0xffffffff },
		{ RCVDBDI_STD_BD+8, 0x0000,
			0x00000000, 0xffff0002 },
		{ RCVDBDI_STD_BD+0xc, 0x0000,
			0x00000000, 0xffffffff },

		/* Receive BD Initiator Control Registers. */
		{ RCVBDI_STD_THRESH, TG3_FL_NOT_5705,
			0x00000000, 0xffffffff },
		{ RCVBDI_STD_THRESH, TG3_FL_5705,
			0x00000000, 0x000003ff },
		{ RCVBDI_JUMBO_THRESH, TG3_FL_NOT_5705,
			0x00000000, 0xffffffff },

		/* Host Coalescing Control Registers. */
		{ HOSTCC_MODE, TG3_FL_NOT_5705,
			0x00000000, 0x00000004 },
		{ HOSTCC_MODE, TG3_FL_5705,
			0x00000000, 0x000000f6 },
		{ HOSTCC_RXCOL_TICKS, TG3_FL_NOT_5705,
			0x00000000, 0xffffffff },
		{ HOSTCC_RXCOL_TICKS, TG3_FL_5705,
			0x00000000, 0x000003ff },
		{ HOSTCC_TXCOL_TICKS, TG3_FL_NOT_5705,
			0x00000000, 0xffffffff },
		{ HOSTCC_TXCOL_TICKS, TG3_FL_5705,
			0x00000000, 0x000003ff },
		{ HOSTCC_RXMAX_FRAMES, TG3_FL_NOT_5705,
			0x00000000, 0xffffffff },
		{ HOSTCC_RXMAX_FRAMES, TG3_FL_5705 | TG3_FL_NOT_5788,
			0x00000000, 0x000000ff },
		{ HOSTCC_TXMAX_FRAMES, TG3_FL_NOT_5705,
			0x00000000, 0xffffffff },
		{ HOSTCC_TXMAX_FRAMES, TG3_FL_5705 | TG3_FL_NOT_5788,
			0x00000000, 0x000000ff },
		{ HOSTCC_RXCOAL_TICK_INT, TG3_FL_NOT_5705,
			0x00000000, 0xffffffff },
		{ HOSTCC_TXCOAL_TICK_INT, TG3_FL_NOT_5705,
			0x00000000, 0xffffffff },
		{ HOSTCC_RXCOAL_MAXF_INT, TG3_FL_NOT_5705,
			0x00000000, 0xffffffff },
		{ HOSTCC_RXCOAL_MAXF_INT, TG3_FL_5705 | TG3_FL_NOT_5788,
			0x00000000, 0x000000ff },
		{ HOSTCC_TXCOAL_MAXF_INT, TG3_FL_NOT_5705,
			0x00000000, 0xffffffff },
		{ HOSTCC_TXCOAL_MAXF_INT, TG3_FL_5705 | TG3_FL_NOT_5788,
			0x00000000, 0x000000ff },
		{ HOSTCC_STAT_COAL_TICKS, TG3_FL_NOT_5705,
			0x00000000, 0xffffffff },
		{ HOSTCC_STATS_BLK_HOST_ADDR, TG3_FL_NOT_5705,
			0x00000000, 0xffffffff },
		{ HOSTCC_STATS_BLK_HOST_ADDR+4, TG3_FL_NOT_5705,
			0x00000000, 0xffffffff },
		{ HOSTCC_STATUS_BLK_HOST_ADDR, 0x0000,
			0x00000000, 0xffffffff },
		{ HOSTCC_STATUS_BLK_HOST_ADDR+4, 0x0000,
			0x00000000, 0xffffffff },
		{ HOSTCC_STATS_BLK_NIC_ADDR, 0x0000,
			0xffffffff, 0x00000000 },
		{ HOSTCC_STATUS_BLK_NIC_ADDR, 0x0000,
			0xffffffff, 0x00000000 },

		/* Buffer Manager Control Registers. */
		{ BUFMGR_MB_POOL_ADDR, TG3_FL_NOT_5750,
			0x00000000, 0x007fff80 },
		{ BUFMGR_MB_POOL_SIZE, TG3_FL_NOT_5750,
			0x00000000, 0x007fffff },
		{ BUFMGR_MB_RDMA_LOW_WATER, 0x0000,
			0x00000000, 0x0000003f },
		{ BUFMGR_MB_MACRX_LOW_WATER, 0x0000,
			0x00000000, 0x000001ff },
		{ BUFMGR_MB_HIGH_WATER, 0x0000,
			0x00000000, 0x000001ff },
		{ BUFMGR_DMA_DESC_POOL_ADDR, TG3_FL_NOT_5705,
			0xffffffff, 0x00000000 },
		{ BUFMGR_DMA_DESC_POOL_SIZE, TG3_FL_NOT_5705,
			0xffffffff, 0x00000000 },

		/* Mailbox Registers */
		{ GRCMBOX_RCVSTD_PROD_IDX+4, 0x0000,
			0x00000000, 0x000001ff },
		{ GRCMBOX_RCVJUMBO_PROD_IDX+4, TG3_FL_NOT_5705,
			0x00000000, 0x000001ff },
		{ GRCMBOX_RCVRET_CON_IDX_0+4, 0x0000,
			0x00000000, 0x000007ff },
		{ GRCMBOX_SNDHOST_PROD_IDX_0+4, 0x0000,
			0x00000000, 0x000001ff },

		{ 0xffff, 0x0000, 0x00000000, 0x00000000 },
	};

	is_5705 = is_5750 = 0;
	if (tg3_flag(tp, 5705_PLUS)) {
		is_5705 = 1;
		if (tg3_flag(tp, 5750_PLUS))
			is_5750 = 1;
	}

	for (i = 0; reg_tbl[i].offset != 0xffff; i++) {
		if (is_5705 && (reg_tbl[i].flags & TG3_FL_NOT_5705))
			continue;

		if (!is_5705 && (reg_tbl[i].flags & TG3_FL_5705))
			continue;

		if (tg3_flag(tp, IS_5788) &&
		    (reg_tbl[i].flags & TG3_FL_NOT_5788))
			continue;

		if (is_5750 && (reg_tbl[i].flags & TG3_FL_NOT_5750))
			continue;

		offset = (u32) reg_tbl[i].offset;
		read_mask = reg_tbl[i].read_mask;
		write_mask = reg_tbl[i].write_mask;

		/* Save the original register content */
		save_val = tr32(offset);

		/* Determine the read-only value. */
		read_val = save_val & read_mask;

		/* Write zero to the register, then make sure the read-only bits
		 * are not changed and the read/write bits are all zeros.
		 */
		tw32(offset, 0);

		val = tr32(offset);

		/* Test the read-only and read/write bits. */
		if (((val & read_mask) != read_val) || (val & write_mask))
			goto out;

		/* Write ones to all the bits defined by RdMask and WrMask, then
		 * make sure the read-only bits are not changed and the
		 * read/write bits are all ones.
		 */
		tw32(offset, read_mask | write_mask);

		val = tr32(offset);

		/* Test the read-only bits. */
		if ((val & read_mask) != read_val)
			goto out;

		/* Test the read/write bits. */
		if ((val & write_mask) != write_mask)
			goto out;

		tw32(offset, save_val);
	}

	return 0;

out:
	if (netif_msg_hw(tp))
		netdev_err(tp->dev,
			   "Register test failed at offset %x\n", offset);
	tw32(offset, save_val);
	return -EIO;
}

static int tg3_do_mem_test(struct tg3 *tp, u32 offset, u32 len)
{
	static const u32 test_pattern[] = { 0x00000000, 0xffffffff, 0xaa55a55a };
	int i;
	u32 j;

	for (i = 0; i < ARRAY_SIZE(test_pattern); i++) {
		for (j = 0; j < len; j += 4) {
			u32 val;

			tg3_write_mem(tp, offset + j, test_pattern[i]);
			tg3_read_mem(tp, offset + j, &val);
			if (val != test_pattern[i])
				return -EIO;
		}
	}
	return 0;
}

static int tg3_test_memory(struct tg3 *tp)
{
	static struct mem_entry {
		u32 offset;
		u32 len;
	} mem_tbl_570x[] = {
		{ 0x00000000, 0x00b50},
		{ 0x00002000, 0x1c000},
		{ 0xffffffff, 0x00000}
	}, mem_tbl_5705[] = {
		{ 0x00000100, 0x0000c},
		{ 0x00000200, 0x00008},
		{ 0x00004000, 0x00800},
		{ 0x00006000, 0x01000},
		{ 0x00008000, 0x02000},
		{ 0x00010000, 0x0e000},
		{ 0xffffffff, 0x00000}
	}, mem_tbl_5755[] = {
		{ 0x00000200, 0x00008},
		{ 0x00004000, 0x00800},
		{ 0x00006000, 0x00800},
		{ 0x00008000, 0x02000},
		{ 0x00010000, 0x0c000},
		{ 0xffffffff, 0x00000}
	}, mem_tbl_5906[] = {
		{ 0x00000200, 0x00008},
		{ 0x00004000, 0x00400},
		{ 0x00006000, 0x00400},
		{ 0x00008000, 0x01000},
		{ 0x00010000, 0x01000},
		{ 0xffffffff, 0x00000}
	}, mem_tbl_5717[] = {
		{ 0x00000200, 0x00008},
		{ 0x00010000, 0x0a000},
		{ 0x00020000, 0x13c00},
		{ 0xffffffff, 0x00000}
	}, mem_tbl_57765[] = {
		{ 0x00000200, 0x00008},
		{ 0x00004000, 0x00800},
		{ 0x00006000, 0x09800},
		{ 0x00010000, 0x0a000},
		{ 0xffffffff, 0x00000}
	};
	struct mem_entry *mem_tbl;
	int err = 0;
	int i;

	if (tg3_flag(tp, 5717_PLUS))
		mem_tbl = mem_tbl_5717;
	else if (tg3_flag(tp, 57765_CLASS) ||
		 tg3_asic_rev(tp) == ASIC_REV_5762)
		mem_tbl = mem_tbl_57765;
	else if (tg3_flag(tp, 5755_PLUS))
		mem_tbl = mem_tbl_5755;
	else if (tg3_asic_rev(tp) == ASIC_REV_5906)
		mem_tbl = mem_tbl_5906;
	else if (tg3_flag(tp, 5705_PLUS))
		mem_tbl = mem_tbl_5705;
	else
		mem_tbl = mem_tbl_570x;

	for (i = 0; mem_tbl[i].offset != 0xffffffff; i++) {
		err = tg3_do_mem_test(tp, mem_tbl[i].offset, mem_tbl[i].len);
		if (err)
			break;
	}

	return err;
}

#define TG3_TSO_MSS		500

#define TG3_TSO_IP_HDR_LEN	20
#define TG3_TSO_TCP_HDR_LEN	20
#define TG3_TSO_TCP_OPT_LEN	12

static const u8 tg3_tso_header[] = {
0x08, 0x00,
0x45, 0x00, 0x00, 0x00,
0x00, 0x00, 0x40, 0x00,
0x40, 0x06, 0x00, 0x00,
0x0a, 0x00, 0x00, 0x01,
0x0a, 0x00, 0x00, 0x02,
0x0d, 0x00, 0xe0, 0x00,
0x00, 0x00, 0x01, 0x00,
0x00, 0x00, 0x02, 0x00,
0x80, 0x10, 0x10, 0x00,
0x14, 0x09, 0x00, 0x00,
0x01, 0x01, 0x08, 0x0a,
0x11, 0x11, 0x11, 0x11,
0x11, 0x11, 0x11, 0x11,
};

static int tg3_run_loopback(struct tg3 *tp, u32 pktsz, bool tso_loopback)
{
	u32 rx_start_idx, rx_idx, tx_idx, opaque_key;
	u32 base_flags = 0, mss = 0, desc_idx, coal_now, data_off, val;
	u32 budget;
	struct sk_buff *skb;
	u8 *tx_data, *rx_data;
	dma_addr_t map;
	int num_pkts, tx_len, rx_len, i, err;
	struct tg3_rx_buffer_desc *desc;
	struct tg3_napi *tnapi, *rnapi;
	struct tg3_rx_prodring_set *tpr = &tp->napi[0].prodring;

	tnapi = &tp->napi[0];
	rnapi = &tp->napi[0];
	if (tp->irq_cnt > 1) {
		if (tg3_flag(tp, ENABLE_RSS))
			rnapi = &tp->napi[1];
		if (tg3_flag(tp, ENABLE_TSS))
			tnapi = &tp->napi[1];
	}
	coal_now = tnapi->coal_now | rnapi->coal_now;

	err = -EIO;

	tx_len = pktsz;
	skb = netdev_alloc_skb(tp->dev, tx_len);
	if (!skb)
		return -ENOMEM;

	tx_data = skb_put(skb, tx_len);
	memcpy(tx_data, tp->dev->dev_addr, 6);
	memset(tx_data + 6, 0x0, 8);

	tw32(MAC_RX_MTU_SIZE, tx_len + ETH_FCS_LEN);

	if (tso_loopback) {
		struct iphdr *iph = (struct iphdr *)&tx_data[ETH_HLEN];

		u32 hdr_len = TG3_TSO_IP_HDR_LEN + TG3_TSO_TCP_HDR_LEN +
			      TG3_TSO_TCP_OPT_LEN;

		memcpy(tx_data + ETH_ALEN * 2, tg3_tso_header,
		       sizeof(tg3_tso_header));
		mss = TG3_TSO_MSS;

		val = tx_len - ETH_ALEN * 2 - sizeof(tg3_tso_header);
		num_pkts = DIV_ROUND_UP(val, TG3_TSO_MSS);

		/* Set the total length field in the IP header */
		iph->tot_len = htons((u16)(mss + hdr_len));

		base_flags = (TXD_FLAG_CPU_PRE_DMA |
			      TXD_FLAG_CPU_POST_DMA);

		if (tg3_flag(tp, HW_TSO_1) ||
		    tg3_flag(tp, HW_TSO_2) ||
		    tg3_flag(tp, HW_TSO_3)) {
			struct tcphdr *th;
			val = ETH_HLEN + TG3_TSO_IP_HDR_LEN;
			th = (struct tcphdr *)&tx_data[val];
			th->check = 0;
		} else
			base_flags |= TXD_FLAG_TCPUDP_CSUM;

		if (tg3_flag(tp, HW_TSO_3)) {
			mss |= (hdr_len & 0xc) << 12;
			if (hdr_len & 0x10)
				base_flags |= 0x00000010;
			base_flags |= (hdr_len & 0x3e0) << 5;
		} else if (tg3_flag(tp, HW_TSO_2))
			mss |= hdr_len << 9;
		else if (tg3_flag(tp, HW_TSO_1) ||
			 tg3_asic_rev(tp) == ASIC_REV_5705) {
			mss |= (TG3_TSO_TCP_OPT_LEN << 9);
		} else {
			base_flags |= (TG3_TSO_TCP_OPT_LEN << 10);
		}

		data_off = ETH_ALEN * 2 + sizeof(tg3_tso_header);
	} else {
		num_pkts = 1;
		data_off = ETH_HLEN;

		if (tg3_flag(tp, USE_JUMBO_BDFLAG) &&
		    tx_len > VLAN_ETH_FRAME_LEN)
			base_flags |= TXD_FLAG_JMB_PKT;
	}

	for (i = data_off; i < tx_len; i++)
		tx_data[i] = (u8) (i & 0xff);

	map = pci_map_single(tp->pdev, skb->data, tx_len, PCI_DMA_TODEVICE);
	if (pci_dma_mapping_error(tp->pdev, map)) {
		dev_kfree_skb(skb);
		return -EIO;
	}

	val = tnapi->tx_prod;
	tnapi->tx_buffers[val].skb = skb;
	dma_unmap_addr_set(&tnapi->tx_buffers[val], mapping, map);

	tw32_f(HOSTCC_MODE, tp->coalesce_mode | HOSTCC_MODE_ENABLE |
	       rnapi->coal_now);

	udelay(10);

	rx_start_idx = rnapi->hw_status->idx[0].rx_producer;

	budget = tg3_tx_avail(tnapi);
	if (tg3_tx_frag_set(tnapi, &val, &budget, map, tx_len,
			    base_flags | TXD_FLAG_END, mss, 0)) {
		tnapi->tx_buffers[val].skb = NULL;
		dev_kfree_skb(skb);
		return -EIO;
	}

	tnapi->tx_prod++;

	/* Sync BD data before updating mailbox */
	wmb();

	tw32_tx_mbox(tnapi->prodmbox, tnapi->tx_prod);
	tr32_mailbox(tnapi->prodmbox);

	udelay(10);

	/* 350 usec to allow enough time on some 10/100 Mbps devices.  */
	for (i = 0; i < 35; i++) {
		tw32_f(HOSTCC_MODE, tp->coalesce_mode | HOSTCC_MODE_ENABLE |
		       coal_now);

		udelay(10);

		tx_idx = tnapi->hw_status->idx[0].tx_consumer;
		rx_idx = rnapi->hw_status->idx[0].rx_producer;
		if ((tx_idx == tnapi->tx_prod) &&
		    (rx_idx == (rx_start_idx + num_pkts)))
			break;
	}

	tg3_tx_skb_unmap(tnapi, tnapi->tx_prod - 1, -1);
	dev_kfree_skb(skb);

	if (tx_idx != tnapi->tx_prod)
		goto out;

	if (rx_idx != rx_start_idx + num_pkts)
		goto out;

	val = data_off;
	while (rx_idx != rx_start_idx) {
		desc = &rnapi->rx_rcb[rx_start_idx++];
		desc_idx = desc->opaque & RXD_OPAQUE_INDEX_MASK;
		opaque_key = desc->opaque & RXD_OPAQUE_RING_MASK;

		if ((desc->err_vlan & RXD_ERR_MASK) != 0 &&
		    (desc->err_vlan != RXD_ERR_ODD_NIBBLE_RCVD_MII))
			goto out;

		rx_len = ((desc->idx_len & RXD_LEN_MASK) >> RXD_LEN_SHIFT)
			 - ETH_FCS_LEN;

		if (!tso_loopback) {
			if (rx_len != tx_len)
				goto out;

			if (pktsz <= TG3_RX_STD_DMA_SZ - ETH_FCS_LEN) {
				if (opaque_key != RXD_OPAQUE_RING_STD)
					goto out;
			} else {
				if (opaque_key != RXD_OPAQUE_RING_JUMBO)
					goto out;
			}
		} else if ((desc->type_flags & RXD_FLAG_TCPUDP_CSUM) &&
			   (desc->ip_tcp_csum & RXD_TCPCSUM_MASK)
			    >> RXD_TCPCSUM_SHIFT != 0xffff) {
			goto out;
		}

		if (opaque_key == RXD_OPAQUE_RING_STD) {
			rx_data = tpr->rx_std_buffers[desc_idx].data;
			map = dma_unmap_addr(&tpr->rx_std_buffers[desc_idx],
					     mapping);
		} else if (opaque_key == RXD_OPAQUE_RING_JUMBO) {
			rx_data = tpr->rx_jmb_buffers[desc_idx].data;
			map = dma_unmap_addr(&tpr->rx_jmb_buffers[desc_idx],
					     mapping);
		} else
			goto out;

		pci_dma_sync_single_for_cpu(tp->pdev, map, rx_len,
					    PCI_DMA_FROMDEVICE);

		rx_data += TG3_RX_OFFSET(tp);
		for (i = data_off; i < rx_len; i++, val++) {
			if (*(rx_data + i) != (u8) (val & 0xff))
				goto out;
		}
	}

	err = 0;

	/* tg3_free_rings will unmap and free the rx_data */
out:
	return err;
}

#define TG3_STD_LOOPBACK_FAILED		1
#define TG3_JMB_LOOPBACK_FAILED		2
#define TG3_TSO_LOOPBACK_FAILED		4
#define TG3_LOOPBACK_FAILED \
	(TG3_STD_LOOPBACK_FAILED | \
	 TG3_JMB_LOOPBACK_FAILED | \
	 TG3_TSO_LOOPBACK_FAILED)

static int tg3_test_loopback(struct tg3 *tp, u64 *data, bool do_extlpbk)
{
	int err = -EIO;
	u32 eee_cap;
	u32 jmb_pkt_sz = 9000;

	if (tp->dma_limit)
		jmb_pkt_sz = tp->dma_limit - ETH_HLEN;

	eee_cap = tp->phy_flags & TG3_PHYFLG_EEE_CAP;
	tp->phy_flags &= ~TG3_PHYFLG_EEE_CAP;

	if (!netif_running(tp->dev)) {
		data[TG3_MAC_LOOPB_TEST] = TG3_LOOPBACK_FAILED;
		data[TG3_PHY_LOOPB_TEST] = TG3_LOOPBACK_FAILED;
		if (do_extlpbk)
			data[TG3_EXT_LOOPB_TEST] = TG3_LOOPBACK_FAILED;
		goto done;
	}

	err = tg3_reset_hw(tp, true);
	if (err) {
		data[TG3_MAC_LOOPB_TEST] = TG3_LOOPBACK_FAILED;
		data[TG3_PHY_LOOPB_TEST] = TG3_LOOPBACK_FAILED;
		if (do_extlpbk)
			data[TG3_EXT_LOOPB_TEST] = TG3_LOOPBACK_FAILED;
		goto done;
	}

	if (tg3_flag(tp, ENABLE_RSS)) {
		int i;

		/* Reroute all rx packets to the 1st queue */
		for (i = MAC_RSS_INDIR_TBL_0;
		     i < MAC_RSS_INDIR_TBL_0 + TG3_RSS_INDIR_TBL_SIZE; i += 4)
			tw32(i, 0x0);
	}

	/* HW errata - mac loopback fails in some cases on 5780.
	 * Normal traffic and PHY loopback are not affected by
	 * errata.  Also, the MAC loopback test is deprecated for
	 * all newer ASIC revisions.
	 */
	if (tg3_asic_rev(tp) != ASIC_REV_5780 &&
	    !tg3_flag(tp, CPMU_PRESENT)) {
		tg3_mac_loopback(tp, true);

		if (tg3_run_loopback(tp, ETH_FRAME_LEN, false))
			data[TG3_MAC_LOOPB_TEST] |= TG3_STD_LOOPBACK_FAILED;

		if (tg3_flag(tp, JUMBO_RING_ENABLE) &&
		    tg3_run_loopback(tp, jmb_pkt_sz + ETH_HLEN, false))
			data[TG3_MAC_LOOPB_TEST] |= TG3_JMB_LOOPBACK_FAILED;

		tg3_mac_loopback(tp, false);
	}

	if (!(tp->phy_flags & TG3_PHYFLG_PHY_SERDES) &&
	    !tg3_flag(tp, USE_PHYLIB)) {
		int i;

		tg3_phy_lpbk_set(tp, 0, false);

		/* Wait for link */
		for (i = 0; i < 100; i++) {
			if (tr32(MAC_TX_STATUS) & TX_STATUS_LINK_UP)
				break;
			mdelay(1);
		}

		if (tg3_run_loopback(tp, ETH_FRAME_LEN, false))
			data[TG3_PHY_LOOPB_TEST] |= TG3_STD_LOOPBACK_FAILED;
		if (tg3_flag(tp, TSO_CAPABLE) &&
		    tg3_run_loopback(tp, ETH_FRAME_LEN, true))
			data[TG3_PHY_LOOPB_TEST] |= TG3_TSO_LOOPBACK_FAILED;
		if (tg3_flag(tp, JUMBO_RING_ENABLE) &&
		    tg3_run_loopback(tp, jmb_pkt_sz + ETH_HLEN, false))
			data[TG3_PHY_LOOPB_TEST] |= TG3_JMB_LOOPBACK_FAILED;

		if (do_extlpbk) {
			tg3_phy_lpbk_set(tp, 0, true);

			/* All link indications report up, but the hardware
			 * isn't really ready for about 20 msec.  Double it
			 * to be sure.
			 */
			mdelay(40);

			if (tg3_run_loopback(tp, ETH_FRAME_LEN, false))
				data[TG3_EXT_LOOPB_TEST] |=
							TG3_STD_LOOPBACK_FAILED;
			if (tg3_flag(tp, TSO_CAPABLE) &&
			    tg3_run_loopback(tp, ETH_FRAME_LEN, true))
				data[TG3_EXT_LOOPB_TEST] |=
							TG3_TSO_LOOPBACK_FAILED;
			if (tg3_flag(tp, JUMBO_RING_ENABLE) &&
			    tg3_run_loopback(tp, jmb_pkt_sz + ETH_HLEN, false))
				data[TG3_EXT_LOOPB_TEST] |=
							TG3_JMB_LOOPBACK_FAILED;
		}

		/* Re-enable gphy autopowerdown. */
		if (tp->phy_flags & TG3_PHYFLG_ENABLE_APD)
			tg3_phy_toggle_apd(tp, true);
	}

	err = (data[TG3_MAC_LOOPB_TEST] | data[TG3_PHY_LOOPB_TEST] |
	       data[TG3_EXT_LOOPB_TEST]) ? -EIO : 0;

done:
	tp->phy_flags |= eee_cap;

	return err;
}

static void tg3_self_test(struct net_device *dev, struct ethtool_test *etest,
			  u64 *data)
{
	struct tg3 *tp = netdev_priv(dev);
	bool doextlpbk = etest->flags & ETH_TEST_FL_EXTERNAL_LB;

	if ((tp->phy_flags & TG3_PHYFLG_IS_LOW_POWER) &&
	    tg3_power_up(tp)) {
		etest->flags |= ETH_TEST_FL_FAILED;
		memset(data, 1, sizeof(u64) * TG3_NUM_TEST);
		return;
	}

	memset(data, 0, sizeof(u64) * TG3_NUM_TEST);

	if (tg3_test_nvram(tp) != 0) {
		etest->flags |= ETH_TEST_FL_FAILED;
		data[TG3_NVRAM_TEST] = 1;
	}
	if (!doextlpbk && tg3_test_link(tp)) {
		etest->flags |= ETH_TEST_FL_FAILED;
		data[TG3_LINK_TEST] = 1;
	}
	if (etest->flags & ETH_TEST_FL_OFFLINE) {
		int err, err2 = 0, irq_sync = 0;

		if (netif_running(dev)) {
			tg3_phy_stop(tp);
			tg3_netif_stop(tp);
			irq_sync = 1;
		}

		tg3_full_lock(tp, irq_sync);
		tg3_halt(tp, RESET_KIND_SUSPEND, 1);
		err = tg3_nvram_lock(tp);
		tg3_halt_cpu(tp, RX_CPU_BASE);
		if (!tg3_flag(tp, 5705_PLUS))
			tg3_halt_cpu(tp, TX_CPU_BASE);
		if (!err)
			tg3_nvram_unlock(tp);

		if (tp->phy_flags & TG3_PHYFLG_MII_SERDES)
			tg3_phy_reset(tp);

		if (tg3_test_registers(tp) != 0) {
			etest->flags |= ETH_TEST_FL_FAILED;
			data[TG3_REGISTER_TEST] = 1;
		}

		if (tg3_test_memory(tp) != 0) {
			etest->flags |= ETH_TEST_FL_FAILED;
			data[TG3_MEMORY_TEST] = 1;
		}

		if (doextlpbk)
			etest->flags |= ETH_TEST_FL_EXTERNAL_LB_DONE;

		if (tg3_test_loopback(tp, data, doextlpbk))
			etest->flags |= ETH_TEST_FL_FAILED;

		tg3_full_unlock(tp);

		if (tg3_test_interrupt(tp) != 0) {
			etest->flags |= ETH_TEST_FL_FAILED;
			data[TG3_INTERRUPT_TEST] = 1;
		}

		tg3_full_lock(tp, 0);

		tg3_halt(tp, RESET_KIND_SHUTDOWN, 1);
		if (netif_running(dev)) {
			tg3_flag_set(tp, INIT_COMPLETE);
			err2 = tg3_restart_hw(tp, true);
			if (!err2)
				tg3_netif_start(tp);
		}

		tg3_full_unlock(tp);

		if (irq_sync && !err2)
			tg3_phy_start(tp);
	}
	if (tp->phy_flags & TG3_PHYFLG_IS_LOW_POWER)
		tg3_power_down(tp);

}

static int tg3_hwtstamp_ioctl(struct net_device *dev,
			      struct ifreq *ifr, int cmd)
{
	struct tg3 *tp = netdev_priv(dev);
	struct hwtstamp_config stmpconf;

	if (!tg3_flag(tp, PTP_CAPABLE))
		return -EINVAL;

	if (copy_from_user(&stmpconf, ifr->ifr_data, sizeof(stmpconf)))
		return -EFAULT;

	if (stmpconf.flags)
		return -EINVAL;

	switch (stmpconf.tx_type) {
	case HWTSTAMP_TX_ON:
		tg3_flag_set(tp, TX_TSTAMP_EN);
		break;
	case HWTSTAMP_TX_OFF:
		tg3_flag_clear(tp, TX_TSTAMP_EN);
		break;
	default:
		return -ERANGE;
	}

	switch (stmpconf.rx_filter) {
	case HWTSTAMP_FILTER_NONE:
		tp->rxptpctl = 0;
		break;
	case HWTSTAMP_FILTER_PTP_V1_L4_EVENT:
		tp->rxptpctl = TG3_RX_PTP_CTL_RX_PTP_V1_EN |
			       TG3_RX_PTP_CTL_ALL_V1_EVENTS;
		break;
	case HWTSTAMP_FILTER_PTP_V1_L4_SYNC:
		tp->rxptpctl = TG3_RX_PTP_CTL_RX_PTP_V1_EN |
			       TG3_RX_PTP_CTL_SYNC_EVNT;
		break;
	case HWTSTAMP_FILTER_PTP_V1_L4_DELAY_REQ:
		tp->rxptpctl = TG3_RX_PTP_CTL_RX_PTP_V1_EN |
			       TG3_RX_PTP_CTL_DELAY_REQ;
		break;
	case HWTSTAMP_FILTER_PTP_V2_EVENT:
		tp->rxptpctl = TG3_RX_PTP_CTL_RX_PTP_V2_EN |
			       TG3_RX_PTP_CTL_ALL_V2_EVENTS;
		break;
	case HWTSTAMP_FILTER_PTP_V2_L2_EVENT:
		tp->rxptpctl = TG3_RX_PTP_CTL_RX_PTP_V2_L2_EN |
			       TG3_RX_PTP_CTL_ALL_V2_EVENTS;
		break;
	case HWTSTAMP_FILTER_PTP_V2_L4_EVENT:
		tp->rxptpctl = TG3_RX_PTP_CTL_RX_PTP_V2_L4_EN |
			       TG3_RX_PTP_CTL_ALL_V2_EVENTS;
		break;
	case HWTSTAMP_FILTER_PTP_V2_SYNC:
		tp->rxptpctl = TG3_RX_PTP_CTL_RX_PTP_V2_EN |
			       TG3_RX_PTP_CTL_SYNC_EVNT;
		break;
	case HWTSTAMP_FILTER_PTP_V2_L2_SYNC:
		tp->rxptpctl = TG3_RX_PTP_CTL_RX_PTP_V2_L2_EN |
			       TG3_RX_PTP_CTL_SYNC_EVNT;
		break;
	case HWTSTAMP_FILTER_PTP_V2_L4_SYNC:
		tp->rxptpctl = TG3_RX_PTP_CTL_RX_PTP_V2_L4_EN |
			       TG3_RX_PTP_CTL_SYNC_EVNT;
		break;
	case HWTSTAMP_FILTER_PTP_V2_DELAY_REQ:
		tp->rxptpctl = TG3_RX_PTP_CTL_RX_PTP_V2_EN |
			       TG3_RX_PTP_CTL_DELAY_REQ;
		break;
	case HWTSTAMP_FILTER_PTP_V2_L2_DELAY_REQ:
		tp->rxptpctl = TG3_RX_PTP_CTL_RX_PTP_V2_L2_EN |
			       TG3_RX_PTP_CTL_DELAY_REQ;
		break;
	case HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ:
		tp->rxptpctl = TG3_RX_PTP_CTL_RX_PTP_V2_L4_EN |
			       TG3_RX_PTP_CTL_DELAY_REQ;
		break;
	default:
		return -ERANGE;
	}

	if (netif_running(dev) && tp->rxptpctl)
		tw32(TG3_RX_PTP_CTL,
		     tp->rxptpctl | TG3_RX_PTP_CTL_HWTS_INTERLOCK);

	return copy_to_user(ifr->ifr_data, &stmpconf, sizeof(stmpconf)) ?
		-EFAULT : 0;
}

static int tg3_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	struct mii_ioctl_data *data = if_mii(ifr);
	struct tg3 *tp = netdev_priv(dev);
	int err;

	if (tg3_flag(tp, USE_PHYLIB)) {
		struct phy_device *phydev;
		if (!(tp->phy_flags & TG3_PHYFLG_IS_CONNECTED))
			return -EAGAIN;
		phydev = tp->mdio_bus->phy_map[TG3_PHY_MII_ADDR];
		return phy_mii_ioctl(phydev, ifr, cmd);
	}

	switch (cmd) {
	case SIOCGMIIPHY:
		data->phy_id = tp->phy_addr;

		/* fallthru */
	case SIOCGMIIREG: {
		u32 mii_regval;

		if (tp->phy_flags & TG3_PHYFLG_PHY_SERDES)
			break;			/* We have no PHY */

		if (!netif_running(dev))
			return -EAGAIN;

		spin_lock_bh(&tp->lock);
		err = __tg3_readphy(tp, data->phy_id & 0x1f,
				    data->reg_num & 0x1f, &mii_regval);
		spin_unlock_bh(&tp->lock);

		data->val_out = mii_regval;

		return err;
	}

	case SIOCSMIIREG:
		if (tp->phy_flags & TG3_PHYFLG_PHY_SERDES)
			break;			/* We have no PHY */

		if (!netif_running(dev))
			return -EAGAIN;

		spin_lock_bh(&tp->lock);
		err = __tg3_writephy(tp, data->phy_id & 0x1f,
				     data->reg_num & 0x1f, data->val_in);
		spin_unlock_bh(&tp->lock);

		return err;

	case SIOCSHWTSTAMP:
		return tg3_hwtstamp_ioctl(dev, ifr, cmd);

	default:
		/* do nothing */
		break;
	}
	return -EOPNOTSUPP;
}

static int tg3_get_coalesce(struct net_device *dev, struct ethtool_coalesce *ec)
{
	struct tg3 *tp = netdev_priv(dev);

	memcpy(ec, &tp->coal, sizeof(*ec));
	return 0;
}

static int tg3_set_coalesce(struct net_device *dev, struct ethtool_coalesce *ec)
{
	struct tg3 *tp = netdev_priv(dev);
	u32 max_rxcoal_tick_int = 0, max_txcoal_tick_int = 0;
	u32 max_stat_coal_ticks = 0, min_stat_coal_ticks = 0;

	if (!tg3_flag(tp, 5705_PLUS)) {
		max_rxcoal_tick_int = MAX_RXCOAL_TICK_INT;
		max_txcoal_tick_int = MAX_TXCOAL_TICK_INT;
		max_stat_coal_ticks = MAX_STAT_COAL_TICKS;
		min_stat_coal_ticks = MIN_STAT_COAL_TICKS;
	}

	if ((ec->rx_coalesce_usecs > MAX_RXCOL_TICKS) ||
	    (ec->tx_coalesce_usecs > MAX_TXCOL_TICKS) ||
	    (ec->rx_max_coalesced_frames > MAX_RXMAX_FRAMES) ||
	    (ec->tx_max_coalesced_frames > MAX_TXMAX_FRAMES) ||
	    (ec->rx_coalesce_usecs_irq > max_rxcoal_tick_int) ||
	    (ec->tx_coalesce_usecs_irq > max_txcoal_tick_int) ||
	    (ec->rx_max_coalesced_frames_irq > MAX_RXCOAL_MAXF_INT) ||
	    (ec->tx_max_coalesced_frames_irq > MAX_TXCOAL_MAXF_INT) ||
	    (ec->stats_block_coalesce_usecs > max_stat_coal_ticks) ||
	    (ec->stats_block_coalesce_usecs < min_stat_coal_ticks))
		return -EINVAL;

	/* No rx interrupts will be generated if both are zero */
	if ((ec->rx_coalesce_usecs == 0) &&
	    (ec->rx_max_coalesced_frames == 0))
		return -EINVAL;

	/* No tx interrupts will be generated if both are zero */
	if ((ec->tx_coalesce_usecs == 0) &&
	    (ec->tx_max_coalesced_frames == 0))
		return -EINVAL;

	/* Only copy relevant parameters, ignore all others. */
	tp->coal.rx_coalesce_usecs = ec->rx_coalesce_usecs;
	tp->coal.tx_coalesce_usecs = ec->tx_coalesce_usecs;
	tp->coal.rx_max_coalesced_frames = ec->rx_max_coalesced_frames;
	tp->coal.tx_max_coalesced_frames = ec->tx_max_coalesced_frames;
	tp->coal.rx_coalesce_usecs_irq = ec->rx_coalesce_usecs_irq;
	tp->coal.tx_coalesce_usecs_irq = ec->tx_coalesce_usecs_irq;
	tp->coal.rx_max_coalesced_frames_irq = ec->rx_max_coalesced_frames_irq;
	tp->coal.tx_max_coalesced_frames_irq = ec->tx_max_coalesced_frames_irq;
	tp->coal.stats_block_coalesce_usecs = ec->stats_block_coalesce_usecs;

	if (netif_running(dev)) {
		tg3_full_lock(tp, 0);
		__tg3_set_coalesce(tp, &tp->coal);
		tg3_full_unlock(tp);
	}
	return 0;
}

static const struct ethtool_ops tg3_ethtool_ops = {
	.get_settings		= tg3_get_settings,
	.set_settings		= tg3_set_settings,
	.get_drvinfo		= tg3_get_drvinfo,
	.get_regs_len		= tg3_get_regs_len,
	.get_regs		= tg3_get_regs,
	.get_wol		= tg3_get_wol,
	.set_wol		= tg3_set_wol,
	.get_msglevel		= tg3_get_msglevel,
	.set_msglevel		= tg3_set_msglevel,
	.nway_reset		= tg3_nway_reset,
	.get_link		= ethtool_op_get_link,
	.get_eeprom_len		= tg3_get_eeprom_len,
	.get_eeprom		= tg3_get_eeprom,
	.set_eeprom		= tg3_set_eeprom,
	.get_ringparam		= tg3_get_ringparam,
	.set_ringparam		= tg3_set_ringparam,
	.get_pauseparam		= tg3_get_pauseparam,
	.set_pauseparam		= tg3_set_pauseparam,
	.self_test		= tg3_self_test,
	.get_strings		= tg3_get_strings,
	.set_phys_id		= tg3_set_phys_id,
	.get_ethtool_stats	= tg3_get_ethtool_stats,
	.get_coalesce		= tg3_get_coalesce,
	.set_coalesce		= tg3_set_coalesce,
	.get_sset_count		= tg3_get_sset_count,
	.get_rxnfc		= tg3_get_rxnfc,
	.get_rxfh_indir_size    = tg3_get_rxfh_indir_size,
	.get_rxfh_indir		= tg3_get_rxfh_indir,
	.set_rxfh_indir		= tg3_set_rxfh_indir,
	.get_channels		= tg3_get_channels,
	.set_channels		= tg3_set_channels,
	.get_ts_info		= tg3_get_ts_info,
};

static struct rtnl_link_stats64 *tg3_get_stats64(struct net_device *dev,
						struct rtnl_link_stats64 *stats)
{
	struct tg3 *tp = netdev_priv(dev);

	spin_lock_bh(&tp->lock);
	if (!tp->hw_stats) {
		spin_unlock_bh(&tp->lock);
		return &tp->net_stats_prev;
	}

	tg3_get_nstats(tp, stats);
	spin_unlock_bh(&tp->lock);

	return stats;
}

static void tg3_set_rx_mode(struct net_device *dev)
{
	struct tg3 *tp = netdev_priv(dev);

	if (!netif_running(dev))
		return;

	tg3_full_lock(tp, 0);
	__tg3_set_rx_mode(dev);
	tg3_full_unlock(tp);
}

static inline void tg3_set_mtu(struct net_device *dev, struct tg3 *tp,
			       int new_mtu)
{
	dev->mtu = new_mtu;

	if (new_mtu > ETH_DATA_LEN) {
		if (tg3_flag(tp, 5780_CLASS)) {
			netdev_update_features(dev);
			tg3_flag_clear(tp, TSO_CAPABLE);
		} else {
			tg3_flag_set(tp, JUMBO_RING_ENABLE);
		}
	} else {
		if (tg3_flag(tp, 5780_CLASS)) {
			tg3_flag_set(tp, TSO_CAPABLE);
			netdev_update_features(dev);
		}
		tg3_flag_clear(tp, JUMBO_RING_ENABLE);
	}
}

static int tg3_change_mtu(struct net_device *dev, int new_mtu)
{
	struct tg3 *tp = netdev_priv(dev);
	int err;
	bool reset_phy = false;

	if (new_mtu < TG3_MIN_MTU || new_mtu > TG3_MAX_MTU(tp))
		return -EINVAL;

	if (!netif_running(dev)) {
		/* We'll just catch it later when the
		 * device is up'd.
		 */
		tg3_set_mtu(dev, tp, new_mtu);
		return 0;
	}

	tg3_phy_stop(tp);

	tg3_netif_stop(tp);

	tg3_set_mtu(dev, tp, new_mtu);

	tg3_full_lock(tp, 1);

	tg3_halt(tp, RESET_KIND_SHUTDOWN, 1);

	/* Reset PHY, otherwise the read DMA engine will be in a mode that
	 * breaks all requests to 256 bytes.
	 */
	if (tg3_asic_rev(tp) == ASIC_REV_57766)
		reset_phy = true;

	err = tg3_restart_hw(tp, reset_phy);

	if (!err)
		tg3_netif_start(tp);

	tg3_full_unlock(tp);

	if (!err)
		tg3_phy_start(tp);

	return err;
}

static const struct net_device_ops tg3_netdev_ops = {
	.ndo_open		= tg3_open,
	.ndo_stop		= tg3_close,
	.ndo_start_xmit		= tg3_start_xmit,
	.ndo_get_stats64	= tg3_get_stats64,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_rx_mode	= tg3_set_rx_mode,
	.ndo_set_mac_address	= tg3_set_mac_addr,
	.ndo_do_ioctl		= tg3_ioctl,
	.ndo_tx_timeout		= tg3_tx_timeout,
	.ndo_change_mtu		= tg3_change_mtu,
	.ndo_fix_features	= tg3_fix_features,
	.ndo_set_features	= tg3_set_features,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= tg3_poll_controller,
#endif
};

static void tg3_get_eeprom_size(struct tg3 *tp)
{
	u32 cursize, val, magic;

	tp->nvram_size = EEPROM_CHIP_SIZE;

	if (tg3_nvram_read(tp, 0, &magic) != 0)
		return;

	if ((magic != TG3_EEPROM_MAGIC) &&
	    ((magic & TG3_EEPROM_MAGIC_FW_MSK) != TG3_EEPROM_MAGIC_FW) &&
	    ((magic & TG3_EEPROM_MAGIC_HW_MSK) != TG3_EEPROM_MAGIC_HW))
		return;

	/*
	 * Size the chip by reading offsets at increasing powers of two.
	 * When we encounter our validation signature, we know the addressing
	 * has wrapped around, and thus have our chip size.
	 */
	cursize = 0x10;

	while (cursize < tp->nvram_size) {
		if (tg3_nvram_read(tp, cursize, &val) != 0)
			return;

		if (val == magic)
			break;

		cursize <<= 1;
	}

	tp->nvram_size = cursize;
}

static void tg3_get_nvram_size(struct tg3 *tp)
{
	u32 val;

	if (tg3_flag(tp, NO_NVRAM) || tg3_nvram_read(tp, 0, &val) != 0)
		return;

	/* Selfboot format */
	if (val != TG3_EEPROM_MAGIC) {
		tg3_get_eeprom_size(tp);
		return;
	}

	if (tg3_nvram_read(tp, 0xf0, &val) == 0) {
		if (val != 0) {
			/* This is confusing.  We want to operate on the
			 * 16-bit value at offset 0xf2.  The tg3_nvram_read()
			 * call will read from NVRAM and byteswap the data
			 * according to the byteswapping settings for all
			 * other register accesses.  This ensures the data we
			 * want will always reside in the lower 16-bits.
			 * However, the data in NVRAM is in LE format, which
			 * means the data from the NVRAM read will always be
			 * opposite the endianness of the CPU.  The 16-bit
			 * byteswap then brings the data to CPU endianness.
			 */
			tp->nvram_size = swab16((u16)(val & 0x0000ffff)) * 1024;
			return;
		}
	}
	tp->nvram_size = TG3_NVRAM_SIZE_512KB;
}

static void tg3_get_nvram_info(struct tg3 *tp)
{
	u32 nvcfg1;

	nvcfg1 = tr32(NVRAM_CFG1);
	if (nvcfg1 & NVRAM_CFG1_FLASHIF_ENAB) {
		tg3_flag_set(tp, FLASH);
	} else {
		nvcfg1 &= ~NVRAM_CFG1_COMPAT_BYPASS;
		tw32(NVRAM_CFG1, nvcfg1);
	}

	if (tg3_asic_rev(tp) == ASIC_REV_5750 ||
	    tg3_flag(tp, 5780_CLASS)) {
		switch (nvcfg1 & NVRAM_CFG1_VENDOR_MASK) {
		case FLASH_VENDOR_ATMEL_FLASH_BUFFERED:
			tp->nvram_jedecnum = JEDEC_ATMEL;
			tp->nvram_pagesize = ATMEL_AT45DB0X1B_PAGE_SIZE;
			tg3_flag_set(tp, NVRAM_BUFFERED);
			break;
		case FLASH_VENDOR_ATMEL_FLASH_UNBUFFERED:
			tp->nvram_jedecnum = JEDEC_ATMEL;
			tp->nvram_pagesize = ATMEL_AT25F512_PAGE_SIZE;
			break;
		case FLASH_VENDOR_ATMEL_EEPROM:
			tp->nvram_jedecnum = JEDEC_ATMEL;
			tp->nvram_pagesize = ATMEL_AT24C512_CHIP_SIZE;
			tg3_flag_set(tp, NVRAM_BUFFERED);
			break;
		case FLASH_VENDOR_ST:
			tp->nvram_jedecnum = JEDEC_ST;
			tp->nvram_pagesize = ST_M45PEX0_PAGE_SIZE;
			tg3_flag_set(tp, NVRAM_BUFFERED);
			break;
		case FLASH_VENDOR_SAIFUN:
			tp->nvram_jedecnum = JEDEC_SAIFUN;
			tp->nvram_pagesize = SAIFUN_SA25F0XX_PAGE_SIZE;
			break;
		case FLASH_VENDOR_SST_SMALL:
		case FLASH_VENDOR_SST_LARGE:
			tp->nvram_jedecnum = JEDEC_SST;
			tp->nvram_pagesize = SST_25VF0X0_PAGE_SIZE;
			break;
		}
	} else {
		tp->nvram_jedecnum = JEDEC_ATMEL;
		tp->nvram_pagesize = ATMEL_AT45DB0X1B_PAGE_SIZE;
		tg3_flag_set(tp, NVRAM_BUFFERED);
	}
}

static void tg3_nvram_get_pagesize(struct tg3 *tp, u32 nvmcfg1)
{
	switch (nvmcfg1 & NVRAM_CFG1_5752PAGE_SIZE_MASK) {
	case FLASH_5752PAGE_SIZE_256:
		tp->nvram_pagesize = 256;
		break;
	case FLASH_5752PAGE_SIZE_512:
		tp->nvram_pagesize = 512;
		break;
	case FLASH_5752PAGE_SIZE_1K:
		tp->nvram_pagesize = 1024;
		break;
	case FLASH_5752PAGE_SIZE_2K:
		tp->nvram_pagesize = 2048;
		break;
	case FLASH_5752PAGE_SIZE_4K:
		tp->nvram_pagesize = 4096;
		break;
	case FLASH_5752PAGE_SIZE_264:
		tp->nvram_pagesize = 264;
		break;
	case FLASH_5752PAGE_SIZE_528:
		tp->nvram_pagesize = 528;
		break;
	}
}

static void tg3_get_5752_nvram_info(struct tg3 *tp)
{
	u32 nvcfg1;

	nvcfg1 = tr32(NVRAM_CFG1);

	/* NVRAM protection for TPM */
	if (nvcfg1 & (1 << 27))
		tg3_flag_set(tp, PROTECTED_NVRAM);

	switch (nvcfg1 & NVRAM_CFG1_5752VENDOR_MASK) {
	case FLASH_5752VENDOR_ATMEL_EEPROM_64KHZ:
	case FLASH_5752VENDOR_ATMEL_EEPROM_376KHZ:
		tp->nvram_jedecnum = JEDEC_ATMEL;
		tg3_flag_set(tp, NVRAM_BUFFERED);
		break;
	case FLASH_5752VENDOR_ATMEL_FLASH_BUFFERED:
		tp->nvram_jedecnum = JEDEC_ATMEL;
		tg3_flag_set(tp, NVRAM_BUFFERED);
		tg3_flag_set(tp, FLASH);
		break;
	case FLASH_5752VENDOR_ST_M45PE10:
	case FLASH_5752VENDOR_ST_M45PE20:
	case FLASH_5752VENDOR_ST_M45PE40:
		tp->nvram_jedecnum = JEDEC_ST;
		tg3_flag_set(tp, NVRAM_BUFFERED);
		tg3_flag_set(tp, FLASH);
		break;
	}

	if (tg3_flag(tp, FLASH)) {
		tg3_nvram_get_pagesize(tp, nvcfg1);
	} else {
		/* For eeprom, set pagesize to maximum eeprom size */
		tp->nvram_pagesize = ATMEL_AT24C512_CHIP_SIZE;

		nvcfg1 &= ~NVRAM_CFG1_COMPAT_BYPASS;
		tw32(NVRAM_CFG1, nvcfg1);
	}
}

static void tg3_get_5755_nvram_info(struct tg3 *tp)
{
	u32 nvcfg1, protect = 0;

	nvcfg1 = tr32(NVRAM_CFG1);

	/* NVRAM protection for TPM */
	if (nvcfg1 & (1 << 27)) {
		tg3_flag_set(tp, PROTECTED_NVRAM);
		protect = 1;
	}

	nvcfg1 &= NVRAM_CFG1_5752VENDOR_MASK;
	switch (nvcfg1) {
	case FLASH_5755VENDOR_ATMEL_FLASH_1:
	case FLASH_5755VENDOR_ATMEL_FLASH_2:
	case FLASH_5755VENDOR_ATMEL_FLASH_3:
	case FLASH_5755VENDOR_ATMEL_FLASH_5:
		tp->nvram_jedecnum = JEDEC_ATMEL;
		tg3_flag_set(tp, NVRAM_BUFFERED);
		tg3_flag_set(tp, FLASH);
		tp->nvram_pagesize = 264;
		if (nvcfg1 == FLASH_5755VENDOR_ATMEL_FLASH_1 ||
		    nvcfg1 == FLASH_5755VENDOR_ATMEL_FLASH_5)
			tp->nvram_size = (protect ? 0x3e200 :
					  TG3_NVRAM_SIZE_512KB);
		else if (nvcfg1 == FLASH_5755VENDOR_ATMEL_FLASH_2)
			tp->nvram_size = (protect ? 0x1f200 :
					  TG3_NVRAM_SIZE_256KB);
		else
			tp->nvram_size = (protect ? 0x1f200 :
					  TG3_NVRAM_SIZE_128KB);
		break;
	case FLASH_5752VENDOR_ST_M45PE10:
	case FLASH_5752VENDOR_ST_M45PE20:
	case FLASH_5752VENDOR_ST_M45PE40:
		tp->nvram_jedecnum = JEDEC_ST;
		tg3_flag_set(tp, NVRAM_BUFFERED);
		tg3_flag_set(tp, FLASH);
		tp->nvram_pagesize = 256;
		if (nvcfg1 == FLASH_5752VENDOR_ST_M45PE10)
			tp->nvram_size = (protect ?
					  TG3_NVRAM_SIZE_64KB :
					  TG3_NVRAM_SIZE_128KB);
		else if (nvcfg1 == FLASH_5752VENDOR_ST_M45PE20)
			tp->nvram_size = (protect ?
					  TG3_NVRAM_SIZE_64KB :
					  TG3_NVRAM_SIZE_256KB);
		else
			tp->nvram_size = (protect ?
					  TG3_NVRAM_SIZE_128KB :
					  TG3_NVRAM_SIZE_512KB);
		break;
	}
}

static void tg3_get_5787_nvram_info(struct tg3 *tp)
{
	u32 nvcfg1;

	nvcfg1 = tr32(NVRAM_CFG1);

	switch (nvcfg1 & NVRAM_CFG1_5752VENDOR_MASK) {
	case FLASH_5787VENDOR_ATMEL_EEPROM_64KHZ:
	case FLASH_5787VENDOR_ATMEL_EEPROM_376KHZ:
	case FLASH_5787VENDOR_MICRO_EEPROM_64KHZ:
	case FLASH_5787VENDOR_MICRO_EEPROM_376KHZ:
		tp->nvram_jedecnum = JEDEC_ATMEL;
		tg3_flag_set(tp, NVRAM_BUFFERED);
		tp->nvram_pagesize = ATMEL_AT24C512_CHIP_SIZE;

		nvcfg1 &= ~NVRAM_CFG1_COMPAT_BYPASS;
		tw32(NVRAM_CFG1, nvcfg1);
		break;
	case FLASH_5752VENDOR_ATMEL_FLASH_BUFFERED:
	case FLASH_5755VENDOR_ATMEL_FLASH_1:
	case FLASH_5755VENDOR_ATMEL_FLASH_2:
	case FLASH_5755VENDOR_ATMEL_FLASH_3:
		tp->nvram_jedecnum = JEDEC_ATMEL;
		tg3_flag_set(tp, NVRAM_BUFFERED);
		tg3_flag_set(tp, FLASH);
		tp->nvram_pagesize = 264;
		break;
	case FLASH_5752VENDOR_ST_M45PE10:
	case FLASH_5752VENDOR_ST_M45PE20:
	case FLASH_5752VENDOR_ST_M45PE40:
		tp->nvram_jedecnum = JEDEC_ST;
		tg3_flag_set(tp, NVRAM_BUFFERED);
		tg3_flag_set(tp, FLASH);
		tp->nvram_pagesize = 256;
		break;
	}
}

static void tg3_get_5761_nvram_info(struct tg3 *tp)
{
	u32 nvcfg1, protect = 0;

	nvcfg1 = tr32(NVRAM_CFG1);

	/* NVRAM protection for TPM */
	if (nvcfg1 & (1 << 27)) {
		tg3_flag_set(tp, PROTECTED_NVRAM);
		protect = 1;
	}

	nvcfg1 &= NVRAM_CFG1_5752VENDOR_MASK;
	switch (nvcfg1) {
	case FLASH_5761VENDOR_ATMEL_ADB021D:
	case FLASH_5761VENDOR_ATMEL_ADB041D:
	case FLASH_5761VENDOR_ATMEL_ADB081D:
	case FLASH_5761VENDOR_ATMEL_ADB161D:
	case FLASH_5761VENDOR_ATMEL_MDB021D:
	case FLASH_5761VENDOR_ATMEL_MDB041D:
	case FLASH_5761VENDOR_ATMEL_MDB081D:
	case FLASH_5761VENDOR_ATMEL_MDB161D:
		tp->nvram_jedecnum = JEDEC_ATMEL;
		tg3_flag_set(tp, NVRAM_BUFFERED);
		tg3_flag_set(tp, FLASH);
		tg3_flag_set(tp, NO_NVRAM_ADDR_TRANS);
		tp->nvram_pagesize = 256;
		break;
	case FLASH_5761VENDOR_ST_A_M45PE20:
	case FLASH_5761VENDOR_ST_A_M45PE40:
	case FLASH_5761VENDOR_ST_A_M45PE80:
	case FLASH_5761VENDOR_ST_A_M45PE16:
	case FLASH_5761VENDOR_ST_M_M45PE20:
	case FLASH_5761VENDOR_ST_M_M45PE40:
	case FLASH_5761VENDOR_ST_M_M45PE80:
	case FLASH_5761VENDOR_ST_M_M45PE16:
		tp->nvram_jedecnum = JEDEC_ST;
		tg3_flag_set(tp, NVRAM_BUFFERED);
		tg3_flag_set(tp, FLASH);
		tp->nvram_pagesize = 256;
		break;
	}

	if (protect) {
		tp->nvram_size = tr32(NVRAM_ADDR_LOCKOUT);
	} else {
		switch (nvcfg1) {
		case FLASH_5761VENDOR_ATMEL_ADB161D:
		case FLASH_5761VENDOR_ATMEL_MDB161D:
		case FLASH_5761VENDOR_ST_A_M45PE16:
		case FLASH_5761VENDOR_ST_M_M45PE16:
			tp->nvram_size = TG3_NVRAM_SIZE_2MB;
			break;
		case FLASH_5761VENDOR_ATMEL_ADB081D:
		case FLASH_5761VENDOR_ATMEL_MDB081D:
		case FLASH_5761VENDOR_ST_A_M45PE80:
		case FLASH_5761VENDOR_ST_M_M45PE80:
			tp->nvram_size = TG3_NVRAM_SIZE_1MB;
			break;
		case FLASH_5761VENDOR_ATMEL_ADB041D:
		case FLASH_5761VENDOR_ATMEL_MDB041D:
		case FLASH_5761VENDOR_ST_A_M45PE40:
		case FLASH_5761VENDOR_ST_M_M45PE40:
			tp->nvram_size = TG3_NVRAM_SIZE_512KB;
			break;
		case FLASH_5761VENDOR_ATMEL_ADB021D:
		case FLASH_5761VENDOR_ATMEL_MDB021D:
		case FLASH_5761VENDOR_ST_A_M45PE20:
		case FLASH_5761VENDOR_ST_M_M45PE20:
			tp->nvram_size = TG3_NVRAM_SIZE_256KB;
			break;
		}
	}
}

static void tg3_get_5906_nvram_info(struct tg3 *tp)
{
	tp->nvram_jedecnum = JEDEC_ATMEL;
	tg3_flag_set(tp, NVRAM_BUFFERED);
	tp->nvram_pagesize = ATMEL_AT24C512_CHIP_SIZE;
}

static void tg3_get_57780_nvram_info(struct tg3 *tp)
{
	u32 nvcfg1;

	nvcfg1 = tr32(NVRAM_CFG1);

	switch (nvcfg1 & NVRAM_CFG1_5752VENDOR_MASK) {
	case FLASH_5787VENDOR_ATMEL_EEPROM_376KHZ:
	case FLASH_5787VENDOR_MICRO_EEPROM_376KHZ:
		tp->nvram_jedecnum = JEDEC_ATMEL;
		tg3_flag_set(tp, NVRAM_BUFFERED);
		tp->nvram_pagesize = ATMEL_AT24C512_CHIP_SIZE;

		nvcfg1 &= ~NVRAM_CFG1_COMPAT_BYPASS;
		tw32(NVRAM_CFG1, nvcfg1);
		return;
	case FLASH_5752VENDOR_ATMEL_FLASH_BUFFERED:
	case FLASH_57780VENDOR_ATMEL_AT45DB011D:
	case FLASH_57780VENDOR_ATMEL_AT45DB011B:
	case FLASH_57780VENDOR_ATMEL_AT45DB021D:
	case FLASH_57780VENDOR_ATMEL_AT45DB021B:
	case FLASH_57780VENDOR_ATMEL_AT45DB041D:
	case FLASH_57780VENDOR_ATMEL_AT45DB041B:
		tp->nvram_jedecnum = JEDEC_ATMEL;
		tg3_flag_set(tp, NVRAM_BUFFERED);
		tg3_flag_set(tp, FLASH);

		switch (nvcfg1 & NVRAM_CFG1_5752VENDOR_MASK) {
		case FLASH_5752VENDOR_ATMEL_FLASH_BUFFERED:
		case FLASH_57780VENDOR_ATMEL_AT45DB011D:
		case FLASH_57780VENDOR_ATMEL_AT45DB011B:
			tp->nvram_size = TG3_NVRAM_SIZE_128KB;
			break;
		case FLASH_57780VENDOR_ATMEL_AT45DB021D:
		case FLASH_57780VENDOR_ATMEL_AT45DB021B:
			tp->nvram_size = TG3_NVRAM_SIZE_256KB;
			break;
		case FLASH_57780VENDOR_ATMEL_AT45DB041D:
		case FLASH_57780VENDOR_ATMEL_AT45DB041B:
			tp->nvram_size = TG3_NVRAM_SIZE_512KB;
			break;
		}
		break;
	case FLASH_5752VENDOR_ST_M45PE10:
	case FLASH_5752VENDOR_ST_M45PE20:
	case FLASH_5752VENDOR_ST_M45PE40:
		tp->nvram_jedecnum = JEDEC_ST;
		tg3_flag_set(tp, NVRAM_BUFFERED);
		tg3_flag_set(tp, FLASH);

		switch (nvcfg1 & NVRAM_CFG1_5752VENDOR_MASK) {
		case FLASH_5752VENDOR_ST_M45PE10:
			tp->nvram_size = TG3_NVRAM_SIZE_128KB;
			break;
		case FLASH_5752VENDOR_ST_M45PE20:
			tp->nvram_size = TG3_NVRAM_SIZE_256KB;
			break;
		case FLASH_5752VENDOR_ST_M45PE40:
			tp->nvram_size = TG3_NVRAM_SIZE_512KB;
			break;
		}
		break;
	default:
		tg3_flag_set(tp, NO_NVRAM);
		return;
	}

	tg3_nvram_get_pagesize(tp, nvcfg1);
	if (tp->nvram_pagesize != 264 && tp->nvram_pagesize != 528)
		tg3_flag_set(tp, NO_NVRAM_ADDR_TRANS);
}


static void tg3_get_5717_nvram_info(struct tg3 *tp)
{
	u32 nvcfg1;

	nvcfg1 = tr32(NVRAM_CFG1);

	switch (nvcfg1 & NVRAM_CFG1_5752VENDOR_MASK) {
	case FLASH_5717VENDOR_ATMEL_EEPROM:
	case FLASH_5717VENDOR_MICRO_EEPROM:
		tp->nvram_jedecnum = JEDEC_ATMEL;
		tg3_flag_set(tp, NVRAM_BUFFERED);
		tp->nvram_pagesize = ATMEL_AT24C512_CHIP_SIZE;

		nvcfg1 &= ~NVRAM_CFG1_COMPAT_BYPASS;
		tw32(NVRAM_CFG1, nvcfg1);
		return;
	case FLASH_5717VENDOR_ATMEL_MDB011D:
	case FLASH_5717VENDOR_ATMEL_ADB011B:
	case FLASH_5717VENDOR_ATMEL_ADB011D:
	case FLASH_5717VENDOR_ATMEL_MDB021D:
	case FLASH_5717VENDOR_ATMEL_ADB021B:
	case FLASH_5717VENDOR_ATMEL_ADB021D:
	case FLASH_5717VENDOR_ATMEL_45USPT:
		tp->nvram_jedecnum = JEDEC_ATMEL;
		tg3_flag_set(tp, NVRAM_BUFFERED);
		tg3_flag_set(tp, FLASH);

		switch (nvcfg1 & NVRAM_CFG1_5752VENDOR_MASK) {
		case FLASH_5717VENDOR_ATMEL_MDB021D:
			/* Detect size with tg3_nvram_get_size() */
			break;
		case FLASH_5717VENDOR_ATMEL_ADB021B:
		case FLASH_5717VENDOR_ATMEL_ADB021D:
			tp->nvram_size = TG3_NVRAM_SIZE_256KB;
			break;
		default:
			tp->nvram_size = TG3_NVRAM_SIZE_128KB;
			break;
		}
		break;
	case FLASH_5717VENDOR_ST_M_M25PE10:
	case FLASH_5717VENDOR_ST_A_M25PE10:
	case FLASH_5717VENDOR_ST_M_M45PE10:
	case FLASH_5717VENDOR_ST_A_M45PE10:
	case FLASH_5717VENDOR_ST_M_M25PE20:
	case FLASH_5717VENDOR_ST_A_M25PE20:
	case FLASH_5717VENDOR_ST_M_M45PE20:
	case FLASH_5717VENDOR_ST_A_M45PE20:
	case FLASH_5717VENDOR_ST_25USPT:
	case FLASH_5717VENDOR_ST_45USPT:
		tp->nvram_jedecnum = JEDEC_ST;
		tg3_flag_set(tp, NVRAM_BUFFERED);
		tg3_flag_set(tp, FLASH);

		switch (nvcfg1 & NVRAM_CFG1_5752VENDOR_MASK) {
		case FLASH_5717VENDOR_ST_M_M25PE20:
		case FLASH_5717VENDOR_ST_M_M45PE20:
			/* Detect size with tg3_nvram_get_size() */
			break;
		case FLASH_5717VENDOR_ST_A_M25PE20:
		case FLASH_5717VENDOR_ST_A_M45PE20:
			tp->nvram_size = TG3_NVRAM_SIZE_256KB;
			break;
		default:
			tp->nvram_size = TG3_NVRAM_SIZE_128KB;
			break;
		}
		break;
	default:
		tg3_flag_set(tp, NO_NVRAM);
		return;
	}

	tg3_nvram_get_pagesize(tp, nvcfg1);
	if (tp->nvram_pagesize != 264 && tp->nvram_pagesize != 528)
		tg3_flag_set(tp, NO_NVRAM_ADDR_TRANS);
}

static void tg3_get_5720_nvram_info(struct tg3 *tp)
{
	u32 nvcfg1, nvmpinstrp;

	nvcfg1 = tr32(NVRAM_CFG1);
	nvmpinstrp = nvcfg1 & NVRAM_CFG1_5752VENDOR_MASK;

	if (tg3_asic_rev(tp) == ASIC_REV_5762) {
		if (!(nvcfg1 & NVRAM_CFG1_5762VENDOR_MASK)) {
			tg3_flag_set(tp, NO_NVRAM);
			return;
		}

		switch (nvmpinstrp) {
		case FLASH_5762_EEPROM_HD:
			nvmpinstrp = FLASH_5720_EEPROM_HD;
			break;
		case FLASH_5762_EEPROM_LD:
			nvmpinstrp = FLASH_5720_EEPROM_LD;
			break;
		case FLASH_5720VENDOR_M_ST_M45PE20:
			/* This pinstrap supports multiple sizes, so force it
			 * to read the actual size from location 0xf0.
			 */
			nvmpinstrp = FLASH_5720VENDOR_ST_45USPT;
			break;
		}
	}

	switch (nvmpinstrp) {
	case FLASH_5720_EEPROM_HD:
	case FLASH_5720_EEPROM_LD:
		tp->nvram_jedecnum = JEDEC_ATMEL;
		tg3_flag_set(tp, NVRAM_BUFFERED);

		nvcfg1 &= ~NVRAM_CFG1_COMPAT_BYPASS;
		tw32(NVRAM_CFG1, nvcfg1);
		if (nvmpinstrp == FLASH_5720_EEPROM_HD)
			tp->nvram_pagesize = ATMEL_AT24C512_CHIP_SIZE;
		else
			tp->nvram_pagesize = ATMEL_AT24C02_CHIP_SIZE;
		return;
	case FLASH_5720VENDOR_M_ATMEL_DB011D:
	case FLASH_5720VENDOR_A_ATMEL_DB011B:
	case FLASH_5720VENDOR_A_ATMEL_DB011D:
	case FLASH_5720VENDOR_M_ATMEL_DB021D:
	case FLASH_5720VENDOR_A_ATMEL_DB021B:
	case FLASH_5720VENDOR_A_ATMEL_DB021D:
	case FLASH_5720VENDOR_M_ATMEL_DB041D:
	case FLASH_5720VENDOR_A_ATMEL_DB041B:
	case FLASH_5720VENDOR_A_ATMEL_DB041D:
	case FLASH_5720VENDOR_M_ATMEL_DB081D:
	case FLASH_5720VENDOR_A_ATMEL_DB081D:
	case FLASH_5720VENDOR_ATMEL_45USPT:
		tp->nvram_jedecnum = JEDEC_ATMEL;
		tg3_flag_set(tp, NVRAM_BUFFERED);
		tg3_flag_set(tp, FLASH);

		switch (nvmpinstrp) {
		case FLASH_5720VENDOR_M_ATMEL_DB021D:
		case FLASH_5720VENDOR_A_ATMEL_DB021B:
		case FLASH_5720VENDOR_A_ATMEL_DB021D:
			tp->nvram_size = TG3_NVRAM_SIZE_256KB;
			break;
		case FLASH_5720VENDOR_M_ATMEL_DB041D:
		case FLASH_5720VENDOR_A_ATMEL_DB041B:
		case FLASH_5720VENDOR_A_ATMEL_DB041D:
			tp->nvram_size = TG3_NVRAM_SIZE_512KB;
			break;
		case FLASH_5720VENDOR_M_ATMEL_DB081D:
		case FLASH_5720VENDOR_A_ATMEL_DB081D:
			tp->nvram_size = TG3_NVRAM_SIZE_1MB;
			break;
		default:
			if (tg3_asic_rev(tp) != ASIC_REV_5762)
				tp->nvram_size = TG3_NVRAM_SIZE_128KB;
			break;
		}
		break;
	case FLASH_5720VENDOR_M_ST_M25PE10:
	case FLASH_5720VENDOR_M_ST_M45PE10:
	case FLASH_5720VENDOR_A_ST_M25PE10:
	case FLASH_5720VENDOR_A_ST_M45PE10:
	case FLASH_5720VENDOR_M_ST_M25PE20:
	case FLASH_5720VENDOR_M_ST_M45PE20:
	case FLASH_5720VENDOR_A_ST_M25PE20:
	case FLASH_5720VENDOR_A_ST_M45PE20:
	case FLASH_5720VENDOR_M_ST_M25PE40:
	case FLASH_5720VENDOR_M_ST_M45PE40:
	case FLASH_5720VENDOR_A_ST_M25PE40:
	case FLASH_5720VENDOR_A_ST_M45PE40:
	case FLASH_5720VENDOR_M_ST_M25PE80:
	case FLASH_5720VENDOR_M_ST_M45PE80:
	case FLASH_5720VENDOR_A_ST_M25PE80:
	case FLASH_5720VENDOR_A_ST_M45PE80:
	case FLASH_5720VENDOR_ST_25USPT:
	case FLASH_5720VENDOR_ST_45USPT:
		tp->nvram_jedecnum = JEDEC_ST;
		tg3_flag_set(tp, NVRAM_BUFFERED);
		tg3_flag_set(tp, FLASH);

		switch (nvmpinstrp) {
		case FLASH_5720VENDOR_M_ST_M25PE20:
		case FLASH_5720VENDOR_M_ST_M45PE20:
		case FLASH_5720VENDOR_A_ST_M25PE20:
		case FLASH_5720VENDOR_A_ST_M45PE20:
			tp->nvram_size = TG3_NVRAM_SIZE_256KB;
			break;
		case FLASH_5720VENDOR_M_ST_M25PE40:
		case FLASH_5720VENDOR_M_ST_M45PE40:
		case FLASH_5720VENDOR_A_ST_M25PE40:
		case FLASH_5720VENDOR_A_ST_M45PE40:
			tp->nvram_size = TG3_NVRAM_SIZE_512KB;
			break;
		case FLASH_5720VENDOR_M_ST_M25PE80:
		case FLASH_5720VENDOR_M_ST_M45PE80:
		case FLASH_5720VENDOR_A_ST_M25PE80:
		case FLASH_5720VENDOR_A_ST_M45PE80:
			tp->nvram_size = TG3_NVRAM_SIZE_1MB;
			break;
		default:
			if (tg3_asic_rev(tp) != ASIC_REV_5762)
				tp->nvram_size = TG3_NVRAM_SIZE_128KB;
			break;
		}
		break;
	default:
		tg3_flag_set(tp, NO_NVRAM);
		return;
	}

	tg3_nvram_get_pagesize(tp, nvcfg1);
	if (tp->nvram_pagesize != 264 && tp->nvram_pagesize != 528)
		tg3_flag_set(tp, NO_NVRAM_ADDR_TRANS);

	if (tg3_asic_rev(tp) == ASIC_REV_5762) {
		u32 val;

		if (tg3_nvram_read(tp, 0, &val))
			return;

		if (val != TG3_EEPROM_MAGIC &&
		    (val & TG3_EEPROM_MAGIC_FW_MSK) != TG3_EEPROM_MAGIC_FW)
			tg3_flag_set(tp, NO_NVRAM);
	}
}

/* Chips other than 5700/5701 use the NVRAM for fetching info. */
static void tg3_nvram_init(struct tg3 *tp)
{
	if (tg3_flag(tp, IS_SSB_CORE)) {
		/* No NVRAM and EEPROM on the SSB Broadcom GigE core. */
		tg3_flag_clear(tp, NVRAM);
		tg3_flag_clear(tp, NVRAM_BUFFERED);
		tg3_flag_set(tp, NO_NVRAM);
		return;
	}

	tw32_f(GRC_EEPROM_ADDR,
	     (EEPROM_ADDR_FSM_RESET |
	      (EEPROM_DEFAULT_CLOCK_PERIOD <<
	       EEPROM_ADDR_CLKPERD_SHIFT)));

	msleep(1);

	/* Enable seeprom accesses. */
	tw32_f(GRC_LOCAL_CTRL,
	     tr32(GRC_LOCAL_CTRL) | GRC_LCLCTRL_AUTO_SEEPROM);
	udelay(100);

	if (tg3_asic_rev(tp) != ASIC_REV_5700 &&
	    tg3_asic_rev(tp) != ASIC_REV_5701) {
		tg3_flag_set(tp, NVRAM);

		if (tg3_nvram_lock(tp)) {
			netdev_warn(tp->dev,
				    "Cannot get nvram lock, %s failed\n",
				    __func__);
			return;
		}
		tg3_enable_nvram_access(tp);

		tp->nvram_size = 0;

		if (tg3_asic_rev(tp) == ASIC_REV_5752)
			tg3_get_5752_nvram_info(tp);
		else if (tg3_asic_rev(tp) == ASIC_REV_5755)
			tg3_get_5755_nvram_info(tp);
		else if (tg3_asic_rev(tp) == ASIC_REV_5787 ||
			 tg3_asic_rev(tp) == ASIC_REV_5784 ||
			 tg3_asic_rev(tp) == ASIC_REV_5785)
			tg3_get_5787_nvram_info(tp);
		else if (tg3_asic_rev(tp) == ASIC_REV_5761)
			tg3_get_5761_nvram_info(tp);
		else if (tg3_asic_rev(tp) == ASIC_REV_5906)
			tg3_get_5906_nvram_info(tp);
		else if (tg3_asic_rev(tp) == ASIC_REV_57780 ||
			 tg3_flag(tp, 57765_CLASS))
			tg3_get_57780_nvram_info(tp);
		else if (tg3_asic_rev(tp) == ASIC_REV_5717 ||
			 tg3_asic_rev(tp) == ASIC_REV_5719)
			tg3_get_5717_nvram_info(tp);
		else if (tg3_asic_rev(tp) == ASIC_REV_5720 ||
			 tg3_asic_rev(tp) == ASIC_REV_5762)
			tg3_get_5720_nvram_info(tp);
		else
			tg3_get_nvram_info(tp);

		if (tp->nvram_size == 0)
			tg3_get_nvram_size(tp);

		tg3_disable_nvram_access(tp);
		tg3_nvram_unlock(tp);

	} else {
		tg3_flag_clear(tp, NVRAM);
		tg3_flag_clear(tp, NVRAM_BUFFERED);

		tg3_get_eeprom_size(tp);
	}
}

struct subsys_tbl_ent {
	u16 subsys_vendor, subsys_devid;
	u32 phy_id;
};

static struct subsys_tbl_ent subsys_id_to_phy_id[] = {
	/* Broadcom boards. */
	{ TG3PCI_SUBVENDOR_ID_BROADCOM,
	  TG3PCI_SUBDEVICE_ID_BROADCOM_95700A6, TG3_PHY_ID_BCM5401 },
	{ TG3PCI_SUBVENDOR_ID_BROADCOM,
	  TG3PCI_SUBDEVICE_ID_BROADCOM_95701A5, TG3_PHY_ID_BCM5701 },
	{ TG3PCI_SUBVENDOR_ID_BROADCOM,
	  TG3PCI_SUBDEVICE_ID_BROADCOM_95700T6, TG3_PHY_ID_BCM8002 },
	{ TG3PCI_SUBVENDOR_ID_BROADCOM,
	  TG3PCI_SUBDEVICE_ID_BROADCOM_95700A9, 0 },
	{ TG3PCI_SUBVENDOR_ID_BROADCOM,
	  TG3PCI_SUBDEVICE_ID_BROADCOM_95701T1, TG3_PHY_ID_BCM5701 },
	{ TG3PCI_SUBVENDOR_ID_BROADCOM,
	  TG3PCI_SUBDEVICE_ID_BROADCOM_95701T8, TG3_PHY_ID_BCM5701 },
	{ TG3PCI_SUBVENDOR_ID_BROADCOM,
	  TG3PCI_SUBDEVICE_ID_BROADCOM_95701A7, 0 },
	{ TG3PCI_SUBVENDOR_ID_BROADCOM,
	  TG3PCI_SUBDEVICE_ID_BROADCOM_95701A10, TG3_PHY_ID_BCM5701 },
	{ TG3PCI_SUBVENDOR_ID_BROADCOM,
	  TG3PCI_SUBDEVICE_ID_BROADCOM_95701A12, TG3_PHY_ID_BCM5701 },
	{ TG3PCI_SUBVENDOR_ID_BROADCOM,
	  TG3PCI_SUBDEVICE_ID_BROADCOM_95703AX1, TG3_PHY_ID_BCM5703 },
	{ TG3PCI_SUBVENDOR_ID_BROADCOM,
	  TG3PCI_SUBDEVICE_ID_BROADCOM_95703AX2, TG3_PHY_ID_BCM5703 },

	/* 3com boards. */
	{ TG3PCI_SUBVENDOR_ID_3COM,
	  TG3PCI_SUBDEVICE_ID_3COM_3C996T, TG3_PHY_ID_BCM5401 },
	{ TG3PCI_SUBVENDOR_ID_3COM,
	  TG3PCI_SUBDEVICE_ID_3COM_3C996BT, TG3_PHY_ID_BCM5701 },
	{ TG3PCI_SUBVENDOR_ID_3COM,
	  TG3PCI_SUBDEVICE_ID_3COM_3C996SX, 0 },
	{ TG3PCI_SUBVENDOR_ID_3COM,
	  TG3PCI_SUBDEVICE_ID_3COM_3C1000T, TG3_PHY_ID_BCM5701 },
	{ TG3PCI_SUBVENDOR_ID_3COM,
	  TG3PCI_SUBDEVICE_ID_3COM_3C940BR01, TG3_PHY_ID_BCM5701 },

	/* DELL boards. */
	{ TG3PCI_SUBVENDOR_ID_DELL,
	  TG3PCI_SUBDEVICE_ID_DELL_VIPER, TG3_PHY_ID_BCM5401 },
	{ TG3PCI_SUBVENDOR_ID_DELL,
	  TG3PCI_SUBDEVICE_ID_DELL_JAGUAR, TG3_PHY_ID_BCM5401 },
	{ TG3PCI_SUBVENDOR_ID_DELL,
	  TG3PCI_SUBDEVICE_ID_DELL_MERLOT, TG3_PHY_ID_BCM5411 },
	{ TG3PCI_SUBVENDOR_ID_DELL,
	  TG3PCI_SUBDEVICE_ID_DELL_SLIM_MERLOT, TG3_PHY_ID_BCM5411 },

	/* Compaq boards. */
	{ TG3PCI_SUBVENDOR_ID_COMPAQ,
	  TG3PCI_SUBDEVICE_ID_COMPAQ_BANSHEE, TG3_PHY_ID_BCM5701 },
	{ TG3PCI_SUBVENDOR_ID_COMPAQ,
	  TG3PCI_SUBDEVICE_ID_COMPAQ_BANSHEE_2, TG3_PHY_ID_BCM5701 },
	{ TG3PCI_SUBVENDOR_ID_COMPAQ,
	  TG3PCI_SUBDEVICE_ID_COMPAQ_CHANGELING, 0 },
	{ TG3PCI_SUBVENDOR_ID_COMPAQ,
	  TG3PCI_SUBDEVICE_ID_COMPAQ_NC7780, TG3_PHY_ID_BCM5701 },
	{ TG3PCI_SUBVENDOR_ID_COMPAQ,
	  TG3PCI_SUBDEVICE_ID_COMPAQ_NC7780_2, TG3_PHY_ID_BCM5701 },

	/* IBM boards. */
	{ TG3PCI_SUBVENDOR_ID_IBM,
	  TG3PCI_SUBDEVICE_ID_IBM_5703SAX2, 0 }
};

static struct subsys_tbl_ent *tg3_lookup_by_subsys(struct tg3 *tp)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(subsys_id_to_phy_id); i++) {
		if ((subsys_id_to_phy_id[i].subsys_vendor ==
		     tp->pdev->subsystem_vendor) &&
		    (subsys_id_to_phy_id[i].subsys_devid ==
		     tp->pdev->subsystem_device))
			return &subsys_id_to_phy_id[i];
	}
	return NULL;
}

static void tg3_get_eeprom_hw_cfg(struct tg3 *tp)
{
	u32 val;

	tp->phy_id = TG3_PHY_ID_INVALID;
	tp->led_ctrl = LED_CTRL_MODE_PHY_1;

	/* Assume an onboard device and WOL capable by default.  */
	tg3_flag_set(tp, EEPROM_WRITE_PROT);
	tg3_flag_set(tp, WOL_CAP);

	if (tg3_asic_rev(tp) == ASIC_REV_5906) {
		if (!(tr32(PCIE_TRANSACTION_CFG) & PCIE_TRANS_CFG_LOM)) {
			tg3_flag_clear(tp, EEPROM_WRITE_PROT);
			tg3_flag_set(tp, IS_NIC);
		}
		val = tr32(VCPU_CFGSHDW);
		if (val & VCPU_CFGSHDW_ASPM_DBNC)
			tg3_flag_set(tp, ASPM_WORKAROUND);
		if ((val & VCPU_CFGSHDW_WOL_ENABLE) &&
		    (val & VCPU_CFGSHDW_WOL_MAGPKT)) {
			tg3_flag_set(tp, WOL_ENABLE);
			device_set_wakeup_enable(&tp->pdev->dev, true);
		}
		goto done;
	}

	tg3_read_mem(tp, NIC_SRAM_DATA_SIG, &val);
	if (val == NIC_SRAM_DATA_SIG_MAGIC) {
		u32 nic_cfg, led_cfg;
		u32 nic_phy_id, ver, cfg2 = 0, cfg4 = 0, eeprom_phy_id;
		int eeprom_phy_serdes = 0;

		tg3_read_mem(tp, NIC_SRAM_DATA_CFG, &nic_cfg);
		tp->nic_sram_data_cfg = nic_cfg;

		tg3_read_mem(tp, NIC_SRAM_DATA_VER, &ver);
		ver >>= NIC_SRAM_DATA_VER_SHIFT;
		if (tg3_asic_rev(tp) != ASIC_REV_5700 &&
		    tg3_asic_rev(tp) != ASIC_REV_5701 &&
		    tg3_asic_rev(tp) != ASIC_REV_5703 &&
		    (ver > 0) && (ver < 0x100))
			tg3_read_mem(tp, NIC_SRAM_DATA_CFG_2, &cfg2);

		if (tg3_asic_rev(tp) == ASIC_REV_5785)
			tg3_read_mem(tp, NIC_SRAM_DATA_CFG_4, &cfg4);

		if ((nic_cfg & NIC_SRAM_DATA_CFG_PHY_TYPE_MASK) ==
		    NIC_SRAM_DATA_CFG_PHY_TYPE_FIBER)
			eeprom_phy_serdes = 1;

		tg3_read_mem(tp, NIC_SRAM_DATA_PHY_ID, &nic_phy_id);
		if (nic_phy_id != 0) {
			u32 id1 = nic_phy_id & NIC_SRAM_DATA_PHY_ID1_MASK;
			u32 id2 = nic_phy_id & NIC_SRAM_DATA_PHY_ID2_MASK;

			eeprom_phy_id  = (id1 >> 16) << 10;
			eeprom_phy_id |= (id2 & 0xfc00) << 16;
			eeprom_phy_id |= (id2 & 0x03ff) <<  0;
		} else
			eeprom_phy_id = 0;

		tp->phy_id = eeprom_phy_id;
		if (eeprom_phy_serdes) {
			if (!tg3_flag(tp, 5705_PLUS))
				tp->phy_flags |= TG3_PHYFLG_PHY_SERDES;
			else
				tp->phy_flags |= TG3_PHYFLG_MII_SERDES;
		}

		if (tg3_flag(tp, 5750_PLUS))
			led_cfg = cfg2 & (NIC_SRAM_DATA_CFG_LED_MODE_MASK |
				    SHASTA_EXT_LED_MODE_MASK);
		else
			led_cfg = nic_cfg & NIC_SRAM_DATA_CFG_LED_MODE_MASK;

		switch (led_cfg) {
		default:
		case NIC_SRAM_DATA_CFG_LED_MODE_PHY_1:
			tp->led_ctrl = LED_CTRL_MODE_PHY_1;
			break;

		case NIC_SRAM_DATA_CFG_LED_MODE_PHY_2:
			tp->led_ctrl = LED_CTRL_MODE_PHY_2;
			break;

		case NIC_SRAM_DATA_CFG_LED_MODE_MAC:
			tp->led_ctrl = LED_CTRL_MODE_MAC;

			/* Default to PHY_1_MODE if 0 (MAC_MODE) is
			 * read on some older 5700/5701 bootcode.
			 */
			if (tg3_asic_rev(tp) == ASIC_REV_5700 ||
			    tg3_asic_rev(tp) == ASIC_REV_5701)
				tp->led_ctrl = LED_CTRL_MODE_PHY_1;

			break;

		case SHASTA_EXT_LED_SHARED:
			tp->led_ctrl = LED_CTRL_MODE_SHARED;
			if (tg3_chip_rev_id(tp) != CHIPREV_ID_5750_A0 &&
			    tg3_chip_rev_id(tp) != CHIPREV_ID_5750_A1)
				tp->led_ctrl |= (LED_CTRL_MODE_PHY_1 |
						 LED_CTRL_MODE_PHY_2);
			break;

		case SHASTA_EXT_LED_MAC:
			tp->led_ctrl = LED_CTRL_MODE_SHASTA_MAC;
			break;

		case SHASTA_EXT_LED_COMBO:
			tp->led_ctrl = LED_CTRL_MODE_COMBO;
			if (tg3_chip_rev_id(tp) != CHIPREV_ID_5750_A0)
				tp->led_ctrl |= (LED_CTRL_MODE_PHY_1 |
						 LED_CTRL_MODE_PHY_2);
			break;

		}

		if ((tg3_asic_rev(tp) == ASIC_REV_5700 ||
		     tg3_asic_rev(tp) == ASIC_REV_5701) &&
		    tp->pdev->subsystem_vendor == PCI_VENDOR_ID_DELL)
			tp->led_ctrl = LED_CTRL_MODE_PHY_2;

		if (tg3_chip_rev(tp) == CHIPREV_5784_AX)
			tp->led_ctrl = LED_CTRL_MODE_PHY_1;

		if (nic_cfg & NIC_SRAM_DATA_CFG_EEPROM_WP) {
			tg3_flag_set(tp, EEPROM_WRITE_PROT);
			if ((tp->pdev->subsystem_vendor ==
			     PCI_VENDOR_ID_ARIMA) &&
			    (tp->pdev->subsystem_device == 0x205a ||
			     tp->pdev->subsystem_device == 0x2063))
				tg3_flag_clear(tp, EEPROM_WRITE_PROT);
		} else {
			tg3_flag_clear(tp, EEPROM_WRITE_PROT);
			tg3_flag_set(tp, IS_NIC);
		}

		if (nic_cfg & NIC_SRAM_DATA_CFG_ASF_ENABLE) {
			tg3_flag_set(tp, ENABLE_ASF);
			if (tg3_flag(tp, 5750_PLUS))
				tg3_flag_set(tp, ASF_NEW_HANDSHAKE);
		}

		if ((nic_cfg & NIC_SRAM_DATA_CFG_APE_ENABLE) &&
		    tg3_flag(tp, 5750_PLUS))
			tg3_flag_set(tp, ENABLE_APE);

		if (tp->phy_flags & TG3_PHYFLG_ANY_SERDES &&
		    !(nic_cfg & NIC_SRAM_DATA_CFG_FIBER_WOL))
			tg3_flag_clear(tp, WOL_CAP);

		if (tg3_flag(tp, WOL_CAP) &&
		    (nic_cfg & NIC_SRAM_DATA_CFG_WOL_ENABLE)) {
			tg3_flag_set(tp, WOL_ENABLE);
			device_set_wakeup_enable(&tp->pdev->dev, true);
		}

		if (cfg2 & (1 << 17))
			tp->phy_flags |= TG3_PHYFLG_CAPACITIVE_COUPLING;

		/* serdes signal pre-emphasis in register 0x590 set by */
		/* bootcode if bit 18 is set */
		if (cfg2 & (1 << 18))
			tp->phy_flags |= TG3_PHYFLG_SERDES_PREEMPHASIS;

		if ((tg3_flag(tp, 57765_PLUS) ||
		     (tg3_asic_rev(tp) == ASIC_REV_5784 &&
		      tg3_chip_rev(tp) != CHIPREV_5784_AX)) &&
		    (cfg2 & NIC_SRAM_DATA_CFG_2_APD_EN))
			tp->phy_flags |= TG3_PHYFLG_ENABLE_APD;

		if (tg3_flag(tp, PCI_EXPRESS)) {
			u32 cfg3;

			tg3_read_mem(tp, NIC_SRAM_DATA_CFG_3, &cfg3);
			if (tg3_asic_rev(tp) != ASIC_REV_5785 &&
			    !tg3_flag(tp, 57765_PLUS) &&
			    (cfg3 & NIC_SRAM_ASPM_DEBOUNCE))
				tg3_flag_set(tp, ASPM_WORKAROUND);
			if (cfg3 & NIC_SRAM_LNK_FLAP_AVOID)
				tp->phy_flags |= TG3_PHYFLG_KEEP_LINK_ON_PWRDN;
			if (cfg3 & NIC_SRAM_1G_ON_VAUX_OK)
				tp->phy_flags |= TG3_PHYFLG_1G_ON_VAUX_OK;
		}

		if (cfg4 & NIC_SRAM_RGMII_INBAND_DISABLE)
			tg3_flag_set(tp, RGMII_INBAND_DISABLE);
		if (cfg4 & NIC_SRAM_RGMII_EXT_IBND_RX_EN)
			tg3_flag_set(tp, RGMII_EXT_IBND_RX_EN);
		if (cfg4 & NIC_SRAM_RGMII_EXT_IBND_TX_EN)
			tg3_flag_set(tp, RGMII_EXT_IBND_TX_EN);
	}
done:
	if (tg3_flag(tp, WOL_CAP))
		device_set_wakeup_enable(&tp->pdev->dev,
					 tg3_flag(tp, WOL_ENABLE));
	else
		device_set_wakeup_capable(&tp->pdev->dev, false);
}

static int tg3_ape_otp_read(struct tg3 *tp, u32 offset, u32 *val)
{
	int i, err;
	u32 val2, off = offset * 8;

	err = tg3_nvram_lock(tp);
	if (err)
		return err;

	tg3_ape_write32(tp, TG3_APE_OTP_ADDR, off | APE_OTP_ADDR_CPU_ENABLE);
	tg3_ape_write32(tp, TG3_APE_OTP_CTRL, APE_OTP_CTRL_PROG_EN |
			APE_OTP_CTRL_CMD_RD | APE_OTP_CTRL_START);
	tg3_ape_read32(tp, TG3_APE_OTP_CTRL);
	udelay(10);

	for (i = 0; i < 100; i++) {
		val2 = tg3_ape_read32(tp, TG3_APE_OTP_STATUS);
		if (val2 & APE_OTP_STATUS_CMD_DONE) {
			*val = tg3_ape_read32(tp, TG3_APE_OTP_RD_DATA);
			break;
		}
		udelay(10);
	}

	tg3_ape_write32(tp, TG3_APE_OTP_CTRL, 0);

	tg3_nvram_unlock(tp);
	if (val2 & APE_OTP_STATUS_CMD_DONE)
		return 0;

	return -EBUSY;
}

static int tg3_issue_otp_command(struct tg3 *tp, u32 cmd)
{
	int i;
	u32 val;

	tw32(OTP_CTRL, cmd | OTP_CTRL_OTP_CMD_START);
	tw32(OTP_CTRL, cmd);

	/* Wait for up to 1 ms for command to execute. */
	for (i = 0; i < 100; i++) {
		val = tr32(OTP_STATUS);
		if (val & OTP_STATUS_CMD_DONE)
			break;
		udelay(10);
	}

	return (val & OTP_STATUS_CMD_DONE) ? 0 : -EBUSY;
}

/* Read the gphy configuration from the OTP region of the chip.  The gphy
 * configuration is a 32-bit value that straddles the alignment boundary.
 * We do two 32-bit reads and then shift and merge the results.
 */
static u32 tg3_read_otp_phycfg(struct tg3 *tp)
{
	u32 bhalf_otp, thalf_otp;

	tw32(OTP_MODE, OTP_MODE_OTP_THRU_GRC);

	if (tg3_issue_otp_command(tp, OTP_CTRL_OTP_CMD_INIT))
		return 0;

	tw32(OTP_ADDRESS, OTP_ADDRESS_MAGIC1);

	if (tg3_issue_otp_command(tp, OTP_CTRL_OTP_CMD_READ))
		return 0;

	thalf_otp = tr32(OTP_READ_DATA);

	tw32(OTP_ADDRESS, OTP_ADDRESS_MAGIC2);

	if (tg3_issue_otp_command(tp, OTP_CTRL_OTP_CMD_READ))
		return 0;

	bhalf_otp = tr32(OTP_READ_DATA);

	return ((thalf_otp & 0x0000ffff) << 16) | (bhalf_otp >> 16);
}

static void tg3_phy_init_link_config(struct tg3 *tp)
{
	u32 adv = ADVERTISED_Autoneg;

	if (!(tp->phy_flags & TG3_PHYFLG_10_100_ONLY))
		adv |= ADVERTISED_1000baseT_Half |
		       ADVERTISED_1000baseT_Full;

	if (!(tp->phy_flags & TG3_PHYFLG_ANY_SERDES))
		adv |= ADVERTISED_100baseT_Half |
		       ADVERTISED_100baseT_Full |
		       ADVERTISED_10baseT_Half |
		       ADVERTISED_10baseT_Full |
		       ADVERTISED_TP;
	else
		adv |= ADVERTISED_FIBRE;

	tp->link_config.advertising = adv;
	tp->link_config.speed = SPEED_UNKNOWN;
	tp->link_config.duplex = DUPLEX_UNKNOWN;
	tp->link_config.autoneg = AUTONEG_ENABLE;
	tp->link_config.active_speed = SPEED_UNKNOWN;
	tp->link_config.active_duplex = DUPLEX_UNKNOWN;

	tp->old_link = -1;
}

static int tg3_phy_probe(struct tg3 *tp)
{
	u32 hw_phy_id_1, hw_phy_id_2;
	u32 hw_phy_id, hw_phy_id_masked;
	int err;

	/* flow control autonegotiation is default behavior */
	tg3_flag_set(tp, PAUSE_AUTONEG);
	tp->link_config.flowctrl = FLOW_CTRL_TX | FLOW_CTRL_RX;

	if (tg3_flag(tp, ENABLE_APE)) {
		switch (tp->pci_fn) {
		case 0:
			tp->phy_ape_lock = TG3_APE_LOCK_PHY0;
			break;
		case 1:
			tp->phy_ape_lock = TG3_APE_LOCK_PHY1;
			break;
		case 2:
			tp->phy_ape_lock = TG3_APE_LOCK_PHY2;
			break;
		case 3:
			tp->phy_ape_lock = TG3_APE_LOCK_PHY3;
			break;
		}
	}

	if (!tg3_flag(tp, ENABLE_ASF) &&
	    !(tp->phy_flags & TG3_PHYFLG_ANY_SERDES) &&
	    !(tp->phy_flags & TG3_PHYFLG_10_100_ONLY))
		tp->phy_flags &= ~(TG3_PHYFLG_1G_ON_VAUX_OK |
				   TG3_PHYFLG_KEEP_LINK_ON_PWRDN);

	if (tg3_flag(tp, USE_PHYLIB))
		return tg3_phy_init(tp);

	/* Reading the PHY ID register can conflict with ASF
	 * firmware access to the PHY hardware.
	 */
	err = 0;
	if (tg3_flag(tp, ENABLE_ASF) || tg3_flag(tp, ENABLE_APE)) {
		hw_phy_id = hw_phy_id_masked = TG3_PHY_ID_INVALID;
	} else {
		/* Now read the physical PHY_ID from the chip and verify
		 * that it is sane.  If it doesn't look good, we fall back
		 * to either the hard-coded table based PHY_ID and failing
		 * that the value found in the eeprom area.
		 */
		err |= tg3_readphy(tp, MII_PHYSID1, &hw_phy_id_1);
		err |= tg3_readphy(tp, MII_PHYSID2, &hw_phy_id_2);

		hw_phy_id  = (hw_phy_id_1 & 0xffff) << 10;
		hw_phy_id |= (hw_phy_id_2 & 0xfc00) << 16;
		hw_phy_id |= (hw_phy_id_2 & 0x03ff) <<  0;

		hw_phy_id_masked = hw_phy_id & TG3_PHY_ID_MASK;
	}

	if (!err && TG3_KNOWN_PHY_ID(hw_phy_id_masked)) {
		tp->phy_id = hw_phy_id;
		if (hw_phy_id_masked == TG3_PHY_ID_BCM8002)
			tp->phy_flags |= TG3_PHYFLG_PHY_SERDES;
		else
			tp->phy_flags &= ~TG3_PHYFLG_PHY_SERDES;
	} else {
		if (tp->phy_id != TG3_PHY_ID_INVALID) {
			/* Do nothing, phy ID already set up in
			 * tg3_get_eeprom_hw_cfg().
			 */
		} else {
			struct subsys_tbl_ent *p;

			/* No eeprom signature?  Try the hardcoded
			 * subsys device table.
			 */
			p = tg3_lookup_by_subsys(tp);
			if (p) {
				tp->phy_id = p->phy_id;
			} else if (!tg3_flag(tp, IS_SSB_CORE)) {
				/* For now we saw the IDs 0xbc050cd0,
				 * 0xbc050f80 and 0xbc050c30 on devices
				 * connected to an BCM4785 and there are
				 * probably more. Just assume that the phy is
				 * supported when it is connected to a SSB core
				 * for now.
				 */
				return -ENODEV;
			}

			if (!tp->phy_id ||
			    tp->phy_id == TG3_PHY_ID_BCM8002)
				tp->phy_flags |= TG3_PHYFLG_PHY_SERDES;
		}
	}

	if (!(tp->phy_flags & TG3_PHYFLG_ANY_SERDES) &&
	    (tg3_asic_rev(tp) == ASIC_REV_5719 ||
	     tg3_asic_rev(tp) == ASIC_REV_5720 ||
	     tg3_asic_rev(tp) == ASIC_REV_57766 ||
	     tg3_asic_rev(tp) == ASIC_REV_5762 ||
	     (tg3_asic_rev(tp) == ASIC_REV_5717 &&
	      tg3_chip_rev_id(tp) != CHIPREV_ID_5717_A0) ||
	     (tg3_asic_rev(tp) == ASIC_REV_57765 &&
	      tg3_chip_rev_id(tp) != CHIPREV_ID_57765_A0)))
		tp->phy_flags |= TG3_PHYFLG_EEE_CAP;

	tg3_phy_init_link_config(tp);

	if (!(tp->phy_flags & TG3_PHYFLG_KEEP_LINK_ON_PWRDN) &&
	    !(tp->phy_flags & TG3_PHYFLG_ANY_SERDES) &&
	    !tg3_flag(tp, ENABLE_APE) &&
	    !tg3_flag(tp, ENABLE_ASF)) {
		u32 bmsr, dummy;

		tg3_readphy(tp, MII_BMSR, &bmsr);
		if (!tg3_readphy(tp, MII_BMSR, &bmsr) &&
		    (bmsr & BMSR_LSTATUS))
			goto skip_phy_reset;

		err = tg3_phy_reset(tp);
		if (err)
			return err;

		tg3_phy_set_wirespeed(tp);

		if (!tg3_phy_copper_an_config_ok(tp, &dummy)) {
			tg3_phy_autoneg_cfg(tp, tp->link_config.advertising,
					    tp->link_config.flowctrl);

			tg3_writephy(tp, MII_BMCR,
				     BMCR_ANENABLE | BMCR_ANRESTART);
		}
	}

skip_phy_reset:
	if ((tp->phy_id & TG3_PHY_ID_MASK) == TG3_PHY_ID_BCM5401) {
		err = tg3_init_5401phy_dsp(tp);
		if (err)
			return err;

		err = tg3_init_5401phy_dsp(tp);
	}

	return err;
}

static void tg3_read_vpd(struct tg3 *tp)
{
	u8 *vpd_data;
	unsigned int block_end, rosize, len;
	u32 vpdlen;
	int j, i = 0;

	vpd_data = (u8 *)tg3_vpd_readblock(tp, &vpdlen);
	if (!vpd_data)
		goto out_no_vpd;

	i = pci_vpd_find_tag(vpd_data, 0, vpdlen, PCI_VPD_LRDT_RO_DATA);
	if (i < 0)
		goto out_not_found;

	rosize = pci_vpd_lrdt_size(&vpd_data[i]);
	block_end = i + PCI_VPD_LRDT_TAG_SIZE + rosize;
	i += PCI_VPD_LRDT_TAG_SIZE;

	if (block_end > vpdlen)
		goto out_not_found;

	j = pci_vpd_find_info_keyword(vpd_data, i, rosize,
				      PCI_VPD_RO_KEYWORD_MFR_ID);
	if (j > 0) {
		len = pci_vpd_info_field_size(&vpd_data[j]);

		j += PCI_VPD_INFO_FLD_HDR_SIZE;
		if (j + len > block_end || len != 4 ||
		    memcmp(&vpd_data[j], "1028", 4))
			goto partno;

		j = pci_vpd_find_info_keyword(vpd_data, i, rosize,
					      PCI_VPD_RO_KEYWORD_VENDOR0);
		if (j < 0)
			goto partno;

		len = pci_vpd_info_field_size(&vpd_data[j]);

		j += PCI_VPD_INFO_FLD_HDR_SIZE;
		if (j + len > block_end)
			goto partno;

		if (len >= sizeof(tp->fw_ver))
			len = sizeof(tp->fw_ver) - 1;
		memset(tp->fw_ver, 0, sizeof(tp->fw_ver));
		snprintf(tp->fw_ver, sizeof(tp->fw_ver), "%.*s bc ", len,
			 &vpd_data[j]);
	}

partno:
	i = pci_vpd_find_info_keyword(vpd_data, i, rosize,
				      PCI_VPD_RO_KEYWORD_PARTNO);
	if (i < 0)
		goto out_not_found;

	len = pci_vpd_info_field_size(&vpd_data[i]);

	i += PCI_VPD_INFO_FLD_HDR_SIZE;
	if (len > TG3_BPN_SIZE ||
	    (len + i) > vpdlen)
		goto out_not_found;

	memcpy(tp->board_part_number, &vpd_data[i], len);

out_not_found:
	kfree(vpd_data);
	if (tp->board_part_number[0])
		return;

out_no_vpd:
	if (tg3_asic_rev(tp) == ASIC_REV_5717) {
		if (tp->pdev->device == TG3PCI_DEVICE_TIGON3_5717 ||
		    tp->pdev->device == TG3PCI_DEVICE_TIGON3_5717_C)
			strcpy(tp->board_part_number, "BCM5717");
		else if (tp->pdev->device == TG3PCI_DEVICE_TIGON3_5718)
			strcpy(tp->board_part_number, "BCM5718");
		else
			goto nomatch;
	} else if (tg3_asic_rev(tp) == ASIC_REV_57780) {
		if (tp->pdev->device == TG3PCI_DEVICE_TIGON3_57780)
			strcpy(tp->board_part_number, "BCM57780");
		else if (tp->pdev->device == TG3PCI_DEVICE_TIGON3_57760)
			strcpy(tp->board_part_number, "BCM57760");
		else if (tp->pdev->device == TG3PCI_DEVICE_TIGON3_57790)
			strcpy(tp->board_part_number, "BCM57790");
		else if (tp->pdev->device == TG3PCI_DEVICE_TIGON3_57788)
			strcpy(tp->board_part_number, "BCM57788");
		else
			goto nomatch;
	} else if (tg3_asic_rev(tp) == ASIC_REV_57765) {
		if (tp->pdev->device == TG3PCI_DEVICE_TIGON3_57761)
			strcpy(tp->board_part_number, "BCM57761");
		else if (tp->pdev->device == TG3PCI_DEVICE_TIGON3_57765)
			strcpy(tp->board_part_number, "BCM57765");
		else if (tp->pdev->device == TG3PCI_DEVICE_TIGON3_57781)
			strcpy(tp->board_part_number, "BCM57781");
		else if (tp->pdev->device == TG3PCI_DEVICE_TIGON3_57785)
			strcpy(tp->board_part_number, "BCM57785");
		else if (tp->pdev->device == TG3PCI_DEVICE_TIGON3_57791)
			strcpy(tp->board_part_number, "BCM57791");
		else if (tp->pdev->device == TG3PCI_DEVICE_TIGON3_57795)
			strcpy(tp->board_part_number, "BCM57795");
		else
			goto nomatch;
	} else if (tg3_asic_rev(tp) == ASIC_REV_57766) {
		if (tp->pdev->device == TG3PCI_DEVICE_TIGON3_57762)
			strcpy(tp->board_part_number, "BCM57762");
		else if (tp->pdev->device == TG3PCI_DEVICE_TIGON3_57766)
			strcpy(tp->board_part_number, "BCM57766");
		else if (tp->pdev->device == TG3PCI_DEVICE_TIGON3_57782)
			strcpy(tp->board_part_number, "BCM57782");
		else if (tp->pdev->device == TG3PCI_DEVICE_TIGON3_57786)
			strcpy(tp->board_part_number, "BCM57786");
		else
			goto nomatch;
	} else if (tg3_asic_rev(tp) == ASIC_REV_5906) {
		strcpy(tp->board_part_number, "BCM95906");
	} else {
nomatch:
		strcpy(tp->board_part_number, "none");
	}
}

static int tg3_fw_img_is_valid(struct tg3 *tp, u32 offset)
{
	u32 val;

	if (tg3_nvram_read(tp, offset, &val) ||
	    (val & 0xfc000000) != 0x0c000000 ||
	    tg3_nvram_read(tp, offset + 4, &val) ||
	    val != 0)
		return 0;

	return 1;
}

static void tg3_read_bc_ver(struct tg3 *tp)
{
	u32 val, offset, start, ver_offset;
	int i, dst_off;
	bool newver = false;

	if (tg3_nvram_read(tp, 0xc, &offset) ||
	    tg3_nvram_read(tp, 0x4, &start))
		return;

	offset = tg3_nvram_logical_addr(tp, offset);

	if (tg3_nvram_read(tp, offset, &val))
		return;

	if ((val & 0xfc000000) == 0x0c000000) {
		if (tg3_nvram_read(tp, offset + 4, &val))
			return;

		if (val == 0)
			newver = true;
	}

	dst_off = strlen(tp->fw_ver);

	if (newver) {
		if (TG3_VER_SIZE - dst_off < 16 ||
		    tg3_nvram_read(tp, offset + 8, &ver_offset))
			return;

		offset = offset + ver_offset - start;
		for (i = 0; i < 16; i += 4) {
			__be32 v;
			if (tg3_nvram_read_be32(tp, offset + i, &v))
				return;

			memcpy(tp->fw_ver + dst_off + i, &v, sizeof(v));
		}
	} else {
		u32 major, minor;

		if (tg3_nvram_read(tp, TG3_NVM_PTREV_BCVER, &ver_offset))
			return;

		major = (ver_offset & TG3_NVM_BCVER_MAJMSK) >>
			TG3_NVM_BCVER_MAJSFT;
		minor = ver_offset & TG3_NVM_BCVER_MINMSK;
		snprintf(&tp->fw_ver[dst_off], TG3_VER_SIZE - dst_off,
			 "v%d.%02d", major, minor);
	}
}

static void tg3_read_hwsb_ver(struct tg3 *tp)
{
	u32 val, major, minor;

	/* Use native endian representation */
	if (tg3_nvram_read(tp, TG3_NVM_HWSB_CFG1, &val))
		return;

	major = (val & TG3_NVM_HWSB_CFG1_MAJMSK) >>
		TG3_NVM_HWSB_CFG1_MAJSFT;
	minor = (val & TG3_NVM_HWSB_CFG1_MINMSK) >>
		TG3_NVM_HWSB_CFG1_MINSFT;

	snprintf(&tp->fw_ver[0], 32, "sb v%d.%02d", major, minor);
}

static void tg3_read_sb_ver(struct tg3 *tp, u32 val)
{
	u32 offset, major, minor, build;

	strncat(tp->fw_ver, "sb", TG3_VER_SIZE - strlen(tp->fw_ver) - 1);

	if ((val & TG3_EEPROM_SB_FORMAT_MASK) != TG3_EEPROM_SB_FORMAT_1)
		return;

	switch (val & TG3_EEPROM_SB_REVISION_MASK) {
	case TG3_EEPROM_SB_REVISION_0:
		offset = TG3_EEPROM_SB_F1R0_EDH_OFF;
		break;
	case TG3_EEPROM_SB_REVISION_2:
		offset = TG3_EEPROM_SB_F1R2_EDH_OFF;
		break;
	case TG3_EEPROM_SB_REVISION_3:
		offset = TG3_EEPROM_SB_F1R3_EDH_OFF;
		break;
	case TG3_EEPROM_SB_REVISION_4:
		offset = TG3_EEPROM_SB_F1R4_EDH_OFF;
		break;
	case TG3_EEPROM_SB_REVISION_5:
		offset = TG3_EEPROM_SB_F1R5_EDH_OFF;
		break;
	case TG3_EEPROM_SB_REVISION_6:
		offset = TG3_EEPROM_SB_F1R6_EDH_OFF;
		break;
	default:
		return;
	}

	if (tg3_nvram_read(tp, offset, &val))
		return;

	build = (val & TG3_EEPROM_SB_EDH_BLD_MASK) >>
		TG3_EEPROM_SB_EDH_BLD_SHFT;
	major = (val & TG3_EEPROM_SB_EDH_MAJ_MASK) >>
		TG3_EEPROM_SB_EDH_MAJ_SHFT;
	minor =  val & TG3_EEPROM_SB_EDH_MIN_MASK;

	if (minor > 99 || build > 26)
		return;

	offset = strlen(tp->fw_ver);
	snprintf(&tp->fw_ver[offset], TG3_VER_SIZE - offset,
		 " v%d.%02d", major, minor);

	if (build > 0) {
		offset = strlen(tp->fw_ver);
		if (offset < TG3_VER_SIZE - 1)
			tp->fw_ver[offset] = 'a' + build - 1;
	}
}

static void tg3_read_mgmtfw_ver(struct tg3 *tp)
{
	u32 val, offset, start;
	int i, vlen;

	for (offset = TG3_NVM_DIR_START;
	     offset < TG3_NVM_DIR_END;
	     offset += TG3_NVM_DIRENT_SIZE) {
		if (tg3_nvram_read(tp, offset, &val))
			return;

		if ((val >> TG3_NVM_DIRTYPE_SHIFT) == TG3_NVM_DIRTYPE_ASFINI)
			break;
	}

	if (offset == TG3_NVM_DIR_END)
		return;

	if (!tg3_flag(tp, 5705_PLUS))
		start = 0x08000000;
	else if (tg3_nvram_read(tp, offset - 4, &start))
		return;

	if (tg3_nvram_read(tp, offset + 4, &offset) ||
	    !tg3_fw_img_is_valid(tp, offset) ||
	    tg3_nvram_read(tp, offset + 8, &val))
		return;

	offset += val - start;

	vlen = strlen(tp->fw_ver);

	tp->fw_ver[vlen++] = ',';
	tp->fw_ver[vlen++] = ' ';

	for (i = 0; i < 4; i++) {
		__be32 v;
		if (tg3_nvram_read_be32(tp, offset, &v))
			return;

		offset += sizeof(v);

		if (vlen > TG3_VER_SIZE - sizeof(v)) {
			memcpy(&tp->fw_ver[vlen], &v, TG3_VER_SIZE - vlen);
			break;
		}

		memcpy(&tp->fw_ver[vlen], &v, sizeof(v));
		vlen += sizeof(v);
	}
}

static void tg3_probe_ncsi(struct tg3 *tp)
{
	u32 apedata;

	apedata = tg3_ape_read32(tp, TG3_APE_SEG_SIG);
	if (apedata != APE_SEG_SIG_MAGIC)
		return;

	apedata = tg3_ape_read32(tp, TG3_APE_FW_STATUS);
	if (!(apedata & APE_FW_STATUS_READY))
		return;

	if (tg3_ape_read32(tp, TG3_APE_FW_FEATURES) & TG3_APE_FW_FEATURE_NCSI)
		tg3_flag_set(tp, APE_HAS_NCSI);
}

static void tg3_read_dash_ver(struct tg3 *tp)
{
	int vlen;
	u32 apedata;
	char *fwtype;

	apedata = tg3_ape_read32(tp, TG3_APE_FW_VERSION);

	if (tg3_flag(tp, APE_HAS_NCSI))
		fwtype = "NCSI";
	else if (tp->pdev->device == TG3PCI_DEVICE_TIGON3_5725)
		fwtype = "SMASH";
	else
		fwtype = "DASH";

	vlen = strlen(tp->fw_ver);

	snprintf(&tp->fw_ver[vlen], TG3_VER_SIZE - vlen, " %s v%d.%d.%d.%d",
		 fwtype,
		 (apedata & APE_FW_VERSION_MAJMSK) >> APE_FW_VERSION_MAJSFT,
		 (apedata & APE_FW_VERSION_MINMSK) >> APE_FW_VERSION_MINSFT,
		 (apedata & APE_FW_VERSION_REVMSK) >> APE_FW_VERSION_REVSFT,
		 (apedata & APE_FW_VERSION_BLDMSK));
}

static void tg3_read_otp_ver(struct tg3 *tp)
{
	u32 val, val2;

	if (tg3_asic_rev(tp) != ASIC_REV_5762)
		return;

	if (!tg3_ape_otp_read(tp, OTP_ADDRESS_MAGIC0, &val) &&
	    !tg3_ape_otp_read(tp, OTP_ADDRESS_MAGIC0 + 4, &val2) &&
	    TG3_OTP_MAGIC0_VALID(val)) {
		u64 val64 = (u64) val << 32 | val2;
		u32 ver = 0;
		int i, vlen;

		for (i = 0; i < 7; i++) {
			if ((val64 & 0xff) == 0)
				break;
			ver = val64 & 0xff;
			val64 >>= 8;
		}
		vlen = strlen(tp->fw_ver);
		snprintf(&tp->fw_ver[vlen], TG3_VER_SIZE - vlen, " .%02d", ver);
	}
}

static void tg3_read_fw_ver(struct tg3 *tp)
{
	u32 val;
	bool vpd_vers = false;

	if (tp->fw_ver[0] != 0)
		vpd_vers = true;

	if (tg3_flag(tp, NO_NVRAM)) {
		strcat(tp->fw_ver, "sb");
		tg3_read_otp_ver(tp);
		return;
	}

	if (tg3_nvram_read(tp, 0, &val))
		return;

	if (val == TG3_EEPROM_MAGIC)
		tg3_read_bc_ver(tp);
	else if ((val & TG3_EEPROM_MAGIC_FW_MSK) == TG3_EEPROM_MAGIC_FW)
		tg3_read_sb_ver(tp, val);
	else if ((val & TG3_EEPROM_MAGIC_HW_MSK) == TG3_EEPROM_MAGIC_HW)
		tg3_read_hwsb_ver(tp);

	if (tg3_flag(tp, ENABLE_ASF)) {
		if (tg3_flag(tp, ENABLE_APE)) {
			tg3_probe_ncsi(tp);
			if (!vpd_vers)
				tg3_read_dash_ver(tp);
		} else if (!vpd_vers) {
			tg3_read_mgmtfw_ver(tp);
		}
	}

	tp->fw_ver[TG3_VER_SIZE - 1] = 0;
}

static inline u32 tg3_rx_ret_ring_size(struct tg3 *tp)
{
	if (tg3_flag(tp, LRG_PROD_RING_CAP))
		return TG3_RX_RET_MAX_SIZE_5717;
	else if (tg3_flag(tp, JUMBO_CAPABLE) && !tg3_flag(tp, 5780_CLASS))
		return TG3_RX_RET_MAX_SIZE_5700;
	else
		return TG3_RX_RET_MAX_SIZE_5705;
}

static DEFINE_PCI_DEVICE_TABLE(tg3_write_reorder_chipsets) = {
	{ PCI_DEVICE(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_FE_GATE_700C) },
	{ PCI_DEVICE(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_8131_BRIDGE) },
	{ PCI_DEVICE(PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_8385_0) },
	{ },
};

static struct pci_dev *tg3_find_peer(struct tg3 *tp)
{
	struct pci_dev *peer;
	unsigned int func, devnr = tp->pdev->devfn & ~7;

	for (func = 0; func < 8; func++) {
		peer = pci_get_slot(tp->pdev->bus, devnr | func);
		if (peer && peer != tp->pdev)
			break;
		pci_dev_put(peer);
	}
	/* 5704 can be configured in single-port mode, set peer to
	 * tp->pdev in that case.
	 */
	if (!peer) {
		peer = tp->pdev;
		return peer;
	}

	/*
	 * We don't need to keep the refcount elevated; there's no way
	 * to remove one half of this device without removing the other
	 */
	pci_dev_put(peer);

	return peer;
}

static void tg3_detect_asic_rev(struct tg3 *tp, u32 misc_ctrl_reg)
{
	tp->pci_chip_rev_id = misc_ctrl_reg >> MISC_HOST_CTRL_CHIPREV_SHIFT;
	if (tg3_asic_rev(tp) == ASIC_REV_USE_PROD_ID_REG) {
		u32 reg;

		/* All devices that use the alternate
		 * ASIC REV location have a CPMU.
		 */
		tg3_flag_set(tp, CPMU_PRESENT);

		if (tp->pdev->device == TG3PCI_DEVICE_TIGON3_5717 ||
		    tp->pdev->device == TG3PCI_DEVICE_TIGON3_5717_C ||
		    tp->pdev->device == TG3PCI_DEVICE_TIGON3_5718 ||
		    tp->pdev->device == TG3PCI_DEVICE_TIGON3_5719 ||
		    tp->pdev->device == TG3PCI_DEVICE_TIGON3_5720 ||
		    tp->pdev->device == TG3PCI_DEVICE_TIGON3_5762 ||
		    tp->pdev->device == TG3PCI_DEVICE_TIGON3_5725 ||
		    tp->pdev->device == TG3PCI_DEVICE_TIGON3_5727)
			reg = TG3PCI_GEN2_PRODID_ASICREV;
		else if (tp->pdev->device == TG3PCI_DEVICE_TIGON3_57781 ||
			 tp->pdev->device == TG3PCI_DEVICE_TIGON3_57785 ||
			 tp->pdev->device == TG3PCI_DEVICE_TIGON3_57761 ||
			 tp->pdev->device == TG3PCI_DEVICE_TIGON3_57765 ||
			 tp->pdev->device == TG3PCI_DEVICE_TIGON3_57791 ||
			 tp->pdev->device == TG3PCI_DEVICE_TIGON3_57795 ||
			 tp->pdev->device == TG3PCI_DEVICE_TIGON3_57762 ||
			 tp->pdev->device == TG3PCI_DEVICE_TIGON3_57766 ||
			 tp->pdev->device == TG3PCI_DEVICE_TIGON3_57782 ||
			 tp->pdev->device == TG3PCI_DEVICE_TIGON3_57786)
			reg = TG3PCI_GEN15_PRODID_ASICREV;
		else
			reg = TG3PCI_PRODID_ASICREV;

		pci_read_config_dword(tp->pdev, reg, &tp->pci_chip_rev_id);
	}

	/* Wrong chip ID in 5752 A0. This code can be removed later
	 * as A0 is not in production.
	 */
	if (tg3_chip_rev_id(tp) == CHIPREV_ID_5752_A0_HW)
		tp->pci_chip_rev_id = CHIPREV_ID_5752_A0;

	if (tg3_chip_rev_id(tp) == CHIPREV_ID_5717_C0)
		tp->pci_chip_rev_id = CHIPREV_ID_5720_A0;

	if (tg3_asic_rev(tp) == ASIC_REV_5717 ||
	    tg3_asic_rev(tp) == ASIC_REV_5719 ||
	    tg3_asic_rev(tp) == ASIC_REV_5720)
		tg3_flag_set(tp, 5717_PLUS);

	if (tg3_asic_rev(tp) == ASIC_REV_57765 ||
	    tg3_asic_rev(tp) == ASIC_REV_57766)
		tg3_flag_set(tp, 57765_CLASS);

	if (tg3_flag(tp, 57765_CLASS) || tg3_flag(tp, 5717_PLUS) ||
	     tg3_asic_rev(tp) == ASIC_REV_5762)
		tg3_flag_set(tp, 57765_PLUS);

	/* Intentionally exclude ASIC_REV_5906 */
	if (tg3_asic_rev(tp) == ASIC_REV_5755 ||
	    tg3_asic_rev(tp) == ASIC_REV_5787 ||
	    tg3_asic_rev(tp) == ASIC_REV_5784 ||
	    tg3_asic_rev(tp) == ASIC_REV_5761 ||
	    tg3_asic_rev(tp) == ASIC_REV_5785 ||
	    tg3_asic_rev(tp) == ASIC_REV_57780 ||
	    tg3_flag(tp, 57765_PLUS))
		tg3_flag_set(tp, 5755_PLUS);

	if (tg3_asic_rev(tp) == ASIC_REV_5780 ||
	    tg3_asic_rev(tp) == ASIC_REV_5714)
		tg3_flag_set(tp, 5780_CLASS);

	if (tg3_asic_rev(tp) == ASIC_REV_5750 ||
	    tg3_asic_rev(tp) == ASIC_REV_5752 ||
	    tg3_asic_rev(tp) == ASIC_REV_5906 ||
	    tg3_flag(tp, 5755_PLUS) ||
	    tg3_flag(tp, 5780_CLASS))
		tg3_flag_set(tp, 5750_PLUS);

	if (tg3_asic_rev(tp) == ASIC_REV_5705 ||
	    tg3_flag(tp, 5750_PLUS))
		tg3_flag_set(tp, 5705_PLUS);
}

static bool tg3_10_100_only_device(struct tg3 *tp,
				   const struct pci_device_id *ent)
{
	u32 grc_misc_cfg = tr32(GRC_MISC_CFG) & GRC_MISC_CFG_BOARD_ID_MASK;

	if ((tg3_asic_rev(tp) == ASIC_REV_5703 &&
	     (grc_misc_cfg == 0x8000 || grc_misc_cfg == 0x4000)) ||
	    (tp->phy_flags & TG3_PHYFLG_IS_FET))
		return true;

	if (ent->driver_data & TG3_DRV_DATA_FLAG_10_100_ONLY) {
		if (tg3_asic_rev(tp) == ASIC_REV_5705) {
			if (ent->driver_data & TG3_DRV_DATA_FLAG_5705_10_100)
				return true;
		} else {
			return true;
		}
	}

	return false;
}

static int tg3_get_invariants(struct tg3 *tp, const struct pci_device_id *ent)
{
	u32 misc_ctrl_reg;
	u32 pci_state_reg, grc_misc_cfg;
	u32 val;
	u16 pci_cmd;
	int err;

	/* Force memory write invalidate off.  If we leave it on,
	 * then on 5700_BX chips we have to enable a workaround.
	 * The workaround is to set the TG3PCI_DMA_RW_CTRL boundary
	 * to match the cacheline size.  The Broadcom driver have this
	 * workaround but turns MWI off all the times so never uses
	 * it.  This seems to suggest that the workaround is insufficient.
	 */
	pci_read_config_word(tp->pdev, PCI_COMMAND, &pci_cmd);
	pci_cmd &= ~PCI_COMMAND_INVALIDATE;
	pci_write_config_word(tp->pdev, PCI_COMMAND, pci_cmd);

	/* Important! -- Make sure register accesses are byteswapped
	 * correctly.  Also, for those chips that require it, make
	 * sure that indirect register accesses are enabled before
	 * the first operation.
	 */
	pci_read_config_dword(tp->pdev, TG3PCI_MISC_HOST_CTRL,
			      &misc_ctrl_reg);
	tp->misc_host_ctrl |= (misc_ctrl_reg &
			       MISC_HOST_CTRL_CHIPREV);
	pci_write_config_dword(tp->pdev, TG3PCI_MISC_HOST_CTRL,
			       tp->misc_host_ctrl);

	tg3_detect_asic_rev(tp, misc_ctrl_reg);

	/* If we have 5702/03 A1 or A2 on certain ICH chipsets,
	 * we need to disable memory and use config. cycles
	 * only to access all registers. The 5702/03 chips
	 * can mistakenly decode the special cycles from the
	 * ICH chipsets as memory write cycles, causing corruption
	 * of register and memory space. Only certain ICH bridges
	 * will drive special cycles with non-zero data during the
	 * address phase which can fall within the 5703's address
	 * range. This is not an ICH bug as the PCI spec allows
	 * non-zero address during special cycles. However, only
	 * these ICH bridges are known to drive non-zero addresses
	 * during special cycles.
	 *
	 * Since special cycles do not cross PCI bridges, we only
	 * enable this workaround if the 5703 is on the secondary
	 * bus of these ICH bridges.
	 */
	if ((tg3_chip_rev_id(tp) == CHIPREV_ID_5703_A1) ||
	    (tg3_chip_rev_id(tp) == CHIPREV_ID_5703_A2)) {
		static struct tg3_dev_id {
			u32	vendor;
			u32	device;
			u32	rev;
		} ich_chipsets[] = {
			{ PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82801AA_8,
			  PCI_ANY_ID },
			{ PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82801AB_8,
			  PCI_ANY_ID },
			{ PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82801BA_11,
			  0xa },
			{ PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82801BA_6,
			  PCI_ANY_ID },
			{ },
		};
		struct tg3_dev_id *pci_id = &ich_chipsets[0];
		struct pci_dev *bridge = NULL;

		while (pci_id->vendor != 0) {
			bridge = pci_get_device(pci_id->vendor, pci_id->device,
						bridge);
			if (!bridge) {
				pci_id++;
				continue;
			}
			if (pci_id->rev != PCI_ANY_ID) {
				if (bridge->revision > pci_id->rev)
					continue;
			}
			if (bridge->subordinate &&
			    (bridge->subordinate->number ==
			     tp->pdev->bus->number)) {
				tg3_flag_set(tp, ICH_WORKAROUND);
				pci_dev_put(bridge);
				break;
			}
		}
	}

	if (tg3_asic_rev(tp) == ASIC_REV_5701) {
		static struct tg3_dev_id {
			u32	vendor;
			u32	device;
		} bridge_chipsets[] = {
			{ PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_PXH_0 },
			{ PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_PXH_1 },
			{ },
		};
		struct tg3_dev_id *pci_id = &bridge_chipsets[0];
		struct pci_dev *bridge = NULL;

		while (pci_id->vendor != 0) {
			bridge = pci_get_device(pci_id->vendor,
						pci_id->device,
						bridge);
			if (!bridge) {
				pci_id++;
				continue;
			}
			if (bridge->subordinate &&
			    (bridge->subordinate->number <=
			     tp->pdev->bus->number) &&
			    (bridge->subordinate->busn_res.end >=
			     tp->pdev->bus->number)) {
				tg3_flag_set(tp, 5701_DMA_BUG);
				pci_dev_put(bridge);
				break;
			}
		}
	}

	/* The EPB bridge inside 5714, 5715, and 5780 cannot support
	 * DMA addresses > 40-bit. This bridge may have other additional
	 * 57xx devices behind it in some 4-port NIC designs for example.
	 * Any tg3 device found behind the bridge will also need the 40-bit
	 * DMA workaround.
	 */
	if (tg3_flag(tp, 5780_CLASS)) {
		tg3_flag_set(tp, 40BIT_DMA_BUG);
		tp->msi_cap = pci_find_capability(tp->pdev, PCI_CAP_ID_MSI);
	} else {
		struct pci_dev *bridge = NULL;

		do {
			bridge = pci_get_device(PCI_VENDOR_ID_SERVERWORKS,
						PCI_DEVICE_ID_SERVERWORKS_EPB,
						bridge);
			if (bridge && bridge->subordinate &&
			    (bridge->subordinate->number <=
			     tp->pdev->bus->number) &&
			    (bridge->subordinate->busn_res.end >=
			     tp->pdev->bus->number)) {
				tg3_flag_set(tp, 40BIT_DMA_BUG);
				pci_dev_put(bridge);
				break;
			}
		} while (bridge);
	}

	if (tg3_asic_rev(tp) == ASIC_REV_5704 ||
	    tg3_asic_rev(tp) == ASIC_REV_5714)
		tp->pdev_peer = tg3_find_peer(tp);

	/* Determine TSO capabilities */
	if (tg3_chip_rev_id(tp) == CHIPREV_ID_5719_A0)
		; /* Do nothing. HW bug. */
	else if (tg3_flag(tp, 57765_PLUS))
		tg3_flag_set(tp, HW_TSO_3);
	else if (tg3_flag(tp, 5755_PLUS) ||
		 tg3_asic_rev(tp) == ASIC_REV_5906)
		tg3_flag_set(tp, HW_TSO_2);
	else if (tg3_flag(tp, 5750_PLUS)) {
		tg3_flag_set(tp, HW_TSO_1);
		tg3_flag_set(tp, TSO_BUG);
		if (tg3_asic_rev(tp) == ASIC_REV_5750 &&
		    tg3_chip_rev_id(tp) >= CHIPREV_ID_5750_C2)
			tg3_flag_clear(tp, TSO_BUG);
	} else if (tg3_asic_rev(tp) != ASIC_REV_5700 &&
		   tg3_asic_rev(tp) != ASIC_REV_5701 &&
		   tg3_chip_rev_id(tp) != CHIPREV_ID_5705_A0) {
		tg3_flag_set(tp, FW_TSO);
		tg3_flag_set(tp, TSO_BUG);
		if (tg3_asic_rev(tp) == ASIC_REV_5705)
			tp->fw_needed = FIRMWARE_TG3TSO5;
		else
			tp->fw_needed = FIRMWARE_TG3TSO;
	}

	/* Selectively allow TSO based on operating conditions */
	if (tg3_flag(tp, HW_TSO_1) ||
	    tg3_flag(tp, HW_TSO_2) ||
	    tg3_flag(tp, HW_TSO_3) ||
	    tg3_flag(tp, FW_TSO)) {
		/* For firmware TSO, assume ASF is disabled.
		 * We'll disable TSO later if we discover ASF
		 * is enabled in tg3_get_eeprom_hw_cfg().
		 */
		tg3_flag_set(tp, TSO_CAPABLE);
	} else {
		tg3_flag_clear(tp, TSO_CAPABLE);
		tg3_flag_clear(tp, TSO_BUG);
		tp->fw_needed = NULL;
	}

	if (tg3_chip_rev_id(tp) == CHIPREV_ID_5701_A0)
		tp->fw_needed = FIRMWARE_TG3;

	if (tg3_asic_rev(tp) == ASIC_REV_57766)
		tp->fw_needed = FIRMWARE_TG357766;

	tp->irq_max = 1;

	if (tg3_flag(tp, 5750_PLUS)) {
		tg3_flag_set(tp, SUPPORT_MSI);
		if (tg3_chip_rev(tp) == CHIPREV_5750_AX ||
		    tg3_chip_rev(tp) == CHIPREV_5750_BX ||
		    (tg3_asic_rev(tp) == ASIC_REV_5714 &&
		     tg3_chip_rev_id(tp) <= CHIPREV_ID_5714_A2 &&
		     tp->pdev_peer == tp->pdev))
			tg3_flag_clear(tp, SUPPORT_MSI);

		if (tg3_flag(tp, 5755_PLUS) ||
		    tg3_asic_rev(tp) == ASIC_REV_5906) {
			tg3_flag_set(tp, 1SHOT_MSI);
		}

		if (tg3_flag(tp, 57765_PLUS)) {
			tg3_flag_set(tp, SUPPORT_MSIX);
			tp->irq_max = TG3_IRQ_MAX_VECS;
		}
	}

	tp->txq_max = 1;
	tp->rxq_max = 1;
	if (tp->irq_max > 1) {
		tp->rxq_max = TG3_RSS_MAX_NUM_QS;
		tg3_rss_init_dflt_indir_tbl(tp, TG3_RSS_MAX_NUM_QS);

		if (tg3_asic_rev(tp) == ASIC_REV_5719 ||
		    tg3_asic_rev(tp) == ASIC_REV_5720)
			tp->txq_max = tp->irq_max - 1;
	}

	if (tg3_flag(tp, 5755_PLUS) ||
	    tg3_asic_rev(tp) == ASIC_REV_5906)
		tg3_flag_set(tp, SHORT_DMA_BUG);

	if (tg3_asic_rev(tp) == ASIC_REV_5719)
		tp->dma_limit = TG3_TX_BD_DMA_MAX_4K;

	if (tg3_asic_rev(tp) == ASIC_REV_5717 ||
	    tg3_asic_rev(tp) == ASIC_REV_5719 ||
	    tg3_asic_rev(tp) == ASIC_REV_5720 ||
	    tg3_asic_rev(tp) == ASIC_REV_5762)
		tg3_flag_set(tp, LRG_PROD_RING_CAP);

	if (tg3_flag(tp, 57765_PLUS) &&
	    tg3_chip_rev_id(tp) != CHIPREV_ID_5719_A0)
		tg3_flag_set(tp, USE_JUMBO_BDFLAG);

	if (!tg3_flag(tp, 5705_PLUS) ||
	    tg3_flag(tp, 5780_CLASS) ||
	    tg3_flag(tp, USE_JUMBO_BDFLAG))
		tg3_flag_set(tp, JUMBO_CAPABLE);

	pci_read_config_dword(tp->pdev, TG3PCI_PCISTATE,
			      &pci_state_reg);

	if (pci_is_pcie(tp->pdev)) {
		u16 lnkctl;

		tg3_flag_set(tp, PCI_EXPRESS);

		pcie_capability_read_word(tp->pdev, PCI_EXP_LNKCTL, &lnkctl);
		if (lnkctl & PCI_EXP_LNKCTL_CLKREQ_EN) {
			if (tg3_asic_rev(tp) == ASIC_REV_5906) {
				tg3_flag_clear(tp, HW_TSO_2);
				tg3_flag_clear(tp, TSO_CAPABLE);
			}
			if (tg3_asic_rev(tp) == ASIC_REV_5784 ||
			    tg3_asic_rev(tp) == ASIC_REV_5761 ||
			    tg3_chip_rev_id(tp) == CHIPREV_ID_57780_A0 ||
			    tg3_chip_rev_id(tp) == CHIPREV_ID_57780_A1)
				tg3_flag_set(tp, CLKREQ_BUG);
		} else if (tg3_chip_rev_id(tp) == CHIPREV_ID_5717_A0) {
			tg3_flag_set(tp, L1PLLPD_EN);
		}
	} else if (tg3_asic_rev(tp) == ASIC_REV_5785) {
		/* BCM5785 devices are effectively PCIe devices, and should
		 * follow PCIe codepaths, but do not have a PCIe capabilities
		 * section.
		 */
		tg3_flag_set(tp, PCI_EXPRESS);
	} else if (!tg3_flag(tp, 5705_PLUS) ||
		   tg3_flag(tp, 5780_CLASS)) {
		tp->pcix_cap = pci_find_capability(tp->pdev, PCI_CAP_ID_PCIX);
		if (!tp->pcix_cap) {
			dev_err(&tp->pdev->dev,
				"Cannot find PCI-X capability, aborting\n");
			return -EIO;
		}

		if (!(pci_state_reg & PCISTATE_CONV_PCI_MODE))
			tg3_flag_set(tp, PCIX_MODE);
	}

	/* If we have an AMD 762 or VIA K8T800 chipset, write
	 * reordering to the mailbox registers done by the host
	 * controller can cause major troubles.  We read back from
	 * every mailbox register write to force the writes to be
	 * posted to the chip in order.
	 */
	if (pci_dev_present(tg3_write_reorder_chipsets) &&
	    !tg3_flag(tp, PCI_EXPRESS))
		tg3_flag_set(tp, MBOX_WRITE_REORDER);

	pci_read_config_byte(tp->pdev, PCI_CACHE_LINE_SIZE,
			     &tp->pci_cacheline_sz);
	pci_read_config_byte(tp->pdev, PCI_LATENCY_TIMER,
			     &tp->pci_lat_timer);
	if (tg3_asic_rev(tp) == ASIC_REV_5703 &&
	    tp->pci_lat_timer < 64) {
		tp->pci_lat_timer = 64;
		pci_write_config_byte(tp->pdev, PCI_LATENCY_TIMER,
				      tp->pci_lat_timer);
	}

	/* Important! -- It is critical that the PCI-X hw workaround
	 * situation is decided before the first MMIO register access.
	 */
	if (tg3_chip_rev(tp) == CHIPREV_5700_BX) {
		/* 5700 BX chips need to have their TX producer index
		 * mailboxes written twice to workaround a bug.
		 */
		tg3_flag_set(tp, TXD_MBOX_HWBUG);

		/* If we are in PCI-X mode, enable register write workaround.
		 *
		 * The workaround is to use indirect register accesses
		 * for all chip writes not to mailbox registers.
		 */
		if (tg3_flag(tp, PCIX_MODE)) {
			u32 pm_reg;

			tg3_flag_set(tp, PCIX_TARGET_HWBUG);

			/* The chip can have it's power management PCI config
			 * space registers clobbered due to this bug.
			 * So explicitly force the chip into D0 here.
			 */
			pci_read_config_dword(tp->pdev,
					      tp->pm_cap + PCI_PM_CTRL,
					      &pm_reg);
			pm_reg &= ~PCI_PM_CTRL_STATE_MASK;
			pm_reg |= PCI_PM_CTRL_PME_ENABLE | 0 /* D0 */;
			pci_write_config_dword(tp->pdev,
					       tp->pm_cap + PCI_PM_CTRL,
					       pm_reg);

			/* Also, force SERR#/PERR# in PCI command. */
			pci_read_config_word(tp->pdev, PCI_COMMAND, &pci_cmd);
			pci_cmd |= PCI_COMMAND_PARITY | PCI_COMMAND_SERR;
			pci_write_config_word(tp->pdev, PCI_COMMAND, pci_cmd);
		}
	}

	if ((pci_state_reg & PCISTATE_BUS_SPEED_HIGH) != 0)
		tg3_flag_set(tp, PCI_HIGH_SPEED);
	if ((pci_state_reg & PCISTATE_BUS_32BIT) != 0)
		tg3_flag_set(tp, PCI_32BIT);

	/* Chip-specific fixup from Broadcom driver */
	if ((tg3_chip_rev_id(tp) == CHIPREV_ID_5704_A0) &&
	    (!(pci_state_reg & PCISTATE_RETRY_SAME_DMA))) {
		pci_state_reg |= PCISTATE_RETRY_SAME_DMA;
		pci_write_config_dword(tp->pdev, TG3PCI_PCISTATE, pci_state_reg);
	}

	/* Default fast path register access methods */
	tp->read32 = tg3_read32;
	tp->write32 = tg3_write32;
	tp->read32_mbox = tg3_read32;
	tp->write32_mbox = tg3_write32;
	tp->write32_tx_mbox = tg3_write32;
	tp->write32_rx_mbox = tg3_write32;

	/* Various workaround register access methods */
	if (tg3_flag(tp, PCIX_TARGET_HWBUG))
		tp->write32 = tg3_write_indirect_reg32;
	else if (tg3_asic_rev(tp) == ASIC_REV_5701 ||
		 (tg3_flag(tp, PCI_EXPRESS) &&
		  tg3_chip_rev_id(tp) == CHIPREV_ID_5750_A0)) {
		/*
		 * Back to back register writes can cause problems on these
		 * chips, the workaround is to read back all reg writes
		 * except those to mailbox regs.
		 *
		 * See tg3_write_indirect_reg32().
		 */
		tp->write32 = tg3_write_flush_reg32;
	}

	if (tg3_flag(tp, TXD_MBOX_HWBUG) || tg3_flag(tp, MBOX_WRITE_REORDER)) {
		tp->write32_tx_mbox = tg3_write32_tx_mbox;
		if (tg3_flag(tp, MBOX_WRITE_REORDER))
			tp->write32_rx_mbox = tg3_write_flush_reg32;
	}

	if (tg3_flag(tp, ICH_WORKAROUND)) {
		tp->read32 = tg3_read_indirect_reg32;
		tp->write32 = tg3_write_indirect_reg32;
		tp->read32_mbox = tg3_read_indirect_mbox;
		tp->write32_mbox = tg3_write_indirect_mbox;
		tp->write32_tx_mbox = tg3_write_indirect_mbox;
		tp->write32_rx_mbox = tg3_write_indirect_mbox;

		iounmap(tp->regs);
		tp->regs = NULL;

		pci_read_config_word(tp->pdev, PCI_COMMAND, &pci_cmd);
		pci_cmd &= ~PCI_COMMAND_MEMORY;
		pci_write_config_word(tp->pdev, PCI_COMMAND, pci_cmd);
	}
	if (tg3_asic_rev(tp) == ASIC_REV_5906) {
		tp->read32_mbox = tg3_read32_mbox_5906;
		tp->write32_mbox = tg3_write32_mbox_5906;
		tp->write32_tx_mbox = tg3_write32_mbox_5906;
		tp->write32_rx_mbox = tg3_write32_mbox_5906;
	}

	if (tp->write32 == tg3_write_indirect_reg32 ||
	    (tg3_flag(tp, PCIX_MODE) &&
	     (tg3_asic_rev(tp) == ASIC_REV_5700 ||
	      tg3_asic_rev(tp) == ASIC_REV_5701)))
		tg3_flag_set(tp, SRAM_USE_CONFIG);

	/* The memory arbiter has to be enabled in order for SRAM accesses
	 * to succeed.  Normally on powerup the tg3 chip firmware will make
	 * sure it is enabled, but other entities such as system netboot
	 * code might disable it.
	 */
	val = tr32(MEMARB_MODE);
	tw32(MEMARB_MODE, val | MEMARB_MODE_ENABLE);

	tp->pci_fn = PCI_FUNC(tp->pdev->devfn) & 3;
	if (tg3_asic_rev(tp) == ASIC_REV_5704 ||
	    tg3_flag(tp, 5780_CLASS)) {
		if (tg3_flag(tp, PCIX_MODE)) {
			pci_read_config_dword(tp->pdev,
					      tp->pcix_cap + PCI_X_STATUS,
					      &val);
			tp->pci_fn = val & 0x7;
		}
	} else if (tg3_asic_rev(tp) == ASIC_REV_5717 ||
		   tg3_asic_rev(tp) == ASIC_REV_5719 ||
		   tg3_asic_rev(tp) == ASIC_REV_5720) {
		tg3_read_mem(tp, NIC_SRAM_CPMU_STATUS, &val);
		if ((val & NIC_SRAM_CPMUSTAT_SIG_MSK) != NIC_SRAM_CPMUSTAT_SIG)
			val = tr32(TG3_CPMU_STATUS);

		if (tg3_asic_rev(tp) == ASIC_REV_5717)
			tp->pci_fn = (val & TG3_CPMU_STATUS_FMSK_5717) ? 1 : 0;
		else
			tp->pci_fn = (val & TG3_CPMU_STATUS_FMSK_5719) >>
				     TG3_CPMU_STATUS_FSHFT_5719;
	}

	if (tg3_flag(tp, FLUSH_POSTED_WRITES)) {
		tp->write32_tx_mbox = tg3_write_flush_reg32;
		tp->write32_rx_mbox = tg3_write_flush_reg32;
	}

	/* Get eeprom hw config before calling tg3_set_power_state().
	 * In particular, the TG3_FLAG_IS_NIC flag must be
	 * determined before calling tg3_set_power_state() so that
	 * we know whether or not to switch out of Vaux power.
	 * When the flag is set, it means that GPIO1 is used for eeprom
	 * write protect and also implies that it is a LOM where GPIOs
	 * are not used to switch power.
	 */
	tg3_get_eeprom_hw_cfg(tp);

	if (tg3_flag(tp, FW_TSO) && tg3_flag(tp, ENABLE_ASF)) {
		tg3_flag_clear(tp, TSO_CAPABLE);
		tg3_flag_clear(tp, TSO_BUG);
		tp->fw_needed = NULL;
	}

	if (tg3_flag(tp, ENABLE_APE)) {
		/* Allow reads and writes to the
		 * APE register and memory space.
		 */
		pci_state_reg |= PCISTATE_ALLOW_APE_CTLSPC_WR |
				 PCISTATE_ALLOW_APE_SHMEM_WR |
				 PCISTATE_ALLOW_APE_PSPACE_WR;
		pci_write_config_dword(tp->pdev, TG3PCI_PCISTATE,
				       pci_state_reg);

		tg3_ape_lock_init(tp);
	}

	/* Set up tp->grc_local_ctrl before calling
	 * tg3_pwrsrc_switch_to_vmain().  GPIO1 driven high
	 * will bring 5700's external PHY out of reset.
	 * It is also used as eeprom write protect on LOMs.
	 */
	tp->grc_local_ctrl = GRC_LCLCTRL_INT_ON_ATTN | GRC_LCLCTRL_AUTO_SEEPROM;
	if (tg3_asic_rev(tp) == ASIC_REV_5700 ||
	    tg3_flag(tp, EEPROM_WRITE_PROT))
		tp->grc_local_ctrl |= (GRC_LCLCTRL_GPIO_OE1 |
				       GRC_LCLCTRL_GPIO_OUTPUT1);
	/* Unused GPIO3 must be driven as output on 5752 because there
	 * are no pull-up resistors on unused GPIO pins.
	 */
	else if (tg3_asic_rev(tp) == ASIC_REV_5752)
		tp->grc_local_ctrl |= GRC_LCLCTRL_GPIO_OE3;

	if (tg3_asic_rev(tp) == ASIC_REV_5755 ||
	    tg3_asic_rev(tp) == ASIC_REV_57780 ||
	    tg3_flag(tp, 57765_CLASS))
		tp->grc_local_ctrl |= GRC_LCLCTRL_GPIO_UART_SEL;

	if (tp->pdev->device == PCI_DEVICE_ID_TIGON3_5761 ||
	    tp->pdev->device == TG3PCI_DEVICE_TIGON3_5761S) {
		/* Turn off the debug UART. */
		tp->grc_local_ctrl |= GRC_LCLCTRL_GPIO_UART_SEL;
		if (tg3_flag(tp, IS_NIC))
			/* Keep VMain power. */
			tp->grc_local_ctrl |= GRC_LCLCTRL_GPIO_OE0 |
					      GRC_LCLCTRL_GPIO_OUTPUT0;
	}

	if (tg3_asic_rev(tp) == ASIC_REV_5762)
		tp->grc_local_ctrl |=
			tr32(GRC_LOCAL_CTRL) & GRC_LCLCTRL_GPIO_UART_SEL;

	/* Switch out of Vaux if it is a NIC */
	tg3_pwrsrc_switch_to_vmain(tp);

	/* Derive initial jumbo mode from MTU assigned in
	 * ether_setup() via the alloc_etherdev() call
	 */
	if (tp->dev->mtu > ETH_DATA_LEN && !tg3_flag(tp, 5780_CLASS))
		tg3_flag_set(tp, JUMBO_RING_ENABLE);

	/* Determine WakeOnLan speed to use. */
	if (tg3_asic_rev(tp) == ASIC_REV_5700 ||
	    tg3_chip_rev_id(tp) == CHIPREV_ID_5701_A0 ||
	    tg3_chip_rev_id(tp) == CHIPREV_ID_5701_B0 ||
	    tg3_chip_rev_id(tp) == CHIPREV_ID_5701_B2) {
		tg3_flag_clear(tp, WOL_SPEED_100MB);
	} else {
		tg3_flag_set(tp, WOL_SPEED_100MB);
	}

	if (tg3_asic_rev(tp) == ASIC_REV_5906)
		tp->phy_flags |= TG3_PHYFLG_IS_FET;

	/* A few boards don't want Ethernet@WireSpeed phy feature */
	if (tg3_asic_rev(tp) == ASIC_REV_5700 ||
	    (tg3_asic_rev(tp) == ASIC_REV_5705 &&
	     (tg3_chip_rev_id(tp) != CHIPREV_ID_5705_A0) &&
	     (tg3_chip_rev_id(tp) != CHIPREV_ID_5705_A1)) ||
	    (tp->phy_flags & TG3_PHYFLG_IS_FET) ||
	    (tp->phy_flags & TG3_PHYFLG_ANY_SERDES))
		tp->phy_flags |= TG3_PHYFLG_NO_ETH_WIRE_SPEED;

	if (tg3_chip_rev(tp) == CHIPREV_5703_AX ||
	    tg3_chip_rev(tp) == CHIPREV_5704_AX)
		tp->phy_flags |= TG3_PHYFLG_ADC_BUG;
	if (tg3_chip_rev_id(tp) == CHIPREV_ID_5704_A0)
		tp->phy_flags |= TG3_PHYFLG_5704_A0_BUG;

	if (tg3_flag(tp, 5705_PLUS) &&
	    !(tp->phy_flags & TG3_PHYFLG_IS_FET) &&
	    tg3_asic_rev(tp) != ASIC_REV_5785 &&
	    tg3_asic_rev(tp) != ASIC_REV_57780 &&
	    !tg3_flag(tp, 57765_PLUS)) {
		if (tg3_asic_rev(tp) == ASIC_REV_5755 ||
		    tg3_asic_rev(tp) == ASIC_REV_5787 ||
		    tg3_asic_rev(tp) == ASIC_REV_5784 ||
		    tg3_asic_rev(tp) == ASIC_REV_5761) {
			if (tp->pdev->device != PCI_DEVICE_ID_TIGON3_5756 &&
			    tp->pdev->device != PCI_DEVICE_ID_TIGON3_5722)
				tp->phy_flags |= TG3_PHYFLG_JITTER_BUG;
			if (tp->pdev->device == PCI_DEVICE_ID_TIGON3_5755M)
				tp->phy_flags |= TG3_PHYFLG_ADJUST_TRIM;
		} else
			tp->phy_flags |= TG3_PHYFLG_BER_BUG;
	}

	if (tg3_asic_rev(tp) == ASIC_REV_5784 &&
	    tg3_chip_rev(tp) != CHIPREV_5784_AX) {
		tp->phy_otp = tg3_read_otp_phycfg(tp);
		if (tp->phy_otp == 0)
			tp->phy_otp = TG3_OTP_DEFAULT;
	}

	if (tg3_flag(tp, CPMU_PRESENT))
		tp->mi_mode = MAC_MI_MODE_500KHZ_CONST;
	else
		tp->mi_mode = MAC_MI_MODE_BASE;

	tp->coalesce_mode = 0;
	if (tg3_chip_rev(tp) != CHIPREV_5700_AX &&
	    tg3_chip_rev(tp) != CHIPREV_5700_BX)
		tp->coalesce_mode |= HOSTCC_MODE_32BYTE;

	/* Set these bits to enable statistics workaround. */
	if (tg3_asic_rev(tp) == ASIC_REV_5717 ||
	    tg3_chip_rev_id(tp) == CHIPREV_ID_5719_A0 ||
	    tg3_chip_rev_id(tp) == CHIPREV_ID_5720_A0) {
		tp->coalesce_mode |= HOSTCC_MODE_ATTN;
		tp->grc_mode |= GRC_MODE_IRQ_ON_FLOW_ATTN;
	}

	if (tg3_asic_rev(tp) == ASIC_REV_5785 ||
	    tg3_asic_rev(tp) == ASIC_REV_57780)
		tg3_flag_set(tp, USE_PHYLIB);

	err = tg3_mdio_init(tp);
	if (err)
		return err;

	/* Initialize data/descriptor byte/word swapping. */
	val = tr32(GRC_MODE);
	if (tg3_asic_rev(tp) == ASIC_REV_5720 ||
	    tg3_asic_rev(tp) == ASIC_REV_5762)
		val &= (GRC_MODE_BYTE_SWAP_B2HRX_DATA |
			GRC_MODE_WORD_SWAP_B2HRX_DATA |
			GRC_MODE_B2HRX_ENABLE |
			GRC_MODE_HTX2B_ENABLE |
			GRC_MODE_HOST_STACKUP);
	else
		val &= GRC_MODE_HOST_STACKUP;

	tw32(GRC_MODE, val | tp->grc_mode);

	tg3_switch_clocks(tp);

	/* Clear this out for sanity. */
	tw32(TG3PCI_MEM_WIN_BASE_ADDR, 0);

	/* Clear TG3PCI_REG_BASE_ADDR to prevent hangs. */
	tw32(TG3PCI_REG_BASE_ADDR, 0);

	pci_read_config_dword(tp->pdev, TG3PCI_PCISTATE,
			      &pci_state_reg);
	if ((pci_state_reg & PCISTATE_CONV_PCI_MODE) == 0 &&
	    !tg3_flag(tp, PCIX_TARGET_HWBUG)) {
		if (tg3_chip_rev_id(tp) == CHIPREV_ID_5701_A0 ||
		    tg3_chip_rev_id(tp) == CHIPREV_ID_5701_B0 ||
		    tg3_chip_rev_id(tp) == CHIPREV_ID_5701_B2 ||
		    tg3_chip_rev_id(tp) == CHIPREV_ID_5701_B5) {
			void __iomem *sram_base;

			/* Write some dummy words into the SRAM status block
			 * area, see if it reads back correctly.  If the return
			 * value is bad, force enable the PCIX workaround.
			 */
			sram_base = tp->regs + NIC_SRAM_WIN_BASE + NIC_SRAM_STATS_BLK;

			writel(0x00000000, sram_base);
			writel(0x00000000, sram_base + 4);
			writel(0xffffffff, sram_base + 4);
			if (readl(sram_base) != 0x00000000)
				tg3_flag_set(tp, PCIX_TARGET_HWBUG);
		}
	}

	udelay(50);
	tg3_nvram_init(tp);

	/* If the device has an NVRAM, no need to load patch firmware */
	if (tg3_asic_rev(tp) == ASIC_REV_57766 &&
	    !tg3_flag(tp, NO_NVRAM))
		tp->fw_needed = NULL;

	grc_misc_cfg = tr32(GRC_MISC_CFG);
	grc_misc_cfg &= GRC_MISC_CFG_BOARD_ID_MASK;

	if (tg3_asic_rev(tp) == ASIC_REV_5705 &&
	    (grc_misc_cfg == GRC_MISC_CFG_BOARD_ID_5788 ||
	     grc_misc_cfg == GRC_MISC_CFG_BOARD_ID_5788M))
		tg3_flag_set(tp, IS_5788);

	if (!tg3_flag(tp, IS_5788) &&
	    tg3_asic_rev(tp) != ASIC_REV_5700)
		tg3_flag_set(tp, TAGGED_STATUS);
	if (tg3_flag(tp, TAGGED_STATUS)) {
		tp->coalesce_mode |= (HOSTCC_MODE_CLRTICK_RXBD |
				      HOSTCC_MODE_CLRTICK_TXBD);

		tp->misc_host_ctrl |= MISC_HOST_CTRL_TAGGED_STATUS;
		pci_write_config_dword(tp->pdev, TG3PCI_MISC_HOST_CTRL,
				       tp->misc_host_ctrl);
	}

	/* Preserve the APE MAC_MODE bits */
	if (tg3_flag(tp, ENABLE_APE))
		tp->mac_mode = MAC_MODE_APE_TX_EN | MAC_MODE_APE_RX_EN;
	else
		tp->mac_mode = 0;

	if (tg3_10_100_only_device(tp, ent))
		tp->phy_flags |= TG3_PHYFLG_10_100_ONLY;

	err = tg3_phy_probe(tp);
	if (err) {
		dev_err(&tp->pdev->dev, "phy probe failed, err %d\n", err);
		/* ... but do not return immediately ... */
		tg3_mdio_fini(tp);
	}

	tg3_read_vpd(tp);
	tg3_read_fw_ver(tp);

	if (tp->phy_flags & TG3_PHYFLG_PHY_SERDES) {
		tp->phy_flags &= ~TG3_PHYFLG_USE_MI_INTERRUPT;
	} else {
		if (tg3_asic_rev(tp) == ASIC_REV_5700)
			tp->phy_flags |= TG3_PHYFLG_USE_MI_INTERRUPT;
		else
			tp->phy_flags &= ~TG3_PHYFLG_USE_MI_INTERRUPT;
	}

	/* 5700 {AX,BX} chips have a broken status block link
	 * change bit implementation, so we must use the
	 * status register in those cases.
	 */
	if (tg3_asic_rev(tp) == ASIC_REV_5700)
		tg3_flag_set(tp, USE_LINKCHG_REG);
	else
		tg3_flag_clear(tp, USE_LINKCHG_REG);

	/* The led_ctrl is set during tg3_phy_probe, here we might
	 * have to force the link status polling mechanism based
	 * upon subsystem IDs.
	 */
	if (tp->pdev->subsystem_vendor == PCI_VENDOR_ID_DELL &&
	    tg3_asic_rev(tp) == ASIC_REV_5701 &&
	    !(tp->phy_flags & TG3_PHYFLG_PHY_SERDES)) {
		tp->phy_flags |= TG3_PHYFLG_USE_MI_INTERRUPT;
		tg3_flag_set(tp, USE_LINKCHG_REG);
	}

	/* For all SERDES we poll the MAC status register. */
	if (tp->phy_flags & TG3_PHYFLG_PHY_SERDES)
		tg3_flag_set(tp, POLL_SERDES);
	else
		tg3_flag_clear(tp, POLL_SERDES);

	tp->rx_offset = NET_SKB_PAD + NET_IP_ALIGN;
	tp->rx_copy_thresh = TG3_RX_COPY_THRESHOLD;
	if (tg3_asic_rev(tp) == ASIC_REV_5701 &&
	    tg3_flag(tp, PCIX_MODE)) {
		tp->rx_offset = NET_SKB_PAD;
#ifndef CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS
		tp->rx_copy_thresh = ~(u16)0;
#endif
	}

	tp->rx_std_ring_mask = TG3_RX_STD_RING_SIZE(tp) - 1;
	tp->rx_jmb_ring_mask = TG3_RX_JMB_RING_SIZE(tp) - 1;
	tp->rx_ret_ring_mask = tg3_rx_ret_ring_size(tp) - 1;

	tp->rx_std_max_post = tp->rx_std_ring_mask + 1;

	/* Increment the rx prod index on the rx std ring by at most
	 * 8 for these chips to workaround hw errata.
	 */
	if (tg3_asic_rev(tp) == ASIC_REV_5750 ||
	    tg3_asic_rev(tp) == ASIC_REV_5752 ||
	    tg3_asic_rev(tp) == ASIC_REV_5755)
		tp->rx_std_max_post = 8;

	if (tg3_flag(tp, ASPM_WORKAROUND))
		tp->pwrmgmt_thresh = tr32(PCIE_PWR_MGMT_THRESH) &
				     PCIE_PWR_MGMT_L1_THRESH_MSK;

	return err;
}

#ifdef CONFIG_SPARC
static int tg3_get_macaddr_sparc(struct tg3 *tp)
{
	struct net_device *dev = tp->dev;
	struct pci_dev *pdev = tp->pdev;
	struct device_node *dp = pci_device_to_OF_node(pdev);
	const unsigned char *addr;
	int len;

	addr = of_get_property(dp, "local-mac-address", &len);
	if (addr && len == 6) {
		memcpy(dev->dev_addr, addr, 6);
		return 0;
	}
	return -ENODEV;
}

static int tg3_get_default_macaddr_sparc(struct tg3 *tp)
{
	struct net_device *dev = tp->dev;

	memcpy(dev->dev_addr, idprom->id_ethaddr, 6);
	return 0;
}
#endif

static int tg3_get_device_address(struct tg3 *tp)
{
	struct net_device *dev = tp->dev;
	u32 hi, lo, mac_offset;
	int addr_ok = 0;
	int err;

#ifdef CONFIG_SPARC
	if (!tg3_get_macaddr_sparc(tp))
		return 0;
#endif

	if (tg3_flag(tp, IS_SSB_CORE)) {
		err = ssb_gige_get_macaddr(tp->pdev, &dev->dev_addr[0]);
		if (!err && is_valid_ether_addr(&dev->dev_addr[0]))
			return 0;
	}

	mac_offset = 0x7c;
	if (tg3_asic_rev(tp) == ASIC_REV_5704 ||
	    tg3_flag(tp, 5780_CLASS)) {
		if (tr32(TG3PCI_DUAL_MAC_CTRL) & DUAL_MAC_CTRL_ID)
			mac_offset = 0xcc;
		if (tg3_nvram_lock(tp))
			tw32_f(NVRAM_CMD, NVRAM_CMD_RESET);
		else
			tg3_nvram_unlock(tp);
	} else if (tg3_flag(tp, 5717_PLUS)) {
		if (tp->pci_fn & 1)
			mac_offset = 0xcc;
		if (tp->pci_fn > 1)
			mac_offset += 0x18c;
	} else if (tg3_asic_rev(tp) == ASIC_REV_5906)
		mac_offset = 0x10;

	/* First try to get it from MAC address mailbox. */
	tg3_read_mem(tp, NIC_SRAM_MAC_ADDR_HIGH_MBOX, &hi);
	if ((hi >> 16) == 0x484b) {
		dev->dev_addr[0] = (hi >>  8) & 0xff;
		dev->dev_addr[1] = (hi >>  0) & 0xff;

		tg3_read_mem(tp, NIC_SRAM_MAC_ADDR_LOW_MBOX, &lo);
		dev->dev_addr[2] = (lo >> 24) & 0xff;
		dev->dev_addr[3] = (lo >> 16) & 0xff;
		dev->dev_addr[4] = (lo >>  8) & 0xff;
		dev->dev_addr[5] = (lo >>  0) & 0xff;

		/* Some old bootcode may report a 0 MAC address in SRAM */
		addr_ok = is_valid_ether_addr(&dev->dev_addr[0]);
	}
	if (!addr_ok) {
		/* Next, try NVRAM. */
		if (!tg3_flag(tp, NO_NVRAM) &&
		    !tg3_nvram_read_be32(tp, mac_offset + 0, &hi) &&
		    !tg3_nvram_read_be32(tp, mac_offset + 4, &lo)) {
			memcpy(&dev->dev_addr[0], ((char *)&hi) + 2, 2);
			memcpy(&dev->dev_addr[2], (char *)&lo, sizeof(lo));
		}
		/* Finally just fetch it out of the MAC control regs. */
		else {
			hi = tr32(MAC_ADDR_0_HIGH);
			lo = tr32(MAC_ADDR_0_LOW);

			dev->dev_addr[5] = lo & 0xff;
			dev->dev_addr[4] = (lo >> 8) & 0xff;
			dev->dev_addr[3] = (lo >> 16) & 0xff;
			dev->dev_addr[2] = (lo >> 24) & 0xff;
			dev->dev_addr[1] = hi & 0xff;
			dev->dev_addr[0] = (hi >> 8) & 0xff;
		}
	}

	if (!is_valid_ether_addr(&dev->dev_addr[0])) {
#ifdef CONFIG_SPARC
		if (!tg3_get_default_macaddr_sparc(tp))
			return 0;
#endif
		return -EINVAL;
	}
	return 0;
}

#define BOUNDARY_SINGLE_CACHELINE	1
#define BOUNDARY_MULTI_CACHELINE	2

static u32 tg3_calc_dma_bndry(struct tg3 *tp, u32 val)
{
	int cacheline_size;
	u8 byte;
	int goal;

	pci_read_config_byte(tp->pdev, PCI_CACHE_LINE_SIZE, &byte);
	if (byte == 0)
		cacheline_size = 1024;
	else
		cacheline_size = (int) byte * 4;

	/* On 5703 and later chips, the boundary bits have no
	 * effect.
	 */
	if (tg3_asic_rev(tp) != ASIC_REV_5700 &&
	    tg3_asic_rev(tp) != ASIC_REV_5701 &&
	    !tg3_flag(tp, PCI_EXPRESS))
		goto out;

#if defined(CONFIG_PPC64) || defined(CONFIG_IA64) || defined(CONFIG_PARISC)
	goal = BOUNDARY_MULTI_CACHELINE;
#else
#if defined(CONFIG_SPARC64) || defined(CONFIG_ALPHA)
	goal = BOUNDARY_SINGLE_CACHELINE;
#else
	goal = 0;
#endif
#endif

	if (tg3_flag(tp, 57765_PLUS)) {
		val = goal ? 0 : DMA_RWCTRL_DIS_CACHE_ALIGNMENT;
		goto out;
	}

	if (!goal)
		goto out;

	/* PCI controllers on most RISC systems tend to disconnect
	 * when a device tries to burst across a cache-line boundary.
	 * Therefore, letting tg3 do so just wastes PCI bandwidth.
	 *
	 * Unfortunately, for PCI-E there are only limited
	 * write-side controls for this, and thus for reads
	 * we will still get the disconnects.  We'll also waste
	 * these PCI cycles for both read and write for chips
	 * other than 5700 and 5701 which do not implement the
	 * boundary bits.
	 */
	if (tg3_flag(tp, PCIX_MODE) && !tg3_flag(tp, PCI_EXPRESS)) {
		switch (cacheline_size) {
		case 16:
		case 32:
		case 64:
		case 128:
			if (goal == BOUNDARY_SINGLE_CACHELINE) {
				val |= (DMA_RWCTRL_READ_BNDRY_128_PCIX |
					DMA_RWCTRL_WRITE_BNDRY_128_PCIX);
			} else {
				val |= (DMA_RWCTRL_READ_BNDRY_384_PCIX |
					DMA_RWCTRL_WRITE_BNDRY_384_PCIX);
			}
			break;

		case 256:
			val |= (DMA_RWCTRL_READ_BNDRY_256_PCIX |
				DMA_RWCTRL_WRITE_BNDRY_256_PCIX);
			break;

		default:
			val |= (DMA_RWCTRL_READ_BNDRY_384_PCIX |
				DMA_RWCTRL_WRITE_BNDRY_384_PCIX);
			break;
		}
	} else if (tg3_flag(tp, PCI_EXPRESS)) {
		switch (cacheline_size) {
		case 16:
		case 32:
		case 64:
			if (goal == BOUNDARY_SINGLE_CACHELINE) {
				val &= ~DMA_RWCTRL_WRITE_BNDRY_DISAB_PCIE;
				val |= DMA_RWCTRL_WRITE_BNDRY_64_PCIE;
				break;
			}
			/* fallthrough */
		case 128:
		default:
			val &= ~DMA_RWCTRL_WRITE_BNDRY_DISAB_PCIE;
			val |= DMA_RWCTRL_WRITE_BNDRY_128_PCIE;
			break;
		}
	} else {
		switch (cacheline_size) {
		case 16:
			if (goal == BOUNDARY_SINGLE_CACHELINE) {
				val |= (DMA_RWCTRL_READ_BNDRY_16 |
					DMA_RWCTRL_WRITE_BNDRY_16);
				break;
			}
			/* fallthrough */
		case 32:
			if (goal == BOUNDARY_SINGLE_CACHELINE) {
				val |= (DMA_RWCTRL_READ_BNDRY_32 |
					DMA_RWCTRL_WRITE_BNDRY_32);
				break;
			}
			/* fallthrough */
		case 64:
			if (goal == BOUNDARY_SINGLE_CACHELINE) {
				val |= (DMA_RWCTRL_READ_BNDRY_64 |
					DMA_RWCTRL_WRITE_BNDRY_64);
				break;
			}
			/* fallthrough */
		case 128:
			if (goal == BOUNDARY_SINGLE_CACHELINE) {
				val |= (DMA_RWCTRL_READ_BNDRY_128 |
					DMA_RWCTRL_WRITE_BNDRY_128);
				break;
			}
			/* fallthrough */
		case 256:
			val |= (DMA_RWCTRL_READ_BNDRY_256 |
				DMA_RWCTRL_WRITE_BNDRY_256);
			break;
		case 512:
			val |= (DMA_RWCTRL_READ_BNDRY_512 |
				DMA_RWCTRL_WRITE_BNDRY_512);
			break;
		case 1024:
		default:
			val |= (DMA_RWCTRL_READ_BNDRY_1024 |
				DMA_RWCTRL_WRITE_BNDRY_1024);
			break;
		}
	}

out:
	return val;
}

static int tg3_do_test_dma(struct tg3 *tp, u32 *buf, dma_addr_t buf_dma,
			   int size, bool to_device)
{
	struct tg3_internal_buffer_desc test_desc;
	u32 sram_dma_descs;
	int i, ret;

	sram_dma_descs = NIC_SRAM_DMA_DESC_POOL_BASE;

	tw32(FTQ_RCVBD_COMP_FIFO_ENQDEQ, 0);
	tw32(FTQ_RCVDATA_COMP_FIFO_ENQDEQ, 0);
	tw32(RDMAC_STATUS, 0);
	tw32(WDMAC_STATUS, 0);

	tw32(BUFMGR_MODE, 0);
	tw32(FTQ_RESET, 0);

	test_desc.addr_hi = ((u64) buf_dma) >> 32;
	test_desc.addr_lo = buf_dma & 0xffffffff;
	test_desc.nic_mbuf = 0x00002100;
	test_desc.len = size;

	/*
	 * HP ZX1 was seeing test failures for 5701 cards running at 33Mhz
	 * the *second* time the tg3 driver was getting loaded after an
	 * initial scan.
	 *
	 * Broadcom tells me:
	 *   ...the DMA engine is connected to the GRC block and a DMA
	 *   reset may affect the GRC block in some unpredictable way...
	 *   The behavior of resets to individual blocks has not been tested.
	 *
	 * Broadcom noted the GRC reset will also reset all sub-components.
	 */
	if (to_device) {
		test_desc.cqid_sqid = (13 << 8) | 2;

		tw32_f(RDMAC_MODE, RDMAC_MODE_ENABLE);
		udelay(40);
	} else {
		test_desc.cqid_sqid = (16 << 8) | 7;

		tw32_f(WDMAC_MODE, WDMAC_MODE_ENABLE);
		udelay(40);
	}
	test_desc.flags = 0x00000005;

	for (i = 0; i < (sizeof(test_desc) / sizeof(u32)); i++) {
		u32 val;

		val = *(((u32 *)&test_desc) + i);
		pci_write_config_dword(tp->pdev, TG3PCI_MEM_WIN_BASE_ADDR,
				       sram_dma_descs + (i * sizeof(u32)));
		pci_write_config_dword(tp->pdev, TG3PCI_MEM_WIN_DATA, val);
	}
	pci_write_config_dword(tp->pdev, TG3PCI_MEM_WIN_BASE_ADDR, 0);

	if (to_device)
		tw32(FTQ_DMA_HIGH_READ_FIFO_ENQDEQ, sram_dma_descs);
	else
		tw32(FTQ_DMA_HIGH_WRITE_FIFO_ENQDEQ, sram_dma_descs);

	ret = -ENODEV;
	for (i = 0; i < 40; i++) {
		u32 val;

		if (to_device)
			val = tr32(FTQ_RCVBD_COMP_FIFO_ENQDEQ);
		else
			val = tr32(FTQ_RCVDATA_COMP_FIFO_ENQDEQ);
		if ((val & 0xffff) == sram_dma_descs) {
			ret = 0;
			break;
		}

		udelay(100);
	}

	return ret;
}

#define TEST_BUFFER_SIZE	0x2000

static DEFINE_PCI_DEVICE_TABLE(tg3_dma_wait_state_chipsets) = {
	{ PCI_DEVICE(PCI_VENDOR_ID_APPLE, PCI_DEVICE_ID_APPLE_UNI_N_PCI15) },
	{ },
};

static int tg3_test_dma(struct tg3 *tp)
{
	dma_addr_t buf_dma;
	u32 *buf, saved_dma_rwctrl;
	int ret = 0;

	buf = dma_alloc_coherent(&tp->pdev->dev, TEST_BUFFER_SIZE,
				 &buf_dma, GFP_KERNEL);
	if (!buf) {
		ret = -ENOMEM;
		goto out_nofree;
	}

	tp->dma_rwctrl = ((0x7 << DMA_RWCTRL_PCI_WRITE_CMD_SHIFT) |
			  (0x6 << DMA_RWCTRL_PCI_READ_CMD_SHIFT));

	tp->dma_rwctrl = tg3_calc_dma_bndry(tp, tp->dma_rwctrl);

	if (tg3_flag(tp, 57765_PLUS))
		goto out;

	if (tg3_flag(tp, PCI_EXPRESS)) {
		/* DMA read watermark not used on PCIE */
		tp->dma_rwctrl |= 0x00180000;
	} else if (!tg3_flag(tp, PCIX_MODE)) {
		if (tg3_asic_rev(tp) == ASIC_REV_5705 ||
		    tg3_asic_rev(tp) == ASIC_REV_5750)
			tp->dma_rwctrl |= 0x003f0000;
		else
			tp->dma_rwctrl |= 0x003f000f;
	} else {
		if (tg3_asic_rev(tp) == ASIC_REV_5703 ||
		    tg3_asic_rev(tp) == ASIC_REV_5704) {
			u32 ccval = (tr32(TG3PCI_CLOCK_CTRL) & 0x1f);
			u32 read_water = 0x7;

			/* If the 5704 is behind the EPB bridge, we can
			 * do the less restrictive ONE_DMA workaround for
			 * better performance.
			 */
			if (tg3_flag(tp, 40BIT_DMA_BUG) &&
			    tg3_asic_rev(tp) == ASIC_REV_5704)
				tp->dma_rwctrl |= 0x8000;
			else if (ccval == 0x6 || ccval == 0x7)
				tp->dma_rwctrl |= DMA_RWCTRL_ONE_DMA;

			if (tg3_asic_rev(tp) == ASIC_REV_5703)
				read_water = 4;
			/* Set bit 23 to enable PCIX hw bug fix */
			tp->dma_rwctrl |=
				(read_water << DMA_RWCTRL_READ_WATER_SHIFT) |
				(0x3 << DMA_RWCTRL_WRITE_WATER_SHIFT) |
				(1 << 23);
		} else if (tg3_asic_rev(tp) == ASIC_REV_5780) {
			/* 5780 always in PCIX mode */
			tp->dma_rwctrl |= 0x00144000;
		} else if (tg3_asic_rev(tp) == ASIC_REV_5714) {
			/* 5714 always in PCIX mode */
			tp->dma_rwctrl |= 0x00148000;
		} else {
			tp->dma_rwctrl |= 0x001b000f;
		}
	}
	if (tg3_flag(tp, ONE_DMA_AT_ONCE))
		tp->dma_rwctrl |= DMA_RWCTRL_ONE_DMA;

	if (tg3_asic_rev(tp) == ASIC_REV_5703 ||
	    tg3_asic_rev(tp) == ASIC_REV_5704)
		tp->dma_rwctrl &= 0xfffffff0;

	if (tg3_asic_rev(tp) == ASIC_REV_5700 ||
	    tg3_asic_rev(tp) == ASIC_REV_5701) {
		/* Remove this if it causes problems for some boards. */
		tp->dma_rwctrl |= DMA_RWCTRL_USE_MEM_READ_MULT;

		/* On 5700/5701 chips, we need to set this bit.
		 * Otherwise the chip will issue cacheline transactions
		 * to streamable DMA memory with not all the byte
		 * enables turned on.  This is an error on several
		 * RISC PCI controllers, in particular sparc64.
		 *
		 * On 5703/5704 chips, this bit has been reassigned
		 * a different meaning.  In particular, it is used
		 * on those chips to enable a PCI-X workaround.
		 */
		tp->dma_rwctrl |= DMA_RWCTRL_ASSERT_ALL_BE;
	}

	tw32(TG3PCI_DMA_RW_CTRL, tp->dma_rwctrl);

#if 0
	/* Unneeded, already done by tg3_get_invariants.  */
	tg3_switch_clocks(tp);
#endif

	if (tg3_asic_rev(tp) != ASIC_REV_5700 &&
	    tg3_asic_rev(tp) != ASIC_REV_5701)
		goto out;

	/* It is best to perform DMA test with maximum write burst size
	 * to expose the 5700/5701 write DMA bug.
	 */
	saved_dma_rwctrl = tp->dma_rwctrl;
	tp->dma_rwctrl &= ~DMA_RWCTRL_WRITE_BNDRY_MASK;
	tw32(TG3PCI_DMA_RW_CTRL, tp->dma_rwctrl);

	while (1) {
		u32 *p = buf, i;

		for (i = 0; i < TEST_BUFFER_SIZE / sizeof(u32); i++)
			p[i] = i;

		/* Send the buffer to the chip. */
		ret = tg3_do_test_dma(tp, buf, buf_dma, TEST_BUFFER_SIZE, true);
		if (ret) {
			dev_err(&tp->pdev->dev,
				"%s: Buffer write failed. err = %d\n",
				__func__, ret);
			break;
		}

#if 0
		/* validate data reached card RAM correctly. */
		for (i = 0; i < TEST_BUFFER_SIZE / sizeof(u32); i++) {
			u32 val;
			tg3_read_mem(tp, 0x2100 + (i*4), &val);
			if (le32_to_cpu(val) != p[i]) {
				dev_err(&tp->pdev->dev,
					"%s: Buffer corrupted on device! "
					"(%d != %d)\n", __func__, val, i);
				/* ret = -ENODEV here? */
			}
			p[i] = 0;
		}
#endif
		/* Now read it back. */
		ret = tg3_do_test_dma(tp, buf, buf_dma, TEST_BUFFER_SIZE, false);
		if (ret) {
			dev_err(&tp->pdev->dev, "%s: Buffer read failed. "
				"err = %d\n", __func__, ret);
			break;
		}

		/* Verify it. */
		for (i = 0; i < TEST_BUFFER_SIZE / sizeof(u32); i++) {
			if (p[i] == i)
				continue;

			if ((tp->dma_rwctrl & DMA_RWCTRL_WRITE_BNDRY_MASK) !=
			    DMA_RWCTRL_WRITE_BNDRY_16) {
				tp->dma_rwctrl &= ~DMA_RWCTRL_WRITE_BNDRY_MASK;
				tp->dma_rwctrl |= DMA_RWCTRL_WRITE_BNDRY_16;
				tw32(TG3PCI_DMA_RW_CTRL, tp->dma_rwctrl);
				break;
			} else {
				dev_err(&tp->pdev->dev,
					"%s: Buffer corrupted on read back! "
					"(%d != %d)\n", __func__, p[i], i);
				ret = -ENODEV;
				goto out;
			}
		}

		if (i == (TEST_BUFFER_SIZE / sizeof(u32))) {
			/* Success. */
			ret = 0;
			break;
		}
	}
	if ((tp->dma_rwctrl & DMA_RWCTRL_WRITE_BNDRY_MASK) !=
	    DMA_RWCTRL_WRITE_BNDRY_16) {
		/* DMA test passed without adjusting DMA boundary,
		 * now look for chipsets that are known to expose the
		 * DMA bug without failing the test.
		 */
		if (pci_dev_present(tg3_dma_wait_state_chipsets)) {
			tp->dma_rwctrl &= ~DMA_RWCTRL_WRITE_BNDRY_MASK;
			tp->dma_rwctrl |= DMA_RWCTRL_WRITE_BNDRY_16;
		} else {
			/* Safe to use the calculated DMA boundary. */
			tp->dma_rwctrl = saved_dma_rwctrl;
		}

		tw32(TG3PCI_DMA_RW_CTRL, tp->dma_rwctrl);
	}

out:
	dma_free_coherent(&tp->pdev->dev, TEST_BUFFER_SIZE, buf, buf_dma);
out_nofree:
	return ret;
}

static void tg3_init_bufmgr_config(struct tg3 *tp)
{
	if (tg3_flag(tp, 57765_PLUS)) {
		tp->bufmgr_config.mbuf_read_dma_low_water =
			DEFAULT_MB_RDMA_LOW_WATER_5705;
		tp->bufmgr_config.mbuf_mac_rx_low_water =
			DEFAULT_MB_MACRX_LOW_WATER_57765;
		tp->bufmgr_config.mbuf_high_water =
			DEFAULT_MB_HIGH_WATER_57765;

		tp->bufmgr_config.mbuf_read_dma_low_water_jumbo =
			DEFAULT_MB_RDMA_LOW_WATER_5705;
		tp->bufmgr_config.mbuf_mac_rx_low_water_jumbo =
			DEFAULT_MB_MACRX_LOW_WATER_JUMBO_57765;
		tp->bufmgr_config.mbuf_high_water_jumbo =
			DEFAULT_MB_HIGH_WATER_JUMBO_57765;
	} else if (tg3_flag(tp, 5705_PLUS)) {
		tp->bufmgr_config.mbuf_read_dma_low_water =
			DEFAULT_MB_RDMA_LOW_WATER_5705;
		tp->bufmgr_config.mbuf_mac_rx_low_water =
			DEFAULT_MB_MACRX_LOW_WATER_5705;
		tp->bufmgr_config.mbuf_high_water =
			DEFAULT_MB_HIGH_WATER_5705;
		if (tg3_asic_rev(tp) == ASIC_REV_5906) {
			tp->bufmgr_config.mbuf_mac_rx_low_water =
				DEFAULT_MB_MACRX_LOW_WATER_5906;
			tp->bufmgr_config.mbuf_high_water =
				DEFAULT_MB_HIGH_WATER_5906;
		}

		tp->bufmgr_config.mbuf_read_dma_low_water_jumbo =
			DEFAULT_MB_RDMA_LOW_WATER_JUMBO_5780;
		tp->bufmgr_config.mbuf_mac_rx_low_water_jumbo =
			DEFAULT_MB_MACRX_LOW_WATER_JUMBO_5780;
		tp->bufmgr_config.mbuf_high_water_jumbo =
			DEFAULT_MB_HIGH_WATER_JUMBO_5780;
	} else {
		tp->bufmgr_config.mbuf_read_dma_low_water =
			DEFAULT_MB_RDMA_LOW_WATER;
		tp->bufmgr_config.mbuf_mac_rx_low_water =
			DEFAULT_MB_MACRX_LOW_WATER;
		tp->bufmgr_config.mbuf_high_water =
			DEFAULT_MB_HIGH_WATER;

		tp->bufmgr_config.mbuf_read_dma_low_water_jumbo =
			DEFAULT_MB_RDMA_LOW_WATER_JUMBO;
		tp->bufmgr_config.mbuf_mac_rx_low_water_jumbo =
			DEFAULT_MB_MACRX_LOW_WATER_JUMBO;
		tp->bufmgr_config.mbuf_high_water_jumbo =
			DEFAULT_MB_HIGH_WATER_JUMBO;
	}

	tp->bufmgr_config.dma_low_water = DEFAULT_DMA_LOW_WATER;
	tp->bufmgr_config.dma_high_water = DEFAULT_DMA_HIGH_WATER;
}

static char *tg3_phy_string(struct tg3 *tp)
{
	switch (tp->phy_id & TG3_PHY_ID_MASK) {
	case TG3_PHY_ID_BCM5400:	return "5400";
	case TG3_PHY_ID_BCM5401:	return "5401";
	case TG3_PHY_ID_BCM5411:	return "5411";
	case TG3_PHY_ID_BCM5701:	return "5701";
	case TG3_PHY_ID_BCM5703:	return "5703";
	case TG3_PHY_ID_BCM5704:	return "5704";
	case TG3_PHY_ID_BCM5705:	return "5705";
	case TG3_PHY_ID_BCM5750:	return "5750";
	case TG3_PHY_ID_BCM5752:	return "5752";
	case TG3_PHY_ID_BCM5714:	return "5714";
	case TG3_PHY_ID_BCM5780:	return "5780";
	case TG3_PHY_ID_BCM5755:	return "5755";
	case TG3_PHY_ID_BCM5787:	return "5787";
	case TG3_PHY_ID_BCM5784:	return "5784";
	case TG3_PHY_ID_BCM5756:	return "5722/5756";
	case TG3_PHY_ID_BCM5906:	return "5906";
	case TG3_PHY_ID_BCM5761:	return "5761";
	case TG3_PHY_ID_BCM5718C:	return "5718C";
	case TG3_PHY_ID_BCM5718S:	return "5718S";
	case TG3_PHY_ID_BCM57765:	return "57765";
	case TG3_PHY_ID_BCM5719C:	return "5719C";
	case TG3_PHY_ID_BCM5720C:	return "5720C";
	case TG3_PHY_ID_BCM5762:	return "5762C";
	case TG3_PHY_ID_BCM8002:	return "8002/serdes";
	case 0:			return "serdes";
	default:		return "unknown";
	}
}

static char *tg3_bus_string(struct tg3 *tp, char *str)
{
	if (tg3_flag(tp, PCI_EXPRESS)) {
		strcpy(str, "PCI Express");
		return str;
	} else if (tg3_flag(tp, PCIX_MODE)) {
		u32 clock_ctrl = tr32(TG3PCI_CLOCK_CTRL) & 0x1f;

		strcpy(str, "PCIX:");

		if ((clock_ctrl == 7) ||
		    ((tr32(GRC_MISC_CFG) & GRC_MISC_CFG_BOARD_ID_MASK) ==
		     GRC_MISC_CFG_BOARD_ID_5704CIOBE))
			strcat(str, "133MHz");
		else if (clock_ctrl == 0)
			strcat(str, "33MHz");
		else if (clock_ctrl == 2)
			strcat(str, "50MHz");
		else if (clock_ctrl == 4)
			strcat(str, "66MHz");
		else if (clock_ctrl == 6)
			strcat(str, "100MHz");
	} else {
		strcpy(str, "PCI:");
		if (tg3_flag(tp, PCI_HIGH_SPEED))
			strcat(str, "66MHz");
		else
			strcat(str, "33MHz");
	}
	if (tg3_flag(tp, PCI_32BIT))
		strcat(str, ":32-bit");
	else
		strcat(str, ":64-bit");
	return str;
}

static void tg3_init_coal(struct tg3 *tp)
{
	struct ethtool_coalesce *ec = &tp->coal;

	memset(ec, 0, sizeof(*ec));
	ec->cmd = ETHTOOL_GCOALESCE;
	ec->rx_coalesce_usecs = LOW_RXCOL_TICKS;
	ec->tx_coalesce_usecs = LOW_TXCOL_TICKS;
	ec->rx_max_coalesced_frames = LOW_RXMAX_FRAMES;
	ec->tx_max_coalesced_frames = LOW_TXMAX_FRAMES;
	ec->rx_coalesce_usecs_irq = DEFAULT_RXCOAL_TICK_INT;
	ec->tx_coalesce_usecs_irq = DEFAULT_TXCOAL_TICK_INT;
	ec->rx_max_coalesced_frames_irq = DEFAULT_RXCOAL_MAXF_INT;
	ec->tx_max_coalesced_frames_irq = DEFAULT_TXCOAL_MAXF_INT;
	ec->stats_block_coalesce_usecs = DEFAULT_STAT_COAL_TICKS;

	if (tp->coalesce_mode & (HOSTCC_MODE_CLRTICK_RXBD |
				 HOSTCC_MODE_CLRTICK_TXBD)) {
		ec->rx_coalesce_usecs = LOW_RXCOL_TICKS_CLRTCKS;
		ec->rx_coalesce_usecs_irq = DEFAULT_RXCOAL_TICK_INT_CLRTCKS;
		ec->tx_coalesce_usecs = LOW_TXCOL_TICKS_CLRTCKS;
		ec->tx_coalesce_usecs_irq = DEFAULT_TXCOAL_TICK_INT_CLRTCKS;
	}

	if (tg3_flag(tp, 5705_PLUS)) {
		ec->rx_coalesce_usecs_irq = 0;
		ec->tx_coalesce_usecs_irq = 0;
		ec->stats_block_coalesce_usecs = 0;
	}
}

static int tg3_init_one(struct pci_dev *pdev,
				  const struct pci_device_id *ent)
{
	struct net_device *dev;
	struct tg3 *tp;
	int i, err, pm_cap;
	u32 sndmbx, rcvmbx, intmbx;
	char str[40];
	u64 dma_mask, persist_dma_mask;
	netdev_features_t features = 0;

	printk_once(KERN_INFO "%s\n", version);

	err = pci_enable_device(pdev);
	if (err) {
		dev_err(&pdev->dev, "Cannot enable PCI device, aborting\n");
		return err;
	}

	err = pci_request_regions(pdev, DRV_MODULE_NAME);
	if (err) {
		dev_err(&pdev->dev, "Cannot obtain PCI resources, aborting\n");
		goto err_out_disable_pdev;
	}

	pci_set_master(pdev);

	/* Find power-management capability. */
	pm_cap = pci_find_capability(pdev, PCI_CAP_ID_PM);
	if (pm_cap == 0) {
		dev_err(&pdev->dev,
			"Cannot find Power Management capability, aborting\n");
		err = -EIO;
		goto err_out_free_res;
	}

	err = pci_set_power_state(pdev, PCI_D0);
	if (err) {
		dev_err(&pdev->dev, "Transition to D0 failed, aborting\n");
		goto err_out_free_res;
	}

	dev = alloc_etherdev_mq(sizeof(*tp), TG3_IRQ_MAX_VECS);
	if (!dev) {
		err = -ENOMEM;
		goto err_out_power_down;
	}

	SET_NETDEV_DEV(dev, &pdev->dev);

	tp = netdev_priv(dev);
	tp->pdev = pdev;
	tp->dev = dev;
	tp->pm_cap = pm_cap;
	tp->rx_mode = TG3_DEF_RX_MODE;
	tp->tx_mode = TG3_DEF_TX_MODE;
	tp->irq_sync = 1;

	if (tg3_debug > 0)
		tp->msg_enable = tg3_debug;
	else
		tp->msg_enable = TG3_DEF_MSG_ENABLE;

	if (pdev_is_ssb_gige_core(pdev)) {
		tg3_flag_set(tp, IS_SSB_CORE);
		if (ssb_gige_must_flush_posted_writes(pdev))
			tg3_flag_set(tp, FLUSH_POSTED_WRITES);
		if (ssb_gige_one_dma_at_once(pdev))
			tg3_flag_set(tp, ONE_DMA_AT_ONCE);
		if (ssb_gige_have_roboswitch(pdev))
			tg3_flag_set(tp, ROBOSWITCH);
		if (ssb_gige_is_rgmii(pdev))
			tg3_flag_set(tp, RGMII_MODE);
	}

	/* The word/byte swap controls here control register access byte
	 * swapping.  DMA data byte swapping is controlled in the GRC_MODE
	 * setting below.
	 */
	tp->misc_host_ctrl =
		MISC_HOST_CTRL_MASK_PCI_INT |
		MISC_HOST_CTRL_WORD_SWAP |
		MISC_HOST_CTRL_INDIR_ACCESS |
		MISC_HOST_CTRL_PCISTATE_RW;

	/* The NONFRM (non-frame) byte/word swap controls take effect
	 * on descriptor entries, anything which isn't packet data.
	 *
	 * The StrongARM chips on the board (one for tx, one for rx)
	 * are running in big-endian mode.
	 */
	tp->grc_mode = (GRC_MODE_WSWAP_DATA | GRC_MODE_BSWAP_DATA |
			GRC_MODE_WSWAP_NONFRM_DATA);
#ifdef __BIG_ENDIAN
	tp->grc_mode |= GRC_MODE_BSWAP_NONFRM_DATA;
#endif
	spin_lock_init(&tp->lock);
	spin_lock_init(&tp->indirect_lock);
	INIT_WORK(&tp->reset_task, tg3_reset_task);

	tp->regs = pci_ioremap_bar(pdev, BAR_0);
	if (!tp->regs) {
		dev_err(&pdev->dev, "Cannot map device registers, aborting\n");
		err = -ENOMEM;
		goto err_out_free_dev;
	}

	if (tp->pdev->device == PCI_DEVICE_ID_TIGON3_5761 ||
	    tp->pdev->device == PCI_DEVICE_ID_TIGON3_5761E ||
	    tp->pdev->device == TG3PCI_DEVICE_TIGON3_5761S ||
	    tp->pdev->device == TG3PCI_DEVICE_TIGON3_5761SE ||
	    tp->pdev->device == TG3PCI_DEVICE_TIGON3_5717 ||
	    tp->pdev->device == TG3PCI_DEVICE_TIGON3_5717_C ||
	    tp->pdev->device == TG3PCI_DEVICE_TIGON3_5718 ||
	    tp->pdev->device == TG3PCI_DEVICE_TIGON3_5719 ||
	    tp->pdev->device == TG3PCI_DEVICE_TIGON3_5720 ||
	    tp->pdev->device == TG3PCI_DEVICE_TIGON3_5762 ||
	    tp->pdev->device == TG3PCI_DEVICE_TIGON3_5725 ||
	    tp->pdev->device == TG3PCI_DEVICE_TIGON3_5727) {
		tg3_flag_set(tp, ENABLE_APE);
		tp->aperegs = pci_ioremap_bar(pdev, BAR_2);
		if (!tp->aperegs) {
			dev_err(&pdev->dev,
				"Cannot map APE registers, aborting\n");
			err = -ENOMEM;
			goto err_out_iounmap;
		}
	}

	tp->rx_pending = TG3_DEF_RX_RING_PENDING;
	tp->rx_jumbo_pending = TG3_DEF_RX_JUMBO_RING_PENDING;

	dev->ethtool_ops = &tg3_ethtool_ops;
	dev->watchdog_timeo = TG3_TX_TIMEOUT;
	dev->netdev_ops = &tg3_netdev_ops;
	dev->irq = pdev->irq;

	err = tg3_get_invariants(tp, ent);
	if (err) {
		dev_err(&pdev->dev,
			"Problem fetching invariants of chip, aborting\n");
		goto err_out_apeunmap;
	}

	/* The EPB bridge inside 5714, 5715, and 5780 and any
	 * device behind the EPB cannot support DMA addresses > 40-bit.
	 * On 64-bit systems with IOMMU, use 40-bit dma_mask.
	 * On 64-bit systems without IOMMU, use 64-bit dma_mask and
	 * do DMA address check in tg3_start_xmit().
	 */
	if (tg3_flag(tp, IS_5788))
		persist_dma_mask = dma_mask = DMA_BIT_MASK(32);
	else if (tg3_flag(tp, 40BIT_DMA_BUG)) {
		persist_dma_mask = dma_mask = DMA_BIT_MASK(40);
#ifdef CONFIG_HIGHMEM
		dma_mask = DMA_BIT_MASK(64);
#endif
	} else
		persist_dma_mask = dma_mask = DMA_BIT_MASK(64);

	/* Configure DMA attributes. */
	if (dma_mask > DMA_BIT_MASK(32)) {
		err = pci_set_dma_mask(pdev, dma_mask);
		if (!err) {
			features |= NETIF_F_HIGHDMA;
			err = pci_set_consistent_dma_mask(pdev,
							  persist_dma_mask);
			if (err < 0) {
				dev_err(&pdev->dev, "Unable to obtain 64 bit "
					"DMA for consistent allocations\n");
				goto err_out_apeunmap;
			}
		}
	}
	if (err || dma_mask == DMA_BIT_MASK(32)) {
		err = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
		if (err) {
			dev_err(&pdev->dev,
				"No usable DMA configuration, aborting\n");
			goto err_out_apeunmap;
		}
	}

	tg3_init_bufmgr_config(tp);

	/* 5700 B0 chips do not support checksumming correctly due
	 * to hardware bugs.
	 */
	if (tg3_chip_rev_id(tp) != CHIPREV_ID_5700_B0) {
		features |= NETIF_F_SG | NETIF_F_IP_CSUM | NETIF_F_RXCSUM;

		if (tg3_flag(tp, 5755_PLUS))
			features |= NETIF_F_IPV6_CSUM;
	}

	/* TSO is on by default on chips that support hardware TSO.
	 * Firmware TSO on older chips gives lower performance, so it
	 * is off by default, but can be enabled using ethtool.
	 */
	if ((tg3_flag(tp, HW_TSO_1) ||
	     tg3_flag(tp, HW_TSO_2) ||
	     tg3_flag(tp, HW_TSO_3)) &&
	    (features & NETIF_F_IP_CSUM))
		features |= NETIF_F_TSO;
	if (tg3_flag(tp, HW_TSO_2) || tg3_flag(tp, HW_TSO_3)) {
		if (features & NETIF_F_IPV6_CSUM)
			features |= NETIF_F_TSO6;
		if (tg3_flag(tp, HW_TSO_3) ||
		    tg3_asic_rev(tp) == ASIC_REV_5761 ||
		    (tg3_asic_rev(tp) == ASIC_REV_5784 &&
		     tg3_chip_rev(tp) != CHIPREV_5784_AX) ||
		    tg3_asic_rev(tp) == ASIC_REV_5785 ||
		    tg3_asic_rev(tp) == ASIC_REV_57780)
			features |= NETIF_F_TSO_ECN;
	}

	dev->features |= features | NETIF_F_HW_VLAN_CTAG_TX |
			 NETIF_F_HW_VLAN_CTAG_RX;
	dev->vlan_features |= features;

	/*
	 * Add loopback capability only for a subset of devices that support
	 * MAC-LOOPBACK. Eventually this need to be enhanced to allow INT-PHY
	 * loopback for the remaining devices.
	 */
	if (tg3_asic_rev(tp) != ASIC_REV_5780 &&
	    !tg3_flag(tp, CPMU_PRESENT))
		/* Add the loopback capability */
		features |= NETIF_F_LOOPBACK;

	dev->hw_features |= features;

	if (tg3_chip_rev_id(tp) == CHIPREV_ID_5705_A1 &&
	    !tg3_flag(tp, TSO_CAPABLE) &&
	    !(tr32(TG3PCI_PCISTATE) & PCISTATE_BUS_SPEED_HIGH)) {
		tg3_flag_set(tp, MAX_RXPEND_64);
		tp->rx_pending = 63;
	}

	err = tg3_get_device_address(tp);
	if (err) {
		dev_err(&pdev->dev,
			"Could not obtain valid ethernet address, aborting\n");
		goto err_out_apeunmap;
	}

	/*
	 * Reset chip in case UNDI or EFI driver did not shutdown
	 * DMA self test will enable WDMAC and we'll see (spurious)
	 * pending DMA on the PCI bus at that point.
	 */
	if ((tr32(HOSTCC_MODE) & HOSTCC_MODE_ENABLE) ||
	    (tr32(WDMAC_MODE) & WDMAC_MODE_ENABLE)) {
		tw32(MEMARB_MODE, MEMARB_MODE_ENABLE);
		tg3_halt(tp, RESET_KIND_SHUTDOWN, 1);
	}

	err = tg3_test_dma(tp);
	if (err) {
		dev_err(&pdev->dev, "DMA engine test failed, aborting\n");
		goto err_out_apeunmap;
	}

	intmbx = MAILBOX_INTERRUPT_0 + TG3_64BIT_REG_LOW;
	rcvmbx = MAILBOX_RCVRET_CON_IDX_0 + TG3_64BIT_REG_LOW;
	sndmbx = MAILBOX_SNDHOST_PROD_IDX_0 + TG3_64BIT_REG_LOW;
	for (i = 0; i < tp->irq_max; i++) {
		struct tg3_napi *tnapi = &tp->napi[i];

		tnapi->tp = tp;
		tnapi->tx_pending = TG3_DEF_TX_RING_PENDING;

		tnapi->int_mbox = intmbx;
		if (i <= 4)
			intmbx += 0x8;
		else
			intmbx += 0x4;

		tnapi->consmbox = rcvmbx;
		tnapi->prodmbox = sndmbx;

		if (i)
			tnapi->coal_now = HOSTCC_MODE_COAL_VEC1_NOW << (i - 1);
		else
			tnapi->coal_now = HOSTCC_MODE_NOW;

		if (!tg3_flag(tp, SUPPORT_MSIX))
			break;

		/*
		 * If we support MSIX, we'll be using RSS.  If we're using
		 * RSS, the first vector only handles link interrupts and the
		 * remaining vectors handle rx and tx interrupts.  Reuse the
		 * mailbox values for the next iteration.  The values we setup
		 * above are still useful for the single vectored mode.
		 */
		if (!i)
			continue;

		rcvmbx += 0x8;

		if (sndmbx & 0x4)
			sndmbx -= 0x4;
		else
			sndmbx += 0xc;
	}

	tg3_init_coal(tp);

	pci_set_drvdata(pdev, dev);

	if (tg3_asic_rev(tp) == ASIC_REV_5719 ||
	    tg3_asic_rev(tp) == ASIC_REV_5720 ||
	    tg3_asic_rev(tp) == ASIC_REV_5762)
		tg3_flag_set(tp, PTP_CAPABLE);

	if (tg3_flag(tp, 5717_PLUS)) {
		/* Resume a low-power mode */
		tg3_frob_aux_power(tp, false);
	}

	tg3_timer_init(tp);

	tg3_carrier_off(tp);

	err = register_netdev(dev);
	if (err) {
		dev_err(&pdev->dev, "Cannot register net device, aborting\n");
		goto err_out_apeunmap;
	}

	netdev_info(dev, "Tigon3 [partno(%s) rev %04x] (%s) MAC address %pM\n",
		    tp->board_part_number,
		    tg3_chip_rev_id(tp),
		    tg3_bus_string(tp, str),
		    dev->dev_addr);

	if (tp->phy_flags & TG3_PHYFLG_IS_CONNECTED) {
		struct phy_device *phydev;
		phydev = tp->mdio_bus->phy_map[TG3_PHY_MII_ADDR];
		netdev_info(dev,
			    "attached PHY driver [%s] (mii_bus:phy_addr=%s)\n",
			    phydev->drv->name, dev_name(&phydev->dev));
	} else {
		char *ethtype;

		if (tp->phy_flags & TG3_PHYFLG_10_100_ONLY)
			ethtype = "10/100Base-TX";
		else if (tp->phy_flags & TG3_PHYFLG_ANY_SERDES)
			ethtype = "1000Base-SX";
		else
			ethtype = "10/100/1000Base-T";

		netdev_info(dev, "attached PHY is %s (%s Ethernet) "
			    "(WireSpeed[%d], EEE[%d])\n",
			    tg3_phy_string(tp), ethtype,
			    (tp->phy_flags & TG3_PHYFLG_NO_ETH_WIRE_SPEED) == 0,
			    (tp->phy_flags & TG3_PHYFLG_EEE_CAP) != 0);
	}

	netdev_info(dev, "RXcsums[%d] LinkChgREG[%d] MIirq[%d] ASF[%d] TSOcap[%d]\n",
		    (dev->features & NETIF_F_RXCSUM) != 0,
		    tg3_flag(tp, USE_LINKCHG_REG) != 0,
		    (tp->phy_flags & TG3_PHYFLG_USE_MI_INTERRUPT) != 0,
		    tg3_flag(tp, ENABLE_ASF) != 0,
		    tg3_flag(tp, TSO_CAPABLE) != 0);
	netdev_info(dev, "dma_rwctrl[%08x] dma_mask[%d-bit]\n",
		    tp->dma_rwctrl,
		    pdev->dma_mask == DMA_BIT_MASK(32) ? 32 :
		    ((u64)pdev->dma_mask) == DMA_BIT_MASK(40) ? 40 : 64);

	pci_save_state(pdev);

	return 0;

err_out_apeunmap:
	if (tp->aperegs) {
		iounmap(tp->aperegs);
		tp->aperegs = NULL;
	}

err_out_iounmap:
	if (tp->regs) {
		iounmap(tp->regs);
		tp->regs = NULL;
	}

err_out_free_dev:
	free_netdev(dev);

err_out_power_down:
	pci_set_power_state(pdev, PCI_D3hot);

err_out_free_res:
	pci_release_regions(pdev);

err_out_disable_pdev:
	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);
	return err;
}

static void tg3_remove_one(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);

	if (dev) {
		struct tg3 *tp = netdev_priv(dev);

		release_firmware(tp->fw);

		tg3_reset_task_cancel(tp);

		if (tg3_flag(tp, USE_PHYLIB)) {
			tg3_phy_fini(tp);
			tg3_mdio_fini(tp);
		}

		unregister_netdev(dev);
		if (tp->aperegs) {
			iounmap(tp->aperegs);
			tp->aperegs = NULL;
		}
		if (tp->regs) {
			iounmap(tp->regs);
			tp->regs = NULL;
		}
		free_netdev(dev);
		pci_release_regions(pdev);
		pci_disable_device(pdev);
		pci_set_drvdata(pdev, NULL);
	}
}

#ifdef CONFIG_PM_SLEEP
static int tg3_suspend(struct device *device)
{
	struct pci_dev *pdev = to_pci_dev(device);
	struct net_device *dev = pci_get_drvdata(pdev);
	struct tg3 *tp = netdev_priv(dev);
	int err;

	if (!netif_running(dev))
		return 0;

	tg3_reset_task_cancel(tp);
	tg3_phy_stop(tp);
	tg3_netif_stop(tp);

	tg3_timer_stop(tp);

	tg3_full_lock(tp, 1);
	tg3_disable_ints(tp);
	tg3_full_unlock(tp);

	netif_device_detach(dev);

	tg3_full_lock(tp, 0);
	tg3_halt(tp, RESET_KIND_SHUTDOWN, 1);
	tg3_flag_clear(tp, INIT_COMPLETE);
	tg3_full_unlock(tp);

	err = tg3_power_down_prepare(tp);
	if (err) {
		int err2;

		tg3_full_lock(tp, 0);

		tg3_flag_set(tp, INIT_COMPLETE);
		err2 = tg3_restart_hw(tp, true);
		if (err2)
			goto out;

		tg3_timer_start(tp);

		netif_device_attach(dev);
		tg3_netif_start(tp);

out:
		tg3_full_unlock(tp);

		if (!err2)
			tg3_phy_start(tp);
	}

	return err;
}

static int tg3_resume(struct device *device)
{
	struct pci_dev *pdev = to_pci_dev(device);
	struct net_device *dev = pci_get_drvdata(pdev);
	struct tg3 *tp = netdev_priv(dev);
	int err;

	if (!netif_running(dev))
		return 0;

	netif_device_attach(dev);

	tg3_full_lock(tp, 0);

	tg3_flag_set(tp, INIT_COMPLETE);
	err = tg3_restart_hw(tp,
			     !(tp->phy_flags & TG3_PHYFLG_KEEP_LINK_ON_PWRDN));
	if (err)
		goto out;

	tg3_timer_start(tp);

	tg3_netif_start(tp);

out:
	tg3_full_unlock(tp);

	if (!err)
		tg3_phy_start(tp);

	return err;
}
#endif /* CONFIG_PM_SLEEP */

static SIMPLE_DEV_PM_OPS(tg3_pm_ops, tg3_suspend, tg3_resume);

/**
 * tg3_io_error_detected - called when PCI error is detected
 * @pdev: Pointer to PCI device
 * @state: The current pci connection state
 *
 * This function is called after a PCI bus error affecting
 * this device has been detected.
 */
static pci_ers_result_t tg3_io_error_detected(struct pci_dev *pdev,
					      pci_channel_state_t state)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct tg3 *tp = netdev_priv(netdev);
	pci_ers_result_t err = PCI_ERS_RESULT_NEED_RESET;

	netdev_info(netdev, "PCI I/O error detected\n");

	rtnl_lock();

	if (!netif_running(netdev))
		goto done;

	tg3_phy_stop(tp);

	tg3_netif_stop(tp);

	tg3_timer_stop(tp);

	/* Want to make sure that the reset task doesn't run */
	tg3_reset_task_cancel(tp);

	netif_device_detach(netdev);

	/* Clean up software state, even if MMIO is blocked */
	tg3_full_lock(tp, 0);
	tg3_halt(tp, RESET_KIND_SHUTDOWN, 0);
	tg3_full_unlock(tp);

done:
	if (state == pci_channel_io_perm_failure)
		err = PCI_ERS_RESULT_DISCONNECT;
	else
		pci_disable_device(pdev);

	rtnl_unlock();

	return err;
}

/**
 * tg3_io_slot_reset - called after the pci bus has been reset.
 * @pdev: Pointer to PCI device
 *
 * Restart the card from scratch, as if from a cold-boot.
 * At this point, the card has exprienced a hard reset,
 * followed by fixups by BIOS, and has its config space
 * set up identically to what it was at cold boot.
 */
static pci_ers_result_t tg3_io_slot_reset(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct tg3 *tp = netdev_priv(netdev);
	pci_ers_result_t rc = PCI_ERS_RESULT_DISCONNECT;
	int err;

	rtnl_lock();

	if (pci_enable_device(pdev)) {
		netdev_err(netdev, "Cannot re-enable PCI device after reset.\n");
		goto done;
	}

	pci_set_master(pdev);
	pci_restore_state(pdev);
	pci_save_state(pdev);

	if (!netif_running(netdev)) {
		rc = PCI_ERS_RESULT_RECOVERED;
		goto done;
	}

	err = tg3_power_up(tp);
	if (err)
		goto done;

	rc = PCI_ERS_RESULT_RECOVERED;

done:
	rtnl_unlock();

	return rc;
}

/**
 * tg3_io_resume - called when traffic can start flowing again.
 * @pdev: Pointer to PCI device
 *
 * This callback is called when the error recovery driver tells
 * us that its OK to resume normal operation.
 */
static void tg3_io_resume(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct tg3 *tp = netdev_priv(netdev);
	int err;

	rtnl_lock();

	if (!netif_running(netdev))
		goto done;

	tg3_full_lock(tp, 0);
	tg3_flag_set(tp, INIT_COMPLETE);
	err = tg3_restart_hw(tp, true);
	if (err) {
		tg3_full_unlock(tp);
		netdev_err(netdev, "Cannot restart hardware after reset.\n");
		goto done;
	}

	netif_device_attach(netdev);

	tg3_timer_start(tp);

	tg3_netif_start(tp);

	tg3_full_unlock(tp);

	tg3_phy_start(tp);

done:
	rtnl_unlock();
}

static const struct pci_error_handlers tg3_err_handler = {
	.error_detected	= tg3_io_error_detected,
	.slot_reset	= tg3_io_slot_reset,
	.resume		= tg3_io_resume
};

static struct pci_driver tg3_driver = {
	.name		= DRV_MODULE_NAME,
	.id_table	= tg3_pci_tbl,
	.probe		= tg3_init_one,
	.remove		= tg3_remove_one,
	.err_handler	= &tg3_err_handler,
	.driver.pm	= &tg3_pm_ops,
};

static int __init tg3_init(void)
{
	return pci_register_driver(&tg3_driver);
}

static void __exit tg3_cleanup(void)
{
	pci_unregister_driver(&tg3_driver);
}

module_init(tg3_init);
module_exit(tg3_cleanup);
