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
#include <linux/extcon.h>
#include <linux/gpio/consumer.h>
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
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/reboot.h>
#include <linux/reset.h>
#include <linux/types.h>
#include <linux/usb/otg.h>

#include <linux/usb.h>
#include <linux/usb/otg.h>
#include <linux/usb/of.h>
#include <linux/usb/ulpi.h>
#include <linux/usb/gadget.h>
#include <linux/usb/hcd.h>
#include <linux/usb/msm_hsusb_hw.h>
#include <linux/regulator/consumer.h>

/**
 * OTG control
 *
 * OTG_NO_CONTROL	Id/VBUS notifications not required. Useful in host
 *                      only configuration.
 * OTG_PHY_CONTROL	Id/VBUS notifications comes form USB PHY.
 * OTG_PMIC_CONTROL	Id/VBUS notifications comes from PMIC hardware.
 * OTG_USER_CONTROL	Id/VBUS notifcations comes from User via sysfs.
 *
 */
enum otg_control_type {
	OTG_NO_CONTROL = 0,
	OTG_PHY_CONTROL,
	OTG_PMIC_CONTROL,
	OTG_USER_CONTROL,
};

/**
 * PHY used in
 *
 * INVALID_PHY			Unsupported PHY
 * CI_45NM_INTEGRATED_PHY	Chipidea 45nm integrated PHY
 * SNPS_28NM_INTEGRATED_PHY	Synopsis 28nm integrated PHY
 *
 */
enum msm_usb_phy_type {
	INVALID_PHY = 0,
	CI_45NM_INTEGRATED_PHY,
	SNPS_28NM_INTEGRATED_PHY,
};

#define IDEV_CHG_MAX	1500
#define IUNIT		100

/**
 * Different states involved in USB charger detection.
 *
 * USB_CHG_STATE_UNDEFINED	USB charger is not connected or detection
 *                              process is not yet started.
 * USB_CHG_STATE_WAIT_FOR_DCD	Waiting for Data pins contact.
 * USB_CHG_STATE_DCD_DONE	Data pin contact is detected.
 * USB_CHG_STATE_PRIMARY_DONE	Primary detection is completed (Detects
 *                              between SDP and DCP/CDP).
 * USB_CHG_STATE_SECONDARY_DONE	Secondary detection is completed (Detects
 *                              between DCP and CDP).
 * USB_CHG_STATE_DETECTED	USB charger type is determined.
 *
 */
enum usb_chg_state {
	USB_CHG_STATE_UNDEFINED = 0,
	USB_CHG_STATE_WAIT_FOR_DCD,
	USB_CHG_STATE_DCD_DONE,
	USB_CHG_STATE_PRIMARY_DONE,
	USB_CHG_STATE_SECONDARY_DONE,
	USB_CHG_STATE_DETECTED,
};

/**
 * USB charger types
 *
 * USB_INVALID_CHARGER	Invalid USB charger.
 * USB_SDP_CHARGER	Standard downstream port. Refers to a downstream port
 *                      on USB2.0 compliant host/hub.
 * USB_DCP_CHARGER	Dedicated charger port (AC charger/ Wall charger).
 * USB_CDP_CHARGER	Charging downstream port. Enumeration can happen and
 *                      IDEV_CHG_MAX can be drawn irrespective of USB state.
 *
 */
enum usb_chg_type {
	USB_INVALID_CHARGER = 0,
	USB_SDP_CHARGER,
	USB_DCP_CHARGER,
	USB_CDP_CHARGER,
};

/**
 * struct msm_otg_platform_data - platform device data
 *              for msm_otg driver.
 * @phy_init_seq: PHY configuration sequence values. Value of -1 is reserved as
 *              "do not overwrite default vaule at this address".
 * @phy_init_sz: PHY configuration sequence size.
 * @vbus_power: VBUS power on/off routine.
 * @power_budget: VBUS power budget in mA (0 will be treated as 500mA).
 * @mode: Supported mode (OTG/peripheral/host).
 * @otg_control: OTG switch controlled by user/Id pin
 */
struct msm_otg_platform_data {
	int *phy_init_seq;
	int phy_init_sz;
	void (*vbus_power)(bool on);
	unsigned power_budget;
	enum usb_dr_mode mode;
	enum otg_control_type otg_control;
	enum msm_usb_phy_type phy_type;
	void (*setup_gpio)(enum usb_otg_state state);
};

/**
 * struct msm_otg: OTG driver data. Shared by HCD and DCD.
 * @otg: USB OTG Transceiver structure.
 * @pdata: otg device platform data.
 * @irq: IRQ number assigned for HSUSB controller.
 * @clk: clock struct of usb_hs_clk.
 * @pclk: clock struct of usb_hs_pclk.
 * @core_clk: clock struct of usb_hs_core_clk.
 * @regs: ioremapped register base address.
 * @inputs: OTG state machine inputs(Id, SessValid etc).
 * @sm_work: OTG state machine work.
 * @in_lpm: indicates low power mode (LPM) state.
 * @async_int: Async interrupt arrived.
 * @cur_power: The amount of mA available from downstream port.
 * @chg_work: Charger detection work.
 * @chg_state: The state of charger detection process.
 * @chg_type: The type of charger attached.
 * @dcd_retires: The retry count used to track Data contact
 *               detection process.
 * @manual_pullup: true if VBUS is not routed to USB controller/phy
 *	and controller driver therefore enables pull-up explicitly before
 *	starting controller using usbcmd run/stop bit.
 * @vbus: VBUS signal state trakining, using extcon framework
 * @id: ID signal state trakining, using extcon framework
 * @switch_gpio: Descriptor for GPIO used to control external Dual
 *               SPDT USB Switch.
 * @reboot: Used to inform the driver to route USB D+/D- line to Device
 *	    connector
 */
struct msm_otg {
	struct usb_phy phy;
	struct msm_otg_platform_data *pdata;
	int irq;
	struct clk *clk;
	struct clk *pclk;
	struct clk *core_clk;
	void __iomem *regs;
#define ID		0
#define B_SESS_VLD	1
	unsigned long inputs;
	struct work_struct sm_work;
	atomic_t in_lpm;
	int async_int;
	unsigned cur_power;
	int phy_number;
	struct delayed_work chg_work;
	enum usb_chg_state chg_state;
	enum usb_chg_type chg_type;
	u8 dcd_retries;
	struct regulator *v3p3;
	struct regulator *v1p8;
	struct regulator *vddcx;

	struct reset_control *phy_rst;
	struct reset_control *link_rst;
	int vdd_levels[3];

	bool manual_pullup;

	struct gpio_desc *switch_gpio;
	struct notifier_block reboot;
};

#define MSM_USB_BASE	(motg->regs)
#define DRIVER_NAME	"msm_otg"

#define ULPI_IO_TIMEOUT_USEC	(10 * 1000)
#define LINK_RESET_TIMEOUT_USEC	(250 * 1000)

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
#define USB_PHY_SUSP_DIG_VOL	500000  /* uV */

enum vdd_levels {
	VDD_LEVEL_NONE = 0,
	VDD_LEVEL_MIN,
	VDD_LEVEL_MAX,
};

