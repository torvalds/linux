// SPDX-License-Identifier: GPL-2.0
/*
 * arch/alpha/kernel/pci-sysfs.c
 *
 * Copyright (C) 2009 Ivan Kokshaysky
 *
 * Alpha PCI resource files.
 *
 * Loosely based on generic HAVE_PCI_MMAP implementation in
 * drivers/pci/pci-sysfs.c
 */

#include <linux/sched.h>
#include <linux/security.h>
#include <linux/pci.h>

static int hose_mmap_page_range(struct pci_controller *hose,
				struct vm_area_struct *vma,
				enum pci_mmap_state mmap_type, int sparse)
{
	unsigned long base;

	if (mmap_type == pci_mmap_mem)
		base = sparse ? hose->sparse_mem_base : hose->dense_mem_base;
	else
		base = sparse ? hose->sparse_io_base : hose->dense_io_base;

	vma->vm_pgoff += base >> PAGE_SHIFT;

	return io_remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff,
				  vma->vm_end - vma->vm_start,
				  vma->vm_page_prot);
}

static int __pci_mmap_fits(struct pci_dev *pdev, int num,
			   struct vm_area_struct *vma, int sparse)
{
	resource_size_t len = pci_resource_len(pdev, num);
	unsigned long nr, start, size;
	int shift = sparse ? 5 : 0;

	if (!len)
		return 0;

	nr = vma_pages(vma);
	start = vma->vm_pgoff;
	size = ((len - 1) >> (PAGE_SHIFT - shift)) + 1;

	return start < size && size - start >= nr;
}

/**
 * pci_mmap_resource - map a PCI resource into user memory space
 * @kobj: kobject for mapping
 * @attr: struct bin_attribute for the file being mapped
 * @vma: struct vm_area_struct passed into the mmap
 * @sparse: address space type
 *
 * Use the bus mapping routines to map a PCI resource into userspace.
 *
 * Return: %0 on success, negative error code otherwise
 */
static int pci_mmap_resource(struct kobject *kobj,
			     const struct bin_attribute *attr,
			     struct vm_area_struct *vma, int sparse)
{
	struct pci_dev *pdev = to_pci_dev(kobj_to_dev(kobj));
	int barno = (unsigned long)attr->private;
	enum pci_mmap_state mmap_type;
	struct pci_bus_region bar;
	int ret;

	ret = security_locked_down(LOCKDOWN_PCI_ACCESS);
	if (ret)
		return ret;

	if (pci_resource_is_mem(pdev, barno) &&
	    iomem_is_exclusive(pci_resource_start(pdev, barno)))
		return -EINVAL;

	if (!__pci_mmap_fits(pdev, barno, vma, sparse))
		return -EINVAL;

	pcibios_resource_to_bus(pdev->bus, &bar, pci_resource_n(pdev, barno));
	vma->vm_pgoff += bar.start >> (PAGE_SHIFT - (sparse ? 5 : 0));
	mmap_type = pci_resource_is_mem(pdev, barno) ? pci_mmap_mem : pci_mmap_io;

	return hose_mmap_page_range(pdev->sysdata, vma, mmap_type, sparse);
}

static int pci_mmap_resource_sparse(struct file *filp, struct kobject *kobj,
				    const struct bin_attribute *attr,
				    struct vm_area_struct *vma)
{
	return pci_mmap_resource(kobj, attr, vma, 1);
}

static int pci_mmap_resource_dense(struct file *filp, struct kobject *kobj,
				   const struct bin_attribute *attr,
				   struct vm_area_struct *vma)
{
	return pci_mmap_resource(kobj, attr, vma, 0);
}

#define __pci_dev_resource_attr(_bar, _name, _suffix, _mmap)		\
static const struct bin_attribute					\
pci_dev_resource##_bar##_suffix##_attr = {				\
	.attr = { .name = __stringify(_name), .mode = 0600 },		\
	.private = (void *)(unsigned long)(_bar),			\
	.mmap = (_mmap),						\
}

#define pci_dev_resource_attr(_bar)					\
	__pci_dev_resource_attr(_bar, resource##_bar,,			\
			    pci_mmap_resource_dense)

#define pci_dev_resource_sparse_attr(_bar)				\
	__pci_dev_resource_attr(_bar, resource##_bar##_sparse, _sparse,	\
			    pci_mmap_resource_sparse)

#define pci_dev_resource_dense_attr(_bar)				\
	__pci_dev_resource_attr(_bar, resource##_bar##_dense, _dense,	\
			    pci_mmap_resource_dense)

