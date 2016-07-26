#include <linux/errno.h>
#include <linux/numa.h>
#include <linux/slab.h>
#include <linux/rculist.h>
#include <linux/threads.h>
#include <linux/preempt.h>
#include <linux/irqflags.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/device-mapper.h>

#include "dm.h"
#include "dm-stats.h"

#define DM_MSG_PREFIX "stats"

static int dm_stat_need_rcu_barrier;

/*
 * Using 64-bit values to avoid overflow (which is a
 * problem that block/genhd.c's IO accounting has).
 */
struct dm_stat_percpu {
	unsigned long long sectors[2];
	unsigned long long ios[2];
	unsigned long long merges[2];
	unsigned long long ticks[2];
	unsigned long long io_ticks[2];
	unsigned long long io_ticks_total;
	unsigned long long time_in_queue;
	unsigned long long *histogram;
};

struct dm_stat_shared {
	atomic_t in_flight[2];
	unsigned long long stamp;
	struct dm_stat_percpu tmp;
};

struct dm_stat {
	struct list_head list_entry;
	int id;
	unsigned stat_flags;
	size_t n_entries;
	sector_t start;
	sector_t end;
	sector_t step;
	unsigned n_histogram_entries;
	unsigned long long *histogram_boundaries;
	const char *program_id;
	const char *aux_data;
	struct rcu_head rcu_head;
	size_t shared_alloc_size;
	size_t percpu_alloc_size;
	size_t histogram_alloc_size;
	struct dm_stat_percpu *stat_percpu[NR_CPUS];
	struct dm_stat_shared stat_shared[0];
};

#define STAT_PRECISE_TIMESTAMPS		1

struct dm_stats_last_position {
	sector_t last_sector;
	unsigned last_rw;
};

/*
 * A typo on the command line could possibly make the kernel run out of memory
 * and crash. To prevent the crash we account all used memory. We fail if we
 * exhaust 1/4 of all memory or 1/2 of vmalloc space.
 */
#define DM_STATS_MEMORY_FACTOR		4
#define DM_STATS_VMALLOC_FACTOR		2

static DEFINE_SPINLOCK(shared_memory_lock);

static unsigned long shared_memory_amount;

static bool __check_shared_memory(size_t alloc_size)
{
	size_t a;

	a = shared_memory_amount + alloc_size;
	if (a < shared_memory_amount)
		return false;
	if (a >> PAGE_SHIFT > totalram_pages / DM_STATS_MEMORY_FACTOR)
		return false;
#ifdef CONFIG_MMU
	if (a > (VMALLOC_END - VMALLOC_START) / DM_STATS_VMALLOC_FACTOR)
		return false;
#endif
	return true;
}

static bool check_shared_memory(size_t alloc_size)
{
	bool ret;

	spin_lock_irq(&shared_memory_lock);

	ret = __check_shared_memory(alloc_size);

	spin_unlock_irq(&shared_memory_lock);

	return ret;
}

static bool claim_shared_memory(size_t alloc_size)
{
	spin_lock_irq(&shared_memory_lock);

	if (!__check_shared_memory(alloc_size)) {
		spin_unlock_irq(&shared_memory_lock);
		return false;
	}

	shared_memory_amount += alloc_size;

	spin_unlock_irq(&shared_memory_lock);

	return true;
}

static void free_shared_memory(size_t alloc_size)
{
	unsigned long flags;

	spin_lock_irqsave(&shared_memory_lock, flags);

	if (WARN_ON_ONCE(shared_memory_amount < alloc_size)) {
		spin_unlock_irqrestore(&shared_memory_lock, flags);
		DMCRIT("Memory usage accounting bug.");
		return;
	}

	shared_memory_amount -= alloc_size;

	spin_unlock_irqrestore(&shared_memory_lock, flags);
}

static void *dm_kvzalloc(size_t alloc_size, int node)
{
	void *p;

	if (!claim_shared_memory(alloc_size))
		return NULL;

	if (alloc_size <= KMALLOC_MAX_SIZE) {
		p = kzalloc_node(alloc_size, GFP_KERNEL | __GFP_NORETRY | __GFP_NOMEMALLOC | __GFP_NOWARN, node);
		if (p)
			return p;
	}
	p = vzalloc_node(alloc_size, node);
	if (p)
		return p;

	free_shared_memory(alloc_size);

	return NULL;
}

static void dm_kvfree(void *ptr, size_t alloc_size)
{
	if (!ptr)
		return;

	free_shared_memory(alloc_size);

	kvfree(ptr);
}

static void dm_stat_free(struct rcu_head *head)
{
	int cpu;
	struct dm_stat *s = container_of(head, struct dm_stat, rcu_head);

	kfree(s->program_id);
	kfree(s->aux_data);
	for_each_possible_cpu(cpu) {
		dm_kvfree(s->stat_percpu[cpu][0].histogram, s->histogram_alloc_size);
		dm_kvfree(s->stat_percpu[cpu], s->percpu_alloc_size);
	}
	dm_kvfree(s->stat_shared[0].tmp.histogram, s->histogram_alloc_size);
	dm_kvfree(s, s->shared_alloc_size);
}

