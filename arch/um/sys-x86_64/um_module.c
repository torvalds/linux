#include <linux/vmalloc.h>
#include <linux/moduleloader.h>

/* Copied from i386 arch/i386/kernel/module.c */
void *module_alloc(unsigned long size)
{
	if (size == 0)
		return NULL;
	return vmalloc_exec(size);
}

/* Free memory returned from module_alloc */
void module_free(struct module *mod, void *module_region)
{
	vfree(module_region);
	/*
	 * FIXME: If module_region == mod->init_region, trim exception
	 * table entries.
	 */
}

