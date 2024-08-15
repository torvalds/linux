#include <nvhe/clock.h>
#include <nvhe/mem_protect.h>
#include <nvhe/mm.h>
#include <nvhe/trace.h>

#include <asm/kvm_mmu.h>
#include <asm/local.h>

#include <linux/ring_buffer.h>

#define HYP_RB_PAGE_HEAD		1UL
#define HYP_RB_PAGE_UPDATE		2UL
#define HYP_RB_FLAG_MASK		3UL

static struct hyp_buffer_pages_backing hyp_buffer_pages_backing;
DEFINE_PER_CPU(struct hyp_rb_per_cpu, trace_rb);
DEFINE_HYP_SPINLOCK(trace_rb_lock);

static bool rb_set_flag(struct hyp_buffer_page *bpage, int new_flag)
{
	unsigned long ret, val = (unsigned long)bpage->list.next;

	ret = cmpxchg((unsigned long *)&bpage->list.next,
		      val, (val & ~HYP_RB_FLAG_MASK) | new_flag);

	return ret == val;
}

static void rb_set_footer_status(struct hyp_buffer_page *bpage,
				 unsigned long status,
				 bool reader)
{
	struct buffer_data_page *page = bpage->page;
	struct rb_ext_page_footer *footer;

	footer = rb_ext_page_get_footer(page);

	if (reader)
		atomic_set(&footer->reader_status, status);
	else
		atomic_set(&footer->writer_status, status);
}

static void rb_footer_writer_status(struct hyp_buffer_page *bpage,
				    unsigned long status)
{
	rb_set_footer_status(bpage, status, false);
}

static void rb_footer_reader_status(struct hyp_buffer_page *bpage,
				    unsigned long status)
{
	rb_set_footer_status(bpage, status, true);
}

static struct hyp_buffer_page *rb_hyp_buffer_page(struct list_head *list)
{
	unsigned long ptr = (unsigned long)list & ~HYP_RB_FLAG_MASK;

	return container_of((struct list_head *)ptr, struct hyp_buffer_page, list);
}

static struct hyp_buffer_page *rb_next_page(struct hyp_buffer_page *bpage)
{
	return rb_hyp_buffer_page(bpage->list.next);
}

static bool rb_is_head_page(struct hyp_buffer_page *bpage)
{
	return (unsigned long)bpage->list.prev->next & HYP_RB_PAGE_HEAD;
}

static struct hyp_buffer_page *rb_set_head_page(struct hyp_rb_per_cpu *cpu_buffer)
{
	struct hyp_buffer_page *bpage, *prev_head;
	int cnt = 0;
again:
	bpage = prev_head = cpu_buffer->head_page;
	do {
		if (rb_is_head_page(bpage)) {
			cpu_buffer->head_page = bpage;
			rb_footer_reader_status(prev_head, 0);
			rb_footer_reader_status(bpage, RB_PAGE_FT_HEAD);
			return bpage;
		}

		bpage = rb_next_page(bpage);
	} while (bpage != prev_head);

	cnt++;

	/* We might have race with the writer let's try again */
	if (cnt < 3)
		goto again;

	return NULL;
}

static int rb_swap_reader_page(struct hyp_rb_per_cpu *cpu_buffer)
{
	unsigned long *old_head_link, old_link_val, new_link_val, overrun;
	struct hyp_buffer_page *head, *reader = cpu_buffer->reader_page;
	struct rb_ext_page_footer *footer;

	rb_footer_reader_status(cpu_buffer->reader_page, 0);
spin:
	/* Update the cpu_buffer->header_page according to HYP_RB_PAGE_HEAD */
	head = rb_set_head_page(cpu_buffer);
	if (!head)
		return -ENODEV;

	/* Connect the reader page around the header page */
	reader->list.next = head->list.next;
	reader->list.prev = head->list.prev;

	/* The reader page points to the new header page */
	rb_set_flag(reader, HYP_RB_PAGE_HEAD);

	/*
	 * Paired with the cmpxchg in rb_move_tail(). Order the read of the head
	 * page and overrun.
	 */
	smp_mb();
	overrun = atomic_read(&cpu_buffer->overrun);

	/* Try to swap the prev head link to the reader page */
	old_head_link = (unsigned long *)&reader->list.prev->next;
	old_link_val = (*old_head_link & ~HYP_RB_FLAG_MASK) | HYP_RB_PAGE_HEAD;
	new_link_val = (unsigned long)&reader->list;
	if (cmpxchg(old_head_link, old_link_val, new_link_val)
		      != old_link_val)
		goto spin;

	cpu_buffer->head_page = rb_hyp_buffer_page(reader->list.next);
	cpu_buffer->head_page->list.prev = &reader->list;
	cpu_buffer->reader_page = head;

	rb_footer_reader_status(cpu_buffer->reader_page, RB_PAGE_FT_READER);
	rb_footer_reader_status(cpu_buffer->head_page, RB_PAGE_FT_HEAD);

	footer = rb_ext_page_get_footer(cpu_buffer->reader_page->page);
	footer->stats.overrun = overrun;

	return 0;
}

