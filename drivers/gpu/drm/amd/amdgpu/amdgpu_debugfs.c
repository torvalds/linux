/*
 * Copyright 2008 Advanced Micro Devices, Inc.
 * Copyright 2008 Red Hat Inc.
 * Copyright 2009 Jerome Glisse.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include <linux/kthread.h>
#include <linux/pci.h>
#include <linux/uaccess.h>
#include <linux/pm_runtime.h>
#include <linux/poll.h>

#include "amdgpu.h"
#include "amdgpu_pm.h"
#include "amdgpu_dm_debugfs.h"
#include "amdgpu_ras.h"
#include "amdgpu_rap.h"
#include "amdgpu_securedisplay.h"
#include "amdgpu_fw_attestation.h"

int amdgpu_debugfs_wait_dump(struct amdgpu_device *adev)
{
#if defined(CONFIG_DEBUG_FS)
	unsigned long timeout = 600 * HZ;
	int ret;

	wake_up_interruptible(&adev->autodump.gpu_hang);

	ret = wait_for_completion_interruptible_timeout(&adev->autodump.dumping, timeout);
	if (ret == 0) {
		pr_err("autodump: timeout, move on to gpu recovery\n");
		return -ETIMEDOUT;
	}
#endif
	return 0;
}

#if defined(CONFIG_DEBUG_FS)

static int amdgpu_debugfs_autodump_open(struct inode *inode, struct file *file)
{
	struct amdgpu_device *adev = inode->i_private;
	int ret;

	file->private_data = adev;

	ret = down_read_killable(&adev->reset_sem);
	if (ret)
		return ret;

	if (adev->autodump.dumping.done) {
		reinit_completion(&adev->autodump.dumping);
		ret = 0;
	} else {
		ret = -EBUSY;
	}

	up_read(&adev->reset_sem);

	return ret;
}

static int amdgpu_debugfs_autodump_release(struct inode *inode, struct file *file)
{
	struct amdgpu_device *adev = file->private_data;

	complete_all(&adev->autodump.dumping);
	return 0;
}

static unsigned int amdgpu_debugfs_autodump_poll(struct file *file, struct poll_table_struct *poll_table)
{
	struct amdgpu_device *adev = file->private_data;

	poll_wait(file, &adev->autodump.gpu_hang, poll_table);

	if (amdgpu_in_reset(adev))
		return POLLIN | POLLRDNORM | POLLWRNORM;

	return 0;
}

static const struct file_operations autodump_debug_fops = {
	.owner = THIS_MODULE,
	.open = amdgpu_debugfs_autodump_open,
	.poll = amdgpu_debugfs_autodump_poll,
	.release = amdgpu_debugfs_autodump_release,
};

static void amdgpu_debugfs_autodump_init(struct amdgpu_device *adev)
{
	init_completion(&adev->autodump.dumping);
	complete_all(&adev->autodump.dumping);
	init_waitqueue_head(&adev->autodump.gpu_hang);

	debugfs_create_file("amdgpu_autodump", 0600,
		adev_to_drm(adev)->primary->debugfs_root,
		adev, &autodump_debug_fops);
}

/**
 * amdgpu_debugfs_process_reg_op - Handle MMIO register reads/writes
 *
 * @read: True if reading
 * @f: open file handle
 * @buf: User buffer to write/read to
 * @size: Number of bytes to write/read
 * @pos:  Offset to seek to
 *
 * This debugfs entry has special meaning on the offset being sought.
 * Various bits have different meanings:
 *
 * Bit 62:  Indicates a GRBM bank switch is needed
 * Bit 61:  Indicates a SRBM bank switch is needed (implies bit 62 is
 * 	    zero)
 * Bits 24..33: The SE or ME selector if needed
 * Bits 34..43: The SH (or SA) or PIPE selector if needed
 * Bits 44..53: The INSTANCE (or CU/WGP) or QUEUE selector if needed
 *
 * Bit 23:  Indicates that the PM power gating lock should be held
 * 	    This is necessary to read registers that might be
 * 	    unreliable during a power gating transistion.
 *
 * The lower bits are the BYTE offset of the register to read.  This
 * allows reading multiple registers in a single call and having
 * the returned size reflect that.
 */
static int  amdgpu_debugfs_process_reg_op(bool read, struct file *f,
		char __user *buf, size_t size, loff_t *pos)
{
	struct amdgpu_device *adev = file_inode(f)->i_private;
	ssize_t result = 0;
	int r;
	bool pm_pg_lock, use_bank, use_ring;
	unsigned instance_bank, sh_bank, se_bank, me, pipe, queue, vmid;

	pm_pg_lock = use_bank = use_ring = false;
	instance_bank = sh_bank = se_bank = me = pipe = queue = vmid = 0;

	if (size & 0x3 || *pos & 0x3 ||
			((*pos & (1ULL << 62)) && (*pos & (1ULL << 61))))
		return -EINVAL;

	/* are we reading registers for which a PG lock is necessary? */
	pm_pg_lock = (*pos >> 23) & 1;

	if (*pos & (1ULL << 62)) {
		se_bank = (*pos & GENMASK_ULL(33, 24)) >> 24;
		sh_bank = (*pos & GENMASK_ULL(43, 34)) >> 34;
		instance_bank = (*pos & GENMASK_ULL(53, 44)) >> 44;

		if (se_bank == 0x3FF)
			se_bank = 0xFFFFFFFF;
		if (sh_bank == 0x3FF)
			sh_bank = 0xFFFFFFFF;
		if (instance_bank == 0x3FF)
			instance_bank = 0xFFFFFFFF;
		use_bank = true;
	} else if (*pos & (1ULL << 61)) {

		me = (*pos & GENMASK_ULL(33, 24)) >> 24;
		pipe = (*pos & GENMASK_ULL(43, 34)) >> 34;
		queue = (*pos & GENMASK_ULL(53, 44)) >> 44;
		vmid = (*pos & GENMASK_ULL(58, 54)) >> 54;

		use_ring = true;
	} else {
		use_bank = use_ring = false;
	}

	*pos &= (1UL << 22) - 1;

	r = pm_runtime_get_sync(adev_to_drm(adev)->dev);
	if (r < 0) {
		pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);
		return r;
	}

	r = amdgpu_virt_enable_access_debugfs(adev);
	if (r < 0) {
		pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);
		return r;
	}

	if (use_bank) {
		if ((sh_bank != 0xFFFFFFFF && sh_bank >= adev->gfx.config.max_sh_per_se) ||
		    (se_bank != 0xFFFFFFFF && se_bank >= adev->gfx.config.max_shader_engines)) {
			pm_runtime_mark_last_busy(adev_to_drm(adev)->dev);
			pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);
			amdgpu_virt_disable_access_debugfs(adev);
			return -EINVAL;
		}
		mutex_lock(&adev->grbm_idx_mutex);
		amdgpu_gfx_select_se_sh(adev, se_bank,
					sh_bank, instance_bank);
	} else if (use_ring) {
		mutex_lock(&adev->srbm_mutex);
		amdgpu_gfx_select_me_pipe_q(adev, me, pipe, queue, vmid);
	}

	if (pm_pg_lock)
		mutex_lock(&adev->pm.mutex);

	while (size) {
		uint32_t value;

		if (read) {
			value = RREG32(*pos >> 2);
			r = put_user(value, (uint32_t *)buf);
		} else {
			r = get_user(value, (uint32_t *)buf);
			if (!r)
				amdgpu_mm_wreg_mmio_rlc(adev, *pos >> 2, value);
		}
		if (r) {
			result = r;
			goto end;
		}

		result += 4;
		buf += 4;
		*pos += 4;
		size -= 4;
	}

