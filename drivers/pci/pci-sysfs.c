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
#include <linux/sched.h>
#include <linux/pci.h>
#include <linux/stat.h>
#include <linux/export.h>
#include <linux/topology.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/capability.h>
#include <linux/security.h>
#include <linux/pci-aspm.h>
#include <linux/slab.h>
#include <linux/vgaarb.h>
#include <linux/pm_runtime.h>
#include <linux/of.h>
#include "pci.h"

static int sysfs_initialized;	/* = 0 */

/* show configuration fields */
#define pci_config_attr(field, format_string)				\
static ssize_t								\
field##_show(struct device *dev, struct device_attribute *attr, char *buf)				\
{									\
	struct pci_dev *pdev;						\
									\
	pdev = to_pci_dev(dev);						\
	return sprintf(buf, format_string, pdev->field);		\
}									\
static DEVICE_ATTR_RO(field)

pci_config_attr(vendor, "0x%04x\n");
pci_config_attr(device, "0x%04x\n");
pci_config_attr(subsystem_vendor, "0x%04x\n");
pci_config_attr(subsystem_device, "0x%04x\n");
pci_config_attr(revision, "0x%02x\n");
pci_config_attr(class, "0x%06x\n");
pci_config_attr(irq, "%u\n");

static ssize_t broken_parity_status_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	return sprintf(buf, "%u\n", pdev->broken_parity_status);
}

static ssize_t broken_parity_status_store(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t count)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	unsigned long val;

	if (kstrtoul(buf, 0, &val) < 0)
		return -EINVAL;

	pdev->broken_parity_status = !!val;

	return count;
}
static DEVICE_ATTR_RW(broken_parity_status);

static ssize_t pci_dev_show_local_cpu(struct device *dev, bool list,
				      struct device_attribute *attr, char *buf)
{
	const struct cpumask *mask;

#ifdef CONFIG_NUMA
	mask = (dev_to_node(dev) == -1) ? cpu_online_mask :
					  cpumask_of_node(dev_to_node(dev));
#else
	mask = cpumask_of_pcibus(to_pci_dev(dev)->bus);
#endif
	return cpumap_print_to_pagebuf(list, buf, mask);
}

static ssize_t local_cpus_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	return pci_dev_show_local_cpu(dev, false, attr, buf);
}
static DEVICE_ATTR_RO(local_cpus);

static ssize_t local_cpulist_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	return pci_dev_show_local_cpu(dev, true, attr, buf);
}
static DEVICE_ATTR_RO(local_cpulist);

/*
 * PCI Bus Class Devices
 */
static ssize_t cpuaffinity_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	const struct cpumask *cpumask = cpumask_of_pcibus(to_pci_bus(dev));

	return cpumap_print_to_pagebuf(false, buf, cpumask);
}
static DEVICE_ATTR_RO(cpuaffinity);

static ssize_t cpulistaffinity_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	const struct cpumask *cpumask = cpumask_of_pcibus(to_pci_bus(dev));

	return cpumap_print_to_pagebuf(true, buf, cpumask);
}
static DEVICE_ATTR_RO(cpulistaffinity);

/* show resources */
static ssize_t resource_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct pci_dev *pci_dev = to_pci_dev(dev);
	char *str = buf;
	int i;
	int max;
	resource_size_t start, end;

	if (pci_dev->subordinate)
		max = DEVICE_COUNT_RESOURCE;
	else
		max = PCI_BRIDGE_RESOURCES;

	for (i = 0; i < max; i++) {
		struct resource *res =  &pci_dev->resource[i];
		pci_resource_to_user(pci_dev, i, res, &start, &end);
		str += sprintf(str, "0x%016llx 0x%016llx 0x%016llx\n",
			       (unsigned long long)start,
			       (unsigned long long)end,
			       (unsigned long long)res->flags);
	}
	return (str - buf);
}
static DEVICE_ATTR_RO(resource);

static ssize_t max_link_speed_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct pci_dev *pci_dev = to_pci_dev(dev);
	u32 linkcap;
	int err;
	const char *speed;

	err = pcie_capability_read_dword(pci_dev, PCI_EXP_LNKCAP, &linkcap);
	if (err)
		return -EINVAL;

	switch (linkcap & PCI_EXP_LNKCAP_SLS) {
	case PCI_EXP_LNKCAP_SLS_8_0GB:
		speed = "8 GT/s";
		break;
	case PCI_EXP_LNKCAP_SLS_5_0GB:
		speed = "5 GT/s";
		break;
	case PCI_EXP_LNKCAP_SLS_2_5GB:
		speed = "2.5 GT/s";
		break;
	default:
		speed = "Unknown speed";
	}

	return sprintf(buf, "%s\n", speed);
}
static DEVICE_ATTR_RO(max_link_speed);

static ssize_t max_link_width_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct pci_dev *pci_dev = to_pci_dev(dev);
	u32 linkcap;
	int err;

	err = pcie_capability_read_dword(pci_dev, PCI_EXP_LNKCAP, &linkcap);
	if (err)
		return -EINVAL;

	return sprintf(buf, "%u\n", (linkcap & PCI_EXP_LNKCAP_MLW) >> 4);
}
static DEVICE_ATTR_RO(max_link_width);

static ssize_t current_link_speed_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct pci_dev *pci_dev = to_pci_dev(dev);
	u16 linkstat;
	int err;
	const char *speed;

	err = pcie_capability_read_word(pci_dev, PCI_EXP_LNKSTA, &linkstat);
	if (err)
		return -EINVAL;

	switch (linkstat & PCI_EXP_LNKSTA_CLS) {
	case PCI_EXP_LNKSTA_CLS_8_0GB:
		speed = "8 GT/s";
		break;
	case PCI_EXP_LNKSTA_CLS_5_0GB:
		speed = "5 GT/s";
		break;
	case PCI_EXP_LNKSTA_CLS_2_5GB:
		speed = "2.5 GT/s";
		break;
	default:
		speed = "Unknown speed";
	}

	return sprintf(buf, "%s\n", speed);
}
static DEVICE_ATTR_RO(current_link_speed);

