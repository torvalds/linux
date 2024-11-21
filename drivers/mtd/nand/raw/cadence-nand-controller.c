// SPDX-License-Identifier: GPL-2.0+
/*
 * Cadence NAND flash controller driver
 *
 * Copyright (C) 2019 Cadence
 *
 * Author: Piotr Sroka <piotrs@cadence.com>
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/rawnand.h>
#include <linux/iopoll.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/slab.h>

/*
 * HPNFC can work in 3 modes:
 * -  PIO - can work in master or slave DMA
 * -  CDMA - needs Master DMA for accessing command descriptors.
 * -  Generic mode - can use only slave DMA.
 * CDMA and PIO modes can be used to execute only base commands.
 * Generic mode can be used to execute any command
 * on NAND flash memory. Driver uses CDMA mode for
 * block erasing, page reading, page programing.
 * Generic mode is used for executing rest of commands.
 */

#define MAX_ADDRESS_CYC		6
#define MAX_ERASE_ADDRESS_CYC	3
#define MAX_DATA_SIZE		0xFFFC
#define DMA_DATA_SIZE_ALIGN	8

/* Register definition. */
/*
 * Command register 0.
 * Writing data to this register will initiate a new transaction
 * of the NF controller.
 */
#define CMD_REG0			0x0000
/* Command type field mask. */
#define		CMD_REG0_CT		GENMASK(31, 30)
/* Command type CDMA. */
#define		CMD_REG0_CT_CDMA	0uL
/* Command type generic. */
#define		CMD_REG0_CT_GEN		3uL
/* Command thread number field mask. */
#define		CMD_REG0_TN		GENMASK(27, 24)

/* Command register 2. */
#define CMD_REG2			0x0008
/* Command register 3. */
#define CMD_REG3			0x000C
/* Pointer register to select which thread status will be selected. */
#define CMD_STATUS_PTR			0x0010
/* Command status register for selected thread. */
#define CMD_STATUS			0x0014

/* Interrupt status register. */
#define INTR_STATUS			0x0110
#define		INTR_STATUS_SDMA_ERR	BIT(22)
#define		INTR_STATUS_SDMA_TRIGG	BIT(21)
#define		INTR_STATUS_UNSUPP_CMD	BIT(19)
#define		INTR_STATUS_DDMA_TERR	BIT(18)
#define		INTR_STATUS_CDMA_TERR	BIT(17)
#define		INTR_STATUS_CDMA_IDL	BIT(16)

/* Interrupt enable register. */
#define INTR_ENABLE				0x0114
#define		INTR_ENABLE_INTR_EN		BIT(31)
#define		INTR_ENABLE_SDMA_ERR_EN		BIT(22)
#define		INTR_ENABLE_SDMA_TRIGG_EN	BIT(21)
#define		INTR_ENABLE_UNSUPP_CMD_EN	BIT(19)
#define		INTR_ENABLE_DDMA_TERR_EN	BIT(18)
#define		INTR_ENABLE_CDMA_TERR_EN	BIT(17)
#define		INTR_ENABLE_CDMA_IDLE_EN	BIT(16)

/* Controller internal state. */
#define CTRL_STATUS				0x0118
#define		CTRL_STATUS_INIT_COMP		BIT(9)
#define		CTRL_STATUS_CTRL_BUSY		BIT(8)

/* Command Engine threads state. */
#define TRD_STATUS				0x0120

/* Command Engine interrupt thread error status. */
#define TRD_ERR_INT_STATUS			0x0128
/* Command Engine interrupt thread error enable. */
#define TRD_ERR_INT_STATUS_EN			0x0130
/* Command Engine interrupt thread complete status. */
#define TRD_COMP_INT_STATUS			0x0138

/*
 * Transfer config 0 register.
 * Configures data transfer parameters.
 */
#define TRAN_CFG_0				0x0400
/* Offset value from the beginning of the page. */
#define		TRAN_CFG_0_OFFSET		GENMASK(31, 16)
/* Numbers of sectors to transfer within singlNF device's page. */
#define		TRAN_CFG_0_SEC_CNT		GENMASK(7, 0)

/*
 * Transfer config 1 register.
 * Configures data transfer parameters.
 */
#define TRAN_CFG_1				0x0404
/* Size of last data sector. */
#define		TRAN_CFG_1_LAST_SEC_SIZE	GENMASK(31, 16)
/* Size of not-last data sector. */
#define		TRAN_CFG_1_SECTOR_SIZE		GENMASK(15, 0)

/* ECC engine configuration register 0. */
#define ECC_CONFIG_0				0x0428
/* Correction strength. */
#define		ECC_CONFIG_0_CORR_STR		GENMASK(10, 8)
/* Enable erased pages detection mechanism. */
#define		ECC_CONFIG_0_ERASE_DET_EN	BIT(1)
/* Enable controller ECC check bits generation and correction. */
#define		ECC_CONFIG_0_ECC_EN		BIT(0)

/* ECC engine configuration register 1. */
#define ECC_CONFIG_1				0x042C

/* Multiplane settings register. */
#define MULTIPLANE_CFG				0x0434
/* Cache operation settings. */
#define CACHE_CFG				0x0438

/* DMA settings register. */
#define DMA_SETINGS				0x043C
/* Enable SDMA error report on access unprepared slave DMA interface. */
#define		DMA_SETINGS_SDMA_ERR_RSP	BIT(17)

/* Transferred data block size for the slave DMA module. */
#define SDMA_SIZE				0x0440

/* Thread number associated with transferred data block
 * for the slave DMA module.
 */
#define SDMA_TRD_NUM				0x0444
/* Thread number mask. */
#define		SDMA_TRD_NUM_SDMA_TRD		GENMASK(2, 0)

#define CONTROL_DATA_CTRL			0x0494
/* Thread number mask. */
#define		CONTROL_DATA_CTRL_SIZE		GENMASK(15, 0)

#define CTRL_VERSION				0x800
#define		CTRL_VERSION_REV		GENMASK(7, 0)

/* Available hardware features of the controller. */
#define CTRL_FEATURES				0x804
/* Support for NV-DDR2/3 work mode. */
#define		CTRL_FEATURES_NVDDR_2_3		BIT(28)
/* Support for NV-DDR work mode. */
#define		CTRL_FEATURES_NVDDR		BIT(27)
/* Support for asynchronous work mode. */
#define		CTRL_FEATURES_ASYNC		BIT(26)
/* Support for asynchronous work mode. */
#define		CTRL_FEATURES_N_BANKS		GENMASK(25, 24)
/* Slave and Master DMA data width. */
#define		CTRL_FEATURES_DMA_DWITH64	BIT(21)
/* Availability of Control Data feature.*/
#define		CTRL_FEATURES_CONTROL_DATA	BIT(10)

/* BCH Engine identification register 0 - correction strengths. */
#define BCH_CFG_0				0x838
#define		BCH_CFG_0_CORR_CAP_0		GENMASK(7, 0)
#define		BCH_CFG_0_CORR_CAP_1		GENMASK(15, 8)
#define		BCH_CFG_0_CORR_CAP_2		GENMASK(23, 16)
#define		BCH_CFG_0_CORR_CAP_3		GENMASK(31, 24)

/* BCH Engine identification register 1 - correction strengths. */
#define BCH_CFG_1				0x83C
#define		BCH_CFG_1_CORR_CAP_4		GENMASK(7, 0)
#define		BCH_CFG_1_CORR_CAP_5		GENMASK(15, 8)
#define		BCH_CFG_1_CORR_CAP_6		GENMASK(23, 16)
#define		BCH_CFG_1_CORR_CAP_7		GENMASK(31, 24)

/* BCH Engine identification register 2 - sector sizes. */
#define BCH_CFG_2				0x840
#define		BCH_CFG_2_SECT_0		GENMASK(15, 0)
#define		BCH_CFG_2_SECT_1		GENMASK(31, 16)

/* BCH Engine identification register 3. */
#define BCH_CFG_3				0x844
#define		BCH_CFG_3_METADATA_SIZE		GENMASK(23, 16)

/* Ready/Busy# line status. */
#define RBN_SETINGS				0x1004

/* Common settings. */
#define COMMON_SET				0x1008
/* 16 bit device connected to the NAND Flash interface. */
#define		COMMON_SET_DEVICE_16BIT		BIT(8)

/* Skip_bytes registers. */
#define SKIP_BYTES_CONF				0x100C
#define		SKIP_BYTES_MARKER_VALUE		GENMASK(31, 16)
#define		SKIP_BYTES_NUM_OF_BYTES		GENMASK(7, 0)

#define SKIP_BYTES_OFFSET			0x1010
#define		 SKIP_BYTES_OFFSET_VALUE	GENMASK(23, 0)

/* Timings configuration. */
#define ASYNC_TOGGLE_TIMINGS			0x101c
#define		ASYNC_TOGGLE_TIMINGS_TRH	GENMASK(28, 24)
#define		ASYNC_TOGGLE_TIMINGS_TRP	GENMASK(20, 16)
#define		ASYNC_TOGGLE_TIMINGS_TWH	GENMASK(12, 8)
#define		ASYNC_TOGGLE_TIMINGS_TWP	GENMASK(4, 0)

#define	TIMINGS0				0x1024
#define		TIMINGS0_TADL			GENMASK(31, 24)
#define		TIMINGS0_TCCS			GENMASK(23, 16)
#define		TIMINGS0_TWHR			GENMASK(15, 8)
#define		TIMINGS0_TRHW			GENMASK(7, 0)

#define	TIMINGS1				0x1028
#define		TIMINGS1_TRHZ			GENMASK(31, 24)
#define		TIMINGS1_TWB			GENMASK(23, 16)
#define		TIMINGS1_TVDLY			GENMASK(7, 0)

#define	TIMINGS2				0x102c
#define		TIMINGS2_TFEAT			GENMASK(25, 16)
#define		TIMINGS2_CS_HOLD_TIME		GENMASK(13, 8)
#define		TIMINGS2_CS_SETUP_TIME		GENMASK(5, 0)

/* Configuration of the resynchronization of slave DLL of PHY. */
#define DLL_PHY_CTRL				0x1034
#define		DLL_PHY_CTRL_DLL_RST_N		BIT(24)
#define		DLL_PHY_CTRL_EXTENDED_WR_MODE	BIT(17)
#define		DLL_PHY_CTRL_EXTENDED_RD_MODE	BIT(16)
#define		DLL_PHY_CTRL_RS_HIGH_WAIT_CNT	GENMASK(11, 8)
#define		DLL_PHY_CTRL_RS_IDLE_CNT	GENMASK(7, 0)

/* Register controlling DQ related timing. */
#define PHY_DQ_TIMING				0x2000
/* Register controlling DSQ related timing.  */
#define PHY_DQS_TIMING				0x2004
#define		PHY_DQS_TIMING_DQS_SEL_OE_END	GENMASK(3, 0)
#define		PHY_DQS_TIMING_PHONY_DQS_SEL	BIT(16)
#define		PHY_DQS_TIMING_USE_PHONY_DQS	BIT(20)

/* Register controlling the gate and loopback control related timing. */
#define PHY_GATE_LPBK_CTRL			0x2008
#define		PHY_GATE_LPBK_CTRL_RDS		GENMASK(24, 19)

/* Register holds the control for the master DLL logic. */
#define PHY_DLL_MASTER_CTRL			0x200C
#define		PHY_DLL_MASTER_CTRL_BYPASS_MODE	BIT(23)

/* Register holds the control for the slave DLL logic. */
#define PHY_DLL_SLAVE_CTRL			0x2010

/* This register handles the global control settings for the PHY. */
#define PHY_CTRL				0x2080
#define		PHY_CTRL_SDR_DQS		BIT(14)
#define		PHY_CTRL_PHONY_DQS		GENMASK(9, 4)

/*
 * This register handles the global control settings
 * for the termination selects for reads.
 */
#define PHY_TSEL				0x2084

/* Generic command layout. */
#define GCMD_LAY_CS			GENMASK_ULL(11, 8)
/*
 * This bit informs the minicotroller if it has to wait for tWB
 * after sending the last CMD/ADDR/DATA in the sequence.
 */
#define GCMD_LAY_TWB			BIT_ULL(6)
/* Type of generic instruction. */
#define GCMD_LAY_INSTR			GENMASK_ULL(5, 0)

/* Generic CMD sequence type. */
#define		GCMD_LAY_INSTR_CMD	0
/* Generic ADDR sequence type. */
#define		GCMD_LAY_INSTR_ADDR	1
/* Generic data transfer sequence type. */
#define		GCMD_LAY_INSTR_DATA	2

/* Input part of generic command type of input is command. */
#define GCMD_LAY_INPUT_CMD		GENMASK_ULL(23, 16)

/* Generic command address sequence - address fields. */
#define GCMD_LAY_INPUT_ADDR		GENMASK_ULL(63, 16)
/* Generic command address sequence - address size. */
#define GCMD_LAY_INPUT_ADDR_SIZE	GENMASK_ULL(13, 11)

/* Transfer direction field of generic command data sequence. */
#define GCMD_DIR			BIT_ULL(11)
/* Read transfer direction of generic command data sequence. */
#define		GCMD_DIR_READ		0
/* Write transfer direction of generic command data sequence. */
#define		GCMD_DIR_WRITE		1

/* ECC enabled flag of generic command data sequence - ECC enabled. */
#define GCMD_ECC_EN			BIT_ULL(12)
/* Generic command data sequence - sector size. */
#define GCMD_SECT_SIZE			GENMASK_ULL(31, 16)
/* Generic command data sequence - sector count. */
#define GCMD_SECT_CNT			GENMASK_ULL(39, 32)
/* Generic command data sequence - last sector size. */
#define GCMD_LAST_SIZE			GENMASK_ULL(55, 40)

/* CDMA descriptor fields. */
/* Erase command type of CDMA descriptor. */
#define CDMA_CT_ERASE		0x1000
/* Program page command type of CDMA descriptor. */
#define CDMA_CT_WR		0x2100
/* Read page command type of CDMA descriptor. */
#define CDMA_CT_RD		0x2200

/* Flash pointer memory shift. */
#define CDMA_CFPTR_MEM_SHIFT	24
/* Flash pointer memory mask. */
#define CDMA_CFPTR_MEM		GENMASK(26, 24)

/*
 * Command DMA descriptor flags. If set causes issue interrupt after
 * the completion of descriptor processing.
 */
#define CDMA_CF_INT		BIT(8)
/*
 * Command DMA descriptor flags - the next descriptor
 * address field is valid and descriptor processing should continue.
 */
#define CDMA_CF_CONT		BIT(9)
/* DMA master flag of command DMA descriptor. */
#define CDMA_CF_DMA_MASTER	BIT(10)

