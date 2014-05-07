/*
 * HDMI PHY
 *
 * Copyright (C) 2013 Texas Instruments Incorporated
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <video/omapdss.h>

#include "dss.h"
#include "hdmi.h"

void hdmi_phy_dump(struct hdmi_phy_data *phy, struct seq_file *s)
{
#define DUMPPHY(r) seq_printf(s, "%-35s %08x\n", #r,\
		hdmi_read_reg(phy->base, r))

	DUMPPHY(HDMI_TXPHY_TX_CTRL);
	DUMPPHY(HDMI_TXPHY_DIGITAL_CTRL);
	DUMPPHY(HDMI_TXPHY_POWER_CTRL);
	DUMPPHY(HDMI_TXPHY_PAD_CFG_CTRL);
}

static irqreturn_t hdmi_irq_handler(int irq, void *data)
{
	struct hdmi_wp_data *wp = data;
	u32 irqstatus;

	irqstatus = hdmi_wp_get_irqstatus(wp);
	hdmi_wp_set_irqstatus(wp, irqstatus);

	if ((irqstatus & HDMI_IRQ_LINK_CONNECT) &&
			irqstatus & HDMI_IRQ_LINK_DISCONNECT) {
		/*
		 * If we get both connect and disconnect interrupts at the same
		 * time, turn off the PHY, clear interrupts, and restart, which
		 * raises connect interrupt if a cable is connected, or nothing
		 * if cable is not connected.
		 */
		hdmi_wp_set_phy_pwr(wp, HDMI_PHYPWRCMD_OFF);

		hdmi_wp_set_irqstatus(wp, HDMI_IRQ_LINK_CONNECT |
				HDMI_IRQ_LINK_DISCONNECT);

		hdmi_wp_set_phy_pwr(wp, HDMI_PHYPWRCMD_LDOON);
	} else if (irqstatus & HDMI_IRQ_LINK_CONNECT) {
		hdmi_wp_set_phy_pwr(wp, HDMI_PHYPWRCMD_TXON);
	} else if (irqstatus & HDMI_IRQ_LINK_DISCONNECT) {
		hdmi_wp_set_phy_pwr(wp, HDMI_PHYPWRCMD_LDOON);
	}

	return IRQ_HANDLED;
}

int hdmi_phy_enable(struct hdmi_phy_data *phy, struct hdmi_wp_data *wp,
			struct hdmi_config *cfg)
{
	u16 r = 0;
	u32 irqstatus;

	hdmi_wp_clear_irqenable(wp, 0xffffffff);

	irqstatus = hdmi_wp_get_irqstatus(wp);
	hdmi_wp_set_irqstatus(wp, irqstatus);

	r = hdmi_wp_set_phy_pwr(wp, HDMI_PHYPWRCMD_LDOON);
	if (r)
		return r;

	/*
	 * Read address 0 in order to get the SCP reset done completed
	 * Dummy access performed to make sure reset is done
	 */
	hdmi_read_reg(phy->base, HDMI_TXPHY_TX_CTRL);

	/*
	 * Write to phy address 0 to configure the clock
	 * use HFBITCLK write HDMI_TXPHY_TX_CONTROL_FREQOUT field
	 */
	REG_FLD_MOD(phy->base, HDMI_TXPHY_TX_CTRL, 0x1, 31, 30);

	/* Write to phy address 1 to start HDMI line (TXVALID and TMDSCLKEN) */
	hdmi_write_reg(phy->base, HDMI_TXPHY_DIGITAL_CTRL, 0xF0000000);

	/* Setup max LDO voltage */
	REG_FLD_MOD(phy->base, HDMI_TXPHY_POWER_CTRL, 0xB, 3, 0);

	/* Write to phy address 3 to change the polarity control */
	REG_FLD_MOD(phy->base, HDMI_TXPHY_PAD_CFG_CTRL, 0x1, 27, 27);

	r = request_threaded_irq(phy->irq, NULL, hdmi_irq_handler,
				IRQF_ONESHOT, "OMAP HDMI", wp);
	if (r) {
		DSSERR("HDMI IRQ request failed\n");
		hdmi_wp_set_phy_pwr(wp, HDMI_PHYPWRCMD_OFF);
		return r;
	}

	hdmi_wp_set_irqenable(wp,
		HDMI_IRQ_LINK_CONNECT | HDMI_IRQ_LINK_DISCONNECT);

	return 0;
}

void hdmi_phy_disable(struct hdmi_phy_data *phy, struct hdmi_wp_data *wp)
{
	free_irq(phy->irq, wp);

	hdmi_wp_set_phy_pwr(wp, HDMI_PHYPWRCMD_OFF);
}

#define PHY_OFFSET	0x300
#define PHY_SIZE	0x100

int hdmi_phy_init(struct platform_device *pdev, struct hdmi_phy_data *phy)
{
	struct resource *res;
	struct resource temp_res;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "phy");
	if (!res) {
		DSSDBG("can't get PHY mem resource by name\n");
		/*
		 * if hwmod/DT doesn't have the memory resource information
		 * split into HDMI sub blocks by name, we try again by getting
		 * the platform's first resource. this code will be removed when
		 * the driver can get the mem resources by name
		 */
		res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
		if (!res) {
			DSSERR("can't get PHY mem resource\n");
			return -EINVAL;
		}

		temp_res.start = res->start + PHY_OFFSET;
		temp_res.end = temp_res.start + PHY_SIZE - 1;
		res = &temp_res;
	}

	phy->base = devm_ioremap(&pdev->dev, res->start, resource_size(res));
	if (!phy->base) {
		DSSERR("can't ioremap TX PHY\n");
		return -ENOMEM;
	}

	phy->irq = platform_get_irq(pdev, 0);
	if (phy->irq < 0) {
		DSSERR("platform_get_irq failed\n");
		return -ENODEV;
	}

	return 0;
}
