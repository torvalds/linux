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

#include <linux/debugfs.h>
#include <linux/dynamic_debug.h>
#include <linux/io.h>
#include <linux/moduleparam.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/stdarg.h>

#include <drm/drm.h>
#include <drm/drm_drv.h>
#include <drm/drm_print.h>

/*
 * __drm_debug: Enable debug output.
 * Bitmask of DRM_UT_x. See include/drm/drm_print.h for details.
 */
unsigned long __drm_debug;
EXPORT_SYMBOL(__drm_debug);

MODULE_PARM_DESC(debug, "Enable debug output, where each bit enables a debug category.\n"
"\t\tBit 0 (0x01)  will enable CORE messages (drm core code)\n"
"\t\tBit 1 (0x02)  will enable DRIVER messages (drm controller code)\n"
"\t\tBit 2 (0x04)  will enable KMS messages (modesetting code)\n"
"\t\tBit 3 (0x08)  will enable PRIME messages (prime code)\n"
"\t\tBit 4 (0x10)  will enable ATOMIC messages (atomic code)\n"
"\t\tBit 5 (0x20)  will enable VBL messages (vblank code)\n"
"\t\tBit 7 (0x80)  will enable LEASE messages (leasing code)\n"
"\t\tBit 8 (0x100) will enable DP messages (displayport code)");

#if !defined(CONFIG_DRM_USE_DYNAMIC_DEBUG)
module_param_named(debug, __drm_debug, ulong, 0600);
#else
/* classnames must match vals of enum drm_debug_category */
DECLARE_DYNDBG_CLASSMAP(drm_debug_classes, DD_CLASS_TYPE_DISJOINT_BITS, 0,
			"DRM_UT_CORE",
			"DRM_UT_DRIVER",
			"DRM_UT_KMS",
			"DRM_UT_PRIME",
			"DRM_UT_ATOMIC",
			"DRM_UT_VBL",
			"DRM_UT_STATE",
			"DRM_UT_LEASE",
			"DRM_UT_DP",
			"DRM_UT_DRMRES");

static struct ddebug_class_param drm_debug_bitmap = {
	.bits = &__drm_debug,
	.flags = "p",
	.map = &drm_debug_classes,
};
module_param_cb(debug, &param_ops_dyndbg_classes, &drm_debug_bitmap, 0600);
#endif

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

static void __drm_dev_vprintk(const struct device *dev, const char *level,
			      const void *origin, const char *prefix,
			      struct va_format *vaf)
{
	const char *prefix_pad = prefix ? " " : "";

	if (!prefix)
		prefix = "";

	if (dev) {
		if (origin)
			dev_printk(level, dev, "[" DRM_NAME ":%ps]%s%s %pV",
				   origin, prefix_pad, prefix, vaf);
		else
			dev_printk(level, dev, "[" DRM_NAME "]%s%s %pV",
				   prefix_pad, prefix, vaf);
	} else {
		if (origin)
			printk("%s" "[" DRM_NAME ":%ps]%s%s %pV",
			       level, origin, prefix_pad, prefix, vaf);
		else
			printk("%s" "[" DRM_NAME "]%s%s %pV",
			       level, prefix_pad, prefix, vaf);
	}
}

void __drm_printfn_info(struct drm_printer *p, struct va_format *vaf)
{
	dev_info(p->arg, "[" DRM_NAME "] %pV", vaf);
}
EXPORT_SYMBOL(__drm_printfn_info);

void __drm_printfn_dbg(struct drm_printer *p, struct va_format *vaf)
{
	const struct drm_device *drm = p->arg;
	const struct device *dev = drm ? drm->dev : NULL;
	enum drm_debug_category category = p->category;

	if (!__drm_debug_enabled(category))
		return;

	__drm_dev_vprintk(dev, KERN_DEBUG, p->origin, p->prefix, vaf);
}
EXPORT_SYMBOL(__drm_printfn_dbg);

void __drm_printfn_err(struct drm_printer *p, struct va_format *vaf)
{
	struct drm_device *drm = p->arg;

	if (p->prefix)
		drm_err(drm, "%s %pV", p->prefix, vaf);
	else
		drm_err(drm, "%pV", vaf);
}
EXPORT_SYMBOL(__drm_printfn_err);

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

/**
 * drm_print_bits - print bits to a &drm_printer stream
 *
 * Print bits (in flag fields for example) in human readable form.
 *
 * @p: the &drm_printer
 * @value: field value.
 * @bits: Array with bit names.
 * @nbits: Size of bit names array.
 */
void drm_print_bits(struct drm_printer *p, unsigned long value,
		    const char * const bits[], unsigned int nbits)
{
	bool first = true;
	unsigned int i;

	if (WARN_ON_ONCE(nbits > BITS_PER_TYPE(value)))
		nbits = BITS_PER_TYPE(value);

	for_each_set_bit(i, &value, nbits) {
		if (WARN_ON_ONCE(!bits[i]))
			continue;
		drm_printf(p, "%s%s", first ? "" : ",",
			   bits[i]);
		first = false;
	}
	if (first)
		drm_printf(p, "(none)");
}
EXPORT_SYMBOL(drm_print_bits);

void drm_dev_printk(const struct device *dev, const char *level,
		    const char *format, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, format);
	vaf.fmt = format;
	vaf.va = &args;

	__drm_dev_vprintk(dev, level, __builtin_return_address(0), NULL, &vaf);

	va_end(args);
}
EXPORT_SYMBOL(drm_dev_printk);

void __drm_dev_dbg(struct _ddebug *desc, const struct device *dev,
		   enum drm_debug_category category, const char *format, ...)
{
	struct va_format vaf;
	va_list args;

	if (!__drm_debug_enabled(category))
		return;

	/* we know we are printing for either syslog, tracefs, or both */
	va_start(args, format);
	vaf.fmt = format;
	vaf.va = &args;

	__drm_dev_vprintk(dev, KERN_DEBUG, __builtin_return_address(0), NULL, &vaf);

	va_end(args);
}
EXPORT_SYMBOL(__drm_dev_dbg);

void __drm_err(const char *format, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, format);
	vaf.fmt = format;
	vaf.va = &args;

	__drm_dev_vprintk(NULL, KERN_ERR, __builtin_return_address(0), "*ERROR*", &vaf);

	va_end(args);
}
EXPORT_SYMBOL(__drm_err);

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
