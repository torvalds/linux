/*
 * Copyright (C) 2003 - 2006 NetXen, Inc.
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
 * in the file called LICENSE.
 *
 * Contact Information:
 *    info@netxen.com
 * NetXen,
 * 3965 Freedom Circle, Fourth floor,
 * Santa Clara, CA 95054
 */

#ifndef _NETXEN_NIC_H_
#define _NETXEN_NIC_H_

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/compiler.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ip.h>
#include <linux/in.h>
#include <linux/tcp.h>
#include <linux/skbuff.h>
#include <linux/version.h>

#include <linux/ethtool.h>
#include <linux/mii.h>
#include <linux/interrupt.h>
#include <linux/timer.h>

#include <linux/mm.h>
#include <linux/mman.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/byteorder.h>
#include <asm/uaccess.h>
#include <asm/pgtable.h>

#include "netxen_nic_hw.h"

#define _NETXEN_NIC_LINUX_MAJOR 3
#define _NETXEN_NIC_LINUX_MINOR 4
#define _NETXEN_NIC_LINUX_SUBVERSION 2
#define NETXEN_NIC_LINUX_VERSIONID  "3.4.2"

#define NETXEN_NUM_FLASH_SECTORS (64)
#define NETXEN_FLASH_SECTOR_SIZE (64 * 1024)
#define NETXEN_FLASH_TOTAL_SIZE  (NETXEN_NUM_FLASH_SECTORS \
					* NETXEN_FLASH_SECTOR_SIZE)

#define PHAN_VENDOR_ID 0x4040

#define RCV_DESC_RINGSIZE	\
	(sizeof(struct rcv_desc) * adapter->max_rx_desc_count)
#define STATUS_DESC_RINGSIZE	\
	(sizeof(struct status_desc)* adapter->max_rx_desc_count)
#define LRO_DESC_RINGSIZE	\
	(sizeof(rcvDesc_t) * adapter->max_lro_rx_desc_count)
#define TX_RINGSIZE	\
	(sizeof(struct netxen_cmd_buffer) * adapter->max_tx_desc_count)
#define RCV_BUFFSIZE	\
	(sizeof(struct netxen_rx_buffer) * rcv_desc->max_rx_desc_count)
#define find_diff_among(a,b,range) ((a)<=(b)?((b)-(a)):((b)+(range)-(a)))

#define NETXEN_NETDEV_STATUS		0x1
#define NETXEN_RCV_PRODUCER_OFFSET	0
#define NETXEN_RCV_PEG_DB_ID		2
#define NETXEN_HOST_DUMMY_DMA_SIZE 1024
#define FLASH_SUCCESS 0

#define ADDR_IN_WINDOW1(off)	\
	((off > NETXEN_CRB_PCIX_HOST2) && (off < NETXEN_CRB_MAX)) ? 1 : 0
/*
 * In netxen_nic_down(), we must wait for any pending callback requests into
 * netxen_watchdog_task() to complete; eg otherwise the watchdog_timer could be
 * reenabled right after it is deleted in netxen_nic_down(). FLUSH_SCHEDULED_WORK()
 * does this synchronization.
 *
 * Normally, schedule_work()/flush_scheduled_work() could have worked, but
 * netxen_nic_close() is invoked with kernel rtnl lock held. netif_carrier_off()
 * call in netxen_nic_close() triggers a schedule_work(&linkwatch_work), and a
 * subsequent call to flush_scheduled_work() in netxen_nic_down() would cause
 * linkwatch_event() to be executed which also attempts to acquire the rtnl
 * lock thus causing a deadlock.
 */

#define SCHEDULE_WORK(tp)	queue_work(netxen_workq, tp)
#define FLUSH_SCHEDULED_WORK()	flush_workqueue(netxen_workq)
extern struct workqueue_struct *netxen_workq;

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

#define FIRST_PAGE_GROUP_START	0
#define FIRST_PAGE_GROUP_END	0x100000

#define SECOND_PAGE_GROUP_START	0x6000000
#define SECOND_PAGE_GROUP_END	0x68BC000

#define THIRD_PAGE_GROUP_START	0x70E4000
#define THIRD_PAGE_GROUP_END	0x8000000

#define FIRST_PAGE_GROUP_SIZE  FIRST_PAGE_GROUP_END - FIRST_PAGE_GROUP_START
#define SECOND_PAGE_GROUP_SIZE SECOND_PAGE_GROUP_END - SECOND_PAGE_GROUP_START
#define THIRD_PAGE_GROUP_SIZE  THIRD_PAGE_GROUP_END - THIRD_PAGE_GROUP_START

#define MAX_RX_BUFFER_LENGTH		1760
#define MAX_RX_JUMBO_BUFFER_LENGTH 	8062
#define MAX_RX_LRO_BUFFER_LENGTH	((48*1024)-512)
#define RX_DMA_MAP_LEN			(MAX_RX_BUFFER_LENGTH - 2)
#define RX_JUMBO_DMA_MAP_LEN	\
	(MAX_RX_JUMBO_BUFFER_LENGTH - 2)
#define RX_LRO_DMA_MAP_LEN		(MAX_RX_LRO_BUFFER_LENGTH - 2)
#define NETXEN_ROM_ROUNDUP		0x80000000ULL

/*
 * Maximum number of ring contexts
 */
#define MAX_RING_CTX 1

/* Opcodes to be used with the commands */
enum {
	TX_ETHER_PKT = 0x01,
/* The following opcodes are for IP checksum	*/
	TX_TCP_PKT,
	TX_UDP_PKT,
	TX_IP_PKT,
	TX_TCP_LSO,
	TX_IPSEC,
	TX_IPSEC_CMD
};

/* The following opcodes are for internal consumption. */
#define NETXEN_CONTROL_OP	0x10
#define PEGNET_REQUEST		0x11

