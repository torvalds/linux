/*
 * Copyright (C) 2005 - 2011 Emulex
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.  The full GNU General
 * Public License is included in this distribution in the file called COPYING.
 *
 * Contact Information:
 * linux-drivers@emulex.com
 *
 * Emulex
 * 3333 Susan Street
 * Costa Mesa, CA 92626
 */

/********* Mailbox door bell *************/
/* Used for driver communication with the FW.
 * The software must write this register twice to post any command. First,
 * it writes the register with hi=1 and the upper bits of the physical address
 * for the MAILBOX structure. Software must poll the ready bit until this
 * is acknowledged. Then, sotware writes the register with hi=0 with the lower
 * bits in the address. It must poll the ready bit until the command is
 * complete. Upon completion, the MAILBOX will contain a valid completion
 * queue entry.
 */
#define MPU_MAILBOX_DB_OFFSET	0x160
#define MPU_MAILBOX_DB_RDY_MASK	0x1 	/* bit 0 */
#define MPU_MAILBOX_DB_HI_MASK	0x2	/* bit 1 */

#define MPU_EP_CONTROL 		0

/********** MPU semphore ******************/
#define MPU_EP_SEMAPHORE_OFFSET		0xac
#define MPU_EP_SEMAPHORE_IF_TYPE2_OFFSET	0x400
#define EP_SEMAPHORE_POST_STAGE_MASK		0x0000FFFF
#define EP_SEMAPHORE_POST_ERR_MASK		0x1
#define EP_SEMAPHORE_POST_ERR_SHIFT		31

/* MPU semphore POST stage values */
#define POST_STAGE_AWAITING_HOST_RDY 	0x1 /* FW awaiting goahead from host */
#define POST_STAGE_HOST_RDY 		0x2 /* Host has given go-ahed to FW */
#define POST_STAGE_BE_RESET		0x3 /* Host wants to reset chip */
#define POST_STAGE_ARMFW_RDY		0xc000	/* FW is done with POST */


/* Lancer SLIPORT registers */
#define SLIPORT_STATUS_OFFSET		0x404
#define SLIPORT_CONTROL_OFFSET		0x408
#define SLIPORT_ERROR1_OFFSET		0x40C
#define SLIPORT_ERROR2_OFFSET		0x410
#define PHYSDEV_CONTROL_OFFSET		0x414

#define SLIPORT_STATUS_ERR_MASK		0x80000000
#define SLIPORT_STATUS_RN_MASK		0x01000000
#define SLIPORT_STATUS_RDY_MASK		0x00800000
#define SLI_PORT_CONTROL_IP_MASK	0x08000000
#define PHYSDEV_CONTROL_FW_RESET_MASK	0x00000002
#define PHYSDEV_CONTROL_INP_MASK	0x40000000

/********* Memory BAR register ************/
#define PCICFG_MEMBAR_CTRL_INT_CTRL_OFFSET 	0xfc
/* Host Interrupt Enable, if set interrupts are enabled although "PCI Interrupt
 * Disable" may still globally block interrupts in addition to individual
 * interrupt masks; a mechanism for the device driver to block all interrupts
 * atomically without having to arbitrate for the PCI Interrupt Disable bit
 * with the OS.
 */
#define MEMBAR_CTRL_INT_CTRL_HOSTINTR_MASK	(1 << 29) /* bit 29 */

/********* Power management (WOL) **********/
#define PCICFG_PM_CONTROL_OFFSET		0x44
#define PCICFG_PM_CONTROL_MASK			0x108	/* bits 3 & 8 */

/********* Online Control Registers *******/
#define PCICFG_ONLINE0				0xB0
#define PCICFG_ONLINE1				0xB4

/********* UE Status and Mask Registers ***/
#define PCICFG_UE_STATUS_LOW			0xA0
#define PCICFG_UE_STATUS_HIGH			0xA4
#define PCICFG_UE_STATUS_LOW_MASK		0xA8
#define PCICFG_UE_STATUS_HI_MASK		0xAC

/******** SLI_INTF ***********************/
#define SLI_INTF_REG_OFFSET			0x58
#define SLI_INTF_VALID_MASK			0xE0000000
#define SLI_INTF_VALID				0xC0000000
#define SLI_INTF_HINT2_MASK			0x1F000000
#define SLI_INTF_HINT2_SHIFT			24
#define SLI_INTF_HINT1_MASK			0x00FF0000
#define SLI_INTF_HINT1_SHIFT			16
#define SLI_INTF_FAMILY_MASK			0x00000F00
#define SLI_INTF_FAMILY_SHIFT			8
#define SLI_INTF_IF_TYPE_MASK			0x0000F000
#define SLI_INTF_IF_TYPE_SHIFT			12
#define SLI_INTF_REV_MASK			0x000000F0
#define SLI_INTF_REV_SHIFT			4
#define SLI_INTF_FT_MASK			0x00000001