static ssize_t current_link_width_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct pci_dev *pci_dev = to_pci_dev(dev);
	u16 linkstat;
	int err;

	err = pcie_capability_read_word(pci_dev, PCI_EXP_LNKSTA, &linkstat);
	if (err)
		return -EINVAL;

	return sprintf(buf, "%u\n",
		(linkstat & PCI_EXP_LNKSTA_NLW) >> PCI_EXP_LNKSTA_NLW_SHIFT);
}
static DEVICE_ATTR_RO(current_link_width);

static ssize_t secondary_bus_number_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct pci_dev *pci_dev = to_pci_dev(dev);
	u8 sec_bus;
	int err;

	err = pci_read_config_byte(pci_dev, PCI_SECONDARY_BUS, &sec_bus);
	if (err)
		return -EINVAL;

	return sprintf(buf, "%u\n", sec_bus);
}
static DEVICE_ATTR_RO(secondary_bus_number);

static ssize_t subordinate_bus_number_show(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct pci_dev *pci_dev = to_pci_dev(dev);
	u8 sub_bus;
	int err;

	err = pci_read_config_byte(pci_dev, PCI_SUBORDINATE_BUS, &sub_bus);
	if (err)
		return -EINVAL;

	return sprintf(buf, "%u\n", sub_bus);
}
static DEVICE_ATTR_RO(subordinate_bus_number);

static ssize_t modalias_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct pci_dev *pci_dev = to_pci_dev(dev);

	return sprintf(buf, "pci:v%08Xd%08Xsv%08Xsd%08Xbc%02Xsc%02Xi%02X\n",
		       pci_dev->vendor, pci_dev->device,
		       pci_dev->subsystem_vendor, pci_dev->subsystem_device,
		       (u8)(pci_dev->class >> 16), (u8)(pci_dev->class >> 8),
		       (u8)(pci_dev->class));
}
static DEVICE_ATTR_RO(modalias);

static ssize_t enable_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	unsigned long val;
	ssize_t result = kstrtoul(buf, 0, &val);

	if (result < 0)
		return result;

	/* this can crash the machine when done on the "wrong" device */
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (!val) {
		if (pci_is_enabled(pdev))
			pci_disable_device(pdev);
		else
			result = -EIO;
	} else
		result = pci_enable_device(pdev);

	return result < 0 ? result : count;
}

static ssize_t enable_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct pci_dev *pdev;

	pdev = to_pci_dev(dev);
	return sprintf(buf, "%u\n", atomic_read(&pdev->enable_cnt));
}
static DEVICE_ATTR_RW(enable);

#ifdef CONFIG_NUMA
static ssize_t numa_node_store(struct device *dev,
			       struct device_attribute *attr, const char *buf,
			       size_t count)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	int node, ret;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	ret = kstrtoint(buf, 0, &node);
	if (ret)
		return ret;

	if ((node < 0 && node != NUMA_NO_NODE) || node >= MAX_NUMNODES)
		return -EINVAL;

	if (node != NUMA_NO_NODE && !node_online(node))
		return -EINVAL;

	add_taint(TAINT_FIRMWARE_WORKAROUND, LOCKDEP_STILL_OK);
	dev_alert(&pdev->dev, FW_BUG "Overriding NUMA node to %d.  Contact your vendor for updates.",
		  node);

	dev->numa_node = node;
	return count;
}

static ssize_t numa_node_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	return sprintf(buf, "%d\n", dev->numa_node);
}
static DEVICE_ATTR_RW(numa_node);
#endif

static ssize_t dma_mask_bits_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct pci_dev *pdev = to_pci_dev(dev);

	return sprintf(buf, "%d\n", fls64(pdev->dma_mask));
}
static DEVICE_ATTR_RO(dma_mask_bits);

static ssize_t consistent_dma_mask_bits_show(struct device *dev,
					     struct device_attribute *attr,
					     char *buf)
{
	return sprintf(buf, "%d\n", fls64(dev->coherent_dma_mask));
}
static DEVICE_ATTR_RO(consistent_dma_mask_bits);

static ssize_t msi_bus_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct pci_bus *subordinate = pdev->subordinate;

	return sprintf(buf, "%u\n", subordinate ?
		       !(subordinate->bus_flags & PCI_BUS_FLAGS_NO_MSI)
			   : !pdev->no_msi);
}

static ssize_t msi_bus_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct pci_bus *subordinate = pdev->subordinate;
	unsigned long val;

	if (kstrtoul(buf, 0, &val) < 0)
		return -EINVAL;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	/*
	 * "no_msi" and "bus_flags" only affect what happens when a driver
	 * requests MSI or MSI-X.  They don't affect any drivers that have
	 * already requested MSI or MSI-X.
	 */
	if (!subordinate) {
		pdev->no_msi = !val;
		dev_info(&pdev->dev, "MSI/MSI-X %s for future drivers\n",
			 val ? "allowed" : "disallowed");
		return count;
	}

	if (val)
		subordinate->bus_flags &= ~PCI_BUS_FLAGS_NO_MSI;
	else
		subordinate->bus_flags |= PCI_BUS_FLAGS_NO_MSI;

	dev_info(&subordinate->dev, "MSI/MSI-X %s for future drivers of devices on this bus\n",
		 val ? "allowed" : "disallowed");
	return count;
}
static DEVICE_ATTR_RW(msi_bus);

static ssize_t bus_rescan_store(struct bus_type *bus, const char *buf,
				size_t count)
{
	unsigned long val;
	struct pci_bus *b = NULL;

	if (kstrtoul(buf, 0, &val) < 0)
		return -EINVAL;

	if (val) {
		pci_lock_rescan_remove();
		while ((b = pci_find_next_bus(b)) != NULL)
			pci_rescan_bus(b);
		pci_unlock_rescan_remove();
	}
	return count;
}
static BUS_ATTR(rescan, (S_IWUSR|S_IWGRP), NULL, bus_rescan_store);

static struct attribute *pci_bus_attrs[] = {
	&bus_attr_rescan.attr,
	NULL,
};

static const struct attribute_group pci_bus_group = {
	.attrs = pci_bus_attrs,
};

const struct attribute_group *pci_bus_groups[] = {
	&pci_bus_group,
	NULL,
};

