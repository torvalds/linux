/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Marvell 88SE94xx hardware specific head file
 *
 * Copyright 2007 Red Hat, Inc.
 * Copyright 2008 Marvell. <kewei@marvell.com>
 * Copyright 2009-2011 Marvell. <yuxiangl@marvell.com>
*/

#ifndef _MVS94XX_REG_H_
#define _MVS94XX_REG_H_

#include <linux/types.h>

#define MAX_LINK_RATE		SAS_LINK_RATE_6_0_GBPS

enum VANIR_REVISION_ID {
	VANIR_A0_REV		= 0xA0,
	VANIR_B0_REV		= 0x01,
	VANIR_C0_REV		= 0x02,
	VANIR_C1_REV		= 0x03,
	VANIR_C2_REV		= 0xC2,
};

enum host_registers {
	MVS_HST_CHIP_CONFIG	= 0x10104,	/* chip configuration */
};

enum hw_registers {
	MVS_GBL_CTL		= 0x04,  /* global control */
	MVS_GBL_INT_STAT	= 0x00,  /* global irq status */
	MVS_GBL_PI		= 0x0C,  /* ports implemented bitmask */

	MVS_PHY_CTL		= 0x40,  /* SOC PHY Control */
	MVS_PORTS_IMP		= 0x9C,  /* SOC Port Implemented */

	MVS_GBL_PORT_TYPE	= 0xa0,  /* port type */

	MVS_CTL			= 0x100, /* SAS/SATA port configuration */
	MVS_PCS			= 0x104, /* SAS/SATA port control/status */
	MVS_CMD_LIST_LO		= 0x108, /* cmd list addr */
	MVS_CMD_LIST_HI		= 0x10C,
	MVS_RX_FIS_LO		= 0x110, /* RX FIS list addr */
	MVS_RX_FIS_HI		= 0x114,
	MVS_STP_REG_SET_0	= 0x118, /* STP/SATA Register Set Enable */
	MVS_STP_REG_SET_1	= 0x11C,
	MVS_TX_CFG		= 0x120, /* TX configuration */
	MVS_TX_LO		= 0x124, /* TX (delivery) ring addr */
	MVS_TX_HI		= 0x128,

	MVS_TX_PROD_IDX		= 0x12C, /* TX producer pointer */
	MVS_TX_CONS_IDX		= 0x130, /* TX consumer pointer (RO) */
	MVS_RX_CFG		= 0x134, /* RX configuration */
	MVS_RX_LO		= 0x138, /* RX (completion) ring addr */
	MVS_RX_HI		= 0x13C,
	MVS_RX_CONS_IDX		= 0x140, /* RX consumer pointer (RO) */

	MVS_INT_COAL		= 0x148, /* Int coalescing config */
	MVS_INT_COAL_TMOUT	= 0x14C, /* Int coalescing timeout */
	MVS_INT_STAT		= 0x150, /* Central int status */
	MVS_INT_MASK		= 0x154, /* Central int enable */
	MVS_INT_STAT_SRS_0	= 0x158, /* SATA register set status */
	MVS_INT_MASK_SRS_0	= 0x15C,
	MVS_INT_STAT_SRS_1	= 0x160,
	MVS_INT_MASK_SRS_1	= 0x164,
	MVS_NON_NCQ_ERR_0	= 0x168, /* SRS Non-specific NCQ Error */
	MVS_NON_NCQ_ERR_1	= 0x16C,
	MVS_CMD_ADDR		= 0x170, /* Command register port (addr) */
	MVS_CMD_DATA		= 0x174, /* Command register port (data) */
	MVS_MEM_PARITY_ERR	= 0x178, /* Memory parity error */

					 /* ports 1-3 follow after this */
	MVS_P0_INT_STAT		= 0x180, /* port0 interrupt status */
	MVS_P0_INT_MASK		= 0x184, /* port0 interrupt mask */
					 /* ports 5-7 follow after this */
	MVS_P4_INT_STAT		= 0x1A0, /* Port4 interrupt status */
	MVS_P4_INT_MASK		= 0x1A4, /* Port4 interrupt enable mask */

					 /* ports 1-3 follow after this */
	MVS_P0_SER_CTLSTAT	= 0x1D0, /* port0 serial control/status */
					 /* ports 5-7 follow after this */
	MVS_P4_SER_CTLSTAT	= 0x1E0, /* port4 serial control/status */

