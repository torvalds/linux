/*
 * Copyright (C) 2001 Mike Corrigan & Dave Engebretsen IBM Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/init.h>
#include <linux/mm.h>
#include <linux/proc_fs.h>
#include <linux/kernel.h>

#include <asm/machdep.h>
#include <asm/vdso_datapage.h>
#include <asm/rtas.h>
#include <asm/uaccess.h>
#include <asm/prom.h>

#ifdef CONFIG_PPC64

static loff_t page_map_seek( struct file *file, loff_t off, int whence)
{
	loff_t new;
	switch(whence) {
	case 0:
		new = off;
		break;
	case 1:
		new = file->f_pos + off;
		break;
	case 2:
		new = PAGE_SIZE + off;
		break;
	default:
		return -EINVAL;
	}
	if ( new < 0 || new > PAGE_SIZE )
		return -EINVAL;
	return (file->f_pos = new);
}

static ssize_t page_map_read( struct file *file, char __user *buf, size_t nbytes,
			      loff_t *ppos)
{
	return simple_read_from_buffer(buf, nbytes, ppos,
			PDE_DATA(file_inode(file)), PAGE_SIZE);
}

static int page_map_mmap( struct file *file, struct vm_area_struct *vma )
{
	if ((vma->vm_end - vma->vm_start) > PAGE_SIZE)
		return -EINVAL;

	remap_pfn_range(vma, vma->vm_start,
			__pa(PDE_DATA(file_inode(file))) >> PAGE_SHIFT,
			PAGE_SIZE, vma->vm_page_prot);
	return 0;
}

static const struct file_operations page_map_fops = {
	.llseek	= page_map_seek,
	.read	= page_map_read,
	.mmap	= page_map_mmap
};


static int __init proc_ppc64_init(void)
{
	struct proc_dir_entry *pde;

	pde = proc_create_data("powerpc/systemcfg", S_IFREG|S_IRUGO, NULL,
			       &page_map_fops, vdso_data);
	if (!pde)
		return 1;
	proc_set_size(pde, PAGE_SIZE);

	return 0;
}
__initcall(proc_ppc64_init);

#endif /* CONFIG_PPC64 */

/*
 * Create the ppc64 and ppc64/rtas directories early. This allows us to
 * assume that they have been previously created in drivers.
 */
static int __init proc_ppc64_create(void)
{
	struct proc_dir_entry *root;

	root = proc_mkdir("powerpc", NULL);
	if (!root)
		return 1;

#ifdef CONFIG_PPC64
	if (!proc_symlink("ppc64", NULL, "powerpc"))
		pr_err("Failed to create link /proc/ppc64 -> /proc/powerpc\n");
#endif

	if (!of_find_node_by_path("/rtas"))
		return 0;

	if (!proc_mkdir("rtas", root))
		return 1;

	if (!proc_symlink("rtas", NULL, "powerpc/rtas"))
		return 1;

	return 0;
}
core_initcall(proc_ppc64_create);