static ssize_t dev_rescan_store(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t count)
{
	unsigned long val;
	struct pci_dev *pdev = to_pci_dev(dev);

	if (kstrtoul(buf, 0, &val) < 0)
		return -EINVAL;

	if (val) {
		pci_lock_rescan_remove();
		pci_rescan_bus(pdev->bus);
		pci_unlock_rescan_remove();
	}
	return count;
}
static struct device_attribute dev_rescan_attr = __ATTR(rescan,
							(S_IWUSR|S_IWGRP),
							NULL, dev_rescan_store);

static ssize_t remove_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	unsigned long val;

	if (kstrtoul(buf, 0, &val) < 0)
		return -EINVAL;

	if (val && device_remove_file_self(dev, attr))
		pci_stop_and_remove_bus_device_locked(to_pci_dev(dev));
	return count;
}
static struct device_attribute dev_remove_attr = __ATTR(remove,
							(S_IWUSR|S_IWGRP),
							NULL, remove_store);

static ssize_t dev_bus_rescan_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	unsigned long val;
	struct pci_bus *bus = to_pci_bus(dev);

	if (kstrtoul(buf, 0, &val) < 0)
		return -EINVAL;

	if (val) {
		pci_lock_rescan_remove();
		if (!pci_is_root_bus(bus) && list_empty(&bus->devices))
			pci_rescan_bus_bridge_resize(bus->self);
		else
			pci_rescan_bus(bus);
		pci_unlock_rescan_remove();
	}
	return count;
}
static DEVICE_ATTR(rescan, (S_IWUSR|S_IWGRP), NULL, dev_bus_rescan_store);

#if defined(CONFIG_PM) && defined(CONFIG_ACPI)
static ssize_t d3cold_allowed_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	unsigned long val;

	if (kstrtoul(buf, 0, &val) < 0)
		return -EINVAL;

	pdev->d3cold_allowed = !!val;
	if (pdev->d3cold_allowed)
		pci_d3cold_enable(pdev);
	else
		pci_d3cold_disable(pdev);

	pm_runtime_resume(dev);

	return count;
}

static ssize_t d3cold_allowed_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	return sprintf(buf, "%u\n", pdev->d3cold_allowed);
}
static DEVICE_ATTR_RW(d3cold_allowed);
#endif

#ifdef CONFIG_OF
static ssize_t devspec_show(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct device_node *np = pci_device_to_OF_node(pdev);

	if (np == NULL)
		return 0;
	return sprintf(buf, "%pOF", np);
}
static DEVICE_ATTR_RO(devspec);
#endif

#ifdef CONFIG_PCI_IOV
static ssize_t sriov_totalvfs_show(struct device *dev,
				   struct device_attribute *attr,
				   char *buf)
{
	struct pci_dev *pdev = to_pci_dev(dev);

	return sprintf(buf, "%u\n", pci_sriov_get_totalvfs(pdev));
}


static ssize_t sriov_numvfs_show(struct device *dev,
				 struct device_attribute *attr,
				 char *buf)
{
	struct pci_dev *pdev = to_pci_dev(dev);

	return sprintf(buf, "%u\n", pdev->sriov->num_VFs);
}

/*
 * num_vfs > 0; number of VFs to enable
 * num_vfs = 0; disable all VFs
 *
 * Note: SRIOV spec doesn't allow partial VF
 *       disable, so it's all or none.
 */
static ssize_t sriov_numvfs_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	int ret;
	u16 num_vfs;

	ret = kstrtou16(buf, 0, &num_vfs);
	if (ret < 0)
		return ret;

	if (num_vfs > pci_sriov_get_totalvfs(pdev))
		return -ERANGE;

	device_lock(&pdev->dev);

	if (num_vfs == pdev->sriov->num_VFs)
		goto exit;

	/* is PF driver loaded w/callback */
	if (!pdev->driver || !pdev->driver->sriov_configure) {
		dev_info(&pdev->dev, "Driver doesn't support SRIOV configuration via sysfs\n");
		ret = -ENOENT;
		goto exit;
	}

	if (num_vfs == 0) {
		/* disable VFs */
		ret = pdev->driver->sriov_configure(pdev, 0);
		goto exit;
	}

	/* enable VFs */
	if (pdev->sriov->num_VFs) {
		dev_warn(&pdev->dev, "%d VFs already enabled. Disable before enabling %d VFs\n",
			 pdev->sriov->num_VFs, num_vfs);
		ret = -EBUSY;
		goto exit;
	}

	ret = pdev->driver->sriov_configure(pdev, num_vfs);
	if (ret < 0)
		goto exit;

	if (ret != num_vfs)
		dev_warn(&pdev->dev, "%d VFs requested; only %d enabled\n",
			 num_vfs, ret);

exit:
	device_unlock(&pdev->dev);

	if (ret < 0)
		return ret;

	return count;
}

static ssize_t sriov_drivers_autoprobe_show(struct device *dev,
					    struct device_attribute *attr,
					    char *buf)
{
	struct pci_dev *pdev = to_pci_dev(dev);

	return sprintf(buf, "%u\n", pdev->sriov->drivers_autoprobe);
}

static ssize_t sriov_drivers_autoprobe_store(struct device *dev,
					     struct device_attribute *attr,
					     const char *buf, size_t count)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	bool drivers_autoprobe;

	if (kstrtobool(buf, &drivers_autoprobe) < 0)
		return -EINVAL;

	pdev->sriov->drivers_autoprobe = drivers_autoprobe;

	return count;
}

static struct device_attribute sriov_totalvfs_attr = __ATTR_RO(sriov_totalvfs);
static struct device_attribute sriov_numvfs_attr =
		__ATTR(sriov_numvfs, (S_IRUGO|S_IWUSR|S_IWGRP),
		       sriov_numvfs_show, sriov_numvfs_store);
static struct device_attribute sriov_drivers_autoprobe_attr =
		__ATTR(sriov_drivers_autoprobe, (S_IRUGO|S_IWUSR|S_IWGRP),
		       sriov_drivers_autoprobe_show, sriov_drivers_autoprobe_store);
#endif /* CONFIG_PCI_IOV */

static ssize_t driver_override_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	char *driver_override, *old = pdev->driver_override, *cp;

	/* We need to keep extra room for a newline */
	if (count >= (PAGE_SIZE - 1))
		return -EINVAL;

	driver_override = kstrndup(buf, count, GFP_KERNEL);
	if (!driver_override)
		return -ENOMEM;

	cp = strchr(driver_override, '\n');
	if (cp)
		*cp = '\0';

	if (strlen(driver_override)) {
		pdev->driver_override = driver_override;
	} else {
		kfree(driver_override);
		pdev->driver_override = NULL;
	}

	kfree(old);

	return count;
}

