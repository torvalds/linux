// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/extcon.h>
#include <linux/extcon-provider.h>
#include <linux/delay.h>
#include <linux/sysfs.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/bitops.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/serial_core.h>
#include <linux/serial.h>
#include <linux/clk.h>
#include <linux/workqueue.h>
#include <linux/power_supply.h>
#include <linux/qcom_scm.h>
#include <linux/delay.h>

#define EUD_ENABLE_CMD 1
#define EUD_DISABLE_CMD 0

#define EUD_REG_COM_TX_ID	0x0000
#define EUD_REG_COM_TX_LEN	0x0004
#define EUD_REG_COM_TX_DAT	0x0008
#define EUD_REG_COM_RX_ID	0x000C
#define EUD_REG_COM_RX_LEN	0x0010
#define EUD_REG_COM_RX_DAT	0x0014
#define EUD_REG_EUD_EN2		0x0000
#define EUD_REG_INT1_EN_MASK	0x0024
#define EUD_REG_INT_STATUS_1	0x0044
#define EUD_REG_CTL_OUT_1	0x0074
#define EUD_REG_VBUS_INT_CLR	0x0080
#define EUD_REG_CHGR_INT_CLR	0x0084
#define EUD_REG_CSR_EUD_EN	0x1014
#define EUD_REG_SW_ATTACH_DET	0x1018
#define EUD_REG_UTMI_DELAY_LSB	0x1030
#define EUD_REG_UTMI_DELAY_MSB	0x1034

#define EUD_INT_RX		BIT(0)
#define EUD_INT_TX		BIT(1)
#define EUD_INT_VBUS		BIT(2)
#define EUD_INT_CHGR		BIT(3)
#define EUD_INT_SAFE_MODE	BIT(4)
#define EUD_INT_ALL		(EUD_INT_RX | EUD_INT_TX | \
				EUD_INT_VBUS | EUD_INT_CHGR | \
				EUD_INT_SAFE_MODE)

#define EUD_NR			1
#define EUD_CONSOLE		NULL
#define UART_ID			0x90
#define MAX_FIFO_SIZE		14
#define EUD_UTMI_DELAY_MASK	0xff
#define EUD_UTMI_DELAY_MIN	28

#define PORT_EUD_UART		300

#define EUD_TCSR_ENABLE_BIT	BIT(0)

struct eud_chip {
	struct device			*dev;
	int				eud_irq;
	unsigned int			extcon_id;
	unsigned int			int_status;
	bool				usb_attach;
	bool				chgr_enable;
	void __iomem			*eud_reg_base;
	struct extcon_dev		*extcon;
	struct uart_port		port;
	struct work_struct		eud_work;
	struct power_supply		*usb_psy;
	bool				secure_eud_en;
	bool				need_phy_clk_vote;
	phys_addr_t			eud_mode_mgr2_phys_base;
	struct clk			*eud_ahb2phy_clk;
	struct clk			*eud_clkref_clk;
	bool				eud_clkref_enabled;
	bool				eud_enabled;
	u16				utmi_switch_delay;
};

static const unsigned int eud_extcon_cable[] = {
	EXTCON_USB,
	EXTCON_CHG_USB_SDP,
	EXTCON_JIG,
	EXTCON_NONE,
};

static int enable = EUD_DISABLE_CMD;
static bool eud_ready;
static struct platform_device *eud_private;

static int msm_eud_clkref_en(struct eud_chip *chip, bool enable)
{
	int ret = 0;

	if (chip->eud_clkref_enabled == enable)
		return 0;

	if (enable) {
		/* Vote for EUD USB reference clk, needed to keep QREF enabled */
		ret = clk_prepare_enable(chip->eud_clkref_clk);
		if (ret) {
			dev_err(chip->dev,
				"Failed to enable eud_clkref_clk\n");
			return ret;
		}
	} else {
		clk_disable_unprepare(chip->eud_clkref_clk);
	}

	chip->eud_clkref_enabled = enable;

	return 0;
}

static inline void msm_eud_enable_irqs(struct eud_chip *chip)
{
	/* Enable vbus, chgr & safe mode warning interrupts */
	writel_relaxed(EUD_INT_VBUS | EUD_INT_CHGR | EUD_INT_SAFE_MODE,
			chip->eud_reg_base + EUD_REG_INT1_EN_MASK);
}