/* Operation complete status of command descriptor. */
#define CDMA_CS_COMP		BIT(15)
/* Operation complete status of command descriptor. */
/* Command descriptor status - operation fail. */
#define CDMA_CS_FAIL		BIT(14)
/* Command descriptor status - page erased. */
#define CDMA_CS_ERP		BIT(11)
/* Command descriptor status - timeout occurred. */
#define CDMA_CS_TOUT		BIT(10)
/*
 * Maximum amount of correction applied to one ECC sector.
 * It is part of command descriptor status.
 */
#define CDMA_CS_MAXERR		GENMASK(9, 2)
/* Command descriptor status - uncorrectable ECC error. */
#define CDMA_CS_UNCE		BIT(1)
/* Command descriptor status - descriptor error. */
#define CDMA_CS_ERR		BIT(0)

/* Status of operation - OK. */
#define STAT_OK			0
/* Status of operation - FAIL. */
#define STAT_FAIL		2
/* Status of operation - uncorrectable ECC error. */
#define STAT_ECC_UNCORR		3
/* Status of operation - page erased. */
#define STAT_ERASED		5
/* Status of operation - correctable ECC error. */
#define STAT_ECC_CORR		6
/* Status of operation - unsuspected state. */
#define STAT_UNKNOWN		7
/* Status of operation - operation is not completed yet. */
#define STAT_BUSY		0xFF

#define BCH_MAX_NUM_CORR_CAPS		8
#define BCH_MAX_NUM_SECTOR_SIZES	2

struct cadence_nand_timings {
	u32 async_toggle_timings;
	u32 timings0;
	u32 timings1;
	u32 timings2;
	u32 dll_phy_ctrl;
	u32 phy_ctrl;
	u32 phy_dqs_timing;
	u32 phy_gate_lpbk_ctrl;
};

/* Command DMA descriptor. */
struct cadence_nand_cdma_desc {
	/* Next descriptor address. */
	u64 next_pointer;

	/* Flash address is a 32-bit address comprising of BANK and ROW ADDR. */
	u32 flash_pointer;
	/*field appears in HPNFC version 13*/
	u16 bank;
	u16 rsvd0;

	/* Operation the controller needs to perform. */
	u16 command_type;
	u16 rsvd1;
	/* Flags for operation of this command. */
	u16 command_flags;
	u16 rsvd2;

	/* System/host memory address required for data DMA commands. */
	u64 memory_pointer;

	/* Status of operation. */
	u32 status;
	u32 rsvd3;

	/* Address pointer to sync buffer location. */
	u64 sync_flag_pointer;

	/* Controls the buffer sync mechanism. */
	u32 sync_arguments;
	u32 rsvd4;

	/* Control data pointer. */
	u64 ctrl_data_ptr;
};

/* Interrupt status. */
struct cadence_nand_irq_status {
	/* Thread operation complete status. */
	u32 trd_status;
	/* Thread operation error. */
	u32 trd_error;
	/* Controller status. */
	u32 status;
};

/* Cadence NAND flash controller capabilities get from driver data. */
struct cadence_nand_dt_devdata {
	/* Skew value of the output signals of the NAND Flash interface. */
	u32 if_skew;
	/* It informs if slave DMA interface is connected to DMA engine. */
	unsigned int has_dma:1;
};

/* Cadence NAND flash controller capabilities read from registers. */
struct cdns_nand_caps {
	/* Maximum number of banks supported by hardware. */
	u8 max_banks;
	/* Slave and Master DMA data width in bytes (4 or 8). */
	u8 data_dma_width;
	/* Control Data feature supported. */
	bool data_control_supp;
	/* Is PHY type DLL. */
	bool is_phy_type_dll;
};

struct cdns_nand_ctrl {
	struct device *dev;
	struct nand_controller controller;
	struct cadence_nand_cdma_desc *cdma_desc;
	/* IP capability. */
	const struct cadence_nand_dt_devdata *caps1;
	struct cdns_nand_caps caps2;
	u8 ctrl_rev;
	dma_addr_t dma_cdma_desc;
	u8 *buf;
	u32 buf_size;
	u8 curr_corr_str_idx;

	/* Register interface. */
	void __iomem *reg;

	struct {
		void __iomem *virt;
		dma_addr_t dma;
	} io;

	int irq;
	/* Interrupts that have happened. */
	struct cadence_nand_irq_status irq_status;
	/* Interrupts we are waiting for. */
	struct cadence_nand_irq_status irq_mask;
	struct completion complete;
	/* Protect irq_mask and irq_status. */
	spinlock_t irq_lock;

	int ecc_strengths[BCH_MAX_NUM_CORR_CAPS];
	struct nand_ecc_step_info ecc_stepinfos[BCH_MAX_NUM_SECTOR_SIZES];
	struct nand_ecc_caps ecc_caps;

	int curr_trans_type;

	struct dma_chan *dmac;

	u32 nf_clk_rate;
	/*
	 * Estimated Board delay. The value includes the total
	 * round trip delay for the signals and is used for deciding on values
	 * associated with data read capture.
	 */
	u32 board_delay;

	struct nand_chip *selected_chip;

	unsigned long assigned_cs;
	struct list_head chips;
	u8 bch_metadata_size;
};

struct cdns_nand_chip {
	struct cadence_nand_timings timings;
	struct nand_chip chip;
	u8 nsels;
	struct list_head node;

	/*
	 * part of oob area of NAND flash memory page.
	 * This part is available for user to read or write.
	 */
	u32 avail_oob_size;

	/* Sector size. There are few sectors per mtd->writesize */
	u32 sector_size;
	u32 sector_count;

	/* Offset of BBM. */
	u8 bbm_offs;
	/* Number of bytes reserved for BBM. */
	u8 bbm_len;
	/* ECC strength index. */
	u8 corr_str_idx;

	u8 cs[] __counted_by(nsels);
};

static inline struct
cdns_nand_chip *to_cdns_nand_chip(struct nand_chip *chip)
{
	return container_of(chip, struct cdns_nand_chip, chip);
}

static inline struct
cdns_nand_ctrl *to_cdns_nand_ctrl(struct nand_controller *controller)
{
	return container_of(controller, struct cdns_nand_ctrl, controller);
}

static bool
cadence_nand_dma_buf_ok(struct cdns_nand_ctrl *cdns_ctrl, const void *buf,
			u32 buf_len)
{
	u8 data_dma_width = cdns_ctrl->caps2.data_dma_width;

	return buf && virt_addr_valid(buf) &&
		likely(IS_ALIGNED((uintptr_t)buf, data_dma_width)) &&
		likely(IS_ALIGNED(buf_len, DMA_DATA_SIZE_ALIGN));
}

static int cadence_nand_wait_for_value(struct cdns_nand_ctrl *cdns_ctrl,
				       u32 reg_offset, u32 timeout_us,
				       u32 mask, bool is_clear)
{
	u32 val;
	int ret;

	ret = readl_relaxed_poll_timeout(cdns_ctrl->reg + reg_offset,
					 val, !(val & mask) == is_clear,
					 10, timeout_us);

	if (ret < 0) {
		dev_err(cdns_ctrl->dev,
			"Timeout while waiting for reg %x with mask %x is clear %d\n",
			reg_offset, mask, is_clear);
	}

	return ret;
}

static int cadence_nand_set_ecc_enable(struct cdns_nand_ctrl *cdns_ctrl,
				       bool enable)
{
	u32 reg;

	if (cadence_nand_wait_for_value(cdns_ctrl, CTRL_STATUS,
					1000000,
					CTRL_STATUS_CTRL_BUSY, true))
		return -ETIMEDOUT;

	reg = readl_relaxed(cdns_ctrl->reg + ECC_CONFIG_0);

	if (enable)
		reg |= ECC_CONFIG_0_ECC_EN;
	else
		reg &= ~ECC_CONFIG_0_ECC_EN;

	writel_relaxed(reg, cdns_ctrl->reg + ECC_CONFIG_0);

	return 0;
}

static void cadence_nand_set_ecc_strength(struct cdns_nand_ctrl *cdns_ctrl,
					  u8 corr_str_idx)
{
	u32 reg;

	if (cdns_ctrl->curr_corr_str_idx == corr_str_idx)
		return;

	reg = readl_relaxed(cdns_ctrl->reg + ECC_CONFIG_0);
	reg &= ~ECC_CONFIG_0_CORR_STR;
	reg |= FIELD_PREP(ECC_CONFIG_0_CORR_STR, corr_str_idx);
	writel_relaxed(reg, cdns_ctrl->reg + ECC_CONFIG_0);

	cdns_ctrl->curr_corr_str_idx = corr_str_idx;
}

static int cadence_nand_get_ecc_strength_idx(struct cdns_nand_ctrl *cdns_ctrl,
					     u8 strength)
{
	int i, corr_str_idx = -1;

	for (i = 0; i < BCH_MAX_NUM_CORR_CAPS; i++) {
		if (cdns_ctrl->ecc_strengths[i] == strength) {
			corr_str_idx = i;
			break;
		}
	}

	return corr_str_idx;
}

static int cadence_nand_set_skip_marker_val(struct cdns_nand_ctrl *cdns_ctrl,
					    u16 marker_value)
{
	u32 reg;

	if (cadence_nand_wait_for_value(cdns_ctrl, CTRL_STATUS,
					1000000,
					CTRL_STATUS_CTRL_BUSY, true))
		return -ETIMEDOUT;

	reg = readl_relaxed(cdns_ctrl->reg + SKIP_BYTES_CONF);
	reg &= ~SKIP_BYTES_MARKER_VALUE;
	reg |= FIELD_PREP(SKIP_BYTES_MARKER_VALUE,
			  marker_value);

	writel_relaxed(reg, cdns_ctrl->reg + SKIP_BYTES_CONF);

	return 0;
}

static int cadence_nand_set_skip_bytes_conf(struct cdns_nand_ctrl *cdns_ctrl,
					    u8 num_of_bytes,
					    u32 offset_value,
					    int enable)
{
	u32 reg, skip_bytes_offset;

	if (cadence_nand_wait_for_value(cdns_ctrl, CTRL_STATUS,
					1000000,
					CTRL_STATUS_CTRL_BUSY, true))
		return -ETIMEDOUT;

	if (!enable) {
		num_of_bytes = 0;
		offset_value = 0;
	}

	reg = readl_relaxed(cdns_ctrl->reg + SKIP_BYTES_CONF);
	reg &= ~SKIP_BYTES_NUM_OF_BYTES;
	reg |= FIELD_PREP(SKIP_BYTES_NUM_OF_BYTES,
			  num_of_bytes);
	skip_bytes_offset = FIELD_PREP(SKIP_BYTES_OFFSET_VALUE,
				       offset_value);

	writel_relaxed(reg, cdns_ctrl->reg + SKIP_BYTES_CONF);
	writel_relaxed(skip_bytes_offset, cdns_ctrl->reg + SKIP_BYTES_OFFSET);

	return 0;
}

/* Functions enables/disables hardware detection of erased data */
static void cadence_nand_set_erase_detection(struct cdns_nand_ctrl *cdns_ctrl,
					     bool enable,
					     u8 bitflips_threshold)
{
	u32 reg;

	reg = readl_relaxed(cdns_ctrl->reg + ECC_CONFIG_0);

	if (enable)
		reg |= ECC_CONFIG_0_ERASE_DET_EN;
	else
		reg &= ~ECC_CONFIG_0_ERASE_DET_EN;

	writel_relaxed(reg, cdns_ctrl->reg + ECC_CONFIG_0);
	writel_relaxed(bitflips_threshold, cdns_ctrl->reg + ECC_CONFIG_1);
}

static int cadence_nand_set_access_width16(struct cdns_nand_ctrl *cdns_ctrl,
					   bool bit_bus16)
{
	u32 reg;

	if (cadence_nand_wait_for_value(cdns_ctrl, CTRL_STATUS,
					1000000,
					CTRL_STATUS_CTRL_BUSY, true))
		return -ETIMEDOUT;

	reg = readl_relaxed(cdns_ctrl->reg + COMMON_SET);

	if (!bit_bus16)
		reg &= ~COMMON_SET_DEVICE_16BIT;
	else
		reg |= COMMON_SET_DEVICE_16BIT;
	writel_relaxed(reg, cdns_ctrl->reg + COMMON_SET);

	return 0;
}

static void
cadence_nand_clear_interrupt(struct cdns_nand_ctrl *cdns_ctrl,
			     struct cadence_nand_irq_status *irq_status)
{
	writel_relaxed(irq_status->status, cdns_ctrl->reg + INTR_STATUS);
	writel_relaxed(irq_status->trd_status,
		       cdns_ctrl->reg + TRD_COMP_INT_STATUS);
	writel_relaxed(irq_status->trd_error,
		       cdns_ctrl->reg + TRD_ERR_INT_STATUS);
}

static void
cadence_nand_read_int_status(struct cdns_nand_ctrl *cdns_ctrl,
			     struct cadence_nand_irq_status *irq_status)
{
	irq_status->status = readl_relaxed(cdns_ctrl->reg + INTR_STATUS);
	irq_status->trd_status = readl_relaxed(cdns_ctrl->reg
					       + TRD_COMP_INT_STATUS);
	irq_status->trd_error = readl_relaxed(cdns_ctrl->reg
					      + TRD_ERR_INT_STATUS);
}

static u32 irq_detected(struct cdns_nand_ctrl *cdns_ctrl,
			struct cadence_nand_irq_status *irq_status)
{
	cadence_nand_read_int_status(cdns_ctrl, irq_status);

	return irq_status->status || irq_status->trd_status ||
		irq_status->trd_error;
}

static void cadence_nand_reset_irq(struct cdns_nand_ctrl *cdns_ctrl)
{
	unsigned long flags;

	spin_lock_irqsave(&cdns_ctrl->irq_lock, flags);
	memset(&cdns_ctrl->irq_status, 0, sizeof(cdns_ctrl->irq_status));
	memset(&cdns_ctrl->irq_mask, 0, sizeof(cdns_ctrl->irq_mask));
	spin_unlock_irqrestore(&cdns_ctrl->irq_lock, flags);
}

/*
 * This is the interrupt service routine. It handles all interrupts
 * sent to this device.
 */
static irqreturn_t cadence_nand_isr(int irq, void *dev_id)
{
	struct cdns_nand_ctrl *cdns_ctrl = dev_id;
	struct cadence_nand_irq_status irq_status;
	irqreturn_t result = IRQ_NONE;

	spin_lock(&cdns_ctrl->irq_lock);

	if (irq_detected(cdns_ctrl, &irq_status)) {
		/* Handle interrupt. */
		/* First acknowledge it. */
		cadence_nand_clear_interrupt(cdns_ctrl, &irq_status);
		/* Status in the device context for someone to read. */
		cdns_ctrl->irq_status.status |= irq_status.status;
		cdns_ctrl->irq_status.trd_status |= irq_status.trd_status;
		cdns_ctrl->irq_status.trd_error |= irq_status.trd_error;
		/* Notify anyone who cares that it happened. */
		complete(&cdns_ctrl->complete);
		/* Tell the OS that we've handled this. */
		result = IRQ_HANDLED;
	}
	spin_unlock(&cdns_ctrl->irq_lock);

	return result;
}

