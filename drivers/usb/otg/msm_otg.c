/* Copyright (c) 2009-2011, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 */

#include <linux/module.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/uaccess.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/pm_runtime.h>

#include <linux/usb.h>
#include <linux/usb/otg.h>
#include <linux/usb/ulpi.h>
#include <linux/usb/gadget.h>
#include <linux/usb/hcd.h>
#include <linux/usb/msm_hsusb.h>
#include <linux/usb/msm_hsusb_hw.h>
#include <linux/regulator/consumer.h>

#include <mach/clk.h>

#define MSM_USB_BASE	(motg->regs)
#define DRIVER_NAME	"msm_otg"

#define ULPI_IO_TIMEOUT_USEC	(10 * 1000)

#define USB_PHY_3P3_VOL_MIN	3050000 /* uV */
#define USB_PHY_3P3_VOL_MAX	3300000 /* uV */
#define USB_PHY_3P3_HPM_LOAD	50000	/* uA */
#define USB_PHY_3P3_LPM_LOAD	4000	/* uA */

#define USB_PHY_1P8_VOL_MIN	1800000 /* uV */
#define USB_PHY_1P8_VOL_MAX	1800000 /* uV */
#define USB_PHY_1P8_HPM_LOAD	50000	/* uA */
#define USB_PHY_1P8_LPM_LOAD	4000	/* uA */

#define USB_PHY_VDD_DIG_VOL_MIN	1000000 /* uV */
#define USB_PHY_VDD_DIG_VOL_MAX	1320000 /* uV */

static struct regulator *hsusb_3p3;
static struct regulator *hsusb_1p8;
static struct regulator *hsusb_vddcx;

static int msm_hsusb_init_vddcx(struct msm_otg *motg, int init)
{
	int ret = 0;

	if (init) {
		hsusb_vddcx = regulator_get(motg->otg.dev, "HSUSB_VDDCX");
		if (IS_ERR(hsusb_vddcx)) {
			dev_err(motg->otg.dev, "unable to get hsusb vddcx\n");
			return PTR_ERR(hsusb_vddcx);
		}

		ret = regulator_set_voltage(hsusb_vddcx,
				USB_PHY_VDD_DIG_VOL_MIN,
				USB_PHY_VDD_DIG_VOL_MAX);
		if (ret) {
			dev_err(motg->otg.dev, "unable to set the voltage "
					"for hsusb vddcx\n");
			regulator_put(hsusb_vddcx);
			return ret;
		}

		ret = regulator_enable(hsusb_vddcx);
		if (ret) {
			dev_err(motg->otg.dev, "unable to enable hsusb vddcx\n");
			regulator_put(hsusb_vddcx);
		}
	} else {
		ret = regulator_set_voltage(hsusb_vddcx, 0,
			USB_PHY_VDD_DIG_VOL_MAX);
		if (ret)
			dev_err(motg->otg.dev, "unable to set the voltage "
					"for hsusb vddcx\n");
		ret = regulator_disable(hsusb_vddcx);
		if (ret)
			dev_err(motg->otg.dev, "unable to disable hsusb vddcx\n");

		regulator_put(hsusb_vddcx);
	}

	return ret;
}

static int msm_hsusb_ldo_init(struct msm_otg *motg, int init)
{
	int rc = 0;

	if (init) {
		hsusb_3p3 = regulator_get(motg->otg.dev, "HSUSB_3p3");
		if (IS_ERR(hsusb_3p3)) {
			dev_err(motg->otg.dev, "unable to get hsusb 3p3\n");
			return PTR_ERR(hsusb_3p3);
		}

		rc = regulator_set_voltage(hsusb_3p3, USB_PHY_3P3_VOL_MIN,
				USB_PHY_3P3_VOL_MAX);
		if (rc) {
			dev_err(motg->otg.dev, "unable to set voltage level "
					"for hsusb 3p3\n");
			goto put_3p3;
		}
		rc = regulator_enable(hsusb_3p3);
		if (rc) {
			dev_err(motg->otg.dev, "unable to enable the hsusb 3p3\n");
			goto put_3p3;
		}
		hsusb_1p8 = regulator_get(motg->otg.dev, "HSUSB_1p8");
		if (IS_ERR(hsusb_1p8)) {
			dev_err(motg->otg.dev, "unable to get hsusb 1p8\n");
			rc = PTR_ERR(hsusb_1p8);
			goto disable_3p3;
		}
		rc = regulator_set_voltage(hsusb_1p8, USB_PHY_1P8_VOL_MIN,
				USB_PHY_1P8_VOL_MAX);
		if (rc) {
			dev_err(motg->otg.dev, "unable to set voltage level "
					"for hsusb 1p8\n");
			goto put_1p8;
		}
		rc = regulator_enable(hsusb_1p8);
		if (rc) {
			dev_err(motg->otg.dev, "unable to enable the hsusb 1p8\n");
			goto put_1p8;
		}

		return 0;
	}

	regulator_disable(hsusb_1p8);
put_1p8:
	regulator_put(hsusb_1p8);
disable_3p3:
	regulator_disable(hsusb_3p3);
put_3p3:
	regulator_put(hsusb_3p3);
	return rc;
}

#ifdef CONFIG_PM_SLEEP
#define USB_PHY_SUSP_DIG_VOL  500000
static int msm_hsusb_config_vddcx(int high)
{
	int max_vol = USB_PHY_VDD_DIG_VOL_MAX;
	int min_vol;
	int ret;

	if (high)
		min_vol = USB_PHY_VDD_DIG_VOL_MIN;
	else
		min_vol = USB_PHY_SUSP_DIG_VOL;

	ret = regulator_set_voltage(hsusb_vddcx, min_vol, max_vol);
	if (ret) {
		pr_err("%s: unable to set the voltage for regulator "
			"HSUSB_VDDCX\n", __func__);
		return ret;
	}

	pr_debug("%s: min_vol:%d max_vol:%d\n", __func__, min_vol, max_vol);

	return ret;
}
#endif

static int msm_hsusb_ldo_set_mode(int on)
{
	int ret = 0;

	if (!hsusb_1p8 || IS_ERR(hsusb_1p8)) {
		pr_err("%s: HSUSB_1p8 is not initialized\n", __func__);
		return -ENODEV;
	}

	if (!hsusb_3p3 || IS_ERR(hsusb_3p3)) {
		pr_err("%s: HSUSB_3p3 is not initialized\n", __func__);
		return -ENODEV;
	}

	if (on) {
		ret = regulator_set_optimum_mode(hsusb_1p8,
				USB_PHY_1P8_HPM_LOAD);
		if (ret < 0) {
			pr_err("%s: Unable to set HPM of the regulator "
				"HSUSB_1p8\n", __func__);
			return ret;
		}
		ret = regulator_set_optimum_mode(hsusb_3p3,
				USB_PHY_3P3_HPM_LOAD);
		if (ret < 0) {
			pr_err("%s: Unable to set HPM of the regulator "
				"HSUSB_3p3\n", __func__);
			regulator_set_optimum_mode(hsusb_1p8,
				USB_PHY_1P8_LPM_LOAD);
			return ret;
		}
	} else {
		ret = regulator_set_optimum_mode(hsusb_1p8,
				USB_PHY_1P8_LPM_LOAD);
		if (ret < 0)
			pr_err("%s: Unable to set LPM of the regulator "
				"HSUSB_1p8\n", __func__);
		ret = regulator_set_optimum_mode(hsusb_3p3,
				USB_PHY_3P3_LPM_LOAD);
		if (ret < 0)
			pr_err("%s: Unable to set LPM of the regulator "
				"HSUSB_3p3\n", __func__);
	}

	pr_debug("reg (%s)\n", on ? "HPM" : "LPM");
	return ret < 0 ? ret : 0;
}