end:
	if (use_bank) {
		amdgpu_gfx_select_se_sh(adev, 0xffffffff, 0xffffffff, 0xffffffff);
		mutex_unlock(&adev->grbm_idx_mutex);
	} else if (use_ring) {
		amdgpu_gfx_select_me_pipe_q(adev, 0, 0, 0, 0);
		mutex_unlock(&adev->srbm_mutex);
	}

	if (pm_pg_lock)
		mutex_unlock(&adev->pm.mutex);

	pm_runtime_mark_last_busy(adev_to_drm(adev)->dev);
	pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);

	amdgpu_virt_disable_access_debugfs(adev);
	return result;
}

/*
 * amdgpu_debugfs_regs_read - Callback for reading MMIO registers
 */
static ssize_t amdgpu_debugfs_regs_read(struct file *f, char __user *buf,
					size_t size, loff_t *pos)
{
	return amdgpu_debugfs_process_reg_op(true, f, buf, size, pos);
}

/*
 * amdgpu_debugfs_regs_write - Callback for writing MMIO registers
 */
static ssize_t amdgpu_debugfs_regs_write(struct file *f, const char __user *buf,
					 size_t size, loff_t *pos)
{
	return amdgpu_debugfs_process_reg_op(false, f, (char __user *)buf, size, pos);
}


/**
 * amdgpu_debugfs_regs_pcie_read - Read from a PCIE register
 *
 * @f: open file handle
 * @buf: User buffer to store read data in
 * @size: Number of bytes to read
 * @pos:  Offset to seek to
 *
 * The lower bits are the BYTE offset of the register to read.  This
 * allows reading multiple registers in a single call and having
 * the returned size reflect that.
 */
static ssize_t amdgpu_debugfs_regs_pcie_read(struct file *f, char __user *buf,
					size_t size, loff_t *pos)
{
	struct amdgpu_device *adev = file_inode(f)->i_private;
	ssize_t result = 0;
	int r;

	if (size & 0x3 || *pos & 0x3)
		return -EINVAL;

	r = pm_runtime_get_sync(adev_to_drm(adev)->dev);
	if (r < 0) {
		pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);
		return r;
	}

	r = amdgpu_virt_enable_access_debugfs(adev);
	if (r < 0) {
		pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);
		return r;
	}

	while (size) {
		uint32_t value;

		value = RREG32_PCIE(*pos);
		r = put_user(value, (uint32_t *)buf);
		if (r) {
			pm_runtime_mark_last_busy(adev_to_drm(adev)->dev);
			pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);
			amdgpu_virt_disable_access_debugfs(adev);
			return r;
		}

		result += 4;
		buf += 4;
		*pos += 4;
		size -= 4;
	}

	pm_runtime_mark_last_busy(adev_to_drm(adev)->dev);
	pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);

	amdgpu_virt_disable_access_debugfs(adev);
	return result;
}

/**
 * amdgpu_debugfs_regs_pcie_write - Write to a PCIE register
 *
 * @f: open file handle
 * @buf: User buffer to write data from
 * @size: Number of bytes to write
 * @pos:  Offset to seek to
 *
 * The lower bits are the BYTE offset of the register to write.  This
 * allows writing multiple registers in a single call and having
 * the returned size reflect that.
 */
static ssize_t amdgpu_debugfs_regs_pcie_write(struct file *f, const char __user *buf,
					 size_t size, loff_t *pos)
{
	struct amdgpu_device *adev = file_inode(f)->i_private;
	ssize_t result = 0;
	int r;

	if (size & 0x3 || *pos & 0x3)
		return -EINVAL;

	r = pm_runtime_get_sync(adev_to_drm(adev)->dev);
	if (r < 0) {
		pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);
		return r;
	}

	r = amdgpu_virt_enable_access_debugfs(adev);
	if (r < 0) {
		pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);
		return r;
	}

	while (size) {
		uint32_t value;

		r = get_user(value, (uint32_t *)buf);
		if (r) {
			pm_runtime_mark_last_busy(adev_to_drm(adev)->dev);
			pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);
			amdgpu_virt_disable_access_debugfs(adev);
			return r;
		}

		WREG32_PCIE(*pos, value);

		result += 4;
		buf += 4;
		*pos += 4;
		size -= 4;
	}

	pm_runtime_mark_last_busy(adev_to_drm(adev)->dev);
	pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);

	amdgpu_virt_disable_access_debugfs(adev);
	return result;
}

/**
 * amdgpu_debugfs_regs_didt_read - Read from a DIDT register
 *
 * @f: open file handle
 * @buf: User buffer to store read data in
 * @size: Number of bytes to read
 * @pos:  Offset to seek to
 *
 * The lower bits are the BYTE offset of the register to read.  This
 * allows reading multiple registers in a single call and having
 * the returned size reflect that.
 */
