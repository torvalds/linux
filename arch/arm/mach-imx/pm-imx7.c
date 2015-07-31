/*
 * Copyright (C) 2015 Freescale Semiconductor, Inc.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/delay.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_fdt.h>
#include <linux/of_irq.h>
#include <linux/slab.h>
#include <linux/suspend.h>
#include <linux/genalloc.h>
#include <linux/mfd/syscon.h>
#include <linux/mfd/syscon/imx7-iomuxc-gpr.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_fdt.h>
#include <linux/of_platform.h>
#include <linux/regmap.h>
#include <linux/suspend.h>
#include <asm/cacheflush.h>
#include <asm/fncpy.h>
#include <asm/mach/map.h>
#include <asm/proc-fns.h>
#include <asm/suspend.h>
#include <asm/tlb.h>

#include "common.h"
#include "hardware.h"

#define MX7_SUSPEND_OCRAM_SIZE		0x1000
#define MX7_MAX_DDRC_NUM		32
#define MX7_MAX_DDRC_PHY_NUM		16

#define MX7_SUSPEND_IRAM_ADDR_OFFSET	0
#define READ_DATA_FROM_HARDWARE		0

#define UART_UCR1	0x80
#define UART_UCR2	0x84
#define UART_UCR3	0x88
#define UART_UCR4	0x8c
#define UART_UFCR	0x90
#define UART_UESC	0x9c
#define UART_UTIM	0xa0
#define UART_UBIR	0xa4
#define UART_UBMR	0xa8
#define UART_UBRC	0xac
#define UART_UTS	0xb4

extern unsigned long iram_tlb_base_addr;
extern unsigned long iram_tlb_phys_addr;

static unsigned int *ocram_saved_in_ddr;
static void __iomem *ocram_base;
static unsigned int ocram_size;
static unsigned int *lpm_ocram_saved_in_ddr;
static void __iomem *lpm_ocram_base;
static unsigned int lpm_ocram_size;
static void __iomem *ccm_base;
static void __iomem *lpsr_base;
static void __iomem *console_base;
static void __iomem *suspend_ocram_base;
static void (*imx7_suspend_in_ocram_fn)(void __iomem *ocram_vbase);
struct imx7_cpu_pm_info *pm_info;
static bool lpsr_enabled;
/*
 * suspend ocram space layout:
 * ======================== high address ======================
 *                              .
 *                              .
 *                              .
 *                              ^
 *                              ^
 *                              ^
 *                      imx7_suspend code
 *              PM_INFO structure(imx7_cpu_pm_info)
 * ======================== low address =======================
 */

struct imx7_pm_base {
	phys_addr_t pbase;
	void __iomem *vbase;
};

struct imx7_pm_socdata {
	u32 ddr_type;
	const char *ddrc_compat;
	const char *src_compat;
	const char *iomuxc_compat;
	const char *gpc_compat;
	const u32 ddrc_num;
	const u32 (*ddrc_offset)[2];
	const u32 ddrc_phy_num;
	const u32 (*ddrc_phy_offset)[2];
};

static const u32 imx7d_ddrc_lpddr3_setting[][2] __initconst = {
	{ 0x0, READ_DATA_FROM_HARDWARE },
	{ 0x1a0, READ_DATA_FROM_HARDWARE },
	{ 0x1a4, READ_DATA_FROM_HARDWARE },
	{ 0x1a8, READ_DATA_FROM_HARDWARE },
	{ 0x64, READ_DATA_FROM_HARDWARE },
	{ 0xd0, 0xc0350001 },
	{ 0xdc, READ_DATA_FROM_HARDWARE },
	{ 0xe0, READ_DATA_FROM_HARDWARE },
	{ 0xe4, READ_DATA_FROM_HARDWARE },
	{ 0xf4, READ_DATA_FROM_HARDWARE },
	{ 0x100, READ_DATA_FROM_HARDWARE },
	{ 0x104, READ_DATA_FROM_HARDWARE },
	{ 0x108, READ_DATA_FROM_HARDWARE },
	{ 0x10c, READ_DATA_FROM_HARDWARE },
	{ 0x110, READ_DATA_FROM_HARDWARE },
	{ 0x114, READ_DATA_FROM_HARDWARE },
	{ 0x118, READ_DATA_FROM_HARDWARE },
	{ 0x11c, READ_DATA_FROM_HARDWARE },
	{ 0x180, READ_DATA_FROM_HARDWARE },
	{ 0x184, READ_DATA_FROM_HARDWARE },
	{ 0x190, READ_DATA_FROM_HARDWARE },
	{ 0x194, READ_DATA_FROM_HARDWARE },
	{ 0x200, READ_DATA_FROM_HARDWARE },
	{ 0x204, READ_DATA_FROM_HARDWARE },
	{ 0x214, READ_DATA_FROM_HARDWARE },
	{ 0x218, READ_DATA_FROM_HARDWARE },
	{ 0x240, 0x06000601 },
	{ 0x244, READ_DATA_FROM_HARDWARE },
};

