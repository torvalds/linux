/*
 *  Driver for Dell laptop extras
 *
 *  Copyright (c) Red Hat <mjg@redhat.com>
 *  Copyright (c) 2014 Gabriele Mazzotta <gabriele.mzt@gmail.com>
 *  Copyright (c) 2014 Pali Rohár <pali.rohar@gmail.com>
 *
 *  Based on documentation in the libsmbios package:
 *  Copyright (C) 2005-2014 Dell Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

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
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include "../../firmware/dcdbas.h"

#define BRIGHTNESS_TOKEN 0x7d
#define KBD_LED_OFF_TOKEN 0x01E1
#define KBD_LED_ON_TOKEN 0x01E2
#define KBD_LED_AUTO_TOKEN 0x01E3
#define KBD_LED_AUTO_25_TOKEN 0x02EA
#define KBD_LED_AUTO_50_TOKEN 0x02EB
#define KBD_LED_AUTO_75_TOKEN 0x02EC
#define KBD_LED_AUTO_100_TOKEN 0x02F6

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

struct quirk_entry {
	u8 touchpad_led;

	int needs_kbd_timeouts;
	/*
	 * Ordered list of timeouts expressed in seconds.
	 * The list must end with -1
	 */
	int kbd_timeouts[];
};

static struct quirk_entry *quirks;

static struct quirk_entry quirk_dell_vostro_v130 = {
	.touchpad_led = 1,
};

static int __init dmi_matched(const struct dmi_system_id *dmi)
{
	quirks = dmi->driver_data;
	return 1;
}

/*
 * These values come from Windows utility provided by Dell. If any other value
 * is used then BIOS silently set timeout to 0 without any error message.
 */
static struct quirk_entry quirk_dell_xps13_9333 = {
	.needs_kbd_timeouts = 1,
	.kbd_timeouts = { 0, 5, 15, 60, 5 * 60, 15 * 60, -1 },
};

static int da_command_address;
static int da_command_code;
static int da_num_tokens;
static struct calling_interface_token *da_tokens;

static struct platform_driver platform_driver = {
	.driver = {
		.name = "dell-laptop",
	}
};

static struct platform_device *platform_device;
static struct backlight_device *dell_backlight_device;
static struct rfkill *wifi_rfkill;
static struct rfkill *bluetooth_rfkill;
static struct rfkill *wwan_rfkill;
static bool force_rfkill;

module_param(force_rfkill, bool, 0444);
MODULE_PARM_DESC(force_rfkill, "enable rfkill on non whitelisted models");

static const struct dmi_system_id dell_device_table[] __initconst = {
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
MODULE_DEVICE_TABLE(dmi, dell_device_table);

static const struct dmi_system_id dell_quirks[] __initconst = {
	{
		.callback = dmi_matched,
		.ident = "Dell Vostro V130",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Vostro V130"),
		},
		.driver_data = &quirk_dell_vostro_v130,
	},
	{
		.callback = dmi_matched,
		.ident = "Dell Vostro V131",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Vostro V131"),
		},
		.driver_data = &quirk_dell_vostro_v130,
	},
	{
		.callback = dmi_matched,
		.ident = "Dell Vostro 3350",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Vostro 3350"),
		},
		.driver_data = &quirk_dell_vostro_v130,
	},
	{
		.callback = dmi_matched,
		.ident = "Dell Vostro 3555",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Vostro 3555"),
		},
		.driver_data = &quirk_dell_vostro_v130,
	},
	{
		.callback = dmi_matched,
		.ident = "Dell Inspiron N311z",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Inspiron N311z"),
		},
		.driver_data = &quirk_dell_vostro_v130,
	},
	{
		.callback = dmi_matched,
		.ident = "Dell Inspiron M5110",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Inspiron M5110"),
		},
		.driver_data = &quirk_dell_vostro_v130,
	},
	{
		.callback = dmi_matched,
		.ident = "Dell Vostro 3360",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Vostro 3360"),
		},
		.driver_data = &quirk_dell_vostro_v130,
	},
	{
		.callback = dmi_matched,
		.ident = "Dell Vostro 3460",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Vostro 3460"),
		},
		.driver_data = &quirk_dell_vostro_v130,
	},
	{
		.callback = dmi_matched,
		.ident = "Dell Vostro 3560",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Vostro 3560"),
		},
		.driver_data = &quirk_dell_vostro_v130,
	},
	{
		.callback = dmi_matched,
		.ident = "Dell Vostro 3450",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Dell System Vostro 3450"),
		},
		.driver_data = &quirk_dell_vostro_v130,
	},
	{
		.callback = dmi_matched,
		.ident = "Dell Inspiron 5420",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Inspiron 5420"),
		},
		.driver_data = &quirk_dell_vostro_v130,
	},
	{
		.callback = dmi_matched,
		.ident = "Dell Inspiron 5520",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Inspiron 5520"),
		},
		.driver_data = &quirk_dell_vostro_v130,
	},
	{
		.callback = dmi_matched,
		.ident = "Dell Inspiron 5720",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Inspiron 5720"),
		},
		.driver_data = &quirk_dell_vostro_v130,
	},
	{
		.callback = dmi_matched,
		.ident = "Dell Inspiron 7420",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Inspiron 7420"),
		},
		.driver_data = &quirk_dell_vostro_v130,
	},
	{
		.callback = dmi_matched,
		.ident = "Dell Inspiron 7520",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Inspiron 7520"),
		},
		.driver_data = &quirk_dell_vostro_v130,
	},
	{
		.callback = dmi_matched,
		.ident = "Dell Inspiron 7720",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Inspiron 7720"),
		},
		.driver_data = &quirk_dell_vostro_v130,
	},
	{
		.callback = dmi_matched,
		.ident = "Dell XPS13 9333",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "XPS13 9333"),
		},
		.driver_data = &quirk_dell_xps13_9333,
	},
	{ }
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
	struct calling_interface_token *new_da_tokens;
	struct calling_interface_structure *table =
		container_of(dm, struct calling_interface_structure, header);

	/* 4 bytes of table header, plus 7 bytes of Dell header, plus at least
	   6 bytes of entry */

	if (dm->length < 17)
		return;

	da_command_address = table->cmdIOAddress;
	da_command_code = table->cmdIOCode;

	new_da_tokens = krealloc(da_tokens, (da_num_tokens + tokens) *
				 sizeof(struct calling_interface_token),
				 GFP_KERNEL);

	if (!new_da_tokens)
		return;
	da_tokens = new_da_tokens;

	memcpy(da_tokens+da_num_tokens, table->tokens,
	       sizeof(struct calling_interface_token) * tokens);

	da_num_tokens += tokens;
}

