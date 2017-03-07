/*
 * QLogic Fibre Channel HBA Driver
 * Copyright (c)  2003-2014 QLogic Corporation
 *
 * See LICENSE.qla2xxx for copyright and licensing details.
 */

#ifndef __QLA_NX2_H
#define __QLA_NX2_H

#define QSNT_ACK_TOV				30
#define INTENT_TO_RECOVER			0x01
#define PROCEED_TO_RECOVER			0x02
#define IDC_LOCK_RECOVERY_OWNER_MASK		0x3C
#define IDC_LOCK_RECOVERY_STATE_MASK		0x3
#define IDC_LOCK_RECOVERY_STATE_SHIFT_BITS	2

#define QLA8044_DRV_LOCK_MSLEEP		200
#define QLA8044_ADDR_DDR_NET		(0x0000000000000000ULL)
#define QLA8044_ADDR_DDR_NET_MAX	(0x000000000fffffffULL)

#define MD_MIU_TEST_AGT_WRDATA_LO		0x410000A0
#define MD_MIU_TEST_AGT_WRDATA_HI		0x410000A4
#define MD_MIU_TEST_AGT_WRDATA_ULO		0x410000B0
#define MD_MIU_TEST_AGT_WRDATA_UHI		0x410000B4
#define MD_MIU_TEST_AGT_RDDATA_LO		0x410000A8
#define MD_MIU_TEST_AGT_RDDATA_HI		0x410000AC
#define MD_MIU_TEST_AGT_RDDATA_ULO		0x410000B8
#define MD_MIU_TEST_AGT_RDDATA_UHI		0x410000BC

/* MIU_TEST_AGT_CTRL flags. work for SIU as well */
#define MIU_TA_CTL_WRITE_ENABLE	(MIU_TA_CTL_WRITE | MIU_TA_CTL_ENABLE)
#define MIU_TA_CTL_WRITE_START	(MIU_TA_CTL_WRITE | MIU_TA_CTL_ENABLE |	\
				 MIU_TA_CTL_START)
#define MIU_TA_CTL_START_ENABLE	(MIU_TA_CTL_START | MIU_TA_CTL_ENABLE)

/* Imbus address bit used to indicate a host address. This bit is
 * eliminated by the pcie bar and bar select before presentation
 * over pcie. */
/* host memory via IMBUS */
#define QLA8044_P2_ADDR_PCIE	(0x0000000800000000ULL)
#define QLA8044_P3_ADDR_PCIE	(0x0000008000000000ULL)
#define QLA8044_ADDR_PCIE_MAX	(0x0000000FFFFFFFFFULL)
#define QLA8044_ADDR_OCM0	(0x0000000200000000ULL)
#define QLA8044_ADDR_OCM0_MAX	(0x00000002000fffffULL)
#define QLA8044_ADDR_OCM1	(0x0000000200400000ULL)
#define QLA8044_ADDR_OCM1_MAX	(0x00000002004fffffULL)
#define QLA8044_ADDR_QDR_NET	(0x0000000300000000ULL)
#define QLA8044_P2_ADDR_QDR_NET_MAX	(0x00000003001fffffULL)
#define QLA8044_P3_ADDR_QDR_NET_MAX	(0x0000000303ffffffULL)
#define QLA8044_ADDR_QDR_NET_MAX	(0x0000000307ffffffULL)
#define QLA8044_PCI_CRBSPACE		((unsigned long)0x06000000)
#define QLA8044_PCI_DIRECT_CRB		((unsigned long)0x04400000)
#define QLA8044_PCI_CAMQM		((unsigned long)0x04800000)
#define QLA8044_PCI_CAMQM_MAX		((unsigned long)0x04ffffff)
#define QLA8044_PCI_DDR_NET		((unsigned long)0x00000000)
#define QLA8044_PCI_QDR_NET		((unsigned long)0x04000000)
#define QLA8044_PCI_QDR_NET_MAX		((unsigned long)0x043fffff)

