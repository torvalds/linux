/*
 * Copyright 2013 Red Hat Inc.
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
 * Authors: Dave Airlie
 *          Alon Levy
 */

#include "qxl_drv.h"
#include "qxl_object.h"

#include <drm/drm_crtc_helper.h>
#include <linux/io-mapping.h>

int qxl_log_level;

static void qxl_dump_mode(struct qxl_device *qdev, void *p)
{
	struct qxl_mode *m = p;
	DRM_DEBUG_KMS("%d: %dx%d %d bits, stride %d, %dmm x %dmm, orientation %d\n",
		      m->id, m->x_res, m->y_res, m->bits, m->stride, m->x_mili,
		      m->y_mili, m->orientation);
}

static bool qxl_check_device(struct qxl_device *qdev)
{
	struct qxl_rom *rom = qdev->rom;
	int mode_offset;
	int i;

	if (rom->magic != 0x4f525851) {
		DRM_ERROR("bad rom signature %x\n", rom->magic);
		return false;
	}

	DRM_INFO("Device Version %d.%d\n", rom->id, rom->update_id);
	DRM_INFO("Compression level %d log level %d\n", rom->compression_level,
		 rom->log_level);
	DRM_INFO("Currently using mode #%d, list at 0x%x\n",
		 rom->mode, rom->modes_offset);
	DRM_INFO("%d io pages at offset 0x%x\n",
		 rom->num_io_pages, rom->pages_offset);
	DRM_INFO("%d byte draw area at offset 0x%x\n",
		 rom->surface0_area_size, rom->draw_area_offset);

	qdev->vram_size = rom->surface0_area_size;
	DRM_INFO("RAM header offset: 0x%x\n", rom->ram_header_offset);

	mode_offset = rom->modes_offset / 4;
	qdev->mode_info.num_modes = ((u32 *)rom)[mode_offset];
	DRM_INFO("rom modes offset 0x%x for %d modes\n", rom->modes_offset,
		 qdev->mode_info.num_modes);
	qdev->mode_info.modes = (void *)((uint32_t *)rom + mode_offset + 1);
	for (i = 0; i < qdev->mode_info.num_modes; i++)
		qxl_dump_mode(qdev, qdev->mode_info.modes + i);
	return true;
}

static void setup_hw_slot(struct qxl_device *qdev, int slot_index,
			  struct qxl_memslot *slot)
{
	qdev->ram_header->mem_slot.mem_start = slot->start_phys_addr;
	qdev->ram_header->mem_slot.mem_end = slot->end_phys_addr;
	qxl_io_memslot_add(qdev, slot_index);
}

static uint8_t setup_slot(struct qxl_device *qdev, uint8_t slot_index_offset,
	unsigned long start_phys_addr, unsigned long end_phys_addr)
{
	uint64_t high_bits;
	struct qxl_memslot *slot;
	uint8_t slot_index;

	slot_index = qdev->rom->slots_start + slot_index_offset;
	slot = &qdev->mem_slots[slot_index];
	slot->start_phys_addr = start_phys_addr;
	slot->end_phys_addr = end_phys_addr;

	setup_hw_slot(qdev, slot_index, slot);

	slot->generation = qdev->rom->slot_generation;
	high_bits = slot_index << qdev->slot_gen_bits;
	high_bits |= slot->generation;
	high_bits <<= (64 - (qdev->slot_gen_bits + qdev->slot_id_bits));
	slot->high_bits = high_bits;
	return slot_index;
}

void qxl_reinit_memslots(struct qxl_device *qdev)
{
	setup_hw_slot(qdev, qdev->main_mem_slot, &qdev->mem_slots[qdev->main_mem_slot]);
	setup_hw_slot(qdev, qdev->surfaces_mem_slot, &qdev->mem_slots[qdev->surfaces_mem_slot]);
}

static void qxl_gc_work(struct work_struct *work)
{
	struct qxl_device *qdev = container_of(work, struct qxl_device, gc_work);
	qxl_garbage_collect(qdev);
}

