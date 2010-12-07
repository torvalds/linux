/* Copyright (c) 2009-2010, Code Aurora Forum. All rights reserved.
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

#include <linux/usb.h>
#include <linux/usb/otg.h>
#include <linux/usb/ulpi.h>
#include <linux/usb/gadget.h>
#include <linux/usb/hcd.h>
#include <linux/usb/msm_hsusb.h>
#include <linux/usb/msm_hsusb_hw.h>

#include <mach/clk.h>

#define MSM_USB_BASE	(motg->regs)
#define DRIVER_NAME	"msm_otg"

#define ULPI_IO_TIMEOUT_USEC	(10 * 1000)
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
	if (motg->pdata->mode == USB_HOST || otg->gadget)
		schedule_work(&motg->sm_work);

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
	if (motg->pdata->mode == USB_PERIPHERAL || otg->host)
		schedule_work(&motg->sm_work);

	return 0;
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
		} else if (test_bit(B_SESS_VLD, &motg->inputs) && otg->gadget) {
			msm_otg_start_peripheral(otg, 1);
			otg->state = OTG_STATE_B_PERIPHERAL;
		}
		break;
	case OTG_STATE_B_PERIPHERAL:
		dev_dbg(otg->dev, "OTG_STATE_B_PERIPHERAL state\n");
		if (!test_bit(B_SESS_VLD, &motg->inputs) ||
				!test_bit(ID, &motg->inputs)) {
			msm_otg_start_peripheral(otg, 0);
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

	otgsc = readl(USB_OTGSC);
	if (!(otgsc & (OTGSC_IDIS | OTGSC_BSVIS)))
		return IRQ_NONE;

	if ((otgsc & OTGSC_IDIS) && (otgsc & OTGSC_IDIE)) {
		if (otgsc & OTGSC_ID)
			set_bit(ID, &motg->inputs);
		else
			clear_bit(ID, &motg->inputs);
		dev_dbg(otg->dev, "ID set/clear\n");
	} else if ((otgsc & OTGSC_BSVIS) && (otgsc & OTGSC_BSVIE)) {
		if (otgsc & OTGSC_BSV)
			set_bit(B_SESS_VLD, &motg->inputs);
		else
			clear_bit(B_SESS_VLD, &motg->inputs);
		dev_dbg(otg->dev, "BSV set/clear\n");
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
	struct msm_otg *motg = file->private_data;
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

	motg->pclk = clk_get(&pdev->dev, "usb_hs_pclk");
	if (IS_ERR(motg->pclk)) {
		dev_err(&pdev->dev, "failed to get usb_hs_pclk\n");
		ret = PTR_ERR(motg->pclk);
		goto put_clk;
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
	if (motg->core_clk)
		clk_enable(motg->core_clk);

	writel(0, USB_USBINTR);
	writel(0, USB_OTGSC);

	INIT_WORK(&motg->sm_work, msm_otg_sm_work);
	ret = request_irq(motg->irq, msm_otg_irq, IRQF_SHARED,
					"msm_otg", motg);
	if (ret) {
		dev_err(&pdev->dev, "request irq failed\n");
		goto disable_clks;
	}

	otg->init = msm_otg_reset;
	otg->set_host = msm_otg_set_host;
	otg->set_peripheral = msm_otg_set_peripheral;

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

	return 0;

free_irq:
	free_irq(motg->irq, motg);
disable_clks:
	clk_disable(motg->pclk);
	clk_disable(motg->clk);
free_regs:
	iounmap(motg->regs);
put_core_clk:
	if (motg->core_clk)
		clk_put(motg->core_clk);
	clk_put(motg->pclk);
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

	if (otg->host || otg->gadget)
		return -EBUSY;

	msm_otg_debugfs_cleanup();
	cancel_work_sync(&motg->sm_work);
	device_init_wakeup(&pdev->dev, 0);
	otg_set_transceiver(NULL);

	free_irq(motg->irq, motg);

	clk_disable(motg->pclk);
	clk_disable(motg->clk);
	if (motg->core_clk)
		clk_disable(motg->core_clk);

	iounmap(motg->regs);

	clk_put(motg->phy_reset_clk);
	clk_put(motg->pclk);
	clk_put(motg->clk);
	if (motg->core_clk)
		clk_put(motg->core_clk);

	kfree(motg);

	return 0;
}

static struct platform_driver msm_otg_driver = {
	.remove = __devexit_p(msm_otg_remove),
	.driver = {
		.name = DRIVER_NAME,
		.owner = THIS_MODULE,
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