/*  PCI Windowing for DDR regions.  */
static inline bool addr_in_range(u64 addr, u64 low, u64 high)
{
	return addr <= high && addr >= low;
}

/* Indirectly Mapped Registers */
#define QLA8044_FLASH_SPI_STATUS	0x2808E010
#define QLA8044_FLASH_SPI_CONTROL	0x2808E014
#define QLA8044_FLASH_STATUS		0x42100004
#define QLA8044_FLASH_CONTROL		0x42110004
#define QLA8044_FLASH_ADDR		0x42110008
#define QLA8044_FLASH_WRDATA		0x4211000C
#define QLA8044_FLASH_RDDATA		0x42110018
#define QLA8044_FLASH_DIRECT_WINDOW	0x42110030
#define QLA8044_FLASH_DIRECT_DATA(DATA) (0x42150000 | (0x0000FFFF&DATA))

/* Flash access regs */
#define QLA8044_FLASH_LOCK		0x3850
#define QLA8044_FLASH_UNLOCK		0x3854
#define QLA8044_FLASH_LOCK_ID		0x3500

/* Driver Lock regs */
#define QLA8044_DRV_LOCK		0x3868
#define QLA8044_DRV_UNLOCK		0x386C
#define QLA8044_DRV_LOCK_ID		0x3504
#define QLA8044_DRV_LOCKRECOVERY	0x379C

/* IDC version */
#define QLA8044_IDC_VER_MAJ_VALUE       0x1
#define QLA8044_IDC_VER_MIN_VALUE       0x0

/* IDC Registers : Driver Coexistence Defines */
#define QLA8044_CRB_IDC_VER_MAJOR	0x3780
#define QLA8044_CRB_IDC_VER_MINOR	0x3798
#define QLA8044_IDC_DRV_AUDIT		0x3794
#define QLA8044_SRE_SHIM_CONTROL	0x0D200284
#define QLA8044_PORT0_RXB_PAUSE_THRS	0x0B2003A4
#define QLA8044_PORT1_RXB_PAUSE_THRS	0x0B2013A4
#define QLA8044_PORT0_RXB_TC_MAX_CELL	0x0B200388
#define QLA8044_PORT1_RXB_TC_MAX_CELL	0x0B201388
#define QLA8044_PORT0_RXB_TC_STATS	0x0B20039C
#define QLA8044_PORT1_RXB_TC_STATS	0x0B20139C
#define QLA8044_PORT2_IFB_PAUSE_THRS	0x0B200704
#define QLA8044_PORT3_IFB_PAUSE_THRS	0x0B201704

/* set value to pause threshold value */
#define QLA8044_SET_PAUSE_VAL		0x0
#define QLA8044_SET_TC_MAX_CELL_VAL	0x03FF03FF
#define QLA8044_PEG_HALT_STATUS1	0x34A8
#define QLA8044_PEG_HALT_STATUS2	0x34AC
#define QLA8044_PEG_ALIVE_COUNTER	0x34B0 /* FW_HEARTBEAT */
#define QLA8044_FW_CAPABILITIES		0x3528
#define QLA8044_CRB_DRV_ACTIVE		0x3788 /* IDC_DRV_PRESENCE */
#define QLA8044_CRB_DEV_STATE		0x3784 /* IDC_DEV_STATE */
#define QLA8044_CRB_DRV_STATE		0x378C /* IDC_DRV_ACK */
#define QLA8044_CRB_DRV_SCRATCH		0x3548
#define QLA8044_CRB_DEV_PART_INFO1	0x37E0
#define QLA8044_CRB_DEV_PART_INFO2	0x37E4
#define QLA8044_FW_VER_MAJOR		0x3550
#define QLA8044_FW_VER_MINOR		0x3554
#define QLA8044_FW_VER_SUB		0x3558
#define QLA8044_NPAR_STATE		0x359C
#define QLA8044_FW_IMAGE_VALID		0x35FC
#define QLA8044_CMDPEG_STATE		0x3650
#define QLA8044_ASIC_TEMP		0x37B4
#define QLA8044_FW_API			0x356C
#define QLA8044_DRV_OP_MODE		0x3570
#define QLA8044_CRB_WIN_BASE		0x3800
#define QLA8044_CRB_WIN_FUNC(f)		(QLA8044_CRB_WIN_BASE+((f)*4))
#define QLA8044_SEM_LOCK_BASE		0x3840
#define QLA8044_SEM_UNLOCK_BASE		0x3844
#define QLA8044_SEM_LOCK_FUNC(f)	(QLA8044_SEM_LOCK_BASE+((f)*8))
#define QLA8044_SEM_UNLOCK_FUNC(f)	(QLA8044_SEM_UNLOCK_BASE+((f)*8))
#define QLA8044_LINK_STATE(f)		(0x3698+((f) > 7 ? 4 : 0))
#define QLA8044_LINK_SPEED(f)		(0x36E0+(((f) >> 2) * 4))
#define QLA8044_MAX_LINK_SPEED(f)       (0x36F0+(((f) / 4) * 4))
#define QLA8044_LINK_SPEED_FACTOR	10
#define QLA8044_FUN7_ACTIVE_INDEX	0x80