#define SLI_INTF_TYPE_2		2
#define SLI_INTF_TYPE_3		3

/* SLI family */
#define BE_SLI_FAMILY		0x0
#define LANCER_A0_SLI_FAMILY	0xA
#define SKYHAWK_SLI_FAMILY      0x2

/********* ISR0 Register offset **********/
#define CEV_ISR0_OFFSET 			0xC18
#define CEV_ISR_SIZE				4

/********* Event Q door bell *************/
#define DB_EQ_OFFSET			DB_CQ_OFFSET
#define DB_EQ_RING_ID_MASK		0x1FF	/* bits 0 - 8 */
#define DB_EQ_RING_ID_EXT_MASK		0x3e00  /* bits 9-13 */
#define DB_EQ_RING_ID_EXT_MASK_SHIFT	(2) /* qid bits 9-13 placing at 11-15 */

/* Clear the interrupt for this eq */
#define DB_EQ_CLR_SHIFT			(9)	/* bit 9 */
/* Must be 1 */
#define DB_EQ_EVNT_SHIFT		(10)	/* bit 10 */
/* Number of event entries processed */
#define DB_EQ_NUM_POPPED_SHIFT		(16)	/* bits 16 - 28 */
/* Rearm bit */
#define DB_EQ_REARM_SHIFT		(29)	/* bit 29 */

/********* Compl Q door bell *************/
#define DB_CQ_OFFSET 			0x120
#define DB_CQ_RING_ID_MASK		0x3FF	/* bits 0 - 9 */
#define DB_CQ_RING_ID_EXT_MASK		0x7C00	/* bits 10-14 */
#define DB_CQ_RING_ID_EXT_MASK_SHIFT	(1)	/* qid bits 10-14
						 placing at 11-15 */

/* Number of event entries processed */
#define DB_CQ_NUM_POPPED_SHIFT		(16) 	/* bits 16 - 28 */
/* Rearm bit */
#define DB_CQ_REARM_SHIFT		(29) 	/* bit 29 */

/********** TX ULP door bell *************/
#define DB_TXULP1_OFFSET		0x60
#define DB_TXULP_RING_ID_MASK		0x7FF	/* bits 0 - 10 */
/* Number of tx entries posted */
#define DB_TXULP_NUM_POSTED_SHIFT	(16)	/* bits 16 - 29 */
#define DB_TXULP_NUM_POSTED_MASK	0x3FFF	/* bits 16 - 29 */

/********** RQ(erx) door bell ************/
#define DB_RQ_OFFSET 			0x100
#define DB_RQ_RING_ID_MASK		0x3FF	/* bits 0 - 9 */
/* Number of rx frags posted */
#define DB_RQ_NUM_POSTED_SHIFT		(24)	/* bits 24 - 31 */

/********** MCC door bell ************/
#define DB_MCCQ_OFFSET 			0x140
#define DB_MCCQ_RING_ID_MASK		0x7FF	/* bits 0 - 10 */
/* Number of entries posted */
#define DB_MCCQ_NUM_POSTED_SHIFT	(16)	/* bits 16 - 29 */

/********** SRIOV VF PCICFG OFFSET ********/
#define SRIOV_VF_PCICFG_OFFSET		(4096)

/********** FAT TABLE  ********/
#define RETRIEVE_FAT	0
#define QUERY_FAT	1

/* Flashrom related descriptors */
#define MAX_FLASH_COMP			32
#define IMAGE_TYPE_FIRMWARE		160
#define IMAGE_TYPE_BOOTCODE		224
#define IMAGE_TYPE_OPTIONROM		32

#define NUM_FLASHDIR_ENTRIES		32

#define OPTYPE_ISCSI_ACTIVE		0
#define OPTYPE_REDBOOT			1
#define OPTYPE_BIOS			2
#define OPTYPE_PXE_BIOS			3
#define OPTYPE_FCOE_BIOS		8
#define OPTYPE_ISCSI_BACKUP		9
#define OPTYPE_FCOE_FW_ACTIVE		10
#define OPTYPE_FCOE_FW_BACKUP		11
#define OPTYPE_NCSI_FW			13
#define OPTYPE_PHY_FW			99
#define TN_8022				13

