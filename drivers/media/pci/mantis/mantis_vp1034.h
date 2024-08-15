/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
	Mantis VP-1034 driver

	Copyright (C) Manu Abraham (abraham.manu@gmail.com)

*/

#ifndef __MANTIS_VP1034_H
#define __MANTIS_VP1034_H

#include <media/dvb_frontend.h>
#include "mantis_common.h"


#define MANTIS_VP_1034_DVB_S	0x0014

extern struct mantis_hwconfig vp1034_config;
extern int vp1034_set_voltage(struct dvb_frontend *fe,
			      enum fe_sec_voltage voltage);

#endif /* __MANTIS_VP1034_H */
