// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Generic System Framebuffers on x86
 * Copyright (c) 2012-2013 David Herrmann <dh.herrmann@gmail.com>
 *
 * EFI Quirks Copyright (c) 2006 Edgar Hucek <gimli@dark-green.com>
 */

/*
 * EFI Quirks
 * Several EFI systems do not correctly advertise their boot framebuffers.
 * Hence, we use this static table of known broken machines and fix up the
 * information so framebuffer drivers can load correctly.
 */

#include <linux/dmi.h>
#include <linux/err.h>
#include <linux/efi.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/pci.h>
#include <linux/screen_info.h>
#include <video/vga.h>

#include <asm/efi.h>
#include <asm/sysfb.h>

enum {
	OVERRIDE_NONE = 0x0,
	OVERRIDE_BASE = 0x1,
	OVERRIDE_STRIDE = 0x2,
	OVERRIDE_HEIGHT = 0x4,
	OVERRIDE_WIDTH = 0x8,
};

struct efifb_dmi_info efifb_dmi_list[] = {
	[M_I17] = { "i17", 0x80010000, 1472 * 4, 1440, 900, OVERRIDE_NONE },
	[M_I20] = { "i20", 0x80010000, 1728 * 4, 1680, 1050, OVERRIDE_NONE }, /* guess */
	[M_I20_SR] = { "imac7", 0x40010000, 1728 * 4, 1680, 1050, OVERRIDE_NONE },
	[M_I24] = { "i24", 0x80010000, 2048 * 4, 1920, 1200, OVERRIDE_NONE }, /* guess */
	[M_I24_8_1] = { "imac8", 0xc0060000, 2048 * 4, 1920, 1200, OVERRIDE_NONE },
	[M_I24_10_1] = { "imac10", 0xc0010000, 2048 * 4, 1920, 1080, OVERRIDE_NONE },
	[M_I27_11_1] = { "imac11", 0xc0010000, 2560 * 4, 2560, 1440, OVERRIDE_NONE },
	[M_MINI]= { "mini", 0x80000000, 2048 * 4, 1024, 768, OVERRIDE_NONE },
	[M_MINI_3_1] = { "mini31", 0x40010000, 1024 * 4, 1024, 768, OVERRIDE_NONE },
	[M_MINI_4_1] = { "mini41", 0xc0010000, 2048 * 4, 1920, 1200, OVERRIDE_NONE },
	[M_MB] = { "macbook", 0x80000000, 2048 * 4, 1280, 800, OVERRIDE_NONE },
	[M_MB_5_1] = { "macbook51", 0x80010000, 2048 * 4, 1280, 800, OVERRIDE_NONE },
	[M_MB_6_1] = { "macbook61", 0x80010000, 2048 * 4, 1280, 800, OVERRIDE_NONE },
	[M_MB_7_1] = { "macbook71", 0x80010000, 2048 * 4, 1280, 800, OVERRIDE_NONE },
	[M_MBA] = { "mba", 0x80000000, 2048 * 4, 1280, 800, OVERRIDE_NONE },
	/* 11" Macbook Air 3,1 passes the wrong stride */
	[M_MBA_3] = { "mba3", 0, 2048 * 4, 0, 0, OVERRIDE_STRIDE },
	[M_MBP] = { "mbp", 0x80010000, 1472 * 4, 1440, 900, OVERRIDE_NONE },
	[M_MBP_2] = { "mbp2", 0, 0, 0, 0, OVERRIDE_NONE }, /* placeholder */
	[M_MBP_2_2] = { "mbp22", 0x80010000, 1472 * 4, 1440, 900, OVERRIDE_NONE },
	[M_MBP_SR] = { "mbp3", 0x80030000, 2048 * 4, 1440, 900, OVERRIDE_NONE },
	[M_MBP_4] = { "mbp4", 0xc0060000, 2048 * 4, 1920, 1200, OVERRIDE_NONE },
	[M_MBP_5_1] = { "mbp51", 0xc0010000, 2048 * 4, 1440, 900, OVERRIDE_NONE },
	[M_MBP_5_2] = { "mbp52", 0xc0010000, 2048 * 4, 1920, 1200, OVERRIDE_NONE },
	[M_MBP_5_3] = { "mbp53", 0xd0010000, 2048 * 4, 1440, 900, OVERRIDE_NONE },
	[M_MBP_6_1] = { "mbp61", 0x90030000, 2048 * 4, 1920, 1200, OVERRIDE_NONE },
	[M_MBP_6_2] = { "mbp62", 0x90030000, 2048 * 4, 1680, 1050, OVERRIDE_NONE },
	[M_MBP_7_1] = { "mbp71", 0xc0010000, 2048 * 4, 1280, 800, OVERRIDE_NONE },
	[M_MBP_8_2] = { "mbp82", 0x90010000, 1472 * 4, 1440, 900, OVERRIDE_NONE },
	[M_UNKNOWN] = { NULL, 0, 0, 0, 0, OVERRIDE_NONE }
};

