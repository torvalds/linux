/*
 * Copyright 2008-2009 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright 2010 Orex Computed Radiography
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

/* based on rtc-mc13892.c */

/*
 * This driver uses the 47-bit 32 kHz counter in the Freescale DryIce block
 * to implement a Linux RTC. Times and alarms are truncated to seconds.
 * Since the RTC framework performs API locking via rtc->ops_lock the
 * only simultaneous accesses we need to deal with is updating DryIce
 * registers while servicing an alarm.
 *
 * Note that reading the DSR (DryIce Status Register) automatically clears
 * the WCF (Write Complete Flag). All DryIce writes are synchronized to the
 * LP (Low Power) domain and set the WCF upon completion. Writes to the
 * DIER (DryIce Interrupt Enable Register) are the only exception. These
 * occur at normal bus speeds and do not set WCF.  Periodic interrupts are
 * not supported by the hardware.
 */

#include <linux/io.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <linux/of.h>

/* DryIce Register Definitions */

#define DTCMR     0x00           /* Time Counter MSB Reg */
#define DTCLR     0x04           /* Time Counter LSB Reg */

#define DCAMR     0x08           /* Clock Alarm MSB Reg */
#define DCALR     0x0c           /* Clock Alarm LSB Reg */
#define DCAMR_UNSET  0xFFFFFFFF  /* doomsday - 1 sec */

#define DCR       0x10           /* Control Reg */
#define DCR_TDCHL (1 << 30)      /* Tamper-detect configuration hard lock */
#define DCR_TDCSL (1 << 29)      /* Tamper-detect configuration soft lock */
#define DCR_KSSL  (1 << 27)      /* Key-select soft lock */
#define DCR_MCHL  (1 << 20)      /* Monotonic-counter hard lock */
#define DCR_MCSL  (1 << 19)      /* Monotonic-counter soft lock */
#define DCR_TCHL  (1 << 18)      /* Timer-counter hard lock */
#define DCR_TCSL  (1 << 17)      /* Timer-counter soft lock */
#define DCR_FSHL  (1 << 16)      /* Failure state hard lock */
#define DCR_TCE   (1 << 3)       /* Time Counter Enable */
#define DCR_MCE   (1 << 2)       /* Monotonic Counter Enable */

#define DSR       0x14           /* Status Reg */
#define DSR_WTD   (1 << 23)      /* Wire-mesh tamper detected */
#define DSR_ETBD  (1 << 22)      /* External tamper B detected */
#define DSR_ETAD  (1 << 21)      /* External tamper A detected */
#define DSR_EBD   (1 << 20)      /* External boot detected */
#define DSR_SAD   (1 << 19)      /* SCC alarm detected */
#define DSR_TTD   (1 << 18)      /* Temperature tamper detected */
#define DSR_CTD   (1 << 17)      /* Clock tamper detected */
#define DSR_VTD   (1 << 16)      /* Voltage tamper detected */
#define DSR_WBF   (1 << 10)      /* Write Busy Flag (synchronous) */
#define DSR_WNF   (1 << 9)       /* Write Next Flag (synchronous) */
#define DSR_WCF   (1 << 8)       /* Write Complete Flag (synchronous)*/
#define DSR_WEF   (1 << 7)       /* Write Error Flag */
#define DSR_CAF   (1 << 4)       /* Clock Alarm Flag */
#define DSR_MCO   (1 << 3)       /* monotonic counter overflow */
#define DSR_TCO   (1 << 2)       /* time counter overflow */
#define DSR_NVF   (1 << 1)       /* Non-Valid Flag */
#define DSR_SVF   (1 << 0)       /* Security Violation Flag */

#define DIER      0x18           /* Interrupt Enable Reg (synchronous) */
#define DIER_WNIE (1 << 9)       /* Write Next Interrupt Enable */
#define DIER_WCIE (1 << 8)       /* Write Complete Interrupt Enable */
#define DIER_WEIE (1 << 7)       /* Write Error Interrupt Enable */
#define DIER_CAIE (1 << 4)       /* Clock Alarm Interrupt Enable */
#define DIER_SVIE (1 << 0)       /* Security-violation Interrupt Enable */

