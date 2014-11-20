/*
 * (c) Copyright 2006 Benjamin Herrenschmidt, IBM Corp.
 *                    <benh@kernel.crashing.org>
 *
 *   This program is free software;  you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY;  without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program;  if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#undef DEBUG

#include <linux/kernel.h>
#include <linux/export.h>
#include <asm/prom.h>
#include <asm/dcr.h>

#ifdef CONFIG_PPC_DCR_MMIO
static struct device_node *find_dcr_parent(struct device_node *node)
{
	struct device_node *par, *tmp;
	const u32 *p;

	for (par = of_node_get(node); par;) {
		if (of_get_property(par, "dcr-controller", NULL))
			break;
		p = of_get_property(par, "dcr-parent", NULL);
		tmp = par;
		if (p == NULL)
			par = of_get_parent(par);
		else
			par = of_find_node_by_phandle(*p);
		of_node_put(tmp);
	}
	return par;
}
#endif

#if defined(CONFIG_PPC_DCR_NATIVE) && defined(CONFIG_PPC_DCR_MMIO)

bool dcr_map_ok_generic(dcr_host_t host)
{
	if (host.type == DCR_HOST_NATIVE)
		return dcr_map_ok_native(host.host.native);
	else if (host.type == DCR_HOST_MMIO)
		return dcr_map_ok_mmio(host.host.mmio);
	else
		return 0;
}
EXPORT_SYMBOL_GPL(dcr_map_ok_generic);

dcr_host_t dcr_map_generic(struct device_node *dev,
			   unsigned int dcr_n,
			   unsigned int dcr_c)
{
	dcr_host_t host;
	struct device_node *dp;
	const char *prop;

	host.type = DCR_HOST_INVALID;

	dp = find_dcr_parent(dev);
	if (dp == NULL)
		return host;

	prop = of_get_property(dp, "dcr-access-method", NULL);

	pr_debug("dcr_map_generic(dcr-access-method = %s)\n", prop);

	if (!strcmp(prop, "native")) {
		host.type = DCR_HOST_NATIVE;
		host.host.native = dcr_map_native(dev, dcr_n, dcr_c);
	} else if (!strcmp(prop, "mmio")) {
		host.type = DCR_HOST_MMIO;
		host.host.mmio = dcr_map_mmio(dev, dcr_n, dcr_c);
	}

	of_node_put(dp);
	return host;
}
EXPORT_SYMBOL_GPL(dcr_map_generic);

void dcr_unmap_generic(dcr_host_t host, unsigned int dcr_c)
{
	if (host.type == DCR_HOST_NATIVE)
		dcr_unmap_native(host.host.native, dcr_c);
	else if (host.type == DCR_HOST_MMIO)
		dcr_unmap_mmio(host.host.mmio, dcr_c);
	else /* host.type == DCR_HOST_INVALID */
		WARN_ON(true);
}
EXPORT_SYMBOL_GPL(dcr_unmap_generic);

u32 dcr_read_generic(dcr_host_t host, unsigned int dcr_n)
{
	if (host.type == DCR_HOST_NATIVE)
		return dcr_read_native(host.host.native, dcr_n);
	else if (host.type == DCR_HOST_MMIO)
		return dcr_read_mmio(host.host.mmio, dcr_n);
	else /* host.type == DCR_HOST_INVALID */
		WARN_ON(true);
	return 0;
}
EXPORT_SYMBOL_GPL(dcr_read_generic);

void dcr_write_generic(dcr_host_t host, unsigned int dcr_n, u32 value)
{
	if (host.type == DCR_HOST_NATIVE)
		dcr_write_native(host.host.native, dcr_n, value);
	else if (host.type == DCR_HOST_MMIO)
		dcr_write_mmio(host.host.mmio, dcr_n, value);
	else /* host.type == DCR_HOST_INVALID */
		WARN_ON(true);
}
EXPORT_SYMBOL_GPL(dcr_write_generic);