static int dm_stat_in_flight(struct dm_stat_shared *shared)
{
	return atomic_read(&shared->in_flight[READ]) +
	       atomic_read(&shared->in_flight[WRITE]);
}

void dm_stats_init(struct dm_stats *stats)
{
	int cpu;
	struct dm_stats_last_position *last;

	mutex_init(&stats->mutex);
	INIT_LIST_HEAD(&stats->list);
	stats->last = alloc_percpu(struct dm_stats_last_position);
	for_each_possible_cpu(cpu) {
		last = per_cpu_ptr(stats->last, cpu);
		last->last_sector = (sector_t)ULLONG_MAX;
		last->last_rw = UINT_MAX;
	}
}

void dm_stats_cleanup(struct dm_stats *stats)
{
	size_t ni;
	struct dm_stat *s;
	struct dm_stat_shared *shared;

	while (!list_empty(&stats->list)) {
		s = container_of(stats->list.next, struct dm_stat, list_entry);
		list_del(&s->list_entry);
		for (ni = 0; ni < s->n_entries; ni++) {
			shared = &s->stat_shared[ni];
			if (WARN_ON(dm_stat_in_flight(shared))) {
				DMCRIT("leaked in-flight counter at index %lu "
				       "(start %llu, end %llu, step %llu): reads %d, writes %d",
				       (unsigned long)ni,
				       (unsigned long long)s->start,
				       (unsigned long long)s->end,
				       (unsigned long long)s->step,
				       atomic_read(&shared->in_flight[READ]),
				       atomic_read(&shared->in_flight[WRITE]));
			}
		}
		dm_stat_free(&s->rcu_head);
	}
	free_percpu(stats->last);
}

static int dm_stats_create(struct dm_stats *stats, sector_t start, sector_t end,
			   sector_t step, unsigned stat_flags,
			   unsigned n_histogram_entries,
			   unsigned long long *histogram_boundaries,
			   const char *program_id, const char *aux_data,
			   void (*suspend_callback)(struct mapped_device *),
			   void (*resume_callback)(struct mapped_device *),
			   struct mapped_device *md)
{
	struct list_head *l;
	struct dm_stat *s, *tmp_s;
	sector_t n_entries;
	size_t ni;
	size_t shared_alloc_size;
	size_t percpu_alloc_size;
	size_t histogram_alloc_size;
	struct dm_stat_percpu *p;
	int cpu;
	int ret_id;
	int r;

	if (end < start || !step)
		return -EINVAL;

	n_entries = end - start;
	if (dm_sector_div64(n_entries, step))
		n_entries++;

	if (n_entries != (size_t)n_entries || !(size_t)(n_entries + 1))
		return -EOVERFLOW;

	shared_alloc_size = sizeof(struct dm_stat) + (size_t)n_entries * sizeof(struct dm_stat_shared);
	if ((shared_alloc_size - sizeof(struct dm_stat)) / sizeof(struct dm_stat_shared) != n_entries)
		return -EOVERFLOW;

	percpu_alloc_size = (size_t)n_entries * sizeof(struct dm_stat_percpu);
	if (percpu_alloc_size / sizeof(struct dm_stat_percpu) != n_entries)
		return -EOVERFLOW;

	histogram_alloc_size = (n_histogram_entries + 1) * (size_t)n_entries * sizeof(unsigned long long);
	if (histogram_alloc_size / (n_histogram_entries + 1) != (size_t)n_entries * sizeof(unsigned long long))
		return -EOVERFLOW;

	if (!check_shared_memory(shared_alloc_size + histogram_alloc_size +
				 num_possible_cpus() * (percpu_alloc_size + histogram_alloc_size)))
		return -ENOMEM;

	s = dm_kvzalloc(shared_alloc_size, NUMA_NO_NODE);
	if (!s)
		return -ENOMEM;

	s->stat_flags = stat_flags;
	s->n_entries = n_entries;
	s->start = start;
	s->end = end;
	s->step = step;
	s->shared_alloc_size = shared_alloc_size;
	s->percpu_alloc_size = percpu_alloc_size;
	s->histogram_alloc_size = histogram_alloc_size;

	s->n_histogram_entries = n_histogram_entries;
	s->histogram_boundaries = kmemdup(histogram_boundaries,
					  s->n_histogram_entries * sizeof(unsigned long long), GFP_KERNEL);
	if (!s->histogram_boundaries) {
		r = -ENOMEM;
		goto out;
	}

	s->program_id = kstrdup(program_id, GFP_KERNEL);
	if (!s->program_id) {
		r = -ENOMEM;
		goto out;
	}
	s->aux_data = kstrdup(aux_data, GFP_KERNEL);
	if (!s->aux_data) {
		r = -ENOMEM;
		goto out;
	}

