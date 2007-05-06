/*
 * Copyright (C) 2000 - 2003 Jeff Dike (jdike@addtoit.com)
 * Licensed under the GPL
 */

#include "linux/mm.h"
#include "linux/rbtree.h"
#include "linux/slab.h"
#include "linux/vmalloc.h"
#include "linux/bootmem.h"
#include "linux/module.h"
#include "linux/pfn.h"
#include "asm/types.h"
#include "asm/pgtable.h"
#include "kern_util.h"
#include "as-layout.h"
#include "mode_kern.h"
#include "mem.h"
#include "mem_user.h"
#include "os.h"
#include "kern.h"
#include "init.h"

struct phys_desc {
	struct rb_node rb;
	int fd;
	__u64 offset;
	void *virt;
	unsigned long phys;
	struct list_head list;
};

static struct rb_root phys_mappings = RB_ROOT;

static struct rb_node **find_rb(void *virt)
{
	struct rb_node **n = &phys_mappings.rb_node;
	struct phys_desc *d;

	while(*n != NULL){
		d = rb_entry(*n, struct phys_desc, rb);
		if(d->virt == virt)
			return n;

		if(d->virt > virt)
			n = &(*n)->rb_left;
		else
			n = &(*n)->rb_right;
	}

	return n;
}

static struct phys_desc *find_phys_mapping(void *virt)
{
	struct rb_node **n = find_rb(virt);

	if(*n == NULL)
		return NULL;

	return rb_entry(*n, struct phys_desc, rb);
}

static void insert_phys_mapping(struct phys_desc *desc)
{
	struct rb_node **n = find_rb(desc->virt);

	if(*n != NULL)
		panic("Physical remapping for %p already present",
		      desc->virt);

	rb_link_node(&desc->rb, rb_parent(*n), n);
	rb_insert_color(&desc->rb, &phys_mappings);
}

LIST_HEAD(descriptor_mappings);

struct desc_mapping {
	int fd;
	struct list_head list;
	struct list_head pages;
};

static struct desc_mapping *find_mapping(int fd)
{
	struct desc_mapping *desc;
	struct list_head *ele;

	list_for_each(ele, &descriptor_mappings){
		desc = list_entry(ele, struct desc_mapping, list);
		if(desc->fd == fd)
			return desc;
	}

	return NULL;
}

static struct desc_mapping *descriptor_mapping(int fd)
{
	struct desc_mapping *desc;

	desc = find_mapping(fd);
	if(desc != NULL)
		return desc;

	desc = kmalloc(sizeof(*desc), GFP_ATOMIC);
	if(desc == NULL)
		return NULL;

	*desc = ((struct desc_mapping)
		{ .fd =		fd,
		  .list =	LIST_HEAD_INIT(desc->list),
		  .pages =	LIST_HEAD_INIT(desc->pages) });
	list_add(&desc->list, &descriptor_mappings);

	return desc;
}

int physmem_subst_mapping(void *virt, int fd, __u64 offset, int w)
{
	struct desc_mapping *fd_maps;
	struct phys_desc *desc;
	unsigned long phys;
	int err;

	fd_maps = descriptor_mapping(fd);
	if(fd_maps == NULL)
		return -ENOMEM;

	phys = __pa(virt);
	desc = find_phys_mapping(virt);
	if(desc != NULL)
		panic("Address 0x%p is already substituted\n", virt);

	err = -ENOMEM;
	desc = kmalloc(sizeof(*desc), GFP_ATOMIC);
	if(desc == NULL)
		goto out;

	*desc = ((struct phys_desc)
		{ .fd =			fd,
		  .offset =		offset,
		  .virt =		virt,
		  .phys =		__pa(virt),
		  .list = 		LIST_HEAD_INIT(desc->list) });
	insert_phys_mapping(desc);

	list_add(&desc->list, &fd_maps->pages);

	virt = (void *) ((unsigned long) virt & PAGE_MASK);
	err = os_map_memory(virt, fd, offset, PAGE_SIZE, 1, w, 0);
	if(!err)
		goto out;

	rb_erase(&desc->rb, &phys_mappings);
	kfree(desc);
 out:
	return err;
}

static int physmem_fd = -1;

