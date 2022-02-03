// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2021 Gerhard Engleder <gerhard@engleder-embedded.com> */

#include "tsnep.h"

#include <net/pkt_sched.h>

enum tsnep_test {
	TSNEP_TEST_ENABLE = 0,
	TSNEP_TEST_TAPRIO,
	TSNEP_TEST_TAPRIO_CHANGE,
	TSNEP_TEST_TAPRIO_EXTENSION,
};

static const char tsnep_test_strings[][ETH_GSTRING_LEN] = {
	"Enable timeout        (offline)",
	"TAPRIO                (offline)",
	"TAPRIO change         (offline)",
	"TAPRIO extension      (offline)",
};

#define TSNEP_TEST_COUNT (sizeof(tsnep_test_strings) / ETH_GSTRING_LEN)

static bool enable_gc_timeout(struct tsnep_adapter *adapter)
{
	iowrite8(TSNEP_GC_ENABLE_TIMEOUT, adapter->addr + TSNEP_GC);
	if (!(ioread32(adapter->addr + TSNEP_GC) & TSNEP_GC_TIMEOUT_ACTIVE))
		return false;

	return true;
}

static bool gc_timeout_signaled(struct tsnep_adapter *adapter)
{
	if (ioread32(adapter->addr + TSNEP_GC) & TSNEP_GC_TIMEOUT_SIGNAL)
		return true;

	return false;
}

static bool ack_gc_timeout(struct tsnep_adapter *adapter)
{
	iowrite8(TSNEP_GC_ENABLE_TIMEOUT, adapter->addr + TSNEP_GC);
	if (ioread32(adapter->addr + TSNEP_GC) &
	    (TSNEP_GC_TIMEOUT_ACTIVE | TSNEP_GC_TIMEOUT_SIGNAL))
		return false;
	return true;
}

static bool enable_gc(struct tsnep_adapter *adapter, bool a)
{
	u8 enable;
	u8 active;

	if (a) {
		enable = TSNEP_GC_ENABLE_A;
		active = TSNEP_GC_ACTIVE_A;
	} else {
		enable = TSNEP_GC_ENABLE_B;
		active = TSNEP_GC_ACTIVE_B;
	}

	iowrite8(enable, adapter->addr + TSNEP_GC);
	if (!(ioread32(adapter->addr + TSNEP_GC) & active))
		return false;

	return true;
}

static bool disable_gc(struct tsnep_adapter *adapter)
{
	iowrite8(TSNEP_GC_DISABLE, adapter->addr + TSNEP_GC);
	if (ioread32(adapter->addr + TSNEP_GC) &
	    (TSNEP_GC_ACTIVE_A | TSNEP_GC_ACTIVE_B))
		return false;

	return true;
}

static bool gc_delayed_enable(struct tsnep_adapter *adapter, bool a, int delay)
{
	u64 before, after;
	u32 time;
	bool enabled;

	if (!disable_gc(adapter))
		return false;

	before = ktime_get_ns();

	if (!enable_gc_timeout(adapter))
		return false;

	/* for start time after timeout, the timeout can guarantee, that enable
	 * is blocked if too late
	 */
	time = ioread32(adapter->addr + ECM_SYSTEM_TIME_LOW);
	time += TSNEP_GC_TIMEOUT;
	iowrite32(time, adapter->addr + TSNEP_GC_TIME);

	ndelay(delay);

	enabled = enable_gc(adapter, a);
	after = ktime_get_ns();

	if (delay > TSNEP_GC_TIMEOUT) {
		/* timeout must have blocked enable */
		if (enabled)
			return false;
	} else if ((after - before) < TSNEP_GC_TIMEOUT * 14 / 16) {
		/* timeout must not have blocked enable */
		if (!enabled)
			return false;
	}

	if (enabled) {
		if (gc_timeout_signaled(adapter))
			return false;
	} else {
		if (!gc_timeout_signaled(adapter))
			return false;
		if (!ack_gc_timeout(adapter))
			return false;
	}

	if (!disable_gc(adapter))
		return false;

	return true;
}

