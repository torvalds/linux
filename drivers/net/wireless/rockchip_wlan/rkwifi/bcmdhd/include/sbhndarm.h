/*
 * Broadcom SiliconBackplane ARM definitions
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

#ifndef	_sbhndarm_h_
#define	_sbhndarm_h_

#ifdef DONGLEBUILD

#include <arminc.h>
#include <sbconfig.h>

/* register offsets */
#define	ARM7_CORECTL		0

/* bits in corecontrol */
#define	ACC_FORCED_RST		0x1
#define	ACC_SERRINT		0x2
#define	ACC_WFICLKSTOP		0x4

#if !defined(__ARM_ARCH_7A__)
#define ACC_NOTSLEEPINGCLKREQ_SHIFT	24
#endif /* !__ARM_ARCH_7A__ */

#if defined(__ARM_ARCH_7A__)

#define ACC_FORCECLOCKRATIO		(0x1u << 8)
#define ACC_CLOCKRATIO_SHIFT		(9u)
#define ACC_CLOCKRATIO_MASK		(0xFu << ACC_CLOCKRATIO_SHIFT)

#define ACC_CLOCKRATIO_1_TO_1		(0u)
#define ACC_CLOCKRATIO_2_TO_1		(1u)
#define ACC_CLOCKRATIO_3_TO_1		(2u)
#define ACC_CLOCKRATIO_4_TO_1		(3u)

#define ACC_FASTCLOCKCHANNEL_SHIFT	(24u)
#define ACC_FASTCLOCKCHANNEL_MASK	(0x3u << ACC_FASTCLOCKCHANNEL_SHIFT)
#define ACC_NUM_FASTCLOCKS_SHIFT	(2u)
#define ACC_NUM_FASTCLOCKS_MASK		(0x3u << ACC_NUM_FASTCLOCKS_SHIFT)

#define ACC_NOTSLEEPINGCLKREQ_SHIFT	(4u)
#define ACC_NOTSLEEPINGCLKREQ_MASK	(0x3u << ACC_NOT_SLEEPING_CLKREQ_SHIFT)
#define ACC_NOTSLEEPING_ALP		(0u)
#define ACC_NOTSLEEPING_HT		(1u)
#define ACC_NOTSLEEPING_ALP_HT_AVAIL	(2u)
#define ACC_NOTSLEEPING_HT_AVAIL	(3u)

#elif defined(__ARM_ARCH_7R__) /* CR4 */

#define ACC_FORCECLOCKRATIO	(1u << 7u)
#define ACC_CLOCKRATIO_SHIFT	8u
#define ACC_CLOCKRATIO_MASK	(0xFu << ACC_CLOCKRATIO_SHIFT)
#define ACC_CLOCKMODE_SHIFT	12u
#define ACC_CLOCKMODE_MASK	(7u << ACC_CLOCKMODE_SHIFT)
#define ACC_NOTSLEEPCLKREQ0	3u
#define ACC_NOTSLEEPCLKREQ1	18u
#define ACC_FLOPSPROTECT	(1u << 20u)

#define ACC_CLOCKRATIO_1_TO_1	(0u)
#define ACC_CLOCKRATIO_2_TO_1	(4u)

#endif /* __ARM_ARCH_7A__ */

#define ACC_CLOCKMODE_SAME	(0)	/**< BP and CPU clock are the same */
#define ACC_CLOCKMODE_ASYNC	(1)	/**< BP and CPU clock are asynchronous */
#define ACC_CLOCKMODE_SYNCH	(2)	/**< BP and CPU clock are synch, ratio 1:1 or 1:2 */

/* Request ALP on backplane bit 3 and 18 */
#define ACC_REQALP			((1<<ACC_NOTSLEEPCLKREQ0) | (1<<ACC_NOTSLEEPCLKREQ1))

#define ACC_MPU_SHIFT		25u
#define ACC_MPU_MASK		(0x1u << ACC_MPU_SHIFT)

#define ACC_MPU_REGION_CNT_MASK		0x7u
#define ACC_MPU_REGION_CNT_SHIFT	3u