static int msm_hsusb_init_vddcx(struct msm_otg *motg, int init)
{
	int ret = 0;

	if (init) {
		ret = regulator_set_voltage(motg->vddcx,
				motg->vdd_levels[VDD_LEVEL_MIN],
				motg->vdd_levels[VDD_LEVEL_MAX]);
		if (ret) {
			dev_err(motg->phy.dev, "Cannot set vddcx voltage\n");
			return ret;
		}

		ret = regulator_enable(motg->vddcx);
		if (ret)
			dev_err(motg->phy.dev, "unable to enable hsusb vddcx\n");
	} else {
		ret = regulator_set_voltage(motg->vddcx, 0,
				motg->vdd_levels[VDD_LEVEL_MAX]);
		if (ret)
			dev_err(motg->phy.dev, "Cannot set vddcx voltage\n");
		ret = regulator_disable(motg->vddcx);
		if (ret)
			dev_err(motg->phy.dev, "unable to disable hsusb vddcx\n");
	}

	return ret;
}

static int msm_hsusb_ldo_init(struct msm_otg *motg, int init)
{
	int rc = 0;

	if (init) {
		rc = regulator_set_voltage(motg->v3p3, USB_PHY_3P3_VOL_MIN,
				USB_PHY_3P3_VOL_MAX);
		if (rc) {
			dev_err(motg->phy.dev, "Cannot set v3p3 voltage\n");
			goto exit;
		}
		rc = regulator_enable(motg->v3p3);
		if (rc) {
			dev_err(motg->phy.dev, "unable to enable the hsusb 3p3\n");
			goto exit;
		}
		rc = regulator_set_voltage(motg->v1p8, USB_PHY_1P8_VOL_MIN,
				USB_PHY_1P8_VOL_MAX);
		if (rc) {
			dev_err(motg->phy.dev, "Cannot set v1p8 voltage\n");
			goto disable_3p3;
		}
		rc = regulator_enable(motg->v1p8);
		if (rc) {
			dev_err(motg->phy.dev, "unable to enable the hsusb 1p8\n");
			goto disable_3p3;
		}

		return 0;
	}

	regulator_disable(motg->v1p8);
disable_3p3:
	regulator_disable(motg->v3p3);
exit:
	return rc;
}

static int msm_hsusb_ldo_set_mode(struct msm_otg *motg, int on)
{
	int ret = 0;

	if (on) {
		ret = regulator_set_load(motg->v1p8, USB_PHY_1P8_HPM_LOAD);
		if (ret < 0) {
			pr_err("Could not set HPM for v1p8\n");
			return ret;
		}
		ret = regulator_set_load(motg->v3p3, USB_PHY_3P3_HPM_LOAD);
		if (ret < 0) {
			pr_err("Could not set HPM for v3p3\n");
			regulator_set_load(motg->v1p8, USB_PHY_1P8_LPM_LOAD);
			return ret;
		}
	} else {
		ret = regulator_set_load(motg->v1p8, USB_PHY_1P8_LPM_LOAD);
		if (ret < 0)
			pr_err("Could not set LPM for v1p8\n");
		ret = regulator_set_load(motg->v3p3, USB_PHY_3P3_LPM_LOAD);
		if (ret < 0)
			pr_err("Could not set LPM for v3p3\n");
	}

	pr_debug("reg (%s)\n", on ? "HPM" : "LPM");
	return ret < 0 ? ret : 0;
}

static int ulpi_read(struct usb_phy *phy, u32 reg)
{
	struct msm_otg *motg = container_of(phy, struct msm_otg, phy);
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
		dev_err(phy->dev, "ulpi_read: timeout %08x\n",
			readl(USB_ULPI_VIEWPORT));
		return -ETIMEDOUT;
	}
	return ULPI_DATA_READ(readl(USB_ULPI_VIEWPORT));
}

static int ulpi_write(struct usb_phy *phy, u32 val, u32 reg)
{
	struct msm_otg *motg = container_of(phy, struct msm_otg, phy);
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
		dev_err(phy->dev, "ulpi_write: timeout\n");
		return -ETIMEDOUT;
	}
	return 0;
}

static struct usb_phy_io_ops msm_otg_io_ops = {
	.read = ulpi_read,
	.write = ulpi_write,
};

static void ulpi_init(struct msm_otg *motg)
{
	struct msm_otg_platform_data *pdata = motg->pdata;
	int *seq = pdata->phy_init_seq, idx;
	u32 addr = ULPI_EXT_VENDOR_SPECIFIC;

	for (idx = 0; idx < pdata->phy_init_sz; idx++) {
		if (seq[idx] == -1)
			continue;

		dev_vdbg(motg->phy.dev, "ulpi: write 0x%02x to 0x%02x\n",
				seq[idx], addr + idx);
		ulpi_write(&motg->phy, seq[idx], addr + idx);
	}
}

static int msm_phy_notify_disconnect(struct usb_phy *phy,
				   enum usb_device_speed speed)
{
	struct msm_otg *motg = container_of(phy, struct msm_otg, phy);
	int val;

	if (motg->manual_pullup) {
		val = ULPI_MISC_A_VBUSVLDEXT | ULPI_MISC_A_VBUSVLDEXTSEL;
		usb_phy_io_write(phy, val, ULPI_CLR(ULPI_MISC_A));
	}

	/*
	 * Put the transceiver in non-driving mode. Otherwise host
	 * may not detect soft-disconnection.
	 */
	val = ulpi_read(phy, ULPI_FUNC_CTRL);
	val &= ~ULPI_FUNC_CTRL_OPMODE_MASK;
	val |= ULPI_FUNC_CTRL_OPMODE_NONDRIVING;
	ulpi_write(phy, val, ULPI_FUNC_CTRL);

	return 0;
}

static int msm_otg_link_clk_reset(struct msm_otg *motg, bool assert)
{
	int ret;

	if (assert)
		ret = reset_control_assert(motg->link_rst);
	else
		ret = reset_control_deassert(motg->link_rst);

	if (ret)
		dev_err(motg->phy.dev, "usb link clk reset %s failed\n",
			assert ? "assert" : "deassert");

	return ret;
}

static int msm_otg_phy_clk_reset(struct msm_otg *motg)
{
	int ret = 0;

	if (motg->phy_rst)
		ret = reset_control_reset(motg->phy_rst);

	if (ret)
		dev_err(motg->phy.dev, "usb phy clk reset failed\n");

	return ret;
}

static int msm_link_reset(struct msm_otg *motg)
{
	u32 val;
	int ret;

	ret = msm_otg_link_clk_reset(motg, 1);
	if (ret)
		return ret;

	/* wait for 1ms delay as suggested in HPG. */
	usleep_range(1000, 1200);

	ret = msm_otg_link_clk_reset(motg, 0);
	if (ret)
		return ret;

	if (motg->phy_number)
		writel(readl(USB_PHY_CTRL2) | BIT(16), USB_PHY_CTRL2);

	/* put transceiver in serial mode as part of reset */
	val = readl(USB_PORTSC) & ~PORTSC_PTS_MASK;
	writel(val | PORTSC_PTS_SERIAL, USB_PORTSC);

	return 0;
}

