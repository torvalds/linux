/**
 * @file me8254.c
 *
 * @brief 8254 subdevice instance.
 * @note Copyright (C) 2007 Meilhaus Electronic GmbH (support@meilhaus.de)
 * @author Guenter Gebhardt
 */

/*
 * Copyright (C) 2007 Meilhaus Electronic GmbH (support@meilhaus.de)
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __KERNEL__
#  define __KERNEL__
#endif

/*
 * Includes
 */
#include <linux/module.h>

#include <linux/slab.h>
#include <linux/spinlock.h>
#include <asm/io.h>
#include <linux/types.h>

#include "medefines.h"
#include "meinternal.h"
#include "meerror.h"

#include "medebug.h"
#include "me8254_reg.h"
#include "me8254.h"

/*
 * Defines
 */
#define ME8254_NUMBER_CHANNELS 1	/**< One channel per counter. */
#define ME8254_CTR_WIDTH 16			/**< One counter has 16 bits. */

/*
 * Functions
 */

static int me8254_io_reset_subdevice(struct me_subdevice *subdevice,
				     struct file *filep, int flags)
{
	me8254_subdevice_t *instance;
	uint8_t clk_src;
	int err = ME_ERRNO_SUCCESS;

	PDEBUG("executed.\n");

	instance = (me8254_subdevice_t *) subdevice;

	if (flags) {
		PERROR("Invalid flag specified.\n");
		return ME_ERRNO_INVALID_FLAGS;
	}

	ME_SUBDEVICE_ENTER;

	spin_lock(&instance->subdevice_lock);
	spin_lock(instance->ctrl_reg_lock);
	if (instance->ctr_idx == 0)
		outb(ME8254_CTRL_SC0 | ME8254_CTRL_LM | ME8254_CTRL_M0 |
		     ME8254_CTRL_BIN, instance->ctrl_reg);
	else if (instance->ctr_idx == 1)
		outb(ME8254_CTRL_SC1 | ME8254_CTRL_LM | ME8254_CTRL_M0 |
		     ME8254_CTRL_BIN, instance->ctrl_reg);
	else
		outb(ME8254_CTRL_SC2 | ME8254_CTRL_LM | ME8254_CTRL_M0 |
		     ME8254_CTRL_BIN, instance->ctrl_reg);
	spin_unlock(instance->ctrl_reg_lock);

	outb(0x00, instance->val_reg);
	outb(0x00, instance->val_reg);

	spin_lock(instance->clk_src_reg_lock);
	clk_src = inb(instance->clk_src_reg);

	switch (instance->device_id) {
	case PCI_DEVICE_ID_MEILHAUS_ME1400:
	case PCI_DEVICE_ID_MEILHAUS_ME140A:
	case PCI_DEVICE_ID_MEILHAUS_ME140B:
	case PCI_DEVICE_ID_MEILHAUS_ME14E0:
	case PCI_DEVICE_ID_MEILHAUS_ME14EA:
	case PCI_DEVICE_ID_MEILHAUS_ME14EB:
		if (instance->me8254_idx == 0) {
			if (instance->ctr_idx == 0)
				clk_src &=
				    ~(ME1400AB_8254_A_0_CLK_SRC_10MHZ |
				      ME1400AB_8254_A_0_CLK_SRC_QUARZ);
			else if (instance->ctr_idx == 1)
				clk_src &= ~(ME1400AB_8254_A_1_CLK_SRC_PREV);
			else
				clk_src &= ~(ME1400AB_8254_A_2_CLK_SRC_PREV);
		} else {
			if (instance->ctr_idx == 0)
				clk_src &=
				    ~(ME1400AB_8254_B_0_CLK_SRC_10MHZ |
				      ME1400AB_8254_B_0_CLK_SRC_QUARZ);
			else if (instance->ctr_idx == 1)
				clk_src &= ~(ME1400AB_8254_B_1_CLK_SRC_PREV);
			else
				clk_src &= ~(ME1400AB_8254_B_2_CLK_SRC_PREV);
		}
		break;

	case PCI_DEVICE_ID_MEILHAUS_ME140C:
	case PCI_DEVICE_ID_MEILHAUS_ME140D:
		switch (instance->me8254_idx) {
		case 0:
		case 2:
		case 4:
		case 6:
		case 8:
			if (instance->ctr_idx == 0)
				clk_src &= ~(ME1400CD_8254_ACE_0_CLK_SRC_MASK);
			else if (instance->ctr_idx == 1)
				clk_src &= ~(ME1400CD_8254_ACE_1_CLK_SRC_MASK);
			else
				clk_src &= ~(ME1400CD_8254_ACE_2_CLK_SRC_MASK);
			break;

		default:
			if (instance->ctr_idx == 0)
				clk_src &= ~(ME1400CD_8254_BD_0_CLK_SRC_MASK);
			else if (instance->ctr_idx == 1)
				clk_src &= ~(ME1400CD_8254_BD_1_CLK_SRC_MASK);
			else
				clk_src &= ~(ME1400CD_8254_BD_2_CLK_SRC_MASK);
			break;
		}
		break;

	case PCI_DEVICE_ID_MEILHAUS_ME4610:
	case PCI_DEVICE_ID_MEILHAUS_ME4660:
	case PCI_DEVICE_ID_MEILHAUS_ME4660I:
	case PCI_DEVICE_ID_MEILHAUS_ME4660S:
	case PCI_DEVICE_ID_MEILHAUS_ME4660IS:
	case PCI_DEVICE_ID_MEILHAUS_ME4670:
	case PCI_DEVICE_ID_MEILHAUS_ME4670I:
	case PCI_DEVICE_ID_MEILHAUS_ME4670S:
	case PCI_DEVICE_ID_MEILHAUS_ME4670IS:
	case PCI_DEVICE_ID_MEILHAUS_ME4680:
	case PCI_DEVICE_ID_MEILHAUS_ME4680I:
	case PCI_DEVICE_ID_MEILHAUS_ME4680S:
	case PCI_DEVICE_ID_MEILHAUS_ME4680IS:
	case PCI_DEVICE_ID_MEILHAUS_ME8100_A:
	case PCI_DEVICE_ID_MEILHAUS_ME8100_B:

		/* No clock source register available */
		break;

	default:
		PERROR("Invalid device type.\n");
		err = ME_ERRNO_INTERNAL;
	}

