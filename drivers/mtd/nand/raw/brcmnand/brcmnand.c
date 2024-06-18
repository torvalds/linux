// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright © 2010-2015 Broadcom Corporation
 */

#include <linux/clk.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/platform_data/brcmnand.h>
#include <linux/err.h>
#include <linux/completion.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/dma-mapping.h>
#include <linux/ioport.h>
#include <linux/bug.h>
#include <linux/kernel.h>
#include <linux/bitops.h>
#include <linux/mm.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/rawnand.h>
#include <linux/mtd/partitions.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/slab.h>
#include <linux/static_key.h>
#include <linux/list.h>
#include <linux/log2.h>

#include "brcmnand.h"

/*
 * This flag controls if WP stays on between erase/write commands to mitigate
 * flash corruption due to power glitches. Values:
 * 0: NAND_WP is not used or not available
 * 1: NAND_WP is set by default, cleared for erase/write operations
 * 2: NAND_WP is always cleared
 */
static int wp_on = 1;
module_param(wp_on, int, 0444);

/***********************************************************************
 * Definitions
 ***********************************************************************/

#define DRV_NAME			"brcmnand"

#define CMD_NULL			0x00
#define CMD_PAGE_READ			0x01
#define CMD_SPARE_AREA_READ		0x02
#define CMD_STATUS_READ			0x03
#define CMD_PROGRAM_PAGE		0x04
#define CMD_PROGRAM_SPARE_AREA		0x05
#define CMD_COPY_BACK			0x06
#define CMD_DEVICE_ID_READ		0x07
#define CMD_BLOCK_ERASE			0x08
#define CMD_FLASH_RESET			0x09
#define CMD_BLOCKS_LOCK			0x0a
#define CMD_BLOCKS_LOCK_DOWN		0x0b
#define CMD_BLOCKS_UNLOCK		0x0c
#define CMD_READ_BLOCKS_LOCK_STATUS	0x0d
#define CMD_PARAMETER_READ		0x0e
#define CMD_PARAMETER_CHANGE_COL	0x0f
#define CMD_LOW_LEVEL_OP		0x10

struct brcm_nand_dma_desc {
	u32 next_desc;
	u32 next_desc_ext;
	u32 cmd_irq;
	u32 dram_addr;
	u32 dram_addr_ext;
	u32 tfr_len;
	u32 total_len;
	u32 flash_addr;
	u32 flash_addr_ext;
	u32 cs;
	u32 pad2[5];
	u32 status_valid;
} __packed;

/* Bitfields for brcm_nand_dma_desc::status_valid */
#define FLASH_DMA_ECC_ERROR	(1 << 8)
#define FLASH_DMA_CORR_ERROR	(1 << 9)

/* Bitfields for DMA_MODE */
#define FLASH_DMA_MODE_STOP_ON_ERROR	BIT(1) /* stop in Uncorr ECC error */
#define FLASH_DMA_MODE_MODE		BIT(0) /* link list */
#define FLASH_DMA_MODE_MASK		(FLASH_DMA_MODE_STOP_ON_ERROR |	\
						FLASH_DMA_MODE_MODE)

/* 512B flash cache in the NAND controller HW */
#define FC_SHIFT		9U
#define FC_BYTES		512U
#define FC_WORDS		(FC_BYTES >> 2)

#define BRCMNAND_MIN_PAGESIZE	512
#define BRCMNAND_MIN_BLOCKSIZE	(8 * 1024)
#define BRCMNAND_MIN_DEVSIZE	(4ULL * 1024 * 1024)

#define NAND_CTRL_RDY			(INTFC_CTLR_READY | INTFC_FLASH_READY)
#define NAND_POLL_STATUS_TIMEOUT_MS	100

#define EDU_CMD_WRITE          0x00
#define EDU_CMD_READ           0x01
#define EDU_STATUS_ACTIVE      BIT(0)
#define EDU_ERR_STATUS_ERRACK  BIT(0)
#define EDU_DONE_MASK		GENMASK(1, 0)

#define EDU_CONFIG_MODE_NAND   BIT(0)
#define EDU_CONFIG_SWAP_BYTE   BIT(1)
#ifdef CONFIG_CPU_BIG_ENDIAN
#define EDU_CONFIG_SWAP_CFG     EDU_CONFIG_SWAP_BYTE
#else
#define EDU_CONFIG_SWAP_CFG     0
#endif

/* edu registers */
enum edu_reg {
	EDU_CONFIG = 0,
	EDU_DRAM_ADDR,
	EDU_EXT_ADDR,
	EDU_LENGTH,
	EDU_CMD,
	EDU_STOP,
	EDU_STATUS,
	EDU_DONE,
	EDU_ERR_STATUS,
};

static const u16  edu_regs[] = {
	[EDU_CONFIG] = 0x00,
	[EDU_DRAM_ADDR] = 0x04,
	[EDU_EXT_ADDR] = 0x08,
	[EDU_LENGTH] = 0x0c,
	[EDU_CMD] = 0x10,
	[EDU_STOP] = 0x14,
	[EDU_STATUS] = 0x18,
	[EDU_DONE] = 0x1c,
	[EDU_ERR_STATUS] = 0x20,
};

/* flash_dma registers */
enum flash_dma_reg {
	FLASH_DMA_REVISION = 0,
	FLASH_DMA_FIRST_DESC,
	FLASH_DMA_FIRST_DESC_EXT,
	FLASH_DMA_CTRL,
	FLASH_DMA_MODE,
	FLASH_DMA_STATUS,
	FLASH_DMA_INTERRUPT_DESC,
	FLASH_DMA_INTERRUPT_DESC_EXT,
	FLASH_DMA_ERROR_STATUS,
	FLASH_DMA_CURRENT_DESC,
	FLASH_DMA_CURRENT_DESC_EXT,
};

/* flash_dma registers v0*/
static const u16 flash_dma_regs_v0[] = {
	[FLASH_DMA_REVISION]		= 0x00,
	[FLASH_DMA_FIRST_DESC]		= 0x04,
	[FLASH_DMA_CTRL]		= 0x08,
	[FLASH_DMA_MODE]		= 0x0c,
	[FLASH_DMA_STATUS]		= 0x10,
	[FLASH_DMA_INTERRUPT_DESC]	= 0x14,
	[FLASH_DMA_ERROR_STATUS]	= 0x18,
	[FLASH_DMA_CURRENT_DESC]	= 0x1c,
};

/* flash_dma registers v1*/
static const u16 flash_dma_regs_v1[] = {
	[FLASH_DMA_REVISION]		= 0x00,
	[FLASH_DMA_FIRST_DESC]		= 0x04,
	[FLASH_DMA_FIRST_DESC_EXT]	= 0x08,
	[FLASH_DMA_CTRL]		= 0x0c,
	[FLASH_DMA_MODE]		= 0x10,
	[FLASH_DMA_STATUS]		= 0x14,
	[FLASH_DMA_INTERRUPT_DESC]	= 0x18,
	[FLASH_DMA_INTERRUPT_DESC_EXT]	= 0x1c,
	[FLASH_DMA_ERROR_STATUS]	= 0x20,
	[FLASH_DMA_CURRENT_DESC]	= 0x24,
	[FLASH_DMA_CURRENT_DESC_EXT]	= 0x28,
};

/* flash_dma registers v4 */
static const u16 flash_dma_regs_v4[] = {
	[FLASH_DMA_REVISION]		= 0x00,
	[FLASH_DMA_FIRST_DESC]		= 0x08,
	[FLASH_DMA_FIRST_DESC_EXT]	= 0x0c,
	[FLASH_DMA_CTRL]		= 0x10,
	[FLASH_DMA_MODE]		= 0x14,
	[FLASH_DMA_STATUS]		= 0x18,
	[FLASH_DMA_INTERRUPT_DESC]	= 0x20,
	[FLASH_DMA_INTERRUPT_DESC_EXT]	= 0x24,
	[FLASH_DMA_ERROR_STATUS]	= 0x28,
	[FLASH_DMA_CURRENT_DESC]	= 0x30,
	[FLASH_DMA_CURRENT_DESC_EXT]	= 0x34,
};

/* Controller feature flags */
enum {
	BRCMNAND_HAS_1K_SECTORS			= BIT(0),
	BRCMNAND_HAS_PREFETCH			= BIT(1),
	BRCMNAND_HAS_CACHE_MODE			= BIT(2),
	BRCMNAND_HAS_WP				= BIT(3),
};

struct brcmnand_host;

static DEFINE_STATIC_KEY_FALSE(brcmnand_soc_has_ops_key);

struct brcmnand_controller {
	struct device		*dev;
	struct nand_controller	controller;
	void __iomem		*nand_base;
	void __iomem		*nand_fc; /* flash cache */
	void __iomem		*flash_dma_base;
	int			irq;
	unsigned int		dma_irq;
	int			nand_version;

	/* Some SoCs provide custom interrupt status register(s) */
	struct brcmnand_soc	*soc;

	/* Some SoCs have a gateable clock for the controller */
	struct clk		*clk;

	int			cmd_pending;
	bool			dma_pending;
	bool                    edu_pending;
	struct completion	done;
	struct completion	dma_done;
	struct completion       edu_done;

	/* List of NAND hosts (one for each chip-select) */
	struct list_head host_list;

	/* EDU info, per-transaction */
	const u16               *edu_offsets;
	void __iomem            *edu_base;
	int			edu_irq;
	int                     edu_count;
	u64                     edu_dram_addr;
	u32                     edu_ext_addr;
	u32                     edu_cmd;
	u32                     edu_config;
	int			sas; /* spare area size, per flash cache */
	int			sector_size_1k;
	u8			*oob;

	/* flash_dma reg */
	const u16		*flash_dma_offsets;
	struct brcm_nand_dma_desc *dma_desc;
	dma_addr_t		dma_pa;

	int (*dma_trans)(struct brcmnand_host *host, u64 addr, u32 *buf,
			 u8 *oob, u32 len, u8 dma_cmd);

	/* in-memory cache of the FLASH_CACHE, used only for some commands */
	u8			flash_cache[FC_BYTES];

	/* Controller revision details */
	const u16		*reg_offsets;
	unsigned int		reg_spacing; /* between CS1, CS2, ... regs */
	const u8		*cs_offsets; /* within each chip-select */
	const u8		*cs0_offsets; /* within CS0, if different */
	unsigned int		max_block_size;
	const unsigned int	*block_sizes;
	unsigned int		max_page_size;
	const unsigned int	*page_sizes;
	unsigned int		page_size_shift;
	unsigned int		max_oob;
	u32			ecc_level_shift;
	u32			features;

	/* for low-power standby/resume only */
	u32			nand_cs_nand_select;
	u32			nand_cs_nand_xor;
	u32			corr_stat_threshold;
	u32			flash_dma_mode;
	u32                     flash_edu_mode;
	bool			pio_poll_mode;
};

struct brcmnand_cfg {
	u64			device_size;
	unsigned int		block_size;
	unsigned int		page_size;
	unsigned int		spare_area_size;
	unsigned int		device_width;
	unsigned int		col_adr_bytes;
	unsigned int		blk_adr_bytes;
	unsigned int		ful_adr_bytes;
	unsigned int		sector_size_1k;
	unsigned int		ecc_level;
	/* use for low-power standby/resume only */
	u32			acc_control;
	u32			config;
	u32			config_ext;
	u32			timing_1;
	u32			timing_2;
};

struct brcmnand_host {
	struct list_head	node;

	struct nand_chip	chip;
	struct platform_device	*pdev;
	int			cs;

	unsigned int		last_cmd;
	unsigned int		last_byte;
	u64			last_addr;
	struct brcmnand_cfg	hwcfg;
	struct brcmnand_controller *ctrl;
};

enum brcmnand_reg {
	BRCMNAND_CMD_START = 0,
	BRCMNAND_CMD_EXT_ADDRESS,
	BRCMNAND_CMD_ADDRESS,
	BRCMNAND_INTFC_STATUS,
	BRCMNAND_CS_SELECT,
	BRCMNAND_CS_XOR,
	BRCMNAND_LL_OP,
	BRCMNAND_CS0_BASE,
	BRCMNAND_CS1_BASE,		/* CS1 regs, if non-contiguous */
	BRCMNAND_CORR_THRESHOLD,
	BRCMNAND_CORR_THRESHOLD_EXT,
	BRCMNAND_UNCORR_COUNT,
	BRCMNAND_CORR_COUNT,
	BRCMNAND_CORR_EXT_ADDR,
	BRCMNAND_CORR_ADDR,
	BRCMNAND_UNCORR_EXT_ADDR,
	BRCMNAND_UNCORR_ADDR,
	BRCMNAND_SEMAPHORE,
	BRCMNAND_ID,
	BRCMNAND_ID_EXT,
	BRCMNAND_LL_RDATA,
	BRCMNAND_OOB_READ_BASE,
	BRCMNAND_OOB_READ_10_BASE,	/* offset 0x10, if non-contiguous */
	BRCMNAND_OOB_WRITE_BASE,
	BRCMNAND_OOB_WRITE_10_BASE,	/* offset 0x10, if non-contiguous */
	BRCMNAND_FC_BASE,
};

/* BRCMNAND v2.1-v2.2 */
static const u16 brcmnand_regs_v21[] = {
	[BRCMNAND_CMD_START]		=  0x04,
	[BRCMNAND_CMD_EXT_ADDRESS]	=  0x08,
	[BRCMNAND_CMD_ADDRESS]		=  0x0c,
	[BRCMNAND_INTFC_STATUS]		=  0x5c,
	[BRCMNAND_CS_SELECT]		=  0x14,
	[BRCMNAND_CS_XOR]		=  0x18,
	[BRCMNAND_LL_OP]		=     0,
	[BRCMNAND_CS0_BASE]		=  0x40,
	[BRCMNAND_CS1_BASE]		=     0,
	[BRCMNAND_CORR_THRESHOLD]	=     0,
	[BRCMNAND_CORR_THRESHOLD_EXT]	=     0,
	[BRCMNAND_UNCORR_COUNT]		=     0,
	[BRCMNAND_CORR_COUNT]		=     0,
	[BRCMNAND_CORR_EXT_ADDR]	=  0x60,
	[BRCMNAND_CORR_ADDR]		=  0x64,
	[BRCMNAND_UNCORR_EXT_ADDR]	=  0x68,
	[BRCMNAND_UNCORR_ADDR]		=  0x6c,
	[BRCMNAND_SEMAPHORE]		=  0x50,
	[BRCMNAND_ID]			=  0x54,
	[BRCMNAND_ID_EXT]		=     0,
	[BRCMNAND_LL_RDATA]		=     0,
	[BRCMNAND_OOB_READ_BASE]	=  0x20,
	[BRCMNAND_OOB_READ_10_BASE]	=     0,
	[BRCMNAND_OOB_WRITE_BASE]	=  0x30,
	[BRCMNAND_OOB_WRITE_10_BASE]	=     0,
	[BRCMNAND_FC_BASE]		= 0x200,
};

/* BRCMNAND v3.3-v4.0 */
static const u16 brcmnand_regs_v33[] = {
	[BRCMNAND_CMD_START]		=  0x04,
	[BRCMNAND_CMD_EXT_ADDRESS]	=  0x08,
	[BRCMNAND_CMD_ADDRESS]		=  0x0c,
	[BRCMNAND_INTFC_STATUS]		=  0x6c,
	[BRCMNAND_CS_SELECT]		=  0x14,
	[BRCMNAND_CS_XOR]		=  0x18,
	[BRCMNAND_LL_OP]		= 0x178,
	[BRCMNAND_CS0_BASE]		=  0x40,
	[BRCMNAND_CS1_BASE]		=  0xd0,
	[BRCMNAND_CORR_THRESHOLD]	=  0x84,
	[BRCMNAND_CORR_THRESHOLD_EXT]	=     0,
	[BRCMNAND_UNCORR_COUNT]		=     0,
	[BRCMNAND_CORR_COUNT]		=     0,
	[BRCMNAND_CORR_EXT_ADDR]	=  0x70,
	[BRCMNAND_CORR_ADDR]		=  0x74,
	[BRCMNAND_UNCORR_EXT_ADDR]	=  0x78,
	[BRCMNAND_UNCORR_ADDR]		=  0x7c,
	[BRCMNAND_SEMAPHORE]		=  0x58,
	[BRCMNAND_ID]			=  0x60,
	[BRCMNAND_ID_EXT]		=  0x64,
	[BRCMNAND_LL_RDATA]		= 0x17c,
	[BRCMNAND_OOB_READ_BASE]	=  0x20,
	[BRCMNAND_OOB_READ_10_BASE]	= 0x130,
	[BRCMNAND_OOB_WRITE_BASE]	=  0x30,
	[BRCMNAND_OOB_WRITE_10_BASE]	=     0,
	[BRCMNAND_FC_BASE]		= 0x200,
};