static int ulpi_read(struct otg_transceiver *otg, u32 reg)
{
	struct msm_otg *motg = container_of(otg, struct msm_otg, otg);
	int cnt = 0;

	/* initiate read operation */
	writel(ULPI_RUN | ULPI_READ | ULPI_ADDR(reg),
	       USB_ULPI_VIEWPORT);

	/* wait for completion */
	while (cnt < ULPI_IO_TIMEOUT_USEC) {
		if (!(readl(USB_ULPI_VIEWPORT) & ULPI_RUN))
			break;
		udelay(1);
		cnt++;
	}

	if (cnt >= ULPI_IO_TIMEOUT_USEC) {
		dev_err(otg->dev, "ulpi_read: timeout %08x\n",
			readl(USB_ULPI_VIEWPORT));
		return -ETIMEDOUT;
	}
	return ULPI_DATA_READ(readl(USB_ULPI_VIEWPORT));
}

static int ulpi_write(struct otg_transceiver *otg, u32 val, u32 reg)
{
	struct msm_otg *motg = container_of(otg, struct msm_otg, otg);
	int cnt = 0;

	/* initiate write operation */
	writel(ULPI_RUN | ULPI_WRITE |
	       ULPI_ADDR(reg) | ULPI_DATA(val),
	       USB_ULPI_VIEWPORT);

	/* wait for completion */
	while (cnt < ULPI_IO_TIMEOUT_USEC) {
		if (!(readl(USB_ULPI_VIEWPORT) & ULPI_RUN))
			break;
		udelay(1);
		cnt++;
	}

	if (cnt >= ULPI_IO_TIMEOUT_USEC) {
		dev_err(otg->dev, "ulpi_write: timeout\n");
		return -ETIMEDOUT;
	}
	return 0;
}

static struct otg_io_access_ops msm_otg_io_ops = {
	.read = ulpi_read,
	.write = ulpi_write,
};

static void ulpi_init(struct msm_otg *motg)
{
	struct msm_otg_platform_data *pdata = motg->pdata;
	int *seq = pdata->phy_init_seq;

	if (!seq)
		return;

	while (seq[0] >= 0) {
		dev_vdbg(motg->otg.dev, "ulpi: write 0x%02x to 0x%02x\n",
				seq[0], seq[1]);
		ulpi_write(&motg->otg, seq[0], seq[1]);
		seq += 2;
	}
}

static int msm_otg_link_clk_reset(struct msm_otg *motg, bool assert)
{
	int ret;

	if (assert) {
		ret = clk_reset(motg->clk, CLK_RESET_ASSERT);
		if (ret)
			dev_err(motg->otg.dev, "usb hs_clk assert failed\n");
	} else {
		ret = clk_reset(motg->clk, CLK_RESET_DEASSERT);
		if (ret)
			dev_err(motg->otg.dev, "usb hs_clk deassert failed\n");
	}
	return ret;
}

static int msm_otg_phy_clk_reset(struct msm_otg *motg)
{
	int ret;

	ret = clk_reset(motg->phy_reset_clk, CLK_RESET_ASSERT);
	if (ret) {
		dev_err(motg->otg.dev, "usb phy clk assert failed\n");
		return ret;
	}
	usleep_range(10000, 12000);
	ret = clk_reset(motg->phy_reset_clk, CLK_RESET_DEASSERT);
	if (ret)
		dev_err(motg->otg.dev, "usb phy clk deassert failed\n");
	return ret;
}

static int msm_otg_phy_reset(struct msm_otg *motg)
{
	u32 val;
	int ret;
	int retries;

	ret = msm_otg_link_clk_reset(motg, 1);
	if (ret)
		return ret;
	ret = msm_otg_phy_clk_reset(motg);
	if (ret)
		return ret;
	ret = msm_otg_link_clk_reset(motg, 0);
	if (ret)
		return ret;

	val = readl(USB_PORTSC) & ~PORTSC_PTS_MASK;
	writel(val | PORTSC_PTS_ULPI, USB_PORTSC);

	for (retries = 3; retries > 0; retries--) {
		ret = ulpi_write(&motg->otg, ULPI_FUNC_CTRL_SUSPENDM,
				ULPI_CLR(ULPI_FUNC_CTRL));
		if (!ret)
			break;
		ret = msm_otg_phy_clk_reset(motg);
		if (ret)
			return ret;
	}
	if (!retries)
		return -ETIMEDOUT;

	/* This reset calibrates the phy, if the above write succeeded */
	ret = msm_otg_phy_clk_reset(motg);
	if (ret)
		return ret;

	for (retries = 3; retries > 0; retries--) {
		ret = ulpi_read(&motg->otg, ULPI_DEBUG);
		if (ret != -ETIMEDOUT)
			break;
		ret = msm_otg_phy_clk_reset(motg);
		if (ret)
			return ret;
	}
	if (!retries)
		return -ETIMEDOUT;

	dev_info(motg->otg.dev, "phy_reset: success\n");
	return 0;
}

#define LINK_RESET_TIMEOUT_USEC		(250 * 1000)
static int msm_otg_reset(struct otg_transceiver *otg)
{
	struct msm_otg *motg = container_of(otg, struct msm_otg, otg);
	struct msm_otg_platform_data *pdata = motg->pdata;
	int cnt = 0;
	int ret;
	u32 val = 0;
	u32 ulpi_val = 0;

	ret = msm_otg_phy_reset(motg);
	if (ret) {
		dev_err(otg->dev, "phy_reset failed\n");
		return ret;
	}

	ulpi_init(motg);

	writel(USBCMD_RESET, USB_USBCMD);
	while (cnt < LINK_RESET_TIMEOUT_USEC) {
		if (!(readl(USB_USBCMD) & USBCMD_RESET))
			break;
		udelay(1);
		cnt++;
	}
	if (cnt >= LINK_RESET_TIMEOUT_USEC)
		return -ETIMEDOUT;

	/* select ULPI phy */
	writel(0x80000000, USB_PORTSC);

	msleep(100);

	writel(0x0, USB_AHBBURST);
	writel(0x00, USB_AHBMODE);

	if (pdata->otg_control == OTG_PHY_CONTROL) {
		val = readl(USB_OTGSC);
		if (pdata->mode == USB_OTG) {
			ulpi_val = ULPI_INT_IDGRD | ULPI_INT_SESS_VALID;
			val |= OTGSC_IDIE | OTGSC_BSVIE;
		} else if (pdata->mode == USB_PERIPHERAL) {
			ulpi_val = ULPI_INT_SESS_VALID;
			val |= OTGSC_BSVIE;
		}
		writel(val, USB_OTGSC);
		ulpi_write(otg, ulpi_val, ULPI_USB_INT_EN_RISE);
		ulpi_write(otg, ulpi_val, ULPI_USB_INT_EN_FALL);
	}

	return 0;
}

#define PHY_SUSPEND_TIMEOUT_USEC	(500 * 1000)
#define PHY_RESUME_TIMEOUT_USEC	(100 * 1000)