static void __init find_tokens(const struct dmi_header *dm, void *dummy)
{
	switch (dm->type) {
	case 0xd4: /* Indexed IO */
	case 0xd5: /* Protected Area Type 1 */
	case 0xd6: /* Protected Area Type 2 */
		break;
	case 0xda: /* Calling interface */
		parse_da_table(dm);
		break;
	}
}

static int find_token_id(int tokenid)
{
	int i;

	for (i = 0; i < da_num_tokens; i++) {
		if (da_tokens[i].tokenID == tokenid)
			return i;
	}

	return -1;
}

static int find_token_location(int tokenid)
{
	int id;

	id = find_token_id(tokenid);
	if (id == -1)
		return -1;

	return da_tokens[id].location;
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

static inline int dell_smi_error(int value)
{
	switch (value) {
	case 0: /* Completed successfully */
		return 0;
	case -1: /* Completed with error */
		return -EIO;
	case -2: /* Function not supported */
		return -ENXIO;
	default: /* Unknown error */
		return -EINVAL;
	}
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

	get_buffer();
	dell_send_request(buffer, 17, 11);

	/* If the hardware switch controls this radio, and the hardware
	   switch is disabled, always disable the radio */
	if ((hwswitch_state & BIT(hwswitch_bit)) &&
	    !(buffer->output[1] & BIT(16)))
		disable = 1;

	buffer->input[0] = (1 | (radio<<8) | (disable << 16));
	dell_send_request(buffer, 17, 11);

	release_buffer();
	return 0;
}

/* Must be called with the buffer held */
static void dell_rfkill_update_sw_state(struct rfkill *rfkill, int radio,
					int status)
{
	if (status & BIT(0)) {
		/* Has hw-switch, sync sw_state to BIOS */
		int block = rfkill_blocked(rfkill);
		buffer->input[0] = (1 | (radio << 8) | (block << 16));
		dell_send_request(buffer, 17, 11);
	} else {
		/* No hw-switch, sync BIOS state to sw_state */
		rfkill_set_sw_state(rfkill, !!(status & BIT(radio + 16)));
	}
}

static void dell_rfkill_update_hw_state(struct rfkill *rfkill, int radio,
					int status)
{
	if (hwswitch_state & (BIT(radio - 1)))
		rfkill_set_hw_state(rfkill, !(status & BIT(16)));
}

static void dell_rfkill_query(struct rfkill *rfkill, void *data)
{
	int status;

	get_buffer();
	dell_send_request(buffer, 17, 11);
	status = buffer->output[1];

	dell_rfkill_update_hw_state(rfkill, (unsigned long)data, status);

	release_buffer();
}

static const struct rfkill_ops dell_rfkill_ops = {
	.set_block = dell_rfkill_set,
	.query = dell_rfkill_query,
};

static struct dentry *dell_laptop_dir;

static int dell_debugfs_show(struct seq_file *s, void *data)
{
	int status;

	get_buffer();
	dell_send_request(buffer, 17, 11);
	status = buffer->output[1];
	release_buffer();

	seq_printf(s, "status:\t0x%X\n", status);
	seq_printf(s, "Bit 0 : Hardware switch supported:   %lu\n",
		   status & BIT(0));
	seq_printf(s, "Bit 1 : Wifi locator supported:      %lu\n",
		  (status & BIT(1)) >> 1);
	seq_printf(s, "Bit 2 : Wifi is supported:           %lu\n",
		  (status & BIT(2)) >> 2);
	seq_printf(s, "Bit 3 : Bluetooth is supported:      %lu\n",
		  (status & BIT(3)) >> 3);
	seq_printf(s, "Bit 4 : WWAN is supported:           %lu\n",
		  (status & BIT(4)) >> 4);
	seq_printf(s, "Bit 5 : Wireless keyboard supported: %lu\n",
		  (status & BIT(5)) >> 5);
	seq_printf(s, "Bit 8 : Wifi is installed:           %lu\n",
		  (status & BIT(8)) >> 8);
	seq_printf(s, "Bit 9 : Bluetooth is installed:      %lu\n",
		  (status & BIT(9)) >> 9);
	seq_printf(s, "Bit 10: WWAN is installed:           %lu\n",
		  (status & BIT(10)) >> 10);
	seq_printf(s, "Bit 16: Hardware switch is on:       %lu\n",
		  (status & BIT(16)) >> 16);
	seq_printf(s, "Bit 17: Wifi is blocked:             %lu\n",
		  (status & BIT(17)) >> 17);
	seq_printf(s, "Bit 18: Bluetooth is blocked:        %lu\n",
		  (status & BIT(18)) >> 18);
	seq_printf(s, "Bit 19: WWAN is blocked:             %lu\n",
		  (status & BIT(19)) >> 19);

	seq_printf(s, "\nhwswitch_state:\t0x%X\n", hwswitch_state);
	seq_printf(s, "Bit 0 : Wifi controlled by switch:      %lu\n",
		   hwswitch_state & BIT(0));
	seq_printf(s, "Bit 1 : Bluetooth controlled by switch: %lu\n",
		   (hwswitch_state & BIT(1)) >> 1);
	seq_printf(s, "Bit 2 : WWAN controlled by switch:      %lu\n",
		   (hwswitch_state & BIT(2)) >> 2);
	seq_printf(s, "Bit 7 : Wireless switch config locked:  %lu\n",
		   (hwswitch_state & BIT(7)) >> 7);
	seq_printf(s, "Bit 8 : Wifi locator enabled:           %lu\n",
		   (hwswitch_state & BIT(8)) >> 8);
	seq_printf(s, "Bit 15: Wifi locator setting locked:    %lu\n",
		   (hwswitch_state & BIT(15)) >> 15);

	return 0;
}

static int dell_debugfs_open(struct inode *inode, struct file *file)
{
	return single_open(file, dell_debugfs_show, inode->i_private);
}

static const struct file_operations dell_debugfs_fops = {
	.owner = THIS_MODULE,
	.open = dell_debugfs_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static void dell_update_rfkill(struct work_struct *ignored)
{
	int status;

	get_buffer();
	dell_send_request(buffer, 17, 11);
	status = buffer->output[1];

	if (wifi_rfkill) {
		dell_rfkill_update_hw_state(wifi_rfkill, 1, status);
		dell_rfkill_update_sw_state(wifi_rfkill, 1, status);
	}
	if (bluetooth_rfkill) {
		dell_rfkill_update_hw_state(bluetooth_rfkill, 2, status);
		dell_rfkill_update_sw_state(bluetooth_rfkill, 2, status);
	}
	if (wwan_rfkill) {
		dell_rfkill_update_hw_state(wwan_rfkill, 3, status);
		dell_rfkill_update_sw_state(wwan_rfkill, 3, status);
	}

	release_buffer();
}
static DECLARE_DELAYED_WORK(dell_rfkill_work, dell_update_rfkill);

static bool dell_laptop_i8042_filter(unsigned char data, unsigned char str,
			      struct serio *port)
{
	static bool extended;

	if (str & I8042_STR_AUXDATA)
		return false;

	if (unlikely(data == 0xe0)) {
		extended = true;
		return false;
	} else if (unlikely(extended)) {
		switch (data) {
		case 0x8:
			schedule_delayed_work(&dell_rfkill_work,
					      round_jiffies_relative(HZ / 4));
			break;
		}
		extended = false;
	}

	return false;
}

static int __init dell_setup_rfkill(void)
{
	int status, ret, whitelisted;
	const char *product;

	/*
	 * rfkill support causes trouble on various models, mostly Inspirons.
	 * So we whitelist certain series, and don't support rfkill on others.
	 */
	whitelisted = 0;
	product = dmi_get_system_info(DMI_PRODUCT_NAME);
	if (product &&  (strncmp(product, "Latitude", 8) == 0 ||
			 strncmp(product, "Precision", 9) == 0))
		whitelisted = 1;
	if (!force_rfkill && !whitelisted)
		return 0;

	get_buffer();
	dell_send_request(buffer, 17, 11);
	status = buffer->output[1];
	buffer->input[0] = 0x2;
	dell_send_request(buffer, 17, 11);
	hwswitch_state = buffer->output[1];
	release_buffer();

	if (!(status & BIT(0))) {
		if (force_rfkill) {
			/* No hwsitch, clear all hw-controlled bits */
			hwswitch_state &= ~7;
		} else {
			/* rfkill is only tested on laptops with a hwswitch */
			return 0;
		}
	}

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

	ret = i8042_install_filter(dell_laptop_i8042_filter);
	if (ret) {
		pr_warn("Unable to install key filter\n");
		goto err_filter;
	}

	return 0;
err_filter:
	if (wwan_rfkill)
		rfkill_unregister(wwan_rfkill);
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
	return ret;
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

	ret = buffer->output[1];

 out:
	release_buffer();
	return ret;
}

static const struct backlight_ops dell_ops = {
	.get_brightness = dell_get_intensity,
	.update_status  = dell_send_intensity,
};

static void touchpad_led_on(void)
{
	int command = 0x97;
	char data = 1;
	i8042_command(&data, command | 1 << 12);
}

static void touchpad_led_off(void)
{
	int command = 0x97;
	char data = 2;
	i8042_command(&data, command | 1 << 12);
}

static void touchpad_led_set(struct led_classdev *led_cdev,
	enum led_brightness value)
{
	if (value > 0)
		touchpad_led_on();
	else
		touchpad_led_off();
}

static struct led_classdev touchpad_led = {
	.name = "dell-laptop::touchpad",
	.brightness_set = touchpad_led_set,
	.flags = LED_CORE_SUSPENDRESUME,
};

static int __init touchpad_led_init(struct device *dev)
{
	return led_classdev_register(dev, &touchpad_led);
}

static void touchpad_led_exit(void)
{
	led_classdev_unregister(&touchpad_led);
}

/*
 * Derived from information in smbios-keyboard-ctl:
 *
 * cbClass 4
 * cbSelect 11
 * Keyboard illumination
 * cbArg1 determines the function to be performed
 *
 * cbArg1 0x0 = Get Feature Information
 *  cbRES1         Standard return codes (0, -1, -2)
 *  cbRES2, word0  Bitmap of user-selectable modes
 *     bit 0     Always off (All systems)
 *     bit 1     Always on (Travis ATG, Siberia)
 *     bit 2     Auto: ALS-based On; ALS-based Off (Travis ATG)
 *     bit 3     Auto: ALS- and input-activity-based On; input-activity based Off
 *     bit 4     Auto: Input-activity-based On; input-activity based Off
 *     bit 5     Auto: Input-activity-based On (illumination level 25%); input-activity based Off
 *     bit 6     Auto: Input-activity-based On (illumination level 50%); input-activity based Off
 *     bit 7     Auto: Input-activity-based On (illumination level 75%); input-activity based Off
 *     bit 8     Auto: Input-activity-based On (illumination level 100%); input-activity based Off
 *     bits 9-15 Reserved for future use
 *  cbRES2, byte2  Reserved for future use
 *  cbRES2, byte3  Keyboard illumination type
 *     0         Reserved
 *     1         Tasklight
 *     2         Backlight
 *     3-255     Reserved for future use
 *  cbRES3, byte0  Supported auto keyboard illumination trigger bitmap.
 *     bit 0     Any keystroke
 *     bit 1     Touchpad activity
 *     bit 2     Pointing stick
 *     bit 3     Any mouse
 *     bits 4-7  Reserved for future use
 *  cbRES3, byte1  Supported timeout unit bitmap
 *     bit 0     Seconds
 *     bit 1     Minutes
 *     bit 2     Hours
 *     bit 3     Days
 *     bits 4-7  Reserved for future use
 *  cbRES3, byte2  Number of keyboard light brightness levels
 *  cbRES4, byte0  Maximum acceptable seconds value (0 if seconds not supported).
 *  cbRES4, byte1  Maximum acceptable minutes value (0 if minutes not supported).
 *  cbRES4, byte2  Maximum acceptable hours value (0 if hours not supported).
 *  cbRES4, byte3  Maximum acceptable days value (0 if days not supported)
 *
 * cbArg1 0x1 = Get Current State
 *  cbRES1         Standard return codes (0, -1, -2)
 *  cbRES2, word0  Bitmap of current mode state
 *     bit 0     Always off (All systems)
 *     bit 1     Always on (Travis ATG, Siberia)
 *     bit 2     Auto: ALS-based On; ALS-based Off (Travis ATG)
 *     bit 3     Auto: ALS- and input-activity-based On; input-activity based Off
 *     bit 4     Auto: Input-activity-based On; input-activity based Off
 *     bit 5     Auto: Input-activity-based On (illumination level 25%); input-activity based Off
 *     bit 6     Auto: Input-activity-based On (illumination level 50%); input-activity based Off
 *     bit 7     Auto: Input-activity-based On (illumination level 75%); input-activity based Off
 *     bit 8     Auto: Input-activity-based On (illumination level 100%); input-activity based Off
 *     bits 9-15 Reserved for future use
 *     Note: Only One bit can be set
 *  cbRES2, byte2  Currently active auto keyboard illumination triggers.
 *     bit 0     Any keystroke
 *     bit 1     Touchpad activity
 *     bit 2     Pointing stick
 *     bit 3     Any mouse
 *     bits 4-7  Reserved for future use
 *  cbRES2, byte3  Current Timeout
 *     bits 7:6  Timeout units indicator:
 *     00b       Seconds
 *     01b       Minutes
 *     10b       Hours
 *     11b       Days
 *     bits 5:0  Timeout value (0-63) in sec/min/hr/day
 *     NOTE: A value of 0 means always on (no timeout) if any bits of RES3 byte
 *     are set upon return from the [Get feature information] call.
 *  cbRES3, byte0  Current setting of ALS value that turns the light on or off.
 *  cbRES3, byte1  Current ALS reading
 *  cbRES3, byte2  Current keyboard light level.
 *
 * cbArg1 0x2 = Set New State
 *  cbRES1         Standard return codes (0, -1, -2)
 *  cbArg2, word0  Bitmap of current mode state
 *     bit 0     Always off (All systems)
 *     bit 1     Always on (Travis ATG, Siberia)
 *     bit 2     Auto: ALS-based On; ALS-based Off (Travis ATG)
 *     bit 3     Auto: ALS- and input-activity-based On; input-activity based Off
 *     bit 4     Auto: Input-activity-based On; input-activity based Off
 *     bit 5     Auto: Input-activity-based On (illumination level 25%); input-activity based Off
 *     bit 6     Auto: Input-activity-based On (illumination level 50%); input-activity based Off
 *     bit 7     Auto: Input-activity-based On (illumination level 75%); input-activity based Off
 *     bit 8     Auto: Input-activity-based On (illumination level 100%); input-activity based Off
 *     bits 9-15 Reserved for future use
 *     Note: Only One bit can be set
 *  cbArg2, byte2  Desired auto keyboard illumination triggers. Must remain inactive to allow
 *                 keyboard to turn off automatically.
 *     bit 0     Any keystroke
 *     bit 1     Touchpad activity
 *     bit 2     Pointing stick
 *     bit 3     Any mouse
 *     bits 4-7  Reserved for future use
 *  cbArg2, byte3  Desired Timeout
 *     bits 7:6  Timeout units indicator:
 *     00b       Seconds
 *     01b       Minutes
 *     10b       Hours
 *     11b       Days
 *     bits 5:0  Timeout value (0-63) in sec/min/hr/day
 *  cbArg3, byte0  Desired setting of ALS value that turns the light on or off.
 *  cbArg3, byte2  Desired keyboard light level.
 */


enum kbd_timeout_unit {
	KBD_TIMEOUT_SECONDS = 0,
	KBD_TIMEOUT_MINUTES,
	KBD_TIMEOUT_HOURS,
	KBD_TIMEOUT_DAYS,
};

enum kbd_mode_bit {
	KBD_MODE_BIT_OFF = 0,
	KBD_MODE_BIT_ON,
	KBD_MODE_BIT_ALS,
	KBD_MODE_BIT_TRIGGER_ALS,
	KBD_MODE_BIT_TRIGGER,
	KBD_MODE_BIT_TRIGGER_25,
	KBD_MODE_BIT_TRIGGER_50,
	KBD_MODE_BIT_TRIGGER_75,
	KBD_MODE_BIT_TRIGGER_100,
};

#define kbd_is_als_mode_bit(bit) \
	((bit) == KBD_MODE_BIT_ALS || (bit) == KBD_MODE_BIT_TRIGGER_ALS)
#define kbd_is_trigger_mode_bit(bit) \
	((bit) >= KBD_MODE_BIT_TRIGGER_ALS && (bit) <= KBD_MODE_BIT_TRIGGER_100)
#define kbd_is_level_mode_bit(bit) \
	((bit) >= KBD_MODE_BIT_TRIGGER_25 && (bit) <= KBD_MODE_BIT_TRIGGER_100)

struct kbd_info {
	u16 modes;
	u8 type;
	u8 triggers;
	u8 levels;
	u8 seconds;
	u8 minutes;
	u8 hours;
	u8 days;
};

struct kbd_state {
	u8 mode_bit;
	u8 triggers;
	u8 timeout_value;
	u8 timeout_unit;
	u8 als_setting;
	u8 als_value;
	u8 level;
};

static const int kbd_tokens[] = {
	KBD_LED_OFF_TOKEN,
	KBD_LED_AUTO_25_TOKEN,
	KBD_LED_AUTO_50_TOKEN,
	KBD_LED_AUTO_75_TOKEN,
	KBD_LED_AUTO_100_TOKEN,
	KBD_LED_ON_TOKEN,
};

static u16 kbd_token_bits;

static struct kbd_info kbd_info;
static bool kbd_als_supported;
static bool kbd_triggers_supported;

static u8 kbd_mode_levels[16];
static int kbd_mode_levels_count;

static u8 kbd_previous_level;
static u8 kbd_previous_mode_bit;

static bool kbd_led_present;

/*
 * NOTE: there are three ways to set the keyboard backlight level.
 * First, via kbd_state.mode_bit (assigning KBD_MODE_BIT_TRIGGER_* value).
 * Second, via kbd_state.level (assigning numerical value <= kbd_info.levels).
 * Third, via SMBIOS tokens (KBD_LED_* in kbd_tokens)
 *
 * There are laptops which support only one of these methods. If we want to
 * support as many machines as possible we need to implement all three methods.
 * The first two methods use the kbd_state structure. The third uses SMBIOS
 * tokens. If kbd_info.levels == 0, the machine does not support setting the
 * keyboard backlight level via kbd_state.level.
 */

static int kbd_get_info(struct kbd_info *info)
{
	u8 units;
	int ret;

	get_buffer();

	buffer->input[0] = 0x0;
	dell_send_request(buffer, 4, 11);
	ret = buffer->output[0];

	if (ret) {
		ret = dell_smi_error(ret);
		goto out;
	}

	info->modes = buffer->output[1] & 0xFFFF;
	info->type = (buffer->output[1] >> 24) & 0xFF;
	info->triggers = buffer->output[2] & 0xFF;
	units = (buffer->output[2] >> 8) & 0xFF;
	info->levels = (buffer->output[2] >> 16) & 0xFF;

	if (units & BIT(0))
		info->seconds = (buffer->output[3] >> 0) & 0xFF;
	if (units & BIT(1))
		info->minutes = (buffer->output[3] >> 8) & 0xFF;
	if (units & BIT(2))
		info->hours = (buffer->output[3] >> 16) & 0xFF;
	if (units & BIT(3))
		info->days = (buffer->output[3] >> 24) & 0xFF;

 out:
	release_buffer();
	return ret;
}

static unsigned int kbd_get_max_level(void)
{
	if (kbd_info.levels != 0)
		return kbd_info.levels;
	if (kbd_mode_levels_count > 0)
		return kbd_mode_levels_count - 1;
	return 0;
}

static int kbd_get_level(struct kbd_state *state)
{
	int i;

	if (kbd_info.levels != 0)
		return state->level;

	if (kbd_mode_levels_count > 0) {
		for (i = 0; i < kbd_mode_levels_count; ++i)
			if (kbd_mode_levels[i] == state->mode_bit)
				return i;
		return 0;
	}

	return -EINVAL;
}

static int kbd_set_level(struct kbd_state *state, u8 level)
{
	if (kbd_info.levels != 0) {
		if (level != 0)
			kbd_previous_level = level;
		if (state->level == level)
			return 0;
		state->level = level;
		if (level != 0 && state->mode_bit == KBD_MODE_BIT_OFF)
			state->mode_bit = kbd_previous_mode_bit;
		else if (level == 0 && state->mode_bit != KBD_MODE_BIT_OFF) {
			kbd_previous_mode_bit = state->mode_bit;
			state->mode_bit = KBD_MODE_BIT_OFF;
		}
		return 0;
	}

	if (kbd_mode_levels_count > 0 && level < kbd_mode_levels_count) {
		if (level != 0)
			kbd_previous_level = level;
		state->mode_bit = kbd_mode_levels[level];
		return 0;
	}

	return -EINVAL;
}

static int kbd_get_state(struct kbd_state *state)
{
	int ret;

	get_buffer();

	buffer->input[0] = 0x1;
	dell_send_request(buffer, 4, 11);
	ret = buffer->output[0];

	if (ret) {
		ret = dell_smi_error(ret);
		goto out;
	}

	state->mode_bit = ffs(buffer->output[1] & 0xFFFF);
	if (state->mode_bit != 0)
		state->mode_bit--;

	state->triggers = (buffer->output[1] >> 16) & 0xFF;
	state->timeout_value = (buffer->output[1] >> 24) & 0x3F;
	state->timeout_unit = (buffer->output[1] >> 30) & 0x3;
	state->als_setting = buffer->output[2] & 0xFF;
	state->als_value = (buffer->output[2] >> 8) & 0xFF;
	state->level = (buffer->output[2] >> 16) & 0xFF;

 out:
	release_buffer();
	return ret;
}

static int kbd_set_state(struct kbd_state *state)
{
	int ret;

	get_buffer();
	buffer->input[0] = 0x2;
	buffer->input[1] = BIT(state->mode_bit) & 0xFFFF;
	buffer->input[1] |= (state->triggers & 0xFF) << 16;
	buffer->input[1] |= (state->timeout_value & 0x3F) << 24;
	buffer->input[1] |= (state->timeout_unit & 0x3) << 30;
	buffer->input[2] = state->als_setting & 0xFF;
	buffer->input[2] |= (state->level & 0xFF) << 16;
	dell_send_request(buffer, 4, 11);
	ret = buffer->output[0];
	release_buffer();

	return dell_smi_error(ret);
}

static int kbd_set_state_safe(struct kbd_state *state, struct kbd_state *old)
{
	int ret;

	ret = kbd_set_state(state);
	if (ret == 0)
		return 0;

	/*
	 * When setting the new state fails,try to restore the previous one.
	 * This is needed on some machines where BIOS sets a default state when
	 * setting a new state fails. This default state could be all off.
	 */

	if (kbd_set_state(old))
		pr_err("Setting old previous keyboard state failed\n");

	return ret;
}

static int kbd_set_token_bit(u8 bit)
{
	int id;
	int ret;

	if (bit >= ARRAY_SIZE(kbd_tokens))
		return -EINVAL;

	id = find_token_id(kbd_tokens[bit]);
	if (id == -1)
		return -EINVAL;

	get_buffer();
	buffer->input[0] = da_tokens[id].location;
	buffer->input[1] = da_tokens[id].value;
	dell_send_request(buffer, 1, 0);
	ret = buffer->output[0];
	release_buffer();

	return dell_smi_error(ret);
}

static int kbd_get_token_bit(u8 bit)
{
	int id;
	int ret;
	int val;

	if (bit >= ARRAY_SIZE(kbd_tokens))
		return -EINVAL;

	id = find_token_id(kbd_tokens[bit]);
	if (id == -1)
		return -EINVAL;

	get_buffer();
	buffer->input[0] = da_tokens[id].location;
	dell_send_request(buffer, 0, 0);
	ret = buffer->output[0];
	val = buffer->output[1];
	release_buffer();

	if (ret)
		return dell_smi_error(ret);

	return (val == da_tokens[id].value);
}

static int kbd_get_first_active_token_bit(void)
{
	int i;
	int ret;

	for (i = 0; i < ARRAY_SIZE(kbd_tokens); ++i) {
		ret = kbd_get_token_bit(i);
		if (ret == 1)
			return i;
	}

	return ret;
}

static int kbd_get_valid_token_counts(void)
{
	return hweight16(kbd_token_bits);
}

static inline int kbd_init_info(void)
{
	struct kbd_state state;
	int ret;
	int i;

	ret = kbd_get_info(&kbd_info);
	if (ret)
		return ret;

	kbd_get_state(&state);

	/* NOTE: timeout value is stored in 6 bits so max value is 63 */
	if (kbd_info.seconds > 63)
		kbd_info.seconds = 63;
	if (kbd_info.minutes > 63)
		kbd_info.minutes = 63;
	if (kbd_info.hours > 63)
		kbd_info.hours = 63;
	if (kbd_info.days > 63)
		kbd_info.days = 63;

	/* NOTE: On tested machines ON mode did not work and caused
	 *       problems (turned backlight off) so do not use it
	 */
	kbd_info.modes &= ~BIT(KBD_MODE_BIT_ON);

	kbd_previous_level = kbd_get_level(&state);
	kbd_previous_mode_bit = state.mode_bit;

	if (kbd_previous_level == 0 && kbd_get_max_level() != 0)
		kbd_previous_level = 1;

	if (kbd_previous_mode_bit == KBD_MODE_BIT_OFF) {
		kbd_previous_mode_bit =
			ffs(kbd_info.modes & ~BIT(KBD_MODE_BIT_OFF));
		if (kbd_previous_mode_bit != 0)
			kbd_previous_mode_bit--;
	}

	if (kbd_info.modes & (BIT(KBD_MODE_BIT_ALS) |
			      BIT(KBD_MODE_BIT_TRIGGER_ALS)))
		kbd_als_supported = true;

	if (kbd_info.modes & (
	    BIT(KBD_MODE_BIT_TRIGGER_ALS) | BIT(KBD_MODE_BIT_TRIGGER) |
	    BIT(KBD_MODE_BIT_TRIGGER_25) | BIT(KBD_MODE_BIT_TRIGGER_50) |
	    BIT(KBD_MODE_BIT_TRIGGER_75) | BIT(KBD_MODE_BIT_TRIGGER_100)
	   ))
		kbd_triggers_supported = true;

	/* kbd_mode_levels[0] is reserved, see below */
	for (i = 0; i < 16; ++i)
		if (kbd_is_level_mode_bit(i) && (BIT(i) & kbd_info.modes))
			kbd_mode_levels[1 + kbd_mode_levels_count++] = i;

	/*
	 * Find the first supported mode and assign to kbd_mode_levels[0].
	 * This should be 0 (off), but we cannot depend on the BIOS to
	 * support 0.
	 */
	if (kbd_mode_levels_count > 0) {
		for (i = 0; i < 16; ++i) {
			if (BIT(i) & kbd_info.modes) {
				kbd_mode_levels[0] = i;
				break;
			}
		}
		kbd_mode_levels_count++;
	}

	return 0;

}

static inline void kbd_init_tokens(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(kbd_tokens); ++i)
		if (find_token_id(kbd_tokens[i]) != -1)
			kbd_token_bits |= BIT(i);
}

static void kbd_init(void)
{
	int ret;

	ret = kbd_init_info();
	kbd_init_tokens();

	if (kbd_token_bits != 0 || ret == 0)
		kbd_led_present = true;
}

static ssize_t kbd_led_timeout_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct kbd_state new_state;
	struct kbd_state state;
	bool convert;
	int value;
	int ret;
	char ch;
	u8 unit;
	int i;

	ret = sscanf(buf, "%d %c", &value, &ch);
	if (ret < 1)
		return -EINVAL;
	else if (ret == 1)
		ch = 's';

	if (value < 0)
		return -EINVAL;

	convert = false;

	switch (ch) {
	case 's':
		if (value > kbd_info.seconds)
			convert = true;
		unit = KBD_TIMEOUT_SECONDS;
		break;
	case 'm':
		if (value > kbd_info.minutes)
			convert = true;
		unit = KBD_TIMEOUT_MINUTES;
		break;
	case 'h':
		if (value > kbd_info.hours)
			convert = true;
		unit = KBD_TIMEOUT_HOURS;
		break;
	case 'd':
		if (value > kbd_info.days)
			convert = true;
		unit = KBD_TIMEOUT_DAYS;
		break;
	default:
		return -EINVAL;
	}

	if (quirks && quirks->needs_kbd_timeouts)
		convert = true;

	if (convert) {
		/* Convert value from current units to seconds */
		switch (unit) {
		case KBD_TIMEOUT_DAYS:
			value *= 24;
		case KBD_TIMEOUT_HOURS:
			value *= 60;
		case KBD_TIMEOUT_MINUTES:
			value *= 60;
			unit = KBD_TIMEOUT_SECONDS;
		}

		if (quirks && quirks->needs_kbd_timeouts) {
			for (i = 0; quirks->kbd_timeouts[i] != -1; i++) {
				if (value <= quirks->kbd_timeouts[i]) {
					value = quirks->kbd_timeouts[i];
					break;
				}
			}
		}

		if (value <= kbd_info.seconds && kbd_info.seconds) {
			unit = KBD_TIMEOUT_SECONDS;
		} else if (value / 60 <= kbd_info.minutes && kbd_info.minutes) {
			value /= 60;
			unit = KBD_TIMEOUT_MINUTES;
		} else if (value / (60 * 60) <= kbd_info.hours && kbd_info.hours) {
			value /= (60 * 60);
			unit = KBD_TIMEOUT_HOURS;
		} else if (value / (60 * 60 * 24) <= kbd_info.days && kbd_info.days) {
			value /= (60 * 60 * 24);
			unit = KBD_TIMEOUT_DAYS;
		} else {
			return -EINVAL;
		}
	}

	ret = kbd_get_state(&state);
	if (ret)
		return ret;

	new_state = state;
	new_state.timeout_value = value;
	new_state.timeout_unit = unit;

	ret = kbd_set_state_safe(&new_state, &state);
	if (ret)
		return ret;

	return count;
}

