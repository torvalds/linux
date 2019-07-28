/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/* Copyright (C) 2015-2018 Netronome Systems, Inc. */

/*
 * nfp_net_ctrl.h
 * Netronome network device driver: Control BAR layout
 * Authors: Jakub Kicinski <jakub.kicinski@netronome.com>
 *          Jason McMullan <jason.mcmullan@netronome.com>
 *          Rolf Neugebauer <rolf.neugebauer@netronome.com>
 *          Brad Petrus <brad.petrus@netronome.com>
 */

#ifndef _NFP_NET_CTRL_H_
#define _NFP_NET_CTRL_H_

#include <linux/types.h>

/**
 * Configuration BAR size.
 *
 * The configuration BAR is 8K in size, but due to
 * THB-350, 32k needs to be reserved.
 */
#define NFP_NET_CFG_BAR_SZ		(32 * 1024)

/**
 * Offset in Freelist buffer where packet starts on RX
 */
#define NFP_NET_RX_OFFSET		32

/**
 * LSO parameters
 * %NFP_NET_LSO_MAX_HDR_SZ:	Maximum header size supported for LSO frames
 * %NFP_NET_LSO_MAX_SEGS:	Maximum number of segments LSO frame can produce
 */
#define NFP_NET_LSO_MAX_HDR_SZ		255
#define NFP_NET_LSO_MAX_SEGS		64

/**
 * Prepend field types
 */
#define NFP_NET_META_FIELD_SIZE		4
#define NFP_NET_META_HASH		1 /* next field carries hash type */
#define NFP_NET_META_MARK		2
#define NFP_NET_META_PORTID		5
#define NFP_NET_META_CSUM		6 /* checksum complete type */
#define NFP_NET_META_CONN_HANDLE	7

#define NFP_META_PORT_ID_CTRL		~0U

/**
 * Hash type pre-pended when a RSS hash was computed
 */
#define NFP_NET_RSS_NONE		0
#define NFP_NET_RSS_IPV4		1
#define NFP_NET_RSS_IPV6		2
#define NFP_NET_RSS_IPV6_EX		3
#define NFP_NET_RSS_IPV4_TCP		4
#define NFP_NET_RSS_IPV6_TCP		5
#define NFP_NET_RSS_IPV6_EX_TCP		6
#define NFP_NET_RSS_IPV4_UDP		7
#define NFP_NET_RSS_IPV6_UDP		8
#define NFP_NET_RSS_IPV6_EX_UDP		9

/**
 * Ring counts
 * %NFP_NET_TXR_MAX:	     Maximum number of TX rings
 * %NFP_NET_RXR_MAX:	     Maximum number of RX rings
 */
#define NFP_NET_TXR_MAX			64
#define NFP_NET_RXR_MAX			64

/**
 * Read/Write config words (0x0000 - 0x002c)
 * %NFP_NET_CFG_CTRL:	     Global control
 * %NFP_NET_CFG_UPDATE:      Indicate which fields are updated
 * %NFP_NET_CFG_TXRS_ENABLE: Bitmask of enabled TX rings
 * %NFP_NET_CFG_RXRS_ENABLE: Bitmask of enabled RX rings
 * %NFP_NET_CFG_MTU:	     Set MTU size
 * %NFP_NET_CFG_FLBUFSZ:     Set freelist buffer size (must be larger than MTU)
 * %NFP_NET_CFG_EXN:	     MSI-X table entry for exceptions
 * %NFP_NET_CFG_LSC:	     MSI-X table entry for link state changes
 * %NFP_NET_CFG_MACADDR:     MAC address
 *
 * TODO:
 * - define Error details in UPDATE
 */