static int msm_eud_hw_is_enabled(struct platform_device *pdev)
{
	struct eud_chip *chip = platform_get_drvdata(pdev);
	int sec_eud_enabled = 0;

	if (chip->secure_eud_en) {
		int ret = qcom_scm_io_readl(
				chip->eud_mode_mgr2_phys_base + EUD_REG_EUD_EN2,
				&sec_eud_enabled);
		if (ret) {
			dev_err(&pdev->dev,
				"qcom_scm_io_readl failed with rc: %d\n", ret);
			return 0;
		}

		return sec_eud_enabled;
	}

	return readl_relaxed(chip->eud_reg_base + EUD_REG_CSR_EUD_EN) & BIT(0);
}

static int set_eud_utmi_switch_delay(struct eud_chip *chip)
{
	u8 val;

	if (chip->utmi_switch_delay) {
		val = (chip->utmi_switch_delay >> 8) & EUD_UTMI_DELAY_MASK;
		writew_relaxed(val, chip->eud_reg_base + EUD_REG_UTMI_DELAY_MSB);
		val = chip->utmi_switch_delay & EUD_UTMI_DELAY_MASK;
		writew_relaxed(val, chip->eud_reg_base + EUD_REG_UTMI_DELAY_LSB);
	}

	return 0;
}

static int check_eud_mode_mgr2(struct eud_chip *chip)
{
	u32 val;

	qcom_scm_io_readl(chip->eud_mode_mgr2_phys_base, &val);
	return val & BIT(0);
}

static void enable_eud(struct platform_device *pdev)
{
	struct eud_chip *priv = platform_get_drvdata(pdev);
	int ret;

	/* No need to notify USB and modify EUD CSR if already enabled */
	if (priv->eud_enabled)
		return;

	/*
	 * Set the default cable state to usb connect and charger
	 * enable
	 */
	extcon_set_state_sync(priv->extcon, EXTCON_USB, true);
	/* indicate that the eud enable is due to the module param */
	extcon_set_state(priv->extcon, EXTCON_JIG, false);
	/* perform spoof disconnect as recommended */
	extcon_set_state_sync(priv->extcon, EXTCON_USB, false);

	msm_eud_clkref_en(priv, true);
	set_eud_utmi_switch_delay(priv);
	/* write into CSR to enable EUD */
	writel_relaxed(BIT(0), priv->eud_reg_base + EUD_REG_CSR_EUD_EN);

	msm_eud_enable_irqs(priv);

	/* Enable secure eud if supported */
	if (priv->secure_eud_en && !check_eud_mode_mgr2(priv)) {
		ret = qcom_scm_io_writel(priv->eud_mode_mgr2_phys_base +
				   EUD_REG_EUD_EN2, EUD_ENABLE_CMD);
		if (ret)
			dev_err(&pdev->dev,
			"qcom_scm_io_writel failed with rc:%d\n", ret);
	}

	/* Ensure Register Writes Complete */
	wmb();

	usleep_range(50, 100);
	/* perform spoof connect as recommended */
	extcon_set_state_sync(priv->extcon, EXTCON_USB, true);
	extcon_set_state_sync(priv->extcon, EXTCON_CHG_USB_SDP, true);
	priv->eud_enabled = true;

	dev_dbg(&pdev->dev, "%s: EUD is Enabled\n", __func__);
}

static void disable_eud(struct platform_device *pdev)
{
	struct eud_chip *priv = platform_get_drvdata(pdev);
	int ret;

	if (!priv->eud_enabled)
		return;

	/* indicate that the eud enable is due to the module param */
	extcon_set_state(priv->extcon, EXTCON_JIG, false);

	/* perform spoof disconnect as recommended */
	extcon_set_state_sync(priv->extcon, EXTCON_USB, false);

	/* write into CSR to disable EUD */
	writel_relaxed(0, priv->eud_reg_base + EUD_REG_CSR_EUD_EN);

	/* Disable secure eud if supported */
	if (priv->secure_eud_en) {
		ret = qcom_scm_io_writel(priv->eud_mode_mgr2_phys_base +
				   EUD_REG_EUD_EN2, EUD_DISABLE_CMD);
		if (ret)
			dev_err(&pdev->dev,
			"qcom_scm_io_write failed with rc:%d\n", ret);
	}

	msm_eud_clkref_en(priv, false);

	usleep_range(50, 100);
	/* perform spoof connect as recommended */
	extcon_set_state_sync(priv->extcon, EXTCON_USB, true);
	priv->eud_enabled = false;

	dev_dbg(&pdev->dev, "%s: EUD Disabled!\n", __func__);
}