#define ILLEGAL_IOCTL_REQ		2
#define FLASHROM_OPER_PHY_FLASH		9
#define FLASHROM_OPER_PHY_SAVE		10
#define FLASHROM_OPER_FLASH		1
#define FLASHROM_OPER_SAVE		2
#define FLASHROM_OPER_REPORT		4

#define FLASH_IMAGE_MAX_SIZE_g2		(1310720) /* Max firmware image size */
#define FLASH_BIOS_IMAGE_MAX_SIZE_g2	(262144)  /* Max OPTION ROM image sz */
#define FLASH_REDBOOT_IMAGE_MAX_SIZE_g2	(262144)  /* Max Redboot image sz    */
#define FLASH_IMAGE_MAX_SIZE_g3		(2097152) /* Max firmware image size */
#define FLASH_BIOS_IMAGE_MAX_SIZE_g3	(524288)  /* Max OPTION ROM image sz */
#define FLASH_REDBOOT_IMAGE_MAX_SIZE_g3	(1048576)  /* Max Redboot image sz    */
#define FLASH_NCSI_IMAGE_MAX_SIZE_g3	(262144)
#define FLASH_PHY_FW_IMAGE_MAX_SIZE_g3	262144

#define FLASH_NCSI_MAGIC		(0x16032009)
#define FLASH_NCSI_DISABLED		(0)
#define FLASH_NCSI_ENABLED		(1)

#define FLASH_NCSI_BITFILE_HDR_OFFSET	(0x600000)

/* Offsets for components on Flash. */
#define FLASH_iSCSI_PRIMARY_IMAGE_START_g2 (1048576)
#define FLASH_iSCSI_BACKUP_IMAGE_START_g2  (2359296)
#define FLASH_FCoE_PRIMARY_IMAGE_START_g2  (3670016)
#define FLASH_FCoE_BACKUP_IMAGE_START_g2   (4980736)
#define FLASH_iSCSI_BIOS_START_g2          (7340032)
#define FLASH_PXE_BIOS_START_g2            (7864320)
#define FLASH_FCoE_BIOS_START_g2           (524288)
#define FLASH_REDBOOT_START_g2		  (0)

#define FLASH_NCSI_START_g3		   (15990784)
#define FLASH_iSCSI_PRIMARY_IMAGE_START_g3 (2097152)
#define FLASH_iSCSI_BACKUP_IMAGE_START_g3  (4194304)
#define FLASH_FCoE_PRIMARY_IMAGE_START_g3  (6291456)
#define FLASH_FCoE_BACKUP_IMAGE_START_g3   (8388608)
#define FLASH_iSCSI_BIOS_START_g3          (12582912)
#define FLASH_PXE_BIOS_START_g3            (13107200)
#define FLASH_FCoE_BIOS_START_g3           (13631488)
#define FLASH_REDBOOT_START_g3             (262144)
#define FLASH_PHY_FW_START_g3		   1310720

#define IMAGE_NCSI			16
#define IMAGE_OPTION_ROM_PXE		32
#define IMAGE_OPTION_ROM_FCoE		33
#define IMAGE_OPTION_ROM_ISCSI		34
#define IMAGE_FLASHISM_JUMPVECTOR	48
#define IMAGE_FLASH_ISM			49
#define IMAGE_JUMP_VECTOR		50
#define IMAGE_FIRMWARE_iSCSI		160
#define IMAGE_FIRMWARE_COMP_iSCSI	161
#define IMAGE_FIRMWARE_FCoE		162
#define IMAGE_FIRMWARE_COMP_FCoE	163
#define IMAGE_FIRMWARE_BACKUP_iSCSI	176
#define IMAGE_FIRMWARE_BACKUP_COMP_iSCSI 177
#define IMAGE_FIRMWARE_BACKUP_FCoE	178
#define IMAGE_FIRMWARE_BACKUP_COMP_FCoE 179
#define IMAGE_FIRMWARE_PHY		192
#define IMAGE_BOOT_CODE			224

/************* Rx Packet Type Encoding **************/
#define BE_UNICAST_PACKET		0
#define BE_MULTICAST_PACKET		1
#define BE_BROADCAST_PACKET		2
#define BE_RSVD_PACKET			3

/*
 * BE descriptors: host memory data structures whose formats
 * are hardwired in BE silicon.
 */
/* Event Queue Descriptor */
#define EQ_ENTRY_VALID_MASK 		0x1	/* bit 0 */
#define EQ_ENTRY_RES_ID_MASK 		0xFFFF	/* bits 16 - 31 */
#define EQ_ENTRY_RES_ID_SHIFT 		16

struct be_eq_entry {
	u32 evt;
};

