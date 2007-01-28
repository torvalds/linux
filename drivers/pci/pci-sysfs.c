/*
 * drivers/pci/pci-sysfs.c
 *
 * (C) Copyright 2002-2004 Greg Kroah-Hartman <greg@kroah.com>
 * (C) Copyright 2002-2004 IBM Corp.
 * (C) Copyright 2003 Matthew Wilcox
 * (C) Copyright 2003 Hewlett-Packard
 * (C) Copyright 2004 Jon Smirl <jonsmirl@yahoo.com>
 * (C) Copyright 2004 Silicon Graphics, Inc. Jesse Barnes <jbarnes@sgi.com>
 *
 * File attributes for PCI devices
 *
 * Modeled after usb's driverfs.c 
 *
 */


#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/stat.h>
#include <linux/topology.h>
#include <linux/mm.h>

#include "pci.h"

static int sysfs_initialized;	/* = 0 */

/* show configuration fields */
#define pci_config_attr(field, format_string)				\
static ssize_t								\
field##_show(struct device *dev, struct device_attribute *attr, char *buf)				\
{									\
	struct pci_dev *pdev;						\
									\
	pdev = to_pci_dev (dev);					\
	return sprintf (buf, format_string, pdev->field);		\
}

pci_config_attr(vendor, "0x%04x\n");
pci_config_attr(device, "0x%04x\n");
pci_config_attr(subsystem_vendor, "0x%04x\n");
pci_config_attr(subsystem_device, "0x%04x\n");
pci_config_attr(class, "0x%06x\n");
pci_config_attr(irq, "%u\n");

static ssize_t broken_parity_status_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	return sprintf (buf, "%u\n", pdev->broken_parity_status);
}

static ssize_t broken_parity_status_store(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t count)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	ssize_t consumed = -EINVAL;

	if ((count > 0) && (*buf == '0' || *buf == '1')) {
		pdev->broken_parity_status = *buf == '1' ? 1 : 0;
		consumed = count;
	}
	return consumed;
}

static ssize_t local_cpus_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{		
	cpumask_t mask;
	int len;

	mask = pcibus_to_cpumask(to_pci_dev(dev)->bus);
	len = cpumask_scnprintf(buf, PAGE_SIZE-2, mask);
	strcat(buf,"\n"); 
	return 1+len;
}

/* show resources */
static ssize_t
resource_show(struct device * dev, struct device_attribute *attr, char * buf)
{
	struct pci_dev * pci_dev = to_pci_dev(dev);
	char * str = buf;
	int i;
	int max = 7;
	resource_size_t start, end;

	if (pci_dev->subordinate)
		max = DEVICE_COUNT_RESOURCE;

	for (i = 0; i < max; i++) {
		struct resource *res =  &pci_dev->resource[i];
		pci_resource_to_user(pci_dev, i, res, &start, &end);
		str += sprintf(str,"0x%016llx 0x%016llx 0x%016llx\n",
			       (unsigned long long)start,
			       (unsigned long long)end,
			       (unsigned long long)res->flags);
	}
	return (str - buf);
}

static ssize_t modalias_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct pci_dev *pci_dev = to_pci_dev(dev);

	return sprintf(buf, "pci:v%08Xd%08Xsv%08Xsd%08Xbc%02Xsc%02Xi%02x\n",
		       pci_dev->vendor, pci_dev->device,
		       pci_dev->subsystem_vendor, pci_dev->subsystem_device,
		       (u8)(pci_dev->class >> 16), (u8)(pci_dev->class >> 8),
		       (u8)(pci_dev->class));
}

static ssize_t is_enabled_store(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t count)
{
	ssize_t result = -EINVAL;
	struct pci_dev *pdev = to_pci_dev(dev);

	/* this can crash the machine when done on the "wrong" device */
	if (!capable(CAP_SYS_ADMIN))
		return count;

	if (*buf == '0') {
		if (atomic_read(&pdev->enable_cnt) != 0)
			pci_disable_device(pdev);
		else
			result = -EIO;
	} else if (*buf == '1')
		result = pci_enable_device(pdev);

	return result < 0 ? result : count;
}

static ssize_t is_enabled_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct pci_dev *pdev;

	pdev = to_pci_dev (dev);
	return sprintf (buf, "%u\n", atomic_read(&pdev->enable_cnt));
}

