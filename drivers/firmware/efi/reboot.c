/*
 * Copyright (C) 2014 Intel Corporation; author Matt Fleming
 * Copyright (c) 2014 Red Hat, Inc., Mark Salter <msalter@redhat.com>
 */
#include <linux/efi.h>
#include <linux/reboot.h>

int efi_reboot_quirk_mode = -1;

void efi_reboot(enum reboot_mode reboot_mode, const char *__unused)
{
	const char *str[] = { "cold", "warm", "shutdown", "platform" };
	int efi_mode, cap_reset_mode;

	if (!efi_enabled(EFI_RUNTIME_SERVICES))
		return;

	switch (reboot_mode) {
	case REBOOT_WARM:
	case REBOOT_SOFT:
		efi_mode = EFI_RESET_WARM;
		break;
	default:
		efi_mode = EFI_RESET_COLD;
		break;
	}

	/*
	 * If a quirk forced an EFI reset mode, always use that.
	 */
	if (efi_reboot_quirk_mode != -1)
		efi_mode = efi_reboot_quirk_mode;

	if (efi_capsule_pending(&cap_reset_mode)) {
		if (efi_mode != cap_reset_mode)
			printk(KERN_CRIT "efi: %s reset requested but pending "
			       "capsule update requires %s reset... Performing "
			       "%s reset.\n", str[efi_mode], str[cap_reset_mode],
			       str[cap_reset_mode]);
		efi_mode = cap_reset_mode;
	}

	efi.reset_system(efi_mode, EFI_SUCCESS, 0, NULL);
}

bool __weak efi_poweroff_required(void)
{
	return false;
}

static void efi_power_off(void)
{
	efi.reset_system(EFI_RESET_SHUTDOWN, EFI_SUCCESS, 0, NULL);
}

static int __init efi_shutdown_init(void)
{
	if (!efi_enabled(EFI_RUNTIME_SERVICES))
		return -ENODEV;

	if (efi_poweroff_required())
		pm_power_off = efi_power_off;

	return 0;
}
late_initcall(efi_shutdown_init);
