#ifndef __ARM64_KVM_HYP_NVHE_TRACE_H
#define __ARM64_KVM_HYP_NVHE_TRACE_H
#include <linux/ring_buffer.h>

#include <asm/kvm_hyptrace.h>
#include <asm/percpu.h>

#ifdef CONFIG_TRACING

struct hyp_buffer_page {
	struct list_head list;
	struct buffer_data_page *page;
	atomic_t write;
	atomic_t entries;
};

#define HYP_RB_UNUSED	0
#define HYP_RB_READY	1
#define HYP_RB_WRITE	2

struct hyp_rb_per_cpu {
	struct hyp_buffer_page *tail_page;
	struct hyp_buffer_page *reader_page;
	struct hyp_buffer_page *head_page;
	struct hyp_buffer_page *bpages;
	unsigned long nr_pages;
	atomic64_t write_stamp;
	atomic_t pages_touched;
	atomic_t nr_entries;
	atomic_t status;
	atomic_t overrun;
};

static inline bool __start_write_hyp_rb(struct hyp_rb_per_cpu *rb)
{
	/*
	 * Paired with rb_cpu_init()
	 */
	return atomic_cmpxchg_acquire(&rb->status, HYP_RB_READY, HYP_RB_WRITE)
		!= HYP_RB_UNUSED;
}

static inline void __stop_write_hyp_rb(struct hyp_rb_per_cpu *rb)
{
	/*
	 * Paired with rb_cpu_teardown()
	 */
	atomic_set_release(&rb->status, HYP_RB_READY);
}

struct hyp_rb_per_cpu;
DECLARE_PER_CPU(struct hyp_rb_per_cpu, trace_rb);

void *rb_reserve_trace_entry(struct hyp_rb_per_cpu *cpu_buffer, unsigned long length);

int __pkvm_start_tracing(unsigned long pack_va, size_t pack_size);
void __pkvm_stop_tracing(void);
int __pkvm_rb_swap_reader_page(int cpu);
int __pkvm_rb_update_footers(int cpu);
#else
static inline int __pkvm_start_tracing(unsigned long pack_va, size_t pack_size)
{
	return -ENODEV;
}

static inline void __pkvm_stop_tracing(void) { }

static inline int __pkvm_rb_swap_reader_page(int cpu)
{
	return -ENODEV;
}

static inline int __pkvm_rb_update_footers(int cpu)
{
	return -ENODEV;
}
#endif
#endif
