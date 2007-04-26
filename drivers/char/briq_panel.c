/*
 * Drivers for the Total Impact PPC based computer "BRIQ"
 * by Dr. Karsten Jeppesen
 *
 */

#include <linux/module.h>

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/tty.h>
#include <linux/timer.h>
#include <linux/kernel.h>
#include <linux/wait.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/init.h>

#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/prom.h>

#define		BRIQ_PANEL_MINOR	156
#define		BRIQ_PANEL_VFD_IOPORT	0x0390
#define		BRIQ_PANEL_LED_IOPORT	0x0398
#define		BRIQ_PANEL_VER		"1.1 (04/20/2002)"
#define		BRIQ_PANEL_MSG0		"Loading Linux"

static int		vfd_is_open;
static unsigned char	vfd[40];
static int		vfd_cursor;
static unsigned char	ledpb, led;

static void update_vfd(void)
{
	int	i;

	/* cursor home */
	outb(0x02, BRIQ_PANEL_VFD_IOPORT);
	for (i=0; i<20; i++)
		outb(vfd[i], BRIQ_PANEL_VFD_IOPORT + 1);

	/* cursor to next line */
	outb(0xc0, BRIQ_PANEL_VFD_IOPORT);
	for (i=20; i<40; i++)
		outb(vfd[i], BRIQ_PANEL_VFD_IOPORT + 1);

}

static void set_led(char state)
{
	if (state == 'R')
		led = 0x01;
	else if (state == 'G')
		led = 0x02;
	else if (state == 'Y')
		led = 0x03;
	else if (state == 'X')
		led = 0x00;
	outb(led, BRIQ_PANEL_LED_IOPORT);
}

static int briq_panel_open(struct inode *ino, struct file *filep)
{
	/* enforce single access */
	if (vfd_is_open)
		return -EBUSY;
	vfd_is_open = 1;

	return 0;
}

static int briq_panel_release(struct inode *ino, struct file *filep)
{
	if (!vfd_is_open)
		return -ENODEV;

	vfd_is_open = 0;

	return 0;
}

static ssize_t briq_panel_read(struct file *file, char __user *buf, size_t count,
			 loff_t *ppos)
{
	unsigned short c;
	unsigned char cp;

#if 0	/*  Can't seek (pread) on this device  */
	if (ppos != &file->f_pos)
		return -ESPIPE;
#endif

	if (!vfd_is_open)
		return -ENODEV;

	c = (inb(BRIQ_PANEL_LED_IOPORT) & 0x000c) | (ledpb & 0x0003);
	set_led(' ');
	/* upper button released */
	if ((!(ledpb & 0x0004)) && (c & 0x0004)) {
		cp = ' ';
		ledpb = c;
		if (copy_to_user(buf, &cp, 1))
			return -EFAULT;
		return 1;
	}
	/* lower button released */
	else if ((!(ledpb & 0x0008)) && (c & 0x0008)) {
		cp = '\r';
		ledpb = c;
		if (copy_to_user(buf, &cp, 1))
			return -EFAULT;
		return 1;
	} else {
		ledpb = c;
		return 0;
	}
}

static void scroll_vfd( void )
{
	int	i;

	for (i=0; i<20; i++) {
		vfd[i] = vfd[i+20];
		vfd[i+20] = ' ';
	}
	vfd_cursor = 20;
}

static ssize_t briq_panel_write(struct file *file, const char __user *buf, size_t len,
			  loff_t *ppos)
{
	size_t indx = len;
	int i, esc = 0;

#if 0	/*  Can't seek (pwrite) on this device  */
	if (ppos != &file->f_pos)
		return -ESPIPE;
#endif

	if (!vfd_is_open)
		return -EBUSY;

	for (;;) {
		char c;
		if (!indx)
			break;
		if (get_user(c, buf))
			return -EFAULT;
		if (esc) {
			set_led(c);
			esc = 0;
		} else if (c == 27) {
			esc = 1;
		} else if (c == 12) {
			/* do a form feed */
			for (i=0; i<40; i++)
				vfd[i] = ' ';
			vfd_cursor = 0;
		} else if (c == 10) {
			if (vfd_cursor < 20)
				vfd_cursor = 20;
			else if (vfd_cursor < 40)
				vfd_cursor = 40;
			else if (vfd_cursor < 60)
				vfd_cursor = 60;
			if (vfd_cursor > 59)
				scroll_vfd();
		} else {
			/* just a character */
			if (vfd_cursor > 39)
				scroll_vfd();
			vfd[vfd_cursor++] = c;
		}
		indx--;
		buf++;
	}
	update_vfd();

	return len;
}

static const struct file_operations briq_panel_fops = {
	.owner		= THIS_MODULE,
	.read		= briq_panel_read,
	.write		= briq_panel_write,
	.open		= briq_panel_open,
	.release	= briq_panel_release,
};

static struct miscdevice briq_panel_miscdev = {
	BRIQ_PANEL_MINOR,
	"briq_panel",
	&briq_panel_fops
};

static int __init briq_panel_init(void)
{
	struct device_node *root = of_find_node_by_path("/");
	const char *machine;
	int i;

	machine = get_property(root, "model", NULL);
	if (!machine || strncmp(machine, "TotalImpact,BRIQ-1", 18) != 0) {
		of_node_put(root);
		return -ENODEV;
	}
	of_node_put(root);

	printk(KERN_INFO
		"briq_panel: v%s Dr. Karsten Jeppesen (kj@totalimpact.com)\n",
		BRIQ_PANEL_VER);

	if (!request_region(BRIQ_PANEL_VFD_IOPORT, 4, "BRIQ Front Panel"))
		return -EBUSY;

	if (!request_region(BRIQ_PANEL_LED_IOPORT, 2, "BRIQ Front Panel")) {
		release_region(BRIQ_PANEL_VFD_IOPORT, 4);
		return -EBUSY;
	}
	ledpb = inb(BRIQ_PANEL_LED_IOPORT) & 0x000c;

	if (misc_register(&briq_panel_miscdev) < 0) {
		release_region(BRIQ_PANEL_VFD_IOPORT, 4);
		release_region(BRIQ_PANEL_LED_IOPORT, 2);
		return -EBUSY;
	}

	outb(0x38, BRIQ_PANEL_VFD_IOPORT);	/* Function set */
	outb(0x01, BRIQ_PANEL_VFD_IOPORT);	/* Clear display */
	outb(0x0c, BRIQ_PANEL_VFD_IOPORT);	/* Display on */
	outb(0x06, BRIQ_PANEL_VFD_IOPORT);	/* Entry normal */
	for (i=0; i<40; i++)
		vfd[i]=' ';
#ifndef MODULE
	vfd[0] = 'L';
	vfd[1] = 'o';
	vfd[2] = 'a';
	vfd[3] = 'd';
	vfd[4] = 'i';
	vfd[5] = 'n';
	vfd[6] = 'g';
	vfd[7] = ' ';
	vfd[8] = '.';
	vfd[9] = '.';
	vfd[10] = '.';
#endif /* !MODULE */

	update_vfd();

	return 0;
}

static void __exit briq_panel_exit(void)
{
	misc_deregister(&briq_panel_miscdev);
	release_region(BRIQ_PANEL_VFD_IOPORT, 4);
	release_region(BRIQ_PANEL_LED_IOPORT, 2);
}

module_init(briq_panel_init);
module_exit(briq_panel_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Karsten Jeppesen <karsten@jeppesens.com>");
MODULE_DESCRIPTION("Driver for the Total Impact briQ front panel");
