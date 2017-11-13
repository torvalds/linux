/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Definitions for the new Marvell Yukon 2 driver.
 */
#ifndef _SKY2_H
#define _SKY2_H

#define ETH_JUMBO_MTU		9000	/* Maximum MTU supported */

/* PCI config registers */
enum {
	PCI_DEV_REG1	= 0x40,
	PCI_DEV_REG2	= 0x44,
	PCI_DEV_STATUS  = 0x7c,
	PCI_DEV_REG3	= 0x80,
	PCI_DEV_REG4	= 0x84,
	PCI_DEV_REG5    = 0x88,
	PCI_CFG_REG_0	= 0x90,
	PCI_CFG_REG_1	= 0x94,

	PSM_CONFIG_REG0  = 0x98,
	PSM_CONFIG_REG1	 = 0x9C,
	PSM_CONFIG_REG2  = 0x160,
	PSM_CONFIG_REG3  = 0x164,
	PSM_CONFIG_REG4  = 0x168,

	PCI_LDO_CTRL    = 0xbc,
};

/* Yukon-2 */
enum pci_dev_reg_1 {
	PCI_Y2_PIG_ENA	 = 1<<31, /* Enable Plug-in-Go (YUKON-2) */
	PCI_Y2_DLL_DIS	 = 1<<30, /* Disable PCI DLL (YUKON-2) */
	PCI_SW_PWR_ON_RST= 1<<30, /* SW Power on Reset (Yukon-EX) */
	PCI_Y2_PHY2_COMA = 1<<29, /* Set PHY 2 to Coma Mode (YUKON-2) */
	PCI_Y2_PHY1_COMA = 1<<28, /* Set PHY 1 to Coma Mode (YUKON-2) */
	PCI_Y2_PHY2_POWD = 1<<27, /* Set PHY 2 to Power Down (YUKON-2) */
	PCI_Y2_PHY1_POWD = 1<<26, /* Set PHY 1 to Power Down (YUKON-2) */
	PCI_Y2_PME_LEGACY= 1<<15, /* PCI Express legacy power management mode */

	PCI_PHY_LNK_TIM_MSK= 3L<<8,/* Bit  9.. 8:	GPHY Link Trigger Timer */
	PCI_ENA_L1_EVENT = 1<<7, /* Enable PEX L1 Event */
	PCI_ENA_GPHY_LNK = 1<<6, /* Enable PEX L1 on GPHY Link down */
	PCI_FORCE_PEX_L1 = 1<<5, /* Force to PEX L1 */
};

enum pci_dev_reg_2 {
	PCI_VPD_WR_THR	= 0xffL<<24,	/* Bit 31..24:	VPD Write Threshold */
	PCI_DEV_SEL	= 0x7fL<<17,	/* Bit 23..17:	EEPROM Device Select */
	PCI_VPD_ROM_SZ	= 7L<<14,	/* Bit 16..14:	VPD ROM Size	*/

	PCI_PATCH_DIR	= 0xfL<<8,	/* Bit 11.. 8:	Ext Patches dir 3..0 */
	PCI_EXT_PATCHS	= 0xfL<<4,	/* Bit	7.. 4:	Extended Patches 3..0 */
	PCI_EN_DUMMY_RD	= 1<<3,		/* Enable Dummy Read */
	PCI_REV_DESC	= 1<<2,		/* Reverse Desc. Bytes */

	PCI_USEDATA64	= 1<<0,		/* Use 64Bit Data bus ext */
};

/*	PCI_OUR_REG_3		32 bit	Our Register 3 (Yukon-ECU only) */
enum pci_dev_reg_3 {
	P_CLK_ASF_REGS_DIS	= 1<<18,/* Disable Clock ASF (Yukon-Ext.) */
	P_CLK_COR_REGS_D0_DIS	= 1<<17,/* Disable Clock Core Regs D0 */
	P_CLK_MACSEC_DIS	= 1<<17,/* Disable Clock MACSec (Yukon-Ext.) */
	P_CLK_PCI_REGS_D0_DIS	= 1<<16,/* Disable Clock PCI  Regs D0 */
	P_CLK_COR_YTB_ARB_DIS	= 1<<15,/* Disable Clock YTB  Arbiter */
	P_CLK_MAC_LNK1_D3_DIS	= 1<<14,/* Disable Clock MAC  Link1 D3 */
	P_CLK_COR_LNK1_D0_DIS	= 1<<13,/* Disable Clock Core Link1 D0 */
	P_CLK_MAC_LNK1_D0_DIS	= 1<<12,/* Disable Clock MAC  Link1 D0 */
	P_CLK_COR_LNK1_D3_DIS	= 1<<11,/* Disable Clock Core Link1 D3 */
	P_CLK_PCI_MST_ARB_DIS	= 1<<10,/* Disable Clock PCI  Master Arb. */
	P_CLK_COR_REGS_D3_DIS	= 1<<9,	/* Disable Clock Core Regs D3 */
	P_CLK_PCI_REGS_D3_DIS	= 1<<8,	/* Disable Clock PCI  Regs D3 */
	P_CLK_REF_LNK1_GM_DIS	= 1<<7,	/* Disable Clock Ref. Link1 GMAC */
	P_CLK_COR_LNK1_GM_DIS	= 1<<6,	/* Disable Clock Core Link1 GMAC */
	P_CLK_PCI_COMMON_DIS	= 1<<5,	/* Disable Clock PCI  Common */
	P_CLK_COR_COMMON_DIS	= 1<<4,	/* Disable Clock Core Common */
	P_CLK_PCI_LNK1_BMU_DIS	= 1<<3,	/* Disable Clock PCI  Link1 BMU */
	P_CLK_COR_LNK1_BMU_DIS	= 1<<2,	/* Disable Clock Core Link1 BMU */
	P_CLK_PCI_LNK1_BIU_DIS	= 1<<1,	/* Disable Clock PCI  Link1 BIU */
	P_CLK_COR_LNK1_BIU_DIS	= 1<<0,	/* Disable Clock Core Link1 BIU */
	PCIE_OUR3_WOL_D3_COLD_SET = P_CLK_ASF_REGS_DIS |
				    P_CLK_COR_REGS_D0_DIS |
				    P_CLK_COR_LNK1_D0_DIS |
				    P_CLK_MAC_LNK1_D0_DIS |
				    P_CLK_PCI_MST_ARB_DIS |
				    P_CLK_COR_COMMON_DIS |
				    P_CLK_COR_LNK1_BMU_DIS,
};

/*	PCI_OUR_REG_4		32 bit	Our Register 4 (Yukon-ECU only) */
enum pci_dev_reg_4 {
				/* (Link Training & Status State Machine) */
	P_PEX_LTSSM_STAT_MSK	= 0x7fL<<25,	/* Bit 31..25:	PEX LTSSM Mask */
#define P_PEX_LTSSM_STAT(x)	((x << 25) & P_PEX_LTSSM_STAT_MSK)
	P_PEX_LTSSM_L1_STAT	= 0x34,
	P_PEX_LTSSM_DET_STAT	= 0x01,
	P_TIMER_VALUE_MSK	= 0xffL<<16,	/* Bit 23..16:	Timer Value Mask */
					/* (Active State Power Management) */
	P_FORCE_ASPM_REQUEST	= 1<<15, /* Force ASPM Request (A1 only) */
	P_ASPM_GPHY_LINK_DOWN	= 1<<14, /* GPHY Link Down (A1 only) */
	P_ASPM_INT_FIFO_EMPTY	= 1<<13, /* Internal FIFO Empty (A1 only) */
	P_ASPM_CLKRUN_REQUEST	= 1<<12, /* CLKRUN Request (A1 only) */

	P_ASPM_FORCE_CLKREQ_ENA	= 1<<4,	/* Force CLKREQ Enable (A1b only) */
	P_ASPM_CLKREQ_PAD_CTL	= 1<<3,	/* CLKREQ PAD Control (A1 only) */
	P_ASPM_A1_MODE_SELECT	= 1<<2,	/* A1 Mode Select (A1 only) */
	P_CLK_GATE_PEX_UNIT_ENA	= 1<<1,	/* Enable Gate PEX Unit Clock */
	P_CLK_GATE_ROOT_COR_ENA	= 1<<0,	/* Enable Gate Root Core Clock */
	P_ASPM_CONTROL_MSK	= P_FORCE_ASPM_REQUEST | P_ASPM_GPHY_LINK_DOWN
				  | P_ASPM_CLKRUN_REQUEST | P_ASPM_INT_FIFO_EMPTY,
};

/*	PCI_OUR_REG_5		32 bit	Our Register 5 (Yukon-ECU only) */
enum pci_dev_reg_5 {
					/* Bit 31..27:	for A3 & later */
	P_CTL_DIV_CORE_CLK_ENA	= 1<<31, /* Divide Core Clock Enable */
	P_CTL_SRESET_VMAIN_AV	= 1<<30, /* Soft Reset for Vmain_av De-Glitch */
	P_CTL_BYPASS_VMAIN_AV	= 1<<29, /* Bypass En. for Vmain_av De-Glitch */
	P_CTL_TIM_VMAIN_AV_MSK	= 3<<27, /* Bit 28..27: Timer Vmain_av Mask */
					 /* Bit 26..16: Release Clock on Event */
	P_REL_PCIE_RST_DE_ASS	= 1<<26, /* PCIe Reset De-Asserted */
	P_REL_GPHY_REC_PACKET	= 1<<25, /* GPHY Received Packet */
	P_REL_INT_FIFO_N_EMPTY	= 1<<24, /* Internal FIFO Not Empty */
	P_REL_MAIN_PWR_AVAIL	= 1<<23, /* Main Power Available */
	P_REL_CLKRUN_REQ_REL	= 1<<22, /* CLKRUN Request Release */
	P_REL_PCIE_RESET_ASS	= 1<<21, /* PCIe Reset Asserted */
	P_REL_PME_ASSERTED	= 1<<20, /* PME Asserted */
	P_REL_PCIE_EXIT_L1_ST	= 1<<19, /* PCIe Exit L1 State */
	P_REL_LOADER_NOT_FIN	= 1<<18, /* EPROM Loader Not Finished */
	P_REL_PCIE_RX_EX_IDLE	= 1<<17, /* PCIe Rx Exit Electrical Idle State */
	P_REL_GPHY_LINK_UP	= 1<<16, /* GPHY Link Up */

					/* Bit 10.. 0: Mask for Gate Clock */
	P_GAT_PCIE_RST_ASSERTED	= 1<<10,/* PCIe Reset Asserted */
	P_GAT_GPHY_N_REC_PACKET	= 1<<9, /* GPHY Not Received Packet */
	P_GAT_INT_FIFO_EMPTY	= 1<<8, /* Internal FIFO Empty */
	P_GAT_MAIN_PWR_N_AVAIL	= 1<<7, /* Main Power Not Available */
	P_GAT_CLKRUN_REQ_REL	= 1<<6, /* CLKRUN Not Requested */
	P_GAT_PCIE_RESET_ASS	= 1<<5, /* PCIe Reset Asserted */
	P_GAT_PME_DE_ASSERTED	= 1<<4, /* PME De-Asserted */
	P_GAT_PCIE_ENTER_L1_ST	= 1<<3, /* PCIe Enter L1 State */
	P_GAT_LOADER_FINISHED	= 1<<2, /* EPROM Loader Finished */
	P_GAT_PCIE_RX_EL_IDLE	= 1<<1, /* PCIe Rx Electrical Idle State */
	P_GAT_GPHY_LINK_DOWN	= 1<<0,	/* GPHY Link Down */

	PCIE_OUR5_EVENT_CLK_D3_SET = P_REL_GPHY_REC_PACKET |
				     P_REL_INT_FIFO_N_EMPTY |
				     P_REL_PCIE_EXIT_L1_ST |
				     P_REL_PCIE_RX_EX_IDLE |
				     P_GAT_GPHY_N_REC_PACKET |
				     P_GAT_INT_FIFO_EMPTY |
				     P_GAT_PCIE_ENTER_L1_ST |
				     P_GAT_PCIE_RX_EL_IDLE,
};

/*	PCI_CFG_REG_1			32 bit	Config Register 1 (Yukon-Ext only) */
enum pci_cfg_reg1 {
	P_CF1_DIS_REL_EVT_RST	= 1<<24, /* Dis. Rel. Event during PCIE reset */
										/* Bit 23..21: Release Clock on Event */
	P_CF1_REL_LDR_NOT_FIN	= 1<<23, /* EEPROM Loader Not Finished */
	P_CF1_REL_VMAIN_AVLBL	= 1<<22, /* Vmain available */
	P_CF1_REL_PCIE_RESET	= 1<<21, /* PCI-E reset */
										/* Bit 20..18: Gate Clock on Event */
	P_CF1_GAT_LDR_NOT_FIN	= 1<<20, /* EEPROM Loader Finished */
	P_CF1_GAT_PCIE_RX_IDLE	= 1<<19, /* PCI-E Rx Electrical idle */
	P_CF1_GAT_PCIE_RESET	= 1<<18, /* PCI-E Reset */
	P_CF1_PRST_PHY_CLKREQ	= 1<<17, /* Enable PCI-E rst & PM2PHY gen. CLKREQ */
	P_CF1_PCIE_RST_CLKREQ	= 1<<16, /* Enable PCI-E rst generate CLKREQ */

	P_CF1_ENA_CFG_LDR_DONE	= 1<<8, /* Enable core level Config loader done */

	P_CF1_ENA_TXBMU_RD_IDLE	= 1<<1, /* Enable TX BMU Read  IDLE for ASPM */
	P_CF1_ENA_TXBMU_WR_IDLE	= 1<<0, /* Enable TX BMU Write IDLE for ASPM */

	PCIE_CFG1_EVENT_CLK_D3_SET = P_CF1_DIS_REL_EVT_RST |
					P_CF1_REL_LDR_NOT_FIN |
					P_CF1_REL_VMAIN_AVLBL |
					P_CF1_REL_PCIE_RESET |
					P_CF1_GAT_LDR_NOT_FIN |
					P_CF1_GAT_PCIE_RESET |
					P_CF1_PRST_PHY_CLKREQ |
					P_CF1_ENA_CFG_LDR_DONE |
					P_CF1_ENA_TXBMU_RD_IDLE |
					P_CF1_ENA_TXBMU_WR_IDLE,
};

/* Yukon-Optima */
enum {
	PSM_CONFIG_REG1_AC_PRESENT_STATUS = 1<<31,   /* AC Present Status */

	PSM_CONFIG_REG1_PTP_CLK_SEL	  = 1<<29,   /* PTP Clock Select */
	PSM_CONFIG_REG1_PTP_MODE	  = 1<<28,   /* PTP Mode */

	PSM_CONFIG_REG1_MUX_PHY_LINK	  = 1<<27,   /* PHY Energy Detect Event */

	PSM_CONFIG_REG1_EN_PIN63_AC_PRESENT = 1<<26,  /* Enable LED_DUPLEX for ac_present */
	PSM_CONFIG_REG1_EN_PCIE_TIMER	  = 1<<25,    /* Enable PCIe Timer */
	PSM_CONFIG_REG1_EN_SPU_TIMER	  = 1<<24,    /* Enable SPU Timer */
	PSM_CONFIG_REG1_POLARITY_AC_PRESENT = 1<<23,  /* AC Present Polarity */

	PSM_CONFIG_REG1_EN_AC_PRESENT	  = 1<<21,    /* Enable AC Present */

	PSM_CONFIG_REG1_EN_GPHY_INT_PSM	= 1<<20,      /* Enable GPHY INT for PSM */
	PSM_CONFIG_REG1_DIS_PSM_TIMER	= 1<<19,      /* Disable PSM Timer */
};

/* Yukon-Supreme */
enum {
	PSM_CONFIG_REG1_GPHY_ENERGY_STS	= 1<<31, /* GPHY Energy Detect Status */

	PSM_CONFIG_REG1_UART_MODE_MSK	= 3<<29, /* UART_Mode */
	PSM_CONFIG_REG1_CLK_RUN_ASF	= 1<<28, /* Enable Clock Free Running for ASF Subsystem */
	PSM_CONFIG_REG1_UART_CLK_DISABLE= 1<<27, /* Disable UART clock */
	PSM_CONFIG_REG1_VAUX_ONE	= 1<<26, /* Tie internal Vaux to 1'b1 */
	PSM_CONFIG_REG1_UART_FC_RI_VAL	= 1<<25, /* Default value for UART_RI_n */
	PSM_CONFIG_REG1_UART_FC_DCD_VAL	= 1<<24, /* Default value for UART_DCD_n */
	PSM_CONFIG_REG1_UART_FC_DSR_VAL	= 1<<23, /* Default value for UART_DSR_n */
	PSM_CONFIG_REG1_UART_FC_CTS_VAL	= 1<<22, /* Default value for UART_CTS_n */
	PSM_CONFIG_REG1_LATCH_VAUX	= 1<<21, /* Enable Latch current Vaux_avlbl */
	PSM_CONFIG_REG1_FORCE_TESTMODE_INPUT= 1<<20, /* Force Testmode pin as input PAD */
	PSM_CONFIG_REG1_UART_RST	= 1<<19, /* UART_RST */
	PSM_CONFIG_REG1_PSM_PCIE_L1_POL	= 1<<18, /* PCIE L1 Event Polarity for PSM */
	PSM_CONFIG_REG1_TIMER_STAT	= 1<<17, /* PSM Timer Status */
	PSM_CONFIG_REG1_GPHY_INT	= 1<<16, /* GPHY INT Status */
	PSM_CONFIG_REG1_FORCE_TESTMODE_ZERO= 1<<15, /* Force internal Testmode as 1'b0 */
	PSM_CONFIG_REG1_EN_INT_ASPM_CLKREQ = 1<<14, /* ENABLE INT for CLKRUN on ASPM and CLKREQ */
	PSM_CONFIG_REG1_EN_SND_TASK_ASPM_CLKREQ	= 1<<13, /* ENABLE Snd_task for CLKRUN on ASPM and CLKREQ */
	PSM_CONFIG_REG1_DIS_CLK_GATE_SND_TASK	= 1<<12, /* Disable CLK_GATE control snd_task */
	PSM_CONFIG_REG1_DIS_FF_CHIAN_SND_INTA	= 1<<11, /* Disable flip-flop chain for sndmsg_inta */

	PSM_CONFIG_REG1_DIS_LOADER	= 1<<9, /* Disable Loader SM after PSM Goes back to IDLE */
	PSM_CONFIG_REG1_DO_PWDN		= 1<<8, /* Do Power Down, Start PSM Scheme */
	PSM_CONFIG_REG1_DIS_PIG		= 1<<7, /* Disable Plug-in-Go SM after PSM Goes back to IDLE */
	PSM_CONFIG_REG1_DIS_PERST	= 1<<6, /* Disable Internal PCIe Reset after PSM Goes back to IDLE */
	PSM_CONFIG_REG1_EN_REG18_PD	= 1<<5, /* Enable REG18 Power Down for PSM */
	PSM_CONFIG_REG1_EN_PSM_LOAD	= 1<<4, /* Disable EEPROM Loader after PSM Goes back to IDLE */
	PSM_CONFIG_REG1_EN_PSM_HOT_RST	= 1<<3, /* Enable PCIe Hot Reset for PSM */
	PSM_CONFIG_REG1_EN_PSM_PERST	= 1<<2, /* Enable PCIe Reset Event for PSM */
	PSM_CONFIG_REG1_EN_PSM_PCIE_L1	= 1<<1, /* Enable PCIe L1 Event for PSM */
	PSM_CONFIG_REG1_EN_PSM		= 1<<0, /* Enable PSM Scheme */
};

/*	PSM_CONFIG_REG4				0x0168	PSM Config Register 4 */
enum {
						/* PHY Link Detect Timer */
	PSM_CONFIG_REG4_TIMER_PHY_LINK_DETECT_MSK = 0xf<<4,
	PSM_CONFIG_REG4_TIMER_PHY_LINK_DETECT_BASE = 4,

