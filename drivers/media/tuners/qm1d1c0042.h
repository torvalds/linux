/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Sharp QM1D1C0042 8PSK tuner driver
 *
 * Copyright (C) 2014 Akihiro Tsukada <tskd08@gmail.com>
 */

#ifndef QM1D1C0042_H
#define QM1D1C0042_H

#include <media/dvb_frontend.h>


struct qm1d1c0042_config {
	struct dvb_frontend *fe;

	u32  xtal_freq;    /* [kHz] */ /* currently ignored */
	bool lpf;          /* enable LPF */
	bool fast_srch;    /* enable fast search mode, no LPF */
	u32  lpf_wait;         /* wait in tuning with LPF enabled. [ms] */
	u32  fast_srch_wait;   /* with fast-search mode, no LPF. [ms] */
	u32  normal_srch_wait; /* with no LPF/fast-search mode. [ms] */
};
/* special values indicating to use the default in qm1d1c0042_config */
#define QM1D1C0042_CFG_XTAL_DFLT 0
#define QM1D1C0042_CFG_WAIT_DFLT 0

#endif /* QM1D1C0042_H */