static void cadence_nand_set_irq_mask(struct cdns_nand_ctrl *cdns_ctrl,
				      struct cadence_nand_irq_status *irq_mask)
{
	writel_relaxed(INTR_ENABLE_INTR_EN | irq_mask->status,
		       cdns_ctrl->reg + INTR_ENABLE);

	writel_relaxed(irq_mask->trd_error,
		       cdns_ctrl->reg + TRD_ERR_INT_STATUS_EN);
}

static void
cadence_nand_wait_for_irq(struct cdns_nand_ctrl *cdns_ctrl,
			  struct cadence_nand_irq_status *irq_mask,
			  struct cadence_nand_irq_status *irq_status)
{
	unsigned long timeout = msecs_to_jiffies(10000);
	unsigned long time_left;

	time_left = wait_for_completion_timeout(&cdns_ctrl->complete,
						timeout);

	*irq_status = cdns_ctrl->irq_status;
	if (time_left == 0) {
		/* Timeout error. */
		dev_err(cdns_ctrl->dev, "timeout occurred:\n");
		dev_err(cdns_ctrl->dev, "\tstatus = 0x%x, mask = 0x%x\n",
			irq_status->status, irq_mask->status);
		dev_err(cdns_ctrl->dev,
			"\ttrd_status = 0x%x, trd_status mask = 0x%x\n",
			irq_status->trd_status, irq_mask->trd_status);
		dev_err(cdns_ctrl->dev,
			"\t trd_error = 0x%x, trd_error mask = 0x%x\n",
			irq_status->trd_error, irq_mask->trd_error);
	}
}

/* Execute generic command on NAND controller. */
static int cadence_nand_generic_cmd_send(struct cdns_nand_ctrl *cdns_ctrl,
					 u8 chip_nr,
					 u64 mini_ctrl_cmd)
{
	u32 mini_ctrl_cmd_l, mini_ctrl_cmd_h, reg;

	mini_ctrl_cmd |= FIELD_PREP(GCMD_LAY_CS, chip_nr);
	mini_ctrl_cmd_l = mini_ctrl_cmd & 0xFFFFFFFF;
	mini_ctrl_cmd_h = mini_ctrl_cmd >> 32;

	if (cadence_nand_wait_for_value(cdns_ctrl, CTRL_STATUS,
					1000000,
					CTRL_STATUS_CTRL_BUSY, true))
		return -ETIMEDOUT;

	cadence_nand_reset_irq(cdns_ctrl);

	writel_relaxed(mini_ctrl_cmd_l, cdns_ctrl->reg + CMD_REG2);
	writel_relaxed(mini_ctrl_cmd_h, cdns_ctrl->reg + CMD_REG3);

	/* Select generic command. */
	reg = FIELD_PREP(CMD_REG0_CT, CMD_REG0_CT_GEN);
	/* Thread number. */
	reg |= FIELD_PREP(CMD_REG0_TN, 0);

	/* Issue command. */
	writel_relaxed(reg, cdns_ctrl->reg + CMD_REG0);

	return 0;
}

/* Wait for data on slave DMA interface. */
static int cadence_nand_wait_on_sdma(struct cdns_nand_ctrl *cdns_ctrl,
				     u8 *out_sdma_trd,
				     u32 *out_sdma_size)
{
	struct cadence_nand_irq_status irq_mask, irq_status;

	irq_mask.trd_status = 0;
	irq_mask.trd_error = 0;
	irq_mask.status = INTR_STATUS_SDMA_TRIGG
		| INTR_STATUS_SDMA_ERR
		| INTR_STATUS_UNSUPP_CMD;

	cadence_nand_set_irq_mask(cdns_ctrl, &irq_mask);
	cadence_nand_wait_for_irq(cdns_ctrl, &irq_mask, &irq_status);
	if (irq_status.status == 0) {
		dev_err(cdns_ctrl->dev, "Timeout while waiting for SDMA\n");
		return -ETIMEDOUT;
	}

	if (irq_status.status & INTR_STATUS_SDMA_TRIGG) {
		*out_sdma_size = readl_relaxed(cdns_ctrl->reg + SDMA_SIZE);
		*out_sdma_trd  = readl_relaxed(cdns_ctrl->reg + SDMA_TRD_NUM);
		*out_sdma_trd =
			FIELD_GET(SDMA_TRD_NUM_SDMA_TRD, *out_sdma_trd);
	} else {
		dev_err(cdns_ctrl->dev, "SDMA error - irq_status %x\n",
			irq_status.status);
		return -EIO;
	}

	return 0;
}

static void cadence_nand_get_caps(struct cdns_nand_ctrl *cdns_ctrl)
{
	u32  reg;

	reg = readl_relaxed(cdns_ctrl->reg + CTRL_FEATURES);

	cdns_ctrl->caps2.max_banks = 1 << FIELD_GET(CTRL_FEATURES_N_BANKS, reg);

	if (FIELD_GET(CTRL_FEATURES_DMA_DWITH64, reg))
		cdns_ctrl->caps2.data_dma_width = 8;
	else
		cdns_ctrl->caps2.data_dma_width = 4;

	if (reg & CTRL_FEATURES_CONTROL_DATA)
		cdns_ctrl->caps2.data_control_supp = true;

	if (reg & (CTRL_FEATURES_NVDDR_2_3
		   | CTRL_FEATURES_NVDDR))
		cdns_ctrl->caps2.is_phy_type_dll = true;
}

/* Prepare CDMA descriptor. */
static void
cadence_nand_cdma_desc_prepare(struct cdns_nand_ctrl *cdns_ctrl,
			       char nf_mem, u32 flash_ptr, dma_addr_t mem_ptr,
				   dma_addr_t ctrl_data_ptr, u16 ctype)
{
	struct cadence_nand_cdma_desc *cdma_desc = cdns_ctrl->cdma_desc;

	memset(cdma_desc, 0, sizeof(struct cadence_nand_cdma_desc));

	/* Set fields for one descriptor. */
	cdma_desc->flash_pointer = flash_ptr;
	if (cdns_ctrl->ctrl_rev >= 13)
		cdma_desc->bank = nf_mem;
	else
		cdma_desc->flash_pointer |= (nf_mem << CDMA_CFPTR_MEM_SHIFT);

	cdma_desc->command_flags |= CDMA_CF_DMA_MASTER;
	cdma_desc->command_flags  |= CDMA_CF_INT;

	cdma_desc->memory_pointer = mem_ptr;
	cdma_desc->status = 0;
	cdma_desc->sync_flag_pointer = 0;
	cdma_desc->sync_arguments = 0;

	cdma_desc->command_type = ctype;
	cdma_desc->ctrl_data_ptr = ctrl_data_ptr;
}

static u8 cadence_nand_check_desc_error(struct cdns_nand_ctrl *cdns_ctrl,
					u32 desc_status)
{
	if (desc_status & CDMA_CS_ERP)
		return STAT_ERASED;

	if (desc_status & CDMA_CS_UNCE)
		return STAT_ECC_UNCORR;

	if (desc_status & CDMA_CS_ERR) {
		dev_err(cdns_ctrl->dev, ":CDMA desc error flag detected.\n");
		return STAT_FAIL;
	}

	if (FIELD_GET(CDMA_CS_MAXERR, desc_status))
		return STAT_ECC_CORR;

	return STAT_FAIL;
}

static int cadence_nand_cdma_finish(struct cdns_nand_ctrl *cdns_ctrl)
{
	struct cadence_nand_cdma_desc *desc_ptr = cdns_ctrl->cdma_desc;
	u8 status = STAT_BUSY;

	if (desc_ptr->status & CDMA_CS_FAIL) {
		status = cadence_nand_check_desc_error(cdns_ctrl,
						       desc_ptr->status);
		dev_err(cdns_ctrl->dev, ":CDMA error %x\n", desc_ptr->status);
	} else if (desc_ptr->status & CDMA_CS_COMP) {
		/* Descriptor finished with no errors. */
		if (desc_ptr->command_flags & CDMA_CF_CONT) {
			dev_info(cdns_ctrl->dev, "DMA unsupported flag is set");
			status = STAT_UNKNOWN;
		} else {
			/* Last descriptor.  */
			status = STAT_OK;
		}
	}

	return status;
}

static int cadence_nand_cdma_send(struct cdns_nand_ctrl *cdns_ctrl,
				  u8 thread)
{
	u32 reg;
	int status;

	/* Wait for thread ready. */
	status = cadence_nand_wait_for_value(cdns_ctrl, TRD_STATUS,
					     1000000,
					     BIT(thread), true);
	if (status)
		return status;

	cadence_nand_reset_irq(cdns_ctrl);
	reinit_completion(&cdns_ctrl->complete);

	writel_relaxed((u32)cdns_ctrl->dma_cdma_desc,
		       cdns_ctrl->reg + CMD_REG2);
	writel_relaxed(0, cdns_ctrl->reg + CMD_REG3);

	/* Select CDMA mode. */
	reg = FIELD_PREP(CMD_REG0_CT, CMD_REG0_CT_CDMA);
	/* Thread number. */
	reg |= FIELD_PREP(CMD_REG0_TN, thread);
	/* Issue command. */
	writel_relaxed(reg, cdns_ctrl->reg + CMD_REG0);

	return 0;
}

/* Send SDMA command and wait for finish. */
static u32
cadence_nand_cdma_send_and_wait(struct cdns_nand_ctrl *cdns_ctrl,
				u8 thread)
{
	struct cadence_nand_irq_status irq_mask, irq_status = {0};
	int status;

	irq_mask.trd_status = BIT(thread);
	irq_mask.trd_error = BIT(thread);
	irq_mask.status = INTR_STATUS_CDMA_TERR;

	cadence_nand_set_irq_mask(cdns_ctrl, &irq_mask);

	status = cadence_nand_cdma_send(cdns_ctrl, thread);
	if (status)
		return status;

	cadence_nand_wait_for_irq(cdns_ctrl, &irq_mask, &irq_status);

	if (irq_status.status == 0 && irq_status.trd_status == 0 &&
	    irq_status.trd_error == 0) {
		dev_err(cdns_ctrl->dev, "CDMA command timeout\n");
		return -ETIMEDOUT;
	}
	if (irq_status.status & irq_mask.status) {
		dev_err(cdns_ctrl->dev, "CDMA command failed\n");
		return -EIO;
	}

	return 0;
}

/*
 * ECC size depends on configured ECC strength and on maximum supported
 * ECC step size.
 */
static int cadence_nand_calc_ecc_bytes(int max_step_size, int strength)
{
	int nbytes = DIV_ROUND_UP(fls(8 * max_step_size) * strength, 8);

	return ALIGN(nbytes, 2);
}

#define CADENCE_NAND_CALC_ECC_BYTES(max_step_size) \
	static int \
	cadence_nand_calc_ecc_bytes_##max_step_size(int step_size, \
						    int strength)\
	{\
		return cadence_nand_calc_ecc_bytes(max_step_size, strength);\
	}

CADENCE_NAND_CALC_ECC_BYTES(256)
CADENCE_NAND_CALC_ECC_BYTES(512)
CADENCE_NAND_CALC_ECC_BYTES(1024)
CADENCE_NAND_CALC_ECC_BYTES(2048)
CADENCE_NAND_CALC_ECC_BYTES(4096)

/* Function reads BCH capabilities. */
static int cadence_nand_read_bch_caps(struct cdns_nand_ctrl *cdns_ctrl)
{
	struct nand_ecc_caps *ecc_caps = &cdns_ctrl->ecc_caps;
	int max_step_size = 0, nstrengths, i;
	u32 reg;

	reg = readl_relaxed(cdns_ctrl->reg + BCH_CFG_3);
	cdns_ctrl->bch_metadata_size = FIELD_GET(BCH_CFG_3_METADATA_SIZE, reg);
	if (cdns_ctrl->bch_metadata_size < 4) {
		dev_err(cdns_ctrl->dev,
			"Driver needs at least 4 bytes of BCH meta data\n");
		return -EIO;
	}

	reg = readl_relaxed(cdns_ctrl->reg + BCH_CFG_0);
	cdns_ctrl->ecc_strengths[0] = FIELD_GET(BCH_CFG_0_CORR_CAP_0, reg);
	cdns_ctrl->ecc_strengths[1] = FIELD_GET(BCH_CFG_0_CORR_CAP_1, reg);
	cdns_ctrl->ecc_strengths[2] = FIELD_GET(BCH_CFG_0_CORR_CAP_2, reg);
	cdns_ctrl->ecc_strengths[3] = FIELD_GET(BCH_CFG_0_CORR_CAP_3, reg);

	reg = readl_relaxed(cdns_ctrl->reg + BCH_CFG_1);
	cdns_ctrl->ecc_strengths[4] = FIELD_GET(BCH_CFG_1_CORR_CAP_4, reg);
	cdns_ctrl->ecc_strengths[5] = FIELD_GET(BCH_CFG_1_CORR_CAP_5, reg);
	cdns_ctrl->ecc_strengths[6] = FIELD_GET(BCH_CFG_1_CORR_CAP_6, reg);
	cdns_ctrl->ecc_strengths[7] = FIELD_GET(BCH_CFG_1_CORR_CAP_7, reg);

	reg = readl_relaxed(cdns_ctrl->reg + BCH_CFG_2);
	cdns_ctrl->ecc_stepinfos[0].stepsize =
		FIELD_GET(BCH_CFG_2_SECT_0, reg);

	cdns_ctrl->ecc_stepinfos[1].stepsize =
		FIELD_GET(BCH_CFG_2_SECT_1, reg);

	nstrengths = 0;
	for (i = 0; i < BCH_MAX_NUM_CORR_CAPS; i++) {
		if (cdns_ctrl->ecc_strengths[i] != 0)
			nstrengths++;
	}

	ecc_caps->nstepinfos = 0;
	for (i = 0; i < BCH_MAX_NUM_SECTOR_SIZES; i++) {
		/* ECC strengths are common for all step infos. */
		cdns_ctrl->ecc_stepinfos[i].nstrengths = nstrengths;
		cdns_ctrl->ecc_stepinfos[i].strengths =
			cdns_ctrl->ecc_strengths;

		if (cdns_ctrl->ecc_stepinfos[i].stepsize != 0)
			ecc_caps->nstepinfos++;

		if (cdns_ctrl->ecc_stepinfos[i].stepsize > max_step_size)
			max_step_size = cdns_ctrl->ecc_stepinfos[i].stepsize;
	}
	ecc_caps->stepinfos = &cdns_ctrl->ecc_stepinfos[0];

	switch (max_step_size) {
	case 256:
		ecc_caps->calc_ecc_bytes = &cadence_nand_calc_ecc_bytes_256;
		break;
	case 512:
		ecc_caps->calc_ecc_bytes = &cadence_nand_calc_ecc_bytes_512;
		break;
	case 1024:
		ecc_caps->calc_ecc_bytes = &cadence_nand_calc_ecc_bytes_1024;
		break;
	case 2048:
		ecc_caps->calc_ecc_bytes = &cadence_nand_calc_ecc_bytes_2048;
		break;
	case 4096:
		ecc_caps->calc_ecc_bytes = &cadence_nand_calc_ecc_bytes_4096;
		break;
	default:
		dev_err(cdns_ctrl->dev,
			"Unsupported sector size(ecc step size) %d\n",
			max_step_size);
		return -EIO;
	}

	return 0;
}

