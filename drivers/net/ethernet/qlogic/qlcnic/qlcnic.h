/*
 * QLogic qlcnic NIC Driver
 * Copyright (c) 2009-2013 QLogic Corporation
 *
 * See LICENSE.qlcnic for copyright and licensing details.
 */

#ifndef _QLCNIC_H_
#define _QLCNIC_H_

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

#include <linux/io.h>
#include <asm/byteorder.h>
#include <linux/bitops.h>
#include <linux/if_vlan.h>

#include "qlcnic_hdr.h"
#include "qlcnic_hw.h"
#include "qlcnic_83xx_hw.h"
#include "qlcnic_dcb.h"

#define _QLCNIC_LINUX_MAJOR 5
#define _QLCNIC_LINUX_MINOR 3
#define _QLCNIC_LINUX_SUBVERSION 49
#define QLCNIC_LINUX_VERSIONID  "5.3.49"
#define QLCNIC_DRV_IDC_VER  0x01
#define QLCNIC_DRIVER_VERSION  ((_QLCNIC_LINUX_MAJOR << 16) |\
		 (_QLCNIC_LINUX_MINOR << 8) | (_QLCNIC_LINUX_SUBVERSION))

#define QLCNIC_VERSION_CODE(a, b, c)	(((a) << 24) + ((b) << 16) + (c))
#define _major(v)	(((v) >> 24) & 0xff)
#define _minor(v)	(((v) >> 16) & 0xff)
#define _build(v)	((v) & 0xffff)

/* version in image has weird encoding:
 *  7:0  - major
 * 15:8  - minor
 * 31:16 - build (little endian)
 */
#define QLCNIC_DECODE_VERSION(v) \
	QLCNIC_VERSION_CODE(((v) & 0xff), (((v) >> 8) & 0xff), ((v) >> 16))

#define QLCNIC_MIN_FW_VERSION     QLCNIC_VERSION_CODE(4, 4, 2)
#define QLCNIC_NUM_FLASH_SECTORS (64)
#define QLCNIC_FLASH_SECTOR_SIZE (64 * 1024)
#define QLCNIC_FLASH_TOTAL_SIZE  (QLCNIC_NUM_FLASH_SECTORS \
					* QLCNIC_FLASH_SECTOR_SIZE)

#define RCV_DESC_RINGSIZE(rds_ring)	\
	(sizeof(struct rcv_desc) * (rds_ring)->num_desc)
#define RCV_BUFF_RINGSIZE(rds_ring)	\
	(sizeof(struct qlcnic_rx_buffer) * rds_ring->num_desc)
#define STATUS_DESC_RINGSIZE(sds_ring)	\
	(sizeof(struct status_desc) * (sds_ring)->num_desc)
#define TX_BUFF_RINGSIZE(tx_ring)	\
	(sizeof(struct qlcnic_cmd_buffer) * tx_ring->num_desc)
#define TX_DESC_RINGSIZE(tx_ring)	\
	(sizeof(struct cmd_desc_type0) * tx_ring->num_desc)

#define QLCNIC_P3P_A0		0x50
#define QLCNIC_P3P_C0		0x58

#define QLCNIC_IS_REVISION_P3P(REVISION)     (REVISION >= QLCNIC_P3P_A0)

#define FIRST_PAGE_GROUP_START	0
#define FIRST_PAGE_GROUP_END	0x100000

#define P3P_MAX_MTU                     (9600)
#define P3P_MIN_MTU                     (68)
#define QLCNIC_MAX_ETHERHDR                32 /* This contains some padding */

#define QLCNIC_P3P_RX_BUF_MAX_LEN         (QLCNIC_MAX_ETHERHDR + ETH_DATA_LEN)
#define QLCNIC_P3P_RX_JUMBO_BUF_MAX_LEN   (QLCNIC_MAX_ETHERHDR + P3P_MAX_MTU)
#define QLCNIC_CT_DEFAULT_RX_BUF_LEN	2048
#define QLCNIC_LRO_BUFFER_EXTRA		2048

/* Tx defines */
#define QLCNIC_MAX_FRAGS_PER_TX	14
#define MAX_TSO_HEADER_DESC	2
#define MGMT_CMD_DESC_RESV	4
#define TX_STOP_THRESH		((MAX_SKB_FRAGS >> 2) + MAX_TSO_HEADER_DESC \
							+ MGMT_CMD_DESC_RESV)
#define QLCNIC_MAX_TX_TIMEOUTS	2
#define QLCNIC_MAX_TX_RINGS	8
#define QLCNIC_MAX_SDS_RINGS	8

/*
 * Following are the states of the Phantom. Phantom will set them and
 * Host will read to check if the fields are correct.
 */
#define PHAN_INITIALIZE_FAILED		0xffff
#define PHAN_INITIALIZE_COMPLETE	0xff01

/* Host writes the following to notify that it has done the init-handshake */
#define PHAN_INITIALIZE_ACK		0xf00f
#define PHAN_PEG_RCV_INITIALIZED	0xff01

#define NUM_RCV_DESC_RINGS	3

#define RCV_RING_NORMAL 0
#define RCV_RING_JUMBO	1

#define MIN_CMD_DESCRIPTORS		64
#define MIN_RCV_DESCRIPTORS		64
#define MIN_JUMBO_DESCRIPTORS		32

#define MAX_CMD_DESCRIPTORS		1024
#define MAX_RCV_DESCRIPTORS_1G		4096
#define MAX_RCV_DESCRIPTORS_10G 	8192
#define MAX_RCV_DESCRIPTORS_VF		2048
#define MAX_JUMBO_RCV_DESCRIPTORS_1G	512
#define MAX_JUMBO_RCV_DESCRIPTORS_10G	1024

#define DEFAULT_RCV_DESCRIPTORS_1G	2048
#define DEFAULT_RCV_DESCRIPTORS_10G	4096
#define DEFAULT_RCV_DESCRIPTORS_VF	1024
#define MAX_RDS_RINGS                   2

#define get_next_index(index, length)	\
	(((index) + 1) & ((length) - 1))

/*
 * Following data structures describe the descriptors that will be used.
 * Added fileds of tcpHdrSize and ipHdrSize, The driver needs to do it only when
 * we are doing LSO (above the 1500 size packet) only.
 */
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

	u8 eth_addr[ETH_ALEN];
	__le16 vlan_TCI;

} __attribute__ ((aligned(64)));

/* Note: sizeof(rcv_desc) should always be a mutliple of 2 */
struct rcv_desc {
	__le16 reference_handle;
	__le16 reserved;
	__le32 buffer_length;	/* allocated buffer length (usually 2K) */
	__le64 addr_buffer;
} __packed;

struct status_desc {
	__le64 status_desc_data[2];
} __attribute__ ((aligned(16)));

/* UNIFIED ROMIMAGE */
#define QLCNIC_UNI_FW_MIN_SIZE		0xc8000
#define QLCNIC_UNI_DIR_SECT_PRODUCT_TBL	0x0
#define QLCNIC_UNI_DIR_SECT_BOOTLD	0x6
#define QLCNIC_UNI_DIR_SECT_FW		0x7

/*Offsets */
#define QLCNIC_UNI_CHIP_REV_OFF		10
#define QLCNIC_UNI_FLAGS_OFF		11
#define QLCNIC_UNI_BIOS_VERSION_OFF 	12
#define QLCNIC_UNI_BOOTLD_IDX_OFF	27
#define QLCNIC_UNI_FIRMWARE_IDX_OFF 	29

struct uni_table_desc{
	__le32	findex;
	__le32	num_entries;
	__le32	entry_size;
	__le32	reserved[5];
};

struct uni_data_desc{
	__le32	findex;
	__le32	size;
	__le32	reserved[5];
};

/* Flash Defines and Structures */
#define QLCNIC_FLT_LOCATION	0x3F1000
#define QLCNIC_FDT_LOCATION     0x3F0000
#define QLCNIC_B0_FW_IMAGE_REGION 0x74
#define QLCNIC_C0_FW_IMAGE_REGION 0x97
#define QLCNIC_BOOTLD_REGION    0X72
struct qlcnic_flt_header {
	u16 version;
	u16 len;
	u16 checksum;
	u16 reserved;
};

struct qlcnic_flt_entry {
	u8 region;
	u8 reserved0;
	u8 attrib;
	u8 reserved1;
	u32 size;
	u32 start_addr;
	u32 end_addr;
};

/* Flash Descriptor Table */
struct qlcnic_fdt {
	u32	valid;
	u16	ver;
	u16	len;
	u16	cksum;
	u16	unused;
	u8	model[16];
	u16	mfg_id;
	u16	id;
	u8	flag;
	u8	erase_cmd;
	u8	alt_erase_cmd;
	u8	write_enable_cmd;
	u8	write_enable_bits;
	u8	write_statusreg_cmd;
	u8	unprotected_sec_cmd;
	u8	read_manuf_cmd;
	u32	block_size;
	u32	alt_block_size;
	u32	flash_size;
	u32	write_enable_data;
	u8	readid_addr_len;
	u8	write_disable_bits;
	u8	read_dev_id_len;
	u8	chip_erase_cmd;
	u16	read_timeo;
	u8	protected_sec_cmd;
	u8	resvd[65];
};
/* Magic number to let user know flash is programmed */
#define	QLCNIC_BDINFO_MAGIC 0x12345678

#define QLCNIC_BRDTYPE_P3P_REF_QG	0x0021
#define QLCNIC_BRDTYPE_P3P_HMEZ		0x0022
#define QLCNIC_BRDTYPE_P3P_10G_CX4_LP	0x0023
#define QLCNIC_BRDTYPE_P3P_4_GB		0x0024
#define QLCNIC_BRDTYPE_P3P_IMEZ		0x0025
#define QLCNIC_BRDTYPE_P3P_10G_SFP_PLUS	0x0026
#define QLCNIC_BRDTYPE_P3P_10000_BASE_T	0x0027
#define QLCNIC_BRDTYPE_P3P_XG_LOM	0x0028
#define QLCNIC_BRDTYPE_P3P_4_GB_MM	0x0029
#define QLCNIC_BRDTYPE_P3P_10G_SFP_CT	0x002a
#define QLCNIC_BRDTYPE_P3P_10G_SFP_QT	0x002b
#define QLCNIC_BRDTYPE_P3P_10G_CX4	0x0031
#define QLCNIC_BRDTYPE_P3P_10G_XFP	0x0032
#define QLCNIC_BRDTYPE_P3P_10G_TP	0x0080

#define QLCNIC_MSIX_TABLE_OFFSET	0x44

/* Flash memory map */
#define QLCNIC_BRDCFG_START	0x4000		/* board config */
#define QLCNIC_BOOTLD_START	0x10000		/* bootld */
#define QLCNIC_IMAGE_START	0x43000		/* compressed image */
#define QLCNIC_USER_START	0x3E8000	/* Firmare info */