static int msm_otg_reset(struct usb_phy *phy)
{
	struct msm_otg *motg = container_of(phy, struct msm_otg, phy);
	int cnt = 0;

	writel(USBCMD_RESET, USB_USBCMD);
	while (cnt < LINK_RESET_TIMEOUT_USEC) {
		if (!(readl(USB_USBCMD) & USBCMD_RESET))
			break;
		udelay(1);
		cnt++;
	}
	if (cnt >= LINK_RESET_TIMEOUT_USEC)
		return -ETIMEDOUT;

	/* select ULPI phy and clear other status/control bits in PORTSC */
	writel(PORTSC_PTS_ULPI, USB_PORTSC);

	writel(0x0, USB_AHBBURST);
	writel(0x08, USB_AHBMODE);

	if (motg->phy_number)
		writel(readl(USB_PHY_CTRL2) | BIT(16), USB_PHY_CTRL2);
	return 0;
}

static void msm_phy_reset(struct msm_otg *motg)
{
	void __iomem *addr;

	if (motg->pdata->phy_type != SNPS_28NM_INTEGRATED_PHY) {
		msm_otg_phy_clk_reset(motg);
		return;
	}

	addr = USB_PHY_CTRL;
	if (motg->phy_number)
		addr = USB_PHY_CTRL2;

	/* Assert USB PHY_POR */
	writel(readl(addr) | PHY_POR_ASSERT, addr);

	/*
	 * wait for minimum 10 microseconds as suggested in HPG.
	 * Use a slightly larger value since the exact value didn't
	 * work 100% of the time.
	 */
	udelay(12);

	/* Deassert USB PHY_POR */
	writel(readl(addr) & ~PHY_POR_ASSERT, addr);
}

static int msm_usb_reset(struct usb_phy *phy)
{
	struct msm_otg *motg = container_of(phy, struct msm_otg, phy);
	int ret;

	if (!IS_ERR(motg->core_clk))
		clk_prepare_enable(motg->core_clk);

	ret = msm_link_reset(motg);
	if (ret) {
		dev_err(phy->dev, "phy_reset failed\n");
		return ret;
	}

	ret = msm_otg_reset(&motg->phy);
	if (ret) {
		dev_err(phy->dev, "link reset failed\n");
		return ret;
	}

	msleep(100);

	/* Reset USB PHY after performing USB Link RESET */
	msm_phy_reset(motg);

	if (!IS_ERR(motg->core_clk))
		clk_disable_unprepare(motg->core_clk);

	return 0;
}

static int msm_phy_init(struct usb_phy *phy)
{
	struct msm_otg *motg = container_of(phy, struct msm_otg, phy);
	struct msm_otg_platform_data *pdata = motg->pdata;
	u32 val, ulpi_val = 0;

	/* Program USB PHY Override registers. */
	ulpi_init(motg);

	/*
	 * It is recommended in HPG to reset USB PHY after programming
	 * USB PHY Override registers.
	 */
	msm_phy_reset(motg);

	if (pdata->otg_control == OTG_PHY_CONTROL) {
		val = readl(USB_OTGSC);
		if (pdata->mode == USB_DR_MODE_OTG) {
			ulpi_val = ULPI_INT_IDGRD | ULPI_INT_SESS_VALID;
			val |= OTGSC_IDIE | OTGSC_BSVIE;
		} else if (pdata->mode == USB_DR_MODE_PERIPHERAL) {
			ulpi_val = ULPI_INT_SESS_VALID;
			val |= OTGSC_BSVIE;
		}
		writel(val, USB_OTGSC);
		ulpi_write(phy, ulpi_val, ULPI_USB_INT_EN_RISE);
		ulpi_write(phy, ulpi_val, ULPI_USB_INT_EN_FALL);
	}

	if (motg->manual_pullup) {
		val = ULPI_MISC_A_VBUSVLDEXTSEL | ULPI_MISC_A_VBUSVLDEXT;
		ulpi_write(phy, val, ULPI_SET(ULPI_MISC_A));

		val = readl(USB_GENCONFIG_2);
		val |= GENCONFIG_2_SESS_VLD_CTRL_EN;
		writel(val, USB_GENCONFIG_2);

		val = readl(USB_USBCMD);
		val |= USBCMD_SESS_VLD_CTRL;
		writel(val, USB_USBCMD);

		val = ulpi_read(phy, ULPI_FUNC_CTRL);
		val &= ~ULPI_FUNC_CTRL_OPMODE_MASK;
		val |= ULPI_FUNC_CTRL_OPMODE_NORMAL;
		ulpi_write(phy, val, ULPI_FUNC_CTRL);
	}

	if (motg->phy_number)
		writel(readl(USB_PHY_CTRL2) | BIT(16), USB_PHY_CTRL2);

	return 0;
}

#define PHY_SUSPEND_TIMEOUT_USEC	(500 * 1000)
#define PHY_RESUME_TIMEOUT_USEC	(100 * 1000)

#ifdef CONFIG_PM

static int msm_hsusb_config_vddcx(struct msm_otg *motg, int high)
{
	int max_vol = motg->vdd_levels[VDD_LEVEL_MAX];
	int min_vol;
	int ret;

	if (high)
		min_vol = motg->vdd_levels[VDD_LEVEL_MIN];
	else
		min_vol = motg->vdd_levels[VDD_LEVEL_NONE];

	ret = regulator_set_voltage(motg->vddcx, min_vol, max_vol);
	if (ret) {
		pr_err("Cannot set vddcx voltage\n");
		return ret;
	}

	pr_debug("%s: min_vol:%d max_vol:%d\n", __func__, min_vol, max_vol);

	return ret;
}

static int msm_otg_suspend(struct msm_otg *motg)
{
	struct usb_phy *phy = &motg->phy;
	struct usb_bus *bus = phy->otg->host;
	struct msm_otg_platform_data *pdata = motg->pdata;
	void __iomem *addr;
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
		ulpi_read(phy, 0x14);
		if (pdata->otg_control == OTG_PHY_CONTROL)
			ulpi_write(phy, 0x01, 0x30);
		ulpi_write(phy, 0x08, 0x09);
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
		dev_err(phy->dev, "Unable to suspend PHY\n");
		msm_otg_reset(phy);
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

	addr = USB_PHY_CTRL;
	if (motg->phy_number)
		addr = USB_PHY_CTRL2;

	if (motg->pdata->phy_type == SNPS_28NM_INTEGRATED_PHY &&
			motg->pdata->otg_control == OTG_PMIC_CONTROL)
		writel(readl(addr) | PHY_RETEN, addr);

	clk_disable_unprepare(motg->pclk);
	clk_disable_unprepare(motg->clk);
	if (!IS_ERR(motg->core_clk))
		clk_disable_unprepare(motg->core_clk);

	if (motg->pdata->phy_type == SNPS_28NM_INTEGRATED_PHY &&
			motg->pdata->otg_control == OTG_PMIC_CONTROL) {
		msm_hsusb_ldo_set_mode(motg, 0);
		msm_hsusb_config_vddcx(motg, 0);
	}

	if (device_may_wakeup(phy->dev))
		enable_irq_wake(motg->irq);
	if (bus)
		clear_bit(HCD_FLAG_HW_ACCESSIBLE, &(bus_to_hcd(bus))->flags);

	atomic_set(&motg->in_lpm, 1);
	enable_irq(motg->irq);

	dev_info(phy->dev, "USB in low power mode\n");

	return 0;
}

