#include <linux/spinlock.h>
#include <linux/list.h>
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
arm_vmregion_alloc(struct arm_vmregion_head *head, size_t size, gfp_t gfp)
{
	unsigned long addr = head->vm_start, end = head->vm_end - size;
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

	spin_lock_irqsave(&head->vm_lock, flags);

	list_for_each_entry(c, &head->vm_list, vm_list) {
		if ((addr + size) < addr)
			goto nospc;
		if ((addr + size) <= c->vm_start)
			goto found;
		addr = c->vm_end;
		if (addr > end)
			goto nospc;
	}

 found:
	/*
	 * Insert this entry _before_ the one we found.
	 */
	list_add_tail(&new->vm_list, &c->vm_list);
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
