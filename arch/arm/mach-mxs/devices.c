/*
 * Copyright 2008 Sascha Hauer, kernel@pengutronix.de
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/amba/bus.h>

struct platform_device *__init mxs_add_platform_device_dmamask(
		const char *name, int id,
		const struct resource *res, unsigned int num_resources,
		const void *data, size_t size_data, u64 dmamask)
{
	int ret = -ENOMEM;
	struct platform_device *pdev;

	pdev = platform_device_alloc(name, id);
	if (!pdev)
		goto err;

	if (dmamask) {
		/*
		 * This memory isn't freed when the device is put,
		 * I don't have a nice idea for that though.  Conceptually
		 * dma_mask in struct device should not be a pointer.
		 * See http://thread.gmane.org/gmane.linux.kernel.pci/9081
		 */
		pdev->dev.dma_mask =
			kmalloc(sizeof(*pdev->dev.dma_mask), GFP_KERNEL);
		if (!pdev->dev.dma_mask)
			/* ret is still -ENOMEM; */
			goto err;

		*pdev->dev.dma_mask = dmamask;
		pdev->dev.coherent_dma_mask = dmamask;
	}

	if (res) {
		ret = platform_device_add_resources(pdev, res, num_resources);
		if (ret)
			goto err;
	}

	if (data) {
		ret = platform_device_add_data(pdev, data, size_data);
		if (ret)
			goto err;
	}

	ret = platform_device_add(pdev);
	if (ret) {
err:
		if (dmamask)
			kfree(pdev->dev.dma_mask);
		platform_device_put(pdev);
		return ERR_PTR(ret);
	}

	return pdev;
}

int __init mxs_add_amba_device(const struct amba_device *dev)
{
	struct amba_device *adev = kmalloc(sizeof(*adev), GFP_KERNEL);

	if (!adev) {
		pr_err("%s: failed to allocate memory", __func__);
		return -ENOMEM;
	}

	*adev = *dev;

	return amba_device_register(adev, &iomem_resource);
}
