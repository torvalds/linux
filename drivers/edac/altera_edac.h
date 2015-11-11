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

#endif	/* #ifndef _ALTERA_EDAC_H */
