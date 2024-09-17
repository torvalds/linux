// SPDX-License-Identifier: GPL-2.0
/* $Date: 2005/11/12 02:13:49 $ $RCSfile: my3126.c,v $ $Revision: 1.15 $ */
#include "cphy.h"
#include "elmer0.h"
#include "suni1x10gexp_regs.h"

/* Port Reset */
static int my3126_reset(struct cphy *cphy, int wait)
{
	/*
	 * This can be done through registers.  It is not required since
	 * a full chip reset is used.
	 */
	return 0;
}

static int my3126_interrupt_enable(struct cphy *cphy)
{
	schedule_delayed_work(&cphy->phy_update, HZ/30);
	t1_tpi_read(cphy->adapter, A_ELMER0_GPO, &cphy->elmer_gpo);
	return 0;
}

static int my3126_interrupt_disable(struct cphy *cphy)
{
	cancel_delayed_work_sync(&cphy->phy_update);
	return 0;
}

static int my3126_interrupt_clear(struct cphy *cphy)
{
	return 0;
}

#define OFFSET(REG_ADDR)    (REG_ADDR << 2)

static int my3126_interrupt_handler(struct cphy *cphy)
{
	u32 val;
	u16 val16;
	u16 status;
	u32 act_count;
	adapter_t *adapter;
	adapter = cphy->adapter;

	if (cphy->count == 50) {
		cphy_mdio_read(cphy, MDIO_MMD_PMAPMD, MDIO_STAT1, &val);
		val16 = (u16) val;
		status = cphy->bmsr ^ val16;

		if (status & MDIO_STAT1_LSTATUS)
			t1_link_changed(adapter, 0);
		cphy->bmsr = val16;

		/* We have only enabled link change interrupts so it
		   must be that
		 */
		cphy->count = 0;
	}

	t1_tpi_write(adapter, OFFSET(SUNI1x10GEXP_REG_MSTAT_CONTROL),
		SUNI1x10GEXP_BITMSK_MSTAT_SNAP);
	t1_tpi_read(adapter,
		OFFSET(SUNI1x10GEXP_REG_MSTAT_COUNTER_1_LOW), &act_count);
	t1_tpi_read(adapter,
		OFFSET(SUNI1x10GEXP_REG_MSTAT_COUNTER_33_LOW), &val);
	act_count += val;

	/* Populate elmer_gpo with the register value */
	t1_tpi_read(adapter, A_ELMER0_GPO, &val);
	cphy->elmer_gpo = val;

	if ( (val & (1 << 8)) || (val & (1 << 19)) ||
	     (cphy->act_count == act_count) || cphy->act_on ) {
		if (is_T2(adapter))
			val |= (1 << 9);
		else if (t1_is_T1B(adapter))
			val |= (1 << 20);
		cphy->act_on = 0;
	} else {
		if (is_T2(adapter))
			val &= ~(1 << 9);
		else if (t1_is_T1B(adapter))
			val &= ~(1 << 20);
		cphy->act_on = 1;
	}

	t1_tpi_write(adapter, A_ELMER0_GPO, val);

	cphy->elmer_gpo = val;
	cphy->act_count = act_count;
	cphy->count++;

	return cphy_cause_link_change;
}

static void my3126_poll(struct work_struct *work)
{
	struct cphy *cphy = container_of(work, struct cphy, phy_update.work);

	my3126_interrupt_handler(cphy);
}

static int my3126_set_loopback(struct cphy *cphy, int on)
{
	return 0;
}

/* To check the activity LED */
static int my3126_get_link_status(struct cphy *cphy,
			int *link_ok, int *speed, int *duplex, int *fc)
{
	u32 val;
	u16 val16;
	adapter_t *adapter;

	adapter = cphy->adapter;
	cphy_mdio_read(cphy, MDIO_MMD_PMAPMD, MDIO_STAT1, &val);
	val16 = (u16) val;

	/* Populate elmer_gpo with the register value */
	t1_tpi_read(adapter, A_ELMER0_GPO, &val);
	cphy->elmer_gpo = val;

	*link_ok = (val16 & MDIO_STAT1_LSTATUS);

	if (*link_ok) {
		/* Turn on the LED. */
		if (is_T2(adapter))
			 val &= ~(1 << 8);
		else if (t1_is_T1B(adapter))
			 val &= ~(1 << 19);
	} else {
		/* Turn off the LED. */
		if (is_T2(adapter))
			 val |= (1 << 8);
		else if (t1_is_T1B(adapter))
			 val |= (1 << 19);
	}

	t1_tpi_write(adapter, A_ELMER0_GPO, val);
	cphy->elmer_gpo = val;
	*speed = SPEED_10000;
	*duplex = DUPLEX_FULL;

	/* need to add flow control */
	if (fc)
		*fc = PAUSE_RX | PAUSE_TX;

	return 0;
}

static void my3126_destroy(struct cphy *cphy)
{
	kfree(cphy);
}

static const struct cphy_ops my3126_ops = {
	.destroy		= my3126_destroy,
	.reset			= my3126_reset,
	.interrupt_enable	= my3126_interrupt_enable,
	.interrupt_disable	= my3126_interrupt_disable,
	.interrupt_clear	= my3126_interrupt_clear,
	.interrupt_handler	= my3126_interrupt_handler,
	.get_link_status	= my3126_get_link_status,
	.set_loopback		= my3126_set_loopback,
	.mmds			= (MDIO_DEVS_PMAPMD | MDIO_DEVS_PCS |
				   MDIO_DEVS_PHYXS),
};

static struct cphy *my3126_phy_create(struct net_device *dev,
			int phy_addr, const struct mdio_ops *mdio_ops)
{
	struct cphy *cphy = kzalloc(sizeof (*cphy), GFP_KERNEL);

	if (!cphy)
		return NULL;

	cphy_init(cphy, dev, phy_addr, &my3126_ops, mdio_ops);
	INIT_DELAYED_WORK(&cphy->phy_update, my3126_poll);
	cphy->bmsr = 0;

	return cphy;
}

/* Chip Reset */
static int my3126_phy_reset(adapter_t * adapter)
{
	u32 val;

	t1_tpi_read(adapter, A_ELMER0_GPO, &val);
	val &= ~4;
	t1_tpi_write(adapter, A_ELMER0_GPO, val);
	msleep(100);

	t1_tpi_write(adapter, A_ELMER0_GPO, val | 4);
	msleep(1000);

	/* Now lets enable the Laser. Delay 100us */
	t1_tpi_read(adapter, A_ELMER0_GPO, &val);
	val |= 0x8000;
	t1_tpi_write(adapter, A_ELMER0_GPO, val);
	udelay(100);
	return 0;
}

const struct gphy t1_my3126_ops = {
	.create = my3126_phy_create,
	.reset = my3126_phy_reset
};
