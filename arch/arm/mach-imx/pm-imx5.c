/*
 *  Copyright (C) 2011 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */
#include <linux/suspend.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/export.h>

#include <linux/genalloc.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>

#include <asm/cacheflush.h>
#include <asm/fncpy.h>
#include <asm/system_misc.h>
#include <asm/tlbflush.h>

#include "common.h"
#include "cpuidle.h"
#include "hardware.h"

#define MXC_CCM_CLPCR			0x54
#define MXC_CCM_CLPCR_LPM_OFFSET	0
#define MXC_CCM_CLPCR_LPM_MASK		0x3
#define MXC_CCM_CLPCR_STBY_COUNT_OFFSET	9
#define MXC_CCM_CLPCR_VSTBY		(0x1 << 8)
#define MXC_CCM_CLPCR_SBYOS		(0x1 << 6)

#define MXC_CORTEXA8_PLAT_LPC		0xc
#define MXC_CORTEXA8_PLAT_LPC_DSM	(1 << 0)
#define MXC_CORTEXA8_PLAT_LPC_DBG_DSM	(1 << 1)

#define MXC_SRPG_NEON_SRPGCR		0x280
#define MXC_SRPG_ARM_SRPGCR		0x2a0
#define MXC_SRPG_EMPGC0_SRPGCR		0x2c0
#define MXC_SRPG_EMPGC1_SRPGCR		0x2d0

#define MXC_SRPGCR_PCR			1

/*
 * The WAIT_UNCLOCKED_POWER_OFF state only requires <= 500ns to exit.
 * This is also the lowest power state possible without affecting
 * non-cpu parts of the system.  For these reasons, imx5 should default
 * to always using this state for cpu idling.  The PM_SUSPEND_STANDBY also
 * uses this state and needs to take no action when registers remain confgiured
 * for this state.
 */
#define IMX5_DEFAULT_CPU_IDLE_STATE WAIT_UNCLOCKED_POWER_OFF

struct imx5_suspend_io_state {
	u32	offset;
	u32	clear;
	u32	set;
	u32	saved_value;
};

struct imx5_pm_data {
	phys_addr_t ccm_addr;
	phys_addr_t cortex_addr;
	phys_addr_t gpc_addr;
	phys_addr_t m4if_addr;
	phys_addr_t iomuxc_addr;
	void (*suspend_asm)(void __iomem *ocram_vbase);
	const u32 *suspend_asm_sz;
	const struct imx5_suspend_io_state *suspend_io_config;
	int suspend_io_count;
};

static const struct imx5_suspend_io_state imx53_suspend_io_config[] = {
#define MX53_DSE_HIGHZ_MASK (0x7 << 19)
	{.offset = 0x584, .clear = MX53_DSE_HIGHZ_MASK}, /* DQM0 */
	{.offset = 0x594, .clear = MX53_DSE_HIGHZ_MASK}, /* DQM1 */
	{.offset = 0x560, .clear = MX53_DSE_HIGHZ_MASK}, /* DQM2 */
	{.offset = 0x554, .clear = MX53_DSE_HIGHZ_MASK}, /* DQM3 */
	{.offset = 0x574, .clear = MX53_DSE_HIGHZ_MASK}, /* CAS */
	{.offset = 0x588, .clear = MX53_DSE_HIGHZ_MASK}, /* RAS */
	{.offset = 0x578, .clear = MX53_DSE_HIGHZ_MASK}, /* SDCLK_0 */
	{.offset = 0x570, .clear = MX53_DSE_HIGHZ_MASK}, /* SDCLK_1 */

	{.offset = 0x580, .clear = MX53_DSE_HIGHZ_MASK}, /* SDODT0 */
	{.offset = 0x564, .clear = MX53_DSE_HIGHZ_MASK}, /* SDODT1 */
	{.offset = 0x57c, .clear = MX53_DSE_HIGHZ_MASK}, /* SDQS0 */
	{.offset = 0x590, .clear = MX53_DSE_HIGHZ_MASK}, /* SDQS1 */
	{.offset = 0x568, .clear = MX53_DSE_HIGHZ_MASK}, /* SDQS2 */
	{.offset = 0x558, .clear = MX53_DSE_HIGHZ_MASK}, /* SDSQ3 */
	{.offset = 0x6f0, .clear = MX53_DSE_HIGHZ_MASK}, /* GRP_ADDS */
	{.offset = 0x718, .clear = MX53_DSE_HIGHZ_MASK}, /* GRP_BODS */
	{.offset = 0x71c, .clear = MX53_DSE_HIGHZ_MASK}, /* GRP_B1DS */
	{.offset = 0x728, .clear = MX53_DSE_HIGHZ_MASK}, /* GRP_B2DS */
	{.offset = 0x72c, .clear = MX53_DSE_HIGHZ_MASK}, /* GRP_B3DS */

	/* Controls the CKE signal which is required to leave self refresh */
	{.offset = 0x720, .clear = MX53_DSE_HIGHZ_MASK, .set = 1 << 19}, /* CTLDS */
};

