#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/string.h>

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

static void led_blink(unsigned long timeout)
{
	led_toggle();

	/* reschedule */
	if (!timeout) { /* blink according to load */
		led_blink_timer.expires = jiffies +
			((1 + (avenrun[0] >> FSHIFT)) * HZ);
		led_blink_timer.data = 0;
	} else { /* blink at user specified interval */
		led_blink_timer.expires = jiffies + (timeout * HZ);
		led_blink_timer.data = timeout;
	}
	add_timer(&led_blink_timer);
}

static int led_read_proc(char *buf, char **start, off_t offset, int count,
			 int *eof, void *data)
{
	int len = 0;

	if (get_auxio() & AUXIO_LED)
		len = sprintf(buf, "on\n");
	else
		len = sprintf(buf, "off\n");

	return len;
}

static int led_write_proc(struct file *file, const char __user *buffer,
			  unsigned long count, void *data)
{
	char *buf = NULL;

	if (count > LED_MAX_LENGTH)
		count = LED_MAX_LENGTH;

	buf = kmalloc(sizeof(char) * (count + 1), GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	if (copy_from_user(buf, buffer, count)) {
		kfree(buf);
		return -EFAULT;
	}

	buf[count] = '\0';

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
		led_blink(simple_strtoul(buf, NULL, 10));
	} else if (!strcmp(buf, "load")) {
		led_blink(0);
	} else {
		auxio_set_led(AUXIO_LED_OFF);
	}

	kfree(buf);

	return count;
}

static struct proc_dir_entry *led;

#define LED_VERSION	"0.1"

static int __init led_init(void)
{
	init_timer(&led_blink_timer);
	led_blink_timer.function = led_blink;

	led = create_proc_entry("led", 0, NULL);
	if (!led)
		return -ENOMEM;

	led->read_proc = led_read_proc; /* reader function */
	led->write_proc = led_write_proc; /* writer function */
	led->owner = THIS_MODULE;

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