#ifdef CONFIG_NUMA
static ssize_t
numa_node_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf (buf, "%d\n", dev->numa_node);
}
#endif

static ssize_t
msi_bus_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct pci_dev *pdev = to_pci_dev(dev);

	if (!pdev->subordinate)
		return 0;

	return sprintf (buf, "%u\n",
			!(pdev->subordinate->bus_flags & PCI_BUS_FLAGS_NO_MSI));
}

static ssize_t
msi_bus_store(struct device *dev, struct device_attribute *attr,
	      const char *buf, size_t count)
{
	struct pci_dev *pdev = to_pci_dev(dev);

	/* bad things may happen if the no_msi flag is changed
	 * while some drivers are loaded */
	if (!capable(CAP_SYS_ADMIN))
		return count;

	if (!pdev->subordinate)
		return count;

	if (*buf == '0') {
		pdev->subordinate->bus_flags |= PCI_BUS_FLAGS_NO_MSI;
		dev_warn(&pdev->dev, "forced subordinate bus to not support MSI,"
			 " bad things could happen.\n");
	}

	if (*buf == '1') {
		pdev->subordinate->bus_flags &= ~PCI_BUS_FLAGS_NO_MSI;
		dev_warn(&pdev->dev, "forced subordinate bus to support MSI,"
			 " bad things could happen.\n");
	}

	return count;
}

struct device_attribute pci_dev_attrs[] = {
	__ATTR_RO(resource),
	__ATTR_RO(vendor),
	__ATTR_RO(device),
	__ATTR_RO(subsystem_vendor),
	__ATTR_RO(subsystem_device),
	__ATTR_RO(class),
	__ATTR_RO(irq),
	__ATTR_RO(local_cpus),
	__ATTR_RO(modalias),
#ifdef CONFIG_NUMA
	__ATTR_RO(numa_node),
#endif
	__ATTR(enable, 0600, is_enabled_show, is_enabled_store),
	__ATTR(broken_parity_status,(S_IRUGO|S_IWUSR),
		broken_parity_status_show,broken_parity_status_store),
	__ATTR(msi_bus, 0644, msi_bus_show, msi_bus_store),
	__ATTR_NULL,
};

static ssize_t
pci_read_config(struct kobject *kobj, char *buf, loff_t off, size_t count)
{
	struct pci_dev *dev = to_pci_dev(container_of(kobj,struct device,kobj));
	unsigned int size = 64;
	loff_t init_off = off;
	u8 *data = (u8*) buf;

	/* Several chips lock up trying to read undefined config space */
	if (capable(CAP_SYS_ADMIN)) {
		size = dev->cfg_size;
	} else if (dev->hdr_type == PCI_HEADER_TYPE_CARDBUS) {
		size = 128;
	}

	if (off > size)
		return 0;
	if (off + count > size) {
		size -= off;
		count = size;
	} else {
		size = count;
	}

	if ((off & 1) && size) {
		u8 val;
		pci_user_read_config_byte(dev, off, &val);
		data[off - init_off] = val;
		off++;
		size--;
	}

	if ((off & 3) && size > 2) {
		u16 val;
		pci_user_read_config_word(dev, off, &val);
		data[off - init_off] = val & 0xff;
		data[off - init_off + 1] = (val >> 8) & 0xff;
		off += 2;
		size -= 2;
	}

	while (size > 3) {
		u32 val;
		pci_user_read_config_dword(dev, off, &val);
		data[off - init_off] = val & 0xff;
		data[off - init_off + 1] = (val >> 8) & 0xff;
		data[off - init_off + 2] = (val >> 16) & 0xff;
		data[off - init_off + 3] = (val >> 24) & 0xff;
		off += 4;
		size -= 4;
	}

	if (size >= 2) {
		u16 val;
		pci_user_read_config_word(dev, off, &val);
		data[off - init_off] = val & 0xff;
		data[off - init_off + 1] = (val >> 8) & 0xff;
		off += 2;
		size -= 2;
	}

	if (size > 0) {
		u8 val;
		pci_user_read_config_byte(dev, off, &val);
		data[off - init_off] = val;
		off++;
		--size;
	}

	return count;
}

