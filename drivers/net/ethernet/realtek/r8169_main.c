// SPDX-License-Identifier: GPL-2.0-only
/*
 * r8169.c: RealTek 8169/8168/8101 ethernet driver.
 *
 * Copyright (c) 2002 ShuChen <shuchen@realtek.com.tw>
 * Copyright (c) 2003 - 2007 Francois Romieu <romieu@fr.zoreil.com>
 * Copyright (c) a lot of people too. Please respect their work.
 *
 * See MAINTAINERS file for support contact information.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/ethtool.h>
#include <linux/phy.h>
#include <linux/if_vlan.h>
#include <linux/crc32.h>
#include <linux/in.h>
#include <linux/io.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/pm_runtime.h>
#include <linux/prefetch.h>
#include <linux/pci-aspm.h>
#include <linux/ipv6.h>
#include <net/ip6_checksum.h>

#include "r8169_firmware.h"

#define MODULENAME "r8169"

#define FIRMWARE_8168D_1	"rtl_nic/rtl8168d-1.fw"
#define FIRMWARE_8168D_2	"rtl_nic/rtl8168d-2.fw"
#define FIRMWARE_8168E_1	"rtl_nic/rtl8168e-1.fw"
#define FIRMWARE_8168E_2	"rtl_nic/rtl8168e-2.fw"
#define FIRMWARE_8168E_3	"rtl_nic/rtl8168e-3.fw"
#define FIRMWARE_8168F_1	"rtl_nic/rtl8168f-1.fw"
#define FIRMWARE_8168F_2	"rtl_nic/rtl8168f-2.fw"
#define FIRMWARE_8105E_1	"rtl_nic/rtl8105e-1.fw"
#define FIRMWARE_8402_1		"rtl_nic/rtl8402-1.fw"
#define FIRMWARE_8411_1		"rtl_nic/rtl8411-1.fw"
#define FIRMWARE_8411_2		"rtl_nic/rtl8411-2.fw"
#define FIRMWARE_8106E_1	"rtl_nic/rtl8106e-1.fw"
#define FIRMWARE_8106E_2	"rtl_nic/rtl8106e-2.fw"
#define FIRMWARE_8168G_2	"rtl_nic/rtl8168g-2.fw"
#define FIRMWARE_8168G_3	"rtl_nic/rtl8168g-3.fw"
#define FIRMWARE_8168H_1	"rtl_nic/rtl8168h-1.fw"
#define FIRMWARE_8168H_2	"rtl_nic/rtl8168h-2.fw"
#define FIRMWARE_8107E_1	"rtl_nic/rtl8107e-1.fw"
#define FIRMWARE_8107E_2	"rtl_nic/rtl8107e-2.fw"

#define R8169_MSG_DEFAULT \
	(NETIF_MSG_DRV | NETIF_MSG_PROBE | NETIF_MSG_IFUP | NETIF_MSG_IFDOWN)

/* Maximum number of multicast addresses to filter (vs. Rx-all-multicast).
   The RTL chips use a 64 element hash table based on the Ethernet CRC. */
static const int multicast_filter_limit = 32;

#define TX_DMA_BURST	7	/* Maximum PCI burst, '7' is unlimited */
#define InterFrameGap	0x03	/* 3 means InterFrameGap = the shortest one */

#define R8169_REGS_SIZE		256
#define R8169_RX_BUF_SIZE	(SZ_16K - 1)
#define NUM_TX_DESC	64	/* Number of Tx descriptor registers */
#define NUM_RX_DESC	256U	/* Number of Rx descriptor registers */
#define R8169_TX_RING_BYTES	(NUM_TX_DESC * sizeof(struct TxDesc))
#define R8169_RX_RING_BYTES	(NUM_RX_DESC * sizeof(struct RxDesc))

#define RTL_CFG_NO_GBIT	1

/* write/read MMIO register */
#define RTL_W8(tp, reg, val8)	writeb((val8), tp->mmio_addr + (reg))
#define RTL_W16(tp, reg, val16)	writew((val16), tp->mmio_addr + (reg))
#define RTL_W32(tp, reg, val32)	writel((val32), tp->mmio_addr + (reg))
#define RTL_R8(tp, reg)		readb(tp->mmio_addr + (reg))
#define RTL_R16(tp, reg)		readw(tp->mmio_addr + (reg))
#define RTL_R32(tp, reg)		readl(tp->mmio_addr + (reg))

enum mac_version {
	/* support for ancient RTL_GIGA_MAC_VER_01 has been removed */
	RTL_GIGA_MAC_VER_02,
	RTL_GIGA_MAC_VER_03,
	RTL_GIGA_MAC_VER_04,
	RTL_GIGA_MAC_VER_05,
	RTL_GIGA_MAC_VER_06,
	RTL_GIGA_MAC_VER_07,
	RTL_GIGA_MAC_VER_08,
	RTL_GIGA_MAC_VER_09,
	RTL_GIGA_MAC_VER_10,
	RTL_GIGA_MAC_VER_11,
	RTL_GIGA_MAC_VER_12,
	RTL_GIGA_MAC_VER_13,
	RTL_GIGA_MAC_VER_14,
	RTL_GIGA_MAC_VER_15,
	RTL_GIGA_MAC_VER_16,
	RTL_GIGA_MAC_VER_17,
	RTL_GIGA_MAC_VER_18,
	RTL_GIGA_MAC_VER_19,
	RTL_GIGA_MAC_VER_20,
	RTL_GIGA_MAC_VER_21,
	RTL_GIGA_MAC_VER_22,
	RTL_GIGA_MAC_VER_23,
	RTL_GIGA_MAC_VER_24,
	RTL_GIGA_MAC_VER_25,
	RTL_GIGA_MAC_VER_26,
	RTL_GIGA_MAC_VER_27,
	RTL_GIGA_MAC_VER_28,
	RTL_GIGA_MAC_VER_29,
	RTL_GIGA_MAC_VER_30,
	RTL_GIGA_MAC_VER_31,
	RTL_GIGA_MAC_VER_32,
	RTL_GIGA_MAC_VER_33,
	RTL_GIGA_MAC_VER_34,
	RTL_GIGA_MAC_VER_35,
	RTL_GIGA_MAC_VER_36,
	RTL_GIGA_MAC_VER_37,
	RTL_GIGA_MAC_VER_38,
	RTL_GIGA_MAC_VER_39,
	RTL_GIGA_MAC_VER_40,
	RTL_GIGA_MAC_VER_41,
	RTL_GIGA_MAC_VER_42,
	RTL_GIGA_MAC_VER_43,
	RTL_GIGA_MAC_VER_44,
	RTL_GIGA_MAC_VER_45,
	RTL_GIGA_MAC_VER_46,
	RTL_GIGA_MAC_VER_47,
	RTL_GIGA_MAC_VER_48,
	RTL_GIGA_MAC_VER_49,
	RTL_GIGA_MAC_VER_50,
	RTL_GIGA_MAC_VER_51,
	RTL_GIGA_MAC_NONE
};

#define JUMBO_1K	ETH_DATA_LEN
#define JUMBO_4K	(4*1024 - ETH_HLEN - 2)
#define JUMBO_6K	(6*1024 - ETH_HLEN - 2)
#define JUMBO_7K	(7*1024 - ETH_HLEN - 2)
#define JUMBO_9K	(9*1024 - ETH_HLEN - 2)

static const struct {
	const char *name;
	const char *fw_name;
} rtl_chip_infos[] = {
	/* PCI devices. */
	[RTL_GIGA_MAC_VER_02] = {"RTL8169s"				},
	[RTL_GIGA_MAC_VER_03] = {"RTL8110s"				},
	[RTL_GIGA_MAC_VER_04] = {"RTL8169sb/8110sb"			},
	[RTL_GIGA_MAC_VER_05] = {"RTL8169sc/8110sc"			},
	[RTL_GIGA_MAC_VER_06] = {"RTL8169sc/8110sc"			},
	/* PCI-E devices. */
	[RTL_GIGA_MAC_VER_07] = {"RTL8102e"				},
	[RTL_GIGA_MAC_VER_08] = {"RTL8102e"				},
	[RTL_GIGA_MAC_VER_09] = {"RTL8102e"				},
	[RTL_GIGA_MAC_VER_10] = {"RTL8101e"				},
	[RTL_GIGA_MAC_VER_11] = {"RTL8168b/8111b"			},
	[RTL_GIGA_MAC_VER_12] = {"RTL8168b/8111b"			},
	[RTL_GIGA_MAC_VER_13] = {"RTL8101e"				},
	[RTL_GIGA_MAC_VER_14] = {"RTL8100e"				},
	[RTL_GIGA_MAC_VER_15] = {"RTL8100e"				},
	[RTL_GIGA_MAC_VER_16] = {"RTL8101e"				},
	[RTL_GIGA_MAC_VER_17] = {"RTL8168b/8111b"			},
	[RTL_GIGA_MAC_VER_18] = {"RTL8168cp/8111cp"			},
	[RTL_GIGA_MAC_VER_19] = {"RTL8168c/8111c"			},
	[RTL_GIGA_MAC_VER_20] = {"RTL8168c/8111c"			},
	[RTL_GIGA_MAC_VER_21] = {"RTL8168c/8111c"			},
	[RTL_GIGA_MAC_VER_22] = {"RTL8168c/8111c"			},
	[RTL_GIGA_MAC_VER_23] = {"RTL8168cp/8111cp"			},
	[RTL_GIGA_MAC_VER_24] = {"RTL8168cp/8111cp"			},
	[RTL_GIGA_MAC_VER_25] = {"RTL8168d/8111d",	FIRMWARE_8168D_1},
	[RTL_GIGA_MAC_VER_26] = {"RTL8168d/8111d",	FIRMWARE_8168D_2},
	[RTL_GIGA_MAC_VER_27] = {"RTL8168dp/8111dp"			},
	[RTL_GIGA_MAC_VER_28] = {"RTL8168dp/8111dp"			},
	[RTL_GIGA_MAC_VER_29] = {"RTL8105e",		FIRMWARE_8105E_1},
	[RTL_GIGA_MAC_VER_30] = {"RTL8105e",		FIRMWARE_8105E_1},
	[RTL_GIGA_MAC_VER_31] = {"RTL8168dp/8111dp"			},
	[RTL_GIGA_MAC_VER_32] = {"RTL8168e/8111e",	FIRMWARE_8168E_1},
	[RTL_GIGA_MAC_VER_33] = {"RTL8168e/8111e",	FIRMWARE_8168E_2},
	[RTL_GIGA_MAC_VER_34] = {"RTL8168evl/8111evl",	FIRMWARE_8168E_3},
	[RTL_GIGA_MAC_VER_35] = {"RTL8168f/8111f",	FIRMWARE_8168F_1},
	[RTL_GIGA_MAC_VER_36] = {"RTL8168f/8111f",	FIRMWARE_8168F_2},
	[RTL_GIGA_MAC_VER_37] = {"RTL8402",		FIRMWARE_8402_1 },
	[RTL_GIGA_MAC_VER_38] = {"RTL8411",		FIRMWARE_8411_1 },
	[RTL_GIGA_MAC_VER_39] = {"RTL8106e",		FIRMWARE_8106E_1},
	[RTL_GIGA_MAC_VER_40] = {"RTL8168g/8111g",	FIRMWARE_8168G_2},
	[RTL_GIGA_MAC_VER_41] = {"RTL8168g/8111g"			},
	[RTL_GIGA_MAC_VER_42] = {"RTL8168g/8111g",	FIRMWARE_8168G_3},
	[RTL_GIGA_MAC_VER_43] = {"RTL8106e",		FIRMWARE_8106E_2},
	[RTL_GIGA_MAC_VER_44] = {"RTL8411",		FIRMWARE_8411_2 },
	[RTL_GIGA_MAC_VER_45] = {"RTL8168h/8111h",	FIRMWARE_8168H_1},
	[RTL_GIGA_MAC_VER_46] = {"RTL8168h/8111h",	FIRMWARE_8168H_2},
	[RTL_GIGA_MAC_VER_47] = {"RTL8107e",		FIRMWARE_8107E_1},
	[RTL_GIGA_MAC_VER_48] = {"RTL8107e",		FIRMWARE_8107E_2},
	[RTL_GIGA_MAC_VER_49] = {"RTL8168ep/8111ep"			},
	[RTL_GIGA_MAC_VER_50] = {"RTL8168ep/8111ep"			},
	[RTL_GIGA_MAC_VER_51] = {"RTL8168ep/8111ep"			},
};

static const struct pci_device_id rtl8169_pci_tbl[] = {
	{ PCI_VDEVICE(REALTEK,	0x2502) },
	{ PCI_VDEVICE(REALTEK,	0x2600) },
	{ PCI_VDEVICE(REALTEK,	0x8129) },
	{ PCI_VDEVICE(REALTEK,	0x8136), RTL_CFG_NO_GBIT },
	{ PCI_VDEVICE(REALTEK,	0x8161) },
	{ PCI_VDEVICE(REALTEK,	0x8167) },
	{ PCI_VDEVICE(REALTEK,	0x8168) },
	{ PCI_VDEVICE(NCUBE,	0x8168) },
	{ PCI_VDEVICE(REALTEK,	0x8169) },
	{ PCI_VENDOR_ID_DLINK,	0x4300,
		PCI_VENDOR_ID_DLINK, 0x4b10, 0, 0 },
	{ PCI_VDEVICE(DLINK,	0x4300) },
	{ PCI_VDEVICE(DLINK,	0x4302) },
	{ PCI_VDEVICE(AT,	0xc107) },
	{ PCI_VDEVICE(USR,	0x0116) },
	{ PCI_VENDOR_ID_LINKSYS, 0x1032, PCI_ANY_ID, 0x0024 },
	{ 0x0001, 0x8168, PCI_ANY_ID, 0x2410 },
	{}
};

MODULE_DEVICE_TABLE(pci, rtl8169_pci_tbl);

static struct {
	u32 msg_enable;
} debug = { -1 };

enum rtl_registers {
	MAC0		= 0,	/* Ethernet hardware address. */
	MAC4		= 4,
	MAR0		= 8,	/* Multicast filter. */
	CounterAddrLow		= 0x10,
	CounterAddrHigh		= 0x14,
	TxDescStartAddrLow	= 0x20,
	TxDescStartAddrHigh	= 0x24,
	TxHDescStartAddrLow	= 0x28,
	TxHDescStartAddrHigh	= 0x2c,
	FLASH		= 0x30,
	ERSR		= 0x36,
	ChipCmd		= 0x37,
	TxPoll		= 0x38,
	IntrMask	= 0x3c,
	IntrStatus	= 0x3e,

	TxConfig	= 0x40,
#define	TXCFG_AUTO_FIFO			(1 << 7)	/* 8111e-vl */
#define	TXCFG_EMPTY			(1 << 11)	/* 8111e-vl */

	RxConfig	= 0x44,
#define	RX128_INT_EN			(1 << 15)	/* 8111c and later */
#define	RX_MULTI_EN			(1 << 14)	/* 8111c only */
#define	RXCFG_FIFO_SHIFT		13
					/* No threshold before first PCI xfer */
#define	RX_FIFO_THRESH			(7 << RXCFG_FIFO_SHIFT)
#define	RX_EARLY_OFF			(1 << 11)
#define	RXCFG_DMA_SHIFT			8
					/* Unlimited maximum PCI burst. */
#define	RX_DMA_BURST			(7 << RXCFG_DMA_SHIFT)

	RxMissed	= 0x4c,
	Cfg9346		= 0x50,
	Config0		= 0x51,
	Config1		= 0x52,
	Config2		= 0x53,
#define PME_SIGNAL			(1 << 5)	/* 8168c and later */

	Config3		= 0x54,
	Config4		= 0x55,
	Config5		= 0x56,
	MultiIntr	= 0x5c,
	PHYAR		= 0x60,
	PHYstatus	= 0x6c,
	RxMaxSize	= 0xda,
	CPlusCmd	= 0xe0,
	IntrMitigate	= 0xe2,

#define RTL_COALESCE_MASK	0x0f
#define RTL_COALESCE_SHIFT	4
#define RTL_COALESCE_T_MAX	(RTL_COALESCE_MASK)
#define RTL_COALESCE_FRAME_MAX	(RTL_COALESCE_MASK << 2)

	RxDescAddrLow	= 0xe4,
	RxDescAddrHigh	= 0xe8,
	EarlyTxThres	= 0xec,	/* 8169. Unit of 32 bytes. */

#define NoEarlyTx	0x3f	/* Max value : no early transmit. */

	MaxTxPacketSize	= 0xec,	/* 8101/8168. Unit of 128 bytes. */

#define TxPacketMax	(8064 >> 7)
#define EarlySize	0x27

	FuncEvent	= 0xf0,
	FuncEventMask	= 0xf4,
	FuncPresetState	= 0xf8,
	IBCR0           = 0xf8,
	IBCR2           = 0xf9,
	IBIMR0          = 0xfa,
	IBISR0          = 0xfb,
	FuncForceEvent	= 0xfc,
};

enum rtl8168_8101_registers {
	CSIDR			= 0x64,
	CSIAR			= 0x68,
#define	CSIAR_FLAG			0x80000000
#define	CSIAR_WRITE_CMD			0x80000000
#define	CSIAR_BYTE_ENABLE		0x0000f000
#define	CSIAR_ADDR_MASK			0x00000fff
	PMCH			= 0x6f,
	EPHYAR			= 0x80,
#define	EPHYAR_FLAG			0x80000000
#define	EPHYAR_WRITE_CMD		0x80000000
#define	EPHYAR_REG_MASK			0x1f
#define	EPHYAR_REG_SHIFT		16
#define	EPHYAR_DATA_MASK		0xffff
	DLLPR			= 0xd0,
#define	PFM_EN				(1 << 6)
#define	TX_10M_PS_EN			(1 << 7)
	DBG_REG			= 0xd1,
#define	FIX_NAK_1			(1 << 4)
#define	FIX_NAK_2			(1 << 3)
	TWSI			= 0xd2,
	MCU			= 0xd3,
#define	NOW_IS_OOB			(1 << 7)
#define	TX_EMPTY			(1 << 5)
#define	RX_EMPTY			(1 << 4)
#define	RXTX_EMPTY			(TX_EMPTY | RX_EMPTY)
#define	EN_NDP				(1 << 3)
#define	EN_OOB_RESET			(1 << 2)
#define	LINK_LIST_RDY			(1 << 1)
	EFUSEAR			= 0xdc,
#define	EFUSEAR_FLAG			0x80000000
#define	EFUSEAR_WRITE_CMD		0x80000000
#define	EFUSEAR_READ_CMD		0x00000000
#define	EFUSEAR_REG_MASK		0x03ff
#define	EFUSEAR_REG_SHIFT		8
#define	EFUSEAR_DATA_MASK		0xff
	MISC_1			= 0xf2,
#define	PFM_D3COLD_EN			(1 << 6)
};

enum rtl8168_registers {
	LED_FREQ		= 0x1a,
	EEE_LED			= 0x1b,
	ERIDR			= 0x70,
	ERIAR			= 0x74,
#define ERIAR_FLAG			0x80000000
#define ERIAR_WRITE_CMD			0x80000000
#define ERIAR_READ_CMD			0x00000000
#define ERIAR_ADDR_BYTE_ALIGN		4
#define ERIAR_TYPE_SHIFT		16
#define ERIAR_EXGMAC			(0x00 << ERIAR_TYPE_SHIFT)
#define ERIAR_MSIX			(0x01 << ERIAR_TYPE_SHIFT)
#define ERIAR_ASF			(0x02 << ERIAR_TYPE_SHIFT)
#define ERIAR_OOB			(0x02 << ERIAR_TYPE_SHIFT)
#define ERIAR_MASK_SHIFT		12
#define ERIAR_MASK_0001			(0x1 << ERIAR_MASK_SHIFT)
#define ERIAR_MASK_0011			(0x3 << ERIAR_MASK_SHIFT)
#define ERIAR_MASK_0100			(0x4 << ERIAR_MASK_SHIFT)
#define ERIAR_MASK_0101			(0x5 << ERIAR_MASK_SHIFT)
#define ERIAR_MASK_1111			(0xf << ERIAR_MASK_SHIFT)
	EPHY_RXER_NUM		= 0x7c,
	OCPDR			= 0xb0,	/* OCP GPHY access */
#define OCPDR_WRITE_CMD			0x80000000
#define OCPDR_READ_CMD			0x00000000
#define OCPDR_REG_MASK			0x7f
#define OCPDR_GPHY_REG_SHIFT		16
#define OCPDR_DATA_MASK			0xffff
	OCPAR			= 0xb4,
#define OCPAR_FLAG			0x80000000
#define OCPAR_GPHY_WRITE_CMD		0x8000f060
#define OCPAR_GPHY_READ_CMD		0x0000f060
	GPHY_OCP		= 0xb8,
	RDSAR1			= 0xd0,	/* 8168c only. Undocumented on 8168dp */
	MISC			= 0xf0,	/* 8168e only. */
#define TXPLA_RST			(1 << 29)
#define DISABLE_LAN_EN			(1 << 23) /* Enable GPIO pin */
#define PWM_EN				(1 << 22)
#define RXDV_GATED_EN			(1 << 19)
#define EARLY_TALLY_EN			(1 << 16)
};

enum rtl_register_content {
	/* InterruptStatusBits */
	SYSErr		= 0x8000,
	PCSTimeout	= 0x4000,
	SWInt		= 0x0100,
	TxDescUnavail	= 0x0080,
	RxFIFOOver	= 0x0040,
	LinkChg		= 0x0020,
	RxOverflow	= 0x0010,
	TxErr		= 0x0008,
	TxOK		= 0x0004,
	RxErr		= 0x0002,
	RxOK		= 0x0001,

	/* RxStatusDesc */
	RxRWT	= (1 << 22),
	RxRES	= (1 << 21),
	RxRUNT	= (1 << 20),
	RxCRC	= (1 << 19),

	/* ChipCmdBits */
	StopReq		= 0x80,
	CmdReset	= 0x10,
	CmdRxEnb	= 0x08,
	CmdTxEnb	= 0x04,
	RxBufEmpty	= 0x01,

	/* TXPoll register p.5 */
	HPQ		= 0x80,		/* Poll cmd on the high prio queue */
	NPQ		= 0x40,		/* Poll cmd on the low prio queue */
	FSWInt		= 0x01,		/* Forced software interrupt */

	/* Cfg9346Bits */
	Cfg9346_Lock	= 0x00,
	Cfg9346_Unlock	= 0xc0,

	/* rx_mode_bits */
	AcceptErr	= 0x20,
	AcceptRunt	= 0x10,
	AcceptBroadcast	= 0x08,
	AcceptMulticast	= 0x04,
	AcceptMyPhys	= 0x02,
	AcceptAllPhys	= 0x01,
#define RX_CONFIG_ACCEPT_MASK		0x3f

	/* TxConfigBits */
	TxInterFrameGapShift = 24,
	TxDMAShift = 8,	/* DMA burst value (0-7) is shift this many bits */

	/* Config1 register p.24 */
	LEDS1		= (1 << 7),
	LEDS0		= (1 << 6),
	Speed_down	= (1 << 4),
	MEMMAP		= (1 << 3),
	IOMAP		= (1 << 2),
	VPD		= (1 << 1),
	PMEnable	= (1 << 0),	/* Power Management Enable */

	/* Config2 register p. 25 */
	ClkReqEn	= (1 << 7),	/* Clock Request Enable */
	MSIEnable	= (1 << 5),	/* 8169 only. Reserved in the 8168. */
	PCI_Clock_66MHz = 0x01,
	PCI_Clock_33MHz = 0x00,

	/* Config3 register p.25 */
	MagicPacket	= (1 << 5),	/* Wake up when receives a Magic Packet */
	LinkUp		= (1 << 4),	/* Wake up when the cable connection is re-established */
	Jumbo_En0	= (1 << 2),	/* 8168 only. Reserved in the 8168b */
	Rdy_to_L23	= (1 << 1),	/* L23 Enable */
	Beacon_en	= (1 << 0),	/* 8168 only. Reserved in the 8168b */

	/* Config4 register */
	Jumbo_En1	= (1 << 1),	/* 8168 only. Reserved in the 8168b */

	/* Config5 register p.27 */
	BWF		= (1 << 6),	/* Accept Broadcast wakeup frame */
	MWF		= (1 << 5),	/* Accept Multicast wakeup frame */
	UWF		= (1 << 4),	/* Accept Unicast wakeup frame */
	Spi_en		= (1 << 3),
	LanWake		= (1 << 1),	/* LanWake enable/disable */
	PMEStatus	= (1 << 0),	/* PME status can be reset by PCI RST# */
	ASPM_en		= (1 << 0),	/* ASPM enable */

	/* CPlusCmd p.31 */
	EnableBist	= (1 << 15),	// 8168 8101
	Mac_dbgo_oe	= (1 << 14),	// 8168 8101
	Normal_mode	= (1 << 13),	// unused
	Force_half_dup	= (1 << 12),	// 8168 8101
	Force_rxflow_en	= (1 << 11),	// 8168 8101
	Force_txflow_en	= (1 << 10),	// 8168 8101
	Cxpl_dbg_sel	= (1 << 9),	// 8168 8101
	ASF		= (1 << 8),	// 8168 8101
	PktCntrDisable	= (1 << 7),	// 8168 8101
	Mac_dbgo_sel	= 0x001c,	// 8168
	RxVlan		= (1 << 6),
	RxChkSum	= (1 << 5),
	PCIDAC		= (1 << 4),
	PCIMulRW	= (1 << 3),
#define INTT_MASK	GENMASK(1, 0)
#define CPCMD_MASK	(Normal_mode | RxVlan | RxChkSum | INTT_MASK)

	/* rtl8169_PHYstatus */
	TBI_Enable	= 0x80,
	TxFlowCtrl	= 0x40,
	RxFlowCtrl	= 0x20,
	_1000bpsF	= 0x10,
	_100bps		= 0x08,
	_10bps		= 0x04,
	LinkStatus	= 0x02,
	FullDup		= 0x01,

	/* ResetCounterCommand */
	CounterReset	= 0x1,

	/* DumpCounterCommand */
	CounterDump	= 0x8,

	/* magic enable v2 */
	MagicPacket_v2	= (1 << 16),	/* Wake up when receives a Magic Packet */
};

enum rtl_desc_bit {
	/* First doubleword. */
	DescOwn		= (1 << 31), /* Descriptor is owned by NIC */
	RingEnd		= (1 << 30), /* End of descriptor ring */
	FirstFrag	= (1 << 29), /* First segment of a packet */
	LastFrag	= (1 << 28), /* Final segment of a packet */
};

/* Generic case. */
enum rtl_tx_desc_bit {
	/* First doubleword. */
	TD_LSO		= (1 << 27),		/* Large Send Offload */
#define TD_MSS_MAX			0x07ffu	/* MSS value */

	/* Second doubleword. */
	TxVlanTag	= (1 << 17),		/* Add VLAN tag */
};

/* 8169, 8168b and 810x except 8102e. */
enum rtl_tx_desc_bit_0 {
	/* First doubleword. */
#define TD0_MSS_SHIFT			16	/* MSS position (11 bits) */
	TD0_TCP_CS	= (1 << 16),		/* Calculate TCP/IP checksum */
	TD0_UDP_CS	= (1 << 17),		/* Calculate UDP/IP checksum */
	TD0_IP_CS	= (1 << 18),		/* Calculate IP checksum */
};

/* 8102e, 8168c and beyond. */
enum rtl_tx_desc_bit_1 {
	/* First doubleword. */
	TD1_GTSENV4	= (1 << 26),		/* Giant Send for IPv4 */
	TD1_GTSENV6	= (1 << 25),		/* Giant Send for IPv6 */
#define GTTCPHO_SHIFT			18
#define GTTCPHO_MAX			0x7fU

	/* Second doubleword. */
#define TCPHO_SHIFT			18
#define TCPHO_MAX			0x3ffU
#define TD1_MSS_SHIFT			18	/* MSS position (11 bits) */
	TD1_IPv6_CS	= (1 << 28),		/* Calculate IPv6 checksum */
	TD1_IPv4_CS	= (1 << 29),		/* Calculate IPv4 checksum */
	TD1_TCP_CS	= (1 << 30),		/* Calculate TCP/IP checksum */
	TD1_UDP_CS	= (1 << 31),		/* Calculate UDP/IP checksum */
};

enum rtl_rx_desc_bit {
	/* Rx private */
	PID1		= (1 << 18), /* Protocol ID bit 1/2 */
	PID0		= (1 << 17), /* Protocol ID bit 0/2 */

#define RxProtoUDP	(PID1)
#define RxProtoTCP	(PID0)
#define RxProtoIP	(PID1 | PID0)
#define RxProtoMask	RxProtoIP

	IPFail		= (1 << 16), /* IP checksum failed */
	UDPFail		= (1 << 15), /* UDP/IP checksum failed */
	TCPFail		= (1 << 14), /* TCP/IP checksum failed */
	RxVlanTag	= (1 << 16), /* VLAN tag available */
};

#define RsvdMask	0x3fffc000

struct TxDesc {
	__le32 opts1;
	__le32 opts2;
	__le64 addr;
};

struct RxDesc {
	__le32 opts1;
	__le32 opts2;
	__le64 addr;
};

struct ring_info {
	struct sk_buff	*skb;
	u32		len;
};

struct rtl8169_counters {
	__le64	tx_packets;
	__le64	rx_packets;
	__le64	tx_errors;
	__le32	rx_errors;
	__le16	rx_missed;
	__le16	align_errors;
	__le32	tx_one_collision;
	__le32	tx_multi_collision;
	__le64	rx_unicast;
	__le64	rx_broadcast;
	__le32	rx_multicast;
	__le16	tx_aborted;
	__le16	tx_underun;
};

struct rtl8169_tc_offsets {
	bool	inited;
	__le64	tx_errors;
	__le32	tx_multi_collision;
	__le16	tx_aborted;
};

enum rtl_flag {
	RTL_FLAG_TASK_ENABLED = 0,
	RTL_FLAG_TASK_RESET_PENDING,
	RTL_FLAG_MAX
};

struct rtl8169_stats {
	u64			packets;
	u64			bytes;
	struct u64_stats_sync	syncp;
};

struct rtl8169_private {
	void __iomem *mmio_addr;	/* memory map physical address */
	struct pci_dev *pci_dev;
	struct net_device *dev;
	struct phy_device *phydev;
	struct napi_struct napi;
	u32 msg_enable;
	enum mac_version mac_version;
	u32 cur_rx; /* Index into the Rx descriptor buffer of next Rx pkt. */
	u32 cur_tx; /* Index into the Tx descriptor buffer of next Rx pkt. */
	u32 dirty_tx;
	struct rtl8169_stats rx_stats;
	struct rtl8169_stats tx_stats;
	struct TxDesc *TxDescArray;	/* 256-aligned Tx descriptor ring */
	struct RxDesc *RxDescArray;	/* 256-aligned Rx descriptor ring */
	dma_addr_t TxPhyAddr;
	dma_addr_t RxPhyAddr;
	void *Rx_databuff[NUM_RX_DESC];	/* Rx data buffers */
	struct ring_info tx_skb[NUM_TX_DESC];	/* Tx data buffers */
	u16 cp_cmd;
	u16 irq_mask;
	struct clk *clk;

	struct {
		DECLARE_BITMAP(flags, RTL_FLAG_MAX);
		struct mutex mutex;
		struct work_struct work;
	} wk;

	unsigned irq_enabled:1;
	unsigned supports_gmii:1;
	unsigned aspm_manageable:1;
	dma_addr_t counters_phys_addr;
	struct rtl8169_counters *counters;
	struct rtl8169_tc_offsets tc_offset;
	u32 saved_wolopts;

	const char *fw_name;
	struct rtl_fw *rtl_fw;

	u32 ocp_base;
};

typedef void (*rtl_generic_fct)(struct rtl8169_private *tp);

MODULE_AUTHOR("Realtek and the Linux r8169 crew <netdev@vger.kernel.org>");
MODULE_DESCRIPTION("RealTek RTL-8169 Gigabit Ethernet driver");
module_param_named(debug, debug.msg_enable, int, 0);
MODULE_PARM_DESC(debug, "Debug verbosity level (0=none, ..., 16=all)");
MODULE_SOFTDEP("pre: realtek");
MODULE_LICENSE("GPL");
MODULE_FIRMWARE(FIRMWARE_8168D_1);
MODULE_FIRMWARE(FIRMWARE_8168D_2);
MODULE_FIRMWARE(FIRMWARE_8168E_1);
MODULE_FIRMWARE(FIRMWARE_8168E_2);
MODULE_FIRMWARE(FIRMWARE_8168E_3);
MODULE_FIRMWARE(FIRMWARE_8105E_1);
MODULE_FIRMWARE(FIRMWARE_8168F_1);
MODULE_FIRMWARE(FIRMWARE_8168F_2);
MODULE_FIRMWARE(FIRMWARE_8402_1);
MODULE_FIRMWARE(FIRMWARE_8411_1);
MODULE_FIRMWARE(FIRMWARE_8411_2);
MODULE_FIRMWARE(FIRMWARE_8106E_1);
MODULE_FIRMWARE(FIRMWARE_8106E_2);
MODULE_FIRMWARE(FIRMWARE_8168G_2);
MODULE_FIRMWARE(FIRMWARE_8168G_3);
MODULE_FIRMWARE(FIRMWARE_8168H_1);
MODULE_FIRMWARE(FIRMWARE_8168H_2);
MODULE_FIRMWARE(FIRMWARE_8107E_1);
MODULE_FIRMWARE(FIRMWARE_8107E_2);

