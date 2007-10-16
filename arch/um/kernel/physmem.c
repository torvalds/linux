/*
 * Copyright (C) 2000 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Licensed under the GPL
 */

#include "linux/bootmem.h"
#include "linux/mm.h"
#include "linux/pfn.h"
#include "asm/page.h"
#include "as-layout.h"
#include "init.h"
#include "kern.h"
#include "mem_user.h"
#include "os.h"

static int physmem_fd = -1;

/* Changed during early boot */
unsigned long high_physmem;

extern unsigned long long physmem_size;

int __init init_maps(unsigned long physmem, unsigned long iomem,
		     unsigned long highmem)
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

	map = alloc_bootmem_low_pages(total_len);
	if (map == NULL)
		return -ENOMEM;

	for (i = 0; i < total_pages; i++) {
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
	if (kmem_top == 0)
		kmem_top = host_task_size - 1024 * 1024;
	return kmem_top;
}

void map_memory(unsigned long virt, unsigned long phys, unsigned long len,
		int r, int w, int x)
{
	__u64 offset;
	int fd, err;

	fd = phys_mapping(phys, &offset);
	err = os_map_memory((void *) virt, fd, offset, len, r, w, x);
	if (err) {
		if (err == -ENOMEM)
			printk(KERN_ERR "try increasing the host's "
			       "/proc/sys/vm/max_map_count to <physical "
			       "memory size>/4096\n");
		panic("map_memory(0x%lx, %d, 0x%llx, %ld, %d, %d, %d) failed, "
		      "err = %d\n", virt, fd, offset, len, r, w, x, err);
	}
}

extern int __syscall_stub_start;

void __init setup_physmem(unsigned long start, unsigned long reserve_end,
			  unsigned long len, unsigned long long highmem)
{
	unsigned long reserve = reserve_end - start;
	int pfn = PFN_UP(__pa(reserve_end));
	int delta = (len - reserve) >> PAGE_SHIFT;
	int err, offset, bootmap_size;

	physmem_fd = create_mem_file(len + highmem);

	offset = uml_reserved - uml_physmem;
	err = os_map_memory((void *) uml_reserved, physmem_fd, offset,
			    len - offset, 1, 1, 1);
	if (err < 0) {
		os_print_error(err, "Mapping memory");
		exit(1);
	}

	/*
	 * Special kludge - This page will be mapped in to userspace processes
	 * from physmem_fd, so it needs to be written out there.
	 */
	os_seek_file(physmem_fd, __pa(&__syscall_stub_start));
	os_write_file(physmem_fd, &__syscall_stub_start, PAGE_SIZE);

	bootmap_size = init_bootmem(pfn, pfn + delta);
	free_bootmem(__pa(reserve_end) + bootmap_size,
		     len - bootmap_size - reserve);
}

int phys_mapping(unsigned long phys, unsigned long long *offset_out)
{
	int fd = -1;

	if (phys < physmem_size) {
		fd = physmem_fd;
		*offset_out = phys;
	}
	else if (phys < __pa(end_iomem)) {
		struct iomem_region *region = iomem_regions;

		while (region != NULL) {
			if ((phys >= region->phys) &&
			    (phys < region->phys + region->size)) {
				fd = region->fd;
				*offset_out = phys - region->phys;
				break;
			}
			region = region->next;
		}
	}
	else if (phys < __pa(end_iomem) + highmem) {
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

	while (region != NULL) {
		if (!strcmp(region->driver, driver)) {
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

	while (region != NULL) {
		err = os_map_memory((void *) iomem_start, region->fd, 0,
				    region->size, 1, 1, 0);
		if (err)
			printk(KERN_ERR "Mapping iomem region for driver '%s' "
			       "failed, errno = %d\n", region->driver, -err);
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