/* FLASH API Defines */
#define QLA8044_FLASH_MAX_WAIT_USEC	100
#define QLA8044_FLASH_LOCK_TIMEOUT	10000
#define QLA8044_FLASH_SECTOR_SIZE	65536
#define QLA8044_DRV_LOCK_TIMEOUT	2000
#define QLA8044_FLASH_SECTOR_ERASE_CMD	0xdeadbeef
#define QLA8044_FLASH_WRITE_CMD		0xdacdacda
#define QLA8044_FLASH_BUFFER_WRITE_CMD	0xcadcadca
#define QLA8044_FLASH_READ_RETRY_COUNT	2000
#define QLA8044_FLASH_STATUS_READY	0x6
#define QLA8044_FLASH_BUFFER_WRITE_MIN	2
#define QLA8044_FLASH_BUFFER_WRITE_MAX	64
#define QLA8044_FLASH_STATUS_REG_POLL_DELAY 1
#define QLA8044_ERASE_MODE		1
#define QLA8044_WRITE_MODE		2
#define QLA8044_DWORD_WRITE_MODE	3
#define QLA8044_GLOBAL_RESET		0x38CC
#define QLA8044_WILDCARD		0x38F0
#define QLA8044_INFORMANT		0x38FC
#define QLA8044_HOST_MBX_CTRL		0x3038
#define QLA8044_FW_MBX_CTRL		0x303C
#define QLA8044_BOOTLOADER_ADDR		0x355C
#define QLA8044_BOOTLOADER_SIZE		0x3560
#define QLA8044_FW_IMAGE_ADDR		0x3564
#define QLA8044_MBX_INTR_ENABLE		0x1000
#define QLA8044_MBX_INTR_MASK		0x1200

/* IDC Control Register bit defines */
#define DONTRESET_BIT0		0x1
#define GRACEFUL_RESET_BIT1	0x2

/* ISP8044 PEG_HALT_STATUS1 bits */
#define QLA8044_HALT_STATUS_INFORMATIONAL (0x1 << 29)
#define QLA8044_HALT_STATUS_FW_RESET	  (0x2 << 29)
#define QLA8044_HALT_STATUS_UNRECOVERABLE (0x4 << 29)

/* Firmware image definitions */
#define QLA8044_BOOTLOADER_FLASH_ADDR	0x10000
#define QLA8044_BOOT_FROM_FLASH		0
#define QLA8044_IDC_PARAM_ADDR		0x3e8020

/* FLASH related definitions */
#define QLA8044_OPTROM_BURST_SIZE		0x100
#define QLA8044_MAX_OPTROM_BURST_DWORDS		(QLA8044_OPTROM_BURST_SIZE / 4)
#define QLA8044_MIN_OPTROM_BURST_DWORDS		2
#define QLA8044_SECTOR_SIZE			(64 * 1024)

