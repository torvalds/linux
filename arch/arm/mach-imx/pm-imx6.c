/*
 * Copyright 2011-2015 Freescale Semiconductor, Inc.
 * Copyright 2011 Linaro Ltd.
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
#include <linux/genalloc.h>
#include <linux/mfd/syscon.h>
#include <linux/mfd/syscon/imx6q-iomuxc-gpr.h>
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

#define CCR				0x0
#define BM_CCR_WB_COUNT			(0x7 << 16)
#define BM_CCR_RBC_BYPASS_COUNT		(0x3f << 21)
#define BM_CCR_RBC_EN			(0x1 << 27)

#define CLPCR				0x54
#define BP_CLPCR_LPM			0
#define BM_CLPCR_LPM			(0x3 << 0)
#define BM_CLPCR_BYPASS_PMIC_READY	(0x1 << 2)
#define BM_CLPCR_ARM_CLK_DIS_ON_LPM	(0x1 << 5)
#define BM_CLPCR_SBYOS			(0x1 << 6)
#define BM_CLPCR_DIS_REF_OSC		(0x1 << 7)
#define BM_CLPCR_VSTBY			(0x1 << 8)
#define BP_CLPCR_STBY_COUNT		9
#define BM_CLPCR_STBY_COUNT		(0x3 << 9)
#define BM_CLPCR_COSC_PWRDOWN		(0x1 << 11)
#define BM_CLPCR_WB_PER_AT_LPM		(0x1 << 16)
#define BM_CLPCR_WB_CORE_AT_LPM		(0x1 << 17)
#define BM_CLPCR_BYP_MMDC_CH0_LPM_HS	(0x1 << 19)
#define BM_CLPCR_BYP_MMDC_CH1_LPM_HS	(0x1 << 21)
#define BM_CLPCR_MASK_CORE0_WFI		(0x1 << 22)
#define BM_CLPCR_MASK_CORE1_WFI		(0x1 << 23)
#define BM_CLPCR_MASK_CORE2_WFI		(0x1 << 24)
#define BM_CLPCR_MASK_CORE3_WFI		(0x1 << 25)
#define BM_CLPCR_MASK_SCU_IDLE		(0x1 << 26)
#define BM_CLPCR_MASK_L2CC_IDLE		(0x1 << 27)

#define CGPR				0x64
#define BM_CGPR_INT_MEM_CLK_LPM		(0x1 << 17)

#define MX6Q_SUSPEND_OCRAM_SIZE		0x1000
#define MX6_MAX_MMDC_IO_NUM		33
#define MX6_MAX_MMDC_NUM		34

extern unsigned long iram_tlb_base_addr;
extern unsigned long iram_tlb_phys_addr;

static void __iomem *ccm_base;
static void __iomem *suspend_ocram_base;
static void (*imx6_suspend_in_ocram_fn)(void __iomem *ocram_vbase);

/*
 * suspend ocram space layout:
 * ======================== high address ======================
 *                              .
 *                              .
 *                              .
 *                              ^
 *                              ^
 *                              ^
 *                      imx6_suspend code
 *              PM_INFO structure(imx6_cpu_pm_info)
 * ======================== low address =======================
 */

struct imx6_pm_base {
	phys_addr_t pbase;
	void __iomem *vbase;
};

struct imx6_pm_socdata {
	u32 ddr_type;
	const char *mmdc_compat;
	const char *src_compat;
	const char *iomuxc_compat;
	const char *gpc_compat;
	const u32 mmdc_io_num;
	const u32 *mmdc_io_offset;
	const u32 mmdc_num;
	const u32 *mmdc_offset;
};