	if (!err)
		outb(clk_src, instance->clk_src_reg);

	spin_unlock(instance->clk_src_reg_lock);
	spin_unlock(&instance->subdevice_lock);

	ME_SUBDEVICE_EXIT;

	return err;
}

static int me1400_ab_ref_config(me8254_subdevice_t * instance, int ref)
{
	uint8_t clk_src;

	spin_lock(instance->clk_src_reg_lock);
	clk_src = inb(instance->clk_src_reg);

	switch (ref) {
	case ME_REF_CTR_EXTERNAL:
		if (instance->me8254_idx == 0) {
			if (instance->ctr_idx == 0)
				clk_src &= ~(ME1400AB_8254_A_0_CLK_SRC_QUARZ);
			else if (instance->ctr_idx == 1)
				clk_src &= ~(ME1400AB_8254_A_1_CLK_SRC_PREV);
			else
				clk_src &= ~(ME1400AB_8254_A_2_CLK_SRC_PREV);
		} else {
			if (instance->ctr_idx == 0)
				clk_src &= ~(ME1400AB_8254_B_0_CLK_SRC_QUARZ);
			else if (instance->ctr_idx == 1)
				clk_src &= ~(ME1400AB_8254_B_1_CLK_SRC_PREV);
			else
				clk_src &= ~(ME1400AB_8254_B_2_CLK_SRC_PREV);
		}

		break;

	case ME_REF_CTR_PREVIOUS:
		if (instance->me8254_idx == 0) {
			if (instance->ctr_idx == 0) {
				PERROR("Invalid reference.\n");
				spin_unlock(instance->clk_src_reg_lock);
				return ME_ERRNO_INVALID_SINGLE_CONFIG;
			} else if (instance->ctr_idx == 1)
				clk_src |= (ME1400AB_8254_A_1_CLK_SRC_PREV);
			else
				clk_src |= (ME1400AB_8254_A_2_CLK_SRC_PREV);
		} else {
			if (instance->ctr_idx == 0) {
				PERROR("Invalid reference.\n");
				spin_unlock(instance->clk_src_reg_lock);
				return ME_ERRNO_INVALID_SINGLE_CONFIG;
			} else if (instance->ctr_idx == 1)
				clk_src |= (ME1400AB_8254_B_1_CLK_SRC_PREV);
			else
				clk_src |= (ME1400AB_8254_B_2_CLK_SRC_PREV);
		}

		break;

	case ME_REF_CTR_INTERNAL_1MHZ:
		if (instance->me8254_idx == 0) {
			if (instance->ctr_idx == 0) {
				clk_src |= (ME1400AB_8254_A_0_CLK_SRC_QUARZ);
				clk_src &= ~(ME1400AB_8254_A_0_CLK_SRC_10MHZ);
			} else {
				PERROR("Invalid reference.\n");
				spin_unlock(instance->clk_src_reg_lock);
				return ME_ERRNO_INVALID_SINGLE_CONFIG;
			}
		} else {
			if (instance->ctr_idx == 0) {
				clk_src |= (ME1400AB_8254_B_0_CLK_SRC_QUARZ);
				clk_src &= ~(ME1400AB_8254_B_0_CLK_SRC_10MHZ);
			} else {
				PERROR("Invalid reference.\n");
				spin_unlock(instance->clk_src_reg_lock);
				return ME_ERRNO_INVALID_SINGLE_CONFIG;
			}
		}

		break;

	case ME_REF_CTR_INTERNAL_10MHZ:
		if (instance->me8254_idx == 0) {
			if (instance->ctr_idx == 0) {
				clk_src |= (ME1400AB_8254_A_0_CLK_SRC_QUARZ);
				clk_src |= (ME1400AB_8254_A_0_CLK_SRC_10MHZ);
			} else {
				PERROR("Invalid reference.\n");
				spin_unlock(instance->clk_src_reg_lock);
				return ME_ERRNO_INVALID_SINGLE_CONFIG;
			}
		} else {
			if (instance->ctr_idx == 0) {
				clk_src |= (ME1400AB_8254_A_0_CLK_SRC_QUARZ);
				clk_src |= (ME1400AB_8254_A_0_CLK_SRC_10MHZ);
			} else {
				PERROR("Invalid reference.\n");
				spin_unlock(instance->clk_src_reg_lock);
				return ME_ERRNO_INVALID_SINGLE_CONFIG;
			}
		}

		break;

	default:
		PERROR("Invalid reference.\n");
		spin_unlock(instance->clk_src_reg_lock);
		return ME_ERRNO_INVALID_REF;
	}

	outb(clk_src, instance->clk_src_reg);
	spin_unlock(instance->clk_src_reg_lock);

	return ME_ERRNO_SUCCESS;
}