#define DMCR      0x1c           /* DryIce Monotonic Counter Reg */

#define DTCR      0x28           /* DryIce Tamper Configuration Reg */
#define DTCR_MOE  (1 << 9)       /* monotonic overflow enabled */
#define DTCR_TOE  (1 << 8)       /* time overflow enabled */
#define DTCR_WTE  (1 << 7)       /* wire-mesh tamper enabled */
#define DTCR_ETBE (1 << 6)       /* external B tamper enabled */
#define DTCR_ETAE (1 << 5)       /* external A tamper enabled */
#define DTCR_EBE  (1 << 4)       /* external boot tamper enabled */
#define DTCR_SAIE (1 << 3)       /* SCC enabled */
#define DTCR_TTE  (1 << 2)       /* temperature tamper enabled */
#define DTCR_CTE  (1 << 1)       /* clock tamper enabled */
#define DTCR_VTE  (1 << 0)       /* voltage tamper enabled */

#define DGPR      0x3c           /* DryIce General Purpose Reg */

/**
 * struct imxdi_dev - private imxdi rtc data
 * @pdev: pionter to platform dev
 * @rtc: pointer to rtc struct
 * @ioaddr: IO registers pointer
 * @clk: input reference clock
 * @dsr: copy of the DSR register
 * @irq_lock: interrupt enable register (DIER) lock
 * @write_wait: registers write complete queue
 * @write_mutex: serialize registers write
 * @work: schedule alarm work
 */
struct imxdi_dev {
	struct platform_device *pdev;
	struct rtc_device *rtc;
	void __iomem *ioaddr;
	struct clk *clk;
	u32 dsr;
	spinlock_t irq_lock;
	wait_queue_head_t write_wait;
	struct mutex write_mutex;
	struct work_struct work;
};

/* Some background:
 *
 * The DryIce unit is a complex security/tamper monitor device. To be able do
 * its job in a useful manner it runs a bigger statemachine to bring it into
 * security/tamper failure state and once again to bring it out of this state.
 *
 * This unit can be in one of three states:
 *
 * - "NON-VALID STATE"
 *   always after the battery power was removed
 * - "FAILURE STATE"
 *   if one of the enabled security events has happened
 * - "VALID STATE"
 *   if the unit works as expected
 *
 * Everything stops when the unit enters the failure state including the RTC
 * counter (to be able to detect the time the security event happened).
 *
 * The following events (when enabled) let the DryIce unit enter the failure
 * state:
 *
 * - wire-mesh-tamper detect
 * - external tamper B detect
 * - external tamper A detect
 * - temperature tamper detect
 * - clock tamper detect
 * - voltage tamper detect
 * - RTC counter overflow
 * - monotonic counter overflow
 * - external boot
 *
 * If we find the DryIce unit in "FAILURE STATE" and the TDCHL cleared, we
 * can only detect this state. In this case the unit is completely locked and
 * must force a second "SYSTEM POR" to bring the DryIce into the
 * "NON-VALID STATE" + "FAILURE STATE" where a recovery is possible.
 * If the TDCHL is set in the "FAILURE STATE" we are out of luck. In this case
 * a battery power cycle is required.
 *
 * In the "NON-VALID STATE" + "FAILURE STATE" we can clear the "FAILURE STATE"
 * and recover the DryIce unit. By clearing the "NON-VALID STATE" as the last
 * task, we bring back this unit into life.
 */

/*
 * Do a write into the unit without interrupt support.
 * We do not need to check the WEF here, because the only reason this kind of
 * write error can happen is if we write to the unit twice within the 122 us
 * interval. This cannot happen, since we are using this function only while
 * setting up the unit.
 */