#define NFP_NET_CFG_CTRL		0x0000
#define   NFP_NET_CFG_CTRL_ENABLE	  (0x1 <<  0) /* Global enable */
#define   NFP_NET_CFG_CTRL_PROMISC	  (0x1 <<  1) /* Enable Promisc mode */
#define   NFP_NET_CFG_CTRL_L2BC		  (0x1 <<  2) /* Allow L2 Broadcast */
#define   NFP_NET_CFG_CTRL_L2MC		  (0x1 <<  3) /* Allow L2 Multicast */
#define   NFP_NET_CFG_CTRL_RXCSUM	  (0x1 <<  4) /* Enable RX Checksum */
#define   NFP_NET_CFG_CTRL_TXCSUM	  (0x1 <<  5) /* Enable TX Checksum */
#define   NFP_NET_CFG_CTRL_RXVLAN	  (0x1 <<  6) /* Enable VLAN strip */
#define   NFP_NET_CFG_CTRL_TXVLAN	  (0x1 <<  7) /* Enable VLAN insert */
#define   NFP_NET_CFG_CTRL_SCATTER	  (0x1 <<  8) /* Scatter DMA */
#define   NFP_NET_CFG_CTRL_GATHER	  (0x1 <<  9) /* Gather DMA */
#define   NFP_NET_CFG_CTRL_LSO		  (0x1 << 10) /* LSO/TSO (version 1) */
#define   NFP_NET_CFG_CTRL_CTAG_FILTER	  (0x1 << 11) /* VLAN CTAG filtering */
#define   NFP_NET_CFG_CTRL_CMSG_DATA	  (0x1 << 12) /* RX cmsgs on data Qs */
#define   NFP_NET_CFG_CTRL_RINGCFG	  (0x1 << 16) /* Ring runtime changes */
#define   NFP_NET_CFG_CTRL_RSS		  (0x1 << 17) /* RSS (version 1) */
#define   NFP_NET_CFG_CTRL_IRQMOD	  (0x1 << 18) /* Interrupt moderation */
#define   NFP_NET_CFG_CTRL_RINGPRIO	  (0x1 << 19) /* Ring priorities */
#define   NFP_NET_CFG_CTRL_MSIXAUTO	  (0x1 << 20) /* MSI-X auto-masking */
#define   NFP_NET_CFG_CTRL_TXRWB	  (0x1 << 21) /* Write-back of TX ring*/
#define   NFP_NET_CFG_CTRL_VXLAN	  (0x1 << 24) /* VXLAN tunnel support */
#define   NFP_NET_CFG_CTRL_NVGRE	  (0x1 << 25) /* NVGRE tunnel support */
#define   NFP_NET_CFG_CTRL_BPF		  (0x1 << 27) /* BPF offload capable */
#define   NFP_NET_CFG_CTRL_LSO2		  (0x1 << 28) /* LSO/TSO (version 2) */
#define   NFP_NET_CFG_CTRL_RSS2		  (0x1 << 29) /* RSS (version 2) */
#define   NFP_NET_CFG_CTRL_CSUM_COMPLETE  (0x1 << 30) /* Checksum complete */
#define   NFP_NET_CFG_CTRL_LIVE_ADDR	  (0x1 << 31) /* live MAC addr change */

#define NFP_NET_CFG_CTRL_LSO_ANY	(NFP_NET_CFG_CTRL_LSO | \
					 NFP_NET_CFG_CTRL_LSO2)
#define NFP_NET_CFG_CTRL_RSS_ANY	(NFP_NET_CFG_CTRL_RSS | \
					 NFP_NET_CFG_CTRL_RSS2)
#define NFP_NET_CFG_CTRL_RXCSUM_ANY	(NFP_NET_CFG_CTRL_RXCSUM | \
					 NFP_NET_CFG_CTRL_CSUM_COMPLETE)
#define NFP_NET_CFG_CTRL_CHAIN_META	(NFP_NET_CFG_CTRL_RSS2 | \
					 NFP_NET_CFG_CTRL_CSUM_COMPLETE)

#define NFP_NET_CFG_UPDATE		0x0004
#define   NFP_NET_CFG_UPDATE_GEN	  (0x1 <<  0) /* General update */
#define   NFP_NET_CFG_UPDATE_RING	  (0x1 <<  1) /* Ring config change */
#define   NFP_NET_CFG_UPDATE_RSS	  (0x1 <<  2) /* RSS config change */
#define   NFP_NET_CFG_UPDATE_TXRPRIO	  (0x1 <<  3) /* TX Ring prio change */
#define   NFP_NET_CFG_UPDATE_RXRPRIO	  (0x1 <<  4) /* RX Ring prio change */
#define   NFP_NET_CFG_UPDATE_MSIX	  (0x1 <<  5) /* MSI-X change */
#define   NFP_NET_CFG_UPDATE_RESET	  (0x1 <<  7) /* Update due to FLR */
#define   NFP_NET_CFG_UPDATE_IRQMOD	  (0x1 <<  8) /* IRQ mod change */
#define   NFP_NET_CFG_UPDATE_VXLAN	  (0x1 <<  9) /* VXLAN port change */
#define   NFP_NET_CFG_UPDATE_BPF	  (0x1 << 10) /* BPF program load */
#define   NFP_NET_CFG_UPDATE_MACADDR	  (0x1 << 11) /* MAC address change */
#define   NFP_NET_CFG_UPDATE_MBOX	  (0x1 << 12) /* Mailbox update */
#define   NFP_NET_CFG_UPDATE_VF		  (0x1 << 13) /* VF settings change */
#define   NFP_NET_CFG_UPDATE_CRYPTO	  (0x1 << 14) /* Crypto on/off */
#define   NFP_NET_CFG_UPDATE_ERR	  (0x1 << 31) /* A error occurred */
#define NFP_NET_CFG_TXRS_ENABLE		0x0008
#define NFP_NET_CFG_RXRS_ENABLE		0x0010
#define NFP_NET_CFG_MTU			0x0018
#define NFP_NET_CFG_FLBUFSZ		0x001c
#define NFP_NET_CFG_EXN			0x001f
#define NFP_NET_CFG_LSC			0x0020
#define NFP_NET_CFG_MACADDR		0x0024