#define QLCNIC_FW_VERSION_OFFSET	(QLCNIC_USER_START+0x408)
#define QLCNIC_FW_SIZE_OFFSET		(QLCNIC_USER_START+0x40c)
#define QLCNIC_FW_SERIAL_NUM_OFFSET	(QLCNIC_USER_START+0x81c)
#define QLCNIC_BIOS_VERSION_OFFSET	(QLCNIC_USER_START+0x83c)

#define QLCNIC_BRDTYPE_OFFSET		(QLCNIC_BRDCFG_START+0x8)
#define QLCNIC_FW_MAGIC_OFFSET		(QLCNIC_BRDCFG_START+0x128)

#define QLCNIC_FW_MIN_SIZE		(0x3fffff)
#define QLCNIC_UNIFIED_ROMIMAGE  	0
#define QLCNIC_FLASH_ROMIMAGE		1
#define QLCNIC_UNKNOWN_ROMIMAGE		0xff

#define QLCNIC_UNIFIED_ROMIMAGE_NAME	"phanfw.bin"
#define QLCNIC_FLASH_ROMIMAGE_NAME	"flash"

extern char qlcnic_driver_name[];

extern int qlcnic_use_msi;
extern int qlcnic_use_msi_x;
extern int qlcnic_auto_fw_reset;
extern int qlcnic_load_fw_file;

/* Number of status descriptors to handle per interrupt */
#define MAX_STATUS_HANDLE	(64)

/*
 * qlcnic_skb_frag{} is to contain mapping info for each SG list. This
 * has to be freed when DMA is complete. This is part of qlcnic_tx_buffer{}.
 */
struct qlcnic_skb_frag {
	u64 dma;
	u64 length;
};

/*    Following defines are for the state of the buffers    */
#define	QLCNIC_BUFFER_FREE	0
#define	QLCNIC_BUFFER_BUSY	1

/*
 * There will be one qlcnic_buffer per skb packet.    These will be
 * used to save the dma info for pci_unmap_page()
 */
struct qlcnic_cmd_buffer {
	struct sk_buff *skb;
	struct qlcnic_skb_frag frag_array[MAX_SKB_FRAGS + 1];
	u32 frag_count;
};

/* In rx_buffer, we do not need multiple fragments as is a single buffer */
struct qlcnic_rx_buffer {
	u16 ref_handle;
	struct sk_buff *skb;
	struct list_head list;
	u64 dma;
};

/* Board types */
#define	QLCNIC_GBE	0x01
#define	QLCNIC_XGBE	0x02

/*
 * Interrupt coalescing defaults. The defaults are for 1500 MTU. It is
 * adjusted based on configured MTU.
 */
#define QLCNIC_INTR_COAL_TYPE_RX		1
#define QLCNIC_INTR_COAL_TYPE_TX		2

#define QLCNIC_DEF_INTR_COALESCE_RX_TIME_US	3
#define QLCNIC_DEF_INTR_COALESCE_RX_PACKETS	256

#define QLCNIC_DEF_INTR_COALESCE_TX_TIME_US	64
#define QLCNIC_DEF_INTR_COALESCE_TX_PACKETS	64

#define QLCNIC_INTR_DEFAULT			0x04
#define QLCNIC_CONFIG_INTR_COALESCE		3
#define QLCNIC_DEV_INFO_SIZE			1

struct qlcnic_nic_intr_coalesce {
	u8	type;
	u8	sts_ring_mask;
	u16	rx_packets;
	u16	rx_time_us;
	u16	tx_packets;
	u16	tx_time_us;
	u16	flag;
	u32	timer_out;
};

struct qlcnic_dump_template_hdr {
	u32	type;
	u32	offset;
	u32	size;
	u32	cap_mask;
	u32	num_entries;
	u32	version;
	u32	timestamp;
	u32	checksum;
	u32	drv_cap_mask;
	u32	sys_info[3];
	u32	saved_state[16];
	u32	cap_sizes[8];
	u32	ocm_wnd_reg[16];
	u32	rsvd[0];
};

struct qlcnic_fw_dump {
	u8	clr;	/* flag to indicate if dump is cleared */
	u8	enable; /* enable/disable dump */
	u32	size;	/* total size of the dump */
	void	*data;	/* dump data area */
	struct	qlcnic_dump_template_hdr *tmpl_hdr;
	dma_addr_t phys_addr;
	void	*dma_buffer;
	bool	use_pex_dma;
};

/*
 * One hardware_context{} per adapter
 * contains interrupt info as well shared hardware info.
 */
struct qlcnic_hardware_context {
	void __iomem *pci_base0;
	void __iomem *ocm_win_crb;

	unsigned long pci_len0;

	rwlock_t crb_lock;
	struct mutex mem_lock;

	u8 revision_id;
	u8 pci_func;
	u8 linkup;
	u8 loopback_state;
	u8 beacon_state;
	u8 has_link_events;
	u8 fw_type;
	u8 physical_port;
	u8 reset_context;
	u8 msix_supported;
	u8 max_mac_filters;
	u8 mc_enabled;
	u8 max_mc_count;
	u8 diag_test;
	u8 num_msix;
	u8 nic_mode;
	char diag_cnt;

	u16 max_uc_count;
	u16 port_type;
	u16 board_type;
	u16 supported_type;

	u16 link_speed;
	u16 link_duplex;
	u16 link_autoneg;
	u16 module_type;

	u16 op_mode;
	u16 switch_mode;
	u16 max_tx_ques;
	u16 max_rx_ques;
	u16 max_mtu;
	u32 msg_enable;
	u16 act_pci_func;
	u16 max_pci_func;

	u32 capabilities;
	u32 extra_capability[3];
	u32 temp;
	u32 int_vec_bit;
	u32 fw_hal_version;
	u32 port_config;
	struct qlcnic_hardware_ops *hw_ops;
	struct qlcnic_nic_intr_coalesce coal;
	struct qlcnic_fw_dump fw_dump;
	struct qlcnic_fdt fdt;
	struct qlc_83xx_reset reset;
	struct qlc_83xx_idc idc;
	struct qlc_83xx_fw_info fw_info;
	struct qlcnic_intrpt_config *intr_tbl;
	struct qlcnic_sriov *sriov;
	u32 *reg_tbl;
	u32 *ext_reg_tbl;
	u32 mbox_aen[QLC_83XX_MBX_AEN_CNT];
	u32 mbox_reg[4];
	struct qlcnic_mailbox *mailbox;
	u8 extend_lb_time;
	u8 phys_port_id[ETH_ALEN];
};

struct qlcnic_adapter_stats {
	u64  xmitcalled;
	u64  xmitfinished;
	u64  rxdropped;
	u64  txdropped;
	u64  csummed;
	u64  rx_pkts;
	u64  lro_pkts;
	u64  rxbytes;
	u64  txbytes;
	u64  lrobytes;
	u64  lso_frames;
	u64  xmit_on;
	u64  xmit_off;
	u64  skb_alloc_failure;
	u64  null_rxbuf;
	u64  rx_dma_map_error;
	u64  tx_dma_map_error;
	u64  spurious_intr;
	u64  mac_filter_limit_overrun;
};

/*
 * Rcv Descriptor Context. One such per Rcv Descriptor. There may
 * be one Rcv Descriptor for normal packets, one for jumbo and may be others.
 */
struct qlcnic_host_rds_ring {
	void __iomem *crb_rcv_producer;
	struct rcv_desc *desc_head;
	struct qlcnic_rx_buffer *rx_buf_arr;
	u32 num_desc;
	u32 producer;
	u32 dma_size;
	u32 skb_size;
	u32 flags;
	struct list_head free_list;
	spinlock_t lock;
	dma_addr_t phys_addr;
} ____cacheline_internodealigned_in_smp;

struct qlcnic_host_sds_ring {
	u32 consumer;
	u32 num_desc;
	void __iomem *crb_sts_consumer;

	struct qlcnic_host_tx_ring *tx_ring;
	struct status_desc *desc_head;
	struct qlcnic_adapter *adapter;
	struct napi_struct napi;
	struct list_head free_list[NUM_RCV_DESC_RINGS];

	void __iomem *crb_intr_mask;
	int irq;

	dma_addr_t phys_addr;
	char name[IFNAMSIZ + 12];
} ____cacheline_internodealigned_in_smp;

struct qlcnic_host_tx_ring {
	int irq;
	void __iomem *crb_intr_mask;
	char name[IFNAMSIZ + 12];
	u16 ctx_id;

	u32 state;
	u32 producer;
	u32 sw_consumer;
	u32 num_desc;

	u64 xmit_on;
	u64 xmit_off;
	u64 xmit_called;
	u64 xmit_finished;

	void __iomem *crb_cmd_producer;
	struct cmd_desc_type0 *desc_head;
	struct qlcnic_adapter *adapter;
	struct napi_struct napi;
	struct qlcnic_cmd_buffer *cmd_buf_arr;
	__le32 *hw_consumer;

	dma_addr_t phys_addr;
	dma_addr_t hw_cons_phys_addr;
	struct netdev_queue *txq;
} ____cacheline_internodealigned_in_smp;

/*
 * Receive context. There is one such structure per instance of the
 * receive processing. Any state information that is relevant to
 * the receive, and is must be in this structure. The global data may be
 * present elsewhere.
 */
struct qlcnic_recv_context {
	struct qlcnic_host_rds_ring *rds_rings;
	struct qlcnic_host_sds_ring *sds_rings;
	u32 state;
	u16 context_id;
	u16 virt_port;
};

/* HW context creation */

#define QLCNIC_OS_CRB_RETRY_COUNT	4000

#define QLCNIC_CDRP_CMD_BIT		0x80000000

/*
 * All responses must have the QLCNIC_CDRP_CMD_BIT cleared
 * in the crb QLCNIC_CDRP_CRB_OFFSET.
 */
#define QLCNIC_CDRP_FORM_RSP(rsp)	(rsp)
#define QLCNIC_CDRP_IS_RSP(rsp)	(((rsp) & QLCNIC_CDRP_CMD_BIT) == 0)

#define QLCNIC_CDRP_RSP_OK		0x00000001
#define QLCNIC_CDRP_RSP_FAIL		0x00000002
#define QLCNIC_CDRP_RSP_TIMEOUT 	0x00000003

/*
 * All commands must have the QLCNIC_CDRP_CMD_BIT set in
 * the crb QLCNIC_CDRP_CRB_OFFSET.
 */
#define QLCNIC_CDRP_FORM_CMD(cmd)	(QLCNIC_CDRP_CMD_BIT | (cmd))

