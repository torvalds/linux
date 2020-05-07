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

#define work_to_sja1105_tas(d) \
	container_of((d), struct sja1105_tas_data, tas_work)
#define tas_to_sja1105(d) \
	container_of((d), struct sja1105_private, tas_data)

/* This is not a preprocessor macro because the "ns" argument may or may not be
 * s64 at caller side. This ensures it is properly type-cast before div_s64.
 */
static s64 ns_to_sja1105_delta(s64 ns)
{
	return div_s64(ns, 200);
}

static s64 sja1105_delta_to_ns(s64 delta)
{
	return delta * 200;
}

static int sja1105_tas_set_runtime_params(struct sja1105_private *priv)
{
	struct sja1105_tas_data *tas_data = &priv->tas_data;
	struct dsa_switch *ds = priv->ds;
	s64 earliest_base_time = S64_MAX;
	s64 latest_base_time = 0;
	s64 its_cycle_time = 0;
	s64 max_cycle_time = 0;
	int port;

	tas_data->enabled = false;

	for (port = 0; port < SJA1105_NUM_PORTS; port++) {
		const struct tc_taprio_qopt_offload *offload;

		offload = tas_data->offload[port];
		if (!offload)
			continue;

		tas_data->enabled = true;

		if (max_cycle_time < offload->cycle_time)
			max_cycle_time = offload->cycle_time;
		if (latest_base_time < offload->base_time)
			latest_base_time = offload->base_time;
		if (earliest_base_time > offload->base_time) {
			earliest_base_time = offload->base_time;
			its_cycle_time = offload->cycle_time;
		}
	}

	if (!tas_data->enabled)
		return 0;

	/* Roll the earliest base time over until it is in a comparable
	 * time base with the latest, then compare their deltas.
	 * We want to enforce that all ports' base times are within
	 * SJA1105_TAS_MAX_DELTA 200ns cycles of one another.
	 */
	earliest_base_time = future_base_time(earliest_base_time,
					      its_cycle_time,
					      latest_base_time);
	while (earliest_base_time > latest_base_time)
		earliest_base_time -= its_cycle_time;
	if (latest_base_time - earliest_base_time >
	    sja1105_delta_to_ns(SJA1105_TAS_MAX_DELTA)) {
		dev_err(ds->dev,
			"Base times too far apart: min %llu max %llu\n",
			earliest_base_time, latest_base_time);
		return -ERANGE;
	}

	tas_data->earliest_base_time = earliest_base_time;
	tas_data->max_cycle_time = max_cycle_time;

	dev_dbg(ds->dev, "earliest base time %lld ns\n", earliest_base_time);
	dev_dbg(ds->dev, "latest base time %lld ns\n", latest_base_time);
	dev_dbg(ds->dev, "longest cycle time %lld ns\n", max_cycle_time);

	return 0;
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
	int port, rc;

	rc = sja1105_tas_set_runtime_params(priv);
	if (rc < 0)
		return rc;

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
	schedule_entry_points_params->clksrc = SJA1105_TAS_CLKSRC_PTP;
	schedule_entry_points_params->actsubsch = num_cycles - 1;

	for (port = 0; port < SJA1105_NUM_PORTS; port++) {
		const struct tc_taprio_qopt_offload *offload;
		/* Relative base time */
		s64 rbt;

		offload = tas_data->offload[port];
		if (!offload)
			continue;

		schedule_start_idx = k;
		schedule_end_idx = k + offload->num_entries - 1;
		/* This is the base time expressed as a number of TAS ticks
		 * relative to PTPSCHTM, which we'll (perhaps improperly) call
		 * the operational base time.
		 */
		rbt = future_base_time(offload->base_time,
				       offload->cycle_time,
				       tas_data->earliest_base_time);
		rbt -= tas_data->earliest_base_time;
		/* UM10944.pdf 4.2.2. Schedule Entry Points table says that
		 * delta cannot be zero, which is shitty. Advance all relative
		 * base times by 1 TAS delta, so that even the earliest base
		 * time becomes 1 in relative terms. Then start the operational
		 * base time (PTPSCHTM) one TAS delta earlier than planned.
		 */
		entry_point_delta = ns_to_sja1105_delta(rbt) + 1;

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

		return sja1105_static_config_reload(priv, SJA1105_SCHEDULING);
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

	return sja1105_static_config_reload(priv, SJA1105_SCHEDULING);
}

static int sja1105_tas_check_running(struct sja1105_private *priv)
{
	struct sja1105_tas_data *tas_data = &priv->tas_data;
	struct dsa_switch *ds = priv->ds;
	struct sja1105_ptp_cmd cmd = {0};
	int rc;

	rc = sja1105_ptp_commit(ds, &cmd, SPI_READ);
	if (rc < 0)
		return rc;

	if (cmd.ptpstrtsch == 1)
		/* Schedule successfully started */
		tas_data->state = SJA1105_TAS_STATE_RUNNING;
	else if (cmd.ptpstopsch == 1)
		/* Schedule is stopped */
		tas_data->state = SJA1105_TAS_STATE_DISABLED;
	else
		/* Schedule is probably not configured with PTP clock source */
		rc = -EINVAL;

	return rc;
}

/* Write to PTPCLKCORP */
static int sja1105_tas_adjust_drift(struct sja1105_private *priv,
				    u64 correction)
{
	const struct sja1105_regs *regs = priv->info->regs;
	u32 ptpclkcorp = ns_to_sja1105_ticks(correction);

	return sja1105_xfer_u32(priv, SPI_WRITE, regs->ptpclkcorp,
				&ptpclkcorp, NULL);
}

/* Write to PTPSCHTM */
static int sja1105_tas_set_base_time(struct sja1105_private *priv,
				     u64 base_time)
{
	const struct sja1105_regs *regs = priv->info->regs;
	u64 ptpschtm = ns_to_sja1105_ticks(base_time);

	return sja1105_xfer_u64(priv, SPI_WRITE, regs->ptpschtm,
				&ptpschtm, NULL);
}

static int sja1105_tas_start(struct sja1105_private *priv)
{
	struct sja1105_tas_data *tas_data = &priv->tas_data;
	struct sja1105_ptp_cmd *cmd = &priv->ptp_data.cmd;
	struct dsa_switch *ds = priv->ds;
	int rc;

	dev_dbg(ds->dev, "Starting the TAS\n");

	if (tas_data->state == SJA1105_TAS_STATE_ENABLED_NOT_RUNNING ||
	    tas_data->state == SJA1105_TAS_STATE_RUNNING) {
		dev_err(ds->dev, "TAS already started\n");
		return -EINVAL;
	}

	cmd->ptpstrtsch = 1;
	cmd->ptpstopsch = 0;

	rc = sja1105_ptp_commit(ds, cmd, SPI_WRITE);
	if (rc < 0)
		return rc;

	tas_data->state = SJA1105_TAS_STATE_ENABLED_NOT_RUNNING;

	return 0;
}

static int sja1105_tas_stop(struct sja1105_private *priv)
{
	struct sja1105_tas_data *tas_data = &priv->tas_data;
	struct sja1105_ptp_cmd *cmd = &priv->ptp_data.cmd;
	struct dsa_switch *ds = priv->ds;
	int rc;

	dev_dbg(ds->dev, "Stopping the TAS\n");

	if (tas_data->state == SJA1105_TAS_STATE_DISABLED) {
		dev_err(ds->dev, "TAS already disabled\n");
		return -EINVAL;
	}

	cmd->ptpstopsch = 1;
	cmd->ptpstrtsch = 0;

	rc = sja1105_ptp_commit(ds, cmd, SPI_WRITE);
	if (rc < 0)
		return rc;

	tas_data->state = SJA1105_TAS_STATE_DISABLED;

	return 0;
}

/* The schedule engine and the PTP clock are driven by the same oscillator, and
 * they run in parallel. But whilst the PTP clock can keep an absolute
 * time-of-day, the schedule engine is only running in 'ticks' (25 ticks make
 * up a delta, which is 200ns), and wrapping around at the end of each cycle.
 * The schedule engine is started when the PTP clock reaches the PTPSCHTM time
 * (in PTP domain).
 * Because the PTP clock can be rate-corrected (accelerated or slowed down) by
 * a software servo, and the schedule engine clock runs in parallel to the PTP
 * clock, there is logic internal to the switch that periodically keeps the
 * schedule engine from drifting away. The frequency with which this internal
 * syntonization happens is the PTP clock correction period (PTPCLKCORP). It is
 * a value also in the PTP clock domain, and is also rate-corrected.
 * To be precise, during a correction period, there is logic to determine by
 * how many scheduler clock ticks has the PTP clock drifted. At the end of each
 * correction period/beginning of new one, the length of a delta is shrunk or
 * expanded with an integer number of ticks, compared with the typical 25.
 * So a delta lasts for 200ns (or 25 ticks) only on average.
 * Sometimes it is longer, sometimes it is shorter. The internal syntonization
 * logic can adjust for at most 5 ticks each 20 ticks.
 *
 * The first implication is that you should choose your schedule correction
 * period to be an integer multiple of the schedule length. Preferably one.
 * In case there are schedules of multiple ports active, then the correction
 * period needs to be a multiple of them all. Given the restriction that the
 * cycle times have to be multiples of one another anyway, this means the
 * correction period can simply be the largest cycle time, hence the current
 * choice. This way, the updates are always synchronous to the transmission
 * cycle, and therefore predictable.
 *
 * The second implication is that at the beginning of a correction period, the
 * first few deltas will be modulated in time, until the schedule engine is
 * properly phase-aligned with the PTP clock. For this reason, you should place
 * your best-effort traffic at the beginning of a cycle, and your
 * time-triggered traffic afterwards.
 *
 * The third implication is that once the schedule engine is started, it can
 * only adjust for so much drift within a correction period. In the servo you
 * can only change the PTPCLKRATE, but not step the clock (PTPCLKADD). If you
 * want to do the latter, you need to stop and restart the schedule engine,
 * which is what the state machine handles.
 */
static void sja1105_tas_state_machine(struct work_struct *work)
{
	struct sja1105_tas_data *tas_data = work_to_sja1105_tas(work);
	struct sja1105_private *priv = tas_to_sja1105(tas_data);
	struct sja1105_ptp_data *ptp_data = &priv->ptp_data;
	struct timespec64 base_time_ts, now_ts;
	struct dsa_switch *ds = priv->ds;
	struct timespec64 diff;
	s64 base_time, now;
	int rc = 0;

	mutex_lock(&ptp_data->lock);

	switch (tas_data->state) {
	case SJA1105_TAS_STATE_DISABLED:
		/* Can't do anything at all if clock is still being stepped */
		if (tas_data->last_op != SJA1105_PTP_ADJUSTFREQ)
			break;

		rc = sja1105_tas_adjust_drift(priv, tas_data->max_cycle_time);
		if (rc < 0)
			break;

		rc = __sja1105_ptp_gettimex(ds, &now, NULL);
		if (rc < 0)
			break;

		/* Plan to start the earliest schedule first. The others
		 * will be started in hardware, by way of their respective
		 * entry points delta.
		 * Try our best to avoid fringe cases (race condition between
		 * ptpschtm and ptpstrtsch) by pushing the oper_base_time at
		 * least one second in the future from now. This is not ideal,
		 * but this only needs to buy us time until the
		 * sja1105_tas_start command below gets executed.
		 */
		base_time = future_base_time(tas_data->earliest_base_time,
					     tas_data->max_cycle_time,
					     now + 1ull * NSEC_PER_SEC);
		base_time -= sja1105_delta_to_ns(1);

		rc = sja1105_tas_set_base_time(priv, base_time);
		if (rc < 0)
			break;

		tas_data->oper_base_time = base_time;

		rc = sja1105_tas_start(priv);
		if (rc < 0)
			break;

		base_time_ts = ns_to_timespec64(base_time);
		now_ts = ns_to_timespec64(now);

		dev_dbg(ds->dev, "OPER base time %lld.%09ld (now %lld.%09ld)\n",
			base_time_ts.tv_sec, base_time_ts.tv_nsec,
			now_ts.tv_sec, now_ts.tv_nsec);

		break;

	case SJA1105_TAS_STATE_ENABLED_NOT_RUNNING:
		if (tas_data->last_op != SJA1105_PTP_ADJUSTFREQ) {
			/* Clock was stepped.. bad news for TAS */
			sja1105_tas_stop(priv);
			break;
		}

		/* Check if TAS has actually started, by comparing the
		 * scheduled start time with the SJA1105 PTP clock
		 */
		rc = __sja1105_ptp_gettimex(ds, &now, NULL);
		if (rc < 0)
			break;

		if (now < tas_data->oper_base_time) {
			/* TAS has not started yet */
			diff = ns_to_timespec64(tas_data->oper_base_time - now);
			dev_dbg(ds->dev, "time to start: [%lld.%09ld]",
				diff.tv_sec, diff.tv_nsec);
			break;
		}

		/* Time elapsed, what happened? */
		rc = sja1105_tas_check_running(priv);
		if (rc < 0)
			break;

		if (tas_data->state != SJA1105_TAS_STATE_RUNNING)
			/* TAS has started */
			dev_err(ds->dev,
				"TAS not started despite time elapsed\n");

		break;

	case SJA1105_TAS_STATE_RUNNING:
		/* Clock was stepped.. bad news for TAS */
		if (tas_data->last_op != SJA1105_PTP_ADJUSTFREQ) {
			sja1105_tas_stop(priv);
			break;
		}

		rc = sja1105_tas_check_running(priv);
		if (rc < 0)
			break;

		if (tas_data->state != SJA1105_TAS_STATE_RUNNING)
			dev_err(ds->dev, "TAS surprisingly stopped\n");

		break;

	default:
		if (net_ratelimit())
			dev_err(ds->dev, "TAS in an invalid state (incorrect use of API)!\n");
	}

	if (rc && net_ratelimit())
		dev_err(ds->dev, "An operation returned %d\n", rc);

	mutex_unlock(&ptp_data->lock);
}

void sja1105_tas_clockstep(struct dsa_switch *ds)
{
	struct sja1105_private *priv = ds->priv;
	struct sja1105_tas_data *tas_data = &priv->tas_data;

	if (!tas_data->enabled)
		return;

	tas_data->last_op = SJA1105_PTP_CLOCKSTEP;
	schedule_work(&tas_data->tas_work);
}

void sja1105_tas_adjfreq(struct dsa_switch *ds)
{
	struct sja1105_private *priv = ds->priv;
	struct sja1105_tas_data *tas_data = &priv->tas_data;

	if (!tas_data->enabled)
		return;

	/* No reason to schedule the workqueue, nothing changed */
	if (tas_data->state == SJA1105_TAS_STATE_RUNNING)
		return;

	tas_data->last_op = SJA1105_PTP_ADJUSTFREQ;
	schedule_work(&tas_data->tas_work);
}

void sja1105_tas_setup(struct dsa_switch *ds)
{
	struct sja1105_private *priv = ds->priv;
	struct sja1105_tas_data *tas_data = &priv->tas_data;

	INIT_WORK(&tas_data->tas_work, sja1105_tas_state_machine);
	tas_data->state = SJA1105_TAS_STATE_DISABLED;
	tas_data->last_op = SJA1105_PTP_NONE;
}

void sja1105_tas_teardown(struct dsa_switch *ds)
{
	struct sja1105_private *priv = ds->priv;
	struct tc_taprio_qopt_offload *offload;
	int port;

	cancel_work_sync(&priv->tas_data.tas_work);

	for (port = 0; port < SJA1105_NUM_PORTS; port++) {
		offload = priv->tas_data.offload[port];
		if (!offload)
			continue;

		taprio_offload_free(offload);
	}
}