static int param_eud_set(const char *val, const struct kernel_param *kp)
{
	int enable = 0;

	if (sscanf(val, "%du", &enable) != 1)
		return -EINVAL;

	if (enable != EUD_ENABLE_CMD && enable != EUD_DISABLE_CMD)
		return -EINVAL;

	*((uint *)kp->arg) = enable;
	if (!eud_ready)
		return 0;

	if (enable == EUD_ENABLE_CMD) {
		pr_debug("%s: Enbling EUD\n", __func__);
		enable_eud(eud_private);
	} else if (enable == EUD_DISABLE_CMD) {
		pr_debug("%s: Disabling EUD\n", __func__);
		disable_eud(eud_private);
	}

	return 0;
}

static const struct kernel_param_ops eud_param_ops = {
	.set = param_eud_set,
	.get = param_get_int,
};

module_param_cb(enable, &eud_param_ops, &enable, 0644);

static bool is_usb_psy_available(struct eud_chip *chip)
{
	if (!chip->usb_psy)
		chip->usb_psy = power_supply_get_by_name("usb");

	if (!chip->usb_psy)
		return false;

	return true;
}

static void eud_event_notifier(struct work_struct *eud_work)
{
	struct eud_chip *chip = container_of(eud_work, struct eud_chip,
					eud_work);
	union power_supply_propval pval;

	if (chip->int_status == EUD_INT_VBUS) {
		extcon_set_state(chip->extcon, EXTCON_JIG, true);
		extcon_set_state_sync(chip->extcon, chip->extcon_id,
					chip->usb_attach);
	} else if (chip->int_status == EUD_INT_CHGR) {
		if (is_usb_psy_available(chip)) {
			int ret;

			pval.intval = chip->chgr_enable ? -EINVAL :
				chip->chgr_enable;
			ret = power_supply_set_property(chip->usb_psy,
				POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT, &pval);
			if (ret < 0)
				dev_err(chip->dev,
				"Failed to set the powersupply property: %d\n",
				ret);
		}
	}
}

static void usb_attach_detach(struct eud_chip *chip)
{
	u32 reg;

	chip->extcon_id = EXTCON_USB;
	/* read ctl_out_1[4] to find USB attach or detach event */
	reg = readl_relaxed(chip->eud_reg_base + EUD_REG_CTL_OUT_1);
	if (reg & BIT(4))
		chip->usb_attach = true;
	else
		chip->usb_attach = false;

	schedule_work(&chip->eud_work);

	/* set and clear vbus_int_clr[0] to clear interrupt */
	writel_relaxed(BIT(0), chip->eud_reg_base + EUD_REG_VBUS_INT_CLR);
	/* Ensure Register Writes Complete */
	wmb();
	writel_relaxed(0, chip->eud_reg_base + EUD_REG_VBUS_INT_CLR);
}

static void chgr_enable_disable(struct eud_chip *chip)
{
	u32 reg;

	chip->extcon_id = EXTCON_CHG_USB_SDP;
	/* read ctl_out_1[6] to find charger enable or disable event */
	reg = readl_relaxed(chip->eud_reg_base + EUD_REG_CTL_OUT_1);
	if (reg & BIT(6))
		chip->chgr_enable = true;
	else
		chip->chgr_enable = false;

	schedule_work(&chip->eud_work);

	/* set and clear chgr_int_clr[0] to clear interrupt */
	writel_relaxed(BIT(0), chip->eud_reg_base + EUD_REG_CHGR_INT_CLR);
	/* Ensure Register Writes Complete */
	wmb();
	writel_relaxed(0, chip->eud_reg_base + EUD_REG_CHGR_INT_CLR);
}

static void pet_eud(struct eud_chip *chip)
{
	u32 reg;

	/* read sw_attach_det[0] to find attach/detach event */
	reg = readl_relaxed(chip->eud_reg_base + EUD_REG_SW_ATTACH_DET);
	if (reg & BIT(0)) {
		/* Detach & Attach pet for EUD */
		writel_relaxed(0, chip->eud_reg_base + EUD_REG_SW_ATTACH_DET);
		/* Ensure Register Writes Complete */
		wmb();
		/* Delay to make sure detach pet is done before attach pet */
		udelay(100);
		writel_relaxed(BIT(0), chip->eud_reg_base +
					EUD_REG_SW_ATTACH_DET);
		/* Ensure Register Writes Complete */
		wmb();
	} else {
		/* Attach pet for EUD */
		writel_relaxed(BIT(0), chip->eud_reg_base +
					EUD_REG_SW_ATTACH_DET);
		/* Ensure Register Writes Complete */
		wmb();
	}
}

