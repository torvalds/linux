// SPDX-License-Identifier: GPL-2.0

#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/memblock.h>
#include <asm/virt.h>
#include <asm/irq.h>

#define VIRTIO_BUS_NB	128

static struct platform_device * __init virt_virtio_init(unsigned int id)
{
	const struct resource res[] = {
		DEFINE_RES_MEM(virt_bi_data.virtio.mmio + id * 0x200, 0x200),
		DEFINE_RES_IRQ(virt_bi_data.virtio.irq + id),
	};

	return platform_device_register_simple("virtio-mmio", id,
					       res, ARRAY_SIZE(res));
}

static int __init virt_platform_init(void)
{
	const struct resource goldfish_tty_res[] = {
		DEFINE_RES_MEM(virt_bi_data.tty.mmio, 1),
		DEFINE_RES_IRQ(virt_bi_data.tty.irq),
	};
	/* this is the second gf-rtc, the first one is used by the scheduler */
	const struct resource goldfish_rtc_res[] = {
		DEFINE_RES_MEM(virt_bi_data.rtc.mmio + 0x1000, 0x1000),
		DEFINE_RES_IRQ(virt_bi_data.rtc.irq + 1),
	};
	struct platform_device *pdev1, *pdev2;
	struct platform_device *pdevs[VIRTIO_BUS_NB];
	unsigned int i;
	int ret = 0;

	if (!MACH_IS_VIRT)
		return -ENODEV;

	/* We need this to have DMA'able memory provided to goldfish-tty */
	min_low_pfn = 0;

	pdev1 = platform_device_register_simple("goldfish_tty",
						PLATFORM_DEVID_NONE,
						goldfish_tty_res,
						ARRAY_SIZE(goldfish_tty_res));
	if (IS_ERR(pdev1))
		return PTR_ERR(pdev1);

	pdev2 = platform_device_register_simple("goldfish_rtc",
						PLATFORM_DEVID_NONE,
						goldfish_rtc_res,
						ARRAY_SIZE(goldfish_rtc_res));
	if (IS_ERR(pdev2)) {
		ret = PTR_ERR(pdev2);
		goto err_unregister_tty;
	}

	for (i = 0; i < VIRTIO_BUS_NB; i++) {
		pdevs[i] = virt_virtio_init(i);
		if (IS_ERR(pdevs[i])) {
			ret = PTR_ERR(pdevs[i]);
			goto err_unregister_rtc_virtio;
		}
	}

	return 0;

err_unregister_rtc_virtio:
	while (i > 0)
		platform_device_unregister(pdevs[--i]);
	platform_device_unregister(pdev2);
err_unregister_tty:
	platform_device_unregister(pdev1);

	return ret;
}

arch_initcall(virt_platform_init);
