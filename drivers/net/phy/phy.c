/*
 * drivers/net/phy/phy.c
 *
 * Framework for configuring and reading PHY devices
 * Based on code in sungem_phy.c and gianfar_phy.c
 *
 * Author: Andy Fleming
 *
 * Copyright (c) 2004 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/unistd.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/mii.h>
#include <linux/ethtool.h>
#include <linux/phy.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/uaccess.h>

/* Convenience function to print out the current phy status
 */
void phy_print_status(struct phy_device *phydev)
{
	pr_info("%s: Link is %s", phydev->dev.bus_id,
			phydev->link ? "Up" : "Down");
	if (phydev->link)
		printk(" - %d/%s", phydev->speed,
				DUPLEX_FULL == phydev->duplex ?
				"Full" : "Half");

	printk("\n");
}
EXPORT_SYMBOL(phy_print_status);


/* Convenience functions for reading/writing a given PHY
 * register. They MUST NOT be called from interrupt context,
 * because the bus read/write functions may wait for an interrupt
 * to conclude the operation. */
int phy_read(struct phy_device *phydev, u16 regnum)
{
	int retval;
	struct mii_bus *bus = phydev->bus;

	spin_lock_bh(&bus->mdio_lock);
	retval = bus->read(bus, phydev->addr, regnum);
	spin_unlock_bh(&bus->mdio_lock);

	return retval;
}
EXPORT_SYMBOL(phy_read);

int phy_write(struct phy_device *phydev, u16 regnum, u16 val)
{
	int err;
	struct mii_bus *bus = phydev->bus;

	spin_lock_bh(&bus->mdio_lock);
	err = bus->write(bus, phydev->addr, regnum, val);
	spin_unlock_bh(&bus->mdio_lock);

	return err;
}
EXPORT_SYMBOL(phy_write);


int phy_clear_interrupt(struct phy_device *phydev)
{
	int err = 0;

	if (phydev->drv->ack_interrupt)
		err = phydev->drv->ack_interrupt(phydev);

	return err;
}


int phy_config_interrupt(struct phy_device *phydev, u32 interrupts)
{
	int err = 0;

	phydev->interrupts = interrupts;
	if (phydev->drv->config_intr)
		err = phydev->drv->config_intr(phydev);

	return err;
}


/* phy_aneg_done
 *
 * description: Reads the status register and returns 0 either if
 *   auto-negotiation is incomplete, or if there was an error.
 *   Returns BMSR_ANEGCOMPLETE if auto-negotiation is done.
 */
static inline int phy_aneg_done(struct phy_device *phydev)
{
	int retval;

	retval = phy_read(phydev, MII_BMSR);

	return (retval < 0) ? retval : (retval & BMSR_ANEGCOMPLETE);
}

/* A structure for mapping a particular speed and duplex
 * combination to a particular SUPPORTED and ADVERTISED value */
struct phy_setting {
	int speed;
	int duplex;
	u32 setting;
};

/* A mapping of all SUPPORTED settings to speed/duplex */
static struct phy_setting settings[] = {
	{
		.speed = 10000,
		.duplex = DUPLEX_FULL,
		.setting = SUPPORTED_10000baseT_Full,
	},
	{
		.speed = SPEED_1000,
		.duplex = DUPLEX_FULL,
		.setting = SUPPORTED_1000baseT_Full,
	},
	{
		.speed = SPEED_1000,
		.duplex = DUPLEX_HALF,
		.setting = SUPPORTED_1000baseT_Half,
	},
	{
		.speed = SPEED_100,
		.duplex = DUPLEX_FULL,
		.setting = SUPPORTED_100baseT_Full,
	},
	{
		.speed = SPEED_100,
		.duplex = DUPLEX_HALF,
		.setting = SUPPORTED_100baseT_Half,
	},
	{
		.speed = SPEED_10,
		.duplex = DUPLEX_FULL,
		.setting = SUPPORTED_10baseT_Full,
	},
	{
		.speed = SPEED_10,
		.duplex = DUPLEX_HALF,
		.setting = SUPPORTED_10baseT_Half,
	},
};

