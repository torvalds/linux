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
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include "dell-smbios.h"

static u32 da_supported_commands;
static int da_num_tokens;
static struct platform_device *platform_device;
static struct calling_interface_token *da_tokens;
static struct device_attribute *token_location_attrs;
static struct device_attribute *token_value_attrs;
static struct attribute **token_attrs;
static DEFINE_MUTEX(smbios_mutex);

struct smbios_device {
	struct list_head list;
	struct device *device;
	int (*call_fn)(struct calling_interface_buffer *arg);
};

struct smbios_call {
	u32 need_capability;
	int cmd_class;
	int cmd_select;
};

/* calls that are whitelisted for given capabilities */
static struct smbios_call call_whitelist[] = {
	/* generally tokens are allowed, but may be further filtered or
	 * restricted by token blacklist or whitelist
	 */
	{CAP_SYS_ADMIN,	CLASS_TOKEN_READ,	SELECT_TOKEN_STD},
	{CAP_SYS_ADMIN,	CLASS_TOKEN_READ,	SELECT_TOKEN_AC},
	{CAP_SYS_ADMIN,	CLASS_TOKEN_READ,	SELECT_TOKEN_BAT},
	{CAP_SYS_ADMIN,	CLASS_TOKEN_WRITE,	SELECT_TOKEN_STD},
	{CAP_SYS_ADMIN,	CLASS_TOKEN_WRITE,	SELECT_TOKEN_AC},
	{CAP_SYS_ADMIN,	CLASS_TOKEN_WRITE,	SELECT_TOKEN_BAT},
	/* used by userspace: fwupdate */
	{CAP_SYS_ADMIN, CLASS_ADMIN_PROP,	SELECT_ADMIN_PROP},
	/* used by userspace: fwupd */
	{CAP_SYS_ADMIN,	CLASS_INFO,		SELECT_DOCK},
	{CAP_SYS_ADMIN,	CLASS_FLASH_INTERFACE,	SELECT_FLASH_INTERFACE},
};

/* calls that are explicitly blacklisted */
static struct smbios_call call_blacklist[] = {
	{0x0000,  1,  7}, /* manufacturing use */
	{0x0000,  6,  5}, /* manufacturing use */
	{0x0000, 11,  3}, /* write once */
	{0x0000, 11,  7}, /* write once */
	{0x0000, 11, 11}, /* write once */
	{0x0000, 19, -1}, /* diagnostics */
	/* handled by kernel: dell-laptop */
	{0x0000, CLASS_INFO, SELECT_RFKILL},
	{0x0000, CLASS_KBD_BACKLIGHT, SELECT_KBD_BACKLIGHT},
};

struct token_range {
	u32 need_capability;
	u16 min;
	u16 max;
};

/* tokens that are whitelisted for given capabilities */
static struct token_range token_whitelist[] = {
	/* used by userspace: fwupdate */
	{CAP_SYS_ADMIN,	CAPSULE_EN_TOKEN,	CAPSULE_DIS_TOKEN},
	/* can indicate to userspace that WMI is needed */
	{0x0000,	WSMT_EN_TOKEN,		WSMT_DIS_TOKEN}
};

/* tokens that are explicitly blacklisted */
static struct token_range token_blacklist[] = {
	{0x0000, 0x0058, 0x0059}, /* ME use */
	{0x0000, 0x00CD, 0x00D0}, /* raid shadow copy */
	{0x0000, 0x013A, 0x01FF}, /* sata shadow copy */
	{0x0000, 0x0175, 0x0176}, /* write once */
	{0x0000, 0x0195, 0x0197}, /* diagnostics */
	{0x0000, 0x01DC, 0x01DD}, /* manufacturing use */
	{0x0000, 0x027D, 0x0284}, /* diagnostics */
	{0x0000, 0x02E3, 0x02E3}, /* manufacturing use */
	{0x0000, 0x02FF, 0x02FF}, /* manufacturing use */
	{0x0000, 0x0300, 0x0302}, /* manufacturing use */
	{0x0000, 0x0325, 0x0326}, /* manufacturing use */
	{0x0000, 0x0332, 0x0335}, /* fan control */
	{0x0000, 0x0350, 0x0350}, /* manufacturing use */
	{0x0000, 0x0363, 0x0363}, /* manufacturing use */
	{0x0000, 0x0368, 0x0368}, /* manufacturing use */
	{0x0000, 0x03F6, 0x03F7}, /* manufacturing use */
	{0x0000, 0x049E, 0x049F}, /* manufacturing use */
	{0x0000, 0x04A0, 0x04A3}, /* disagnostics */
	{0x0000, 0x04E6, 0x04E7}, /* manufacturing use */
	{0x0000, 0x4000, 0x7FFF}, /* internal BIOS use */
	{0x0000, 0x9000, 0x9001}, /* internal BIOS use */
	{0x0000, 0xA000, 0xBFFF}, /* write only */
	{0x0000, 0xEFF0, 0xEFFF}, /* internal BIOS use */
	/* handled by kernel: dell-laptop */
	{0x0000, BRIGHTNESS_TOKEN,	BRIGHTNESS_TOKEN},
	{0x0000, KBD_LED_OFF_TOKEN,	KBD_LED_AUTO_TOKEN},
	{0x0000, KBD_LED_AC_TOKEN,	KBD_LED_AC_TOKEN},
	{0x0000, KBD_LED_AUTO_25_TOKEN,	KBD_LED_AUTO_75_TOKEN},
	{0x0000, KBD_LED_AUTO_100_TOKEN,	KBD_LED_AUTO_100_TOKEN},
	{0x0000, GLOBAL_MIC_MUTE_ENABLE,	GLOBAL_MIC_MUTE_DISABLE},
};