/**
 * Read-only words (0x0030 - 0x0050):
 * %NFP_NET_CFG_VERSION:     Firmware version number
 * %NFP_NET_CFG_STS:	     Status
 * %NFP_NET_CFG_CAP:	     Capabilities (same bits as %NFP_NET_CFG_CTRL)
 * %NFP_NET_CFG_MAX_TXRINGS: Maximum number of TX rings
 * %NFP_NET_CFG_MAX_RXRINGS: Maximum number of RX rings
 * %NFP_NET_CFG_MAX_MTU:     Maximum support MTU
 * %NFP_NET_CFG_START_TXQ:   Start Queue Control Queue to use for TX (PF only)
 * %NFP_NET_CFG_START_RXQ:   Start Queue Control Queue to use for RX (PF only)
 *
 * TODO:
 * - define more STS bits
 */
#define NFP_NET_CFG_VERSION		0x0030
#define   NFP_NET_CFG_VERSION_RESERVED_MASK	(0xff << 24)
#define   NFP_NET_CFG_VERSION_CLASS_MASK  (0xff << 16)
#define   NFP_NET_CFG_VERSION_CLASS(x)	  (((x) & 0xff) << 16)
#define   NFP_NET_CFG_VERSION_CLASS_GENERIC	0
#define   NFP_NET_CFG_VERSION_MAJOR_MASK  (0xff <<  8)
#define   NFP_NET_CFG_VERSION_MAJOR(x)	  (((x) & 0xff) <<  8)
#define   NFP_NET_CFG_VERSION_MINOR_MASK  (0xff <<  0)
#define   NFP_NET_CFG_VERSION_MINOR(x)	  (((x) & 0xff) <<  0)
#define NFP_NET_CFG_STS			0x0034
#define   NFP_NET_CFG_STS_LINK		  (0x1 << 0) /* Link up or down */
/* Link rate */
#define   NFP_NET_CFG_STS_LINK_RATE_SHIFT 1
#define   NFP_NET_CFG_STS_LINK_RATE_MASK  0xF
#define   NFP_NET_CFG_STS_LINK_RATE	  \
	(NFP_NET_CFG_STS_LINK_RATE_MASK << NFP_NET_CFG_STS_LINK_RATE_SHIFT)
#define   NFP_NET_CFG_STS_LINK_RATE_UNSUPPORTED   0
#define   NFP_NET_CFG_STS_LINK_RATE_UNKNOWN	  1
#define   NFP_NET_CFG_STS_LINK_RATE_1G		  2
#define   NFP_NET_CFG_STS_LINK_RATE_10G		  3
#define   NFP_NET_CFG_STS_LINK_RATE_25G		  4
#define   NFP_NET_CFG_STS_LINK_RATE_40G		  5
#define   NFP_NET_CFG_STS_LINK_RATE_50G		  6
#define   NFP_NET_CFG_STS_LINK_RATE_100G	  7
#define NFP_NET_CFG_CAP			0x0038
#define NFP_NET_CFG_MAX_TXRINGS		0x003c
#define NFP_NET_CFG_MAX_RXRINGS		0x0040
#define NFP_NET_CFG_MAX_MTU		0x0044
/* Next two words are being used by VFs for solving THB350 issue */
#define NFP_NET_CFG_START_TXQ		0x0048
#define NFP_NET_CFG_START_RXQ		0x004c

/**
 * Prepend configuration
 */
