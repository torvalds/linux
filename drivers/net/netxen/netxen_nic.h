/*
 * Copyright (C) 2003 - 2009 NetXen, Inc.
 * Copyright (C) 2009 - QLogic Corporation.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston,
 * MA  02111-1307, USA.
 *
 * The full GNU General Public License is included in this distribution
 * in the file called "COPYING".
 *
 */

#ifndef _NETXEN_NIC_H_
#define _NETXEN_NIC_H_

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ip.h>
#include <linux/in.h>
#include <linux/tcp.h>
#include <linux/skbuff.h>
#include <linux/firmware.h>

#include <linux/ethtool.h>
#include <linux/mii.h>
#include <linux/timer.h>

#include <linux/vmalloc.h>

#include <asm/io.h>
#include <asm/byteorder.h>

#include "netxen_nic_hdr.h"
#include "netxen_nic_hw.h"

#define _NETXEN_NIC_LINUX_MAJOR 4
#define _NETXEN_NIC_LINUX_MINOR 0
#define _NETXEN_NIC_LINUX_SUBVERSION 73
#define NETXEN_NIC_LINUX_VERSIONID  "4.0.73"

#define NETXEN_VERSION_CODE(a, b, c)	(((a) << 24) + ((b) << 16) + (c))
#define _major(v)	(((v) >> 24) & 0xff)
#define _minor(v)	(((v) >> 16) & 0xff)
#define _build(v)	((v) & 0xffff)

/* version in image has weird encoding:
 *  7:0  - major
 * 15:8  - minor
 * 31:16 - build (little endian)
 */
#define NETXEN_DECODE_VERSION(v) \
	NETXEN_VERSION_CODE(((v) & 0xff), (((v) >> 8) & 0xff), ((v) >> 16))

#define NETXEN_NUM_FLASH_SECTORS (64)
#define NETXEN_FLASH_SECTOR_SIZE (64 * 1024)
#define NETXEN_FLASH_TOTAL_SIZE  (NETXEN_NUM_FLASH_SECTORS \
					* NETXEN_FLASH_SECTOR_SIZE)

#define RCV_DESC_RINGSIZE(rds_ring)	\
	(sizeof(struct rcv_desc) * (rds_ring)->num_desc)
#define RCV_BUFF_RINGSIZE(rds_ring)	\
	(sizeof(struct netxen_rx_buffer) * rds_ring->num_desc)
#define STATUS_DESC_RINGSIZE(sds_ring)	\
	(sizeof(struct status_desc) * (sds_ring)->num_desc)
#define TX_BUFF_RINGSIZE(tx_ring)	\
	(sizeof(struct netxen_cmd_buffer) * tx_ring->num_desc)
#define TX_DESC_RINGSIZE(tx_ring)	\
	(sizeof(struct cmd_desc_type0) * tx_ring->num_desc)

#define find_diff_among(a,b,range) ((a)<(b)?((b)-(a)):((b)+(range)-(a)))

#define NETXEN_RCV_PRODUCER_OFFSET	0
#define NETXEN_RCV_PEG_DB_ID		2
#define NETXEN_HOST_DUMMY_DMA_SIZE 1024
#define FLASH_SUCCESS 0

#define ADDR_IN_WINDOW1(off)	\
	((off > NETXEN_CRB_PCIX_HOST2) && (off < NETXEN_CRB_MAX)) ? 1 : 0

/*
 * normalize a 64MB crb address to 32MB PCI window
 * To use NETXEN_CRB_NORMALIZE, window _must_ be set to 1
 */
#define NETXEN_CRB_NORMAL(reg)	\
	((reg) - NETXEN_CRB_PCIX_HOST2 + NETXEN_CRB_PCIX_HOST)

#define NETXEN_CRB_NORMALIZE(adapter, reg) \
	pci_base_offset(adapter, NETXEN_CRB_NORMAL(reg))

#define DB_NORMALIZE(adapter, off) \
	(adapter->ahw.db_base + (off))

#define NX_P2_C0		0x24
#define NX_P2_C1		0x25
#define NX_P3_A0		0x30
#define NX_P3_A2		0x30
#define NX_P3_B0		0x40
#define NX_P3_B1		0x41
#define NX_P3_B2		0x42
#define NX_P3P_A0		0x50

#define NX_IS_REVISION_P2(REVISION)     (REVISION <= NX_P2_C1)
#define NX_IS_REVISION_P3(REVISION)     (REVISION >= NX_P3_A0)
#define NX_IS_REVISION_P3P(REVISION)     (REVISION >= NX_P3P_A0)

#define FIRST_PAGE_GROUP_START	0
#define FIRST_PAGE_GROUP_END	0x100000

#define SECOND_PAGE_GROUP_START	0x6000000
#define SECOND_PAGE_GROUP_END	0x68BC000

#define THIRD_PAGE_GROUP_START	0x70E4000
#define THIRD_PAGE_GROUP_END	0x8000000

#define FIRST_PAGE_GROUP_SIZE  FIRST_PAGE_GROUP_END - FIRST_PAGE_GROUP_START
#define SECOND_PAGE_GROUP_SIZE SECOND_PAGE_GROUP_END - SECOND_PAGE_GROUP_START
#define THIRD_PAGE_GROUP_SIZE  THIRD_PAGE_GROUP_END - THIRD_PAGE_GROUP_START

#define P2_MAX_MTU                     (8000)
#define P3_MAX_MTU                     (9600)
#define NX_ETHERMTU                    1500
#define NX_MAX_ETHERHDR                32 /* This contains some padding */

#define NX_P2_RX_BUF_MAX_LEN           1760
#define NX_P3_RX_BUF_MAX_LEN           (NX_MAX_ETHERHDR + NX_ETHERMTU)
#define NX_P2_RX_JUMBO_BUF_MAX_LEN     (NX_MAX_ETHERHDR + P2_MAX_MTU)
#define NX_P3_RX_JUMBO_BUF_MAX_LEN     (NX_MAX_ETHERHDR + P3_MAX_MTU)
#define NX_CT_DEFAULT_RX_BUF_LEN	2048
#define NX_LRO_BUFFER_EXTRA		2048

#define NX_RX_LRO_BUFFER_LENGTH		(8060)

/*
 * Maximum number of ring contexts
 */
#define MAX_RING_CTX 1

/* Opcodes to be used with the commands */
#define TX_ETHER_PKT	0x01
#define TX_TCP_PKT	0x02
#define TX_UDP_PKT	0x03
#define TX_IP_PKT	0x04
#define TX_TCP_LSO	0x05
#define TX_TCP_LSO6	0x06
#define TX_IPSEC	0x07
#define TX_IPSEC_CMD	0x0a
#define TX_TCPV6_PKT	0x0b
#define TX_UDPV6_PKT	0x0c

/* The following opcodes are for internal consumption. */
#define NETXEN_CONTROL_OP	0x10
#define PEGNET_REQUEST		0x11

#define	MAX_NUM_CARDS		4

#define MAX_BUFFERS_PER_CMD	32
#define TX_STOP_THRESH		((MAX_SKB_FRAGS >> 2) + 4)
#define NX_MAX_TX_TIMEOUTS	2

/*
 * Following are the states of the Phantom. Phantom will set them and
 * Host will read to check if the fields are correct.
 */
#define PHAN_INITIALIZE_START		0xff00
#define PHAN_INITIALIZE_FAILED		0xffff
#define PHAN_INITIALIZE_COMPLETE	0xff01

/* Host writes the following to notify that it has done the init-handshake */
#define PHAN_INITIALIZE_ACK	0xf00f

#define NUM_RCV_DESC_RINGS	3
#define NUM_STS_DESC_RINGS	4

#define RCV_RING_NORMAL	0
#define RCV_RING_JUMBO	1
#define RCV_RING_LRO	2

#define MIN_CMD_DESCRIPTORS		64
#define MIN_RCV_DESCRIPTORS		64
#define MIN_JUMBO_DESCRIPTORS		32