static int sparse_mem_mmap_fits(struct pci_dev *pdev, int num)
{
	struct pci_bus_region bar;
	struct pci_controller *hose = pdev->sysdata;
	long dense_offset;
	unsigned long sparse_size;

	pcibios_resource_to_bus(pdev->bus, &bar, pci_resource_n(pdev, num));

	/* All core logic chips have 4G sparse address space, except
	   CIA which has 16G (see xxx_SPARSE_MEM and xxx_DENSE_MEM
	   definitions in asm/core_xxx.h files). This corresponds
	   to 128M or 512M of the bus space. */
	dense_offset = (long)(hose->dense_mem_base - hose->sparse_mem_base);
	sparse_size = dense_offset >= 0x400000000UL ? 0x20000000 : 0x8000000;

	return bar.end < sparse_size;
}

/* Legacy I/O bus mapping stuff. */

static int __legacy_mmap_fits(struct vm_area_struct *vma,
			      unsigned long res_size)
{
	unsigned long nr, start, size;

	nr = vma_pages(vma);
	start = vma->vm_pgoff;
	size = ((res_size - 1) >> PAGE_SHIFT) + 1;

	return start < size && size - start >= nr;
}

static inline int has_sparse(struct pci_controller *hose,
			     enum pci_mmap_state mmap_type)
{
	unsigned long base;

	base = (mmap_type == pci_mmap_mem) ? hose->sparse_mem_base :
					     hose->sparse_io_base;

	return base != 0;
}

int pci_mmap_legacy_page_range(struct pci_bus *bus, struct vm_area_struct *vma,
			       enum pci_mmap_state mmap_type)
{
	struct pci_controller *hose = bus->sysdata;
	int sparse = has_sparse(hose, mmap_type);
	unsigned long res_size;

	res_size = (mmap_type == pci_mmap_mem) ? PCI_LEGACY_MEM_SIZE :
						 PCI_LEGACY_IO_SIZE;
	if (sparse)
		res_size <<= 5;

	if (!__legacy_mmap_fits(vma, res_size))
		return -EINVAL;

	return hose_mmap_page_range(hose, vma, mmap_type, sparse);
}

bool pci_legacy_has_sparse(struct pci_bus *bus, enum pci_mmap_state type)
{
	struct pci_controller *hose = bus->sysdata;

	return has_sparse(hose, type);
}

/* Legacy I/O bus read/write functions */
int pci_legacy_read(struct pci_bus *bus, loff_t port, u32 *val, size_t size)
{
	struct pci_controller *hose = bus->sysdata;

	port += hose->io_space->start;

	switch(size) {
	case 1:
		*((u8 *)val) = inb(port);
		return 1;
	case 2:
		if (port & 1)
			return -EINVAL;
		*((u16 *)val) = inw(port);
		return 2;
	case 4:
		if (port & 3)
			return -EINVAL;
		*((u32 *)val) = inl(port);
		return 4;
	}
	return -EINVAL;
}

int pci_legacy_write(struct pci_bus *bus, loff_t port, u32 val, size_t size)
{
	struct pci_controller *hose = bus->sysdata;

	port += hose->io_space->start;

	switch(size) {
	case 1:
		outb(port, val);
		return 1;
	case 2:
		if (port & 1)
			return -EINVAL;
		outw(port, val);
		return 2;
	case 4:
		if (port & 3)
			return -EINVAL;
		outl(port, val);
		return 4;
	}
	return -EINVAL;
}

pci_dev_resource_attr(0);
pci_dev_resource_attr(1);
pci_dev_resource_attr(2);
pci_dev_resource_attr(3);
pci_dev_resource_attr(4);
pci_dev_resource_attr(5);

pci_dev_resource_sparse_attr(0);
pci_dev_resource_sparse_attr(1);
pci_dev_resource_sparse_attr(2);
pci_dev_resource_sparse_attr(3);
pci_dev_resource_sparse_attr(4);
pci_dev_resource_sparse_attr(5);

pci_dev_resource_dense_attr(0);
pci_dev_resource_dense_attr(1);
pci_dev_resource_dense_attr(2);
pci_dev_resource_dense_attr(3);
pci_dev_resource_dense_attr(4);
pci_dev_resource_dense_attr(5);

static inline enum pci_mmap_state pci_bar_mmap_type(struct pci_dev *pdev,
						    int bar)
{
	return pci_resource_is_mem(pdev, bar) ? pci_mmap_mem : pci_mmap_io;
}

static inline umode_t __pci_resource_attr_is_visible(struct kobject *kobj,
						     const struct bin_attribute *a,
						     int bar)
{
	struct pci_dev *pdev = to_pci_dev(kobj_to_dev(kobj));

	if (!pci_resource_len(pdev, bar))
		return 0;

	return a->attr.mode;
}

