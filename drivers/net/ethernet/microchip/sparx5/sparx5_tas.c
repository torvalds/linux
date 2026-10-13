// SPDX-License-Identifier: GPL-2.0+
/* Microchip Sparx5 Switch driver
 *
 * Copyright (c) 2026 Microchip Technology Inc. and its subsidiaries.
 */

#include <net/pkt_sched.h>

#include "sparx5_main.h"
#include "sparx5_qos.h"

enum sparx5_tas_link_speed {
	TAS_SPEED_NO_GB,
	TAS_SPEED_10,
	TAS_SPEED_100,
	TAS_SPEED_1000,
	TAS_SPEED_2500,
	TAS_SPEED_5000,
	TAS_SPEED_10000,
	TAS_SPEED_25000,
};

/* Maximum time in milliseconds to wait for TAS state transitions. */
#define TAS_TIMEOUT_MS 2000

/* Minimum supported cycle time in nanoseconds */
#define TAS_MIN_CYCLE_TIME_NS (1 * NSEC_PER_USEC) /* 1 usec */

/* Maximum supported cycle time in nanoseconds */
#define TAS_MAX_CYCLE_TIME_NS ((1 * NSEC_PER_SEC) - 1) /* 999.999.999 nsec */

/* STARTUP_TIME in units of 256 ns, must be non-zero for a list to start */
#define TAS_STARTUP_TIME_DEFAULT 8

/* We use 2 TAS lists per present port, as far as the chip has lists:
 * num_tas_lists = min(port_count * SPX5_TAS_ENTRIES_PER_PORT, n_tas_lists);
 *
 * The index for the 2 entries of the n'th present port is calculated as:
 * index_1 = n * SPX5_TAS_ENTRIES_PER_PORT;
 * index_2 = index_1 + 1;
 *
 * GCL entries are allocated from a bitmap shared by all ports and sized to the
 * chip's GCL pool. Each list records the range it owns. The range is freed
 * once hardware reports the list back in ADMIN, which for a list obsoleted by
 * a replace happens some time after the replace itself. Entry 0 is reserved:
 * unwritten NEXT_GCL fields read as 0, so a list must never own it.
 *
 * We use 1 TAS profile per port, shared by both of its lists:
 * index = portno;
 *
 * We do not yet support frame preemption and the default guard band
 * of 1536 bytes is always used on all queues.
 *
 * The gate mask of each entry is written as is to the port's 8 queues,
 * where bit n gates queue (QoS class) n. The mqprio parameters of the
 * taprio qdisc (prio_tc_map, queue ranges) are not used, so a schedule
 * must use 8 traffic classes mapped 1:1 to the queues, as with the
 * mqprio offload. Other values of num_tc are rejected.
 *
 * These are the possible combinations of states for the two TAS lists in a
 * running system.
 *
 * ADMIN/ADMIN: No schedules are currently operating or pending.
 *
 * ADMIN/PENDING: No schedule is currently operating but one is pending and
 * when current time exceeds the configured base time it will automatically
 * enter the OPERATING state. The state is now ADMIN/OPERATING.
 *
 * ADMIN/OPERATING: A schedule is currently operating and will run until it is
 * terminated manually.
 *
 * OPERATING/PENDING: A schedule is currently operating and another is
 * pending. When current time exceeds the configured base time for the
 * pending schedule it will automatically stop the operating schedule and
 * enter the OPERATING state. The state is now ADMIN/OPERATING.
 *
 * When we want to disable TAS on a port, we must stop schedules that are in
 * state PENDING or OPERATING.
 * Pending schedules are stopped first and then operating schedules.
 * The gate state for the port must be restored to "all-queues-open" manually
 * in case the schedule was stopped with one or more of the queues closed.
 *
 * When we add a schedule we always use a base time in the future, where base
 * time is at least current time + (2 * cycle time). This is a requirement
 * from the hardware. This means that a schedule will always start in state
 * PENDING. It also means that we always use the new schedule to stop an
 * eventually operating schedule.
 *
 * When we want to add a new schedule we must consider the current state of
 * the two TAS list entries:
 *
 * ADMIN/ADMIN: Just add the new schedule in one of the TAS lists.
 *
 * ADMIN/PENDING: Stop the current pending schedule and add the new one.
 *
 * ADMIN/OPERATING: Add the new schedule in the TAS list that is currently in
 * admin state and configure it to stop the currently operating schedule when
 * current time exceeds the configured base time in the new schedule.
 *
 * OPERATING/PENDING: Stop the current pending schedule, add the new one and
 * configure it to stop the currently operating schedule when
 * current time exceeds the configured base time in the new schedule.
 */