					 /* ports 1-3 follow after this */
	MVS_P0_CFG_ADDR		= 0x200, /* port0 phy register address */
	MVS_P0_CFG_DATA		= 0x204, /* port0 phy register data */
					 /* ports 5-7 follow after this */
	MVS_P4_CFG_ADDR		= 0x220, /* Port4 config address */
	MVS_P4_CFG_DATA		= 0x224, /* Port4 config data */

					 /* phys 1-3 follow after this */
	MVS_P0_VSR_ADDR		= 0x250, /* phy0 VSR address */
	MVS_P0_VSR_DATA		= 0x254, /* phy0 VSR data */
					 /* phys 1-3 follow after this */
					 /* multiplexing */
	MVS_P4_VSR_ADDR 	= 0x250, /* phy4 VSR address */
	MVS_P4_VSR_DATA 	= 0x254, /* phy4 VSR data */
	MVS_PA_VSR_ADDR		= 0x290, /* All port VSR addr */
	MVS_PA_VSR_PORT		= 0x294, /* All port VSR data */
	MVS_COMMAND_ACTIVE	= 0x300,
};

enum pci_cfg_registers {
	PCR_PHY_CTL		= 0x40,
	PCR_PHY_CTL2		= 0x90,
	PCR_DEV_CTRL		= 0x78,
	PCR_LINK_STAT		= 0x82,
};

/*  SAS/SATA Vendor Specific Port Registers */
enum sas_sata_vsp_regs {
	VSR_PHY_STAT		= 0x00 * 4, /* Phy Interrupt Status */
	VSR_PHY_MODE1		= 0x01 * 4, /* phy Interrupt Enable */
	VSR_PHY_MODE2		= 0x02 * 4, /* Phy Configuration */
	VSR_PHY_MODE3		= 0x03 * 4, /* Phy Status */
	VSR_PHY_MODE4		= 0x04 * 4, /* Phy Counter 0 */
	VSR_PHY_MODE5		= 0x05 * 4, /* Phy Counter 1 */
	VSR_PHY_MODE6		= 0x06 * 4, /* Event Counter Control */
	VSR_PHY_MODE7		= 0x07 * 4, /* Event Counter Select */
	VSR_PHY_MODE8		= 0x08 * 4, /* Event Counter 0 */
	VSR_PHY_MODE9		= 0x09 * 4, /* Event Counter 1 */
	VSR_PHY_MODE10		= 0x0A * 4, /* Event Counter 2 */
	VSR_PHY_MODE11		= 0x0B * 4, /* Event Counter 3 */
	VSR_PHY_ACT_LED		= 0x0C * 4, /* Activity LED control */

	VSR_PHY_FFE_CONTROL	= 0x10C,
	VSR_PHY_DFE_UPDATE_CRTL	= 0x110,
	VSR_REF_CLOCK_CRTL	= 0x1A0,
};

enum chip_register_bits {
	PHY_MIN_SPP_PHYS_LINK_RATE_MASK = (0x7 << 8),
	PHY_MAX_SPP_PHYS_LINK_RATE_MASK = (0x7 << 12),
	PHY_NEG_SPP_PHYS_LINK_RATE_MASK_OFFSET = (16),
	PHY_NEG_SPP_PHYS_LINK_RATE_MASK =
			(0x3 << PHY_NEG_SPP_PHYS_LINK_RATE_MASK_OFFSET),
};