#define ACC_MPU_SECURE_SHIFT	27u
#define ACC_MPU_SECURE_MASK	(0x1u << ACC_MPU_SECURE_SHIFT)
#define ACC_MPU_READ_SHIFT	30u
#define ACC_MPU_READ_MASK	(0x1u << ACC_MPU_READ_SHIFT)
#define ACC_MPU_WRITE_SHIFT	29u
#define ACC_MPU_WRITE_MASK	(0x1u << ACC_MPU_WRITE_SHIFT)
#define ACC_MPU_VALID_SHIFT	31u
#define ACC_MPU_VALID_MASK	(0x1u << ACC_MPU_VALID_SHIFT)

/* arm resetlog */
#define SBRESETLOG		0x1
#define SERRORLOG		0x2

/* arm core-specific control flags */
#define	SICF_REMAP_MSK		0x001c
#define	SICF_REMAP_NONE		0
#define	SICF_REMAP_ROM		0x0004
#define	SIFC_REMAP_FLASH	0x0008

/* misc core-specific defines */
#if defined(__ARM_ARCH_7M__)
/* cortex-m3 */
/* backplane related stuff */
#define ARM_CORE_ID		ARMCM3_CORE_ID	/**< arm coreid */
#define SI_ARM_ROM		SI_ARMCM3_ROM	/**< ROM backplane/system address */
#define SI_ARM_SRAM2		SI_ARMCM3_SRAM2	/**< RAM backplane address when remap is 1 or 2 */
/* core registers offsets */
#define ARMCM3_CYCLECNT		0x90		/**< Cortex-M3 core registers offsets */
#define ARMCM3_INTTIMER		0x94
#define ARMCM3_INTMASK		0x98
#define ARMCM3_INTSTATUS	0x9c
/* interrupt/exception */
#define ARMCM3_NUMINTS		16		/**< # of external interrupts */
#define ARMCM3_INTALL		((1 << ARMCM3_NUMINTS) - 1)	/**< Interrupt mask */
#define ARMCM3_SHARED_INT	0		/**< Interrupt shared by multiple cores */
#define ARMCM3_INT(i)		(1 << (i))	/**< Individual interrupt enable/disable */
/* intmask/intstatus bits */
#define ARMCM3_INTMASK_TIMER	0x1
#define ARMCM3_INTMASK_SYSRESET	0x4
#define ARMCM3_INTMASK_LOCKUP	0x8

/*
 * Overlay Support in Rev 5
 */
#define ARMCM3_OVL_VALID_SHIFT		0
#define ARMCM3_OVL_VALID		1
#define ARMCM3_OVL_SZ_SHIFT		1
#define ARMCM3_OVL_SZ_MASK		0x0000000e
#define ARMCM3_OVL_SZ_512B		0	/* 512B */
#define ARMCM3_OVL_SZ_1KB		1	/* 1KB */
#define ARMCM3_OVL_SZ_2KB		2	/* 2KB */
#define ARMCM3_OVL_SZ_4KB		3	/* 4KB */
#define ARMCM3_OVL_SZ_8KB		4	/* 8KB */
#define ARMCM3_OVL_SZ_16KB		5	/* 16KB */
#define ARMCM3_OVL_SZ_32KB		6	/* 32KB */
#define ARMCM3_OVL_SZ_64KB		7	/* 64KB */
#define ARMCM3_OVL_ADDR_SHIFT		9
#define ARMCM3_OVL_ADDR_MASK		0x003FFE00
#define ARMCM3_OVL_MAX			16

#elif defined(__ARM_ARCH_7R__)
/* cortex-r4 */
/* backplane related stuff */
#define ARM_CORE_ID		ARMCR4_CORE_ID	/**< arm coreid */
#define SI_ARM_ROM		SI_ARMCR4_ROM	/**< ROM backplane/system address */
#define SI_ARM_SRAM2		0x0	/**< In the cr4 the RAM is just not available
					 * when remap is 1
					 */

/* core registers offsets */
#define ARMCR4_CORECTL		0
#define ARMCR4_CORECAP		4
#define ARMCR4_COREST		8

#define ARMCR4_FIQRSTATUS	0x10
#define ARMCR4_FIQMASK		0x14
#define ARMCR4_IRQMASK		0x18

