#include <linux/fs.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>

#include "vmregion.h"

/*
 * VM region handling support.
 *
 * This should become something generic, handling VM region allocations for
 * vmalloc and similar (ioremap, module space, etc).
 *
 * I envisage vmalloc()'s supporting vm_struct becoming:
 *
 *  struct vm_struct {
 *    struct vmregion	region;
 *    unsigned long	flags;
 *    struct page	**pages;
 *    unsigned int	nr_pages;
 *    unsigned long	phys_addr;
 *  };
 *
 * get_vm_area() would then call vmregion_alloc with an appropriate
 * struct vmregion head (eg):
 *
 *  struct vmregion vmalloc_head = {
 *	.vm_list	= LIST_HEAD_INIT(vmalloc_head.vm_list),
 *	.vm_start	= VMALLOC_START,
 *	.vm_end		= VMALLOC_END,
 *  };
 *
 * However, vmalloc_head.vm_start is variable (typically, it is dependent on
 * the amount of RAM found at boot time.)  I would imagine that get_vm_area()
 * would have to initialise this each time prior to calling vmregion_alloc().
 */

struct arm_vmregion *
arm_vmregion_alloc(struct arm_vmregion_head *head, size_t align,
		   size_t size, gfp_t gfp, const void *caller)
{
	unsigned long start = head->vm_start, addr = head->vm_end;
	unsigned long flags;
	struct arm_vmregion *c, *new;

	if (head->vm_end - head->vm_start < size) {
		printk(KERN_WARNING "%s: allocation too big (requested %#x)\n",
			__func__, size);
		goto out;
	}

	new = kmalloc(sizeof(struct arm_vmregion), gfp);
	if (!new)
		goto out;

	new->caller = caller;

	spin_lock_irqsave(&head->vm_lock, flags);

	addr = rounddown(addr - size, align);
	list_for_each_entry_reverse(c, &head->vm_list, vm_list) {
		if (addr >= c->vm_end)
			goto found;
		addr = rounddown(c->vm_start - size, align);
		if (addr < start)
			goto nospc;
	}

 found:
	/*
	 * Insert this entry after the one we found.
	 */
	list_add(&new->vm_list, &c->vm_list);
	new->vm_start = addr;
	new->vm_end = addr + size;
	new->vm_active = 1;

	spin_unlock_irqrestore(&head->vm_lock, flags);
	return new;

 nospc:
	spin_unlock_irqrestore(&head->vm_lock, flags);
	kfree(new);
 out:
	return NULL;
}

static struct arm_vmregion *__arm_vmregion_find(struct arm_vmregion_head *head, unsigned long addr)
{
	struct arm_vmregion *c;

	list_for_each_entry(c, &head->vm_list, vm_list) {
		if (c->vm_active && c->vm_start == addr)
			goto out;
	}
	c = NULL;
 out:
	return c;
}

struct arm_vmregion *arm_vmregion_find(struct arm_vmregion_head *head, unsigned long addr)
{
	struct arm_vmregion *c;
	unsigned long flags;

	spin_lock_irqsave(&head->vm_lock, flags);
	c = __arm_vmregion_find(head, addr);
	spin_unlock_irqrestore(&head->vm_lock, flags);
	return c;
}

struct arm_vmregion *arm_vmregion_find_remove(struct arm_vmregion_head *head, unsigned long addr)
{
	struct arm_vmregion *c;
	unsigned long flags;

	spin_lock_irqsave(&head->vm_lock, flags);
	c = __arm_vmregion_find(head, addr);
	if (c)
		c->vm_active = 0;
	spin_unlock_irqrestore(&head->vm_lock, flags);
	return c;
}

void arm_vmregion_free(struct arm_vmregion_head *head, struct arm_vmregion *c)
{
	unsigned long flags;

	spin_lock_irqsave(&head->vm_lock, flags);
	list_del(&c->vm_list);
	spin_unlock_irqrestore(&head->vm_lock, flags);

	kfree(c);
}

#ifdef CONFIG_PROC_FS
static int arm_vmregion_show(struct seq_file *m, void *p)
{
	struct arm_vmregion *c = list_entry(p, struct arm_vmregion, vm_list);

	seq_printf(m, "0x%08lx-0x%08lx %7lu", c->vm_start, c->vm_end,
		c->vm_end - c->vm_start);
	if (c->caller)
		seq_printf(m, " %pS", (void *)c->caller);
	seq_putc(m, '\n');
	return 0;
}

static void *arm_vmregion_start(struct seq_file *m, loff_t *pos)
{
	struct arm_vmregion_head *h = m->private;
	spin_lock_irq(&h->vm_lock);
	return seq_list_start(&h->vm_list, *pos);
}

static void *arm_vmregion_next(struct seq_file *m, void *p, loff_t *pos)
{
	struct arm_vmregion_head *h = m->private;
	return seq_list_next(p, &h->vm_list, pos);
}

static void arm_vmregion_stop(struct seq_file *m, void *p)
{
	struct arm_vmregion_head *h = m->private;
	spin_unlock_irq(&h->vm_lock);
}

static const struct seq_operations arm_vmregion_ops = {
	.start	= arm_vmregion_start,
	.stop	= arm_vmregion_stop,
	.next	= arm_vmregion_next,
	.show	= arm_vmregion_show,
};

static int arm_vmregion_open(struct inode *inode, struct file *file)
{
	struct arm_vmregion_head *h = PDE(inode)->data;
	int ret = seq_open(file, &arm_vmregion_ops);
	if (!ret) {
		struct seq_file *m = file->private_data;
		m->private = h;
	}
	return ret;
}

static const struct file_operations arm_vmregion_fops = {
	.open	= arm_vmregion_open,
	.read	= seq_read,
	.llseek	= seq_lseek,
	.release = seq_release,
};

int arm_vmregion_create_proc(const char *path, struct arm_vmregion_head *h)
{
	proc_create_data(path, S_IRUSR, NULL, &arm_vmregion_fops, h);
	return 0;
}
#else
int arm_vmregion_create_proc(const char *path, struct arm_vmregion_head *h)
{
	return 0;
}
#endif
