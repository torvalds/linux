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


static struct notifier_block blk_cpu_notifier __cpuinitdata = {
	.notifier_call	= blk_cpu_notify,
};

/*
 * splice the completion data to a local structure and hand off to
 * process_completion_queue() to complete the requests
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

		rq = list_entry(local_list.next, struct request, donelist);
		list_del_init(&rq->donelist);
		rq->q->softirq_done_fn(rq);
	}
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
	struct list_head *cpu_list;
	unsigned long flags;

	BUG_ON(!req->q->softirq_done_fn);

	local_irq_save(flags);

	cpu_list = &__get_cpu_var(blk_cpu_done);
	list_add_tail(&req->donelist, cpu_list);
	raise_softirq_irqoff(BLOCK_SOFTIRQ);

	local_irq_restore(flags);
}
EXPORT_SYMBOL(blk_complete_request);

int __init blk_softirq_init(void)
{
	int i;

	for_each_possible_cpu(i)
		INIT_LIST_HEAD(&per_cpu(blk_cpu_done, i));

	open_softirq(BLOCK_SOFTIRQ, blk_done_softirq);
	register_hotcpu_notifier(&blk_cpu_notifier);
	return 0;
}
subsys_initcall(blk_softirq_init);