#define NFP_NET_CFG_RX_OFFSET		0x0050
#define NFP_NET_CFG_RX_OFFSET_DYNAMIC		0	/* Prepend mode */

/**
 * RSS capabilities
 * %NFP_NET_CFG_RSS_CAP_HFUNC:	supported hash functions (same bits as
 *				%NFP_NET_CFG_RSS_HFUNC)
 */
#define NFP_NET_CFG_RSS_CAP		0x0054
#define   NFP_NET_CFG_RSS_CAP_HFUNC	  0xff000000

/**
 * TLV area start
 * %NFP_NET_CFG_TLV_BASE:	start anchor of the TLV area
 */
#define NFP_NET_CFG_TLV_BASE		0x0058

/**
 * VXLAN/UDP encap configuration
 * %NFP_NET_CFG_VXLAN_PORT:	Base address of table of tunnels' UDP dst ports
 * %NFP_NET_CFG_VXLAN_SZ:	Size of the UDP port table in bytes
 */
#define NFP_NET_CFG_VXLAN_PORT		0x0060
#define NFP_NET_CFG_VXLAN_SZ		  0x0008

/**
 * BPF section
 * %NFP_NET_CFG_BPF_ABI:	BPF ABI version
 * %NFP_NET_CFG_BPF_CAP:	BPF capabilities
 * %NFP_NET_CFG_BPF_MAX_LEN:	Maximum size of JITed BPF code in bytes
 * %NFP_NET_CFG_BPF_START:	Offset at which BPF will be loaded
 * %NFP_NET_CFG_BPF_DONE:	Offset to jump to on exit
 * %NFP_NET_CFG_BPF_STACK_SZ:	Total size of stack area in 64B chunks
 * %NFP_NET_CFG_BPF_INL_MTU:	Packet data split offset in 64B chunks
 * %NFP_NET_CFG_BPF_SIZE:	Size of the JITed BPF code in instructions
 * %NFP_NET_CFG_BPF_ADDR:	DMA address of the buffer with JITed BPF code
 */
#define NFP_NET_CFG_BPF_ABI		0x0080
#define NFP_NET_CFG_BPF_CAP		0x0081
#define   NFP_NET_BPF_CAP_RELO		(1 << 0) /* seamless reload */
#define NFP_NET_CFG_BPF_MAX_LEN		0x0082
#define NFP_NET_CFG_BPF_START		0x0084
#define NFP_NET_CFG_BPF_DONE		0x0086
#define NFP_NET_CFG_BPF_STACK_SZ	0x0088
#define NFP_NET_CFG_BPF_INL_MTU		0x0089
#define NFP_NET_CFG_BPF_SIZE		0x008e
#define NFP_NET_CFG_BPF_ADDR		0x0090
#define   NFP_NET_CFG_BPF_CFG_8CTX	(1 << 0) /* 8ctx mode */
#define   NFP_NET_CFG_BPF_CFG_MASK	7ULL
#define   NFP_NET_CFG_BPF_ADDR_MASK	(~NFP_NET_CFG_BPF_CFG_MASK)

/**
 * 40B reserved for future use (0x0098 - 0x00c0)
 */
#define NFP_NET_CFG_RESERVED		0x0098
#define NFP_NET_CFG_RESERVED_SZ		0x0028

/**
 * RSS configuration (0x0100 - 0x01ac):
 * Used only when NFP_NET_CFG_CTRL_RSS is enabled
 * %NFP_NET_CFG_RSS_CFG:     RSS configuration word
 * %NFP_NET_CFG_RSS_KEY:     RSS "secret" key
 * %NFP_NET_CFG_RSS_ITBL:    RSS indirection table
 */
