/*
 *
 * Copyright (C) 2015 Altera Corporation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _ALTERA_EDAC_H
#define _ALTERA_EDAC_H

#include <linux/edac.h>
#include <linux/types.h>

/* SDRAM Controller CtrlCfg Register */
#define CV_CTLCFG_OFST             0x00

/* SDRAM Controller CtrlCfg Register Bit Masks */
#define CV_CTLCFG_ECC_EN           0x400
#define CV_CTLCFG_ECC_CORR_EN      0x800
#define CV_CTLCFG_GEN_SB_ERR       0x2000
#define CV_CTLCFG_GEN_DB_ERR       0x4000

#define CV_CTLCFG_ECC_AUTO_EN     (CV_CTLCFG_ECC_EN)

/* SDRAM Controller Address Width Register */
#define CV_DRAMADDRW_OFST          0x2C

/* SDRAM Controller Address Widths Field Register */
#define DRAMADDRW_COLBIT_MASK      0x001F
#define DRAMADDRW_COLBIT_SHIFT     0
#define DRAMADDRW_ROWBIT_MASK      0x03E0
#define DRAMADDRW_ROWBIT_SHIFT     5
#define CV_DRAMADDRW_BANKBIT_MASK  0x1C00
#define CV_DRAMADDRW_BANKBIT_SHIFT 10
#define CV_DRAMADDRW_CSBIT_MASK    0xE000
#define CV_DRAMADDRW_CSBIT_SHIFT   13

/* SDRAM Controller Interface Data Width Register */
#define CV_DRAMIFWIDTH_OFST        0x30

/* SDRAM Controller Interface Data Width Defines */
#define CV_DRAMIFWIDTH_16B_ECC     24
#define CV_DRAMIFWIDTH_32B_ECC     40

/* SDRAM Controller DRAM Status Register */
#define CV_DRAMSTS_OFST            0x38

/* SDRAM Controller DRAM Status Register Bit Masks */
#define CV_DRAMSTS_SBEERR          0x04
#define CV_DRAMSTS_DBEERR          0x08
#define CV_DRAMSTS_CORR_DROP       0x10

/* SDRAM Controller DRAM IRQ Register */
#define CV_DRAMINTR_OFST           0x3C

/* SDRAM Controller DRAM IRQ Register Bit Masks */
#define CV_DRAMINTR_INTREN         0x01
#define CV_DRAMINTR_SBEMASK        0x02
#define CV_DRAMINTR_DBEMASK        0x04
#define CV_DRAMINTR_CORRDROPMASK   0x08
#define CV_DRAMINTR_INTRCLR        0x10

/* SDRAM Controller Single Bit Error Count Register */
#define CV_SBECOUNT_OFST           0x40

/* SDRAM Controller Double Bit Error Count Register */
#define CV_DBECOUNT_OFST           0x44

/* SDRAM Controller ECC Error Address Register */
#define CV_ERRADDR_OFST            0x48

/*-----------------------------------------*/

/* SDRAM Controller EccCtrl Register */
#define A10_ECCCTRL1_OFST          0x00

/* SDRAM Controller EccCtrl Register Bit Masks */
#define A10_ECCCTRL1_ECC_EN        0x001
#define A10_ECCCTRL1_CNT_RST       0x010
#define A10_ECCCTRL1_AWB_CNT_RST   0x100
#define A10_ECC_CNT_RESET_MASK     (A10_ECCCTRL1_CNT_RST | \
				    A10_ECCCTRL1_AWB_CNT_RST)

/* SDRAM Controller Address Width Register */
#define CV_DRAMADDRW               0xFFC2502C
#define A10_DRAMADDRW              0xFFCFA0A8

