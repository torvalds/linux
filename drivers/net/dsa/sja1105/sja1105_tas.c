// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019, Vladimir Oltean <olteanv@gmail.com>
 */
#include "sja1105.h"

#define SJA1105_TAS_CLKSRC_DISABLED	0
#define SJA1105_TAS_CLKSRC_STANDALONE	1
#define SJA1105_TAS_CLKSRC_AS6802	2
#define SJA1105_TAS_CLKSRC_PTP		3
#define SJA1105_TAS_MAX_DELTA		BIT(19)
#define SJA1105_GATE_MASK		GENMASK_ULL(SJA1105_NUM_TC - 1, 0)

/* This is not a preprocessor macro because the "ns" argument may or may not be
 * s64 at caller side. This ensures it is properly type-cast before div_s64.
 */
static s64 ns_to_sja1105_delta(s64 ns)
{
	return div_s64(ns, 200);
}

/* Lo and behold: the egress scheduler from hell.
 *
 * At the hardware level, the Time-Aware Shaper holds a global linear arrray of
 * all schedule entries for all ports. These are the Gate Control List (GCL)
 * entries, let's call them "timeslots" for short. This linear array of
 * timeslots is held in BLK_IDX_SCHEDULE.
 *
 * Then there are a maximum of 8 "execution threads" inside the switch, which
 * iterate cyclically through the "schedule". Each "cycle" has an entry point
 * and an exit point, both being timeslot indices in the schedule table. The
 * hardware calls each cycle a "subschedule".
 *
 * Subschedule (cycle) i starts when
 *   ptpclkval >= ptpschtm + BLK_IDX_SCHEDULE_ENTRY_POINTS[i].delta.
 *
 * The hardware scheduler iterates BLK_IDX_SCHEDULE with a k ranging from
 *   k = BLK_IDX_SCHEDULE_ENTRY_POINTS[i].address to
 *   k = BLK_IDX_SCHEDULE_PARAMS.subscheind[i]
 *
 * For each schedule entry (timeslot) k, the engine executes the gate control
 * list entry for the duration of BLK_IDX_SCHEDULE[k].delta.
 *
 *         +---------+
 *         |         | BLK_IDX_SCHEDULE_ENTRY_POINTS_PARAMS
 *         +---------+
 *              |
 *              +-----------------+
 *                                | .actsubsch
 *  BLK_IDX_SCHEDULE_ENTRY_POINTS v
 *                 +-------+-------+
 *                 |cycle 0|cycle 1|
 *                 +-------+-------+
 *                   |  |      |  |
 *  +----------------+  |      |  +-------------------------------------+
 *  |   .subschindx     |      |             .subschindx                |
 *  |                   |      +---------------+                        |
 *  |          .address |        .address      |                        |
 *  |                   |                      |                        |
 *  |                   |                      |                        |
 *  |  BLK_IDX_SCHEDULE v                      v                        |
 *  |              +-------+-------+-------+-------+-------+------+     |
 *  |              |entry 0|entry 1|entry 2|entry 3|entry 4|entry5|     |
 *  |              +-------+-------+-------+-------+-------+------+     |
 *  |                                  ^                    ^  ^  ^     |
 *  |                                  |                    |  |  |     |
 *  |        +-------------------------+                    |  |  |     |
 *  |        |              +-------------------------------+  |  |     |
 *  |        |              |              +-------------------+  |     |
 *  |        |              |              |                      |     |
 *  | +---------------------------------------------------------------+ |
 *  | |subscheind[0]<=subscheind[1]<=subscheind[2]<=...<=subscheind[7]| |
 *  | +---------------------------------------------------------------+ |
 *  |        ^              ^                BLK_IDX_SCHEDULE_PARAMS    |
 *  |        |              |                                           |
 *  +--------+              +-------------------------------------------+
 *
 *  In the above picture there are two subschedules (cycles):
 *
 *  - cycle 0: iterates the schedule table from 0 to 2 (and back)
 *  - cycle 1: iterates the schedule table from 3 to 5 (and back)
 *
 *  All other possible execution threads must be marked as unused by making
 *  their "subschedule end index" (subscheind) equal to the last valid
 *  subschedule's end index (in this case 5).
 */