static unsigned int eud_tx_empty(struct uart_port *port)
{
	u32 reg;

	/* read status register and cross check for Tx interrupt */
	reg = readl_relaxed(port->membase + EUD_REG_INT_STATUS_1);
	if (reg & EUD_INT_TX)
		return TIOCSER_TEMT;
	else
		return 0;
}

static void eud_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	/* Nothing to set */
}

static unsigned int eud_get_mctrl(struct uart_port *port)
{
	return 0;
}

static void eud_set_termios(struct uart_port *port, struct ktermios *new,
				const struct ktermios *old)
{
	/* Nothing to do here, but to satisfy the serial core */
}

static void eud_stop_tx(struct uart_port *port)
{
	/* Disable Tx interrupt */
	writel_relaxed((__force u32)~EUD_INT_TX, port->membase + EUD_REG_INT_STATUS_1);
	/* Ensure Register Writes Complete */
	wmb();
}

static void eud_start_tx(struct uart_port *port)
{
	/* Enable Tx interrupt */
	writel_relaxed(EUD_INT_TX, port->membase + EUD_REG_INT_STATUS_1);
	/* Ensure Register Writes Complete */
	wmb();
}

static void eud_stop_rx(struct uart_port *port)
{
	/* Disable Rx interrupt */
	writel_relaxed((__force u32)~EUD_INT_RX, port->membase + EUD_REG_INT_STATUS_1);
	/* Ensure Register Writes Complete */
	wmb();
}

static int eud_startup(struct uart_port *port)
{
	/* Enable Rx interrupt */
	writel_relaxed(EUD_INT_RX, port->membase + EUD_REG_INT_STATUS_1);
	/* Ensure Register Writes Complete */
	wmb();
	return 0;
}

static void eud_shutdown(struct uart_port *port)
{
	/* Disable both Tx & Rx interrupts */
	writel_relaxed((__force u32)(~EUD_INT_TX | ~EUD_INT_RX),
			port->membase + EUD_REG_INT_STATUS_1);
	/* Ensure Register Writes Complete */
	wmb();
}

static const char *eud_type(struct uart_port *port)
{
	return (port->type == PORT_EUD_UART) ? "EUD UART" : NULL;
}

static int eud_request_port(struct uart_port *port)
{
	/* Nothing to request */
	return 0;
}

static void eud_release_port(struct uart_port *port)
{
	/* Nothing to release */
}

static void eud_config_port(struct uart_port *port, int flags)
{
	/* set port type, clear Tx and Rx interrupts */
	port->type = PORT_EUD_UART;
	writel_relaxed((__force u32)(~EUD_INT_TX | ~EUD_INT_RX),
			port->membase + EUD_REG_INT_STATUS_1);
	/* Ensure Register Writes Complete */
	wmb();
}

static int eud_verify_port(struct uart_port *port,
				       struct serial_struct *ser)
{
	if (ser->type != PORT_UNKNOWN && ser->type != PORT_EUD_UART)
		return -EINVAL;
	return 0;
}

/* serial functions supported */
static const struct uart_ops eud_uart_ops = {
	.tx_empty	= eud_tx_empty,
	.set_mctrl	= eud_set_mctrl,
	.get_mctrl	= eud_get_mctrl,
	.set_termios	= eud_set_termios,
	.stop_tx	= eud_stop_tx,
	.start_tx	= eud_start_tx,
	.stop_rx	= eud_stop_rx,
	.startup	= eud_startup,
	.shutdown	= eud_shutdown,
	.type		= eud_type,
	.release_port	= eud_release_port,
	.request_port	= eud_request_port,
	.config_port	= eud_config_port,
	.verify_port	= eud_verify_port,
};

static struct uart_driver eud_uart_driver = {
	.owner		= THIS_MODULE,
	.driver_name	= "msm-eud",
	.dev_name	= "ttyEUD",
	.nr		= EUD_NR,
	.cons		= EUD_CONSOLE,
};

