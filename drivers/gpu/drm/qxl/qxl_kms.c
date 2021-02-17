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

#include <linux/io-mapping.h>
#include <linux/pci.h>

#include <drm/drm_drv.h>
#include <drm/drm_managed.h>
#include <drm/drm_probe_helper.h>

#include "qxl_drv.h"
#include "qxl_object.h"

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

static void setup_hw_slot(struct qxl_device *qdev, struct qxl_memslot *slot)
{
	qdev->ram_header->mem_slot.mem_start = slot->start_phys_addr;
	qdev->ram_header->mem_slot.mem_end = slot->start_phys_addr + slot->size;
	qxl_io_memslot_add(qdev, qdev->rom->slots_start + slot->index);
}

static void setup_slot(struct qxl_device *qdev,
		       struct qxl_memslot *slot,
		       unsigned int slot_index,
		       const char *slot_name,
		       unsigned long start_phys_addr,
		       unsigned long size)
{
	uint64_t high_bits;

	slot->index = slot_index;
	slot->name = slot_name;
	slot->start_phys_addr = start_phys_addr;
	slot->size = size;

	setup_hw_slot(qdev, slot);

	slot->generation = qdev->rom->slot_generation;
	high_bits = (qdev->rom->slots_start + slot->index)
		<< qdev->rom->slot_gen_bits;
	high_bits |= slot->generation;
	high_bits <<= (64 - (qdev->rom->slot_gen_bits + qdev->rom->slot_id_bits));
	slot->high_bits = high_bits;

	DRM_INFO("slot %d (%s): base 0x%08lx, size 0x%08lx\n",
		 slot->index, slot->name,
		 (unsigned long)slot->start_phys_addr,
		 (unsigned long)slot->size);
}

void qxl_reinit_memslots(struct qxl_device *qdev)
{
	setup_hw_slot(qdev, &qdev->main_slot);
	setup_hw_slot(qdev, &qdev->surfaces_slot);
}

static void qxl_gc_work(struct work_struct *work)
{
	struct qxl_device *qdev = container_of(work, struct qxl_device, gc_work);

	qxl_garbage_collect(qdev);
}

int qxl_device_init(struct qxl_device *qdev,
		    struct pci_dev *pdev)
{
	int r, sb;

	pci_set_drvdata(pdev, &qdev->ddev);

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
		return -ENOMEM;
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
		goto rom_unmap;
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
				qdev->io_base + QXL_IO_NOTIFY_CURSOR,
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

	idr_init_base(&qdev->release_idr, 1);
	spin_lock_init(&qdev->release_idr_lock);
	spin_lock_init(&qdev->release_lock);

	idr_init_base(&qdev->surf_id_idr, 1);
	spin_lock_init(&qdev->surf_id_idr_lock);

	mutex_init(&qdev->async_io_mutex);

	/* reset the device into a known state - no memslots, no primary
	 * created, no surfaces. */
	qxl_io_reset(qdev);

	/* must initialize irq before first async io - slot creation */
	r = qxl_irq_init(qdev);
	if (r) {
		DRM_ERROR("Unable to init qxl irq\n");
		goto release_ring_free;
	}

	/*
	 * Note that virtual is surface0. We rely on the single ioremap done
	 * before.
	 */
	setup_slot(qdev, &qdev->main_slot, 0, "main",
		   (unsigned long)qdev->vram_base,
		   (unsigned long)qdev->rom->ram_header_offset);
	setup_slot(qdev, &qdev->surfaces_slot, 1, "surfaces",
		   (unsigned long)qdev->surfaceram_base,
		   (unsigned long)qdev->surfaceram_size);

	INIT_WORK(&qdev->gc_work, qxl_gc_work);

	return 0;

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
	return r;
}

void qxl_device_fini(struct qxl_device *qdev)
{
	int cur_idx;

	/* check if qxl_device_init() was successful (gc_work is initialized last) */
	if (!qdev->gc_work.func)
		return;

	for (cur_idx = 0; cur_idx < 3; cur_idx++) {
		if (!qdev->current_release_bo[cur_idx])
			continue;
		qxl_bo_unpin(qdev->current_release_bo[cur_idx]);
		qxl_bo_unref(&qdev->current_release_bo[cur_idx]);
		qdev->current_release_bo_offset[cur_idx] = 0;
		qdev->current_release_bo[cur_idx] = NULL;
	}

	/*
	 * Ask host to release resources (+fill release ring),
	 * then wait for the release actually happening.
	 */
	qxl_io_notify_oom(qdev);
	wait_event_timeout(qdev->release_event,
			   atomic_read(&qdev->release_count) == 0,
			   HZ);
	flush_work(&qdev->gc_work);
	qxl_surf_evict(qdev);
	qxl_vram_evict(qdev);

	qxl_gem_fini(qdev);
	qxl_bo_fini(qdev);
	qxl_ring_free(qdev->command_ring);
	qxl_ring_free(qdev->cursor_ring);
	qxl_ring_free(qdev->release_ring);
	io_mapping_free(qdev->surface_mapping);
	io_mapping_free(qdev->vram_mapping);
	iounmap(qdev->ram_header);
	iounmap(qdev->rom);
	qdev->rom = NULL;
}