void efifb_setup_from_dmi(struct screen_info *si, const char *opt)
{
	int i;

	for (i = 0; i < M_UNKNOWN; i++) {
		if (efifb_dmi_list[i].base != 0 &&
		    !strcmp(opt, efifb_dmi_list[i].optname)) {
			si->lfb_base = efifb_dmi_list[i].base;
			si->lfb_linelength = efifb_dmi_list[i].stride;
			si->lfb_width = efifb_dmi_list[i].width;
			si->lfb_height = efifb_dmi_list[i].height;
		}
	}
}

#define choose_value(dmivalue, fwvalue, field, flags) ({	\
		typeof(fwvalue) _ret_ = fwvalue;		\
		if ((flags) & (field))				\
			_ret_ = dmivalue;			\
		else if ((fwvalue) == 0)			\
			_ret_ = dmivalue;			\
		_ret_;						\
	})

static int __init efifb_set_system(const struct dmi_system_id *id)
{
	struct efifb_dmi_info *info = id->driver_data;

	if (info->base == 0 && info->height == 0 && info->width == 0 &&
	    info->stride == 0)
		return 0;

	/* Trust the bootloader over the DMI tables */
	if (screen_info.lfb_base == 0) {
#if defined(CONFIG_PCI)
		struct pci_dev *dev = NULL;
		int found_bar = 0;
#endif
		if (info->base) {
			screen_info.lfb_base = choose_value(info->base,
				screen_info.lfb_base, OVERRIDE_BASE,
				info->flags);

#if defined(CONFIG_PCI)
			/* make sure that the address in the table is actually
			 * on a VGA device's PCI BAR */

			for_each_pci_dev(dev) {
				int i;
				if ((dev->class >> 8) != PCI_CLASS_DISPLAY_VGA)
					continue;
				for (i = 0; i < DEVICE_COUNT_RESOURCE; i++) {
					resource_size_t start, end;
					unsigned long flags;

					flags = pci_resource_flags(dev, i);
					if (!(flags & IORESOURCE_MEM))
						continue;

					if (flags & IORESOURCE_UNSET)
						continue;

					if (pci_resource_len(dev, i) == 0)
						continue;

					start = pci_resource_start(dev, i);
					end = pci_resource_end(dev, i);
					if (screen_info.lfb_base >= start &&
					    screen_info.lfb_base < end) {
						found_bar = 1;
						break;
					}
				}
			}
			if (!found_bar)
				screen_info.lfb_base = 0;
#endif
		}
	}
	if (screen_info.lfb_base) {
		screen_info.lfb_linelength = choose_value(info->stride,
			screen_info.lfb_linelength, OVERRIDE_STRIDE,
			info->flags);
		screen_info.lfb_width = choose_value(info->width,
			screen_info.lfb_width, OVERRIDE_WIDTH,
			info->flags);
		screen_info.lfb_height = choose_value(info->height,
			screen_info.lfb_height, OVERRIDE_HEIGHT,
			info->flags);
		if (screen_info.orig_video_isVGA == 0)
			screen_info.orig_video_isVGA = VIDEO_TYPE_EFI;
	} else {
		screen_info.lfb_linelength = 0;
		screen_info.lfb_width = 0;
		screen_info.lfb_height = 0;
		screen_info.orig_video_isVGA = 0;
		return 0;
	}

	printk(KERN_INFO "efifb: dmi detected %s - framebuffer at 0x%08x "
			 "(%dx%d, stride %d)\n", id->ident,
			 screen_info.lfb_base, screen_info.lfb_width,
			 screen_info.lfb_height, screen_info.lfb_linelength);

	return 1;
}

