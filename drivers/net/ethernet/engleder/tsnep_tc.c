// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2021 Gerhard Engleder <gerhard@engleder-embedded.com> */

#include "tsnep.h"

#include <net/pkt_sched.h>

/* save one operation at the end for additional operation at list change */
#define TSNEP_MAX_GCL_NUM (TSNEP_GCL_COUNT - 1)

static int tsnep_validate_gcl(struct tc_taprio_qopt_offload *qopt)
{
	int i;
	u64 cycle_time;

	if (!qopt->cycle_time)
		return -ERANGE;
	if (qopt->num_entries > TSNEP_MAX_GCL_NUM)
		return -EINVAL;
	cycle_time = 0;
	for (i = 0; i < qopt->num_entries; i++) {
		if (qopt->entries[i].command != TC_TAPRIO_CMD_SET_GATES)
			return -EINVAL;
		if (qopt->entries[i].gate_mask & ~TSNEP_GCL_MASK)
			return -EINVAL;
		if (qopt->entries[i].interval < TSNEP_GCL_MIN_INTERVAL)
			return -EINVAL;
		cycle_time += qopt->entries[i].interval;
	}
	if (qopt->cycle_time != cycle_time)
		return -EINVAL;
	if (qopt->cycle_time_extension >= qopt->cycle_time)
		return -EINVAL;

	return 0;
}

static void tsnep_write_gcl_operation(struct tsnep_gcl *gcl, int index,
				      u32 properties, u32 interval, bool flush)
{
	void __iomem *addr = gcl->addr +
			     sizeof(struct tsnep_gcl_operation) * index;

	gcl->operation[index].properties = properties;
	gcl->operation[index].interval = interval;

	iowrite32(properties, addr);
	iowrite32(interval, addr + sizeof(u32));

	if (flush) {
		/* flush write with read access */
		ioread32(addr);
	}
}

static u64 tsnep_change_duration(struct tsnep_gcl *gcl, int index)
{
	u64 duration;
	int count;

	/* change needs to be triggered one or two operations before start of
	 * new gate control list
	 * - change is triggered at start of operation (minimum one operation)
	 * - operation with adjusted interval is inserted on demand to exactly
	 *   meet the start of the new gate control list (optional)
	 *
	 * additionally properties are read directly after start of previous
	 * operation
	 *
	 * therefore, three operations needs to be considered for the limit
	 */
	duration = 0;
	count = 3;
	while (count) {
		duration += gcl->operation[index].interval;

		index--;
		if (index < 0)
			index = gcl->count - 1;

		count--;
	}

	return duration;
}

static void tsnep_write_gcl(struct tsnep_gcl *gcl,
			    struct tc_taprio_qopt_offload *qopt)
{
	int i;
	u32 properties;
	u64 extend;
	u64 cut;

	gcl->base_time = ktime_to_ns(qopt->base_time);
	gcl->cycle_time = qopt->cycle_time;
	gcl->cycle_time_extension = qopt->cycle_time_extension;

	for (i = 0; i < qopt->num_entries; i++) {
		properties = qopt->entries[i].gate_mask;
		if (i == (qopt->num_entries - 1))
			properties |= TSNEP_GCL_LAST;

		tsnep_write_gcl_operation(gcl, i, properties,
					  qopt->entries[i].interval, true);
	}
	gcl->count = qopt->num_entries;

	/* calculate change limit; i.e., the time needed between enable and
	 * start of new gate control list
	 */

	/* case 1: extend cycle time for change
	 * - change duration of last operation
	 * - cycle time extension
	 */
	extend = tsnep_change_duration(gcl, gcl->count - 1);
	extend += gcl->cycle_time_extension;

	/* case 2: cut cycle time for change
	 * - maximum change duration
	 */
	cut = 0;
	for (i = 0; i < gcl->count; i++)
		cut = max(cut, tsnep_change_duration(gcl, i));

	/* use maximum, because the actual case (extend or cut) can be
	 * determined only after limit is known (chicken-and-egg problem)
	 */
	gcl->change_limit = max(extend, cut);
}

static u64 tsnep_gcl_start_after(struct tsnep_gcl *gcl, u64 limit)
{
	u64 start = gcl->base_time;
	u64 n;

	if (start <= limit) {
		n = div64_u64(limit - start, gcl->cycle_time);
		start += (n + 1) * gcl->cycle_time;
	}

	return start;
}

static u64 tsnep_gcl_start_before(struct tsnep_gcl *gcl, u64 limit)
{
	u64 start = gcl->base_time;
	u64 n;

	n = div64_u64(limit - start, gcl->cycle_time);
	start += n * gcl->cycle_time;
	if (start == limit)
		start -= gcl->cycle_time;

	return start;
}

