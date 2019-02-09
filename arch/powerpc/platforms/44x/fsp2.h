#ifndef _ASM_POWERPC_FSP_DCR_H_
#define _ASM_POWERPC_FSP_DCR_H_
#ifdef __KERNEL__
#include <asm/dcr.h>

#define DCRN_CMU_ADDR		0x00C	/* Chip management unic addr */
#define DCRN_CMU_DATA		0x00D	/* Chip management unic data */

/* PLB4 Arbiter */
#define DCRN_PLB4_PCBI		0x010	/* PLB Crossbar ID/Rev Register */
#define DCRN_PLB4_P0ACR		0x011	/* PLB0 Arbiter Control Register */
#define DCRN_PLB4_P0ESRL	0x012	/* PLB0 Error Status Register Low */
#define DCRN_PLB4_P0ESRH	0x013	/* PLB0 Error Status Register High */
#define DCRN_PLB4_P0EARL	0x014	/* PLB0 Error Address Register Low */
#define DCRN_PLB4_P0EARH	0x015	/* PLB0 Error Address Register High */
#define DCRN_PLB4_P0ESRLS	0x016	/* PLB0 Error Status Register Low Set*/
#define DCRN_PLB4_P0ESRHS	0x017	/* PLB0 Error Status Register High */
#define DCRN_PLB4_PCBC		0x018	/* PLB Crossbar Control Register */
#define DCRN_PLB4_P1ACR		0x019	/* PLB1 Arbiter Control Register */
#define DCRN_PLB4_P1ESRL	0x01A	/* PLB1 Error Status Register Low */
#define DCRN_PLB4_P1ESRH	0x01B	/* PLB1 Error Status Register High */
#define DCRN_PLB4_P1EARL	0x01C	/* PLB1 Error Address Register Low */
#define DCRN_PLB4_P1EARH	0x01D	/* PLB1 Error Address Register High */
#define DCRN_PLB4_P1ESRLS	0x01E	/* PLB1 Error Status Register Low Set*/
#define DCRN_PLB4_P1ESRHS	0x01F	/*PLB1 Error Status Register High Set*/

/* PLB4/OPB bridge 0, 1, 2, 3 */
#define DCRN_PLB4OPB0_BASE	0x020
#define DCRN_PLB4OPB1_BASE	0x030
#define DCRN_PLB4OPB2_BASE	0x040
#define DCRN_PLB4OPB3_BASE	0x050

#define PLB4OPB_GESR0		0x0	/* Error status 0: Master Dev 0-3 */
#define PLB4OPB_GEAR		0x2	/* Error Address Register */
#define PLB4OPB_GEARU		0x3	/* Error Upper Address Register */
#define PLB4OPB_GESR1		0x4	/* Error Status 1: Master Dev 4-7 */
#define PLB4OPB_GESR2		0xC	/* Error Status 2: Master Dev 8-11 */

/* PLB4-to-AHB Bridge */
#define DCRN_PLB4AHB_BASE	0x400
#define DCRN_PLB4AHB_SEUAR	(DCRN_PLB4AHB_BASE + 1)
#define DCRN_PLB4AHB_SELAR	(DCRN_PLB4AHB_BASE + 2)
#define DCRN_PLB4AHB_ESR	(DCRN_PLB4AHB_BASE + 3)
#define DCRN_AHBPLB4_ESR	(DCRN_PLB4AHB_BASE + 8)
#define DCRN_AHBPLB4_EAR	(DCRN_PLB4AHB_BASE + 9)

/* PLB6 Controller */
#define DCRN_PLB6_BASE		0x11111300
#define DCRN_PLB6_CR0		(DCRN_PLB6_BASE)
#define DCRN_PLB6_ERR		(DCRN_PLB6_BASE + 0x0B)
#define DCRN_PLB6_HD		(DCRN_PLB6_BASE + 0x0E)
#define DCRN_PLB6_SHD		(DCRN_PLB6_BASE + 0x10)

/* PLB4-to-PLB6 Bridge */
#define DCRN_PLB4PLB6_BASE	0x11111320
#define DCRN_PLB4PLB6_ESR	(DCRN_PLB4PLB6_BASE + 1)
#define DCRN_PLB4PLB6_EARH	(DCRN_PLB4PLB6_BASE + 3)
#define DCRN_PLB4PLB6_EARL	(DCRN_PLB4PLB6_BASE + 4)

