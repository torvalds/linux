/*
 * PXA910 Power Management Routines
 *
 * This software program is licensed subject to the GNU General Public License
 * (GPL).Version 2,June 1991, available at http://www.fsf.org/copyleft/gpl.html
 *
 * (C) Copyright 2009 Marvell International Ltd.
 * All Rights Reserved
 */

#ifndef __PXA910_PM_H__
#define __PXA910_PM_H__

#define APMU_MOH_IDLE_CFG			APMU_REG(0x0018)
#define APMU_MOH_IDLE_CFG_MOH_IDLE		(1 << 1)
#define APMU_MOH_IDLE_CFG_MOH_PWRDWN		(1 << 5)
#define APMU_MOH_IDLE_CFG_MOH_SRAM_PWRDWN	(1 << 6)
#define APMU_MOH_IDLE_CFG_MOH_PWR_SW(x)		(((x) & 0x3) << 16)
#define APMU_MOH_IDLE_CFG_MOH_L2_PWR_SW(x)	(((x) & 0x3) << 18)
#define APMU_MOH_IDLE_CFG_MOH_DIS_MC_SW_REQ	(1 << 21)
#define APMU_MOH_IDLE_CFG_MOH_MC_WAKE_EN	(1 << 20)

#define APMU_SQU_CLK_GATE_CTRL			APMU_REG(0x001c)
#define APMU_MC_HW_SLP_TYPE			APMU_REG(0x00b0)

#define MPMU_FCCR				MPMU_REG(0x0008)
#define MPMU_APCR				MPMU_REG(0x1000)
#define MPMU_APCR_AXISD				(1 << 31)
#define MPMU_APCR_DSPSD				(1 << 30)
#define MPMU_APCR_SLPEN				(1 << 29)
#define MPMU_APCR_DTCMSD			(1 << 28)
#define MPMU_APCR_DDRCORSD			(1 << 27)
#define MPMU_APCR_APBSD				(1 << 26)
#define MPMU_APCR_BBSD				(1 << 25)
#define MPMU_APCR_SLPWP0			(1 << 23)
#define MPMU_APCR_SLPWP1			(1 << 22)
#define MPMU_APCR_SLPWP2			(1 << 21)
#define MPMU_APCR_SLPWP3			(1 << 20)
#define MPMU_APCR_VCTCXOSD			(1 << 19)
#define MPMU_APCR_SLPWP4			(1 << 18)
#define MPMU_APCR_SLPWP5			(1 << 17)
#define MPMU_APCR_SLPWP6			(1 << 16)
#define MPMU_APCR_SLPWP7			(1 << 15)
#define MPMU_APCR_MSASLPEN			(1 << 14)
#define MPMU_APCR_STBYEN			(1 << 13)

#define MPMU_AWUCRM				MPMU_REG(0x104c)
#define MPMU_AWUCRM_AP_ASYNC_INT		(1 << 25)
#define MPMU_AWUCRM_AP_FULL_IDLE		(1 << 24)
#define MPMU_AWUCRM_SDH1			(1 << 23)
#define MPMU_AWUCRM_SDH2			(1 << 22)
#define MPMU_AWUCRM_KEYPRESS			(1 << 21)
#define MPMU_AWUCRM_TRACKBALL			(1 << 20)
#define MPMU_AWUCRM_NEWROTARY			(1 << 19)
#define MPMU_AWUCRM_RTC_ALARM			(1 << 17)
#define MPMU_AWUCRM_AP2_TIMER_3			(1 << 13)
#define MPMU_AWUCRM_AP2_TIMER_2			(1 << 12)
#define MPMU_AWUCRM_AP2_TIMER_1			(1 << 11)
#define MPMU_AWUCRM_AP1_TIMER_3			(1 << 10)
#define MPMU_AWUCRM_AP1_TIMER_2			(1 << 9)
#define MPMU_AWUCRM_AP1_TIMER_1			(1 << 8)
#define MPMU_AWUCRM_WAKEUP(x)			(1 << ((x) & 0x7))

enum {
	POWER_MODE_ACTIVE = 0,
	POWER_MODE_CORE_INTIDLE,
	POWER_MODE_CORE_EXTIDLE,
	POWER_MODE_APPS_IDLE,
	POWER_MODE_APPS_SLEEP,
	POWER_MODE_SYS_SLEEP,
	POWER_MODE_HIBERNATE,
	POWER_MODE_UDR,
};

extern int pxa910_set_wake(struct irq_data *data, unsigned int on);

#endif