#define MAX_CMD_DESCRIPTORS		1024
#define MAX_RCV_DESCRIPTORS_1G		4096
#define MAX_RCV_DESCRIPTORS_10G		8192
#define MAX_JUMBO_RCV_DESCRIPTORS_1G	512
#define MAX_JUMBO_RCV_DESCRIPTORS_10G	1024
#define MAX_LRO_RCV_DESCRIPTORS		8

#define DEFAULT_RCV_DESCRIPTORS_1G	2048
#define DEFAULT_RCV_DESCRIPTORS_10G	4096

#define NETXEN_CTX_SIGNATURE	0xdee0
#define NETXEN_CTX_SIGNATURE_V2	0x0002dee0
#define NETXEN_CTX_RESET	0xbad0
#define NETXEN_CTX_D3_RESET	0xacc0
#define NETXEN_RCV_PRODUCER(ringid)	(ringid)

#define PHAN_PEG_RCV_INITIALIZED	0xff01
#define PHAN_PEG_RCV_START_INITIALIZE	0xff00

#define get_next_index(index, length)	\
	(((index) + 1) & ((length) - 1))

#define get_index_range(index,length,count)	\
	(((index) + (count)) & ((length) - 1))

#define MPORT_SINGLE_FUNCTION_MODE 0x1111
#define MPORT_MULTI_FUNCTION_MODE 0x2222

#define NX_MAX_PCI_FUNC		8

/*
 * NetXen host-peg signal message structure
 *
 *	Bit 0-1		: peg_id => 0x2 for tx and 01 for rx
 *	Bit 2		: priv_id => must be 1
 *	Bit 3-17	: count => for doorbell
 *	Bit 18-27	: ctx_id => Context id
 *	Bit 28-31	: opcode
 */

typedef u32 netxen_ctx_msg;

#define netxen_set_msg_peg_id(config_word, val)	\
	((config_word) &= ~3, (config_word) |= val & 3)
#define netxen_set_msg_privid(config_word)	\
	((config_word) |= 1 << 2)
#define netxen_set_msg_count(config_word, val)	\
	((config_word) &= ~(0x7fff<<3), (config_word) |= (val & 0x7fff) << 3)
#define netxen_set_msg_ctxid(config_word, val)	\
	((config_word) &= ~(0x3ff<<18), (config_word) |= (val & 0x3ff) << 18)
#define netxen_set_msg_opcode(config_word, val)	\
	((config_word) &= ~(0xf<<28), (config_word) |= (val & 0xf) << 28)

struct netxen_rcv_ring {
	__le64 addr;
	__le32 size;
	__le32 rsrvd;
};

struct netxen_sts_ring {
	__le64 addr;
	__le32 size;
	__le16 msi_index;
	__le16 rsvd;
} ;

struct netxen_ring_ctx {

	/* one command ring */
	__le64 cmd_consumer_offset;
	__le64 cmd_ring_addr;
	__le32 cmd_ring_size;
	__le32 rsrvd;

	/* three receive rings */
	struct netxen_rcv_ring rcv_rings[NUM_RCV_DESC_RINGS];

	__le64 sts_ring_addr;
	__le32 sts_ring_size;

	__le32 ctx_id;

	__le64 rsrvd_2[3];
	__le32 sts_ring_count;
	__le32 rsrvd_3;
	struct netxen_sts_ring sts_rings[NUM_STS_DESC_RINGS];

} __attribute__ ((aligned(64)));

/*
 * Following data structures describe the descriptors that will be used.
 * Added fileds of tcpHdrSize and ipHdrSize, The driver needs to do it only when
 * we are doing LSO (above the 1500 size packet) only.
 */

/*
 * The size of reference handle been changed to 16 bits to pass the MSS fields
 * for the LSO packet
 */

#define FLAGS_CHECKSUM_ENABLED	0x01
#define FLAGS_LSO_ENABLED	0x02
#define FLAGS_IPSEC_SA_ADD	0x04
#define FLAGS_IPSEC_SA_DELETE	0x08
#define FLAGS_VLAN_TAGGED	0x10
#define FLAGS_VLAN_OOB		0x40

#define netxen_set_tx_vlan_tci(cmd_desc, v)	\
	(cmd_desc)->vlan_TCI = cpu_to_le16(v);

#define netxen_set_cmd_desc_port(cmd_desc, var)	\
	((cmd_desc)->port_ctxid |= ((var) & 0x0F))
#define netxen_set_cmd_desc_ctxid(cmd_desc, var)	\
	((cmd_desc)->port_ctxid |= ((var) << 4 & 0xF0))

#define netxen_set_tx_port(_desc, _port) \
	(_desc)->port_ctxid = ((_port) & 0xf) | (((_port) << 4) & 0xf0)

#define netxen_set_tx_flags_opcode(_desc, _flags, _opcode) \
	(_desc)->flags_opcode = \
	cpu_to_le16(((_flags) & 0x7f) | (((_opcode) & 0x3f) << 7))

#define netxen_set_tx_frags_len(_desc, _frags, _len) \
	(_desc)->nfrags__length = \
	cpu_to_le32(((_frags) & 0xff) | (((_len) & 0xffffff) << 8))

struct cmd_desc_type0 {
	u8 tcp_hdr_offset;	/* For LSO only */
	u8 ip_hdr_offset;	/* For LSO only */
	__le16 flags_opcode;	/* 15:13 unused, 12:7 opcode, 6:0 flags */
	__le32 nfrags__length;	/* 31:8 total len, 7:0 frag count */

	__le64 addr_buffer2;

	__le16 reference_handle;
	__le16 mss;
	u8 port_ctxid;		/* 7:4 ctxid 3:0 port */
	u8 total_hdr_length;	/* LSO only : MAC+IP+TCP Hdr size */
	__le16 conn_id;		/* IPSec offoad only */

	__le64 addr_buffer3;
	__le64 addr_buffer1;

	__le16 buffer_length[4];

	__le64 addr_buffer4;

	__le32 reserved2;
	__le16 reserved;
	__le16 vlan_TCI;

} __attribute__ ((aligned(64)));

/* Note: sizeof(rcv_desc) should always be a mutliple of 2 */
struct rcv_desc {
	__le16 reference_handle;
	__le16 reserved;
	__le32 buffer_length;	/* allocated buffer length (usually 2K) */
	__le64 addr_buffer;
};

/* opcode field in status_desc */
#define NETXEN_NIC_SYN_OFFLOAD  0x03
#define NETXEN_NIC_RXPKT_DESC  0x04
#define NETXEN_OLD_RXPKT_DESC  0x3f
#define NETXEN_NIC_RESPONSE_DESC 0x05
#define NETXEN_NIC_LRO_DESC  	0x12

/* for status field in status_desc */
#define STATUS_NEED_CKSUM	(1)
#define STATUS_CKSUM_OK		(2)

/* owner bits of status_desc */
#define STATUS_OWNER_HOST	(0x1ULL << 56)
#define STATUS_OWNER_PHANTOM	(0x2ULL << 56)

/* Status descriptor:
   0-3 port, 4-7 status, 8-11 type, 12-27 total_length
   28-43 reference_handle, 44-47 protocol, 48-52 pkt_offset
   53-55 desc_cnt, 56-57 owner, 58-63 opcode
 */
#define netxen_get_sts_port(sts_data)	\
	((sts_data) & 0x0F)
#define netxen_get_sts_status(sts_data)	\
	(((sts_data) >> 4) & 0x0F)
#define netxen_get_sts_type(sts_data)	\
	(((sts_data) >> 8) & 0x0F)
#define netxen_get_sts_totallength(sts_data)	\
	(((sts_data) >> 12) & 0xFFFF)
#define netxen_get_sts_refhandle(sts_data)	\
	(((sts_data) >> 28) & 0xFFFF)
#define netxen_get_sts_prot(sts_data)	\
	(((sts_data) >> 44) & 0x0F)
#define netxen_get_sts_pkt_offset(sts_data)	\
	(((sts_data) >> 48) & 0x1F)
#define netxen_get_sts_desc_cnt(sts_data)	\
	(((sts_data) >> 53) & 0x7)