static void di_write_busy_wait(const struct imxdi_dev *imxdi, u32 val,
			       unsigned reg)
{
	/* do the register write */
	writel(val, imxdi->ioaddr + reg);

	/*
	 * now it takes four 32,768 kHz clock cycles to take
	 * the change into effect = 122 us
	 */
	usleep_range(130, 200);
}

static void di_report_tamper_info(struct imxdi_dev *imxdi,  u32 dsr)
{
	u32 dtcr;

	dtcr = readl(imxdi->ioaddr + DTCR);

	dev_emerg(&imxdi->pdev->dev, "DryIce tamper event detected\n");
	/* the following flags force a transition into the "FAILURE STATE" */
	if (dsr & DSR_VTD)
		dev_emerg(&imxdi->pdev->dev, "%sVoltage Tamper Event\n",
			  dtcr & DTCR_VTE ? "" : "Spurious ");

	if (dsr & DSR_CTD)
		dev_emerg(&imxdi->pdev->dev, "%s32768 Hz Clock Tamper Event\n",
			  dtcr & DTCR_CTE ? "" : "Spurious ");

	if (dsr & DSR_TTD)
		dev_emerg(&imxdi->pdev->dev, "%sTemperature Tamper Event\n",
			  dtcr & DTCR_TTE ? "" : "Spurious ");

	if (dsr & DSR_SAD)
		dev_emerg(&imxdi->pdev->dev,
			  "%sSecure Controller Alarm Event\n",
			  dtcr & DTCR_SAIE ? "" : "Spurious ");

	if (dsr & DSR_EBD)
		dev_emerg(&imxdi->pdev->dev, "%sExternal Boot Tamper Event\n",
			  dtcr & DTCR_EBE ? "" : "Spurious ");

	if (dsr & DSR_ETAD)
		dev_emerg(&imxdi->pdev->dev, "%sExternal Tamper A Event\n",
			  dtcr & DTCR_ETAE ? "" : "Spurious ");

	if (dsr & DSR_ETBD)
		dev_emerg(&imxdi->pdev->dev, "%sExternal Tamper B Event\n",
			  dtcr & DTCR_ETBE ? "" : "Spurious ");

	if (dsr & DSR_WTD)
		dev_emerg(&imxdi->pdev->dev, "%sWire-mesh Tamper Event\n",
			  dtcr & DTCR_WTE ? "" : "Spurious ");

	if (dsr & DSR_MCO)
		dev_emerg(&imxdi->pdev->dev,
			  "%sMonotonic-counter Overflow Event\n",
			  dtcr & DTCR_MOE ? "" : "Spurious ");

	if (dsr & DSR_TCO)
		dev_emerg(&imxdi->pdev->dev, "%sTimer-counter Overflow Event\n",
			  dtcr & DTCR_TOE ? "" : "Spurious ");
}

static void di_what_is_to_be_done(struct imxdi_dev *imxdi,
				  const char *power_supply)
{
	dev_emerg(&imxdi->pdev->dev, "Please cycle the %s power supply in order to get the DryIce/RTC unit working again\n",
		  power_supply);
}

static int di_handle_failure_state(struct imxdi_dev *imxdi, u32 dsr)
{
	u32 dcr;

	dev_dbg(&imxdi->pdev->dev, "DSR register reports: %08X\n", dsr);

	/* report the cause */
	di_report_tamper_info(imxdi, dsr);

	dcr = readl(imxdi->ioaddr + DCR);

	if (dcr & DCR_FSHL) {
		/* we are out of luck */
		di_what_is_to_be_done(imxdi, "battery");
		return -ENODEV;
	}
	/*
	 * with the next SYSTEM POR we will transit from the "FAILURE STATE"
	 * into the "NON-VALID STATE" + "FAILURE STATE"
	 */
	di_what_is_to_be_done(imxdi, "main");

	return -ENODEV;
}