static LIST_HEAD(smbios_device_list);

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

int dell_smbios_register_device(struct device *d, void *call_fn)
{
	struct smbios_device *priv;

	priv = devm_kzalloc(d, sizeof(struct smbios_device), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	get_device(d);
	priv->device = d;
	priv->call_fn = call_fn;
	mutex_lock(&smbios_mutex);
	list_add_tail(&priv->list, &smbios_device_list);
	mutex_unlock(&smbios_mutex);
	dev_dbg(d, "Added device: %s\n", d->driver->name);
	return 0;
}
EXPORT_SYMBOL_GPL(dell_smbios_register_device);

void dell_smbios_unregister_device(struct device *d)
{
	struct smbios_device *priv;

	mutex_lock(&smbios_mutex);
	list_for_each_entry(priv, &smbios_device_list, list) {
		if (priv->device == d) {
			list_del(&priv->list);
			put_device(d);
			break;
		}
	}
	mutex_unlock(&smbios_mutex);
	dev_dbg(d, "Remove device: %s\n", d->driver->name);
}
EXPORT_SYMBOL_GPL(dell_smbios_unregister_device);

int dell_smbios_call_filter(struct device *d,
			    struct calling_interface_buffer *buffer)
{
	u16 t = 0;
	int i;

	/* can't make calls over 30 */
	if (buffer->cmd_class > 30) {
		dev_dbg(d, "class too big: %u\n", buffer->cmd_class);
		return -EINVAL;
	}

	/* supported calls on the particular system */
	if (!(da_supported_commands & (1 << buffer->cmd_class))) {
		dev_dbg(d, "invalid command, supported commands: 0x%8x\n",
			da_supported_commands);
		return -EINVAL;
	}

	/* match against call blacklist  */
	for (i = 0; i < ARRAY_SIZE(call_blacklist); i++) {
		if (buffer->cmd_class != call_blacklist[i].cmd_class)
			continue;
		if (buffer->cmd_select != call_blacklist[i].cmd_select &&
		    call_blacklist[i].cmd_select != -1)
			continue;
		dev_dbg(d, "blacklisted command: %u/%u\n",
			buffer->cmd_class, buffer->cmd_select);
		return -EINVAL;
	}

	/* if a token call, find token ID */

	if ((buffer->cmd_class == CLASS_TOKEN_READ ||
	     buffer->cmd_class == CLASS_TOKEN_WRITE) &&
	     buffer->cmd_select < 3) {
		/* find the matching token ID */
		for (i = 0; i < da_num_tokens; i++) {
			if (da_tokens[i].location != buffer->input[0])
				continue;
			t = da_tokens[i].tokenID;
			break;
		}

		/* token call; but token didn't exist */
		if (!t) {
			dev_dbg(d, "token at location %04x doesn't exist\n",
				buffer->input[0]);
			return -EINVAL;
		}

		/* match against token blacklist */
		for (i = 0; i < ARRAY_SIZE(token_blacklist); i++) {
			if (!token_blacklist[i].min || !token_blacklist[i].max)
				continue;
			if (t >= token_blacklist[i].min &&
			    t <= token_blacklist[i].max)
				return -EINVAL;
		}

		/* match against token whitelist */
		for (i = 0; i < ARRAY_SIZE(token_whitelist); i++) {
			if (!token_whitelist[i].min || !token_whitelist[i].max)
				continue;
			if (t < token_whitelist[i].min ||
			    t > token_whitelist[i].max)
				continue;
			if (!token_whitelist[i].need_capability ||
			    capable(token_whitelist[i].need_capability)) {
				dev_dbg(d, "whitelisted token: %x\n", t);
				return 0;
			}

		}
	}
	/* match against call whitelist */
	for (i = 0; i < ARRAY_SIZE(call_whitelist); i++) {
		if (buffer->cmd_class != call_whitelist[i].cmd_class)
			continue;
		if (buffer->cmd_select != call_whitelist[i].cmd_select)
			continue;
		if (!call_whitelist[i].need_capability ||
		    capable(call_whitelist[i].need_capability)) {
			dev_dbg(d, "whitelisted capable command: %u/%u\n",
			buffer->cmd_class, buffer->cmd_select);
			return 0;
		}
		dev_dbg(d, "missing capability %d for %u/%u\n",
			call_whitelist[i].need_capability,
			buffer->cmd_class, buffer->cmd_select);

	}

	/* not in a whitelist, only allow processes with capabilities */
	if (capable(CAP_SYS_RAWIO)) {
		dev_dbg(d, "Allowing %u/%u due to CAP_SYS_RAWIO\n",
			buffer->cmd_class, buffer->cmd_select);
		return 0;
	}

	return -EACCES;
}
EXPORT_SYMBOL_GPL(dell_smbios_call_filter);

int dell_smbios_call(struct calling_interface_buffer *buffer)
{
	int (*call_fn)(struct calling_interface_buffer *) = NULL;
	struct device *selected_dev = NULL;
	struct smbios_device *priv;
	int ret;

	mutex_lock(&smbios_mutex);
	list_for_each_entry(priv, &smbios_device_list, list) {
		if (!selected_dev || priv->device->id >= selected_dev->id) {
			dev_dbg(priv->device, "Trying device ID: %d\n",
				priv->device->id);
			call_fn = priv->call_fn;
			selected_dev = priv->device;
		}
	}

	if (!selected_dev) {
		ret = -ENODEV;
		pr_err("No dell-smbios drivers are loaded\n");
		goto out_smbios_call;
	}

	ret = call_fn(buffer);

out_smbios_call:
	mutex_unlock(&smbios_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(dell_smbios_call);

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

	/*
	 * 4 bytes of table header, plus 7 bytes of Dell header
	 * plus at least 6 bytes of entry
	 */

	if (dm->length < 17)
		return;

	da_supported_commands = table->supportedCmds;

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
		location_name = kasprintf(GFP_KERNEL, "%04x_location",
					  da_tokens[i].tokenID);
		if (location_name == NULL)
			goto out_unwind_strings;
		sysfs_attr_init(&token_location_attrs[i].attr);
		token_location_attrs[i].attr.name = location_name;
		token_location_attrs[i].attr.mode = 0444;
		token_location_attrs[i].show = location_show;
		token_attrs[j++] = &token_location_attrs[i].attr;

		/* add value */
		value_name = kasprintf(GFP_KERNEL, "%04x_value",
				       da_tokens[i].tokenID);
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
	int ret, wmi, smm;

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

	/* register backends */
	wmi = init_dell_smbios_wmi();
	if (wmi)
		pr_debug("Failed to initialize WMI backend: %d\n", wmi);
	smm = init_dell_smbios_smm();
	if (smm)
		pr_debug("Failed to initialize SMM backend: %d\n", smm);
	if (wmi && smm) {
		pr_err("No SMBIOS backends available (wmi: %d, smm: %d)\n",
			wmi, smm);
		goto fail_sysfs;
	}

	return 0;

fail_sysfs:
	free_group(platform_device);

fail_create_group:
	platform_device_del(platform_device);

fail_platform_device_add:
	platform_device_put(platform_device);

fail_platform_device_alloc:
	platform_driver_unregister(&platform_driver);

fail_platform_driver:
	kfree(da_tokens);
	return ret;
}

static void __exit dell_smbios_exit(void)
{
	exit_dell_smbios_wmi();
	exit_dell_smbios_smm();
	mutex_lock(&smbios_mutex);
	if (platform_device) {
		free_group(platform_device);
		platform_device_unregister(platform_device);
		platform_driver_unregister(&platform_driver);
	}
	kfree(da_tokens);
	mutex_unlock(&smbios_mutex);
}

module_init(dell_smbios_init);
module_exit(dell_smbios_exit);

MODULE_AUTHOR("Matthew Garrett <mjg@redhat.com>");
MODULE_AUTHOR("Gabriele Mazzotta <gabriele.mzt@gmail.com>");
MODULE_AUTHOR("Pali Rohár <pali.rohar@gmail.com>");
MODULE_AUTHOR("Mario Limonciello <mario.limonciello@dell.com>");
MODULE_DESCRIPTION("Common functions for kernel modules using Dell SMBIOS");
MODULE_LICENSE("GPL");
