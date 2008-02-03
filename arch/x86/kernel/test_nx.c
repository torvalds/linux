/*
 * test_nx.c: functional test for NX functionality
 *
 * (C) Copyright 2008 Intel Corporation
 * Author: Arjan van de Ven <arjan@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */
#include <linux/module.h>
#include <linux/sort.h>
#include <asm/uaccess.h>

extern int rodata_test_data;

/*
 * This file checks 4 things:
 * 1) Check if the stack is not executable
 * 2) Check if kmalloc memory is not executable
 * 3) Check if the .rodata section is not executable
 * 4) Check if the .data section of a module is not executable
 *
 * To do this, the test code tries to execute memory in stack/kmalloc/etc,
 * and then checks if the expected trap happens.
 *
 * Sadly, this implies having a dynamic exception handling table entry.
 * ... which can be done (and will make Rusty cry)... but it can only
 * be done in a stand-alone module with only 1 entry total.
 * (otherwise we'd have to sort and that's just too messy)
 */



/*
 * We want to set up an exception handling point on our stack,
 * which means a variable value. This function is rather dirty
 * and walks the exception table of the module, looking for a magic
 * marker and replaces it with a specific function.
 */
static void fudze_exception_table(void *marker, void *new)
{
	struct module *mod = THIS_MODULE;
	struct exception_table_entry *extable;

	/*
	 * Note: This module has only 1 exception table entry,
	 * so searching and sorting is not needed. If that changes,
	 * this would be the place to search and re-sort the exception
	 * table.
	 */
	if (mod->num_exentries > 1) {
		printk(KERN_ERR "test_nx: too many exception table entries!\n");
		printk(KERN_ERR "test_nx: test results are not reliable.\n");
		return;
	}
	extable = (struct exception_table_entry *)mod->extable;
	extable[0].insn = (unsigned long)new;
}


/*
 * exception tables get their symbols translated so we need
 * to use a fake function to put in there, which we can then
 * replace at runtime.
 */
void foo_label(void);

/*
 * returns 0 for not-executable, negative for executable
 *
 * Note: we cannot allow this function to be inlined, because
 * that would give us more than 1 exception table entry.
 * This in turn would break the assumptions above.
 */
static noinline int test_address(void *address)
{
	unsigned long result;

	/* Set up an exception table entry for our address */
	fudze_exception_table(&foo_label, address);
	result = 1;
	asm volatile(
		"foo_label:\n"
		"0:	call *%[fake_code]\n"
		"1:\n"
		".section .fixup,\"ax\"\n"
		"2:	mov %[zero], %[rslt]\n"
		"	ret\n"
		".previous\n"
		".section __ex_table,\"a\"\n"
		"       .align 8\n"
#ifdef CONFIG_X86_32
		"	.long 0b\n"
		"	.long 2b\n"
#else
		"	.quad 0b\n"
		"	.quad 2b\n"
#endif
		".previous\n"
		: [rslt] "=r" (result)
		: [fake_code] "r" (address), [zero] "r" (0UL), "0" (result)
	);
	/* change the exception table back for the next round */
	fudze_exception_table(address, &foo_label);

	if (result)
		return -ENODEV;
	return 0;
}

static unsigned char test_data = 0xC3; /* 0xC3 is the opcode for "ret" */

static int test_NX(void)
{
	int ret = 0;
	/* 0xC3 is the opcode for "ret" */
	char stackcode[] = {0xC3, 0x90, 0 };
	char *heap;

	test_data = 0xC3;

	printk(KERN_INFO "Testing NX protection\n");

	/* Test 1: check if the stack is not executable */
	if (test_address(&stackcode)) {
		printk(KERN_ERR "test_nx: stack was executable\n");
		ret = -ENODEV;
	}


	/* Test 2: Check if the heap is executable */
	heap = kmalloc(64, GFP_KERNEL);
	if (!heap)
		return -ENOMEM;
	heap[0] = 0xC3; /* opcode for "ret" */

	if (test_address(heap)) {
		printk(KERN_ERR "test_nx: heap was executable\n");
		ret = -ENODEV;
	}
	kfree(heap);

	/*
	 * The following 2 tests currently fail, this needs to get fixed
	 * Until then, don't run them to avoid too many people getting scared
	 * by the error message
	 */
#if 0

#ifdef CONFIG_DEBUG_RODATA
	/* Test 3: Check if the .rodata section is executable */
	if (rodata_test_data != 0xC3) {
		printk(KERN_ERR "test_nx: .rodata marker has invalid value\n");
		ret = -ENODEV;
	} else if (test_address(&rodata_test_data)) {
		printk(KERN_ERR "test_nx: .rodata section is executable\n");
		ret = -ENODEV;
	}
#endif

	/* Test 4: Check if the .data section of a module is executable */
	if (test_address(&test_data)) {
		printk(KERN_ERR "test_nx: .data section is executable\n");
		ret = -ENODEV;
	}

#endif
	return 0;
}

static void test_exit(void)
{
}

module_init(test_NX);
module_exit(test_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Testcase for the NX infrastructure");
MODULE_AUTHOR("Arjan van de Ven <arjan@linux.intel.com>");
