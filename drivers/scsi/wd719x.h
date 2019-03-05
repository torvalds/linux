/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _WD719X_H_
#define _WD719X_H_

#define WD719X_SG 255		/* Scatter/gather size */

struct wd719x_sglist {
	__le32 ptr;
	__le32 length;
} __packed;

enum wd719x_card_type {
	WD719X_TYPE_UNKNOWN = 0,
	WD719X_TYPE_7193,
	WD719X_TYPE_7197,
	WD719X_TYPE_7296,
};

union wd719x_regs {
	__le32 all;	/* All Status at once */
	struct {
		u8 OPC;		/* Opcode register */
		u8 SCSI;	/* SCSI Errors */
		u8 SUE;		/* Spider unique Errors */
		u8 INT;		/* Interrupt Status */
	} bytes;
};

/* Spider Command Block (SCB) */
struct wd719x_scb {
	__le32 Int_SCB;	/* 00-03 Internal SCB link pointer (must be cleared) */
	u8 SCB_opcode;	/* 04 SCB Command opcode */
	u8 CDB_tag;	/* 05 SCSI Tag byte for CDB queues (0 if untagged) */
	u8 lun;		/* 06 SCSI LUN */
	u8 devid;	/* 07 SCSI Device ID */
	u8 CDB[16];	/* 08-23 SCSI CDB (16 bytes as defined by ANSI spec. */
	__le32 data_p;	/* 24-27 Data transfer address (or SG list address) */
	__le32 data_length; /* 28-31 Data transfer Length (or SG list length) */
	__le32 CDB_link;    /* 32-35 SCSI CDB Link Ptr */
	__le32 sense_buf;   /* 36-39 Auto request sense buffer address */
	u8 sense_buf_length;/* 40 Auto request sense transfer length */
	u8 reserved;	/* 41 reserved */
	u8 SCB_options;	/* 42 SCB-options */
	u8 SCB_tag_msg;	/* 43 Tagged messages options */
	/* Not filled in by host */
	__le32 req_ptr;	/* 44-47 Ptr to Host Request returned on interrupt */
	u8 host_opcode;	/* 48 Host Command Opcode (same as AMR_00) */
	u8 scsi_stat;	/* 49 SCSI Status returned */
	u8 ret_error;	/* 50 SPIDER Unique Error Code returned (SUE) */
	u8 int_stat;	/* 51 Message u8 / Interrupt Status byte returned */
	__le32 transferred; /* 52-55 Bytes Transferred */
	u8 last_trans[3];  /* 56-58 Bytes Transferred in last session */
	u8 length;	/* 59 SCSI Messages Length (1-8) */
	u8 sync_offset;	/* 60 Synchronous offset */
	u8 sync_rate;	/* 61 Synchronous rate */
	u8 flags[2];	/* 62-63 SCB specific flags (local to each thread) */
	/* everything below is for driver use (not used by card) */
	dma_addr_t phys;	/* bus address of the SCB */
	struct scsi_cmnd *cmd;	/* a copy of the pointer we were passed */
	struct list_head list;
	struct wd719x_sglist sg_list[WD719X_SG] __aligned(8); /* SG list */
} __packed;

struct wd719x {
	struct Scsi_Host *sh;	/* pointer to host structure */
	struct pci_dev *pdev;
	void __iomem *base;
	enum wd719x_card_type type; /* type of card */
	void *fw_virt;		/* firmware buffer CPU address */
	dma_addr_t fw_phys;	/* firmware buffer bus address */
	size_t fw_size;		/* firmware buffer size */
	struct wd719x_host_param *params; /* host parameters (EEPROM) */
	dma_addr_t params_phys; /* host parameters bus address */
	void *hash_virt;	/* hash table CPU address */
	dma_addr_t hash_phys;	/* hash table bus address */
	struct list_head active_scbs;
};

/* timeout delays in microsecs */
#define WD719X_WAIT_FOR_CMD_READY	500
#define WD719X_WAIT_FOR_RISC		2000
#define WD719X_WAIT_FOR_SCSI_RESET	3000000

