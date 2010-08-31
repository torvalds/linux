/*
 *  Driver for Dell laptop extras
 *
 *  Copyright (c) Red Hat <mjg@redhat.com>
 *
 *  Based on documentation in the libsmbios package, Copyright (C) 2005 Dell
 *  Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/backlight.h>
#include <linux/err.h>
#include <linux/dmi.h>
#include <linux/io.h>
#include <linux/rfkill.h>
#include <linux/power_supply.h>
#include <linux/acpi.h>
#include <linux/mm.h>
#include <linux/i8042.h>
#include <linux/slab.h>
#include "../../firmware/dcdbas.h"

#define BRIGHTNESS_TOKEN 0x7d

/* This structure will be modified by the firmware when we enter
 * system management mode, hence the volatiles */

struct calling_interface_buffer {
	u16 class;
	u16 select;
	volatile u32 input[4];
	volatile u32 output[4];
} __packed;

struct calling_interface_token {
	u16 tokenID;
	u16 location;
	union {
		u16 value;
		u16 stringlength;
	};
};

struct calling_interface_structure {
	struct dmi_header header;
	u16 cmdIOAddress;
	u8 cmdIOCode;
	u32 supportedCmds;
	struct calling_interface_token tokens[];
} __packed;

static int da_command_address;
static int da_command_code;
static int da_num_tokens;
static struct calling_interface_token *da_tokens;

static struct platform_driver platform_driver = {
	.driver = {
		.name = "dell-laptop",
		.owner = THIS_MODULE,
	}
};

static struct platform_device *platform_device;
static struct backlight_device *dell_backlight_device;
static struct rfkill *wifi_rfkill;
static struct rfkill *bluetooth_rfkill;
static struct rfkill *wwan_rfkill;

static const struct dmi_system_id __initdata dell_device_table[] = {
	{
		.ident = "Dell laptop",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_CHASSIS_TYPE, "8"),
		},
	},
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_CHASSIS_TYPE, "9"), /*Laptop*/
		},
	},
	{
		.ident = "Dell Computer Corporation",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Computer Corporation"),
			DMI_MATCH(DMI_CHASSIS_TYPE, "8"),
		},
	},
	{ }
};

static struct dmi_system_id __devinitdata dell_blacklist[] = {
	/* Supported by compal-laptop */
	{
		.ident = "Dell Mini 9",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Inspiron 910"),
		},
	},
	{
		.ident = "Dell Mini 10",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Inspiron 1010"),
		},
	},
	{
		.ident = "Dell Mini 10v",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Inspiron 1011"),
		},
	},
	{
		.ident = "Dell Mini 1012",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Inspiron 1012"),
		},
	},
	{
		.ident = "Dell Inspiron 11z",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Inspiron 1110"),
		},
	},
	{
		.ident = "Dell Mini 12",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Inspiron 1210"),
		},
	},
	{}
};

static struct calling_interface_buffer *buffer;
static struct page *bufferpage;
static DEFINE_MUTEX(buffer_mutex);

static int hwswitch_state;

static void get_buffer(void)
{
	mutex_lock(&buffer_mutex);
	memset(buffer, 0, sizeof(struct calling_interface_buffer));
}

static void release_buffer(void)
{
	mutex_unlock(&buffer_mutex);
}

static void __init parse_da_table(const struct dmi_header *dm)
{
	/* Final token is a terminator, so we don't want to copy it */
	int tokens = (dm->length-11)/sizeof(struct calling_interface_token)-1;
	struct calling_interface_structure *table =
		container_of(dm, struct calling_interface_structure, header);

	/* 4 bytes of table header, plus 7 bytes of Dell header, plus at least
	   6 bytes of entry */

	if (dm->length < 17)
		return;

	da_command_address = table->cmdIOAddress;
	da_command_code = table->cmdIOCode;

	da_tokens = krealloc(da_tokens, (da_num_tokens + tokens) *
			     sizeof(struct calling_interface_token),
			     GFP_KERNEL);

	if (!da_tokens)
		return;

	memcpy(da_tokens+da_num_tokens, table->tokens,
	       sizeof(struct calling_interface_token) * tokens);

	da_num_tokens += tokens;
}