#define netxen_get_sts_opcode(sts_data)	\
	(((sts_data) >> 58) & 0x03F)

#define netxen_get_lro_sts_refhandle(sts_data) 	\
	((sts_data) & 0x0FFFF)
#define netxen_get_lro_sts_length(sts_data)	\
	(((sts_data) >> 16) & 0x0FFFF)
#define netxen_get_lro_sts_l2_hdr_offset(sts_data)	\
	(((sts_data) >> 32) & 0x0FF)
#define netxen_get_lro_sts_l4_hdr_offset(sts_data)	\
	(((sts_data) >> 40) & 0x0FF)
#define netxen_get_lro_sts_timestamp(sts_data)	\
	(((sts_data) >> 48) & 0x1)
#define netxen_get_lro_sts_type(sts_data)	\
	(((sts_data) >> 49) & 0x7)
#define netxen_get_lro_sts_push_flag(sts_data)		\
	(((sts_data) >> 52) & 0x1)
#define netxen_get_lro_sts_seq_number(sts_data)		\
	((sts_data) & 0x0FFFFFFFF)


struct status_desc {
	__le64 status_desc_data[2];
} __attribute__ ((aligned(16)));

/* UNIFIED ROMIMAGE *************************/
#define NX_UNI_FW_MIN_SIZE		0xc8000
#define NX_UNI_DIR_SECT_PRODUCT_TBL	0x0
#define NX_UNI_DIR_SECT_BOOTLD		0x6
#define NX_UNI_DIR_SECT_FW		0x7

/*Offsets */
#define NX_UNI_CHIP_REV_OFF		10
#define NX_UNI_FLAGS_OFF		11
#define NX_UNI_BIOS_VERSION_OFF 	12
#define NX_UNI_BOOTLD_IDX_OFF		27
#define NX_UNI_FIRMWARE_IDX_OFF 	29

struct uni_table_desc{
	uint32_t	findex;
	uint32_t	num_entries;
	uint32_t	entry_size;
	uint32_t	reserved[5];
};

struct uni_data_desc{
	uint32_t	findex;
	uint32_t	size;
	uint32_t	reserved[5];
};

/* UNIFIED ROMIMAGE *************************/

/* The version of the main data structure */
#define	NETXEN_BDINFO_VERSION 1

/* Magic number to let user know flash is programmed */
#define	NETXEN_BDINFO_MAGIC 0x12345678

/* Max number of Gig ports on a Phantom board */
#define NETXEN_MAX_PORTS 4

#define NETXEN_BRDTYPE_P1_BD		0x0000
#define NETXEN_BRDTYPE_P1_SB		0x0001
#define NETXEN_BRDTYPE_P1_SMAX		0x0002
#define NETXEN_BRDTYPE_P1_SOCK		0x0003

#define NETXEN_BRDTYPE_P2_SOCK_31	0x0008
#define NETXEN_BRDTYPE_P2_SOCK_35	0x0009
#define NETXEN_BRDTYPE_P2_SB35_4G	0x000a
#define NETXEN_BRDTYPE_P2_SB31_10G	0x000b
#define NETXEN_BRDTYPE_P2_SB31_2G	0x000c

#define NETXEN_BRDTYPE_P2_SB31_10G_IMEZ		0x000d
#define NETXEN_BRDTYPE_P2_SB31_10G_HMEZ		0x000e
#define NETXEN_BRDTYPE_P2_SB31_10G_CX4		0x000f

#define NETXEN_BRDTYPE_P3_REF_QG	0x0021
#define NETXEN_BRDTYPE_P3_HMEZ		0x0022
#define NETXEN_BRDTYPE_P3_10G_CX4_LP	0x0023
#define NETXEN_BRDTYPE_P3_4_GB		0x0024
#define NETXEN_BRDTYPE_P3_IMEZ		0x0025
#define NETXEN_BRDTYPE_P3_10G_SFP_PLUS	0x0026
#define NETXEN_BRDTYPE_P3_10000_BASE_T	0x0027
#define NETXEN_BRDTYPE_P3_XG_LOM	0x0028
#define NETXEN_BRDTYPE_P3_4_GB_MM	0x0029
#define NETXEN_BRDTYPE_P3_10G_SFP_CT	0x002a
#define NETXEN_BRDTYPE_P3_10G_SFP_QT	0x002b
#define NETXEN_BRDTYPE_P3_10G_CX4	0x0031
#define NETXEN_BRDTYPE_P3_10G_XFP	0x0032
#define NETXEN_BRDTYPE_P3_10G_TP	0x0080

/* Flash memory map */
#define NETXEN_CRBINIT_START	0	/* crbinit section */
#define NETXEN_BRDCFG_START	0x4000	/* board config */
#define NETXEN_INITCODE_START	0x6000	/* pegtune code */
#define NETXEN_BOOTLD_START	0x10000	/* bootld */
#define NETXEN_IMAGE_START	0x43000	/* compressed image */
#define NETXEN_SECONDARY_START	0x200000	/* backup images */
#define NETXEN_PXE_START	0x3E0000	/* PXE boot rom */
#define NETXEN_USER_START	0x3E8000	/* Firmare info */
#define NETXEN_FIXED_START	0x3F0000	/* backup of crbinit */
#define NETXEN_USER_START_OLD	NETXEN_PXE_START /* very old flash */

#define NX_OLD_MAC_ADDR_OFFSET	(NETXEN_USER_START)
#define NX_FW_VERSION_OFFSET	(NETXEN_USER_START+0x408)
#define NX_FW_SIZE_OFFSET	(NETXEN_USER_START+0x40c)
#define NX_FW_MAC_ADDR_OFFSET	(NETXEN_USER_START+0x418)
#define NX_FW_SERIAL_NUM_OFFSET	(NETXEN_USER_START+0x81c)
#define NX_BIOS_VERSION_OFFSET	(NETXEN_USER_START+0x83c)

#define NX_HDR_VERSION_OFFSET	(NETXEN_BRDCFG_START)
#define NX_BRDTYPE_OFFSET	(NETXEN_BRDCFG_START+0x8)
#define NX_FW_MAGIC_OFFSET	(NETXEN_BRDCFG_START+0x128)

#define NX_FW_MIN_SIZE		(0x3fffff)
#define NX_P2_MN_ROMIMAGE	0
#define NX_P3_CT_ROMIMAGE	1
#define NX_P3_MN_ROMIMAGE	2
#define NX_UNIFIED_ROMIMAGE	3
#define NX_FLASH_ROMIMAGE	4
#define NX_UNKNOWN_ROMIMAGE	0xff

#define NX_P2_MN_ROMIMAGE_NAME		"nxromimg.bin"
#define NX_P3_CT_ROMIMAGE_NAME		"nx3fwct.bin"
#define NX_P3_MN_ROMIMAGE_NAME		"nx3fwmn.bin"
#define NX_UNIFIED_ROMIMAGE_NAME	"phanfw.bin"
#define NX_FLASH_ROMIMAGE_NAME		"flash"

extern char netxen_nic_driver_name[];

/* Number of status descriptors to handle per interrupt */
#define MAX_STATUS_HANDLE	(64)

/*
 * netxen_skb_frag{} is to contain mapping info for each SG list. This
 * has to be freed when DMA is complete. This is part of netxen_tx_buffer{}.
 */
struct netxen_skb_frag {
	u64 dma;
	u64 length;
};

struct netxen_recv_crb {
	u32 crb_rcv_producer[NUM_RCV_DESC_RINGS];
	u32 crb_sts_consumer[NUM_STS_DESC_RINGS];
	u32 sw_int_mask[NUM_STS_DESC_RINGS];
};

/*    Following defines are for the state of the buffers    */
#define	NETXEN_BUFFER_FREE	0
#define	NETXEN_BUFFER_BUSY	1

/*
 * There will be one netxen_buffer per skb packet.    These will be
 * used to save the dma info for pci_unmap_page()
 */