/* Hardware initialization. */
static int cadence_nand_hw_init(struct cdns_nand_ctrl *cdns_ctrl)
{
	int status;
	u32 reg;

	status = cadence_nand_wait_for_value(cdns_ctrl, CTRL_STATUS,
					     1000000,
					     CTRL_STATUS_INIT_COMP, false);
	if (status)
		return status;

	reg = readl_relaxed(cdns_ctrl->reg + CTRL_VERSION);
	cdns_ctrl->ctrl_rev = FIELD_GET(CTRL_VERSION_REV, reg);

	dev_info(cdns_ctrl->dev,
		 "%s: cadence nand controller version reg %x\n",
		 __func__, reg);

	/* Disable cache and multiplane. */
	writel_relaxed(0, cdns_ctrl->reg + MULTIPLANE_CFG);
	writel_relaxed(0, cdns_ctrl->reg + CACHE_CFG);

	/* Clear all interrupts. */
	writel_relaxed(0xFFFFFFFF, cdns_ctrl->reg + INTR_STATUS);

	cadence_nand_get_caps(cdns_ctrl);
	if (cadence_nand_read_bch_caps(cdns_ctrl))
		return -EIO;

#ifndef CONFIG_64BIT
	if (cdns_ctrl->caps2.data_dma_width == 8) {
		dev_err(cdns_ctrl->dev,
			"cannot access 64-bit dma on !64-bit architectures");
		return -EIO;
	}
#endif

	/*
	 * Set IO width access to 8.
	 * It is because during SW device discovering width access
	 * is expected to be 8.
	 */
	status = cadence_nand_set_access_width16(cdns_ctrl, false);

	return status;
}

#define TT_MAIN_OOB_AREAS	2
#define TT_RAW_PAGE		3
#define TT_BBM			4
#define TT_MAIN_OOB_AREA_EXT	5

/* Prepare size of data to transfer. */
static void
cadence_nand_prepare_data_size(struct nand_chip *chip,
			       int transfer_type)
{
	struct cdns_nand_ctrl *cdns_ctrl = to_cdns_nand_ctrl(chip->controller);
	struct cdns_nand_chip *cdns_chip = to_cdns_nand_chip(chip);
	struct mtd_info *mtd = nand_to_mtd(chip);
	u32 sec_size = 0, offset = 0, sec_cnt = 1;
	u32 last_sec_size = cdns_chip->sector_size;
	u32 data_ctrl_size = 0;
	u32 reg = 0;

	if (cdns_ctrl->curr_trans_type == transfer_type)
		return;

	switch (transfer_type) {
	case TT_MAIN_OOB_AREA_EXT:
		sec_cnt = cdns_chip->sector_count;
		sec_size = cdns_chip->sector_size;
		data_ctrl_size = cdns_chip->avail_oob_size;
		break;
	case TT_MAIN_OOB_AREAS:
		sec_cnt = cdns_chip->sector_count;
		last_sec_size = cdns_chip->sector_size
			+ cdns_chip->avail_oob_size;
		sec_size = cdns_chip->sector_size;
		break;
	case TT_RAW_PAGE:
		last_sec_size = mtd->writesize + mtd->oobsize;
		break;
	case TT_BBM:
		offset = mtd->writesize + cdns_chip->bbm_offs;
		last_sec_size = 8;
		break;
	}

	reg = 0;
	reg |= FIELD_PREP(TRAN_CFG_0_OFFSET, offset);
	reg |= FIELD_PREP(TRAN_CFG_0_SEC_CNT, sec_cnt);
	writel_relaxed(reg, cdns_ctrl->reg + TRAN_CFG_0);

	reg = 0;
	reg |= FIELD_PREP(TRAN_CFG_1_LAST_SEC_SIZE, last_sec_size);
	reg |= FIELD_PREP(TRAN_CFG_1_SECTOR_SIZE, sec_size);
	writel_relaxed(reg, cdns_ctrl->reg + TRAN_CFG_1);

	if (cdns_ctrl->caps2.data_control_supp) {
		reg = readl_relaxed(cdns_ctrl->reg + CONTROL_DATA_CTRL);
		reg &= ~CONTROL_DATA_CTRL_SIZE;
		reg |= FIELD_PREP(CONTROL_DATA_CTRL_SIZE, data_ctrl_size);
		writel_relaxed(reg, cdns_ctrl->reg + CONTROL_DATA_CTRL);
	}

	cdns_ctrl->curr_trans_type = transfer_type;
}

static int
cadence_nand_cdma_transfer(struct cdns_nand_ctrl *cdns_ctrl, u8 chip_nr,
			   int page, void *buf, void *ctrl_dat, u32 buf_size,
			   u32 ctrl_dat_size, enum dma_data_direction dir,
			   bool with_ecc)
{
	dma_addr_t dma_buf, dma_ctrl_dat = 0;
	u8 thread_nr = chip_nr;
	int status;
	u16 ctype;

	if (dir == DMA_FROM_DEVICE)
		ctype = CDMA_CT_RD;
	else
		ctype = CDMA_CT_WR;

	cadence_nand_set_ecc_enable(cdns_ctrl, with_ecc);

	dma_buf = dma_map_single(cdns_ctrl->dev, buf, buf_size, dir);
	if (dma_mapping_error(cdns_ctrl->dev, dma_buf)) {
		dev_err(cdns_ctrl->dev, "Failed to map DMA buffer\n");
		return -EIO;
	}

	if (ctrl_dat && ctrl_dat_size) {
		dma_ctrl_dat = dma_map_single(cdns_ctrl->dev, ctrl_dat,
					      ctrl_dat_size, dir);
		if (dma_mapping_error(cdns_ctrl->dev, dma_ctrl_dat)) {
			dma_unmap_single(cdns_ctrl->dev, dma_buf,
					 buf_size, dir);
			dev_err(cdns_ctrl->dev, "Failed to map DMA buffer\n");
			return -EIO;
		}
	}

	cadence_nand_cdma_desc_prepare(cdns_ctrl, chip_nr, page,
				       dma_buf, dma_ctrl_dat, ctype);

	status = cadence_nand_cdma_send_and_wait(cdns_ctrl, thread_nr);

	dma_unmap_single(cdns_ctrl->dev, dma_buf,
			 buf_size, dir);

	if (ctrl_dat && ctrl_dat_size)
		dma_unmap_single(cdns_ctrl->dev, dma_ctrl_dat,
				 ctrl_dat_size, dir);
	if (status)
		return status;

	return cadence_nand_cdma_finish(cdns_ctrl);
}

static void cadence_nand_set_timings(struct cdns_nand_ctrl *cdns_ctrl,
				     struct cadence_nand_timings *t)
{
	writel_relaxed(t->async_toggle_timings,
		       cdns_ctrl->reg + ASYNC_TOGGLE_TIMINGS);
	writel_relaxed(t->timings0, cdns_ctrl->reg + TIMINGS0);
	writel_relaxed(t->timings1, cdns_ctrl->reg + TIMINGS1);
	writel_relaxed(t->timings2, cdns_ctrl->reg + TIMINGS2);

	if (cdns_ctrl->caps2.is_phy_type_dll)
		writel_relaxed(t->dll_phy_ctrl, cdns_ctrl->reg + DLL_PHY_CTRL);

	writel_relaxed(t->phy_ctrl, cdns_ctrl->reg + PHY_CTRL);

	if (cdns_ctrl->caps2.is_phy_type_dll) {
		writel_relaxed(0, cdns_ctrl->reg + PHY_TSEL);
		writel_relaxed(2, cdns_ctrl->reg + PHY_DQ_TIMING);
		writel_relaxed(t->phy_dqs_timing,
			       cdns_ctrl->reg + PHY_DQS_TIMING);
		writel_relaxed(t->phy_gate_lpbk_ctrl,
			       cdns_ctrl->reg + PHY_GATE_LPBK_CTRL);
		writel_relaxed(PHY_DLL_MASTER_CTRL_BYPASS_MODE,
			       cdns_ctrl->reg + PHY_DLL_MASTER_CTRL);
		writel_relaxed(0, cdns_ctrl->reg + PHY_DLL_SLAVE_CTRL);
	}
}

static int cadence_nand_select_target(struct nand_chip *chip)
{
	struct cdns_nand_ctrl *cdns_ctrl = to_cdns_nand_ctrl(chip->controller);
	struct cdns_nand_chip *cdns_chip = to_cdns_nand_chip(chip);

	if (chip == cdns_ctrl->selected_chip)
		return 0;

	if (cadence_nand_wait_for_value(cdns_ctrl, CTRL_STATUS,
					1000000,
					CTRL_STATUS_CTRL_BUSY, true))
		return -ETIMEDOUT;

	cadence_nand_set_timings(cdns_ctrl, &cdns_chip->timings);

	cadence_nand_set_ecc_strength(cdns_ctrl,
				      cdns_chip->corr_str_idx);

	cadence_nand_set_erase_detection(cdns_ctrl, true,
					 chip->ecc.strength);

	cdns_ctrl->curr_trans_type = -1;
	cdns_ctrl->selected_chip = chip;

	return 0;
}

static int cadence_nand_erase(struct nand_chip *chip, u32 page)
{
	struct cdns_nand_ctrl *cdns_ctrl = to_cdns_nand_ctrl(chip->controller);
	struct cdns_nand_chip *cdns_chip = to_cdns_nand_chip(chip);
	int status;
	u8 thread_nr = cdns_chip->cs[chip->cur_cs];

	cadence_nand_cdma_desc_prepare(cdns_ctrl,
				       cdns_chip->cs[chip->cur_cs],
				       page, 0, 0,
				       CDMA_CT_ERASE);
	status = cadence_nand_cdma_send_and_wait(cdns_ctrl, thread_nr);
	if (status) {
		dev_err(cdns_ctrl->dev, "erase operation failed\n");
		return -EIO;
	}

	status = cadence_nand_cdma_finish(cdns_ctrl);
	if (status)
		return status;

	return 0;
}

static int cadence_nand_read_bbm(struct nand_chip *chip, int page, u8 *buf)
{
	int status;
	struct cdns_nand_ctrl *cdns_ctrl = to_cdns_nand_ctrl(chip->controller);
	struct cdns_nand_chip *cdns_chip = to_cdns_nand_chip(chip);
	struct mtd_info *mtd = nand_to_mtd(chip);

	cadence_nand_prepare_data_size(chip, TT_BBM);

	cadence_nand_set_skip_bytes_conf(cdns_ctrl, 0, 0, 0);

	/*
	 * Read only bad block marker from offset
	 * defined by a memory manufacturer.
	 */
	status = cadence_nand_cdma_transfer(cdns_ctrl,
					    cdns_chip->cs[chip->cur_cs],
					    page, cdns_ctrl->buf, NULL,
					    mtd->oobsize,
					    0, DMA_FROM_DEVICE, false);
	if (status) {
		dev_err(cdns_ctrl->dev, "read BBM failed\n");
		return -EIO;
	}

	memcpy(buf + cdns_chip->bbm_offs, cdns_ctrl->buf, cdns_chip->bbm_len);

	return 0;
}

static int cadence_nand_write_page(struct nand_chip *chip,
				   const u8 *buf, int oob_required,
				   int page)
{
	struct cdns_nand_ctrl *cdns_ctrl = to_cdns_nand_ctrl(chip->controller);
	struct cdns_nand_chip *cdns_chip = to_cdns_nand_chip(chip);
	struct mtd_info *mtd = nand_to_mtd(chip);
	int status;
	u16 marker_val = 0xFFFF;

	status = cadence_nand_select_target(chip);
	if (status)
		return status;

	cadence_nand_set_skip_bytes_conf(cdns_ctrl, cdns_chip->bbm_len,
					 mtd->writesize
					 + cdns_chip->bbm_offs,
					 1);

	if (oob_required) {
		marker_val = *(u16 *)(chip->oob_poi
				      + cdns_chip->bbm_offs);
	} else {
		/* Set oob data to 0xFF. */
		memset(cdns_ctrl->buf + mtd->writesize, 0xFF,
		       cdns_chip->avail_oob_size);
	}

	cadence_nand_set_skip_marker_val(cdns_ctrl, marker_val);

	cadence_nand_prepare_data_size(chip, TT_MAIN_OOB_AREA_EXT);

	if (cadence_nand_dma_buf_ok(cdns_ctrl, buf, mtd->writesize) &&
	    cdns_ctrl->caps2.data_control_supp) {
		u8 *oob;

		if (oob_required)
			oob = chip->oob_poi;
		else
			oob = cdns_ctrl->buf + mtd->writesize;

		status = cadence_nand_cdma_transfer(cdns_ctrl,
						    cdns_chip->cs[chip->cur_cs],
						    page, (void *)buf, oob,
						    mtd->writesize,
						    cdns_chip->avail_oob_size,
						    DMA_TO_DEVICE, true);
		if (status) {
			dev_err(cdns_ctrl->dev, "write page failed\n");
			return -EIO;
		}

		return 0;
	}

	if (oob_required) {
		/* Transfer the data to the oob area. */
		memcpy(cdns_ctrl->buf + mtd->writesize, chip->oob_poi,
		       cdns_chip->avail_oob_size);
	}

	memcpy(cdns_ctrl->buf, buf, mtd->writesize);

	cadence_nand_prepare_data_size(chip, TT_MAIN_OOB_AREAS);

	return cadence_nand_cdma_transfer(cdns_ctrl,
					  cdns_chip->cs[chip->cur_cs],
					  page, cdns_ctrl->buf, NULL,
					  mtd->writesize
					  + cdns_chip->avail_oob_size,
					  0, DMA_TO_DEVICE, true);
}