static void __init find_tokens(const struct dmi_header *dm, void *dummy)
{
	switch (dm->type) {
	case 0xd4: /* Indexed IO */
		break;
	case 0xd5: /* Protected Area Type 1 */
		break;
	case 0xd6: /* Protected Area Type 2 */
		break;
	case 0xda: /* Calling interface */
		parse_da_table(dm);
		break;
	}
}

static int find_token_location(int tokenid)
{
	int i;
	for (i = 0; i < da_num_tokens; i++) {
		if (da_tokens[i].tokenID == tokenid)
			return da_tokens[i].location;
	}

	return -1;
}

static struct calling_interface_buffer *
dell_send_request(struct calling_interface_buffer *buffer, int class,
		  int select)
{
	struct smi_cmd command;

	command.magic = SMI_CMD_MAGIC;
	command.command_address = da_command_address;
	command.command_code = da_command_code;
	command.ebx = virt_to_phys(buffer);
	command.ecx = 0x42534931;

	buffer->class = class;
	buffer->select = select;

	dcdbas_smi_request(&command);

	return buffer;
}

/* Derived from information in DellWirelessCtl.cpp:
   Class 17, select 11 is radio control. It returns an array of 32-bit values.

   Input byte 0 = 0: Wireless information

   result[0]: return code
   result[1]:
     Bit 0:      Hardware switch supported
     Bit 1:      Wifi locator supported
     Bit 2:      Wifi is supported
     Bit 3:      Bluetooth is supported
     Bit 4:      WWAN is supported
     Bit 5:      Wireless keyboard supported
     Bits 6-7:   Reserved
     Bit 8:      Wifi is installed
     Bit 9:      Bluetooth is installed
     Bit 10:     WWAN is installed
     Bits 11-15: Reserved
     Bit 16:     Hardware switch is on
     Bit 17:     Wifi is blocked
     Bit 18:     Bluetooth is blocked
     Bit 19:     WWAN is blocked
     Bits 20-31: Reserved
   result[2]: NVRAM size in bytes
   result[3]: NVRAM format version number

   Input byte 0 = 2: Wireless switch configuration
   result[0]: return code
   result[1]:
     Bit 0:      Wifi controlled by switch
     Bit 1:      Bluetooth controlled by switch
     Bit 2:      WWAN controlled by switch
     Bits 3-6:   Reserved
     Bit 7:      Wireless switch config locked
     Bit 8:      Wifi locator enabled
     Bits 9-14:  Reserved
     Bit 15:     Wifi locator setting locked
     Bits 16-31: Reserved
*/

static int dell_rfkill_set(void *data, bool blocked)
{
	int disable = blocked ? 1 : 0;
	unsigned long radio = (unsigned long)data;
	int hwswitch_bit = (unsigned long)data - 1;
	int ret = 0;

	get_buffer();
	dell_send_request(buffer, 17, 11);

	/* If the hardware switch controls this radio, and the hardware
	   switch is disabled, don't allow changing the software state */
	if ((hwswitch_state & BIT(hwswitch_bit)) &&
	    !(buffer->output[1] & BIT(16))) {
		ret = -EINVAL;
		goto out;
	}

	buffer->input[0] = (1 | (radio<<8) | (disable << 16));
	dell_send_request(buffer, 17, 11);

out:
	release_buffer();
	return ret;
}

static void dell_rfkill_query(struct rfkill *rfkill, void *data)
{
	int status;
	int bit = (unsigned long)data + 16;
	int hwswitch_bit = (unsigned long)data - 1;

	get_buffer();
	dell_send_request(buffer, 17, 11);
	status = buffer->output[1];
	release_buffer();

	rfkill_set_sw_state(rfkill, !!(status & BIT(bit)));

	if (hwswitch_state & (BIT(hwswitch_bit)))
		rfkill_set_hw_state(rfkill, !(status & BIT(16)));
}

static const struct rfkill_ops dell_rfkill_ops = {
	.set_block = dell_rfkill_set,
	.query = dell_rfkill_query,
};

static void dell_update_rfkill(struct work_struct *ignored)
{
	if (wifi_rfkill)
		dell_rfkill_query(wifi_rfkill, (void *)1);
	if (bluetooth_rfkill)
		dell_rfkill_query(bluetooth_rfkill, (void *)2);
	if (wwan_rfkill)
		dell_rfkill_query(wwan_rfkill, (void *)3);
}
static DECLARE_DELAYED_WORK(dell_rfkill_work, dell_update_rfkill);