static ssize_t amdgpu_debugfs_regs_didt_read(struct file *f, char __user *buf,
					size_t size, loff_t *pos)
{
	struct amdgpu_device *adev = file_inode(f)->i_private;
	ssize_t result = 0;
	int r;

	if (size & 0x3 || *pos & 0x3)
		return -EINVAL;

	r = pm_runtime_get_sync(adev_to_drm(adev)->dev);
	if (r < 0) {
		pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);
		return r;
	}

	r = amdgpu_virt_enable_access_debugfs(adev);
	if (r < 0) {
		pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);
		return r;
	}

	while (size) {
		uint32_t value;

		value = RREG32_DIDT(*pos >> 2);
		r = put_user(value, (uint32_t *)buf);
		if (r) {
			pm_runtime_mark_last_busy(adev_to_drm(adev)->dev);
			pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);
			amdgpu_virt_disable_access_debugfs(adev);
			return r;
		}

		result += 4;
		buf += 4;
		*pos += 4;
		size -= 4;
	}

	pm_runtime_mark_last_busy(adev_to_drm(adev)->dev);
	pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);

	amdgpu_virt_disable_access_debugfs(adev);
	return result;
}

/**
 * amdgpu_debugfs_regs_didt_write - Write to a DIDT register
 *
 * @f: open file handle
 * @buf: User buffer to write data from
 * @size: Number of bytes to write
 * @pos:  Offset to seek to
 *
 * The lower bits are the BYTE offset of the register to write.  This
 * allows writing multiple registers in a single call and having
 * the returned size reflect that.
 */
static ssize_t amdgpu_debugfs_regs_didt_write(struct file *f, const char __user *buf,
					 size_t size, loff_t *pos)
{
	struct amdgpu_device *adev = file_inode(f)->i_private;
	ssize_t result = 0;
	int r;

	if (size & 0x3 || *pos & 0x3)
		return -EINVAL;

	r = pm_runtime_get_sync(adev_to_drm(adev)->dev);
	if (r < 0) {
		pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);
		return r;
	}

	r = amdgpu_virt_enable_access_debugfs(adev);
	if (r < 0) {
		pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);
		return r;
	}

	while (size) {
		uint32_t value;

		r = get_user(value, (uint32_t *)buf);
		if (r) {
			pm_runtime_mark_last_busy(adev_to_drm(adev)->dev);
			pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);
			amdgpu_virt_disable_access_debugfs(adev);
			return r;
		}

		WREG32_DIDT(*pos >> 2, value);

		result += 4;
		buf += 4;
		*pos += 4;
		size -= 4;
	}

	pm_runtime_mark_last_busy(adev_to_drm(adev)->dev);
	pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);

	amdgpu_virt_disable_access_debugfs(adev);
	return result;
}

/**
 * amdgpu_debugfs_regs_smc_read - Read from a SMC register
 *
 * @f: open file handle
 * @buf: User buffer to store read data in
 * @size: Number of bytes to read
 * @pos:  Offset to seek to
 *
 * The lower bits are the BYTE offset of the register to read.  This
 * allows reading multiple registers in a single call and having
 * the returned size reflect that.
 */
static ssize_t amdgpu_debugfs_regs_smc_read(struct file *f, char __user *buf,
					size_t size, loff_t *pos)
{
	struct amdgpu_device *adev = file_inode(f)->i_private;
	ssize_t result = 0;
	int r;

	if (size & 0x3 || *pos & 0x3)
		return -EINVAL;

	r = pm_runtime_get_sync(adev_to_drm(adev)->dev);
	if (r < 0) {
		pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);
		return r;
	}

	r = amdgpu_virt_enable_access_debugfs(adev);
	if (r < 0) {
		pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);
		return r;
	}

	while (size) {
		uint32_t value;

		value = RREG32_SMC(*pos);
		r = put_user(value, (uint32_t *)buf);
		if (r) {
			pm_runtime_mark_last_busy(adev_to_drm(adev)->dev);
			pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);
			amdgpu_virt_disable_access_debugfs(adev);
			return r;
		}

		result += 4;
		buf += 4;
		*pos += 4;
		size -= 4;
	}

	pm_runtime_mark_last_busy(adev_to_drm(adev)->dev);
	pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);

	amdgpu_virt_disable_access_debugfs(adev);
	return result;
}

/**
 * amdgpu_debugfs_regs_smc_write - Write to a SMC register
 *
 * @f: open file handle
 * @buf: User buffer to write data from
 * @size: Number of bytes to write
 * @pos:  Offset to seek to
 *
 * The lower bits are the BYTE offset of the register to write.  This
 * allows writing multiple registers in a single call and having
 * the returned size reflect that.
 */
static ssize_t amdgpu_debugfs_regs_smc_write(struct file *f, const char __user *buf,
					 size_t size, loff_t *pos)
{
	struct amdgpu_device *adev = file_inode(f)->i_private;
	ssize_t result = 0;
	int r;

	if (size & 0x3 || *pos & 0x3)
		return -EINVAL;

	r = pm_runtime_get_sync(adev_to_drm(adev)->dev);
	if (r < 0) {
		pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);
		return r;
	}

	r = amdgpu_virt_enable_access_debugfs(adev);
	if (r < 0) {
		pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);
		return r;
	}

	while (size) {
		uint32_t value;

		r = get_user(value, (uint32_t *)buf);
		if (r) {
			pm_runtime_mark_last_busy(adev_to_drm(adev)->dev);
			pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);
			amdgpu_virt_disable_access_debugfs(adev);
			return r;
		}

		WREG32_SMC(*pos, value);

		result += 4;
		buf += 4;
		*pos += 4;
		size -= 4;
	}

	pm_runtime_mark_last_busy(adev_to_drm(adev)->dev);
	pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);

	amdgpu_virt_disable_access_debugfs(adev);
	return result;
}

/**
 * amdgpu_debugfs_gca_config_read - Read from gfx config data
 *
 * @f: open file handle
 * @buf: User buffer to store read data in
 * @size: Number of bytes to read
 * @pos:  Offset to seek to
 *
 * This file is used to access configuration data in a somewhat
 * stable fashion.  The format is a series of DWORDs with the first
 * indicating which revision it is.  New content is appended to the
 * end so that older software can still read the data.
 */

