
#ifndef _SLIC_H
#define _SLIC_H

#include <linux/types.h>
#include <linux/netdevice.h>
#include <linux/spinlock_types.h>
#include <linux/dma-mapping.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/list.h>
#include <linux/u64_stats_sync.h>

#define SLIC_VGBSTAT_XPERR		0x40000000
#define SLIC_VGBSTAT_XERRSHFT		25
#define SLIC_VGBSTAT_XCSERR		0x23
#define SLIC_VGBSTAT_XUFLOW		0x22
#define SLIC_VGBSTAT_XHLEN		0x20
#define SLIC_VGBSTAT_NETERR		0x01000000
#define SLIC_VGBSTAT_NERRSHFT		16
#define SLIC_VGBSTAT_NERRMSK		0x1ff
#define SLIC_VGBSTAT_NCSERR		0x103
#define SLIC_VGBSTAT_NUFLOW		0x102
#define SLIC_VGBSTAT_NHLEN		0x100
#define SLIC_VGBSTAT_LNKERR		0x00000080
#define SLIC_VGBSTAT_LERRMSK		0xff
#define SLIC_VGBSTAT_LDEARLY		0x86
#define SLIC_VGBSTAT_LBOFLO		0x85
#define SLIC_VGBSTAT_LCODERR		0x84
#define SLIC_VGBSTAT_LDBLNBL		0x83
#define SLIC_VGBSTAT_LCRCERR		0x82
#define SLIC_VGBSTAT_LOFLO		0x81
#define SLIC_VGBSTAT_LUFLO		0x80

#define SLIC_IRHDDR_FLEN_MSK		0x0000ffff
#define SLIC_IRHDDR_SVALID		0x80000000
#define SLIC_IRHDDR_ERR			0x10000000

#define SLIC_VRHSTAT_802OE		0x80000000
#define SLIC_VRHSTAT_TPOFLO		0x10000000
#define SLIC_VRHSTATB_802UE		0x80000000
#define SLIC_VRHSTATB_RCVE		0x40000000
#define SLIC_VRHSTATB_BUFF		0x20000000
#define SLIC_VRHSTATB_CARRE		0x08000000
#define SLIC_VRHSTATB_LONGE		0x02000000
#define SLIC_VRHSTATB_PREA		0x01000000
#define SLIC_VRHSTATB_CRC		0x00800000
#define SLIC_VRHSTATB_DRBL		0x00400000
#define SLIC_VRHSTATB_CODE		0x00200000
#define SLIC_VRHSTATB_TPCSUM		0x00100000
#define SLIC_VRHSTATB_TPHLEN		0x00080000
#define SLIC_VRHSTATB_IPCSUM		0x00040000
#define SLIC_VRHSTATB_IPLERR		0x00020000
#define SLIC_VRHSTATB_IPHERR		0x00010000

#define SLIC_CMD_XMT_REQ		0x01
#define SLIC_CMD_TYPE_DUMB		3

#define SLIC_RESET_MAGIC		0xDEAD
#define SLIC_ICR_INT_OFF		0
#define SLIC_ICR_INT_ON			1
#define SLIC_ICR_INT_MASK		2

#define SLIC_ISR_ERR			0x80000000
#define SLIC_ISR_RCV			0x40000000
#define SLIC_ISR_CMD			0x20000000
#define SLIC_ISR_IO			0x60000000
#define SLIC_ISR_UPC			0x10000000
#define SLIC_ISR_LEVENT			0x08000000
#define SLIC_ISR_RMISS			0x02000000
#define SLIC_ISR_UPCERR			0x01000000
#define SLIC_ISR_XDROP			0x00800000
#define SLIC_ISR_UPCBSY			0x00020000

#define SLIC_ISR_PING_MASK		0x00700000
#define SLIC_ISR_UPCERR_MASK		(SLIC_ISR_UPCERR | SLIC_ISR_UPCBSY)
#define SLIC_ISR_UPC_MASK		(SLIC_ISR_UPC | SLIC_ISR_UPCERR_MASK)
#define SLIC_WCS_START			0x80000000
#define SLIC_WCS_COMPARE		0x40000000
#define SLIC_RCVWCS_BEGIN		0x40000000
#define SLIC_RCVWCS_FINISH		0x80000000

