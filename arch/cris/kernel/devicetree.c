#include <linux/init.h>
#include <linux/bootmem.h>
#include <linux/printk.h>

void __init early_init_dt_add_memory_arch(u64 base, u64 size)
{
	pr_err("%s(%llx, %llx)\n",
	       __func__, base, size);
}

void * __init early_init_dt_alloc_memory_arch(u64 size, u64 align)
{
	return alloc_bootmem_align(size, align);
}
