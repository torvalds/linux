// SPDX-License-Identifier: GPL-2.0
/*
 * non-coherent cache functions for Andes AX45MP
 *
 * Copyright (C) 2023 Renesas Electronics Corp.
 */

#include <linux/cacheflush.h>
#include <linux/cacheinfo.h>
#include <linux/dma-direction.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>

#include <asm/dma-noncoherent.h>

/* L2 cache registers */
#define AX45MP_L2C_REG_CTL_OFFSET		0x8

#define AX45MP_L2C_REG_C0_CMD_OFFSET		0x40
#define AX45MP_L2C_REG_C0_ACC_OFFSET		0x48
#define AX45MP_L2C_REG_STATUS_OFFSET		0x80

/* D-cache operation */
#define AX45MP_CCTL_L1D_VA_INVAL		0 /* Invalidate an L1 cache entry */
#define AX45MP_CCTL_L1D_VA_WB			1 /* Write-back an L1 cache entry */

/* L2 CCTL status */
#define AX45MP_CCTL_L2_STATUS_IDLE		0

/* L2 CCTL status cores mask */
#define AX45MP_CCTL_L2_STATUS_C0_MASK		0xf

/* L2 cache operation */
#define AX45MP_CCTL_L2_PA_INVAL			0x8 /* Invalidate an L2 cache entry */
#define AX45MP_CCTL_L2_PA_WB			0x9 /* Write-back an L2 cache entry */

#define AX45MP_L2C_REG_PER_CORE_OFFSET		0x10
#define AX45MP_CCTL_L2_STATUS_PER_CORE_OFFSET	4

#define AX45MP_L2C_REG_CN_CMD_OFFSET(n)	\
	(AX45MP_L2C_REG_C0_CMD_OFFSET + ((n) * AX45MP_L2C_REG_PER_CORE_OFFSET))
#define AX45MP_L2C_REG_CN_ACC_OFFSET(n)	\
	(AX45MP_L2C_REG_C0_ACC_OFFSET + ((n) * AX45MP_L2C_REG_PER_CORE_OFFSET))
#define AX45MP_CCTL_L2_STATUS_CN_MASK(n)	\
	(AX45MP_CCTL_L2_STATUS_C0_MASK << ((n) * AX45MP_CCTL_L2_STATUS_PER_CORE_OFFSET))

#define AX45MP_CCTL_REG_UCCTLBEGINADDR_NUM	0x80b
#define AX45MP_CCTL_REG_UCCTLCOMMAND_NUM	0x80c

#define AX45MP_CACHE_LINE_SIZE			64

struct ax45mp_priv {
	void __iomem *l2c_base;
	u32 ax45mp_cache_line_size;
};

static struct ax45mp_priv ax45mp_priv;

/* L2 Cache operations */
static inline uint32_t ax45mp_cpu_l2c_get_cctl_status(void)
{
	return readl(ax45mp_priv.l2c_base + AX45MP_L2C_REG_STATUS_OFFSET);
}

static void ax45mp_cpu_cache_operation(unsigned long start, unsigned long end,
				       unsigned int l1_op, unsigned int l2_op)
{
	unsigned long line_size = ax45mp_priv.ax45mp_cache_line_size;
	void __iomem *base = ax45mp_priv.l2c_base;
	int mhartid = smp_processor_id();
	unsigned long pa;

	while (end > start) {
		csr_write(AX45MP_CCTL_REG_UCCTLBEGINADDR_NUM, start);
		csr_write(AX45MP_CCTL_REG_UCCTLCOMMAND_NUM, l1_op);

		pa = virt_to_phys((void *)start);
		writel(pa, base + AX45MP_L2C_REG_CN_ACC_OFFSET(mhartid));
		writel(l2_op, base + AX45MP_L2C_REG_CN_CMD_OFFSET(mhartid));
		while ((ax45mp_cpu_l2c_get_cctl_status() &
			AX45MP_CCTL_L2_STATUS_CN_MASK(mhartid)) !=
			AX45MP_CCTL_L2_STATUS_IDLE)
			;

		start += line_size;
	}
}

