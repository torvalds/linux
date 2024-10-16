/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2007 Google, Inc.
 * Copyright (c) 2012-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __QPIC_NAND_H
#define __QPIC_NAND_H

#define pr_fmt(fmt) "%s: " fmt, __func__

#include <linux/clk.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/rawnand.h>
#include <linux/mtd/partitions.h>
#include <linux/panic_notifier.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/crc16.h>
#include <linux/bitrev.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/ctype.h>
#include <linux/msm-sps.h>
#include <linux/soc/qcom/smem.h>
#include <linux/interconnect.h>
#include <linux/suspend.h>
#include <linux/nvmem-consumer.h>

#define PAGE_SIZE_2K 2048
#define PAGE_SIZE_4K 4096

#undef WRITE /* To avoid redefinition in above header files */
#undef READ /* To avoid redefinition in above header files */
#define WRITE 1
#define READ 0

#define MSM_NAND_IDLE_TIMEOUT   200 /* msecs */
#define MSM_NAND_BUS_VOTE_MAX_RATE  100000000 /* Hz */

/*
 * The maximum no of descriptors per transfer (page read/write) won't be more
 * than 64. For more details on what those commands are, please refer to the
 * page read and page write functions in the driver.
 */
#define SPS_MAX_DESC_NUM 64
#define SPS_DATA_CONS_PIPE_INDEX 0
#define SPS_DATA_PROD_PIPE_INDEX 1
#define SPS_CMD_CONS_PIPE_INDEX 2
#define SPS_DATA_PROD_STAT_PIPE_INDEX 3

#define msm_virt_to_dma(chip, vaddr) \
	((chip)->dma_phys_addr + \
	((uint8_t *)(vaddr) - (chip)->dma_virt_addr))

/*
 * A single page read/write request would typically need DMA memory of about
 * 1K memory approximately. So for a single request this memory is more than
 * enough.
 *
 * But to accommodate multiple clients we allocate 8K of memory. Though only
 * one client request can be submitted to NANDc at any time, other clients can
 * still prepare the descriptors while waiting for current client request to
 * be done. Thus for a total memory of 8K, the driver can currently support
 * maximum clients up to 7 or 8 at a time. The client for which there is no
 * free DMA memory shall wait on the wait queue until other clients free up
 * the required memory.
 */
#define MSM_NAND_DMA_BUFFER_SIZE SZ_8K
/*
 * This defines the granularity at which the buffer management is done. The
 * total number of slots is based on the size of the atomic_t variable
 * dma_buffer_busy(number of bits) within the structure msm_nand_chip.
 */
#define MSM_NAND_DMA_BUFFER_SLOT_SZ \
	(MSM_NAND_DMA_BUFFER_SIZE / (sizeof(((atomic_t *)0)->counter) * 8))

/* ONFI(Open NAND Flash Interface) parameters */
#define MSM_NAND_CFG0_RAW_ONFI_IDENTIFIER 0x88000800
#define MSM_NAND_CFG0_RAW_ONFI_PARAM_INFO 0x88040000
#define MSM_NAND_CFG1_RAW_ONFI_IDENTIFIER 0x0005045d
#define MSM_NAND_CFG1_RAW_ONFI_PARAM_INFO 0x0005045d
#define ONFI_PARAM_INFO_LENGTH 0x0200
#define ONFI_PARAM_PAGE_LENGTH 0x0100
#define ONFI_PARAMETER_PAGE_SIGNATURE 0x49464E4F
#define FLASH_READ_ONFI_SIGNATURE_ADDRESS 0x20
#define FLASH_READ_ONFI_PARAMETERS_ADDRESS 0x00
#define FLASH_READ_DEVICE_ID_ADDRESS 0x00

#define MSM_NAND_RESET_FLASH_STS 0x00000020
#define MSM_NAND_RESET_READ_STS 0x000000C0

