/*
 * drivers/gpu/ion/ion_system_mapper.c
 *
 * Copyright (C) 2011 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/err.h>
#include <linux/ion.h>
#include <linux/memory.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include "ion_priv.h"
/*
 * This mapper is valid for any heap that allocates memory that already has
 * a kernel mapping, this includes vmalloc'd memory, kmalloc'd memory,
 * pages obtained via io_remap, etc.
 */
static void *ion_kernel_mapper_map(struct ion_mapper *mapper,
				   struct ion_buffer *buffer,
				   struct ion_mapping **mapping)
{
	if (!((1 << buffer->heap->type) & mapper->heap_mask)) {
		pr_err("%s: attempting to map an unsupported heap\n", __func__);
		return ERR_PTR(-EINVAL);
	}
	/* XXX REVISIT ME!!! */
	*((unsigned long *)mapping) = (unsigned long)buffer->priv;
	return buffer->priv;
}

static void ion_kernel_mapper_unmap(struct ion_mapper *mapper,
				    struct ion_buffer *buffer,
				    struct ion_mapping *mapping)
{
	if (!((1 << buffer->heap->type) & mapper->heap_mask))
		pr_err("%s: attempting to unmap an unsupported heap\n",
		       __func__);
}

static void *ion_kernel_mapper_map_kernel(struct ion_mapper *mapper,
					struct ion_buffer *buffer,
					struct ion_mapping *mapping)
{
	if (!((1 << buffer->heap->type) & mapper->heap_mask)) {
		pr_err("%s: attempting to unmap an unsupported heap\n",
		       __func__);
		return ERR_PTR(-EINVAL);
	}
	return buffer->priv;
}

static int ion_kernel_mapper_map_user(struct ion_mapper *mapper,
				      struct ion_buffer *buffer,
				      struct vm_area_struct *vma,
				      struct ion_mapping *mapping,
				      unsigned long flags)
{
	int ret;

	switch (buffer->heap->type) {
	case ION_HEAP_KMALLOC:
	{
		unsigned long pfn = __phys_to_pfn(virt_to_phys(buffer->priv));
		ret = remap_pfn_range(vma, vma->vm_start, pfn + vma->vm_pgoff,
				      vma->vm_end - vma->vm_start,
				      vma->vm_page_prot);
		break;
	}
	case ION_HEAP_VMALLOC:
		ret = remap_vmalloc_range(vma, buffer->priv, vma->vm_pgoff);
		break;
	default:
		pr_err("%s: attempting to map unsupported heap to userspace\n",
		       __func__);
		return -EINVAL;
	}

	return ret;
}

static struct ion_mapper_ops ops = {
	.map = ion_kernel_mapper_map,
	.map_kernel = ion_kernel_mapper_map_kernel,
	.map_user = ion_kernel_mapper_map_user,
	.unmap = ion_kernel_mapper_unmap,
};

struct ion_mapper *ion_system_mapper_create(void)
{
	struct ion_mapper *mapper;
	mapper = kzalloc(sizeof(struct ion_mapper), GFP_KERNEL);
	if (!mapper)
		return ERR_PTR(-ENOMEM);
	mapper->type = ION_SYSTEM_MAPPER;
	mapper->ops = &ops;
	mapper->heap_mask = (1 << ION_HEAP_VMALLOC) | (1 << ION_HEAP_KMALLOC);
	return mapper;
}

void ion_system_mapper_destroy(struct ion_mapper *mapper)
{
	kfree(mapper);
}