static ssize_t kbd_led_timeout_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct kbd_state state;
	int ret;
	int len;

	ret = kbd_get_state(&state);
	if (ret)
		return ret;

	len = sprintf(buf, "%d", state.timeout_value);

	switch (state.timeout_unit) {
	case KBD_TIMEOUT_SECONDS:
		return len + sprintf(buf+len, "s\n");
	case KBD_TIMEOUT_MINUTES:
		return len + sprintf(buf+len, "m\n");
	case KBD_TIMEOUT_HOURS:
		return len + sprintf(buf+len, "h\n");
	case KBD_TIMEOUT_DAYS:
		return len + sprintf(buf+len, "d\n");
	default:
		return -EINVAL;
	}

	return len;
}

static DEVICE_ATTR(stop_timeout, S_IRUGO | S_IWUSR,
		   kbd_led_timeout_show, kbd_led_timeout_store);

static const char * const kbd_led_triggers[] = {
	"keyboard",
	"touchpad",
	/*"trackstick"*/ NULL, /* NOTE: trackstick is just alias for touchpad */
	"mouse",
};

static ssize_t kbd_led_triggers_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct kbd_state new_state;
	struct kbd_state state;
	bool triggers_enabled = false;
	bool als_enabled = false;
	bool disable_als = false;
	bool enable_als = false;
	int trigger_bit = -1;
	char trigger[21];
	int i, ret;

	ret = sscanf(buf, "%20s", trigger);
	if (ret != 1)
		return -EINVAL;

	if (trigger[0] != '+' && trigger[0] != '-')
		return -EINVAL;

	ret = kbd_get_state(&state);
	if (ret)
		return ret;

	if (kbd_als_supported)
		als_enabled = kbd_is_als_mode_bit(state.mode_bit);

	if (kbd_triggers_supported)
		triggers_enabled = kbd_is_trigger_mode_bit(state.mode_bit);

	if (kbd_als_supported) {
		if (strcmp(trigger, "+als") == 0) {
			if (als_enabled)
				return count;
			enable_als = true;
		} else if (strcmp(trigger, "-als") == 0) {
			if (!als_enabled)
				return count;
			disable_als = true;
		}
	}

	if (enable_als || disable_als) {
		new_state = state;
		if (enable_als) {
			if (triggers_enabled)
				new_state.mode_bit = KBD_MODE_BIT_TRIGGER_ALS;
			else
				new_state.mode_bit = KBD_MODE_BIT_ALS;
		} else {
			if (triggers_enabled) {
				new_state.mode_bit = KBD_MODE_BIT_TRIGGER;
				kbd_set_level(&new_state, kbd_previous_level);
			} else {
				new_state.mode_bit = KBD_MODE_BIT_ON;
			}
		}
		if (!(kbd_info.modes & BIT(new_state.mode_bit)))
			return -EINVAL;
		ret = kbd_set_state_safe(&new_state, &state);
		if (ret)
			return ret;
		kbd_previous_mode_bit = new_state.mode_bit;
		return count;
	}

	if (kbd_triggers_supported) {
		for (i = 0; i < ARRAY_SIZE(kbd_led_triggers); ++i) {
			if (!(kbd_info.triggers & BIT(i)))
				continue;
			if (!kbd_led_triggers[i])
				continue;
			if (strcmp(trigger+1, kbd_led_triggers[i]) != 0)
				continue;
			if (trigger[0] == '+' &&
			    triggers_enabled && (state.triggers & BIT(i)))
				return count;
			if (trigger[0] == '-' &&
			    (!triggers_enabled || !(state.triggers & BIT(i))))
				return count;
			trigger_bit = i;
			break;
		}
	}

	if (trigger_bit != -1) {
		new_state = state;
		if (trigger[0] == '+')
			new_state.triggers |= BIT(trigger_bit);
		else {
			new_state.triggers &= ~BIT(trigger_bit);
			/* NOTE: trackstick bit (2) must be disabled when
			 *       disabling touchpad bit (1), otherwise touchpad
			 *       bit (1) will not be disabled */
			if (trigger_bit == 1)
				new_state.triggers &= ~BIT(2);
		}
		if ((kbd_info.triggers & new_state.triggers) !=
		    new_state.triggers)
			return -EINVAL;
		if (new_state.triggers && !triggers_enabled) {
			if (als_enabled)
				new_state.mode_bit = KBD_MODE_BIT_TRIGGER_ALS;
			else {
				new_state.mode_bit = KBD_MODE_BIT_TRIGGER;
				kbd_set_level(&new_state, kbd_previous_level);
			}
		} else if (new_state.triggers == 0) {
			if (als_enabled)
				new_state.mode_bit = KBD_MODE_BIT_ALS;
			else
				kbd_set_level(&new_state, 0);
		}
		if (!(kbd_info.modes & BIT(new_state.mode_bit)))
			return -EINVAL;
		ret = kbd_set_state_safe(&new_state, &state);
		if (ret)
			return ret;
		if (new_state.mode_bit != KBD_MODE_BIT_OFF)
			kbd_previous_mode_bit = new_state.mode_bit;
		return count;
	}

	return -EINVAL;
}