static const u32 imx6q_mmdc_io_offset[] __initconst = {
	0x5ac, 0x5b4, 0x528, 0x520, /* DQM0 ~ DQM3 */
	0x514, 0x510, 0x5bc, 0x5c4, /* DQM4 ~ DQM7 */
	0x56c, 0x578, 0x588, 0x594, /* CAS, RAS, SDCLK_0, SDCLK_1 */
	0x5a8, 0x5b0, 0x524, 0x51c, /* SDQS0 ~ SDQS3 */
	0x518, 0x50c, 0x5b8, 0x5c0, /* SDQS4 ~ SDQS7 */
	0x784, 0x788, 0x794, 0x79c, /* GPR_B0DS ~ GPR_B3DS */
	0x7a0, 0x7a4, 0x7a8, 0x748, /* GPR_B4DS ~ GPR_B7DS */
	0x59c, 0x5a0, 0x750, 0x774, /* SODT0, SODT1, MODE_CTL, MODE */
	0x74c,			    /* GPR_ADDS */
};

static const u32 imx6dl_mmdc_io_offset[] __initconst = {
	0x470, 0x474, 0x478, 0x47c, /* DQM0 ~ DQM3 */
	0x480, 0x484, 0x488, 0x48c, /* DQM4 ~ DQM7 */
	0x464, 0x490, 0x4ac, 0x4b0, /* CAS, RAS, SDCLK_0, SDCLK_1 */
	0x4bc, 0x4c0, 0x4c4, 0x4c8, /* DRAM_SDQS0 ~ DRAM_SDQS3 */
	0x4cc, 0x4d0, 0x4d4, 0x4d8, /* DRAM_SDQS4 ~ DRAM_SDQS7 */
	0x764, 0x770, 0x778, 0x77c, /* GPR_B0DS ~ GPR_B3DS */
	0x780, 0x784, 0x78c, 0x748, /* GPR_B4DS ~ GPR_B7DS */
	0x4b4, 0x4b8, 0x750, 0x760, /* SODT0, SODT1, MODE_CTL, MODE */
	0x74c,			    /* GPR_ADDS */
};

static const u32 imx6sl_mmdc_io_offset[] __initconst = {
	0x30c, 0x310, 0x314, 0x318, /* DQM0 ~ DQM3 */
	0x5c4, 0x5cc, 0x5d4, 0x5d8, /* GPR_B0DS ~ GPR_B3DS */
	0x300, 0x31c, 0x338, 0x5ac, /* CAS, RAS, SDCLK_0, GPR_ADDS */
	0x33c, 0x340, 0x5b0, 0x5c0, /* SODT0, SODT1, MODE_CTL, MODE */
	0x330, 0x334, 0x320,        /* SDCKE0, SDCKE1, RESET */
};

static const u32 imx6sx_mmdc_io_offset[] __initconst = {
	0x2ec, 0x2f0, 0x2f4, 0x2f8, /* DQM0 ~ DQM3 */
	0x60c, 0x610, 0x61c, 0x620, /* GPR_B0DS ~ GPR_B3DS */
	0x300, 0x2fc, 0x32c, 0x5f4, /* CAS, RAS, SDCLK_0, GPR_ADDS */
	0x310, 0x314, 0x5f8, 0x608, /* SODT0, SODT1, MODE_CTL, MODE */
	0x330, 0x334, 0x338, 0x33c, /* SDQS0 ~ SDQS3 */
};

static const u32 imx6ul_mmdc_io_offset[] __initconst = {
	0x244, 0x248, 0x24c, 0x250, /* DQM0, DQM1, RAS, CAS */
	0x27c, 0x498, 0x4a4, 0x490, /* SDCLK0, GPR_B0DS-B1DS, GPR_ADDS */
	0x280, 0x284, 0x260, 0x264, /* SDQS0~1, SODT0, SODT1 */
	0x494, 0x4b0,               /* MODE_CTL, MODE, */
};