#define NFP_NET_CFG_RSS_BASE		0x0100
#define NFP_NET_CFG_RSS_CTRL		NFP_NET_CFG_RSS_BASE
#define   NFP_NET_CFG_RSS_MASK		  (0x7f)
#define   NFP_NET_CFG_RSS_MASK_of(_x)	  ((_x) & 0x7f)
#define   NFP_NET_CFG_RSS_IPV4		  (1 <<  8) /* RSS for IPv4 */
#define   NFP_NET_CFG_RSS_IPV6		  (1 <<  9) /* RSS for IPv6 */
#define   NFP_NET_CFG_RSS_IPV4_TCP	  (1 << 10) /* RSS for IPv4/TCP */
#define   NFP_NET_CFG_RSS_IPV4_UDP	  (1 << 11) /* RSS for IPv4/UDP */
#define   NFP_NET_CFG_RSS_IPV6_TCP	  (1 << 12) /* RSS for IPv6/TCP */
#define   NFP_NET_CFG_RSS_IPV6_UDP	  (1 << 13) /* RSS for IPv6/UDP */
#define   NFP_NET_CFG_RSS_HFUNC		  0xff000000
#define   NFP_NET_CFG_RSS_TOEPLITZ	  (1 << 24) /* Use Toeplitz hash */
#define   NFP_NET_CFG_RSS_XOR		  (1 << 25) /* Use XOR as hash */
#define   NFP_NET_CFG_RSS_CRC32		  (1 << 26) /* Use CRC32 as hash */
#define   NFP_NET_CFG_RSS_HFUNCS	  3
#define NFP_NET_CFG_RSS_KEY		(NFP_NET_CFG_RSS_BASE + 0x4)
#define NFP_NET_CFG_RSS_KEY_SZ		0x28
#define NFP_NET_CFG_RSS_ITBL		(NFP_NET_CFG_RSS_BASE + 0x4 + \
					 NFP_NET_CFG_RSS_KEY_SZ)
#define NFP_NET_CFG_RSS_ITBL_SZ		0x80

/**
 * TX ring configuration (0x200 - 0x800)
 * %NFP_NET_CFG_TXR_BASE:    Base offset for TX ring configuration
 * %NFP_NET_CFG_TXR_ADDR:    Per TX ring DMA address (8B entries)
 * %NFP_NET_CFG_TXR_WB_ADDR: Per TX ring write back DMA address (8B entries)
 * %NFP_NET_CFG_TXR_SZ:      Per TX ring ring size (1B entries)
 * %NFP_NET_CFG_TXR_VEC:     Per TX ring MSI-X table entry (1B entries)
 * %NFP_NET_CFG_TXR_PRIO:    Per TX ring priority (1B entries)
 * %NFP_NET_CFG_TXR_IRQ_MOD: Per TX ring interrupt moderation packet
 */
#define NFP_NET_CFG_TXR_BASE		0x0200
#define NFP_NET_CFG_TXR_ADDR(_x)	(NFP_NET_CFG_TXR_BASE + ((_x) * 0x8))
#define NFP_NET_CFG_TXR_WB_ADDR(_x)	(NFP_NET_CFG_TXR_BASE + 0x200 + \
					 ((_x) * 0x8))
#define NFP_NET_CFG_TXR_SZ(_x)		(NFP_NET_CFG_TXR_BASE + 0x400 + (_x))
#define NFP_NET_CFG_TXR_VEC(_x)		(NFP_NET_CFG_TXR_BASE + 0x440 + (_x))
#define NFP_NET_CFG_TXR_PRIO(_x)	(NFP_NET_CFG_TXR_BASE + 0x480 + (_x))
#define NFP_NET_CFG_TXR_IRQ_MOD(_x)	(NFP_NET_CFG_TXR_BASE + 0x500 + \
					 ((_x) * 0x4))

/**
 * RX ring configuration (0x0800 - 0x0c00)
 * %NFP_NET_CFG_RXR_BASE:    Base offset for RX ring configuration
 * %NFP_NET_CFG_RXR_ADDR:    Per RX ring DMA address (8B entries)
 * %NFP_NET_CFG_RXR_SZ:      Per RX ring ring size (1B entries)
 * %NFP_NET_CFG_RXR_VEC:     Per RX ring MSI-X table entry (1B entries)
 * %NFP_NET_CFG_RXR_PRIO:    Per RX ring priority (1B entries)
 * %NFP_NET_CFG_RXR_IRQ_MOD: Per RX ring interrupt moderation (4B entries)
 */
#define NFP_NET_CFG_RXR_BASE		0x0800
#define NFP_NET_CFG_RXR_ADDR(_x)	(NFP_NET_CFG_RXR_BASE + ((_x) * 0x8))
#define NFP_NET_CFG_RXR_SZ(_x)		(NFP_NET_CFG_RXR_BASE + 0x200 + (_x))
#define NFP_NET_CFG_RXR_VEC(_x)		(NFP_NET_CFG_RXR_BASE + 0x240 + (_x))
#define NFP_NET_CFG_RXR_PRIO(_x)	(NFP_NET_CFG_RXR_BASE + 0x280 + (_x))
#define NFP_NET_CFG_RXR_IRQ_MOD(_x)	(NFP_NET_CFG_RXR_BASE + 0x300 + \
					 ((_x) * 0x4))

