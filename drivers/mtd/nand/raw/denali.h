/* SPDX-License-Identifier: GPL-2.0 */
/*
 * NAND Flash Controller Device Driver
 * Copyright (c) 2009 - 2010, Intel Corporation and its suppliers.
 */

#ifndef __DENALI_H__
#define __DENALI_H__

#include <linux/bits.h>
#include <linux/completion.h>
#include <linux/list.h>
#include <linux/mtd/rawnand.h>
#include <linux/spinlock_types.h>
#include <linux/types.h>

#define DEVICE_RESET				0x0
#define     DEVICE_RESET__BANK(bank)			BIT(bank)

#define TRANSFER_SPARE_REG			0x10
#define     TRANSFER_SPARE_REG__FLAG			BIT(0)

#define LOAD_WAIT_CNT				0x20
#define     LOAD_WAIT_CNT__VALUE			GENMASK(15, 0)

#define PROGRAM_WAIT_CNT			0x30
#define     PROGRAM_WAIT_CNT__VALUE			GENMASK(15, 0)

#define ERASE_WAIT_CNT				0x40
#define     ERASE_WAIT_CNT__VALUE			GENMASK(15, 0)

#define INT_MON_CYCCNT				0x50
#define     INT_MON_CYCCNT__VALUE			GENMASK(15, 0)

#define RB_PIN_ENABLED				0x60
#define     RB_PIN_ENABLED__BANK(bank)			BIT(bank)

#define MULTIPLANE_OPERATION			0x70
#define     MULTIPLANE_OPERATION__FLAG			BIT(0)

#define MULTIPLANE_READ_ENABLE			0x80
#define     MULTIPLANE_READ_ENABLE__FLAG		BIT(0)

#define COPYBACK_DISABLE			0x90
#define     COPYBACK_DISABLE__FLAG			BIT(0)

#define CACHE_WRITE_ENABLE			0xa0
#define     CACHE_WRITE_ENABLE__FLAG			BIT(0)

#define CACHE_READ_ENABLE			0xb0
#define     CACHE_READ_ENABLE__FLAG			BIT(0)

#define PREFETCH_MODE				0xc0
#define     PREFETCH_MODE__PREFETCH_EN			BIT(0)
#define     PREFETCH_MODE__PREFETCH_BURST_LENGTH	GENMASK(15, 4)

#define CHIP_ENABLE_DONT_CARE			0xd0
#define     CHIP_EN_DONT_CARE__FLAG			BIT(0)

#define ECC_ENABLE				0xe0
#define     ECC_ENABLE__FLAG				BIT(0)

#define GLOBAL_INT_ENABLE			0xf0
#define     GLOBAL_INT_EN_FLAG				BIT(0)

#define TWHR2_AND_WE_2_RE			0x100
#define     TWHR2_AND_WE_2_RE__WE_2_RE			GENMASK(5, 0)
#define     TWHR2_AND_WE_2_RE__TWHR2			GENMASK(13, 8)

#define TCWAW_AND_ADDR_2_DATA			0x110
/* The width of ADDR_2_DATA is 6 bit for old IP, 7 bit for new IP */
#define     TCWAW_AND_ADDR_2_DATA__ADDR_2_DATA		GENMASK(6, 0)
#define     TCWAW_AND_ADDR_2_DATA__TCWAW		GENMASK(13, 8)

#define RE_2_WE					0x120
#define     RE_2_WE__VALUE				GENMASK(5, 0)

#define ACC_CLKS				0x130
#define     ACC_CLKS__VALUE				GENMASK(3, 0)

#define NUMBER_OF_PLANES			0x140
#define     NUMBER_OF_PLANES__VALUE			GENMASK(2, 0)

#define PAGES_PER_BLOCK				0x150
#define     PAGES_PER_BLOCK__VALUE			GENMASK(15, 0)

#define DEVICE_WIDTH				0x160
#define     DEVICE_WIDTH__VALUE				GENMASK(1, 0)

#define DEVICE_MAIN_AREA_SIZE			0x170
#define     DEVICE_MAIN_AREA_SIZE__VALUE		GENMASK(15, 0)

#define DEVICE_SPARE_AREA_SIZE			0x180
#define     DEVICE_SPARE_AREA_SIZE__VALUE		GENMASK(15, 0)

#define TWO_ROW_ADDR_CYCLES			0x190
#define     TWO_ROW_ADDR_CYCLES__FLAG			BIT(0)

#define MULTIPLANE_ADDR_RESTRICT		0x1a0
#define     MULTIPLANE_ADDR_RESTRICT__FLAG		BIT(0)

#define ECC_CORRECTION				0x1b0
#define     ECC_CORRECTION__VALUE			GENMASK(4, 0)
#define     ECC_CORRECTION__ERASE_THRESHOLD		GENMASK(31, 16)

