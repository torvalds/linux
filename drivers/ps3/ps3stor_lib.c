// SPDX-License-Identifier: GPL-2.0-only
/*
 * PS3 Storage Library
 *
 * Copyright (C) 2007 Sony Computer Entertainment Inc.
 * Copyright 2007 Sony Corp.
 */

#include <linux/dma-mapping.h>
#include <linux/module.h>

#include <asm/lv1call.h>
#include <asm/ps3stor.h>

/*
 * A workaround for flash memory I/O errors when the internal hard disk
 * has not been formatted for OtherOS use.  Delay disk close until flash
 * memory is closed.
 */

static struct ps3_flash_workaround {
	int flash_open;
	int disk_open;
	struct ps3_system_bus_device *disk_sbd;
} ps3_flash_workaround;

static int ps3stor_open_hv_device(struct ps3_system_bus_device *sbd)
{
	int error = ps3_open_hv_device(sbd);

	if (error)
		return error;

	if (sbd->match_id == PS3_MATCH_ID_STOR_FLASH)
		ps3_flash_workaround.flash_open = 1;

	if (sbd->match_id == PS3_MATCH_ID_STOR_DISK)
		ps3_flash_workaround.disk_open = 1;

	return 0;
}

static int ps3stor_close_hv_device(struct ps3_system_bus_device *sbd)
{
	int error;

	if (sbd->match_id == PS3_MATCH_ID_STOR_DISK
		&& ps3_flash_workaround.disk_open
		&& ps3_flash_workaround.flash_open) {
		ps3_flash_workaround.disk_sbd = sbd;
		return 0;
	}

	error = ps3_close_hv_device(sbd);

	if (error)
		return error;

	if (sbd->match_id == PS3_MATCH_ID_STOR_DISK)
		ps3_flash_workaround.disk_open = 0;

	if (sbd->match_id == PS3_MATCH_ID_STOR_FLASH) {
		ps3_flash_workaround.flash_open = 0;

		if (ps3_flash_workaround.disk_sbd) {
			ps3_close_hv_device(ps3_flash_workaround.disk_sbd);
			ps3_flash_workaround.disk_open = 0;
			ps3_flash_workaround.disk_sbd = NULL;
		}
	}

	return 0;
}

static int ps3stor_probe_access(struct ps3_storage_device *dev)
{
	int res, error;
	unsigned int i;
	unsigned long n;

	if (dev->sbd.match_id == PS3_MATCH_ID_STOR_ROM) {
		/* special case: CD-ROM is assumed always accessible */
		dev->accessible_regions = 1;
		return 0;
	}

	error = -EPERM;
	for (i = 0; i < dev->num_regions; i++) {
		dev_dbg(&dev->sbd.core,
			"%s:%u: checking accessibility of region %u\n",
			__func__, __LINE__, i);

		dev->region_idx = i;
		res = ps3stor_read_write_sectors(dev, dev->bounce_lpar, 0, 1,
						 0);
		if (res) {
			dev_dbg(&dev->sbd.core, "%s:%u: read failed, "
				"region %u is not accessible\n", __func__,
				__LINE__, i);
			continue;
		}

		dev_dbg(&dev->sbd.core, "%s:%u: region %u is accessible\n",
			__func__, __LINE__, i);
		set_bit(i, &dev->accessible_regions);

		/* We can access at least one region */
		error = 0;
	}
	if (error)
		return error;

	n = hweight_long(dev->accessible_regions);
	if (n > 1)
		dev_info(&dev->sbd.core,
			 "%s:%u: %lu accessible regions found. Only the first "
			 "one will be used\n",
			 __func__, __LINE__, n);
	dev->region_idx = __ffs(dev->accessible_regions);
	dev_info(&dev->sbd.core,
		 "First accessible region has index %u start %llu size %llu\n",
		 dev->region_idx, dev->regions[dev->region_idx].start,
		 dev->regions[dev->region_idx].size);

	return 0;
}


/**
 *	ps3stor_setup - Setup a storage device before use
 *	@dev: Pointer to a struct ps3_storage_device
 *	@handler: Pointer to an interrupt handler
 *
 *	Returns 0 for success, or an error code
 */
