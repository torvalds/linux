/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __FIRMWARE_SYSFS_H
#define __FIRMWARE_SYSFS_H

#include <linux/device.h>

#include "firmware.h"

MODULE_IMPORT_NS("FIRMWARE_LOADER_PRIVATE");

extern struct firmware_fallback_config fw_fallback_config;
extern struct device_attribute dev_attr_loading;

#ifdef CONFIG_FW_LOADER_USER_HELPER
/**
 * struct firmware_fallback_config - firmware fallback configuration settings
 *
 * Helps describe and fine tune the fallback mechanism.
 *
 * @force_sysfs_fallback: force the sysfs fallback mechanism to be used
 *	as if one had enabled CONFIG_FW_LOADER_USER_HELPER_FALLBACK=y.
 *	Useful to help debug a CONFIG_FW_LOADER_USER_HELPER_FALLBACK=y
 *	functionality on a kernel where that config entry has been disabled.
 * @ignore_sysfs_fallback: force to disable the sysfs fallback mechanism.
 *	This emulates the behaviour as if we had set the kernel
 *	config CONFIG_FW_LOADER_USER_HELPER=n.
 * @old_timeout: for internal use
 * @loading_timeout: the timeout to wait for the fallback mechanism before
 *	giving up, in seconds.
 */
struct firmware_fallback_config {
	unsigned int force_sysfs_fallback;
	unsigned int ignore_sysfs_fallback;
	int old_timeout;
	int loading_timeout;
};

/* These getters are vetted to use int properly */
static inline int __firmware_loading_timeout(void)
{
	return fw_fallback_config.loading_timeout;
}

/* These setters are vetted to use int properly */
static inline void __fw_fallback_set_timeout(int timeout)
{
	fw_fallback_config.loading_timeout = timeout;
}
#endif

#ifdef CONFIG_FW_LOADER_SYSFS
int register_sysfs_loader(void);
void unregister_sysfs_loader(void);
#if defined(CONFIG_FW_LOADER_USER_HELPER) && defined(CONFIG_SYSCTL)
int register_firmware_config_sysctl(void);
void unregister_firmware_config_sysctl(void);
#else
static inline int register_firmware_config_sysctl(void)
{
	return 0;
}

static inline void unregister_firmware_config_sysctl(void) { }
#endif /* CONFIG_FW_LOADER_USER_HELPER && CONFIG_SYSCTL */
#else /* CONFIG_FW_LOADER_SYSFS */
static inline int register_sysfs_loader(void)
{
	return 0;
}

static inline void unregister_sysfs_loader(void)
{
}
#endif /* CONFIG_FW_LOADER_SYSFS */

struct fw_sysfs {
	bool nowait;
	struct device dev;
	struct fw_priv *fw_priv;
	struct firmware *fw;
	void *fw_upload_priv;
};
#define to_fw_sysfs(__dev)	container_of_const(__dev, struct fw_sysfs, dev)

void __fw_load_abort(struct fw_priv *fw_priv);

static inline void fw_load_abort(struct fw_sysfs *fw_sysfs)
{
	struct fw_priv *fw_priv = fw_sysfs->fw_priv;

	__fw_load_abort(fw_priv);
}

struct fw_sysfs *
fw_create_instance(struct firmware *firmware, const char *fw_name,
		   struct device *device, u32 opt_flags);

#ifdef CONFIG_FW_UPLOAD
extern struct device_attribute dev_attr_status;
extern struct device_attribute dev_attr_error;
extern struct device_attribute dev_attr_cancel;
extern struct device_attribute dev_attr_remaining_size;

int fw_upload_start(struct fw_sysfs *fw_sysfs);
void fw_upload_free(struct fw_sysfs *fw_sysfs);
umode_t fw_upload_is_visible(struct kobject *kobj, struct attribute *attr, int n);
#else
static inline int fw_upload_start(struct fw_sysfs *fw_sysfs)
{
	return 0;
}

static inline void fw_upload_free(struct fw_sysfs *fw_sysfs)
{
}
#endif

#endif /* __FIRMWARE_SYSFS_H */