	PSM_CONFIG_REG4_DEBUG_TIMER	    = 1<<1, /* Debug Timer */
	PSM_CONFIG_REG4_RST_PHY_LINK_DETECT = 1<<0, /* Reset GPHY Link Detect */
};


#define PCI_STATUS_ERROR_BITS (PCI_STATUS_DETECTED_PARITY | \
			       PCI_STATUS_SIG_SYSTEM_ERROR | \
			       PCI_STATUS_REC_MASTER_ABORT | \
			       PCI_STATUS_REC_TARGET_ABORT | \
			       PCI_STATUS_PARITY)

enum csr_regs {
	B0_RAP		= 0x0000,
	B0_CTST		= 0x0004,

	B0_POWER_CTRL	= 0x0007,
	B0_ISRC		= 0x0008,
	B0_IMSK		= 0x000c,
	B0_HWE_ISRC	= 0x0010,
	B0_HWE_IMSK	= 0x0014,

	/* Special ISR registers (Yukon-2 only) */
	B0_Y2_SP_ISRC2	= 0x001c,
	B0_Y2_SP_ISRC3	= 0x0020,
	B0_Y2_SP_EISR	= 0x0024,
	B0_Y2_SP_LISR	= 0x0028,
	B0_Y2_SP_ICR	= 0x002c,

	B2_MAC_1	= 0x0100,
	B2_MAC_2	= 0x0108,
	B2_MAC_3	= 0x0110,
	B2_CONN_TYP	= 0x0118,
	B2_PMD_TYP	= 0x0119,
	B2_MAC_CFG	= 0x011a,
	B2_CHIP_ID	= 0x011b,
	B2_E_0		= 0x011c,

	B2_Y2_CLK_GATE  = 0x011d,
	B2_Y2_HW_RES	= 0x011e,
	B2_E_3		= 0x011f,
	B2_Y2_CLK_CTRL	= 0x0120,

	B2_TI_INI	= 0x0130,
	B2_TI_VAL	= 0x0134,
	B2_TI_CTRL	= 0x0138,
	B2_TI_TEST	= 0x0139,

	B2_TST_CTRL1	= 0x0158,
	B2_TST_CTRL2	= 0x0159,
	B2_GP_IO	= 0x015c,

	B2_I2C_CTRL	= 0x0160,
	B2_I2C_DATA	= 0x0164,
	B2_I2C_IRQ	= 0x0168,
	B2_I2C_SW	= 0x016c,

	Y2_PEX_PHY_DATA = 0x0170,
	Y2_PEX_PHY_ADDR = 0x0172,

	B3_RAM_ADDR	= 0x0180,
	B3_RAM_DATA_LO	= 0x0184,
	B3_RAM_DATA_HI	= 0x0188,

/* RAM Interface Registers */
/* Yukon-2: use RAM_BUFFER() to access the RAM buffer */
/*
 * The HW-Spec. calls this registers Timeout Value 0..11. But this names are
 * not usable in SW. Please notice these are NOT real timeouts, these are
 * the number of qWords transferred continuously.
 */
#define RAM_BUFFER(port, reg)	(reg | (port <<6))

	B3_RI_WTO_R1	= 0x0190,
	B3_RI_WTO_XA1	= 0x0191,
	B3_RI_WTO_XS1	= 0x0192,
	B3_RI_RTO_R1	= 0x0193,
	B3_RI_RTO_XA1	= 0x0194,
	B3_RI_RTO_XS1	= 0x0195,
	B3_RI_WTO_R2	= 0x0196,
	B3_RI_WTO_XA2	= 0x0197,
	B3_RI_WTO_XS2	= 0x0198,
	B3_RI_RTO_R2	= 0x0199,
	B3_RI_RTO_XA2	= 0x019a,
	B3_RI_RTO_XS2	= 0x019b,
	B3_RI_TO_VAL	= 0x019c,
	B3_RI_CTRL	= 0x01a0,
	B3_RI_TEST	= 0x01a2,
	B3_MA_TOINI_RX1	= 0x01b0,
	B3_MA_TOINI_RX2	= 0x01b1,
	B3_MA_TOINI_TX1	= 0x01b2,
	B3_MA_TOINI_TX2	= 0x01b3,
	B3_MA_TOVAL_RX1	= 0x01b4,
	B3_MA_TOVAL_RX2	= 0x01b5,
	B3_MA_TOVAL_TX1	= 0x01b6,
	B3_MA_TOVAL_TX2	= 0x01b7,
	B3_MA_TO_CTRL	= 0x01b8,
	B3_MA_TO_TEST	= 0x01ba,
	B3_MA_RCINI_RX1	= 0x01c0,
	B3_MA_RCINI_RX2	= 0x01c1,
	B3_MA_RCINI_TX1	= 0x01c2,
	B3_MA_RCINI_TX2	= 0x01c3,
	B3_MA_RCVAL_RX1	= 0x01c4,
	B3_MA_RCVAL_RX2	= 0x01c5,
	B3_MA_RCVAL_TX1	= 0x01c6,
	B3_MA_RCVAL_TX2	= 0x01c7,
	B3_MA_RC_CTRL	= 0x01c8,
	B3_MA_RC_TEST	= 0x01ca,
	B3_PA_TOINI_RX1	= 0x01d0,
	B3_PA_TOINI_RX2	= 0x01d4,
	B3_PA_TOINI_TX1	= 0x01d8,
	B3_PA_TOINI_TX2	= 0x01dc,
	B3_PA_TOVAL_RX1	= 0x01e0,
	B3_PA_TOVAL_RX2	= 0x01e4,
	B3_PA_TOVAL_TX1	= 0x01e8,
	B3_PA_TOVAL_TX2	= 0x01ec,
	B3_PA_CTRL	= 0x01f0,
	B3_PA_TEST	= 0x01f2,

	Y2_CFG_SPC	= 0x1c00,	/* PCI config space region */
	Y2_CFG_AER      = 0x1d00,	/* PCI Advanced Error Report region */
};

/*	B0_CTST			24 bit	Control/Status register */
enum {
	Y2_VMAIN_AVAIL	= 1<<17,/* VMAIN available (YUKON-2 only) */
	Y2_VAUX_AVAIL	= 1<<16,/* VAUX available (YUKON-2 only) */
	Y2_HW_WOL_ON	= 1<<15,/* HW WOL On  (Yukon-EC Ultra A1 only) */
	Y2_HW_WOL_OFF	= 1<<14,/* HW WOL On  (Yukon-EC Ultra A1 only) */
	Y2_ASF_ENABLE	= 1<<13,/* ASF Unit Enable (YUKON-2 only) */
	Y2_ASF_DISABLE	= 1<<12,/* ASF Unit Disable (YUKON-2 only) */
	Y2_CLK_RUN_ENA	= 1<<11,/* CLK_RUN Enable  (YUKON-2 only) */
	Y2_CLK_RUN_DIS	= 1<<10,/* CLK_RUN Disable (YUKON-2 only) */
	Y2_LED_STAT_ON	= 1<<9, /* Status LED On  (YUKON-2 only) */
	Y2_LED_STAT_OFF	= 1<<8, /* Status LED Off (YUKON-2 only) */

	CS_ST_SW_IRQ	= 1<<7,	/* Set IRQ SW Request */
	CS_CL_SW_IRQ	= 1<<6,	/* Clear IRQ SW Request */
	CS_STOP_DONE	= 1<<5,	/* Stop Master is finished */
	CS_STOP_MAST	= 1<<4,	/* Command Bit to stop the master */
	CS_MRST_CLR	= 1<<3,	/* Clear Master reset	*/
	CS_MRST_SET	= 1<<2,	/* Set Master reset	*/
	CS_RST_CLR	= 1<<1,	/* Clear Software reset	*/
	CS_RST_SET	= 1,	/* Set   Software reset	*/
};

/*	B0_POWER_CTRL	 8 Bit	Power Control reg (YUKON only) */
enum {
	PC_VAUX_ENA	= 1<<7,	/* Switch VAUX Enable  */
	PC_VAUX_DIS	= 1<<6,	/* Switch VAUX Disable */
	PC_VCC_ENA	= 1<<5,	/* Switch VCC Enable  */
	PC_VCC_DIS	= 1<<4,	/* Switch VCC Disable */
	PC_VAUX_ON	= 1<<3,	/* Switch VAUX On  */
	PC_VAUX_OFF	= 1<<2,	/* Switch VAUX Off */
	PC_VCC_ON	= 1<<1,	/* Switch VCC On  */
	PC_VCC_OFF	= 1<<0,	/* Switch VCC Off */
};

/*	B2_IRQM_MSK 	32 bit	IRQ Moderation Mask */

/*	B0_Y2_SP_ISRC2	32 bit	Special Interrupt Source Reg 2 */
/*	B0_Y2_SP_ISRC3	32 bit	Special Interrupt Source Reg 3 */
/*	B0_Y2_SP_EISR	32 bit	Enter ISR Reg */
/*	B0_Y2_SP_LISR	32 bit	Leave ISR Reg */
enum {
	Y2_IS_HW_ERR	= 1<<31,	/* Interrupt HW Error */
	Y2_IS_STAT_BMU	= 1<<30,	/* Status BMU Interrupt */
	Y2_IS_ASF	= 1<<29,	/* ASF subsystem Interrupt */
	Y2_IS_CPU_TO	= 1<<28,	/* CPU Timeout */
	Y2_IS_POLL_CHK	= 1<<27,	/* Check IRQ from polling unit */
	Y2_IS_TWSI_RDY	= 1<<26,	/* IRQ on end of TWSI Tx */
	Y2_IS_IRQ_SW	= 1<<25,	/* SW forced IRQ	*/
	Y2_IS_TIMINT	= 1<<24,	/* IRQ from Timer	*/

	Y2_IS_IRQ_PHY2	= 1<<12,	/* Interrupt from PHY 2 */
	Y2_IS_IRQ_MAC2	= 1<<11,	/* Interrupt from MAC 2 */
	Y2_IS_CHK_RX2	= 1<<10,	/* Descriptor error Rx 2 */
	Y2_IS_CHK_TXS2	= 1<<9,		/* Descriptor error TXS 2 */
	Y2_IS_CHK_TXA2	= 1<<8,		/* Descriptor error TXA 2 */

	Y2_IS_PSM_ACK	= 1<<7,		/* PSM Acknowledge (Yukon-Optima only) */
	Y2_IS_PTP_TIST	= 1<<6,		/* PTP Time Stamp (Yukon-Optima only) */
	Y2_IS_PHY_QLNK	= 1<<5,		/* PHY Quick Link (Yukon-Optima only) */

	Y2_IS_IRQ_PHY1	= 1<<4,		/* Interrupt from PHY 1 */
	Y2_IS_IRQ_MAC1	= 1<<3,		/* Interrupt from MAC 1 */
	Y2_IS_CHK_RX1	= 1<<2,		/* Descriptor error Rx 1 */
	Y2_IS_CHK_TXS1	= 1<<1,		/* Descriptor error TXS 1 */
	Y2_IS_CHK_TXA1	= 1<<0,		/* Descriptor error TXA 1 */

	Y2_IS_BASE	= Y2_IS_HW_ERR | Y2_IS_STAT_BMU,
	Y2_IS_PORT_1	= Y2_IS_IRQ_PHY1 | Y2_IS_IRQ_MAC1
		          | Y2_IS_CHK_TXA1 | Y2_IS_CHK_RX1,
	Y2_IS_PORT_2	= Y2_IS_IRQ_PHY2 | Y2_IS_IRQ_MAC2
			  | Y2_IS_CHK_TXA2 | Y2_IS_CHK_RX2,
	Y2_IS_ERROR     = Y2_IS_HW_ERR |
			  Y2_IS_IRQ_MAC1 | Y2_IS_CHK_TXA1 | Y2_IS_CHK_RX1 |
			  Y2_IS_IRQ_MAC2 | Y2_IS_CHK_TXA2 | Y2_IS_CHK_RX2,
};

/*	B2_IRQM_HWE_MSK	32 bit	IRQ Moderation HW Error Mask */
enum {
	IS_ERR_MSK	= 0x00003fff,/* 		All Error bits */

	IS_IRQ_TIST_OV	= 1<<13, /* Time Stamp Timer Overflow (YUKON only) */
	IS_IRQ_SENSOR	= 1<<12, /* IRQ from Sensor (YUKON only) */
	IS_IRQ_MST_ERR	= 1<<11, /* IRQ master error detected */
	IS_IRQ_STAT	= 1<<10, /* IRQ status exception */
	IS_NO_STAT_M1	= 1<<9,	/* No Rx Status from MAC 1 */
	IS_NO_STAT_M2	= 1<<8,	/* No Rx Status from MAC 2 */
	IS_NO_TIST_M1	= 1<<7,	/* No Time Stamp from MAC 1 */
	IS_NO_TIST_M2	= 1<<6,	/* No Time Stamp from MAC 2 */
	IS_RAM_RD_PAR	= 1<<5,	/* RAM Read  Parity Error */
	IS_RAM_WR_PAR	= 1<<4,	/* RAM Write Parity Error */
	IS_M1_PAR_ERR	= 1<<3,	/* MAC 1 Parity Error */
	IS_M2_PAR_ERR	= 1<<2,	/* MAC 2 Parity Error */
	IS_R1_PAR_ERR	= 1<<1,	/* Queue R1 Parity Error */
	IS_R2_PAR_ERR	= 1<<0,	/* Queue R2 Parity Error */
};

/* Hardware error interrupt mask for Yukon 2 */
enum {
	Y2_IS_TIST_OV	= 1<<29,/* Time Stamp Timer overflow interrupt */
	Y2_IS_SENSOR	= 1<<28, /* Sensor interrupt */
	Y2_IS_MST_ERR	= 1<<27, /* Master error interrupt */
	Y2_IS_IRQ_STAT	= 1<<26, /* Status exception interrupt */
	Y2_IS_PCI_EXP	= 1<<25, /* PCI-Express interrupt */
	Y2_IS_PCI_NEXP	= 1<<24, /* PCI-Express error similar to PCI error */
						/* Link 2 */
	Y2_IS_PAR_RD2	= 1<<13, /* Read RAM parity error interrupt */
	Y2_IS_PAR_WR2	= 1<<12, /* Write RAM parity error interrupt */
	Y2_IS_PAR_MAC2	= 1<<11, /* MAC hardware fault interrupt */
	Y2_IS_PAR_RX2	= 1<<10, /* Parity Error Rx Queue 2 */
	Y2_IS_TCP_TXS2	= 1<<9, /* TCP length mismatch sync Tx queue IRQ */
	Y2_IS_TCP_TXA2	= 1<<8, /* TCP length mismatch async Tx queue IRQ */
						/* Link 1 */
	Y2_IS_PAR_RD1	= 1<<5, /* Read RAM parity error interrupt */
	Y2_IS_PAR_WR1	= 1<<4, /* Write RAM parity error interrupt */
	Y2_IS_PAR_MAC1	= 1<<3, /* MAC hardware fault interrupt */
	Y2_IS_PAR_RX1	= 1<<2, /* Parity Error Rx Queue 1 */
	Y2_IS_TCP_TXS1	= 1<<1, /* TCP length mismatch sync Tx queue IRQ */
	Y2_IS_TCP_TXA1	= 1<<0, /* TCP length mismatch async Tx queue IRQ */

	Y2_HWE_L1_MASK	= Y2_IS_PAR_RD1 | Y2_IS_PAR_WR1 | Y2_IS_PAR_MAC1 |
			  Y2_IS_PAR_RX1 | Y2_IS_TCP_TXS1| Y2_IS_TCP_TXA1,
	Y2_HWE_L2_MASK	= Y2_IS_PAR_RD2 | Y2_IS_PAR_WR2 | Y2_IS_PAR_MAC2 |
			  Y2_IS_PAR_RX2 | Y2_IS_TCP_TXS2| Y2_IS_TCP_TXA2,

	Y2_HWE_ALL_MASK	= Y2_IS_TIST_OV | Y2_IS_MST_ERR | Y2_IS_IRQ_STAT |
			  Y2_HWE_L1_MASK | Y2_HWE_L2_MASK,
};

/*	B28_DPT_CTRL	 8 bit	Descriptor Poll Timer Ctrl Reg */
enum {
	DPT_START	= 1<<1,
	DPT_STOP	= 1<<0,
};

/*	B2_TST_CTRL1	 8 bit	Test Control Register 1 */
enum {
	TST_FRC_DPERR_MR = 1<<7, /* force DATAPERR on MST RD */
	TST_FRC_DPERR_MW = 1<<6, /* force DATAPERR on MST WR */
	TST_FRC_DPERR_TR = 1<<5, /* force DATAPERR on TRG RD */
	TST_FRC_DPERR_TW = 1<<4, /* force DATAPERR on TRG WR */
	TST_FRC_APERR_M	 = 1<<3, /* force ADDRPERR on MST */
	TST_FRC_APERR_T	 = 1<<2, /* force ADDRPERR on TRG */
	TST_CFG_WRITE_ON = 1<<1, /* Enable  Config Reg WR */
	TST_CFG_WRITE_OFF= 1<<0, /* Disable Config Reg WR */
};

/* 	B2_GPIO */
enum {
	GLB_GPIO_CLK_DEB_ENA = 1<<31,	/* Clock Debug Enable */
	GLB_GPIO_CLK_DBG_MSK = 0xf<<26, /* Clock Debug */

	GLB_GPIO_INT_RST_D3_DIS = 1<<15, /* Disable Internal Reset After D3 to D0 */
	GLB_GPIO_LED_PAD_SPEED_UP = 1<<14, /* LED PAD Speed Up */
	GLB_GPIO_STAT_RACE_DIS	= 1<<13, /* Status Race Disable */
	GLB_GPIO_TEST_SEL_MSK	= 3<<11, /* Testmode Select */
	GLB_GPIO_TEST_SEL_BASE	= 1<<11,
	GLB_GPIO_RAND_ENA	= 1<<10, /* Random Enable */
	GLB_GPIO_RAND_BIT_1	= 1<<9,  /* Random Bit 1 */
};

/*	B2_MAC_CFG		 8 bit	MAC Configuration / Chip Revision */
enum {
	CFG_CHIP_R_MSK	  = 0xf<<4,	/* Bit 7.. 4: Chip Revision */
					/* Bit 3.. 2:	reserved */
	CFG_DIS_M2_CLK	  = 1<<1,	/* Disable Clock for 2nd MAC */
	CFG_SNG_MAC	  = 1<<0,	/* MAC Config: 0=2 MACs / 1=1 MAC*/
};

/*	B2_CHIP_ID		 8 bit 	Chip Identification Number */
enum {
	CHIP_ID_YUKON_XL   = 0xb3, /* YUKON-2 XL */
	CHIP_ID_YUKON_EC_U = 0xb4, /* YUKON-2 EC Ultra */
	CHIP_ID_YUKON_EX   = 0xb5, /* YUKON-2 Extreme */
	CHIP_ID_YUKON_EC   = 0xb6, /* YUKON-2 EC */
 	CHIP_ID_YUKON_FE   = 0xb7, /* YUKON-2 FE */
 	CHIP_ID_YUKON_FE_P = 0xb8, /* YUKON-2 FE+ */
	CHIP_ID_YUKON_SUPR = 0xb9, /* YUKON-2 Supreme */
	CHIP_ID_YUKON_UL_2 = 0xba, /* YUKON-2 Ultra 2 */
	CHIP_ID_YUKON_OPT  = 0xbc, /* YUKON-2 Optima */
	CHIP_ID_YUKON_PRM  = 0xbd, /* YUKON-2 Optima Prime */
	CHIP_ID_YUKON_OP_2 = 0xbe, /* YUKON-2 Optima 2 */
};

enum yukon_xl_rev {
	CHIP_REV_YU_XL_A0  = 0,
	CHIP_REV_YU_XL_A1  = 1,
	CHIP_REV_YU_XL_A2  = 2,
	CHIP_REV_YU_XL_A3  = 3,
};