static int di_handle_valid_state(struct imxdi_dev *imxdi, u32 dsr)
{
	/* initialize alarm */
	di_write_busy_wait(imxdi, DCAMR_UNSET, DCAMR);
	di_write_busy_wait(imxdi, 0, DCALR);

	/* clear alarm flag */
	if (dsr & DSR_CAF)
		di_write_busy_wait(imxdi, DSR_CAF, DSR);

	return 0;
}

static int di_handle_invalid_state(struct imxdi_dev *imxdi, u32 dsr)
{
	u32 dcr, sec;

	/*
	 * lets disable all sources which can force the DryIce unit into
	 * the "FAILURE STATE" for now
	 */
	di_write_busy_wait(imxdi, 0x00000000, DTCR);
	/* and lets protect them at runtime from any change */
	di_write_busy_wait(imxdi, DCR_TDCSL, DCR);

	sec = readl(imxdi->ioaddr + DTCMR);
	if (sec != 0)
		dev_warn(&imxdi->pdev->dev,
			 "The security violation has happened at %u seconds\n",
			 sec);
	/*
	 * the timer cannot be set/modified if
	 * - the TCHL or TCSL bit is set in DCR
	 */
	dcr = readl(imxdi->ioaddr + DCR);
	if (!(dcr & DCR_TCE)) {
		if (dcr & DCR_TCHL) {
			/* we are out of luck */
			di_what_is_to_be_done(imxdi, "battery");
			return -ENODEV;
		}
		if (dcr & DCR_TCSL) {
			di_what_is_to_be_done(imxdi, "main");
			return -ENODEV;
		}
	}
	/*
	 * - the timer counter stops/is stopped if
	 *   - its overflow flag is set (TCO in DSR)
	 *      -> clear overflow bit to make it count again
	 *   - NVF is set in DSR
	 *      -> clear non-valid bit to make it count again
	 *   - its TCE (DCR) is cleared
	 *      -> set TCE to make it count
	 *   - it was never set before
	 *      -> write a time into it (required again if the NVF was set)
	 */
	/* state handled */
	di_write_busy_wait(imxdi, DSR_NVF, DSR);
	/* clear overflow flag */
	di_write_busy_wait(imxdi, DSR_TCO, DSR);
	/* enable the counter */
	di_write_busy_wait(imxdi, dcr | DCR_TCE, DCR);
	/* set and trigger it to make it count */
	di_write_busy_wait(imxdi, sec, DTCMR);

	/* now prepare for the valid state */
	return di_handle_valid_state(imxdi, __raw_readl(imxdi->ioaddr + DSR));
}