static ssize_t amdgpu_debugfs_gca_config_read(struct file *f, char __user *buf,
					size_t size, loff_t *pos)
{
	struct amdgpu_device *adev = file_inode(f)->i_private;
	ssize_t result = 0;
	int r;
	uint32_t *config, no_regs = 0;

	if (size & 0x3 || *pos & 0x3)
		return -EINVAL;

	config = kmalloc_array(256, sizeof(*config), GFP_KERNEL);
	if (!config)
		return -ENOMEM;

	/* version, increment each time something is added */
	config[no_regs++] = 3;
	config[no_regs++] = adev->gfx.config.max_shader_engines;
	config[no_regs++] = adev->gfx.config.max_tile_pipes;
	config[no_regs++] = adev->gfx.config.max_cu_per_sh;
	config[no_regs++] = adev->gfx.config.max_sh_per_se;
	config[no_regs++] = adev->gfx.config.max_backends_per_se;
	config[no_regs++] = adev->gfx.config.max_texture_channel_caches;
	config[no_regs++] = adev->gfx.config.max_gprs;
	config[no_regs++] = adev->gfx.config.max_gs_threads;
	config[no_regs++] = adev->gfx.config.max_hw_contexts;
	config[no_regs++] = adev->gfx.config.sc_prim_fifo_size_frontend;
	config[no_regs++] = adev->gfx.config.sc_prim_fifo_size_backend;
	config[no_regs++] = adev->gfx.config.sc_hiz_tile_fifo_size;
	config[no_regs++] = adev->gfx.config.sc_earlyz_tile_fifo_size;
	config[no_regs++] = adev->gfx.config.num_tile_pipes;
	config[no_regs++] = adev->gfx.config.backend_enable_mask;
	config[no_regs++] = adev->gfx.config.mem_max_burst_length_bytes;
	config[no_regs++] = adev->gfx.config.mem_row_size_in_kb;
	config[no_regs++] = adev->gfx.config.shader_engine_tile_size;
	config[no_regs++] = adev->gfx.config.num_gpus;
	config[no_regs++] = adev->gfx.config.multi_gpu_tile_size;
	config[no_regs++] = adev->gfx.config.mc_arb_ramcfg;
	config[no_regs++] = adev->gfx.config.gb_addr_config;
	config[no_regs++] = adev->gfx.config.num_rbs;

	/* rev==1 */
	config[no_regs++] = adev->rev_id;
	config[no_regs++] = adev->pg_flags;
	config[no_regs++] = adev->cg_flags;

	/* rev==2 */
	config[no_regs++] = adev->family;
	config[no_regs++] = adev->external_rev_id;

	/* rev==3 */
	config[no_regs++] = adev->pdev->device;
	config[no_regs++] = adev->pdev->revision;
	config[no_regs++] = adev->pdev->subsystem_device;
	config[no_regs++] = adev->pdev->subsystem_vendor;

	while (size && (*pos < no_regs * 4)) {
		uint32_t value;

		value = config[*pos >> 2];
		r = put_user(value, (uint32_t *)buf);
		if (r) {
			kfree(config);
			return r;
		}

		result += 4;
		buf += 4;
		*pos += 4;
		size -= 4;
	}

	kfree(config);
	return result;
}

/**
 * amdgpu_debugfs_sensor_read - Read from the powerplay sensors
 *
 * @f: open file handle
 * @buf: User buffer to store read data in
 * @size: Number of bytes to read
 * @pos:  Offset to seek to
 *
 * The offset is treated as the BYTE address of one of the sensors
 * enumerated in amd/include/kgd_pp_interface.h under the
 * 'amd_pp_sensors' enumeration.  For instance to read the UVD VCLK
 * you would use the offset 3 * 4 = 12.
 */
static ssize_t amdgpu_debugfs_sensor_read(struct file *f, char __user *buf,
					size_t size, loff_t *pos)
{
	struct amdgpu_device *adev = file_inode(f)->i_private;
	int idx, x, outsize, r, valuesize;
	uint32_t values[16];

	if (size & 3 || *pos & 0x3)
		return -EINVAL;

	if (!adev->pm.dpm_enabled)
		return -EINVAL;

	/* convert offset to sensor number */
	idx = *pos >> 2;

	valuesize = sizeof(values);

	r = pm_runtime_get_sync(adev_to_drm(adev)->dev);
	if (r < 0) {
		pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);
		return r;
	}

	r = amdgpu_virt_enable_access_debugfs(adev);
	if (r < 0) {
		pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);
		return r;
	}

	r = amdgpu_dpm_read_sensor(adev, idx, &values[0], &valuesize);

	pm_runtime_mark_last_busy(adev_to_drm(adev)->dev);
	pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);

	if (r) {
		amdgpu_virt_disable_access_debugfs(adev);
		return r;
	}

	if (size > valuesize) {
		amdgpu_virt_disable_access_debugfs(adev);
		return -EINVAL;
	}

	outsize = 0;
	x = 0;
	if (!r) {
		while (size) {
			r = put_user(values[x++], (int32_t *)buf);
			buf += 4;
			size -= 4;
			outsize += 4;
		}
	}

	amdgpu_virt_disable_access_debugfs(adev);
	return !r ? outsize : r;
}

/** amdgpu_debugfs_wave_read - Read WAVE STATUS data
 *
 * @f: open file handle
 * @buf: User buffer to store read data in
 * @size: Number of bytes to read
 * @pos:  Offset to seek to
 *
 * The offset being sought changes which wave that the status data
 * will be returned for.  The bits are used as follows:
 *
 * Bits 0..6: 	Byte offset into data
 * Bits 7..14:	SE selector
 * Bits 15..22:	SH/SA selector
 * Bits 23..30: CU/{WGP+SIMD} selector
 * Bits 31..36: WAVE ID selector
 * Bits 37..44: SIMD ID selector
 *
 * The returned data begins with one DWORD of version information
 * Followed by WAVE STATUS registers relevant to the GFX IP version
 * being used.  See gfx_v8_0_read_wave_data() for an example output.
 */