int ps3stor_setup(struct ps3_storage_device *dev, irq_handler_t handler)
{
	int error, res, alignment;
	enum ps3_dma_page_size page_size;

	error = ps3stor_open_hv_device(&dev->sbd);
	if (error) {
		dev_err(&dev->sbd.core,
			"%s:%u: ps3_open_hv_device failed %d\n", __func__,
			__LINE__, error);
		goto fail;
	}

	error = ps3_sb_event_receive_port_setup(&dev->sbd, PS3_BINDING_CPU_ANY,
						&dev->irq);
	if (error) {
		dev_err(&dev->sbd.core,
			"%s:%u: ps3_sb_event_receive_port_setup failed %d\n",
		       __func__, __LINE__, error);
		goto fail_close_device;
	}

	error = request_irq(dev->irq, handler, 0,
			    dev->sbd.core.driver->name, dev);
	if (error) {
		dev_err(&dev->sbd.core, "%s:%u: request_irq failed %d\n",
			__func__, __LINE__, error);
		goto fail_sb_event_receive_port_destroy;
	}

	alignment = min(__ffs(dev->bounce_size),
			__ffs((unsigned long)dev->bounce_buf));
	if (alignment < 12) {
		dev_err(&dev->sbd.core,
			"%s:%u: bounce buffer not aligned (%lx at 0x%p)\n",
			__func__, __LINE__, dev->bounce_size, dev->bounce_buf);
		error = -EINVAL;
		goto fail_free_irq;
	} else if (alignment < 16)
		page_size = PS3_DMA_4K;
	else
		page_size = PS3_DMA_64K;
	dev->sbd.d_region = &dev->dma_region;
	ps3_dma_region_init(&dev->sbd, &dev->dma_region, page_size,
			    PS3_DMA_OTHER, dev->bounce_buf, dev->bounce_size);
	res = ps3_dma_region_create(&dev->dma_region);
	if (res) {
		dev_err(&dev->sbd.core, "%s:%u: cannot create DMA region\n",
			__func__, __LINE__);
		error = -ENOMEM;
		goto fail_free_irq;
	}

	dev->bounce_lpar = ps3_mm_phys_to_lpar(__pa(dev->bounce_buf));
	dev->bounce_dma = dma_map_single(&dev->sbd.core, dev->bounce_buf,
					 dev->bounce_size, DMA_BIDIRECTIONAL);
	if (!dev->bounce_dma) {
		dev_err(&dev->sbd.core, "%s:%u: map DMA region failed\n",
			__func__, __LINE__);
		error = -ENODEV;
		goto fail_free_dma;
	}

	error = ps3stor_probe_access(dev);
	if (error) {
		dev_err(&dev->sbd.core, "%s:%u: No accessible regions found\n",
			__func__, __LINE__);
		goto fail_unmap_dma;
	}
	return 0;

fail_unmap_dma:
	dma_unmap_single(&dev->sbd.core, dev->bounce_dma, dev->bounce_size,
			 DMA_BIDIRECTIONAL);
fail_free_dma:
	ps3_dma_region_free(&dev->dma_region);
fail_free_irq:
	free_irq(dev->irq, dev);
fail_sb_event_receive_port_destroy:
	ps3_sb_event_receive_port_destroy(&dev->sbd, dev->irq);
fail_close_device:
	ps3stor_close_hv_device(&dev->sbd);
fail:
	return error;
}
EXPORT_SYMBOL_GPL(ps3stor_setup);


/**
 *	ps3stor_teardown - Tear down a storage device after use
 *	@dev: Pointer to a struct ps3_storage_device
 */