	for (ni = 0; ni < n_entries; ni++) {
		atomic_set(&s->stat_shared[ni].in_flight[READ], 0);
		atomic_set(&s->stat_shared[ni].in_flight[WRITE], 0);
	}

	if (s->n_histogram_entries) {
		unsigned long long *hi;
		hi = dm_kvzalloc(s->histogram_alloc_size, NUMA_NO_NODE);
		if (!hi) {
			r = -ENOMEM;
			goto out;
		}
		for (ni = 0; ni < n_entries; ni++) {
			s->stat_shared[ni].tmp.histogram = hi;
			hi += s->n_histogram_entries + 1;
		}
	}

	for_each_possible_cpu(cpu) {
		p = dm_kvzalloc(percpu_alloc_size, cpu_to_node(cpu));
		if (!p) {
			r = -ENOMEM;
			goto out;
		}
		s->stat_percpu[cpu] = p;
		if (s->n_histogram_entries) {
			unsigned long long *hi;
			hi = dm_kvzalloc(s->histogram_alloc_size, cpu_to_node(cpu));
			if (!hi) {
				r = -ENOMEM;
				goto out;
			}
			for (ni = 0; ni < n_entries; ni++) {
				p[ni].histogram = hi;
				hi += s->n_histogram_entries + 1;
			}
		}
	}

	/*
	 * Suspend/resume to make sure there is no i/o in flight,
	 * so that newly created statistics will be exact.
	 *
	 * (note: we couldn't suspend earlier because we must not
	 * allocate memory while suspended)
	 */
	suspend_callback(md);

	mutex_lock(&stats->mutex);
	s->id = 0;
	list_for_each(l, &stats->list) {
		tmp_s = container_of(l, struct dm_stat, list_entry);
		if (WARN_ON(tmp_s->id < s->id)) {
			r = -EINVAL;
			goto out_unlock_resume;
		}
		if (tmp_s->id > s->id)
			break;
		if (unlikely(s->id == INT_MAX)) {
			r = -ENFILE;
			goto out_unlock_resume;
		}
		s->id++;
	}
	ret_id = s->id;
	list_add_tail_rcu(&s->list_entry, l);
	mutex_unlock(&stats->mutex);

	resume_callback(md);

	return ret_id;

out_unlock_resume:
	mutex_unlock(&stats->mutex);
	resume_callback(md);
out:
	dm_stat_free(&s->rcu_head);
	return r;
}

static struct dm_stat *__dm_stats_find(struct dm_stats *stats, int id)
{
	struct dm_stat *s;

	list_for_each_entry(s, &stats->list, list_entry) {
		if (s->id > id)
			break;
		if (s->id == id)
			return s;
	}

	return NULL;
}

static int dm_stats_delete(struct dm_stats *stats, int id)
{
	struct dm_stat *s;
	int cpu;

	mutex_lock(&stats->mutex);

	s = __dm_stats_find(stats, id);
	if (!s) {
		mutex_unlock(&stats->mutex);
		return -ENOENT;
	}

	list_del_rcu(&s->list_entry);
	mutex_unlock(&stats->mutex);

	/*
	 * vfree can't be called from RCU callback
	 */
	for_each_possible_cpu(cpu)
		if (is_vmalloc_addr(s->stat_percpu) ||
		    is_vmalloc_addr(s->stat_percpu[cpu][0].histogram))
			goto do_sync_free;
	if (is_vmalloc_addr(s) ||
	    is_vmalloc_addr(s->stat_shared[0].tmp.histogram)) {
do_sync_free:
		synchronize_rcu_expedited();
		dm_stat_free(&s->rcu_head);
	} else {
		ACCESS_ONCE(dm_stat_need_rcu_barrier) = 1;
		call_rcu(&s->rcu_head, dm_stat_free);
	}
	return 0;
}

static int dm_stats_list(struct dm_stats *stats, const char *program,
			 char *result, unsigned maxlen)
{
	struct dm_stat *s;
	sector_t len;
	unsigned sz = 0;

	/*
	 * Output format:
	 *   <region_id>: <start_sector>+<length> <step> <program_id> <aux_data>
	 */

	mutex_lock(&stats->mutex);
	list_for_each_entry(s, &stats->list, list_entry) {
		if (!program || !strcmp(program, s->program_id)) {
			len = s->end - s->start;
			DMEMIT("%d: %llu+%llu %llu %s %s", s->id,
				(unsigned long long)s->start,
				(unsigned long long)len,
				(unsigned long long)s->step,
				s->program_id,
				s->aux_data);
			if (s->stat_flags & STAT_PRECISE_TIMESTAMPS)
				DMEMIT(" precise_timestamps");
			if (s->n_histogram_entries) {
				unsigned i;
				DMEMIT(" histogram:");
				for (i = 0; i < s->n_histogram_entries; i++) {
					if (i)
						DMEMIT(",");
					DMEMIT("%llu", s->histogram_boundaries[i]);
				}
			}
			DMEMIT("\n");
		}
	}
	mutex_unlock(&stats->mutex);

	return 1;
}