/* TX Queue Descriptor */
#define ETH_WRB_FRAG_LEN_MASK		0xFFFF
struct be_eth_wrb {
	u32 frag_pa_hi;		/* dword 0 */
	u32 frag_pa_lo;		/* dword 1 */
	u32 rsvd0;		/* dword 2 */
	u32 frag_len;		/* dword 3: bits 0 - 15 */
} __packed;

/* Pseudo amap definition for eth_hdr_wrb in which each bit of the
 * actual structure is defined as a byte : used to calculate
 * offset/shift/mask of each field */
struct amap_eth_hdr_wrb {
	u8 rsvd0[32];		/* dword 0 */
	u8 rsvd1[32];		/* dword 1 */
	u8 complete;		/* dword 2 */
	u8 event;
	u8 crc;
	u8 forward;
	u8 lso6;
	u8 mgmt;
	u8 ipcs;
	u8 udpcs;
	u8 tcpcs;
	u8 lso;
	u8 vlan;
	u8 gso[2];
	u8 num_wrb[5];
	u8 lso_mss[14];
	u8 len[16];		/* dword 3 */
	u8 vlan_tag[16];
} __packed;

struct be_eth_hdr_wrb {
	u32 dw[4];
};

/* TX Compl Queue Descriptor */

/* Pseudo amap definition for eth_tx_compl in which each bit of the
 * actual structure is defined as a byte: used to calculate
 * offset/shift/mask of each field */
struct amap_eth_tx_compl {
	u8 wrb_index[16];	/* dword 0 */
	u8 ct[2]; 		/* dword 0 */
	u8 port[2];		/* dword 0 */
	u8 rsvd0[8];		/* dword 0 */
	u8 status[4];		/* dword 0 */
	u8 user_bytes[16];	/* dword 1 */
	u8 nwh_bytes[8];	/* dword 1 */
	u8 lso;			/* dword 1 */
	u8 cast_enc[2];		/* dword 1 */
	u8 rsvd1[5];		/* dword 1 */
	u8 rsvd2[32];		/* dword 2 */
	u8 pkts[16];		/* dword 3 */
	u8 ringid[11];		/* dword 3 */
	u8 hash_val[4];		/* dword 3 */
	u8 valid;		/* dword 3 */
} __packed;

struct be_eth_tx_compl {
	u32 dw[4];
};

/* RX Queue Descriptor */
struct be_eth_rx_d {
	u32 fragpa_hi;
	u32 fragpa_lo;
};

/* RX Compl Queue Descriptor */

/* Pseudo amap definition for BE2 and BE3 legacy mode eth_rx_compl in which
 * each bit of the actual structure is defined as a byte: used to calculate
 * offset/shift/mask of each field */
struct amap_eth_rx_compl_v0 {
	u8 vlan_tag[16];	/* dword 0 */
	u8 pktsize[14];		/* dword 0 */
	u8 port;		/* dword 0 */
	u8 ip_opt;		/* dword 0 */
	u8 err;			/* dword 1 */
	u8 rsshp;		/* dword 1 */
	u8 ipf;			/* dword 1 */
	u8 tcpf;		/* dword 1 */
	u8 udpf;		/* dword 1 */
	u8 ipcksm;		/* dword 1 */
	u8 l4_cksm;		/* dword 1 */
	u8 ip_version;		/* dword 1 */
	u8 macdst[6];		/* dword 1 */
	u8 vtp;			/* dword 1 */
	u8 rsvd0;		/* dword 1 */
	u8 fragndx[10];		/* dword 1 */
	u8 ct[2];		/* dword 1 */
	u8 sw;			/* dword 1 */
	u8 numfrags[3];		/* dword 1 */
	u8 rss_flush;		/* dword 2 */
	u8 cast_enc[2];		/* dword 2 */
	u8 vtm;			/* dword 2 */
	u8 rss_bank;		/* dword 2 */
	u8 rsvd1[23];		/* dword 2 */
	u8 lro_pkt;		/* dword 2 */
	u8 rsvd2[2];		/* dword 2 */
	u8 valid;		/* dword 2 */
	u8 rsshash[32];		/* dword 3 */
} __packed;

/* Pseudo amap definition for BE3 native mode eth_rx_compl in which
 * each bit of the actual structure is defined as a byte: used to calculate
 * offset/shift/mask of each field */