#endif /* defined(CONFIG_PPC_DCR_NATIVE) && defined(CONFIG_PPC_DCR_MMIO) */

unsigned int dcr_resource_start(const struct device_node *np,
				unsigned int index)
{
	unsigned int ds;
	const u32 *dr = of_get_property(np, "dcr-reg", &ds);

	if (dr == NULL || ds & 1 || index >= (ds / 8))
		return 0;

	return dr[index * 2];
}
EXPORT_SYMBOL_GPL(dcr_resource_start);

unsigned int dcr_resource_len(const struct device_node *np, unsigned int index)
{
	unsigned int ds;
	const u32 *dr = of_get_property(np, "dcr-reg", &ds);

	if (dr == NULL || ds & 1 || index >= (ds / 8))
		return 0;

	return dr[index * 2 + 1];
}
EXPORT_SYMBOL_GPL(dcr_resource_len);

#ifdef CONFIG_PPC_DCR_MMIO

static u64 of_translate_dcr_address(struct device_node *dev,
				    unsigned int dcr_n,
				    unsigned int *out_stride)
{
	struct device_node *dp;
	const u32 *p;
	unsigned int stride;
	u64 ret = OF_BAD_ADDR;

	dp = find_dcr_parent(dev);
	if (dp == NULL)
		return OF_BAD_ADDR;

	/* Stride is not properly defined yet, default to 0x10 for Axon */
	p = of_get_property(dp, "dcr-mmio-stride", NULL);
	stride = (p == NULL) ? 0x10 : *p;

	/* XXX FIXME: Which property name is to use of the 2 following ? */
	p = of_get_property(dp, "dcr-mmio-range", NULL);
	if (p == NULL)
		p = of_get_property(dp, "dcr-mmio-space", NULL);
	if (p == NULL)
		goto done;

	/* Maybe could do some better range checking here */
	ret = of_translate_address(dp, p);
	if (ret != OF_BAD_ADDR)
		ret += (u64)(stride) * (u64)dcr_n;
	if (out_stride)
		*out_stride = stride;

 done:
	of_node_put(dp);
	return ret;
}

dcr_host_mmio_t dcr_map_mmio(struct device_node *dev,
			     unsigned int dcr_n,
			     unsigned int dcr_c)
{
	dcr_host_mmio_t ret = { .token = NULL, .stride = 0, .base = dcr_n };
	u64 addr;

	pr_debug("dcr_map(%s, 0x%x, 0x%x)\n",
		 dev->full_name, dcr_n, dcr_c);

	addr = of_translate_dcr_address(dev, dcr_n, &ret.stride);
	pr_debug("translates to addr: 0x%llx, stride: 0x%x\n",
		 (unsigned long long) addr, ret.stride);
	if (addr == OF_BAD_ADDR)
		return ret;
	pr_debug("mapping 0x%x bytes\n", dcr_c * ret.stride);
	ret.token = ioremap(addr, dcr_c * ret.stride);
	if (ret.token == NULL)
		return ret;
	pr_debug("mapped at 0x%p -> base is 0x%p\n",
		 ret.token, ret.token - dcr_n * ret.stride);
	ret.token -= dcr_n * ret.stride;
	return ret;
}
EXPORT_SYMBOL_GPL(dcr_map_mmio);

void dcr_unmap_mmio(dcr_host_mmio_t host, unsigned int dcr_c)
{
	dcr_host_mmio_t h = host;

	if (h.token == NULL)
		return;
	h.token += host.base * h.stride;
	iounmap(h.token);
	h.token = NULL;
}
EXPORT_SYMBOL_GPL(dcr_unmap_mmio);

#endif /* defined(CONFIG_PPC_DCR_MMIO) */

#ifdef CONFIG_PPC_DCR_NATIVE
DEFINE_SPINLOCK(dcr_ind_lock);
EXPORT_SYMBOL_GPL(dcr_ind_lock);
#endif	/* defined(CONFIG_PPC_DCR_NATIVE) */