static int di_handle_invalid_and_failure_state(struct imxdi_dev *imxdi, u32 dsr)
{
	u32 dcr;

	/*
	 * now we must first remove the tamper sources in order to get the
	 * device out of the "FAILURE STATE"
	 * To disable any of the following sources we need to modify the DTCR
	 */
	if (dsr & (DSR_WTD | DSR_ETBD | DSR_ETAD | DSR_EBD | DSR_SAD |
			DSR_TTD | DSR_CTD | DSR_VTD | DSR_MCO | DSR_TCO)) {
		dcr = __raw_readl(imxdi->ioaddr + DCR);
		if (dcr & DCR_TDCHL) {
			/*
			 * the tamper register is locked. We cannot disable the
			 * tamper detection. The TDCHL can only be reset by a
			 * DRYICE POR, but we cannot force a DRYICE POR in
			 * softwere because we are still in "FAILURE STATE".
			 * We need a DRYICE POR via battery power cycling....
			 */
			/*
			 * out of luck!
			 * we cannot disable them without a DRYICE POR
			 */
			di_what_is_to_be_done(imxdi, "battery");
			return -ENODEV;
		}
		if (dcr & DCR_TDCSL) {
			/* a soft lock can be removed by a SYSTEM POR */
			di_what_is_to_be_done(imxdi, "main");
			return -ENODEV;
		}
	}

	/* disable all sources */
	di_write_busy_wait(imxdi, 0x00000000, DTCR);

	/* clear the status bits now */
	di_write_busy_wait(imxdi, dsr & (DSR_WTD | DSR_ETBD | DSR_ETAD |
			DSR_EBD | DSR_SAD | DSR_TTD | DSR_CTD | DSR_VTD |
			DSR_MCO | DSR_TCO), DSR);

	dsr = readl(imxdi->ioaddr + DSR);
	if ((dsr & ~(DSR_NVF | DSR_SVF | DSR_WBF | DSR_WNF |
			DSR_WCF | DSR_WEF)) != 0)
		dev_warn(&imxdi->pdev->dev,
			 "There are still some sources of pain in DSR: %08x!\n",
			 dsr & ~(DSR_NVF | DSR_SVF | DSR_WBF | DSR_WNF |
				 DSR_WCF | DSR_WEF));

	/*
	 * now we are trying to clear the "Security-violation flag" to
	 * get the DryIce out of this state
	 */
	di_write_busy_wait(imxdi, DSR_SVF, DSR);

	/* success? */
	dsr = readl(imxdi->ioaddr + DSR);
	if (dsr & DSR_SVF) {
		dev_crit(&imxdi->pdev->dev,
			 "Cannot clear the security violation flag. We are ending up in an endless loop!\n");
		/* last resort */
		di_what_is_to_be_done(imxdi, "battery");
		return -ENODEV;
	}

	/*
	 * now we have left the "FAILURE STATE" and ending up in the
	 * "NON-VALID STATE" time to recover everything
	 */
	return di_handle_invalid_state(imxdi, dsr);
}

static int di_handle_state(struct imxdi_dev *imxdi)
{
	int rc;
	u32 dsr;

	dsr = readl(imxdi->ioaddr + DSR);

	switch (dsr & (DSR_NVF | DSR_SVF)) {
	case DSR_NVF:
		dev_warn(&imxdi->pdev->dev, "Invalid stated unit detected\n");
		rc = di_handle_invalid_state(imxdi, dsr);
		break;
	case DSR_SVF:
		dev_warn(&imxdi->pdev->dev, "Failure stated unit detected\n");
		rc = di_handle_failure_state(imxdi, dsr);
		break;
	case DSR_NVF | DSR_SVF:
		dev_warn(&imxdi->pdev->dev,
			 "Failure+Invalid stated unit detected\n");
		rc = di_handle_invalid_and_failure_state(imxdi, dsr);
		break;
	default:
		dev_notice(&imxdi->pdev->dev, "Unlocked unit detected\n");
		rc = di_handle_valid_state(imxdi, dsr);
	}

	return rc;
}

/*
 * enable a dryice interrupt
 */
static void di_int_enable(struct imxdi_dev *imxdi, u32 intr)
{
	unsigned long flags;

	spin_lock_irqsave(&imxdi->irq_lock, flags);
	writel(readl(imxdi->ioaddr + DIER) | intr,
	       imxdi->ioaddr + DIER);
	spin_unlock_irqrestore(&imxdi->irq_lock, flags);
}

/*
 * disable a dryice interrupt
 */
static void di_int_disable(struct imxdi_dev *imxdi, u32 intr)
{
	unsigned long flags;

	spin_lock_irqsave(&imxdi->irq_lock, flags);
	writel(readl(imxdi->ioaddr + DIER) & ~intr,
	       imxdi->ioaddr + DIER);
	spin_unlock_irqrestore(&imxdi->irq_lock, flags);
}

/*
 * This function attempts to clear the dryice write-error flag.
 *
 * A dryice write error is similar to a bus fault and should not occur in
 * normal operation.  Clearing the flag requires another write, so the root
 * cause of the problem may need to be fixed before the flag can be cleared.
 */
