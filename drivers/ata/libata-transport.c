// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Copyright 2008 ioogle, Inc.  All rights reserved.
 *
 * Libata transport class.
 *
 * The ATA transport class contains common code to deal with ATA HBAs,
 * an approximated representation of ATA topologies in the driver model,
 * and various sysfs attributes to expose these topologies and management
 * interfaces to user-space.
 *
 * There are 3 objects defined in this class:
 * - ata_port
 * - ata_link
 * - ata_device
 * Each port has a link object. Each link can have up to two devices for PATA
 * and generally one for SATA.
 * If there is SATA port multiplier [PMP], 15 additional ata_link object are
 * created.
 *
 * These objects are created when the ata host is initialized and when a PMP is
 * found. They are removed only when the HBA is removed, cleaned before the
 * error handler runs.
 */


#include <linux/kernel.h>
#include <linux/blkdev.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <scsi/scsi_transport.h>
#include <linux/libata.h>
#include <linux/hdreg.h>
#include <linux/uaccess.h>
#include <linux/pm_runtime.h>

#include "libata.h"
#include "libata-transport.h"

static int ata_tlink_match(struct attribute_container *cont,
			   struct device *dev);
static int ata_tdev_match(struct attribute_container *cont,
			  struct device *dev);

#define tdev_to_device(d)					\
	container_of((d), struct ata_device, tdev)
#define transport_class_to_dev(dev)				\
	tdev_to_device((dev)->parent)

#define tdev_to_link(d)						\
	container_of((d), struct ata_link, tdev)
#define transport_class_to_link(dev)				\
	tdev_to_link((dev)->parent)

#define tdev_to_port(d)						\
	container_of((d), struct ata_port, tdev)
#define transport_class_to_port(dev)				\
	tdev_to_port((dev)->parent)

#define ata_bitfield_name_match(title, table)			\
static ssize_t							\
get_ata_##title##_names(u32 table_key, char *buf)		\
{								\
	char *prefix = "";					\
	ssize_t len = 0;					\
	int i;							\
								\
	for (i = 0; i < ARRAY_SIZE(table); i++) {		\
		if (table[i].value & table_key) {		\
			len += sprintf(buf + len, "%s%s",	\
				prefix, table[i].name);		\
			prefix = ", ";				\
		}						\
	}							\
	len += sprintf(buf + len, "\n");			\
	return len;						\
}

#define ata_bitfield_name_search(title, table)			\
static ssize_t							\
get_ata_##title##_names(u32 table_key, char *buf)		\
{								\
	ssize_t len = 0;					\
	int i;							\
								\
	for (i = 0; i < ARRAY_SIZE(table); i++) {		\
		if (table[i].value == table_key) {		\
			len += sprintf(buf + len, "%s",		\
				table[i].name);			\
			break;					\
		}						\
	}							\
	len += sprintf(buf + len, "\n");			\
	return len;						\
}

static struct {
	u32		value;
	char		*name;
} ata_class_names[] = {
	{ ATA_DEV_UNKNOWN,		"unknown" },
	{ ATA_DEV_ATA,			"ata" },
	{ ATA_DEV_ATA_UNSUP,		"ata" },
	{ ATA_DEV_ATAPI,		"atapi" },
	{ ATA_DEV_ATAPI_UNSUP,		"atapi" },
	{ ATA_DEV_PMP,			"pmp" },
	{ ATA_DEV_PMP_UNSUP,		"pmp" },
	{ ATA_DEV_SEMB,			"semb" },
	{ ATA_DEV_SEMB_UNSUP,		"semb" },
	{ ATA_DEV_ZAC,			"zac" },
	{ ATA_DEV_NONE,			"none" }
};
ata_bitfield_name_search(class, ata_class_names)


static struct {
	u32		value;
	char		*name;
} ata_err_names[] = {
	{ AC_ERR_DEV,			"DeviceError" },
	{ AC_ERR_HSM,			"HostStateMachineError" },
	{ AC_ERR_TIMEOUT,		"Timeout" },
	{ AC_ERR_MEDIA,			"MediaError" },
	{ AC_ERR_ATA_BUS,		"BusError" },
	{ AC_ERR_HOST_BUS,		"HostBusError" },
	{ AC_ERR_SYSTEM,		"SystemError" },
	{ AC_ERR_INVALID,		"InvalidArg" },
	{ AC_ERR_OTHER,			"Unknown" },
	{ AC_ERR_NODEV_HINT,		"NoDeviceHint" },
	{ AC_ERR_NCQ,			"NCQError" }
};
ata_bitfield_name_match(err, ata_err_names)