static inline struct device *tp_to_dev(struct rtl8169_private *tp)
{
	return &tp->pci_dev->dev;
}

static void rtl_lock_work(struct rtl8169_private *tp)
{
	mutex_lock(&tp->wk.mutex);
}

static void rtl_unlock_work(struct rtl8169_private *tp)
{
	mutex_unlock(&tp->wk.mutex);
}

static void rtl_lock_config_regs(struct rtl8169_private *tp)
{
	RTL_W8(tp, Cfg9346, Cfg9346_Lock);
}

static void rtl_unlock_config_regs(struct rtl8169_private *tp)
{
	RTL_W8(tp, Cfg9346, Cfg9346_Unlock);
}

static void rtl_tx_performance_tweak(struct rtl8169_private *tp, u16 force)
{
	pcie_capability_clear_and_set_word(tp->pci_dev, PCI_EXP_DEVCTL,
					   PCI_EXP_DEVCTL_READRQ, force);
}

static bool rtl_is_8168evl_up(struct rtl8169_private *tp)
{
	return tp->mac_version >= RTL_GIGA_MAC_VER_34 &&
	       tp->mac_version != RTL_GIGA_MAC_VER_39;
}

struct rtl_cond {
	bool (*check)(struct rtl8169_private *);
	const char *msg;
};

static void rtl_udelay(unsigned int d)
{
	udelay(d);
}

static bool rtl_loop_wait(struct rtl8169_private *tp, const struct rtl_cond *c,
			  void (*delay)(unsigned int), unsigned int d, int n,
			  bool high)
{
	int i;

	for (i = 0; i < n; i++) {
		if (c->check(tp) == high)
			return true;
		delay(d);
	}
	netif_err(tp, drv, tp->dev, "%s == %d (loop: %d, delay: %d).\n",
		  c->msg, !high, n, d);
	return false;
}

static bool rtl_udelay_loop_wait_high(struct rtl8169_private *tp,
				      const struct rtl_cond *c,
				      unsigned int d, int n)
{
	return rtl_loop_wait(tp, c, rtl_udelay, d, n, true);
}

static bool rtl_udelay_loop_wait_low(struct rtl8169_private *tp,
				     const struct rtl_cond *c,
				     unsigned int d, int n)
{
	return rtl_loop_wait(tp, c, rtl_udelay, d, n, false);
}

static bool rtl_msleep_loop_wait_high(struct rtl8169_private *tp,
				      const struct rtl_cond *c,
				      unsigned int d, int n)
{
	return rtl_loop_wait(tp, c, msleep, d, n, true);
}

static bool rtl_msleep_loop_wait_low(struct rtl8169_private *tp,
				     const struct rtl_cond *c,
				     unsigned int d, int n)
{
	return rtl_loop_wait(tp, c, msleep, d, n, false);
}

#define DECLARE_RTL_COND(name)				\
static bool name ## _check(struct rtl8169_private *);	\
							\
static const struct rtl_cond name = {			\
	.check	= name ## _check,			\
	.msg	= #name					\
};							\
							\
static bool name ## _check(struct rtl8169_private *tp)

static bool rtl_ocp_reg_failure(struct rtl8169_private *tp, u32 reg)
{
	if (reg & 0xffff0001) {
		netif_err(tp, drv, tp->dev, "Invalid ocp reg %x!\n", reg);
		return true;
	}
	return false;
}

DECLARE_RTL_COND(rtl_ocp_gphy_cond)
{
	return RTL_R32(tp, GPHY_OCP) & OCPAR_FLAG;
}

static void r8168_phy_ocp_write(struct rtl8169_private *tp, u32 reg, u32 data)
{
	if (rtl_ocp_reg_failure(tp, reg))
		return;

	RTL_W32(tp, GPHY_OCP, OCPAR_FLAG | (reg << 15) | data);

	rtl_udelay_loop_wait_low(tp, &rtl_ocp_gphy_cond, 25, 10);
}

static int r8168_phy_ocp_read(struct rtl8169_private *tp, u32 reg)
{
	if (rtl_ocp_reg_failure(tp, reg))
		return 0;

	RTL_W32(tp, GPHY_OCP, reg << 15);

	return rtl_udelay_loop_wait_high(tp, &rtl_ocp_gphy_cond, 25, 10) ?
		(RTL_R32(tp, GPHY_OCP) & 0xffff) : -ETIMEDOUT;
}

static void r8168_mac_ocp_write(struct rtl8169_private *tp, u32 reg, u32 data)
{
	if (rtl_ocp_reg_failure(tp, reg))
		return;

	RTL_W32(tp, OCPDR, OCPAR_FLAG | (reg << 15) | data);
}

static u16 r8168_mac_ocp_read(struct rtl8169_private *tp, u32 reg)
{
	if (rtl_ocp_reg_failure(tp, reg))
		return 0;

	RTL_W32(tp, OCPDR, reg << 15);

	return RTL_R32(tp, OCPDR);
}

#define OCP_STD_PHY_BASE	0xa400

static void r8168g_mdio_write(struct rtl8169_private *tp, int reg, int value)
{
	if (reg == 0x1f) {
		tp->ocp_base = value ? value << 4 : OCP_STD_PHY_BASE;
		return;
	}

	if (tp->ocp_base != OCP_STD_PHY_BASE)
		reg -= 0x10;

	r8168_phy_ocp_write(tp, tp->ocp_base + reg * 2, value);
}

static int r8168g_mdio_read(struct rtl8169_private *tp, int reg)
{
	if (tp->ocp_base != OCP_STD_PHY_BASE)
		reg -= 0x10;

	return r8168_phy_ocp_read(tp, tp->ocp_base + reg * 2);
}

static void mac_mcu_write(struct rtl8169_private *tp, int reg, int value)
{
	if (reg == 0x1f) {
		tp->ocp_base = value << 4;
		return;
	}

	r8168_mac_ocp_write(tp, tp->ocp_base + reg, value);
}

static int mac_mcu_read(struct rtl8169_private *tp, int reg)
{
	return r8168_mac_ocp_read(tp, tp->ocp_base + reg);
}

DECLARE_RTL_COND(rtl_phyar_cond)
{
	return RTL_R32(tp, PHYAR) & 0x80000000;
}

static void r8169_mdio_write(struct rtl8169_private *tp, int reg, int value)
{
	RTL_W32(tp, PHYAR, 0x80000000 | (reg & 0x1f) << 16 | (value & 0xffff));

	rtl_udelay_loop_wait_low(tp, &rtl_phyar_cond, 25, 20);
	/*
	 * According to hardware specs a 20us delay is required after write
	 * complete indication, but before sending next command.
	 */
	udelay(20);
}

static int r8169_mdio_read(struct rtl8169_private *tp, int reg)
{
	int value;

	RTL_W32(tp, PHYAR, 0x0 | (reg & 0x1f) << 16);

	value = rtl_udelay_loop_wait_high(tp, &rtl_phyar_cond, 25, 20) ?
		RTL_R32(tp, PHYAR) & 0xffff : -ETIMEDOUT;

	/*
	 * According to hardware specs a 20us delay is required after read
	 * complete indication, but before sending next command.
	 */
	udelay(20);

	return value;
}

DECLARE_RTL_COND(rtl_ocpar_cond)
{
	return RTL_R32(tp, OCPAR) & OCPAR_FLAG;
}

static void r8168dp_1_mdio_access(struct rtl8169_private *tp, int reg, u32 data)
{
	RTL_W32(tp, OCPDR, data | ((reg & OCPDR_REG_MASK) << OCPDR_GPHY_REG_SHIFT));
	RTL_W32(tp, OCPAR, OCPAR_GPHY_WRITE_CMD);
	RTL_W32(tp, EPHY_RXER_NUM, 0);

	rtl_udelay_loop_wait_low(tp, &rtl_ocpar_cond, 1000, 100);
}

static void r8168dp_1_mdio_write(struct rtl8169_private *tp, int reg, int value)
{
	r8168dp_1_mdio_access(tp, reg,
			      OCPDR_WRITE_CMD | (value & OCPDR_DATA_MASK));
}

static int r8168dp_1_mdio_read(struct rtl8169_private *tp, int reg)
{
	r8168dp_1_mdio_access(tp, reg, OCPDR_READ_CMD);

	mdelay(1);
	RTL_W32(tp, OCPAR, OCPAR_GPHY_READ_CMD);
	RTL_W32(tp, EPHY_RXER_NUM, 0);

	return rtl_udelay_loop_wait_high(tp, &rtl_ocpar_cond, 1000, 100) ?
		RTL_R32(tp, OCPDR) & OCPDR_DATA_MASK : -ETIMEDOUT;
}

#define R8168DP_1_MDIO_ACCESS_BIT	0x00020000

static void r8168dp_2_mdio_start(struct rtl8169_private *tp)
{
	RTL_W32(tp, 0xd0, RTL_R32(tp, 0xd0) & ~R8168DP_1_MDIO_ACCESS_BIT);
}

static void r8168dp_2_mdio_stop(struct rtl8169_private *tp)
{
	RTL_W32(tp, 0xd0, RTL_R32(tp, 0xd0) | R8168DP_1_MDIO_ACCESS_BIT);
}

static void r8168dp_2_mdio_write(struct rtl8169_private *tp, int reg, int value)
{
	r8168dp_2_mdio_start(tp);

	r8169_mdio_write(tp, reg, value);

	r8168dp_2_mdio_stop(tp);
}

static int r8168dp_2_mdio_read(struct rtl8169_private *tp, int reg)
{
	int value;

	r8168dp_2_mdio_start(tp);

	value = r8169_mdio_read(tp, reg);

	r8168dp_2_mdio_stop(tp);

	return value;
}

static void rtl_writephy(struct rtl8169_private *tp, int location, int val)
{
	switch (tp->mac_version) {
	case RTL_GIGA_MAC_VER_27:
		r8168dp_1_mdio_write(tp, location, val);
		break;
	case RTL_GIGA_MAC_VER_28:
	case RTL_GIGA_MAC_VER_31:
		r8168dp_2_mdio_write(tp, location, val);
		break;
	case RTL_GIGA_MAC_VER_40 ... RTL_GIGA_MAC_VER_51:
		r8168g_mdio_write(tp, location, val);
		break;
	default:
		r8169_mdio_write(tp, location, val);
		break;
	}
}

static int rtl_readphy(struct rtl8169_private *tp, int location)
{
	switch (tp->mac_version) {
	case RTL_GIGA_MAC_VER_27:
		return r8168dp_1_mdio_read(tp, location);
	case RTL_GIGA_MAC_VER_28:
	case RTL_GIGA_MAC_VER_31:
		return r8168dp_2_mdio_read(tp, location);
	case RTL_GIGA_MAC_VER_40 ... RTL_GIGA_MAC_VER_51:
		return r8168g_mdio_read(tp, location);
	default:
		return r8169_mdio_read(tp, location);
	}
}

static void rtl_patchphy(struct rtl8169_private *tp, int reg_addr, int value)
{
	rtl_writephy(tp, reg_addr, rtl_readphy(tp, reg_addr) | value);
}

static void rtl_w0w1_phy(struct rtl8169_private *tp, int reg_addr, int p, int m)
{
	int val;

	val = rtl_readphy(tp, reg_addr);
	rtl_writephy(tp, reg_addr, (val & ~m) | p);
}

DECLARE_RTL_COND(rtl_ephyar_cond)
{
	return RTL_R32(tp, EPHYAR) & EPHYAR_FLAG;
}

static void rtl_ephy_write(struct rtl8169_private *tp, int reg_addr, int value)
{
	RTL_W32(tp, EPHYAR, EPHYAR_WRITE_CMD | (value & EPHYAR_DATA_MASK) |
		(reg_addr & EPHYAR_REG_MASK) << EPHYAR_REG_SHIFT);

	rtl_udelay_loop_wait_low(tp, &rtl_ephyar_cond, 10, 100);

	udelay(10);
}

static u16 rtl_ephy_read(struct rtl8169_private *tp, int reg_addr)
{
	RTL_W32(tp, EPHYAR, (reg_addr & EPHYAR_REG_MASK) << EPHYAR_REG_SHIFT);

	return rtl_udelay_loop_wait_high(tp, &rtl_ephyar_cond, 10, 100) ?
		RTL_R32(tp, EPHYAR) & EPHYAR_DATA_MASK : ~0;
}

DECLARE_RTL_COND(rtl_eriar_cond)
{
	return RTL_R32(tp, ERIAR) & ERIAR_FLAG;
}

static void _rtl_eri_write(struct rtl8169_private *tp, int addr, u32 mask,
			   u32 val, int type)
{
	BUG_ON((addr & 3) || (mask == 0));
	RTL_W32(tp, ERIDR, val);
	RTL_W32(tp, ERIAR, ERIAR_WRITE_CMD | type | mask | addr);

	rtl_udelay_loop_wait_low(tp, &rtl_eriar_cond, 100, 100);
}

static void rtl_eri_write(struct rtl8169_private *tp, int addr, u32 mask,
			  u32 val)
{
	_rtl_eri_write(tp, addr, mask, val, ERIAR_EXGMAC);
}

static u32 _rtl_eri_read(struct rtl8169_private *tp, int addr, int type)
{
	RTL_W32(tp, ERIAR, ERIAR_READ_CMD | type | ERIAR_MASK_1111 | addr);

	return rtl_udelay_loop_wait_high(tp, &rtl_eriar_cond, 100, 100) ?
		RTL_R32(tp, ERIDR) : ~0;
}

static u32 rtl_eri_read(struct rtl8169_private *tp, int addr)
{
	return _rtl_eri_read(tp, addr, ERIAR_EXGMAC);
}

static void rtl_w0w1_eri(struct rtl8169_private *tp, int addr, u32 mask, u32 p,
			 u32 m)
{
	u32 val;

	val = rtl_eri_read(tp, addr);
	rtl_eri_write(tp, addr, mask, (val & ~m) | p);
}

static void rtl_eri_set_bits(struct rtl8169_private *tp, int addr, u32 mask,
			     u32 p)
{
	rtl_w0w1_eri(tp, addr, mask, p, 0);
}

static void rtl_eri_clear_bits(struct rtl8169_private *tp, int addr, u32 mask,
			       u32 m)
{
	rtl_w0w1_eri(tp, addr, mask, 0, m);
}

static u32 r8168dp_ocp_read(struct rtl8169_private *tp, u8 mask, u16 reg)
{
	RTL_W32(tp, OCPAR, ((u32)mask & 0x0f) << 12 | (reg & 0x0fff));
	return rtl_udelay_loop_wait_high(tp, &rtl_ocpar_cond, 100, 20) ?
		RTL_R32(tp, OCPDR) : ~0;
}

static u32 r8168ep_ocp_read(struct rtl8169_private *tp, u8 mask, u16 reg)
{
	return _rtl_eri_read(tp, reg, ERIAR_OOB);
}

static void r8168dp_ocp_write(struct rtl8169_private *tp, u8 mask, u16 reg,
			      u32 data)
{
	RTL_W32(tp, OCPDR, data);
	RTL_W32(tp, OCPAR, OCPAR_FLAG | ((u32)mask & 0x0f) << 12 | (reg & 0x0fff));
	rtl_udelay_loop_wait_low(tp, &rtl_ocpar_cond, 100, 20);
}

static void r8168ep_ocp_write(struct rtl8169_private *tp, u8 mask, u16 reg,
			      u32 data)
{
	_rtl_eri_write(tp, reg, ((u32)mask & 0x0f) << ERIAR_MASK_SHIFT,
		       data, ERIAR_OOB);
}

static void r8168dp_oob_notify(struct rtl8169_private *tp, u8 cmd)
{
	rtl_eri_write(tp, 0xe8, ERIAR_MASK_0001, cmd);

	r8168dp_ocp_write(tp, 0x1, 0x30, 0x00000001);
}

#define OOB_CMD_RESET		0x00
#define OOB_CMD_DRIVER_START	0x05
#define OOB_CMD_DRIVER_STOP	0x06

static u16 rtl8168_get_ocp_reg(struct rtl8169_private *tp)
{
	return (tp->mac_version == RTL_GIGA_MAC_VER_31) ? 0xb8 : 0x10;
}

DECLARE_RTL_COND(rtl_dp_ocp_read_cond)
{
	u16 reg;

	reg = rtl8168_get_ocp_reg(tp);

	return r8168dp_ocp_read(tp, 0x0f, reg) & 0x00000800;
}

DECLARE_RTL_COND(rtl_ep_ocp_read_cond)
{
	return r8168ep_ocp_read(tp, 0x0f, 0x124) & 0x00000001;
}

DECLARE_RTL_COND(rtl_ocp_tx_cond)
{
	return RTL_R8(tp, IBISR0) & 0x20;
}

static void rtl8168ep_stop_cmac(struct rtl8169_private *tp)
{
	RTL_W8(tp, IBCR2, RTL_R8(tp, IBCR2) & ~0x01);
	rtl_msleep_loop_wait_high(tp, &rtl_ocp_tx_cond, 50, 2000);
	RTL_W8(tp, IBISR0, RTL_R8(tp, IBISR0) | 0x20);
	RTL_W8(tp, IBCR0, RTL_R8(tp, IBCR0) & ~0x01);
}

static void rtl8168dp_driver_start(struct rtl8169_private *tp)
{
	r8168dp_oob_notify(tp, OOB_CMD_DRIVER_START);
	rtl_msleep_loop_wait_high(tp, &rtl_dp_ocp_read_cond, 10, 10);
}

static void rtl8168ep_driver_start(struct rtl8169_private *tp)
{
	r8168ep_ocp_write(tp, 0x01, 0x180, OOB_CMD_DRIVER_START);
	r8168ep_ocp_write(tp, 0x01, 0x30,
			  r8168ep_ocp_read(tp, 0x01, 0x30) | 0x01);
	rtl_msleep_loop_wait_high(tp, &rtl_ep_ocp_read_cond, 10, 10);
}

static void rtl8168_driver_start(struct rtl8169_private *tp)
{
	switch (tp->mac_version) {
	case RTL_GIGA_MAC_VER_27:
	case RTL_GIGA_MAC_VER_28:
	case RTL_GIGA_MAC_VER_31:
		rtl8168dp_driver_start(tp);
		break;
	case RTL_GIGA_MAC_VER_49:
	case RTL_GIGA_MAC_VER_50:
	case RTL_GIGA_MAC_VER_51:
		rtl8168ep_driver_start(tp);
		break;
	default:
		BUG();
		break;
	}
}

static void rtl8168dp_driver_stop(struct rtl8169_private *tp)
{
	r8168dp_oob_notify(tp, OOB_CMD_DRIVER_STOP);
	rtl_msleep_loop_wait_low(tp, &rtl_dp_ocp_read_cond, 10, 10);
}

static void rtl8168ep_driver_stop(struct rtl8169_private *tp)
{
	rtl8168ep_stop_cmac(tp);
	r8168ep_ocp_write(tp, 0x01, 0x180, OOB_CMD_DRIVER_STOP);
	r8168ep_ocp_write(tp, 0x01, 0x30,
			  r8168ep_ocp_read(tp, 0x01, 0x30) | 0x01);
	rtl_msleep_loop_wait_low(tp, &rtl_ep_ocp_read_cond, 10, 10);
}

static void rtl8168_driver_stop(struct rtl8169_private *tp)
{
	switch (tp->mac_version) {
	case RTL_GIGA_MAC_VER_27:
	case RTL_GIGA_MAC_VER_28:
	case RTL_GIGA_MAC_VER_31:
		rtl8168dp_driver_stop(tp);
		break;
	case RTL_GIGA_MAC_VER_49:
	case RTL_GIGA_MAC_VER_50:
	case RTL_GIGA_MAC_VER_51:
		rtl8168ep_driver_stop(tp);
		break;
	default:
		BUG();
		break;
	}
}

static bool r8168dp_check_dash(struct rtl8169_private *tp)
{
	u16 reg = rtl8168_get_ocp_reg(tp);

	return !!(r8168dp_ocp_read(tp, 0x0f, reg) & 0x00008000);
}

static bool r8168ep_check_dash(struct rtl8169_private *tp)
{
	return !!(r8168ep_ocp_read(tp, 0x0f, 0x128) & 0x00000001);
}

static bool r8168_check_dash(struct rtl8169_private *tp)
{
	switch (tp->mac_version) {
	case RTL_GIGA_MAC_VER_27:
	case RTL_GIGA_MAC_VER_28:
	case RTL_GIGA_MAC_VER_31:
		return r8168dp_check_dash(tp);
	case RTL_GIGA_MAC_VER_49:
	case RTL_GIGA_MAC_VER_50:
	case RTL_GIGA_MAC_VER_51:
		return r8168ep_check_dash(tp);
	default:
		return false;
	}
}

static void rtl_reset_packet_filter(struct rtl8169_private *tp)
{
	rtl_eri_clear_bits(tp, 0xdc, ERIAR_MASK_0001, BIT(0));
	rtl_eri_set_bits(tp, 0xdc, ERIAR_MASK_0001, BIT(0));
}

DECLARE_RTL_COND(rtl_efusear_cond)
{
	return RTL_R32(tp, EFUSEAR) & EFUSEAR_FLAG;
}

static u8 rtl8168d_efuse_read(struct rtl8169_private *tp, int reg_addr)
{
	RTL_W32(tp, EFUSEAR, (reg_addr & EFUSEAR_REG_MASK) << EFUSEAR_REG_SHIFT);

	return rtl_udelay_loop_wait_high(tp, &rtl_efusear_cond, 100, 300) ?
		RTL_R32(tp, EFUSEAR) & EFUSEAR_DATA_MASK : ~0;
}

static void rtl_ack_events(struct rtl8169_private *tp, u16 bits)
{
	RTL_W16(tp, IntrStatus, bits);
}

static void rtl_irq_disable(struct rtl8169_private *tp)
{
	RTL_W16(tp, IntrMask, 0);
	tp->irq_enabled = 0;
}

#define RTL_EVENT_NAPI_RX	(RxOK | RxErr)
#define RTL_EVENT_NAPI_TX	(TxOK | TxErr)
#define RTL_EVENT_NAPI		(RTL_EVENT_NAPI_RX | RTL_EVENT_NAPI_TX)

static void rtl_irq_enable(struct rtl8169_private *tp)
{
	tp->irq_enabled = 1;
	RTL_W16(tp, IntrMask, tp->irq_mask);
}

static void rtl8169_irq_mask_and_ack(struct rtl8169_private *tp)
{
	rtl_irq_disable(tp);
	rtl_ack_events(tp, 0xffff);
	/* PCI commit */
	RTL_R8(tp, ChipCmd);
}

static void rtl_link_chg_patch(struct rtl8169_private *tp)
{
	struct net_device *dev = tp->dev;
	struct phy_device *phydev = tp->phydev;

	if (!netif_running(dev))
		return;

	if (tp->mac_version == RTL_GIGA_MAC_VER_34 ||
	    tp->mac_version == RTL_GIGA_MAC_VER_38) {
		if (phydev->speed == SPEED_1000) {
			rtl_eri_write(tp, 0x1bc, ERIAR_MASK_1111, 0x00000011);
			rtl_eri_write(tp, 0x1dc, ERIAR_MASK_1111, 0x00000005);
		} else if (phydev->speed == SPEED_100) {
			rtl_eri_write(tp, 0x1bc, ERIAR_MASK_1111, 0x0000001f);
			rtl_eri_write(tp, 0x1dc, ERIAR_MASK_1111, 0x00000005);
		} else {
			rtl_eri_write(tp, 0x1bc, ERIAR_MASK_1111, 0x0000001f);
			rtl_eri_write(tp, 0x1dc, ERIAR_MASK_1111, 0x0000003f);
		}
		rtl_reset_packet_filter(tp);
	} else if (tp->mac_version == RTL_GIGA_MAC_VER_35 ||
		   tp->mac_version == RTL_GIGA_MAC_VER_36) {
		if (phydev->speed == SPEED_1000) {
			rtl_eri_write(tp, 0x1bc, ERIAR_MASK_1111, 0x00000011);
			rtl_eri_write(tp, 0x1dc, ERIAR_MASK_1111, 0x00000005);
		} else {
			rtl_eri_write(tp, 0x1bc, ERIAR_MASK_1111, 0x0000001f);
			rtl_eri_write(tp, 0x1dc, ERIAR_MASK_1111, 0x0000003f);
		}
	} else if (tp->mac_version == RTL_GIGA_MAC_VER_37) {
		if (phydev->speed == SPEED_10) {
			rtl_eri_write(tp, 0x1d0, ERIAR_MASK_0011, 0x4d02);
			rtl_eri_write(tp, 0x1dc, ERIAR_MASK_0011, 0x0060a);
		} else {
			rtl_eri_write(tp, 0x1d0, ERIAR_MASK_0011, 0x0000);
		}
	}
}

#define WAKE_ANY (WAKE_PHY | WAKE_MAGIC | WAKE_UCAST | WAKE_BCAST | WAKE_MCAST)

static void rtl8169_get_wol(struct net_device *dev, struct ethtool_wolinfo *wol)
{
	struct rtl8169_private *tp = netdev_priv(dev);

	rtl_lock_work(tp);
	wol->supported = WAKE_ANY;
	wol->wolopts = tp->saved_wolopts;
	rtl_unlock_work(tp);
}

static void __rtl8169_set_wol(struct rtl8169_private *tp, u32 wolopts)
{
	unsigned int i, tmp;
	static const struct {
		u32 opt;
		u16 reg;
		u8  mask;
	} cfg[] = {
		{ WAKE_PHY,   Config3, LinkUp },
		{ WAKE_UCAST, Config5, UWF },
		{ WAKE_BCAST, Config5, BWF },
		{ WAKE_MCAST, Config5, MWF },
		{ WAKE_ANY,   Config5, LanWake },
		{ WAKE_MAGIC, Config3, MagicPacket }
	};
	u8 options;

	rtl_unlock_config_regs(tp);

	if (rtl_is_8168evl_up(tp)) {
		tmp = ARRAY_SIZE(cfg) - 1;
		if (wolopts & WAKE_MAGIC)
			rtl_eri_set_bits(tp, 0x0dc, ERIAR_MASK_0100,
					 MagicPacket_v2);
		else
			rtl_eri_clear_bits(tp, 0x0dc, ERIAR_MASK_0100,
					   MagicPacket_v2);
	} else {
		tmp = ARRAY_SIZE(cfg);
	}

	for (i = 0; i < tmp; i++) {
		options = RTL_R8(tp, cfg[i].reg) & ~cfg[i].mask;
		if (wolopts & cfg[i].opt)
			options |= cfg[i].mask;
		RTL_W8(tp, cfg[i].reg, options);
	}

	switch (tp->mac_version) {
	case RTL_GIGA_MAC_VER_02 ... RTL_GIGA_MAC_VER_17:
		options = RTL_R8(tp, Config1) & ~PMEnable;
		if (wolopts)
			options |= PMEnable;
		RTL_W8(tp, Config1, options);
		break;
	default:
		options = RTL_R8(tp, Config2) & ~PME_SIGNAL;
		if (wolopts)
			options |= PME_SIGNAL;
		RTL_W8(tp, Config2, options);
		break;
	}

	rtl_lock_config_regs(tp);

	device_set_wakeup_enable(tp_to_dev(tp), wolopts);
}

static int rtl8169_set_wol(struct net_device *dev, struct ethtool_wolinfo *wol)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	struct device *d = tp_to_dev(tp);

	if (wol->wolopts & ~WAKE_ANY)
		return -EINVAL;

	pm_runtime_get_noresume(d);

	rtl_lock_work(tp);

	tp->saved_wolopts = wol->wolopts;

	if (pm_runtime_active(d))
		__rtl8169_set_wol(tp, tp->saved_wolopts);

	rtl_unlock_work(tp);

	pm_runtime_put_noidle(d);

	return 0;
}

static void rtl8169_get_drvinfo(struct net_device *dev,
				struct ethtool_drvinfo *info)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	struct rtl_fw *rtl_fw = tp->rtl_fw;

	strlcpy(info->driver, MODULENAME, sizeof(info->driver));
	strlcpy(info->bus_info, pci_name(tp->pci_dev), sizeof(info->bus_info));
	BUILD_BUG_ON(sizeof(info->fw_version) < sizeof(rtl_fw->version));
	if (rtl_fw)
		strlcpy(info->fw_version, rtl_fw->version,
			sizeof(info->fw_version));
}

static int rtl8169_get_regs_len(struct net_device *dev)
{
	return R8169_REGS_SIZE;
}

static netdev_features_t rtl8169_fix_features(struct net_device *dev,
	netdev_features_t features)
{
	struct rtl8169_private *tp = netdev_priv(dev);

	if (dev->mtu > TD_MSS_MAX)
		features &= ~NETIF_F_ALL_TSO;

	if (dev->mtu > JUMBO_1K &&
	    tp->mac_version > RTL_GIGA_MAC_VER_06)
		features &= ~NETIF_F_IP_CSUM;

	return features;
}

static int rtl8169_set_features(struct net_device *dev,
				netdev_features_t features)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	u32 rx_config;

	rtl_lock_work(tp);

	rx_config = RTL_R32(tp, RxConfig);
	if (features & NETIF_F_RXALL)
		rx_config |= (AcceptErr | AcceptRunt);
	else
		rx_config &= ~(AcceptErr | AcceptRunt);

	RTL_W32(tp, RxConfig, rx_config);

	if (features & NETIF_F_RXCSUM)
		tp->cp_cmd |= RxChkSum;
	else
		tp->cp_cmd &= ~RxChkSum;

	if (features & NETIF_F_HW_VLAN_CTAG_RX)
		tp->cp_cmd |= RxVlan;
	else
		tp->cp_cmd &= ~RxVlan;

	RTL_W16(tp, CPlusCmd, tp->cp_cmd);
	RTL_R16(tp, CPlusCmd);

	rtl_unlock_work(tp);

	return 0;
}

static inline u32 rtl8169_tx_vlan_tag(struct sk_buff *skb)
{
	return (skb_vlan_tag_present(skb)) ?
		TxVlanTag | swab16(skb_vlan_tag_get(skb)) : 0x00;
}

static void rtl8169_rx_vlan_tag(struct RxDesc *desc, struct sk_buff *skb)
{
	u32 opts2 = le32_to_cpu(desc->opts2);

	if (opts2 & RxVlanTag)
		__vlan_hwaccel_put_tag(skb, htons(ETH_P_8021Q), swab16(opts2 & 0xffff));
}

static void rtl8169_get_regs(struct net_device *dev, struct ethtool_regs *regs,
			     void *p)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	u32 __iomem *data = tp->mmio_addr;
	u32 *dw = p;
	int i;

	rtl_lock_work(tp);
	for (i = 0; i < R8169_REGS_SIZE; i += 4)
		memcpy_fromio(dw++, data++, 4);
	rtl_unlock_work(tp);
}

static u32 rtl8169_get_msglevel(struct net_device *dev)
{
	struct rtl8169_private *tp = netdev_priv(dev);

	return tp->msg_enable;
}

static void rtl8169_set_msglevel(struct net_device *dev, u32 value)
{
	struct rtl8169_private *tp = netdev_priv(dev);

	tp->msg_enable = value;
}

static const char rtl8169_gstrings[][ETH_GSTRING_LEN] = {
	"tx_packets",
	"rx_packets",
	"tx_errors",
	"rx_errors",
	"rx_missed",
	"align_errors",
	"tx_single_collisions",
	"tx_multi_collisions",
	"unicast",
	"broadcast",
	"multicast",
	"tx_aborted",
	"tx_underrun",
};

static int rtl8169_get_sset_count(struct net_device *dev, int sset)
{
	switch (sset) {
	case ETH_SS_STATS:
		return ARRAY_SIZE(rtl8169_gstrings);
	default:
		return -EOPNOTSUPP;
	}
}

DECLARE_RTL_COND(rtl_counters_cond)
{
	return RTL_R32(tp, CounterAddrLow) & (CounterReset | CounterDump);
}

static bool rtl8169_do_counters(struct rtl8169_private *tp, u32 counter_cmd)
{
	dma_addr_t paddr = tp->counters_phys_addr;
	u32 cmd;

	RTL_W32(tp, CounterAddrHigh, (u64)paddr >> 32);
	RTL_R32(tp, CounterAddrHigh);
	cmd = (u64)paddr & DMA_BIT_MASK(32);
	RTL_W32(tp, CounterAddrLow, cmd);
	RTL_W32(tp, CounterAddrLow, cmd | counter_cmd);

	return rtl_udelay_loop_wait_low(tp, &rtl_counters_cond, 10, 1000);
}

static bool rtl8169_reset_counters(struct rtl8169_private *tp)
{
	/*
	 * Versions prior to RTL_GIGA_MAC_VER_19 don't support resetting the
	 * tally counters.
	 */
	if (tp->mac_version < RTL_GIGA_MAC_VER_19)
		return true;

	return rtl8169_do_counters(tp, CounterReset);
}

static bool rtl8169_update_counters(struct rtl8169_private *tp)
{
	u8 val = RTL_R8(tp, ChipCmd);

	/*
	 * Some chips are unable to dump tally counters when the receiver
	 * is disabled. If 0xff chip may be in a PCI power-save state.
	 */
	if (!(val & CmdRxEnb) || val == 0xff)
		return true;

	return rtl8169_do_counters(tp, CounterDump);
}