enum yukon_ec_rev {
	CHIP_REV_YU_EC_A1    = 0,  /* Chip Rev. for Yukon-EC A1/A0 */
	CHIP_REV_YU_EC_A2    = 1,  /* Chip Rev. for Yukon-EC A2 */
	CHIP_REV_YU_EC_A3    = 2,  /* Chip Rev. for Yukon-EC A3 */
};
enum yukon_ec_u_rev {
	CHIP_REV_YU_EC_U_A0  = 1,
	CHIP_REV_YU_EC_U_A1  = 2,
	CHIP_REV_YU_EC_U_B0  = 3,
	CHIP_REV_YU_EC_U_B1  = 5,
};
enum yukon_fe_rev {
	CHIP_REV_YU_FE_A1    = 1,
	CHIP_REV_YU_FE_A2    = 2,
};
enum yukon_fe_p_rev {
	CHIP_REV_YU_FE2_A0   = 0,
};
enum yukon_ex_rev {
	CHIP_REV_YU_EX_A0    = 1,
	CHIP_REV_YU_EX_B0    = 2,
};
enum yukon_supr_rev {
	CHIP_REV_YU_SU_A0    = 0,
	CHIP_REV_YU_SU_B0    = 1,
	CHIP_REV_YU_SU_B1    = 3,
};

enum yukon_prm_rev {
	CHIP_REV_YU_PRM_Z1   = 1,
	CHIP_REV_YU_PRM_A0   = 2,
};

/*	B2_Y2_CLK_GATE	 8 bit	Clock Gating (Yukon-2 only) */
enum {
	Y2_STATUS_LNK2_INAC	= 1<<7, /* Status Link 2 inactive (0 = active) */
	Y2_CLK_GAT_LNK2_DIS	= 1<<6, /* Disable clock gating Link 2 */
	Y2_COR_CLK_LNK2_DIS	= 1<<5, /* Disable Core clock Link 2 */
	Y2_PCI_CLK_LNK2_DIS	= 1<<4, /* Disable PCI clock Link 2 */
	Y2_STATUS_LNK1_INAC	= 1<<3, /* Status Link 1 inactive (0 = active) */
	Y2_CLK_GAT_LNK1_DIS	= 1<<2, /* Disable clock gating Link 1 */
	Y2_COR_CLK_LNK1_DIS	= 1<<1, /* Disable Core clock Link 1 */
	Y2_PCI_CLK_LNK1_DIS	= 1<<0, /* Disable PCI clock Link 1 */
};

/*	B2_Y2_HW_RES	8 bit	HW Resources (Yukon-2 only) */
enum {
	CFG_LED_MODE_MSK	= 7<<2,	/* Bit  4.. 2:	LED Mode Mask */
	CFG_LINK_2_AVAIL	= 1<<1,	/* Link 2 available */
	CFG_LINK_1_AVAIL	= 1<<0,	/* Link 1 available */
};
#define CFG_LED_MODE(x)		(((x) & CFG_LED_MODE_MSK) >> 2)
#define CFG_DUAL_MAC_MSK	(CFG_LINK_2_AVAIL | CFG_LINK_1_AVAIL)


/* B2_Y2_CLK_CTRL	32 bit	Clock Frequency Control Register (Yukon-2/EC) */
enum {
	Y2_CLK_DIV_VAL_MSK	= 0xff<<16,/* Bit 23..16: Clock Divisor Value */
#define	Y2_CLK_DIV_VAL(x)	(((x)<<16) & Y2_CLK_DIV_VAL_MSK)
	Y2_CLK_DIV_VAL2_MSK	= 7<<21,   /* Bit 23..21: Clock Divisor Value */
	Y2_CLK_SELECT2_MSK	= 0x1f<<16,/* Bit 20..16: Clock Select */
#define Y2_CLK_DIV_VAL_2(x)	(((x)<<21) & Y2_CLK_DIV_VAL2_MSK)
#define Y2_CLK_SEL_VAL_2(x)	(((x)<<16) & Y2_CLK_SELECT2_MSK)
	Y2_CLK_DIV_ENA		= 1<<1, /* Enable  Core Clock Division */
	Y2_CLK_DIV_DIS		= 1<<0,	/* Disable Core Clock Division */
};

/*	B2_TI_CTRL		 8 bit	Timer control */
/*	B2_IRQM_CTRL	 8 bit	IRQ Moderation Timer Control */
enum {
	TIM_START	= 1<<2,	/* Start Timer */
	TIM_STOP	= 1<<1,	/* Stop  Timer */
	TIM_CLR_IRQ	= 1<<0,	/* Clear Timer IRQ (!IRQM) */
};

/*	B2_TI_TEST		 8 Bit	Timer Test */
/*	B2_IRQM_TEST	 8 bit	IRQ Moderation Timer Test */
/*	B28_DPT_TST		 8 bit	Descriptor Poll Timer Test Reg */
enum {
	TIM_T_ON	= 1<<2,	/* Test mode on */
	TIM_T_OFF	= 1<<1,	/* Test mode off */
	TIM_T_STEP	= 1<<0,	/* Test step */
};

/*	Y2_PEX_PHY_ADDR/DATA		PEX PHY address and data reg  (Yukon-2 only) */
enum {
	PEX_RD_ACCESS	= 1<<31, /* Access Mode Read = 1, Write = 0 */
	PEX_DB_ACCESS	= 1<<30, /* Access to debug register */
};

/*	B3_RAM_ADDR		32 bit	RAM Address, to read or write */
					/* Bit 31..19:	reserved */
#define RAM_ADR_RAN	0x0007ffffL	/* Bit 18.. 0:	RAM Address Range */
/* RAM Interface Registers */

/*	B3_RI_CTRL		16 bit	RAM Interface Control Register */
enum {
	RI_CLR_RD_PERR	= 1<<9,	/* Clear IRQ RAM Read Parity Err */
	RI_CLR_WR_PERR	= 1<<8,	/* Clear IRQ RAM Write Parity Err*/

	RI_RST_CLR	= 1<<1,	/* Clear RAM Interface Reset */
	RI_RST_SET	= 1<<0,	/* Set   RAM Interface Reset */
};

#define SK_RI_TO_53	36		/* RAM interface timeout */


/* Port related registers FIFO, and Arbiter */
#define SK_REG(port,reg)	(((port)<<7)+(reg))

/* Transmit Arbiter Registers MAC 1 and 2, use SK_REG() to access */
/*	TXA_ITI_INI		32 bit	Tx Arb Interval Timer Init Val */
/*	TXA_ITI_VAL		32 bit	Tx Arb Interval Timer Value */
/*	TXA_LIM_INI		32 bit	Tx Arb Limit Counter Init Val */
/*	TXA_LIM_VAL		32 bit	Tx Arb Limit Counter Value */

#define TXA_MAX_VAL	0x00ffffffUL	/* Bit 23.. 0:	Max TXA Timer/Cnt Val */

/*	TXA_CTRL		 8 bit	Tx Arbiter Control Register */
enum {
	TXA_ENA_FSYNC	= 1<<7,	/* Enable  force of sync Tx queue */
	TXA_DIS_FSYNC	= 1<<6,	/* Disable force of sync Tx queue */
	TXA_ENA_ALLOC	= 1<<5,	/* Enable  alloc of free bandwidth */
	TXA_DIS_ALLOC	= 1<<4,	/* Disable alloc of free bandwidth */
	TXA_START_RC	= 1<<3,	/* Start sync Rate Control */
	TXA_STOP_RC	= 1<<2,	/* Stop  sync Rate Control */
	TXA_ENA_ARB	= 1<<1,	/* Enable  Tx Arbiter */
	TXA_DIS_ARB	= 1<<0,	/* Disable Tx Arbiter */
};

/*
 *	Bank 4 - 5
 */
/* Transmit Arbiter Registers MAC 1 and 2, use SK_REG() to access */
enum {
	TXA_ITI_INI	= 0x0200,/* 32 bit	Tx Arb Interval Timer Init Val*/
	TXA_ITI_VAL	= 0x0204,/* 32 bit	Tx Arb Interval Timer Value */
	TXA_LIM_INI	= 0x0208,/* 32 bit	Tx Arb Limit Counter Init Val */
	TXA_LIM_VAL	= 0x020c,/* 32 bit	Tx Arb Limit Counter Value */
	TXA_CTRL	= 0x0210,/*  8 bit	Tx Arbiter Control Register */
	TXA_TEST	= 0x0211,/*  8 bit	Tx Arbiter Test Register */
	TXA_STAT	= 0x0212,/*  8 bit	Tx Arbiter Status Register */

	RSS_KEY		= 0x0220, /* RSS Key setup */
	RSS_CFG		= 0x0248, /* RSS Configuration */
};

enum {
	HASH_TCP_IPV6_EX_CTRL	= 1<<5,
	HASH_IPV6_EX_CTRL	= 1<<4,
	HASH_TCP_IPV6_CTRL	= 1<<3,
	HASH_IPV6_CTRL		= 1<<2,
	HASH_TCP_IPV4_CTRL	= 1<<1,
	HASH_IPV4_CTRL		= 1<<0,

	HASH_ALL		= 0x3f,
};

enum {
	B6_EXT_REG	= 0x0300,/* External registers (GENESIS only) */
	B7_CFG_SPC	= 0x0380,/* copy of the Configuration register */
	B8_RQ1_REGS	= 0x0400,/* Receive Queue 1 */
	B8_RQ2_REGS	= 0x0480,/* Receive Queue 2 */
	B8_TS1_REGS	= 0x0600,/* Transmit sync queue 1 */
	B8_TA1_REGS	= 0x0680,/* Transmit async queue 1 */
	B8_TS2_REGS	= 0x0700,/* Transmit sync queue 2 */
	B8_TA2_REGS	= 0x0780,/* Transmit sync queue 2 */
	B16_RAM_REGS	= 0x0800,/* RAM Buffer Registers */
};

/* Queue Register Offsets, use Q_ADDR() to access */
enum {
	B8_Q_REGS = 0x0400, /* base of Queue registers */
	Q_D	= 0x00,	/* 8*32	bit	Current Descriptor */
	Q_VLAN  = 0x20, /* 16 bit	Current VLAN Tag */
	Q_DONE	= 0x24,	/* 16 bit	Done Index */
	Q_AC_L	= 0x28,	/* 32 bit	Current Address Counter Low dWord */
	Q_AC_H	= 0x2c,	/* 32 bit	Current Address Counter High dWord */
	Q_BC	= 0x30,	/* 32 bit	Current Byte Counter */
	Q_CSR	= 0x34,	/* 32 bit	BMU Control/Status Register */
	Q_TEST	= 0x38,	/* 32 bit	Test/Control Register */

/* Yukon-2 */
	Q_WM	= 0x40,	/* 16 bit	FIFO Watermark */
	Q_AL	= 0x42,	/*  8 bit	FIFO Alignment */
	Q_RSP	= 0x44,	/* 16 bit	FIFO Read Shadow Pointer */
	Q_RSL	= 0x46,	/*  8 bit	FIFO Read Shadow Level */
	Q_RP	= 0x48,	/*  8 bit	FIFO Read Pointer */
	Q_RL	= 0x4a,	/*  8 bit	FIFO Read Level */
	Q_WP	= 0x4c,	/*  8 bit	FIFO Write Pointer */
	Q_WSP	= 0x4d,	/*  8 bit	FIFO Write Shadow Pointer */
	Q_WL	= 0x4e,	/*  8 bit	FIFO Write Level */
	Q_WSL	= 0x4f,	/*  8 bit	FIFO Write Shadow Level */
};
#define Q_ADDR(reg, offs) (B8_Q_REGS + (reg) + (offs))

/*	Q_TEST				32 bit	Test Register */
enum {
	/* Transmit */
	F_TX_CHK_AUTO_OFF = 1<<31, /* Tx checksum auto calc off (Yukon EX) */
	F_TX_CHK_AUTO_ON  = 1<<30, /* Tx checksum auto calc off (Yukon EX) */

	/* Receive */
	F_M_RX_RAM_DIS	= 1<<24, /* MAC Rx RAM Read Port disable */

	/* Hardware testbits not used */
};

/* Queue Prefetch Unit Offsets, use Y2_QADDR() to address (Yukon-2 only)*/
enum {
	Y2_B8_PREF_REGS		= 0x0450,

	PREF_UNIT_CTRL		= 0x00,	/* 32 bit	Control register */
	PREF_UNIT_LAST_IDX	= 0x04,	/* 16 bit	Last Index */
	PREF_UNIT_ADDR_LO	= 0x08,	/* 32 bit	List start addr, low part */
	PREF_UNIT_ADDR_HI	= 0x0c,	/* 32 bit	List start addr, high part*/
	PREF_UNIT_GET_IDX	= 0x10,	/* 16 bit	Get Index */
	PREF_UNIT_PUT_IDX	= 0x14,	/* 16 bit	Put Index */
	PREF_UNIT_FIFO_WP	= 0x20,	/*  8 bit	FIFO write pointer */
	PREF_UNIT_FIFO_RP	= 0x24,	/*  8 bit	FIFO read pointer */
	PREF_UNIT_FIFO_WM	= 0x28,	/*  8 bit	FIFO watermark */
	PREF_UNIT_FIFO_LEV	= 0x2c,	/*  8 bit	FIFO level */

	PREF_UNIT_MASK_IDX	= 0x0fff,
};
#define Y2_QADDR(q,reg)		(Y2_B8_PREF_REGS + (q) + (reg))

/* RAM Buffer Register Offsets */
enum {

	RB_START	= 0x00,/* 32 bit	RAM Buffer Start Address */
	RB_END	= 0x04,/* 32 bit	RAM Buffer End Address */
	RB_WP	= 0x08,/* 32 bit	RAM Buffer Write Pointer */
	RB_RP	= 0x0c,/* 32 bit	RAM Buffer Read Pointer */
	RB_RX_UTPP	= 0x10,/* 32 bit	Rx Upper Threshold, Pause Packet */
	RB_RX_LTPP	= 0x14,/* 32 bit	Rx Lower Threshold, Pause Packet */
	RB_RX_UTHP	= 0x18,/* 32 bit	Rx Upper Threshold, High Prio */
	RB_RX_LTHP	= 0x1c,/* 32 bit	Rx Lower Threshold, High Prio */
	/* 0x10 - 0x1f:	reserved at Tx RAM Buffer Registers */
	RB_PC	= 0x20,/* 32 bit	RAM Buffer Packet Counter */
	RB_LEV	= 0x24,/* 32 bit	RAM Buffer Level Register */
	RB_CTRL	= 0x28,/* 32 bit	RAM Buffer Control Register */
	RB_TST1	= 0x29,/*  8 bit	RAM Buffer Test Register 1 */
	RB_TST2	= 0x2a,/*  8 bit	RAM Buffer Test Register 2 */
};

/* Receive and Transmit Queues */
enum {
	Q_R1	= 0x0000,	/* Receive Queue 1 */
	Q_R2	= 0x0080,	/* Receive Queue 2 */
	Q_XS1	= 0x0200,	/* Synchronous Transmit Queue 1 */
	Q_XA1	= 0x0280,	/* Asynchronous Transmit Queue 1 */
	Q_XS2	= 0x0300,	/* Synchronous Transmit Queue 2 */
	Q_XA2	= 0x0380,	/* Asynchronous Transmit Queue 2 */
};

/* Different PHY Types */
enum {
	PHY_ADDR_MARV	= 0,
};

#define RB_ADDR(offs, queue) ((u16) B16_RAM_REGS + (queue) + (offs))


enum {
	LNK_SYNC_INI	= 0x0c30,/* 32 bit	Link Sync Cnt Init Value */
	LNK_SYNC_VAL	= 0x0c34,/* 32 bit	Link Sync Cnt Current Value */
	LNK_SYNC_CTRL	= 0x0c38,/*  8 bit	Link Sync Cnt Control Register */
	LNK_SYNC_TST	= 0x0c39,/*  8 bit	Link Sync Cnt Test Register */

	LNK_LED_REG	= 0x0c3c,/*  8 bit	Link LED Register */

/* Receive GMAC FIFO (YUKON and Yukon-2) */

	RX_GMF_EA	= 0x0c40,/* 32 bit	Rx GMAC FIFO End Address */
	RX_GMF_AF_THR	= 0x0c44,/* 32 bit	Rx GMAC FIFO Almost Full Thresh. */
	RX_GMF_CTRL_T	= 0x0c48,/* 32 bit	Rx GMAC FIFO Control/Test */
	RX_GMF_FL_MSK	= 0x0c4c,/* 32 bit	Rx GMAC FIFO Flush Mask */
	RX_GMF_FL_THR	= 0x0c50,/* 16 bit	Rx GMAC FIFO Flush Threshold */
	RX_GMF_FL_CTRL	= 0x0c52,/* 16 bit	Rx GMAC FIFO Flush Control */
	RX_GMF_TR_THR	= 0x0c54,/* 32 bit	Rx Truncation Threshold (Yukon-2) */
	RX_GMF_UP_THR	= 0x0c58,/* 16 bit	Rx Upper Pause Thr (Yukon-EC_U) */
	RX_GMF_LP_THR	= 0x0c5a,/* 16 bit	Rx Lower Pause Thr (Yukon-EC_U) */
	RX_GMF_VLAN	= 0x0c5c,/* 32 bit	Rx VLAN Type Register (Yukon-2) */
	RX_GMF_WP	= 0x0c60,/* 32 bit	Rx GMAC FIFO Write Pointer */

	RX_GMF_WLEV	= 0x0c68,/* 32 bit	Rx GMAC FIFO Write Level */

	RX_GMF_RP	= 0x0c70,/* 32 bit	Rx GMAC FIFO Read Pointer */

	RX_GMF_RLEV	= 0x0c78,/* 32 bit	Rx GMAC FIFO Read Level */
};


/*	Q_BC			32 bit	Current Byte Counter */

/* BMU Control Status Registers */
/*	B0_R1_CSR		32 bit	BMU Ctrl/Stat Rx Queue 1 */
/*	B0_R2_CSR		32 bit	BMU Ctrl/Stat Rx Queue 2 */
/*	B0_XA1_CSR		32 bit	BMU Ctrl/Stat Sync Tx Queue 1 */
/*	B0_XS1_CSR		32 bit	BMU Ctrl/Stat Async Tx Queue 1 */
/*	B0_XA2_CSR		32 bit	BMU Ctrl/Stat Sync Tx Queue 2 */
/*	B0_XS2_CSR		32 bit	BMU Ctrl/Stat Async Tx Queue 2 */
/*	Q_CSR			32 bit	BMU Control/Status Register */

/* Rx BMU Control / Status Registers (Yukon-2) */
enum {
	BMU_IDLE	= 1<<31, /* BMU Idle State */
	BMU_RX_TCP_PKT	= 1<<30, /* Rx TCP Packet (when RSS Hash enabled) */
	BMU_RX_IP_PKT	= 1<<29, /* Rx IP  Packet (when RSS Hash enabled) */

	BMU_ENA_RX_RSS_HASH = 1<<15, /* Enable  Rx RSS Hash */
	BMU_DIS_RX_RSS_HASH = 1<<14, /* Disable Rx RSS Hash */
	BMU_ENA_RX_CHKSUM = 1<<13, /* Enable  Rx TCP/IP Checksum Check */
	BMU_DIS_RX_CHKSUM = 1<<12, /* Disable Rx TCP/IP Checksum Check */
	BMU_CLR_IRQ_PAR	= 1<<11, /* Clear IRQ on Parity errors (Rx) */
	BMU_CLR_IRQ_TCP	= 1<<11, /* Clear IRQ on TCP segment. error (Tx) */
	BMU_CLR_IRQ_CHK	= 1<<10, /* Clear IRQ Check */
	BMU_STOP	= 1<<9, /* Stop  Rx/Tx Queue */
	BMU_START	= 1<<8, /* Start Rx/Tx Queue */
	BMU_FIFO_OP_ON	= 1<<7, /* FIFO Operational On */
	BMU_FIFO_OP_OFF	= 1<<6, /* FIFO Operational Off */
	BMU_FIFO_ENA	= 1<<5, /* Enable FIFO */
	BMU_FIFO_RST	= 1<<4, /* Reset  FIFO */
	BMU_OP_ON	= 1<<3, /* BMU Operational On */
	BMU_OP_OFF	= 1<<2, /* BMU Operational Off */
	BMU_RST_CLR	= 1<<1, /* Clear BMU Reset (Enable) */
	BMU_RST_SET	= 1<<0, /* Set   BMU Reset */