struct netxen_cmd_buffer {
	struct sk_buff *skb;
	struct netxen_skb_frag frag_array[MAX_BUFFERS_PER_CMD + 1];
	u32 frag_count;
};

/* In rx_buffer, we do not need multiple fragments as is a single buffer */
struct netxen_rx_buffer {
	struct list_head list;
	struct sk_buff *skb;
	u64 dma;
	u16 ref_handle;
	u16 state;
};

/* Board types */
#define	NETXEN_NIC_GBE	0x01
#define	NETXEN_NIC_XGBE	0x02

/*
 * One hardware_context{} per adapter
 * contains interrupt info as well shared hardware info.
 */
struct netxen_hardware_context {
	void __iomem *pci_base0;
	void __iomem *pci_base1;
	void __iomem *pci_base2;
	void __iomem *db_base;
	void __iomem *ocm_win_crb;

	unsigned long db_len;
	unsigned long pci_len0;

	u32 ocm_win;
	u32 crb_win;

	rwlock_t crb_lock;
	spinlock_t mem_lock;

	u8 cut_through;
	u8 revision_id;
	u8 pci_func;
	u8 linkup;
	u16 port_type;
	u16 board_type;
};

#define MINIMUM_ETHERNET_FRAME_SIZE	64	/* With FCS */
#define ETHERNET_FCS_SIZE		4

struct netxen_adapter_stats {
	u64  xmitcalled;
	u64  xmitfinished;
	u64  rxdropped;
	u64  txdropped;
	u64  csummed;
	u64  rx_pkts;
	u64  lro_pkts;
	u64  rxbytes;
	u64  txbytes;
};

/*
 * Rcv Descriptor Context. One such per Rcv Descriptor. There may
 * be one Rcv Descriptor for normal packets, one for jumbo and may be others.
 */
struct nx_host_rds_ring {
	u32 producer;
	u32 num_desc;
	u32 dma_size;
	u32 skb_size;
	u32 flags;
	void __iomem *crb_rcv_producer;
	struct rcv_desc *desc_head;
	struct netxen_rx_buffer *rx_buf_arr;
	struct list_head free_list;
	spinlock_t lock;
	dma_addr_t phys_addr;
};

struct nx_host_sds_ring {
	u32 consumer;
	u32 num_desc;
	void __iomem *crb_sts_consumer;
	void __iomem *crb_intr_mask;

	struct status_desc *desc_head;
	struct netxen_adapter *adapter;
	struct napi_struct napi;
	struct list_head free_list[NUM_RCV_DESC_RINGS];

	int irq;

	dma_addr_t phys_addr;
	char name[IFNAMSIZ+4];
};

struct nx_host_tx_ring {
	u32 producer;
	__le32 *hw_consumer;
	u32 sw_consumer;
	void __iomem *crb_cmd_producer;
	void __iomem *crb_cmd_consumer;
	u32 num_desc;

	struct netdev_queue *txq;

	struct netxen_cmd_buffer *cmd_buf_arr;
	struct cmd_desc_type0 *desc_head;
	dma_addr_t phys_addr;
};

/*
 * Receive context. There is one such structure per instance of the
 * receive processing. Any state information that is relevant to
 * the receive, and is must be in this structure. The global data may be
 * present elsewhere.
 */
struct netxen_recv_context {
	u32 state;
	u16 context_id;
	u16 virt_port;

	struct nx_host_rds_ring *rds_rings;
	struct nx_host_sds_ring *sds_rings;

	struct netxen_ring_ctx *hwctx;
	dma_addr_t phys_addr;
};

/* New HW context creation */

#define NX_OS_CRB_RETRY_COUNT	4000
#define NX_CDRP_SIGNATURE_MAKE(pcifn, version) \
	(((pcifn) & 0xff) | (((version) & 0xff) << 8) | (0xcafe << 16))

#define NX_CDRP_CLEAR		0x00000000
#define NX_CDRP_CMD_BIT		0x80000000

/*
 * All responses must have the NX_CDRP_CMD_BIT cleared
 * in the crb NX_CDRP_CRB_OFFSET.
 */
#define NX_CDRP_FORM_RSP(rsp)	(rsp)
#define NX_CDRP_IS_RSP(rsp)	(((rsp) & NX_CDRP_CMD_BIT) == 0)

#define NX_CDRP_RSP_OK		0x00000001
#define NX_CDRP_RSP_FAIL	0x00000002
#define NX_CDRP_RSP_TIMEOUT	0x00000003

/*
 * All commands must have the NX_CDRP_CMD_BIT set in
 * the crb NX_CDRP_CRB_OFFSET.
 */
#define NX_CDRP_FORM_CMD(cmd)	(NX_CDRP_CMD_BIT | (cmd))
#define NX_CDRP_IS_CMD(cmd)	(((cmd) & NX_CDRP_CMD_BIT) != 0)

#define NX_CDRP_CMD_SUBMIT_CAPABILITIES     0x00000001
#define NX_CDRP_CMD_READ_MAX_RDS_PER_CTX    0x00000002
#define NX_CDRP_CMD_READ_MAX_SDS_PER_CTX    0x00000003
#define NX_CDRP_CMD_READ_MAX_RULES_PER_CTX  0x00000004
#define NX_CDRP_CMD_READ_MAX_RX_CTX         0x00000005
#define NX_CDRP_CMD_READ_MAX_TX_CTX         0x00000006
#define NX_CDRP_CMD_CREATE_RX_CTX           0x00000007
#define NX_CDRP_CMD_DESTROY_RX_CTX          0x00000008
#define NX_CDRP_CMD_CREATE_TX_CTX           0x00000009
#define NX_CDRP_CMD_DESTROY_TX_CTX          0x0000000a
#define NX_CDRP_CMD_SETUP_STATISTICS        0x0000000e
#define NX_CDRP_CMD_GET_STATISTICS          0x0000000f
#define NX_CDRP_CMD_DELETE_STATISTICS       0x00000010
#define NX_CDRP_CMD_SET_MTU                 0x00000012
#define NX_CDRP_CMD_READ_PHY			0x00000013
#define NX_CDRP_CMD_WRITE_PHY			0x00000014
#define NX_CDRP_CMD_READ_HW_REG			0x00000015
#define NX_CDRP_CMD_GET_FLOW_CTL		0x00000016
#define NX_CDRP_CMD_SET_FLOW_CTL		0x00000017
#define NX_CDRP_CMD_READ_MAX_MTU		0x00000018
#define NX_CDRP_CMD_READ_MAX_LRO		0x00000019
#define NX_CDRP_CMD_CONFIGURE_TOE		0x0000001a
#define NX_CDRP_CMD_FUNC_ATTRIB			0x0000001b
#define NX_CDRP_CMD_READ_PEXQ_PARAMETERS	0x0000001c
#define NX_CDRP_CMD_GET_LIC_CAPABILITIES	0x0000001d
#define NX_CDRP_CMD_READ_MAX_LRO_PER_BOARD	0x0000001e
#define NX_CDRP_CMD_MAX				0x0000001f

#define NX_RCODE_SUCCESS		0
#define NX_RCODE_NO_HOST_MEM		1
#define NX_RCODE_NO_HOST_RESOURCE	2
#define NX_RCODE_NO_CARD_CRB		3
#define NX_RCODE_NO_CARD_MEM		4
#define NX_RCODE_NO_CARD_RESOURCE	5
#define NX_RCODE_INVALID_ARGS		6
#define NX_RCODE_INVALID_ACTION		7
#define NX_RCODE_INVALID_STATE		8
#define NX_RCODE_NOT_SUPPORTED		9
#define NX_RCODE_NOT_PERMITTED		10
#define NX_RCODE_NOT_READY		11
#define NX_RCODE_DOES_NOT_EXIST		12
#define NX_RCODE_ALREADY_EXISTS		13
#define NX_RCODE_BAD_SIGNATURE		14
#define NX_RCODE_CMD_NOT_IMPL		15
#define NX_RCODE_CMD_INVALID		16
#define NX_RCODE_TIMEOUT		17
#define NX_RCODE_CMD_FAILED		18
#define NX_RCODE_MAX_EXCEEDED		19
#define NX_RCODE_MAX			20

