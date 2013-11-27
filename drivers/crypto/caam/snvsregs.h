/*
 * SNVS hardware register-level view
 *
 * Copyright (C) 2015 Freescale Semiconductor, Inc., All Rights Reserved
 */

#ifndef SNVSREGS_H
#define SNVSREGS_H

#include <linux/types.h>
#include <linux/io.h>

/*
 * SNVS High Power Domain
 * Includes security violations, HA counter, RTC, alarm
 */
struct snvs_hp {
	u32 lock;
	u32 cmd;
	u32 ctl;
	u32 secvio_int_en;	/* Security Violation Interrupt Enable */
	u32 secvio_int_ctl;	/* Security Violation Interrupt Control */
	u32 status;
	u32 secvio_status;	/* Security Violation Status */
	u32 ha_counteriv;	/* High Assurance Counter IV */
	u32 ha_counter;		/* High Assurance Counter */
	u32 rtc_msb;		/* Real Time Clock/Counter MSB */
	u32 rtc_lsb;		/* Real Time Counter LSB */
	u32 time_alarm_msb;	/* Time Alarm MSB */
	u32 time_alarm_lsb;	/* Time Alarm LSB */
};

#define HP_LOCK_HAC_LCK		0x00040000
#define HP_LOCK_HPSICR_LCK	0x00020000
#define HP_LOCK_HPSVCR_LCK	0x00010000
#define HP_LOCK_MKEYSEL_LCK	0x00000200
#define HP_LOCK_TAMPCFG_LCK	0x00000100
#define HP_LOCK_TAMPFLT_LCK	0x00000080
#define HP_LOCK_SECVIO_LCK	0x00000040
#define HP_LOCK_GENP_LCK	0x00000020
#define HP_LOCK_MONOCTR_LCK	0x00000010
#define HP_LOCK_CALIB_LCK	0x00000008
#define HP_LOCK_SRTC_LCK	0x00000004
#define HP_LOCK_ZMK_RD_LCK	0x00000002
#define HP_LOCK_ZMK_WT_LCK	0x00000001

#define HP_CMD_NONPRIV_AXS	0x80000000
#define HP_CMD_HAC_STOP		0x00080000
#define HP_CMD_HAC_CLEAR	0x00040000
#define HP_CMD_HAC_LOAD		0x00020000
#define HP_CMD_HAC_CFG_EN	0x00010000
#define HP_CMD_SNVS_MSTR_KEY	0x00002000
#define HP_CMD_PROG_ZMK		0x00001000
#define HP_CMD_SW_LPSV		0x00000400
#define HP_CMD_SW_FSV		0x00000200
#define HP_CMD_SW_SV		0x00000100
#define HP_CMD_LP_SWR_DIS	0x00000020
#define HP_CMD_LP_SWR		0x00000010
#define HP_CMD_SSM_SFNS_DIS	0x00000004
#define HP_CMD_SSM_ST_DIS	0x00000002
#define HP_CMD_SMM_ST		0x00000001

#define HP_CTL_TIME_SYNC	0x00010000
#define HP_CTL_CAL_VAL_SHIFT	10
#define HP_CTL_CAL_VAL_MASK	(0x1f << HP_CTL_CALIB_SHIFT)
#define HP_CTL_CALIB_EN		0x00000100
#define HP_CTL_PI_FREQ_SHIFT	4
#define HP_CTL_PI_FREQ_MASK	(0xf << HP_CTL_PI_FREQ_SHIFT)
#define HP_CTL_PI_EN		0x00000008
#define HP_CTL_TIMEALARM_EN	0x00000002
#define HP_CTL_RTC_EN		0x00000001

#define HP_SECVIO_INTEN_EN	0x10000000
#define HP_SECVIO_INTEN_SRC5	0x00000020
#define HP_SECVIO_INTEN_SRC4	0x00000010
#define HP_SECVIO_INTEN_SRC3	0x00000008
#define HP_SECVIO_INTEN_SRC2	0x00000004
#define HP_SECVIO_INTEN_SRC1	0x00000002
#define HP_SECVIO_INTEN_SRC0	0x00000001
#define HP_SECVIO_INTEN_ALL	0x8000003f

