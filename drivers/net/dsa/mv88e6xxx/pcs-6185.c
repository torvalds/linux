// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Marvell 88E6185 family SERDES PCS support
 *
 * Copyright (c) 2008 Marvell Semiconductor
 *
 * Copyright (c) 2017 Andrew Lunn <andrew@lunn.ch>
 */
#include <linux/phylink.h>

#include "global2.h"
#include "port.h"
#include "serdes.h"

struct mv88e6185_pcs {
	struct phylink_pcs phylink_pcs;
	unsigned int irq;
	char name[64];

	struct mv88e6xxx_chip *chip;
	int port;
};

static struct mv88e6185_pcs *pcs_to_mv88e6185_pcs(struct phylink_pcs *pcs)
{
	return container_of(pcs, struct mv88e6185_pcs, phylink_pcs);
}

static irqreturn_t mv88e6185_pcs_handle_irq(int irq, void *dev_id)
{
	struct mv88e6185_pcs *mpcs = dev_id;
	struct mv88e6xxx_chip *chip;
	irqreturn_t ret = IRQ_NONE;
	bool link_up;
	u16 status;
	int port;
	int err;

	chip = mpcs->chip;
	port = mpcs->port;

	mv88e6xxx_reg_lock(chip);
	err = mv88e6xxx_port_read(chip, port, MV88E6XXX_PORT_STS, &status);
	mv88e6xxx_reg_unlock(chip);

	if (!err) {
		link_up = !!(status & MV88E6XXX_PORT_STS_LINK);

		phylink_pcs_change(&mpcs->phylink_pcs, link_up);

		ret = IRQ_HANDLED;
	}

	return ret;
}

static void mv88e6185_pcs_get_state(struct phylink_pcs *pcs,
				    struct phylink_link_state *state)
{
	struct mv88e6185_pcs *mpcs = pcs_to_mv88e6185_pcs(pcs);
	struct mv88e6xxx_chip *chip = mpcs->chip;
	int port = mpcs->port;
	u16 status;
	int err;

	mv88e6xxx_reg_lock(chip);
	err = mv88e6xxx_port_read(chip, port, MV88E6XXX_PORT_STS, &status);
	mv88e6xxx_reg_unlock(chip);

	if (err)
		status = 0;

	state->link = !!(status & MV88E6XXX_PORT_STS_LINK);
	if (state->link) {
		state->duplex = status & MV88E6XXX_PORT_STS_DUPLEX ?
			DUPLEX_FULL : DUPLEX_HALF;

		switch (status & MV88E6XXX_PORT_STS_SPEED_MASK) {
		case MV88E6XXX_PORT_STS_SPEED_1000:
			state->speed = SPEED_1000;
			break;

		case MV88E6XXX_PORT_STS_SPEED_100:
			state->speed = SPEED_100;
			break;

		case MV88E6XXX_PORT_STS_SPEED_10:
			state->speed = SPEED_10;
			break;

		default:
			state->link = false;
			break;
		}
	}
}

static int mv88e6185_pcs_config(struct phylink_pcs *pcs, unsigned int neg_mode,
				phy_interface_t interface,
				const unsigned long *advertising,
				bool permit_pause_to_mac)
{
	return 0;
}

static void mv88e6185_pcs_an_restart(struct phylink_pcs *pcs)
{
}

static const struct phylink_pcs_ops mv88e6185_phylink_pcs_ops = {
	.pcs_get_state = mv88e6185_pcs_get_state,
	.pcs_config = mv88e6185_pcs_config,
	.pcs_an_restart = mv88e6185_pcs_an_restart,
};

static int mv88e6185_pcs_init(struct mv88e6xxx_chip *chip, int port)
{
	struct mv88e6185_pcs *mpcs;
	struct device *dev;
	unsigned int irq;
	int err;

	/* There are no configurable serdes lanes on this switch chip, so
	 * we use the static cmode configuration to determine whether we
	 * have a PCS or not.
	 */
	if (chip->ports[port].cmode != MV88E6185_PORT_STS_CMODE_SERDES &&
	    chip->ports[port].cmode != MV88E6185_PORT_STS_CMODE_1000BASE_X)
		return 0;

	dev = chip->dev;

	mpcs = kzalloc(sizeof(*mpcs), GFP_KERNEL);
	if (!mpcs)
		return -ENOMEM;

	mpcs->chip = chip;
	mpcs->port = port;
	mpcs->phylink_pcs.ops = &mv88e6185_phylink_pcs_ops;
	mpcs->phylink_pcs.neg_mode = true;

	irq = mv88e6xxx_serdes_irq_mapping(chip, port);
	if (irq) {
		snprintf(mpcs->name, sizeof(mpcs->name),
			 "mv88e6xxx-%s-serdes-%d", dev_name(dev), port);

		err = request_threaded_irq(irq, NULL, mv88e6185_pcs_handle_irq,
					   IRQF_ONESHOT, mpcs->name, mpcs);
		if (err) {
			kfree(mpcs);
			return err;
		}

		mpcs->irq = irq;
	} else {
		mpcs->phylink_pcs.poll = true;
	}

	chip->ports[port].pcs_private = &mpcs->phylink_pcs;

	return 0;
}

static void mv88e6185_pcs_teardown(struct mv88e6xxx_chip *chip, int port)
{
	struct mv88e6185_pcs *mpcs;

	mpcs = chip->ports[port].pcs_private;
	if (!mpcs)
		return;

	if (mpcs->irq)
		free_irq(mpcs->irq, mpcs);

	kfree(mpcs);

	chip->ports[port].pcs_private = NULL;
}

static struct phylink_pcs *mv88e6185_pcs_select(struct mv88e6xxx_chip *chip,
						int port,
						phy_interface_t interface)
{
	return chip->ports[port].pcs_private;
}

const struct mv88e6xxx_pcs_ops mv88e6185_pcs_ops = {
	.pcs_init = mv88e6185_pcs_init,
	.pcs_teardown = mv88e6185_pcs_teardown,
	.pcs_select = mv88e6185_pcs_select,
};