static int cadence_nand_write_oob(struct nand_chip *chip, int page)
{
	struct cdns_nand_ctrl *cdns_ctrl = to_cdns_nand_ctrl(chip->controller);
	struct mtd_info *mtd = nand_to_mtd(chip);

	memset(cdns_ctrl->buf, 0xFF, mtd->writesize);

	return cadence_nand_write_page(chip, cdns_ctrl->buf, 1, page);
}

static int cadence_nand_write_page_raw(struct nand_chip *chip,
				       const u8 *buf, int oob_required,
				       int page)
{
	struct cdns_nand_ctrl *cdns_ctrl = to_cdns_nand_ctrl(chip->controller);
	struct cdns_nand_chip *cdns_chip = to_cdns_nand_chip(chip);
	struct mtd_info *mtd = nand_to_mtd(chip);
	int writesize = mtd->writesize;
	int oobsize = mtd->oobsize;
	int ecc_steps = chip->ecc.steps;
	int ecc_size = chip->ecc.size;
	int ecc_bytes = chip->ecc.bytes;
	void *tmp_buf = cdns_ctrl->buf;
	int oob_skip = cdns_chip->bbm_len;
	size_t size = writesize + oobsize;
	int i, pos, len;
	int status = 0;

	status = cadence_nand_select_target(chip);
	if (status)
		return status;

	/*
	 * Fill the buffer with 0xff first except the full page transfer.
	 * This simplifies the logic.
	 */
	if (!buf || !oob_required)
		memset(tmp_buf, 0xff, size);

	cadence_nand_set_skip_bytes_conf(cdns_ctrl, 0, 0, 0);

	/* Arrange the buffer for syndrome payload/ecc layout. */
	if (buf) {
		for (i = 0; i < ecc_steps; i++) {
			pos = i * (ecc_size + ecc_bytes);
			len = ecc_size;

			if (pos >= writesize)
				pos += oob_skip;
			else if (pos + len > writesize)
				len = writesize - pos;

			memcpy(tmp_buf + pos, buf, len);
			buf += len;
			if (len < ecc_size) {
				len = ecc_size - len;
				memcpy(tmp_buf + writesize + oob_skip, buf,
				       len);
				buf += len;
			}
		}
	}

	if (oob_required) {
		const u8 *oob = chip->oob_poi;
		u32 oob_data_offset = (cdns_chip->sector_count - 1) *
			(cdns_chip->sector_size + chip->ecc.bytes)
			+ cdns_chip->sector_size + oob_skip;

		/* BBM at the beginning of the OOB area. */
		memcpy(tmp_buf + writesize, oob, oob_skip);

		/* OOB free. */
		memcpy(tmp_buf + oob_data_offset, oob,
		       cdns_chip->avail_oob_size);
		oob += cdns_chip->avail_oob_size;

		/* OOB ECC. */
		for (i = 0; i < ecc_steps; i++) {
			pos = ecc_size + i * (ecc_size + ecc_bytes);
			if (i == (ecc_steps - 1))
				pos += cdns_chip->avail_oob_size;

			len = ecc_bytes;

			if (pos >= writesize)
				pos += oob_skip;
			else if (pos + len > writesize)
				len = writesize - pos;

			memcpy(tmp_buf + pos, oob, len);
			oob += len;
			if (len < ecc_bytes) {
				len = ecc_bytes - len;
				memcpy(tmp_buf + writesize + oob_skip, oob,
				       len);
				oob += len;
			}
		}
	}

	cadence_nand_prepare_data_size(chip, TT_RAW_PAGE);

	return cadence_nand_cdma_transfer(cdns_ctrl,
					  cdns_chip->cs[chip->cur_cs],
					  page, cdns_ctrl->buf, NULL,
					  mtd->writesize +
					  mtd->oobsize,
					  0, DMA_TO_DEVICE, false);
}

static int cadence_nand_write_oob_raw(struct nand_chip *chip,
				      int page)
{
	return cadence_nand_write_page_raw(chip, NULL, true, page);
}

static int cadence_nand_read_page(struct nand_chip *chip,
				  u8 *buf, int oob_required, int page)
{
	struct cdns_nand_ctrl *cdns_ctrl = to_cdns_nand_ctrl(chip->controller);
	struct cdns_nand_chip *cdns_chip = to_cdns_nand_chip(chip);
	struct mtd_info *mtd = nand_to_mtd(chip);
	int status = 0;
	int ecc_err_count = 0;

	status = cadence_nand_select_target(chip);
	if (status)
		return status;

	cadence_nand_set_skip_bytes_conf(cdns_ctrl, cdns_chip->bbm_len,
					 mtd->writesize
					 + cdns_chip->bbm_offs, 1);

	/*
	 * If data buffer can be accessed by DMA and data_control feature
	 * is supported then transfer data and oob directly.
	 */
	if (cadence_nand_dma_buf_ok(cdns_ctrl, buf, mtd->writesize) &&
	    cdns_ctrl->caps2.data_control_supp) {
		u8 *oob;

		if (oob_required)
			oob = chip->oob_poi;
		else
			oob = cdns_ctrl->buf + mtd->writesize;

		cadence_nand_prepare_data_size(chip, TT_MAIN_OOB_AREA_EXT);
		status = cadence_nand_cdma_transfer(cdns_ctrl,
						    cdns_chip->cs[chip->cur_cs],
						    page, buf, oob,
						    mtd->writesize,
						    cdns_chip->avail_oob_size,
						    DMA_FROM_DEVICE, true);
	/* Otherwise use bounce buffer. */
	} else {
		cadence_nand_prepare_data_size(chip, TT_MAIN_OOB_AREAS);
		status = cadence_nand_cdma_transfer(cdns_ctrl,
						    cdns_chip->cs[chip->cur_cs],
						    page, cdns_ctrl->buf,
						    NULL, mtd->writesize
						    + cdns_chip->avail_oob_size,
						    0, DMA_FROM_DEVICE, true);

		memcpy(buf, cdns_ctrl->buf, mtd->writesize);
		if (oob_required)
			memcpy(chip->oob_poi,
			       cdns_ctrl->buf + mtd->writesize,
			       mtd->oobsize);
	}

	switch (status) {
	case STAT_ECC_UNCORR:
		mtd->ecc_stats.failed++;
		ecc_err_count++;
		break;
	case STAT_ECC_CORR:
		ecc_err_count = FIELD_GET(CDMA_CS_MAXERR,
					  cdns_ctrl->cdma_desc->status);
		mtd->ecc_stats.corrected += ecc_err_count;
		break;
	case STAT_ERASED:
	case STAT_OK:
		break;
	default:
		dev_err(cdns_ctrl->dev, "read page failed\n");
		return -EIO;
	}

	if (oob_required)
		if (cadence_nand_read_bbm(chip, page, chip->oob_poi))
			return -EIO;

	return ecc_err_count;
}

/* Reads OOB data from the device. */
static int cadence_nand_read_oob(struct nand_chip *chip, int page)
{
	struct cdns_nand_ctrl *cdns_ctrl = to_cdns_nand_ctrl(chip->controller);

	return cadence_nand_read_page(chip, cdns_ctrl->buf, 1, page);
}

static int cadence_nand_read_page_raw(struct nand_chip *chip,
				      u8 *buf, int oob_required, int page)
{
	struct cdns_nand_ctrl *cdns_ctrl = to_cdns_nand_ctrl(chip->controller);
	struct cdns_nand_chip *cdns_chip = to_cdns_nand_chip(chip);
	struct mtd_info *mtd = nand_to_mtd(chip);
	int oob_skip = cdns_chip->bbm_len;
	int writesize = mtd->writesize;
	int ecc_steps = chip->ecc.steps;
	int ecc_size = chip->ecc.size;
	int ecc_bytes = chip->ecc.bytes;
	void *tmp_buf = cdns_ctrl->buf;
	int i, pos, len;
	int status = 0;

	status = cadence_nand_select_target(chip);
	if (status)
		return status;

	cadence_nand_set_skip_bytes_conf(cdns_ctrl, 0, 0, 0);

	cadence_nand_prepare_data_size(chip, TT_RAW_PAGE);
	status = cadence_nand_cdma_transfer(cdns_ctrl,
					    cdns_chip->cs[chip->cur_cs],
					    page, cdns_ctrl->buf, NULL,
					    mtd->writesize
					    + mtd->oobsize,
					    0, DMA_FROM_DEVICE, false);

	switch (status) {
	case STAT_ERASED:
	case STAT_OK:
		break;
	default:
		dev_err(cdns_ctrl->dev, "read raw page failed\n");
		return -EIO;
	}

	/* Arrange the buffer for syndrome payload/ecc layout. */
	if (buf) {
		for (i = 0; i < ecc_steps; i++) {
			pos = i * (ecc_size + ecc_bytes);
			len = ecc_size;

			if (pos >= writesize)
				pos += oob_skip;
			else if (pos + len > writesize)
				len = writesize - pos;

			memcpy(buf, tmp_buf + pos, len);
			buf += len;
			if (len < ecc_size) {
				len = ecc_size - len;
				memcpy(buf, tmp_buf + writesize + oob_skip,
				       len);
				buf += len;
			}
		}
	}

	if (oob_required) {
		u8 *oob = chip->oob_poi;
		u32 oob_data_offset = (cdns_chip->sector_count - 1) *
			(cdns_chip->sector_size + chip->ecc.bytes)
			+ cdns_chip->sector_size + oob_skip;

		/* OOB free. */
		memcpy(oob, tmp_buf + oob_data_offset,
		       cdns_chip->avail_oob_size);

		/* BBM at the beginning of the OOB area. */
		memcpy(oob, tmp_buf + writesize, oob_skip);

		oob += cdns_chip->avail_oob_size;

		/* OOB ECC */
		for (i = 0; i < ecc_steps; i++) {
			pos = ecc_size + i * (ecc_size + ecc_bytes);
			len = ecc_bytes;

			if (i == (ecc_steps - 1))
				pos += cdns_chip->avail_oob_size;

			if (pos >= writesize)
				pos += oob_skip;
			else if (pos + len > writesize)
				len = writesize - pos;

			memcpy(oob, tmp_buf + pos, len);
			oob += len;
			if (len < ecc_bytes) {
				len = ecc_bytes - len;
				memcpy(oob, tmp_buf + writesize + oob_skip,
				       len);
				oob += len;
			}
		}
	}

	return 0;
}

static int cadence_nand_read_oob_raw(struct nand_chip *chip,
				     int page)
{
	return cadence_nand_read_page_raw(chip, NULL, true, page);
}

static void cadence_nand_slave_dma_transfer_finished(void *data)
{
	struct completion *finished = data;

	complete(finished);
}

static int cadence_nand_slave_dma_transfer(struct cdns_nand_ctrl *cdns_ctrl,
					   void *buf,
					   dma_addr_t dev_dma, size_t len,
					   enum dma_data_direction dir)
{
	DECLARE_COMPLETION_ONSTACK(finished);
	struct dma_chan *chan;
	struct dma_device *dma_dev;
	dma_addr_t src_dma, dst_dma, buf_dma;
	struct dma_async_tx_descriptor *tx;
	dma_cookie_t cookie;

	chan = cdns_ctrl->dmac;
	dma_dev = chan->device;

	buf_dma = dma_map_single(dma_dev->dev, buf, len, dir);
	if (dma_mapping_error(dma_dev->dev, buf_dma)) {
		dev_err(cdns_ctrl->dev, "Failed to map DMA buffer\n");
		goto err;
	}

	if (dir == DMA_FROM_DEVICE) {
		src_dma = cdns_ctrl->io.dma;
		dst_dma = buf_dma;
	} else {
		src_dma = buf_dma;
		dst_dma = cdns_ctrl->io.dma;
	}

	tx = dmaengine_prep_dma_memcpy(cdns_ctrl->dmac, dst_dma, src_dma, len,
				       DMA_CTRL_ACK | DMA_PREP_INTERRUPT);
	if (!tx) {
		dev_err(cdns_ctrl->dev, "Failed to prepare DMA memcpy\n");
		goto err_unmap;
	}

	tx->callback = cadence_nand_slave_dma_transfer_finished;
	tx->callback_param = &finished;

	cookie = dmaengine_submit(tx);
	if (dma_submit_error(cookie)) {
		dev_err(cdns_ctrl->dev, "Failed to do DMA tx_submit\n");
		goto err_unmap;
	}

	dma_async_issue_pending(cdns_ctrl->dmac);
	wait_for_completion(&finished);

	dma_unmap_single(cdns_ctrl->dev, buf_dma, len, dir);

	return 0;

err_unmap:
	dma_unmap_single(cdns_ctrl->dev, buf_dma, len, dir);

err:
	dev_dbg(cdns_ctrl->dev, "Fall back to CPU I/O\n");

	return -EIO;
}

static int cadence_nand_read_buf(struct cdns_nand_ctrl *cdns_ctrl,
				 u8 *buf, int len)
{
	u8 thread_nr = 0;
	u32 sdma_size;
	int status;

	/* Wait until slave DMA interface is ready to data transfer. */
	status = cadence_nand_wait_on_sdma(cdns_ctrl, &thread_nr, &sdma_size);
	if (status)
		return status;

	if (!cdns_ctrl->caps1->has_dma) {
		u8 data_dma_width = cdns_ctrl->caps2.data_dma_width;

		int len_in_words = (data_dma_width == 4) ? len >> 2 : len >> 3;

		/* read alignment data */
		if (data_dma_width == 4)
			ioread32_rep(cdns_ctrl->io.virt, buf, len_in_words);
#ifdef CONFIG_64BIT
		else
			readsq(cdns_ctrl->io.virt, buf, len_in_words);
#endif

		if (sdma_size > len) {
			int read_bytes = (data_dma_width == 4) ?
				len_in_words << 2 : len_in_words << 3;

			/* read rest data from slave DMA interface if any */
			if (data_dma_width == 4)
				ioread32_rep(cdns_ctrl->io.virt,
					     cdns_ctrl->buf,
					     sdma_size / 4 - len_in_words);
#ifdef CONFIG_64BIT
			else
				readsq(cdns_ctrl->io.virt, cdns_ctrl->buf,
				       sdma_size / 8 - len_in_words);
#endif

			/* copy rest of data */
			memcpy(buf + read_bytes, cdns_ctrl->buf,
			       len - read_bytes);
		}
		return 0;
	}

	if (cadence_nand_dma_buf_ok(cdns_ctrl, buf, len)) {
		status = cadence_nand_slave_dma_transfer(cdns_ctrl, buf,
							 cdns_ctrl->io.dma,
							 len, DMA_FROM_DEVICE);
		if (status == 0)
			return 0;

		dev_warn(cdns_ctrl->dev,
			 "Slave DMA transfer failed. Try again using bounce buffer.");
	}

	/* If DMA transfer is not possible or failed then use bounce buffer. */
	status = cadence_nand_slave_dma_transfer(cdns_ctrl, cdns_ctrl->buf,
						 cdns_ctrl->io.dma,
						 sdma_size, DMA_FROM_DEVICE);

	if (status) {
		dev_err(cdns_ctrl->dev, "Slave DMA transfer failed");
		return status;
	}

	memcpy(buf, cdns_ctrl->buf, len);

	return 0;
}

