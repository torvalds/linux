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
#include <asm/prom.h>
#include <asm/dcr.h>

unsigned int dcr_resource_start(struct device_node *np, unsigned int index)
{
	unsigned int ds;
	const u32 *dr = of_get_property(np, "dcr-reg", &ds);

	if (dr == NULL || ds & 1 || index >= (ds / 8))
		return 0;

	return dr[index * 2];
}

unsigned int dcr_resource_len(struct device_node *np, unsigned int index)
{
	unsigned int ds;
	const u32 *dr = of_get_property(np, "dcr-reg", &ds);

	if (dr == NULL || ds & 1 || index >= (ds / 8))
		return 0;

	return dr[index * 2 + 1];
}

#ifndef CONFIG_PPC_DCR_NATIVE

static struct device_node * find_dcr_parent(struct device_node * node)
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

u64 of_translate_dcr_address(struct device_node *dev,
			     unsigned int dcr_n,
			     unsigned int *out_stride)
{
	struct device_node *dp;
	const u32 *p;
	unsigned int stride;
	u64 ret;

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
		return OF_BAD_ADDR;

	/* Maybe could do some better range checking here */
	ret = of_translate_address(dp, p);
	if (ret != OF_BAD_ADDR)
		ret += (u64)(stride) * (u64)dcr_n;
	if (out_stride)
		*out_stride = stride;
	return ret;
}

dcr_host_t dcr_map(struct device_node *dev, unsigned int dcr_n,
		   unsigned int dcr_c)
{
	dcr_host_t ret = { .token = NULL, .stride = 0 };
	u64 addr;

	pr_debug("dcr_map(%s, 0x%x, 0x%x)\n",
		 dev->full_name, dcr_n, dcr_c);

	addr = of_translate_dcr_address(dev, dcr_n, &ret.stride);
	pr_debug("translates to addr: 0x%lx, stride: 0x%x\n",
		 addr, ret.stride);
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

void dcr_unmap(dcr_host_t host, unsigned int dcr_n, unsigned int dcr_c)
{
	dcr_host_t h = host;

	if (h.token == NULL)
		return;
	h.token += dcr_n * h.stride;
	iounmap(h.token);
	h.token = NULL;
}

#endif /* !defined(CONFIG_PPC_DCR_NATIVE) */
