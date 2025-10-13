// SPDX-License-Identifier: GPL-2.0
/*
 * This is for all the tests related to validating kernel memory
 * permissions: non-executable regions, non-writable regions, and
 * even non-readable regions.
 */
#include "lkdtm.h"
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/mman.h>
#include <linux/uaccess.h>
#include <linux/objtool.h>
#include <asm/cacheflush.h>
#include <asm/sections.h>

/* Whether or not to fill the target memory area with do_nothing(). */
#define CODE_WRITE	true
#define CODE_AS_IS	false

/* How many bytes to copy to be sure we've copied enough of do_nothing(). */
#define EXEC_SIZE 64

/* This is non-const, so it will end up in the .data section. */
static u8 data_area[EXEC_SIZE];

/* This is const, so it will end up in the .rodata section. */
static const unsigned long rodata = 0xAA55AA55;

/* This is marked __ro_after_init, so it should ultimately be .rodata. */
static unsigned long ro_after_init __ro_after_init = 0x55AA5500;

/*
 * This is a pointer to do_nothing() which is initialized at runtime rather
 * than build time to avoid objtool IBT validation warnings caused by an
 * inlined unrolled memcpy() in execute_location().
 */
static void __ro_after_init *do_nothing_ptr;

/*
 * This just returns to the caller. It is designed to be copied into
 * non-executable memory regions.
 */
static noinline void do_nothing(void)
{
	return;
}

/* Must immediately follow do_nothing for size calculuations to work out. */
static noinline void do_overwritten(void)
{
	pr_info("do_overwritten wasn't overwritten!\n");
	return;
}

static noinline void do_almost_nothing(void)
{
	pr_info("do_nothing was hijacked!\n");
}

static void *setup_function_descriptor(func_desc_t *fdesc, void *dst)
{
	if (!have_function_descriptors())
		return dst;

	memcpy(fdesc, do_nothing, sizeof(*fdesc));
	fdesc->addr = (unsigned long)dst;
	barrier();

	return fdesc;
}

static noinline __nocfi void execute_location(void *dst, bool write)
{
	void (*func)(void);
	func_desc_t fdesc;

	pr_info("attempting ok execution at %px\n", do_nothing_ptr);
	do_nothing();

	if (write == CODE_WRITE) {
		memcpy(dst, do_nothing_ptr, EXEC_SIZE);
		flush_icache_range((unsigned long)dst,
				   (unsigned long)dst + EXEC_SIZE);
	}
	pr_info("attempting bad execution at %px\n", dst);
	func = setup_function_descriptor(&fdesc, dst);
	func();
	pr_err("FAIL: func returned\n");
}
/*
 * Explicitly doing the wrong thing for testing.
 */
ANNOTATE_NOCFI_SYM(execute_location);

static void execute_user_location(void *dst)
{
	int copied;

	/* Intentionally crossing kernel/user memory boundary. */
	void (*func)(void);
	func_desc_t fdesc;
	void *do_nothing_text = dereference_function_descriptor(do_nothing);

	pr_info("attempting ok execution at %px\n", do_nothing_text);
	do_nothing();

	copied = access_process_vm(current, (unsigned long)dst, do_nothing_text,
				   EXEC_SIZE, FOLL_WRITE);
	if (copied < EXEC_SIZE)
		return;
	pr_info("attempting bad execution at %px\n", dst);
	func = setup_function_descriptor(&fdesc, dst);
	func();
	pr_err("FAIL: func returned\n");
}

static void lkdtm_WRITE_RO(void)
{
	/* Explicitly cast away "const" for the test and make volatile. */
	volatile unsigned long *ptr = (unsigned long *)&rodata;

	pr_info("attempting bad rodata write at %px\n", ptr);
	*ptr ^= 0xabcd1234;
	pr_err("FAIL: survived bad write\n");
}

static void lkdtm_WRITE_RO_AFTER_INIT(void)
{
	volatile unsigned long *ptr = &ro_after_init;

	/*
	 * Verify we were written to during init. Since an Oops
	 * is considered a "success", a failure is to just skip the
	 * real test.
	 */
	if ((*ptr & 0xAA) != 0xAA) {
		pr_info("%p was NOT written during init!?\n", ptr);
		return;
	}

	pr_info("attempting bad ro_after_init write at %px\n", ptr);
	*ptr ^= 0xabcd1234;
	pr_err("FAIL: survived bad write\n");
}

static void lkdtm_WRITE_KERN(void)
{
	size_t size;
	volatile unsigned char *ptr;

	size = (unsigned long)dereference_function_descriptor(do_overwritten) -
	       (unsigned long)dereference_function_descriptor(do_nothing);
	ptr = dereference_function_descriptor(do_overwritten);

	pr_info("attempting bad %zu byte write at %px\n", size, ptr);
	memcpy((void *)ptr, (unsigned char *)do_nothing, size);
	flush_icache_range((unsigned long)ptr, (unsigned long)(ptr + size));
	pr_err("FAIL: survived bad write\n");

	do_overwritten();
}