#define MAX_NUM_SETTINGS (sizeof(settings)/sizeof(struct phy_setting))

/* phy_find_setting
 *
 * description: Searches the settings array for the setting which
 *   matches the desired speed and duplex, and returns the index
 *   of that setting.  Returns the index of the last setting if
 *   none of the others match.
 */
static inline int phy_find_setting(int speed, int duplex)
{
	int idx = 0;

	while (idx < ARRAY_SIZE(settings) &&
			(settings[idx].speed != speed ||
			settings[idx].duplex != duplex))
		idx++;

	return idx < MAX_NUM_SETTINGS ? idx : MAX_NUM_SETTINGS - 1;
}

/* phy_find_valid
 * idx: The first index in settings[] to search
 * features: A mask of the valid settings
 *
 * description: Returns the index of the first valid setting less
 *   than or equal to the one pointed to by idx, as determined by
 *   the mask in features.  Returns the index of the last setting
 *   if nothing else matches.
 */
static inline int phy_find_valid(int idx, u32 features)
{
	while (idx < MAX_NUM_SETTINGS && !(settings[idx].setting & features))
		idx++;

	return idx < MAX_NUM_SETTINGS ? idx : MAX_NUM_SETTINGS - 1;
}

/* phy_sanitize_settings
 *
 * description: Make sure the PHY is set to supported speeds and
 *   duplexes.  Drop down by one in this order:  1000/FULL,
 *   1000/HALF, 100/FULL, 100/HALF, 10/FULL, 10/HALF
 */
void phy_sanitize_settings(struct phy_device *phydev)
{
	u32 features = phydev->supported;
	int idx;

	/* Sanitize settings based on PHY capabilities */
	if ((features & SUPPORTED_Autoneg) == 0)
		phydev->autoneg = 0;

	idx = phy_find_valid(phy_find_setting(phydev->speed, phydev->duplex),
			features);

	phydev->speed = settings[idx].speed;
	phydev->duplex = settings[idx].duplex;
}
EXPORT_SYMBOL(phy_sanitize_settings);

/* phy_ethtool_sset:
 * A generic ethtool sset function.  Handles all the details
 *
 * A few notes about parameter checking:
 * - We don't set port or transceiver, so we don't care what they
 *   were set to.
 * - phy_start_aneg() will make sure forced settings are sane, and
 *   choose the next best ones from the ones selected, so we don't
 *   care if ethtool tries to give us bad values
 *
 */
int phy_ethtool_sset(struct phy_device *phydev, struct ethtool_cmd *cmd)
{
	if (cmd->phy_address != phydev->addr)
		return -EINVAL;

	/* We make sure that we don't pass unsupported
	 * values in to the PHY */
	cmd->advertising &= phydev->supported;

	/* Verify the settings we care about. */
	if (cmd->autoneg != AUTONEG_ENABLE && cmd->autoneg != AUTONEG_DISABLE)
		return -EINVAL;

	if (cmd->autoneg == AUTONEG_ENABLE && cmd->advertising == 0)
		return -EINVAL;

	if (cmd->autoneg == AUTONEG_DISABLE
			&& ((cmd->speed != SPEED_1000
					&& cmd->speed != SPEED_100
					&& cmd->speed != SPEED_10)
				|| (cmd->duplex != DUPLEX_HALF
					&& cmd->duplex != DUPLEX_FULL)))
		return -EINVAL;

	phydev->autoneg = cmd->autoneg;

	phydev->speed = cmd->speed;

	phydev->advertising = cmd->advertising;

	if (AUTONEG_ENABLE == cmd->autoneg)
		phydev->advertising |= ADVERTISED_Autoneg;
	else
		phydev->advertising &= ~ADVERTISED_Autoneg;

	phydev->duplex = cmd->duplex;

	/* Restart the PHY */
	phy_start_aneg(phydev);

	return 0;
}

