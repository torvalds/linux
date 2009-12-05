/*
 * arch/blackfin/kernel/kgdb_test.c - Blackfin kgdb tests
 *
 * Copyright 2005-2008 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/proc_fs.h>

#include <asm/current.h>
#include <asm/uaccess.h>
#include <asm/system.h>

#include <asm/blackfin.h>

static char cmdline[256];
static size_t len;

#ifndef CONFIG_SMP
static int num1 __attribute__((l1_data));

void kgdb_l1_test(void) __attribute__((l1_text));

void kgdb_l1_test(void)
{
	printk(KERN_ALERT "L1(before change) : data variable addr = 0x%p, data value is %d\n", &num1, num1);
	printk(KERN_ALERT "L1 : code function addr = 0x%p\n", kgdb_l1_test);
	num1 = num1 + 10 ;
	printk(KERN_ALERT "L1(after change) : data variable addr = 0x%p, data value is %d\n", &num1, num1);
	return ;
}
#endif

#if L2_LENGTH

static int num2 __attribute__((l2));
void kgdb_l2_test(void) __attribute__((l2));

void kgdb_l2_test(void)
{
	printk(KERN_ALERT "L2(before change) : data variable addr = 0x%p, data value is %d\n", &num2, num2);
	printk(KERN_ALERT "L2 : code function addr = 0x%p\n", kgdb_l2_test);
	num2 = num2 + 20 ;
	printk(KERN_ALERT "L2(after change) : data variable addr = 0x%p, data value is %d\n", &num2, num2);
	return ;
}

#endif


int kgdb_test(char *name, int len, int count, int z)
{
	printk(KERN_ALERT "kgdb name(%d): %s, %d, %d\n", len, name, count, z);
	count = z;
	return count;
}

static ssize_t kgdb_test_proc_read(struct file *file, char __user *buf,
				   size_t count, loff_t *ppos)
{
	kgdb_test("hello world!", 12, 0x55, 0x10);
#ifndef CONFIG_SMP
	kgdb_l1_test();
#endif
#if L2_LENGTH
	kgdb_l2_test();
#endif

	return 0;
}

static ssize_t kgdb_test_proc_write(struct file *file,
		const char __user *buffer, size_t count, loff_t *pos)
{
	if (count >= 256)
		len = 255;
	else
		len = count;

	memcpy(cmdline, buffer, count);
	cmdline[len] = 0;

	return len;
}

static const struct file_operations kgdb_test_proc_fops = {
	.owner		= THIS_MODULE,
	.read		= kgdb_test_proc_read,
	.write		= kgdb_test_proc_write,
};

static int __init kgdbtest_init(void)
{
	struct proc_dir_entry *entry;

	entry = proc_create("kgdbtest", 0, NULL, &kgdb_test_proc_fops);
	if (entry == NULL)
		return -ENOMEM;
	return 0;
}

static void __exit kgdbtest_exit(void)
{
	remove_proc_entry("kgdbtest", NULL);
}

module_init(kgdbtest_init);
module_exit(kgdbtest_exit);
MODULE_LICENSE("GPL");