#define	MAX_NUM_CARDS		4

#define MAX_BUFFERS_PER_CMD	32

/*
 * Following are the states of the Phantom. Phantom will set them and
 * Host will read to check if the fields are correct.
 */
#define PHAN_INITIALIZE_START		0xff00
#define PHAN_INITIALIZE_FAILED		0xffff
#define PHAN_INITIALIZE_COMPLETE	0xff01

/* Host writes the following to notify that it has done the init-handshake */
#define PHAN_INITIALIZE_ACK	0xf00f

#define NUM_RCV_DESC_RINGS	3	/* No of Rcv Descriptor contexts */

/* descriptor types */
#define RCV_DESC_NORMAL		0x01
#define RCV_DESC_JUMBO		0x02
#define RCV_DESC_LRO		0x04
#define RCV_DESC_NORMAL_CTXID	0
#define RCV_DESC_JUMBO_CTXID	1
#define RCV_DESC_LRO_CTXID	2

#define RCV_DESC_TYPE(ID) \
	((ID == RCV_DESC_JUMBO_CTXID)	\
		? RCV_DESC_JUMBO	\
		: ((ID == RCV_DESC_LRO_CTXID)	\
			? RCV_DESC_LRO :	\
			(RCV_DESC_NORMAL)))

#define MAX_CMD_DESCRIPTORS		1024
#define MAX_RCV_DESCRIPTORS		16384
#define MAX_CMD_DESCRIPTORS_HOST	(MAX_CMD_DESCRIPTORS / 4)
#define MAX_RCV_DESCRIPTORS_1G		(MAX_RCV_DESCRIPTORS / 4)
#define MAX_JUMBO_RCV_DESCRIPTORS	1024
#define MAX_LRO_RCV_DESCRIPTORS		64
#define MAX_RCVSTATUS_DESCRIPTORS	MAX_RCV_DESCRIPTORS
#define MAX_JUMBO_RCV_DESC	MAX_JUMBO_RCV_DESCRIPTORS
#define MAX_RCV_DESC		MAX_RCV_DESCRIPTORS
#define MAX_RCVSTATUS_DESC	MAX_RCV_DESCRIPTORS
#define MAX_EPG_DESCRIPTORS	(MAX_CMD_DESCRIPTORS * 8)
#define NUM_RCV_DESC		(MAX_RCV_DESC + MAX_JUMBO_RCV_DESCRIPTORS + \
				 MAX_LRO_RCV_DESCRIPTORS)
#define MIN_TX_COUNT	4096
#define MIN_RX_COUNT	4096
#define NETXEN_CTX_SIGNATURE	0xdee0
#define NETXEN_RCV_PRODUCER(ringid)	(ringid)
#define MAX_FRAME_SIZE	0x10000	/* 64K MAX size for LSO */

#define PHAN_PEG_RCV_INITIALIZED	0xff01
#define PHAN_PEG_RCV_START_INITIALIZE	0xff00

#define get_next_index(index, length)	\
	(((index) + 1) & ((length) - 1))

#define get_index_range(index,length,count)	\
	(((index) + (count)) & ((length) - 1))

#define MPORT_SINGLE_FUNCTION_MODE 0x1111
#define MPORT_MULTI_FUNCTION_MODE 0x2222

#include "netxen_nic_phan_reg.h"
extern unsigned long long netxen_dma_mask;
extern unsigned long last_schedule_time;

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

struct netxen_rcv_context {
	__le64 rcv_ring_addr;
	__le32 rcv_ring_size;
	__le32 rsrvd;
};

struct netxen_ring_ctx {

	/* one command ring */
	__le64 cmd_consumer_offset;
	__le64 cmd_ring_addr;
	__le32 cmd_ring_size;
	__le32 rsrvd;

	/* three receive rings */
	struct netxen_rcv_context rcv_ctx[3];

	/* one status ring */
	__le64 sts_ring_addr;
	__le32 sts_ring_size;

	__le32 ctx_id;
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

#define netxen_set_cmd_desc_port(cmd_desc, var)	\
	((cmd_desc)->port_ctxid |= ((var) & 0x0F))
#define netxen_set_cmd_desc_ctxid(cmd_desc, var)	\
	((cmd_desc)->port_ctxid |= ((var) & 0xF0))

#define netxen_set_cmd_desc_flags(cmd_desc, val)	\
	((cmd_desc)->flags_opcode &= ~cpu_to_le16(0x7f), \
	(cmd_desc)->flags_opcode |= cpu_to_le16((val) & 0x7f))
#define netxen_set_cmd_desc_opcode(cmd_desc, val)	\
	((cmd_desc)->flags_opcode &= ~cpu_to_le16(0x3f<<7), \
	(cmd_desc)->flags_opcode |= cpu_to_le16(((val & 0x3f)<<7)))

#define netxen_set_cmd_desc_num_of_buff(cmd_desc, val)	\
	((cmd_desc)->num_of_buffers_total_length &= ~cpu_to_le32(0xff), \
	(cmd_desc)->num_of_buffers_total_length |= cpu_to_le32((val) & 0xff))
#define netxen_set_cmd_desc_totallength(cmd_desc, val)	\
	((cmd_desc)->num_of_buffers_total_length &= ~cpu_to_le32(0xffffff00), \
	(cmd_desc)->num_of_buffers_total_length |= cpu_to_le32(val << 8))

#define netxen_get_cmd_desc_opcode(cmd_desc)	\
	((le16_to_cpu((cmd_desc)->flags_opcode) >> 7) & 0x003F)
#define netxen_get_cmd_desc_totallength(cmd_desc)	\
	(le32_to_cpu((cmd_desc)->num_of_buffers_total_length) >> 8)

struct cmd_desc_type0 {
	u8 tcp_hdr_offset;	/* For LSO only */
	u8 ip_hdr_offset;	/* For LSO only */
	/* Bit pattern: 0-6 flags, 7-12 opcode, 13-15 unused */
	__le16 flags_opcode;
	/* Bit pattern: 0-7 total number of segments,
	   8-31 Total size of the packet */
	__le32 num_of_buffers_total_length;
	union {
		struct {
			__le32 addr_low_part2;
			__le32 addr_high_part2;
		};
		__le64 addr_buffer2;
	};