static struct {
	u32		value;
	char		*name;
} ata_xfer_names[] = {
	{ XFER_UDMA_7,			"XFER_UDMA_7" },
	{ XFER_UDMA_6,			"XFER_UDMA_6" },
	{ XFER_UDMA_5,			"XFER_UDMA_5" },
	{ XFER_UDMA_4,			"XFER_UDMA_4" },
	{ XFER_UDMA_3,			"XFER_UDMA_3" },
	{ XFER_UDMA_2,			"XFER_UDMA_2" },
	{ XFER_UDMA_1,			"XFER_UDMA_1" },
	{ XFER_UDMA_0,			"XFER_UDMA_0" },
	{ XFER_MW_DMA_4,		"XFER_MW_DMA_4" },
	{ XFER_MW_DMA_3,		"XFER_MW_DMA_3" },
	{ XFER_MW_DMA_2,		"XFER_MW_DMA_2" },
	{ XFER_MW_DMA_1,		"XFER_MW_DMA_1" },
	{ XFER_MW_DMA_0,		"XFER_MW_DMA_0" },
	{ XFER_SW_DMA_2,		"XFER_SW_DMA_2" },
	{ XFER_SW_DMA_1,		"XFER_SW_DMA_1" },
	{ XFER_SW_DMA_0,		"XFER_SW_DMA_0" },
	{ XFER_PIO_6,			"XFER_PIO_6" },
	{ XFER_PIO_5,			"XFER_PIO_5" },
	{ XFER_PIO_4,			"XFER_PIO_4" },
	{ XFER_PIO_3,			"XFER_PIO_3" },
	{ XFER_PIO_2,			"XFER_PIO_2" },
	{ XFER_PIO_1,			"XFER_PIO_1" },
	{ XFER_PIO_0,			"XFER_PIO_0" },
	{ XFER_PIO_SLOW,		"XFER_PIO_SLOW" }
};
ata_bitfield_name_search(xfer, ata_xfer_names)

/*
 * ATA Port attributes
 */
#define ata_port_show_simple(field, name, format_string, cast)		\
static ssize_t								\
show_ata_port_##name(struct device *dev,				\
		     struct device_attribute *attr, char *buf)		\
{									\
	struct ata_port *ap = transport_class_to_port(dev);		\
									\
	return sysfs_emit(buf, format_string, cast ap->field);	        \
}

#define ata_port_simple_attr(field, name, format_string, type)		\
	ata_port_show_simple(field, name, format_string, (type))	\