static bool tsnep_test_gc_enable(struct tsnep_adapter *adapter)
{
	int i;

	iowrite32(0x80000001, adapter->addr + TSNEP_GCL_A + 0);
	iowrite32(100000, adapter->addr + TSNEP_GCL_A + 4);

	for (i = 0; i < 200000; i += 100) {
		if (!gc_delayed_enable(adapter, true, i))
			return false;
	}

	iowrite32(0x80000001, adapter->addr + TSNEP_GCL_B + 0);
	iowrite32(100000, adapter->addr + TSNEP_GCL_B + 4);

	for (i = 0; i < 200000; i += 100) {
		if (!gc_delayed_enable(adapter, false, i))
			return false;
	}

	return true;
}

static void delay_base_time(struct tsnep_adapter *adapter,
			    struct tc_taprio_qopt_offload *qopt, s64 ms)
{
	u64 system_time;
	u64 base_time = ktime_to_ns(qopt->base_time);
	u64 n;

	tsnep_get_system_time(adapter, &system_time);
	system_time += ms * 1000000;
	n = div64_u64(system_time - base_time, qopt->cycle_time);

	qopt->base_time = ktime_add_ns(qopt->base_time,
				       (n + 1) * qopt->cycle_time);
}

static void get_gate_state(struct tsnep_adapter *adapter, u32 *gc, u32 *gc_time,
			   u64 *system_time)
{
	u32 time_high_before;
	u32 time_low;
	u32 time_high;
	u32 gc_time_before;

	time_high = ioread32(adapter->addr + ECM_SYSTEM_TIME_HIGH);
	*gc_time = ioread32(adapter->addr + TSNEP_GC_TIME);
	do {
		time_low = ioread32(adapter->addr + ECM_SYSTEM_TIME_LOW);
		*gc = ioread32(adapter->addr + TSNEP_GC);

		gc_time_before = *gc_time;
		*gc_time = ioread32(adapter->addr + TSNEP_GC_TIME);
		time_high_before = time_high;
		time_high = ioread32(adapter->addr + ECM_SYSTEM_TIME_HIGH);
	} while ((time_high != time_high_before) ||
		 (*gc_time != gc_time_before));

	*system_time = (((u64)time_high) << 32) | ((u64)time_low);
}

static int get_operation(struct tsnep_gcl *gcl, u64 system_time, u64 *next)
{
	u64 n = div64_u64(system_time - gcl->base_time, gcl->cycle_time);
	u64 cycle_start = gcl->base_time + gcl->cycle_time * n;
	int i;

	*next = cycle_start;
	for (i = 0; i < gcl->count; i++) {
		*next += gcl->operation[i].interval;
		if (*next > system_time)
			break;
	}

	return i;
}

static bool check_gate(struct tsnep_adapter *adapter)
{
	u32 gc_time;
	u32 gc;
	u64 system_time;
	struct tsnep_gcl *curr;
	struct tsnep_gcl *prev;
	u64 next_time;
	u8 gate_open;
	u8 next_gate_open;

	get_gate_state(adapter, &gc, &gc_time, &system_time);

	if (gc & TSNEP_GC_ACTIVE_A) {
		curr = &adapter->gcl[0];
		prev = &adapter->gcl[1];
	} else if (gc & TSNEP_GC_ACTIVE_B) {
		curr = &adapter->gcl[1];
		prev = &adapter->gcl[0];
	} else {
		return false;
	}
	if (curr->start_time <= system_time) {
		/* GCL is already active */
		int index;

		index = get_operation(curr, system_time, &next_time);
		gate_open = curr->operation[index].properties & TSNEP_GCL_MASK;
		if (index == curr->count - 1)
			index = 0;
		else
			index++;
		next_gate_open =
			curr->operation[index].properties & TSNEP_GCL_MASK;
	} else if (curr->change) {
		/* operation of previous GCL is active */
		int index;
		u64 start_before;
		u64 n;

		index = get_operation(prev, system_time, &next_time);
		next_time = curr->start_time;
		start_before = prev->base_time;
		n = div64_u64(curr->start_time - start_before,
			      prev->cycle_time);
		start_before += n * prev->cycle_time;
		if (curr->start_time == start_before)
			start_before -= prev->cycle_time;
		if (((start_before + prev->cycle_time_extension) >=
		     curr->start_time) &&
		    (curr->start_time - prev->cycle_time_extension <=
		     system_time)) {
			/* extend */
			index = prev->count - 1;
		}
		gate_open = prev->operation[index].properties & TSNEP_GCL_MASK;
		next_gate_open =
			curr->operation[0].properties & TSNEP_GCL_MASK;
	} else {
		/* GCL is waiting for start */
		next_time = curr->start_time;
		gate_open = 0xFF;
		next_gate_open = curr->operation[0].properties & TSNEP_GCL_MASK;
	}

	if (gc_time != (next_time & 0xFFFFFFFF)) {
		dev_err(&adapter->pdev->dev, "gate control time 0x%x!=0x%llx\n",
			gc_time, next_time);
		return false;
	}
	if (((gc & TSNEP_GC_OPEN) >> TSNEP_GC_OPEN_SHIFT) != gate_open) {
		dev_err(&adapter->pdev->dev,
			"gate control open 0x%02x!=0x%02x\n",
			((gc & TSNEP_GC_OPEN) >> TSNEP_GC_OPEN_SHIFT),
			gate_open);
		return false;
	}
	if (((gc & TSNEP_GC_NEXT_OPEN) >> TSNEP_GC_NEXT_OPEN_SHIFT) !=
	    next_gate_open) {
		dev_err(&adapter->pdev->dev,
			"gate control next open 0x%02x!=0x%02x\n",
			((gc & TSNEP_GC_NEXT_OPEN) >> TSNEP_GC_NEXT_OPEN_SHIFT),
			next_gate_open);
		return false;
	}

	return true;
}

