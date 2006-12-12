/*
 *  Copyright (C) 2002 Intersil Americas Inc.
 *  Copyright (C) 2003-2004 Luis R. Rodriguez <mcgrof@ruslug.rutgers.edu>_
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/delay.h>

#include <asm/uaccess.h>
#include <asm/io.h>

#include "prismcompat.h"
#include "isl_38xx.h"
#include "islpci_dev.h"
#include "islpci_mgt.h"

/******************************************************************************
    Device Interface & Control functions
******************************************************************************/

/**
 * isl38xx_disable_interrupts - disable all interrupts
 * @device: pci memory base address
 *
 *  Instructs the device to disable all interrupt reporting by asserting
 *  the IRQ line. New events may still show up in the interrupt identification
 *  register located at offset %ISL38XX_INT_IDENT_REG.
 */
void
isl38xx_disable_interrupts(void __iomem *device)
{
	isl38xx_w32_flush(device, 0x00000000, ISL38XX_INT_EN_REG);
	udelay(ISL38XX_WRITEIO_DELAY);
}

void
isl38xx_handle_sleep_request(isl38xx_control_block *control_block,
			     int *powerstate, void __iomem *device_base)
{
	/* device requests to go into sleep mode
	 * check whether the transmit queues for data and management are empty */
	if (isl38xx_in_queue(control_block, ISL38XX_CB_TX_DATA_LQ))
		/* data tx queue not empty */
		return;

	if (isl38xx_in_queue(control_block, ISL38XX_CB_TX_MGMTQ))
		/* management tx queue not empty */
		return;

	/* check also whether received frames are pending */
	if (isl38xx_in_queue(control_block, ISL38XX_CB_RX_DATA_LQ))
		/* data rx queue not empty */
		return;

	if (isl38xx_in_queue(control_block, ISL38XX_CB_RX_MGMTQ))
		/* management rx queue not empty */
		return;

#if VERBOSE > SHOW_ERROR_MESSAGES
	DEBUG(SHOW_TRACING, "Device going to sleep mode\n");
#endif

	/* all queues are empty, allow the device to go into sleep mode */
	*powerstate = ISL38XX_PSM_POWERSAVE_STATE;

	/* assert the Sleep interrupt in the Device Interrupt Register */
	isl38xx_w32_flush(device_base, ISL38XX_DEV_INT_SLEEP,
			  ISL38XX_DEV_INT_REG);
	udelay(ISL38XX_WRITEIO_DELAY);
}

void
isl38xx_handle_wakeup(isl38xx_control_block *control_block,
		      int *powerstate, void __iomem *device_base)
{
	/* device is in active state, update the powerstate flag */
	*powerstate = ISL38XX_PSM_ACTIVE_STATE;

	/* now check whether there are frames pending for the card */
	if (!isl38xx_in_queue(control_block, ISL38XX_CB_TX_DATA_LQ)
	    && !isl38xx_in_queue(control_block, ISL38XX_CB_TX_MGMTQ))
		return;

#if VERBOSE > SHOW_ERROR_MESSAGES
	DEBUG(SHOW_ANYTHING, "Wake up handler trigger the device\n");
#endif

	/* either data or management transmit queue has a frame pending
	 * trigger the device by setting the Update bit in the Device Int reg */
	isl38xx_w32_flush(device_base, ISL38XX_DEV_INT_UPDATE,
			  ISL38XX_DEV_INT_REG);
	udelay(ISL38XX_WRITEIO_DELAY);
}