static const struct imx5_pm_data imx51_pm_data __initconst = {
	.ccm_addr = 0x73fd4000,
	.cortex_addr = 0x83fa0000,
	.gpc_addr = 0x73fd8000,
};

static const struct imx5_pm_data imx53_pm_data __initconst = {
	.ccm_addr = 0x53fd4000,
	.cortex_addr = 0x63fa0000,
	.gpc_addr = 0x53fd8000,
	.m4if_addr = 0x63fd8000,
	.iomuxc_addr = 0x53fa8000,
	.suspend_asm = &imx53_suspend,
	.suspend_asm_sz = &imx53_suspend_sz,
	.suspend_io_config = imx53_suspend_io_config,
	.suspend_io_count = ARRAY_SIZE(imx53_suspend_io_config),
};

#define MX5_MAX_SUSPEND_IOSTATE ARRAY_SIZE(imx53_suspend_io_config)

/*
 * This structure is for passing necessary data for low level ocram
 * suspend code(arch/arm/mach-imx/suspend-imx53.S), if this struct
 * definition is changed, the offset definition in that file
 * must be also changed accordingly otherwise, the suspend to ocram
 * function will be broken!
 */
struct imx5_cpu_suspend_info {
	void __iomem	*m4if_base;
	void __iomem	*iomuxc_base;
	u32		io_count;
	struct imx5_suspend_io_state io_state[MX5_MAX_SUSPEND_IOSTATE];
} __aligned(8);

static void __iomem *ccm_base;
static void __iomem *cortex_base;
static void __iomem *gpc_base;
static void __iomem *suspend_ocram_base;
static void (*imx5_suspend_in_ocram_fn)(void __iomem *ocram_vbase);

/*
 * set cpu low power mode before WFI instruction. This function is called
 * mx5 because it can be used for mx51, and mx53.
 */