static ssize_t kbd_led_triggers_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct kbd_state state;
	bool triggers_enabled;
	int level, i, ret;
	int len = 0;

	ret = kbd_get_state(&state);
	if (ret)
		return ret;

	len = 0;

	if (kbd_triggers_supported) {
		triggers_enabled = kbd_is_trigger_mode_bit(state.mode_bit);
		level = kbd_get_level(&state);
		for (i = 0; i < ARRAY_SIZE(kbd_led_triggers); ++i) {
			if (!(kbd_info.triggers & BIT(i)))
				continue;
			if (!kbd_led_triggers[i])
				continue;
			if ((triggers_enabled || level <= 0) &&
			    (state.triggers & BIT(i)))
				buf[len++] = '+';
			else
				buf[len++] = '-';
			len += sprintf(buf+len, "%s ", kbd_led_triggers[i]);
		}
	}

	if (kbd_als_supported) {
		if (kbd_is_als_mode_bit(state.mode_bit))
			len += sprintf(buf+len, "+als ");
		else
			len += sprintf(buf+len, "-als ");
	}

	if (len)
		buf[len - 1] = '\n';

	return len;
}

static DEVICE_ATTR(start_triggers, S_IRUGO | S_IWUSR,
		   kbd_led_triggers_show, kbd_led_triggers_store);