static int me1400_cd_ref_config(me8254_subdevice_t * instance, int ref)
{
	uint8_t clk_src;

	spin_lock(instance->clk_src_reg_lock);
	clk_src = inb(instance->clk_src_reg);

	switch (ref) {
	case ME_REF_CTR_EXTERNAL:
		switch (instance->me8254_idx) {
		case 0:
		case 2:
		case 4:
		case 6:
		case 8:
			if (instance->ctr_idx == 0)
				clk_src &= ~(ME1400CD_8254_ACE_0_CLK_SRC_MASK);
			else if (instance->ctr_idx == 1)
				clk_src &= ~(ME1400CD_8254_ACE_1_CLK_SRC_MASK);
			else
				clk_src &= ~(ME1400CD_8254_ACE_2_CLK_SRC_MASK);
			break;

		default:
			if (instance->ctr_idx == 0)
				clk_src &= ~(ME1400CD_8254_BD_0_CLK_SRC_MASK);
			else if (instance->ctr_idx == 1)
				clk_src &= ~(ME1400CD_8254_BD_1_CLK_SRC_MASK);
			else
				clk_src &= ~(ME1400CD_8254_BD_2_CLK_SRC_MASK);
			break;
		}
		break;

	case ME_REF_CTR_PREVIOUS:
		switch (instance->me8254_idx) {
		case 0:
		case 2:
		case 4:
		case 6:
		case 8:
			if (instance->ctr_idx == 0) {
				clk_src &= ~(ME1400CD_8254_ACE_0_CLK_SRC_MASK);
				clk_src |= (ME1400CD_8254_ACE_0_CLK_SRC_PREV);
			} else if (instance->ctr_idx == 1) {
				clk_src &= ~(ME1400CD_8254_ACE_1_CLK_SRC_MASK);
				clk_src |= (ME1400CD_8254_ACE_1_CLK_SRC_PREV);
			} else {
				clk_src &= ~(ME1400CD_8254_ACE_2_CLK_SRC_MASK);
				clk_src |= (ME1400CD_8254_ACE_2_CLK_SRC_PREV);
			}
			break;

		default:
			if (instance->ctr_idx == 0) {
				clk_src &= ~(ME1400CD_8254_BD_0_CLK_SRC_MASK);
				clk_src |= (ME1400CD_8254_BD_0_CLK_SRC_PREV);
			} else if (instance->ctr_idx == 1) {
				clk_src &= ~(ME1400CD_8254_BD_1_CLK_SRC_MASK);
				clk_src |= (ME1400CD_8254_BD_1_CLK_SRC_PREV);
			} else {
				clk_src &= ~(ME1400CD_8254_BD_2_CLK_SRC_MASK);
				clk_src |= (ME1400CD_8254_BD_2_CLK_SRC_PREV);
			}
			break;
		}

		break;

	case ME_REF_CTR_INTERNAL_1MHZ:
		switch (instance->me8254_idx) {
		case 0:
		case 2:
		case 4:
		case 6:
		case 8:
			if (instance->ctr_idx == 0) {
				clk_src &= ~(ME1400CD_8254_ACE_0_CLK_SRC_MASK);
				clk_src |= (ME1400CD_8254_ACE_0_CLK_SRC_1MHZ);
			} else {
				PERROR("Invalid reference.\n");
				spin_unlock(instance->clk_src_reg_lock);
				return ME_ERRNO_INVALID_REF;
			}

			break;

		default:
			if (instance->ctr_idx == 0) {
				clk_src &= ~(ME1400CD_8254_BD_0_CLK_SRC_MASK);
				clk_src |= (ME1400CD_8254_BD_0_CLK_SRC_1MHZ);
			} else {
				PERROR("Invalid reference.\n");
				spin_unlock(instance->clk_src_reg_lock);
				return ME_ERRNO_INVALID_REF;
			}
			break;
		}

		break;

	case ME_REF_CTR_INTERNAL_10MHZ:
		switch (instance->me8254_idx) {
		case 0:
		case 2:
		case 4:
		case 6:
		case 8:
			if (instance->ctr_idx == 0) {
				clk_src &= ~(ME1400CD_8254_ACE_0_CLK_SRC_MASK);
				clk_src |= (ME1400CD_8254_ACE_0_CLK_SRC_10MHZ);
			} else {
				PERROR("Invalid reference.\n");
				spin_unlock(instance->clk_src_reg_lock);
				return ME_ERRNO_INVALID_REF;
			}
			break;

		default:
			if (instance->ctr_idx == 0) {
				clk_src &= ~(ME1400CD_8254_BD_0_CLK_SRC_MASK);
				clk_src |= (ME1400CD_8254_BD_0_CLK_SRC_10MHZ);
			} else {
				PERROR("Invalid reference.\n");
				spin_unlock(instance->clk_src_reg_lock);
				return ME_ERRNO_INVALID_REF;
			}

			break;
		}

		break;

	default:
		PERROR("Invalid reference.\n");
		spin_unlock(instance->clk_src_reg_lock);
		return ME_ERRNO_INVALID_REF;
	}

	outb(clk_src, instance->clk_src_reg);
	spin_unlock(instance->clk_src_reg_lock);

	return ME_ERRNO_SUCCESS;
}

static int me4600_ref_config(me8254_subdevice_t * instance, int ref)
{
	switch (ref) {

	case ME_REF_CTR_EXTERNAL:
		// Nothing to do
		break;

	default:
		PERROR("Invalid reference.\n");
//                      spin_unlock(instance->clk_src_reg_lock);
		return ME_ERRNO_INVALID_REF;
	}

	return ME_ERRNO_SUCCESS;
}