	__le16 reference_handle;	/* changed to u16 to add mss */
	__le16 mss;		/* passed by NDIS_PACKET for LSO */
	/* Bit pattern 0-3 port, 0-3 ctx id */
	u8 port_ctxid;
	u8 total_hdr_length;	/* LSO only : MAC+IP+TCP Hdr size */
	__le16 conn_id;		/* IPSec offoad only */

	union {
		struct {
			__le32 addr_low_part3;
			__le32 addr_high_part3;
		};
		__le64 addr_buffer3;
	};
	union {
		struct {
			__le32 addr_low_part1;
			__le32 addr_high_part1;
		};
		__le64 addr_buffer1;
	};

	__le16 buffer1_length;
	__le16 buffer2_length;
	__le16 buffer3_length;
	__le16 buffer4_length;

	union {
		struct {
			__le32 addr_low_part4;
			__le32 addr_high_part4;
		};
		__le64 addr_buffer4;
	};

	__le64 unused;

} __attribute__ ((aligned(64)));

/* Note: sizeof(rcv_desc) should always be a mutliple of 2 */
struct rcv_desc {
	__le16 reference_handle;
	__le16 reserved;
	__le32 buffer_length;	/* allocated buffer length (usually 2K) */
	__le64 addr_buffer;
};

/* opcode field in status_desc */
#define RCV_NIC_PKT	(0xA)
#define STATUS_NIC_PKT	((RCV_NIC_PKT) << 12)

/* for status field in status_desc */
#define STATUS_NEED_CKSUM	(1)
#define STATUS_CKSUM_OK		(2)

/* owner bits of status_desc */
#define STATUS_OWNER_HOST	(0x1)
#define STATUS_OWNER_PHANTOM	(0x2)

#define NETXEN_PROT_IP		(1)
#define NETXEN_PROT_UNKNOWN	(0)

/* Note: sizeof(status_desc) should always be a mutliple of 2 */

#define netxen_get_sts_desc_lro_cnt(status_desc)	\
	((status_desc)->lro & 0x7F)
#define netxen_get_sts_desc_lro_last_frag(status_desc)	\
	(((status_desc)->lro & 0x80) >> 7)

#define netxen_get_sts_port(status_desc)	\
	(le64_to_cpu((status_desc)->status_desc_data) & 0x0F)
#define netxen_get_sts_status(status_desc)	\
	((le64_to_cpu((status_desc)->status_desc_data) >> 4) & 0x0F)
#define netxen_get_sts_type(status_desc)	\
	((le64_to_cpu((status_desc)->status_desc_data) >> 8) & 0x0F)
#define netxen_get_sts_totallength(status_desc)	\
	((le64_to_cpu((status_desc)->status_desc_data) >> 12) & 0xFFFF)
#define netxen_get_sts_refhandle(status_desc)	\
	((le64_to_cpu((status_desc)->status_desc_data) >> 28) & 0xFFFF)
#define netxen_get_sts_prot(status_desc)	\
	((le64_to_cpu((status_desc)->status_desc_data) >> 44) & 0x0F)
#define netxen_get_sts_owner(status_desc)	\
	((le64_to_cpu((status_desc)->status_desc_data) >> 56) & 0x03)
#define netxen_get_sts_opcode(status_desc)	\
	((le64_to_cpu((status_desc)->status_desc_data) >> 58) & 0x03F)

#define netxen_clear_sts_owner(status_desc)	\
	((status_desc)->status_desc_data &=	\
	~cpu_to_le64(((unsigned long long)3) << 56 ))
#define netxen_set_sts_owner(status_desc, val)	\
	((status_desc)->status_desc_data |=	\
	cpu_to_le64(((unsigned long long)((val) & 0x3)) << 56 ))

struct status_desc {
	/* Bit pattern: 0-3 port, 4-7 status, 8-11 type, 12-27 total_length
	   28-43 reference_handle, 44-47 protocol, 48-52 unused
	   53-55 desc_cnt, 56-57 owner, 58-63 opcode
	 */
	__le64 status_desc_data;
	__le32 hash_value;
	u8 hash_type;
	u8 msg_type;
	u8 unused;
	/* Bit pattern: 0-6 lro_count indicates frag sequence,
	   7 last_frag indicates last frag */
	u8 lro;
} __attribute__ ((aligned(16)));

enum {
	NETXEN_RCV_PEG_0 = 0,
	NETXEN_RCV_PEG_1
};
/* The version of the main data structure */
#define	NETXEN_BDINFO_VERSION 1

/* Magic number to let user know flash is programmed */
#define	NETXEN_BDINFO_MAGIC 0x12345678

/* Max number of Gig ports on a Phantom board */
#define NETXEN_MAX_PORTS 4

typedef enum {
	NETXEN_BRDTYPE_P1_BD = 0x0000,
	NETXEN_BRDTYPE_P1_SB = 0x0001,
	NETXEN_BRDTYPE_P1_SMAX = 0x0002,
	NETXEN_BRDTYPE_P1_SOCK = 0x0003,

	NETXEN_BRDTYPE_P2_SOCK_31 = 0x0008,
	NETXEN_BRDTYPE_P2_SOCK_35 = 0x0009,
	NETXEN_BRDTYPE_P2_SB35_4G = 0x000a,
	NETXEN_BRDTYPE_P2_SB31_10G = 0x000b,
	NETXEN_BRDTYPE_P2_SB31_2G = 0x000c,

	NETXEN_BRDTYPE_P2_SB31_10G_IMEZ = 0x000d,
	NETXEN_BRDTYPE_P2_SB31_10G_HMEZ = 0x000e,
	NETXEN_BRDTYPE_P2_SB31_10G_CX4 = 0x000f
} netxen_brdtype_t;

typedef enum {
	NETXEN_BRDMFG_INVENTEC = 1
} netxen_brdmfg;

typedef enum {
	MEM_ORG_128Mbx4 = 0x0,	/* DDR1 only */
	MEM_ORG_128Mbx8 = 0x1,	/* DDR1 only */
	MEM_ORG_128Mbx16 = 0x2,	/* DDR1 only */
	MEM_ORG_256Mbx4 = 0x3,
	MEM_ORG_256Mbx8 = 0x4,
	MEM_ORG_256Mbx16 = 0x5,
	MEM_ORG_512Mbx4 = 0x6,
	MEM_ORG_512Mbx8 = 0x7,
	MEM_ORG_512Mbx16 = 0x8,
	MEM_ORG_1Gbx4 = 0x9,
	MEM_ORG_1Gbx8 = 0xa,
	MEM_ORG_1Gbx16 = 0xb,
	MEM_ORG_2Gbx4 = 0xc,
	MEM_ORG_2Gbx8 = 0xd,
	MEM_ORG_2Gbx16 = 0xe,
	MEM_ORG_128Mbx32 = 0x10002,	/* GDDR only */
	MEM_ORG_256Mbx32 = 0x10005	/* GDDR only */
} netxen_mn_mem_org_t;

typedef enum {
	MEM_ORG_512Kx36 = 0x0,
	MEM_ORG_1Mx36 = 0x1,
	MEM_ORG_2Mx36 = 0x2
} netxen_sn_mem_org_t;

typedef enum {
	MEM_DEPTH_4MB = 0x1,
	MEM_DEPTH_8MB = 0x2,
	MEM_DEPTH_16MB = 0x3,
	MEM_DEPTH_32MB = 0x4,
	MEM_DEPTH_64MB = 0x5,
	MEM_DEPTH_128MB = 0x6,
	MEM_DEPTH_256MB = 0x7,
	MEM_DEPTH_512MB = 0x8,
	MEM_DEPTH_1GB = 0x9,
	MEM_DEPTH_2GB = 0xa,
	MEM_DEPTH_4GB = 0xb,
	MEM_DEPTH_8GB = 0xc,
	MEM_DEPTH_16GB = 0xd,
	MEM_DEPTH_32GB = 0xe
} netxen_mem_depth_t;

struct netxen_board_info {
	u32 header_version;