static void remove_mapping(struct phys_desc *desc)
{
	void *virt = desc->virt;
	int err;

	rb_erase(&desc->rb, &phys_mappings);
	list_del(&desc->list);
	kfree(desc);

	err = os_map_memory(virt, physmem_fd, __pa(virt), PAGE_SIZE, 1, 1, 0);
	if(err)
		panic("Failed to unmap block device page from physical memory, "
		      "errno = %d", -err);
}

int physmem_remove_mapping(void *virt)
{
	struct phys_desc *desc;

	virt = (void *) ((unsigned long) virt & PAGE_MASK);
	desc = find_phys_mapping(virt);
	if(desc == NULL)
		return 0;

	remove_mapping(desc);
	return 1;
}

void physmem_forget_descriptor(int fd)
{
	struct desc_mapping *desc;
	struct phys_desc *page;
	struct list_head *ele, *next;
	__u64 offset;
	void *addr;
	int err;

	desc = find_mapping(fd);
	if(desc == NULL)
		return;

	list_for_each_safe(ele, next, &desc->pages){
		page = list_entry(ele, struct phys_desc, list);
		offset = page->offset;
		addr = page->virt;
		remove_mapping(page);
		err = os_seek_file(fd, offset);
		if(err)
			panic("physmem_forget_descriptor - failed to seek "
			      "to %lld in fd %d, error = %d\n",
			      offset, fd, -err);
		err = os_read_file(fd, addr, PAGE_SIZE);
		if(err < 0)
			panic("physmem_forget_descriptor - failed to read "
			      "from fd %d to 0x%p, error = %d\n",
			      fd, addr, -err);
	}

	list_del(&desc->list);
	kfree(desc);
}

EXPORT_SYMBOL(physmem_forget_descriptor);
EXPORT_SYMBOL(physmem_remove_mapping);
EXPORT_SYMBOL(physmem_subst_mapping);

void arch_free_page(struct page *page, int order)
{
	void *virt;
	int i;

	for(i = 0; i < (1 << order); i++){
		virt = __va(page_to_phys(page + i));
		physmem_remove_mapping(virt);
	}
}

int is_remapped(void *virt)
{
 	struct phys_desc *desc = find_phys_mapping(virt);

	return desc != NULL;
}

/* Changed during early boot */
unsigned long high_physmem;

extern unsigned long long physmem_size;

int init_maps(unsigned long physmem, unsigned long iomem, unsigned long highmem)
{
	struct page *p, *map;
	unsigned long phys_len, phys_pages, highmem_len, highmem_pages;
	unsigned long iomem_len, iomem_pages, total_len, total_pages;
	int i;

	phys_pages = physmem >> PAGE_SHIFT;
	phys_len = phys_pages * sizeof(struct page);

	iomem_pages = iomem >> PAGE_SHIFT;
	iomem_len = iomem_pages * sizeof(struct page);

	highmem_pages = highmem >> PAGE_SHIFT;
	highmem_len = highmem_pages * sizeof(struct page);

	total_pages = phys_pages + iomem_pages + highmem_pages;
	total_len = phys_len + iomem_len + highmem_len;

	if(kmalloc_ok){
		map = kmalloc(total_len, GFP_KERNEL);
		if(map == NULL)
			map = vmalloc(total_len);
	}
	else map = alloc_bootmem_low_pages(total_len);

	if(map == NULL)
		return -ENOMEM;

	for(i = 0; i < total_pages; i++){
		p = &map[i];
		memset(p, 0, sizeof(struct page));
		SetPageReserved(p);
		INIT_LIST_HEAD(&p->lru);
	}

	max_mapnr = total_pages;
	return 0;
}

/* Changed during early boot */
static unsigned long kmem_top = 0;

unsigned long get_kmem_end(void)
{
	if(kmem_top == 0)
		kmem_top = CHOOSE_MODE(kmem_end_tt, kmem_end_skas);
	return kmem_top;
}

void map_memory(unsigned long virt, unsigned long phys, unsigned long len,
		int r, int w, int x)
{
	__u64 offset;
	int fd, err;

	fd = phys_mapping(phys, &offset);
	err = os_map_memory((void *) virt, fd, offset, len, r, w, x);
	if(err) {
		if(err == -ENOMEM)
			printk("try increasing the host's "
			       "/proc/sys/vm/max_map_count to <physical "
			       "memory size>/4096\n");
		panic("map_memory(0x%lx, %d, 0x%llx, %ld, %d, %d, %d) failed, "
		      "err = %d\n", virt, fd, offset, len, r, w, x, err);
	}
}