#define READ_MODE				0x1c0
#define     READ_MODE__VALUE				GENMASK(3, 0)

#define WRITE_MODE				0x1d0
#define     WRITE_MODE__VALUE				GENMASK(3, 0)

#define COPYBACK_MODE				0x1e0
#define     COPYBACK_MODE__VALUE			GENMASK(3, 0)

#define RDWR_EN_LO_CNT				0x1f0
#define     RDWR_EN_LO_CNT__VALUE			GENMASK(4, 0)

#define RDWR_EN_HI_CNT				0x200
#define     RDWR_EN_HI_CNT__VALUE			GENMASK(4, 0)

#define MAX_RD_DELAY				0x210
#define     MAX_RD_DELAY__VALUE				GENMASK(3, 0)

#define CS_SETUP_CNT				0x220
#define     CS_SETUP_CNT__VALUE				GENMASK(4, 0)
#define     CS_SETUP_CNT__TWB				GENMASK(17, 12)

#define SPARE_AREA_SKIP_BYTES			0x230
#define     SPARE_AREA_SKIP_BYTES__VALUE		GENMASK(5, 0)

#define SPARE_AREA_MARKER			0x240
#define     SPARE_AREA_MARKER__VALUE			GENMASK(15, 0)

#define DEVICES_CONNECTED			0x250
#define     DEVICES_CONNECTED__VALUE			GENMASK(2, 0)

#define DIE_MASK				0x260
#define     DIE_MASK__VALUE				GENMASK(7, 0)

#define FIRST_BLOCK_OF_NEXT_PLANE		0x270
#define     FIRST_BLOCK_OF_NEXT_PLANE__VALUE		GENMASK(15, 0)

#define WRITE_PROTECT				0x280
#define     WRITE_PROTECT__FLAG				BIT(0)

#define RE_2_RE					0x290
#define     RE_2_RE__VALUE				GENMASK(5, 0)

#define MANUFACTURER_ID				0x300
#define     MANUFACTURER_ID__VALUE			GENMASK(7, 0)

#define DEVICE_ID				0x310
#define     DEVICE_ID__VALUE				GENMASK(7, 0)

#define DEVICE_PARAM_0				0x320
#define     DEVICE_PARAM_0__VALUE			GENMASK(7, 0)

#define DEVICE_PARAM_1				0x330
#define     DEVICE_PARAM_1__VALUE			GENMASK(7, 0)

#define DEVICE_PARAM_2				0x340
#define     DEVICE_PARAM_2__VALUE			GENMASK(7, 0)

#define LOGICAL_PAGE_DATA_SIZE			0x350
#define     LOGICAL_PAGE_DATA_SIZE__VALUE		GENMASK(15, 0)

#define LOGICAL_PAGE_SPARE_SIZE			0x360
#define     LOGICAL_PAGE_SPARE_SIZE__VALUE		GENMASK(15, 0)

#define REVISION				0x370
#define     REVISION__VALUE				GENMASK(15, 0)

#define ONFI_DEVICE_FEATURES			0x380
#define     ONFI_DEVICE_FEATURES__VALUE			GENMASK(5, 0)

#define ONFI_OPTIONAL_COMMANDS			0x390
#define     ONFI_OPTIONAL_COMMANDS__VALUE		GENMASK(5, 0)

#define ONFI_TIMING_MODE			0x3a0
#define     ONFI_TIMING_MODE__VALUE			GENMASK(5, 0)

#define ONFI_PGM_CACHE_TIMING_MODE		0x3b0
#define     ONFI_PGM_CACHE_TIMING_MODE__VALUE		GENMASK(5, 0)

#define ONFI_DEVICE_NO_OF_LUNS			0x3c0
#define     ONFI_DEVICE_NO_OF_LUNS__NO_OF_LUNS		GENMASK(7, 0)
#define     ONFI_DEVICE_NO_OF_LUNS__ONFI_DEVICE		BIT(8)

#define ONFI_DEVICE_NO_OF_BLOCKS_PER_LUN_L	0x3d0
#define     ONFI_DEVICE_NO_OF_BLOCKS_PER_LUN_L__VALUE	GENMASK(15, 0)

#define ONFI_DEVICE_NO_OF_BLOCKS_PER_LUN_U	0x3e0
#define     ONFI_DEVICE_NO_OF_BLOCKS_PER_LUN_U__VALUE	GENMASK(15, 0)

#define FEATURES				0x3f0
#define     FEATURES__N_BANKS				GENMASK(1, 0)
#define     FEATURES__ECC_MAX_ERR			GENMASK(5, 2)
#define     FEATURES__DMA				BIT(6)
#define     FEATURES__CMD_DMA				BIT(7)
#define     FEATURES__PARTITION				BIT(8)
#define     FEATURES__XDMA_SIDEBAND			BIT(9)
#define     FEATURES__GPREG				BIT(10)
#define     FEATURES__INDEX_ADDR			BIT(11)