static int cadence_nand_write_buf(struct cdns_nand_ctrl *cdns_ctrl,
				  const u8 *buf, int len)
{
	u8 thread_nr = 0;
	u32 sdma_size;
	int status;

	/* Wait until slave DMA interface is ready to data transfer. */
	status = cadence_nand_wait_on_sdma(cdns_ctrl, &thread_nr, &sdma_size);
	if (status)
		return status;

	if (!cdns_ctrl->caps1->has_dma) {
		u8 data_dma_width = cdns_ctrl->caps2.data_dma_width;

		int len_in_words = (data_dma_width == 4) ? len >> 2 : len >> 3;

		if (data_dma_width == 4)
			iowrite32_rep(cdns_ctrl->io.virt, buf, len_in_words);
#ifdef CONFIG_64BIT
		else
			writesq(cdns_ctrl->io.virt, buf, len_in_words);
#endif

		if (sdma_size > len) {
			int written_bytes = (data_dma_width == 4) ?
				len_in_words << 2 : len_in_words << 3;

			/* copy rest of data */
			memcpy(cdns_ctrl->buf, buf + written_bytes,
			       len - written_bytes);

			/* write all expected by nand controller data */
			if (data_dma_width == 4)
				iowrite32_rep(cdns_ctrl->io.virt,
					      cdns_ctrl->buf,
					      sdma_size / 4 - len_in_words);
#ifdef CONFIG_64BIT
			else
				writesq(cdns_ctrl->io.virt, cdns_ctrl->buf,
					sdma_size / 8 - len_in_words);
#endif
		}

		return 0;
	}

	if (cadence_nand_dma_buf_ok(cdns_ctrl, buf, len)) {
		status = cadence_nand_slave_dma_transfer(cdns_ctrl, (void *)buf,
							 cdns_ctrl->io.dma,
							 len, DMA_TO_DEVICE);
		if (status == 0)
			return 0;

		dev_warn(cdns_ctrl->dev,
			 "Slave DMA transfer failed. Try again using bounce buffer.");
	}

	/* If DMA transfer is not possible or failed then use bounce buffer. */
	memcpy(cdns_ctrl->buf, buf, len);

	status = cadence_nand_slave_dma_transfer(cdns_ctrl, cdns_ctrl->buf,
						 cdns_ctrl->io.dma,
						 sdma_size, DMA_TO_DEVICE);

	if (status)
		dev_err(cdns_ctrl->dev, "Slave DMA transfer failed");

	return status;
}

static int cadence_nand_force_byte_access(struct nand_chip *chip,
					  bool force_8bit)
{
	struct cdns_nand_ctrl *cdns_ctrl = to_cdns_nand_ctrl(chip->controller);

	/*
	 * Callers of this function do not verify if the NAND is using a 16-bit
	 * an 8-bit bus for normal operations, so we need to take care of that
	 * here by leaving the configuration unchanged if the NAND does not have
	 * the NAND_BUSWIDTH_16 flag set.
	 */
	if (!(chip->options & NAND_BUSWIDTH_16))
		return 0;

	return cadence_nand_set_access_width16(cdns_ctrl, !force_8bit);
}

static int cadence_nand_cmd_opcode(struct nand_chip *chip,
				   const struct nand_subop *subop)
{
	struct cdns_nand_ctrl *cdns_ctrl = to_cdns_nand_ctrl(chip->controller);
	struct cdns_nand_chip *cdns_chip = to_cdns_nand_chip(chip);
	const struct nand_op_instr *instr;
	unsigned int op_id = 0;
	u64 mini_ctrl_cmd = 0;
	int ret;

	instr = &subop->instrs[op_id];

	if (instr->delay_ns > 0)
		mini_ctrl_cmd |= GCMD_LAY_TWB;

	mini_ctrl_cmd |= FIELD_PREP(GCMD_LAY_INSTR,
				    GCMD_LAY_INSTR_CMD);
	mini_ctrl_cmd |= FIELD_PREP(GCMD_LAY_INPUT_CMD,
				    instr->ctx.cmd.opcode);

	ret = cadence_nand_generic_cmd_send(cdns_ctrl,
					    cdns_chip->cs[chip->cur_cs],
					    mini_ctrl_cmd);
	if (ret)
		dev_err(cdns_ctrl->dev, "send cmd %x failed\n",
			instr->ctx.cmd.opcode);

	return ret;
}

static int cadence_nand_cmd_address(struct nand_chip *chip,
				    const struct nand_subop *subop)
{
	struct cdns_nand_ctrl *cdns_ctrl = to_cdns_nand_ctrl(chip->controller);
	struct cdns_nand_chip *cdns_chip = to_cdns_nand_chip(chip);
	const struct nand_op_instr *instr;
	unsigned int op_id = 0;
	u64 mini_ctrl_cmd = 0;
	unsigned int offset, naddrs;
	u64 address = 0;
	const u8 *addrs;
	int ret;
	int i;

	instr = &subop->instrs[op_id];

	if (instr->delay_ns > 0)
		mini_ctrl_cmd |= GCMD_LAY_TWB;

	mini_ctrl_cmd |= FIELD_PREP(GCMD_LAY_INSTR,
				    GCMD_LAY_INSTR_ADDR);

	offset = nand_subop_get_addr_start_off(subop, op_id);
	naddrs = nand_subop_get_num_addr_cyc(subop, op_id);
	addrs = &instr->ctx.addr.addrs[offset];

	for (i = 0; i < naddrs; i++)
		address |= (u64)addrs[i] << (8 * i);

	mini_ctrl_cmd |= FIELD_PREP(GCMD_LAY_INPUT_ADDR,
				    address);
	mini_ctrl_cmd |= FIELD_PREP(GCMD_LAY_INPUT_ADDR_SIZE,
				    naddrs - 1);

	ret = cadence_nand_generic_cmd_send(cdns_ctrl,
					    cdns_chip->cs[chip->cur_cs],
					    mini_ctrl_cmd);
	if (ret)
		dev_err(cdns_ctrl->dev, "send address %llx failed\n", address);

	return ret;
}

static int cadence_nand_cmd_erase(struct nand_chip *chip,
				  const struct nand_subop *subop)
{
	unsigned int op_id;

	if (subop->instrs[0].ctx.cmd.opcode == NAND_CMD_ERASE1) {
		int i;
		const struct nand_op_instr *instr = NULL;
		unsigned int offset, naddrs;
		const u8 *addrs;
		u32 page = 0;

		instr = &subop->instrs[1];
		offset = nand_subop_get_addr_start_off(subop, 1);
		naddrs = nand_subop_get_num_addr_cyc(subop, 1);
		addrs = &instr->ctx.addr.addrs[offset];

		for (i = 0; i < naddrs; i++)
			page |= (u32)addrs[i] << (8 * i);

		return cadence_nand_erase(chip, page);
	}

	/*
	 * If it is not an erase operation then handle operation
	 * by calling exec_op function.
	 */
	for (op_id = 0; op_id < subop->ninstrs; op_id++) {
		int ret;
		const struct nand_operation nand_op = {
			.cs = chip->cur_cs,
			.instrs =  &subop->instrs[op_id],
			.ninstrs = 1};
		ret = chip->controller->ops->exec_op(chip, &nand_op, false);
		if (ret)
			return ret;
	}

	return 0;
}

static int cadence_nand_cmd_data(struct nand_chip *chip,
				 const struct nand_subop *subop)
{
	struct cdns_nand_ctrl *cdns_ctrl = to_cdns_nand_ctrl(chip->controller);
	struct cdns_nand_chip *cdns_chip = to_cdns_nand_chip(chip);
	const struct nand_op_instr *instr;
	unsigned int offset, op_id = 0;
	u64 mini_ctrl_cmd = 0;
	int len = 0;
	int ret;

	instr = &subop->instrs[op_id];

	if (instr->delay_ns > 0)
		mini_ctrl_cmd |= GCMD_LAY_TWB;

	mini_ctrl_cmd |= FIELD_PREP(GCMD_LAY_INSTR,
				    GCMD_LAY_INSTR_DATA);

	if (instr->type == NAND_OP_DATA_OUT_INSTR)
		mini_ctrl_cmd |= FIELD_PREP(GCMD_DIR,
					    GCMD_DIR_WRITE);

	len = nand_subop_get_data_len(subop, op_id);
	offset = nand_subop_get_data_start_off(subop, op_id);
	mini_ctrl_cmd |= FIELD_PREP(GCMD_SECT_CNT, 1);
	mini_ctrl_cmd |= FIELD_PREP(GCMD_LAST_SIZE, len);
	if (instr->ctx.data.force_8bit) {
		ret = cadence_nand_force_byte_access(chip, true);
		if (ret) {
			dev_err(cdns_ctrl->dev,
				"cannot change byte access generic data cmd failed\n");
			return ret;
		}
	}

	ret = cadence_nand_generic_cmd_send(cdns_ctrl,
					    cdns_chip->cs[chip->cur_cs],
					    mini_ctrl_cmd);
	if (ret) {
		dev_err(cdns_ctrl->dev, "send generic data cmd failed\n");
		return ret;
	}

	if (instr->type == NAND_OP_DATA_IN_INSTR) {
		void *buf = instr->ctx.data.buf.in + offset;

		ret = cadence_nand_read_buf(cdns_ctrl, buf, len);
	} else {
		const void *buf = instr->ctx.data.buf.out + offset;

		ret = cadence_nand_write_buf(cdns_ctrl, buf, len);
	}

	if (ret) {
		dev_err(cdns_ctrl->dev, "data transfer failed for generic command\n");
		return ret;
	}

	if (instr->ctx.data.force_8bit) {
		ret = cadence_nand_force_byte_access(chip, false);
		if (ret) {
			dev_err(cdns_ctrl->dev,
				"cannot change byte access generic data cmd failed\n");
		}
	}

	return ret;
}

static int cadence_nand_cmd_waitrdy(struct nand_chip *chip,
				    const struct nand_subop *subop)
{
	int status;
	unsigned int op_id = 0;
	struct cdns_nand_ctrl *cdns_ctrl = to_cdns_nand_ctrl(chip->controller);
	struct cdns_nand_chip *cdns_chip = to_cdns_nand_chip(chip);
	const struct nand_op_instr *instr = &subop->instrs[op_id];
	u32 timeout_us = instr->ctx.waitrdy.timeout_ms * 1000;

	status = cadence_nand_wait_for_value(cdns_ctrl, RBN_SETINGS,
					     timeout_us,
					     BIT(cdns_chip->cs[chip->cur_cs]),
					     false);
	return status;
}

static const struct nand_op_parser cadence_nand_op_parser = NAND_OP_PARSER(
	NAND_OP_PARSER_PATTERN(
		cadence_nand_cmd_erase,
		NAND_OP_PARSER_PAT_CMD_ELEM(false),
		NAND_OP_PARSER_PAT_ADDR_ELEM(false, MAX_ERASE_ADDRESS_CYC),
		NAND_OP_PARSER_PAT_CMD_ELEM(false),
		NAND_OP_PARSER_PAT_WAITRDY_ELEM(false)),
	NAND_OP_PARSER_PATTERN(
		cadence_nand_cmd_opcode,
		NAND_OP_PARSER_PAT_CMD_ELEM(false)),
	NAND_OP_PARSER_PATTERN(
		cadence_nand_cmd_address,
		NAND_OP_PARSER_PAT_ADDR_ELEM(false, MAX_ADDRESS_CYC)),
	NAND_OP_PARSER_PATTERN(
		cadence_nand_cmd_data,
		NAND_OP_PARSER_PAT_DATA_IN_ELEM(false, MAX_DATA_SIZE)),
	NAND_OP_PARSER_PATTERN(
		cadence_nand_cmd_data,
		NAND_OP_PARSER_PAT_DATA_OUT_ELEM(false, MAX_DATA_SIZE)),
	NAND_OP_PARSER_PATTERN(
		cadence_nand_cmd_waitrdy,
		NAND_OP_PARSER_PAT_WAITRDY_ELEM(false))
	);

static int cadence_nand_exec_op(struct nand_chip *chip,
				const struct nand_operation *op,
				bool check_only)
{
	if (!check_only) {
		int status = cadence_nand_select_target(chip);

		if (status)
			return status;
	}

	return nand_op_parser_exec_op(chip, &cadence_nand_op_parser, op,
				      check_only);
}

static int cadence_nand_ooblayout_free(struct mtd_info *mtd, int section,
				       struct mtd_oob_region *oobregion)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct cdns_nand_chip *cdns_chip = to_cdns_nand_chip(chip);

	if (section)
		return -ERANGE;

	oobregion->offset = cdns_chip->bbm_len;
	oobregion->length = cdns_chip->avail_oob_size
		- cdns_chip->bbm_len;

	return 0;
}

static int cadence_nand_ooblayout_ecc(struct mtd_info *mtd, int section,
				      struct mtd_oob_region *oobregion)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct cdns_nand_chip *cdns_chip = to_cdns_nand_chip(chip);

	if (section)
		return -ERANGE;

	oobregion->offset = cdns_chip->avail_oob_size;
	oobregion->length = chip->ecc.total;

	return 0;
}

static const struct mtd_ooblayout_ops cadence_nand_ooblayout_ops = {
	.free = cadence_nand_ooblayout_free,
	.ecc = cadence_nand_ooblayout_ecc,
};

static int calc_cycl(u32 timing, u32 clock)
{
	if (timing == 0 || clock == 0)
		return 0;

	if ((timing % clock) > 0)
		return timing / clock;
	else
		return timing / clock - 1;
}

/* Calculate max data valid window. */
static inline u32 calc_tdvw_max(u32 trp_cnt, u32 clk_period, u32 trhoh_min,
				u32 board_delay_skew_min, u32 ext_mode)
{
	if (ext_mode == 0)
		clk_period /= 2;

	return (trp_cnt + 1) * clk_period + trhoh_min +
		board_delay_skew_min;
}

/* Calculate data valid window. */
static inline u32 calc_tdvw(u32 trp_cnt, u32 clk_period, u32 trhoh_min,
			    u32 trea_max, u32 ext_mode)
{
	if (ext_mode == 0)
		clk_period /= 2;

	return (trp_cnt + 1) * clk_period + trhoh_min - trea_max;
}

