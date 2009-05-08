#ifndef _MVS64XX_REG_H_
#define _MVS64XX_REG_H_

/* enhanced mode registers (BAR4) */
enum hw_registers {
	MVS_GBL_CTL		= 0x04,  /* global control */
	MVS_GBL_INT_STAT	= 0x08,  /* global irq status */
	MVS_GBL_PI		= 0x0C,  /* ports implemented bitmask */
	MVS_GBL_PORT_TYPE	= 0xa0,  /* port type */

	MVS_CTL			= 0x100, /* SAS/SATA port configuration */
	MVS_PCS			= 0x104, /* SAS/SATA port control/status */
	MVS_CMD_LIST_LO		= 0x108, /* cmd list addr */
	MVS_CMD_LIST_HI		= 0x10C,
	MVS_RX_FIS_LO		= 0x110, /* RX FIS list addr */
	MVS_RX_FIS_HI		= 0x114,

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
	MVS_INT_STAT_SRS	= 0x158, /* SATA register set status */
	MVS_INT_MASK_SRS	= 0x15C,

					 /* ports 1-3 follow after this */
	MVS_P0_INT_STAT		= 0x160, /* port0 interrupt status */
	MVS_P0_INT_MASK		= 0x164, /* port0 interrupt mask */
	MVS_P4_INT_STAT		= 0x200, /* Port 4 interrupt status */
	MVS_P4_INT_MASK		= 0x204, /* Port 4 interrupt enable mask */

					 /* ports 1-3 follow after this */
	MVS_P0_SER_CTLSTAT	= 0x180, /* port0 serial control/status */
	MVS_P4_SER_CTLSTAT	= 0x220, /* port4 serial control/status */

	MVS_CMD_ADDR		= 0x1B8, /* Command register port (addr) */
	MVS_CMD_DATA		= 0x1BC, /* Command register port (data) */

					 /* ports 1-3 follow after this */
	MVS_P0_CFG_ADDR		= 0x1C0, /* port0 phy register address */
	MVS_P0_CFG_DATA		= 0x1C4, /* port0 phy register data */
	MVS_P4_CFG_ADDR		= 0x230, /* Port 4 config address */
	MVS_P4_CFG_DATA		= 0x234, /* Port 4 config data */

					 /* ports 1-3 follow after this */
	MVS_P0_VSR_ADDR		= 0x1E0, /* port0 VSR address */
	MVS_P0_VSR_DATA		= 0x1E4, /* port0 VSR data */
	MVS_P4_VSR_ADDR		= 0x250, /* port 4 VSR addr */
	MVS_P4_VSR_DATA		= 0x254, /* port 4 VSR data */
};

enum pci_cfg_registers {
	PCR_PHY_CTL		= 0x40,
	PCR_PHY_CTL2		= 0x90,
	PCR_DEV_CTRL		= 0xE8,
};

/*  SAS/SATA Vendor Specific Port Registers */
enum sas_sata_vsp_regs {
	VSR_PHY_STAT		= 0x00, /* Phy Status */
	VSR_PHY_MODE1		= 0x01, /* phy tx */
	VSR_PHY_MODE2		= 0x02, /* tx scc */
	VSR_PHY_MODE3		= 0x03, /* pll */
	VSR_PHY_MODE4		= 0x04, /* VCO */
	VSR_PHY_MODE5		= 0x05, /* Rx */
	VSR_PHY_MODE6		= 0x06, /* CDR */
	VSR_PHY_MODE7		= 0x07, /* Impedance */
	VSR_PHY_MODE8		= 0x08, /* Voltage */
	VSR_PHY_MODE9		= 0x09, /* Test */
	VSR_PHY_MODE10		= 0x0A, /* Power */
	VSR_PHY_MODE11		= 0x0B, /* Phy Mode */
	VSR_PHY_VS0		= 0x0C, /* Vednor Specific 0 */
	VSR_PHY_VS1		= 0x0D, /* Vednor Specific 1 */
};

struct mvs_prd {
	__le64			addr;		/* 64-bit buffer address */
	__le32			reserved;
	__le32			len;		/* 16-bit length */
};

#endif