#define ARMCR4_INTSTATUS	0x20
#define ARMCR4_INTMASK		0x24
#define ARMCR4_CYCLECNT		0x28
#define ARMCR4_INTTIMER		0x2c

#define ARMCR4_GPIOSEL		0x30
#define ARMCR4_GPIOEN		0x34

#define ARMCR4_BANKIDX		0x40
#define ARMCR4_BANKINFO		0x44
#define ARMCR4_BANKSTBY		0x48
#define ARMCR4_BANKPDA		0x4c

#define ARMCR4_TCAMPATCHCTRL		0x68
#define ARMCR4_TCAMPATCHTBLBASEADDR	0x6C
#define ARMCR4_TCAMCMDREG		0x70
#define ARMCR4_TCAMDATAREG		0x74
#define ARMCR4_TCAMBANKXMASKREG		0x78

#define	ARMCR4_ROMNB_MASK	0xf00
#define	ARMCR4_ROMNB_SHIFT	8
#define	ARMCR4_MSB_ROMNB_MASK	0x1E00000
/* adjusted shift to fit 4-LSB (21 - 4 = 17) */
#define	ARMCR4_MSB_ROMNB_SHIFT	17
#define	ARMCR4_TCBBNB_MASK	0xf0
#define	ARMCR4_TCBBNB_SHIFT	4
#define	ARMCR4_TCBANB_MASK	0xf
#define	ARMCR4_TCBANB_SHIFT	0

#define	ARMCR4_MT_MASK		0x300
#define	ARMCR4_MT_SHIFT		8
#define	ARMCR4_MT_ROM		0x100
#define	ARMCR4_MT_RAM		0

#define	ARMCR4_BSZ_MASK		0x7f
#define	ARMCR4_BUNITSZ_MASK	0x200
#define	ARMCR4_BSZ_8K		8192
#define	ARMCR4_BSZ_1K		1024

#define	ARMCR4_STBY_SUPPORTED		0x400
#define	ARMCR4_STBY_TIMER_PRESENT	0x800

#define ARMCR4_TIMER_VAL_MASK		0xfffff
#define ARMCR4_STBY_TIMER_ENABLE	(1 << 24)
#define ARMCR4_STBY_OVERRIDE		(1 << 31)

#define ARMCR4_TCAM_ENABLE		(1u << 31u)
#define ARMCR4_TCAM_CLKENAB		(1u << 30u)
#define ARMCR4_TCAM_WRITEPROT		(1u << 29u)
#define ARMCR4_TCAM_PATCHCNT_MASK	0xfu

#define ARMCR4_TCAM_CMD_DONE	(1u << 31)
#define ARMCR4_TCAM_MATCH	(1u << 24)
#define ARMCR4_TCAM_OPCODE_MASK	(3 << 16)
#define ARMCR4_TCAM_OPCODE_SHIFT 16
#define ARMCR4_TCAM_ADDR_MASK	0xffff
#define ARMCR4_TCAM_NONE	(0 << ARMCR4_TCAM_OPCODE_SHIFT)
#define ARMCR4_TCAM_READ	(1 << ARMCR4_TCAM_OPCODE_SHIFT)
#define ARMCR4_TCAM_WRITE	(2 << ARMCR4_TCAM_OPCODE_SHIFT)
#define ARMCR4_TCAM_COMPARE	(3 << ARMCR4_TCAM_OPCODE_SHIFT)
#define ARMCR4_TCAM_CMD_DONE_DLY	1000

#define ARMCR4_DATA_MASK	(~0x7)
#define ARMCR4_DATA_VALID	(1u << 0)

/* intmask/intstatus bits */
#define ARMCR4_INTMASK_TIMER		(0x1)
#define ARMCR4_INTMASK_CLOCKSTABLE	(0x20000000)

#define CHIP_SDRENABLE(sih)	(sih->boardflags2 & BFL2_SDR_EN)
#define CHIP_TCMPROTENAB(sih)	(si_arm_sflags(sih) & SISF_TCMPROT)

/* Power Control */
#define ARM_ENAB_MEM_CLK_GATE_SHIFT	5

#define ROM_STBY_TIMER_4378	0xb0
#define RAM_STBY_TIMER_4378	0x64

