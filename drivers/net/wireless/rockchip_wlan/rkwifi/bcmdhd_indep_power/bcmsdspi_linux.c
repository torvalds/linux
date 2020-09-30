/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Broadcom SPI Host Controller Driver - Linux Per-port
 *
 * Copyright (C) 1999-2017, Broadcom Corporation
 * 
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 * 
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 * 
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id: bcmsdspi_linux.c 514727 2014-11-12 03:02:48Z $
 */

#include <typedefs.h>
#include <bcmutils.h>

#include <bcmsdbus.h>		/* bcmsdh to/from specific controller APIs */
#include <sdiovar.h>		/* to get msglevel bit values */

#include <pcicfg.h>
#include <sdio.h>		/* SDIO Device and Protocol Specs */
#include <linux/sched.h>	/* request_irq(), free_irq() */
#include <bcmsdspi.h>
#include <bcmspi.h>

extern uint sd_crc;
module_param(sd_crc, uint, 0);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0))
#define KERNEL26
#endif

struct sdos_info {
	sdioh_info_t *sd;
	spinlock_t lock;
	wait_queue_head_t intr_wait_queue;
};

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0))
#define BLOCKABLE()	(!in_atomic())
#else
#define BLOCKABLE()	(!in_interrupt())
#endif

/* Interrupt handler */
static irqreturn_t
sdspi_isr(int irq, void *dev_id
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 20)
, struct pt_regs *ptregs
#endif
)
{
	sdioh_info_t *sd;
	struct sdos_info *sdos;
	bool ours;

	sd = (sdioh_info_t *)dev_id;
	sd->local_intrcount++;

	if (!sd->card_init_done) {
		sd_err(("%s: Hey Bogus intr...not even initted: irq %d\n", __FUNCTION__, irq));
		return IRQ_RETVAL(FALSE);
	} else {
		ours = spi_check_client_intr(sd, NULL);

		/* For local interrupts, wake the waiting process */
		if (ours && sd->got_hcint) {
			sdos = (struct sdos_info *)sd->sdos_info;
			wake_up_interruptible(&sdos->intr_wait_queue);
		}

		return IRQ_RETVAL(ours);
	}
}


/* Register with Linux for interrupts */
int
spi_register_irq(sdioh_info_t *sd, uint irq)
{
	sd_trace(("Entering %s: irq == %d\n", __FUNCTION__, irq));
	if (request_irq(irq, sdspi_isr, IRQF_SHARED, "bcmsdspi", sd) < 0) {
		sd_err(("%s: request_irq() failed\n", __FUNCTION__));
		return ERROR;
	}
	return SUCCESS;
}

/* Free Linux irq */
void
spi_free_irq(uint irq, sdioh_info_t *sd)
{
	free_irq(irq, sd);
}

/* Map Host controller registers */
uint32 *
spi_reg_map(osl_t *osh, uintptr addr, int size)
{
	return (uint32 *)REG_MAP(addr, size);
}

void
spi_reg_unmap(osl_t *osh, uintptr addr, int size)
{
	REG_UNMAP((void*)(uintptr)addr);
}

int
spi_osinit(sdioh_info_t *sd)
{
	struct sdos_info *sdos;

	sdos = (struct sdos_info*)MALLOC(sd->osh, sizeof(struct sdos_info));
	sd->sdos_info = (void*)sdos;
	if (sdos == NULL)
		return BCME_NOMEM;

	sdos->sd = sd;
	spin_lock_init(&sdos->lock);
	init_waitqueue_head(&sdos->intr_wait_queue);
	return BCME_OK;
}

void
spi_osfree(sdioh_info_t *sd)
{
	struct sdos_info *sdos;
	ASSERT(sd && sd->sdos_info);

	sdos = (struct sdos_info *)sd->sdos_info;
	MFREE(sd->osh, sdos, sizeof(struct sdos_info));
}

/* Interrupt enable/disable */
SDIOH_API_RC
sdioh_interrupt_set(sdioh_info_t *sd, bool enable)
{
	ulong flags;
	struct sdos_info *sdos;

	sd_trace(("%s: %s\n", __FUNCTION__, enable ? "Enabling" : "Disabling"));

	sdos = (struct sdos_info *)sd->sdos_info;
	ASSERT(sdos);

	if (!(sd->host_init_done && sd->card_init_done)) {
		sd_err(("%s: Card & Host are not initted - bailing\n", __FUNCTION__));
		return SDIOH_API_RC_FAIL;
	}

	if (enable && !(sd->intr_handler && sd->intr_handler_arg)) {
		sd_err(("%s: no handler registered, will not enable\n", __FUNCTION__));
		return SDIOH_API_RC_FAIL;
	}

	/* Ensure atomicity for enable/disable calls */
	spin_lock_irqsave(&sdos->lock, flags);

	sd->client_intr_enabled = enable;
	if (enable && !sd->lockcount)
		spi_devintr_on(sd);
	else
		spi_devintr_off(sd);

	spin_unlock_irqrestore(&sdos->lock, flags);

	return SDIOH_API_RC_SUCCESS;
}

/* Protect against reentrancy (disable device interrupts while executing) */
void
spi_lock(sdioh_info_t *sd)
{
	ulong flags;
	struct sdos_info *sdos;

	sdos = (struct sdos_info *)sd->sdos_info;
	ASSERT(sdos);

	sd_trace(("%s: %d\n", __FUNCTION__, sd->lockcount));

	spin_lock_irqsave(&sdos->lock, flags);
	if (sd->lockcount) {
		sd_err(("%s: Already locked!\n", __FUNCTION__));
		ASSERT(sd->lockcount == 0);
	}
	spi_devintr_off(sd);
	sd->lockcount++;
	spin_unlock_irqrestore(&sdos->lock, flags);
}

/* Enable client interrupt */
void
spi_unlock(sdioh_info_t *sd)
{
	ulong flags;
	struct sdos_info *sdos;

	sd_trace(("%s: %d, %d\n", __FUNCTION__, sd->lockcount, sd->client_intr_enabled));
	ASSERT(sd->lockcount > 0);

	sdos = (struct sdos_info *)sd->sdos_info;
	ASSERT(sdos);

	spin_lock_irqsave(&sdos->lock, flags);
	if (--sd->lockcount == 0 && sd->client_intr_enabled) {
		spi_devintr_on(sd);
	}
	spin_unlock_irqrestore(&sdos->lock, flags);
}

void spi_waitbits(sdioh_info_t *sd, bool yield)
{
#ifndef BCMSDYIELD
	ASSERT(!yield);
#endif
	sd_trace(("%s: yield %d canblock %d\n",
	          __FUNCTION__, yield, BLOCKABLE()));

	/* Clear the "interrupt happened" flag and last intrstatus */
	sd->got_hcint = FALSE;

#ifdef BCMSDYIELD
	if (yield && BLOCKABLE()) {
		struct sdos_info *sdos;
		sdos = (struct sdos_info *)sd->sdos_info;
		/* Wait for the indication, the interrupt will be masked when the ISR fires. */
		wait_event_interruptible(sdos->intr_wait_queue, (sd->got_hcint));
	} else
#endif /* BCMSDYIELD */
	{
		spi_spinbits(sd);
	}

}