#define QLCNIC_RCODE_SUCCESS		0
#define QLCNIC_RCODE_INVALID_ARGS	6
#define QLCNIC_RCODE_NOT_SUPPORTED	9
#define QLCNIC_RCODE_NOT_PERMITTED	10
#define QLCNIC_RCODE_NOT_IMPL		15
#define QLCNIC_RCODE_INVALID		16
#define QLCNIC_RCODE_TIMEOUT		17
#define QLCNIC_DESTROY_CTX_RESET	0

/*
 * Capabilities Announced
 */
#define QLCNIC_CAP0_LEGACY_CONTEXT	(1)
#define QLCNIC_CAP0_LEGACY_MN		(1 << 2)
#define QLCNIC_CAP0_LSO 		(1 << 6)
#define QLCNIC_CAP0_JUMBO_CONTIGUOUS	(1 << 7)
#define QLCNIC_CAP0_LRO_CONTIGUOUS	(1 << 8)
#define QLCNIC_CAP0_VALIDOFF		(1 << 11)
#define QLCNIC_CAP0_LRO_MSS		(1 << 21)
#define QLCNIC_CAP0_TX_MULTI		(1 << 22)

/*
 * Context state
 */
#define QLCNIC_HOST_CTX_STATE_FREED	0
#define QLCNIC_HOST_CTX_STATE_ACTIVE	2

/*
 * Rx context
 */

struct qlcnic_hostrq_sds_ring {
	__le64 host_phys_addr;	/* Ring base addr */
	__le32 ring_size;		/* Ring entries */
	__le16 msi_index;
	__le16 rsvd;		/* Padding */
} __packed;

struct qlcnic_hostrq_rds_ring {
	__le64 host_phys_addr;	/* Ring base addr */
	__le64 buff_size;		/* Packet buffer size */
	__le32 ring_size;		/* Ring entries */
	__le32 ring_kind;		/* Class of ring */
} __packed;

struct qlcnic_hostrq_rx_ctx {
	__le64 host_rsp_dma_addr;	/* Response dma'd here */
	__le32 capabilities[4];		/* Flag bit vector */
	__le32 host_int_crb_mode;	/* Interrupt crb usage */
	__le32 host_rds_crb_mode;	/* RDS crb usage */
	/* These ring offsets are relative to data[0] below */
	__le32 rds_ring_offset;	/* Offset to RDS config */
	__le32 sds_ring_offset;	/* Offset to SDS config */
	__le16 num_rds_rings;	/* Count of RDS rings */
	__le16 num_sds_rings;	/* Count of SDS rings */
	__le16 valid_field_offset;
	u8  txrx_sds_binding;
	u8  msix_handler;
	u8  reserved[128];      /* reserve space for future expansion*/
	/* MUST BE 64-bit aligned.
	   The following is packed:
	   - N hostrq_rds_rings
	   - N hostrq_sds_rings */
	char data[0];
} __packed;

struct qlcnic_cardrsp_rds_ring{
	__le32 host_producer_crb;	/* Crb to use */
	__le32 rsvd1;		/* Padding */
} __packed;

struct qlcnic_cardrsp_sds_ring {
	__le32 host_consumer_crb;	/* Crb to use */
	__le32 interrupt_crb;	/* Crb to use */
} __packed;

struct qlcnic_cardrsp_rx_ctx {
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
} __packed;

#define SIZEOF_HOSTRQ_RX(HOSTRQ_RX, rds_rings, sds_rings)	\
	(sizeof(HOSTRQ_RX) + 					\
	(rds_rings)*(sizeof(struct qlcnic_hostrq_rds_ring)) +		\
	(sds_rings)*(sizeof(struct qlcnic_hostrq_sds_ring)))

#define SIZEOF_CARDRSP_RX(CARDRSP_RX, rds_rings, sds_rings) 	\
	(sizeof(CARDRSP_RX) + 					\
	(rds_rings)*(sizeof(struct qlcnic_cardrsp_rds_ring)) + 		\
	(sds_rings)*(sizeof(struct qlcnic_cardrsp_sds_ring)))

/*
 * Tx context
 */

struct qlcnic_hostrq_cds_ring {
	__le64 host_phys_addr;	/* Ring base addr */
	__le32 ring_size;		/* Ring entries */
	__le32 rsvd;		/* Padding */
} __packed;

struct qlcnic_hostrq_tx_ctx {
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
	struct qlcnic_hostrq_cds_ring cds_ring;	/* Desc of cds ring */
	u8  reserved[128];	/* future expansion */
} __packed;

struct qlcnic_cardrsp_cds_ring {
	__le32 host_producer_crb;	/* Crb to use */
	__le32 interrupt_crb;	/* Crb to use */
} __packed;

struct qlcnic_cardrsp_tx_ctx {
	__le32 host_ctx_state;	/* Starting state */
	__le16 context_id;		/* Handle for context */
	u8  phys_port;		/* Physical id of port */
	u8  virt_port;		/* Virtual/Logical id of port */
	struct qlcnic_cardrsp_cds_ring cds_ring;	/* Card cds settings */
	u8  reserved[128];	/* future expansion */
} __packed;

#define SIZEOF_HOSTRQ_TX(HOSTRQ_TX)	(sizeof(HOSTRQ_TX))
#define SIZEOF_CARDRSP_TX(CARDRSP_TX)	(sizeof(CARDRSP_TX))

/* CRB */

#define QLCNIC_HOST_RDS_CRB_MODE_UNIQUE	0
#define QLCNIC_HOST_RDS_CRB_MODE_SHARED	1
#define QLCNIC_HOST_RDS_CRB_MODE_CUSTOM	2
#define QLCNIC_HOST_RDS_CRB_MODE_MAX	3

#define QLCNIC_HOST_INT_CRB_MODE_UNIQUE	0
#define QLCNIC_HOST_INT_CRB_MODE_SHARED	1
#define QLCNIC_HOST_INT_CRB_MODE_NORX	2
#define QLCNIC_HOST_INT_CRB_MODE_NOTX	3
#define QLCNIC_HOST_INT_CRB_MODE_NORXTX	4


/* MAC */

#define MC_COUNT_P3P	38

#define QLCNIC_MAC_NOOP	0
#define QLCNIC_MAC_ADD	1
#define QLCNIC_MAC_DEL	2
#define QLCNIC_MAC_VLAN_ADD	3
#define QLCNIC_MAC_VLAN_DEL	4

struct qlcnic_mac_list_s {
	struct list_head list;
	uint8_t mac_addr[ETH_ALEN+2];
};

/* MAC Learn */
#define NO_MAC_LEARN		0
#define DRV_MAC_LEARN		1
#define FDB_MAC_LEARN		2

#define QLCNIC_HOST_REQUEST	0x13
#define QLCNIC_REQUEST		0x14

#define QLCNIC_MAC_EVENT	0x1

#define QLCNIC_IP_UP		2
#define QLCNIC_IP_DOWN		3

#define QLCNIC_ILB_MODE		0x1
#define QLCNIC_ELB_MODE		0x2

#define QLCNIC_LINKEVENT	0x1
#define QLCNIC_LB_RESPONSE	0x2
#define QLCNIC_IS_LB_CONFIGURED(VAL)	\
		(VAL == (QLCNIC_LINKEVENT | QLCNIC_LB_RESPONSE))

/*
 * Driver --> Firmware
 */
#define QLCNIC_H2C_OPCODE_CONFIG_RSS			0x1
#define QLCNIC_H2C_OPCODE_CONFIG_INTR_COALESCE		0x3
#define QLCNIC_H2C_OPCODE_CONFIG_LED			0x4
#define QLCNIC_H2C_OPCODE_LRO_REQUEST			0x7
#define QLCNIC_H2C_OPCODE_SET_MAC_RECEIVE_MODE		0xc
#define QLCNIC_H2C_OPCODE_CONFIG_IPADDR		0x12

#define QLCNIC_H2C_OPCODE_GET_LINKEVENT		0x15
#define QLCNIC_H2C_OPCODE_CONFIG_BRIDGING		0x17
#define QLCNIC_H2C_OPCODE_CONFIG_HW_LRO		0x18
#define QLCNIC_H2C_OPCODE_CONFIG_LOOPBACK		0x13

/*
 * Firmware --> Driver
 */

#define QLCNIC_C2H_OPCODE_CONFIG_LOOPBACK		0x8f
#define QLCNIC_C2H_OPCODE_GET_LINKEVENT_RESPONSE	0x8D
#define QLCNIC_C2H_OPCODE_GET_DCB_AEN			0x90

#define VPORT_MISS_MODE_DROP		0 /* drop all unmatched */
#define VPORT_MISS_MODE_ACCEPT_ALL	1 /* accept all packets */
#define VPORT_MISS_MODE_ACCEPT_MULTI	2 /* accept unmatched multicast */

#define QLCNIC_LRO_REQUEST_CLEANUP	4

/* Capabilites received */
#define QLCNIC_FW_CAPABILITY_TSO		BIT_1
#define QLCNIC_FW_CAPABILITY_BDG		BIT_8
#define QLCNIC_FW_CAPABILITY_FVLANTX		BIT_9
#define QLCNIC_FW_CAPABILITY_HW_LRO		BIT_10
#define QLCNIC_FW_CAPABILITY_2_MULTI_TX		BIT_4
#define QLCNIC_FW_CAPABILITY_MULTI_LOOPBACK	BIT_27
#define QLCNIC_FW_CAPABILITY_MORE_CAPS		BIT_31

#define QLCNIC_FW_CAPABILITY_2_LRO_MAX_TCP_SEG	BIT_2
#define QLCNIC_FW_CAP2_HW_LRO_IPV6		BIT_3
#define QLCNIC_FW_CAPABILITY_SET_DRV_VER	BIT_5
#define QLCNIC_FW_CAPABILITY_2_BEACON		BIT_7

/* module types */
#define LINKEVENT_MODULE_NOT_PRESENT			1
#define LINKEVENT_MODULE_OPTICAL_UNKNOWN		2
#define LINKEVENT_MODULE_OPTICAL_SRLR			3
#define LINKEVENT_MODULE_OPTICAL_LRM			4
#define LINKEVENT_MODULE_OPTICAL_SFP_1G 		5
#define LINKEVENT_MODULE_TWINAX_UNSUPPORTED_CABLE	6
#define LINKEVENT_MODULE_TWINAX_UNSUPPORTED_CABLELEN	7
#define LINKEVENT_MODULE_TWINAX 			8

#define LINKSPEED_10GBPS	10000
#define LINKSPEED_1GBPS 	1000
#define LINKSPEED_100MBPS	100
#define LINKSPEED_10MBPS	10

#define LINKSPEED_ENCODED_10MBPS	0
#define LINKSPEED_ENCODED_100MBPS	1
#define LINKSPEED_ENCODED_1GBPS 	2

#define LINKEVENT_AUTONEG_DISABLED	0
#define LINKEVENT_AUTONEG_ENABLED	1

