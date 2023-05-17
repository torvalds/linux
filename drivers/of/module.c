// SPDX-License-Identifier: GPL-2.0
/*
 * Linux kernel module helpers.
 */

#include <linux/of.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>

ssize_t of_modalias(const struct device_node *np, char *str, ssize_t len)
{
	const char *compat;
	char *c;
	struct property *p;
	ssize_t csize;
	ssize_t tsize;

	/* Name & Type */
	/* %p eats all alphanum characters, so %c must be used here */
	csize = snprintf(str, len, "of:N%pOFn%c%s", np, 'T',
			 of_node_get_device_type(np));
	tsize = csize;
	len -= csize;
	if (str)
		str += csize;

	of_property_for_each_string(np, "compatible", p, compat) {
		csize = strlen(compat) + 1;
		tsize += csize;
		if (csize > len)
			continue;

		csize = snprintf(str, len, "C%s", compat);
		for (c = str; c; ) {
			c = strchr(c, ' ');
			if (c)
				*c++ = '_';
		}
		len -= csize;
		str += csize;
	}

	return tsize;
}

int of_request_module(const struct device_node *np)
{
	char *str;
	ssize_t size;
	int ret;

	if (!np)
		return -ENODEV;

	size = of_modalias(np, NULL, 0);
	if (size < 0)
		return size;

	/* Reserve an additional byte for the trailing '\0' */
	size++;

	str = kmalloc(size, GFP_KERNEL);
	if (!str)
		return -ENOMEM;

	of_modalias(np, str, size);
	str[size - 1] = '\0';
	ret = request_module(str);
	kfree(str);

	return ret;
}
EXPORT_SYMBOL_GPL(of_request_module);