static ssize_t amdgpu_debugfs_wave_read(struct file *f, char __user *buf,
					size_t size, loff_t *pos)
{
	struct amdgpu_device *adev = f->f_inode->i_private;
	int r, x;
	ssize_t result = 0;
	uint32_t offset, se, sh, cu, wave, simd, data[32];

	if (size & 3 || *pos & 3)
		return -EINVAL;

	/* decode offset */
	offset = (*pos & GENMASK_ULL(6, 0));
	se = (*pos & GENMASK_ULL(14, 7)) >> 7;
	sh = (*pos & GENMASK_ULL(22, 15)) >> 15;
	cu = (*pos & GENMASK_ULL(30, 23)) >> 23;
	wave = (*pos & GENMASK_ULL(36, 31)) >> 31;
	simd = (*pos & GENMASK_ULL(44, 37)) >> 37;

	r = pm_runtime_get_sync(adev_to_drm(adev)->dev);
	if (r < 0) {
		pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);
		return r;
	}

	r = amdgpu_virt_enable_access_debugfs(adev);
	if (r < 0) {
		pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);
		return r;
	}

	/* switch to the specific se/sh/cu */
	mutex_lock(&adev->grbm_idx_mutex);
	amdgpu_gfx_select_se_sh(adev, se, sh, cu);

	x = 0;
	if (adev->gfx.funcs->read_wave_data)
		adev->gfx.funcs->read_wave_data(adev, simd, wave, data, &x);

	amdgpu_gfx_select_se_sh(adev, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF);
	mutex_unlock(&adev->grbm_idx_mutex);

	pm_runtime_mark_last_busy(adev_to_drm(adev)->dev);
	pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);

	if (!x) {
		amdgpu_virt_disable_access_debugfs(adev);
		return -EINVAL;
	}

	while (size && (offset < x * 4)) {
		uint32_t value;

		value = data[offset >> 2];
		r = put_user(value, (uint32_t *)buf);
		if (r) {
			amdgpu_virt_disable_access_debugfs(adev);
			return r;
		}

		result += 4;
		buf += 4;
		offset += 4;
		size -= 4;
	}

	amdgpu_virt_disable_access_debugfs(adev);
	return result;
}

/** amdgpu_debugfs_gpr_read - Read wave gprs
 *
 * @f: open file handle
 * @buf: User buffer to store read data in
 * @size: Number of bytes to read
 * @pos:  Offset to seek to
 *
 * The offset being sought changes which wave that the status data
 * will be returned for.  The bits are used as follows:
 *
 * Bits 0..11:	Byte offset into data
 * Bits 12..19:	SE selector
 * Bits 20..27:	SH/SA selector
 * Bits 28..35: CU/{WGP+SIMD} selector
 * Bits 36..43: WAVE ID selector
 * Bits 37..44: SIMD ID selector
 * Bits 52..59: Thread selector
 * Bits 60..61: Bank selector (VGPR=0,SGPR=1)
 *
 * The return data comes from the SGPR or VGPR register bank for
 * the selected operational unit.
 */
static ssize_t amdgpu_debugfs_gpr_read(struct file *f, char __user *buf,
					size_t size, loff_t *pos)
{
	struct amdgpu_device *adev = f->f_inode->i_private;
	int r;
	ssize_t result = 0;
	uint32_t offset, se, sh, cu, wave, simd, thread, bank, *data;

	if (size > 4096 || size & 3 || *pos & 3)
		return -EINVAL;

	/* decode offset */
	offset = (*pos & GENMASK_ULL(11, 0)) >> 2;
	se = (*pos & GENMASK_ULL(19, 12)) >> 12;
	sh = (*pos & GENMASK_ULL(27, 20)) >> 20;
	cu = (*pos & GENMASK_ULL(35, 28)) >> 28;
	wave = (*pos & GENMASK_ULL(43, 36)) >> 36;
	simd = (*pos & GENMASK_ULL(51, 44)) >> 44;
	thread = (*pos & GENMASK_ULL(59, 52)) >> 52;
	bank = (*pos & GENMASK_ULL(61, 60)) >> 60;

	data = kcalloc(1024, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	r = pm_runtime_get_sync(adev_to_drm(adev)->dev);
	if (r < 0)
		goto err;

	r = amdgpu_virt_enable_access_debugfs(adev);
	if (r < 0)
		goto err;

	/* switch to the specific se/sh/cu */
	mutex_lock(&adev->grbm_idx_mutex);
	amdgpu_gfx_select_se_sh(adev, se, sh, cu);

	if (bank == 0) {
		if (adev->gfx.funcs->read_wave_vgprs)
			adev->gfx.funcs->read_wave_vgprs(adev, simd, wave, thread, offset, size>>2, data);
	} else {
		if (adev->gfx.funcs->read_wave_sgprs)
			adev->gfx.funcs->read_wave_sgprs(adev, simd, wave, offset, size>>2, data);
	}

	amdgpu_gfx_select_se_sh(adev, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF);
	mutex_unlock(&adev->grbm_idx_mutex);

	pm_runtime_mark_last_busy(adev_to_drm(adev)->dev);
	pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);

	while (size) {
		uint32_t value;

		value = data[result >> 2];
		r = put_user(value, (uint32_t *)buf);
		if (r) {
			amdgpu_virt_disable_access_debugfs(adev);
			goto err;
		}

		result += 4;
		buf += 4;
		size -= 4;
	}

	kfree(data);
	amdgpu_virt_disable_access_debugfs(adev);
	return result;

err:
	pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);
	kfree(data);
	return r;
}

/**
 * amdgpu_debugfs_gfxoff_write - Enable/disable GFXOFF
 *
 * @f: open file handle
 * @buf: User buffer to write data from
 * @size: Number of bytes to write
 * @pos:  Offset to seek to
 *
 * Write a 32-bit zero to disable or a 32-bit non-zero to enable
 */
static ssize_t amdgpu_debugfs_gfxoff_write(struct file *f, const char __user *buf,
					 size_t size, loff_t *pos)
{
	struct amdgpu_device *adev = file_inode(f)->i_private;
	ssize_t result = 0;
	int r;

	if (size & 0x3 || *pos & 0x3)
		return -EINVAL;

	r = pm_runtime_get_sync(adev_to_drm(adev)->dev);
	if (r < 0) {
		pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);
		return r;
	}

	while (size) {
		uint32_t value;

		r = get_user(value, (uint32_t *)buf);
		if (r) {
			pm_runtime_mark_last_busy(adev_to_drm(adev)->dev);
			pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);
			return r;
		}

		amdgpu_gfx_off_ctrl(adev, value ? true : false);

		result += 4;
		buf += 4;
		*pos += 4;
		size -= 4;
	}

	pm_runtime_mark_last_busy(adev_to_drm(adev)->dev);
	pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);

	return result;
}