/* Scheduler element index (HSCH_POS) for this port's per-port TAS gate,
 * i.e. its position at the layer-2 boundary of the scheduling hierarchy.
 */
static int sparx5_tas_scheduler_get(struct sparx5 *sparx5, int portno)
{
	if (is_sparx5(sparx5))
		return SPX5_HSCH_L0_SE_CNT + SPX5_HSCH_L1_SE_CNT + portno;
	else
		return 1120 + portno;
}

static int sparx5_tas_list_index(struct sparx5_port *port, u8 tas_entry)
{
	const struct sparx5_consts *consts = port->sparx5->data->consts;
	int portno, pidx = 0;

	/* Limit the index to available ports: the n'th present port uses
	 * lists 2n and 2n + 1, so the lists of all ports fit in
	 * port_count * SPX5_TAS_ENTRIES_PER_PORT.
	 */
	for (portno = 0; portno < consts->n_ports; ++portno) {
		if (portno == port->portno)
			return (pidx * SPX5_TAS_ENTRIES_PER_PORT) + tas_entry;
		if (port->sparx5->ports[portno])
			pidx++;
	}

	return 0;
}

/* Number of TAS lists in use: two per present port, as far as the chip has
 * lists for. A port whose lists fall outside this range has no TAS.
 */
static int sparx5_tas_num_lists(struct sparx5 *sparx5)
{
	return min_t(int, sparx5->port_count * SPX5_TAS_ENTRIES_PER_PORT,
		     sparx5->data->consts->n_tas_lists);
}

static bool sparx5_tas_port_has_lists(struct sparx5_port *port)
{
	return sparx5_tas_list_index(port, SPX5_TAS_ENTRIES_PER_PORT - 1) <
	       sparx5_tas_num_lists(port->sparx5);
}

static const char *sparx5_tas_state_to_str(int state)
{
	switch (state) {
	case SPX5_TAS_STATE_ADMIN:
		return "ADMIN";
	case SPX5_TAS_STATE_ADVANCING:
		return "ADVANCING";
	case SPX5_TAS_STATE_PENDING:
		return "PENDING";
	case SPX5_TAS_STATE_OPERATING:
		return "OPERATING";
	case SPX5_TAS_STATE_TERMINATING:
		return "TERMINATING";
	default:
		return "??";
	}
}

static void sparx5_tas_select_list(struct sparx5 *sparx5, int list)
{
	spx5_rmw(HSCH_TAS_CFG_CTRL_LIST_NUM_SET(list),
		 HSCH_TAS_CFG_CTRL_LIST_NUM,
		 sparx5,
		 HSCH_TAS_CFG_CTRL);
}

static int sparx5_tas_list_state_get(struct sparx5 *sparx5)
{
	return HSCH_TAS_LIST_STATE_LIST_STATE_GET(spx5_rd(sparx5,
							  HSCH_TAS_LIST_STATE));
}

static int sparx5_tas_gcl_alloc(struct sparx5 *sparx5, int count)
{
	u32 n_gcl = sparx5->data->consts->n_tas_gcl;
	unsigned long base;

	base = bitmap_find_next_zero_area(sparx5->tas_gcl_bitmap,
					  n_gcl,
					  0,
					  count,
					  0);
	if (base >= n_gcl)
		return -ENOSPC;

	bitmap_set(sparx5->tas_gcl_bitmap, base, count);

	return base;
}