#define QLA8044_FLASH_SPI_CTL			0x4
#define QLA8044_FLASH_FIRST_TEMP_VAL		0x00800000
#define QLA8044_FLASH_SECOND_TEMP_VAL		0x00800001
#define QLA8044_FLASH_FIRST_MS_PATTERN		0x43
#define QLA8044_FLASH_SECOND_MS_PATTERN		0x7F
#define QLA8044_FLASH_LAST_MS_PATTERN		0x7D
#define QLA8044_FLASH_STATUS_WRITE_DEF_SIG	0xFD0100
#define QLA8044_FLASH_SECOND_ERASE_MS_VAL	0x5
#define QLA8044_FLASH_ERASE_SIG			0xFD0300
#define QLA8044_FLASH_LAST_ERASE_MS_VAL		0x3D

/* Reset template definitions */
#define QLA8044_MAX_RESET_SEQ_ENTRIES	16
#define QLA8044_RESTART_TEMPLATE_SIZE	0x2000
#define QLA8044_RESET_TEMPLATE_ADDR	0x4F0000
#define QLA8044_RESET_SEQ_VERSION	0x0101

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
#define QLA8044_IDC_DRV_CTRL            0x3790
#define AF_8044_NO_FW_DUMP              27 /* 0x08000000 */

#define MINIDUMP_SIZE_36K		36864

struct qla8044_reset_template_hdr {
	uint16_t	version;
	uint16_t	signature;
	uint16_t	size;
	uint16_t	entries;
	uint16_t	hdr_size;
	uint16_t	checksum;
	uint16_t	init_seq_offset;
	uint16_t	start_seq_offset;
} __packed;

/* Common Entry Header. */
struct qla8044_reset_entry_hdr {
	uint16_t cmd;
	uint16_t size;
	uint16_t count;
	uint16_t delay;
} __packed;

/* Generic poll entry type. */
struct qla8044_poll {
	uint32_t  test_mask;
	uint32_t  test_value;
} __packed;

/* Read modify write entry type. */
struct qla8044_rmw {
	uint32_t test_mask;
	uint32_t xor_value;
	uint32_t  or_value;
	uint8_t shl;
	uint8_t shr;
	uint8_t index_a;
	uint8_t rsvd;
} __packed;

/* Generic Entry Item with 2 DWords. */
struct qla8044_entry {
	uint32_t arg1;
	uint32_t arg2;
} __packed;

/* Generic Entry Item with 4 DWords.*/
struct qla8044_quad_entry {
	uint32_t dr_addr;
	uint32_t dr_value;
	uint32_t ar_addr;
	uint32_t ar_value;
} __packed;

struct qla8044_reset_template {
	int seq_index;
	int seq_error;
	int array_index;
	uint32_t array[QLA8044_MAX_RESET_SEQ_ENTRIES];
	uint8_t *buff;
	uint8_t *stop_offset;
	uint8_t *start_offset;
	uint8_t *init_offset;
	struct qla8044_reset_template_hdr *hdr;
	uint8_t seq_end;
	uint8_t template_end;
};

/* Driver_code is for driver to write some info about the entry
 * currently not used.
 */
struct qla8044_minidump_entry_hdr {
	uint32_t entry_type;
	uint32_t entry_size;
	uint32_t entry_capture_size;
	struct {
		uint8_t entry_capture_mask;
		uint8_t entry_code;
		uint8_t driver_code;
		uint8_t driver_flags;
	} d_ctrl;
} __packed;

/*  Read CRB entry header */
struct qla8044_minidump_entry_crb {
	struct qla8044_minidump_entry_hdr h;
	uint32_t addr;
	struct {
		uint8_t addr_stride;
		uint8_t state_index_a;
		uint16_t poll_timeout;
	} crb_strd;
	uint32_t data_size;
	uint32_t op_count;

	struct {
		uint8_t opcode;
		uint8_t state_index_v;
		uint8_t shl;
		uint8_t shr;
	} crb_ctrl;

