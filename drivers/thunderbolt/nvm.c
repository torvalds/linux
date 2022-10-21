// SPDX-License-Identifier: GPL-2.0
/*
 * NVM helpers
 *
 * Copyright (C) 2020, Intel Corporation
 * Author: Mika Westerberg <mika.westerberg@linux.intel.com>
 */

#include <linux/idr.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

#include "tb.h"

/* Intel specific NVM offsets */
#define INTEL_NVM_DEVID			0x05
#define INTEL_NVM_VERSION		0x08
#define INTEL_NVM_CSS			0x10
#define INTEL_NVM_FLASH_SIZE		0x45

/* ASMedia specific NVM offsets */
#define ASMEDIA_NVM_DATE		0x1c
#define ASMEDIA_NVM_VERSION		0x28

static DEFINE_IDA(nvm_ida);

/**
 * struct tb_nvm_vendor_ops - Vendor specific NVM operations
 * @read_version: Reads out NVM version from the flash
 * @validate: Validates the NVM image before update (optional)
 * @write_headers: Writes headers before the rest of the image (optional)
 */
struct tb_nvm_vendor_ops {
	int (*read_version)(struct tb_nvm *nvm);
	int (*validate)(struct tb_nvm *nvm);
	int (*write_headers)(struct tb_nvm *nvm);
};

/**
 * struct tb_nvm_vendor - Vendor to &struct tb_nvm_vendor_ops mapping
 * @vendor: Vendor ID
 * @vops: Vendor specific NVM operations
 *
 * Maps vendor ID to NVM vendor operations. If there is no mapping then
 * NVM firmware upgrade is disabled for the device.
 */
struct tb_nvm_vendor {
	u16 vendor;
	const struct tb_nvm_vendor_ops *vops;
};

static int intel_switch_nvm_version(struct tb_nvm *nvm)
{
	struct tb_switch *sw = tb_to_switch(nvm->dev);
	u32 val, nvm_size, hdr_size;
	int ret;

	/*
	 * If the switch is in safe-mode the only accessible portion of
	 * the NVM is the non-active one where userspace is expected to
	 * write new functional NVM.
	 */
	if (sw->safe_mode)
		return 0;

	ret = tb_switch_nvm_read(sw, INTEL_NVM_FLASH_SIZE, &val, sizeof(val));
	if (ret)
		return ret;

	hdr_size = sw->generation < 3 ? SZ_8K : SZ_16K;
	nvm_size = (SZ_1M << (val & 7)) / 8;
	nvm_size = (nvm_size - hdr_size) / 2;

	ret = tb_switch_nvm_read(sw, INTEL_NVM_VERSION, &val, sizeof(val));
	if (ret)
		return ret;

	nvm->major = (val >> 16) & 0xff;
	nvm->minor = (val >> 8) & 0xff;
	nvm->active_size = nvm_size;

	return 0;
}

static int intel_switch_nvm_validate(struct tb_nvm *nvm)
{
	struct tb_switch *sw = tb_to_switch(nvm->dev);
	unsigned int image_size, hdr_size;
	u16 ds_size, device_id;
	u8 *buf = nvm->buf;

	image_size = nvm->buf_data_size;

	/*
	 * FARB pointer must point inside the image and must at least
	 * contain parts of the digital section we will be reading here.
	 */
	hdr_size = (*(u32 *)buf) & 0xffffff;
	if (hdr_size + INTEL_NVM_DEVID + 2 >= image_size)
		return -EINVAL;

	/* Digital section start should be aligned to 4k page */
	if (!IS_ALIGNED(hdr_size, SZ_4K))
		return -EINVAL;

	/*
	 * Read digital section size and check that it also fits inside
	 * the image.
	 */
	ds_size = *(u16 *)(buf + hdr_size);
	if (ds_size >= image_size)
		return -EINVAL;

	if (sw->safe_mode)
		return 0;

	/*
	 * Make sure the device ID in the image matches the one
	 * we read from the switch config space.
	 */
	device_id = *(u16 *)(buf + hdr_size + INTEL_NVM_DEVID);
	if (device_id != sw->config.device_id)
		return -EINVAL;

	/* Skip headers in the image */
	nvm->buf_data_start = buf + hdr_size;
	nvm->buf_data_size = image_size - hdr_size;

	return 0;
}