static int qxl_device_init(struct qxl_device *qdev,
		    struct drm_device *ddev,
		    struct pci_dev *pdev,
		    unsigned long flags)
{
	int r, sb;

	qdev->dev = &pdev->dev;
	qdev->ddev = ddev;
	qdev->pdev = pdev;
	qdev->flags = flags;

	mutex_init(&qdev->gem.mutex);
	mutex_init(&qdev->update_area_mutex);
	mutex_init(&qdev->release_mutex);
	mutex_init(&qdev->surf_evict_mutex);
	INIT_LIST_HEAD(&qdev->gem.objects);

	qdev->rom_base = pci_resource_start(pdev, 2);
	qdev->rom_size = pci_resource_len(pdev, 2);
	qdev->vram_base = pci_resource_start(pdev, 0);
	qdev->io_base = pci_resource_start(pdev, 3);

	qdev->vram_mapping = io_mapping_create_wc(qdev->vram_base, pci_resource_len(pdev, 0));

	if (pci_resource_len(pdev, 4) > 0) {
		/* 64bit surface bar present */
		sb = 4;
		qdev->surfaceram_base = pci_resource_start(pdev, sb);
		qdev->surfaceram_size = pci_resource_len(pdev, sb);
		qdev->surface_mapping =
			io_mapping_create_wc(qdev->surfaceram_base,
					     qdev->surfaceram_size);
	}
	if (qdev->surface_mapping == NULL) {
		/* 64bit surface bar not present (or mapping failed) */
		sb = 1;
		qdev->surfaceram_base = pci_resource_start(pdev, sb);
		qdev->surfaceram_size = pci_resource_len(pdev, sb);
		qdev->surface_mapping =
			io_mapping_create_wc(qdev->surfaceram_base,
					     qdev->surfaceram_size);
	}

	DRM_DEBUG_KMS("qxl: vram %llx-%llx(%dM %dk), surface %llx-%llx(%dM %dk, %s)\n",
		 (unsigned long long)qdev->vram_base,
		 (unsigned long long)pci_resource_end(pdev, 0),
		 (int)pci_resource_len(pdev, 0) / 1024 / 1024,
		 (int)pci_resource_len(pdev, 0) / 1024,
		 (unsigned long long)qdev->surfaceram_base,
		 (unsigned long long)pci_resource_end(pdev, sb),
		 (int)qdev->surfaceram_size / 1024 / 1024,
		 (int)qdev->surfaceram_size / 1024,
		 (sb == 4) ? "64bit" : "32bit");

	qdev->rom = ioremap(qdev->rom_base, qdev->rom_size);
	if (!qdev->rom) {
		pr_err("Unable to ioremap ROM\n");
		return -ENOMEM;
	}

	qxl_check_device(qdev);

	r = qxl_bo_init(qdev);
	if (r) {
		DRM_ERROR("bo init failed %d\n", r);
		return r;
	}

	qdev->ram_header = ioremap(qdev->vram_base +
				   qdev->rom->ram_header_offset,
				   sizeof(*qdev->ram_header));

	qdev->command_ring = qxl_ring_create(&(qdev->ram_header->cmd_ring_hdr),
					     sizeof(struct qxl_command),
					     QXL_COMMAND_RING_SIZE,
					     qdev->io_base + QXL_IO_NOTIFY_CMD,
					     false,
					     &qdev->display_event);

	qdev->cursor_ring = qxl_ring_create(
				&(qdev->ram_header->cursor_ring_hdr),
				sizeof(struct qxl_command),
				QXL_CURSOR_RING_SIZE,
				qdev->io_base + QXL_IO_NOTIFY_CMD,
				false,
				&qdev->cursor_event);

	qdev->release_ring = qxl_ring_create(
				&(qdev->ram_header->release_ring_hdr),
				sizeof(uint64_t),
				QXL_RELEASE_RING_SIZE, 0, true,
				NULL);

	/* TODO - slot initialization should happen on reset. where is our
	 * reset handler? */
	qdev->n_mem_slots = qdev->rom->slots_end;
	qdev->slot_gen_bits = qdev->rom->slot_gen_bits;
	qdev->slot_id_bits = qdev->rom->slot_id_bits;
	qdev->va_slot_mask =
		(~(uint64_t)0) >> (qdev->slot_id_bits + qdev->slot_gen_bits);

	qdev->mem_slots =
		kmalloc(qdev->n_mem_slots * sizeof(struct qxl_memslot),
			GFP_KERNEL);

	idr_init(&qdev->release_idr);
	spin_lock_init(&qdev->release_idr_lock);
	spin_lock_init(&qdev->release_lock);

	idr_init(&qdev->surf_id_idr);
	spin_lock_init(&qdev->surf_id_idr_lock);

	mutex_init(&qdev->async_io_mutex);

	/* reset the device into a known state - no memslots, no primary
	 * created, no surfaces. */
	qxl_io_reset(qdev);

	/* must initialize irq before first async io - slot creation */
	r = qxl_irq_init(qdev);
	if (r)
		return r;

	/*
	 * Note that virtual is surface0. We rely on the single ioremap done
	 * before.
	 */
	qdev->main_mem_slot = setup_slot(qdev, 0,
		(unsigned long)qdev->vram_base,
		(unsigned long)qdev->vram_base + qdev->rom->ram_header_offset);
	qdev->surfaces_mem_slot = setup_slot(qdev, 1,
		(unsigned long)qdev->surfaceram_base,
		(unsigned long)qdev->surfaceram_base + qdev->surfaceram_size);
	DRM_INFO("main mem slot %d [%lx,%x]\n",
		 qdev->main_mem_slot,
		 (unsigned long)qdev->vram_base, qdev->rom->ram_header_offset);
	DRM_INFO("surface mem slot %d [%lx,%lx]\n",
		 qdev->surfaces_mem_slot,
		 (unsigned long)qdev->surfaceram_base,
		 (unsigned long)qdev->surfaceram_size);


	qdev->gc_queue = create_singlethread_workqueue("qxl_gc");
	INIT_WORK(&qdev->gc_work, qxl_gc_work);

	r = qxl_fb_init(qdev);
	if (r)
		return r;

	return 0;
}

