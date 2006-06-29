/**
 * @file oprof.c
 *
 * @remark Copyright 2002 OProfile authors
 * @remark Read the file COPYING
 *
 * @author John Levon <levon@movementarian.org>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/oprofile.h>
#include <linux/moduleparam.h>
#include <asm/mutex.h>

#include "oprof.h"
#include "event_buffer.h"
#include "cpu_buffer.h"
#include "buffer_sync.h"
#include "oprofile_stats.h"
 
struct oprofile_operations oprofile_ops;

unsigned long oprofile_started;
unsigned long backtrace_depth;
static unsigned long is_setup;
static DEFINE_MUTEX(start_mutex);

/* timer
   0 - use performance monitoring hardware if available
   1 - use the timer int mechanism regardless
 */
static int timer = 0;

int oprofile_setup(void)
{
	int err;
 
	mutex_lock(&start_mutex);

	if ((err = alloc_cpu_buffers()))
		goto out;

	if ((err = alloc_event_buffer()))
		goto out1;
 
	if (oprofile_ops.setup && (err = oprofile_ops.setup()))
		goto out2;
 
	/* Note even though this starts part of the
	 * profiling overhead, it's necessary to prevent
	 * us missing task deaths and eventually oopsing
	 * when trying to process the event buffer.
	 */
	if ((err = sync_start()))
		goto out3;

	is_setup = 1;
	mutex_unlock(&start_mutex);
	return 0;
 
out3:
	if (oprofile_ops.shutdown)
		oprofile_ops.shutdown();
out2:
	free_event_buffer();
out1:
	free_cpu_buffers();
out:
	mutex_unlock(&start_mutex);
	return err;
}


/* Actually start profiling (echo 1>/dev/oprofile/enable) */
int oprofile_start(void)
{
	int err = -EINVAL;
 
	mutex_lock(&start_mutex);
 
	if (!is_setup)
		goto out;

	err = 0; 
 
	if (oprofile_started)
		goto out;
 
	oprofile_reset_stats();

	if ((err = oprofile_ops.start()))
		goto out;

	oprofile_started = 1;
out:
	mutex_unlock(&start_mutex);
	return err;
}

 
/* echo 0>/dev/oprofile/enable */
void oprofile_stop(void)
{
	mutex_lock(&start_mutex);
	if (!oprofile_started)
		goto out;
	oprofile_ops.stop();
	oprofile_started = 0;
	/* wake up the daemon to read what remains */
	wake_up_buffer_waiter();
out:
	mutex_unlock(&start_mutex);
}


void oprofile_shutdown(void)
{
	mutex_lock(&start_mutex);
	sync_stop();
	if (oprofile_ops.shutdown)
		oprofile_ops.shutdown();
	is_setup = 0;
	free_event_buffer();
	free_cpu_buffers();
	mutex_unlock(&start_mutex);
}


int oprofile_set_backtrace(unsigned long val)
{
	int err = 0;

	mutex_lock(&start_mutex);

	if (oprofile_started) {
		err = -EBUSY;
		goto out;
	}

	if (!oprofile_ops.backtrace) {
		err = -EINVAL;
		goto out;
	}

	backtrace_depth = val;

out:
	mutex_unlock(&start_mutex);
	return err;
}

static int __init oprofile_init(void)
{
	int err;

	err = oprofile_arch_init(&oprofile_ops);

	if (err < 0 || timer) {
		printk(KERN_INFO "oprofile: using timer interrupt.\n");
		oprofile_timer_init(&oprofile_ops);
	}

	err = oprofilefs_register();
	if (err)
		oprofile_arch_exit();

	return err;
}


static void __exit oprofile_exit(void)
{
	oprofilefs_unregister();
	oprofile_arch_exit();
}

 
module_init(oprofile_init);
module_exit(oprofile_exit);

module_param_named(timer, timer, int, 0644);
MODULE_PARM_DESC(timer, "force use of timer interrupt");
 
MODULE_LICENSE("GPL");
MODULE_AUTHOR("John Levon <levon@movementarian.org>");
MODULE_DESCRIPTION("OProfile system profiler");
