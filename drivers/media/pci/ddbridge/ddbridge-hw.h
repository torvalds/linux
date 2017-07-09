/*
 * ddbridge-hw.h: Digital Devices bridge hardware maps
 *
 * Copyright (C) 2010-2017 Digital Devices GmbH
 *                         Ralph Metzler <rjkm@metzlerbros.de>
 *                         Marcus Metzler <mocm@metzlerbros.de>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 only, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef _DDBRIDGE_HW_H_
#define _DDBRIDGE_HW_H_

#include "ddbridge.h"

/******************************************************************************/

extern const struct ddb_info ddb_none;
extern const struct ddb_info ddb_octopus;
extern const struct ddb_info ddb_octopusv3;
extern const struct ddb_info ddb_octopus_le;
extern const struct ddb_info ddb_octopus_oem;
extern const struct ddb_info ddb_octopus_mini;
extern const struct ddb_info ddb_v6;
extern const struct ddb_info ddb_v6_5;
extern const struct ddb_info ddb_v7;
extern const struct ddb_info ddb_v7a;
extern const struct ddb_info ddb_ctv7;
extern const struct ddb_info ddb_satixS2v3;
extern const struct ddb_info ddb_ci;
extern const struct ddb_info ddb_cis;
extern const struct ddb_info ddb_ci_s2_pro;
extern const struct ddb_info ddb_ci_s2_pro_a;
extern const struct ddb_info ddb_dvbct;

/****************************************************************************/

extern const struct ddb_info ddb_ct2_8;
extern const struct ddb_info ddb_c2t2_8;
extern const struct ddb_info ddb_isdbt_8;
extern const struct ddb_info ddb_c2t2i_v0_8;
extern const struct ddb_info ddb_c2t2i_8;

/****************************************************************************/

extern const struct ddb_info ddb_s2_48;

#endif /* _DDBRIDGE_HW_H */