/* BRCMNAND v5.0 */
static const u16 brcmnand_regs_v50[] = {
	[BRCMNAND_CMD_START]		=  0x04,
	[BRCMNAND_CMD_EXT_ADDRESS]	=  0x08,
	[BRCMNAND_CMD_ADDRESS]		=  0x0c,
	[BRCMNAND_INTFC_STATUS]		=  0x6c,
	[BRCMNAND_CS_SELECT]		=  0x14,
	[BRCMNAND_CS_XOR]		=  0x18,
	[BRCMNAND_LL_OP]		= 0x178,
	[BRCMNAND_CS0_BASE]		=  0x40,
	[BRCMNAND_CS1_BASE]		=  0xd0,
	[BRCMNAND_CORR_THRESHOLD]	=  0x84,
	[BRCMNAND_CORR_THRESHOLD_EXT]	=     0,
	[BRCMNAND_UNCORR_COUNT]		=     0,
	[BRCMNAND_CORR_COUNT]		=     0,
	[BRCMNAND_CORR_EXT_ADDR]	=  0x70,
	[BRCMNAND_CORR_ADDR]		=  0x74,
	[BRCMNAND_UNCORR_EXT_ADDR]	=  0x78,
	[BRCMNAND_UNCORR_ADDR]		=  0x7c,
	[BRCMNAND_SEMAPHORE]		=  0x58,
	[BRCMNAND_ID]			=  0x60,
	[BRCMNAND_ID_EXT]		=  0x64,
	[BRCMNAND_LL_RDATA]		= 0x17c,
	[BRCMNAND_OOB_READ_BASE]	=  0x20,
	[BRCMNAND_OOB_READ_10_BASE]	= 0x130,
	[BRCMNAND_OOB_WRITE_BASE]	=  0x30,
	[BRCMNAND_OOB_WRITE_10_BASE]	= 0x140,
	[BRCMNAND_FC_BASE]		= 0x200,
};

/* BRCMNAND v6.0 - v7.1 */
static const u16 brcmnand_regs_v60[] = {
	[BRCMNAND_CMD_START]		=  0x04,
	[BRCMNAND_CMD_EXT_ADDRESS]	=  0x08,
	[BRCMNAND_CMD_ADDRESS]		=  0x0c,
	[BRCMNAND_INTFC_STATUS]		=  0x14,
	[BRCMNAND_CS_SELECT]		=  0x18,
	[BRCMNAND_CS_XOR]		=  0x1c,
	[BRCMNAND_LL_OP]		=  0x20,
	[BRCMNAND_CS0_BASE]		=  0x50,
	[BRCMNAND_CS1_BASE]		=     0,
	[BRCMNAND_CORR_THRESHOLD]	=  0xc0,
	[BRCMNAND_CORR_THRESHOLD_EXT]	=  0xc4,
	[BRCMNAND_UNCORR_COUNT]		=  0xfc,
	[BRCMNAND_CORR_COUNT]		= 0x100,
	[BRCMNAND_CORR_EXT_ADDR]	= 0x10c,
	[BRCMNAND_CORR_ADDR]		= 0x110,
	[BRCMNAND_UNCORR_EXT_ADDR]	= 0x114,
	[BRCMNAND_UNCORR_ADDR]		= 0x118,
	[BRCMNAND_SEMAPHORE]		= 0x150,
	[BRCMNAND_ID]			= 0x194,
	[BRCMNAND_ID_EXT]		= 0x198,
	[BRCMNAND_LL_RDATA]		= 0x19c,
	[BRCMNAND_OOB_READ_BASE]	= 0x200,
	[BRCMNAND_OOB_READ_10_BASE]	=     0,
	[BRCMNAND_OOB_WRITE_BASE]	= 0x280,
	[BRCMNAND_OOB_WRITE_10_BASE]	=     0,
	[BRCMNAND_FC_BASE]		= 0x400,
};

/* BRCMNAND v7.1 */
static const u16 brcmnand_regs_v71[] = {
	[BRCMNAND_CMD_START]		=  0x04,
	[BRCMNAND_CMD_EXT_ADDRESS]	=  0x08,
	[BRCMNAND_CMD_ADDRESS]		=  0x0c,
	[BRCMNAND_INTFC_STATUS]		=  0x14,
	[BRCMNAND_CS_SELECT]		=  0x18,
	[BRCMNAND_CS_XOR]		=  0x1c,
	[BRCMNAND_LL_OP]		=  0x20,
	[BRCMNAND_CS0_BASE]		=  0x50,
	[BRCMNAND_CS1_BASE]		=     0,
	[BRCMNAND_CORR_THRESHOLD]	=  0xdc,
	[BRCMNAND_CORR_THRESHOLD_EXT]	=  0xe0,
	[BRCMNAND_UNCORR_COUNT]		=  0xfc,
	[BRCMNAND_CORR_COUNT]		= 0x100,
	[BRCMNAND_CORR_EXT_ADDR]	= 0x10c,
	[BRCMNAND_CORR_ADDR]		= 0x110,
	[BRCMNAND_UNCORR_EXT_ADDR]	= 0x114,
	[BRCMNAND_UNCORR_ADDR]		= 0x118,
	[BRCMNAND_SEMAPHORE]		= 0x150,
	[BRCMNAND_ID]			= 0x194,
	[BRCMNAND_ID_EXT]		= 0x198,
	[BRCMNAND_LL_RDATA]		= 0x19c,
	[BRCMNAND_OOB_READ_BASE]	= 0x200,
	[BRCMNAND_OOB_READ_10_BASE]	=     0,
	[BRCMNAND_OOB_WRITE_BASE]	= 0x280,
	[BRCMNAND_OOB_WRITE_10_BASE]	=     0,
	[BRCMNAND_FC_BASE]		= 0x400,
};

/* BRCMNAND v7.2 */
static const u16 brcmnand_regs_v72[] = {
	[BRCMNAND_CMD_START]		=  0x04,
	[BRCMNAND_CMD_EXT_ADDRESS]	=  0x08,
	[BRCMNAND_CMD_ADDRESS]		=  0x0c,
	[BRCMNAND_INTFC_STATUS]		=  0x14,
	[BRCMNAND_CS_SELECT]		=  0x18,
	[BRCMNAND_CS_XOR]		=  0x1c,
	[BRCMNAND_LL_OP]		=  0x20,
	[BRCMNAND_CS0_BASE]		=  0x50,
	[BRCMNAND_CS1_BASE]		=     0,
	[BRCMNAND_CORR_THRESHOLD]	=  0xdc,
	[BRCMNAND_CORR_THRESHOLD_EXT]	=  0xe0,
	[BRCMNAND_UNCORR_COUNT]		=  0xfc,
	[BRCMNAND_CORR_COUNT]		= 0x100,
	[BRCMNAND_CORR_EXT_ADDR]	= 0x10c,
	[BRCMNAND_CORR_ADDR]		= 0x110,
	[BRCMNAND_UNCORR_EXT_ADDR]	= 0x114,
	[BRCMNAND_UNCORR_ADDR]		= 0x118,
	[BRCMNAND_SEMAPHORE]		= 0x150,
	[BRCMNAND_ID]			= 0x194,
	[BRCMNAND_ID_EXT]		= 0x198,
	[BRCMNAND_LL_RDATA]		= 0x19c,
	[BRCMNAND_OOB_READ_BASE]	= 0x200,
	[BRCMNAND_OOB_READ_10_BASE]	=     0,
	[BRCMNAND_OOB_WRITE_BASE]	= 0x400,
	[BRCMNAND_OOB_WRITE_10_BASE]	=     0,
	[BRCMNAND_FC_BASE]		= 0x600,
};

enum brcmnand_cs_reg {
	BRCMNAND_CS_CFG_EXT = 0,
	BRCMNAND_CS_CFG,
	BRCMNAND_CS_ACC_CONTROL,
	BRCMNAND_CS_TIMING1,
	BRCMNAND_CS_TIMING2,
};

/* Per chip-select offsets for v7.1 */
static const u8 brcmnand_cs_offsets_v71[] = {
	[BRCMNAND_CS_ACC_CONTROL]	= 0x00,
	[BRCMNAND_CS_CFG_EXT]		= 0x04,
	[BRCMNAND_CS_CFG]		= 0x08,
	[BRCMNAND_CS_TIMING1]		= 0x0c,
	[BRCMNAND_CS_TIMING2]		= 0x10,
};

/* Per chip-select offsets for pre v7.1, except CS0 on <= v5.0 */
static const u8 brcmnand_cs_offsets[] = {
	[BRCMNAND_CS_ACC_CONTROL]	= 0x00,
	[BRCMNAND_CS_CFG_EXT]		= 0x04,
	[BRCMNAND_CS_CFG]		= 0x04,
	[BRCMNAND_CS_TIMING1]		= 0x08,
	[BRCMNAND_CS_TIMING2]		= 0x0c,
};

/* Per chip-select offset for <= v5.0 on CS0 only */
static const u8 brcmnand_cs_offsets_cs0[] = {
	[BRCMNAND_CS_ACC_CONTROL]	= 0x00,
	[BRCMNAND_CS_CFG_EXT]		= 0x08,
	[BRCMNAND_CS_CFG]		= 0x08,
	[BRCMNAND_CS_TIMING1]		= 0x10,
	[BRCMNAND_CS_TIMING2]		= 0x14,
};

/*
 * Bitfields for the CFG and CFG_EXT registers. Pre-v7.1 controllers only had
 * one config register, but once the bitfields overflowed, newer controllers
 * (v7.1 and newer) added a CFG_EXT register and shuffled a few fields around.
 */
enum {
	CFG_BLK_ADR_BYTES_SHIFT		= 8,
	CFG_COL_ADR_BYTES_SHIFT		= 12,
	CFG_FUL_ADR_BYTES_SHIFT		= 16,
	CFG_BUS_WIDTH_SHIFT		= 23,
	CFG_BUS_WIDTH			= BIT(CFG_BUS_WIDTH_SHIFT),
	CFG_DEVICE_SIZE_SHIFT		= 24,

	/* Only for v2.1 */
	CFG_PAGE_SIZE_SHIFT_v2_1	= 30,

	/* Only for pre-v7.1 (with no CFG_EXT register) */
	CFG_PAGE_SIZE_SHIFT		= 20,
	CFG_BLK_SIZE_SHIFT		= 28,

	/* Only for v7.1+ (with CFG_EXT register) */
	CFG_EXT_PAGE_SIZE_SHIFT		= 0,
	CFG_EXT_BLK_SIZE_SHIFT		= 4,
};

/* BRCMNAND_INTFC_STATUS */
enum {
	INTFC_FLASH_STATUS		= GENMASK(7, 0),

	INTFC_ERASED			= BIT(27),
	INTFC_OOB_VALID			= BIT(28),
	INTFC_CACHE_VALID		= BIT(29),
	INTFC_FLASH_READY		= BIT(30),
	INTFC_CTLR_READY		= BIT(31),
};

/***********************************************************************
 * NAND ACC CONTROL bitfield
 *
 * Some bits have remained constant throughout hardware revision, while
 * others have shifted around.
 ***********************************************************************/

/* Constant for all versions (where supported) */
enum {
	/* See BRCMNAND_HAS_CACHE_MODE */
	ACC_CONTROL_CACHE_MODE				= BIT(22),

	/* See BRCMNAND_HAS_PREFETCH */
	ACC_CONTROL_PREFETCH				= BIT(23),

	ACC_CONTROL_PAGE_HIT				= BIT(24),
	ACC_CONTROL_WR_PREEMPT				= BIT(25),
	ACC_CONTROL_PARTIAL_PAGE			= BIT(26),
	ACC_CONTROL_RD_ERASED				= BIT(27),
	ACC_CONTROL_FAST_PGM_RDIN			= BIT(28),
	ACC_CONTROL_WR_ECC				= BIT(30),
	ACC_CONTROL_RD_ECC				= BIT(31),
};

#define	ACC_CONTROL_ECC_SHIFT			16
/* Only for v7.2 */
#define	ACC_CONTROL_ECC_EXT_SHIFT		13

static int brcmnand_status(struct brcmnand_host *host);

static inline bool brcmnand_non_mmio_ops(struct brcmnand_controller *ctrl)
{
#if IS_ENABLED(CONFIG_MTD_NAND_BRCMNAND_BCMA)
	return static_branch_unlikely(&brcmnand_soc_has_ops_key);
#else
	return false;
#endif
}

static inline u32 nand_readreg(struct brcmnand_controller *ctrl, u32 offs)
{
	if (brcmnand_non_mmio_ops(ctrl))
		return brcmnand_soc_read(ctrl->soc, offs);
	return brcmnand_readl(ctrl->nand_base + offs);
}

static inline void nand_writereg(struct brcmnand_controller *ctrl, u32 offs,
				 u32 val)
{
	if (brcmnand_non_mmio_ops(ctrl))
		brcmnand_soc_write(ctrl->soc, val, offs);
	else
		brcmnand_writel(val, ctrl->nand_base + offs);
}

