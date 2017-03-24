/*
 * Copyright (C) 2015 Netronome Systems, Inc.
 *
 * This software is dual licensed under the GNU General License Version 2,
 * June 1991 as shown in the file COPYING in the top-level directory of this
 * source tree or the BSD 2-Clause License provided below.  You have the
 * option to license this software under the complete terms of either license.
 *
 * The BSD 2-Clause License:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      1. Redistributions of source code must retain the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer.
 *
 *      2. Redistributions in binary form must reproduce the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer in the documentation and/or other materials
 *         provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

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

/* IMPORTANT: This header file is shared with the FW,
 *	      no OS specific constructs, please!
 */

/**
 * Configuration BAR size.
 *
 * The configuration BAR is 8K in size, but due to
 * THB-350, 32k needs to be reserved.
 */
#define NFP_NET_CFG_BAR_SZ              (32 * 1024)

/**
 * Offset in Freelist buffer where packet starts on RX
 */
#define NFP_NET_RX_OFFSET               32

/**
 * Maximum header size supported for LSO frames
 */
#define NFP_NET_LSO_MAX_HDR_SZ		255

/**
 * Prepend field types
 */
#define NFP_NET_META_FIELD_SIZE		4
#define NFP_NET_META_HASH		1 /* next field carries hash type */
#define NFP_NET_META_MARK		2

/**
 * Hash type pre-pended when a RSS hash was computed
 */
#define NFP_NET_RSS_NONE                0
#define NFP_NET_RSS_IPV4                1
#define NFP_NET_RSS_IPV6                2
#define NFP_NET_RSS_IPV6_EX             3
#define NFP_NET_RSS_IPV4_TCP            4
#define NFP_NET_RSS_IPV6_TCP            5
#define NFP_NET_RSS_IPV6_EX_TCP         6
#define NFP_NET_RSS_IPV4_UDP            7
#define NFP_NET_RSS_IPV6_UDP            8
#define NFP_NET_RSS_IPV6_EX_UDP         9

/**
 * @NFP_NET_TXR_MAX:         Maximum number of TX rings
 * @NFP_NET_RXR_MAX:         Maximum number of RX rings
 */
#define NFP_NET_TXR_MAX                 64
#define NFP_NET_RXR_MAX                 64

/**
 * Read/Write config words (0x0000 - 0x002c)
 * @NFP_NET_CFG_CTRL:        Global control
 * @NFP_NET_CFG_UPDATE:      Indicate which fields are updated
 * @NFP_NET_CFG_TXRS_ENABLE: Bitmask of enabled TX rings
 * @NFP_NET_CFG_RXRS_ENABLE: Bitmask of enabled RX rings
 * @NFP_NET_CFG_MTU:         Set MTU size
 * @NFP_NET_CFG_FLBUFSZ:     Set freelist buffer size (must be larger than MTU)
 * @NFP_NET_CFG_EXN:         MSI-X table entry for exceptions
 * @NFP_NET_CFG_LSC:         MSI-X table entry for link state changes
 * @NFP_NET_CFG_MACADDR:     MAC address
 *
 * TODO:
 * - define Error details in UPDATE
 */
