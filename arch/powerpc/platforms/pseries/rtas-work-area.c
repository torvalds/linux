// SPDX-License-Identifier: GPL-2.0-only

#define pr_fmt(fmt)	"rtas-work-area: " fmt

#include <linux/genalloc.h>
#include <linux/log2.h>
#include <linux/kernel.h>
#include <linux/memblock.h>
#include <linux/mempool.h>
#include <linux/minmax.h>
#include <linux/mutex.h>
#include <linux/numa.h>
#include <linux/sizes.h>
#include <linux/wait.h>

#include <asm/machdep.h>
#include <asm/rtas-work-area.h>
#include <asm/rtas.h>

enum {
	/*
	 * Ensure the pool is page-aligned.
	 */
	RTAS_WORK_AREA_ARENA_ALIGN = PAGE_SIZE,
	/*
	 * Don't let a single allocation claim the whole arena.
	 */
	RTAS_WORK_AREA_ARENA_SZ = RTAS_WORK_AREA_MAX_ALLOC_SZ * 2,
	/*
	 * The smallest known work area size is for ibm,get-vpd's
	 * location code argument, which is limited to 79 characters
	 * plus 1 nul terminator.
	 *
	 * PAPR+ 7.3.20 ibm,get-vpd RTAS Call
	 * PAPR+ 12.3.2.4 Converged Location Code Rules - Length Restrictions
	 */
	RTAS_WORK_AREA_MIN_ALLOC_SZ = roundup_pow_of_two(80),
};

static struct {
	struct gen_pool *gen_pool;
	char *arena;
	struct mutex mutex; /* serializes allocations */
	struct wait_queue_head wqh;
	mempool_t descriptor_pool;
	bool available;
} rwa_state = {
	.mutex = __MUTEX_INITIALIZER(rwa_state.mutex),
	.wqh = __WAIT_QUEUE_HEAD_INITIALIZER(rwa_state.wqh),
};

/*
 * A single work area buffer and descriptor to serve requests early in
 * boot before the allocator is fully initialized. We know 4KB is the
 * most any boot time user needs (they all call ibm,get-system-parameter).
 */
static bool early_work_area_in_use __initdata;
static char early_work_area_buf[SZ_4K] __initdata __aligned(SZ_4K);
static struct rtas_work_area early_work_area __initdata = {
	.buf = early_work_area_buf,
	.size = sizeof(early_work_area_buf),
};


static struct rtas_work_area * __init rtas_work_area_alloc_early(size_t size)
{
	WARN_ON(size > early_work_area.size);
	WARN_ON(early_work_area_in_use);
	early_work_area_in_use = true;
	memset(early_work_area.buf, 0, early_work_area.size);
	return &early_work_area;
}

static void __init rtas_work_area_free_early(struct rtas_work_area *work_area)
{
	WARN_ON(work_area != &early_work_area);
	WARN_ON(!early_work_area_in_use);
	early_work_area_in_use = false;
}

struct rtas_work_area * __ref __rtas_work_area_alloc(size_t size)
{
	struct rtas_work_area *area;
	unsigned long addr;

	might_sleep();

	/*
	 * The rtas_work_area_alloc() wrapper enforces this at build
	 * time. Requests that exceed the arena size will block
	 * indefinitely.
	 */
	WARN_ON(size > RTAS_WORK_AREA_MAX_ALLOC_SZ);

	if (!rwa_state.available)
		return rtas_work_area_alloc_early(size);
	/*
	 * To ensure FCFS behavior and prevent a high rate of smaller
	 * requests from starving larger ones, use the mutex to queue
	 * allocations.
	 */
	mutex_lock(&rwa_state.mutex);
	wait_event(rwa_state.wqh,
		   (addr = gen_pool_alloc(rwa_state.gen_pool, size)) != 0);
	mutex_unlock(&rwa_state.mutex);

