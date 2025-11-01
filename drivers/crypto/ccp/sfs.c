// SPDX-License-Identifier: GPL-2.0-only
/*
 * AMD Secure Processor Seamless Firmware Servicing support.
 *
 * Copyright (C) 2025 Advanced Micro Devices, Inc.
 *
 * Author: Ashish Kalra <ashish.kalra@amd.com>
 */

#include <linux/firmware.h>

#include "sfs.h"
#include "sev-dev.h"

#define SFS_DEFAULT_TIMEOUT		(10 * MSEC_PER_SEC)
#define SFS_MAX_PAYLOAD_SIZE		(2 * 1024 * 1024)
#define SFS_NUM_2MB_PAGES_CMDBUF	(SFS_MAX_PAYLOAD_SIZE / PMD_SIZE)
#define SFS_NUM_PAGES_CMDBUF		(SFS_MAX_PAYLOAD_SIZE / PAGE_SIZE)

static DEFINE_MUTEX(sfs_ioctl_mutex);

static struct sfs_misc_dev *misc_dev;

static int send_sfs_cmd(struct sfs_device *sfs_dev, int msg)
{
	int ret;

	sfs_dev->command_buf->hdr.status = 0;
	sfs_dev->command_buf->hdr.sub_cmd_id = msg;

	ret = psp_extended_mailbox_cmd(sfs_dev->psp,
				       SFS_DEFAULT_TIMEOUT,
				       (struct psp_ext_request *)sfs_dev->command_buf);
	if (ret == -EIO) {
		dev_dbg(sfs_dev->dev,
			 "msg 0x%x failed with PSP error: 0x%x, extended status: 0x%x\n",
			 msg, sfs_dev->command_buf->hdr.status,
			 *(u32 *)sfs_dev->command_buf->buf);
	}

	return ret;
}

static int send_sfs_get_fw_versions(struct sfs_device *sfs_dev)
{
	/*
	 * SFS_GET_FW_VERSIONS command needs the output buffer to be
	 * initialized to 0xC7 in every byte.
	 */
	memset(sfs_dev->command_buf->sfs_buffer, 0xc7, PAGE_SIZE);
	sfs_dev->command_buf->hdr.payload_size = 2 * PAGE_SIZE;

	return send_sfs_cmd(sfs_dev, PSP_SFS_GET_FW_VERSIONS);
}

static int send_sfs_update_package(struct sfs_device *sfs_dev, const char *payload_name)
{
	char payload_path[PAYLOAD_NAME_SIZE + sizeof("amd/")];
	const struct firmware *firmware;
	unsigned long package_size;
	int ret;

	/* Sanitize userspace provided payload name */
	if (!strnchr(payload_name, PAYLOAD_NAME_SIZE, '\0'))
		return -EINVAL;

	snprintf(payload_path, sizeof(payload_path), "amd/%s", payload_name);

	ret = firmware_request_nowarn(&firmware, payload_path, sfs_dev->dev);
	if (ret < 0) {
		dev_warn_ratelimited(sfs_dev->dev, "firmware request failed for %s (%d)\n",
				     payload_path, ret);
		return -ENOENT;
	}

	/*
	 * SFS Update Package command's input buffer contains TEE_EXT_CMD_BUFFER
	 * followed by the Update Package and it should be 64KB aligned.
	 */
	package_size = ALIGN(firmware->size + PAGE_SIZE, 0x10000U);

	/*
	 * SFS command buffer is a pre-allocated 2MB buffer, fail update package
	 * if SFS payload is larger than the pre-allocated command buffer.
	 */
	if (package_size > SFS_MAX_PAYLOAD_SIZE) {
		dev_warn_ratelimited(sfs_dev->dev,
			 "SFS payload size %ld larger than maximum supported payload size of %u\n",
			 package_size, SFS_MAX_PAYLOAD_SIZE);
		release_firmware(firmware);
		return -E2BIG;
	}

	/*
	 * Copy firmware data to a HV_Fixed memory region.
	 */
	memcpy(sfs_dev->command_buf->sfs_buffer, firmware->data, firmware->size);
	sfs_dev->command_buf->hdr.payload_size = package_size;

	release_firmware(firmware);

	return send_sfs_cmd(sfs_dev, PSP_SFS_UPDATE);
}