/**
 * Interrupt Control/Cause registers (0x0c00 - 0x0d00)
 * These registers are only used when MSI-X auto-masking is not
 * enabled (%NFP_NET_CFG_CTRL_MSIXAUTO not set).  The array is index
 * by MSI-X entry and are 1B in size.  If an entry is zero, the
 * corresponding entry is enabled.  If the FW generates an interrupt,
 * it writes a cause into the corresponding field.  This also masks
 * the MSI-X entry and the host driver must clear the register to
 * re-enable the interrupt.
 */
#define NFP_NET_CFG_ICR_BASE		0x0c00
#define NFP_NET_CFG_ICR(_x)		(NFP_NET_CFG_ICR_BASE + (_x))
#define   NFP_NET_CFG_ICR_UNMASKED	0x0
#define   NFP_NET_CFG_ICR_RXTX		0x1
#define   NFP_NET_CFG_ICR_LSC		0x2

/**
 * General device stats (0x0d00 - 0x0d90)
 * all counters are 64bit.
 */
#define NFP_NET_CFG_STATS_BASE		0x0d00
#define NFP_NET_CFG_STATS_RX_DISCARDS	(NFP_NET_CFG_STATS_BASE + 0x00)
#define NFP_NET_CFG_STATS_RX_ERRORS	(NFP_NET_CFG_STATS_BASE + 0x08)
#define NFP_NET_CFG_STATS_RX_OCTETS	(NFP_NET_CFG_STATS_BASE + 0x10)
#define NFP_NET_CFG_STATS_RX_UC_OCTETS	(NFP_NET_CFG_STATS_BASE + 0x18)
#define NFP_NET_CFG_STATS_RX_MC_OCTETS	(NFP_NET_CFG_STATS_BASE + 0x20)
#define NFP_NET_CFG_STATS_RX_BC_OCTETS	(NFP_NET_CFG_STATS_BASE + 0x28)
#define NFP_NET_CFG_STATS_RX_FRAMES	(NFP_NET_CFG_STATS_BASE + 0x30)
#define NFP_NET_CFG_STATS_RX_MC_FRAMES	(NFP_NET_CFG_STATS_BASE + 0x38)
#define NFP_NET_CFG_STATS_RX_BC_FRAMES	(NFP_NET_CFG_STATS_BASE + 0x40)

#define NFP_NET_CFG_STATS_TX_DISCARDS	(NFP_NET_CFG_STATS_BASE + 0x48)
#define NFP_NET_CFG_STATS_TX_ERRORS	(NFP_NET_CFG_STATS_BASE + 0x50)
#define NFP_NET_CFG_STATS_TX_OCTETS	(NFP_NET_CFG_STATS_BASE + 0x58)
#define NFP_NET_CFG_STATS_TX_UC_OCTETS	(NFP_NET_CFG_STATS_BASE + 0x60)
#define NFP_NET_CFG_STATS_TX_MC_OCTETS	(NFP_NET_CFG_STATS_BASE + 0x68)
#define NFP_NET_CFG_STATS_TX_BC_OCTETS	(NFP_NET_CFG_STATS_BASE + 0x70)
#define NFP_NET_CFG_STATS_TX_FRAMES	(NFP_NET_CFG_STATS_BASE + 0x78)
#define NFP_NET_CFG_STATS_TX_MC_FRAMES	(NFP_NET_CFG_STATS_BASE + 0x80)
#define NFP_NET_CFG_STATS_TX_BC_FRAMES	(NFP_NET_CFG_STATS_BASE + 0x88)

#define NFP_NET_CFG_STATS_APP0_FRAMES	(NFP_NET_CFG_STATS_BASE + 0x90)
#define NFP_NET_CFG_STATS_APP0_BYTES	(NFP_NET_CFG_STATS_BASE + 0x98)
#define NFP_NET_CFG_STATS_APP1_FRAMES	(NFP_NET_CFG_STATS_BASE + 0xa0)
#define NFP_NET_CFG_STATS_APP1_BYTES	(NFP_NET_CFG_STATS_BASE + 0xa8)
#define NFP_NET_CFG_STATS_APP2_FRAMES	(NFP_NET_CFG_STATS_BASE + 0xb0)
#define NFP_NET_CFG_STATS_APP2_BYTES	(NFP_NET_CFG_STATS_BASE + 0xb8)
#define NFP_NET_CFG_STATS_APP3_FRAMES	(NFP_NET_CFG_STATS_BASE + 0xc0)
#define NFP_NET_CFG_STATS_APP3_BYTES	(NFP_NET_CFG_STATS_BASE + 0xc8)