static ssize_t driver_override_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct pci_dev *pdev = to_pci_dev(dev);

	return snprintf(buf, PAGE_SIZE, "%s\n", pdev->driver_override);
}
static DEVICE_ATTR_RW(driver_override);

static struct attribute *pci_dev_attrs[] = {
	&dev_attr_resource.attr,
	&dev_attr_vendor.attr,
	&dev_attr_device.attr,
	&dev_attr_subsystem_vendor.attr,
	&dev_attr_subsystem_device.attr,
	&dev_attr_revision.attr,
	&dev_attr_class.attr,
	&dev_attr_irq.attr,
	&dev_attr_local_cpus.attr,
	&dev_attr_local_cpulist.attr,
	&dev_attr_modalias.attr,
#ifdef CONFIG_NUMA
	&dev_attr_numa_node.attr,
#endif
	&dev_attr_dma_mask_bits.attr,
	&dev_attr_consistent_dma_mask_bits.attr,
	&dev_attr_enable.attr,
	&dev_attr_broken_parity_status.attr,
	&dev_attr_msi_bus.attr,
#if defined(CONFIG_PM) && defined(CONFIG_ACPI)
	&dev_attr_d3cold_allowed.attr,
#endif
#ifdef CONFIG_OF
	&dev_attr_devspec.attr,
#endif
	&dev_attr_driver_override.attr,
	NULL,
};

static struct attribute *pci_bridge_attrs[] = {
	&dev_attr_subordinate_bus_number.attr,
	&dev_attr_secondary_bus_number.attr,
	NULL,
};

static struct attribute *pcie_dev_attrs[] = {
	&dev_attr_current_link_speed.attr,
	&dev_attr_current_link_width.attr,
	&dev_attr_max_link_width.attr,
	&dev_attr_max_link_speed.attr,
	NULL,
};

static struct attribute *pcibus_attrs[] = {
	&dev_attr_rescan.attr,
	&dev_attr_cpuaffinity.attr,
	&dev_attr_cpulistaffinity.attr,
	NULL,
};

static const struct attribute_group pcibus_group = {
	.attrs = pcibus_attrs,
};

const struct attribute_group *pcibus_groups[] = {
	&pcibus_group,
	NULL,
};

static ssize_t boot_vga_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct pci_dev *vga_dev = vga_default_device();

	if (vga_dev)
		return sprintf(buf, "%u\n", (pdev == vga_dev));

	return sprintf(buf, "%u\n",
		!!(pdev->resource[PCI_ROM_RESOURCE].flags &
		   IORESOURCE_ROM_SHADOW));
}
static struct device_attribute vga_attr = __ATTR_RO(boot_vga);

static ssize_t pci_read_config(struct file *filp, struct kobject *kobj,
			       struct bin_attribute *bin_attr, char *buf,
			       loff_t off, size_t count)
{
	struct pci_dev *dev = to_pci_dev(kobj_to_dev(kobj));
	unsigned int size = 64;
	loff_t init_off = off;
	u8 *data = (u8 *) buf;

	/* Several chips lock up trying to read undefined config space */
	if (file_ns_capable(filp, &init_user_ns, CAP_SYS_ADMIN))
		size = dev->cfg_size;
	else if (dev->hdr_type == PCI_HEADER_TYPE_CARDBUS)
		size = 128;

	if (off > size)
		return 0;
	if (off + count > size) {
		size -= off;
		count = size;
	} else {
		size = count;
	}

	pci_config_pm_runtime_get(dev);

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

	pci_config_pm_runtime_put(dev);

	return count;
}

static ssize_t pci_write_config(struct file *filp, struct kobject *kobj,
				struct bin_attribute *bin_attr, char *buf,
				loff_t off, size_t count)
{
	struct pci_dev *dev = to_pci_dev(kobj_to_dev(kobj));
	unsigned int size = count;
	loff_t init_off = off;
	u8 *data = (u8 *) buf;

	if (off > dev->cfg_size)
		return 0;
	if (off + count > dev->cfg_size) {
		size = dev->cfg_size - off;
		count = size;
	}

	pci_config_pm_runtime_get(dev);

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

	pci_config_pm_runtime_put(dev);

	return count;
}

static ssize_t read_vpd_attr(struct file *filp, struct kobject *kobj,
			     struct bin_attribute *bin_attr, char *buf,
			     loff_t off, size_t count)
{
	struct pci_dev *dev = to_pci_dev(kobj_to_dev(kobj));

	if (bin_attr->size > 0) {
		if (off > bin_attr->size)
			count = 0;
		else if (count > bin_attr->size - off)
			count = bin_attr->size - off;
	}

	return pci_read_vpd(dev, off, count, buf);
}

static ssize_t write_vpd_attr(struct file *filp, struct kobject *kobj,
			      struct bin_attribute *bin_attr, char *buf,
			      loff_t off, size_t count)
{
	struct pci_dev *dev = to_pci_dev(kobj_to_dev(kobj));

	if (bin_attr->size > 0) {
		if (off > bin_attr->size)
			count = 0;
		else if (count > bin_attr->size - off)
			count = bin_attr->size - off;
	}

	return pci_write_vpd(dev, off, count, buf);
}

#ifdef HAVE_PCI_LEGACY
/**
 * pci_read_legacy_io - read byte(s) from legacy I/O port space
 * @filp: open sysfs file
 * @kobj: kobject corresponding to file to read from
 * @bin_attr: struct bin_attribute for this file
 * @buf: buffer to store results
 * @off: offset into legacy I/O port space
 * @count: number of bytes to read
 *
 * Reads 1, 2, or 4 bytes from legacy I/O port space using an arch specific
 * callback routine (pci_legacy_read).
 */
static ssize_t pci_read_legacy_io(struct file *filp, struct kobject *kobj,
				  struct bin_attribute *bin_attr, char *buf,
				  loff_t off, size_t count)
{
	struct pci_bus *bus = to_pci_bus(kobj_to_dev(kobj));

	/* Only support 1, 2 or 4 byte accesses */
	if (count != 1 && count != 2 && count != 4)
		return -EINVAL;

	return pci_legacy_read(bus, off, (u32 *)buf, count);
}