/* All commands except 0x00 generate an interrupt */
#define WD719X_CMD_READY	0x00 /* Command register ready (or noop) */
#define WD719X_CMD_INIT_RISC	0x01 /* Initialize RISC */
/* 0x02 is reserved */
#define WD719X_CMD_BUSRESET	0x03 /* Assert SCSI bus reset */
#define WD719X_CMD_READ_FIRMVER	0x04 /* Read the Firmware Revision */
#define WD719X_CMD_ECHO_BYTES	0x05 /* Echo command bytes (DW) */
/* 0x06 is reserved */
/* 0x07 is reserved */
#define WD719X_CMD_GET_PARAM	0x08 /* Get programmable parameters */
#define WD719X_CMD_SET_PARAM	0x09 /* Set programmable parameters */
#define WD719X_CMD_SLEEP	0x0a /* Put SPIDER to sleep */
#define WD719X_CMD_READ_INIT	0x0b /* Read initialization parameters */
#define WD719X_CMD_RESTORE_INIT	0x0c /* Restore initialization parameters */
/* 0x0d is reserved */
/* 0x0e is reserved */
/* 0x0f is reserved */
#define WD719X_CMD_ABORT_TAG	0x10 /* Send Abort tag message to target */
#define WD719X_CMD_ABORT	0x11 /* Send Abort message to target */
#define WD719X_CMD_RESET	0x12 /* Send Reset message to target */
#define WD719X_CMD_INIT_SCAM	0x13 /* Initiate SCAM */
#define WD719X_CMD_GET_SYNC	0x14 /* Get synchronous rates */
#define WD719X_CMD_SET_SYNC	0x15 /* Set synchronous rates */
#define WD719X_CMD_GET_WIDTH	0x16 /* Get SCSI bus width */
#define WD719X_CMD_SET_WIDTH	0x17 /* Set SCSI bus width */
#define WD719X_CMD_GET_TAGS	0x18 /* Get tag flags */
#define WD719X_CMD_SET_TAGS	0x19 /* Set tag flags */
#define WD719X_CMD_GET_PARAM2	0x1a /* Get programmable params (format 2) */
#define WD719X_CMD_SET_PARAM2	0x1b /* Set programmable params (format 2) */
/* Commands with request pointers (mailbox) */
#define WD719X_CMD_PROCESS_SCB	0x80 /* Process SCSI Control Block (SCB) */
/* No interrupt generated on acceptance of SCB pointer */

/* interrupt status defines */
#define WD719X_INT_NONE		0x00 /* No interrupt pending */
#define WD719X_INT_NOERRORS	0x01 /* Command completed with no errors */
#define WD719X_INT_LINKNOERRORS	0x02 /* link cmd completed with no errors */
#define WD719X_INT_LINKNOSTATUS	0x03 /* link cmd completed with no flag set */
#define WD719X_INT_ERRORSLOGGED	0x04 /* cmd completed with errors logged */
#define WD719X_INT_SPIDERFAILED	0x05 /* cmd failed without valid SCSI status */
#define WD719X_INT_BADINT	0x80 /* unsolicited interrupt */
#define WD719X_INT_PIOREADY	0xf0 /* data ready for PIO output */

/* Spider Unique Error Codes (SUE) */
#define WD719X_SUE_NOERRORS	0x00 /* No errors detected by SPIDER */
#define WD719X_SUE_REJECTED	0x01 /* Command Rejected (bad opcode/param) */
#define WD719X_SUE_SCBQFULL	0x02 /* SCB queue full */
/* 0x03 is reserved */
#define WD719X_SUE_TERM		0x04 /* Host terminated SCB via primative cmd */
#define WD719X_SUE_CHAN1PAR	0x05 /* PCI Channel 1 parity error occurred */
#define WD719X_SUE_CHAN1ABORT	0x06 /* PCI Channel 1 system abort occurred */
#define WD719X_SUE_CHAN23PAR	0x07 /* PCI Channel 2/3 parity error occurred */
#define WD719X_SUE_CHAN23ABORT	0x08 /* PCI Channel 2/3 system abort occurred */
#define WD719X_SUE_TIMEOUT	0x10 /* Selection/reselection timeout */
#define WD719X_SUE_RESET	0x11 /* SCSI bus reset occurred */
#define WD719X_SUE_BUSERROR	0x12 /* SCSI bus error */
#define WD719X_SUE_WRONGWAY	0x13 /* Wrong data transfer dir set by target */
#define WD719X_SUE_BADPHASE	0x14 /* SCSI phase illegal or unexpected */
#define WD719X_SUE_TOOLONG	0x15 /* target requested too much data */
#define WD719X_SUE_BUSFREE	0x16 /* Unexpected SCSI bus free */
#define WD719X_SUE_ARSDONE	0x17 /* Auto request sense executed */
#define WD719X_SUE_IGNORED	0x18 /* SCSI message was ignored by target */
#define WD719X_SUE_WRONGTAGS	0x19 /* Tagged SCB & tags off (or vice versa) */
#define WD719X_SUE_BADTAGS	0x1a /* Wrong tag message type for target */
#define WD719X_SUE_NOSCAMID	0x1b /* No SCAM soft ID available */