#define LINKEVENT_HALF_DUPLEX		0
#define LINKEVENT_FULL_DUPLEX		1

#define LINKEVENT_LINKSPEED_MBPS	0
#define LINKEVENT_LINKSPEED_ENCODED	1

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
#define qlcnic_get_nic_msg_opcode(msg_hdr)	\
	((msg_hdr >> 32) & 0xFF)

struct qlcnic_fw_msg {
	union {
		struct {
			u64 hdr;
			u64 body[7];
		};
		u64 words[8];
	};
};

struct qlcnic_nic_req {
	__le64 qhdr;
	__le64 req_hdr;
	__le64 words[6];
} __packed;

struct qlcnic_mac_req {
	u8 op;
	u8 tag;
	u8 mac_addr[6];
};

struct qlcnic_vlan_req {
	__le16 vlan_id;
	__le16 rsvd[3];
} __packed;

struct qlcnic_ipaddr {
	__be32 ipv4;
	__be32 ipv6[4];
};

#define QLCNIC_MSI_ENABLED		0x02
#define QLCNIC_MSIX_ENABLED		0x04
#define QLCNIC_LRO_ENABLED		0x01
#define QLCNIC_LRO_DISABLED		0x00
#define QLCNIC_BRIDGE_ENABLED       	0X10
#define QLCNIC_DIAG_ENABLED		0x20
#define QLCNIC_ESWITCH_ENABLED		0x40
#define QLCNIC_ADAPTER_INITIALIZED	0x80
#define QLCNIC_TAGGING_ENABLED		0x100
#define QLCNIC_MACSPOOF			0x200
#define QLCNIC_MAC_OVERRIDE_DISABLED	0x400
#define QLCNIC_PROMISC_DISABLED		0x800
#define QLCNIC_NEED_FLR			0x1000
#define QLCNIC_FW_RESET_OWNER		0x2000
#define QLCNIC_FW_HANG			0x4000
#define QLCNIC_FW_LRO_MSS_CAP		0x8000
#define QLCNIC_TX_INTR_SHARED		0x10000
#define QLCNIC_APP_CHANGED_FLAGS	0x20000
#define QLCNIC_HAS_PHYS_PORT_ID		0x40000

#define QLCNIC_IS_MSI_FAMILY(adapter) \
	((adapter)->flags & (QLCNIC_MSI_ENABLED | QLCNIC_MSIX_ENABLED))
#define QLCNIC_IS_TSO_CAPABLE(adapter)  \
	((adapter)->ahw->capabilities & QLCNIC_FW_CAPABILITY_TSO)

#define QLCNIC_BEACON_EANBLE		0xC
#define QLCNIC_BEACON_DISABLE		0xD

#define QLCNIC_DEF_NUM_STS_DESC_RINGS	4
#define QLCNIC_DEF_NUM_TX_RINGS		4
#define QLCNIC_MSIX_TBL_SPACE		8192
#define QLCNIC_PCI_REG_MSIX_TBL 	0x44
#define QLCNIC_MSIX_TBL_PGSIZE		4096

#define QLCNIC_NETDEV_WEIGHT	128
#define QLCNIC_ADAPTER_UP_MAGIC 777

#define __QLCNIC_FW_ATTACHED		0
#define __QLCNIC_DEV_UP 		1
#define __QLCNIC_RESETTING		2
#define __QLCNIC_START_FW 		4
#define __QLCNIC_AER			5
#define __QLCNIC_DIAG_RES_ALLOC		6
#define __QLCNIC_LED_ENABLE		7
#define __QLCNIC_ELB_INPROGRESS		8
#define __QLCNIC_MULTI_TX_UNIQUE	9
#define __QLCNIC_SRIOV_ENABLE		10
#define __QLCNIC_SRIOV_CAPABLE		11
#define __QLCNIC_MBX_POLL_ENABLE	12
#define __QLCNIC_DIAG_MODE		13
#define __QLCNIC_DCB_STATE		14
#define __QLCNIC_DCB_IN_AEN		15

#define QLCNIC_INTERRUPT_TEST		1
#define QLCNIC_LOOPBACK_TEST		2
#define QLCNIC_LED_TEST		3

#define QLCNIC_FILTER_AGE	80
#define QLCNIC_READD_AGE	20
#define QLCNIC_LB_MAX_FILTERS	64
#define QLCNIC_LB_BUCKET_SIZE	32
#define QLCNIC_ILB_MAX_RCV_LOOP	10

struct qlcnic_filter {
	struct hlist_node fnode;
	u8 faddr[ETH_ALEN];
	u16 vlan_id;
	unsigned long ftime;
};

struct qlcnic_filter_hash {
	struct hlist_head *fhead;
	u8 fnum;
	u16 fmax;
	u16 fbucket_size;
};

/* Mailbox specific data structures */
struct qlcnic_mailbox {
	struct workqueue_struct	*work_q;
	struct qlcnic_adapter	*adapter;
	struct qlcnic_mbx_ops	*ops;
	struct work_struct	work;
	struct completion	completion;
	struct list_head	cmd_q;
	unsigned long		status;
	spinlock_t		queue_lock;	/* Mailbox queue lock */
	spinlock_t		aen_lock;	/* Mailbox response/AEN lock */
	atomic_t		rsp_status;
	u32			num_cmds;
};

struct qlcnic_adapter {
	struct qlcnic_hardware_context *ahw;
	struct qlcnic_recv_context *recv_ctx;
	struct qlcnic_host_tx_ring *tx_ring;
	struct net_device *netdev;
	struct pci_dev *pdev;

	unsigned long state;
	u32 flags;

	int max_drv_tx_rings;
	u16 num_txd;
	u16 num_rxd;
	u16 num_jumbo_rxd;
	u16 max_rxd;
	u16 max_jumbo_rxd;

	u8 max_rds_rings;
	u8 max_sds_rings;
	u8 rx_csum;
	u8 portnum;

	u8 fw_wait_cnt;
	u8 fw_fail_cnt;
	u8 tx_timeo_cnt;
	u8 need_fw_reset;
	u8 reset_ctx_cnt;

	u16 is_up;
	u16 rx_pvid;
	u16 tx_pvid;

	u32 irq;
	u32 heartbeat;

	u8 dev_state;
	u8 reset_ack_timeo;
	u8 dev_init_timeo;

	u8 mac_addr[ETH_ALEN];

	u64 dev_rst_time;
	bool drv_mac_learn;
	bool fdb_mac_learn;
	unsigned long vlans[BITS_TO_LONGS(VLAN_N_VID)];
	u8 flash_mfg_id;
	struct qlcnic_npar_info *npars;
	struct qlcnic_eswitch *eswitch;
	struct qlcnic_nic_template *nic_ops;

	struct qlcnic_adapter_stats stats;
	struct list_head mac_list;

	void __iomem	*tgt_mask_reg;
	void __iomem	*tgt_status_reg;
	void __iomem	*crb_int_state_reg;
	void __iomem	*isr_int_vec;

	struct msix_entry *msix_entries;
	struct workqueue_struct *qlcnic_wq;
	struct delayed_work fw_work;
	struct delayed_work idc_aen_work;
	struct delayed_work mbx_poll_work;
	struct qlcnic_dcb *dcb;

	struct qlcnic_filter_hash fhash;
	struct qlcnic_filter_hash rx_fhash;
	struct list_head vf_mc_list;

	spinlock_t tx_clean_lock;
	spinlock_t mac_learn_lock;
	/* spinlock for catching rcv filters for eswitch traffic */
	spinlock_t rx_mac_learn_lock;
	u32 file_prd_off;	/*File fw product offset*/
	u32 fw_version;
	u32 offload_flags;
	const struct firmware *fw;
};

struct qlcnic_info_le {
	__le16	pci_func;
	__le16	op_mode;	/* 1 = Priv, 2 = NP, 3 = NP passthru */
	__le16	phys_port;
	__le16	switch_mode;	/* 0 = disabled, 1 = int, 2 = ext */

	__le32	capabilities;
	u8	max_mac_filters;
	u8	reserved1;
	__le16	max_mtu;

	__le16	max_tx_ques;
	__le16	max_rx_ques;
	__le16	min_tx_bw;
	__le16	max_tx_bw;
	__le32  op_type;
	__le16  max_bw_reg_offset;
	__le16  max_linkspeed_reg_offset;
	__le32  capability1;
	__le32  capability2;
	__le32  capability3;
	__le16  max_tx_mac_filters;
	__le16  max_rx_mcast_mac_filters;
	__le16  max_rx_ucast_mac_filters;
	__le16  max_rx_ip_addr;
	__le16  max_rx_lro_flow;
	__le16  max_rx_status_rings;
	__le16  max_rx_buf_rings;
	__le16  max_tx_vlan_keys;
	u8      total_pf;
	u8      total_rss_engines;
	__le16  max_vports;
	__le16	linkstate_reg_offset;
	__le16	bit_offsets;
	__le16  max_local_ipv6_addrs;
	__le16  max_remote_ipv6_addrs;
	u8	reserved2[56];
} __packed;

struct qlcnic_info {
	u16	pci_func;
	u16	op_mode;
	u16	phys_port;
	u16	switch_mode;
	u32	capabilities;
	u8	max_mac_filters;
	u16	max_mtu;
	u16	max_tx_ques;
	u16	max_rx_ques;
	u16	min_tx_bw;
	u16	max_tx_bw;
	u32	op_type;
	u16	max_bw_reg_offset;
	u16	max_linkspeed_reg_offset;
	u32	capability1;
	u32	capability2;
	u32	capability3;
	u16	max_tx_mac_filters;
	u16	max_rx_mcast_mac_filters;
	u16	max_rx_ucast_mac_filters;
	u16	max_rx_ip_addr;
	u16	max_rx_lro_flow;
	u16	max_rx_status_rings;
	u16	max_rx_buf_rings;
	u16	max_tx_vlan_keys;
	u8      total_pf;
	u8      total_rss_engines;
	u16	max_vports;
	u16	linkstate_reg_offset;
	u16	bit_offsets;
	u16	max_local_ipv6_addrs;
	u16	max_remote_ipv6_addrs;
};

struct qlcnic_pci_info_le {
	__le16	id;		/* pci function id */
	__le16	active;		/* 1 = Enabled */
	__le16	type;		/* 1 = NIC, 2 = FCoE, 3 = iSCSI */
	__le16	default_port;	/* default port number */

	__le16	tx_min_bw;	/* Multiple of 100mbpc */
	__le16	tx_max_bw;
	__le16	reserved1[2];

	u8	mac[ETH_ALEN];
	__le16  func_count;
	u8      reserved2[104];

} __packed;

struct qlcnic_pci_info {
	u16	id;
	u16	active;
	u16	type;
	u16	default_port;
	u16	tx_min_bw;
	u16	tx_max_bw;
	u8	mac[ETH_ALEN];
	u16  func_count;
};