static void dm_stat_round(struct dm_stat *s, struct dm_stat_shared *shared,
			  struct dm_stat_percpu *p)
{
	/*
	 * This is racy, but so is part_round_stats_single.
	 */
	unsigned long long now, difference;
	unsigned in_flight_read, in_flight_write;

	if (likely(!(s->stat_flags & STAT_PRECISE_TIMESTAMPS)))
		now = jiffies;
	else
		now = ktime_to_ns(ktime_get());

	difference = now - shared->stamp;
	if (!difference)
		return;

	in_flight_read = (unsigned)atomic_read(&shared->in_flight[READ]);
	in_flight_write = (unsigned)atomic_read(&shared->in_flight[WRITE]);
	if (in_flight_read)
		p->io_ticks[READ] += difference;
	if (in_flight_write)
		p->io_ticks[WRITE] += difference;
	if (in_flight_read + in_flight_write) {
		p->io_ticks_total += difference;
		p->time_in_queue += (in_flight_read + in_flight_write) * difference;
	}
	shared->stamp = now;
}

static void dm_stat_for_entry(struct dm_stat *s, size_t entry,
			      int idx, sector_t len,
			      struct dm_stats_aux *stats_aux, bool end,
			      unsigned long duration_jiffies)
{
	struct dm_stat_shared *shared = &s->stat_shared[entry];
	struct dm_stat_percpu *p;

	/*
	 * For strict correctness we should use local_irq_save/restore
	 * instead of preempt_disable/enable.
	 *
	 * preempt_disable/enable is racy if the driver finishes bios
	 * from non-interrupt context as well as from interrupt context
	 * or from more different interrupts.
	 *
	 * On 64-bit architectures the race only results in not counting some
	 * events, so it is acceptable.  On 32-bit architectures the race could
	 * cause the counter going off by 2^32, so we need to do proper locking
	 * there.
	 *
	 * part_stat_lock()/part_stat_unlock() have this race too.
	 */
#if BITS_PER_LONG == 32
	unsigned long flags;
	local_irq_save(flags);
#else
	preempt_disable();
#endif
	p = &s->stat_percpu[smp_processor_id()][entry];

	if (!end) {
		dm_stat_round(s, shared, p);
		atomic_inc(&shared->in_flight[idx]);
	} else {
		unsigned long long duration;
		dm_stat_round(s, shared, p);
		atomic_dec(&shared->in_flight[idx]);
		p->sectors[idx] += len;
		p->ios[idx] += 1;
		p->merges[idx] += stats_aux->merged;
		if (!(s->stat_flags & STAT_PRECISE_TIMESTAMPS)) {
			p->ticks[idx] += duration_jiffies;
			duration = jiffies_to_msecs(duration_jiffies);
		} else {
			p->ticks[idx] += stats_aux->duration_ns;
			duration = stats_aux->duration_ns;
		}
		if (s->n_histogram_entries) {
			unsigned lo = 0, hi = s->n_histogram_entries + 1;
			while (lo + 1 < hi) {
				unsigned mid = (lo + hi) / 2;
				if (s->histogram_boundaries[mid - 1] > duration) {
					hi = mid;
				} else {
					lo = mid;
				}

			}
			p->histogram[lo]++;
		}
	}

#if BITS_PER_LONG == 32
	local_irq_restore(flags);
#else
	preempt_enable();
#endif
}

static void __dm_stat_bio(struct dm_stat *s, int bi_rw,
			  sector_t bi_sector, sector_t end_sector,
			  bool end, unsigned long duration_jiffies,
			  struct dm_stats_aux *stats_aux)
{
	sector_t rel_sector, offset, todo, fragment_len;
	size_t entry;

	if (end_sector <= s->start || bi_sector >= s->end)
		return;
	if (unlikely(bi_sector < s->start)) {
		rel_sector = 0;
		todo = end_sector - s->start;
	} else {
		rel_sector = bi_sector - s->start;
		todo = end_sector - bi_sector;
	}
	if (unlikely(end_sector > s->end))
		todo -= (end_sector - s->end);

	offset = dm_sector_div64(rel_sector, s->step);
	entry = rel_sector;
	do {
		if (WARN_ON_ONCE(entry >= s->n_entries)) {
			DMCRIT("Invalid area access in region id %d", s->id);
			return;
		}
		fragment_len = todo;
		if (fragment_len > s->step - offset)
			fragment_len = s->step - offset;
		dm_stat_for_entry(s, entry, bi_rw, fragment_len,
				  stats_aux, end, duration_jiffies);
		todo -= fragment_len;
		entry++;
		offset = 0;
	} while (unlikely(todo != 0));
}