	u32 board_mfg;
	u32 board_type;
	u32 board_num;
	u32 chip_id;
	u32 chip_minor;
	u32 chip_major;
	u32 chip_pkg;
	u32 chip_lot;

	u32 port_mask;		/* available niu ports */
	u32 peg_mask;		/* available pegs */
	u32 icache_ok;		/* can we run with icache? */
	u32 dcache_ok;		/* can we run with dcache? */
	u32 casper_ok;

	u32 mac_addr_lo_0;
	u32 mac_addr_lo_1;
	u32 mac_addr_lo_2;
	u32 mac_addr_lo_3;

	/* MN-related config */
	u32 mn_sync_mode;	/* enable/ sync shift cclk/ sync shift mclk */
	u32 mn_sync_shift_cclk;
	u32 mn_sync_shift_mclk;
	u32 mn_wb_en;
	u32 mn_crystal_freq;	/* in MHz */
	u32 mn_speed;		/* in MHz */
	u32 mn_org;
	u32 mn_depth;
	u32 mn_ranks_0;		/* ranks per slot */
	u32 mn_ranks_1;		/* ranks per slot */
	u32 mn_rd_latency_0;
	u32 mn_rd_latency_1;
	u32 mn_rd_latency_2;
	u32 mn_rd_latency_3;
	u32 mn_rd_latency_4;
	u32 mn_rd_latency_5;
	u32 mn_rd_latency_6;
	u32 mn_rd_latency_7;
	u32 mn_rd_latency_8;
	u32 mn_dll_val[18];
	u32 mn_mode_reg;	/* MIU DDR Mode Register */
	u32 mn_ext_mode_reg;	/* MIU DDR Extended Mode Register */
	u32 mn_timing_0;	/* MIU Memory Control Timing Rgister */
	u32 mn_timing_1;	/* MIU Extended Memory Ctrl Timing Register */
	u32 mn_timing_2;	/* MIU Extended Memory Ctrl Timing2 Register */

	/* SN-related config */
	u32 sn_sync_mode;	/* enable/ sync shift cclk / sync shift mclk */
	u32 sn_pt_mode;		/* pass through mode */
	u32 sn_ecc_en;
	u32 sn_wb_en;
	u32 sn_crystal_freq;
	u32 sn_speed;
	u32 sn_org;
	u32 sn_depth;
	u32 sn_dll_tap;
	u32 sn_rd_latency;

	u32 mac_addr_hi_0;
	u32 mac_addr_hi_1;
	u32 mac_addr_hi_2;
	u32 mac_addr_hi_3;

	u32 magic;		/* indicates flash has been initialized */