static bool rtl8169_init_counter_offsets(struct rtl8169_private *tp)
{
	struct rtl8169_counters *counters = tp->counters;
	bool ret = false;

	/*
	 * rtl8169_init_counter_offsets is called from rtl_open.  On chip
	 * versions prior to RTL_GIGA_MAC_VER_19 the tally counters are only
	 * reset by a power cycle, while the counter values collected by the
	 * driver are reset at every driver unload/load cycle.
	 *
	 * To make sure the HW values returned by @get_stats64 match the SW
	 * values, we collect the initial values at first open(*) and use them
	 * as offsets to normalize the values returned by @get_stats64.
	 *
	 * (*) We can't call rtl8169_init_counter_offsets from rtl_init_one
	 * for the reason stated in rtl8169_update_counters; CmdRxEnb is only
	 * set at open time by rtl_hw_start.
	 */

	if (tp->tc_offset.inited)
		return true;

	/* If both, reset and update fail, propagate to caller. */
	if (rtl8169_reset_counters(tp))
		ret = true;

	if (rtl8169_update_counters(tp))
		ret = true;

	tp->tc_offset.tx_errors = counters->tx_errors;
	tp->tc_offset.tx_multi_collision = counters->tx_multi_collision;
	tp->tc_offset.tx_aborted = counters->tx_aborted;
	tp->tc_offset.inited = true;

	return ret;
}

static void rtl8169_get_ethtool_stats(struct net_device *dev,
				      struct ethtool_stats *stats, u64 *data)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	struct device *d = tp_to_dev(tp);
	struct rtl8169_counters *counters = tp->counters;

	ASSERT_RTNL();

	pm_runtime_get_noresume(d);

	if (pm_runtime_active(d))
		rtl8169_update_counters(tp);

	pm_runtime_put_noidle(d);

	data[0] = le64_to_cpu(counters->tx_packets);
	data[1] = le64_to_cpu(counters->rx_packets);
	data[2] = le64_to_cpu(counters->tx_errors);
	data[3] = le32_to_cpu(counters->rx_errors);
	data[4] = le16_to_cpu(counters->rx_missed);
	data[5] = le16_to_cpu(counters->align_errors);
	data[6] = le32_to_cpu(counters->tx_one_collision);
	data[7] = le32_to_cpu(counters->tx_multi_collision);
	data[8] = le64_to_cpu(counters->rx_unicast);
	data[9] = le64_to_cpu(counters->rx_broadcast);
	data[10] = le32_to_cpu(counters->rx_multicast);
	data[11] = le16_to_cpu(counters->tx_aborted);
	data[12] = le16_to_cpu(counters->tx_underun);
}

static void rtl8169_get_strings(struct net_device *dev, u32 stringset, u8 *data)
{
	switch(stringset) {
	case ETH_SS_STATS:
		memcpy(data, *rtl8169_gstrings, sizeof(rtl8169_gstrings));
		break;
	}
}

/*
 * Interrupt coalescing
 *
 * > 1 - the availability of the IntrMitigate (0xe2) register through the
 * >     8169, 8168 and 810x line of chipsets
 *
 * 8169, 8168, and 8136(810x) serial chipsets support it.
 *
 * > 2 - the Tx timer unit at gigabit speed
 *
 * The unit of the timer depends on both the speed and the setting of CPlusCmd
 * (0xe0) bit 1 and bit 0.
 *
 * For 8169
 * bit[1:0] \ speed        1000M           100M            10M
 * 0 0                     320ns           2.56us          40.96us
 * 0 1                     2.56us          20.48us         327.7us
 * 1 0                     5.12us          40.96us         655.4us
 * 1 1                     10.24us         81.92us         1.31ms
 *
 * For the other
 * bit[1:0] \ speed        1000M           100M            10M
 * 0 0                     5us             2.56us          40.96us
 * 0 1                     40us            20.48us         327.7us
 * 1 0                     80us            40.96us         655.4us
 * 1 1                     160us           81.92us         1.31ms
 */

/* rx/tx scale factors for one particular CPlusCmd[0:1] value */
struct rtl_coalesce_scale {
	/* Rx / Tx */
	u32 nsecs[2];
};

/* rx/tx scale factors for all CPlusCmd[0:1] cases */
struct rtl_coalesce_info {
	u32 speed;
	struct rtl_coalesce_scale scalev[4];	/* each CPlusCmd[0:1] case */
};

/* produce (r,t) pairs with each being in series of *1, *8, *8*2, *8*2*2 */
#define rxtx_x1822(r, t) {		\
	{{(r),		(t)}},		\
	{{(r)*8,	(t)*8}},	\
	{{(r)*8*2,	(t)*8*2}},	\
	{{(r)*8*2*2,	(t)*8*2*2}},	\
}
static const struct rtl_coalesce_info rtl_coalesce_info_8169[] = {
	/* speed	delays:     rx00   tx00	*/
	{ SPEED_10,	rxtx_x1822(40960, 40960)	},
	{ SPEED_100,	rxtx_x1822( 2560,  2560)	},
	{ SPEED_1000,	rxtx_x1822(  320,   320)	},
	{ 0 },
};

static const struct rtl_coalesce_info rtl_coalesce_info_8168_8136[] = {
	/* speed	delays:     rx00   tx00	*/
	{ SPEED_10,	rxtx_x1822(40960, 40960)	},
	{ SPEED_100,	rxtx_x1822( 2560,  2560)	},
	{ SPEED_1000,	rxtx_x1822( 5000,  5000)	},
	{ 0 },
};
#undef rxtx_x1822

/* get rx/tx scale vector corresponding to current speed */
static const struct rtl_coalesce_info *rtl_coalesce_info(struct net_device *dev)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	const struct rtl_coalesce_info *ci;

	if (tp->mac_version <= RTL_GIGA_MAC_VER_06)
		ci = rtl_coalesce_info_8169;
	else
		ci = rtl_coalesce_info_8168_8136;

	for (; ci->speed; ci++) {
		if (tp->phydev->speed == ci->speed)
			return ci;
	}

	return ERR_PTR(-ELNRNG);
}

static int rtl_get_coalesce(struct net_device *dev, struct ethtool_coalesce *ec)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	const struct rtl_coalesce_info *ci;
	const struct rtl_coalesce_scale *scale;
	struct {
		u32 *max_frames;
		u32 *usecs;
	} coal_settings [] = {
		{ &ec->rx_max_coalesced_frames, &ec->rx_coalesce_usecs },
		{ &ec->tx_max_coalesced_frames, &ec->tx_coalesce_usecs }
	}, *p = coal_settings;
	int i;
	u16 w;

	memset(ec, 0, sizeof(*ec));

	/* get rx/tx scale corresponding to current speed and CPlusCmd[0:1] */
	ci = rtl_coalesce_info(dev);
	if (IS_ERR(ci))
		return PTR_ERR(ci);

	scale = &ci->scalev[tp->cp_cmd & INTT_MASK];

	/* read IntrMitigate and adjust according to scale */
	for (w = RTL_R16(tp, IntrMitigate); w; w >>= RTL_COALESCE_SHIFT, p++) {
		*p->max_frames = (w & RTL_COALESCE_MASK) << 2;
		w >>= RTL_COALESCE_SHIFT;
		*p->usecs = w & RTL_COALESCE_MASK;
	}

	for (i = 0; i < 2; i++) {
		p = coal_settings + i;
		*p->usecs = (*p->usecs * scale->nsecs[i]) / 1000;

		/*
		 * ethtool_coalesce says it is illegal to set both usecs and
		 * max_frames to 0.
		 */
		if (!*p->usecs && !*p->max_frames)
			*p->max_frames = 1;
	}

	return 0;
}

/* choose appropriate scale factor and CPlusCmd[0:1] for (speed, nsec) */
static const struct rtl_coalesce_scale *rtl_coalesce_choose_scale(
			struct net_device *dev, u32 nsec, u16 *cp01)
{
	const struct rtl_coalesce_info *ci;
	u16 i;

	ci = rtl_coalesce_info(dev);
	if (IS_ERR(ci))
		return ERR_CAST(ci);

	for (i = 0; i < 4; i++) {
		u32 rxtx_maxscale = max(ci->scalev[i].nsecs[0],
					ci->scalev[i].nsecs[1]);
		if (nsec <= rxtx_maxscale * RTL_COALESCE_T_MAX) {
			*cp01 = i;
			return &ci->scalev[i];
		}
	}

	return ERR_PTR(-EINVAL);
}

static int rtl_set_coalesce(struct net_device *dev, struct ethtool_coalesce *ec)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	const struct rtl_coalesce_scale *scale;
	struct {
		u32 frames;
		u32 usecs;
	} coal_settings [] = {
		{ ec->rx_max_coalesced_frames, ec->rx_coalesce_usecs },
		{ ec->tx_max_coalesced_frames, ec->tx_coalesce_usecs }
	}, *p = coal_settings;
	u16 w = 0, cp01;
	int i;

	scale = rtl_coalesce_choose_scale(dev,
			max(p[0].usecs, p[1].usecs) * 1000, &cp01);
	if (IS_ERR(scale))
		return PTR_ERR(scale);

	for (i = 0; i < 2; i++, p++) {
		u32 units;

		/*
		 * accept max_frames=1 we returned in rtl_get_coalesce.
		 * accept it not only when usecs=0 because of e.g. the following scenario:
		 *
		 * - both rx_usecs=0 & rx_frames=0 in hardware (no delay on RX)
		 * - rtl_get_coalesce returns rx_usecs=0, rx_frames=1
		 * - then user does `ethtool -C eth0 rx-usecs 100`
		 *
		 * since ethtool sends to kernel whole ethtool_coalesce
		 * settings, if we do not handle rx_usecs=!0, rx_frames=1
		 * we'll reject it below in `frames % 4 != 0`.
		 */
		if (p->frames == 1) {
			p->frames = 0;
		}

		units = p->usecs * 1000 / scale->nsecs[i];
		if (p->frames > RTL_COALESCE_FRAME_MAX || p->frames % 4)
			return -EINVAL;

		w <<= RTL_COALESCE_SHIFT;
		w |= units;
		w <<= RTL_COALESCE_SHIFT;
		w |= p->frames >> 2;
	}

	rtl_lock_work(tp);

	RTL_W16(tp, IntrMitigate, swab16(w));

	tp->cp_cmd = (tp->cp_cmd & ~INTT_MASK) | cp01;
	RTL_W16(tp, CPlusCmd, tp->cp_cmd);
	RTL_R16(tp, CPlusCmd);

	rtl_unlock_work(tp);

	return 0;
}

static int rtl_get_eee_supp(struct rtl8169_private *tp)
{
	struct phy_device *phydev = tp->phydev;
	int ret;

	switch (tp->mac_version) {
	case RTL_GIGA_MAC_VER_34:
	case RTL_GIGA_MAC_VER_35:
	case RTL_GIGA_MAC_VER_36:
	case RTL_GIGA_MAC_VER_38:
		ret = phy_read_mmd(phydev, MDIO_MMD_PCS, MDIO_PCS_EEE_ABLE);
		break;
	case RTL_GIGA_MAC_VER_40 ... RTL_GIGA_MAC_VER_51:
		ret = phy_read_paged(phydev, 0x0a5c, 0x12);
		break;
	default:
		ret = -EPROTONOSUPPORT;
		break;
	}

	return ret;
}

static int rtl_get_eee_lpadv(struct rtl8169_private *tp)
{
	struct phy_device *phydev = tp->phydev;
	int ret;

	switch (tp->mac_version) {
	case RTL_GIGA_MAC_VER_34:
	case RTL_GIGA_MAC_VER_35:
	case RTL_GIGA_MAC_VER_36:
	case RTL_GIGA_MAC_VER_38:
		ret = phy_read_mmd(phydev, MDIO_MMD_AN, MDIO_AN_EEE_LPABLE);
		break;
	case RTL_GIGA_MAC_VER_40 ... RTL_GIGA_MAC_VER_51:
		ret = phy_read_paged(phydev, 0x0a5d, 0x11);
		break;
	default:
		ret = -EPROTONOSUPPORT;
		break;
	}

	return ret;
}

static int rtl_get_eee_adv(struct rtl8169_private *tp)
{
	struct phy_device *phydev = tp->phydev;
	int ret;

	switch (tp->mac_version) {
	case RTL_GIGA_MAC_VER_34:
	case RTL_GIGA_MAC_VER_35:
	case RTL_GIGA_MAC_VER_36:
	case RTL_GIGA_MAC_VER_38:
		ret = phy_read_mmd(phydev, MDIO_MMD_AN, MDIO_AN_EEE_ADV);
		break;
	case RTL_GIGA_MAC_VER_40 ... RTL_GIGA_MAC_VER_51:
		ret = phy_read_paged(phydev, 0x0a5d, 0x10);
		break;
	default:
		ret = -EPROTONOSUPPORT;
		break;
	}

	return ret;
}

static int rtl_set_eee_adv(struct rtl8169_private *tp, int val)
{
	struct phy_device *phydev = tp->phydev;
	int ret = 0;

	switch (tp->mac_version) {
	case RTL_GIGA_MAC_VER_34:
	case RTL_GIGA_MAC_VER_35:
	case RTL_GIGA_MAC_VER_36:
	case RTL_GIGA_MAC_VER_38:
		ret = phy_write_mmd(phydev, MDIO_MMD_AN, MDIO_AN_EEE_ADV, val);
		break;
	case RTL_GIGA_MAC_VER_40 ... RTL_GIGA_MAC_VER_51:
		phy_write_paged(phydev, 0x0a5d, 0x10, val);
		break;
	default:
		ret = -EPROTONOSUPPORT;
		break;
	}

	return ret;
}

static int rtl8169_get_eee(struct net_device *dev, struct ethtool_eee *data)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	struct device *d = tp_to_dev(tp);
	int ret;

	pm_runtime_get_noresume(d);

	if (!pm_runtime_active(d)) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	/* Get Supported EEE */
	ret = rtl_get_eee_supp(tp);
	if (ret < 0)
		goto out;
	data->supported = mmd_eee_cap_to_ethtool_sup_t(ret);

	/* Get advertisement EEE */
	ret = rtl_get_eee_adv(tp);
	if (ret < 0)
		goto out;
	data->advertised = mmd_eee_adv_to_ethtool_adv_t(ret);
	data->eee_enabled = !!data->advertised;

	/* Get LP advertisement EEE */
	ret = rtl_get_eee_lpadv(tp);
	if (ret < 0)
		goto out;
	data->lp_advertised = mmd_eee_adv_to_ethtool_adv_t(ret);
	data->eee_active = !!(data->advertised & data->lp_advertised);
out:
	pm_runtime_put_noidle(d);
	return ret < 0 ? ret : 0;
}

static int rtl8169_set_eee(struct net_device *dev, struct ethtool_eee *data)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	struct device *d = tp_to_dev(tp);
	int old_adv, adv = 0, cap, ret;

	pm_runtime_get_noresume(d);

	if (!dev->phydev || !pm_runtime_active(d)) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	if (dev->phydev->autoneg == AUTONEG_DISABLE ||
	    dev->phydev->duplex != DUPLEX_FULL) {
		ret = -EPROTONOSUPPORT;
		goto out;
	}

	/* Get Supported EEE */
	ret = rtl_get_eee_supp(tp);
	if (ret < 0)
		goto out;
	cap = ret;

	ret = rtl_get_eee_adv(tp);
	if (ret < 0)
		goto out;
	old_adv = ret;

	if (data->eee_enabled) {
		adv = !data->advertised ? cap :
		      ethtool_adv_to_mmd_eee_adv_t(data->advertised) & cap;
		/* Mask prohibited EEE modes */
		adv &= ~dev->phydev->eee_broken_modes;
	}

	if (old_adv != adv) {
		ret = rtl_set_eee_adv(tp, adv);
		if (ret < 0)
			goto out;

		/* Restart autonegotiation so the new modes get sent to the
		 * link partner.
		 */
		ret = phy_restart_aneg(dev->phydev);
	}

out:
	pm_runtime_put_noidle(d);
	return ret < 0 ? ret : 0;
}

static const struct ethtool_ops rtl8169_ethtool_ops = {
	.get_drvinfo		= rtl8169_get_drvinfo,
	.get_regs_len		= rtl8169_get_regs_len,
	.get_link		= ethtool_op_get_link,
	.get_coalesce		= rtl_get_coalesce,
	.set_coalesce		= rtl_set_coalesce,
	.get_msglevel		= rtl8169_get_msglevel,
	.set_msglevel		= rtl8169_set_msglevel,
	.get_regs		= rtl8169_get_regs,
	.get_wol		= rtl8169_get_wol,
	.set_wol		= rtl8169_set_wol,
	.get_strings		= rtl8169_get_strings,
	.get_sset_count		= rtl8169_get_sset_count,
	.get_ethtool_stats	= rtl8169_get_ethtool_stats,
	.get_ts_info		= ethtool_op_get_ts_info,
	.nway_reset		= phy_ethtool_nway_reset,
	.get_eee		= rtl8169_get_eee,
	.set_eee		= rtl8169_set_eee,
	.get_link_ksettings	= phy_ethtool_get_link_ksettings,
	.set_link_ksettings	= phy_ethtool_set_link_ksettings,
};

static void rtl_enable_eee(struct rtl8169_private *tp)
{
	int supported = rtl_get_eee_supp(tp);

	if (supported > 0)
		rtl_set_eee_adv(tp, supported);
}

static void rtl8169_get_mac_version(struct rtl8169_private *tp)
{
	/*
	 * The driver currently handles the 8168Bf and the 8168Be identically
	 * but they can be identified more specifically through the test below
	 * if needed:
	 *
	 * (RTL_R32(tp, TxConfig) & 0x700000) == 0x500000 ? 8168Bf : 8168Be
	 *
	 * Same thing for the 8101Eb and the 8101Ec:
	 *
	 * (RTL_R32(tp, TxConfig) & 0x700000) == 0x200000 ? 8101Eb : 8101Ec
	 */
	static const struct rtl_mac_info {
		u16 mask;
		u16 val;
		u16 mac_version;
	} mac_info[] = {
		/* 8168EP family. */
		{ 0x7cf, 0x502,	RTL_GIGA_MAC_VER_51 },
		{ 0x7cf, 0x501,	RTL_GIGA_MAC_VER_50 },
		{ 0x7cf, 0x500,	RTL_GIGA_MAC_VER_49 },

		/* 8168H family. */
		{ 0x7cf, 0x541,	RTL_GIGA_MAC_VER_46 },
		{ 0x7cf, 0x540,	RTL_GIGA_MAC_VER_45 },

		/* 8168G family. */
		{ 0x7cf, 0x5c8,	RTL_GIGA_MAC_VER_44 },
		{ 0x7cf, 0x509,	RTL_GIGA_MAC_VER_42 },
		{ 0x7cf, 0x4c1,	RTL_GIGA_MAC_VER_41 },
		{ 0x7cf, 0x4c0,	RTL_GIGA_MAC_VER_40 },

		/* 8168F family. */
		{ 0x7c8, 0x488,	RTL_GIGA_MAC_VER_38 },
		{ 0x7cf, 0x481,	RTL_GIGA_MAC_VER_36 },
		{ 0x7cf, 0x480,	RTL_GIGA_MAC_VER_35 },

		/* 8168E family. */
		{ 0x7c8, 0x2c8,	RTL_GIGA_MAC_VER_34 },
		{ 0x7cf, 0x2c1,	RTL_GIGA_MAC_VER_32 },
		{ 0x7c8, 0x2c0,	RTL_GIGA_MAC_VER_33 },

		/* 8168D family. */
		{ 0x7cf, 0x281,	RTL_GIGA_MAC_VER_25 },
		{ 0x7c8, 0x280,	RTL_GIGA_MAC_VER_26 },

		/* 8168DP family. */
		{ 0x7cf, 0x288,	RTL_GIGA_MAC_VER_27 },
		{ 0x7cf, 0x28a,	RTL_GIGA_MAC_VER_28 },
		{ 0x7cf, 0x28b,	RTL_GIGA_MAC_VER_31 },

		/* 8168C family. */
		{ 0x7cf, 0x3c9,	RTL_GIGA_MAC_VER_23 },
		{ 0x7cf, 0x3c8,	RTL_GIGA_MAC_VER_18 },
		{ 0x7c8, 0x3c8,	RTL_GIGA_MAC_VER_24 },
		{ 0x7cf, 0x3c0,	RTL_GIGA_MAC_VER_19 },
		{ 0x7cf, 0x3c2,	RTL_GIGA_MAC_VER_20 },
		{ 0x7cf, 0x3c3,	RTL_GIGA_MAC_VER_21 },
		{ 0x7c8, 0x3c0,	RTL_GIGA_MAC_VER_22 },

		/* 8168B family. */
		{ 0x7cf, 0x380,	RTL_GIGA_MAC_VER_12 },
		{ 0x7c8, 0x380,	RTL_GIGA_MAC_VER_17 },
		{ 0x7c8, 0x300,	RTL_GIGA_MAC_VER_11 },

		/* 8101 family. */
		{ 0x7c8, 0x448,	RTL_GIGA_MAC_VER_39 },
		{ 0x7c8, 0x440,	RTL_GIGA_MAC_VER_37 },
		{ 0x7cf, 0x409,	RTL_GIGA_MAC_VER_29 },
		{ 0x7c8, 0x408,	RTL_GIGA_MAC_VER_30 },
		{ 0x7cf, 0x349,	RTL_GIGA_MAC_VER_08 },
		{ 0x7cf, 0x249,	RTL_GIGA_MAC_VER_08 },
		{ 0x7cf, 0x348,	RTL_GIGA_MAC_VER_07 },
		{ 0x7cf, 0x248,	RTL_GIGA_MAC_VER_07 },
		{ 0x7cf, 0x340,	RTL_GIGA_MAC_VER_13 },
		{ 0x7cf, 0x343,	RTL_GIGA_MAC_VER_10 },
		{ 0x7cf, 0x342,	RTL_GIGA_MAC_VER_16 },
		{ 0x7c8, 0x348,	RTL_GIGA_MAC_VER_09 },
		{ 0x7c8, 0x248,	RTL_GIGA_MAC_VER_09 },
		{ 0x7c8, 0x340,	RTL_GIGA_MAC_VER_16 },
		/* FIXME: where did these entries come from ? -- FR */
		{ 0xfc8, 0x388,	RTL_GIGA_MAC_VER_15 },
		{ 0xfc8, 0x308,	RTL_GIGA_MAC_VER_14 },

		/* 8110 family. */
		{ 0xfc8, 0x980,	RTL_GIGA_MAC_VER_06 },
		{ 0xfc8, 0x180,	RTL_GIGA_MAC_VER_05 },
		{ 0xfc8, 0x100,	RTL_GIGA_MAC_VER_04 },
		{ 0xfc8, 0x040,	RTL_GIGA_MAC_VER_03 },
		{ 0xfc8, 0x008,	RTL_GIGA_MAC_VER_02 },

		/* Catch-all */
		{ 0x000, 0x000,	RTL_GIGA_MAC_NONE   }
	};
	const struct rtl_mac_info *p = mac_info;
	u16 reg = RTL_R32(tp, TxConfig) >> 20;

	while ((reg & p->mask) != p->val)
		p++;
	tp->mac_version = p->mac_version;

	if (tp->mac_version == RTL_GIGA_MAC_NONE) {
		dev_err(tp_to_dev(tp), "unknown chip XID %03x\n", reg & 0xfcf);
	} else if (!tp->supports_gmii) {
		if (tp->mac_version == RTL_GIGA_MAC_VER_42)
			tp->mac_version = RTL_GIGA_MAC_VER_43;
		else if (tp->mac_version == RTL_GIGA_MAC_VER_45)
			tp->mac_version = RTL_GIGA_MAC_VER_47;
		else if (tp->mac_version == RTL_GIGA_MAC_VER_46)
			tp->mac_version = RTL_GIGA_MAC_VER_48;
	}
}

struct phy_reg {
	u16 reg;
	u16 val;
};

static void __rtl_writephy_batch(struct rtl8169_private *tp,
				 const struct phy_reg *regs, int len)
{
	while (len-- > 0) {
		rtl_writephy(tp, regs->reg, regs->val);
		regs++;
	}
}

#define rtl_writephy_batch(tp, a) __rtl_writephy_batch(tp, a, ARRAY_SIZE(a))

static void rtl_release_firmware(struct rtl8169_private *tp)
{
	if (tp->rtl_fw) {
		rtl_fw_release_firmware(tp->rtl_fw);
		kfree(tp->rtl_fw);
		tp->rtl_fw = NULL;
	}
}

static void rtl_apply_firmware(struct rtl8169_private *tp)
{
	/* TODO: release firmware if rtl_fw_write_firmware signals failure. */
	if (tp->rtl_fw)
		rtl_fw_write_firmware(tp, tp->rtl_fw);
}

static void rtl_apply_firmware_cond(struct rtl8169_private *tp, u8 reg, u16 val)
{
	if (rtl_readphy(tp, reg) != val)
		netif_warn(tp, hw, tp->dev, "chipset not ready for firmware\n");
	else
		rtl_apply_firmware(tp);
}

static void rtl8168_config_eee_mac(struct rtl8169_private *tp)
{
	/* Adjust EEE LED frequency */
	if (tp->mac_version != RTL_GIGA_MAC_VER_38)
		RTL_W8(tp, EEE_LED, RTL_R8(tp, EEE_LED) & ~0x07);

	rtl_eri_set_bits(tp, 0x1b0, ERIAR_MASK_1111, 0x0003);
}

static void rtl8168f_config_eee_phy(struct rtl8169_private *tp)
{
	struct phy_device *phydev = tp->phydev;

	phy_write(phydev, 0x1f, 0x0007);
	phy_write(phydev, 0x1e, 0x0020);
	phy_set_bits(phydev, 0x15, BIT(8));

	phy_write(phydev, 0x1f, 0x0005);
	phy_write(phydev, 0x05, 0x8b85);
	phy_set_bits(phydev, 0x06, BIT(13));

	phy_write(phydev, 0x1f, 0x0000);
}

static void rtl8168g_config_eee_phy(struct rtl8169_private *tp)
{
	phy_modify_paged(tp->phydev, 0x0a43, 0x11, 0, BIT(4));
}

static void rtl8169s_hw_phy_config(struct rtl8169_private *tp)
{
	static const struct phy_reg phy_reg_init[] = {
		{ 0x1f, 0x0001 },
		{ 0x06, 0x006e },
		{ 0x08, 0x0708 },
		{ 0x15, 0x4000 },
		{ 0x18, 0x65c7 },

		{ 0x1f, 0x0001 },
		{ 0x03, 0x00a1 },
		{ 0x02, 0x0008 },
		{ 0x01, 0x0120 },
		{ 0x00, 0x1000 },
		{ 0x04, 0x0800 },
		{ 0x04, 0x0000 },

		{ 0x03, 0xff41 },
		{ 0x02, 0xdf60 },
		{ 0x01, 0x0140 },
		{ 0x00, 0x0077 },
		{ 0x04, 0x7800 },
		{ 0x04, 0x7000 },

		{ 0x03, 0x802f },
		{ 0x02, 0x4f02 },
		{ 0x01, 0x0409 },
		{ 0x00, 0xf0f9 },
		{ 0x04, 0x9800 },
		{ 0x04, 0x9000 },

		{ 0x03, 0xdf01 },
		{ 0x02, 0xdf20 },
		{ 0x01, 0xff95 },
		{ 0x00, 0xba00 },
		{ 0x04, 0xa800 },
		{ 0x04, 0xa000 },

		{ 0x03, 0xff41 },
		{ 0x02, 0xdf20 },
		{ 0x01, 0x0140 },
		{ 0x00, 0x00bb },
		{ 0x04, 0xb800 },
		{ 0x04, 0xb000 },

		{ 0x03, 0xdf41 },
		{ 0x02, 0xdc60 },
		{ 0x01, 0x6340 },
		{ 0x00, 0x007d },
		{ 0x04, 0xd800 },
		{ 0x04, 0xd000 },

		{ 0x03, 0xdf01 },
		{ 0x02, 0xdf20 },
		{ 0x01, 0x100a },
		{ 0x00, 0xa0ff },
		{ 0x04, 0xf800 },
		{ 0x04, 0xf000 },

		{ 0x1f, 0x0000 },
		{ 0x0b, 0x0000 },
		{ 0x00, 0x9200 }
	};

	rtl_writephy_batch(tp, phy_reg_init);
}

static void rtl8169sb_hw_phy_config(struct rtl8169_private *tp)
{
	static const struct phy_reg phy_reg_init[] = {
		{ 0x1f, 0x0002 },
		{ 0x01, 0x90d0 },
		{ 0x1f, 0x0000 }
	};

	rtl_writephy_batch(tp, phy_reg_init);
}

static void rtl8169scd_hw_phy_config_quirk(struct rtl8169_private *tp)
{
	struct pci_dev *pdev = tp->pci_dev;

	if ((pdev->subsystem_vendor != PCI_VENDOR_ID_GIGABYTE) ||
	    (pdev->subsystem_device != 0xe000))
		return;

	rtl_writephy(tp, 0x1f, 0x0001);
	rtl_writephy(tp, 0x10, 0xf01b);
	rtl_writephy(tp, 0x1f, 0x0000);
}

static void rtl8169scd_hw_phy_config(struct rtl8169_private *tp)
{
	static const struct phy_reg phy_reg_init[] = {
		{ 0x1f, 0x0001 },
		{ 0x04, 0x0000 },
		{ 0x03, 0x00a1 },
		{ 0x02, 0x0008 },
		{ 0x01, 0x0120 },
		{ 0x00, 0x1000 },
		{ 0x04, 0x0800 },
		{ 0x04, 0x9000 },
		{ 0x03, 0x802f },
		{ 0x02, 0x4f02 },
		{ 0x01, 0x0409 },
		{ 0x00, 0xf099 },
		{ 0x04, 0x9800 },
		{ 0x04, 0xa000 },
		{ 0x03, 0xdf01 },
		{ 0x02, 0xdf20 },
		{ 0x01, 0xff95 },
		{ 0x00, 0xba00 },
		{ 0x04, 0xa800 },
		{ 0x04, 0xf000 },
		{ 0x03, 0xdf01 },
		{ 0x02, 0xdf20 },
		{ 0x01, 0x101a },
		{ 0x00, 0xa0ff },
		{ 0x04, 0xf800 },
		{ 0x04, 0x0000 },
		{ 0x1f, 0x0000 },

		{ 0x1f, 0x0001 },
		{ 0x10, 0xf41b },
		{ 0x14, 0xfb54 },
		{ 0x18, 0xf5c7 },
		{ 0x1f, 0x0000 },

		{ 0x1f, 0x0001 },
		{ 0x17, 0x0cc0 },
		{ 0x1f, 0x0000 }
	};

	rtl_writephy_batch(tp, phy_reg_init);

	rtl8169scd_hw_phy_config_quirk(tp);
}

static void rtl8169sce_hw_phy_config(struct rtl8169_private *tp)
{
	static const struct phy_reg phy_reg_init[] = {
		{ 0x1f, 0x0001 },
		{ 0x04, 0x0000 },
		{ 0x03, 0x00a1 },
		{ 0x02, 0x0008 },
		{ 0x01, 0x0120 },
		{ 0x00, 0x1000 },
		{ 0x04, 0x0800 },
		{ 0x04, 0x9000 },
		{ 0x03, 0x802f },
		{ 0x02, 0x4f02 },
		{ 0x01, 0x0409 },
		{ 0x00, 0xf099 },
		{ 0x04, 0x9800 },
		{ 0x04, 0xa000 },
		{ 0x03, 0xdf01 },
		{ 0x02, 0xdf20 },
		{ 0x01, 0xff95 },
		{ 0x00, 0xba00 },
		{ 0x04, 0xa800 },
		{ 0x04, 0xf000 },
		{ 0x03, 0xdf01 },
		{ 0x02, 0xdf20 },
		{ 0x01, 0x101a },
		{ 0x00, 0xa0ff },
		{ 0x04, 0xf800 },
		{ 0x04, 0x0000 },
		{ 0x1f, 0x0000 },

		{ 0x1f, 0x0001 },
		{ 0x0b, 0x8480 },
		{ 0x1f, 0x0000 },

		{ 0x1f, 0x0001 },
		{ 0x18, 0x67c7 },
		{ 0x04, 0x2000 },
		{ 0x03, 0x002f },
		{ 0x02, 0x4360 },
		{ 0x01, 0x0109 },
		{ 0x00, 0x3022 },
		{ 0x04, 0x2800 },
		{ 0x1f, 0x0000 },

		{ 0x1f, 0x0001 },
		{ 0x17, 0x0cc0 },
		{ 0x1f, 0x0000 }
	};

	rtl_writephy_batch(tp, phy_reg_init);
}

static void rtl8168bb_hw_phy_config(struct rtl8169_private *tp)
{
	static const struct phy_reg phy_reg_init[] = {
		{ 0x10, 0xf41b },
		{ 0x1f, 0x0000 }
	};

	rtl_writephy(tp, 0x1f, 0x0001);
	rtl_patchphy(tp, 0x16, 1 << 0);

	rtl_writephy_batch(tp, phy_reg_init);
}

static void rtl8168bef_hw_phy_config(struct rtl8169_private *tp)
{
	static const struct phy_reg phy_reg_init[] = {
		{ 0x1f, 0x0001 },
		{ 0x10, 0xf41b },
		{ 0x1f, 0x0000 }
	};

	rtl_writephy_batch(tp, phy_reg_init);
}