static DEVICE_ATTR(name, S_IRUGO, show_ata_port_##name, NULL)

ata_port_simple_attr(nr_pmp_links, nr_pmp_links, "%d\n", int);
ata_port_simple_attr(stats.idle_irq, idle_irq, "%ld\n", unsigned long);
/* We want the port_no sysfs attibute to start at 1 (ap->port_no starts at 0) */
ata_port_simple_attr(port_no + 1, port_no, "%u\n", unsigned int);

static const struct attribute *const ata_port_attr_attrs[] = {
	&dev_attr_nr_pmp_links.attr,
	&dev_attr_idle_irq.attr,
	&dev_attr_port_no.attr,
	NULL
};

static const struct attribute_group ata_port_attr_group = {
	.attrs_const = ata_port_attr_attrs,
};

static DECLARE_TRANSPORT_CLASS(ata_port_class,
			       "ata_port", NULL, NULL, NULL);

static void ata_tport_release(struct device *dev)
{
	struct ata_port *ap = tdev_to_port(dev);
	ata_host_put(ap->host);
}

/**
 * ata_is_port --  check if a struct device represents a ATA port
 * @dev:	device to check
 *
 * Returns:
 *	%1 if the device represents a ATA Port, %0 else
 */
static int ata_is_port(const struct device *dev)
{
	return dev->release == ata_tport_release;
}

static int ata_tport_match(struct attribute_container *cont,
			   struct device *dev)
{
	if (!ata_is_port(dev))
		return 0;
	return &ata_scsi_transportt.host_attrs.ac == cont;
}

/**
 * ata_tport_delete  --  remove ATA PORT
 * @ap:	ATA PORT to remove
 *
 * Removes the specified ATA PORT.  Remove the associated link as well.
 */
void ata_tport_delete(struct ata_port *ap)
{
	struct device *dev = &ap->tdev;

	ata_tlink_delete(&ap->link);

	transport_remove_device(dev);
	device_del(dev);
	transport_destroy_device(dev);
	put_device(dev);
}
EXPORT_SYMBOL_GPL(ata_tport_delete);

static const struct device_type ata_port_sas_type = {
	.name = ATA_PORT_TYPE_NAME,
};

/** ata_tport_add - initialize a transport ATA port structure
 *
 * @parent:	parent device
 * @ap:		existing ata_port structure
 *
 * Initialize a ATA port structure for sysfs.  It will be added to the device
 * tree below the device specified by @parent which could be a PCI device.
 *
 * Returns %0 on success
 */
int ata_tport_add(struct device *parent,
		  struct ata_port *ap)
{
	int error;
	struct device *dev = &ap->tdev;

	device_initialize(dev);
	if (ap->flags & ATA_FLAG_SAS_HOST)
		dev->type = &ata_port_sas_type;
	else
		dev->type = &ata_port_type;

	dev->parent = parent;
	ata_host_get(ap->host);
	dev->release = ata_tport_release;
	dev_set_name(dev, "ata%d", ap->print_id);
	transport_setup_device(dev);
	ata_acpi_bind_port(ap);
	error = device_add(dev);
	if (error) {
		goto tport_err;
	}

	device_enable_async_suspend(dev);
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_forbid(dev);

	error = transport_add_device(dev);
	if (error)
		goto tport_transport_add_err;
	transport_configure_device(dev);

	error = ata_tlink_add(&ap->link);
	if (error) {
		goto tport_link_err;
	}
	return 0;

 tport_link_err:
	transport_remove_device(dev);
 tport_transport_add_err:
	device_del(dev);

 tport_err:
	transport_destroy_device(dev);
	put_device(dev);
	return error;
}
EXPORT_SYMBOL_GPL(ata_tport_add);

/**
 *     ata_port_classify - determine device type based on ATA-spec signature
 *     @ap: ATA port device on which the classification should be run
 *     @tf: ATA taskfile register set for device to be identified
 *
 *     A wrapper around ata_dev_classify() to provide additional logging
 *
 *     RETURNS:
 *     Device type, %ATA_DEV_ATA, %ATA_DEV_ATAPI, %ATA_DEV_PMP,
 *     %ATA_DEV_ZAC, or %ATA_DEV_UNKNOWN the event of failure.
 */
unsigned int ata_port_classify(struct ata_port *ap,
			       const struct ata_taskfile *tf)
{
	int i;
	unsigned int class = ata_dev_classify(tf);

	/* Start with index '1' to skip the 'unknown' entry */
	for (i = 1; i < ARRAY_SIZE(ata_class_names); i++) {
		if (ata_class_names[i].value == class) {
			ata_port_dbg(ap, "found %s device by sig\n",
				     ata_class_names[i].name);
			return class;
		}
	}

	ata_port_info(ap, "found unknown device (class %u)\n", class);
	return class;
}
EXPORT_SYMBOL_GPL(ata_port_classify);

/*
 * ATA device attributes
 */

#define ata_dev_show_class(title, field)				\
static ssize_t								\
show_ata_dev_##field(struct device *dev,				\
		     struct device_attribute *attr, char *buf)		\
{									\
	struct ata_device *ata_dev = transport_class_to_dev(dev);	\
									\
	return get_ata_##title##_names(ata_dev->field, buf);		\
}

#define ata_dev_attr(title, field)					\
	ata_dev_show_class(title, field)				\
static DEVICE_ATTR(field, S_IRUGO, show_ata_dev_##field, NULL)

ata_dev_attr(class, class);
ata_dev_attr(xfer, pio_mode);
ata_dev_attr(xfer, dma_mode);
ata_dev_attr(xfer, xfer_mode);


#define ata_dev_show_simple(field, format_string, cast)			\
static ssize_t								\
show_ata_dev_##field(struct device *dev,				\
		     struct device_attribute *attr, char *buf)		\
{									\
	struct ata_device *ata_dev = transport_class_to_dev(dev);	\
									\
	return sysfs_emit(buf, format_string, cast ata_dev->field);	\
}