static void mx5_cpu_lp_set(enum mxc_cpu_pwr_mode mode)
{
	u32 plat_lpc, arm_srpgcr, ccm_clpcr;
	u32 empgc0, empgc1;
	int stop_mode = 0;

	/* always allow platform to issue a deep sleep mode request */
	plat_lpc = imx_readl(cortex_base + MXC_CORTEXA8_PLAT_LPC) &
	    ~(MXC_CORTEXA8_PLAT_LPC_DSM);
	ccm_clpcr = imx_readl(ccm_base + MXC_CCM_CLPCR) &
		    ~(MXC_CCM_CLPCR_LPM_MASK);
	arm_srpgcr = imx_readl(gpc_base + MXC_SRPG_ARM_SRPGCR) &
		     ~(MXC_SRPGCR_PCR);
	empgc0 = imx_readl(gpc_base + MXC_SRPG_EMPGC0_SRPGCR) &
		 ~(MXC_SRPGCR_PCR);
	empgc1 = imx_readl(gpc_base + MXC_SRPG_EMPGC1_SRPGCR) &
		 ~(MXC_SRPGCR_PCR);

	switch (mode) {
	case WAIT_CLOCKED:
		break;
	case WAIT_UNCLOCKED:
		ccm_clpcr |= 0x1 << MXC_CCM_CLPCR_LPM_OFFSET;
		break;
	case WAIT_UNCLOCKED_POWER_OFF:
	case STOP_POWER_OFF:
		plat_lpc |= MXC_CORTEXA8_PLAT_LPC_DSM
			    | MXC_CORTEXA8_PLAT_LPC_DBG_DSM;
		if (mode == WAIT_UNCLOCKED_POWER_OFF) {
			ccm_clpcr |= 0x1 << MXC_CCM_CLPCR_LPM_OFFSET;
			ccm_clpcr &= ~MXC_CCM_CLPCR_VSTBY;
			ccm_clpcr &= ~MXC_CCM_CLPCR_SBYOS;
			stop_mode = 0;
		} else {
			ccm_clpcr |= 0x2 << MXC_CCM_CLPCR_LPM_OFFSET;
			ccm_clpcr |= 0x3 << MXC_CCM_CLPCR_STBY_COUNT_OFFSET;
			ccm_clpcr |= MXC_CCM_CLPCR_VSTBY;
			ccm_clpcr |= MXC_CCM_CLPCR_SBYOS;
			stop_mode = 1;
		}
		arm_srpgcr |= MXC_SRPGCR_PCR;
		break;
	case STOP_POWER_ON:
		ccm_clpcr |= 0x2 << MXC_CCM_CLPCR_LPM_OFFSET;
		break;
	default:
		printk(KERN_WARNING "UNKNOWN cpu power mode: %d\n", mode);
		return;
	}

	imx_writel(plat_lpc, cortex_base + MXC_CORTEXA8_PLAT_LPC);
	imx_writel(ccm_clpcr, ccm_base + MXC_CCM_CLPCR);
	imx_writel(arm_srpgcr, gpc_base + MXC_SRPG_ARM_SRPGCR);
	imx_writel(arm_srpgcr, gpc_base + MXC_SRPG_NEON_SRPGCR);

	if (stop_mode) {
		empgc0 |= MXC_SRPGCR_PCR;
		empgc1 |= MXC_SRPGCR_PCR;

		imx_writel(empgc0, gpc_base + MXC_SRPG_EMPGC0_SRPGCR);
		imx_writel(empgc1, gpc_base + MXC_SRPG_EMPGC1_SRPGCR);
	}
}

static int mx5_suspend_enter(suspend_state_t state)
{
	switch (state) {
	case PM_SUSPEND_MEM:
		mx5_cpu_lp_set(STOP_POWER_OFF);
		break;
	case PM_SUSPEND_STANDBY:
		/* DEFAULT_IDLE_STATE already configured */
		break;
	default:
		return -EINVAL;
	}

	if (state == PM_SUSPEND_MEM) {
		local_flush_tlb_all();
		flush_cache_all();

		/*clear the EMPGC0/1 bits */
		imx_writel(0, gpc_base + MXC_SRPG_EMPGC0_SRPGCR);
		imx_writel(0, gpc_base + MXC_SRPG_EMPGC1_SRPGCR);

		if (imx5_suspend_in_ocram_fn)
			imx5_suspend_in_ocram_fn(suspend_ocram_base);
		else
			cpu_do_idle();

	} else {
		cpu_do_idle();
	}

	/* return registers to default idle state */
	mx5_cpu_lp_set(IMX5_DEFAULT_CPU_IDLE_STATE);
	return 0;
}

static int mx5_pm_valid(suspend_state_t state)
{
	return (state > PM_SUSPEND_ON && state <= PM_SUSPEND_MAX);
}

static const struct platform_suspend_ops mx5_suspend_ops = {
	.valid = mx5_pm_valid,
	.enter = mx5_suspend_enter,
};

static inline int imx5_cpu_do_idle(void)
{
	int ret = tzic_enable_wake();

	if (likely(!ret))
		cpu_do_idle();

	return ret;
}

static void imx5_pm_idle(void)
{
	imx5_cpu_do_idle();
}

