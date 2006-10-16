/*
 * This file does the necessary interface mapping between the bootwrapper
 * device tree operations and the interface provided by shared source
 * files flatdevicetree.[ch].
 *
 * Author: Mark A. Greer <mgreer@mvista.com>
 *
 * 2006 (c) MontaVista Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */
#include <stddef.h>
#include "flatdevtree.h"
#include "ops.h"

static struct ft_cxt cxt;

static void *ft_finddevice(const char *name)
{
	return ft_find_device(&cxt, name);
}

static int ft_getprop(const void *phandle, const char *propname, void *buf,
		const int buflen)
{
	return ft_get_prop(&cxt, phandle, propname, buf, buflen);
}

static int ft_setprop(const void *phandle, const char *propname,
		const void *buf, const int buflen)
{
	return ft_set_prop(&cxt, phandle, propname, buf, buflen);
}

static void ft_pack(void)
{
	ft_end_tree(&cxt);
}

static unsigned long ft_addr(void)
{
	return (unsigned long)cxt.bph;
}

int ft_init(void *dt_blob, unsigned int max_size, unsigned int max_find_device)
{
	dt_ops.finddevice = ft_finddevice;
	dt_ops.getprop = ft_getprop;
	dt_ops.setprop = ft_setprop;
	dt_ops.ft_pack = ft_pack;
	dt_ops.ft_addr = ft_addr;

	return ft_open(&cxt, dt_blob, max_size, max_find_device,
			platform_ops.realloc);
}