#define ata_dev_simple_attr(field, format_string, type)		\
	ata_dev_show_simple(field, format_string, (type))	\
	static DEVICE_ATTR(field, S_IRUGO,			\
		   show_ata_dev_##field, NULL)

ata_dev_simple_attr(spdn_cnt, "%d\n", int);

struct ata_show_ering_arg {
	char* buf;
	int written;
};

static int ata_show_ering(struct ata_ering_entry *ent, void *void_arg)
{
	struct ata_show_ering_arg* arg = void_arg;
	u64 seconds;
	u32 rem;

	seconds = div_u64_rem(ent->timestamp, HZ, &rem);
	arg->written += sprintf(arg->buf + arg->written,
				"[%5llu.%09lu]", seconds,
				rem * NSEC_PER_SEC / HZ);
	arg->written += get_ata_err_names(ent->err_mask,
					  arg->buf + arg->written);
	return 0;
}

static ssize_t
show_ata_dev_ering(struct device *dev,
		   struct device_attribute *attr, char *buf)
{
	struct ata_device *ata_dev = transport_class_to_dev(dev);
	struct ata_show_ering_arg arg = { buf, 0 };

	ata_ering_map(&ata_dev->ering, ata_show_ering, &arg);
	return arg.written;
}


static DEVICE_ATTR(ering, S_IRUGO, show_ata_dev_ering, NULL);

static ssize_t
show_ata_dev_id(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct ata_device *ata_dev = transport_class_to_dev(dev);
	int written = 0, i = 0;

	if (ata_dev->class == ATA_DEV_PMP)
		return 0;
	for(i=0;i<ATA_ID_WORDS;i++)  {
		written += scnprintf(buf+written, 20, "%04x%c",
				    ata_dev->id[i],
				    ((i+1) & 7) ? ' ' : '\n');
	}
	return written;
}

static DEVICE_ATTR(id, S_IRUGO, show_ata_dev_id, NULL);

static ssize_t
show_ata_dev_gscr(struct device *dev,
		  struct device_attribute *attr, char *buf)
{
	struct ata_device *ata_dev = transport_class_to_dev(dev);
	int written = 0, i = 0;

	if (ata_dev->class != ATA_DEV_PMP)
		return 0;
	for(i=0;i<SATA_PMP_GSCR_DWORDS;i++)  {
		written += scnprintf(buf+written, 20, "%08x%c",
				    ata_dev->gscr[i],
				    ((i+1) & 3) ? ' ' : '\n');
	}
	if (SATA_PMP_GSCR_DWORDS & 3)
		buf[written-1] = '\n';
	return written;
}

static DEVICE_ATTR(gscr, S_IRUGO, show_ata_dev_gscr, NULL);

static ssize_t
show_ata_dev_trim(struct device *dev,
		  struct device_attribute *attr, char *buf)
{
	struct ata_device *ata_dev = transport_class_to_dev(dev);
	unsigned char *mode;

	if (!ata_id_has_trim(ata_dev->id))
		mode = "unsupported";
	else if (ata_dev->quirks & ATA_QUIRK_NOTRIM)
		mode = "forced_unsupported";
	else if (ata_dev->quirks & ATA_QUIRK_NO_NCQ_TRIM)
		mode = "forced_unqueued";
	else if (ata_fpdma_dsm_supported(ata_dev))
		mode = "queued";
	else
		mode = "unqueued";

	return scnprintf(buf, 20, "%s\n", mode);
}

static DEVICE_ATTR(trim, S_IRUGO, show_ata_dev_trim, NULL);

static const struct attribute *const ata_device_attr_attrs[] = {
	&dev_attr_class.attr,
	&dev_attr_pio_mode.attr,
	&dev_attr_dma_mode.attr,
	&dev_attr_xfer_mode.attr,
	&dev_attr_spdn_cnt.attr,
	&dev_attr_ering.attr,
	&dev_attr_id.attr,
	&dev_attr_gscr.attr,
	&dev_attr_trim.attr,
	NULL
};

static const struct attribute_group ata_device_attr_group = {
	.attrs_const = ata_device_attr_attrs,
};

static DECLARE_TRANSPORT_CLASS(ata_dev_class,
			       "ata_device", NULL, NULL, NULL);

static void ata_tdev_release(struct device *dev)
{
}

/**
 * ata_is_ata_dev  --  check if a struct device represents a ATA device
 * @dev:	device to check
 *
 * Returns:
 *	true if the device represents a ATA device, false otherwise
 */
static bool ata_is_ata_dev(const struct device *dev)
{
	return dev->release == ata_tdev_release;
}

/**
 * ata_tdev_free  --  free an ATA transport device
 * @dev:	struct ata_device owning the transport device to free
 *
 * Free the ATA transport device for the specified ATA device.
 *
 * Note:
 *   This function must only be called for a ATA transport device that has not
 *   yet successfully been added using ata_tdev_add().
 */
static void ata_tdev_free(struct ata_device *dev)
{
	transport_destroy_device(&dev->tdev);
	put_device(&dev->tdev);
}

/**
 * ata_tdev_delete  --  remove an ATA transport device
 * @ata_dev:	struct ata_device owning the transport device to delete
 *
 * Removes the ATA transport device for the specified ATA device.
 */
static void ata_tdev_delete(struct ata_device *ata_dev)
{
	struct device *dev = &ata_dev->tdev;

	transport_remove_device(dev);
	device_del(dev);
	ata_tdev_free(ata_dev);
}

/**
 * ata_tdev_add  --  initialize an ATA transport device
 * @ata_dev:	struct ata_device owning the transport device to add
 *
 * Initialize an ATA transport device for sysfs.  It will be added in the
 * device tree below the ATA link device it belongs to.
 *
 * Returns %0 on success and a negative error code on error.
 */
static int ata_tdev_add(struct ata_device *ata_dev)
{
	struct device *dev = &ata_dev->tdev;
	struct ata_link *link = ata_dev->link;
	struct ata_port *ap = link->ap;
	int error;

	device_initialize(dev);
	dev->parent = &link->tdev;
	dev->release = ata_tdev_release;
	if (ata_is_host_link(link))
		dev_set_name(dev, "dev%d.%d", ap->print_id,ata_dev->devno);
	else
		dev_set_name(dev, "dev%d.%d.0", ap->print_id, link->pmp);

	transport_setup_device(dev);
	ata_acpi_bind_dev(ata_dev);
	error = device_add(dev);
	if (error) {
		ata_tdev_free(ata_dev);
		return error;
	}

	error = transport_add_device(dev);
	if (error) {
		device_del(dev);
		ata_tdev_free(ata_dev);
		return error;
	}

	transport_configure_device(dev);
	return 0;
}

/*
 * ATA link attributes
 */
static int noop(int x)
{
	return x;
}

#define ata_link_show_linkspeed(field, format)			\
static ssize_t							\
show_ata_link_##field(struct device *dev,			\
		      struct device_attribute *attr, char *buf)	\
{								\
	struct ata_link *link = transport_class_to_link(dev);	\
								\
	return sprintf(buf, "%s\n",				\
		       sata_spd_string(format(link->field)));	\
}

