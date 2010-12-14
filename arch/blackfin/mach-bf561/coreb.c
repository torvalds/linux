/* Load firmware into Core B on a BF561
 *
 * Copyright 2004-2009 Analog Devices Inc.
 * Licensed under the GPL-2 or later.
 */

/* The Core B reset func requires code in the application that is loaded into
 * Core B.  In order to reset, the application needs to install an interrupt
 * handler for Supplemental Interrupt 0, that sets RETI to 0xff600000 and
 * writes bit 11 of SICB_SYSCR when bit 5 of SICA_SYSCR is 0.  This causes Core
 * B to stall when Supplemental Interrupt 0 is set, and will reset PC to
 * 0xff600000 when COREB_SRAM_INIT is cleared.
 */

#include <linux/device.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/module.h>

#define CMD_COREB_START		_IO('b', 0)
#define CMD_COREB_STOP		_IO('b', 1)
#define CMD_COREB_RESET		_IO('b', 2)

static long
coreb_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int ret = 0;

	switch (cmd) {
	case CMD_COREB_START:
		bfin_write_SYSCR(bfin_read_SYSCR() & ~0x0020);
		break;
	case CMD_COREB_STOP:
		bfin_write_SYSCR(bfin_read_SYSCR() | 0x0020);
		bfin_write_SICB_SYSCR(bfin_read_SICB_SYSCR() | 0x0080);
		break;
	case CMD_COREB_RESET:
		bfin_write_SICB_SYSCR(bfin_read_SICB_SYSCR() | 0x0080);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	CSYNC();

	return ret;
}

static const struct file_operations coreb_fops = {
	.owner          = THIS_MODULE,
	.unlocked_ioctl = coreb_ioctl,
	.llseek		= noop_llseek,
};

static struct miscdevice coreb_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name  = "coreb",
	.fops  = &coreb_fops,
};

static int __init bf561_coreb_init(void)
{
	return misc_register(&coreb_dev);
}
module_init(bf561_coreb_init);

static void __exit bf561_coreb_exit(void)
{
	misc_deregister(&coreb_dev);
}
module_exit(bf561_coreb_exit);

MODULE_AUTHOR("Bas Vermeulen <bvermeul@blackstar.xs4all.nl>");
MODULE_DESCRIPTION("BF561 Core B Support");
MODULE_LICENSE("GPL");