static int sja1105_init_scheduling(struct sja1105_private *priv)
{
	struct sja1105_schedule_entry_points_entry *schedule_entry_points;
	struct sja1105_schedule_entry_points_params_entry
					*schedule_entry_points_params;
	struct sja1105_schedule_params_entry *schedule_params;
	struct sja1105_tas_data *tas_data = &priv->tas_data;
	struct sja1105_schedule_entry *schedule;
	struct sja1105_table *table;
	int schedule_start_idx;
	s64 entry_point_delta;
	int schedule_end_idx;
	int num_entries = 0;
	int num_cycles = 0;
	int cycle = 0;
	int i, k = 0;
	int port;

	/* Discard previous Schedule Table */
	table = &priv->static_config.tables[BLK_IDX_SCHEDULE];
	if (table->entry_count) {
		kfree(table->entries);
		table->entry_count = 0;
	}

	/* Discard previous Schedule Entry Points Parameters Table */
	table = &priv->static_config.tables[BLK_IDX_SCHEDULE_ENTRY_POINTS_PARAMS];
	if (table->entry_count) {
		kfree(table->entries);
		table->entry_count = 0;
	}

	/* Discard previous Schedule Parameters Table */
	table = &priv->static_config.tables[BLK_IDX_SCHEDULE_PARAMS];
	if (table->entry_count) {
		kfree(table->entries);
		table->entry_count = 0;
	}

	/* Discard previous Schedule Entry Points Table */
	table = &priv->static_config.tables[BLK_IDX_SCHEDULE_ENTRY_POINTS];
	if (table->entry_count) {
		kfree(table->entries);
		table->entry_count = 0;
	}

	/* Figure out the dimensioning of the problem */
	for (port = 0; port < SJA1105_NUM_PORTS; port++) {
		if (tas_data->offload[port]) {
			num_entries += tas_data->offload[port]->num_entries;
			num_cycles++;
		}
	}

	/* Nothing to do */
	if (!num_cycles)
		return 0;

	/* Pre-allocate space in the static config tables */

	/* Schedule Table */
	table = &priv->static_config.tables[BLK_IDX_SCHEDULE];
	table->entries = kcalloc(num_entries, table->ops->unpacked_entry_size,
				 GFP_KERNEL);
	if (!table->entries)
		return -ENOMEM;
	table->entry_count = num_entries;
	schedule = table->entries;

	/* Schedule Points Parameters Table */
	table = &priv->static_config.tables[BLK_IDX_SCHEDULE_ENTRY_POINTS_PARAMS];
	table->entries = kcalloc(SJA1105_MAX_SCHEDULE_ENTRY_POINTS_PARAMS_COUNT,
				 table->ops->unpacked_entry_size, GFP_KERNEL);
	if (!table->entries)
		/* Previously allocated memory will be freed automatically in
		 * sja1105_static_config_free. This is true for all early
		 * returns below.
		 */
		return -ENOMEM;
	table->entry_count = SJA1105_MAX_SCHEDULE_ENTRY_POINTS_PARAMS_COUNT;
	schedule_entry_points_params = table->entries;

	/* Schedule Parameters Table */
	table = &priv->static_config.tables[BLK_IDX_SCHEDULE_PARAMS];
	table->entries = kcalloc(SJA1105_MAX_SCHEDULE_PARAMS_COUNT,
				 table->ops->unpacked_entry_size, GFP_KERNEL);
	if (!table->entries)
		return -ENOMEM;
	table->entry_count = SJA1105_MAX_SCHEDULE_PARAMS_COUNT;
	schedule_params = table->entries;

	/* Schedule Entry Points Table */
	table = &priv->static_config.tables[BLK_IDX_SCHEDULE_ENTRY_POINTS];
	table->entries = kcalloc(num_cycles, table->ops->unpacked_entry_size,
				 GFP_KERNEL);
	if (!table->entries)
		return -ENOMEM;
	table->entry_count = num_cycles;
	schedule_entry_points = table->entries;

	/* Finally start populating the static config tables */
	schedule_entry_points_params->clksrc = SJA1105_TAS_CLKSRC_STANDALONE;
	schedule_entry_points_params->actsubsch = num_cycles - 1;

	for (port = 0; port < SJA1105_NUM_PORTS; port++) {
		const struct tc_taprio_qopt_offload *offload;

		offload = tas_data->offload[port];
		if (!offload)
			continue;

		schedule_start_idx = k;
		schedule_end_idx = k + offload->num_entries - 1;
		/* TODO this is the base time for the port's subschedule,
		 * relative to PTPSCHTM. But as we're using the standalone
		 * clock source and not PTP clock as time reference, there's
		 * little point in even trying to put more logic into this,
		 * like preserving the phases between the subschedules of
		 * different ports. We'll get all of that when switching to the
		 * PTP clock source.
		 */
		entry_point_delta = 1;

		schedule_entry_points[cycle].subschindx = cycle;
		schedule_entry_points[cycle].delta = entry_point_delta;
		schedule_entry_points[cycle].address = schedule_start_idx;

		/* The subschedule end indices need to be
		 * monotonically increasing.
		 */
		for (i = cycle; i < 8; i++)
			schedule_params->subscheind[i] = schedule_end_idx;

		for (i = 0; i < offload->num_entries; i++, k++) {
			s64 delta_ns = offload->entries[i].interval;

			schedule[k].delta = ns_to_sja1105_delta(delta_ns);
			schedule[k].destports = BIT(port);
			schedule[k].resmedia_en = true;
			schedule[k].resmedia = SJA1105_GATE_MASK &
					~offload->entries[i].gate_mask;
		}
		cycle++;
	}

	return 0;
}

