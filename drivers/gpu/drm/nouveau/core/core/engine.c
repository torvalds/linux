/*
 * Copyright 2012 Red Hat Inc.
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

#include <core/device.h>
#include <core/engine.h>
#include <core/option.h>

int
nouveau_engine_create_(struct nouveau_object *parent,
		       struct nouveau_object *engobj,
		       struct nouveau_oclass *oclass, bool enable,
		       const char *iname, const char *fname,
		       int length, void **pobject)
{
	struct nouveau_engine *engine;
	int ret;

	ret = nouveau_subdev_create_(parent, engobj, oclass, NV_ENGINE_CLASS,
				     iname, fname, length, pobject);
	engine = *pobject;
	if (ret)
		return ret;

	if (parent) {
		struct nouveau_device *device = nv_device(parent);
		int engidx = nv_engidx(nv_object(engine));

		if (device->disable_mask & (1ULL << engidx)) {
			if (!nouveau_boolopt(device->cfgopt, iname, false)) {
				nv_debug(engine, "engine disabled by hw/fw\n");
				return -ENODEV;
			}

			nv_warn(engine, "ignoring hw/fw engine disable\n");
		}

		if (!nouveau_boolopt(device->cfgopt, iname, enable)) {
			if (!enable)
				nv_warn(engine, "disabled, %s=1 to enable\n", iname);
			return -ENODEV;
		}
	}

	INIT_LIST_HEAD(&engine->contexts);
	spin_lock_init(&engine->lock);
	return 0;
}