static struct hyp_buffer_page *
rb_move_tail(struct hyp_rb_per_cpu *cpu_buffer)
{
	struct hyp_buffer_page *tail_page, *new_tail, *new_head;

	tail_page = cpu_buffer->tail_page;
	new_tail = rb_next_page(tail_page);
again:
	/*
	 * We caught the reader ... Let's try to move the head page.
	 * The writer can only rely on ->next links to check if this is head.
	 */
	if ((unsigned long)tail_page->list.next & HYP_RB_PAGE_HEAD) {
		/* The reader moved the head in between */
		if (!rb_set_flag(tail_page, HYP_RB_PAGE_UPDATE))
			goto again;

		atomic_add(atomic_read(&new_tail->entries), &cpu_buffer->overrun);

		/* Move the head */
		rb_set_flag(new_tail, HYP_RB_PAGE_HEAD);

		/* The new head is in place, reset the update flag */
		rb_set_flag(tail_page, 0);

		new_head = rb_next_page(new_tail);
	}

	rb_footer_writer_status(tail_page, 0);
	rb_footer_writer_status(new_tail, RB_PAGE_FT_COMMIT);

	local_set(&new_tail->page->commit, 0);

	atomic_set(&new_tail->write, 0);
	atomic_set(&new_tail->entries, 0);

	atomic_inc(&cpu_buffer->pages_touched);

	cpu_buffer->tail_page = new_tail;

	return new_tail;
}

unsigned long rb_event_size(unsigned long length)
{
	struct ring_buffer_event *event;

	return length + RB_EVNT_HDR_SIZE + sizeof(event->array[0]);
}

static struct ring_buffer_event *
rb_add_ts_extend(struct ring_buffer_event *event, u64 delta)
{
	event->type_len = RINGBUF_TYPE_TIME_EXTEND;
	event->time_delta = delta & TS_MASK;
	event->array[0] = delta >> TS_SHIFT;

	return (struct ring_buffer_event *)((unsigned long)event + 8);
}

static struct ring_buffer_event *
rb_reserve_next(struct hyp_rb_per_cpu *cpu_buffer, unsigned long length)
{
	unsigned long ts_ext_size = 0, event_size = rb_event_size(length);
	struct hyp_buffer_page *tail_page = cpu_buffer->tail_page;
	struct ring_buffer_event *event;
	unsigned long write, prev_write;
	u64 ts, time_delta;

	ts = trace_clock();

	time_delta = ts - atomic64_read(&cpu_buffer->write_stamp);

	if (test_time_stamp(time_delta))
		ts_ext_size = 8;

	prev_write = atomic_read(&tail_page->write);
	write = prev_write + event_size + ts_ext_size;

	if (unlikely(write > BUF_EXT_PAGE_SIZE))
		tail_page = rb_move_tail(cpu_buffer);

	if (!atomic_read(&tail_page->entries)) {
		tail_page->page->time_stamp = ts;
		time_delta = 0;
		ts_ext_size = 0;
		write = event_size;
		prev_write = 0;
	}

	atomic_set(&tail_page->write, write);
	atomic_inc(&tail_page->entries);

	local_set(&tail_page->page->commit, write);

	atomic_inc(&cpu_buffer->nr_entries);
	atomic64_set(&cpu_buffer->write_stamp, ts);

	event = (struct ring_buffer_event *)(tail_page->page->data +
					     prev_write);
	if (ts_ext_size) {
		event = rb_add_ts_extend(event, time_delta);
		time_delta = 0;
	}

	event->type_len = 0;
	event->time_delta = time_delta;
	event->array[0] = event_size - RB_EVNT_HDR_SIZE;

	return event;
}