/* SDRAM Controller Address Widths Field Register */
#define DRAMADDRW_COLBIT_MASK      0x001F
#define DRAMADDRW_COLBIT_SHIFT     0
#define DRAMADDRW_ROWBIT_MASK      0x03E0
#define DRAMADDRW_ROWBIT_SHIFT     5
#define CV_DRAMADDRW_BANKBIT_MASK  0x1C00
#define CV_DRAMADDRW_BANKBIT_SHIFT 10
#define CV_DRAMADDRW_CSBIT_MASK    0xE000
#define CV_DRAMADDRW_CSBIT_SHIFT   13

#define A10_DRAMADDRW_BANKBIT_MASK  0x3C00
#define A10_DRAMADDRW_BANKBIT_SHIFT 10
#define A10_DRAMADDRW_GRPBIT_MASK   0xC000
#define A10_DRAMADDRW_GRPBIT_SHIFT  14
#define A10_DRAMADDRW_CSBIT_MASK    0x70000
#define A10_DRAMADDRW_CSBIT_SHIFT   16

/* SDRAM Controller Interface Data Width Register */
#define CV_DRAMIFWIDTH             0xFFC25030
#define A10_DRAMIFWIDTH            0xFFCFB008

/* SDRAM Controller Interface Data Width Defines */
#define CV_DRAMIFWIDTH_16B_ECC     24
#define CV_DRAMIFWIDTH_32B_ECC     40

#define A10_DRAMIFWIDTH_16B        0x0
#define A10_DRAMIFWIDTH_32B        0x1
#define A10_DRAMIFWIDTH_64B        0x2

/* SDRAM Controller DRAM IRQ Register */
#define A10_ERRINTEN_OFST          0x10

/* SDRAM Controller DRAM IRQ Register Bit Masks */
#define A10_ERRINTEN_SERRINTEN     0x01
#define A10_ERRINTEN_DERRINTEN     0x02
#define A10_ECC_IRQ_EN_MASK        (A10_ERRINTEN_SERRINTEN | \
				    A10_ERRINTEN_DERRINTEN)

/* SDRAM Interrupt Mode Register */
#define A10_INTMODE_OFST           0x1C
#define A10_INTMODE_SB_INT         1

/* SDRAM Controller Error Status Register */
#define A10_INTSTAT_OFST           0x20

/* SDRAM Controller Error Status Register Bit Masks */
#define A10_INTSTAT_SBEERR         0x01
#define A10_INTSTAT_DBEERR         0x02

/* SDRAM Controller ECC Error Address Register */
#define A10_DERRADDR_OFST          0x2C
#define A10_SERRADDR_OFST          0x30

/* SDRAM Controller ECC Diagnostic Register */
#define A10_DIAGINTTEST_OFST       0x24

#define A10_DIAGINT_TSERRA_MASK    0x0001
#define A10_DIAGINT_TDERRA_MASK    0x0100

#define A10_SBERR_IRQ              34
#define A10_DBERR_IRQ              32

/* SDRAM Single Bit Error Count Compare Set Register */
#define A10_SERRCNTREG_OFST        0x3C

#define A10_SYMAN_INTMASK_CLR      0xFFD06098
#define A10_INTMASK_CLR_OFST       0x10
#define A10_DDR0_IRQ_MASK          BIT(17)

struct altr_sdram_prv_data {
	int ecc_ctrl_offset;
	int ecc_ctl_en_mask;
	int ecc_cecnt_offset;
	int ecc_uecnt_offset;
	int ecc_stat_offset;
	int ecc_stat_ce_mask;
	int ecc_stat_ue_mask;
	int ecc_saddr_offset;
	int ecc_daddr_offset;
	int ecc_irq_en_offset;
	int ecc_irq_en_mask;
	int ecc_irq_clr_offset;
	int ecc_irq_clr_mask;
	int ecc_cnt_rst_offset;
	int ecc_cnt_rst_mask;
	struct edac_dev_sysfs_attribute *eccmgr_sysfs_attr;
	int ecc_enable_mask;
	int ce_set_mask;
	int ue_set_mask;
	int ce_ue_trgr_offset;
};