#define ata_link_linkspeed_attr(field, format)			\
	ata_link_show_linkspeed(field, format)			\
static DEVICE_ATTR(field, 0444, show_ata_link_##field, NULL)

ata_link_linkspeed_attr(hw_sata_spd_limit, fls);
ata_link_linkspeed_attr(sata_spd_limit, fls);
ata_link_linkspeed_attr(sata_spd, noop);

static const struct attribute *const ata_link_attr_attrs[] = {
	&dev_attr_hw_sata_spd_limit.attr,
	&dev_attr_sata_spd_limit.attr,
	&dev_attr_sata_spd.attr,
	NULL
};

static const struct attribute_group ata_link_attr_group = {
	.attrs_const = ata_link_attr_attrs,
};

static DECLARE_TRANSPORT_CLASS(ata_link_class,
		"ata_link", NULL, NULL, NULL);

static void ata_tlink_release(struct device *dev)
{
}

/**
 * ata_is_link --  check if a struct device represents a ATA link
 * @dev:	device to check
 *
 * Returns:
 *	true if the device represents a ATA link, false otherwise
 */
static bool ata_is_link(const struct device *dev)
{
	return dev->release == ata_tlink_release;
}

/**
 * ata_tlink_delete  --  remove an ATA link transport device
 * @link:	struct ata_link owning the link transport device to remove
 *
 * Removes the link transport device of the specified ATA link. This also
 * removes the ATA device(s) associated with the link as well.
 */
void ata_tlink_delete(struct ata_link *link)
{
	struct device *dev = &link->tdev;
	struct ata_device *ata_dev;

	ata_for_each_dev(ata_dev, link, ALL) {
		ata_tdev_delete(ata_dev);
	}

	transport_remove_device(dev);
	device_del(dev);
	transport_destroy_device(dev);
	put_device(dev);
}

/**
 * ata_tlink_add  --  initialize an ATA link transport device
 * @link:	struct ata_link owning the link transport device to initialize
 *
 * Initialize an ATA link transport device for sysfs. It will be added in the
 * device tree below the ATA port it belongs to.
 *
 * Returns %0 on success and a negative error code on error.
 */
int ata_tlink_add(struct ata_link *link)
{
	struct device *dev = &link->tdev;
	struct ata_port *ap = link->ap;
	struct ata_device *ata_dev;
	int error;

	device_initialize(dev);
	dev->parent = &ap->tdev;
	dev->release = ata_tlink_release;
	if (ata_is_host_link(link))
		dev_set_name(dev, "link%d", ap->print_id);
	else
		dev_set_name(dev, "link%d.%d", ap->print_id, link->pmp);

	transport_setup_device(dev);

	error = device_add(dev);
	if (error)
		goto tlink_err;

	error = transport_add_device(dev);
	if (error)
		goto tlink_transport_err;
	transport_configure_device(dev);

	ata_for_each_dev(ata_dev, link, ALL) {
		error = ata_tdev_add(ata_dev);
		if (error)
			goto tlink_dev_err;
	}
	return 0;
 tlink_dev_err:
	while (--ata_dev >= link->device)
		ata_tdev_delete(ata_dev);
	transport_remove_device(dev);
 tlink_transport_err:
	device_del(dev);
 tlink_err:
	transport_destroy_device(dev);
	put_device(dev);
	return error;
}

struct scsi_transport_template ata_scsi_transportt = {
	.eh_strategy_handler	= ata_scsi_error,
	.user_scan		= ata_scsi_user_scan,

	.host_attrs.ac.class	= &ata_port_class.class,
	.host_attrs.ac.grp	= &ata_port_attr_group,
	.host_attrs.ac.match	= ata_tport_match,
};

static struct transport_container ata_link_attr_cont = {
	.ac.class	= &ata_link_class.class,
	.ac.grp		= &ata_link_attr_group,
	.ac.match	= ata_tlink_match,
};

static struct transport_container ata_dev_attr_cont = {
	.ac.class	= &ata_dev_class.class,
	.ac.grp		= &ata_device_attr_group,
	.ac.match	= ata_tdev_match,
};

static int ata_tlink_match(struct attribute_container *cont,
			   struct device *dev)
{
	if (!ata_is_link(dev))
		return 0;

	return &ata_link_attr_cont.ac == cont;
}

static int ata_tdev_match(struct attribute_container *cont,
			  struct device *dev)
{
	if (!ata_is_ata_dev(dev))
		return 0;

	return &ata_dev_attr_cont.ac == cont;
}

/*
 * Setup / Teardown code
 */

__init int libata_transport_init(void)
{
	int error;

	error = transport_class_register(&ata_link_class);
	if (error)
		goto out_unregister_transport;
	error = transport_class_register(&ata_port_class);
	if (error)
		goto out_unregister_link;
	error = transport_class_register(&ata_dev_class);
	if (error)
		goto out_unregister_port;

	transport_container_register(&ata_scsi_transportt.host_attrs);
	transport_container_register(&ata_link_attr_cont);
	transport_container_register(&ata_dev_attr_cont);

	return 0;

 out_unregister_port:
	transport_class_unregister(&ata_port_class);
 out_unregister_link:
	transport_class_unregister(&ata_link_class);
 out_unregister_transport:
	return error;

}

void __exit libata_transport_exit(void)
{
	transport_container_unregister(&ata_scsi_transportt.host_attrs);
	transport_container_unregister(&ata_link_attr_cont);
	transport_container_unregister(&ata_dev_attr_cont);

	transport_class_unregister(&ata_link_class);
	transport_class_unregister(&ata_port_class);
	transport_class_unregister(&ata_dev_class);
}
