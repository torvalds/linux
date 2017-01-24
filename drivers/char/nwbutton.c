/*
 * 	NetWinder Button Driver-
 *	Copyright (C) Alex Holden <alex@linuxhacker.org> 1998, 1999.
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/time.h>
#include <linux/timer.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/init.h>

#include <linux/uaccess.h>
#include <asm/irq.h>
#include <asm/mach-types.h>

#define __NWBUTTON_C		/* Tell the header file who we are */
#include "nwbutton.h"

static void button_sequence_finished (unsigned long parameters);

static int button_press_count;		/* The count of button presses */
/* Times for the end of a sequence */
static DEFINE_TIMER(button_timer, button_sequence_finished, 0, 0);
static DECLARE_WAIT_QUEUE_HEAD(button_wait_queue); /* Used for blocking read */
static char button_output_buffer[32];	/* Stores data to write out of device */
static int bcount;			/* The number of bytes in the buffer */
static int bdelay = BUTTON_DELAY;	/* The delay, in jiffies */
static struct button_callback button_callback_list[32]; /* The callback list */
static int callback_count;		/* The number of callbacks registered */
static int reboot_count = NUM_PRESSES_REBOOT; /* Number of presses to reboot */

/*
 * This function is called by other drivers to register a callback function
 * to be called when a particular number of button presses occurs.
 * The callback list is a static array of 32 entries (I somehow doubt many
 * people are ever going to want to register more than 32 different actions
 * to be performed by the kernel on different numbers of button presses ;).
 * However, if an attempt to register a 33rd entry (perhaps a stuck loop
 * somewhere registering the same entry over and over?) it will fail to
 * do so and return -ENOMEM. If an attempt is made to register a null pointer,
 * it will fail to do so and return -EINVAL.
 * Because callbacks can be unregistered at random the list can become
 * fragmented, so we need to search through the list until we find the first
 * free entry.
 *
 * FIXME: Has anyone spotted any locking functions int his code recently ??
 */

int button_add_callback (void (*callback) (void), int count)
{
	int lp = 0;
	if (callback_count == 32) {
		return -ENOMEM;
	}
	if (!callback) {
		return -EINVAL;
	}
	callback_count++;
	for (; (button_callback_list [lp].callback); lp++);
	button_callback_list [lp].callback = callback;
	button_callback_list [lp].count = count;
	return 0;
}

/*
 * This function is called by other drivers to deregister a callback function.
 * If you attempt to unregister a callback which does not exist, it will fail
 * with -EINVAL. If there is more than one entry with the same address,
 * because it searches the list from end to beginning, it will unregister the
 * last one to be registered first (FILO- First In Last Out).
 * Note that this is not necessarily true if the entries are not submitted
 * at the same time, because another driver could have unregistered a callback
 * between the submissions creating a gap earlier in the list, which would
 * be filled first at submission time.
 */

int button_del_callback (void (*callback) (void))
{
	int lp = 31;
	if (!callback) {
		return -EINVAL;
	}
	while (lp >= 0) {
		if ((button_callback_list [lp].callback) == callback) {
			button_callback_list [lp].callback = NULL;
			button_callback_list [lp].count = 0;
			callback_count--;
			return 0;
		}
		lp--;
	}
	return -EINVAL;
}

/*
 * This function is called by button_sequence_finished to search through the
 * list of callback functions, and call any of them whose count argument
 * matches the current count of button presses. It starts at the beginning
 * of the list and works up to the end. It will refuse to follow a null
 * pointer (which should never happen anyway).
 */

static void button_consume_callbacks (int bpcount)
{
	int lp = 0;
	for (; lp <= 31; lp++) {
		if ((button_callback_list [lp].count) == bpcount) {
			if (button_callback_list [lp].callback) {
				button_callback_list[lp].callback();
			}
		}
	}
}

/* 
 * This function is called when the button_timer times out.
 * ie. When you don't press the button for bdelay jiffies, this is taken to
 * mean you have ended the sequence of key presses, and this function is
 * called to wind things up (write the press_count out to /dev/button, call
 * any matching registered function callbacks, initiate reboot, etc.).
 */