static int brcmnand_revision_init(struct brcmnand_controller *ctrl)
{
	static const unsigned int block_sizes_v6[] = { 8, 16, 128, 256, 512, 1024, 2048, 0 };
	static const unsigned int block_sizes_v4[] = { 16, 128, 8, 512, 256, 1024, 2048, 0 };
	static const unsigned int block_sizes_v2_2[] = { 16, 128, 8, 512, 256, 0 };
	static const unsigned int block_sizes_v2_1[] = { 16, 128, 8, 512, 0 };
	static const unsigned int page_sizes_v3_4[] = { 512, 2048, 4096, 8192, 0 };
	static const unsigned int page_sizes_v2_2[] = { 512, 2048, 4096, 0 };
	static const unsigned int page_sizes_v2_1[] = { 512, 2048, 0 };

	ctrl->nand_version = nand_readreg(ctrl, 0) & 0xffff;

	/* Only support v2.1+ */
	if (ctrl->nand_version < 0x0201) {
		dev_err(ctrl->dev, "version %#x not supported\n",
			ctrl->nand_version);
		return -ENODEV;
	}

	/* Register offsets */
	if (ctrl->nand_version >= 0x0702)
		ctrl->reg_offsets = brcmnand_regs_v72;
	else if (ctrl->nand_version == 0x0701)
		ctrl->reg_offsets = brcmnand_regs_v71;
	else if (ctrl->nand_version >= 0x0600)
		ctrl->reg_offsets = brcmnand_regs_v60;
	else if (ctrl->nand_version >= 0x0500)
		ctrl->reg_offsets = brcmnand_regs_v50;
	else if (ctrl->nand_version >= 0x0303)
		ctrl->reg_offsets = brcmnand_regs_v33;
	else if (ctrl->nand_version >= 0x0201)
		ctrl->reg_offsets = brcmnand_regs_v21;

	/* Chip-select stride */
	if (ctrl->nand_version >= 0x0701)
		ctrl->reg_spacing = 0x14;
	else
		ctrl->reg_spacing = 0x10;

	/* Per chip-select registers */
	if (ctrl->nand_version >= 0x0701) {
		ctrl->cs_offsets = brcmnand_cs_offsets_v71;
	} else {
		ctrl->cs_offsets = brcmnand_cs_offsets;

		/* v3.3-5.0 have a different CS0 offset layout */
		if (ctrl->nand_version >= 0x0303 &&
		    ctrl->nand_version <= 0x0500)
			ctrl->cs0_offsets = brcmnand_cs_offsets_cs0;
	}

	/* Page / block sizes */
	if (ctrl->nand_version >= 0x0701) {
		/* >= v7.1 use nice power-of-2 values! */
		ctrl->max_page_size = 16 * 1024;
		ctrl->max_block_size = 2 * 1024 * 1024;
	} else {
		if (ctrl->nand_version >= 0x0304)
			ctrl->page_sizes = page_sizes_v3_4;
		else if (ctrl->nand_version >= 0x0202)
			ctrl->page_sizes = page_sizes_v2_2;
		else
			ctrl->page_sizes = page_sizes_v2_1;

		if (ctrl->nand_version >= 0x0202)
			ctrl->page_size_shift = CFG_PAGE_SIZE_SHIFT;
		else
			ctrl->page_size_shift = CFG_PAGE_SIZE_SHIFT_v2_1;

		if (ctrl->nand_version >= 0x0600)
			ctrl->block_sizes = block_sizes_v6;
		else if (ctrl->nand_version >= 0x0400)
			ctrl->block_sizes = block_sizes_v4;
		else if (ctrl->nand_version >= 0x0202)
			ctrl->block_sizes = block_sizes_v2_2;
		else
			ctrl->block_sizes = block_sizes_v2_1;

		if (ctrl->nand_version < 0x0400) {
			if (ctrl->nand_version < 0x0202)
				ctrl->max_page_size = 2048;
			else
				ctrl->max_page_size = 4096;
			ctrl->max_block_size = 512 * 1024;
		}
	}

	/* Maximum spare area sector size (per 512B) */
	if (ctrl->nand_version == 0x0702)
		ctrl->max_oob = 128;
	else if (ctrl->nand_version >= 0x0600)
		ctrl->max_oob = 64;
	else if (ctrl->nand_version >= 0x0500)
		ctrl->max_oob = 32;
	else
		ctrl->max_oob = 16;

	/* v6.0 and newer (except v6.1) have prefetch support */
	if (ctrl->nand_version >= 0x0600 && ctrl->nand_version != 0x0601)
		ctrl->features |= BRCMNAND_HAS_PREFETCH;

	/*
	 * v6.x has cache mode, but it's implemented differently. Ignore it for
	 * now.
	 */
	if (ctrl->nand_version >= 0x0700)
		ctrl->features |= BRCMNAND_HAS_CACHE_MODE;

	if (ctrl->nand_version >= 0x0500)
		ctrl->features |= BRCMNAND_HAS_1K_SECTORS;

	if (ctrl->nand_version >= 0x0700)
		ctrl->features |= BRCMNAND_HAS_WP;
	else if (of_property_read_bool(ctrl->dev->of_node, "brcm,nand-has-wp"))
		ctrl->features |= BRCMNAND_HAS_WP;

	/* v7.2 has different ecc level shift in the acc register */
	if (ctrl->nand_version == 0x0702)
		ctrl->ecc_level_shift = ACC_CONTROL_ECC_EXT_SHIFT;
	else
		ctrl->ecc_level_shift = ACC_CONTROL_ECC_SHIFT;

	return 0;
}

static void brcmnand_flash_dma_revision_init(struct brcmnand_controller *ctrl)
{
	/* flash_dma register offsets */
	if (ctrl->nand_version >= 0x0703)
		ctrl->flash_dma_offsets = flash_dma_regs_v4;
	else if (ctrl->nand_version == 0x0602)
		ctrl->flash_dma_offsets = flash_dma_regs_v0;
	else
		ctrl->flash_dma_offsets = flash_dma_regs_v1;
}

static inline u32 brcmnand_read_reg(struct brcmnand_controller *ctrl,
		enum brcmnand_reg reg)
{
	u16 offs = ctrl->reg_offsets[reg];

	if (offs)
		return nand_readreg(ctrl, offs);
	else
		return 0;
}

static inline void brcmnand_write_reg(struct brcmnand_controller *ctrl,
				      enum brcmnand_reg reg, u32 val)
{
	u16 offs = ctrl->reg_offsets[reg];

	if (offs)
		nand_writereg(ctrl, offs, val);
}

static inline void brcmnand_rmw_reg(struct brcmnand_controller *ctrl,
				    enum brcmnand_reg reg, u32 mask, unsigned
				    int shift, u32 val)
{
	u32 tmp = brcmnand_read_reg(ctrl, reg);

	tmp &= ~mask;
	tmp |= val << shift;
	brcmnand_write_reg(ctrl, reg, tmp);
}

static inline u32 brcmnand_read_fc(struct brcmnand_controller *ctrl, int word)
{
	if (brcmnand_non_mmio_ops(ctrl))
		return brcmnand_soc_read(ctrl->soc, BRCMNAND_NON_MMIO_FC_ADDR);
	return __raw_readl(ctrl->nand_fc + word * 4);
}

static inline void brcmnand_write_fc(struct brcmnand_controller *ctrl,
				     int word, u32 val)
{
	if (brcmnand_non_mmio_ops(ctrl))
		brcmnand_soc_write(ctrl->soc, val, BRCMNAND_NON_MMIO_FC_ADDR);
	else
		__raw_writel(val, ctrl->nand_fc + word * 4);
}

static inline void edu_writel(struct brcmnand_controller *ctrl,
			      enum edu_reg reg, u32 val)
{
	u16 offs = ctrl->edu_offsets[reg];

	brcmnand_writel(val, ctrl->edu_base + offs);
}

static inline u32 edu_readl(struct brcmnand_controller *ctrl,
			    enum edu_reg reg)
{
	u16 offs = ctrl->edu_offsets[reg];

	return brcmnand_readl(ctrl->edu_base + offs);
}

static inline void brcmnand_read_data_bus(struct brcmnand_controller *ctrl,
					  void __iomem *flash_cache, u32 *buffer, int fc_words)
{
	struct brcmnand_soc *soc = ctrl->soc;
	int i;

	if (soc && soc->read_data_bus) {
		soc->read_data_bus(soc, flash_cache, buffer, fc_words);
	} else {
		for (i = 0; i < fc_words; i++)
			buffer[i] = brcmnand_read_fc(ctrl, i);
	}
}

static void brcmnand_clear_ecc_addr(struct brcmnand_controller *ctrl)
{

	/* Clear error addresses */
	brcmnand_write_reg(ctrl, BRCMNAND_UNCORR_ADDR, 0);
	brcmnand_write_reg(ctrl, BRCMNAND_CORR_ADDR, 0);
	brcmnand_write_reg(ctrl, BRCMNAND_UNCORR_EXT_ADDR, 0);
	brcmnand_write_reg(ctrl, BRCMNAND_CORR_EXT_ADDR, 0);
}

static u64 brcmnand_get_uncorrecc_addr(struct brcmnand_controller *ctrl)
{
	u64 err_addr;

	err_addr = brcmnand_read_reg(ctrl, BRCMNAND_UNCORR_ADDR);
	err_addr |= ((u64)(brcmnand_read_reg(ctrl,
					     BRCMNAND_UNCORR_EXT_ADDR)
					     & 0xffff) << 32);

	return err_addr;
}

static u64 brcmnand_get_correcc_addr(struct brcmnand_controller *ctrl)
{
	u64 err_addr;

	err_addr = brcmnand_read_reg(ctrl, BRCMNAND_CORR_ADDR);
	err_addr |= ((u64)(brcmnand_read_reg(ctrl,
					     BRCMNAND_CORR_EXT_ADDR)
					     & 0xffff) << 32);

	return err_addr;
}

static void brcmnand_set_cmd_addr(struct mtd_info *mtd, u64 addr)
{
	struct nand_chip *chip =  mtd_to_nand(mtd);
	struct brcmnand_host *host = nand_get_controller_data(chip);
	struct brcmnand_controller *ctrl = host->ctrl;

	brcmnand_write_reg(ctrl, BRCMNAND_CMD_EXT_ADDRESS,
			   (host->cs << 16) | ((addr >> 32) & 0xffff));
	(void)brcmnand_read_reg(ctrl, BRCMNAND_CMD_EXT_ADDRESS);
	brcmnand_write_reg(ctrl, BRCMNAND_CMD_ADDRESS,
			   lower_32_bits(addr));
	(void)brcmnand_read_reg(ctrl, BRCMNAND_CMD_ADDRESS);
}

static inline u16 brcmnand_cs_offset(struct brcmnand_controller *ctrl, int cs,
				     enum brcmnand_cs_reg reg)
{
	u16 offs_cs0 = ctrl->reg_offsets[BRCMNAND_CS0_BASE];
	u16 offs_cs1 = ctrl->reg_offsets[BRCMNAND_CS1_BASE];
	u8 cs_offs;

	if (cs == 0 && ctrl->cs0_offsets)
		cs_offs = ctrl->cs0_offsets[reg];
	else
		cs_offs = ctrl->cs_offsets[reg];

	if (cs && offs_cs1)
		return offs_cs1 + (cs - 1) * ctrl->reg_spacing + cs_offs;

	return offs_cs0 + cs * ctrl->reg_spacing + cs_offs;
}

static inline u32 brcmnand_count_corrected(struct brcmnand_controller *ctrl)
{
	if (ctrl->nand_version < 0x0600)
		return 1;
	return brcmnand_read_reg(ctrl, BRCMNAND_CORR_COUNT);
}

static void brcmnand_wr_corr_thresh(struct brcmnand_host *host, u8 val)
{
	struct brcmnand_controller *ctrl = host->ctrl;
	unsigned int shift = 0, bits;
	enum brcmnand_reg reg = BRCMNAND_CORR_THRESHOLD;
	int cs = host->cs;

	if (!ctrl->reg_offsets[reg])
		return;

	if (ctrl->nand_version == 0x0702)
		bits = 7;
	else if (ctrl->nand_version >= 0x0600)
		bits = 6;
	else if (ctrl->nand_version >= 0x0500)
		bits = 5;
	else
		bits = 4;

	if (ctrl->nand_version >= 0x0702) {
		if (cs >= 4)
			reg = BRCMNAND_CORR_THRESHOLD_EXT;
		shift = (cs % 4) * bits;
	} else if (ctrl->nand_version >= 0x0600) {
		if (cs >= 5)
			reg = BRCMNAND_CORR_THRESHOLD_EXT;
		shift = (cs % 5) * bits;
	}
	brcmnand_rmw_reg(ctrl, reg, (bits - 1) << shift, shift, val);
}

static inline int brcmnand_cmd_shift(struct brcmnand_controller *ctrl)
{
	/* Kludge for the BCMA-based NAND controller which does not actually
	 * shift the command
	 */
	if (ctrl->nand_version == 0x0304 && brcmnand_non_mmio_ops(ctrl))
		return 0;

	if (ctrl->nand_version < 0x0602)
		return 24;
	return 0;
}

static inline u32 brcmnand_spare_area_mask(struct brcmnand_controller *ctrl)
{
	if (ctrl->nand_version == 0x0702)
		return GENMASK(7, 0);
	else if (ctrl->nand_version >= 0x0600)
		return GENMASK(6, 0);
	else if (ctrl->nand_version >= 0x0303)
		return GENMASK(5, 0);
	else
		return GENMASK(4, 0);
}

static inline u32 brcmnand_ecc_level_mask(struct brcmnand_controller *ctrl)
{
	u32 mask = (ctrl->nand_version >= 0x0600) ? 0x1f : 0x0f;

	mask <<= ACC_CONTROL_ECC_SHIFT;

	/* v7.2 includes additional ECC levels */
	if (ctrl->nand_version == 0x0702)
		mask |= 0x7 << ACC_CONTROL_ECC_EXT_SHIFT;

	return mask;
}

static void brcmnand_set_ecc_enabled(struct brcmnand_host *host, int en)
{
	struct brcmnand_controller *ctrl = host->ctrl;
	u16 offs = brcmnand_cs_offset(ctrl, host->cs, BRCMNAND_CS_ACC_CONTROL);
	u32 acc_control = nand_readreg(ctrl, offs);
	u32 ecc_flags = ACC_CONTROL_WR_ECC | ACC_CONTROL_RD_ECC;

	if (en) {
		acc_control |= ecc_flags; /* enable RD/WR ECC */
		acc_control &= ~brcmnand_ecc_level_mask(ctrl);
		acc_control |= host->hwcfg.ecc_level << ctrl->ecc_level_shift;
	} else {
		acc_control &= ~ecc_flags; /* disable RD/WR ECC */
		acc_control &= ~brcmnand_ecc_level_mask(ctrl);
	}

	nand_writereg(ctrl, offs, acc_control);
}

static inline int brcmnand_sector_1k_shift(struct brcmnand_controller *ctrl)
{
	if (ctrl->nand_version >= 0x0702)
		return 9;
	else if (ctrl->nand_version >= 0x0600)
		return 7;
	else if (ctrl->nand_version >= 0x0500)
		return 6;
	else
		return -1;
}

static bool brcmnand_get_sector_size_1k(struct brcmnand_host *host)
{
	struct brcmnand_controller *ctrl = host->ctrl;
	int sector_size_bit = brcmnand_sector_1k_shift(ctrl);
	u16 acc_control_offs = brcmnand_cs_offset(ctrl, host->cs,
						  BRCMNAND_CS_ACC_CONTROL);
	u32 acc_control;

	if (sector_size_bit < 0)
		return false;

	acc_control = nand_readreg(ctrl, acc_control_offs);

	return ((acc_control & BIT(sector_size_bit)) != 0);
}

static void brcmnand_set_sector_size_1k(struct brcmnand_host *host, int val)
{
	struct brcmnand_controller *ctrl = host->ctrl;
	int shift = brcmnand_sector_1k_shift(ctrl);
	u16 acc_control_offs = brcmnand_cs_offset(ctrl, host->cs,
						  BRCMNAND_CS_ACC_CONTROL);
	u32 tmp;

	if (shift < 0)
		return;

	tmp = nand_readreg(ctrl, acc_control_offs);
	tmp &= ~(1 << shift);
	tmp |= (!!val) << shift;
	nand_writereg(ctrl, acc_control_offs, tmp);
}

static int brcmnand_get_spare_size(struct brcmnand_host *host)
{
	struct brcmnand_controller *ctrl = host->ctrl;
	u16 acc_control_offs = brcmnand_cs_offset(ctrl, host->cs,
						  BRCMNAND_CS_ACC_CONTROL);
	u32 acc = nand_readreg(ctrl, acc_control_offs);

	return (acc & brcmnand_spare_area_mask(ctrl));
}

static void brcmnand_get_ecc_settings(struct brcmnand_host *host, struct nand_chip *chip)
{
	struct brcmnand_controller *ctrl = host->ctrl;
	u16 acc_control_offs = brcmnand_cs_offset(ctrl, host->cs,
						  BRCMNAND_CS_ACC_CONTROL);
	bool sector_size_1k = brcmnand_get_sector_size_1k(host);
	int spare_area_size, ecc_level;
	u32 acc;

	spare_area_size = brcmnand_get_spare_size(host);
	acc = nand_readreg(ctrl, acc_control_offs);
	ecc_level = (acc & brcmnand_ecc_level_mask(ctrl)) >> ctrl->ecc_level_shift;
	if (sector_size_1k)
		chip->ecc.strength = ecc_level * 2;
	else if (spare_area_size == 16 && ecc_level == 15)
		chip->ecc.strength = 1; /* hamming */
	else
		chip->ecc.strength = ecc_level;

	if (chip->ecc.size == 0) {
		if (sector_size_1k)
			chip->ecc.size = 1024;
		else
			chip->ecc.size = 512;
	}
}

/***********************************************************************
 * CS_NAND_SELECT
 ***********************************************************************/

enum {
	CS_SELECT_NAND_WP			= BIT(29),
	CS_SELECT_AUTO_DEVICE_ID_CFG		= BIT(30),
};