#define ROM_STBY_TIMER_4387	0x10
#define RAM_STBY_TIMER_4387	0x100

#define RAM_STBY_DEFAULT_WAIT_TIME	(3u)
#define ROM_STBY_DEFAULT_WAIT_TIME	(4u)
#define DEFAULT_FORCE_STBY_IN_WFI	(1u)
#define	ARMCR4_DYN_STBY_CTRL_RAM_STBY_WAIT_TIMER_SHIFT	(0u)
#define ARMCR4_DYN_STBY_CTRL_RAM_STBY_WAIT_TIMER_MASK	(0xF << \
		ARMCR4_DYN_STBY_CTRL_RAM_STBY_WAIT_TIMER_SHIFT)
#define	ARMCR4_DYN_STBY_CTRL_ROM_STBY_WAIT_TIMER_SHIFT	(8u)
#define	ARMCR4_DYN_STBY_CTRL_ROM_STBY_WAIT_TIMER_MASK	(0x3F << \
		ARMCR4_DYN_STBY_CTRL_ROM_STBY_WAIT_TIMER_SHIFT)
#define	ARMCR4_DYN_STBY_CTRL_FORCE_STBY_IN_WFI_SHIFT	(16u)
#define	ARMCR4_DYN_STBY_CTRL_FORCE_STBY_IN_WFI_MASK	(0x1 << \
		ARMCR4_DYN_STBY_CTRL_FORCE_STBY_IN_WFI_SHIFT)

/* using CHIPID because no capabilities bit */
#define ARM_CLKGATING_CAP(sih)		((void)(sih), (BCM4378_CHIP(sih->chip) ||\
					 BCM4387_CHIP(sih->chip)))

#define ARM_CLKGATING_ENAB(sih)	(ARM_CLKGATING_CAP(sih) && 1)

#elif defined(__ARM_ARCH_7A__)

#if defined(CA7)
/* backplane related stuff */
#define ARM_CORE_ID		ARMCA7_CORE_ID
#define SI_ARM_ROM		SI_ARMCA7_ROM	/**< ROM backplane/system address */

#else
/* backplane related stuff */
#define ARM_CORE_ID		ARMCA9_CORE_ID	/* arm coreid */
#endif /* __ARM_ARCH_7A__ */
#else	/* !__ARM_ARCH_7M__ && !__ARM_ARCH_7R__ */
#error Unrecognized ARM Architecture
#endif	/* !__ARM_ARCH_7M__ && !__ARM_ARCH_7R__ */

#endif /* DONGLEBUILD */

#ifndef _LANGUAGE_ASSEMBLY

/* cpp contortions to concatenate w/arg prescan */
#ifndef PAD
#define	_PADLINE(line)	pad ## line
#define	_XSTR(line)	_PADLINE(line)
#define	PAD		_XSTR(__LINE__)
#endif	/* PAD */

/* cortex-m3 */
typedef volatile struct {
	uint32	corecontrol;	/* 0x0 */
	uint32	corestatus;	/* 0x4 */
	uint32	PAD[1];
	uint32	biststatus;	/* 0xc */
	uint32	nmiisrst;	/* 0x10 */
	uint32	nmimask;	/* 0x14 */
	uint32	isrmask;	/* 0x18 */
	uint32	PAD[1];
	uint32	resetlog;	/* 0x20 */
	uint32	gpioselect;	/* 0x24 */
	uint32	gpioenable;	/* 0x28 */
	uint32	PAD[1];
	uint32	bpaddrlo;	/* 0x30 */
	uint32	bpaddrhi;	/* 0x34 */
	uint32	bpdata;		/* 0x38 */
	uint32	bpindaccess;	/* 0x3c */
	uint32	ovlidx;		/* 0x40 */
	uint32	ovlmatch;	/* 0x44 */
	uint32	ovladdr;	/* 0x48 */
	uint32	PAD[13];
	uint32	bwalloc;	/* 0x80 */
	uint32	PAD[3];
	uint32	cyclecnt;	/* 0x90 */
	uint32	inttimer;	/* 0x94 */
	uint32	intmask;	/* 0x98 */
	uint32	intstatus;	/* 0x9c */
	uint32	PAD[80];
	uint32	clk_ctl_st;	/* 0x1e0 */
	uint32  PAD[1];
	uint32  powerctl;	/* 0x1e8 */
} cm3regs_t;
#define ARM_CM3_REG(regs, reg)	(&((cm3regs_t *)regs)->reg)

