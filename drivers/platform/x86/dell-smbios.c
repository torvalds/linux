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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/dmi.h>
#include <linux/err.h>
#include <linux/gfp.h>
#include <linux/mutex.h>
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
static struct calling_interface_token *da_tokens;

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

	buffer->class = class;
	buffer->select = select;

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

static int __init dell_smbios_init(void)
{
	int ret;

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

	return 0;

fail_buffer:
	kfree(da_tokens);
	return ret;
}

static void __exit dell_smbios_exit(void)
{
	kfree(da_tokens);
	free_page((unsigned long)buffer);
}

subsys_initcall(dell_smbios_init);
module_exit(dell_smbios_exit);

MODULE_AUTHOR("Matthew Garrett <mjg@redhat.com>");
MODULE_AUTHOR("Gabriele Mazzotta <gabriele.mzt@gmail.com>");
MODULE_AUTHOR("Pali Rohár <pali.rohar@gmail.com>");
MODULE_DESCRIPTION("Common functions for kernel modules using Dell SMBIOS");
MODULE_LICENSE("GPL");
