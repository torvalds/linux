/*
 * ddbridge-maxs8.h: Digital Devices bridge MaxS4/8 support
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

#ifndef _DDBRIDGE_MAXS8_H_
#define _DDBRIDGE_MAXS8_H_

#include "ddbridge.h"

/******************************************************************************/

int lnb_init_fmode(struct ddb *dev, struct ddb_link *link, u32 fm);
int fe_attach_mxl5xx(struct ddb_input *input);

#endif /* _DDBRIDGE_MAXS8_H */