/* Altera SDRAM Memory Controller data */
struct altr_sdram_mc_data {
	struct regmap *mc_vbase;
	int sb_irq;
	int db_irq;
	const struct altr_sdram_prv_data *data;
};

/************************** EDAC Device Defines **************************/
/***** General Device Trigger Defines *****/
#define ALTR_UE_TRIGGER_CHAR            'U'   /* Trigger for UE */
#define ALTR_TRIGGER_READ_WRD_CNT       32    /* Line size x 4 */
#define ALTR_TRIG_OCRAM_BYTE_SIZE       128   /* Line size x 4 */
#define ALTR_TRIG_L2C_BYTE_SIZE         4096  /* Full Page */

/******* Cyclone5 and Arria5 Defines *******/
/* OCRAM ECC Management Group Defines */
#define ALTR_MAN_GRP_OCRAM_ECC_OFFSET   0x04
#define ALTR_OCR_ECC_REG_OFFSET         0x00
#define ALTR_OCR_ECC_EN                 BIT(0)
#define ALTR_OCR_ECC_INJS               BIT(1)
#define ALTR_OCR_ECC_INJD               BIT(2)
#define ALTR_OCR_ECC_SERR               BIT(3)
#define ALTR_OCR_ECC_DERR               BIT(4)

/* L2 ECC Management Group Defines */
#define ALTR_MAN_GRP_L2_ECC_OFFSET      0x00
#define ALTR_L2_ECC_REG_OFFSET          0x00
#define ALTR_L2_ECC_EN                  BIT(0)
#define ALTR_L2_ECC_INJS                BIT(1)
#define ALTR_L2_ECC_INJD                BIT(2)

/* Arria10 General ECC Block Module Defines */
#define ALTR_A10_ECC_CTRL_OFST          0x08
#define ALTR_A10_ECC_EN                 BIT(0)
#define ALTR_A10_ECC_INITA              BIT(16)
#define ALTR_A10_ECC_INITB              BIT(24)

#define ALTR_A10_ECC_INITSTAT_OFST      0x0C
#define ALTR_A10_ECC_INITCOMPLETEA      BIT(0)
#define ALTR_A10_ECC_INITCOMPLETEB      BIT(8)

#define ALTR_A10_ECC_ERRINTEN_OFST      0x10
#define ALTR_A10_ECC_ERRINTENS_OFST     0x14
#define ALTR_A10_ECC_ERRINTENR_OFST     0x18
#define ALTR_A10_ECC_SERRINTEN          BIT(0)

#define ALTR_A10_ECC_INTMODE_OFST       0x1C
#define ALTR_A10_ECC_INTMODE            BIT(0)

#define ALTR_A10_ECC_INTSTAT_OFST       0x20
#define ALTR_A10_ECC_SERRPENA           BIT(0)
#define ALTR_A10_ECC_DERRPENA           BIT(8)
#define ALTR_A10_ECC_ERRPENA_MASK       (ALTR_A10_ECC_SERRPENA | \
					 ALTR_A10_ECC_DERRPENA)
#define ALTR_A10_ECC_SERRPENB           BIT(16)
#define ALTR_A10_ECC_DERRPENB           BIT(24)
#define ALTR_A10_ECC_ERRPENB_MASK       (ALTR_A10_ECC_SERRPENB | \
					 ALTR_A10_ECC_DERRPENB)

#define ALTR_A10_ECC_INTTEST_OFST       0x24
#define ALTR_A10_ECC_TSERRA             BIT(0)
#define ALTR_A10_ECC_TDERRA             BIT(8)
#define ALTR_A10_ECC_TSERRB             BIT(16)
#define ALTR_A10_ECC_TDERRB             BIT(24)