/* code sizes */
#define	WD719X_HASH_TABLE_SIZE	4096

/* Advanced Mode Registers */
/* Regs 0x00..0x1f are for Advanced Mode of the card (RISC is running). */
#define WD719X_AMR_COMMAND		0x00
#define WD719X_AMR_CMD_PARAM		0x01
#define WD719X_AMR_CMD_PARAM_2		0x02
#define WD719X_AMR_CMD_PARAM_3		0x03
#define WD719X_AMR_SCB_IN		0x04

#define WD719X_AMR_BIOS_SHARE_INT	0x0f

#define WD719X_AMR_SCB_OUT		0x18
#define WD719X_AMR_OP_CODE		0x1c
#define WD719X_AMR_SCSI_STATUS		0x1d
#define WD719X_AMR_SCB_ERROR		0x1e
#define WD719X_AMR_INT_STATUS		0x1f

#define WD719X_DISABLE_INT	0x80

/* SCB flags */
#define WD719X_SCB_FLAGS_CHECK_DIRECTION	0x01
#define WD719X_SCB_FLAGS_PCI_TO_SCSI		0x02
#define WD719X_SCB_FLAGS_AUTO_REQUEST_SENSE	0x10
#define WD719X_SCB_FLAGS_DO_SCATTER_GATHER	0x20
#define WD719X_SCB_FLAGS_NO_DISCONNECT		0x40

/* PCI Registers used for reset, initial code download */
/* Regs 0x20..0x3f are for Normal (DOS) mode (RISC is asleep). */
#define WD719X_PCI_GPIO_CONTROL		0x3C
#define WD719X_PCI_GPIO_DATA		0x3D
#define WD719X_PCI_PORT_RESET		0x3E
#define WD719X_PCI_MODE_SELECT		0x3F

#define WD719X_PCI_EXTERNAL_ADDR	0x60
#define WD719X_PCI_INTERNAL_ADDR	0x64
#define WD719X_PCI_DMA_TRANSFER_SIZE	0x66
#define WD719X_PCI_CHANNEL2_3CMD	0x68
#define WD719X_PCI_CHANNEL2_3STATUS	0x69

#define WD719X_GPIO_ID_BITS		0x0a
#define WD719X_PRAM_BASE_ADDR		0x00

/* codes written to or read from the card */
#define WD719X_PCI_RESET		 0x01
#define WD719X_ENABLE_ADVANCE_MODE	 0x01

#define WD719X_START_CHANNEL2_3DMA	 0x17
#define WD719X_START_CHANNEL2_3DONE	 0x01
#define WD719X_START_CHANNEL2_3ABORT	 0x20

/* 33C296 GPIO bits for EEPROM pins */
#define WD719X_EE_DI	(1 << 1)
#define WD719X_EE_CS	(1 << 2)
#define WD719X_EE_CLK	(1 << 3)
#define WD719X_EE_DO	(1 << 4)

/* EEPROM contents */
struct wd719x_eeprom_header {
	u8 sig1;
	u8 sig2;
	u8 version;
	u8 checksum;
	u8 cfg_offset;
	u8 cfg_size;
	u8 setup_offset;
	u8 setup_size;
} __packed;

#define WD719X_EE_SIG1		0
#define WD719X_EE_SIG2		1
#define WD719X_EE_VERSION	2
#define WD719X_EE_CHECKSUM	3
#define WD719X_EE_CFG_OFFSET	4
#define WD719X_EE_CFG_SIZE	5
#define WD719X_EE_SETUP_OFFSET	6
#define WD719X_EE_SETUP_SIZE	7

#define WD719X_EE_SCSI_ID_MASK	0xf

/* SPIDER Host Parameters Block (=EEPROM configuration block) */
struct wd719x_host_param {
	u8 ch_1_th;	/* FIFO threshold */
	u8 scsi_conf;	/* SCSI configuration */
	u8 own_scsi_id;	/* controller SCSI ID */
	u8 sel_timeout;	/* selection timeout*/
	u8 sleep_timer;	/* seep timer */
	__le16 cdb_size;/* CDB size groups */
	__le16 tag_en;	/* Tag msg enables (ID 0-15) */
	u8 scsi_pad;	/* SCSI pad control */
	__le32 wide;	/* WIDE msg options (ID 0-15) */
	__le32 sync;	/* SYNC msg options (ID 0-15) */
	u8 soft_mask;	/* soft error mask */
	u8 unsol_mask;	/* unsolicited error mask */
} __packed;

#endif /* _WD719X_H_ */
