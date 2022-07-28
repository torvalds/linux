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
 * There are 3 objects defined in in this class:
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

#define ATA_PORT_ATTRS		3
#define ATA_LINK_ATTRS		3
#define ATA_DEV_ATTRS		9

struct scsi_transport_template;
struct scsi_transport_template *ata_scsi_transport_template;

struct ata_internal {
	struct scsi_transport_template t;

	struct device_attribute private_port_attrs[ATA_PORT_ATTRS];
	struct device_attribute private_link_attrs[ATA_LINK_ATTRS];
	struct device_attribute private_dev_attrs[ATA_DEV_ATTRS];

	struct transport_container link_attr_cont;
	struct transport_container dev_attr_cont;

	/*
	 * The array of null terminated pointers to attributes
	 * needed by scsi_sysfs.c
	 */
	struct device_attribute *link_attrs[ATA_LINK_ATTRS + 1];
	struct device_attribute *port_attrs[ATA_PORT_ATTRS + 1];
	struct device_attribute *dev_attrs[ATA_DEV_ATTRS + 1];
};
#define to_ata_internal(tmpl)	container_of(tmpl, struct ata_internal, t)


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


/* Device objects are always created whit link objects */
static int ata_tdev_add(struct ata_device *dev);
static void ata_tdev_delete(struct ata_device *dev);


/*
 * Hack to allow attributes of the same name in different objects.
 */
#define ATA_DEVICE_ATTR(_prefix,_name,_mode,_show,_store) \
	struct device_attribute device_attr_##_prefix##_##_name = \
	__ATTR(_name,_mode,_show,_store)

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
	{ AC_ERR_NCQ,		 	"NCQError" }
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
	return scnprintf(buf, 20, format_string, cast ap->field);	\
}

#define ata_port_simple_attr(field, name, format_string, type)		\
	ata_port_show_simple(field, name, format_string, (type))	\
static DEVICE_ATTR(name, S_IRUGO, show_ata_port_##name, NULL)

ata_port_simple_attr(nr_pmp_links, nr_pmp_links, "%d\n", int);
ata_port_simple_attr(stats.idle_irq, idle_irq, "%ld\n", unsigned long);
ata_port_simple_attr(local_port_no, port_no, "%u\n", unsigned int);

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
	return &ata_scsi_transport_template->host_attrs.ac == cont;
}

/**
 * ata_tport_delete  --  remove ATA PORT
 * @port:	ATA PORT to remove
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

	transport_add_device(dev);
	transport_configure_device(dev);

	error = ata_tlink_add(&ap->link);
	if (error) {
		goto tport_link_err;
	}
	return 0;

 tport_link_err:
	transport_remove_device(dev);
	device_del(dev);

 tport_err:
	transport_destroy_device(dev);
	put_device(dev);
	ata_host_put(ap->host);
	return error;
}


/*
 * ATA link attributes
 */
static int noop(int x) { return x; }

#define ata_link_show_linkspeed(field, format)			        \
static ssize_t								\
show_ata_link_##field(struct device *dev,				\
		      struct device_attribute *attr, char *buf)		\
{									\
	struct ata_link *link = transport_class_to_link(dev);		\
									\
	return sprintf(buf, "%s\n", sata_spd_string(format(link->field))); \
}

#define ata_link_linkspeed_attr(field, format)				\
	ata_link_show_linkspeed(field, format)				\