struct amap_eth_rx_compl_v1 {
	u8 vlan_tag[16];	/* dword 0 */
	u8 pktsize[14];		/* dword 0 */
	u8 vtp;			/* dword 0 */
	u8 ip_opt;		/* dword 0 */
	u8 err;			/* dword 1 */
	u8 rsshp;		/* dword 1 */
	u8 ipf;			/* dword 1 */
	u8 tcpf;		/* dword 1 */
	u8 udpf;		/* dword 1 */
	u8 ipcksm;		/* dword 1 */
	u8 l4_cksm;		/* dword 1 */
	u8 ip_version;		/* dword 1 */
	u8 macdst[7];		/* dword 1 */
	u8 rsvd0;		/* dword 1 */
	u8 fragndx[10];		/* dword 1 */
	u8 ct[2];		/* dword 1 */
	u8 sw;			/* dword 1 */
	u8 numfrags[3];		/* dword 1 */
	u8 rss_flush;		/* dword 2 */
	u8 cast_enc[2];		/* dword 2 */
	u8 vtm;			/* dword 2 */
	u8 rss_bank;		/* dword 2 */
	u8 port[2];		/* dword 2 */
	u8 vntagp;		/* dword 2 */
	u8 header_len[8];	/* dword 2 */
	u8 header_split[2];	/* dword 2 */
	u8 rsvd1[13];		/* dword 2 */
	u8 valid;		/* dword 2 */
	u8 rsshash[32];		/* dword 3 */
} __packed;

struct be_eth_rx_compl {
	u32 dw[4];
};

struct mgmt_hba_attribs {
	u8 flashrom_version_string[32];
	u8 manufacturer_name[32];
	u32 supported_modes;
	u32 rsvd0[3];
	u8 ncsi_ver_string[12];
	u32 default_extended_timeout;
	u8 controller_model_number[32];
	u8 controller_description[64];
	u8 controller_serial_number[32];
	u8 ip_version_string[32];
	u8 firmware_version_string[32];
	u8 bios_version_string[32];
	u8 redboot_version_string[32];
	u8 driver_version_string[32];
	u8 fw_on_flash_version_string[32];
	u32 functionalities_supported;
	u16 max_cdblength;
	u8 asic_revision;
	u8 generational_guid[16];
	u8 hba_port_count;
	u16 default_link_down_timeout;
	u8 iscsi_ver_min_max;
	u8 multifunction_device;
	u8 cache_valid;
	u8 hba_status;
	u8 max_domains_supported;
	u8 phy_port;
	u32 firmware_post_status;
	u32 hba_mtu[8];
	u32 rsvd1[4];
};

struct mgmt_controller_attrib {
	struct mgmt_hba_attribs hba_attribs;
	u16 pci_vendor_id;
	u16 pci_device_id;
	u16 pci_sub_vendor_id;
	u16 pci_sub_system_id;
	u8 pci_bus_number;
	u8 pci_device_number;
	u8 pci_function_number;
	u8 interface_type;
	u64 unique_identifier;
	u32 rsvd0[5];
};

struct controller_id {
	u32 vendor;
	u32 device;
	u32 subvendor;
	u32 subdevice;
};

struct flash_comp {
	unsigned long offset;
	int optype;
	int size;
	int img_type;
};

struct image_hdr {
	u32 imageid;
	u32 imageoffset;
	u32 imagelength;
	u32 image_checksum;
	u8 image_version[32];
};
struct flash_file_hdr_g2 {
	u8 sign[32];
	u32 cksum;
	u32 antidote;
	struct controller_id cont_id;
	u32 file_len;
	u32 chunk_num;
	u32 total_chunks;
	u32 num_imgs;
	u8 build[24];
};

struct flash_file_hdr_g3 {
	u8 sign[52];
	u8 ufi_version[4];
	u32 file_len;
	u32 cksum;
	u32 antidote;
	u32 num_imgs;
	u8 build[24];
	u8 rsvd[32];
};

struct flash_section_hdr {
	u32 format_rev;
	u32 cksum;
	u32 antidote;
	u32 num_images;
	u8 id_string[128];
	u32 rsvd[4];
} __packed;

struct flash_section_hdr_g2 {
	u32 format_rev;
	u32 cksum;
	u32 antidote;
	u32 build_num;
	u8 id_string[128];
	u32 rsvd[8];
} __packed;

struct flash_section_entry {
	u32 type;
	u32 offset;
	u32 pad_size;
	u32 image_size;
	u32 cksum;
	u32 entry_point;
	u32 rsvd0;
	u32 rsvd1;
	u8 ver_data[32];
} __packed;

struct flash_section_info {
	u8 cookie[32];
	struct flash_section_hdr fsec_hdr;
	struct flash_section_entry fsec_entry[32];
} __packed;

struct flash_section_info_g2 {
	u8 cookie[32];
	struct flash_section_hdr_g2 fsec_hdr;
	struct flash_section_entry fsec_entry[32];
} __packed;