	uint32_t value_1;
	uint32_t value_2;
	uint32_t value_3;
} __packed;

struct qla8044_minidump_entry_cache {
	struct qla8044_minidump_entry_hdr h;
	uint32_t tag_reg_addr;
	struct {
		uint16_t tag_value_stride;
		uint16_t init_tag_value;
	} addr_ctrl;
	uint32_t data_size;
	uint32_t op_count;
	uint32_t control_addr;
	struct {
		uint16_t write_value;
		uint8_t poll_mask;
		uint8_t poll_wait;
	} cache_ctrl;
	uint32_t read_addr;
	struct {
		uint8_t read_addr_stride;
		uint8_t read_addr_cnt;
		uint16_t rsvd_1;
	} read_ctrl;
} __packed;

/* Read OCM */
struct qla8044_minidump_entry_rdocm {
	struct qla8044_minidump_entry_hdr h;
	uint32_t rsvd_0;
	uint32_t rsvd_1;
	uint32_t data_size;
	uint32_t op_count;
	uint32_t rsvd_2;
	uint32_t rsvd_3;
	uint32_t read_addr;
	uint32_t read_addr_stride;
} __packed;

/* Read Memory */
struct qla8044_minidump_entry_rdmem {
	struct qla8044_minidump_entry_hdr h;
	uint32_t rsvd[6];
	uint32_t read_addr;
	uint32_t read_data_size;
};

/* Read Memory: For Pex-DMA */
struct qla8044_minidump_entry_rdmem_pex_dma {
	struct qla8044_minidump_entry_hdr h;
	uint32_t desc_card_addr;
	uint16_t dma_desc_cmd;
	uint8_t rsvd[2];
	uint32_t start_dma_cmd;
	uint8_t rsvd2[12];
	uint32_t read_addr;
	uint32_t read_data_size;
} __packed;

/* Read ROM */
struct qla8044_minidump_entry_rdrom {
	struct qla8044_minidump_entry_hdr h;
	uint32_t rsvd[6];
	uint32_t read_addr;
	uint32_t read_data_size;
} __packed;

/* Mux entry */
struct qla8044_minidump_entry_mux {
	struct qla8044_minidump_entry_hdr h;
	uint32_t select_addr;
	uint32_t rsvd_0;
	uint32_t data_size;
	uint32_t op_count;
	uint32_t select_value;
	uint32_t select_value_stride;
	uint32_t read_addr;
	uint32_t rsvd_1;
} __packed;

/* Queue entry */
struct qla8044_minidump_entry_queue {
	struct qla8044_minidump_entry_hdr h;
	uint32_t select_addr;
	struct {
		uint16_t queue_id_stride;
		uint16_t rsvd_0;
	} q_strd;
	uint32_t data_size;
	uint32_t op_count;
	uint32_t rsvd_1;
	uint32_t rsvd_2;
	uint32_t read_addr;
	struct {
		uint8_t read_addr_stride;
		uint8_t read_addr_cnt;
		uint16_t rsvd_3;
	} rd_strd;
} __packed;

/* POLLRD Entry */
struct qla8044_minidump_entry_pollrd {
	struct qla8044_minidump_entry_hdr h;
	uint32_t select_addr;
	uint32_t read_addr;
	uint32_t select_value;
	uint16_t select_value_stride;
	uint16_t op_count;
	uint32_t poll_wait;
	uint32_t poll_mask;
	uint32_t data_size;
	uint32_t rsvd_1;
} __packed;

struct qla8044_minidump_entry_rddfe {
	struct qla8044_minidump_entry_hdr h;
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
	struct qla8044_minidump_entry_hdr h;

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
	struct qla8044_minidump_entry_hdr h;
	uint32_t addr_1;
	uint32_t addr_2;
	uint32_t value_1;
	uint32_t value_2;
	uint32_t poll;
	uint32_t mask;
	uint32_t data_size;
	uint32_t rsvd;

}  __packed;

