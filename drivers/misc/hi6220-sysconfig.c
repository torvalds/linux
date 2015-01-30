/*
 * For Hisilicon Hi6220 SoC, the reset of some hosts (e.g. UART) should be disabled
 * before using them, this driver will handle the host chip reset disable.
 *
 * Copyright (C) 2015 Hisilicon Ltd.
 * Author: Bintian Wang <bintian.wang@huawei.com>
 *
 */

#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>

#define reset_offset 0x334
#define pclk_offset 0x230
#define PMUSSI_REG_EX(pmu_base, reg_addr) (((reg_addr) << 2) + (char *)pmu_base)

static int __init hi6220_sysconf(void)
{
        static void __iomem *base = NULL, *base1 = NULL;
        struct device_node *node, *node1;
	unsigned char ret;

        node = of_find_compatible_node(NULL, NULL, "hisilicon,hi6220-sysctrl");
        if (!node)
                return -ENOENT;

        base = of_iomap(node, 0);
        if (base == NULL) {
                printk(KERN_ERR "hi6220: sysctrl reg iomap failed!\n");
                return -ENOMEM;
        }

        node1 = of_find_compatible_node(NULL, NULL, "hisilicon,hi655x-pmic-driver");
        if (!node1)
                return -ENOENT;

        base1 = of_iomap(node1, 0);
        if (base1 == NULL) {
                printk(KERN_ERR "hi6220: pmic reg iomap failed!\n");
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

	/*unreset microSD*/
	writel(readl(base+0x304) | 0x6, base + 0x304);

	/*enable clk for BT/WIFI*/
	ret = *(volatile unsigned char*)PMUSSI_REG_EX(base1, 0x1c);
	ret |= 0x40;
	*(volatile unsigned char*)PMUSSI_REG_EX(base1, 0x1c) = ret;

        iounmap(base);
        iounmap(base1);

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