static int intel_switch_nvm_write_headers(struct tb_nvm *nvm)
{
	struct tb_switch *sw = tb_to_switch(nvm->dev);

	if (sw->generation < 3) {
		int ret;

		/* Write CSS headers first */
		ret = dma_port_flash_write(sw->dma_port,
			DMA_PORT_CSS_ADDRESS, nvm->buf + INTEL_NVM_CSS,
			DMA_PORT_CSS_MAX_SIZE);
		if (ret)
			return ret;
	}

	return 0;
}

static const struct tb_nvm_vendor_ops intel_switch_nvm_ops = {
	.read_version = intel_switch_nvm_version,
	.validate = intel_switch_nvm_validate,
	.write_headers = intel_switch_nvm_write_headers,
};

static int asmedia_switch_nvm_version(struct tb_nvm *nvm)
{
	struct tb_switch *sw = tb_to_switch(nvm->dev);
	u32 val;
	int ret;

	ret = tb_switch_nvm_read(sw, ASMEDIA_NVM_VERSION, &val, sizeof(val));
	if (ret)
		return ret;

	nvm->major = (val << 16) & 0xff0000;
	nvm->major |= val & 0x00ff00;
	nvm->major |= (val >> 16) & 0x0000ff;

	ret = tb_switch_nvm_read(sw, ASMEDIA_NVM_DATE, &val, sizeof(val));
	if (ret)
		return ret;

	nvm->minor = (val << 16) & 0xff0000;
	nvm->minor |= val & 0x00ff00;
	nvm->minor |= (val >> 16) & 0x0000ff;

	/* ASMedia NVM size is fixed to 512k */
	nvm->active_size = SZ_512K;

	return 0;
}

static const struct tb_nvm_vendor_ops asmedia_switch_nvm_ops = {
	.read_version = asmedia_switch_nvm_version,
};

/* Router vendor NVM support table */
static const struct tb_nvm_vendor switch_nvm_vendors[] = {
	{ 0x174c, &asmedia_switch_nvm_ops },
	{ PCI_VENDOR_ID_INTEL, &intel_switch_nvm_ops },
	{ 0x8087, &intel_switch_nvm_ops },
};

static int intel_retimer_nvm_version(struct tb_nvm *nvm)
{
	struct tb_retimer *rt = tb_to_retimer(nvm->dev);
	u32 val, nvm_size;
	int ret;

	ret = tb_retimer_nvm_read(rt, INTEL_NVM_VERSION, &val, sizeof(val));
	if (ret)
		return ret;

	nvm->major = (val >> 16) & 0xff;
	nvm->minor = (val >> 8) & 0xff;

	ret = tb_retimer_nvm_read(rt, INTEL_NVM_FLASH_SIZE, &val, sizeof(val));
	if (ret)
		return ret;

	nvm_size = (SZ_1M << (val & 7)) / 8;
	nvm_size = (nvm_size - SZ_16K) / 2;
	nvm->active_size = nvm_size;

	return 0;
}

static int intel_retimer_nvm_validate(struct tb_nvm *nvm)
{
	struct tb_retimer *rt = tb_to_retimer(nvm->dev);
	unsigned int image_size, hdr_size;
	u8 *buf = nvm->buf;
	u16 ds_size, device;

	image_size = nvm->buf_data_size;

	/*
	 * FARB pointer must point inside the image and must at least
	 * contain parts of the digital section we will be reading here.
	 */
	hdr_size = (*(u32 *)buf) & 0xffffff;
	if (hdr_size + INTEL_NVM_DEVID + 2 >= image_size)
		return -EINVAL;

	/* Digital section start should be aligned to 4k page */
	if (!IS_ALIGNED(hdr_size, SZ_4K))
		return -EINVAL;

	/*
	 * Read digital section size and check that it also fits inside
	 * the image.
	 */
	ds_size = *(u16 *)(buf + hdr_size);
	if (ds_size >= image_size)
		return -EINVAL;

	/*
	 * Make sure the device ID in the image matches the retimer
	 * hardware.
	 */
	device = *(u16 *)(buf + hdr_size + INTEL_NVM_DEVID);
	if (device != rt->device)
		return -EINVAL;

	/* Skip headers in the image */
	nvm->buf_data_start = buf + hdr_size;
	nvm->buf_data_size = image_size - hdr_size;

	return 0;
}

