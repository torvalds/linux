#include <linux/init.h>
#include <linux/suspend.h>
#include <asm/io.h>
#include <asm/time.h>
#include <asm/mpc52xx.h>
#include "mpc52xx_pic.h"

/* defined in lite5200_sleep.S and only used here */
extern void lite5200_low_power(void __iomem *sram, void __iomem *mbar);

static struct mpc52xx_cdm __iomem *cdm;
static struct mpc52xx_intr __iomem *pic;
static struct mpc52xx_sdma __iomem *bes;
static struct mpc52xx_xlb __iomem *xlb;
static struct mpc52xx_gpio __iomem *gps;
static struct mpc52xx_gpio_wkup __iomem *gpw;
static void __iomem *sram;
static const int sram_size = 0x4000;	/* 16 kBytes */
static void __iomem *mbar;

static suspend_state_t lite5200_pm_target_state;

static int lite5200_pm_valid(suspend_state_t state)
{
	switch (state) {
	case PM_SUSPEND_STANDBY:
	case PM_SUSPEND_MEM:
		return 1;
	default:
		return 0;
	}
}

static int lite5200_pm_set_target(suspend_state_t state)
{
	if (lite5200_pm_valid(state)) {
		lite5200_pm_target_state = state;
		return 0;
	}
	return -EINVAL;
}

static int lite5200_pm_prepare(void)
{
	struct device_node *np;
	const struct of_device_id immr_ids[] = {
		{ .compatible = "fsl,mpc5200-immr", },
		{ .compatible = "fsl,mpc5200b-immr", },
		{ .type = "soc", .compatible = "mpc5200", }, /* lite5200 */
		{ .type = "builtin", .compatible = "mpc5200", }, /* efika */
		{}
	};

	/* deep sleep? let mpc52xx code handle that */
	if (lite5200_pm_target_state == PM_SUSPEND_STANDBY)
		return mpc52xx_pm_prepare();

	if (lite5200_pm_target_state != PM_SUSPEND_MEM)
		return -EINVAL;

	/* map registers */
	np = of_find_matching_node(NULL, immr_ids);
	mbar = of_iomap(np, 0);
	of_node_put(np);
	if (!mbar) {
		printk(KERN_ERR "%s:%i Error mapping registers\n", __func__, __LINE__);
		return -ENOSYS;
	}

	cdm = mbar + 0x200;
	pic = mbar + 0x500;
	gps = mbar + 0xb00;
	gpw = mbar + 0xc00;
	bes = mbar + 0x1200;
	xlb = mbar + 0x1f00;
	sram = mbar + 0x8000;

	return 0;
}

/* save and restore registers not bound to any real devices */
static struct mpc52xx_cdm scdm;
static struct mpc52xx_intr spic;
static struct mpc52xx_sdma sbes;
static struct mpc52xx_xlb sxlb;
static struct mpc52xx_gpio sgps;
static struct mpc52xx_gpio_wkup sgpw;

static void lite5200_save_regs(void)
{
	_memcpy_fromio(&spic, pic, sizeof(*pic));
	_memcpy_fromio(&sbes, bes, sizeof(*bes));
	_memcpy_fromio(&scdm, cdm, sizeof(*cdm));
	_memcpy_fromio(&sxlb, xlb, sizeof(*xlb));
	_memcpy_fromio(&sgps, gps, sizeof(*gps));
	_memcpy_fromio(&sgpw, gpw, sizeof(*gpw));

	_memcpy_fromio(saved_sram, sram, sram_size);
}