struct qlcnic_npar_info {
	u16	pvid;
	u16	min_bw;
	u16	max_bw;
	u8	phy_port;
	u8	type;
	u8	active;
	u8	enable_pm;
	u8	dest_npar;
	u8	discard_tagged;
	u8	mac_override;
	u8	mac_anti_spoof;
	u8	promisc_mode;
	u8	offload_flags;
	u8      pci_func;
};

struct qlcnic_eswitch {
	u8	port;
	u8	active_vports;
	u8	active_vlans;
	u8	active_ucast_filters;
	u8	max_ucast_filters;
	u8	max_active_vlans;

	u32	flags;
#define QLCNIC_SWITCH_ENABLE		BIT_1
#define QLCNIC_SWITCH_VLAN_FILTERING	BIT_2
#define QLCNIC_SWITCH_PROMISC_MODE	BIT_3
#define QLCNIC_SWITCH_PORT_MIRRORING	BIT_4
};


/* Return codes for Error handling */
#define QL_STATUS_INVALID_PARAM	-1

#define MAX_BW			100	/* % of link speed */
#define MAX_VLAN_ID		4095
#define MIN_VLAN_ID		2
#define DEFAULT_MAC_LEARN	1

#define IS_VALID_VLAN(vlan)	(vlan >= MIN_VLAN_ID && vlan < MAX_VLAN_ID)
#define IS_VALID_BW(bw)		(bw <= MAX_BW)

struct qlcnic_pci_func_cfg {
	u16	func_type;
	u16	min_bw;
	u16	max_bw;
	u16	port_num;
	u8	pci_func;
	u8	func_state;
	u8	def_mac_addr[6];
};

struct qlcnic_npar_func_cfg {
	u32	fw_capab;
	u16	port_num;
	u16	min_bw;
	u16	max_bw;
	u16	max_tx_queues;
	u16	max_rx_queues;
	u8	pci_func;
	u8	op_mode;
};

struct qlcnic_pm_func_cfg {
	u8	pci_func;
	u8	action;
	u8	dest_npar;
	u8	reserved[5];
};

struct qlcnic_esw_func_cfg {
	u16	vlan_id;
	u8	op_mode;
	u8	op_type;
	u8	pci_func;
	u8	host_vlan_tag;
	u8	promisc_mode;
	u8	discard_tagged;
	u8	mac_override;
	u8	mac_anti_spoof;
	u8	offload_flags;
	u8	reserved[5];
};

#define QLCNIC_STATS_VERSION		1
#define QLCNIC_STATS_PORT		1
#define QLCNIC_STATS_ESWITCH		2
#define QLCNIC_QUERY_RX_COUNTER		0
#define QLCNIC_QUERY_TX_COUNTER		1
#define QLCNIC_STATS_NOT_AVAIL	0xffffffffffffffffULL
#define QLCNIC_FILL_STATS(VAL1) \
	(((VAL1) == QLCNIC_STATS_NOT_AVAIL) ? 0 : VAL1)
#define QLCNIC_MAC_STATS 1
#define QLCNIC_ESW_STATS 2

#define QLCNIC_ADD_ESW_STATS(VAL1, VAL2)\
do {	\
	if (((VAL1) == QLCNIC_STATS_NOT_AVAIL) && \
	    ((VAL2) != QLCNIC_STATS_NOT_AVAIL)) \
		(VAL1) = (VAL2); \
	else if (((VAL1) != QLCNIC_STATS_NOT_AVAIL) && \
		 ((VAL2) != QLCNIC_STATS_NOT_AVAIL)) \
			(VAL1) += (VAL2); \
} while (0)

struct qlcnic_mac_statistics_le {
	__le64	mac_tx_frames;
	__le64	mac_tx_bytes;
	__le64	mac_tx_mcast_pkts;
	__le64	mac_tx_bcast_pkts;
	__le64	mac_tx_pause_cnt;
	__le64	mac_tx_ctrl_pkt;
	__le64	mac_tx_lt_64b_pkts;
	__le64	mac_tx_lt_127b_pkts;
	__le64	mac_tx_lt_255b_pkts;
	__le64	mac_tx_lt_511b_pkts;
	__le64	mac_tx_lt_1023b_pkts;
	__le64	mac_tx_lt_1518b_pkts;
	__le64	mac_tx_gt_1518b_pkts;
	__le64	rsvd1[3];

	__le64	mac_rx_frames;
	__le64	mac_rx_bytes;
	__le64	mac_rx_mcast_pkts;
	__le64	mac_rx_bcast_pkts;
	__le64	mac_rx_pause_cnt;
	__le64	mac_rx_ctrl_pkt;
	__le64	mac_rx_lt_64b_pkts;
	__le64	mac_rx_lt_127b_pkts;
	__le64	mac_rx_lt_255b_pkts;
	__le64	mac_rx_lt_511b_pkts;
	__le64	mac_rx_lt_1023b_pkts;
	__le64	mac_rx_lt_1518b_pkts;
	__le64	mac_rx_gt_1518b_pkts;
	__le64	rsvd2[3];

	__le64	mac_rx_length_error;
	__le64	mac_rx_length_small;
	__le64	mac_rx_length_large;
	__le64	mac_rx_jabber;
	__le64	mac_rx_dropped;
	__le64	mac_rx_crc_error;
	__le64	mac_align_error;
} __packed;

struct qlcnic_mac_statistics {
	u64	mac_tx_frames;
	u64	mac_tx_bytes;
	u64	mac_tx_mcast_pkts;
	u64	mac_tx_bcast_pkts;
	u64	mac_tx_pause_cnt;
	u64	mac_tx_ctrl_pkt;
	u64	mac_tx_lt_64b_pkts;
	u64	mac_tx_lt_127b_pkts;
	u64	mac_tx_lt_255b_pkts;
	u64	mac_tx_lt_511b_pkts;
	u64	mac_tx_lt_1023b_pkts;
	u64	mac_tx_lt_1518b_pkts;
	u64	mac_tx_gt_1518b_pkts;
	u64	rsvd1[3];
	u64	mac_rx_frames;
	u64	mac_rx_bytes;
	u64	mac_rx_mcast_pkts;
	u64	mac_rx_bcast_pkts;
	u64	mac_rx_pause_cnt;
	u64	mac_rx_ctrl_pkt;
	u64	mac_rx_lt_64b_pkts;
	u64	mac_rx_lt_127b_pkts;
	u64	mac_rx_lt_255b_pkts;
	u64	mac_rx_lt_511b_pkts;
	u64	mac_rx_lt_1023b_pkts;
	u64	mac_rx_lt_1518b_pkts;
	u64	mac_rx_gt_1518b_pkts;
	u64	rsvd2[3];
	u64	mac_rx_length_error;
	u64	mac_rx_length_small;
	u64	mac_rx_length_large;
	u64	mac_rx_jabber;
	u64	mac_rx_dropped;
	u64	mac_rx_crc_error;
	u64	mac_align_error;
};

struct qlcnic_esw_stats_le {
	__le16 context_id;
	__le16 version;
	__le16 size;
	__le16 unused;
	__le64 unicast_frames;
	__le64 multicast_frames;
	__le64 broadcast_frames;
	__le64 dropped_frames;
	__le64 errors;
	__le64 local_frames;
	__le64 numbytes;
	__le64 rsvd[3];
} __packed;

struct __qlcnic_esw_statistics {
	u16	context_id;
	u16	version;
	u16	size;
	u16	unused;
	u64	unicast_frames;
	u64	multicast_frames;
	u64	broadcast_frames;
	u64	dropped_frames;
	u64	errors;
	u64	local_frames;
	u64	numbytes;
	u64	rsvd[3];
};

struct qlcnic_esw_statistics {
	struct __qlcnic_esw_statistics rx;
	struct __qlcnic_esw_statistics tx;
};

#define QLCNIC_DUMP_MASK_DEF		0x1f
#define QLCNIC_FORCE_FW_DUMP_KEY	0xdeadfeed
#define QLCNIC_ENABLE_FW_DUMP		0xaddfeed
#define QLCNIC_DISABLE_FW_DUMP		0xbadfeed
#define QLCNIC_FORCE_FW_RESET		0xdeaddead
#define QLCNIC_SET_QUIESCENT		0xadd00010
#define QLCNIC_RESET_QUIESCENT		0xadd00020

struct _cdrp_cmd {
	u32 num;
	u32 *arg;
};

struct qlcnic_cmd_args {
	struct completion	completion;
	struct list_head	list;
	struct _cdrp_cmd	req;
	struct _cdrp_cmd	rsp;
	atomic_t		rsp_status;
	int			pay_size;
	u32			rsp_opcode;
	u32			total_cmds;
	u32			op_type;
	u32			type;
	u32			cmd_op;
	u32			*hdr;	/* Back channel message header */
	u32			*pay;	/* Back channel message payload */
	u8			func_num;
};

int qlcnic_fw_cmd_get_minidump_temp(struct qlcnic_adapter *adapter);
int qlcnic_fw_cmd_set_port(struct qlcnic_adapter *adapter, u32 config);
int qlcnic_pci_mem_write_2M(struct qlcnic_adapter *, u64 off, u64 data);
int qlcnic_pci_mem_read_2M(struct qlcnic_adapter *, u64 off, u64 *data);
void qlcnic_pci_camqm_read_2M(struct qlcnic_adapter *, u64, u64 *);
void qlcnic_pci_camqm_write_2M(struct qlcnic_adapter *, u64, u64);

#define ADDR_IN_RANGE(addr, low, high)	\
	(((addr) < (high)) && ((addr) >= (low)))

#define QLCRD32(adapter, off, err) \
	(adapter->ahw->hw_ops->read_reg)(adapter, off, err)

#define QLCWR32(adapter, off, val) \
	adapter->ahw->hw_ops->write_reg(adapter, off, val)

int qlcnic_pcie_sem_lock(struct qlcnic_adapter *, int, u32);
void qlcnic_pcie_sem_unlock(struct qlcnic_adapter *, int);

#define qlcnic_rom_lock(a)	\
	qlcnic_pcie_sem_lock((a), 2, QLCNIC_ROM_LOCK_ID)
#define qlcnic_rom_unlock(a)	\
	qlcnic_pcie_sem_unlock((a), 2)
#define qlcnic_phy_lock(a)	\
	qlcnic_pcie_sem_lock((a), 3, QLCNIC_PHY_LOCK_ID)
#define qlcnic_phy_unlock(a)	\
	qlcnic_pcie_sem_unlock((a), 3)
#define qlcnic_sw_lock(a)	\
	qlcnic_pcie_sem_lock((a), 6, 0)
#define qlcnic_sw_unlock(a)	\
	qlcnic_pcie_sem_unlock((a), 6)
#define crb_win_lock(a)	\
	qlcnic_pcie_sem_lock((a), 7, QLCNIC_CRB_WIN_LOCK_ID)