#define HP_SECVIO_ICTL_CFG_SHIFT	30
#define HP_SECVIO_ICTL_CFG_MASK		(0x3 << HP_SECVIO_ICTL_CFG_SHIFT)
#define HP_SECVIO_ICTL_CFG5_SHIFT	5
#define HP_SECVIO_ICTL_CFG5_MASK	(0x3 << HP_SECVIO_ICTL_CFG5_SHIFT)
#define HP_SECVIO_ICTL_CFG_DISABLE	0
#define HP_SECVIO_ICTL_CFG_NONFATAL	1
#define HP_SECVIO_ICTL_CFG_FATAL	2
#define HP_SECVIO_ICTL_CFG4_FATAL	0x00000010
#define HP_SECVIO_ICTL_CFG3_FATAL	0x00000008
#define HP_SECVIO_ICTL_CFG2_FATAL	0x00000004
#define HP_SECVIO_ICTL_CFG1_FATAL	0x00000002
#define HP_SECVIO_ICTL_CFG0_FATAL	0x00000001

#define HP_STATUS_ZMK_ZERO		0x80000000
#define HP_STATUS_OTPMK_ZERO		0x08000000
#define HP_STATUS_OTPMK_SYN_SHIFT	16
#define HP_STATUS_OTPMK_SYN_MASK	(0x1ff << HP_STATUS_OTPMK_SYN_SHIFT)
#define HP_STATUS_SSM_ST_SHIFT		8
#define HP_STATUS_SSM_ST_MASK		(0xf << HP_STATUS_SSM_ST_SHIFT)
#define HP_STATUS_SSM_ST_INIT		0
#define HP_STATUS_SSM_ST_HARDFAIL	1
#define HP_STATUS_SSM_ST_SOFTFAIL	3
#define HP_STATUS_SSM_ST_INITINT	8
#define HP_STATUS_SSM_ST_CHECK		9
#define HP_STATUS_SSM_ST_NONSECURE	11
#define HP_STATUS_SSM_ST_TRUSTED	13
#define HP_STATUS_SSM_ST_SECURE		15

#define HP_SECVIOST_ZMK_ECC_FAIL	0x08000000	/* write to clear */
#define HP_SECVIOST_ZMK_SYN_SHIFT	16
#define HP_SECVIOST_ZMK_SYN_MASK	(0x1ff << HP_SECVIOST_ZMK_SYN_SHIFT)
#define HP_SECVIOST_SECVIO5		0x00000020
#define HP_SECVIOST_SECVIO4		0x00000010
#define HP_SECVIOST_SECVIO3		0x00000008
#define HP_SECVIOST_SECVIO2		0x00000004
#define HP_SECVIOST_SECVIO1		0x00000002
#define HP_SECVIOST_SECVIO0		0x00000001
#define HP_SECVIOST_SECVIOMASK		0x0000003f

/*
 * SNVS Low Power Domain
 * Includes glitch detector, SRTC, alarm, monotonic counter, ZMK
 */
struct snvs_lp {
	u32 lock;
	u32 ctl;
	u32 mstr_key_ctl;	/* Master Key Control */
	u32 secvio_ctl;		/* Security Violation Control */
	u32 tamper_filt_cfg;	/* Tamper Glitch Filters Configuration */
	u32 tamper_det_cfg;	/* Tamper Detectors Configuration */
	u32 status;
	u32 srtc_msb;		/* Secure Real Time Clock/Counter MSB */
	u32 srtc_lsb;		/* Secure Real Time Clock/Counter LSB */
	u32 time_alarm;		/* Time Alarm */
	u32 smc_msb;		/* Secure Monotonic Counter MSB */
	u32 smc_lsb;		/* Secure Monotonic Counter LSB */
	u32 pwr_glitch_det;	/* Power Glitch Detector */
	u32 gen_purpose;
	u32 zmk[8];		/* Zeroizable Master Key */
};

#define LP_LOCK_MKEYSEL_LCK	0x00000200
#define LP_LOCK_TAMPDET_LCK	0x00000100
#define LP_LOCK_TAMPFLT_LCK	0x00000080
#define LP_LOCK_SECVIO_LCK	0x00000040
#define LP_LOCK_GENP_LCK	0x00000020
#define LP_LOCK_MONOCTR_LCK	0x00000010
#define LP_LOCK_CALIB_LCK	0x00000008
#define LP_LOCK_SRTC_LCK	0x00000004
#define LP_LOCK_ZMK_RD_LCK	0x00000002
#define LP_LOCK_ZMK_WT_LCK	0x00000001

#define LP_CTL_CAL_VAL_SHIFT	10
#define LP_CTL_CAL_VAL_MASK	(0x1f << LP_CTL_CAL_VAL_SHIFT)
#define LP_CTL_CALIB_EN		0x00000100
#define LP_CTL_SRTC_INVAL_EN	0x00000010
#define LP_CTL_WAKE_INT_EN	0x00000008
#define LP_CTL_MONOCTR_EN	0x00000004
#define LP_CTL_TIMEALARM_EN	0x00000002
#define LP_CTL_SRTC_EN		0x00000001