static const struct tb_nvm_vendor_ops intel_retimer_nvm_ops = {
	.read_version = intel_retimer_nvm_version,
	.validate = intel_retimer_nvm_validate,
};

/* Retimer vendor NVM support table */
static const struct tb_nvm_vendor retimer_nvm_vendors[] = {
	{ 0x8087, &intel_retimer_nvm_ops },
};

/**
 * tb_nvm_alloc() - Allocate new NVM structure
 * @dev: Device owning the NVM
 *
 * Allocates new NVM structure with unique @id and returns it. In case
 * of error returns ERR_PTR(). Specifically returns %-EOPNOTSUPP if the
 * NVM format of the @dev is not known by the kernel.
 */
struct tb_nvm *tb_nvm_alloc(struct device *dev)
{
	const struct tb_nvm_vendor_ops *vops = NULL;
	struct tb_nvm *nvm;
	int ret, i;

	if (tb_is_switch(dev)) {
		const struct tb_switch *sw = tb_to_switch(dev);

		for (i = 0; i < ARRAY_SIZE(switch_nvm_vendors); i++) {
			const struct tb_nvm_vendor *v = &switch_nvm_vendors[i];

			if (v->vendor == sw->config.vendor_id) {
				vops = v->vops;
				break;
			}
		}

		if (!vops) {
			tb_sw_dbg(sw, "router NVM format of vendor %#x unknown\n",
				  sw->config.vendor_id);
			return ERR_PTR(-EOPNOTSUPP);
		}
	} else if (tb_is_retimer(dev)) {
		const struct tb_retimer *rt = tb_to_retimer(dev);

		for (i = 0; i < ARRAY_SIZE(retimer_nvm_vendors); i++) {
			const struct tb_nvm_vendor *v = &retimer_nvm_vendors[i];

			if (v->vendor == rt->vendor) {
				vops = v->vops;
				break;
			}
		}

		if (!vops) {
			dev_dbg(dev, "retimer NVM format of vendor %#x unknown\n",
				rt->vendor);
			return ERR_PTR(-EOPNOTSUPP);
		}
	} else {
		return ERR_PTR(-EOPNOTSUPP);
	}

	nvm = kzalloc(sizeof(*nvm), GFP_KERNEL);
	if (!nvm)
		return ERR_PTR(-ENOMEM);

	ret = ida_simple_get(&nvm_ida, 0, 0, GFP_KERNEL);
	if (ret < 0) {
		kfree(nvm);
		return ERR_PTR(ret);
	}

	nvm->id = ret;
	nvm->dev = dev;
	nvm->vops = vops;

	return nvm;
}

/**
 * tb_nvm_read_version() - Read and populate NVM version
 * @nvm: NVM structure
 *
 * Uses vendor specific means to read out and fill in the existing
 * active NVM version. Returns %0 in case of success and negative errno
 * otherwise.
 */
int tb_nvm_read_version(struct tb_nvm *nvm)
{
	const struct tb_nvm_vendor_ops *vops = nvm->vops;

	if (vops && vops->read_version)
		return vops->read_version(nvm);

	return -EOPNOTSUPP;
}

/**
 * tb_nvm_validate() - Validate new NVM image
 * @nvm: NVM structure
 *
 * Runs vendor specific validation over the new NVM image and if all
 * checks pass returns %0. As side effect updates @nvm->buf_data_start
 * and @nvm->buf_data_size fields to match the actual data to be written
 * to the NVM.
 *
 * If the validation does not pass then returns negative errno.
 */
