/*
 * Copyright 2014 Red Hat Inc.
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
 * Authors: Ben Skeggs
 */

#include "acpi.h"

#ifdef CONFIG_ACPI
static int
nvkm_acpi_ntfy(struct notifier_block *nb, unsigned long val, void *data)
{
	struct nouveau_device *device =
		container_of(nb, typeof(*device), acpi.nb);
	struct acpi_bus_event *info = data;

	if (!strcmp(info->device_class, "ac_adapter"))
		nvkm_event_send(&device->event, 1, 0, NULL, 0);

	return NOTIFY_DONE;
}
#endif

int
nvkm_acpi_fini(struct nouveau_device *device, bool suspend)
{
#ifdef CONFIG_ACPI
	unregister_acpi_notifier(&device->acpi.nb);
#endif
	return 0;
}

int
nvkm_acpi_init(struct nouveau_device *device)
{
#ifdef CONFIG_ACPI
	device->acpi.nb.notifier_call = nvkm_acpi_ntfy;
	register_acpi_notifier(&device->acpi.nb);
#endif
	return 0;
}