static u64 tsnep_set_gcl_change(struct tsnep_gcl *gcl, int index, u64 change,
				bool insert)
{
	/* previous operation triggers change and properties are evaluated at
	 * start of operation
	 */
	if (index == 0)
		index = gcl->count - 1;
	else
		index = index - 1;
	change -= gcl->operation[index].interval;

	/* optionally change to new list with additional operation in between */
	if (insert) {
		void __iomem *addr = gcl->addr +
				     sizeof(struct tsnep_gcl_operation) * index;

		gcl->operation[index].properties |= TSNEP_GCL_INSERT;
		iowrite32(gcl->operation[index].properties, addr);
	}

	return change;
}

static void tsnep_clean_gcl(struct tsnep_gcl *gcl)
{
	int i;
	u32 mask = TSNEP_GCL_LAST | TSNEP_GCL_MASK;
	void __iomem *addr;

	/* search for insert operation and reset properties */
	for (i = 0; i < gcl->count; i++) {
		if (gcl->operation[i].properties & ~mask) {
			addr = gcl->addr +
			       sizeof(struct tsnep_gcl_operation) * i;

			gcl->operation[i].properties &= mask;
			iowrite32(gcl->operation[i].properties, addr);

			break;
		}
	}
}

static u64 tsnep_insert_gcl_operation(struct tsnep_gcl *gcl, int ref,
				      u64 change, u32 interval)
{
	u32 properties;

	properties = gcl->operation[ref].properties & TSNEP_GCL_MASK;
	/* change to new list directly after inserted operation */
	properties |= TSNEP_GCL_CHANGE;

	/* last operation of list is reserved to insert operation */
	tsnep_write_gcl_operation(gcl, TSNEP_GCL_COUNT - 1, properties,
				  interval, false);

	return tsnep_set_gcl_change(gcl, ref, change, true);
}

static u64 tsnep_extend_gcl(struct tsnep_gcl *gcl, u64 start, u32 extension)
{
	int ref = gcl->count - 1;
	u32 interval = gcl->operation[ref].interval + extension;

	start -= gcl->operation[ref].interval;

	return tsnep_insert_gcl_operation(gcl, ref, start, interval);
}

static u64 tsnep_cut_gcl(struct tsnep_gcl *gcl, u64 start, u64 cycle_time)
{
	u64 sum = 0;
	int i;

	/* find operation which shall be cutted */
	for (i = 0; i < gcl->count; i++) {
		u64 sum_tmp = sum + gcl->operation[i].interval;
		u64 interval;

		/* sum up operations as long as cycle time is not exceeded */
		if (sum_tmp > cycle_time)
			break;

		/* remaining interval must be big enough for hardware */
		interval = cycle_time - sum_tmp;
		if (interval > 0 && interval < TSNEP_GCL_MIN_INTERVAL)
			break;

		sum = sum_tmp;
	}
	if (sum == cycle_time) {
		/* no need to cut operation itself or whole cycle
		 * => change exactly at operation
		 */
		return tsnep_set_gcl_change(gcl, i, start + sum, false);
	}
	return tsnep_insert_gcl_operation(gcl, i, start + sum,
					  cycle_time - sum);
}

static int tsnep_enable_gcl(struct tsnep_adapter *adapter,
			    struct tsnep_gcl *gcl, struct tsnep_gcl *curr)
{
	u64 system_time;
	u64 timeout;
	u64 limit;

	/* estimate timeout limit after timeout enable, actually timeout limit
	 * in hardware will be earlier than estimate so we are on the safe side
	 */
	tsnep_get_system_time(adapter, &system_time);
	timeout = system_time + TSNEP_GC_TIMEOUT;

	if (curr)
		limit = timeout + curr->change_limit;
	else
		limit = timeout;

	gcl->start_time = tsnep_gcl_start_after(gcl, limit);

	/* gate control time register is only 32bit => time shall be in the near
	 * future (no driver support for far future implemented)
	 */
	if ((gcl->start_time - system_time) >= U32_MAX)
		return -EAGAIN;

	if (curr) {
		/* change gate control list */
		u64 last;
		u64 change;

		last = tsnep_gcl_start_before(curr, gcl->start_time);
		if ((last + curr->cycle_time) == gcl->start_time)
			change = tsnep_cut_gcl(curr, last,
					       gcl->start_time - last);
		else if (((gcl->start_time - last) <=
			  curr->cycle_time_extension) ||
			 ((gcl->start_time - last) <= TSNEP_GCL_MIN_INTERVAL))
			change = tsnep_extend_gcl(curr, last,
						  gcl->start_time - last);
		else
			change = tsnep_cut_gcl(curr, last,
					       gcl->start_time - last);

		WARN_ON(change <= timeout);
		gcl->change = true;
		iowrite32(change & 0xFFFFFFFF, adapter->addr + TSNEP_GC_CHANGE);
	} else {
		/* start gate control list */
		WARN_ON(gcl->start_time <= timeout);
		gcl->change = false;
		iowrite32(gcl->start_time & 0xFFFFFFFF,
			  adapter->addr + TSNEP_GC_TIME);
	}

