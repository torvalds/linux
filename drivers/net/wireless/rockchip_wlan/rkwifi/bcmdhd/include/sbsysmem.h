/*
 * SiliconBackplane System Memory core
 *
 * Copyright (C) 2020, Broadcom.
 *
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 *
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 *
 *
 * <<Broadcom-WL-IPTag/Dual:>>
 */

#ifndef	_SBSYSMEM_H
#define	_SBSYSMEM_H

#ifndef _LANGUAGE_ASSEMBLY

/* cpp contortions to concatenate w/arg prescan */
#ifndef PAD
#define	_PADLINE(line)	pad ## line
#define	_XSTR(line)	_PADLINE(line)
#define	PAD		_XSTR(__LINE__)
#endif	/* PAD */

/* sysmem core registers */
typedef volatile struct sysmemregs {
	uint32	coreinfo;
	uint32	bwalloc;
	uint32	extracoreinfo;
	uint32	biststat;
	uint32	bankidx;
	uint32	standbyctrl;

	uint32	errlogstatus;
	uint32	errlogaddr;

	uint32	cambankidx;
	uint32	cambankstandbyctrl;
	uint32	cambankpatchctrl;
	uint32	cambankpatchtblbaseaddr;
	uint32	cambankcmdreg;
	uint32	cambankdatareg;
	uint32	standbywait;
	uint32	PAD[1];
	uint32	bankinfo;
	uint32  PAD[7];
	uint32  region_n_regs[32];
	uint32  initiat_n_masks[31];
	uint32  PAD[1];
	uint32  mpucontrol;
	uint32  mpucapabilities;
	uint32  PAD[31];
	uint32	workaround;
	uint32	pwrctl;
	uint32	PAD[133];
	uint32  sr_control;
	uint32  sr_status;
	uint32  sr_address;
	uint32  sr_data;
} sysmemregs_t;

/* bus MPU region count mask of sysmemregs_t->mpucapabilities */
#define ACC_MPU_REGION_CNT_MASK	0x7u
/* bus MPU disable mask of sysmemregs_t->mpucontrol */
#define BUSMPU_DISABLE_MASK	0xfu

#endif	/* _LANGUAGE_ASSEMBLY */

/* Register offsets */
#define	SR_COREINFO		0x00
#define	SR_BWALLOC		0x04
#define	SR_BISTSTAT		0x0c
#define	SR_BANKINDEX		0x10
#define	SR_BANKSTBYCTL		0x14
#define SR_PWRCTL		0x1e8

/* Coreinfo register */
#define	SRCI_PT_MASK		0x00070000	/* port type[18:16] */
#define	SRCI_PT_SHIFT		16
/* port types : SRCI_PT_<processorPT>_<backplanePT> */
#define SRCI_PT_OCP_OCP		0
#define SRCI_PT_AXI_OCP		1
#define SRCI_PT_ARM7AHB_OCP	2
#define SRCI_PT_CM3AHB_OCP	3
#define SRCI_PT_AXI_AXI		4
#define SRCI_PT_AHB_AXI		5

#define SRCI_LSS_MASK		0x00f00000
#define SRCI_LSS_SHIFT		20
#define SRCI_LRS_MASK		0x0f000000
#define SRCI_LRS_SHIFT		24

/* In corerev 0, the memory size is 2 to the power of the
 * base plus 16 plus to the contents of the memsize field plus 1.
 */
#define	SRCI_MS0_MASK		0xf
#define SR_MS0_BASE		16

/*
 * In corerev 1 the bank size is 2 ^ the bank size field plus 14,
 * the memory size is number of banks times bank size.
 * The same applies to rom size.
 */
#define	SYSMEM_SRCI_ROMNB_MASK	0x3e0
#define	SYSMEM_SRCI_ROMNB_SHIFT	5
#define	SYSMEM_SRCI_SRNB_MASK	0x1f
#define	SYSMEM_SRCI_SRNB_SHIFT	0
/* Above bits are obsolete and replaced with below in rev 12 */
#define	SYSMEM_SRCI_NEW_ROMNB_MASK	0xff000000u
#define	SYSMEM_SRCI_NEW_ROMNB_SHIFT	24u
#define	SYSMEM_SRCI_NEW_SRNB_MASK	0xff0000u
#define	SYSMEM_SRCI_NEW_SRNB_SHIFT	16u

/* Standby control register */
#define	SRSC_SBYOVR_MASK	0x80000000
#define	SRSC_SBYOVR_SHIFT	31
#define	SRSC_SBYOVRVAL_MASK	0x60000000
#define	SRSC_SBYOVRVAL_SHIFT	29
#define	SRSC_SBYEN_MASK		0x01000000
#define	SRSC_SBYEN_SHIFT	24

/* Power control register */
#define SRPC_PMU_STBYDIS_MASK	0x00000010
#define SRPC_PMU_STBYDIS_SHIFT	4
#define SRPC_STBYOVRVAL_MASK	0x00000008
#define SRPC_STBYOVRVAL_SHIFT	3
#define SRPC_STBYOVR_MASK	0x00000007
#define SRPC_STBYOVR_SHIFT	0

/* Extra core capability register */
#define SRECC_NUM_BANKS_MASK   0x000000F0
#define SRECC_NUM_BANKS_SHIFT  4
#define SRECC_BANKSIZE_MASK    0x0000000F
#define SRECC_BANKSIZE_SHIFT   0

#define SRECC_BANKSIZE(value)	 (1 << (value))

/* CAM bank patch control */
#define SRCBPC_PATCHENABLE 0x80000000

#define SRP_ADDRESS   0x0001FFFC
#define SRP_VALID     0x8000

/* CAM bank command reg */
#define SRCMD_WRITE  0x00020000
#define SRCMD_READ   0x00010000
#define SRCMD_DONE   0x80000000

#define SRCMD_DONE_DLY	1000

/* bankidx and bankinfo reg defines */
#define SYSMEM_BANKINFO_SZMASK		0x7f
#define SYSMEM_BANKIDX_ROM_MASK		0x80

#define	SYSMEM_BANKINFO_REG		0x40
#define	SYSMEM_BANKIDX_REG		0x10
#define	SYSMEM_BANKINFO_STDBY_MASK	0x200
#define	SYSMEM_BANKINFO_STDBY_TIMER	0x400

#define SYSMEM_BANKINFO_SLPSUPP_SHIFT		14
#define SYSMEM_BANKINFO_SLPSUPP_MASK		0x4000
#define SYSMEM_BANKINFO_PDASZ_SHIFT		16
#define SYSMEM_BANKINFO_PDASZ_MASK		0x001F0000

/* extracoreinfo register */
#define SYSMEM_DEVRAMBANK_MASK		0xF000
#define SYSMEM_DEVRAMBANK_SHIFT		12

/* bank info to calculate bank size */
#define	SYSMEM_BANKINFO_SZBASE          8192
#define SYSMEM_BANKSIZE_SHIFT		13      /* SYSMEM_BANKINFO_SZBASE */

/* standbycontrol register default values */
#define SYSMEM_SBYCNTRL_TIMEVAL		0x100000u	/* standbycontrol timeval[23:0] */
#define SYSMEM_SBYCNTRL_TIMEVAL_MASK	0xffffffu

/* sbywaitcycle register default values (sysme rev 8) */
#define SYSMEM_SBYWAIT_RAM_TIMEVAL	0xau	/* RAM memory access after standby exit */

#endif	/* _SBSYSMEM_H */