static int msm_otg_resume(struct msm_otg *motg)
{
	struct usb_phy *phy = &motg->phy;
	struct usb_bus *bus = phy->otg->host;
	void __iomem *addr;
	int cnt = 0;
	unsigned temp;

	if (!atomic_read(&motg->in_lpm))
		return 0;

	clk_prepare_enable(motg->pclk);
	clk_prepare_enable(motg->clk);
	if (!IS_ERR(motg->core_clk))
		clk_prepare_enable(motg->core_clk);

	if (motg->pdata->phy_type == SNPS_28NM_INTEGRATED_PHY &&
			motg->pdata->otg_control == OTG_PMIC_CONTROL) {

		addr = USB_PHY_CTRL;
		if (motg->phy_number)
			addr = USB_PHY_CTRL2;

		msm_hsusb_ldo_set_mode(motg, 1);
		msm_hsusb_config_vddcx(motg, 1);
		writel(readl(addr) & ~PHY_RETEN, addr);
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
		dev_err(phy->dev, "Unable to resume USB. Re-plugin the cable\n");
		msm_otg_reset(phy);
	}

skip_phy_resume:
	if (device_may_wakeup(phy->dev))
		disable_irq_wake(motg->irq);
	if (bus)
		set_bit(HCD_FLAG_HW_ACCESSIBLE, &(bus_to_hcd(bus))->flags);

	atomic_set(&motg->in_lpm, 0);

	if (motg->async_int) {
		motg->async_int = 0;
		pm_runtime_put(phy->dev);
		enable_irq(motg->irq);
	}

	dev_info(phy->dev, "USB exited from low power mode\n");

	return 0;
}
#endif

static void msm_otg_notify_charger(struct msm_otg *motg, unsigned mA)
{
	if (motg->cur_power == mA)
		return;

	/* TODO: Notify PMIC about available current */
	dev_info(motg->phy.dev, "Avail curr from USB = %u\n", mA);
	motg->cur_power = mA;
}

static void msm_otg_start_host(struct usb_phy *phy, int on)
{
	struct msm_otg *motg = container_of(phy, struct msm_otg, phy);
	struct msm_otg_platform_data *pdata = motg->pdata;
	struct usb_hcd *hcd;

	if (!phy->otg->host)
		return;

	hcd = bus_to_hcd(phy->otg->host);

	if (on) {
		dev_dbg(phy->dev, "host on\n");

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
		device_wakeup_enable(hcd->self.controller);
#endif
	} else {
		dev_dbg(phy->dev, "host off\n");

#ifdef CONFIG_USB
		usb_remove_hcd(hcd);
#endif
		if (pdata->setup_gpio)
			pdata->setup_gpio(OTG_STATE_UNDEFINED);
		if (pdata->vbus_power)
			pdata->vbus_power(0);
	}
}