#define crb_win_unlock(a)	\
	qlcnic_pcie_sem_unlock((a), 7)

#define __QLCNIC_MAX_LED_RATE	0xf
#define __QLCNIC_MAX_LED_STATE	0x2

#define MAX_CTL_CHECK 1000

int qlcnic_wol_supported(struct qlcnic_adapter *adapter);
void qlcnic_prune_lb_filters(struct qlcnic_adapter *adapter);
void qlcnic_delete_lb_filters(struct qlcnic_adapter *adapter);
int qlcnic_dump_fw(struct qlcnic_adapter *);

/* Functions from qlcnic_init.c */
void qlcnic_schedule_work(struct qlcnic_adapter *, work_func_t, int);
int qlcnic_load_firmware(struct qlcnic_adapter *adapter);
int qlcnic_need_fw_reset(struct qlcnic_adapter *adapter);
void qlcnic_request_firmware(struct qlcnic_adapter *adapter);
void qlcnic_release_firmware(struct qlcnic_adapter *adapter);
int qlcnic_pinit_from_rom(struct qlcnic_adapter *adapter);
int qlcnic_setup_idc_param(struct qlcnic_adapter *adapter);
int qlcnic_check_flash_fw_ver(struct qlcnic_adapter *adapter);

int qlcnic_rom_fast_read(struct qlcnic_adapter *adapter, u32 addr, u32 *valp);
int qlcnic_rom_fast_read_words(struct qlcnic_adapter *adapter, int addr,
				u8 *bytes, size_t size);
int qlcnic_alloc_sw_resources(struct qlcnic_adapter *adapter);
void qlcnic_free_sw_resources(struct qlcnic_adapter *adapter);

void __iomem *qlcnic_get_ioaddr(struct qlcnic_hardware_context *, u32);

int qlcnic_alloc_hw_resources(struct qlcnic_adapter *adapter);
void qlcnic_free_hw_resources(struct qlcnic_adapter *adapter);

int qlcnic_fw_create_ctx(struct qlcnic_adapter *adapter);
void qlcnic_fw_destroy_ctx(struct qlcnic_adapter *adapter);

void qlcnic_reset_rx_buffers_list(struct qlcnic_adapter *adapter);
void qlcnic_release_rx_buffers(struct qlcnic_adapter *adapter);
void qlcnic_release_tx_buffers(struct qlcnic_adapter *,
			       struct qlcnic_host_tx_ring *);

int qlcnic_check_fw_status(struct qlcnic_adapter *adapter);
void qlcnic_watchdog_task(struct work_struct *work);
void qlcnic_post_rx_buffers(struct qlcnic_adapter *adapter,
		struct qlcnic_host_rds_ring *rds_ring, u8 ring_id);
int qlcnic_process_rcv_ring(struct qlcnic_host_sds_ring *sds_ring, int max);
void qlcnic_set_multi(struct net_device *netdev);
void __qlcnic_set_multi(struct net_device *, u16);
int qlcnic_nic_add_mac(struct qlcnic_adapter *, const u8 *, u16);
int qlcnic_nic_del_mac(struct qlcnic_adapter *, const u8 *);
void qlcnic_82xx_free_mac_list(struct qlcnic_adapter *adapter);
int qlcnic_82xx_read_phys_port_id(struct qlcnic_adapter *);

int qlcnic_fw_cmd_set_mtu(struct qlcnic_adapter *adapter, int mtu);
int qlcnic_fw_cmd_set_drv_version(struct qlcnic_adapter *, u32);
int qlcnic_change_mtu(struct net_device *netdev, int new_mtu);
netdev_features_t qlcnic_fix_features(struct net_device *netdev,
	netdev_features_t features);
int qlcnic_set_features(struct net_device *netdev, netdev_features_t features);
int qlcnic_config_bridged_mode(struct qlcnic_adapter *adapter, u32 enable);
int qlcnic_send_lro_cleanup(struct qlcnic_adapter *adapter);
void qlcnic_update_cmd_producer(struct qlcnic_host_tx_ring *);

/* Functions from qlcnic_ethtool.c */
int qlcnic_check_loopback_buff(unsigned char *, u8 []);
int qlcnic_do_lb_test(struct qlcnic_adapter *, u8);
int qlcnic_loopback_test(struct net_device *, u8);

/* Functions from qlcnic_main.c */
int qlcnic_reset_context(struct qlcnic_adapter *);
void qlcnic_diag_free_res(struct net_device *netdev, int max_sds_rings);
int qlcnic_diag_alloc_res(struct net_device *netdev, int test);
netdev_tx_t qlcnic_xmit_frame(struct sk_buff *skb, struct net_device *netdev);
int qlcnic_set_max_rss(struct qlcnic_adapter *, u8, int);
int qlcnic_validate_max_rss(struct qlcnic_adapter *, __u32);
int qlcnic_validate_max_tx_rings(struct qlcnic_adapter *, u32 txq);
void qlcnic_alloc_lb_filters_mem(struct qlcnic_adapter *adapter);
void qlcnic_82xx_set_mac_filter_count(struct qlcnic_adapter *);
int qlcnic_enable_msix(struct qlcnic_adapter *, u32);
void qlcnic_set_drv_version(struct qlcnic_adapter *);

/*  eSwitch management functions */
int qlcnic_config_switch_port(struct qlcnic_adapter *,
				struct qlcnic_esw_func_cfg *);

int qlcnic_get_eswitch_port_config(struct qlcnic_adapter *,
				struct qlcnic_esw_func_cfg *);
int qlcnic_config_port_mirroring(struct qlcnic_adapter *, u8, u8, u8);
int qlcnic_get_port_stats(struct qlcnic_adapter *, const u8, const u8,
					struct __qlcnic_esw_statistics *);
int qlcnic_get_eswitch_stats(struct qlcnic_adapter *, const u8, u8,
					struct __qlcnic_esw_statistics *);
int qlcnic_clear_esw_stats(struct qlcnic_adapter *adapter, u8, u8, u8);
int qlcnic_get_mac_stats(struct qlcnic_adapter *, struct qlcnic_mac_statistics *);

void qlcnic_free_mbx_args(struct qlcnic_cmd_args *cmd);

int qlcnic_alloc_sds_rings(struct qlcnic_recv_context *, int);
void qlcnic_free_sds_rings(struct qlcnic_recv_context *);
void qlcnic_advert_link_change(struct qlcnic_adapter *, int);
void qlcnic_free_tx_rings(struct qlcnic_adapter *);
int qlcnic_alloc_tx_rings(struct qlcnic_adapter *, struct net_device *);
void qlcnic_dump_mbx(struct qlcnic_adapter *, struct qlcnic_cmd_args *);

void qlcnic_create_sysfs_entries(struct qlcnic_adapter *adapter);
void qlcnic_remove_sysfs_entries(struct qlcnic_adapter *adapter);
void qlcnic_create_diag_entries(struct qlcnic_adapter *adapter);
void qlcnic_remove_diag_entries(struct qlcnic_adapter *adapter);
void qlcnic_82xx_add_sysfs(struct qlcnic_adapter *adapter);
void qlcnic_82xx_remove_sysfs(struct qlcnic_adapter *adapter);
int qlcnic_82xx_get_settings(struct qlcnic_adapter *, struct ethtool_cmd *);

int qlcnicvf_config_bridged_mode(struct qlcnic_adapter *, u32);
int qlcnicvf_config_led(struct qlcnic_adapter *, u32, u32);
void qlcnic_set_vlan_config(struct qlcnic_adapter *,
			    struct qlcnic_esw_func_cfg *);
void qlcnic_set_eswitch_port_features(struct qlcnic_adapter *,
				      struct qlcnic_esw_func_cfg *);

void qlcnic_down(struct qlcnic_adapter *, struct net_device *);
int qlcnic_up(struct qlcnic_adapter *, struct net_device *);
void __qlcnic_down(struct qlcnic_adapter *, struct net_device *);
void qlcnic_detach(struct qlcnic_adapter *);
void qlcnic_teardown_intr(struct qlcnic_adapter *);
int qlcnic_attach(struct qlcnic_adapter *);
int __qlcnic_up(struct qlcnic_adapter *, struct net_device *);
void qlcnic_restore_indev_addr(struct net_device *, unsigned long);

int qlcnic_check_temp(struct qlcnic_adapter *);
int qlcnic_init_pci_info(struct qlcnic_adapter *);
int qlcnic_set_default_offload_settings(struct qlcnic_adapter *);
int qlcnic_reset_npar_config(struct qlcnic_adapter *);
int qlcnic_set_eswitch_port_config(struct qlcnic_adapter *);
void qlcnic_add_lb_filter(struct qlcnic_adapter *, struct sk_buff *, int, u16);
int qlcnic_get_beacon_state(struct qlcnic_adapter *, u8 *);
int qlcnic_83xx_configure_opmode(struct qlcnic_adapter *adapter);
int qlcnic_read_mac_addr(struct qlcnic_adapter *);
int qlcnic_setup_netdev(struct qlcnic_adapter *, struct net_device *, int);
void qlcnic_set_netdev_features(struct qlcnic_adapter *,
				struct qlcnic_esw_func_cfg *);
void qlcnic_sriov_vf_schedule_multi(struct net_device *);
void qlcnic_vf_add_mc_list(struct net_device *, u16);

/*
 * QLOGIC Board information
 */

#define QLCNIC_MAX_BOARD_NAME_LEN 100
struct qlcnic_board_info {
	unsigned short  vendor;
	unsigned short  device;
	unsigned short  sub_vendor;
	unsigned short  sub_device;
	char short_name[QLCNIC_MAX_BOARD_NAME_LEN];
};

static inline u32 qlcnic_tx_avail(struct qlcnic_host_tx_ring *tx_ring)
{
	if (likely(tx_ring->producer < tx_ring->sw_consumer))
		return tx_ring->sw_consumer - tx_ring->producer;
	else
		return tx_ring->sw_consumer + tx_ring->num_desc -
				tx_ring->producer;
}

static inline int qlcnic_set_real_num_queues(struct qlcnic_adapter *adapter,
					     struct net_device *netdev)
{
	int err, tx_q;

	tx_q = adapter->max_drv_tx_rings;

	netdev->num_tx_queues = tx_q;
	netdev->real_num_tx_queues = tx_q;

	err = netif_set_real_num_tx_queues(netdev, tx_q);
	if (err)
		dev_err(&adapter->pdev->dev, "failed to set %d Tx queues\n",
			tx_q);
	else
		dev_info(&adapter->pdev->dev, "set %d Tx queues\n", tx_q);

	return err;
}

