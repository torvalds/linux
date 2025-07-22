// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Common functions for kernel modules using Dell SMBIOS
 *
 *  Copyright (c) Red Hat <mjg@redhat.com>
 *  Copyright (c) 2014 Gabriele Mazzotta <gabriele.mzt@gmail.com>
 *  Copyright (c) 2014 Pali Rohár <pali@kernel.org>
 *
 *  Based on documentation in the libsmbios package:
 *  Copyright (C) 2005-2014 Dell Inc.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/container_of.h>
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
static struct token_sysfs_data *token_entries;
static struct attribute **token_attrs;
static DEFINE_MUTEX(smbios_mutex);

struct token_sysfs_data {
	struct device_attribute location_attr;
	struct device_attribute value_attr;
	struct calling_interface_token *token;
};

struct smbios_device {
	struct list_head list;
	struct device *device;
	int priority;
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
	{0x0000, CLASS_INFO, SELECT_THERMAL_MANAGEMENT},
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

int dell_smbios_register_device(struct device *d, int priority, void *call_fn)
{
	struct smbios_device *priv;

	priv = devm_kzalloc(d, sizeof(struct smbios_device), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	get_device(d);
	priv->device = d;
	priv->priority = priority;
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
		/* tokens enabled ? */
		if (!da_tokens) {
			dev_dbg(d, "no token support on this system\n");
			return -EINVAL;
		}

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
	struct smbios_device *selected = NULL;
	struct smbios_device *priv;
	int ret;

	mutex_lock(&smbios_mutex);
	list_for_each_entry(priv, &smbios_device_list, list) {
		if (!selected || priv->priority >= selected->priority) {
			dev_dbg(priv->device, "Trying device ID: %d\n", priv->priority);
			selected = priv;
		}
	}

	if (!selected) {
		ret = -ENODEV;
		pr_err("No dell-smbios drivers are loaded\n");
		goto out_smbios_call;
	}

	ret = selected->call_fn(buffer);

out_smbios_call:
	mutex_unlock(&smbios_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(dell_smbios_call);

void dell_fill_request(struct calling_interface_buffer *buffer,
			       u32 arg0, u32 arg1, u32 arg2, u32 arg3)
{
	memset(buffer, 0, sizeof(struct calling_interface_buffer));
	buffer->input[0] = arg0;
	buffer->input[1] = arg1;
	buffer->input[2] = arg2;
	buffer->input[3] = arg3;
}
EXPORT_SYMBOL_GPL(dell_fill_request);

int dell_send_request(struct calling_interface_buffer *buffer,
			     u16 class, u16 select)
{
	int ret;

	buffer->cmd_class = class;
	buffer->cmd_select = select;
	ret = dell_smbios_call(buffer);
	if (ret != 0)
		return ret;
	return dell_smbios_error(buffer->output[0]);
}
EXPORT_SYMBOL_GPL(dell_send_request);

struct calling_interface_token *dell_smbios_find_token(int tokenid)
{
	int i;

	if (!da_tokens)
		return NULL;

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

bool dell_smbios_class_is_supported(u16 class)
{
	/* Classes over 30 always unsupported */
	if (class > 30)
		return false;
	return da_supported_commands & (1 << class);
}
EXPORT_SYMBOL_GPL(dell_smbios_class_is_supported);

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

static ssize_t location_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct token_sysfs_data *data = container_of(attr, struct token_sysfs_data, location_attr);

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	return sysfs_emit(buf, "%08x", data->token->location);
}

static ssize_t value_show(struct device *dev,
			  struct device_attribute *attr, char *buf)
{
	struct token_sysfs_data *data = container_of(attr, struct token_sysfs_data, value_attr);

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	return sysfs_emit(buf, "%08x", data->token->value);
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
	int ret;
	int i, j;

	token_entries = kcalloc(da_num_tokens, sizeof(*token_entries), GFP_KERNEL);
	if (!token_entries)
		return -ENOMEM;

	/* need to store both location and value + terminator*/
	token_attrs = kcalloc((2 * da_num_tokens) + 1, sizeof(*token_attrs), GFP_KERNEL);
	if (!token_attrs)
		goto out_allocate_attrs;

	for (i = 0, j = 0; i < da_num_tokens; i++) {
		/* skip empty */
		if (da_tokens[i].tokenID == 0)
			continue;

		token_entries[i].token = &da_tokens[i];

		/* add location */
		location_name = kasprintf(GFP_KERNEL, "%04x_location",
					  da_tokens[i].tokenID);
		if (location_name == NULL)
			goto out_unwind_strings;

		sysfs_attr_init(&token_entries[i].location_attr.attr);
		token_entries[i].location_attr.attr.name = location_name;
		token_entries[i].location_attr.attr.mode = 0444;
		token_entries[i].location_attr.show = location_show;
		token_attrs[j++] = &token_entries[i].location_attr.attr;

		/* add value */
		value_name = kasprintf(GFP_KERNEL, "%04x_value",
				       da_tokens[i].tokenID);
		if (!value_name) {
			kfree(location_name);
			goto out_unwind_strings;
		}

		sysfs_attr_init(&token_entries[i].value_attr.attr);
		token_entries[i].value_attr.attr.name = value_name;
		token_entries[i].value_attr.attr.mode = 0444;
		token_entries[i].value_attr.show = value_show;
		token_attrs[j++] = &token_entries[i].value_attr.attr;
	}
	smbios_attribute_group.attrs = token_attrs;

	ret = sysfs_create_group(&dev->dev.kobj, &smbios_attribute_group);
	if (ret)
		goto out_unwind_strings;
	return 0;

out_unwind_strings:
	while (i--) {
		kfree(token_entries[i].location_attr.attr.name);
		kfree(token_entries[i].value_attr.attr.name);
	}
	kfree(token_attrs);
out_allocate_attrs:
	kfree(token_entries);

	return -ENOMEM;
}

static void free_group(struct platform_device *pdev)
{
	int i;

	sysfs_remove_group(&pdev->dev.kobj,
				&smbios_attribute_group);
	for (i = 0; i < da_num_tokens; i++) {
		kfree(token_entries[i].location_attr.attr.name);
		kfree(token_entries[i].value_attr.attr.name);
	}
	kfree(token_attrs);
	kfree(token_entries);
}

static int __init dell_smbios_init(void)
{
	int ret, wmi, smm;

	if (!dmi_find_device(DMI_DEV_TYPE_OEM_STRING, "Dell System", NULL) &&
	    !dmi_find_device(DMI_DEV_TYPE_OEM_STRING, "Alienware", NULL) &&
	    !dmi_find_device(DMI_DEV_TYPE_OEM_STRING, "www.dell.com", NULL)) {
		pr_err("Unable to run on non-Dell system\n");
		return -ENODEV;
	}

	dmi_walk(find_tokens, NULL);

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
		ret = -ENODEV;
		goto fail_create_group;
	}

	if (da_tokens)  {
		/* duplicate tokens will cause problems building sysfs files */
		zero_duplicates(&platform_device->dev);

		ret = build_tokens_sysfs(platform_device);
		if (ret)
			goto fail_sysfs;
	}

	return 0;

fail_sysfs:
	if (!wmi)
		exit_dell_smbios_wmi();
	if (!smm)
		exit_dell_smbios_smm();

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
		if (da_tokens)
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
MODULE_AUTHOR("Pali Rohár <pali@kernel.org>");
MODULE_AUTHOR("Mario Limonciello <mario.limonciello@outlook.com>");
MODULE_DESCRIPTION("Common functions for kernel modules using Dell SMBIOS");
MODULE_LICENSE("GPL");