#define NFP_NET_CFG_CTRL                0x0000
#define   NFP_NET_CFG_CTRL_ENABLE         (0x1 <<  0) /* Global enable */
#define   NFP_NET_CFG_CTRL_PROMISC        (0x1 <<  1) /* Enable Promisc mode */
#define   NFP_NET_CFG_CTRL_L2BC           (0x1 <<  2) /* Allow L2 Broadcast */
#define   NFP_NET_CFG_CTRL_L2MC           (0x1 <<  3) /* Allow L2 Multicast */
#define   NFP_NET_CFG_CTRL_RXCSUM         (0x1 <<  4) /* Enable RX Checksum */
#define   NFP_NET_CFG_CTRL_TXCSUM         (0x1 <<  5) /* Enable TX Checksum */
#define   NFP_NET_CFG_CTRL_RXVLAN         (0x1 <<  6) /* Enable VLAN strip */
#define   NFP_NET_CFG_CTRL_TXVLAN         (0x1 <<  7) /* Enable VLAN insert */
#define   NFP_NET_CFG_CTRL_SCATTER        (0x1 <<  8) /* Scatter DMA */
#define   NFP_NET_CFG_CTRL_GATHER         (0x1 <<  9) /* Gather DMA */
#define   NFP_NET_CFG_CTRL_LSO            (0x1 << 10) /* LSO/TSO */
#define   NFP_NET_CFG_CTRL_RINGCFG        (0x1 << 16) /* Ring runtime changes */
#define   NFP_NET_CFG_CTRL_RSS            (0x1 << 17) /* RSS */
#define   NFP_NET_CFG_CTRL_IRQMOD         (0x1 << 18) /* Interrupt moderation */
#define   NFP_NET_CFG_CTRL_RINGPRIO       (0x1 << 19) /* Ring priorities */
#define   NFP_NET_CFG_CTRL_MSIXAUTO       (0x1 << 20) /* MSI-X auto-masking */
#define   NFP_NET_CFG_CTRL_TXRWB          (0x1 << 21) /* Write-back of TX ring*/
#define   NFP_NET_CFG_CTRL_L2SWITCH       (0x1 << 22) /* L2 Switch */
#define   NFP_NET_CFG_CTRL_L2SWITCH_LOCAL (0x1 << 23) /* Switch to local */
#define   NFP_NET_CFG_CTRL_VXLAN	  (0x1 << 24) /* VXLAN tunnel support */
#define   NFP_NET_CFG_CTRL_NVGRE	  (0x1 << 25) /* NVGRE tunnel support */
#define   NFP_NET_CFG_CTRL_BPF		  (0x1 << 27) /* BPF offload capable */
#define NFP_NET_CFG_UPDATE              0x0004
#define   NFP_NET_CFG_UPDATE_GEN          (0x1 <<  0) /* General update */
#define   NFP_NET_CFG_UPDATE_RING         (0x1 <<  1) /* Ring config change */
#define   NFP_NET_CFG_UPDATE_RSS          (0x1 <<  2) /* RSS config change */
#define   NFP_NET_CFG_UPDATE_TXRPRIO      (0x1 <<  3) /* TX Ring prio change */
#define   NFP_NET_CFG_UPDATE_RXRPRIO      (0x1 <<  4) /* RX Ring prio change */
#define   NFP_NET_CFG_UPDATE_MSIX         (0x1 <<  5) /* MSI-X change */
#define   NFP_NET_CFG_UPDATE_L2SWITCH     (0x1 <<  6) /* Switch changes */
#define   NFP_NET_CFG_UPDATE_RESET        (0x1 <<  7) /* Update due to FLR */
#define   NFP_NET_CFG_UPDATE_IRQMOD       (0x1 <<  8) /* IRQ mod change */
#define   NFP_NET_CFG_UPDATE_VXLAN	  (0x1 <<  9) /* VXLAN port change */
#define   NFP_NET_CFG_UPDATE_BPF	  (0x1 << 10) /* BPF program load */
#define   NFP_NET_CFG_UPDATE_ERR          (0x1 << 31) /* A error occurred */
#define NFP_NET_CFG_TXRS_ENABLE         0x0008
#define NFP_NET_CFG_RXRS_ENABLE         0x0010
#define NFP_NET_CFG_MTU                 0x0018
#define NFP_NET_CFG_FLBUFSZ             0x001c
#define NFP_NET_CFG_EXN                 0x001f
#define NFP_NET_CFG_LSC                 0x0020
#define NFP_NET_CFG_MACADDR             0x0024

/**
 * Read-only words (0x0030 - 0x0050):
 * @NFP_NET_CFG_VERSION:     Firmware version number
 * @NFP_NET_CFG_STS:         Status
 * @NFP_NET_CFG_CAP:         Capabilities (same bits as @NFP_NET_CFG_CTRL)
 * @NFP_NET_CFG_MAX_TXRINGS: Maximum number of TX rings
 * @NFP_NET_CFG_MAX_RXRINGS: Maximum number of RX rings
 * @NFP_NET_CFG_MAX_MTU:     Maximum support MTU
 * @NFP_NET_CFG_START_TXQ:   Start Queue Control Queue to use for TX (PF only)
 * @NFP_NET_CFG_START_RXQ:   Start Queue Control Queue to use for RX (PF only)
 *
 * TODO:
 * - define more STS bits
 */