extern int __syscall_stub_start;

void setup_physmem(unsigned long start, unsigned long reserve_end,
		   unsigned long len, unsigned long long highmem)
{
	unsigned long reserve = reserve_end - start;
	int pfn = PFN_UP(__pa(reserve_end));
	int delta = (len - reserve) >> PAGE_SHIFT;
	int err, offset, bootmap_size;

	physmem_fd = create_mem_file(len + highmem);

	offset = uml_reserved - uml_physmem;
	err = os_map_memory((void *) uml_reserved, physmem_fd, offset,
			    len - offset, 1, 1, 0);
	if(err < 0){
		os_print_error(err, "Mapping memory");
		exit(1);
	}

	/* Special kludge - This page will be mapped in to userspace processes
	 * from physmem_fd, so it needs to be written out there.
	 */
	os_seek_file(physmem_fd, __pa(&__syscall_stub_start));
	os_write_file_k(physmem_fd, &__syscall_stub_start, PAGE_SIZE);

	bootmap_size = init_bootmem(pfn, pfn + delta);
	free_bootmem(__pa(reserve_end) + bootmap_size,
		     len - bootmap_size - reserve);
}

int phys_mapping(unsigned long phys, __u64 *offset_out)
{
	struct phys_desc *desc = find_phys_mapping(__va(phys & PAGE_MASK));
	int fd = -1;

	if(desc != NULL){
		fd = desc->fd;
		*offset_out = desc->offset;
	}
	else if(phys < physmem_size){
		fd = physmem_fd;
		*offset_out = phys;
	}
	else if(phys < __pa(end_iomem)){
		struct iomem_region *region = iomem_regions;

		while(region != NULL){
			if((phys >= region->phys) &&
			   (phys < region->phys + region->size)){
				fd = region->fd;
				*offset_out = phys - region->phys;
				break;
			}
			region = region->next;
		}
	}
	else if(phys < __pa(end_iomem) + highmem){
		fd = physmem_fd;
		*offset_out = phys - iomem_size;
	}

	return fd;
}

static int __init uml_mem_setup(char *line, int *add)
{
	char *retptr;
	physmem_size = memparse(line,&retptr);
	return 0;
}
__uml_setup("mem=", uml_mem_setup,
"mem=<Amount of desired ram>\n"
"    This controls how much \"physical\" memory the kernel allocates\n"
"    for the system. The size is specified as a number followed by\n"
"    one of 'k', 'K', 'm', 'M', which have the obvious meanings.\n"
"    This is not related to the amount of memory in the host.  It can\n"
"    be more, and the excess, if it's ever used, will just be swapped out.\n"
"	Example: mem=64M\n\n"
);

extern int __init parse_iomem(char *str, int *add);

__uml_setup("iomem=", parse_iomem,
"iomem=<name>,<file>\n"
"    Configure <file> as an IO memory region named <name>.\n\n"
);

/*
 * This list is constructed in parse_iomem and addresses filled in in
 * setup_iomem, both of which run during early boot.  Afterwards, it's
 * unchanged.
 */
struct iomem_region *iomem_regions = NULL;

/* Initialized in parse_iomem */
int iomem_size = 0;

unsigned long find_iomem(char *driver, unsigned long *len_out)
{
	struct iomem_region *region = iomem_regions;

	while(region != NULL){
		if(!strcmp(region->driver, driver)){
			*len_out = region->size;
			return region->virt;
		}

		region = region->next;
	}

	return 0;
}

int setup_iomem(void)
{
	struct iomem_region *region = iomem_regions;
	unsigned long iomem_start = high_physmem + PAGE_SIZE;
	int err;

	while(region != NULL){
		err = os_map_memory((void *) iomem_start, region->fd, 0,
				    region->size, 1, 1, 0);
		if(err)
			printk("Mapping iomem region for driver '%s' failed, "
			       "errno = %d\n", region->driver, -err);
		else {
			region->virt = iomem_start;
			region->phys = __pa(region->virt);
		}

		iomem_start += region->size + PAGE_SIZE;
		region = region->next;
	}

	return 0;
}

__initcall(setup_iomem);
