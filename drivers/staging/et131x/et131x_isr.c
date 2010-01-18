/*
 * Agere Systems Inc.
 * 10/100/1000 Base-T Ethernet Driver for the ET1301 and ET131x series MACs
 *
 * Copyright © 2005 Agere Systems Inc.
 * All rights reserved.
 *   http://www.agere.com
 *
 *------------------------------------------------------------------------------
 *
 * et131x_isr.c - File which contains the ISR, ISR handler, and related routines
 *                for processing interrupts from the device.
 *
 *------------------------------------------------------------------------------
 *
 * SOFTWARE LICENSE
 *
 * This software is provided subject to the following terms and conditions,
 * which you should read carefully before using the software.  Using this
 * software indicates your acceptance of these terms and conditions.  If you do
 * not agree with these terms and conditions, do not use the software.
 *
 * Copyright © 2005 Agere Systems Inc.
 * All rights reserved.
 *
 * Redistribution and use in source or binary forms, with or without
 * modifications, are permitted provided that the following conditions are met:
 *
 * . Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following Disclaimer as comments in the code as
 *    well as in the documentation and/or other materials provided with the
 *    distribution.
 *
 * . Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following Disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * . Neither the name of Agere Systems Inc. nor the names of the contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * Disclaimer
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, INFRINGEMENT AND THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  ANY
 * USE, MODIFICATION OR DISTRIBUTION OF THIS SOFTWARE IS SOLELY AT THE USERS OWN
 * RISK. IN NO EVENT SHALL AGERE SYSTEMS INC. OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, INCLUDING, BUT NOT LIMITED TO, CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 *
 */

#include "et131x_version.h"
#include "et131x_defs.h"

#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>

#include <linux/sched.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/ctype.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/in.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/bitops.h>
#include <linux/pci.h>
#include <asm/system.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/if_arp.h>
#include <linux/ioport.h>

#include "et1310_phy.h"
#include "et131x_adapter.h"
#include "et131x.h"

/*
 * For interrupts, normal running is:
 *       rxdma_xfr_done, phy_interrupt, mac_stat_interrupt,
 *       watchdog_interrupt & txdma_xfer_done
 *
 * In both cases, when flow control is enabled for either Tx or bi-direction,
 * we additional enable rx_fbr0_low and rx_fbr1_low, so we know when the
 * buffer rings are running low.
 */
#define INT_MASK_DISABLE            0xffffffff

/* NOTE: Masking out MAC_STAT Interrupt for now...
 * #define INT_MASK_ENABLE             0xfff6bf17
 * #define INT_MASK_ENABLE_NO_FLOW     0xfff6bfd7
 */
#define INT_MASK_ENABLE             0xfffebf17
#define INT_MASK_ENABLE_NO_FLOW     0xfffebfd7


/**
 *	et131x_enable_interrupts	-	enable interrupt
 *	@adapter: et131x device
 *
 *	Enable the appropriate interrupts on the ET131x according to our
 *	configuration
 */

void et131x_enable_interrupts(struct et131x_adapter *adapter)
{
	u32 mask;

	/* Enable all global interrupts */
	if (adapter->FlowControl == TxOnly || adapter->FlowControl == Both)
		mask = INT_MASK_ENABLE;
	else
		mask = INT_MASK_ENABLE_NO_FLOW;

	adapter->CachedMaskValue = mask;
	writel(mask, &adapter->regs->global.int_mask);
}

/**
 *	et131x_disable_interrupts	-	interrupt disable
 *	@adapter: et131x device
 *
 *	Block all interrupts from the et131x device at the device itself
 */

void et131x_disable_interrupts(struct et131x_adapter *adapter)
{
	/* Disable all global interrupts */
	adapter->CachedMaskValue = INT_MASK_DISABLE;
	writel(INT_MASK_DISABLE, &adapter->regs->global.int_mask);
}


/**
 * et131x_isr - The Interrupt Service Routine for the driver.
 * @irq: the IRQ on which the interrupt was received.
 * @dev_id: device-specific info (here a pointer to a net_device struct)
 *
 * Returns a value indicating if the interrupt was handled.
 */

