/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Intel(R) Trace Hub Global Trace Hub (GTH) data structures
 *
 * Copyright (C) 2014-2015 Intel Corporation.
 */

#ifndef __INTEL_TH_GTH_H__
#define __INTEL_TH_GTH_H__

/* Map output port parameter bits to symbolic names */
#define TH_OUTPUT_PARM(name)			\
	TH_OUTPUT_ ## name

enum intel_th_output_parm {
	/* output port type */
	TH_OUTPUT_PARM(port),
	/* generate NULL packet */
	TH_OUTPUT_PARM(null),
	/* packet drop */
	TH_OUTPUT_PARM(drop),
	/* port in reset state */
	TH_OUTPUT_PARM(reset),
	/* flush out data */
	TH_OUTPUT_PARM(flush),
	/* mainenance packet frequency */
	TH_OUTPUT_PARM(smcfreq),
};

/*
 * Register offsets
 */
enum {
	REG_GTH_GTHOPT0		= 0x00, /* Output ports 0..3 config */
	REG_GTH_GTHOPT1		= 0x04, /* Output ports 4..7 config */
	REG_GTH_SWDEST0		= 0x08, /* Switching destination masters 0..7 */
	REG_GTH_GSWTDEST	= 0x88, /* Global sw trace destination */
	REG_GTH_SMCR0		= 0x9c, /* STP mainenance for ports 0/1 */
	REG_GTH_SMCR1		= 0xa0, /* STP mainenance for ports 2/3 */
	REG_GTH_SMCR2		= 0xa4, /* STP mainenance for ports 4/5 */
	REG_GTH_SMCR3		= 0xa8, /* STP mainenance for ports 6/7 */
	REG_GTH_SCR		= 0xc8, /* Source control (storeEn override) */
	REG_GTH_STAT		= 0xd4, /* GTH status */
	REG_GTH_SCR2		= 0xd8, /* Source control (force storeEn off) */
	REG_GTH_DESTOVR		= 0xdc, /* Destination override */
	REG_GTH_SCRPD0		= 0xe0, /* ScratchPad[0] */
	REG_GTH_SCRPD1		= 0xe4, /* ScratchPad[1] */
	REG_GTH_SCRPD2		= 0xe8, /* ScratchPad[2] */
	REG_GTH_SCRPD3		= 0xec, /* ScratchPad[3] */
	REG_TSCU_TSUCTRL	= 0x2000, /* TSCU control register */
	REG_TSCU_TSCUSTAT	= 0x2004, /* TSCU status register */

	/* Common Capture Sequencer (CTS) registers */
	REG_CTS_C0S0_EN		= 0x30c0, /* clause_event_enable_c0s0 */
	REG_CTS_C0S0_ACT	= 0x3180, /* clause_action_control_c0s0 */
	REG_CTS_STAT		= 0x32a0, /* cts_status */
	REG_CTS_CTL		= 0x32a4, /* cts_control */
};

/* waiting for Pipeline Empty bit(s) to assert for GTH */
#define GTH_PLE_WAITLOOP_DEPTH	10000

#define TSUCTRL_CTCRESYNC	BIT(0)
#define TSCUSTAT_CTCSYNCING	BIT(1)

/* waiting for Trigger status to assert for CTS */
#define CTS_TRIG_WAITLOOP_DEPTH	10000

#define CTS_EVENT_ENABLE_IF_ANYTHING	BIT(31)
#define CTS_ACTION_CONTROL_STATE_OFF	27
#define CTS_ACTION_CONTROL_SET_STATE(x)	\
	(((x) & 0x1f) << CTS_ACTION_CONTROL_STATE_OFF)
#define CTS_ACTION_CONTROL_TRIGGER	BIT(4)

#define CTS_STATE_IDLE			0x10u

#define CTS_CTL_SEQUENCER_ENABLE	BIT(0)

#endif /* __INTEL_TH_GTH_H__ */