static int me8100_ref_config(me8254_subdevice_t * instance, int ref)
{
	switch (ref) {

	case ME_REF_CTR_EXTERNAL:
		// Nothing to do
		break;

	default:
		PERROR("Invalid reference.\n");
//                      spin_unlock(instance->clk_src_reg_lock);
		return ME_ERRNO_INVALID_REF;
	}

	return ME_ERRNO_SUCCESS;
}

static int me8254_io_single_config(struct me_subdevice *subdevice,
				   struct file *filep,
				   int channel,
				   int single_config,
				   int ref,
				   int trig_chan,
				   int trig_type, int trig_edge, int flags)
{
	me8254_subdevice_t *instance;
	int err;

	PDEBUG("executed.\n");

	if (channel) {
		PERROR("Invalid channel.\n");
		return ME_ERRNO_INVALID_CHANNEL;
	}

	instance = (me8254_subdevice_t *) subdevice;

	if (flags) {
		PERROR("Invalid flag specified.\n");
		return ME_ERRNO_INVALID_FLAGS;
	}

	ME_SUBDEVICE_ENTER;

	spin_lock(&instance->subdevice_lock);
	// Configure the counter modes
	if (instance->ctr_idx == 0) {
		if (single_config == ME_SINGLE_CONFIG_CTR_8254_MODE_0) {
			outb(ME8254_CTRL_SC0 | ME8254_CTRL_LM | ME8254_CTRL_M0 |
			     ME8254_CTRL_BIN, instance->ctrl_reg);
		} else if (single_config == ME_SINGLE_CONFIG_CTR_8254_MODE_1) {
			outb(ME8254_CTRL_SC0 | ME8254_CTRL_LM | ME8254_CTRL_M1 |
			     ME8254_CTRL_BIN, instance->ctrl_reg);
		} else if (single_config == ME_SINGLE_CONFIG_CTR_8254_MODE_2) {
			outb(ME8254_CTRL_SC0 | ME8254_CTRL_LM | ME8254_CTRL_M2 |
			     ME8254_CTRL_BIN, instance->ctrl_reg);
		} else if (single_config == ME_SINGLE_CONFIG_CTR_8254_MODE_3) {
			outb(ME8254_CTRL_SC0 | ME8254_CTRL_LM | ME8254_CTRL_M3 |
			     ME8254_CTRL_BIN, instance->ctrl_reg);
		} else if (single_config == ME_SINGLE_CONFIG_CTR_8254_MODE_4) {
			outb(ME8254_CTRL_SC0 | ME8254_CTRL_LM | ME8254_CTRL_M4 |
			     ME8254_CTRL_BIN, instance->ctrl_reg);
		} else if (single_config == ME_SINGLE_CONFIG_CTR_8254_MODE_5) {
			outb(ME8254_CTRL_SC0 | ME8254_CTRL_LM | ME8254_CTRL_M5 |
			     ME8254_CTRL_BIN, instance->ctrl_reg);
		} else {
			PERROR("Invalid single configuration.\n");
			spin_unlock(&instance->subdevice_lock);
			return ME_ERRNO_INVALID_SINGLE_CONFIG;
		}
	} else if (instance->ctr_idx == 1) {
		if (single_config == ME_SINGLE_CONFIG_CTR_8254_MODE_0) {
			outb(ME8254_CTRL_SC1 | ME8254_CTRL_LM | ME8254_CTRL_M0 |
			     ME8254_CTRL_BIN, instance->ctrl_reg);
		} else if (single_config == ME_SINGLE_CONFIG_CTR_8254_MODE_1) {
			outb(ME8254_CTRL_SC1 | ME8254_CTRL_LM | ME8254_CTRL_M1 |
			     ME8254_CTRL_BIN, instance->ctrl_reg);
		} else if (single_config == ME_SINGLE_CONFIG_CTR_8254_MODE_2) {
			outb(ME8254_CTRL_SC1 | ME8254_CTRL_LM | ME8254_CTRL_M2 |
			     ME8254_CTRL_BIN, instance->ctrl_reg);
		} else if (single_config == ME_SINGLE_CONFIG_CTR_8254_MODE_3) {
			outb(ME8254_CTRL_SC1 | ME8254_CTRL_LM | ME8254_CTRL_M3 |
			     ME8254_CTRL_BIN, instance->ctrl_reg);
		} else if (single_config == ME_SINGLE_CONFIG_CTR_8254_MODE_4) {
			outb(ME8254_CTRL_SC1 | ME8254_CTRL_LM | ME8254_CTRL_M4 |
			     ME8254_CTRL_BIN, instance->ctrl_reg);
		} else if (single_config == ME_SINGLE_CONFIG_CTR_8254_MODE_5) {
			outb(ME8254_CTRL_SC1 | ME8254_CTRL_LM | ME8254_CTRL_M5 |
			     ME8254_CTRL_BIN, instance->ctrl_reg);
		} else {
			PERROR("Invalid single configuration.\n");
			spin_unlock(&instance->subdevice_lock);
			return ME_ERRNO_INVALID_SINGLE_CONFIG;
		}
	} else {
		if (single_config == ME_SINGLE_CONFIG_CTR_8254_MODE_0) {
			outb(ME8254_CTRL_SC2 | ME8254_CTRL_LM | ME8254_CTRL_M0 |
			     ME8254_CTRL_BIN, instance->ctrl_reg);
		} else if (single_config == ME_SINGLE_CONFIG_CTR_8254_MODE_1) {
			outb(ME8254_CTRL_SC2 | ME8254_CTRL_LM | ME8254_CTRL_M1 |
			     ME8254_CTRL_BIN, instance->ctrl_reg);
		} else if (single_config == ME_SINGLE_CONFIG_CTR_8254_MODE_2) {
			outb(ME8254_CTRL_SC2 | ME8254_CTRL_LM | ME8254_CTRL_M2 |
			     ME8254_CTRL_BIN, instance->ctrl_reg);
		} else if (single_config == ME_SINGLE_CONFIG_CTR_8254_MODE_3) {
			outb(ME8254_CTRL_SC2 | ME8254_CTRL_LM | ME8254_CTRL_M3 |
			     ME8254_CTRL_BIN, instance->ctrl_reg);
		} else if (single_config == ME_SINGLE_CONFIG_CTR_8254_MODE_4) {
			outb(ME8254_CTRL_SC2 | ME8254_CTRL_LM | ME8254_CTRL_M4 |
			     ME8254_CTRL_BIN, instance->ctrl_reg);
		} else if (single_config == ME_SINGLE_CONFIG_CTR_8254_MODE_5) {
			outb(ME8254_CTRL_SC2 | ME8254_CTRL_LM | ME8254_CTRL_M5 |
			     ME8254_CTRL_BIN, instance->ctrl_reg);
		} else {
			PERROR("Invalid single configuration.\n");
			spin_unlock(&instance->subdevice_lock);
			return ME_ERRNO_INVALID_SINGLE_CONFIG;
		}
	}

	switch (instance->device_id) {
	case PCI_DEVICE_ID_MEILHAUS_ME1400:
	case PCI_DEVICE_ID_MEILHAUS_ME14E0:
	case PCI_DEVICE_ID_MEILHAUS_ME140A:
	case PCI_DEVICE_ID_MEILHAUS_ME14EA:
	case PCI_DEVICE_ID_MEILHAUS_ME140B:
	case PCI_DEVICE_ID_MEILHAUS_ME14EB:
		err = me1400_ab_ref_config(instance, ref);

		if (err) {
			spin_unlock(&instance->subdevice_lock);
			return err;
		}

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME140C:
	case PCI_DEVICE_ID_MEILHAUS_ME140D:
		err = me1400_cd_ref_config(instance, ref);

		if (err) {
			spin_unlock(&instance->subdevice_lock);
			return err;
		}

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME4610:
	case PCI_DEVICE_ID_MEILHAUS_ME4660:
	case PCI_DEVICE_ID_MEILHAUS_ME4660I:
	case PCI_DEVICE_ID_MEILHAUS_ME4660S:
	case PCI_DEVICE_ID_MEILHAUS_ME4660IS:
	case PCI_DEVICE_ID_MEILHAUS_ME4670:
	case PCI_DEVICE_ID_MEILHAUS_ME4670I:
	case PCI_DEVICE_ID_MEILHAUS_ME4670S:
	case PCI_DEVICE_ID_MEILHAUS_ME4670IS:
	case PCI_DEVICE_ID_MEILHAUS_ME4680:
	case PCI_DEVICE_ID_MEILHAUS_ME4680I:
	case PCI_DEVICE_ID_MEILHAUS_ME4680S:
	case PCI_DEVICE_ID_MEILHAUS_ME4680IS:
		err = me4600_ref_config(instance, ref);

		if (err) {
			spin_unlock(&instance->subdevice_lock);
			return err;
		}

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME8100_A:
	case PCI_DEVICE_ID_MEILHAUS_ME8100_B:
		err = me8100_ref_config(instance, ref);

		if (err) {
			spin_unlock(&instance->subdevice_lock);
			return err;
		}

		break;

	default:
		PERROR("Invalid device type.\n");

		spin_unlock(&instance->subdevice_lock);
//                              spin_unlock(instance->clk_src_reg_lock);
		return ME_ERRNO_INVALID_SINGLE_CONFIG;
	}
	spin_unlock(&instance->subdevice_lock);

	ME_SUBDEVICE_EXIT;

	return ME_ERRNO_SUCCESS;
}