/* QPIC NANDc (NAND Controller) Register Set */
#define MSM_NAND_REG(info, off)		    (off)
#define MSM_NAND_QPIC_VERSION(info)	    MSM_NAND_REG(info, 0x24100)
#define MSM_NAND_FLASH_CMD(info)	    MSM_NAND_REG(info, 0x30000)
#define MSM_NAND_ADDR0(info)                MSM_NAND_REG(info, 0x30004)
#define MSM_NAND_ADDR1(info)                MSM_NAND_REG(info, 0x30008)
#define MSM_NAND_EXEC_CMD(info)             MSM_NAND_REG(info, 0x30010)
#define MSM_NAND_FLASH_STATUS(info)         MSM_NAND_REG(info, 0x30014)
#define FS_OP_ERR (1 << 4)
#define FS_MPU_ERR (1 << 8)
#define FS_DEVICE_STS_ERR (1 << 16)
#define FS_DEVICE_WP (1 << 23)

#define MSM_NAND_BUFFER_STATUS(info)        MSM_NAND_REG(info, 0x30018)
#define BS_UNCORRECTABLE_BIT (1 << 8)
#define BS_CORRECTABLE_ERR_MSK 0x1F

#define MSM_NAND_DEV0_CFG0(info)            MSM_NAND_REG(info, 0x30020)
#define DISABLE_STATUS_AFTER_WRITE 4
#define CW_PER_PAGE	6
#define UD_SIZE_BYTES	9
#define SPARE_SIZE_BYTES 23
#define NUM_ADDR_CYCLES	27

#define MSM_NAND_DEV0_CFG1(info)            MSM_NAND_REG(info, 0x30024)
#define DEV0_CFG1_ECC_DISABLE	0
#define WIDE_FLASH		1
#define NAND_RECOVERY_CYCLES	2
#define CS_ACTIVE_BSY		5
#define BAD_BLOCK_BYTE_NUM	6
#define BAD_BLOCK_IN_SPARE_AREA 16
#define WR_RD_BSY_GAP		17
#define ENABLE_BCH_ECC		27

#define BYTES_512		512
#define BYTES_516		516
#define BYTES_517		517

#define MSM_NAND_DEV0_ECC_CFG(info)	    MSM_NAND_REG(info, 0x30028)
#define ECC_CFG_ECC_DISABLE	0
#define ECC_SW_RESET	1
#define ECC_MODE	4
#define ECC_PARITY_SIZE_BYTES 8
#define ECC_NUM_DATA_BYTES 16
#define ECC_FORCE_CLK_OPEN 30

#define MSM_NAND_READ_ID(info)              MSM_NAND_REG(info, 0x30040)
#define MSM_NAND_READ_STATUS(info)          MSM_NAND_REG(info, 0x30044)
#define MSM_NAND_READ_ID2(info)              MSM_NAND_REG(info, 0x30048)
#define EXTENDED_FETCH_ID           BIT(19)
#define MSM_NAND_DEV_CMD1(info)             MSM_NAND_REG(info, 0x300A4)
#define MSM_NAND_DEV_CMD_VLD(info)          MSM_NAND_REG(info, 0x300AC)
#define MSM_NAND_EBI2_ECC_BUF_CFG(info)     MSM_NAND_REG(info, 0x300F0)

#define MSM_NAND_ERASED_CW_DETECT_CFG(info)	MSM_NAND_REG(info, 0x300E8)
#define ERASED_CW_ECC_MASK	1
#define AUTO_DETECT_RES		0
#define MASK_ECC		(1 << ERASED_CW_ECC_MASK)
#define RESET_ERASED_DET	(1 << AUTO_DETECT_RES)
#define ACTIVE_ERASED_DET	(0 << AUTO_DETECT_RES)
#define CLR_ERASED_PAGE_DET	(RESET_ERASED_DET | MASK_ECC)
#define SET_ERASED_PAGE_DET	(ACTIVE_ERASED_DET | MASK_ECC)

#define MSM_NAND_ERASED_CW_DETECT_STATUS(info)  MSM_NAND_REG(info, 0x300EC)
#define PAGE_ALL_ERASED		7
#define CODEWORD_ALL_ERASED	6
#define PAGE_ERASED		5
#define CODEWORD_ERASED		4
#define ERASED_PAGE	((1 << PAGE_ALL_ERASED) | (1 << PAGE_ERASED))
#define ERASED_CW	((1 << CODEWORD_ALL_ERASED) | (1 << CODEWORD_ERASED))