static const u32 imx7d_ddrc_phy_lpddr3_setting[][2] __initconst = {
	{ 0x0, READ_DATA_FROM_HARDWARE },
	{ 0x4, READ_DATA_FROM_HARDWARE },
	{ 0x8, READ_DATA_FROM_HARDWARE },
	{ 0x10, READ_DATA_FROM_HARDWARE },
	{ 0x1c, READ_DATA_FROM_HARDWARE },
	{ 0x9c, READ_DATA_FROM_HARDWARE },
	{ 0x20, READ_DATA_FROM_HARDWARE },
	{ 0x30, READ_DATA_FROM_HARDWARE },
	{ 0x50, 0x01000008 },
	{ 0x50, 0x00000008 },
	{ 0xc0, 0x0e407304 },
	{ 0xc0, 0x0e447304 },
	{ 0xc0, 0x0e447306 },
	{ 0xc0, 0x0e4c7304 },
	{ 0xc0, 0x0e487306 },
};

static const u32 imx7d_ddrc_ddr3_setting[][2] __initconst = {
	{ 0x0, READ_DATA_FROM_HARDWARE },
	{ 0x1a0, READ_DATA_FROM_HARDWARE },
	{ 0x1a4, READ_DATA_FROM_HARDWARE },
	{ 0x1a8, READ_DATA_FROM_HARDWARE },
	{ 0x64, READ_DATA_FROM_HARDWARE },
	{ 0x490, 0x00000001 },
	{ 0xd0, 0xc0020001 },
	{ 0xd4, READ_DATA_FROM_HARDWARE },
	{ 0xdc, READ_DATA_FROM_HARDWARE },
	{ 0xe0, READ_DATA_FROM_HARDWARE },
	{ 0xe4, READ_DATA_FROM_HARDWARE },
	{ 0xf4, READ_DATA_FROM_HARDWARE },
	{ 0x100, READ_DATA_FROM_HARDWARE },
	{ 0x104, READ_DATA_FROM_HARDWARE },
	{ 0x108, READ_DATA_FROM_HARDWARE },
	{ 0x10c, READ_DATA_FROM_HARDWARE },
	{ 0x110, READ_DATA_FROM_HARDWARE },
	{ 0x114, READ_DATA_FROM_HARDWARE },
	{ 0x120, 0x03030803 },
	{ 0x180, READ_DATA_FROM_HARDWARE },
	{ 0x190, READ_DATA_FROM_HARDWARE },
	{ 0x194, READ_DATA_FROM_HARDWARE },
	{ 0x200, READ_DATA_FROM_HARDWARE },
	{ 0x204, READ_DATA_FROM_HARDWARE },
	{ 0x214, READ_DATA_FROM_HARDWARE },
	{ 0x218, READ_DATA_FROM_HARDWARE },
	{ 0x240, 0x06000601 },
	{ 0x244, READ_DATA_FROM_HARDWARE },
};

static const u32 imx7d_ddrc_phy_ddr3_setting[][2] __initconst = {
	{ 0x0, READ_DATA_FROM_HARDWARE },
	{ 0x4, READ_DATA_FROM_HARDWARE },
	{ 0x10, READ_DATA_FROM_HARDWARE },
	{ 0x9c, READ_DATA_FROM_HARDWARE },
	{ 0x20, READ_DATA_FROM_HARDWARE },
	{ 0x30, READ_DATA_FROM_HARDWARE },
	{ 0x50, 0x01000010 },
	{ 0x50, 0x00000010 },
	{ 0xc0, 0x0e407304 },
	{ 0xc0, 0x0e447304 },
	{ 0xc0, 0x0e447306 },
	{ 0xc0, 0x0e447304 },
	{ 0xc0, 0x0e407306 },
};