/* PLB6-to-PLB4 Bridge */
#define DCRN_PLB6PLB4_BASE	0x11111350
#define DCRN_PLB6PLB4_ESR	(DCRN_PLB6PLB4_BASE + 1)
#define DCRN_PLB6PLB4_EARH	(DCRN_PLB6PLB4_BASE + 3)
#define DCRN_PLB6PLB4_EARL	(DCRN_PLB6PLB4_BASE + 4)

/* PLB6-to-MCIF Bridge */
#define DCRN_PLB6MCIF_BASE	0x11111380
#define DCRN_PLB6MCIF_BESR0	(DCRN_PLB6MCIF_BASE + 0)
#define DCRN_PLB6MCIF_BESR1	(DCRN_PLB6MCIF_BASE + 1)
#define DCRN_PLB6MCIF_BEARL	(DCRN_PLB6MCIF_BASE + 2)
#define DCRN_PLB6MCIF_BEARH	(DCRN_PLB6MCIF_BASE + 3)

/* Configuration Logic Registers */
#define DCRN_CONF_BASE		0x11111400
#define DCRN_CONF_FIR_RWC	(DCRN_CONF_BASE + 0x3A)
#define DCRN_CONF_EIR_RS	(DCRN_CONF_BASE + 0x3E)
#define DCRN_CONF_RPERR0	(DCRN_CONF_BASE + 0x4D)
#define DCRN_CONF_RPERR1	(DCRN_CONF_BASE + 0x4E)

#define DCRN_L2CDCRAI		0x11111100
#define DCRN_L2CDCRDI		0x11111104
/* L2 indirect addresses */
#define L2MCK		0x120
#define L2MCKEN		0x130
#define L2INT		0x150
#define L2INTEN		0x160
#define L2LOG0		0x180
#define L2LOG1		0x184
#define L2LOG2		0x188
#define L2LOG3		0x18C
#define L2LOG4		0x190
#define L2LOG5		0x194
#define L2PLBSTAT0	0x300
#define L2PLBSTAT1	0x304
#define L2PLBMCKEN0	0x330
#define L2PLBMCKEN1	0x334
#define L2PLBINTEN0	0x360
#define L2PLBINTEN1	0x364
#define L2ARRSTAT0	0x500
#define L2ARRSTAT1	0x504
#define L2ARRSTAT2	0x508
#define L2ARRMCKEN0	0x530
#define L2ARRMCKEN1	0x534
#define L2ARRMCKEN2	0x538
#define L2ARRINTEN0	0x560
#define L2ARRINTEN1	0x564
#define L2ARRINTEN2	0x568
#define L2CPUSTAT	0x700
#define L2CPUMCKEN	0x730
#define L2CPUINTEN	0x760
#define L2RACSTAT0	0x900
#define L2RACMCKEN0	0x930
#define L2RACINTEN0	0x960
#define L2WACSTAT0	0xD00
#define L2WACSTAT1	0xD04
#define L2WACSTAT2	0xD08
#define L2WACMCKEN0	0xD30
#define L2WACMCKEN1	0xD34
#define L2WACMCKEN2	0xD38
#define L2WACINTEN0	0xD60
#define L2WACINTEN1	0xD64
#define L2WACINTEN2	0xD68
#define L2WDFSTAT	0xF00
#define L2WDFMCKEN	0xF30
#define L2WDFINTEN	0xF60

