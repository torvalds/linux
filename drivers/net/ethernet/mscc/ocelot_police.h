/* SPDX-License-Identifier: (GPL-2.0 OR MIT) */
/* Microsemi Ocelot Switch driver
 *
 * Copyright (c) 2019 Microsemi Corporation
 */

#ifndef _MSCC_OCELOT_POLICE_H_
#define _MSCC_OCELOT_POLICE_H_

#include "ocelot.h"
#include <net/flow_offload.h>

enum mscc_qos_rate_mode {
	MSCC_QOS_RATE_MODE_DISABLED, /* Policer/shaper disabled */
	MSCC_QOS_RATE_MODE_LINE, /* Measure line rate in kbps incl. IPG */
	MSCC_QOS_RATE_MODE_DATA, /* Measures data rate in kbps excl. IPG */
	MSCC_QOS_RATE_MODE_FRAME, /* Measures frame rate in fps */
	__MSCC_QOS_RATE_MODE_END,
	NUM_MSCC_QOS_RATE_MODE = __MSCC_QOS_RATE_MODE_END,
	MSCC_QOS_RATE_MODE_MAX = __MSCC_QOS_RATE_MODE_END - 1,
};

struct qos_policer_conf {
	enum mscc_qos_rate_mode mode;
	bool dlb; /* Enable DLB (dual leaky bucket mode */
	bool cf;  /* Coupling flag (ignored in SLB mode) */
	u32  cir; /* CIR in kbps/fps (ignored in SLB mode) */
	u32  cbs; /* CBS in bytes/frames (ignored in SLB mode) */
	u32  pir; /* PIR in kbps/fps */
	u32  pbs; /* PBS in bytes/frames */
	u8   ipg; /* Size of IPG when MSCC_QOS_RATE_MODE_LINE is chosen */
};

int qos_policer_conf_set(struct ocelot *ocelot, int port, u32 pol_ix,
			 struct qos_policer_conf *conf);

int ocelot_policer_validate(const struct flow_action *action,
			    const struct flow_action_entry *a,
			    struct netlink_ext_ack *extack);

#endif /* _MSCC_OCELOT_POLICE_H_ */