static int
cadence_nand_setup_interface(struct nand_chip *chip, int chipnr,
			     const struct nand_interface_config *conf)
{
	const struct nand_sdr_timings *sdr;
	struct cdns_nand_ctrl *cdns_ctrl = to_cdns_nand_ctrl(chip->controller);
	struct cdns_nand_chip *cdns_chip = to_cdns_nand_chip(chip);
	struct cadence_nand_timings *t = &cdns_chip->timings;
	u32 reg;
	u32 board_delay = cdns_ctrl->board_delay;
	u32 clk_period = DIV_ROUND_DOWN_ULL(1000000000000ULL,
					    cdns_ctrl->nf_clk_rate);
	u32 tceh_cnt, tcs_cnt, tadl_cnt, tccs_cnt;
	u32 tfeat_cnt, trhz_cnt, tvdly_cnt;
	u32 trhw_cnt, twb_cnt, twh_cnt = 0, twhr_cnt;
	u32 twp_cnt = 0, trp_cnt = 0, trh_cnt = 0;
	u32 if_skew = cdns_ctrl->caps1->if_skew;
	u32 board_delay_skew_min = board_delay - if_skew;
	u32 board_delay_skew_max = board_delay + if_skew;
	u32 dqs_sampl_res, phony_dqs_mod;
	u32 tdvw, tdvw_min, tdvw_max;
	u32 ext_rd_mode, ext_wr_mode;
	u32 dll_phy_dqs_timing = 0, phony_dqs_timing = 0, rd_del_sel = 0;
	u32 sampling_point;

	sdr = nand_get_sdr_timings(conf);
	if (IS_ERR(sdr))
		return PTR_ERR(sdr);

	memset(t, 0, sizeof(*t));
	/* Sampling point calculation. */

	if (cdns_ctrl->caps2.is_phy_type_dll)
		phony_dqs_mod = 2;
	else
		phony_dqs_mod = 1;

	dqs_sampl_res = clk_period / phony_dqs_mod;

	tdvw_min = sdr->tREA_max + board_delay_skew_max;
	/*
	 * The idea of those calculation is to get the optimum value
	 * for tRP and tRH timings. If it is NOT possible to sample data
	 * with optimal tRP/tRH settings, the parameters will be extended.
	 * If clk_period is 50ns (the lowest value) this condition is met
	 * for SDR timing modes 1, 2, 3, 4 and 5.
	 * If clk_period is 20ns the condition is met only for SDR timing
	 * mode 5.
	 */
	if (sdr->tRC_min <= clk_period &&
	    sdr->tRP_min <= (clk_period / 2) &&
	    sdr->tREH_min <= (clk_period / 2)) {
		/* Performance mode. */
		ext_rd_mode = 0;
		tdvw = calc_tdvw(trp_cnt, clk_period, sdr->tRHOH_min,
				 sdr->tREA_max, ext_rd_mode);
		tdvw_max = calc_tdvw_max(trp_cnt, clk_period, sdr->tRHOH_min,
					 board_delay_skew_min,
					 ext_rd_mode);
		/*
		 * Check if data valid window and sampling point can be found
		 * and is not on the edge (ie. we have hold margin).
		 * If not extend the tRP timings.
		 */
		if (tdvw > 0) {
			if (tdvw_max <= tdvw_min ||
			    (tdvw_max % dqs_sampl_res) == 0) {
				/*
				 * No valid sampling point so the RE pulse need
				 * to be widen widening by half clock cycle.
				 */
				ext_rd_mode = 1;
			}
		} else {
			/*
			 * There is no valid window
			 * to be able to sample data the tRP need to be widen.
			 * Very safe calculations are performed here.
			 */
			trp_cnt = (sdr->tREA_max + board_delay_skew_max
				   + dqs_sampl_res) / clk_period;
			ext_rd_mode = 1;
		}

	} else {
		/* Extended read mode. */
		u32 trh;

		ext_rd_mode = 1;
		trp_cnt = calc_cycl(sdr->tRP_min, clk_period);
		trh = sdr->tRC_min - ((trp_cnt + 1) * clk_period);
		if (sdr->tREH_min >= trh)
			trh_cnt = calc_cycl(sdr->tREH_min, clk_period);
		else
			trh_cnt = calc_cycl(trh, clk_period);

		tdvw = calc_tdvw(trp_cnt, clk_period, sdr->tRHOH_min,
				 sdr->tREA_max, ext_rd_mode);
		/*
		 * Check if data valid window and sampling point can be found
		 * or if it is at the edge check if previous is valid
		 * - if not extend the tRP timings.
		 */
		if (tdvw > 0) {
			tdvw_max = calc_tdvw_max(trp_cnt, clk_period,
						 sdr->tRHOH_min,
						 board_delay_skew_min,
						 ext_rd_mode);

			if ((((tdvw_max / dqs_sampl_res)
			      * dqs_sampl_res) <= tdvw_min) ||
			    (((tdvw_max % dqs_sampl_res) == 0) &&
			     (((tdvw_max / dqs_sampl_res - 1)
			       * dqs_sampl_res) <= tdvw_min))) {
				/*
				 * Data valid window width is lower than
				 * sampling resolution and do not hit any
				 * sampling point to be sure the sampling point
				 * will be found the RE low pulse width will be
				 *  extended by one clock cycle.
				 */
				trp_cnt = trp_cnt + 1;
			}
		} else {
			/*
			 * There is no valid window to be able to sample data.
			 * The tRP need to be widen.
			 * Very safe calculations are performed here.
			 */
			trp_cnt = (sdr->tREA_max + board_delay_skew_max
				   + dqs_sampl_res) / clk_period;
		}
	}

	tdvw_max = calc_tdvw_max(trp_cnt, clk_period,
				 sdr->tRHOH_min,
				 board_delay_skew_min, ext_rd_mode);

	if (sdr->tWC_min <= clk_period &&
	    (sdr->tWP_min + if_skew) <= (clk_period / 2) &&
	    (sdr->tWH_min + if_skew) <= (clk_period / 2)) {
		ext_wr_mode = 0;
	} else {
		u32 twh;

		ext_wr_mode = 1;
		twp_cnt = calc_cycl(sdr->tWP_min + if_skew, clk_period);
		if ((twp_cnt + 1) * clk_period < (sdr->tALS_min + if_skew))
			twp_cnt = calc_cycl(sdr->tALS_min + if_skew,
					    clk_period);

		twh = (sdr->tWC_min - (twp_cnt + 1) * clk_period);
		if (sdr->tWH_min >= twh)
			twh = sdr->tWH_min;

		twh_cnt = calc_cycl(twh + if_skew, clk_period);
	}

	reg = FIELD_PREP(ASYNC_TOGGLE_TIMINGS_TRH, trh_cnt);
	reg |= FIELD_PREP(ASYNC_TOGGLE_TIMINGS_TRP, trp_cnt);
	reg |= FIELD_PREP(ASYNC_TOGGLE_TIMINGS_TWH, twh_cnt);
	reg |= FIELD_PREP(ASYNC_TOGGLE_TIMINGS_TWP, twp_cnt);
	t->async_toggle_timings = reg;
	dev_dbg(cdns_ctrl->dev, "ASYNC_TOGGLE_TIMINGS_SDR\t%x\n", reg);

	tadl_cnt = calc_cycl((sdr->tADL_min + if_skew), clk_period);
	tccs_cnt = calc_cycl((sdr->tCCS_min + if_skew), clk_period);
	twhr_cnt = calc_cycl((sdr->tWHR_min + if_skew), clk_period);
	trhw_cnt = calc_cycl((sdr->tRHW_min + if_skew), clk_period);
	reg = FIELD_PREP(TIMINGS0_TADL, tadl_cnt);

	/*
	 * If timing exceeds delay field in timing register
	 * then use maximum value.
	 */
	if (FIELD_FIT(TIMINGS0_TCCS, tccs_cnt))
		reg |= FIELD_PREP(TIMINGS0_TCCS, tccs_cnt);
	else
		reg |= TIMINGS0_TCCS;

	reg |= FIELD_PREP(TIMINGS0_TWHR, twhr_cnt);
	reg |= FIELD_PREP(TIMINGS0_TRHW, trhw_cnt);
	t->timings0 = reg;
	dev_dbg(cdns_ctrl->dev, "TIMINGS0_SDR\t%x\n", reg);

	/* The following is related to single signal so skew is not needed. */
	trhz_cnt = calc_cycl(sdr->tRHZ_max, clk_period);
	trhz_cnt = trhz_cnt + 1;
	twb_cnt = calc_cycl((sdr->tWB_max + board_delay), clk_period);
	/*
	 * Because of the two stage syncflop the value must be increased by 3
	 * first value is related with sync, second value is related
	 * with output if delay.
	 */
	twb_cnt = twb_cnt + 3 + 5;
	/*
	 * The following is related to the we edge of the random data input
	 * sequence so skew is not needed.
	 */
	tvdly_cnt = calc_cycl(500000 + if_skew, clk_period);
	reg = FIELD_PREP(TIMINGS1_TRHZ, trhz_cnt);
	reg |= FIELD_PREP(TIMINGS1_TWB, twb_cnt);
	reg |= FIELD_PREP(TIMINGS1_TVDLY, tvdly_cnt);
	t->timings1 = reg;
	dev_dbg(cdns_ctrl->dev, "TIMINGS1_SDR\t%x\n", reg);

	tfeat_cnt = calc_cycl(sdr->tFEAT_max, clk_period);
	if (tfeat_cnt < twb_cnt)
		tfeat_cnt = twb_cnt;

	tceh_cnt = calc_cycl(sdr->tCEH_min, clk_period);
	tcs_cnt = calc_cycl((sdr->tCS_min + if_skew), clk_period);

	reg = FIELD_PREP(TIMINGS2_TFEAT, tfeat_cnt);
	reg |= FIELD_PREP(TIMINGS2_CS_HOLD_TIME, tceh_cnt);
	reg |= FIELD_PREP(TIMINGS2_CS_SETUP_TIME, tcs_cnt);
	t->timings2 = reg;
	dev_dbg(cdns_ctrl->dev, "TIMINGS2_SDR\t%x\n", reg);

	if (cdns_ctrl->caps2.is_phy_type_dll) {
		reg = DLL_PHY_CTRL_DLL_RST_N;
		if (ext_wr_mode)
			reg |= DLL_PHY_CTRL_EXTENDED_WR_MODE;
		if (ext_rd_mode)
			reg |= DLL_PHY_CTRL_EXTENDED_RD_MODE;

		reg |= FIELD_PREP(DLL_PHY_CTRL_RS_HIGH_WAIT_CNT, 7);
		reg |= FIELD_PREP(DLL_PHY_CTRL_RS_IDLE_CNT, 7);
		t->dll_phy_ctrl = reg;
		dev_dbg(cdns_ctrl->dev, "DLL_PHY_CTRL_SDR\t%x\n", reg);
	}

	/* Sampling point calculation. */
	if ((tdvw_max % dqs_sampl_res) > 0)
		sampling_point = tdvw_max / dqs_sampl_res;
	else
		sampling_point = (tdvw_max / dqs_sampl_res - 1);

	if (sampling_point * dqs_sampl_res > tdvw_min) {
		dll_phy_dqs_timing =
			FIELD_PREP(PHY_DQS_TIMING_DQS_SEL_OE_END, 4);
		dll_phy_dqs_timing |= PHY_DQS_TIMING_USE_PHONY_DQS;
		phony_dqs_timing = sampling_point / phony_dqs_mod;

		if ((sampling_point % 2) > 0) {
			dll_phy_dqs_timing |= PHY_DQS_TIMING_PHONY_DQS_SEL;
			if ((tdvw_max % dqs_sampl_res) == 0)
				/*
				 * Calculation for sampling point at the edge
				 * of data and being odd number.
				 */
				phony_dqs_timing = (tdvw_max / dqs_sampl_res)
					/ phony_dqs_mod - 1;

			if (!cdns_ctrl->caps2.is_phy_type_dll)
				phony_dqs_timing--;

		} else {
			phony_dqs_timing--;
		}
		rd_del_sel = phony_dqs_timing + 3;
	} else {
		dev_warn(cdns_ctrl->dev,
			 "ERROR : cannot find valid sampling point\n");
	}

	reg = FIELD_PREP(PHY_CTRL_PHONY_DQS, phony_dqs_timing);
	if (cdns_ctrl->caps2.is_phy_type_dll)
		reg  |= PHY_CTRL_SDR_DQS;
	t->phy_ctrl = reg;
	dev_dbg(cdns_ctrl->dev, "PHY_CTRL_REG_SDR\t%x\n", reg);

	if (cdns_ctrl->caps2.is_phy_type_dll) {
		dev_dbg(cdns_ctrl->dev, "PHY_TSEL_REG_SDR\t%x\n", 0);
		dev_dbg(cdns_ctrl->dev, "PHY_DQ_TIMING_REG_SDR\t%x\n", 2);
		dev_dbg(cdns_ctrl->dev, "PHY_DQS_TIMING_REG_SDR\t%x\n",
			dll_phy_dqs_timing);
		t->phy_dqs_timing = dll_phy_dqs_timing;

		reg = FIELD_PREP(PHY_GATE_LPBK_CTRL_RDS, rd_del_sel);
		dev_dbg(cdns_ctrl->dev, "PHY_GATE_LPBK_CTRL_REG_SDR\t%x\n",
			reg);
		t->phy_gate_lpbk_ctrl = reg;

		dev_dbg(cdns_ctrl->dev, "PHY_DLL_MASTER_CTRL_REG_SDR\t%lx\n",
			PHY_DLL_MASTER_CTRL_BYPASS_MODE);
		dev_dbg(cdns_ctrl->dev, "PHY_DLL_SLAVE_CTRL_REG_SDR\t%x\n", 0);
	}

	return 0;
}