static const u32 imx6ul_mmdc_offset[] __initconst = {
	0x01c, 0x800, 0x80c, 0x83c,
	0x848, 0x850, 0x81c, 0x820,
	0x82c, 0x830, 0x8c0, 0x8b8,
	0x004, 0x008, 0x00c, 0x010,
	0x014, 0x018, 0x01c, 0x02c,
	0x030, 0x040, 0x000, 0x01c,
	0x020, 0x818, 0x01c,
};

static const struct imx6_pm_socdata imx6q_pm_data __initconst = {
	.mmdc_compat = "fsl,imx6q-mmdc",
	.src_compat = "fsl,imx6q-src",
	.iomuxc_compat = "fsl,imx6q-iomuxc",
	.gpc_compat = "fsl,imx6q-gpc",
	.mmdc_io_num = ARRAY_SIZE(imx6q_mmdc_io_offset),
	.mmdc_io_offset = imx6q_mmdc_io_offset,
	.mmdc_num = 0,
	.mmdc_offset = NULL,
};

static const struct imx6_pm_socdata imx6dl_pm_data __initconst = {
	.mmdc_compat = "fsl,imx6q-mmdc",
	.src_compat = "fsl,imx6q-src",
	.iomuxc_compat = "fsl,imx6dl-iomuxc",
	.gpc_compat = "fsl,imx6q-gpc",
	.mmdc_io_num = ARRAY_SIZE(imx6dl_mmdc_io_offset),
	.mmdc_io_offset = imx6dl_mmdc_io_offset,
	.mmdc_num = 0,
	.mmdc_offset = NULL,
};

static const struct imx6_pm_socdata imx6sl_pm_data __initconst = {
	.mmdc_compat = "fsl,imx6sl-mmdc",
	.src_compat = "fsl,imx6sl-src",
	.iomuxc_compat = "fsl,imx6sl-iomuxc",
	.gpc_compat = "fsl,imx6sl-gpc",
	.mmdc_io_num = ARRAY_SIZE(imx6sl_mmdc_io_offset),
	.mmdc_io_offset = imx6sl_mmdc_io_offset,
	.mmdc_num = 0,
	.mmdc_offset = NULL,
};

static const struct imx6_pm_socdata imx6sx_pm_data __initconst = {
	.mmdc_compat = "fsl,imx6sx-mmdc",
	.src_compat = "fsl,imx6sx-src",
	.iomuxc_compat = "fsl,imx6sx-iomuxc",
	.gpc_compat = "fsl,imx6sx-gpc",
	.mmdc_io_num = ARRAY_SIZE(imx6sx_mmdc_io_offset),
	.mmdc_io_offset = imx6sx_mmdc_io_offset,
	.mmdc_num = 0,
	.mmdc_offset = NULL,
};

static const struct imx6_pm_socdata imx6ul_pm_data __initconst = {
	.mmdc_compat = "fsl,imx6ul-mmdc",
	.src_compat = "fsl,imx6ul-src",
	.iomuxc_compat = "fsl,imx6ul-iomuxc",
	.gpc_compat = "fsl,imx6ul-gpc",
	.mmdc_io_num = ARRAY_SIZE(imx6ul_mmdc_io_offset),
	.mmdc_io_offset = imx6ul_mmdc_io_offset,
	.mmdc_num = ARRAY_SIZE(imx6ul_mmdc_offset),
	.mmdc_offset = imx6ul_mmdc_offset,
};

static struct map_desc iram_tlb_io_desc __initdata = {
	/* .virtual and .pfn are run-time assigned */
	.length     = SZ_1M,
	.type       = MT_MEMORY_RWX_NONCACHED,
};

/*
 * AIPS1 and AIPS2 is not used, because it will trigger a BUG_ON if
 * lowlevel debug and earlyprintk are configured.
 *
 * it is because there is a vm conflict because UART1 is mapped early if
 * AIPS1 is mapped using 1M size.
 *
 * Thus no use AIPS1 and AIPS2 to avoid kernel BUG_ON.
 */