static void eud_uart_rx(struct eud_chip *chip)
{
	struct uart_port *port = &chip->port;
	u32 reg;
	unsigned int len;
	unsigned char ch, flag;
	int i;

	reg = readl_relaxed(chip->eud_reg_base + EUD_REG_COM_RX_ID);
	if (reg != UART_ID) {
		dev_dbg(chip->dev, "Rx interrupt isn't for us\n");
		return;
	}
	/* Read Rx Len & Data registers */
	spin_lock(&port->lock);
	len = readl_relaxed(chip->eud_reg_base + EUD_REG_COM_RX_LEN);
	for (i = 0; i < len; i++) {
		ch = readl_relaxed(chip->eud_reg_base + EUD_REG_COM_RX_DAT);
		flag = TTY_NORMAL;
		port->icount.rx++;

		if (uart_handle_sysrq_char(port, ch))
			continue;
		uart_insert_char(port, 0, 0, ch, flag);
	}

	spin_unlock(&port->lock);
	tty_flip_buffer_push(&port->state->port);
}

static void eud_uart_tx(struct eud_chip *chip)
{
	struct uart_port *port = &chip->port;
	struct circ_buf *xmit = &port->state->xmit;
	unsigned int len;
	u32 reg;

	writel_relaxed(UART_ID, chip->eud_reg_base + EUD_REG_COM_TX_ID);
	reg = readl_relaxed(chip->eud_reg_base + EUD_REG_COM_TX_ID);
	if (reg != UART_ID) {
		dev_dbg(chip->dev, "Tx interrupt isn't for us\n");
		return;
	}
	/* Write to Tx Len & Data registers */
	spin_lock(&port->lock);
	len = uart_circ_chars_pending(xmit);
	if (len > 0) {
		if (len > port->fifosize)
			len = port->fifosize;
		while (len--) {
			writel_relaxed(xmit->buf[xmit->tail],
			       port->membase + EUD_REG_COM_TX_DAT);
			xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
			port->icount.tx++;
		}
	}
	spin_unlock(&port->lock);
}

static irqreturn_t handle_eud_irq(int irq, void *data)
{
	struct eud_chip *chip = data;
	u8 reg;
	u32 int_mask_en1 = readl_relaxed(chip->eud_reg_base +
					EUD_REG_INT1_EN_MASK);

	/* read status register and find out which interrupt triggered */
	reg = readb_relaxed(chip->eud_reg_base + EUD_REG_INT_STATUS_1);
	dev_dbg(chip->dev, "EUD Interrupt is received status:%x\n", reg);

	if (reg & EUD_INT_RX) {
		dev_dbg(chip->dev, "EUD RX Interrupt is received\n");
		eud_uart_rx(chip);
		reg &= ~EUD_INT_RX;
	}

	if (reg & EUD_INT_TX) {
		if (EUD_INT_TX & int_mask_en1) {
			dev_dbg(chip->dev, "EUD TX Interrupt is received\n");
			eud_uart_tx(chip);
		}
		reg &= ~EUD_INT_TX;
	}

	if (reg & EUD_INT_VBUS) {
		dev_dbg(chip->dev, "EUD VBUS Interrupt is received\n");
		chip->int_status = EUD_INT_VBUS;
		usb_attach_detach(chip);
		reg &= ~EUD_INT_VBUS;
	}

	if (reg & EUD_INT_CHGR) {
		dev_dbg(chip->dev, "EUD CHGR Interrupt is received\n");
		chip->int_status = EUD_INT_CHGR;
		chgr_enable_disable(chip);
		reg &= ~EUD_INT_CHGR;
	}

	if (reg & EUD_INT_SAFE_MODE) {
		dev_dbg(chip->dev, "EUD SAFE MODE Interrupt is received\n");
		pet_eud(chip);
		reg &= ~EUD_INT_SAFE_MODE;
	}

	if (reg) {
		dev_dbg(chip->dev, "Unhandled EUD interrupt status:%x\n", reg);
		return IRQ_NONE;
	}

	return IRQ_HANDLED;
}

static int msm_eud_suspend(struct device *dev)
{
	struct eud_chip *chip = dev_get_drvdata(dev);

	if (chip->need_phy_clk_vote && chip->eud_ahb2phy_clk)
		clk_disable_unprepare(chip->eud_ahb2phy_clk);

	return 0;
}