static void lite5200_restore_regs(void)
{
	int i;
	_memcpy_toio(sram, saved_sram, sram_size);


	/*
	 * GPIOs. Interrupt Master Enable has higher address then other
	 * registers, so just memcpy is ok.
	 */
	_memcpy_toio(gpw, &sgpw, sizeof(*gpw));
	_memcpy_toio(gps, &sgps, sizeof(*gps));


	/* XLB Arbitrer */
	out_be32(&xlb->snoop_window, sxlb.snoop_window);
	out_be32(&xlb->master_priority, sxlb.master_priority);
	out_be32(&xlb->master_pri_enable, sxlb.master_pri_enable);

	/* enable */
	out_be32(&xlb->int_enable, sxlb.int_enable);
	out_be32(&xlb->config, sxlb.config);


	/* CDM - Clock Distribution Module */
	out_8(&cdm->ipb_clk_sel, scdm.ipb_clk_sel);
	out_8(&cdm->pci_clk_sel, scdm.pci_clk_sel);

	out_8(&cdm->ext_48mhz_en, scdm.ext_48mhz_en);
	out_8(&cdm->fd_enable, scdm.fd_enable);
	out_be16(&cdm->fd_counters, scdm.fd_counters);

	out_be32(&cdm->clk_enables, scdm.clk_enables);

	out_8(&cdm->osc_disable, scdm.osc_disable);

	out_be16(&cdm->mclken_div_psc1, scdm.mclken_div_psc1);
	out_be16(&cdm->mclken_div_psc2, scdm.mclken_div_psc2);
	out_be16(&cdm->mclken_div_psc3, scdm.mclken_div_psc3);
	out_be16(&cdm->mclken_div_psc6, scdm.mclken_div_psc6);


	/* BESTCOMM */
	out_be32(&bes->taskBar, sbes.taskBar);
	out_be32(&bes->currentPointer, sbes.currentPointer);
	out_be32(&bes->endPointer, sbes.endPointer);
	out_be32(&bes->variablePointer, sbes.variablePointer);

	out_8(&bes->IntVect1, sbes.IntVect1);
	out_8(&bes->IntVect2, sbes.IntVect2);
	out_be16(&bes->PtdCntrl, sbes.PtdCntrl);

	for (i=0; i<32; i++)
		out_8(&bes->ipr[i], sbes.ipr[i]);

	out_be32(&bes->cReqSelect, sbes.cReqSelect);
	out_be32(&bes->task_size0, sbes.task_size0);
	out_be32(&bes->task_size1, sbes.task_size1);
	out_be32(&bes->MDEDebug, sbes.MDEDebug);
	out_be32(&bes->ADSDebug, sbes.ADSDebug);
	out_be32(&bes->Value1, sbes.Value1);
	out_be32(&bes->Value2, sbes.Value2);
	out_be32(&bes->Control, sbes.Control);
	out_be32(&bes->Status, sbes.Status);
	out_be32(&bes->PTDDebug, sbes.PTDDebug);

	/* restore tasks */
	for (i=0; i<16; i++)
		out_be16(&bes->tcr[i], sbes.tcr[i]);

	/* enable interrupts */
	out_be32(&bes->IntPend, sbes.IntPend);
	out_be32(&bes->IntMask, sbes.IntMask);


	/* PIC */
	out_be32(&pic->per_pri1, spic.per_pri1);
	out_be32(&pic->per_pri2, spic.per_pri2);
	out_be32(&pic->per_pri3, spic.per_pri3);

	out_be32(&pic->main_pri1, spic.main_pri1);
	out_be32(&pic->main_pri2, spic.main_pri2);

	out_be32(&pic->enc_status, spic.enc_status);

	/* unmask and enable interrupts */
	out_be32(&pic->per_mask, spic.per_mask);
	out_be32(&pic->main_mask, spic.main_mask);
	out_be32(&pic->ctrl, spic.ctrl);
}

static int lite5200_pm_enter(suspend_state_t state)
{
	/* deep sleep? let mpc52xx code handle that */
	if (state == PM_SUSPEND_STANDBY) {
		return mpc52xx_pm_enter(state);
	}

	lite5200_save_regs();

	/* effectively save FP regs */
	enable_kernel_fp();

	lite5200_low_power(sram, mbar);

	lite5200_restore_regs();

	/* restart jiffies */
	wakeup_decrementer();

	iounmap(mbar);
	return 0;
}

static void lite5200_pm_finish(void)
{
	/* deep sleep? let mpc52xx code handle that */
	if (lite5200_pm_target_state == PM_SUSPEND_STANDBY)
		mpc52xx_pm_finish();
}

static struct platform_suspend_ops lite5200_pm_ops = {
	.valid		= lite5200_pm_valid,
	.set_target	= lite5200_pm_set_target,
	.prepare	= lite5200_pm_prepare,
	.enter		= lite5200_pm_enter,
	.finish		= lite5200_pm_finish,
};

int __init lite5200_pm_init(void)
{
	suspend_set_ops(&lite5200_pm_ops);
	return 0;
}
