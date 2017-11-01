/*
 *  Common functions for kernel modules using Dell SMBIOS
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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/capability.h>
#include <linux/dmi.h>
#include <linux/err.h>
#include <linux/gfp.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/io.h>
#include "../../firmware/dcdbas.h"
#include "dell-smbios.h"

struct calling_interface_structure {
	struct dmi_header header;
	u16 cmdIOAddress;
	u8 cmdIOCode;
	u32 supportedCmds;
	struct calling_interface_token tokens[];
} __packed;

static struct calling_interface_buffer *buffer;
static DEFINE_MUTEX(buffer_mutex);

static int da_command_address;
static int da_command_code;
static int da_num_tokens;
static struct platform_device *platform_device;
static struct calling_interface_token *da_tokens;
static struct device_attribute *token_location_attrs;
static struct device_attribute *token_value_attrs;
static struct attribute **token_attrs;

int dell_smbios_error(int value)
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
EXPORT_SYMBOL_GPL(dell_smbios_error);

struct calling_interface_buffer *dell_smbios_get_buffer(void)
{
	mutex_lock(&buffer_mutex);
	dell_smbios_clear_buffer();
	return buffer;
}
EXPORT_SYMBOL_GPL(dell_smbios_get_buffer);

void dell_smbios_clear_buffer(void)
{
	memset(buffer, 0, sizeof(struct calling_interface_buffer));
}
EXPORT_SYMBOL_GPL(dell_smbios_clear_buffer);

void dell_smbios_release_buffer(void)
{
	mutex_unlock(&buffer_mutex);
}
EXPORT_SYMBOL_GPL(dell_smbios_release_buffer);

void dell_smbios_send_request(int class, int select)
{
	struct smi_cmd command;

	command.magic = SMI_CMD_MAGIC;
	command.command_address = da_command_address;
	command.command_code = da_command_code;
	command.ebx = virt_to_phys(buffer);
	command.ecx = 0x42534931;

	buffer->cmd_class = class;
	buffer->cmd_select = select;

	dcdbas_smi_request(&command);
}
EXPORT_SYMBOL_GPL(dell_smbios_send_request);

struct calling_interface_token *dell_smbios_find_token(int tokenid)
{
	int i;

	for (i = 0; i < da_num_tokens; i++) {
		if (da_tokens[i].tokenID == tokenid)
			return &da_tokens[i];
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(dell_smbios_find_token);

static BLOCKING_NOTIFIER_HEAD(dell_laptop_chain_head);

int dell_laptop_register_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&dell_laptop_chain_head, nb);
}
EXPORT_SYMBOL_GPL(dell_laptop_register_notifier);

int dell_laptop_unregister_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&dell_laptop_chain_head, nb);
}
EXPORT_SYMBOL_GPL(dell_laptop_unregister_notifier);

void dell_laptop_call_notifier(unsigned long action, void *data)
{
	blocking_notifier_call_chain(&dell_laptop_chain_head, action, data);
}
EXPORT_SYMBOL_GPL(dell_laptop_call_notifier);

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

static void zero_duplicates(struct device *dev)
{
	int i, j;

	for (i = 0; i < da_num_tokens; i++) {
		if (da_tokens[i].tokenID == 0)
			continue;
		for (j = i+1; j < da_num_tokens; j++) {
			if (da_tokens[j].tokenID == 0)
				continue;
			if (da_tokens[i].tokenID == da_tokens[j].tokenID) {
				dev_dbg(dev, "Zeroing dup token ID %x(%x/%x)\n",
					da_tokens[j].tokenID,
					da_tokens[j].location,
					da_tokens[j].value);
				da_tokens[j].tokenID = 0;
			}
		}
	}
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

static int match_attribute(struct device *dev,
			   struct device_attribute *attr)
{
	int i;

	for (i = 0; i < da_num_tokens * 2; i++) {
		if (!token_attrs[i])
			continue;
		if (strcmp(token_attrs[i]->name, attr->attr.name) == 0)
			return i/2;
	}
	dev_dbg(dev, "couldn't match: %s\n", attr->attr.name);
	return -EINVAL;
}

static ssize_t location_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	int i;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	i = match_attribute(dev, attr);
	if (i > 0)
		return scnprintf(buf, PAGE_SIZE, "%08x", da_tokens[i].location);
	return 0;
}

static ssize_t value_show(struct device *dev,
			  struct device_attribute *attr, char *buf)
{
	int i;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	i = match_attribute(dev, attr);
	if (i > 0)
		return scnprintf(buf, PAGE_SIZE, "%08x", da_tokens[i].value);
	return 0;
}

static struct attribute_group smbios_attribute_group = {
	.name = "tokens"
};

static struct platform_driver platform_driver = {
	.driver = {
		.name = "dell-smbios",
	},
};

static int build_tokens_sysfs(struct platform_device *dev)
{
	char buffer_location[13];
	char buffer_value[10];
	char *location_name;
	char *value_name;
	size_t size;
	int ret;
	int i, j;

	/* (number of tokens  + 1 for null terminated */
	size = sizeof(struct device_attribute) * (da_num_tokens + 1);
	token_location_attrs = kzalloc(size, GFP_KERNEL);
	if (!token_location_attrs)
		return -ENOMEM;
	token_value_attrs = kzalloc(size, GFP_KERNEL);
	if (!token_value_attrs)
		goto out_allocate_value;

	/* need to store both location and value + terminator*/
	size = sizeof(struct attribute *) * ((2 * da_num_tokens) + 1);
	token_attrs = kzalloc(size, GFP_KERNEL);
	if (!token_attrs)
		goto out_allocate_attrs;

	for (i = 0, j = 0; i < da_num_tokens; i++) {
		/* skip empty */
		if (da_tokens[i].tokenID == 0)
			continue;
		/* add location */
		sprintf(buffer_location, "%04x_location",
			da_tokens[i].tokenID);
		location_name = kstrdup(buffer_location, GFP_KERNEL);
		if (location_name == NULL)
			goto out_unwind_strings;
		sysfs_attr_init(&token_location_attrs[i].attr);
		token_location_attrs[i].attr.name = location_name;
		token_location_attrs[i].attr.mode = 0444;
		token_location_attrs[i].show = location_show;
		token_attrs[j++] = &token_location_attrs[i].attr;

		/* add value */
		sprintf(buffer_value, "%04x_value",
			da_tokens[i].tokenID);
		value_name = kstrdup(buffer_value, GFP_KERNEL);
		if (value_name == NULL)
			goto loop_fail_create_value;
		sysfs_attr_init(&token_value_attrs[i].attr);
		token_value_attrs[i].attr.name = value_name;
		token_value_attrs[i].attr.mode = 0444;
		token_value_attrs[i].show = value_show;
		token_attrs[j++] = &token_value_attrs[i].attr;
		continue;

loop_fail_create_value:
		kfree(value_name);
		goto out_unwind_strings;
	}
	smbios_attribute_group.attrs = token_attrs;

	ret = sysfs_create_group(&dev->dev.kobj, &smbios_attribute_group);
	if (ret)
		goto out_unwind_strings;
	return 0;