	u32 mn_rdimm;
	u32 mn_dll_override;

};

#define FLASH_NUM_PORTS		(4)

struct netxen_flash_mac_addr {
	u32 flash_addr[32];
};

struct netxen_user_old_info {
	u8 flash_md5[16];
	u8 crbinit_md5[16];
	u8 brdcfg_md5[16];
	/* bootloader */
	u32 bootld_version;
	u32 bootld_size;
	u8 bootld_md5[16];
	/* image */
	u32 image_version;
	u32 image_size;
	u8 image_md5[16];
	/* primary image status */
	u32 primary_status;
	u32 secondary_present;

	/* MAC address , 4 ports */
	struct netxen_flash_mac_addr mac_addr[FLASH_NUM_PORTS];
};
#define FLASH_NUM_MAC_PER_PORT	32
struct netxen_user_info {
	u8 flash_md5[16 * 64];
	/* bootloader */
	u32 bootld_version;
	u32 bootld_size;
	/* image */
	u32 image_version;
	u32 image_size;
	/* primary image status */
	u32 primary_status;
	u32 secondary_present;

	/* MAC address , 4 ports, 32 address per port */
	u64 mac_addr[FLASH_NUM_PORTS * FLASH_NUM_MAC_PER_PORT];
	u32 sub_sys_id;
	u8 serial_num[32];

	/* Any user defined data */
};

/*
 * Flash Layout - new format.
 */
struct netxen_new_user_info {
	u8 flash_md5[16 * 64];
	/* bootloader */
	u32 bootld_version;
	u32 bootld_size;
	/* image */
	u32 image_version;
	u32 image_size;
	/* primary image status */
	u32 primary_status;
	u32 secondary_present;

	/* MAC address , 4 ports, 32 address per port */
	u64 mac_addr[FLASH_NUM_PORTS * FLASH_NUM_MAC_PER_PORT];
	u32 sub_sys_id;
	u8 serial_num[32];

	/* Any user defined data */
};

#define SECONDARY_IMAGE_PRESENT 0xb3b4b5b6
#define SECONDARY_IMAGE_ABSENT	0xffffffff
#define PRIMARY_IMAGE_GOOD	0x5a5a5a5a
#define PRIMARY_IMAGE_BAD	0xffffffff

/* Flash memory map */
typedef enum {
	NETXEN_CRBINIT_START = 0,	/* Crbinit section */
	NETXEN_BRDCFG_START = 0x4000,	/* board config */
	NETXEN_INITCODE_START = 0x6000,	/* pegtune code */
	NETXEN_BOOTLD_START = 0x10000,	/* bootld */
	NETXEN_IMAGE_START = 0x43000,	/* compressed image */
	NETXEN_SECONDARY_START = 0x200000,	/* backup images */
	NETXEN_PXE_START = 0x3E0000,	/* user defined region */
	NETXEN_USER_START = 0x3E8000,	/* User defined region for new boards */
	NETXEN_FIXED_START = 0x3F0000	/* backup of crbinit */
} netxen_flash_map_t;

#define NETXEN_USER_START_OLD NETXEN_PXE_START	/* for backward compatibility */

#define NETXEN_FLASH_START		(NETXEN_CRBINIT_START)
#define NETXEN_INIT_SECTOR		(0)
#define NETXEN_PRIMARY_START 		(NETXEN_BOOTLD_START)
#define NETXEN_FLASH_CRBINIT_SIZE 	(0x4000)
#define NETXEN_FLASH_BRDCFG_SIZE 	(sizeof(struct netxen_board_info))
#define NETXEN_FLASH_USER_SIZE		(sizeof(struct netxen_user_info)/sizeof(u32))
#define NETXEN_FLASH_SECONDARY_SIZE 	(NETXEN_USER_START-NETXEN_SECONDARY_START)
#define NETXEN_NUM_PRIMARY_SECTORS	(0x20)
#define NETXEN_NUM_CONFIG_SECTORS 	(1)
#define PFX "NetXen: "
extern char netxen_nic_driver_name[];

/* Note: Make sure to not call this before adapter->port is valid */
#if !defined(NETXEN_DEBUG)
#define DPRINTK(klevel, fmt, args...)	do { \
	} while (0)
#else
#define DPRINTK(klevel, fmt, args...)	do { \
	printk(KERN_##klevel PFX "%s: %s: " fmt, __FUNCTION__,\
		(adapter != NULL && adapter->netdev != NULL) ? \
		adapter->netdev->name : NULL, \
		## args); } while(0)
#endif

/* Number of status descriptors to handle per interrupt */
#define MAX_STATUS_HANDLE	(128)

/*
 * netxen_skb_frag{} is to contain mapping info for each SG list. This
 * has to be freed when DMA is complete. This is part of netxen_tx_buffer{}.
 */
struct netxen_skb_frag {
	u64 dma;
	u32 length;
};

#define _netxen_set_bits(config_word, start, bits, val)	{\
	unsigned long long __tmask = (((1ULL << (bits)) - 1) << (start));\
	unsigned long long __tvalue = (val);    \
	(config_word) &= ~__tmask;      \
	(config_word) |= (((__tvalue) << (start)) & __tmask); \
}
	
#define _netxen_clear_bits(config_word, start, bits) {\
	unsigned long long __tmask = (((1ULL << (bits)) - 1) << (start));  \
	(config_word) &= ~__tmask; \
}		

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
	u32 total_length;
	u32 mss;
	u16 port;
	u8 cmd;
	u8 frag_count;
	unsigned long time_stamp;
	u32 state;
};

/* In rx_buffer, we do not need multiple fragments as is a single buffer */
struct netxen_rx_buffer {
	struct sk_buff *skb;
	u64 dma;
	u16 ref_handle;
	u16 state;
	u32 lro_expected_frags;
	u32 lro_current_frags;
	u32 lro_length;
};

/* Board types */
#define	NETXEN_NIC_GBE	0x01
#define	NETXEN_NIC_XGBE	0x02

/*
 * One hardware_context{} per adapter
 * contains interrupt info as well shared hardware info.
 */
