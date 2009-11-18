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
static unsigned long len;

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

static int test_proc_output(char *buf)
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

static int test_read_proc(char *page, char **start, off_t off,
				 int count, int *eof, void *data)
{
	int len;

	len = test_proc_output(page);
	if (len <= off+count)
		*eof = 1;
	*start = page + off;
	len -= off;
	if (len > count)
		len = count;
	if (len < 0)
		len = 0;
	return len;
}

static int test_write_proc(struct file *file, const char *buffer,
				 unsigned long count, void *data)
{
	if (count >= 256)
		len = 255;
	else
		len = count;

	memcpy(cmdline, buffer, count);
	cmdline[len] = 0;

	return len;
}

static int __init kgdbtest_init(void)
{
	struct proc_dir_entry *entry;

	entry = create_proc_entry("kgdbtest", 0, NULL);
	if (entry == NULL)
		return -ENOMEM;

	entry->read_proc = test_read_proc;
	entry->write_proc = test_write_proc;
	entry->data = NULL;

	return 0;
}

static void __exit kgdbtest_exit(void)
{
	remove_proc_entry("kgdbtest", NULL);
}

module_init(kgdbtest_init);
module_exit(kgdbtest_exit);
MODULE_LICENSE("GPL");
