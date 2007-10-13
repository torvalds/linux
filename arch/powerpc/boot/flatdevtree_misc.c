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

static void *fdtm_finddevice(const char *name)
{
	return ft_find_device(&cxt, NULL, name);
}

static int fdtm_getprop(const void *phandle, const char *propname,
                        void *buf, const int buflen)
{
	return ft_get_prop(&cxt, phandle, propname, buf, buflen);
}

static int fdtm_setprop(const void *phandle, const char *propname,
                        const void *buf, const int buflen)
{
	return ft_set_prop(&cxt, phandle, propname, buf, buflen);
}

static void *fdtm_get_parent(const void *phandle)
{
	return ft_get_parent(&cxt, phandle);
}

static void *fdtm_create_node(const void *phandle, const char *name)
{
	return ft_create_node(&cxt, phandle, name);
}

static void *fdtm_find_node_by_prop_value(const void *prev,
                                          const char *propname,
                                          const char *propval,
                                          int proplen)
{
	return ft_find_node_by_prop_value(&cxt, prev, propname,
	                                  propval, proplen);
}

static unsigned long fdtm_finalize(void)
{
	ft_end_tree(&cxt);
	return (unsigned long)cxt.bph;
}

static char *fdtm_get_path(const void *phandle, char *buf, int len)
{
	return ft_get_path(&cxt, phandle, buf, len);
}

int ft_init(void *dt_blob, unsigned int max_size, unsigned int max_find_device)
{
	dt_ops.finddevice = fdtm_finddevice;
	dt_ops.getprop = fdtm_getprop;
	dt_ops.setprop = fdtm_setprop;
	dt_ops.get_parent = fdtm_get_parent;
	dt_ops.create_node = fdtm_create_node;
	dt_ops.find_node_by_prop_value = fdtm_find_node_by_prop_value;
	dt_ops.finalize = fdtm_finalize;
	dt_ops.get_path = fdtm_get_path;

	return ft_open(&cxt, dt_blob, max_size, max_find_device,
			platform_ops.realloc);
}