static void sparx5_tas_gcl_free(struct sparx5_port *port, int slot)
{
	if (port->tas_lists[slot].gcl_count)
		bitmap_clear(port->sparx5->tas_gcl_bitmap,
			     port->tas_lists[slot].gcl_base,
			     port->tas_lists[slot].gcl_count);

	port->tas_lists[slot].gcl_base = 0;
	port->tas_lists[slot].gcl_count = 0;
}

/* Free the GCL entries of every list that hardware has returned to ADMIN */
static void sparx5_tas_gcl_flush(struct sparx5 *sparx5)
{
	struct sparx5_port *port;
	int i, j;

	lockdep_assert_held(&sparx5->tas_lock);

	for (i = 0; i < sparx5->data->consts->n_ports; i++) {
		port = sparx5->ports[i];
		if (!port)
			continue;

		for (j = 0; j < SPX5_TAS_ENTRIES_PER_PORT; j++) {
			if (!port->tas_lists[j].gcl_count)
				continue;

			sparx5_tas_select_list(sparx5,
					       sparx5_tas_list_index(port, j));
			if (sparx5_tas_list_state_get(sparx5) ==
			    SPX5_TAS_STATE_ADMIN)
				sparx5_tas_gcl_free(port, j);
		}
	}
}

/* Request state 'req' (or only wait if 'req' < 0) on the selected list until
 * it reaches a state in the 'done' mask.
 */
static int sparx5_tas_list_state_wait(struct sparx5_port *port, int list,
				      int req, u32 done)
{
	struct sparx5 *sparx5 = port->sparx5;
	unsigned long end;
	int state;

	/* Do not wait forever for the state change */
	end = jiffies + msecs_to_jiffies(TAS_TIMEOUT_MS);
	do {
		if (req >= 0)
			spx5_rmw(HSCH_TAS_LIST_STATE_LIST_STATE_SET(req),
				 HSCH_TAS_LIST_STATE_LIST_STATE,
				 sparx5,
				 HSCH_TAS_LIST_STATE);

		state = sparx5_tas_list_state_get(sparx5);
		if (BIT(state) & done)
			return 0;

		cond_resched();
	} while (!time_after(jiffies, end));

	netdev_err(port->ndev, "Timeout switching TAS state %s in list %d\n",
		   sparx5_tas_state_to_str(state), list);

	return -ETIME;
}

/* Wait for hardware to bring the selected list back to ADMIN */
static int sparx5_tas_wait_admin(struct sparx5_port *port, int list)
{
	return sparx5_tas_list_state_wait(port, list, -1,
					  BIT(SPX5_TAS_STATE_ADMIN));
}

static int sparx5_tas_shutdown_pending(struct sparx5_port *port)
{
	struct sparx5 *sparx5 = port->sparx5;
	int i, list, state, err;

	for (i = 0; i < SPX5_TAS_ENTRIES_PER_PORT; i++) {
		list = sparx5_tas_list_index(port, i);
		spx5_rmw(HSCH_TAS_CFG_CTRL_LIST_NUM_SET(list),
			 HSCH_TAS_CFG_CTRL_LIST_NUM,
			 sparx5,
			 HSCH_TAS_CFG_CTRL);

		state = sparx5_tas_list_state_get(sparx5);
		if (state != SPX5_TAS_STATE_ADVANCING &&
		    state != SPX5_TAS_STATE_PENDING)
			continue;

		err = sparx5_tas_list_state_wait(port, list,
						 SPX5_TAS_STATE_ADMIN,
						 BIT(SPX5_TAS_STATE_ADMIN));
		if (err)
			return err;

		sparx5_tas_gcl_free(port, i);
	}

	return 0;
}