static void rtl8168cp_1_hw_phy_config(struct rtl8169_private *tp)
{
	static const struct phy_reg phy_reg_init[] = {
		{ 0x1f, 0x0000 },
		{ 0x1d, 0x0f00 },
		{ 0x1f, 0x0002 },
		{ 0x0c, 0x1ec8 },
		{ 0x1f, 0x0000 }
	};

	rtl_writephy_batch(tp, phy_reg_init);
}

static void rtl8168cp_2_hw_phy_config(struct rtl8169_private *tp)
{
	static const struct phy_reg phy_reg_init[] = {
		{ 0x1f, 0x0001 },
		{ 0x1d, 0x3d98 },
		{ 0x1f, 0x0000 }
	};

	rtl_writephy(tp, 0x1f, 0x0000);
	rtl_patchphy(tp, 0x14, 1 << 5);
	rtl_patchphy(tp, 0x0d, 1 << 5);

	rtl_writephy_batch(tp, phy_reg_init);
}

static void rtl8168c_1_hw_phy_config(struct rtl8169_private *tp)
{
	static const struct phy_reg phy_reg_init[] = {
		{ 0x1f, 0x0001 },
		{ 0x12, 0x2300 },
		{ 0x1f, 0x0002 },
		{ 0x00, 0x88d4 },
		{ 0x01, 0x82b1 },
		{ 0x03, 0x7002 },
		{ 0x08, 0x9e30 },
		{ 0x09, 0x01f0 },
		{ 0x0a, 0x5500 },
		{ 0x0c, 0x00c8 },
		{ 0x1f, 0x0003 },
		{ 0x12, 0xc096 },
		{ 0x16, 0x000a },
		{ 0x1f, 0x0000 },
		{ 0x1f, 0x0000 },
		{ 0x09, 0x2000 },
		{ 0x09, 0x0000 }
	};

	rtl_writephy_batch(tp, phy_reg_init);

	rtl_patchphy(tp, 0x14, 1 << 5);
	rtl_patchphy(tp, 0x0d, 1 << 5);
	rtl_writephy(tp, 0x1f, 0x0000);
}

static void rtl8168c_2_hw_phy_config(struct rtl8169_private *tp)
{
	static const struct phy_reg phy_reg_init[] = {
		{ 0x1f, 0x0001 },
		{ 0x12, 0x2300 },
		{ 0x03, 0x802f },
		{ 0x02, 0x4f02 },
		{ 0x01, 0x0409 },
		{ 0x00, 0xf099 },
		{ 0x04, 0x9800 },
		{ 0x04, 0x9000 },
		{ 0x1d, 0x3d98 },
		{ 0x1f, 0x0002 },
		{ 0x0c, 0x7eb8 },
		{ 0x06, 0x0761 },
		{ 0x1f, 0x0003 },
		{ 0x16, 0x0f0a },
		{ 0x1f, 0x0000 }
	};

	rtl_writephy_batch(tp, phy_reg_init);

	rtl_patchphy(tp, 0x16, 1 << 0);
	rtl_patchphy(tp, 0x14, 1 << 5);
	rtl_patchphy(tp, 0x0d, 1 << 5);
	rtl_writephy(tp, 0x1f, 0x0000);
}

static void rtl8168c_3_hw_phy_config(struct rtl8169_private *tp)
{
	static const struct phy_reg phy_reg_init[] = {
		{ 0x1f, 0x0001 },
		{ 0x12, 0x2300 },
		{ 0x1d, 0x3d98 },
		{ 0x1f, 0x0002 },
		{ 0x0c, 0x7eb8 },
		{ 0x06, 0x5461 },
		{ 0x1f, 0x0003 },
		{ 0x16, 0x0f0a },
		{ 0x1f, 0x0000 }
	};

	rtl_writephy_batch(tp, phy_reg_init);

	rtl_patchphy(tp, 0x16, 1 << 0);
	rtl_patchphy(tp, 0x14, 1 << 5);
	rtl_patchphy(tp, 0x0d, 1 << 5);
	rtl_writephy(tp, 0x1f, 0x0000);
}

static void rtl8168c_4_hw_phy_config(struct rtl8169_private *tp)
{
	rtl8168c_3_hw_phy_config(tp);
}

static const struct phy_reg rtl8168d_1_phy_reg_init_0[] = {
	/* Channel Estimation */
	{ 0x1f, 0x0001 },
	{ 0x06, 0x4064 },
	{ 0x07, 0x2863 },
	{ 0x08, 0x059c },
	{ 0x09, 0x26b4 },
	{ 0x0a, 0x6a19 },
	{ 0x0b, 0xdcc8 },
	{ 0x10, 0xf06d },
	{ 0x14, 0x7f68 },
	{ 0x18, 0x7fd9 },
	{ 0x1c, 0xf0ff },
	{ 0x1d, 0x3d9c },
	{ 0x1f, 0x0003 },
	{ 0x12, 0xf49f },
	{ 0x13, 0x070b },
	{ 0x1a, 0x05ad },
	{ 0x14, 0x94c0 },

	/*
	 * Tx Error Issue
	 * Enhance line driver power
	 */
	{ 0x1f, 0x0002 },
	{ 0x06, 0x5561 },
	{ 0x1f, 0x0005 },
	{ 0x05, 0x8332 },
	{ 0x06, 0x5561 },

	/*
	 * Can not link to 1Gbps with bad cable
	 * Decrease SNR threshold form 21.07dB to 19.04dB
	 */
	{ 0x1f, 0x0001 },
	{ 0x17, 0x0cc0 },

	{ 0x1f, 0x0000 },
	{ 0x0d, 0xf880 }
};

static const struct phy_reg rtl8168d_1_phy_reg_init_1[] = {
	{ 0x1f, 0x0002 },
	{ 0x05, 0x669a },
	{ 0x1f, 0x0005 },
	{ 0x05, 0x8330 },
	{ 0x06, 0x669a },
	{ 0x1f, 0x0002 }
};

static void rtl8168d_1_hw_phy_config(struct rtl8169_private *tp)
{
	rtl_writephy_batch(tp, rtl8168d_1_phy_reg_init_0);

	/*
	 * Rx Error Issue
	 * Fine Tune Switching regulator parameter
	 */
	rtl_writephy(tp, 0x1f, 0x0002);
	rtl_w0w1_phy(tp, 0x0b, 0x0010, 0x00ef);
	rtl_w0w1_phy(tp, 0x0c, 0xa200, 0x5d00);

	if (rtl8168d_efuse_read(tp, 0x01) == 0xb1) {
		int val;

		rtl_writephy_batch(tp, rtl8168d_1_phy_reg_init_1);

		val = rtl_readphy(tp, 0x0d);

		if ((val & 0x00ff) != 0x006c) {
			static const u32 set[] = {
				0x0065, 0x0066, 0x0067, 0x0068,
				0x0069, 0x006a, 0x006b, 0x006c
			};
			int i;

			rtl_writephy(tp, 0x1f, 0x0002);

			val &= 0xff00;
			for (i = 0; i < ARRAY_SIZE(set); i++)
				rtl_writephy(tp, 0x0d, val | set[i]);
		}
	} else {
		static const struct phy_reg phy_reg_init[] = {
			{ 0x1f, 0x0002 },
			{ 0x05, 0x6662 },
			{ 0x1f, 0x0005 },
			{ 0x05, 0x8330 },
			{ 0x06, 0x6662 }
		};

		rtl_writephy_batch(tp, phy_reg_init);
	}

	/* RSET couple improve */
	rtl_writephy(tp, 0x1f, 0x0002);
	rtl_patchphy(tp, 0x0d, 0x0300);
	rtl_patchphy(tp, 0x0f, 0x0010);

	/* Fine tune PLL performance */
	rtl_writephy(tp, 0x1f, 0x0002);
	rtl_w0w1_phy(tp, 0x02, 0x0100, 0x0600);
	rtl_w0w1_phy(tp, 0x03, 0x0000, 0xe000);

	rtl_writephy(tp, 0x1f, 0x0005);
	rtl_writephy(tp, 0x05, 0x001b);

	rtl_apply_firmware_cond(tp, MII_EXPANSION, 0xbf00);

	rtl_writephy(tp, 0x1f, 0x0000);
}

static void rtl8168d_2_hw_phy_config(struct rtl8169_private *tp)
{
	rtl_writephy_batch(tp, rtl8168d_1_phy_reg_init_0);

	if (rtl8168d_efuse_read(tp, 0x01) == 0xb1) {
		int val;

		rtl_writephy_batch(tp, rtl8168d_1_phy_reg_init_1);

		val = rtl_readphy(tp, 0x0d);
		if ((val & 0x00ff) != 0x006c) {
			static const u32 set[] = {
				0x0065, 0x0066, 0x0067, 0x0068,
				0x0069, 0x006a, 0x006b, 0x006c
			};
			int i;

			rtl_writephy(tp, 0x1f, 0x0002);

			val &= 0xff00;
			for (i = 0; i < ARRAY_SIZE(set); i++)
				rtl_writephy(tp, 0x0d, val | set[i]);
		}
	} else {
		static const struct phy_reg phy_reg_init[] = {
			{ 0x1f, 0x0002 },
			{ 0x05, 0x2642 },
			{ 0x1f, 0x0005 },
			{ 0x05, 0x8330 },
			{ 0x06, 0x2642 }
		};

		rtl_writephy_batch(tp, phy_reg_init);
	}

	/* Fine tune PLL performance */
	rtl_writephy(tp, 0x1f, 0x0002);
	rtl_w0w1_phy(tp, 0x02, 0x0100, 0x0600);
	rtl_w0w1_phy(tp, 0x03, 0x0000, 0xe000);

	/* Switching regulator Slew rate */
	rtl_writephy(tp, 0x1f, 0x0002);
	rtl_patchphy(tp, 0x0f, 0x0017);

	rtl_writephy(tp, 0x1f, 0x0005);
	rtl_writephy(tp, 0x05, 0x001b);

	rtl_apply_firmware_cond(tp, MII_EXPANSION, 0xb300);

	rtl_writephy(tp, 0x1f, 0x0000);
}

static void rtl8168d_3_hw_phy_config(struct rtl8169_private *tp)
{
	static const struct phy_reg phy_reg_init[] = {
		{ 0x1f, 0x0002 },
		{ 0x10, 0x0008 },
		{ 0x0d, 0x006c },

		{ 0x1f, 0x0000 },
		{ 0x0d, 0xf880 },

		{ 0x1f, 0x0001 },
		{ 0x17, 0x0cc0 },

		{ 0x1f, 0x0001 },
		{ 0x0b, 0xa4d8 },
		{ 0x09, 0x281c },
		{ 0x07, 0x2883 },
		{ 0x0a, 0x6b35 },
		{ 0x1d, 0x3da4 },
		{ 0x1c, 0xeffd },
		{ 0x14, 0x7f52 },
		{ 0x18, 0x7fc6 },
		{ 0x08, 0x0601 },
		{ 0x06, 0x4063 },
		{ 0x10, 0xf074 },
		{ 0x1f, 0x0003 },
		{ 0x13, 0x0789 },
		{ 0x12, 0xf4bd },
		{ 0x1a, 0x04fd },
		{ 0x14, 0x84b0 },
		{ 0x1f, 0x0000 },
		{ 0x00, 0x9200 },

		{ 0x1f, 0x0005 },
		{ 0x01, 0x0340 },
		{ 0x1f, 0x0001 },
		{ 0x04, 0x4000 },
		{ 0x03, 0x1d21 },
		{ 0x02, 0x0c32 },
		{ 0x01, 0x0200 },
		{ 0x00, 0x5554 },
		{ 0x04, 0x4800 },
		{ 0x04, 0x4000 },
		{ 0x04, 0xf000 },
		{ 0x03, 0xdf01 },
		{ 0x02, 0xdf20 },
		{ 0x01, 0x101a },
		{ 0x00, 0xa0ff },
		{ 0x04, 0xf800 },
		{ 0x04, 0xf000 },
		{ 0x1f, 0x0000 },

		{ 0x1f, 0x0007 },
		{ 0x1e, 0x0023 },
		{ 0x16, 0x0000 },
		{ 0x1f, 0x0000 }
	};

	rtl_writephy_batch(tp, phy_reg_init);
}

static void rtl8168d_4_hw_phy_config(struct rtl8169_private *tp)
{
	static const struct phy_reg phy_reg_init[] = {
		{ 0x1f, 0x0001 },
		{ 0x17, 0x0cc0 },

		{ 0x1f, 0x0007 },
		{ 0x1e, 0x002d },
		{ 0x18, 0x0040 },
		{ 0x1f, 0x0000 }
	};

	rtl_writephy_batch(tp, phy_reg_init);
	rtl_patchphy(tp, 0x0d, 1 << 5);
}

static void rtl8168e_1_hw_phy_config(struct rtl8169_private *tp)
{
	static const struct phy_reg phy_reg_init[] = {
		/* Enable Delay cap */
		{ 0x1f, 0x0005 },
		{ 0x05, 0x8b80 },
		{ 0x06, 0xc896 },
		{ 0x1f, 0x0000 },

		/* Channel estimation fine tune */
		{ 0x1f, 0x0001 },
		{ 0x0b, 0x6c20 },
		{ 0x07, 0x2872 },
		{ 0x1c, 0xefff },
		{ 0x1f, 0x0003 },
		{ 0x14, 0x6420 },
		{ 0x1f, 0x0000 },

		/* Update PFM & 10M TX idle timer */
		{ 0x1f, 0x0007 },
		{ 0x1e, 0x002f },
		{ 0x15, 0x1919 },
		{ 0x1f, 0x0000 },

		{ 0x1f, 0x0007 },
		{ 0x1e, 0x00ac },
		{ 0x18, 0x0006 },
		{ 0x1f, 0x0000 }
	};

	rtl_apply_firmware(tp);

	rtl_writephy_batch(tp, phy_reg_init);

	/* DCO enable for 10M IDLE Power */
	rtl_writephy(tp, 0x1f, 0x0007);
	rtl_writephy(tp, 0x1e, 0x0023);
	rtl_w0w1_phy(tp, 0x17, 0x0006, 0x0000);
	rtl_writephy(tp, 0x1f, 0x0000);

	/* For impedance matching */
	rtl_writephy(tp, 0x1f, 0x0002);
	rtl_w0w1_phy(tp, 0x08, 0x8000, 0x7f00);
	rtl_writephy(tp, 0x1f, 0x0000);

	/* PHY auto speed down */
	rtl_writephy(tp, 0x1f, 0x0007);
	rtl_writephy(tp, 0x1e, 0x002d);
	rtl_w0w1_phy(tp, 0x18, 0x0050, 0x0000);
	rtl_writephy(tp, 0x1f, 0x0000);
	rtl_w0w1_phy(tp, 0x14, 0x8000, 0x0000);

	rtl_writephy(tp, 0x1f, 0x0005);
	rtl_writephy(tp, 0x05, 0x8b86);
	rtl_w0w1_phy(tp, 0x06, 0x0001, 0x0000);
	rtl_writephy(tp, 0x1f, 0x0000);

	rtl_writephy(tp, 0x1f, 0x0005);
	rtl_writephy(tp, 0x05, 0x8b85);
	rtl_w0w1_phy(tp, 0x06, 0x0000, 0x2000);
	rtl_writephy(tp, 0x1f, 0x0007);
	rtl_writephy(tp, 0x1e, 0x0020);
	rtl_w0w1_phy(tp, 0x15, 0x0000, 0x1100);
	rtl_writephy(tp, 0x1f, 0x0006);
	rtl_writephy(tp, 0x00, 0x5a00);
	rtl_writephy(tp, 0x1f, 0x0000);
	rtl_writephy(tp, 0x0d, 0x0007);
	rtl_writephy(tp, 0x0e, 0x003c);
	rtl_writephy(tp, 0x0d, 0x4007);
	rtl_writephy(tp, 0x0e, 0x0000);
	rtl_writephy(tp, 0x0d, 0x0000);
}

static void rtl_rar_exgmac_set(struct rtl8169_private *tp, u8 *addr)
{
	const u16 w[] = {
		addr[0] | (addr[1] << 8),
		addr[2] | (addr[3] << 8),
		addr[4] | (addr[5] << 8)
	};

	rtl_eri_write(tp, 0xe0, ERIAR_MASK_1111, w[0] | (w[1] << 16));
	rtl_eri_write(tp, 0xe4, ERIAR_MASK_1111, w[2]);
	rtl_eri_write(tp, 0xf0, ERIAR_MASK_1111, w[0] << 16);
	rtl_eri_write(tp, 0xf4, ERIAR_MASK_1111, w[1] | (w[2] << 16));
}

static void rtl8168e_2_hw_phy_config(struct rtl8169_private *tp)
{
	static const struct phy_reg phy_reg_init[] = {
		/* Enable Delay cap */
		{ 0x1f, 0x0004 },
		{ 0x1f, 0x0007 },
		{ 0x1e, 0x00ac },
		{ 0x18, 0x0006 },
		{ 0x1f, 0x0002 },
		{ 0x1f, 0x0000 },
		{ 0x1f, 0x0000 },

		/* Channel estimation fine tune */
		{ 0x1f, 0x0003 },
		{ 0x09, 0xa20f },
		{ 0x1f, 0x0000 },
		{ 0x1f, 0x0000 },

		/* Green Setting */
		{ 0x1f, 0x0005 },
		{ 0x05, 0x8b5b },
		{ 0x06, 0x9222 },
		{ 0x05, 0x8b6d },
		{ 0x06, 0x8000 },
		{ 0x05, 0x8b76 },
		{ 0x06, 0x8000 },
		{ 0x1f, 0x0000 }
	};

	rtl_apply_firmware(tp);

	rtl_writephy_batch(tp, phy_reg_init);

	/* For 4-corner performance improve */
	rtl_writephy(tp, 0x1f, 0x0005);
	rtl_writephy(tp, 0x05, 0x8b80);
	rtl_w0w1_phy(tp, 0x17, 0x0006, 0x0000);
	rtl_writephy(tp, 0x1f, 0x0000);

	/* PHY auto speed down */
	rtl_writephy(tp, 0x1f, 0x0004);
	rtl_writephy(tp, 0x1f, 0x0007);
	rtl_writephy(tp, 0x1e, 0x002d);
	rtl_w0w1_phy(tp, 0x18, 0x0010, 0x0000);
	rtl_writephy(tp, 0x1f, 0x0002);
	rtl_writephy(tp, 0x1f, 0x0000);
	rtl_w0w1_phy(tp, 0x14, 0x8000, 0x0000);

	/* improve 10M EEE waveform */
	rtl_writephy(tp, 0x1f, 0x0005);
	rtl_writephy(tp, 0x05, 0x8b86);
	rtl_w0w1_phy(tp, 0x06, 0x0001, 0x0000);
	rtl_writephy(tp, 0x1f, 0x0000);

	/* Improve 2-pair detection performance */
	rtl_writephy(tp, 0x1f, 0x0005);
	rtl_writephy(tp, 0x05, 0x8b85);
	rtl_w0w1_phy(tp, 0x06, 0x4000, 0x0000);
	rtl_writephy(tp, 0x1f, 0x0000);

	rtl8168f_config_eee_phy(tp);
	rtl_enable_eee(tp);

	/* Green feature */
	rtl_writephy(tp, 0x1f, 0x0003);
	rtl_w0w1_phy(tp, 0x19, 0x0001, 0x0000);
	rtl_w0w1_phy(tp, 0x10, 0x0400, 0x0000);
	rtl_writephy(tp, 0x1f, 0x0000);
	rtl_writephy(tp, 0x1f, 0x0005);
	rtl_w0w1_phy(tp, 0x01, 0x0100, 0x0000);
	rtl_writephy(tp, 0x1f, 0x0000);

	/* Broken BIOS workaround: feed GigaMAC registers with MAC address. */
	rtl_rar_exgmac_set(tp, tp->dev->dev_addr);
}

static void rtl8168f_hw_phy_config(struct rtl8169_private *tp)
{
	/* For 4-corner performance improve */
	rtl_writephy(tp, 0x1f, 0x0005);
	rtl_writephy(tp, 0x05, 0x8b80);
	rtl_w0w1_phy(tp, 0x06, 0x0006, 0x0000);
	rtl_writephy(tp, 0x1f, 0x0000);

	/* PHY auto speed down */
	rtl_writephy(tp, 0x1f, 0x0007);
	rtl_writephy(tp, 0x1e, 0x002d);
	rtl_w0w1_phy(tp, 0x18, 0x0010, 0x0000);
	rtl_writephy(tp, 0x1f, 0x0000);
	rtl_w0w1_phy(tp, 0x14, 0x8000, 0x0000);

	/* Improve 10M EEE waveform */
	rtl_writephy(tp, 0x1f, 0x0005);
	rtl_writephy(tp, 0x05, 0x8b86);
	rtl_w0w1_phy(tp, 0x06, 0x0001, 0x0000);
	rtl_writephy(tp, 0x1f, 0x0000);

	rtl8168f_config_eee_phy(tp);
	rtl_enable_eee(tp);
}

static void rtl8168f_1_hw_phy_config(struct rtl8169_private *tp)
{
	static const struct phy_reg phy_reg_init[] = {
		/* Channel estimation fine tune */
		{ 0x1f, 0x0003 },
		{ 0x09, 0xa20f },
		{ 0x1f, 0x0000 },

		/* Modify green table for giga & fnet */
		{ 0x1f, 0x0005 },
		{ 0x05, 0x8b55 },
		{ 0x06, 0x0000 },
		{ 0x05, 0x8b5e },
		{ 0x06, 0x0000 },
		{ 0x05, 0x8b67 },
		{ 0x06, 0x0000 },
		{ 0x05, 0x8b70 },
		{ 0x06, 0x0000 },
		{ 0x1f, 0x0000 },
		{ 0x1f, 0x0007 },
		{ 0x1e, 0x0078 },
		{ 0x17, 0x0000 },
		{ 0x19, 0x00fb },
		{ 0x1f, 0x0000 },

		/* Modify green table for 10M */
		{ 0x1f, 0x0005 },
		{ 0x05, 0x8b79 },
		{ 0x06, 0xaa00 },
		{ 0x1f, 0x0000 },

		/* Disable hiimpedance detection (RTCT) */
		{ 0x1f, 0x0003 },
		{ 0x01, 0x328a },
		{ 0x1f, 0x0000 }
	};

	rtl_apply_firmware(tp);

	rtl_writephy_batch(tp, phy_reg_init);

	rtl8168f_hw_phy_config(tp);

	/* Improve 2-pair detection performance */
	rtl_writephy(tp, 0x1f, 0x0005);
	rtl_writephy(tp, 0x05, 0x8b85);
	rtl_w0w1_phy(tp, 0x06, 0x4000, 0x0000);
	rtl_writephy(tp, 0x1f, 0x0000);
}

static void rtl8168f_2_hw_phy_config(struct rtl8169_private *tp)
{
	rtl_apply_firmware(tp);

	rtl8168f_hw_phy_config(tp);
}

static void rtl8411_hw_phy_config(struct rtl8169_private *tp)
{
	static const struct phy_reg phy_reg_init[] = {
		/* Channel estimation fine tune */
		{ 0x1f, 0x0003 },
		{ 0x09, 0xa20f },
		{ 0x1f, 0x0000 },

		/* Modify green table for giga & fnet */
		{ 0x1f, 0x0005 },
		{ 0x05, 0x8b55 },
		{ 0x06, 0x0000 },
		{ 0x05, 0x8b5e },
		{ 0x06, 0x0000 },
		{ 0x05, 0x8b67 },
		{ 0x06, 0x0000 },
		{ 0x05, 0x8b70 },
		{ 0x06, 0x0000 },
		{ 0x1f, 0x0000 },
		{ 0x1f, 0x0007 },
		{ 0x1e, 0x0078 },
		{ 0x17, 0x0000 },
		{ 0x19, 0x00aa },
		{ 0x1f, 0x0000 },

		/* Modify green table for 10M */
		{ 0x1f, 0x0005 },
		{ 0x05, 0x8b79 },
		{ 0x06, 0xaa00 },
		{ 0x1f, 0x0000 },

		/* Disable hiimpedance detection (RTCT) */
		{ 0x1f, 0x0003 },
		{ 0x01, 0x328a },
		{ 0x1f, 0x0000 }
	};


	rtl_apply_firmware(tp);

	rtl8168f_hw_phy_config(tp);

	/* Improve 2-pair detection performance */
	rtl_writephy(tp, 0x1f, 0x0005);
	rtl_writephy(tp, 0x05, 0x8b85);
	rtl_w0w1_phy(tp, 0x06, 0x4000, 0x0000);
	rtl_writephy(tp, 0x1f, 0x0000);

	rtl_writephy_batch(tp, phy_reg_init);

	/* Modify green table for giga */
	rtl_writephy(tp, 0x1f, 0x0005);
	rtl_writephy(tp, 0x05, 0x8b54);
	rtl_w0w1_phy(tp, 0x06, 0x0000, 0x0800);
	rtl_writephy(tp, 0x05, 0x8b5d);
	rtl_w0w1_phy(tp, 0x06, 0x0000, 0x0800);
	rtl_writephy(tp, 0x05, 0x8a7c);
	rtl_w0w1_phy(tp, 0x06, 0x0000, 0x0100);
	rtl_writephy(tp, 0x05, 0x8a7f);
	rtl_w0w1_phy(tp, 0x06, 0x0100, 0x0000);
	rtl_writephy(tp, 0x05, 0x8a82);
	rtl_w0w1_phy(tp, 0x06, 0x0000, 0x0100);
	rtl_writephy(tp, 0x05, 0x8a85);
	rtl_w0w1_phy(tp, 0x06, 0x0000, 0x0100);
	rtl_writephy(tp, 0x05, 0x8a88);
	rtl_w0w1_phy(tp, 0x06, 0x0000, 0x0100);
	rtl_writephy(tp, 0x1f, 0x0000);

	/* uc same-seed solution */
	rtl_writephy(tp, 0x1f, 0x0005);
	rtl_writephy(tp, 0x05, 0x8b85);
	rtl_w0w1_phy(tp, 0x06, 0x8000, 0x0000);
	rtl_writephy(tp, 0x1f, 0x0000);

	/* Green feature */
	rtl_writephy(tp, 0x1f, 0x0003);
	rtl_w0w1_phy(tp, 0x19, 0x0000, 0x0001);
	rtl_w0w1_phy(tp, 0x10, 0x0000, 0x0400);
	rtl_writephy(tp, 0x1f, 0x0000);
}

static void rtl8168g_disable_aldps(struct rtl8169_private *tp)
{
	phy_modify_paged(tp->phydev, 0x0a43, 0x10, BIT(2), 0);
}

static void rtl8168g_phy_adjust_10m_aldps(struct rtl8169_private *tp)
{
	struct phy_device *phydev = tp->phydev;

	phy_modify_paged(phydev, 0x0bcc, 0x14, BIT(8), 0);
	phy_modify_paged(phydev, 0x0a44, 0x11, 0, BIT(7) | BIT(6));
	phy_write(phydev, 0x1f, 0x0a43);
	phy_write(phydev, 0x13, 0x8084);
	phy_clear_bits(phydev, 0x14, BIT(14) | BIT(13));
	phy_set_bits(phydev, 0x10, BIT(12) | BIT(1) | BIT(0));

	phy_write(phydev, 0x1f, 0x0000);
}

static void rtl8168g_1_hw_phy_config(struct rtl8169_private *tp)
{
	int ret;

	rtl_apply_firmware(tp);

	ret = phy_read_paged(tp->phydev, 0x0a46, 0x10);
	if (ret & BIT(8))
		phy_modify_paged(tp->phydev, 0x0bcc, 0x12, BIT(15), 0);
	else
		phy_modify_paged(tp->phydev, 0x0bcc, 0x12, 0, BIT(15));

	ret = phy_read_paged(tp->phydev, 0x0a46, 0x13);
	if (ret & BIT(8))
		phy_modify_paged(tp->phydev, 0x0c41, 0x12, 0, BIT(1));
	else
		phy_modify_paged(tp->phydev, 0x0c41, 0x12, BIT(1), 0);

	/* Enable PHY auto speed down */
	phy_modify_paged(tp->phydev, 0x0a44, 0x11, 0, BIT(3) | BIT(2));

	rtl8168g_phy_adjust_10m_aldps(tp);

	/* EEE auto-fallback function */
	phy_modify_paged(tp->phydev, 0x0a4b, 0x11, 0, BIT(2));

	/* Enable UC LPF tune function */
	rtl_writephy(tp, 0x1f, 0x0a43);
	rtl_writephy(tp, 0x13, 0x8012);
	rtl_w0w1_phy(tp, 0x14, 0x8000, 0x0000);

	phy_modify_paged(tp->phydev, 0x0c42, 0x11, BIT(13), BIT(14));

	/* Improve SWR Efficiency */
	rtl_writephy(tp, 0x1f, 0x0bcd);
	rtl_writephy(tp, 0x14, 0x5065);
	rtl_writephy(tp, 0x14, 0xd065);
	rtl_writephy(tp, 0x1f, 0x0bc8);
	rtl_writephy(tp, 0x11, 0x5655);
	rtl_writephy(tp, 0x1f, 0x0bcd);
	rtl_writephy(tp, 0x14, 0x1065);
	rtl_writephy(tp, 0x14, 0x9065);
	rtl_writephy(tp, 0x14, 0x1065);
	rtl_writephy(tp, 0x1f, 0x0000);

	rtl8168g_disable_aldps(tp);
	rtl8168g_config_eee_phy(tp);
	rtl_enable_eee(tp);
}

static void rtl8168g_2_hw_phy_config(struct rtl8169_private *tp)
{
	rtl_apply_firmware(tp);
	rtl8168g_config_eee_phy(tp);
	rtl_enable_eee(tp);
}

static void rtl8168h_1_hw_phy_config(struct rtl8169_private *tp)
{
	u16 dout_tapbin;
	u32 data;

	rtl_apply_firmware(tp);

	/* CHN EST parameters adjust - giga master */
	rtl_writephy(tp, 0x1f, 0x0a43);
	rtl_writephy(tp, 0x13, 0x809b);
	rtl_w0w1_phy(tp, 0x14, 0x8000, 0xf800);
	rtl_writephy(tp, 0x13, 0x80a2);
	rtl_w0w1_phy(tp, 0x14, 0x8000, 0xff00);
	rtl_writephy(tp, 0x13, 0x80a4);
	rtl_w0w1_phy(tp, 0x14, 0x8500, 0xff00);
	rtl_writephy(tp, 0x13, 0x809c);
	rtl_w0w1_phy(tp, 0x14, 0xbd00, 0xff00);
	rtl_writephy(tp, 0x1f, 0x0000);

	/* CHN EST parameters adjust - giga slave */
	rtl_writephy(tp, 0x1f, 0x0a43);
	rtl_writephy(tp, 0x13, 0x80ad);
	rtl_w0w1_phy(tp, 0x14, 0x7000, 0xf800);
	rtl_writephy(tp, 0x13, 0x80b4);
	rtl_w0w1_phy(tp, 0x14, 0x5000, 0xff00);
	rtl_writephy(tp, 0x13, 0x80ac);
	rtl_w0w1_phy(tp, 0x14, 0x4000, 0xff00);
	rtl_writephy(tp, 0x1f, 0x0000);

	/* CHN EST parameters adjust - fnet */
	rtl_writephy(tp, 0x1f, 0x0a43);
	rtl_writephy(tp, 0x13, 0x808e);
	rtl_w0w1_phy(tp, 0x14, 0x1200, 0xff00);
	rtl_writephy(tp, 0x13, 0x8090);
	rtl_w0w1_phy(tp, 0x14, 0xe500, 0xff00);
	rtl_writephy(tp, 0x13, 0x8092);
	rtl_w0w1_phy(tp, 0x14, 0x9f00, 0xff00);
	rtl_writephy(tp, 0x1f, 0x0000);

	/* enable R-tune & PGA-retune function */
	dout_tapbin = 0;
	rtl_writephy(tp, 0x1f, 0x0a46);
	data = rtl_readphy(tp, 0x13);
	data &= 3;
	data <<= 2;
	dout_tapbin |= data;
	data = rtl_readphy(tp, 0x12);
	data &= 0xc000;
	data >>= 14;
	dout_tapbin |= data;
	dout_tapbin = ~(dout_tapbin^0x08);
	dout_tapbin <<= 12;
	dout_tapbin &= 0xf000;
	rtl_writephy(tp, 0x1f, 0x0a43);
	rtl_writephy(tp, 0x13, 0x827a);
	rtl_w0w1_phy(tp, 0x14, dout_tapbin, 0xf000);
	rtl_writephy(tp, 0x13, 0x827b);
	rtl_w0w1_phy(tp, 0x14, dout_tapbin, 0xf000);
	rtl_writephy(tp, 0x13, 0x827c);
	rtl_w0w1_phy(tp, 0x14, dout_tapbin, 0xf000);
	rtl_writephy(tp, 0x13, 0x827d);
	rtl_w0w1_phy(tp, 0x14, dout_tapbin, 0xf000);

	rtl_writephy(tp, 0x1f, 0x0a43);
	rtl_writephy(tp, 0x13, 0x0811);
	rtl_w0w1_phy(tp, 0x14, 0x0800, 0x0000);
	rtl_writephy(tp, 0x1f, 0x0a42);
	rtl_w0w1_phy(tp, 0x16, 0x0002, 0x0000);
	rtl_writephy(tp, 0x1f, 0x0000);

	/* enable GPHY 10M */
	phy_modify_paged(tp->phydev, 0x0a44, 0x11, 0, BIT(11));

	/* SAR ADC performance */
	phy_modify_paged(tp->phydev, 0x0bca, 0x17, BIT(12) | BIT(13), BIT(14));

	rtl_writephy(tp, 0x1f, 0x0a43);
	rtl_writephy(tp, 0x13, 0x803f);
	rtl_w0w1_phy(tp, 0x14, 0x0000, 0x3000);
	rtl_writephy(tp, 0x13, 0x8047);
	rtl_w0w1_phy(tp, 0x14, 0x0000, 0x3000);
	rtl_writephy(tp, 0x13, 0x804f);
	rtl_w0w1_phy(tp, 0x14, 0x0000, 0x3000);
	rtl_writephy(tp, 0x13, 0x8057);
	rtl_w0w1_phy(tp, 0x14, 0x0000, 0x3000);
	rtl_writephy(tp, 0x13, 0x805f);
	rtl_w0w1_phy(tp, 0x14, 0x0000, 0x3000);
	rtl_writephy(tp, 0x13, 0x8067);
	rtl_w0w1_phy(tp, 0x14, 0x0000, 0x3000);
	rtl_writephy(tp, 0x13, 0x806f);
	rtl_w0w1_phy(tp, 0x14, 0x0000, 0x3000);
	rtl_writephy(tp, 0x1f, 0x0000);

	/* disable phy pfm mode */
	phy_modify_paged(tp->phydev, 0x0a44, 0x11, BIT(7), 0);

	rtl8168g_disable_aldps(tp);
	rtl8168g_config_eee_phy(tp);
	rtl_enable_eee(tp);
}