static ssize_t
pci_write_config(struct kobject *kobj, char *buf, loff_t off, size_t count)
{
	struct pci_dev *dev = to_pci_dev(container_of(kobj,struct device,kobj));
	unsigned int size = count;
	loff_t init_off = off;
	u8 *data = (u8*) buf;

	if (off > dev->cfg_size)
		return 0;
	if (off + count > dev->cfg_size) {
		size = dev->cfg_size - off;
		count = size;
	}
	
	if ((off & 1) && size) {
		pci_user_write_config_byte(dev, off, data[off - init_off]);
		off++;
		size--;
	}
	
	if ((off & 3) && size > 2) {
		u16 val = data[off - init_off];
		val |= (u16) data[off - init_off + 1] << 8;
                pci_user_write_config_word(dev, off, val);
                off += 2;
                size -= 2;
        }

	while (size > 3) {
		u32 val = data[off - init_off];
		val |= (u32) data[off - init_off + 1] << 8;
		val |= (u32) data[off - init_off + 2] << 16;
		val |= (u32) data[off - init_off + 3] << 24;
		pci_user_write_config_dword(dev, off, val);
		off += 4;
		size -= 4;
	}
	
	if (size >= 2) {
		u16 val = data[off - init_off];
		val |= (u16) data[off - init_off + 1] << 8;
		pci_user_write_config_word(dev, off, val);
		off += 2;
		size -= 2;
	}

	if (size) {
		pci_user_write_config_byte(dev, off, data[off - init_off]);
		off++;
		--size;
	}

	return count;
}

#ifdef HAVE_PCI_LEGACY
/**
 * pci_read_legacy_io - read byte(s) from legacy I/O port space
 * @kobj: kobject corresponding to file to read from
 * @buf: buffer to store results
 * @off: offset into legacy I/O port space
 * @count: number of bytes to read
 *
 * Reads 1, 2, or 4 bytes from legacy I/O port space using an arch specific
 * callback routine (pci_legacy_read).
 */
ssize_t
pci_read_legacy_io(struct kobject *kobj, char *buf, loff_t off, size_t count)
{
        struct pci_bus *bus = to_pci_bus(container_of(kobj,
                                                      struct class_device,
						      kobj));

        /* Only support 1, 2 or 4 byte accesses */
        if (count != 1 && count != 2 && count != 4)
                return -EINVAL;

        return pci_legacy_read(bus, off, (u32 *)buf, count);
}

/**
 * pci_write_legacy_io - write byte(s) to legacy I/O port space
 * @kobj: kobject corresponding to file to read from
 * @buf: buffer containing value to be written
 * @off: offset into legacy I/O port space
 * @count: number of bytes to write
 *
 * Writes 1, 2, or 4 bytes from legacy I/O port space using an arch specific
 * callback routine (pci_legacy_write).
 */
ssize_t
pci_write_legacy_io(struct kobject *kobj, char *buf, loff_t off, size_t count)
{
        struct pci_bus *bus = to_pci_bus(container_of(kobj,
						      struct class_device,
						      kobj));
        /* Only support 1, 2 or 4 byte accesses */
        if (count != 1 && count != 2 && count != 4)
                return -EINVAL;

        return pci_legacy_write(bus, off, *(u32 *)buf, count);
}

/**
 * pci_mmap_legacy_mem - map legacy PCI memory into user memory space
 * @kobj: kobject corresponding to device to be mapped
 * @attr: struct bin_attribute for this file
 * @vma: struct vm_area_struct passed to mmap
 *
 * Uses an arch specific callback, pci_mmap_legacy_page_range, to mmap
 * legacy memory space (first meg of bus space) into application virtual
 * memory space.
 */
int
pci_mmap_legacy_mem(struct kobject *kobj, struct bin_attribute *attr,
                    struct vm_area_struct *vma)
{
        struct pci_bus *bus = to_pci_bus(container_of(kobj,
                                                      struct class_device,
						      kobj));

        return pci_mmap_legacy_page_range(bus, vma);
}
#endif /* HAVE_PCI_LEGACY */

#ifdef HAVE_PCI_MMAP
/**
 * pci_mmap_resource - map a PCI resource into user memory space
 * @kobj: kobject for mapping
 * @attr: struct bin_attribute for the file being mapped
 * @vma: struct vm_area_struct passed into the mmap
 *
 * Use the regular PCI mapping routines to map a PCI resource into userspace.
 * FIXME: write combining?  maybe automatic for prefetchable regions?
 */
