#include <linux/init.h>
#include <linux/suspend.h>
#include <linux/io.h>
#include <asm/time.h>
#include <asm/cacheflush.h>
#include <asm/mpc52xx.h>

#include "mpc52xx_pic.h"


/* these are defined in mpc52xx_sleep.S, and only used here */
extern void mpc52xx_deep_sleep(void __iomem *sram, void __iomem *sdram_regs,
		struct mpc52xx_cdm __iomem *, struct mpc52xx_intr __iomem*);
extern void mpc52xx_ds_sram(void);
extern const long mpc52xx_ds_sram_size;
extern void mpc52xx_ds_cached(void);
extern const long mpc52xx_ds_cached_size;

static void __iomem *mbar;
static void __iomem *sdram;
static struct mpc52xx_cdm __iomem *cdm;
static struct mpc52xx_intr __iomem *intr;
static struct mpc52xx_gpio_wkup __iomem *gpiow;
static void __iomem *sram;
static int sram_size;

struct mpc52xx_suspend mpc52xx_suspend;

static int mpc52xx_pm_valid(suspend_state_t state)
{
	switch (state) {
	case PM_SUSPEND_STANDBY:
		return 1;
	default:
		return 0;
	}
}

int mpc52xx_set_wakeup_gpio(u8 pin, u8 level)
{
	u16 tmp;

	/* enable gpio */
	out_8(&gpiow->wkup_gpioe, in_8(&gpiow->wkup_gpioe) | (1 << pin));
	/* set as input */
	out_8(&gpiow->wkup_ddr, in_8(&gpiow->wkup_ddr) & ~(1 << pin));
	/* enable deep sleep interrupt */
	out_8(&gpiow->wkup_inten, in_8(&gpiow->wkup_inten) | (1 << pin));
	/* low/high level creates wakeup interrupt */
	tmp = in_be16(&gpiow->wkup_itype);
	tmp &= ~(0x3 << (pin * 2));
	tmp |= (!level + 1) << (pin * 2);
	out_be16(&gpiow->wkup_itype, tmp);
	/* master enable */
	out_8(&gpiow->wkup_maste, 1);

	return 0;
}

int mpc52xx_pm_prepare(suspend_state_t state)
{
	if (state != PM_SUSPEND_STANDBY)
		return -EINVAL;

	/* map the whole register space */
	mbar = mpc52xx_find_and_map("mpc5200");
	if (!mbar) {
		printk(KERN_ERR "%s:%i Error mapping registers\n", __func__, __LINE__);
		return -ENOSYS;
	}
	/* these offsets are from mpc5200 users manual */
	sdram	= mbar + 0x100;
	cdm	= mbar + 0x200;
	intr	= mbar + 0x500;
	gpiow	= mbar + 0xc00;
	sram	= mbar + 0x8000;	/* Those will be handled by the */
	sram_size = 0x4000;		/* bestcomm driver soon */

	/* call board suspend code, if applicable */
	if (mpc52xx_suspend.board_suspend_prepare)
		mpc52xx_suspend.board_suspend_prepare(mbar);
	else {
		printk(KERN_ALERT "%s: %i don't know how to wake up the board\n",
				__func__, __LINE__);
		goto out_unmap;
	}

	return 0;

 out_unmap:
	iounmap(mbar);
	return -ENOSYS;
}


char saved_sram[0x4000];

int mpc52xx_pm_enter(suspend_state_t state)
{
	u32 clk_enables;
	u32 msr, hid0;
	u32 intr_main_mask;
	void __iomem * irq_0x500 = (void __iomem *)CONFIG_KERNEL_START + 0x500;
	unsigned long irq_0x500_stop = (unsigned long)irq_0x500 + mpc52xx_ds_cached_size;
	char saved_0x500[mpc52xx_ds_cached_size];

	/* disable all interrupts in PIC */
	intr_main_mask = in_be32(&intr->main_mask);
	out_be32(&intr->main_mask, intr_main_mask | 0x1ffff);

	/* don't let DEC expire any time soon */
	mtspr(SPRN_DEC, 0x7fffffff);

	/* save SRAM */
	memcpy(saved_sram, sram, sram_size);

	/* copy low level suspend code to sram */
	memcpy(sram, mpc52xx_ds_sram, mpc52xx_ds_sram_size);

	out_8(&cdm->ccs_sleep_enable, 1);
	out_8(&cdm->osc_sleep_enable, 1);
	out_8(&cdm->ccs_qreq_test, 1);

	/* disable all but SDRAM and bestcomm (SRAM) clocks */
	clk_enables = in_be32(&cdm->clk_enables);
	out_be32(&cdm->clk_enables, clk_enables & 0x00088000);

	/* disable power management */
	msr = mfmsr();
	mtmsr(msr & ~MSR_POW);

	/* enable sleep mode, disable others */
	hid0 = mfspr(SPRN_HID0);
	mtspr(SPRN_HID0, (hid0 & ~(HID0_DOZE | HID0_NAP | HID0_DPM)) | HID0_SLEEP);

	/* save original, copy our irq handler, flush from dcache and invalidate icache */
	memcpy(saved_0x500, irq_0x500, mpc52xx_ds_cached_size);
	memcpy(irq_0x500, mpc52xx_ds_cached, mpc52xx_ds_cached_size);
	flush_icache_range((unsigned long)irq_0x500, irq_0x500_stop);

	/* call low-level sleep code */
	mpc52xx_deep_sleep(sram, sdram, cdm, intr);

	/* restore original irq handler */
	memcpy(irq_0x500, saved_0x500, mpc52xx_ds_cached_size);
	flush_icache_range((unsigned long)irq_0x500, irq_0x500_stop);

	/* restore old power mode */
	mtmsr(msr & ~MSR_POW);
	mtspr(SPRN_HID0, hid0);
	mtmsr(msr);

	out_be32(&cdm->clk_enables, clk_enables);
	out_8(&cdm->ccs_sleep_enable, 0);
	out_8(&cdm->osc_sleep_enable, 0);

	/* restore SRAM */
	memcpy(sram, saved_sram, sram_size);

	/* restart jiffies */
	wakeup_decrementer();

	/* reenable interrupts in PIC */
	out_be32(&intr->main_mask, intr_main_mask);

	return 0;
}

int mpc52xx_pm_finish(suspend_state_t state)
{
	/* call board resume code */
	if (mpc52xx_suspend.board_resume_finish)
		mpc52xx_suspend.board_resume_finish(mbar);

	iounmap(mbar);

	return 0;
}

static struct pm_ops mpc52xx_pm_ops = {
	.valid		= mpc52xx_pm_valid,
	.prepare	= mpc52xx_pm_prepare,
	.enter		= mpc52xx_pm_enter,
	.finish		= mpc52xx_pm_finish,
};

int __init mpc52xx_pm_init(void)
{
	pm_set_ops(&mpc52xx_pm_ops);
	return 0;
}