static const struct imx7_pm_socdata imx7d_pm_data_lpddr3 __initconst = {
	.ddrc_compat = "fsl,imx7d-ddrc",
	.src_compat = "fsl,imx7d-src",
	.iomuxc_compat = "fsl,imx7d-iomuxc",
	.gpc_compat = "fsl,imx7d-gpc",
	.ddrc_num = ARRAY_SIZE(imx7d_ddrc_lpddr3_setting),
	.ddrc_offset = imx7d_ddrc_lpddr3_setting,
	.ddrc_phy_num = ARRAY_SIZE(imx7d_ddrc_phy_lpddr3_setting),
	.ddrc_phy_offset = imx7d_ddrc_phy_lpddr3_setting,
};

static const struct imx7_pm_socdata imx7d_pm_data_ddr3 __initconst = {
	.ddrc_compat = "fsl,imx7d-ddrc",
	.src_compat = "fsl,imx7d-src",
	.iomuxc_compat = "fsl,imx7d-iomuxc",
	.gpc_compat = "fsl,imx7d-gpc",
	.ddrc_num = ARRAY_SIZE(imx7d_ddrc_ddr3_setting),
	.ddrc_offset = imx7d_ddrc_ddr3_setting,
	.ddrc_phy_num = ARRAY_SIZE(imx7d_ddrc_phy_ddr3_setting),
	.ddrc_phy_offset = imx7d_ddrc_phy_ddr3_setting,
};

/*
 * This structure is for passing necessary data for low level ocram
 * suspend code(arch/arm/mach-imx/suspend-imx7.S), if this struct
 * definition is changed, the offset definition in
 * arch/arm/mach-imx/suspend-imx7.S must be also changed accordingly,
 * otherwise, the suspend to ocram function will be broken!
 */
struct imx7_cpu_pm_info {
	u32 m4_reserve0;
	u32 m4_reserve1;
	u32 m4_reserve2;
	phys_addr_t pbase; /* The physical address of pm_info. */
	phys_addr_t resume_addr; /* The physical resume address for asm code */
	u32 ddr_type;
	u32 pm_info_size; /* Size of pm_info. */
	struct imx7_pm_base ddrc_base;
	struct imx7_pm_base ddrc_phy_base;
	struct imx7_pm_base src_base;
	struct imx7_pm_base iomuxc_gpr_base;
	struct imx7_pm_base ccm_base;
	struct imx7_pm_base gpc_base;
	struct imx7_pm_base snvs_base;
	struct imx7_pm_base anatop_base;
	struct imx7_pm_base lpsr_base;
	u32 ttbr1; /* Store TTBR1 */
	u32 ddrc_num; /* Number of DDRC which need saved/restored. */
	u32 ddrc_val[MX7_MAX_DDRC_NUM][2]; /* To save offset and value */
	u32 ddrc_phy_num; /* Number of DDRC which need saved/restored. */
	u32 ddrc_phy_val[MX7_MAX_DDRC_NUM][2]; /* To save offset and value */
} __aligned(8);

static struct map_desc imx7_pm_io_desc[] __initdata = {
	imx_map_entry(MX7D, AIPS1, MT_DEVICE),
	imx_map_entry(MX7D, AIPS2, MT_DEVICE),
	imx_map_entry(MX7D, AIPS3, MT_DEVICE),
};

static const char * const low_power_ocram_match[] __initconst = {
	"fsl,lpm-sram",
	NULL
};