int tb_nvm_validate(struct tb_nvm *nvm)
{
	const struct tb_nvm_vendor_ops *vops = nvm->vops;
	unsigned int image_size;
	u8 *buf = nvm->buf;

	if (!buf)
		return -EINVAL;
	if (!vops)
		return -EOPNOTSUPP;

	/* Just do basic image size checks */
	image_size = nvm->buf_data_size;
	if (image_size < NVM_MIN_SIZE || image_size > NVM_MAX_SIZE)
		return -EINVAL;

	/*
	 * Set the default data start in the buffer. The validate method
	 * below can change this if needed.
	 */
	nvm->buf_data_start = buf;

	return vops->validate ? vops->validate(nvm) : 0;
}

/**
 * tb_nvm_write_headers() - Write headers before the rest of the image
 * @nvm: NVM structure
 *
 * If the vendor NVM format requires writing headers before the rest of
 * the image, this function does that. Can be called even if the device
 * does not need this.
 *
 * Returns %0 in case of success and negative errno otherwise.
 */
int tb_nvm_write_headers(struct tb_nvm *nvm)
{
	const struct tb_nvm_vendor_ops *vops = nvm->vops;

	return vops->write_headers ? vops->write_headers(nvm) : 0;
}

/**
 * tb_nvm_add_active() - Adds active NVMem device to NVM
 * @nvm: NVM structure
 * @reg_read: Pointer to the function to read the NVM (passed directly to the
 *	      NVMem device)
 *
 * Registers new active NVmem device for @nvm. The @reg_read is called
 * directly from NVMem so it must handle possible concurrent access if
 * needed. The first parameter passed to @reg_read is @nvm structure.
 * Returns %0 in success and negative errno otherwise.
 */
int tb_nvm_add_active(struct tb_nvm *nvm, nvmem_reg_read_t reg_read)
{
	struct nvmem_config config;
	struct nvmem_device *nvmem;

	memset(&config, 0, sizeof(config));

	config.name = "nvm_active";
	config.reg_read = reg_read;
	config.read_only = true;
	config.id = nvm->id;
	config.stride = 4;
	config.word_size = 4;
	config.size = nvm->active_size;
	config.dev = nvm->dev;
	config.owner = THIS_MODULE;
	config.priv = nvm;

	nvmem = nvmem_register(&config);
	if (IS_ERR(nvmem))
		return PTR_ERR(nvmem);

	nvm->active = nvmem;
	return 0;
}

/**
 * tb_nvm_write_buf() - Write data to @nvm buffer
 * @nvm: NVM structure
 * @offset: Offset where to write the data
 * @val: Data buffer to write
 * @bytes: Number of bytes to write
 *
 * Helper function to cache the new NVM image before it is actually
 * written to the flash. Copies @bytes from @val to @nvm->buf starting
 * from @offset.
 */
int tb_nvm_write_buf(struct tb_nvm *nvm, unsigned int offset, void *val,
		     size_t bytes)
{
	if (!nvm->buf) {
		nvm->buf = vmalloc(NVM_MAX_SIZE);
		if (!nvm->buf)
			return -ENOMEM;
	}

	nvm->flushed = false;
	nvm->buf_data_size = offset + bytes;
	memcpy(nvm->buf + offset, val, bytes);
	return 0;
}

/**
 * tb_nvm_add_non_active() - Adds non-active NVMem device to NVM
 * @nvm: NVM structure
 * @reg_write: Pointer to the function to write the NVM (passed directly
 *	       to the NVMem device)
 *
 * Registers new non-active NVmem device for @nvm. The @reg_write is called
 * directly from NVMem so it must handle possible concurrent access if
 * needed. The first parameter passed to @reg_write is @nvm structure.
 * The size of the NVMem device is set to %NVM_MAX_SIZE.
 *
 * Returns %0 in success and negative errno otherwise.
 */