#define NX_DESTROY_CTX_RESET		0
#define NX_DESTROY_CTX_D3_RESET		1
#define NX_DESTROY_CTX_MAX		2

/*
 * Capabilities
 */
#define NX_CAP_BIT(class, bit)		(1 << bit)
#define NX_CAP0_LEGACY_CONTEXT		NX_CAP_BIT(0, 0)
#define NX_CAP0_MULTI_CONTEXT		NX_CAP_BIT(0, 1)
#define NX_CAP0_LEGACY_MN		NX_CAP_BIT(0, 2)
#define NX_CAP0_LEGACY_MS		NX_CAP_BIT(0, 3)
#define NX_CAP0_CUT_THROUGH		NX_CAP_BIT(0, 4)
#define NX_CAP0_LRO			NX_CAP_BIT(0, 5)
#define NX_CAP0_LSO			NX_CAP_BIT(0, 6)
#define NX_CAP0_JUMBO_CONTIGUOUS	NX_CAP_BIT(0, 7)
#define NX_CAP0_LRO_CONTIGUOUS		NX_CAP_BIT(0, 8)
#define NX_CAP0_HW_LRO			NX_CAP_BIT(0, 10)

/*
 * Context state
 */
#define NX_HOST_CTX_STATE_FREED		0
#define NX_HOST_CTX_STATE_ALLOCATED	1
#define NX_HOST_CTX_STATE_ACTIVE	2
#define NX_HOST_CTX_STATE_DISABLED	3
#define NX_HOST_CTX_STATE_QUIESCED	4
#define NX_HOST_CTX_STATE_MAX		5

/*
 * Rx context
 */

typedef struct {
	__le64 host_phys_addr;	/* Ring base addr */
	__le32 ring_size;		/* Ring entries */
	__le16 msi_index;
	__le16 rsvd;		/* Padding */
} nx_hostrq_sds_ring_t;

typedef struct {
	__le64 host_phys_addr;	/* Ring base addr */
	__le64 buff_size;		/* Packet buffer size */
	__le32 ring_size;		/* Ring entries */
	__le32 ring_kind;		/* Class of ring */
} nx_hostrq_rds_ring_t;

typedef struct {
	__le64 host_rsp_dma_addr;	/* Response dma'd here */
	__le32 capabilities[4];	/* Flag bit vector */
	__le32 host_int_crb_mode;	/* Interrupt crb usage */
	__le32 host_rds_crb_mode;	/* RDS crb usage */
	/* These ring offsets are relative to data[0] below */
	__le32 rds_ring_offset;	/* Offset to RDS config */
	__le32 sds_ring_offset;	/* Offset to SDS config */
	__le16 num_rds_rings;	/* Count of RDS rings */
	__le16 num_sds_rings;	/* Count of SDS rings */
	__le16 rsvd1;		/* Padding */
	__le16 rsvd2;		/* Padding */
	u8  reserved[128]; 	/* reserve space for future expansion*/
	/* MUST BE 64-bit aligned.
	   The following is packed:
	   - N hostrq_rds_rings
	   - N hostrq_sds_rings */
	char data[0];
} nx_hostrq_rx_ctx_t;

typedef struct {
	__le32 host_producer_crb;	/* Crb to use */
	__le32 rsvd1;		/* Padding */
} nx_cardrsp_rds_ring_t;

typedef struct {
	__le32 host_consumer_crb;	/* Crb to use */
	__le32 interrupt_crb;	/* Crb to use */
} nx_cardrsp_sds_ring_t;

typedef struct {
	/* These ring offsets are relative to data[0] below */
	__le32 rds_ring_offset;	/* Offset to RDS config */
	__le32 sds_ring_offset;	/* Offset to SDS config */
	__le32 host_ctx_state;	/* Starting State */
	__le32 num_fn_per_port;	/* How many PCI fn share the port */
	__le16 num_rds_rings;	/* Count of RDS rings */
	__le16 num_sds_rings;	/* Count of SDS rings */
	__le16 context_id;		/* Handle for context */
	u8  phys_port;		/* Physical id of port */
	u8  virt_port;		/* Virtual/Logical id of port */
	u8  reserved[128];	/* save space for future expansion */
	/*  MUST BE 64-bit aligned.
	   The following is packed:
	   - N cardrsp_rds_rings
	   - N cardrs_sds_rings */
	char data[0];
} nx_cardrsp_rx_ctx_t;

#define SIZEOF_HOSTRQ_RX(HOSTRQ_RX, rds_rings, sds_rings)	\
	(sizeof(HOSTRQ_RX) + 					\
	(rds_rings)*(sizeof(nx_hostrq_rds_ring_t)) +		\
	(sds_rings)*(sizeof(nx_hostrq_sds_ring_t)))

#define SIZEOF_CARDRSP_RX(CARDRSP_RX, rds_rings, sds_rings) 	\
	(sizeof(CARDRSP_RX) + 					\
	(rds_rings)*(sizeof(nx_cardrsp_rds_ring_t)) + 		\
	(sds_rings)*(sizeof(nx_cardrsp_sds_ring_t)))

/*
 * Tx context
 */

typedef struct {
	__le64 host_phys_addr;	/* Ring base addr */
	__le32 ring_size;		/* Ring entries */
	__le32 rsvd;		/* Padding */
} nx_hostrq_cds_ring_t;

typedef struct {
	__le64 host_rsp_dma_addr;	/* Response dma'd here */
	__le64 cmd_cons_dma_addr;	/*  */
	__le64 dummy_dma_addr;	/*  */
	__le32 capabilities[4];	/* Flag bit vector */
	__le32 host_int_crb_mode;	/* Interrupt crb usage */
	__le32 rsvd1;		/* Padding */
	__le16 rsvd2;		/* Padding */
	__le16 interrupt_ctl;
	__le16 msi_index;
	__le16 rsvd3;		/* Padding */
	nx_hostrq_cds_ring_t cds_ring;	/* Desc of cds ring */
	u8  reserved[128];	/* future expansion */
} nx_hostrq_tx_ctx_t;

typedef struct {
	__le32 host_producer_crb;	/* Crb to use */
	__le32 interrupt_crb;	/* Crb to use */
} nx_cardrsp_cds_ring_t;

typedef struct {
	__le32 host_ctx_state;	/* Starting state */
	__le16 context_id;		/* Handle for context */
	u8  phys_port;		/* Physical id of port */
	u8  virt_port;		/* Virtual/Logical id of port */
	nx_cardrsp_cds_ring_t cds_ring;	/* Card cds settings */
	u8  reserved[128];	/* future expansion */
} nx_cardrsp_tx_ctx_t;

#define SIZEOF_HOSTRQ_TX(HOSTRQ_TX)	(sizeof(HOSTRQ_TX))
#define SIZEOF_CARDRSP_TX(CARDRSP_TX)	(sizeof(CARDRSP_TX))

/* CRB */

#define NX_HOST_RDS_CRB_MODE_UNIQUE	0
#define NX_HOST_RDS_CRB_MODE_SHARED	1
#define NX_HOST_RDS_CRB_MODE_CUSTOM	2
#define NX_HOST_RDS_CRB_MODE_MAX	3

#define NX_HOST_INT_CRB_MODE_UNIQUE	0
#define NX_HOST_INT_CRB_MODE_SHARED	1
#define NX_HOST_INT_CRB_MODE_NORX	2
#define NX_HOST_INT_CRB_MODE_NOTX	3
#define NX_HOST_INT_CRB_MODE_NORXTX	4


/* MAC */

#define MC_COUNT_P2	16
#define MC_COUNT_P3	38

#define NETXEN_MAC_NOOP	0
#define NETXEN_MAC_ADD	1
#define NETXEN_MAC_DEL	2

typedef struct nx_mac_list_s {
	struct list_head list;
	uint8_t mac_addr[ETH_ALEN+2];
} nx_mac_list_t;

/*
 * Interrupt coalescing defaults. The defaults are for 1500 MTU. It is
 * adjusted based on configured MTU.
 */
