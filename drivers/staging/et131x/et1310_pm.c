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
 * et1310_pm.c - All power management related code (not completely implemented)
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
#include "et1310_rx.h"

#include "et131x_adapter.h"
#include "et131x_initpci.h"

/* Data for debugging facilities */
#ifdef CONFIG_ET131X_DEBUG
extern dbg_info_t *et131x_dbginfo;
#endif /* CONFIG_ET131X_DEBUG */

/**
 * EnablePhyComa - called when network cable is unplugged
 * @pAdapter: pointer to our adapter structure
 *
 * driver receive an phy status change interrupt while in D0 and check that
 * phy_status is down.
 *
 *          -- gate off JAGCore;
 *          -- set gigE PHY in Coma mode
 *          -- wake on phy_interrupt; Perform software reset JAGCore,
 *             re-initialize jagcore and gigE PHY
 *
 *      Add D0-ASPM-PhyLinkDown Support:
 *          -- while in D0, when there is a phy_interrupt indicating phy link
 *             down status, call the MPSetPhyComa routine to enter this active
 *             state power saving mode
 *          -- while in D0-ASPM-PhyLinkDown mode, when there is a phy_interrupt
 *       indicating linkup status, call the MPDisablePhyComa routine to
 *             restore JAGCore and gigE PHY
 */
void EnablePhyComa(struct et131x_adapter *pAdapter)
{
	unsigned long lockflags;
	PM_CSR_t GlobalPmCSR;
	int32_t LoopCounter = 10;

	DBG_ENTER(et131x_dbginfo);

	GlobalPmCSR.value = readl(&pAdapter->CSRAddress->global.pm_csr.value);

	/* Save the GbE PHY speed and duplex modes. Need to restore this
	 * when cable is plugged back in
	 */
	pAdapter->PoMgmt.PowerDownSpeed = pAdapter->AiForceSpeed;
	pAdapter->PoMgmt.PowerDownDuplex = pAdapter->AiForceDpx;

	/* Stop sending packets. */
	spin_lock_irqsave(&pAdapter->SendHWLock, lockflags);
	MP_SET_FLAG(pAdapter, fMP_ADAPTER_LOWER_POWER);
	spin_unlock_irqrestore(&pAdapter->SendHWLock, lockflags);

	/* Wait for outstanding Receive packets */
	while ((MP_GET_RCV_REF(pAdapter) != 0) && (LoopCounter-- > 0)) {
		mdelay(2);
	}

	/* Gate off JAGCore 3 clock domains */
	GlobalPmCSR.bits.pm_sysclk_gate = 0;
	GlobalPmCSR.bits.pm_txclk_gate = 0;
	GlobalPmCSR.bits.pm_rxclk_gate = 0;
	writel(GlobalPmCSR.value, &pAdapter->CSRAddress->global.pm_csr.value);

	/* Program gigE PHY in to Coma mode */
	GlobalPmCSR.bits.pm_phy_sw_coma = 1;
	writel(GlobalPmCSR.value, &pAdapter->CSRAddress->global.pm_csr.value);

	DBG_LEAVE(et131x_dbginfo);
}

/**
 * DisablePhyComa - Disable the Phy Coma Mode
 * @pAdapter: pointer to our adapter structure
 */
void DisablePhyComa(struct et131x_adapter *pAdapter)
{
	PM_CSR_t GlobalPmCSR;

	DBG_ENTER(et131x_dbginfo);

	GlobalPmCSR.value = readl(&pAdapter->CSRAddress->global.pm_csr.value);

	/* Disable phy_sw_coma register and re-enable JAGCore clocks */
	GlobalPmCSR.bits.pm_sysclk_gate = 1;
	GlobalPmCSR.bits.pm_txclk_gate = 1;
	GlobalPmCSR.bits.pm_rxclk_gate = 1;
	GlobalPmCSR.bits.pm_phy_sw_coma = 0;
	writel(GlobalPmCSR.value, &pAdapter->CSRAddress->global.pm_csr.value);

	/* Restore the GbE PHY speed and duplex modes;
	 * Reset JAGCore; re-configure and initialize JAGCore and gigE PHY
	 */
	pAdapter->AiForceSpeed = pAdapter->PoMgmt.PowerDownSpeed;
	pAdapter->AiForceDpx = pAdapter->PoMgmt.PowerDownDuplex;

	/* Re-initialize the send structures */
	et131x_init_send(pAdapter);

	/* Reset the RFD list and re-start RU  */
	et131x_reset_recv(pAdapter);

	/* Bring the device back to the state it was during init prior to
         * autonegotiation being complete.  This way, when we get the auto-neg
         * complete interrupt, we can complete init by calling ConfigMacREGS2.
         */
	et131x_soft_reset(pAdapter);

	/* setup et1310 as per the documentation ?? */
	et131x_adapter_setup(pAdapter);

	/* Allow Tx to restart */
	MP_CLEAR_FLAG(pAdapter, fMP_ADAPTER_LOWER_POWER);

	/* Need to re-enable Rx. */
	et131x_rx_dma_enable(pAdapter);

	DBG_LEAVE(et131x_dbginfo);
}