#define NFP_NET_CFG_VERSION             0x0030
#define   NFP_NET_CFG_VERSION_RESERVED_MASK	(0xff << 24)
#define   NFP_NET_CFG_VERSION_CLASS_MASK  (0xff << 16)
#define   NFP_NET_CFG_VERSION_CLASS(x)    (((x) & 0xff) << 16)
#define   NFP_NET_CFG_VERSION_CLASS_GENERIC	0
#define   NFP_NET_CFG_VERSION_MAJOR_MASK  (0xff <<  8)
#define   NFP_NET_CFG_VERSION_MAJOR(x)    (((x) & 0xff) <<  8)
#define   NFP_NET_CFG_VERSION_MINOR_MASK  (0xff <<  0)
#define   NFP_NET_CFG_VERSION_MINOR(x)    (((x) & 0xff) <<  0)
#define NFP_NET_CFG_STS                 0x0034
#define   NFP_NET_CFG_STS_LINK            (0x1 << 0) /* Link up or down */
#define NFP_NET_CFG_CAP                 0x0038
#define NFP_NET_CFG_MAX_TXRINGS         0x003c
#define NFP_NET_CFG_MAX_RXRINGS         0x0040
#define NFP_NET_CFG_MAX_MTU             0x0044
/* Next two words are being used by VFs for solving THB350 issue */
#define NFP_NET_CFG_START_TXQ           0x0048
#define NFP_NET_CFG_START_RXQ           0x004c

/**
 * Prepend configuration
 */
#define NFP_NET_CFG_RX_OFFSET		0x0050
#define NFP_NET_CFG_RX_OFFSET_DYNAMIC		0	/* Prepend mode */

/**
 * VXLAN/UDP encap configuration
 * @NFP_NET_CFG_VXLAN_PORT:	Base address of table of tunnels' UDP dst ports
 * @NFP_NET_CFG_VXLAN_SZ:	Size of the UDP port table in bytes
 */
#define NFP_NET_CFG_VXLAN_PORT		0x0060
#define NFP_NET_CFG_VXLAN_SZ		  0x0008

/**
 * BPF section
 * @NFP_NET_CFG_BPF_ABI:	BPF ABI version
 * @NFP_NET_CFG_BPF_CAP:	BPF capabilities
 * @NFP_NET_CFG_BPF_MAX_LEN:	Maximum size of JITed BPF code in bytes
 * @NFP_NET_CFG_BPF_START:	Offset at which BPF will be loaded
 * @NFP_NET_CFG_BPF_DONE:	Offset to jump to on exit
 * @NFP_NET_CFG_BPF_STACK_SZ:	Total size of stack area in 64B chunks
 * @NFP_NET_CFG_BPF_INL_MTU:	Packet data split offset in 64B chunks
 * @NFP_NET_CFG_BPF_SIZE:	Size of the JITed BPF code in instructions
 * @NFP_NET_CFG_BPF_ADDR:	DMA address of the buffer with JITed BPF code
 */
#define NFP_NET_CFG_BPF_ABI		0x0080
#define   NFP_NET_BPF_ABI		1
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
#define NFP_NET_CFG_RESERVED            0x0098
#define NFP_NET_CFG_RESERVED_SZ         0x0028

/**
 * RSS configuration (0x0100 - 0x01ac):
 * Used only when NFP_NET_CFG_CTRL_RSS is enabled
 * @NFP_NET_CFG_RSS_CFG:     RSS configuration word
 * @NFP_NET_CFG_RSS_KEY:     RSS "secret" key
 * @NFP_NET_CFG_RSS_ITBL:    RSS indirection table
 */
