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
#include <linux/stat.h>
#include <linux/slab.h>
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
	unsigned long nr, start, size;
	int shift = sparse ? 5 : 0;

	nr = (vma->vm_end - vma->vm_start) >> PAGE_SHIFT;
	start = vma->vm_pgoff;
	size = ((pci_resource_len(pdev, num) - 1) >> (PAGE_SHIFT - shift)) + 1;

	if (start < size && size - start >= nr)
		return 1;
	WARN(1, "process \"%s\" tried to map%s 0x%08lx-0x%08lx on %s BAR %d "
		"(size 0x%08lx)\n",
		current->comm, sparse ? " sparse" : "", start, start + nr,
		pci_name(pdev), num, size);
	return 0;
}

/**
 * pci_mmap_resource - map a PCI resource into user memory space
 * @kobj: kobject for mapping
 * @attr: struct bin_attribute for the file being mapped
 * @vma: struct vm_area_struct passed into the mmap
 * @sparse: address space type
 *
 * Use the bus mapping routines to map a PCI resource into userspace.
 */
static int pci_mmap_resource(struct kobject *kobj,
			     struct bin_attribute *attr,
			     struct vm_area_struct *vma, int sparse)
{
	struct pci_dev *pdev = to_pci_dev(container_of(kobj,
						       struct device, kobj));
	struct resource *res = attr->private;
	enum pci_mmap_state mmap_type;
	struct pci_bus_region bar;
	int i;

	for (i = 0; i < PCI_ROM_RESOURCE; i++)
		if (res == &pdev->resource[i])
			break;
	if (i >= PCI_ROM_RESOURCE)
		return -ENODEV;

	if (!__pci_mmap_fits(pdev, i, vma, sparse))
		return -EINVAL;

	if (iomem_is_exclusive(res->start))
		return -EINVAL;

	pcibios_resource_to_bus(pdev->bus, &bar, res);
	vma->vm_pgoff += bar.start >> (PAGE_SHIFT - (sparse ? 5 : 0));
	mmap_type = res->flags & IORESOURCE_MEM ? pci_mmap_mem : pci_mmap_io;

	return hose_mmap_page_range(pdev->sysdata, vma, mmap_type, sparse);
}

static int pci_mmap_resource_sparse(struct file *filp, struct kobject *kobj,
				    struct bin_attribute *attr,
				    struct vm_area_struct *vma)
{
	return pci_mmap_resource(kobj, attr, vma, 1);
}

static int pci_mmap_resource_dense(struct file *filp, struct kobject *kobj,
				   struct bin_attribute *attr,
				   struct vm_area_struct *vma)
{
	return pci_mmap_resource(kobj, attr, vma, 0);
}

/**
 * pci_remove_resource_files - cleanup resource files
 * @dev: dev to cleanup
 *
 * If we created resource files for @dev, remove them from sysfs and
 * free their resources.
 */
void pci_remove_resource_files(struct pci_dev *pdev)
{
	int i;

	for (i = 0; i < PCI_ROM_RESOURCE; i++) {
		struct bin_attribute *res_attr;

		res_attr = pdev->res_attr[i];
		if (res_attr) {
			sysfs_remove_bin_file(&pdev->dev.kobj, res_attr);
			kfree(res_attr);
		}

		res_attr = pdev->res_attr_wc[i];
		if (res_attr) {
			sysfs_remove_bin_file(&pdev->dev.kobj, res_attr);
			kfree(res_attr);
		}
	}
}

static int sparse_mem_mmap_fits(struct pci_dev *pdev, int num)
{
	struct pci_bus_region bar;
	struct pci_controller *hose = pdev->sysdata;
	long dense_offset;
	unsigned long sparse_size;

	pcibios_resource_to_bus(pdev->bus, &bar, &pdev->resource[num]);

	/* All core logic chips have 4G sparse address space, except
	   CIA which has 16G (see xxx_SPARSE_MEM and xxx_DENSE_MEM
	   definitions in asm/core_xxx.h files). This corresponds
	   to 128M or 512M of the bus space. */
	dense_offset = (long)(hose->dense_mem_base - hose->sparse_mem_base);
	sparse_size = dense_offset >= 0x400000000UL ? 0x20000000 : 0x8000000;

	return bar.end < sparse_size;
}

static int pci_create_one_attr(struct pci_dev *pdev, int num, char *name,
			       char *suffix, struct bin_attribute *res_attr,
			       unsigned long sparse)
{
	size_t size = pci_resource_len(pdev, num);

	sprintf(name, "resource%d%s", num, suffix);
	res_attr->mmap = sparse ? pci_mmap_resource_sparse :
				  pci_mmap_resource_dense;
	res_attr->attr.name = name;
	res_attr->attr.mode = S_IRUSR | S_IWUSR;
	res_attr->size = sparse ? size << 5 : size;
	res_attr->private = &pdev->resource[num];
	return sysfs_create_bin_file(&pdev->dev.kobj, res_attr);
}