void dm_stats_account_io(struct dm_stats *stats, unsigned long bi_rw,
			 sector_t bi_sector, unsigned bi_sectors, bool end,
			 unsigned long duration_jiffies,
			 struct dm_stats_aux *stats_aux)
{
	struct dm_stat *s;
	sector_t end_sector;
	struct dm_stats_last_position *last;
	bool got_precise_time;

	if (unlikely(!bi_sectors))
		return;

	end_sector = bi_sector + bi_sectors;

	if (!end) {
		/*
		 * A race condition can at worst result in the merged flag being
		 * misrepresented, so we don't have to disable preemption here.
		 */
		last = raw_cpu_ptr(stats->last);
		stats_aux->merged =
			(bi_sector == (ACCESS_ONCE(last->last_sector) &&
				       ((bi_rw == WRITE) ==
					(ACCESS_ONCE(last->last_rw) == WRITE))
				       ));
		ACCESS_ONCE(last->last_sector) = end_sector;
		ACCESS_ONCE(last->last_rw) = bi_rw;
	}

	rcu_read_lock();

	got_precise_time = false;
	list_for_each_entry_rcu(s, &stats->list, list_entry) {
		if (s->stat_flags & STAT_PRECISE_TIMESTAMPS && !got_precise_time) {
			if (!end)
				stats_aux->duration_ns = ktime_to_ns(ktime_get());
			else
				stats_aux->duration_ns = ktime_to_ns(ktime_get()) - stats_aux->duration_ns;
			got_precise_time = true;
		}
		__dm_stat_bio(s, bi_rw, bi_sector, end_sector, end, duration_jiffies, stats_aux);
	}

	rcu_read_unlock();
}

static void __dm_stat_init_temporary_percpu_totals(struct dm_stat_shared *shared,
						   struct dm_stat *s, size_t x)
{
	int cpu;
	struct dm_stat_percpu *p;

	local_irq_disable();
	p = &s->stat_percpu[smp_processor_id()][x];
	dm_stat_round(s, shared, p);
	local_irq_enable();

	shared->tmp.sectors[READ] = 0;
	shared->tmp.sectors[WRITE] = 0;
	shared->tmp.ios[READ] = 0;
	shared->tmp.ios[WRITE] = 0;
	shared->tmp.merges[READ] = 0;
	shared->tmp.merges[WRITE] = 0;
	shared->tmp.ticks[READ] = 0;
	shared->tmp.ticks[WRITE] = 0;
	shared->tmp.io_ticks[READ] = 0;
	shared->tmp.io_ticks[WRITE] = 0;
	shared->tmp.io_ticks_total = 0;
	shared->tmp.time_in_queue = 0;

	if (s->n_histogram_entries)
		memset(shared->tmp.histogram, 0, (s->n_histogram_entries + 1) * sizeof(unsigned long long));

	for_each_possible_cpu(cpu) {
		p = &s->stat_percpu[cpu][x];
		shared->tmp.sectors[READ] += ACCESS_ONCE(p->sectors[READ]);
		shared->tmp.sectors[WRITE] += ACCESS_ONCE(p->sectors[WRITE]);
		shared->tmp.ios[READ] += ACCESS_ONCE(p->ios[READ]);
		shared->tmp.ios[WRITE] += ACCESS_ONCE(p->ios[WRITE]);
		shared->tmp.merges[READ] += ACCESS_ONCE(p->merges[READ]);
		shared->tmp.merges[WRITE] += ACCESS_ONCE(p->merges[WRITE]);
		shared->tmp.ticks[READ] += ACCESS_ONCE(p->ticks[READ]);
		shared->tmp.ticks[WRITE] += ACCESS_ONCE(p->ticks[WRITE]);
		shared->tmp.io_ticks[READ] += ACCESS_ONCE(p->io_ticks[READ]);
		shared->tmp.io_ticks[WRITE] += ACCESS_ONCE(p->io_ticks[WRITE]);
		shared->tmp.io_ticks_total += ACCESS_ONCE(p->io_ticks_total);
		shared->tmp.time_in_queue += ACCESS_ONCE(p->time_in_queue);
		if (s->n_histogram_entries) {
			unsigned i;
			for (i = 0; i < s->n_histogram_entries + 1; i++)
				shared->tmp.histogram[i] += ACCESS_ONCE(p->histogram[i]);
		}
	}
}

