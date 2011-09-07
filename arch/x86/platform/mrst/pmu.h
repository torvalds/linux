/*
 * mrst/pmu.h - private definitions for MRST Power Management Unit mrst/pmu.c
 *
 * Copyright (c) 2011, Intel Corporation.
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
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef _MRST_PMU_H_
#define _MRST_PMU_H_

#define PCI_DEV_ID_MRST_PMU		0x0810
#define MRST_PMU_DRV_NAME		"mrst_pmu"
#define	PCI_SUB_CLASS_MASK		0xFF00

#define	PCI_VENDOR_CAP_LOG_ID_MASK	0x7F
#define PCI_VENDOR_CAP_LOG_SS_MASK	0x80

#define SUB_SYS_ALL_D0I1	0x01155555
#define S0I3_WAKE_SOURCES	0x00001FFF

#define PM_S0I3_COMMAND					\
	((0 << 31) |	/* Reserved */			\
	(0 << 30) |	/* Core must be idle */		\
	(0xc2 << 22) |	/* ACK C6 trigger */		\
	(3 << 19) |	/* Trigger on DMI message */	\
	(3 << 16) |	/* Enter S0i3 */		\
	(0 << 13) |	/* Numeric mode ID (sw) */	\
	(3 << 9) |	/* Trigger mode */		\
	(0 << 8) |	/* Do not interrupt */		\
	(1 << 0))	/* Set configuration */

#define	LSS_DMI		0
#define	LSS_SD_HC0	1
#define	LSS_SD_HC1	2
#define	LSS_NAND	3
#define	LSS_IMAGING	4
#define	LSS_SECURITY	5
#define	LSS_DISPLAY	6
#define	LSS_USB_HC	7
#define	LSS_USB_OTG	8
#define	LSS_AUDIO	9
#define	LSS_AUDIO_LPE	9
#define	LSS_AUDIO_SSP	9
#define	LSS_I2C0	10
#define	LSS_I2C1	10
#define	LSS_I2C2	10
#define	LSS_KBD		10
#define	LSS_SPI0	10
#define	LSS_SPI1	10
#define	LSS_SPI2	10
#define	LSS_GPIO	10
#define	LSS_SRAM	11	/* used by SCU, do not touch */
#define	LSS_SD_HC2	12
/* LSS hardware bits 15,14,13 are hardwired to 0, thus unusable */
#define MRST_NUM_LSS	13

#define MIN(a, b) (((a) < (b)) ? (a) : (b))

#define	SSMSK(mask, lss) ((mask) << ((lss) * 2))
#define	D0	0
#define	D0i1	1
#define	D0i2	2
#define	D0i3	3

#define S0I3_SSS_TARGET	(		\
	SSMSK(D0i1, LSS_DMI) |		\
	SSMSK(D0i3, LSS_SD_HC0) |	\
	SSMSK(D0i3, LSS_SD_HC1) |	\
	SSMSK(D0i3, LSS_NAND) |		\
	SSMSK(D0i3, LSS_SD_HC2) |	\
	SSMSK(D0i3, LSS_IMAGING) |	\
	SSMSK(D0i3, LSS_SECURITY) |	\
	SSMSK(D0i3, LSS_DISPLAY) |	\
	SSMSK(D0i3, LSS_USB_HC) |	\
	SSMSK(D0i3, LSS_USB_OTG) |	\
	SSMSK(D0i3, LSS_AUDIO) |	\
	SSMSK(D0i1, LSS_I2C0))

/*
 * D0i1 on Langwell is Autonomous Clock Gating (ACG).
 * Enable ACG on every LSS except camera and audio
 */
#define D0I1_ACG_SSS_TARGET	 \
	(SUB_SYS_ALL_D0I1 & ~SSMSK(D0i1, LSS_IMAGING) & ~SSMSK(D0i1, LSS_AUDIO))

enum cm_mode {
	CM_NOP,			/* ignore the config mode value */
	CM_IMMEDIATE,
	CM_DELAY,
	CM_TRIGGER,
	CM_INVALID
};

enum sys_state {
	SYS_STATE_S0I0,
	SYS_STATE_S0I1,
	SYS_STATE_S0I2,
	SYS_STATE_S0I3,
	SYS_STATE_S3,
	SYS_STATE_S5
};

