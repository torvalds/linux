// SPDX-License-Identifier: GPL-2.0

#include <linux/types.h>
#include <linux/kconfig.h>
#include <linux/list.h>
#include <linux/security.h>
#include <linux/umh.h>
#include <linux/sysctl.h>
#include <linux/module.h>

#include "fallback.h"
#include "firmware.h"

/*
 * firmware fallback mechanism
 */

/*
 * use small loading timeout for caching devices' firmware because all these
 * firmware images have been loaded successfully at lease once, also system is
 * ready for completing firmware loading now. The maximum size of firmware in
 * current distributions is about 2M bytes, so 10 secs should be enough.
 */
void fw_fallback_set_cache_timeout(void)
{
	fw_fallback_config.old_timeout = __firmware_loading_timeout();
	__fw_fallback_set_timeout(10);
}

/* Restores the timeout to the value last configured during normal operation */
void fw_fallback_set_default_timeout(void)
{
	__fw_fallback_set_timeout(fw_fallback_config.old_timeout);
}

static long firmware_loading_timeout(void)
{
	return __firmware_loading_timeout() > 0 ?
		__firmware_loading_timeout() * HZ : MAX_JIFFY_OFFSET;
}

static inline int fw_sysfs_wait_timeout(struct fw_priv *fw_priv,  long timeout)
{
	return __fw_state_wait_common(fw_priv, timeout);
}

static LIST_HEAD(pending_fw_head);

void kill_pending_fw_fallback_reqs(bool kill_all)
{
	struct fw_priv *fw_priv;
	struct fw_priv *next;

	mutex_lock(&fw_lock);
	list_for_each_entry_safe(fw_priv, next, &pending_fw_head,
				 pending_list) {
		if (kill_all || !fw_priv->need_uevent)
			 __fw_load_abort(fw_priv);
	}

	if (kill_all)
		fw_load_abort_all = true;

	mutex_unlock(&fw_lock);
}

/**
 * fw_load_sysfs_fallback() - load a firmware via the sysfs fallback mechanism
 * @fw_sysfs: firmware sysfs information for the firmware to load
 * @timeout: timeout to wait for the load
 *
 * In charge of constructing a sysfs fallback interface for firmware loading.
 **/
static int fw_load_sysfs_fallback(struct fw_sysfs *fw_sysfs, long timeout)
{
	int retval = 0;
	struct device *f_dev = &fw_sysfs->dev;
	struct fw_priv *fw_priv = fw_sysfs->fw_priv;

	/* fall back on userspace loading */
	if (!fw_priv->data)
		fw_priv->is_paged_buf = true;

	dev_set_uevent_suppress(f_dev, true);

	retval = device_add(f_dev);
	if (retval) {
		dev_err(f_dev, "%s: device_register failed\n", __func__);
		goto err_put_dev;
	}

	mutex_lock(&fw_lock);
	if (fw_load_abort_all || fw_state_is_aborted(fw_priv)) {
		mutex_unlock(&fw_lock);
		retval = -EINTR;
		goto out;
	}
	list_add(&fw_priv->pending_list, &pending_fw_head);
	mutex_unlock(&fw_lock);

	if (fw_priv->opt_flags & FW_OPT_UEVENT) {
		fw_priv->need_uevent = true;
		dev_set_uevent_suppress(f_dev, false);
		dev_dbg(f_dev, "firmware: requesting %s\n", fw_priv->fw_name);
		kobject_uevent(&fw_sysfs->dev.kobj, KOBJ_ADD);
	} else {
		timeout = MAX_JIFFY_OFFSET;
	}

	retval = fw_sysfs_wait_timeout(fw_priv, timeout);
	if (retval < 0 && retval != -ENOENT) {
		mutex_lock(&fw_lock);
		fw_load_abort(fw_sysfs);
		mutex_unlock(&fw_lock);
	}

	if (fw_state_is_aborted(fw_priv)) {
		if (retval == -ERESTARTSYS)
			retval = -EINTR;
	} else if (fw_priv->is_paged_buf && !fw_priv->data)
		retval = -ENOMEM;

out:
	device_del(f_dev);
err_put_dev:
	put_device(f_dev);
	return retval;
}