static void __dm_stat_clear(struct dm_stat *s, size_t idx_start, size_t idx_end,
			    bool init_tmp_percpu_totals)
{
	size_t x;
	struct dm_stat_shared *shared;
	struct dm_stat_percpu *p;

	for (x = idx_start; x < idx_end; x++) {
		shared = &s->stat_shared[x];
		if (init_tmp_percpu_totals)
			__dm_stat_init_temporary_percpu_totals(shared, s, x);
		local_irq_disable();
		p = &s->stat_percpu[smp_processor_id()][x];
		p->sectors[READ] -= shared->tmp.sectors[READ];
		p->sectors[WRITE] -= shared->tmp.sectors[WRITE];
		p->ios[READ] -= shared->tmp.ios[READ];
		p->ios[WRITE] -= shared->tmp.ios[WRITE];
		p->merges[READ] -= shared->tmp.merges[READ];
		p->merges[WRITE] -= shared->tmp.merges[WRITE];
		p->ticks[READ] -= shared->tmp.ticks[READ];
		p->ticks[WRITE] -= shared->tmp.ticks[WRITE];
		p->io_ticks[READ] -= shared->tmp.io_ticks[READ];
		p->io_ticks[WRITE] -= shared->tmp.io_ticks[WRITE];
		p->io_ticks_total -= shared->tmp.io_ticks_total;
		p->time_in_queue -= shared->tmp.time_in_queue;
		local_irq_enable();
		if (s->n_histogram_entries) {
			unsigned i;
			for (i = 0; i < s->n_histogram_entries + 1; i++) {
				local_irq_disable();
				p = &s->stat_percpu[smp_processor_id()][x];
				p->histogram[i] -= shared->tmp.histogram[i];
				local_irq_enable();
			}
		}
	}
}

static int dm_stats_clear(struct dm_stats *stats, int id)
{
	struct dm_stat *s;

	mutex_lock(&stats->mutex);

	s = __dm_stats_find(stats, id);
	if (!s) {
		mutex_unlock(&stats->mutex);
		return -ENOENT;
	}

	__dm_stat_clear(s, 0, s->n_entries, true);

	mutex_unlock(&stats->mutex);

	return 1;
}

/*
 * This is like jiffies_to_msec, but works for 64-bit values.
 */
static unsigned long long dm_jiffies_to_msec64(struct dm_stat *s, unsigned long long j)
{
	unsigned long long result;
	unsigned mult;

	if (s->stat_flags & STAT_PRECISE_TIMESTAMPS)
		return j;

	result = 0;
	if (j)
		result = jiffies_to_msecs(j & 0x3fffff);
	if (j >= 1 << 22) {
		mult = jiffies_to_msecs(1 << 22);
		result += (unsigned long long)mult * (unsigned long long)jiffies_to_msecs((j >> 22) & 0x3fffff);
	}
	if (j >= 1ULL << 44)
		result += (unsigned long long)mult * (unsigned long long)mult * (unsigned long long)jiffies_to_msecs(j >> 44);

	return result;
}

static int dm_stats_print(struct dm_stats *stats, int id,
			  size_t idx_start, size_t idx_len,
			  bool clear, char *result, unsigned maxlen)
{
	unsigned sz = 0;
	struct dm_stat *s;
	size_t x;
	sector_t start, end, step;
	size_t idx_end;
	struct dm_stat_shared *shared;

	/*
	 * Output format:
	 *   <start_sector>+<length> counters
	 */

	mutex_lock(&stats->mutex);

	s = __dm_stats_find(stats, id);
	if (!s) {
		mutex_unlock(&stats->mutex);
		return -ENOENT;
	}

	idx_end = idx_start + idx_len;
	if (idx_end < idx_start ||
	    idx_end > s->n_entries)
		idx_end = s->n_entries;

	if (idx_start > idx_end)
		idx_start = idx_end;

	step = s->step;
	start = s->start + (step * idx_start);

	for (x = idx_start; x < idx_end; x++, start = end) {
		shared = &s->stat_shared[x];
		end = start + step;
		if (unlikely(end > s->end))
			end = s->end;

		__dm_stat_init_temporary_percpu_totals(shared, s, x);

		DMEMIT("%llu+%llu %llu %llu %llu %llu %llu %llu %llu %llu %d %llu %llu %llu %llu",
		       (unsigned long long)start,
		       (unsigned long long)step,
		       shared->tmp.ios[READ],
		       shared->tmp.merges[READ],
		       shared->tmp.sectors[READ],
		       dm_jiffies_to_msec64(s, shared->tmp.ticks[READ]),
		       shared->tmp.ios[WRITE],
		       shared->tmp.merges[WRITE],
		       shared->tmp.sectors[WRITE],
		       dm_jiffies_to_msec64(s, shared->tmp.ticks[WRITE]),
		       dm_stat_in_flight(shared),
		       dm_jiffies_to_msec64(s, shared->tmp.io_ticks_total),
		       dm_jiffies_to_msec64(s, shared->tmp.time_in_queue),
		       dm_jiffies_to_msec64(s, shared->tmp.io_ticks[READ]),
		       dm_jiffies_to_msec64(s, shared->tmp.io_ticks[WRITE]));
		if (s->n_histogram_entries) {
			unsigned i;
			for (i = 0; i < s->n_histogram_entries + 1; i++) {
				DMEMIT("%s%llu", !i ? " " : ":", shared->tmp.histogram[i]);
			}
		}
		DMEMIT("\n");

		if (unlikely(sz + 1 >= maxlen))
			goto buffer_overflow;
	}

	if (clear)
		__dm_stat_clear(s, idx_start, idx_end, false);

buffer_overflow:
	mutex_unlock(&stats->mutex);

	return 1;
}