int tb_nvm_add_non_active(struct tb_nvm *nvm, nvmem_reg_write_t reg_write)
{
	struct nvmem_config config;
	struct nvmem_device *nvmem;

	memset(&config, 0, sizeof(config));

	config.name = "nvm_non_active";
	config.reg_write = reg_write;
	config.root_only = true;
	config.id = nvm->id;
	config.stride = 4;
	config.word_size = 4;
	config.size = NVM_MAX_SIZE;
	config.dev = nvm->dev;
	config.owner = THIS_MODULE;
	config.priv = nvm;

	nvmem = nvmem_register(&config);
	if (IS_ERR(nvmem))
		return PTR_ERR(nvmem);

	nvm->non_active = nvmem;
	return 0;
}

/**
 * tb_nvm_free() - Release NVM and its resources
 * @nvm: NVM structure to release
 *
 * Releases NVM and the NVMem devices if they were registered.
 */
void tb_nvm_free(struct tb_nvm *nvm)
{
	if (nvm) {
		nvmem_unregister(nvm->non_active);
		nvmem_unregister(nvm->active);
		vfree(nvm->buf);
		ida_simple_remove(&nvm_ida, nvm->id);
	}
	kfree(nvm);
}

/**
 * tb_nvm_read_data() - Read data from NVM
 * @address: Start address on the flash
 * @buf: Buffer where the read data is copied
 * @size: Size of the buffer in bytes
 * @retries: Number of retries if block read fails
 * @read_block: Function that reads block from the flash
 * @read_block_data: Data passsed to @read_block
 *
 * This is a generic function that reads data from NVM or NVM like
 * device.
 *
 * Returns %0 on success and negative errno otherwise.
 */
int tb_nvm_read_data(unsigned int address, void *buf, size_t size,
		     unsigned int retries, read_block_fn read_block,
		     void *read_block_data)
{
	do {
		unsigned int dwaddress, dwords, offset;
		u8 data[NVM_DATA_DWORDS * 4];
		size_t nbytes;
		int ret;

		offset = address & 3;
		nbytes = min_t(size_t, size + offset, NVM_DATA_DWORDS * 4);

		dwaddress = address / 4;
		dwords = ALIGN(nbytes, 4) / 4;

		ret = read_block(read_block_data, dwaddress, data, dwords);
		if (ret) {
			if (ret != -ENODEV && retries--)
				continue;
			return ret;
		}

		nbytes -= offset;
		memcpy(buf, data + offset, nbytes);

		size -= nbytes;
		address += nbytes;
		buf += nbytes;
	} while (size > 0);

	return 0;
}

/**
 * tb_nvm_write_data() - Write data to NVM
 * @address: Start address on the flash
 * @buf: Buffer where the data is copied from
 * @size: Size of the buffer in bytes
 * @retries: Number of retries if the block write fails
 * @write_block: Function that writes block to the flash
 * @write_block_data: Data passwd to @write_block
 *
 * This is generic function that writes data to NVM or NVM like device.
 *
 * Returns %0 on success and negative errno otherwise.
 */
int tb_nvm_write_data(unsigned int address, const void *buf, size_t size,
		      unsigned int retries, write_block_fn write_block,
		      void *write_block_data)
{
	do {
		unsigned int offset, dwaddress;
		u8 data[NVM_DATA_DWORDS * 4];
		size_t nbytes;
		int ret;

		offset = address & 3;
		nbytes = min_t(u32, size + offset, NVM_DATA_DWORDS * 4);

		memcpy(data + offset, buf, nbytes);

		dwaddress = address / 4;
		ret = write_block(write_block_data, dwaddress, data, nbytes / 4);
		if (ret) {
			if (ret == -ETIMEDOUT) {
				if (retries--)
					continue;
				ret = -EIO;
			}
			return ret;
		}

		size -= nbytes;
		address += nbytes;
		buf += nbytes;
	} while (size > 0);

	return 0;
}

void tb_nvm_exit(void)
{
	ida_destroy(&nvm_ida);
}