static ssize_t kbd_led_als_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct kbd_state state;
	struct kbd_state new_state;
	u8 setting;
	int ret;

	ret = kstrtou8(buf, 10, &setting);
	if (ret)
		return ret;

	ret = kbd_get_state(&state);
	if (ret)
		return ret;

	new_state = state;
	new_state.als_setting = setting;

	ret = kbd_set_state_safe(&new_state, &state);
	if (ret)
		return ret;

	return count;
}

static ssize_t kbd_led_als_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct kbd_state state;
	int ret;

	ret = kbd_get_state(&state);
	if (ret)
		return ret;

	return sprintf(buf, "%d\n", state.als_setting);
}

static DEVICE_ATTR(als_setting, S_IRUGO | S_IWUSR,
		   kbd_led_als_show, kbd_led_als_store);

static struct attribute *kbd_led_attrs[] = {
	&dev_attr_stop_timeout.attr,
	&dev_attr_start_triggers.attr,
	&dev_attr_als_setting.attr,
	NULL,
};
ATTRIBUTE_GROUPS(kbd_led);

static enum led_brightness kbd_led_level_get(struct led_classdev *led_cdev)
{
	int ret;
	u16 num;
	struct kbd_state state;

	if (kbd_get_max_level()) {
		ret = kbd_get_state(&state);
		if (ret)
			return 0;
		ret = kbd_get_level(&state);
		if (ret < 0)
			return 0;
		return ret;
	}

