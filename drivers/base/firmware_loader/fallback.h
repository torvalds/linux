/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __FIRMWARE_FALLBACK_H
#define __FIRMWARE_FALLBACK_H

#include <linux/firmware.h>
#include <linux/device.h>

/**
 * struct firmware_fallback_config - firmware fallback configuratioon settings
 *
 * Helps describe and fine tune the fallback mechanism.
 *
 * @force_sysfs_fallback: force the sysfs fallback mechanism to be used
 * 	as if one had enabled CONFIG_FW_LOADER_USER_HELPER_FALLBACK=y.
 * 	Useful to help debug a CONFIG_FW_LOADER_USER_HELPER_FALLBACK=y
 * 	functionality on a kernel where that config entry has been disabled.
 * @ignore_sysfs_fallback: force to disable the sysfs fallback mechanism.
 * 	This emulates the behaviour as if we had set the kernel
 * 	config CONFIG_FW_LOADER_USER_HELPER=n.
 * @old_timeout: for internal use
 * @loading_timeout: the timeout to wait for the fallback mechanism before
 * 	giving up, in seconds.
 */
struct firmware_fallback_config {
	unsigned int force_sysfs_fallback;
	unsigned int ignore_sysfs_fallback;
	int old_timeout;
	int loading_timeout;
};

#ifdef CONFIG_FW_LOADER_USER_HELPER
int fw_sysfs_fallback(struct firmware *fw, const char *name,
		      struct device *device,
		      unsigned int opt_flags,
		      int ret);
void kill_pending_fw_fallback_reqs(bool only_kill_custom);

void fw_fallback_set_cache_timeout(void);
void fw_fallback_set_default_timeout(void);

int register_sysfs_loader(void);
void unregister_sysfs_loader(void);
#else /* CONFIG_FW_LOADER_USER_HELPER */
static inline int fw_sysfs_fallback(struct firmware *fw, const char *name,
				    struct device *device,
				    unsigned int opt_flags,
				    int ret)
{
	/* Keep carrying over the same error */
	return ret;
}

static inline void kill_pending_fw_fallback_reqs(bool only_kill_custom) { }
static inline void fw_fallback_set_cache_timeout(void) { }
static inline void fw_fallback_set_default_timeout(void) { }

static inline int register_sysfs_loader(void)
{
	return 0;
}

static inline void unregister_sysfs_loader(void)
{
}
#endif /* CONFIG_FW_LOADER_USER_HELPER */

#endif /* __FIRMWARE_FALLBACK_H */
