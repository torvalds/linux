/*
 * QLogic iSCSI HBA Driver
 * Copyright (c)  2003-2013 QLogic Corporation
 *
 * See LICENSE.qla4xxx for copyright and licensing details.
 */

#ifndef __QL483XX_H
#define __QL483XX_H

/* Indirectly Mapped Registers */
#define QLA83XX_FLASH_SPI_STATUS	0x2808E010
#define QLA83XX_FLASH_SPI_CONTROL	0x2808E014
#define QLA83XX_FLASH_STATUS		0x42100004
#define QLA83XX_FLASH_CONTROL		0x42110004
#define QLA83XX_FLASH_ADDR		0x42110008
#define QLA83XX_FLASH_WRDATA		0x4211000C
#define QLA83XX_FLASH_RDDATA		0x42110018
#define QLA83XX_FLASH_DIRECT_WINDOW	0x42110030
#define QLA83XX_FLASH_DIRECT_DATA(DATA) (0x42150000 | (0x0000FFFF&DATA))

/* Directly Mapped Registers in 83xx register table */

/* Flash access regs */
#define QLA83XX_FLASH_LOCK		0x3850
#define QLA83XX_FLASH_UNLOCK		0x3854
#define QLA83XX_FLASH_LOCK_ID		0x3500

/* Driver Lock regs */
#define QLA83XX_DRV_LOCK		0x3868
#define QLA83XX_DRV_UNLOCK		0x386C
#define QLA83XX_DRV_LOCK_ID		0x3504
#define QLA83XX_DRV_LOCKRECOVERY	0x379C

/* IDC version */
#define QLA83XX_IDC_VER_MAJ_VALUE       0x1
#define QLA83XX_IDC_VER_MIN_VALUE       0x0

/* IDC Registers : Driver Coexistence Defines */
#define QLA83XX_CRB_IDC_VER_MAJOR	0x3780
#define QLA83XX_CRB_IDC_VER_MINOR	0x3798
#define QLA83XX_IDC_DRV_CTRL		0x3790
#define QLA83XX_IDC_DRV_AUDIT		0x3794
#define QLA83XX_SRE_SHIM_CONTROL	0x0D200284
#define QLA83XX_PORT0_RXB_PAUSE_THRS	0x0B2003A4
#define QLA83XX_PORT1_RXB_PAUSE_THRS	0x0B2013A4
#define QLA83XX_PORT0_RXB_TC_MAX_CELL	0x0B200388
#define QLA83XX_PORT1_RXB_TC_MAX_CELL	0x0B201388
#define QLA83XX_PORT0_RXB_TC_STATS	0x0B20039C
#define QLA83XX_PORT1_RXB_TC_STATS	0x0B20139C
#define QLA83XX_PORT2_IFB_PAUSE_THRS	0x0B200704
#define QLA83XX_PORT3_IFB_PAUSE_THRS	0x0B201704

/* set value to pause threshold value */
#define QLA83XX_SET_PAUSE_VAL		0x0
#define QLA83XX_SET_TC_MAX_CELL_VAL	0x03FF03FF

#define QLA83XX_RESET_CONTROL		0x28084E50
#define QLA83XX_RESET_REG		0x28084E60
#define QLA83XX_RESET_PORT0		0x28084E70
#define QLA83XX_RESET_PORT1		0x28084E80
#define QLA83XX_RESET_PORT2		0x28084E90
#define QLA83XX_RESET_PORT3		0x28084EA0
#define QLA83XX_RESET_SRE_SHIM		0x28084EB0
#define QLA83XX_RESET_EPG_SHIM		0x28084EC0
#define QLA83XX_RESET_ETHER_PCS		0x28084ED0

/* qla_83xx_reg_tbl registers */
#define QLA83XX_PEG_HALT_STATUS1	0x34A8
#define QLA83XX_PEG_HALT_STATUS2	0x34AC
#define QLA83XX_PEG_ALIVE_COUNTER	0x34B0 /* FW_HEARTBEAT */
#define QLA83XX_FW_CAPABILITIES		0x3528
#define QLA83XX_CRB_DRV_ACTIVE		0x3788 /* IDC_DRV_PRESENCE */
#define QLA83XX_CRB_DEV_STATE		0x3784 /* IDC_DEV_STATE */
#define QLA83XX_CRB_DRV_STATE		0x378C /* IDC_DRV_ACK */
#define QLA83XX_CRB_DRV_SCRATCH		0x3548
#define QLA83XX_CRB_DEV_PART_INFO1	0x37E0
#define QLA83XX_CRB_DEV_PART_INFO2	0x37E4