static bool check_gate_duration(struct tsnep_adapter *adapter, s64 ms)
{
	ktime_t start = ktime_get();

	do {
		if (!check_gate(adapter))
			return false;
	} while (ktime_ms_delta(ktime_get(), start) < ms);

	return true;
}

static bool enable_check_taprio(struct tsnep_adapter *adapter,
				struct tc_taprio_qopt_offload *qopt, s64 ms)
{
	int retval;

	retval = tsnep_tc_setup(adapter->netdev, TC_SETUP_QDISC_TAPRIO, qopt);
	if (retval)
		return false;

	if (!check_gate_duration(adapter, ms))
		return false;

	return true;
}

static bool disable_taprio(struct tsnep_adapter *adapter)
{
	struct tc_taprio_qopt_offload qopt;
	int retval;

	memset(&qopt, 0, sizeof(qopt));
	qopt.enable = 0;
	retval = tsnep_tc_setup(adapter->netdev, TC_SETUP_QDISC_TAPRIO, &qopt);
	if (retval)
		return false;

	return true;
}

static bool run_taprio(struct tsnep_adapter *adapter,
		       struct tc_taprio_qopt_offload *qopt, s64 ms)
{
	if (!enable_check_taprio(adapter, qopt, ms))
		return false;

	if (!disable_taprio(adapter))
		return false;

	return true;
}