struct qlcnic_nic_template {
	int (*config_bridged_mode) (struct qlcnic_adapter *, u32);
	int (*config_led) (struct qlcnic_adapter *, u32, u32);
	int (*start_firmware) (struct qlcnic_adapter *);
	int (*init_driver) (struct qlcnic_adapter *);
	void (*request_reset) (struct qlcnic_adapter *, u32);
	void (*cancel_idc_work) (struct qlcnic_adapter *);
	int (*napi_add)(struct qlcnic_adapter *, struct net_device *);
	void (*napi_del)(struct qlcnic_adapter *);
	void (*config_ipaddr)(struct qlcnic_adapter *, __be32, int);
	irqreturn_t (*clear_legacy_intr)(struct qlcnic_adapter *);
	int (*shutdown)(struct pci_dev *);
	int (*resume)(struct qlcnic_adapter *);
};

struct qlcnic_mbx_ops {
	int (*enqueue_cmd) (struct qlcnic_adapter *,
			    struct qlcnic_cmd_args *, unsigned long *);
	void (*dequeue_cmd) (struct qlcnic_adapter *, struct qlcnic_cmd_args *);
	void (*decode_resp) (struct qlcnic_adapter *, struct qlcnic_cmd_args *);
	void (*encode_cmd) (struct qlcnic_adapter *, struct qlcnic_cmd_args *);
	void (*nofity_fw) (struct qlcnic_adapter *, u8);
};

int qlcnic_83xx_init_mailbox_work(struct qlcnic_adapter *);
void qlcnic_83xx_detach_mailbox_work(struct qlcnic_adapter *);
void qlcnic_83xx_reinit_mbx_work(struct qlcnic_mailbox *mbx);
void qlcnic_83xx_free_mailbox(struct qlcnic_mailbox *mbx);

/* Adapter hardware abstraction */
struct qlcnic_hardware_ops {
	void (*read_crb) (struct qlcnic_adapter *, char *, loff_t, size_t);
	void (*write_crb) (struct qlcnic_adapter *, char *, loff_t, size_t);
	int (*read_reg) (struct qlcnic_adapter *, ulong, int *);
	int (*write_reg) (struct qlcnic_adapter *, ulong, u32);
	void (*get_ocm_win) (struct qlcnic_hardware_context *);
	int (*get_mac_address) (struct qlcnic_adapter *, u8 *, u8);
	int (*setup_intr) (struct qlcnic_adapter *, u8, int);
	int (*alloc_mbx_args)(struct qlcnic_cmd_args *,
			      struct qlcnic_adapter *, u32);
	int (*mbx_cmd) (struct qlcnic_adapter *, struct qlcnic_cmd_args *);
	void (*get_func_no) (struct qlcnic_adapter *);
	int (*api_lock) (struct qlcnic_adapter *);
	void (*api_unlock) (struct qlcnic_adapter *);
	void (*add_sysfs) (struct qlcnic_adapter *);
	void (*remove_sysfs) (struct qlcnic_adapter *);
	void (*process_lb_rcv_ring_diag) (struct qlcnic_host_sds_ring *);
	int (*create_rx_ctx) (struct qlcnic_adapter *);
	int (*create_tx_ctx) (struct qlcnic_adapter *,
	struct qlcnic_host_tx_ring *, int);
	void (*del_rx_ctx) (struct qlcnic_adapter *);
	void (*del_tx_ctx) (struct qlcnic_adapter *,
			    struct qlcnic_host_tx_ring *);
	int (*setup_link_event) (struct qlcnic_adapter *, int);
	int (*get_nic_info) (struct qlcnic_adapter *, struct qlcnic_info *, u8);
	int (*get_pci_info) (struct qlcnic_adapter *, struct qlcnic_pci_info *);
	int (*set_nic_info) (struct qlcnic_adapter *, struct qlcnic_info *);
	int (*change_macvlan) (struct qlcnic_adapter *, u8*, u16, u8);
	void (*napi_enable) (struct qlcnic_adapter *);
	void (*napi_disable) (struct qlcnic_adapter *);
	void (*config_intr_coal) (struct qlcnic_adapter *);
	int (*config_rss) (struct qlcnic_adapter *, int);
	int (*config_hw_lro) (struct qlcnic_adapter *, int);
	int (*config_loopback) (struct qlcnic_adapter *, u8);
	int (*clear_loopback) (struct qlcnic_adapter *, u8);
	int (*config_promisc_mode) (struct qlcnic_adapter *, u32);
	void (*change_l2_filter) (struct qlcnic_adapter *, u64 *, u16);
	int (*get_board_info) (struct qlcnic_adapter *);
	void (*set_mac_filter_count) (struct qlcnic_adapter *);
	void (*free_mac_list) (struct qlcnic_adapter *);
	int (*read_phys_port_id) (struct qlcnic_adapter *);
};

extern struct qlcnic_nic_template qlcnic_vf_ops;

static inline int qlcnic_start_firmware(struct qlcnic_adapter *adapter)
{
	return adapter->nic_ops->start_firmware(adapter);
}

static inline void qlcnic_read_crb(struct qlcnic_adapter *adapter, char *buf,
				   loff_t offset, size_t size)
{
	adapter->ahw->hw_ops->read_crb(adapter, buf, offset, size);
}

static inline void qlcnic_write_crb(struct qlcnic_adapter *adapter, char *buf,
				    loff_t offset, size_t size)
{
	adapter->ahw->hw_ops->write_crb(adapter, buf, offset, size);
}

static inline int qlcnic_hw_write_wx_2M(struct qlcnic_adapter *adapter,
					ulong off, u32 data)
{
	return adapter->ahw->hw_ops->write_reg(adapter, off, data);
}

static inline int qlcnic_get_mac_address(struct qlcnic_adapter *adapter,
					 u8 *mac, u8 function)
{
	return adapter->ahw->hw_ops->get_mac_address(adapter, mac, function);
}

static inline int qlcnic_setup_intr(struct qlcnic_adapter *adapter,
				    u8 num_intr, int txq)
{
	return adapter->ahw->hw_ops->setup_intr(adapter, num_intr, txq);
}

static inline int qlcnic_alloc_mbx_args(struct qlcnic_cmd_args *mbx,
					struct qlcnic_adapter *adapter, u32 arg)
{
	return adapter->ahw->hw_ops->alloc_mbx_args(mbx, adapter, arg);
}

static inline int qlcnic_issue_cmd(struct qlcnic_adapter *adapter,
				   struct qlcnic_cmd_args *cmd)
{
	if (adapter->ahw->hw_ops->mbx_cmd)
		return adapter->ahw->hw_ops->mbx_cmd(adapter, cmd);

	return -EIO;
}

static inline void qlcnic_get_func_no(struct qlcnic_adapter *adapter)
{
	adapter->ahw->hw_ops->get_func_no(adapter);
}

static inline int qlcnic_api_lock(struct qlcnic_adapter *adapter)
{
	return adapter->ahw->hw_ops->api_lock(adapter);
}

static inline void qlcnic_api_unlock(struct qlcnic_adapter *adapter)
{
	adapter->ahw->hw_ops->api_unlock(adapter);
}

static inline void qlcnic_add_sysfs(struct qlcnic_adapter *adapter)
{
	if (adapter->ahw->hw_ops->add_sysfs)
		adapter->ahw->hw_ops->add_sysfs(adapter);
}

static inline void qlcnic_remove_sysfs(struct qlcnic_adapter *adapter)
{
	if (adapter->ahw->hw_ops->remove_sysfs)
		adapter->ahw->hw_ops->remove_sysfs(adapter);
}

static inline void
qlcnic_process_rcv_ring_diag(struct qlcnic_host_sds_ring *sds_ring)
{
	sds_ring->adapter->ahw->hw_ops->process_lb_rcv_ring_diag(sds_ring);
}

static inline int qlcnic_fw_cmd_create_rx_ctx(struct qlcnic_adapter *adapter)
{
	return adapter->ahw->hw_ops->create_rx_ctx(adapter);
}

static inline int qlcnic_fw_cmd_create_tx_ctx(struct qlcnic_adapter *adapter,
					      struct qlcnic_host_tx_ring *ptr,
					      int ring)
{
	return adapter->ahw->hw_ops->create_tx_ctx(adapter, ptr, ring);
}

static inline void qlcnic_fw_cmd_del_rx_ctx(struct qlcnic_adapter *adapter)
{
	return adapter->ahw->hw_ops->del_rx_ctx(adapter);
}

static inline void qlcnic_fw_cmd_del_tx_ctx(struct qlcnic_adapter *adapter,
					    struct qlcnic_host_tx_ring *ptr)
{
	return adapter->ahw->hw_ops->del_tx_ctx(adapter, ptr);
}

static inline int qlcnic_linkevent_request(struct qlcnic_adapter *adapter,
					   int enable)
{
	return adapter->ahw->hw_ops->setup_link_event(adapter, enable);
}

static inline int qlcnic_get_nic_info(struct qlcnic_adapter *adapter,
				      struct qlcnic_info *info, u8 id)
{
	return adapter->ahw->hw_ops->get_nic_info(adapter, info, id);
}

static inline int qlcnic_get_pci_info(struct qlcnic_adapter *adapter,
				      struct qlcnic_pci_info *info)
{
	return adapter->ahw->hw_ops->get_pci_info(adapter, info);
}

static inline int qlcnic_set_nic_info(struct qlcnic_adapter *adapter,
				      struct qlcnic_info *info)
{
	return adapter->ahw->hw_ops->set_nic_info(adapter, info);
}

static inline int qlcnic_sre_macaddr_change(struct qlcnic_adapter *adapter,
					    u8 *addr, u16 id, u8 cmd)
{
	return adapter->ahw->hw_ops->change_macvlan(adapter, addr, id, cmd);
}

static inline int qlcnic_napi_add(struct qlcnic_adapter *adapter,
				  struct net_device *netdev)
{
	return adapter->nic_ops->napi_add(adapter, netdev);
}

static inline void qlcnic_napi_del(struct qlcnic_adapter *adapter)
{
	adapter->nic_ops->napi_del(adapter);
}

static inline void qlcnic_napi_enable(struct qlcnic_adapter *adapter)
{
	adapter->ahw->hw_ops->napi_enable(adapter);
}

static inline int __qlcnic_shutdown(struct pci_dev *pdev)
{
	struct qlcnic_adapter *adapter = pci_get_drvdata(pdev);

	return adapter->nic_ops->shutdown(pdev);
}

static inline int __qlcnic_resume(struct qlcnic_adapter *adapter)
{
	return adapter->nic_ops->resume(adapter);
}

static inline void qlcnic_napi_disable(struct qlcnic_adapter *adapter)
{
	adapter->ahw->hw_ops->napi_disable(adapter);
}

static inline void qlcnic_config_intr_coalesce(struct qlcnic_adapter *adapter)
{
	adapter->ahw->hw_ops->config_intr_coal(adapter);
}