static void imx7_console_save(unsigned int *regs)
{
	if (!console_base)
		return;

	regs[0] = readl_relaxed(console_base + UART_UCR1);
	regs[1] = readl_relaxed(console_base + UART_UCR2);
	regs[2] = readl_relaxed(console_base + UART_UCR3);
	regs[3] = readl_relaxed(console_base + UART_UCR4);
	regs[4] = readl_relaxed(console_base + UART_UFCR);
	regs[5] = readl_relaxed(console_base + UART_UESC);
	regs[6] = readl_relaxed(console_base + UART_UTIM);
	regs[7] = readl_relaxed(console_base + UART_UBIR);
	regs[8] = readl_relaxed(console_base + UART_UBMR);
	regs[9] = readl_relaxed(console_base + UART_UTS);
}

static void imx7_console_restore(unsigned int *regs)
{
	if (!console_base)
		return;

	writel_relaxed(regs[4], console_base + UART_UFCR);
	writel_relaxed(regs[5], console_base + UART_UESC);
	writel_relaxed(regs[6], console_base + UART_UTIM);
	writel_relaxed(regs[7], console_base + UART_UBIR);
	writel_relaxed(regs[8], console_base + UART_UBMR);
	writel_relaxed(regs[9], console_base + UART_UTS);
	writel_relaxed(regs[0], console_base + UART_UCR1);
	writel_relaxed(regs[1] | 0x1, console_base + UART_UCR2);
	writel_relaxed(regs[2], console_base + UART_UCR3);
	writel_relaxed(regs[3], console_base + UART_UCR4);
}

static int imx7_suspend_finish(unsigned long val)
{
	if (!imx7_suspend_in_ocram_fn) {
		cpu_do_idle();
	} else {
		/*
		 * call low level suspend function in ocram,
		 * as we need to float DDR IO.
		 */
		local_flush_tlb_all();
		imx7_suspend_in_ocram_fn(suspend_ocram_base);
	}

	return 0;
}

static void imx7_pm_set_lpsr_resume_addr(unsigned long addr)
{
	writel_relaxed(addr, pm_info->lpsr_base.vbase);
}

static int imx7_pm_is_resume_from_lpsr(void)
{
	return readl_relaxed(lpsr_base);
}