#ifdef CONFIG_PM_SLEEP
static int msm_otg_suspend(struct msm_otg *motg)
{
	struct otg_transceiver *otg = &motg->otg;
	struct usb_bus *bus = otg->host;
	struct msm_otg_platform_data *pdata = motg->pdata;
	int cnt = 0;

	if (atomic_read(&motg->in_lpm))
		return 0;

	disable_irq(motg->irq);
	/*
	 * Chipidea 45-nm PHY suspend sequence:
	 *
	 * Interrupt Latch Register auto-clear feature is not present
	 * in all PHY versions. Latch register is clear on read type.
	 * Clear latch register to avoid spurious wakeup from
	 * low power mode (LPM).
	 *
	 * PHY comparators are disabled when PHY enters into low power
	 * mode (LPM). Keep PHY comparators ON in LPM only when we expect
	 * VBUS/Id notifications from USB PHY. Otherwise turn off USB
	 * PHY comparators. This save significant amount of power.
	 *
	 * PLL is not turned off when PHY enters into low power mode (LPM).
	 * Disable PLL for maximum power savings.
	 */

	if (motg->pdata->phy_type == CI_45NM_INTEGRATED_PHY) {
		ulpi_read(otg, 0x14);
		if (pdata->otg_control == OTG_PHY_CONTROL)
			ulpi_write(otg, 0x01, 0x30);
		ulpi_write(otg, 0x08, 0x09);
	}

	/*
	 * PHY may take some time or even fail to enter into low power
	 * mode (LPM). Hence poll for 500 msec and reset the PHY and link
	 * in failure case.
	 */
	writel(readl(USB_PORTSC) | PORTSC_PHCD, USB_PORTSC);
	while (cnt < PHY_SUSPEND_TIMEOUT_USEC) {
		if (readl(USB_PORTSC) & PORTSC_PHCD)
			break;
		udelay(1);
		cnt++;
	}

	if (cnt >= PHY_SUSPEND_TIMEOUT_USEC) {
		dev_err(otg->dev, "Unable to suspend PHY\n");
		msm_otg_reset(otg);
		enable_irq(motg->irq);
		return -ETIMEDOUT;
	}

	/*
	 * PHY has capability to generate interrupt asynchronously in low
	 * power mode (LPM). This interrupt is level triggered. So USB IRQ
	 * line must be disabled till async interrupt enable bit is cleared
	 * in USBCMD register. Assert STP (ULPI interface STOP signal) to
	 * block data communication from PHY.
	 */
	writel(readl(USB_USBCMD) | ASYNC_INTR_CTRL | ULPI_STP_CTRL, USB_USBCMD);

	if (motg->pdata->phy_type == SNPS_28NM_INTEGRATED_PHY &&
			motg->pdata->otg_control == OTG_PMIC_CONTROL)
		writel(readl(USB_PHY_CTRL) | PHY_RETEN, USB_PHY_CTRL);

	clk_disable(motg->pclk);
	clk_disable(motg->clk);
	if (motg->core_clk)
		clk_disable(motg->core_clk);

	if (!IS_ERR(motg->pclk_src))
		clk_disable(motg->pclk_src);

	if (motg->pdata->phy_type == SNPS_28NM_INTEGRATED_PHY &&
			motg->pdata->otg_control == OTG_PMIC_CONTROL) {
		msm_hsusb_ldo_set_mode(0);
		msm_hsusb_config_vddcx(0);
	}

	if (device_may_wakeup(otg->dev))
		enable_irq_wake(motg->irq);
	if (bus)
		clear_bit(HCD_FLAG_HW_ACCESSIBLE, &(bus_to_hcd(bus))->flags);

	atomic_set(&motg->in_lpm, 1);
	enable_irq(motg->irq);

	dev_info(otg->dev, "USB in low power mode\n");

	return 0;
}

static int msm_otg_resume(struct msm_otg *motg)
{
	struct otg_transceiver *otg = &motg->otg;
	struct usb_bus *bus = otg->host;
	int cnt = 0;
	unsigned temp;

	if (!atomic_read(&motg->in_lpm))
		return 0;

	if (!IS_ERR(motg->pclk_src))
		clk_enable(motg->pclk_src);

	clk_enable(motg->pclk);
	clk_enable(motg->clk);
	if (motg->core_clk)
		clk_enable(motg->core_clk);

	if (motg->pdata->phy_type == SNPS_28NM_INTEGRATED_PHY &&
			motg->pdata->otg_control == OTG_PMIC_CONTROL) {
		msm_hsusb_ldo_set_mode(1);
		msm_hsusb_config_vddcx(1);
		writel(readl(USB_PHY_CTRL) & ~PHY_RETEN, USB_PHY_CTRL);
	}

	temp = readl(USB_USBCMD);
	temp &= ~ASYNC_INTR_CTRL;
	temp &= ~ULPI_STP_CTRL;
	writel(temp, USB_USBCMD);

	/*
	 * PHY comes out of low power mode (LPM) in case of wakeup
	 * from asynchronous interrupt.
	 */
	if (!(readl(USB_PORTSC) & PORTSC_PHCD))
		goto skip_phy_resume;

	writel(readl(USB_PORTSC) & ~PORTSC_PHCD, USB_PORTSC);
	while (cnt < PHY_RESUME_TIMEOUT_USEC) {
		if (!(readl(USB_PORTSC) & PORTSC_PHCD))
			break;
		udelay(1);
		cnt++;
	}

	if (cnt >= PHY_RESUME_TIMEOUT_USEC) {
		/*
		 * This is a fatal error. Reset the link and
		 * PHY. USB state can not be restored. Re-insertion
		 * of USB cable is the only way to get USB working.
		 */
		dev_err(otg->dev, "Unable to resume USB."
				"Re-plugin the cable\n");
		msm_otg_reset(otg);
	}

skip_phy_resume:
	if (device_may_wakeup(otg->dev))
		disable_irq_wake(motg->irq);
	if (bus)
		set_bit(HCD_FLAG_HW_ACCESSIBLE, &(bus_to_hcd(bus))->flags);

	atomic_set(&motg->in_lpm, 0);

	if (motg->async_int) {
		motg->async_int = 0;
		pm_runtime_put(otg->dev);
		enable_irq(motg->irq);
	}

	dev_info(otg->dev, "USB exited from low power mode\n");

	return 0;
}
#endif

static void msm_otg_notify_charger(struct msm_otg *motg, unsigned mA)
{
	if (motg->cur_power == mA)
		return;

	/* TODO: Notify PMIC about available current */
	dev_info(motg->otg.dev, "Avail curr from USB = %u\n", mA);
	motg->cur_power = mA;
}

static int msm_otg_set_power(struct otg_transceiver *otg, unsigned mA)
{
	struct msm_otg *motg = container_of(otg, struct msm_otg, otg);

	/*
	 * Gadget driver uses set_power method to notify about the
	 * available current based on suspend/configured states.
	 *
	 * IDEV_CHG can be drawn irrespective of suspend/un-configured
	 * states when CDP/ACA is connected.
	 */
	if (motg->chg_type == USB_SDP_CHARGER)
		msm_otg_notify_charger(motg, mA);

	return 0;
}