	return 0;
}

static int tsnep_taprio(struct tsnep_adapter *adapter,
			struct tc_taprio_qopt_offload *qopt)
{
	struct tsnep_gcl *gcl;
	struct tsnep_gcl *curr;
	int retval;

	if (!adapter->gate_control)
		return -EOPNOTSUPP;

	if (qopt->cmd == TAPRIO_CMD_DESTROY) {
		/* disable gate control if active */
		mutex_lock(&adapter->gate_control_lock);

		if (adapter->gate_control_active) {
			iowrite8(TSNEP_GC_DISABLE, adapter->addr + TSNEP_GC);
			adapter->gate_control_active = false;
		}

		mutex_unlock(&adapter->gate_control_lock);

		return 0;
	} else if (qopt->cmd != TAPRIO_CMD_REPLACE) {
		return -EOPNOTSUPP;
	}

	retval = tsnep_validate_gcl(qopt);
	if (retval)
		return retval;

	mutex_lock(&adapter->gate_control_lock);

	gcl = &adapter->gcl[adapter->next_gcl];
	tsnep_write_gcl(gcl, qopt);

	/* select current gate control list if active */
	if (adapter->gate_control_active) {
		if (adapter->next_gcl == 0)
			curr = &adapter->gcl[1];
		else
			curr = &adapter->gcl[0];
	} else {
		curr = NULL;
	}

	for (;;) {
		/* start timeout which discards late enable, this helps ensuring
		 * that start/change time are in the future at enable
		 */
		iowrite8(TSNEP_GC_ENABLE_TIMEOUT, adapter->addr + TSNEP_GC);

		retval = tsnep_enable_gcl(adapter, gcl, curr);
		if (retval) {
			mutex_unlock(&adapter->gate_control_lock);

			return retval;
		}

		/* enable gate control list */
		if (adapter->next_gcl == 0)
			iowrite8(TSNEP_GC_ENABLE_A, adapter->addr + TSNEP_GC);
		else
			iowrite8(TSNEP_GC_ENABLE_B, adapter->addr + TSNEP_GC);

		/* done if timeout did not happen */
		if (!(ioread32(adapter->addr + TSNEP_GC) &
		      TSNEP_GC_TIMEOUT_SIGNAL))
			break;

		/* timeout is acknowledged with any enable */
		iowrite8(TSNEP_GC_ENABLE_A, adapter->addr + TSNEP_GC);

		if (curr)
			tsnep_clean_gcl(curr);

		/* retry because of timeout */
	}

	adapter->gate_control_active = true;

	if (adapter->next_gcl == 0)
		adapter->next_gcl = 1;
	else
		adapter->next_gcl = 0;

	mutex_unlock(&adapter->gate_control_lock);

	return 0;
}

static int tsnep_tc_query_caps(struct tsnep_adapter *adapter,
			       struct tc_query_caps_base *base)
{
	switch (base->type) {
	case TC_SETUP_QDISC_TAPRIO: {
		struct tc_taprio_caps *caps = base->caps;

		if (!adapter->gate_control)
			return -EOPNOTSUPP;

		caps->gate_mask_per_txq = true;

		return 0;
	}
	default:
		return -EOPNOTSUPP;
	}
}

int tsnep_tc_setup(struct net_device *netdev, enum tc_setup_type type,
		   void *type_data)
{
	struct tsnep_adapter *adapter = netdev_priv(netdev);

	switch (type) {
	case TC_QUERY_CAPS:
		return tsnep_tc_query_caps(adapter, type_data);
	case TC_SETUP_QDISC_TAPRIO:
		return tsnep_taprio(adapter, type_data);
	default:
		return -EOPNOTSUPP;
	}
}

int tsnep_tc_init(struct tsnep_adapter *adapter)
{
	if (!adapter->gate_control)
		return 0;

	/* open all gates */
	iowrite8(TSNEP_GC_DISABLE, adapter->addr + TSNEP_GC);
	iowrite32(TSNEP_GC_OPEN | TSNEP_GC_NEXT_OPEN, adapter->addr + TSNEP_GC);

	adapter->gcl[0].addr = adapter->addr + TSNEP_GCL_A;
	adapter->gcl[1].addr = adapter->addr + TSNEP_GCL_B;

	return 0;
}

void tsnep_tc_cleanup(struct tsnep_adapter *adapter)
{
	if (!adapter->gate_control)
		return;

	if (adapter->gate_control_active) {
		iowrite8(TSNEP_GC_DISABLE, adapter->addr + TSNEP_GC);
		adapter->gate_control_active = false;
	}
}