#define SLIC_MIICR_REG_16		0x00100000
#define SLIC_MRV_REG16_XOVERON		0x0068

#define SLIC_GIG_LINKUP			0x0001
#define SLIC_GIG_FULLDUPLEX		0x0002
#define SLIC_GIG_SPEED_MASK		0x000C
#define SLIC_GIG_SPEED_1000		0x0008
#define SLIC_GIG_SPEED_100		0x0004
#define SLIC_GIG_SPEED_10		0x0000

#define SLIC_GMCR_RESET			0x80000000
#define SLIC_GMCR_GBIT			0x20000000
#define SLIC_GMCR_FULLD			0x10000000
#define SLIC_GMCR_GAPBB_SHIFT		14
#define SLIC_GMCR_GAPR1_SHIFT		7
#define SLIC_GMCR_GAPR2_SHIFT		0
#define SLIC_GMCR_GAPBB_1000		0x60
#define SLIC_GMCR_GAPR1_1000		0x2C
#define SLIC_GMCR_GAPR2_1000		0x40
#define SLIC_GMCR_GAPBB_100		0x70
#define SLIC_GMCR_GAPR1_100		0x2C
#define SLIC_GMCR_GAPR2_100		0x40

#define SLIC_XCR_RESET			0x80000000
#define SLIC_XCR_XMTEN			0x40000000
#define SLIC_XCR_PAUSEEN		0x20000000
#define SLIC_XCR_LOADRNG		0x10000000

#define SLIC_GXCR_RESET			0x80000000
#define SLIC_GXCR_XMTEN			0x40000000
#define SLIC_GXCR_PAUSEEN		0x20000000

#define SLIC_GRCR_RESET			0x80000000
#define SLIC_GRCR_RCVEN			0x40000000
#define SLIC_GRCR_RCVALL		0x20000000
#define SLIC_GRCR_RCVBAD		0x10000000
#define SLIC_GRCR_CTLEN			0x08000000
#define SLIC_GRCR_ADDRAEN		0x02000000
#define SLIC_GRCR_HASHSIZE_SHIFT	17
#define SLIC_GRCR_HASHSIZE		14

/* Reset Register */
#define SLIC_REG_RESET			0x0000
/* Interrupt Control Register */
#define SLIC_REG_ICR			0x0008
/* Interrupt status pointer */
#define SLIC_REG_ISP			0x0010
/* Interrupt status */
#define SLIC_REG_ISR			0x0018
/* Header buffer address reg
 * 31-8 - phy addr of set of contiguous hdr buffers
 *  7-0 - number of buffers passed
 * Buffers are 256 bytes long on 256-byte boundaries.
 */
#define SLIC_REG_HBAR			0x0020
/* Data buffer handle & address reg
 * 4 sets of registers; Buffers are 2K bytes long 2 per 4K page.
 */
#define SLIC_REG_DBAR			0x0028
/* Xmt Cmd buf addr regs.
 * 1 per XMT interface
 * 31-5 - phy addr of host command buffer
 *  4-0 - length of cmd in multiples of 32 bytes
 * Buffers are 32 bytes up to 512 bytes long
 */
#define SLIC_REG_CBAR			0x0030
/* Write control store */
#define	SLIC_REG_WCS			0x0034
/*Response buffer address reg.
 * 31-8 - phy addr of set of contiguous response buffers
 * 7-0 - number of buffers passed
 * Buffers are 32 bytes long on 32-byte boundaries.
 */