static int bcmnand_ctrl_poll_status(struct brcmnand_host *host,
				    u32 mask, u32 expected_val,
				    unsigned long timeout_ms)
{
	struct brcmnand_controller *ctrl = host->ctrl;
	unsigned long limit;
	u32 val;

	if (!timeout_ms)
		timeout_ms = NAND_POLL_STATUS_TIMEOUT_MS;

	limit = jiffies + msecs_to_jiffies(timeout_ms);
	do {
		if (mask & INTFC_FLASH_STATUS)
			brcmnand_status(host);

		val = brcmnand_read_reg(ctrl, BRCMNAND_INTFC_STATUS);
		if ((val & mask) == expected_val)
			return 0;

		cpu_relax();
	} while (time_after(limit, jiffies));

	/*
	 * do a final check after time out in case the CPU was busy and the driver
	 * did not get enough time to perform the polling to avoid false alarms
	 */
	if (mask & INTFC_FLASH_STATUS)
		brcmnand_status(host);

	val = brcmnand_read_reg(ctrl, BRCMNAND_INTFC_STATUS);
	if ((val & mask) == expected_val)
		return 0;

	dev_err(ctrl->dev, "timeout on status poll (expected %x got %x)\n",
		expected_val, val & mask);

	return -ETIMEDOUT;
}

static inline void brcmnand_set_wp(struct brcmnand_controller *ctrl, bool en)
{
	u32 val = en ? CS_SELECT_NAND_WP : 0;

	brcmnand_rmw_reg(ctrl, BRCMNAND_CS_SELECT, CS_SELECT_NAND_WP, 0, val);
}

/***********************************************************************
 * Flash DMA
 ***********************************************************************/

static inline bool has_flash_dma(struct brcmnand_controller *ctrl)
{
	return ctrl->flash_dma_base;
}

static inline bool has_edu(struct brcmnand_controller *ctrl)
{
	return ctrl->edu_base;
}

static inline bool use_dma(struct brcmnand_controller *ctrl)
{
	return has_flash_dma(ctrl) || has_edu(ctrl);
}

static inline void disable_ctrl_irqs(struct brcmnand_controller *ctrl)
{
	if (ctrl->pio_poll_mode)
		return;

	if (has_flash_dma(ctrl)) {
		ctrl->flash_dma_base = NULL;
		disable_irq(ctrl->dma_irq);
	}

	disable_irq(ctrl->irq);
	ctrl->pio_poll_mode = true;
}

static inline bool flash_dma_buf_ok(const void *buf)
{
	return buf && !is_vmalloc_addr(buf) &&
		likely(IS_ALIGNED((uintptr_t)buf, 4));
}

static inline void flash_dma_writel(struct brcmnand_controller *ctrl,
				    enum flash_dma_reg dma_reg, u32 val)
{
	u16 offs = ctrl->flash_dma_offsets[dma_reg];

	brcmnand_writel(val, ctrl->flash_dma_base + offs);
}

static inline u32 flash_dma_readl(struct brcmnand_controller *ctrl,
				  enum flash_dma_reg dma_reg)
{
	u16 offs = ctrl->flash_dma_offsets[dma_reg];

	return brcmnand_readl(ctrl->flash_dma_base + offs);
}

/* Low-level operation types: command, address, write, or read */
enum brcmnand_llop_type {
	LL_OP_CMD,
	LL_OP_ADDR,
	LL_OP_WR,
	LL_OP_RD,
};

/***********************************************************************
 * Internal support functions
 ***********************************************************************/

static inline bool is_hamming_ecc(struct brcmnand_controller *ctrl,
				  struct brcmnand_cfg *cfg)
{
	if (ctrl->nand_version <= 0x0701)
		return cfg->sector_size_1k == 0 && cfg->spare_area_size == 16 &&
			cfg->ecc_level == 15;
	else
		return cfg->sector_size_1k == 0 && ((cfg->spare_area_size == 16 &&
			cfg->ecc_level == 15) ||
			(cfg->spare_area_size == 28 && cfg->ecc_level == 16));
}

/*
 * Set mtd->ooblayout to the appropriate mtd_ooblayout_ops given
 * the layout/configuration.
 * Returns -ERRCODE on failure.
 */
static int brcmnand_hamming_ooblayout_ecc(struct mtd_info *mtd, int section,
					  struct mtd_oob_region *oobregion)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct brcmnand_host *host = nand_get_controller_data(chip);
	struct brcmnand_cfg *cfg = &host->hwcfg;
	int sas = cfg->spare_area_size << cfg->sector_size_1k;
	int sectors = cfg->page_size / (512 << cfg->sector_size_1k);

	if (section >= sectors)
		return -ERANGE;

	oobregion->offset = (section * sas) + 6;
	oobregion->length = 3;

	return 0;
}

static int brcmnand_hamming_ooblayout_free(struct mtd_info *mtd, int section,
					   struct mtd_oob_region *oobregion)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct brcmnand_host *host = nand_get_controller_data(chip);
	struct brcmnand_cfg *cfg = &host->hwcfg;
	int sas = cfg->spare_area_size << cfg->sector_size_1k;
	int sectors = cfg->page_size / (512 << cfg->sector_size_1k);
	u32 next;

	if (section > sectors)
		return -ERANGE;

	next = (section * sas);
	if (section < sectors)
		next += 6;

	if (section) {
		oobregion->offset = ((section - 1) * sas) + 9;
	} else {
		if (cfg->page_size > 512) {
			/* Large page NAND uses first 2 bytes for BBI */
			oobregion->offset = 2;
		} else {
			/* Small page NAND uses last byte before ECC for BBI */
			oobregion->offset = 0;
			next--;
		}
	}

	oobregion->length = next - oobregion->offset;

	return 0;
}

static const struct mtd_ooblayout_ops brcmnand_hamming_ooblayout_ops = {
	.ecc = brcmnand_hamming_ooblayout_ecc,
	.free = brcmnand_hamming_ooblayout_free,
};

static int brcmnand_bch_ooblayout_ecc(struct mtd_info *mtd, int section,
				      struct mtd_oob_region *oobregion)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct brcmnand_host *host = nand_get_controller_data(chip);
	struct brcmnand_cfg *cfg = &host->hwcfg;
	int sas = cfg->spare_area_size << cfg->sector_size_1k;
	int sectors = cfg->page_size / (512 << cfg->sector_size_1k);

	if (section >= sectors)
		return -ERANGE;

	oobregion->offset = ((section + 1) * sas) - chip->ecc.bytes;
	oobregion->length = chip->ecc.bytes;

	return 0;
}

static int brcmnand_bch_ooblayout_free_lp(struct mtd_info *mtd, int section,
					  struct mtd_oob_region *oobregion)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct brcmnand_host *host = nand_get_controller_data(chip);
	struct brcmnand_cfg *cfg = &host->hwcfg;
	int sas = cfg->spare_area_size << cfg->sector_size_1k;
	int sectors = cfg->page_size / (512 << cfg->sector_size_1k);

	if (section >= sectors)
		return -ERANGE;

	if (sas <= chip->ecc.bytes)
		return 0;

	oobregion->offset = section * sas;
	oobregion->length = sas - chip->ecc.bytes;

	if (!section) {
		oobregion->offset++;
		oobregion->length--;
	}

	return 0;
}

static int brcmnand_bch_ooblayout_free_sp(struct mtd_info *mtd, int section,
					  struct mtd_oob_region *oobregion)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct brcmnand_host *host = nand_get_controller_data(chip);
	struct brcmnand_cfg *cfg = &host->hwcfg;
	int sas = cfg->spare_area_size << cfg->sector_size_1k;

	if (section > 1 || sas - chip->ecc.bytes < 6 ||
	    (section && sas - chip->ecc.bytes == 6))
		return -ERANGE;

	if (!section) {
		oobregion->offset = 0;
		oobregion->length = 5;
	} else {
		oobregion->offset = 6;
		oobregion->length = sas - chip->ecc.bytes - 6;
	}

	return 0;
}

static const struct mtd_ooblayout_ops brcmnand_bch_lp_ooblayout_ops = {
	.ecc = brcmnand_bch_ooblayout_ecc,
	.free = brcmnand_bch_ooblayout_free_lp,
};

static const struct mtd_ooblayout_ops brcmnand_bch_sp_ooblayout_ops = {
	.ecc = brcmnand_bch_ooblayout_ecc,
	.free = brcmnand_bch_ooblayout_free_sp,
};

static int brcmstb_choose_ecc_layout(struct brcmnand_host *host)
{
	struct brcmnand_cfg *p = &host->hwcfg;
	struct mtd_info *mtd = nand_to_mtd(&host->chip);
	struct nand_ecc_ctrl *ecc = &host->chip.ecc;
	unsigned int ecc_level = p->ecc_level;
	int sas = p->spare_area_size << p->sector_size_1k;
	int sectors = p->page_size / (512 << p->sector_size_1k);

	if (p->sector_size_1k)
		ecc_level <<= 1;

	if (is_hamming_ecc(host->ctrl, p)) {
		ecc->bytes = 3 * sectors;
		mtd_set_ooblayout(mtd, &brcmnand_hamming_ooblayout_ops);
		return 0;
	}

	/*
	 * CONTROLLER_VERSION:
	 *   < v5.0: ECC_REQ = ceil(BCH_T * 13/8)
	 *  >= v5.0: ECC_REQ = ceil(BCH_T * 14/8)
	 * But we will just be conservative.
	 */
	ecc->bytes = DIV_ROUND_UP(ecc_level * 14, 8);
	if (p->page_size == 512)
		mtd_set_ooblayout(mtd, &brcmnand_bch_sp_ooblayout_ops);
	else
		mtd_set_ooblayout(mtd, &brcmnand_bch_lp_ooblayout_ops);

	if (ecc->bytes >= sas) {
		dev_err(&host->pdev->dev,
			"error: ECC too large for OOB (ECC bytes %d, spare sector %d)\n",
			ecc->bytes, sas);
		return -EINVAL;
	}

	return 0;
}

static void brcmnand_wp(struct mtd_info *mtd, int wp)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct brcmnand_host *host = nand_get_controller_data(chip);
	struct brcmnand_controller *ctrl = host->ctrl;

	if ((ctrl->features & BRCMNAND_HAS_WP) && wp_on == 1) {
		static int old_wp = -1;
		int ret;

		if (old_wp != wp) {
			dev_dbg(ctrl->dev, "WP %s\n", wp ? "on" : "off");
			old_wp = wp;
		}

		/*
		 * make sure ctrl/flash ready before and after
		 * changing state of #WP pin
		 */
		ret = bcmnand_ctrl_poll_status(host, NAND_CTRL_RDY |
					       NAND_STATUS_READY,
					       NAND_CTRL_RDY |
					       NAND_STATUS_READY, 0);
		if (ret)
			return;

		brcmnand_set_wp(ctrl, wp);
		/* force controller operation to update internal copy of NAND chip status */
		brcmnand_status(host);
		/* NAND_STATUS_WP 0x00 = protected, 0x80 = not protected */
		ret = bcmnand_ctrl_poll_status(host,
					       NAND_CTRL_RDY |
					       NAND_STATUS_READY |
					       NAND_STATUS_WP,
					       NAND_CTRL_RDY |
					       NAND_STATUS_READY |
					       (wp ? 0 : NAND_STATUS_WP), 0);

		if (ret)
			dev_err_ratelimited(&host->pdev->dev,
					    "nand #WP expected %s\n",
					    wp ? "on" : "off");
	}
}

/* Helper functions for reading and writing OOB registers */
static inline u8 oob_reg_read(struct brcmnand_controller *ctrl, u32 offs)
{
	u16 offset0, offset10, reg_offs;

	offset0 = ctrl->reg_offsets[BRCMNAND_OOB_READ_BASE];
	offset10 = ctrl->reg_offsets[BRCMNAND_OOB_READ_10_BASE];

	if (offs >= ctrl->max_oob)
		return 0x77;

	if (offs >= 16 && offset10)
		reg_offs = offset10 + ((offs - 0x10) & ~0x03);
	else
		reg_offs = offset0 + (offs & ~0x03);

	return nand_readreg(ctrl, reg_offs) >> (24 - ((offs & 0x03) << 3));
}

static inline void oob_reg_write(struct brcmnand_controller *ctrl, u32 offs,
				 u32 data)
{
	u16 offset0, offset10, reg_offs;

	offset0 = ctrl->reg_offsets[BRCMNAND_OOB_WRITE_BASE];
	offset10 = ctrl->reg_offsets[BRCMNAND_OOB_WRITE_10_BASE];

	if (offs >= ctrl->max_oob)
		return;

	if (offs >= 16 && offset10)
		reg_offs = offset10 + ((offs - 0x10) & ~0x03);
	else
		reg_offs = offset0 + (offs & ~0x03);

	nand_writereg(ctrl, reg_offs, data);
}

/*
 * read_oob_from_regs - read data from OOB registers
 * @ctrl: NAND controller
 * @i: sub-page sector index
 * @oob: buffer to read to
 * @sas: spare area sector size (i.e., OOB size per FLASH_CACHE)
 * @sector_1k: 1 for 1KiB sectors, 0 for 512B, other values are illegal
 */
static int read_oob_from_regs(struct brcmnand_controller *ctrl, int i, u8 *oob,
			      int sas, int sector_1k)
{
	int tbytes = sas << sector_1k;
	int j;

	/* Adjust OOB values for 1K sector size */
	if (sector_1k && (i & 0x01))
		tbytes = max(0, tbytes - (int)ctrl->max_oob);
	tbytes = min_t(int, tbytes, ctrl->max_oob);

	for (j = 0; j < tbytes; j++)
		oob[j] = oob_reg_read(ctrl, j);
	return tbytes;
}

/*
 * write_oob_to_regs - write data to OOB registers
 * @i: sub-page sector index
 * @oob: buffer to write from
 * @sas: spare area sector size (i.e., OOB size per FLASH_CACHE)
 * @sector_1k: 1 for 1KiB sectors, 0 for 512B, other values are illegal
 */
static int write_oob_to_regs(struct brcmnand_controller *ctrl, int i,
			     const u8 *oob, int sas, int sector_1k)
{
	int tbytes = sas << sector_1k;
	int j, k = 0;
	u32 last = 0xffffffff;
	u8 *plast = (u8 *)&last;

	/* Adjust OOB values for 1K sector size */
	if (sector_1k && (i & 0x01))
		tbytes = max(0, tbytes - (int)ctrl->max_oob);
	tbytes = min_t(int, tbytes, ctrl->max_oob);

	/*
	 * tbytes may not be multiple of words. Make sure we don't read out of
	 * the boundary and stop at last word.
	 */
	for (j = 0; (j + 3) < tbytes; j += 4)
		oob_reg_write(ctrl, j,
				(oob[j + 0] << 24) |
				(oob[j + 1] << 16) |
				(oob[j + 2] <<  8) |
				(oob[j + 3] <<  0));

	/* handle the remaing bytes */
	while (j < tbytes)
		plast[k++] = oob[j++];

	if (tbytes & 0x3)
		oob_reg_write(ctrl, (tbytes & ~0x3), (__force u32)cpu_to_be32(last));

	return tbytes;
}

static void brcmnand_edu_init(struct brcmnand_controller *ctrl)
{
	/* initialize edu */
	edu_writel(ctrl, EDU_ERR_STATUS, 0);
	edu_readl(ctrl, EDU_ERR_STATUS);
	edu_writel(ctrl, EDU_DONE, 0);
	edu_writel(ctrl, EDU_DONE, 0);
	edu_writel(ctrl, EDU_DONE, 0);
	edu_writel(ctrl, EDU_DONE, 0);
	edu_readl(ctrl, EDU_DONE);
}

/* edu irq */
static irqreturn_t brcmnand_edu_irq(int irq, void *data)
{
	struct brcmnand_controller *ctrl = data;

	if (ctrl->edu_count) {
		ctrl->edu_count--;
		while (!(edu_readl(ctrl, EDU_DONE) & EDU_DONE_MASK))
			udelay(1);
		edu_writel(ctrl, EDU_DONE, 0);
		edu_readl(ctrl, EDU_DONE);
	}

	if (ctrl->edu_count) {
		ctrl->edu_dram_addr += FC_BYTES;
		ctrl->edu_ext_addr += FC_BYTES;

		edu_writel(ctrl, EDU_DRAM_ADDR, (u32)ctrl->edu_dram_addr);
		edu_readl(ctrl, EDU_DRAM_ADDR);
		edu_writel(ctrl, EDU_EXT_ADDR, ctrl->edu_ext_addr);
		edu_readl(ctrl, EDU_EXT_ADDR);

		if (ctrl->oob) {
			if (ctrl->edu_cmd == EDU_CMD_READ) {
				ctrl->oob += read_oob_from_regs(ctrl,
							ctrl->edu_count + 1,
							ctrl->oob, ctrl->sas,
							ctrl->sector_size_1k);
			} else {
				brcmnand_write_reg(ctrl, BRCMNAND_CMD_ADDRESS,
						   ctrl->edu_ext_addr);
				brcmnand_read_reg(ctrl, BRCMNAND_CMD_ADDRESS);
				ctrl->oob += write_oob_to_regs(ctrl,
							       ctrl->edu_count,
							       ctrl->oob, ctrl->sas,
							       ctrl->sector_size_1k);
			}
		}

		mb(); /* flush previous writes */
		edu_writel(ctrl, EDU_CMD, ctrl->edu_cmd);
		edu_readl(ctrl, EDU_CMD);

		return IRQ_HANDLED;
	}

	complete(&ctrl->edu_done);

	return IRQ_HANDLED;
}

