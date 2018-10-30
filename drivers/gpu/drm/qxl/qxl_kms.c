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

static bool qxl_check_device(struct qxl_device *qdev)
{
	struct qxl_rom *rom = qdev->rom;

	if (rom->magic != 0x4f525851) {
		DRM_ERROR("bad rom signature %x\n", rom->magic);
		return false;
	}

	DRM_INFO("Device Version %d.%d\n", rom->id, rom->update_id);
	DRM_INFO("Compression level %d log level %d\n", rom->compression_level,
		 rom->log_level);
	DRM_INFO("%d io pages at offset 0x%x\n",
		 rom->num_io_pages, rom->pages_offset);
	DRM_INFO("%d byte draw area at offset 0x%x\n",
		 rom->surface0_area_size, rom->draw_area_offset);

	qdev->vram_size = rom->surface0_area_size;
	DRM_INFO("RAM header offset: 0x%x\n", rom->ram_header_offset);
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

int qxl_device_init(struct qxl_device *qdev,
		    struct drm_driver *drv,
		    struct pci_dev *pdev)
{
	int r, sb;

	r = drm_dev_init(&qdev->ddev, drv, &pdev->dev);
	if (r) {
		pr_err("Unable to init drm dev");
		goto error;
	}

	qdev->ddev.pdev = pdev;
	pci_set_drvdata(pdev, &qdev->ddev);
	qdev->ddev.dev_private = qdev;

	mutex_init(&qdev->gem.mutex);
	mutex_init(&qdev->update_area_mutex);
	mutex_init(&qdev->release_mutex);
	mutex_init(&qdev->surf_evict_mutex);
	qxl_gem_init(qdev);

	qdev->rom_base = pci_resource_start(pdev, 2);
	qdev->rom_size = pci_resource_len(pdev, 2);
	qdev->vram_base = pci_resource_start(pdev, 0);
	qdev->io_base = pci_resource_start(pdev, 3);

	qdev->vram_mapping = io_mapping_create_wc(qdev->vram_base, pci_resource_len(pdev, 0));
	if (!qdev->vram_mapping) {
		pr_err("Unable to create vram_mapping");
		r = -ENOMEM;
		goto error;
	}

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
		if (!qdev->surface_mapping) {
			pr_err("Unable to create surface_mapping");
			r = -ENOMEM;
			goto vram_mapping_free;
		}
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
		r = -ENOMEM;
		goto surface_mapping_free;
	}

	if (!qxl_check_device(qdev)) {
		r = -ENODEV;
		goto surface_mapping_free;
	}

	r = qxl_bo_init(qdev);
	if (r) {
		DRM_ERROR("bo init failed %d\n", r);
		goto rom_unmap;
	}

	qdev->ram_header = ioremap(qdev->vram_base +
				   qdev->rom->ram_header_offset,
				   sizeof(*qdev->ram_header));
	if (!qdev->ram_header) {
		DRM_ERROR("Unable to ioremap RAM header\n");
		r = -ENOMEM;
		goto bo_fini;
	}

	qdev->command_ring = qxl_ring_create(&(qdev->ram_header->cmd_ring_hdr),
					     sizeof(struct qxl_command),
					     QXL_COMMAND_RING_SIZE,
					     qdev->io_base + QXL_IO_NOTIFY_CMD,
					     false,
					     &qdev->display_event);
	if (!qdev->command_ring) {
		DRM_ERROR("Unable to create command ring\n");
		r = -ENOMEM;
		goto ram_header_unmap;
	}

	qdev->cursor_ring = qxl_ring_create(
				&(qdev->ram_header->cursor_ring_hdr),
				sizeof(struct qxl_command),
				QXL_CURSOR_RING_SIZE,
				qdev->io_base + QXL_IO_NOTIFY_CMD,
				false,
				&qdev->cursor_event);

	if (!qdev->cursor_ring) {
		DRM_ERROR("Unable to create cursor ring\n");
		r = -ENOMEM;
		goto command_ring_free;
	}

	qdev->release_ring = qxl_ring_create(
				&(qdev->ram_header->release_ring_hdr),
				sizeof(uint64_t),
				QXL_RELEASE_RING_SIZE, 0, true,
				NULL);

	if (!qdev->release_ring) {
		DRM_ERROR("Unable to create release ring\n");
		r = -ENOMEM;
		goto cursor_ring_free;
	}
	/* TODO - slot initialization should happen on reset. where is our
	 * reset handler? */
	qdev->n_mem_slots = qdev->rom->slots_end;
	qdev->slot_gen_bits = qdev->rom->slot_gen_bits;
	qdev->slot_id_bits = qdev->rom->slot_id_bits;
	qdev->va_slot_mask =
		(~(uint64_t)0) >> (qdev->slot_id_bits + qdev->slot_gen_bits);

	qdev->mem_slots =
		kmalloc_array(qdev->n_mem_slots, sizeof(struct qxl_memslot),
			      GFP_KERNEL);

	if (!qdev->mem_slots) {
		DRM_ERROR("Unable to alloc mem slots\n");
		r = -ENOMEM;
		goto release_ring_free;
	}

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
	if (r) {
		DRM_ERROR("Unable to init qxl irq\n");
		goto mem_slots_free;
	}

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


	INIT_WORK(&qdev->gc_work, qxl_gc_work);

	return 0;

mem_slots_free:
	kfree(qdev->mem_slots);
release_ring_free:
	qxl_ring_free(qdev->release_ring);
cursor_ring_free:
	qxl_ring_free(qdev->cursor_ring);
command_ring_free:
	qxl_ring_free(qdev->command_ring);
ram_header_unmap:
	iounmap(qdev->ram_header);
bo_fini:
	qxl_bo_fini(qdev);
rom_unmap:
	iounmap(qdev->rom);
surface_mapping_free:
	io_mapping_free(qdev->surface_mapping);
vram_mapping_free:
	io_mapping_free(qdev->vram_mapping);
error:
	return r;
}

void qxl_device_fini(struct qxl_device *qdev)
{
	if (qdev->current_release_bo[0])
		qxl_bo_unref(&qdev->current_release_bo[0]);
	if (qdev->current_release_bo[1])
		qxl_bo_unref(&qdev->current_release_bo[1]);
	flush_work(&qdev->gc_work);
	qxl_ring_free(qdev->command_ring);
	qxl_ring_free(qdev->cursor_ring);
	qxl_ring_free(qdev->release_ring);
	qxl_gem_fini(qdev);
	qxl_bo_fini(qdev);
	io_mapping_free(qdev->surface_mapping);
	io_mapping_free(qdev->vram_mapping);
	iounmap(qdev->ram_header);
	iounmap(qdev->rom);
	qdev->rom = NULL;
}