#define	SLIC_REG_RBAR			0x0038
/* Read statistics (UPR) */
#define	SLIC_REG_RSTAT			0x0040
/* Read link status */
#define	SLIC_REG_LSTAT			0x0048
/* Write Mac Config */
#define	SLIC_REG_WMCFG			0x0050
/* Write phy register */
#define SLIC_REG_WPHY			0x0058
/* Rcv Cmd buf addr reg */
#define	SLIC_REG_RCBAR			0x0060
/* Read SLIC Config*/
#define SLIC_REG_RCONFIG		0x0068
/* Interrupt aggregation time */
#define SLIC_REG_INTAGG			0x0070
/* Write XMIT config reg */
#define	SLIC_REG_WXCFG			0x0078
/* Write RCV config reg */
#define	SLIC_REG_WRCFG			0x0080
/* Write rcv addr a low */
#define	SLIC_REG_WRADDRAL		0x0088
/* Write rcv addr a high */
#define	SLIC_REG_WRADDRAH		0x0090
/* Write rcv addr b low */
#define	SLIC_REG_WRADDRBL		0x0098
/* Write rcv addr b high */
#define	SLIC_REG_WRADDRBH		0x00a0
/* Low bits of mcast mask */
#define	SLIC_REG_MCASTLOW		0x00a8
/* High bits of mcast mask */
#define	SLIC_REG_MCASTHIGH		0x00b0
/* Ping the card */
#define SLIC_REG_PING			0x00b8
/* Dump command */
#define SLIC_REG_DUMP_CMD		0x00c0
/* Dump data pointer */
#define SLIC_REG_DUMP_DATA		0x00c8
/* Read card's pci_status register */
#define	SLIC_REG_PCISTATUS		0x00d0
/* Write hostid field */
#define SLIC_REG_WRHOSTID		0x00d8
/* Put card in a low power state */
#define SLIC_REG_LOW_POWER		0x00e0
/* Force slic into quiescent state  before soft reset */
#define SLIC_REG_QUIESCE		0x00e8
/* Reset interface queues */
#define SLIC_REG_RESET_IFACE		0x00f0
/* Register is only written when it has changed.
 * Bits 63-32 for host i/f addrs.
 */
#define SLIC_REG_ADDR_UPPER		0x00f8
/* 64 bit Header buffer address reg */
#define SLIC_REG_HBAR64			0x0100
/* 64 bit Data buffer handle & address reg */
#define SLIC_REG_DBAR64			0x0108
/* 64 bit Xmt Cmd buf addr regs. */
#define SLIC_REG_CBAR64			0x0110
/* 64 bit Response buffer address reg.*/
#define SLIC_REG_RBAR64			0x0118
/* 64 bit Rcv Cmd buf addr reg*/
#define	SLIC_REG_RCBAR64		0x0120
/* Read statistics (64 bit UPR) */
#define	SLIC_REG_RSTAT64		0x0128
/* Download Gigabit RCV sequencer ucode */
#define SLIC_REG_RCV_WCS		0x0130
/* Write VlanId field */
#define SLIC_REG_WRVLANID		0x0138
/* Read Transformer info */
#define SLIC_REG_READ_XF_INFO		0x0140
/* Write Transformer info */
#define SLIC_REG_WRITE_XF_INFO		0x0148
/* Write card ticks per second */
#define SLIC_REG_TICKS_PER_SEC		0x0170
#define SLIC_REG_HOSTID			0x1554

#define PCI_VENDOR_ID_ALACRITECH		0x139A
#define PCI_DEVICE_ID_ALACRITECH_MOJAVE		0x0005
#define PCI_SUBDEVICE_ID_ALACRITECH_1000X1	0x0005
#define PCI_SUBDEVICE_ID_ALACRITECH_1000X1_2	0x0006
#define PCI_SUBDEVICE_ID_ALACRITECH_1000X1F	0x0007
#define PCI_SUBDEVICE_ID_ALACRITECH_CICADA	0x0008
#define PCI_SUBDEVICE_ID_ALACRITECH_SES1001T	0x2006
#define PCI_SUBDEVICE_ID_ALACRITECH_SES1001F	0x2007
#define PCI_DEVICE_ID_ALACRITECH_OASIS		0x0007
#define PCI_SUBDEVICE_ID_ALACRITECH_SEN2002XT	0x000B
#define PCI_SUBDEVICE_ID_ALACRITECH_SEN2002XF	0x000C
#define PCI_SUBDEVICE_ID_ALACRITECH_SEN2001XT	0x000D
#define PCI_SUBDEVICE_ID_ALACRITECH_SEN2001XF	0x000E
#define PCI_SUBDEVICE_ID_ALACRITECH_SEN2104EF	0x000F
#define PCI_SUBDEVICE_ID_ALACRITECH_SEN2104ET	0x0010
#define PCI_SUBDEVICE_ID_ALACRITECH_SEN2102EF	0x0011
#define PCI_SUBDEVICE_ID_ALACRITECH_SEN2102ET	0x0012

/* Note: power of two required for number descriptors  */
#define SLIC_NUM_RX_LES			256
#define SLIC_RX_BUFF_SIZE		2048
#define SLIC_RX_BUFF_ALIGN		256
#define SLIC_RX_BUFF_HDR_SIZE		34
#define SLIC_MAX_REQ_RX_DESCS		1