static void msm_otg_start_host(struct otg_transceiver *otg, int on)
{
	struct msm_otg *motg = container_of(otg, struct msm_otg, otg);
	struct msm_otg_platform_data *pdata = motg->pdata;
	struct usb_hcd *hcd;

	if (!otg->host)
		return;

	hcd = bus_to_hcd(otg->host);

	if (on) {
		dev_dbg(otg->dev, "host on\n");

		if (pdata->vbus_power)
			pdata->vbus_power(1);
		/*
		 * Some boards have a switch cotrolled by gpio
		 * to enable/disable internal HUB. Enable internal
		 * HUB before kicking the host.
		 */
		if (pdata->setup_gpio)
			pdata->setup_gpio(OTG_STATE_A_HOST);
#ifdef CONFIG_USB
		usb_add_hcd(hcd, hcd->irq, IRQF_SHARED);
#endif
	} else {
		dev_dbg(otg->dev, "host off\n");

#ifdef CONFIG_USB
		usb_remove_hcd(hcd);
#endif
		if (pdata->setup_gpio)
			pdata->setup_gpio(OTG_STATE_UNDEFINED);
		if (pdata->vbus_power)
			pdata->vbus_power(0);
	}
}

static int msm_otg_set_host(struct otg_transceiver *otg, struct usb_bus *host)
{
	struct msm_otg *motg = container_of(otg, struct msm_otg, otg);
	struct usb_hcd *hcd;

	/*
	 * Fail host registration if this board can support
	 * only peripheral configuration.
	 */
	if (motg->pdata->mode == USB_PERIPHERAL) {
		dev_info(otg->dev, "Host mode is not supported\n");
		return -ENODEV;
	}

	if (!host) {
		if (otg->state == OTG_STATE_A_HOST) {
			pm_runtime_get_sync(otg->dev);
			msm_otg_start_host(otg, 0);
			otg->host = NULL;
			otg->state = OTG_STATE_UNDEFINED;
			schedule_work(&motg->sm_work);
		} else {
			otg->host = NULL;
		}

		return 0;
	}

	hcd = bus_to_hcd(host);
	hcd->power_budget = motg->pdata->power_budget;

	otg->host = host;
	dev_dbg(otg->dev, "host driver registered w/ tranceiver\n");

	/*
	 * Kick the state machine work, if peripheral is not supported
	 * or peripheral is already registered with us.
	 */
	if (motg->pdata->mode == USB_HOST || otg->gadget) {
		pm_runtime_get_sync(otg->dev);
		schedule_work(&motg->sm_work);
	}

	return 0;
}

static void msm_otg_start_peripheral(struct otg_transceiver *otg, int on)
{
	struct msm_otg *motg = container_of(otg, struct msm_otg, otg);
	struct msm_otg_platform_data *pdata = motg->pdata;

	if (!otg->gadget)
		return;

	if (on) {
		dev_dbg(otg->dev, "gadget on\n");
		/*
		 * Some boards have a switch cotrolled by gpio
		 * to enable/disable internal HUB. Disable internal
		 * HUB before kicking the gadget.
		 */
		if (pdata->setup_gpio)
			pdata->setup_gpio(OTG_STATE_B_PERIPHERAL);
		usb_gadget_vbus_connect(otg->gadget);
	} else {
		dev_dbg(otg->dev, "gadget off\n");
		usb_gadget_vbus_disconnect(otg->gadget);
		if (pdata->setup_gpio)
			pdata->setup_gpio(OTG_STATE_UNDEFINED);
	}

}

static int msm_otg_set_peripheral(struct otg_transceiver *otg,
			struct usb_gadget *gadget)
{
	struct msm_otg *motg = container_of(otg, struct msm_otg, otg);

	/*
	 * Fail peripheral registration if this board can support
	 * only host configuration.
	 */
	if (motg->pdata->mode == USB_HOST) {
		dev_info(otg->dev, "Peripheral mode is not supported\n");
		return -ENODEV;
	}

	if (!gadget) {
		if (otg->state == OTG_STATE_B_PERIPHERAL) {
			pm_runtime_get_sync(otg->dev);
			msm_otg_start_peripheral(otg, 0);
			otg->gadget = NULL;
			otg->state = OTG_STATE_UNDEFINED;
			schedule_work(&motg->sm_work);
		} else {
			otg->gadget = NULL;
		}

		return 0;
	}
	otg->gadget = gadget;
	dev_dbg(otg->dev, "peripheral driver registered w/ tranceiver\n");

	/*
	 * Kick the state machine work, if host is not supported
	 * or host is already registered with us.
	 */
	if (motg->pdata->mode == USB_PERIPHERAL || otg->host) {
		pm_runtime_get_sync(otg->dev);
		schedule_work(&motg->sm_work);
	}

	return 0;
}

static bool msm_chg_check_secondary_det(struct msm_otg *motg)
{
	struct otg_transceiver *otg = &motg->otg;
	u32 chg_det;
	bool ret = false;

	switch (motg->pdata->phy_type) {
	case CI_45NM_INTEGRATED_PHY:
		chg_det = ulpi_read(otg, 0x34);
		ret = chg_det & (1 << 4);
		break;
	case SNPS_28NM_INTEGRATED_PHY:
		chg_det = ulpi_read(otg, 0x87);
		ret = chg_det & 1;
		break;
	default:
		break;
	}
	return ret;
}

static void msm_chg_enable_secondary_det(struct msm_otg *motg)
{
	struct otg_transceiver *otg = &motg->otg;
	u32 chg_det;

	switch (motg->pdata->phy_type) {
	case CI_45NM_INTEGRATED_PHY:
		chg_det = ulpi_read(otg, 0x34);
		/* Turn off charger block */
		chg_det |= ~(1 << 1);
		ulpi_write(otg, chg_det, 0x34);
		udelay(20);
		/* control chg block via ULPI */
		chg_det &= ~(1 << 3);
		ulpi_write(otg, chg_det, 0x34);
		/* put it in host mode for enabling D- source */
		chg_det &= ~(1 << 2);
		ulpi_write(otg, chg_det, 0x34);
		/* Turn on chg detect block */
		chg_det &= ~(1 << 1);
		ulpi_write(otg, chg_det, 0x34);
		udelay(20);
		/* enable chg detection */
		chg_det &= ~(1 << 0);
		ulpi_write(otg, chg_det, 0x34);
		break;
	case SNPS_28NM_INTEGRATED_PHY:
		/*
		 * Configure DM as current source, DP as current sink
		 * and enable battery charging comparators.
		 */
		ulpi_write(otg, 0x8, 0x85);
		ulpi_write(otg, 0x2, 0x85);
		ulpi_write(otg, 0x1, 0x85);
		break;
	default:
		break;
	}
}

static bool msm_chg_check_primary_det(struct msm_otg *motg)
{
	struct otg_transceiver *otg = &motg->otg;
	u32 chg_det;
	bool ret = false;

	switch (motg->pdata->phy_type) {
	case CI_45NM_INTEGRATED_PHY:
		chg_det = ulpi_read(otg, 0x34);
		ret = chg_det & (1 << 4);
		break;
	case SNPS_28NM_INTEGRATED_PHY:
		chg_det = ulpi_read(otg, 0x87);
		ret = chg_det & 1;
		break;
	default:
		break;
	}
	return ret;
}