static int
pci_mmap_resource(struct kobject *kobj, struct bin_attribute *attr,
		  struct vm_area_struct *vma)
{
	struct pci_dev *pdev = to_pci_dev(container_of(kobj,
						       struct device, kobj));
	struct resource *res = (struct resource *)attr->private;
	enum pci_mmap_state mmap_type;
	resource_size_t start, end;
	int i;

	for (i = 0; i < PCI_ROM_RESOURCE; i++)
		if (res == &pdev->resource[i])
			break;
	if (i >= PCI_ROM_RESOURCE)
		return -ENODEV;

	/* pci_mmap_page_range() expects the same kind of entry as coming
	 * from /proc/bus/pci/ which is a "user visible" value. If this is
	 * different from the resource itself, arch will do necessary fixup.
	 */
	pci_resource_to_user(pdev, i, res, &start, &end);
	vma->vm_pgoff += start >> PAGE_SHIFT;
	mmap_type = res->flags & IORESOURCE_MEM ? pci_mmap_mem : pci_mmap_io;

	return pci_mmap_page_range(pdev, vma, mmap_type, 0);
}

/**
 * pci_remove_resource_files - cleanup resource files
 * @dev: dev to cleanup
 *
 * If we created resource files for @dev, remove them from sysfs and
 * free their resources.
 */
static void
pci_remove_resource_files(struct pci_dev *pdev)
{
	int i;

	for (i = 0; i < PCI_ROM_RESOURCE; i++) {
		struct bin_attribute *res_attr;

		res_attr = pdev->res_attr[i];
		if (res_attr) {
			sysfs_remove_bin_file(&pdev->dev.kobj, res_attr);
			kfree(res_attr);
		}
	}
}

/**
 * pci_create_resource_files - create resource files in sysfs for @dev
 * @dev: dev in question
 *
 * Walk the resources in @dev creating files for each resource available.
 */
static int pci_create_resource_files(struct pci_dev *pdev)
{
	int i;
	int retval;

	/* Expose the PCI resources from this device as files */
	for (i = 0; i < PCI_ROM_RESOURCE; i++) {
		struct bin_attribute *res_attr;

		/* skip empty resources */
		if (!pci_resource_len(pdev, i))
			continue;

		/* allocate attribute structure, piggyback attribute name */
		res_attr = kzalloc(sizeof(*res_attr) + 10, GFP_ATOMIC);
		if (res_attr) {
			char *res_attr_name = (char *)(res_attr + 1);

			pdev->res_attr[i] = res_attr;
			sprintf(res_attr_name, "resource%d", i);
			res_attr->attr.name = res_attr_name;
			res_attr->attr.mode = S_IRUSR | S_IWUSR;
			res_attr->attr.owner = THIS_MODULE;
			res_attr->size = pci_resource_len(pdev, i);
			res_attr->mmap = pci_mmap_resource;
			res_attr->private = &pdev->resource[i];
			retval = sysfs_create_bin_file(&pdev->dev.kobj, res_attr);
			if (retval) {
				pci_remove_resource_files(pdev);
				return retval;
			}
		} else {
			return -ENOMEM;
		}
	}
	return 0;
}
#else /* !HAVE_PCI_MMAP */
static inline int pci_create_resource_files(struct pci_dev *dev) { return 0; }
static inline void pci_remove_resource_files(struct pci_dev *dev) { return; }
#endif /* HAVE_PCI_MMAP */

/**
 * pci_write_rom - used to enable access to the PCI ROM display
 * @kobj: kernel object handle
 * @buf: user input
 * @off: file offset
 * @count: number of byte in input
 *
 * writing anything except 0 enables it
 */
static ssize_t
pci_write_rom(struct kobject *kobj, char *buf, loff_t off, size_t count)
{
	struct pci_dev *pdev = to_pci_dev(container_of(kobj, struct device, kobj));

	if ((off ==  0) && (*buf == '0') && (count == 2))
		pdev->rom_attr_enabled = 0;
	else
		pdev->rom_attr_enabled = 1;

	return count;
}

/**
 * pci_read_rom - read a PCI ROM
 * @kobj: kernel object handle
 * @buf: where to put the data we read from the ROM
 * @off: file offset
 * @count: number of bytes to read
 *
 * Put @count bytes starting at @off into @buf from the ROM in the PCI
 * device corresponding to @kobj.
 */
