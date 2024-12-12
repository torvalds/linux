/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _LINUX_NVMEM_LAYOUTS_U_BOOT_ENV_H
#define _LINUX_NVMEM_LAYOUTS_U_BOOT_ENV_H

enum u_boot_env_format {
	U_BOOT_FORMAT_SINGLE,
	U_BOOT_FORMAT_REDUNDANT,
	U_BOOT_FORMAT_BROADCOM,
};

int u_boot_env_parse(struct device *dev, struct nvmem_device *nvmem,
		     enum u_boot_env_format format);

#endif  /* ifndef _LINUX_NVMEM_LAYOUTS_U_BOOT_ENV_H */