/**
 * pci_write_legacy_io - write byte(s) to legacy I/O port space
 * @filp: open sysfs file
 * @kobj: kobject corresponding to file to read from
 * @bin_attr: struct bin_attribute for this file
 * @buf: buffer containing value to be written
 * @off: offset into legacy I/O port space
 * @count: number of bytes to write
 *
 * Writes 1, 2, or 4 bytes from legacy I/O port space using an arch specific
 * callback routine (pci_legacy_write).
 */
static ssize_t pci_write_legacy_io(struct file *filp, struct kobject *kobj,
				   struct bin_attribute *bin_attr, char *buf,
				   loff_t off, size_t count)
{
	struct pci_bus *bus = to_pci_bus(kobj_to_dev(kobj));

	/* Only support 1, 2 or 4 byte accesses */
	if (count != 1 && count != 2 && count != 4)
		return -EINVAL;

	return pci_legacy_write(bus, off, *(u32 *)buf, count);
}

/**
 * pci_mmap_legacy_mem - map legacy PCI memory into user memory space
 * @filp: open sysfs file
 * @kobj: kobject corresponding to device to be mapped
 * @attr: struct bin_attribute for this file
 * @vma: struct vm_area_struct passed to mmap
 *
 * Uses an arch specific callback, pci_mmap_legacy_mem_page_range, to mmap
 * legacy memory space (first meg of bus space) into application virtual
 * memory space.
 */
static int pci_mmap_legacy_mem(struct file *filp, struct kobject *kobj,
			       struct bin_attribute *attr,
			       struct vm_area_struct *vma)
{
	struct pci_bus *bus = to_pci_bus(kobj_to_dev(kobj));

	return pci_mmap_legacy_page_range(bus, vma, pci_mmap_mem);
}

/**
 * pci_mmap_legacy_io - map legacy PCI IO into user memory space
 * @filp: open sysfs file
 * @kobj: kobject corresponding to device to be mapped
 * @attr: struct bin_attribute for this file
 * @vma: struct vm_area_struct passed to mmap
 *
 * Uses an arch specific callback, pci_mmap_legacy_io_page_range, to mmap
 * legacy IO space (first meg of bus space) into application virtual
 * memory space. Returns -ENOSYS if the operation isn't supported
 */
static int pci_mmap_legacy_io(struct file *filp, struct kobject *kobj,
			      struct bin_attribute *attr,
			      struct vm_area_struct *vma)
{
	struct pci_bus *bus = to_pci_bus(kobj_to_dev(kobj));

	return pci_mmap_legacy_page_range(bus, vma, pci_mmap_io);
}

/**
 * pci_adjust_legacy_attr - adjustment of legacy file attributes
 * @b: bus to create files under
 * @mmap_type: I/O port or memory
 *
 * Stub implementation. Can be overridden by arch if necessary.
 */
void __weak pci_adjust_legacy_attr(struct pci_bus *b,
				   enum pci_mmap_state mmap_type)
{
}

/**
 * pci_create_legacy_files - create legacy I/O port and memory files
 * @b: bus to create files under
 *
 * Some platforms allow access to legacy I/O port and ISA memory space on
 * a per-bus basis.  This routine creates the files and ties them into
 * their associated read, write and mmap files from pci-sysfs.c
 *
 * On error unwind, but don't propagate the error to the caller
 * as it is ok to set up the PCI bus without these files.
 */
void pci_create_legacy_files(struct pci_bus *b)
{
	int error;

	b->legacy_io = kzalloc(sizeof(struct bin_attribute) * 2,
			       GFP_ATOMIC);
	if (!b->legacy_io)
		goto kzalloc_err;

	sysfs_bin_attr_init(b->legacy_io);
	b->legacy_io->attr.name = "legacy_io";
	b->legacy_io->size = 0xffff;
	b->legacy_io->attr.mode = S_IRUSR | S_IWUSR;
	b->legacy_io->read = pci_read_legacy_io;
	b->legacy_io->write = pci_write_legacy_io;
	b->legacy_io->mmap = pci_mmap_legacy_io;
	pci_adjust_legacy_attr(b, pci_mmap_io);
	error = device_create_bin_file(&b->dev, b->legacy_io);
	if (error)
		goto legacy_io_err;

	/* Allocated above after the legacy_io struct */
	b->legacy_mem = b->legacy_io + 1;
	sysfs_bin_attr_init(b->legacy_mem);
	b->legacy_mem->attr.name = "legacy_mem";
	b->legacy_mem->size = 1024*1024;
	b->legacy_mem->attr.mode = S_IRUSR | S_IWUSR;
	b->legacy_mem->mmap = pci_mmap_legacy_mem;
	pci_adjust_legacy_attr(b, pci_mmap_mem);
	error = device_create_bin_file(&b->dev, b->legacy_mem);
	if (error)
		goto legacy_mem_err;

	return;

legacy_mem_err:
	device_remove_bin_file(&b->dev, b->legacy_io);
legacy_io_err:
	kfree(b->legacy_io);
	b->legacy_io = NULL;
kzalloc_err:
	printk(KERN_WARNING "pci: warning: could not create legacy I/O port and ISA memory resources to sysfs\n");
	return;
}

void pci_remove_legacy_files(struct pci_bus *b)
{
	if (b->legacy_io) {
		device_remove_bin_file(&b->dev, b->legacy_io);
		device_remove_bin_file(&b->dev, b->legacy_mem);
		kfree(b->legacy_io); /* both are allocated here */
	}
}
#endif /* HAVE_PCI_LEGACY */

#if defined(HAVE_PCI_MMAP) || defined(ARCH_GENERIC_PCI_MMAP_RESOURCE)

int pci_mmap_fits(struct pci_dev *pdev, int resno, struct vm_area_struct *vma,
		  enum pci_mmap_api mmap_api)
{
	unsigned long nr, start, size;
	resource_size_t pci_start = 0, pci_end;

