#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/jiffies.h>
#include <linux/timer.h>
#include <linux/uaccess.h>
#include <linux/sched/loadavg.h>

#include <asm/auxio.h>

#define LED_MAX_LENGTH 8 /* maximum chars written to proc file */

static inline void led_toggle(void)
{
	unsigned char val = get_auxio();
	unsigned char on, off;

	if (val & AUXIO_LED) {
		on = 0;
		off = AUXIO_LED;
	} else {
		on = AUXIO_LED;
		off = 0;
	}

	set_auxio(on, off);
}

static struct timer_list led_blink_timer;
static unsigned long led_blink_timer_timeout;

static void led_blink(struct timer_list *unused)
{
	unsigned long timeout = led_blink_timer_timeout;

	led_toggle();

	/* reschedule */
	if (!timeout) { /* blink according to load */
		led_blink_timer.expires = jiffies +
			((1 + (avenrun[0] >> FSHIFT)) * HZ);
	} else { /* blink at user specified interval */
		led_blink_timer.expires = jiffies + (timeout * HZ);
	}
	add_timer(&led_blink_timer);
}

static int led_proc_show(struct seq_file *m, void *v)
{
	if (get_auxio() & AUXIO_LED)
		seq_puts(m, "on\n");
	else
		seq_puts(m, "off\n");
	return 0;
}

static int led_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, led_proc_show, NULL);
}

static ssize_t led_proc_write(struct file *file, const char __user *buffer,
			      size_t count, loff_t *ppos)
{
	char *buf = NULL;

	if (count > LED_MAX_LENGTH)
		count = LED_MAX_LENGTH;

	buf = memdup_user_nul(buffer, count);
	if (IS_ERR(buf))
		return PTR_ERR(buf);

	/* work around \n when echo'ing into proc */
	if (buf[count - 1] == '\n')
		buf[count - 1] = '\0';

	/* before we change anything we want to stop any running timers,
	 * otherwise calls such as on will have no persistent effect
	 */
	del_timer_sync(&led_blink_timer);

	if (!strcmp(buf, "on")) {
		auxio_set_led(AUXIO_LED_ON);
	} else if (!strcmp(buf, "toggle")) {
		led_toggle();
	} else if ((*buf > '0') && (*buf <= '9')) {
		led_blink_timer_timeout = simple_strtoul(buf, NULL, 10);
		led_blink(&led_blink_timer);
	} else if (!strcmp(buf, "load")) {
		led_blink_timer_timeout = 0;
		led_blink(&led_blink_timer);
	} else {
		auxio_set_led(AUXIO_LED_OFF);
	}

	kfree(buf);

	return count;
}

static const struct file_operations led_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= led_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
	.write		= led_proc_write,
};

static struct proc_dir_entry *led;

#define LED_VERSION	"0.1"

static int __init led_init(void)
{
	timer_setup(&led_blink_timer, led_blink, 0);

	led = proc_create("led", 0, NULL, &led_proc_fops);
	if (!led)
		return -ENOMEM;

	printk(KERN_INFO
	       "led: version %s, Lars Kotthoff <metalhead@metalhead.ws>\n",
	       LED_VERSION);

	return 0;
}

static void __exit led_exit(void)
{
	remove_proc_entry("led", NULL);
	del_timer_sync(&led_blink_timer);
}

module_init(led_init);
module_exit(led_exit);

MODULE_AUTHOR("Lars Kotthoff <metalhead@metalhead.ws>");
MODULE_DESCRIPTION("Provides control of the front LED on SPARC systems.");
MODULE_LICENSE("GPL");
MODULE_VERSION(LED_VERSION);