/**
 * amdgpu_debugfs_gfxoff_read - read gfxoff status
 *
 * @f: open file handle
 * @buf: User buffer to store read data in
 * @size: Number of bytes to read
 * @pos:  Offset to seek to
 */
static ssize_t amdgpu_debugfs_gfxoff_read(struct file *f, char __user *buf,
					 size_t size, loff_t *pos)
{
	struct amdgpu_device *adev = file_inode(f)->i_private;
	ssize_t result = 0;
	int r;

	if (size & 0x3 || *pos & 0x3)
		return -EINVAL;

	r = pm_runtime_get_sync(adev_to_drm(adev)->dev);
	if (r < 0)
		return r;

	while (size) {
		uint32_t value;

		r = amdgpu_get_gfx_off_status(adev, &value);
		if (r) {
			pm_runtime_mark_last_busy(adev_to_drm(adev)->dev);
			pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);
			return r;
		}

		r = put_user(value, (uint32_t *)buf);
		if (r) {
			pm_runtime_mark_last_busy(adev_to_drm(adev)->dev);
			pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);
			return r;
		}

		result += 4;
		buf += 4;
		*pos += 4;
		size -= 4;
	}

	pm_runtime_mark_last_busy(adev_to_drm(adev)->dev);
	pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);

	return result;
}

static const struct file_operations amdgpu_debugfs_regs_fops = {
	.owner = THIS_MODULE,
	.read = amdgpu_debugfs_regs_read,
	.write = amdgpu_debugfs_regs_write,
	.llseek = default_llseek
};
static const struct file_operations amdgpu_debugfs_regs_didt_fops = {
	.owner = THIS_MODULE,
	.read = amdgpu_debugfs_regs_didt_read,
	.write = amdgpu_debugfs_regs_didt_write,
	.llseek = default_llseek
};
static const struct file_operations amdgpu_debugfs_regs_pcie_fops = {
	.owner = THIS_MODULE,
	.read = amdgpu_debugfs_regs_pcie_read,
	.write = amdgpu_debugfs_regs_pcie_write,
	.llseek = default_llseek
};
static const struct file_operations amdgpu_debugfs_regs_smc_fops = {
	.owner = THIS_MODULE,
	.read = amdgpu_debugfs_regs_smc_read,
	.write = amdgpu_debugfs_regs_smc_write,
	.llseek = default_llseek
};

static const struct file_operations amdgpu_debugfs_gca_config_fops = {
	.owner = THIS_MODULE,
	.read = amdgpu_debugfs_gca_config_read,
	.llseek = default_llseek
};

static const struct file_operations amdgpu_debugfs_sensors_fops = {
	.owner = THIS_MODULE,
	.read = amdgpu_debugfs_sensor_read,
	.llseek = default_llseek
};

static const struct file_operations amdgpu_debugfs_wave_fops = {
	.owner = THIS_MODULE,
	.read = amdgpu_debugfs_wave_read,
	.llseek = default_llseek
};
static const struct file_operations amdgpu_debugfs_gpr_fops = {
	.owner = THIS_MODULE,
	.read = amdgpu_debugfs_gpr_read,
	.llseek = default_llseek
};

static const struct file_operations amdgpu_debugfs_gfxoff_fops = {
	.owner = THIS_MODULE,
	.read = amdgpu_debugfs_gfxoff_read,
	.write = amdgpu_debugfs_gfxoff_write,
	.llseek = default_llseek
};

static const struct file_operations *debugfs_regs[] = {
	&amdgpu_debugfs_regs_fops,
	&amdgpu_debugfs_regs_didt_fops,
	&amdgpu_debugfs_regs_pcie_fops,
	&amdgpu_debugfs_regs_smc_fops,
	&amdgpu_debugfs_gca_config_fops,
	&amdgpu_debugfs_sensors_fops,
	&amdgpu_debugfs_wave_fops,
	&amdgpu_debugfs_gpr_fops,
	&amdgpu_debugfs_gfxoff_fops,
};

static const char *debugfs_regs_names[] = {
	"amdgpu_regs",
	"amdgpu_regs_didt",
	"amdgpu_regs_pcie",
	"amdgpu_regs_smc",
	"amdgpu_gca_config",
	"amdgpu_sensors",
	"amdgpu_wave",
	"amdgpu_gpr",
	"amdgpu_gfxoff",
};

/**
 * amdgpu_debugfs_regs_init -	Initialize debugfs entries that provide
 * 				register access.
 *
 * @adev: The device to attach the debugfs entries to
 */
int amdgpu_debugfs_regs_init(struct amdgpu_device *adev)
{
	struct drm_minor *minor = adev_to_drm(adev)->primary;
	struct dentry *ent, *root = minor->debugfs_root;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(debugfs_regs); i++) {
		ent = debugfs_create_file(debugfs_regs_names[i],
					  S_IFREG | S_IRUGO, root,
					  adev, debugfs_regs[i]);
		if (!i && !IS_ERR_OR_NULL(ent))
			i_size_write(ent->d_inode, adev->rmmio_size);
	}

	return 0;
}

static int amdgpu_debugfs_test_ib_show(struct seq_file *m, void *unused)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)m->private;
	struct drm_device *dev = adev_to_drm(adev);
	int r = 0, i;

	r = pm_runtime_get_sync(dev->dev);
	if (r < 0) {
		pm_runtime_put_autosuspend(dev->dev);
		return r;
	}

	/* Avoid accidently unparking the sched thread during GPU reset */
	r = down_read_killable(&adev->reset_sem);
	if (r)
		return r;

	/* hold on the scheduler */
	for (i = 0; i < AMDGPU_MAX_RINGS; i++) {
		struct amdgpu_ring *ring = adev->rings[i];

		if (!ring || !ring->sched.thread)
			continue;
		kthread_park(ring->sched.thread);
	}

	seq_printf(m, "run ib test:\n");
	r = amdgpu_ib_ring_tests(adev);
	if (r)
		seq_printf(m, "ib ring tests failed (%d).\n", r);
	else
		seq_printf(m, "ib ring tests passed.\n");

	/* go on the scheduler */
	for (i = 0; i < AMDGPU_MAX_RINGS; i++) {
		struct amdgpu_ring *ring = adev->rings[i];

		if (!ring || !ring->sched.thread)
			continue;
		kthread_unpark(ring->sched.thread);
	}

	up_read(&adev->reset_sem);

	pm_runtime_mark_last_busy(dev->dev);
	pm_runtime_put_autosuspend(dev->dev);

	return 0;
}