static void msm_chg_enable_primary_det(struct msm_otg *motg)
{
	struct otg_transceiver *otg = &motg->otg;
	u32 chg_det;

	switch (motg->pdata->phy_type) {
	case CI_45NM_INTEGRATED_PHY:
		chg_det = ulpi_read(otg, 0x34);
		/* enable chg detection */
		chg_det &= ~(1 << 0);
		ulpi_write(otg, chg_det, 0x34);
		break;
	case SNPS_28NM_INTEGRATED_PHY:
		/*
		 * Configure DP as current source, DM as current sink
		 * and enable battery charging comparators.
		 */
		ulpi_write(otg, 0x2, 0x85);
		ulpi_write(otg, 0x1, 0x85);
		break;
	default:
		break;
	}
}

static bool msm_chg_check_dcd(struct msm_otg *motg)
{
	struct otg_transceiver *otg = &motg->otg;
	u32 line_state;
	bool ret = false;

	switch (motg->pdata->phy_type) {
	case CI_45NM_INTEGRATED_PHY:
		line_state = ulpi_read(otg, 0x15);
		ret = !(line_state & 1);
		break;
	case SNPS_28NM_INTEGRATED_PHY:
		line_state = ulpi_read(otg, 0x87);
		ret = line_state & 2;
		break;
	default:
		break;
	}
	return ret;
}

static void msm_chg_disable_dcd(struct msm_otg *motg)
{
	struct otg_transceiver *otg = &motg->otg;
	u32 chg_det;

	switch (motg->pdata->phy_type) {
	case CI_45NM_INTEGRATED_PHY:
		chg_det = ulpi_read(otg, 0x34);
		chg_det &= ~(1 << 5);
		ulpi_write(otg, chg_det, 0x34);
		break;
	case SNPS_28NM_INTEGRATED_PHY:
		ulpi_write(otg, 0x10, 0x86);
		break;
	default:
		break;
	}
}

static void msm_chg_enable_dcd(struct msm_otg *motg)
{
	struct otg_transceiver *otg = &motg->otg;
	u32 chg_det;

	switch (motg->pdata->phy_type) {
	case CI_45NM_INTEGRATED_PHY:
		chg_det = ulpi_read(otg, 0x34);
		/* Turn on D+ current source */
		chg_det |= (1 << 5);
		ulpi_write(otg, chg_det, 0x34);
		break;
	case SNPS_28NM_INTEGRATED_PHY:
		/* Data contact detection enable */
		ulpi_write(otg, 0x10, 0x85);
		break;
	default:
		break;
	}
}

static void msm_chg_block_on(struct msm_otg *motg)
{
	struct otg_transceiver *otg = &motg->otg;
	u32 func_ctrl, chg_det;

	/* put the controller in non-driving mode */
	func_ctrl = ulpi_read(otg, ULPI_FUNC_CTRL);
	func_ctrl &= ~ULPI_FUNC_CTRL_OPMODE_MASK;
	func_ctrl |= ULPI_FUNC_CTRL_OPMODE_NONDRIVING;
	ulpi_write(otg, func_ctrl, ULPI_FUNC_CTRL);

	switch (motg->pdata->phy_type) {
	case CI_45NM_INTEGRATED_PHY:
		chg_det = ulpi_read(otg, 0x34);
		/* control chg block via ULPI */
		chg_det &= ~(1 << 3);
		ulpi_write(otg, chg_det, 0x34);
		/* Turn on chg detect block */
		chg_det &= ~(1 << 1);
		ulpi_write(otg, chg_det, 0x34);
		udelay(20);
		break;
	case SNPS_28NM_INTEGRATED_PHY:
		/* Clear charger detecting control bits */
		ulpi_write(otg, 0x3F, 0x86);
		/* Clear alt interrupt latch and enable bits */
		ulpi_write(otg, 0x1F, 0x92);
		ulpi_write(otg, 0x1F, 0x95);
		udelay(100);
		break;
	default:
		break;
	}
}

static void msm_chg_block_off(struct msm_otg *motg)
{
	struct otg_transceiver *otg = &motg->otg;
	u32 func_ctrl, chg_det;

	switch (motg->pdata->phy_type) {
	case CI_45NM_INTEGRATED_PHY:
		chg_det = ulpi_read(otg, 0x34);
		/* Turn off charger block */
		chg_det |= ~(1 << 1);
		ulpi_write(otg, chg_det, 0x34);
		break;
	case SNPS_28NM_INTEGRATED_PHY:
		/* Clear charger detecting control bits */
		ulpi_write(otg, 0x3F, 0x86);
		/* Clear alt interrupt latch and enable bits */
		ulpi_write(otg, 0x1F, 0x92);
		ulpi_write(otg, 0x1F, 0x95);
		break;
	default:
		break;
	}

	/* put the controller in normal mode */
	func_ctrl = ulpi_read(otg, ULPI_FUNC_CTRL);
	func_ctrl &= ~ULPI_FUNC_CTRL_OPMODE_MASK;
	func_ctrl |= ULPI_FUNC_CTRL_OPMODE_NORMAL;
	ulpi_write(otg, func_ctrl, ULPI_FUNC_CTRL);
}

#define MSM_CHG_DCD_POLL_TIME		(100 * HZ/1000) /* 100 msec */
#define MSM_CHG_DCD_MAX_RETRIES		6 /* Tdcd_tmout = 6 * 100 msec */
#define MSM_CHG_PRIMARY_DET_TIME	(40 * HZ/1000) /* TVDPSRC_ON */
#define MSM_CHG_SECONDARY_DET_TIME	(40 * HZ/1000) /* TVDMSRC_ON */
static void msm_chg_detect_work(struct work_struct *w)
{
	struct msm_otg *motg = container_of(w, struct msm_otg, chg_work.work);
	struct otg_transceiver *otg = &motg->otg;
	bool is_dcd, tmout, vout;
	unsigned long delay;

	dev_dbg(otg->dev, "chg detection work\n");
	switch (motg->chg_state) {
	case USB_CHG_STATE_UNDEFINED:
		pm_runtime_get_sync(otg->dev);
		msm_chg_block_on(motg);
		msm_chg_enable_dcd(motg);
		motg->chg_state = USB_CHG_STATE_WAIT_FOR_DCD;
		motg->dcd_retries = 0;
		delay = MSM_CHG_DCD_POLL_TIME;
		break;
	case USB_CHG_STATE_WAIT_FOR_DCD:
		is_dcd = msm_chg_check_dcd(motg);
		tmout = ++motg->dcd_retries == MSM_CHG_DCD_MAX_RETRIES;
		if (is_dcd || tmout) {
			msm_chg_disable_dcd(motg);
			msm_chg_enable_primary_det(motg);
			delay = MSM_CHG_PRIMARY_DET_TIME;
			motg->chg_state = USB_CHG_STATE_DCD_DONE;
		} else {
			delay = MSM_CHG_DCD_POLL_TIME;
		}
		break;
	case USB_CHG_STATE_DCD_DONE:
		vout = msm_chg_check_primary_det(motg);
		if (vout) {
			msm_chg_enable_secondary_det(motg);
			delay = MSM_CHG_SECONDARY_DET_TIME;
			motg->chg_state = USB_CHG_STATE_PRIMARY_DONE;
		} else {
			motg->chg_type = USB_SDP_CHARGER;
			motg->chg_state = USB_CHG_STATE_DETECTED;
			delay = 0;
		}
		break;
	case USB_CHG_STATE_PRIMARY_DONE:
		vout = msm_chg_check_secondary_det(motg);
		if (vout)
			motg->chg_type = USB_DCP_CHARGER;
		else
			motg->chg_type = USB_CDP_CHARGER;
		motg->chg_state = USB_CHG_STATE_SECONDARY_DONE;
		/* fall through */
	case USB_CHG_STATE_SECONDARY_DONE:
		motg->chg_state = USB_CHG_STATE_DETECTED;
	case USB_CHG_STATE_DETECTED:
		msm_chg_block_off(motg);
		dev_dbg(otg->dev, "charger = %d\n", motg->chg_type);
		schedule_work(&motg->sm_work);
		return;
	default:
		return;
	}

	schedule_delayed_work(&motg->chg_work, delay);
}