static void clear_write_error(struct imxdi_dev *imxdi)
{
	int cnt;

	dev_warn(&imxdi->pdev->dev, "WARNING: Register write error!\n");

	/* clear the write error flag */
	writel(DSR_WEF, imxdi->ioaddr + DSR);

	/* wait for it to take effect */
	for (cnt = 0; cnt < 1000; cnt++) {
		if ((readl(imxdi->ioaddr + DSR) & DSR_WEF) == 0)
			return;
		udelay(10);
	}
	dev_err(&imxdi->pdev->dev,
			"ERROR: Cannot clear write-error flag!\n");
}

/*
 * Write a dryice register and wait until it completes.
 *
 * This function uses interrupts to determine when the
 * write has completed.
 */
static int di_write_wait(struct imxdi_dev *imxdi, u32 val, int reg)
{
	int ret;
	int rc = 0;

	/* serialize register writes */
	mutex_lock(&imxdi->write_mutex);

	/* enable the write-complete interrupt */
	di_int_enable(imxdi, DIER_WCIE);

	imxdi->dsr = 0;

	/* do the register write */
	writel(val, imxdi->ioaddr + reg);

	/* wait for the write to finish */
	ret = wait_event_interruptible_timeout(imxdi->write_wait,
			imxdi->dsr & (DSR_WCF | DSR_WEF), msecs_to_jiffies(1));
	if (ret < 0) {
		rc = ret;
		goto out;
	} else if (ret == 0) {
		dev_warn(&imxdi->pdev->dev,
				"Write-wait timeout "
				"val = 0x%08x reg = 0x%08x\n", val, reg);
	}

	/* check for write error */
	if (imxdi->dsr & DSR_WEF) {
		clear_write_error(imxdi);
		rc = -EIO;
	}

out:
	mutex_unlock(&imxdi->write_mutex);

	return rc;
}

/*
 * read the seconds portion of the current time from the dryice time counter
 */
static int dryice_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct imxdi_dev *imxdi = dev_get_drvdata(dev);
	unsigned long now;

	now = readl(imxdi->ioaddr + DTCMR);
	rtc_time_to_tm(now, tm);

	return 0;
}

/*
 * set the seconds portion of dryice time counter and clear the
 * fractional part.
 */
static int dryice_rtc_set_mmss(struct device *dev, unsigned long secs)
{
	struct imxdi_dev *imxdi = dev_get_drvdata(dev);
	u32 dcr, dsr;
	int rc;

	dcr = readl(imxdi->ioaddr + DCR);
	dsr = readl(imxdi->ioaddr + DSR);

	if (!(dcr & DCR_TCE) || (dsr & DSR_SVF)) {
		if (dcr & DCR_TCHL) {
			/* we are even more out of luck */
			di_what_is_to_be_done(imxdi, "battery");
			return -EPERM;
		}
		if ((dcr & DCR_TCSL) || (dsr & DSR_SVF)) {
			/* we are out of luck for now */
			di_what_is_to_be_done(imxdi, "main");
			return -EPERM;
		}
	}

	/* zero the fractional part first */
	rc = di_write_wait(imxdi, 0, DTCLR);
	if (rc != 0)
		return rc;

	rc = di_write_wait(imxdi, secs, DTCMR);
	if (rc != 0)
		return rc;

	return di_write_wait(imxdi, readl(imxdi->ioaddr + DCR) | DCR_TCE, DCR);
}

static int dryice_rtc_alarm_irq_enable(struct device *dev,
		unsigned int enabled)
{
	struct imxdi_dev *imxdi = dev_get_drvdata(dev);

	if (enabled)
		di_int_enable(imxdi, DIER_CAIE);
	else
		di_int_disable(imxdi, DIER_CAIE);

	return 0;
}

/*
 * read the seconds portion of the alarm register.
 * the fractional part of the alarm register is always zero.
 */