static int amdgpu_debugfs_evict_vram(void *data, u64 *val)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)data;
	struct drm_device *dev = adev_to_drm(adev);
	int r;

	r = pm_runtime_get_sync(dev->dev);
	if (r < 0) {
		pm_runtime_put_autosuspend(dev->dev);
		return r;
	}

	*val = amdgpu_bo_evict_vram(adev);

	pm_runtime_mark_last_busy(dev->dev);
	pm_runtime_put_autosuspend(dev->dev);

	return 0;
}


static int amdgpu_debugfs_evict_gtt(void *data, u64 *val)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)data;
	struct drm_device *dev = adev_to_drm(adev);
	struct ttm_resource_manager *man;
	int r;

	r = pm_runtime_get_sync(dev->dev);
	if (r < 0) {
		pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);
		return r;
	}

	man = ttm_manager_type(&adev->mman.bdev, TTM_PL_TT);
	*val = ttm_resource_manager_evict_all(&adev->mman.bdev, man);

	pm_runtime_mark_last_busy(dev->dev);
	pm_runtime_put_autosuspend(dev->dev);

	return 0;
}


static int amdgpu_debugfs_vm_info_show(struct seq_file *m, void *unused)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)m->private;
	struct drm_device *dev = adev_to_drm(adev);
	struct drm_file *file;
	int r;

	r = mutex_lock_interruptible(&dev->filelist_mutex);
	if (r)
		return r;

	list_for_each_entry(file, &dev->filelist, lhead) {
		struct amdgpu_fpriv *fpriv = file->driver_priv;
		struct amdgpu_vm *vm = &fpriv->vm;

		seq_printf(m, "pid:%d\tProcess:%s ----------\n",
				vm->task_info.pid, vm->task_info.process_name);
		r = amdgpu_bo_reserve(vm->root.bo, true);
		if (r)
			break;
		amdgpu_debugfs_vm_bo_info(vm, m);
		amdgpu_bo_unreserve(vm->root.bo);
	}

	mutex_unlock(&dev->filelist_mutex);

	return r;
}

DEFINE_SHOW_ATTRIBUTE(amdgpu_debugfs_test_ib);
DEFINE_SHOW_ATTRIBUTE(amdgpu_debugfs_vm_info);
DEFINE_DEBUGFS_ATTRIBUTE(amdgpu_evict_vram_fops, amdgpu_debugfs_evict_vram,
			 NULL, "%lld\n");
DEFINE_DEBUGFS_ATTRIBUTE(amdgpu_evict_gtt_fops, amdgpu_debugfs_evict_gtt,
			 NULL, "%lld\n");

static void amdgpu_ib_preempt_fences_swap(struct amdgpu_ring *ring,
					  struct dma_fence **fences)
{
	struct amdgpu_fence_driver *drv = &ring->fence_drv;
	uint32_t sync_seq, last_seq;

	last_seq = atomic_read(&ring->fence_drv.last_seq);
	sync_seq = ring->fence_drv.sync_seq;

	last_seq &= drv->num_fences_mask;
	sync_seq &= drv->num_fences_mask;

	do {
		struct dma_fence *fence, **ptr;

		++last_seq;
		last_seq &= drv->num_fences_mask;
		ptr = &drv->fences[last_seq];

		fence = rcu_dereference_protected(*ptr, 1);
		RCU_INIT_POINTER(*ptr, NULL);

		if (!fence)
			continue;

		fences[last_seq] = fence;

	} while (last_seq != sync_seq);
}

static void amdgpu_ib_preempt_signal_fences(struct dma_fence **fences,
					    int length)
{
	int i;
	struct dma_fence *fence;

	for (i = 0; i < length; i++) {
		fence = fences[i];
		if (!fence)
			continue;
		dma_fence_signal(fence);
		dma_fence_put(fence);
	}
}

static void amdgpu_ib_preempt_job_recovery(struct drm_gpu_scheduler *sched)
{
	struct drm_sched_job *s_job;
	struct dma_fence *fence;

	spin_lock(&sched->job_list_lock);
	list_for_each_entry(s_job, &sched->pending_list, list) {
		fence = sched->ops->run_job(s_job);
		dma_fence_put(fence);
	}
	spin_unlock(&sched->job_list_lock);
}

static void amdgpu_ib_preempt_mark_partial_job(struct amdgpu_ring *ring)
{
	struct amdgpu_job *job;
	struct drm_sched_job *s_job, *tmp;
	uint32_t preempt_seq;
	struct dma_fence *fence, **ptr;
	struct amdgpu_fence_driver *drv = &ring->fence_drv;
	struct drm_gpu_scheduler *sched = &ring->sched;
	bool preempted = true;

	if (ring->funcs->type != AMDGPU_RING_TYPE_GFX)
		return;

	preempt_seq = le32_to_cpu(*(drv->cpu_addr + 2));
	if (preempt_seq <= atomic_read(&drv->last_seq)) {
		preempted = false;
		goto no_preempt;
	}

	preempt_seq &= drv->num_fences_mask;
	ptr = &drv->fences[preempt_seq];
	fence = rcu_dereference_protected(*ptr, 1);

no_preempt:
	spin_lock(&sched->job_list_lock);
	list_for_each_entry_safe(s_job, tmp, &sched->pending_list, list) {
		if (dma_fence_is_signaled(&s_job->s_fence->finished)) {
			/* remove job from ring_mirror_list */
			list_del_init(&s_job->list);
			sched->ops->free_job(s_job);
			continue;
		}
		job = to_amdgpu_job(s_job);
		if (preempted && job->fence == fence)
			/* mark the job as preempted */
			job->preemption_status |= AMDGPU_IB_PREEMPTED;
	}
	spin_unlock(&sched->job_list_lock);
}

