/*
 * wasawasa - Baby, please kernel module. (BPKM)
 * 
 * Copyright © 2014 sasairc
 * This work is free. You can redistribute it and/or modify it under the
 * terms of the Do What The Fuck You Want To Public License, Version 2,
 * as published by Sam Hocevar.Hocevar See the COPYING file or http://www.wtfpl.net/
 * for more details.
 */
 
#include <linux/semaphore.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <asm/string.h>

#define MODNAME     "wasawasa"
#define CHAR_MAJOR  254
#define CHAR_MINOR  1

MODULE_LICENSE("WTFPL");
MODULE_AUTHOR("sasairc");

static struct cdev      cdev;
static struct semaphore sema;

static
int open_wasawasa(struct inode *inode, struct file *file)
{
    return 0;
}

static
int release_wasawasa(struct inode *inode, struct file *file)
{
    return 0;
}

static
ssize_t read_wasawasa(struct file *file, char *buf, size_t count, loff_t *offset)
{
    ssize_t     len = 0;

    const char* str = "わさわさ";

    down_interruptible(&sema);
    if ((len = strlen(str)) > 0) {
        copy_to_user(buf, str, len);
        *offset += len;
    }
    up(&sema);

    return len;
}

static
struct file_operations cdev_fops = {
    .owner      = THIS_MODULE,
    .open       = open_wasawasa,
    .read       = read_wasawasa,
    .release    = release_wasawasa,
};

static
int __init init_wasawasa(void)
{
    int     ret = 0;

    dev_t   dev;

    dev = MKDEV(CHAR_MAJOR, 0);
    cdev_init(&cdev, &cdev_fops);
    if ((ret = cdev_add(&cdev, dev, CHAR_MINOR)) < 0)
        printk(KERN_WARNING "%s: cdev_add() failure.\n",
                MODNAME);
    else
        sema_init(&sema, 1);

    return ret;
}

static
void __exit exit_wasawasa(void)
{
    dev_t   dev;

    dev = MKDEV(CHAR_MAJOR, 0);
    cdev_del(&cdev);
    unregister_chrdev_region(dev, CHAR_MINOR);
}

module_init(init_wasawasa);
module_exit(exit_wasawasa);
