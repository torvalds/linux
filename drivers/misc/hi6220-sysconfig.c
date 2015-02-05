/*
 * For Hisilicon Hi6220 SoC, the reset of some hosts (e.g. UART) should be disabled
 * before using them, this driver will handle the host chip reset disable.
 *
 * Copyright (C) 2015 Hisilicon Ltd.
 * Author: Bintian Wang <bintian.wang@huawei.com>
 *
 */

#include <linux/of.h>
#include <linux/of_address.h>

#define reset_offset 0x334
#define pclk_offset 0x230

static int __init hi6220_sysconf(void)
{
        static void __iomem *base = NULL;
        struct device_node *node;

        node = of_find_compatible_node(NULL, NULL, "hisilicon,sysctrl");
        if (!node)
                return -ENOENT;

        base = of_iomap(node, 0);
        if (base == NULL) {
                printk(KERN_ERR "hi6220: sysctrl reg iomap failed!\n");
                return -ENOMEM;
        }

        /*Disable UART1 reset and set pclk*/
        writel(BIT(5), base + reset_offset);
        writel(BIT(5), base + pclk_offset);

        /*Disable UART2 reset and set pclk*/
        writel(BIT(6), base + reset_offset);
        writel(BIT(6), base + pclk_offset);

        /*Disable UART3 reset and set pclk*/
        writel(BIT(7), base + reset_offset);
        writel(BIT(7), base + pclk_offset);

        /*Disable UART4 reset and set pclk*/
        writel(BIT(8), base + reset_offset);
        writel(BIT(8), base + pclk_offset);

        iounmap(base);

        return 0;
}
postcore_initcall(hi6220_sysconf);

#ifdef CONFIG_ARM64
#ifdef CONFIG_SPARSE_IRQ
#define NR_IRQS_LEGACY_HI6220 16

int __init arch_probe_nr_irqs(void)
{
	return NR_IRQS_LEGACY_HI6220;
}

#endif
#endif
