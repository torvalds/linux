#include <linux/module.h>
#include <linux/utsname.h>


/*
 * kbuild is not very cooperative with respect to linking separately
 * compiled library objects into one module.  So for now we won't use
 * separate compilation ... ensuring init/exit sections work to shrink
 * the runtime footprint, and giving us at least some parts of what
 * a "gcc --combine ... part1.c part2.c part3.c ... " build would.
 */

#include "composite.c"
#include "usbstring.c"
#include "config.c"
#include "epautoconf.c"

#if defined CONFIG_USB_FUNCTIONFS_ETH || defined CONFIG_USB_FUNCTIONFS_RNDIS
#  if defined USB_ETH_RNDIS
#    undef USB_ETH_RNDIS
#  endif
#  ifdef CONFIG_USB_FUNCTIONFS_RNDIS
#    define USB_ETH_RNDIS y
#  endif

#  include "f_ecm.c"
#  include "f_subset.c"
#  ifdef USB_ETH_RNDIS
#    include "f_rndis.c"
#    include "rndis.c"
#  endif
#  include "u_ether.c"

static u8 gfs_hostaddr[ETH_ALEN];
#  ifdef CONFIG_USB_FUNCTIONFS_ETH
static int eth_bind_config(struct usb_configuration *c, u8 ethaddr[ETH_ALEN]);
#  endif
#else
#  define gether_cleanup() do { } while (0)
#  define gether_setup(gadget, hostaddr)   ((int)0)
#  define gfs_hostaddr NULL
#endif

#include "f_fs.c"


#define DRIVER_NAME	"g_ffs"
#define DRIVER_DESC	"USB Function Filesystem"
#define DRIVER_VERSION	"24 Aug 2004"

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR("Michal Nazarewicz");
MODULE_LICENSE("GPL");


static unsigned short gfs_vendor_id    = 0x1d6b;	/* Linux Foundation */
static unsigned short gfs_product_id   = 0x0105;	/* FunctionFS Gadget */

static struct usb_device_descriptor gfs_dev_desc = {
	.bLength		= sizeof gfs_dev_desc,
	.bDescriptorType	= USB_DT_DEVICE,

	.bcdUSB			= cpu_to_le16(0x0200),
	.bDeviceClass		= USB_CLASS_PER_INTERFACE,

	/* Vendor and product id can be overridden by module parameters.  */
	/* .idVendor		= cpu_to_le16(gfs_vendor_id), */
	/* .idProduct		= cpu_to_le16(gfs_product_id), */
	/* .bcdDevice		= f(hardware) */
	/* .iManufacturer	= DYNAMIC */
	/* .iProduct		= DYNAMIC */
	/* NO SERIAL NUMBER */
	.bNumConfigurations	= 1,
};