static ssize_t
pci_read_rom(struct kobject *kobj, char *buf, loff_t off, size_t count)
{
	struct pci_dev *pdev = to_pci_dev(container_of(kobj, struct device, kobj));
	void __iomem *rom;
	size_t size;

	if (!pdev->rom_attr_enabled)
		return -EINVAL;
	
	rom = pci_map_rom(pdev, &size);	/* size starts out as PCI window size */
	if (!rom)
		return 0;
		
	if (off >= size)
		count = 0;
	else {
		if (off + count > size)
			count = size - off;
		
		memcpy_fromio(buf, rom + off, count);
	}
	pci_unmap_rom(pdev, rom);
		
	return count;
}

static struct bin_attribute pci_config_attr = {
	.attr =	{
		.name = "config",
		.mode = S_IRUGO | S_IWUSR,
		.owner = THIS_MODULE,
	},
	.size = 256,
	.read = pci_read_config,
	.write = pci_write_config,
};

static struct bin_attribute pcie_config_attr = {
	.attr =	{
		.name = "config",
		.mode = S_IRUGO | S_IWUSR,
		.owner = THIS_MODULE,
	},
	.size = 4096,
	.read = pci_read_config,
	.write = pci_write_config,
};

int __must_check pci_create_sysfs_dev_files (struct pci_dev *pdev)
{
	struct bin_attribute *rom_attr = NULL;
	int retval;

	if (!sysfs_initialized)
		return -EACCES;

	if (pdev->cfg_size < 4096)
		retval = sysfs_create_bin_file(&pdev->dev.kobj, &pci_config_attr);
	else
		retval = sysfs_create_bin_file(&pdev->dev.kobj, &pcie_config_attr);
	if (retval)
		goto err;

	retval = pci_create_resource_files(pdev);
	if (retval)
		goto err_bin_file;

	/* If the device has a ROM, try to expose it in sysfs. */
	if (pci_resource_len(pdev, PCI_ROM_RESOURCE)) {
		rom_attr = kzalloc(sizeof(*rom_attr), GFP_ATOMIC);
		if (rom_attr) {
			pdev->rom_attr = rom_attr;
			rom_attr->size = pci_resource_len(pdev, PCI_ROM_RESOURCE);
			rom_attr->attr.name = "rom";
			rom_attr->attr.mode = S_IRUSR;
			rom_attr->attr.owner = THIS_MODULE;
			rom_attr->read = pci_read_rom;
			rom_attr->write = pci_write_rom;
			retval = sysfs_create_bin_file(&pdev->dev.kobj, rom_attr);
			if (retval)
				goto err_rom;
		} else {
			retval = -ENOMEM;
			goto err_bin_file;
		}
	}
	/* add platform-specific attributes */
	pcibios_add_platform_entries(pdev);

	return 0;

err_rom:
	kfree(rom_attr);
err_bin_file:
	if (pdev->cfg_size < 4096)
		sysfs_remove_bin_file(&pdev->dev.kobj, &pci_config_attr);
	else
		sysfs_remove_bin_file(&pdev->dev.kobj, &pcie_config_attr);
err:
	return retval;
}

/**
 * pci_remove_sysfs_dev_files - cleanup PCI specific sysfs files
 * @pdev: device whose entries we should free
 *
 * Cleanup when @pdev is removed from sysfs.
 */
void pci_remove_sysfs_dev_files(struct pci_dev *pdev)
{
	if (!sysfs_initialized)
		return;

	if (pdev->cfg_size < 4096)
		sysfs_remove_bin_file(&pdev->dev.kobj, &pci_config_attr);
	else
		sysfs_remove_bin_file(&pdev->dev.kobj, &pcie_config_attr);

	pci_remove_resource_files(pdev);

	if (pci_resource_len(pdev, PCI_ROM_RESOURCE)) {
		if (pdev->rom_attr) {
			sysfs_remove_bin_file(&pdev->dev.kobj, pdev->rom_attr);
			kfree(pdev->rom_attr);
		}
	}
}

static int __init pci_sysfs_init(void)
{
	struct pci_dev *pdev = NULL;
	int retval;

	sysfs_initialized = 1;
	for_each_pci_dev(pdev) {
		retval = pci_create_sysfs_dev_files(pdev);
		if (retval)
			return retval;
	}

	return 0;
}

__initcall(pci_sysfs_init);