static int dm_stats_set_aux(struct dm_stats *stats, int id, const char *aux_data)
{
	struct dm_stat *s;
	const char *new_aux_data;

	mutex_lock(&stats->mutex);

	s = __dm_stats_find(stats, id);
	if (!s) {
		mutex_unlock(&stats->mutex);
		return -ENOENT;
	}

	new_aux_data = kstrdup(aux_data, GFP_KERNEL);
	if (!new_aux_data) {
		mutex_unlock(&stats->mutex);
		return -ENOMEM;
	}

	kfree(s->aux_data);
	s->aux_data = new_aux_data;

	mutex_unlock(&stats->mutex);

	return 0;
}

static int parse_histogram(const char *h, unsigned *n_histogram_entries,
			   unsigned long long **histogram_boundaries)
{
	const char *q;
	unsigned n;
	unsigned long long last;

	*n_histogram_entries = 1;
	for (q = h; *q; q++)
		if (*q == ',')
			(*n_histogram_entries)++;

	*histogram_boundaries = kmalloc(*n_histogram_entries * sizeof(unsigned long long), GFP_KERNEL);
	if (!*histogram_boundaries)
		return -ENOMEM;

	n = 0;
	last = 0;
	while (1) {
		unsigned long long hi;
		int s;
		char ch;
		s = sscanf(h, "%llu%c", &hi, &ch);
		if (!s || (s == 2 && ch != ','))
			return -EINVAL;
		if (hi <= last)
			return -EINVAL;
		last = hi;
		(*histogram_boundaries)[n] = hi;
		if (s == 1)
			return 0;
		h = strchr(h, ',') + 1;
		n++;
	}
}

static int message_stats_create(struct mapped_device *md,
				unsigned argc, char **argv,
				char *result, unsigned maxlen)
{
	int r;
	int id;
	char dummy;
	unsigned long long start, end, len, step;
	unsigned divisor;
	const char *program_id, *aux_data;
	unsigned stat_flags = 0;

	unsigned n_histogram_entries = 0;
	unsigned long long *histogram_boundaries = NULL;

	struct dm_arg_set as, as_backup;
	const char *a;
	unsigned feature_args;

	/*
	 * Input format:
	 *   <range> <step> [<extra_parameters> <parameters>] [<program_id> [<aux_data>]]
	 */

	if (argc < 3)
		goto ret_einval;

	as.argc = argc;
	as.argv = argv;
	dm_consume_args(&as, 1);

	a = dm_shift_arg(&as);
	if (!strcmp(a, "-")) {
		start = 0;
		len = dm_get_size(md);
		if (!len)
			len = 1;
	} else if (sscanf(a, "%llu+%llu%c", &start, &len, &dummy) != 2 ||
		   start != (sector_t)start || len != (sector_t)len)
		goto ret_einval;

	end = start + len;
	if (start >= end)
		goto ret_einval;

	a = dm_shift_arg(&as);
	if (sscanf(a, "/%u%c", &divisor, &dummy) == 1) {
		if (!divisor)
			return -EINVAL;
		step = end - start;
		if (do_div(step, divisor))
			step++;
		if (!step)
			step = 1;
	} else if (sscanf(a, "%llu%c", &step, &dummy) != 1 ||
		   step != (sector_t)step || !step)
		goto ret_einval;

	as_backup = as;
	a = dm_shift_arg(&as);
	if (a && sscanf(a, "%u%c", &feature_args, &dummy) == 1) {
		while (feature_args--) {
			a = dm_shift_arg(&as);
			if (!a)
				goto ret_einval;
			if (!strcasecmp(a, "precise_timestamps"))
				stat_flags |= STAT_PRECISE_TIMESTAMPS;
			else if (!strncasecmp(a, "histogram:", 10)) {
				if (n_histogram_entries)
					goto ret_einval;
				if ((r = parse_histogram(a + 10, &n_histogram_entries, &histogram_boundaries)))
					goto ret;
			} else
				goto ret_einval;
		}
	} else {
		as = as_backup;
	}

	program_id = "-";
	aux_data = "-";

	a = dm_shift_arg(&as);
	if (a)
		program_id = a;

	a = dm_shift_arg(&as);
	if (a)
		aux_data = a;

	if (as.argc)
		goto ret_einval;

	/*
	 * If a buffer overflow happens after we created the region,
	 * it's too late (the userspace would retry with a larger
	 * buffer, but the region id that caused the overflow is already
	 * leaked).  So we must detect buffer overflow in advance.
	 */
	snprintf(result, maxlen, "%d", INT_MAX);
	if (dm_message_test_buffer_overflow(result, maxlen)) {
		r = 1;
		goto ret;
	}

	id = dm_stats_create(dm_get_stats(md), start, end, step, stat_flags,
			     n_histogram_entries, histogram_boundaries, program_id, aux_data,
			     dm_internal_suspend_fast, dm_internal_resume_fast, md);
	if (id < 0) {
		r = id;
		goto ret;
	}

	snprintf(result, maxlen, "%d", id);

	r = 1;
	goto ret;

ret_einval:
	r = -EINVAL;
ret:
	kfree(histogram_boundaries);
	return r;
}