static bool tsnep_test_taprio(struct tsnep_adapter *adapter)
{
	struct tc_taprio_qopt_offload *qopt;
	int i;

	qopt = kzalloc(struct_size(qopt, entries, 255), GFP_KERNEL);
	if (!qopt)
		return false;
	for (i = 0; i < 255; i++)
		qopt->entries[i].command = TC_TAPRIO_CMD_SET_GATES;

	qopt->enable = 1;
	qopt->base_time = ktime_set(0, 0);
	qopt->cycle_time = 1500000;
	qopt->cycle_time_extension = 0;
	qopt->entries[0].gate_mask = 0x02;
	qopt->entries[0].interval = 200000;
	qopt->entries[1].gate_mask = 0x03;
	qopt->entries[1].interval = 800000;
	qopt->entries[2].gate_mask = 0x07;
	qopt->entries[2].interval = 240000;
	qopt->entries[3].gate_mask = 0x01;
	qopt->entries[3].interval = 80000;
	qopt->entries[4].gate_mask = 0x04;
	qopt->entries[4].interval = 70000;
	qopt->entries[5].gate_mask = 0x06;
	qopt->entries[5].interval = 60000;
	qopt->entries[6].gate_mask = 0x0F;
	qopt->entries[6].interval = 50000;
	qopt->num_entries = 7;
	if (!run_taprio(adapter, qopt, 100))
		goto failed;

	qopt->enable = 1;
	qopt->base_time = ktime_set(0, 0);
	qopt->cycle_time = 411854;
	qopt->cycle_time_extension = 0;
	qopt->entries[0].gate_mask = 0x17;
	qopt->entries[0].interval = 23842;
	qopt->entries[1].gate_mask = 0x16;
	qopt->entries[1].interval = 13482;
	qopt->entries[2].gate_mask = 0x15;
	qopt->entries[2].interval = 49428;
	qopt->entries[3].gate_mask = 0x14;
	qopt->entries[3].interval = 38189;
	qopt->entries[4].gate_mask = 0x13;
	qopt->entries[4].interval = 92321;
	qopt->entries[5].gate_mask = 0x12;
	qopt->entries[5].interval = 71239;
	qopt->entries[6].gate_mask = 0x11;
	qopt->entries[6].interval = 69932;
	qopt->entries[7].gate_mask = 0x10;
	qopt->entries[7].interval = 53421;
	qopt->num_entries = 8;
	if (!run_taprio(adapter, qopt, 100))
		goto failed;

	qopt->enable = 1;
	qopt->base_time = ktime_set(0, 0);
	delay_base_time(adapter, qopt, 12);
	qopt->cycle_time = 125000;
	qopt->cycle_time_extension = 0;
	qopt->entries[0].gate_mask = 0x27;
	qopt->entries[0].interval = 15000;
	qopt->entries[1].gate_mask = 0x26;
	qopt->entries[1].interval = 15000;
	qopt->entries[2].gate_mask = 0x25;
	qopt->entries[2].interval = 12500;
	qopt->entries[3].gate_mask = 0x24;
	qopt->entries[3].interval = 17500;
	qopt->entries[4].gate_mask = 0x23;
	qopt->entries[4].interval = 10000;
	qopt->entries[5].gate_mask = 0x22;
	qopt->entries[5].interval = 11000;
	qopt->entries[6].gate_mask = 0x21;
	qopt->entries[6].interval = 9000;
	qopt->entries[7].gate_mask = 0x20;
	qopt->entries[7].interval = 10000;
	qopt->entries[8].gate_mask = 0x20;
	qopt->entries[8].interval = 12500;
	qopt->entries[9].gate_mask = 0x20;
	qopt->entries[9].interval = 12500;
	qopt->num_entries = 10;
	if (!run_taprio(adapter, qopt, 100))
		goto failed;

	kfree(qopt);

	return true;

failed:
	disable_taprio(adapter);
	kfree(qopt);

	return false;
}

