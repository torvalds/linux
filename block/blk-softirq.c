/*
 * Functions related to softirq rq completions
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/interrupt.h>
#include <linux/cpu.h>

#include "blk.h"

static DEFINE_PER_CPU(struct list_head, blk_cpu_done);

/*
 * Softirq action handler - move entries to local list and loop over them
 * while passing them to the queue registered handler.
 */
static void blk_done_softirq(struct softirq_action *h)
{
	struct list_head *cpu_list, local_list;

	local_irq_disable();
	cpu_list = &__get_cpu_var(blk_cpu_done);
	list_replace_init(cpu_list, &local_list);
	local_irq_enable();

	while (!list_empty(&local_list)) {
		struct request *rq;

		rq = list_entry(local_list.next, struct request, csd.list);
		list_del_init(&rq->csd.list);
		rq->q->softirq_done_fn(rq);
	}
}

#if defined(CONFIG_SMP) && defined(CONFIG_USE_GENERIC_SMP_HELPERS)
static void trigger_softirq(void *data)
{
	struct request *rq = data;
	unsigned long flags;
	struct list_head *list;

	local_irq_save(flags);
	list = &__get_cpu_var(blk_cpu_done);
	list_add_tail(&rq->csd.list, list);

	if (list->next == &rq->csd.list)
		raise_softirq_irqoff(BLOCK_SOFTIRQ);

	local_irq_restore(flags);
}

/*
 * Setup and invoke a run of 'trigger_softirq' on the given cpu.
 */
static int raise_blk_irq(int cpu, struct request *rq)
{
	if (cpu_online(cpu)) {
		struct call_single_data *data = &rq->csd;

		data->func = trigger_softirq;
		data->info = rq;
		data->flags = 0;

		__smp_call_function_single(cpu, data);
		return 0;
	}

	return 1;
}
#else /* CONFIG_SMP && CONFIG_USE_GENERIC_SMP_HELPERS */
static int raise_blk_irq(int cpu, struct request *rq)
{
	return 1;
}
#endif

static int __cpuinit blk_cpu_notify(struct notifier_block *self,
				    unsigned long action, void *hcpu)
{
	/*
	 * If a CPU goes away, splice its entries to the current CPU
	 * and trigger a run of the softirq
	 */
	if (action == CPU_DEAD || action == CPU_DEAD_FROZEN) {
		int cpu = (unsigned long) hcpu;

		local_irq_disable();
		list_splice_init(&per_cpu(blk_cpu_done, cpu),
				 &__get_cpu_var(blk_cpu_done));
		raise_softirq_irqoff(BLOCK_SOFTIRQ);
		local_irq_enable();
	}

	return NOTIFY_OK;
}

static struct notifier_block __cpuinitdata blk_cpu_notifier = {
	.notifier_call	= blk_cpu_notify,
};

void __blk_complete_request(struct request *req)
{
	struct request_queue *q = req->q;
	unsigned long flags;
	int ccpu, cpu, group_cpu;

	BUG_ON(!q->softirq_done_fn);

	local_irq_save(flags);
	cpu = smp_processor_id();
	group_cpu = blk_cpu_to_group(cpu);

	/*
	 * Select completion CPU
	 */
	if (test_bit(QUEUE_FLAG_SAME_COMP, &q->queue_flags) && req->cpu != -1)
		ccpu = req->cpu;
	else
		ccpu = cpu;

	if (ccpu == cpu || ccpu == group_cpu) {
		struct list_head *list;
do_local:
		list = &__get_cpu_var(blk_cpu_done);
		list_add_tail(&req->csd.list, list);

		/*
		 * if the list only contains our just added request,
		 * signal a raise of the softirq. If there are already
		 * entries there, someone already raised the irq but it
		 * hasn't run yet.
		 */
		if (list->next == &req->csd.list)
			raise_softirq_irqoff(BLOCK_SOFTIRQ);
	} else if (raise_blk_irq(ccpu, req))
		goto do_local;

	local_irq_restore(flags);
}

/**
 * blk_complete_request - end I/O on a request
 * @req:      the request being processed
 *
 * Description:
 *     Ends all I/O on a request. It does not handle partial completions,
 *     unless the driver actually implements this in its completion callback
 *     through requeueing. The actual completion happens out-of-order,
 *     through a softirq handler. The user must have registered a completion
 *     callback through blk_queue_softirq_done().
 **/
void blk_complete_request(struct request *req)
{
	if (!blk_mark_rq_complete(req))
		__blk_complete_request(req);
}
EXPORT_SYMBOL(blk_complete_request);

__init int blk_softirq_init(void)
{
	int i;

	for_each_possible_cpu(i)
		INIT_LIST_HEAD(&per_cpu(blk_cpu_done, i));

	open_softirq(BLOCK_SOFTIRQ, blk_done_softirq);
	register_hotcpu_notifier(&blk_cpu_notifier);
	return 0;
}
subsys_initcall(blk_softirq_init);
