/*
 * This is for all the tests related to copy_to_user() and copy_from_user()
 * hardening.
 */
#include "lkdtm.h"
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/mman.h>
#include <linux/uaccess.h>
#include <asm/cacheflush.h>

static size_t cache_size = 1024;
static struct kmem_cache *bad_cache;

static const unsigned char test_text[] = "This is a test.\n";

/*
 * Instead of adding -Wno-return-local-addr, just pass the stack address
 * through a function to obfuscate it from the compiler.
 */
static noinline unsigned char *trick_compiler(unsigned char *stack)
{
	return stack + 0;
}

static noinline unsigned char *do_usercopy_stack_callee(int value)
{
	unsigned char buf[32];
	int i;

	/* Exercise stack to avoid everything living in registers. */
	for (i = 0; i < sizeof(buf); i++) {
		buf[i] = value & 0xff;
	}

	return trick_compiler(buf);
}

static noinline void do_usercopy_stack(bool to_user, bool bad_frame)
{
	unsigned long user_addr;
	unsigned char good_stack[32];
	unsigned char *bad_stack;
	int i;

	/* Exercise stack to avoid everything living in registers. */
	for (i = 0; i < sizeof(good_stack); i++)
		good_stack[i] = test_text[i % sizeof(test_text)];

	/* This is a pointer to outside our current stack frame. */
	if (bad_frame) {
		bad_stack = do_usercopy_stack_callee((uintptr_t)&bad_stack);
	} else {
		/* Put start address just inside stack. */
		bad_stack = task_stack_page(current) + THREAD_SIZE;
		bad_stack -= sizeof(unsigned long);
	}

	user_addr = vm_mmap(NULL, 0, PAGE_SIZE,
			    PROT_READ | PROT_WRITE | PROT_EXEC,
			    MAP_ANONYMOUS | MAP_PRIVATE, 0);
	if (user_addr >= TASK_SIZE) {
		pr_warn("Failed to allocate user memory\n");
		return;
	}

	if (to_user) {
		pr_info("attempting good copy_to_user of local stack\n");
		if (copy_to_user((void __user *)user_addr, good_stack,
				 sizeof(good_stack))) {
			pr_warn("copy_to_user failed unexpectedly?!\n");
			goto free_user;
		}

		pr_info("attempting bad copy_to_user of distant stack\n");
		if (copy_to_user((void __user *)user_addr, bad_stack,
				 sizeof(good_stack))) {
			pr_warn("copy_to_user failed, but lacked Oops\n");
			goto free_user;
		}
	} else {
		/*
		 * There isn't a safe way to not be protected by usercopy
		 * if we're going to write to another thread's stack.
		 */
		if (!bad_frame)
			goto free_user;

		pr_info("attempting good copy_from_user of local stack\n");
		if (copy_from_user(good_stack, (void __user *)user_addr,
				   sizeof(good_stack))) {
			pr_warn("copy_from_user failed unexpectedly?!\n");
			goto free_user;
		}

		pr_info("attempting bad copy_from_user of distant stack\n");
		if (copy_from_user(bad_stack, (void __user *)user_addr,
				   sizeof(good_stack))) {
			pr_warn("copy_from_user failed, but lacked Oops\n");
			goto free_user;
		}
	}

free_user:
	vm_munmap(user_addr, PAGE_SIZE);
}

static void do_usercopy_heap_size(bool to_user)
{
	unsigned long user_addr;
	unsigned char *one, *two;
	const size_t size = 1024;

	one = kmalloc(size, GFP_KERNEL);
	two = kmalloc(size, GFP_KERNEL);
	if (!one || !two) {
		pr_warn("Failed to allocate kernel memory\n");
		goto free_kernel;
	}

	user_addr = vm_mmap(NULL, 0, PAGE_SIZE,
			    PROT_READ | PROT_WRITE | PROT_EXEC,
			    MAP_ANONYMOUS | MAP_PRIVATE, 0);
	if (user_addr >= TASK_SIZE) {
		pr_warn("Failed to allocate user memory\n");
		goto free_kernel;
	}

	memset(one, 'A', size);
	memset(two, 'B', size);

	if (to_user) {
		pr_info("attempting good copy_to_user of correct size\n");
		if (copy_to_user((void __user *)user_addr, one, size)) {
			pr_warn("copy_to_user failed unexpectedly?!\n");
			goto free_user;
		}

		pr_info("attempting bad copy_to_user of too large size\n");
		if (copy_to_user((void __user *)user_addr, one, 2 * size)) {
			pr_warn("copy_to_user failed, but lacked Oops\n");
			goto free_user;
		}
	} else {
		pr_info("attempting good copy_from_user of correct size\n");
		if (copy_from_user(one, (void __user *)user_addr, size)) {
			pr_warn("copy_from_user failed unexpectedly?!\n");
			goto free_user;
		}

		pr_info("attempting bad copy_from_user of too large size\n");
		if (copy_from_user(one, (void __user *)user_addr, 2 * size)) {
			pr_warn("copy_from_user failed, but lacked Oops\n");
			goto free_user;
		}
	}

free_user:
	vm_munmap(user_addr, PAGE_SIZE);
free_kernel:
	kfree(one);
	kfree(two);
}