static int me8254_io_single_read(struct me_subdevice *subdevice,
				 struct file *filep,
				 int channel,
				 int *value, int time_out, int flags)
{
	me8254_subdevice_t *instance;
	uint16_t lo_byte;
	uint16_t hi_byte;

	PDEBUG("executed.\n");

	if (channel) {
		PERROR("Invalid channel.\n");
		return ME_ERRNO_INVALID_CHANNEL;
	}

	instance = (me8254_subdevice_t *) subdevice;

	if (flags) {
		PERROR("Invalid flag specified.\n");
		return ME_ERRNO_INVALID_FLAGS;
	}

	ME_SUBDEVICE_ENTER;

	spin_lock(&instance->subdevice_lock);
	spin_lock(instance->ctrl_reg_lock);
	if (instance->ctr_idx == 0)
		outb(ME8254_CTRL_SC0 | ME8254_CTRL_TLO, instance->ctrl_reg);
	else if (instance->ctr_idx == 1)
		outb(ME8254_CTRL_SC1 | ME8254_CTRL_TLO, instance->ctrl_reg);
	else
		outb(ME8254_CTRL_SC2 | ME8254_CTRL_TLO, instance->ctrl_reg);

	lo_byte = inb(instance->val_reg);
	hi_byte = inb(instance->val_reg);
	spin_unlock(instance->ctrl_reg_lock);

	*value = lo_byte | (hi_byte << 8);
	spin_unlock(&instance->subdevice_lock);

	ME_SUBDEVICE_EXIT;

	return ME_ERRNO_SUCCESS;
}