/* RDMUX2 Entry */
struct qla8044_minidump_entry_rdmux2 {
	struct qla8044_minidump_entry_hdr h;
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
} __packed;

/* POLLRDMWR Entry */
struct qla8044_minidump_entry_pollrdmwr {
	struct qla8044_minidump_entry_hdr h;
	uint32_t addr_1;
	uint32_t addr_2;
	uint32_t value_1;
	uint32_t value_2;
	uint32_t poll_wait;
	uint32_t poll_mask;
	uint32_t modify_mask;
	uint32_t data_size;
} __packed;

/* IDC additional information */
struct qla8044_idc_information {
	uint32_t request_desc;  /* IDC request descriptor */
	uint32_t info1; /* IDC additional info */
	uint32_t info2; /* IDC additional info */
	uint32_t info3; /* IDC additional info */
} __packed;

enum qla_regs {
	QLA8044_PEG_HALT_STATUS1_INDEX = 0,
	QLA8044_PEG_HALT_STATUS2_INDEX,
	QLA8044_PEG_ALIVE_COUNTER_INDEX,
	QLA8044_CRB_DRV_ACTIVE_INDEX,
	QLA8044_CRB_DEV_STATE_INDEX,
	QLA8044_CRB_DRV_STATE_INDEX,
	QLA8044_CRB_DRV_SCRATCH_INDEX,
	QLA8044_CRB_DEV_PART_INFO_INDEX,
	QLA8044_CRB_DRV_IDC_VERSION_INDEX,
	QLA8044_FW_VERSION_MAJOR_INDEX,
	QLA8044_FW_VERSION_MINOR_INDEX,
	QLA8044_FW_VERSION_SUB_INDEX,
	QLA8044_CRB_CMDPEG_STATE_INDEX,
	QLA8044_CRB_TEMP_STATE_INDEX,
} __packed;

#define CRB_REG_INDEX_MAX	14
#define CRB_CMDPEG_CHECK_RETRY_COUNT    60
#define CRB_CMDPEG_CHECK_DELAY          500

/* MiniDump Structures */

/* Driver_code is for driver to write some info about the entry
 * currently not used.
 */
#define QLA8044_SS_OCM_WNDREG_INDEX             3
#define QLA8044_DBG_STATE_ARRAY_LEN             16
#define QLA8044_DBG_CAP_SIZE_ARRAY_LEN          8
#define QLA8044_DBG_RSVD_ARRAY_LEN              8
#define QLA8044_DBG_OCM_WNDREG_ARRAY_LEN        16
#define QLA8044_SS_PCI_INDEX                    0
#define QLA8044_RDDFE          38
#define QLA8044_RDMDIO         39
#define QLA8044_POLLWR         40

struct qla8044_minidump_template_hdr {
	uint32_t entry_type;
	uint32_t first_entry_offset;
	uint32_t size_of_template;
	uint32_t capture_debug_level;
	uint32_t num_of_entries;
	uint32_t version;
	uint32_t driver_timestamp;
	uint32_t checksum;

	uint32_t driver_capture_mask;
	uint32_t driver_info_word2;
	uint32_t driver_info_word3;
	uint32_t driver_info_word4;

	uint32_t saved_state_array[QLA8044_DBG_STATE_ARRAY_LEN];
	uint32_t capture_size_array[QLA8044_DBG_CAP_SIZE_ARRAY_LEN];
	uint32_t ocm_window_reg[QLA8044_DBG_OCM_WNDREG_ARRAY_LEN];
};

struct qla8044_pex_dma_descriptor {
	struct {
		uint32_t read_data_size; /* 0-23: size, 24-31: rsvd */
		uint8_t rsvd[2];
		uint16_t dma_desc_cmd;
	} cmd;
	uint64_t src_addr;
	uint64_t dma_bus_addr; /*0-3: desc-cmd, 4-7: pci-func, 8-15: desc-cmd*/
	uint8_t rsvd[24];
} __packed;

#endif