	area = mempool_alloc(&rwa_state.descriptor_pool, GFP_KERNEL);
	area->buf = (char *)addr;
	area->size = size;

	return area;
}

void __ref rtas_work_area_free(struct rtas_work_area *area)
{
	if (!rwa_state.available) {
		rtas_work_area_free_early(area);
		return;
	}

	gen_pool_free(rwa_state.gen_pool, (unsigned long)area->buf, area->size);
	mempool_free(area, &rwa_state.descriptor_pool);
	wake_up(&rwa_state.wqh);
}

/*
 * Initialization of the work area allocator happens in two parts. To
 * reliably reserve an arena that satisfies RTAS addressing
 * requirements, we must perform a memblock allocation early,
 * immmediately after RTAS instantiation. Then we have to wait until
 * the slab allocator is up before setting up the descriptor mempool
 * and adding the arena to a gen_pool.
 */
static __init int rtas_work_area_allocator_init(void)
{
	const unsigned int order = ilog2(RTAS_WORK_AREA_MIN_ALLOC_SZ);
	const phys_addr_t pa_start = __pa(rwa_state.arena);
	const phys_addr_t pa_end = pa_start + RTAS_WORK_AREA_ARENA_SZ - 1;
	struct gen_pool *pool;
	const int nid = NUMA_NO_NODE;
	int err;

	err = -ENOMEM;
	if (!rwa_state.arena)
		goto err_out;

	pool = gen_pool_create(order, nid);
	if (!pool)
		goto err_out;
	/*
	 * All RTAS functions that consume work areas are OK with
	 * natural alignment, when they have alignment requirements at
	 * all.
	 */
	gen_pool_set_algo(pool, gen_pool_first_fit_order_align, NULL);

	err = gen_pool_add(pool, (unsigned long)rwa_state.arena,
			   RTAS_WORK_AREA_ARENA_SZ, nid);
	if (err)
		goto err_destroy;

	err = mempool_init_kmalloc_pool(&rwa_state.descriptor_pool, 1,
					sizeof(struct rtas_work_area));
	if (err)
		goto err_destroy;

	rwa_state.gen_pool = pool;
	rwa_state.available = true;

	pr_debug("arena [%pa-%pa] (%uK), min/max alloc sizes %u/%u\n",
		 &pa_start, &pa_end,
		 RTAS_WORK_AREA_ARENA_SZ / SZ_1K,
		 RTAS_WORK_AREA_MIN_ALLOC_SZ,
		 RTAS_WORK_AREA_MAX_ALLOC_SZ);

	return 0;

err_destroy:
	gen_pool_destroy(pool);
err_out:
	return err;
}
machine_arch_initcall(pseries, rtas_work_area_allocator_init);

/**
 * rtas_work_area_reserve_arena() - Reserve memory suitable for RTAS work areas.
 * @limit: Upper limit for memblock allocation.
 */
void __init rtas_work_area_reserve_arena(const phys_addr_t limit)
{
	const phys_addr_t align = RTAS_WORK_AREA_ARENA_ALIGN;
	const phys_addr_t size = RTAS_WORK_AREA_ARENA_SZ;
	const phys_addr_t min = MEMBLOCK_LOW_LIMIT;
	const int nid = NUMA_NO_NODE;

	/*
	 * Too early for a machine_is(pseries) check. But PAPR
	 * effectively mandates that ibm,get-system-parameter is
	 * present:
	 *
	 * R1–7.3.16–1. All platforms must support the System
	 * Parameters option.
	 *
	 * So set up the arena if we find that, with a fallback to
	 * ibm,configure-connector, just in case.
	 */
	if (rtas_function_implemented(RTAS_FN_IBM_GET_SYSTEM_PARAMETER) ||
	    rtas_function_implemented(RTAS_FN_IBM_CONFIGURE_CONNECTOR))
		rwa_state.arena = memblock_alloc_try_nid(size, align, min, limit, nid);
}