void *
rb_reserve_trace_entry(struct hyp_rb_per_cpu *cpu_buffer, unsigned long length)
{
	struct ring_buffer_event *rb_event;

	rb_event = rb_reserve_next(cpu_buffer, length);

	return &rb_event->array[1];
}

static int rb_update_footers(struct hyp_rb_per_cpu *cpu_buffer)
{
	unsigned long entries, pages_touched, overrun;
	struct rb_ext_page_footer *footer;
	struct buffer_data_page *reader;

	if (!rb_set_head_page(cpu_buffer))
		return -ENODEV;

	reader = cpu_buffer->reader_page->page;
	footer = rb_ext_page_get_footer(reader);

	entries = atomic_read(&cpu_buffer->nr_entries);
	footer->stats.entries = entries;
	pages_touched = atomic_read(&cpu_buffer->pages_touched);
	footer->stats.pages_touched = pages_touched;
	overrun = atomic_read(&cpu_buffer->overrun);
	footer->stats.overrun = overrun;

	return 0;
}

static int rb_page_init(struct hyp_buffer_page *bpage, unsigned long hva)
{
	void *hyp_va = (void *)kern_hyp_va(hva);
	int ret;

	ret = hyp_pin_shared_mem(hyp_va, hyp_va + PAGE_SIZE);
	if (ret)
		return ret;

	INIT_LIST_HEAD(&bpage->list);
	bpage->page = (struct buffer_data_page *)hyp_va;

	atomic_set(&bpage->write, 0);

	rb_footer_reader_status(bpage, 0);
	rb_footer_writer_status(bpage, 0);

	return 0;
}

static bool rb_cpu_loaded(struct hyp_rb_per_cpu *cpu_buffer)
{
	return cpu_buffer->bpages;
}

static void rb_cpu_disable(struct hyp_rb_per_cpu *cpu_buffer)
{
	unsigned int prev_status;

	/* Wait for release of the buffer */
	do {
		/* Paired with __stop_write_hyp_rb */
		prev_status = atomic_cmpxchg_acquire(&cpu_buffer->status,
						     HYP_RB_WRITABLE,
						     HYP_RB_NONWRITABLE);
	} while (prev_status == HYP_RB_WRITING);

	if (prev_status == HYP_RB_WRITABLE)
		rb_update_footers(cpu_buffer);
}

static int rb_cpu_enable(struct hyp_rb_per_cpu *cpu_buffer)
{
	unsigned int prev_status;

	if (!rb_cpu_loaded(cpu_buffer))
		return -EINVAL;

	prev_status = atomic_cmpxchg(&cpu_buffer->status,
				     HYP_RB_NONWRITABLE, HYP_RB_WRITABLE);

	if (prev_status == HYP_RB_NONWRITABLE)
		return 0;

	return -EINVAL;
}

static void rb_cpu_teardown(struct hyp_rb_per_cpu *cpu_buffer)
{
	int i;

	if (!rb_cpu_loaded(cpu_buffer))
		return;

	rb_cpu_disable(cpu_buffer);

	for (i = 0; i < cpu_buffer->nr_pages; i++) {
		struct hyp_buffer_page *bpage = &cpu_buffer->bpages[i];

		if (!bpage->page)
			continue;

		hyp_unpin_shared_mem((void *)bpage->page,
				     (void *)bpage->page + PAGE_SIZE);
	}

	cpu_buffer->bpages = NULL;
}

static bool rb_cpu_fits_backing(unsigned long nr_pages,
			        struct hyp_buffer_page *start)
{
	unsigned long max = hyp_buffer_pages_backing.start +
			    hyp_buffer_pages_backing.size;
	struct hyp_buffer_page *end = start + nr_pages;

	return (unsigned long)end <= max;
}