static irqreturn_t brcmnand_ctlrdy_irq(int irq, void *data)
{
	struct brcmnand_controller *ctrl = data;

	/* Discard all NAND_CTLRDY interrupts during DMA */
	if (ctrl->dma_pending)
		return IRQ_HANDLED;

	/* check if you need to piggy back on the ctrlrdy irq */
	if (ctrl->edu_pending) {
		if (irq == ctrl->irq && ((int)ctrl->edu_irq >= 0))
	/* Discard interrupts while using dedicated edu irq */
			return IRQ_HANDLED;

	/* no registered edu irq, call handler */
		return brcmnand_edu_irq(irq, data);
	}

	complete(&ctrl->done);
	return IRQ_HANDLED;
}

/* Handle SoC-specific interrupt hardware */
static irqreturn_t brcmnand_irq(int irq, void *data)
{
	struct brcmnand_controller *ctrl = data;

	if (ctrl->soc->ctlrdy_ack(ctrl->soc))
		return brcmnand_ctlrdy_irq(irq, data);

	return IRQ_NONE;
}

static irqreturn_t brcmnand_dma_irq(int irq, void *data)
{
	struct brcmnand_controller *ctrl = data;

	complete(&ctrl->dma_done);

	return IRQ_HANDLED;
}

static void brcmnand_send_cmd(struct brcmnand_host *host, int cmd)
{
	struct brcmnand_controller *ctrl = host->ctrl;
	int ret;
	u64 cmd_addr;

	cmd_addr = brcmnand_read_reg(ctrl, BRCMNAND_CMD_ADDRESS);

	dev_dbg(ctrl->dev, "send native cmd %d addr 0x%llx\n", cmd, cmd_addr);

	/*
	 * If we came here through _panic_write and there is a pending
	 * command, try to wait for it. If it times out, rather than
	 * hitting BUG_ON, just return so we don't crash while crashing.
	 */
	if (oops_in_progress) {
		if (ctrl->cmd_pending &&
			bcmnand_ctrl_poll_status(host, NAND_CTRL_RDY, NAND_CTRL_RDY, 0))
			return;
	} else
		BUG_ON(ctrl->cmd_pending != 0);
	ctrl->cmd_pending = cmd;

	ret = bcmnand_ctrl_poll_status(host, NAND_CTRL_RDY, NAND_CTRL_RDY, 0);
	WARN_ON(ret);

	mb(); /* flush previous writes */
	brcmnand_write_reg(ctrl, BRCMNAND_CMD_START,
			   cmd << brcmnand_cmd_shift(ctrl));
}

static bool brcmstb_nand_wait_for_completion(struct nand_chip *chip)
{
	struct brcmnand_host *host = nand_get_controller_data(chip);
	struct brcmnand_controller *ctrl = host->ctrl;
	struct mtd_info *mtd = nand_to_mtd(chip);
	bool err = false;
	int sts;

	if (mtd->oops_panic_write || ctrl->irq < 0) {
		/* switch to interrupt polling and PIO mode */
		disable_ctrl_irqs(ctrl);
		sts = bcmnand_ctrl_poll_status(host, NAND_CTRL_RDY,
					       NAND_CTRL_RDY, 0);
		err = sts < 0;
	} else {
		unsigned long timeo = msecs_to_jiffies(
						NAND_POLL_STATUS_TIMEOUT_MS);
		/* wait for completion interrupt */
		sts = wait_for_completion_timeout(&ctrl->done, timeo);
		err = !sts;
	}

	return err;
}

static int brcmnand_waitfunc(struct nand_chip *chip)
{
	struct brcmnand_host *host = nand_get_controller_data(chip);
	struct brcmnand_controller *ctrl = host->ctrl;
	bool err = false;

	dev_dbg(ctrl->dev, "wait on native cmd %d\n", ctrl->cmd_pending);
	if (ctrl->cmd_pending)
		err = brcmstb_nand_wait_for_completion(chip);

	ctrl->cmd_pending = 0;
	if (err) {
		u32 cmd = brcmnand_read_reg(ctrl, BRCMNAND_CMD_START)
					>> brcmnand_cmd_shift(ctrl);

		dev_err_ratelimited(ctrl->dev,
			"timeout waiting for command %#02x\n", cmd);
		dev_err_ratelimited(ctrl->dev, "intfc status %08x\n",
			brcmnand_read_reg(ctrl, BRCMNAND_INTFC_STATUS));
		return -ETIMEDOUT;
	}
	return brcmnand_read_reg(ctrl, BRCMNAND_INTFC_STATUS) &
				 INTFC_FLASH_STATUS;
}

static int brcmnand_status(struct brcmnand_host *host)
{
	struct nand_chip *chip = &host->chip;
	struct mtd_info *mtd = nand_to_mtd(chip);

	brcmnand_set_cmd_addr(mtd, 0);
	brcmnand_send_cmd(host, CMD_STATUS_READ);

	return brcmnand_waitfunc(chip);
}

static int brcmnand_reset(struct brcmnand_host *host)
{
	struct nand_chip *chip = &host->chip;

	brcmnand_send_cmd(host, CMD_FLASH_RESET);

	return brcmnand_waitfunc(chip);
}

enum {
	LLOP_RE				= BIT(16),
	LLOP_WE				= BIT(17),
	LLOP_ALE			= BIT(18),
	LLOP_CLE			= BIT(19),
	LLOP_RETURN_IDLE		= BIT(31),

	LLOP_DATA_MASK			= GENMASK(15, 0),
};

static int brcmnand_low_level_op(struct brcmnand_host *host,
				 enum brcmnand_llop_type type, u32 data,
				 bool last_op)
{
	struct nand_chip *chip = &host->chip;
	struct brcmnand_controller *ctrl = host->ctrl;
	u32 tmp;

	tmp = data & LLOP_DATA_MASK;
	switch (type) {
	case LL_OP_CMD:
		tmp |= LLOP_WE | LLOP_CLE;
		break;
	case LL_OP_ADDR:
		/* WE | ALE */
		tmp |= LLOP_WE | LLOP_ALE;
		break;
	case LL_OP_WR:
		/* WE */
		tmp |= LLOP_WE;
		break;
	case LL_OP_RD:
		/* RE */
		tmp |= LLOP_RE;
		break;
	}
	if (last_op)
		/* RETURN_IDLE */
		tmp |= LLOP_RETURN_IDLE;

	dev_dbg(ctrl->dev, "ll_op cmd %#x\n", tmp);

	brcmnand_write_reg(ctrl, BRCMNAND_LL_OP, tmp);
	(void)brcmnand_read_reg(ctrl, BRCMNAND_LL_OP);

	brcmnand_send_cmd(host, CMD_LOW_LEVEL_OP);
	return brcmnand_waitfunc(chip);
}

/*
 *  Kick EDU engine
 */
static int brcmnand_edu_trans(struct brcmnand_host *host, u64 addr, u32 *buf,
			      u8 *oob, u32 len, u8 cmd)
{
	struct brcmnand_controller *ctrl = host->ctrl;
	struct brcmnand_cfg *cfg = &host->hwcfg;
	unsigned long timeo = msecs_to_jiffies(200);
	int ret = 0;
	int dir = (cmd == CMD_PAGE_READ ? DMA_FROM_DEVICE : DMA_TO_DEVICE);
	u8 edu_cmd = (cmd == CMD_PAGE_READ ? EDU_CMD_READ : EDU_CMD_WRITE);
	unsigned int trans = len >> FC_SHIFT;
	dma_addr_t pa;

	dev_dbg(ctrl->dev, "EDU %s %p:%p\n", ((edu_cmd == EDU_CMD_READ) ?
					      "read" : "write"), buf, oob);

	pa = dma_map_single(ctrl->dev, buf, len, dir);
	if (dma_mapping_error(ctrl->dev, pa)) {
		dev_err(ctrl->dev, "unable to map buffer for EDU DMA\n");
		return -ENOMEM;
	}

	ctrl->edu_pending = true;
	ctrl->edu_dram_addr = pa;
	ctrl->edu_ext_addr = addr;
	ctrl->edu_cmd = edu_cmd;
	ctrl->edu_count = trans;
	ctrl->sas = cfg->spare_area_size;
	ctrl->oob = oob;

	edu_writel(ctrl, EDU_DRAM_ADDR, (u32)ctrl->edu_dram_addr);
	edu_readl(ctrl,  EDU_DRAM_ADDR);
	edu_writel(ctrl, EDU_EXT_ADDR, ctrl->edu_ext_addr);
	edu_readl(ctrl, EDU_EXT_ADDR);
	edu_writel(ctrl, EDU_LENGTH, FC_BYTES);
	edu_readl(ctrl, EDU_LENGTH);

	if (ctrl->oob && (ctrl->edu_cmd == EDU_CMD_WRITE)) {
		brcmnand_write_reg(ctrl, BRCMNAND_CMD_ADDRESS,
				   ctrl->edu_ext_addr);
		brcmnand_read_reg(ctrl, BRCMNAND_CMD_ADDRESS);
		ctrl->oob += write_oob_to_regs(ctrl,
					       1,
					       ctrl->oob, ctrl->sas,
					       ctrl->sector_size_1k);
	}

	/* Start edu engine */
	mb(); /* flush previous writes */
	edu_writel(ctrl, EDU_CMD, ctrl->edu_cmd);
	edu_readl(ctrl, EDU_CMD);

	if (wait_for_completion_timeout(&ctrl->edu_done, timeo) <= 0) {
		dev_err(ctrl->dev,
			"timeout waiting for EDU; status %#x, error status %#x\n",
			edu_readl(ctrl, EDU_STATUS),
			edu_readl(ctrl, EDU_ERR_STATUS));
	}

	dma_unmap_single(ctrl->dev, pa, len, dir);

	/* read last subpage oob */
	if (ctrl->oob && (ctrl->edu_cmd == EDU_CMD_READ)) {
		ctrl->oob += read_oob_from_regs(ctrl,
						1,
						ctrl->oob, ctrl->sas,
						ctrl->sector_size_1k);
	}

	/* for program page check NAND status */
	if (((brcmnand_read_reg(ctrl, BRCMNAND_INTFC_STATUS) &
	      INTFC_FLASH_STATUS) & NAND_STATUS_FAIL) &&
	    edu_cmd == EDU_CMD_WRITE) {
		dev_info(ctrl->dev, "program failed at %llx\n",
			 (unsigned long long)addr);
		ret = -EIO;
	}

	/* Make sure the EDU status is clean */
	if (edu_readl(ctrl, EDU_STATUS) & EDU_STATUS_ACTIVE)
		dev_warn(ctrl->dev, "EDU still active: %#x\n",
			 edu_readl(ctrl, EDU_STATUS));

	if (unlikely(edu_readl(ctrl, EDU_ERR_STATUS) & EDU_ERR_STATUS_ERRACK)) {
		dev_warn(ctrl->dev, "EDU RBUS error at addr %llx\n",
			 (unsigned long long)addr);
		ret = -EIO;
	}

	ctrl->edu_pending = false;
	brcmnand_edu_init(ctrl);
	edu_writel(ctrl, EDU_STOP, 0); /* force stop */
	edu_readl(ctrl, EDU_STOP);

	if (!ret && edu_cmd == EDU_CMD_READ) {
		u64 err_addr = 0;

		/*
		 * check for ECC errors here, subpage ECC errors are
		 * retained in ECC error address register
		 */
		err_addr = brcmnand_get_uncorrecc_addr(ctrl);
		if (!err_addr) {
			err_addr = brcmnand_get_correcc_addr(ctrl);
			if (err_addr)
				ret = -EUCLEAN;
		} else
			ret = -EBADMSG;
	}

	return ret;
}

/*
 * Construct a FLASH_DMA descriptor as part of a linked list. You must know the
 * following ahead of time:
 *  - Is this descriptor the beginning or end of a linked list?
 *  - What is the (DMA) address of the next descriptor in the linked list?
 */
static int brcmnand_fill_dma_desc(struct brcmnand_host *host,
				  struct brcm_nand_dma_desc *desc, u64 addr,
				  dma_addr_t buf, u32 len, u8 dma_cmd,
				  bool begin, bool end,
				  dma_addr_t next_desc)
{
	memset(desc, 0, sizeof(*desc));
	/* Descriptors are written in native byte order (wordwise) */
	desc->next_desc = lower_32_bits(next_desc);
	desc->next_desc_ext = upper_32_bits(next_desc);
	desc->cmd_irq = (dma_cmd << 24) |
		(end ? (0x03 << 8) : 0) | /* IRQ | STOP */
		(!!begin) | ((!!end) << 1); /* head, tail */
#ifdef CONFIG_CPU_BIG_ENDIAN
	desc->cmd_irq |= 0x01 << 12;
#endif
	desc->dram_addr = lower_32_bits(buf);
	desc->dram_addr_ext = upper_32_bits(buf);
	desc->tfr_len = len;
	desc->total_len = len;
	desc->flash_addr = lower_32_bits(addr);
	desc->flash_addr_ext = upper_32_bits(addr);
	desc->cs = host->cs;
	desc->status_valid = 0x01;
	return 0;
}

/*
 * Kick the FLASH_DMA engine, with a given DMA descriptor
 */
static void brcmnand_dma_run(struct brcmnand_host *host, dma_addr_t desc)
{
	struct brcmnand_controller *ctrl = host->ctrl;
	unsigned long timeo = msecs_to_jiffies(100);

	flash_dma_writel(ctrl, FLASH_DMA_FIRST_DESC, lower_32_bits(desc));
	(void)flash_dma_readl(ctrl, FLASH_DMA_FIRST_DESC);
	if (ctrl->nand_version > 0x0602) {
		flash_dma_writel(ctrl, FLASH_DMA_FIRST_DESC_EXT,
				 upper_32_bits(desc));
		(void)flash_dma_readl(ctrl, FLASH_DMA_FIRST_DESC_EXT);
	}

	/* Start FLASH_DMA engine */
	ctrl->dma_pending = true;
	mb(); /* flush previous writes */
	flash_dma_writel(ctrl, FLASH_DMA_CTRL, 0x03); /* wake | run */

	if (wait_for_completion_timeout(&ctrl->dma_done, timeo) <= 0) {
		dev_err(ctrl->dev,
				"timeout waiting for DMA; status %#x, error status %#x\n",
				flash_dma_readl(ctrl, FLASH_DMA_STATUS),
				flash_dma_readl(ctrl, FLASH_DMA_ERROR_STATUS));
	}
	ctrl->dma_pending = false;
	flash_dma_writel(ctrl, FLASH_DMA_CTRL, 0); /* force stop */
}

static int brcmnand_dma_trans(struct brcmnand_host *host, u64 addr, u32 *buf,
			      u8 *oob, u32 len, u8 dma_cmd)
{
	struct brcmnand_controller *ctrl = host->ctrl;
	dma_addr_t buf_pa;
	int dir = dma_cmd == CMD_PAGE_READ ? DMA_FROM_DEVICE : DMA_TO_DEVICE;

	buf_pa = dma_map_single(ctrl->dev, buf, len, dir);
	if (dma_mapping_error(ctrl->dev, buf_pa)) {
		dev_err(ctrl->dev, "unable to map buffer for DMA\n");
		return -ENOMEM;
	}

	brcmnand_fill_dma_desc(host, ctrl->dma_desc, addr, buf_pa, len,
				   dma_cmd, true, true, 0);

	brcmnand_dma_run(host, ctrl->dma_pa);

	dma_unmap_single(ctrl->dev, buf_pa, len, dir);

	if (ctrl->dma_desc->status_valid & FLASH_DMA_ECC_ERROR)
		return -EBADMSG;
	else if (ctrl->dma_desc->status_valid & FLASH_DMA_CORR_ERROR)
		return -EUCLEAN;

	return 0;
}

