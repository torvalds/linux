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

#include <subdev/pci.h>
#include <subdev/mc.h>

static void
nvkm_intr_rearm_locked(struct nvkm_device *device)
{
	nvkm_mc_intr_rearm(device);
}

static void
nvkm_intr_unarm_locked(struct nvkm_device *device)
{
	nvkm_mc_intr_unarm(device);
}

static irqreturn_t
nvkm_intr(int irq, void *arg)
{
	struct nvkm_device *device = arg;
	irqreturn_t ret = IRQ_NONE;
	bool handled;

	spin_lock(&device->intr.lock);
	if (!device->intr.armed)
		goto done_unlock;

	nvkm_intr_unarm_locked(device);
	nvkm_pci_msi_rearm(device);

	nvkm_mc_intr(device, &handled);
	if (handled)
		ret = IRQ_HANDLED;

	nvkm_intr_rearm_locked(device);
done_unlock:
	spin_unlock(&device->intr.lock);
	return ret;
}

void
nvkm_intr_rearm(struct nvkm_device *device)
{
	spin_lock_irq(&device->intr.lock);
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
	if (device->intr.alloc)
		free_irq(device->intr.irq, device);
}

void
nvkm_intr_ctor(struct nvkm_device *device)
{
	spin_lock_init(&device->intr.lock);
}