static int sparx5_tas_shutdown_operating(struct sparx5_port *port)
{
	struct sparx5 *sparx5 = port->sparx5;
	int i, list, state, err = 0;
	u32 sched;

	sched = sparx5_tas_scheduler_get(sparx5, port->portno);

	for (i = 0; i < SPX5_TAS_ENTRIES_PER_PORT; i++) {
		list = sparx5_tas_list_index(port, i);
		spx5_rmw(HSCH_TAS_CFG_CTRL_LIST_NUM_SET(list),
			 HSCH_TAS_CFG_CTRL_LIST_NUM,
			 sparx5,
			 HSCH_TAS_CFG_CTRL);

		/* A list being stopped still runs its gate operations until
		 * the end of its cycle, so wait for it before opening the
		 * gates.
		 */
		state = sparx5_tas_list_state_get(sparx5);
		if (state != SPX5_TAS_STATE_OPERATING &&
		    state != SPX5_TAS_STATE_TERMINATING)
			continue;

		if (state == SPX5_TAS_STATE_OPERATING &&
		    sparx5_tas_list_state_wait(port, list,
					       SPX5_TAS_STATE_TERMINATING,
					       BIT(SPX5_TAS_STATE_TERMINATING) |
					       BIT(SPX5_TAS_STATE_ADMIN))) {
			err = -ETIME;
			continue;
		}

		if (sparx5_tas_wait_admin(port, list)) {
			err = -ETIME;
			continue;
		}

		sparx5_tas_gcl_free(port, i);
	}

	/* Restore gate state to "all-queues-open". */

	/* Select port n on layer 2 of Hierarchical Scheduler */
	spx5_wr(HSCH_TAS_GATE_STATE_CTRL_HSCH_POS_SET(sched),
		sparx5,
		HSCH_TAS_GATE_STATE_CTRL);

	/* Set gate state to "all-queues-open" */
	spx5_wr(HSCH_TAS_GATE_STATE_TAS_GATE_STATE_SET(0xff),
		sparx5,
		HSCH_TAS_GATE_STATE);

	return err;
}

/* Find an ADMIN list for a new schedule, after stopping a pending one. An
 * operating list is returned in obsolete, to be stopped by the new list.
 */
static int sparx5_tas_list_find(struct sparx5_port *port, int *new,
				int *obsolete)
{
	int i, err, oper = -1, state_cnt[SPX5_NUM_TAS_STATE] = {0};
	struct sparx5 *sparx5 = port->sparx5;
	int state[SPX5_TAS_ENTRIES_PER_PORT];
	int list[SPX5_TAS_ENTRIES_PER_PORT];
	bool valid = false;

	err = sparx5_tas_shutdown_pending(port);
	if (err)
		return err;

	for (i = 0; i < SPX5_TAS_ENTRIES_PER_PORT; i++) {
		list[i] = sparx5_tas_list_index(port, i);
		spx5_rmw(HSCH_TAS_CFG_CTRL_LIST_NUM_SET(list[i]),
			 HSCH_TAS_CFG_CTRL_LIST_NUM,
			 sparx5,
			 HSCH_TAS_CFG_CTRL);

		state[i] = sparx5_tas_list_state_get(sparx5);

		if (state[i] == SPX5_TAS_STATE_TERMINATING) {
			err = sparx5_tas_wait_admin(port, list[i]);
			if (err)
				return err;
			state[i] = SPX5_TAS_STATE_ADMIN;
		}

		if (state[i] >= SPX5_NUM_TAS_STATE) {
			netdev_err(port->ndev,
				   "Invalid tas list state %u %u %d\n",
				   state[i], port->portno, i);
			return -EINVAL;
		}

		if (state[i] == SPX5_TAS_STATE_OPERATING)
			oper = list[i];

		state_cnt[state[i]]++;
	}

	if (state_cnt[SPX5_TAS_STATE_ADMIN] == 2)
		valid = true;

	if (state_cnt[SPX5_TAS_STATE_ADMIN] == 1 &&
	    state_cnt[SPX5_TAS_STATE_OPERATING] == 1)
		valid = true;

	if (!valid) {
		netdev_err(port->ndev,
			   "Invalid tas state combination: %d %d %d %d %d\n",
			   state_cnt[SPX5_TAS_STATE_ADMIN],
			   state_cnt[SPX5_TAS_STATE_ADVANCING],
			   state_cnt[SPX5_TAS_STATE_PENDING],
			   state_cnt[SPX5_TAS_STATE_OPERATING],
			   state_cnt[SPX5_TAS_STATE_TERMINATING]);

		return -EINVAL;
	}

	for (i = 0; i < SPX5_TAS_ENTRIES_PER_PORT; i++) {
		if (state[i] == SPX5_TAS_STATE_ADMIN) {
			*new = list[i];
			*obsolete = (oper == -1) ? *new : oper;
			return 0;
		}
	}

	return -EBUSY; /* No suitable list found */
}