void ps3stor_teardown(struct ps3_storage_device *dev)
{
	int error;

	dma_unmap_single(&dev->sbd.core, dev->bounce_dma, dev->bounce_size,
			 DMA_BIDIRECTIONAL);
	ps3_dma_region_free(&dev->dma_region);

	free_irq(dev->irq, dev);

	error = ps3_sb_event_receive_port_destroy(&dev->sbd, dev->irq);
	if (error)
		dev_err(&dev->sbd.core,
			"%s:%u: destroy event receive port failed %d\n",
			__func__, __LINE__, error);

	error = ps3stor_close_hv_device(&dev->sbd);
	if (error)
		dev_err(&dev->sbd.core,
			"%s:%u: ps3_close_hv_device failed %d\n", __func__,
			__LINE__, error);
}
EXPORT_SYMBOL_GPL(ps3stor_teardown);


/**
 *	ps3stor_read_write_sectors - read/write from/to a storage device
 *	@dev: Pointer to a struct ps3_storage_device
 *	@lpar: HV logical partition address
 *	@start_sector: First sector to read/write
 *	@sectors: Number of sectors to read/write
 *	@write: Flag indicating write (non-zero) or read (zero)
 *
 *	Returns 0 for success, -1 in case of failure to submit the command, or
 *	an LV1 status value in case of other errors
 */
u64 ps3stor_read_write_sectors(struct ps3_storage_device *dev, u64 lpar,
			       u64 start_sector, u64 sectors, int write)
{
	unsigned int region_id = dev->regions[dev->region_idx].id;
	const char *op = write ? "write" : "read";
	int res;

	dev_dbg(&dev->sbd.core, "%s:%u: %s %llu sectors starting at %llu\n",
		__func__, __LINE__, op, sectors, start_sector);

	init_completion(&dev->done);
	res = write ? lv1_storage_write(dev->sbd.dev_id, region_id,
					start_sector, sectors, 0, lpar,
					&dev->tag)
		    : lv1_storage_read(dev->sbd.dev_id, region_id,
				       start_sector, sectors, 0, lpar,
				       &dev->tag);
	if (res) {
		dev_dbg(&dev->sbd.core, "%s:%u: %s failed %d\n", __func__,
			__LINE__, op, res);
		return -1;
	}

	wait_for_completion(&dev->done);
	if (dev->lv1_status) {
		dev_dbg(&dev->sbd.core, "%s:%u: %s failed 0x%llx\n", __func__,
			__LINE__, op, dev->lv1_status);
		return dev->lv1_status;
	}

	dev_dbg(&dev->sbd.core, "%s:%u: %s completed\n", __func__, __LINE__,
		op);

	return 0;
}
EXPORT_SYMBOL_GPL(ps3stor_read_write_sectors);


/**
 *	ps3stor_send_command - send a device command to a storage device
 *	@dev: Pointer to a struct ps3_storage_device
 *	@cmd: Command number
 *	@arg1: First command argument
 *	@arg2: Second command argument
 *	@arg3: Third command argument
 *	@arg4: Fourth command argument
 *
 *	Returns 0 for success, -1 in case of failure to submit the command, or
 *	an LV1 status value in case of other errors
 */
u64 ps3stor_send_command(struct ps3_storage_device *dev, u64 cmd, u64 arg1,
			 u64 arg2, u64 arg3, u64 arg4)
{
	int res;

	dev_dbg(&dev->sbd.core, "%s:%u: send device command 0x%llx\n", __func__,
		__LINE__, cmd);

	init_completion(&dev->done);

	res = lv1_storage_send_device_command(dev->sbd.dev_id, cmd, arg1,
					      arg2, arg3, arg4, &dev->tag);
	if (res) {
		dev_err(&dev->sbd.core,
			"%s:%u: send_device_command 0x%llx failed %d\n",
			__func__, __LINE__, cmd, res);
		return -1;
	}

	wait_for_completion(&dev->done);
	if (dev->lv1_status) {
		dev_dbg(&dev->sbd.core, "%s:%u: command 0x%llx failed 0x%llx\n",
			__func__, __LINE__, cmd, dev->lv1_status);
		return dev->lv1_status;
	}

	dev_dbg(&dev->sbd.core, "%s:%u: command 0x%llx completed\n", __func__,
		__LINE__, cmd);

	return 0;
}
EXPORT_SYMBOL_GPL(ps3stor_send_command);


MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("PS3 Storage Bus Library");
MODULE_AUTHOR("Sony Corporation");
