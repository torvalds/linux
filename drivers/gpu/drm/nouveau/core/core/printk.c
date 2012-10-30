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

#include <core/object.h>
#include <core/client.h>
#include <core/subdev.h>
#include <core/printk.h>

void
nv_printk_(struct nouveau_object *object, const char *pfx, int level,
	   const char *fmt, ...)
{
	static const char name[] = { '!', 'E', 'W', ' ', 'D', 'T', 'P', 'S' };
	char mfmt[256];
	va_list args;

	if (object && !nv_iclass(object, NV_CLIENT_CLASS)) {
		struct nouveau_object *device = object;
		struct nouveau_object *subdev = object;
		char obuf[64], *ofmt = "";

		if (object->engine) {
			snprintf(obuf, sizeof(obuf), "[0x%08x][%p]",
				 nv_hclass(object), object);
			ofmt = obuf;
			subdev = object->engine;
			device = object->engine;
		}

		if (subdev->parent)
			device = subdev->parent;

		if (level > nv_subdev(subdev)->debug)
			return;

		snprintf(mfmt, sizeof(mfmt), "%snouveau %c[%8s][%s]%s %s", pfx,
			 name[level], nv_subdev(subdev)->name,
			 nv_device(device)->name, ofmt, fmt);
	} else
	if (object && nv_iclass(object, NV_CLIENT_CLASS)) {
		if (level > nv_client(object)->debug)
			return;

		snprintf(mfmt, sizeof(mfmt), "%snouveau %c[%8s] %s", pfx,
			 name[level], nv_client(object)->name, fmt);
	} else {
		snprintf(mfmt, sizeof(mfmt), "%snouveau: %s", pfx, fmt);
	}

	va_start(args, fmt);
	vprintk(mfmt, args);
	va_end(args);
}