static int msm_eud_resume(struct device *dev)
{
	struct eud_chip *chip = dev_get_drvdata(dev);
	int ret = 0;

	if (chip->need_phy_clk_vote && chip->eud_ahb2phy_clk) {
		ret = clk_prepare_enable(chip->eud_ahb2phy_clk);
		if (ret)
			dev_err(chip->dev, "%s failed to vote ahb2phy clk %d\n",
					__func__, ret);
	}

	return ret;
}

static int msm_eud_probe(struct platform_device *pdev)
{
	struct eud_chip *chip;
	struct uart_port *port;
	struct resource *res;
	int ret;
	bool eud_tcsr_check_state;
	phys_addr_t eud_tcsr_check;

	chip = devm_kzalloc(&pdev->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	platform_set_drvdata(pdev, chip);

	chip->dev = &pdev->dev;
	chip->extcon = devm_extcon_dev_allocate(&pdev->dev, eud_extcon_cable);
	if (IS_ERR(chip->extcon)) {
		dev_err(chip->dev, "%s: failed to allocate extcon device\n",
					__func__);
		return PTR_ERR(chip->extcon);
	}

	ret = devm_extcon_dev_register(&pdev->dev, chip->extcon);
	if (ret) {
		dev_err(chip->dev, "%s: failed to register extcon device\n",
					__func__);
		return ret;
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "eud_base");
	if (!res) {
		dev_err(chip->dev, "%s: failed to get resource eud_base\n",
					__func__);
		return -ENOMEM;
	}

	chip->eud_reg_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(chip->eud_reg_base))
		return PTR_ERR(chip->eud_reg_base);

	chip->eud_irq = platform_get_irq_byname(pdev, "eud_irq");

	chip->secure_eud_en = of_property_read_bool(pdev->dev.of_node,
			      "qcom,secure-eud-en");
	if (chip->secure_eud_en) {
		res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						   "eud_mode_mgr2");
		if (!res) {
			dev_err(chip->dev,
			"%s: failed to get resource eud_mode_mgr2\n",
			__func__);
			return -ENOMEM;
		}

		chip->eud_mode_mgr2_phys_base = res->start;
	}

	chip->need_phy_clk_vote = of_property_read_bool(pdev->dev.of_node,
			      "qcom,eud-clock-vote-req");
	if (chip->need_phy_clk_vote) {
		chip->eud_ahb2phy_clk = devm_clk_get(&pdev->dev,
						     "eud_ahb2phy_clk");
		if (IS_ERR(chip->eud_ahb2phy_clk)) {
			ret = PTR_ERR(chip->eud_ahb2phy_clk);
			return ret;
		}

		ret = clk_prepare_enable(chip->eud_ahb2phy_clk);
		if (ret)
			return ret;
	}

	chip->eud_clkref_clk = devm_clk_get(&pdev->dev, "eud_clkref_clk");
	if (IS_ERR(chip->eud_clkref_clk))
		chip->eud_clkref_clk = NULL;

	ret = devm_request_irq(&pdev->dev, chip->eud_irq, handle_eud_irq,
				IRQF_TRIGGER_HIGH, "eud_irq", chip);
	if (ret) {
		dev_err(chip->dev, "request failed for eud irq\n");
		goto error;
	}

	ret = of_property_read_u16(pdev->dev.of_node, "qcom,eud-utmi-delay",
					&chip->utmi_switch_delay);
	if (ret || (chip->utmi_switch_delay < EUD_UTMI_DELAY_MIN))
		/*
		 * Leave default POR value if not defined.  According to
		 * EUD team, a zero value written to the UTMI SWITCH delay
		 * register is not valid.  Requires a minimum value of 28.
		 */
		chip->utmi_switch_delay = 0;

	device_init_wakeup(&pdev->dev, true);
	enable_irq_wake(chip->eud_irq);

	INIT_WORK(&chip->eud_work, eud_event_notifier);

	pdev->id = 0;
	port = &chip->port;
	port->line = pdev->id;
	port->type = PORT_EUD_UART;
	port->dev = chip->dev;
	port->fifosize = MAX_FIFO_SIZE;
	port->iotype = SERIAL_IO_MEM;
	port->flags = UPF_BOOT_AUTOCONF;
	port->membase = chip->eud_reg_base;
	port->irq = chip->eud_irq;
	port->ops = &eud_uart_ops;

	/*
	 * Before enabling EUD, check for TCSR register
	 * and if present, enable it.
	 */
	eud_tcsr_check_state = of_property_read_bool(
		pdev->dev.of_node, "qcom,eud-tcsr-check-enable");

	if (eud_tcsr_check_state) {
		res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
					"eud_tcsr_check_reg");
		if (res) {
			eud_tcsr_check = res->start;
			ret = qcom_scm_io_writel(eud_tcsr_check,
					EUD_TCSR_ENABLE_BIT);
			if (ret) {
				dev_err(&pdev->dev,
				"TCSR qcom_scm_io_writel failed with rc:%d\n",
				ret);
			}
		} else {
			dev_err(chip->dev,
				"Failed to get resource for tcsr check!\n");
			goto error;
		}
	}

	if (chip->secure_eud_en && check_eud_mode_mgr2(chip))
		enable = 1;

	ret = uart_add_one_port(&eud_uart_driver, port);
	if (ret) {
		dev_err(chip->dev, "failed to add uart port!\n");
		goto error;
	}

	eud_private = pdev;
	eud_ready = true;

	/* Proceed enable other EUD elements if bootloader has enabled it */
	if (msm_eud_hw_is_enabled(pdev)) {
		/*
		 * UEFI USB driver should have already enabled this clock, but
		 * enable it here as well, so the clk driver can track the use
		 * count if we disable EUD from the module param.
		 */
		msm_eud_clkref_en(chip, true);

		set_eud_utmi_switch_delay(chip);

		msm_eud_enable_irqs(chip);

		/*
		 * Set the default cable state to usb connect and charger
		 * enable
		 */
		ret = extcon_set_state_sync(chip->extcon, EXTCON_USB, true);
		if (ret)
			dev_warn(&pdev->dev, "Failed to set EXTCON_USB (%d)\n",
				ret);

		ret = extcon_set_state_sync(chip->extcon,
						EXTCON_CHG_USB_SDP, true);
		if (ret)
			dev_warn(&pdev->dev,
				"Failed to set EXTCON_CHG_USB_SDP (%d)\n", ret);

		enable = EUD_ENABLE_CMD;
		chip->eud_enabled = true;
	}

	return 0;