	BMU_CLR_RESET	= BMU_FIFO_RST | BMU_OP_OFF | BMU_RST_CLR,
	BMU_OPER_INIT	= BMU_CLR_IRQ_PAR | BMU_CLR_IRQ_CHK | BMU_START |
			  BMU_FIFO_ENA | BMU_OP_ON,

	BMU_WM_DEFAULT = 0x600,
	BMU_WM_PEX     = 0x80,
};

/* Tx BMU Control / Status Registers (Yukon-2) */
								/* Bit 31: same as for Rx */
enum {
	BMU_TX_IPIDINCR_ON	= 1<<13, /* Enable  IP ID Increment */
	BMU_TX_IPIDINCR_OFF	= 1<<12, /* Disable IP ID Increment */
	BMU_TX_CLR_IRQ_TCP	= 1<<11, /* Clear IRQ on TCP segment length mismatch */
};

/*	TBMU_TEST			0x06B8	Transmit BMU Test Register */
enum {
	TBMU_TEST_BMU_TX_CHK_AUTO_OFF		= 1<<31, /* BMU Tx Checksum Auto Calculation Disable */
	TBMU_TEST_BMU_TX_CHK_AUTO_ON		= 1<<30, /* BMU Tx Checksum Auto Calculation Enable */
	TBMU_TEST_HOME_ADD_PAD_FIX1_EN		= 1<<29, /* Home Address Paddiing FIX1 Enable */
	TBMU_TEST_HOME_ADD_PAD_FIX1_DIS		= 1<<28, /* Home Address Paddiing FIX1 Disable */
	TBMU_TEST_ROUTING_ADD_FIX_EN		= 1<<27, /* Routing Address Fix Enable */
	TBMU_TEST_ROUTING_ADD_FIX_DIS		= 1<<26, /* Routing Address Fix Disable */
	TBMU_TEST_HOME_ADD_FIX_EN		= 1<<25, /* Home address checksum fix enable */
	TBMU_TEST_HOME_ADD_FIX_DIS		= 1<<24, /* Home address checksum fix disable */

	TBMU_TEST_TEST_RSPTR_ON			= 1<<22, /* Testmode Shadow Read Ptr On */
	TBMU_TEST_TEST_RSPTR_OFF		= 1<<21, /* Testmode Shadow Read Ptr Off */
	TBMU_TEST_TESTSTEP_RSPTR		= 1<<20, /* Teststep Shadow Read Ptr */

	TBMU_TEST_TEST_RPTR_ON			= 1<<18, /* Testmode Read Ptr On */
	TBMU_TEST_TEST_RPTR_OFF			= 1<<17, /* Testmode Read Ptr Off */
	TBMU_TEST_TESTSTEP_RPTR			= 1<<16, /* Teststep Read Ptr */

	TBMU_TEST_TEST_WSPTR_ON			= 1<<14, /* Testmode Shadow Write Ptr On */
	TBMU_TEST_TEST_WSPTR_OFF		= 1<<13, /* Testmode Shadow Write Ptr Off */
	TBMU_TEST_TESTSTEP_WSPTR		= 1<<12, /* Teststep Shadow Write Ptr */

	TBMU_TEST_TEST_WPTR_ON			= 1<<10, /* Testmode Write Ptr On */
	TBMU_TEST_TEST_WPTR_OFF			= 1<<9, /* Testmode Write Ptr Off */
	TBMU_TEST_TESTSTEP_WPTR			= 1<<8,			/* Teststep Write Ptr */

	TBMU_TEST_TEST_REQ_NB_ON		= 1<<6, /* Testmode Req Nbytes/Addr On */
	TBMU_TEST_TEST_REQ_NB_OFF		= 1<<5, /* Testmode Req Nbytes/Addr Off */
	TBMU_TEST_TESTSTEP_REQ_NB		= 1<<4, /* Teststep Req Nbytes/Addr */

	TBMU_TEST_TEST_DONE_IDX_ON		= 1<<2, /* Testmode Done Index On */
	TBMU_TEST_TEST_DONE_IDX_OFF		= 1<<1, /* Testmode Done Index Off */
	TBMU_TEST_TESTSTEP_DONE_IDX		= 1<<0,	/* Teststep Done Index */
};

/* Queue Prefetch Unit Offsets, use Y2_QADDR() to address (Yukon-2 only)*/
/* PREF_UNIT_CTRL	32 bit	Prefetch Control register */
enum {
	PREF_UNIT_OP_ON		= 1<<3,	/* prefetch unit operational */
	PREF_UNIT_OP_OFF	= 1<<2,	/* prefetch unit not operational */
	PREF_UNIT_RST_CLR	= 1<<1,	/* Clear Prefetch Unit Reset */
	PREF_UNIT_RST_SET	= 1<<0,	/* Set   Prefetch Unit Reset */
};

/* RAM Buffer Register Offsets, use RB_ADDR(Queue, Offs) to access */
/*	RB_START		32 bit	RAM Buffer Start Address */
/*	RB_END			32 bit	RAM Buffer End Address */
/*	RB_WP			32 bit	RAM Buffer Write Pointer */
/*	RB_RP			32 bit	RAM Buffer Read Pointer */
/*	RB_RX_UTPP		32 bit	Rx Upper Threshold, Pause Pack */
/*	RB_RX_LTPP		32 bit	Rx Lower Threshold, Pause Pack */
/*	RB_RX_UTHP		32 bit	Rx Upper Threshold, High Prio */
/*	RB_RX_LTHP		32 bit	Rx Lower Threshold, High Prio */
/*	RB_PC			32 bit	RAM Buffer Packet Counter */
/*	RB_LEV			32 bit	RAM Buffer Level Register */

#define RB_MSK	0x0007ffff	/* Bit 18.. 0:	RAM Buffer Pointer Bits */
/*	RB_TST2			 8 bit	RAM Buffer Test Register 2 */
/*	RB_TST1			 8 bit	RAM Buffer Test Register 1 */

/*	RB_CTRL			 8 bit	RAM Buffer Control Register */
enum {
	RB_ENA_STFWD	= 1<<5,	/* Enable  Store & Forward */
	RB_DIS_STFWD	= 1<<4,	/* Disable Store & Forward */
	RB_ENA_OP_MD	= 1<<3,	/* Enable  Operation Mode */
	RB_DIS_OP_MD	= 1<<2,	/* Disable Operation Mode */
	RB_RST_CLR	= 1<<1,	/* Clear RAM Buf STM Reset */
	RB_RST_SET	= 1<<0,	/* Set   RAM Buf STM Reset */
};


/* Transmit GMAC FIFO (YUKON only) */
enum {
	TX_GMF_EA	= 0x0d40,/* 32 bit	Tx GMAC FIFO End Address */
	TX_GMF_AE_THR	= 0x0d44,/* 32 bit	Tx GMAC FIFO Almost Empty Thresh.*/
	TX_GMF_CTRL_T	= 0x0d48,/* 32 bit	Tx GMAC FIFO Control/Test */

	TX_GMF_WP	= 0x0d60,/* 32 bit 	Tx GMAC FIFO Write Pointer */
	TX_GMF_WSP	= 0x0d64,/* 32 bit 	Tx GMAC FIFO Write Shadow Ptr. */
	TX_GMF_WLEV	= 0x0d68,/* 32 bit 	Tx GMAC FIFO Write Level */

	TX_GMF_RP	= 0x0d70,/* 32 bit 	Tx GMAC FIFO Read Pointer */
	TX_GMF_RSTP	= 0x0d74,/* 32 bit 	Tx GMAC FIFO Restart Pointer */
	TX_GMF_RLEV	= 0x0d78,/* 32 bit 	Tx GMAC FIFO Read Level */

	/* Threshold values for Yukon-EC Ultra and Extreme */
	ECU_AE_THR	= 0x0070, /* Almost Empty Threshold */
	ECU_TXFF_LEV	= 0x01a0, /* Tx BMU FIFO Level */
	ECU_JUMBO_WM	= 0x0080, /* Jumbo Mode Watermark */
};

/* Descriptor Poll Timer Registers */
enum {
	B28_DPT_INI	= 0x0e00,/* 24 bit	Descriptor Poll Timer Init Val */
	B28_DPT_VAL	= 0x0e04,/* 24 bit	Descriptor Poll Timer Curr Val */
	B28_DPT_CTRL	= 0x0e08,/*  8 bit	Descriptor Poll Timer Ctrl Reg */

	B28_DPT_TST	= 0x0e0a,/*  8 bit	Descriptor Poll Timer Test Reg */
};

/* Time Stamp Timer Registers (YUKON only) */
enum {
	GMAC_TI_ST_VAL	= 0x0e14,/* 32 bit	Time Stamp Timer Curr Val */
	GMAC_TI_ST_CTRL	= 0x0e18,/*  8 bit	Time Stamp Timer Ctrl Reg */
	GMAC_TI_ST_TST	= 0x0e1a,/*  8 bit	Time Stamp Timer Test Reg */
};

/* Polling Unit Registers (Yukon-2 only) */
enum {
	POLL_CTRL	= 0x0e20, /* 32 bit	Polling Unit Control Reg */
	POLL_LAST_IDX	= 0x0e24,/* 16 bit	Polling Unit List Last Index */

	POLL_LIST_ADDR_LO= 0x0e28,/* 32 bit	Poll. List Start Addr (low) */
	POLL_LIST_ADDR_HI= 0x0e2c,/* 32 bit	Poll. List Start Addr (high) */
};

enum {
	SMB_CFG		 = 0x0e40, /* 32 bit	SMBus Config Register */
	SMB_CSR		 = 0x0e44, /* 32 bit	SMBus Control/Status Register */
};

enum {
	CPU_WDOG	 = 0x0e48, /* 32 bit	Watchdog Register  */
	CPU_CNTR	 = 0x0e4C, /* 32 bit	Counter Register  */
	CPU_TIM		 = 0x0e50,/* 32 bit	Timer Compare Register  */
	CPU_AHB_ADDR	 = 0x0e54, /* 32 bit	CPU AHB Debug  Register  */
	CPU_AHB_WDATA	 = 0x0e58, /* 32 bit	CPU AHB Debug  Register  */
	CPU_AHB_RDATA	 = 0x0e5C, /* 32 bit	CPU AHB Debug  Register  */
	HCU_MAP_BASE	 = 0x0e60, /* 32 bit	Reset Mapping Base */
	CPU_AHB_CTRL	 = 0x0e64, /* 32 bit	CPU AHB Debug  Register  */
	HCU_CCSR	 = 0x0e68, /* 32 bit	CPU Control and Status Register */
	HCU_HCSR	 = 0x0e6C, /* 32 bit	Host Control and Status Register */
};

/* ASF Subsystem Registers (Yukon-2 only) */
enum {
	B28_Y2_SMB_CONFIG  = 0x0e40,/* 32 bit	ASF SMBus Config Register */
	B28_Y2_SMB_CSD_REG = 0x0e44,/* 32 bit	ASF SMB Control/Status/Data */
	B28_Y2_ASF_IRQ_V_BASE=0x0e60,/* 32 bit	ASF IRQ Vector Base */

	B28_Y2_ASF_STAT_CMD= 0x0e68,/* 32 bit	ASF Status and Command Reg */
	B28_Y2_ASF_HOST_COM= 0x0e6c,/* 32 bit	ASF Host Communication Reg */
	B28_Y2_DATA_REG_1  = 0x0e70,/* 32 bit	ASF/Host Data Register 1 */
	B28_Y2_DATA_REG_2  = 0x0e74,/* 32 bit	ASF/Host Data Register 2 */
	B28_Y2_DATA_REG_3  = 0x0e78,/* 32 bit	ASF/Host Data Register 3 */
	B28_Y2_DATA_REG_4  = 0x0e7c,/* 32 bit	ASF/Host Data Register 4 */
};

/* Status BMU Registers (Yukon-2 only)*/
enum {
	STAT_CTRL	= 0x0e80,/* 32 bit	Status BMU Control Reg */
	STAT_LAST_IDX	= 0x0e84,/* 16 bit	Status BMU Last Index */

	STAT_LIST_ADDR_LO= 0x0e88,/* 32 bit	Status List Start Addr (low) */
	STAT_LIST_ADDR_HI= 0x0e8c,/* 32 bit	Status List Start Addr (high) */
	STAT_TXA1_RIDX	= 0x0e90,/* 16 bit	Status TxA1 Report Index Reg */
	STAT_TXS1_RIDX	= 0x0e92,/* 16 bit	Status TxS1 Report Index Reg */
	STAT_TXA2_RIDX	= 0x0e94,/* 16 bit	Status TxA2 Report Index Reg */
	STAT_TXS2_RIDX	= 0x0e96,/* 16 bit	Status TxS2 Report Index Reg */
	STAT_TX_IDX_TH	= 0x0e98,/* 16 bit	Status Tx Index Threshold Reg */
	STAT_PUT_IDX	= 0x0e9c,/* 16 bit	Status Put Index Reg */

/* FIFO Control/Status Registers (Yukon-2 only)*/
	STAT_FIFO_WP	= 0x0ea0,/*  8 bit	Status FIFO Write Pointer Reg */
	STAT_FIFO_RP	= 0x0ea4,/*  8 bit	Status FIFO Read Pointer Reg */
	STAT_FIFO_RSP	= 0x0ea6,/*  8 bit	Status FIFO Read Shadow Ptr */
	STAT_FIFO_LEVEL	= 0x0ea8,/*  8 bit	Status FIFO Level Reg */
	STAT_FIFO_SHLVL	= 0x0eaa,/*  8 bit	Status FIFO Shadow Level Reg */
	STAT_FIFO_WM	= 0x0eac,/*  8 bit	Status FIFO Watermark Reg */
	STAT_FIFO_ISR_WM= 0x0ead,/*  8 bit	Status FIFO ISR Watermark Reg */

/* Level and ISR Timer Registers (Yukon-2 only)*/
	STAT_LEV_TIMER_INI= 0x0eb0,/* 32 bit	Level Timer Init. Value Reg */
	STAT_LEV_TIMER_CNT= 0x0eb4,/* 32 bit	Level Timer Counter Reg */
	STAT_LEV_TIMER_CTRL= 0x0eb8,/*  8 bit	Level Timer Control Reg */
	STAT_LEV_TIMER_TEST= 0x0eb9,/*  8 bit	Level Timer Test Reg */
	STAT_TX_TIMER_INI  = 0x0ec0,/* 32 bit	Tx Timer Init. Value Reg */
	STAT_TX_TIMER_CNT  = 0x0ec4,/* 32 bit	Tx Timer Counter Reg */
	STAT_TX_TIMER_CTRL = 0x0ec8,/*  8 bit	Tx Timer Control Reg */
	STAT_TX_TIMER_TEST = 0x0ec9,/*  8 bit	Tx Timer Test Reg */
	STAT_ISR_TIMER_INI = 0x0ed0,/* 32 bit	ISR Timer Init. Value Reg */
	STAT_ISR_TIMER_CNT = 0x0ed4,/* 32 bit	ISR Timer Counter Reg */
	STAT_ISR_TIMER_CTRL= 0x0ed8,/*  8 bit	ISR Timer Control Reg */
	STAT_ISR_TIMER_TEST= 0x0ed9,/*  8 bit	ISR Timer Test Reg */
};

enum {
	LINKLED_OFF 	     = 0x01,
	LINKLED_ON  	     = 0x02,
	LINKLED_LINKSYNC_OFF = 0x04,
	LINKLED_LINKSYNC_ON  = 0x08,
	LINKLED_BLINK_OFF    = 0x10,
	LINKLED_BLINK_ON     = 0x20,
};

/* GMAC and GPHY Control Registers (YUKON only) */
enum {
	GMAC_CTRL	= 0x0f00,/* 32 bit	GMAC Control Reg */
	GPHY_CTRL	= 0x0f04,/* 32 bit	GPHY Control Reg */
	GMAC_IRQ_SRC	= 0x0f08,/*  8 bit	GMAC Interrupt Source Reg */
	GMAC_IRQ_MSK	= 0x0f0c,/*  8 bit	GMAC Interrupt Mask Reg */
	GMAC_LINK_CTRL	= 0x0f10,/* 16 bit	Link Control Reg */

/* Wake-up Frame Pattern Match Control Registers (YUKON only) */
	WOL_CTRL_STAT	= 0x0f20,/* 16 bit	WOL Control/Status Reg */
	WOL_MATCH_CTL	= 0x0f22,/*  8 bit	WOL Match Control Reg */
	WOL_MATCH_RES	= 0x0f23,/*  8 bit	WOL Match Result Reg */
	WOL_MAC_ADDR	= 0x0f24,/* 32 bit	WOL MAC Address */
	WOL_PATT_RPTR	= 0x0f2c,/*  8 bit	WOL Pattern Read Pointer */

/* WOL Pattern Length Registers (YUKON only) */
	WOL_PATT_LEN_LO	= 0x0f30,/* 32 bit	WOL Pattern Length 3..0 */
	WOL_PATT_LEN_HI	= 0x0f34,/* 24 bit	WOL Pattern Length 6..4 */

/* WOL Pattern Counter Registers (YUKON only) */
	WOL_PATT_CNT_0	= 0x0f38,/* 32 bit	WOL Pattern Counter 3..0 */
	WOL_PATT_CNT_4	= 0x0f3c,/* 24 bit	WOL Pattern Counter 6..4 */
};
#define WOL_REGS(port, x)	(x + (port)*0x80)

enum {
	WOL_PATT_RAM_1	= 0x1000,/*  WOL Pattern RAM Link 1 */
	WOL_PATT_RAM_2	= 0x1400,/*  WOL Pattern RAM Link 2 */
};
#define WOL_PATT_RAM_BASE(port)	(WOL_PATT_RAM_1 + (port)*0x400)

enum {
	BASE_GMAC_1	= 0x2800,/* GMAC 1 registers */
	BASE_GMAC_2	= 0x3800,/* GMAC 2 registers */
};

/*
 * Marvel-PHY Registers, indirect addressed over GMAC
 */
