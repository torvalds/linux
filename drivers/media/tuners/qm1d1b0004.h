/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Sharp QM1D1B0004 satellite tuner
 *
 * Copyright (C) 2014 Akihiro Tsukada <tskd08@gmail.com>
 */

#ifndef QM1D1B0004_H
#define QM1D1B0004_H

#include <media/dvb_frontend.h>

struct qm1d1b0004_config {
	struct dvb_frontend *fe;

	u32 lpf_freq;   /* LPF frequency[kHz]. Default: symbol rate */
	bool half_step; /* use PLL frequency step of 500Hz instead of 1000Hz */
};

/* special values indicating to use the default in qm1d1b0004_config */
#define QM1D1B0004_CFG_PLL_DFLT 0
#define QM1D1B0004_CFG_LPF_DFLT 0

#endif