static void rtl8168h_2_hw_phy_config(struct rtl8169_private *tp)
{
	u16 ioffset_p3, ioffset_p2, ioffset_p1, ioffset_p0;
	u16 rlen;
	u32 data;

	rtl_apply_firmware(tp);

	/* CHIN EST parameter update */
	rtl_writephy(tp, 0x1f, 0x0a43);
	rtl_writephy(tp, 0x13, 0x808a);
	rtl_w0w1_phy(tp, 0x14, 0x000a, 0x003f);
	rtl_writephy(tp, 0x1f, 0x0000);

	/* enable R-tune & PGA-retune function */
	rtl_writephy(tp, 0x1f, 0x0a43);
	rtl_writephy(tp, 0x13, 0x0811);
	rtl_w0w1_phy(tp, 0x14, 0x0800, 0x0000);
	rtl_writephy(tp, 0x1f, 0x0a42);
	rtl_w0w1_phy(tp, 0x16, 0x0002, 0x0000);
	rtl_writephy(tp, 0x1f, 0x0000);

	/* enable GPHY 10M */
	phy_modify_paged(tp->phydev, 0x0a44, 0x11, 0, BIT(11));

	r8168_mac_ocp_write(tp, 0xdd02, 0x807d);
	data = r8168_mac_ocp_read(tp, 0xdd02);
	ioffset_p3 = ((data & 0x80)>>7);
	ioffset_p3 <<= 3;

	data = r8168_mac_ocp_read(tp, 0xdd00);
	ioffset_p3 |= ((data & (0xe000))>>13);
	ioffset_p2 = ((data & (0x1e00))>>9);
	ioffset_p1 = ((data & (0x01e0))>>5);
	ioffset_p0 = ((data & 0x0010)>>4);
	ioffset_p0 <<= 3;
	ioffset_p0 |= (data & (0x07));
	data = (ioffset_p3<<12)|(ioffset_p2<<8)|(ioffset_p1<<4)|(ioffset_p0);

	if ((ioffset_p3 != 0x0f) || (ioffset_p2 != 0x0f) ||
	    (ioffset_p1 != 0x0f) || (ioffset_p0 != 0x0f)) {
		rtl_writephy(tp, 0x1f, 0x0bcf);
		rtl_writephy(tp, 0x16, data);
		rtl_writephy(tp, 0x1f, 0x0000);
	}

	/* Modify rlen (TX LPF corner frequency) level */
	rtl_writephy(tp, 0x1f, 0x0bcd);
	data = rtl_readphy(tp, 0x16);
	data &= 0x000f;
	rlen = 0;
	if (data > 3)
		rlen = data - 3;
	data = rlen | (rlen<<4) | (rlen<<8) | (rlen<<12);
	rtl_writephy(tp, 0x17, data);
	rtl_writephy(tp, 0x1f, 0x0bcd);
	rtl_writephy(tp, 0x1f, 0x0000);

	/* disable phy pfm mode */
	phy_modify_paged(tp->phydev, 0x0a44, 0x11, BIT(7), 0);

	rtl8168g_disable_aldps(tp);
	rtl8168g_config_eee_phy(tp);
	rtl_enable_eee(tp);
}

static void rtl8168ep_1_hw_phy_config(struct rtl8169_private *tp)
{
	/* Enable PHY auto speed down */
	phy_modify_paged(tp->phydev, 0x0a44, 0x11, 0, BIT(3) | BIT(2));

	rtl8168g_phy_adjust_10m_aldps(tp);

	/* Enable EEE auto-fallback function */
	phy_modify_paged(tp->phydev, 0x0a4b, 0x11, 0, BIT(2));

	/* Enable UC LPF tune function */
	rtl_writephy(tp, 0x1f, 0x0a43);
	rtl_writephy(tp, 0x13, 0x8012);
	rtl_w0w1_phy(tp, 0x14, 0x8000, 0x0000);
	rtl_writephy(tp, 0x1f, 0x0000);

	/* set rg_sel_sdm_rate */
	phy_modify_paged(tp->phydev, 0x0c42, 0x11, BIT(13), BIT(14));

	rtl8168g_disable_aldps(tp);
	rtl8168g_config_eee_phy(tp);
	rtl_enable_eee(tp);
}

static void rtl8168ep_2_hw_phy_config(struct rtl8169_private *tp)
{
	rtl8168g_phy_adjust_10m_aldps(tp);

	/* Enable UC LPF tune function */
	rtl_writephy(tp, 0x1f, 0x0a43);
	rtl_writephy(tp, 0x13, 0x8012);
	rtl_w0w1_phy(tp, 0x14, 0x8000, 0x0000);
	rtl_writephy(tp, 0x1f, 0x0000);

	/* Set rg_sel_sdm_rate */
	phy_modify_paged(tp->phydev, 0x0c42, 0x11, BIT(13), BIT(14));

	/* Channel estimation parameters */
	rtl_writephy(tp, 0x1f, 0x0a43);
	rtl_writephy(tp, 0x13, 0x80f3);
	rtl_w0w1_phy(tp, 0x14, 0x8b00, ~0x8bff);
	rtl_writephy(tp, 0x13, 0x80f0);
	rtl_w0w1_phy(tp, 0x14, 0x3a00, ~0x3aff);
	rtl_writephy(tp, 0x13, 0x80ef);
	rtl_w0w1_phy(tp, 0x14, 0x0500, ~0x05ff);
	rtl_writephy(tp, 0x13, 0x80f6);
	rtl_w0w1_phy(tp, 0x14, 0x6e00, ~0x6eff);
	rtl_writephy(tp, 0x13, 0x80ec);
	rtl_w0w1_phy(tp, 0x14, 0x6800, ~0x68ff);
	rtl_writephy(tp, 0x13, 0x80ed);
	rtl_w0w1_phy(tp, 0x14, 0x7c00, ~0x7cff);
	rtl_writephy(tp, 0x13, 0x80f2);
	rtl_w0w1_phy(tp, 0x14, 0xf400, ~0xf4ff);
	rtl_writephy(tp, 0x13, 0x80f4);
	rtl_w0w1_phy(tp, 0x14, 0x8500, ~0x85ff);
	rtl_writephy(tp, 0x1f, 0x0a43);
	rtl_writephy(tp, 0x13, 0x8110);
	rtl_w0w1_phy(tp, 0x14, 0xa800, ~0xa8ff);
	rtl_writephy(tp, 0x13, 0x810f);
	rtl_w0w1_phy(tp, 0x14, 0x1d00, ~0x1dff);
	rtl_writephy(tp, 0x13, 0x8111);
	rtl_w0w1_phy(tp, 0x14, 0xf500, ~0xf5ff);
	rtl_writephy(tp, 0x13, 0x8113);
	rtl_w0w1_phy(tp, 0x14, 0x6100, ~0x61ff);
	rtl_writephy(tp, 0x13, 0x8115);
	rtl_w0w1_phy(tp, 0x14, 0x9200, ~0x92ff);
	rtl_writephy(tp, 0x13, 0x810e);
	rtl_w0w1_phy(tp, 0x14, 0x0400, ~0x04ff);
	rtl_writephy(tp, 0x13, 0x810c);
	rtl_w0w1_phy(tp, 0x14, 0x7c00, ~0x7cff);
	rtl_writephy(tp, 0x13, 0x810b);
	rtl_w0w1_phy(tp, 0x14, 0x5a00, ~0x5aff);
	rtl_writephy(tp, 0x1f, 0x0a43);
	rtl_writephy(tp, 0x13, 0x80d1);
	rtl_w0w1_phy(tp, 0x14, 0xff00, ~0xffff);
	rtl_writephy(tp, 0x13, 0x80cd);
	rtl_w0w1_phy(tp, 0x14, 0x9e00, ~0x9eff);
	rtl_writephy(tp, 0x13, 0x80d3);
	rtl_w0w1_phy(tp, 0x14, 0x0e00, ~0x0eff);
	rtl_writephy(tp, 0x13, 0x80d5);
	rtl_w0w1_phy(tp, 0x14, 0xca00, ~0xcaff);
	rtl_writephy(tp, 0x13, 0x80d7);
	rtl_w0w1_phy(tp, 0x14, 0x8400, ~0x84ff);

	/* Force PWM-mode */
	rtl_writephy(tp, 0x1f, 0x0bcd);
	rtl_writephy(tp, 0x14, 0x5065);
	rtl_writephy(tp, 0x14, 0xd065);
	rtl_writephy(tp, 0x1f, 0x0bc8);
	rtl_writephy(tp, 0x12, 0x00ed);
	rtl_writephy(tp, 0x1f, 0x0bcd);
	rtl_writephy(tp, 0x14, 0x1065);
	rtl_writephy(tp, 0x14, 0x9065);
	rtl_writephy(tp, 0x14, 0x1065);
	rtl_writephy(tp, 0x1f, 0x0000);

	rtl8168g_disable_aldps(tp);
	rtl8168g_config_eee_phy(tp);
	rtl_enable_eee(tp);
}

static void rtl8102e_hw_phy_config(struct rtl8169_private *tp)
{
	static const struct phy_reg phy_reg_init[] = {
		{ 0x1f, 0x0003 },
		{ 0x08, 0x441d },
		{ 0x01, 0x9100 },
		{ 0x1f, 0x0000 }
	};

	rtl_writephy(tp, 0x1f, 0x0000);
	rtl_patchphy(tp, 0x11, 1 << 12);
	rtl_patchphy(tp, 0x19, 1 << 13);
	rtl_patchphy(tp, 0x10, 1 << 15);

	rtl_writephy_batch(tp, phy_reg_init);
}

static void rtl8105e_hw_phy_config(struct rtl8169_private *tp)
{
	static const struct phy_reg phy_reg_init[] = {
		{ 0x1f, 0x0005 },
		{ 0x1a, 0x0000 },
		{ 0x1f, 0x0000 },

		{ 0x1f, 0x0004 },
		{ 0x1c, 0x0000 },
		{ 0x1f, 0x0000 },

		{ 0x1f, 0x0001 },
		{ 0x15, 0x7701 },
		{ 0x1f, 0x0000 }
	};

	/* Disable ALDPS before ram code */
	rtl_writephy(tp, 0x1f, 0x0000);
	rtl_writephy(tp, 0x18, 0x0310);
	msleep(100);

	rtl_apply_firmware(tp);

	rtl_writephy_batch(tp, phy_reg_init);
}

static void rtl8402_hw_phy_config(struct rtl8169_private *tp)
{
	/* Disable ALDPS before setting firmware */
	rtl_writephy(tp, 0x1f, 0x0000);
	rtl_writephy(tp, 0x18, 0x0310);
	msleep(20);

	rtl_apply_firmware(tp);

	/* EEE setting */
	rtl_eri_write(tp, 0x1b0, ERIAR_MASK_0011, 0x0000);
	rtl_writephy(tp, 0x1f, 0x0004);
	rtl_writephy(tp, 0x10, 0x401f);
	rtl_writephy(tp, 0x19, 0x7030);
	rtl_writephy(tp, 0x1f, 0x0000);
}

static void rtl8106e_hw_phy_config(struct rtl8169_private *tp)
{
	static const struct phy_reg phy_reg_init[] = {
		{ 0x1f, 0x0004 },
		{ 0x10, 0xc07f },
		{ 0x19, 0x7030 },
		{ 0x1f, 0x0000 }
	};

	/* Disable ALDPS before ram code */
	rtl_writephy(tp, 0x1f, 0x0000);
	rtl_writephy(tp, 0x18, 0x0310);
	msleep(100);

	rtl_apply_firmware(tp);

	rtl_eri_write(tp, 0x1b0, ERIAR_MASK_0011, 0x0000);
	rtl_writephy_batch(tp, phy_reg_init);

	rtl_eri_write(tp, 0x1d0, ERIAR_MASK_0011, 0x0000);
}

static void rtl_hw_phy_config(struct net_device *dev)
{
	static const rtl_generic_fct phy_configs[] = {
		/* PCI devices. */
		[RTL_GIGA_MAC_VER_02] = rtl8169s_hw_phy_config,
		[RTL_GIGA_MAC_VER_03] = rtl8169s_hw_phy_config,
		[RTL_GIGA_MAC_VER_04] = rtl8169sb_hw_phy_config,
		[RTL_GIGA_MAC_VER_05] = rtl8169scd_hw_phy_config,
		[RTL_GIGA_MAC_VER_06] = rtl8169sce_hw_phy_config,
		/* PCI-E devices. */
		[RTL_GIGA_MAC_VER_07] = rtl8102e_hw_phy_config,
		[RTL_GIGA_MAC_VER_08] = rtl8102e_hw_phy_config,
		[RTL_GIGA_MAC_VER_09] = rtl8102e_hw_phy_config,
		[RTL_GIGA_MAC_VER_10] = NULL,
		[RTL_GIGA_MAC_VER_11] = rtl8168bb_hw_phy_config,
		[RTL_GIGA_MAC_VER_12] = rtl8168bef_hw_phy_config,
		[RTL_GIGA_MAC_VER_13] = NULL,
		[RTL_GIGA_MAC_VER_14] = NULL,
		[RTL_GIGA_MAC_VER_15] = NULL,
		[RTL_GIGA_MAC_VER_16] = NULL,
		[RTL_GIGA_MAC_VER_17] = rtl8168bef_hw_phy_config,
		[RTL_GIGA_MAC_VER_18] = rtl8168cp_1_hw_phy_config,
		[RTL_GIGA_MAC_VER_19] = rtl8168c_1_hw_phy_config,
		[RTL_GIGA_MAC_VER_20] = rtl8168c_2_hw_phy_config,
		[RTL_GIGA_MAC_VER_21] = rtl8168c_3_hw_phy_config,
		[RTL_GIGA_MAC_VER_22] = rtl8168c_4_hw_phy_config,
		[RTL_GIGA_MAC_VER_23] = rtl8168cp_2_hw_phy_config,
		[RTL_GIGA_MAC_VER_24] = rtl8168cp_2_hw_phy_config,
		[RTL_GIGA_MAC_VER_25] = rtl8168d_1_hw_phy_config,
		[RTL_GIGA_MAC_VER_26] = rtl8168d_2_hw_phy_config,
		[RTL_GIGA_MAC_VER_27] = rtl8168d_3_hw_phy_config,
		[RTL_GIGA_MAC_VER_28] = rtl8168d_4_hw_phy_config,
		[RTL_GIGA_MAC_VER_29] = rtl8105e_hw_phy_config,
		[RTL_GIGA_MAC_VER_30] = rtl8105e_hw_phy_config,
		[RTL_GIGA_MAC_VER_31] = NULL,
		[RTL_GIGA_MAC_VER_32] = rtl8168e_1_hw_phy_config,
		[RTL_GIGA_MAC_VER_33] = rtl8168e_1_hw_phy_config,
		[RTL_GIGA_MAC_VER_34] = rtl8168e_2_hw_phy_config,
		[RTL_GIGA_MAC_VER_35] = rtl8168f_1_hw_phy_config,
		[RTL_GIGA_MAC_VER_36] = rtl8168f_2_hw_phy_config,
		[RTL_GIGA_MAC_VER_37] = rtl8402_hw_phy_config,
		[RTL_GIGA_MAC_VER_38] = rtl8411_hw_phy_config,
		[RTL_GIGA_MAC_VER_39] = rtl8106e_hw_phy_config,
		[RTL_GIGA_MAC_VER_40] = rtl8168g_1_hw_phy_config,
		[RTL_GIGA_MAC_VER_41] = NULL,
		[RTL_GIGA_MAC_VER_42] = rtl8168g_2_hw_phy_config,
		[RTL_GIGA_MAC_VER_43] = rtl8168g_2_hw_phy_config,
		[RTL_GIGA_MAC_VER_44] = rtl8168g_2_hw_phy_config,
		[RTL_GIGA_MAC_VER_45] = rtl8168h_1_hw_phy_config,
		[RTL_GIGA_MAC_VER_46] = rtl8168h_2_hw_phy_config,
		[RTL_GIGA_MAC_VER_47] = rtl8168h_1_hw_phy_config,
		[RTL_GIGA_MAC_VER_48] = rtl8168h_2_hw_phy_config,
		[RTL_GIGA_MAC_VER_49] = rtl8168ep_1_hw_phy_config,
		[RTL_GIGA_MAC_VER_50] = rtl8168ep_2_hw_phy_config,
		[RTL_GIGA_MAC_VER_51] = rtl8168ep_2_hw_phy_config,
	};
	struct rtl8169_private *tp = netdev_priv(dev);

	if (phy_configs[tp->mac_version])
		phy_configs[tp->mac_version](tp);
}

static void rtl_schedule_task(struct rtl8169_private *tp, enum rtl_flag flag)
{
	if (!test_and_set_bit(flag, tp->wk.flags))
		schedule_work(&tp->wk.work);
}

static void rtl8169_init_phy(struct net_device *dev, struct rtl8169_private *tp)
{
	rtl_hw_phy_config(dev);

	if (tp->mac_version <= RTL_GIGA_MAC_VER_06) {
		pci_write_config_byte(tp->pci_dev, PCI_LATENCY_TIMER, 0x40);
		pci_write_config_byte(tp->pci_dev, PCI_CACHE_LINE_SIZE, 0x08);
		netif_dbg(tp, drv, dev,
			  "Set MAC Reg C+CR Offset 0x82h = 0x01h\n");
		RTL_W8(tp, 0x82, 0x01);
	}

	/* We may have called phy_speed_down before */
	phy_speed_up(tp->phydev);

	genphy_soft_reset(tp->phydev);
}

static void rtl_rar_set(struct rtl8169_private *tp, u8 *addr)
{
	rtl_lock_work(tp);

	rtl_unlock_config_regs(tp);

	RTL_W32(tp, MAC4, addr[4] | addr[5] << 8);
	RTL_R32(tp, MAC4);

	RTL_W32(tp, MAC0, addr[0] | addr[1] << 8 | addr[2] << 16 | addr[3] << 24);
	RTL_R32(tp, MAC0);

	if (tp->mac_version == RTL_GIGA_MAC_VER_34)
		rtl_rar_exgmac_set(tp, addr);

	rtl_lock_config_regs(tp);

	rtl_unlock_work(tp);
}

static int rtl_set_mac_address(struct net_device *dev, void *p)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	struct device *d = tp_to_dev(tp);
	int ret;

	ret = eth_mac_addr(dev, p);
	if (ret)
		return ret;

	pm_runtime_get_noresume(d);

	if (pm_runtime_active(d))
		rtl_rar_set(tp, dev->dev_addr);

	pm_runtime_put_noidle(d);

	return 0;
}

static int rtl8169_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	struct rtl8169_private *tp = netdev_priv(dev);

	if (!netif_running(dev))
		return -ENODEV;

	return phy_mii_ioctl(tp->phydev, ifr, cmd);
}

static void rtl_wol_suspend_quirk(struct rtl8169_private *tp)
{
	switch (tp->mac_version) {
	case RTL_GIGA_MAC_VER_25:
	case RTL_GIGA_MAC_VER_26:
	case RTL_GIGA_MAC_VER_29:
	case RTL_GIGA_MAC_VER_30:
	case RTL_GIGA_MAC_VER_32:
	case RTL_GIGA_MAC_VER_33:
	case RTL_GIGA_MAC_VER_34:
	case RTL_GIGA_MAC_VER_37 ... RTL_GIGA_MAC_VER_51:
		RTL_W32(tp, RxConfig, RTL_R32(tp, RxConfig) |
			AcceptBroadcast | AcceptMulticast | AcceptMyPhys);
		break;
	default:
		break;
	}
}

static void rtl_pll_power_down(struct rtl8169_private *tp)
{
	if (r8168_check_dash(tp))
		return;

	if (tp->mac_version == RTL_GIGA_MAC_VER_32 ||
	    tp->mac_version == RTL_GIGA_MAC_VER_33)
		rtl_ephy_write(tp, 0x19, 0xff64);

	if (device_may_wakeup(tp_to_dev(tp))) {
		phy_speed_down(tp->phydev, false);
		rtl_wol_suspend_quirk(tp);
		return;
	}

	switch (tp->mac_version) {
	case RTL_GIGA_MAC_VER_25 ... RTL_GIGA_MAC_VER_33:
	case RTL_GIGA_MAC_VER_37:
	case RTL_GIGA_MAC_VER_39:
	case RTL_GIGA_MAC_VER_43:
	case RTL_GIGA_MAC_VER_44:
	case RTL_GIGA_MAC_VER_45:
	case RTL_GIGA_MAC_VER_46:
	case RTL_GIGA_MAC_VER_47:
	case RTL_GIGA_MAC_VER_48:
	case RTL_GIGA_MAC_VER_50:
	case RTL_GIGA_MAC_VER_51:
		RTL_W8(tp, PMCH, RTL_R8(tp, PMCH) & ~0x80);
		break;
	case RTL_GIGA_MAC_VER_40:
	case RTL_GIGA_MAC_VER_41:
	case RTL_GIGA_MAC_VER_49:
		rtl_eri_clear_bits(tp, 0x1a8, ERIAR_MASK_1111, 0xfc000000);
		RTL_W8(tp, PMCH, RTL_R8(tp, PMCH) & ~0x80);
		break;
	default:
		break;
	}
}

static void rtl_pll_power_up(struct rtl8169_private *tp)
{
	switch (tp->mac_version) {
	case RTL_GIGA_MAC_VER_25 ... RTL_GIGA_MAC_VER_33:
	case RTL_GIGA_MAC_VER_37:
	case RTL_GIGA_MAC_VER_39:
	case RTL_GIGA_MAC_VER_43:
		RTL_W8(tp, PMCH, RTL_R8(tp, PMCH) | 0x80);
		break;
	case RTL_GIGA_MAC_VER_44:
	case RTL_GIGA_MAC_VER_45:
	case RTL_GIGA_MAC_VER_46:
	case RTL_GIGA_MAC_VER_47:
	case RTL_GIGA_MAC_VER_48:
	case RTL_GIGA_MAC_VER_50:
	case RTL_GIGA_MAC_VER_51:
		RTL_W8(tp, PMCH, RTL_R8(tp, PMCH) | 0xc0);
		break;
	case RTL_GIGA_MAC_VER_40:
	case RTL_GIGA_MAC_VER_41:
	case RTL_GIGA_MAC_VER_49:
		RTL_W8(tp, PMCH, RTL_R8(tp, PMCH) | 0xc0);
		rtl_eri_set_bits(tp, 0x1a8, ERIAR_MASK_1111, 0xfc000000);
		break;
	default:
		break;
	}

	phy_resume(tp->phydev);
	/* give MAC/PHY some time to resume */
	msleep(20);
}

static void rtl_init_rxcfg(struct rtl8169_private *tp)
{
	switch (tp->mac_version) {
	case RTL_GIGA_MAC_VER_02 ... RTL_GIGA_MAC_VER_06:
	case RTL_GIGA_MAC_VER_10 ... RTL_GIGA_MAC_VER_17:
		RTL_W32(tp, RxConfig, RX_FIFO_THRESH | RX_DMA_BURST);
		break;
	case RTL_GIGA_MAC_VER_18 ... RTL_GIGA_MAC_VER_24:
	case RTL_GIGA_MAC_VER_34 ... RTL_GIGA_MAC_VER_36:
	case RTL_GIGA_MAC_VER_38:
		RTL_W32(tp, RxConfig, RX128_INT_EN | RX_MULTI_EN | RX_DMA_BURST);
		break;
	case RTL_GIGA_MAC_VER_40 ... RTL_GIGA_MAC_VER_51:
		RTL_W32(tp, RxConfig, RX128_INT_EN | RX_MULTI_EN | RX_DMA_BURST | RX_EARLY_OFF);
		break;
	default:
		RTL_W32(tp, RxConfig, RX128_INT_EN | RX_DMA_BURST);
		break;
	}
}

static void rtl8169_init_ring_indexes(struct rtl8169_private *tp)
{
	tp->dirty_tx = tp->cur_tx = tp->cur_rx = 0;
}

static void r8168c_hw_jumbo_enable(struct rtl8169_private *tp)
{
	RTL_W8(tp, Config3, RTL_R8(tp, Config3) | Jumbo_En0);
	RTL_W8(tp, Config4, RTL_R8(tp, Config4) | Jumbo_En1);
	rtl_tx_performance_tweak(tp, PCI_EXP_DEVCTL_READRQ_512B);
}

static void r8168c_hw_jumbo_disable(struct rtl8169_private *tp)
{
	RTL_W8(tp, Config3, RTL_R8(tp, Config3) & ~Jumbo_En0);
	RTL_W8(tp, Config4, RTL_R8(tp, Config4) & ~Jumbo_En1);
	rtl_tx_performance_tweak(tp, PCI_EXP_DEVCTL_READRQ_4096B);
}

static void r8168dp_hw_jumbo_enable(struct rtl8169_private *tp)
{
	RTL_W8(tp, Config3, RTL_R8(tp, Config3) | Jumbo_En0);
}

static void r8168dp_hw_jumbo_disable(struct rtl8169_private *tp)
{
	RTL_W8(tp, Config3, RTL_R8(tp, Config3) & ~Jumbo_En0);
}

static void r8168e_hw_jumbo_enable(struct rtl8169_private *tp)
{
	RTL_W8(tp, MaxTxPacketSize, 0x3f);
	RTL_W8(tp, Config3, RTL_R8(tp, Config3) | Jumbo_En0);
	RTL_W8(tp, Config4, RTL_R8(tp, Config4) | 0x01);
	rtl_tx_performance_tweak(tp, PCI_EXP_DEVCTL_READRQ_512B);
}

static void r8168e_hw_jumbo_disable(struct rtl8169_private *tp)
{
	RTL_W8(tp, MaxTxPacketSize, 0x0c);
	RTL_W8(tp, Config3, RTL_R8(tp, Config3) & ~Jumbo_En0);
	RTL_W8(tp, Config4, RTL_R8(tp, Config4) & ~0x01);
	rtl_tx_performance_tweak(tp, PCI_EXP_DEVCTL_READRQ_4096B);
}

static void r8168b_0_hw_jumbo_enable(struct rtl8169_private *tp)
{
	rtl_tx_performance_tweak(tp,
		PCI_EXP_DEVCTL_READRQ_512B | PCI_EXP_DEVCTL_NOSNOOP_EN);
}

static void r8168b_0_hw_jumbo_disable(struct rtl8169_private *tp)
{
	rtl_tx_performance_tweak(tp,
		PCI_EXP_DEVCTL_READRQ_4096B | PCI_EXP_DEVCTL_NOSNOOP_EN);
}

static void r8168b_1_hw_jumbo_enable(struct rtl8169_private *tp)
{
	r8168b_0_hw_jumbo_enable(tp);

	RTL_W8(tp, Config4, RTL_R8(tp, Config4) | (1 << 0));
}

static void r8168b_1_hw_jumbo_disable(struct rtl8169_private *tp)
{
	r8168b_0_hw_jumbo_disable(tp);

	RTL_W8(tp, Config4, RTL_R8(tp, Config4) & ~(1 << 0));
}

static void rtl_hw_jumbo_enable(struct rtl8169_private *tp)
{
	rtl_unlock_config_regs(tp);
	switch (tp->mac_version) {
	case RTL_GIGA_MAC_VER_11:
		r8168b_0_hw_jumbo_enable(tp);
		break;
	case RTL_GIGA_MAC_VER_12:
	case RTL_GIGA_MAC_VER_17:
		r8168b_1_hw_jumbo_enable(tp);
		break;
	case RTL_GIGA_MAC_VER_18 ... RTL_GIGA_MAC_VER_26:
		r8168c_hw_jumbo_enable(tp);
		break;
	case RTL_GIGA_MAC_VER_27 ... RTL_GIGA_MAC_VER_28:
		r8168dp_hw_jumbo_enable(tp);
		break;
	case RTL_GIGA_MAC_VER_31 ... RTL_GIGA_MAC_VER_34:
		r8168e_hw_jumbo_enable(tp);
		break;
	default:
		break;
	}
	rtl_lock_config_regs(tp);
}

static void rtl_hw_jumbo_disable(struct rtl8169_private *tp)
{
	rtl_unlock_config_regs(tp);
	switch (tp->mac_version) {
	case RTL_GIGA_MAC_VER_11:
		r8168b_0_hw_jumbo_disable(tp);
		break;
	case RTL_GIGA_MAC_VER_12:
	case RTL_GIGA_MAC_VER_17:
		r8168b_1_hw_jumbo_disable(tp);
		break;
	case RTL_GIGA_MAC_VER_18 ... RTL_GIGA_MAC_VER_26:
		r8168c_hw_jumbo_disable(tp);
		break;
	case RTL_GIGA_MAC_VER_27 ... RTL_GIGA_MAC_VER_28:
		r8168dp_hw_jumbo_disable(tp);
		break;
	case RTL_GIGA_MAC_VER_31 ... RTL_GIGA_MAC_VER_34:
		r8168e_hw_jumbo_disable(tp);
		break;
	default:
		break;
	}
	rtl_lock_config_regs(tp);
}

DECLARE_RTL_COND(rtl_chipcmd_cond)
{
	return RTL_R8(tp, ChipCmd) & CmdReset;
}

static void rtl_hw_reset(struct rtl8169_private *tp)
{
	RTL_W8(tp, ChipCmd, CmdReset);

	rtl_udelay_loop_wait_low(tp, &rtl_chipcmd_cond, 100, 100);
}

static void rtl_request_firmware(struct rtl8169_private *tp)
{
	struct rtl_fw *rtl_fw;

	/* firmware loaded already or no firmware available */
	if (tp->rtl_fw || !tp->fw_name)
		return;

	rtl_fw = kzalloc(sizeof(*rtl_fw), GFP_KERNEL);
	if (!rtl_fw) {
		netif_warn(tp, ifup, tp->dev, "Unable to load firmware, out of memory\n");
		return;
	}

	rtl_fw->phy_write = rtl_writephy;
	rtl_fw->phy_read = rtl_readphy;
	rtl_fw->mac_mcu_write = mac_mcu_write;
	rtl_fw->mac_mcu_read = mac_mcu_read;
	rtl_fw->fw_name = tp->fw_name;
	rtl_fw->dev = tp_to_dev(tp);

	if (rtl_fw_request_firmware(rtl_fw))
		kfree(rtl_fw);
	else
		tp->rtl_fw = rtl_fw;
}

static void rtl_rx_close(struct rtl8169_private *tp)
{
	RTL_W32(tp, RxConfig, RTL_R32(tp, RxConfig) & ~RX_CONFIG_ACCEPT_MASK);
}

DECLARE_RTL_COND(rtl_npq_cond)
{
	return RTL_R8(tp, TxPoll) & NPQ;
}

DECLARE_RTL_COND(rtl_txcfg_empty_cond)
{
	return RTL_R32(tp, TxConfig) & TXCFG_EMPTY;
}

static void rtl8169_hw_reset(struct rtl8169_private *tp)
{
	/* Disable interrupts */
	rtl8169_irq_mask_and_ack(tp);

	rtl_rx_close(tp);

	switch (tp->mac_version) {
	case RTL_GIGA_MAC_VER_27:
	case RTL_GIGA_MAC_VER_28:
	case RTL_GIGA_MAC_VER_31:
		rtl_udelay_loop_wait_low(tp, &rtl_npq_cond, 20, 42*42);
		break;
	case RTL_GIGA_MAC_VER_34 ... RTL_GIGA_MAC_VER_38:
	case RTL_GIGA_MAC_VER_40 ... RTL_GIGA_MAC_VER_51:
		RTL_W8(tp, ChipCmd, RTL_R8(tp, ChipCmd) | StopReq);
		rtl_udelay_loop_wait_high(tp, &rtl_txcfg_empty_cond, 100, 666);
		break;
	default:
		RTL_W8(tp, ChipCmd, RTL_R8(tp, ChipCmd) | StopReq);
		udelay(100);
		break;
	}

	rtl_hw_reset(tp);
}

static void rtl_set_tx_config_registers(struct rtl8169_private *tp)
{
	u32 val = TX_DMA_BURST << TxDMAShift |
		  InterFrameGap << TxInterFrameGapShift;

	if (rtl_is_8168evl_up(tp))
		val |= TXCFG_AUTO_FIFO;

	RTL_W32(tp, TxConfig, val);
}

