/*
 * tg3.c: Broadcom Tigon3 ethernet driver.
 *
 * Copyright (C) 2001, 2002, 2003, 2004 David S. Miller (davem@redhat.com)
 * Copyright (C) 2001, 2002, 2003 Jeff Garzik (jgarzik@pobox.com)
 * Copyright (C) 2004 Sun Microsystems Inc.
 * Copyright (C) 2005-2009 Broadcom Corporation.
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
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/compiler.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/in.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/ethtool.h>
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

#include <net/checksum.h>
#include <net/ip.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/byteorder.h>
#include <asm/uaccess.h>

#ifdef CONFIG_SPARC
#include <asm/idprom.h>
#include <asm/prom.h>
#endif

#define BAR_0	0
#define BAR_2	2

#if defined(CONFIG_VLAN_8021Q) || defined(CONFIG_VLAN_8021Q_MODULE)
#define TG3_VLAN_TAG_USED 1
#else
#define TG3_VLAN_TAG_USED 0
#endif

#include "tg3.h"

#define DRV_MODULE_NAME		"tg3"
#define PFX DRV_MODULE_NAME	": "
#define DRV_MODULE_VERSION	"3.102"
#define DRV_MODULE_RELDATE	"September 1, 2009"

#define TG3_DEF_MAC_MODE	0
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

/* length of time before we decide the hardware is borked,
 * and dev->tx_timeout() should be called to fix the problem
 */
#define TG3_TX_TIMEOUT			(5 * HZ)

/* hardware minimum and maximum for a single frame's data payload */
#define TG3_MIN_MTU			60
#define TG3_MAX_MTU(tp)	\
	((tp->tg3_flags & TG3_FLAG_JUMBO_CAPABLE) ? 9000 : 1500)

/* These numbers seem to be hard coded in the NIC firmware somehow.
 * You can't change the ring sizes, but you can change where you place
 * them in the NIC onboard memory.
 */
#define TG3_RX_RING_SIZE		512
#define TG3_DEF_RX_RING_PENDING		200
#define TG3_RX_JUMBO_RING_SIZE		256
#define TG3_DEF_RX_JUMBO_RING_PENDING	100
#define TG3_RSS_INDIR_TBL_SIZE 128

/* Do not place this n-ring entries value into the tp struct itself,
 * we really want to expose these constants to GCC so that modulo et
 * al.  operations are done with shifts and masks instead of with
 * hw multiply/modulo instructions.  Another solution would be to
 * replace things like '% foo' with '& (foo - 1)'.
 */
#define TG3_RX_RCB_RING_SIZE(tp)	\
	(((tp->tg3_flags & TG3_FLAG_JUMBO_CAPABLE) && \
	  !(tp->tg3_flags2 & TG3_FLG2_5780_CLASS)) ? 1024 : 512)

#define TG3_TX_RING_SIZE		512
#define TG3_DEF_TX_RING_PENDING		(TG3_TX_RING_SIZE - 1)

#define TG3_RX_RING_BYTES	(sizeof(struct tg3_rx_buffer_desc) * \
				 TG3_RX_RING_SIZE)
#define TG3_RX_JUMBO_RING_BYTES	(sizeof(struct tg3_ext_rx_buffer_desc) * \
				 TG3_RX_JUMBO_RING_SIZE)
#define TG3_RX_RCB_RING_BYTES(tp) (sizeof(struct tg3_rx_buffer_desc) * \
				 TG3_RX_RCB_RING_SIZE(tp))
#define TG3_TX_RING_BYTES	(sizeof(struct tg3_tx_buffer_desc) * \
				 TG3_TX_RING_SIZE)
#define NEXT_TX(N)		(((N) + 1) & (TG3_TX_RING_SIZE - 1))

#define TG3_DMA_BYTE_ENAB		64

#define TG3_RX_STD_DMA_SZ		1536
#define TG3_RX_JMB_DMA_SZ		9046

#define TG3_RX_DMA_TO_MAP_SZ(x)		((x) + TG3_DMA_BYTE_ENAB)

#define TG3_RX_STD_MAP_SZ		TG3_RX_DMA_TO_MAP_SZ(TG3_RX_STD_DMA_SZ)
#define TG3_RX_JMB_MAP_SZ		TG3_RX_DMA_TO_MAP_SZ(TG3_RX_JMB_DMA_SZ)

/* minimum number of free TX descriptors required to wake up TX process */
#define TG3_TX_WAKEUP_THRESH(tnapi)		((tnapi)->tx_pending / 4)

#define TG3_RAW_IP_ALIGN 2

/* number of ETHTOOL_GSTATS u64's */
#define TG3_NUM_STATS		(sizeof(struct tg3_ethtool_stats)/sizeof(u64))

#define TG3_NUM_TEST		6

#define FIRMWARE_TG3		"tigon/tg3.bin"
#define FIRMWARE_TG3TSO		"tigon/tg3_tso.bin"
#define FIRMWARE_TG3TSO5	"tigon/tg3_tso5.bin"

static char version[] __devinitdata =
	DRV_MODULE_NAME ".c:v" DRV_MODULE_VERSION " (" DRV_MODULE_RELDATE ")\n";

MODULE_AUTHOR("David S. Miller (davem@redhat.com) and Jeff Garzik (jgarzik@pobox.com)");
MODULE_DESCRIPTION("Broadcom Tigon3 ethernet driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_MODULE_VERSION);
MODULE_FIRMWARE(FIRMWARE_TG3);
MODULE_FIRMWARE(FIRMWARE_TG3TSO);
MODULE_FIRMWARE(FIRMWARE_TG3TSO5);

#define TG3_RSS_MIN_NUM_MSIX_VECS	2

static int tg3_debug = -1;	/* -1 == use TG3_DEF_MSG_ENABLE as value */
module_param(tg3_debug, int, 0);
MODULE_PARM_DESC(tg3_debug, "Tigon3 bitmapped debugging message enable value");

static struct pci_device_id tg3_pci_tbl[] = {
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
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_TIGON3_5901)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_TIGON3_5901_2)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_TIGON3_5704S_2)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_TIGON3_5705F)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_TIGON3_5720)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_TIGON3_5721)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_TIGON3_5722)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_TIGON3_5750)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_TIGON3_5751)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_TIGON3_5750M)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_TIGON3_5751M)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_TIGON3_5751F)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_TIGON3_5752)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_TIGON3_5752M)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_TIGON3_5753)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_TIGON3_5753M)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_TIGON3_5753F)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_TIGON3_5754)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_TIGON3_5754M)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_TIGON3_5755)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_TIGON3_5755M)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_TIGON3_5756)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_TIGON3_5786)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_TIGON3_5787)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_TIGON3_5787M)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_TIGON3_5787F)},
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
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, TG3PCI_DEVICE_TIGON3_57780)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, TG3PCI_DEVICE_TIGON3_57760)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, TG3PCI_DEVICE_TIGON3_57790)},
	{PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, TG3PCI_DEVICE_TIGON3_57788)},
	{PCI_DEVICE(PCI_VENDOR_ID_SYSKONNECT, PCI_DEVICE_ID_SYSKONNECT_9DXX)},
	{PCI_DEVICE(PCI_VENDOR_ID_SYSKONNECT, PCI_DEVICE_ID_SYSKONNECT_9MXX)},
	{PCI_DEVICE(PCI_VENDOR_ID_ALTIMA, PCI_DEVICE_ID_ALTIMA_AC1000)},
	{PCI_DEVICE(PCI_VENDOR_ID_ALTIMA, PCI_DEVICE_ID_ALTIMA_AC1001)},
	{PCI_DEVICE(PCI_VENDOR_ID_ALTIMA, PCI_DEVICE_ID_ALTIMA_AC1003)},
	{PCI_DEVICE(PCI_VENDOR_ID_ALTIMA, PCI_DEVICE_ID_ALTIMA_AC9100)},
	{PCI_DEVICE(PCI_VENDOR_ID_APPLE, PCI_DEVICE_ID_APPLE_TIGON3)},
	{}
};

MODULE_DEVICE_TABLE(pci, tg3_pci_tbl);

static const struct {
	const char string[ETH_GSTRING_LEN];
} ethtool_stats_keys[TG3_NUM_STATS] = {
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
	{ "nic_tx_threshold_hit" }
};

static const struct {
	const char string[ETH_GSTRING_LEN];
} ethtool_test_keys[TG3_NUM_TEST] = {
	{ "nvram test     (online) " },
	{ "link test      (online) " },
	{ "register test  (offline)" },
	{ "memory test    (offline)" },
	{ "loopback test  (offline)" },
	{ "interrupt test (offline)" },
};

static void tg3_write32(struct tg3 *tp, u32 off, u32 val)
{
	writel(val, tp->regs + off);
}

static u32 tg3_read32(struct tg3 *tp, u32 off)
{
	return (readl(tp->regs + off));
}

static void tg3_ape_write32(struct tg3 *tp, u32 off, u32 val)
{
	writel(val, tp->aperegs + off);
}

static u32 tg3_ape_read32(struct tg3 *tp, u32 off)
{
	return (readl(tp->aperegs + off));
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
	if (off == (MAILBOX_RCV_STD_PROD_IDX + TG3_64BIT_REG_LOW)) {
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
	if ((tp->tg3_flags & TG3_FLAG_PCIX_TARGET_HWBUG) ||
	    (tp->tg3_flags2 & TG3_FLG2_ICH_WORKAROUND))
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
	if (!(tp->tg3_flags & TG3_FLAG_MBOX_WRITE_REORDER) &&
	    !(tp->tg3_flags2 & TG3_FLG2_ICH_WORKAROUND))
		tp->read32_mbox(tp, off);
}

static void tg3_write32_tx_mbox(struct tg3 *tp, u32 off, u32 val)
{
	void __iomem *mbox = tp->regs + off;
	writel(val, mbox);
	if (tp->tg3_flags & TG3_FLAG_TXD_MBOX_HWBUG)
		writel(val, mbox);
	if (tp->tg3_flags & TG3_FLAG_MBOX_WRITE_REORDER)
		readl(mbox);
}

static u32 tg3_read32_mbox_5906(struct tg3 *tp, u32 off)
{
	return (readl(tp->regs + off + GRCMBOX_BASE));
}

static void tg3_write32_mbox_5906(struct tg3 *tp, u32 off, u32 val)
{
	writel(val, tp->regs + off + GRCMBOX_BASE);
}

#define tw32_mailbox(reg, val)	tp->write32_mbox(tp, reg, val)
#define tw32_mailbox_f(reg, val)	tw32_mailbox_flush(tp, (reg), (val))
#define tw32_rx_mbox(reg, val)	tp->write32_rx_mbox(tp, reg, val)
#define tw32_tx_mbox(reg, val)	tp->write32_tx_mbox(tp, reg, val)
#define tr32_mailbox(reg)	tp->read32_mbox(tp, reg)

#define tw32(reg,val)		tp->write32(tp, reg, val)
#define tw32_f(reg,val)		_tw32_flush(tp,(reg),(val), 0)
#define tw32_wait_f(reg,val,us)	_tw32_flush(tp,(reg),(val), (us))
#define tr32(reg)		tp->read32(tp, reg)

static void tg3_write_mem(struct tg3 *tp, u32 off, u32 val)
{
	unsigned long flags;

	if ((GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5906) &&
	    (off >= NIC_SRAM_STATS_BLK) && (off < NIC_SRAM_TX_BUFFER_DESC))
		return;

	spin_lock_irqsave(&tp->indirect_lock, flags);
	if (tp->tg3_flags & TG3_FLAG_SRAM_USE_CONFIG) {
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

	if ((GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5906) &&
	    (off >= NIC_SRAM_STATS_BLK) && (off < NIC_SRAM_TX_BUFFER_DESC)) {
		*val = 0;
		return;
	}

	spin_lock_irqsave(&tp->indirect_lock, flags);
	if (tp->tg3_flags & TG3_FLAG_SRAM_USE_CONFIG) {
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

	/* Make sure the driver hasn't any stale locks. */
	for (i = 0; i < 8; i++)
		tg3_ape_write32(tp, TG3_APE_LOCK_GRANT + 4 * i,
				APE_LOCK_GRANT_DRIVER);
}

static int tg3_ape_lock(struct tg3 *tp, int locknum)
{
	int i, off;
	int ret = 0;
	u32 status;

	if (!(tp->tg3_flags3 & TG3_FLG3_ENABLE_APE))
		return 0;

	switch (locknum) {
		case TG3_APE_LOCK_GRC:
		case TG3_APE_LOCK_MEM:
			break;
		default:
			return -EINVAL;
	}

	off = 4 * locknum;

	tg3_ape_write32(tp, TG3_APE_LOCK_REQ + off, APE_LOCK_REQ_DRIVER);

	/* Wait for up to 1 millisecond to acquire lock. */
	for (i = 0; i < 100; i++) {
		status = tg3_ape_read32(tp, TG3_APE_LOCK_GRANT + off);
		if (status == APE_LOCK_GRANT_DRIVER)
			break;
		udelay(10);
	}

	if (status != APE_LOCK_GRANT_DRIVER) {
		/* Revoke the lock request. */
		tg3_ape_write32(tp, TG3_APE_LOCK_GRANT + off,
				APE_LOCK_GRANT_DRIVER);

		ret = -EBUSY;
	}

	return ret;
}

static void tg3_ape_unlock(struct tg3 *tp, int locknum)
{
	int off;

	if (!(tp->tg3_flags3 & TG3_FLG3_ENABLE_APE))
		return;

	switch (locknum) {
		case TG3_APE_LOCK_GRC:
		case TG3_APE_LOCK_MEM:
			break;
		default:
			return;
	}

	off = 4 * locknum;
	tg3_ape_write32(tp, TG3_APE_LOCK_GRANT + off, APE_LOCK_GRANT_DRIVER);
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
	u32 coal_now = 0;

	tp->irq_sync = 0;
	wmb();

	tw32(TG3PCI_MISC_HOST_CTRL,
	     (tp->misc_host_ctrl & ~MISC_HOST_CTRL_MASK_PCI_INT));

	for (i = 0; i < tp->irq_cnt; i++) {
		struct tg3_napi *tnapi = &tp->napi[i];
		tw32_mailbox_f(tnapi->int_mbox, tnapi->last_tag << 24);
		if (tp->tg3_flags2 & TG3_FLG2_1SHOT_MSI)
			tw32_mailbox_f(tnapi->int_mbox, tnapi->last_tag << 24);

		coal_now |= tnapi->coal_now;
	}

	/* Force an initial interrupt */
	if (!(tp->tg3_flags & TG3_FLAG_TAGGED_STATUS) &&
	    (tp->napi[0].hw_status->status & SD_STATUS_UPDATED))
		tw32(GRC_LOCAL_CTRL, tp->grc_local_ctrl | GRC_LCLCTRL_SETINT);
	else
		tw32(HOSTCC_MODE, tp->coalesce_mode |
		     HOSTCC_MODE_ENABLE | coal_now);
}

static inline unsigned int tg3_has_work(struct tg3_napi *tnapi)
{
	struct tg3 *tp = tnapi->tp;
	struct tg3_hw_status *sblk = tnapi->hw_status;
	unsigned int work_exists = 0;

	/* check for phy events */
	if (!(tp->tg3_flags &
	      (TG3_FLAG_USE_LINKCHG_REG |
	       TG3_FLAG_POLL_SERDES))) {
		if (sblk->status & SD_STATUS_LINK_CHG)
			work_exists = 1;
	}
	/* check for RX/TX work to do */
	if (sblk->idx[0].tx_consumer != tnapi->tx_cons ||
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
	if (!(tp->tg3_flags & TG3_FLAG_TAGGED_STATUS) &&
	    tg3_has_work(tnapi))
		tw32(HOSTCC_MODE, tp->coalesce_mode |
		     HOSTCC_MODE_ENABLE | tnapi->coal_now);
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

static inline void tg3_netif_stop(struct tg3 *tp)
{
	tp->dev->trans_start = jiffies;	/* prevent tx timeout */
	tg3_napi_disable(tp);
	netif_tx_disable(tp->dev);
}

static inline void tg3_netif_start(struct tg3 *tp)
{
	/* NOTE: unconditional netif_tx_wake_all_queues is only
	 * appropriate so long as all callers are assured to
	 * have free tx slots (such as after tg3_init_hw)
	 */
	netif_tx_wake_all_queues(tp->dev);

	tg3_napi_enable(tp);
	tp->napi[0].hw_status->status |= SD_STATUS_UPDATED;
	tg3_enable_ints(tp);
}

static void tg3_switch_clocks(struct tg3 *tp)
{
	u32 clock_ctrl;
	u32 orig_clock_ctrl;

	if ((tp->tg3_flags & TG3_FLAG_CPMU_PRESENT) ||
	    (tp->tg3_flags2 & TG3_FLG2_5780_CLASS))
		return;

	clock_ctrl = tr32(TG3PCI_CLOCK_CTRL);

	orig_clock_ctrl = clock_ctrl;
	clock_ctrl &= (CLOCK_CTRL_FORCE_CLKRUN |
		       CLOCK_CTRL_CLKRUN_OENABLE |
		       0x1f);
	tp->pci_clock_ctrl = clock_ctrl;

	if (tp->tg3_flags2 & TG3_FLG2_5705_PLUS) {
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

static int tg3_readphy(struct tg3 *tp, int reg, u32 *val)
{
	u32 frame_val;
	unsigned int loops;
	int ret;

	if ((tp->mi_mode & MAC_MI_MODE_AUTO_POLL) != 0) {
		tw32_f(MAC_MI_MODE,
		     (tp->mi_mode & ~MAC_MI_MODE_AUTO_POLL));
		udelay(80);
	}

	*val = 0x0;

	frame_val  = ((tp->phy_addr << MI_COM_PHY_ADDR_SHIFT) &
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

	return ret;
}

static int tg3_writephy(struct tg3 *tp, int reg, u32 val)
{
	u32 frame_val;
	unsigned int loops;
	int ret;

	if ((tp->tg3_flags3 & TG3_FLG3_PHY_IS_FET) &&
	    (reg == MII_TG3_CTRL || reg == MII_TG3_AUX_CTRL))
		return 0;

	if ((tp->mi_mode & MAC_MI_MODE_AUTO_POLL) != 0) {
		tw32_f(MAC_MI_MODE,
		     (tp->mi_mode & ~MAC_MI_MODE_AUTO_POLL));
		udelay(80);
	}

	frame_val  = ((tp->phy_addr << MI_COM_PHY_ADDR_SHIFT) &
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

	return ret;
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

	phydev = tp->mdio_bus->phy_map[PHY_ADDR];
	switch (phydev->drv->phy_id & phydev->drv->phy_id_mask) {
	case TG3_PHY_ID_BCM50610:
		val = MAC_PHYCFG2_50610_LED_MODES;
		break;
	case TG3_PHY_ID_BCMAC131:
		val = MAC_PHYCFG2_AC131_LED_MODES;
		break;
	case TG3_PHY_ID_RTL8211C:
		val = MAC_PHYCFG2_RTL8211C_LED_MODES;
		break;
	case TG3_PHY_ID_RTL8201E:
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

	if (!(tp->tg3_flags3 & TG3_FLG3_RGMII_STD_IBND_DISABLE))
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
	if (!(tp->tg3_flags3 & TG3_FLG3_RGMII_STD_IBND_DISABLE)) {
		if (tp->tg3_flags3 & TG3_FLG3_RGMII_EXT_IBND_RX_EN)
			val |= MAC_PHYCFG1_RGMII_EXT_RX_DEC;
		if (tp->tg3_flags3 & TG3_FLG3_RGMII_EXT_IBND_TX_EN)
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
	if (!(tp->tg3_flags3 & TG3_FLG3_RGMII_STD_IBND_DISABLE)) {
		if (tp->tg3_flags3 & TG3_FLG3_RGMII_EXT_IBND_RX_EN)
			val |= MAC_RGMII_MODE_RX_INT_B |
			       MAC_RGMII_MODE_RX_QUALITY |
			       MAC_RGMII_MODE_RX_ACTIVITY |
			       MAC_RGMII_MODE_RX_ENG_DET;
		if (tp->tg3_flags3 & TG3_FLG3_RGMII_EXT_IBND_TX_EN)
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

	if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5717) {
		u32 funcnum, is_serdes;

		funcnum = tr32(TG3_CPMU_STATUS) & TG3_CPMU_STATUS_PCIE_FUNC;
		if (funcnum)
			tp->phy_addr = 2;
		else
			tp->phy_addr = 1;

		is_serdes = tr32(SG_DIG_STATUS) & SG_DIG_IS_SERDES;
		if (is_serdes)
			tp->phy_addr += 7;
	} else
		tp->phy_addr = PHY_ADDR;

	if ((tp->tg3_flags3 & TG3_FLG3_MDIOBUS_INITED) &&
	    GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5785)
		tg3_mdio_config_5785(tp);
}

static int tg3_mdio_init(struct tg3 *tp)
{
	int i;
	u32 reg;
	struct phy_device *phydev;

	tg3_mdio_start(tp);

	if (!(tp->tg3_flags3 & TG3_FLG3_USE_PHYLIB) ||
	    (tp->tg3_flags3 & TG3_FLG3_MDIOBUS_INITED))
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
	tp->mdio_bus->phy_mask = ~(1 << PHY_ADDR);
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
		printk(KERN_WARNING "%s: mdiobus_reg failed (0x%x)\n",
			tp->dev->name, i);
		mdiobus_free(tp->mdio_bus);
		return i;
	}

	phydev = tp->mdio_bus->phy_map[PHY_ADDR];

	if (!phydev || !phydev->drv) {
		printk(KERN_WARNING "%s: No PHY devices\n", tp->dev->name);
		mdiobus_unregister(tp->mdio_bus);
		mdiobus_free(tp->mdio_bus);
		return -ENODEV;
	}

	switch (phydev->drv->phy_id & phydev->drv->phy_id_mask) {
	case TG3_PHY_ID_BCM57780:
		phydev->interface = PHY_INTERFACE_MODE_GMII;
		break;
	case TG3_PHY_ID_BCM50610:
		if (tp->tg3_flags3 & TG3_FLG3_RGMII_STD_IBND_DISABLE)
			phydev->dev_flags |= PHY_BRCM_STD_IBND_DISABLE;
		if (tp->tg3_flags3 & TG3_FLG3_RGMII_EXT_IBND_RX_EN)
			phydev->dev_flags |= PHY_BRCM_EXT_IBND_RX_ENABLE;
		if (tp->tg3_flags3 & TG3_FLG3_RGMII_EXT_IBND_TX_EN)
			phydev->dev_flags |= PHY_BRCM_EXT_IBND_TX_ENABLE;
		/* fallthru */
	case TG3_PHY_ID_RTL8211C:
		phydev->interface = PHY_INTERFACE_MODE_RGMII;
		break;
	case TG3_PHY_ID_RTL8201E:
	case TG3_PHY_ID_BCMAC131:
		phydev->interface = PHY_INTERFACE_MODE_MII;
		tp->tg3_flags3 |= TG3_FLG3_PHY_IS_FET;
		break;
	}

	tp->tg3_flags3 |= TG3_FLG3_MDIOBUS_INITED;

	if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5785)
		tg3_mdio_config_5785(tp);

	return 0;
}

static void tg3_mdio_fini(struct tg3 *tp)
{
	if (tp->tg3_flags3 & TG3_FLG3_MDIOBUS_INITED) {
		tp->tg3_flags3 &= ~TG3_FLG3_MDIOBUS_INITED;
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
		udelay(8);
	}
}

/* tp->lock is held. */
static void tg3_ump_link_report(struct tg3 *tp)
{
	u32 reg;
	u32 val;

	if (!(tp->tg3_flags2 & TG3_FLG2_5780_CLASS) ||
	    !(tp->tg3_flags  & TG3_FLAG_ENABLE_ASF))
		return;

	tg3_wait_for_event_ack(tp);

	tg3_write_mem(tp, NIC_SRAM_FW_CMD_MBOX, FWCMD_NICDRV_LINK_UPDATE);

	tg3_write_mem(tp, NIC_SRAM_FW_CMD_LEN_MBOX, 14);

	val = 0;
	if (!tg3_readphy(tp, MII_BMCR, &reg))
		val = reg << 16;
	if (!tg3_readphy(tp, MII_BMSR, &reg))
		val |= (reg & 0xffff);
	tg3_write_mem(tp, NIC_SRAM_FW_CMD_DATA_MBOX, val);

	val = 0;
	if (!tg3_readphy(tp, MII_ADVERTISE, &reg))
		val = reg << 16;
	if (!tg3_readphy(tp, MII_LPA, &reg))
		val |= (reg & 0xffff);
	tg3_write_mem(tp, NIC_SRAM_FW_CMD_DATA_MBOX + 4, val);

	val = 0;
	if (!(tp->tg3_flags2 & TG3_FLG2_MII_SERDES)) {
		if (!tg3_readphy(tp, MII_CTRL1000, &reg))
			val = reg << 16;
		if (!tg3_readphy(tp, MII_STAT1000, &reg))
			val |= (reg & 0xffff);
	}
	tg3_write_mem(tp, NIC_SRAM_FW_CMD_DATA_MBOX + 8, val);

	if (!tg3_readphy(tp, MII_PHYADDR, &reg))
		val = reg << 16;
	else
		val = 0;
	tg3_write_mem(tp, NIC_SRAM_FW_CMD_DATA_MBOX + 12, val);

	tg3_generate_fw_event(tp);
}

static void tg3_link_report(struct tg3 *tp)
{
	if (!netif_carrier_ok(tp->dev)) {
		if (netif_msg_link(tp))
			printk(KERN_INFO PFX "%s: Link is down.\n",
			       tp->dev->name);
		tg3_ump_link_report(tp);
	} else if (netif_msg_link(tp)) {
		printk(KERN_INFO PFX "%s: Link is up at %d Mbps, %s duplex.\n",
		       tp->dev->name,
		       (tp->link_config.active_speed == SPEED_1000 ?
			1000 :
			(tp->link_config.active_speed == SPEED_100 ?
			 100 : 10)),
		       (tp->link_config.active_duplex == DUPLEX_FULL ?
			"full" : "half"));

		printk(KERN_INFO PFX
		       "%s: Flow control is %s for TX and %s for RX.\n",
		       tp->dev->name,
		       (tp->link_config.active_flowctrl & FLOW_CTRL_TX) ?
		       "on" : "off",
		       (tp->link_config.active_flowctrl & FLOW_CTRL_RX) ?
		       "on" : "off");
		tg3_ump_link_report(tp);
	}
}

static u16 tg3_advert_flowctrl_1000T(u8 flow_ctrl)
{
	u16 miireg;

	if ((flow_ctrl & FLOW_CTRL_TX) && (flow_ctrl & FLOW_CTRL_RX))
		miireg = ADVERTISE_PAUSE_CAP;
	else if (flow_ctrl & FLOW_CTRL_TX)
		miireg = ADVERTISE_PAUSE_ASYM;
	else if (flow_ctrl & FLOW_CTRL_RX)
		miireg = ADVERTISE_PAUSE_CAP | ADVERTISE_PAUSE_ASYM;
	else
		miireg = 0;

	return miireg;
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

static u8 tg3_resolve_flowctrl_1000X(u16 lcladv, u16 rmtadv)
{
	u8 cap = 0;

	if (lcladv & ADVERTISE_1000XPAUSE) {
		if (lcladv & ADVERTISE_1000XPSE_ASYM) {
			if (rmtadv & LPA_1000XPAUSE)
				cap = FLOW_CTRL_TX | FLOW_CTRL_RX;
			else if (rmtadv & LPA_1000XPAUSE_ASYM)
				cap = FLOW_CTRL_RX;
		} else {
			if (rmtadv & LPA_1000XPAUSE)
				cap = FLOW_CTRL_TX | FLOW_CTRL_RX;
		}
	} else if (lcladv & ADVERTISE_1000XPSE_ASYM) {
		if ((rmtadv & LPA_1000XPAUSE) && (rmtadv & LPA_1000XPAUSE_ASYM))
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

	if (tp->tg3_flags3 & TG3_FLG3_USE_PHYLIB)
		autoneg = tp->mdio_bus->phy_map[PHY_ADDR]->autoneg;
	else
		autoneg = tp->link_config.autoneg;

	if (autoneg == AUTONEG_ENABLE &&
	    (tp->tg3_flags & TG3_FLAG_PAUSE_AUTONEG)) {
		if (tp->tg3_flags2 & TG3_FLG2_ANY_SERDES)
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
	struct phy_device *phydev = tp->mdio_bus->phy_map[PHY_ADDR];

	spin_lock_bh(&tp->lock);

	mac_mode = tp->mac_mode & ~(MAC_MODE_PORT_MODE_MASK |
				    MAC_MODE_HALF_DUPLEX);

	oldflowctrl = tp->link_config.active_flowctrl;

	if (phydev->link) {
		lcl_adv = 0;
		rmt_adv = 0;

		if (phydev->speed == SPEED_100 || phydev->speed == SPEED_10)
			mac_mode |= MAC_MODE_PORT_MODE_MII;
		else
			mac_mode |= MAC_MODE_PORT_MODE_GMII;

		if (phydev->duplex == DUPLEX_HALF)
			mac_mode |= MAC_MODE_HALF_DUPLEX;
		else {
			lcl_adv = tg3_advert_flowctrl_1000T(
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

	if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5785) {
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

	if ((phydev->link && tp->link_config.active_speed == SPEED_INVALID) ||
	    (!phydev->link && tp->link_config.active_speed != SPEED_INVALID) ||
	    phydev->speed != tp->link_config.active_speed ||
	    phydev->duplex != tp->link_config.active_duplex ||
	    oldflowctrl != tp->link_config.active_flowctrl)
	    linkmesg = 1;

	tp->link_config.active_speed = phydev->speed;
	tp->link_config.active_duplex = phydev->duplex;

	spin_unlock_bh(&tp->lock);

	if (linkmesg)
		tg3_link_report(tp);
}

static int tg3_phy_init(struct tg3 *tp)
{
	struct phy_device *phydev;

	if (tp->tg3_flags3 & TG3_FLG3_PHY_CONNECTED)
		return 0;

	/* Bring the PHY back to a known state. */
	tg3_bmcr_reset(tp);

	phydev = tp->mdio_bus->phy_map[PHY_ADDR];

	/* Attach the MAC to the PHY. */
	phydev = phy_connect(tp->dev, dev_name(&phydev->dev), tg3_adjust_link,
			     phydev->dev_flags, phydev->interface);
	if (IS_ERR(phydev)) {
		printk(KERN_ERR "%s: Could not attach to PHY\n", tp->dev->name);
		return PTR_ERR(phydev);
	}

	/* Mask with MAC supported features. */
	switch (phydev->interface) {
	case PHY_INTERFACE_MODE_GMII:
	case PHY_INTERFACE_MODE_RGMII:
		if (!(tp->tg3_flags & TG3_FLAG_10_100_ONLY)) {
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
		phy_disconnect(tp->mdio_bus->phy_map[PHY_ADDR]);
		return -EINVAL;
	}

	tp->tg3_flags3 |= TG3_FLG3_PHY_CONNECTED;

	phydev->advertising = phydev->supported;

	return 0;
}

static void tg3_phy_start(struct tg3 *tp)
{
	struct phy_device *phydev;

	if (!(tp->tg3_flags3 & TG3_FLG3_PHY_CONNECTED))
		return;

	phydev = tp->mdio_bus->phy_map[PHY_ADDR];

	if (tp->link_config.phy_is_low_power) {
		tp->link_config.phy_is_low_power = 0;
		phydev->speed = tp->link_config.orig_speed;
		phydev->duplex = tp->link_config.orig_duplex;
		phydev->autoneg = tp->link_config.orig_autoneg;
		phydev->advertising = tp->link_config.orig_advertising;
	}

	phy_start(phydev);

	phy_start_aneg(phydev);
}

static void tg3_phy_stop(struct tg3 *tp)
{
	if (!(tp->tg3_flags3 & TG3_FLG3_PHY_CONNECTED))
		return;

	phy_stop(tp->mdio_bus->phy_map[PHY_ADDR]);
}

static void tg3_phy_fini(struct tg3 *tp)
{
	if (tp->tg3_flags3 & TG3_FLG3_PHY_CONNECTED) {
		phy_disconnect(tp->mdio_bus->phy_map[PHY_ADDR]);
		tp->tg3_flags3 &= ~TG3_FLG3_PHY_CONNECTED;
	}
}

static void tg3_phydsp_write(struct tg3 *tp, u32 reg, u32 val)
{
	tg3_writephy(tp, MII_TG3_DSP_ADDRESS, reg);
	tg3_writephy(tp, MII_TG3_DSP_RW_PORT, val);
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

	if (!(tp->tg3_flags2 & TG3_FLG2_5705_PLUS))
		return;

	if (tp->tg3_flags3 & TG3_FLG3_PHY_IS_FET) {
		tg3_phy_fet_toggle_apd(tp, enable);
		return;
	}

	reg = MII_TG3_MISC_SHDW_WREN |
	      MII_TG3_MISC_SHDW_SCR5_SEL |
	      MII_TG3_MISC_SHDW_SCR5_LPED |
	      MII_TG3_MISC_SHDW_SCR5_DLPTLM |
	      MII_TG3_MISC_SHDW_SCR5_SDTL |
	      MII_TG3_MISC_SHDW_SCR5_C125OE;
	if (GET_ASIC_REV(tp->pci_chip_rev_id) != ASIC_REV_5784 || !enable)
		reg |= MII_TG3_MISC_SHDW_SCR5_DLLAPD;

	tg3_writephy(tp, MII_TG3_MISC_SHDW, reg);


	reg = MII_TG3_MISC_SHDW_WREN |
	      MII_TG3_MISC_SHDW_APD_SEL |
	      MII_TG3_MISC_SHDW_APD_WKTM_84MS;
	if (enable)
		reg |= MII_TG3_MISC_SHDW_APD_ENABLE;

	tg3_writephy(tp, MII_TG3_MISC_SHDW, reg);
}

static void tg3_phy_toggle_automdix(struct tg3 *tp, int enable)
{
	u32 phy;

	if (!(tp->tg3_flags2 & TG3_FLG2_5705_PLUS) ||
	    (tp->tg3_flags2 & TG3_FLG2_ANY_SERDES))
		return;

	if (tp->tg3_flags3 & TG3_FLG3_PHY_IS_FET) {
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
		phy = MII_TG3_AUXCTL_MISC_RDSEL_MISC |
		      MII_TG3_AUXCTL_SHDWSEL_MISC;
		if (!tg3_writephy(tp, MII_TG3_AUX_CTRL, phy) &&
		    !tg3_readphy(tp, MII_TG3_AUX_CTRL, &phy)) {
			if (enable)
				phy |= MII_TG3_AUXCTL_MISC_FORCE_AMDIX;
			else
				phy &= ~MII_TG3_AUXCTL_MISC_FORCE_AMDIX;
			phy |= MII_TG3_AUXCTL_MISC_WREN;
			tg3_writephy(tp, MII_TG3_AUX_CTRL, phy);
		}
	}
}

static void tg3_phy_set_wirespeed(struct tg3 *tp)
{
	u32 val;

	if (tp->tg3_flags2 & TG3_FLG2_NO_ETH_WIRE_SPEED)
		return;

	if (!tg3_writephy(tp, MII_TG3_AUX_CTRL, 0x7007) &&
	    !tg3_readphy(tp, MII_TG3_AUX_CTRL, &val))
		tg3_writephy(tp, MII_TG3_AUX_CTRL,
			     (val | (1 << 15) | (1 << 4)));
}

static void tg3_phy_apply_otp(struct tg3 *tp)
{
	u32 otp, phy;

	if (!tp->phy_otp)
		return;

	otp = tp->phy_otp;

	/* Enable SM_DSP clock and tx 6dB coding. */
	phy = MII_TG3_AUXCTL_SHDWSEL_AUXCTL |
	      MII_TG3_AUXCTL_ACTL_SMDSP_ENA |
	      MII_TG3_AUXCTL_ACTL_TX_6DB;
	tg3_writephy(tp, MII_TG3_AUX_CTRL, phy);

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

	/* Turn off SM_DSP clock. */
	phy = MII_TG3_AUXCTL_SHDWSEL_AUXCTL |
	      MII_TG3_AUXCTL_ACTL_TX_6DB;
	tg3_writephy(tp, MII_TG3_AUX_CTRL, phy);
}

static int tg3_wait_macro_done(struct tg3 *tp)
{
	int limit = 100;

	while (limit--) {
		u32 tmp32;

		if (!tg3_readphy(tp, 0x16, &tmp32)) {
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
		tg3_writephy(tp, 0x16, 0x0002);

		for (i = 0; i < 6; i++)
			tg3_writephy(tp, MII_TG3_DSP_RW_PORT,
				     test_pat[chan][i]);

		tg3_writephy(tp, 0x16, 0x0202);
		if (tg3_wait_macro_done(tp)) {
			*resetp = 1;
			return -EBUSY;
		}

		tg3_writephy(tp, MII_TG3_DSP_ADDRESS,
			     (chan * 0x2000) | 0x0200);
		tg3_writephy(tp, 0x16, 0x0082);
		if (tg3_wait_macro_done(tp)) {
			*resetp = 1;
			return -EBUSY;
		}

		tg3_writephy(tp, 0x16, 0x0802);
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
		tg3_writephy(tp, 0x16, 0x0002);
		for (i = 0; i < 6; i++)
			tg3_writephy(tp, MII_TG3_DSP_RW_PORT, 0x000);
		tg3_writephy(tp, 0x16, 0x0202);
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
			     BMCR_FULLDPLX | TG3_BMCR_SPEED1000);

		/* Set to master mode.  */
		if (tg3_readphy(tp, MII_TG3_CTRL, &phy9_orig))
			continue;

		tg3_writephy(tp, MII_TG3_CTRL,
			     (MII_TG3_CTRL_AS_MASTER |
			      MII_TG3_CTRL_ENABLE_AS_MASTER));

		/* Enable SM_DSP_CLOCK and 6dB.  */
		tg3_writephy(tp, MII_TG3_AUX_CTRL, 0x0c00);

		/* Block the PHY control access.  */
		tg3_writephy(tp, MII_TG3_DSP_ADDRESS, 0x8005);
		tg3_writephy(tp, MII_TG3_DSP_RW_PORT, 0x0800);

		err = tg3_phy_write_and_check_testpat(tp, &do_phy_reset);
		if (!err)
			break;
	} while (--retries);

	err = tg3_phy_reset_chanpat(tp);
	if (err)
		return err;

	tg3_writephy(tp, MII_TG3_DSP_ADDRESS, 0x8005);
	tg3_writephy(tp, MII_TG3_DSP_RW_PORT, 0x0000);

	tg3_writephy(tp, MII_TG3_DSP_ADDRESS, 0x8200);
	tg3_writephy(tp, 0x16, 0x0000);

	if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5703 ||
	    GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5704) {
		/* Set Extended packet length bit for jumbo frames */
		tg3_writephy(tp, MII_TG3_AUX_CTRL, 0x4400);
	}
	else {
		tg3_writephy(tp, MII_TG3_AUX_CTRL, 0x0400);
	}

	tg3_writephy(tp, MII_TG3_CTRL, phy9_orig);

	if (!tg3_readphy(tp, MII_TG3_EXT_CTRL, &reg32)) {
		reg32 &= ~0x3000;
		tg3_writephy(tp, MII_TG3_EXT_CTRL, reg32);
	} else if (!err)
		err = -EBUSY;

	return err;
}

/* This will reset the tigon3 PHY if there is no valid
 * link unless the FORCE argument is non-zero.
 */
static int tg3_phy_reset(struct tg3 *tp)
{
	u32 cpmuctrl;
	u32 phy_status;
	int err;

	if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5906) {
		u32 val;

		val = tr32(GRC_MISC_CFG);
		tw32_f(GRC_MISC_CFG, val & ~GRC_MISC_CFG_EPHY_IDDQ);
		udelay(40);
	}
	err  = tg3_readphy(tp, MII_BMSR, &phy_status);
	err |= tg3_readphy(tp, MII_BMSR, &phy_status);
	if (err != 0)
		return -EBUSY;

	if (netif_running(tp->dev) && netif_carrier_ok(tp->dev)) {
		netif_carrier_off(tp->dev);
		tg3_link_report(tp);
	}

	if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5703 ||
	    GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5704 ||
	    GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5705) {
		err = tg3_phy_reset_5703_4_5(tp);
		if (err)
			return err;
		goto out;
	}

	cpmuctrl = 0;
	if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5784 &&
	    GET_CHIP_REV(tp->pci_chip_rev_id) != CHIPREV_5784_AX) {
		cpmuctrl = tr32(TG3_CPMU_CTRL);
		if (cpmuctrl & CPMU_CTRL_GPHY_10MB_RXONLY)
			tw32(TG3_CPMU_CTRL,
			     cpmuctrl & ~CPMU_CTRL_GPHY_10MB_RXONLY);
	}

	err = tg3_bmcr_reset(tp);
	if (err)
		return err;

	if (cpmuctrl & CPMU_CTRL_GPHY_10MB_RXONLY) {
		u32 phy;

		phy = MII_TG3_DSP_EXP8_AEDW | MII_TG3_DSP_EXP8_REJ2MHz;
		tg3_phydsp_write(tp, MII_TG3_DSP_EXP8, phy);

		tw32(TG3_CPMU_CTRL, cpmuctrl);
	}

	if (GET_CHIP_REV(tp->pci_chip_rev_id) == CHIPREV_5784_AX ||
	    GET_CHIP_REV(tp->pci_chip_rev_id) == CHIPREV_5761_AX) {
		u32 val;

		val = tr32(TG3_CPMU_LSPD_1000MB_CLK);
		if ((val & CPMU_LSPD_1000MB_MACCLK_MASK) ==
		    CPMU_LSPD_1000MB_MACCLK_12_5) {
			val &= ~CPMU_LSPD_1000MB_MACCLK_MASK;
			udelay(40);
			tw32_f(TG3_CPMU_LSPD_1000MB_CLK, val);
		}
	}

	tg3_phy_apply_otp(tp);

	if (tp->tg3_flags3 & TG3_FLG3_PHY_ENABLE_APD)
		tg3_phy_toggle_apd(tp, true);
	else
		tg3_phy_toggle_apd(tp, false);

out:
	if (tp->tg3_flags2 & TG3_FLG2_PHY_ADC_BUG) {
		tg3_writephy(tp, MII_TG3_AUX_CTRL, 0x0c00);
		tg3_writephy(tp, MII_TG3_DSP_ADDRESS, 0x201f);
		tg3_writephy(tp, MII_TG3_DSP_RW_PORT, 0x2aaa);
		tg3_writephy(tp, MII_TG3_DSP_ADDRESS, 0x000a);
		tg3_writephy(tp, MII_TG3_DSP_RW_PORT, 0x0323);
		tg3_writephy(tp, MII_TG3_AUX_CTRL, 0x0400);
	}
	if (tp->tg3_flags2 & TG3_FLG2_PHY_5704_A0_BUG) {
		tg3_writephy(tp, 0x1c, 0x8d68);
		tg3_writephy(tp, 0x1c, 0x8d68);
	}
	if (tp->tg3_flags2 & TG3_FLG2_PHY_BER_BUG) {
		tg3_writephy(tp, MII_TG3_AUX_CTRL, 0x0c00);
		tg3_writephy(tp, MII_TG3_DSP_ADDRESS, 0x000a);
		tg3_writephy(tp, MII_TG3_DSP_RW_PORT, 0x310b);
		tg3_writephy(tp, MII_TG3_DSP_ADDRESS, 0x201f);
		tg3_writephy(tp, MII_TG3_DSP_RW_PORT, 0x9506);
		tg3_writephy(tp, MII_TG3_DSP_ADDRESS, 0x401f);
		tg3_writephy(tp, MII_TG3_DSP_RW_PORT, 0x14e2);
		tg3_writephy(tp, MII_TG3_AUX_CTRL, 0x0400);
	}
	else if (tp->tg3_flags2 & TG3_FLG2_PHY_JITTER_BUG) {
		tg3_writephy(tp, MII_TG3_AUX_CTRL, 0x0c00);
		tg3_writephy(tp, MII_TG3_DSP_ADDRESS, 0x000a);
		if (tp->tg3_flags2 & TG3_FLG2_PHY_ADJUST_TRIM) {
			tg3_writephy(tp, MII_TG3_DSP_RW_PORT, 0x110b);
			tg3_writephy(tp, MII_TG3_TEST1,
				     MII_TG3_TEST1_TRIM_EN | 0x4);
		} else
			tg3_writephy(tp, MII_TG3_DSP_RW_PORT, 0x010b);
		tg3_writephy(tp, MII_TG3_AUX_CTRL, 0x0400);
	}
	/* Set Extended packet length bit (bit 14) on all chips that */
	/* support jumbo frames */
	if ((tp->phy_id & PHY_ID_MASK) == PHY_ID_BCM5401) {
		/* Cannot do read-modify-write on 5401 */
		tg3_writephy(tp, MII_TG3_AUX_CTRL, 0x4c20);
	} else if (tp->tg3_flags & TG3_FLAG_JUMBO_CAPABLE) {
		u32 phy_reg;

		/* Set bit 14 with read-modify-write to preserve other bits */
		if (!tg3_writephy(tp, MII_TG3_AUX_CTRL, 0x0007) &&
		    !tg3_readphy(tp, MII_TG3_AUX_CTRL, &phy_reg))
			tg3_writephy(tp, MII_TG3_AUX_CTRL, phy_reg | 0x4000);
	}

	/* Set phy register 0x10 bit 0 to high fifo elasticity to support
	 * jumbo frames transmission.
	 */
	if (tp->tg3_flags & TG3_FLAG_JUMBO_CAPABLE) {
		u32 phy_reg;

		if (!tg3_readphy(tp, MII_TG3_EXT_CTRL, &phy_reg))
		    tg3_writephy(tp, MII_TG3_EXT_CTRL,
				 phy_reg | MII_TG3_EXT_CTRL_FIFO_ELASTIC);
	}

	if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5906) {
		/* adjust output voltage */
		tg3_writephy(tp, MII_TG3_FET_PTEST, 0x12);
	}

	tg3_phy_toggle_automdix(tp, 1);
	tg3_phy_set_wirespeed(tp);
	return 0;
}

static void tg3_frob_aux_power(struct tg3 *tp)
{
	struct tg3 *tp_peer = tp;

	if ((tp->tg3_flags2 & TG3_FLG2_IS_NIC) == 0)
		return;

	if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5704 ||
	    GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5714 ||
	    GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5717) {
		struct net_device *dev_peer;

		dev_peer = pci_get_drvdata(tp->pdev_peer);
		/* remove_one() may have been run on the peer. */
		if (!dev_peer)
			tp_peer = tp;
		else
			tp_peer = netdev_priv(dev_peer);
	}

	if ((tp->tg3_flags & TG3_FLAG_WOL_ENABLE) != 0 ||
	    (tp->tg3_flags & TG3_FLAG_ENABLE_ASF) != 0 ||
	    (tp_peer->tg3_flags & TG3_FLAG_WOL_ENABLE) != 0 ||
	    (tp_peer->tg3_flags & TG3_FLAG_ENABLE_ASF) != 0) {
		if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5700 ||
		    GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5701) {
			tw32_wait_f(GRC_LOCAL_CTRL, tp->grc_local_ctrl |
				    (GRC_LCLCTRL_GPIO_OE0 |
				     GRC_LCLCTRL_GPIO_OE1 |
				     GRC_LCLCTRL_GPIO_OE2 |
				     GRC_LCLCTRL_GPIO_OUTPUT0 |
				     GRC_LCLCTRL_GPIO_OUTPUT1),
				    100);
		} else if (tp->pdev->device == PCI_DEVICE_ID_TIGON3_5761 ||
			   tp->pdev->device == TG3PCI_DEVICE_TIGON3_5761S) {
			/* The 5761 non-e device swaps GPIO 0 and GPIO 2. */
			u32 grc_local_ctrl = GRC_LCLCTRL_GPIO_OE0 |
					     GRC_LCLCTRL_GPIO_OE1 |
					     GRC_LCLCTRL_GPIO_OE2 |
					     GRC_LCLCTRL_GPIO_OUTPUT0 |
					     GRC_LCLCTRL_GPIO_OUTPUT1 |
					     tp->grc_local_ctrl;
			tw32_wait_f(GRC_LOCAL_CTRL, grc_local_ctrl, 100);

			grc_local_ctrl |= GRC_LCLCTRL_GPIO_OUTPUT2;
			tw32_wait_f(GRC_LOCAL_CTRL, grc_local_ctrl, 100);

			grc_local_ctrl &= ~GRC_LCLCTRL_GPIO_OUTPUT0;
			tw32_wait_f(GRC_LOCAL_CTRL, grc_local_ctrl, 100);
		} else {
			u32 no_gpio2;
			u32 grc_local_ctrl = 0;

			if (tp_peer != tp &&
			    (tp_peer->tg3_flags & TG3_FLAG_INIT_COMPLETE) != 0)
				return;

			/* Workaround to prevent overdrawing Amps. */
			if (GET_ASIC_REV(tp->pci_chip_rev_id) ==
			    ASIC_REV_5714) {
				grc_local_ctrl |= GRC_LCLCTRL_GPIO_OE3;
				tw32_wait_f(GRC_LOCAL_CTRL, tp->grc_local_ctrl |
					    grc_local_ctrl, 100);
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
			tw32_wait_f(GRC_LOCAL_CTRL, tp->grc_local_ctrl |
						    grc_local_ctrl, 100);

			grc_local_ctrl |= GRC_LCLCTRL_GPIO_OUTPUT0;

			tw32_wait_f(GRC_LOCAL_CTRL, tp->grc_local_ctrl |
						    grc_local_ctrl, 100);

			if (!no_gpio2) {
				grc_local_ctrl &= ~GRC_LCLCTRL_GPIO_OUTPUT2;
				tw32_wait_f(GRC_LOCAL_CTRL, tp->grc_local_ctrl |
					    grc_local_ctrl, 100);
			}
		}
	} else {
		if (GET_ASIC_REV(tp->pci_chip_rev_id) != ASIC_REV_5700 &&
		    GET_ASIC_REV(tp->pci_chip_rev_id) != ASIC_REV_5701) {
			if (tp_peer != tp &&
			    (tp_peer->tg3_flags & TG3_FLAG_INIT_COMPLETE) != 0)
				return;

			tw32_wait_f(GRC_LOCAL_CTRL, tp->grc_local_ctrl |
				    (GRC_LCLCTRL_GPIO_OE1 |
				     GRC_LCLCTRL_GPIO_OUTPUT1), 100);

			tw32_wait_f(GRC_LOCAL_CTRL, tp->grc_local_ctrl |
				    GRC_LCLCTRL_GPIO_OE1, 100);

			tw32_wait_f(GRC_LOCAL_CTRL, tp->grc_local_ctrl |
				    (GRC_LCLCTRL_GPIO_OE1 |
				     GRC_LCLCTRL_GPIO_OUTPUT1), 100);
		}
	}
}

static int tg3_5700_link_polarity(struct tg3 *tp, u32 speed)
{
	if (tp->led_ctrl == LED_CTRL_MODE_PHY_2)
		return 1;
	else if ((tp->phy_id & PHY_ID_MASK) == PHY_ID_BCM5411) {
		if (speed != SPEED_10)
			return 1;
	} else if (speed == SPEED_10)
		return 1;

	return 0;
}

static int tg3_setup_phy(struct tg3 *, int);

#define RESET_KIND_SHUTDOWN	0
#define RESET_KIND_INIT		1
#define RESET_KIND_SUSPEND	2

static void tg3_write_sig_post_reset(struct tg3 *, int);
static int tg3_halt_cpu(struct tg3 *, u32);

static void tg3_power_down_phy(struct tg3 *tp, bool do_low_power)
{
	u32 val;

	if (tp->tg3_flags2 & TG3_FLG2_PHY_SERDES) {
		if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5704) {
			u32 sg_dig_ctrl = tr32(SG_DIG_CTRL);
			u32 serdes_cfg = tr32(MAC_SERDES_CFG);

			sg_dig_ctrl |=
				SG_DIG_USING_HW_AUTONEG | SG_DIG_SOFT_RESET;
			tw32(SG_DIG_CTRL, sg_dig_ctrl);
			tw32(MAC_SERDES_CFG, serdes_cfg | (1 << 15));
		}
		return;
	}

	if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5906) {
		tg3_bmcr_reset(tp);
		val = tr32(GRC_MISC_CFG);
		tw32_f(GRC_MISC_CFG, val | GRC_MISC_CFG_EPHY_IDDQ);
		udelay(40);
		return;
	} else if (do_low_power) {
		tg3_writephy(tp, MII_TG3_EXT_CTRL,
			     MII_TG3_EXT_CTRL_FORCE_LED_OFF);

		tg3_writephy(tp, MII_TG3_AUX_CTRL,
			     MII_TG3_AUXCTL_SHDWSEL_PWRCTL |
			     MII_TG3_AUXCTL_PCTL_100TX_LPWR |
			     MII_TG3_AUXCTL_PCTL_SPR_ISOLATE |
			     MII_TG3_AUXCTL_PCTL_VREG_11V);
	}

	/* The PHY should not be powered down on some chips because
	 * of bugs.
	 */
	if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5700 ||
	    GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5704 ||
	    (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5780 &&
	     (tp->tg3_flags2 & TG3_FLG2_MII_SERDES)))
		return;

	if (GET_CHIP_REV(tp->pci_chip_rev_id) == CHIPREV_5784_AX ||
	    GET_CHIP_REV(tp->pci_chip_rev_id) == CHIPREV_5761_AX) {
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
	if (tp->tg3_flags & TG3_FLAG_NVRAM) {
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
	if (tp->tg3_flags & TG3_FLAG_NVRAM) {
		if (tp->nvram_lock_cnt > 0)
			tp->nvram_lock_cnt--;
		if (tp->nvram_lock_cnt == 0)
			tw32_f(NVRAM_SWARB, SWARB_REQ_CLR1);
	}
}

/* tp->lock is held. */
static void tg3_enable_nvram_access(struct tg3 *tp)
{
	if ((tp->tg3_flags2 & TG3_FLG2_5750_PLUS) &&
	    !(tp->tg3_flags2 & TG3_FLG2_PROTECTED_NVRAM)) {
		u32 nvaccess = tr32(NVRAM_ACCESS);

		tw32(NVRAM_ACCESS, nvaccess | ACCESS_ENABLE);
	}
}

/* tp->lock is held. */
static void tg3_disable_nvram_access(struct tg3 *tp)
{
	if ((tp->tg3_flags2 & TG3_FLG2_5750_PLUS) &&
	    !(tp->tg3_flags2 & TG3_FLG2_PROTECTED_NVRAM)) {
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
	if ((tp->tg3_flags & TG3_FLAG_NVRAM) &&
	    (tp->tg3_flags & TG3_FLAG_NVRAM_BUFFERED) &&
	    (tp->tg3_flags2 & TG3_FLG2_FLASH) &&
	   !(tp->tg3_flags3 & TG3_FLG3_NO_NVRAM_ADDR_TRANS) &&
	    (tp->nvram_jedecnum == JEDEC_ATMEL))

		addr = ((addr / tp->nvram_pagesize) <<
			ATMEL_AT45DB0X1B_PAGE_POS) +
		       (addr % tp->nvram_pagesize);

	return addr;
}

static u32 tg3_nvram_logical_addr(struct tg3 *tp, u32 addr)
{
	if ((tp->tg3_flags & TG3_FLAG_NVRAM) &&
	    (tp->tg3_flags & TG3_FLAG_NVRAM_BUFFERED) &&
	    (tp->tg3_flags2 & TG3_FLG2_FLASH) &&
	   !(tp->tg3_flags3 & TG3_FLG3_NO_NVRAM_ADDR_TRANS) &&
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

	if (!(tp->tg3_flags & TG3_FLAG_NVRAM))
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

/* tp->lock is held. */
static void __tg3_set_mac_addr(struct tg3 *tp, int skip_mac_1)
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

	if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5703 ||
	    GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5704) {
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

static int tg3_set_power_state(struct tg3 *tp, pci_power_t state)
{
	u32 misc_host_ctrl;
	bool device_should_wake, do_low_power;

	/* Make sure register accesses (indirect or otherwise)
	 * will function correctly.
	 */
	pci_write_config_dword(tp->pdev,
			       TG3PCI_MISC_HOST_CTRL,
			       tp->misc_host_ctrl);

	switch (state) {
	case PCI_D0:
		pci_enable_wake(tp->pdev, state, false);
		pci_set_power_state(tp->pdev, PCI_D0);

		/* Switch out of Vaux if it is a NIC */
		if (tp->tg3_flags2 & TG3_FLG2_IS_NIC)
			tw32_wait_f(GRC_LOCAL_CTRL, tp->grc_local_ctrl, 100);

		return 0;

	case PCI_D1:
	case PCI_D2:
	case PCI_D3hot:
		break;

	default:
		printk(KERN_ERR PFX "%s: Invalid power state (D%d) requested\n",
			tp->dev->name, state);
		return -EINVAL;
	}

	/* Restore the CLKREQ setting. */
	if (tp->tg3_flags3 & TG3_FLG3_CLKREQ_BUG) {
		u16 lnkctl;

		pci_read_config_word(tp->pdev,
				     tp->pcie_cap + PCI_EXP_LNKCTL,
				     &lnkctl);
		lnkctl |= PCI_EXP_LNKCTL_CLKREQ_EN;
		pci_write_config_word(tp->pdev,
				      tp->pcie_cap + PCI_EXP_LNKCTL,
				      lnkctl);
	}

	misc_host_ctrl = tr32(TG3PCI_MISC_HOST_CTRL);
	tw32(TG3PCI_MISC_HOST_CTRL,
	     misc_host_ctrl | MISC_HOST_CTRL_MASK_PCI_INT);

	device_should_wake = pci_pme_capable(tp->pdev, state) &&
			     device_may_wakeup(&tp->pdev->dev) &&
			     (tp->tg3_flags & TG3_FLAG_WOL_ENABLE);

	if (tp->tg3_flags3 & TG3_FLG3_USE_PHYLIB) {
		do_low_power = false;
		if ((tp->tg3_flags3 & TG3_FLG3_PHY_CONNECTED) &&
		    !tp->link_config.phy_is_low_power) {
			struct phy_device *phydev;
			u32 phyid, advertising;

			phydev = tp->mdio_bus->phy_map[PHY_ADDR];

			tp->link_config.phy_is_low_power = 1;

			tp->link_config.orig_speed = phydev->speed;
			tp->link_config.orig_duplex = phydev->duplex;
			tp->link_config.orig_autoneg = phydev->autoneg;
			tp->link_config.orig_advertising = phydev->advertising;

			advertising = ADVERTISED_TP |
				      ADVERTISED_Pause |
				      ADVERTISED_Autoneg |
				      ADVERTISED_10baseT_Half;

			if ((tp->tg3_flags & TG3_FLAG_ENABLE_ASF) ||
			    device_should_wake) {
				if (tp->tg3_flags & TG3_FLAG_WOL_SPEED_100MB)
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
			if (phyid != TG3_PHY_ID_BCMAC131) {
				phyid &= TG3_PHY_OUI_MASK;
				if (phyid == TG3_PHY_OUI_1 ||
				    phyid == TG3_PHY_OUI_2 ||
				    phyid == TG3_PHY_OUI_3)
					do_low_power = true;
			}
		}
	} else {
		do_low_power = true;

		if (tp->link_config.phy_is_low_power == 0) {
			tp->link_config.phy_is_low_power = 1;
			tp->link_config.orig_speed = tp->link_config.speed;
			tp->link_config.orig_duplex = tp->link_config.duplex;
			tp->link_config.orig_autoneg = tp->link_config.autoneg;
		}

		if (!(tp->tg3_flags2 & TG3_FLG2_ANY_SERDES)) {
			tp->link_config.speed = SPEED_10;
			tp->link_config.duplex = DUPLEX_HALF;
			tp->link_config.autoneg = AUTONEG_ENABLE;
			tg3_setup_phy(tp, 0);
		}
	}

	if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5906) {
		u32 val;

		val = tr32(GRC_VCPU_EXT_CTRL);
		tw32(GRC_VCPU_EXT_CTRL, val | GRC_VCPU_EXT_CTRL_DISABLE_WOL);
	} else if (!(tp->tg3_flags & TG3_FLAG_ENABLE_ASF)) {
		int i;
		u32 val;

		for (i = 0; i < 200; i++) {
			tg3_read_mem(tp, NIC_SRAM_FW_ASF_STATUS_MBOX, &val);
			if (val == ~NIC_SRAM_FIRMWARE_MBOX_MAGIC1)
				break;
			msleep(1);
		}
	}
	if (tp->tg3_flags & TG3_FLAG_WOL_CAP)
		tg3_write_mem(tp, NIC_SRAM_WOL_MBOX, WOL_SIGNATURE |
						     WOL_DRV_STATE_SHUTDOWN |
						     WOL_DRV_WOL |
						     WOL_SET_MAGIC_PKT);

	if (device_should_wake) {
		u32 mac_mode;

		if (!(tp->tg3_flags2 & TG3_FLG2_PHY_SERDES)) {
			if (do_low_power) {
				tg3_writephy(tp, MII_TG3_AUX_CTRL, 0x5a);
				udelay(40);
			}

			if (tp->tg3_flags2 & TG3_FLG2_MII_SERDES)
				mac_mode = MAC_MODE_PORT_MODE_GMII;
			else
				mac_mode = MAC_MODE_PORT_MODE_MII;

			mac_mode |= tp->mac_mode & MAC_MODE_LINK_POLARITY;
			if (GET_ASIC_REV(tp->pci_chip_rev_id) ==
			    ASIC_REV_5700) {
				u32 speed = (tp->tg3_flags &
					     TG3_FLAG_WOL_SPEED_100MB) ?
					     SPEED_100 : SPEED_10;
				if (tg3_5700_link_polarity(tp, speed))
					mac_mode |= MAC_MODE_LINK_POLARITY;
				else
					mac_mode &= ~MAC_MODE_LINK_POLARITY;
			}
		} else {
			mac_mode = MAC_MODE_PORT_MODE_TBI;
		}

		if (!(tp->tg3_flags2 & TG3_FLG2_5750_PLUS))
			tw32(MAC_LED_CTRL, tp->led_ctrl);

		mac_mode |= MAC_MODE_MAGIC_PKT_ENABLE;
		if (((tp->tg3_flags2 & TG3_FLG2_5705_PLUS) &&
		    !(tp->tg3_flags2 & TG3_FLG2_5780_CLASS)) &&
		    ((tp->tg3_flags & TG3_FLAG_ENABLE_ASF) ||
		     (tp->tg3_flags3 & TG3_FLG3_ENABLE_APE)))
			mac_mode |= MAC_MODE_KEEP_FRAME_IN_WOL;

		if (tp->tg3_flags3 & TG3_FLG3_ENABLE_APE) {
			mac_mode |= tp->mac_mode &
				    (MAC_MODE_APE_TX_EN | MAC_MODE_APE_RX_EN);
			if (mac_mode & MAC_MODE_APE_TX_EN)
				mac_mode |= MAC_MODE_TDE_ENABLE;
		}

		tw32_f(MAC_MODE, mac_mode);
		udelay(100);

		tw32_f(MAC_RX_MODE, RX_MODE_ENABLE);
		udelay(10);
	}

	if (!(tp->tg3_flags & TG3_FLAG_WOL_SPEED_100MB) &&
	    (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5700 ||
	     GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5701)) {
		u32 base_val;

		base_val = tp->pci_clock_ctrl;
		base_val |= (CLOCK_CTRL_RXCLK_DISABLE |
			     CLOCK_CTRL_TXCLK_DISABLE);

		tw32_wait_f(TG3PCI_CLOCK_CTRL, base_val | CLOCK_CTRL_ALTCLK |
			    CLOCK_CTRL_PWRDOWN_PLL133, 40);
	} else if ((tp->tg3_flags2 & TG3_FLG2_5780_CLASS) ||
		   (tp->tg3_flags & TG3_FLAG_CPMU_PRESENT) ||
		   (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5906)) {
		/* do nothing */
	} else if (!((tp->tg3_flags2 & TG3_FLG2_5750_PLUS) &&
		     (tp->tg3_flags & TG3_FLAG_ENABLE_ASF))) {
		u32 newbits1, newbits2;

		if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5700 ||
		    GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5701) {
			newbits1 = (CLOCK_CTRL_RXCLK_DISABLE |
				    CLOCK_CTRL_TXCLK_DISABLE |
				    CLOCK_CTRL_ALTCLK);
			newbits2 = newbits1 | CLOCK_CTRL_44MHZ_CORE;
		} else if (tp->tg3_flags2 & TG3_FLG2_5705_PLUS) {
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

		if (!(tp->tg3_flags2 & TG3_FLG2_5705_PLUS)) {
			u32 newbits3;

			if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5700 ||
			    GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5701) {
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

	if (!(device_should_wake) &&
	    !(tp->tg3_flags & TG3_FLAG_ENABLE_ASF))
		tg3_power_down_phy(tp, do_low_power);

	tg3_frob_aux_power(tp);

	/* Workaround for unstable PLL clock */
	if ((GET_CHIP_REV(tp->pci_chip_rev_id) == CHIPREV_5750_AX) ||
	    (GET_CHIP_REV(tp->pci_chip_rev_id) == CHIPREV_5750_BX)) {
		u32 val = tr32(0x7d00);

		val &= ~((1 << 16) | (1 << 4) | (1 << 2) | (1 << 1) | 1);
		tw32(0x7d00, val);
		if (!(tp->tg3_flags & TG3_FLAG_ENABLE_ASF)) {
			int err;

			err = tg3_nvram_lock(tp);
			tg3_halt_cpu(tp, RX_CPU_BASE);
			if (!err)
				tg3_nvram_unlock(tp);
		}
	}

	tg3_write_sig_post_reset(tp, RESET_KIND_SHUTDOWN);

	if (device_should_wake)
		pci_enable_wake(tp->pdev, state, true);

	/* Finally, set the new power state. */
	pci_set_power_state(tp->pdev, state);

	return 0;
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
		if (tp->tg3_flags3 & TG3_FLG3_PHY_IS_FET) {
			*speed = (val & MII_TG3_AUX_STAT_100) ? SPEED_100 :
				 SPEED_10;
			*duplex = (val & MII_TG3_AUX_STAT_FULL) ? DUPLEX_FULL :
				  DUPLEX_HALF;
			break;
		}
		*speed = SPEED_INVALID;
		*duplex = DUPLEX_INVALID;
		break;
	}
}

static void tg3_phy_copper_begin(struct tg3 *tp)
{
	u32 new_adv;
	int i;

	if (tp->link_config.phy_is_low_power) {
		/* Entering low power mode.  Disable gigabit and
		 * 100baseT advertisements.
		 */
		tg3_writephy(tp, MII_TG3_CTRL, 0);

		new_adv = (ADVERTISE_10HALF | ADVERTISE_10FULL |
			   ADVERTISE_CSMA | ADVERTISE_PAUSE_CAP);
		if (tp->tg3_flags & TG3_FLAG_WOL_SPEED_100MB)
			new_adv |= (ADVERTISE_100HALF | ADVERTISE_100FULL);

		tg3_writephy(tp, MII_ADVERTISE, new_adv);
	} else if (tp->link_config.speed == SPEED_INVALID) {
		if (tp->tg3_flags & TG3_FLAG_10_100_ONLY)
			tp->link_config.advertising &=
				~(ADVERTISED_1000baseT_Half |
				  ADVERTISED_1000baseT_Full);

		new_adv = ADVERTISE_CSMA;
		if (tp->link_config.advertising & ADVERTISED_10baseT_Half)
			new_adv |= ADVERTISE_10HALF;
		if (tp->link_config.advertising & ADVERTISED_10baseT_Full)
			new_adv |= ADVERTISE_10FULL;
		if (tp->link_config.advertising & ADVERTISED_100baseT_Half)
			new_adv |= ADVERTISE_100HALF;
		if (tp->link_config.advertising & ADVERTISED_100baseT_Full)
			new_adv |= ADVERTISE_100FULL;

		new_adv |= tg3_advert_flowctrl_1000T(tp->link_config.flowctrl);

		tg3_writephy(tp, MII_ADVERTISE, new_adv);

		if (tp->link_config.advertising &
		    (ADVERTISED_1000baseT_Half | ADVERTISED_1000baseT_Full)) {
			new_adv = 0;
			if (tp->link_config.advertising & ADVERTISED_1000baseT_Half)
				new_adv |= MII_TG3_CTRL_ADV_1000_HALF;
			if (tp->link_config.advertising & ADVERTISED_1000baseT_Full)
				new_adv |= MII_TG3_CTRL_ADV_1000_FULL;
			if (!(tp->tg3_flags & TG3_FLAG_10_100_ONLY) &&
			    (tp->pci_chip_rev_id == CHIPREV_ID_5701_A0 ||
			     tp->pci_chip_rev_id == CHIPREV_ID_5701_B0))
				new_adv |= (MII_TG3_CTRL_AS_MASTER |
					    MII_TG3_CTRL_ENABLE_AS_MASTER);
			tg3_writephy(tp, MII_TG3_CTRL, new_adv);
		} else {
			tg3_writephy(tp, MII_TG3_CTRL, 0);
		}
	} else {
		new_adv = tg3_advert_flowctrl_1000T(tp->link_config.flowctrl);
		new_adv |= ADVERTISE_CSMA;

		/* Asking for a specific link mode. */
		if (tp->link_config.speed == SPEED_1000) {
			tg3_writephy(tp, MII_ADVERTISE, new_adv);

			if (tp->link_config.duplex == DUPLEX_FULL)
				new_adv = MII_TG3_CTRL_ADV_1000_FULL;
			else
				new_adv = MII_TG3_CTRL_ADV_1000_HALF;
			if (tp->pci_chip_rev_id == CHIPREV_ID_5701_A0 ||
			    tp->pci_chip_rev_id == CHIPREV_ID_5701_B0)
				new_adv |= (MII_TG3_CTRL_AS_MASTER |
					    MII_TG3_CTRL_ENABLE_AS_MASTER);
		} else {
			if (tp->link_config.speed == SPEED_100) {
				if (tp->link_config.duplex == DUPLEX_FULL)
					new_adv |= ADVERTISE_100FULL;
				else
					new_adv |= ADVERTISE_100HALF;
			} else {
				if (tp->link_config.duplex == DUPLEX_FULL)
					new_adv |= ADVERTISE_10FULL;
				else
					new_adv |= ADVERTISE_10HALF;
			}
			tg3_writephy(tp, MII_ADVERTISE, new_adv);

			new_adv = 0;
		}

		tg3_writephy(tp, MII_TG3_CTRL, new_adv);
	}

	if (tp->link_config.autoneg == AUTONEG_DISABLE &&
	    tp->link_config.speed != SPEED_INVALID) {
		u32 bmcr, orig_bmcr;

		tp->link_config.active_speed = tp->link_config.speed;
		tp->link_config.active_duplex = tp->link_config.duplex;

		bmcr = 0;
		switch (tp->link_config.speed) {
		default:
		case SPEED_10:
			break;

		case SPEED_100:
			bmcr |= BMCR_SPEED100;
			break;

		case SPEED_1000:
			bmcr |= TG3_BMCR_SPEED1000;
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
	} else {
		tg3_writephy(tp, MII_BMCR,
			     BMCR_ANENABLE | BMCR_ANRESTART);
	}
}

static int tg3_init_5401phy_dsp(struct tg3 *tp)
{
	int err;

	/* Turn off tap power management. */
	/* Set Extended packet length bit */
	err  = tg3_writephy(tp, MII_TG3_AUX_CTRL, 0x4c20);

	err |= tg3_writephy(tp, MII_TG3_DSP_ADDRESS, 0x0012);
	err |= tg3_writephy(tp, MII_TG3_DSP_RW_PORT, 0x1804);

	err |= tg3_writephy(tp, MII_TG3_DSP_ADDRESS, 0x0013);
	err |= tg3_writephy(tp, MII_TG3_DSP_RW_PORT, 0x1204);

	err |= tg3_writephy(tp, MII_TG3_DSP_ADDRESS, 0x8006);
	err |= tg3_writephy(tp, MII_TG3_DSP_RW_PORT, 0x0132);

	err |= tg3_writephy(tp, MII_TG3_DSP_ADDRESS, 0x8006);
	err |= tg3_writephy(tp, MII_TG3_DSP_RW_PORT, 0x0232);

	err |= tg3_writephy(tp, MII_TG3_DSP_ADDRESS, 0x201f);
	err |= tg3_writephy(tp, MII_TG3_DSP_RW_PORT, 0x0a20);

	udelay(40);

	return err;
}

static int tg3_copper_is_advertising_all(struct tg3 *tp, u32 mask)
{
	u32 adv_reg, all_mask = 0;

	if (mask & ADVERTISED_10baseT_Half)
		all_mask |= ADVERTISE_10HALF;
	if (mask & ADVERTISED_10baseT_Full)
		all_mask |= ADVERTISE_10FULL;
	if (mask & ADVERTISED_100baseT_Half)
		all_mask |= ADVERTISE_100HALF;
	if (mask & ADVERTISED_100baseT_Full)
		all_mask |= ADVERTISE_100FULL;

	if (tg3_readphy(tp, MII_ADVERTISE, &adv_reg))
		return 0;

	if ((adv_reg & all_mask) != all_mask)
		return 0;
	if (!(tp->tg3_flags & TG3_FLAG_10_100_ONLY)) {
		u32 tg3_ctrl;

		all_mask = 0;
		if (mask & ADVERTISED_1000baseT_Half)
			all_mask |= ADVERTISE_1000HALF;
		if (mask & ADVERTISED_1000baseT_Full)
			all_mask |= ADVERTISE_1000FULL;

		if (tg3_readphy(tp, MII_TG3_CTRL, &tg3_ctrl))
			return 0;

		if ((tg3_ctrl & all_mask) != all_mask)
			return 0;
	}
	return 1;
}

static int tg3_adv_1000T_flowctrl_ok(struct tg3 *tp, u32 *lcladv, u32 *rmtadv)
{
	u32 curadv, reqadv;

	if (tg3_readphy(tp, MII_ADVERTISE, lcladv))
		return 1;

	curadv = *lcladv & (ADVERTISE_PAUSE_CAP | ADVERTISE_PAUSE_ASYM);
	reqadv = tg3_advert_flowctrl_1000T(tp->link_config.flowctrl);

	if (tp->link_config.active_duplex == DUPLEX_FULL) {
		if (curadv != reqadv)
			return 0;

		if (tp->tg3_flags & TG3_FLAG_PAUSE_AUTONEG)
			tg3_readphy(tp, MII_LPA, rmtadv);
	} else {
		/* Reprogram the advertisement register, even if it
		 * does not affect the current link.  If the link
		 * gets renegotiated in the future, we can save an
		 * additional renegotiation cycle by advertising
		 * it correctly in the first place.
		 */
		if (curadv != reqadv) {
			*lcladv &= ~(ADVERTISE_PAUSE_CAP |
				     ADVERTISE_PAUSE_ASYM);
			tg3_writephy(tp, MII_ADVERTISE, *lcladv | reqadv);
		}
	}

	return 1;
}

static int tg3_setup_copper_phy(struct tg3 *tp, int force_reset)
{
	int current_link_up;
	u32 bmsr, dummy;
	u32 lcl_adv, rmt_adv;
	u16 current_speed;
	u8 current_duplex;
	int i, err;

	tw32(MAC_EVENT, 0);

	tw32_f(MAC_STATUS,
	     (MAC_STATUS_SYNC_CHANGED |
	      MAC_STATUS_CFG_CHANGED |
	      MAC_STATUS_MI_COMPLETION |
	      MAC_STATUS_LNKSTATE_CHANGED));
	udelay(40);

	if ((tp->mi_mode & MAC_MI_MODE_AUTO_POLL) != 0) {
		tw32_f(MAC_MI_MODE,
		     (tp->mi_mode & ~MAC_MI_MODE_AUTO_POLL));
		udelay(80);
	}

	tg3_writephy(tp, MII_TG3_AUX_CTRL, 0x02);

	/* Some third-party PHYs need to be reset on link going
	 * down.
	 */
	if ((GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5703 ||
	     GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5704 ||
	     GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5705) &&
	    netif_carrier_ok(tp->dev)) {
		tg3_readphy(tp, MII_BMSR, &bmsr);
		if (!tg3_readphy(tp, MII_BMSR, &bmsr) &&
		    !(bmsr & BMSR_LSTATUS))
			force_reset = 1;
	}
	if (force_reset)
		tg3_phy_reset(tp);

	if ((tp->phy_id & PHY_ID_MASK) == PHY_ID_BCM5401) {
		tg3_readphy(tp, MII_BMSR, &bmsr);
		if (tg3_readphy(tp, MII_BMSR, &bmsr) ||
		    !(tp->tg3_flags & TG3_FLAG_INIT_COMPLETE))
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

			if ((tp->phy_id & PHY_ID_REV_MASK) == PHY_REV_BCM5401_B0 &&
			    !(bmsr & BMSR_LSTATUS) &&
			    tp->link_config.active_speed == SPEED_1000) {
				err = tg3_phy_reset(tp);
				if (!err)
					err = tg3_init_5401phy_dsp(tp);
				if (err)
					return err;
			}
		}
	} else if (tp->pci_chip_rev_id == CHIPREV_ID_5701_A0 ||
		   tp->pci_chip_rev_id == CHIPREV_ID_5701_B0) {
		/* 5701 {A0,B0} CRC bug workaround */
		tg3_writephy(tp, 0x15, 0x0a75);
		tg3_writephy(tp, 0x1c, 0x8c68);
		tg3_writephy(tp, 0x1c, 0x8d68);
		tg3_writephy(tp, 0x1c, 0x8c68);
	}

	/* Clear pending interrupts... */
	tg3_readphy(tp, MII_TG3_ISTAT, &dummy);
	tg3_readphy(tp, MII_TG3_ISTAT, &dummy);

	if (tp->tg3_flags & TG3_FLAG_USE_MI_INTERRUPT)
		tg3_writephy(tp, MII_TG3_IMASK, ~MII_TG3_INT_LINKCHG);
	else if (!(tp->tg3_flags3 & TG3_FLG3_PHY_IS_FET))
		tg3_writephy(tp, MII_TG3_IMASK, ~0);

	if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5700 ||
	    GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5701) {
		if (tp->led_ctrl == LED_CTRL_MODE_PHY_1)
			tg3_writephy(tp, MII_TG3_EXT_CTRL,
				     MII_TG3_EXT_CTRL_LNK3_LED_MODE);
		else
			tg3_writephy(tp, MII_TG3_EXT_CTRL, 0);
	}

	current_link_up = 0;
	current_speed = SPEED_INVALID;
	current_duplex = DUPLEX_INVALID;

	if (tp->tg3_flags2 & TG3_FLG2_CAPACITIVE_COUPLING) {
		u32 val;

		tg3_writephy(tp, MII_TG3_AUX_CTRL, 0x4007);
		tg3_readphy(tp, MII_TG3_AUX_CTRL, &val);
		if (!(val & (1 << 10))) {
			val |= (1 << 10);
			tg3_writephy(tp, MII_TG3_AUX_CTRL, val);
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
			if ((bmcr & BMCR_ANENABLE) &&
			    tg3_copper_is_advertising_all(tp,
						tp->link_config.advertising)) {
				if (tg3_adv_1000T_flowctrl_ok(tp, &lcl_adv,
								  &rmt_adv))
					current_link_up = 1;
			}
		} else {
			if (!(bmcr & BMCR_ANENABLE) &&
			    tp->link_config.speed == current_speed &&
			    tp->link_config.duplex == current_duplex &&
			    tp->link_config.flowctrl ==
			    tp->link_config.active_flowctrl) {
				current_link_up = 1;
			}
		}

		if (current_link_up == 1 &&
		    tp->link_config.active_duplex == DUPLEX_FULL)
			tg3_setup_flow_control(tp, lcl_adv, rmt_adv);
	}

relink:
	if (current_link_up == 0 || tp->link_config.phy_is_low_power) {
		u32 tmp;

		tg3_phy_copper_begin(tp);

		tg3_readphy(tp, MII_BMSR, &tmp);
		if (!tg3_readphy(tp, MII_BMSR, &tmp) &&
		    (tmp & BMSR_LSTATUS))
			current_link_up = 1;
	}

	tp->mac_mode &= ~MAC_MODE_PORT_MODE_MASK;
	if (current_link_up == 1) {
		if (tp->link_config.active_speed == SPEED_100 ||
		    tp->link_config.active_speed == SPEED_10)
			tp->mac_mode |= MAC_MODE_PORT_MODE_MII;
		else
			tp->mac_mode |= MAC_MODE_PORT_MODE_GMII;
	} else if (tp->tg3_flags3 & TG3_FLG3_PHY_IS_FET)
		tp->mac_mode |= MAC_MODE_PORT_MODE_MII;
	else
		tp->mac_mode |= MAC_MODE_PORT_MODE_GMII;

	tp->mac_mode &= ~MAC_MODE_HALF_DUPLEX;
	if (tp->link_config.active_duplex == DUPLEX_HALF)
		tp->mac_mode |= MAC_MODE_HALF_DUPLEX;

	if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5700) {
		if (current_link_up == 1 &&
		    tg3_5700_link_polarity(tp, tp->link_config.active_speed))
			tp->mac_mode |= MAC_MODE_LINK_POLARITY;
		else
			tp->mac_mode &= ~MAC_MODE_LINK_POLARITY;
	}

	/* ??? Without this setting Netgear GA302T PHY does not
	 * ??? send/receive packets...
	 */
	if ((tp->phy_id & PHY_ID_MASK) == PHY_ID_BCM5411 &&
	    tp->pci_chip_rev_id == CHIPREV_ID_5700_ALTIMA) {
		tp->mi_mode |= MAC_MI_MODE_AUTO_POLL;
		tw32_f(MAC_MI_MODE, tp->mi_mode);
		udelay(80);
	}

	tw32_f(MAC_MODE, tp->mac_mode);
	udelay(40);

	if (tp->tg3_flags & TG3_FLAG_USE_LINKCHG_REG) {
		/* Polled via timer. */
		tw32_f(MAC_EVENT, 0);
	} else {
		tw32_f(MAC_EVENT, MAC_EVENT_LNKSTATE_CHANGED);
	}
	udelay(40);

	if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5700 &&
	    current_link_up == 1 &&
	    tp->link_config.active_speed == SPEED_1000 &&
	    ((tp->tg3_flags & TG3_FLAG_PCIX_MODE) ||
	     (tp->tg3_flags & TG3_FLAG_PCI_HIGH_SPEED))) {
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
	if (tp->tg3_flags3 & TG3_FLG3_CLKREQ_BUG) {
		u16 oldlnkctl, newlnkctl;

		pci_read_config_word(tp->pdev,
				     tp->pcie_cap + PCI_EXP_LNKCTL,
				     &oldlnkctl);
		if (tp->link_config.active_speed == SPEED_100 ||
		    tp->link_config.active_speed == SPEED_10)
			newlnkctl = oldlnkctl & ~PCI_EXP_LNKCTL_CLKREQ_EN;
		else
			newlnkctl = oldlnkctl | PCI_EXP_LNKCTL_CLKREQ_EN;
		if (newlnkctl != oldlnkctl)
			pci_write_config_word(tp->pdev,
					      tp->pcie_cap + PCI_EXP_LNKCTL,
					      newlnkctl);
	} else if (tp->tg3_flags3 & TG3_FLG3_TOGGLE_10_100_L1PLLPD) {
		u32 newreg, oldreg = tr32(TG3_PCIE_LNKCTL);
		if (tp->link_config.active_speed == SPEED_100 ||
		    tp->link_config.active_speed == SPEED_10)
			newreg = oldreg & ~TG3_PCIE_LNKCTL_L1_PLL_PD_EN;
		else
			newreg = oldreg | TG3_PCIE_LNKCTL_L1_PLL_PD_EN;
		if (newreg != oldreg)
			tw32(TG3_PCIE_LNKCTL, newreg);
	}

	if (current_link_up != netif_carrier_ok(tp->dev)) {
		if (current_link_up)
			netif_carrier_on(tp->dev);
		else
			netif_carrier_off(tp->dev);
		tg3_link_report(tp);
	}

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

	switch(ap->state) {
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
		if (delta > ANEG_STATE_SETTLE_TIME) {
			ap->state = ANEG_STATE_ABILITY_DETECT_INIT;
		} else {
			ret = ANEG_TIMER_ENAB;
		}
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
		if (ap->ability_match != 0 && ap->rxconfig != 0) {
			ap->state = ANEG_STATE_ACK_DETECT_INIT;
		}
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
	if ((tp->tg3_flags & TG3_FLAG_INIT_COMPLETE) &&
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

static int tg3_setup_fiber_hw_autoneg(struct tg3 *tp, u32 mac_status)
{
	u16 flowctrl;
	u32 sg_dig_ctrl, sg_dig_status;
	u32 serdes_cfg, expected_sg_dig_ctrl;
	int workaround, port_a;
	int current_link_up;

	serdes_cfg = 0;
	expected_sg_dig_ctrl = 0;
	workaround = 0;
	port_a = 1;
	current_link_up = 0;

	if (tp->pci_chip_rev_id != CHIPREV_ID_5704_A0 &&
	    tp->pci_chip_rev_id != CHIPREV_ID_5704_A1) {
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
			current_link_up = 1;
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
		if ((tp->tg3_flags2 & TG3_FLG2_PARALLEL_DETECT) &&
		    tp->serdes_counter &&
		    ((mac_status & (MAC_STATUS_PCS_SYNCED |
				    MAC_STATUS_RCVD_CFG)) ==
		     MAC_STATUS_PCS_SYNCED)) {
			tp->serdes_counter--;
			current_link_up = 1;
			goto out;
		}
restart_autoneg:
		if (workaround)
			tw32_f(MAC_SERDES_CFG, serdes_cfg | 0xc011000);
		tw32_f(SG_DIG_CTRL, expected_sg_dig_ctrl | SG_DIG_SOFT_RESET);
		udelay(5);
		tw32_f(SG_DIG_CTRL, expected_sg_dig_ctrl);

		tp->serdes_counter = SERDES_AN_TIMEOUT_5704S;
		tp->tg3_flags2 &= ~TG3_FLG2_PARALLEL_DETECT;
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

			tg3_setup_flow_control(tp, local_adv, remote_adv);
			current_link_up = 1;
			tp->serdes_counter = 0;
			tp->tg3_flags2 &= ~TG3_FLG2_PARALLEL_DETECT;
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
					current_link_up = 1;
					tp->tg3_flags2 |=
						TG3_FLG2_PARALLEL_DETECT;
					tp->serdes_counter =
						SERDES_PARALLEL_DET_TIMEOUT;
				} else
					goto restart_autoneg;
			}
		}
	} else {
		tp->serdes_counter = SERDES_AN_TIMEOUT_5704S;
		tp->tg3_flags2 &= ~TG3_FLG2_PARALLEL_DETECT;
	}

out:
	return current_link_up;
}

static int tg3_setup_fiber_by_hand(struct tg3 *tp, u32 mac_status)
{
	int current_link_up = 0;

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

			tg3_setup_flow_control(tp, local_adv, remote_adv);

			current_link_up = 1;
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
		if (current_link_up == 0 &&
		    (mac_status & MAC_STATUS_PCS_SYNCED) &&
		    !(mac_status & MAC_STATUS_RCVD_CFG))
			current_link_up = 1;
	} else {
		tg3_setup_flow_control(tp, 0, 0);

		/* Forcing 1000FD link up. */
		current_link_up = 1;

		tw32_f(MAC_MODE, (tp->mac_mode | MAC_MODE_SEND_CONFIGS));
		udelay(40);

		tw32_f(MAC_MODE, tp->mac_mode);
		udelay(40);
	}

out:
	return current_link_up;
}

static int tg3_setup_fiber_phy(struct tg3 *tp, int force_reset)
{
	u32 orig_pause_cfg;
	u16 orig_active_speed;
	u8 orig_active_duplex;
	u32 mac_status;
	int current_link_up;
	int i;

	orig_pause_cfg = tp->link_config.active_flowctrl;
	orig_active_speed = tp->link_config.active_speed;
	orig_active_duplex = tp->link_config.active_duplex;

	if (!(tp->tg3_flags2 & TG3_FLG2_HW_AUTONEG) &&
	    netif_carrier_ok(tp->dev) &&
	    (tp->tg3_flags & TG3_FLAG_INIT_COMPLETE)) {
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

	if (tp->phy_id == PHY_ID_BCM8002)
		tg3_init_bcm8002(tp);

	/* Enable link change event even when serdes polling.  */
	tw32_f(MAC_EVENT, MAC_EVENT_LNKSTATE_CHANGED);
	udelay(40);

	current_link_up = 0;
	mac_status = tr32(MAC_STATUS);

	if (tp->tg3_flags2 & TG3_FLG2_HW_AUTONEG)
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
		current_link_up = 0;
		if (tp->link_config.autoneg == AUTONEG_ENABLE &&
		    tp->serdes_counter == 0) {
			tw32_f(MAC_MODE, (tp->mac_mode |
					  MAC_MODE_SEND_CONFIGS));
			udelay(1);
			tw32_f(MAC_MODE, tp->mac_mode);
		}
	}

	if (current_link_up == 1) {
		tp->link_config.active_speed = SPEED_1000;
		tp->link_config.active_duplex = DUPLEX_FULL;
		tw32(MAC_LED_CTRL, (tp->led_ctrl |
				    LED_CTRL_LNKLED_OVERRIDE |
				    LED_CTRL_1000MBPS_ON));
	} else {
		tp->link_config.active_speed = SPEED_INVALID;
		tp->link_config.active_duplex = DUPLEX_INVALID;
		tw32(MAC_LED_CTRL, (tp->led_ctrl |
				    LED_CTRL_LNKLED_OVERRIDE |
				    LED_CTRL_TRAFFIC_OVERRIDE));
	}

	if (current_link_up != netif_carrier_ok(tp->dev)) {
		if (current_link_up)
			netif_carrier_on(tp->dev);
		else
			netif_carrier_off(tp->dev);
		tg3_link_report(tp);
	} else {
		u32 now_pause_cfg = tp->link_config.active_flowctrl;
		if (orig_pause_cfg != now_pause_cfg ||
		    orig_active_speed != tp->link_config.active_speed ||
		    orig_active_duplex != tp->link_config.active_duplex)
			tg3_link_report(tp);
	}

	return 0;
}

static int tg3_setup_fiber_mii_phy(struct tg3 *tp, int force_reset)
{
	int current_link_up, err = 0;
	u32 bmsr, bmcr;
	u16 current_speed;
	u8 current_duplex;
	u32 local_adv, remote_adv;

	tp->mac_mode |= MAC_MODE_PORT_MODE_GMII;
	tw32_f(MAC_MODE, tp->mac_mode);
	udelay(40);

	tw32(MAC_EVENT, 0);

	tw32_f(MAC_STATUS,
	     (MAC_STATUS_SYNC_CHANGED |
	      MAC_STATUS_CFG_CHANGED |
	      MAC_STATUS_MI_COMPLETION |
	      MAC_STATUS_LNKSTATE_CHANGED));
	udelay(40);

	if (force_reset)
		tg3_phy_reset(tp);

	current_link_up = 0;
	current_speed = SPEED_INVALID;
	current_duplex = DUPLEX_INVALID;

	err |= tg3_readphy(tp, MII_BMSR, &bmsr);
	err |= tg3_readphy(tp, MII_BMSR, &bmsr);
	if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5714) {
		if (tr32(MAC_TX_STATUS) & TX_STATUS_LINK_UP)
			bmsr |= BMSR_LSTATUS;
		else
			bmsr &= ~BMSR_LSTATUS;
	}

	err |= tg3_readphy(tp, MII_BMCR, &bmcr);

	if ((tp->link_config.autoneg == AUTONEG_ENABLE) && !force_reset &&
	    (tp->tg3_flags2 & TG3_FLG2_PARALLEL_DETECT)) {
		/* do nothing, just check for link up at the end */
	} else if (tp->link_config.autoneg == AUTONEG_ENABLE) {
		u32 adv, new_adv;

		err |= tg3_readphy(tp, MII_ADVERTISE, &adv);
		new_adv = adv & ~(ADVERTISE_1000XFULL | ADVERTISE_1000XHALF |
				  ADVERTISE_1000XPAUSE |
				  ADVERTISE_1000XPSE_ASYM |
				  ADVERTISE_SLCT);

		new_adv |= tg3_advert_flowctrl_1000X(tp->link_config.flowctrl);

		if (tp->link_config.advertising & ADVERTISED_1000baseT_Half)
			new_adv |= ADVERTISE_1000XHALF;
		if (tp->link_config.advertising & ADVERTISED_1000baseT_Full)
			new_adv |= ADVERTISE_1000XFULL;

		if ((new_adv != adv) || !(bmcr & BMCR_ANENABLE)) {
			tg3_writephy(tp, MII_ADVERTISE, new_adv);
			bmcr |= BMCR_ANENABLE | BMCR_ANRESTART;
			tg3_writephy(tp, MII_BMCR, bmcr);

			tw32_f(MAC_EVENT, MAC_EVENT_LNKSTATE_CHANGED);
			tp->serdes_counter = SERDES_AN_TIMEOUT_5714S;
			tp->tg3_flags2 &= ~TG3_FLG2_PARALLEL_DETECT;

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
			if (netif_carrier_ok(tp->dev)) {
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
				netif_carrier_off(tp->dev);
			}
			tg3_writephy(tp, MII_BMCR, new_bmcr);
			bmcr = new_bmcr;
			err |= tg3_readphy(tp, MII_BMSR, &bmsr);
			err |= tg3_readphy(tp, MII_BMSR, &bmsr);
			if (GET_ASIC_REV(tp->pci_chip_rev_id) ==
			    ASIC_REV_5714) {
				if (tr32(MAC_TX_STATUS) & TX_STATUS_LINK_UP)
					bmsr |= BMSR_LSTATUS;
				else
					bmsr &= ~BMSR_LSTATUS;
			}
			tp->tg3_flags2 &= ~TG3_FLG2_PARALLEL_DETECT;
		}
	}

	if (bmsr & BMSR_LSTATUS) {
		current_speed = SPEED_1000;
		current_link_up = 1;
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
			}
			else
				current_link_up = 0;
		}
	}

	if (current_link_up == 1 && current_duplex == DUPLEX_FULL)
		tg3_setup_flow_control(tp, local_adv, remote_adv);

	tp->mac_mode &= ~MAC_MODE_HALF_DUPLEX;
	if (tp->link_config.active_duplex == DUPLEX_HALF)
		tp->mac_mode |= MAC_MODE_HALF_DUPLEX;

	tw32_f(MAC_MODE, tp->mac_mode);
	udelay(40);

	tw32_f(MAC_EVENT, MAC_EVENT_LNKSTATE_CHANGED);

	tp->link_config.active_speed = current_speed;
	tp->link_config.active_duplex = current_duplex;

	if (current_link_up != netif_carrier_ok(tp->dev)) {
		if (current_link_up)
			netif_carrier_on(tp->dev);
		else {
			netif_carrier_off(tp->dev);
			tp->tg3_flags2 &= ~TG3_FLG2_PARALLEL_DETECT;
		}
		tg3_link_report(tp);
	}
	return err;
}

static void tg3_serdes_parallel_detect(struct tg3 *tp)
{
	if (tp->serdes_counter) {
		/* Give autoneg time to complete. */
		tp->serdes_counter--;
		return;
	}
	if (!netif_carrier_ok(tp->dev) &&
	    (tp->link_config.autoneg == AUTONEG_ENABLE)) {
		u32 bmcr;

		tg3_readphy(tp, MII_BMCR, &bmcr);
		if (bmcr & BMCR_ANENABLE) {
			u32 phy1, phy2;

			/* Select shadow register 0x1f */
			tg3_writephy(tp, 0x1c, 0x7c00);
			tg3_readphy(tp, 0x1c, &phy1);

			/* Select expansion interrupt status register */
			tg3_writephy(tp, 0x17, 0x0f01);
			tg3_readphy(tp, 0x15, &phy2);
			tg3_readphy(tp, 0x15, &phy2);

			if ((phy1 & 0x10) && !(phy2 & 0x20)) {
				/* We have signal detect and not receiving
				 * config code words, link is up by parallel
				 * detection.
				 */

				bmcr &= ~BMCR_ANENABLE;
				bmcr |= BMCR_SPEED1000 | BMCR_FULLDPLX;
				tg3_writephy(tp, MII_BMCR, bmcr);
				tp->tg3_flags2 |= TG3_FLG2_PARALLEL_DETECT;
			}
		}
	}
	else if (netif_carrier_ok(tp->dev) &&
		 (tp->link_config.autoneg == AUTONEG_ENABLE) &&
		 (tp->tg3_flags2 & TG3_FLG2_PARALLEL_DETECT)) {
		u32 phy2;

		/* Select expansion interrupt status register */
		tg3_writephy(tp, 0x17, 0x0f01);
		tg3_readphy(tp, 0x15, &phy2);
		if (phy2 & 0x20) {
			u32 bmcr;

			/* Config code words received, turn on autoneg. */
			tg3_readphy(tp, MII_BMCR, &bmcr);
			tg3_writephy(tp, MII_BMCR, bmcr | BMCR_ANENABLE);

			tp->tg3_flags2 &= ~TG3_FLG2_PARALLEL_DETECT;

		}
	}
}

static int tg3_setup_phy(struct tg3 *tp, int force_reset)
{
	int err;

	if (tp->tg3_flags2 & TG3_FLG2_PHY_SERDES) {
		err = tg3_setup_fiber_phy(tp, force_reset);
	} else if (tp->tg3_flags2 & TG3_FLG2_MII_SERDES) {
		err = tg3_setup_fiber_mii_phy(tp, force_reset);
	} else {
		err = tg3_setup_copper_phy(tp, force_reset);
	}

	if (GET_CHIP_REV(tp->pci_chip_rev_id) == CHIPREV_5784_AX) {
		u32 val, scale;

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

	if (tp->link_config.active_speed == SPEED_1000 &&
	    tp->link_config.active_duplex == DUPLEX_HALF)
		tw32(MAC_TX_LENGTHS,
		     ((2 << TX_LENGTHS_IPG_CRS_SHIFT) |
		      (6 << TX_LENGTHS_IPG_SHIFT) |
		      (0xff << TX_LENGTHS_SLOT_TIME_SHIFT)));
	else
		tw32(MAC_TX_LENGTHS,
		     ((2 << TX_LENGTHS_IPG_CRS_SHIFT) |
		      (6 << TX_LENGTHS_IPG_SHIFT) |
		      (32 << TX_LENGTHS_SLOT_TIME_SHIFT)));

	if (!(tp->tg3_flags2 & TG3_FLG2_5705_PLUS)) {
		if (netif_carrier_ok(tp->dev)) {
			tw32(HOSTCC_STAT_COAL_TICKS,
			     tp->coal.stats_block_coalesce_usecs);
		} else {
			tw32(HOSTCC_STAT_COAL_TICKS, 0);
		}
	}

	if (tp->tg3_flags & TG3_FLAG_ASPM_WORKAROUND) {
		u32 val = tr32(PCIE_PWR_MGMT_THRESH);
		if (!netif_carrier_ok(tp->dev))
			val = (val & ~PCIE_PWR_MGMT_L1_THRESH_MSK) |
			      tp->pwrmgmt_thresh;
		else
			val |= PCIE_PWR_MGMT_L1_THRESH_MSK;
		tw32(PCIE_PWR_MGMT_THRESH, val);
	}

	return err;
}

/* This is called whenever we suspect that the system chipset is re-
 * ordering the sequence of MMIO to the tx send mailbox. The symptom
 * is bogus tx completions. We try to recover by setting the
 * TG3_FLAG_MBOX_WRITE_REORDER flag and resetting the chip later
 * in the workqueue.
 */
static void tg3_tx_recover(struct tg3 *tp)
{
	BUG_ON((tp->tg3_flags & TG3_FLAG_MBOX_WRITE_REORDER) ||
	       tp->write32_tx_mbox == tg3_write_indirect_mbox);

	printk(KERN_WARNING PFX "%s: The system may be re-ordering memory-"
	       "mapped I/O cycles to the network device, attempting to "
	       "recover. Please report the problem to the driver maintainer "
	       "and include system chipset information.\n", tp->dev->name);

	spin_lock(&tp->lock);
	tp->tg3_flags |= TG3_FLAG_TX_RECOVERY_PENDING;
	spin_unlock(&tp->lock);
}

static inline u32 tg3_tx_avail(struct tg3_napi *tnapi)
{
	smp_mb();
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

	if (tp->tg3_flags2 & TG3_FLG2_USING_MSIX)
		index--;

	txq = netdev_get_tx_queue(tp->dev, index);

	while (sw_idx != hw_idx) {
		struct tx_ring_info *ri = &tnapi->tx_buffers[sw_idx];
		struct sk_buff *skb = ri->skb;
		int i, tx_bug = 0;

		if (unlikely(skb == NULL)) {
			tg3_tx_recover(tp);
			return;
		}

		skb_dma_unmap(&tp->pdev->dev, skb, DMA_TO_DEVICE);

		ri->skb = NULL;

		sw_idx = NEXT_TX(sw_idx);

		for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
			ri = &tnapi->tx_buffers[sw_idx];
			if (unlikely(ri->skb != NULL || sw_idx == hw_idx))
				tx_bug = 1;
			sw_idx = NEXT_TX(sw_idx);
		}

		dev_kfree_skb(skb);

		if (unlikely(tx_bug)) {
			tg3_tx_recover(tp);
			return;
		}
	}

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
static int tg3_alloc_rx_skb(struct tg3_napi *tnapi, u32 opaque_key,
			    int src_idx, u32 dest_idx_unmasked)
{
	struct tg3 *tp = tnapi->tp;
	struct tg3_rx_buffer_desc *desc;
	struct ring_info *map, *src_map;
	struct sk_buff *skb;
	dma_addr_t mapping;
	int skb_size, dest_idx;
	struct tg3_rx_prodring_set *tpr = &tp->prodring[0];

	src_map = NULL;
	switch (opaque_key) {
	case RXD_OPAQUE_RING_STD:
		dest_idx = dest_idx_unmasked % TG3_RX_RING_SIZE;
		desc = &tpr->rx_std[dest_idx];
		map = &tpr->rx_std_buffers[dest_idx];
		if (src_idx >= 0)
			src_map = &tpr->rx_std_buffers[src_idx];
		skb_size = tp->rx_pkt_map_sz;
		break;

	case RXD_OPAQUE_RING_JUMBO:
		dest_idx = dest_idx_unmasked % TG3_RX_JUMBO_RING_SIZE;
		desc = &tpr->rx_jmb[dest_idx].std;
		map = &tpr->rx_jmb_buffers[dest_idx];
		if (src_idx >= 0)
			src_map = &tpr->rx_jmb_buffers[src_idx];
		skb_size = TG3_RX_JMB_MAP_SZ;
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
	skb = netdev_alloc_skb(tp->dev, skb_size + tp->rx_offset);
	if (skb == NULL)
		return -ENOMEM;

	skb_reserve(skb, tp->rx_offset);

	mapping = pci_map_single(tp->pdev, skb->data, skb_size,
				 PCI_DMA_FROMDEVICE);

	map->skb = skb;
	pci_unmap_addr_set(map, mapping, mapping);

	if (src_map != NULL)
		src_map->skb = NULL;

	desc->addr_hi = ((u64)mapping >> 32);
	desc->addr_lo = ((u64)mapping & 0xffffffff);

	return skb_size;
}

/* We only need to move over in the address because the other
 * members of the RX descriptor are invariant.  See notes above
 * tg3_alloc_rx_skb for full details.
 */
static void tg3_recycle_rx(struct tg3_napi *tnapi, u32 opaque_key,
			   int src_idx, u32 dest_idx_unmasked)
{
	struct tg3 *tp = tnapi->tp;
	struct tg3_rx_buffer_desc *src_desc, *dest_desc;
	struct ring_info *src_map, *dest_map;
	int dest_idx;
	struct tg3_rx_prodring_set *tpr = &tp->prodring[0];

	switch (opaque_key) {
	case RXD_OPAQUE_RING_STD:
		dest_idx = dest_idx_unmasked % TG3_RX_RING_SIZE;
		dest_desc = &tpr->rx_std[dest_idx];
		dest_map = &tpr->rx_std_buffers[dest_idx];
		src_desc = &tpr->rx_std[src_idx];
		src_map = &tpr->rx_std_buffers[src_idx];
		break;

	case RXD_OPAQUE_RING_JUMBO:
		dest_idx = dest_idx_unmasked % TG3_RX_JUMBO_RING_SIZE;
		dest_desc = &tpr->rx_jmb[dest_idx].std;
		dest_map = &tpr->rx_jmb_buffers[dest_idx];
		src_desc = &tpr->rx_jmb[src_idx].std;
		src_map = &tpr->rx_jmb_buffers[src_idx];
		break;

	default:
		return;
	}

	dest_map->skb = src_map->skb;
	pci_unmap_addr_set(dest_map, mapping,
			   pci_unmap_addr(src_map, mapping));
	dest_desc->addr_hi = src_desc->addr_hi;
	dest_desc->addr_lo = src_desc->addr_lo;

	src_map->skb = NULL;
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
	u32 sw_idx = tnapi->rx_rcb_ptr;
	u16 hw_idx;
	int received;
	struct tg3_rx_prodring_set *tpr = &tp->prodring[0];

	hw_idx = *(tnapi->rx_rcb_prod_idx);
	/*
	 * We need to order the read of hw_idx and the read of
	 * the opaque cookie.
	 */
	rmb();
	work_mask = 0;
	received = 0;
	while (sw_idx != hw_idx && budget > 0) {
		struct tg3_rx_buffer_desc *desc = &tnapi->rx_rcb[sw_idx];
		unsigned int len;
		struct sk_buff *skb;
		dma_addr_t dma_addr;
		u32 opaque_key, desc_idx, *post_ptr;

		desc_idx = desc->opaque & RXD_OPAQUE_INDEX_MASK;
		opaque_key = desc->opaque & RXD_OPAQUE_RING_MASK;
		if (opaque_key == RXD_OPAQUE_RING_STD) {
			struct ring_info *ri = &tpr->rx_std_buffers[desc_idx];
			dma_addr = pci_unmap_addr(ri, mapping);
			skb = ri->skb;
			post_ptr = &tpr->rx_std_ptr;
			rx_std_posted++;
		} else if (opaque_key == RXD_OPAQUE_RING_JUMBO) {
			struct ring_info *ri = &tpr->rx_jmb_buffers[desc_idx];
			dma_addr = pci_unmap_addr(ri, mapping);
			skb = ri->skb;
			post_ptr = &tpr->rx_jmb_ptr;
		} else
			goto next_pkt_nopost;

		work_mask |= opaque_key;

		if ((desc->err_vlan & RXD_ERR_MASK) != 0 &&
		    (desc->err_vlan != RXD_ERR_ODD_NIBBLE_RCVD_MII)) {
		drop_it:
			tg3_recycle_rx(tnapi, opaque_key,
				       desc_idx, *post_ptr);
		drop_it_no_recycle:
			/* Other statistics kept track of by card. */
			tp->net_stats.rx_dropped++;
			goto next_pkt;
		}

		len = ((desc->idx_len & RXD_LEN_MASK) >> RXD_LEN_SHIFT) -
		      ETH_FCS_LEN;

		if (len > RX_COPY_THRESHOLD
			&& tp->rx_offset == NET_IP_ALIGN
			/* rx_offset will likely not equal NET_IP_ALIGN
			 * if this is a 5701 card running in PCI-X mode
			 * [see tg3_get_invariants()]
			 */
		) {
			int skb_size;

			skb_size = tg3_alloc_rx_skb(tnapi, opaque_key,
						    desc_idx, *post_ptr);
			if (skb_size < 0)
				goto drop_it;

			pci_unmap_single(tp->pdev, dma_addr, skb_size,
					 PCI_DMA_FROMDEVICE);

			skb_put(skb, len);
		} else {
			struct sk_buff *copy_skb;

			tg3_recycle_rx(tnapi, opaque_key,
				       desc_idx, *post_ptr);

			copy_skb = netdev_alloc_skb(tp->dev,
						    len + TG3_RAW_IP_ALIGN);
			if (copy_skb == NULL)
				goto drop_it_no_recycle;

			skb_reserve(copy_skb, TG3_RAW_IP_ALIGN);
			skb_put(copy_skb, len);
			pci_dma_sync_single_for_cpu(tp->pdev, dma_addr, len, PCI_DMA_FROMDEVICE);
			skb_copy_from_linear_data(skb, copy_skb->data, len);
			pci_dma_sync_single_for_device(tp->pdev, dma_addr, len, PCI_DMA_FROMDEVICE);

			/* We'll reuse the original ring buffer. */
			skb = copy_skb;
		}

		if ((tp->tg3_flags & TG3_FLAG_RX_CHECKSUMS) &&
		    (desc->type_flags & RXD_FLAG_TCPUDP_CSUM) &&
		    (((desc->ip_tcp_csum & RXD_TCPCSUM_MASK)
		      >> RXD_TCPCSUM_SHIFT) == 0xffff))
			skb->ip_summed = CHECKSUM_UNNECESSARY;
		else
			skb->ip_summed = CHECKSUM_NONE;

		skb->protocol = eth_type_trans(skb, tp->dev);

		if (len > (tp->dev->mtu + ETH_HLEN) &&
		    skb->protocol != htons(ETH_P_8021Q)) {
			dev_kfree_skb(skb);
			goto next_pkt;
		}

#if TG3_VLAN_TAG_USED
		if (tp->vlgrp != NULL &&
		    desc->type_flags & RXD_FLAG_VLAN) {
			vlan_gro_receive(&tnapi->napi, tp->vlgrp,
					 desc->err_vlan & RXD_VLAN_MASK, skb);
		} else
#endif
			napi_gro_receive(&tnapi->napi, skb);

		received++;
		budget--;

next_pkt:
		(*post_ptr)++;

		if (unlikely(rx_std_posted >= tp->rx_std_max_post)) {
			u32 idx = *post_ptr % TG3_RX_RING_SIZE;

			tw32_rx_mbox(MAILBOX_RCV_STD_PROD_IDX +
				     TG3_64BIT_REG_LOW, idx);
			work_mask &= ~RXD_OPAQUE_RING_STD;
			rx_std_posted = 0;
		}
next_pkt_nopost:
		sw_idx++;
		sw_idx &= (TG3_RX_RCB_RING_SIZE(tp) - 1);

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
	if (work_mask & RXD_OPAQUE_RING_STD) {
		sw_idx = tpr->rx_std_ptr % TG3_RX_RING_SIZE;
		tw32_rx_mbox(MAILBOX_RCV_STD_PROD_IDX + TG3_64BIT_REG_LOW,
			     sw_idx);
	}
	if (work_mask & RXD_OPAQUE_RING_JUMBO) {
		sw_idx = tpr->rx_jmb_ptr % TG3_RX_JUMBO_RING_SIZE;
		tw32_rx_mbox(MAILBOX_RCV_JUMBO_PROD_IDX + TG3_64BIT_REG_LOW,
			     sw_idx);
	}
	mmiowb();

	return received;
}

static int tg3_poll_work(struct tg3_napi *tnapi, int work_done, int budget)
{
	struct tg3 *tp = tnapi->tp;
	struct tg3_hw_status *sblk = tnapi->hw_status;

	/* handle link change and other phy events */
	if (!(tp->tg3_flags &
	      (TG3_FLAG_USE_LINKCHG_REG |
	       TG3_FLAG_POLL_SERDES))) {
		if (sblk->status & SD_STATUS_LINK_CHG) {
			sblk->status = SD_STATUS_UPDATED |
				(sblk->status & ~SD_STATUS_LINK_CHG);
			spin_lock(&tp->lock);
			if (tp->tg3_flags3 & TG3_FLG3_USE_PHYLIB) {
				tw32_f(MAC_STATUS,
				     (MAC_STATUS_SYNC_CHANGED |
				      MAC_STATUS_CFG_CHANGED |
				      MAC_STATUS_MI_COMPLETION |
				      MAC_STATUS_LNKSTATE_CHANGED));
				udelay(40);
			} else
				tg3_setup_phy(tp, 0);
			spin_unlock(&tp->lock);
		}
	}

	/* run TX completion thread */
	if (tnapi->hw_status->idx[0].tx_consumer != tnapi->tx_cons) {
		tg3_tx(tnapi);
		if (unlikely(tp->tg3_flags & TG3_FLAG_TX_RECOVERY_PENDING))
			return work_done;
	}

	/* run RX thread, within the bounds set by NAPI.
	 * All RX "locking" is done by ensuring outside
	 * code synchronizes with tg3->napi.poll()
	 */
	if (*(tnapi->rx_rcb_prod_idx) != tnapi->rx_rcb_ptr)
		work_done += tg3_rx(tnapi, budget - work_done);

	return work_done;
}

static int tg3_poll(struct napi_struct *napi, int budget)
{
	struct tg3_napi *tnapi = container_of(napi, struct tg3_napi, napi);
	struct tg3 *tp = tnapi->tp;
	int work_done = 0;
	struct tg3_hw_status *sblk = tnapi->hw_status;

	while (1) {
		work_done = tg3_poll_work(tnapi, work_done, budget);

		if (unlikely(tp->tg3_flags & TG3_FLAG_TX_RECOVERY_PENDING))
			goto tx_recovery;

		if (unlikely(work_done >= budget))
			break;

		if (tp->tg3_flags & TG3_FLAG_TAGGED_STATUS) {
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
	schedule_work(&tp->reset_task);
	return work_done;
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

static inline int tg3_irq_sync(struct tg3 *tp)
{
	return tp->irq_sync;
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
	tw32_mailbox(MAILBOX_INTERRUPT_0 + TG3_64BIT_REG_LOW, 0x00000001);
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
		if ((tp->tg3_flags & TG3_FLAG_CHIP_RESETTING) ||
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
		if ((tp->tg3_flags & TG3_FLAG_CHIP_RESETTING) ||
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

static int tg3_init_hw(struct tg3 *, int);
static int tg3_halt(struct tg3 *, int, int);

/* Restart hardware after configuration changes, self-test, etc.
 * Invoked with tp->lock held.
 */
static int tg3_restart_hw(struct tg3 *tp, int reset_phy)
	__releases(tp->lock)
	__acquires(tp->lock)
{
	int err;

	err = tg3_init_hw(tp, reset_phy);
	if (err) {
		printk(KERN_ERR PFX "%s: Failed to re-initialize device, "
		       "aborting.\n", tp->dev->name);
		tg3_halt(tp, RESET_KIND_SHUTDOWN, 1);
		tg3_full_unlock(tp);
		del_timer_sync(&tp->timer);
		tp->irq_sync = 0;
		tg3_napi_enable(tp);
		dev_close(tp->dev);
		tg3_full_lock(tp, 0);
	}
	return err;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void tg3_poll_controller(struct net_device *dev)
{
	int i;
	struct tg3 *tp = netdev_priv(dev);

	for (i = 0; i < tp->irq_cnt; i++)
		tg3_interrupt(tp->napi[i].irq_vec, dev);
}
#endif

static void tg3_reset_task(struct work_struct *work)
{
	struct tg3 *tp = container_of(work, struct tg3, reset_task);
	int err;
	unsigned int restart_timer;

	tg3_full_lock(tp, 0);

	if (!netif_running(tp->dev)) {
		tg3_full_unlock(tp);
		return;
	}

	tg3_full_unlock(tp);

	tg3_phy_stop(tp);

	tg3_netif_stop(tp);

	tg3_full_lock(tp, 1);

	restart_timer = tp->tg3_flags2 & TG3_FLG2_RESTART_TIMER;
	tp->tg3_flags2 &= ~TG3_FLG2_RESTART_TIMER;

	if (tp->tg3_flags & TG3_FLAG_TX_RECOVERY_PENDING) {
		tp->write32_tx_mbox = tg3_write32_tx_mbox;
		tp->write32_rx_mbox = tg3_write_flush_reg32;
		tp->tg3_flags |= TG3_FLAG_MBOX_WRITE_REORDER;
		tp->tg3_flags &= ~TG3_FLAG_TX_RECOVERY_PENDING;
	}

	tg3_halt(tp, RESET_KIND_SHUTDOWN, 0);
	err = tg3_init_hw(tp, 1);
	if (err)
		goto out;

	tg3_netif_start(tp);

	if (restart_timer)
		mod_timer(&tp->timer, jiffies + 1);

out:
	tg3_full_unlock(tp);

	if (!err)
		tg3_phy_start(tp);
}

static void tg3_dump_short_state(struct tg3 *tp)
{
	printk(KERN_ERR PFX "DEBUG: MAC_TX_STATUS[%08x] MAC_RX_STATUS[%08x]\n",
	       tr32(MAC_TX_STATUS), tr32(MAC_RX_STATUS));
	printk(KERN_ERR PFX "DEBUG: RDMAC_STATUS[%08x] WDMAC_STATUS[%08x]\n",
	       tr32(RDMAC_STATUS), tr32(WDMAC_STATUS));
}

static void tg3_tx_timeout(struct net_device *dev)
{
	struct tg3 *tp = netdev_priv(dev);

	if (netif_msg_tx_err(tp)) {
		printk(KERN_ERR PFX "%s: transmit timed out, resetting\n",
		       dev->name);
		tg3_dump_short_state(tp);
	}

	schedule_work(&tp->reset_task);
}

/* Test for DMA buffers crossing any 4GB boundaries: 4G, 8G, etc */
static inline int tg3_4g_overflow_test(dma_addr_t mapping, int len)
{
	u32 base = (u32) mapping & 0xffffffff;

	return ((base > 0xffffdcc0) &&
		(base + len + 8 < base));
}

/* Test for DMA addresses > 40-bit */
static inline int tg3_40bit_overflow_test(struct tg3 *tp, dma_addr_t mapping,
					  int len)
{
#if defined(CONFIG_HIGHMEM) && (BITS_PER_LONG == 64)
	if (tp->tg3_flags & TG3_FLAG_40BIT_DMA_BUG)
		return (((u64) mapping + len) > DMA_BIT_MASK(40));
	return 0;
#else
	return 0;
#endif
}

static void tg3_set_txd(struct tg3_napi *, int, dma_addr_t, int, u32, u32);

/* Workaround 4GB and 40-bit hardware DMA bugs. */
static int tigon3_dma_hwbug_workaround(struct tg3 *tp, struct sk_buff *skb,
				       u32 last_plus_one, u32 *start,
				       u32 base_flags, u32 mss)
{
	struct tg3_napi *tnapi = &tp->napi[0];
	struct sk_buff *new_skb;
	dma_addr_t new_addr = 0;
	u32 entry = *start;
	int i, ret = 0;

	if (GET_ASIC_REV(tp->pci_chip_rev_id) != ASIC_REV_5701)
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
		entry = *start;
		ret = skb_dma_map(&tp->pdev->dev, new_skb, DMA_TO_DEVICE);
		new_addr = skb_shinfo(new_skb)->dma_head;

		/* Make sure new skb does not cross any 4G boundaries.
		 * Drop the packet if it does.
		 */
		if (ret || ((tp->tg3_flags3 & TG3_FLG3_4G_DMA_BNDRY_BUG) &&
			    tg3_4g_overflow_test(new_addr, new_skb->len))) {
			if (!ret)
				skb_dma_unmap(&tp->pdev->dev, new_skb,
					      DMA_TO_DEVICE);
			ret = -1;
			dev_kfree_skb(new_skb);
			new_skb = NULL;
		} else {
			tg3_set_txd(tnapi, entry, new_addr, new_skb->len,
				    base_flags, 1 | (mss << 1));
			*start = NEXT_TX(entry);
		}
	}

	/* Now clean up the sw ring entries. */
	i = 0;
	while (entry != last_plus_one) {
		if (i == 0)
			tnapi->tx_buffers[entry].skb = new_skb;
		else
			tnapi->tx_buffers[entry].skb = NULL;
		entry = NEXT_TX(entry);
		i++;
	}

	skb_dma_unmap(&tp->pdev->dev, skb, DMA_TO_DEVICE);
	dev_kfree_skb(skb);

	return ret;
}

static void tg3_set_txd(struct tg3_napi *tnapi, int entry,
			dma_addr_t mapping, int len, u32 flags,
			u32 mss_and_is_end)
{
	struct tg3_tx_buffer_desc *txd = &tnapi->tx_ring[entry];
	int is_end = (mss_and_is_end & 0x1);
	u32 mss = (mss_and_is_end >> 1);
	u32 vlan_tag = 0;

	if (is_end)
		flags |= TXD_FLAG_END;
	if (flags & TXD_FLAG_VLAN) {
		vlan_tag = flags >> 16;
		flags &= 0xffff;
	}
	vlan_tag |= (mss << TXD_MSS_SHIFT);

	txd->addr_hi = ((u64) mapping >> 32);
	txd->addr_lo = ((u64) mapping & 0xffffffff);
	txd->len_flags = (len << TXD_LEN_SHIFT) | flags;
	txd->vlan_tag = vlan_tag << TXD_VLAN_TAG_SHIFT;
}

/* hard_start_xmit for devices that don't have any bugs and
 * support TG3_FLG2_HW_TSO_2 only.
 */
static netdev_tx_t tg3_start_xmit(struct sk_buff *skb,
				  struct net_device *dev)
{
	struct tg3 *tp = netdev_priv(dev);
	u32 len, entry, base_flags, mss;
	struct skb_shared_info *sp;
	dma_addr_t mapping;
	struct tg3_napi *tnapi;
	struct netdev_queue *txq;

	txq = netdev_get_tx_queue(dev, skb_get_queue_mapping(skb));
	tnapi = &tp->napi[skb_get_queue_mapping(skb)];
	if (tp->tg3_flags2 & TG3_FLG2_USING_MSIX)
		tnapi++;

	/* We are running in BH disabled context with netif_tx_lock
	 * and TX reclaim runs via tp->napi.poll inside of a software
	 * interrupt.  Furthermore, IRQ processing runs lockless so we have
	 * no IRQ context deadlocks to worry about either.  Rejoice!
	 */
	if (unlikely(tg3_tx_avail(tnapi) <= (skb_shinfo(skb)->nr_frags + 1))) {
		if (!netif_tx_queue_stopped(txq)) {
			netif_tx_stop_queue(txq);

			/* This is a hard error, log it. */
			printk(KERN_ERR PFX "%s: BUG! Tx Ring full when "
			       "queue awake!\n", dev->name);
		}
		return NETDEV_TX_BUSY;
	}

	entry = tnapi->tx_prod;
	base_flags = 0;
	mss = 0;
	if ((mss = skb_shinfo(skb)->gso_size) != 0) {
		int tcp_opt_len, ip_tcp_len;
		u32 hdrlen;

		if (skb_header_cloned(skb) &&
		    pskb_expand_head(skb, 0, 0, GFP_ATOMIC)) {
			dev_kfree_skb(skb);
			goto out_unlock;
		}

		if (skb_shinfo(skb)->gso_type & SKB_GSO_TCPV6)
			hdrlen = skb_headlen(skb) - ETH_HLEN;
		else {
			struct iphdr *iph = ip_hdr(skb);

			tcp_opt_len = tcp_optlen(skb);
			ip_tcp_len = ip_hdrlen(skb) + sizeof(struct tcphdr);

			iph->check = 0;
			iph->tot_len = htons(mss + ip_tcp_len + tcp_opt_len);
			hdrlen = ip_tcp_len + tcp_opt_len;
		}

		if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5717) {
			mss |= (hdrlen & 0xc) << 12;
			if (hdrlen & 0x10)
				base_flags |= 0x00000010;
			base_flags |= (hdrlen & 0x3e0) << 5;
		} else
			mss |= hdrlen << 9;

		base_flags |= (TXD_FLAG_CPU_PRE_DMA |
			       TXD_FLAG_CPU_POST_DMA);

		tcp_hdr(skb)->check = 0;

	}
	else if (skb->ip_summed == CHECKSUM_PARTIAL)
		base_flags |= TXD_FLAG_TCPUDP_CSUM;
#if TG3_VLAN_TAG_USED
	if (tp->vlgrp != NULL && vlan_tx_tag_present(skb))
		base_flags |= (TXD_FLAG_VLAN |
			       (vlan_tx_tag_get(skb) << 16));
#endif

	if (skb_dma_map(&tp->pdev->dev, skb, DMA_TO_DEVICE)) {
		dev_kfree_skb(skb);
		goto out_unlock;
	}

	sp = skb_shinfo(skb);

	mapping = sp->dma_head;

	tnapi->tx_buffers[entry].skb = skb;

	len = skb_headlen(skb);

	if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5717 &&
	    !mss && skb->len > ETH_DATA_LEN)
		base_flags |= TXD_FLAG_JMB_PKT;

	tg3_set_txd(tnapi, entry, mapping, len, base_flags,
		    (skb_shinfo(skb)->nr_frags == 0) | (mss << 1));

	entry = NEXT_TX(entry);

	/* Now loop through additional data fragments, and queue them. */
	if (skb_shinfo(skb)->nr_frags > 0) {
		unsigned int i, last;

		last = skb_shinfo(skb)->nr_frags - 1;
		for (i = 0; i <= last; i++) {
			skb_frag_t *frag = &skb_shinfo(skb)->frags[i];

			len = frag->size;
			mapping = sp->dma_maps[i];
			tnapi->tx_buffers[entry].skb = NULL;

			tg3_set_txd(tnapi, entry, mapping, len,
				    base_flags, (i == last) | (mss << 1));

			entry = NEXT_TX(entry);
		}
	}

	/* Packets are ready, update Tx producer idx local and on card. */
	tw32_tx_mbox(tnapi->prodmbox, entry);

	tnapi->tx_prod = entry;
	if (unlikely(tg3_tx_avail(tnapi) <= (MAX_SKB_FRAGS + 1))) {
		netif_tx_stop_queue(txq);
		if (tg3_tx_avail(tnapi) > TG3_TX_WAKEUP_THRESH(tnapi))
			netif_tx_wake_queue(txq);
	}

out_unlock:
	mmiowb();

	return NETDEV_TX_OK;
}

static netdev_tx_t tg3_start_xmit_dma_bug(struct sk_buff *,
					  struct net_device *);

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
		tg3_start_xmit_dma_bug(nskb, tp->dev);
	} while (segs);

tg3_tso_bug_end:
	dev_kfree_skb(skb);

	return NETDEV_TX_OK;
}

/* hard_start_xmit for devices that have the 4G bug and/or 40-bit bug and
 * support TG3_FLG2_HW_TSO_1 or firmware TSO only.
 */
static netdev_tx_t tg3_start_xmit_dma_bug(struct sk_buff *skb,
					  struct net_device *dev)
{
	struct tg3 *tp = netdev_priv(dev);
	u32 len, entry, base_flags, mss;
	struct skb_shared_info *sp;
	int would_hit_hwbug;
	dma_addr_t mapping;
	struct tg3_napi *tnapi = &tp->napi[0];

	len = skb_headlen(skb);

	/* We are running in BH disabled context with netif_tx_lock
	 * and TX reclaim runs via tp->napi.poll inside of a software
	 * interrupt.  Furthermore, IRQ processing runs lockless so we have
	 * no IRQ context deadlocks to worry about either.  Rejoice!
	 */
	if (unlikely(tg3_tx_avail(tnapi) <= (skb_shinfo(skb)->nr_frags + 1))) {
		if (!netif_queue_stopped(dev)) {
			netif_stop_queue(dev);

			/* This is a hard error, log it. */
			printk(KERN_ERR PFX "%s: BUG! Tx Ring full when "
			       "queue awake!\n", dev->name);
		}
		return NETDEV_TX_BUSY;
	}

	entry = tnapi->tx_prod;
	base_flags = 0;
	if (skb->ip_summed == CHECKSUM_PARTIAL)
		base_flags |= TXD_FLAG_TCPUDP_CSUM;
	mss = 0;
	if ((mss = skb_shinfo(skb)->gso_size) != 0) {
		struct iphdr *iph;
		u32 tcp_opt_len, ip_tcp_len, hdr_len;

		if (skb_header_cloned(skb) &&
		    pskb_expand_head(skb, 0, 0, GFP_ATOMIC)) {
			dev_kfree_skb(skb);
			goto out_unlock;
		}

		tcp_opt_len = tcp_optlen(skb);
		ip_tcp_len = ip_hdrlen(skb) + sizeof(struct tcphdr);

		hdr_len = ip_tcp_len + tcp_opt_len;
		if (unlikely((ETH_HLEN + hdr_len) > 80) &&
			     (tp->tg3_flags2 & TG3_FLG2_TSO_BUG))
			return (tg3_tso_bug(tp, skb));

		base_flags |= (TXD_FLAG_CPU_PRE_DMA |
			       TXD_FLAG_CPU_POST_DMA);

		iph = ip_hdr(skb);
		iph->check = 0;
		iph->tot_len = htons(mss + hdr_len);
		if (tp->tg3_flags2 & TG3_FLG2_HW_TSO) {
			tcp_hdr(skb)->check = 0;
			base_flags &= ~TXD_FLAG_TCPUDP_CSUM;
		} else
			tcp_hdr(skb)->check = ~csum_tcpudp_magic(iph->saddr,
								 iph->daddr, 0,
								 IPPROTO_TCP,
								 0);

		if (tp->tg3_flags2 & TG3_FLG2_HW_TSO_2)
			mss |= hdr_len << 9;
		else if ((tp->tg3_flags2 & TG3_FLG2_HW_TSO_1) ||
			 GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5705) {
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
	}
#if TG3_VLAN_TAG_USED
	if (tp->vlgrp != NULL && vlan_tx_tag_present(skb))
		base_flags |= (TXD_FLAG_VLAN |
			       (vlan_tx_tag_get(skb) << 16));
#endif

	if (skb_dma_map(&tp->pdev->dev, skb, DMA_TO_DEVICE)) {
		dev_kfree_skb(skb);
		goto out_unlock;
	}

	sp = skb_shinfo(skb);

	mapping = sp->dma_head;

	tnapi->tx_buffers[entry].skb = skb;

	would_hit_hwbug = 0;

	if ((tp->tg3_flags3 & TG3_FLG3_SHORT_DMA_BUG) && len <= 8)
		would_hit_hwbug = 1;

	if ((tp->tg3_flags3 & TG3_FLG3_4G_DMA_BNDRY_BUG) &&
	    tg3_4g_overflow_test(mapping, len))
		would_hit_hwbug = 1;

	if ((tp->tg3_flags3 & TG3_FLG3_40BIT_DMA_LIMIT_BUG) &&
	    tg3_40bit_overflow_test(tp, mapping, len))
		would_hit_hwbug = 1;

	if (tp->tg3_flags3 & TG3_FLG3_5701_DMA_BUG)
		would_hit_hwbug = 1;

	tg3_set_txd(tnapi, entry, mapping, len, base_flags,
		    (skb_shinfo(skb)->nr_frags == 0) | (mss << 1));

	entry = NEXT_TX(entry);

	/* Now loop through additional data fragments, and queue them. */
	if (skb_shinfo(skb)->nr_frags > 0) {
		unsigned int i, last;

		last = skb_shinfo(skb)->nr_frags - 1;
		for (i = 0; i <= last; i++) {
			skb_frag_t *frag = &skb_shinfo(skb)->frags[i];

			len = frag->size;
			mapping = sp->dma_maps[i];

			tnapi->tx_buffers[entry].skb = NULL;

			if ((tp->tg3_flags3 & TG3_FLG3_SHORT_DMA_BUG) &&
			    len <= 8)
				would_hit_hwbug = 1;

			if ((tp->tg3_flags3 & TG3_FLG3_4G_DMA_BNDRY_BUG) &&
			    tg3_4g_overflow_test(mapping, len))
				would_hit_hwbug = 1;

			if ((tp->tg3_flags3 & TG3_FLG3_40BIT_DMA_LIMIT_BUG) &&
			    tg3_40bit_overflow_test(tp, mapping, len))
				would_hit_hwbug = 1;

			if (tp->tg3_flags2 & TG3_FLG2_HW_TSO)
				tg3_set_txd(tnapi, entry, mapping, len,
					    base_flags, (i == last)|(mss << 1));
			else
				tg3_set_txd(tnapi, entry, mapping, len,
					    base_flags, (i == last));

			entry = NEXT_TX(entry);
		}
	}

	if (would_hit_hwbug) {
		u32 last_plus_one = entry;
		u32 start;

		start = entry - 1 - skb_shinfo(skb)->nr_frags;
		start &= (TG3_TX_RING_SIZE - 1);

		/* If the workaround fails due to memory/mapping
		 * failure, silently drop this packet.
		 */
		if (tigon3_dma_hwbug_workaround(tp, skb, last_plus_one,
						&start, base_flags, mss))
			goto out_unlock;

		entry = start;
	}

	/* Packets are ready, update Tx producer idx local and on card. */
	tw32_tx_mbox(MAILBOX_SNDHOST_PROD_IDX_0 + TG3_64BIT_REG_LOW, entry);

	tnapi->tx_prod = entry;
	if (unlikely(tg3_tx_avail(tnapi) <= (MAX_SKB_FRAGS + 1))) {
		netif_stop_queue(dev);
		if (tg3_tx_avail(tnapi) > TG3_TX_WAKEUP_THRESH(tnapi))
			netif_wake_queue(tp->dev);
	}

out_unlock:
	mmiowb();

	return NETDEV_TX_OK;
}

static inline void tg3_set_mtu(struct net_device *dev, struct tg3 *tp,
			       int new_mtu)
{
	dev->mtu = new_mtu;

	if (new_mtu > ETH_DATA_LEN) {
		if (tp->tg3_flags2 & TG3_FLG2_5780_CLASS) {
			tp->tg3_flags2 &= ~TG3_FLG2_TSO_CAPABLE;
			ethtool_op_set_tso(dev, 0);
		}
		else
			tp->tg3_flags |= TG3_FLAG_JUMBO_RING_ENABLE;
	} else {
		if (tp->tg3_flags2 & TG3_FLG2_5780_CLASS)
			tp->tg3_flags2 |= TG3_FLG2_TSO_CAPABLE;
		tp->tg3_flags &= ~TG3_FLAG_JUMBO_RING_ENABLE;
	}
}

static int tg3_change_mtu(struct net_device *dev, int new_mtu)
{
	struct tg3 *tp = netdev_priv(dev);
	int err;

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

	tg3_full_lock(tp, 1);

	tg3_halt(tp, RESET_KIND_SHUTDOWN, 1);

	tg3_set_mtu(dev, tp, new_mtu);

	err = tg3_restart_hw(tp, 0);

	if (!err)
		tg3_netif_start(tp);

	tg3_full_unlock(tp);

	if (!err)
		tg3_phy_start(tp);

	return err;
}

static void tg3_rx_prodring_free(struct tg3 *tp,
				 struct tg3_rx_prodring_set *tpr)
{
	int i;
	struct ring_info *rxp;

	for (i = 0; i < TG3_RX_RING_SIZE; i++) {
		rxp = &tpr->rx_std_buffers[i];

		if (rxp->skb == NULL)
			continue;

		pci_unmap_single(tp->pdev,
				 pci_unmap_addr(rxp, mapping),
				 tp->rx_pkt_map_sz,
				 PCI_DMA_FROMDEVICE);
		dev_kfree_skb_any(rxp->skb);
		rxp->skb = NULL;
	}

	if (tp->tg3_flags & TG3_FLAG_JUMBO_CAPABLE) {
		for (i = 0; i < TG3_RX_JUMBO_RING_SIZE; i++) {
			rxp = &tpr->rx_jmb_buffers[i];

			if (rxp->skb == NULL)
				continue;

			pci_unmap_single(tp->pdev,
					 pci_unmap_addr(rxp, mapping),
					 TG3_RX_JMB_MAP_SZ,
					 PCI_DMA_FROMDEVICE);
			dev_kfree_skb_any(rxp->skb);
			rxp->skb = NULL;
		}
	}
}

/* Initialize tx/rx rings for packet processing.
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
	struct tg3_napi *tnapi = &tp->napi[0];

	/* Zero out all descriptors. */
	memset(tpr->rx_std, 0, TG3_RX_RING_BYTES);

	rx_pkt_dma_sz = TG3_RX_STD_DMA_SZ;
	if ((tp->tg3_flags2 & TG3_FLG2_5780_CLASS) &&
	    tp->dev->mtu > ETH_DATA_LEN)
		rx_pkt_dma_sz = TG3_RX_JMB_DMA_SZ;
	tp->rx_pkt_map_sz = TG3_RX_DMA_TO_MAP_SZ(rx_pkt_dma_sz);

	/* Initialize invariants of the rings, we only set this
	 * stuff once.  This works because the card does not
	 * write into the rx buffer posting rings.
	 */
	for (i = 0; i < TG3_RX_RING_SIZE; i++) {
		struct tg3_rx_buffer_desc *rxd;

		rxd = &tpr->rx_std[i];
		rxd->idx_len = rx_pkt_dma_sz << RXD_LEN_SHIFT;
		rxd->type_flags = (RXD_FLAG_END << RXD_FLAGS_SHIFT);
		rxd->opaque = (RXD_OPAQUE_RING_STD |
			       (i << RXD_OPAQUE_INDEX_SHIFT));
	}

	/* Now allocate fresh SKBs for each rx ring. */
	for (i = 0; i < tp->rx_pending; i++) {
		if (tg3_alloc_rx_skb(tnapi, RXD_OPAQUE_RING_STD, -1, i) < 0) {
			printk(KERN_WARNING PFX
			       "%s: Using a smaller RX standard ring, "
			       "only %d out of %d buffers were allocated "
			       "successfully.\n",
			       tp->dev->name, i, tp->rx_pending);
			if (i == 0)
				goto initfail;
			tp->rx_pending = i;
			break;
		}
	}

	if (!(tp->tg3_flags & TG3_FLAG_JUMBO_CAPABLE))
		goto done;

	memset(tpr->rx_jmb, 0, TG3_RX_JUMBO_RING_BYTES);

	if (tp->tg3_flags & TG3_FLAG_JUMBO_RING_ENABLE) {
		for (i = 0; i < TG3_RX_JUMBO_RING_SIZE; i++) {
			struct tg3_rx_buffer_desc *rxd;

			rxd = &tpr->rx_jmb[i].std;
			rxd->idx_len = TG3_RX_JMB_DMA_SZ << RXD_LEN_SHIFT;
			rxd->type_flags = (RXD_FLAG_END << RXD_FLAGS_SHIFT) |
				RXD_FLAG_JUMBO;
			rxd->opaque = (RXD_OPAQUE_RING_JUMBO |
			       (i << RXD_OPAQUE_INDEX_SHIFT));
		}

		for (i = 0; i < tp->rx_jumbo_pending; i++) {
			if (tg3_alloc_rx_skb(tnapi, RXD_OPAQUE_RING_JUMBO,
					     -1, i) < 0) {
				printk(KERN_WARNING PFX
				       "%s: Using a smaller RX jumbo ring, "
				       "only %d out of %d buffers were "
				       "allocated successfully.\n",
				       tp->dev->name, i, tp->rx_jumbo_pending);
				if (i == 0)
					goto initfail;
				tp->rx_jumbo_pending = i;
				break;
			}
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
		pci_free_consistent(tp->pdev, TG3_RX_RING_BYTES,
				    tpr->rx_std, tpr->rx_std_mapping);
		tpr->rx_std = NULL;
	}
	if (tpr->rx_jmb) {
		pci_free_consistent(tp->pdev, TG3_RX_JUMBO_RING_BYTES,
				    tpr->rx_jmb, tpr->rx_jmb_mapping);
		tpr->rx_jmb = NULL;
	}
}

static int tg3_rx_prodring_init(struct tg3 *tp,
				struct tg3_rx_prodring_set *tpr)
{
	tpr->rx_std_buffers = kzalloc(sizeof(struct ring_info) *
				      TG3_RX_RING_SIZE, GFP_KERNEL);
	if (!tpr->rx_std_buffers)
		return -ENOMEM;

	tpr->rx_std = pci_alloc_consistent(tp->pdev, TG3_RX_RING_BYTES,
					   &tpr->rx_std_mapping);
	if (!tpr->rx_std)
		goto err_out;

	if (tp->tg3_flags & TG3_FLAG_JUMBO_CAPABLE) {
		tpr->rx_jmb_buffers = kzalloc(sizeof(struct ring_info) *
					      TG3_RX_JUMBO_RING_SIZE,
					      GFP_KERNEL);
		if (!tpr->rx_jmb_buffers)
			goto err_out;

		tpr->rx_jmb = pci_alloc_consistent(tp->pdev,
						   TG3_RX_JUMBO_RING_BYTES,
						   &tpr->rx_jmb_mapping);
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

		if (!tnapi->tx_buffers)
			continue;

		for (i = 0; i < TG3_TX_RING_SIZE; ) {
			struct tx_ring_info *txp;
			struct sk_buff *skb;

			txp = &tnapi->tx_buffers[i];
			skb = txp->skb;

			if (skb == NULL) {
				i++;
				continue;
			}

			skb_dma_unmap(&tp->pdev->dev, skb, DMA_TO_DEVICE);

			txp->skb = NULL;

			i += skb_shinfo(skb)->nr_frags + 1;

			dev_kfree_skb_any(skb);
		}
	}

	tg3_rx_prodring_free(tp, &tp->prodring[0]);
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
	}

	return tg3_rx_prodring_alloc(tp, &tp->prodring[0]);
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

		if (tnapi->tx_ring) {
			pci_free_consistent(tp->pdev, TG3_TX_RING_BYTES,
				tnapi->tx_ring, tnapi->tx_desc_mapping);
			tnapi->tx_ring = NULL;
		}

		kfree(tnapi->tx_buffers);
		tnapi->tx_buffers = NULL;

		if (tnapi->rx_rcb) {
			pci_free_consistent(tp->pdev, TG3_RX_RCB_RING_BYTES(tp),
					    tnapi->rx_rcb,
					    tnapi->rx_rcb_mapping);
			tnapi->rx_rcb = NULL;
		}

		if (tnapi->hw_status) {
			pci_free_consistent(tp->pdev, TG3_HW_STATUS_SIZE,
					    tnapi->hw_status,
					    tnapi->status_mapping);
			tnapi->hw_status = NULL;
		}
	}

	if (tp->hw_stats) {
		pci_free_consistent(tp->pdev, sizeof(struct tg3_hw_stats),
				    tp->hw_stats, tp->stats_mapping);
		tp->hw_stats = NULL;
	}

	tg3_rx_prodring_fini(tp, &tp->prodring[0]);
}

/*
 * Must not be invoked with interrupt sources disabled and
 * the hardware shutdown down.  Can sleep.
 */
static int tg3_alloc_consistent(struct tg3 *tp)
{
	int i;

	if (tg3_rx_prodring_init(tp, &tp->prodring[0]))
		return -ENOMEM;

	tp->hw_stats = pci_alloc_consistent(tp->pdev,
					    sizeof(struct tg3_hw_stats),
					    &tp->stats_mapping);
	if (!tp->hw_stats)
		goto err_out;

	memset(tp->hw_stats, 0, sizeof(struct tg3_hw_stats));

	for (i = 0; i < tp->irq_cnt; i++) {
		struct tg3_napi *tnapi = &tp->napi[i];
		struct tg3_hw_status *sblk;

		tnapi->hw_status = pci_alloc_consistent(tp->pdev,
							TG3_HW_STATUS_SIZE,
							&tnapi->status_mapping);
		if (!tnapi->hw_status)
			goto err_out;

		memset(tnapi->hw_status, 0, TG3_HW_STATUS_SIZE);
		sblk = tnapi->hw_status;

		/*
		 * When RSS is enabled, the status block format changes
		 * slightly.  The "rx_jumbo_consumer", "reserved",
		 * and "rx_mini_consumer" members get mapped to the
		 * other three rx return ring producer indexes.
		 */
		switch (i) {
		default:
			tnapi->rx_rcb_prod_idx = &sblk->idx[0].rx_producer;
			break;
		case 2:
			tnapi->rx_rcb_prod_idx = &sblk->rx_jumbo_consumer;
			break;
		case 3:
			tnapi->rx_rcb_prod_idx = &sblk->reserved;
			break;
		case 4:
			tnapi->rx_rcb_prod_idx = &sblk->rx_mini_consumer;
			break;
		}

		/*
		 * If multivector RSS is enabled, vector 0 does not handle
		 * rx or tx interrupts.  Don't allocate any resources for it.
		 */
		if (!i && (tp->tg3_flags3 & TG3_FLG3_ENABLE_RSS))
			continue;

		tnapi->rx_rcb = pci_alloc_consistent(tp->pdev,
						     TG3_RX_RCB_RING_BYTES(tp),
						     &tnapi->rx_rcb_mapping);
		if (!tnapi->rx_rcb)
			goto err_out;

		memset(tnapi->rx_rcb, 0, TG3_RX_RCB_RING_BYTES(tp));

		tnapi->tx_buffers = kzalloc(sizeof(struct tx_ring_info) *
					    TG3_TX_RING_SIZE, GFP_KERNEL);
		if (!tnapi->tx_buffers)
			goto err_out;

		tnapi->tx_ring = pci_alloc_consistent(tp->pdev,
						      TG3_TX_RING_BYTES,
						      &tnapi->tx_desc_mapping);
		if (!tnapi->tx_ring)
			goto err_out;
	}

	return 0;

err_out:
	tg3_free_consistent(tp);
	return -ENOMEM;
}

#define MAX_WAIT_CNT 1000

/* To stop a block, clear the enable bit and poll till it
 * clears.  tp->lock is held.
 */
static int tg3_stop_block(struct tg3 *tp, unsigned long ofs, u32 enable_bit, int silent)
{
	unsigned int i;
	u32 val;

	if (tp->tg3_flags2 & TG3_FLG2_5705_PLUS) {
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
		udelay(100);
		val = tr32(ofs);
		if ((val & enable_bit) == 0)
			break;
	}

	if (i == MAX_WAIT_CNT && !silent) {
		printk(KERN_ERR PFX "tg3_stop_block timed out, "
		       "ofs=%lx enable_bit=%x\n",
		       ofs, enable_bit);
		return -ENODEV;
	}

	return 0;
}

/* tp->lock is held. */
static int tg3_abort_hw(struct tg3 *tp, int silent)
{
	int i, err;

	tg3_disable_ints(tp);

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
		printk(KERN_ERR PFX "tg3_abort_hw timed out for %s, "
		       "TX_MODE_ENABLE will not clear MAC_TX_MODE=%08x\n",
		       tp->dev->name, tr32(MAC_TX_MODE));
		err |= -ENODEV;
	}

	err |= tg3_stop_block(tp, HOSTCC_MODE, HOSTCC_MODE_ENABLE, silent);
	err |= tg3_stop_block(tp, WDMAC_MODE, WDMAC_MODE_ENABLE, silent);
	err |= tg3_stop_block(tp, MBFREE_MODE, MBFREE_MODE_ENABLE, silent);

	tw32(FTQ_RESET, 0xffffffff);
	tw32(FTQ_RESET, 0x00000000);

	err |= tg3_stop_block(tp, BUFMGR_MODE, BUFMGR_MODE_ENABLE, silent);
	err |= tg3_stop_block(tp, MEMARB_MODE, MEMARB_MODE_ENABLE, silent);

	for (i = 0; i < tp->irq_cnt; i++) {
		struct tg3_napi *tnapi = &tp->napi[i];
		if (tnapi->hw_status)
			memset(tnapi->hw_status, 0, TG3_HW_STATUS_SIZE);
	}
	if (tp->hw_stats)
		memset(tp->hw_stats, 0, sizeof(struct tg3_hw_stats));

	return err;
}

static void tg3_ape_send_event(struct tg3 *tp, u32 event)
{
	int i;
	u32 apedata;

	apedata = tg3_ape_read32(tp, TG3_APE_SEG_SIG);
	if (apedata != APE_SEG_SIG_MAGIC)
		return;

	apedata = tg3_ape_read32(tp, TG3_APE_FW_STATUS);
	if (!(apedata & APE_FW_STATUS_READY))
		return;

	/* Wait for up to 1 millisecond for APE to service previous event. */
	for (i = 0; i < 10; i++) {
		if (tg3_ape_lock(tp, TG3_APE_LOCK_MEM))
			return;

		apedata = tg3_ape_read32(tp, TG3_APE_EVENT_STATUS);

		if (!(apedata & APE_EVENT_STATUS_EVENT_PENDING))
			tg3_ape_write32(tp, TG3_APE_EVENT_STATUS,
					event | APE_EVENT_STATUS_EVENT_PENDING);

		tg3_ape_unlock(tp, TG3_APE_LOCK_MEM);

		if (!(apedata & APE_EVENT_STATUS_EVENT_PENDING))
			break;

		udelay(100);
	}

	if (!(apedata & APE_EVENT_STATUS_EVENT_PENDING))
		tg3_ape_write32(tp, TG3_APE_EVENT, APE_EVENT_1);
}

static void tg3_ape_driver_state_change(struct tg3 *tp, int kind)
{
	u32 event;
	u32 apedata;

	if (!(tp->tg3_flags3 & TG3_FLG3_ENABLE_APE))
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
					APE_HOST_DRIVER_ID_MAGIC);
			tg3_ape_write32(tp, TG3_APE_HOST_BEHAVIOR,
					APE_HOST_BEHAV_NO_PHYLOCK);

			event = APE_EVENT_STATUS_STATE_START;
			break;
		case RESET_KIND_SHUTDOWN:
			/* With the interface we are currently using,
			 * APE does not track driver state.  Wiping
			 * out the HOST SEGMENT SIGNATURE forces
			 * the APE to assume OS absent status.
			 */
			tg3_ape_write32(tp, TG3_APE_HOST_SEG_SIG, 0x0);

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

/* tp->lock is held. */
static void tg3_write_sig_pre_reset(struct tg3 *tp, int kind)
{
	tg3_write_mem(tp, NIC_SRAM_FIRMWARE_MBOX,
		      NIC_SRAM_FIRMWARE_MBOX_MAGIC1);

	if (tp->tg3_flags2 & TG3_FLG2_ASF_NEW_HANDSHAKE) {
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
	if (tp->tg3_flags2 & TG3_FLG2_ASF_NEW_HANDSHAKE) {
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
	if (tp->tg3_flags & TG3_FLAG_ENABLE_ASF) {
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

	if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5906) {
		/* Wait up to 20ms for init done. */
		for (i = 0; i < 200; i++) {
			if (tr32(VCPU_STATUS) & VCPU_STATUS_INIT_DONE)
				return 0;
			udelay(100);
		}
		return -ENODEV;
	}

	/* Wait for firmware initialization to complete. */
	for (i = 0; i < 100000; i++) {
		tg3_read_mem(tp, NIC_SRAM_FIRMWARE_MBOX, &val);
		if (val == ~NIC_SRAM_FIRMWARE_MBOX_MAGIC1)
			break;
		udelay(10);
	}

	/* Chip might not be fitted with firmware.  Some Sun onboard
	 * parts are configured like that.  So don't signal the timeout
	 * of the above loop as an error, but do report the lack of
	 * running firmware once.
	 */
	if (i >= 100000 &&
	    !(tp->tg3_flags2 & TG3_FLG2_NO_FWARE_REPORTED)) {
		tp->tg3_flags2 |= TG3_FLG2_NO_FWARE_REPORTED;

		printk(KERN_INFO PFX "%s: No firmware running.\n",
		       tp->dev->name);
	}

	return 0;
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
	if (tp->pci_chip_rev_id == CHIPREV_ID_5704_A0 &&
	    (tp->tg3_flags & TG3_FLAG_PCIX_MODE))
		val |= PCISTATE_RETRY_SAME_DMA;
	/* Allow reads and writes to the APE register and memory space. */
	if (tp->tg3_flags3 & TG3_FLG3_ENABLE_APE)
		val |= PCISTATE_ALLOW_APE_CTLSPC_WR |
		       PCISTATE_ALLOW_APE_SHMEM_WR;
	pci_write_config_dword(tp->pdev, TG3PCI_PCISTATE, val);

	pci_write_config_word(tp->pdev, PCI_COMMAND, tp->pci_cmd);

	if (GET_ASIC_REV(tp->pci_chip_rev_id) != ASIC_REV_5785) {
		if (tp->tg3_flags2 & TG3_FLG2_PCI_EXPRESS)
			pcie_set_readrq(tp->pdev, 4096);
		else {
			pci_write_config_byte(tp->pdev, PCI_CACHE_LINE_SIZE,
					      tp->pci_cacheline_sz);
			pci_write_config_byte(tp->pdev, PCI_LATENCY_TIMER,
					      tp->pci_lat_timer);
		}
	}

	/* Make sure PCI-X relaxed ordering bit is clear. */
	if (tp->tg3_flags & TG3_FLAG_PCIX_MODE) {
		u16 pcix_cmd;

		pci_read_config_word(tp->pdev, tp->pcix_cap + PCI_X_CMD,
				     &pcix_cmd);
		pcix_cmd &= ~PCI_X_CMD_ERO;
		pci_write_config_word(tp->pdev, tp->pcix_cap + PCI_X_CMD,
				      pcix_cmd);
	}

	if (tp->tg3_flags2 & TG3_FLG2_5780_CLASS) {

		/* Chip reset on 5780 will reset MSI enable bit,
		 * so need to restore it.
		 */
		if (tp->tg3_flags2 & TG3_FLG2_USING_MSI) {
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

static void tg3_stop_fw(struct tg3 *);

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

	if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5752 ||
	    (tp->tg3_flags3 & TG3_FLG3_5755_PLUS))
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
	tp->tg3_flags |= TG3_FLAG_CHIP_RESETTING;
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

	if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_57780) {
		val = tr32(TG3_PCIE_LNKCTL) & ~TG3_PCIE_LNKCTL_L1_PLL_PD_EN;
		tw32(TG3_PCIE_LNKCTL, val | TG3_PCIE_LNKCTL_L1_PLL_PD_DIS);
	}

	/* do the reset */
	val = GRC_MISC_CFG_CORECLK_RESET;

	if (tp->tg3_flags2 & TG3_FLG2_PCI_EXPRESS) {
		if (tr32(0x7e2c) == 0x60) {
			tw32(0x7e2c, 0x20);
		}
		if (tp->pci_chip_rev_id != CHIPREV_ID_5750_A0) {
			tw32(GRC_MISC_CFG, (1 << 29));
			val |= (1 << 29);
		}
	}

	if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5906) {
		tw32(VCPU_STATUS, tr32(VCPU_STATUS) | VCPU_STATUS_DRV_RESET);
		tw32(GRC_VCPU_EXT_CTRL,
		     tr32(GRC_VCPU_EXT_CTRL) & ~GRC_VCPU_EXT_CTRL_HALT_CPU);
	}

	if (tp->tg3_flags2 & TG3_FLG2_5705_PLUS)
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

	if ((tp->tg3_flags2 & TG3_FLG2_PCI_EXPRESS) && tp->pcie_cap) {
		u16 val16;

		if (tp->pci_chip_rev_id == CHIPREV_ID_5750_A0) {
			int i;
			u32 cfg_val;

			/* Wait for link training to complete.  */
			for (i = 0; i < 5000; i++)
				udelay(100);

			pci_read_config_dword(tp->pdev, 0xc4, &cfg_val);
			pci_write_config_dword(tp->pdev, 0xc4,
					       cfg_val | (1 << 15));
		}

		/* Clear the "no snoop" and "relaxed ordering" bits. */
		pci_read_config_word(tp->pdev,
				     tp->pcie_cap + PCI_EXP_DEVCTL,
				     &val16);
		val16 &= ~(PCI_EXP_DEVCTL_RELAX_EN |
			   PCI_EXP_DEVCTL_NOSNOOP_EN);
		/*
		 * Older PCIe devices only support the 128 byte
		 * MPS setting.  Enforce the restriction.
		 */
		if (!(tp->tg3_flags & TG3_FLAG_CPMU_PRESENT) ||
		    (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5784))
			val16 &= ~PCI_EXP_DEVCTL_PAYLOAD;
		pci_write_config_word(tp->pdev,
				      tp->pcie_cap + PCI_EXP_DEVCTL,
				      val16);

		pcie_set_readrq(tp->pdev, 4096);

		/* Clear error status */
		pci_write_config_word(tp->pdev,
				      tp->pcie_cap + PCI_EXP_DEVSTA,
				      PCI_EXP_DEVSTA_CED |
				      PCI_EXP_DEVSTA_NFED |
				      PCI_EXP_DEVSTA_FED |
				      PCI_EXP_DEVSTA_URD);
	}

	tg3_restore_pci_state(tp);

	tp->tg3_flags &= ~TG3_FLAG_CHIP_RESETTING;

	val = 0;
	if (tp->tg3_flags2 & TG3_FLG2_5780_CLASS)
		val = tr32(MEMARB_MODE);
	tw32(MEMARB_MODE, val | MEMARB_MODE_ENABLE);

	if (tp->pci_chip_rev_id == CHIPREV_ID_5750_A3) {
		tg3_stop_fw(tp);
		tw32(0x5000, 0x400);
	}

	tw32(GRC_MODE, tp->grc_mode);

	if (tp->pci_chip_rev_id == CHIPREV_ID_5705_A0) {
		val = tr32(0xc4);

		tw32(0xc4, val | (1 << 15));
	}

	if ((tp->nic_sram_data_cfg & NIC_SRAM_DATA_CFG_MINI_PCI) != 0 &&
	    GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5705) {
		tp->pci_clock_ctrl |= CLOCK_CTRL_CLKRUN_OENABLE;
		if (tp->pci_chip_rev_id == CHIPREV_ID_5705_A0)
			tp->pci_clock_ctrl |= CLOCK_CTRL_FORCE_CLKRUN;
		tw32(TG3PCI_CLOCK_CTRL, tp->pci_clock_ctrl);
	}

	if (tp->tg3_flags2 & TG3_FLG2_PHY_SERDES) {
		tp->mac_mode = MAC_MODE_PORT_MODE_TBI;
		tw32_f(MAC_MODE, tp->mac_mode);
	} else if (tp->tg3_flags2 & TG3_FLG2_MII_SERDES) {
		tp->mac_mode = MAC_MODE_PORT_MODE_GMII;
		tw32_f(MAC_MODE, tp->mac_mode);
	} else if (tp->tg3_flags3 & TG3_FLG3_ENABLE_APE) {
		tp->mac_mode &= (MAC_MODE_APE_TX_EN | MAC_MODE_APE_RX_EN);
		if (tp->mac_mode & MAC_MODE_APE_TX_EN)
			tp->mac_mode |= MAC_MODE_TDE_ENABLE;
		tw32_f(MAC_MODE, tp->mac_mode);
	} else
		tw32_f(MAC_MODE, 0);
	udelay(40);

	tg3_ape_unlock(tp, TG3_APE_LOCK_GRC);

	err = tg3_poll_fw(tp);
	if (err)
		return err;

	tg3_mdio_start(tp);

	if ((tp->tg3_flags2 & TG3_FLG2_PCI_EXPRESS) &&
	    tp->pci_chip_rev_id != CHIPREV_ID_5750_A0 &&
	    GET_ASIC_REV(tp->pci_chip_rev_id) != ASIC_REV_5785 &&
	    GET_ASIC_REV(tp->pci_chip_rev_id) != ASIC_REV_5717) {
		val = tr32(0x7c00);

		tw32(0x7c00, val | (1 << 25));
	}

	/* Reprobe ASF enable state.  */
	tp->tg3_flags &= ~TG3_FLAG_ENABLE_ASF;
	tp->tg3_flags2 &= ~TG3_FLG2_ASF_NEW_HANDSHAKE;
	tg3_read_mem(tp, NIC_SRAM_DATA_SIG, &val);
	if (val == NIC_SRAM_DATA_SIG_MAGIC) {
		u32 nic_cfg;

		tg3_read_mem(tp, NIC_SRAM_DATA_CFG, &nic_cfg);
		if (nic_cfg & NIC_SRAM_DATA_CFG_ASF_ENABLE) {
			tp->tg3_flags |= TG3_FLAG_ENABLE_ASF;
			tp->last_event_jiffies = jiffies;
			if (tp->tg3_flags2 & TG3_FLG2_5750_PLUS)
				tp->tg3_flags2 |= TG3_FLG2_ASF_NEW_HANDSHAKE;
		}
	}

	return 0;
}

/* tp->lock is held. */
static void tg3_stop_fw(struct tg3 *tp)
{
	if ((tp->tg3_flags & TG3_FLAG_ENABLE_ASF) &&
	   !(tp->tg3_flags3 & TG3_FLG3_ENABLE_APE)) {
		/* Wait for RX cpu to ACK the previous event. */
		tg3_wait_for_event_ack(tp);

		tg3_write_mem(tp, NIC_SRAM_FW_CMD_MBOX, FWCMD_NICDRV_PAUSE_FW);

		tg3_generate_fw_event(tp);

		/* Wait for RX cpu to ACK this event. */
		tg3_wait_for_event_ack(tp);
	}
}

/* tp->lock is held. */
static int tg3_halt(struct tg3 *tp, int kind, int silent)
{
	int err;

	tg3_stop_fw(tp);

	tg3_write_sig_pre_reset(tp, kind);

	tg3_abort_hw(tp, silent);
	err = tg3_chip_reset(tp);

	__tg3_set_mac_addr(tp, 0);

	tg3_write_sig_legacy(tp, kind);
	tg3_write_sig_post_reset(tp, kind);

	if (err)
		return err;

	return 0;
}

#define RX_CPU_SCRATCH_BASE	0x30000
#define RX_CPU_SCRATCH_SIZE	0x04000
#define TX_CPU_SCRATCH_BASE	0x34000
#define TX_CPU_SCRATCH_SIZE	0x04000

/* tp->lock is held. */
static int tg3_halt_cpu(struct tg3 *tp, u32 offset)
{
	int i;

	BUG_ON(offset == TX_CPU_BASE &&
	    (tp->tg3_flags2 & TG3_FLG2_5705_PLUS));

	if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5906) {
		u32 val = tr32(GRC_VCPU_EXT_CTRL);

		tw32(GRC_VCPU_EXT_CTRL, val | GRC_VCPU_EXT_CTRL_HALT_CPU);
		return 0;
	}
	if (offset == RX_CPU_BASE) {
		for (i = 0; i < 10000; i++) {
			tw32(offset + CPU_STATE, 0xffffffff);
			tw32(offset + CPU_MODE,  CPU_MODE_HALT);
			if (tr32(offset + CPU_MODE) & CPU_MODE_HALT)
				break;
		}

		tw32(offset + CPU_STATE, 0xffffffff);
		tw32_f(offset + CPU_MODE,  CPU_MODE_HALT);
		udelay(10);
	} else {
		for (i = 0; i < 10000; i++) {
			tw32(offset + CPU_STATE, 0xffffffff);
			tw32(offset + CPU_MODE,  CPU_MODE_HALT);
			if (tr32(offset + CPU_MODE) & CPU_MODE_HALT)
				break;
		}
	}

	if (i >= 10000) {
		printk(KERN_ERR PFX "tg3_reset_cpu timed out for %s, "
		       "and %s CPU\n",
		       tp->dev->name,
		       (offset == RX_CPU_BASE ? "RX" : "TX"));
		return -ENODEV;
	}

	/* Clear firmware's nvram arbitration. */
	if (tp->tg3_flags & TG3_FLAG_NVRAM)
		tw32(NVRAM_SWARB, SWARB_REQ_CLR0);
	return 0;
}

struct fw_info {
	unsigned int fw_base;
	unsigned int fw_len;
	const __be32 *fw_data;
};

/* tp->lock is held. */
static int tg3_load_firmware_cpu(struct tg3 *tp, u32 cpu_base, u32 cpu_scratch_base,
				 int cpu_scratch_size, struct fw_info *info)
{
	int err, lock_err, i;
	void (*write_op)(struct tg3 *, u32, u32);

	if (cpu_base == TX_CPU_BASE &&
	    (tp->tg3_flags2 & TG3_FLG2_5705_PLUS)) {
		printk(KERN_ERR PFX "tg3_load_firmware_cpu: Trying to load "
		       "TX cpu firmware on %s which is 5705.\n",
		       tp->dev->name);
		return -EINVAL;
	}

	if (tp->tg3_flags2 & TG3_FLG2_5705_PLUS)
		write_op = tg3_write_mem;
	else
		write_op = tg3_write_indirect_reg32;

	/* It is possible that bootcode is still loading at this point.
	 * Get the nvram lock first before halting the cpu.
	 */
	lock_err = tg3_nvram_lock(tp);
	err = tg3_halt_cpu(tp, cpu_base);
	if (!lock_err)
		tg3_nvram_unlock(tp);
	if (err)
		goto out;

	for (i = 0; i < cpu_scratch_size; i += sizeof(u32))
		write_op(tp, cpu_scratch_base + i, 0);
	tw32(cpu_base + CPU_STATE, 0xffffffff);
	tw32(cpu_base + CPU_MODE, tr32(cpu_base+CPU_MODE)|CPU_MODE_HALT);
	for (i = 0; i < (info->fw_len / sizeof(u32)); i++)
		write_op(tp, (cpu_scratch_base +
			      (info->fw_base & 0xffff) +
			      (i * sizeof(u32))),
			      be32_to_cpu(info->fw_data[i]));

	err = 0;

out:
	return err;
}

/* tp->lock is held. */
static int tg3_load_5701_a0_firmware_fix(struct tg3 *tp)
{
	struct fw_info info;
	const __be32 *fw_data;
	int err, i;

	fw_data = (void *)tp->fw->data;

	/* Firmware blob starts with version numbers, followed by
	   start address and length. We are setting complete length.
	   length = end_address_of_bss - start_address_of_text.
	   Remainder is the blob to be loaded contiguously
	   from start address. */

	info.fw_base = be32_to_cpu(fw_data[1]);
	info.fw_len = tp->fw->size - 12;
	info.fw_data = &fw_data[3];

	err = tg3_load_firmware_cpu(tp, RX_CPU_BASE,
				    RX_CPU_SCRATCH_BASE, RX_CPU_SCRATCH_SIZE,
				    &info);
	if (err)
		return err;

	err = tg3_load_firmware_cpu(tp, TX_CPU_BASE,
				    TX_CPU_SCRATCH_BASE, TX_CPU_SCRATCH_SIZE,
				    &info);
	if (err)
		return err;

	/* Now startup only the RX cpu. */
	tw32(RX_CPU_BASE + CPU_STATE, 0xffffffff);
	tw32_f(RX_CPU_BASE + CPU_PC, info.fw_base);

	for (i = 0; i < 5; i++) {
		if (tr32(RX_CPU_BASE + CPU_PC) == info.fw_base)
			break;
		tw32(RX_CPU_BASE + CPU_STATE, 0xffffffff);
		tw32(RX_CPU_BASE + CPU_MODE,  CPU_MODE_HALT);
		tw32_f(RX_CPU_BASE + CPU_PC, info.fw_base);
		udelay(1000);
	}
	if (i >= 5) {
		printk(KERN_ERR PFX "tg3_load_firmware fails for %s "
		       "to set RX CPU PC, is %08x should be %08x\n",
		       tp->dev->name, tr32(RX_CPU_BASE + CPU_PC),
		       info.fw_base);
		return -ENODEV;
	}
	tw32(RX_CPU_BASE + CPU_STATE, 0xffffffff);
	tw32_f(RX_CPU_BASE + CPU_MODE,  0x00000000);

	return 0;
}

/* 5705 needs a special version of the TSO firmware.  */

/* tp->lock is held. */
static int tg3_load_tso_firmware(struct tg3 *tp)
{
	struct fw_info info;
	const __be32 *fw_data;
	unsigned long cpu_base, cpu_scratch_base, cpu_scratch_size;
	int err, i;

	if (tp->tg3_flags2 & TG3_FLG2_HW_TSO)
		return 0;

	fw_data = (void *)tp->fw->data;

	/* Firmware blob starts with version numbers, followed by
	   start address and length. We are setting complete length.
	   length = end_address_of_bss - start_address_of_text.
	   Remainder is the blob to be loaded contiguously
	   from start address. */

	info.fw_base = be32_to_cpu(fw_data[1]);
	cpu_scratch_size = tp->fw_len;
	info.fw_len = tp->fw->size - 12;
	info.fw_data = &fw_data[3];

	if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5705) {
		cpu_base = RX_CPU_BASE;
		cpu_scratch_base = NIC_SRAM_MBUF_POOL_BASE5705;
	} else {
		cpu_base = TX_CPU_BASE;
		cpu_scratch_base = TX_CPU_SCRATCH_BASE;
		cpu_scratch_size = TX_CPU_SCRATCH_SIZE;
	}

	err = tg3_load_firmware_cpu(tp, cpu_base,
				    cpu_scratch_base, cpu_scratch_size,
				    &info);
	if (err)
		return err;

	/* Now startup the cpu. */
	tw32(cpu_base + CPU_STATE, 0xffffffff);
	tw32_f(cpu_base + CPU_PC, info.fw_base);

	for (i = 0; i < 5; i++) {
		if (tr32(cpu_base + CPU_PC) == info.fw_base)
			break;
		tw32(cpu_base + CPU_STATE, 0xffffffff);
		tw32(cpu_base + CPU_MODE,  CPU_MODE_HALT);
		tw32_f(cpu_base + CPU_PC, info.fw_base);
		udelay(1000);
	}
	if (i >= 5) {
		printk(KERN_ERR PFX "tg3_load_tso_firmware fails for %s "
		       "to set CPU PC, is %08x should be %08x\n",
		       tp->dev->name, tr32(cpu_base + CPU_PC),
		       info.fw_base);
		return -ENODEV;
	}
	tw32(cpu_base + CPU_STATE, 0xffffffff);
	tw32_f(cpu_base + CPU_MODE,  0x00000000);
	return 0;
}


static int tg3_set_mac_addr(struct net_device *dev, void *p)
{
	struct tg3 *tp = netdev_priv(dev);
	struct sockaddr *addr = p;
	int err = 0, skip_mac_1 = 0;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EINVAL;

	memcpy(dev->dev_addr, addr->sa_data, dev->addr_len);

	if (!netif_running(dev))
		return 0;

	if (tp->tg3_flags & TG3_FLAG_ENABLE_ASF) {
		u32 addr0_high, addr0_low, addr1_high, addr1_low;

		addr0_high = tr32(MAC_ADDR_0_HIGH);
		addr0_low = tr32(MAC_ADDR_0_LOW);
		addr1_high = tr32(MAC_ADDR_1_HIGH);
		addr1_low = tr32(MAC_ADDR_1_LOW);

		/* Skip MAC addr 1 if ASF is using it. */
		if ((addr0_high != addr1_high || addr0_low != addr1_low) &&
		    !(addr1_high == 0 && addr1_low == 0))
			skip_mac_1 = 1;
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

	if (!(tp->tg3_flags2 & TG3_FLG2_5705_PLUS))
		tg3_write_mem(tp,
			      (bdinfo_addr + TG3_BDINFO_NIC_ADDR),
			      nic_addr);
}

static void __tg3_set_rx_mode(struct net_device *);
static void __tg3_set_coalesce(struct tg3 *tp, struct ethtool_coalesce *ec)
{
	int i;

	if (!(tp->tg3_flags2 & TG3_FLG2_USING_MSIX)) {
		tw32(HOSTCC_TXCOL_TICKS, ec->tx_coalesce_usecs);
		tw32(HOSTCC_TXMAX_FRAMES, ec->tx_max_coalesced_frames);
		tw32(HOSTCC_TXCOAL_MAXF_INT, ec->tx_max_coalesced_frames_irq);

		tw32(HOSTCC_RXCOL_TICKS, ec->rx_coalesce_usecs);
		tw32(HOSTCC_RXMAX_FRAMES, ec->rx_max_coalesced_frames);
		tw32(HOSTCC_RXCOAL_MAXF_INT, ec->rx_max_coalesced_frames_irq);
	} else {
		tw32(HOSTCC_TXCOL_TICKS, 0);
		tw32(HOSTCC_TXMAX_FRAMES, 0);
		tw32(HOSTCC_TXCOAL_MAXF_INT, 0);

		tw32(HOSTCC_RXCOL_TICKS, 0);
		tw32(HOSTCC_RXMAX_FRAMES, 0);
		tw32(HOSTCC_RXCOAL_MAXF_INT, 0);
	}

	if (!(tp->tg3_flags2 & TG3_FLG2_5705_PLUS)) {
		u32 val = ec->stats_block_coalesce_usecs;

		tw32(HOSTCC_RXCOAL_TICK_INT, ec->rx_coalesce_usecs_irq);
		tw32(HOSTCC_TXCOAL_TICK_INT, ec->tx_coalesce_usecs_irq);

		if (!netif_carrier_ok(tp->dev))
			val = 0;

		tw32(HOSTCC_STAT_COAL_TICKS, val);
	}

	for (i = 0; i < tp->irq_cnt - 1; i++) {
		u32 reg;

		reg = HOSTCC_RXCOL_TICKS_VEC1 + i * 0x18;
		tw32(reg, ec->rx_coalesce_usecs);
		reg = HOSTCC_TXCOL_TICKS_VEC1 + i * 0x18;
		tw32(reg, ec->tx_coalesce_usecs);
		reg = HOSTCC_RXMAX_FRAMES_VEC1 + i * 0x18;
		tw32(reg, ec->rx_max_coalesced_frames);
		reg = HOSTCC_TXMAX_FRAMES_VEC1 + i * 0x18;
		tw32(reg, ec->tx_max_coalesced_frames);
		reg = HOSTCC_RXCOAL_MAXF_INT_VEC1 + i * 0x18;
		tw32(reg, ec->rx_max_coalesced_frames_irq);
		reg = HOSTCC_TXCOAL_MAXF_INT_VEC1 + i * 0x18;
		tw32(reg, ec->tx_max_coalesced_frames_irq);
	}

	for (; i < tp->irq_max - 1; i++) {
		tw32(HOSTCC_RXCOL_TICKS_VEC1 + i * 0x18, 0);
		tw32(HOSTCC_TXCOL_TICKS_VEC1 + i * 0x18, 0);
		tw32(HOSTCC_RXMAX_FRAMES_VEC1 + i * 0x18, 0);
		tw32(HOSTCC_TXMAX_FRAMES_VEC1 + i * 0x18, 0);
		tw32(HOSTCC_RXCOAL_MAXF_INT_VEC1 + i * 0x18, 0);
		tw32(HOSTCC_TXCOAL_MAXF_INT_VEC1 + i * 0x18, 0);
	}
}

/* tp->lock is held. */
static void tg3_rings_reset(struct tg3 *tp)
{
	int i;
	u32 stblk, txrcb, rxrcb, limit;
	struct tg3_napi *tnapi = &tp->napi[0];

	/* Disable all transmit rings but the first. */
	if (!(tp->tg3_flags2 & TG3_FLG2_5705_PLUS))
		limit = NIC_SRAM_SEND_RCB + TG3_BDINFO_SIZE * 16;
	else
		limit = NIC_SRAM_SEND_RCB + TG3_BDINFO_SIZE;

	for (txrcb = NIC_SRAM_SEND_RCB + TG3_BDINFO_SIZE;
	     txrcb < limit; txrcb += TG3_BDINFO_SIZE)
		tg3_write_mem(tp, txrcb + TG3_BDINFO_MAXLEN_FLAGS,
			      BDINFO_FLAGS_DISABLED);


	/* Disable all receive return rings but the first. */
	if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5717)
		limit = NIC_SRAM_RCV_RET_RCB + TG3_BDINFO_SIZE * 17;
	else if (!(tp->tg3_flags2 & TG3_FLG2_5705_PLUS))
		limit = NIC_SRAM_RCV_RET_RCB + TG3_BDINFO_SIZE * 16;
	else if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5755)
		limit = NIC_SRAM_RCV_RET_RCB + TG3_BDINFO_SIZE * 4;
	else
		limit = NIC_SRAM_RCV_RET_RCB + TG3_BDINFO_SIZE;

	for (rxrcb = NIC_SRAM_RCV_RET_RCB + TG3_BDINFO_SIZE;
	     rxrcb < limit; rxrcb += TG3_BDINFO_SIZE)
		tg3_write_mem(tp, rxrcb + TG3_BDINFO_MAXLEN_FLAGS,
			      BDINFO_FLAGS_DISABLED);

	/* Disable interrupts */
	tw32_mailbox_f(tp->napi[0].int_mbox, 1);

	/* Zero mailbox registers. */
	if (tp->tg3_flags & TG3_FLAG_SUPPORT_MSIX) {
		for (i = 1; i < TG3_IRQ_MAX_VECS; i++) {
			tp->napi[i].tx_prod = 0;
			tp->napi[i].tx_cons = 0;
			tw32_mailbox(tp->napi[i].prodmbox, 0);
			tw32_rx_mbox(tp->napi[i].consmbox, 0);
			tw32_mailbox_f(tp->napi[i].int_mbox, 1);
		}
	} else {
		tp->napi[0].tx_prod = 0;
		tp->napi[0].tx_cons = 0;
		tw32_mailbox(tp->napi[0].prodmbox, 0);
		tw32_rx_mbox(tp->napi[0].consmbox, 0);
	}

	/* Make sure the NIC-based send BD rings are disabled. */
	if (!(tp->tg3_flags2 & TG3_FLG2_5705_PLUS)) {
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
			       (TG3_RX_RCB_RING_SIZE(tp) <<
				BDINFO_FLAGS_MAXLEN_SHIFT), 0);
		rxrcb += TG3_BDINFO_SIZE;
	}

	stblk = HOSTCC_STATBLCK_RING1;

	for (i = 1, tnapi++; i < tp->irq_cnt; i++, tnapi++) {
		u64 mapping = (u64)tnapi->status_mapping;
		tw32(stblk + TG3_64BIT_REG_HIGH, mapping >> 32);
		tw32(stblk + TG3_64BIT_REG_LOW, mapping & 0xffffffff);

		/* Clear status block in ram. */
		memset(tnapi->hw_status, 0, TG3_HW_STATUS_SIZE);

		tg3_set_bdinfo(tp, txrcb, tnapi->tx_desc_mapping,
			       (TG3_TX_RING_SIZE <<
				BDINFO_FLAGS_MAXLEN_SHIFT),
			       NIC_SRAM_TX_BUFFER_DESC);

		tg3_set_bdinfo(tp, rxrcb, tnapi->rx_rcb_mapping,
			       (TG3_RX_RCB_RING_SIZE(tp) <<
				BDINFO_FLAGS_MAXLEN_SHIFT), 0);

		stblk += 8;
		txrcb += TG3_BDINFO_SIZE;
		rxrcb += TG3_BDINFO_SIZE;
	}
}

/* tp->lock is held. */
static int tg3_reset_hw(struct tg3 *tp, int reset_phy)
{
	u32 val, rdmac_mode;
	int i, err, limit;
	struct tg3_rx_prodring_set *tpr = &tp->prodring[0];

	tg3_disable_ints(tp);

	tg3_stop_fw(tp);

	tg3_write_sig_pre_reset(tp, RESET_KIND_INIT);

	if (tp->tg3_flags & TG3_FLAG_INIT_COMPLETE) {
		tg3_abort_hw(tp, 1);
	}

	if (reset_phy &&
	    !(tp->tg3_flags3 & TG3_FLG3_USE_PHYLIB))
		tg3_phy_reset(tp);

	err = tg3_chip_reset(tp);
	if (err)
		return err;

	tg3_write_sig_legacy(tp, RESET_KIND_INIT);

	if (GET_CHIP_REV(tp->pci_chip_rev_id) == CHIPREV_5784_AX) {
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

	if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_57780) {
		val = tr32(PCIE_PWR_MGMT_THRESH) & ~PCIE_PWR_MGMT_L1_THRESH_MSK;
		val |= PCIE_PWR_MGMT_EXT_ASPM_TMR_EN |
		       PCIE_PWR_MGMT_L1_THRESH_4MS;
		tw32(PCIE_PWR_MGMT_THRESH, val);

		val = tr32(TG3_PCIE_EIDLE_DELAY) & ~TG3_PCIE_EIDLE_DELAY_MASK;
		tw32(TG3_PCIE_EIDLE_DELAY, val | TG3_PCIE_EIDLE_DELAY_13_CLKS);

		tw32(TG3_CORR_ERR_STAT, TG3_CORR_ERR_STAT_CLEAR);
	}

	if (tp->tg3_flags3 & TG3_FLG3_TOGGLE_10_100_L1PLLPD) {
		val = tr32(TG3_PCIE_LNKCTL);
		if (tp->tg3_flags3 & TG3_FLG3_CLKREQ_BUG)
			val |= TG3_PCIE_LNKCTL_L1_PLL_PD_DIS;
		else
			val &= ~TG3_PCIE_LNKCTL_L1_PLL_PD_DIS;
		tw32(TG3_PCIE_LNKCTL, val);
	}

	/* This works around an issue with Athlon chipsets on
	 * B3 tigon3 silicon.  This bit has no effect on any
	 * other revision.  But do not set this on PCI Express
	 * chips and don't even touch the clocks if the CPMU is present.
	 */
	if (!(tp->tg3_flags & TG3_FLAG_CPMU_PRESENT)) {
		if (!(tp->tg3_flags2 & TG3_FLG2_PCI_EXPRESS))
			tp->pci_clock_ctrl |= CLOCK_CTRL_DELAY_PCI_GRANT;
		tw32_f(TG3PCI_CLOCK_CTRL, tp->pci_clock_ctrl);
	}

	if (tp->pci_chip_rev_id == CHIPREV_ID_5704_A0 &&
	    (tp->tg3_flags & TG3_FLAG_PCIX_MODE)) {
		val = tr32(TG3PCI_PCISTATE);
		val |= PCISTATE_RETRY_SAME_DMA;
		tw32(TG3PCI_PCISTATE, val);
	}

	if (tp->tg3_flags3 & TG3_FLG3_ENABLE_APE) {
		/* Allow reads and writes to the
		 * APE register and memory space.
		 */
		val = tr32(TG3PCI_PCISTATE);
		val |= PCISTATE_ALLOW_APE_CTLSPC_WR |
		       PCISTATE_ALLOW_APE_SHMEM_WR;
		tw32(TG3PCI_PCISTATE, val);
	}

	if (GET_CHIP_REV(tp->pci_chip_rev_id) == CHIPREV_5704_BX) {
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

	if (GET_ASIC_REV(tp->pci_chip_rev_id) != ASIC_REV_5784 &&
	    GET_ASIC_REV(tp->pci_chip_rev_id) != ASIC_REV_5761 &&
	    GET_ASIC_REV(tp->pci_chip_rev_id) != ASIC_REV_5717) {
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

	tw32(GRC_MODE,
	     tp->grc_mode |
	     (GRC_MODE_IRQ_ON_MAC_ATTN | GRC_MODE_HOST_STACKUP));

	/* Setup the timer prescalar register.  Clock is always 66Mhz. */
	val = tr32(GRC_MISC_CFG);
	val &= ~0xff;
	val |= (65 << GRC_MISC_CFG_PRESCALAR_SHIFT);
	tw32(GRC_MISC_CFG, val);

	/* Initialize MBUF/DESC pool. */
	if (tp->tg3_flags2 & TG3_FLG2_5750_PLUS) {
		/* Do nothing.  */
	} else if (GET_ASIC_REV(tp->pci_chip_rev_id) != ASIC_REV_5705) {
		tw32(BUFMGR_MB_POOL_ADDR, NIC_SRAM_MBUF_POOL_BASE);
		if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5704)
			tw32(BUFMGR_MB_POOL_SIZE, NIC_SRAM_MBUF_POOL_SIZE64);
		else
			tw32(BUFMGR_MB_POOL_SIZE, NIC_SRAM_MBUF_POOL_SIZE96);
		tw32(BUFMGR_DMA_DESC_POOL_ADDR, NIC_SRAM_DMA_DESC_POOL_BASE);
		tw32(BUFMGR_DMA_DESC_POOL_SIZE, NIC_SRAM_DMA_DESC_POOL_SIZE);
	}
	else if (tp->tg3_flags2 & TG3_FLG2_TSO_CAPABLE) {
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

	tw32(BUFMGR_MODE, BUFMGR_MODE_ENABLE | BUFMGR_MODE_ATTN_ENABLE);
	for (i = 0; i < 2000; i++) {
		if (tr32(BUFMGR_MODE) & BUFMGR_MODE_ENABLE)
			break;
		udelay(10);
	}
	if (i >= 2000) {
		printk(KERN_ERR PFX "tg3_reset_hw cannot enable BUFMGR for %s.\n",
		       tp->dev->name);
		return -ENODEV;
	}

	/* Setup replenish threshold. */
	val = tp->rx_pending / 8;
	if (val == 0)
		val = 1;
	else if (val > tp->rx_std_max_post)
		val = tp->rx_std_max_post;
	else if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5906) {
		if (tp->pci_chip_rev_id == CHIPREV_ID_5906_A1)
			tw32(ISO_PKT_TX, (tr32(ISO_PKT_TX) & ~0x3) | 0x2);

		if (val > (TG3_RX_INTERNAL_RING_SZ_5906 / 2))
			val = TG3_RX_INTERNAL_RING_SZ_5906 / 2;
	}

	tw32(RCVBDI_STD_THRESH, val);

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
	tw32(RCVDBDI_STD_BD + TG3_BDINFO_NIC_ADDR,
	     NIC_SRAM_RX_BUFFER_DESC);

	/* Disable the mini ring */
	if (!(tp->tg3_flags2 & TG3_FLG2_5705_PLUS))
		tw32(RCVDBDI_MINI_BD + TG3_BDINFO_MAXLEN_FLAGS,
		     BDINFO_FLAGS_DISABLED);

	/* Program the jumbo buffer descriptor ring control
	 * blocks on those devices that have them.
	 */
	if ((tp->tg3_flags & TG3_FLAG_JUMBO_CAPABLE) &&
	    !(tp->tg3_flags2 & TG3_FLG2_5780_CLASS)) {
		/* Setup replenish threshold. */
		tw32(RCVBDI_JUMBO_THRESH, tp->rx_jumbo_pending / 8);

		if (tp->tg3_flags & TG3_FLAG_JUMBO_RING_ENABLE) {
			tw32(RCVDBDI_JUMBO_BD + TG3_BDINFO_HOST_ADDR + TG3_64BIT_REG_HIGH,
			     ((u64) tpr->rx_jmb_mapping >> 32));
			tw32(RCVDBDI_JUMBO_BD + TG3_BDINFO_HOST_ADDR + TG3_64BIT_REG_LOW,
			     ((u64) tpr->rx_jmb_mapping & 0xffffffff));
			tw32(RCVDBDI_JUMBO_BD + TG3_BDINFO_MAXLEN_FLAGS,
			     (RX_JUMBO_MAX_SIZE << BDINFO_FLAGS_MAXLEN_SHIFT) |
			     BDINFO_FLAGS_USE_EXT_RECV);
			tw32(RCVDBDI_JUMBO_BD + TG3_BDINFO_NIC_ADDR,
			     NIC_SRAM_RX_JUMBO_BUFFER_DESC);
		} else {
			tw32(RCVDBDI_JUMBO_BD + TG3_BDINFO_MAXLEN_FLAGS,
			     BDINFO_FLAGS_DISABLED);
		}

		if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5717)
			val = (RX_STD_MAX_SIZE_5705 << BDINFO_FLAGS_MAXLEN_SHIFT) |
			      (RX_STD_MAX_SIZE << 2);
		else
			val = RX_STD_MAX_SIZE << BDINFO_FLAGS_MAXLEN_SHIFT;
	} else
		val = RX_STD_MAX_SIZE_5705 << BDINFO_FLAGS_MAXLEN_SHIFT;

	tw32(RCVDBDI_STD_BD + TG3_BDINFO_MAXLEN_FLAGS, val);

	tpr->rx_std_ptr = tp->rx_pending;
	tw32_rx_mbox(MAILBOX_RCV_STD_PROD_IDX + TG3_64BIT_REG_LOW,
		     tpr->rx_std_ptr);

	tpr->rx_jmb_ptr = (tp->tg3_flags & TG3_FLAG_JUMBO_RING_ENABLE) ?
			  tp->rx_jumbo_pending : 0;
	tw32_rx_mbox(MAILBOX_RCV_JUMBO_PROD_IDX + TG3_64BIT_REG_LOW,
		     tpr->rx_jmb_ptr);

	if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5717) {
		tw32(STD_REPLENISH_LWM, 32);
		tw32(JMB_REPLENISH_LWM, 16);
	}

	tg3_rings_reset(tp);

	/* Initialize MAC address and backoff seed. */
	__tg3_set_mac_addr(tp, 0);

	/* MTU + ethernet header + FCS + optional VLAN tag */
	tw32(MAC_RX_MTU_SIZE,
	     tp->dev->mtu + ETH_HLEN + ETH_FCS_LEN + VLAN_HLEN);

	/* The slot time is changed by tg3_setup_phy if we
	 * run at gigabit with half duplex.
	 */
	tw32(MAC_TX_LENGTHS,
	     (2 << TX_LENGTHS_IPG_CRS_SHIFT) |
	     (6 << TX_LENGTHS_IPG_SHIFT) |
	     (32 << TX_LENGTHS_SLOT_TIME_SHIFT));

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

	if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5784 ||
	    GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5785 ||
	    GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_57780)
		rdmac_mode |= RDMAC_MODE_BD_SBD_CRPT_ENAB |
			      RDMAC_MODE_MBUF_RBD_CRPT_ENAB |
			      RDMAC_MODE_MBUF_SBD_CRPT_ENAB;

	/* If statement applies to 5705 and 5750 PCI devices only */
	if ((GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5705 &&
	     tp->pci_chip_rev_id != CHIPREV_ID_5705_A0) ||
	    (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5750)) {
		if (tp->tg3_flags2 & TG3_FLG2_TSO_CAPABLE &&
		    GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5705) {
			rdmac_mode |= RDMAC_MODE_FIFO_SIZE_128;
		} else if (!(tr32(TG3PCI_PCISTATE) & PCISTATE_BUS_SPEED_HIGH) &&
			   !(tp->tg3_flags2 & TG3_FLG2_IS_5788)) {
			rdmac_mode |= RDMAC_MODE_FIFO_LONG_BURST;
		}
	}

	if (tp->tg3_flags2 & TG3_FLG2_PCI_EXPRESS)
		rdmac_mode |= RDMAC_MODE_FIFO_LONG_BURST;

	if (tp->tg3_flags2 & TG3_FLG2_HW_TSO)
		rdmac_mode |= RDMAC_MODE_IPV4_LSO_EN;

	if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5785 ||
	    GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_57780)
		rdmac_mode |= RDMAC_MODE_IPV6_LSO_EN;

	/* Receive/send statistics. */
	if (tp->tg3_flags2 & TG3_FLG2_5750_PLUS) {
		val = tr32(RCVLPC_STATS_ENABLE);
		val &= ~RCVLPC_STATSENAB_DACK_FIX;
		tw32(RCVLPC_STATS_ENABLE, val);
	} else if ((rdmac_mode & RDMAC_MODE_FIFO_SIZE_128) &&
		   (tp->tg3_flags2 & TG3_FLG2_TSO_CAPABLE)) {
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

	if (!(tp->tg3_flags2 & TG3_FLG2_5705_PLUS)) {
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
	if (!(tp->tg3_flags2 & TG3_FLG2_5705_PLUS))
		tw32(RCVLSC_MODE, RCVLSC_MODE_ENABLE | RCVLSC_MODE_ATTN_ENABLE);

	if (tp->tg3_flags2 & TG3_FLG2_MII_SERDES) {
		tp->tg3_flags2 &= ~TG3_FLG2_PARALLEL_DETECT;
		/* reset to prevent losing 1st rx packet intermittently */
		tw32_f(MAC_RX_MODE, RX_MODE_RESET);
		udelay(10);
	}

	if (tp->tg3_flags3 & TG3_FLG3_ENABLE_APE)
		tp->mac_mode &= MAC_MODE_APE_TX_EN | MAC_MODE_APE_RX_EN;
	else
		tp->mac_mode = 0;
	tp->mac_mode |= MAC_MODE_TXSTAT_ENABLE | MAC_MODE_RXSTAT_ENABLE |
		MAC_MODE_TDE_ENABLE | MAC_MODE_RDE_ENABLE | MAC_MODE_FHDE_ENABLE;
	if (!(tp->tg3_flags2 & TG3_FLG2_5705_PLUS) &&
	    !(tp->tg3_flags2 & TG3_FLG2_PHY_SERDES) &&
	    GET_ASIC_REV(tp->pci_chip_rev_id) != ASIC_REV_5700)
		tp->mac_mode |= MAC_MODE_LINK_POLARITY;
	tw32_f(MAC_MODE, tp->mac_mode | MAC_MODE_RXSTAT_CLEAR | MAC_MODE_TXSTAT_CLEAR);
	udelay(40);

	/* tp->grc_local_ctrl is partially set up during tg3_get_invariants().
	 * If TG3_FLG2_IS_NIC is zero, we should read the
	 * register to preserve the GPIO settings for LOMs. The GPIOs,
	 * whether used as inputs or outputs, are set by boot code after
	 * reset.
	 */
	if (!(tp->tg3_flags2 & TG3_FLG2_IS_NIC)) {
		u32 gpio_mask;

		gpio_mask = GRC_LCLCTRL_GPIO_OE0 | GRC_LCLCTRL_GPIO_OE1 |
			    GRC_LCLCTRL_GPIO_OE2 | GRC_LCLCTRL_GPIO_OUTPUT0 |
			    GRC_LCLCTRL_GPIO_OUTPUT1 | GRC_LCLCTRL_GPIO_OUTPUT2;

		if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5752)
			gpio_mask |= GRC_LCLCTRL_GPIO_OE3 |
				     GRC_LCLCTRL_GPIO_OUTPUT3;

		if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5755)
			gpio_mask |= GRC_LCLCTRL_GPIO_UART_SEL;

		tp->grc_local_ctrl &= ~gpio_mask;
		tp->grc_local_ctrl |= tr32(GRC_LOCAL_CTRL) & gpio_mask;

		/* GPIO1 must be driven high for eeprom write protect */
		if (tp->tg3_flags & TG3_FLAG_EEPROM_WRITE_PROT)
			tp->grc_local_ctrl |= (GRC_LCLCTRL_GPIO_OE1 |
					       GRC_LCLCTRL_GPIO_OUTPUT1);
	}
	tw32_f(GRC_LOCAL_CTRL, tp->grc_local_ctrl);
	udelay(100);

	if (tp->tg3_flags2 & TG3_FLG2_USING_MSIX) {
		val = tr32(MSGINT_MODE);
		val |= MSGINT_MODE_MULTIVEC_EN | MSGINT_MODE_ENABLE;
		tw32(MSGINT_MODE, val);
	}

	if (!(tp->tg3_flags2 & TG3_FLG2_5705_PLUS)) {
		tw32_f(DMAC_MODE, DMAC_MODE_ENABLE);
		udelay(40);
	}

	val = (WDMAC_MODE_ENABLE | WDMAC_MODE_TGTABORT_ENAB |
	       WDMAC_MODE_MSTABORT_ENAB | WDMAC_MODE_PARITYERR_ENAB |
	       WDMAC_MODE_ADDROFLOW_ENAB | WDMAC_MODE_FIFOOFLOW_ENAB |
	       WDMAC_MODE_FIFOURUN_ENAB | WDMAC_MODE_FIFOOREAD_ENAB |
	       WDMAC_MODE_LNGREAD_ENAB);

	/* If statement applies to 5705 and 5750 PCI devices only */
	if ((GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5705 &&
	     tp->pci_chip_rev_id != CHIPREV_ID_5705_A0) ||
	    GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5750) {
		if ((tp->tg3_flags2 & TG3_FLG2_TSO_CAPABLE) &&
		    (tp->pci_chip_rev_id == CHIPREV_ID_5705_A1 ||
		     tp->pci_chip_rev_id == CHIPREV_ID_5705_A2)) {
			/* nothing */
		} else if (!(tr32(TG3PCI_PCISTATE) & PCISTATE_BUS_SPEED_HIGH) &&
			   !(tp->tg3_flags2 & TG3_FLG2_IS_5788) &&
			   !(tp->tg3_flags2 & TG3_FLG2_PCI_EXPRESS)) {
			val |= WDMAC_MODE_RX_ACCEL;
		}
	}

	/* Enable host coalescing bug fix */
	if (tp->tg3_flags3 & TG3_FLG3_5755_PLUS)
		val |= WDMAC_MODE_STATUS_TAG_FIX;

	tw32_f(WDMAC_MODE, val);
	udelay(40);

	if (tp->tg3_flags & TG3_FLAG_PCIX_MODE) {
		u16 pcix_cmd;

		pci_read_config_word(tp->pdev, tp->pcix_cap + PCI_X_CMD,
				     &pcix_cmd);
		if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5703) {
			pcix_cmd &= ~PCI_X_CMD_MAX_READ;
			pcix_cmd |= PCI_X_CMD_READ_2K;
		} else if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5704) {
			pcix_cmd &= ~(PCI_X_CMD_MAX_SPLIT | PCI_X_CMD_MAX_READ);
			pcix_cmd |= PCI_X_CMD_READ_2K;
		}
		pci_write_config_word(tp->pdev, tp->pcix_cap + PCI_X_CMD,
				      pcix_cmd);
	}

	tw32_f(RDMAC_MODE, rdmac_mode);
	udelay(40);

	tw32(RCVDCC_MODE, RCVDCC_MODE_ENABLE | RCVDCC_MODE_ATTN_ENABLE);
	if (!(tp->tg3_flags2 & TG3_FLG2_5705_PLUS))
		tw32(MBFREE_MODE, MBFREE_MODE_ENABLE);

	if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5761)
		tw32(SNDDATAC_MODE,
		     SNDDATAC_MODE_ENABLE | SNDDATAC_MODE_CDELAY);
	else
		tw32(SNDDATAC_MODE, SNDDATAC_MODE_ENABLE);

	tw32(SNDBDC_MODE, SNDBDC_MODE_ENABLE | SNDBDC_MODE_ATTN_ENABLE);
	tw32(RCVBDI_MODE, RCVBDI_MODE_ENABLE | RCVBDI_MODE_RCB_ATTN_ENAB);
	tw32(RCVDBDI_MODE, RCVDBDI_MODE_ENABLE | RCVDBDI_MODE_INV_RING_SZ);
	tw32(SNDDATAI_MODE, SNDDATAI_MODE_ENABLE);
	if (tp->tg3_flags2 & TG3_FLG2_HW_TSO)
		tw32(SNDDATAI_MODE, SNDDATAI_MODE_ENABLE | 0x8);
	val = SNDBDI_MODE_ENABLE | SNDBDI_MODE_ATTN_ENABLE;
	if (tp->tg3_flags2 & TG3_FLG2_USING_MSIX)
		val |= SNDBDI_MODE_MULTI_TXQ_EN;
	tw32(SNDBDI_MODE, val);
	tw32(SNDBDS_MODE, SNDBDS_MODE_ENABLE | SNDBDS_MODE_ATTN_ENABLE);

	if (tp->pci_chip_rev_id == CHIPREV_ID_5701_A0) {
		err = tg3_load_5701_a0_firmware_fix(tp);
		if (err)
			return err;
	}

	if (tp->tg3_flags2 & TG3_FLG2_TSO_CAPABLE) {
		err = tg3_load_tso_firmware(tp);
		if (err)
			return err;
	}

	tp->tx_mode = TX_MODE_ENABLE;
	tw32_f(MAC_TX_MODE, tp->tx_mode);
	udelay(100);

	if (tp->tg3_flags3 & TG3_FLG3_ENABLE_RSS) {
		u32 reg = MAC_RSS_INDIR_TBL_0;
		u8 *ent = (u8 *)&val;

		/* Setup the indirection table */
		for (i = 0; i < TG3_RSS_INDIR_TBL_SIZE; i++) {
			int idx = i % sizeof(val);

			ent[idx] = i % (tp->irq_cnt - 1);
			if (idx == sizeof(val) - 1) {
				tw32(reg, val);
				reg += 4;
			}
		}

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
	if (tp->tg3_flags3 & TG3_FLG3_5755_PLUS)
		tp->rx_mode |= RX_MODE_IPV6_CSUM_ENABLE;

	if (tp->tg3_flags3 & TG3_FLG3_ENABLE_RSS)
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
	if (tp->tg3_flags2 & TG3_FLG2_PHY_SERDES) {
		tw32_f(MAC_RX_MODE, RX_MODE_RESET);
		udelay(10);
	}
	tw32_f(MAC_RX_MODE, tp->rx_mode);
	udelay(10);

	if (tp->tg3_flags2 & TG3_FLG2_PHY_SERDES) {
		if ((GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5704) &&
			!(tp->tg3_flags2 & TG3_FLG2_SERDES_PREEMPHASIS)) {
			/* Set drive transmission level to 1.2V  */
			/* only if the signal pre-emphasis bit is not set  */
			val = tr32(MAC_SERDES_CFG);
			val &= 0xfffff000;
			val |= 0x880;
			tw32(MAC_SERDES_CFG, val);
		}
		if (tp->pci_chip_rev_id == CHIPREV_ID_5703_A1)
			tw32(MAC_SERDES_CFG, 0x616000);
	}

	/* Prevent chip from dropping frames when flow control
	 * is enabled.
	 */
	tw32_f(MAC_LOW_WMARK_MAX_RX_FRAME, 2);

	if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5704 &&
	    (tp->tg3_flags2 & TG3_FLG2_PHY_SERDES)) {
		/* Use hardware link auto-negotiation */
		tp->tg3_flags2 |= TG3_FLG2_HW_AUTONEG;
	}

	if ((tp->tg3_flags2 & TG3_FLG2_MII_SERDES) &&
	    (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5714)) {
		u32 tmp;

		tmp = tr32(SERDES_RX_CTRL);
		tw32(SERDES_RX_CTRL, tmp | SERDES_RX_SIG_DETECT);
		tp->grc_local_ctrl &= ~GRC_LCLCTRL_USE_EXT_SIG_DETECT;
		tp->grc_local_ctrl |= GRC_LCLCTRL_USE_SIG_DETECT;
		tw32(GRC_LOCAL_CTRL, tp->grc_local_ctrl);
	}

	if (!(tp->tg3_flags3 & TG3_FLG3_USE_PHYLIB)) {
		if (tp->link_config.phy_is_low_power) {
			tp->link_config.phy_is_low_power = 0;
			tp->link_config.speed = tp->link_config.orig_speed;
			tp->link_config.duplex = tp->link_config.orig_duplex;
			tp->link_config.autoneg = tp->link_config.orig_autoneg;
		}

		err = tg3_setup_phy(tp, 0);
		if (err)
			return err;

		if (!(tp->tg3_flags2 & TG3_FLG2_PHY_SERDES) &&
		    !(tp->tg3_flags3 & TG3_FLG3_PHY_IS_FET)) {
			u32 tmp;

			/* Clear CRC stats. */
			if (!tg3_readphy(tp, MII_TG3_TEST1, &tmp)) {
				tg3_writephy(tp, MII_TG3_TEST1,
					     tmp | MII_TG3_TEST1_CRC_EN);
				tg3_readphy(tp, 0x14, &tmp);
			}
		}
	}

	__tg3_set_rx_mode(tp->dev);

	/* Initialize receive rules. */
	tw32(MAC_RCV_RULE_0,  0xc2000000 & RCV_RULE_DISABLE_MASK);
	tw32(MAC_RCV_VALUE_0, 0xffffffff & RCV_RULE_DISABLE_MASK);
	tw32(MAC_RCV_RULE_1,  0x86000004 & RCV_RULE_DISABLE_MASK);
	tw32(MAC_RCV_VALUE_1, 0xffffffff & RCV_RULE_DISABLE_MASK);

	if ((tp->tg3_flags2 & TG3_FLG2_5705_PLUS) &&
	    !(tp->tg3_flags2 & TG3_FLG2_5780_CLASS))
		limit = 8;
	else
		limit = 16;
	if (tp->tg3_flags & TG3_FLAG_ENABLE_ASF)
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

	if (tp->tg3_flags3 & TG3_FLG3_ENABLE_APE)
		/* Write our heartbeat update interval to APE. */
		tg3_ape_write32(tp, TG3_APE_HOST_HEARTBEAT_INT_MS,
				APE_HOST_HEARTBEAT_INT_DISABLE);

	tg3_write_sig_post_reset(tp, RESET_KIND_INIT);

	return 0;
}

/* Called at device open time to get the chip ready for
 * packet processing.  Invoked with tp->lock held.
 */
static int tg3_init_hw(struct tg3 *tp, int reset_phy)
{
	tg3_switch_clocks(tp);

	tw32(TG3PCI_MEM_WIN_BASE_ADDR, 0);

	return tg3_reset_hw(tp, reset_phy);
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

	if (!netif_carrier_ok(tp->dev))
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
	TG3_STAT_ADD32(&sp->rx_discards, RCVLPC_IN_DISCARDS_CNT);
	TG3_STAT_ADD32(&sp->rx_errors, RCVLPC_IN_ERRORS_CNT);
}

static void tg3_timer(unsigned long __opaque)
{
	struct tg3 *tp = (struct tg3 *) __opaque;

	if (tp->irq_sync)
		goto restart_timer;

	spin_lock(&tp->lock);

	if (!(tp->tg3_flags & TG3_FLAG_TAGGED_STATUS)) {
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
			tp->tg3_flags2 |= TG3_FLG2_RESTART_TIMER;
			spin_unlock(&tp->lock);
			schedule_work(&tp->reset_task);
			return;
		}
	}

	/* This part only runs once per second. */
	if (!--tp->timer_counter) {
		if (tp->tg3_flags2 & TG3_FLG2_5705_PLUS)
			tg3_periodic_fetch_stats(tp);

		if (tp->tg3_flags & TG3_FLAG_USE_LINKCHG_REG) {
			u32 mac_stat;
			int phy_event;

			mac_stat = tr32(MAC_STATUS);

			phy_event = 0;
			if (tp->tg3_flags & TG3_FLAG_USE_MI_INTERRUPT) {
				if (mac_stat & MAC_STATUS_MI_INTERRUPT)
					phy_event = 1;
			} else if (mac_stat & MAC_STATUS_LNKSTATE_CHANGED)
				phy_event = 1;

			if (phy_event)
				tg3_setup_phy(tp, 0);
		} else if (tp->tg3_flags & TG3_FLAG_POLL_SERDES) {
			u32 mac_stat = tr32(MAC_STATUS);
			int need_setup = 0;

			if (netif_carrier_ok(tp->dev) &&
			    (mac_stat & MAC_STATUS_LNKSTATE_CHANGED)) {
				need_setup = 1;
			}
			if (! netif_carrier_ok(tp->dev) &&
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
				tg3_setup_phy(tp, 0);
			}
		} else if (tp->tg3_flags2 & TG3_FLG2_MII_SERDES)
			tg3_serdes_parallel_detect(tp);

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
		if ((tp->tg3_flags & TG3_FLAG_ENABLE_ASF) &&
		    !(tp->tg3_flags3 & TG3_FLG3_ENABLE_APE)) {
			tg3_wait_for_event_ack(tp);

			tg3_write_mem(tp, NIC_SRAM_FW_CMD_MBOX,
				      FWCMD_NICDRV_ALIVE3);
			tg3_write_mem(tp, NIC_SRAM_FW_CMD_LEN_MBOX, 4);
			/* 5 seconds timeout */
			tg3_write_mem(tp, NIC_SRAM_FW_CMD_DATA_MBOX, 5);

			tg3_generate_fw_event(tp);
		}
		tp->asf_counter = tp->asf_multiplier;
	}

	spin_unlock(&tp->lock);

restart_timer:
	tp->timer.expires = jiffies + tp->timer_offset;
	add_timer(&tp->timer);
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

	if (tp->tg3_flags2 & TG3_FLG2_USING_MSI_OR_MSIX) {
		fn = tg3_msi;
		if (tp->tg3_flags2 & TG3_FLG2_1SHOT_MSI)
			fn = tg3_msi_1shot;
		flags = IRQF_SAMPLE_RANDOM;
	} else {
		fn = tg3_interrupt;
		if (tp->tg3_flags & TG3_FLAG_TAGGED_STATUS)
			fn = tg3_interrupt_tagged;
		flags = IRQF_SHARED | IRQF_SAMPLE_RANDOM;
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
	if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5717 &&
	    (tp->tg3_flags2 & TG3_FLG2_USING_MSI)) {
		val = tr32(MSGINT_MODE) | MSGINT_MODE_ONE_SHOT_DISABLE;
		tw32(MSGINT_MODE, val);
	}

	err = request_irq(tnapi->irq_vec, tg3_test_isr,
			  IRQF_SHARED | IRQF_SAMPLE_RANDOM, dev->name, tnapi);
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

		msleep(10);
	}

	tg3_disable_ints(tp);

	free_irq(tnapi->irq_vec, tnapi);

	err = tg3_request_irq(tp, 0);

	if (err)
		return err;

	if (intr_ok) {
		/* Reenable MSI one shot mode. */
		if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5717 &&
		    (tp->tg3_flags2 & TG3_FLG2_USING_MSI)) {
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

	if (!(tp->tg3_flags2 & TG3_FLG2_USING_MSI))
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
	printk(KERN_WARNING PFX "%s: No interrupt was generated using MSI, "
	       "switching to INTx mode. Please report this failure to "
	       "the PCI maintainer and include system chipset information.\n",
		       tp->dev->name);

	free_irq(tp->napi[0].irq_vec, &tp->napi[0]);

	pci_disable_msi(tp->pdev);

	tp->tg3_flags2 &= ~TG3_FLG2_USING_MSI;

	err = tg3_request_irq(tp, 0);
	if (err)
		return err;

	/* Need to reset the chip because the MSI cycle may have terminated
	 * with Master Abort.
	 */
	tg3_full_lock(tp, 1);

	tg3_halt(tp, RESET_KIND_SHUTDOWN, 1);
	err = tg3_init_hw(tp, 1);

	tg3_full_unlock(tp);

	if (err)
		free_irq(tp->napi[0].irq_vec, &tp->napi[0]);

	return err;
}

static int tg3_request_firmware(struct tg3 *tp)
{
	const __be32 *fw_data;

	if (request_firmware(&tp->fw, tp->fw_needed, &tp->pdev->dev)) {
		printk(KERN_ERR "%s: Failed to load firmware \"%s\"\n",
		       tp->dev->name, tp->fw_needed);
		return -ENOENT;
	}

	fw_data = (void *)tp->fw->data;

	/* Firmware blob starts with version numbers, followed by
	 * start address and _full_ length including BSS sections
	 * (which must be longer than the actual data, of course
	 */

	tp->fw_len = be32_to_cpu(fw_data[2]);	/* includes bss */
	if (tp->fw_len < (tp->fw->size - 12)) {
		printk(KERN_ERR "%s: bogus length %d in \"%s\"\n",
		       tp->dev->name, tp->fw_len, tp->fw_needed);
		release_firmware(tp->fw);
		tp->fw = NULL;
		return -EINVAL;
	}

	/* We no longer need firmware; we have it. */
	tp->fw_needed = NULL;
	return 0;
}

static bool tg3_enable_msix(struct tg3 *tp)
{
	int i, rc, cpus = num_online_cpus();
	struct msix_entry msix_ent[tp->irq_max];

	if (cpus == 1)
		/* Just fallback to the simpler MSI mode. */
		return false;

	/*
	 * We want as many rx rings enabled as there are cpus.
	 * The first MSIX vector only deals with link interrupts, etc,
	 * so we add one to the number of vectors we are requesting.
	 */
	tp->irq_cnt = min_t(unsigned, cpus + 1, tp->irq_max);

	for (i = 0; i < tp->irq_max; i++) {
		msix_ent[i].entry  = i;
		msix_ent[i].vector = 0;
	}

	rc = pci_enable_msix(tp->pdev, msix_ent, tp->irq_cnt);
	if (rc != 0) {
		if (rc < TG3_RSS_MIN_NUM_MSIX_VECS)
			return false;
		if (pci_enable_msix(tp->pdev, msix_ent, rc))
			return false;
		printk(KERN_NOTICE
		       "%s: Requested %d MSI-X vectors, received %d\n",
		       tp->dev->name, tp->irq_cnt, rc);
		tp->irq_cnt = rc;
	}

	tp->tg3_flags3 |= TG3_FLG3_ENABLE_RSS;

	for (i = 0; i < tp->irq_max; i++)
		tp->napi[i].irq_vec = msix_ent[i].vector;

	tp->dev->real_num_tx_queues = tp->irq_cnt - 1;

	return true;
}

static void tg3_ints_init(struct tg3 *tp)
{
	if ((tp->tg3_flags & TG3_FLAG_SUPPORT_MSI_OR_MSIX) &&
	    !(tp->tg3_flags & TG3_FLAG_TAGGED_STATUS)) {
		/* All MSI supporting chips should support tagged
		 * status.  Assert that this is the case.
		 */
		printk(KERN_WARNING PFX "%s: MSI without TAGGED? "
		       "Not using MSI.\n", tp->dev->name);
		goto defcfg;
	}

	if ((tp->tg3_flags & TG3_FLAG_SUPPORT_MSIX) && tg3_enable_msix(tp))
		tp->tg3_flags2 |= TG3_FLG2_USING_MSIX;
	else if ((tp->tg3_flags & TG3_FLAG_SUPPORT_MSI) &&
		 pci_enable_msi(tp->pdev) == 0)
		tp->tg3_flags2 |= TG3_FLG2_USING_MSI;

	if (tp->tg3_flags2 & TG3_FLG2_USING_MSI_OR_MSIX) {
		u32 msi_mode = tr32(MSGINT_MODE);
		if (tp->tg3_flags2 & TG3_FLG2_USING_MSIX)
			msi_mode |= MSGINT_MODE_MULTIVEC_EN;
		tw32(MSGINT_MODE, msi_mode | MSGINT_MODE_ENABLE);
	}
defcfg:
	if (!(tp->tg3_flags2 & TG3_FLG2_USING_MSIX)) {
		tp->irq_cnt = 1;
		tp->napi[0].irq_vec = tp->pdev->irq;
		tp->dev->real_num_tx_queues = 1;
	}
}

static void tg3_ints_fini(struct tg3 *tp)
{
	if (tp->tg3_flags2 & TG3_FLG2_USING_MSIX)
		pci_disable_msix(tp->pdev);
	else if (tp->tg3_flags2 & TG3_FLG2_USING_MSI)
		pci_disable_msi(tp->pdev);
	tp->tg3_flags2 &= ~TG3_FLG2_USING_MSI_OR_MSIX;
	tp->tg3_flags3 &= ~TG3_FLG3_ENABLE_RSS;
}

static int tg3_open(struct net_device *dev)
{
	struct tg3 *tp = netdev_priv(dev);
	int i, err;

	if (tp->fw_needed) {
		err = tg3_request_firmware(tp);
		if (tp->pci_chip_rev_id == CHIPREV_ID_5701_A0) {
			if (err)
				return err;
		} else if (err) {
			printk(KERN_WARNING "%s: TSO capability disabled.\n",
			       tp->dev->name);
			tp->tg3_flags2 &= ~TG3_FLG2_TSO_CAPABLE;
		} else if (!(tp->tg3_flags2 & TG3_FLG2_TSO_CAPABLE)) {
			printk(KERN_NOTICE "%s: TSO capability restored.\n",
			       tp->dev->name);
			tp->tg3_flags2 |= TG3_FLG2_TSO_CAPABLE;
		}
	}

	netif_carrier_off(tp->dev);

	err = tg3_set_power_state(tp, PCI_D0);
	if (err)
		return err;

	tg3_full_lock(tp, 0);

	tg3_disable_ints(tp);
	tp->tg3_flags &= ~TG3_FLAG_INIT_COMPLETE;

	tg3_full_unlock(tp);

	/*
	 * Setup interrupts first so we know how
	 * many NAPI resources to allocate
	 */
	tg3_ints_init(tp);

	/* The placement of this call is tied
	 * to the setup and use of Host TX descriptors.
	 */
	err = tg3_alloc_consistent(tp);
	if (err)
		goto err_out1;

	tg3_napi_enable(tp);

	for (i = 0; i < tp->irq_cnt; i++) {
		struct tg3_napi *tnapi = &tp->napi[i];
		err = tg3_request_irq(tp, i);
		if (err) {
			for (i--; i >= 0; i--)
				free_irq(tnapi->irq_vec, tnapi);
			break;
		}
	}

	if (err)
		goto err_out2;

	tg3_full_lock(tp, 0);

	err = tg3_init_hw(tp, 1);
	if (err) {
		tg3_halt(tp, RESET_KIND_SHUTDOWN, 1);
		tg3_free_rings(tp);
	} else {
		if (tp->tg3_flags & TG3_FLAG_TAGGED_STATUS)
			tp->timer_offset = HZ;
		else
			tp->timer_offset = HZ / 10;

		BUG_ON(tp->timer_offset > HZ);
		tp->timer_counter = tp->timer_multiplier =
			(HZ / tp->timer_offset);
		tp->asf_counter = tp->asf_multiplier =
			((HZ / tp->timer_offset) * 2);

		init_timer(&tp->timer);
		tp->timer.expires = jiffies + tp->timer_offset;
		tp->timer.data = (unsigned long) tp;
		tp->timer.function = tg3_timer;
	}

	tg3_full_unlock(tp);

	if (err)
		goto err_out3;

	if (tp->tg3_flags2 & TG3_FLG2_USING_MSI) {
		err = tg3_test_msi(tp);

		if (err) {
			tg3_full_lock(tp, 0);
			tg3_halt(tp, RESET_KIND_SHUTDOWN, 1);
			tg3_free_rings(tp);
			tg3_full_unlock(tp);

			goto err_out2;
		}

		if (GET_ASIC_REV(tp->pci_chip_rev_id) != ASIC_REV_5717 &&
		    (tp->tg3_flags2 & TG3_FLG2_USING_MSI) &&
		    (tp->tg3_flags2 & TG3_FLG2_1SHOT_MSI)) {
			u32 val = tr32(PCIE_TRANSACTION_CFG);

			tw32(PCIE_TRANSACTION_CFG,
			     val | PCIE_TRANS_CFG_1SHOT_MSI);
		}
	}

	tg3_phy_start(tp);

	tg3_full_lock(tp, 0);

	add_timer(&tp->timer);
	tp->tg3_flags |= TG3_FLAG_INIT_COMPLETE;
	tg3_enable_ints(tp);

	tg3_full_unlock(tp);

	netif_tx_start_all_queues(dev);

	return 0;

err_out3:
	for (i = tp->irq_cnt - 1; i >= 0; i--) {
		struct tg3_napi *tnapi = &tp->napi[i];
		free_irq(tnapi->irq_vec, tnapi);
	}

err_out2:
	tg3_napi_disable(tp);
	tg3_free_consistent(tp);

err_out1:
	tg3_ints_fini(tp);
	return err;
}

#if 0
/*static*/ void tg3_dump_state(struct tg3 *tp)
{
	u32 val32, val32_2, val32_3, val32_4, val32_5;
	u16 val16;
	int i;
	struct tg3_hw_status *sblk = tp->napi[0]->hw_status;

	pci_read_config_word(tp->pdev, PCI_STATUS, &val16);
	pci_read_config_dword(tp->pdev, TG3PCI_PCISTATE, &val32);
	printk("DEBUG: PCI status [%04x] TG3PCI state[%08x]\n",
	       val16, val32);

	/* MAC block */
	printk("DEBUG: MAC_MODE[%08x] MAC_STATUS[%08x]\n",
	       tr32(MAC_MODE), tr32(MAC_STATUS));
	printk("       MAC_EVENT[%08x] MAC_LED_CTRL[%08x]\n",
	       tr32(MAC_EVENT), tr32(MAC_LED_CTRL));
	printk("DEBUG: MAC_TX_MODE[%08x] MAC_TX_STATUS[%08x]\n",
	       tr32(MAC_TX_MODE), tr32(MAC_TX_STATUS));
	printk("       MAC_RX_MODE[%08x] MAC_RX_STATUS[%08x]\n",
	       tr32(MAC_RX_MODE), tr32(MAC_RX_STATUS));

	/* Send data initiator control block */
	printk("DEBUG: SNDDATAI_MODE[%08x] SNDDATAI_STATUS[%08x]\n",
	       tr32(SNDDATAI_MODE), tr32(SNDDATAI_STATUS));
	printk("       SNDDATAI_STATSCTRL[%08x]\n",
	       tr32(SNDDATAI_STATSCTRL));

	/* Send data completion control block */
	printk("DEBUG: SNDDATAC_MODE[%08x]\n", tr32(SNDDATAC_MODE));

	/* Send BD ring selector block */
	printk("DEBUG: SNDBDS_MODE[%08x] SNDBDS_STATUS[%08x]\n",
	       tr32(SNDBDS_MODE), tr32(SNDBDS_STATUS));

	/* Send BD initiator control block */
	printk("DEBUG: SNDBDI_MODE[%08x] SNDBDI_STATUS[%08x]\n",
	       tr32(SNDBDI_MODE), tr32(SNDBDI_STATUS));

	/* Send BD completion control block */
	printk("DEBUG: SNDBDC_MODE[%08x]\n", tr32(SNDBDC_MODE));

	/* Receive list placement control block */
	printk("DEBUG: RCVLPC_MODE[%08x] RCVLPC_STATUS[%08x]\n",
	       tr32(RCVLPC_MODE), tr32(RCVLPC_STATUS));
	printk("       RCVLPC_STATSCTRL[%08x]\n",
	       tr32(RCVLPC_STATSCTRL));

	/* Receive data and receive BD initiator control block */
	printk("DEBUG: RCVDBDI_MODE[%08x] RCVDBDI_STATUS[%08x]\n",
	       tr32(RCVDBDI_MODE), tr32(RCVDBDI_STATUS));

	/* Receive data completion control block */
	printk("DEBUG: RCVDCC_MODE[%08x]\n",
	       tr32(RCVDCC_MODE));

	/* Receive BD initiator control block */
	printk("DEBUG: RCVBDI_MODE[%08x] RCVBDI_STATUS[%08x]\n",
	       tr32(RCVBDI_MODE), tr32(RCVBDI_STATUS));

	/* Receive BD completion control block */
	printk("DEBUG: RCVCC_MODE[%08x] RCVCC_STATUS[%08x]\n",
	       tr32(RCVCC_MODE), tr32(RCVCC_STATUS));

	/* Receive list selector control block */
	printk("DEBUG: RCVLSC_MODE[%08x] RCVLSC_STATUS[%08x]\n",
	       tr32(RCVLSC_MODE), tr32(RCVLSC_STATUS));

	/* Mbuf cluster free block */
	printk("DEBUG: MBFREE_MODE[%08x] MBFREE_STATUS[%08x]\n",
	       tr32(MBFREE_MODE), tr32(MBFREE_STATUS));

	/* Host coalescing control block */
	printk("DEBUG: HOSTCC_MODE[%08x] HOSTCC_STATUS[%08x]\n",
	       tr32(HOSTCC_MODE), tr32(HOSTCC_STATUS));
	printk("DEBUG: HOSTCC_STATS_BLK_HOST_ADDR[%08x%08x]\n",
	       tr32(HOSTCC_STATS_BLK_HOST_ADDR + TG3_64BIT_REG_HIGH),
	       tr32(HOSTCC_STATS_BLK_HOST_ADDR + TG3_64BIT_REG_LOW));
	printk("DEBUG: HOSTCC_STATUS_BLK_HOST_ADDR[%08x%08x]\n",
	       tr32(HOSTCC_STATUS_BLK_HOST_ADDR + TG3_64BIT_REG_HIGH),
	       tr32(HOSTCC_STATUS_BLK_HOST_ADDR + TG3_64BIT_REG_LOW));
	printk("DEBUG: HOSTCC_STATS_BLK_NIC_ADDR[%08x]\n",
	       tr32(HOSTCC_STATS_BLK_NIC_ADDR));
	printk("DEBUG: HOSTCC_STATUS_BLK_NIC_ADDR[%08x]\n",
	       tr32(HOSTCC_STATUS_BLK_NIC_ADDR));

	/* Memory arbiter control block */
	printk("DEBUG: MEMARB_MODE[%08x] MEMARB_STATUS[%08x]\n",
	       tr32(MEMARB_MODE), tr32(MEMARB_STATUS));

	/* Buffer manager control block */
	printk("DEBUG: BUFMGR_MODE[%08x] BUFMGR_STATUS[%08x]\n",
	       tr32(BUFMGR_MODE), tr32(BUFMGR_STATUS));
	printk("DEBUG: BUFMGR_MB_POOL_ADDR[%08x] BUFMGR_MB_POOL_SIZE[%08x]\n",
	       tr32(BUFMGR_MB_POOL_ADDR), tr32(BUFMGR_MB_POOL_SIZE));
	printk("DEBUG: BUFMGR_DMA_DESC_POOL_ADDR[%08x] "
	       "BUFMGR_DMA_DESC_POOL_SIZE[%08x]\n",
	       tr32(BUFMGR_DMA_DESC_POOL_ADDR),
	       tr32(BUFMGR_DMA_DESC_POOL_SIZE));

	/* Read DMA control block */
	printk("DEBUG: RDMAC_MODE[%08x] RDMAC_STATUS[%08x]\n",
	       tr32(RDMAC_MODE), tr32(RDMAC_STATUS));

	/* Write DMA control block */
	printk("DEBUG: WDMAC_MODE[%08x] WDMAC_STATUS[%08x]\n",
	       tr32(WDMAC_MODE), tr32(WDMAC_STATUS));

	/* DMA completion block */
	printk("DEBUG: DMAC_MODE[%08x]\n",
	       tr32(DMAC_MODE));

	/* GRC block */
	printk("DEBUG: GRC_MODE[%08x] GRC_MISC_CFG[%08x]\n",
	       tr32(GRC_MODE), tr32(GRC_MISC_CFG));
	printk("DEBUG: GRC_LOCAL_CTRL[%08x]\n",
	       tr32(GRC_LOCAL_CTRL));

	/* TG3_BDINFOs */
	printk("DEBUG: RCVDBDI_JUMBO_BD[%08x%08x:%08x:%08x]\n",
	       tr32(RCVDBDI_JUMBO_BD + 0x0),
	       tr32(RCVDBDI_JUMBO_BD + 0x4),
	       tr32(RCVDBDI_JUMBO_BD + 0x8),
	       tr32(RCVDBDI_JUMBO_BD + 0xc));
	printk("DEBUG: RCVDBDI_STD_BD[%08x%08x:%08x:%08x]\n",
	       tr32(RCVDBDI_STD_BD + 0x0),
	       tr32(RCVDBDI_STD_BD + 0x4),
	       tr32(RCVDBDI_STD_BD + 0x8),
	       tr32(RCVDBDI_STD_BD + 0xc));
	printk("DEBUG: RCVDBDI_MINI_BD[%08x%08x:%08x:%08x]\n",
	       tr32(RCVDBDI_MINI_BD + 0x0),
	       tr32(RCVDBDI_MINI_BD + 0x4),
	       tr32(RCVDBDI_MINI_BD + 0x8),
	       tr32(RCVDBDI_MINI_BD + 0xc));

	tg3_read_mem(tp, NIC_SRAM_SEND_RCB + 0x0, &val32);
	tg3_read_mem(tp, NIC_SRAM_SEND_RCB + 0x4, &val32_2);
	tg3_read_mem(tp, NIC_SRAM_SEND_RCB + 0x8, &val32_3);
	tg3_read_mem(tp, NIC_SRAM_SEND_RCB + 0xc, &val32_4);
	printk("DEBUG: SRAM_SEND_RCB_0[%08x%08x:%08x:%08x]\n",
	       val32, val32_2, val32_3, val32_4);

	tg3_read_mem(tp, NIC_SRAM_RCV_RET_RCB + 0x0, &val32);
	tg3_read_mem(tp, NIC_SRAM_RCV_RET_RCB + 0x4, &val32_2);
	tg3_read_mem(tp, NIC_SRAM_RCV_RET_RCB + 0x8, &val32_3);
	tg3_read_mem(tp, NIC_SRAM_RCV_RET_RCB + 0xc, &val32_4);
	printk("DEBUG: SRAM_RCV_RET_RCB_0[%08x%08x:%08x:%08x]\n",
	       val32, val32_2, val32_3, val32_4);

	tg3_read_mem(tp, NIC_SRAM_STATUS_BLK + 0x0, &val32);
	tg3_read_mem(tp, NIC_SRAM_STATUS_BLK + 0x4, &val32_2);
	tg3_read_mem(tp, NIC_SRAM_STATUS_BLK + 0x8, &val32_3);
	tg3_read_mem(tp, NIC_SRAM_STATUS_BLK + 0xc, &val32_4);
	tg3_read_mem(tp, NIC_SRAM_STATUS_BLK + 0x10, &val32_5);
	printk("DEBUG: SRAM_STATUS_BLK[%08x:%08x:%08x:%08x:%08x]\n",
	       val32, val32_2, val32_3, val32_4, val32_5);

	/* SW status block */
	printk(KERN_DEBUG
	 "Host status block [%08x:%08x:(%04x:%04x:%04x):(%04x:%04x)]\n",
	       sblk->status,
	       sblk->status_tag,
	       sblk->rx_jumbo_consumer,
	       sblk->rx_consumer,
	       sblk->rx_mini_consumer,
	       sblk->idx[0].rx_producer,
	       sblk->idx[0].tx_consumer);

	/* SW statistics block */
	printk("DEBUG: Host statistics block [%08x:%08x:%08x:%08x]\n",
	       ((u32 *)tp->hw_stats)[0],
	       ((u32 *)tp->hw_stats)[1],
	       ((u32 *)tp->hw_stats)[2],
	       ((u32 *)tp->hw_stats)[3]);

	/* Mailboxes */
	printk("DEBUG: SNDHOST_PROD[%08x%08x] SNDNIC_PROD[%08x%08x]\n",
	       tr32_mailbox(MAILBOX_SNDHOST_PROD_IDX_0 + 0x0),
	       tr32_mailbox(MAILBOX_SNDHOST_PROD_IDX_0 + 0x4),
	       tr32_mailbox(MAILBOX_SNDNIC_PROD_IDX_0 + 0x0),
	       tr32_mailbox(MAILBOX_SNDNIC_PROD_IDX_0 + 0x4));

	/* NIC side send descriptors. */
	for (i = 0; i < 6; i++) {
		unsigned long txd;

		txd = tp->regs + NIC_SRAM_WIN_BASE + NIC_SRAM_TX_BUFFER_DESC
			+ (i * sizeof(struct tg3_tx_buffer_desc));
		printk("DEBUG: NIC TXD(%d)[%08x:%08x:%08x:%08x]\n",
		       i,
		       readl(txd + 0x0), readl(txd + 0x4),
		       readl(txd + 0x8), readl(txd + 0xc));
	}

	/* NIC side RX descriptors. */
	for (i = 0; i < 6; i++) {
		unsigned long rxd;

		rxd = tp->regs + NIC_SRAM_WIN_BASE + NIC_SRAM_RX_BUFFER_DESC
			+ (i * sizeof(struct tg3_rx_buffer_desc));
		printk("DEBUG: NIC RXD_STD(%d)[0][%08x:%08x:%08x:%08x]\n",
		       i,
		       readl(rxd + 0x0), readl(rxd + 0x4),
		       readl(rxd + 0x8), readl(rxd + 0xc));
		rxd += (4 * sizeof(u32));
		printk("DEBUG: NIC RXD_STD(%d)[1][%08x:%08x:%08x:%08x]\n",
		       i,
		       readl(rxd + 0x0), readl(rxd + 0x4),
		       readl(rxd + 0x8), readl(rxd + 0xc));
	}

	for (i = 0; i < 6; i++) {
		unsigned long rxd;

		rxd = tp->regs + NIC_SRAM_WIN_BASE + NIC_SRAM_RX_JUMBO_BUFFER_DESC
			+ (i * sizeof(struct tg3_rx_buffer_desc));
		printk("DEBUG: NIC RXD_JUMBO(%d)[0][%08x:%08x:%08x:%08x]\n",
		       i,
		       readl(rxd + 0x0), readl(rxd + 0x4),
		       readl(rxd + 0x8), readl(rxd + 0xc));
		rxd += (4 * sizeof(u32));
		printk("DEBUG: NIC RXD_JUMBO(%d)[1][%08x:%08x:%08x:%08x]\n",
		       i,
		       readl(rxd + 0x0), readl(rxd + 0x4),
		       readl(rxd + 0x8), readl(rxd + 0xc));
	}
}
#endif

static struct net_device_stats *tg3_get_stats(struct net_device *);
static struct tg3_ethtool_stats *tg3_get_estats(struct tg3 *);

static int tg3_close(struct net_device *dev)
{
	int i;
	struct tg3 *tp = netdev_priv(dev);

	tg3_napi_disable(tp);
	cancel_work_sync(&tp->reset_task);

	netif_tx_stop_all_queues(dev);

	del_timer_sync(&tp->timer);

	tg3_phy_stop(tp);

	tg3_full_lock(tp, 1);
#if 0
	tg3_dump_state(tp);
#endif

	tg3_disable_ints(tp);

	tg3_halt(tp, RESET_KIND_SHUTDOWN, 1);
	tg3_free_rings(tp);
	tp->tg3_flags &= ~TG3_FLAG_INIT_COMPLETE;

	tg3_full_unlock(tp);

	for (i = tp->irq_cnt - 1; i >= 0; i--) {
		struct tg3_napi *tnapi = &tp->napi[i];
		free_irq(tnapi->irq_vec, tnapi);
	}

	tg3_ints_fini(tp);

	memcpy(&tp->net_stats_prev, tg3_get_stats(tp->dev),
	       sizeof(tp->net_stats_prev));
	memcpy(&tp->estats_prev, tg3_get_estats(tp),
	       sizeof(tp->estats_prev));

	tg3_free_consistent(tp);

	tg3_set_power_state(tp, PCI_D3hot);

	netif_carrier_off(tp->dev);

	return 0;
}

static inline unsigned long get_stat64(tg3_stat64_t *val)
{
	unsigned long ret;

#if (BITS_PER_LONG == 32)
	ret = val->low;
#else
	ret = ((u64)val->high << 32) | ((u64)val->low);
#endif
	return ret;
}

static inline u64 get_estat64(tg3_stat64_t *val)
{
       return ((u64)val->high << 32) | ((u64)val->low);
}

static unsigned long calc_crc_errors(struct tg3 *tp)
{
	struct tg3_hw_stats *hw_stats = tp->hw_stats;

	if (!(tp->tg3_flags2 & TG3_FLG2_PHY_SERDES) &&
	    (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5700 ||
	     GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5701)) {
		u32 val;

		spin_lock_bh(&tp->lock);
		if (!tg3_readphy(tp, MII_TG3_TEST1, &val)) {
			tg3_writephy(tp, MII_TG3_TEST1,
				     val | MII_TG3_TEST1_CRC_EN);
			tg3_readphy(tp, 0x14, &val);
		} else
			val = 0;
		spin_unlock_bh(&tp->lock);

		tp->phy_crc_errors += val;

		return tp->phy_crc_errors;
	}

	return get_stat64(&hw_stats->rx_fcs_errors);
}

#define ESTAT_ADD(member) \
	estats->member =	old_estats->member + \
				get_estat64(&hw_stats->member)

static struct tg3_ethtool_stats *tg3_get_estats(struct tg3 *tp)
{
	struct tg3_ethtool_stats *estats = &tp->estats;
	struct tg3_ethtool_stats *old_estats = &tp->estats_prev;
	struct tg3_hw_stats *hw_stats = tp->hw_stats;

	if (!hw_stats)
		return old_estats;

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

	return estats;
}

static struct net_device_stats *tg3_get_stats(struct net_device *dev)
{
	struct tg3 *tp = netdev_priv(dev);
	struct net_device_stats *stats = &tp->net_stats;
	struct net_device_stats *old_stats = &tp->net_stats_prev;
	struct tg3_hw_stats *hw_stats = tp->hw_stats;

	if (!hw_stats)
		return old_stats;

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
		calc_crc_errors(tp);

	stats->rx_missed_errors = old_stats->rx_missed_errors +
		get_stat64(&hw_stats->rx_discards);

	return stats;
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

			if (tmp) {
				reg ^= 0xedb88320;
			}
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

	/* When ASF is in use, we always keep the RX_MODE_KEEP_VLAN_TAG
	 * flag clear.
	 */
#if TG3_VLAN_TAG_USED
	if (!tp->vlgrp &&
	    !(tp->tg3_flags & TG3_FLAG_ENABLE_ASF))
		rx_mode |= RX_MODE_KEEP_VLAN_TAG;
#else
	/* By definition, VLAN is disabled always in this
	 * case.
	 */
	if (!(tp->tg3_flags & TG3_FLAG_ENABLE_ASF))
		rx_mode |= RX_MODE_KEEP_VLAN_TAG;
#endif

	if (dev->flags & IFF_PROMISC) {
		/* Promiscuous mode. */
		rx_mode |= RX_MODE_PROMISC;
	} else if (dev->flags & IFF_ALLMULTI) {
		/* Accept all multicast. */
		tg3_set_multi (tp, 1);
	} else if (dev->mc_count < 1) {
		/* Reject all multicast. */
		tg3_set_multi (tp, 0);
	} else {
		/* Accept one or more multicast(s). */
		struct dev_mc_list *mclist;
		unsigned int i;
		u32 mc_filter[4] = { 0, };
		u32 regidx;
		u32 bit;
		u32 crc;

		for (i = 0, mclist = dev->mc_list; mclist && i < dev->mc_count;
		     i++, mclist = mclist->next) {

			crc = calc_crc (mclist->dmi_addr, ETH_ALEN);
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

static void tg3_set_rx_mode(struct net_device *dev)
{
	struct tg3 *tp = netdev_priv(dev);

	if (!netif_running(dev))
		return;

	tg3_full_lock(tp, 0);
	__tg3_set_rx_mode(dev);
	tg3_full_unlock(tp);
}

#define TG3_REGDUMP_LEN		(32 * 1024)

static int tg3_get_regs_len(struct net_device *dev)
{
	return TG3_REGDUMP_LEN;
}

static void tg3_get_regs(struct net_device *dev,
		struct ethtool_regs *regs, void *_p)
{
	u32 *p = _p;
	struct tg3 *tp = netdev_priv(dev);
	u8 *orig_p = _p;
	int i;

	regs->version = 0;

	memset(p, 0, TG3_REGDUMP_LEN);

	if (tp->link_config.phy_is_low_power)
		return;

	tg3_full_lock(tp, 0);

#define __GET_REG32(reg)	(*(p)++ = tr32(reg))
#define GET_REG32_LOOP(base,len)		\
do {	p = (u32 *)(orig_p + (base));		\
	for (i = 0; i < len; i += 4)		\
		__GET_REG32((base) + i);	\
} while (0)
#define GET_REG32_1(reg)			\
do {	p = (u32 *)(orig_p + (reg));		\
	__GET_REG32((reg));			\
} while (0)

	GET_REG32_LOOP(TG3PCI_VENDOR, 0xb0);
	GET_REG32_LOOP(MAILBOX_INTERRUPT_0, 0x200);
	GET_REG32_LOOP(MAC_MODE, 0x4f0);
	GET_REG32_LOOP(SNDDATAI_MODE, 0xe0);
	GET_REG32_1(SNDDATAC_MODE);
	GET_REG32_LOOP(SNDBDS_MODE, 0x80);
	GET_REG32_LOOP(SNDBDI_MODE, 0x48);
	GET_REG32_1(SNDBDC_MODE);
	GET_REG32_LOOP(RCVLPC_MODE, 0x20);
	GET_REG32_LOOP(RCVLPC_SELLST_BASE, 0x15c);
	GET_REG32_LOOP(RCVDBDI_MODE, 0x0c);
	GET_REG32_LOOP(RCVDBDI_JUMBO_BD, 0x3c);
	GET_REG32_LOOP(RCVDBDI_BD_PROD_IDX_0, 0x44);
	GET_REG32_1(RCVDCC_MODE);
	GET_REG32_LOOP(RCVBDI_MODE, 0x20);
	GET_REG32_LOOP(RCVCC_MODE, 0x14);
	GET_REG32_LOOP(RCVLSC_MODE, 0x08);
	GET_REG32_1(MBFREE_MODE);
	GET_REG32_LOOP(HOSTCC_MODE, 0x100);
	GET_REG32_LOOP(MEMARB_MODE, 0x10);
	GET_REG32_LOOP(BUFMGR_MODE, 0x58);
	GET_REG32_LOOP(RDMAC_MODE, 0x08);
	GET_REG32_LOOP(WDMAC_MODE, 0x08);
	GET_REG32_1(RX_CPU_MODE);
	GET_REG32_1(RX_CPU_STATE);
	GET_REG32_1(RX_CPU_PGMCTR);
	GET_REG32_1(RX_CPU_HWBKPT);
	GET_REG32_1(TX_CPU_MODE);
	GET_REG32_1(TX_CPU_STATE);
	GET_REG32_1(TX_CPU_PGMCTR);
	GET_REG32_LOOP(GRCMBOX_INTERRUPT_0, 0x110);
	GET_REG32_LOOP(FTQ_RESET, 0x120);
	GET_REG32_LOOP(MSGINT_MODE, 0x0c);
	GET_REG32_1(DMAC_MODE);
	GET_REG32_LOOP(GRC_MODE, 0x4c);
	if (tp->tg3_flags & TG3_FLAG_NVRAM)
		GET_REG32_LOOP(NVRAM_CMD, 0x24);

#undef __GET_REG32
#undef GET_REG32_LOOP
#undef GET_REG32_1

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

	if (tp->tg3_flags3 & TG3_FLG3_NO_NVRAM)
		return -EINVAL;

	if (tp->link_config.phy_is_low_power)
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
		memcpy(data, ((char*)&val) + b_offset, b_count);
		len -= b_count;
		offset += b_count;
	        eeprom->len += b_count;
	}

	/* read bytes upto the last 4 byte boundary */
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

static int tg3_nvram_write_block(struct tg3 *tp, u32 offset, u32 len, u8 *buf);

static int tg3_set_eeprom(struct net_device *dev, struct ethtool_eeprom *eeprom, u8 *data)
{
	struct tg3 *tp = netdev_priv(dev);
	int ret;
	u32 offset, len, b_offset, odd_len;
	u8 *buf;
	__be32 start, end;

	if (tp->link_config.phy_is_low_power)
		return -EAGAIN;

	if ((tp->tg3_flags3 & TG3_FLG3_NO_NVRAM) ||
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

	if (tp->tg3_flags3 & TG3_FLG3_USE_PHYLIB) {
		if (!(tp->tg3_flags3 & TG3_FLG3_PHY_CONNECTED))
			return -EAGAIN;
		return phy_ethtool_gset(tp->mdio_bus->phy_map[PHY_ADDR], cmd);
	}

	cmd->supported = (SUPPORTED_Autoneg);

	if (!(tp->tg3_flags & TG3_FLAG_10_100_ONLY))
		cmd->supported |= (SUPPORTED_1000baseT_Half |
				   SUPPORTED_1000baseT_Full);

	if (!(tp->tg3_flags2 & TG3_FLG2_ANY_SERDES)) {
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
	if (netif_running(dev)) {
		cmd->speed = tp->link_config.active_speed;
		cmd->duplex = tp->link_config.active_duplex;
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

	if (tp->tg3_flags3 & TG3_FLG3_USE_PHYLIB) {
		if (!(tp->tg3_flags3 & TG3_FLG3_PHY_CONNECTED))
			return -EAGAIN;
		return phy_ethtool_sset(tp->mdio_bus->phy_map[PHY_ADDR], cmd);
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

		if (!(tp->tg3_flags2 & TG3_FLAG_10_100_ONLY))
			mask |= ADVERTISED_1000baseT_Half |
				ADVERTISED_1000baseT_Full;

		if (!(tp->tg3_flags2 & TG3_FLG2_ANY_SERDES))
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
		if (tp->tg3_flags2 & TG3_FLG2_ANY_SERDES) {
			if (cmd->speed != SPEED_1000)
				return -EINVAL;

			if (cmd->duplex != DUPLEX_FULL)
				return -EINVAL;
		} else {
			if (cmd->speed != SPEED_100 &&
			    cmd->speed != SPEED_10)
				return -EINVAL;
		}
	}

	tg3_full_lock(tp, 0);

	tp->link_config.autoneg = cmd->autoneg;
	if (cmd->autoneg == AUTONEG_ENABLE) {
		tp->link_config.advertising = (cmd->advertising |
					      ADVERTISED_Autoneg);
		tp->link_config.speed = SPEED_INVALID;
		tp->link_config.duplex = DUPLEX_INVALID;
	} else {
		tp->link_config.advertising = 0;
		tp->link_config.speed = cmd->speed;
		tp->link_config.duplex = cmd->duplex;
	}

	tp->link_config.orig_speed = tp->link_config.speed;
	tp->link_config.orig_duplex = tp->link_config.duplex;
	tp->link_config.orig_autoneg = tp->link_config.autoneg;

	if (netif_running(dev))
		tg3_setup_phy(tp, 1);

	tg3_full_unlock(tp);

	return 0;
}

static void tg3_get_drvinfo(struct net_device *dev, struct ethtool_drvinfo *info)
{
	struct tg3 *tp = netdev_priv(dev);

	strcpy(info->driver, DRV_MODULE_NAME);
	strcpy(info->version, DRV_MODULE_VERSION);
	strcpy(info->fw_version, tp->fw_ver);
	strcpy(info->bus_info, pci_name(tp->pdev));
}

static void tg3_get_wol(struct net_device *dev, struct ethtool_wolinfo *wol)
{
	struct tg3 *tp = netdev_priv(dev);

	if ((tp->tg3_flags & TG3_FLAG_WOL_CAP) &&
	    device_can_wakeup(&tp->pdev->dev))
		wol->supported = WAKE_MAGIC;
	else
		wol->supported = 0;
	wol->wolopts = 0;
	if ((tp->tg3_flags & TG3_FLAG_WOL_ENABLE) &&
	    device_can_wakeup(&tp->pdev->dev))
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
	    !((tp->tg3_flags & TG3_FLAG_WOL_CAP) && device_can_wakeup(dp)))
		return -EINVAL;

	spin_lock_bh(&tp->lock);
	if (wol->wolopts & WAKE_MAGIC) {
		tp->tg3_flags |= TG3_FLAG_WOL_ENABLE;
		device_set_wakeup_enable(dp, true);
	} else {
		tp->tg3_flags &= ~TG3_FLAG_WOL_ENABLE;
		device_set_wakeup_enable(dp, false);
	}
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

static int tg3_set_tso(struct net_device *dev, u32 value)
{
	struct tg3 *tp = netdev_priv(dev);

	if (!(tp->tg3_flags2 & TG3_FLG2_TSO_CAPABLE)) {
		if (value)
			return -EINVAL;
		return 0;
	}
	if ((dev->features & NETIF_F_IPV6_CSUM) &&
	    (tp->tg3_flags2 & TG3_FLG2_HW_TSO_2)) {
		if (value) {
			dev->features |= NETIF_F_TSO6;
			if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5761 ||
			    (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5784 &&
			     GET_CHIP_REV(tp->pci_chip_rev_id) != CHIPREV_5784_AX) ||
			    GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5785 ||
			    GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_57780 ||
			    GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5717)
				dev->features |= NETIF_F_TSO_ECN;
		} else
			dev->features &= ~(NETIF_F_TSO6 | NETIF_F_TSO_ECN);
	}
	return ethtool_op_set_tso(dev, value);
}

static int tg3_nway_reset(struct net_device *dev)
{
	struct tg3 *tp = netdev_priv(dev);
	int r;

	if (!netif_running(dev))
		return -EAGAIN;

	if (tp->tg3_flags2 & TG3_FLG2_PHY_SERDES)
		return -EINVAL;

	if (tp->tg3_flags3 & TG3_FLG3_USE_PHYLIB) {
		if (!(tp->tg3_flags3 & TG3_FLG3_PHY_CONNECTED))
			return -EAGAIN;
		r = phy_start_aneg(tp->mdio_bus->phy_map[PHY_ADDR]);
	} else {
		u32 bmcr;

		spin_lock_bh(&tp->lock);
		r = -EINVAL;
		tg3_readphy(tp, MII_BMCR, &bmcr);
		if (!tg3_readphy(tp, MII_BMCR, &bmcr) &&
		    ((bmcr & BMCR_ANENABLE) ||
		     (tp->tg3_flags2 & TG3_FLG2_PARALLEL_DETECT))) {
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

	ering->rx_max_pending = TG3_RX_RING_SIZE - 1;
	ering->rx_mini_max_pending = 0;
	if (tp->tg3_flags & TG3_FLAG_JUMBO_RING_ENABLE)
		ering->rx_jumbo_max_pending = TG3_RX_JUMBO_RING_SIZE - 1;
	else
		ering->rx_jumbo_max_pending = 0;

	ering->tx_max_pending = TG3_TX_RING_SIZE - 1;

	ering->rx_pending = tp->rx_pending;
	ering->rx_mini_pending = 0;
	if (tp->tg3_flags & TG3_FLAG_JUMBO_RING_ENABLE)
		ering->rx_jumbo_pending = tp->rx_jumbo_pending;
	else
		ering->rx_jumbo_pending = 0;

	ering->tx_pending = tp->napi[0].tx_pending;
}

static int tg3_set_ringparam(struct net_device *dev, struct ethtool_ringparam *ering)
{
	struct tg3 *tp = netdev_priv(dev);
	int i, irq_sync = 0, err = 0;

	if ((ering->rx_pending > TG3_RX_RING_SIZE - 1) ||
	    (ering->rx_jumbo_pending > TG3_RX_JUMBO_RING_SIZE - 1) ||
	    (ering->tx_pending > TG3_TX_RING_SIZE - 1) ||
	    (ering->tx_pending <= MAX_SKB_FRAGS) ||
	    ((tp->tg3_flags2 & TG3_FLG2_TSO_BUG) &&
	     (ering->tx_pending <= (MAX_SKB_FRAGS * 3))))
		return -EINVAL;

	if (netif_running(dev)) {
		tg3_phy_stop(tp);
		tg3_netif_stop(tp);
		irq_sync = 1;
	}

	tg3_full_lock(tp, irq_sync);

	tp->rx_pending = ering->rx_pending;

	if ((tp->tg3_flags2 & TG3_FLG2_MAX_RXPEND_64) &&
	    tp->rx_pending > 63)
		tp->rx_pending = 63;
	tp->rx_jumbo_pending = ering->rx_jumbo_pending;

	for (i = 0; i < TG3_IRQ_MAX_VECS; i++)
		tp->napi[i].tx_pending = ering->tx_pending;

	if (netif_running(dev)) {
		tg3_halt(tp, RESET_KIND_SHUTDOWN, 1);
		err = tg3_restart_hw(tp, 1);
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

	epause->autoneg = (tp->tg3_flags & TG3_FLAG_PAUSE_AUTONEG) != 0;

	if (tp->link_config.active_flowctrl & FLOW_CTRL_RX)
		epause->rx_pause = 1;
	else
		epause->rx_pause = 0;

	if (tp->link_config.active_flowctrl & FLOW_CTRL_TX)
		epause->tx_pause = 1;
	else
		epause->tx_pause = 0;
}

static int tg3_set_pauseparam(struct net_device *dev, struct ethtool_pauseparam *epause)
{
	struct tg3 *tp = netdev_priv(dev);
	int err = 0;

	if (tp->tg3_flags3 & TG3_FLG3_USE_PHYLIB) {
		if (!(tp->tg3_flags3 & TG3_FLG3_PHY_CONNECTED))
			return -EAGAIN;

		if (epause->autoneg) {
			u32 newadv;
			struct phy_device *phydev;

			phydev = tp->mdio_bus->phy_map[PHY_ADDR];

			if (epause->rx_pause) {
				if (epause->tx_pause)
					newadv = ADVERTISED_Pause;
				else
					newadv = ADVERTISED_Pause |
						 ADVERTISED_Asym_Pause;
			} else if (epause->tx_pause) {
				newadv = ADVERTISED_Asym_Pause;
			} else
				newadv = 0;

			if (tp->tg3_flags3 & TG3_FLG3_PHY_CONNECTED) {
				u32 oldadv = phydev->advertising &
					     (ADVERTISED_Pause |
					      ADVERTISED_Asym_Pause);
				if (oldadv != newadv) {
					phydev->advertising &=
						~(ADVERTISED_Pause |
						  ADVERTISED_Asym_Pause);
					phydev->advertising |= newadv;
					err = phy_start_aneg(phydev);
				}
			} else {
				tp->link_config.advertising &=
						~(ADVERTISED_Pause |
						  ADVERTISED_Asym_Pause);
				tp->link_config.advertising |= newadv;
			}
		} else {
			if (epause->rx_pause)
				tp->link_config.flowctrl |= FLOW_CTRL_RX;
			else
				tp->link_config.flowctrl &= ~FLOW_CTRL_RX;

			if (epause->tx_pause)
				tp->link_config.flowctrl |= FLOW_CTRL_TX;
			else
				tp->link_config.flowctrl &= ~FLOW_CTRL_TX;

			if (netif_running(dev))
				tg3_setup_flow_control(tp, 0, 0);
		}
	} else {
		int irq_sync = 0;

		if (netif_running(dev)) {
			tg3_netif_stop(tp);
			irq_sync = 1;
		}

		tg3_full_lock(tp, irq_sync);

		if (epause->autoneg)
			tp->tg3_flags |= TG3_FLAG_PAUSE_AUTONEG;
		else
			tp->tg3_flags &= ~TG3_FLAG_PAUSE_AUTONEG;
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
			err = tg3_restart_hw(tp, 1);
			if (!err)
				tg3_netif_start(tp);
		}

		tg3_full_unlock(tp);
	}

	return err;
}

static u32 tg3_get_rx_csum(struct net_device *dev)
{
	struct tg3 *tp = netdev_priv(dev);
	return (tp->tg3_flags & TG3_FLAG_RX_CHECKSUMS) != 0;
}

static int tg3_set_rx_csum(struct net_device *dev, u32 data)
{
	struct tg3 *tp = netdev_priv(dev);

	if (tp->tg3_flags & TG3_FLAG_BROKEN_CHECKSUMS) {
		if (data != 0)
			return -EINVAL;
  		return 0;
  	}

	spin_lock_bh(&tp->lock);
	if (data)
		tp->tg3_flags |= TG3_FLAG_RX_CHECKSUMS;
	else
		tp->tg3_flags &= ~TG3_FLAG_RX_CHECKSUMS;
	spin_unlock_bh(&tp->lock);

	return 0;
}

static int tg3_set_tx_csum(struct net_device *dev, u32 data)
{
	struct tg3 *tp = netdev_priv(dev);

	if (tp->tg3_flags & TG3_FLAG_BROKEN_CHECKSUMS) {
		if (data != 0)
			return -EINVAL;
  		return 0;
  	}

	if (tp->tg3_flags3 & TG3_FLG3_5755_PLUS)
		ethtool_op_set_tx_ipv6_csum(dev, data);
	else
		ethtool_op_set_tx_csum(dev, data);

	return 0;
}

static int tg3_get_sset_count (struct net_device *dev, int sset)
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

static void tg3_get_strings (struct net_device *dev, u32 stringset, u8 *buf)
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

static int tg3_phys_id(struct net_device *dev, u32 data)
{
	struct tg3 *tp = netdev_priv(dev);
	int i;

	if (!netif_running(tp->dev))
		return -EAGAIN;

	if (data == 0)
		data = UINT_MAX / 2;

	for (i = 0; i < (data * 2); i++) {
		if ((i % 2) == 0)
			tw32(MAC_LED_CTRL, LED_CTRL_LNKLED_OVERRIDE |
					   LED_CTRL_1000MBPS_ON |
					   LED_CTRL_100MBPS_ON |
					   LED_CTRL_10MBPS_ON |
					   LED_CTRL_TRAFFIC_OVERRIDE |
					   LED_CTRL_TRAFFIC_BLINK |
					   LED_CTRL_TRAFFIC_LED);

		else
			tw32(MAC_LED_CTRL, LED_CTRL_LNKLED_OVERRIDE |
					   LED_CTRL_TRAFFIC_OVERRIDE);

		if (msleep_interruptible(500))
			break;
	}
	tw32(MAC_LED_CTRL, tp->led_ctrl);
	return 0;
}

static void tg3_get_ethtool_stats (struct net_device *dev,
				   struct ethtool_stats *estats, u64 *tmp_stats)
{
	struct tg3 *tp = netdev_priv(dev);
	memcpy(tmp_stats, tg3_get_estats(tp), sizeof(tp->estats));
}

#define NVRAM_TEST_SIZE 0x100
#define NVRAM_SELFBOOT_FORMAT1_0_SIZE	0x14
#define NVRAM_SELFBOOT_FORMAT1_2_SIZE	0x18
#define NVRAM_SELFBOOT_FORMAT1_3_SIZE	0x1c
#define NVRAM_SELFBOOT_HW_SIZE 0x20
#define NVRAM_SELFBOOT_DATA_SIZE 0x1c

static int tg3_test_nvram(struct tg3 *tp)
{
	u32 csum, magic;
	__be32 *buf;
	int i, j, k, err = 0, size;

	if (tp->tg3_flags3 & TG3_FLG3_NO_NVRAM)
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
			default:
				return 0;
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
			}
			else if (i == 16) {
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

	/* Bootstrap checksum at offset 0x10 */
	csum = calc_crc((unsigned char *) buf, 0x10);
	if (csum != be32_to_cpu(buf[0x10/4]))
		goto out;

	/* Manufacturing block starts at offset 0x74, checksum at 0xfc */
	csum = calc_crc((unsigned char *) &buf[0x74/4], 0x88);
	if (csum != be32_to_cpu(buf[0xfc/4]))
		goto out;

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

	if (tp->tg3_flags2 & TG3_FLG2_ANY_SERDES)
		max = TG3_SERDES_TIMEOUT_SEC;
	else
		max = TG3_COPPER_TIMEOUT_SEC;

	for (i = 0; i < max; i++) {
		if (netif_carrier_ok(tp->dev))
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
	if (tp->tg3_flags2 & TG3_FLG2_5705_PLUS) {
		is_5705 = 1;
		if (tp->tg3_flags2 & TG3_FLG2_5750_PLUS)
			is_5750 = 1;
	}

	for (i = 0; reg_tbl[i].offset != 0xffff; i++) {
		if (is_5705 && (reg_tbl[i].flags & TG3_FL_NOT_5705))
			continue;

		if (!is_5705 && (reg_tbl[i].flags & TG3_FL_5705))
			continue;

		if ((tp->tg3_flags2 & TG3_FLG2_IS_5788) &&
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
		printk(KERN_ERR PFX "Register test failed at offset %x\n",
		       offset);
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
	};
	struct mem_entry *mem_tbl;
	int err = 0;
	int i;

	if (tp->tg3_flags3 & TG3_FLG3_5755_PLUS)
		mem_tbl = mem_tbl_5755;
	else if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5906)
		mem_tbl = mem_tbl_5906;
	else if (tp->tg3_flags2 & TG3_FLG2_5705_PLUS)
		mem_tbl = mem_tbl_5705;
	else
		mem_tbl = mem_tbl_570x;

	for (i = 0; mem_tbl[i].offset != 0xffffffff; i++) {
		if ((err = tg3_do_mem_test(tp, mem_tbl[i].offset,
		    mem_tbl[i].len)) != 0)
			break;
	}

	return err;
}

#define TG3_MAC_LOOPBACK	0
#define TG3_PHY_LOOPBACK	1

static int tg3_run_loopback(struct tg3 *tp, int loopback_mode)
{
	u32 mac_mode, rx_start_idx, rx_idx, tx_idx, opaque_key;
	u32 desc_idx, coal_now;
	struct sk_buff *skb, *rx_skb;
	u8 *tx_data;
	dma_addr_t map;
	int num_pkts, tx_len, rx_len, i, err;
	struct tg3_rx_buffer_desc *desc;
	struct tg3_napi *tnapi, *rnapi;
	struct tg3_rx_prodring_set *tpr = &tp->prodring[0];

	if (tp->irq_cnt > 1) {
		tnapi = &tp->napi[1];
		rnapi = &tp->napi[1];
	} else {
		tnapi = &tp->napi[0];
		rnapi = &tp->napi[0];
	}
	coal_now = tnapi->coal_now | rnapi->coal_now;

	if (loopback_mode == TG3_MAC_LOOPBACK) {
		/* HW errata - mac loopback fails in some cases on 5780.
		 * Normal traffic and PHY loopback are not affected by
		 * errata.
		 */
		if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5780)
			return 0;

		mac_mode = (tp->mac_mode & ~MAC_MODE_PORT_MODE_MASK) |
			   MAC_MODE_PORT_INT_LPBACK;
		if (!(tp->tg3_flags2 & TG3_FLG2_5705_PLUS))
			mac_mode |= MAC_MODE_LINK_POLARITY;
		if (tp->tg3_flags & TG3_FLAG_10_100_ONLY)
			mac_mode |= MAC_MODE_PORT_MODE_MII;
		else
			mac_mode |= MAC_MODE_PORT_MODE_GMII;
		tw32(MAC_MODE, mac_mode);
	} else if (loopback_mode == TG3_PHY_LOOPBACK) {
		u32 val;

		if (tp->tg3_flags3 & TG3_FLG3_PHY_IS_FET) {
			tg3_phy_fet_toggle_apd(tp, false);
			val = BMCR_LOOPBACK | BMCR_FULLDPLX | BMCR_SPEED100;
		} else
			val = BMCR_LOOPBACK | BMCR_FULLDPLX | BMCR_SPEED1000;

		tg3_phy_toggle_automdix(tp, 0);

		tg3_writephy(tp, MII_BMCR, val);
		udelay(40);

		mac_mode = tp->mac_mode & ~MAC_MODE_PORT_MODE_MASK;
		if (tp->tg3_flags3 & TG3_FLG3_PHY_IS_FET) {
			if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5906)
				tg3_writephy(tp, MII_TG3_FET_PTEST, 0x1800);
			mac_mode |= MAC_MODE_PORT_MODE_MII;
		} else
			mac_mode |= MAC_MODE_PORT_MODE_GMII;

		/* reset to prevent losing 1st rx packet intermittently */
		if (tp->tg3_flags2 & TG3_FLG2_MII_SERDES) {
			tw32_f(MAC_RX_MODE, RX_MODE_RESET);
			udelay(10);
			tw32_f(MAC_RX_MODE, tp->rx_mode);
		}
		if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5700) {
			if ((tp->phy_id & PHY_ID_MASK) == PHY_ID_BCM5401)
				mac_mode &= ~MAC_MODE_LINK_POLARITY;
			else if ((tp->phy_id & PHY_ID_MASK) == PHY_ID_BCM5411)
				mac_mode |= MAC_MODE_LINK_POLARITY;
			tg3_writephy(tp, MII_TG3_EXT_CTRL,
				     MII_TG3_EXT_CTRL_LNK3_LED_MODE);
		}
		tw32(MAC_MODE, mac_mode);
	}
	else
		return -EINVAL;

	err = -EIO;

	tx_len = 1514;
	skb = netdev_alloc_skb(tp->dev, tx_len);
	if (!skb)
		return -ENOMEM;

	tx_data = skb_put(skb, tx_len);
	memcpy(tx_data, tp->dev->dev_addr, 6);
	memset(tx_data + 6, 0x0, 8);

	tw32(MAC_RX_MTU_SIZE, tx_len + 4);

	for (i = 14; i < tx_len; i++)
		tx_data[i] = (u8) (i & 0xff);

	map = pci_map_single(tp->pdev, skb->data, tx_len, PCI_DMA_TODEVICE);

	tw32_f(HOSTCC_MODE, tp->coalesce_mode | HOSTCC_MODE_ENABLE |
	       rnapi->coal_now);

	udelay(10);

	rx_start_idx = rnapi->hw_status->idx[0].rx_producer;

	num_pkts = 0;

	tg3_set_txd(tnapi, tnapi->tx_prod, map, tx_len, 0, 1);

	tnapi->tx_prod++;
	num_pkts++;

	tw32_tx_mbox(tnapi->prodmbox, tnapi->tx_prod);
	tr32_mailbox(tnapi->prodmbox);

	udelay(10);

	/* 250 usec to allow enough time on some 10/100 Mbps devices.  */
	for (i = 0; i < 25; i++) {
		tw32_f(HOSTCC_MODE, tp->coalesce_mode | HOSTCC_MODE_ENABLE |
		       coal_now);

		udelay(10);

		tx_idx = tnapi->hw_status->idx[0].tx_consumer;
		rx_idx = rnapi->hw_status->idx[0].rx_producer;
		if ((tx_idx == tnapi->tx_prod) &&
		    (rx_idx == (rx_start_idx + num_pkts)))
			break;
	}

	pci_unmap_single(tp->pdev, map, tx_len, PCI_DMA_TODEVICE);
	dev_kfree_skb(skb);

	if (tx_idx != tnapi->tx_prod)
		goto out;

	if (rx_idx != rx_start_idx + num_pkts)
		goto out;

	desc = &rnapi->rx_rcb[rx_start_idx];
	desc_idx = desc->opaque & RXD_OPAQUE_INDEX_MASK;
	opaque_key = desc->opaque & RXD_OPAQUE_RING_MASK;
	if (opaque_key != RXD_OPAQUE_RING_STD)
		goto out;

	if ((desc->err_vlan & RXD_ERR_MASK) != 0 &&
	    (desc->err_vlan != RXD_ERR_ODD_NIBBLE_RCVD_MII))
		goto out;

	rx_len = ((desc->idx_len & RXD_LEN_MASK) >> RXD_LEN_SHIFT) - 4;
	if (rx_len != tx_len)
		goto out;

	rx_skb = tpr->rx_std_buffers[desc_idx].skb;

	map = pci_unmap_addr(&tpr->rx_std_buffers[desc_idx], mapping);
	pci_dma_sync_single_for_cpu(tp->pdev, map, rx_len, PCI_DMA_FROMDEVICE);

	for (i = 14; i < tx_len; i++) {
		if (*(rx_skb->data + i) != (u8) (i & 0xff))
			goto out;
	}
	err = 0;

	/* tg3_free_rings will unmap and free the rx_skb */
out:
	return err;
}

#define TG3_MAC_LOOPBACK_FAILED		1
#define TG3_PHY_LOOPBACK_FAILED		2
#define TG3_LOOPBACK_FAILED		(TG3_MAC_LOOPBACK_FAILED |	\
					 TG3_PHY_LOOPBACK_FAILED)

static int tg3_test_loopback(struct tg3 *tp)
{
	int err = 0;
	u32 cpmuctrl = 0;

	if (!netif_running(tp->dev))
		return TG3_LOOPBACK_FAILED;

	err = tg3_reset_hw(tp, 1);
	if (err)
		return TG3_LOOPBACK_FAILED;

	/* Turn off gphy autopowerdown. */
	if (tp->tg3_flags3 & TG3_FLG3_PHY_ENABLE_APD)
		tg3_phy_toggle_apd(tp, false);

	if (tp->tg3_flags & TG3_FLAG_CPMU_PRESENT) {
		int i;
		u32 status;

		tw32(TG3_CPMU_MUTEX_REQ, CPMU_MUTEX_REQ_DRIVER);

		/* Wait for up to 40 microseconds to acquire lock. */
		for (i = 0; i < 4; i++) {
			status = tr32(TG3_CPMU_MUTEX_GNT);
			if (status == CPMU_MUTEX_GNT_DRIVER)
				break;
			udelay(10);
		}

		if (status != CPMU_MUTEX_GNT_DRIVER)
			return TG3_LOOPBACK_FAILED;

		/* Turn off link-based power management. */
		cpmuctrl = tr32(TG3_CPMU_CTRL);
		tw32(TG3_CPMU_CTRL,
		     cpmuctrl & ~(CPMU_CTRL_LINK_SPEED_MODE |
				  CPMU_CTRL_LINK_AWARE_MODE));
	}

	if (tg3_run_loopback(tp, TG3_MAC_LOOPBACK))
		err |= TG3_MAC_LOOPBACK_FAILED;

	if (tp->tg3_flags & TG3_FLAG_CPMU_PRESENT) {
		tw32(TG3_CPMU_CTRL, cpmuctrl);

		/* Release the mutex */
		tw32(TG3_CPMU_MUTEX_GNT, CPMU_MUTEX_GNT_DRIVER);
	}

	if (!(tp->tg3_flags2 & TG3_FLG2_PHY_SERDES) &&
	    !(tp->tg3_flags3 & TG3_FLG3_USE_PHYLIB)) {
		if (tg3_run_loopback(tp, TG3_PHY_LOOPBACK))
			err |= TG3_PHY_LOOPBACK_FAILED;
	}

	/* Re-enable gphy autopowerdown. */
	if (tp->tg3_flags3 & TG3_FLG3_PHY_ENABLE_APD)
		tg3_phy_toggle_apd(tp, true);

	return err;
}

static void tg3_self_test(struct net_device *dev, struct ethtool_test *etest,
			  u64 *data)
{
	struct tg3 *tp = netdev_priv(dev);

	if (tp->link_config.phy_is_low_power)
		tg3_set_power_state(tp, PCI_D0);

	memset(data, 0, sizeof(u64) * TG3_NUM_TEST);

	if (tg3_test_nvram(tp) != 0) {
		etest->flags |= ETH_TEST_FL_FAILED;
		data[0] = 1;
	}
	if (tg3_test_link(tp) != 0) {
		etest->flags |= ETH_TEST_FL_FAILED;
		data[1] = 1;
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
		if (!(tp->tg3_flags2 & TG3_FLG2_5705_PLUS))
			tg3_halt_cpu(tp, TX_CPU_BASE);
		if (!err)
			tg3_nvram_unlock(tp);

		if (tp->tg3_flags2 & TG3_FLG2_MII_SERDES)
			tg3_phy_reset(tp);

		if (tg3_test_registers(tp) != 0) {
			etest->flags |= ETH_TEST_FL_FAILED;
			data[2] = 1;
		}
		if (tg3_test_memory(tp) != 0) {
			etest->flags |= ETH_TEST_FL_FAILED;
			data[3] = 1;
		}
		if ((data[4] = tg3_test_loopback(tp)) != 0)
			etest->flags |= ETH_TEST_FL_FAILED;

		tg3_full_unlock(tp);

		if (tg3_test_interrupt(tp) != 0) {
			etest->flags |= ETH_TEST_FL_FAILED;
			data[5] = 1;
		}

		tg3_full_lock(tp, 0);

		tg3_halt(tp, RESET_KIND_SHUTDOWN, 1);
		if (netif_running(dev)) {
			tp->tg3_flags |= TG3_FLAG_INIT_COMPLETE;
			err2 = tg3_restart_hw(tp, 1);
			if (!err2)
				tg3_netif_start(tp);
		}

		tg3_full_unlock(tp);

		if (irq_sync && !err2)
			tg3_phy_start(tp);
	}
	if (tp->link_config.phy_is_low_power)
		tg3_set_power_state(tp, PCI_D3hot);

}

static int tg3_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	struct mii_ioctl_data *data = if_mii(ifr);
	struct tg3 *tp = netdev_priv(dev);
	int err;

	if (tp->tg3_flags3 & TG3_FLG3_USE_PHYLIB) {
		if (!(tp->tg3_flags3 & TG3_FLG3_PHY_CONNECTED))
			return -EAGAIN;
		return phy_mii_ioctl(tp->mdio_bus->phy_map[PHY_ADDR], data, cmd);
	}

	switch(cmd) {
	case SIOCGMIIPHY:
		data->phy_id = tp->phy_addr;

		/* fallthru */
	case SIOCGMIIREG: {
		u32 mii_regval;

		if (tp->tg3_flags2 & TG3_FLG2_PHY_SERDES)
			break;			/* We have no PHY */

		if (tp->link_config.phy_is_low_power)
			return -EAGAIN;

		spin_lock_bh(&tp->lock);
		err = tg3_readphy(tp, data->reg_num & 0x1f, &mii_regval);
		spin_unlock_bh(&tp->lock);

		data->val_out = mii_regval;

		return err;
	}

	case SIOCSMIIREG:
		if (tp->tg3_flags2 & TG3_FLG2_PHY_SERDES)
			break;			/* We have no PHY */

		if (tp->link_config.phy_is_low_power)
			return -EAGAIN;

		spin_lock_bh(&tp->lock);
		err = tg3_writephy(tp, data->reg_num & 0x1f, data->val_in);
		spin_unlock_bh(&tp->lock);

		return err;

	default:
		/* do nothing */
		break;
	}
	return -EOPNOTSUPP;
}

#if TG3_VLAN_TAG_USED
static void tg3_vlan_rx_register(struct net_device *dev, struct vlan_group *grp)
{
	struct tg3 *tp = netdev_priv(dev);

	if (!netif_running(dev)) {
		tp->vlgrp = grp;
		return;
	}

	tg3_netif_stop(tp);

	tg3_full_lock(tp, 0);

	tp->vlgrp = grp;

	/* Update RX_MODE_KEEP_VLAN_TAG bit in RX_MODE register. */
	__tg3_set_rx_mode(dev);

	tg3_netif_start(tp);

	tg3_full_unlock(tp);
}
#endif

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

	if (!(tp->tg3_flags2 & TG3_FLG2_5705_PLUS)) {
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
	.get_rx_csum		= tg3_get_rx_csum,
	.set_rx_csum		= tg3_set_rx_csum,
	.set_tx_csum		= tg3_set_tx_csum,
	.set_sg			= ethtool_op_set_sg,
	.set_tso		= tg3_set_tso,
	.self_test		= tg3_self_test,
	.get_strings		= tg3_get_strings,
	.phys_id		= tg3_phys_id,
	.get_ethtool_stats	= tg3_get_ethtool_stats,
	.get_coalesce		= tg3_get_coalesce,
	.set_coalesce		= tg3_set_coalesce,
	.get_sset_count		= tg3_get_sset_count,
};

static void __devinit tg3_get_eeprom_size(struct tg3 *tp)
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

static void __devinit tg3_get_nvram_size(struct tg3 *tp)
{
	u32 val;

	if ((tp->tg3_flags3 & TG3_FLG3_NO_NVRAM) ||
	    tg3_nvram_read(tp, 0, &val) != 0)
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

static void __devinit tg3_get_nvram_info(struct tg3 *tp)
{
	u32 nvcfg1;

	nvcfg1 = tr32(NVRAM_CFG1);
	if (nvcfg1 & NVRAM_CFG1_FLASHIF_ENAB) {
		tp->tg3_flags2 |= TG3_FLG2_FLASH;
	} else {
		nvcfg1 &= ~NVRAM_CFG1_COMPAT_BYPASS;
		tw32(NVRAM_CFG1, nvcfg1);
	}

	if ((GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5750) ||
	    (tp->tg3_flags2 & TG3_FLG2_5780_CLASS)) {
		switch (nvcfg1 & NVRAM_CFG1_VENDOR_MASK) {
		case FLASH_VENDOR_ATMEL_FLASH_BUFFERED:
			tp->nvram_jedecnum = JEDEC_ATMEL;
			tp->nvram_pagesize = ATMEL_AT45DB0X1B_PAGE_SIZE;
			tp->tg3_flags |= TG3_FLAG_NVRAM_BUFFERED;
			break;
		case FLASH_VENDOR_ATMEL_FLASH_UNBUFFERED:
			tp->nvram_jedecnum = JEDEC_ATMEL;
			tp->nvram_pagesize = ATMEL_AT25F512_PAGE_SIZE;
			break;
		case FLASH_VENDOR_ATMEL_EEPROM:
			tp->nvram_jedecnum = JEDEC_ATMEL;
			tp->nvram_pagesize = ATMEL_AT24C512_CHIP_SIZE;
			tp->tg3_flags |= TG3_FLAG_NVRAM_BUFFERED;
			break;
		case FLASH_VENDOR_ST:
			tp->nvram_jedecnum = JEDEC_ST;
			tp->nvram_pagesize = ST_M45PEX0_PAGE_SIZE;
			tp->tg3_flags |= TG3_FLAG_NVRAM_BUFFERED;
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
		tp->tg3_flags |= TG3_FLAG_NVRAM_BUFFERED;
	}
}

static void __devinit tg3_nvram_get_pagesize(struct tg3 *tp, u32 nvmcfg1)
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

static void __devinit tg3_get_5752_nvram_info(struct tg3 *tp)
{
	u32 nvcfg1;

	nvcfg1 = tr32(NVRAM_CFG1);

	/* NVRAM protection for TPM */
	if (nvcfg1 & (1 << 27))
		tp->tg3_flags2 |= TG3_FLG2_PROTECTED_NVRAM;

	switch (nvcfg1 & NVRAM_CFG1_5752VENDOR_MASK) {
	case FLASH_5752VENDOR_ATMEL_EEPROM_64KHZ:
	case FLASH_5752VENDOR_ATMEL_EEPROM_376KHZ:
		tp->nvram_jedecnum = JEDEC_ATMEL;
		tp->tg3_flags |= TG3_FLAG_NVRAM_BUFFERED;
		break;
	case FLASH_5752VENDOR_ATMEL_FLASH_BUFFERED:
		tp->nvram_jedecnum = JEDEC_ATMEL;
		tp->tg3_flags |= TG3_FLAG_NVRAM_BUFFERED;
		tp->tg3_flags2 |= TG3_FLG2_FLASH;
		break;
	case FLASH_5752VENDOR_ST_M45PE10:
	case FLASH_5752VENDOR_ST_M45PE20:
	case FLASH_5752VENDOR_ST_M45PE40:
		tp->nvram_jedecnum = JEDEC_ST;
		tp->tg3_flags |= TG3_FLAG_NVRAM_BUFFERED;
		tp->tg3_flags2 |= TG3_FLG2_FLASH;
		break;
	}

	if (tp->tg3_flags2 & TG3_FLG2_FLASH) {
		tg3_nvram_get_pagesize(tp, nvcfg1);
	} else {
		/* For eeprom, set pagesize to maximum eeprom size */
		tp->nvram_pagesize = ATMEL_AT24C512_CHIP_SIZE;

		nvcfg1 &= ~NVRAM_CFG1_COMPAT_BYPASS;
		tw32(NVRAM_CFG1, nvcfg1);
	}
}

static void __devinit tg3_get_5755_nvram_info(struct tg3 *tp)
{
	u32 nvcfg1, protect = 0;

	nvcfg1 = tr32(NVRAM_CFG1);

	/* NVRAM protection for TPM */
	if (nvcfg1 & (1 << 27)) {
		tp->tg3_flags2 |= TG3_FLG2_PROTECTED_NVRAM;
		protect = 1;
	}

	nvcfg1 &= NVRAM_CFG1_5752VENDOR_MASK;
	switch (nvcfg1) {
	case FLASH_5755VENDOR_ATMEL_FLASH_1:
	case FLASH_5755VENDOR_ATMEL_FLASH_2:
	case FLASH_5755VENDOR_ATMEL_FLASH_3:
	case FLASH_5755VENDOR_ATMEL_FLASH_5:
		tp->nvram_jedecnum = JEDEC_ATMEL;
		tp->tg3_flags |= TG3_FLAG_NVRAM_BUFFERED;
		tp->tg3_flags2 |= TG3_FLG2_FLASH;
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
		tp->tg3_flags |= TG3_FLAG_NVRAM_BUFFERED;
		tp->tg3_flags2 |= TG3_FLG2_FLASH;
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

static void __devinit tg3_get_5787_nvram_info(struct tg3 *tp)
{
	u32 nvcfg1;

	nvcfg1 = tr32(NVRAM_CFG1);

	switch (nvcfg1 & NVRAM_CFG1_5752VENDOR_MASK) {
	case FLASH_5787VENDOR_ATMEL_EEPROM_64KHZ:
	case FLASH_5787VENDOR_ATMEL_EEPROM_376KHZ:
	case FLASH_5787VENDOR_MICRO_EEPROM_64KHZ:
	case FLASH_5787VENDOR_MICRO_EEPROM_376KHZ:
		tp->nvram_jedecnum = JEDEC_ATMEL;
		tp->tg3_flags |= TG3_FLAG_NVRAM_BUFFERED;
		tp->nvram_pagesize = ATMEL_AT24C512_CHIP_SIZE;

		nvcfg1 &= ~NVRAM_CFG1_COMPAT_BYPASS;
		tw32(NVRAM_CFG1, nvcfg1);
		break;
	case FLASH_5752VENDOR_ATMEL_FLASH_BUFFERED:
	case FLASH_5755VENDOR_ATMEL_FLASH_1:
	case FLASH_5755VENDOR_ATMEL_FLASH_2:
	case FLASH_5755VENDOR_ATMEL_FLASH_3:
		tp->nvram_jedecnum = JEDEC_ATMEL;
		tp->tg3_flags |= TG3_FLAG_NVRAM_BUFFERED;
		tp->tg3_flags2 |= TG3_FLG2_FLASH;
		tp->nvram_pagesize = 264;
		break;
	case FLASH_5752VENDOR_ST_M45PE10:
	case FLASH_5752VENDOR_ST_M45PE20:
	case FLASH_5752VENDOR_ST_M45PE40:
		tp->nvram_jedecnum = JEDEC_ST;
		tp->tg3_flags |= TG3_FLAG_NVRAM_BUFFERED;
		tp->tg3_flags2 |= TG3_FLG2_FLASH;
		tp->nvram_pagesize = 256;
		break;
	}
}

static void __devinit tg3_get_5761_nvram_info(struct tg3 *tp)
{
	u32 nvcfg1, protect = 0;

	nvcfg1 = tr32(NVRAM_CFG1);

	/* NVRAM protection for TPM */
	if (nvcfg1 & (1 << 27)) {
		tp->tg3_flags2 |= TG3_FLG2_PROTECTED_NVRAM;
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
		tp->tg3_flags |= TG3_FLAG_NVRAM_BUFFERED;
		tp->tg3_flags2 |= TG3_FLG2_FLASH;
		tp->tg3_flags3 |= TG3_FLG3_NO_NVRAM_ADDR_TRANS;
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
		tp->tg3_flags |= TG3_FLAG_NVRAM_BUFFERED;
		tp->tg3_flags2 |= TG3_FLG2_FLASH;
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

static void __devinit tg3_get_5906_nvram_info(struct tg3 *tp)
{
	tp->nvram_jedecnum = JEDEC_ATMEL;
	tp->tg3_flags |= TG3_FLAG_NVRAM_BUFFERED;
	tp->nvram_pagesize = ATMEL_AT24C512_CHIP_SIZE;
}

static void __devinit tg3_get_57780_nvram_info(struct tg3 *tp)
{
	u32 nvcfg1;

	nvcfg1 = tr32(NVRAM_CFG1);

	switch (nvcfg1 & NVRAM_CFG1_5752VENDOR_MASK) {
	case FLASH_5787VENDOR_ATMEL_EEPROM_376KHZ:
	case FLASH_5787VENDOR_MICRO_EEPROM_376KHZ:
		tp->nvram_jedecnum = JEDEC_ATMEL;
		tp->tg3_flags |= TG3_FLAG_NVRAM_BUFFERED;
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
		tp->tg3_flags |= TG3_FLAG_NVRAM_BUFFERED;
		tp->tg3_flags2 |= TG3_FLG2_FLASH;

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
		tp->tg3_flags |= TG3_FLAG_NVRAM_BUFFERED;
		tp->tg3_flags2 |= TG3_FLG2_FLASH;

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
		tp->tg3_flags3 |= TG3_FLG3_NO_NVRAM;
		return;
	}

	tg3_nvram_get_pagesize(tp, nvcfg1);
	if (tp->nvram_pagesize != 264 && tp->nvram_pagesize != 528)
		tp->tg3_flags3 |= TG3_FLG3_NO_NVRAM_ADDR_TRANS;
}


static void __devinit tg3_get_5717_nvram_info(struct tg3 *tp)
{
	u32 nvcfg1;

	nvcfg1 = tr32(NVRAM_CFG1);

	switch (nvcfg1 & NVRAM_CFG1_5752VENDOR_MASK) {
	case FLASH_5717VENDOR_ATMEL_EEPROM:
	case FLASH_5717VENDOR_MICRO_EEPROM:
		tp->nvram_jedecnum = JEDEC_ATMEL;
		tp->tg3_flags |= TG3_FLAG_NVRAM_BUFFERED;
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
		tp->tg3_flags |= TG3_FLAG_NVRAM_BUFFERED;
		tp->tg3_flags2 |= TG3_FLG2_FLASH;

		switch (nvcfg1 & NVRAM_CFG1_5752VENDOR_MASK) {
		case FLASH_5717VENDOR_ATMEL_MDB021D:
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
		tp->tg3_flags |= TG3_FLAG_NVRAM_BUFFERED;
		tp->tg3_flags2 |= TG3_FLG2_FLASH;

		switch (nvcfg1 & NVRAM_CFG1_5752VENDOR_MASK) {
		case FLASH_5717VENDOR_ST_M_M25PE20:
		case FLASH_5717VENDOR_ST_A_M25PE20:
		case FLASH_5717VENDOR_ST_M_M45PE20:
		case FLASH_5717VENDOR_ST_A_M45PE20:
			tp->nvram_size = TG3_NVRAM_SIZE_256KB;
			break;
		default:
			tp->nvram_size = TG3_NVRAM_SIZE_128KB;
			break;
		}
		break;
	default:
		tp->tg3_flags3 |= TG3_FLG3_NO_NVRAM;
		return;
	}

	tg3_nvram_get_pagesize(tp, nvcfg1);
	if (tp->nvram_pagesize != 264 && tp->nvram_pagesize != 528)
		tp->tg3_flags3 |= TG3_FLG3_NO_NVRAM_ADDR_TRANS;
}

/* Chips other than 5700/5701 use the NVRAM for fetching info. */
static void __devinit tg3_nvram_init(struct tg3 *tp)
{
	tw32_f(GRC_EEPROM_ADDR,
	     (EEPROM_ADDR_FSM_RESET |
	      (EEPROM_DEFAULT_CLOCK_PERIOD <<
	       EEPROM_ADDR_CLKPERD_SHIFT)));

	msleep(1);

	/* Enable seeprom accesses. */
	tw32_f(GRC_LOCAL_CTRL,
	     tr32(GRC_LOCAL_CTRL) | GRC_LCLCTRL_AUTO_SEEPROM);
	udelay(100);

	if (GET_ASIC_REV(tp->pci_chip_rev_id) != ASIC_REV_5700 &&
	    GET_ASIC_REV(tp->pci_chip_rev_id) != ASIC_REV_5701) {
		tp->tg3_flags |= TG3_FLAG_NVRAM;

		if (tg3_nvram_lock(tp)) {
			printk(KERN_WARNING PFX "%s: Cannot get nvarm lock, "
			       "tg3_nvram_init failed.\n", tp->dev->name);
			return;
		}
		tg3_enable_nvram_access(tp);

		tp->nvram_size = 0;

		if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5752)
			tg3_get_5752_nvram_info(tp);
		else if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5755)
			tg3_get_5755_nvram_info(tp);
		else if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5787 ||
			 GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5784 ||
			 GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5785)
			tg3_get_5787_nvram_info(tp);
		else if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5761)
			tg3_get_5761_nvram_info(tp);
		else if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5906)
			tg3_get_5906_nvram_info(tp);
		else if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_57780)
			tg3_get_57780_nvram_info(tp);
		else if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5717)
			tg3_get_5717_nvram_info(tp);
		else
			tg3_get_nvram_info(tp);

		if (tp->nvram_size == 0)
			tg3_get_nvram_size(tp);

		tg3_disable_nvram_access(tp);
		tg3_nvram_unlock(tp);

	} else {
		tp->tg3_flags &= ~(TG3_FLAG_NVRAM | TG3_FLAG_NVRAM_BUFFERED);

		tg3_get_eeprom_size(tp);
	}
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

			if ((ret = tg3_nvram_exec_cmd(tp, nvram_cmd)))
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

		tw32(NVRAM_ADDR, phy_addr);

		nvram_cmd = NVRAM_CMD_GO | NVRAM_CMD_DONE | NVRAM_CMD_WR;

	        if ((page_off == 0) || (i == 0))
			nvram_cmd |= NVRAM_CMD_FIRST;
		if (page_off == (tp->nvram_pagesize - 4))
			nvram_cmd |= NVRAM_CMD_LAST;

		if (i == (len - 4))
			nvram_cmd |= NVRAM_CMD_LAST;

		if (GET_ASIC_REV(tp->pci_chip_rev_id) != ASIC_REV_5752 &&
		    !(tp->tg3_flags3 & TG3_FLG3_5755_PLUS) &&
		    (tp->nvram_jedecnum == JEDEC_ST) &&
		    (nvram_cmd & NVRAM_CMD_FIRST)) {

			if ((ret = tg3_nvram_exec_cmd(tp,
				NVRAM_CMD_WREN | NVRAM_CMD_GO |
				NVRAM_CMD_DONE)))

				break;
		}
		if (!(tp->tg3_flags2 & TG3_FLG2_FLASH)) {
			/* We always do complete word writes to eeprom. */
			nvram_cmd |= (NVRAM_CMD_FIRST | NVRAM_CMD_LAST);
		}

		if ((ret = tg3_nvram_exec_cmd(tp, nvram_cmd)))
			break;
	}
	return ret;
}

/* offset and length are dword aligned */
static int tg3_nvram_write_block(struct tg3 *tp, u32 offset, u32 len, u8 *buf)
{
	int ret;

	if (tp->tg3_flags & TG3_FLAG_EEPROM_WRITE_PROT) {
		tw32_f(GRC_LOCAL_CTRL, tp->grc_local_ctrl &
		       ~GRC_LCLCTRL_GPIO_OUTPUT1);
		udelay(40);
	}

	if (!(tp->tg3_flags & TG3_FLAG_NVRAM)) {
		ret = tg3_nvram_write_block_using_eeprom(tp, offset, len, buf);
	}
	else {
		u32 grc_mode;

		ret = tg3_nvram_lock(tp);
		if (ret)
			return ret;

		tg3_enable_nvram_access(tp);
		if ((tp->tg3_flags2 & TG3_FLG2_5750_PLUS) &&
		    !(tp->tg3_flags2 & TG3_FLG2_PROTECTED_NVRAM))
			tw32(NVRAM_WRITE1, 0x406);

		grc_mode = tr32(GRC_MODE);
		tw32(GRC_MODE, grc_mode | GRC_MODE_NVRAM_WR_ENABLE);

		if ((tp->tg3_flags & TG3_FLAG_NVRAM_BUFFERED) ||
			!(tp->tg3_flags2 & TG3_FLG2_FLASH)) {

			ret = tg3_nvram_write_block_buffered(tp, offset, len,
				buf);
		}
		else {
			ret = tg3_nvram_write_block_unbuffered(tp, offset, len,
				buf);
		}

		grc_mode = tr32(GRC_MODE);
		tw32(GRC_MODE, grc_mode & ~GRC_MODE_NVRAM_WR_ENABLE);

		tg3_disable_nvram_access(tp);
		tg3_nvram_unlock(tp);
	}

	if (tp->tg3_flags & TG3_FLAG_EEPROM_WRITE_PROT) {
		tw32_f(GRC_LOCAL_CTRL, tp->grc_local_ctrl);
		udelay(40);
	}

	return ret;
}

struct subsys_tbl_ent {
	u16 subsys_vendor, subsys_devid;
	u32 phy_id;
};

static struct subsys_tbl_ent subsys_id_to_phy_id[] = {
	/* Broadcom boards. */
	{ PCI_VENDOR_ID_BROADCOM, 0x1644, PHY_ID_BCM5401 }, /* BCM95700A6 */
	{ PCI_VENDOR_ID_BROADCOM, 0x0001, PHY_ID_BCM5701 }, /* BCM95701A5 */
	{ PCI_VENDOR_ID_BROADCOM, 0x0002, PHY_ID_BCM8002 }, /* BCM95700T6 */
	{ PCI_VENDOR_ID_BROADCOM, 0x0003, 0 },		    /* BCM95700A9 */
	{ PCI_VENDOR_ID_BROADCOM, 0x0005, PHY_ID_BCM5701 }, /* BCM95701T1 */
	{ PCI_VENDOR_ID_BROADCOM, 0x0006, PHY_ID_BCM5701 }, /* BCM95701T8 */
	{ PCI_VENDOR_ID_BROADCOM, 0x0007, 0 },		    /* BCM95701A7 */
	{ PCI_VENDOR_ID_BROADCOM, 0x0008, PHY_ID_BCM5701 }, /* BCM95701A10 */
	{ PCI_VENDOR_ID_BROADCOM, 0x8008, PHY_ID_BCM5701 }, /* BCM95701A12 */
	{ PCI_VENDOR_ID_BROADCOM, 0x0009, PHY_ID_BCM5703 }, /* BCM95703Ax1 */
	{ PCI_VENDOR_ID_BROADCOM, 0x8009, PHY_ID_BCM5703 }, /* BCM95703Ax2 */

	/* 3com boards. */
	{ PCI_VENDOR_ID_3COM, 0x1000, PHY_ID_BCM5401 }, /* 3C996T */
	{ PCI_VENDOR_ID_3COM, 0x1006, PHY_ID_BCM5701 }, /* 3C996BT */
	{ PCI_VENDOR_ID_3COM, 0x1004, 0 },		/* 3C996SX */
	{ PCI_VENDOR_ID_3COM, 0x1007, PHY_ID_BCM5701 }, /* 3C1000T */
	{ PCI_VENDOR_ID_3COM, 0x1008, PHY_ID_BCM5701 }, /* 3C940BR01 */

	/* DELL boards. */
	{ PCI_VENDOR_ID_DELL, 0x00d1, PHY_ID_BCM5401 }, /* VIPER */
	{ PCI_VENDOR_ID_DELL, 0x0106, PHY_ID_BCM5401 }, /* JAGUAR */
	{ PCI_VENDOR_ID_DELL, 0x0109, PHY_ID_BCM5411 }, /* MERLOT */
	{ PCI_VENDOR_ID_DELL, 0x010a, PHY_ID_BCM5411 }, /* SLIM_MERLOT */

	/* Compaq boards. */
	{ PCI_VENDOR_ID_COMPAQ, 0x007c, PHY_ID_BCM5701 }, /* BANSHEE */
	{ PCI_VENDOR_ID_COMPAQ, 0x009a, PHY_ID_BCM5701 }, /* BANSHEE_2 */
	{ PCI_VENDOR_ID_COMPAQ, 0x007d, 0 },		  /* CHANGELING */
	{ PCI_VENDOR_ID_COMPAQ, 0x0085, PHY_ID_BCM5701 }, /* NC7780 */
	{ PCI_VENDOR_ID_COMPAQ, 0x0099, PHY_ID_BCM5701 }, /* NC7780_2 */

	/* IBM boards. */
	{ PCI_VENDOR_ID_IBM, 0x0281, 0 } /* IBM??? */
};

static inline struct subsys_tbl_ent *lookup_by_subsys(struct tg3 *tp)
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

static void __devinit tg3_get_eeprom_hw_cfg(struct tg3 *tp)
{
	u32 val;
	u16 pmcsr;

	/* On some early chips the SRAM cannot be accessed in D3hot state,
	 * so need make sure we're in D0.
	 */
	pci_read_config_word(tp->pdev, tp->pm_cap + PCI_PM_CTRL, &pmcsr);
	pmcsr &= ~PCI_PM_CTRL_STATE_MASK;
	pci_write_config_word(tp->pdev, tp->pm_cap + PCI_PM_CTRL, pmcsr);
	msleep(1);

	/* Make sure register accesses (indirect or otherwise)
	 * will function correctly.
	 */
	pci_write_config_dword(tp->pdev, TG3PCI_MISC_HOST_CTRL,
			       tp->misc_host_ctrl);

	/* The memory arbiter has to be enabled in order for SRAM accesses
	 * to succeed.  Normally on powerup the tg3 chip firmware will make
	 * sure it is enabled, but other entities such as system netboot
	 * code might disable it.
	 */
	val = tr32(MEMARB_MODE);
	tw32(MEMARB_MODE, val | MEMARB_MODE_ENABLE);

	tp->phy_id = PHY_ID_INVALID;
	tp->led_ctrl = LED_CTRL_MODE_PHY_1;

	/* Assume an onboard device and WOL capable by default.  */
	tp->tg3_flags |= TG3_FLAG_EEPROM_WRITE_PROT | TG3_FLAG_WOL_CAP;

	if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5906) {
		if (!(tr32(PCIE_TRANSACTION_CFG) & PCIE_TRANS_CFG_LOM)) {
			tp->tg3_flags &= ~TG3_FLAG_EEPROM_WRITE_PROT;
			tp->tg3_flags2 |= TG3_FLG2_IS_NIC;
		}
		val = tr32(VCPU_CFGSHDW);
		if (val & VCPU_CFGSHDW_ASPM_DBNC)
			tp->tg3_flags |= TG3_FLAG_ASPM_WORKAROUND;
		if ((val & VCPU_CFGSHDW_WOL_ENABLE) &&
		    (val & VCPU_CFGSHDW_WOL_MAGPKT))
			tp->tg3_flags |= TG3_FLAG_WOL_ENABLE;
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
		if ((GET_ASIC_REV(tp->pci_chip_rev_id) != ASIC_REV_5700) &&
		    (GET_ASIC_REV(tp->pci_chip_rev_id) != ASIC_REV_5701) &&
		    (GET_ASIC_REV(tp->pci_chip_rev_id) != ASIC_REV_5703) &&
		    (ver > 0) && (ver < 0x100))
			tg3_read_mem(tp, NIC_SRAM_DATA_CFG_2, &cfg2);

		if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5785)
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
			if (tp->tg3_flags2 & TG3_FLG2_5780_CLASS)
				tp->tg3_flags2 |= TG3_FLG2_MII_SERDES;
			else
				tp->tg3_flags2 |= TG3_FLG2_PHY_SERDES;
		}

		if (tp->tg3_flags2 & TG3_FLG2_5750_PLUS)
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
			if (GET_ASIC_REV(tp->pci_chip_rev_id) ==
			    ASIC_REV_5700 ||
			    GET_ASIC_REV(tp->pci_chip_rev_id) ==
			    ASIC_REV_5701)
				tp->led_ctrl = LED_CTRL_MODE_PHY_1;

			break;

		case SHASTA_EXT_LED_SHARED:
			tp->led_ctrl = LED_CTRL_MODE_SHARED;
			if (tp->pci_chip_rev_id != CHIPREV_ID_5750_A0 &&
			    tp->pci_chip_rev_id != CHIPREV_ID_5750_A1)
				tp->led_ctrl |= (LED_CTRL_MODE_PHY_1 |
						 LED_CTRL_MODE_PHY_2);
			break;

		case SHASTA_EXT_LED_MAC:
			tp->led_ctrl = LED_CTRL_MODE_SHASTA_MAC;
			break;

		case SHASTA_EXT_LED_COMBO:
			tp->led_ctrl = LED_CTRL_MODE_COMBO;
			if (tp->pci_chip_rev_id != CHIPREV_ID_5750_A0)
				tp->led_ctrl |= (LED_CTRL_MODE_PHY_1 |
						 LED_CTRL_MODE_PHY_2);
			break;

		}

		if ((GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5700 ||
		     GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5701) &&
		    tp->pdev->subsystem_vendor == PCI_VENDOR_ID_DELL)
			tp->led_ctrl = LED_CTRL_MODE_PHY_2;

		if (GET_CHIP_REV(tp->pci_chip_rev_id) == CHIPREV_5784_AX)
			tp->led_ctrl = LED_CTRL_MODE_PHY_1;

		if (nic_cfg & NIC_SRAM_DATA_CFG_EEPROM_WP) {
			tp->tg3_flags |= TG3_FLAG_EEPROM_WRITE_PROT;
			if ((tp->pdev->subsystem_vendor ==
			     PCI_VENDOR_ID_ARIMA) &&
			    (tp->pdev->subsystem_device == 0x205a ||
			     tp->pdev->subsystem_device == 0x2063))
				tp->tg3_flags &= ~TG3_FLAG_EEPROM_WRITE_PROT;
		} else {
			tp->tg3_flags &= ~TG3_FLAG_EEPROM_WRITE_PROT;
			tp->tg3_flags2 |= TG3_FLG2_IS_NIC;
		}

		if (nic_cfg & NIC_SRAM_DATA_CFG_ASF_ENABLE) {
			tp->tg3_flags |= TG3_FLAG_ENABLE_ASF;
			if (tp->tg3_flags2 & TG3_FLG2_5750_PLUS)
				tp->tg3_flags2 |= TG3_FLG2_ASF_NEW_HANDSHAKE;
		}

		if ((nic_cfg & NIC_SRAM_DATA_CFG_APE_ENABLE) &&
			(tp->tg3_flags2 & TG3_FLG2_5750_PLUS))
			tp->tg3_flags3 |= TG3_FLG3_ENABLE_APE;

		if (tp->tg3_flags2 & TG3_FLG2_ANY_SERDES &&
		    !(nic_cfg & NIC_SRAM_DATA_CFG_FIBER_WOL))
			tp->tg3_flags &= ~TG3_FLAG_WOL_CAP;

		if ((tp->tg3_flags & TG3_FLAG_WOL_CAP) &&
		    (nic_cfg & NIC_SRAM_DATA_CFG_WOL_ENABLE))
			tp->tg3_flags |= TG3_FLAG_WOL_ENABLE;

		if (cfg2 & (1 << 17))
			tp->tg3_flags2 |= TG3_FLG2_CAPACITIVE_COUPLING;

		/* serdes signal pre-emphasis in register 0x590 set by */
		/* bootcode if bit 18 is set */
		if (cfg2 & (1 << 18))
			tp->tg3_flags2 |= TG3_FLG2_SERDES_PREEMPHASIS;

		if (((GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5784 &&
		      GET_CHIP_REV(tp->pci_chip_rev_id) != CHIPREV_5784_AX)) &&
		    (cfg2 & NIC_SRAM_DATA_CFG_2_APD_EN))
			tp->tg3_flags3 |= TG3_FLG3_PHY_ENABLE_APD;

		if (tp->tg3_flags2 & TG3_FLG2_PCI_EXPRESS) {
			u32 cfg3;

			tg3_read_mem(tp, NIC_SRAM_DATA_CFG_3, &cfg3);
			if (cfg3 & NIC_SRAM_ASPM_DEBOUNCE)
				tp->tg3_flags |= TG3_FLAG_ASPM_WORKAROUND;
		}

		if (cfg4 & NIC_SRAM_RGMII_STD_IBND_DISABLE)
			tp->tg3_flags3 |= TG3_FLG3_RGMII_STD_IBND_DISABLE;
		if (cfg4 & NIC_SRAM_RGMII_EXT_IBND_RX_EN)
			tp->tg3_flags3 |= TG3_FLG3_RGMII_EXT_IBND_RX_EN;
		if (cfg4 & NIC_SRAM_RGMII_EXT_IBND_TX_EN)
			tp->tg3_flags3 |= TG3_FLG3_RGMII_EXT_IBND_TX_EN;
	}
done:
	device_init_wakeup(&tp->pdev->dev, tp->tg3_flags & TG3_FLAG_WOL_CAP);
	device_set_wakeup_enable(&tp->pdev->dev,
				 tp->tg3_flags & TG3_FLAG_WOL_ENABLE);
}

static int __devinit tg3_issue_otp_command(struct tg3 *tp, u32 cmd)
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
static u32 __devinit tg3_read_otp_phycfg(struct tg3 *tp)
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

static int __devinit tg3_phy_probe(struct tg3 *tp)
{
	u32 hw_phy_id_1, hw_phy_id_2;
	u32 hw_phy_id, hw_phy_id_masked;
	int err;

	if (tp->tg3_flags3 & TG3_FLG3_USE_PHYLIB)
		return tg3_phy_init(tp);

	/* Reading the PHY ID register can conflict with ASF
	 * firmware access to the PHY hardware.
	 */
	err = 0;
	if ((tp->tg3_flags & TG3_FLAG_ENABLE_ASF) ||
	    (tp->tg3_flags3 & TG3_FLG3_ENABLE_APE)) {
		hw_phy_id = hw_phy_id_masked = PHY_ID_INVALID;
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

		hw_phy_id_masked = hw_phy_id & PHY_ID_MASK;
	}

	if (!err && KNOWN_PHY_ID(hw_phy_id_masked)) {
		tp->phy_id = hw_phy_id;
		if (hw_phy_id_masked == PHY_ID_BCM8002)
			tp->tg3_flags2 |= TG3_FLG2_PHY_SERDES;
		else
			tp->tg3_flags2 &= ~TG3_FLG2_PHY_SERDES;
	} else {
		if (tp->phy_id != PHY_ID_INVALID) {
			/* Do nothing, phy ID already set up in
			 * tg3_get_eeprom_hw_cfg().
			 */
		} else {
			struct subsys_tbl_ent *p;

			/* No eeprom signature?  Try the hardcoded
			 * subsys device table.
			 */
			p = lookup_by_subsys(tp);
			if (!p)
				return -ENODEV;

			tp->phy_id = p->phy_id;
			if (!tp->phy_id ||
			    tp->phy_id == PHY_ID_BCM8002)
				tp->tg3_flags2 |= TG3_FLG2_PHY_SERDES;
		}
	}

	if (!(tp->tg3_flags2 & TG3_FLG2_ANY_SERDES) &&
	    !(tp->tg3_flags3 & TG3_FLG3_ENABLE_APE) &&
	    !(tp->tg3_flags & TG3_FLAG_ENABLE_ASF)) {
		u32 bmsr, adv_reg, tg3_ctrl, mask;

		tg3_readphy(tp, MII_BMSR, &bmsr);
		if (!tg3_readphy(tp, MII_BMSR, &bmsr) &&
		    (bmsr & BMSR_LSTATUS))
			goto skip_phy_reset;

		err = tg3_phy_reset(tp);
		if (err)
			return err;

		adv_reg = (ADVERTISE_10HALF | ADVERTISE_10FULL |
			   ADVERTISE_100HALF | ADVERTISE_100FULL |
			   ADVERTISE_CSMA | ADVERTISE_PAUSE_CAP);
		tg3_ctrl = 0;
		if (!(tp->tg3_flags & TG3_FLAG_10_100_ONLY)) {
			tg3_ctrl = (MII_TG3_CTRL_ADV_1000_HALF |
				    MII_TG3_CTRL_ADV_1000_FULL);
			if (tp->pci_chip_rev_id == CHIPREV_ID_5701_A0 ||
			    tp->pci_chip_rev_id == CHIPREV_ID_5701_B0)
				tg3_ctrl |= (MII_TG3_CTRL_AS_MASTER |
					     MII_TG3_CTRL_ENABLE_AS_MASTER);
		}

		mask = (ADVERTISED_10baseT_Half | ADVERTISED_10baseT_Full |
			ADVERTISED_100baseT_Half | ADVERTISED_100baseT_Full |
			ADVERTISED_1000baseT_Half | ADVERTISED_1000baseT_Full);
		if (!tg3_copper_is_advertising_all(tp, mask)) {
			tg3_writephy(tp, MII_ADVERTISE, adv_reg);

			if (!(tp->tg3_flags & TG3_FLAG_10_100_ONLY))
				tg3_writephy(tp, MII_TG3_CTRL, tg3_ctrl);

			tg3_writephy(tp, MII_BMCR,
				     BMCR_ANENABLE | BMCR_ANRESTART);
		}
		tg3_phy_set_wirespeed(tp);

		tg3_writephy(tp, MII_ADVERTISE, adv_reg);
		if (!(tp->tg3_flags & TG3_FLAG_10_100_ONLY))
			tg3_writephy(tp, MII_TG3_CTRL, tg3_ctrl);
	}

skip_phy_reset:
	if ((tp->phy_id & PHY_ID_MASK) == PHY_ID_BCM5401) {
		err = tg3_init_5401phy_dsp(tp);
		if (err)
			return err;
	}

	if (!err && ((tp->phy_id & PHY_ID_MASK) == PHY_ID_BCM5401)) {
		err = tg3_init_5401phy_dsp(tp);
	}

	if (tp->tg3_flags2 & TG3_FLG2_ANY_SERDES)
		tp->link_config.advertising =
			(ADVERTISED_1000baseT_Half |
			 ADVERTISED_1000baseT_Full |
			 ADVERTISED_Autoneg |
			 ADVERTISED_FIBRE);
	if (tp->tg3_flags & TG3_FLAG_10_100_ONLY)
		tp->link_config.advertising &=
			~(ADVERTISED_1000baseT_Half |
			  ADVERTISED_1000baseT_Full);

	return err;
}

static void __devinit tg3_read_partno(struct tg3 *tp)
{
	unsigned char vpd_data[256];   /* in little-endian format */
	unsigned int i;
	u32 magic;

	if ((tp->tg3_flags3 & TG3_FLG3_NO_NVRAM) ||
	    tg3_nvram_read(tp, 0x0, &magic))
		goto out_not_found;

	if (magic == TG3_EEPROM_MAGIC) {
		for (i = 0; i < 256; i += 4) {
			u32 tmp;

			/* The data is in little-endian format in NVRAM.
			 * Use the big-endian read routines to preserve
			 * the byte order as it exists in NVRAM.
			 */
			if (tg3_nvram_read_be32(tp, 0x100 + i, &tmp))
				goto out_not_found;

			memcpy(&vpd_data[i], &tmp, sizeof(tmp));
		}
	} else {
		int vpd_cap;

		vpd_cap = pci_find_capability(tp->pdev, PCI_CAP_ID_VPD);
		for (i = 0; i < 256; i += 4) {
			u32 tmp, j = 0;
			__le32 v;
			u16 tmp16;

			pci_write_config_word(tp->pdev, vpd_cap + PCI_VPD_ADDR,
					      i);
			while (j++ < 100) {
				pci_read_config_word(tp->pdev, vpd_cap +
						     PCI_VPD_ADDR, &tmp16);
				if (tmp16 & 0x8000)
					break;
				msleep(1);
			}
			if (!(tmp16 & 0x8000))
				goto out_not_found;

			pci_read_config_dword(tp->pdev, vpd_cap + PCI_VPD_DATA,
					      &tmp);
			v = cpu_to_le32(tmp);
			memcpy(&vpd_data[i], &v, sizeof(v));
		}
	}

	/* Now parse and find the part number. */
	for (i = 0; i < 254; ) {
		unsigned char val = vpd_data[i];
		unsigned int block_end;

		if (val == 0x82 || val == 0x91) {
			i = (i + 3 +
			     (vpd_data[i + 1] +
			      (vpd_data[i + 2] << 8)));
			continue;
		}

		if (val != 0x90)
			goto out_not_found;

		block_end = (i + 3 +
			     (vpd_data[i + 1] +
			      (vpd_data[i + 2] << 8)));
		i += 3;

		if (block_end > 256)
			goto out_not_found;

		while (i < (block_end - 2)) {
			if (vpd_data[i + 0] == 'P' &&
			    vpd_data[i + 1] == 'N') {
				int partno_len = vpd_data[i + 2];

				i += 3;
				if (partno_len > 24 || (partno_len + i) > 256)
					goto out_not_found;

				memcpy(tp->board_part_number,
				       &vpd_data[i], partno_len);

				/* Success. */
				return;
			}
			i += 3 + vpd_data[i + 2];
		}

		/* Part number not found. */
		goto out_not_found;
	}

out_not_found:
	if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5906)
		strcpy(tp->board_part_number, "BCM95906");
	else if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_57780 &&
		 tp->pdev->device == TG3PCI_DEVICE_TIGON3_57780)
		strcpy(tp->board_part_number, "BCM57780");
	else if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_57780 &&
		 tp->pdev->device == TG3PCI_DEVICE_TIGON3_57760)
		strcpy(tp->board_part_number, "BCM57760");
	else if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_57780 &&
		 tp->pdev->device == TG3PCI_DEVICE_TIGON3_57790)
		strcpy(tp->board_part_number, "BCM57790");
	else if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_57780 &&
		 tp->pdev->device == TG3PCI_DEVICE_TIGON3_57788)
		strcpy(tp->board_part_number, "BCM57788");
	else
		strcpy(tp->board_part_number, "none");
}

static int __devinit tg3_fw_img_is_valid(struct tg3 *tp, u32 offset)
{
	u32 val;

	if (tg3_nvram_read(tp, offset, &val) ||
	    (val & 0xfc000000) != 0x0c000000 ||
	    tg3_nvram_read(tp, offset + 4, &val) ||
	    val != 0)
		return 0;

	return 1;
}

static void __devinit tg3_read_bc_ver(struct tg3 *tp)
{
	u32 val, offset, start, ver_offset;
	int i;
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

	if (newver) {
		if (tg3_nvram_read(tp, offset + 8, &ver_offset))
			return;

		offset = offset + ver_offset - start;
		for (i = 0; i < 16; i += 4) {
			__be32 v;
			if (tg3_nvram_read_be32(tp, offset + i, &v))
				return;

			memcpy(tp->fw_ver + i, &v, sizeof(v));
		}
	} else {
		u32 major, minor;

		if (tg3_nvram_read(tp, TG3_NVM_PTREV_BCVER, &ver_offset))
			return;

		major = (ver_offset & TG3_NVM_BCVER_MAJMSK) >>
			TG3_NVM_BCVER_MAJSFT;
		minor = ver_offset & TG3_NVM_BCVER_MINMSK;
		snprintf(&tp->fw_ver[0], 32, "v%d.%02d", major, minor);
	}
}

static void __devinit tg3_read_hwsb_ver(struct tg3 *tp)
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

static void __devinit tg3_read_sb_ver(struct tg3 *tp, u32 val)
{
	u32 offset, major, minor, build;

	tp->fw_ver[0] = 's';
	tp->fw_ver[1] = 'b';
	tp->fw_ver[2] = '\0';

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

	snprintf(&tp->fw_ver[2], 30, " v%d.%02d", major, minor);

	if (build > 0) {
		tp->fw_ver[8] = 'a' + build - 1;
		tp->fw_ver[9] = '\0';
	}
}

static void __devinit tg3_read_mgmtfw_ver(struct tg3 *tp)
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

	if (!(tp->tg3_flags2 & TG3_FLG2_5705_PLUS))
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

static void __devinit tg3_read_dash_ver(struct tg3 *tp)
{
	int vlen;
	u32 apedata;

	if (!(tp->tg3_flags3 & TG3_FLG3_ENABLE_APE) ||
	    !(tp->tg3_flags  & TG3_FLAG_ENABLE_ASF))
		return;

	apedata = tg3_ape_read32(tp, TG3_APE_SEG_SIG);
	if (apedata != APE_SEG_SIG_MAGIC)
		return;

	apedata = tg3_ape_read32(tp, TG3_APE_FW_STATUS);
	if (!(apedata & APE_FW_STATUS_READY))
		return;

	apedata = tg3_ape_read32(tp, TG3_APE_FW_VERSION);

	vlen = strlen(tp->fw_ver);

	snprintf(&tp->fw_ver[vlen], TG3_VER_SIZE - vlen, " DASH v%d.%d.%d.%d",
		 (apedata & APE_FW_VERSION_MAJMSK) >> APE_FW_VERSION_MAJSFT,
		 (apedata & APE_FW_VERSION_MINMSK) >> APE_FW_VERSION_MINSFT,
		 (apedata & APE_FW_VERSION_REVMSK) >> APE_FW_VERSION_REVSFT,
		 (apedata & APE_FW_VERSION_BLDMSK));
}

static void __devinit tg3_read_fw_ver(struct tg3 *tp)
{
	u32 val;

	if (tp->tg3_flags3 & TG3_FLG3_NO_NVRAM) {
		tp->fw_ver[0] = 's';
		tp->fw_ver[1] = 'b';
		tp->fw_ver[2] = '\0';

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
	else
		return;

	if (!(tp->tg3_flags & TG3_FLAG_ENABLE_ASF) ||
	     (tp->tg3_flags3 & TG3_FLG3_ENABLE_APE))
		return;

	tg3_read_mgmtfw_ver(tp);

	tp->fw_ver[TG3_VER_SIZE - 1] = 0;
}

static struct pci_dev * __devinit tg3_find_peer(struct tg3 *);

static int __devinit tg3_get_invariants(struct tg3 *tp)
{
	static struct pci_device_id write_reorder_chipsets[] = {
		{ PCI_DEVICE(PCI_VENDOR_ID_AMD,
		             PCI_DEVICE_ID_AMD_FE_GATE_700C) },
		{ PCI_DEVICE(PCI_VENDOR_ID_AMD,
		             PCI_DEVICE_ID_AMD_8131_BRIDGE) },
		{ PCI_DEVICE(PCI_VENDOR_ID_VIA,
			     PCI_DEVICE_ID_VIA_8385_0) },
		{ },
	};
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

	/* It is absolutely critical that TG3PCI_MISC_HOST_CTRL
	 * has the register indirect write enable bit set before
	 * we try to access any of the MMIO registers.  It is also
	 * critical that the PCI-X hw workaround situation is decided
	 * before that as well.
	 */
	pci_read_config_dword(tp->pdev, TG3PCI_MISC_HOST_CTRL,
			      &misc_ctrl_reg);

	tp->pci_chip_rev_id = (misc_ctrl_reg >>
			       MISC_HOST_CTRL_CHIPREV_SHIFT);
	if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_USE_PROD_ID_REG) {
		u32 prod_id_asic_rev;

		if (tp->pdev->device == TG3PCI_DEVICE_TIGON3_5717C ||
		    tp->pdev->device == TG3PCI_DEVICE_TIGON3_5717S ||
		    tp->pdev->device == TG3PCI_DEVICE_TIGON3_5718C ||
		    tp->pdev->device == TG3PCI_DEVICE_TIGON3_5718S)
			pci_read_config_dword(tp->pdev,
					      TG3PCI_GEN2_PRODID_ASICREV,
					      &prod_id_asic_rev);
		else
			pci_read_config_dword(tp->pdev, TG3PCI_PRODID_ASICREV,
					      &prod_id_asic_rev);

		tp->pci_chip_rev_id = prod_id_asic_rev;
	}

	/* Wrong chip ID in 5752 A0. This code can be removed later
	 * as A0 is not in production.
	 */
	if (tp->pci_chip_rev_id == CHIPREV_ID_5752_A0_HW)
		tp->pci_chip_rev_id = CHIPREV_ID_5752_A0;

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
	if ((tp->pci_chip_rev_id == CHIPREV_ID_5703_A1) ||
	    (tp->pci_chip_rev_id == CHIPREV_ID_5703_A2)) {
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

				tp->tg3_flags2 |= TG3_FLG2_ICH_WORKAROUND;
				pci_dev_put(bridge);
				break;
			}
		}
	}

	if ((GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5701)) {
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
			    (bridge->subordinate->subordinate >=
			     tp->pdev->bus->number)) {
				tp->tg3_flags3 |= TG3_FLG3_5701_DMA_BUG;
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
	if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5780 ||
	    GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5714) {
		tp->tg3_flags2 |= TG3_FLG2_5780_CLASS;
		tp->tg3_flags |= TG3_FLAG_40BIT_DMA_BUG;
		tp->msi_cap = pci_find_capability(tp->pdev, PCI_CAP_ID_MSI);
	}
	else {
		struct pci_dev *bridge = NULL;

		do {
			bridge = pci_get_device(PCI_VENDOR_ID_SERVERWORKS,
						PCI_DEVICE_ID_SERVERWORKS_EPB,
						bridge);
			if (bridge && bridge->subordinate &&
			    (bridge->subordinate->number <=
			     tp->pdev->bus->number) &&
			    (bridge->subordinate->subordinate >=
			     tp->pdev->bus->number)) {
				tp->tg3_flags |= TG3_FLAG_40BIT_DMA_BUG;
				pci_dev_put(bridge);
				break;
			}
		} while (bridge);
	}

	/* Initialize misc host control in PCI block. */
	tp->misc_host_ctrl |= (misc_ctrl_reg &
			       MISC_HOST_CTRL_CHIPREV);
	pci_write_config_dword(tp->pdev, TG3PCI_MISC_HOST_CTRL,
			       tp->misc_host_ctrl);

	if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5704 ||
	    GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5714 ||
	    GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5717)
		tp->pdev_peer = tg3_find_peer(tp);

	/* Intentionally exclude ASIC_REV_5906 */
	if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5755 ||
	    GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5787 ||
	    GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5784 ||
	    GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5761 ||
	    GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5785 ||
	    GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_57780 ||
	    GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5717)
		tp->tg3_flags3 |= TG3_FLG3_5755_PLUS;

	if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5750 ||
	    GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5752 ||
	    GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5906 ||
	    (tp->tg3_flags3 & TG3_FLG3_5755_PLUS) ||
	    (tp->tg3_flags2 & TG3_FLG2_5780_CLASS))
		tp->tg3_flags2 |= TG3_FLG2_5750_PLUS;

	if ((GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5705) ||
	    (tp->tg3_flags2 & TG3_FLG2_5750_PLUS))
		tp->tg3_flags2 |= TG3_FLG2_5705_PLUS;

	/* 5700 B0 chips do not support checksumming correctly due
	 * to hardware bugs.
	 */
	if (tp->pci_chip_rev_id == CHIPREV_ID_5700_B0)
		tp->tg3_flags |= TG3_FLAG_BROKEN_CHECKSUMS;
	else {
		tp->tg3_flags |= TG3_FLAG_RX_CHECKSUMS;
		tp->dev->features |= NETIF_F_IP_CSUM | NETIF_F_SG;
		if (tp->tg3_flags3 & TG3_FLG3_5755_PLUS)
			tp->dev->features |= NETIF_F_IPV6_CSUM;
	}

	if (tp->tg3_flags2 & TG3_FLG2_5750_PLUS) {
		tp->tg3_flags |= TG3_FLAG_SUPPORT_MSI;
		if (GET_CHIP_REV(tp->pci_chip_rev_id) == CHIPREV_5750_AX ||
		    GET_CHIP_REV(tp->pci_chip_rev_id) == CHIPREV_5750_BX ||
		    (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5714 &&
		     tp->pci_chip_rev_id <= CHIPREV_ID_5714_A2 &&
		     tp->pdev_peer == tp->pdev))
			tp->tg3_flags &= ~TG3_FLAG_SUPPORT_MSI;

		if ((tp->tg3_flags3 & TG3_FLG3_5755_PLUS) ||
		    GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5906) {
			tp->tg3_flags2 |= TG3_FLG2_HW_TSO_2;
			tp->tg3_flags2 |= TG3_FLG2_1SHOT_MSI;
		} else {
			tp->tg3_flags2 |= TG3_FLG2_HW_TSO_1 | TG3_FLG2_TSO_BUG;
			if (GET_ASIC_REV(tp->pci_chip_rev_id) ==
				ASIC_REV_5750 &&
	     		    tp->pci_chip_rev_id >= CHIPREV_ID_5750_C2)
				tp->tg3_flags2 &= ~TG3_FLG2_TSO_BUG;
		}
	}

	tp->irq_max = 1;

	if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5717) {
		tp->tg3_flags |= TG3_FLAG_SUPPORT_MSIX;
		tp->irq_max = TG3_IRQ_MAX_VECS;
	}

	if (!(tp->tg3_flags3 & TG3_FLG3_5755_PLUS)) {
		if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5906)
			tp->tg3_flags3 |= TG3_FLG3_SHORT_DMA_BUG;
		else {
			tp->tg3_flags3 |= TG3_FLG3_4G_DMA_BNDRY_BUG;
			tp->tg3_flags3 |= TG3_FLG3_40BIT_DMA_LIMIT_BUG;
		}
	}

	if (!(tp->tg3_flags2 & TG3_FLG2_5705_PLUS) ||
	     (tp->tg3_flags2 & TG3_FLG2_5780_CLASS) ||
	    GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5717)
		tp->tg3_flags |= TG3_FLAG_JUMBO_CAPABLE;

	pci_read_config_dword(tp->pdev, TG3PCI_PCISTATE,
			      &pci_state_reg);

	tp->pcie_cap = pci_find_capability(tp->pdev, PCI_CAP_ID_EXP);
	if (tp->pcie_cap != 0) {
		u16 lnkctl;

		tp->tg3_flags2 |= TG3_FLG2_PCI_EXPRESS;

		pcie_set_readrq(tp->pdev, 4096);

		pci_read_config_word(tp->pdev,
				     tp->pcie_cap + PCI_EXP_LNKCTL,
				     &lnkctl);
		if (lnkctl & PCI_EXP_LNKCTL_CLKREQ_EN) {
			if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5906)
				tp->tg3_flags2 &= ~TG3_FLG2_HW_TSO_2;
			if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5784 ||
			    GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5761 ||
			    tp->pci_chip_rev_id == CHIPREV_ID_57780_A0 ||
			    tp->pci_chip_rev_id == CHIPREV_ID_57780_A1)
				tp->tg3_flags3 |= TG3_FLG3_CLKREQ_BUG;
		}
	} else if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5785) {
		tp->tg3_flags2 |= TG3_FLG2_PCI_EXPRESS;
	} else if (!(tp->tg3_flags2 & TG3_FLG2_5705_PLUS) ||
		   (tp->tg3_flags2 & TG3_FLG2_5780_CLASS)) {
		tp->pcix_cap = pci_find_capability(tp->pdev, PCI_CAP_ID_PCIX);
		if (!tp->pcix_cap) {
			printk(KERN_ERR PFX "Cannot find PCI-X "
					    "capability, aborting.\n");
			return -EIO;
		}

		if (!(pci_state_reg & PCISTATE_CONV_PCI_MODE))
			tp->tg3_flags |= TG3_FLAG_PCIX_MODE;
	}

	/* If we have an AMD 762 or VIA K8T800 chipset, write
	 * reordering to the mailbox registers done by the host
	 * controller can cause major troubles.  We read back from
	 * every mailbox register write to force the writes to be
	 * posted to the chip in order.
	 */
	if (pci_dev_present(write_reorder_chipsets) &&
	    !(tp->tg3_flags2 & TG3_FLG2_PCI_EXPRESS))
		tp->tg3_flags |= TG3_FLAG_MBOX_WRITE_REORDER;

	pci_read_config_byte(tp->pdev, PCI_CACHE_LINE_SIZE,
			     &tp->pci_cacheline_sz);
	pci_read_config_byte(tp->pdev, PCI_LATENCY_TIMER,
			     &tp->pci_lat_timer);
	if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5703 &&
	    tp->pci_lat_timer < 64) {
		tp->pci_lat_timer = 64;
		pci_write_config_byte(tp->pdev, PCI_LATENCY_TIMER,
				      tp->pci_lat_timer);
	}

	if (GET_CHIP_REV(tp->pci_chip_rev_id) == CHIPREV_5700_BX) {
		/* 5700 BX chips need to have their TX producer index
		 * mailboxes written twice to workaround a bug.
		 */
		tp->tg3_flags |= TG3_FLAG_TXD_MBOX_HWBUG;

		/* If we are in PCI-X mode, enable register write workaround.
		 *
		 * The workaround is to use indirect register accesses
		 * for all chip writes not to mailbox registers.
		 */
		if (tp->tg3_flags & TG3_FLAG_PCIX_MODE) {
			u32 pm_reg;

			tp->tg3_flags |= TG3_FLAG_PCIX_TARGET_HWBUG;

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
		tp->tg3_flags |= TG3_FLAG_PCI_HIGH_SPEED;
	if ((pci_state_reg & PCISTATE_BUS_32BIT) != 0)
		tp->tg3_flags |= TG3_FLAG_PCI_32BIT;

	/* Chip-specific fixup from Broadcom driver */
	if ((tp->pci_chip_rev_id == CHIPREV_ID_5704_A0) &&
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
	if (tp->tg3_flags & TG3_FLAG_PCIX_TARGET_HWBUG)
		tp->write32 = tg3_write_indirect_reg32;
	else if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5701 ||
		 ((tp->tg3_flags2 & TG3_FLG2_PCI_EXPRESS) &&
		  tp->pci_chip_rev_id == CHIPREV_ID_5750_A0)) {
		/*
		 * Back to back register writes can cause problems on these
		 * chips, the workaround is to read back all reg writes
		 * except those to mailbox regs.
		 *
		 * See tg3_write_indirect_reg32().
		 */
		tp->write32 = tg3_write_flush_reg32;
	}

	if ((tp->tg3_flags & TG3_FLAG_TXD_MBOX_HWBUG) ||
	    (tp->tg3_flags & TG3_FLAG_MBOX_WRITE_REORDER)) {
		tp->write32_tx_mbox = tg3_write32_tx_mbox;
		if (tp->tg3_flags & TG3_FLAG_MBOX_WRITE_REORDER)
			tp->write32_rx_mbox = tg3_write_flush_reg32;
	}

	if (tp->tg3_flags2 & TG3_FLG2_ICH_WORKAROUND) {
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
	if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5906) {
		tp->read32_mbox = tg3_read32_mbox_5906;
		tp->write32_mbox = tg3_write32_mbox_5906;
		tp->write32_tx_mbox = tg3_write32_mbox_5906;
		tp->write32_rx_mbox = tg3_write32_mbox_5906;
	}

	if (tp->write32 == tg3_write_indirect_reg32 ||
	    ((tp->tg3_flags & TG3_FLAG_PCIX_MODE) &&
	     (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5700 ||
	      GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5701)))
		tp->tg3_flags |= TG3_FLAG_SRAM_USE_CONFIG;

	/* Get eeprom hw config before calling tg3_set_power_state().
	 * In particular, the TG3_FLG2_IS_NIC flag must be
	 * determined before calling tg3_set_power_state() so that
	 * we know whether or not to switch out of Vaux power.
	 * When the flag is set, it means that GPIO1 is used for eeprom
	 * write protect and also implies that it is a LOM where GPIOs
	 * are not used to switch power.
	 */
	tg3_get_eeprom_hw_cfg(tp);

	if (tp->tg3_flags3 & TG3_FLG3_ENABLE_APE) {
		/* Allow reads and writes to the
		 * APE register and memory space.
		 */
		pci_state_reg |= PCISTATE_ALLOW_APE_CTLSPC_WR |
				 PCISTATE_ALLOW_APE_SHMEM_WR;
		pci_write_config_dword(tp->pdev, TG3PCI_PCISTATE,
				       pci_state_reg);
	}

	if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5784 ||
	    GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5761 ||
	    GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5785 ||
	    GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_57780 ||
	    GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5717)
		tp->tg3_flags |= TG3_FLAG_CPMU_PRESENT;

	/* Set up tp->grc_local_ctrl before calling tg3_set_power_state().
	 * GPIO1 driven high will bring 5700's external PHY out of reset.
	 * It is also used as eeprom write protect on LOMs.
	 */
	tp->grc_local_ctrl = GRC_LCLCTRL_INT_ON_ATTN | GRC_LCLCTRL_AUTO_SEEPROM;
	if ((GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5700) ||
	    (tp->tg3_flags & TG3_FLAG_EEPROM_WRITE_PROT))
		tp->grc_local_ctrl |= (GRC_LCLCTRL_GPIO_OE1 |
				       GRC_LCLCTRL_GPIO_OUTPUT1);
	/* Unused GPIO3 must be driven as output on 5752 because there
	 * are no pull-up resistors on unused GPIO pins.
	 */
	else if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5752)
		tp->grc_local_ctrl |= GRC_LCLCTRL_GPIO_OE3;

	if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5755 ||
	    GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_57780)
		tp->grc_local_ctrl |= GRC_LCLCTRL_GPIO_UART_SEL;

	if (tp->pdev->device == PCI_DEVICE_ID_TIGON3_5761 ||
	    tp->pdev->device == TG3PCI_DEVICE_TIGON3_5761S) {
		/* Turn off the debug UART. */
		tp->grc_local_ctrl |= GRC_LCLCTRL_GPIO_UART_SEL;
		if (tp->tg3_flags2 & TG3_FLG2_IS_NIC)
			/* Keep VMain power. */
			tp->grc_local_ctrl |= GRC_LCLCTRL_GPIO_OE0 |
					      GRC_LCLCTRL_GPIO_OUTPUT0;
	}

	/* Force the chip into D0. */
	err = tg3_set_power_state(tp, PCI_D0);
	if (err) {
		printk(KERN_ERR PFX "(%s) transition to D0 failed\n",
		       pci_name(tp->pdev));
		return err;
	}

	/* Derive initial jumbo mode from MTU assigned in
	 * ether_setup() via the alloc_etherdev() call
	 */
	if (tp->dev->mtu > ETH_DATA_LEN &&
	    !(tp->tg3_flags2 & TG3_FLG2_5780_CLASS))
		tp->tg3_flags |= TG3_FLAG_JUMBO_RING_ENABLE;

	/* Determine WakeOnLan speed to use. */
	if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5700 ||
	    tp->pci_chip_rev_id == CHIPREV_ID_5701_A0 ||
	    tp->pci_chip_rev_id == CHIPREV_ID_5701_B0 ||
	    tp->pci_chip_rev_id == CHIPREV_ID_5701_B2) {
		tp->tg3_flags &= ~(TG3_FLAG_WOL_SPEED_100MB);
	} else {
		tp->tg3_flags |= TG3_FLAG_WOL_SPEED_100MB;
	}

	if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5906)
		tp->tg3_flags3 |= TG3_FLG3_PHY_IS_FET;

	/* A few boards don't want Ethernet@WireSpeed phy feature */
	if ((GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5700) ||
	    ((GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5705) &&
	     (tp->pci_chip_rev_id != CHIPREV_ID_5705_A0) &&
	     (tp->pci_chip_rev_id != CHIPREV_ID_5705_A1)) ||
	    (tp->tg3_flags3 & TG3_FLG3_PHY_IS_FET) ||
	    (tp->tg3_flags2 & TG3_FLG2_ANY_SERDES))
		tp->tg3_flags2 |= TG3_FLG2_NO_ETH_WIRE_SPEED;

	if (GET_CHIP_REV(tp->pci_chip_rev_id) == CHIPREV_5703_AX ||
	    GET_CHIP_REV(tp->pci_chip_rev_id) == CHIPREV_5704_AX)
		tp->tg3_flags2 |= TG3_FLG2_PHY_ADC_BUG;
	if (tp->pci_chip_rev_id == CHIPREV_ID_5704_A0)
		tp->tg3_flags2 |= TG3_FLG2_PHY_5704_A0_BUG;

	if ((tp->tg3_flags2 & TG3_FLG2_5705_PLUS) &&
	    !(tp->tg3_flags3 & TG3_FLG3_PHY_IS_FET) &&
	    GET_ASIC_REV(tp->pci_chip_rev_id) != ASIC_REV_5785 &&
	    GET_ASIC_REV(tp->pci_chip_rev_id) != ASIC_REV_57780 &&
	    GET_ASIC_REV(tp->pci_chip_rev_id) != ASIC_REV_5717) {
		if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5755 ||
		    GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5787 ||
		    GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5784 ||
		    GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5761) {
			if (tp->pdev->device != PCI_DEVICE_ID_TIGON3_5756 &&
			    tp->pdev->device != PCI_DEVICE_ID_TIGON3_5722)
				tp->tg3_flags2 |= TG3_FLG2_PHY_JITTER_BUG;
			if (tp->pdev->device == PCI_DEVICE_ID_TIGON3_5755M)
				tp->tg3_flags2 |= TG3_FLG2_PHY_ADJUST_TRIM;
		} else
			tp->tg3_flags2 |= TG3_FLG2_PHY_BER_BUG;
	}

	if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5784 &&
	    GET_CHIP_REV(tp->pci_chip_rev_id) != CHIPREV_5784_AX) {
		tp->phy_otp = tg3_read_otp_phycfg(tp);
		if (tp->phy_otp == 0)
			tp->phy_otp = TG3_OTP_DEFAULT;
	}

	if (tp->tg3_flags & TG3_FLAG_CPMU_PRESENT)
		tp->mi_mode = MAC_MI_MODE_500KHZ_CONST;
	else
		tp->mi_mode = MAC_MI_MODE_BASE;

	tp->coalesce_mode = 0;
	if (GET_CHIP_REV(tp->pci_chip_rev_id) != CHIPREV_5700_AX &&
	    GET_CHIP_REV(tp->pci_chip_rev_id) != CHIPREV_5700_BX)
		tp->coalesce_mode |= HOSTCC_MODE_32BYTE;

	if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5785 ||
	    GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_57780)
		tp->tg3_flags3 |= TG3_FLG3_USE_PHYLIB;

	if ((tp->pci_chip_rev_id == CHIPREV_ID_57780_A1 &&
	     tr32(RCVLPC_STATS_ENABLE) & RCVLPC_STATSENAB_ASF_FIX) ||
	    tp->pci_chip_rev_id == CHIPREV_ID_57780_A0)
		tp->tg3_flags3 |= TG3_FLG3_TOGGLE_10_100_L1PLLPD;

	err = tg3_mdio_init(tp);
	if (err)
		return err;

	/* Initialize data/descriptor byte/word swapping. */
	val = tr32(GRC_MODE);
	val &= GRC_MODE_HOST_STACKUP;
	tw32(GRC_MODE, val | tp->grc_mode);

	tg3_switch_clocks(tp);

	/* Clear this out for sanity. */
	tw32(TG3PCI_MEM_WIN_BASE_ADDR, 0);

	pci_read_config_dword(tp->pdev, TG3PCI_PCISTATE,
			      &pci_state_reg);
	if ((pci_state_reg & PCISTATE_CONV_PCI_MODE) == 0 &&
	    (tp->tg3_flags & TG3_FLAG_PCIX_TARGET_HWBUG) == 0) {
		u32 chiprevid = GET_CHIP_REV_ID(tp->misc_host_ctrl);

		if (chiprevid == CHIPREV_ID_5701_A0 ||
		    chiprevid == CHIPREV_ID_5701_B0 ||
		    chiprevid == CHIPREV_ID_5701_B2 ||
		    chiprevid == CHIPREV_ID_5701_B5) {
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
				tp->tg3_flags |= TG3_FLAG_PCIX_TARGET_HWBUG;
		}
	}

	udelay(50);
	tg3_nvram_init(tp);

	grc_misc_cfg = tr32(GRC_MISC_CFG);
	grc_misc_cfg &= GRC_MISC_CFG_BOARD_ID_MASK;

	if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5705 &&
	    (grc_misc_cfg == GRC_MISC_CFG_BOARD_ID_5788 ||
	     grc_misc_cfg == GRC_MISC_CFG_BOARD_ID_5788M))
		tp->tg3_flags2 |= TG3_FLG2_IS_5788;

	if (!(tp->tg3_flags2 & TG3_FLG2_IS_5788) &&
	    (GET_ASIC_REV(tp->pci_chip_rev_id) != ASIC_REV_5700))
		tp->tg3_flags |= TG3_FLAG_TAGGED_STATUS;
	if (tp->tg3_flags & TG3_FLAG_TAGGED_STATUS) {
		tp->coalesce_mode |= (HOSTCC_MODE_CLRTICK_RXBD |
				      HOSTCC_MODE_CLRTICK_TXBD);

		tp->misc_host_ctrl |= MISC_HOST_CTRL_TAGGED_STATUS;
		pci_write_config_dword(tp->pdev, TG3PCI_MISC_HOST_CTRL,
				       tp->misc_host_ctrl);
	}

	/* Preserve the APE MAC_MODE bits */
	if (tp->tg3_flags3 & TG3_FLG3_ENABLE_APE)
		tp->mac_mode = tr32(MAC_MODE) |
			       MAC_MODE_APE_TX_EN | MAC_MODE_APE_RX_EN;
	else
		tp->mac_mode = TG3_DEF_MAC_MODE;

	/* these are limited to 10/100 only */
	if ((GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5703 &&
	     (grc_misc_cfg == 0x8000 || grc_misc_cfg == 0x4000)) ||
	    (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5705 &&
	     tp->pdev->vendor == PCI_VENDOR_ID_BROADCOM &&
	     (tp->pdev->device == PCI_DEVICE_ID_TIGON3_5901 ||
	      tp->pdev->device == PCI_DEVICE_ID_TIGON3_5901_2 ||
	      tp->pdev->device == PCI_DEVICE_ID_TIGON3_5705F)) ||
	    (tp->pdev->vendor == PCI_VENDOR_ID_BROADCOM &&
	     (tp->pdev->device == PCI_DEVICE_ID_TIGON3_5751F ||
	      tp->pdev->device == PCI_DEVICE_ID_TIGON3_5753F ||
	      tp->pdev->device == PCI_DEVICE_ID_TIGON3_5787F)) ||
	    tp->pdev->device == TG3PCI_DEVICE_TIGON3_57790 ||
	    (tp->tg3_flags3 & TG3_FLG3_PHY_IS_FET))
		tp->tg3_flags |= TG3_FLAG_10_100_ONLY;

	err = tg3_phy_probe(tp);
	if (err) {
		printk(KERN_ERR PFX "(%s) phy probe failed, err %d\n",
		       pci_name(tp->pdev), err);
		/* ... but do not return immediately ... */
		tg3_mdio_fini(tp);
	}

	tg3_read_partno(tp);
	tg3_read_fw_ver(tp);

	if (tp->tg3_flags2 & TG3_FLG2_PHY_SERDES) {
		tp->tg3_flags &= ~TG3_FLAG_USE_MI_INTERRUPT;
	} else {
		if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5700)
			tp->tg3_flags |= TG3_FLAG_USE_MI_INTERRUPT;
		else
			tp->tg3_flags &= ~TG3_FLAG_USE_MI_INTERRUPT;
	}

	/* 5700 {AX,BX} chips have a broken status block link
	 * change bit implementation, so we must use the
	 * status register in those cases.
	 */
	if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5700)
		tp->tg3_flags |= TG3_FLAG_USE_LINKCHG_REG;
	else
		tp->tg3_flags &= ~TG3_FLAG_USE_LINKCHG_REG;

	/* The led_ctrl is set during tg3_phy_probe, here we might
	 * have to force the link status polling mechanism based
	 * upon subsystem IDs.
	 */
	if (tp->pdev->subsystem_vendor == PCI_VENDOR_ID_DELL &&
	    GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5701 &&
	    !(tp->tg3_flags2 & TG3_FLG2_PHY_SERDES)) {
		tp->tg3_flags |= (TG3_FLAG_USE_MI_INTERRUPT |
				  TG3_FLAG_USE_LINKCHG_REG);
	}

	/* For all SERDES we poll the MAC status register. */
	if (tp->tg3_flags2 & TG3_FLG2_PHY_SERDES)
		tp->tg3_flags |= TG3_FLAG_POLL_SERDES;
	else
		tp->tg3_flags &= ~TG3_FLAG_POLL_SERDES;

	tp->rx_offset = NET_IP_ALIGN;
	if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5701 &&
	    (tp->tg3_flags & TG3_FLAG_PCIX_MODE) != 0)
		tp->rx_offset = 0;

	tp->rx_std_max_post = TG3_RX_RING_SIZE;

	/* Increment the rx prod index on the rx std ring by at most
	 * 8 for these chips to workaround hw errata.
	 */
	if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5750 ||
	    GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5752 ||
	    GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5755)
		tp->rx_std_max_post = 8;

	if (tp->tg3_flags & TG3_FLAG_ASPM_WORKAROUND)
		tp->pwrmgmt_thresh = tr32(PCIE_PWR_MGMT_THRESH) &
				     PCIE_PWR_MGMT_L1_THRESH_MSK;

	return err;
}

#ifdef CONFIG_SPARC
static int __devinit tg3_get_macaddr_sparc(struct tg3 *tp)
{
	struct net_device *dev = tp->dev;
	struct pci_dev *pdev = tp->pdev;
	struct device_node *dp = pci_device_to_OF_node(pdev);
	const unsigned char *addr;
	int len;

	addr = of_get_property(dp, "local-mac-address", &len);
	if (addr && len == 6) {
		memcpy(dev->dev_addr, addr, 6);
		memcpy(dev->perm_addr, dev->dev_addr, 6);
		return 0;
	}
	return -ENODEV;
}

static int __devinit tg3_get_default_macaddr_sparc(struct tg3 *tp)
{
	struct net_device *dev = tp->dev;

	memcpy(dev->dev_addr, idprom->id_ethaddr, 6);
	memcpy(dev->perm_addr, idprom->id_ethaddr, 6);
	return 0;
}
#endif

static int __devinit tg3_get_device_address(struct tg3 *tp)
{
	struct net_device *dev = tp->dev;
	u32 hi, lo, mac_offset;
	int addr_ok = 0;

#ifdef CONFIG_SPARC
	if (!tg3_get_macaddr_sparc(tp))
		return 0;
#endif

	mac_offset = 0x7c;
	if ((GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5704) ||
	    (tp->tg3_flags2 & TG3_FLG2_5780_CLASS)) {
		if (tr32(TG3PCI_DUAL_MAC_CTRL) & DUAL_MAC_CTRL_ID)
			mac_offset = 0xcc;
		if (tg3_nvram_lock(tp))
			tw32_f(NVRAM_CMD, NVRAM_CMD_RESET);
		else
			tg3_nvram_unlock(tp);
	} else if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5717) {
		if (tr32(TG3_CPMU_STATUS) & TG3_CPMU_STATUS_PCIE_FUNC)
			mac_offset = 0xcc;
	} else if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5906)
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
		if (!(tp->tg3_flags3 & TG3_FLG3_NO_NVRAM) &&
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
	memcpy(dev->perm_addr, dev->dev_addr, dev->addr_len);
	return 0;
}

#define BOUNDARY_SINGLE_CACHELINE	1
#define BOUNDARY_MULTI_CACHELINE	2

static u32 __devinit tg3_calc_dma_bndry(struct tg3 *tp, u32 val)
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
	if (GET_ASIC_REV(tp->pci_chip_rev_id) != ASIC_REV_5700 &&
	    GET_ASIC_REV(tp->pci_chip_rev_id) != ASIC_REV_5701 &&
	    !(tp->tg3_flags2 & TG3_FLG2_PCI_EXPRESS))
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
	if ((tp->tg3_flags & TG3_FLAG_PCIX_MODE) &&
	    !(tp->tg3_flags2 & TG3_FLG2_PCI_EXPRESS)) {
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
	} else if (tp->tg3_flags2 & TG3_FLG2_PCI_EXPRESS) {
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

static int __devinit tg3_do_test_dma(struct tg3 *tp, u32 *buf, dma_addr_t buf_dma, int size, int to_device)
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

	if (to_device) {
		tw32(FTQ_DMA_HIGH_READ_FIFO_ENQDEQ, sram_dma_descs);
	} else {
		tw32(FTQ_DMA_HIGH_WRITE_FIFO_ENQDEQ, sram_dma_descs);
	}

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

static int __devinit tg3_test_dma(struct tg3 *tp)
{
	dma_addr_t buf_dma;
	u32 *buf, saved_dma_rwctrl;
	int ret;

	buf = pci_alloc_consistent(tp->pdev, TEST_BUFFER_SIZE, &buf_dma);
	if (!buf) {
		ret = -ENOMEM;
		goto out_nofree;
	}

	tp->dma_rwctrl = ((0x7 << DMA_RWCTRL_PCI_WRITE_CMD_SHIFT) |
			  (0x6 << DMA_RWCTRL_PCI_READ_CMD_SHIFT));

	tp->dma_rwctrl = tg3_calc_dma_bndry(tp, tp->dma_rwctrl);

	if (tp->tg3_flags2 & TG3_FLG2_PCI_EXPRESS) {
		/* DMA read watermark not used on PCIE */
		tp->dma_rwctrl |= 0x00180000;
	} else if (!(tp->tg3_flags & TG3_FLAG_PCIX_MODE)) {
		if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5705 ||
		    GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5750)
			tp->dma_rwctrl |= 0x003f0000;
		else
			tp->dma_rwctrl |= 0x003f000f;
	} else {
		if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5703 ||
		    GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5704) {
			u32 ccval = (tr32(TG3PCI_CLOCK_CTRL) & 0x1f);
			u32 read_water = 0x7;

			/* If the 5704 is behind the EPB bridge, we can
			 * do the less restrictive ONE_DMA workaround for
			 * better performance.
			 */
			if ((tp->tg3_flags & TG3_FLAG_40BIT_DMA_BUG) &&
			    GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5704)
				tp->dma_rwctrl |= 0x8000;
			else if (ccval == 0x6 || ccval == 0x7)
				tp->dma_rwctrl |= DMA_RWCTRL_ONE_DMA;

			if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5703)
				read_water = 4;
			/* Set bit 23 to enable PCIX hw bug fix */
			tp->dma_rwctrl |=
				(read_water << DMA_RWCTRL_READ_WATER_SHIFT) |
				(0x3 << DMA_RWCTRL_WRITE_WATER_SHIFT) |
				(1 << 23);
		} else if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5780) {
			/* 5780 always in PCIX mode */
			tp->dma_rwctrl |= 0x00144000;
		} else if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5714) {
			/* 5714 always in PCIX mode */
			tp->dma_rwctrl |= 0x00148000;
		} else {
			tp->dma_rwctrl |= 0x001b000f;
		}
	}

	if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5703 ||
	    GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5704)
		tp->dma_rwctrl &= 0xfffffff0;

	if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5700 ||
	    GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5701) {
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

	ret = 0;
	if (GET_ASIC_REV(tp->pci_chip_rev_id) != ASIC_REV_5700 &&
	    GET_ASIC_REV(tp->pci_chip_rev_id) != ASIC_REV_5701)
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
		ret = tg3_do_test_dma(tp, buf, buf_dma, TEST_BUFFER_SIZE, 1);
		if (ret) {
			printk(KERN_ERR "tg3_test_dma() Write the buffer failed %d\n", ret);
			break;
		}

#if 0
		/* validate data reached card RAM correctly. */
		for (i = 0; i < TEST_BUFFER_SIZE / sizeof(u32); i++) {
			u32 val;
			tg3_read_mem(tp, 0x2100 + (i*4), &val);
			if (le32_to_cpu(val) != p[i]) {
				printk(KERN_ERR "  tg3_test_dma()  Card buffer corrupted on write! (%d != %d)\n", val, i);
				/* ret = -ENODEV here? */
			}
			p[i] = 0;
		}
#endif
		/* Now read it back. */
		ret = tg3_do_test_dma(tp, buf, buf_dma, TEST_BUFFER_SIZE, 0);
		if (ret) {
			printk(KERN_ERR "tg3_test_dma() Read the buffer failed %d\n", ret);

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
				printk(KERN_ERR "tg3_test_dma() buffer corrupted on read back! (%d != %d)\n", p[i], i);
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
		static struct pci_device_id dma_wait_state_chipsets[] = {
			{ PCI_DEVICE(PCI_VENDOR_ID_APPLE,
				     PCI_DEVICE_ID_APPLE_UNI_N_PCI15) },
			{ },
		};

		/* DMA test passed without adjusting DMA boundary,
		 * now look for chipsets that are known to expose the
		 * DMA bug without failing the test.
		 */
		if (pci_dev_present(dma_wait_state_chipsets)) {
			tp->dma_rwctrl &= ~DMA_RWCTRL_WRITE_BNDRY_MASK;
			tp->dma_rwctrl |= DMA_RWCTRL_WRITE_BNDRY_16;
		}
		else
			/* Safe to use the calculated DMA boundary. */
			tp->dma_rwctrl = saved_dma_rwctrl;

		tw32(TG3PCI_DMA_RW_CTRL, tp->dma_rwctrl);
	}

out:
	pci_free_consistent(tp->pdev, TEST_BUFFER_SIZE, buf, buf_dma);
out_nofree:
	return ret;
}

static void __devinit tg3_init_link_config(struct tg3 *tp)
{
	tp->link_config.advertising =
		(ADVERTISED_10baseT_Half | ADVERTISED_10baseT_Full |
		 ADVERTISED_100baseT_Half | ADVERTISED_100baseT_Full |
		 ADVERTISED_1000baseT_Half | ADVERTISED_1000baseT_Full |
		 ADVERTISED_Autoneg | ADVERTISED_MII);
	tp->link_config.speed = SPEED_INVALID;
	tp->link_config.duplex = DUPLEX_INVALID;
	tp->link_config.autoneg = AUTONEG_ENABLE;
	tp->link_config.active_speed = SPEED_INVALID;
	tp->link_config.active_duplex = DUPLEX_INVALID;
	tp->link_config.phy_is_low_power = 0;
	tp->link_config.orig_speed = SPEED_INVALID;
	tp->link_config.orig_duplex = DUPLEX_INVALID;
	tp->link_config.orig_autoneg = AUTONEG_INVALID;
}

static void __devinit tg3_init_bufmgr_config(struct tg3 *tp)
{
	if (tp->tg3_flags2 & TG3_FLG2_5705_PLUS &&
	    GET_ASIC_REV(tp->pci_chip_rev_id) != ASIC_REV_5717) {
		tp->bufmgr_config.mbuf_read_dma_low_water =
			DEFAULT_MB_RDMA_LOW_WATER_5705;
		tp->bufmgr_config.mbuf_mac_rx_low_water =
			DEFAULT_MB_MACRX_LOW_WATER_5705;
		tp->bufmgr_config.mbuf_high_water =
			DEFAULT_MB_HIGH_WATER_5705;
		if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5906) {
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

static char * __devinit tg3_phy_string(struct tg3 *tp)
{
	switch (tp->phy_id & PHY_ID_MASK) {
	case PHY_ID_BCM5400:	return "5400";
	case PHY_ID_BCM5401:	return "5401";
	case PHY_ID_BCM5411:	return "5411";
	case PHY_ID_BCM5701:	return "5701";
	case PHY_ID_BCM5703:	return "5703";
	case PHY_ID_BCM5704:	return "5704";
	case PHY_ID_BCM5705:	return "5705";
	case PHY_ID_BCM5750:	return "5750";
	case PHY_ID_BCM5752:	return "5752";
	case PHY_ID_BCM5714:	return "5714";
	case PHY_ID_BCM5780:	return "5780";
	case PHY_ID_BCM5755:	return "5755";
	case PHY_ID_BCM5787:	return "5787";
	case PHY_ID_BCM5784:	return "5784";
	case PHY_ID_BCM5756:	return "5722/5756";
	case PHY_ID_BCM5906:	return "5906";
	case PHY_ID_BCM5761:	return "5761";
	case PHY_ID_BCM8002:	return "8002/serdes";
	case 0:			return "serdes";
	default:		return "unknown";
	}
}

static char * __devinit tg3_bus_string(struct tg3 *tp, char *str)
{
	if (tp->tg3_flags2 & TG3_FLG2_PCI_EXPRESS) {
		strcpy(str, "PCI Express");
		return str;
	} else if (tp->tg3_flags & TG3_FLAG_PCIX_MODE) {
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
		if (tp->tg3_flags & TG3_FLAG_PCI_HIGH_SPEED)
			strcat(str, "66MHz");
		else
			strcat(str, "33MHz");
	}
	if (tp->tg3_flags & TG3_FLAG_PCI_32BIT)
		strcat(str, ":32-bit");
	else
		strcat(str, ":64-bit");
	return str;
}

static struct pci_dev * __devinit tg3_find_peer(struct tg3 *tp)
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

static void __devinit tg3_init_coal(struct tg3 *tp)
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

	if (tp->tg3_flags2 & TG3_FLG2_5705_PLUS) {
		ec->rx_coalesce_usecs_irq = 0;
		ec->tx_coalesce_usecs_irq = 0;
		ec->stats_block_coalesce_usecs = 0;
	}
}

static const struct net_device_ops tg3_netdev_ops = {
	.ndo_open		= tg3_open,
	.ndo_stop		= tg3_close,
	.ndo_start_xmit		= tg3_start_xmit,
	.ndo_get_stats		= tg3_get_stats,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_multicast_list	= tg3_set_rx_mode,
	.ndo_set_mac_address	= tg3_set_mac_addr,
	.ndo_do_ioctl		= tg3_ioctl,
	.ndo_tx_timeout		= tg3_tx_timeout,
	.ndo_change_mtu		= tg3_change_mtu,
#if TG3_VLAN_TAG_USED
	.ndo_vlan_rx_register	= tg3_vlan_rx_register,
#endif
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= tg3_poll_controller,
#endif
};

static const struct net_device_ops tg3_netdev_ops_dma_bug = {
	.ndo_open		= tg3_open,
	.ndo_stop		= tg3_close,
	.ndo_start_xmit		= tg3_start_xmit_dma_bug,
	.ndo_get_stats		= tg3_get_stats,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_multicast_list	= tg3_set_rx_mode,
	.ndo_set_mac_address	= tg3_set_mac_addr,
	.ndo_do_ioctl		= tg3_ioctl,
	.ndo_tx_timeout		= tg3_tx_timeout,
	.ndo_change_mtu		= tg3_change_mtu,
#if TG3_VLAN_TAG_USED
	.ndo_vlan_rx_register	= tg3_vlan_rx_register,
#endif
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= tg3_poll_controller,
#endif
};

static int __devinit tg3_init_one(struct pci_dev *pdev,
				  const struct pci_device_id *ent)
{
	static int tg3_version_printed = 0;
	struct net_device *dev;
	struct tg3 *tp;
	int i, err, pm_cap;
	u32 sndmbx, rcvmbx, intmbx;
	char str[40];
	u64 dma_mask, persist_dma_mask;

	if (tg3_version_printed++ == 0)
		printk(KERN_INFO "%s", version);

	err = pci_enable_device(pdev);
	if (err) {
		printk(KERN_ERR PFX "Cannot enable PCI device, "
		       "aborting.\n");
		return err;
	}

	err = pci_request_regions(pdev, DRV_MODULE_NAME);
	if (err) {
		printk(KERN_ERR PFX "Cannot obtain PCI resources, "
		       "aborting.\n");
		goto err_out_disable_pdev;
	}

	pci_set_master(pdev);

	/* Find power-management capability. */
	pm_cap = pci_find_capability(pdev, PCI_CAP_ID_PM);
	if (pm_cap == 0) {
		printk(KERN_ERR PFX "Cannot find PowerManagement capability, "
		       "aborting.\n");
		err = -EIO;
		goto err_out_free_res;
	}

	dev = alloc_etherdev_mq(sizeof(*tp), TG3_IRQ_MAX_VECS);
	if (!dev) {
		printk(KERN_ERR PFX "Etherdev alloc failed, aborting.\n");
		err = -ENOMEM;
		goto err_out_free_res;
	}

	SET_NETDEV_DEV(dev, &pdev->dev);

#if TG3_VLAN_TAG_USED
	dev->features |= NETIF_F_HW_VLAN_TX | NETIF_F_HW_VLAN_RX;
#endif

	tp = netdev_priv(dev);
	tp->pdev = pdev;
	tp->dev = dev;
	tp->pm_cap = pm_cap;
	tp->rx_mode = TG3_DEF_RX_MODE;
	tp->tx_mode = TG3_DEF_TX_MODE;

	if (tg3_debug > 0)
		tp->msg_enable = tg3_debug;
	else
		tp->msg_enable = TG3_DEF_MSG_ENABLE;

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
		printk(KERN_ERR PFX "Cannot map device registers, "
		       "aborting.\n");
		err = -ENOMEM;
		goto err_out_free_dev;
	}

	tg3_init_link_config(tp);

	tp->rx_pending = TG3_DEF_RX_RING_PENDING;
	tp->rx_jumbo_pending = TG3_DEF_RX_JUMBO_RING_PENDING;

	intmbx = MAILBOX_INTERRUPT_0 + TG3_64BIT_REG_LOW;
	rcvmbx = MAILBOX_RCVRET_CON_IDX_0 + TG3_64BIT_REG_LOW;
	sndmbx = MAILBOX_SNDHOST_PROD_IDX_0 + TG3_64BIT_REG_LOW;
	for (i = 0; i < TG3_IRQ_MAX_VECS; i++) {
		struct tg3_napi *tnapi = &tp->napi[i];

		tnapi->tp = tp;
		tnapi->tx_pending = TG3_DEF_TX_RING_PENDING;

		tnapi->int_mbox = intmbx;
		if (i < 4)
			intmbx += 0x8;
		else
			intmbx += 0x4;

		tnapi->consmbox = rcvmbx;
		tnapi->prodmbox = sndmbx;

		if (i)
			tnapi->coal_now = HOSTCC_MODE_COAL_VEC1_NOW << (i - 1);
		else
			tnapi->coal_now = HOSTCC_MODE_NOW;

		if (!(tp->tg3_flags & TG3_FLAG_SUPPORT_MSIX))
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

	netif_napi_add(dev, &tp->napi[0].napi, tg3_poll, 64);
	dev->ethtool_ops = &tg3_ethtool_ops;
	dev->watchdog_timeo = TG3_TX_TIMEOUT;
	dev->irq = pdev->irq;

	err = tg3_get_invariants(tp);
	if (err) {
		printk(KERN_ERR PFX "Problem fetching invariants of chip, "
		       "aborting.\n");
		goto err_out_iounmap;
	}

	if (tp->tg3_flags3 & TG3_FLG3_5755_PLUS)
		dev->netdev_ops = &tg3_netdev_ops;
	else
		dev->netdev_ops = &tg3_netdev_ops_dma_bug;


	/* The EPB bridge inside 5714, 5715, and 5780 and any
	 * device behind the EPB cannot support DMA addresses > 40-bit.
	 * On 64-bit systems with IOMMU, use 40-bit dma_mask.
	 * On 64-bit systems without IOMMU, use 64-bit dma_mask and
	 * do DMA address check in tg3_start_xmit().
	 */
	if (tp->tg3_flags2 & TG3_FLG2_IS_5788)
		persist_dma_mask = dma_mask = DMA_BIT_MASK(32);
	else if (tp->tg3_flags & TG3_FLAG_40BIT_DMA_BUG) {
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
			dev->features |= NETIF_F_HIGHDMA;
			err = pci_set_consistent_dma_mask(pdev,
							  persist_dma_mask);
			if (err < 0) {
				printk(KERN_ERR PFX "Unable to obtain 64 bit "
				       "DMA for consistent allocations\n");
				goto err_out_iounmap;
			}
		}
	}
	if (err || dma_mask == DMA_BIT_MASK(32)) {
		err = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
		if (err) {
			printk(KERN_ERR PFX "No usable DMA configuration, "
			       "aborting.\n");
			goto err_out_iounmap;
		}
	}

	tg3_init_bufmgr_config(tp);

	if (tp->pci_chip_rev_id == CHIPREV_ID_5701_A0)
		tp->fw_needed = FIRMWARE_TG3;

	if (tp->tg3_flags2 & TG3_FLG2_HW_TSO) {
		tp->tg3_flags2 |= TG3_FLG2_TSO_CAPABLE;
	}
	else if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5700 ||
	    GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5701 ||
	    tp->pci_chip_rev_id == CHIPREV_ID_5705_A0 ||
	    GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5906 ||
	    (tp->tg3_flags & TG3_FLAG_ENABLE_ASF) != 0) {
		tp->tg3_flags2 &= ~TG3_FLG2_TSO_CAPABLE;
	} else {
		tp->tg3_flags2 |= TG3_FLG2_TSO_CAPABLE | TG3_FLG2_TSO_BUG;
		if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5705)
			tp->fw_needed = FIRMWARE_TG3TSO5;
		else
			tp->fw_needed = FIRMWARE_TG3TSO;
	}

	/* TSO is on by default on chips that support hardware TSO.
	 * Firmware TSO on older chips gives lower performance, so it
	 * is off by default, but can be enabled using ethtool.
	 */
	if (tp->tg3_flags2 & TG3_FLG2_HW_TSO) {
		if (dev->features & NETIF_F_IP_CSUM)
			dev->features |= NETIF_F_TSO;
		if ((dev->features & NETIF_F_IPV6_CSUM) &&
		    (tp->tg3_flags2 & TG3_FLG2_HW_TSO_2))
			dev->features |= NETIF_F_TSO6;
		if (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5761 ||
		    (GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5784 &&
		     GET_CHIP_REV(tp->pci_chip_rev_id) != CHIPREV_5784_AX) ||
			GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5785 ||
		    GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_57780 ||
		    GET_ASIC_REV(tp->pci_chip_rev_id) == ASIC_REV_5717)
			dev->features |= NETIF_F_TSO_ECN;
	}


	if (tp->pci_chip_rev_id == CHIPREV_ID_5705_A1 &&
	    !(tp->tg3_flags2 & TG3_FLG2_TSO_CAPABLE) &&
	    !(tr32(TG3PCI_PCISTATE) & PCISTATE_BUS_SPEED_HIGH)) {
		tp->tg3_flags2 |= TG3_FLG2_MAX_RXPEND_64;
		tp->rx_pending = 63;
	}

	err = tg3_get_device_address(tp);
	if (err) {
		printk(KERN_ERR PFX "Could not obtain valid ethernet address, "
		       "aborting.\n");
		goto err_out_fw;
	}

	if (tp->tg3_flags3 & TG3_FLG3_ENABLE_APE) {
		tp->aperegs = pci_ioremap_bar(pdev, BAR_2);
		if (!tp->aperegs) {
			printk(KERN_ERR PFX "Cannot map APE registers, "
			       "aborting.\n");
			err = -ENOMEM;
			goto err_out_fw;
		}

		tg3_ape_lock_init(tp);

		if (tp->tg3_flags & TG3_FLAG_ENABLE_ASF)
			tg3_read_dash_ver(tp);
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
		printk(KERN_ERR PFX "DMA engine test failed, aborting.\n");
		goto err_out_apeunmap;
	}

	/* flow control autonegotiation is default behavior */
	tp->tg3_flags |= TG3_FLAG_PAUSE_AUTONEG;
	tp->link_config.flowctrl = FLOW_CTRL_TX | FLOW_CTRL_RX;

	tg3_init_coal(tp);

	pci_set_drvdata(pdev, dev);

	err = register_netdev(dev);
	if (err) {
		printk(KERN_ERR PFX "Cannot register net device, "
		       "aborting.\n");
		goto err_out_apeunmap;
	}

	printk(KERN_INFO "%s: Tigon3 [partno(%s) rev %04x] (%s) MAC address %pM\n",
	       dev->name,
	       tp->board_part_number,
	       tp->pci_chip_rev_id,
	       tg3_bus_string(tp, str),
	       dev->dev_addr);

	if (tp->tg3_flags3 & TG3_FLG3_PHY_CONNECTED)
		printk(KERN_INFO
		       "%s: attached PHY driver [%s] (mii_bus:phy_addr=%s)\n",
		       tp->dev->name,
		       tp->mdio_bus->phy_map[PHY_ADDR]->drv->name,
		       dev_name(&tp->mdio_bus->phy_map[PHY_ADDR]->dev));
	else
		printk(KERN_INFO
		       "%s: attached PHY is %s (%s Ethernet) (WireSpeed[%d])\n",
		       tp->dev->name, tg3_phy_string(tp),
		       ((tp->tg3_flags & TG3_FLAG_10_100_ONLY) ? "10/100Base-TX" :
			((tp->tg3_flags2 & TG3_FLG2_ANY_SERDES) ? "1000Base-SX" :
			 "10/100/1000Base-T")),
		       (tp->tg3_flags2 & TG3_FLG2_NO_ETH_WIRE_SPEED) == 0);

	printk(KERN_INFO "%s: RXcsums[%d] LinkChgREG[%d] MIirq[%d] ASF[%d] TSOcap[%d]\n",
	       dev->name,
	       (tp->tg3_flags & TG3_FLAG_RX_CHECKSUMS) != 0,
	       (tp->tg3_flags & TG3_FLAG_USE_LINKCHG_REG) != 0,
	       (tp->tg3_flags & TG3_FLAG_USE_MI_INTERRUPT) != 0,
	       (tp->tg3_flags & TG3_FLAG_ENABLE_ASF) != 0,
	       (tp->tg3_flags2 & TG3_FLG2_TSO_CAPABLE) != 0);
	printk(KERN_INFO "%s: dma_rwctrl[%08x] dma_mask[%d-bit]\n",
	       dev->name, tp->dma_rwctrl,
	       (pdev->dma_mask == DMA_BIT_MASK(32)) ? 32 :
	        (((u64) pdev->dma_mask == DMA_BIT_MASK(40)) ? 40 : 64));

	return 0;

err_out_apeunmap:
	if (tp->aperegs) {
		iounmap(tp->aperegs);
		tp->aperegs = NULL;
	}

err_out_fw:
	if (tp->fw)
		release_firmware(tp->fw);

err_out_iounmap:
	if (tp->regs) {
		iounmap(tp->regs);
		tp->regs = NULL;
	}

err_out_free_dev:
	free_netdev(dev);

err_out_free_res:
	pci_release_regions(pdev);

err_out_disable_pdev:
	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);
	return err;
}

static void __devexit tg3_remove_one(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);

	if (dev) {
		struct tg3 *tp = netdev_priv(dev);

		if (tp->fw)
			release_firmware(tp->fw);

		flush_scheduled_work();

		if (tp->tg3_flags3 & TG3_FLG3_USE_PHYLIB) {
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

static int tg3_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct tg3 *tp = netdev_priv(dev);
	pci_power_t target_state;
	int err;

	/* PCI register 4 needs to be saved whether netif_running() or not.
	 * MSI address and data need to be saved if using MSI and
	 * netif_running().
	 */
	pci_save_state(pdev);

	if (!netif_running(dev))
		return 0;

	flush_scheduled_work();
	tg3_phy_stop(tp);
	tg3_netif_stop(tp);

	del_timer_sync(&tp->timer);

	tg3_full_lock(tp, 1);
	tg3_disable_ints(tp);
	tg3_full_unlock(tp);

	netif_device_detach(dev);

	tg3_full_lock(tp, 0);
	tg3_halt(tp, RESET_KIND_SHUTDOWN, 1);
	tp->tg3_flags &= ~TG3_FLAG_INIT_COMPLETE;
	tg3_full_unlock(tp);

	target_state = pdev->pm_cap ? pci_target_state(pdev) : PCI_D3hot;

	err = tg3_set_power_state(tp, target_state);
	if (err) {
		int err2;

		tg3_full_lock(tp, 0);

		tp->tg3_flags |= TG3_FLAG_INIT_COMPLETE;
		err2 = tg3_restart_hw(tp, 1);
		if (err2)
			goto out;

		tp->timer.expires = jiffies + tp->timer_offset;
		add_timer(&tp->timer);

		netif_device_attach(dev);
		tg3_netif_start(tp);

out:
		tg3_full_unlock(tp);

		if (!err2)
			tg3_phy_start(tp);
	}

	return err;
}

static int tg3_resume(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct tg3 *tp = netdev_priv(dev);
	int err;

	pci_restore_state(tp->pdev);

	if (!netif_running(dev))
		return 0;

	err = tg3_set_power_state(tp, PCI_D0);
	if (err)
		return err;

	netif_device_attach(dev);

	tg3_full_lock(tp, 0);

	tp->tg3_flags |= TG3_FLAG_INIT_COMPLETE;
	err = tg3_restart_hw(tp, 1);
	if (err)
		goto out;

	tp->timer.expires = jiffies + tp->timer_offset;
	add_timer(&tp->timer);

	tg3_netif_start(tp);

out:
	tg3_full_unlock(tp);

	if (!err)
		tg3_phy_start(tp);

	return err;
}

static struct pci_driver tg3_driver = {
	.name		= DRV_MODULE_NAME,
	.id_table	= tg3_pci_tbl,
	.probe		= tg3_init_one,
	.remove		= __devexit_p(tg3_remove_one),
	.suspend	= tg3_suspend,
	.resume		= tg3_resume
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