static int dryice_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alarm)
{
	struct imxdi_dev *imxdi = dev_get_drvdata(dev);
	u32 dcamr;

	dcamr = readl(imxdi->ioaddr + DCAMR);
	rtc_time_to_tm(dcamr, &alarm->time);

	/* alarm is enabled if the interrupt is enabled */
	alarm->enabled = (readl(imxdi->ioaddr + DIER) & DIER_CAIE) != 0;

	/* don't allow the DSR read to mess up DSR_WCF */
	mutex_lock(&imxdi->write_mutex);

	/* alarm is pending if the alarm flag is set */
	alarm->pending = (readl(imxdi->ioaddr + DSR) & DSR_CAF) != 0;

	mutex_unlock(&imxdi->write_mutex);

	return 0;
}

/*
 * set the seconds portion of dryice alarm register
 */
static int dryice_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alarm)
{
	struct imxdi_dev *imxdi = dev_get_drvdata(dev);
	unsigned long now;
	unsigned long alarm_time;
	int rc;

	rc = rtc_tm_to_time(&alarm->time, &alarm_time);
	if (rc)
		return rc;

	/* don't allow setting alarm in the past */
	now = readl(imxdi->ioaddr + DTCMR);
	if (alarm_time < now)
		return -EINVAL;

	/* write the new alarm time */
	rc = di_write_wait(imxdi, (u32)alarm_time, DCAMR);
	if (rc)
		return rc;

	if (alarm->enabled)
		di_int_enable(imxdi, DIER_CAIE);  /* enable alarm intr */
	else
		di_int_disable(imxdi, DIER_CAIE); /* disable alarm intr */

	return 0;
}

static const struct rtc_class_ops dryice_rtc_ops = {
	.read_time		= dryice_rtc_read_time,
	.set_mmss		= dryice_rtc_set_mmss,
	.alarm_irq_enable	= dryice_rtc_alarm_irq_enable,
	.read_alarm		= dryice_rtc_read_alarm,
	.set_alarm		= dryice_rtc_set_alarm,
};

/*
 * interrupt handler for dryice "normal" and security violation interrupt
 */
static irqreturn_t dryice_irq(int irq, void *dev_id)
{
	struct imxdi_dev *imxdi = dev_id;
	u32 dsr, dier;
	irqreturn_t rc = IRQ_NONE;

	dier = readl(imxdi->ioaddr + DIER);
	dsr = readl(imxdi->ioaddr + DSR);

	/* handle the security violation event */
	if (dier & DIER_SVIE) {
		if (dsr & DSR_SVF) {
			/*
			 * Disable the interrupt when this kind of event has
			 * happened.
			 * There cannot be more than one event of this type,
			 * because it needs a complex state change
			 * including a main power cycle to get again out of
			 * this state.
			 */
			di_int_disable(imxdi, DIER_SVIE);
			/* report the violation */
			di_report_tamper_info(imxdi, dsr);
			rc = IRQ_HANDLED;
		}
	}

	/* handle write complete and write error cases */
	if (dier & DIER_WCIE) {
		/*If the write wait queue is empty then there is no pending
		  operations. It means the interrupt is for DryIce -Security.
		  IRQ must be returned as none.*/
		if (list_empty_careful(&imxdi->write_wait.head))
			return rc;

		/* DSR_WCF clears itself on DSR read */
		if (dsr & (DSR_WCF | DSR_WEF)) {
			/* mask the interrupt */
			di_int_disable(imxdi, DIER_WCIE);

			/* save the dsr value for the wait queue */
			imxdi->dsr |= dsr;

			wake_up_interruptible(&imxdi->write_wait);
			rc = IRQ_HANDLED;
		}
	}

	/* handle the alarm case */
	if (dier & DIER_CAIE) {
		/* DSR_WCF clears itself on DSR read */
		if (dsr & DSR_CAF) {
			/* mask the interrupt */
			di_int_disable(imxdi, DIER_CAIE);

			/* finish alarm in user context */
			schedule_work(&imxdi->work);
			rc = IRQ_HANDLED;
		}
	}
	return rc;
}

/*
 * post the alarm event from user context so it can sleep
 * on the write completion.
 */
