/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019, Vladimir Oltean <olteanv@gmail.com>
 */
#ifndef _SJA1105_TAS_H
#define _SJA1105_TAS_H

#include <net/pkt_sched.h>

#if IS_ENABLED(CONFIG_NET_DSA_SJA1105_TAS)

struct sja1105_tas_data {
	struct tc_taprio_qopt_offload *offload[SJA1105_NUM_PORTS];
};

int sja1105_setup_tc_taprio(struct dsa_switch *ds, int port,
			    struct tc_taprio_qopt_offload *admin);

void sja1105_tas_setup(struct dsa_switch *ds);

void sja1105_tas_teardown(struct dsa_switch *ds);

#else

/* C doesn't allow empty structures, bah! */
struct sja1105_tas_data {
	u8 dummy;
};

static inline int sja1105_setup_tc_taprio(struct dsa_switch *ds, int port,
					  struct tc_taprio_qopt_offload *admin)
{
	return -EOPNOTSUPP;
}

static inline void sja1105_tas_setup(struct dsa_switch *ds) { }

static inline void sja1105_tas_teardown(struct dsa_switch *ds) { }

#endif /* IS_ENABLED(CONFIG_NET_DSA_SJA1105_TAS) */

#endif /* _SJA1105_TAS_H */
