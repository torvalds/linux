/*
 * Copyright (C) 2016 Red Hat
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
 * Authors:
 * Rob Clark <robdclark@gmail.com>
 */

#define DEBUG /* for pr_debug() */

#include <stdarg.h>
#include <linux/seq_file.h>
#include <drm/drmP.h>
#include <drm/drm_print.h>

void __drm_printfn_seq_file(struct drm_printer *p, struct va_format *vaf)
{
	seq_printf(p->arg, "%pV", vaf);
}
EXPORT_SYMBOL(__drm_printfn_seq_file);

void __drm_printfn_info(struct drm_printer *p, struct va_format *vaf)
{
	dev_info(p->arg, "[" DRM_NAME "] %pV", vaf);
}
EXPORT_SYMBOL(__drm_printfn_info);

void __drm_printfn_debug(struct drm_printer *p, struct va_format *vaf)
{
	pr_debug("%s %pV", p->prefix, vaf);
}
EXPORT_SYMBOL(__drm_printfn_debug);

/**
 * drm_printf - print to a &drm_printer stream
 * @p: the &drm_printer
 * @f: format string
 */
void drm_printf(struct drm_printer *p, const char *f, ...)
{
	va_list args;

	va_start(args, f);
	drm_vprintf(p, f, &args);
	va_end(args);
}
EXPORT_SYMBOL(drm_printf);

#define DRM_PRINTK_FMT "[" DRM_NAME ":%s]%s %pV"

void drm_dev_printk(const struct device *dev, const char *level,
		    unsigned int category, const char *function_name,
		    const char *prefix, const char *format, ...)
{
	struct va_format vaf;
	va_list args;

	if (category != DRM_UT_NONE && !(drm_debug & category))
		return;

	va_start(args, format);
	vaf.fmt = format;
	vaf.va = &args;

	if (dev)
		dev_printk(level, dev, DRM_PRINTK_FMT, function_name, prefix,
			   &vaf);
	else
		printk("%s" DRM_PRINTK_FMT, level, function_name, prefix, &vaf);

	va_end(args);
}
EXPORT_SYMBOL(drm_dev_printk);

void drm_printk(const char *level, unsigned int category,
		const char *format, ...)
{
	struct va_format vaf;
	va_list args;

	if (category != DRM_UT_NONE && !(drm_debug & category))
		return;

	va_start(args, format);
	vaf.fmt = format;
	vaf.va = &args;

	printk("%s" "[" DRM_NAME ":%ps]%s %pV",
	       level, __builtin_return_address(0),
	       strcmp(level, KERN_ERR) == 0 ? " *ERROR*" : "", &vaf);

	va_end(args);
}
EXPORT_SYMBOL(drm_printk);