/*
 * We support OTG, Peripheral only and Host only configurations. In case
 * of OTG, mode switch (host-->peripheral/peripheral-->host) can happen
 * via Id pin status or user request (debugfs). Id/BSV interrupts are not
 * enabled when switch is controlled by user and default mode is supplied
 * by board file, which can be changed by userspace later.
 */
static void msm_otg_init_sm(struct msm_otg *motg)
{
	struct msm_otg_platform_data *pdata = motg->pdata;
	u32 otgsc = readl(USB_OTGSC);

	switch (pdata->mode) {
	case USB_OTG:
		if (pdata->otg_control == OTG_PHY_CONTROL) {
			if (otgsc & OTGSC_ID)
				set_bit(ID, &motg->inputs);
			else
				clear_bit(ID, &motg->inputs);

			if (otgsc & OTGSC_BSV)
				set_bit(B_SESS_VLD, &motg->inputs);
			else
				clear_bit(B_SESS_VLD, &motg->inputs);
		} else if (pdata->otg_control == OTG_USER_CONTROL) {
			if (pdata->default_mode == USB_HOST) {
				clear_bit(ID, &motg->inputs);
			} else if (pdata->default_mode == USB_PERIPHERAL) {
				set_bit(ID, &motg->inputs);
				set_bit(B_SESS_VLD, &motg->inputs);
			} else {
				set_bit(ID, &motg->inputs);
				clear_bit(B_SESS_VLD, &motg->inputs);
			}
		}
		break;
	case USB_HOST:
		clear_bit(ID, &motg->inputs);
		break;
	case USB_PERIPHERAL:
		set_bit(ID, &motg->inputs);
		if (otgsc & OTGSC_BSV)
			set_bit(B_SESS_VLD, &motg->inputs);
		else
			clear_bit(B_SESS_VLD, &motg->inputs);
		break;
	default:
		break;
	}
}

static void msm_otg_sm_work(struct work_struct *w)
{
	struct msm_otg *motg = container_of(w, struct msm_otg, sm_work);
	struct otg_transceiver *otg = &motg->otg;

	switch (otg->state) {
	case OTG_STATE_UNDEFINED:
		dev_dbg(otg->dev, "OTG_STATE_UNDEFINED state\n");
		msm_otg_reset(otg);
		msm_otg_init_sm(motg);
		otg->state = OTG_STATE_B_IDLE;
		/* FALL THROUGH */
	case OTG_STATE_B_IDLE:
		dev_dbg(otg->dev, "OTG_STATE_B_IDLE state\n");
		if (!test_bit(ID, &motg->inputs) && otg->host) {
			/* disable BSV bit */
			writel(readl(USB_OTGSC) & ~OTGSC_BSVIE, USB_OTGSC);
			msm_otg_start_host(otg, 1);
			otg->state = OTG_STATE_A_HOST;
		} else if (test_bit(B_SESS_VLD, &motg->inputs)) {
			switch (motg->chg_state) {
			case USB_CHG_STATE_UNDEFINED:
				msm_chg_detect_work(&motg->chg_work.work);
				break;
			case USB_CHG_STATE_DETECTED:
				switch (motg->chg_type) {
				case USB_DCP_CHARGER:
					msm_otg_notify_charger(motg,
							IDEV_CHG_MAX);
					break;
				case USB_CDP_CHARGER:
					msm_otg_notify_charger(motg,
							IDEV_CHG_MAX);
					msm_otg_start_peripheral(otg, 1);
					otg->state = OTG_STATE_B_PERIPHERAL;
					break;
				case USB_SDP_CHARGER:
					msm_otg_notify_charger(motg, IUNIT);
					msm_otg_start_peripheral(otg, 1);
					otg->state = OTG_STATE_B_PERIPHERAL;
					break;
				default:
					break;
				}
				break;
			default:
				break;
			}
		} else {
			/*
			 * If charger detection work is pending, decrement
			 * the pm usage counter to balance with the one that
			 * is incremented in charger detection work.
			 */
			if (cancel_delayed_work_sync(&motg->chg_work)) {
				pm_runtime_put_sync(otg->dev);
				msm_otg_reset(otg);
			}
			msm_otg_notify_charger(motg, 0);
			motg->chg_state = USB_CHG_STATE_UNDEFINED;
			motg->chg_type = USB_INVALID_CHARGER;
		}
		pm_runtime_put_sync(otg->dev);
		break;
	case OTG_STATE_B_PERIPHERAL:
		dev_dbg(otg->dev, "OTG_STATE_B_PERIPHERAL state\n");
		if (!test_bit(B_SESS_VLD, &motg->inputs) ||
				!test_bit(ID, &motg->inputs)) {
			msm_otg_notify_charger(motg, 0);
			msm_otg_start_peripheral(otg, 0);
			motg->chg_state = USB_CHG_STATE_UNDEFINED;
			motg->chg_type = USB_INVALID_CHARGER;
			otg->state = OTG_STATE_B_IDLE;
			msm_otg_reset(otg);
			schedule_work(w);
		}
		break;
	case OTG_STATE_A_HOST:
		dev_dbg(otg->dev, "OTG_STATE_A_HOST state\n");
		if (test_bit(ID, &motg->inputs)) {
			msm_otg_start_host(otg, 0);
			otg->state = OTG_STATE_B_IDLE;
			msm_otg_reset(otg);
			schedule_work(w);
		}
		break;
	default:
		break;
	}
}

static irqreturn_t msm_otg_irq(int irq, void *data)
{
	struct msm_otg *motg = data;
	struct otg_transceiver *otg = &motg->otg;
	u32 otgsc = 0;

	if (atomic_read(&motg->in_lpm)) {
		disable_irq_nosync(irq);
		motg->async_int = 1;
		pm_runtime_get(otg->dev);
		return IRQ_HANDLED;
	}

	otgsc = readl(USB_OTGSC);
	if (!(otgsc & (OTGSC_IDIS | OTGSC_BSVIS)))
		return IRQ_NONE;

	if ((otgsc & OTGSC_IDIS) && (otgsc & OTGSC_IDIE)) {
		if (otgsc & OTGSC_ID)
			set_bit(ID, &motg->inputs);
		else
			clear_bit(ID, &motg->inputs);
		dev_dbg(otg->dev, "ID set/clear\n");
		pm_runtime_get_noresume(otg->dev);
	} else if ((otgsc & OTGSC_BSVIS) && (otgsc & OTGSC_BSVIE)) {
		if (otgsc & OTGSC_BSV)
			set_bit(B_SESS_VLD, &motg->inputs);
		else
			clear_bit(B_SESS_VLD, &motg->inputs);
		dev_dbg(otg->dev, "BSV set/clear\n");
		pm_runtime_get_noresume(otg->dev);
	}

	writel(otgsc, USB_OTGSC);
	schedule_work(&motg->sm_work);
	return IRQ_HANDLED;
}