static int __init dell_setup_rfkill(void)
{
	int status;
	int ret;

	if (dmi_check_system(dell_blacklist)) {
		printk(KERN_INFO "dell-laptop: Blacklisted hardware detected - "
				"not enabling rfkill\n");
		return 0;
	}

	get_buffer();
	dell_send_request(buffer, 17, 11);
	status = buffer->output[1];
	buffer->input[0] = 0x2;
	dell_send_request(buffer, 17, 11);
	hwswitch_state = buffer->output[1];
	release_buffer();

	if ((status & (1<<2|1<<8)) == (1<<2|1<<8)) {
		wifi_rfkill = rfkill_alloc("dell-wifi", &platform_device->dev,
					   RFKILL_TYPE_WLAN,
					   &dell_rfkill_ops, (void *) 1);
		if (!wifi_rfkill) {
			ret = -ENOMEM;
			goto err_wifi;
		}
		ret = rfkill_register(wifi_rfkill);
		if (ret)
			goto err_wifi;
	}

	if ((status & (1<<3|1<<9)) == (1<<3|1<<9)) {
		bluetooth_rfkill = rfkill_alloc("dell-bluetooth",
						&platform_device->dev,
						RFKILL_TYPE_BLUETOOTH,
						&dell_rfkill_ops, (void *) 2);
		if (!bluetooth_rfkill) {
			ret = -ENOMEM;
			goto err_bluetooth;
		}
		ret = rfkill_register(bluetooth_rfkill);
		if (ret)
			goto err_bluetooth;
	}

	if ((status & (1<<4|1<<10)) == (1<<4|1<<10)) {
		wwan_rfkill = rfkill_alloc("dell-wwan",
					   &platform_device->dev,
					   RFKILL_TYPE_WWAN,
					   &dell_rfkill_ops, (void *) 3);
		if (!wwan_rfkill) {
			ret = -ENOMEM;
			goto err_wwan;
		}
		ret = rfkill_register(wwan_rfkill);
		if (ret)
			goto err_wwan;
	}

	return 0;
err_wwan:
	rfkill_destroy(wwan_rfkill);
	if (bluetooth_rfkill)
		rfkill_unregister(bluetooth_rfkill);
err_bluetooth:
	rfkill_destroy(bluetooth_rfkill);
	if (wifi_rfkill)
		rfkill_unregister(wifi_rfkill);
err_wifi:
	rfkill_destroy(wifi_rfkill);

	return ret;
}

static void dell_cleanup_rfkill(void)
{
	if (wifi_rfkill) {
		rfkill_unregister(wifi_rfkill);
		rfkill_destroy(wifi_rfkill);
	}
	if (bluetooth_rfkill) {
		rfkill_unregister(bluetooth_rfkill);
		rfkill_destroy(bluetooth_rfkill);
	}
	if (wwan_rfkill) {
		rfkill_unregister(wwan_rfkill);
		rfkill_destroy(wwan_rfkill);
	}
}

static int dell_send_intensity(struct backlight_device *bd)
{
	int ret = 0;

	get_buffer();
	buffer->input[0] = find_token_location(BRIGHTNESS_TOKEN);
	buffer->input[1] = bd->props.brightness;

	if (buffer->input[0] == -1) {
		ret = -ENODEV;
		goto out;
	}

	if (power_supply_is_system_supplied() > 0)
		dell_send_request(buffer, 1, 2);
	else
		dell_send_request(buffer, 1, 1);

out:
	release_buffer();
	return 0;
}

static int dell_get_intensity(struct backlight_device *bd)
{
	int ret = 0;

	get_buffer();
	buffer->input[0] = find_token_location(BRIGHTNESS_TOKEN);

	if (buffer->input[0] == -1) {
		ret = -ENODEV;
		goto out;
	}

	if (power_supply_is_system_supplied() > 0)
		dell_send_request(buffer, 0, 2);
	else
		dell_send_request(buffer, 0, 1);

out:
	release_buffer();
	if (ret)
		return ret;
	return buffer->output[1];
}

static struct backlight_ops dell_ops = {
	.get_brightness = dell_get_intensity,
	.update_status  = dell_send_intensity,
};

static bool dell_laptop_i8042_filter(unsigned char data, unsigned char str,
			      struct serio *port)
{
	static bool extended;

	if (str & 0x20)
		return false;

