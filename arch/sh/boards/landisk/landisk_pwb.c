/*
 * arch/sh/boards/landisk/landisk_pwb.c -- driver for the Power control switch.
 *
 * This driver will also support the I-O DATA Device, Inc. LANDISK Board.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copylight (C) 2002 Atom Create Engineering Co., Ltd.
 *
 * LED control drive function added by kogiidena
 */
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/major.h>
#include <linux/poll.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/interrupt.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/landisk/iodata_landisk.h>

#define SHUTDOWN_BTN_MINOR	1	/* Shutdown button device minor no. */
#define LED_MINOR	       21	/* LED minor no. */
#define BTN_MINOR	       22	/* BUTTON minor no. */
#define GIO_MINOR	       40	/* GIO minor no. */

static int openCnt;
static int openCntLED;
static int openCntGio;
static int openCntBtn;
static int landisk_btn;
static int landisk_btnctrlpid;
/*
 * Functions prototypes
 */

static int gio_ioctl(struct inode *inode, struct file *filp, unsigned int cmd,
		     unsigned long arg);

static int swdrv_open(struct inode *inode, struct file *filp)
{
	int minor;

	minor = MINOR(inode->i_rdev);
	filp->private_data = (void *)minor;

	if (minor == SHUTDOWN_BTN_MINOR) {
		if (openCnt > 0) {
			return -EALREADY;
		} else {
			openCnt++;
			return 0;
		}
	} else if (minor == LED_MINOR) {
		if (openCntLED > 0) {
			return -EALREADY;
		} else {
			openCntLED++;
			return 0;
		}
	} else if (minor == BTN_MINOR) {
		if (openCntBtn > 0) {
			return -EALREADY;
		} else {
			openCntBtn++;
			return 0;
		}
	} else if (minor == GIO_MINOR) {
		if (openCntGio > 0) {
			return -EALREADY;
		} else {
			openCntGio++;
			return 0;
		}
	}
	return -ENOENT;

}

static int swdrv_close(struct inode *inode, struct file *filp)
{
	int minor;

	minor = MINOR(inode->i_rdev);
	if (minor == SHUTDOWN_BTN_MINOR) {
		openCnt--;
	} else if (minor == LED_MINOR) {
		openCntLED--;
	} else if (minor == BTN_MINOR) {
		openCntBtn--;
	} else if (minor == GIO_MINOR) {
		openCntGio--;
	}
	return 0;
}

static int swdrv_read(struct file *filp, char *buff, size_t count,
		      loff_t * ppos)
{
	int minor;
	minor = (int)(filp->private_data);

	if (!access_ok(VERIFY_WRITE, (void *)buff, count))
		return -EFAULT;

	if (minor == SHUTDOWN_BTN_MINOR) {
		if (landisk_btn & 0x10) {
			put_user(1, buff);
			return 1;
		} else {
			return 0;
		}
	}
	return 0;
}

static int swdrv_write(struct file *filp, const char *buff, size_t count,
		       loff_t * ppos)
{
	int minor;
	minor = (int)(filp->private_data);

	if (minor == SHUTDOWN_BTN_MINOR) {
		return count;
	}
	return count;
}

static irqreturn_t sw_interrupt(int irq, void *dev_id)
{
	landisk_btn = (0x0ff & (~ctrl_inb(PA_STATUS)));
	disable_irq(IRQ_BUTTON);
	disable_irq(IRQ_POWER);
	ctrl_outb(0x00, PA_PWRINT_CLR);

	if (landisk_btnctrlpid != 0) {
		kill_proc(landisk_btnctrlpid, SIGUSR1, 1);
		landisk_btnctrlpid = 0;
	}

	return IRQ_HANDLED;
}

static struct file_operations swdrv_fops = {
	.read = swdrv_read,	/* read */
	.write = swdrv_write,	/* write */
	.open = swdrv_open,	/* open */
	.release = swdrv_close,	/* release */
	.ioctl = gio_ioctl,	/* ioctl */

};

static char banner[] __initdata =
    KERN_INFO "LANDISK and USL-5P Button, LED and GIO driver initialized\n";

int __init swdrv_init(void)
{
	int error;

	printk("%s", banner);

	openCnt = 0;
	openCntLED = 0;
	openCntBtn = 0;
	openCntGio = 0;
	landisk_btn = 0;
	landisk_btnctrlpid = 0;

	if ((error = register_chrdev(SHUTDOWN_BTN_MAJOR, "swdrv", &swdrv_fops))) {
		printk(KERN_ERR
		       "Button, LED and GIO driver:Couldn't register driver, error=%d\n",
		       error);
		return 1;
	}

	if (request_irq(IRQ_POWER, sw_interrupt, 0, "SHUTDOWNSWITCH", NULL)) {
		printk(KERN_ERR "Unable to get IRQ 11.\n");
		return 1;
	}
	if (request_irq(IRQ_BUTTON, sw_interrupt, 0, "USL-5P BUTTON", NULL)) {
		printk(KERN_ERR "Unable to get IRQ 12.\n");
		return 1;
	}
	ctrl_outb(0x00, PA_PWRINT_CLR);

	return 0;
}