static int msm_otg_mode_show(struct seq_file *s, void *unused)
{
	struct msm_otg *motg = s->private;
	struct otg_transceiver *otg = &motg->otg;

	switch (otg->state) {
	case OTG_STATE_A_HOST:
		seq_printf(s, "host\n");
		break;
	case OTG_STATE_B_PERIPHERAL:
		seq_printf(s, "peripheral\n");
		break;
	default:
		seq_printf(s, "none\n");
		break;
	}

	return 0;
}

static int msm_otg_mode_open(struct inode *inode, struct file *file)
{
	return single_open(file, msm_otg_mode_show, inode->i_private);
}

static ssize_t msm_otg_mode_write(struct file *file, const char __user *ubuf,
				size_t count, loff_t *ppos)
{
	struct seq_file *s = file->private_data;
	struct msm_otg *motg = s->private;
	char buf[16];
	struct otg_transceiver *otg = &motg->otg;
	int status = count;
	enum usb_mode_type req_mode;

	memset(buf, 0x00, sizeof(buf));

	if (copy_from_user(&buf, ubuf, min_t(size_t, sizeof(buf) - 1, count))) {
		status = -EFAULT;
		goto out;
	}

	if (!strncmp(buf, "host", 4)) {
		req_mode = USB_HOST;
	} else if (!strncmp(buf, "peripheral", 10)) {
		req_mode = USB_PERIPHERAL;
	} else if (!strncmp(buf, "none", 4)) {
		req_mode = USB_NONE;
	} else {
		status = -EINVAL;
		goto out;
	}

	switch (req_mode) {
	case USB_NONE:
		switch (otg->state) {
		case OTG_STATE_A_HOST:
		case OTG_STATE_B_PERIPHERAL:
			set_bit(ID, &motg->inputs);
			clear_bit(B_SESS_VLD, &motg->inputs);
			break;
		default:
			goto out;
		}
		break;
	case USB_PERIPHERAL:
		switch (otg->state) {
		case OTG_STATE_B_IDLE:
		case OTG_STATE_A_HOST:
			set_bit(ID, &motg->inputs);
			set_bit(B_SESS_VLD, &motg->inputs);
			break;
		default:
			goto out;
		}
		break;
	case USB_HOST:
		switch (otg->state) {
		case OTG_STATE_B_IDLE:
		case OTG_STATE_B_PERIPHERAL:
			clear_bit(ID, &motg->inputs);
			break;
		default:
			goto out;
		}
		break;
	default:
		goto out;
	}

	pm_runtime_get_sync(otg->dev);
	schedule_work(&motg->sm_work);
out:
	return status;
}

const struct file_operations msm_otg_mode_fops = {
	.open = msm_otg_mode_open,
	.read = seq_read,
	.write = msm_otg_mode_write,
	.llseek = seq_lseek,
	.release = single_release,
};

static struct dentry *msm_otg_dbg_root;
static struct dentry *msm_otg_dbg_mode;

static int msm_otg_debugfs_init(struct msm_otg *motg)
{
	msm_otg_dbg_root = debugfs_create_dir("msm_otg", NULL);

	if (!msm_otg_dbg_root || IS_ERR(msm_otg_dbg_root))
		return -ENODEV;

	msm_otg_dbg_mode = debugfs_create_file("mode", S_IRUGO | S_IWUSR,
				msm_otg_dbg_root, motg, &msm_otg_mode_fops);
	if (!msm_otg_dbg_mode) {
		debugfs_remove(msm_otg_dbg_root);
		msm_otg_dbg_root = NULL;
		return -ENODEV;
	}

	return 0;
}

static void msm_otg_debugfs_cleanup(void)
{
	debugfs_remove(msm_otg_dbg_mode);
	debugfs_remove(msm_otg_dbg_root);
}

static int __init msm_otg_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct resource *res;
	struct msm_otg *motg;
	struct otg_transceiver *otg;

	dev_info(&pdev->dev, "msm_otg probe\n");
	if (!pdev->dev.platform_data) {
		dev_err(&pdev->dev, "No platform data given. Bailing out\n");
		return -ENODEV;
	}

	motg = kzalloc(sizeof(struct msm_otg), GFP_KERNEL);
	if (!motg) {
		dev_err(&pdev->dev, "unable to allocate msm_otg\n");
		return -ENOMEM;
	}

	motg->pdata = pdev->dev.platform_data;
	otg = &motg->otg;
	otg->dev = &pdev->dev;

	motg->phy_reset_clk = clk_get(&pdev->dev, "usb_phy_clk");
	if (IS_ERR(motg->phy_reset_clk)) {
		dev_err(&pdev->dev, "failed to get usb_phy_clk\n");
		ret = PTR_ERR(motg->phy_reset_clk);
		goto free_motg;
	}

	motg->clk = clk_get(&pdev->dev, "usb_hs_clk");
	if (IS_ERR(motg->clk)) {
		dev_err(&pdev->dev, "failed to get usb_hs_clk\n");
		ret = PTR_ERR(motg->clk);
		goto put_phy_reset_clk;
	}
	clk_set_rate(motg->clk, 60000000);

	/*
	 * If USB Core is running its protocol engine based on CORE CLK,
	 * CORE CLK  must be running at >55Mhz for correct HSUSB
	 * operation and USB core cannot tolerate frequency changes on
	 * CORE CLK. For such USB cores, vote for maximum clk frequency
	 * on pclk source
	 */
	 if (motg->pdata->pclk_src_name) {
		motg->pclk_src = clk_get(&pdev->dev,
			motg->pdata->pclk_src_name);
		if (IS_ERR(motg->pclk_src))
			goto put_clk;
		clk_set_rate(motg->pclk_src, INT_MAX);
		clk_enable(motg->pclk_src);
	} else
		motg->pclk_src = ERR_PTR(-ENOENT);


	motg->pclk = clk_get(&pdev->dev, "usb_hs_pclk");
	if (IS_ERR(motg->pclk)) {
		dev_err(&pdev->dev, "failed to get usb_hs_pclk\n");
		ret = PTR_ERR(motg->pclk);
		goto put_pclk_src;
	}

	/*
	 * USB core clock is not present on all MSM chips. This
	 * clock is introduced to remove the dependency on AXI
	 * bus frequency.
	 */
	motg->core_clk = clk_get(&pdev->dev, "usb_hs_core_clk");
	if (IS_ERR(motg->core_clk))
		motg->core_clk = NULL;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "failed to get platform resource mem\n");
		ret = -ENODEV;
		goto put_core_clk;
	}

	motg->regs = ioremap(res->start, resource_size(res));
	if (!motg->regs) {
		dev_err(&pdev->dev, "ioremap failed\n");
		ret = -ENOMEM;
		goto put_core_clk;
	}
	dev_info(&pdev->dev, "OTG regs = %p\n", motg->regs);

	motg->irq = platform_get_irq(pdev, 0);
	if (!motg->irq) {
		dev_err(&pdev->dev, "platform_get_irq failed\n");
		ret = -ENODEV;
		goto free_regs;
	}

	clk_enable(motg->clk);
	clk_enable(motg->pclk);

	ret = msm_hsusb_init_vddcx(motg, 1);
	if (ret) {
		dev_err(&pdev->dev, "hsusb vddcx configuration failed\n");
		goto free_regs;
	}

	ret = msm_hsusb_ldo_init(motg, 1);
	if (ret) {
		dev_err(&pdev->dev, "hsusb vreg configuration failed\n");
		goto vddcx_exit;
	}
	ret = msm_hsusb_ldo_set_mode(1);
	if (ret) {
		dev_err(&pdev->dev, "hsusb vreg enable failed\n");
		goto ldo_exit;
	}

	if (motg->core_clk)
		clk_enable(motg->core_clk);

	writel(0, USB_USBINTR);
	writel(0, USB_OTGSC);

	INIT_WORK(&motg->sm_work, msm_otg_sm_work);
	INIT_DELAYED_WORK(&motg->chg_work, msm_chg_detect_work);
	ret = request_irq(motg->irq, msm_otg_irq, IRQF_SHARED,
					"msm_otg", motg);
	if (ret) {
		dev_err(&pdev->dev, "request irq failed\n");
		goto disable_clks;
	}

	otg->init = msm_otg_reset;
	otg->set_host = msm_otg_set_host;
	otg->set_peripheral = msm_otg_set_peripheral;
	otg->set_power = msm_otg_set_power;

	otg->io_ops = &msm_otg_io_ops;

	ret = otg_set_transceiver(&motg->otg);
	if (ret) {
		dev_err(&pdev->dev, "otg_set_transceiver failed\n");
		goto free_irq;
	}

	platform_set_drvdata(pdev, motg);
	device_init_wakeup(&pdev->dev, 1);

	if (motg->pdata->mode == USB_OTG &&
			motg->pdata->otg_control == OTG_USER_CONTROL) {
		ret = msm_otg_debugfs_init(motg);
		if (ret)
			dev_dbg(&pdev->dev, "mode debugfs file is"
					"not available\n");
	}

	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	return 0;