static void rtl_set_rx_max_size(struct rtl8169_private *tp)
{
	/* Low hurts. Let's disable the filtering. */
	RTL_W16(tp, RxMaxSize, R8169_RX_BUF_SIZE + 1);
}

static void rtl_set_rx_tx_desc_registers(struct rtl8169_private *tp)
{
	/*
	 * Magic spell: some iop3xx ARM board needs the TxDescAddrHigh
	 * register to be written before TxDescAddrLow to work.
	 * Switching from MMIO to I/O access fixes the issue as well.
	 */
	RTL_W32(tp, TxDescStartAddrHigh, ((u64) tp->TxPhyAddr) >> 32);
	RTL_W32(tp, TxDescStartAddrLow, ((u64) tp->TxPhyAddr) & DMA_BIT_MASK(32));
	RTL_W32(tp, RxDescAddrHigh, ((u64) tp->RxPhyAddr) >> 32);
	RTL_W32(tp, RxDescAddrLow, ((u64) tp->RxPhyAddr) & DMA_BIT_MASK(32));
}

static void rtl8169_set_magic_reg(struct rtl8169_private *tp, unsigned mac_version)
{
	u32 val;

	if (tp->mac_version == RTL_GIGA_MAC_VER_05)
		val = 0x000fff00;
	else if (tp->mac_version == RTL_GIGA_MAC_VER_06)
		val = 0x00ffff00;
	else
		return;

	if (RTL_R8(tp, Config2) & PCI_Clock_66MHz)
		val |= 0xff;

	RTL_W32(tp, 0x7c, val);
}

static void rtl_set_rx_mode(struct net_device *dev)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	u32 mc_filter[2];	/* Multicast hash filter */
	int rx_mode;
	u32 tmp = 0;

	if (dev->flags & IFF_PROMISC) {
		/* Unconditionally log net taps. */
		netif_notice(tp, link, dev, "Promiscuous mode enabled\n");
		rx_mode =
		    AcceptBroadcast | AcceptMulticast | AcceptMyPhys |
		    AcceptAllPhys;
		mc_filter[1] = mc_filter[0] = 0xffffffff;
	} else if ((netdev_mc_count(dev) > multicast_filter_limit) ||
		   (dev->flags & IFF_ALLMULTI)) {
		/* Too many to filter perfectly -- accept all multicasts. */
		rx_mode = AcceptBroadcast | AcceptMulticast | AcceptMyPhys;
		mc_filter[1] = mc_filter[0] = 0xffffffff;
	} else {
		struct netdev_hw_addr *ha;

		rx_mode = AcceptBroadcast | AcceptMyPhys;
		mc_filter[1] = mc_filter[0] = 0;
		netdev_for_each_mc_addr(ha, dev) {
			int bit_nr = ether_crc(ETH_ALEN, ha->addr) >> 26;
			mc_filter[bit_nr >> 5] |= 1 << (bit_nr & 31);
			rx_mode |= AcceptMulticast;
		}
	}

	if (dev->features & NETIF_F_RXALL)
		rx_mode |= (AcceptErr | AcceptRunt);

	tmp = (RTL_R32(tp, RxConfig) & ~RX_CONFIG_ACCEPT_MASK) | rx_mode;

	if (tp->mac_version > RTL_GIGA_MAC_VER_06) {
		u32 data = mc_filter[0];

		mc_filter[0] = swab32(mc_filter[1]);
		mc_filter[1] = swab32(data);
	}

	if (tp->mac_version == RTL_GIGA_MAC_VER_35)
		mc_filter[1] = mc_filter[0] = 0xffffffff;

	RTL_W32(tp, MAR0 + 4, mc_filter[1]);
	RTL_W32(tp, MAR0 + 0, mc_filter[0]);

	RTL_W32(tp, RxConfig, tmp);
}

DECLARE_RTL_COND(rtl_csiar_cond)
{
	return RTL_R32(tp, CSIAR) & CSIAR_FLAG;
}

static void rtl_csi_write(struct rtl8169_private *tp, int addr, int value)
{
	u32 func = PCI_FUNC(tp->pci_dev->devfn);

	RTL_W32(tp, CSIDR, value);
	RTL_W32(tp, CSIAR, CSIAR_WRITE_CMD | (addr & CSIAR_ADDR_MASK) |
		CSIAR_BYTE_ENABLE | func << 16);

	rtl_udelay_loop_wait_low(tp, &rtl_csiar_cond, 10, 100);
}

static u32 rtl_csi_read(struct rtl8169_private *tp, int addr)
{
	u32 func = PCI_FUNC(tp->pci_dev->devfn);

	RTL_W32(tp, CSIAR, (addr & CSIAR_ADDR_MASK) | func << 16 |
		CSIAR_BYTE_ENABLE);

	return rtl_udelay_loop_wait_high(tp, &rtl_csiar_cond, 10, 100) ?
		RTL_R32(tp, CSIDR) : ~0;
}

static void rtl_csi_access_enable(struct rtl8169_private *tp, u8 val)
{
	struct pci_dev *pdev = tp->pci_dev;
	u32 csi;

	/* According to Realtek the value at config space address 0x070f
	 * controls the L0s/L1 entrance latency. We try standard ECAM access
	 * first and if it fails fall back to CSI.
	 */
	if (pdev->cfg_size > 0x070f &&
	    pci_write_config_byte(pdev, 0x070f, val) == PCIBIOS_SUCCESSFUL)
		return;

	netdev_notice_once(tp->dev,
		"No native access to PCI extended config space, falling back to CSI\n");
	csi = rtl_csi_read(tp, 0x070c) & 0x00ffffff;
	rtl_csi_write(tp, 0x070c, csi | val << 24);
}

static void rtl_set_def_aspm_entry_latency(struct rtl8169_private *tp)
{
	rtl_csi_access_enable(tp, 0x27);
}

struct ephy_info {
	unsigned int offset;
	u16 mask;
	u16 bits;
};

static void __rtl_ephy_init(struct rtl8169_private *tp,
			    const struct ephy_info *e, int len)
{
	u16 w;

	while (len-- > 0) {
		w = (rtl_ephy_read(tp, e->offset) & ~e->mask) | e->bits;
		rtl_ephy_write(tp, e->offset, w);
		e++;
	}
}

#define rtl_ephy_init(tp, a) __rtl_ephy_init(tp, a, ARRAY_SIZE(a))

static void rtl_disable_clock_request(struct rtl8169_private *tp)
{
	pcie_capability_clear_word(tp->pci_dev, PCI_EXP_LNKCTL,
				   PCI_EXP_LNKCTL_CLKREQ_EN);
}

static void rtl_enable_clock_request(struct rtl8169_private *tp)
{
	pcie_capability_set_word(tp->pci_dev, PCI_EXP_LNKCTL,
				 PCI_EXP_LNKCTL_CLKREQ_EN);
}

static void rtl_pcie_state_l2l3_disable(struct rtl8169_private *tp)
{
	/* work around an issue when PCI reset occurs during L2/L3 state */
	RTL_W8(tp, Config3, RTL_R8(tp, Config3) & ~Rdy_to_L23);
}

static void rtl_hw_aspm_clkreq_enable(struct rtl8169_private *tp, bool enable)
{
	/* Don't enable ASPM in the chip if OS can't control ASPM */
	if (enable && tp->aspm_manageable) {
		RTL_W8(tp, Config5, RTL_R8(tp, Config5) | ASPM_en);
		RTL_W8(tp, Config2, RTL_R8(tp, Config2) | ClkReqEn);
	} else {
		RTL_W8(tp, Config2, RTL_R8(tp, Config2) & ~ClkReqEn);
		RTL_W8(tp, Config5, RTL_R8(tp, Config5) & ~ASPM_en);
	}

	udelay(10);
}

static void rtl_set_fifo_size(struct rtl8169_private *tp, u16 rx_stat,
			      u16 tx_stat, u16 rx_dyn, u16 tx_dyn)
{
	/* Usage of dynamic vs. static FIFO is controlled by bit
	 * TXCFG_AUTO_FIFO. Exact meaning of FIFO values isn't known.
	 */
	rtl_eri_write(tp, 0xc8, ERIAR_MASK_1111, (rx_stat << 16) | rx_dyn);
	rtl_eri_write(tp, 0xe8, ERIAR_MASK_1111, (tx_stat << 16) | tx_dyn);
}

static void rtl8168g_set_pause_thresholds(struct rtl8169_private *tp,
					  u8 low, u8 high)
{
	/* FIFO thresholds for pause flow control */
	rtl_eri_write(tp, 0xcc, ERIAR_MASK_0001, low);
	rtl_eri_write(tp, 0xd0, ERIAR_MASK_0001, high);
}

static void rtl_hw_start_8168bb(struct rtl8169_private *tp)
{
	RTL_W8(tp, Config3, RTL_R8(tp, Config3) & ~Beacon_en);

	if (tp->dev->mtu <= ETH_DATA_LEN) {
		rtl_tx_performance_tweak(tp, PCI_EXP_DEVCTL_READRQ_4096B |
					 PCI_EXP_DEVCTL_NOSNOOP_EN);
	}
}

static void rtl_hw_start_8168bef(struct rtl8169_private *tp)
{
	rtl_hw_start_8168bb(tp);

	RTL_W8(tp, Config4, RTL_R8(tp, Config4) & ~(1 << 0));
}

static void __rtl_hw_start_8168cp(struct rtl8169_private *tp)
{
	RTL_W8(tp, Config1, RTL_R8(tp, Config1) | Speed_down);

	RTL_W8(tp, Config3, RTL_R8(tp, Config3) & ~Beacon_en);

	if (tp->dev->mtu <= ETH_DATA_LEN)
		rtl_tx_performance_tweak(tp, PCI_EXP_DEVCTL_READRQ_4096B);

	rtl_disable_clock_request(tp);
}

static void rtl_hw_start_8168cp_1(struct rtl8169_private *tp)
{
	static const struct ephy_info e_info_8168cp[] = {
		{ 0x01, 0,	0x0001 },
		{ 0x02, 0x0800,	0x1000 },
		{ 0x03, 0,	0x0042 },
		{ 0x06, 0x0080,	0x0000 },
		{ 0x07, 0,	0x2000 }
	};

	rtl_set_def_aspm_entry_latency(tp);

	rtl_ephy_init(tp, e_info_8168cp);

	__rtl_hw_start_8168cp(tp);
}

static void rtl_hw_start_8168cp_2(struct rtl8169_private *tp)
{
	rtl_set_def_aspm_entry_latency(tp);

	RTL_W8(tp, Config3, RTL_R8(tp, Config3) & ~Beacon_en);

	if (tp->dev->mtu <= ETH_DATA_LEN)
		rtl_tx_performance_tweak(tp, PCI_EXP_DEVCTL_READRQ_4096B);
}

static void rtl_hw_start_8168cp_3(struct rtl8169_private *tp)
{
	rtl_set_def_aspm_entry_latency(tp);

	RTL_W8(tp, Config3, RTL_R8(tp, Config3) & ~Beacon_en);

	/* Magic. */
	RTL_W8(tp, DBG_REG, 0x20);

	if (tp->dev->mtu <= ETH_DATA_LEN)
		rtl_tx_performance_tweak(tp, PCI_EXP_DEVCTL_READRQ_4096B);
}

static void rtl_hw_start_8168c_1(struct rtl8169_private *tp)
{
	static const struct ephy_info e_info_8168c_1[] = {
		{ 0x02, 0x0800,	0x1000 },
		{ 0x03, 0,	0x0002 },
		{ 0x06, 0x0080,	0x0000 }
	};

	rtl_set_def_aspm_entry_latency(tp);

	RTL_W8(tp, DBG_REG, 0x06 | FIX_NAK_1 | FIX_NAK_2);

	rtl_ephy_init(tp, e_info_8168c_1);

	__rtl_hw_start_8168cp(tp);
}

static void rtl_hw_start_8168c_2(struct rtl8169_private *tp)
{
	static const struct ephy_info e_info_8168c_2[] = {
		{ 0x01, 0,	0x0001 },
		{ 0x03, 0x0400,	0x0220 }
	};

	rtl_set_def_aspm_entry_latency(tp);

	rtl_ephy_init(tp, e_info_8168c_2);

	__rtl_hw_start_8168cp(tp);
}

static void rtl_hw_start_8168c_3(struct rtl8169_private *tp)
{
	rtl_hw_start_8168c_2(tp);
}

static void rtl_hw_start_8168c_4(struct rtl8169_private *tp)
{
	rtl_set_def_aspm_entry_latency(tp);

	__rtl_hw_start_8168cp(tp);
}

static void rtl_hw_start_8168d(struct rtl8169_private *tp)
{
	rtl_set_def_aspm_entry_latency(tp);

	rtl_disable_clock_request(tp);

	if (tp->dev->mtu <= ETH_DATA_LEN)
		rtl_tx_performance_tweak(tp, PCI_EXP_DEVCTL_READRQ_4096B);
}

static void rtl_hw_start_8168dp(struct rtl8169_private *tp)
{
	rtl_set_def_aspm_entry_latency(tp);

	if (tp->dev->mtu <= ETH_DATA_LEN)
		rtl_tx_performance_tweak(tp, PCI_EXP_DEVCTL_READRQ_4096B);

	rtl_disable_clock_request(tp);
}

static void rtl_hw_start_8168d_4(struct rtl8169_private *tp)
{
	static const struct ephy_info e_info_8168d_4[] = {
		{ 0x0b, 0x0000,	0x0048 },
		{ 0x19, 0x0020,	0x0050 },
		{ 0x0c, 0x0100,	0x0020 }
	};

	rtl_set_def_aspm_entry_latency(tp);

	rtl_tx_performance_tweak(tp, PCI_EXP_DEVCTL_READRQ_4096B);

	rtl_ephy_init(tp, e_info_8168d_4);

	rtl_enable_clock_request(tp);
}

static void rtl_hw_start_8168e_1(struct rtl8169_private *tp)
{
	static const struct ephy_info e_info_8168e_1[] = {
		{ 0x00, 0x0200,	0x0100 },
		{ 0x00, 0x0000,	0x0004 },
		{ 0x06, 0x0002,	0x0001 },
		{ 0x06, 0x0000,	0x0030 },
		{ 0x07, 0x0000,	0x2000 },
		{ 0x00, 0x0000,	0x0020 },
		{ 0x03, 0x5800,	0x2000 },
		{ 0x03, 0x0000,	0x0001 },
		{ 0x01, 0x0800,	0x1000 },
		{ 0x07, 0x0000,	0x4000 },
		{ 0x1e, 0x0000,	0x2000 },
		{ 0x19, 0xffff,	0xfe6c },
		{ 0x0a, 0x0000,	0x0040 }
	};

	rtl_set_def_aspm_entry_latency(tp);

	rtl_ephy_init(tp, e_info_8168e_1);

	if (tp->dev->mtu <= ETH_DATA_LEN)
		rtl_tx_performance_tweak(tp, PCI_EXP_DEVCTL_READRQ_4096B);

	rtl_disable_clock_request(tp);

	/* Reset tx FIFO pointer */
	RTL_W32(tp, MISC, RTL_R32(tp, MISC) | TXPLA_RST);
	RTL_W32(tp, MISC, RTL_R32(tp, MISC) & ~TXPLA_RST);

	RTL_W8(tp, Config5, RTL_R8(tp, Config5) & ~Spi_en);
}

static void rtl_hw_start_8168e_2(struct rtl8169_private *tp)
{
	static const struct ephy_info e_info_8168e_2[] = {
		{ 0x09, 0x0000,	0x0080 },
		{ 0x19, 0x0000,	0x0224 }
	};

	rtl_set_def_aspm_entry_latency(tp);

	rtl_ephy_init(tp, e_info_8168e_2);

	if (tp->dev->mtu <= ETH_DATA_LEN)
		rtl_tx_performance_tweak(tp, PCI_EXP_DEVCTL_READRQ_4096B);

	rtl_eri_write(tp, 0xc0, ERIAR_MASK_0011, 0x0000);
	rtl_eri_write(tp, 0xb8, ERIAR_MASK_0011, 0x0000);
	rtl_set_fifo_size(tp, 0x10, 0x10, 0x02, 0x06);
	rtl_eri_write(tp, 0xcc, ERIAR_MASK_1111, 0x00000050);
	rtl_eri_write(tp, 0xd0, ERIAR_MASK_1111, 0x07ff0060);
	rtl_eri_set_bits(tp, 0x1b0, ERIAR_MASK_0001, BIT(4));
	rtl_w0w1_eri(tp, 0x0d4, ERIAR_MASK_0011, 0x0c00, 0xff00);

	rtl_disable_clock_request(tp);

	RTL_W8(tp, MCU, RTL_R8(tp, MCU) & ~NOW_IS_OOB);

	rtl8168_config_eee_mac(tp);

	RTL_W8(tp, DLLPR, RTL_R8(tp, DLLPR) | PFM_EN);
	RTL_W32(tp, MISC, RTL_R32(tp, MISC) | PWM_EN);
	RTL_W8(tp, Config5, RTL_R8(tp, Config5) & ~Spi_en);

	rtl_hw_aspm_clkreq_enable(tp, true);
}

static void rtl_hw_start_8168f(struct rtl8169_private *tp)
{
	rtl_set_def_aspm_entry_latency(tp);

	rtl_tx_performance_tweak(tp, PCI_EXP_DEVCTL_READRQ_4096B);

	rtl_eri_write(tp, 0xc0, ERIAR_MASK_0011, 0x0000);
	rtl_eri_write(tp, 0xb8, ERIAR_MASK_0011, 0x0000);
	rtl_set_fifo_size(tp, 0x10, 0x10, 0x02, 0x06);
	rtl_reset_packet_filter(tp);
	rtl_eri_set_bits(tp, 0x1b0, ERIAR_MASK_0001, BIT(4));
	rtl_eri_set_bits(tp, 0x1d0, ERIAR_MASK_0001, BIT(4));
	rtl_eri_write(tp, 0xcc, ERIAR_MASK_1111, 0x00000050);
	rtl_eri_write(tp, 0xd0, ERIAR_MASK_1111, 0x00000060);

	rtl_disable_clock_request(tp);

	RTL_W8(tp, MCU, RTL_R8(tp, MCU) & ~NOW_IS_OOB);
	RTL_W8(tp, DLLPR, RTL_R8(tp, DLLPR) | PFM_EN);
	RTL_W32(tp, MISC, RTL_R32(tp, MISC) | PWM_EN);
	RTL_W8(tp, Config5, RTL_R8(tp, Config5) & ~Spi_en);

	rtl8168_config_eee_mac(tp);
}

static void rtl_hw_start_8168f_1(struct rtl8169_private *tp)
{
	static const struct ephy_info e_info_8168f_1[] = {
		{ 0x06, 0x00c0,	0x0020 },
		{ 0x08, 0x0001,	0x0002 },
		{ 0x09, 0x0000,	0x0080 },
		{ 0x19, 0x0000,	0x0224 }
	};

	rtl_hw_start_8168f(tp);

	rtl_ephy_init(tp, e_info_8168f_1);

	rtl_w0w1_eri(tp, 0x0d4, ERIAR_MASK_0011, 0x0c00, 0xff00);
}

static void rtl_hw_start_8411(struct rtl8169_private *tp)
{
	static const struct ephy_info e_info_8168f_1[] = {
		{ 0x06, 0x00c0,	0x0020 },
		{ 0x0f, 0xffff,	0x5200 },
		{ 0x1e, 0x0000,	0x4000 },
		{ 0x19, 0x0000,	0x0224 }
	};

	rtl_hw_start_8168f(tp);
	rtl_pcie_state_l2l3_disable(tp);

	rtl_ephy_init(tp, e_info_8168f_1);

	rtl_eri_set_bits(tp, 0x0d4, ERIAR_MASK_0011, 0x0c00);
}

static void rtl_hw_start_8168g(struct rtl8169_private *tp)
{
	rtl_set_fifo_size(tp, 0x08, 0x10, 0x02, 0x06);
	rtl8168g_set_pause_thresholds(tp, 0x38, 0x48);

	rtl_set_def_aspm_entry_latency(tp);

	rtl_tx_performance_tweak(tp, PCI_EXP_DEVCTL_READRQ_4096B);

	rtl_reset_packet_filter(tp);
	rtl_eri_write(tp, 0x2f8, ERIAR_MASK_0011, 0x1d8f);

	RTL_W32(tp, MISC, RTL_R32(tp, MISC) & ~RXDV_GATED_EN);

	rtl_eri_write(tp, 0xc0, ERIAR_MASK_0011, 0x0000);
	rtl_eri_write(tp, 0xb8, ERIAR_MASK_0011, 0x0000);

	rtl8168_config_eee_mac(tp);

	rtl_w0w1_eri(tp, 0x2fc, ERIAR_MASK_0001, 0x01, 0x06);
	rtl_eri_clear_bits(tp, 0x1b0, ERIAR_MASK_0011, BIT(12));

	rtl_pcie_state_l2l3_disable(tp);
}

static void rtl_hw_start_8168g_1(struct rtl8169_private *tp)
{
	static const struct ephy_info e_info_8168g_1[] = {
		{ 0x00, 0x0000,	0x0008 },
		{ 0x0c, 0x37d0,	0x0820 },
		{ 0x1e, 0x0000,	0x0001 },
		{ 0x19, 0x8000,	0x0000 }
	};

	rtl_hw_start_8168g(tp);

	/* disable aspm and clock request before access ephy */
	rtl_hw_aspm_clkreq_enable(tp, false);
	rtl_ephy_init(tp, e_info_8168g_1);
	rtl_hw_aspm_clkreq_enable(tp, true);
}

static void rtl_hw_start_8168g_2(struct rtl8169_private *tp)
{
	static const struct ephy_info e_info_8168g_2[] = {
		{ 0x00, 0x0000,	0x0008 },
		{ 0x0c, 0x3df0,	0x0200 },
		{ 0x19, 0xffff,	0xfc00 },
		{ 0x1e, 0xffff,	0x20eb }
	};

	rtl_hw_start_8168g(tp);

	/* disable aspm and clock request before access ephy */
	RTL_W8(tp, Config2, RTL_R8(tp, Config2) & ~ClkReqEn);
	RTL_W8(tp, Config5, RTL_R8(tp, Config5) & ~ASPM_en);
	rtl_ephy_init(tp, e_info_8168g_2);
}

static void rtl_hw_start_8411_2(struct rtl8169_private *tp)
{
	static const struct ephy_info e_info_8411_2[] = {
		{ 0x00, 0x0000,	0x0008 },
		{ 0x0c, 0x3df0,	0x0200 },
		{ 0x0f, 0xffff,	0x5200 },
		{ 0x19, 0x0020,	0x0000 },
		{ 0x1e, 0x0000,	0x2000 }
	};

	rtl_hw_start_8168g(tp);

	/* disable aspm and clock request before access ephy */
	rtl_hw_aspm_clkreq_enable(tp, false);
	rtl_ephy_init(tp, e_info_8411_2);
	rtl_hw_aspm_clkreq_enable(tp, true);
}

static void rtl_hw_start_8168h_1(struct rtl8169_private *tp)
{
	int rg_saw_cnt;
	u32 data;
	static const struct ephy_info e_info_8168h_1[] = {
		{ 0x1e, 0x0800,	0x0001 },
		{ 0x1d, 0x0000,	0x0800 },
		{ 0x05, 0xffff,	0x2089 },
		{ 0x06, 0xffff,	0x5881 },
		{ 0x04, 0xffff,	0x154a },
		{ 0x01, 0xffff,	0x068b }
	};

	/* disable aspm and clock request before access ephy */
	rtl_hw_aspm_clkreq_enable(tp, false);
	rtl_ephy_init(tp, e_info_8168h_1);

	rtl_set_fifo_size(tp, 0x08, 0x10, 0x02, 0x06);
	rtl8168g_set_pause_thresholds(tp, 0x38, 0x48);

	rtl_set_def_aspm_entry_latency(tp);

	rtl_tx_performance_tweak(tp, PCI_EXP_DEVCTL_READRQ_4096B);

	rtl_reset_packet_filter(tp);

	rtl_eri_set_bits(tp, 0xdc, ERIAR_MASK_1111, BIT(4));

	rtl_eri_set_bits(tp, 0xd4, ERIAR_MASK_1111, 0x1f00);

	rtl_eri_write(tp, 0x5f0, ERIAR_MASK_0011, 0x4f87);

	RTL_W32(tp, MISC, RTL_R32(tp, MISC) & ~RXDV_GATED_EN);

	rtl_eri_write(tp, 0xc0, ERIAR_MASK_0011, 0x0000);
	rtl_eri_write(tp, 0xb8, ERIAR_MASK_0011, 0x0000);

	rtl8168_config_eee_mac(tp);

	RTL_W8(tp, DLLPR, RTL_R8(tp, DLLPR) & ~PFM_EN);
	RTL_W8(tp, MISC_1, RTL_R8(tp, MISC_1) & ~PFM_D3COLD_EN);

	RTL_W8(tp, DLLPR, RTL_R8(tp, DLLPR) & ~TX_10M_PS_EN);

	rtl_eri_clear_bits(tp, 0x1b0, ERIAR_MASK_0011, BIT(12));

	rtl_pcie_state_l2l3_disable(tp);

	rtl_writephy(tp, 0x1f, 0x0c42);
	rg_saw_cnt = (rtl_readphy(tp, 0x13) & 0x3fff);
	rtl_writephy(tp, 0x1f, 0x0000);
	if (rg_saw_cnt > 0) {
		u16 sw_cnt_1ms_ini;

		sw_cnt_1ms_ini = 16000000/rg_saw_cnt;
		sw_cnt_1ms_ini &= 0x0fff;
		data = r8168_mac_ocp_read(tp, 0xd412);
		data &= ~0x0fff;
		data |= sw_cnt_1ms_ini;
		r8168_mac_ocp_write(tp, 0xd412, data);
	}

	data = r8168_mac_ocp_read(tp, 0xe056);
	data &= ~0xf0;
	data |= 0x70;
	r8168_mac_ocp_write(tp, 0xe056, data);

	data = r8168_mac_ocp_read(tp, 0xe052);
	data &= ~0x6000;
	data |= 0x8008;
	r8168_mac_ocp_write(tp, 0xe052, data);

	data = r8168_mac_ocp_read(tp, 0xe0d6);
	data &= ~0x01ff;
	data |= 0x017f;
	r8168_mac_ocp_write(tp, 0xe0d6, data);

	data = r8168_mac_ocp_read(tp, 0xd420);
	data &= ~0x0fff;
	data |= 0x047f;
	r8168_mac_ocp_write(tp, 0xd420, data);

	r8168_mac_ocp_write(tp, 0xe63e, 0x0001);
	r8168_mac_ocp_write(tp, 0xe63e, 0x0000);
	r8168_mac_ocp_write(tp, 0xc094, 0x0000);
	r8168_mac_ocp_write(tp, 0xc09e, 0x0000);

	rtl_hw_aspm_clkreq_enable(tp, true);
}

static void rtl_hw_start_8168ep(struct rtl8169_private *tp)
{
	rtl8168ep_stop_cmac(tp);

	rtl_set_fifo_size(tp, 0x08, 0x10, 0x02, 0x06);
	rtl8168g_set_pause_thresholds(tp, 0x2f, 0x5f);

	rtl_set_def_aspm_entry_latency(tp);

	rtl_tx_performance_tweak(tp, PCI_EXP_DEVCTL_READRQ_4096B);

	rtl_reset_packet_filter(tp);

	rtl_eri_set_bits(tp, 0xd4, ERIAR_MASK_1111, 0x1f80);

	rtl_eri_write(tp, 0x5f0, ERIAR_MASK_0011, 0x4f87);

	RTL_W32(tp, MISC, RTL_R32(tp, MISC) & ~RXDV_GATED_EN);

	rtl_eri_write(tp, 0xc0, ERIAR_MASK_0011, 0x0000);
	rtl_eri_write(tp, 0xb8, ERIAR_MASK_0011, 0x0000);

	rtl8168_config_eee_mac(tp);

	rtl_w0w1_eri(tp, 0x2fc, ERIAR_MASK_0001, 0x01, 0x06);

	RTL_W8(tp, DLLPR, RTL_R8(tp, DLLPR) & ~TX_10M_PS_EN);

	rtl_pcie_state_l2l3_disable(tp);
}

static void rtl_hw_start_8168ep_1(struct rtl8169_private *tp)
{
	static const struct ephy_info e_info_8168ep_1[] = {
		{ 0x00, 0xffff,	0x10ab },
		{ 0x06, 0xffff,	0xf030 },
		{ 0x08, 0xffff,	0x2006 },
		{ 0x0d, 0xffff,	0x1666 },
		{ 0x0c, 0x3ff0,	0x0000 }
	};

	/* disable aspm and clock request before access ephy */
	rtl_hw_aspm_clkreq_enable(tp, false);
	rtl_ephy_init(tp, e_info_8168ep_1);

	rtl_hw_start_8168ep(tp);

	rtl_hw_aspm_clkreq_enable(tp, true);
}

static void rtl_hw_start_8168ep_2(struct rtl8169_private *tp)
{
	static const struct ephy_info e_info_8168ep_2[] = {
		{ 0x00, 0xffff,	0x10a3 },
		{ 0x19, 0xffff,	0xfc00 },
		{ 0x1e, 0xffff,	0x20ea }
	};

	/* disable aspm and clock request before access ephy */
	rtl_hw_aspm_clkreq_enable(tp, false);
	rtl_ephy_init(tp, e_info_8168ep_2);

	rtl_hw_start_8168ep(tp);

	RTL_W8(tp, DLLPR, RTL_R8(tp, DLLPR) & ~PFM_EN);
	RTL_W8(tp, MISC_1, RTL_R8(tp, MISC_1) & ~PFM_D3COLD_EN);

	rtl_hw_aspm_clkreq_enable(tp, true);
}

static void rtl_hw_start_8168ep_3(struct rtl8169_private *tp)
{
	u32 data;
	static const struct ephy_info e_info_8168ep_3[] = {
		{ 0x00, 0xffff,	0x10a3 },
		{ 0x19, 0xffff,	0x7c00 },
		{ 0x1e, 0xffff,	0x20eb },
		{ 0x0d, 0xffff,	0x1666 }
	};

	/* disable aspm and clock request before access ephy */
	rtl_hw_aspm_clkreq_enable(tp, false);
	rtl_ephy_init(tp, e_info_8168ep_3);

	rtl_hw_start_8168ep(tp);

	RTL_W8(tp, DLLPR, RTL_R8(tp, DLLPR) & ~PFM_EN);
	RTL_W8(tp, MISC_1, RTL_R8(tp, MISC_1) & ~PFM_D3COLD_EN);

	data = r8168_mac_ocp_read(tp, 0xd3e2);
	data &= 0xf000;
	data |= 0x0271;
	r8168_mac_ocp_write(tp, 0xd3e2, data);

	data = r8168_mac_ocp_read(tp, 0xd3e4);
	data &= 0xff00;
	r8168_mac_ocp_write(tp, 0xd3e4, data);

	data = r8168_mac_ocp_read(tp, 0xe860);
	data |= 0x0080;
	r8168_mac_ocp_write(tp, 0xe860, data);

	rtl_hw_aspm_clkreq_enable(tp, true);
}

static void rtl_hw_start_8102e_1(struct rtl8169_private *tp)
{
	static const struct ephy_info e_info_8102e_1[] = {
		{ 0x01,	0, 0x6e65 },
		{ 0x02,	0, 0x091f },
		{ 0x03,	0, 0xc2f9 },
		{ 0x06,	0, 0xafb5 },
		{ 0x07,	0, 0x0e00 },
		{ 0x19,	0, 0xec80 },
		{ 0x01,	0, 0x2e65 },
		{ 0x01,	0, 0x6e65 }
	};
	u8 cfg1;

	rtl_set_def_aspm_entry_latency(tp);

	RTL_W8(tp, DBG_REG, FIX_NAK_1);

	rtl_tx_performance_tweak(tp, PCI_EXP_DEVCTL_READRQ_4096B);

	RTL_W8(tp, Config1,
	       LEDS1 | LEDS0 | Speed_down | MEMMAP | IOMAP | VPD | PMEnable);
	RTL_W8(tp, Config3, RTL_R8(tp, Config3) & ~Beacon_en);

	cfg1 = RTL_R8(tp, Config1);
	if ((cfg1 & LEDS0) && (cfg1 & LEDS1))
		RTL_W8(tp, Config1, cfg1 & ~LEDS0);

	rtl_ephy_init(tp, e_info_8102e_1);
}

static void rtl_hw_start_8102e_2(struct rtl8169_private *tp)
{
	rtl_set_def_aspm_entry_latency(tp);

	rtl_tx_performance_tweak(tp, PCI_EXP_DEVCTL_READRQ_4096B);

	RTL_W8(tp, Config1, MEMMAP | IOMAP | VPD | PMEnable);
	RTL_W8(tp, Config3, RTL_R8(tp, Config3) & ~Beacon_en);
}

static void rtl_hw_start_8102e_3(struct rtl8169_private *tp)
{
	rtl_hw_start_8102e_2(tp);

	rtl_ephy_write(tp, 0x03, 0xc2f9);
}