#define NETXEN_DEFAULT_INTR_COALESCE_RX_TIME_US	3
#define NETXEN_DEFAULT_INTR_COALESCE_RX_PACKETS	256
#define NETXEN_DEFAULT_INTR_COALESCE_TX_PACKETS	64
#define NETXEN_DEFAULT_INTR_COALESCE_TX_TIME_US	4

#define NETXEN_NIC_INTR_DEFAULT			0x04

typedef union {
	struct {
		uint16_t	rx_packets;
		uint16_t	rx_time_us;
		uint16_t	tx_packets;
		uint16_t	tx_time_us;
	} data;
	uint64_t		word;
} nx_nic_intr_coalesce_data_t;

typedef struct {
	uint16_t			stats_time_us;
	uint16_t			rate_sample_time;
	uint16_t			flags;
	uint16_t			rsvd_1;
	uint32_t			low_threshold;
	uint32_t			high_threshold;
	nx_nic_intr_coalesce_data_t	normal;
	nx_nic_intr_coalesce_data_t	low;
	nx_nic_intr_coalesce_data_t	high;
	nx_nic_intr_coalesce_data_t	irq;
} nx_nic_intr_coalesce_t;

#define NX_HOST_REQUEST		0x13
#define NX_NIC_REQUEST		0x14

#define NX_MAC_EVENT		0x1

#define NX_IP_UP		2
#define NX_IP_DOWN		3

/*
 * Driver --> Firmware
 */
#define NX_NIC_H2C_OPCODE_START				0
#define NX_NIC_H2C_OPCODE_CONFIG_RSS			1
#define NX_NIC_H2C_OPCODE_CONFIG_RSS_TBL		2
#define NX_NIC_H2C_OPCODE_CONFIG_INTR_COALESCE		3
#define NX_NIC_H2C_OPCODE_CONFIG_LED			4
#define NX_NIC_H2C_OPCODE_CONFIG_PROMISCUOUS		5
#define NX_NIC_H2C_OPCODE_CONFIG_L2_MAC			6
#define NX_NIC_H2C_OPCODE_LRO_REQUEST			7
#define NX_NIC_H2C_OPCODE_GET_SNMP_STATS		8
#define NX_NIC_H2C_OPCODE_PROXY_START_REQUEST		9
#define NX_NIC_H2C_OPCODE_PROXY_STOP_REQUEST		10
#define NX_NIC_H2C_OPCODE_PROXY_SET_MTU			11
#define NX_NIC_H2C_OPCODE_PROXY_SET_VPORT_MISS_MODE	12
#define NX_NIC_H2C_OPCODE_GET_FINGER_PRINT_REQUEST	13
#define NX_NIC_H2C_OPCODE_INSTALL_LICENSE_REQUEST	14
#define NX_NIC_H2C_OPCODE_GET_LICENSE_CAPABILITY_REQUEST	15
#define NX_NIC_H2C_OPCODE_GET_NET_STATS			16
#define NX_NIC_H2C_OPCODE_PROXY_UPDATE_P2V		17
#define NX_NIC_H2C_OPCODE_CONFIG_IPADDR			18
#define NX_NIC_H2C_OPCODE_CONFIG_LOOPBACK		19
#define NX_NIC_H2C_OPCODE_PROXY_STOP_DONE		20
#define NX_NIC_H2C_OPCODE_GET_LINKEVENT			21
#define NX_NIC_C2C_OPCODE				22
#define NX_NIC_H2C_OPCODE_CONFIG_BRIDGING               23
#define NX_NIC_H2C_OPCODE_CONFIG_HW_LRO			24
#define NX_NIC_H2C_OPCODE_LAST				25

/*
 * Firmware --> Driver
 */

#define NX_NIC_C2H_OPCODE_START				128
#define NX_NIC_C2H_OPCODE_CONFIG_RSS_RESPONSE		129
#define NX_NIC_C2H_OPCODE_CONFIG_RSS_TBL_RESPONSE	130
#define NX_NIC_C2H_OPCODE_CONFIG_MAC_RESPONSE		131
#define NX_NIC_C2H_OPCODE_CONFIG_PROMISCUOUS_RESPONSE	132
#define NX_NIC_C2H_OPCODE_CONFIG_L2_MAC_RESPONSE	133
#define NX_NIC_C2H_OPCODE_LRO_DELETE_RESPONSE		134
#define NX_NIC_C2H_OPCODE_LRO_ADD_FAILURE_RESPONSE	135
#define NX_NIC_C2H_OPCODE_GET_SNMP_STATS		136
#define NX_NIC_C2H_OPCODE_GET_FINGER_PRINT_REPLY	137
#define NX_NIC_C2H_OPCODE_INSTALL_LICENSE_REPLY		138
#define NX_NIC_C2H_OPCODE_GET_LICENSE_CAPABILITIES_REPLY 139
#define NX_NIC_C2H_OPCODE_GET_NET_STATS_RESPONSE	140
#define NX_NIC_C2H_OPCODE_GET_LINKEVENT_RESPONSE	141
#define NX_NIC_C2H_OPCODE_LAST				142

#define VPORT_MISS_MODE_DROP		0 /* drop all unmatched */
#define VPORT_MISS_MODE_ACCEPT_ALL	1 /* accept all packets */
#define VPORT_MISS_MODE_ACCEPT_MULTI	2 /* accept unmatched multicast */

#define NX_NIC_LRO_REQUEST_FIRST		0
#define NX_NIC_LRO_REQUEST_ADD_FLOW		1
#define NX_NIC_LRO_REQUEST_DELETE_FLOW		2
#define NX_NIC_LRO_REQUEST_TIMER		3
#define NX_NIC_LRO_REQUEST_CLEANUP		4
#define NX_NIC_LRO_REQUEST_ADD_FLOW_SCHEDULED	5
#define NX_TOE_LRO_REQUEST_ADD_FLOW		6
#define NX_TOE_LRO_REQUEST_ADD_FLOW_RESPONSE	7
#define NX_TOE_LRO_REQUEST_DELETE_FLOW		8
#define NX_TOE_LRO_REQUEST_DELETE_FLOW_RESPONSE	9
#define NX_TOE_LRO_REQUEST_TIMER		10
#define NX_NIC_LRO_REQUEST_LAST			11

#define NX_FW_CAPABILITY_LINK_NOTIFICATION	(1 << 5)
#define NX_FW_CAPABILITY_SWITCHING		(1 << 6)
#define NX_FW_CAPABILITY_PEXQ			(1 << 7)
#define NX_FW_CAPABILITY_BDG			(1 << 8)
#define NX_FW_CAPABILITY_FVLANTX		(1 << 9)
#define NX_FW_CAPABILITY_HW_LRO			(1 << 10)

/* module types */
#define LINKEVENT_MODULE_NOT_PRESENT			1
#define LINKEVENT_MODULE_OPTICAL_UNKNOWN		2
#define LINKEVENT_MODULE_OPTICAL_SRLR			3
#define LINKEVENT_MODULE_OPTICAL_LRM			4
#define LINKEVENT_MODULE_OPTICAL_SFP_1G			5
#define LINKEVENT_MODULE_TWINAX_UNSUPPORTED_CABLE	6
#define LINKEVENT_MODULE_TWINAX_UNSUPPORTED_CABLELEN	7
#define LINKEVENT_MODULE_TWINAX				8

#define LINKSPEED_10GBPS	10000
#define LINKSPEED_1GBPS		1000
#define LINKSPEED_100MBPS	100
#define LINKSPEED_10MBPS	10

#define LINKSPEED_ENCODED_10MBPS	0
#define LINKSPEED_ENCODED_100MBPS	1
#define LINKSPEED_ENCODED_1GBPS		2

#define LINKEVENT_AUTONEG_DISABLED	0
#define LINKEVENT_AUTONEG_ENABLED	1

#define LINKEVENT_HALF_DUPLEX		0
#define LINKEVENT_FULL_DUPLEX		1

