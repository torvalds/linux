// SPDX-License-Identifier: GPL-2.0

#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/memblock.h>
#include <asm/virt.h>
#include <asm/irq.h>

#define VIRTIO_BUS_NB	128

static int __init virt_virtio_init(unsigned int id)
{
	const struct resource res[] = {
		DEFINE_RES_MEM(virt_bi_data.virtio.mmio + id * 0x200, 0x200),
		DEFINE_RES_IRQ(virt_bi_data.virtio.irq + id),
	};
	struct platform_device *pdev;

	pdev = platform_device_register_simple("virtio-mmio", id,
					       res, ARRAY_SIZE(res));
	if (IS_ERR(pdev))
		return PTR_ERR(pdev);

	return 0;
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
	struct platform_device *pdev;
	unsigned int i;

	if (!MACH_IS_VIRT)
		return -ENODEV;

	/* We need this to have DMA'able memory provided to goldfish-tty */
	min_low_pfn = 0;

	pdev = platform_device_register_simple("goldfish_tty",
					       PLATFORM_DEVID_NONE,
					       goldfish_tty_res,
					       ARRAY_SIZE(goldfish_tty_res));
	if (IS_ERR(pdev))
		return PTR_ERR(pdev);

	pdev = platform_device_register_simple("goldfish_rtc",
					       PLATFORM_DEVID_NONE,
					       goldfish_rtc_res,
					       ARRAY_SIZE(goldfish_rtc_res));
	if (IS_ERR(pdev))
		return PTR_ERR(pdev);

	for (i = 0; i < VIRTIO_BUS_NB; i++) {
		int err;

		err = virt_virtio_init(i);
		if (err)
			return err;
	}

	return 0;
}

arch_initcall(virt_platform_init);