struct netxen_hardware_context {
	struct pci_dev *pdev;
	void __iomem *pci_base0;
	void __iomem *pci_base1;
	void __iomem *pci_base2;
	unsigned long first_page_group_end;
	unsigned long first_page_group_start;
	void __iomem *db_base;
	unsigned long db_len;

	u8 revision_id;
	u16 board_type;
	u16 max_ports;
	struct netxen_board_info boardcfg;
	u32 xg_linkup;
	u32 qg_linksup;
	/* Address of cmd ring in Phantom */
	struct cmd_desc_type0 *cmd_desc_head;
	struct pci_dev *cmd_desc_pdev;
	dma_addr_t cmd_desc_phys_addr;
	struct netxen_adapter *adapter;
	int pci_func;
};

#define RCV_RING_LRO	RCV_DESC_LRO

#define MINIMUM_ETHERNET_FRAME_SIZE	64	/* With FCS */
#define ETHERNET_FCS_SIZE		4

struct netxen_adapter_stats {
	u64  rcvdbadskb;
	u64  xmitcalled;
	u64  xmitedframes;
	u64  xmitfinished;
	u64  badskblen;
	u64  nocmddescriptor;
	u64  polled;
	u64  uphappy;
	u64  updropped;
	u64  uplcong;
	u64  uphcong;
	u64  upmcong;
	u64  updunno;
	u64  skbfreed;
	u64  txdropped;
	u64  txnullskb;
	u64  csummed;
	u64  no_rcv;
	u64  rxbytes;
	u64  txbytes;
	u64  ints;
};

/*
 * Rcv Descriptor Context. One such per Rcv Descriptor. There may
 * be one Rcv Descriptor for normal packets, one for jumbo and may be others.
 */
struct netxen_rcv_desc_ctx {
	u32 flags;
	u32 producer;
	u32 rcv_pending;	/* Num of bufs posted in phantom */
	u32 rcv_free;		/* Num of bufs in free list */
	dma_addr_t phys_addr;
	struct pci_dev *phys_pdev;
	struct rcv_desc *desc_head;	/* address of rx ring in Phantom */
	u32 max_rx_desc_count;
	u32 dma_size;
	u32 skb_size;
	struct netxen_rx_buffer *rx_buf_arr;	/* rx buffers for receive   */
	int begin_alloc;
};

/*
 * Receive context. There is one such structure per instance of the
 * receive processing. Any state information that is relevant to
 * the receive, and is must be in this structure. The global data may be
 * present elsewhere.
 */
struct netxen_recv_context {
	struct netxen_rcv_desc_ctx rcv_desc[NUM_RCV_DESC_RINGS];
	u32 status_rx_producer;
	u32 status_rx_consumer;
	dma_addr_t rcv_status_desc_phys_addr;
	struct pci_dev *rcv_status_desc_pdev;
	struct status_desc *rcv_status_desc_head;
};

#define NETXEN_NIC_MSI_ENABLED 0x02
#define NETXEN_DMA_MASK	0xfffffffe
#define NETXEN_DB_MAPSIZE_BYTES    0x1000

struct netxen_dummy_dma {
	void *addr;
	dma_addr_t phys_addr;
};

struct netxen_adapter {
	struct netxen_hardware_context ahw;
	
	struct netxen_adapter *master;
	struct net_device *netdev;
	struct pci_dev *pdev;
	struct napi_struct napi;
	struct net_device_stats net_stats;
	unsigned char mac_addr[ETH_ALEN];
	int mtu;
	int portnum;

	spinlock_t tx_lock;
	spinlock_t lock;
	struct work_struct watchdog_task;
	struct timer_list watchdog_timer;
	struct work_struct  tx_timeout_task;

	u32 curr_window;

	u32 cmd_producer;
	u32 *cmd_consumer;

	u32 last_cmd_consumer;
	u32 max_tx_desc_count;
	u32 max_rx_desc_count;
	u32 max_jumbo_rx_desc_count;
	u32 max_lro_rx_desc_count;
	/* Num of instances active on cmd buffer ring */
	u32 proc_cmd_buf_counter;

	u32 num_threads, total_threads;	/*Use to keep track of xmit threads */

	u32 flags;
	u32 irq;
	int driver_mismatch;
	u32 temp;

	struct netxen_adapter_stats stats;
	
	u16 portno;
	u16 link_speed;
	u16 link_duplex;
	u16 state;
	u16 link_autoneg;
	int rx_csum;
	int status;
	spinlock_t stats_lock;

	struct netxen_cmd_buffer *cmd_buf_arr;	/* Command buffers for xmit */

	/*
	 * Receive instances. These can be either one per port,
	 * or one per peg, etc.
	 */
	struct netxen_recv_context recv_ctx[MAX_RCV_CTX];

	int is_up;
	struct netxen_dummy_dma dummy_dma;

	/* Context interface shared between card and host */
	struct netxen_ring_ctx *ctx_desc;
	struct pci_dev *ctx_desc_pdev;
	dma_addr_t ctx_desc_phys_addr;
	int intr_scheme;
	int (*enable_phy_interrupts) (struct netxen_adapter *);
	int (*disable_phy_interrupts) (struct netxen_adapter *);
	void (*handle_phy_intr) (struct netxen_adapter *);
	int (*macaddr_set) (struct netxen_adapter *, netxen_ethernet_macaddr_t);
	int (*set_mtu) (struct netxen_adapter *, int);
	int (*set_promisc) (struct netxen_adapter *, netxen_niu_prom_mode_t);
	int (*unset_promisc) (struct netxen_adapter *, netxen_niu_prom_mode_t);
	int (*phy_read) (struct netxen_adapter *, long reg, u32 *);
	int (*phy_write) (struct netxen_adapter *, long reg, u32 val);
	int (*init_port) (struct netxen_adapter *, int);
	void (*init_niu) (struct netxen_adapter *);
	int (*stop_port) (struct netxen_adapter *);
};				/* netxen_adapter structure */