static void rtl_hw_start_8105e_1(struct rtl8169_private *tp)
{
	static const struct ephy_info e_info_8105e_1[] = {
		{ 0x07,	0, 0x4000 },
		{ 0x19,	0, 0x0200 },
		{ 0x19,	0, 0x0020 },
		{ 0x1e,	0, 0x2000 },
		{ 0x03,	0, 0x0001 },
		{ 0x19,	0, 0x0100 },
		{ 0x19,	0, 0x0004 },
		{ 0x0a,	0, 0x0020 }
	};

	/* Force LAN exit from ASPM if Rx/Tx are not idle */
	RTL_W32(tp, FuncEvent, RTL_R32(tp, FuncEvent) | 0x002800);

	/* Disable Early Tally Counter */
	RTL_W32(tp, FuncEvent, RTL_R32(tp, FuncEvent) & ~0x010000);

	RTL_W8(tp, MCU, RTL_R8(tp, MCU) | EN_NDP | EN_OOB_RESET);
	RTL_W8(tp, DLLPR, RTL_R8(tp, DLLPR) | PFM_EN);

	rtl_ephy_init(tp, e_info_8105e_1);

	rtl_pcie_state_l2l3_disable(tp);
}

static void rtl_hw_start_8105e_2(struct rtl8169_private *tp)
{
	rtl_hw_start_8105e_1(tp);
	rtl_ephy_write(tp, 0x1e, rtl_ephy_read(tp, 0x1e) | 0x8000);
}

static void rtl_hw_start_8402(struct rtl8169_private *tp)
{
	static const struct ephy_info e_info_8402[] = {
		{ 0x19,	0xffff, 0xff64 },
		{ 0x1e,	0, 0x4000 }
	};

	rtl_set_def_aspm_entry_latency(tp);

	/* Force LAN exit from ASPM if Rx/Tx are not idle */
	RTL_W32(tp, FuncEvent, RTL_R32(tp, FuncEvent) | 0x002800);

	RTL_W8(tp, MCU, RTL_R8(tp, MCU) & ~NOW_IS_OOB);

	rtl_ephy_init(tp, e_info_8402);

	rtl_tx_performance_tweak(tp, PCI_EXP_DEVCTL_READRQ_4096B);

	rtl_set_fifo_size(tp, 0x00, 0x00, 0x02, 0x06);
	rtl_reset_packet_filter(tp);
	rtl_eri_write(tp, 0xc0, ERIAR_MASK_0011, 0x0000);
	rtl_eri_write(tp, 0xb8, ERIAR_MASK_0011, 0x0000);
	rtl_w0w1_eri(tp, 0x0d4, ERIAR_MASK_0011, 0x0e00, 0xff00);

	rtl_pcie_state_l2l3_disable(tp);
}

static void rtl_hw_start_8106(struct rtl8169_private *tp)
{
	rtl_hw_aspm_clkreq_enable(tp, false);

	/* Force LAN exit from ASPM if Rx/Tx are not idle */
	RTL_W32(tp, FuncEvent, RTL_R32(tp, FuncEvent) | 0x002800);

	RTL_W32(tp, MISC, (RTL_R32(tp, MISC) | DISABLE_LAN_EN) & ~EARLY_TALLY_EN);
	RTL_W8(tp, MCU, RTL_R8(tp, MCU) | EN_NDP | EN_OOB_RESET);
	RTL_W8(tp, DLLPR, RTL_R8(tp, DLLPR) & ~PFM_EN);

	rtl_pcie_state_l2l3_disable(tp);
	rtl_hw_aspm_clkreq_enable(tp, true);
}

static void rtl_hw_config(struct rtl8169_private *tp)
{
	static const rtl_generic_fct hw_configs[] = {
		[RTL_GIGA_MAC_VER_07] = rtl_hw_start_8102e_1,
		[RTL_GIGA_MAC_VER_08] = rtl_hw_start_8102e_3,
		[RTL_GIGA_MAC_VER_09] = rtl_hw_start_8102e_2,
		[RTL_GIGA_MAC_VER_10] = NULL,
		[RTL_GIGA_MAC_VER_11] = rtl_hw_start_8168bb,
		[RTL_GIGA_MAC_VER_12] = rtl_hw_start_8168bef,
		[RTL_GIGA_MAC_VER_13] = NULL,
		[RTL_GIGA_MAC_VER_14] = NULL,
		[RTL_GIGA_MAC_VER_15] = NULL,
		[RTL_GIGA_MAC_VER_16] = NULL,
		[RTL_GIGA_MAC_VER_17] = rtl_hw_start_8168bef,
		[RTL_GIGA_MAC_VER_18] = rtl_hw_start_8168cp_1,
		[RTL_GIGA_MAC_VER_19] = rtl_hw_start_8168c_1,
		[RTL_GIGA_MAC_VER_20] = rtl_hw_start_8168c_2,
		[RTL_GIGA_MAC_VER_21] = rtl_hw_start_8168c_3,
		[RTL_GIGA_MAC_VER_22] = rtl_hw_start_8168c_4,
		[RTL_GIGA_MAC_VER_23] = rtl_hw_start_8168cp_2,
		[RTL_GIGA_MAC_VER_24] = rtl_hw_start_8168cp_3,
		[RTL_GIGA_MAC_VER_25] = rtl_hw_start_8168d,
		[RTL_GIGA_MAC_VER_26] = rtl_hw_start_8168d,
		[RTL_GIGA_MAC_VER_27] = rtl_hw_start_8168d,
		[RTL_GIGA_MAC_VER_28] = rtl_hw_start_8168d_4,
		[RTL_GIGA_MAC_VER_29] = rtl_hw_start_8105e_1,
		[RTL_GIGA_MAC_VER_30] = rtl_hw_start_8105e_2,
		[RTL_GIGA_MAC_VER_31] = rtl_hw_start_8168dp,
		[RTL_GIGA_MAC_VER_32] = rtl_hw_start_8168e_1,
		[RTL_GIGA_MAC_VER_33] = rtl_hw_start_8168e_1,
		[RTL_GIGA_MAC_VER_34] = rtl_hw_start_8168e_2,
		[RTL_GIGA_MAC_VER_35] = rtl_hw_start_8168f_1,
		[RTL_GIGA_MAC_VER_36] = rtl_hw_start_8168f_1,
		[RTL_GIGA_MAC_VER_37] = rtl_hw_start_8402,
		[RTL_GIGA_MAC_VER_38] = rtl_hw_start_8411,
		[RTL_GIGA_MAC_VER_39] = rtl_hw_start_8106,
		[RTL_GIGA_MAC_VER_40] = rtl_hw_start_8168g_1,
		[RTL_GIGA_MAC_VER_41] = rtl_hw_start_8168g_1,
		[RTL_GIGA_MAC_VER_42] = rtl_hw_start_8168g_2,
		[RTL_GIGA_MAC_VER_43] = rtl_hw_start_8168g_2,
		[RTL_GIGA_MAC_VER_44] = rtl_hw_start_8411_2,
		[RTL_GIGA_MAC_VER_45] = rtl_hw_start_8168h_1,
		[RTL_GIGA_MAC_VER_46] = rtl_hw_start_8168h_1,
		[RTL_GIGA_MAC_VER_47] = rtl_hw_start_8168h_1,
		[RTL_GIGA_MAC_VER_48] = rtl_hw_start_8168h_1,
		[RTL_GIGA_MAC_VER_49] = rtl_hw_start_8168ep_1,
		[RTL_GIGA_MAC_VER_50] = rtl_hw_start_8168ep_2,
		[RTL_GIGA_MAC_VER_51] = rtl_hw_start_8168ep_3,
	};

	if (hw_configs[tp->mac_version])
		hw_configs[tp->mac_version](tp);
}

static void rtl_hw_start_8168(struct rtl8169_private *tp)
{
	if (tp->mac_version == RTL_GIGA_MAC_VER_13 ||
	    tp->mac_version == RTL_GIGA_MAC_VER_16)
		pcie_capability_set_word(tp->pci_dev, PCI_EXP_DEVCTL,
					 PCI_EXP_DEVCTL_NOSNOOP_EN);

	if (rtl_is_8168evl_up(tp))
		RTL_W8(tp, MaxTxPacketSize, EarlySize);
	else
		RTL_W8(tp, MaxTxPacketSize, TxPacketMax);

	rtl_hw_config(tp);
}

static void rtl_hw_start_8169(struct rtl8169_private *tp)
{
	if (tp->mac_version == RTL_GIGA_MAC_VER_05)
		pci_write_config_byte(tp->pci_dev, PCI_CACHE_LINE_SIZE, 0x08);

	RTL_W8(tp, EarlyTxThres, NoEarlyTx);

	tp->cp_cmd |= PCIMulRW;

	if (tp->mac_version == RTL_GIGA_MAC_VER_02 ||
	    tp->mac_version == RTL_GIGA_MAC_VER_03) {
		netif_dbg(tp, drv, tp->dev,
			  "Set MAC Reg C+CR Offset 0xe0. Bit 3 and Bit 14 MUST be 1\n");
		tp->cp_cmd |= (1 << 14);
	}

	RTL_W16(tp, CPlusCmd, tp->cp_cmd);

	rtl8169_set_magic_reg(tp, tp->mac_version);

	RTL_W32(tp, RxMissed, 0);
}

static void rtl_hw_start(struct  rtl8169_private *tp)
{
	rtl_unlock_config_regs(tp);

	tp->cp_cmd &= CPCMD_MASK;
	RTL_W16(tp, CPlusCmd, tp->cp_cmd);

	if (tp->mac_version <= RTL_GIGA_MAC_VER_06)
		rtl_hw_start_8169(tp);
	else
		rtl_hw_start_8168(tp);

	rtl_set_rx_max_size(tp);
	rtl_set_rx_tx_desc_registers(tp);
	rtl_lock_config_regs(tp);

	/* disable interrupt coalescing */
	RTL_W16(tp, IntrMitigate, 0x0000);
	/* Initially a 10 us delay. Turned it into a PCI commit. - FR */
	RTL_R8(tp, IntrMask);
	RTL_W8(tp, ChipCmd, CmdTxEnb | CmdRxEnb);
	rtl_init_rxcfg(tp);
	rtl_set_tx_config_registers(tp);

	rtl_set_rx_mode(tp->dev);
	/* no early-rx interrupts */
	RTL_W16(tp, MultiIntr, RTL_R16(tp, MultiIntr) & 0xf000);
	rtl_irq_enable(tp);
}

static int rtl8169_change_mtu(struct net_device *dev, int new_mtu)
{
	struct rtl8169_private *tp = netdev_priv(dev);

	if (new_mtu > ETH_DATA_LEN)
		rtl_hw_jumbo_enable(tp);
	else
		rtl_hw_jumbo_disable(tp);

	dev->mtu = new_mtu;
	netdev_update_features(dev);

	return 0;
}

static inline void rtl8169_make_unusable_by_asic(struct RxDesc *desc)
{
	desc->addr = cpu_to_le64(0x0badbadbadbadbadull);
	desc->opts1 &= ~cpu_to_le32(DescOwn | RsvdMask);
}

static void rtl8169_free_rx_databuff(struct rtl8169_private *tp,
				     void **data_buff, struct RxDesc *desc)
{
	dma_unmap_single(tp_to_dev(tp), le64_to_cpu(desc->addr),
			 R8169_RX_BUF_SIZE, DMA_FROM_DEVICE);

	kfree(*data_buff);
	*data_buff = NULL;
	rtl8169_make_unusable_by_asic(desc);
}

static inline void rtl8169_mark_to_asic(struct RxDesc *desc)
{
	u32 eor = le32_to_cpu(desc->opts1) & RingEnd;

	/* Force memory writes to complete before releasing descriptor */
	dma_wmb();

	desc->opts1 = cpu_to_le32(DescOwn | eor | R8169_RX_BUF_SIZE);
}

static struct sk_buff *rtl8169_alloc_rx_data(struct rtl8169_private *tp,
					     struct RxDesc *desc)
{
	void *data;
	dma_addr_t mapping;
	struct device *d = tp_to_dev(tp);
	int node = dev_to_node(d);

	data = kmalloc_node(R8169_RX_BUF_SIZE, GFP_KERNEL, node);
	if (!data)
		return NULL;

	/* Memory should be properly aligned, but better check. */
	if (!IS_ALIGNED((unsigned long)data, 8)) {
		netdev_err_once(tp->dev, "RX buffer not 8-byte-aligned\n");
		goto err_out;
	}

	mapping = dma_map_single(d, data, R8169_RX_BUF_SIZE, DMA_FROM_DEVICE);
	if (unlikely(dma_mapping_error(d, mapping))) {
		if (net_ratelimit())
			netif_err(tp, drv, tp->dev, "Failed to map RX DMA!\n");
		goto err_out;
	}

	desc->addr = cpu_to_le64(mapping);
	rtl8169_mark_to_asic(desc);
	return data;

err_out:
	kfree(data);
	return NULL;
}

static void rtl8169_rx_clear(struct rtl8169_private *tp)
{
	unsigned int i;

	for (i = 0; i < NUM_RX_DESC; i++) {
		if (tp->Rx_databuff[i]) {
			rtl8169_free_rx_databuff(tp, tp->Rx_databuff + i,
					    tp->RxDescArray + i);
		}
	}
}

static inline void rtl8169_mark_as_last_descriptor(struct RxDesc *desc)
{
	desc->opts1 |= cpu_to_le32(RingEnd);
}

static int rtl8169_rx_fill(struct rtl8169_private *tp)
{
	unsigned int i;

	for (i = 0; i < NUM_RX_DESC; i++) {
		void *data;

		data = rtl8169_alloc_rx_data(tp, tp->RxDescArray + i);
		if (!data) {
			rtl8169_make_unusable_by_asic(tp->RxDescArray + i);
			goto err_out;
		}
		tp->Rx_databuff[i] = data;
	}

	rtl8169_mark_as_last_descriptor(tp->RxDescArray + NUM_RX_DESC - 1);
	return 0;

err_out:
	rtl8169_rx_clear(tp);
	return -ENOMEM;
}

static int rtl8169_init_ring(struct rtl8169_private *tp)
{
	rtl8169_init_ring_indexes(tp);

	memset(tp->tx_skb, 0, sizeof(tp->tx_skb));
	memset(tp->Rx_databuff, 0, sizeof(tp->Rx_databuff));

	return rtl8169_rx_fill(tp);
}

static void rtl8169_unmap_tx_skb(struct device *d, struct ring_info *tx_skb,
				 struct TxDesc *desc)
{
	unsigned int len = tx_skb->len;

	dma_unmap_single(d, le64_to_cpu(desc->addr), len, DMA_TO_DEVICE);

	desc->opts1 = 0x00;
	desc->opts2 = 0x00;
	desc->addr = 0x00;
	tx_skb->len = 0;
}

static void rtl8169_tx_clear_range(struct rtl8169_private *tp, u32 start,
				   unsigned int n)
{
	unsigned int i;

	for (i = 0; i < n; i++) {
		unsigned int entry = (start + i) % NUM_TX_DESC;
		struct ring_info *tx_skb = tp->tx_skb + entry;
		unsigned int len = tx_skb->len;

		if (len) {
			struct sk_buff *skb = tx_skb->skb;

			rtl8169_unmap_tx_skb(tp_to_dev(tp), tx_skb,
					     tp->TxDescArray + entry);
			if (skb) {
				dev_consume_skb_any(skb);
				tx_skb->skb = NULL;
			}
		}
	}
}

static void rtl8169_tx_clear(struct rtl8169_private *tp)
{
	rtl8169_tx_clear_range(tp, tp->dirty_tx, NUM_TX_DESC);
	tp->cur_tx = tp->dirty_tx = 0;
	netdev_reset_queue(tp->dev);
}

static void rtl_reset_work(struct rtl8169_private *tp)
{
	struct net_device *dev = tp->dev;
	int i;

	napi_disable(&tp->napi);
	netif_stop_queue(dev);
	synchronize_rcu();

	rtl8169_hw_reset(tp);

	for (i = 0; i < NUM_RX_DESC; i++)
		rtl8169_mark_to_asic(tp->RxDescArray + i);

	rtl8169_tx_clear(tp);
	rtl8169_init_ring_indexes(tp);

	napi_enable(&tp->napi);
	rtl_hw_start(tp);
	netif_wake_queue(dev);
}

static void rtl8169_tx_timeout(struct net_device *dev)
{
	struct rtl8169_private *tp = netdev_priv(dev);

	rtl_schedule_task(tp, RTL_FLAG_TASK_RESET_PENDING);
}

static __le32 rtl8169_get_txd_opts1(u32 opts0, u32 len, unsigned int entry)
{
	u32 status = opts0 | len;

	if (entry == NUM_TX_DESC - 1)
		status |= RingEnd;

	return cpu_to_le32(status);
}

static int rtl8169_xmit_frags(struct rtl8169_private *tp, struct sk_buff *skb,
			      u32 *opts)
{
	struct skb_shared_info *info = skb_shinfo(skb);
	unsigned int cur_frag, entry;
	struct TxDesc *uninitialized_var(txd);
	struct device *d = tp_to_dev(tp);

	entry = tp->cur_tx;
	for (cur_frag = 0; cur_frag < info->nr_frags; cur_frag++) {
		const skb_frag_t *frag = info->frags + cur_frag;
		dma_addr_t mapping;
		u32 len;
		void *addr;

		entry = (entry + 1) % NUM_TX_DESC;

		txd = tp->TxDescArray + entry;
		len = skb_frag_size(frag);
		addr = skb_frag_address(frag);
		mapping = dma_map_single(d, addr, len, DMA_TO_DEVICE);
		if (unlikely(dma_mapping_error(d, mapping))) {
			if (net_ratelimit())
				netif_err(tp, drv, tp->dev,
					  "Failed to map TX fragments DMA!\n");
			goto err_out;
		}

		txd->opts1 = rtl8169_get_txd_opts1(opts[0], len, entry);
		txd->opts2 = cpu_to_le32(opts[1]);
		txd->addr = cpu_to_le64(mapping);

		tp->tx_skb[entry].len = len;
	}

	if (cur_frag) {
		tp->tx_skb[entry].skb = skb;
		txd->opts1 |= cpu_to_le32(LastFrag);
	}

	return cur_frag;

err_out:
	rtl8169_tx_clear_range(tp, tp->cur_tx + 1, cur_frag);
	return -EIO;
}

static bool rtl_test_hw_pad_bug(struct rtl8169_private *tp, struct sk_buff *skb)
{
	return skb->len < ETH_ZLEN && tp->mac_version == RTL_GIGA_MAC_VER_34;
}

static netdev_tx_t rtl8169_start_xmit(struct sk_buff *skb,
				      struct net_device *dev);
/* r8169_csum_workaround()
 * The hw limites the value the transport offset. When the offset is out of the
 * range, calculate the checksum by sw.
 */
static void r8169_csum_workaround(struct rtl8169_private *tp,
				  struct sk_buff *skb)
{
	if (skb_is_gso(skb)) {
		netdev_features_t features = tp->dev->features;
		struct sk_buff *segs, *nskb;

		features &= ~(NETIF_F_SG | NETIF_F_IPV6_CSUM | NETIF_F_TSO6);
		segs = skb_gso_segment(skb, features);
		if (IS_ERR(segs) || !segs)
			goto drop;

		do {
			nskb = segs;
			segs = segs->next;
			nskb->next = NULL;
			rtl8169_start_xmit(nskb, tp->dev);
		} while (segs);

		dev_consume_skb_any(skb);
	} else if (skb->ip_summed == CHECKSUM_PARTIAL) {
		if (skb_checksum_help(skb) < 0)
			goto drop;

		rtl8169_start_xmit(skb, tp->dev);
	} else {
drop:
		tp->dev->stats.tx_dropped++;
		dev_kfree_skb_any(skb);
	}
}

/* msdn_giant_send_check()
 * According to the document of microsoft, the TCP Pseudo Header excludes the
 * packet length for IPv6 TCP large packets.
 */
static int msdn_giant_send_check(struct sk_buff *skb)
{
	const struct ipv6hdr *ipv6h;
	struct tcphdr *th;
	int ret;

	ret = skb_cow_head(skb, 0);
	if (ret)
		return ret;

	ipv6h = ipv6_hdr(skb);
	th = tcp_hdr(skb);

	th->check = 0;
	th->check = ~tcp_v6_check(0, &ipv6h->saddr, &ipv6h->daddr, 0);

	return ret;
}

static void rtl8169_tso_csum_v1(struct sk_buff *skb, u32 *opts)
{
	u32 mss = skb_shinfo(skb)->gso_size;

	if (mss) {
		opts[0] |= TD_LSO;
		opts[0] |= min(mss, TD_MSS_MAX) << TD0_MSS_SHIFT;
	} else if (skb->ip_summed == CHECKSUM_PARTIAL) {
		const struct iphdr *ip = ip_hdr(skb);

		if (ip->protocol == IPPROTO_TCP)
			opts[0] |= TD0_IP_CS | TD0_TCP_CS;
		else if (ip->protocol == IPPROTO_UDP)
			opts[0] |= TD0_IP_CS | TD0_UDP_CS;
		else
			WARN_ON_ONCE(1);
	}
}

static bool rtl8169_tso_csum_v2(struct rtl8169_private *tp,
				struct sk_buff *skb, u32 *opts)
{
	u32 transport_offset = (u32)skb_transport_offset(skb);
	u32 mss = skb_shinfo(skb)->gso_size;

	if (mss) {
		if (transport_offset > GTTCPHO_MAX) {
			netif_warn(tp, tx_err, tp->dev,
				   "Invalid transport offset 0x%x for TSO\n",
				   transport_offset);
			return false;
		}

		switch (vlan_get_protocol(skb)) {
		case htons(ETH_P_IP):
			opts[0] |= TD1_GTSENV4;
			break;

		case htons(ETH_P_IPV6):
			if (msdn_giant_send_check(skb))
				return false;

			opts[0] |= TD1_GTSENV6;
			break;

		default:
			WARN_ON_ONCE(1);
			break;
		}

		opts[0] |= transport_offset << GTTCPHO_SHIFT;
		opts[1] |= min(mss, TD_MSS_MAX) << TD1_MSS_SHIFT;
	} else if (skb->ip_summed == CHECKSUM_PARTIAL) {
		u8 ip_protocol;

		if (unlikely(rtl_test_hw_pad_bug(tp, skb)))
			return !(skb_checksum_help(skb) || eth_skb_pad(skb));

		if (transport_offset > TCPHO_MAX) {
			netif_warn(tp, tx_err, tp->dev,
				   "Invalid transport offset 0x%x\n",
				   transport_offset);
			return false;
		}

		switch (vlan_get_protocol(skb)) {
		case htons(ETH_P_IP):
			opts[1] |= TD1_IPv4_CS;
			ip_protocol = ip_hdr(skb)->protocol;
			break;

		case htons(ETH_P_IPV6):
			opts[1] |= TD1_IPv6_CS;
			ip_protocol = ipv6_hdr(skb)->nexthdr;
			break;

		default:
			ip_protocol = IPPROTO_RAW;
			break;
		}

		if (ip_protocol == IPPROTO_TCP)
			opts[1] |= TD1_TCP_CS;
		else if (ip_protocol == IPPROTO_UDP)
			opts[1] |= TD1_UDP_CS;
		else
			WARN_ON_ONCE(1);

		opts[1] |= transport_offset << TCPHO_SHIFT;
	} else {
		if (unlikely(rtl_test_hw_pad_bug(tp, skb)))
			return !eth_skb_pad(skb);
	}

	return true;
}

static bool rtl_tx_slots_avail(struct rtl8169_private *tp,
			       unsigned int nr_frags)
{
	unsigned int slots_avail = tp->dirty_tx + NUM_TX_DESC - tp->cur_tx;

	/* A skbuff with nr_frags needs nr_frags+1 entries in the tx queue */
	return slots_avail > nr_frags;
}

/* Versions RTL8102e and from RTL8168c onwards support csum_v2 */
static bool rtl_chip_supports_csum_v2(struct rtl8169_private *tp)
{
	switch (tp->mac_version) {
	case RTL_GIGA_MAC_VER_02 ... RTL_GIGA_MAC_VER_06:
	case RTL_GIGA_MAC_VER_10 ... RTL_GIGA_MAC_VER_17:
		return false;
	default:
		return true;
	}
}

static netdev_tx_t rtl8169_start_xmit(struct sk_buff *skb,
				      struct net_device *dev)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	unsigned int entry = tp->cur_tx % NUM_TX_DESC;
	struct TxDesc *txd = tp->TxDescArray + entry;
	struct device *d = tp_to_dev(tp);
	dma_addr_t mapping;
	u32 opts[2], len;
	int frags;

	if (unlikely(!rtl_tx_slots_avail(tp, skb_shinfo(skb)->nr_frags))) {
		netif_err(tp, drv, dev, "BUG! Tx Ring full when queue awake!\n");
		goto err_stop_0;
	}

	if (unlikely(le32_to_cpu(txd->opts1) & DescOwn))
		goto err_stop_0;

	opts[1] = rtl8169_tx_vlan_tag(skb);
	opts[0] = DescOwn;

	if (rtl_chip_supports_csum_v2(tp)) {
		if (!rtl8169_tso_csum_v2(tp, skb, opts)) {
			r8169_csum_workaround(tp, skb);
			return NETDEV_TX_OK;
		}
	} else {
		rtl8169_tso_csum_v1(skb, opts);
	}

	len = skb_headlen(skb);
	mapping = dma_map_single(d, skb->data, len, DMA_TO_DEVICE);
	if (unlikely(dma_mapping_error(d, mapping))) {
		if (net_ratelimit())
			netif_err(tp, drv, dev, "Failed to map TX DMA!\n");
		goto err_dma_0;
	}

	tp->tx_skb[entry].len = len;
	txd->addr = cpu_to_le64(mapping);

	frags = rtl8169_xmit_frags(tp, skb, opts);
	if (frags < 0)
		goto err_dma_1;
	else if (frags)
		opts[0] |= FirstFrag;
	else {
		opts[0] |= FirstFrag | LastFrag;
		tp->tx_skb[entry].skb = skb;
	}

	txd->opts2 = cpu_to_le32(opts[1]);

	netdev_sent_queue(dev, skb->len);

	skb_tx_timestamp(skb);

	/* Force memory writes to complete before releasing descriptor */
	dma_wmb();

	txd->opts1 = rtl8169_get_txd_opts1(opts[0], len, entry);

	/* Force all memory writes to complete before notifying device */
	wmb();

	tp->cur_tx += frags + 1;

	RTL_W8(tp, TxPoll, NPQ);

	if (!rtl_tx_slots_avail(tp, MAX_SKB_FRAGS)) {
		/* Avoid wrongly optimistic queue wake-up: rtl_tx thread must
		 * not miss a ring update when it notices a stopped queue.
		 */
		smp_wmb();
		netif_stop_queue(dev);
		/* Sync with rtl_tx:
		 * - publish queue status and cur_tx ring index (write barrier)
		 * - refresh dirty_tx ring index (read barrier).
		 * May the current thread have a pessimistic view of the ring
		 * status and forget to wake up queue, a racing rtl_tx thread
		 * can't.
		 */
		smp_mb();
		if (rtl_tx_slots_avail(tp, MAX_SKB_FRAGS))
			netif_start_queue(dev);
	}

	return NETDEV_TX_OK;

err_dma_1:
	rtl8169_unmap_tx_skb(d, tp->tx_skb + entry, txd);
err_dma_0:
	dev_kfree_skb_any(skb);
	dev->stats.tx_dropped++;
	return NETDEV_TX_OK;

err_stop_0:
	netif_stop_queue(dev);
	dev->stats.tx_dropped++;
	return NETDEV_TX_BUSY;
}

static void rtl8169_pcierr_interrupt(struct net_device *dev)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	struct pci_dev *pdev = tp->pci_dev;
	u16 pci_status, pci_cmd;

	pci_read_config_word(pdev, PCI_COMMAND, &pci_cmd);
	pci_read_config_word(pdev, PCI_STATUS, &pci_status);

	netif_err(tp, intr, dev, "PCI error (cmd = 0x%04x, status = 0x%04x)\n",
		  pci_cmd, pci_status);

	/*
	 * The recovery sequence below admits a very elaborated explanation:
	 * - it seems to work;
	 * - I did not see what else could be done;
	 * - it makes iop3xx happy.
	 *
	 * Feel free to adjust to your needs.
	 */
	if (pdev->broken_parity_status)
		pci_cmd &= ~PCI_COMMAND_PARITY;
	else
		pci_cmd |= PCI_COMMAND_SERR | PCI_COMMAND_PARITY;

	pci_write_config_word(pdev, PCI_COMMAND, pci_cmd);

	pci_write_config_word(pdev, PCI_STATUS,
		pci_status & (PCI_STATUS_DETECTED_PARITY |
		PCI_STATUS_SIG_SYSTEM_ERROR | PCI_STATUS_REC_MASTER_ABORT |
		PCI_STATUS_REC_TARGET_ABORT | PCI_STATUS_SIG_TARGET_ABORT));

	rtl_schedule_task(tp, RTL_FLAG_TASK_RESET_PENDING);
}

static void rtl_tx(struct net_device *dev, struct rtl8169_private *tp,
		   int budget)
{
	unsigned int dirty_tx, tx_left, bytes_compl = 0, pkts_compl = 0;

	dirty_tx = tp->dirty_tx;
	smp_rmb();
	tx_left = tp->cur_tx - dirty_tx;

	while (tx_left > 0) {
		unsigned int entry = dirty_tx % NUM_TX_DESC;
		struct ring_info *tx_skb = tp->tx_skb + entry;
		u32 status;

		status = le32_to_cpu(tp->TxDescArray[entry].opts1);
		if (status & DescOwn)
			break;

		/* This barrier is needed to keep us from reading
		 * any other fields out of the Tx descriptor until
		 * we know the status of DescOwn
		 */
		dma_rmb();

		rtl8169_unmap_tx_skb(tp_to_dev(tp), tx_skb,
				     tp->TxDescArray + entry);
		if (status & LastFrag) {
			pkts_compl++;
			bytes_compl += tx_skb->skb->len;
			napi_consume_skb(tx_skb->skb, budget);
			tx_skb->skb = NULL;
		}
		dirty_tx++;
		tx_left--;
	}

	if (tp->dirty_tx != dirty_tx) {
		netdev_completed_queue(dev, pkts_compl, bytes_compl);

		u64_stats_update_begin(&tp->tx_stats.syncp);
		tp->tx_stats.packets += pkts_compl;
		tp->tx_stats.bytes += bytes_compl;
		u64_stats_update_end(&tp->tx_stats.syncp);

		tp->dirty_tx = dirty_tx;
		/* Sync with rtl8169_start_xmit:
		 * - publish dirty_tx ring index (write barrier)
		 * - refresh cur_tx ring index and queue status (read barrier)
		 * May the current thread miss the stopped queue condition,
		 * a racing xmit thread can only have a right view of the
		 * ring status.
		 */
		smp_mb();
		if (netif_queue_stopped(dev) &&
		    rtl_tx_slots_avail(tp, MAX_SKB_FRAGS)) {
			netif_wake_queue(dev);
		}
		/*
		 * 8168 hack: TxPoll requests are lost when the Tx packets are
		 * too close. Let's kick an extra TxPoll request when a burst
		 * of start_xmit activity is detected (if it is not detected,
		 * it is slow enough). -- FR
		 */
		if (tp->cur_tx != dirty_tx)
			RTL_W8(tp, TxPoll, NPQ);
	}
}

static inline int rtl8169_fragmented_frame(u32 status)
{
	return (status & (FirstFrag | LastFrag)) != (FirstFrag | LastFrag);
}

static inline void rtl8169_rx_csum(struct sk_buff *skb, u32 opts1)
{
	u32 status = opts1 & RxProtoMask;

	if (((status == RxProtoTCP) && !(opts1 & TCPFail)) ||
	    ((status == RxProtoUDP) && !(opts1 & UDPFail)))
		skb->ip_summed = CHECKSUM_UNNECESSARY;
	else
		skb_checksum_none_assert(skb);
}

static struct sk_buff *rtl8169_try_rx_copy(void *data,
					   struct rtl8169_private *tp,
					   int pkt_size,
					   dma_addr_t addr)
{
	struct sk_buff *skb;
	struct device *d = tp_to_dev(tp);

	dma_sync_single_for_cpu(d, addr, pkt_size, DMA_FROM_DEVICE);
	prefetch(data);
	skb = napi_alloc_skb(&tp->napi, pkt_size);
	if (skb)
		skb_copy_to_linear_data(skb, data, pkt_size);

	return skb;
}