/* ECC Manager Defines */
#define A10_SYSMGR_ECC_INTMASK_SET_OFST   0x94
#define A10_SYSMGR_ECC_INTMASK_CLR_OFST   0x98
#define A10_SYSMGR_ECC_INTMASK_OCRAM      BIT(1)

#define A10_SYSMGR_ECC_INTSTAT_SERR_OFST  0x9C
#define A10_SYSMGR_ECC_INTSTAT_DERR_OFST  0xA0
#define A10_SYSMGR_ECC_INTSTAT_L2         BIT(0)
#define A10_SYSMGR_ECC_INTSTAT_OCRAM      BIT(1)

#define A10_SYSGMR_MPU_CLEAR_L2_ECC_OFST  0xA8
#define A10_SYSGMR_MPU_CLEAR_L2_ECC_SB    BIT(15)
#define A10_SYSGMR_MPU_CLEAR_L2_ECC_MB    BIT(31)

/* Arria 10 L2 ECC Management Group Defines */
#define ALTR_A10_L2_ECC_CTL_OFST        0x0
#define ALTR_A10_L2_ECC_EN_CTL          BIT(0)

#define ALTR_A10_L2_ECC_STATUS          0xFFD060A4
#define ALTR_A10_L2_ECC_STAT_OFST       0xA4
#define ALTR_A10_L2_ECC_SERR_PEND       BIT(0)
#define ALTR_A10_L2_ECC_MERR_PEND       BIT(0)

#define ALTR_A10_L2_ECC_CLR_OFST        0x4
#define ALTR_A10_L2_ECC_SERR_CLR        BIT(15)
#define ALTR_A10_L2_ECC_MERR_CLR        BIT(31)

#define ALTR_A10_L2_ECC_INJ_OFST        ALTR_A10_L2_ECC_CTL_OFST
#define ALTR_A10_L2_ECC_CE_INJ_MASK     0x00000101
#define ALTR_A10_L2_ECC_UE_INJ_MASK     0x00010101

/* Arria 10 OCRAM ECC Management Group Defines */
#define ALTR_A10_OCRAM_ECC_EN_CTL       (BIT(1) | BIT(0))

/* Arria 10 Ethernet ECC Management Group Defines */
#define ALTR_A10_COMMON_ECC_EN_CTL      BIT(0)

/* Arria 10 SDMMC ECC Management Group Defines */
#define ALTR_A10_SDMMC_IRQ_MASK         (BIT(16) | BIT(15))

/* A10 ECC Controller memory initialization timeout */
#define ALTR_A10_ECC_INIT_WATCHDOG_10US      10000

struct altr_edac_device_dev;

struct edac_device_prv_data {
	int (*setup)(struct altr_edac_device_dev *device);
	int ce_clear_mask;
	int ue_clear_mask;
	int irq_status_mask;
	void * (*alloc_mem)(size_t size, void **other);
	void (*free_mem)(void *p, size_t size, void *other);
	int ecc_enable_mask;
	int ecc_en_ofst;
	int ce_set_mask;
	int ue_set_mask;
	int set_err_ofst;
	irqreturn_t (*ecc_irq_handler)(int irq, void *dev_id);
	int trig_alloc_sz;
	const struct file_operations *inject_fops;
	bool panic;
};

struct altr_edac_device_dev {
	struct list_head next;
	void __iomem *base;
	int sb_irq;
	int db_irq;
	const struct edac_device_prv_data *data;
	struct dentry *debugfs_dir;
	char *edac_dev_name;
	struct altr_arria10_edac *edac;
	struct edac_device_ctl_info *edac_dev;
	struct device ddev;
	int edac_idx;
};

struct altr_arria10_edac {
	struct device		*dev;
	struct regmap		*ecc_mgr_map;
	int sb_irq;
	int db_irq;
	struct irq_domain	*domain;
	struct irq_chip		irq_chip;
	struct list_head	a10_ecc_devices;
};

#endif	/* #ifndef _ALTERA_EDAC_H */