static inline int qlcnic_config_rss(struct qlcnic_adapter *adapter, int enable)
{
	return adapter->ahw->hw_ops->config_rss(adapter, enable);
}

static inline int qlcnic_config_hw_lro(struct qlcnic_adapter *adapter,
				       int enable)
{
	return adapter->ahw->hw_ops->config_hw_lro(adapter, enable);
}

static inline int qlcnic_set_lb_mode(struct qlcnic_adapter *adapter, u8 mode)
{
	return adapter->ahw->hw_ops->config_loopback(adapter, mode);
}

static inline int qlcnic_clear_lb_mode(struct qlcnic_adapter *adapter, u8 mode)
{
	return adapter->ahw->hw_ops->clear_loopback(adapter, mode);
}

static inline int qlcnic_nic_set_promisc(struct qlcnic_adapter *adapter,
					 u32 mode)
{
	return adapter->ahw->hw_ops->config_promisc_mode(adapter, mode);
}

static inline void qlcnic_change_filter(struct qlcnic_adapter *adapter,
					u64 *addr, u16 id)
{
	adapter->ahw->hw_ops->change_l2_filter(adapter, addr, id);
}

static inline int qlcnic_get_board_info(struct qlcnic_adapter *adapter)
{
	return adapter->ahw->hw_ops->get_board_info(adapter);
}

static inline void qlcnic_free_mac_list(struct qlcnic_adapter *adapter)
{
	return adapter->ahw->hw_ops->free_mac_list(adapter);
}

static inline void qlcnic_set_mac_filter_count(struct qlcnic_adapter *adapter)
{
	if (adapter->ahw->hw_ops->set_mac_filter_count)
		adapter->ahw->hw_ops->set_mac_filter_count(adapter);
}

static inline void qlcnic_read_phys_port_id(struct qlcnic_adapter *adapter)
{
	if (adapter->ahw->hw_ops->read_phys_port_id)
		adapter->ahw->hw_ops->read_phys_port_id(adapter);
}

static inline void qlcnic_dev_request_reset(struct qlcnic_adapter *adapter,
					    u32 key)
{
	if (adapter->nic_ops->request_reset)
		adapter->nic_ops->request_reset(adapter, key);
}

static inline void qlcnic_cancel_idc_work(struct qlcnic_adapter *adapter)
{
	if (adapter->nic_ops->cancel_idc_work)
		adapter->nic_ops->cancel_idc_work(adapter);
}

static inline irqreturn_t
qlcnic_clear_legacy_intr(struct qlcnic_adapter *adapter)
{
	return adapter->nic_ops->clear_legacy_intr(adapter);
}

static inline int qlcnic_config_led(struct qlcnic_adapter *adapter, u32 state,
				    u32 rate)
{
	return adapter->nic_ops->config_led(adapter, state, rate);
}

static inline void qlcnic_config_ipaddr(struct qlcnic_adapter *adapter,
					__be32 ip, int cmd)
{
	adapter->nic_ops->config_ipaddr(adapter, ip, cmd);
}

static inline bool qlcnic_check_multi_tx(struct qlcnic_adapter *adapter)
{
	return test_bit(__QLCNIC_MULTI_TX_UNIQUE, &adapter->state);
}

static inline void qlcnic_disable_multi_tx(struct qlcnic_adapter *adapter)
{
	test_and_clear_bit(__QLCNIC_MULTI_TX_UNIQUE, &adapter->state);
	adapter->max_drv_tx_rings = 1;
}

/* When operating in a muti tx mode, driver needs to write 0x1
 * to src register, instead of 0x0 to disable receiving interrupt.
 */
static inline void qlcnic_disable_int(struct qlcnic_host_sds_ring *sds_ring)
{
	struct qlcnic_adapter *adapter = sds_ring->adapter;

	if (qlcnic_check_multi_tx(adapter) &&
	    !adapter->ahw->diag_test &&
	    (adapter->flags & QLCNIC_MSIX_ENABLED))
		writel(0x1, sds_ring->crb_intr_mask);
	else
		writel(0, sds_ring->crb_intr_mask);
}

/* When operating in a muti tx mode, driver needs to write 0x0
 * to src register, instead of 0x1 to enable receiving interrupts.
 */
static inline void qlcnic_enable_int(struct qlcnic_host_sds_ring *sds_ring)
{
	struct qlcnic_adapter *adapter = sds_ring->adapter;

	if (qlcnic_check_multi_tx(adapter) &&
	    !adapter->ahw->diag_test &&
	    (adapter->flags & QLCNIC_MSIX_ENABLED))
		writel(0, sds_ring->crb_intr_mask);
	else
		writel(0x1, sds_ring->crb_intr_mask);

	if (!QLCNIC_IS_MSI_FAMILY(adapter))
		writel(0xfbff, adapter->tgt_mask_reg);
}

static inline int qlcnic_get_diag_lock(struct qlcnic_adapter *adapter)
{
	return test_and_set_bit(__QLCNIC_DIAG_MODE, &adapter->state);
}

static inline void qlcnic_release_diag_lock(struct qlcnic_adapter *adapter)
{
	clear_bit(__QLCNIC_DIAG_MODE, &adapter->state);
}

static inline int qlcnic_check_diag_status(struct qlcnic_adapter *adapter)
{
	return test_bit(__QLCNIC_DIAG_MODE, &adapter->state);
}

extern const struct ethtool_ops qlcnic_sriov_vf_ethtool_ops;
extern const struct ethtool_ops qlcnic_ethtool_ops;
extern const struct ethtool_ops qlcnic_ethtool_failed_ops;

#define QLCDB(adapter, lvl, _fmt, _args...) do {	\
	if (NETIF_MSG_##lvl & adapter->ahw->msg_enable)	\
		printk(KERN_INFO "%s: %s: " _fmt,	\
			 dev_name(&adapter->pdev->dev),	\
			__func__, ##_args);		\
	} while (0)

#define PCI_DEVICE_ID_QLOGIC_QLE824X		0x8020
#define PCI_DEVICE_ID_QLOGIC_QLE834X		0x8030
#define PCI_DEVICE_ID_QLOGIC_VF_QLE834X	0x8430
#define PCI_DEVICE_ID_QLOGIC_QLE844X		0x8040
#define PCI_DEVICE_ID_QLOGIC_VF_QLE844X	0x8440

static inline bool qlcnic_82xx_check(struct qlcnic_adapter *adapter)
{
	unsigned short device = adapter->pdev->device;
	return (device == PCI_DEVICE_ID_QLOGIC_QLE824X) ? true : false;
}

static inline bool qlcnic_84xx_check(struct qlcnic_adapter *adapter)
{
	unsigned short device = adapter->pdev->device;

	return ((device == PCI_DEVICE_ID_QLOGIC_QLE844X) ||
		(device == PCI_DEVICE_ID_QLOGIC_VF_QLE844X)) ? true : false;
}

static inline bool qlcnic_83xx_check(struct qlcnic_adapter *adapter)
{
	unsigned short device = adapter->pdev->device;
	bool status;

	status = ((device == PCI_DEVICE_ID_QLOGIC_QLE834X) ||
		  (device == PCI_DEVICE_ID_QLOGIC_QLE844X) ||
		  (device == PCI_DEVICE_ID_QLOGIC_VF_QLE844X) ||
		  (device == PCI_DEVICE_ID_QLOGIC_VF_QLE834X)) ? true : false;

	return status;
}

static inline bool qlcnic_sriov_pf_check(struct qlcnic_adapter *adapter)
{
	return (adapter->ahw->op_mode == QLCNIC_SRIOV_PF_FUNC) ? true : false;
}

static inline bool qlcnic_sriov_vf_check(struct qlcnic_adapter *adapter)
{
	unsigned short device = adapter->pdev->device;
	bool status;

	status = ((device == PCI_DEVICE_ID_QLOGIC_VF_QLE834X) ||
		  (device == PCI_DEVICE_ID_QLOGIC_VF_QLE844X)) ? true : false;

	return status;
}

static inline int qlcnic_dcb_get_hw_capability(struct qlcnic_adapter *adapter)
{
	struct qlcnic_dcb *dcb = adapter->dcb;

	if (dcb && dcb->ops->get_hw_capability)
		return dcb->ops->get_hw_capability(adapter);

	return 0;
}

static inline void qlcnic_dcb_free(struct qlcnic_adapter *adapter)
{
	struct qlcnic_dcb *dcb = adapter->dcb;

	if (dcb && dcb->ops->free)
		dcb->ops->free(adapter);
}

static inline int qlcnic_dcb_attach(struct qlcnic_adapter *adapter)
{
	struct qlcnic_dcb *dcb = adapter->dcb;

	if (dcb && dcb->ops->attach)
		return dcb->ops->attach(adapter);

	return 0;
}

static inline int
qlcnic_dcb_query_hw_capability(struct qlcnic_adapter *adapter, char *buf)
{
	struct qlcnic_dcb *dcb = adapter->dcb;

	if (dcb && dcb->ops->query_hw_capability)
		return dcb->ops->query_hw_capability(adapter, buf);

	return 0;
}

static inline void qlcnic_dcb_get_info(struct qlcnic_adapter *adapter)
{
	struct qlcnic_dcb *dcb = adapter->dcb;

	if (dcb && dcb->ops->get_info)
		dcb->ops->get_info(adapter);
}

static inline int
qlcnic_dcb_query_cee_param(struct qlcnic_adapter *adapter, char *buf, u8 type)
{
	struct qlcnic_dcb *dcb = adapter->dcb;

	if (dcb && dcb->ops->query_cee_param)
		return dcb->ops->query_cee_param(adapter, buf, type);

	return 0;
}

static inline int qlcnic_dcb_get_cee_cfg(struct qlcnic_adapter *adapter)
{
	struct qlcnic_dcb *dcb = adapter->dcb;

	if (dcb && dcb->ops->get_cee_cfg)
		return dcb->ops->get_cee_cfg(adapter);

	return 0;
}

static inline void
qlcnic_dcb_register_aen(struct qlcnic_adapter *adapter, u8 flag)
{
	struct qlcnic_dcb *dcb = adapter->dcb;

	if (dcb && dcb->ops->register_aen)
		dcb->ops->register_aen(adapter, flag);
}

static inline void qlcnic_dcb_handle_aen(struct qlcnic_adapter *adapter,
					 void *msg)
{
	struct qlcnic_dcb *dcb = adapter->dcb;

	if (dcb && dcb->ops->handle_aen)
		dcb->ops->handle_aen(adapter, msg);
}

static inline void qlcnic_dcb_init_dcbnl_ops(struct qlcnic_adapter *adapter)
{
	struct qlcnic_dcb *dcb = adapter->dcb;

	if (dcb && dcb->ops->init_dcbnl_ops)
		dcb->ops->init_dcbnl_ops(adapter);
}
#endif				/* __QLCNIC_H_ */