static bool tsnep_test_taprio_change(struct tsnep_adapter *adapter)
{
	struct tc_taprio_qopt_offload *qopt;
	int i;

	qopt = kzalloc(struct_size(qopt, entries, 255), GFP_KERNEL);
	if (!qopt)
		return false;
	for (i = 0; i < 255; i++)
		qopt->entries[i].command = TC_TAPRIO_CMD_SET_GATES;

	qopt->enable = 1;
	qopt->base_time = ktime_set(0, 0);
	qopt->cycle_time = 100000;
	qopt->cycle_time_extension = 0;
	qopt->entries[0].gate_mask = 0x30;
	qopt->entries[0].interval = 20000;
	qopt->entries[1].gate_mask = 0x31;
	qopt->entries[1].interval = 80000;
	qopt->num_entries = 2;
	if (!enable_check_taprio(adapter, qopt, 100))
		goto failed;

	/* change to identical */
	if (!enable_check_taprio(adapter, qopt, 100))
		goto failed;
	delay_base_time(adapter, qopt, 17);
	if (!enable_check_taprio(adapter, qopt, 100))
		goto failed;

	/* change to same cycle time */
	qopt->base_time = ktime_set(0, 0);
	qopt->entries[0].gate_mask = 0x42;
	qopt->entries[1].gate_mask = 0x43;
	delay_base_time(adapter, qopt, 2);
	if (!enable_check_taprio(adapter, qopt, 100))
		goto failed;
	qopt->base_time = ktime_set(0, 0);
	qopt->entries[0].gate_mask = 0x54;
	qopt->entries[0].interval = 33333;
	qopt->entries[1].gate_mask = 0x55;
	qopt->entries[1].interval = 66667;
	delay_base_time(adapter, qopt, 23);
	if (!enable_check_taprio(adapter, qopt, 100))
		goto failed;
	qopt->base_time = ktime_set(0, 0);
	qopt->entries[0].gate_mask = 0x66;
	qopt->entries[0].interval = 50000;
	qopt->entries[1].gate_mask = 0x67;
	qopt->entries[1].interval = 25000;
	qopt->entries[2].gate_mask = 0x68;
	qopt->entries[2].interval = 25000;
	qopt->num_entries = 3;
	delay_base_time(adapter, qopt, 11);
	if (!enable_check_taprio(adapter, qopt, 100))
		goto failed;

	/* change to multiple of cycle time */
	qopt->base_time = ktime_set(0, 0);
	qopt->cycle_time = 200000;
	qopt->entries[0].gate_mask = 0x79;
	qopt->entries[0].interval = 50000;
	qopt->entries[1].gate_mask = 0x7A;
	qopt->entries[1].interval = 150000;
	qopt->num_entries = 2;
	delay_base_time(adapter, qopt, 11);
	if (!enable_check_taprio(adapter, qopt, 100))
		goto failed;
	qopt->base_time = ktime_set(0, 0);
	qopt->cycle_time = 1000000;
	qopt->entries[0].gate_mask = 0x7B;
	qopt->entries[0].interval = 125000;
	qopt->entries[1].gate_mask = 0x7C;
	qopt->entries[1].interval = 250000;
	qopt->entries[2].gate_mask = 0x7D;
	qopt->entries[2].interval = 375000;
	qopt->entries[3].gate_mask = 0x7E;
	qopt->entries[3].interval = 250000;
	qopt->num_entries = 4;
	delay_base_time(adapter, qopt, 3);
	if (!enable_check_taprio(adapter, qopt, 100))
		goto failed;

	/* change to shorter cycle time */
	qopt->base_time = ktime_set(0, 0);
	qopt->cycle_time = 333333;
	qopt->entries[0].gate_mask = 0x8F;
	qopt->entries[0].interval = 166666;
	qopt->entries[1].gate_mask = 0x80;
	qopt->entries[1].interval = 166667;
	qopt->num_entries = 2;
	delay_base_time(adapter, qopt, 11);
	if (!enable_check_taprio(adapter, qopt, 100))
		goto failed;
	qopt->base_time = ktime_set(0, 0);
	qopt->cycle_time = 62500;
	qopt->entries[0].gate_mask = 0x81;
	qopt->entries[0].interval = 31250;
	qopt->entries[1].gate_mask = 0x82;
	qopt->entries[1].interval = 15625;
	qopt->entries[2].gate_mask = 0x83;
	qopt->entries[2].interval = 15625;
	qopt->num_entries = 3;
	delay_base_time(adapter, qopt, 1);
	if (!enable_check_taprio(adapter, qopt, 100))
		goto failed;

	/* change to longer cycle time */
	qopt->base_time = ktime_set(0, 0);
	qopt->cycle_time = 400000;
	qopt->entries[0].gate_mask = 0x84;
	qopt->entries[0].interval = 100000;
	qopt->entries[1].gate_mask = 0x85;
	qopt->entries[1].interval = 100000;
	qopt->entries[2].gate_mask = 0x86;
	qopt->entries[2].interval = 100000;
	qopt->entries[3].gate_mask = 0x87;
	qopt->entries[3].interval = 100000;
	qopt->num_entries = 4;
	delay_base_time(adapter, qopt, 7);
	if (!enable_check_taprio(adapter, qopt, 100))
		goto failed;
	qopt->base_time = ktime_set(0, 0);
	qopt->cycle_time = 1700000;
	qopt->entries[0].gate_mask = 0x88;
	qopt->entries[0].interval = 200000;
	qopt->entries[1].gate_mask = 0x89;
	qopt->entries[1].interval = 300000;
	qopt->entries[2].gate_mask = 0x8A;
	qopt->entries[2].interval = 600000;
	qopt->entries[3].gate_mask = 0x8B;
	qopt->entries[3].interval = 100000;
	qopt->entries[4].gate_mask = 0x8C;
	qopt->entries[4].interval = 500000;
	qopt->num_entries = 5;
	delay_base_time(adapter, qopt, 6);
	if (!enable_check_taprio(adapter, qopt, 100))
		goto failed;

	if (!disable_taprio(adapter))
		goto failed;

	kfree(qopt);

	return true;

failed:
	disable_taprio(adapter);
	kfree(qopt);

	return false;
}

