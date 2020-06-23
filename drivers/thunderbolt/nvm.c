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

static DEFINE_IDA(nvm_ida);

/**
 * tb_nvm_alloc() - Allocate new NVM structure
 * @dev: Device owning the NVM
 *
 * Allocates new NVM structure with unique @id and returns it. In case
 * of error returns ERR_PTR().
 */
struct tb_nvm *tb_nvm_alloc(struct device *dev)
{
	struct tb_nvm *nvm;
	int ret;

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

	return nvm;
}

/**
 * tb_nvm_add_active() - Adds active NVMem device to NVM
 * @nvm: NVM structure
 * @size: Size of the active NVM in bytes
 * @reg_read: Pointer to the function to read the NVM (passed directly to the
 *	      NVMem device)
 *
 * Registers new active NVmem device for @nvm. The @reg_read is called
 * directly from NVMem so it must handle possible concurrent access if
 * needed. The first parameter passed to @reg_read is @nvm structure.
 * Returns %0 in success and negative errno otherwise.
 */
int tb_nvm_add_active(struct tb_nvm *nvm, size_t size, nvmem_reg_read_t reg_read)
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
	config.size = size;
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
 * @size: Size of the non-active NVM in bytes
 * @reg_write: Pointer to the function to write the NVM (passed directly
 *	       to the NVMem device)
 *
 * Registers new non-active NVmem device for @nvm. The @reg_write is called
 * directly from NVMem so it must handle possible concurrent access if
 * needed. The first parameter passed to @reg_write is @nvm structure.
 * Returns %0 in success and negative errno otherwise.
 */
int tb_nvm_add_non_active(struct tb_nvm *nvm, size_t size,
			  nvmem_reg_write_t reg_write)
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
	config.size = size;
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
		if (nvm->non_active)
			nvmem_unregister(nvm->non_active);
		if (nvm->active)
			nvmem_unregister(nvm->active);
		vfree(nvm->buf);
		ida_simple_remove(&nvm_ida, nvm->id);
	}
	kfree(nvm);
}

void tb_nvm_exit(void)
{
	ida_destroy(&nvm_ida);
}