static int pci_create_attr(struct pci_dev *pdev, int num)
{
	/* allocate attribute structure, piggyback attribute name */
	int retval, nlen1, nlen2 = 0, res_count = 1;
	unsigned long sparse_base, dense_base;
	struct bin_attribute *attr;
	struct pci_controller *hose = pdev->sysdata;
	char *suffix, *attr_name;

	suffix = "";	/* Assume bwx machine, normal resourceN files. */
	nlen1 = 10;

	if (pdev->resource[num].flags & IORESOURCE_MEM) {
		sparse_base = hose->sparse_mem_base;
		dense_base = hose->dense_mem_base;
		if (sparse_base && !sparse_mem_mmap_fits(pdev, num)) {
			sparse_base = 0;
			suffix = "_dense";
			nlen1 = 16;	/* resourceN_dense */
		}
	} else {
		sparse_base = hose->sparse_io_base;
		dense_base = hose->dense_io_base;
	}

	if (sparse_base) {
		suffix = "_sparse";
		nlen1 = 17;
		if (dense_base) {
			nlen2 = 16;	/* resourceN_dense */
			res_count = 2;
		}
	}

	attr = kzalloc(sizeof(*attr) * res_count + nlen1 + nlen2, GFP_ATOMIC);
	if (!attr)
		return -ENOMEM;

	/* Create bwx, sparse or single dense file */
	attr_name = (char *)(attr + res_count);
	pdev->res_attr[num] = attr;
	retval = pci_create_one_attr(pdev, num, attr_name, suffix, attr,
				     sparse_base);
	if (retval || res_count == 1)
		return retval;

	/* Create dense file */
	attr_name += nlen1;
	attr++;
	pdev->res_attr_wc[num] = attr;
	return pci_create_one_attr(pdev, num, attr_name, "_dense", attr, 0);
}

/**
 * pci_create_resource_files - create resource files in sysfs for @dev
 * @dev: dev in question
 *
 * Walk the resources in @dev creating files for each resource available.
 */
int pci_create_resource_files(struct pci_dev *pdev)
{
	int i;
	int retval;

	/* Expose the PCI resources from this device as files */
	for (i = 0; i < PCI_ROM_RESOURCE; i++) {

		/* skip empty resources */
		if (!pci_resource_len(pdev, i))
			continue;

		retval = pci_create_attr(pdev, i);
		if (retval) {
			pci_remove_resource_files(pdev);
			return retval;
		}
	}
	return 0;
}

/* Legacy I/O bus mapping stuff. */

static int __legacy_mmap_fits(struct pci_controller *hose,
			      struct vm_area_struct *vma,
			      unsigned long res_size, int sparse)
{
	unsigned long nr, start, size;

	nr = (vma->vm_end - vma->vm_start) >> PAGE_SHIFT;
	start = vma->vm_pgoff;
	size = ((res_size - 1) >> PAGE_SHIFT) + 1;

	if (start < size && size - start >= nr)
		return 1;
	WARN(1, "process \"%s\" tried to map%s 0x%08lx-0x%08lx on hose %d "
		"(size 0x%08lx)\n",
		current->comm, sparse ? " sparse" : "", start, start + nr,
		hose->index, size);
	return 0;
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

	res_size = (mmap_type == pci_mmap_mem) ? bus->legacy_mem->size :
						 bus->legacy_io->size;
	if (!__legacy_mmap_fits(hose, vma, res_size, sparse))
		return -EINVAL;

	return hose_mmap_page_range(hose, vma, mmap_type, sparse);
}

/**
 * pci_adjust_legacy_attr - adjustment of legacy file attributes
 * @b: bus to create files under
 * @mmap_type: I/O port or memory
 *
 * Adjust file name and size for sparse mappings.
 */
void pci_adjust_legacy_attr(struct pci_bus *bus, enum pci_mmap_state mmap_type)
{
	struct pci_controller *hose = bus->sysdata;

	if (!has_sparse(hose, mmap_type))
		return;

	if (mmap_type == pci_mmap_mem) {
		bus->legacy_mem->attr.name = "legacy_mem_sparse";
		bus->legacy_mem->size <<= 5;
	} else {
		bus->legacy_io->attr.name = "legacy_io_sparse";
		bus->legacy_io->size <<= 5;
	}
	return;
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
