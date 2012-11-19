/*
 *  Driver for TANBAC TB0219 base board.
 *
 *  Copyright (C) 2005  Yoichi Yuasa <yuasa@linux-mips.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <linux/platform_device.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>

#include <asm/io.h>
#include <asm/reboot.h>
#include <asm/vr41xx/giu.h>
#include <asm/vr41xx/tb0219.h>

MODULE_AUTHOR("Yoichi Yuasa <yuasa@linux-mips.org>");
MODULE_DESCRIPTION("TANBAC TB0219 base board driver");
MODULE_LICENSE("GPL");

static int major;	/* default is dynamic major device number */
module_param(major, int, 0);
MODULE_PARM_DESC(major, "Major device number");

static void (*old_machine_restart)(char *command);
static void __iomem *tb0219_base;
static DEFINE_SPINLOCK(tb0219_lock);

#define tb0219_read(offset)		readw(tb0219_base + (offset))
#define tb0219_write(offset, value)	writew((value), tb0219_base + (offset))

#define TB0219_START	0x0a000000UL
#define TB0219_SIZE	0x20UL

#define TB0219_LED			0x00
#define TB0219_GPIO_INPUT		0x02
#define TB0219_GPIO_OUTPUT		0x04
#define TB0219_DIP_SWITCH		0x06
#define TB0219_MISC			0x08
#define TB0219_RESET			0x0e
#define TB0219_PCI_SLOT1_IRQ_STATUS	0x10
#define TB0219_PCI_SLOT2_IRQ_STATUS	0x12
#define TB0219_PCI_SLOT3_IRQ_STATUS	0x14

typedef enum {
	TYPE_LED,
	TYPE_GPIO_OUTPUT,
} tb0219_type_t;

/*
 * Minor device number
 *	 0 = 7 segment LED
 *
 *	16 = GPIO IN 0
 *	17 = GPIO IN 1
 *	18 = GPIO IN 2
 *	19 = GPIO IN 3
 *	20 = GPIO IN 4
 *	21 = GPIO IN 5
 *	22 = GPIO IN 6
 *	23 = GPIO IN 7
 *
 *	32 = GPIO OUT 0
 *	33 = GPIO OUT 1
 *	34 = GPIO OUT 2
 *	35 = GPIO OUT 3
 *	36 = GPIO OUT 4
 *	37 = GPIO OUT 5
 *	38 = GPIO OUT 6
 *	39 = GPIO OUT 7
 *
 *	48 = DIP switch 1
 *	49 = DIP switch 2
 *	50 = DIP switch 3
 *	51 = DIP switch 4
 *	52 = DIP switch 5
 *	53 = DIP switch 6
 *	54 = DIP switch 7
 *	55 = DIP switch 8
 */

static inline char get_led(void)
{
	return (char)tb0219_read(TB0219_LED);
}

static inline char get_gpio_input_pin(unsigned int pin)
{
	uint16_t values;

	values = tb0219_read(TB0219_GPIO_INPUT);
	if (values & (1 << pin))
		return '1';

	return '0';
}

static inline char get_gpio_output_pin(unsigned int pin)
{
	uint16_t values;

	values = tb0219_read(TB0219_GPIO_OUTPUT);
	if (values & (1 << pin))
		return '1';

	return '0';
}

static inline char get_dip_switch(unsigned int pin)
{
	uint16_t values;

	values = tb0219_read(TB0219_DIP_SWITCH);
	if (values & (1 << pin))
		return '1';

	return '0';
}

static inline int set_led(char command)
{
	tb0219_write(TB0219_LED, command);

	return 0;
}

static inline int set_gpio_output_pin(unsigned int pin, char command)
{
	unsigned long flags;
	uint16_t value;

	if (command != '0' && command != '1')
		return -EINVAL;

	spin_lock_irqsave(&tb0219_lock, flags);
	value = tb0219_read(TB0219_GPIO_OUTPUT);
	if (command == '0')
		value &= ~(1 << pin);
	else
		value |= 1 << pin;
	tb0219_write(TB0219_GPIO_OUTPUT, value);
	spin_unlock_irqrestore(&tb0219_lock, flags);

	return 0;

}

static ssize_t tanbac_tb0219_read(struct file *file, char __user *buf, size_t len,
                                  loff_t *ppos)
{
	unsigned int minor;
	char value;

	minor = iminor(file->f_path.dentry->d_inode);
	switch (minor) {
	case 0:
		value = get_led();
		break;
	case 16 ... 23:
		value = get_gpio_input_pin(minor - 16);
		break;
	case 32 ... 39:
		value = get_gpio_output_pin(minor - 32);
		break;
	case 48 ... 55:
		value = get_dip_switch(minor - 48);
		break;
	default:
		return -EBADF;
	}

	if (len <= 0)
		return -EFAULT;

	if (put_user(value, buf))
		return -EFAULT;

	return 1;
}