module_init(swdrv_init);

/*
 * gio driver
 *
 */

#include <asm/landisk/gio.h>

static int gio_ioctl(struct inode *inode, struct file *filp,
		     unsigned int cmd, unsigned long arg)
{
	int minor;
	unsigned int data, mask;
	static unsigned int addr = 0;

	minor = (int)(filp->private_data);

	/* access control */
	if (minor == GIO_MINOR) {
		;
	} else if (minor == LED_MINOR) {
		if (((cmd & 0x0ff) >= 9) && ((cmd & 0x0ff) < 20)) {
			;
		} else {
			return -EINVAL;
		}
	} else if (minor == BTN_MINOR) {
		if (((cmd & 0x0ff) >= 20) && ((cmd & 0x0ff) < 30)) {
			;
		} else {
			return -EINVAL;
		}
	} else {
		return -EINVAL;
	}

	if (cmd & 0x01) {	/* write */
		if (copy_from_user(&data, (int *)arg, sizeof(int))) {
			return -EFAULT;
		}
	}

	switch (cmd) {
	case GIODRV_IOCSGIOSETADDR:	/* addres set */
		addr = data;
		break;

	case GIODRV_IOCSGIODATA1:	/* write byte */
		ctrl_outb((unsigned char)(0x0ff & data), addr);
		break;

	case GIODRV_IOCSGIODATA2:	/* write word */
		if (addr & 0x01) {
			return -EFAULT;
		}
		ctrl_outw((unsigned short int)(0x0ffff & data), addr);
		break;

	case GIODRV_IOCSGIODATA4:	/* write long */
		if (addr & 0x03) {
			return -EFAULT;
		}
		ctrl_outl(data, addr);
		break;

	case GIODRV_IOCGGIODATA1:	/* read byte */
		data = ctrl_inb(addr);
		break;

	case GIODRV_IOCGGIODATA2:	/* read word */
		if (addr & 0x01) {
			return -EFAULT;
		}
		data = ctrl_inw(addr);
		break;

	case GIODRV_IOCGGIODATA4:	/* read long */
		if (addr & 0x03) {
			return -EFAULT;
		}
		data = ctrl_inl(addr);
		break;
	case GIODRV_IOCSGIO_LED:	/* write */
		mask = ((data & 0x00ffffff) << 8)
		    | ((data & 0x0000ffff) << 16)
		    | ((data & 0x000000ff) << 24);
		landisk_ledparam = data & (~mask);
		if (landisk_arch == 0) {	/* arch == landisk */
			landisk_ledparam &= 0x03030303;
			mask = (~(landisk_ledparam >> 22)) & 0x000c;
			landisk_ledparam |= mask;
		} else {	                /* arch == usl-5p */
			mask = (landisk_ledparam >> 24) & 0x0001;
			landisk_ledparam |= mask;
			landisk_ledparam &= 0x007f7f7f;
		}
		landisk_ledparam |= 0x80;
		break;
	case GIODRV_IOCGGIO_LED:	/* read */
		data = landisk_ledparam;
		if (landisk_arch == 0) {	/* arch == landisk */
			data &= 0x03030303;
		} else {	                /* arch == usl-5p */
			;
		}
		data &= (~0x080);
		break;
	case GIODRV_IOCSGIO_BUZZER:	/* write */
		landisk_buzzerparam = data;
		landisk_ledparam |= 0x80;
		break;
	case GIODRV_IOCGGIO_LANDISK:	/* read */
		data = landisk_arch & 0x01;
		break;
	case GIODRV_IOCGGIO_BTN:	/* read */
		data = (0x0ff & ctrl_inb(PA_PWRINT_CLR));
		data <<= 8;
		data |= (0x0ff & ctrl_inb(PA_IMASK));
		data <<= 8;
		data |= (0x0ff & landisk_btn);
		data <<= 8;
		data |= (0x0ff & (~ctrl_inb(PA_STATUS)));
		break;
	case GIODRV_IOCSGIO_BTNPID:	/* write */
		landisk_btnctrlpid = data;
		landisk_btn = 0;
		if (irq_desc[IRQ_BUTTON].depth) {
			enable_irq(IRQ_BUTTON);
		}
		if (irq_desc[IRQ_POWER].depth) {
			enable_irq(IRQ_POWER);
		}
		break;
	case GIODRV_IOCGGIO_BTNPID:	/* read */
		data = landisk_btnctrlpid;
		break;
	default:
		return -EFAULT;
		break;
	}

	if ((cmd & 0x01) == 0) {	/* read */
		if (copy_to_user((int *)arg, &data, sizeof(int))) {
			return -EFAULT;
		}
	}
	return 0;
}