#define TRANSFER_MODE				0x400
#define     TRANSFER_MODE__VALUE			GENMASK(1, 0)

#define INTR_STATUS(bank)			(0x410 + (bank) * 0x50)
#define INTR_EN(bank)				(0x420 + (bank) * 0x50)
/* bit[1:0] is used differently depending on IP version */
#define     INTR__ECC_UNCOR_ERR				BIT(0)	/* new IP */
#define     INTR__ECC_TRANSACTION_DONE			BIT(0)	/* old IP */
#define     INTR__ECC_ERR				BIT(1)	/* old IP */
#define     INTR__DMA_CMD_COMP				BIT(2)
#define     INTR__TIME_OUT				BIT(3)
#define     INTR__PROGRAM_FAIL				BIT(4)
#define     INTR__ERASE_FAIL				BIT(5)
#define     INTR__LOAD_COMP				BIT(6)
#define     INTR__PROGRAM_COMP				BIT(7)
#define     INTR__ERASE_COMP				BIT(8)
#define     INTR__PIPE_CPYBCK_CMD_COMP			BIT(9)
#define     INTR__LOCKED_BLK				BIT(10)
#define     INTR__UNSUP_CMD				BIT(11)
#define     INTR__INT_ACT				BIT(12)
#define     INTR__RST_COMP				BIT(13)
#define     INTR__PIPE_CMD_ERR				BIT(14)
#define     INTR__PAGE_XFER_INC				BIT(15)
#define     INTR__ERASED_PAGE				BIT(16)

#define PAGE_CNT(bank)				(0x430 + (bank) * 0x50)
#define ERR_PAGE_ADDR(bank)			(0x440 + (bank) * 0x50)
#define ERR_BLOCK_ADDR(bank)			(0x450 + (bank) * 0x50)

#define ECC_THRESHOLD				0x600
#define     ECC_THRESHOLD__VALUE			GENMASK(9, 0)

#define ECC_ERROR_BLOCK_ADDRESS			0x610
#define     ECC_ERROR_BLOCK_ADDRESS__VALUE		GENMASK(15, 0)

#define ECC_ERROR_PAGE_ADDRESS			0x620
#define     ECC_ERROR_PAGE_ADDRESS__VALUE		GENMASK(11, 0)
#define     ECC_ERROR_PAGE_ADDRESS__BANK		GENMASK(15, 12)

#define ECC_ERROR_ADDRESS			0x630
#define     ECC_ERROR_ADDRESS__OFFSET			GENMASK(11, 0)
#define     ECC_ERROR_ADDRESS__SECTOR			GENMASK(15, 12)

#define ERR_CORRECTION_INFO			0x640
#define     ERR_CORRECTION_INFO__BYTE			GENMASK(7, 0)
#define     ERR_CORRECTION_INFO__DEVICE			GENMASK(11, 8)
#define     ERR_CORRECTION_INFO__UNCOR			BIT(14)
#define     ERR_CORRECTION_INFO__LAST_ERR		BIT(15)

#define ECC_COR_INFO(bank)			(0x650 + (bank) / 2 * 0x10)
#define     ECC_COR_INFO__SHIFT(bank)			((bank) % 2 * 8)
#define     ECC_COR_INFO__MAX_ERRORS			GENMASK(6, 0)
#define     ECC_COR_INFO__UNCOR_ERR			BIT(7)

#define CFG_DATA_BLOCK_SIZE			0x6b0

#define CFG_LAST_DATA_BLOCK_SIZE		0x6c0

#define CFG_NUM_DATA_BLOCKS			0x6d0

#define CFG_META_DATA_SIZE			0x6e0

#define DMA_ENABLE				0x700
#define     DMA_ENABLE__FLAG				BIT(0)

#define IGNORE_ECC_DONE				0x710
#define     IGNORE_ECC_DONE__FLAG			BIT(0)

#define DMA_INTR				0x720
#define DMA_INTR_EN				0x730
#define     DMA_INTR__TARGET_ERROR			BIT(0)
#define     DMA_INTR__DESC_COMP_CHANNEL0		BIT(1)
#define     DMA_INTR__DESC_COMP_CHANNEL1		BIT(2)
#define     DMA_INTR__DESC_COMP_CHANNEL2		BIT(3)
#define     DMA_INTR__DESC_COMP_CHANNEL3		BIT(4)
#define     DMA_INTR__MEMCOPY_DESC_COMP			BIT(5)