	if (pci_resource_len(pdev, resno) == 0)
		return 0;
	nr = vma_pages(vma);
	start = vma->vm_pgoff;
	size = ((pci_resource_len(pdev, resno) - 1) >> PAGE_SHIFT) + 1;
	if (mmap_api == PCI_MMAP_PROCFS) {
		pci_resource_to_user(pdev, resno, &pdev->resource[resno],
				     &pci_start, &pci_end);
		pci_start >>= PAGE_SHIFT;
	}
	if (start >= pci_start && start < pci_start + size &&
			start + nr <= pci_start + size)
		return 1;
	return 0;
}

/**
 * pci_mmap_resource - map a PCI resource into user memory space
 * @kobj: kobject for mapping
 * @attr: struct bin_attribute for the file being mapped
 * @vma: struct vm_area_struct passed into the mmap
 * @write_combine: 1 for write_combine mapping
 *
 * Use the regular PCI mapping routines to map a PCI resource into userspace.
 */
static int pci_mmap_resource(struct kobject *kobj, struct bin_attribute *attr,
			     struct vm_area_struct *vma, int write_combine)
{
	struct pci_dev *pdev = to_pci_dev(kobj_to_dev(kobj));
	int bar = (unsigned long)attr->private;
	enum pci_mmap_state mmap_type;
	struct resource *res = &pdev->resource[bar];

	if (res->flags & IORESOURCE_MEM && iomem_is_exclusive(res->start))
		return -EINVAL;

	if (!pci_mmap_fits(pdev, bar, vma, PCI_MMAP_SYSFS)) {
		WARN(1, "process \"%s\" tried to map 0x%08lx bytes at page 0x%08lx on %s BAR %d (start 0x%16Lx, size 0x%16Lx)\n",
			current->comm, vma->vm_end-vma->vm_start, vma->vm_pgoff,
			pci_name(pdev), bar,
			(u64)pci_resource_start(pdev, bar),
			(u64)pci_resource_len(pdev, bar));
		return -EINVAL;
	}
	mmap_type = res->flags & IORESOURCE_MEM ? pci_mmap_mem : pci_mmap_io;

	return pci_mmap_resource_range(pdev, bar, vma, mmap_type, write_combine);
}

static int pci_mmap_resource_uc(struct file *filp, struct kobject *kobj,
				struct bin_attribute *attr,
				struct vm_area_struct *vma)
{
	return pci_mmap_resource(kobj, attr, vma, 0);
}

static int pci_mmap_resource_wc(struct file *filp, struct kobject *kobj,
				struct bin_attribute *attr,
				struct vm_area_struct *vma)
{
	return pci_mmap_resource(kobj, attr, vma, 1);
}

static ssize_t pci_resource_io(struct file *filp, struct kobject *kobj,
			       struct bin_attribute *attr, char *buf,
			       loff_t off, size_t count, bool write)
{
	struct pci_dev *pdev = to_pci_dev(kobj_to_dev(kobj));
	int bar = (unsigned long)attr->private;
	unsigned long port = off;

	port += pci_resource_start(pdev, bar);

	if (port > pci_resource_end(pdev, bar))
		return 0;

	if (port + count - 1 > pci_resource_end(pdev, bar))
		return -EINVAL;

	switch (count) {
	case 1:
		if (write)
			outb(*(u8 *)buf, port);
		else
			*(u8 *)buf = inb(port);
		return 1;
	case 2:
		if (write)
			outw(*(u16 *)buf, port);
		else
			*(u16 *)buf = inw(port);
		return 2;
	case 4:
		if (write)
			outl(*(u32 *)buf, port);
		else
			*(u32 *)buf = inl(port);
		return 4;
	}
	return -EINVAL;
}

static ssize_t pci_read_resource_io(struct file *filp, struct kobject *kobj,
				    struct bin_attribute *attr, char *buf,
				    loff_t off, size_t count)
{
	return pci_resource_io(filp, kobj, attr, buf, off, count, false);
}

static ssize_t pci_write_resource_io(struct file *filp, struct kobject *kobj,
				     struct bin_attribute *attr, char *buf,
				     loff_t off, size_t count)
{
	return pci_resource_io(filp, kobj, attr, buf, off, count, true);
}

/**
 * pci_remove_resource_files - cleanup resource files
 * @pdev: dev to cleanup
 *
 * If we created resource files for @pdev, remove them from sysfs and
 * free their resources.
 */
static void pci_remove_resource_files(struct pci_dev *pdev)
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

static int pci_create_attr(struct pci_dev *pdev, int num, int write_combine)
{
	/* allocate attribute structure, piggyback attribute name */
	int name_len = write_combine ? 13 : 10;
	struct bin_attribute *res_attr;
	char *res_attr_name;
	int retval;

	res_attr = kzalloc(sizeof(*res_attr) + name_len, GFP_ATOMIC);
	if (!res_attr)
		return -ENOMEM;

	res_attr_name = (char *)(res_attr + 1);

	sysfs_bin_attr_init(res_attr);
	if (write_combine) {
		pdev->res_attr_wc[num] = res_attr;
		sprintf(res_attr_name, "resource%d_wc", num);
		res_attr->mmap = pci_mmap_resource_wc;
	} else {
		pdev->res_attr[num] = res_attr;
		sprintf(res_attr_name, "resource%d", num);
		if (pci_resource_flags(pdev, num) & IORESOURCE_IO) {
			res_attr->read = pci_read_resource_io;
			res_attr->write = pci_write_resource_io;
			if (arch_can_pci_mmap_io())
				res_attr->mmap = pci_mmap_resource_uc;
		} else {
			res_attr->mmap = pci_mmap_resource_uc;
		}
	}
	res_attr->attr.name = res_attr_name;
	res_attr->attr.mode = S_IRUSR | S_IWUSR;
	res_attr->size = pci_resource_len(pdev, num);
	res_attr->private = (void *)(unsigned long)num;
	retval = sysfs_create_bin_file(&pdev->dev.kobj, res_attr);
	if (retval)
		kfree(res_attr);

	return retval;
}

/**
 * pci_create_resource_files - create resource files in sysfs for @dev
 * @pdev: dev in question
 *
 * Walk the resources in @pdev creating files for each resource available.
 */