error:
	if (chip->need_phy_clk_vote && chip->eud_ahb2phy_clk)
		clk_disable_unprepare(chip->eud_ahb2phy_clk);

	return ret;
}

static int msm_eud_remove(struct platform_device *pdev)
{
	struct eud_chip *chip = platform_get_drvdata(pdev);
	struct uart_port *port = &chip->port;

	if (chip->usb_psy)
		power_supply_put(chip->usb_psy);

	uart_remove_one_port(&eud_uart_driver, port);
	device_init_wakeup(chip->dev, false);
	if (chip->need_phy_clk_vote)
		clk_disable_unprepare(chip->eud_ahb2phy_clk);

	return 0;
}

static const struct of_device_id msm_eud_dt_match[] = {
	{.compatible = "qcom,msm-eud"},
	{},
};
MODULE_DEVICE_TABLE(of, msm_eud_dt_match);

static const struct dev_pm_ops msm_eud_dev_pm_ops = {
	SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(msm_eud_suspend, msm_eud_resume)
};

static struct platform_driver msm_eud_driver = {
	.probe		= msm_eud_probe,
	.remove		= msm_eud_remove,
	.driver		= {
		.name		= "msm-eud",
		.pm = &msm_eud_dev_pm_ops,
		.of_match_table = msm_eud_dt_match,
	},
};

static int __init msm_eud_init(void)
{
	int ret;

	ret = uart_register_driver(&eud_uart_driver);
	if (ret) {
		pr_err("%s: Failed to register EUD UART driver\n",
			__func__);
		return ret;
	}

	ret = platform_driver_register(&msm_eud_driver);
	if (ret) {
		pr_err("%s: Failed to register EUD driver\n",
			__func__);
		uart_unregister_driver(&eud_uart_driver);
		return ret;
	}

	return 0;
}
module_init(msm_eud_init);

static void __exit msm_eud_exit(void)
{
	platform_driver_unregister(&msm_eud_driver);
	uart_unregister_driver(&eud_uart_driver);
}
module_exit(msm_eud_exit);

MODULE_DESCRIPTION("QTI EUD driver");
MODULE_LICENSE("GPL");