#define SLIC_NUM_TX_DESCS		256
#define SLIC_TX_DESC_ALIGN		32
#define SLIC_MIN_TX_WAKEUP_DESCS	10
#define SLIC_MAX_REQ_TX_DESCS		1
#define SLIC_MAX_TX_COMPLETIONS		100

#define SLIC_NUM_STAT_DESCS		128
#define SLIC_STATS_DESC_ALIGN		256

#define SLIC_NUM_STAT_DESC_ARRAYS	4
#define SLIC_INVALID_STAT_DESC_IDX	0xffffffff

#define SLIC_NAPI_WEIGHT		64

#define SLIC_UPR_LSTAT			0
#define SLIC_UPR_CONFIG			1

#define SLIC_EEPROM_SIZE		128
#define SLIC_EEPROM_MAGIC		0xa5a5

#define SLIC_FIRMWARE_MOJAVE		"slicoss/gbdownload.sys"
#define SLIC_FIRMWARE_OASIS		"slicoss/oasisdownload.sys"
#define SLIC_RCV_FIRMWARE_MOJAVE	"slicoss/gbrcvucode.sys"
#define SLIC_RCV_FIRMWARE_OASIS		"slicoss/oasisrcvucode.sys"
#define SLIC_FIRMWARE_MIN_SIZE		64
#define SLIC_FIRMWARE_MAX_SECTIONS	3

#define SLIC_MODEL_MOJAVE		0
#define SLIC_MODEL_OASIS		1

#define SLIC_INC_STATS_COUNTER(st, counter)	\
do {						\
	u64_stats_update_begin(&(st)->syncp);	\
	(st)->counter++;			\
	u64_stats_update_end(&(st)->syncp);	\
} while (0)

#define SLIC_GET_STATS_COUNTER(newst, st, counter)			\
{									\
	unsigned int start;						\
	do {							\
		start = u64_stats_fetch_begin_irq(&(st)->syncp);	\
		newst = (st)->counter;					\
	} while (u64_stats_fetch_retry_irq(&(st)->syncp, start));	\
}

struct slic_upr {
	dma_addr_t paddr;
	unsigned int type;
	struct list_head list;
};

struct slic_upr_list {
	bool pending;
	struct list_head list;
	/* upr list lock */
	spinlock_t lock;
};

/* SLIC EEPROM structure for Mojave */
struct slic_mojave_eeprom {
	__le16 id;		/* 00 EEPROM/FLASH Magic code 'A5A5'*/
	__le16 eeprom_code_size;/* 01 Size of EEPROM Codes (bytes * 4)*/
	__le16 flash_size;	/* 02 Flash size */
	__le16 eeprom_size;	/* 03 EEPROM Size */
	__le16 vendor_id;	/* 04 Vendor ID */
	__le16 dev_id;		/* 05 Device ID */
	u8 rev_id;		/* 06 Revision ID */
	u8 class_code[3];	/* 07 Class Code */
	u8 irqpin_dbg;		/* 08 Debug Interrupt pin */
	u8 irqpin;		/*    Network Interrupt Pin */
	u8 min_grant;		/* 09 Minimum grant */
	u8 max_lat;		/*    Maximum Latency */
	__le16 pci_stat;	/* 10 PCI Status */
	__le16 sub_vendor_id;	/* 11 Subsystem Vendor Id */
	__le16 sub_id;		/* 12 Subsystem ID */
	__le16 dev_id_dbg;	/* 13 Debug Device Id */
	__le16 ramrom;		/* 14 Dram/Rom function */
	__le16 dram_size2pci;	/* 15 DRAM size to PCI (bytes * 64K) */
	__le16 rom_size2pci;	/* 16 ROM extension size to PCI (bytes * 4k) */
	u8 pad[2];		/* 17 Padding */
	u8 freetime;		/* 18 FreeTime setting */
	u8 ifctrl;		/* 10-bit interface control (Mojave only) */
	__le16 dram_size;	/* 19 DRAM size (bytes * 64k) */
	u8 mac[ETH_ALEN];	/* 20 MAC addresses */
	u8 mac2[ETH_ALEN];
	u8 pad2[6];
	u16 dev_id2;		/* Device ID for 2nd PCI function */
	u8 irqpin2;		/* Interrupt pin for 2nd PCI function */
	u8 class_code2[3];	/* Class Code for 2nd PCI function */
	u16 cfg_byte6;		/* Config Byte 6 */
	u16 pme_cap;		/* Power Mgment capabilities */
	u16 nwclk_ctrl;		/* NetworkClockControls */
	u8 fru_format;		/* Alacritech FRU format type */
	u8 fru_assembly[6];	/* Alacritech FRU information */
	u8 fru_rev[2];
	u8 fru_serial[14];
	u8 fru_pad[3];
	u8 oem_fru[28];		/* optional OEM FRU format type */
	u8 pad3[4];		/* Pad to 128 bytes - includes 2 cksum bytes
				 * (if OEM FRU info exists) and two unusable
				 * bytes at the end
				 */
};

