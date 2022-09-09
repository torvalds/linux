/* Copyright 2017 NXP Semiconductor, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *	 notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *	 notice, this list of conditions and the following disclaimer in the
 *	 documentation and/or other materials provided with the distribution.
 *     * Neither the name of NXP Semiconductor nor the
 *	 names of its contributors may be used to endorse or promote products
 *	 derived from this software without specific prior written permission.
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") as published by the Free Software
 * Foundation, either version 2 of that License or (at your option) any
 * later version.
 *
 * THIS SOFTWARE IS PROVIDED BY NXP Semiconductor ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL NXP Semiconductor BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <linux/dma-mapping.h>
#include "dpaa_sys.h"

/*
 * Initialize a devices private memory region
 */
int qbman_init_private_mem(struct device *dev, int idx, dma_addr_t *addr,
				size_t *size)
{
	struct device_node *mem_node;
	struct reserved_mem *rmem;
	struct property *prop;
	int len, err;
	__be32 *res_array;

	mem_node = of_parse_phandle(dev->of_node, "memory-region", idx);
	if (!mem_node) {
		dev_err(dev, "No memory-region found for index %d\n", idx);
		return -ENODEV;
	}

	rmem = of_reserved_mem_lookup(mem_node);
	if (!rmem) {
		dev_err(dev, "of_reserved_mem_lookup() returned NULL\n");
		return -ENODEV;
	}
	*addr = rmem->base;
	*size = rmem->size;

	/*
	 * Check if the reg property exists - if not insert the node
	 * so upon kexec() the same memory region address will be preserved.
	 * This is needed because QBMan HW does not allow the base address/
	 * size to be modified once set.
	 */
	prop = of_find_property(mem_node, "reg", &len);
	if (!prop) {
		prop = devm_kzalloc(dev, sizeof(*prop), GFP_KERNEL);
		if (!prop)
			return -ENOMEM;
		prop->value = res_array = devm_kzalloc(dev, sizeof(__be32) * 4,
						       GFP_KERNEL);
		if (!prop->value)
			return -ENOMEM;
		res_array[0] = cpu_to_be32(upper_32_bits(*addr));
		res_array[1] = cpu_to_be32(lower_32_bits(*addr));
		res_array[2] = cpu_to_be32(upper_32_bits(*size));
		res_array[3] = cpu_to_be32(lower_32_bits(*size));
		prop->length = sizeof(__be32) * 4;
		prop->name = devm_kstrdup(dev, "reg", GFP_KERNEL);
		if (!prop->name)
			return -ENOMEM;
		err = of_add_property(mem_node, prop);
		if (err)
			return err;
	}

	return 0;
}