#define QLA83XX_FW_VER_MAJOR		0x3550
#define QLA83XX_FW_VER_MINOR		0x3554
#define QLA83XX_FW_VER_SUB		0x3558
#define QLA83XX_NPAR_STATE		0x359C
#define QLA83XX_FW_IMAGE_VALID		0x35FC
#define QLA83XX_CMDPEG_STATE		0x3650
#define QLA83XX_ASIC_TEMP		0x37B4
#define QLA83XX_FW_API			0x356C
#define QLA83XX_DRV_OP_MODE		0x3570

static const uint32_t qla4_83xx_reg_tbl[] = {
	QLA83XX_PEG_HALT_STATUS1,
	QLA83XX_PEG_HALT_STATUS2,
	QLA83XX_PEG_ALIVE_COUNTER,
	QLA83XX_CRB_DRV_ACTIVE,
	QLA83XX_CRB_DEV_STATE,
	QLA83XX_CRB_DRV_STATE,
	QLA83XX_CRB_DRV_SCRATCH,
	QLA83XX_CRB_DEV_PART_INFO1,
	QLA83XX_CRB_IDC_VER_MAJOR,
	QLA83XX_FW_VER_MAJOR,
	QLA83XX_FW_VER_MINOR,
	QLA83XX_FW_VER_SUB,
	QLA83XX_CMDPEG_STATE,
	QLA83XX_ASIC_TEMP,
};

#define QLA83XX_CRB_WIN_BASE		0x3800
#define QLA83XX_CRB_WIN_FUNC(f)		(QLA83XX_CRB_WIN_BASE+((f)*4))
#define QLA83XX_SEM_LOCK_BASE		0x3840
#define QLA83XX_SEM_UNLOCK_BASE		0x3844
#define QLA83XX_SEM_LOCK_FUNC(f)	(QLA83XX_SEM_LOCK_BASE+((f)*8))
#define QLA83XX_SEM_UNLOCK_FUNC(f)	(QLA83XX_SEM_UNLOCK_BASE+((f)*8))
#define QLA83XX_LINK_STATE(f)		(0x3698+((f) > 7 ? 4 : 0))
#define QLA83XX_LINK_SPEED(f)		(0x36E0+(((f) >> 2) * 4))
#define QLA83XX_MAX_LINK_SPEED(f)       (0x36F0+(((f) / 4) * 4))
#define QLA83XX_LINK_SPEED_FACTOR	10

/* FLASH API Defines */
#define QLA83xx_FLASH_MAX_WAIT_USEC	100
#define QLA83XX_FLASH_LOCK_TIMEOUT	10000
#define QLA83XX_FLASH_SECTOR_SIZE	65536
#define QLA83XX_DRV_LOCK_TIMEOUT	2000
#define QLA83XX_FLASH_SECTOR_ERASE_CMD	0xdeadbeef
#define QLA83XX_FLASH_WRITE_CMD		0xdacdacda
#define QLA83XX_FLASH_BUFFER_WRITE_CMD	0xcadcadca
#define QLA83XX_FLASH_READ_RETRY_COUNT	2000
#define QLA83XX_FLASH_STATUS_READY	0x6
#define QLA83XX_FLASH_BUFFER_WRITE_MIN	2
#define QLA83XX_FLASH_BUFFER_WRITE_MAX	64
#define QLA83XX_FLASH_STATUS_REG_POLL_DELAY 1
#define QLA83XX_ERASE_MODE		1
#define QLA83XX_WRITE_MODE		2
#define QLA83XX_DWORD_WRITE_MODE	3