static int imx7_pm_enter(suspend_state_t state)
{
	unsigned int console_saved_reg[10] = {0};

	if (!iram_tlb_base_addr) {
		pr_warn("No IRAM/OCRAM memory allocated for suspend/resume \
			 code. Please ensure device tree has an entry for \
			 fsl,lpm-sram.\n");
		return -EINVAL;
	}

	switch (state) {
	case PM_SUSPEND_STANDBY:
		imx_anatop_pre_suspend();
		imx_gpcv2_pre_suspend(false);

		/* Zzz ... */
		imx7_suspend_in_ocram_fn(suspend_ocram_base);

		imx_anatop_post_resume();
		imx_gpcv2_post_resume();
		break;
	case PM_SUSPEND_MEM:
		imx_anatop_pre_suspend();
		imx_gpcv2_pre_suspend(true);
		if (imx_gpcv2_is_mf_mix_off()) {
			imx7_console_save(console_saved_reg);
			memcpy(ocram_saved_in_ddr, ocram_base, ocram_size);
			if (lpsr_enabled) {
				imx7_pm_set_lpsr_resume_addr(pm_info->resume_addr);
				memcpy(lpm_ocram_saved_in_ddr, lpm_ocram_base,
					lpm_ocram_size);
			}
		}

		/* Zzz ... */
		cpu_suspend(0, imx7_suspend_finish);

		if (imx7_pm_is_resume_from_lpsr())
			memcpy(lpm_ocram_base, lpm_ocram_saved_in_ddr,
				lpm_ocram_size);
		if (imx_gpcv2_is_mf_mix_off() ||
			imx7_pm_is_resume_from_lpsr()) {
			memcpy(ocram_base, ocram_saved_in_ddr, ocram_size);
			imx7_console_restore(console_saved_reg);
		}
		/* clear LPSR resume address */
		imx7_pm_set_lpsr_resume_addr(0);
		imx_anatop_post_resume();
		imx_gpcv2_post_resume();
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int imx7_pm_valid(suspend_state_t state)
{
	return state == PM_SUSPEND_STANDBY || state == PM_SUSPEND_MEM;
}

static const struct platform_suspend_ops imx7_pm_ops = {
	.enter = imx7_pm_enter,
	.valid = imx7_pm_valid,
};

void __init imx7_pm_set_ccm_base(void __iomem *base)
{
	ccm_base = base;
}

static struct map_desc iram_tlb_io_desc __initdata = {
	/* .virtual and .pfn are run-time assigned */
	.length     = SZ_1M,
	.type       = MT_MEMORY_RWX_NONCACHED,
};

static int __init imx7_dt_find_lpsram(unsigned long node, const char *uname,
				      int depth, void *data)
{
	unsigned long lpram_addr;
	const __be32 *prop = of_get_flat_dt_prop(node, "reg", NULL);

	if (of_flat_dt_match(node, low_power_ocram_match)) {
		if (!prop)
			return -EINVAL;

		lpram_addr = be32_to_cpup(prop);

		/* We need to create a 1M page table entry. */
		iram_tlb_io_desc.virtual = IMX_IO_P2V(lpram_addr & 0xFFF00000);
		iram_tlb_io_desc.pfn = __phys_to_pfn(lpram_addr & 0xFFF00000);
		iram_tlb_phys_addr = lpram_addr;
		iram_tlb_base_addr = IMX_IO_P2V(lpram_addr);
		iotable_init(&iram_tlb_io_desc, 1);
	}

	return 0;
}

void __init imx7_pm_map_io(void)
{
	unsigned long i, j;

	iotable_init(imx7_pm_io_desc, ARRAY_SIZE(imx7_pm_io_desc));
	/*
	 * Get the address of IRAM or OCRAM to be used by the low
	 * power code from the device tree.
	 */
	WARN_ON(of_scan_flat_dt(imx7_dt_find_lpsram, NULL));

	/* Return if no IRAM space is allocated for suspend/resume code. */
	if (!iram_tlb_base_addr) {
		pr_warn("No valid ocram available for suspend/resume!\n");
		return;
	}

	/* Set all entries to 0. */
	memset((void *)iram_tlb_base_addr, 0, MX7_IRAM_TLB_SIZE);

	/*
	 * Make sure the IRAM virtual address has a mapping in the IRAM
	 * page table.
	 *
	 * Only use the top 12 bits [31-20] when storing the physical
	 * address in the page table as only these bits are required
	 * for 1M mapping.
	 */
	j = ((iram_tlb_base_addr >> 20) << 2) / 4;
	*((unsigned long *)iram_tlb_base_addr + j) =
		(iram_tlb_phys_addr & 0xFFF00000) | TT_ATTRIB_NON_CACHEABLE_1M;

	/*
	 * Make sure the AIPS1 virtual address has a mapping in the
	 * IRAM page table.
	 */
	for (i = 0; i < 4; i++) {
		j = ((IMX_IO_P2V(MX7D_AIPS1_BASE_ADDR + i * 0x100000) >> 20) << 2) / 4;
		*((unsigned long *)iram_tlb_base_addr + j) =
			((MX7D_AIPS1_BASE_ADDR + i * 0x100000) & 0xFFF00000) |
			TT_ATTRIB_NON_CACHEABLE_1M;
	}

	/*
	 * Make sure the AIPS2 virtual address has a mapping in the
	 * IRAM page table.
	 */
	for (i = 0; i < 4; i++) {
		j = ((IMX_IO_P2V(MX7D_AIPS2_BASE_ADDR + i * 0x100000) >> 20) << 2) / 4;
		*((unsigned long *)iram_tlb_base_addr + j) =
			((MX7D_AIPS2_BASE_ADDR + i * 0x100000) & 0xFFF00000) |
			TT_ATTRIB_NON_CACHEABLE_1M;
	}

	/*
	 * Make sure the AIPS3 virtual address has a mapping
	 * in the IRAM page table.
	 */
	for (i = 0; i < 4; i++) {
		j = ((IMX_IO_P2V(MX7D_AIPS3_BASE_ADDR + i * 0x100000) >> 20) << 2) / 4;
		*((unsigned long *)iram_tlb_base_addr + j) =
			((MX7D_AIPS3_BASE_ADDR + i * 0x100000) & 0xFFF00000) |
			TT_ATTRIB_NON_CACHEABLE_1M;
	}
}

static int __init imx7_suspend_init(const struct imx7_pm_socdata *socdata)
{
	struct device_node *node;
	int i, ret = 0;
	const u32 (*ddrc_offset_array)[2];
	const u32 (*ddrc_phy_offset_array)[2];
	unsigned long iram_paddr;

	suspend_set_ops(&imx7_pm_ops);

	if (!socdata) {
		pr_warn("%s: invalid argument!\n", __func__);
		return -EINVAL;
	}

	/*
	 * 16KB is allocated for IRAM TLB, but only up 8k is for kernel TLB,
	 * The lower 8K is not used, so use the lower 8K for IRAM code and
	 * pm_info.
	 *
	 */
	iram_paddr = iram_tlb_phys_addr + MX7_SUSPEND_IRAM_ADDR_OFFSET;

	/* Make sure iram_paddr is 8 byte aligned. */
	if ((uintptr_t)(iram_paddr) & (FNCPY_ALIGN - 1))
		iram_paddr += FNCPY_ALIGN - iram_paddr % (FNCPY_ALIGN);

	/* Get the virtual address of the suspend code. */
	suspend_ocram_base = (void *)IMX_IO_P2V(iram_paddr);

	pm_info = suspend_ocram_base;
	/* pbase points to iram_paddr. */
	pm_info->pbase = iram_paddr;
	pm_info->resume_addr = virt_to_phys(ca7_cpu_resume);
	pm_info->pm_info_size = sizeof(*pm_info);

	/*
	 * ccm physical address is not used by asm code currently,
	 * so get ccm virtual address directly, as we already have
	 * it from ccm driver.
	 */
	pm_info->ccm_base.pbase = MX7D_CCM_BASE_ADDR;
	pm_info->ccm_base.vbase = (void __iomem *)
				IMX_IO_P2V(MX7D_CCM_BASE_ADDR);

	pm_info->ddrc_base.pbase = MX7D_DDRC_BASE_ADDR;
	pm_info->ddrc_base.vbase = (void __iomem *)
				IMX_IO_P2V(MX7D_DDRC_BASE_ADDR);

	pm_info->ddrc_phy_base.pbase = MX7D_DDRC_PHY_BASE_ADDR;
	pm_info->ddrc_phy_base.vbase = (void __iomem *)
				IMX_IO_P2V(MX7D_DDRC_PHY_BASE_ADDR);

	pm_info->src_base.pbase = MX7D_SRC_BASE_ADDR;
	pm_info->src_base.vbase = (void __iomem *)
				IMX_IO_P2V(MX7D_SRC_BASE_ADDR);

	pm_info->iomuxc_gpr_base.pbase = MX7D_IOMUXC_GPR_BASE_ADDR;
	pm_info->iomuxc_gpr_base.vbase = (void __iomem *)
				IMX_IO_P2V(MX7D_IOMUXC_GPR_BASE_ADDR);

	pm_info->gpc_base.pbase = MX7D_GPC_BASE_ADDR;
	pm_info->gpc_base.vbase = (void __iomem *)
				IMX_IO_P2V(MX7D_GPC_BASE_ADDR);

	pm_info->anatop_base.pbase = MX7D_ANATOP_BASE_ADDR;
	pm_info->anatop_base.vbase = (void __iomem *)
				IMX_IO_P2V(MX7D_ANATOP_BASE_ADDR);

	pm_info->snvs_base.pbase = MX7D_SNVS_BASE_ADDR;
	pm_info->snvs_base.vbase = (void __iomem *)
				IMX_IO_P2V(MX7D_SNVS_BASE_ADDR);

	pm_info->lpsr_base.pbase = MX7D_LPSR_BASE_ADDR;
	lpsr_base = pm_info->lpsr_base.vbase = (void __iomem *)
				IMX_IO_P2V(MX7D_LPSR_BASE_ADDR);

	pm_info->ddrc_num = socdata->ddrc_num;
	ddrc_offset_array = socdata->ddrc_offset;
	pm_info->ddrc_phy_num = socdata->ddrc_phy_num;
	ddrc_phy_offset_array = socdata->ddrc_phy_offset;

	/* initialize DDRC settings */
	for (i = 0; i < pm_info->ddrc_num; i++) {
		pm_info->ddrc_val[i][0] = ddrc_offset_array[i][0];
		if (ddrc_offset_array[i][1] == READ_DATA_FROM_HARDWARE)
			pm_info->ddrc_val[i][1] =
				readl_relaxed(pm_info->ddrc_base.vbase +
				ddrc_offset_array[i][0]);
		else
			pm_info->ddrc_val[i][1] = ddrc_offset_array[i][1];
	}

	/* initialize DDRC PHY settings */
	for (i = 0; i < pm_info->ddrc_phy_num; i++) {
		pm_info->ddrc_phy_val[i][0] =
			ddrc_phy_offset_array[i][0];
		if (ddrc_phy_offset_array[i][1] == READ_DATA_FROM_HARDWARE)
			pm_info->ddrc_phy_val[i][1] =
				readl_relaxed(pm_info->ddrc_phy_base.vbase +
				ddrc_phy_offset_array[i][0]);
		else
			pm_info->ddrc_phy_val[i][1] =
				ddrc_phy_offset_array[i][1];
	}

	imx7_suspend_in_ocram_fn = fncpy(
		suspend_ocram_base + sizeof(*pm_info),
		&imx7_suspend,
		MX7_SUSPEND_OCRAM_SIZE - sizeof(*pm_info));

	goto put_node;

put_node:
	of_node_put(node);

	return ret;
}

static void __init imx7_pm_common_init(const struct imx7_pm_socdata
					*socdata)
{
	int ret;
	struct regmap *gpr;

	if (IS_ENABLED(CONFIG_SUSPEND)) {
		ret = imx7_suspend_init(socdata);
		if (ret)
			pr_warn("%s: No DDR LPM support with suspend %d!\n",
				__func__, ret);
	}

	/*
	 * Force IOMUXC irq pending, so that the interrupt to GPC can be
	 * used to deassert dsm_request signal when the signal gets
	 * asserted unexpectedly.
	 */
	gpr = syscon_regmap_lookup_by_compatible("fsl,imx7d-iomuxc-gpr");
	if (!IS_ERR(gpr))
		regmap_update_bits(gpr, IOMUXC_GPR1, IMX7D_GPR1_IRQ_MASK,
			IMX7D_GPR1_IRQ_MASK);
}

void __init imx7d_pm_init(void)
{
	struct device_node *np;
	struct resource res;

	np = of_find_compatible_node(NULL, NULL, "fsl,lpm-sram");
	if (of_get_property(np, "fsl,enable-lpsr", NULL))
		lpsr_enabled = true;

	if (lpsr_enabled) {
		pr_info("LPSR mode enabled, DSM will go into LPSR mode!\n");
		lpm_ocram_base = of_iomap(np, 0);
		WARN_ON(!lpm_ocram_base);
		WARN_ON(of_address_to_resource(np, 0, &res));
		lpm_ocram_size = resource_size(&res);
		lpm_ocram_saved_in_ddr = kzalloc(lpm_ocram_size, GFP_KERNEL);
		WARN_ON(!lpm_ocram_saved_in_ddr);
	}

	if (imx_ddrc_get_ddr_type() == IMX_DDR_TYPE_LPDDR3
		|| imx_ddrc_get_ddr_type() == IMX_DDR_TYPE_LPDDR2)
		imx7_pm_common_init(&imx7d_pm_data_lpddr3);
	else if (imx_ddrc_get_ddr_type() == IMX_DDR_TYPE_DDR3)
		imx7_pm_common_init(&imx7d_pm_data_ddr3);

	np = of_find_compatible_node(NULL, NULL, "fsl,mega-fast-sram");
	ocram_base = of_iomap(np, 0);
	WARN_ON(!ocram_base);
	WARN_ON(of_address_to_resource(np, 0, &res));
	ocram_size = resource_size(&res);
	ocram_saved_in_ddr = kzalloc(ocram_size, GFP_KERNEL);
	WARN_ON(!ocram_saved_in_ddr);

	np = of_find_node_by_path(
		"/soc/aips-bus@30800000/spba-bus@30800000/serial@30860000");
	if (np)
		console_base = of_iomap(np, 0);

	/* clear LPSR resume address first */
	imx7_pm_set_lpsr_resume_addr(0);
}