out_unwind_strings:
	for (i = i-1; i > 0; i--) {
		kfree(token_location_attrs[i].attr.name);
		kfree(token_value_attrs[i].attr.name);
	}
	kfree(token_attrs);
out_allocate_attrs:
	kfree(token_value_attrs);
out_allocate_value:
	kfree(token_location_attrs);

	return -ENOMEM;
}

static void free_group(struct platform_device *pdev)
{
	int i;

	sysfs_remove_group(&pdev->dev.kobj,
				&smbios_attribute_group);
	for (i = 0; i < da_num_tokens; i++) {
		kfree(token_location_attrs[i].attr.name);
		kfree(token_value_attrs[i].attr.name);
	}
	kfree(token_attrs);
	kfree(token_value_attrs);
	kfree(token_location_attrs);
}


static int __init dell_smbios_init(void)
{
	const struct dmi_device *valid;
	int ret;

	valid = dmi_find_device(DMI_DEV_TYPE_OEM_STRING, "Dell System", NULL);
	if (!valid) {
		pr_err("Unable to run on non-Dell system\n");
		return -ENODEV;
	}

	dmi_walk(find_tokens, NULL);

	if (!da_tokens)  {
		pr_info("Unable to find dmi tokens\n");
		return -ENODEV;
	}

	/*
	 * Allocate buffer below 4GB for SMI data--only 32-bit physical addr
	 * is passed to SMI handler.
	 */
	buffer = (void *)__get_free_page(GFP_KERNEL | GFP_DMA32);
	if (!buffer) {
		ret = -ENOMEM;
		goto fail_buffer;
	}
	ret = platform_driver_register(&platform_driver);
	if (ret)
		goto fail_platform_driver;

	platform_device = platform_device_alloc("dell-smbios", 0);
	if (!platform_device) {
		ret = -ENOMEM;
		goto fail_platform_device_alloc;
	}
	ret = platform_device_add(platform_device);
	if (ret)
		goto fail_platform_device_add;

	/* duplicate tokens will cause problems building sysfs files */
	zero_duplicates(&platform_device->dev);

	ret = build_tokens_sysfs(platform_device);
	if (ret)
		goto fail_create_group;

	return 0;

fail_create_group:
	platform_device_del(platform_device);

fail_platform_device_add:
	platform_device_put(platform_device);

fail_platform_device_alloc:
	platform_driver_unregister(&platform_driver);

fail_platform_driver:
	free_page((unsigned long)buffer);

fail_buffer:
	kfree(da_tokens);
	return ret;
}

static void __exit dell_smbios_exit(void)
{
	if (platform_device) {
		free_group(platform_device);
		platform_device_unregister(platform_device);
		platform_driver_unregister(&platform_driver);
	}
	free_page((unsigned long)buffer);
	kfree(da_tokens);
}

subsys_initcall(dell_smbios_init);
module_exit(dell_smbios_exit);

MODULE_AUTHOR("Matthew Garrett <mjg@redhat.com>");
MODULE_AUTHOR("Gabriele Mazzotta <gabriele.mzt@gmail.com>");
MODULE_AUTHOR("Pali Rohár <pali.rohar@gmail.com>");
MODULE_DESCRIPTION("Common functions for kernel modules using Dell SMBIOS");
MODULE_LICENSE("GPL");