/**
 * Per ring stats (0x1000 - 0x1800)
 * options, 64bit per entry
 * %NFP_NET_CFG_TXR_STATS:   TX ring statistics (Packet and Byte count)
 * %NFP_NET_CFG_RXR_STATS:   RX ring statistics (Packet and Byte count)
 */
#define NFP_NET_CFG_TXR_STATS_BASE	0x1000
#define NFP_NET_CFG_TXR_STATS(_x)	(NFP_NET_CFG_TXR_STATS_BASE + \
					 ((_x) * 0x10))
#define NFP_NET_CFG_RXR_STATS_BASE	0x1400
#define NFP_NET_CFG_RXR_STATS(_x)	(NFP_NET_CFG_RXR_STATS_BASE + \
					 ((_x) * 0x10))

/**
 * General use mailbox area (0x1800 - 0x19ff)
 * 4B used for update command and 4B return code
 * followed by a max of 504B of variable length value
 */
#define NFP_NET_CFG_MBOX_BASE		0x1800
#define NFP_NET_CFG_MBOX_VAL_MAX_SZ	0x1F8

#define NFP_NET_CFG_MBOX_SIMPLE_CMD	0x0
#define NFP_NET_CFG_MBOX_SIMPLE_RET	0x4
#define NFP_NET_CFG_MBOX_SIMPLE_VAL	0x8

#define NFP_NET_CFG_MBOX_CMD_CTAG_FILTER_ADD 1
#define NFP_NET_CFG_MBOX_CMD_CTAG_FILTER_KILL 2

#define NFP_NET_CFG_MBOX_CMD_PCI_DSCP_PRIOMAP_SET	5
#define NFP_NET_CFG_MBOX_CMD_TLV_CMSG			6

/**
 * VLAN filtering using general use mailbox
 * %NFP_NET_CFG_VLAN_FILTER:		Base address of VLAN filter mailbox
 * %NFP_NET_CFG_VLAN_FILTER_VID:	VLAN ID to filter
 * %NFP_NET_CFG_VLAN_FILTER_PROTO:	VLAN proto to filter
 * %NFP_NET_CFG_VXLAN_SZ:		Size of the VLAN filter mailbox in bytes
 */
#define NFP_NET_CFG_VLAN_FILTER		NFP_NET_CFG_MBOX_SIMPLE_VAL
#define  NFP_NET_CFG_VLAN_FILTER_VID	NFP_NET_CFG_VLAN_FILTER
#define  NFP_NET_CFG_VLAN_FILTER_PROTO	 (NFP_NET_CFG_VLAN_FILTER + 2)
#define NFP_NET_CFG_VLAN_FILTER_SZ	 0x0004

/**
 * TLV capabilities
 * %NFP_NET_CFG_TLV_TYPE:	Offset of type within the TLV
 * %NFP_NET_CFG_TLV_TYPE_REQUIRED: Driver must be able to parse the TLV
 * %NFP_NET_CFG_TLV_LENGTH:	Offset of length within the TLV
 * %NFP_NET_CFG_TLV_LENGTH_INC: TLV length increments
 * %NFP_NET_CFG_TLV_VALUE:	Offset of value with the TLV
 *
 * List of simple TLV structures, first one starts at %NFP_NET_CFG_TLV_BASE.
 * Last structure must be of type %NFP_NET_CFG_TLV_TYPE_END.  Presence of TLVs
 * is indicated by %NFP_NET_CFG_TLV_BASE being non-zero.  TLV structures may
 * fill the entire remainder of the BAR or be shorter.  FW must make sure TLVs
 * don't conflict with other features which allocate space beyond
 * %NFP_NET_CFG_TLV_BASE.  %NFP_NET_CFG_TLV_TYPE_RESERVED should be used to wrap
 * space used by such features.
 * Note that the 4 byte TLV header is not counted in %NFP_NET_CFG_TLV_LENGTH.
 */
#define NFP_NET_CFG_TLV_TYPE		0x00
#define   NFP_NET_CFG_TLV_TYPE_REQUIRED   0x8000
#define NFP_NET_CFG_TLV_LENGTH		0x02
#define   NFP_NET_CFG_TLV_LENGTH_INC	  4
#define NFP_NET_CFG_TLV_VALUE		0x04