static long sfs_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct sfs_user_get_fw_versions __user *sfs_get_fw_versions;
	struct sfs_user_update_package __user *sfs_update_package;
	struct psp_device *psp_master = psp_get_master_device();
	char payload_name[PAYLOAD_NAME_SIZE];
	struct sfs_device *sfs_dev;
	int ret = 0;

	if (!psp_master || !psp_master->sfs_data)
		return -ENODEV;

	sfs_dev = psp_master->sfs_data;

	guard(mutex)(&sfs_ioctl_mutex);

	switch (cmd) {
	case SFSIOCFWVERS:
		dev_dbg(sfs_dev->dev, "in SFSIOCFWVERS\n");

		sfs_get_fw_versions = (struct sfs_user_get_fw_versions __user *)arg;

		ret = send_sfs_get_fw_versions(sfs_dev);
		if (ret && ret != -EIO)
			return ret;

		/*
		 * Return SFS status and extended status back to userspace
		 * if PSP status indicated success or command error.
		 */
		if (copy_to_user(&sfs_get_fw_versions->blob, sfs_dev->command_buf->sfs_buffer,
				 PAGE_SIZE))
			return -EFAULT;
		if (copy_to_user(&sfs_get_fw_versions->sfs_status,
				 &sfs_dev->command_buf->hdr.status,
				 sizeof(sfs_get_fw_versions->sfs_status)))
			return -EFAULT;
		if (copy_to_user(&sfs_get_fw_versions->sfs_extended_status,
				 &sfs_dev->command_buf->buf,
				 sizeof(sfs_get_fw_versions->sfs_extended_status)))
			return -EFAULT;
		break;
	case SFSIOCUPDATEPKG:
		dev_dbg(sfs_dev->dev, "in SFSIOCUPDATEPKG\n");

		sfs_update_package = (struct sfs_user_update_package __user *)arg;

		if (copy_from_user(payload_name, sfs_update_package->payload_name,
				   PAYLOAD_NAME_SIZE))
			return -EFAULT;

		ret = send_sfs_update_package(sfs_dev, payload_name);
		if (ret && ret != -EIO)
			return ret;

		/*
		 * Return SFS status and extended status back to userspace
		 * if PSP status indicated success or command error.
		 */
		if (copy_to_user(&sfs_update_package->sfs_status,
				 &sfs_dev->command_buf->hdr.status,
				 sizeof(sfs_update_package->sfs_status)))
			return -EFAULT;
		if (copy_to_user(&sfs_update_package->sfs_extended_status,
				 &sfs_dev->command_buf->buf,
				 sizeof(sfs_update_package->sfs_extended_status)))
			return -EFAULT;
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static const struct file_operations sfs_fops = {
	.owner	= THIS_MODULE,
	.unlocked_ioctl = sfs_ioctl,
};

static void sfs_exit(struct kref *ref)
{
	misc_deregister(&misc_dev->misc);
	kfree(misc_dev);
	misc_dev = NULL;
}

void sfs_dev_destroy(struct psp_device *psp)
{
	struct sfs_device *sfs_dev = psp->sfs_data;

	if (!sfs_dev)
		return;

	/*
	 * Change SFS command buffer back to the default "Write-Back" type.
	 */
	set_memory_wb((unsigned long)sfs_dev->command_buf, SFS_NUM_PAGES_CMDBUF);

	snp_free_hv_fixed_pages(sfs_dev->page);

	if (sfs_dev->misc)
		kref_put(&misc_dev->refcount, sfs_exit);

	psp->sfs_data = NULL;
}

/* Based on sev_misc_init() */
static int sfs_misc_init(struct sfs_device *sfs)
{
	struct device *dev = sfs->dev;
	int ret;

	/*
	 * SFS feature support can be detected on multiple devices but the SFS
	 * FW commands must be issued on the master. During probe, we do not
	 * know the master hence we create /dev/sfs on the first device probe.
	 */
	if (!misc_dev) {
		struct miscdevice *misc;

		misc_dev = kzalloc(sizeof(*misc_dev), GFP_KERNEL);
		if (!misc_dev)
			return -ENOMEM;

		misc = &misc_dev->misc;
		misc->minor = MISC_DYNAMIC_MINOR;
		misc->name = "sfs";
		misc->fops = &sfs_fops;
		misc->mode = 0600;

		ret = misc_register(misc);
		if (ret)
			return ret;

		kref_init(&misc_dev->refcount);
	} else {
		kref_get(&misc_dev->refcount);
	}

	sfs->misc = misc_dev;
	dev_dbg(dev, "registered SFS device\n");

	return 0;
}

int sfs_dev_init(struct psp_device *psp)
{
	struct device *dev = psp->dev;
	struct sfs_device *sfs_dev;
	struct page *page;
	int ret = -ENOMEM;

	sfs_dev = devm_kzalloc(dev, sizeof(*sfs_dev), GFP_KERNEL);
	if (!sfs_dev)
		return -ENOMEM;

	/*
	 * Pre-allocate 2MB command buffer for all SFS commands using
	 * SNP HV_Fixed page allocator which also transitions the
	 * SFS command buffer to HV_Fixed page state if SNP is enabled.
	 */
	page = snp_alloc_hv_fixed_pages(SFS_NUM_2MB_PAGES_CMDBUF);
	if (!page) {
		dev_dbg(dev, "Command Buffer HV-Fixed page allocation failed\n");
		goto cleanup_dev;
	}
	sfs_dev->page = page;
	sfs_dev->command_buf = page_address(page);

	dev_dbg(dev, "Command buffer 0x%px to be marked as HV_Fixed\n", sfs_dev->command_buf);

	/*
	 * SFS command buffer must be mapped as non-cacheable.
	 */
	ret = set_memory_uc((unsigned long)sfs_dev->command_buf, SFS_NUM_PAGES_CMDBUF);
	if (ret) {
		dev_dbg(dev, "Set memory uc failed\n");
		goto cleanup_cmd_buf;
	}

	dev_dbg(dev, "Command buffer 0x%px marked uncacheable\n", sfs_dev->command_buf);

	psp->sfs_data = sfs_dev;
	sfs_dev->dev = dev;
	sfs_dev->psp = psp;

	ret = sfs_misc_init(sfs_dev);
	if (ret)
		goto cleanup_mem_attr;

	dev_notice(sfs_dev->dev, "SFS support is available\n");

	return 0;

cleanup_mem_attr:
	set_memory_wb((unsigned long)sfs_dev->command_buf, SFS_NUM_PAGES_CMDBUF);

cleanup_cmd_buf:
	snp_free_hv_fixed_pages(page);

cleanup_dev:
	psp->sfs_data = NULL;
	devm_kfree(dev, sfs_dev);

	return ret;
}