void
isl38xx_trigger_device(int asleep, void __iomem *device_base)
{
	u32 reg;

#if VERBOSE > SHOW_ERROR_MESSAGES
	u32 counter = 0;
	struct timeval current_time;
	DEBUG(SHOW_FUNCTION_CALLS, "isl38xx trigger device\n");
#endif

	/* check whether the device is in power save mode */
	if (asleep) {
		/* device is in powersave, trigger the device for wakeup */
#if VERBOSE > SHOW_ERROR_MESSAGES
		do_gettimeofday(&current_time);
		DEBUG(SHOW_TRACING, "%08li.%08li Device wakeup triggered\n",
		      current_time.tv_sec, (long)current_time.tv_usec);

		DEBUG(SHOW_TRACING, "%08li.%08li Device register read %08x\n",
		      current_time.tv_sec, (long)current_time.tv_usec,
		      readl(device_base + ISL38XX_CTRL_STAT_REG));
#endif

		reg = readl(device_base + ISL38XX_INT_IDENT_REG);
		if (reg == 0xabadface) {
#if VERBOSE > SHOW_ERROR_MESSAGES
			do_gettimeofday(&current_time);
			DEBUG(SHOW_TRACING,
			      "%08li.%08li Device register abadface\n",
			      current_time.tv_sec, (long)current_time.tv_usec);
#endif
			/* read the Device Status Register until Sleepmode bit is set */
			while (reg = readl(device_base + ISL38XX_CTRL_STAT_REG),
			       (reg & ISL38XX_CTRL_STAT_SLEEPMODE) == 0) {
				udelay(ISL38XX_WRITEIO_DELAY);
#if VERBOSE > SHOW_ERROR_MESSAGES
				counter++;
#endif
			}

#if VERBOSE > SHOW_ERROR_MESSAGES
			DEBUG(SHOW_TRACING,
			      "%08li.%08li Device register read %08x\n",
			      current_time.tv_sec, (long)current_time.tv_usec,
			      readl(device_base + ISL38XX_CTRL_STAT_REG));
			do_gettimeofday(&current_time);
			DEBUG(SHOW_TRACING,
			      "%08li.%08li Device asleep counter %i\n",
			      current_time.tv_sec, (long)current_time.tv_usec,
			      counter);
#endif
		}
		/* assert the Wakeup interrupt in the Device Interrupt Register */
		isl38xx_w32_flush(device_base, ISL38XX_DEV_INT_WAKEUP,
				  ISL38XX_DEV_INT_REG);

#if VERBOSE > SHOW_ERROR_MESSAGES
		udelay(ISL38XX_WRITEIO_DELAY);

		/* perform another read on the Device Status Register */
		reg = readl(device_base + ISL38XX_CTRL_STAT_REG);
		do_gettimeofday(&current_time);
		DEBUG(SHOW_TRACING, "%08li.%08li Device register read %08x\n",
		      current_time.tv_sec, (long)current_time.tv_usec, reg);
#endif
	} else {
		/* device is (still) awake  */
#if VERBOSE > SHOW_ERROR_MESSAGES
		DEBUG(SHOW_TRACING, "Device is in active state\n");
#endif
		/* trigger the device by setting the Update bit in the Device Int reg */

		isl38xx_w32_flush(device_base, ISL38XX_DEV_INT_UPDATE,
				  ISL38XX_DEV_INT_REG);
	}
}

void
isl38xx_interface_reset(void __iomem *device_base, dma_addr_t host_address)
{
#if VERBOSE > SHOW_ERROR_MESSAGES
	DEBUG(SHOW_FUNCTION_CALLS, "isl38xx_interface_reset\n");
#endif

	/* load the address of the control block in the device */
	isl38xx_w32_flush(device_base, host_address, ISL38XX_CTRL_BLK_BASE_REG);
	udelay(ISL38XX_WRITEIO_DELAY);

	/* set the reset bit in the Device Interrupt Register */
	isl38xx_w32_flush(device_base, ISL38XX_DEV_INT_RESET, ISL38XX_DEV_INT_REG);
	udelay(ISL38XX_WRITEIO_DELAY);

	/* enable the interrupt for detecting initialization */

	/* Note: Do not enable other interrupts here. We want the
	 * device to have come up first 100% before allowing any other
	 * interrupts. */
	isl38xx_w32_flush(device_base, ISL38XX_INT_IDENT_INIT, ISL38XX_INT_EN_REG);
	udelay(ISL38XX_WRITEIO_DELAY);  /* allow complete full reset */
}

void
isl38xx_enable_common_interrupts(void __iomem *device_base)
{
	u32 reg;

	reg = ISL38XX_INT_IDENT_UPDATE | ISL38XX_INT_IDENT_SLEEP |
	      ISL38XX_INT_IDENT_WAKEUP;
	isl38xx_w32_flush(device_base, reg, ISL38XX_INT_EN_REG);
	udelay(ISL38XX_WRITEIO_DELAY);
}

int
isl38xx_in_queue(isl38xx_control_block *cb, int queue)
{
	const s32 delta = (le32_to_cpu(cb->driver_curr_frag[queue]) -
			   le32_to_cpu(cb->device_curr_frag[queue]));

	/* determine the amount of fragments in the queue depending on the type
	 * of the queue, either transmit or receive */

	BUG_ON(delta < 0);	/* driver ptr must be ahead of device ptr */

	switch (queue) {
		/* send queues */
	case ISL38XX_CB_TX_MGMTQ:
		BUG_ON(delta > ISL38XX_CB_MGMT_QSIZE);

	case ISL38XX_CB_TX_DATA_LQ:
	case ISL38XX_CB_TX_DATA_HQ:
		BUG_ON(delta > ISL38XX_CB_TX_QSIZE);
		return delta;

		/* receive queues */
	case ISL38XX_CB_RX_MGMTQ:
		BUG_ON(delta > ISL38XX_CB_MGMT_QSIZE);
		return ISL38XX_CB_MGMT_QSIZE - delta;

	case ISL38XX_CB_RX_DATA_LQ:
	case ISL38XX_CB_RX_DATA_HQ:
		BUG_ON(delta > ISL38XX_CB_RX_QSIZE);
		return ISL38XX_CB_RX_QSIZE - delta;
	}
	BUG();
	return 0;
}
