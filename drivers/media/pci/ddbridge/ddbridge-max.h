/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ddbridge-max.h: Digital Devices bridge MAX card support
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
 */

#ifndef _DDBRIDGE_MAX_H_
#define _DDBRIDGE_MAX_H_

#include "ddbridge.h"

/******************************************************************************/

int ddb_lnb_init_fmode(struct ddb *dev, struct ddb_link *link, u32 fm);
int ddb_fe_attach_mxl5xx(struct ddb_input *input);
int ddb_fe_attach_mci(struct ddb_input *input, u32 type);

#endif /* _DDBRIDGE_MAX_H */