#define MSM_NAND_CTRL(info)		    MSM_NAND_REG(info, 0x30F00)
#define BAM_MODE_EN	0
#define BAM_MODE_EN_MASK	0x1
#define BOOST_MODE_EN	0xB
#define MSM_NAND_VERSION(info)         MSM_NAND_REG(info, 0x34F08)
#define MSM_NAND_READ_LOCATION_0(info)      MSM_NAND_REG(info, 0x30F20)
#define MSM_NAND_READ_LOCATION_1(info)      MSM_NAND_REG(info, 0x30F24)
#define MSM_NAND_READ_LOCATION_LAST_CW_0(info) MSM_NAND_REG(info, 0x30F40)
#define MSM_NAND_READ_LOCATION_LAST_CW_1(info) MSM_NAND_REG(info, 0x30F44)
#define MSM_NAND_AUTO_STATUS_EN(info)       MSM_NAND_REG(info, 0x3002c)
#define MSM_NAND_MULTI_PAGE_CMD(info)       MSM_NAND_REG(info, 0x30F60)

#define NAND_FLASH_STATUS_EN                     BIT(0)
#define NANDC_BUFFER_STATUS_EN                   BIT(1)
#define NAND_ERASED_CW_DETECT_STATUS_EN          BIT(3)
#define NAND_FLASH_STATUS_LAST_CW_EN             BIT(16)
#define NANDC_BUFFER_STATUS_LAST_CW_EN           BIT(17)
#define NAND_ERASED_CW_DETECT_STATUS_LAST_CW_EN  BIT(19)

/* device commands */
#define MSM_NAND_CMD_PAGE_READ          0x32
#define MSM_NAND_CMD_PAGE_READ_ECC      0x33
#define MSM_NAND_CMD_PAGE_READ_ALL      0x34
#define MSM_NAND_CMD_PAGE_READ_ONFI     0x35
#define MSM_NAND_CMD_PRG_PAGE           0x36
#define MSM_NAND_CMD_PRG_PAGE_ECC       0x37
#define MSM_NAND_CMD_PRG_PAGE_ALL       0x39
#define MSM_NAND_CMD_BLOCK_ERASE        0x3A
#define MSM_NAND_CMD_FETCH_ID           0x0B

/* device read commands for pagescope */

#define MSM_NAND_CMD_PAGE_READ_ECC_PS   0x800033
#define MSM_NAND_CMD_PAGE_READ_ALL_PS   0x800034

/* device read commands for multipage */

#define MSM_NAND_CMD_PAGE_READ_ECC_MP   0x400033
#define MSM_NAND_CMD_PAGE_READ_ALL_MP   0x400034

#define MAX_MULTI_PAGE_READS   8

/* Version Mask */
#define MSM_NAND_VERSION_MAJOR_MASK	0xF0000000
#define MSM_NAND_VERSION_MAJOR_SHIFT	28
#define MSM_NAND_VERSION_MINOR_MASK	0x0FFF0000
#define MSM_NAND_VERSION_MINOR_SHIFT	16

#define CMD		SPS_IOVEC_FLAG_CMD
#define CMD_LCK		(CMD | SPS_IOVEC_FLAG_LOCK)
#define INT		SPS_IOVEC_FLAG_INT
#define INT_UNLCK	(INT | SPS_IOVEC_FLAG_UNLOCK)
#define CMD_INT_UNLCK	(CMD | INT_UNLCK)
#define NWD		SPS_IOVEC_FLAG_NWD

/* Structure that defines a NAND SPS command element */
struct msm_nand_sps_cmd {
	struct sps_command_element ce;
	uint32_t flags;
};

struct msm_nand_cmd_setup_desc {
	struct sps_command_element ce[14];
	uint32_t flags;
	uint32_t num_ce;
};

struct msm_nand_cmd_cw_desc {
	struct sps_command_element ce[5];
	uint32_t flags;
	uint32_t num_ce;
};

struct msm_nand_rw_cmd_desc {
	uint32_t count;
	struct msm_nand_cmd_setup_desc setup_desc;
	struct msm_nand_cmd_cw_desc cw_desc[];
};

/*
 * Structure that holds the flash, buffer,
 * erased codeword status after every codeword
 * read during Pagescope read operation.
 */
struct msm_nand_read_status_desc {
	uint32_t flash_status;
	uint32_t buffer_status;
	uint32_t erased_cw_status;
};

/*
 * Structure that defines the NAND controller properties as per the
 * NAND flash device/chip that is attached.
 */
