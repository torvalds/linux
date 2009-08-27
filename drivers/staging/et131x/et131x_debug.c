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
 * et131x_debug.c - Routines used for debugging.
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

#ifdef CONFIG_ET131X_DEBUG

#include "et131x_version.h"
#include "et131x_debug.h"
#include "et131x_defs.h"

#include <linux/pci.h>
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
#include <asm/system.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/if_arp.h>
#include <linux/ioport.h>
#include <linux/random.h>

#include "et1310_phy.h"
#include "et1310_pm.h"
#include "et1310_jagcore.h"

#include "et131x_adapter.h"
#include "et131x_netdev.h"
#include "et131x_config.h"
#include "et131x_isr.h"

#include "et1310_address_map.h"
#include "et1310_tx.h"
#include "et1310_rx.h"
#include "et1310_mac.h"

/* Data for debugging facilities */
extern dbg_info_t *et131x_dbginfo;

/**
 * DumpTxQueueContents - Dump out the tx queue and the shadow pointers
 * @etdev: pointer to our adapter structure
 */
void DumpTxQueueContents(int debug, struct et131x_adapter *etdev)
{
	MMC_t __iomem *mmc = &etdev->regs->mmc;
	u32 txq_addr;

	if (DBG_FLAGS(et131x_dbginfo) & debug) {
		for (txq_addr = 0x200; txq_addr < 0x3ff; txq_addr++) {
			u32 sram_access = readl(&mmc->sram_access);
			sram_access &= 0xFFFF;
			sram_access |= (txq_addr << 16) | ET_SRAM_REQ_ACCESS;
			writel(sram_access, &mmc->sram_access);

			DBG_PRINT("Addr 0x%x, Access 0x%08x\t"
				  "Value 1 0x%08x, Value 2 0x%08x, "
				  "Value 3 0x%08x, Value 4 0x%08x, \n",
				  txq_addr,
				  readl(&mmc->sram_access),
				  readl(&mmc->sram_word1),
				  readl(&mmc->sram_word2),
				  readl(&mmc->sram_word3),
				  readl(&mmc->sram_word4));
		}

		DBG_PRINT("Shadow Pointers 0x%08x\n",
			  readl(&etdev->regs->txmac.shadow_ptr.value));
	}
}

#define NUM_BLOCKS 8

static const char *BlockNames[NUM_BLOCKS] = {
	"Global", "Tx DMA", "Rx DMA", "Tx MAC",
	"Rx MAC", "MAC", "MAC Stat", "MMC"
};


/**
 * DumpDeviceBlock
 * @etdev: pointer to our adapter
 *
 * Dumps the first 64 regs of each block of the et-1310 (each block is
 * mapped to a new page, each page is 4096 bytes).
 */
void DumpDeviceBlock(int debug, struct et131x_adapter *etdev,
		     u32 block)
{
	u32 addr1, addr2;
	u32 __iomem *regs = (u32 __iomem *) etdev->regs;

	/* Output the debug counters to the debug terminal */
	if (DBG_FLAGS(et131x_dbginfo) & debug) {
		DBG_PRINT("%s block\n", BlockNames[block]);
		regs += block * 1024;
		for (addr1 = 0; addr1 < 8; addr1++) {
			for (addr2 = 0; addr2 < 8; addr2++) {
				if (block == 0 &&
				    (addr1 * 8 + addr2) == 6)
					DBG_PRINT("  ISR    , ");
				else
					DBG_PRINT("0x%08x, ", readl(regs++));
			}
			DBG_PRINT("\n");
		}
		DBG_PRINT("\n");
	}
}

/**
 * DumpDeviceReg
 * @etdev: pointer to our adapter
 *
 * Dumps the first 64 regs of each block of the et-1310 (each block is
 * mapped to a new page, each page is 4096 bytes).
 */
void DumpDeviceReg(int debug, struct et131x_adapter *etdev)
{
	u32 addr1, addr2;
	u32 block;
	u32 __iomem *regs = (u32 __iomem *)etdev->regs;
	u32 __iomem *p;

	/* Output the debug counters to the debug terminal */
	if (DBG_FLAGS(et131x_dbginfo) & debug) {
		for (block = 0; block < NUM_BLOCKS; block++) {
			DBG_PRINT("%s block\n", BlockNames[block]);
			p = regs + block * 1024;

			for (addr1 = 0; addr1 < 8; addr1++) {
				for (addr2 = 0; addr2 < 8; addr2++)
					DBG_PRINT("0x%08x, ", readl(p++));
				DBG_PRINT("\n");
			}
			DBG_PRINT("\n");
		}
	}
}

#endif /* CONFIG_ET131X_DEBUG */