#define GFS_MODULE_PARAM_DESC(name, field) \
	MODULE_PARM_DESC(name, "Value of the " #field " field of the device descriptor sent to the host.  Takes effect only prior to the user-space driver registering to the FunctionFS.")

module_param_named(usb_class,    gfs_dev_desc.bDeviceClass,    byte,   0644);
GFS_MODULE_PARAM_DESC(usb_class, bDeviceClass);
module_param_named(usb_subclass, gfs_dev_desc.bDeviceSubClass, byte,   0644);
GFS_MODULE_PARAM_DESC(usb_subclass, bDeviceSubClass);
module_param_named(usb_protocol, gfs_dev_desc.bDeviceProtocol, byte,   0644);
GFS_MODULE_PARAM_DESC(usb_protocol, bDeviceProtocol);
module_param_named(usb_vendor,   gfs_vendor_id,                ushort, 0644);
GFS_MODULE_PARAM_DESC(usb_vendor, idVendor);
module_param_named(usb_product,  gfs_product_id,               ushort, 0644);
GFS_MODULE_PARAM_DESC(usb_product, idProduct);



static const struct usb_descriptor_header *gfs_otg_desc[] = {
	(const struct usb_descriptor_header *)
	&(const struct usb_otg_descriptor) {
		.bLength		= sizeof(struct usb_otg_descriptor),
		.bDescriptorType	= USB_DT_OTG,

		/* REVISIT SRP-only hardware is possible, although
		 * it would not be called "OTG" ... */
		.bmAttributes		= USB_OTG_SRP | USB_OTG_HNP,
	},

	NULL
};

/* string IDs are assigned dynamically */

enum {
	GFS_STRING_MANUFACTURER_IDX,
	GFS_STRING_PRODUCT_IDX,
	GFS_STRING_FIRST_CONFIG_IDX,
};

static       char gfs_manufacturer[50];
static const char gfs_driver_desc[] = DRIVER_DESC;
static const char gfs_short_name[]  = DRIVER_NAME;

static struct usb_string gfs_strings[] = {
	[GFS_STRING_MANUFACTURER_IDX].s = gfs_manufacturer,
	[GFS_STRING_PRODUCT_IDX].s = gfs_driver_desc,
#ifdef CONFIG_USB_FUNCTIONFS_RNDIS
	{ .s = "FunctionFS + RNDIS" },
#endif
#ifdef CONFIG_USB_FUNCTIONFS_ETH
	{ .s = "FunctionFS + ECM" },
#endif
#ifdef CONFIG_USB_FUNCTIONFS_GENERIC
	{ .s = "FunctionFS" },
#endif
	{  } /* end of list */
};

static struct usb_gadget_strings *gfs_dev_strings[] = {
	&(struct usb_gadget_strings) {
		.language	= 0x0409,	/* en-us */
		.strings	= gfs_strings,
	},
	NULL,
};



struct gfs_configuration {
	struct usb_configuration c;
	int (*eth)(struct usb_configuration *c, u8 *ethaddr);
} gfs_configurations[] = {
#ifdef CONFIG_USB_FUNCTIONFS_RNDIS
	{
		.eth		= rndis_bind_config,
	},
#endif

#ifdef CONFIG_USB_FUNCTIONFS_ETH
	{
		.eth		= eth_bind_config,
	},
#endif

#ifdef CONFIG_USB_FUNCTIONFS_GENERIC
	{
	},
#endif
};


static int gfs_bind(struct usb_composite_dev *cdev);
static int gfs_unbind(struct usb_composite_dev *cdev);
static int gfs_do_config(struct usb_configuration *c);

static struct usb_composite_driver gfs_driver = {
	.name		= gfs_short_name,
	.dev		= &gfs_dev_desc,
	.strings	= gfs_dev_strings,
	.bind		= gfs_bind,
	.unbind		= gfs_unbind,
};


static struct ffs_data *gfs_ffs_data;
static unsigned long gfs_registered;


static int  gfs_init(void)
{
	ENTER();

	return functionfs_init();
}
module_init(gfs_init);

static void  gfs_exit(void)
{
	ENTER();

	if (test_and_clear_bit(0, &gfs_registered))
		usb_composite_unregister(&gfs_driver);

	functionfs_cleanup();
}
module_exit(gfs_exit);


static int functionfs_ready_callback(struct ffs_data *ffs)
{
	int ret;

	ENTER();

	if (WARN_ON(test_and_set_bit(0, &gfs_registered)))
		return -EBUSY;

	gfs_ffs_data = ffs;
	ret = usb_composite_register(&gfs_driver);
	if (unlikely(ret < 0))
		clear_bit(0, &gfs_registered);
	return ret;
}

static void functionfs_closed_callback(struct ffs_data *ffs)
{
	ENTER();

	if (test_and_clear_bit(0, &gfs_registered))
		usb_composite_unregister(&gfs_driver);
}


static int functionfs_check_dev_callback(const char *dev_name)
{
	return 0;
}



static int gfs_bind(struct usb_composite_dev *cdev)
{
	int ret, i;

	ENTER();

	if (WARN_ON(!gfs_ffs_data))
		return -ENODEV;

	ret = gether_setup(cdev->gadget, gfs_hostaddr);
	if (unlikely(ret < 0))
		goto error_quick;

	gfs_dev_desc.idVendor  = cpu_to_le16(gfs_vendor_id);
	gfs_dev_desc.idProduct = cpu_to_le16(gfs_product_id);

	snprintf(gfs_manufacturer, sizeof gfs_manufacturer, "%s %s with %s",
		 init_utsname()->sysname, init_utsname()->release,
		 cdev->gadget->name);

	ret = usb_string_ids_tab(cdev, gfs_strings);
	if (unlikely(ret < 0))
		goto error;

	gfs_dev_desc.iManufacturer = gfs_strings[GFS_STRING_MANUFACTURER_IDX].id;
	gfs_dev_desc.iProduct      = gfs_strings[GFS_STRING_PRODUCT_IDX].id;

	ret = functionfs_bind(gfs_ffs_data, cdev);
	if (unlikely(ret < 0))
		goto error;

	for (i = 0; i < ARRAY_SIZE(gfs_configurations); ++i) {
		struct gfs_configuration *c = gfs_configurations + i;

		ret = GFS_STRING_FIRST_CONFIG_IDX + i;
		c->c.label			= gfs_strings[ret].s;
		c->c.iConfiguration		= gfs_strings[ret].id;
		c->c.bind			= gfs_do_config;
		c->c.bConfigurationValue	= 1 + i;
		c->c.bmAttributes		= USB_CONFIG_ATT_SELFPOWER;

		ret = usb_add_config(cdev, &c->c);
		if (unlikely(ret < 0))
			goto error_unbind;
	}

	return 0;

error_unbind:
	functionfs_unbind(gfs_ffs_data);
error:
	gether_cleanup();
error_quick:
	gfs_ffs_data = NULL;
	return ret;
}

static int gfs_unbind(struct usb_composite_dev *cdev)
{
	ENTER();

	/* We may have been called in an error recovery frem
	 * composite_bind() after gfs_unbind() failure so we need to
	 * check if gfs_ffs_data is not NULL since gfs_bind() handles
	 * all error recovery itself.  I'd rather we werent called
	 * from composite on orror recovery, but what you're gonna
	 * do...? */

	if (gfs_ffs_data) {
		gether_cleanup();
		functionfs_unbind(gfs_ffs_data);
		gfs_ffs_data = NULL;
	}

	return 0;
}


static int gfs_do_config(struct usb_configuration *c)
{
	struct gfs_configuration *gc =
		container_of(c, struct gfs_configuration, c);
	int ret;

	if (WARN_ON(!gfs_ffs_data))
		return -ENODEV;

	if (gadget_is_otg(c->cdev->gadget)) {
		c->descriptors = gfs_otg_desc;
		c->bmAttributes |= USB_CONFIG_ATT_WAKEUP;
	}

	if (gc->eth) {
		ret = gc->eth(c, gfs_hostaddr);
		if (unlikely(ret < 0))
			return ret;
	}

	ret = functionfs_bind_config(c->cdev, c, gfs_ffs_data);
	if (unlikely(ret < 0))
		return ret;

	/* After previous do_configs there may be some invalid
	 * pointers in c->interface array.  This happens every time
	 * a user space function with fewer interfaces than a user
	 * space function that was run before the new one is run.  The
	 * compasit's set_config() assumes that if there is no more
	 * then MAX_CONFIG_INTERFACES interfaces in a configuration
	 * then there is a NULL pointer after the last interface in
	 * c->interface array.  We need to make sure this is true. */
	if (c->next_interface_id < ARRAY_SIZE(c->interface))
		c->interface[c->next_interface_id] = NULL;

	return 0;
}


#ifdef CONFIG_USB_FUNCTIONFS_ETH
static int eth_bind_config(struct usb_configuration *c, u8 ethaddr[ETH_ALEN])
{
	return can_support_ecm(c->cdev->gadget)
		? ecm_bind_config(c, ethaddr)
		: geth_bind_config(c, ethaddr);
}
#endif