#define SET_CFG_CMD	1

enum int_status {
	INT_SPURIOUS = 0,
	INT_CMD_DONE = 1,
	INT_CMD_ERR = 2,
	INT_WAKE_RX = 3,
	INT_SS_ERROR = 4,
	INT_S0IX_MISS = 5,
	INT_NO_ACKC6 = 6,
	INT_INVALID = 7,
};

/* PMU register interface */
static struct mrst_pmu_reg {
	u32 pm_sts;		/* 0x00 */
	u32 pm_cmd;		/* 0x04 */
	u32 pm_ics;		/* 0x08 */
	u32 _resv1;		/* 0x0C */
	u32 pm_wkc[2];		/* 0x10 */
	u32 pm_wks[2];		/* 0x18 */
	u32 pm_ssc[4];		/* 0x20 */
	u32 pm_sss[4];		/* 0x30 */
	u32 pm_wssc[4];		/* 0x40 */
	u32 pm_c3c4;		/* 0x50 */
	u32 pm_c5c6;		/* 0x54 */
	u32 pm_msi_disable;	/* 0x58 */
} *pmu_reg;

static inline u32 pmu_read_sts(void) { return readl(&pmu_reg->pm_sts); }
static inline u32 pmu_read_ics(void) { return readl(&pmu_reg->pm_ics); }
static inline u32 pmu_read_wks(void) { return readl(&pmu_reg->pm_wks[0]); }
static inline u32 pmu_read_sss(void) { return readl(&pmu_reg->pm_sss[0]); }

static inline void pmu_write_cmd(u32 arg) { writel(arg, &pmu_reg->pm_cmd); }
static inline void pmu_write_ics(u32 arg) { writel(arg, &pmu_reg->pm_ics); }
static inline void pmu_write_wkc(u32 arg) { writel(arg, &pmu_reg->pm_wkc[0]); }
static inline void pmu_write_ssc(u32 arg) { writel(arg, &pmu_reg->pm_ssc[0]); }
static inline void pmu_write_wssc(u32 arg)
					{ writel(arg, &pmu_reg->pm_wssc[0]); }

static inline void pmu_msi_enable(void) { writel(0, &pmu_reg->pm_msi_disable); }
static inline u32 pmu_msi_is_disabled(void)
				{ return readl(&pmu_reg->pm_msi_disable); }

union pmu_pm_ics {
	struct {
		u32 cause:8;
		u32 enable:1;
		u32 pending:1;
		u32 reserved:22;
	} bits;
	u32 value;
};

static inline void pmu_irq_enable(void)
{
	union pmu_pm_ics pmu_ics;

	pmu_ics.value = pmu_read_ics();
	pmu_ics.bits.enable = 1;
	pmu_write_ics(pmu_ics.value);
}

union pmu_pm_status {
	struct {
		u32 pmu_rev:8;
		u32 pmu_busy:1;
		u32 mode_id:4;
		u32 Reserved:19;
	} pmu_status_parts;
	u32 pmu_status_value;
};

static inline int pmu_read_busy_status(void)
{
	union pmu_pm_status result;

	result.pmu_status_value = pmu_read_sts();

	return result.pmu_status_parts.pmu_busy;
}

/* pmu set config parameters */
struct cfg_delay_param_t {
	u32 cmd:8;
	u32 ioc:1;
	u32 cfg_mode:4;
	u32 mode_id:3;
	u32 sys_state:3;
	u32 cfg_delay:8;
	u32 rsvd:5;
};

struct cfg_trig_param_t {
	u32 cmd:8;
	u32 ioc:1;
	u32 cfg_mode:4;
	u32 mode_id:3;
	u32 sys_state:3;
	u32 cfg_trig_type:3;
	u32 cfg_trig_val:8;
	u32 cmbi:1;
	u32 rsvd1:1;
};

union pmu_pm_set_cfg_cmd_t {
	union {
		struct cfg_delay_param_t d_param;
		struct cfg_trig_param_t t_param;
	} pmu2_params;
	u32 pmu_pm_set_cfg_cmd_value;
};

#ifdef FUTURE_PATCH
extern int mrst_s0i3_entry(u32 regval, u32 *regaddr);
#else
static inline int mrst_s0i3_entry(u32 regval, u32 *regaddr) { return -1; }
#endif
#endif