static umode_t pci_dev_resource_is_visible(struct kobject *kobj,
					   const struct bin_attribute *a,
					   int bar)
{
	struct pci_dev *pdev = to_pci_dev(kobj_to_dev(kobj));
	struct pci_controller *hose = pdev->sysdata;

	if (has_sparse(hose, pci_bar_mmap_type(pdev, bar)))
		return 0;

	return __pci_resource_attr_is_visible(kobj, a, bar);
}

static umode_t pci_dev_resource_sparse_is_visible(struct kobject *kobj,
						  const struct bin_attribute *a,
						  int bar)
{
	struct pci_dev *pdev = to_pci_dev(kobj_to_dev(kobj));
	struct pci_controller *hose = pdev->sysdata;
	enum pci_mmap_state type = pci_bar_mmap_type(pdev, bar);

	if (!has_sparse(hose, type))
		return 0;

	if (type == pci_mmap_mem && !sparse_mem_mmap_fits(pdev, bar))
		return 0;

	return __pci_resource_attr_is_visible(kobj, a, bar);
}

static umode_t pci_dev_resource_dense_is_visible(struct kobject *kobj,
						 const struct bin_attribute *a,
						 int bar)
{
	struct pci_dev *pdev = to_pci_dev(kobj_to_dev(kobj));
	struct pci_controller *hose = pdev->sysdata;
	enum pci_mmap_state type = pci_bar_mmap_type(pdev, bar);
	unsigned long dense_base;

	if (!has_sparse(hose, type))
		return 0;

	if (type == pci_mmap_mem && !sparse_mem_mmap_fits(pdev, bar))
		return __pci_resource_attr_is_visible(kobj, a, bar);

	dense_base = (type == pci_mmap_mem) ? hose->dense_mem_base :
					      hose->dense_io_base;
	if (!dense_base)
		return 0;

	return __pci_resource_attr_is_visible(kobj, a, bar);
}

static inline size_t __pci_dev_resource_bin_size(struct kobject *kobj,
						 int bar, bool sparse)
{
	struct pci_dev *pdev = to_pci_dev(kobj_to_dev(kobj));
	size_t size = pci_resource_len(pdev, bar);

	return sparse ? size << 5 : size;
}

static size_t pci_dev_resource_bin_size(struct kobject *kobj,
					const struct bin_attribute *a,
					int bar)
{
	return __pci_dev_resource_bin_size(kobj, bar, false);
}

static size_t pci_dev_resource_sparse_bin_size(struct kobject *kobj,
					       const struct bin_attribute *a,
					       int bar)
{
	return __pci_dev_resource_bin_size(kobj, bar, true);
}

static const struct bin_attribute *const pci_dev_resource_attrs[] = {
	&pci_dev_resource0_attr,
	&pci_dev_resource1_attr,
	&pci_dev_resource2_attr,
	&pci_dev_resource3_attr,
	&pci_dev_resource4_attr,
	&pci_dev_resource5_attr,
	NULL,
};

static const struct bin_attribute *const pci_dev_resource_sparse_attrs[] = {
	&pci_dev_resource0_sparse_attr,
	&pci_dev_resource1_sparse_attr,
	&pci_dev_resource2_sparse_attr,
	&pci_dev_resource3_sparse_attr,
	&pci_dev_resource4_sparse_attr,
	&pci_dev_resource5_sparse_attr,
	NULL,
};

static const struct bin_attribute *const pci_dev_resource_dense_attrs[] = {
	&pci_dev_resource0_dense_attr,
	&pci_dev_resource1_dense_attr,
	&pci_dev_resource2_dense_attr,
	&pci_dev_resource3_dense_attr,
	&pci_dev_resource4_dense_attr,
	&pci_dev_resource5_dense_attr,
	NULL,
};

const struct attribute_group pci_dev_resource_attr_group = {
	.bin_attrs = pci_dev_resource_attrs,
	.is_bin_visible = pci_dev_resource_is_visible,
	.bin_size = pci_dev_resource_bin_size,
};

const struct attribute_group pci_dev_resource_sparse_attr_group = {
	.bin_attrs = pci_dev_resource_sparse_attrs,
	.is_bin_visible = pci_dev_resource_sparse_is_visible,
	.bin_size = pci_dev_resource_sparse_bin_size,
};

const struct attribute_group pci_dev_resource_dense_attr_group = {
	.bin_attrs = pci_dev_resource_dense_attrs,
	.is_bin_visible = pci_dev_resource_dense_is_visible,
	.bin_size = pci_dev_resource_bin_size,
};