enum {
	PHY_MARV_CTRL		= 0x00,/* 16 bit r/w	PHY Control Register */
	PHY_MARV_STAT		= 0x01,/* 16 bit r/o	PHY Status Register */
	PHY_MARV_ID0		= 0x02,/* 16 bit r/o	PHY ID0 Register */
	PHY_MARV_ID1		= 0x03,/* 16 bit r/o	PHY ID1 Register */
	PHY_MARV_AUNE_ADV	= 0x04,/* 16 bit r/w	Auto-Neg. Advertisement */
	PHY_MARV_AUNE_LP	= 0x05,/* 16 bit r/o	Link Part Ability Reg */
	PHY_MARV_AUNE_EXP	= 0x06,/* 16 bit r/o	Auto-Neg. Expansion Reg */
	PHY_MARV_NEPG		= 0x07,/* 16 bit r/w	Next Page Register */
	PHY_MARV_NEPG_LP	= 0x08,/* 16 bit r/o	Next Page Link Partner */
	/* Marvel-specific registers */
	PHY_MARV_1000T_CTRL	= 0x09,/* 16 bit r/w	1000Base-T Control Reg */
	PHY_MARV_1000T_STAT	= 0x0a,/* 16 bit r/o	1000Base-T Status Reg */
	PHY_MARV_EXT_STAT	= 0x0f,/* 16 bit r/o	Extended Status Reg */
	PHY_MARV_PHY_CTRL	= 0x10,/* 16 bit r/w	PHY Specific Ctrl Reg */
	PHY_MARV_PHY_STAT	= 0x11,/* 16 bit r/o	PHY Specific Stat Reg */
	PHY_MARV_INT_MASK	= 0x12,/* 16 bit r/w	Interrupt Mask Reg */
	PHY_MARV_INT_STAT	= 0x13,/* 16 bit r/o	Interrupt Status Reg */
	PHY_MARV_EXT_CTRL	= 0x14,/* 16 bit r/w	Ext. PHY Specific Ctrl */
	PHY_MARV_RXE_CNT	= 0x15,/* 16 bit r/w	Receive Error Counter */
	PHY_MARV_EXT_ADR	= 0x16,/* 16 bit r/w	Ext. Ad. for Cable Diag. */
	PHY_MARV_PORT_IRQ	= 0x17,/* 16 bit r/o	Port 0 IRQ (88E1111 only) */
	PHY_MARV_LED_CTRL	= 0x18,/* 16 bit r/w	LED Control Reg */
	PHY_MARV_LED_OVER	= 0x19,/* 16 bit r/w	Manual LED Override Reg */
	PHY_MARV_EXT_CTRL_2	= 0x1a,/* 16 bit r/w	Ext. PHY Specific Ctrl 2 */
	PHY_MARV_EXT_P_STAT	= 0x1b,/* 16 bit r/w	Ext. PHY Spec. Stat Reg */
	PHY_MARV_CABLE_DIAG	= 0x1c,/* 16 bit r/o	Cable Diagnostic Reg */
	PHY_MARV_PAGE_ADDR	= 0x1d,/* 16 bit r/w	Extended Page Address Reg */
	PHY_MARV_PAGE_DATA	= 0x1e,/* 16 bit r/w	Extended Page Data Reg */

/* for 10/100 Fast Ethernet PHY (88E3082 only) */
	PHY_MARV_FE_LED_PAR	= 0x16,/* 16 bit r/w	LED Parallel Select Reg. */
	PHY_MARV_FE_LED_SER	= 0x17,/* 16 bit r/w	LED Stream Select S. LED */
	PHY_MARV_FE_VCT_TX	= 0x1a,/* 16 bit r/w	VCT Reg. for TXP/N Pins */
	PHY_MARV_FE_VCT_RX	= 0x1b,/* 16 bit r/o	VCT Reg. for RXP/N Pins */
	PHY_MARV_FE_SPEC_2	= 0x1c,/* 16 bit r/w	Specific Control Reg. 2 */
};

enum {
	PHY_CT_RESET	= 1<<15, /* Bit 15: (sc)	clear all PHY related regs */
	PHY_CT_LOOP	= 1<<14, /* Bit 14:	enable Loopback over PHY */
	PHY_CT_SPS_LSB	= 1<<13, /* Bit 13:	Speed select, lower bit */
	PHY_CT_ANE	= 1<<12, /* Bit 12:	Auto-Negotiation Enabled */
	PHY_CT_PDOWN	= 1<<11, /* Bit 11:	Power Down Mode */
	PHY_CT_ISOL	= 1<<10, /* Bit 10:	Isolate Mode */
	PHY_CT_RE_CFG	= 1<<9, /* Bit  9:	(sc) Restart Auto-Negotiation */
	PHY_CT_DUP_MD	= 1<<8, /* Bit  8:	Duplex Mode */
	PHY_CT_COL_TST	= 1<<7, /* Bit  7:	Collision Test enabled */
	PHY_CT_SPS_MSB	= 1<<6, /* Bit  6:	Speed select, upper bit */
};

enum {
	PHY_CT_SP1000	= PHY_CT_SPS_MSB, /* enable speed of 1000 Mbps */
	PHY_CT_SP100	= PHY_CT_SPS_LSB, /* enable speed of  100 Mbps */
	PHY_CT_SP10	= 0,		  /* enable speed of   10 Mbps */
};

enum {
	PHY_ST_EXT_ST	= 1<<8, /* Bit  8:	Extended Status Present */

	PHY_ST_PRE_SUP	= 1<<6, /* Bit  6:	Preamble Suppression */
	PHY_ST_AN_OVER	= 1<<5, /* Bit  5:	Auto-Negotiation Over */
	PHY_ST_REM_FLT	= 1<<4, /* Bit  4:	Remote Fault Condition Occurred */
	PHY_ST_AN_CAP	= 1<<3, /* Bit  3:	Auto-Negotiation Capability */
	PHY_ST_LSYNC	= 1<<2, /* Bit  2:	Link Synchronized */
	PHY_ST_JAB_DET	= 1<<1, /* Bit  1:	Jabber Detected */
	PHY_ST_EXT_REG	= 1<<0, /* Bit  0:	Extended Register available */
};

enum {
	PHY_I1_OUI_MSK	= 0x3f<<10, /* Bit 15..10:	Organization Unique ID */
	PHY_I1_MOD_NUM	= 0x3f<<4, /* Bit  9.. 4:	Model Number */
	PHY_I1_REV_MSK	= 0xf, /* Bit  3.. 0:	Revision Number */
};

/* different Marvell PHY Ids */
enum {
	PHY_MARV_ID0_VAL= 0x0141, /* Marvell Unique Identifier */

	PHY_BCOM_ID1_A1	= 0x6041,
	PHY_BCOM_ID1_B2	= 0x6043,
	PHY_BCOM_ID1_C0	= 0x6044,
	PHY_BCOM_ID1_C5	= 0x6047,

	PHY_MARV_ID1_B0	= 0x0C23, /* Yukon 	(PHY 88E1011) */
	PHY_MARV_ID1_B2	= 0x0C25, /* Yukon-Plus (PHY 88E1011) */
	PHY_MARV_ID1_C2	= 0x0CC2, /* Yukon-EC	(PHY 88E1111) */
	PHY_MARV_ID1_Y2	= 0x0C91, /* Yukon-2	(PHY 88E1112) */
	PHY_MARV_ID1_FE = 0x0C83, /* Yukon-FE   (PHY 88E3082 Rev.A1) */
	PHY_MARV_ID1_ECU= 0x0CB0, /* Yukon-ECU  (PHY 88E1149 Rev.B2?) */
};

/* Advertisement register bits */
enum {
	PHY_AN_NXT_PG	= 1<<15, /* Bit 15:	Request Next Page */
	PHY_AN_ACK	= 1<<14, /* Bit 14:	(ro) Acknowledge Received */
	PHY_AN_RF	= 1<<13, /* Bit 13:	Remote Fault Bits */

	PHY_AN_PAUSE_ASYM = 1<<11,/* Bit 11:	Try for asymmetric */
	PHY_AN_PAUSE_CAP = 1<<10, /* Bit 10:	Try for pause */
	PHY_AN_100BASE4	= 1<<9, /* Bit 9:	Try for 100mbps 4k packets */
	PHY_AN_100FULL	= 1<<8, /* Bit 8:	Try for 100mbps full-duplex */
	PHY_AN_100HALF	= 1<<7, /* Bit 7:	Try for 100mbps half-duplex */
	PHY_AN_10FULL	= 1<<6, /* Bit 6:	Try for 10mbps full-duplex */
	PHY_AN_10HALF	= 1<<5, /* Bit 5:	Try for 10mbps half-duplex */
	PHY_AN_CSMA	= 1<<0, /* Bit 0:	Only selector supported */
	PHY_AN_SEL	= 0x1f, /* Bit 4..0:	Selector Field, 00001=Ethernet*/
	PHY_AN_FULL	= PHY_AN_100FULL | PHY_AN_10FULL | PHY_AN_CSMA,
	PHY_AN_ALL	= PHY_AN_10HALF | PHY_AN_10FULL |
		  	  PHY_AN_100HALF | PHY_AN_100FULL,
};

/*****  PHY_BCOM_1000T_STAT	16 bit r/o	1000Base-T Status Reg *****/
/*****  PHY_MARV_1000T_STAT	16 bit r/o	1000Base-T Status Reg *****/
enum {
	PHY_B_1000S_MSF	= 1<<15, /* Bit 15:	Master/Slave Fault */
	PHY_B_1000S_MSR	= 1<<14, /* Bit 14:	Master/Slave Result */
	PHY_B_1000S_LRS	= 1<<13, /* Bit 13:	Local Receiver Status */
	PHY_B_1000S_RRS	= 1<<12, /* Bit 12:	Remote Receiver Status */
	PHY_B_1000S_LP_FD	= 1<<11, /* Bit 11:	Link Partner can FD */
	PHY_B_1000S_LP_HD	= 1<<10, /* Bit 10:	Link Partner can HD */
									/* Bit  9..8:	reserved */
	PHY_B_1000S_IEC	= 0xff, /* Bit  7..0:	Idle Error Count */
};

/** Marvell-Specific */
enum {
	PHY_M_AN_NXT_PG	= 1<<15, /* Request Next Page */
	PHY_M_AN_ACK	= 1<<14, /* (ro)	Acknowledge Received */
	PHY_M_AN_RF	= 1<<13, /* Remote Fault */

	PHY_M_AN_ASP	= 1<<11, /* Asymmetric Pause */
	PHY_M_AN_PC	= 1<<10, /* MAC Pause implemented */
	PHY_M_AN_100_T4	= 1<<9, /* Not cap. 100Base-T4 (always 0) */
	PHY_M_AN_100_FD	= 1<<8, /* Advertise 100Base-TX Full Duplex */
	PHY_M_AN_100_HD	= 1<<7, /* Advertise 100Base-TX Half Duplex */
	PHY_M_AN_10_FD	= 1<<6, /* Advertise 10Base-TX Full Duplex */
	PHY_M_AN_10_HD	= 1<<5, /* Advertise 10Base-TX Half Duplex */
	PHY_M_AN_SEL_MSK =0x1f<<4,	/* Bit  4.. 0: Selector Field Mask */
};

/* special defines for FIBER (88E1011S only) */
enum {
	PHY_M_AN_ASP_X	= 1<<8, /* Asymmetric Pause */
	PHY_M_AN_PC_X	= 1<<7, /* MAC Pause implemented */
	PHY_M_AN_1000X_AHD	= 1<<6, /* Advertise 10000Base-X Half Duplex */
	PHY_M_AN_1000X_AFD	= 1<<5, /* Advertise 10000Base-X Full Duplex */
};

/* Pause Bits (PHY_M_AN_ASP_X and PHY_M_AN_PC_X) encoding */
enum {
	PHY_M_P_NO_PAUSE_X	= 0<<7,/* Bit  8.. 7:	no Pause Mode */
	PHY_M_P_SYM_MD_X	= 1<<7, /* Bit  8.. 7:	symmetric Pause Mode */
	PHY_M_P_ASYM_MD_X	= 2<<7,/* Bit  8.. 7:	asymmetric Pause Mode */
	PHY_M_P_BOTH_MD_X	= 3<<7,/* Bit  8.. 7:	both Pause Mode */
};

/*****  PHY_MARV_1000T_CTRL	16 bit r/w	1000Base-T Control Reg *****/
enum {
	PHY_M_1000C_TEST	= 7<<13,/* Bit 15..13:	Test Modes */
	PHY_M_1000C_MSE	= 1<<12, /* Manual Master/Slave Enable */
	PHY_M_1000C_MSC	= 1<<11, /* M/S Configuration (1=Master) */
	PHY_M_1000C_MPD	= 1<<10, /* Multi-Port Device */
	PHY_M_1000C_AFD	= 1<<9, /* Advertise Full Duplex */
	PHY_M_1000C_AHD	= 1<<8, /* Advertise Half Duplex */
};

/*****  PHY_MARV_PHY_CTRL	16 bit r/w	PHY Specific Ctrl Reg *****/
enum {
	PHY_M_PC_TX_FFD_MSK	= 3<<14,/* Bit 15..14: Tx FIFO Depth Mask */
	PHY_M_PC_RX_FFD_MSK	= 3<<12,/* Bit 13..12: Rx FIFO Depth Mask */
	PHY_M_PC_ASS_CRS_TX	= 1<<11, /* Assert CRS on Transmit */
	PHY_M_PC_FL_GOOD	= 1<<10, /* Force Link Good */
	PHY_M_PC_EN_DET_MSK	= 3<<8,/* Bit  9.. 8: Energy Detect Mask */
	PHY_M_PC_ENA_EXT_D	= 1<<7, /* Enable Ext. Distance (10BT) */
	PHY_M_PC_MDIX_MSK	= 3<<5,/* Bit  6.. 5: MDI/MDIX Config. Mask */
	PHY_M_PC_DIS_125CLK	= 1<<4, /* Disable 125 CLK */
	PHY_M_PC_MAC_POW_UP	= 1<<3, /* MAC Power up */
	PHY_M_PC_SQE_T_ENA	= 1<<2, /* SQE Test Enabled */
	PHY_M_PC_POL_R_DIS	= 1<<1, /* Polarity Reversal Disabled */
	PHY_M_PC_DIS_JABBER	= 1<<0, /* Disable Jabber */
};

enum {
	PHY_M_PC_EN_DET		= 2<<8,	/* Energy Detect (Mode 1) */
	PHY_M_PC_EN_DET_PLUS	= 3<<8, /* Energy Detect Plus (Mode 2) */
};

#define PHY_M_PC_MDI_XMODE(x)	(((u16)(x)<<5) & PHY_M_PC_MDIX_MSK)

enum {
	PHY_M_PC_MAN_MDI	= 0, /* 00 = Manual MDI configuration */
	PHY_M_PC_MAN_MDIX	= 1, /* 01 = Manual MDIX configuration */
	PHY_M_PC_ENA_AUTO	= 3, /* 11 = Enable Automatic Crossover */
};

/* for Yukon-EC Ultra Gigabit Ethernet PHY (88E1149 only) */
enum {
	PHY_M_PC_COP_TX_DIS	= 1<<3, /* Copper Transmitter Disable */
	PHY_M_PC_POW_D_ENA	= 1<<2,	/* Power Down Enable */
};

/* for 10/100 Fast Ethernet PHY (88E3082 only) */
enum {
	PHY_M_PC_ENA_DTE_DT	= 1<<15, /* Enable Data Terminal Equ. (DTE) Detect */
	PHY_M_PC_ENA_ENE_DT	= 1<<14, /* Enable Energy Detect (sense & pulse) */
	PHY_M_PC_DIS_NLP_CK	= 1<<13, /* Disable Normal Link Puls (NLP) Check */
	PHY_M_PC_ENA_LIP_NP	= 1<<12, /* Enable Link Partner Next Page Reg. */
	PHY_M_PC_DIS_NLP_GN	= 1<<11, /* Disable Normal Link Puls Generation */

	PHY_M_PC_DIS_SCRAMB	= 1<<9, /* Disable Scrambler */
	PHY_M_PC_DIS_FEFI	= 1<<8, /* Disable Far End Fault Indic. (FEFI) */

	PHY_M_PC_SH_TP_SEL	= 1<<6, /* Shielded Twisted Pair Select */
	PHY_M_PC_RX_FD_MSK	= 3<<2,/* Bit  3.. 2: Rx FIFO Depth Mask */
};

/*****  PHY_MARV_PHY_STAT	16 bit r/o	PHY Specific Status Reg *****/
enum {
	PHY_M_PS_SPEED_MSK	= 3<<14, /* Bit 15..14: Speed Mask */
	PHY_M_PS_SPEED_1000	= 1<<15, /*		10 = 1000 Mbps */
	PHY_M_PS_SPEED_100	= 1<<14, /*		01 =  100 Mbps */
	PHY_M_PS_SPEED_10	= 0,	 /*		00 =   10 Mbps */
	PHY_M_PS_FULL_DUP	= 1<<13, /* Full Duplex */
	PHY_M_PS_PAGE_REC	= 1<<12, /* Page Received */
	PHY_M_PS_SPDUP_RES	= 1<<11, /* Speed & Duplex Resolved */
	PHY_M_PS_LINK_UP	= 1<<10, /* Link Up */
	PHY_M_PS_CABLE_MSK	= 7<<7,  /* Bit  9.. 7: Cable Length Mask */
	PHY_M_PS_MDI_X_STAT	= 1<<6,  /* MDI Crossover Stat (1=MDIX) */
	PHY_M_PS_DOWNS_STAT	= 1<<5,  /* Downshift Status (1=downsh.) */
	PHY_M_PS_ENDET_STAT	= 1<<4,  /* Energy Detect Status (1=act) */
	PHY_M_PS_TX_P_EN	= 1<<3,  /* Tx Pause Enabled */
	PHY_M_PS_RX_P_EN	= 1<<2,  /* Rx Pause Enabled */
	PHY_M_PS_POL_REV	= 1<<1,  /* Polarity Reversed */
	PHY_M_PS_JABBER		= 1<<0,  /* Jabber */
};

#define PHY_M_PS_PAUSE_MSK	(PHY_M_PS_TX_P_EN | PHY_M_PS_RX_P_EN)

/* for 10/100 Fast Ethernet PHY (88E3082 only) */
enum {
	PHY_M_PS_DTE_DETECT	= 1<<15, /* Data Terminal Equipment (DTE) Detected */
	PHY_M_PS_RES_SPEED	= 1<<14, /* Resolved Speed (1=100 Mbps, 0=10 Mbps */
};

enum {
	PHY_M_IS_AN_ERROR	= 1<<15, /* Auto-Negotiation Error */
	PHY_M_IS_LSP_CHANGE	= 1<<14, /* Link Speed Changed */
	PHY_M_IS_DUP_CHANGE	= 1<<13, /* Duplex Mode Changed */
	PHY_M_IS_AN_PR		= 1<<12, /* Page Received */
	PHY_M_IS_AN_COMPL	= 1<<11, /* Auto-Negotiation Completed */
	PHY_M_IS_LST_CHANGE	= 1<<10, /* Link Status Changed */
	PHY_M_IS_SYMB_ERROR	= 1<<9, /* Symbol Error */
	PHY_M_IS_FALSE_CARR	= 1<<8, /* False Carrier */
	PHY_M_IS_FIFO_ERROR	= 1<<7, /* FIFO Overflow/Underrun Error */
	PHY_M_IS_MDI_CHANGE	= 1<<6, /* MDI Crossover Changed */
	PHY_M_IS_DOWNSH_DET	= 1<<5, /* Downshift Detected */
	PHY_M_IS_END_CHANGE	= 1<<4, /* Energy Detect Changed */

	PHY_M_IS_DTE_CHANGE	= 1<<2, /* DTE Power Det. Status Changed */
	PHY_M_IS_POL_CHANGE	= 1<<1, /* Polarity Changed */
	PHY_M_IS_JABBER		= 1<<0, /* Jabber */

	PHY_M_DEF_MSK		= PHY_M_IS_LSP_CHANGE | PHY_M_IS_LST_CHANGE
				 | PHY_M_IS_DUP_CHANGE,
	PHY_M_AN_MSK	       = PHY_M_IS_AN_ERROR | PHY_M_IS_AN_COMPL,
};


/*****  PHY_MARV_EXT_CTRL	16 bit r/w	Ext. PHY Specific Ctrl *****/
enum {
	PHY_M_EC_ENA_BC_EXT = 1<<15, /* Enable Block Carr. Ext. (88E1111 only) */
	PHY_M_EC_ENA_LIN_LB = 1<<14, /* Enable Line Loopback (88E1111 only) */

	PHY_M_EC_DIS_LINK_P = 1<<12, /* Disable Link Pulses (88E1111 only) */
	PHY_M_EC_M_DSC_MSK  = 3<<10, /* Bit 11..10:	Master Downshift Counter */
					/* (88E1011 only) */
	PHY_M_EC_S_DSC_MSK  = 3<<8,/* Bit  9.. 8:	Slave  Downshift Counter */
				       /* (88E1011 only) */
	PHY_M_EC_M_DSC_MSK2 = 7<<9,/* Bit 11.. 9:	Master Downshift Counter */
					/* (88E1111 only) */
	PHY_M_EC_DOWN_S_ENA = 1<<8, /* Downshift Enable (88E1111 only) */
					/* !!! Errata in spec. (1 = disable) */
	PHY_M_EC_RX_TIM_CT  = 1<<7, /* RGMII Rx Timing Control*/
	PHY_M_EC_MAC_S_MSK  = 7<<4,/* Bit  6.. 4:	Def. MAC interface speed */
	PHY_M_EC_FIB_AN_ENA = 1<<3, /* Fiber Auto-Neg. Enable (88E1011S only) */
	PHY_M_EC_DTE_D_ENA  = 1<<2, /* DTE Detect Enable (88E1111 only) */
	PHY_M_EC_TX_TIM_CT  = 1<<1, /* RGMII Tx Timing Control */
	PHY_M_EC_TRANS_DIS  = 1<<0, /* Transmitter Disable (88E1111 only) */