#define LP_MKEYCTL_ZMKECC_SHIFT	8
#define LP_MKEYCTL_ZMKECC_MASK	(0xff << LP_MKEYCTL_ZMKECC_SHIFT)
#define LP_MKEYCTL_ZMKECC_EN	0x00000010
#define LP_MKEYCTL_ZMKECC_VAL	0x00000008
#define LP_MKEYCTL_ZMKECC_PROG	0x00000004
#define LP_MKEYCTL_MKSEL_SHIFT	0
#define LP_MKEYCTL_MKSEL_MASK	(3 << LP_MKEYCTL_MKSEL_SHIFT)
#define LP_MKEYCTL_MK_OTP	0
#define LP_MKEYCTL_MK_ZMK	2
#define LP_MKEYCTL_MK_COMB	3

#define LP_SECVIO_CTL_SRC5	0x20
#define LP_SECVIO_CTL_SRC4	0x10
#define LP_SECVIO_CTL_SRC3	0x08
#define LP_SECVIO_CTL_SRC2	0x04
#define LP_SECVIO_CTL_SRC1	0x02
#define LP_SECVIO_CTL_SRC0	0x01

#define LP_TAMPFILT_EXT2_EN	0x80000000
#define LP_TAMPFILT_EXT2_SHIFT	24
#define LP_TAMPFILT_EXT2_MASK	(0x1f << LP_TAMPFILT_EXT2_SHIFT)
#define LP_TAMPFILT_EXT1_EN	0x00800000
#define LP_TAMPFILT_EXT1_SHIFT	16
#define LP_TAMPFILT_EXT1_MASK	(0x1f << LP_TAMPFILT_EXT1_SHIFT)
#define LP_TAMPFILT_WM_EN	0x00000080
#define LP_TAMPFILT_WM_SHIFT	0
#define LP_TAMPFILT_WM_MASK	(0x1f << LP_TAMPFILT_WM_SHIFT)

#define LP_TAMPDET_OSC_BPS	0x10000000
#define LP_TAMPDET_VRC_SHIFT	24
#define LP_TAMPDET_VRC_MASK	(3 << LP_TAMPFILT_VRC_SHIFT)
#define LP_TAMPDET_HTDC_SHIFT	20
#define LP_TAMPDET_HTDC_MASK	(3 << LP_TAMPFILT_HTDC_SHIFT)
#define LP_TAMPDET_LTDC_SHIFT	16
#define LP_TAMPDET_LTDC_MASK	(3 << LP_TAMPFILT_LTDC_SHIFT)
#define LP_TAMPDET_POR_OBS	0x00008000
#define LP_TAMPDET_PFD_OBS	0x00004000
#define LP_TAMPDET_ET2_EN	0x00000400
#define LP_TAMPDET_ET1_EN	0x00000200
#define LP_TAMPDET_WMT2_EN	0x00000100
#define LP_TAMPDET_WMT1_EN	0x00000080
#define LP_TAMPDET_VT_EN	0x00000040
#define LP_TAMPDET_TT_EN	0x00000020
#define LP_TAMPDET_CT_EN	0x00000010
#define LP_TAMPDET_MCR_EN	0x00000004
#define LP_TAMPDET_SRTCR_EN	0x00000002

#define LP_STATUS_SECURE
#define LP_STATUS_NONSECURE
#define LP_STATUS_SCANEXIT	0x00100000	/* all write 1 clear here on */
#define LP_STATUS_EXT_SECVIO	0x00010000
#define LP_STATUS_ET2		0x00000400
#define LP_STATUS_ET1		0x00000200
#define LP_STATUS_WMT2		0x00000100
#define LP_STATUS_WMT1		0x00000080
#define LP_STATUS_VTD		0x00000040
#define LP_STATUS_TTD		0x00000020
#define LP_STATUS_CTD		0x00000010
#define LP_STATUS_PGD		0x00000008
#define LP_STATUS_MCR		0x00000004
#define LP_STATUS_SRTCR		0x00000002
#define LP_STATUS_LPTA		0x00000001

/* Full SNVS register page, including version/options */
struct snvs_full {
	struct snvs_hp hp;
	struct snvs_lp lp;
	u32 rsvd[731];		/* deadspace 0x08c-0xbf7 */

	/* Version / Revision / Option ID space - end of register page */
	u32 vid;		/* 0xbf8 HP Version ID (VID 1) */
	u32 opt_rev;		/* 0xbfc HP Options / Revision (VID 2) */
};

#endif /* SNVSREGS_H */