/* Program the list and its GCL entries [base, base + num_entries) */
static void sparx5_tas_gcl_setup(struct sparx5_port *port, int list, int base,
				 struct tc_taprio_qopt_offload *qopt)
{
	struct sparx5 *sparx5 = port->sparx5;
	u32 sched, entry, prof;
	int i;

	sched = sparx5_tas_scheduler_get(sparx5, port->portno);
	/* Each port uses the TAS profile of its own number */
	prof = port->portno;

	/* Program only the chosen list; the sibling slot stays in ADMIN. */
	sparx5_tas_select_list(sparx5, list);

	spx5_rmw(HSCH_TAS_LIST_CFG_LIST_BASE_ADDR_SET(base),
		 HSCH_TAS_LIST_CFG_LIST_BASE_ADDR,
		 sparx5,
		 HSCH_TAS_LIST_CFG);

	if (is_sparx5(sparx5)) {
		spx5_rmw(HSCH_TAS_LIST_CFG_LIST_LENGTH_SET(qopt->num_entries),
			 HSCH_TAS_LIST_CFG_LIST_LENGTH,
			 sparx5,
			 HSCH_TAS_LIST_CFG);
	} else {
		/* Associate TAS list with physical port number and
		 * scheduler element.
		 */
		spx5_rmw(HSCH_TAS_LIST_CFG_LIST_PORT_NUM_SET(port->portno),
			 HSCH_TAS_LIST_CFG_LIST_PORT_NUM, sparx5,
			 HSCH_TAS_LIST_CFG);
		spx5_rmw(HSCH_TAS_LIST_CFG_LIST_HSCH_POS_SET(sched),
			 HSCH_TAS_LIST_CFG_LIST_HSCH_POS, sparx5,
			 HSCH_TAS_LIST_CFG);
	}

	for (i = 0; i < qopt->num_entries; i++) {
		u32 gcl_next = (i >= qopt->num_entries - 1) ? base :
							       base + i + 1;
		u32 gate_mask = qopt->entries[i].gate_mask;

		/* On sparx5 GCL_ENTRY_NUM is relative to the list's BASE_ADDR.
		 * On lan969x the GCL is a linked list and GCL_ENTRY_NUM is an
		 * absolute index into the pool, as is NEXT_GCL.
		 */
		entry = is_sparx5(sparx5) ? i : base + i;

		spx5_rmw(HSCH_TAS_CFG_CTRL_GCL_ENTRY_NUM_SET(entry),
			 HSCH_TAS_CFG_CTRL_GCL_ENTRY_NUM,
			 sparx5,
			 HSCH_TAS_CFG_CTRL);

		/* Set gate states for this GCL */
		spx5_rmw(HSCH_TAS_GCL_CTRL_CFG_GATE_STATE_SET(gate_mask),
			 HSCH_TAS_GCL_CTRL_CFG_GATE_STATE, sparx5,
			 HSCH_TAS_GCL_CTRL_CFG);

		if (is_sparx5(sparx5)) {
			spx5_rmw(HSCH_TAS_GCL_CTRL_CFG_HSCH_POS_SET(sched) |
				 HSCH_TAS_GCL_CTRL_CFG_PORT_PROFILE_SET(prof),
				 HSCH_TAS_GCL_CTRL_CFG_HSCH_POS |
				 HSCH_TAS_GCL_CTRL_CFG_PORT_PROFILE, sparx5,
				 HSCH_TAS_GCL_CTRL_CFG);
		} else {
			/* The GCL list is a linked list on lan969x */
			spx5_wr(HSCH_TAS_GCL_CTRL_CFG2_NEXT_GCL_SET(gcl_next),
				sparx5, HSCH_TAS_GCL_CTRL_CFG2);
		}

		spx5_wr(qopt->entries[i].interval,
			sparx5,
			HSCH_TAS_GCL_TIME_CFG);
	}
}