static int me8254_io_single_write(struct me_subdevice *subdevice,
				  struct file *filep,
				  int channel,
				  int value, int time_out, int flags)
{
	me8254_subdevice_t *instance;

	PDEBUG("executed.\n");

	if (channel) {
		PERROR("Invalid channel.\n");
		return ME_ERRNO_INVALID_CHANNEL;
	}

	instance = (me8254_subdevice_t *) subdevice;

	if (flags) {
		PERROR("Invalid flag specified.\n");
		return ME_ERRNO_INVALID_FLAGS;
	}

	ME_SUBDEVICE_ENTER;

	spin_lock(&instance->subdevice_lock);
	outb(value, instance->val_reg);
	outb((value >> 8), instance->val_reg);
	spin_unlock(&instance->subdevice_lock);

	ME_SUBDEVICE_EXIT;

	return ME_ERRNO_SUCCESS;
}

static int me8254_query_number_channels(struct me_subdevice *subdevice,
					int *number)
{
	PDEBUG("executed.\n");
	*number = ME8254_NUMBER_CHANNELS;
	return ME_ERRNO_SUCCESS;
}

static int me8254_query_subdevice_type(struct me_subdevice *subdevice,
				       int *type, int *subtype)
{
	PDEBUG("executed.\n");
	*type = ME_TYPE_CTR;
	*subtype = ME_SUBTYPE_CTR_8254;
	return ME_ERRNO_SUCCESS;
}

static int me8254_query_subdevice_caps(struct me_subdevice *subdevice,
				       int *caps)
{
	me8254_subdevice_t *instance;
	PDEBUG("executed.\n");
	instance = (me8254_subdevice_t *) subdevice;
	*caps = instance->caps;
	return ME_ERRNO_SUCCESS;
}

static int me8254_query_subdevice_caps_args(struct me_subdevice *subdevice,
					    int cap, int *args, int count)
{
	PDEBUG("executed.\n");

	if (count != 1) {
		PERROR("Invalid capability argument count.\n");
		return ME_ERRNO_INVALID_CAP_ARG_COUNT;
	}

	if (cap == ME_CAP_CTR_WIDTH) {
		args[0] = ME8254_CTR_WIDTH;
	} else {
		PERROR("Invalid capability.\n");
		return ME_ERRNO_INVALID_CAP;
	}

	return ME_ERRNO_SUCCESS;
}

static uint32_t me1400AB_get_val_reg(uint32_t reg_base, unsigned int me8254_idx,
				     unsigned int ctr_idx)
{
	switch (me8254_idx) {

	case 0:
		return (reg_base + ME1400AB_8254_A_0_VAL_REG + ctr_idx);

	default:
		return (reg_base + ME1400AB_8254_B_0_VAL_REG + ctr_idx);
	}

	return 0;
}

static uint32_t me1400AB_get_ctrl_reg(uint32_t reg_base,
				      unsigned int me8254_idx,
				      unsigned int ctr_idx)
{
	switch (me8254_idx) {
	case 0:
		return (reg_base + ME1400AB_8254_A_CTRL_REG);

	default:
		return (reg_base + ME1400AB_8254_B_CTRL_REG);
	}

	return 0;
}

static uint32_t me1400AB_get_clk_src_reg(uint32_t reg_base,
					 unsigned int me8254_idx,
					 unsigned int ctr_idx)
{
	switch (me8254_idx) {
	case 0:
		return (reg_base + ME1400AB_CLK_SRC_REG);

	default:
		return (reg_base + ME1400AB_CLK_SRC_REG);
	}

	return 0;
}

static uint32_t me1400CD_get_val_reg(uint32_t reg_base, unsigned int me8254_idx,
				     unsigned int ctr_idx)
{
	switch (me8254_idx) {
	case 0:
		return (reg_base + ME1400C_8254_A_0_VAL_REG + ctr_idx);

	case 1:
		return (reg_base + ME1400C_8254_B_0_VAL_REG + ctr_idx);

	case 2:
		return (reg_base + ME1400C_8254_C_0_VAL_REG + ctr_idx);

	case 3:
		return (reg_base + ME1400C_8254_D_0_VAL_REG + ctr_idx);

	case 4:
		return (reg_base + ME1400C_8254_E_0_VAL_REG + ctr_idx);

	case 5:
		return (reg_base + ME1400D_8254_A_0_VAL_REG + ctr_idx);

	case 6:
		return (reg_base + ME1400D_8254_B_0_VAL_REG + ctr_idx);

	case 7:
		return (reg_base + ME1400D_8254_C_0_VAL_REG + ctr_idx);

	case 8:
		return (reg_base + ME1400D_8254_D_0_VAL_REG + ctr_idx);

	default:
		return (reg_base + ME1400D_8254_E_0_VAL_REG + ctr_idx);
	}

	return 0;
}

static uint32_t me1400CD_get_ctrl_reg(uint32_t reg_base,
				      unsigned int me8254_idx,
				      unsigned int ctr_idx)
{
	switch (me8254_idx) {
	case 0:
		return (reg_base + ME1400C_8254_A_CTRL_REG);

	case 1:
		return (reg_base + ME1400C_8254_B_CTRL_REG);

	case 2:
		return (reg_base + ME1400C_8254_C_CTRL_REG);

	case 3:
		return (reg_base + ME1400C_8254_D_CTRL_REG);

	case 4:
		return (reg_base + ME1400C_8254_E_CTRL_REG);

	case 5:
		return (reg_base + ME1400D_8254_A_CTRL_REG);

	case 6:
		return (reg_base + ME1400D_8254_B_CTRL_REG);

	case 7:
		return (reg_base + ME1400D_8254_C_CTRL_REG);

	case 8:
		return (reg_base + ME1400D_8254_D_CTRL_REG);

	default:
		return (reg_base + ME1400D_8254_E_CTRL_REG);
	}

	return 0;
}