static bool tsnep_test_taprio_extension(struct tsnep_adapter *adapter)
{
	struct tc_taprio_qopt_offload *qopt;
	int i;

	qopt = kzalloc(struct_size(qopt, entries, 255), GFP_KERNEL);
	if (!qopt)
		return false;
	for (i = 0; i < 255; i++)
		qopt->entries[i].command = TC_TAPRIO_CMD_SET_GATES;

	qopt->enable = 1;
	qopt->base_time = ktime_set(0, 0);
	qopt->cycle_time = 100000;
	qopt->cycle_time_extension = 50000;
	qopt->entries[0].gate_mask = 0x90;
	qopt->entries[0].interval = 20000;
	qopt->entries[1].gate_mask = 0x91;
	qopt->entries[1].interval = 80000;
	qopt->num_entries = 2;
	if (!enable_check_taprio(adapter, qopt, 100))
		goto failed;

	/* change to different phase */
	qopt->base_time = ktime_set(0, 50000);
	qopt->entries[0].gate_mask = 0x92;
	qopt->entries[0].interval = 33000;
	qopt->entries[1].gate_mask = 0x93;
	qopt->entries[1].interval = 67000;
	qopt->num_entries = 2;
	delay_base_time(adapter, qopt, 2);
	if (!enable_check_taprio(adapter, qopt, 100))
		goto failed;

	/* change to different phase and longer cycle time */
	qopt->base_time = ktime_set(0, 0);
	qopt->cycle_time = 1000000;
	qopt->cycle_time_extension = 700000;
	qopt->entries[0].gate_mask = 0x94;
	qopt->entries[0].interval = 400000;
	qopt->entries[1].gate_mask = 0x95;
	qopt->entries[1].interval = 600000;
	qopt->num_entries = 2;
	delay_base_time(adapter, qopt, 7);
	if (!enable_check_taprio(adapter, qopt, 100))
		goto failed;
	qopt->base_time = ktime_set(0, 700000);
	qopt->cycle_time = 2000000;
	qopt->cycle_time_extension = 1900000;
	qopt->entries[0].gate_mask = 0x96;
	qopt->entries[0].interval = 400000;
	qopt->entries[1].gate_mask = 0x97;
	qopt->entries[1].interval = 1600000;
	qopt->num_entries = 2;
	delay_base_time(adapter, qopt, 9);
	if (!enable_check_taprio(adapter, qopt, 100))
		goto failed;

	/* change to different phase and shorter cycle time */
	qopt->base_time = ktime_set(0, 0);
	qopt->cycle_time = 1500000;
	qopt->cycle_time_extension = 700000;
	qopt->entries[0].gate_mask = 0x98;
	qopt->entries[0].interval = 400000;
	qopt->entries[1].gate_mask = 0x99;
	qopt->entries[1].interval = 600000;
	qopt->entries[2].gate_mask = 0x9A;
	qopt->entries[2].interval = 500000;
	qopt->num_entries = 3;
	delay_base_time(adapter, qopt, 3);
	if (!enable_check_taprio(adapter, qopt, 100))
		goto failed;
	qopt->base_time = ktime_set(0, 100000);
	qopt->cycle_time = 500000;
	qopt->cycle_time_extension = 300000;
	qopt->entries[0].gate_mask = 0x9B;
	qopt->entries[0].interval = 150000;
	qopt->entries[1].gate_mask = 0x9C;
	qopt->entries[1].interval = 350000;
	qopt->num_entries = 2;
	delay_base_time(adapter, qopt, 9);
	if (!enable_check_taprio(adapter, qopt, 100))
		goto failed;

	/* change to different cycle time */
	qopt->base_time = ktime_set(0, 0);
	qopt->cycle_time = 1000000;
	qopt->cycle_time_extension = 700000;
	qopt->entries[0].gate_mask = 0xAD;
	qopt->entries[0].interval = 400000;
	qopt->entries[1].gate_mask = 0xAE;
	qopt->entries[1].interval = 300000;
	qopt->entries[2].gate_mask = 0xAF;
	qopt->entries[2].interval = 300000;
	qopt->num_entries = 3;
	if (!enable_check_taprio(adapter, qopt, 100))
		goto failed;
	qopt->base_time = ktime_set(0, 0);
	qopt->cycle_time = 400000;
	qopt->cycle_time_extension = 100000;
	qopt->entries[0].gate_mask = 0xA0;
	qopt->entries[0].interval = 200000;
	qopt->entries[1].gate_mask = 0xA1;
	qopt->entries[1].interval = 200000;
	qopt->num_entries = 2;
	delay_base_time(adapter, qopt, 19);
	if (!enable_check_taprio(adapter, qopt, 100))
		goto failed;
	qopt->base_time = ktime_set(0, 0);
	qopt->cycle_time = 500000;
	qopt->cycle_time_extension = 499999;
	qopt->entries[0].gate_mask = 0xB2;
	qopt->entries[0].interval = 100000;
	qopt->entries[1].gate_mask = 0xB3;
	qopt->entries[1].interval = 100000;
	qopt->entries[2].gate_mask = 0xB4;
	qopt->entries[2].interval = 100000;
	qopt->entries[3].gate_mask = 0xB5;
	qopt->entries[3].interval = 200000;
	qopt->num_entries = 4;
	delay_base_time(adapter, qopt, 19);
	if (!enable_check_taprio(adapter, qopt, 100))
		goto failed;
	qopt->base_time = ktime_set(0, 0);
	qopt->cycle_time = 6000000;
	qopt->cycle_time_extension = 5999999;
	qopt->entries[0].gate_mask = 0xC6;
	qopt->entries[0].interval = 1000000;
	qopt->entries[1].gate_mask = 0xC7;
	qopt->entries[1].interval = 1000000;
	qopt->entries[2].gate_mask = 0xC8;
	qopt->entries[2].interval = 1000000;
	qopt->entries[3].gate_mask = 0xC9;
	qopt->entries[3].interval = 1500000;
	qopt->entries[4].gate_mask = 0xCA;
	qopt->entries[4].interval = 1500000;
	qopt->num_entries = 5;
	delay_base_time(adapter, qopt, 1);
	if (!enable_check_taprio(adapter, qopt, 100))
		goto failed;

	if (!disable_taprio(adapter))
		goto failed;

	kfree(qopt);

	return true;

failed:
	disable_taprio(adapter);
	kfree(qopt);

	return false;
}