int phy_ethtool_gset(struct phy_device *phydev, struct ethtool_cmd *cmd)
{
	cmd->supported = phydev->supported;

	cmd->advertising = phydev->advertising;

	cmd->speed = phydev->speed;
	cmd->duplex = phydev->duplex;
	cmd->port = PORT_MII;
	cmd->phy_address = phydev->addr;
	cmd->transceiver = XCVR_EXTERNAL;
	cmd->autoneg = phydev->autoneg;

	return 0;
}


/* Note that this function is currently incompatible with the
 * PHYCONTROL layer.  It changes registers without regard to
 * current state.  Use at own risk
 */
int phy_mii_ioctl(struct phy_device *phydev,
		struct mii_ioctl_data *mii_data, int cmd)
{
	u16 val = mii_data->val_in;

	switch (cmd) {
	case SIOCGMIIPHY:
		mii_data->phy_id = phydev->addr;
		break;
	case SIOCGMIIREG:
		mii_data->val_out = phy_read(phydev, mii_data->reg_num);
		break;

	case SIOCSMIIREG:
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;

		if (mii_data->phy_id == phydev->addr) {
			switch(mii_data->reg_num) {
			case MII_BMCR:
				if (val & (BMCR_RESET|BMCR_ANENABLE))
					phydev->autoneg = AUTONEG_DISABLE;
				else
					phydev->autoneg = AUTONEG_ENABLE;
				if ((!phydev->autoneg) && (val & BMCR_FULLDPLX))
					phydev->duplex = DUPLEX_FULL;
				else
					phydev->duplex = DUPLEX_HALF;
				break;
			case MII_ADVERTISE:
				phydev->advertising = val;
				break;
			default:
				/* do nothing */
				break;
			}
		}

		phy_write(phydev, mii_data->reg_num, val);
		
		if (mii_data->reg_num == MII_BMCR 
				&& val & BMCR_RESET
				&& phydev->drv->config_init)
			phydev->drv->config_init(phydev);
		break;
	}

	return 0;
}

/* phy_start_aneg
 *
 * description: Sanitizes the settings (if we're not
 *   autonegotiating them), and then calls the driver's
 *   config_aneg function.  If the PHYCONTROL Layer is operating,
 *   we change the state to reflect the beginning of
 *   Auto-negotiation or forcing.
 */
int phy_start_aneg(struct phy_device *phydev)
{
	int err;

	spin_lock(&phydev->lock);

	if (AUTONEG_DISABLE == phydev->autoneg)
		phy_sanitize_settings(phydev);

	err = phydev->drv->config_aneg(phydev);

	if (err < 0)
		goto out_unlock;

	if (phydev->state != PHY_HALTED) {
		if (AUTONEG_ENABLE == phydev->autoneg) {
			phydev->state = PHY_AN;
			phydev->link_timeout = PHY_AN_TIMEOUT;
		} else {
			phydev->state = PHY_FORCING;
			phydev->link_timeout = PHY_FORCE_TIMEOUT;
		}
	}

out_unlock:
	spin_unlock(&phydev->lock);
	return err;
}
EXPORT_SYMBOL(phy_start_aneg);


static void phy_change(void *data);
static void phy_timer(unsigned long data);

/* phy_start_machine:
 *
 * description: The PHY infrastructure can run a state machine
 *   which tracks whether the PHY is starting up, negotiating,
 *   etc.  This function starts the timer which tracks the state
 *   of the PHY.  If you want to be notified when the state
 *   changes, pass in the callback, otherwise, pass NULL.  If you
 *   want to maintain your own state machine, do not call this
 *   function. */
void phy_start_machine(struct phy_device *phydev,
		void (*handler)(struct net_device *))
{
	phydev->adjust_state = handler;

	init_timer(&phydev->phy_timer);
	phydev->phy_timer.function = &phy_timer;
	phydev->phy_timer.data = (unsigned long) phydev;
	mod_timer(&phydev->phy_timer, jiffies + HZ);
}