free_irq:
	free_irq(motg->irq, motg);
disable_clks:
	clk_disable(motg->pclk);
	clk_disable(motg->clk);
ldo_exit:
	msm_hsusb_ldo_init(motg, 0);
vddcx_exit:
	msm_hsusb_init_vddcx(motg, 0);
free_regs:
	iounmap(motg->regs);
put_core_clk:
	if (motg->core_clk)
		clk_put(motg->core_clk);
	clk_put(motg->pclk);
put_pclk_src:
	if (!IS_ERR(motg->pclk_src)) {
		clk_disable(motg->pclk_src);
		clk_put(motg->pclk_src);
	}
put_clk:
	clk_put(motg->clk);
put_phy_reset_clk:
	clk_put(motg->phy_reset_clk);
free_motg:
	kfree(motg);
	return ret;
}

static int __devexit msm_otg_remove(struct platform_device *pdev)
{
	struct msm_otg *motg = platform_get_drvdata(pdev);
	struct otg_transceiver *otg = &motg->otg;
	int cnt = 0;

	if (otg->host || otg->gadget)
		return -EBUSY;

	msm_otg_debugfs_cleanup();
	cancel_delayed_work_sync(&motg->chg_work);
	cancel_work_sync(&motg->sm_work);

	pm_runtime_resume(&pdev->dev);

	device_init_wakeup(&pdev->dev, 0);
	pm_runtime_disable(&pdev->dev);

	otg_set_transceiver(NULL);
	free_irq(motg->irq, motg);

	/*
	 * Put PHY in low power mode.
	 */
	ulpi_read(otg, 0x14);
	ulpi_write(otg, 0x08, 0x09);

	writel(readl(USB_PORTSC) | PORTSC_PHCD, USB_PORTSC);
	while (cnt < PHY_SUSPEND_TIMEOUT_USEC) {
		if (readl(USB_PORTSC) & PORTSC_PHCD)
			break;
		udelay(1);
		cnt++;
	}
	if (cnt >= PHY_SUSPEND_TIMEOUT_USEC)
		dev_err(otg->dev, "Unable to suspend PHY\n");

	clk_disable(motg->pclk);
	clk_disable(motg->clk);
	if (motg->core_clk)
		clk_disable(motg->core_clk);
	if (!IS_ERR(motg->pclk_src)) {
		clk_disable(motg->pclk_src);
		clk_put(motg->pclk_src);
	}
	msm_hsusb_ldo_init(motg, 0);

	iounmap(motg->regs);
	pm_runtime_set_suspended(&pdev->dev);

	clk_put(motg->phy_reset_clk);
	clk_put(motg->pclk);
	clk_put(motg->clk);
	if (motg->core_clk)
		clk_put(motg->core_clk);

	kfree(motg);

	return 0;
}

#ifdef CONFIG_PM_RUNTIME
static int msm_otg_runtime_idle(struct device *dev)
{
	struct msm_otg *motg = dev_get_drvdata(dev);
	struct otg_transceiver *otg = &motg->otg;

	dev_dbg(dev, "OTG runtime idle\n");

	/*
	 * It is observed some times that a spurious interrupt
	 * comes when PHY is put into LPM immediately after PHY reset.
	 * This 1 sec delay also prevents entering into LPM immediately
	 * after asynchronous interrupt.
	 */
	if (otg->state != OTG_STATE_UNDEFINED)
		pm_schedule_suspend(dev, 1000);

	return -EAGAIN;
}

static int msm_otg_runtime_suspend(struct device *dev)
{
	struct msm_otg *motg = dev_get_drvdata(dev);

	dev_dbg(dev, "OTG runtime suspend\n");
	return msm_otg_suspend(motg);
}

static int msm_otg_runtime_resume(struct device *dev)
{
	struct msm_otg *motg = dev_get_drvdata(dev);

	dev_dbg(dev, "OTG runtime resume\n");
	return msm_otg_resume(motg);
}
#endif

#ifdef CONFIG_PM_SLEEP
static int msm_otg_pm_suspend(struct device *dev)
{
	struct msm_otg *motg = dev_get_drvdata(dev);

	dev_dbg(dev, "OTG PM suspend\n");
	return msm_otg_suspend(motg);
}

static int msm_otg_pm_resume(struct device *dev)
{
	struct msm_otg *motg = dev_get_drvdata(dev);
	int ret;

	dev_dbg(dev, "OTG PM resume\n");

	ret = msm_otg_resume(motg);
	if (ret)
		return ret;

	/*
	 * Runtime PM Documentation recommends bringing the
	 * device to full powered state upon resume.
	 */
	pm_runtime_disable(dev);
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);

	return 0;
}
#endif

#ifdef CONFIG_PM
static const struct dev_pm_ops msm_otg_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(msm_otg_pm_suspend, msm_otg_pm_resume)
	SET_RUNTIME_PM_OPS(msm_otg_runtime_suspend, msm_otg_runtime_resume,
				msm_otg_runtime_idle)
};
#endif

static struct platform_driver msm_otg_driver = {
	.remove = __devexit_p(msm_otg_remove),
	.driver = {
		.name = DRIVER_NAME,
		.owner = THIS_MODULE,
#ifdef CONFIG_PM
		.pm = &msm_otg_dev_pm_ops,
#endif
	},
};

static int __init msm_otg_init(void)
{
	return platform_driver_probe(&msm_otg_driver, msm_otg_probe);
}

static void __exit msm_otg_exit(void)
{
	platform_driver_unregister(&msm_otg_driver);
}

module_init(msm_otg_init);
module_exit(msm_otg_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("MSM USB transceiver driver");
