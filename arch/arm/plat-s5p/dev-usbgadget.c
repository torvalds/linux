/*
 * Copyright (C) 2010-2011 Samsung Electronics Co., Ltd.
 *
 * S3C series device definition for USB-GADGET
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#ifdef CONFIG_USB_ANDROID
#include <linux/usb/android_composite.h>
#endif

#include <mach/map.h>
#include <plat/devs.h>
#include <plat/usbgadget.h>
#include <plat/usb-phy.h>

#ifdef CONFIG_USB_GADGET
/* USB Device (Gadget)*/
static struct resource s3c_usbgadget_resource[] = {
	[0] = {
		.start = S5P_PA_HSOTG,
		.end   = S5P_PA_HSOTG + SZ_4K - 1,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = IRQ_USB_HSOTG,
		.end   = IRQ_USB_HSOTG,
		.flags = IORESOURCE_IRQ,
	}
};

static u64 s5p_device_usb_gadget_dmamask = 0xffffffffUL;
struct platform_device s3c_device_usbgadget = {
	.name		= "s3c-usbgadget",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(s3c_usbgadget_resource),
	.resource	= s3c_usbgadget_resource,
	.dev		= {
		.dma_mask		= &s5p_device_usb_gadget_dmamask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
};

void __init s5p_usbgadget_set_platdata(struct s5p_usbgadget_platdata *pd)
{
	struct s5p_usbgadget_platdata *npd;

	npd = s3c_set_platdata(pd, sizeof(struct s5p_usbgadget_platdata),
			&s3c_device_usbgadget);

	if (!npd->phy_init)
		npd->phy_init = s5p_usb_phy_init;
	if (!npd->phy_exit)
		npd->phy_exit = s5p_usb_phy_exit;
}
#endif /* CONFIG_USB_GADGET */

#ifdef CONFIG_USB_ANDROID
#define S3C_VENDOR_ID			0x18d1

/* single product ID*/
#define S3C_ADB_PRODUCT_ID		0x4E11
#define S3C_UMS_PRODUCT_ID		0x4E21
#define S3C_RNDIS_PRODUCT_ID		0x4E23
#define S3C_MTP_PRODUCT_ID		0x4E26

/* composite product ID */
#define S3C_UMS_ADB_PRODUCT_ID		0x4E22
#define S3C_UMS_ADB_ACM_PRODUCT_ID	0x4E24
#define S3C_MTP_ADB_PRODUCT_ID		0x4E26

#ifdef	CONFIG_USB_ANDROID_MTP
#define S3C_PRODUCT_ID			S3C_MTP_PRODUCT_ID
#else
#define S3C_PRODUCT_ID			S3C_UMS_PRODUCT_ID
#endif

#define MAX_USB_SERIAL_NUM	17

#ifdef	CONFIG_USB_ANDROID_MASS_STORAGE
static char *usb_functions_ums[] = {
	"usb_mass_storage",
};
#endif

#ifdef	CONFIG_USB_ANDROID_RNDIS
static char *usb_functions_rndis[] = {
	"rndis",
};
#endif

#ifdef	CONFIG_USB_ANDROID_MTP
static char *usb_functions_mtp[] = {
	"mtp",
};
#endif

#ifdef	CONFIG_USB_ANDROID_ADB
static char *usb_functions_adb[] = {
	"adb",
};
#endif

#if defined(CONFIG_USB_ANDROID_MASS_STORAGE) && defined(CONFIG_USB_ANDROID_ADB)
static char *usb_functions_ums_adb[] = {
	"usb_mass_storage",
	"adb",
};
#endif

#if defined(CONFIG_USB_ANDROID_MTP) && defined(CONFIG_USB_ANDROID_ADB)
static char *usb_functions_mtp_adb[] = {
	"mtp",
	"adb",
};
#endif

#if	defined(CONFIG_USB_ANDROID_MASS_STORAGE) && defined(CONFIG_USB_ANDROID_ADB) && defined(CONFIG_USB_ANDROID_ACM)
static char *usb_functions_ums_adb_acm[] = {
	"usb_mass_storage",
	"adb",
	"acm",
};
#endif

static char *usb_functions_all[] = {
#ifdef	CONFIG_USB_ANDROID_RNDIS
	"rndis",
#endif
#ifdef	CONFIG_USB_ANDROID_MASS_STORAGE
	"usb_mass_storage",
#endif
#ifdef	CONFIG_USB_ANDROID_MTP
	"mtp",
#endif
#ifdef	CONFIG_USB_ANDROID_ADB
	"adb",
#endif
#ifdef	CONFIG_USB_ANDROID_ACM
	"acm",
#endif
};

static struct android_usb_product usb_products[] = {
#ifdef	CONFIG_USB_ANDROID_MASS_STORAGE
	{
		.product_id	= S3C_UMS_PRODUCT_ID,
		.num_functions	= ARRAY_SIZE(usb_functions_ums),
		.functions	= usb_functions_ums,
	},
#endif
#ifdef	CONFIG_USB_ANDROID_RNDIS
	{
		.product_id	= S3C_RNDIS_PRODUCT_ID,
		.num_functions	= ARRAY_SIZE(usb_functions_rndis),
		.functions	= usb_functions_rndis,
	},
#endif
#ifdef	CONFIG_USB_ANDROID_MTP
	{
		.product_id	= S3C_MTP_PRODUCT_ID,
		.num_functions	= ARRAY_SIZE(usb_functions_mtp),
		.functions	= usb_functions_mtp,
	},
#endif
#if	defined(CONFIG_USB_ANDROID_MASS_STORAGE) && defined(CONFIG_USB_ANDROID_ADB)
	{
		.product_id	= S3C_UMS_ADB_PRODUCT_ID,
		.num_functions	= ARRAY_SIZE(usb_functions_ums_adb),
		.functions	= usb_functions_ums_adb,
	},
#endif
#if	defined(CONFIG_USB_ANDROID_MTP) && defined(CONFIG_USB_ANDROID_ADB)
	{
		.product_id	= S3C_MTP_ADB_PRODUCT_ID,
		.num_functions	= ARRAY_SIZE(usb_functions_mtp_adb),
		.functions	= usb_functions_mtp_adb,
	},
#endif
#ifdef	CONFIG_USB_ANDROID_ADB
	{
		.product_id	= S3C_ADB_PRODUCT_ID,
		.num_functions	= ARRAY_SIZE(usb_functions_adb),
		.functions	= usb_functions_adb,
	},
#endif
#if defined(CONFIG_USB_ANDROID_MASS_STORAGE) && defined(CONFIG_USB_ANDROID_ADB) && defined(CONFIG_USB_ANDROID_ACM)
	{
		.product_id	= S3C_UMS_ADB_ACM_PRODUCT_ID,
		.num_functions	= ARRAY_SIZE(usb_functions_ums_adb_acm),
		.functions	= usb_functions_ums_adb_acm,
	},
#endif
};

static char device_serial[MAX_USB_SERIAL_NUM] = "0123456789ABCDEF";

/* standard android USB platform data */
static struct android_usb_platform_data android_usb_pdata = {
	.vendor_id		= S3C_VENDOR_ID,
	.product_id		= S3C_PRODUCT_ID,
	.manufacturer_name	= "SAMSUNG",
	.product_name		= "S5P OTG-USB",
	.serial_number		= device_serial,
	.num_products		= ARRAY_SIZE(usb_products),
	.products		= usb_products,
	.num_functions		= ARRAY_SIZE(usb_functions_all),
	.functions		= usb_functions_all,
};

struct platform_device s3c_device_android_usb = {
	.name	= "android_usb",
	.id	= -1,
	.dev	= {
		.platform_data	= &android_usb_pdata,
	},
};

static struct usb_mass_storage_platform_data ums_pdata = {
	.vendor			= "SAMSUNG",
	.product		= "S5P UMS",
	.release		= 1,
	.nluns			= 1,
};
struct platform_device s3c_device_usb_mass_storage = {
	.name	= "usb_mass_storage",
	.id	= -1,
	.dev	= {
		.platform_data = &ums_pdata,
	},
};

#ifdef CONFIG_USB_ANDROID_RNDIS
static struct usb_ether_platform_data rndis_pdata = {
/* ethaddr is filled by board_serialno_setup */
	.vendorID       = S3C_VENDOR_ID,
	.vendorDescr    = "SAMSUNG",
};
struct platform_device s3c_device_rndis = {
	.name   = "rndis",
	.id     = -1,
	.dev    = {
		.platform_data = &rndis_pdata,
	},
};
#endif
#endif /* CONFIG_USB_ANDROID */