/*
 * NetXen dma watchdog control structure
 *
 *	Bit 0		: enabled => R/O: 1 watchdog active, 0 inactive
 *	Bit 1		: disable_request => 1 req disable dma watchdog
 *	Bit 2		: enable_request =>  1 req enable dma watchdog
 *	Bit 3-31	: unused
 */

#define netxen_set_dma_watchdog_disable_req(config_word) \
	_netxen_set_bits(config_word, 1, 1, 1)
#define netxen_set_dma_watchdog_enable_req(config_word) \
	_netxen_set_bits(config_word, 2, 1, 1)
#define netxen_get_dma_watchdog_enabled(config_word) \
	((config_word) & 0x1)
#define netxen_get_dma_watchdog_disabled(config_word) \
	(((config_word) >> 1) & 0x1)

/* Max number of xmit producer threads that can run simultaneously */
#define	MAX_XMIT_PRODUCERS		16

#define PCI_OFFSET_FIRST_RANGE(adapter, off)    \
	((adapter)->ahw.pci_base0 + (off))
#define PCI_OFFSET_SECOND_RANGE(adapter, off)   \
	((adapter)->ahw.pci_base1 + (off) - SECOND_PAGE_GROUP_START)
#define PCI_OFFSET_THIRD_RANGE(adapter, off)    \
	((adapter)->ahw.pci_base2 + (off) - THIRD_PAGE_GROUP_START)

static inline void __iomem *pci_base_offset(struct netxen_adapter *adapter,
					    unsigned long off)
{
	if ((off < FIRST_PAGE_GROUP_END) && (off >= FIRST_PAGE_GROUP_START)) {
		return (adapter->ahw.pci_base0 + off);
	} else if ((off < SECOND_PAGE_GROUP_END) &&
		   (off >= SECOND_PAGE_GROUP_START)) {
		return (adapter->ahw.pci_base1 + off - SECOND_PAGE_GROUP_START);
	} else if ((off < THIRD_PAGE_GROUP_END) &&
		   (off >= THIRD_PAGE_GROUP_START)) {
		return (adapter->ahw.pci_base2 + off - THIRD_PAGE_GROUP_START);
	}
	return NULL;
}

static inline void __iomem *pci_base(struct netxen_adapter *adapter,
				     unsigned long off)
{
	if ((off < FIRST_PAGE_GROUP_END) && (off >= FIRST_PAGE_GROUP_START)) {
		return adapter->ahw.pci_base0;
	} else if ((off < SECOND_PAGE_GROUP_END) &&
		   (off >= SECOND_PAGE_GROUP_START)) {
		return adapter->ahw.pci_base1;
	} else if ((off < THIRD_PAGE_GROUP_END) &&
		   (off >= THIRD_PAGE_GROUP_START)) {
		return adapter->ahw.pci_base2;
	}
	return NULL;
}

int netxen_niu_xgbe_enable_phy_interrupts(struct netxen_adapter *adapter);
int netxen_niu_gbe_enable_phy_interrupts(struct netxen_adapter *adapter);
int netxen_niu_xgbe_disable_phy_interrupts(struct netxen_adapter *adapter);
int netxen_niu_gbe_disable_phy_interrupts(struct netxen_adapter *adapter);
int netxen_niu_xgbe_clear_phy_interrupts(struct netxen_adapter *adapter);
int netxen_niu_gbe_clear_phy_interrupts(struct netxen_adapter *adapter);
void netxen_nic_xgbe_handle_phy_intr(struct netxen_adapter *adapter);
void netxen_nic_gbe_handle_phy_intr(struct netxen_adapter *adapter);
void netxen_niu_gbe_set_mii_mode(struct netxen_adapter *adapter, int port,
				 long enable);
void netxen_niu_gbe_set_gmii_mode(struct netxen_adapter *adapter, int port,
				  long enable);
int netxen_niu_gbe_phy_read(struct netxen_adapter *adapter, long reg,
			    __u32 * readval);
int netxen_niu_gbe_phy_write(struct netxen_adapter *adapter,
			     long reg, __u32 val);

/* Functions available from netxen_nic_hw.c */
int netxen_nic_set_mtu_xgb(struct netxen_adapter *adapter, int new_mtu);
int netxen_nic_set_mtu_gb(struct netxen_adapter *adapter, int new_mtu);
void netxen_nic_init_niu_gb(struct netxen_adapter *adapter);
void netxen_nic_pci_change_crbwindow(struct netxen_adapter *adapter, u32 wndw);
void netxen_nic_reg_write(struct netxen_adapter *adapter, u64 off, u32 val);
int netxen_nic_reg_read(struct netxen_adapter *adapter, u64 off);
void netxen_nic_write_w0(struct netxen_adapter *adapter, u32 index, u32 value);
void netxen_nic_read_w0(struct netxen_adapter *adapter, u32 index, u32 * value);

int netxen_nic_get_board_info(struct netxen_adapter *adapter);
int netxen_nic_hw_read_wx(struct netxen_adapter *adapter, u64 off, void *data,
			  int len);
int netxen_nic_hw_write_wx(struct netxen_adapter *adapter, u64 off, void *data,
			   int len);
void netxen_crb_writelit_adapter(struct netxen_adapter *adapter,
				 unsigned long off, int data);
int netxen_nic_erase_pxe(struct netxen_adapter *adapter);

