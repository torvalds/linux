/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ddbridge-max.h: Digital Devices bridge MAX card support
 *
 * Copyright (C) 2010-2017 Digital Devices GmbH
 *                         Ralph Metzler <rjkm@metzlerbros.de>
 *                         Marcus Metzler <mocm@metzlerbros.de>
 */

#ifndef _DDBRIDGE_MAX_H_
#define _DDBRIDGE_MAX_H_

#include "ddbridge.h"

/******************************************************************************/

int ddb_lnb_init_fmode(struct ddb *dev, struct ddb_link *link, u32 fm);
int ddb_fe_attach_mxl5xx(struct ddb_input *input);
int ddb_fe_attach_mci(struct ddb_input *input, u32 type);

#endif /* _DDBRIDGE_MAX_H_ */
