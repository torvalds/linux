// SPDX-License-Identifier: GPL-2.0-only
/*
 * MIPS-specific support for Broadcom STB S2/S3/S5 power management
 *
 * Copyright (C) 2016-2017 Broadcom
 */

#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/delay.h>
#include <linux/suspend.h>
#include <asm/bmips.h>
#include <asm/tlbflush.h>

#include "pm.h"

#define S2_NUM_PARAMS		6
#define MAX_NUM_MEMC		3

/* S3 constants */
#define MAX_GP_REGS		16
#define MAX_CP0_REGS		32
#define NUM_MEMC_CLIENTS	128
#define AON_CTRL_RAM_SIZE	128
#define BRCMSTB_S3_MAGIC	0x5AFEB007

#define CLEAR_RESET_MASK	0x01

/* Index each CP0 register that needs to be saved */
#define CONTEXT		0
#define USER_LOCAL	1
#define PGMK		2
#define HWRENA		3
#define COMPARE		4
#define STATUS		5
#define CONFIG		6
#define MODE		7
#define EDSP		8
#define BOOT_VEC	9
#define EBASE		10

struct brcmstb_memc {
	void __iomem *ddr_phy_base;
	void __iomem *arb_base;
};

struct brcmstb_pm_control {
	void __iomem *aon_ctrl_base;
	void __iomem *aon_sram_base;
	void __iomem *timers_base;
	struct brcmstb_memc memcs[MAX_NUM_MEMC];
	int num_memc;
};

struct brcm_pm_s3_context {
	u32			cp0_regs[MAX_CP0_REGS];
	u32			memc0_rts[NUM_MEMC_CLIENTS];
	u32			sc_boot_vec;
};

struct brcmstb_mem_transfer;

struct brcmstb_mem_transfer {
	struct brcmstb_mem_transfer	*next;
	void				*src;
	void				*dst;
	dma_addr_t			pa_src;
	dma_addr_t			pa_dst;
	u32				len;
	u8				key;
	u8				mode;
	u8				src_remapped;
	u8				dst_remapped;
	u8				src_dst_remapped;
};

#define AON_SAVE_SRAM(base, idx, val) \
	__raw_writel(val, base + (idx << 2))

/* Used for saving registers in asm */
u32 gp_regs[MAX_GP_REGS];

#define	BSP_CLOCK_STOP		0x00
#define PM_INITIATE		0x01

static struct brcmstb_pm_control ctrl;

static void brcm_pm_save_cp0_context(struct brcm_pm_s3_context *ctx)
{
	/* Generic MIPS */
	ctx->cp0_regs[CONTEXT] = read_c0_context();
	ctx->cp0_regs[USER_LOCAL] = read_c0_userlocal();
	ctx->cp0_regs[PGMK] = read_c0_pagemask();
	ctx->cp0_regs[HWRENA] = read_c0_cache();
	ctx->cp0_regs[COMPARE] = read_c0_compare();
	ctx->cp0_regs[STATUS] = read_c0_status();

	/* Broadcom specific */
	ctx->cp0_regs[CONFIG] = read_c0_brcm_config();
	ctx->cp0_regs[MODE] = read_c0_brcm_mode();
	ctx->cp0_regs[EDSP] = read_c0_brcm_edsp();
	ctx->cp0_regs[BOOT_VEC] = read_c0_brcm_bootvec();
	ctx->cp0_regs[EBASE] = read_c0_ebase();

	ctx->sc_boot_vec = bmips_read_zscm_reg(0xa0);
}