#define LINKEVENT_LINKSPEED_MBPS	0
#define LINKEVENT_LINKSPEED_ENCODED	1

#define AUTO_FW_RESET_ENABLED	0xEF10AF12
#define AUTO_FW_RESET_DISABLED	0xDCBAAF12

/* firmware response header:
 *	63:58 - message type
 *	57:56 - owner
 *	55:53 - desc count
 *	52:48 - reserved
 *	47:40 - completion id
 *	39:32 - opcode
 *	31:16 - error code
 *	15:00 - reserved
 */
#define netxen_get_nic_msgtype(msg_hdr)	\
	((msg_hdr >> 58) & 0x3F)
#define netxen_get_nic_msg_compid(msg_hdr)	\
	((msg_hdr >> 40) & 0xFF)
#define netxen_get_nic_msg_opcode(msg_hdr)	\
	((msg_hdr >> 32) & 0xFF)
#define netxen_get_nic_msg_errcode(msg_hdr)	\
	((msg_hdr >> 16) & 0xFFFF)

typedef struct {
	union {
		struct {
			u64 hdr;
			u64 body[7];
		};
		u64 words[8];
	};
} nx_fw_msg_t;

typedef struct {
	__le64 qhdr;
	__le64 req_hdr;
	__le64 words[6];
} nx_nic_req_t;

typedef struct {
	u8 op;
	u8 tag;
	u8 mac_addr[6];
} nx_mac_req_t;

#define MAX_PENDING_DESC_BLOCK_SIZE	64

#define NETXEN_NIC_MSI_ENABLED		0x02
#define NETXEN_NIC_MSIX_ENABLED		0x04
#define NETXEN_NIC_LRO_ENABLED		0x08
#define NETXEN_NIC_BRIDGE_ENABLED       0X10
#define NETXEN_NIC_DIAG_ENABLED		0x20
#define NETXEN_IS_MSI_FAMILY(adapter) \
	((adapter)->flags & (NETXEN_NIC_MSI_ENABLED | NETXEN_NIC_MSIX_ENABLED))

#define MSIX_ENTRIES_PER_ADAPTER	NUM_STS_DESC_RINGS
#define NETXEN_MSIX_TBL_SPACE		8192
#define NETXEN_PCI_REG_MSIX_TBL		0x44

#define NETXEN_DB_MAPSIZE_BYTES    	0x1000

#define NETXEN_NETDEV_WEIGHT 128
#define NETXEN_ADAPTER_UP_MAGIC 777
#define NETXEN_NIC_PEG_TUNE 0

#define __NX_FW_ATTACHED		0
#define __NX_DEV_UP			1
#define __NX_RESETTING			2

struct netxen_dummy_dma {
	void *addr;
	dma_addr_t phys_addr;
};

struct netxen_adapter {
	struct netxen_hardware_context ahw;

	struct net_device *netdev;
	struct pci_dev *pdev;
	struct list_head mac_list;

	spinlock_t tx_clean_lock;

	u16 num_txd;
	u16 num_rxd;
	u16 num_jumbo_rxd;
	u16 num_lro_rxd;

	u8 max_rds_rings;
	u8 max_sds_rings;
	u8 driver_mismatch;
	u8 msix_supported;
	u8 rx_csum;
	u8 pci_using_dac;
	u8 portnum;
	u8 physical_port;

	u8 mc_enabled;
	u8 max_mc_count;
	u8 rss_supported;
	u8 link_changed;
	u8 fw_wait_cnt;
	u8 fw_fail_cnt;
	u8 tx_timeo_cnt;
	u8 need_fw_reset;

	u8 has_link_events;
	u8 fw_type;
	u16 tx_context_id;
	u16 mtu;
	u16 is_up;

	u16 link_speed;
	u16 link_duplex;
	u16 link_autoneg;
	u16 module_type;

	u32 capabilities;
	u32 flags;
	u32 irq;
	u32 temp;

	u32 int_vec_bit;
	u32 heartbit;

	u8 mac_addr[ETH_ALEN];

	struct netxen_adapter_stats stats;

	struct netxen_recv_context recv_ctx;
	struct nx_host_tx_ring *tx_ring;

	int (*macaddr_set) (struct netxen_adapter *, u8 *);
	int (*set_mtu) (struct netxen_adapter *, int);
	int (*set_promisc) (struct netxen_adapter *, u32);
	void (*set_multi) (struct net_device *);
	int (*phy_read) (struct netxen_adapter *, u32 reg, u32 *);
	int (*phy_write) (struct netxen_adapter *, u32 reg, u32 val);
	int (*init_port) (struct netxen_adapter *, int);
	int (*stop_port) (struct netxen_adapter *);

	u32 (*crb_read)(struct netxen_adapter *, ulong);
	int (*crb_write)(struct netxen_adapter *, ulong, u32);

	int (*pci_mem_read)(struct netxen_adapter *, u64, u64 *);
	int (*pci_mem_write)(struct netxen_adapter *, u64, u64);

	int (*pci_set_window)(struct netxen_adapter *, u64, u32 *);

	u32 (*io_read)(struct netxen_adapter *, void __iomem *);
	void (*io_write)(struct netxen_adapter *, void __iomem *, u32);

	void __iomem	*tgt_mask_reg;
	void __iomem	*pci_int_reg;
	void __iomem	*tgt_status_reg;
	void __iomem	*crb_int_state_reg;
	void __iomem	*isr_int_vec;

	struct msix_entry msix_entries[MSIX_ENTRIES_PER_ADAPTER];

	struct netxen_dummy_dma dummy_dma;

	struct delayed_work fw_work;

	struct work_struct  tx_timeout_task;

	nx_nic_intr_coalesce_t coal;

	unsigned long state;
	__le32 file_prd_off;	/*File fw product offset*/
	u32 fw_version;
	const struct firmware *fw;
};

int netxen_niu_xg_init_port(struct netxen_adapter *adapter, int port);
int netxen_niu_disable_xg_port(struct netxen_adapter *adapter);

int nx_fw_cmd_query_phy(struct netxen_adapter *adapter, u32 reg, u32 *val);
int nx_fw_cmd_set_phy(struct netxen_adapter *adapter, u32 reg, u32 val);

/* Functions available from netxen_nic_hw.c */
int netxen_nic_set_mtu_xgb(struct netxen_adapter *adapter, int new_mtu);
int netxen_nic_set_mtu_gb(struct netxen_adapter *adapter, int new_mtu);

int netxen_p2_nic_set_mac_addr(struct netxen_adapter *adapter, u8 *addr);
int netxen_p3_nic_set_mac_addr(struct netxen_adapter *adapter, u8 *addr);

#define NXRD32(adapter, off) \
	(adapter->crb_read(adapter, off))
#define NXWR32(adapter, off, val) \
	(adapter->crb_write(adapter, off, val))
#define NXRDIO(adapter, addr) \
	(adapter->io_read(adapter, addr))
#define NXWRIO(adapter, addr, val) \
	(adapter->io_write(adapter, addr, val))

int netxen_pcie_sem_lock(struct netxen_adapter *, int, u32);
void netxen_pcie_sem_unlock(struct netxen_adapter *, int);

#define netxen_rom_lock(a)	\
	netxen_pcie_sem_lock((a), 2, NETXEN_ROM_LOCK_ID)
#define netxen_rom_unlock(a)	\
	netxen_pcie_sem_unlock((a), 2)
#define netxen_phy_lock(a)	\
	netxen_pcie_sem_lock((a), 3, NETXEN_PHY_LOCK_ID)
#define netxen_phy_unlock(a)	\
	netxen_pcie_sem_unlock((a), 3)
#define netxen_api_lock(a)	\
	netxen_pcie_sem_lock((a), 5, 0)
#define netxen_api_unlock(a)	\
	netxen_pcie_sem_unlock((a), 5)
#define netxen_sw_lock(a)	\
	netxen_pcie_sem_lock((a), 6, 0)
#define netxen_sw_unlock(a)	\
	netxen_pcie_sem_unlock((a), 6)