int sparx5_tas_enable(struct sparx5_port *port,
		      struct tc_taprio_qopt_offload *qopt)
{
	u64 cycle_time = qopt->cycle_time, calculated_cycle_time = 0;
	int i, err, base, slot, state, new_list = -1, obsolete = -1;
	struct sparx5 *sparx5 = port->sparx5;
	struct timespec64 ts;
	ktime_t base_time;
	unsigned long end;
	u32 startup, val;

	if (qopt->cycle_time_extension) {
		netdev_err(port->ndev,
			   "cycle_time_extension is not supported\n");
		return -EOPNOTSUPP;
	}

	/* The base time is taken from the PTP clock */
	if (!sparx5->ptp)
		return -EOPNOTSUPP;

	if (qopt->base_time < 0) {
		netdev_err(port->ndev, "Invalid base_time %lld\n",
			   qopt->base_time);
		return -EINVAL;
	}

	if (!sparx5_tas_port_has_lists(port)) {
		netdev_err(port->ndev, "No TAS lists left for this port\n");
		return -EOPNOTSUPP;
	}

	if (qopt->mqprio.qopt.num_tc != SPX5_PRIOS) {
		netdev_err(port->ndev, "Only %d traffic classes supported\n",
			   SPX5_PRIOS);
		return -EOPNOTSUPP;
	}

	for (i = 0; i < qopt->num_entries; i++) {
		if (qopt->entries[i].command != TC_TAPRIO_CMD_SET_GATES) {
			netdev_err(port->ndev, "Unsupported GCL command: %d\n",
				   qopt->entries[i].command);
			return -EOPNOTSUPP;
		}
	}

	mutex_lock(&sparx5->tas_lock);

	sparx5_tas_gcl_flush(sparx5);

	if (cycle_time > TAS_MAX_CYCLE_TIME_NS) {
		netdev_err(port->ndev, "Invalid cycle_time %llu\n", cycle_time);
		err = -EINVAL;
		goto out;
	}

	for (i = 0; i < qopt->num_entries; i++) {
		u32 interval = qopt->entries[i].interval;

		if (interval < TAS_MIN_CYCLE_TIME_NS) {
			netdev_err(port->ndev,
				   "Invalid minimum cycle time %u\n", interval);
			err = -EINVAL;
			goto out;
		}

		if (interval > TAS_MAX_CYCLE_TIME_NS) {
			netdev_err(port->ndev,
				   "Invalid maximum cycle time %u\n", interval);
			err = -EINVAL;
			goto out;
		}

		calculated_cycle_time += interval;
	}

	if (calculated_cycle_time > TAS_MAX_CYCLE_TIME_NS ||
	    cycle_time < calculated_cycle_time) {
		netdev_err(port->ndev, "Invalid cycle_time %llu\n", cycle_time);
		err = -EINVAL;
		goto out;
	}

	sparx5_new_base_time(sparx5, cycle_time, qopt->base_time, &base_time);

	/* Allocate before sparx5_tas_list_find() stops a pending schedule, so
	 * running out of GCL entries fails the replace without touching
	 * hardware.
	 */
	base = sparx5_tas_gcl_alloc(sparx5, qopt->num_entries);
	if (base < 0) {
		netdev_err(port->ndev, "No room for %zu GCL entries\n",
			   qopt->num_entries);
		err = base;
		goto out;
	}