static DEVICE_ATTR(field, S_IRUGO, show_ata_link_##field, NULL)

ata_link_linkspeed_attr(hw_sata_spd_limit, fls);
ata_link_linkspeed_attr(sata_spd_limit, fls);
ata_link_linkspeed_attr(sata_spd, noop);


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
 *	%1 if the device represents a ATA link, %0 else
 */
static int ata_is_link(const struct device *dev)
{
	return dev->release == ata_tlink_release;
}

static int ata_tlink_match(struct attribute_container *cont,
			   struct device *dev)
{
	struct ata_internal* i = to_ata_internal(ata_scsi_transport_template);
	if (!ata_is_link(dev))
		return 0;
	return &i->link_attr_cont.ac == cont;
}

/**
 * ata_tlink_delete  --  remove ATA LINK
 * @port:	ATA LINK to remove
 *
 * Removes the specified ATA LINK.  remove associated ATA device(s) as well.
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
 * ata_tlink_add  --  initialize a transport ATA link structure
 * @link:	allocated ata_link structure.
 *
 * Initialize an ATA LINK structure for sysfs.  It will be added in the
 * device tree below the ATA PORT it belongs to.
 *
 * Returns %0 on success
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
	if (error) {
		goto tlink_err;
	}

	transport_add_device(dev);
	transport_configure_device(dev);

	ata_for_each_dev(ata_dev, link, ALL) {
		error = ata_tdev_add(ata_dev);
		if (error) {
			goto tlink_dev_err;
		}
	}
	return 0;
  tlink_dev_err:
	while (--ata_dev >= link->device) {
		ata_tdev_delete(ata_dev);
	}
	transport_remove_device(dev);
	device_del(dev);
  tlink_err:
	transport_destroy_device(dev);
	put_device(dev);
	return error;
}

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


#define ata_dev_show_simple(field, format_string, cast)		\
static ssize_t								\
show_ata_dev_##field(struct device *dev,				\
		     struct device_attribute *attr, char *buf)		\
{									\
	struct ata_device *ata_dev = transport_class_to_dev(dev);	\
									\
	return scnprintf(buf, 20, format_string, cast ata_dev->field);	\
}

#define ata_dev_simple_attr(field, format_string, type)	\
	ata_dev_show_simple(field, format_string, (type))	\
static DEVICE_ATTR(field, S_IRUGO, 			\
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
	else if (ata_dev->horkage & ATA_HORKAGE_NOTRIM)
		mode = "forced_unsupported";
	else if (ata_dev->horkage & ATA_HORKAGE_NO_NCQ_TRIM)
			mode = "forced_unqueued";
	else if (ata_fpdma_dsm_supported(ata_dev))
		mode = "queued";
	else
		mode = "unqueued";

	return scnprintf(buf, 20, "%s\n", mode);
}

static DEVICE_ATTR(trim, S_IRUGO, show_ata_dev_trim, NULL);

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
 *	%1 if the device represents a ATA device, %0 else
 */
static int ata_is_ata_dev(const struct device *dev)
{
	return dev->release == ata_tdev_release;
}

static int ata_tdev_match(struct attribute_container *cont,
			  struct device *dev)
{
	struct ata_internal* i = to_ata_internal(ata_scsi_transport_template);
	if (!ata_is_ata_dev(dev))
		return 0;
	return &i->dev_attr_cont.ac == cont;
}

/**
 * ata_tdev_free  --  free a ATA LINK
 * @dev:	ATA PHY to free
 *
 * Frees the specified ATA PHY.
 *
 * Note:
 *   This function must only be called on a PHY that has not
 *   successfully been added using ata_tdev_add().
 */
static void ata_tdev_free(struct ata_device *dev)
{
	transport_destroy_device(&dev->tdev);
	put_device(&dev->tdev);
}

/**
 * ata_tdev_delete  --  remove ATA device
 * @port:	ATA PORT to remove
 *
 * Removes the specified ATA device.
 */
static void ata_tdev_delete(struct ata_device *ata_dev)
{
	struct device *dev = &ata_dev->tdev;

	transport_remove_device(dev);
	device_del(dev);
	ata_tdev_free(ata_dev);
}


/**
 * ata_tdev_add  --  initialize a transport ATA device structure.
 * @ata_dev:	ata_dev structure.
 *
 * Initialize an ATA device structure for sysfs.  It will be added in the
 * device tree below the ATA LINK device it belongs to.
 *
 * Returns %0 on success
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

	transport_add_device(dev);
	transport_configure_device(dev);
	return 0;
}


/*
 * Setup / Teardown code
 */

#define SETUP_TEMPLATE(attrb, field, perm, test)			\
	i->private_##attrb[count] = dev_attr_##field;		       	\
	i->private_##attrb[count].attr.mode = perm;			\
	i->attrb[count] = &i->private_##attrb[count];			\
	if (test)							\
		count++

#define SETUP_LINK_ATTRIBUTE(field)					\
	SETUP_TEMPLATE(link_attrs, field, S_IRUGO, 1)

#define SETUP_PORT_ATTRIBUTE(field)					\
	SETUP_TEMPLATE(port_attrs, field, S_IRUGO, 1)

#define SETUP_DEV_ATTRIBUTE(field)					\
	SETUP_TEMPLATE(dev_attrs, field, S_IRUGO, 1)

/**
 * ata_attach_transport  --  instantiate ATA transport template
 */
struct scsi_transport_template *ata_attach_transport(void)
{
	struct ata_internal *i;
	int count;

	i = kzalloc(sizeof(struct ata_internal), GFP_KERNEL);
	if (!i)
		return NULL;

	i->t.eh_strategy_handler	= ata_scsi_error;
	i->t.user_scan			= ata_scsi_user_scan;

	i->t.host_attrs.ac.attrs = &i->port_attrs[0];
	i->t.host_attrs.ac.class = &ata_port_class.class;
	i->t.host_attrs.ac.match = ata_tport_match;
	transport_container_register(&i->t.host_attrs);

	i->link_attr_cont.ac.class = &ata_link_class.class;
	i->link_attr_cont.ac.attrs = &i->link_attrs[0];
	i->link_attr_cont.ac.match = ata_tlink_match;
	transport_container_register(&i->link_attr_cont);

	i->dev_attr_cont.ac.class = &ata_dev_class.class;
	i->dev_attr_cont.ac.attrs = &i->dev_attrs[0];
	i->dev_attr_cont.ac.match = ata_tdev_match;
	transport_container_register(&i->dev_attr_cont);

	count = 0;
	SETUP_PORT_ATTRIBUTE(nr_pmp_links);
	SETUP_PORT_ATTRIBUTE(idle_irq);
	SETUP_PORT_ATTRIBUTE(port_no);
	BUG_ON(count > ATA_PORT_ATTRS);
	i->port_attrs[count] = NULL;

	count = 0;
	SETUP_LINK_ATTRIBUTE(hw_sata_spd_limit);
	SETUP_LINK_ATTRIBUTE(sata_spd_limit);
	SETUP_LINK_ATTRIBUTE(sata_spd);
	BUG_ON(count > ATA_LINK_ATTRS);
	i->link_attrs[count] = NULL;

	count = 0;
	SETUP_DEV_ATTRIBUTE(class);
	SETUP_DEV_ATTRIBUTE(pio_mode);
	SETUP_DEV_ATTRIBUTE(dma_mode);
	SETUP_DEV_ATTRIBUTE(xfer_mode);
	SETUP_DEV_ATTRIBUTE(spdn_cnt);
	SETUP_DEV_ATTRIBUTE(ering);
	SETUP_DEV_ATTRIBUTE(id);
	SETUP_DEV_ATTRIBUTE(gscr);
	SETUP_DEV_ATTRIBUTE(trim);
	BUG_ON(count > ATA_DEV_ATTRS);
	i->dev_attrs[count] = NULL;

	return &i->t;
}

/**
 * ata_release_transport  --  release ATA transport template instance
 * @t:		transport template instance
 */
void ata_release_transport(struct scsi_transport_template *t)
{
	struct ata_internal *i = to_ata_internal(t);

	transport_container_unregister(&i->t.host_attrs);
	transport_container_unregister(&i->link_attr_cont);
	transport_container_unregister(&i->dev_attr_cont);

	kfree(i);
}

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
	transport_class_unregister(&ata_link_class);
	transport_class_unregister(&ata_port_class);
	transport_class_unregister(&ata_dev_class);
}