static int fw_load_from_user_helper(struct firmware *firmware,
				    const char *name, struct device *device,
				    u32 opt_flags)
{
	struct fw_sysfs *fw_sysfs;
	long timeout;
	int ret;

	timeout = firmware_loading_timeout();
	if (opt_flags & FW_OPT_NOWAIT) {
		timeout = usermodehelper_read_lock_wait(timeout);
		if (!timeout) {
			dev_dbg(device, "firmware: %s loading timed out\n",
				name);
			return -EBUSY;
		}
	} else {
		ret = usermodehelper_read_trylock();
		if (WARN_ON(ret)) {
			dev_err(device, "firmware: %s will not be loaded\n",
				name);
			return ret;
		}
	}

	fw_sysfs = fw_create_instance(firmware, name, device, opt_flags);
	if (IS_ERR(fw_sysfs)) {
		ret = PTR_ERR(fw_sysfs);
		goto out_unlock;
	}

	fw_sysfs->fw_priv = firmware->priv;
	ret = fw_load_sysfs_fallback(fw_sysfs, timeout);

	if (!ret)
		ret = assign_fw(firmware, device);

out_unlock:
	usermodehelper_read_unlock();

	return ret;
}

static bool fw_force_sysfs_fallback(u32 opt_flags)
{
	if (fw_fallback_config.force_sysfs_fallback)
		return true;
	if (!(opt_flags & FW_OPT_USERHELPER))
		return false;
	return true;
}

static bool fw_run_sysfs_fallback(u32 opt_flags)
{
	int ret;

	if (fw_fallback_config.ignore_sysfs_fallback) {
		pr_info_once("Ignoring firmware sysfs fallback due to sysctl knob\n");
		return false;
	}

	if ((opt_flags & FW_OPT_NOFALLBACK_SYSFS))
		return false;

	/* Also permit LSMs and IMA to fail firmware sysfs fallback */
	ret = security_kernel_load_data(LOADING_FIRMWARE, true);
	if (ret < 0)
		return false;

	return fw_force_sysfs_fallback(opt_flags);
}

/**
 * firmware_fallback_sysfs() - use the fallback mechanism to find firmware
 * @fw: pointer to firmware image
 * @name: name of firmware file to look for
 * @device: device for which firmware is being loaded
 * @opt_flags: options to control firmware loading behaviour, as defined by
 *	       &enum fw_opt
 * @ret: return value from direct lookup which triggered the fallback mechanism
 *
 * This function is called if direct lookup for the firmware failed, it enables
 * a fallback mechanism through userspace by exposing a sysfs loading
 * interface. Userspace is in charge of loading the firmware through the sysfs
 * loading interface. This sysfs fallback mechanism may be disabled completely
 * on a system by setting the proc sysctl value ignore_sysfs_fallback to true.
 * If this is false we check if the internal API caller set the
 * @FW_OPT_NOFALLBACK_SYSFS flag, if so it would also disable the fallback
 * mechanism. A system may want to enforce the sysfs fallback mechanism at all
 * times, it can do this by setting ignore_sysfs_fallback to false and
 * force_sysfs_fallback to true.
 * Enabling force_sysfs_fallback is functionally equivalent to build a kernel
 * with CONFIG_FW_LOADER_USER_HELPER_FALLBACK.
 **/
int firmware_fallback_sysfs(struct firmware *fw, const char *name,
			    struct device *device,
			    u32 opt_flags,
			    int ret)
{
	if (!fw_run_sysfs_fallback(opt_flags))
		return ret;

	if (!(opt_flags & FW_OPT_NO_WARN))
		dev_warn(device, "Falling back to sysfs fallback for: %s\n",
				 name);
	else
		dev_dbg(device, "Falling back to sysfs fallback for: %s\n",
				name);
	return fw_load_from_user_helper(fw, name, device, opt_flags);
}