static void button_sequence_finished (unsigned long parameters)
{
	if (IS_ENABLED(CONFIG_NWBUTTON_REBOOT) &&
	    button_press_count == reboot_count)
		kill_cad_pid(SIGINT, 1);	/* Ask init to reboot us */
	button_consume_callbacks (button_press_count);
	bcount = sprintf (button_output_buffer, "%d\n", button_press_count);
	button_press_count = 0;		/* Reset the button press counter */
	wake_up_interruptible (&button_wait_queue);
}

/* 
 *  This handler is called when the orange button is pressed (GPIO 10 of the
 *  SuperIO chip, which maps to logical IRQ 26). If the press_count is 0,
 *  this is the first press, so it starts a timer and increments the counter.
 *  If it is higher than 0, it deletes the old timer, starts a new one, and
 *  increments the counter.
 */ 

static irqreturn_t button_handler (int irq, void *dev_id)
{
	button_press_count++;
	mod_timer(&button_timer, jiffies + bdelay);

	return IRQ_HANDLED;
}

/*
 * This function is called when a user space program attempts to read
 * /dev/nwbutton. It puts the device to sleep on the wait queue until
 * button_sequence_finished writes some data to the buffer and flushes
 * the queue, at which point it writes the data out to the device and
 * returns the number of characters it has written. This function is
 * reentrant, so that many processes can be attempting to read from the
 * device at any one time.
 */

static int button_read (struct file *filp, char __user *buffer,
			size_t count, loff_t *ppos)
{
	DEFINE_WAIT(wait);
	prepare_to_wait(&button_wait_queue, &wait, TASK_INTERRUPTIBLE);
	schedule();
	finish_wait(&button_wait_queue, &wait);
	return (copy_to_user (buffer, &button_output_buffer, bcount))
		 ? -EFAULT : bcount;
}

/* 
 * This structure is the file operations structure, which specifies what
 * callbacks functions the kernel should call when a user mode process
 * attempts to perform these operations on the device.
 */

static const struct file_operations button_fops = {
	.owner		= THIS_MODULE,
	.read		= button_read,
	.llseek		= noop_llseek,
};

/* 
 * This structure is the misc device structure, which specifies the minor
 * device number (158 in this case), the name of the device (for /proc/misc),
 * and the address of the above file operations structure.
 */

static struct miscdevice button_misc_device = {
	BUTTON_MINOR,
	"nwbutton",
	&button_fops,
};

/*
 * This function is called to initialise the driver, either from misc.c at
 * bootup if the driver is compiled into the kernel, or from init_module
 * below at module insert time. It attempts to register the device node
 * and the IRQ and fails with a warning message if either fails, though
 * neither ever should because the device number and IRQ are unique to
 * this driver.
 */

static int __init nwbutton_init(void)
{
	if (!machine_is_netwinder())
		return -ENODEV;

	printk (KERN_INFO "NetWinder Button Driver Version %s (C) Alex Holden "
			"<alex@linuxhacker.org> 1998.\n", VERSION);

	if (misc_register (&button_misc_device)) {
		printk (KERN_WARNING "nwbutton: Couldn't register device 10, "
				"%d.\n", BUTTON_MINOR);
		return -EBUSY;
	}

	if (request_irq (IRQ_NETWINDER_BUTTON, button_handler, 0,
			"nwbutton", NULL)) {
		printk (KERN_WARNING "nwbutton: IRQ %d is not free.\n",
				IRQ_NETWINDER_BUTTON);
		misc_deregister (&button_misc_device);
		return -EIO;
	}
	return 0;
}

static void __exit nwbutton_exit (void) 
{
	free_irq (IRQ_NETWINDER_BUTTON, NULL);
	misc_deregister (&button_misc_device);
}


MODULE_AUTHOR("Alex Holden");
MODULE_LICENSE("GPL");

module_init(nwbutton_init);
module_exit(nwbutton_exit);
