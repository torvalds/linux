// SPDX-License-Identifier: GPL-2.0-only
/*
 *  SMI methods for use with dell-smbios
 *
 *  Copyright (c) Red Hat <mjg@redhat.com>
 *  Copyright (c) 2014 Gabriele Mazzotta <gabriele.mzt@gmail.com>
 *  Copyright (c) 2014 Pali Rohár <pali.rohar@gmail.com>
 *  Copyright (c) 2017 Dell Inc.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/dmi.h>
#include <linux/gfp.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include "dcdbas.h"
#include "dell-smbios.h"

static int da_command_address;
static int da_command_code;
static struct calling_interface_buffer *buffer;
static struct platform_device *platform_device;
static DEFINE_MUTEX(smm_mutex);

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
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_CHASSIS_TYPE, "10"), /*Notebook*/
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

static void parse_da_table(const struct dmi_header *dm)
{
	struct calling_interface_structure *table =
		container_of(dm, struct calling_interface_structure, header);

	/* 4 bytes of table header, plus 7 bytes of Dell header, plus at least
	 * 6 bytes of entry
	 */
	if (dm->length < 17)
		return;

	da_command_address = table->cmdIOAddress;
	da_command_code = table->cmdIOCode;
}

static void find_cmd_address(const struct dmi_header *dm, void *dummy)
{
	switch (dm->type) {
	case 0xda: /* Calling interface */
		parse_da_table(dm);
		break;
	}
}

static int dell_smbios_smm_call(struct calling_interface_buffer *input)
{
	struct smi_cmd command;
	size_t size;

	size = sizeof(struct calling_interface_buffer);
	command.magic = SMI_CMD_MAGIC;
	command.command_address = da_command_address;
	command.command_code = da_command_code;
	command.ebx = virt_to_phys(buffer);
	command.ecx = 0x42534931;

	mutex_lock(&smm_mutex);
	memcpy(buffer, input, size);
	dcdbas_smi_request(&command);
	memcpy(input, buffer, size);
	mutex_unlock(&smm_mutex);
	return 0;
}

/* When enabled this indicates that SMM won't work */
static bool test_wsmt_enabled(void)
{
	struct calling_interface_token *wsmt;

	/* if token doesn't exist, SMM will work */
	wsmt = dell_smbios_find_token(WSMT_EN_TOKEN);
	if (!wsmt)
		return false;

	/* If token exists, try to access over SMM but set a dummy return.
	 * - If WSMT disabled it will be overwritten by SMM
	 * - If WSMT enabled then dummy value will remain
	 */
	buffer->cmd_class = CLASS_TOKEN_READ;
	buffer->cmd_select = SELECT_TOKEN_STD;
	memset(buffer, 0, sizeof(struct calling_interface_buffer));
	buffer->input[0] = wsmt->location;
	buffer->output[0] = 99;
	dell_smbios_smm_call(buffer);
	if (buffer->output[0] == 99)
		return true;

	return false;
}

int init_dell_smbios_smm(void)
{
	int ret;
	/*
	 * Allocate buffer below 4GB for SMI data--only 32-bit physical addr
	 * is passed to SMI handler.
	 */
	buffer = (void *)__get_free_page(GFP_KERNEL | GFP_DMA32);
	if (!buffer)
		return -ENOMEM;

	dmi_walk(find_cmd_address, NULL);

	if (test_wsmt_enabled()) {
		pr_debug("Disabling due to WSMT enabled\n");
		ret = -ENODEV;
		goto fail_wsmt;
	}

	platform_device = platform_device_alloc("dell-smbios", 1);
	if (!platform_device) {
		ret = -ENOMEM;
		goto fail_platform_device_alloc;
	}

	ret = platform_device_add(platform_device);
	if (ret)
		goto fail_platform_device_add;

	ret = dell_smbios_register_device(&platform_device->dev,
					  &dell_smbios_smm_call);
	if (ret)
		goto fail_register;

	return 0;

fail_register:
	platform_device_del(platform_device);

fail_platform_device_add:
	platform_device_put(platform_device);

fail_wsmt:
fail_platform_device_alloc:
	free_page((unsigned long)buffer);
	return ret;
}

void exit_dell_smbios_smm(void)
{
	if (platform_device) {
		dell_smbios_unregister_device(&platform_device->dev);
		platform_device_unregister(platform_device);
		free_page((unsigned long)buffer);
	}
}