	if (kbd_get_valid_token_counts()) {
		ret = kbd_get_first_active_token_bit();
		if (ret < 0)
			return 0;
		for (num = kbd_token_bits; num != 0 && ret > 0; --ret)
			num &= num - 1; /* clear the first bit set */
		if (num == 0)
			return 0;
		return ffs(num) - 1;
	}

	pr_warn("Keyboard brightness level control not supported\n");
	return 0;
}

static void kbd_led_level_set(struct led_classdev *led_cdev,
			      enum led_brightness value)
{
	struct kbd_state state;
	struct kbd_state new_state;
	u16 num;

	if (kbd_get_max_level()) {
		if (kbd_get_state(&state))
			return;
		new_state = state;
		if (kbd_set_level(&new_state, value))
			return;
		kbd_set_state_safe(&new_state, &state);
		return;
	}

	if (kbd_get_valid_token_counts()) {
		for (num = kbd_token_bits; num != 0 && value > 0; --value)
			num &= num - 1; /* clear the first bit set */
		if (num == 0)
			return;
		kbd_set_token_bit(ffs(num) - 1);
		return;
	}

	pr_warn("Keyboard brightness level control not supported\n");
}

static struct led_classdev kbd_led = {
	.name           = "dell::kbd_backlight",
	.brightness_set = kbd_led_level_set,
	.brightness_get = kbd_led_level_get,
	.groups         = kbd_led_groups,
};