/* Be there 2 port subschedules, each executing an arbitrary number of gate
 * open/close events cyclically.
 * None of those gate events must ever occur at the exact same time, otherwise
 * the switch is known to act in exotically strange ways.
 * However the hardware doesn't bother performing these integrity checks.
 * So here we are with the task of validating whether the new @admin offload
 * has any conflict with the already established TAS configuration in
 * tas_data->offload.  We already know the other ports are in harmony with one
 * another, otherwise we wouldn't have saved them.
 * Each gate event executes periodically, with a period of @cycle_time and a
 * phase given by its cycle's @base_time plus its offset within the cycle
 * (which in turn is given by the length of the events prior to it).
 * There are two aspects to possible collisions:
 * - Collisions within one cycle's (actually the longest cycle's) time frame.
 *   For that, we need to compare the cartesian product of each possible
 *   occurrence of each event within one cycle time.
 * - Collisions in the future. Events may not collide within one cycle time,
 *   but if two port schedules don't have the same periodicity (aka the cycle
 *   times aren't multiples of one another), they surely will some time in the
 *   future (actually they will collide an infinite amount of times).
 */
static bool
sja1105_tas_check_conflicts(struct sja1105_private *priv, int port,
			    const struct tc_taprio_qopt_offload *admin)
{
	struct sja1105_tas_data *tas_data = &priv->tas_data;
	const struct tc_taprio_qopt_offload *offload;
	s64 max_cycle_time, min_cycle_time;
	s64 delta1, delta2;
	s64 rbt1, rbt2;
	s64 stop_time;
	s64 t1, t2;
	int i, j;
	s32 rem;

	offload = tas_data->offload[port];
	if (!offload)
		return false;

	/* Check if the two cycle times are multiples of one another.
	 * If they aren't, then they will surely collide.
	 */
	max_cycle_time = max(offload->cycle_time, admin->cycle_time);
	min_cycle_time = min(offload->cycle_time, admin->cycle_time);
	div_s64_rem(max_cycle_time, min_cycle_time, &rem);
	if (rem)
		return true;

	/* Calculate the "reduced" base time of each of the two cycles
	 * (transposed back as close to 0 as possible) by dividing to
	 * the cycle time.
	 */
	div_s64_rem(offload->base_time, offload->cycle_time, &rem);
	rbt1 = rem;

	div_s64_rem(admin->base_time, admin->cycle_time, &rem);
	rbt2 = rem;

	stop_time = max_cycle_time + max(rbt1, rbt2);

	/* delta1 is the relative base time of each GCL entry within
	 * the established ports' TAS config.
	 */
	for (i = 0, delta1 = 0;
	     i < offload->num_entries;
	     delta1 += offload->entries[i].interval, i++) {
		/* delta2 is the relative base time of each GCL entry
		 * within the newly added TAS config.
		 */
		for (j = 0, delta2 = 0;
		     j < admin->num_entries;
		     delta2 += admin->entries[j].interval, j++) {
			/* t1 follows all possible occurrences of the
			 * established ports' GCL entry i within the
			 * first cycle time.
			 */
			for (t1 = rbt1 + delta1;
			     t1 <= stop_time;
			     t1 += offload->cycle_time) {
				/* t2 follows all possible occurrences
				 * of the newly added GCL entry j
				 * within the first cycle time.
				 */
				for (t2 = rbt2 + delta2;
				     t2 <= stop_time;
				     t2 += admin->cycle_time) {
					if (t1 == t2) {
						dev_warn(priv->ds->dev,
							 "GCL entry %d collides with entry %d of port %d\n",
							 j, i, port);
						return true;
					}
				}
			}
		}
	}

	return false;
}