static int amdgpu_debugfs_ib_preempt(void *data, u64 val)
{
	int r, resched, length;
	struct amdgpu_ring *ring;
	struct dma_fence **fences = NULL;
	struct amdgpu_device *adev = (struct amdgpu_device *)data;

	if (val >= AMDGPU_MAX_RINGS)
		return -EINVAL;

	ring = adev->rings[val];

	if (!ring || !ring->funcs->preempt_ib || !ring->sched.thread)
		return -EINVAL;

	/* the last preemption failed */
	if (ring->trail_seq != le32_to_cpu(*ring->trail_fence_cpu_addr))
		return -EBUSY;

	length = ring->fence_drv.num_fences_mask + 1;
	fences = kcalloc(length, sizeof(void *), GFP_KERNEL);
	if (!fences)
		return -ENOMEM;

	/* Avoid accidently unparking the sched thread during GPU reset */
	r = down_read_killable(&adev->reset_sem);
	if (r)
		goto pro_end;

	/* stop the scheduler */
	kthread_park(ring->sched.thread);

	resched = ttm_bo_lock_delayed_workqueue(&adev->mman.bdev);

	/* preempt the IB */
	r = amdgpu_ring_preempt_ib(ring);
	if (r) {
		DRM_WARN("failed to preempt ring %d\n", ring->idx);
		goto failure;
	}

	amdgpu_fence_process(ring);

	if (atomic_read(&ring->fence_drv.last_seq) !=
	    ring->fence_drv.sync_seq) {
		DRM_INFO("ring %d was preempted\n", ring->idx);

		amdgpu_ib_preempt_mark_partial_job(ring);

		/* swap out the old fences */
		amdgpu_ib_preempt_fences_swap(ring, fences);

		amdgpu_fence_driver_force_completion(ring);

		/* resubmit unfinished jobs */
		amdgpu_ib_preempt_job_recovery(&ring->sched);

		/* wait for jobs finished */
		amdgpu_fence_wait_empty(ring);

		/* signal the old fences */
		amdgpu_ib_preempt_signal_fences(fences, length);
	}

failure:
	/* restart the scheduler */
	kthread_unpark(ring->sched.thread);

	up_read(&adev->reset_sem);

	ttm_bo_unlock_delayed_workqueue(&adev->mman.bdev, resched);

pro_end:
	kfree(fences);

	return r;
}

static int amdgpu_debugfs_sclk_set(void *data, u64 val)
{
	int ret = 0;
	uint32_t max_freq, min_freq;
	struct amdgpu_device *adev = (struct amdgpu_device *)data;

	if (amdgpu_sriov_vf(adev) && !amdgpu_sriov_is_pp_one_vf(adev))
		return -EINVAL;

	ret = pm_runtime_get_sync(adev_to_drm(adev)->dev);
	if (ret < 0) {
		pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);
		return ret;
	}

	if (is_support_sw_smu(adev)) {
		ret = smu_get_dpm_freq_range(&adev->smu, SMU_SCLK, &min_freq, &max_freq);
		if (ret || val > max_freq || val < min_freq)
			return -EINVAL;
		ret = smu_set_soft_freq_range(&adev->smu, SMU_SCLK, (uint32_t)val, (uint32_t)val);
	} else {
		return 0;
	}

	pm_runtime_mark_last_busy(adev_to_drm(adev)->dev);
	pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);

	if (ret)
		return -EINVAL;

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_ib_preempt, NULL,
			amdgpu_debugfs_ib_preempt, "%llu\n");

DEFINE_DEBUGFS_ATTRIBUTE(fops_sclk_set, NULL,
			amdgpu_debugfs_sclk_set, "%llu\n");

int amdgpu_debugfs_init(struct amdgpu_device *adev)
{
	struct dentry *root = adev_to_drm(adev)->primary->debugfs_root;
	struct dentry *ent;
	int r, i;



	ent = debugfs_create_file("amdgpu_preempt_ib", 0600, root, adev,
				  &fops_ib_preempt);
	if (!ent) {
		DRM_ERROR("unable to create amdgpu_preempt_ib debugsfs file\n");
		return -EIO;
	}

	ent = debugfs_create_file("amdgpu_force_sclk", 0200, root, adev,
				  &fops_sclk_set);
	if (!ent) {
		DRM_ERROR("unable to create amdgpu_set_sclk debugsfs file\n");
		return -EIO;
	}

	/* Register debugfs entries for amdgpu_ttm */
	amdgpu_ttm_debugfs_init(adev);
	amdgpu_debugfs_pm_init(adev);
	amdgpu_debugfs_sa_init(adev);
	amdgpu_debugfs_fence_init(adev);
	amdgpu_debugfs_gem_init(adev);

	r = amdgpu_debugfs_regs_init(adev);
	if (r)
		DRM_ERROR("registering register debugfs failed (%d).\n", r);

	amdgpu_debugfs_firmware_init(adev);

#if defined(CONFIG_DRM_AMD_DC)
	if (amdgpu_device_has_dc_support(adev))
		dtn_debugfs_init(adev);
#endif

	for (i = 0; i < AMDGPU_MAX_RINGS; ++i) {
		struct amdgpu_ring *ring = adev->rings[i];

		if (!ring)
			continue;

		if (amdgpu_debugfs_ring_init(adev, ring)) {
			DRM_ERROR("Failed to register debugfs file for rings !\n");
		}
	}

	amdgpu_ras_debugfs_create_all(adev);
	amdgpu_debugfs_autodump_init(adev);
	amdgpu_rap_debugfs_init(adev);
	amdgpu_securedisplay_debugfs_init(adev);
	amdgpu_fw_attestation_debugfs_init(adev);

	debugfs_create_file("amdgpu_evict_vram", 0444, root, adev,
			    &amdgpu_evict_vram_fops);
	debugfs_create_file("amdgpu_evict_gtt", 0444, root, adev,
			    &amdgpu_evict_gtt_fops);
	debugfs_create_file("amdgpu_test_ib", 0444, root, adev,
			    &amdgpu_debugfs_test_ib_fops);
	debugfs_create_file("amdgpu_vm_info", 0444, root, adev,
			    &amdgpu_debugfs_vm_info_fops);

	adev->debugfs_vbios_blob.data = adev->bios;
	adev->debugfs_vbios_blob.size = adev->bios_size;
	debugfs_create_blob("amdgpu_vbios", 0444, root,
			    &adev->debugfs_vbios_blob);

	return 0;
}

#else
int amdgpu_debugfs_init(struct amdgpu_device *adev)
{
	return 0;
}
int amdgpu_debugfs_regs_init(struct amdgpu_device *adev)
{
	return 0;
}
#endif