/* phy_stop_machine
 *
 * description: Stops the state machine timer, sets the state to
 *   UP (unless it wasn't up yet), and then frees the interrupt,
 *   if it is in use. This function must be called BEFORE
 *   phy_detach.
 */
void phy_stop_machine(struct phy_device *phydev)
{
	del_timer_sync(&phydev->phy_timer);

	spin_lock(&phydev->lock);
	if (phydev->state > PHY_UP)
		phydev->state = PHY_UP;
	spin_unlock(&phydev->lock);

	if (phydev->irq != PHY_POLL)
		phy_stop_interrupts(phydev);

	phydev->adjust_state = NULL;
}

/* phy_force_reduction
 *
 * description: Reduces the speed/duplex settings by
 *   one notch.  The order is so:
 *   1000/FULL, 1000/HALF, 100/FULL, 100/HALF,
 *   10/FULL, 10/HALF.  The function bottoms out at 10/HALF.
 */
static void phy_force_reduction(struct phy_device *phydev)
{
	int idx;

	idx = phy_find_setting(phydev->speed, phydev->duplex);
	
	idx++;

	idx = phy_find_valid(idx, phydev->supported);

	phydev->speed = settings[idx].speed;
	phydev->duplex = settings[idx].duplex;

	pr_info("Trying %d/%s\n", phydev->speed,
			DUPLEX_FULL == phydev->duplex ?
			"FULL" : "HALF");
}


/* phy_error:
 *
 * Moves the PHY to the HALTED state in response to a read
 * or write error, and tells the controller the link is down.
 * Must not be called from interrupt context, or while the
 * phydev->lock is held.
 */
void phy_error(struct phy_device *phydev)
{
	spin_lock(&phydev->lock);
	phydev->state = PHY_HALTED;
	spin_unlock(&phydev->lock);
}

/* phy_interrupt
 *
 * description: When a PHY interrupt occurs, the handler disables
 * interrupts, and schedules a work task to clear the interrupt.
 */
static irqreturn_t phy_interrupt(int irq, void *phy_dat, struct pt_regs *regs)
{
	struct phy_device *phydev = phy_dat;

	/* The MDIO bus is not allowed to be written in interrupt
	 * context, so we need to disable the irq here.  A work
	 * queue will write the PHY to disable and clear the
	 * interrupt, and then reenable the irq line. */
	disable_irq_nosync(irq);

	schedule_work(&phydev->phy_queue);

	return IRQ_HANDLED;
}

/* Enable the interrupts from the PHY side */
int phy_enable_interrupts(struct phy_device *phydev)
{
	int err;

	err = phy_clear_interrupt(phydev);

	if (err < 0)
		return err;

	err = phy_config_interrupt(phydev, PHY_INTERRUPT_ENABLED);

	return err;
}
EXPORT_SYMBOL(phy_enable_interrupts);

/* Disable the PHY interrupts from the PHY side */
int phy_disable_interrupts(struct phy_device *phydev)
{
	int err;

	/* Disable PHY interrupts */
	err = phy_config_interrupt(phydev, PHY_INTERRUPT_DISABLED);

	if (err)
		goto phy_err;

	/* Clear the interrupt */
	err = phy_clear_interrupt(phydev);

	if (err)
		goto phy_err;

	return 0;

phy_err:
	phy_error(phydev);

	return err;
}
EXPORT_SYMBOL(phy_disable_interrupts);

/* phy_start_interrupts
 *
 * description: Request the interrupt for the given PHY.  If
 *   this fails, then we set irq to PHY_POLL.
 *   Otherwise, we enable the interrupts in the PHY.
 *   Returns 0 on success.
 *   This should only be called with a valid IRQ number.
 */
