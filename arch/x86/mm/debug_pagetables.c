// SPDX-License-Identifier: GPL-2.0-only
#include <linux/debugfs.h>
#include <linux/efi.h>
#include <linux/module.h>
#include <linux/seq_file.h>
#include <linux/pgtable.h>

static int ptdump_show(struct seq_file *m, void *v)
{
	ptdump_walk_pgd_level_debugfs(m, &init_mm, false);
	return 0;
}

DEFINE_SHOW_ATTRIBUTE(ptdump);

static int ptdump_curknl_show(struct seq_file *m, void *v)
{
	if (current->mm->pgd)
		ptdump_walk_pgd_level_debugfs(m, current->mm, false);
	return 0;
}

DEFINE_SHOW_ATTRIBUTE(ptdump_curknl);

#ifdef CONFIG_PAGE_TABLE_ISOLATION
static int ptdump_curusr_show(struct seq_file *m, void *v)
{
	if (current->mm->pgd)
		ptdump_walk_pgd_level_debugfs(m, current->mm, true);
	return 0;
}

DEFINE_SHOW_ATTRIBUTE(ptdump_curusr);
#endif

#if defined(CONFIG_EFI) && defined(CONFIG_X86_64)
static int ptdump_efi_show(struct seq_file *m, void *v)
{
	if (efi_mm.pgd)
		ptdump_walk_pgd_level_debugfs(m, &efi_mm, false);
	return 0;
}

DEFINE_SHOW_ATTRIBUTE(ptdump_efi);
#endif

static struct dentry *dir;

static int __init pt_dump_debug_init(void)
{
	dir = debugfs_create_dir("page_tables", NULL);

	debugfs_create_file("kernel", 0400, dir, NULL, &ptdump_fops);
	debugfs_create_file("current_kernel", 0400, dir, NULL,
			    &ptdump_curknl_fops);

#ifdef CONFIG_PAGE_TABLE_ISOLATION
	debugfs_create_file("current_user", 0400, dir, NULL,
			    &ptdump_curusr_fops);
#endif
#if defined(CONFIG_EFI) && defined(CONFIG_X86_64)
	debugfs_create_file("efi", 0400, dir, NULL, &ptdump_efi_fops);
#endif
	return 0;
}

static void __exit pt_dump_debug_exit(void)
{
	debugfs_remove_recursive(dir);
}

module_init(pt_dump_debug_init);
module_exit(pt_dump_debug_exit);
MODULE_AUTHOR("Arjan van de Ven <arjan@linux.intel.com>");
MODULE_DESCRIPTION("Kernel debugging helper that dumps pagetables");
