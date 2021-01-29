/* SPDX-License-Identifier: (GPL-2.0 OR MIT) */
/* Microsemi Ocelot Switch driver
 * Copyright (c) 2019 Microsemi Corporation
 */

#ifndef _MSCC_OCELOT_VCAP_H_
#define _MSCC_OCELOT_VCAP_H_

#include "ocelot.h"
#include <soc/mscc/ocelot_vcap.h>
#include <net/flow_offload.h>

#define OCELOT_POLICER_DISCARD 0x17f

int ocelot_vcap_filter_stats_update(struct ocelot *ocelot,
				    struct ocelot_vcap_filter *rule);
struct ocelot_vcap_filter *
ocelot_vcap_block_find_filter_by_id(struct ocelot_vcap_block *block, int id,
				    bool tc_offload);

void ocelot_detect_vcap_constants(struct ocelot *ocelot);
int ocelot_vcap_init(struct ocelot *ocelot);

int ocelot_setup_tc_cls_flower(struct ocelot_port_private *priv,
			       struct flow_cls_offload *f,
			       bool ingress);

#endif /* _MSCC_OCELOT_VCAP_H_ */
