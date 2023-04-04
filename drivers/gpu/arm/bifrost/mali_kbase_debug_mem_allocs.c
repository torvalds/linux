// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2022-2023 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 */

/*
 * Debugfs interface to dump information about GPU allocations in kctx
 */

#include "mali_kbase_debug_mem_allocs.h"
#include "mali_kbase.h"

#include <linux/string.h>
#include <linux/list.h>
#include <linux/file.h>

#if IS_ENABLED(CONFIG_DEBUG_FS)

/**
 * debug_zone_mem_allocs_show - Show information from specific rbtree
 * @zone: Name of GPU virtual memory zone
 * @rbtree: Pointer to the root of the rbtree associated with @zone
 * @sfile: The debugfs entry
 *
 * This function is called to show information about all the GPU allocations of a
 * a particular zone within GPU virtual memory space of a context.
 * The information like the start virtual address and size (in bytes) is shown for
 * every GPU allocation mapped in the zone.
 */
static void debug_zone_mem_allocs_show(char *zone, struct rb_root *rbtree, struct seq_file *sfile)
{
	struct rb_node *p;
	struct kbase_va_region *reg;
	const char *type_names[5] = {
		"Native",
		"Imported UMM",
		"Imported user buf",
		"Alias",
		"Raw"
	};

#define MEM_ALLOCS_HEADER \
	"              VA,          VA size,      Commit size,    Flags,     Mem type\n"
	seq_printf(sfile, "Zone name: %s\n:", zone);
	seq_printf(sfile, MEM_ALLOCS_HEADER);
	for (p = rb_first(rbtree); p; p = rb_next(p)) {
		reg = rb_entry(p, struct kbase_va_region, rblink);
		if (!(reg->flags & KBASE_REG_FREE)) {
			seq_printf(sfile, "%16llx, %16zx, %16zx, %8lx, %s\n",
					reg->start_pfn << PAGE_SHIFT, reg->nr_pages << PAGE_SHIFT,
					kbase_reg_current_backed_size(reg) << PAGE_SHIFT,
					reg->flags, type_names[reg->gpu_alloc->type]);
		}
	}
}

/**
 * debug_ctx_mem_allocs_show - Show information about GPU allocations in a kctx
 * @sfile: The debugfs entry
 * @data: Data associated with the entry
 *
 * Return:
 * 0 if successfully prints data in debugfs entry file
 * -1 if it encountered an error
 */
static int debug_ctx_mem_allocs_show(struct seq_file *sfile, void *data)
{
	struct kbase_context *const kctx = sfile->private;

	kbase_gpu_vm_lock(kctx);

	debug_zone_mem_allocs_show("SAME_VA:", &kctx->reg_rbtree_same, sfile);
	debug_zone_mem_allocs_show("CUSTOM_VA:",  &kctx->reg_rbtree_custom, sfile);
	debug_zone_mem_allocs_show("EXEC_VA:", &kctx->reg_rbtree_exec, sfile);

#if MALI_USE_CSF
	debug_zone_mem_allocs_show("EXEC_VA_FIXED:", &kctx->reg_rbtree_exec_fixed, sfile);
	debug_zone_mem_allocs_show("FIXED_VA:", &kctx->reg_rbtree_fixed, sfile);
#endif /* MALI_USE_CSF */

	kbase_gpu_vm_unlock(kctx);
	return 0;
}

/*
 *  File operations related to debugfs entry for mem_zones
 */
static int debug_mem_allocs_open(struct inode *in, struct file *file)
{
	return single_open(file, debug_ctx_mem_allocs_show, in->i_private);
}

static const struct file_operations kbase_debug_mem_allocs_fops = {
	.owner = THIS_MODULE,
	.open = debug_mem_allocs_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

/*
 *  Initialize debugfs entry for mem_allocs
 */
void kbase_debug_mem_allocs_init(struct kbase_context *const kctx)
{
	/* Caller already ensures this, but we keep the pattern for
	 * maintenance safety.
	 */
	if (WARN_ON(!kctx) || WARN_ON(IS_ERR_OR_NULL(kctx->kctx_dentry)))
		return;

	debugfs_create_file("mem_allocs", 0400, kctx->kctx_dentry, kctx,
			    &kbase_debug_mem_allocs_fops);
}
#else
/*
 * Stub functions for when debugfs is disabled
 */
void kbase_debug_mem_allocs_init(struct kbase_context *const kctx)
{
}
#endif
