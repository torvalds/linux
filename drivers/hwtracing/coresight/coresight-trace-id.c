// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022, Linaro Limited, All rights reserved.
 * Author: Mike Leach <mike.leach@linaro.org>
 */
#include <linux/coresight-pmu.h>
#include <linux/cpumask.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#include "coresight-trace-id.h"

/* Default trace ID map. Used on systems that don't require per sink mappings */
static struct coresight_trace_id_map id_map_default;

/* maintain a record of the mapping of IDs and pending releases per cpu */
static DEFINE_PER_CPU(atomic_t, cpu_id) = ATOMIC_INIT(0);
static cpumask_t cpu_id_release_pending;

/* perf session active counter */
static atomic_t perf_cs_etm_session_active = ATOMIC_INIT(0);

/* lock to protect id_map and cpu data  */
static DEFINE_SPINLOCK(id_map_lock);

/* #define TRACE_ID_DEBUG 1 */
#if defined(TRACE_ID_DEBUG) || defined(CONFIG_COMPILE_TEST)

static void coresight_trace_id_dump_table(struct coresight_trace_id_map *id_map,
					  const char *func_name)
{
	pr_debug("%s id_map::\n", func_name);
	pr_debug("Used = %*pb\n", CORESIGHT_TRACE_IDS_MAX, id_map->used_ids);
	pr_debug("Pend = %*pb\n", CORESIGHT_TRACE_IDS_MAX, id_map->pend_rel_ids);
}
#define DUMP_ID_MAP(map)   coresight_trace_id_dump_table(map, __func__)
#define DUMP_ID_CPU(cpu, id) pr_debug("%s called;  cpu=%d, id=%d\n", __func__, cpu, id)
#define DUMP_ID(id)   pr_debug("%s called; id=%d\n", __func__, id)
#define PERF_SESSION(n) pr_debug("%s perf count %d\n", __func__, n)
#else
#define DUMP_ID_MAP(map)
#define DUMP_ID(id)
#define DUMP_ID_CPU(cpu, id)
#define PERF_SESSION(n)
#endif

/* unlocked read of current trace ID value for given CPU */
static int _coresight_trace_id_read_cpu_id(int cpu)
{
	return atomic_read(&per_cpu(cpu_id, cpu));
}

/* look for next available odd ID, return 0 if none found */
static int coresight_trace_id_find_odd_id(struct coresight_trace_id_map *id_map)
{
	int found_id = 0, bit = 1, next_id;

	while ((bit < CORESIGHT_TRACE_ID_RES_TOP) && !found_id) {
		/*
		 * bitmap length of CORESIGHT_TRACE_ID_RES_TOP,
		 * search from offset `bit`.
		 */
		next_id = find_next_zero_bit(id_map->used_ids,
					     CORESIGHT_TRACE_ID_RES_TOP, bit);
		if ((next_id < CORESIGHT_TRACE_ID_RES_TOP) && (next_id & 0x1))
			found_id = next_id;
		else
			bit = next_id + 1;
	}
	return found_id;
}

/*
 * Allocate new ID and set in use
 *
 * if @preferred_id is a valid id then try to use that value if available.
 * if @preferred_id is not valid and @prefer_odd_id is true, try for odd id.
 *
 * Otherwise allocate next available ID.
 */
static int coresight_trace_id_alloc_new_id(struct coresight_trace_id_map *id_map,
					   int preferred_id, bool prefer_odd_id)
{
	int id = 0;

	/* for backwards compatibility, cpu IDs may use preferred value */
	if (IS_VALID_CS_TRACE_ID(preferred_id) &&
	    !test_bit(preferred_id, id_map->used_ids)) {
		id = preferred_id;
		goto trace_id_allocated;
	} else if (prefer_odd_id) {
	/* may use odd ids to avoid preferred legacy cpu IDs */
		id = coresight_trace_id_find_odd_id(id_map);
		if (id)
			goto trace_id_allocated;
	}

	/*
	 * skip reserved bit 0, look at bitmap length of
	 * CORESIGHT_TRACE_ID_RES_TOP from offset of bit 1.
	 */
	id = find_next_zero_bit(id_map->used_ids, CORESIGHT_TRACE_ID_RES_TOP, 1);
	if (id >= CORESIGHT_TRACE_ID_RES_TOP)
		return -EINVAL;

	/* mark as used */
trace_id_allocated:
	set_bit(id, id_map->used_ids);
	return id;
}

static void coresight_trace_id_free(int id, struct coresight_trace_id_map *id_map)
{
	if (WARN(!IS_VALID_CS_TRACE_ID(id), "Invalid Trace ID %d\n", id))
		return;
	if (WARN(!test_bit(id, id_map->used_ids), "Freeing unused ID %d\n", id))
		return;
	clear_bit(id, id_map->used_ids);
}

static void coresight_trace_id_set_pend_rel(int id, struct coresight_trace_id_map *id_map)
{
	if (WARN(!IS_VALID_CS_TRACE_ID(id), "Invalid Trace ID %d\n", id))
		return;
	set_bit(id, id_map->pend_rel_ids);
}

/*
 * release all pending IDs for all current maps & clear CPU associations
 *
 * This currently operates on the default id map, but may be extended to
 * operate on all registered id maps if per sink id maps are used.
 */