/* cortex-R4 */
typedef volatile struct {
	uint32	corecontrol;		/* 0x0 */
	uint32	corecapabilities;	/* 0x4 */
	uint32	corestatus;		/* 0x8 */
	uint32	biststatus;		/* 0xc */
	uint32	nmiisrst;		/* 0x10 */
	uint32	nmimask;		/* 0x14 */
	uint32	isrmask;		/* 0x18 */
	uint32	swintreg;		/* 0x1C */
	uint32	intstatus;		/* 0x20 */
	uint32	intmask;		/* 0x24 */
	uint32	cyclecnt;		/* 0x28 */
	uint32	inttimer;		/* 0x2c */
	uint32	gpioselect;		/* 0x30 */
	uint32	gpioenable;		/* 0x34 */
	uint32	PAD[2];
	uint32	bankidx;		/* 0x40 */
	uint32	bankinfo;		/* 0x44 */
	uint32	bankstbyctl;		/* 0x48 */
	uint32	bankpda;		/* 0x4c */
	uint32	dyn_stby_control;	/* 0x50 */
	uint32	PAD[5];
	uint32	tcampatchctrl;		/* 0x68 */
	uint32	tcampatchtblbaseaddr;	/* 0x6c */
	uint32	tcamcmdreg;		/* 0x70 */
	uint32	tcamdatareg;		/* 0x74 */
	uint32	tcambankxmaskreg;	/* 0x78 */
	uint32	PAD[5];
	uint32  mpucontrol;		/* 0x90 */
	uint32  mpucapabilities;	/* 0x94 */
	uint32	rom_reloc_addr;		/* 0x98 */
	uint32	PAD[1];
	uint32  region_n_regs[16];	/* 0xa0 - 0xdc */
	uint32  PAD[16];
	uint32  initiat_n_masks[16];	/* 0x120 - 0x15c */
	uint32  PAD[32];
	uint32	clk_ctl_st;		/* 0x1e0 */
	uint32	hw_war;			/* 0x1e4 */
	uint32	powerctl;		/* 0x1e8 */
	uint32  powerctl2;		/* 0x1ec */
} cr4regs_t;
#define ARM_CR4_REG(regs, reg)	(&((cr4regs_t *)regs)->reg)

#define SBRESETLOG_CR4		0x4

/* cortex-A7 */
typedef volatile struct {
	uint32	corecontrol;		/* 0x0 */
	uint32	corecapabilities;	/* 0x4 */
	uint32	corestatus;		/* 0x8 */
	uint32	tracecontrol;		/* 0xc */
	uint32	gpioselect;		/* 0x10 */
	uint32	gpioenable;		/* 0x14 */
	uint32	PAD[114];
	uint32	clk_ctl_st;		/* 0x1e0 */
	uint32	workaround;		/* 0x1e4 */
	uint32  powerctl;		/* 0x1e8 */
	uint32  powerctl2;		/* 0x1ec */
} ca7regs_t;
#define ARM_CA7_REG(regs, reg)	(&((ca7regs_t *)regs)->reg)

#if defined(__ARM_ARCH_7M__)
#define ARMREG(regs, reg)	ARM_CM3_REG(regs, reg)
#endif	/* __ARM_ARCH_7M__ */

#if defined(__ARM_ARCH_7R__)
#define ARMREG(regs, reg)	ARM_CR4_REG(regs, reg)
#endif	/* __ARM_ARCH_7R__ */

#if defined(__ARM_ARCH_7A__)
#define ARMREG(regs, reg)	ARM_CA7_REG(regs, reg)
#endif	/* __ARM_ARCH_7A__ */

/* MPU is present mask of ca7regs_t->corecapabilities */
#define CAP_MPU_MASK		2000000u

#endif	/* _LANGUAGE_ASSEMBLY */

#endif	/* _sbhndarm_h_ */