static uint32_t me1400CD_get_clk_src_reg(uint32_t reg_base,
					 unsigned int me8254_idx,
					 unsigned int ctr_idx)
{
	switch (me8254_idx) {
	case 0:
		return (reg_base + ME1400C_CLK_SRC_0_REG);

	case 1:
		return (reg_base + ME1400C_CLK_SRC_0_REG);

	case 2:
		return (reg_base + ME1400C_CLK_SRC_1_REG);

	case 3:
		return (reg_base + ME1400C_CLK_SRC_1_REG);

	case 4:
		return (reg_base + ME1400C_CLK_SRC_2_REG);

	case 5:
		return (reg_base + ME1400D_CLK_SRC_0_REG);

	case 6:
		return (reg_base + ME1400D_CLK_SRC_0_REG);

	case 7:
		return (reg_base + ME1400D_CLK_SRC_1_REG);

	case 8:
		return (reg_base + ME1400D_CLK_SRC_1_REG);

	default:
		return (reg_base + ME1400D_CLK_SRC_2_REG);
	}

	return 0;
}

static uint32_t me4600_get_val_reg(uint32_t reg_base, unsigned int me8254_idx,
				   unsigned int ctr_idx)
{
	return (reg_base + ME4600_8254_0_VAL_REG + ctr_idx);
}

static uint32_t me4600_get_ctrl_reg(uint32_t reg_base, unsigned int me8254_idx,
				    unsigned int ctr_idx)
{
	return (reg_base + ME4600_8254_CTRL_REG);
}

static uint32_t me8100_get_val_reg(uint32_t reg_base, unsigned int me8254_idx,
				   unsigned int ctr_idx)
{
	return (reg_base + ME8100_COUNTER_REG_0 + ctr_idx * 2);
}

static uint32_t me8100_get_ctrl_reg(uint32_t reg_base, unsigned int me8254_idx,
				    unsigned int ctr_idx)
{
	return (reg_base + ME8100_COUNTER_CTRL_REG);
}

