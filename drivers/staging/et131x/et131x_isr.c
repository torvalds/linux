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
 * THIS SOFTWARE IS PROVIDED “AS IS” AND ANY EXPRESS OR IMPLIED WARRANTIES,
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
#include "et131x_debug.h"
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
#include <asm/io.h>
#include <asm/system.h>
#include <asm/bitops.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/if_arp.h>
#include <linux/ioport.h>

#include "et1310_phy.h"
#include "et1310_pm.h"
#include "et1310_jagcore.h"
#include "et1310_mac.h"

#include "et131x_adapter.h"

/* Data for debugging facilities */
#ifdef CONFIG_ET131X_DEBUG
extern dbg_info_t *et131x_dbginfo;
#endif /* CONFIG_ET131X_DEBUG */

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
	INTERRUPT_t status;

	if (netdev == NULL || !netif_device_present(netdev)) {
		DBG_WARNING(et131x_dbginfo,
			    "No net_device struct or device not present\n");
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
	status.value = readl(&adapter->CSRAddress->global.int_status.value);

	if (adapter->FlowControl == TxOnly ||
	    adapter->FlowControl == Both) {
		status.value &= ~INT_MASK_ENABLE;
	} else {
		status.value &= ~INT_MASK_ENABLE_NO_FLOW;
	}

	/* Make sure this is our interrupt */
	if (!status.value) {
#ifdef CONFIG_ET131X_DEBUG
		adapter->Stats.UnhandledInterruptsPerSec++;
#endif
		handled = false;
		DBG_VERBOSE(et131x_dbginfo, "NOT OUR INTERRUPT\n");
		et131x_enable_interrupts(adapter);
		goto out;
	}

	/* This is our interrupt, so process accordingly */
#ifdef CONFIG_ET131X_DEBUG
	if (status.bits.rxdma_xfr_done) {
		adapter->Stats.RxDmaInterruptsPerSec++;
	}

	if (status.bits.txdma_isr) {
		adapter->Stats.TxDmaInterruptsPerSec++;
	}
#endif

	if (status.bits.watchdog_interrupt) {
		PMP_TCB pMpTcb = adapter->TxRing.CurrSendHead;

		if (pMpTcb) {
			if (++pMpTcb->PacketStaleCount > 1) {
				status.bits.txdma_isr = 1;
			}
		}

		if (adapter->RxRing.UnfinishedReceives) {
			status.bits.rxdma_xfr_done = 1;
		} else if (pMpTcb == NULL) {
			writel(0, &adapter->CSRAddress->global.watchdog_timer);
		}

		status.bits.watchdog_interrupt = 0;
#ifdef CONFIG_ET131X_DEBUG
		adapter->Stats.WatchDogInterruptsPerSec++;
#endif
	}

	if (status.value == 0) {
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
	struct et131x_adapter *pAdapter =
		container_of(work, struct et131x_adapter, task);
	INTERRUPT_t GlobStatus = pAdapter->Stats.InterruptStatus;
	ADDRESS_MAP_t __iomem *iomem = pAdapter->CSRAddress;

	/*
	 * These first two are by far the most common.  Once handled, we clear
	 * their two bits in the status word.  If the word is now zero, we
	 * exit.
	 */
	/* Handle all the completed Transmit interrupts */
	if (GlobStatus.bits.txdma_isr) {
		DBG_TX(et131x_dbginfo, "TXDMA_ISR interrupt\n");
		et131x_handle_send_interrupt(pAdapter);
	}

	/* Handle all the completed Receives interrupts */
	if (GlobStatus.bits.rxdma_xfr_done) {
		DBG_RX(et131x_dbginfo, "RXDMA_XFR_DONE interrupt\n");
		et131x_handle_recv_interrupt(pAdapter);
	}

	GlobStatus.value &= 0xffffffd7;

	if (GlobStatus.value) {
		/* Handle the TXDMA Error interrupt */
		if (GlobStatus.bits.txdma_err) {
			TXDMA_ERROR_t TxDmaErr;

			/* Following read also clears the register (COR) */
			TxDmaErr.value = readl(&iomem->txdma.TxDmaError.value);

			DBG_WARNING(et131x_dbginfo,
				    "TXDMA_ERR interrupt, error = %d\n",
				    TxDmaErr.value);
		}

		/* Handle Free Buffer Ring 0 and 1 Low interrupt */
		if (GlobStatus.bits.rxdma_fb_ring0_low ||
		    GlobStatus.bits.rxdma_fb_ring1_low) {
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
			DBG_WARNING(et131x_dbginfo,
				    "RXDMA_FB_RING0_LOW or "
				    "RXDMA_FB_RING1_LOW interrupt\n");

			/* If the user has flow control on, then we will
			 * send a pause packet, otherwise just exit
			 */
			if (pAdapter->FlowControl == TxOnly ||
			    pAdapter->FlowControl == Both) {
				PM_CSR_t pm_csr;

				/* Tell the device to send a pause packet via
				 * the back pressure register
				 */
				pm_csr.value = readl(&iomem->global.pm_csr.value);
				if (pm_csr.bits.pm_phy_sw_coma == 0) {
					TXMAC_BP_CTRL_t bp_ctrl = { 0 };

					bp_ctrl.bits.bp_req = 1;
					bp_ctrl.bits.bp_xonxoff = 1;
					writel(bp_ctrl.value,
					       &iomem->txmac.bp_ctrl.value);
				}
			}
		}

		/* Handle Packet Status Ring Low Interrupt */
		if (GlobStatus.bits.rxdma_pkt_stat_ring_low) {
			DBG_WARNING(et131x_dbginfo,
				    "RXDMA_PKT_STAT_RING_LOW interrupt\n");

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
		if (GlobStatus.bits.rxdma_err) {
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
			// TRAP();

			pAdapter->TxMacTest.value =
				readl(&iomem->txmac.tx_test.value);
			DBG_WARNING(et131x_dbginfo,
				    "RxDMA_ERR interrupt, error %x\n",
				    pAdapter->TxMacTest.value);
		}

		/* Handle the Wake on LAN Event */
		if (GlobStatus.bits.wake_on_lan) {
			/*
			 * This is a secondary interrupt for wake on LAN.
			 * The driver should never see this, if it does,
			 * something serious is wrong. We will TRAP the
			 * message when we are in DBG mode, otherwise we
			 * will ignore it.
			 */
			DBG_ERROR(et131x_dbginfo, "WAKE_ON_LAN interrupt\n");
		}

		/* Handle the PHY interrupt */
		if (GlobStatus.bits.phy_interrupt) {
			PM_CSR_t pm_csr;
			MI_BMSR_t BmsrInts, BmsrData;
			MI_ISR_t myIsr;

			DBG_VERBOSE(et131x_dbginfo, "PHY interrupt\n");

			/* If we are in coma mode when we get this interrupt,
			 * we need to disable it.
			 */
			pm_csr.value = readl(&iomem->global.pm_csr.value);
			if (pm_csr.bits.pm_phy_sw_coma == 1) {
				/*
				 * Check to see if we are in coma mode and if
				 * so, disable it because we will not be able
				 * to read PHY values until we are out.
				 */
				DBG_VERBOSE(et131x_dbginfo,
					    "Device is in COMA mode, "
					    "need to wake up\n");
				DisablePhyComa(pAdapter);
			}

			/* Read the PHY ISR to clear the reason for the
			 * interrupt.
			 */
			MiRead(pAdapter, (uint8_t) offsetof(MI_REGS_t, isr),
			       &myIsr.value);

			if (!pAdapter->ReplicaPhyLoopbk) {
				MiRead(pAdapter,
				       (uint8_t) offsetof(MI_REGS_t, bmsr),
				       &BmsrData.value);

				BmsrInts.value =
				    pAdapter->Bmsr.value ^ BmsrData.value;
				pAdapter->Bmsr.value = BmsrData.value;

				DBG_VERBOSE(et131x_dbginfo,
					    "Bmsr.value = 0x%04x,"
					    "Bmsr_ints.value = 0x%04x\n",
					    BmsrData.value, BmsrInts.value);

				/* Do all the cable in / cable out stuff */
				et131x_Mii_check(pAdapter, BmsrData, BmsrInts);
			}
		}

		/* Let's move on to the TxMac */
		if (GlobStatus.bits.txmac_interrupt) {
			pAdapter->TxRing.TxMacErr.value =
				readl(&iomem->txmac.err.value);

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
			DBG_WARNING(et131x_dbginfo,
				    "TXMAC interrupt, error 0x%08x\n",
				    pAdapter->TxRing.TxMacErr.value);

			/* If we are debugging, we want to see this error,
			 * otherwise we just want the device to be reset and
			 * continue
			 */
			//DBG_TRAP();
		}

		/* Handle RXMAC Interrupt */
		if (GlobStatus.bits.rxmac_interrupt) {
			/*
			 * These interrupts are catastrophic to the device,
			 * what we need to do is disable the interrupts and
			 * set the flag to cause us to reset so we can solve
			 * this issue.
			 */
			// MP_SET_FLAG( pAdapter, fMP_ADAPTER_HARDWARE_ERROR );

			DBG_WARNING(et131x_dbginfo,
				    "RXMAC interrupt, error 0x%08x.  Requesting reset\n",
				    readl(&iomem->rxmac.err_reg.value));

			DBG_WARNING(et131x_dbginfo,
				    "Enable 0x%08x, Diag 0x%08x\n",
				    readl(&iomem->rxmac.ctrl.value),
				    readl(&iomem->rxmac.rxq_diag.value));

			/*
			 * If we are debugging, we want to see this error,
			 * otherwise we just want the device to be reset and
			 * continue
			 */
			// TRAP();
		}

		/* Handle MAC_STAT Interrupt */
		if (GlobStatus.bits.mac_stat_interrupt) {
			/*
			 * This means at least one of the un-masked counters
			 * in the MAC_STAT block has rolled over.  Use this
			 * to maintain the top, software managed bits of the
			 * counter(s).
			 */
			DBG_VERBOSE(et131x_dbginfo, "MAC_STAT interrupt\n");
			HandleMacStatInterrupt(pAdapter);
		}

		/* Handle SLV Timeout Interrupt */
		if (GlobStatus.bits.slv_timeout) {
			/*
			 * This means a timeout has occured on a read or
			 * write request to one of the JAGCore registers. The
			 * Global Resources block has terminated the request
			 * and on a read request, returned a "fake" value.
			 * The most likely reasons are: Bad Address or the
			 * addressed module is in a power-down state and
			 * can't respond.
			 */
			DBG_VERBOSE(et131x_dbginfo, "SLV_TIMEOUT interrupt\n");
		}
	}

	if (pAdapter->PoMgmt.PowerState == NdisDeviceStateD0) {
		et131x_enable_interrupts(pAdapter);
	}
}