/* Write-back L1 and L2 cache entry */
static inline void ax45mp_cpu_dcache_wb_range(unsigned long start, unsigned long end)
{
	ax45mp_cpu_cache_operation(start, end, AX45MP_CCTL_L1D_VA_WB,
				   AX45MP_CCTL_L2_PA_WB);
}

/* Invalidate the L1 and L2 cache entry */
static inline void ax45mp_cpu_dcache_inval_range(unsigned long start, unsigned long end)
{
	ax45mp_cpu_cache_operation(start, end, AX45MP_CCTL_L1D_VA_INVAL,
				   AX45MP_CCTL_L2_PA_INVAL);
}

static void ax45mp_dma_cache_inv(phys_addr_t paddr, size_t size)
{
	unsigned long start = (unsigned long)phys_to_virt(paddr);
	unsigned long end = start + size;
	unsigned long line_size;
	unsigned long flags;

	if (unlikely(start == end))
		return;

	line_size = ax45mp_priv.ax45mp_cache_line_size;

	start = start & (~(line_size - 1));
	end = ((end + line_size - 1) & (~(line_size - 1)));

	local_irq_save(flags);

	ax45mp_cpu_dcache_inval_range(start, end);

	local_irq_restore(flags);
}

static void ax45mp_dma_cache_wback(phys_addr_t paddr, size_t size)
{
	unsigned long start = (unsigned long)phys_to_virt(paddr);
	unsigned long end = start + size;
	unsigned long line_size;
	unsigned long flags;

	line_size = ax45mp_priv.ax45mp_cache_line_size;
	start = start & (~(line_size - 1));
	local_irq_save(flags);
	ax45mp_cpu_dcache_wb_range(start, end);
	local_irq_restore(flags);
}

static void ax45mp_dma_cache_wback_inv(phys_addr_t paddr, size_t size)
{
	ax45mp_dma_cache_wback(paddr, size);
	ax45mp_dma_cache_inv(paddr, size);
}

static int ax45mp_get_l2_line_size(struct device_node *np)
{
	int ret;

	ret = of_property_read_u32(np, "cache-line-size", &ax45mp_priv.ax45mp_cache_line_size);
	if (ret) {
		pr_err("Failed to get cache-line-size, defaulting to 64 bytes\n");
		return ret;
	}

	if (ax45mp_priv.ax45mp_cache_line_size != AX45MP_CACHE_LINE_SIZE) {
		pr_err("Expected cache-line-size to be 64 bytes (found:%u)\n",
		       ax45mp_priv.ax45mp_cache_line_size);
		return -EINVAL;
	}

	return 0;
}

static const struct riscv_nonstd_cache_ops ax45mp_cmo_ops __initdata = {
	.wback = &ax45mp_dma_cache_wback,
	.inv = &ax45mp_dma_cache_inv,
	.wback_inv = &ax45mp_dma_cache_wback_inv,
};

static const struct of_device_id ax45mp_cache_ids[] = {
	{ .compatible = "andestech,ax45mp-cache" },
	{ /* sentinel */ }
};

static int __init ax45mp_cache_init(void)
{
	struct device_node *np;
	struct resource res;
	int ret;

	np = of_find_matching_node(NULL, ax45mp_cache_ids);
	if (!of_device_is_available(np))
		return -ENODEV;

	ret = of_address_to_resource(np, 0, &res);
	if (ret)
		return ret;

	/*
	 * If IOCP is present on the Andes AX45MP core riscv_cbom_block_size
	 * will be 0 for sure, so we can definitely rely on it. If
	 * riscv_cbom_block_size = 0 we don't need to handle CMO using SW any
	 * more so we just return success here and only if its being set we
	 * continue further in the probe path.
	 */
	if (!riscv_cbom_block_size)
		return 0;

	ax45mp_priv.l2c_base = ioremap(res.start, resource_size(&res));
	if (!ax45mp_priv.l2c_base)
		return -ENOMEM;

	ret = ax45mp_get_l2_line_size(np);
	if (ret) {
		iounmap(ax45mp_priv.l2c_base);
		return ret;
	}

	riscv_noncoherent_register_cache_ops(&ax45mp_cmo_ops);

	return 0;
}
early_initcall(ax45mp_cache_init);