static struct map_desc imx6_pm_io_desc[] __initdata = {
	imx_map_entry(MX6Q, MMDC_P0, MT_DEVICE),
	imx_map_entry(MX6Q, MMDC_P1, MT_DEVICE),
	imx_map_entry(MX6Q, SRC, MT_DEVICE),
	imx_map_entry(MX6Q, IOMUXC, MT_DEVICE),
	imx_map_entry(MX6Q, CCM, MT_DEVICE),
	imx_map_entry(MX6Q, ANATOP, MT_DEVICE),
	imx_map_entry(MX6Q, GPC, MT_DEVICE),
	imx_map_entry(MX6Q, L2,	MT_DEVICE),
};

static const char * const low_power_ocram_match[] __initconst = {
	"fsl,lpm-sram",
	NULL
};

/*
 * This structure is for passing necessary data for low level ocram
 * suspend code(arch/arm/mach-imx/suspend-imx6.S), if this struct
 * definition is changed, the offset definition in
 * arch/arm/mach-imx/suspend-imx6.S must be also changed accordingly,
 * otherwise, the suspend to ocram function will be broken!
 */
struct imx6_cpu_pm_info {
	phys_addr_t pbase; /* The physical address of pm_info. */
	phys_addr_t resume_addr; /* The physical resume address for asm code */
	u32 ddr_type;
	u32 pm_info_size; /* Size of pm_info. */
	struct imx6_pm_base mmdc_base;
	struct imx6_pm_base src_base;
	struct imx6_pm_base iomuxc_base;
	struct imx6_pm_base ccm_base;
	struct imx6_pm_base gpc_base;
	struct imx6_pm_base l2_base;
	struct imx6_pm_base anatop_base;
	u32 ttbr1; /* Store TTBR1 */
	u32 mmdc_io_num; /* Number of MMDC IOs which need saved/restored. */
	u32 mmdc_io_val[MX6_MAX_MMDC_IO_NUM][2]; /* To save offset and value */
	u32 mmdc_num; /* Number of MMDC registers which need saved/restored. */
	u32 mmdc_val[MX6_MAX_MMDC_NUM][2];
} __aligned(8);

unsigned long save_ttbr1(void)
{
	unsigned long lttbr1;

	asm volatile(
		".align 4\n"
		"mrc p15, 0, %0, c2, c0, 1\n"
		: "=r" (lttbr1)
	);
	return lttbr1;
}

void restore_ttbr1(unsigned long ttbr1)
{
	asm volatile(
		".align 4\n"
		"mcr p15, 0, %0, c2, c0, 1\n"
		: : "r" (ttbr1)
	);
}

void imx6q_set_int_mem_clk_lpm(bool enable)
{
	u32 val = readl_relaxed(ccm_base + CGPR);

	val &= ~BM_CGPR_INT_MEM_CLK_LPM;
	if (enable)
		val |= BM_CGPR_INT_MEM_CLK_LPM;
	writel_relaxed(val, ccm_base + CGPR);
}

void imx6_enable_rbc(bool enable)
{
	u32 val;

	/*
	 * need to mask all interrupts in GPC before
	 * operating RBC configurations
	 */
	imx_gpc_mask_all();

	/* configure RBC enable bit */
	val = readl_relaxed(ccm_base + CCR);
	val &= ~BM_CCR_RBC_EN;
	val |= enable ? BM_CCR_RBC_EN : 0;
	writel_relaxed(val, ccm_base + CCR);

	/* configure RBC count */
	val = readl_relaxed(ccm_base + CCR);
	val &= ~BM_CCR_RBC_BYPASS_COUNT;
	val |= enable ? BM_CCR_RBC_BYPASS_COUNT : 0;
	writel(val, ccm_base + CCR);

	/*
	 * need to delay at least 2 cycles of CKIL(32K)
	 * due to hardware design requirement, which is
	 * ~61us, here we use 65us for safe
	 */
	udelay(65);

	/* restore GPC interrupt mask settings */
	imx_gpc_restore_all();
}