static int message_stats_delete(struct mapped_device *md,
				unsigned argc, char **argv)
{
	int id;
	char dummy;

	if (argc != 2)
		return -EINVAL;

	if (sscanf(argv[1], "%d%c", &id, &dummy) != 1 || id < 0)
		return -EINVAL;

	return dm_stats_delete(dm_get_stats(md), id);
}

static int message_stats_clear(struct mapped_device *md,
			       unsigned argc, char **argv)
{
	int id;
	char dummy;

	if (argc != 2)
		return -EINVAL;

	if (sscanf(argv[1], "%d%c", &id, &dummy) != 1 || id < 0)
		return -EINVAL;

	return dm_stats_clear(dm_get_stats(md), id);
}

static int message_stats_list(struct mapped_device *md,
			      unsigned argc, char **argv,
			      char *result, unsigned maxlen)
{
	int r;
	const char *program = NULL;

	if (argc < 1 || argc > 2)
		return -EINVAL;

	if (argc > 1) {
		program = kstrdup(argv[1], GFP_KERNEL);
		if (!program)
			return -ENOMEM;
	}

	r = dm_stats_list(dm_get_stats(md), program, result, maxlen);

	kfree(program);

	return r;
}

static int message_stats_print(struct mapped_device *md,
			       unsigned argc, char **argv, bool clear,
			       char *result, unsigned maxlen)
{
	int id;
	char dummy;
	unsigned long idx_start = 0, idx_len = ULONG_MAX;

	if (argc != 2 && argc != 4)
		return -EINVAL;

	if (sscanf(argv[1], "%d%c", &id, &dummy) != 1 || id < 0)
		return -EINVAL;

	if (argc > 3) {
		if (strcmp(argv[2], "-") &&
		    sscanf(argv[2], "%lu%c", &idx_start, &dummy) != 1)
			return -EINVAL;
		if (strcmp(argv[3], "-") &&
		    sscanf(argv[3], "%lu%c", &idx_len, &dummy) != 1)
			return -EINVAL;
	}

	return dm_stats_print(dm_get_stats(md), id, idx_start, idx_len, clear,
			      result, maxlen);
}

static int message_stats_set_aux(struct mapped_device *md,
				 unsigned argc, char **argv)
{
	int id;
	char dummy;

	if (argc != 3)
		return -EINVAL;

	if (sscanf(argv[1], "%d%c", &id, &dummy) != 1 || id < 0)
		return -EINVAL;

	return dm_stats_set_aux(dm_get_stats(md), id, argv[2]);
}

int dm_stats_message(struct mapped_device *md, unsigned argc, char **argv,
		     char *result, unsigned maxlen)
{
	int r;

	/* All messages here must start with '@' */
	if (!strcasecmp(argv[0], "@stats_create"))
		r = message_stats_create(md, argc, argv, result, maxlen);
	else if (!strcasecmp(argv[0], "@stats_delete"))
		r = message_stats_delete(md, argc, argv);
	else if (!strcasecmp(argv[0], "@stats_clear"))
		r = message_stats_clear(md, argc, argv);
	else if (!strcasecmp(argv[0], "@stats_list"))
		r = message_stats_list(md, argc, argv, result, maxlen);
	else if (!strcasecmp(argv[0], "@stats_print"))
		r = message_stats_print(md, argc, argv, false, result, maxlen);
	else if (!strcasecmp(argv[0], "@stats_print_clear"))
		r = message_stats_print(md, argc, argv, true, result, maxlen);
	else if (!strcasecmp(argv[0], "@stats_set_aux"))
		r = message_stats_set_aux(md, argc, argv);
	else
		return 2; /* this wasn't a stats message */

	if (r == -EINVAL)
		DMWARN("Invalid parameters for message %s", argv[0]);

	return r;
}

int __init dm_statistics_init(void)
{
	shared_memory_amount = 0;
	dm_stat_need_rcu_barrier = 0;
	return 0;
}

void dm_statistics_exit(void)
{
	if (dm_stat_need_rcu_barrier)
		rcu_barrier();
	if (WARN_ON(shared_memory_amount))
		DMCRIT("shared_memory_amount leaked: %lu", shared_memory_amount);
}

module_param_named(stats_current_allocated_bytes, shared_memory_amount, ulong, S_IRUGO);
MODULE_PARM_DESC(stats_current_allocated_bytes, "Memory currently used by statistics");