static int msm_otg_set_host(struct usb_otg *otg, struct usb_bus *host)
{
	struct msm_otg *motg = container_of(otg->usb_phy, struct msm_otg, phy);
	struct usb_hcd *hcd;

	/*
	 * Fail host registration if this board can support
	 * only peripheral configuration.
	 */
	if (motg->pdata->mode == USB_DR_MODE_PERIPHERAL) {
		dev_info(otg->usb_phy->dev, "Host mode is not supported\n");
		return -ENODEV;
	}

	if (!host) {
		if (otg->state == OTG_STATE_A_HOST) {
			pm_runtime_get_sync(otg->usb_phy->dev);
			msm_otg_start_host(otg->usb_phy, 0);
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
	dev_dbg(otg->usb_phy->dev, "host driver registered w/ tranceiver\n");

	pm_runtime_get_sync(otg->usb_phy->dev);
	schedule_work(&motg->sm_work);

	return 0;
}

static void msm_otg_start_peripheral(struct usb_phy *phy, int on)
{
	struct msm_otg *motg = container_of(phy, struct msm_otg, phy);
	struct msm_otg_platform_data *pdata = motg->pdata;

	if (!phy->otg->gadget)
		return;

	if (on) {
		dev_dbg(phy->dev, "gadget on\n");
		/*
		 * Some boards have a switch cotrolled by gpio
		 * to enable/disable internal HUB. Disable internal
		 * HUB before kicking the gadget.
		 */
		if (pdata->setup_gpio)
			pdata->setup_gpio(OTG_STATE_B_PERIPHERAL);
		usb_gadget_vbus_connect(phy->otg->gadget);
	} else {
		dev_dbg(phy->dev, "gadget off\n");
		usb_gadget_vbus_disconnect(phy->otg->gadget);
		if (pdata->setup_gpio)
			pdata->setup_gpio(OTG_STATE_UNDEFINED);
	}

}

static int msm_otg_set_peripheral(struct usb_otg *otg,
					struct usb_gadget *gadget)
{
	struct msm_otg *motg = container_of(otg->usb_phy, struct msm_otg, phy);

	/*
	 * Fail peripheral registration if this board can support
	 * only host configuration.
	 */
	if (motg->pdata->mode == USB_DR_MODE_HOST) {
		dev_info(otg->usb_phy->dev, "Peripheral mode is not supported\n");
		return -ENODEV;
	}

	if (!gadget) {
		if (otg->state == OTG_STATE_B_PERIPHERAL) {
			pm_runtime_get_sync(otg->usb_phy->dev);
			msm_otg_start_peripheral(otg->usb_phy, 0);
			otg->gadget = NULL;
			otg->state = OTG_STATE_UNDEFINED;
			schedule_work(&motg->sm_work);
		} else {
			otg->gadget = NULL;
		}

		return 0;
	}
	otg->gadget = gadget;
	dev_dbg(otg->usb_phy->dev,
		"peripheral driver registered w/ tranceiver\n");

	pm_runtime_get_sync(otg->usb_phy->dev);
	schedule_work(&motg->sm_work);

	return 0;
}

static bool msm_chg_check_secondary_det(struct msm_otg *motg)
{
	struct usb_phy *phy = &motg->phy;
	u32 chg_det;
	bool ret = false;

	switch (motg->pdata->phy_type) {
	case CI_45NM_INTEGRATED_PHY:
		chg_det = ulpi_read(phy, 0x34);
		ret = chg_det & (1 << 4);
		break;
	case SNPS_28NM_INTEGRATED_PHY:
		chg_det = ulpi_read(phy, 0x87);
		ret = chg_det & 1;
		break;
	default:
		break;
	}
	return ret;
}

static void msm_chg_enable_secondary_det(struct msm_otg *motg)
{
	struct usb_phy *phy = &motg->phy;
	u32 chg_det;

	switch (motg->pdata->phy_type) {
	case CI_45NM_INTEGRATED_PHY:
		chg_det = ulpi_read(phy, 0x34);
		/* Turn off charger block */
		chg_det |= ~(1 << 1);
		ulpi_write(phy, chg_det, 0x34);
		udelay(20);
		/* control chg block via ULPI */
		chg_det &= ~(1 << 3);
		ulpi_write(phy, chg_det, 0x34);
		/* put it in host mode for enabling D- source */
		chg_det &= ~(1 << 2);
		ulpi_write(phy, chg_det, 0x34);
		/* Turn on chg detect block */
		chg_det &= ~(1 << 1);
		ulpi_write(phy, chg_det, 0x34);
		udelay(20);
		/* enable chg detection */
		chg_det &= ~(1 << 0);
		ulpi_write(phy, chg_det, 0x34);
		break;
	case SNPS_28NM_INTEGRATED_PHY:
		/*
		 * Configure DM as current source, DP as current sink
		 * and enable battery charging comparators.
		 */
		ulpi_write(phy, 0x8, 0x85);
		ulpi_write(phy, 0x2, 0x85);
		ulpi_write(phy, 0x1, 0x85);
		break;
	default:
		break;
	}
}

static bool msm_chg_check_primary_det(struct msm_otg *motg)
{
	struct usb_phy *phy = &motg->phy;
	u32 chg_det;
	bool ret = false;

	switch (motg->pdata->phy_type) {
	case CI_45NM_INTEGRATED_PHY:
		chg_det = ulpi_read(phy, 0x34);
		ret = chg_det & (1 << 4);
		break;
	case SNPS_28NM_INTEGRATED_PHY:
		chg_det = ulpi_read(phy, 0x87);
		ret = chg_det & 1;
		break;
	default:
		break;
	}
	return ret;
}

static void msm_chg_enable_primary_det(struct msm_otg *motg)
{
	struct usb_phy *phy = &motg->phy;
	u32 chg_det;

	switch (motg->pdata->phy_type) {
	case CI_45NM_INTEGRATED_PHY:
		chg_det = ulpi_read(phy, 0x34);
		/* enable chg detection */
		chg_det &= ~(1 << 0);
		ulpi_write(phy, chg_det, 0x34);
		break;
	case SNPS_28NM_INTEGRATED_PHY:
		/*
		 * Configure DP as current source, DM as current sink
		 * and enable battery charging comparators.
		 */
		ulpi_write(phy, 0x2, 0x85);
		ulpi_write(phy, 0x1, 0x85);
		break;
	default:
		break;
	}
}

static bool msm_chg_check_dcd(struct msm_otg *motg)
{
	struct usb_phy *phy = &motg->phy;
	u32 line_state;
	bool ret = false;

	switch (motg->pdata->phy_type) {
	case CI_45NM_INTEGRATED_PHY:
		line_state = ulpi_read(phy, 0x15);
		ret = !(line_state & 1);
		break;
	case SNPS_28NM_INTEGRATED_PHY:
		line_state = ulpi_read(phy, 0x87);
		ret = line_state & 2;
		break;
	default:
		break;
	}
	return ret;
}

static void msm_chg_disable_dcd(struct msm_otg *motg)
{
	struct usb_phy *phy = &motg->phy;
	u32 chg_det;

	switch (motg->pdata->phy_type) {
	case CI_45NM_INTEGRATED_PHY:
		chg_det = ulpi_read(phy, 0x34);
		chg_det &= ~(1 << 5);
		ulpi_write(phy, chg_det, 0x34);
		break;
	case SNPS_28NM_INTEGRATED_PHY:
		ulpi_write(phy, 0x10, 0x86);
		break;
	default:
		break;
	}
}

static void msm_chg_enable_dcd(struct msm_otg *motg)
{
	struct usb_phy *phy = &motg->phy;
	u32 chg_det;

	switch (motg->pdata->phy_type) {
	case CI_45NM_INTEGRATED_PHY:
		chg_det = ulpi_read(phy, 0x34);
		/* Turn on D+ current source */
		chg_det |= (1 << 5);
		ulpi_write(phy, chg_det, 0x34);
		break;
	case SNPS_28NM_INTEGRATED_PHY:
		/* Data contact detection enable */
		ulpi_write(phy, 0x10, 0x85);
		break;
	default:
		break;
	}
}

static void msm_chg_block_on(struct msm_otg *motg)
{
	struct usb_phy *phy = &motg->phy;
	u32 func_ctrl, chg_det;

	/* put the controller in non-driving mode */
	func_ctrl = ulpi_read(phy, ULPI_FUNC_CTRL);
	func_ctrl &= ~ULPI_FUNC_CTRL_OPMODE_MASK;
	func_ctrl |= ULPI_FUNC_CTRL_OPMODE_NONDRIVING;
	ulpi_write(phy, func_ctrl, ULPI_FUNC_CTRL);

	switch (motg->pdata->phy_type) {
	case CI_45NM_INTEGRATED_PHY:
		chg_det = ulpi_read(phy, 0x34);
		/* control chg block via ULPI */
		chg_det &= ~(1 << 3);
		ulpi_write(phy, chg_det, 0x34);
		/* Turn on chg detect block */
		chg_det &= ~(1 << 1);
		ulpi_write(phy, chg_det, 0x34);
		udelay(20);
		break;
	case SNPS_28NM_INTEGRATED_PHY:
		/* Clear charger detecting control bits */
		ulpi_write(phy, 0x3F, 0x86);
		/* Clear alt interrupt latch and enable bits */
		ulpi_write(phy, 0x1F, 0x92);
		ulpi_write(phy, 0x1F, 0x95);
		udelay(100);
		break;
	default:
		break;
	}
}

static void msm_chg_block_off(struct msm_otg *motg)
{
	struct usb_phy *phy = &motg->phy;
	u32 func_ctrl, chg_det;

	switch (motg->pdata->phy_type) {
	case CI_45NM_INTEGRATED_PHY:
		chg_det = ulpi_read(phy, 0x34);
		/* Turn off charger block */
		chg_det |= ~(1 << 1);
		ulpi_write(phy, chg_det, 0x34);
		break;
	case SNPS_28NM_INTEGRATED_PHY:
		/* Clear charger detecting control bits */
		ulpi_write(phy, 0x3F, 0x86);
		/* Clear alt interrupt latch and enable bits */
		ulpi_write(phy, 0x1F, 0x92);
		ulpi_write(phy, 0x1F, 0x95);
		break;
	default:
		break;
	}

	/* put the controller in normal mode */
	func_ctrl = ulpi_read(phy, ULPI_FUNC_CTRL);
	func_ctrl &= ~ULPI_FUNC_CTRL_OPMODE_MASK;
	func_ctrl |= ULPI_FUNC_CTRL_OPMODE_NORMAL;
	ulpi_write(phy, func_ctrl, ULPI_FUNC_CTRL);
}

#define MSM_CHG_DCD_POLL_TIME		(100 * HZ/1000) /* 100 msec */
#define MSM_CHG_DCD_MAX_RETRIES		6 /* Tdcd_tmout = 6 * 100 msec */
#define MSM_CHG_PRIMARY_DET_TIME	(40 * HZ/1000) /* TVDPSRC_ON */
#define MSM_CHG_SECONDARY_DET_TIME	(40 * HZ/1000) /* TVDMSRC_ON */
static void msm_chg_detect_work(struct work_struct *w)
{
	struct msm_otg *motg = container_of(w, struct msm_otg, chg_work.work);
	struct usb_phy *phy = &motg->phy;
	bool is_dcd, tmout, vout;
	unsigned long delay;

	dev_dbg(phy->dev, "chg detection work\n");
	switch (motg->chg_state) {
	case USB_CHG_STATE_UNDEFINED:
		pm_runtime_get_sync(phy->dev);
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
		dev_dbg(phy->dev, "charger = %d\n", motg->chg_type);
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
	case USB_DR_MODE_OTG:
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
				set_bit(ID, &motg->inputs);
				clear_bit(B_SESS_VLD, &motg->inputs);
		}
		break;
	case USB_DR_MODE_HOST:
		clear_bit(ID, &motg->inputs);
		break;
	case USB_DR_MODE_PERIPHERAL:
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
	struct usb_otg *otg = motg->phy.otg;

	switch (otg->state) {
	case OTG_STATE_UNDEFINED:
		dev_dbg(otg->usb_phy->dev, "OTG_STATE_UNDEFINED state\n");
		msm_otg_reset(otg->usb_phy);
		msm_otg_init_sm(motg);
		otg->state = OTG_STATE_B_IDLE;
		/* FALL THROUGH */
	case OTG_STATE_B_IDLE:
		dev_dbg(otg->usb_phy->dev, "OTG_STATE_B_IDLE state\n");
		if (!test_bit(ID, &motg->inputs) && otg->host) {
			/* disable BSV bit */
			writel(readl(USB_OTGSC) & ~OTGSC_BSVIE, USB_OTGSC);
			msm_otg_start_host(otg->usb_phy, 1);
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
					msm_otg_start_peripheral(otg->usb_phy,
								 1);
					otg->state
						= OTG_STATE_B_PERIPHERAL;
					break;
				case USB_SDP_CHARGER:
					msm_otg_notify_charger(motg, IUNIT);
					msm_otg_start_peripheral(otg->usb_phy,
								 1);
					otg->state
						= OTG_STATE_B_PERIPHERAL;
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
				pm_runtime_put_sync(otg->usb_phy->dev);
				msm_otg_reset(otg->usb_phy);
			}
			msm_otg_notify_charger(motg, 0);
			motg->chg_state = USB_CHG_STATE_UNDEFINED;
			motg->chg_type = USB_INVALID_CHARGER;
		}

		if (otg->state == OTG_STATE_B_IDLE)
			pm_runtime_put_sync(otg->usb_phy->dev);
		break;
	case OTG_STATE_B_PERIPHERAL:
		dev_dbg(otg->usb_phy->dev, "OTG_STATE_B_PERIPHERAL state\n");
		if (!test_bit(B_SESS_VLD, &motg->inputs) ||
				!test_bit(ID, &motg->inputs)) {
			msm_otg_notify_charger(motg, 0);
			msm_otg_start_peripheral(otg->usb_phy, 0);
			motg->chg_state = USB_CHG_STATE_UNDEFINED;
			motg->chg_type = USB_INVALID_CHARGER;
			otg->state = OTG_STATE_B_IDLE;
			msm_otg_reset(otg->usb_phy);
			schedule_work(w);
		}
		break;
	case OTG_STATE_A_HOST:
		dev_dbg(otg->usb_phy->dev, "OTG_STATE_A_HOST state\n");
		if (test_bit(ID, &motg->inputs)) {
			msm_otg_start_host(otg->usb_phy, 0);
			otg->state = OTG_STATE_B_IDLE;
			msm_otg_reset(otg->usb_phy);
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
	struct usb_phy *phy = &motg->phy;
	u32 otgsc = 0;

	if (atomic_read(&motg->in_lpm)) {
		disable_irq_nosync(irq);
		motg->async_int = 1;
		pm_runtime_get(phy->dev);
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
		dev_dbg(phy->dev, "ID set/clear\n");
		pm_runtime_get_noresume(phy->dev);
	} else if ((otgsc & OTGSC_BSVIS) && (otgsc & OTGSC_BSVIE)) {
		if (otgsc & OTGSC_BSV)
			set_bit(B_SESS_VLD, &motg->inputs);
		else
			clear_bit(B_SESS_VLD, &motg->inputs);
		dev_dbg(phy->dev, "BSV set/clear\n");
		pm_runtime_get_noresume(phy->dev);
	}

	writel(otgsc, USB_OTGSC);
	schedule_work(&motg->sm_work);
	return IRQ_HANDLED;
}

static int msm_otg_mode_show(struct seq_file *s, void *unused)
{
	struct msm_otg *motg = s->private;
	struct usb_otg *otg = motg->phy.otg;

	switch (otg->state) {
	case OTG_STATE_A_HOST:
		seq_puts(s, "host\n");
		break;
	case OTG_STATE_B_PERIPHERAL:
		seq_puts(s, "peripheral\n");
		break;
	default:
		seq_puts(s, "none\n");
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
	struct usb_otg *otg = motg->phy.otg;
	int status = count;
	enum usb_dr_mode req_mode;

	memset(buf, 0x00, sizeof(buf));

	if (copy_from_user(&buf, ubuf, min_t(size_t, sizeof(buf) - 1, count))) {
		status = -EFAULT;
		goto out;
	}

	if (!strncmp(buf, "host", 4)) {
		req_mode = USB_DR_MODE_HOST;
	} else if (!strncmp(buf, "peripheral", 10)) {
		req_mode = USB_DR_MODE_PERIPHERAL;
	} else if (!strncmp(buf, "none", 4)) {
		req_mode = USB_DR_MODE_UNKNOWN;
	} else {
		status = -EINVAL;
		goto out;
	}

	switch (req_mode) {
	case USB_DR_MODE_UNKNOWN:
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
	case USB_DR_MODE_PERIPHERAL:
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
	case USB_DR_MODE_HOST:
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

	pm_runtime_get_sync(otg->usb_phy->dev);
	schedule_work(&motg->sm_work);
out:
	return status;
}

static const struct file_operations msm_otg_mode_fops = {
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

static const struct of_device_id msm_otg_dt_match[] = {
	{
		.compatible = "qcom,usb-otg-ci",
		.data = (void *) CI_45NM_INTEGRATED_PHY
	},
	{
		.compatible = "qcom,usb-otg-snps",
		.data = (void *) SNPS_28NM_INTEGRATED_PHY
	},
	{ }
};
MODULE_DEVICE_TABLE(of, msm_otg_dt_match);

static int msm_otg_vbus_notifier(struct notifier_block *nb, unsigned long event,
				void *ptr)
{
	struct usb_phy *usb_phy = container_of(nb, struct usb_phy, vbus_nb);
	struct msm_otg *motg = container_of(usb_phy, struct msm_otg, phy);

	if (event)
		set_bit(B_SESS_VLD, &motg->inputs);
	else
		clear_bit(B_SESS_VLD, &motg->inputs);

	if (test_bit(B_SESS_VLD, &motg->inputs)) {
		/* Switch D+/D- lines to Device connector */
		gpiod_set_value_cansleep(motg->switch_gpio, 0);
	} else {
		/* Switch D+/D- lines to Hub */
		gpiod_set_value_cansleep(motg->switch_gpio, 1);
	}

	schedule_work(&motg->sm_work);

	return NOTIFY_DONE;
}

static int msm_otg_id_notifier(struct notifier_block *nb, unsigned long event,
				void *ptr)
{
	struct usb_phy *usb_phy = container_of(nb, struct usb_phy, id_nb);
	struct msm_otg *motg = container_of(usb_phy, struct msm_otg, phy);

	if (event)
		clear_bit(ID, &motg->inputs);
	else
		set_bit(ID, &motg->inputs);

	schedule_work(&motg->sm_work);

	return NOTIFY_DONE;
}

static int msm_otg_read_dt(struct platform_device *pdev, struct msm_otg *motg)
{
	struct msm_otg_platform_data *pdata;
	struct device_node *node = pdev->dev.of_node;
	struct property *prop;
	int len, ret, words;
	u32 val, tmp[3];

	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	motg->pdata = pdata;

	pdata->phy_type = (enum msm_usb_phy_type)of_device_get_match_data(&pdev->dev);
	if (!pdata->phy_type)
		return 1;

	motg->link_rst = devm_reset_control_get(&pdev->dev, "link");
	if (IS_ERR(motg->link_rst))
		return PTR_ERR(motg->link_rst);

	motg->phy_rst = devm_reset_control_get(&pdev->dev, "phy");
	if (IS_ERR(motg->phy_rst))
		motg->phy_rst = NULL;

	pdata->mode = usb_get_dr_mode(&pdev->dev);
	if (pdata->mode == USB_DR_MODE_UNKNOWN)
		pdata->mode = USB_DR_MODE_OTG;

	pdata->otg_control = OTG_PHY_CONTROL;
	if (!of_property_read_u32(node, "qcom,otg-control", &val))
		if (val == OTG_PMIC_CONTROL)
			pdata->otg_control = val;

	if (!of_property_read_u32(node, "qcom,phy-num", &val) && val < 2)
		motg->phy_number = val;

	motg->vdd_levels[VDD_LEVEL_NONE] = USB_PHY_SUSP_DIG_VOL;
	motg->vdd_levels[VDD_LEVEL_MIN] = USB_PHY_VDD_DIG_VOL_MIN;
	motg->vdd_levels[VDD_LEVEL_MAX] = USB_PHY_VDD_DIG_VOL_MAX;

	if (of_get_property(node, "qcom,vdd-levels", &len) &&
	    len == sizeof(tmp)) {
		of_property_read_u32_array(node, "qcom,vdd-levels",
					   tmp, len / sizeof(*tmp));
		motg->vdd_levels[VDD_LEVEL_NONE] = tmp[VDD_LEVEL_NONE];
		motg->vdd_levels[VDD_LEVEL_MIN] = tmp[VDD_LEVEL_MIN];
		motg->vdd_levels[VDD_LEVEL_MAX] = tmp[VDD_LEVEL_MAX];
	}

	motg->manual_pullup = of_property_read_bool(node, "qcom,manual-pullup");

	motg->switch_gpio = devm_gpiod_get_optional(&pdev->dev, "switch",
						    GPIOD_OUT_LOW);
	if (IS_ERR(motg->switch_gpio))
		return PTR_ERR(motg->switch_gpio);

	prop = of_find_property(node, "qcom,phy-init-sequence", &len);
	if (!prop || !len)
		return 0;

	words = len / sizeof(u32);

	if (words >= ULPI_EXT_VENDOR_SPECIFIC) {
		dev_warn(&pdev->dev, "Too big PHY init sequence %d\n", words);
		return 0;
	}

	pdata->phy_init_seq = devm_kzalloc(&pdev->dev, len, GFP_KERNEL);
	if (!pdata->phy_init_seq)
		return 0;

	ret = of_property_read_u32_array(node, "qcom,phy-init-sequence",
					 pdata->phy_init_seq, words);
	if (!ret)
		pdata->phy_init_sz = words;

	return 0;
}

static int msm_otg_reboot_notify(struct notifier_block *this,
				 unsigned long code, void *unused)
{
	struct msm_otg *motg = container_of(this, struct msm_otg, reboot);

	/*
	 * Ensure that D+/D- lines are routed to uB connector, so
	 * we could load bootloader/kernel at next reboot
	 */
	gpiod_set_value_cansleep(motg->switch_gpio, 0);
	return NOTIFY_DONE;
}

static int msm_otg_probe(struct platform_device *pdev)
{
	struct regulator_bulk_data regs[3];
	int ret = 0;
	struct device_node *np = pdev->dev.of_node;
	struct msm_otg_platform_data *pdata;
	struct resource *res;
	struct msm_otg *motg;
	struct usb_phy *phy;
	void __iomem *phy_select;

	motg = devm_kzalloc(&pdev->dev, sizeof(struct msm_otg), GFP_KERNEL);
	if (!motg)
		return -ENOMEM;

	motg->phy.otg = devm_kzalloc(&pdev->dev, sizeof(struct usb_otg),
				     GFP_KERNEL);
	if (!motg->phy.otg)
		return -ENOMEM;

	phy = &motg->phy;
	phy->dev = &pdev->dev;

	motg->clk = devm_clk_get(&pdev->dev, np ? "core" : "usb_hs_clk");
	if (IS_ERR(motg->clk)) {
		dev_err(&pdev->dev, "failed to get usb_hs_clk\n");
		return PTR_ERR(motg->clk);
	}

	/*
	 * If USB Core is running its protocol engine based on CORE CLK,
	 * CORE CLK  must be running at >55Mhz for correct HSUSB
	 * operation and USB core cannot tolerate frequency changes on
	 * CORE CLK.
	 */
	motg->pclk = devm_clk_get(&pdev->dev, np ? "iface" : "usb_hs_pclk");
	if (IS_ERR(motg->pclk)) {
		dev_err(&pdev->dev, "failed to get usb_hs_pclk\n");
		return PTR_ERR(motg->pclk);
	}

	/*
	 * USB core clock is not present on all MSM chips. This
	 * clock is introduced to remove the dependency on AXI
	 * bus frequency.
	 */
	motg->core_clk = devm_clk_get(&pdev->dev,
				      np ? "alt_core" : "usb_hs_core_clk");

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -EINVAL;
	motg->regs = devm_ioremap(&pdev->dev, res->start, resource_size(res));
	if (!motg->regs)
		return -ENOMEM;

	pdata = dev_get_platdata(&pdev->dev);
	if (!pdata) {
		if (!np)
			return -ENXIO;
		ret = msm_otg_read_dt(pdev, motg);
		if (ret)
			return ret;
	}

	/*
	 * NOTE: The PHYs can be multiplexed between the chipidea controller
	 * and the dwc3 controller, using a single bit. It is important that
	 * the dwc3 driver does not set this bit in an incompatible way.
	 */
	if (motg->phy_number) {
		phy_select = devm_ioremap_nocache(&pdev->dev, USB2_PHY_SEL, 4);
		if (!phy_select)
			return -ENOMEM;

		/* Enable second PHY with the OTG port */
		writel(0x1, phy_select);
	}

	dev_info(&pdev->dev, "OTG regs = %p\n", motg->regs);

	motg->irq = platform_get_irq(pdev, 0);
	if (motg->irq < 0) {
		dev_err(&pdev->dev, "platform_get_irq failed\n");
		ret = motg->irq;
		return motg->irq;
	}

	regs[0].supply = "vddcx";
	regs[1].supply = "v3p3";
	regs[2].supply = "v1p8";

	ret = devm_regulator_bulk_get(motg->phy.dev, ARRAY_SIZE(regs), regs);
	if (ret)
		return ret;

	motg->vddcx = regs[0].consumer;
	motg->v3p3  = regs[1].consumer;
	motg->v1p8  = regs[2].consumer;

	clk_set_rate(motg->clk, 60000000);

	clk_prepare_enable(motg->clk);
	clk_prepare_enable(motg->pclk);

	if (!IS_ERR(motg->core_clk))
		clk_prepare_enable(motg->core_clk);

	ret = msm_hsusb_init_vddcx(motg, 1);
	if (ret) {
		dev_err(&pdev->dev, "hsusb vddcx configuration failed\n");
		goto disable_clks;
	}

	ret = msm_hsusb_ldo_init(motg, 1);
	if (ret) {
		dev_err(&pdev->dev, "hsusb vreg configuration failed\n");
		goto disable_vddcx;
	}
	ret = msm_hsusb_ldo_set_mode(motg, 1);
	if (ret) {
		dev_err(&pdev->dev, "hsusb vreg enable failed\n");
		goto disable_ldo;
	}

	writel(0, USB_USBINTR);
	writel(0, USB_OTGSC);

	INIT_WORK(&motg->sm_work, msm_otg_sm_work);
	INIT_DELAYED_WORK(&motg->chg_work, msm_chg_detect_work);
	ret = devm_request_irq(&pdev->dev, motg->irq, msm_otg_irq, IRQF_SHARED,
					"msm_otg", motg);
	if (ret) {
		dev_err(&pdev->dev, "request irq failed\n");
		goto disable_ldo;
	}

	phy->init = msm_phy_init;
	phy->notify_disconnect = msm_phy_notify_disconnect;
	phy->type = USB_PHY_TYPE_USB2;
	phy->vbus_nb.notifier_call = msm_otg_vbus_notifier;
	phy->id_nb.notifier_call = msm_otg_id_notifier;

	phy->io_ops = &msm_otg_io_ops;

	phy->otg->usb_phy = &motg->phy;
	phy->otg->set_host = msm_otg_set_host;
	phy->otg->set_peripheral = msm_otg_set_peripheral;

	msm_usb_reset(phy);

	ret = usb_add_phy_dev(&motg->phy);
	if (ret) {
		dev_err(&pdev->dev, "usb_add_phy failed\n");
		goto disable_ldo;
	}

	ret = extcon_get_state(phy->edev, EXTCON_USB);
	if (ret)
		set_bit(B_SESS_VLD, &motg->inputs);
	else
		clear_bit(B_SESS_VLD, &motg->inputs);

	ret = extcon_get_state(phy->id_edev, EXTCON_USB_HOST);
	if (ret)
		clear_bit(ID, &motg->inputs);
	else
		set_bit(ID, &motg->inputs);

	platform_set_drvdata(pdev, motg);
	device_init_wakeup(&pdev->dev, 1);

	if (motg->pdata->mode == USB_DR_MODE_OTG &&
		motg->pdata->otg_control == OTG_USER_CONTROL) {
		ret = msm_otg_debugfs_init(motg);
		if (ret)
			dev_dbg(&pdev->dev, "Can not create mode change file\n");
	}

	if (test_bit(B_SESS_VLD, &motg->inputs)) {
		/* Switch D+/D- lines to Device connector */
		gpiod_set_value_cansleep(motg->switch_gpio, 0);
	} else {
		/* Switch D+/D- lines to Hub */
		gpiod_set_value_cansleep(motg->switch_gpio, 1);
	}

	motg->reboot.notifier_call = msm_otg_reboot_notify;
	register_reboot_notifier(&motg->reboot);

	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	return 0;

disable_ldo:
	msm_hsusb_ldo_init(motg, 0);
disable_vddcx:
	msm_hsusb_init_vddcx(motg, 0);
disable_clks:
	clk_disable_unprepare(motg->pclk);
	clk_disable_unprepare(motg->clk);
	if (!IS_ERR(motg->core_clk))
		clk_disable_unprepare(motg->core_clk);

	return ret;
}

static int msm_otg_remove(struct platform_device *pdev)
{
	struct msm_otg *motg = platform_get_drvdata(pdev);
	struct usb_phy *phy = &motg->phy;
	int cnt = 0;

	if (phy->otg->host || phy->otg->gadget)
		return -EBUSY;

	unregister_reboot_notifier(&motg->reboot);

	/*
	 * Ensure that D+/D- lines are routed to uB connector, so
	 * we could load bootloader/kernel at next reboot
	 */
	gpiod_set_value_cansleep(motg->switch_gpio, 0);

	msm_otg_debugfs_cleanup();
	cancel_delayed_work_sync(&motg->chg_work);
	cancel_work_sync(&motg->sm_work);

	pm_runtime_resume(&pdev->dev);

	device_init_wakeup(&pdev->dev, 0);
	pm_runtime_disable(&pdev->dev);

	usb_remove_phy(phy);
	disable_irq(motg->irq);

	/*
	 * Put PHY in low power mode.
	 */
	ulpi_read(phy, 0x14);
	ulpi_write(phy, 0x08, 0x09);

	writel(readl(USB_PORTSC) | PORTSC_PHCD, USB_PORTSC);
	while (cnt < PHY_SUSPEND_TIMEOUT_USEC) {
		if (readl(USB_PORTSC) & PORTSC_PHCD)
			break;
		udelay(1);
		cnt++;
	}
	if (cnt >= PHY_SUSPEND_TIMEOUT_USEC)
		dev_err(phy->dev, "Unable to suspend PHY\n");

	clk_disable_unprepare(motg->pclk);
	clk_disable_unprepare(motg->clk);
	if (!IS_ERR(motg->core_clk))
		clk_disable_unprepare(motg->core_clk);
	msm_hsusb_ldo_init(motg, 0);

	pm_runtime_set_suspended(&pdev->dev);

	return 0;
}

#ifdef CONFIG_PM
static int msm_otg_runtime_idle(struct device *dev)
{
	struct msm_otg *motg = dev_get_drvdata(dev);
	struct usb_otg *otg = motg->phy.otg;

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

static const struct dev_pm_ops msm_otg_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(msm_otg_pm_suspend, msm_otg_pm_resume)
	SET_RUNTIME_PM_OPS(msm_otg_runtime_suspend, msm_otg_runtime_resume,
				msm_otg_runtime_idle)
};

static struct platform_driver msm_otg_driver = {
	.probe = msm_otg_probe,
	.remove = msm_otg_remove,
	.driver = {
		.name = DRIVER_NAME,
		.pm = &msm_otg_dev_pm_ops,
		.of_match_table = msm_otg_dt_match,
	},
};

module_platform_driver(msm_otg_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("MSM USB transceiver driver");