static int cadence_nand_attach_chip(struct nand_chip *chip)
{
	struct cdns_nand_ctrl *cdns_ctrl = to_cdns_nand_ctrl(chip->controller);
	struct cdns_nand_chip *cdns_chip = to_cdns_nand_chip(chip);
	u32 ecc_size;
	struct mtd_info *mtd = nand_to_mtd(chip);
	int ret;

	if (chip->options & NAND_BUSWIDTH_16) {
		ret = cadence_nand_set_access_width16(cdns_ctrl, true);
		if (ret)
			return ret;
	}

	chip->bbt_options |= NAND_BBT_USE_FLASH;
	chip->bbt_options |= NAND_BBT_NO_OOB;
	chip->ecc.engine_type = NAND_ECC_ENGINE_TYPE_ON_HOST;

	chip->options |= NAND_NO_SUBPAGE_WRITE;

	cdns_chip->bbm_offs = chip->badblockpos;
	cdns_chip->bbm_offs &= ~0x01;
	/* this value should be even number */
	cdns_chip->bbm_len = 2;

	ret = nand_ecc_choose_conf(chip,
				   &cdns_ctrl->ecc_caps,
				   mtd->oobsize - cdns_chip->bbm_len);
	if (ret) {
		dev_err(cdns_ctrl->dev, "ECC configuration failed\n");
		return ret;
	}

	dev_dbg(cdns_ctrl->dev,
		"chosen ECC settings: step=%d, strength=%d, bytes=%d\n",
		chip->ecc.size, chip->ecc.strength, chip->ecc.bytes);

	/* Error correction configuration. */
	cdns_chip->sector_size = chip->ecc.size;
	cdns_chip->sector_count = mtd->writesize / cdns_chip->sector_size;
	ecc_size = cdns_chip->sector_count * chip->ecc.bytes;

	cdns_chip->avail_oob_size = mtd->oobsize - ecc_size;

	if (cdns_chip->avail_oob_size > cdns_ctrl->bch_metadata_size)
		cdns_chip->avail_oob_size = cdns_ctrl->bch_metadata_size;

	if ((cdns_chip->avail_oob_size + cdns_chip->bbm_len + ecc_size)
	    > mtd->oobsize)
		cdns_chip->avail_oob_size -= 4;

	ret = cadence_nand_get_ecc_strength_idx(cdns_ctrl, chip->ecc.strength);
	if (ret < 0)
		return -EINVAL;

	cdns_chip->corr_str_idx = (u8)ret;

	if (cadence_nand_wait_for_value(cdns_ctrl, CTRL_STATUS,
					1000000,
					CTRL_STATUS_CTRL_BUSY, true))
		return -ETIMEDOUT;

	cadence_nand_set_ecc_strength(cdns_ctrl,
				      cdns_chip->corr_str_idx);

	cadence_nand_set_erase_detection(cdns_ctrl, true,
					 chip->ecc.strength);

	/* Override the default read operations. */
	chip->ecc.read_page = cadence_nand_read_page;
	chip->ecc.read_page_raw = cadence_nand_read_page_raw;
	chip->ecc.write_page = cadence_nand_write_page;
	chip->ecc.write_page_raw = cadence_nand_write_page_raw;
	chip->ecc.read_oob = cadence_nand_read_oob;
	chip->ecc.write_oob = cadence_nand_write_oob;
	chip->ecc.read_oob_raw = cadence_nand_read_oob_raw;
	chip->ecc.write_oob_raw = cadence_nand_write_oob_raw;

	if ((mtd->writesize + mtd->oobsize) > cdns_ctrl->buf_size)
		cdns_ctrl->buf_size = mtd->writesize + mtd->oobsize;

	/* Is 32-bit DMA supported? */
	ret = dma_set_mask(cdns_ctrl->dev, DMA_BIT_MASK(32));
	if (ret) {
		dev_err(cdns_ctrl->dev, "no usable DMA configuration\n");
		return ret;
	}

	mtd_set_ooblayout(mtd, &cadence_nand_ooblayout_ops);

	return 0;
}

static const struct nand_controller_ops cadence_nand_controller_ops = {
	.attach_chip = cadence_nand_attach_chip,
	.exec_op = cadence_nand_exec_op,
	.setup_interface = cadence_nand_setup_interface,
};

static int cadence_nand_chip_init(struct cdns_nand_ctrl *cdns_ctrl,
				  struct device_node *np)
{
	struct cdns_nand_chip *cdns_chip;
	struct mtd_info *mtd;
	struct nand_chip *chip;
	int nsels, ret, i;
	u32 cs;

	nsels = of_property_count_elems_of_size(np, "reg", sizeof(u32));
	if (nsels <= 0) {
		dev_err(cdns_ctrl->dev, "missing/invalid reg property\n");
		return -EINVAL;
	}

	/* Allocate the nand chip structure. */
	cdns_chip = devm_kzalloc(cdns_ctrl->dev, sizeof(*cdns_chip) +
				 (nsels * sizeof(u8)),
				 GFP_KERNEL);
	if (!cdns_chip) {
		dev_err(cdns_ctrl->dev, "could not allocate chip structure\n");
		return -ENOMEM;
	}

	cdns_chip->nsels = nsels;

	for (i = 0; i < nsels; i++) {
		/* Retrieve CS id. */
		ret = of_property_read_u32_index(np, "reg", i, &cs);
		if (ret) {
			dev_err(cdns_ctrl->dev,
				"could not retrieve reg property: %d\n",
				ret);
			return ret;
		}

		if (cs >= cdns_ctrl->caps2.max_banks) {
			dev_err(cdns_ctrl->dev,
				"invalid reg value: %u (max CS = %d)\n",
				cs, cdns_ctrl->caps2.max_banks);
			return -EINVAL;
		}

		if (test_and_set_bit(cs, &cdns_ctrl->assigned_cs)) {
			dev_err(cdns_ctrl->dev,
				"CS %d already assigned\n", cs);
			return -EINVAL;
		}

		cdns_chip->cs[i] = cs;
	}

	chip = &cdns_chip->chip;
	chip->controller = &cdns_ctrl->controller;
	nand_set_flash_node(chip, np);

	mtd = nand_to_mtd(chip);
	mtd->dev.parent = cdns_ctrl->dev;

	/*
	 * Default to HW ECC engine mode. If the nand-ecc-mode property is given
	 * in the DT node, this entry will be overwritten in nand_scan_ident().
	 */
	chip->ecc.engine_type = NAND_ECC_ENGINE_TYPE_ON_HOST;

	ret = nand_scan(chip, cdns_chip->nsels);
	if (ret) {
		dev_err(cdns_ctrl->dev, "could not scan the nand chip\n");
		return ret;
	}

	ret = mtd_device_register(mtd, NULL, 0);
	if (ret) {
		dev_err(cdns_ctrl->dev,
			"failed to register mtd device: %d\n", ret);
		nand_cleanup(chip);
		return ret;
	}

	list_add_tail(&cdns_chip->node, &cdns_ctrl->chips);

	return 0;
}

static void cadence_nand_chips_cleanup(struct cdns_nand_ctrl *cdns_ctrl)
{
	struct cdns_nand_chip *entry, *temp;
	struct nand_chip *chip;
	int ret;

	list_for_each_entry_safe(entry, temp, &cdns_ctrl->chips, node) {
		chip = &entry->chip;
		ret = mtd_device_unregister(nand_to_mtd(chip));
		WARN_ON(ret);
		nand_cleanup(chip);
		list_del(&entry->node);
	}
}

static int cadence_nand_chips_init(struct cdns_nand_ctrl *cdns_ctrl)
{
	struct device_node *np = cdns_ctrl->dev->of_node;
	int max_cs = cdns_ctrl->caps2.max_banks;
	int nchips, ret;

	nchips = of_get_child_count(np);

	if (nchips > max_cs) {
		dev_err(cdns_ctrl->dev,
			"too many NAND chips: %d (max = %d CS)\n",
			nchips, max_cs);
		return -EINVAL;
	}

	for_each_child_of_node_scoped(np, nand_np) {
		ret = cadence_nand_chip_init(cdns_ctrl, nand_np);
		if (ret) {
			cadence_nand_chips_cleanup(cdns_ctrl);
			return ret;
		}
	}

	return 0;
}

static void
cadence_nand_irq_cleanup(int irqnum, struct cdns_nand_ctrl *cdns_ctrl)
{
	/* Disable interrupts. */
	writel_relaxed(INTR_ENABLE_INTR_EN, cdns_ctrl->reg + INTR_ENABLE);
}

static int cadence_nand_init(struct cdns_nand_ctrl *cdns_ctrl)
{
	dma_cap_mask_t mask;
	int ret;

	cdns_ctrl->cdma_desc = dma_alloc_coherent(cdns_ctrl->dev,
						  sizeof(*cdns_ctrl->cdma_desc),
						  &cdns_ctrl->dma_cdma_desc,
						  GFP_KERNEL);
	if (!cdns_ctrl->dma_cdma_desc)
		return -ENOMEM;

	cdns_ctrl->buf_size = SZ_16K;
	cdns_ctrl->buf = kmalloc(cdns_ctrl->buf_size, GFP_KERNEL);
	if (!cdns_ctrl->buf) {
		ret = -ENOMEM;
		goto free_buf_desc;
	}

	if (devm_request_irq(cdns_ctrl->dev, cdns_ctrl->irq, cadence_nand_isr,
			     IRQF_SHARED, "cadence-nand-controller",
			     cdns_ctrl)) {
		dev_err(cdns_ctrl->dev, "Unable to allocate IRQ\n");
		ret = -ENODEV;
		goto free_buf;
	}

	spin_lock_init(&cdns_ctrl->irq_lock);
	init_completion(&cdns_ctrl->complete);

	ret = cadence_nand_hw_init(cdns_ctrl);
	if (ret)
		goto disable_irq;

	dma_cap_zero(mask);
	dma_cap_set(DMA_MEMCPY, mask);

	if (cdns_ctrl->caps1->has_dma) {
		cdns_ctrl->dmac = dma_request_channel(mask, NULL, NULL);
		if (!cdns_ctrl->dmac) {
			dev_err(cdns_ctrl->dev,
				"Unable to get a DMA channel\n");
			ret = -EBUSY;
			goto disable_irq;
		}
	}

	nand_controller_init(&cdns_ctrl->controller);
	INIT_LIST_HEAD(&cdns_ctrl->chips);

	cdns_ctrl->controller.ops = &cadence_nand_controller_ops;
	cdns_ctrl->curr_corr_str_idx = 0xFF;

	ret = cadence_nand_chips_init(cdns_ctrl);
	if (ret) {
		dev_err(cdns_ctrl->dev, "Failed to register MTD: %d\n",
			ret);
		goto dma_release_chnl;
	}

	kfree(cdns_ctrl->buf);
	cdns_ctrl->buf = kzalloc(cdns_ctrl->buf_size, GFP_KERNEL);
	if (!cdns_ctrl->buf) {
		ret = -ENOMEM;
		goto dma_release_chnl;
	}

	return 0;

dma_release_chnl:
	if (cdns_ctrl->dmac)
		dma_release_channel(cdns_ctrl->dmac);

disable_irq:
	cadence_nand_irq_cleanup(cdns_ctrl->irq, cdns_ctrl);

free_buf:
	kfree(cdns_ctrl->buf);

free_buf_desc:
	dma_free_coherent(cdns_ctrl->dev, sizeof(struct cadence_nand_cdma_desc),
			  cdns_ctrl->cdma_desc, cdns_ctrl->dma_cdma_desc);

	return ret;
}

/* Driver exit point. */
static void cadence_nand_remove(struct cdns_nand_ctrl *cdns_ctrl)
{
	cadence_nand_chips_cleanup(cdns_ctrl);
	cadence_nand_irq_cleanup(cdns_ctrl->irq, cdns_ctrl);
	kfree(cdns_ctrl->buf);
	dma_free_coherent(cdns_ctrl->dev, sizeof(struct cadence_nand_cdma_desc),
			  cdns_ctrl->cdma_desc, cdns_ctrl->dma_cdma_desc);

	if (cdns_ctrl->dmac)
		dma_release_channel(cdns_ctrl->dmac);
}

struct cadence_nand_dt {
	struct cdns_nand_ctrl cdns_ctrl;
	struct clk *clk;
};

static const struct cadence_nand_dt_devdata cadence_nand_default = {
	.if_skew = 0,
	.has_dma = 1,
};

static const struct of_device_id cadence_nand_dt_ids[] = {
	{
		.compatible = "cdns,hp-nfc",
		.data = &cadence_nand_default
	}, {}
};

MODULE_DEVICE_TABLE(of, cadence_nand_dt_ids);

static int cadence_nand_dt_probe(struct platform_device *ofdev)
{
	struct resource *res;
	struct cadence_nand_dt *dt;
	struct cdns_nand_ctrl *cdns_ctrl;
	int ret;
	const struct cadence_nand_dt_devdata *devdata;
	u32 val;

	devdata = device_get_match_data(&ofdev->dev);
	if (!devdata) {
		pr_err("Failed to find the right device id.\n");
		return -ENOMEM;
	}

	dt = devm_kzalloc(&ofdev->dev, sizeof(*dt), GFP_KERNEL);
	if (!dt)
		return -ENOMEM;

	cdns_ctrl = &dt->cdns_ctrl;
	cdns_ctrl->caps1 = devdata;

	cdns_ctrl->dev = &ofdev->dev;
	cdns_ctrl->irq = platform_get_irq(ofdev, 0);
	if (cdns_ctrl->irq < 0)
		return cdns_ctrl->irq;

	dev_info(cdns_ctrl->dev, "IRQ: nr %d\n", cdns_ctrl->irq);

	cdns_ctrl->reg = devm_platform_ioremap_resource(ofdev, 0);
	if (IS_ERR(cdns_ctrl->reg))
		return PTR_ERR(cdns_ctrl->reg);

	cdns_ctrl->io.virt = devm_platform_get_and_ioremap_resource(ofdev, 1, &res);
	if (IS_ERR(cdns_ctrl->io.virt))
		return PTR_ERR(cdns_ctrl->io.virt);
	cdns_ctrl->io.dma = res->start;

	dt->clk = devm_clk_get(cdns_ctrl->dev, "nf_clk");
	if (IS_ERR(dt->clk))
		return PTR_ERR(dt->clk);

	cdns_ctrl->nf_clk_rate = clk_get_rate(dt->clk);

	ret = of_property_read_u32(ofdev->dev.of_node,
				   "cdns,board-delay-ps", &val);
	if (ret) {
		val = 4830;
		dev_info(cdns_ctrl->dev,
			 "missing cdns,board-delay-ps property, %d was set\n",
			 val);
	}
	cdns_ctrl->board_delay = val;

	ret = cadence_nand_init(cdns_ctrl);
	if (ret)
		return ret;

	platform_set_drvdata(ofdev, dt);
	return 0;
}

static void cadence_nand_dt_remove(struct platform_device *ofdev)
{
	struct cadence_nand_dt *dt = platform_get_drvdata(ofdev);

	cadence_nand_remove(&dt->cdns_ctrl);
}

static struct platform_driver cadence_nand_dt_driver = {
	.probe		= cadence_nand_dt_probe,
	.remove		= cadence_nand_dt_remove,
	.driver		= {
		.name	= "cadence-nand-controller",
		.of_match_table = cadence_nand_dt_ids,
	},
};

module_platform_driver(cadence_nand_dt_driver);

MODULE_AUTHOR("Piotr Sroka <piotrs@cadence.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Driver for Cadence NAND flash controller");