static bool rb_cpu_fits_pack(struct ring_buffer_pack *rb_pack,
			     unsigned long pack_end)
{
	unsigned long *end;

	/* Check we can at least read nr_pages */
	if ((unsigned long)&rb_pack->nr_pages >= pack_end)
		return false;

	end = &rb_pack->page_va[rb_pack->nr_pages];

	return (unsigned long)end <= pack_end;
}

static int rb_cpu_init(struct ring_buffer_pack *rb_pack, struct hyp_buffer_page *start,
		       struct hyp_rb_per_cpu *cpu_buffer)
{
	struct hyp_buffer_page *bpage = start;
	int i, ret;

	if (!rb_pack->nr_pages ||
	    !rb_cpu_fits_backing(rb_pack->nr_pages + 1, start))
		return -EINVAL;

	memset(cpu_buffer, 0, sizeof(*cpu_buffer));

	cpu_buffer->bpages = start;
	cpu_buffer->nr_pages = rb_pack->nr_pages + 1;

	/* The reader page is not part of the ring initially */
	ret = rb_page_init(bpage, rb_pack->reader_page_va);
	if (ret)
		return ret;

	cpu_buffer->reader_page = bpage;
	cpu_buffer->tail_page = bpage + 1;
	cpu_buffer->head_page = bpage + 1;

	for (i = 0; i < rb_pack->nr_pages; i++) {
		ret = rb_page_init(++bpage, rb_pack->page_va[i]);
		if (ret)
			goto err;

		bpage->list.next = &(bpage + 1)->list;
		bpage->list.prev = &(bpage - 1)->list;
	}

	/* Close the ring */
	bpage->list.next = &cpu_buffer->tail_page->list;
	cpu_buffer->tail_page->list.prev = &bpage->list;

	/* The last init'ed page points to the head page */
	rb_set_flag(bpage, HYP_RB_PAGE_HEAD);

	rb_footer_reader_status(cpu_buffer->reader_page, RB_PAGE_FT_READER);
	rb_footer_reader_status(cpu_buffer->head_page, RB_PAGE_FT_HEAD);
	rb_footer_writer_status(cpu_buffer->head_page, RB_PAGE_FT_COMMIT);

	atomic_set(&cpu_buffer->overrun, 0);
	atomic64_set(&cpu_buffer->write_stamp, 0);

	return 0;
err:
	rb_cpu_teardown(cpu_buffer);

	return ret;
}

static int rb_setup_bpage_backing(struct hyp_trace_pack *pack)
{
	unsigned long start = kern_hyp_va(pack->backing.start);
	size_t size = pack->backing.size;
	int ret;

	if (hyp_buffer_pages_backing.size)
		return -EBUSY;

	if (!PAGE_ALIGNED(start) || !PAGE_ALIGNED(size))
		return -EINVAL;

	ret = __pkvm_host_donate_hyp(hyp_virt_to_pfn((void *)start), size >> PAGE_SHIFT);
	if (ret)
		return ret;

	memset((void *)start, 0, size);

	hyp_buffer_pages_backing.start = start;
	hyp_buffer_pages_backing.size = size;

	return 0;
}

static void rb_teardown_bpage_backing(void)
{
	unsigned long start = hyp_buffer_pages_backing.start;
	size_t size = hyp_buffer_pages_backing.size;

	if (!size)
		return;

	memset((void *)start, 0, size);

	WARN_ON(__pkvm_hyp_donate_host(hyp_virt_to_pfn(start), size >> PAGE_SHIFT));

	hyp_buffer_pages_backing.start = 0;
	hyp_buffer_pages_backing.size = 0;
}

int __pkvm_rb_update_footers(int cpu)
{
	struct hyp_rb_per_cpu *cpu_buffer;
	int ret = 0;

	if (cpu >= hyp_nr_cpus)
		return -EINVAL;

	/* TODO: per-CPU lock for */
	hyp_spin_lock(&trace_rb_lock);

	cpu_buffer = per_cpu_ptr(&trace_rb, cpu);

	if (!rb_cpu_loaded(cpu_buffer))
		ret = -ENODEV;
	else
		ret = rb_update_footers(cpu_buffer);

	hyp_spin_unlock(&trace_rb_lock);

	return ret;
}