static void qxl_device_fini(struct qxl_device *qdev)
{
	if (qdev->current_release_bo[0])
		qxl_bo_unref(&qdev->current_release_bo[0]);
	if (qdev->current_release_bo[1])
		qxl_bo_unref(&qdev->current_release_bo[1]);
	flush_workqueue(qdev->gc_queue);
	destroy_workqueue(qdev->gc_queue);
	qdev->gc_queue = NULL;

	qxl_ring_free(qdev->command_ring);
	qxl_ring_free(qdev->cursor_ring);
	qxl_ring_free(qdev->release_ring);
	qxl_bo_fini(qdev);
	io_mapping_free(qdev->surface_mapping);
	io_mapping_free(qdev->vram_mapping);
	iounmap(qdev->ram_header);
	iounmap(qdev->rom);
	qdev->rom = NULL;
	qdev->mode_info.modes = NULL;
	qdev->mode_info.num_modes = 0;
	qxl_debugfs_remove_files(qdev);
}

int qxl_driver_unload(struct drm_device *dev)
{
	struct qxl_device *qdev = dev->dev_private;

	if (qdev == NULL)
		return 0;

	drm_vblank_cleanup(dev);

	qxl_modeset_fini(qdev);
	qxl_device_fini(qdev);

	kfree(qdev);
	dev->dev_private = NULL;
	return 0;
}

int qxl_driver_load(struct drm_device *dev, unsigned long flags)
{
	struct qxl_device *qdev;
	int r;

	/* require kms */
	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return -ENODEV;

	qdev = kzalloc(sizeof(struct qxl_device), GFP_KERNEL);
	if (qdev == NULL)
		return -ENOMEM;

	dev->dev_private = qdev;

	r = qxl_device_init(qdev, dev, dev->pdev, flags);
	if (r)
		goto out;

	r = drm_vblank_init(dev, 1);
	if (r)
		goto unload;

	r = qxl_modeset_init(qdev);
	if (r)
		goto unload;

	drm_kms_helper_poll_init(qdev->ddev);

	return 0;
unload:
	qxl_driver_unload(dev);

out:
	kfree(qdev);
	return r;
}