#define EFIFB_DMI_SYSTEM_ID(vendor, name, enumid)		\
	{							\
		efifb_set_system,				\
		name,						\
		{						\
			DMI_MATCH(DMI_BIOS_VENDOR, vendor),	\
			DMI_MATCH(DMI_PRODUCT_NAME, name)	\
		},						\
		&efifb_dmi_list[enumid]				\
	}

static const struct dmi_system_id efifb_dmi_system_table[] __initconst = {
	EFIFB_DMI_SYSTEM_ID("Apple Computer, Inc.", "iMac4,1", M_I17),
	/* At least one of these two will be right; maybe both? */
	EFIFB_DMI_SYSTEM_ID("Apple Computer, Inc.", "iMac5,1", M_I20),
	EFIFB_DMI_SYSTEM_ID("Apple Inc.", "iMac5,1", M_I20),
	/* At least one of these two will be right; maybe both? */
	EFIFB_DMI_SYSTEM_ID("Apple Computer, Inc.", "iMac6,1", M_I24),
	EFIFB_DMI_SYSTEM_ID("Apple Inc.", "iMac6,1", M_I24),
	EFIFB_DMI_SYSTEM_ID("Apple Inc.", "iMac7,1", M_I20_SR),
	EFIFB_DMI_SYSTEM_ID("Apple Inc.", "iMac8,1", M_I24_8_1),
	EFIFB_DMI_SYSTEM_ID("Apple Inc.", "iMac10,1", M_I24_10_1),
	EFIFB_DMI_SYSTEM_ID("Apple Inc.", "iMac11,1", M_I27_11_1),
	EFIFB_DMI_SYSTEM_ID("Apple Computer, Inc.", "Macmini1,1", M_MINI),
	EFIFB_DMI_SYSTEM_ID("Apple Inc.", "Macmini3,1", M_MINI_3_1),
	EFIFB_DMI_SYSTEM_ID("Apple Inc.", "Macmini4,1", M_MINI_4_1),
	EFIFB_DMI_SYSTEM_ID("Apple Computer, Inc.", "MacBook1,1", M_MB),
	/* At least one of these two will be right; maybe both? */
	EFIFB_DMI_SYSTEM_ID("Apple Computer, Inc.", "MacBook2,1", M_MB),
	EFIFB_DMI_SYSTEM_ID("Apple Inc.", "MacBook2,1", M_MB),
	/* At least one of these two will be right; maybe both? */
	EFIFB_DMI_SYSTEM_ID("Apple Computer, Inc.", "MacBook3,1", M_MB),
	EFIFB_DMI_SYSTEM_ID("Apple Inc.", "MacBook3,1", M_MB),
	EFIFB_DMI_SYSTEM_ID("Apple Inc.", "MacBook4,1", M_MB),
	EFIFB_DMI_SYSTEM_ID("Apple Inc.", "MacBook5,1", M_MB_5_1),
	EFIFB_DMI_SYSTEM_ID("Apple Inc.", "MacBook6,1", M_MB_6_1),
	EFIFB_DMI_SYSTEM_ID("Apple Inc.", "MacBook7,1", M_MB_7_1),
	EFIFB_DMI_SYSTEM_ID("Apple Inc.", "MacBookAir1,1", M_MBA),
	EFIFB_DMI_SYSTEM_ID("Apple Inc.", "MacBookAir3,1", M_MBA_3),
	EFIFB_DMI_SYSTEM_ID("Apple Computer, Inc.", "MacBookPro1,1", M_MBP),
	EFIFB_DMI_SYSTEM_ID("Apple Computer, Inc.", "MacBookPro2,1", M_MBP_2),
	EFIFB_DMI_SYSTEM_ID("Apple Computer, Inc.", "MacBookPro2,2", M_MBP_2_2),
	EFIFB_DMI_SYSTEM_ID("Apple Inc.", "MacBookPro2,1", M_MBP_2),
	EFIFB_DMI_SYSTEM_ID("Apple Computer, Inc.", "MacBookPro3,1", M_MBP_SR),
	EFIFB_DMI_SYSTEM_ID("Apple Inc.", "MacBookPro3,1", M_MBP_SR),
	EFIFB_DMI_SYSTEM_ID("Apple Inc.", "MacBookPro4,1", M_MBP_4),
	EFIFB_DMI_SYSTEM_ID("Apple Inc.", "MacBookPro5,1", M_MBP_5_1),
	EFIFB_DMI_SYSTEM_ID("Apple Inc.", "MacBookPro5,2", M_MBP_5_2),
	EFIFB_DMI_SYSTEM_ID("Apple Inc.", "MacBookPro5,3", M_MBP_5_3),
	EFIFB_DMI_SYSTEM_ID("Apple Inc.", "MacBookPro6,1", M_MBP_6_1),
	EFIFB_DMI_SYSTEM_ID("Apple Inc.", "MacBookPro6,2", M_MBP_6_2),
	EFIFB_DMI_SYSTEM_ID("Apple Inc.", "MacBookPro7,1", M_MBP_7_1),
	EFIFB_DMI_SYSTEM_ID("Apple Inc.", "MacBookPro8,2", M_MBP_8_2),
	{},
};