	PHY_M_10B_TE_ENABLE = 1<<7, /* 10Base-Te Enable (88E8079 and above) */
};
#define PHY_M_EC_M_DSC(x)	((u16)(x)<<10 & PHY_M_EC_M_DSC_MSK)
					/* 00=1x; 01=2x; 10=3x; 11=4x */
#define PHY_M_EC_S_DSC(x)	((u16)(x)<<8 & PHY_M_EC_S_DSC_MSK)
					/* 00=dis; 01=1x; 10=2x; 11=3x */
#define PHY_M_EC_DSC_2(x)	((u16)(x)<<9 & PHY_M_EC_M_DSC_MSK2)
					/* 000=1x; 001=2x; 010=3x; 011=4x */
#define PHY_M_EC_MAC_S(x)	((u16)(x)<<4 & PHY_M_EC_MAC_S_MSK)
					/* 01X=0; 110=2.5; 111=25 (MHz) */

/* for Yukon-2 Gigabit Ethernet PHY (88E1112 only) */
enum {
	PHY_M_PC_DIS_LINK_Pa	= 1<<15,/* Disable Link Pulses */
	PHY_M_PC_DSC_MSK	= 7<<12,/* Bit 14..12:	Downshift Counter */
	PHY_M_PC_DOWN_S_ENA	= 1<<11,/* Downshift Enable */
};
/* !!! Errata in spec. (1 = disable) */

#define PHY_M_PC_DSC(x)			(((u16)(x)<<12) & PHY_M_PC_DSC_MSK)
											/* 100=5x; 101=6x; 110=7x; 111=8x */
enum {
	MAC_TX_CLK_0_MHZ	= 2,
	MAC_TX_CLK_2_5_MHZ	= 6,
	MAC_TX_CLK_25_MHZ 	= 7,
};

/*****  PHY_MARV_LED_CTRL	16 bit r/w	LED Control Reg *****/
enum {
	PHY_M_LEDC_DIS_LED	= 1<<15, /* Disable LED */
	PHY_M_LEDC_PULS_MSK	= 7<<12,/* Bit 14..12: Pulse Stretch Mask */
	PHY_M_LEDC_F_INT	= 1<<11, /* Force Interrupt */
	PHY_M_LEDC_BL_R_MSK	= 7<<8,/* Bit 10.. 8: Blink Rate Mask */
	PHY_M_LEDC_DP_C_LSB	= 1<<7, /* Duplex Control (LSB, 88E1111 only) */
	PHY_M_LEDC_TX_C_LSB	= 1<<6, /* Tx Control (LSB, 88E1111 only) */
	PHY_M_LEDC_LK_C_MSK	= 7<<3,/* Bit  5.. 3: Link Control Mask */
					/* (88E1111 only) */
};

enum {
	PHY_M_LEDC_LINK_MSK	= 3<<3,/* Bit  4.. 3: Link Control Mask */
									/* (88E1011 only) */
	PHY_M_LEDC_DP_CTRL	= 1<<2, /* Duplex Control */
	PHY_M_LEDC_DP_C_MSB	= 1<<2, /* Duplex Control (MSB, 88E1111 only) */
	PHY_M_LEDC_RX_CTRL	= 1<<1, /* Rx Activity / Link */
	PHY_M_LEDC_TX_CTRL	= 1<<0, /* Tx Activity / Link */
	PHY_M_LEDC_TX_C_MSB	= 1<<0, /* Tx Control (MSB, 88E1111 only) */
};

#define PHY_M_LED_PULS_DUR(x)	(((u16)(x)<<12) & PHY_M_LEDC_PULS_MSK)

/*****  PHY_MARV_PHY_STAT (page 3)16 bit r/w	Polarity Control Reg. *****/
enum {
	PHY_M_POLC_LS1M_MSK	= 0xf<<12, /* Bit 15..12: LOS,STAT1 Mix % Mask */
	PHY_M_POLC_IS0M_MSK	= 0xf<<8,  /* Bit 11.. 8: INIT,STAT0 Mix % Mask */
	PHY_M_POLC_LOS_MSK	= 0x3<<6,  /* Bit  7.. 6: LOS Pol. Ctrl. Mask */
	PHY_M_POLC_INIT_MSK	= 0x3<<4,  /* Bit  5.. 4: INIT Pol. Ctrl. Mask */
	PHY_M_POLC_STA1_MSK	= 0x3<<2,  /* Bit  3.. 2: STAT1 Pol. Ctrl. Mask */
	PHY_M_POLC_STA0_MSK	= 0x3,     /* Bit  1.. 0: STAT0 Pol. Ctrl. Mask */
};

#define PHY_M_POLC_LS1_P_MIX(x)	(((x)<<12) & PHY_M_POLC_LS1M_MSK)
#define PHY_M_POLC_IS0_P_MIX(x)	(((x)<<8) & PHY_M_POLC_IS0M_MSK)
#define PHY_M_POLC_LOS_CTRL(x)	(((x)<<6) & PHY_M_POLC_LOS_MSK)
#define PHY_M_POLC_INIT_CTRL(x)	(((x)<<4) & PHY_M_POLC_INIT_MSK)
#define PHY_M_POLC_STA1_CTRL(x)	(((x)<<2) & PHY_M_POLC_STA1_MSK)
#define PHY_M_POLC_STA0_CTRL(x)	(((x)<<0) & PHY_M_POLC_STA0_MSK)

enum {
	PULS_NO_STR	= 0,/* no pulse stretching */
	PULS_21MS	= 1,/* 21 ms to 42 ms */
	PULS_42MS	= 2,/* 42 ms to 84 ms */
	PULS_84MS	= 3,/* 84 ms to 170 ms */
	PULS_170MS	= 4,/* 170 ms to 340 ms */
	PULS_340MS	= 5,/* 340 ms to 670 ms */
	PULS_670MS	= 6,/* 670 ms to 1.3 s */
	PULS_1300MS	= 7,/* 1.3 s to 2.7 s */
};

#define PHY_M_LED_BLINK_RT(x)	(((u16)(x)<<8) & PHY_M_LEDC_BL_R_MSK)

enum {
	BLINK_42MS	= 0,/* 42 ms */
	BLINK_84MS	= 1,/* 84 ms */
	BLINK_170MS	= 2,/* 170 ms */
	BLINK_340MS	= 3,/* 340 ms */
	BLINK_670MS	= 4,/* 670 ms */
};

/*****  PHY_MARV_LED_OVER	16 bit r/w	Manual LED Override Reg *****/
#define PHY_M_LED_MO_SGMII(x)	((x)<<14)	/* Bit 15..14:  SGMII AN Timer */

#define PHY_M_LED_MO_DUP(x)	((x)<<10)	/* Bit 11..10:  Duplex */
#define PHY_M_LED_MO_10(x)	((x)<<8)	/* Bit  9.. 8:  Link 10 */
#define PHY_M_LED_MO_100(x)	((x)<<6)	/* Bit  7.. 6:  Link 100 */
#define PHY_M_LED_MO_1000(x)	((x)<<4)	/* Bit  5.. 4:  Link 1000 */
#define PHY_M_LED_MO_RX(x)	((x)<<2)	/* Bit  3.. 2:  Rx */
#define PHY_M_LED_MO_TX(x)	((x)<<0)	/* Bit  1.. 0:  Tx */

enum led_mode {
	MO_LED_NORM  = 0,
	MO_LED_BLINK = 1,
	MO_LED_OFF   = 2,
	MO_LED_ON    = 3,
};

/*****  PHY_MARV_EXT_CTRL_2	16 bit r/w	Ext. PHY Specific Ctrl 2 *****/
enum {
	PHY_M_EC2_FI_IMPED	= 1<<6, /* Fiber Input  Impedance */
	PHY_M_EC2_FO_IMPED	= 1<<5, /* Fiber Output Impedance */
	PHY_M_EC2_FO_M_CLK	= 1<<4, /* Fiber Mode Clock Enable */
	PHY_M_EC2_FO_BOOST	= 1<<3, /* Fiber Output Boost */
	PHY_M_EC2_FO_AM_MSK	= 7,/* Bit  2.. 0:	Fiber Output Amplitude */
};

/*****  PHY_MARV_EXT_P_STAT 16 bit r/w	Ext. PHY Specific Status *****/
enum {
	PHY_M_FC_AUTO_SEL	= 1<<15, /* Fiber/Copper Auto Sel. Dis. */
	PHY_M_FC_AN_REG_ACC	= 1<<14, /* Fiber/Copper AN Reg. Access */
	PHY_M_FC_RESOLUTION	= 1<<13, /* Fiber/Copper Resolution */
	PHY_M_SER_IF_AN_BP	= 1<<12, /* Ser. IF AN Bypass Enable */
	PHY_M_SER_IF_BP_ST	= 1<<11, /* Ser. IF AN Bypass Status */
	PHY_M_IRQ_POLARITY	= 1<<10, /* IRQ polarity */
	PHY_M_DIS_AUT_MED	= 1<<9, /* Disable Aut. Medium Reg. Selection */
	/* (88E1111 only) */

	PHY_M_UNDOC1		= 1<<7, /* undocumented bit !! */
	PHY_M_DTE_POW_STAT	= 1<<4, /* DTE Power Status (88E1111 only) */
	PHY_M_MODE_MASK	= 0xf, /* Bit  3.. 0: copy of HWCFG MODE[3:0] */
};

/* for 10/100 Fast Ethernet PHY (88E3082 only) */
/*****  PHY_MARV_FE_LED_PAR		16 bit r/w	LED Parallel Select Reg. *****/
									/* Bit 15..12: reserved (used internally) */
enum {
	PHY_M_FELP_LED2_MSK = 0xf<<8,	/* Bit 11.. 8: LED2 Mask (LINK) */
	PHY_M_FELP_LED1_MSK = 0xf<<4,	/* Bit  7.. 4: LED1 Mask (ACT) */
	PHY_M_FELP_LED0_MSK = 0xf, /* Bit  3.. 0: LED0 Mask (SPEED) */
};

#define PHY_M_FELP_LED2_CTRL(x)	(((u16)(x)<<8) & PHY_M_FELP_LED2_MSK)
#define PHY_M_FELP_LED1_CTRL(x)	(((u16)(x)<<4) & PHY_M_FELP_LED1_MSK)
#define PHY_M_FELP_LED0_CTRL(x)	(((u16)(x)<<0) & PHY_M_FELP_LED0_MSK)

enum {
	LED_PAR_CTRL_COLX	= 0x00,
	LED_PAR_CTRL_ERROR	= 0x01,
	LED_PAR_CTRL_DUPLEX	= 0x02,
	LED_PAR_CTRL_DP_COL	= 0x03,
	LED_PAR_CTRL_SPEED	= 0x04,
	LED_PAR_CTRL_LINK	= 0x05,
	LED_PAR_CTRL_TX		= 0x06,
	LED_PAR_CTRL_RX		= 0x07,
	LED_PAR_CTRL_ACT	= 0x08,
	LED_PAR_CTRL_LNK_RX	= 0x09,
	LED_PAR_CTRL_LNK_AC	= 0x0a,
	LED_PAR_CTRL_ACT_BL	= 0x0b,
	LED_PAR_CTRL_TX_BL	= 0x0c,
	LED_PAR_CTRL_RX_BL	= 0x0d,
	LED_PAR_CTRL_COL_BL	= 0x0e,
	LED_PAR_CTRL_INACT	= 0x0f
};

/*****,PHY_MARV_FE_SPEC_2		16 bit r/w	Specific Control Reg. 2 *****/
enum {
	PHY_M_FESC_DIS_WAIT	= 1<<2, /* Disable TDR Waiting Period */
	PHY_M_FESC_ENA_MCLK	= 1<<1, /* Enable MAC Rx Clock in sleep mode */
	PHY_M_FESC_SEL_CL_A	= 1<<0, /* Select Class A driver (100B-TX) */
};

/* for Yukon-2 Gigabit Ethernet PHY (88E1112 only) */
/*****  PHY_MARV_PHY_CTRL (page 1)		16 bit r/w	Fiber Specific Ctrl *****/
enum {
	PHY_M_FIB_FORCE_LNK	= 1<<10,/* Force Link Good */
	PHY_M_FIB_SIGD_POL	= 1<<9,	/* SIGDET Polarity */
	PHY_M_FIB_TX_DIS	= 1<<3,	/* Transmitter Disable */
};

/* for Yukon-2 Gigabit Ethernet PHY (88E1112 only) */
/*****  PHY_MARV_PHY_CTRL (page 2)		16 bit r/w	MAC Specific Ctrl *****/
enum {
	PHY_M_MAC_MD_MSK	= 7<<7, /* Bit  9.. 7: Mode Select Mask */
	PHY_M_MAC_GMIF_PUP	= 1<<3,	/* GMII Power Up (88E1149 only) */
	PHY_M_MAC_MD_AUTO	= 3,/* Auto Copper/1000Base-X */
	PHY_M_MAC_MD_COPPER	= 5,/* Copper only */
	PHY_M_MAC_MD_1000BX	= 7,/* 1000Base-X only */
};
#define PHY_M_MAC_MODE_SEL(x)	(((x)<<7) & PHY_M_MAC_MD_MSK)

/*****  PHY_MARV_PHY_CTRL (page 3)		16 bit r/w	LED Control Reg. *****/
enum {
	PHY_M_LEDC_LOS_MSK	= 0xf<<12,/* Bit 15..12: LOS LED Ctrl. Mask */
	PHY_M_LEDC_INIT_MSK	= 0xf<<8, /* Bit 11.. 8: INIT LED Ctrl. Mask */
	PHY_M_LEDC_STA1_MSK	= 0xf<<4,/* Bit  7.. 4: STAT1 LED Ctrl. Mask */
	PHY_M_LEDC_STA0_MSK	= 0xf, /* Bit  3.. 0: STAT0 LED Ctrl. Mask */
};

#define PHY_M_LEDC_LOS_CTRL(x)	(((x)<<12) & PHY_M_LEDC_LOS_MSK)
#define PHY_M_LEDC_INIT_CTRL(x)	(((x)<<8) & PHY_M_LEDC_INIT_MSK)
#define PHY_M_LEDC_STA1_CTRL(x)	(((x)<<4) & PHY_M_LEDC_STA1_MSK)
#define PHY_M_LEDC_STA0_CTRL(x)	(((x)<<0) & PHY_M_LEDC_STA0_MSK)

/* GMAC registers  */
/* Port Registers */
enum {
	GM_GP_STAT	= 0x0000,	/* 16 bit r/o	General Purpose Status */
	GM_GP_CTRL	= 0x0004,	/* 16 bit r/w	General Purpose Control */
	GM_TX_CTRL	= 0x0008,	/* 16 bit r/w	Transmit Control Reg. */
	GM_RX_CTRL	= 0x000c,	/* 16 bit r/w	Receive Control Reg. */
	GM_TX_FLOW_CTRL	= 0x0010,	/* 16 bit r/w	Transmit Flow-Control */
	GM_TX_PARAM	= 0x0014,	/* 16 bit r/w	Transmit Parameter Reg. */
	GM_SERIAL_MODE	= 0x0018,	/* 16 bit r/w	Serial Mode Register */
/* Source Address Registers */
	GM_SRC_ADDR_1L	= 0x001c,	/* 16 bit r/w	Source Address 1 (low) */
	GM_SRC_ADDR_1M	= 0x0020,	/* 16 bit r/w	Source Address 1 (middle) */
	GM_SRC_ADDR_1H	= 0x0024,	/* 16 bit r/w	Source Address 1 (high) */
	GM_SRC_ADDR_2L	= 0x0028,	/* 16 bit r/w	Source Address 2 (low) */
	GM_SRC_ADDR_2M	= 0x002c,	/* 16 bit r/w	Source Address 2 (middle) */
	GM_SRC_ADDR_2H	= 0x0030,	/* 16 bit r/w	Source Address 2 (high) */

/* Multicast Address Hash Registers */
	GM_MC_ADDR_H1	= 0x0034,	/* 16 bit r/w	Multicast Address Hash 1 */
	GM_MC_ADDR_H2	= 0x0038,	/* 16 bit r/w	Multicast Address Hash 2 */
	GM_MC_ADDR_H3	= 0x003c,	/* 16 bit r/w	Multicast Address Hash 3 */
	GM_MC_ADDR_H4	= 0x0040,	/* 16 bit r/w	Multicast Address Hash 4 */

/* Interrupt Source Registers */
	GM_TX_IRQ_SRC	= 0x0044,	/* 16 bit r/o	Tx Overflow IRQ Source */
	GM_RX_IRQ_SRC	= 0x0048,	/* 16 bit r/o	Rx Overflow IRQ Source */
	GM_TR_IRQ_SRC	= 0x004c,	/* 16 bit r/o	Tx/Rx Over. IRQ Source */

/* Interrupt Mask Registers */
	GM_TX_IRQ_MSK	= 0x0050,	/* 16 bit r/w	Tx Overflow IRQ Mask */
	GM_RX_IRQ_MSK	= 0x0054,	/* 16 bit r/w	Rx Overflow IRQ Mask */
	GM_TR_IRQ_MSK	= 0x0058,	/* 16 bit r/w	Tx/Rx Over. IRQ Mask */

/* Serial Management Interface (SMI) Registers */
	GM_SMI_CTRL	= 0x0080,	/* 16 bit r/w	SMI Control Register */
	GM_SMI_DATA	= 0x0084,	/* 16 bit r/w	SMI Data Register */
	GM_PHY_ADDR	= 0x0088,	/* 16 bit r/w	GPHY Address Register */
/* MIB Counters */
	GM_MIB_CNT_BASE	= 0x0100,	/* Base Address of MIB Counters */
	GM_MIB_CNT_END	= 0x025C,	/* Last MIB counter */
};


/*
 * MIB Counters base address definitions (low word) -
 * use offset 4 for access to high word	(32 bit r/o)
 */
enum {
	GM_RXF_UC_OK    = GM_MIB_CNT_BASE + 0,	/* Unicast Frames Received OK */
	GM_RXF_BC_OK	= GM_MIB_CNT_BASE + 8,	/* Broadcast Frames Received OK */
	GM_RXF_MPAUSE	= GM_MIB_CNT_BASE + 16,	/* Pause MAC Ctrl Frames Received */
	GM_RXF_MC_OK	= GM_MIB_CNT_BASE + 24,	/* Multicast Frames Received OK */
	GM_RXF_FCS_ERR	= GM_MIB_CNT_BASE + 32,	/* Rx Frame Check Seq. Error */

	GM_RXO_OK_LO	= GM_MIB_CNT_BASE + 48,	/* Octets Received OK Low */
	GM_RXO_OK_HI	= GM_MIB_CNT_BASE + 56,	/* Octets Received OK High */
	GM_RXO_ERR_LO	= GM_MIB_CNT_BASE + 64,	/* Octets Received Invalid Low */
	GM_RXO_ERR_HI	= GM_MIB_CNT_BASE + 72,	/* Octets Received Invalid High */
	GM_RXF_SHT	= GM_MIB_CNT_BASE + 80,	/* Frames <64 Byte Received OK */
	GM_RXE_FRAG	= GM_MIB_CNT_BASE + 88,	/* Frames <64 Byte Received with FCS Err */
	GM_RXF_64B	= GM_MIB_CNT_BASE + 96,	/* 64 Byte Rx Frame */
	GM_RXF_127B	= GM_MIB_CNT_BASE + 104,/* 65-127 Byte Rx Frame */
	GM_RXF_255B	= GM_MIB_CNT_BASE + 112,/* 128-255 Byte Rx Frame */
	GM_RXF_511B	= GM_MIB_CNT_BASE + 120,/* 256-511 Byte Rx Frame */
	GM_RXF_1023B	= GM_MIB_CNT_BASE + 128,/* 512-1023 Byte Rx Frame */
	GM_RXF_1518B	= GM_MIB_CNT_BASE + 136,/* 1024-1518 Byte Rx Frame */
	GM_RXF_MAX_SZ	= GM_MIB_CNT_BASE + 144,/* 1519-MaxSize Byte Rx Frame */
	GM_RXF_LNG_ERR	= GM_MIB_CNT_BASE + 152,/* Rx Frame too Long Error */
	GM_RXF_JAB_PKT	= GM_MIB_CNT_BASE + 160,/* Rx Jabber Packet Frame */

