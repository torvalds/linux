/*
 * Copyright 2021 Red Hat Inc.
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
 */
#include <core/intr.h>
#include <core/device.h>
#include <core/subdev.h>
#include <subdev/pci.h>
#include <subdev/top.h>

static int
nvkm_intr_xlat(struct nvkm_subdev *subdev, struct nvkm_intr *intr,
	       enum nvkm_intr_type type, int *leaf, u32 *mask)
{
	struct nvkm_device *device = subdev->device;

	if (type < NVKM_INTR_VECTOR_0) {
		if (type == NVKM_INTR_SUBDEV) {
			const struct nvkm_intr_data *data = intr->data;
			struct nvkm_top_device *tdev;

			while (data && data->mask) {
				if (data->type == NVKM_SUBDEV_TOP) {
					list_for_each_entry(tdev, &device->top->device, head) {
						if (tdev->intr >= 0 &&
						    tdev->type == subdev->type &&
						    tdev->inst == subdev->inst) {
							if (data->mask & BIT(tdev->intr)) {
								*leaf = data->leaf;
								*mask = BIT(tdev->intr);
								return 0;
							}
						}
					}
				} else
				if (data->type == subdev->type && data->inst == subdev->inst) {
					*leaf = data->leaf;
					*mask = data->mask;
					return 0;
				}

				data++;
			}
		} else {
			return -ENOSYS;
		}
	} else {
		if (type < intr->leaves * sizeof(*intr->stat) * 8) {
			*leaf = type / 32;
			*mask = BIT(type % 32);
			return 0;
		}
	}

	return -EINVAL;
}

static struct nvkm_intr *
nvkm_intr_find(struct nvkm_subdev *subdev, enum nvkm_intr_type type, int *leaf, u32 *mask)
{
	struct nvkm_intr *intr;
	int ret;

	list_for_each_entry(intr, &subdev->device->intr.intr, head) {
		ret = nvkm_intr_xlat(subdev, intr, type, leaf, mask);
		if (ret == 0)
			return intr;
	}

	return NULL;
}

static void
nvkm_intr_allow_locked(struct nvkm_intr *intr, int leaf, u32 mask)
{
	intr->mask[leaf] |= mask;
	if (intr->func->allow) {
		if (intr->func->reset)
			intr->func->reset(intr, leaf, mask);
		intr->func->allow(intr, leaf, mask);
	}
}

void
nvkm_intr_allow(struct nvkm_subdev *subdev, enum nvkm_intr_type type)
{
	struct nvkm_device *device = subdev->device;
	struct nvkm_intr *intr;
	unsigned long flags;
	int leaf;
	u32 mask;

	intr = nvkm_intr_find(subdev, type, &leaf, &mask);
	if (intr) {
		nvkm_debug(intr->subdev, "intr %d/%08x allowed by %s\n", leaf, mask, subdev->name);
		spin_lock_irqsave(&device->intr.lock, flags);
		nvkm_intr_allow_locked(intr, leaf, mask);
		spin_unlock_irqrestore(&device->intr.lock, flags);
	}
}

static void
nvkm_intr_block_locked(struct nvkm_intr *intr, int leaf, u32 mask)
{
	intr->mask[leaf] &= ~mask;
	if (intr->func->block)
		intr->func->block(intr, leaf, mask);
}

void
nvkm_intr_block(struct nvkm_subdev *subdev, enum nvkm_intr_type type)
{
	struct nvkm_device *device = subdev->device;
	struct nvkm_intr *intr;
	unsigned long flags;
	int leaf;
	u32 mask;

	intr = nvkm_intr_find(subdev, type, &leaf, &mask);
	if (intr) {
		nvkm_debug(intr->subdev, "intr %d/%08x blocked by %s\n", leaf, mask, subdev->name);
		spin_lock_irqsave(&device->intr.lock, flags);
		nvkm_intr_block_locked(intr, leaf, mask);
		spin_unlock_irqrestore(&device->intr.lock, flags);
	}
}

static void
nvkm_intr_rearm_locked(struct nvkm_device *device)
{
	struct nvkm_intr *intr;

	list_for_each_entry(intr, &device->intr.intr, head)
		intr->func->rearm(intr);
}

static void
nvkm_intr_unarm_locked(struct nvkm_device *device)
{
	struct nvkm_intr *intr;

	list_for_each_entry(intr, &device->intr.intr, head)
		intr->func->unarm(intr);
}

