// SPDX-License-Identifier: GPL-2.0-only
#include <linux/init_task.h>
#include <linux/kasan.h>
#include <asm/host_ops.h>

void kasan_unpoison_stack(void)
{
	void *stack = NULL;
	unsigned long stack_size;

	if (lkl_ops->thread_stack)
		stack = lkl_ops->thread_stack(&stack_size);

	if (stack)
		kasan_unpoison_range(stack, stack_size);
}

int kasan_cleanup(void)
{
	return lkl_ops->munmap((void *)KASAN_SHADOW_OFFSET, KASAN_SHADOW_SIZE);
}

int kasan_init(void)
{
	void *offset = (void *)KASAN_SHADOW_OFFSET;
	int prot = LKL_PROT_READ | LKL_PROT_WRITE;

	/* reserve address range for KASAN shadow memory */
	if (lkl_ops->mmap(offset, KASAN_SHADOW_SIZE, prot) != offset) {
		lkl_printf("kasan: failed to map shadow memory\n");
		return -EFAULT;
	}

	init_task.kasan_depth = 0;
	pr_info("KernelAddressSanitizer initialized\n");

	return 0;
}
