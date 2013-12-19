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

int nv_info_debug_level = NV_DBG_INFO_NORMAL;

void
nv_printk_(struct nouveau_object *object, int level, const char *fmt, ...)
{
	static const char name[] = { '!', 'E', 'W', ' ', 'D', 'T', 'P', 'S' };
	const char *pfx;
	char mfmt[256];
	va_list args;

	switch (level) {
	case NV_DBG_FATAL:
		pfx = KERN_CRIT;
		break;
	case NV_DBG_ERROR:
		pfx = KERN_ERR;
		break;
	case NV_DBG_WARN:
		pfx = KERN_WARNING;
		break;
	case NV_DBG_INFO_NORMAL:
		pfx = KERN_INFO;
		break;
	case NV_DBG_DEBUG:
	case NV_DBG_PARANOIA:
	case NV_DBG_TRACE:
	case NV_DBG_SPAM:
	default:
		pfx = KERN_DEBUG;
		break;
	}

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
