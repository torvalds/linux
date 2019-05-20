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

void __drm_puts_coredump(struct drm_printer *p, const char *str)
{
	struct drm_print_iterator *iterator = p->arg;
	ssize_t len;

	if (!iterator->remain)
		return;

	if (iterator->offset < iterator->start) {
		ssize_t copy;

		len = strlen(str);

		if (iterator->offset + len <= iterator->start) {
			iterator->offset += len;
			return;
		}

		copy = len - (iterator->start - iterator->offset);

		if (copy > iterator->remain)
			copy = iterator->remain;

		/* Copy out the bit of the string that we need */
		memcpy(iterator->data,
			str + (iterator->start - iterator->offset), copy);

		iterator->offset = iterator->start + copy;
		iterator->remain -= copy;
	} else {
		ssize_t pos = iterator->offset - iterator->start;

		len = min_t(ssize_t, strlen(str), iterator->remain);

		memcpy(iterator->data + pos, str, len);

		iterator->offset += len;
		iterator->remain -= len;
	}
}
EXPORT_SYMBOL(__drm_puts_coredump);

void __drm_printfn_coredump(struct drm_printer *p, struct va_format *vaf)
{
	struct drm_print_iterator *iterator = p->arg;
	size_t len;
	char *buf;

	if (!iterator->remain)
		return;

	/* Figure out how big the string will be */
	len = snprintf(NULL, 0, "%pV", vaf);

	/* This is the easiest path, we've already advanced beyond the offset */
	if (iterator->offset + len <= iterator->start) {
		iterator->offset += len;
		return;
	}

	/* Then check if we can directly copy into the target buffer */
	if ((iterator->offset >= iterator->start) && (len < iterator->remain)) {
		ssize_t pos = iterator->offset - iterator->start;

		snprintf(((char *) iterator->data) + pos,
			iterator->remain, "%pV", vaf);

		iterator->offset += len;
		iterator->remain -= len;

		return;
	}

	/*
	 * Finally, hit the slow path and make a temporary string to copy over
	 * using _drm_puts_coredump
	 */
	buf = kmalloc(len + 1, GFP_KERNEL | __GFP_NOWARN | __GFP_NORETRY);
	if (!buf)
		return;

	snprintf(buf, len + 1, "%pV", vaf);
	__drm_puts_coredump(p, (const char *) buf);

	kfree(buf);
}
EXPORT_SYMBOL(__drm_printfn_coredump);

void __drm_puts_seq_file(struct drm_printer *p, const char *str)
{
	seq_puts(p->arg, str);
}
EXPORT_SYMBOL(__drm_puts_seq_file);

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
 * drm_puts - print a const string to a &drm_printer stream
 * @p: the &drm printer
 * @str: const string
 *
 * Allow &drm_printer types that have a constant string
 * option to use it.
 */
void drm_puts(struct drm_printer *p, const char *str)
{
	if (p->puts)
		p->puts(p, str);
	else
		drm_printf(p, "%s", str);
}
EXPORT_SYMBOL(drm_puts);

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

void drm_dev_printk(const struct device *dev, const char *level,
		    const char *format, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, format);
	vaf.fmt = format;
	vaf.va = &args;

	if (dev)
		dev_printk(level, dev, "[" DRM_NAME ":%ps] %pV",
			   __builtin_return_address(0), &vaf);
	else
		printk("%s" "[" DRM_NAME ":%ps] %pV",
		       level, __builtin_return_address(0), &vaf);

	va_end(args);
}
EXPORT_SYMBOL(drm_dev_printk);

void drm_dev_dbg(const struct device *dev, unsigned int category,
		 const char *format, ...)
{
	struct va_format vaf;
	va_list args;

	if (!(drm_debug & category))
		return;

	va_start(args, format);
	vaf.fmt = format;
	vaf.va = &args;

	if (dev)
		dev_printk(KERN_DEBUG, dev, "[" DRM_NAME ":%ps] %pV",
			   __builtin_return_address(0), &vaf);
	else
		printk(KERN_DEBUG "[" DRM_NAME ":%ps] %pV",
		       __builtin_return_address(0), &vaf);

	va_end(args);
}
EXPORT_SYMBOL(drm_dev_dbg);

void drm_dbg(unsigned int category, const char *format, ...)
{
	struct va_format vaf;
	va_list args;

	if (!(drm_debug & category))
		return;

	va_start(args, format);
	vaf.fmt = format;
	vaf.va = &args;

	printk(KERN_DEBUG "[" DRM_NAME ":%ps] %pV",
	       __builtin_return_address(0), &vaf);

	va_end(args);
}
EXPORT_SYMBOL(drm_dbg);

void drm_err(const char *format, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, format);
	vaf.fmt = format;
	vaf.va = &args;

	printk(KERN_ERR "[" DRM_NAME ":%ps] *ERROR* %pV",
	       __builtin_return_address(0), &vaf);

	va_end(args);
}
EXPORT_SYMBOL(drm_err);

/**
 * drm_print_regset32 - print the contents of registers to a
 * &drm_printer stream.
 *
 * @p: the &drm printer
 * @regset: the list of registers to print.
 *
 * Often in driver debug, it's useful to be able to either capture the
 * contents of registers in the steady state using debugfs or at
 * specific points during operation.  This lets the driver have a
 * single list of registers for both.
 */
void drm_print_regset32(struct drm_printer *p, struct debugfs_regset32 *regset)
{
	int namelen = 0;
	int i;

	for (i = 0; i < regset->nregs; i++)
		namelen = max(namelen, (int)strlen(regset->regs[i].name));

	for (i = 0; i < regset->nregs; i++) {
		drm_printf(p, "%*s = 0x%08x\n",
			   namelen, regset->regs[i].name,
			   readl(regset->base + regset->regs[i].offset));
	}
}
EXPORT_SYMBOL(drm_print_regset32);