#define TARGET_ERR_ADDR_LO			0x740
#define     TARGET_ERR_ADDR_LO__VALUE			GENMASK(15, 0)

#define TARGET_ERR_ADDR_HI			0x750
#define     TARGET_ERR_ADDR_HI__VALUE			GENMASK(15, 0)

#define CHNL_ACTIVE				0x760
#define     CHNL_ACTIVE__CHANNEL0			BIT(0)
#define     CHNL_ACTIVE__CHANNEL1			BIT(1)
#define     CHNL_ACTIVE__CHANNEL2			BIT(2)
#define     CHNL_ACTIVE__CHANNEL3			BIT(3)

/**
 * struct denali_chip_sel - per-CS data of Denali NAND
 *
 * @bank:                  bank id of the controller this CS is connected to
 * @hwhr2_and_we_2_re:     value of timing register HWHR2_AND_WE_2_RE
 * @tcwaw_and_addr_2_data: value of timing register TCWAW_AND_ADDR_2_DATA
 * @re_2_we:               value of timing register RE_2_WE
 * @acc_clks:              value of timing register ACC_CLKS
 * @rdwr_en_lo_cnt:        value of timing register RDWR_EN_LO_CNT
 * @rdwr_en_hi_cnt:        value of timing register RDWR_EN_HI_CNT
 * @cs_setup_cnt:          value of timing register CS_SETUP_CNT
 * @re_2_re:               value of timing register RE_2_RE
 */
struct denali_chip_sel {
	int bank;
	u32 hwhr2_and_we_2_re;
	u32 tcwaw_and_addr_2_data;
	u32 re_2_we;
	u32 acc_clks;
	u32 rdwr_en_lo_cnt;
	u32 rdwr_en_hi_cnt;
	u32 cs_setup_cnt;
	u32 re_2_re;
};

/**
 * struct denali_chip - per-chip data of Denali NAND
 *
 * @chip:  base NAND chip structure
 * @node:  node to be used to associate this chip with the controller
 * @nsels: the number of CS lines of this chip
 * @sels:  the array of per-cs data
 */
struct denali_chip {
	struct nand_chip chip;
	struct list_head node;
	unsigned int nsels;
	struct denali_chip_sel sels[] __counted_by(nsels);
};

/**
 * struct denali_controller - Denali NAND controller data
 *
 * @controller:     base NAND controller structure
 * @dev:            device
 * @chips:          the list of chips attached to this controller
 * @clk_rate:       frequency of core clock
 * @clk_x_rate:     frequency of bus interface clock
 * @reg:            base of Register Interface
 * @host:           base of Host Data/Command interface
 * @complete:       completion used to wait for interrupts
 * @irq:            interrupt number
 * @irq_mask:       interrupt bits the controller is waiting for
 * @irq_status:     interrupt bits of events that have happened
 * @irq_lock:       lock to protect @irq_mask and @irq_status
 * @dma_avail:      set if DMA engine is available
 * @devs_per_cs:    number of devices connected in parallel
 * @oob_skip_bytes: number of bytes in OOB skipped by the ECC engine
 * @active_bank:    active bank id
 * @nbanks:         the number of banks supported by this controller
 * @revision:       IP revision
 * @caps:           controller capabilities that cannot be detected run-time
 * @ecc_caps:       ECC engine capabilities
 * @host_read:      callback for read access of Host Data/Command Interface
 * @host_write:     callback for write access of Host Data/Command Interface
 * @setup_dma:      callback for setup of the Data DMA
 */
struct denali_controller {
	struct nand_controller controller;
	struct device *dev;
	struct list_head chips;
	unsigned long clk_rate;
	unsigned long clk_x_rate;
	void __iomem *reg;
	void __iomem *host;
	struct completion complete;
	int irq;
	u32 irq_mask;
	u32 irq_status;
	spinlock_t irq_lock;
	bool dma_avail;
	int devs_per_cs;
	int oob_skip_bytes;
	int active_bank;
	int nbanks;
	unsigned int revision;
	unsigned int caps;
	const struct nand_ecc_caps *ecc_caps;
	u32 (*host_read)(struct denali_controller *denali, u32 addr);
	void (*host_write)(struct denali_controller *denali, u32 addr,
			   u32 data);
	void (*setup_dma)(struct denali_controller *denali, dma_addr_t dma_addr,
			  int page, bool write);
};

#define DENALI_CAP_HW_ECC_FIXUP			BIT(0)
#define DENALI_CAP_DMA_64BIT			BIT(1)

int denali_calc_ecc_bytes(int step_size, int strength);
int denali_chip_init(struct denali_controller *denali,
		     struct denali_chip *dchip);
int denali_init(struct denali_controller *denali);
void denali_remove(struct denali_controller *denali);

#endif /* __DENALI_H__ */