static ssize_t tanbac_tb0219_write(struct file *file, const char __user *data,
                                   size_t len, loff_t *ppos)
{
	unsigned int minor;
	tb0219_type_t type;
	size_t i;
	int retval = 0;
	char c;

	minor = iminor(file->f_path.dentry->d_inode);
	switch (minor) {
	case 0:
		type = TYPE_LED;
		break;
	case 32 ... 39:
		type = TYPE_GPIO_OUTPUT;
		break;
	default:
		return -EBADF;
	}

	for (i = 0; i < len; i++) {
		if (get_user(c, data + i))
			return -EFAULT;

		switch (type) {
		case TYPE_LED:
			retval = set_led(c);
			break;
		case TYPE_GPIO_OUTPUT:
			retval = set_gpio_output_pin(minor - 32, c);
			break;
		}

		if (retval < 0)
			break;
	}

	return i;
}

static int tanbac_tb0219_open(struct inode *inode, struct file *file)
{
	unsigned int minor;

	minor = iminor(inode);
	switch (minor) {
	case 0:
	case 16 ... 23:
	case 32 ... 39:
	case 48 ... 55:
		return nonseekable_open(inode, file);
	default:
		break;
	}

	return -EBADF;
}

static int tanbac_tb0219_release(struct inode *inode, struct file *file)
{
	return 0;
}

static const struct file_operations tb0219_fops = {
	.owner		= THIS_MODULE,
	.read		= tanbac_tb0219_read,
	.write		= tanbac_tb0219_write,
	.open		= tanbac_tb0219_open,
	.release	= tanbac_tb0219_release,
	.llseek		= no_llseek,
};

static void tb0219_restart(char *command)
{
	tb0219_write(TB0219_RESET, 0);
}

static void tb0219_pci_irq_init(void)
{
	/* PCI Slot 1 */
	vr41xx_set_irq_trigger(TB0219_PCI_SLOT1_PIN, IRQ_TRIGGER_LEVEL, IRQ_SIGNAL_THROUGH);
	vr41xx_set_irq_level(TB0219_PCI_SLOT1_PIN, IRQ_LEVEL_LOW);

	/* PCI Slot 2 */
	vr41xx_set_irq_trigger(TB0219_PCI_SLOT2_PIN, IRQ_TRIGGER_LEVEL, IRQ_SIGNAL_THROUGH);
	vr41xx_set_irq_level(TB0219_PCI_SLOT2_PIN, IRQ_LEVEL_LOW);

	/* PCI Slot 3 */
	vr41xx_set_irq_trigger(TB0219_PCI_SLOT3_PIN, IRQ_TRIGGER_LEVEL, IRQ_SIGNAL_THROUGH);
	vr41xx_set_irq_level(TB0219_PCI_SLOT3_PIN, IRQ_LEVEL_LOW);
}

static int __devinit tb0219_probe(struct platform_device *dev)
{
	int retval;

	if (request_mem_region(TB0219_START, TB0219_SIZE, "TB0219") == NULL)
		return -EBUSY;

	tb0219_base = ioremap(TB0219_START, TB0219_SIZE);
	if (tb0219_base == NULL) {
		release_mem_region(TB0219_START, TB0219_SIZE);
		return -ENOMEM;
	}

	retval = register_chrdev(major, "TB0219", &tb0219_fops);
	if (retval < 0) {
		iounmap(tb0219_base);
		tb0219_base = NULL;
		release_mem_region(TB0219_START, TB0219_SIZE);
		return retval;
	}

	old_machine_restart = _machine_restart;
	_machine_restart = tb0219_restart;

	tb0219_pci_irq_init();

	if (major == 0) {
		major = retval;
		printk(KERN_INFO "TB0219: major number %d\n", major);
	}

	return 0;
}

static int __devexit tb0219_remove(struct platform_device *dev)
{
	_machine_restart = old_machine_restart;

	iounmap(tb0219_base);
	tb0219_base = NULL;

	release_mem_region(TB0219_START, TB0219_SIZE);

	return 0;
}

static struct platform_device *tb0219_platform_device;

static struct platform_driver tb0219_device_driver = {
	.probe		= tb0219_probe,
	.remove		= tb0219_remove,
	.driver		= {
		.name	= "TB0219",
		.owner	= THIS_MODULE,
	},
};

static int __init tanbac_tb0219_init(void)
{
	int retval;

	tb0219_platform_device = platform_device_alloc("TB0219", -1);
	if (!tb0219_platform_device)
		return -ENOMEM;

	retval = platform_device_add(tb0219_platform_device);
	if (retval < 0) {
		platform_device_put(tb0219_platform_device);
		return retval;
	}

	retval = platform_driver_register(&tb0219_device_driver);
	if (retval < 0)
		platform_device_unregister(tb0219_platform_device);

	return retval;
}

static void __exit tanbac_tb0219_exit(void)
{
	platform_driver_unregister(&tb0219_device_driver);
	platform_device_unregister(tb0219_platform_device);
}

module_init(tanbac_tb0219_init);
module_exit(tanbac_tb0219_exit);
