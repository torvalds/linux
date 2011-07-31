#ifndef VMREGION_H
#define VMREGION_H

#include <linux/spinlock.h>
#include <linux/list.h>

struct page;

struct arm_vmregion_head {
	spinlock_t		vm_lock;
	struct list_head	vm_list;
	unsigned long		vm_start;
	unsigned long		vm_end;
};

struct arm_vmregion {
	struct list_head	vm_list;
	unsigned long		vm_start;
	unsigned long		vm_end;
	struct page		*vm_pages;
	int			vm_active;
};

struct arm_vmregion *arm_vmregion_alloc(struct arm_vmregion_head *, size_t, size_t, gfp_t);
struct arm_vmregion *arm_vmregion_find(struct arm_vmregion_head *, unsigned long);
struct arm_vmregion *arm_vmregion_find_remove(struct arm_vmregion_head *, unsigned long);
void arm_vmregion_free(struct arm_vmregion_head *, struct arm_vmregion *);

#endif