static void brcm_pm_restore_cp0_context(struct brcm_pm_s3_context *ctx)
{
	/* Restore cp0 state */
	bmips_write_zscm_reg(0xa0, ctx->sc_boot_vec);

	/* Generic MIPS */
	write_c0_context(ctx->cp0_regs[CONTEXT]);
	write_c0_userlocal(ctx->cp0_regs[USER_LOCAL]);
	write_c0_pagemask(ctx->cp0_regs[PGMK]);
	write_c0_cache(ctx->cp0_regs[HWRENA]);
	write_c0_compare(ctx->cp0_regs[COMPARE]);
	write_c0_status(ctx->cp0_regs[STATUS]);

	/* Broadcom specific */
	write_c0_brcm_config(ctx->cp0_regs[CONFIG]);
	write_c0_brcm_mode(ctx->cp0_regs[MODE]);
	write_c0_brcm_edsp(ctx->cp0_regs[EDSP]);
	write_c0_brcm_bootvec(ctx->cp0_regs[BOOT_VEC]);
	write_c0_ebase(ctx->cp0_regs[EBASE]);
}

static void  brcmstb_pm_handshake(void)
{
	void __iomem *base = ctrl.aon_ctrl_base;
	u32 tmp;

	/* BSP power handshake, v1 */
	tmp = __raw_readl(base + AON_CTRL_HOST_MISC_CMDS);
	tmp &= ~1UL;
	__raw_writel(tmp, base + AON_CTRL_HOST_MISC_CMDS);
	(void)__raw_readl(base + AON_CTRL_HOST_MISC_CMDS);

	__raw_writel(0, base + AON_CTRL_PM_INITIATE);
	(void)__raw_readl(base + AON_CTRL_PM_INITIATE);
	__raw_writel(BSP_CLOCK_STOP | PM_INITIATE,
		     base + AON_CTRL_PM_INITIATE);
	/*
	 * HACK: BSP may have internal race on the CLOCK_STOP command.
	 * Avoid touching the BSP for a few milliseconds.
	 */
	mdelay(3);
}

static void brcmstb_pm_s5(void)
{
	void __iomem *base = ctrl.aon_ctrl_base;

	brcmstb_pm_handshake();

	/* Clear magic s3 warm-boot value */
	AON_SAVE_SRAM(ctrl.aon_sram_base, 0, 0);

	/* Set the countdown */
	__raw_writel(0x10, base + AON_CTRL_PM_CPU_WAIT_COUNT);
	(void)__raw_readl(base + AON_CTRL_PM_CPU_WAIT_COUNT);

	/* Prepare to S5 cold boot */
	__raw_writel(PM_COLD_CONFIG, base + AON_CTRL_PM_CTRL);
	(void)__raw_readl(base + AON_CTRL_PM_CTRL);

	__raw_writel((PM_COLD_CONFIG | PM_PWR_DOWN), base +
		      AON_CTRL_PM_CTRL);
	(void)__raw_readl(base + AON_CTRL_PM_CTRL);

	__asm__ __volatile__(
	"	wait\n"
	: : : "memory");
}