/*
 * Some devices have a portrait LCD but advertise a landscape resolution (and
 * pitch). We simply swap width and height for these devices so that we can
 * correctly deal with some of them coming with multiple resolutions.
 */
static const struct dmi_system_id efifb_dmi_swap_width_height[] __initconst = {
	{
		/*
		 * Lenovo MIIX310-10ICR, only some batches have the troublesome
		 * 800x1280 portrait screen. Luckily the portrait version has
		 * its own BIOS version, so we match on that.
		 */
		.matches = {
			DMI_EXACT_MATCH(DMI_SYS_VENDOR, "LENOVO"),
			DMI_EXACT_MATCH(DMI_PRODUCT_VERSION, "MIIX 310-10ICR"),
			DMI_EXACT_MATCH(DMI_BIOS_VERSION, "1HCN44WW"),
		},
	},
	{
		/* Lenovo MIIX 320-10ICR with 800x1280 portrait screen */
		.matches = {
			DMI_EXACT_MATCH(DMI_SYS_VENDOR, "LENOVO"),
			DMI_EXACT_MATCH(DMI_PRODUCT_VERSION,
					"Lenovo MIIX 320-10ICR"),
		},
	},
	{
		/* Lenovo D330 with 800x1280 or 1200x1920 portrait screen */
		.matches = {
			DMI_EXACT_MATCH(DMI_SYS_VENDOR, "LENOVO"),
			DMI_EXACT_MATCH(DMI_PRODUCT_VERSION,
					"Lenovo ideapad D330-10IGM"),
		},
	},
	{},
};

__init void sysfb_apply_efi_quirks(void)
{
	if (screen_info.orig_video_isVGA != VIDEO_TYPE_EFI ||
	    !(screen_info.capabilities & VIDEO_CAPABILITY_SKIP_QUIRKS))
		dmi_check_system(efifb_dmi_system_table);

	if (screen_info.orig_video_isVGA == VIDEO_TYPE_EFI &&
	    dmi_check_system(efifb_dmi_swap_width_height)) {
		u16 temp = screen_info.lfb_width;

		screen_info.lfb_width = screen_info.lfb_height;
		screen_info.lfb_height = temp;
		screen_info.lfb_linelength = 4 * screen_info.lfb_width;
	}
}