/*
 * Assumes proper CS is already set
 */
static int brcmnand_read_by_pio(struct mtd_info *mtd, struct nand_chip *chip,
				u64 addr, unsigned int trans, u32 *buf,
				u8 *oob, u64 *err_addr)
{
	struct brcmnand_host *host = nand_get_controller_data(chip);
	struct brcmnand_controller *ctrl = host->ctrl;
	int i, ret = 0;

	brcmnand_clear_ecc_addr(ctrl);

	for (i = 0; i < trans; i++, addr += FC_BYTES) {
		brcmnand_set_cmd_addr(mtd, addr);
		/* SPARE_AREA_READ does not use ECC, so just use PAGE_READ */
		brcmnand_send_cmd(host, CMD_PAGE_READ);
		brcmnand_waitfunc(chip);

		if (likely(buf)) {
			brcmnand_soc_data_bus_prepare(ctrl->soc, false);

			brcmnand_read_data_bus(ctrl, ctrl->nand_fc, buf, FC_WORDS);
			buf += FC_WORDS;

			brcmnand_soc_data_bus_unprepare(ctrl->soc, false);
		}

		if (oob)
			oob += read_oob_from_regs(ctrl, i, oob,
					mtd->oobsize / trans,
					host->hwcfg.sector_size_1k);

		if (ret != -EBADMSG) {
			*err_addr = brcmnand_get_uncorrecc_addr(ctrl);

			if (*err_addr)
				ret = -EBADMSG;
		}

		if (!ret) {
			*err_addr = brcmnand_get_correcc_addr(ctrl);

			if (*err_addr)
				ret = -EUCLEAN;
		}
	}

	return ret;
}

/*
 * Check a page to see if it is erased (w/ bitflips) after an uncorrectable ECC
 * error
 *
 * Because the HW ECC signals an ECC error if an erase paged has even a single
 * bitflip, we must check each ECC error to see if it is actually an erased
 * page with bitflips, not a truly corrupted page.
 *
 * On a real error, return a negative error code (-EBADMSG for ECC error), and
 * buf will contain raw data.
 * Otherwise, buf gets filled with 0xffs and return the maximum number of
 * bitflips-per-ECC-sector to the caller.
 *
 */
static int brcmstb_nand_verify_erased_page(struct mtd_info *mtd,
		  struct nand_chip *chip, void *buf, u64 addr)
{
	struct mtd_oob_region ecc;
	int i;
	int bitflips = 0;
	int page = addr >> chip->page_shift;
	int ret;
	void *ecc_bytes;
	void *ecc_chunk;

	if (!buf)
		buf = nand_get_data_buf(chip);

	/* read without ecc for verification */
	ret = chip->ecc.read_page_raw(chip, buf, true, page);
	if (ret)
		return ret;

	for (i = 0; i < chip->ecc.steps; i++) {
		ecc_chunk = buf + chip->ecc.size * i;

		mtd_ooblayout_ecc(mtd, i, &ecc);
		ecc_bytes = chip->oob_poi + ecc.offset;

		ret = nand_check_erased_ecc_chunk(ecc_chunk, chip->ecc.size,
						  ecc_bytes, ecc.length,
						  NULL, 0,
						  chip->ecc.strength);
		if (ret < 0)
			return ret;

		bitflips = max(bitflips, ret);
	}

	return bitflips;
}

static int brcmnand_read(struct mtd_info *mtd, struct nand_chip *chip,
			 u64 addr, unsigned int trans, u32 *buf, u8 *oob)
{
	struct brcmnand_host *host = nand_get_controller_data(chip);
	struct brcmnand_controller *ctrl = host->ctrl;
	u64 err_addr = 0;
	int err;
	bool retry = true;
	bool edu_err = false;

	dev_dbg(ctrl->dev, "read %llx -> %p\n", (unsigned long long)addr, buf);

try_dmaread:
	brcmnand_clear_ecc_addr(ctrl);

	if (ctrl->dma_trans && (has_edu(ctrl) || !oob) &&
	    flash_dma_buf_ok(buf)) {
		err = ctrl->dma_trans(host, addr, buf, oob,
				      trans * FC_BYTES,
				      CMD_PAGE_READ);

		if (err) {
			if (mtd_is_bitflip_or_eccerr(err))
				err_addr = addr;
			else
				return -EIO;
		}

		if (has_edu(ctrl) && err_addr)
			edu_err = true;

	} else {
		if (oob)
			memset(oob, 0x99, mtd->oobsize);

		err = brcmnand_read_by_pio(mtd, chip, addr, trans, buf,
					       oob, &err_addr);
	}

	if (mtd_is_eccerr(err)) {
		/*
		 * On controller version and 7.0, 7.1 , DMA read after a
		 * prior PIO read that reported uncorrectable error,
		 * the DMA engine captures this error following DMA read
		 * cleared only on subsequent DMA read, so just retry once
		 * to clear a possible false error reported for current DMA
		 * read
		 */
		if ((ctrl->nand_version == 0x0700) ||
		    (ctrl->nand_version == 0x0701)) {
			if (retry) {
				retry = false;
				goto try_dmaread;
			}
		}

		/*
		 * Controller version 7.2 has hw encoder to detect erased page
		 * bitflips, apply sw verification for older controllers only
		 */
		if (ctrl->nand_version < 0x0702) {
			err = brcmstb_nand_verify_erased_page(mtd, chip, buf,
							      addr);
			/* erased page bitflips corrected */
			if (err >= 0)
				return err;
		}

		dev_err(ctrl->dev, "uncorrectable error at 0x%llx\n",
			(unsigned long long)err_addr);
		mtd->ecc_stats.failed++;
		/* NAND layer expects zero on ECC errors */
		return 0;
	}

	if (mtd_is_bitflip(err)) {
		unsigned int corrected = brcmnand_count_corrected(ctrl);

		/* in case of EDU correctable error we read again using PIO */
		if (edu_err)
			err = brcmnand_read_by_pio(mtd, chip, addr, trans, buf,
						   oob, &err_addr);

		dev_dbg(ctrl->dev, "corrected error at 0x%llx\n",
			(unsigned long long)err_addr);
		mtd->ecc_stats.corrected += corrected;
		/* Always exceed the software-imposed threshold */
		return max(mtd->bitflip_threshold, corrected);
	}

	return 0;
}

static int brcmnand_read_page(struct nand_chip *chip, uint8_t *buf,
			      int oob_required, int page)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct brcmnand_host *host = nand_get_controller_data(chip);
	u8 *oob = oob_required ? (u8 *)chip->oob_poi : NULL;
	u64 addr = (u64)page << chip->page_shift;

	host->last_addr = addr;

	return brcmnand_read(mtd, chip, host->last_addr,
			mtd->writesize >> FC_SHIFT, (u32 *)buf, oob);
}

static int brcmnand_read_page_raw(struct nand_chip *chip, uint8_t *buf,
				  int oob_required, int page)
{
	struct brcmnand_host *host = nand_get_controller_data(chip);
	struct mtd_info *mtd = nand_to_mtd(chip);
	u8 *oob = oob_required ? (u8 *)chip->oob_poi : NULL;
	int ret;
	u64 addr = (u64)page << chip->page_shift;

	host->last_addr = addr;

	brcmnand_set_ecc_enabled(host, 0);
	ret = brcmnand_read(mtd, chip, host->last_addr,
			mtd->writesize >> FC_SHIFT, (u32 *)buf, oob);
	brcmnand_set_ecc_enabled(host, 1);
	return ret;
}

static int brcmnand_read_oob(struct nand_chip *chip, int page)
{
	struct mtd_info *mtd = nand_to_mtd(chip);

	return brcmnand_read(mtd, chip, (u64)page << chip->page_shift,
			mtd->writesize >> FC_SHIFT,
			NULL, (u8 *)chip->oob_poi);
}

static int brcmnand_read_oob_raw(struct nand_chip *chip, int page)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct brcmnand_host *host = nand_get_controller_data(chip);

	brcmnand_set_ecc_enabled(host, 0);
	brcmnand_read(mtd, chip, (u64)page << chip->page_shift,
		mtd->writesize >> FC_SHIFT,
		NULL, (u8 *)chip->oob_poi);
	brcmnand_set_ecc_enabled(host, 1);
	return 0;
}

static int brcmnand_write(struct mtd_info *mtd, struct nand_chip *chip,
			  u64 addr, const u32 *buf, u8 *oob)
{
	struct brcmnand_host *host = nand_get_controller_data(chip);
	struct brcmnand_controller *ctrl = host->ctrl;
	unsigned int i, j, trans = mtd->writesize >> FC_SHIFT;
	int status, ret = 0;

	dev_dbg(ctrl->dev, "write %llx <- %p\n", (unsigned long long)addr, buf);

	if (unlikely((unsigned long)buf & 0x03)) {
		dev_warn(ctrl->dev, "unaligned buffer: %p\n", buf);
		buf = (u32 *)((unsigned long)buf & ~0x03);
	}

	brcmnand_wp(mtd, 0);

	for (i = 0; i < ctrl->max_oob; i += 4)
		oob_reg_write(ctrl, i, 0xffffffff);

	if (mtd->oops_panic_write)
		/* switch to interrupt polling and PIO mode */
		disable_ctrl_irqs(ctrl);

	if (use_dma(ctrl) && (has_edu(ctrl) || !oob) && flash_dma_buf_ok(buf)) {
		if (ctrl->dma_trans(host, addr, (u32 *)buf, oob, mtd->writesize,
				    CMD_PROGRAM_PAGE))

			ret = -EIO;

		goto out;
	}

	for (i = 0; i < trans; i++, addr += FC_BYTES) {
		/* full address MUST be set before populating FC */
		brcmnand_set_cmd_addr(mtd, addr);

		if (buf) {
			brcmnand_soc_data_bus_prepare(ctrl->soc, false);

			for (j = 0; j < FC_WORDS; j++, buf++)
				brcmnand_write_fc(ctrl, j, *buf);

			brcmnand_soc_data_bus_unprepare(ctrl->soc, false);
		} else if (oob) {
			for (j = 0; j < FC_WORDS; j++)
				brcmnand_write_fc(ctrl, j, 0xffffffff);
		}

		if (oob) {
			oob += write_oob_to_regs(ctrl, i, oob,
					mtd->oobsize / trans,
					host->hwcfg.sector_size_1k);
		}

		/* we cannot use SPARE_AREA_PROGRAM when PARTIAL_PAGE_EN=0 */
		brcmnand_send_cmd(host, CMD_PROGRAM_PAGE);
		status = brcmnand_waitfunc(chip);

		if (status & NAND_STATUS_FAIL) {
			dev_info(ctrl->dev, "program failed at %llx\n",
				(unsigned long long)addr);
			ret = -EIO;
			goto out;
		}
	}
out:
	brcmnand_wp(mtd, 1);
	return ret;
}

static int brcmnand_write_page(struct nand_chip *chip, const uint8_t *buf,
			       int oob_required, int page)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct brcmnand_host *host = nand_get_controller_data(chip);
	void *oob = oob_required ? chip->oob_poi : NULL;
	u64 addr = (u64)page << chip->page_shift;

	host->last_addr = addr;

	return brcmnand_write(mtd, chip, host->last_addr, (const u32 *)buf, oob);
}

static int brcmnand_write_page_raw(struct nand_chip *chip, const uint8_t *buf,
				   int oob_required, int page)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct brcmnand_host *host = nand_get_controller_data(chip);
	void *oob = oob_required ? chip->oob_poi : NULL;
	u64 addr = (u64)page << chip->page_shift;
	int ret = 0;

	host->last_addr = addr;
	brcmnand_set_ecc_enabled(host, 0);
	ret = brcmnand_write(mtd, chip, host->last_addr, (const u32 *)buf, oob);
	brcmnand_set_ecc_enabled(host, 1);

	return ret;
}

static int brcmnand_write_oob(struct nand_chip *chip, int page)
{
	return brcmnand_write(nand_to_mtd(chip), chip,
			      (u64)page << chip->page_shift, NULL,
			      chip->oob_poi);
}

static int brcmnand_write_oob_raw(struct nand_chip *chip, int page)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct brcmnand_host *host = nand_get_controller_data(chip);
	int ret;

	brcmnand_set_ecc_enabled(host, 0);
	ret = brcmnand_write(mtd, chip, (u64)page << chip->page_shift, NULL,
				 (u8 *)chip->oob_poi);
	brcmnand_set_ecc_enabled(host, 1);

	return ret;
}