	/* Select an appropriate entry to use */
	err = sparx5_tas_list_find(port, &new_list, &obsolete);
	if (err) {
		bitmap_clear(sparx5->tas_gcl_bitmap, base, qopt->num_entries);
		goto out;
	}

	/* The new list is in ADMIN, so a range it still owns can go */
	slot = new_list - sparx5_tas_list_index(port, 0);
	sparx5_tas_gcl_free(port, slot);

	sparx5_tas_gcl_setup(port, new_list, base, qopt);

	/* The obsolete list keeps its entries until it reaches ADMIN */
	port->tas_lists[slot].gcl_base = base;
	port->tas_lists[slot].gcl_count = qopt->num_entries;

	/* Setup TAS list */
	ts = ktime_to_timespec64(base_time);
	spx5_wr(HSCH_TAS_BASE_TIME_NSEC_BASE_TIME_NSEC_SET(ts.tv_nsec),
		sparx5,
		HSCH_TAS_BASE_TIME_NSEC);

	spx5_wr((ts.tv_sec & GENMASK(31, 0)),
		sparx5,
		HSCH_TAS_BASE_TIME_SEC_LSB);

	val = ts.tv_sec >> 32;
	spx5_wr(HSCH_TAS_BASE_TIME_SEC_MSB_BASE_TIME_SEC_MSB_SET(val),
		sparx5,
		HSCH_TAS_BASE_TIME_SEC_MSB);

	spx5_wr(cycle_time,
		sparx5,
		HSCH_TAS_CYCLE_TIME_CFG);

	/* Full write, to also clear a sticky STARTUP_ERROR from a previous use
	 * of this list.
	 */
	spx5_wr(HSCH_TAS_STARTUP_CFG_OBSOLETE_IDX_SET(obsolete) |
		HSCH_TAS_STARTUP_CFG_STARTUP_TIME_SET(TAS_STARTUP_TIME_DEFAULT),
		sparx5,
		HSCH_TAS_STARTUP_CFG);

	/* Start list processing */
	spx5_rmw(HSCH_TAS_LIST_STATE_LIST_STATE_SET(SPX5_TAS_STATE_ADVANCING),
		 HSCH_TAS_LIST_STATE_LIST_STATE,
		 sparx5,
		 HSCH_TAS_LIST_STATE);

	/* Do not wait forever for the startup error flag to be reported */
	end = jiffies + msecs_to_jiffies(TAS_TIMEOUT_MS);
	do {
		/* Read the state first, so an error that ended ADVANCING is
		 * already latched when STARTUP_CFG is read.
		 */
		val = spx5_rd(sparx5, HSCH_TAS_LIST_STATE);
		state = HSCH_TAS_LIST_STATE_LIST_STATE_GET(val);
		startup = spx5_rd(sparx5, HSCH_TAS_STARTUP_CFG);
		if (HSCH_TAS_STARTUP_CFG_STARTUP_ERROR_GET(startup) ||
		    state != SPX5_TAS_STATE_ADVANCING)
			break;

		cond_resched();
	} while (!time_after(jiffies, end));

	if (HSCH_TAS_STARTUP_CFG_STARTUP_ERROR_GET(startup)) {
		netdev_err(port->ndev, "TAS startup error in list %d\n",
			   new_list);
		err = -EIO;
	} else if (state == SPX5_TAS_STATE_ADVANCING) {
		netdev_err(port->ndev, "Timeout starting TAS list %d\n",
			   new_list);
		err = -ETIME;
	}

	/* taprio sends no DESTROY for a failed offload, so do not leave a
	 * list behind that may still start.
	 */
	if (err)
		sparx5_tas_shutdown_pending(port);

out:
	mutex_unlock(&sparx5->tas_lock);
	return err;
}

