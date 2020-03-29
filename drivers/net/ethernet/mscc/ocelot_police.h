/* SPDX-License-Identifier: (GPL-2.0 OR MIT) */
/* Microsemi Ocelot Switch driver
 *
 * Copyright (c) 2019 Microsemi Corporation
 */

#ifndef _MSCC_OCELOT_POLICE_H_
#define _MSCC_OCELOT_POLICE_H_

#include "ocelot.h"

int ocelot_ace_policer_add(struct ocelot *ocelot, u32 pol_ix,
			   struct ocelot_policer *pol);

int ocelot_ace_policer_del(struct ocelot *ocelot, u32 pol_ix);

#endif /* _MSCC_OCELOT_POLICE_H_ */