/* Functions from netxen_nic_init.c */
void netxen_free_adapter_offload(struct netxen_adapter *adapter);
int netxen_initialize_adapter_offload(struct netxen_adapter *adapter);
int netxen_phantom_init(struct netxen_adapter *adapter, int pegtune_val);
int netxen_load_firmware(struct netxen_adapter *adapter);
int netxen_pinit_from_rom(struct netxen_adapter *adapter, int verbose);
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

int netxen_rom_fast_write(struct netxen_adapter *adapter, int addr, int data);
int netxen_rom_se(struct netxen_adapter *adapter, int addr);
int netxen_do_rom_se(struct netxen_adapter *adapter, int addr);

/* Functions from netxen_nic_isr.c */
int netxen_nic_link_ok(struct netxen_adapter *adapter);
void netxen_nic_isr_other(struct netxen_adapter *adapter);
void netxen_indicate_link_status(struct netxen_adapter *adapter, u32 link);
void netxen_handle_port_int(struct netxen_adapter *adapter, u32 enable);
void netxen_initialize_adapter_sw(struct netxen_adapter *adapter);
void netxen_initialize_adapter_hw(struct netxen_adapter *adapter);
void *netxen_alloc(struct pci_dev *pdev, size_t sz, dma_addr_t * ptr,
		   struct pci_dev **used_dev);
void netxen_initialize_adapter_ops(struct netxen_adapter *adapter);
int netxen_init_firmware(struct netxen_adapter *adapter);
void netxen_free_hw_resources(struct netxen_adapter *adapter);
void netxen_tso_check(struct netxen_adapter *adapter,
		      struct cmd_desc_type0 *desc, struct sk_buff *skb);
int netxen_nic_hw_resources(struct netxen_adapter *adapter);
void netxen_nic_clear_stats(struct netxen_adapter *adapter);
int netxen_nic_rx_has_work(struct netxen_adapter *adapter);
int netxen_nic_tx_has_work(struct netxen_adapter *adapter);
void netxen_watchdog_task(struct work_struct *work);
void netxen_post_rx_buffers(struct netxen_adapter *adapter, u32 ctx,
			    u32 ringid);
void netxen_post_rx_buffers_nodb(struct netxen_adapter *adapter, u32 ctx,
				 u32 ringid);
int netxen_process_cmd_ring(unsigned long data);
u32 netxen_process_rcv_ring(struct netxen_adapter *adapter, int ctx, int max);
void netxen_nic_set_multi(struct net_device *netdev);
int netxen_nic_change_mtu(struct net_device *netdev, int new_mtu);
int netxen_nic_set_mac(struct net_device *netdev, void *p);
struct net_device_stats *netxen_nic_get_stats(struct net_device *netdev);


/*
 * NetXen Board information
 */

#define NETXEN_MAX_SHORT_NAME 16
struct netxen_brdinfo {
	netxen_brdtype_t brdtype;	/* type of board */
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
};

#define NUM_SUPPORTED_BOARDS ARRAY_SIZE(netxen_boards)

static inline void get_brd_port_by_type(u32 type, int *ports)
{
	int i, found = 0;
	for (i = 0; i < NUM_SUPPORTED_BOARDS; ++i) {
		if (netxen_boards[i].brdtype == type) {
			*ports = netxen_boards[i].ports;
			found = 1;
			break;
		}
	}
	if (!found)
		*ports = 0;
}

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

static inline int
dma_watchdog_shutdown_request(struct netxen_adapter *adapter)
{
	u32 ctrl;

	/* check if already inactive */
	if (netxen_nic_hw_read_wx(adapter,
	    NETXEN_CAM_RAM(NETXEN_CAM_RAM_DMA_WATCHDOG_CTRL), &ctrl, 4))
		printk(KERN_ERR "failed to read dma watchdog status\n");

	if (netxen_get_dma_watchdog_enabled(ctrl) == 0)
		return 1;

	/* Send the disable request */
	netxen_set_dma_watchdog_disable_req(ctrl);
	netxen_crb_writelit_adapter(adapter,
		NETXEN_CAM_RAM(NETXEN_CAM_RAM_DMA_WATCHDOG_CTRL), ctrl);

	return 0;
}

static inline int
dma_watchdog_shutdown_poll_result(struct netxen_adapter *adapter)
{
	u32 ctrl;

	if (netxen_nic_hw_read_wx(adapter,
	    NETXEN_CAM_RAM(NETXEN_CAM_RAM_DMA_WATCHDOG_CTRL), &ctrl, 4))
		printk(KERN_ERR "failed to read dma watchdog status\n");

	return (netxen_get_dma_watchdog_enabled(ctrl) == 0);
}

static inline int
dma_watchdog_wakeup(struct netxen_adapter *adapter)
{
	u32 ctrl;

	if (netxen_nic_hw_read_wx(adapter,
		NETXEN_CAM_RAM(NETXEN_CAM_RAM_DMA_WATCHDOG_CTRL), &ctrl, 4))
		printk(KERN_ERR "failed to read dma watchdog status\n");

	if (netxen_get_dma_watchdog_enabled(ctrl))
		return 1;

	/* send the wakeup request */
	netxen_set_dma_watchdog_enable_req(ctrl);

	netxen_crb_writelit_adapter(adapter,
		NETXEN_CAM_RAM(NETXEN_CAM_RAM_DMA_WATCHDOG_CTRL), ctrl);

	return 0;
}


int netxen_is_flash_supported(struct netxen_adapter *adapter);
int netxen_get_flash_mac_addr(struct netxen_adapter *adapter, u64 mac[]);
extern void netxen_change_ringparam(struct netxen_adapter *adapter);
extern int netxen_rom_fast_read(struct netxen_adapter *adapter, int addr,
				int *valp);

extern struct ethtool_ops netxen_nic_ethtool_ops;

extern int physical_port[];	/* physical port # from virtual port.*/
#endif				/* __NETXEN_NIC_H_ */