/* SLIC EEPROM structure for Oasis */
struct slic_oasis_eeprom {
	__le16 id;		/* 00 EEPROM/FLASH Magic code 'A5A5' */
	__le16 eeprom_code_size;/* 01 Size of EEPROM Codes (bytes * 4)*/
	__le16 spidev0_cfg;	/* 02 Flash Config for SPI device 0 */
	__le16 spidev1_cfg;	/* 03 Flash Config for SPI device 1 */
	__le16 vendor_id;	/* 04 Vendor ID */
	__le16 dev_id;		/* 05 Device ID (function 0) */
	u8 rev_id;		/* 06 Revision ID */
	u8 class_code0[3];	/* 07 Class Code for PCI function 0 */
	u8 irqpin1;		/* 08 Interrupt pin for PCI function 1*/
	u8 class_code1[3];	/* 09 Class Code for PCI function 1 */
	u8 irqpin2;		/* 10 Interrupt pin for PCI function 2*/
	u8 irqpin0;		/*    Interrupt pin for PCI function 0*/
	u8 min_grant;		/* 11 Minimum grant */
	u8 max_lat;		/*    Maximum Latency */
	__le16 sub_vendor_id;	/* 12 Subsystem Vendor Id */
	__le16 sub_id;		/* 13 Subsystem ID */
	__le16 flash_size;	/* 14 Flash size (bytes / 4K) */
	__le16 dram_size2pci;	/* 15 DRAM size to PCI (bytes / 64K) */
	__le16 rom_size2pci;	/* 16 Flash (ROM extension) size to PCI
				 *   (bytes / 4K)
				 */
	__le16 dev_id1;		/* 17 Device Id (function 1) */
	__le16 dev_id2;		/* 18 Device Id (function 2) */
	__le16 dev_stat_cfg;	/* 19 Device Status Config Bytes 6-7 */
	__le16 pme_cap;		/* 20 Power Mgment capabilities */
	u8 msi_cap;		/* 21 MSI capabilities */
	u8 clock_div;		/*    Clock divider */
	__le16 pci_stat_lo;	/* 22 PCI Status bits 15:0 */
	__le16 pci_stat_hi;	/* 23 PCI Status bits 31:16 */
	__le16 dram_cfg_lo;	/* 24 DRAM Configuration bits 15:0 */
	__le16 dram_cfg_hi;	/* 25 DRAM Configuration bits 31:16 */
	__le16 dram_size;	/* 26 DRAM size (bytes / 64K) */
	__le16 gpio_tbi_ctrl;	/* 27 GPIO/TBI controls for functions 1/0 */
	__le16 eeprom_size;	/* 28 EEPROM Size */
	u8 mac[ETH_ALEN];	/* 29 MAC addresses (2 ports) */
	u8 mac2[ETH_ALEN];
	u8 fru_format;		/* 35 Alacritech FRU format type */
	u8 fru_assembly[6];	/* Alacritech FRU information */
	u8 fru_rev[2];
	u8 fru_serial[14];
	u8 fru_pad[3];
	u8 oem_fru[28];		/* optional OEM FRU information */
	u8 pad[4];		/* Pad to 128 bytes - includes 2 checksum bytes
				 * (if OEM FRU info exists) and two unusable
				 * bytes at the end
				 */
};