	GM_RXE_FIFO_OV	= GM_MIB_CNT_BASE + 176,/* Rx FIFO overflow Event */
	GM_TXF_UC_OK	= GM_MIB_CNT_BASE + 192,/* Unicast Frames Xmitted OK */
	GM_TXF_BC_OK	= GM_MIB_CNT_BASE + 200,/* Broadcast Frames Xmitted OK */
	GM_TXF_MPAUSE	= GM_MIB_CNT_BASE + 208,/* Pause MAC Ctrl Frames Xmitted */
	GM_TXF_MC_OK	= GM_MIB_CNT_BASE + 216,/* Multicast Frames Xmitted OK */
	GM_TXO_OK_LO	= GM_MIB_CNT_BASE + 224,/* Octets Transmitted OK Low */
	GM_TXO_OK_HI	= GM_MIB_CNT_BASE + 232,/* Octets Transmitted OK High */
	GM_TXF_64B	= GM_MIB_CNT_BASE + 240,/* 64 Byte Tx Frame */
	GM_TXF_127B	= GM_MIB_CNT_BASE + 248,/* 65-127 Byte Tx Frame */
	GM_TXF_255B	= GM_MIB_CNT_BASE + 256,/* 128-255 Byte Tx Frame */
	GM_TXF_511B	= GM_MIB_CNT_BASE + 264,/* 256-511 Byte Tx Frame */
	GM_TXF_1023B	= GM_MIB_CNT_BASE + 272,/* 512-1023 Byte Tx Frame */
	GM_TXF_1518B	= GM_MIB_CNT_BASE + 280,/* 1024-1518 Byte Tx Frame */
	GM_TXF_MAX_SZ	= GM_MIB_CNT_BASE + 288,/* 1519-MaxSize Byte Tx Frame */

	GM_TXF_COL	= GM_MIB_CNT_BASE + 304,/* Tx Collision */
	GM_TXF_LAT_COL	= GM_MIB_CNT_BASE + 312,/* Tx Late Collision */
	GM_TXF_ABO_COL	= GM_MIB_CNT_BASE + 320,/* Tx aborted due to Exces. Col. */
	GM_TXF_MUL_COL	= GM_MIB_CNT_BASE + 328,/* Tx Multiple Collision */
	GM_TXF_SNG_COL	= GM_MIB_CNT_BASE + 336,/* Tx Single Collision */
	GM_TXE_FIFO_UR	= GM_MIB_CNT_BASE + 344,/* Tx FIFO Underrun Event */
};

/* GMAC Bit Definitions */
/*	GM_GP_STAT	16 bit r/o	General Purpose Status Register */
enum {
	GM_GPSR_SPEED		= 1<<15, /* Bit 15:	Port Speed (1 = 100 Mbps) */
	GM_GPSR_DUPLEX		= 1<<14, /* Bit 14:	Duplex Mode (1 = Full) */
	GM_GPSR_FC_TX_DIS	= 1<<13, /* Bit 13:	Tx Flow-Control Mode Disabled */
	GM_GPSR_LINK_UP		= 1<<12, /* Bit 12:	Link Up Status */
	GM_GPSR_PAUSE		= 1<<11, /* Bit 11:	Pause State */
	GM_GPSR_TX_ACTIVE	= 1<<10, /* Bit 10:	Tx in Progress */
	GM_GPSR_EXC_COL		= 1<<9,	/* Bit  9:	Excessive Collisions Occurred */
	GM_GPSR_LAT_COL		= 1<<8,	/* Bit  8:	Late Collisions Occurred */

	GM_GPSR_PHY_ST_CH	= 1<<5,	/* Bit  5:	PHY Status Change */
	GM_GPSR_GIG_SPEED	= 1<<4,	/* Bit  4:	Gigabit Speed (1 = 1000 Mbps) */
	GM_GPSR_PART_MODE	= 1<<3,	/* Bit  3:	Partition mode */
	GM_GPSR_FC_RX_DIS	= 1<<2,	/* Bit  2:	Rx Flow-Control Mode Disabled */
	GM_GPSR_PROM_EN		= 1<<1,	/* Bit  1:	Promiscuous Mode Enabled */
};

/*	GM_GP_CTRL	16 bit r/w	General Purpose Control Register */
enum {
	GM_GPCR_PROM_ENA	= 1<<14,	/* Bit 14:	Enable Promiscuous Mode */
	GM_GPCR_FC_TX_DIS	= 1<<13, /* Bit 13:	Disable Tx Flow-Control Mode */
	GM_GPCR_TX_ENA		= 1<<12, /* Bit 12:	Enable Transmit */
	GM_GPCR_RX_ENA		= 1<<11, /* Bit 11:	Enable Receive */
	GM_GPCR_BURST_ENA	= 1<<10, /* Bit 10:	Enable Burst Mode */
	GM_GPCR_LOOP_ENA	= 1<<9,	/* Bit  9:	Enable MAC Loopback Mode */
	GM_GPCR_PART_ENA	= 1<<8,	/* Bit  8:	Enable Partition Mode */
	GM_GPCR_GIGS_ENA	= 1<<7,	/* Bit  7:	Gigabit Speed (1000 Mbps) */
	GM_GPCR_FL_PASS		= 1<<6,	/* Bit  6:	Force Link Pass */
	GM_GPCR_DUP_FULL	= 1<<5,	/* Bit  5:	Full Duplex Mode */
	GM_GPCR_FC_RX_DIS	= 1<<4,	/* Bit  4:	Disable Rx Flow-Control Mode */
	GM_GPCR_SPEED_100	= 1<<3,   /* Bit  3:	Port Speed 100 Mbps */
	GM_GPCR_AU_DUP_DIS	= 1<<2,	/* Bit  2:	Disable Auto-Update Duplex */
	GM_GPCR_AU_FCT_DIS	= 1<<1,	/* Bit  1:	Disable Auto-Update Flow-C. */
	GM_GPCR_AU_SPD_DIS	= 1<<0,	/* Bit  0:	Disable Auto-Update Speed */
};

#define GM_GPCR_SPEED_1000	(GM_GPCR_GIGS_ENA | GM_GPCR_SPEED_100)

/*	GM_TX_CTRL			16 bit r/w	Transmit Control Register */
enum {
	GM_TXCR_FORCE_JAM	= 1<<15, /* Bit 15:	Force Jam / Flow-Control */
	GM_TXCR_CRC_DIS		= 1<<14, /* Bit 14:	Disable insertion of CRC */
	GM_TXCR_PAD_DIS		= 1<<13, /* Bit 13:	Disable padding of packets */
	GM_TXCR_COL_THR_MSK	= 7<<10, /* Bit 12..10:	Collision Threshold */
};

#define TX_COL_THR(x)		(((x)<<10) & GM_TXCR_COL_THR_MSK)
#define TX_COL_DEF		0x04

/*	GM_RX_CTRL			16 bit r/w	Receive Control Register */
enum {
	GM_RXCR_UCF_ENA	= 1<<15, /* Bit 15:	Enable Unicast filtering */
	GM_RXCR_MCF_ENA	= 1<<14, /* Bit 14:	Enable Multicast filtering */
	GM_RXCR_CRC_DIS	= 1<<13, /* Bit 13:	Remove 4-byte CRC */
	GM_RXCR_PASS_FC	= 1<<12, /* Bit 12:	Pass FC packets to FIFO */
};

/*	GM_TX_PARAM		16 bit r/w	Transmit Parameter Register */
enum {
	GM_TXPA_JAMLEN_MSK	= 0x03<<14,	/* Bit 15..14:	Jam Length */
	GM_TXPA_JAMIPG_MSK	= 0x1f<<9,	/* Bit 13..9:	Jam IPG */
	GM_TXPA_JAMDAT_MSK	= 0x1f<<4,	/* Bit  8..4:	IPG Jam to Data */
	GM_TXPA_BO_LIM_MSK	= 0x0f,		/* Bit  3.. 0: Backoff Limit Mask */

	TX_JAM_LEN_DEF		= 0x03,
	TX_JAM_IPG_DEF		= 0x0b,
	TX_IPG_JAM_DEF		= 0x1c,
	TX_BOF_LIM_DEF		= 0x04,
};

#define TX_JAM_LEN_VAL(x)	(((x)<<14) & GM_TXPA_JAMLEN_MSK)
#define TX_JAM_IPG_VAL(x)	(((x)<<9)  & GM_TXPA_JAMIPG_MSK)
#define TX_IPG_JAM_DATA(x)	(((x)<<4)  & GM_TXPA_JAMDAT_MSK)
#define TX_BACK_OFF_LIM(x)	((x) & GM_TXPA_BO_LIM_MSK)


/*	GM_SERIAL_MODE			16 bit r/w	Serial Mode Register */
enum {
	GM_SMOD_DATABL_MSK	= 0x1f<<11, /* Bit 15..11:	Data Blinder (r/o) */
	GM_SMOD_LIMIT_4		= 1<<10, /* 4 consecutive Tx trials */
	GM_SMOD_VLAN_ENA	= 1<<9,	 /* Enable VLAN  (Max. Frame Len) */
	GM_SMOD_JUMBO_ENA	= 1<<8,	 /* Enable Jumbo (Max. Frame Len) */

	GM_NEW_FLOW_CTRL	= 1<<6,	 /* Enable New Flow-Control */

	GM_SMOD_IPG_MSK		= 0x1f	 /* Bit 4..0:	Inter-Packet Gap (IPG) */
};

#define DATA_BLIND_VAL(x)	(((x)<<11) & GM_SMOD_DATABL_MSK)
#define IPG_DATA_VAL(x)		(x & GM_SMOD_IPG_MSK)

#define DATA_BLIND_DEF		0x04
#define IPG_DATA_DEF_1000	0x1e
#define IPG_DATA_DEF_10_100	0x18

/*	GM_SMI_CTRL			16 bit r/w	SMI Control Register */
enum {
	GM_SMI_CT_PHY_A_MSK	= 0x1f<<11,/* Bit 15..11:	PHY Device Address */
	GM_SMI_CT_REG_A_MSK	= 0x1f<<6,/* Bit 10.. 6:	PHY Register Address */
	GM_SMI_CT_OP_RD		= 1<<5,	/* Bit  5:	OpCode Read (0=Write)*/
	GM_SMI_CT_RD_VAL	= 1<<4,	/* Bit  4:	Read Valid (Read completed) */
	GM_SMI_CT_BUSY		= 1<<3,	/* Bit  3:	Busy (Operation in progress) */
};

#define GM_SMI_CT_PHY_AD(x)	(((u16)(x)<<11) & GM_SMI_CT_PHY_A_MSK)
#define GM_SMI_CT_REG_AD(x)	(((u16)(x)<<6) & GM_SMI_CT_REG_A_MSK)

/*	GM_PHY_ADDR				16 bit r/w	GPHY Address Register */
enum {
	GM_PAR_MIB_CLR	= 1<<5,	/* Bit  5:	Set MIB Clear Counter Mode */
	GM_PAR_MIB_TST	= 1<<4,	/* Bit  4:	MIB Load Counter (Test Mode) */
};

/* Receive Frame Status Encoding */
enum {
	GMR_FS_LEN	= 0x7fff<<16, /* Bit 30..16:	Rx Frame Length */
	GMR_FS_VLAN	= 1<<13, /* VLAN Packet */
	GMR_FS_JABBER	= 1<<12, /* Jabber Packet */
	GMR_FS_UN_SIZE	= 1<<11, /* Undersize Packet */
	GMR_FS_MC	= 1<<10, /* Multicast Packet */
	GMR_FS_BC	= 1<<9,  /* Broadcast Packet */
	GMR_FS_RX_OK	= 1<<8,  /* Receive OK (Good Packet) */
	GMR_FS_GOOD_FC	= 1<<7,  /* Good Flow-Control Packet */
	GMR_FS_BAD_FC	= 1<<6,  /* Bad  Flow-Control Packet */
	GMR_FS_MII_ERR	= 1<<5,  /* MII Error */
	GMR_FS_LONG_ERR	= 1<<4,  /* Too Long Packet */
	GMR_FS_FRAGMENT	= 1<<3,  /* Fragment */

	GMR_FS_CRC_ERR	= 1<<1,  /* CRC Error */
	GMR_FS_RX_FF_OV	= 1<<0,  /* Rx FIFO Overflow */

	GMR_FS_ANY_ERR	= GMR_FS_RX_FF_OV | GMR_FS_CRC_ERR |
			  GMR_FS_FRAGMENT | GMR_FS_LONG_ERR |
		  	  GMR_FS_MII_ERR | GMR_FS_BAD_FC |
			  GMR_FS_UN_SIZE | GMR_FS_JABBER,
};

/*	RX_GMF_CTRL_T	32 bit	Rx GMAC FIFO Control/Test */
enum {
	RX_GCLKMAC_ENA	= 1<<31,	/* RX MAC Clock Gating Enable */
	RX_GCLKMAC_OFF	= 1<<30,

	RX_STFW_DIS	= 1<<29,	/* RX Store and Forward Enable */
	RX_STFW_ENA	= 1<<28,

	RX_TRUNC_ON	= 1<<27,  	/* enable  packet truncation */
	RX_TRUNC_OFF	= 1<<26, 	/* disable packet truncation */
	RX_VLAN_STRIP_ON = 1<<25,	/* enable  VLAN stripping */
	RX_VLAN_STRIP_OFF = 1<<24,	/* disable VLAN stripping */

	RX_MACSEC_FLUSH_ON  = 1<<23,
	RX_MACSEC_FLUSH_OFF = 1<<22,
	RX_MACSEC_ASF_FLUSH_ON = 1<<21,
	RX_MACSEC_ASF_FLUSH_OFF = 1<<20,

	GMF_RX_OVER_ON      = 1<<19,	/* enable flushing on receive overrun */
	GMF_RX_OVER_OFF     = 1<<18,	/* disable flushing on receive overrun */
	GMF_ASF_RX_OVER_ON  = 1<<17,	/* enable flushing of ASF when overrun */
	GMF_ASF_RX_OVER_OFF = 1<<16,	/* disable flushing of ASF when overrun */

	GMF_WP_TST_ON	= 1<<14,	/* Write Pointer Test On */
	GMF_WP_TST_OFF	= 1<<13,	/* Write Pointer Test Off */
	GMF_WP_STEP	= 1<<12,	/* Write Pointer Step/Increment */

	GMF_RP_TST_ON	= 1<<10,	/* Read Pointer Test On */
	GMF_RP_TST_OFF	= 1<<9,		/* Read Pointer Test Off */
	GMF_RP_STEP	= 1<<8,		/* Read Pointer Step/Increment */
	GMF_RX_F_FL_ON	= 1<<7,		/* Rx FIFO Flush Mode On */
	GMF_RX_F_FL_OFF	= 1<<6,		/* Rx FIFO Flush Mode Off */
	GMF_CLI_RX_FO	= 1<<5,		/* Clear IRQ Rx FIFO Overrun */
	GMF_CLI_RX_C	= 1<<4,		/* Clear IRQ Rx Frame Complete */

	GMF_OPER_ON	= 1<<3,		/* Operational Mode On */
	GMF_OPER_OFF	= 1<<2,		/* Operational Mode Off */
	GMF_RST_CLR	= 1<<1,		/* Clear GMAC FIFO Reset */
	GMF_RST_SET	= 1<<0,		/* Set   GMAC FIFO Reset */

	RX_GMF_FL_THR_DEF = 0xa,	/* flush threshold (default) */

	GMF_RX_CTRL_DEF	= GMF_OPER_ON | GMF_RX_F_FL_ON,
};

/*	RX_GMF_FL_CTRL	16 bit	Rx GMAC FIFO Flush Control (Yukon-Supreme) */
enum {
	RX_IPV6_SA_MOB_ENA	= 1<<9,	/* IPv6 SA Mobility Support Enable */
	RX_IPV6_SA_MOB_DIS	= 1<<8,	/* IPv6 SA Mobility Support Disable */
	RX_IPV6_DA_MOB_ENA	= 1<<7,	/* IPv6 DA Mobility Support Enable */
	RX_IPV6_DA_MOB_DIS	= 1<<6,	/* IPv6 DA Mobility Support Disable */
	RX_PTR_SYNCDLY_ENA	= 1<<5,	/* Pointers Delay Synch Enable */
	RX_PTR_SYNCDLY_DIS	= 1<<4,	/* Pointers Delay Synch Disable */
	RX_ASF_NEWFLAG_ENA	= 1<<3,	/* RX ASF Flag New Logic Enable */
	RX_ASF_NEWFLAG_DIS	= 1<<2,	/* RX ASF Flag New Logic Disable */
	RX_FLSH_MISSPKT_ENA	= 1<<1,	/* RX Flush Miss-Packet Enable */
	RX_FLSH_MISSPKT_DIS	= 1<<0,	/* RX Flush Miss-Packet Disable */
};

/*	TX_GMF_EA		32 bit	Tx GMAC FIFO End Address */
enum {
	TX_DYN_WM_ENA	= 3,	/* Yukon-FE+ specific */
};

/*	TX_GMF_CTRL_T	32 bit	Tx GMAC FIFO Control/Test */
enum {
	TX_STFW_DIS	= 1<<31,/* Disable Store & Forward */
	TX_STFW_ENA	= 1<<30,/* Enable  Store & Forward */

	TX_VLAN_TAG_ON	= 1<<25,/* enable  VLAN tagging */
	TX_VLAN_TAG_OFF	= 1<<24,/* disable VLAN tagging */

	TX_PCI_JUM_ENA  = 1<<23,/* PCI Jumbo Mode enable */
	TX_PCI_JUM_DIS  = 1<<22,/* PCI Jumbo Mode enable */

	GMF_WSP_TST_ON	= 1<<18,/* Write Shadow Pointer Test On */
	GMF_WSP_TST_OFF	= 1<<17,/* Write Shadow Pointer Test Off */
	GMF_WSP_STEP	= 1<<16,/* Write Shadow Pointer Step/Increment */

	GMF_CLI_TX_FU	= 1<<6,	/* Clear IRQ Tx FIFO Underrun */
	GMF_CLI_TX_FC	= 1<<5,	/* Clear IRQ Tx Frame Complete */
	GMF_CLI_TX_PE	= 1<<4,	/* Clear IRQ Tx Parity Error */
};

/*	GMAC_TI_ST_CTRL	 8 bit	Time Stamp Timer Ctrl Reg (YUKON only) */
enum {
	GMT_ST_START	= 1<<2,	/* Start Time Stamp Timer */
	GMT_ST_STOP	= 1<<1,	/* Stop  Time Stamp Timer */
	GMT_ST_CLR_IRQ	= 1<<0,	/* Clear Time Stamp Timer IRQ */
};

/* B28_Y2_ASF_STAT_CMD		32 bit	ASF Status and Command Reg */
enum {
	Y2_ASF_OS_PRES	= 1<<4,	/* ASF operation system present */
	Y2_ASF_RESET	= 1<<3,	/* ASF system in reset state */
	Y2_ASF_RUNNING	= 1<<2,	/* ASF system operational */
	Y2_ASF_CLR_HSTI = 1<<1,	/* Clear ASF IRQ */
	Y2_ASF_IRQ	= 1<<0,	/* Issue an IRQ to ASF system */

	Y2_ASF_UC_STATE = 3<<2,	/* ASF uC State */
	Y2_ASF_CLK_HALT	= 0,	/* ASF system clock stopped */
};