struct msm_nand_chip {
	struct device *dev;
	/*
	 * DMA memory will be allocated only once during probe and this memory
	 * will be used by all NAND clients. This wait queue is needed to
	 * make the applications wait for DMA memory to be free'd when the
	 * complete memory is exhausted.
	 */
	wait_queue_head_t dma_wait_queue;
	atomic_t dma_buffer_busy;
	uint8_t *dma_virt_addr;
	dma_addr_t dma_phys_addr;
	uint32_t ecc_parity_bytes;
	uint32_t bch_caps; /* Controller BCH ECC capabilities */
#define MSM_NAND_CAP_4_BIT_BCH      (1 << 0)
#define MSM_NAND_CAP_8_BIT_BCH      (1 << 1)
	uint32_t cw_size;
	/* NANDc register configurations */
	uint32_t cfg0, cfg1, cfg0_raw, cfg1_raw;
	uint32_t ecc_buf_cfg;
	uint32_t ecc_bch_cfg;
	uint32_t ecc_cfg_raw;
	uint32_t qpic_version; /* To store the qpic controller major version */
	uint16_t qpic_min_version; /* To store the qpic controller minor version */
	uint32_t caps; /* General host capabilities */
#define MSM_NAND_CAP_PAGE_SCOPE_READ	BIT(0)
#define MSM_NAND_CAP_MULTI_PAGE_READ	BIT(1)
#define MSM_NAND_CAP_BOOST_MODE		BIT(2)
#define MSM_NAND_INTERRUPT_MODE_ENABLE  BIT(3) /* To enable nand in Interrupt mode */
};

/* Structure that defines an SPS end point for a NANDc BAM pipe. */
struct msm_nand_sps_endpt {
	struct sps_pipe *handle;
	struct sps_connect config;
	struct sps_register_event event;
	struct completion completion;
	uint32_t index;
};

/*
 * Structure that defines NANDc SPS data - BAM handle and an end point
 * for each BAM pipe.
 */
struct msm_nand_sps_info {
	unsigned long bam_handle;
	struct msm_nand_sps_endpt data_prod;
	struct msm_nand_sps_endpt data_cons;
	struct msm_nand_sps_endpt cmd_pipe;
	struct msm_nand_sps_endpt data_prod_stat;
};

/*
 * Structure that contains flash device information. This gets updated after
 * the NAND flash device detection.
 */
struct flash_identification {
	uint32_t flash_id;
	uint64_t density;
	uint32_t widebus;
	uint32_t pagesize;
	uint32_t blksize;
	uint32_t oobsize;
	uint32_t ecc_correctability;
	uint32_t ecc_capability; /* Set based on the ECC capability selected. */
	uint16_t timing_mode_support; /* Timing mode */
	/* Flag to distinguish b/w ONFI and NON-ONFI properties */
	bool is_onfi_compliant;
};

struct msm_bus_vectors {
	u64 ab;
	u64 ib;
};

struct msm_bus_path {
	unsigned int num_paths;
	struct msm_bus_vectors *vec;
};

struct msm_nand_bus_vote_data {
	const char *name;
	unsigned int num_usecase;
	struct msm_bus_path *usecase;

	struct icc_path *nand_ddr;

	u32 curr_vote;
};

struct msm_nand_clk_data {
	struct clk *qpic_clk;
	struct msm_nand_bus_vote_data *bus_vote_data;
	atomic_t clk_enabled;
	atomic_t curr_vote;
	bool rpmh_clk;
};

/* Structure that defines NANDc private data. */
struct msm_nand_info {
	struct mtd_info		mtd;
	struct msm_nand_chip	nand_chip;
	struct msm_nand_sps_info sps;
	unsigned long bam_phys;
	unsigned long nand_phys;
	unsigned long nand_phys_adjusted;
	void __iomem *bam_base;
	int bam_irq;
	/*
	 * This lock must be acquired before submitting any command or data
	 * descriptors to BAM pipes and must be held until all the submitted
	 * descriptors are processed.
	 *
	 * This is required to ensure that both command and descriptors are
	 * submitted atomically without interruption from other clients,
	 * when there are requests from more than client at any time.
	 * Othewise, data and command descriptors can be submitted out of
	 * order for a request which can cause data corruption.
	 */
	struct mutex lock;
	struct flash_identification flash_dev;
	struct msm_nand_clk_data clk_data;
	u64 dma_mask;
	u32 bam_irq_type; /*Edge trigger or Level trigger */
	unsigned long panic_notifier_dump; /* set upon nand panic notifier call */
};