int phy_start_interrupts(struct phy_device *phydev)
{
	int err = 0;

	INIT_WORK(&phydev->phy_queue, phy_change, phydev);

	if (request_irq(phydev->irq, phy_interrupt,
				SA_SHIRQ,
				"phy_interrupt",
				phydev) < 0) {
		printk(KERN_WARNING "%s: Can't get IRQ %d (PHY)\n",
				phydev->bus->name,
				phydev->irq);
		phydev->irq = PHY_POLL;
		return 0;
	}

	err = phy_enable_interrupts(phydev);

	return err;
}
EXPORT_SYMBOL(phy_start_interrupts);

int phy_stop_interrupts(struct phy_device *phydev)
{
	int err;

	err = phy_disable_interrupts(phydev);

	if (err)
		phy_error(phydev);

	free_irq(phydev->irq, phydev);

	return err;
}
EXPORT_SYMBOL(phy_stop_interrupts);


/* Scheduled by the phy_interrupt/timer to handle PHY changes */
static void phy_change(void *data)
{
	int err;
	struct phy_device *phydev = data;

	err = phy_disable_interrupts(phydev);

	if (err)
		goto phy_err;

	spin_lock(&phydev->lock);
	if ((PHY_RUNNING == phydev->state) || (PHY_NOLINK == phydev->state))
		phydev->state = PHY_CHANGELINK;
	spin_unlock(&phydev->lock);

	enable_irq(phydev->irq);

	/* Reenable interrupts */
	err = phy_config_interrupt(phydev, PHY_INTERRUPT_ENABLED);

	if (err)
		goto irq_enable_err;

	return;

irq_enable_err:
	disable_irq(phydev->irq);
phy_err:
	phy_error(phydev);
}

/* Bring down the PHY link, and stop checking the status. */
void phy_stop(struct phy_device *phydev)
{
	spin_lock(&phydev->lock);

	if (PHY_HALTED == phydev->state)
		goto out_unlock;

	if (phydev->irq != PHY_POLL) {
		/* Clear any pending interrupts */
		phy_clear_interrupt(phydev);

		/* Disable PHY Interrupts */
		phy_config_interrupt(phydev, PHY_INTERRUPT_DISABLED);
	}

	phydev->state = PHY_HALTED;

out_unlock:
	spin_unlock(&phydev->lock);
}


/* phy_start
 *
 * description: Indicates the attached device's readiness to
 *   handle PHY-related work.  Used during startup to start the
 *   PHY, and after a call to phy_stop() to resume operation.
 *   Also used to indicate the MDIO bus has cleared an error
 *   condition.
 */
void phy_start(struct phy_device *phydev)
{
	spin_lock(&phydev->lock);

	switch (phydev->state) {
		case PHY_STARTING:
			phydev->state = PHY_PENDING;
			break;
		case PHY_READY:
			phydev->state = PHY_UP;
			break;
		case PHY_HALTED:
			phydev->state = PHY_RESUMING;
		default:
			break;
	}
	spin_unlock(&phydev->lock);
}
EXPORT_SYMBOL(phy_stop);
EXPORT_SYMBOL(phy_start);