int __pkvm_rb_swap_reader_page(int cpu)
{
	struct hyp_rb_per_cpu *cpu_buffer = per_cpu_ptr(&trace_rb, cpu);
	int ret = 0;

	if (cpu >= hyp_nr_cpus)
		return -EINVAL;

	/* TODO: per-CPU lock for */
	hyp_spin_lock(&trace_rb_lock);

	cpu_buffer = per_cpu_ptr(&trace_rb, cpu);

	if (!rb_cpu_loaded(cpu_buffer))
		ret = -ENODEV;
	else
		ret = rb_swap_reader_page(cpu_buffer);

	hyp_spin_unlock(&trace_rb_lock);

	return ret;
}

static void __pkvm_teardown_tracing_locked(void)
{
	int cpu;

	hyp_assert_lock_held(&trace_rb_lock);

	for (cpu = 0; cpu < hyp_nr_cpus; cpu++) {
		struct hyp_rb_per_cpu *cpu_buffer = per_cpu_ptr(&trace_rb, cpu);

		rb_cpu_teardown(cpu_buffer);
	}

	rb_teardown_bpage_backing();
}

void __pkvm_teardown_tracing(void)
{
	hyp_spin_lock(&trace_rb_lock);
	__pkvm_teardown_tracing_locked();
	hyp_spin_unlock(&trace_rb_lock);
}

int __pkvm_load_tracing(unsigned long pack_hva, size_t pack_size)
{
	struct hyp_trace_pack *pack = (struct hyp_trace_pack *)kern_hyp_va(pack_hva);
	struct trace_buffer_pack *trace_pack = &pack->trace_buffer_pack;
	struct hyp_buffer_page *bpage_backing_start;
	struct ring_buffer_pack *rb_pack;
	int ret, cpu;

	if (!pack_size || !PAGE_ALIGNED(pack_hva) || !PAGE_ALIGNED(pack_size))
		return -EINVAL;

	ret = __pkvm_host_donate_hyp(hyp_virt_to_pfn((void *)pack),
				     pack_size >> PAGE_SHIFT);
	if (ret)
		return ret;

	hyp_spin_lock(&trace_rb_lock);

	ret = rb_setup_bpage_backing(pack);
	if (ret)
		goto err;

	trace_clock_update(&pack->trace_clock_data);

	bpage_backing_start = (struct hyp_buffer_page *)hyp_buffer_pages_backing.start;

	for_each_ring_buffer_pack(rb_pack, cpu, trace_pack) {
		struct hyp_rb_per_cpu *cpu_buffer;
		int cpu;

		ret = -EINVAL;
		if (!rb_cpu_fits_pack(rb_pack, pack_hva + pack_size))
			break;

		cpu = rb_pack->cpu;
		if (cpu >= hyp_nr_cpus)
			break;

		cpu_buffer = per_cpu_ptr(&trace_rb, cpu);

		ret = rb_cpu_init(rb_pack, bpage_backing_start, cpu_buffer);
		if (ret)
			break;

		/* reader page + nr pages in rb */
		bpage_backing_start += 1 + rb_pack->nr_pages;
	}
err:
	if (ret)
		__pkvm_teardown_tracing_locked();

	hyp_spin_unlock(&trace_rb_lock);

	WARN_ON(__pkvm_hyp_donate_host(hyp_virt_to_pfn((void *)pack),
				       pack_size >> PAGE_SHIFT));
	return ret;
}

int __pkvm_enable_tracing(bool enable)
{
	int cpu, ret = enable ? -EINVAL : 0;

	hyp_spin_lock(&trace_rb_lock);
	for (cpu = 0; cpu < hyp_nr_cpus; cpu++) {
		struct hyp_rb_per_cpu *cpu_buffer = per_cpu_ptr(&trace_rb, cpu);

		if (enable) {
			int __ret = rb_cpu_enable(cpu_buffer);

			if (!__ret)
				ret = 0;
		} else {
			rb_cpu_disable(cpu_buffer);
		}

	}
	hyp_spin_unlock(&trace_rb_lock);

	return ret;
}
