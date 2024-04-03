// SPDX-License-Identifier: GPL-2.0
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/tty.h>
#include <linux/module.h>

/*
 *  n_null.c - Null line discipline used in the failure path
 *
 *  Copyright (C) Intel 2017
 */

static ssize_t n_null_read(struct tty_struct *tty, struct file *file, u8 *buf,
			   size_t nr, void **cookie, unsigned long offset)
{
	return -EOPNOTSUPP;
}

static ssize_t n_null_write(struct tty_struct *tty, struct file *file,
			    const u8 *buf, size_t nr)
{
	return -EOPNOTSUPP;
}

static struct tty_ldisc_ops null_ldisc = {
	.owner		=	THIS_MODULE,
	.num		=	N_NULL,
	.name		=	"n_null",
	.read		=	n_null_read,
	.write		=	n_null_write,
};

static int __init n_null_init(void)
{
	BUG_ON(tty_register_ldisc(&null_ldisc));
	return 0;
}

static void __exit n_null_exit(void)
{
	tty_unregister_ldisc(&null_ldisc);
}

module_init(n_null_init);
module_exit(n_null_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alan Cox");
MODULE_ALIAS_LDISC(N_NULL);
MODULE_DESCRIPTION("Null ldisc driver");