enum pci_interrupt_cause {
	/*  MAIN_IRQ_CAUSE (R10200) Bits*/
	MVS_IRQ_COM_IN_I2O_IOP0        = (1 << 0),
	MVS_IRQ_COM_IN_I2O_IOP1        = (1 << 1),
	MVS_IRQ_COM_IN_I2O_IOP2        = (1 << 2),
	MVS_IRQ_COM_IN_I2O_IOP3        = (1 << 3),
	MVS_IRQ_COM_OUT_I2O_HOS0       = (1 << 4),
	MVS_IRQ_COM_OUT_I2O_HOS1       = (1 << 5),
	MVS_IRQ_COM_OUT_I2O_HOS2       = (1 << 6),
	MVS_IRQ_COM_OUT_I2O_HOS3       = (1 << 7),
	MVS_IRQ_PCIF_TO_CPU_DRBL0      = (1 << 8),
	MVS_IRQ_PCIF_TO_CPU_DRBL1      = (1 << 9),
	MVS_IRQ_PCIF_TO_CPU_DRBL2      = (1 << 10),
	MVS_IRQ_PCIF_TO_CPU_DRBL3      = (1 << 11),
	MVS_IRQ_PCIF_DRBL0             = (1 << 12),
	MVS_IRQ_PCIF_DRBL1             = (1 << 13),
	MVS_IRQ_PCIF_DRBL2             = (1 << 14),
	MVS_IRQ_PCIF_DRBL3             = (1 << 15),
	MVS_IRQ_XOR_A                  = (1 << 16),
	MVS_IRQ_XOR_B                  = (1 << 17),
	MVS_IRQ_SAS_A                  = (1 << 18),
	MVS_IRQ_SAS_B                  = (1 << 19),
	MVS_IRQ_CPU_CNTRL              = (1 << 20),
	MVS_IRQ_GPIO                   = (1 << 21),
	MVS_IRQ_UART                   = (1 << 22),
	MVS_IRQ_SPI                    = (1 << 23),
	MVS_IRQ_I2C                    = (1 << 24),
	MVS_IRQ_SGPIO                  = (1 << 25),
	MVS_IRQ_COM_ERR                = (1 << 29),
	MVS_IRQ_I2O_ERR                = (1 << 30),
	MVS_IRQ_PCIE_ERR               = (1 << 31),
};

union reg_phy_cfg {
	u32 v;
	struct {
		u32 phy_reset:1;
		u32 sas_support:1;
		u32 sata_support:1;
		u32 sata_host_mode:1;
		/*
		 * bit 2: 6Gbps support
		 * bit 1: 3Gbps support
		 * bit 0: 1.5Gbps support
		 */
		u32 speed_support:3;
		u32 snw_3_support:1;
		u32 tx_lnk_parity:1;
		/*
		 * bit 5: G1 (1.5Gbps) Without SSC
		 * bit 4: G1 (1.5Gbps) with SSC
		 * bit 3: G2 (3.0Gbps) Without SSC
		 * bit 2: G2 (3.0Gbps) with SSC
		 * bit 1: G3 (6.0Gbps) without SSC
		 * bit 0: G3 (6.0Gbps) with SSC
		 */
		u32 tx_spt_phs_lnk_rate:6;
		/* 8h: 1.5Gbps 9h: 3Gbps Ah: 6Gbps */
		u32 tx_lgcl_lnk_rate:4;
		u32 tx_ssc_type:1;
		u32 sata_spin_up_spt:1;
		u32 sata_spin_up_en:1;
		u32 bypass_oob:1;
		u32 disable_phy:1;
		u32 rsvd:8;
	} u;
};

#define MAX_SG_ENTRY		255

struct mvs_prd_imt {
#ifndef __BIG_ENDIAN
	__le32			len:22;
	u8			_r_a:2;
	u8			misc_ctl:4;
	u8			inter_sel:4;
#else
	u32			inter_sel:4;
	u32			misc_ctl:4;
	u32			_r_a:2;
	u32			len:22;
#endif
};

struct mvs_prd {
	/* 64-bit buffer address */
	__le64			addr;
	/* 22-bit length */
	__le32			im_len;
} __attribute__ ((packed));

enum sgpio_registers {
	MVS_SGPIO_HOST_OFFSET	= 0x100,	/* offset between hosts */

	MVS_SGPIO_CFG0	= 0xc200,
	MVS_SGPIO_CFG0_ENABLE	= (1 << 0),	/* enable pins */
	MVS_SGPIO_CFG0_BLINKB	= (1 << 1),	/* blink generators */
	MVS_SGPIO_CFG0_BLINKA	= (1 << 2),
	MVS_SGPIO_CFG0_INVSCLK	= (1 << 3),	/* invert signal? */
	MVS_SGPIO_CFG0_INVSLOAD	= (1 << 4),
	MVS_SGPIO_CFG0_INVSDOUT	= (1 << 5),
	MVS_SGPIO_CFG0_SLOAD_FALLEDGE = (1 << 6),	/* rise/fall edge? */
	MVS_SGPIO_CFG0_SDOUT_FALLEDGE = (1 << 7),
	MVS_SGPIO_CFG0_SDIN_RISEEDGE = (1 << 8),
	MVS_SGPIO_CFG0_MAN_BITLEN_SHIFT = 18,	/* bits/frame manual mode */
	MVS_SGPIO_CFG0_AUT_BITLEN_SHIFT = 24,	/* bits/frame auto mode */

