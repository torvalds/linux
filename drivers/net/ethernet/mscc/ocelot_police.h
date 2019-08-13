/* SPDX-License-Identifier: (GPL-2.0 OR MIT) */
/* Microsemi Ocelot Switch driver
 *
 * Copyright (c) 2019 Microsemi Corporation
 */

#ifndef _MSCC_OCELOT_POLICE_H_
#define _MSCC_OCELOT_POLICE_H_

#include "ocelot.h"

struct ocelot_policer {
	u32 rate; /* kilobit per second */
	u32 burst; /* bytes */
};

int ocelot_port_policer_add(struct ocelot_port *port,
			    struct ocelot_policer *pol);

int ocelot_port_policer_del(struct ocelot_port *port);

#endif /* _MSCC_OCELOT_POLICE_H_ */
