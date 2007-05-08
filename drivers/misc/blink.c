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

static int blink_init(void)
{
	printk(KERN_INFO "Enabling keyboard blinking\n");
	do_blink(0);
	return 0;
}

module_init(blink_init);