#define crb_win_lock(a)	\
	netxen_pcie_sem_lock((a), 7, NETXEN_CRB_WIN_LOCK_ID)
#define crb_win_unlock(a)	\
	netxen_pcie_sem_unlock((a), 7)

int netxen_nic_get_board_info(struct netxen_adapter *adapter);
int netxen_nic_wol_supported(struct netxen_adapter *adapter);

/* Functions from netxen_nic_init.c */
int netxen_init_dummy_dma(struct netxen_adapter *adapter);
void netxen_free_dummy_dma(struct netxen_adapter *adapter);

int netxen_phantom_init(struct netxen_adapter *adapter, int pegtune_val);
int netxen_load_firmware(struct netxen_adapter *adapter);
int netxen_need_fw_reset(struct netxen_adapter *adapter);
void netxen_request_firmware(struct netxen_adapter *adapter);
void netxen_release_firmware(struct netxen_adapter *adapter);
int netxen_pinit_from_rom(struct netxen_adapter *adapter);

int netxen_rom_fast_read(struct netxen_adapter *adapter, int addr, int *valp);
int netxen_rom_fast_read_words(struct netxen_adapter *adapter, int addr,
				u8 *bytes, size_t size);
int netxen_rom_fast_write_words(struct netxen_adapter *adapter, int addr,
				u8 *bytes, size_t size);
int netxen_flash_unlock(struct netxen_adapter *adapter);
int netxen_backup_crbinit(struct netxen_adapter *adapter);
int netxen_flash_erase_secondary(struct netxen_adapter *adapter);
int netxen_flash_erase_primary(struct netxen_adapter *adapter);
void netxen_halt_pegs(struct netxen_adapter *adapter);

int netxen_rom_se(struct netxen_adapter *adapter, int addr);

int netxen_alloc_sw_resources(struct netxen_adapter *adapter);
void netxen_free_sw_resources(struct netxen_adapter *adapter);

void netxen_setup_hwops(struct netxen_adapter *adapter);
void __iomem *netxen_get_ioaddr(struct netxen_adapter *, u32);

int netxen_alloc_hw_resources(struct netxen_adapter *adapter);
void netxen_free_hw_resources(struct netxen_adapter *adapter);

void netxen_release_rx_buffers(struct netxen_adapter *adapter);
void netxen_release_tx_buffers(struct netxen_adapter *adapter);

int netxen_init_firmware(struct netxen_adapter *adapter);
void netxen_nic_clear_stats(struct netxen_adapter *adapter);
void netxen_watchdog_task(struct work_struct *work);
void netxen_post_rx_buffers(struct netxen_adapter *adapter, u32 ringid,
		struct nx_host_rds_ring *rds_ring);
int netxen_process_cmd_ring(struct netxen_adapter *adapter);
int netxen_process_rcv_ring(struct nx_host_sds_ring *sds_ring, int max);
void netxen_p2_nic_set_multi(struct net_device *netdev);
void netxen_p3_nic_set_multi(struct net_device *netdev);
void netxen_p3_free_mac_list(struct netxen_adapter *adapter);
int netxen_p2_nic_set_promisc(struct netxen_adapter *adapter, u32 mode);
int netxen_p3_nic_set_promisc(struct netxen_adapter *adapter, u32);
int netxen_config_intr_coalesce(struct netxen_adapter *adapter);
int netxen_config_rss(struct netxen_adapter *adapter, int enable);
int netxen_config_ipaddr(struct netxen_adapter *adapter, u32 ip, int cmd);
int netxen_linkevent_request(struct netxen_adapter *adapter, int enable);
void netxen_advert_link_change(struct netxen_adapter *adapter, int linkup);

int nx_fw_cmd_set_mtu(struct netxen_adapter *adapter, int mtu);
int netxen_nic_change_mtu(struct net_device *netdev, int new_mtu);
int netxen_config_hw_lro(struct netxen_adapter *adapter, int enable);
int netxen_config_bridged_mode(struct netxen_adapter *adapter, int enable);
int netxen_send_lro_cleanup(struct netxen_adapter *adapter);

int netxen_nic_set_mac(struct net_device *netdev, void *p);
struct net_device_stats *netxen_nic_get_stats(struct net_device *netdev);

void netxen_nic_update_cmd_producer(struct netxen_adapter *adapter,
		struct nx_host_tx_ring *tx_ring);

/* Functions from netxen_nic_main.c */
int netxen_nic_reset_context(struct netxen_adapter *);

/*
 * NetXen Board information
 */

#define NETXEN_MAX_SHORT_NAME 32
struct netxen_brdinfo {
	int brdtype;	/* type of board */
	long ports;		/* max no of physical ports */
	char short_name[NETXEN_MAX_SHORT_NAME];
};

static const struct netxen_brdinfo netxen_boards[] = {
	{NETXEN_BRDTYPE_P2_SB31_10G_CX4, 1, "XGb CX4"},
	{NETXEN_BRDTYPE_P2_SB31_10G_HMEZ, 1, "XGb HMEZ"},
	{NETXEN_BRDTYPE_P2_SB31_10G_IMEZ, 2, "XGb IMEZ"},
	{NETXEN_BRDTYPE_P2_SB31_10G, 1, "XGb XFP"},
	{NETXEN_BRDTYPE_P2_SB35_4G, 4, "Quad Gb"},
	{NETXEN_BRDTYPE_P2_SB31_2G, 2, "Dual Gb"},
	{NETXEN_BRDTYPE_P3_REF_QG,  4, "Reference Quad Gig "},
	{NETXEN_BRDTYPE_P3_HMEZ,    2, "Dual XGb HMEZ"},
	{NETXEN_BRDTYPE_P3_10G_CX4_LP,   2, "Dual XGb CX4 LP"},
	{NETXEN_BRDTYPE_P3_4_GB,    4, "Quad Gig LP"},
	{NETXEN_BRDTYPE_P3_IMEZ,    2, "Dual XGb IMEZ"},
	{NETXEN_BRDTYPE_P3_10G_SFP_PLUS, 2, "Dual XGb SFP+ LP"},
	{NETXEN_BRDTYPE_P3_10000_BASE_T, 1, "XGB 10G BaseT LP"},
	{NETXEN_BRDTYPE_P3_XG_LOM,  2, "Dual XGb LOM"},
	{NETXEN_BRDTYPE_P3_4_GB_MM, 4, "NX3031 Gigabit Ethernet"},
	{NETXEN_BRDTYPE_P3_10G_SFP_CT, 2, "NX3031 10 Gigabit Ethernet"},
	{NETXEN_BRDTYPE_P3_10G_SFP_QT, 2, "Quanta Dual XGb SFP+"},
	{NETXEN_BRDTYPE_P3_10G_CX4, 2, "Reference Dual CX4 Option"},
	{NETXEN_BRDTYPE_P3_10G_XFP, 1, "Reference Single XFP Option"}
};

#define NUM_SUPPORTED_BOARDS ARRAY_SIZE(netxen_boards)

static inline void get_brd_name_by_type(u32 type, char *name)
{
	int i, found = 0;
	for (i = 0; i < NUM_SUPPORTED_BOARDS; ++i) {
		if (netxen_boards[i].brdtype == type) {
			strcpy(name, netxen_boards[i].short_name);
			found = 1;
			break;
		}

	}
	if (!found)
		name = "Unknown";
}

static inline u32 netxen_tx_avail(struct nx_host_tx_ring *tx_ring)
{
	smp_mb();
	return find_diff_among(tx_ring->producer,
			tx_ring->sw_consumer, tx_ring->num_desc);

}

int netxen_get_flash_mac_addr(struct netxen_adapter *adapter, u64 *mac);
int netxen_p3_get_mac_addr(struct netxen_adapter *adapter, u64 *mac);
extern void netxen_change_ringparam(struct netxen_adapter *adapter);
extern int netxen_rom_fast_read(struct netxen_adapter *adapter, int addr,
				int *valp);

extern const struct ethtool_ops netxen_nic_ethtool_ops;

#endif				/* __NETXEN_NIC_H_ */
