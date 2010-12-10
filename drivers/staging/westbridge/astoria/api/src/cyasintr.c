/* Cypress West Bridge API source file (cyasintr.c)
## ===========================
## Copyright (C) 2010  Cypress Semiconductor
##
## This program is free software; you can redistribute it and/or
## modify it under the terms of the GNU General Public License
## as published by the Free Software Foundation; either version 2
## of the License, or (at your option) any later version.
##
## This program is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
## GNU General Public License for more details.
##
## You should have received a copy of the GNU General Public License
## along with this program; if not, write to the Free Software
## Foundation, Inc., 51 Franklin Street, Fifth Floor
## Boston, MA  02110-1301, USA.
## ===========================
*/

#include "../../include/linux/westbridge/cyashal.h"
#include "../../include/linux/westbridge/cyasdevice.h"
#include "../../include/linux/westbridge/cyasregs.h"
#include "../../include/linux/westbridge/cyaserr.h"

extern void cy_as_mail_box_interrupt_handler(cy_as_device *);

void
cy_as_mcu_interrupt_handler(cy_as_device *dev_p)
{
	/* Read and clear the interrupt. */
	uint16_t v;

	v = cy_as_hal_read_register(dev_p->tag, CY_AS_MEM_P0_MCU_STAT);
	v = v;
}

void
cy_as_power_management_interrupt_handler(cy_as_device *dev_p)
{
	uint16_t v;

	v = cy_as_hal_read_register(dev_p->tag, CY_AS_MEM_PWR_MAGT_STAT);
	v = v;
}

void
cy_as_pll_lock_loss_interrupt_handler(cy_as_device *dev_p)
{
	uint16_t v;

	v = cy_as_hal_read_register(dev_p->tag, CY_AS_MEM_PLL_LOCK_LOSS_STAT);
	v = v;
}

uint32_t cy_as_intr_start(cy_as_device *dev_p, cy_bool dmaintr)
{
	uint16_t v;

	cy_as_hal_assert(dev_p->sig == CY_AS_DEVICE_HANDLE_SIGNATURE);

	if (cy_as_device_is_intr_running(dev_p) != 0)
		return CY_AS_ERROR_ALREADY_RUNNING;

	v = CY_AS_MEM_P0_INT_MASK_REG_MMCUINT |
		CY_AS_MEM_P0_INT_MASK_REG_MMBINT |
		CY_AS_MEM_P0_INT_MASK_REG_MPMINT;

	if (dmaintr)
		v |= CY_AS_MEM_P0_INT_MASK_REG_MDRQINT;

	/* Enable the interrupts of interest */
	cy_as_hal_write_register(dev_p->tag, CY_AS_MEM_P0_INT_MASK_REG, v);

	/* Mark the interrupt module as initialized */
	cy_as_device_set_intr_running(dev_p);

	return CY_AS_ERROR_SUCCESS;
}

uint32_t cy_as_intr_stop(cy_as_device *dev_p)
{
	cy_as_hal_assert(dev_p->sig == CY_AS_DEVICE_HANDLE_SIGNATURE);

	if (cy_as_device_is_intr_running(dev_p) == 0)
		return CY_AS_ERROR_NOT_RUNNING;

	cy_as_hal_write_register(dev_p->tag, CY_AS_MEM_P0_INT_MASK_REG, 0);
	cy_as_device_set_intr_stopped(dev_p);

	return CY_AS_ERROR_SUCCESS;
}

void cy_as_intr_service_interrupt(cy_as_hal_device_tag tag)
{
	uint16_t v;
	cy_as_device *dev_p;

	dev_p = cy_as_device_find_from_tag(tag);

	/*
	 * only power management interrupts can occur before the
	 * antioch API setup is complete. if this is a PM interrupt
	 *  handle it here; otherwise output a warning message.
	 */
	if (dev_p == 0) {
		v = cy_as_hal_read_register(tag, CY_AS_MEM_P0_INTR_REG);
		if (v == CY_AS_MEM_P0_INTR_REG_PMINT) {
			/* Read the PWR_MAGT_STAT register
			 * to clear this interrupt. */
			v = cy_as_hal_read_register(tag,
				CY_AS_MEM_PWR_MAGT_STAT);
		} else
			cy_as_hal_print_message("stray antioch "
				"interrupt detected"
				", tag not associated "
				"with any created device.");
		return;
	}

	/* Make sure we got a valid object from CyAsDeviceFindFromTag */
	cy_as_hal_assert(dev_p->sig == CY_AS_DEVICE_HANDLE_SIGNATURE);

	v = cy_as_hal_read_register(dev_p->tag, CY_AS_MEM_P0_INTR_REG);

	if (v & CY_AS_MEM_P0_INTR_REG_MCUINT)
		cy_as_mcu_interrupt_handler(dev_p);

	if (v & CY_AS_MEM_P0_INTR_REG_PMINT)
		cy_as_power_management_interrupt_handler(dev_p);

	if (v & CY_AS_MEM_P0_INTR_REG_PLLLOCKINT)
		cy_as_pll_lock_loss_interrupt_handler(dev_p);

	/* If the interrupt module is not running, no mailbox
	 * interrupts are expected from the west bridge. */
	if (cy_as_device_is_intr_running(dev_p) == 0)
		return;

	if (v & CY_AS_MEM_P0_INTR_REG_MBINT)
		cy_as_mail_box_interrupt_handler(dev_p);
}