extern struct nand_flash_dev nand_flash_ids[];

const struct nand_manufacturer_desc *nand_get_manufacturer_desc(u8 id);

/* Structure that defines an ONFI parameter page (512B) */
struct onfi_param_page {
	uint32_t parameter_page_signature;
	uint16_t revision_number;
	uint16_t features_supported;
	uint16_t optional_commands_supported;
	uint8_t  reserved0[22];
	uint8_t  device_manufacturer[12];
	uint8_t  device_model[20];
	uint8_t  jedec_manufacturer_id;
	uint16_t date_code;
	uint8_t  reserved1[13];
	uint32_t number_of_data_bytes_per_page;
	uint16_t number_of_spare_bytes_per_page;
	uint32_t number_of_data_bytes_per_partial_page;
	uint16_t number_of_spare_bytes_per_partial_page;
	uint32_t number_of_pages_per_block;
	uint32_t number_of_blocks_per_logical_unit;
	uint8_t  number_of_logical_units;
	uint8_t  number_of_address_cycles;
	uint8_t  number_of_bits_per_cell;
	uint16_t maximum_bad_blocks_per_logical_unit;
	uint16_t block_endurance;
	uint8_t  guaranteed_valid_begin_blocks;
	uint16_t guaranteed_valid_begin_blocks_endurance;
	uint8_t  number_of_programs_per_page;
	uint8_t  partial_program_attributes;
	uint8_t  number_of_bits_ecc_correctability;
	uint8_t  number_of_interleaved_address_bits;
	uint8_t  interleaved_operation_attributes;
	uint8_t  reserved2[13];
	uint8_t  io_pin_capacitance;
	uint16_t timing_mode_support;
	uint16_t program_cache_timing_mode_support;
	uint16_t maximum_page_programming_time;
	uint16_t maximum_block_erase_time;
	uint16_t maximum_page_read_time;
	uint16_t maximum_change_column_setup_time;
	uint8_t  reserved3[23];
	uint16_t vendor_specific_revision_number;
	uint8_t  vendor_specific[88];
	uint16_t integrity_crc;
} __packed;

#define FLASH_PART_MAGIC1	0x55EE73AA
#define FLASH_PART_MAGIC2	0xE35EBDDB
#define FLASH_PTABLE_V3		3
#define FLASH_PTABLE_V4		4
#define FLASH_PTABLE_MAX_PARTS_V3 16
#define FLASH_PTABLE_MAX_PARTS_V4 80
#define FLASH_PTABLE_HDR_LEN (4*sizeof(uint32_t))
#define FLASH_PTABLE_ENTRY_NAME_SIZE 16

struct flash_partition_entry {
	char name[FLASH_PTABLE_ENTRY_NAME_SIZE];
	u32 offset;     /* Offset in blocks from beginning of device */
	u32 length;     /* Length of the partition in blocks */
	u8 attr;	/* Flags for this partition */
};

struct flash_partition_table {
	u32 magic1;
	u32 magic2;
	u32 version;
	u32 numparts;
	struct flash_partition_entry part_entry[FLASH_PTABLE_MAX_PARTS_V4];
};

static struct flash_partition_table ptable;

static struct mtd_partition mtd_part[FLASH_PTABLE_MAX_PARTS_V4];

static inline bool is_buffer_in_page(const void *buf, size_t len)
{
	return !(((unsigned long) buf & ~PAGE_MASK) + len > PAGE_SIZE);
}

static void msm_nand_bam_free(struct msm_nand_info *nand_info);
static int msm_nand_bam_init(struct msm_nand_info *nand_info);
static int msm_nand_enable_dma(struct msm_nand_info *info);
static int msm_nand_init_status_pipe(struct msm_nand_info *info);
static int msm_nand_get_device(struct device *dev);
static int msm_nand_put_device(struct device *dev);
static int msm_nand_flash_rd_rw_reg(struct msm_nand_info *info,
		uint32_t addr, uint32_t *val, uint32_t command);
static int msm_nand_boost_mode_enable(struct msm_nand_info *info);

#endif /* __QPIC_NAND_H */