static int brcmstb_pm_s3(void)
{
	struct brcm_pm_s3_context s3_context;
	void __iomem *memc_arb_base;
	unsigned long flags;
	u32 tmp;
	int i;

	/* Prepare for s3 */
	AON_SAVE_SRAM(ctrl.aon_sram_base, 0, BRCMSTB_S3_MAGIC);
	AON_SAVE_SRAM(ctrl.aon_sram_base, 1, (u32)&s3_reentry);
	AON_SAVE_SRAM(ctrl.aon_sram_base, 2, 0);

	/* Clear RESET_HISTORY */
	tmp = __raw_readl(ctrl.aon_ctrl_base + AON_CTRL_RESET_CTRL);
	tmp &= ~CLEAR_RESET_MASK;
	__raw_writel(tmp, ctrl.aon_ctrl_base + AON_CTRL_RESET_CTRL);

	local_irq_save(flags);

	/* Inhibit DDR_RSTb pulse for both MMCs*/
	for (i = 0; i < ctrl.num_memc; i++) {
		tmp = __raw_readl(ctrl.memcs[i].ddr_phy_base +
			DDR40_PHY_CONTROL_REGS_0_STANDBY_CTRL);

		tmp &= ~0x0f;
		__raw_writel(tmp, ctrl.memcs[i].ddr_phy_base +
			DDR40_PHY_CONTROL_REGS_0_STANDBY_CTRL);
		tmp |= (0x05 | BIT(5));
		__raw_writel(tmp, ctrl.memcs[i].ddr_phy_base +
			DDR40_PHY_CONTROL_REGS_0_STANDBY_CTRL);
	}

	/* Save CP0 context */
	brcm_pm_save_cp0_context(&s3_context);

	/* Save RTS(skip debug register) */
	memc_arb_base = ctrl.memcs[0].arb_base + 4;
	for (i = 0; i < NUM_MEMC_CLIENTS; i++) {
		s3_context.memc0_rts[i] = __raw_readl(memc_arb_base);
		memc_arb_base += 4;
	}

	/* Save I/O context */
	local_flush_tlb_all();
	_dma_cache_wback_inv(0, ~0);

	brcm_pm_do_s3(ctrl.aon_ctrl_base, current_cpu_data.dcache.linesz);

	/* CPU reconfiguration */
	local_flush_tlb_all();
	bmips_cpu_setup();
	cpumask_clear(&bmips_booted_mask);

	/* Restore RTS (skip debug register) */
	memc_arb_base = ctrl.memcs[0].arb_base + 4;
	for (i = 0; i < NUM_MEMC_CLIENTS; i++) {
		__raw_writel(s3_context.memc0_rts[i], memc_arb_base);
		memc_arb_base += 4;
	}

	/* restore CP0 context */
	brcm_pm_restore_cp0_context(&s3_context);

	local_irq_restore(flags);

	return 0;
}

static int brcmstb_pm_s2(void)
{
	/*
	 * We need to pass 6 arguments to an assembly function. Lets avoid the
	 * stack and pass arguments in a explicit 4 byte array. The assembly
	 * code assumes all arguments are 4 bytes and arguments are ordered
	 * like so:
	 *
	 * 0: AON_CTRl base register
	 * 1: DDR_PHY base register
	 * 2: TIMERS base resgister
	 * 3: I-Cache line size
	 * 4: Restart vector address
	 * 5: Restart vector size
	 */
	u32 s2_params[6];

	/* Prepare s2 parameters */
	s2_params[0] = (u32)ctrl.aon_ctrl_base;
	s2_params[1] = (u32)ctrl.memcs[0].ddr_phy_base;
	s2_params[2] = (u32)ctrl.timers_base;
	s2_params[3] = (u32)current_cpu_data.icache.linesz;
	s2_params[4] = (u32)BMIPS_WARM_RESTART_VEC;
	s2_params[5] = (u32)(bmips_smp_int_vec_end -
		bmips_smp_int_vec);

	/* Drop to standby */
	brcm_pm_do_s2(s2_params);

	return 0;
}

static int brcmstb_pm_standby(bool deep_standby)
{
	brcmstb_pm_handshake();

	/* Send IRQs to BMIPS_WARM_RESTART_VEC */
	clear_c0_cause(CAUSEF_IV);
	irq_disable_hazard();
	set_c0_status(ST0_BEV);
	irq_disable_hazard();

	if (deep_standby)
		brcmstb_pm_s3();
	else
		brcmstb_pm_s2();

	/* Send IRQs to normal runtime vectors */
	clear_c0_status(ST0_BEV);
	irq_disable_hazard();
	set_c0_cause(CAUSEF_IV);
	irq_disable_hazard();

	return 0;
}

static int brcmstb_pm_enter(suspend_state_t state)
{
	int ret = -EINVAL;

	switch (state) {
	case PM_SUSPEND_STANDBY:
		ret = brcmstb_pm_standby(false);
		break;
	case PM_SUSPEND_MEM:
		ret = brcmstb_pm_standby(true);
		break;
	}

	return ret;
}

static int brcmstb_pm_valid(suspend_state_t state)
{
	switch (state) {
	case PM_SUSPEND_STANDBY:
		return true;
	case PM_SUSPEND_MEM:
		return true;
	default:
		return false;
	}
}