#define QLA83XX_GLOBAL_RESET		0x38CC
#define QLA83XX_WILDCARD		0x38F0
#define QLA83XX_INFORMANT		0x38FC
#define QLA83XX_HOST_MBX_CTRL		0x3038
#define QLA83XX_FW_MBX_CTRL		0x303C
#define QLA83XX_BOOTLOADER_ADDR		0x355C
#define QLA83XX_BOOTLOADER_SIZE		0x3560
#define QLA83XX_FW_IMAGE_ADDR		0x3564
#define QLA83XX_MBX_INTR_ENABLE		0x1000
#define QLA83XX_MBX_INTR_MASK		0x1200

/* IDC Control Register bit defines */
#define DONTRESET_BIT0		0x1
#define GRACEFUL_RESET_BIT1	0x2

#define QLA83XX_HALT_STATUS_INFORMATIONAL	(0x1 << 29)
#define QLA83XX_HALT_STATUS_FW_RESET		(0x2 << 29)
#define QLA83XX_HALT_STATUS_UNRECOVERABLE	(0x4 << 29)

/* Firmware image definitions */
#define QLA83XX_BOOTLOADER_FLASH_ADDR	0x10000
#define QLA83XX_BOOT_FROM_FLASH		0

#define QLA83XX_IDC_PARAM_ADDR		0x3e8020
/* Reset template definitions */
#define QLA83XX_MAX_RESET_SEQ_ENTRIES	16
#define QLA83XX_RESTART_TEMPLATE_SIZE	0x2000
#define QLA83XX_RESET_TEMPLATE_ADDR	0x4F0000
#define QLA83XX_RESET_SEQ_VERSION	0x0101

/* Reset template entry opcodes */
#define OPCODE_NOP			0x0000
#define OPCODE_WRITE_LIST		0x0001
#define OPCODE_READ_WRITE_LIST		0x0002
#define OPCODE_POLL_LIST		0x0004
#define OPCODE_POLL_WRITE_LIST		0x0008
#define OPCODE_READ_MODIFY_WRITE	0x0010
#define OPCODE_SEQ_PAUSE		0x0020
#define OPCODE_SEQ_END			0x0040
#define OPCODE_TMPL_END			0x0080
#define OPCODE_POLL_READ_LIST		0x0100

/* Template Header */
#define RESET_TMPLT_HDR_SIGNATURE	0xCAFE
struct qla4_83xx_reset_template_hdr {
	__le16	version;
	__le16	signature;
	__le16	size;
	__le16	entries;
	__le16	hdr_size;
	__le16	checksum;
	__le16	init_seq_offset;
	__le16	start_seq_offset;
} __packed;

/* Common Entry Header. */
struct qla4_83xx_reset_entry_hdr {
	__le16 cmd;
	__le16 size;
	__le16 count;
	__le16 delay;
} __packed;

/* Generic poll entry type. */
struct qla4_83xx_poll {
	__le32  test_mask;
	__le32  test_value;
} __packed;

/* Read modify write entry type. */
struct qla4_83xx_rmw {
	__le32  test_mask;
	__le32  xor_value;
	__le32  or_value;
	uint8_t shl;
	uint8_t shr;
	uint8_t index_a;
	uint8_t rsvd;
} __packed;

/* Generic Entry Item with 2 DWords. */
struct qla4_83xx_entry {
	__le32 arg1;
	__le32 arg2;
} __packed;

/* Generic Entry Item with 4 DWords.*/
struct qla4_83xx_quad_entry {
	__le32 dr_addr;
	__le32 dr_value;
	__le32 ar_addr;
	__le32 ar_value;
} __packed;

struct qla4_83xx_reset_template {
	int seq_index;
	int seq_error;
	int array_index;
	uint32_t array[QLA83XX_MAX_RESET_SEQ_ENTRIES];
	uint8_t *buff;
	uint8_t *stop_offset;
	uint8_t *start_offset;
	uint8_t *init_offset;
	struct qla4_83xx_reset_template_hdr *hdr;
	uint8_t seq_end;
	uint8_t template_end;
};