	MVS_SGPIO_CFG1	= 0xc204,	/* blink timing register */
	MVS_SGPIO_CFG1_LOWA_SHIFT	= 0,	/* A off time */
	MVS_SGPIO_CFG1_HIA_SHIFT	= 4,	/* A on time */
	MVS_SGPIO_CFG1_LOWB_SHIFT	= 8,	/* B off time */
	MVS_SGPIO_CFG1_HIB_SHIFT	= 12,	/* B on time */
	MVS_SGPIO_CFG1_MAXACTON_SHIFT	= 16,	/* max activity on time */

		/* force activity off time */
	MVS_SGPIO_CFG1_FORCEACTOFF_SHIFT	= 20,
		/* stretch activity on time */
	MVS_SGPIO_CFG1_STRCHACTON_SHIFT	= 24,
		/* stretch activiity off time */
	MVS_SGPIO_CFG1_STRCHACTOFF_SHIFT	= 28,


	MVS_SGPIO_CFG2	= 0xc208,	/* clock speed register */
	MVS_SGPIO_CFG2_CLK_SHIFT	= 0,
	MVS_SGPIO_CFG2_BLINK_SHIFT	= 20,

	MVS_SGPIO_CTRL	= 0xc20c,	/* SDOUT/SDIN mode control */
	MVS_SGPIO_CTRL_SDOUT_AUTO	= 2,
	MVS_SGPIO_CTRL_SDOUT_SHIFT	= 2,

	MVS_SGPIO_DSRC	= 0xc220,	/* map ODn bits to drives */

	MVS_SGPIO_DCTRL	= 0xc238,
	MVS_SGPIO_DCTRL_ERR_SHIFT	= 0,
	MVS_SGPIO_DCTRL_LOC_SHIFT	= 3,
	MVS_SGPIO_DCTRL_ACT_SHIFT	= 5,
};

enum sgpio_led_status {
	LED_OFF	= 0,
	LED_ON	= 1,
	LED_BLINKA	= 2,
	LED_BLINKA_INV	= 3,
	LED_BLINKA_SOF	= 4,
	LED_BLINKA_EOF	= 5,
	LED_BLINKB	= 6,
	LED_BLINKB_INV	= 7,
};

#define DEFAULT_SGPIO_BITS ((LED_BLINKA_SOF << \
				MVS_SGPIO_DCTRL_ACT_SHIFT) << (8 * 3) | \
			(LED_BLINKA_SOF << \
				MVS_SGPIO_DCTRL_ACT_SHIFT) << (8 * 2) | \
			(LED_BLINKA_SOF << \
				MVS_SGPIO_DCTRL_ACT_SHIFT) << (8 * 1) | \
			(LED_BLINKA_SOF << \
				MVS_SGPIO_DCTRL_ACT_SHIFT) << (8 * 0))

/*
 * these registers are accessed through port vendor
 * specific address/data registers
 */
enum sas_sata_phy_regs {
	GENERATION_1_SETTING		= 0x118,
	GENERATION_1_2_SETTING		= 0x11C,
	GENERATION_2_3_SETTING		= 0x120,
	GENERATION_3_4_SETTING		= 0x124,
};

#define SPI_CTRL_REG_94XX           	0xc800
#define SPI_ADDR_REG_94XX            	0xc804
#define SPI_WR_DATA_REG_94XX         0xc808
#define SPI_RD_DATA_REG_94XX         	0xc80c
#define SPI_CTRL_READ_94XX         	(1U << 2)
#define SPI_ADDR_VLD_94XX         	(1U << 1)
#define SPI_CTRL_SpiStart_94XX     	(1U << 0)

static inline int
mv_ffc64(u64 v)
{
	u64 x = ~v;
	return x ? __ffs64(x) : -1;
}

#define r_reg_set_enable(i) \
	(((i) > 31) ? mr32(MVS_STP_REG_SET_1) : \
	mr32(MVS_STP_REG_SET_0))

#define w_reg_set_enable(i, tmp) \
	(((i) > 31) ? mw32(MVS_STP_REG_SET_1, tmp) : \
	mw32(MVS_STP_REG_SET_0, tmp))

extern const struct mvs_dispatch mvs_94xx_dispatch;
#endif