#define NFP_NET_CFG_RSS_BASE            0x0100
#define NFP_NET_CFG_RSS_CTRL            NFP_NET_CFG_RSS_BASE
#define   NFP_NET_CFG_RSS_MASK            (0x7f)
#define   NFP_NET_CFG_RSS_MASK_of(_x)     ((_x) & 0x7f)
#define   NFP_NET_CFG_RSS_IPV4            (1 <<  8) /* RSS for IPv4 */
#define   NFP_NET_CFG_RSS_IPV6            (1 <<  9) /* RSS for IPv6 */
#define   NFP_NET_CFG_RSS_IPV4_TCP        (1 << 10) /* RSS for IPv4/TCP */
#define   NFP_NET_CFG_RSS_IPV4_UDP        (1 << 11) /* RSS for IPv4/UDP */
#define   NFP_NET_CFG_RSS_IPV6_TCP        (1 << 12) /* RSS for IPv6/TCP */
#define   NFP_NET_CFG_RSS_IPV6_UDP        (1 << 13) /* RSS for IPv6/UDP */
#define   NFP_NET_CFG_RSS_TOEPLITZ        (1 << 24) /* Use Toeplitz hash */
#define NFP_NET_CFG_RSS_KEY             (NFP_NET_CFG_RSS_BASE + 0x4)
#define NFP_NET_CFG_RSS_KEY_SZ          0x28
#define NFP_NET_CFG_RSS_ITBL            (NFP_NET_CFG_RSS_BASE + 0x4 + \
					 NFP_NET_CFG_RSS_KEY_SZ)
#define NFP_NET_CFG_RSS_ITBL_SZ         0x80

/**
 * TX ring configuration (0x200 - 0x800)
 * @NFP_NET_CFG_TXR_BASE:    Base offset for TX ring configuration
 * @NFP_NET_CFG_TXR_ADDR:    Per TX ring DMA address (8B entries)
 * @NFP_NET_CFG_TXR_WB_ADDR: Per TX ring write back DMA address (8B entries)
 * @NFP_NET_CFG_TXR_SZ:      Per TX ring ring size (1B entries)
 * @NFP_NET_CFG_TXR_VEC:     Per TX ring MSI-X table entry (1B entries)
 * @NFP_NET_CFG_TXR_PRIO:    Per TX ring priority (1B entries)
 * @NFP_NET_CFG_TXR_IRQ_MOD: Per TX ring interrupt moderation packet
 */
#define NFP_NET_CFG_TXR_BASE            0x0200
#define NFP_NET_CFG_TXR_ADDR(_x)        (NFP_NET_CFG_TXR_BASE + ((_x) * 0x8))
#define NFP_NET_CFG_TXR_WB_ADDR(_x)     (NFP_NET_CFG_TXR_BASE + 0x200 + \
					 ((_x) * 0x8))
#define NFP_NET_CFG_TXR_SZ(_x)          (NFP_NET_CFG_TXR_BASE + 0x400 + (_x))
#define NFP_NET_CFG_TXR_VEC(_x)         (NFP_NET_CFG_TXR_BASE + 0x440 + (_x))
#define NFP_NET_CFG_TXR_PRIO(_x)        (NFP_NET_CFG_TXR_BASE + 0x480 + (_x))
#define NFP_NET_CFG_TXR_IRQ_MOD(_x)	(NFP_NET_CFG_TXR_BASE + 0x500 + \
					 ((_x) * 0x4))

/**
 * RX ring configuration (0x0800 - 0x0c00)
 * @NFP_NET_CFG_RXR_BASE:    Base offset for RX ring configuration
 * @NFP_NET_CFG_RXR_ADDR:    Per RX ring DMA address (8B entries)
 * @NFP_NET_CFG_RXR_SZ:      Per RX ring ring size (1B entries)
 * @NFP_NET_CFG_RXR_VEC:     Per RX ring MSI-X table entry (1B entries)
 * @NFP_NET_CFG_RXR_PRIO:    Per RX ring priority (1B entries)
 * @NFP_NET_CFG_RXR_IRQ_MOD: Per RX ring interrupt moderation (4B entries)
 */
#define NFP_NET_CFG_RXR_BASE            0x0800
#define NFP_NET_CFG_RXR_ADDR(_x)        (NFP_NET_CFG_RXR_BASE + ((_x) * 0x8))
#define NFP_NET_CFG_RXR_SZ(_x)          (NFP_NET_CFG_RXR_BASE + 0x200 + (_x))
#define NFP_NET_CFG_RXR_VEC(_x)         (NFP_NET_CFG_RXR_BASE + 0x240 + (_x))
#define NFP_NET_CFG_RXR_PRIO(_x)        (NFP_NET_CFG_RXR_BASE + 0x280 + (_x))
#define NFP_NET_CFG_RXR_IRQ_MOD(_x)	(NFP_NET_CFG_RXR_BASE + 0x300 + \
					 ((_x) * 0x4))