static void imx6q_enable_wb(bool enable)
{
	u32 val;

	/* configure well bias enable bit */
	val = readl_relaxed(ccm_base + CLPCR);
	val &= ~BM_CLPCR_WB_PER_AT_LPM;
	val |= enable ? BM_CLPCR_WB_PER_AT_LPM : 0;
	writel_relaxed(val, ccm_base + CLPCR);

	/* configure well bias count */
	val = readl_relaxed(ccm_base + CCR);
	val &= ~BM_CCR_WB_COUNT;
	val |= enable ? BM_CCR_WB_COUNT : 0;
	writel_relaxed(val, ccm_base + CCR);
}

int imx6q_set_lpm(enum mxc_cpu_pwr_mode mode)
{
	u32 val = readl_relaxed(ccm_base + CLPCR);

	val &= ~BM_CLPCR_LPM;
	switch (mode) {
	case WAIT_CLOCKED:
		break;
	case WAIT_UNCLOCKED:
		val |= 0x1 << BP_CLPCR_LPM;
		val |= BM_CLPCR_ARM_CLK_DIS_ON_LPM;
		break;
	case STOP_POWER_ON:
		val |= 0x2 << BP_CLPCR_LPM;
		val &= ~BM_CLPCR_VSTBY;
		val &= ~BM_CLPCR_SBYOS;
		if (cpu_is_imx6sl() || cpu_is_imx6sx())
			val |= BM_CLPCR_BYPASS_PMIC_READY;
		if (cpu_is_imx6sl() || cpu_is_imx6sx() ||
		    cpu_is_imx6ul())
			val |= BM_CLPCR_BYP_MMDC_CH0_LPM_HS;
		else
			val |= BM_CLPCR_BYP_MMDC_CH1_LPM_HS;
		break;
	case WAIT_UNCLOCKED_POWER_OFF:
		val |= 0x1 << BP_CLPCR_LPM;
		val &= ~BM_CLPCR_VSTBY;
		val &= ~BM_CLPCR_SBYOS;
		break;
	case STOP_POWER_OFF:
		val |= 0x2 << BP_CLPCR_LPM;
		val |= 0x3 << BP_CLPCR_STBY_COUNT;
		val |= BM_CLPCR_VSTBY;
		val |= BM_CLPCR_SBYOS;
		if (cpu_is_imx6sl() || cpu_is_imx6sx())
			val |= BM_CLPCR_BYPASS_PMIC_READY;
		if (cpu_is_imx6sl() || cpu_is_imx6sx() ||
		    cpu_is_imx6ul())
			val |= BM_CLPCR_BYP_MMDC_CH0_LPM_HS;
		else
			val |= BM_CLPCR_BYP_MMDC_CH1_LPM_HS;
		break;
	default:
		return -EINVAL;
	}

	/*
	 * ERR007265: CCM: When improper low-power sequence is used,
	 * the SoC enters low power mode before the ARM core executes WFI.
	 *
	 * Software workaround:
	 * 1) Software should trigger IRQ #32 (IOMUX) to be always pending
	 *    by setting IOMUX_GPR1_GINT.
	 * 2) Software should then unmask IRQ #32 in GPC before setting CCM
	 *    Low-Power mode.
	 * 3) Software should mask IRQ #32 right after CCM Low-Power mode
	 *    is set (set bits 0-1 of CCM_CLPCR).
	 *
	 * Note that IRQ #32 is GIC SPI #0.
	 */
	imx_gpc_hwirq_unmask(0);
	writel_relaxed(val, ccm_base + CLPCR);
	imx_gpc_hwirq_mask(0);

	return 0;
}

static int imx6q_suspend_finish(unsigned long val)
{
	if (!imx6_suspend_in_ocram_fn) {
		cpu_do_idle();
	} else {
		/*
		 * call low level suspend function in ocram,
		 * as we need to float DDR IO.
		 */
		local_flush_tlb_all();
		imx6_suspend_in_ocram_fn(suspend_ocram_base);
	}

	return 0;
}

