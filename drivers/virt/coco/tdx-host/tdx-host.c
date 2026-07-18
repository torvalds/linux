// SPDX-License-Identifier: GPL-2.0
/*
 * TDX host user interface driver
 *
 * Copyright (C) 2025 Intel Corporation
 */

#include <linux/device/faux.h>
#include <linux/firmware.h>
#include <linux/module.h>
#include <linux/sysfs.h>

#include <asm/cpu_device_id.h>
#include <asm/seamldr.h>
#include <asm/tdx.h>

static const struct x86_cpu_id tdx_host_ids[] = {
	X86_MATCH_FEATURE(X86_FEATURE_TDX_HOST_PLATFORM, NULL),
	{}
};
MODULE_DEVICE_TABLE(x86cpu, tdx_host_ids);

static ssize_t version_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	const struct tdx_sys_info *tdx_sysinfo = tdx_get_sysinfo();
	const struct tdx_sys_info_version *ver;
	int ret;

	if (!tdx_sysinfo)
		return -ENXIO;

	/*
	 * The version number can change during an update.
	 * Lock out updates while printing the version.
	 */
	seamldr_lock_module_update();

	ver = &tdx_sysinfo->version;
	ret = sysfs_emit(buf, TDX_VERSION_FMT "\n", ver->major_version,
						    ver->minor_version,
						    ver->update_version);
	seamldr_unlock_module_update();

	return ret;
}
static DEVICE_ATTR_RO(version);

static struct attribute *tdx_host_attrs[] = {
	&dev_attr_version.attr,
	NULL,
};

static const struct attribute_group tdx_host_group = {
	.attrs = tdx_host_attrs,
};

static ssize_t seamldr_version_show(struct device *dev, struct device_attribute *attr,
				    char *buf)
{
	struct seamldr_info info;
	int ret;

	ret = seamldr_get_info(&info);
	if (ret)
		return ret;

	return sysfs_emit(buf, TDX_VERSION_FMT "\n", info.major_version,
						     info.minor_version,
						     info.update_version);
}

static ssize_t num_remaining_updates_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	struct seamldr_info info;
	int ret;

	ret = seamldr_get_info(&info);
	if (ret)
		return ret;

	return sysfs_emit(buf, "%u\n", info.num_remaining_updates);
}

/*
 * These attributes are intended for managing TDX module updates. Reading
 * them issues a slow, serialized P-SEAMLDR query, so keep them admin-only.
 */
static DEVICE_ATTR_ADMIN_RO(seamldr_version);
static DEVICE_ATTR_ADMIN_RO(num_remaining_updates);

static struct attribute *seamldr_attrs[] = {
	&dev_attr_seamldr_version.attr,
	&dev_attr_num_remaining_updates.attr,
	NULL,
};

static bool supports_runtime_update(void)
{
	const struct tdx_sys_info *sysinfo = tdx_get_sysinfo();

	if (!sysinfo)
		return false;

	if (!tdx_supports_runtime_update(sysinfo))
		return false;

	/*
	 * This bug makes P-SEAMLDR calls clobber the current VMCS
	 * which breaks KVM. Avoid P-SEAMLDR calls by hiding all
	 * attributes if the CPU has this bug.
	 */
	if (boot_cpu_has_bug(X86_BUG_SEAMRET_INVD_VMCS))
		return false;

	return true;
}

static umode_t seamldr_group_visible(struct kobject *kobj, struct attribute *attr, int idx)
{
	if (!supports_runtime_update())
		return 0;

	return attr->mode;
}

static const struct attribute_group seamldr_group = {
	.attrs = seamldr_attrs,
	.is_visible = seamldr_group_visible,
};

static const struct attribute_group *tdx_host_groups[] = {
	&tdx_host_group,
	&seamldr_group,
	NULL,
};

static enum fw_upload_err tdx_fw_prepare(struct fw_upload *fwl,
					 const u8 *data, u32 data_len)
{
	return FW_UPLOAD_ERR_NONE;
}

static enum fw_upload_err tdx_fw_write(struct fw_upload *fwl, const u8 *data,
				       u32 offset, u32 data_len, u32 *written)
{
	int ret;

	ret = seamldr_install_module(data, data_len);
	switch (ret) {
	case 0:
		*written = data_len;
		return FW_UPLOAD_ERR_NONE;
	default:
		return FW_UPLOAD_ERR_FW_INVALID;
	}
}

static enum fw_upload_err tdx_fw_poll_complete(struct fw_upload *fwl)
{
	/*
	 * The upload completed during tdx_fw_write().
	 * Never poll for completion.
	 */
	return FW_UPLOAD_ERR_NONE;
}

static void tdx_fw_cancel(struct fw_upload *fwl)
{
	/*
	 * TDX module updates are not cancellable.
	 * Provide a no-op callback to satisfy fw_upload_ops.
	 */
}

static const struct fw_upload_ops tdx_fw_ops = {
	.prepare	= tdx_fw_prepare,
	.write		= tdx_fw_write,
	.poll_complete	= tdx_fw_poll_complete,
	.cancel		= tdx_fw_cancel,
};

static void seamldr_deinit(void *tdx_fwl)
{
	firmware_upload_unregister(tdx_fwl);
}

static int seamldr_init(struct device *dev)
{
	struct fw_upload *tdx_fwl;

	if (!supports_runtime_update())
		return 0;

	tdx_fwl = firmware_upload_register(THIS_MODULE, dev, "tdx_module",
					   &tdx_fw_ops, NULL);
	if (IS_ERR(tdx_fwl))
		return PTR_ERR(tdx_fwl);

	return devm_add_action_or_reset(dev, seamldr_deinit, tdx_fwl);
}

static int tdx_host_probe(struct faux_device *fdev)
{
	return seamldr_init(&fdev->dev);
}

static const struct faux_device_ops tdx_host_ops = {
	.probe		= tdx_host_probe,
};

static struct faux_device *fdev;

static int __init tdx_host_init(void)
{
	if (!x86_match_cpu(tdx_host_ids) || !tdx_get_sysinfo())
		return -ENODEV;

	fdev = faux_device_create_with_groups(KBUILD_MODNAME, NULL,
					      &tdx_host_ops,
					      tdx_host_groups);
	if (!fdev)
		return -ENODEV;

	return 0;
}
module_init(tdx_host_init);

static void __exit tdx_host_exit(void)
{
	faux_device_destroy(fdev);
}
module_exit(tdx_host_exit);

MODULE_DESCRIPTION("TDX Host Services");
MODULE_LICENSE("GPL");