/* PHY timer which handles the state machine */
static void phy_timer(unsigned long data)
{
	struct phy_device *phydev = (struct phy_device *)data;
	int needs_aneg = 0;
	int err = 0;

	spin_lock(&phydev->lock);

	if (phydev->adjust_state)
		phydev->adjust_state(phydev->attached_dev);

	switch(phydev->state) {
		case PHY_DOWN:
		case PHY_STARTING:
		case PHY_READY:
		case PHY_PENDING:
			break;
		case PHY_UP:
			needs_aneg = 1;

			phydev->link_timeout = PHY_AN_TIMEOUT;

			break;
		case PHY_AN:
			/* Check if negotiation is done.  Break
			 * if there's an error */
			err = phy_aneg_done(phydev);
			if (err < 0)
				break;

			/* If auto-negotiation is done, we change to
			 * either RUNNING, or NOLINK */
			if (err > 0) {
				err = phy_read_status(phydev);

				if (err)
					break;

				if (phydev->link) {
					phydev->state = PHY_RUNNING;
					netif_carrier_on(phydev->attached_dev);
				} else {
					phydev->state = PHY_NOLINK;
					netif_carrier_off(phydev->attached_dev);
				}

				phydev->adjust_link(phydev->attached_dev);

			} else if (0 == phydev->link_timeout--) {
				/* The counter expired, so either we
				 * switch to forced mode, or the
				 * magic_aneg bit exists, and we try aneg
				 * again */
				if (!(phydev->drv->flags & PHY_HAS_MAGICANEG)) {
					int idx;

					/* We'll start from the
					 * fastest speed, and work
					 * our way down */
					idx = phy_find_valid(0,
							phydev->supported);

					phydev->speed = settings[idx].speed;
					phydev->duplex = settings[idx].duplex;
					
					phydev->autoneg = AUTONEG_DISABLE;
					phydev->state = PHY_FORCING;
					phydev->link_timeout =
						PHY_FORCE_TIMEOUT;

					pr_info("Trying %d/%s\n",
							phydev->speed,
							DUPLEX_FULL ==
							phydev->duplex ?
							"FULL" : "HALF");
				}

				needs_aneg = 1;
			}
			break;
		case PHY_NOLINK:
			err = phy_read_status(phydev);

			if (err)
				break;

			if (phydev->link) {
				phydev->state = PHY_RUNNING;
				netif_carrier_on(phydev->attached_dev);
				phydev->adjust_link(phydev->attached_dev);
			}
			break;
		case PHY_FORCING:
			err = phy_read_status(phydev);

			if (err)
				break;

			if (phydev->link) {
				phydev->state = PHY_RUNNING;
				netif_carrier_on(phydev->attached_dev);
			} else {
				if (0 == phydev->link_timeout--) {
					phy_force_reduction(phydev);
					needs_aneg = 1;
				}
			}

			phydev->adjust_link(phydev->attached_dev);
			break;
		case PHY_RUNNING:
			/* Only register a CHANGE if we are
			 * polling */
			if (PHY_POLL == phydev->irq)
				phydev->state = PHY_CHANGELINK;
			break;
		case PHY_CHANGELINK:
			err = phy_read_status(phydev);

			if (err)
				break;

			if (phydev->link) {
				phydev->state = PHY_RUNNING;
				netif_carrier_on(phydev->attached_dev);
			} else {
				phydev->state = PHY_NOLINK;
				netif_carrier_off(phydev->attached_dev);
			}

			phydev->adjust_link(phydev->attached_dev);

			if (PHY_POLL != phydev->irq)
				err = phy_config_interrupt(phydev,
						PHY_INTERRUPT_ENABLED);
			break;
		case PHY_HALTED:
			if (phydev->link) {
				phydev->link = 0;
				netif_carrier_off(phydev->attached_dev);
				phydev->adjust_link(phydev->attached_dev);
			}
			break;
		case PHY_RESUMING:

			err = phy_clear_interrupt(phydev);

			if (err)
				break;

			err = phy_config_interrupt(phydev,
					PHY_INTERRUPT_ENABLED);

			if (err)
				break;

			if (AUTONEG_ENABLE == phydev->autoneg) {
				err = phy_aneg_done(phydev);
				if (err < 0)
					break;

				/* err > 0 if AN is done.
				 * Otherwise, it's 0, and we're
				 * still waiting for AN */
				if (err > 0) {
					phydev->state = PHY_RUNNING;
				} else {
					phydev->state = PHY_AN;
					phydev->link_timeout = PHY_AN_TIMEOUT;
				}
			} else
				phydev->state = PHY_RUNNING;
			break;
	}

	spin_unlock(&phydev->lock);

	if (needs_aneg)
		err = phy_start_aneg(phydev);

	if (err < 0)
		phy_error(phydev);

	mod_timer(&phydev->phy_timer, jiffies + PHY_STATE_TIME * HZ);
}