irqreturn_t et131x_isr(int irq, void *dev_id)
{
	bool handled = true;
	struct net_device *netdev = (struct net_device *)dev_id;
	struct et131x_adapter *adapter = NULL;
	u32 status;

	if (!netif_device_present(netdev)) {
		handled = false;
		goto out;
	}

	adapter = netdev_priv(netdev);

	/* If the adapter is in low power state, then it should not
	 * recognize any interrupt
	 */

	/* Disable Device Interrupts */
	et131x_disable_interrupts(adapter);

	/* Get a copy of the value in the interrupt status register
	 * so we can process the interrupting section
	 */
	status = readl(&adapter->regs->global.int_status);

	if (adapter->FlowControl == TxOnly ||
	    adapter->FlowControl == Both) {
		status &= ~INT_MASK_ENABLE;
	} else {
		status &= ~INT_MASK_ENABLE_NO_FLOW;
	}

	/* Make sure this is our interrupt */
	if (!status) {
		handled = false;
		et131x_enable_interrupts(adapter);
		goto out;
	}

	/* This is our interrupt, so process accordingly */

	if (status & ET_INTR_WATCHDOG) {
		struct tcb *tcb = adapter->tx_ring.send_head;

		if (tcb)
			if (++tcb->stale > 1)
				status |= ET_INTR_TXDMA_ISR;

		if (adapter->rx_ring.UnfinishedReceives)
			status |= ET_INTR_RXDMA_XFR_DONE;
		else if (tcb == NULL)
			writel(0, &adapter->regs->global.watchdog_timer);

		status &= ~ET_INTR_WATCHDOG;
	}

	if (status == 0) {
		/* This interrupt has in some way been "handled" by
		 * the ISR. Either it was a spurious Rx interrupt, or
		 * it was a Tx interrupt that has been filtered by
		 * the ISR.
		 */
		et131x_enable_interrupts(adapter);
		goto out;
	}

	/* We need to save the interrupt status value for use in our
	 * DPC. We will clear the software copy of that in that
	 * routine.
	 */
	adapter->Stats.InterruptStatus = status;

	/* Schedule the ISR handler as a bottom-half task in the
	 * kernel's tq_immediate queue, and mark the queue for
	 * execution
	 */
	schedule_work(&adapter->task);
out:
	return IRQ_RETVAL(handled);
}

/**
 * et131x_isr_handler - The ISR handler
 * @p_adapter, a pointer to the device's private adapter structure
 *
 * scheduled to run in a deferred context by the ISR. This is where the ISR's
 * work actually gets done.
 */