int tsnep_ethtool_get_test_count(void)
{
	return TSNEP_TEST_COUNT;
}

void tsnep_ethtool_get_test_strings(u8 *data)
{
	memcpy(data, tsnep_test_strings, sizeof(tsnep_test_strings));
}

void tsnep_ethtool_self_test(struct net_device *netdev,
			     struct ethtool_test *eth_test, u64 *data)
{
	struct tsnep_adapter *adapter = netdev_priv(netdev);

	eth_test->len = TSNEP_TEST_COUNT;

	if (eth_test->flags != ETH_TEST_FL_OFFLINE) {
		/* no tests are done online */
		data[TSNEP_TEST_ENABLE] = 0;
		data[TSNEP_TEST_TAPRIO] = 0;
		data[TSNEP_TEST_TAPRIO_CHANGE] = 0;
		data[TSNEP_TEST_TAPRIO_EXTENSION] = 0;

		return;
	}

	if (tsnep_test_gc_enable(adapter)) {
		data[TSNEP_TEST_ENABLE] = 0;
	} else {
		eth_test->flags |= ETH_TEST_FL_FAILED;
		data[TSNEP_TEST_ENABLE] = 1;
	}

	if (tsnep_test_taprio(adapter)) {
		data[TSNEP_TEST_TAPRIO] = 0;
	} else {
		eth_test->flags |= ETH_TEST_FL_FAILED;
		data[TSNEP_TEST_TAPRIO] = 1;
	}

	if (tsnep_test_taprio_change(adapter)) {
		data[TSNEP_TEST_TAPRIO_CHANGE] = 0;
	} else {
		eth_test->flags |= ETH_TEST_FL_FAILED;
		data[TSNEP_TEST_TAPRIO_CHANGE] = 1;
	}

	if (tsnep_test_taprio_extension(adapter)) {
		data[TSNEP_TEST_TAPRIO_EXTENSION] = 0;
	} else {
		eth_test->flags |= ETH_TEST_FL_FAILED;
		data[TSNEP_TEST_TAPRIO_EXTENSION] = 1;
	}
}