/* DDR3/4 Memory Controller */
#define DCRN_DDR34_BASE			0x11120000
#define DCRN_DDR34_MCSTAT		0x10
#define DCRN_DDR34_MCOPT1		0x20
#define DCRN_DDR34_MCOPT2		0x21
#define DCRN_DDR34_PHYSTAT		0x32
#define DCRN_DDR34_CFGR0		0x40
#define DCRN_DDR34_CFGR1		0x41
#define DCRN_DDR34_CFGR2		0x42
#define DCRN_DDR34_CFGR3		0x43
#define DCRN_DDR34_SCRUB_CNTL		0xAA
#define DCRN_DDR34_SCRUB_INT		0xAB
#define DCRN_DDR34_SCRUB_START_ADDR	0xB0
#define DCRN_DDR34_SCRUB_END_ADDR	0xD0
#define DCRN_DDR34_ECCERR_ADDR_PORT0	0xE0
#define DCRN_DDR34_ECCERR_ADDR_PORT1	0xE1
#define DCRN_DDR34_ECCERR_ADDR_PORT2	0xE2
#define DCRN_DDR34_ECCERR_ADDR_PORT3	0xE3
#define DCRN_DDR34_ECCERR_COUNT_PORT0	0xE4
#define DCRN_DDR34_ECCERR_COUNT_PORT1	0xE5
#define DCRN_DDR34_ECCERR_COUNT_PORT2	0xE6
#define DCRN_DDR34_ECCERR_COUNT_PORT3	0xE7
#define DCRN_DDR34_ECCERR_PORT0		0xF0
#define DCRN_DDR34_ECCERR_PORT1		0xF2
#define DCRN_DDR34_ECCERR_PORT2		0xF4
#define DCRN_DDR34_ECCERR_PORT3		0xF6
#define DCRN_DDR34_ECC_CHECK_PORT0	0xF8
#define DCRN_DDR34_ECC_CHECK_PORT1	0xF9
#define DCRN_DDR34_ECC_CHECK_PORT2	0xF9
#define DCRN_DDR34_ECC_CHECK_PORT3	0xFB

#define DDR34_SCRUB_CNTL_STOP		0x00000000
#define DDR34_SCRUB_CNTL_SCRUB		0x80000000
#define DDR34_SCRUB_CNTL_UE_STOP	0x20000000
#define DDR34_SCRUB_CNTL_CE_STOP	0x10000000
#define DDR34_SCRUB_CNTL_RANK_EN	0x00008000

/* PLB-Attached DDR3/4 Core Wrapper */
#define DCRN_CW_BASE			0x11111800
#define DCRN_CW_MCER0			0x00
#define DCRN_CW_MCER1			0x01
#define DCRN_CW_MCER_AND0		0x02
#define DCRN_CW_MCER_AND1		0x03
#define DCRN_CW_MCER_OR0		0x04
#define DCRN_CW_MCER_OR1		0x05
#define DCRN_CW_MCER_MASK0		0x06
#define DCRN_CW_MCER_MASK1		0x07
#define DCRN_CW_MCER_MASK_AND0		0x08
#define DCRN_CW_MCER_MASK_AND1		0x09
#define DCRN_CW_MCER_MASK_OR0		0x0A
#define DCRN_CW_MCER_MASK_OR1		0x0B
#define DCRN_CW_MCER_ACTION0		0x0C
#define DCRN_CW_MCER_ACTION1		0x0D
#define DCRN_CW_MCER_WOF0		0x0E
#define DCRN_CW_MCER_WOF1		0x0F
#define DCRN_CW_LFIR			0x10
#define DCRN_CW_LFIR_AND		0x11
#define DCRN_CW_LFIR_OR			0x12
#define DCRN_CW_LFIR_MASK		0x13
#define DCRN_CW_LFIR_MASK_AND		0x14
#define DCRN_CW_LFIR_MASK_OR		0x15