static int __init kbd_led_init(struct device *dev)
{
	kbd_init();
	if (!kbd_led_present)
		return -ENODEV;
	kbd_led.max_brightness = kbd_get_max_level();
	if (!kbd_led.max_brightness) {
		kbd_led.max_brightness = kbd_get_valid_token_counts();
		if (kbd_led.max_brightness)
			kbd_led.max_brightness--;
	}
	return led_classdev_register(dev, &kbd_led);
}

static void brightness_set_exit(struct led_classdev *led_cdev,
				enum led_brightness value)
{
	/* Don't change backlight level on exit */
};

static void kbd_led_exit(void)
{
	if (!kbd_led_present)
		return;
	kbd_led.brightness_set = brightness_set_exit;
	led_classdev_unregister(&kbd_led);
}

static int __init dell_init(void)
{
	int max_intensity = 0;
	int ret;

	if (!dmi_check_system(dell_device_table))
		return -ENODEV;

	quirks = NULL;
	/* find if this machine support other functions */
	dmi_check_system(dell_quirks);

	dmi_walk(find_tokens, NULL);

	if (!da_tokens)  {
		pr_info("Unable to find dmi tokens\n");
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
	if (!bufferpage) {
		ret = -ENOMEM;
		goto fail_buffer;
	}
	buffer = page_address(bufferpage);

	ret = dell_setup_rfkill();

	if (ret) {
		pr_warn("Unable to setup rfkill\n");
		goto fail_rfkill;
	}

	if (quirks && quirks->touchpad_led)
		touchpad_led_init(&platform_device->dev);

	kbd_led_init(&platform_device->dev);

	dell_laptop_dir = debugfs_create_dir("dell_laptop", NULL);
	if (dell_laptop_dir != NULL)
		debugfs_create_file("rfkill", 0444, dell_laptop_dir, NULL,
				    &dell_debugfs_fops);

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
		props.type = BACKLIGHT_PLATFORM;
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
	debugfs_remove_recursive(dell_laptop_dir);
	if (quirks && quirks->touchpad_led)
		touchpad_led_exit();
	kbd_led_exit();
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
MODULE_AUTHOR("Gabriele Mazzotta <gabriele.mzt@gmail.com>");
MODULE_AUTHOR("Pali Rohár <pali.rohar@gmail.com>");
MODULE_DESCRIPTION("Dell laptop driver");
MODULE_LICENSE("GPL");