static int rtl_rx(struct net_device *dev, struct rtl8169_private *tp, u32 budget)
{
	unsigned int cur_rx, rx_left;
	unsigned int count;

	cur_rx = tp->cur_rx;

	for (rx_left = min(budget, NUM_RX_DESC); rx_left > 0; rx_left--, cur_rx++) {
		unsigned int entry = cur_rx % NUM_RX_DESC;
		struct RxDesc *desc = tp->RxDescArray + entry;
		u32 status;

		status = le32_to_cpu(desc->opts1);
		if (status & DescOwn)
			break;

		/* This barrier is needed to keep us from reading
		 * any other fields out of the Rx descriptor until
		 * we know the status of DescOwn
		 */
		dma_rmb();

		if (unlikely(status & RxRES)) {
			netif_info(tp, rx_err, dev, "Rx ERROR. status = %08x\n",
				   status);
			dev->stats.rx_errors++;
			if (status & (RxRWT | RxRUNT))
				dev->stats.rx_length_errors++;
			if (status & RxCRC)
				dev->stats.rx_crc_errors++;
			if (status & (RxRUNT | RxCRC) && !(status & RxRWT) &&
			    dev->features & NETIF_F_RXALL) {
				goto process_pkt;
			}
		} else {
			struct sk_buff *skb;
			dma_addr_t addr;
			int pkt_size;

process_pkt:
			addr = le64_to_cpu(desc->addr);
			if (likely(!(dev->features & NETIF_F_RXFCS)))
				pkt_size = (status & 0x00003fff) - 4;
			else
				pkt_size = status & 0x00003fff;

			/*
			 * The driver does not support incoming fragmented
			 * frames. They are seen as a symptom of over-mtu
			 * sized frames.
			 */
			if (unlikely(rtl8169_fragmented_frame(status))) {
				dev->stats.rx_dropped++;
				dev->stats.rx_length_errors++;
				goto release_descriptor;
			}

			skb = rtl8169_try_rx_copy(tp->Rx_databuff[entry],
						  tp, pkt_size, addr);
			if (!skb) {
				dev->stats.rx_dropped++;
				goto release_descriptor;
			}

			rtl8169_rx_csum(skb, status);
			skb_put(skb, pkt_size);
			skb->protocol = eth_type_trans(skb, dev);

			rtl8169_rx_vlan_tag(desc, skb);

			if (skb->pkt_type == PACKET_MULTICAST)
				dev->stats.multicast++;

			napi_gro_receive(&tp->napi, skb);

			u64_stats_update_begin(&tp->rx_stats.syncp);
			tp->rx_stats.packets++;
			tp->rx_stats.bytes += pkt_size;
			u64_stats_update_end(&tp->rx_stats.syncp);
		}
release_descriptor:
		desc->opts2 = 0;
		rtl8169_mark_to_asic(desc);
	}

	count = cur_rx - tp->cur_rx;
	tp->cur_rx = cur_rx;

	return count;
}

static irqreturn_t rtl8169_interrupt(int irq, void *dev_instance)
{
	struct rtl8169_private *tp = dev_instance;
	u16 status = RTL_R16(tp, IntrStatus);

	if (!tp->irq_enabled || status == 0xffff || !(status & tp->irq_mask))
		return IRQ_NONE;

	if (unlikely(status & SYSErr)) {
		rtl8169_pcierr_interrupt(tp->dev);
		goto out;
	}

	if (status & LinkChg)
		phy_mac_interrupt(tp->phydev);

	if (unlikely(status & RxFIFOOver &&
	    tp->mac_version == RTL_GIGA_MAC_VER_11)) {
		netif_stop_queue(tp->dev);
		/* XXX - Hack alert. See rtl_task(). */
		set_bit(RTL_FLAG_TASK_RESET_PENDING, tp->wk.flags);
	}

	rtl_irq_disable(tp);
	napi_schedule_irqoff(&tp->napi);
out:
	rtl_ack_events(tp, status);

	return IRQ_HANDLED;
}

static void rtl_task(struct work_struct *work)
{
	static const struct {
		int bitnr;
		void (*action)(struct rtl8169_private *);
	} rtl_work[] = {
		{ RTL_FLAG_TASK_RESET_PENDING,	rtl_reset_work },
	};
	struct rtl8169_private *tp =
		container_of(work, struct rtl8169_private, wk.work);
	struct net_device *dev = tp->dev;
	int i;

	rtl_lock_work(tp);

	if (!netif_running(dev) ||
	    !test_bit(RTL_FLAG_TASK_ENABLED, tp->wk.flags))
		goto out_unlock;

	for (i = 0; i < ARRAY_SIZE(rtl_work); i++) {
		bool pending;

		pending = test_and_clear_bit(rtl_work[i].bitnr, tp->wk.flags);
		if (pending)
			rtl_work[i].action(tp);
	}

out_unlock:
	rtl_unlock_work(tp);
}

static int rtl8169_poll(struct napi_struct *napi, int budget)
{
	struct rtl8169_private *tp = container_of(napi, struct rtl8169_private, napi);
	struct net_device *dev = tp->dev;
	int work_done;

	work_done = rtl_rx(dev, tp, (u32) budget);

	rtl_tx(dev, tp, budget);

	if (work_done < budget) {
		napi_complete_done(napi, work_done);
		rtl_irq_enable(tp);
	}

	return work_done;
}

static void rtl8169_rx_missed(struct net_device *dev)
{
	struct rtl8169_private *tp = netdev_priv(dev);

	if (tp->mac_version > RTL_GIGA_MAC_VER_06)
		return;

	dev->stats.rx_missed_errors += RTL_R32(tp, RxMissed) & 0xffffff;
	RTL_W32(tp, RxMissed, 0);
}

static void r8169_phylink_handler(struct net_device *ndev)
{
	struct rtl8169_private *tp = netdev_priv(ndev);

	if (netif_carrier_ok(ndev)) {
		rtl_link_chg_patch(tp);
		pm_request_resume(&tp->pci_dev->dev);
	} else {
		pm_runtime_idle(&tp->pci_dev->dev);
	}

	if (net_ratelimit())
		phy_print_status(tp->phydev);
}

static int r8169_phy_connect(struct rtl8169_private *tp)
{
	struct phy_device *phydev = tp->phydev;
	phy_interface_t phy_mode;
	int ret;

	phy_mode = tp->supports_gmii ? PHY_INTERFACE_MODE_GMII :
		   PHY_INTERFACE_MODE_MII;

	ret = phy_connect_direct(tp->dev, phydev, r8169_phylink_handler,
				 phy_mode);
	if (ret)
		return ret;

	if (tp->supports_gmii)
		phy_remove_link_mode(phydev,
				     ETHTOOL_LINK_MODE_1000baseT_Half_BIT);
	else
		phy_set_max_speed(phydev, SPEED_100);

	phy_support_asym_pause(phydev);

	phy_attached_info(phydev);

	return 0;
}

static void rtl8169_down(struct net_device *dev)
{
	struct rtl8169_private *tp = netdev_priv(dev);

	phy_stop(tp->phydev);

	napi_disable(&tp->napi);
	netif_stop_queue(dev);

	rtl8169_hw_reset(tp);
	/*
	 * At this point device interrupts can not be enabled in any function,
	 * as netif_running is not true (rtl8169_interrupt, rtl8169_reset_task)
	 * and napi is disabled (rtl8169_poll).
	 */
	rtl8169_rx_missed(dev);

	/* Give a racing hard_start_xmit a few cycles to complete. */
	synchronize_rcu();

	rtl8169_tx_clear(tp);

	rtl8169_rx_clear(tp);

	rtl_pll_power_down(tp);
}

static int rtl8169_close(struct net_device *dev)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	struct pci_dev *pdev = tp->pci_dev;

	pm_runtime_get_sync(&pdev->dev);

	/* Update counters before going down */
	rtl8169_update_counters(tp);

	rtl_lock_work(tp);
	/* Clear all task flags */
	bitmap_zero(tp->wk.flags, RTL_FLAG_MAX);

	rtl8169_down(dev);
	rtl_unlock_work(tp);

	cancel_work_sync(&tp->wk.work);

	phy_disconnect(tp->phydev);

	pci_free_irq(pdev, 0, tp);

	dma_free_coherent(&pdev->dev, R8169_RX_RING_BYTES, tp->RxDescArray,
			  tp->RxPhyAddr);
	dma_free_coherent(&pdev->dev, R8169_TX_RING_BYTES, tp->TxDescArray,
			  tp->TxPhyAddr);
	tp->TxDescArray = NULL;
	tp->RxDescArray = NULL;

	pm_runtime_put_sync(&pdev->dev);

	return 0;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void rtl8169_netpoll(struct net_device *dev)
{
	struct rtl8169_private *tp = netdev_priv(dev);

	rtl8169_interrupt(pci_irq_vector(tp->pci_dev, 0), tp);
}
#endif

static int rtl_open(struct net_device *dev)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	struct pci_dev *pdev = tp->pci_dev;
	int retval = -ENOMEM;

	pm_runtime_get_sync(&pdev->dev);

	/*
	 * Rx and Tx descriptors needs 256 bytes alignment.
	 * dma_alloc_coherent provides more.
	 */
	tp->TxDescArray = dma_alloc_coherent(&pdev->dev, R8169_TX_RING_BYTES,
					     &tp->TxPhyAddr, GFP_KERNEL);
	if (!tp->TxDescArray)
		goto err_pm_runtime_put;

	tp->RxDescArray = dma_alloc_coherent(&pdev->dev, R8169_RX_RING_BYTES,
					     &tp->RxPhyAddr, GFP_KERNEL);
	if (!tp->RxDescArray)
		goto err_free_tx_0;

	retval = rtl8169_init_ring(tp);
	if (retval < 0)
		goto err_free_rx_1;

	rtl_request_firmware(tp);

	retval = pci_request_irq(pdev, 0, rtl8169_interrupt, NULL, tp,
				 dev->name);
	if (retval < 0)
		goto err_release_fw_2;

	retval = r8169_phy_connect(tp);
	if (retval)
		goto err_free_irq;

	rtl_lock_work(tp);

	set_bit(RTL_FLAG_TASK_ENABLED, tp->wk.flags);

	napi_enable(&tp->napi);

	rtl8169_init_phy(dev, tp);

	rtl_pll_power_up(tp);

	rtl_hw_start(tp);

	if (!rtl8169_init_counter_offsets(tp))
		netif_warn(tp, hw, dev, "counter reset/update failed\n");

	phy_start(tp->phydev);
	netif_start_queue(dev);

	rtl_unlock_work(tp);

	pm_runtime_put_sync(&pdev->dev);
out:
	return retval;

err_free_irq:
	pci_free_irq(pdev, 0, tp);
err_release_fw_2:
	rtl_release_firmware(tp);
	rtl8169_rx_clear(tp);
err_free_rx_1:
	dma_free_coherent(&pdev->dev, R8169_RX_RING_BYTES, tp->RxDescArray,
			  tp->RxPhyAddr);
	tp->RxDescArray = NULL;
err_free_tx_0:
	dma_free_coherent(&pdev->dev, R8169_TX_RING_BYTES, tp->TxDescArray,
			  tp->TxPhyAddr);
	tp->TxDescArray = NULL;
err_pm_runtime_put:
	pm_runtime_put_noidle(&pdev->dev);
	goto out;
}

static void
rtl8169_get_stats64(struct net_device *dev, struct rtnl_link_stats64 *stats)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	struct pci_dev *pdev = tp->pci_dev;
	struct rtl8169_counters *counters = tp->counters;
	unsigned int start;

	pm_runtime_get_noresume(&pdev->dev);

	if (netif_running(dev) && pm_runtime_active(&pdev->dev))
		rtl8169_rx_missed(dev);

	do {
		start = u64_stats_fetch_begin_irq(&tp->rx_stats.syncp);
		stats->rx_packets = tp->rx_stats.packets;
		stats->rx_bytes	= tp->rx_stats.bytes;
	} while (u64_stats_fetch_retry_irq(&tp->rx_stats.syncp, start));

	do {
		start = u64_stats_fetch_begin_irq(&tp->tx_stats.syncp);
		stats->tx_packets = tp->tx_stats.packets;
		stats->tx_bytes	= tp->tx_stats.bytes;
	} while (u64_stats_fetch_retry_irq(&tp->tx_stats.syncp, start));

	stats->rx_dropped	= dev->stats.rx_dropped;
	stats->tx_dropped	= dev->stats.tx_dropped;
	stats->rx_length_errors = dev->stats.rx_length_errors;
	stats->rx_errors	= dev->stats.rx_errors;
	stats->rx_crc_errors	= dev->stats.rx_crc_errors;
	stats->rx_fifo_errors	= dev->stats.rx_fifo_errors;
	stats->rx_missed_errors = dev->stats.rx_missed_errors;
	stats->multicast	= dev->stats.multicast;

	/*
	 * Fetch additonal counter values missing in stats collected by driver
	 * from tally counters.
	 */
	if (pm_runtime_active(&pdev->dev))
		rtl8169_update_counters(tp);

	/*
	 * Subtract values fetched during initalization.
	 * See rtl8169_init_counter_offsets for a description why we do that.
	 */
	stats->tx_errors = le64_to_cpu(counters->tx_errors) -
		le64_to_cpu(tp->tc_offset.tx_errors);
	stats->collisions = le32_to_cpu(counters->tx_multi_collision) -
		le32_to_cpu(tp->tc_offset.tx_multi_collision);
	stats->tx_aborted_errors = le16_to_cpu(counters->tx_aborted) -
		le16_to_cpu(tp->tc_offset.tx_aborted);

	pm_runtime_put_noidle(&pdev->dev);
}

static void rtl8169_net_suspend(struct net_device *dev)
{
	struct rtl8169_private *tp = netdev_priv(dev);

	if (!netif_running(dev))
		return;

	phy_stop(tp->phydev);
	netif_device_detach(dev);

	rtl_lock_work(tp);
	napi_disable(&tp->napi);
	/* Clear all task flags */
	bitmap_zero(tp->wk.flags, RTL_FLAG_MAX);

	rtl_unlock_work(tp);

	rtl_pll_power_down(tp);
}

#ifdef CONFIG_PM

static int rtl8169_suspend(struct device *device)
{
	struct net_device *dev = dev_get_drvdata(device);
	struct rtl8169_private *tp = netdev_priv(dev);

	rtl8169_net_suspend(dev);
	clk_disable_unprepare(tp->clk);

	return 0;
}

static void __rtl8169_resume(struct net_device *dev)
{
	struct rtl8169_private *tp = netdev_priv(dev);

	netif_device_attach(dev);

	rtl_pll_power_up(tp);
	rtl8169_init_phy(dev, tp);

	phy_start(tp->phydev);

	rtl_lock_work(tp);
	napi_enable(&tp->napi);
	set_bit(RTL_FLAG_TASK_ENABLED, tp->wk.flags);
	rtl_reset_work(tp);
	rtl_unlock_work(tp);
}

static int rtl8169_resume(struct device *device)
{
	struct net_device *dev = dev_get_drvdata(device);
	struct rtl8169_private *tp = netdev_priv(dev);

	rtl_rar_set(tp, dev->dev_addr);

	clk_prepare_enable(tp->clk);

	if (netif_running(dev))
		__rtl8169_resume(dev);

	return 0;
}

static int rtl8169_runtime_suspend(struct device *device)
{
	struct net_device *dev = dev_get_drvdata(device);
	struct rtl8169_private *tp = netdev_priv(dev);

	if (!tp->TxDescArray)
		return 0;

	rtl_lock_work(tp);
	__rtl8169_set_wol(tp, WAKE_ANY);
	rtl_unlock_work(tp);

	rtl8169_net_suspend(dev);

	/* Update counters before going runtime suspend */
	rtl8169_rx_missed(dev);
	rtl8169_update_counters(tp);

	return 0;
}

static int rtl8169_runtime_resume(struct device *device)
{
	struct net_device *dev = dev_get_drvdata(device);
	struct rtl8169_private *tp = netdev_priv(dev);

	rtl_rar_set(tp, dev->dev_addr);

	if (!tp->TxDescArray)
		return 0;

	rtl_lock_work(tp);
	__rtl8169_set_wol(tp, tp->saved_wolopts);
	rtl_unlock_work(tp);

	__rtl8169_resume(dev);

	return 0;
}

static int rtl8169_runtime_idle(struct device *device)
{
	struct net_device *dev = dev_get_drvdata(device);

	if (!netif_running(dev) || !netif_carrier_ok(dev))
		pm_schedule_suspend(device, 10000);

	return -EBUSY;
}

static const struct dev_pm_ops rtl8169_pm_ops = {
	.suspend		= rtl8169_suspend,
	.resume			= rtl8169_resume,
	.freeze			= rtl8169_suspend,
	.thaw			= rtl8169_resume,
	.poweroff		= rtl8169_suspend,
	.restore		= rtl8169_resume,
	.runtime_suspend	= rtl8169_runtime_suspend,
	.runtime_resume		= rtl8169_runtime_resume,
	.runtime_idle		= rtl8169_runtime_idle,
};

#define RTL8169_PM_OPS	(&rtl8169_pm_ops)

#else /* !CONFIG_PM */

#define RTL8169_PM_OPS	NULL

#endif /* !CONFIG_PM */

static void rtl_wol_shutdown_quirk(struct rtl8169_private *tp)
{
	/* WoL fails with 8168b when the receiver is disabled. */
	switch (tp->mac_version) {
	case RTL_GIGA_MAC_VER_11:
	case RTL_GIGA_MAC_VER_12:
	case RTL_GIGA_MAC_VER_17:
		pci_clear_master(tp->pci_dev);

		RTL_W8(tp, ChipCmd, CmdRxEnb);
		/* PCI commit */
		RTL_R8(tp, ChipCmd);
		break;
	default:
		break;
	}
}

static void rtl_shutdown(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct rtl8169_private *tp = netdev_priv(dev);

	rtl8169_net_suspend(dev);

	/* Restore original MAC address */
	rtl_rar_set(tp, dev->perm_addr);

	rtl8169_hw_reset(tp);

	if (system_state == SYSTEM_POWER_OFF) {
		if (tp->saved_wolopts) {
			rtl_wol_suspend_quirk(tp);
			rtl_wol_shutdown_quirk(tp);
		}

		pci_wake_from_d3(pdev, true);
		pci_set_power_state(pdev, PCI_D3hot);
	}
}

static void rtl_remove_one(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct rtl8169_private *tp = netdev_priv(dev);

	if (r8168_check_dash(tp))
		rtl8168_driver_stop(tp);

	netif_napi_del(&tp->napi);

	unregister_netdev(dev);
	mdiobus_unregister(tp->phydev->mdio.bus);

	rtl_release_firmware(tp);

	if (pci_dev_run_wake(pdev))
		pm_runtime_get_noresume(&pdev->dev);

	/* restore original MAC address */
	rtl_rar_set(tp, dev->perm_addr);
}

static const struct net_device_ops rtl_netdev_ops = {
	.ndo_open		= rtl_open,
	.ndo_stop		= rtl8169_close,
	.ndo_get_stats64	= rtl8169_get_stats64,
	.ndo_start_xmit		= rtl8169_start_xmit,
	.ndo_tx_timeout		= rtl8169_tx_timeout,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_change_mtu		= rtl8169_change_mtu,
	.ndo_fix_features	= rtl8169_fix_features,
	.ndo_set_features	= rtl8169_set_features,
	.ndo_set_mac_address	= rtl_set_mac_address,
	.ndo_do_ioctl		= rtl8169_ioctl,
	.ndo_set_rx_mode	= rtl_set_rx_mode,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= rtl8169_netpoll,
#endif

};

static void rtl_set_irq_mask(struct rtl8169_private *tp)
{
	tp->irq_mask = RTL_EVENT_NAPI | LinkChg;

	if (tp->mac_version <= RTL_GIGA_MAC_VER_06)
		tp->irq_mask |= SYSErr | RxOverflow | RxFIFOOver;
	else if (tp->mac_version == RTL_GIGA_MAC_VER_11)
		/* special workaround needed */
		tp->irq_mask |= RxFIFOOver;
	else
		tp->irq_mask |= RxOverflow;
}

static int rtl_alloc_irq(struct rtl8169_private *tp)
{
	unsigned int flags;

	if (tp->mac_version <= RTL_GIGA_MAC_VER_06) {
		rtl_unlock_config_regs(tp);
		RTL_W8(tp, Config2, RTL_R8(tp, Config2) & ~MSIEnable);
		rtl_lock_config_regs(tp);
		flags = PCI_IRQ_LEGACY;
	} else {
		flags = PCI_IRQ_ALL_TYPES;
	}

	return pci_alloc_irq_vectors(tp->pci_dev, 1, 1, flags);
}

static void rtl_read_mac_address(struct rtl8169_private *tp,
				 u8 mac_addr[ETH_ALEN])
{
	/* Get MAC address */
	if (rtl_is_8168evl_up(tp) && tp->mac_version != RTL_GIGA_MAC_VER_34) {
		u32 value = rtl_eri_read(tp, 0xe0);

		mac_addr[0] = (value >>  0) & 0xff;
		mac_addr[1] = (value >>  8) & 0xff;
		mac_addr[2] = (value >> 16) & 0xff;
		mac_addr[3] = (value >> 24) & 0xff;

		value = rtl_eri_read(tp, 0xe4);
		mac_addr[4] = (value >>  0) & 0xff;
		mac_addr[5] = (value >>  8) & 0xff;
	}
}

DECLARE_RTL_COND(rtl_link_list_ready_cond)
{
	return RTL_R8(tp, MCU) & LINK_LIST_RDY;
}

DECLARE_RTL_COND(rtl_rxtx_empty_cond)
{
	return (RTL_R8(tp, MCU) & RXTX_EMPTY) == RXTX_EMPTY;
}

static int r8169_mdio_read_reg(struct mii_bus *mii_bus, int phyaddr, int phyreg)
{
	struct rtl8169_private *tp = mii_bus->priv;

	if (phyaddr > 0)
		return -ENODEV;

	return rtl_readphy(tp, phyreg);
}

static int r8169_mdio_write_reg(struct mii_bus *mii_bus, int phyaddr,
				int phyreg, u16 val)
{
	struct rtl8169_private *tp = mii_bus->priv;

	if (phyaddr > 0)
		return -ENODEV;

	rtl_writephy(tp, phyreg, val);

	return 0;
}

static int r8169_mdio_register(struct rtl8169_private *tp)
{
	struct pci_dev *pdev = tp->pci_dev;
	struct mii_bus *new_bus;
	int ret;

	new_bus = devm_mdiobus_alloc(&pdev->dev);
	if (!new_bus)
		return -ENOMEM;

	new_bus->name = "r8169";
	new_bus->priv = tp;
	new_bus->parent = &pdev->dev;
	new_bus->irq[0] = PHY_IGNORE_INTERRUPT;
	snprintf(new_bus->id, MII_BUS_ID_SIZE, "r8169-%x", pci_dev_id(pdev));

	new_bus->read = r8169_mdio_read_reg;
	new_bus->write = r8169_mdio_write_reg;

	ret = mdiobus_register(new_bus);
	if (ret)
		return ret;

	tp->phydev = mdiobus_get_phy(new_bus, 0);
	if (!tp->phydev) {
		mdiobus_unregister(new_bus);
		return -ENODEV;
	}

	/* PHY will be woken up in rtl_open() */
	phy_suspend(tp->phydev);

	return 0;
}

static void rtl_hw_init_8168g(struct rtl8169_private *tp)
{
	u32 data;

	tp->ocp_base = OCP_STD_PHY_BASE;

	RTL_W32(tp, MISC, RTL_R32(tp, MISC) | RXDV_GATED_EN);

	if (!rtl_udelay_loop_wait_high(tp, &rtl_txcfg_empty_cond, 100, 42))
		return;

	if (!rtl_udelay_loop_wait_high(tp, &rtl_rxtx_empty_cond, 100, 42))
		return;

	RTL_W8(tp, ChipCmd, RTL_R8(tp, ChipCmd) & ~(CmdTxEnb | CmdRxEnb));
	msleep(1);
	RTL_W8(tp, MCU, RTL_R8(tp, MCU) & ~NOW_IS_OOB);

	data = r8168_mac_ocp_read(tp, 0xe8de);
	data &= ~(1 << 14);
	r8168_mac_ocp_write(tp, 0xe8de, data);

	if (!rtl_udelay_loop_wait_high(tp, &rtl_link_list_ready_cond, 100, 42))
		return;

	data = r8168_mac_ocp_read(tp, 0xe8de);
	data |= (1 << 15);
	r8168_mac_ocp_write(tp, 0xe8de, data);

	rtl_udelay_loop_wait_high(tp, &rtl_link_list_ready_cond, 100, 42);
}

static void rtl_hw_initialize(struct rtl8169_private *tp)
{
	switch (tp->mac_version) {
	case RTL_GIGA_MAC_VER_49 ... RTL_GIGA_MAC_VER_51:
		rtl8168ep_stop_cmac(tp);
		/* fall through */
	case RTL_GIGA_MAC_VER_40 ... RTL_GIGA_MAC_VER_48:
		rtl_hw_init_8168g(tp);
		break;
	default:
		break;
	}
}

static int rtl_jumbo_max(struct rtl8169_private *tp)
{
	/* Non-GBit versions don't support jumbo frames */
	if (!tp->supports_gmii)
		return JUMBO_1K;

	switch (tp->mac_version) {
	/* RTL8169 */
	case RTL_GIGA_MAC_VER_02 ... RTL_GIGA_MAC_VER_06:
		return JUMBO_7K;
	/* RTL8168b */
	case RTL_GIGA_MAC_VER_11:
	case RTL_GIGA_MAC_VER_12:
	case RTL_GIGA_MAC_VER_17:
		return JUMBO_4K;
	/* RTL8168c */
	case RTL_GIGA_MAC_VER_18 ... RTL_GIGA_MAC_VER_24:
		return JUMBO_6K;
	default:
		return JUMBO_9K;
	}
}

static void rtl_disable_clk(void *data)
{
	clk_disable_unprepare(data);
}

static int rtl_get_ether_clk(struct rtl8169_private *tp)
{
	struct device *d = tp_to_dev(tp);
	struct clk *clk;
	int rc;

	clk = devm_clk_get(d, "ether_clk");
	if (IS_ERR(clk)) {
		rc = PTR_ERR(clk);
		if (rc == -ENOENT)
			/* clk-core allows NULL (for suspend / resume) */
			rc = 0;
		else if (rc != -EPROBE_DEFER)
			dev_err(d, "failed to get clk: %d\n", rc);
	} else {
		tp->clk = clk;
		rc = clk_prepare_enable(clk);
		if (rc)
			dev_err(d, "failed to enable clk: %d\n", rc);
		else
			rc = devm_add_action_or_reset(d, rtl_disable_clk, clk);
	}

	return rc;
}

static void rtl_init_mac_address(struct rtl8169_private *tp)
{
	struct net_device *dev = tp->dev;
	u8 *mac_addr = dev->dev_addr;
	int rc, i;

	rc = eth_platform_get_mac_address(tp_to_dev(tp), mac_addr);
	if (!rc)
		goto done;

	rtl_read_mac_address(tp, mac_addr);
	if (is_valid_ether_addr(mac_addr))
		goto done;

	for (i = 0; i < ETH_ALEN; i++)
		mac_addr[i] = RTL_R8(tp, MAC0 + i);
	if (is_valid_ether_addr(mac_addr))
		goto done;

	eth_hw_addr_random(dev);
	dev_warn(tp_to_dev(tp), "can't read MAC address, setting random one\n");
done:
	rtl_rar_set(tp, mac_addr);
}

static int rtl_init_one(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct rtl8169_private *tp;
	struct net_device *dev;
	int chipset, region;
	int jumbo_max, rc;

	dev = devm_alloc_etherdev(&pdev->dev, sizeof (*tp));
	if (!dev)
		return -ENOMEM;

	SET_NETDEV_DEV(dev, &pdev->dev);
	dev->netdev_ops = &rtl_netdev_ops;
	tp = netdev_priv(dev);
	tp->dev = dev;
	tp->pci_dev = pdev;
	tp->msg_enable = netif_msg_init(debug.msg_enable, R8169_MSG_DEFAULT);
	tp->supports_gmii = ent->driver_data == RTL_CFG_NO_GBIT ? 0 : 1;

	/* Get the *optional* external "ether_clk" used on some boards */
	rc = rtl_get_ether_clk(tp);
	if (rc)
		return rc;

	/* Disable ASPM completely as that cause random device stop working
	 * problems as well as full system hangs for some PCIe devices users.
	 */
	rc = pci_disable_link_state(pdev, PCIE_LINK_STATE_L0S |
					  PCIE_LINK_STATE_L1);
	tp->aspm_manageable = !rc;

	/* enable device (incl. PCI PM wakeup and hotplug setup) */
	rc = pcim_enable_device(pdev);
	if (rc < 0) {
		dev_err(&pdev->dev, "enable failure\n");
		return rc;
	}

	if (pcim_set_mwi(pdev) < 0)
		dev_info(&pdev->dev, "Mem-Wr-Inval unavailable\n");

	/* use first MMIO region */
	region = ffs(pci_select_bars(pdev, IORESOURCE_MEM)) - 1;
	if (region < 0) {
		dev_err(&pdev->dev, "no MMIO resource found\n");
		return -ENODEV;
	}

	/* check for weird/broken PCI region reporting */
	if (pci_resource_len(pdev, region) < R8169_REGS_SIZE) {
		dev_err(&pdev->dev, "Invalid PCI region size(s), aborting\n");
		return -ENODEV;
	}

	rc = pcim_iomap_regions(pdev, BIT(region), MODULENAME);
	if (rc < 0) {
		dev_err(&pdev->dev, "cannot remap MMIO, aborting\n");
		return rc;
	}

	tp->mmio_addr = pcim_iomap_table(pdev)[region];

	/* Identify chip attached to board */
	rtl8169_get_mac_version(tp);
	if (tp->mac_version == RTL_GIGA_MAC_NONE)
		return -ENODEV;

	tp->cp_cmd = RTL_R16(tp, CPlusCmd);

	if (sizeof(dma_addr_t) > 4 && tp->mac_version >= RTL_GIGA_MAC_VER_18 &&
	    !dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64)))
		dev->features |= NETIF_F_HIGHDMA;

	rtl_init_rxcfg(tp);

	rtl8169_irq_mask_and_ack(tp);

	rtl_hw_initialize(tp);

	rtl_hw_reset(tp);

	pci_set_master(pdev);

	chipset = tp->mac_version;

	rc = rtl_alloc_irq(tp);
	if (rc < 0) {
		dev_err(&pdev->dev, "Can't allocate interrupt\n");
		return rc;
	}

	mutex_init(&tp->wk.mutex);
	INIT_WORK(&tp->wk.work, rtl_task);
	u64_stats_init(&tp->rx_stats.syncp);
	u64_stats_init(&tp->tx_stats.syncp);

	rtl_init_mac_address(tp);

	dev->ethtool_ops = &rtl8169_ethtool_ops;

	netif_napi_add(dev, &tp->napi, rtl8169_poll, NAPI_POLL_WEIGHT);

	/* don't enable SG, IP_CSUM and TSO by default - it might not work
	 * properly for all devices */
	dev->features |= NETIF_F_RXCSUM |
		NETIF_F_HW_VLAN_CTAG_TX | NETIF_F_HW_VLAN_CTAG_RX;

	dev->hw_features = NETIF_F_SG | NETIF_F_IP_CSUM | NETIF_F_TSO |
		NETIF_F_RXCSUM | NETIF_F_HW_VLAN_CTAG_TX |
		NETIF_F_HW_VLAN_CTAG_RX;
	dev->vlan_features = NETIF_F_SG | NETIF_F_IP_CSUM | NETIF_F_TSO |
		NETIF_F_HIGHDMA;
	dev->priv_flags |= IFF_LIVE_ADDR_CHANGE;

	tp->cp_cmd |= RxChkSum | RxVlan;

	/*
	 * Pretend we are using VLANs; This bypasses a nasty bug where
	 * Interrupts stop flowing on high load on 8110SCd controllers.
	 */
	if (tp->mac_version == RTL_GIGA_MAC_VER_05)
		/* Disallow toggling */
		dev->hw_features &= ~NETIF_F_HW_VLAN_CTAG_RX;

	if (rtl_chip_supports_csum_v2(tp))
		dev->hw_features |= NETIF_F_IPV6_CSUM | NETIF_F_TSO6;

	dev->hw_features |= NETIF_F_RXALL;
	dev->hw_features |= NETIF_F_RXFCS;

	/* MTU range: 60 - hw-specific max */
	dev->min_mtu = ETH_ZLEN;
	jumbo_max = rtl_jumbo_max(tp);
	dev->max_mtu = jumbo_max;

	rtl_set_irq_mask(tp);

	tp->fw_name = rtl_chip_infos[chipset].fw_name;

	tp->counters = dmam_alloc_coherent (&pdev->dev, sizeof(*tp->counters),
					    &tp->counters_phys_addr,
					    GFP_KERNEL);
	if (!tp->counters)
		return -ENOMEM;

	pci_set_drvdata(pdev, dev);

	rc = r8169_mdio_register(tp);
	if (rc)
		return rc;

	/* chip gets powered up in rtl_open() */
	rtl_pll_power_down(tp);

	rc = register_netdev(dev);
	if (rc)
		goto err_mdio_unregister;

	netif_info(tp, probe, dev, "%s, %pM, XID %03x, IRQ %d\n",
		   rtl_chip_infos[chipset].name, dev->dev_addr,
		   (RTL_R32(tp, TxConfig) >> 20) & 0xfcf,
		   pci_irq_vector(pdev, 0));

	if (jumbo_max > JUMBO_1K)
		netif_info(tp, probe, dev,
			   "jumbo features [frames: %d bytes, tx checksumming: %s]\n",
			   jumbo_max, tp->mac_version <= RTL_GIGA_MAC_VER_06 ?
			   "ok" : "ko");

	if (r8168_check_dash(tp))
		rtl8168_driver_start(tp);

	if (pci_dev_run_wake(pdev))
		pm_runtime_put_sync(&pdev->dev);

	return 0;

err_mdio_unregister:
	mdiobus_unregister(tp->phydev->mdio.bus);
	return rc;
}

static struct pci_driver rtl8169_pci_driver = {
	.name		= MODULENAME,
	.id_table	= rtl8169_pci_tbl,
	.probe		= rtl_init_one,
	.remove		= rtl_remove_one,
	.shutdown	= rtl_shutdown,
	.driver.pm	= RTL8169_PM_OPS,
};

module_pci_driver(rtl8169_pci_driver);