static int pci_create_resource_files(struct pci_dev *pdev)
{
	int i;
	int retval;

	/* Expose the PCI resources from this device as files */
	for (i = 0; i < PCI_ROM_RESOURCE; i++) {

		/* skip empty resources */
		if (!pci_resource_len(pdev, i))
			continue;

		retval = pci_create_attr(pdev, i, 0);
		/* for prefetchable resources, create a WC mappable file */
		if (!retval && arch_can_pci_mmap_wc() &&
		    pdev->resource[i].flags & IORESOURCE_PREFETCH)
			retval = pci_create_attr(pdev, i, 1);
		if (retval) {
			pci_remove_resource_files(pdev);
			return retval;
		}
	}
	return 0;
}
#else /* !HAVE_PCI_MMAP */
int __weak pci_create_resource_files(struct pci_dev *dev) { return 0; }
void __weak pci_remove_resource_files(struct pci_dev *dev) { return; }
#endif /* HAVE_PCI_MMAP */

/**
 * pci_write_rom - used to enable access to the PCI ROM display
 * @filp: sysfs file
 * @kobj: kernel object handle
 * @bin_attr: struct bin_attribute for this file
 * @buf: user input
 * @off: file offset
 * @count: number of byte in input
 *
 * writing anything except 0 enables it
 */
static ssize_t pci_write_rom(struct file *filp, struct kobject *kobj,
			     struct bin_attribute *bin_attr, char *buf,
			     loff_t off, size_t count)
{
	struct pci_dev *pdev = to_pci_dev(kobj_to_dev(kobj));

	if ((off ==  0) && (*buf == '0') && (count == 2))
		pdev->rom_attr_enabled = 0;
	else
		pdev->rom_attr_enabled = 1;

	return count;
}

/**
 * pci_read_rom - read a PCI ROM
 * @filp: sysfs file
 * @kobj: kernel object handle
 * @bin_attr: struct bin_attribute for this file
 * @buf: where to put the data we read from the ROM
 * @off: file offset
 * @count: number of bytes to read
 *
 * Put @count bytes starting at @off into @buf from the ROM in the PCI
 * device corresponding to @kobj.
 */