/* B28_Y2_ASF_HOST_COM	32 bit	ASF Host Communication Reg */
enum {
	Y2_ASF_CLR_ASFI = 1<<1,	/* Clear host IRQ */
	Y2_ASF_HOST_IRQ = 1<<0,	/* Issue an IRQ to HOST system */
};
/*	HCU_CCSR	CPU Control and Status Register */
enum {
	HCU_CCSR_SMBALERT_MONITOR= 1<<27, /* SMBALERT pin monitor */
	HCU_CCSR_CPU_SLEEP	= 1<<26, /* CPU sleep status */
	/* Clock Stretching Timeout */
	HCU_CCSR_CS_TO		= 1<<25,
	HCU_CCSR_WDOG		= 1<<24, /* Watchdog Reset */

	HCU_CCSR_CLR_IRQ_HOST	= 1<<17, /* Clear IRQ_HOST */
	HCU_CCSR_SET_IRQ_HCU	= 1<<16, /* Set IRQ_HCU */

	HCU_CCSR_AHB_RST	= 1<<9, /* Reset AHB bridge */
	HCU_CCSR_CPU_RST_MODE	= 1<<8, /* CPU Reset Mode */

	HCU_CCSR_SET_SYNC_CPU	= 1<<5,
	HCU_CCSR_CPU_CLK_DIVIDE_MSK = 3<<3,/* CPU Clock Divide */
	HCU_CCSR_CPU_CLK_DIVIDE_BASE= 1<<3,
	HCU_CCSR_OS_PRSNT	= 1<<2, /* ASF OS Present */
/* Microcontroller State */
	HCU_CCSR_UC_STATE_MSK	= 3,
	HCU_CCSR_UC_STATE_BASE	= 1<<0,
	HCU_CCSR_ASF_RESET	= 0,
	HCU_CCSR_ASF_HALTED	= 1<<1,
	HCU_CCSR_ASF_RUNNING	= 1<<0,
};

/*	HCU_HCSR	Host Control and Status Register */
enum {
	HCU_HCSR_SET_IRQ_CPU	= 1<<16, /* Set IRQ_CPU */

	HCU_HCSR_CLR_IRQ_HCU	= 1<<1, /* Clear IRQ_HCU */
	HCU_HCSR_SET_IRQ_HOST	= 1<<0,	/* Set IRQ_HOST */
};

/*	STAT_CTRL		32 bit	Status BMU control register (Yukon-2 only) */
enum {
	SC_STAT_CLR_IRQ	= 1<<4,	/* Status Burst IRQ clear */
	SC_STAT_OP_ON	= 1<<3,	/* Operational Mode On */
	SC_STAT_OP_OFF	= 1<<2,	/* Operational Mode Off */
	SC_STAT_RST_CLR	= 1<<1,	/* Clear Status Unit Reset (Enable) */
	SC_STAT_RST_SET	= 1<<0,	/* Set   Status Unit Reset */
};

/*	GMAC_CTRL		32 bit	GMAC Control Reg (YUKON only) */
enum {
	GMC_SET_RST	    = 1<<15,/* MAC SEC RST */
	GMC_SEC_RST_OFF     = 1<<14,/* MAC SEC RSt OFF */
	GMC_BYP_MACSECRX_ON = 1<<13,/* Bypass macsec RX */
	GMC_BYP_MACSECRX_OFF= 1<<12,/* Bypass macsec RX off */
	GMC_BYP_MACSECTX_ON = 1<<11,/* Bypass macsec TX */
	GMC_BYP_MACSECTX_OFF= 1<<10,/* Bypass macsec TX  off*/
	GMC_BYP_RETR_ON	= 1<<9, /* Bypass retransmit FIFO On */
	GMC_BYP_RETR_OFF= 1<<8, /* Bypass retransmit FIFO Off */

	GMC_H_BURST_ON	= 1<<7,	/* Half Duplex Burst Mode On */
	GMC_H_BURST_OFF	= 1<<6,	/* Half Duplex Burst Mode Off */
	GMC_F_LOOPB_ON	= 1<<5,	/* FIFO Loopback On */
	GMC_F_LOOPB_OFF	= 1<<4,	/* FIFO Loopback Off */
	GMC_PAUSE_ON	= 1<<3,	/* Pause On */
	GMC_PAUSE_OFF	= 1<<2,	/* Pause Off */
	GMC_RST_CLR	= 1<<1,	/* Clear GMAC Reset */
	GMC_RST_SET	= 1<<0,	/* Set   GMAC Reset */
};

/*	GPHY_CTRL		32 bit	GPHY Control Reg (YUKON only) */
enum {
	GPC_TX_PAUSE	= 1<<30, /* Tx pause enabled (ro) */
	GPC_RX_PAUSE	= 1<<29, /* Rx pause enabled (ro) */
	GPC_SPEED	= 3<<27, /* PHY speed (ro) */
	GPC_LINK	= 1<<26, /* Link up (ro) */
	GPC_DUPLEX	= 1<<25, /* Duplex (ro) */
	GPC_CLOCK	= 1<<24, /* 125Mhz clock stable (ro) */

	GPC_PDOWN	= 1<<23, /* Internal regulator 2.5 power down */
	GPC_TSTMODE	= 1<<22, /* Test mode */
	GPC_REG18	= 1<<21, /* Reg18 Power down */
	GPC_REG12SEL	= 3<<19, /* Reg12 power setting */
	GPC_REG18SEL	= 3<<17, /* Reg18 power setting */
	GPC_SPILOCK	= 1<<16, /* SPI lock (ASF) */

	GPC_LEDMUX	= 3<<14, /* LED Mux */
	GPC_INTPOL	= 1<<13, /* Interrupt polarity */
	GPC_DETECT	= 1<<12, /* Energy detect */
	GPC_1000HD	= 1<<11, /* Enable 1000Mbit HD */
	GPC_SLAVE	= 1<<10, /* Slave mode */
	GPC_PAUSE	= 1<<9, /* Pause enable */
	GPC_LEDCTL	= 3<<6, /* GPHY Leds */

	GPC_RST_CLR	= 1<<1,	/* Clear GPHY Reset */
	GPC_RST_SET	= 1<<0,	/* Set   GPHY Reset */
};

/*	GMAC_IRQ_SRC	 8 bit	GMAC Interrupt Source Reg (YUKON only) */
/*	GMAC_IRQ_MSK	 8 bit	GMAC Interrupt Mask   Reg (YUKON only) */
enum {
	GM_IS_TX_CO_OV	= 1<<5,	/* Transmit Counter Overflow IRQ */
	GM_IS_RX_CO_OV	= 1<<4,	/* Receive Counter Overflow IRQ */
	GM_IS_TX_FF_UR	= 1<<3,	/* Transmit FIFO Underrun */
	GM_IS_TX_COMPL	= 1<<2,	/* Frame Transmission Complete */
	GM_IS_RX_FF_OR	= 1<<1,	/* Receive FIFO Overrun */
	GM_IS_RX_COMPL	= 1<<0,	/* Frame Reception Complete */

#define GMAC_DEF_MSK     (GM_IS_TX_FF_UR | GM_IS_RX_FF_OR)
};

/*	GMAC_LINK_CTRL	16 bit	GMAC Link Control Reg (YUKON only) */
enum {						/* Bits 15.. 2:	reserved */
	GMLC_RST_CLR	= 1<<1,	/* Clear GMAC Link Reset */
	GMLC_RST_SET	= 1<<0,	/* Set   GMAC Link Reset */
};


/*	WOL_CTRL_STAT	16 bit	WOL Control/Status Reg */
enum {
	WOL_CTL_LINK_CHG_OCC		= 1<<15,
	WOL_CTL_MAGIC_PKT_OCC		= 1<<14,
	WOL_CTL_PATTERN_OCC		= 1<<13,
	WOL_CTL_CLEAR_RESULT		= 1<<12,
	WOL_CTL_ENA_PME_ON_LINK_CHG	= 1<<11,
	WOL_CTL_DIS_PME_ON_LINK_CHG	= 1<<10,
	WOL_CTL_ENA_PME_ON_MAGIC_PKT	= 1<<9,
	WOL_CTL_DIS_PME_ON_MAGIC_PKT	= 1<<8,
	WOL_CTL_ENA_PME_ON_PATTERN	= 1<<7,
	WOL_CTL_DIS_PME_ON_PATTERN	= 1<<6,
	WOL_CTL_ENA_LINK_CHG_UNIT	= 1<<5,
	WOL_CTL_DIS_LINK_CHG_UNIT	= 1<<4,
	WOL_CTL_ENA_MAGIC_PKT_UNIT	= 1<<3,
	WOL_CTL_DIS_MAGIC_PKT_UNIT	= 1<<2,
	WOL_CTL_ENA_PATTERN_UNIT	= 1<<1,
	WOL_CTL_DIS_PATTERN_UNIT	= 1<<0,
};


/* Control flags */
enum {
	UDPTCP	= 1<<0,
	CALSUM	= 1<<1,
	WR_SUM	= 1<<2,
	INIT_SUM= 1<<3,
	LOCK_SUM= 1<<4,
	INS_VLAN= 1<<5,
	EOP	= 1<<7,
};

enum {
	HW_OWNER 	= 1<<7,
	OP_TCPWRITE	= 0x11,
	OP_TCPSTART	= 0x12,
	OP_TCPINIT	= 0x14,
	OP_TCPLCK	= 0x18,
	OP_TCPCHKSUM	= OP_TCPSTART,
	OP_TCPIS	= OP_TCPINIT | OP_TCPSTART,
	OP_TCPLW	= OP_TCPLCK | OP_TCPWRITE,
	OP_TCPLSW	= OP_TCPLCK | OP_TCPSTART | OP_TCPWRITE,
	OP_TCPLISW	= OP_TCPLCK | OP_TCPINIT | OP_TCPSTART | OP_TCPWRITE,

	OP_ADDR64	= 0x21,
	OP_VLAN		= 0x22,
	OP_ADDR64VLAN	= OP_ADDR64 | OP_VLAN,
	OP_LRGLEN	= 0x24,
	OP_LRGLENVLAN	= OP_LRGLEN | OP_VLAN,
	OP_MSS		= 0x28,
	OP_MSSVLAN	= OP_MSS | OP_VLAN,

	OP_BUFFER	= 0x40,
	OP_PACKET	= 0x41,
	OP_LARGESEND	= 0x43,
	OP_LSOV2	= 0x45,

/* YUKON-2 STATUS opcodes defines */
	OP_RXSTAT	= 0x60,
	OP_RXTIMESTAMP	= 0x61,
	OP_RXVLAN	= 0x62,
	OP_RXCHKS	= 0x64,
	OP_RXCHKSVLAN	= OP_RXCHKS | OP_RXVLAN,
	OP_RXTIMEVLAN	= OP_RXTIMESTAMP | OP_RXVLAN,
	OP_RSS_HASH	= 0x65,
	OP_TXINDEXLE	= 0x68,
	OP_MACSEC	= 0x6c,
	OP_PUTIDX	= 0x70,
};

enum status_css {
	CSS_TCPUDPCSOK	= 1<<7,	/* TCP / UDP checksum is ok */
	CSS_ISUDP	= 1<<6, /* packet is a UDP packet */
	CSS_ISTCP	= 1<<5, /* packet is a TCP packet */
	CSS_ISIPFRAG	= 1<<4, /* packet is a TCP/UDP frag, CS calc not done */
	CSS_ISIPV6	= 1<<3, /* packet is a IPv6 packet */
	CSS_IPV4CSUMOK	= 1<<2, /* IP v4: TCP header checksum is ok */
	CSS_ISIPV4	= 1<<1, /* packet is a IPv4 packet */
	CSS_LINK_BIT	= 1<<0, /* port number (legacy) */
};

/* Yukon 2 hardware interface */
struct sky2_tx_le {
	__le32	addr;
	__le16	length;	/* also vlan tag or checksum start */
	u8	ctrl;
	u8	opcode;
} __packed;

struct sky2_rx_le {
	__le32	addr;
	__le16	length;
	u8	ctrl;
	u8	opcode;
} __packed;

struct sky2_status_le {
	__le32	status;	/* also checksum */
	__le16	length;	/* also vlan tag */
	u8	css;
	u8	opcode;
} __packed;

struct tx_ring_info {
	struct sk_buff	*skb;
	unsigned long flags;
#define TX_MAP_SINGLE   0x0001
#define TX_MAP_PAGE     0x0002
	DEFINE_DMA_UNMAP_ADDR(mapaddr);
	DEFINE_DMA_UNMAP_LEN(maplen);
};

struct rx_ring_info {
	struct sk_buff	*skb;
	dma_addr_t	data_addr;
	DEFINE_DMA_UNMAP_LEN(data_size);
	dma_addr_t	frag_addr[ETH_JUMBO_MTU >> PAGE_SHIFT];
};

enum flow_control {
	FC_NONE	= 0,
	FC_TX	= 1,
	FC_RX	= 2,
	FC_BOTH	= 3,
};

struct sky2_stats {
	struct u64_stats_sync syncp;
	u64		packets;
	u64		bytes;
};

struct sky2_port {
	struct sky2_hw	     *hw;
	struct net_device    *netdev;
	unsigned	     port;
	u32		     msg_enable;
	spinlock_t	     phy_lock;

	struct tx_ring_info  *tx_ring;
	struct sky2_tx_le    *tx_le;
	struct sky2_stats    tx_stats;

	u16		     tx_ring_size;
	u16		     tx_cons;		/* next le to check */
	u16		     tx_prod;		/* next le to use */
	u16		     tx_next;		/* debug only */

	u16		     tx_pending;
	u16		     tx_last_mss;
	u32		     tx_last_upper;
	u32		     tx_tcpsum;

	struct rx_ring_info  *rx_ring ____cacheline_aligned_in_smp;
	struct sky2_rx_le    *rx_le;
	struct sky2_stats    rx_stats;

	u16		     rx_next;		/* next re to check */
	u16		     rx_put;		/* next le index to use */
	u16		     rx_pending;
	u16		     rx_data_size;
	u16		     rx_nfrags;

	unsigned long	     last_rx;
	struct {
		unsigned long last;
		u32	mac_rp;
		u8	mac_lev;
		u8	fifo_rp;
		u8	fifo_lev;
	} check;

	dma_addr_t	     rx_le_map;
	dma_addr_t	     tx_le_map;

	u16		     advertising;	/* ADVERTISED_ bits */
	u16		     speed;		/* SPEED_1000, SPEED_100, ... */
	u8		     wol;		/* WAKE_ bits */
	u8		     duplex;		/* DUPLEX_HALF, DUPLEX_FULL */
	u16		     flags;
#define SKY2_FLAG_AUTO_SPEED		0x0002
#define SKY2_FLAG_AUTO_PAUSE		0x0004

 	enum flow_control    flow_mode;
 	enum flow_control    flow_status;

#ifdef CONFIG_SKY2_DEBUG
	struct dentry	     *debugfs;
#endif
};

struct sky2_hw {
	void __iomem  	     *regs;
	struct pci_dev	     *pdev;
	struct napi_struct   napi;
	struct net_device    *dev[2];
	unsigned long	     flags;
#define SKY2_HW_USE_MSI		0x00000001
#define SKY2_HW_FIBRE_PHY	0x00000002
#define SKY2_HW_GIGABIT		0x00000004
#define SKY2_HW_NEWER_PHY	0x00000008
#define SKY2_HW_RAM_BUFFER	0x00000010
#define SKY2_HW_NEW_LE		0x00000020	/* new LSOv2 format */
#define SKY2_HW_AUTO_TX_SUM	0x00000040	/* new IP decode for Tx */
#define SKY2_HW_ADV_POWER_CTL	0x00000080	/* additional PHY power regs */
#define SKY2_HW_RSS_BROKEN	0x00000100
#define SKY2_HW_VLAN_BROKEN     0x00000200
#define SKY2_HW_RSS_CHKSUM	0x00000400	/* RSS requires chksum */
#define SKY2_HW_IRQ_SETUP	0x00000800

	u8	     	     chip_id;
	u8		     chip_rev;
	u8		     pmd_type;
	u8		     ports;

	struct sky2_status_le *st_le;
	u32		     st_size;
	u32		     st_idx;
	dma_addr_t   	     st_dma;

	struct timer_list    watchdog_timer;
	struct work_struct   restart_work;
	wait_queue_head_t    msi_wait;

	char		     irq_name[0];
};

static inline int sky2_is_copper(const struct sky2_hw *hw)
{
	return !(hw->flags & SKY2_HW_FIBRE_PHY);
}

/* Register accessor for memory mapped device */
static inline u32 sky2_read32(const struct sky2_hw *hw, unsigned reg)
{
	return readl(hw->regs + reg);
}

static inline u16 sky2_read16(const struct sky2_hw *hw, unsigned reg)
{
	return readw(hw->regs + reg);
}

static inline u8 sky2_read8(const struct sky2_hw *hw, unsigned reg)
{
	return readb(hw->regs + reg);
}

static inline void sky2_write32(const struct sky2_hw *hw, unsigned reg, u32 val)
{
	writel(val, hw->regs + reg);
}

static inline void sky2_write16(const struct sky2_hw *hw, unsigned reg, u16 val)
{
	writew(val, hw->regs + reg);
}

static inline void sky2_write8(const struct sky2_hw *hw, unsigned reg, u8 val)
{
	writeb(val, hw->regs + reg);
}

/* Yukon PHY related registers */
#define SK_GMAC_REG(port,reg) \
	(BASE_GMAC_1 + (port) * (BASE_GMAC_2-BASE_GMAC_1) + (reg))
#define GM_PHY_RETRIES	100

static inline u16 gma_read16(const struct sky2_hw *hw, unsigned port, unsigned reg)
{
	return sky2_read16(hw, SK_GMAC_REG(port,reg));
}

static inline u32 gma_read32(struct sky2_hw *hw, unsigned port, unsigned reg)
{
	unsigned base = SK_GMAC_REG(port, reg);
	return (u32) sky2_read16(hw, base)
		| (u32) sky2_read16(hw, base+4) << 16;
}

static inline u64 gma_read64(struct sky2_hw *hw, unsigned port, unsigned reg)
{
	unsigned base = SK_GMAC_REG(port, reg);

	return (u64) sky2_read16(hw, base)
		| (u64) sky2_read16(hw, base+4) << 16
		| (u64) sky2_read16(hw, base+8) << 32
		| (u64) sky2_read16(hw, base+12) << 48;
}

/* There is no way to atomically read32 bit values from PHY, so retry */
static inline u32 get_stats32(struct sky2_hw *hw, unsigned port, unsigned reg)
{
	u32 val;

	do {
		val = gma_read32(hw, port, reg);
	} while (gma_read32(hw, port, reg) != val);

	return val;
}

static inline u64 get_stats64(struct sky2_hw *hw, unsigned port, unsigned reg)
{
	u64 val;

	do {
		val = gma_read64(hw, port, reg);
	} while (gma_read64(hw, port, reg) != val);

	return val;
}

static inline void gma_write16(const struct sky2_hw *hw, unsigned port, int r, u16 v)
{
	sky2_write16(hw, SK_GMAC_REG(port,r), v);
}

static inline void gma_set_addr(struct sky2_hw *hw, unsigned port, unsigned reg,
				    const u8 *addr)
{
	gma_write16(hw, port, reg,  (u16) addr[0] | ((u16) addr[1] << 8));
	gma_write16(hw, port, reg+4,(u16) addr[2] | ((u16) addr[3] << 8));
	gma_write16(hw, port, reg+8,(u16) addr[4] | ((u16) addr[5] << 8));
}

/* PCI config space access */
static inline u32 sky2_pci_read32(const struct sky2_hw *hw, unsigned reg)
{
	return sky2_read32(hw, Y2_CFG_SPC + reg);
}

static inline u16 sky2_pci_read16(const struct sky2_hw *hw, unsigned reg)
{
	return sky2_read16(hw, Y2_CFG_SPC + reg);
}

static inline void sky2_pci_write32(struct sky2_hw *hw, unsigned reg, u32 val)
{
	sky2_write32(hw, Y2_CFG_SPC + reg, val);
}

static inline void sky2_pci_write16(struct sky2_hw *hw, unsigned reg, u16 val)
{
	sky2_write16(hw, Y2_CFG_SPC + reg, val);
}
#endif
