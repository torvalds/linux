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
	int ret;
	struct device_node *mem_node;
	u64 size64;

	ret = of_reserved_mem_device_init_by_idx(dev, dev->of_node, idx);
	if (ret) {
		dev_err(dev,
			"of_reserved_mem_device_init_by_idx(%d) failed 0x%x\n",
			idx, ret);
		return -ENODEV;
	}
	mem_node = of_parse_phandle(dev->of_node, "memory-region", 0);
	if (mem_node) {
		ret = of_property_read_u64(mem_node, "size", &size64);
		if (ret) {
			dev_err(dev, "of_address_to_resource fails 0x%x\n",
			        ret);
			return -ENODEV;
		}
		*size = size64;
	} else {
		dev_err(dev, "No memory-region found for index %d\n", idx);
		return -ENODEV;
	}

	if (!dma_alloc_coherent(dev, *size, addr, 0)) {
		dev_err(dev, "DMA Alloc memory failed\n");
		return -ENODEV;
	}

	/*
	 * Disassociate the reserved memory area from the device
	 * because a device can only have one DMA memory area. This
	 * should be fine since the memory is allocated and initialized
	 * and only ever accessed by the QBMan device from now on
	 */
	of_reserved_mem_device_release(dev);
	return 0;
}