static int brcmnand_exec_instr(struct brcmnand_host *host, int i,
		const struct nand_operation *op)
{
	const struct nand_op_instr *instr = &op->instrs[i];
	struct brcmnand_controller *ctrl = host->ctrl;
	const u8 *out;
	bool last_op;
	int ret = 0;
	u8 *in;

	/*
	 * The controller needs to be aware of the last command in the operation
	 * (WAITRDY excepted).
	 */
	last_op = ((i == (op->ninstrs - 1)) && (instr->type != NAND_OP_WAITRDY_INSTR)) ||
		  ((i == (op->ninstrs - 2)) && (op->instrs[i + 1].type == NAND_OP_WAITRDY_INSTR));

	switch (instr->type) {
	case NAND_OP_CMD_INSTR:
		brcmnand_low_level_op(host, LL_OP_CMD, instr->ctx.cmd.opcode, last_op);
		break;

	case NAND_OP_ADDR_INSTR:
		for (i = 0; i < instr->ctx.addr.naddrs; i++)
			brcmnand_low_level_op(host, LL_OP_ADDR, instr->ctx.addr.addrs[i],
					      last_op && (i == (instr->ctx.addr.naddrs - 1)));
		break;

	case NAND_OP_DATA_IN_INSTR:
		in = instr->ctx.data.buf.in;
		for (i = 0; i < instr->ctx.data.len; i++) {
			brcmnand_low_level_op(host, LL_OP_RD, 0,
					      last_op && (i == (instr->ctx.data.len - 1)));
			in[i] = brcmnand_read_reg(host->ctrl, BRCMNAND_LL_RDATA);
		}
		break;

	case NAND_OP_DATA_OUT_INSTR:
		out = instr->ctx.data.buf.out;
		for (i = 0; i < instr->ctx.data.len; i++)
			brcmnand_low_level_op(host, LL_OP_WR, out[i],
					      last_op && (i == (instr->ctx.data.len - 1)));
		break;

	case NAND_OP_WAITRDY_INSTR:
		ret = bcmnand_ctrl_poll_status(host, NAND_CTRL_RDY, NAND_CTRL_RDY, 0);
		break;

	default:
		dev_err(ctrl->dev, "unsupported instruction type: %d\n",
			instr->type);
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int brcmnand_op_is_status(const struct nand_operation *op)
{
	if (op->ninstrs == 2 &&
	    op->instrs[0].type == NAND_OP_CMD_INSTR &&
	    op->instrs[0].ctx.cmd.opcode == NAND_CMD_STATUS &&
	    op->instrs[1].type == NAND_OP_DATA_IN_INSTR)
		return 1;

	return 0;
}

static int brcmnand_op_is_reset(const struct nand_operation *op)
{
	if (op->ninstrs == 2 &&
	    op->instrs[0].type == NAND_OP_CMD_INSTR &&
	    op->instrs[0].ctx.cmd.opcode == NAND_CMD_RESET &&
	    op->instrs[1].type == NAND_OP_WAITRDY_INSTR)
		return 1;

	return 0;
}

static int brcmnand_exec_op(struct nand_chip *chip,
			    const struct nand_operation *op,
			    bool check_only)
{
	struct brcmnand_host *host = nand_get_controller_data(chip);
	struct mtd_info *mtd = nand_to_mtd(chip);
	u8 *status;
	unsigned int i;
	int ret = 0;

	if (check_only)
		return 0;

	if (brcmnand_op_is_status(op)) {
		status = op->instrs[1].ctx.data.buf.in;
		ret = brcmnand_status(host);
		if (ret < 0)
			return ret;

		*status = ret & 0xFF;

		return 0;
	} else if (brcmnand_op_is_reset(op)) {
		ret = brcmnand_reset(host);
		if (ret < 0)
			return ret;

		brcmnand_wp(mtd, 1);

		return 0;
	}

	if (op->deassert_wp)
		brcmnand_wp(mtd, 0);

	for (i = 0; i < op->ninstrs; i++) {
		ret = brcmnand_exec_instr(host, i, op);
		if (ret)
			break;
	}

	if (op->deassert_wp)
		brcmnand_wp(mtd, 1);

	return ret;
}

/***********************************************************************
 * Per-CS setup (1 NAND device)
 ***********************************************************************/

static int brcmnand_set_cfg(struct brcmnand_host *host,
			    struct brcmnand_cfg *cfg)
{
	struct brcmnand_controller *ctrl = host->ctrl;
	struct nand_chip *chip = &host->chip;
	u16 cfg_offs = brcmnand_cs_offset(ctrl, host->cs, BRCMNAND_CS_CFG);
	u16 cfg_ext_offs = brcmnand_cs_offset(ctrl, host->cs,
			BRCMNAND_CS_CFG_EXT);
	u16 acc_control_offs = brcmnand_cs_offset(ctrl, host->cs,
			BRCMNAND_CS_ACC_CONTROL);
	u8 block_size = 0, page_size = 0, device_size = 0;
	u32 tmp;

	if (ctrl->block_sizes) {
		int i, found;

		for (i = 0, found = 0; ctrl->block_sizes[i]; i++)
			if (ctrl->block_sizes[i] * 1024 == cfg->block_size) {
				block_size = i;
				found = 1;
			}
		if (!found) {
			dev_warn(ctrl->dev, "invalid block size %u\n",
					cfg->block_size);
			return -EINVAL;
		}
	} else {
		block_size = ffs(cfg->block_size) - ffs(BRCMNAND_MIN_BLOCKSIZE);
	}

	if (cfg->block_size < BRCMNAND_MIN_BLOCKSIZE || (ctrl->max_block_size &&
				cfg->block_size > ctrl->max_block_size)) {
		dev_warn(ctrl->dev, "invalid block size %u\n",
				cfg->block_size);
		block_size = 0;
	}

	if (ctrl->page_sizes) {
		int i, found;

		for (i = 0, found = 0; ctrl->page_sizes[i]; i++)
			if (ctrl->page_sizes[i] == cfg->page_size) {
				page_size = i;
				found = 1;
			}
		if (!found) {
			dev_warn(ctrl->dev, "invalid page size %u\n",
					cfg->page_size);
			return -EINVAL;
		}
	} else {
		page_size = ffs(cfg->page_size) - ffs(BRCMNAND_MIN_PAGESIZE);
	}

	if (cfg->page_size < BRCMNAND_MIN_PAGESIZE || (ctrl->max_page_size &&
				cfg->page_size > ctrl->max_page_size)) {
		dev_warn(ctrl->dev, "invalid page size %u\n", cfg->page_size);
		return -EINVAL;
	}

	if (fls64(cfg->device_size) < fls64(BRCMNAND_MIN_DEVSIZE)) {
		dev_warn(ctrl->dev, "invalid device size 0x%llx\n",
			(unsigned long long)cfg->device_size);
		return -EINVAL;
	}
	device_size = fls64(cfg->device_size) - fls64(BRCMNAND_MIN_DEVSIZE);

	tmp = (cfg->blk_adr_bytes << CFG_BLK_ADR_BYTES_SHIFT) |
		(cfg->col_adr_bytes << CFG_COL_ADR_BYTES_SHIFT) |
		(cfg->ful_adr_bytes << CFG_FUL_ADR_BYTES_SHIFT) |
		(!!(cfg->device_width == 16) << CFG_BUS_WIDTH_SHIFT) |
		(device_size << CFG_DEVICE_SIZE_SHIFT);
	if (cfg_offs == cfg_ext_offs) {
		tmp |= (page_size << ctrl->page_size_shift) |
		       (block_size << CFG_BLK_SIZE_SHIFT);
		nand_writereg(ctrl, cfg_offs, tmp);
	} else {
		nand_writereg(ctrl, cfg_offs, tmp);
		tmp = (page_size << CFG_EXT_PAGE_SIZE_SHIFT) |
		      (block_size << CFG_EXT_BLK_SIZE_SHIFT);
		nand_writereg(ctrl, cfg_ext_offs, tmp);
	}

	tmp = nand_readreg(ctrl, acc_control_offs);
	tmp &= ~brcmnand_ecc_level_mask(ctrl);
	tmp &= ~brcmnand_spare_area_mask(ctrl);
	if (ctrl->nand_version >= 0x0302) {
		tmp |= cfg->ecc_level << ctrl->ecc_level_shift;
		tmp |= cfg->spare_area_size;
	}
	nand_writereg(ctrl, acc_control_offs, tmp);

	brcmnand_set_sector_size_1k(host, cfg->sector_size_1k);

	/* threshold = ceil(BCH-level * 0.75) */
	brcmnand_wr_corr_thresh(host, DIV_ROUND_UP(chip->ecc.strength * 3, 4));

	return 0;
}

static void brcmnand_print_cfg(struct brcmnand_host *host,
			       char *buf, struct brcmnand_cfg *cfg)
{
	buf += sprintf(buf,
		"%lluMiB total, %uKiB blocks, %u%s pages, %uB OOB, %u-bit",
		(unsigned long long)cfg->device_size >> 20,
		cfg->block_size >> 10,
		cfg->page_size >= 1024 ? cfg->page_size >> 10 : cfg->page_size,
		cfg->page_size >= 1024 ? "KiB" : "B",
		cfg->spare_area_size, cfg->device_width);

	/* Account for Hamming ECC and for BCH 512B vs 1KiB sectors */
	if (is_hamming_ecc(host->ctrl, cfg))
		sprintf(buf, ", Hamming ECC");
	else if (cfg->sector_size_1k)
		sprintf(buf, ", BCH-%u (1KiB sector)", cfg->ecc_level << 1);
	else
		sprintf(buf, ", BCH-%u", cfg->ecc_level);
}

/*
 * Minimum number of bytes to address a page. Calculated as:
 *     roundup(log2(size / page-size) / 8)
 *
 * NB: the following does not "round up" for non-power-of-2 'size'; but this is
 *     OK because many other things will break if 'size' is irregular...
 */
static inline int get_blk_adr_bytes(u64 size, u32 writesize)
{
	return ALIGN(ilog2(size) - ilog2(writesize), 8) >> 3;
}

static int brcmnand_setup_dev(struct brcmnand_host *host)
{
	struct mtd_info *mtd = nand_to_mtd(&host->chip);
	struct nand_chip *chip = &host->chip;
	const struct nand_ecc_props *requirements =
		nanddev_get_ecc_requirements(&chip->base);
	struct nand_memory_organization *memorg =
		nanddev_get_memorg(&chip->base);
	struct brcmnand_controller *ctrl = host->ctrl;
	struct brcmnand_cfg *cfg = &host->hwcfg;
	struct device_node *np = nand_get_flash_node(chip);
	u32 offs, tmp, oob_sector;
	bool use_strap = false;
	char msg[128];
	int ret;

	memset(cfg, 0, sizeof(*cfg));
	use_strap = of_property_read_bool(np, "brcm,nand-ecc-use-strap");

	/*
	 * Either nand-ecc-xxx or brcm,nand-ecc-use-strap can be set. Error out
	 * if both exist.
	 */
	if (chip->ecc.strength && use_strap) {
		dev_err(ctrl->dev,
			"ECC strap and DT ECC configuration properties are mutually exclusive\n");
		return -EINVAL;
	}

	if (use_strap)
		brcmnand_get_ecc_settings(host, chip);

	ret = of_property_read_u32(np, "brcm,nand-oob-sector-size",
				   &oob_sector);
	if (ret) {
		if (use_strap)
			cfg->spare_area_size = brcmnand_get_spare_size(host);
		else
			/* Use detected size */
			cfg->spare_area_size = mtd->oobsize /
						(mtd->writesize >> FC_SHIFT);
	} else {
		cfg->spare_area_size = oob_sector;
	}
	if (cfg->spare_area_size > ctrl->max_oob)
		cfg->spare_area_size = ctrl->max_oob;
	/*
	 * Set mtd and memorg oobsize to be consistent with controller's
	 * spare_area_size, as the rest is inaccessible.
	 */
	mtd->oobsize = cfg->spare_area_size * (mtd->writesize >> FC_SHIFT);
	memorg->oobsize = mtd->oobsize;

	cfg->device_size = mtd->size;
	cfg->block_size = mtd->erasesize;
	cfg->page_size = mtd->writesize;
	cfg->device_width = (chip->options & NAND_BUSWIDTH_16) ? 16 : 8;
	cfg->col_adr_bytes = 2;
	cfg->blk_adr_bytes = get_blk_adr_bytes(mtd->size, mtd->writesize);

	if (chip->ecc.engine_type != NAND_ECC_ENGINE_TYPE_ON_HOST) {
		dev_err(ctrl->dev, "only HW ECC supported; selected: %d\n",
			chip->ecc.engine_type);
		return -EINVAL;
	}

	if (chip->ecc.algo == NAND_ECC_ALGO_UNKNOWN) {
		if (chip->ecc.strength == 1 && chip->ecc.size == 512)
			/* Default to Hamming for 1-bit ECC, if unspecified */
			chip->ecc.algo = NAND_ECC_ALGO_HAMMING;
		else
			/* Otherwise, BCH */
			chip->ecc.algo = NAND_ECC_ALGO_BCH;
	}

	if (chip->ecc.algo == NAND_ECC_ALGO_HAMMING &&
	    (chip->ecc.strength != 1 || chip->ecc.size != 512)) {
		dev_err(ctrl->dev, "invalid Hamming params: %d bits per %d bytes\n",
			chip->ecc.strength, chip->ecc.size);
		return -EINVAL;
	}

	if (chip->ecc.engine_type != NAND_ECC_ENGINE_TYPE_NONE &&
	    (!chip->ecc.size || !chip->ecc.strength)) {
		if (requirements->step_size && requirements->strength) {
			/* use detected ECC parameters */
			chip->ecc.size = requirements->step_size;
			chip->ecc.strength = requirements->strength;
			dev_info(ctrl->dev, "Using ECC step-size %d, strength %d\n",
				chip->ecc.size, chip->ecc.strength);
		}
	}

	switch (chip->ecc.size) {
	case 512:
		if (chip->ecc.algo == NAND_ECC_ALGO_HAMMING)
			cfg->ecc_level = 15;
		else
			cfg->ecc_level = chip->ecc.strength;
		cfg->sector_size_1k = 0;
		break;
	case 1024:
		if (!(ctrl->features & BRCMNAND_HAS_1K_SECTORS)) {
			dev_err(ctrl->dev, "1KB sectors not supported\n");
			return -EINVAL;
		}
		if (chip->ecc.strength & 0x1) {
			dev_err(ctrl->dev,
				"odd ECC not supported with 1KB sectors\n");
			return -EINVAL;
		}

		cfg->ecc_level = chip->ecc.strength >> 1;
		cfg->sector_size_1k = 1;
		break;
	default:
		dev_err(ctrl->dev, "unsupported ECC size: %d\n",
			chip->ecc.size);
		return -EINVAL;
	}

	cfg->ful_adr_bytes = cfg->blk_adr_bytes;
	if (mtd->writesize > 512)
		cfg->ful_adr_bytes += cfg->col_adr_bytes;
	else
		cfg->ful_adr_bytes += 1;

	ret = brcmnand_set_cfg(host, cfg);
	if (ret)
		return ret;

	brcmnand_set_ecc_enabled(host, 1);

	brcmnand_print_cfg(host, msg, cfg);
	dev_info(ctrl->dev, "detected %s\n", msg);

	/* Configure ACC_CONTROL */
	offs = brcmnand_cs_offset(ctrl, host->cs, BRCMNAND_CS_ACC_CONTROL);
	tmp = nand_readreg(ctrl, offs);
	tmp &= ~ACC_CONTROL_PARTIAL_PAGE;
	tmp &= ~ACC_CONTROL_RD_ERASED;

	/* We need to turn on Read from erased paged protected by ECC */
	if (ctrl->nand_version >= 0x0702)
		tmp |= ACC_CONTROL_RD_ERASED;
	tmp &= ~ACC_CONTROL_FAST_PGM_RDIN;
	if (ctrl->features & BRCMNAND_HAS_PREFETCH)
		tmp &= ~ACC_CONTROL_PREFETCH;

	nand_writereg(ctrl, offs, tmp);

	return 0;
}

static int brcmnand_attach_chip(struct nand_chip *chip)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct brcmnand_host *host = nand_get_controller_data(chip);
	int ret;

	chip->options |= NAND_NO_SUBPAGE_WRITE;
	/*
	 * Avoid (for instance) kmap()'d buffers from JFFS2, which we can't DMA
	 * to/from, and have nand_base pass us a bounce buffer instead, as
	 * needed.
	 */
	chip->options |= NAND_USES_DMA;

	if (chip->bbt_options & NAND_BBT_USE_FLASH)
		chip->bbt_options |= NAND_BBT_NO_OOB;

	if (brcmnand_setup_dev(host))
		return -ENXIO;

	chip->ecc.size = host->hwcfg.sector_size_1k ? 1024 : 512;

	/* only use our internal HW threshold */
	mtd->bitflip_threshold = 1;

	ret = brcmstb_choose_ecc_layout(host);

	/* If OOB is written with ECC enabled it will cause ECC errors */
	if (is_hamming_ecc(host->ctrl, &host->hwcfg)) {
		chip->ecc.write_oob = brcmnand_write_oob_raw;
		chip->ecc.read_oob = brcmnand_read_oob_raw;
	}

	return ret;
}

static const struct nand_controller_ops brcmnand_controller_ops = {
	.attach_chip = brcmnand_attach_chip,
	.exec_op = brcmnand_exec_op,
};

static int brcmnand_init_cs(struct brcmnand_host *host,
			    const char * const *part_probe_types)
{
	struct brcmnand_controller *ctrl = host->ctrl;
	struct device *dev = ctrl->dev;
	struct mtd_info *mtd;
	struct nand_chip *chip;
	int ret;
	u16 cfg_offs;

	mtd = nand_to_mtd(&host->chip);
	chip = &host->chip;

	nand_set_controller_data(chip, host);
	mtd->name = devm_kasprintf(dev, GFP_KERNEL, "brcmnand.%d",
				   host->cs);
	if (!mtd->name)
		return -ENOMEM;

	mtd->owner = THIS_MODULE;
	mtd->dev.parent = dev;

	chip->ecc.engine_type = NAND_ECC_ENGINE_TYPE_ON_HOST;
	chip->ecc.read_page = brcmnand_read_page;
	chip->ecc.write_page = brcmnand_write_page;
	chip->ecc.read_page_raw = brcmnand_read_page_raw;
	chip->ecc.write_page_raw = brcmnand_write_page_raw;
	chip->ecc.write_oob_raw = brcmnand_write_oob_raw;
	chip->ecc.read_oob_raw = brcmnand_read_oob_raw;
	chip->ecc.read_oob = brcmnand_read_oob;
	chip->ecc.write_oob = brcmnand_write_oob;

	chip->controller = &ctrl->controller;
	ctrl->controller.controller_wp = 1;

	/*
	 * The bootloader might have configured 16bit mode but
	 * NAND READID command only works in 8bit mode. We force
	 * 8bit mode here to ensure that NAND READID commands works.
	 */
	cfg_offs = brcmnand_cs_offset(ctrl, host->cs, BRCMNAND_CS_CFG);
	nand_writereg(ctrl, cfg_offs,
		      nand_readreg(ctrl, cfg_offs) & ~CFG_BUS_WIDTH);

	ret = nand_scan(chip, 1);
	if (ret)
		return ret;

	ret = mtd_device_parse_register(mtd, part_probe_types, NULL, NULL, 0);
	if (ret)
		nand_cleanup(chip);

	return ret;
}

static void brcmnand_save_restore_cs_config(struct brcmnand_host *host,
					    int restore)
{
	struct brcmnand_controller *ctrl = host->ctrl;
	u16 cfg_offs = brcmnand_cs_offset(ctrl, host->cs, BRCMNAND_CS_CFG);
	u16 cfg_ext_offs = brcmnand_cs_offset(ctrl, host->cs,
			BRCMNAND_CS_CFG_EXT);
	u16 acc_control_offs = brcmnand_cs_offset(ctrl, host->cs,
			BRCMNAND_CS_ACC_CONTROL);
	u16 t1_offs = brcmnand_cs_offset(ctrl, host->cs, BRCMNAND_CS_TIMING1);
	u16 t2_offs = brcmnand_cs_offset(ctrl, host->cs, BRCMNAND_CS_TIMING2);

	if (restore) {
		nand_writereg(ctrl, cfg_offs, host->hwcfg.config);
		if (cfg_offs != cfg_ext_offs)
			nand_writereg(ctrl, cfg_ext_offs,
				      host->hwcfg.config_ext);
		nand_writereg(ctrl, acc_control_offs, host->hwcfg.acc_control);
		nand_writereg(ctrl, t1_offs, host->hwcfg.timing_1);
		nand_writereg(ctrl, t2_offs, host->hwcfg.timing_2);
	} else {
		host->hwcfg.config = nand_readreg(ctrl, cfg_offs);
		if (cfg_offs != cfg_ext_offs)
			host->hwcfg.config_ext =
				nand_readreg(ctrl, cfg_ext_offs);
		host->hwcfg.acc_control = nand_readreg(ctrl, acc_control_offs);
		host->hwcfg.timing_1 = nand_readreg(ctrl, t1_offs);
		host->hwcfg.timing_2 = nand_readreg(ctrl, t2_offs);
	}
}

static int brcmnand_suspend(struct device *dev)
{
	struct brcmnand_controller *ctrl = dev_get_drvdata(dev);
	struct brcmnand_host *host;

	list_for_each_entry(host, &ctrl->host_list, node)
		brcmnand_save_restore_cs_config(host, 0);

	ctrl->nand_cs_nand_select = brcmnand_read_reg(ctrl, BRCMNAND_CS_SELECT);
	ctrl->nand_cs_nand_xor = brcmnand_read_reg(ctrl, BRCMNAND_CS_XOR);
	ctrl->corr_stat_threshold =
		brcmnand_read_reg(ctrl, BRCMNAND_CORR_THRESHOLD);

	if (has_flash_dma(ctrl))
		ctrl->flash_dma_mode = flash_dma_readl(ctrl, FLASH_DMA_MODE);
	else if (has_edu(ctrl))
		ctrl->edu_config = edu_readl(ctrl, EDU_CONFIG);

	return 0;
}

static int brcmnand_resume(struct device *dev)
{
	struct brcmnand_controller *ctrl = dev_get_drvdata(dev);
	struct brcmnand_host *host;

	if (has_flash_dma(ctrl)) {
		flash_dma_writel(ctrl, FLASH_DMA_MODE, ctrl->flash_dma_mode);
		flash_dma_writel(ctrl, FLASH_DMA_ERROR_STATUS, 0);
	}

	if (has_edu(ctrl)) {
		ctrl->edu_config = edu_readl(ctrl, EDU_CONFIG);
		edu_writel(ctrl, EDU_CONFIG, ctrl->edu_config);
		edu_readl(ctrl, EDU_CONFIG);
		brcmnand_edu_init(ctrl);
	}

	brcmnand_write_reg(ctrl, BRCMNAND_CS_SELECT, ctrl->nand_cs_nand_select);
	brcmnand_write_reg(ctrl, BRCMNAND_CS_XOR, ctrl->nand_cs_nand_xor);
	brcmnand_write_reg(ctrl, BRCMNAND_CORR_THRESHOLD,
			ctrl->corr_stat_threshold);
	if (ctrl->soc) {
		/* Clear/re-enable interrupt */
		ctrl->soc->ctlrdy_ack(ctrl->soc);
		ctrl->soc->ctlrdy_set_enabled(ctrl->soc, true);
	}

	list_for_each_entry(host, &ctrl->host_list, node) {
		struct nand_chip *chip = &host->chip;

		brcmnand_save_restore_cs_config(host, 1);

		/* Reset the chip, required by some chips after power-up */
		nand_reset_op(chip);
	}

	return 0;
}

const struct dev_pm_ops brcmnand_pm_ops = {
	.suspend		= brcmnand_suspend,
	.resume			= brcmnand_resume,
};
EXPORT_SYMBOL_GPL(brcmnand_pm_ops);

static const struct of_device_id __maybe_unused brcmnand_of_match[] = {
	{ .compatible = "brcm,brcmnand-v2.1" },
	{ .compatible = "brcm,brcmnand-v2.2" },
	{ .compatible = "brcm,brcmnand-v4.0" },
	{ .compatible = "brcm,brcmnand-v5.0" },
	{ .compatible = "brcm,brcmnand-v6.0" },
	{ .compatible = "brcm,brcmnand-v6.1" },
	{ .compatible = "brcm,brcmnand-v6.2" },
	{ .compatible = "brcm,brcmnand-v7.0" },
	{ .compatible = "brcm,brcmnand-v7.1" },
	{ .compatible = "brcm,brcmnand-v7.2" },
	{ .compatible = "brcm,brcmnand-v7.3" },
	{},
};
MODULE_DEVICE_TABLE(of, brcmnand_of_match);

/***********************************************************************
 * Platform driver setup (per controller)
 ***********************************************************************/
static int brcmnand_edu_setup(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct brcmnand_controller *ctrl = dev_get_drvdata(&pdev->dev);
	struct resource *res;
	int ret;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "flash-edu");
	if (res) {
		ctrl->edu_base = devm_ioremap_resource(dev, res);
		if (IS_ERR(ctrl->edu_base))
			return PTR_ERR(ctrl->edu_base);

		ctrl->edu_offsets = edu_regs;

		edu_writel(ctrl, EDU_CONFIG, EDU_CONFIG_MODE_NAND |
			   EDU_CONFIG_SWAP_CFG);
		edu_readl(ctrl, EDU_CONFIG);

		/* initialize edu */
		brcmnand_edu_init(ctrl);

		ctrl->edu_irq = platform_get_irq_optional(pdev, 1);
		if (ctrl->edu_irq < 0) {
			dev_warn(dev,
				 "FLASH EDU enabled, using ctlrdy irq\n");
		} else {
			ret = devm_request_irq(dev, ctrl->edu_irq,
					       brcmnand_edu_irq, 0,
					       "brcmnand-edu", ctrl);
			if (ret < 0) {
				dev_err(ctrl->dev, "can't allocate IRQ %d: error %d\n",
					ctrl->edu_irq, ret);
				return ret;
			}

			dev_info(dev, "FLASH EDU enabled using irq %u\n",
				 ctrl->edu_irq);
		}
	}

	return 0;
}

