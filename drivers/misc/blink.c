#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/timer.h>
#include <linux/jiffies.h>

static void do_blink(unsigned long data);

static DEFINE_TIMER(blink_timer, do_blink, 0 ,0);

static void do_blink(unsigned long data)
{
	static long count;
	if (panic_blink)
		panic_blink(count++);
	blink_timer.expires = jiffies + msecs_to_jiffies(1);
	add_timer(&blink_timer);
}

static int blink_panic_event(struct notifier_block *blk,
			     unsigned long event, void *arg)
{
	do_blink(0);
	return 0;
}

static struct notifier_block blink_notify = {
	.notifier_call = blink_panic_event,
};

static __init int blink_init(void)
{
	printk(KERN_INFO "Enabling keyboard blinking\n");
	atomic_notifier_chain_register(&panic_notifier_list, &blink_notify);
	return 0;
}

static __exit void blink_remove(void)
{
	del_timer_sync(&blink_timer);
	atomic_notifier_chain_unregister(&panic_notifier_list, &blink_notify);
}

module_init(blink_init);
module_exit(blink_remove);