static int __init imx_suspend_alloc_ocram(
				size_t size,
				void __iomem **virt_out,
				phys_addr_t *phys_out)
{
	struct device_node *node;
	struct platform_device *pdev;
	struct gen_pool *ocram_pool;
	unsigned long ocram_base;
	void __iomem *virt;
	phys_addr_t phys;
	int ret = 0;

	/* Copied from imx6: TODO factorize */
	node = of_find_compatible_node(NULL, NULL, "mmio-sram");
	if (!node) {
		pr_warn("%s: failed to find ocram node!\n", __func__);
		return -ENODEV;
	}

	pdev = of_find_device_by_node(node);
	if (!pdev) {
		pr_warn("%s: failed to find ocram device!\n", __func__);
		ret = -ENODEV;
		goto put_node;
	}

	ocram_pool = gen_pool_get(&pdev->dev, NULL);
	if (!ocram_pool) {
		pr_warn("%s: ocram pool unavailable!\n", __func__);
		ret = -ENODEV;
		goto put_device;
	}

	ocram_base = gen_pool_alloc(ocram_pool, size);
	if (!ocram_base) {
		pr_warn("%s: unable to alloc ocram!\n", __func__);
		ret = -ENOMEM;
		goto put_device;
	}

	phys = gen_pool_virt_to_phys(ocram_pool, ocram_base);
	virt = __arm_ioremap_exec(phys, size, false);
	if (phys_out)
		*phys_out = phys;
	if (virt_out)
		*virt_out = virt;

put_device:
	put_device(&pdev->dev);
put_node:
	of_node_put(node);

	return ret;
}

static int __init imx5_suspend_init(const struct imx5_pm_data *soc_data)
{
	struct imx5_cpu_suspend_info *suspend_info;
	int ret;
	/* Need this to avoid compile error due to const typeof in fncpy.h */
	void (*suspend_asm)(void __iomem *) = soc_data->suspend_asm;

	if (!suspend_asm)
		return 0;

	if (!soc_data->suspend_asm_sz || !*soc_data->suspend_asm_sz)
		return -EINVAL;

	ret = imx_suspend_alloc_ocram(
		*soc_data->suspend_asm_sz + sizeof(*suspend_info),
		&suspend_ocram_base, NULL);
	if (ret)
		return ret;

	suspend_info = suspend_ocram_base;

	suspend_info->io_count = soc_data->suspend_io_count;
	memcpy(suspend_info->io_state, soc_data->suspend_io_config,
	       sizeof(*suspend_info->io_state) * soc_data->suspend_io_count);

	suspend_info->m4if_base = ioremap(soc_data->m4if_addr, SZ_16K);
	if (!suspend_info->m4if_base) {
		ret = -ENOMEM;
		goto failed_map_m4if;
	}

	suspend_info->iomuxc_base = ioremap(soc_data->iomuxc_addr, SZ_16K);
	if (!suspend_info->iomuxc_base) {
		ret = -ENOMEM;
		goto failed_map_iomuxc;
	}

	imx5_suspend_in_ocram_fn = fncpy(
		suspend_ocram_base + sizeof(*suspend_info),
		suspend_asm,
		*soc_data->suspend_asm_sz);

	return 0;

failed_map_iomuxc:
	iounmap(suspend_info->m4if_base);

failed_map_m4if:
	return ret;
}

static int __init imx5_pm_common_init(const struct imx5_pm_data *data)
{
	int ret;
	struct clk *gpc_dvfs_clk = clk_get(NULL, "gpc_dvfs");

	if (IS_ERR(gpc_dvfs_clk))
		return PTR_ERR(gpc_dvfs_clk);

	ret = clk_prepare_enable(gpc_dvfs_clk);
	if (ret)
		return ret;

	arm_pm_idle = imx5_pm_idle;

	ccm_base = ioremap(data->ccm_addr, SZ_16K);
	cortex_base = ioremap(data->cortex_addr, SZ_16K);
	gpc_base = ioremap(data->gpc_addr, SZ_16K);
	WARN_ON(!ccm_base || !cortex_base || !gpc_base);

	/* Set the registers to the default cpu idle state. */
	mx5_cpu_lp_set(IMX5_DEFAULT_CPU_IDLE_STATE);

	ret = imx5_cpuidle_init();
	if (ret)
		pr_warn("%s: cpuidle init failed %d\n", __func__, ret);

	ret = imx5_suspend_init(data);
	if (ret)
		pr_warn("%s: No DDR LPM support with suspend %d!\n",
			__func__, ret);

	suspend_set_ops(&mx5_suspend_ops);

	return 0;
}

void __init imx51_pm_init(void)
{
	if (IS_ENABLED(CONFIG_SOC_IMX51))
		imx5_pm_common_init(&imx51_pm_data);
}

void __init imx53_pm_init(void)
{
	if (IS_ENABLED(CONFIG_SOC_IMX53))
		imx5_pm_common_init(&imx53_pm_data);
}