int brcmnand_probe(struct platform_device *pdev, struct brcmnand_soc *soc)
{
	struct brcmnand_platform_data *pd = dev_get_platdata(&pdev->dev);
	struct device *dev = &pdev->dev;
	struct device_node *dn = dev->of_node, *child;
	struct brcmnand_controller *ctrl;
	struct brcmnand_host *host;
	struct resource *res;
	int ret;

	if (dn && !of_match_node(brcmnand_of_match, dn))
		return -ENODEV;

	ctrl = devm_kzalloc(dev, sizeof(*ctrl), GFP_KERNEL);
	if (!ctrl)
		return -ENOMEM;

	dev_set_drvdata(dev, ctrl);
	ctrl->dev = dev;
	ctrl->soc = soc;

	/* Enable the static key if the soc provides I/O operations indicating
	 * that a non-memory mapped IO access path must be used
	 */
	if (brcmnand_soc_has_ops(ctrl->soc))
		static_branch_enable(&brcmnand_soc_has_ops_key);

	init_completion(&ctrl->done);
	init_completion(&ctrl->dma_done);
	init_completion(&ctrl->edu_done);
	nand_controller_init(&ctrl->controller);
	ctrl->controller.ops = &brcmnand_controller_ops;
	INIT_LIST_HEAD(&ctrl->host_list);

	/* NAND register range */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	ctrl->nand_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(ctrl->nand_base) && !brcmnand_soc_has_ops(soc))
		return PTR_ERR(ctrl->nand_base);

	/* Enable clock before using NAND registers */
	ctrl->clk = devm_clk_get(dev, "nand");
	if (!IS_ERR(ctrl->clk)) {
		ret = clk_prepare_enable(ctrl->clk);
		if (ret)
			return ret;
	} else {
		ret = PTR_ERR(ctrl->clk);
		if (ret == -EPROBE_DEFER)
			return ret;

		ctrl->clk = NULL;
	}

	/* Initialize NAND revision */
	ret = brcmnand_revision_init(ctrl);
	if (ret)
		goto err;

	/*
	 * Most chips have this cache at a fixed offset within 'nand' block.
	 * Some must specify this region separately.
	 */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "nand-cache");
	if (res) {
		ctrl->nand_fc = devm_ioremap_resource(dev, res);
		if (IS_ERR(ctrl->nand_fc)) {
			ret = PTR_ERR(ctrl->nand_fc);
			goto err;
		}
	} else {
		ctrl->nand_fc = ctrl->nand_base +
				ctrl->reg_offsets[BRCMNAND_FC_BASE];
	}

	/* FLASH_DMA */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "flash-dma");
	if (res) {
		ctrl->flash_dma_base = devm_ioremap_resource(dev, res);
		if (IS_ERR(ctrl->flash_dma_base)) {
			ret = PTR_ERR(ctrl->flash_dma_base);
			goto err;
		}

		/* initialize the dma version */
		brcmnand_flash_dma_revision_init(ctrl);

		ret = -EIO;
		if (ctrl->nand_version >= 0x0700)
			ret = dma_set_mask_and_coherent(&pdev->dev,
							DMA_BIT_MASK(40));
		if (ret)
			ret = dma_set_mask_and_coherent(&pdev->dev,
							DMA_BIT_MASK(32));
		if (ret)
			goto err;

		/* linked-list and stop on error */
		flash_dma_writel(ctrl, FLASH_DMA_MODE, FLASH_DMA_MODE_MASK);
		flash_dma_writel(ctrl, FLASH_DMA_ERROR_STATUS, 0);

		/* Allocate descriptor(s) */
		ctrl->dma_desc = dmam_alloc_coherent(dev,
						     sizeof(*ctrl->dma_desc),
						     &ctrl->dma_pa, GFP_KERNEL);
		if (!ctrl->dma_desc) {
			ret = -ENOMEM;
			goto err;
		}

		ctrl->dma_irq = platform_get_irq(pdev, 1);
		if ((int)ctrl->dma_irq < 0) {
			dev_err(dev, "missing FLASH_DMA IRQ\n");
			ret = -ENODEV;
			goto err;
		}

		ret = devm_request_irq(dev, ctrl->dma_irq,
				brcmnand_dma_irq, 0, DRV_NAME,
				ctrl);
		if (ret < 0) {
			dev_err(dev, "can't allocate IRQ %d: error %d\n",
					ctrl->dma_irq, ret);
			goto err;
		}

		dev_info(dev, "enabling FLASH_DMA\n");
		/* set flash dma transfer function to call */
		ctrl->dma_trans = brcmnand_dma_trans;
	} else	{
		ret = brcmnand_edu_setup(pdev);
		if (ret < 0)
			goto err;

		if (has_edu(ctrl))
			/* set edu transfer function to call */
			ctrl->dma_trans = brcmnand_edu_trans;
	}

	/* Disable automatic device ID config, direct addressing */
	brcmnand_rmw_reg(ctrl, BRCMNAND_CS_SELECT,
			 CS_SELECT_AUTO_DEVICE_ID_CFG | 0xff, 0, 0);
	/* Disable XOR addressing */
	brcmnand_rmw_reg(ctrl, BRCMNAND_CS_XOR, 0xff, 0, 0);

	/* Check if the board connects the WP pin */
	if (of_property_read_bool(dn, "brcm,wp-not-connected"))
		wp_on = 0;

	if (ctrl->features & BRCMNAND_HAS_WP) {
		/* Permanently disable write protection */
		if (wp_on == 2)
			brcmnand_set_wp(ctrl, false);
	} else {
		wp_on = 0;
	}

	/* IRQ */
	ctrl->irq = platform_get_irq_optional(pdev, 0);
	if (ctrl->irq > 0) {
		/*
		 * Some SoCs integrate this controller (e.g., its interrupt bits) in
		 * interesting ways
		 */
		if (soc) {
			ret = devm_request_irq(dev, ctrl->irq, brcmnand_irq, 0,
					       DRV_NAME, ctrl);

			/* Enable interrupt */
			ctrl->soc->ctlrdy_ack(ctrl->soc);
			ctrl->soc->ctlrdy_set_enabled(ctrl->soc, true);
		} else {
			/* Use standard interrupt infrastructure */
			ret = devm_request_irq(dev, ctrl->irq, brcmnand_ctlrdy_irq, 0,
					       DRV_NAME, ctrl);
		}
		if (ret < 0) {
			dev_err(dev, "can't allocate IRQ %d: error %d\n",
				ctrl->irq, ret);
			goto err;
		}
	}

	for_each_available_child_of_node(dn, child) {
		if (of_device_is_compatible(child, "brcm,nandcs")) {

			host = devm_kzalloc(dev, sizeof(*host), GFP_KERNEL);
			if (!host) {
				of_node_put(child);
				ret = -ENOMEM;
				goto err;
			}
			host->pdev = pdev;
			host->ctrl = ctrl;

			ret = of_property_read_u32(child, "reg", &host->cs);
			if (ret) {
				dev_err(dev, "can't get chip-select\n");
				devm_kfree(dev, host);
				continue;
			}

			nand_set_flash_node(&host->chip, child);

			ret = brcmnand_init_cs(host, NULL);
			if (ret) {
				if (ret == -EPROBE_DEFER) {
					of_node_put(child);
					goto err;
				}
				devm_kfree(dev, host);
				continue; /* Try all chip-selects */
			}

			list_add_tail(&host->node, &ctrl->host_list);
		}
	}

	if (!list_empty(&ctrl->host_list))
		return 0;

	if (!pd) {
		ret = -ENODEV;
		goto err;
	}

	/* If we got there we must have been probing via platform data */
	host = devm_kzalloc(dev, sizeof(*host), GFP_KERNEL);
	if (!host) {
		ret = -ENOMEM;
		goto err;
	}
	host->pdev = pdev;
	host->ctrl = ctrl;
	host->cs = pd->chip_select;
	host->chip.ecc.size = pd->ecc_stepsize;
	host->chip.ecc.strength = pd->ecc_strength;

	ret = brcmnand_init_cs(host, pd->part_probe_types);
	if (ret)
		goto err;

	list_add_tail(&host->node, &ctrl->host_list);

	/* No chip-selects could initialize properly */
	if (list_empty(&ctrl->host_list)) {
		ret = -ENODEV;
		goto err;
	}

	return 0;

err:
	clk_disable_unprepare(ctrl->clk);
	return ret;

}
EXPORT_SYMBOL_GPL(brcmnand_probe);

void brcmnand_remove(struct platform_device *pdev)
{
	struct brcmnand_controller *ctrl = dev_get_drvdata(&pdev->dev);
	struct brcmnand_host *host;
	struct nand_chip *chip;
	int ret;

	list_for_each_entry(host, &ctrl->host_list, node) {
		chip = &host->chip;
		ret = mtd_device_unregister(nand_to_mtd(chip));
		WARN_ON(ret);
		nand_cleanup(chip);
	}

	clk_disable_unprepare(ctrl->clk);

	dev_set_drvdata(&pdev->dev, NULL);
}
EXPORT_SYMBOL_GPL(brcmnand_remove);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Kevin Cernekee");
MODULE_AUTHOR("Brian Norris");
MODULE_DESCRIPTION("NAND driver for Broadcom chips");
MODULE_ALIAS("platform:brcmnand");