static int imx6q_pm_enter(suspend_state_t state)
{
	switch (state) {
	case PM_SUSPEND_STANDBY:
		imx6q_set_lpm(STOP_POWER_ON);
		imx6q_set_int_mem_clk_lpm(true);
		imx_gpc_pre_suspend(false);
		if (cpu_is_imx6sl())
			imx6sl_set_wait_clk(true);
		/* Zzz ... */
		cpu_do_idle();
		if (cpu_is_imx6sl())
			imx6sl_set_wait_clk(false);
		imx_gpc_post_resume();
		imx6q_set_lpm(WAIT_CLOCKED);
		break;
	case PM_SUSPEND_MEM:
		imx6q_set_lpm(STOP_POWER_OFF);
		imx6q_set_int_mem_clk_lpm(false);
		imx6q_enable_wb(true);
		/*
		 * For suspend into ocram, asm code already take care of
		 * RBC setting, so we do NOT need to do that here.
		 */
		if (!imx6_suspend_in_ocram_fn)
			imx6_enable_rbc(true);
		imx_gpc_pre_suspend(true);
		imx_anatop_pre_suspend();
		/* Zzz ... */
		cpu_suspend(0, imx6q_suspend_finish);
		if (cpu_is_imx6q() || cpu_is_imx6dl())
			imx_smp_prepare();
		imx_anatop_post_resume();
		imx_gpc_post_resume();
		imx6_enable_rbc(false);
		imx6q_enable_wb(false);
		imx6q_set_int_mem_clk_lpm(true);
		imx6q_set_lpm(WAIT_CLOCKED);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int imx6q_pm_valid(suspend_state_t state)
{
	return (state == PM_SUSPEND_STANDBY || state == PM_SUSPEND_MEM);
}

static const struct platform_suspend_ops imx6q_pm_ops = {
	.enter = imx6q_pm_enter,
	.valid = imx6q_pm_valid,
};

void __init imx6q_pm_set_ccm_base(void __iomem *base)
{
	ccm_base = base;
}

static int __init imx6_dt_find_lpsram(unsigned long node, const char *uname,
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

void __init imx6_pm_map_io(void)
{
	unsigned long i;

	iotable_init(imx6_pm_io_desc, ARRAY_SIZE(imx6_pm_io_desc));

	/*
	 * Get the address of IRAM or OCRAM to be used by the low
	 * power code from the device tree.
	 */
	WARN_ON(of_scan_flat_dt(imx6_dt_find_lpsram, NULL));

	/* Return if no IRAM space is allocated for suspend/resume code. */
	if (!iram_tlb_base_addr) {
		pr_warn("No IRAM/OCRAM memory allocated for suspend/resume \
			 code. Please ensure device tree has an entry for \
			 fsl,lpm-sram.\n");
		return;
	}

	/* Set all entries to 0. */
	memset((void *)iram_tlb_base_addr, 0, MX6Q_IRAM_TLB_SIZE);

	/*
	 * Make sure the IRAM virtual address has a mapping in the IRAM
	 * page table.
	 *
	 * Only use the top 11 bits [31-20] when storing the physical
	 * address in the page table as only these bits are required
	 * for 1M mapping.
	 */
	i = ((iram_tlb_base_addr >> 20) << 2) / 4;
	*((unsigned long *)iram_tlb_base_addr + i) =
		(iram_tlb_phys_addr & 0xFFF00000) | TT_ATTRIB_NON_CACHEABLE_1M;

	/*
	 * Make sure the AIPS1 virtual address has a mapping in the
	 * IRAM page table.
	 */
	i = ((IMX_IO_P2V(MX6Q_AIPS1_BASE_ADDR) >> 20) << 2) / 4;
	*((unsigned long *)iram_tlb_base_addr + i) =
		(MX6Q_AIPS1_BASE_ADDR & 0xFFF00000) |
		TT_ATTRIB_NON_CACHEABLE_1M;

	/*
	 * Make sure the AIPS2 virtual address has a mapping in the
	 * IRAM page table.
	 */
	i = ((IMX_IO_P2V(MX6Q_AIPS2_BASE_ADDR) >> 20) << 2) / 4;
	*((unsigned long *)iram_tlb_base_addr + i) =
		(MX6Q_AIPS2_BASE_ADDR & 0xFFF00000) |
		TT_ATTRIB_NON_CACHEABLE_1M;

	/*
	 * Make sure the AIPS3 virtual address has a mapping
	 * in the IRAM page table.
	 */
	i = ((IMX_IO_P2V(MX6Q_AIPS3_BASE_ADDR) >> 20) << 2) / 4;
		*((unsigned long *)iram_tlb_base_addr + i) =
		(MX6Q_AIPS3_BASE_ADDR & 0xFFF00000) |
		TT_ATTRIB_NON_CACHEABLE_1M;

	/*
	 * Make sure the L2 controller virtual address has a mapping
	 * in the IRAM page table.
	 */
	i = ((IMX_IO_P2V(MX6Q_L2_BASE_ADDR) >> 20) << 2) / 4;
	*((unsigned long *)iram_tlb_base_addr + i) =
		(MX6Q_L2_BASE_ADDR & 0xFFF00000) | TT_ATTRIB_NON_CACHEABLE_1M;
}

static int __init imx6q_suspend_init(const struct imx6_pm_socdata *socdata)
{
	struct device_node *node;
	struct imx6_cpu_pm_info *pm_info;
	unsigned long iram_paddr;
	int i, ret = 0;
	const u32 *mmdc_offset_array;
	const u32 *mmdc_io_offset_array;

	suspend_set_ops(&imx6q_pm_ops);

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
	iram_paddr = iram_tlb_phys_addr + MX6_SUSPEND_IRAM_ADDR_OFFSET;

	/* Make sure iram_paddr is 8 byte aligned. */
	if ((uintptr_t)(iram_paddr) & (FNCPY_ALIGN - 1))
		iram_paddr += FNCPY_ALIGN - iram_paddr % (FNCPY_ALIGN);

	/* Get the virtual address of the suspend code. */
	suspend_ocram_base = (void *)IMX_IO_P2V(iram_paddr);

	pm_info = suspend_ocram_base;
	pm_info->pbase = iram_paddr;
	pm_info->resume_addr = virt_to_phys(v7_cpu_resume);
	pm_info->pm_info_size = sizeof(*pm_info);

	/*
	 * ccm physical address is not used by asm code currently,
	 * so get ccm virtual address directly, as we already have
	 * it from ccm driver.
	 */
	pm_info->ccm_base.pbase = MX6Q_CCM_BASE_ADDR;
	pm_info->ccm_base.vbase = (void __iomem *)
				   IMX_IO_P2V(MX6Q_CCM_BASE_ADDR);

	pm_info->mmdc_base.pbase = MX6Q_MMDC_P0_BASE_ADDR;
	pm_info->mmdc_base.vbase = (void __iomem *)
				    IMX_IO_P2V(MX6Q_MMDC_P0_BASE_ADDR);

	pm_info->src_base.pbase = MX6Q_SRC_BASE_ADDR;
	pm_info->src_base.vbase = (void __iomem *)
				   IMX_IO_P2V(MX6Q_SRC_BASE_ADDR);

	pm_info->iomuxc_base.pbase = MX6Q_IOMUXC_BASE_ADDR;
	pm_info->iomuxc_base.vbase = (void __iomem *)
				      IMX_IO_P2V(MX6Q_IOMUXC_BASE_ADDR);

	pm_info->gpc_base.pbase = MX6Q_GPC_BASE_ADDR;
	pm_info->gpc_base.vbase = (void __iomem *)
				   IMX_IO_P2V(MX6Q_GPC_BASE_ADDR);

	pm_info->l2_base.pbase = MX6Q_L2_BASE_ADDR;
	pm_info->l2_base.vbase = (void __iomem *)
				  IMX_IO_P2V(MX6Q_L2_BASE_ADDR);

	pm_info->anatop_base.pbase = MX6Q_ANATOP_BASE_ADDR;
	pm_info->anatop_base.vbase = (void __iomem *)
				  IMX_IO_P2V(MX6Q_ANATOP_BASE_ADDR);


	pm_info->ddr_type = imx_mmdc_get_ddr_type();
	pm_info->mmdc_io_num = socdata->mmdc_io_num;
	mmdc_io_offset_array = socdata->mmdc_io_offset;
	pm_info->mmdc_num = socdata->mmdc_num;
	mmdc_offset_array = socdata->mmdc_offset;

	for (i = 0; i < pm_info->mmdc_io_num; i++) {
		pm_info->mmdc_io_val[i][0] =
			mmdc_io_offset_array[i];
		pm_info->mmdc_io_val[i][1] =
			readl_relaxed(pm_info->iomuxc_base.vbase +
			mmdc_io_offset_array[i]);
	}

	/* initialize MMDC settings */
	for (i = 0; i < pm_info->mmdc_num; i++) {
		pm_info->mmdc_val[i][0] =
			mmdc_offset_array[i];
		pm_info->mmdc_val[i][1] =
			readl_relaxed(pm_info->mmdc_base.vbase +
			mmdc_offset_array[i]);
	}

	/* need to overwrite the value for some mmdc registers */
	if (cpu_is_imx6ul()) {
		pm_info->mmdc_val[20][1] = (pm_info->mmdc_val[20][1]
			& 0xffff0000) | 0x0202;
		pm_info->mmdc_val[23][1] = 0x8033;
	}

	imx6_suspend_in_ocram_fn = fncpy(
		suspend_ocram_base + sizeof(*pm_info),
		&imx6_suspend,
		MX6Q_SUSPEND_OCRAM_SIZE - sizeof(*pm_info));

	goto put_node;

put_node:
	of_node_put(node);

	return ret;
}

static void __init imx6_pm_common_init(const struct imx6_pm_socdata
					*socdata)
{
	struct regmap *gpr;
	int ret;

	WARN_ON(!ccm_base);

	if (IS_ENABLED(CONFIG_SUSPEND)) {
		ret = imx6q_suspend_init(socdata);
		if (ret)
			pr_warn("%s: No DDR LPM support with suspend %d!\n",
				__func__, ret);
	}

	/*
	 * This is for SW workaround step #1 of ERR007265, see comments
	 * in imx6q_set_lpm for details of this errata.
	 * Force IOMUXC irq pending, so that the interrupt to GPC can be
	 * used to deassert dsm_request signal when the signal gets
	 * asserted unexpectedly.
	 */
	gpr = syscon_regmap_lookup_by_compatible("fsl,imx6q-iomuxc-gpr");
	if (!IS_ERR(gpr))
		regmap_update_bits(gpr, IOMUXC_GPR1, IMX6Q_GPR1_GINT,
				   IMX6Q_GPR1_GINT);
}

void __init imx6q_pm_init(void)
{
	imx6_pm_common_init(&imx6q_pm_data);
}

void __init imx6dl_pm_init(void)
{
	imx6_pm_common_init(&imx6dl_pm_data);
}

void __init imx6sl_pm_init(void)
{
	imx6_pm_common_init(&imx6sl_pm_data);
}

void __init imx6sx_pm_init(void)
{
	imx6_pm_common_init(&imx6sx_pm_data);
}

void __init imx6ul_pm_init(void)
{
	imx6_pm_common_init(&imx6ul_pm_data);
}