static const struct platform_suspend_ops brcmstb_pm_ops = {
	.enter		= brcmstb_pm_enter,
	.valid		= brcmstb_pm_valid,
};

static const struct of_device_id aon_ctrl_dt_ids[] = {
	{ .compatible = "brcm,brcmstb-aon-ctrl" },
	{ /* sentinel */ }
};

static const struct of_device_id ddr_phy_dt_ids[] = {
	{ .compatible = "brcm,brcmstb-ddr-phy" },
	{ /* sentinel */ }
};

static const struct of_device_id arb_dt_ids[] = {
	{ .compatible = "brcm,brcmstb-memc-arb" },
	{ /* sentinel */ }
};

static const struct of_device_id timers_ids[] = {
	{ .compatible = "brcm,brcmstb-timers" },
	{ /* sentinel */ }
};

static inline void __iomem *brcmstb_ioremap_node(struct device_node *dn,
						 int index)
{
	return of_io_request_and_map(dn, index, dn->full_name);
}

static void __iomem *brcmstb_ioremap_match(const struct of_device_id *matches,
					   int index, const void **ofdata)
{
	struct device_node *dn;
	const struct of_device_id *match;

	dn = of_find_matching_node_and_match(NULL, matches, &match);
	if (!dn)
		return ERR_PTR(-EINVAL);

	if (ofdata)
		*ofdata = match->data;

	return brcmstb_ioremap_node(dn, index);
}

static int brcmstb_pm_init(void)
{
	struct device_node *dn;
	void __iomem *base;
	int i;

	/* AON ctrl registers */
	base = brcmstb_ioremap_match(aon_ctrl_dt_ids, 0, NULL);
	if (IS_ERR(base)) {
		pr_err("error mapping AON_CTRL\n");
		goto aon_err;
	}
	ctrl.aon_ctrl_base = base;

	/* AON SRAM registers */
	base = brcmstb_ioremap_match(aon_ctrl_dt_ids, 1, NULL);
	if (IS_ERR(base)) {
		pr_err("error mapping AON_SRAM\n");
		goto sram_err;
	}
	ctrl.aon_sram_base = base;

	ctrl.num_memc = 0;
	/* Map MEMC DDR PHY registers */
	for_each_matching_node(dn, ddr_phy_dt_ids) {
		i = ctrl.num_memc;
		if (i >= MAX_NUM_MEMC) {
			pr_warn("Too many MEMCs (max %d)\n", MAX_NUM_MEMC);
			of_node_put(dn);
			break;
		}
		base = brcmstb_ioremap_node(dn, 0);
		if (IS_ERR(base)) {
			of_node_put(dn);
			goto ddr_err;
		}

		ctrl.memcs[i].ddr_phy_base = base;
		ctrl.num_memc++;
	}

	/* MEMC ARB registers */
	base = brcmstb_ioremap_match(arb_dt_ids, 0, NULL);
	if (IS_ERR(base)) {
		pr_err("error mapping MEMC ARB\n");
		goto ddr_err;
	}
	ctrl.memcs[0].arb_base = base;

	/* Timer registers */
	base = brcmstb_ioremap_match(timers_ids, 0, NULL);
	if (IS_ERR(base)) {
		pr_err("error mapping timers\n");
		goto tmr_err;
	}
	ctrl.timers_base = base;

	/* s3 cold boot aka s5 */
	pm_power_off = brcmstb_pm_s5;

	suspend_set_ops(&brcmstb_pm_ops);

	return 0;

tmr_err:
	iounmap(ctrl.memcs[0].arb_base);
ddr_err:
	for (i = 0; i < ctrl.num_memc; i++)
		iounmap(ctrl.memcs[i].ddr_phy_base);

	iounmap(ctrl.aon_sram_base);
sram_err:
	iounmap(ctrl.aon_ctrl_base);
aon_err:
	return PTR_ERR(base);
}
arch_initcall(brcmstb_pm_init);
