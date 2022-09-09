// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/hrtimer.h>
#include <linux/fs.h>
#include <linux/debugfs.h>
#include <linux/export.h>
#include <linux/spinlock.h>
#include <asm/debug.h>

static int ss_get(void *data, u64 *val)
{
	ktime_t start, finish;
	int loops;
	int cont;
	DEFINE_RAW_SPINLOCK(ss_spin);

	loops = 1000000;
	cont = 1;

	start = ktime_get();

	while (cont) {
		raw_spin_lock(&ss_spin);
		loops--;
		if (loops == 0)
			cont = 0;
		raw_spin_unlock(&ss_spin);
	}

	finish = ktime_get();

	*val = ktime_us_delta(finish, start);

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_ss, ss_get, NULL, "%llu\n");



struct spin_multi_state {
	raw_spinlock_t lock;
	atomic_t start_wait;
	atomic_t enter_wait;
	atomic_t exit_wait;
	int loops;
};

struct spin_multi_per_thread {
	struct spin_multi_state *state;
	ktime_t start;
};

static int multi_other(void *data)
{
	int loops;
	int cont;
	struct spin_multi_per_thread *pt = data;
	struct spin_multi_state *s = pt->state;

	loops = s->loops;
	cont = 1;

	atomic_dec(&s->enter_wait);

	while (atomic_read(&s->enter_wait))
		; /* spin */

	pt->start = ktime_get();

	atomic_dec(&s->start_wait);

	while (atomic_read(&s->start_wait))
		; /* spin */

	while (cont) {
		raw_spin_lock(&s->lock);
		loops--;
		if (loops == 0)
			cont = 0;
		raw_spin_unlock(&s->lock);
	}

	atomic_dec(&s->exit_wait);
	while (atomic_read(&s->exit_wait))
		; /* spin */
	return 0;
}

static int multi_get(void *data, u64 *val)
{
	ktime_t finish;
	struct spin_multi_state ms;
	struct spin_multi_per_thread t1, t2;

	ms.lock = __RAW_SPIN_LOCK_UNLOCKED("multi_get");
	ms.loops = 1000000;

	atomic_set(&ms.start_wait, 2);
	atomic_set(&ms.enter_wait, 2);
	atomic_set(&ms.exit_wait, 2);
	t1.state = &ms;
	t2.state = &ms;

	kthread_run(multi_other, &t2, "multi_get");

	multi_other(&t1);

	finish = ktime_get();

	*val = ktime_us_delta(finish, t1.start);

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_multi, multi_get, NULL, "%llu\n");

static int __init spinlock_test(void)
{
	debugfs_create_file_unsafe("spin_single", S_IRUGO, mips_debugfs_dir, NULL,
			    &fops_ss);
	debugfs_create_file_unsafe("spin_multi", S_IRUGO, mips_debugfs_dir, NULL,
			    &fops_multi);
	return 0;
}
device_initcall(spinlock_test);