void et131x_isr_handler(struct work_struct *work)
{
	struct et131x_adapter *etdev =
		container_of(work, struct et131x_adapter, task);
	u32 status = etdev->Stats.InterruptStatus;
	ADDRESS_MAP_t __iomem *iomem = etdev->regs;

	/*
	 * These first two are by far the most common.  Once handled, we clear
	 * their two bits in the status word.  If the word is now zero, we
	 * exit.
	 */
	/* Handle all the completed Transmit interrupts */
	if (status & ET_INTR_TXDMA_ISR) {
		et131x_handle_send_interrupt(etdev);
	}

	/* Handle all the completed Receives interrupts */
	if (status & ET_INTR_RXDMA_XFR_DONE) {
		et131x_handle_recv_interrupt(etdev);
	}

	status &= 0xffffffd7;

	if (status) {
		/* Handle the TXDMA Error interrupt */
		if (status & ET_INTR_TXDMA_ERR) {
			u32 txdma_err;

			/* Following read also clears the register (COR) */
			txdma_err = readl(&iomem->txdma.TxDmaError);

			dev_warn(&etdev->pdev->dev,
				    "TXDMA_ERR interrupt, error = %d\n",
				    txdma_err);
		}

		/* Handle Free Buffer Ring 0 and 1 Low interrupt */
		if (status & (ET_INTR_RXDMA_FB_R0_LOW | ET_INTR_RXDMA_FB_R1_LOW)) {
			/*
			 * This indicates the number of unused buffers in
			 * RXDMA free buffer ring 0 is <= the limit you
			 * programmed. Free buffer resources need to be
			 * returned.  Free buffers are consumed as packets
			 * are passed from the network to the host. The host
			 * becomes aware of the packets from the contents of
			 * the packet status ring. This ring is queried when
			 * the packet done interrupt occurs. Packets are then
			 * passed to the OS. When the OS is done with the
			 * packets the resources can be returned to the
			 * ET1310 for re-use. This interrupt is one method of
			 * returning resources.
			 */

			/* If the user has flow control on, then we will
			 * send a pause packet, otherwise just exit
			 */
			if (etdev->FlowControl == TxOnly ||
			    etdev->FlowControl == Both) {
				u32 pm_csr;

				/* Tell the device to send a pause packet via
				 * the back pressure register (bp req  and
				 * bp xon/xoff)
				 */
				pm_csr = readl(&iomem->global.pm_csr);
				if ((pm_csr & ET_PM_PHY_SW_COMA) == 0)
					writel(3, &iomem->txmac.bp_ctrl);
			}
		}

		/* Handle Packet Status Ring Low Interrupt */
		if (status & ET_INTR_RXDMA_STAT_LOW) {

			/*
			 * Same idea as with the two Free Buffer Rings.
			 * Packets going from the network to the host each
			 * consume a free buffer resource and a packet status
			 * resource.  These resoures are passed to the OS.
			 * When the OS is done with the resources, they need
			 * to be returned to the ET1310. This is one method
			 * of returning the resources.
			 */
		}

		/* Handle RXDMA Error Interrupt */
		if (status & ET_INTR_RXDMA_ERR) {
			/*
			 * The rxdma_error interrupt is sent when a time-out
			 * on a request issued by the JAGCore has occurred or
			 * a completion is returned with an un-successful
			 * status.  In both cases the request is considered
			 * complete. The JAGCore will automatically re-try the
			 * request in question. Normally information on events
			 * like these are sent to the host using the "Advanced
			 * Error Reporting" capability. This interrupt is
			 * another way of getting similar information. The
			 * only thing required is to clear the interrupt by
			 * reading the ISR in the global resources. The
			 * JAGCore will do a re-try on the request.  Normally
			 * you should never see this interrupt. If you start
			 * to see this interrupt occurring frequently then
			 * something bad has occurred. A reset might be the
			 * thing to do.
			 */
			/* TRAP();*/

			dev_warn(&etdev->pdev->dev,
				    "RxDMA_ERR interrupt, error %x\n",
				    readl(&iomem->txmac.tx_test));
		}

		/* Handle the Wake on LAN Event */
		if (status & ET_INTR_WOL) {
			/*
			 * This is a secondary interrupt for wake on LAN.
			 * The driver should never see this, if it does,
			 * something serious is wrong. We will TRAP the
			 * message when we are in DBG mode, otherwise we
			 * will ignore it.
			 */
			dev_err(&etdev->pdev->dev, "WAKE_ON_LAN interrupt\n");
		}

		/* Handle the PHY interrupt */
		if (status & ET_INTR_PHY) {
			u32 pm_csr;
			MI_BMSR_t BmsrInts, BmsrData;
			MI_ISR_t myIsr;

			/* If we are in coma mode when we get this interrupt,
			 * we need to disable it.
			 */
			pm_csr = readl(&iomem->global.pm_csr);
			if (pm_csr & ET_PM_PHY_SW_COMA) {
				/*
				 * Check to see if we are in coma mode and if
				 * so, disable it because we will not be able
				 * to read PHY values until we are out.
				 */
				DisablePhyComa(etdev);
			}

			/* Read the PHY ISR to clear the reason for the
			 * interrupt.
			 */
			MiRead(etdev, (uint8_t) offsetof(MI_REGS_t, isr),
			       &myIsr.value);

			if (!etdev->ReplicaPhyLoopbk) {
				MiRead(etdev,
				       (uint8_t) offsetof(MI_REGS_t, bmsr),
				       &BmsrData.value);

				BmsrInts.value =
				    etdev->Bmsr.value ^ BmsrData.value;
				etdev->Bmsr.value = BmsrData.value;

				/* Do all the cable in / cable out stuff */
				et131x_Mii_check(etdev, BmsrData, BmsrInts);
			}
		}

		/* Let's move on to the TxMac */
		if (status & ET_INTR_TXMAC) {
			u32 err = readl(&iomem->txmac.err);

			/*
			 * When any of the errors occur and TXMAC generates
			 * an interrupt to report these errors, it usually
			 * means that TXMAC has detected an error in the data
			 * stream retrieved from the on-chip Tx Q. All of
			 * these errors are catastrophic and TXMAC won't be
			 * able to recover data when these errors occur.  In
			 * a nutshell, the whole Tx path will have to be reset
			 * and re-configured afterwards.
			 */
			dev_warn(&etdev->pdev->dev,
				    "TXMAC interrupt, error 0x%08x\n",
				    err);

			/* If we are debugging, we want to see this error,
			 * otherwise we just want the device to be reset and
			 * continue
			 */
		}

		/* Handle RXMAC Interrupt */
		if (status & ET_INTR_RXMAC) {
			/*
			 * These interrupts are catastrophic to the device,
			 * what we need to do is disable the interrupts and
			 * set the flag to cause us to reset so we can solve
			 * this issue.
			 */
			/* MP_SET_FLAG( etdev,
						fMP_ADAPTER_HARDWARE_ERROR); */

			dev_warn(&etdev->pdev->dev,
			  "RXMAC interrupt, error 0x%08x.  Requesting reset\n",
				    readl(&iomem->rxmac.err_reg.value));

			dev_warn(&etdev->pdev->dev,
				    "Enable 0x%08x, Diag 0x%08x\n",
				    readl(&iomem->rxmac.ctrl.value),
				    readl(&iomem->rxmac.rxq_diag));

			/*
			 * If we are debugging, we want to see this error,
			 * otherwise we just want the device to be reset and
			 * continue
			 */
		}

		/* Handle MAC_STAT Interrupt */
		if (status & ET_INTR_MAC_STAT) {
			/*
			 * This means at least one of the un-masked counters
			 * in the MAC_STAT block has rolled over.  Use this
			 * to maintain the top, software managed bits of the
			 * counter(s).
			 */
			HandleMacStatInterrupt(etdev);
		}

		/* Handle SLV Timeout Interrupt */
		if (status & ET_INTR_SLV_TIMEOUT) {
			/*
			 * This means a timeout has occured on a read or
			 * write request to one of the JAGCore registers. The
			 * Global Resources block has terminated the request
			 * and on a read request, returned a "fake" value.
			 * The most likely reasons are: Bad Address or the
			 * addressed module is in a power-down state and
			 * can't respond.
			 */
		}
	}
	et131x_enable_interrupts(etdev);
}