/**
 * Interrupt Control/Cause registers (0x0c00 - 0x0d00)
 * These registers are only used when MSI-X auto-masking is not
 * enabled (@NFP_NET_CFG_CTRL_MSIXAUTO not set).  The array is index
 * by MSI-X entry and are 1B in size.  If an entry is zero, the
 * corresponding entry is enabled.  If the FW generates an interrupt,
 * it writes a cause into the corresponding field.  This also masks
 * the MSI-X entry and the host driver must clear the register to
 * re-enable the interrupt.
 */
#define NFP_NET_CFG_ICR_BASE            0x0c00
#define NFP_NET_CFG_ICR(_x)             (NFP_NET_CFG_ICR_BASE + (_x))
#define   NFP_NET_CFG_ICR_UNMASKED      0x0
#define   NFP_NET_CFG_ICR_RXTX          0x1
#define   NFP_NET_CFG_ICR_LSC           0x2

/**
 * General device stats (0x0d00 - 0x0d90)
 * all counters are 64bit.
 */
#define NFP_NET_CFG_STATS_BASE          0x0d00
#define NFP_NET_CFG_STATS_RX_DISCARDS   (NFP_NET_CFG_STATS_BASE + 0x00)
#define NFP_NET_CFG_STATS_RX_ERRORS     (NFP_NET_CFG_STATS_BASE + 0x08)
#define NFP_NET_CFG_STATS_RX_OCTETS     (NFP_NET_CFG_STATS_BASE + 0x10)
#define NFP_NET_CFG_STATS_RX_UC_OCTETS  (NFP_NET_CFG_STATS_BASE + 0x18)
#define NFP_NET_CFG_STATS_RX_MC_OCTETS  (NFP_NET_CFG_STATS_BASE + 0x20)
#define NFP_NET_CFG_STATS_RX_BC_OCTETS  (NFP_NET_CFG_STATS_BASE + 0x28)
#define NFP_NET_CFG_STATS_RX_FRAMES     (NFP_NET_CFG_STATS_BASE + 0x30)
#define NFP_NET_CFG_STATS_RX_MC_FRAMES  (NFP_NET_CFG_STATS_BASE + 0x38)
#define NFP_NET_CFG_STATS_RX_BC_FRAMES  (NFP_NET_CFG_STATS_BASE + 0x40)

#define NFP_NET_CFG_STATS_TX_DISCARDS   (NFP_NET_CFG_STATS_BASE + 0x48)
#define NFP_NET_CFG_STATS_TX_ERRORS     (NFP_NET_CFG_STATS_BASE + 0x50)
#define NFP_NET_CFG_STATS_TX_OCTETS     (NFP_NET_CFG_STATS_BASE + 0x58)
#define NFP_NET_CFG_STATS_TX_UC_OCTETS  (NFP_NET_CFG_STATS_BASE + 0x60)
#define NFP_NET_CFG_STATS_TX_MC_OCTETS  (NFP_NET_CFG_STATS_BASE + 0x68)
#define NFP_NET_CFG_STATS_TX_BC_OCTETS  (NFP_NET_CFG_STATS_BASE + 0x70)
#define NFP_NET_CFG_STATS_TX_FRAMES     (NFP_NET_CFG_STATS_BASE + 0x78)
#define NFP_NET_CFG_STATS_TX_MC_FRAMES  (NFP_NET_CFG_STATS_BASE + 0x80)
#define NFP_NET_CFG_STATS_TX_BC_FRAMES  (NFP_NET_CFG_STATS_BASE + 0x88)

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
 * @NFP_NET_CFG_TXR_STATS:   TX ring statistics (Packet and Byte count)
 * @NFP_NET_CFG_RXR_STATS:   RX ring statistics (Packet and Byte count)
 */
#define NFP_NET_CFG_TXR_STATS_BASE      0x1000
#define NFP_NET_CFG_TXR_STATS(_x)       (NFP_NET_CFG_TXR_STATS_BASE + \
					 ((_x) * 0x10))
#define NFP_NET_CFG_RXR_STATS_BASE      0x1400
#define NFP_NET_CFG_RXR_STATS(_x)       (NFP_NET_CFG_RXR_STATS_BASE + \
					 ((_x) * 0x10))

#endif /* _NFP_NET_CTRL_H_ */