static irqreturn_t
nvkm_intr(int irq, void *arg)
{
	struct nvkm_device *device = arg;
	struct nvkm_intr *intr;
	struct nvkm_inth *inth;
	irqreturn_t ret = IRQ_NONE;
	bool pending = false;
	int prio, leaf;

	/* Disable all top-level interrupt sources, and re-arm MSI interrupts. */
	spin_lock(&device->intr.lock);
	if (!device->intr.armed)
		goto done_unlock;

	nvkm_intr_unarm_locked(device);
	nvkm_pci_msi_rearm(device);

	/* Fetch pending interrupt masks. */
	list_for_each_entry(intr, &device->intr.intr, head) {
		if (intr->func->pending(intr))
			pending = true;
	}

	if (!pending)
		goto done;

	/* Check that GPU is still on the bus by reading NV_PMC_BOOT_0. */
	if (WARN_ON(nvkm_rd32(device, 0x000000) == 0xffffffff))
		goto done;

	/* Execute handlers. */
	for (prio = 0; prio < ARRAY_SIZE(device->intr.prio); prio++) {
		list_for_each_entry(inth, &device->intr.prio[prio], head) {
			struct nvkm_intr *intr = inth->intr;

			if (intr->stat[inth->leaf] & inth->mask) {
				if (atomic_read(&inth->allowed)) {
					if (intr->func->reset)
						intr->func->reset(intr, inth->leaf, inth->mask);
					if (inth->func(inth) == IRQ_HANDLED)
						ret = IRQ_HANDLED;
				}
			}
		}
	}

	/* Nothing handled?  Some debugging/protection from IRQ storms is in order... */
	if (ret == IRQ_NONE) {
		list_for_each_entry(intr, &device->intr.intr, head) {
			for (leaf = 0; leaf < intr->leaves; leaf++) {
				if (intr->stat[leaf]) {
					nvkm_debug(intr->subdev, "intr%d: %08x\n",
						   leaf, intr->stat[leaf]);
					nvkm_intr_block_locked(intr, leaf, intr->stat[leaf]);
				}
			}
		}
	}

done:
	/* Re-enable all top-level interrupt sources. */
	nvkm_intr_rearm_locked(device);
done_unlock:
	spin_unlock(&device->intr.lock);
	return ret;
}

int
nvkm_intr_add(const struct nvkm_intr_func *func, const struct nvkm_intr_data *data,
	      struct nvkm_subdev *subdev, int leaves, struct nvkm_intr *intr)
{
	struct nvkm_device *device = subdev->device;
	int i;

	intr->func = func;
	intr->data = data;
	intr->subdev = subdev;
	intr->leaves = leaves;
	intr->stat = kcalloc(leaves, sizeof(*intr->stat), GFP_KERNEL);
	intr->mask = kcalloc(leaves, sizeof(*intr->mask), GFP_KERNEL);
	if (!intr->stat || !intr->mask) {
		kfree(intr->stat);
		return -ENOMEM;
	}

	if (intr->subdev->debug >= NV_DBG_DEBUG) {
		for (i = 0; i < intr->leaves; i++)
			intr->mask[i] = ~0;
	}

	spin_lock_irq(&device->intr.lock);
	list_add_tail(&intr->head, &device->intr.intr);
	spin_unlock_irq(&device->intr.lock);
	return 0;
}

static irqreturn_t
nvkm_intr_subdev(struct nvkm_inth *inth)
{
	struct nvkm_subdev *subdev = container_of(inth, typeof(*subdev), inth);

	nvkm_subdev_intr(subdev);
	return IRQ_HANDLED;
}

static void
nvkm_intr_subdev_add_dev(struct nvkm_intr *intr, enum nvkm_subdev_type type, int inst)
{
	struct nvkm_subdev *subdev;
	enum nvkm_intr_prio prio;
	int ret;

	subdev = nvkm_device_subdev(intr->subdev->device, type, inst);
	if (!subdev || !subdev->func->intr)
		return;

	if (type == NVKM_ENGINE_DISP)
		prio = NVKM_INTR_PRIO_VBLANK;
	else
		prio = NVKM_INTR_PRIO_NORMAL;

	ret = nvkm_inth_add(intr, NVKM_INTR_SUBDEV, prio, subdev, nvkm_intr_subdev, &subdev->inth);
	if (WARN_ON(ret))
		return;

	nvkm_inth_allow(&subdev->inth);
}