struct slic_stats {
	u64 rx_packets;
	u64 rx_bytes;
	u64 rx_mcasts;
	u64 rx_errors;
	u64 tx_packets;
	u64 tx_bytes;
	/* HW STATS */
	u64 rx_buff_miss;
	u64 tx_dropped;
	u64 irq_errs;
	/* transport layer */
	u64 rx_tpcsum;
	u64 rx_tpoflow;
	u64 rx_tphlen;
	/* ip layer */
	u64 rx_ipcsum;
	u64 rx_iplen;
	u64 rx_iphlen;
	/* link layer */
	u64 rx_early;
	u64 rx_buffoflow;
	u64 rx_lcode;
	u64 rx_drbl;
	u64 rx_crc;
	u64 rx_oflow802;
	u64 rx_uflow802;
	/* oasis only */
	u64 tx_carrier;
	struct u64_stats_sync syncp;
};

struct slic_shmem_data {
	__le32 isr;
	__le32 link;
};

struct slic_shmem {
	dma_addr_t isr_paddr;
	dma_addr_t link_paddr;
	struct slic_shmem_data *shmem_data;
};

struct slic_rx_info_oasis {
	__le32 frame_status;
	__le32 frame_status_b;
	__le32 time_stamp;
	__le32 checksum;
};

struct slic_rx_info_mojave {
	__le32 frame_status;
	__le16 byte_cnt;
	__le16 tp_chksum;
	__le16 ctx_hash;
	__le16 mac_hash;
	__le16 buff_lnk;
};

struct slic_stat_desc {
	__le32 hnd;
	__u8 pad[8];
	__le32 status;
	__u8 pad2[16];
};

struct slic_stat_queue {
	struct slic_stat_desc *descs[SLIC_NUM_STAT_DESC_ARRAYS];
	dma_addr_t paddr[SLIC_NUM_STAT_DESC_ARRAYS];
	unsigned int addr_offset[SLIC_NUM_STAT_DESC_ARRAYS];
	unsigned int active_array;
	unsigned int len;
	unsigned int done_idx;
	size_t mem_size;
};

struct slic_tx_desc {
	__le32 hnd;
	__le32 rsvd;
	u8 cmd;
	u8 flags;
	__le16 rsvd2;
	__le32 totlen;
	__le32 paddrl;
	__le32 paddrh;
	__le32 len;
	__le32 type;
};

struct slic_tx_buffer {
	struct sk_buff *skb;
	DEFINE_DMA_UNMAP_ADDR(map_addr);
	DEFINE_DMA_UNMAP_LEN(map_len);
	struct slic_tx_desc *desc;
	dma_addr_t desc_paddr;
};

struct slic_tx_queue {
	struct dma_pool *dma_pool;
	struct slic_tx_buffer *txbuffs;
	unsigned int len;
	unsigned int put_idx;
	unsigned int done_idx;
};

struct slic_rx_desc {
	u8 pad[16];
	__le32 buffer;
	__le32 length;
	__le32 status;
};

struct slic_rx_buffer {
	struct sk_buff *skb;
	DEFINE_DMA_UNMAP_ADDR(map_addr);
	DEFINE_DMA_UNMAP_LEN(map_len);
	unsigned int addr_offset;
};

struct slic_rx_queue {
	struct slic_rx_buffer *rxbuffs;
	unsigned int len;
	unsigned int done_idx;
	unsigned int put_idx;
};

struct slic_device {
	struct pci_dev *pdev;
	struct net_device *netdev;
	void __iomem *regs;
	/* upper address setting lock */
	spinlock_t upper_lock;
	struct slic_shmem shmem;
	struct napi_struct napi;
	struct slic_rx_queue rxq;
	struct slic_tx_queue txq;
	struct slic_stat_queue stq;
	struct slic_stats stats;
	struct slic_upr_list upr_list;
	/* link configuration lock */
	spinlock_t link_lock;
	bool promisc;
	int speed;
	unsigned int duplex;
	bool is_fiber;
	unsigned char model;
};

static inline u32 slic_read(struct slic_device *sdev, unsigned int reg)
{
	return ioread32(sdev->regs + reg);
}

static inline void slic_write(struct slic_device *sdev, unsigned int reg,
			      u32 val)
{
	iowrite32(val, sdev->regs + reg);
}

static inline void slic_flush_write(struct slic_device *sdev)
{
	(void)ioread32(sdev->regs + SLIC_REG_HOSTID);
}

#endif /* _SLIC_H */