	if (unlikely(data == 0xe0)) {
		extended = true;
		return false;
	} else if (unlikely(extended)) {
		switch (data) {
		case 0x8:
			schedule_delayed_work(&dell_rfkill_work,
					      round_jiffies_relative(HZ));
			break;
		}
		extended = false;
	}

	return false;
}

static int __init dell_init(void)
{
	int max_intensity = 0;
	int ret;

	if (!dmi_check_system(dell_device_table))
		return -ENODEV;

	dmi_walk(find_tokens, NULL);

	if (!da_tokens)  {
		printk(KERN_INFO "dell-laptop: Unable to find dmi tokens\n");
		return -ENODEV;
	}

	ret = platform_driver_register(&platform_driver);
	if (ret)
		goto fail_platform_driver;
	platform_device = platform_device_alloc("dell-laptop", -1);
	if (!platform_device) {
		ret = -ENOMEM;
		goto fail_platform_device1;
	}
	ret = platform_device_add(platform_device);
	if (ret)
		goto fail_platform_device2;

	/*
	 * Allocate buffer below 4GB for SMI data--only 32-bit physical addr
	 * is passed to SMI handler.
	 */
	bufferpage = alloc_page(GFP_KERNEL | GFP_DMA32);

	if (!bufferpage)
		goto fail_buffer;
	buffer = page_address(bufferpage);
	mutex_init(&buffer_mutex);

	ret = dell_setup_rfkill();

	if (ret) {
		printk(KERN_WARNING "dell-laptop: Unable to setup rfkill\n");
		goto fail_rfkill;
	}

	ret = i8042_install_filter(dell_laptop_i8042_filter);
	if (ret) {
		printk(KERN_WARNING
		       "dell-laptop: Unable to install key filter\n");
		goto fail_filter;
	}

#ifdef CONFIG_ACPI
	/* In the event of an ACPI backlight being available, don't
	 * register the platform controller.
	 */
	if (acpi_video_backlight_support())
		return 0;
#endif

	get_buffer();
	buffer->input[0] = find_token_location(BRIGHTNESS_TOKEN);
	if (buffer->input[0] != -1) {
		dell_send_request(buffer, 0, 2);
		max_intensity = buffer->output[3];
	}
	release_buffer();

	if (max_intensity) {
		struct backlight_properties props;
		memset(&props, 0, sizeof(struct backlight_properties));
		props.max_brightness = max_intensity;
		dell_backlight_device = backlight_device_register("dell_backlight",
								  &platform_device->dev,
								  NULL,
								  &dell_ops,
								  &props);

		if (IS_ERR(dell_backlight_device)) {
			ret = PTR_ERR(dell_backlight_device);
			dell_backlight_device = NULL;
			goto fail_backlight;
		}

		dell_backlight_device->props.brightness =
			dell_get_intensity(dell_backlight_device);
		backlight_update_status(dell_backlight_device);
	}

	return 0;

fail_backlight:
	i8042_remove_filter(dell_laptop_i8042_filter);
	cancel_delayed_work_sync(&dell_rfkill_work);
fail_filter:
	dell_cleanup_rfkill();
fail_rfkill:
	free_page((unsigned long)bufferpage);
fail_buffer:
	platform_device_del(platform_device);
fail_platform_device2:
	platform_device_put(platform_device);
fail_platform_device1:
	platform_driver_unregister(&platform_driver);
fail_platform_driver:
	kfree(da_tokens);
	return ret;
}

static void __exit dell_exit(void)
{
	i8042_remove_filter(dell_laptop_i8042_filter);
	cancel_delayed_work_sync(&dell_rfkill_work);
	backlight_device_unregister(dell_backlight_device);
	dell_cleanup_rfkill();
	if (platform_device) {
		platform_device_unregister(platform_device);
		platform_driver_unregister(&platform_driver);
	}
	kfree(da_tokens);
	free_page((unsigned long)buffer);
}

module_init(dell_init);
module_exit(dell_exit);

MODULE_AUTHOR("Matthew Garrett <mjg@redhat.com>");
MODULE_DESCRIPTION("Dell laptop driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("dmi:*svnDellInc.:*:ct8:*");
MODULE_ALIAS("dmi:*svnDellInc.:*:ct9:*");
MODULE_ALIAS("dmi:*svnDellComputerCorporation.:*:ct8:*");