#define NFP_NET_CFG_TLV_HEADER_REQUIRED 0x80000000
#define NFP_NET_CFG_TLV_HEADER_TYPE	0x7fff0000
#define NFP_NET_CFG_TLV_HEADER_LENGTH	0x0000ffff

/**
 * Capability TLV types
 *
 * %NFP_NET_CFG_TLV_TYPE_UNKNOWN:
 * Special TLV type to catch bugs, should never be encountered.  Drivers should
 * treat encountering this type as error and refuse to probe.
 *
 * %NFP_NET_CFG_TLV_TYPE_RESERVED:
 * Reserved space, may contain legacy fixed-offset fields, or be used for
 * padding.  The use of this type should be otherwise avoided.
 *
 * %NFP_NET_CFG_TLV_TYPE_END:
 * Empty, end of TLV list.  Must be the last TLV.  Drivers will stop processing
 * further TLVs when encountered.
 *
 * %NFP_NET_CFG_TLV_TYPE_ME_FREQ:
 * Single word, ME frequency in MHz as used in calculation for
 * %NFP_NET_CFG_RXR_IRQ_MOD and %NFP_NET_CFG_TXR_IRQ_MOD.
 *
 * %NFP_NET_CFG_TLV_TYPE_MBOX:
 * Variable, mailbox area.  Overwrites the default location which is
 * %NFP_NET_CFG_MBOX_BASE and length %NFP_NET_CFG_MBOX_VAL_MAX_SZ.
 *
 * %NFP_NET_CFG_TLV_TYPE_EXPERIMENTAL0:
 * %NFP_NET_CFG_TLV_TYPE_EXPERIMENTAL1:
 * Variable, experimental IDs.  IDs designated for internal development and
 * experiments before a stable TLV ID has been allocated to a feature.  Should
 * never be present in production firmware.
 *
 * %NFP_NET_CFG_TLV_TYPE_REPR_CAP:
 * Single word, equivalent of %NFP_NET_CFG_CAP for representors, features which
 * can be used on representors.
 *
 * %NFP_NET_CFG_TLV_TYPE_MBOX_CMSG_TYPES:
 * Variable, bitmap of control message types supported by the mailbox handler.
 * Bit 0 corresponds to message type 0, bit 1 to 1, etc.  Control messages are
 * encapsulated into simple TLVs, with an end TLV and written to the Mailbox.
 *
 * %NFP_NET_CFG_TLV_TYPE_CRYPTO_OPS:
 * 8 words, bitmaps of supported and enabled crypto operations.
 * First 16B (4 words) contains a bitmap of supported crypto operations,
 * and next 16B contain the enabled operations.
 */
#define NFP_NET_CFG_TLV_TYPE_UNKNOWN		0
#define NFP_NET_CFG_TLV_TYPE_RESERVED		1
#define NFP_NET_CFG_TLV_TYPE_END		2
#define NFP_NET_CFG_TLV_TYPE_ME_FREQ		3
#define NFP_NET_CFG_TLV_TYPE_MBOX		4
#define NFP_NET_CFG_TLV_TYPE_EXPERIMENTAL0	5
#define NFP_NET_CFG_TLV_TYPE_EXPERIMENTAL1	6
#define NFP_NET_CFG_TLV_TYPE_REPR_CAP		7
#define NFP_NET_CFG_TLV_TYPE_MBOX_CMSG_TYPES	10
#define NFP_NET_CFG_TLV_TYPE_CRYPTO_OPS		11 /* see crypto/fw.h */

struct device;

/**
 * struct nfp_net_tlv_caps - parsed control BAR TLV capabilities
 * @me_freq_mhz:	ME clock_freq (MHz)
 * @mbox_off:		vNIC mailbox area offset
 * @mbox_len:		vNIC mailbox area length
 * @repr_cap:		capabilities for representors
 * @mbox_cmsg_types:	cmsgs which can be passed through the mailbox
 * @crypto_ops:		supported crypto operations
 * @crypto_enable_off:	offset of crypto ops enable region
 */
struct nfp_net_tlv_caps {
	u32 me_freq_mhz;
	unsigned int mbox_off;
	unsigned int mbox_len;
	u32 repr_cap;
	u32 mbox_cmsg_types;
	u32 crypto_ops;
	unsigned int crypto_enable_off;
};

int nfp_net_tlv_caps_parse(struct device *dev, u8 __iomem *ctrl_mem,
			   struct nfp_net_tlv_caps *caps);
#endif /* _NFP_NET_CTRL_H_ */