static void
nvkm_intr_subdev_add(struct nvkm_intr *intr)
{
	const struct nvkm_intr_data *data;
	struct nvkm_device *device = intr->subdev->device;
	struct nvkm_top_device *tdev;

	for (data = intr->data; data && data->mask; data++) {
		if (data->legacy) {
			if (data->type == NVKM_SUBDEV_TOP) {
				list_for_each_entry(tdev, &device->top->device, head) {
					if (tdev->intr < 0 || !(data->mask & BIT(tdev->intr)))
						continue;

					nvkm_intr_subdev_add_dev(intr, tdev->type, tdev->inst);
				}
			} else {
				nvkm_intr_subdev_add_dev(intr, data->type, data->inst);
			}
		}
	}
}

void
nvkm_intr_rearm(struct nvkm_device *device)
{
	struct nvkm_intr *intr;
	int i;

	if (unlikely(!device->intr.legacy_done)) {
		list_for_each_entry(intr, &device->intr.intr, head)
			nvkm_intr_subdev_add(intr);
		device->intr.legacy_done = true;
	}

	spin_lock_irq(&device->intr.lock);
	list_for_each_entry(intr, &device->intr.intr, head) {
		for (i = 0; intr->func->block && i < intr->leaves; i++) {
			intr->func->block(intr, i, ~0);
			intr->func->allow(intr, i, intr->mask[i]);
		}
	}

	nvkm_intr_rearm_locked(device);
	device->intr.armed = true;
	spin_unlock_irq(&device->intr.lock);
}

void
nvkm_intr_unarm(struct nvkm_device *device)
{
	spin_lock_irq(&device->intr.lock);
	nvkm_intr_unarm_locked(device);
	device->intr.armed = false;
	spin_unlock_irq(&device->intr.lock);
}

int
nvkm_intr_install(struct nvkm_device *device)
{
	int ret;

	device->intr.irq = device->func->irq(device);
	if (device->intr.irq < 0)
		return device->intr.irq;

	ret = request_irq(device->intr.irq, nvkm_intr, IRQF_SHARED, "nvkm", device);
	if (ret)
		return ret;

	device->intr.alloc = true;
	return 0;
}

void
nvkm_intr_dtor(struct nvkm_device *device)
{
	struct nvkm_intr *intr, *intt;

	list_for_each_entry_safe(intr, intt, &device->intr.intr, head) {
		list_del(&intr->head);
		kfree(intr->mask);
		kfree(intr->stat);
	}

	if (device->intr.alloc)
		free_irq(device->intr.irq, device);
}

void
nvkm_intr_ctor(struct nvkm_device *device)
{
	int i;

	INIT_LIST_HEAD(&device->intr.intr);
	for (i = 0; i < ARRAY_SIZE(device->intr.prio); i++)
		INIT_LIST_HEAD(&device->intr.prio[i]);

	spin_lock_init(&device->intr.lock);
	device->intr.armed = false;
}

void
nvkm_inth_block(struct nvkm_inth *inth)
{
	if (unlikely(!inth->intr))
		return;

	atomic_set(&inth->allowed, 0);
}

void
nvkm_inth_allow(struct nvkm_inth *inth)
{
	struct nvkm_intr *intr = inth->intr;
	unsigned long flags;

	if (unlikely(!inth->intr))
		return;

	spin_lock_irqsave(&intr->subdev->device->intr.lock, flags);
	if (!atomic_xchg(&inth->allowed, 1)) {
		if ((intr->mask[inth->leaf] & inth->mask) != inth->mask)
			nvkm_intr_allow_locked(intr, inth->leaf, inth->mask);
	}
	spin_unlock_irqrestore(&intr->subdev->device->intr.lock, flags);
}

int
nvkm_inth_add(struct nvkm_intr *intr, enum nvkm_intr_type type, enum nvkm_intr_prio prio,
	      struct nvkm_subdev *subdev, nvkm_inth_func func, struct nvkm_inth *inth)
{
	struct nvkm_device *device = subdev->device;
	int ret;

	if (WARN_ON(inth->mask))
		return -EBUSY;

	ret = nvkm_intr_xlat(subdev, intr, type, &inth->leaf, &inth->mask);
	if (ret)
		return ret;

	nvkm_debug(intr->subdev, "intr %d/%08x requested by %s\n",
		   inth->leaf, inth->mask, subdev->name);

	inth->intr = intr;
	inth->func = func;
	atomic_set(&inth->allowed, 0);
	list_add_tail(&inth->head, &device->intr.prio[prio]);
	return 0;
}