static void coresight_trace_id_release_all_pending(void)
{
	struct coresight_trace_id_map *id_map = &id_map_default;
	unsigned long flags;
	int cpu, bit;

	spin_lock_irqsave(&id_map_lock, flags);
	for_each_set_bit(bit, id_map->pend_rel_ids, CORESIGHT_TRACE_ID_RES_TOP) {
		clear_bit(bit, id_map->used_ids);
		clear_bit(bit, id_map->pend_rel_ids);
	}
	for_each_cpu(cpu, &cpu_id_release_pending) {
		atomic_set(&per_cpu(cpu_id, cpu), 0);
		cpumask_clear_cpu(cpu, &cpu_id_release_pending);
	}
	spin_unlock_irqrestore(&id_map_lock, flags);
	DUMP_ID_MAP(id_map);
}

static int coresight_trace_id_map_get_cpu_id(int cpu, struct coresight_trace_id_map *id_map)
{
	unsigned long flags;
	int id;

	spin_lock_irqsave(&id_map_lock, flags);

	/* check for existing allocation for this CPU */
	id = _coresight_trace_id_read_cpu_id(cpu);
	if (id)
		goto get_cpu_id_clr_pend;

	/*
	 * Find a new ID.
	 *
	 * Use legacy values where possible in the dynamic trace ID allocator to
	 * allow older tools to continue working if they are not upgraded at the
	 * same time as the kernel drivers.
	 *
	 * If the generated legacy ID is invalid, or not available then the next
	 * available dynamic ID will be used.
	 */
	id = coresight_trace_id_alloc_new_id(id_map,
					     CORESIGHT_LEGACY_CPU_TRACE_ID(cpu),
					     false);
	if (!IS_VALID_CS_TRACE_ID(id))
		goto get_cpu_id_out_unlock;

	/* allocate the new id to the cpu */
	atomic_set(&per_cpu(cpu_id, cpu), id);

get_cpu_id_clr_pend:
	/* we are (re)using this ID - so ensure it is not marked for release */
	cpumask_clear_cpu(cpu, &cpu_id_release_pending);
	clear_bit(id, id_map->pend_rel_ids);

get_cpu_id_out_unlock:
	spin_unlock_irqrestore(&id_map_lock, flags);

	DUMP_ID_CPU(cpu, id);
	DUMP_ID_MAP(id_map);
	return id;
}

static void coresight_trace_id_map_put_cpu_id(int cpu, struct coresight_trace_id_map *id_map)
{
	unsigned long flags;
	int id;

	/* check for existing allocation for this CPU */
	id = _coresight_trace_id_read_cpu_id(cpu);
	if (!id)
		return;

	spin_lock_irqsave(&id_map_lock, flags);

	if (atomic_read(&perf_cs_etm_session_active)) {
		/* set release at pending if perf still active */
		coresight_trace_id_set_pend_rel(id, id_map);
		cpumask_set_cpu(cpu, &cpu_id_release_pending);
	} else {
		/* otherwise clear id */
		coresight_trace_id_free(id, id_map);
		atomic_set(&per_cpu(cpu_id, cpu), 0);
	}

	spin_unlock_irqrestore(&id_map_lock, flags);
	DUMP_ID_CPU(cpu, id);
	DUMP_ID_MAP(id_map);
}

static int coresight_trace_id_map_get_system_id(struct coresight_trace_id_map *id_map)
{
	unsigned long flags;
	int id;

	spin_lock_irqsave(&id_map_lock, flags);
	/* prefer odd IDs for system components to avoid legacy CPU IDS */
	id = coresight_trace_id_alloc_new_id(id_map, 0, true);
	spin_unlock_irqrestore(&id_map_lock, flags);

	DUMP_ID(id);
	DUMP_ID_MAP(id_map);
	return id;
}

static void coresight_trace_id_map_put_system_id(struct coresight_trace_id_map *id_map, int id)
{
	unsigned long flags;

	spin_lock_irqsave(&id_map_lock, flags);
	coresight_trace_id_free(id, id_map);
	spin_unlock_irqrestore(&id_map_lock, flags);

	DUMP_ID(id);
	DUMP_ID_MAP(id_map);
}

/* API functions */

int coresight_trace_id_get_cpu_id(int cpu)
{
	return coresight_trace_id_map_get_cpu_id(cpu, &id_map_default);
}
EXPORT_SYMBOL_GPL(coresight_trace_id_get_cpu_id);

void coresight_trace_id_put_cpu_id(int cpu)
{
	coresight_trace_id_map_put_cpu_id(cpu, &id_map_default);
}
EXPORT_SYMBOL_GPL(coresight_trace_id_put_cpu_id);

int coresight_trace_id_read_cpu_id(int cpu)
{
	return _coresight_trace_id_read_cpu_id(cpu);
}
EXPORT_SYMBOL_GPL(coresight_trace_id_read_cpu_id);

int coresight_trace_id_get_system_id(void)
{
	return coresight_trace_id_map_get_system_id(&id_map_default);
}
EXPORT_SYMBOL_GPL(coresight_trace_id_get_system_id);

void coresight_trace_id_put_system_id(int id)
{
	coresight_trace_id_map_put_system_id(&id_map_default, id);
}
EXPORT_SYMBOL_GPL(coresight_trace_id_put_system_id);

void coresight_trace_id_perf_start(void)
{
	atomic_inc(&perf_cs_etm_session_active);
	PERF_SESSION(atomic_read(&perf_cs_etm_session_active));
}
EXPORT_SYMBOL_GPL(coresight_trace_id_perf_start);

void coresight_trace_id_perf_stop(void)
{
	if (!atomic_dec_return(&perf_cs_etm_session_active))
		coresight_trace_id_release_all_pending();
	PERF_SESSION(atomic_read(&perf_cs_etm_session_active));
}
EXPORT_SYMBOL_GPL(coresight_trace_id_perf_stop);