static ssize_t pci_read_rom(struct file *filp, struct kobject *kobj,
			    struct bin_attribute *bin_attr, char *buf,
			    loff_t off, size_t count)
{
	struct pci_dev *pdev = to_pci_dev(kobj_to_dev(kobj));
	void __iomem *rom;
	size_t size;

	if (!pdev->rom_attr_enabled)
		return -EINVAL;

	rom = pci_map_rom(pdev, &size);	/* size starts out as PCI window size */
	if (!rom || !size)
		return -EIO;

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

static const struct bin_attribute pci_config_attr = {
	.attr =	{
		.name = "config",
		.mode = S_IRUGO | S_IWUSR,
	},
	.size = PCI_CFG_SPACE_SIZE,
	.read = pci_read_config,
	.write = pci_write_config,
};

static const struct bin_attribute pcie_config_attr = {
	.attr =	{
		.name = "config",
		.mode = S_IRUGO | S_IWUSR,
	},
	.size = PCI_CFG_SPACE_EXP_SIZE,
	.read = pci_read_config,
	.write = pci_write_config,
};

static ssize_t reset_store(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	unsigned long val;
	ssize_t result = kstrtoul(buf, 0, &val);

	if (result < 0)
		return result;

	if (val != 1)
		return -EINVAL;

	result = pci_reset_function(pdev);
	if (result < 0)
		return result;

	return count;
}

static struct device_attribute reset_attr = __ATTR(reset, 0200, NULL, reset_store);

static int pci_create_capabilities_sysfs(struct pci_dev *dev)
{
	int retval;
	struct bin_attribute *attr;

	/* If the device has VPD, try to expose it in sysfs. */
	if (dev->vpd) {
		attr = kzalloc(sizeof(*attr), GFP_ATOMIC);
		if (!attr)
			return -ENOMEM;

		sysfs_bin_attr_init(attr);
		attr->size = 0;
		attr->attr.name = "vpd";
		attr->attr.mode = S_IRUSR | S_IWUSR;
		attr->read = read_vpd_attr;
		attr->write = write_vpd_attr;
		retval = sysfs_create_bin_file(&dev->dev.kobj, attr);
		if (retval) {
			kfree(attr);
			return retval;
		}
		dev->vpd->attr = attr;
	}

	/* Active State Power Management */
	pcie_aspm_create_sysfs_dev_files(dev);

	if (!pci_probe_reset_function(dev)) {
		retval = device_create_file(&dev->dev, &reset_attr);
		if (retval)
			goto error;
		dev->reset_fn = 1;
	}
	return 0;

error:
	pcie_aspm_remove_sysfs_dev_files(dev);
	if (dev->vpd && dev->vpd->attr) {
		sysfs_remove_bin_file(&dev->dev.kobj, dev->vpd->attr);
		kfree(dev->vpd->attr);
	}

	return retval;
}

int __must_check pci_create_sysfs_dev_files(struct pci_dev *pdev)
{
	int retval;
	int rom_size;
	struct bin_attribute *attr;

	if (!sysfs_initialized)
		return -EACCES;

	if (pdev->cfg_size > PCI_CFG_SPACE_SIZE)
		retval = sysfs_create_bin_file(&pdev->dev.kobj, &pcie_config_attr);
	else
		retval = sysfs_create_bin_file(&pdev->dev.kobj, &pci_config_attr);
	if (retval)
		goto err;

	retval = pci_create_resource_files(pdev);
	if (retval)
		goto err_config_file;

	/* If the device has a ROM, try to expose it in sysfs. */
	rom_size = pci_resource_len(pdev, PCI_ROM_RESOURCE);
	if (rom_size) {
		attr = kzalloc(sizeof(*attr), GFP_ATOMIC);
		if (!attr) {
			retval = -ENOMEM;
			goto err_resource_files;
		}
		sysfs_bin_attr_init(attr);
		attr->size = rom_size;
		attr->attr.name = "rom";
		attr->attr.mode = S_IRUSR | S_IWUSR;
		attr->read = pci_read_rom;
		attr->write = pci_write_rom;
		retval = sysfs_create_bin_file(&pdev->dev.kobj, attr);
		if (retval) {
			kfree(attr);
			goto err_resource_files;
		}
		pdev->rom_attr = attr;
	}

	/* add sysfs entries for various capabilities */
	retval = pci_create_capabilities_sysfs(pdev);
	if (retval)
		goto err_rom_file;

	pci_create_firmware_label_files(pdev);

	return 0;

err_rom_file:
	if (pdev->rom_attr) {
		sysfs_remove_bin_file(&pdev->dev.kobj, pdev->rom_attr);
		kfree(pdev->rom_attr);
		pdev->rom_attr = NULL;
	}
err_resource_files:
	pci_remove_resource_files(pdev);
err_config_file:
	if (pdev->cfg_size > PCI_CFG_SPACE_SIZE)
		sysfs_remove_bin_file(&pdev->dev.kobj, &pcie_config_attr);
	else
		sysfs_remove_bin_file(&pdev->dev.kobj, &pci_config_attr);
err:
	return retval;
}

static void pci_remove_capabilities_sysfs(struct pci_dev *dev)
{
	if (dev->vpd && dev->vpd->attr) {
		sysfs_remove_bin_file(&dev->dev.kobj, dev->vpd->attr);
		kfree(dev->vpd->attr);
	}

	pcie_aspm_remove_sysfs_dev_files(dev);
	if (dev->reset_fn) {
		device_remove_file(&dev->dev, &reset_attr);
		dev->reset_fn = 0;
	}
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

	pci_remove_capabilities_sysfs(pdev);

	if (pdev->cfg_size > PCI_CFG_SPACE_SIZE)
		sysfs_remove_bin_file(&pdev->dev.kobj, &pcie_config_attr);
	else
		sysfs_remove_bin_file(&pdev->dev.kobj, &pci_config_attr);

	pci_remove_resource_files(pdev);

	if (pdev->rom_attr) {
		sysfs_remove_bin_file(&pdev->dev.kobj, pdev->rom_attr);
		kfree(pdev->rom_attr);
		pdev->rom_attr = NULL;
	}

	pci_remove_firmware_label_files(pdev);
}

static int __init pci_sysfs_init(void)
{
	struct pci_dev *pdev = NULL;
	int retval;

	sysfs_initialized = 1;
	for_each_pci_dev(pdev) {
		retval = pci_create_sysfs_dev_files(pdev);
		if (retval) {
			pci_dev_put(pdev);
			return retval;
		}
	}

	return 0;
}
late_initcall(pci_sysfs_init);

static struct attribute *pci_dev_dev_attrs[] = {
	&vga_attr.attr,
	NULL,
};

static umode_t pci_dev_attrs_are_visible(struct kobject *kobj,
					 struct attribute *a, int n)
{
	struct device *dev = kobj_to_dev(kobj);
	struct pci_dev *pdev = to_pci_dev(dev);

	if (a == &vga_attr.attr)
		if ((pdev->class >> 8) != PCI_CLASS_DISPLAY_VGA)
			return 0;

	return a->mode;
}

static struct attribute *pci_dev_hp_attrs[] = {
	&dev_remove_attr.attr,
	&dev_rescan_attr.attr,
	NULL,
};

static umode_t pci_dev_hp_attrs_are_visible(struct kobject *kobj,
					    struct attribute *a, int n)
{
	struct device *dev = kobj_to_dev(kobj);
	struct pci_dev *pdev = to_pci_dev(dev);

	if (pdev->is_virtfn)
		return 0;

	return a->mode;
}

static umode_t pci_bridge_attrs_are_visible(struct kobject *kobj,
					    struct attribute *a, int n)
{
	struct device *dev = kobj_to_dev(kobj);
	struct pci_dev *pdev = to_pci_dev(dev);

	if (pci_is_bridge(pdev))
		return a->mode;

	return 0;
}

static umode_t pcie_dev_attrs_are_visible(struct kobject *kobj,
					  struct attribute *a, int n)
{
	struct device *dev = kobj_to_dev(kobj);
	struct pci_dev *pdev = to_pci_dev(dev);

	if (pci_is_pcie(pdev))
		return a->mode;

	return 0;
}

static const struct attribute_group pci_dev_group = {
	.attrs = pci_dev_attrs,
};

const struct attribute_group *pci_dev_groups[] = {
	&pci_dev_group,
	NULL,
};

static const struct attribute_group pci_bridge_group = {
	.attrs = pci_bridge_attrs,
};

const struct attribute_group *pci_bridge_groups[] = {
	&pci_bridge_group,
	NULL,
};

static const struct attribute_group pcie_dev_group = {
	.attrs = pcie_dev_attrs,
};

const struct attribute_group *pcie_dev_groups[] = {
	&pcie_dev_group,
	NULL,
};

static const struct attribute_group pci_dev_hp_attr_group = {
	.attrs = pci_dev_hp_attrs,
	.is_visible = pci_dev_hp_attrs_are_visible,
};

#ifdef CONFIG_PCI_IOV
static struct attribute *sriov_dev_attrs[] = {
	&sriov_totalvfs_attr.attr,
	&sriov_numvfs_attr.attr,
	&sriov_drivers_autoprobe_attr.attr,
	NULL,
};

static umode_t sriov_attrs_are_visible(struct kobject *kobj,
				       struct attribute *a, int n)
{
	struct device *dev = kobj_to_dev(kobj);

	if (!dev_is_pf(dev))
		return 0;

	return a->mode;
}

static const struct attribute_group sriov_dev_attr_group = {
	.attrs = sriov_dev_attrs,
	.is_visible = sriov_attrs_are_visible,
};
#endif /* CONFIG_PCI_IOV */

static const struct attribute_group pci_dev_attr_group = {
	.attrs = pci_dev_dev_attrs,
	.is_visible = pci_dev_attrs_are_visible,
};

static const struct attribute_group pci_bridge_attr_group = {
	.attrs = pci_bridge_attrs,
	.is_visible = pci_bridge_attrs_are_visible,
};

static const struct attribute_group pcie_dev_attr_group = {
	.attrs = pcie_dev_attrs,
	.is_visible = pcie_dev_attrs_are_visible,
};

static const struct attribute_group *pci_dev_attr_groups[] = {
	&pci_dev_attr_group,
	&pci_dev_hp_attr_group,
#ifdef CONFIG_PCI_IOV
	&sriov_dev_attr_group,
#endif
	&pci_bridge_attr_group,
	&pcie_dev_attr_group,
	NULL,
};

struct device_type pci_dev_type = {
	.groups = pci_dev_attr_groups,
};