/* POLLRD Entry */
struct qla83xx_minidump_entry_pollrd {
	struct qla8xxx_minidump_entry_hdr h;
	uint32_t select_addr;
	uint32_t read_addr;
	uint32_t select_value;
	uint16_t select_value_stride;
	uint16_t op_count;
	uint32_t poll_wait;
	uint32_t poll_mask;
	uint32_t data_size;
	uint32_t rsvd_1;
};

struct qla8044_minidump_entry_rddfe {
	struct qla8xxx_minidump_entry_hdr h;
	uint32_t addr_1;
	uint32_t value;
	uint8_t stride;
	uint8_t stride2;
	uint16_t count;
	uint32_t poll;
	uint32_t mask;
	uint32_t modify_mask;
	uint32_t data_size;
	uint32_t rsvd;

} __packed;

struct qla8044_minidump_entry_rdmdio {
	struct qla8xxx_minidump_entry_hdr h;

	uint32_t addr_1;
	uint32_t addr_2;
	uint32_t value_1;
	uint8_t stride_1;
	uint8_t stride_2;
	uint16_t count;
	uint32_t poll;
	uint32_t mask;
	uint32_t value_2;
	uint32_t data_size;

} __packed;

struct qla8044_minidump_entry_pollwr {
	struct qla8xxx_minidump_entry_hdr h;
	uint32_t addr_1;
	uint32_t addr_2;
	uint32_t value_1;
	uint32_t value_2;
	uint32_t poll;
	uint32_t mask;
	uint32_t data_size;
	uint32_t rsvd;

} __packed;

/* RDMUX2 Entry */
struct qla83xx_minidump_entry_rdmux2 {
	struct qla8xxx_minidump_entry_hdr h;
	uint32_t select_addr_1;
	uint32_t select_addr_2;
	uint32_t select_value_1;
	uint32_t select_value_2;
	uint32_t op_count;
	uint32_t select_value_mask;
	uint32_t read_addr;
	uint8_t select_value_stride;
	uint8_t data_size;
	uint8_t rsvd[2];
};

/* POLLRDMWR Entry */
struct qla83xx_minidump_entry_pollrdmwr {
	struct qla8xxx_minidump_entry_hdr h;
	uint32_t addr_1;
	uint32_t addr_2;
	uint32_t value_1;
	uint32_t value_2;
	uint32_t poll_wait;
	uint32_t poll_mask;
	uint32_t modify_mask;
	uint32_t data_size;
};

/* IDC additional information */
struct qla4_83xx_idc_information {
	uint32_t request_desc;  /* IDC request descriptor */
	uint32_t info1; /* IDC additional info */
	uint32_t info2; /* IDC additional info */
	uint32_t info3; /* IDC additional info */
};

#define QLA83XX_PEX_DMA_ENGINE_INDEX		8
#define QLA83XX_PEX_DMA_BASE_ADDRESS		0x77320000
#define QLA83XX_PEX_DMA_NUM_OFFSET		0x10000
#define QLA83XX_PEX_DMA_CMD_ADDR_LOW		0x0
#define QLA83XX_PEX_DMA_CMD_ADDR_HIGH		0x04
#define QLA83XX_PEX_DMA_CMD_STS_AND_CNTRL	0x08

#define QLA83XX_PEX_DMA_READ_SIZE	(16 * 1024)
#define QLA83XX_PEX_DMA_MAX_WAIT	(100 * 100) /* Max wait of 100 msecs */

/* Read Memory: For Pex-DMA */
struct qla4_83xx_minidump_entry_rdmem_pex_dma {
	struct qla8xxx_minidump_entry_hdr h;
	uint32_t desc_card_addr;
	uint16_t dma_desc_cmd;
	uint8_t rsvd[2];
	uint32_t start_dma_cmd;
	uint8_t rsvd2[12];
	uint32_t read_addr;
	uint32_t read_data_size;
};

struct qla4_83xx_pex_dma_descriptor {
	struct {
		uint32_t read_data_size; /* 0-23: size, 24-31: rsvd */
		uint8_t rsvd[2];
		uint16_t dma_desc_cmd;
	} cmd;
	uint64_t src_addr;
	uint64_t dma_bus_addr; /* 0-3: desc-cmd, 4-7: pci-func,
				* 8-15: desc-cmd */
	uint8_t rsvd[24];
} __packed;

#endif