int sja1105_setup_tc_taprio(struct dsa_switch *ds, int port,
			    struct tc_taprio_qopt_offload *admin)
{
	struct sja1105_private *priv = ds->priv;
	struct sja1105_tas_data *tas_data = &priv->tas_data;
	int other_port, rc, i;

	/* Can't change an already configured port (must delete qdisc first).
	 * Can't delete the qdisc from an unconfigured port.
	 */
	if (!!tas_data->offload[port] == admin->enable)
		return -EINVAL;

	if (!admin->enable) {
		taprio_offload_free(tas_data->offload[port]);
		tas_data->offload[port] = NULL;

		rc = sja1105_init_scheduling(priv);
		if (rc < 0)
			return rc;

		return sja1105_static_config_reload(priv);
	}

	/* The cycle time extension is the amount of time the last cycle from
	 * the old OPER needs to be extended in order to phase-align with the
	 * base time of the ADMIN when that becomes the new OPER.
	 * But of course our switch needs to be reset to switch-over between
	 * the ADMIN and the OPER configs - so much for a seamless transition.
	 * So don't add insult over injury and just say we don't support cycle
	 * time extension.
	 */
	if (admin->cycle_time_extension)
		return -ENOTSUPP;

	if (!ns_to_sja1105_delta(admin->base_time)) {
		dev_err(ds->dev, "A base time of zero is not hardware-allowed\n");
		return -ERANGE;
	}

	for (i = 0; i < admin->num_entries; i++) {
		s64 delta_ns = admin->entries[i].interval;
		s64 delta_cycles = ns_to_sja1105_delta(delta_ns);
		bool too_long, too_short;

		too_long = (delta_cycles >= SJA1105_TAS_MAX_DELTA);
		too_short = (delta_cycles == 0);
		if (too_long || too_short) {
			dev_err(priv->ds->dev,
				"Interval %llu too %s for GCL entry %d\n",
				delta_ns, too_long ? "long" : "short", i);
			return -ERANGE;
		}
	}

	for (other_port = 0; other_port < SJA1105_NUM_PORTS; other_port++) {
		if (other_port == port)
			continue;

		if (sja1105_tas_check_conflicts(priv, other_port, admin))
			return -ERANGE;
	}

	tas_data->offload[port] = taprio_offload_get(admin);

	rc = sja1105_init_scheduling(priv);
	if (rc < 0)
		return rc;

	return sja1105_static_config_reload(priv);
}

void sja1105_tas_setup(struct dsa_switch *ds)
{
}

void sja1105_tas_teardown(struct dsa_switch *ds)
{
	struct sja1105_private *priv = ds->priv;
	struct tc_taprio_qopt_offload *offload;
	int port;

	for (port = 0; port < SJA1105_NUM_PORTS; port++) {
		offload = priv->tas_data.offload[port];
		if (!offload)
			continue;

		taprio_offload_free(offload);
	}
}
