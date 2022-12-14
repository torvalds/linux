// SPDX-License-Identifier: GPL-2.0
/*
 * efibc: control EFI bootloaders which obey LoaderEntryOneShot var
 * Copyright (c) 2013-2016, Intel Corporation.
 */

#define pr_fmt(fmt) "efibc: " fmt

#include <linux/efi.h>
#include <linux/module.h>
#include <linux/reboot.h>
#include <linux/slab.h>
#include <linux/ucs2_string.h>

#define MAX_DATA_LEN	512

static int efibc_set_variable(efi_char16_t *name, efi_char16_t *value,
			      unsigned long len)
{
	efi_status_t status;

	status = efi.set_variable(name, &LINUX_EFI_LOADER_ENTRY_GUID,
				  EFI_VARIABLE_NON_VOLATILE
				  | EFI_VARIABLE_BOOTSERVICE_ACCESS
				  | EFI_VARIABLE_RUNTIME_ACCESS,
				  len * sizeof(efi_char16_t), value);

	if (status != EFI_SUCCESS) {
		pr_err("failed to set EFI variable: 0x%lx\n", status);
		return -EIO;
	}
	return 0;
}

static int efibc_reboot_notifier_call(struct notifier_block *notifier,
				      unsigned long event, void *data)
{
	efi_char16_t *reason = event == SYS_RESTART ? L"reboot"
						    : L"shutdown";
	const u8 *str = data;
	efi_char16_t *wdata;
	unsigned long l;
	int ret;

	ret = efibc_set_variable(L"LoaderEntryRebootReason", reason,
				 ucs2_strlen(reason));
	if (ret || !data)
		return NOTIFY_DONE;

	wdata = kmalloc(MAX_DATA_LEN * sizeof(efi_char16_t), GFP_KERNEL);
	if (!wdata)
		return NOTIFY_DONE;

	for (l = 0; l < MAX_DATA_LEN - 1 && str[l] != '\0'; l++)
		wdata[l] = str[l];
	wdata[l] = L'\0';

	efibc_set_variable(L"LoaderEntryOneShot", wdata, l);

	kfree(wdata);
	return NOTIFY_DONE;
}

static struct notifier_block efibc_reboot_notifier = {
	.notifier_call = efibc_reboot_notifier_call,
};

static int __init efibc_init(void)
{
	int ret;

	if (!efi_rt_services_supported(EFI_RT_SUPPORTED_SET_VARIABLE))
		return -ENODEV;

	ret = register_reboot_notifier(&efibc_reboot_notifier);
	if (ret)
		pr_err("unable to register reboot notifier\n");

	return ret;
}
module_init(efibc_init);

static void __exit efibc_exit(void)
{
	unregister_reboot_notifier(&efibc_reboot_notifier);
}
module_exit(efibc_exit);

MODULE_AUTHOR("Jeremy Compostella <jeremy.compostella@intel.com>");
MODULE_AUTHOR("Matt Gumbel <matthew.k.gumbel@intel.com");
MODULE_DESCRIPTION("EFI Bootloader Control");
MODULE_LICENSE("GPL v2");