me8254_subdevice_t *me8254_constructor(uint32_t device_id,
				       uint32_t reg_base,
				       unsigned int me8254_idx,
				       unsigned int ctr_idx,
				       spinlock_t * ctrl_reg_lock,
				       spinlock_t * clk_src_reg_lock)
{
	me8254_subdevice_t *subdevice;
	int err;

	PDEBUG("executed.\n");

	// Allocate memory for subdevice instance
	subdevice = kmalloc(sizeof(me8254_subdevice_t), GFP_KERNEL);

	if (!subdevice) {
		PERROR("Cannot get memory for 8254 instance.\n");
		return NULL;
	}

	memset(subdevice, 0, sizeof(me8254_subdevice_t));

	// Check if counter index is out of range

	if (ctr_idx > 2) {
		PERROR("Counter index is out of range.\n");
		kfree(subdevice);
		return NULL;
	}
	// Initialize subdevice base class
	err = me_subdevice_init(&subdevice->base);

	if (err) {
		PERROR("Cannot initialize subdevice base class instance.\n");
		kfree(subdevice);
		return NULL;
	}
	// Initialize spin locks.
	spin_lock_init(&subdevice->subdevice_lock);
	subdevice->ctrl_reg_lock = ctrl_reg_lock;
	subdevice->clk_src_reg_lock = clk_src_reg_lock;

	// Save type of Meilhaus device
	subdevice->device_id = device_id;

	// Save the indices
	subdevice->me8254_idx = me8254_idx;
	subdevice->ctr_idx = ctr_idx;

	// Do device specific initialization
	switch (device_id) {

	case PCI_DEVICE_ID_MEILHAUS_ME140A:
	case PCI_DEVICE_ID_MEILHAUS_ME14EA:
		// Check if 8254 index is out of range
		if (me8254_idx > 0) {
			PERROR("8254 index is out of range.\n");
			me_subdevice_deinit(&subdevice->base);
			kfree(subdevice);
			return NULL;
		}

	case PCI_DEVICE_ID_MEILHAUS_ME140B:	// Fall through
	case PCI_DEVICE_ID_MEILHAUS_ME14EB:
		// Check if 8254 index is out of range
		if (me8254_idx > 1) {
			PERROR("8254 index is out of range.\n");
			me_subdevice_deinit(&subdevice->base);
			kfree(subdevice);
			return NULL;
		}
		// Initialize the counters capabilities
		if (ctr_idx == 0)
			subdevice->caps =
			    ME_CAPS_CTR_CLK_INTERNAL_1MHZ |
			    ME_CAPS_CTR_CLK_INTERNAL_10MHZ |
			    ME_CAPS_CTR_CLK_EXTERNAL;
		else
			subdevice->caps =
			    ME_CAPS_CTR_CLK_PREVIOUS | ME_CAPS_CTR_CLK_EXTERNAL;

		// Get the counters registers
		subdevice->val_reg =
		    me1400AB_get_val_reg(reg_base, me8254_idx, ctr_idx);
		subdevice->ctrl_reg =
		    me1400AB_get_ctrl_reg(reg_base, me8254_idx, ctr_idx);
		subdevice->clk_src_reg =
		    me1400AB_get_clk_src_reg(reg_base, me8254_idx, ctr_idx);
		break;

	case PCI_DEVICE_ID_MEILHAUS_ME140C:
		// Check if 8254 index is out of range
		if (me8254_idx > 4) {
			PERROR("8254 index is out of range.\n");
			me_subdevice_deinit(&subdevice->base);
			kfree(subdevice);
			return NULL;
		}

	case PCI_DEVICE_ID_MEILHAUS_ME140D:
		// Check if 8254 index is out of range
		if (me8254_idx > 9) {
			PERROR("8254 index is out of range.\n");
			me_subdevice_deinit(&subdevice->base);
			kfree(subdevice);
			return NULL;
		}
		// Initialize the counters capabilities
		if (ctr_idx == 0) {
			if (me8254_idx == 0)
				subdevice->caps =
				    ME_CAPS_CTR_CLK_PREVIOUS |
				    ME_CAPS_CTR_CLK_INTERNAL_1MHZ |
				    ME_CAPS_CTR_CLK_INTERNAL_10MHZ |
				    ME_CAPS_CTR_CLK_EXTERNAL;
			else
				subdevice->caps =
				    ME_CAPS_CTR_CLK_INTERNAL_1MHZ |
				    ME_CAPS_CTR_CLK_INTERNAL_10MHZ |
				    ME_CAPS_CTR_CLK_EXTERNAL;
		} else
			subdevice->caps =
			    ME_CAPS_CTR_CLK_PREVIOUS | ME_CAPS_CTR_CLK_EXTERNAL;

		// Get the counters registers
		subdevice->val_reg =
		    me1400CD_get_val_reg(reg_base, me8254_idx, ctr_idx);
		subdevice->ctrl_reg =
		    me1400CD_get_ctrl_reg(reg_base, me8254_idx, ctr_idx);
		subdevice->clk_src_reg =
		    me1400CD_get_clk_src_reg(reg_base, me8254_idx, ctr_idx);
		break;

	case PCI_DEVICE_ID_MEILHAUS_ME4610:
	case PCI_DEVICE_ID_MEILHAUS_ME4660:
	case PCI_DEVICE_ID_MEILHAUS_ME4660I:
	case PCI_DEVICE_ID_MEILHAUS_ME4660S:
	case PCI_DEVICE_ID_MEILHAUS_ME4660IS:
	case PCI_DEVICE_ID_MEILHAUS_ME4670:
	case PCI_DEVICE_ID_MEILHAUS_ME4670I:
	case PCI_DEVICE_ID_MEILHAUS_ME4670S:
	case PCI_DEVICE_ID_MEILHAUS_ME4670IS:
	case PCI_DEVICE_ID_MEILHAUS_ME4680:
	case PCI_DEVICE_ID_MEILHAUS_ME4680I:
	case PCI_DEVICE_ID_MEILHAUS_ME4680S:
	case PCI_DEVICE_ID_MEILHAUS_ME4680IS:
		// Check if 8254 index is out of range
		if (me8254_idx > 0) {
			PERROR("8254 index is out of range.\n");
			me_subdevice_deinit(&subdevice->base);
			kfree(subdevice);
			return NULL;
		}
		// Initialize the counters capabilities
		subdevice->caps = ME_CAPS_CTR_CLK_EXTERNAL;

		// Get the counters registers
		subdevice->val_reg =
		    me4600_get_val_reg(reg_base, me8254_idx, ctr_idx);
		subdevice->ctrl_reg =
		    me4600_get_ctrl_reg(reg_base, me8254_idx, ctr_idx);
		subdevice->clk_src_reg = 0;	// Not used
		break;

	case PCI_DEVICE_ID_MEILHAUS_ME8100_A:
	case PCI_DEVICE_ID_MEILHAUS_ME8100_B:
		// Check if 8254 index is out of range
		if (me8254_idx > 0) {
			PERROR("8254 index is out of range.\n");
			me_subdevice_deinit(&subdevice->base);
			kfree(subdevice);
			return NULL;
		}
		// Initialize the counters capabilities
		subdevice->caps = ME_CAPS_CTR_CLK_EXTERNAL;

		// Get the counters registers
		subdevice->val_reg =
		    me8100_get_val_reg(reg_base, me8254_idx, ctr_idx);
		subdevice->ctrl_reg =
		    me8100_get_ctrl_reg(reg_base, me8254_idx, ctr_idx);
		subdevice->clk_src_reg = 0;	// Not used
		break;

	case PCI_DEVICE_ID_MEILHAUS_ME4650:
	case PCI_DEVICE_ID_MEILHAUS_ME1400:
	case PCI_DEVICE_ID_MEILHAUS_ME14E0:
		PERROR("No 8254 subdevices available for subdevice device.\n");
		me_subdevice_deinit(&subdevice->base);
		kfree(subdevice);
		return NULL;

	default:
		PERROR("Unknown device type.\n");
		me_subdevice_deinit(&subdevice->base);
		kfree(subdevice);
		return NULL;
	}

	// Overload subdevice base class methods.
	subdevice->base.me_subdevice_io_reset_subdevice =
	    me8254_io_reset_subdevice;
	subdevice->base.me_subdevice_io_single_config = me8254_io_single_config;
	subdevice->base.me_subdevice_io_single_read = me8254_io_single_read;
	subdevice->base.me_subdevice_io_single_write = me8254_io_single_write;
	subdevice->base.me_subdevice_query_number_channels =
	    me8254_query_number_channels;
	subdevice->base.me_subdevice_query_subdevice_type =
	    me8254_query_subdevice_type;
	subdevice->base.me_subdevice_query_subdevice_caps =
	    me8254_query_subdevice_caps;
	subdevice->base.me_subdevice_query_subdevice_caps_args =
	    me8254_query_subdevice_caps_args;

	return subdevice;
}