static void dryice_work(struct work_struct *work)
{
	struct imxdi_dev *imxdi = container_of(work,
			struct imxdi_dev, work);

	/* dismiss the interrupt (ignore error) */
	di_write_wait(imxdi, DSR_CAF, DSR);

	/* pass the alarm event to the rtc framework. */
	rtc_update_irq(imxdi->rtc, 1, RTC_AF | RTC_IRQF);
}

/*
 * probe for dryice rtc device
 */
static int __init dryice_rtc_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct imxdi_dev *imxdi;
	int norm_irq, sec_irq;
	int rc;

	imxdi = devm_kzalloc(&pdev->dev, sizeof(*imxdi), GFP_KERNEL);
	if (!imxdi)
		return -ENOMEM;

	imxdi->pdev = pdev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	imxdi->ioaddr = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(imxdi->ioaddr))
		return PTR_ERR(imxdi->ioaddr);

	spin_lock_init(&imxdi->irq_lock);

	norm_irq = platform_get_irq(pdev, 0);
	if (norm_irq < 0)
		return norm_irq;

	/* the 2nd irq is the security violation irq
	 * make this optional, don't break the device tree ABI
	 */
	sec_irq = platform_get_irq(pdev, 1);
	if (sec_irq <= 0)
		sec_irq = IRQ_NOTCONNECTED;

	init_waitqueue_head(&imxdi->write_wait);

	INIT_WORK(&imxdi->work, dryice_work);

	mutex_init(&imxdi->write_mutex);

	imxdi->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(imxdi->clk))
		return PTR_ERR(imxdi->clk);
	rc = clk_prepare_enable(imxdi->clk);
	if (rc)
		return rc;

	/*
	 * Initialize dryice hardware
	 */

	/* mask all interrupts */
	writel(0, imxdi->ioaddr + DIER);

	rc = di_handle_state(imxdi);
	if (rc != 0)
		goto err;

	rc = devm_request_irq(&pdev->dev, norm_irq, dryice_irq,
			      IRQF_SHARED, pdev->name, imxdi);
	if (rc) {
		dev_warn(&pdev->dev, "interrupt not available.\n");
		goto err;
	}

	rc = devm_request_irq(&pdev->dev, sec_irq, dryice_irq,
			      IRQF_SHARED, pdev->name, imxdi);
	if (rc) {
		dev_warn(&pdev->dev, "security violation interrupt not available.\n");
		/* this is not an error, see above */
	}

	platform_set_drvdata(pdev, imxdi);
	imxdi->rtc = devm_rtc_device_register(&pdev->dev, pdev->name,
				  &dryice_rtc_ops, THIS_MODULE);
	if (IS_ERR(imxdi->rtc)) {
		rc = PTR_ERR(imxdi->rtc);
		goto err;
	}

	return 0;

err:
	clk_disable_unprepare(imxdi->clk);

	return rc;
}

static int __exit dryice_rtc_remove(struct platform_device *pdev)
{
	struct imxdi_dev *imxdi = platform_get_drvdata(pdev);

	flush_work(&imxdi->work);

	/* mask all interrupts */
	writel(0, imxdi->ioaddr + DIER);

	clk_disable_unprepare(imxdi->clk);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id dryice_dt_ids[] = {
	{ .compatible = "fsl,imx25-rtc" },
	{ /* sentinel */ }
};

MODULE_DEVICE_TABLE(of, dryice_dt_ids);
#endif

static struct platform_driver dryice_rtc_driver = {
	.driver = {
		   .name = "imxdi_rtc",
		   .of_match_table = of_match_ptr(dryice_dt_ids),
		   },
	.remove = __exit_p(dryice_rtc_remove),
};

module_platform_driver_probe(dryice_rtc_driver, dryice_rtc_probe);

MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_AUTHOR("Baruch Siach <baruch@tkos.co.il>");
MODULE_DESCRIPTION("IMX DryIce Realtime Clock Driver (RTC)");
MODULE_LICENSE("GPL");