#define CW_MCER0_MEM_CE			0x00020000
/* CMU addresses */
#define CMUN_CRCS		0x00 /* Chip Reset Control/Status */
#define CMUN_CONFFIR0		0x20 /* Config Reg Parity FIR 0 */
#define CMUN_CONFFIR1		0x21 /* Config Reg Parity FIR 1 */
#define CMUN_CONFFIR2		0x22 /* Config Reg Parity FIR 2 */
#define CMUN_CONFFIR3		0x23 /* Config Reg Parity FIR 3 */
#define CMUN_URCR3_RS		0x24 /* Unit Reset Control Reg 3 Set */
#define CMUN_URCR3_C		0x25 /* Unit Reset Control Reg 3 Clear */
#define CMUN_URCR3_P		0x26 /* Unit Reset Control Reg 3 Pulse */
#define CMUN_PW0		0x2C /* Pulse Width Register */
#define CMUN_URCR0_P		0x2D /* Unit Reset Control Reg 0 Pulse */
#define CMUN_URCR1_P		0x2E /* Unit Reset Control Reg 1 Pulse */
#define CMUN_URCR2_P		0x2F /* Unit Reset Control Reg 2 Pulse */
#define CMUN_CLS_RW		0x30 /* Code Load Status (Read/Write) */
#define CMUN_CLS_S		0x31 /* Code Load Status (Set) */
#define CMUN_CLS_C		0x32 /* Code Load Status (Clear */
#define CMUN_URCR2_RS		0x33 /* Unit Reset Control Reg 2 Set */
#define CMUN_URCR2_C		0x34 /* Unit Reset Control Reg 2 Clear */
#define CMUN_CLKEN0		0x35 /* Clock Enable 0 */
#define CMUN_CLKEN1		0x36 /* Clock Enable 1 */
#define CMUN_PCD0		0x37 /* PSI clock divider 0 */
#define CMUN_PCD1		0x38 /* PSI clock divider 1 */
#define CMUN_TMR0		0x39 /* Reset Timer */
#define CMUN_TVS0		0x3A /* TV Sense Reg 0 */
#define CMUN_TVS1		0x3B /* TV Sense Reg 1 */
#define CMUN_MCCR		0x3C /* DRAM Configuration Reg */
#define CMUN_FIR0		0x3D /* Fault Isolation Reg 0 */
#define CMUN_FMR0		0x3E /* FIR Mask Reg 0 */
#define CMUN_ETDRB		0x3F /* ETDR Backdoor */

/* CRCS bit fields */
#define CRCS_STAT_MASK		0xF0000000
#define CRCS_STAT_POR		0x10000000
#define CRCS_STAT_PHR		0x20000000
#define CRCS_STAT_PCIE		0x30000000
#define CRCS_STAT_CRCS_SYS	0x40000000
#define CRCS_STAT_DBCR_SYS	0x50000000
#define CRCS_STAT_HOST_SYS	0x60000000
#define CRCS_STAT_CHIP_RST_B	0x70000000
#define CRCS_STAT_CRCS_CHIP	0x80000000
#define CRCS_STAT_DBCR_CHIP	0x90000000
#define CRCS_STAT_HOST_CHIP	0xA0000000
#define CRCS_STAT_PSI_CHIP	0xB0000000
#define CRCS_STAT_CRCS_CORE	0xC0000000
#define CRCS_STAT_DBCR_CORE	0xD0000000
#define CRCS_STAT_HOST_CORE	0xE0000000
#define CRCS_STAT_PCIE_HOT	0xF0000000
#define CRCS_STAT_SELF_CORE	0x40000000
#define CRCS_STAT_SELF_CHIP	0x50000000
#define CRCS_WATCHE		0x08000000
#define CRCS_CORE		0x04000000 /* Reset PPC440 core */
#define CRCS_CHIP		0x02000000 /* Chip Reset */
#define CRCS_SYS		0x01000000 /* System Reset */
#define CRCS_WRCR		0x00800000 /* Watchdog reset on core reset */
#define CRCS_EXTCR		0x00080000 /* CHIP_RST_B triggers chip reset */
#define CRCS_PLOCK		0x00000002 /* PLL Locked */

#define mtcmu(reg, data)		\
do {					\
	mtdcr(DCRN_CMU_ADDR, reg);	\
	mtdcr(DCRN_CMU_DATA, data);	\
} while (0)

#define mfcmu(reg)\
	({u32 data;			\
	mtdcr(DCRN_CMU_ADDR, reg);	\
	data = mfdcr(DCRN_CMU_DATA);	\
	data; })

#define mtl2(reg, data)			\
do {					\
	mtdcr(DCRN_L2CDCRAI, reg);	\
	mtdcr(DCRN_L2CDCRDI, data);	\
} while (0)

#define mfl2(reg)			\
	({u32 data;			\
	mtdcr(DCRN_L2CDCRAI, reg);	\
	data = mfdcr(DCRN_L2CDCRDI);	\
	data; })

#endif /* __KERNEL__ */
#endif /* _ASM_POWERPC_FSP2_DCR_H_ */