static void do_usercopy_heap_flag(bool to_user)
{
	unsigned long user_addr;
	unsigned char *good_buf = NULL;
	unsigned char *bad_buf = NULL;

	/* Make sure cache was prepared. */
	if (!bad_cache) {
		pr_warn("Failed to allocate kernel cache\n");
		return;
	}

	/*
	 * Allocate one buffer from each cache (kmalloc will have the
	 * SLAB_USERCOPY flag already, but "bad_cache" won't).
	 */
	good_buf = kmalloc(cache_size, GFP_KERNEL);
	bad_buf = kmem_cache_alloc(bad_cache, GFP_KERNEL);
	if (!good_buf || !bad_buf) {
		pr_warn("Failed to allocate buffers from caches\n");
		goto free_alloc;
	}

	/* Allocate user memory we'll poke at. */
	user_addr = vm_mmap(NULL, 0, PAGE_SIZE,
			    PROT_READ | PROT_WRITE | PROT_EXEC,
			    MAP_ANONYMOUS | MAP_PRIVATE, 0);
	if (user_addr >= TASK_SIZE) {
		pr_warn("Failed to allocate user memory\n");
		goto free_alloc;
	}

	memset(good_buf, 'A', cache_size);
	memset(bad_buf, 'B', cache_size);

	if (to_user) {
		pr_info("attempting good copy_to_user with SLAB_USERCOPY\n");
		if (copy_to_user((void __user *)user_addr, good_buf,
				 cache_size)) {
			pr_warn("copy_to_user failed unexpectedly?!\n");
			goto free_user;
		}

		pr_info("attempting bad copy_to_user w/o SLAB_USERCOPY\n");
		if (copy_to_user((void __user *)user_addr, bad_buf,
				 cache_size)) {
			pr_warn("copy_to_user failed, but lacked Oops\n");
			goto free_user;
		}
	} else {
		pr_info("attempting good copy_from_user with SLAB_USERCOPY\n");
		if (copy_from_user(good_buf, (void __user *)user_addr,
				   cache_size)) {
			pr_warn("copy_from_user failed unexpectedly?!\n");
			goto free_user;
		}

		pr_info("attempting bad copy_from_user w/o SLAB_USERCOPY\n");
		if (copy_from_user(bad_buf, (void __user *)user_addr,
				   cache_size)) {
			pr_warn("copy_from_user failed, but lacked Oops\n");
			goto free_user;
		}
	}

free_user:
	vm_munmap(user_addr, PAGE_SIZE);
free_alloc:
	if (bad_buf)
		kmem_cache_free(bad_cache, bad_buf);
	kfree(good_buf);
}

/* Callable tests. */
void lkdtm_USERCOPY_HEAP_SIZE_TO(void)
{
	do_usercopy_heap_size(true);
}

void lkdtm_USERCOPY_HEAP_SIZE_FROM(void)
{
	do_usercopy_heap_size(false);
}

void lkdtm_USERCOPY_HEAP_FLAG_TO(void)
{
	do_usercopy_heap_flag(true);
}

void lkdtm_USERCOPY_HEAP_FLAG_FROM(void)
{
	do_usercopy_heap_flag(false);
}

void lkdtm_USERCOPY_STACK_FRAME_TO(void)
{
	do_usercopy_stack(true, true);
}

void lkdtm_USERCOPY_STACK_FRAME_FROM(void)
{
	do_usercopy_stack(false, true);
}

void lkdtm_USERCOPY_STACK_BEYOND(void)
{
	do_usercopy_stack(true, false);
}

void lkdtm_USERCOPY_KERNEL(void)
{
	unsigned long user_addr;

	user_addr = vm_mmap(NULL, 0, PAGE_SIZE,
			    PROT_READ | PROT_WRITE | PROT_EXEC,
			    MAP_ANONYMOUS | MAP_PRIVATE, 0);
	if (user_addr >= TASK_SIZE) {
		pr_warn("Failed to allocate user memory\n");
		return;
	}

	pr_info("attempting good copy_to_user from kernel rodata\n");
	if (copy_to_user((void __user *)user_addr, test_text,
			 sizeof(test_text))) {
		pr_warn("copy_to_user failed unexpectedly?!\n");
		goto free_user;
	}

	pr_info("attempting bad copy_to_user from kernel text\n");
	if (copy_to_user((void __user *)user_addr, vm_mmap, PAGE_SIZE)) {
		pr_warn("copy_to_user failed, but lacked Oops\n");
		goto free_user;
	}

free_user:
	vm_munmap(user_addr, PAGE_SIZE);
}

void __init lkdtm_usercopy_init(void)
{
	/* Prepare cache that lacks SLAB_USERCOPY flag. */
	bad_cache = kmem_cache_create("lkdtm-no-usercopy", cache_size, 0,
				      0, NULL);
}

void __exit lkdtm_usercopy_exit(void)
{
	kmem_cache_destroy(bad_cache);
}