static void lkdtm_WRITE_OPD(void)
{
	size_t size = sizeof(func_desc_t);
	void (*func)(void) = do_nothing;

	if (!have_function_descriptors()) {
		pr_info("XFAIL: Platform doesn't use function descriptors.\n");
		return;
	}
	pr_info("attempting bad %zu bytes write at %px\n", size, do_nothing);
	memcpy(do_nothing, do_almost_nothing, size);
	pr_err("FAIL: survived bad write\n");

	asm("" : "=m"(func));
	func();
}

static void lkdtm_EXEC_DATA(void)
{
	execute_location(data_area, CODE_WRITE);
}

static void lkdtm_EXEC_STACK(void)
{
	u8 stack_area[EXEC_SIZE];
	execute_location(stack_area, CODE_WRITE);
}

static void lkdtm_EXEC_KMALLOC(void)
{
	u32 *kmalloc_area = kmalloc(EXEC_SIZE, GFP_KERNEL);
	execute_location(kmalloc_area, CODE_WRITE);
	kfree(kmalloc_area);
}

static void lkdtm_EXEC_VMALLOC(void)
{
	u32 *vmalloc_area = vmalloc(EXEC_SIZE);
	execute_location(vmalloc_area, CODE_WRITE);
	vfree(vmalloc_area);
}

static void lkdtm_EXEC_RODATA(void)
{
	execute_location(dereference_function_descriptor(lkdtm_rodata_do_nothing),
			 CODE_AS_IS);
}

static void lkdtm_EXEC_USERSPACE(void)
{
	unsigned long user_addr;

	user_addr = vm_mmap(NULL, 0, PAGE_SIZE,
			    PROT_READ | PROT_WRITE | PROT_EXEC,
			    MAP_ANONYMOUS | MAP_PRIVATE, 0);
	if (user_addr >= TASK_SIZE) {
		pr_warn("Failed to allocate user memory\n");
		return;
	}
	execute_user_location((void *)user_addr);
	vm_munmap(user_addr, PAGE_SIZE);
}

static void lkdtm_EXEC_NULL(void)
{
	execute_location(NULL, CODE_AS_IS);
}

static void lkdtm_ACCESS_USERSPACE(void)
{
	unsigned long user_addr, tmp = 0;
	unsigned long *ptr;

	user_addr = vm_mmap(NULL, 0, PAGE_SIZE,
			    PROT_READ | PROT_WRITE | PROT_EXEC,
			    MAP_ANONYMOUS | MAP_PRIVATE, 0);
	if (user_addr >= TASK_SIZE) {
		pr_warn("Failed to allocate user memory\n");
		return;
	}

	if (copy_to_user((void __user *)user_addr, &tmp, sizeof(tmp))) {
		pr_warn("copy_to_user failed\n");
		vm_munmap(user_addr, PAGE_SIZE);
		return;
	}

	ptr = (unsigned long *)user_addr;

	pr_info("attempting bad read at %px\n", ptr);
	tmp = *ptr;
	tmp += 0xc0dec0de;
	pr_err("FAIL: survived bad read\n");

	pr_info("attempting bad write at %px\n", ptr);
	*ptr = tmp;
	pr_err("FAIL: survived bad write\n");

	vm_munmap(user_addr, PAGE_SIZE);
}

static void lkdtm_ACCESS_NULL(void)
{
	unsigned long tmp;
	volatile unsigned long *ptr = (unsigned long *)NULL;

	pr_info("attempting bad read at %px\n", ptr);
	tmp = *ptr;
	tmp += 0xc0dec0de;
	pr_err("FAIL: survived bad read\n");

	pr_info("attempting bad write at %px\n", ptr);
	*ptr = tmp;
	pr_err("FAIL: survived bad write\n");
}

void __init lkdtm_perms_init(void)
{
	do_nothing_ptr = dereference_function_descriptor(do_nothing);

	/* Make sure we can write to __ro_after_init values during __init */
	ro_after_init |= 0xAA;
}

static struct crashtype crashtypes[] = {
	CRASHTYPE(WRITE_RO),
	CRASHTYPE(WRITE_RO_AFTER_INIT),
	CRASHTYPE(WRITE_KERN),
	CRASHTYPE(WRITE_OPD),
	CRASHTYPE(EXEC_DATA),
	CRASHTYPE(EXEC_STACK),
	CRASHTYPE(EXEC_KMALLOC),
	CRASHTYPE(EXEC_VMALLOC),
	CRASHTYPE(EXEC_RODATA),
	CRASHTYPE(EXEC_USERSPACE),
	CRASHTYPE(EXEC_NULL),
	CRASHTYPE(ACCESS_USERSPACE),
	CRASHTYPE(ACCESS_NULL),
};

struct crashtype_category perms_crashtypes = {
	.crashtypes = crashtypes,
	.len	    = ARRAY_SIZE(crashtypes),
};
