/*
 * This is for all the tests related to logic bugs (e.g. bad dereferences,
 * bad alignment, bad loops, bad locking, bad scheduling, deep stacks, and
 * lockups) along with other things that don't fit well into existing LKDTM
 * test source files.
 */
#include "lkdtm.h"
#include <linux/sched.h>

/*
 * Make sure our attempts to over run the kernel stack doesn't trigger
 * a compiler warning when CONFIG_FRAME_WARN is set. Then make sure we
 * recurse past the end of THREAD_SIZE by default.
 */
#if defined(CONFIG_FRAME_WARN) && (CONFIG_FRAME_WARN > 0)
#define REC_STACK_SIZE (CONFIG_FRAME_WARN / 2)
#else
#define REC_STACK_SIZE (THREAD_SIZE / 8)
#endif
#define REC_NUM_DEFAULT ((THREAD_SIZE / REC_STACK_SIZE) * 2)

static int recur_count = REC_NUM_DEFAULT;

static DEFINE_SPINLOCK(lock_me_up);

static int recursive_loop(int remaining)
{
	char buf[REC_STACK_SIZE];

	/* Make sure compiler does not optimize this away. */
	memset(buf, (remaining & 0xff) | 0x1, REC_STACK_SIZE);
	if (!remaining)
		return 0;
	else
		return recursive_loop(remaining - 1);
}

/* If the depth is negative, use the default, otherwise keep parameter. */
void __init lkdtm_bugs_init(int *recur_param)
{
	if (*recur_param < 0)
		*recur_param = recur_count;
	else
		recur_count = *recur_param;
}

void lkdtm_PANIC(void)
{
	panic("dumptest");
}

void lkdtm_BUG(void)
{
	BUG();
}

void lkdtm_WARNING(void)
{
	WARN_ON(1);
}

void lkdtm_EXCEPTION(void)
{
	*((int *) 0) = 0;
}

void lkdtm_LOOP(void)
{
	for (;;)
		;
}

void lkdtm_OVERFLOW(void)
{
	(void) recursive_loop(recur_count);
}

noinline void lkdtm_CORRUPT_STACK(void)
{
	/* Use default char array length that triggers stack protection. */
	char data[8];

	memset((void *)data, 0, 64);
}

void lkdtm_UNALIGNED_LOAD_STORE_WRITE(void)
{
	static u8 data[5] __attribute__((aligned(4))) = {1, 2, 3, 4, 5};
	u32 *p;
	u32 val = 0x12345678;

	p = (u32 *)(data + 1);
	if (*p == 0)
		val = 0x87654321;
	*p = val;
}

void lkdtm_SOFTLOCKUP(void)
{
	preempt_disable();
	for (;;)
		cpu_relax();
}

void lkdtm_HARDLOCKUP(void)
{
	local_irq_disable();
	for (;;)
		cpu_relax();
}

void lkdtm_SPINLOCKUP(void)
{
	/* Must be called twice to trigger. */
	spin_lock(&lock_me_up);
	/* Let sparse know we intended to exit holding the lock. */
	__release(&lock_me_up);
}

void lkdtm_HUNG_TASK(void)
{
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule();
}

void lkdtm_ATOMIC_UNDERFLOW(void)
{
	atomic_t under = ATOMIC_INIT(INT_MIN);

	pr_info("attempting good atomic increment\n");
	atomic_inc(&under);
	atomic_dec(&under);

	pr_info("attempting bad atomic underflow\n");
	atomic_dec(&under);
}

void lkdtm_ATOMIC_OVERFLOW(void)
{
	atomic_t over = ATOMIC_INIT(INT_MAX);

	pr_info("attempting good atomic decrement\n");
	atomic_dec(&over);
	atomic_inc(&over);

	pr_info("attempting bad atomic overflow\n");
	atomic_inc(&over);
}
