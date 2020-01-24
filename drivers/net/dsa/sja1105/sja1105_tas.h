/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019, Vladimir Oltean <olteanv@gmail.com>
 */
#ifndef _SJA1105_TAS_H
#define _SJA1105_TAS_H

#include <net/pkt_sched.h>

#if IS_ENABLED(CONFIG_NET_DSA_SJA1105_TAS)

enum sja1105_tas_state {
	SJA1105_TAS_STATE_DISABLED,
	SJA1105_TAS_STATE_ENABLED_NOT_RUNNING,
	SJA1105_TAS_STATE_RUNNING,
};

enum sja1105_ptp_op {
	SJA1105_PTP_NONE,
	SJA1105_PTP_CLOCKSTEP,
	SJA1105_PTP_ADJUSTFREQ,
};

struct sja1105_tas_data {
	struct tc_taprio_qopt_offload *offload[SJA1105_NUM_PORTS];
	enum sja1105_tas_state state;
	enum sja1105_ptp_op last_op;
	struct work_struct tas_work;
	s64 earliest_base_time;
	s64 oper_base_time;
	u64 max_cycle_time;
	bool enabled;
};

int sja1105_setup_tc_taprio(struct dsa_switch *ds, int port,
			    struct tc_taprio_qopt_offload *admin);

void sja1105_tas_setup(struct dsa_switch *ds);

void sja1105_tas_teardown(struct dsa_switch *ds);

void sja1105_tas_clockstep(struct dsa_switch *ds);

void sja1105_tas_adjfreq(struct dsa_switch *ds);

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

static inline void sja1105_tas_clockstep(struct dsa_switch *ds) { }

static inline void sja1105_tas_adjfreq(struct dsa_switch *ds) { }

#endif /* IS_ENABLED(CONFIG_NET_DSA_SJA1105_TAS) */

#endif /* _SJA1105_TAS_H */