int sparx5_tas_disable(struct sparx5_port *port)
{
	int err, ret;

	/* Nothing was offloaded on a port without lists */
	if (!sparx5_tas_port_has_lists(port))
		return 0;

	mutex_lock(&port->sparx5->tas_lock);

	sparx5_tas_gcl_flush(port->sparx5);

	/* Stop the operating list and restore the gates even if a pending list
	 * could not be stopped, as taprio does not retry a failed destroy.
	 */
	err = sparx5_tas_shutdown_pending(port);
	ret = sparx5_tas_shutdown_operating(port);
	if (!err)
		err = ret;

	mutex_unlock(&port->sparx5->tas_lock);

	return err;
}

int sparx5_tas_init(struct sparx5 *sparx5)
{
	const struct sparx5_consts *consts = sparx5->data->consts;
	int i, num_ports, list_num_max;

	/* There are only 128 (sparx5) / 60 (lan969x) TAS lists, not enough
	 * for the whole port range. LIST_NUM_MAX is the highest list index
	 * processed, and the datasheet gives 24 as its minimum value.
	 */
	num_ports = consts->n_ports;
	list_num_max = max(sparx5_tas_num_lists(sparx5) - 1, 24);

	sparx5->tas_gcl_bitmap = devm_bitmap_zalloc(sparx5->dev,
						    consts->n_tas_gcl,
						    GFP_KERNEL);
	if (!sparx5->tas_gcl_bitmap)
		return -ENOMEM;

	/* Unwritten NEXT_GCL fields read as 0, so no list may own entry 0 */
	bitmap_set(sparx5->tas_gcl_bitmap, 0, 1);

	mutex_init(&sparx5->tas_lock);

	/* Apply the guard band to gate closes of all traffic classes, not only
	 * the classes not marked as scheduled in SCH_TRAFFIC_QUEUES.
	 */
	spx5_rmw(HSCH_TAS_CFG_CTRL_LIST_NUM_MAX_SET(list_num_max) |
		 HSCH_TAS_CFG_CTRL_ALWAYS_GUARD_BAND_SCH_Q_SET(1),
		 HSCH_TAS_CFG_CTRL_LIST_NUM_MAX |
		 HSCH_TAS_CFG_CTRL_ALWAYS_GUARD_BAND_SCH_Q,
		 sparx5,
		 HSCH_TAS_CFG_CTRL);

	/* Associate profile with port (profile idx = port on lan969x) */
	if (is_sparx5(sparx5)) {
		for (i = 0; i < num_ports; i++) {
			if (!sparx5->ports[i])
				continue;

			spx5_rmw(HSCH_TAS_PROFILE_CONFIG_PORT_NUM_SET(i),
				 HSCH_TAS_PROFILE_CONFIG_PORT_NUM,
				 sparx5,
				 HSCH_TAS_PROFILE_CONFIG(i));
		}
	}

	return 0;
}

void sparx5_tas_speed(struct sparx5_port *port, int speed)
{
	struct sparx5 *sparx5 = port->sparx5;
	u8 spd;

	switch (speed) {
	case SPEED_10:
		spd = TAS_SPEED_10;
		break;
	case SPEED_100:
		spd = TAS_SPEED_100;
		break;
	case SPEED_1000:
		spd = TAS_SPEED_1000;
		break;
	case SPEED_2500:
		spd = TAS_SPEED_2500;
		break;
	case SPEED_5000:
		spd = TAS_SPEED_5000;
		break;
	case SPEED_10000:
		spd = TAS_SPEED_10000;
		break;
	case SPEED_25000:
		spd = TAS_SPEED_25000;
		break;
	default:
		netdev_err(port->ndev, "TAS: Unsupported speed: %d\n", speed);
		return;
	}

	spx5_rmw(HSCH_TAS_PROFILE_CONFIG_LINK_SPEED_SET(spd),
		 HSCH_TAS_PROFILE_CONFIG_LINK_SPEED,
		 sparx5,
		 HSCH_TAS_PROFILE_CONFIG(port->portno));
}
