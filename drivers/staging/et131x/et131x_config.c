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
 * et131x_config.c - Handles parsing of configuration data during
 *                   initialization.
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

#include "et131x_adapter.h"
#include "et131x_initpci.h"
#include "et131x_config.h"

#include "et1310_tx.h"

/* Data for debugging facilities */
#ifdef CONFIG_ET131X_DEBUG
extern dbg_info_t *et131x_dbginfo;
#endif /* CONFIG_ET131X_DEBUG */

/* Defines for Parameter Default/Min/Max vaules */
#define PARM_SPEED_DUPLEX_DEF   0
#define PARM_SPEED_DUPLEX_MIN   0
#define PARM_SPEED_DUPLEX_MAX   5

#define PARM_VLAN_TAG_DEF       0
#define PARM_VLAN_TAG_MIN       0
#define PARM_VLAN_TAG_MAX       4095

#define PARM_FLOW_CTL_DEF       0
#define PARM_FLOW_CTL_MIN       0
#define PARM_FLOW_CTL_MAX       3

#define PARM_WOL_LINK_DEF       3
#define PARM_WOL_LINK_MIN       0
#define PARM_WOL_LINK_MAX       3

#define PARM_WOL_MATCH_DEF      7
#define PARM_WOL_MATCH_MIN      0
#define PARM_WOL_MATCH_MAX      7

#define PARM_JUMBO_PKT_DEF      1514
#define PARM_JUMBO_PKT_MIN      1514
#define PARM_JUMBO_PKT_MAX      9216

#define PARM_PHY_COMA_DEF       0
#define PARM_PHY_COMA_MIN       0
#define PARM_PHY_COMA_MAX       1

#define PARM_RX_NUM_BUFS_DEF    4
#define PARM_RX_NUM_BUFS_MIN    1
#define PARM_RX_NUM_BUFS_MAX    64

#define PARM_RX_TIME_INT_DEF    10
#define PARM_RX_TIME_INT_MIN    2
#define PARM_RX_TIME_INT_MAX    320

#define PARM_TX_NUM_BUFS_DEF    4
#define PARM_TX_NUM_BUFS_MIN    1
#define PARM_TX_NUM_BUFS_MAX    40

#define PARM_TX_TIME_INT_DEF    40
#define PARM_TX_TIME_INT_MIN    1
#define PARM_TX_TIME_INT_MAX    140

#define PARM_RX_MEM_END_DEF     0x2bc
#define PARM_RX_MEM_END_MIN     0
#define PARM_RX_MEM_END_MAX     0x3ff

#define PARM_MAC_STAT_DEF       1
#define PARM_MAC_STAT_MIN       0
#define PARM_MAC_STAT_MAX       1

#define PARM_SC_GAIN_DEF        7
#define PARM_SC_GAIN_MIN        0
#define PARM_SC_GAIN_MAX        7

#define PARM_PM_WOL_DEF         0
#define PARM_PM_WOL_MIN         0
#define PARM_PM_WOL_MAX         1

#define PARM_NMI_DISABLE_DEF    0
#define PARM_NMI_DISABLE_MIN    0
#define PARM_NMI_DISABLE_MAX    2

#define PARM_DMA_CACHE_DEF      0
#define PARM_DMA_CACHE_MIN      0
#define PARM_DMA_CACHE_MAX      15

#define PARM_PHY_LOOPBK_DEF     0
#define PARM_PHY_LOOPBK_MIN     0
#define PARM_PHY_LOOPBK_MAX     1

#define PARM_MAC_ADDRESS_DEF    { 0x00, 0x05, 0x3d, 0x00, 0x02, 0x00 }

/* Module parameter for disabling NMI
 * et131x_speed_set :
 * Set Link speed and dublex manually (0-5)  [0]
 *  1 : 10Mb   Half-Duplex
 *  2 : 10Mb   Full-Duplex
 *  3 : 100Mb  Half-Duplex
 *  4 : 100Mb  Full-Duplex
 *  5 : 1000Mb Full-Duplex
 *  0 : Auto Speed Auto Dublex // default
 */
static u32 et131x_nmi_disable = PARM_NMI_DISABLE_DEF;
module_param(et131x_nmi_disable, uint, 0);
MODULE_PARM_DESC(et131x_nmi_disable, "Disable NMI (0-2) [0]");

/* Module parameter for manual speed setting
 * et131x_nmi_disable :
 * Disable NMI (0-2) [0]
 *  0 :
 *  1 :
 *  2 :
 */
static u32 et131x_speed_set = PARM_SPEED_DUPLEX_DEF;
module_param(et131x_speed_set, uint, 0);
MODULE_PARM_DESC(et131x_speed_set,
		 "Set Link speed and dublex manually (0-5)  [0] \n  1 : 10Mb   Half-Duplex \n  2 : 10Mb   Full-Duplex \n  3 : 100Mb  Half-Duplex \n  4 : 100Mb  Full-Duplex \n  5 : 1000Mb Full-Duplex \n 0 : Auto Speed Auto Dublex");

/**
 * et131x_config_parse
 * @pAdapter: pointer to the private adapter struct
 *
 * Parses a configuration from some location (module parameters, for example)
 * into the private adapter struct
 */
void et131x_config_parse(struct et131x_adapter *pAdapter)
{
	uint8_t macAddrDef[] = PARM_MAC_ADDRESS_DEF;

	DBG_ENTER(et131x_dbginfo);

	/*
	 * The NDIS driver uses the registry to store persistent per-device
	 * configuration, and reads this configuration into the appropriate
	 * elements of the private adapter structure on initialization.
	 * Because Linux has no analog to the registry, use this function to
	 * initialize the private adapter structure with a default
	 * configuration.
	 *
	 * One other possibility is to use a series of module parameters which
	 * can be passed in by the caller when the module is initialized.
	 * However, this implementation does not allow for seperate
	 * configurations in the event multiple devices are present, and hence
	 * will not suffice.
	 *
	 * If another method is derived which addresses this problem, this is
	 * where it should be implemented.
	 */

	 /* Set the private adapter struct with default values for the
	  * corresponding parameters
	  */
	if (et131x_speed_set != PARM_SPEED_DUPLEX_DEF) {
		DBG_VERBOSE(et131x_dbginfo, "Speed set manually to : %d \n",
			    et131x_speed_set);
		pAdapter->SpeedDuplex = et131x_speed_set;
	} else {
		pAdapter->SpeedDuplex = PARM_SPEED_DUPLEX_DEF;
	}

	//  pAdapter->SpeedDuplex            = PARM_SPEED_DUPLEX_DEF;

	pAdapter->RegistryVlanTag = PARM_VLAN_TAG_DEF;
	pAdapter->RegistryFlowControl = PARM_FLOW_CTL_DEF;
	pAdapter->RegistryWOLLink = PARM_WOL_LINK_DEF;
	pAdapter->RegistryWOLMatch = PARM_WOL_MATCH_DEF;
	pAdapter->RegistryJumboPacket = PARM_JUMBO_PKT_DEF;
	pAdapter->RegistryPhyComa = PARM_PHY_COMA_DEF;
	pAdapter->RegistryRxNumBuffers = PARM_RX_NUM_BUFS_DEF;
	pAdapter->RegistryRxTimeInterval = PARM_RX_TIME_INT_DEF;
	pAdapter->RegistryTxNumBuffers = PARM_TX_NUM_BUFS_DEF;
	pAdapter->RegistryTxTimeInterval = PARM_TX_TIME_INT_DEF;
	pAdapter->RegistryRxMemEnd = PARM_RX_MEM_END_DEF;
	pAdapter->RegistryMACStat = PARM_MAC_STAT_DEF;
	pAdapter->RegistrySCGain = PARM_SC_GAIN_DEF;
	pAdapter->RegistryPMWOL = PARM_PM_WOL_DEF;

	if (et131x_nmi_disable != PARM_NMI_DISABLE_DEF) {
		pAdapter->RegistryNMIDisable = et131x_nmi_disable;
	} else {
		pAdapter->RegistryNMIDisable = PARM_NMI_DISABLE_DEF;
	}

	pAdapter->RegistryDMACache = PARM_DMA_CACHE_DEF;
	pAdapter->RegistryPhyLoopbk = PARM_PHY_LOOPBK_DEF;

	/* Set the MAC address to a default */
	memcpy(pAdapter->CurrentAddress, macAddrDef, ETH_ALEN);
	pAdapter->bOverrideAddress = false;

	DBG_TRACE(et131x_dbginfo,
		  "Default MAC Address  : %02x:%02x:%02x:%02x:%02x:%02x\n",
		  pAdapter->CurrentAddress[0], pAdapter->CurrentAddress[1],
		  pAdapter->CurrentAddress[2], pAdapter->CurrentAddress[3],
		  pAdapter->CurrentAddress[4], pAdapter->CurrentAddress[5]);

	/* Decode SpeedDuplex
	 *
	 * Set up as if we are auto negotiating always and then change if we
	 * go into force mode
	 */
	pAdapter->AiForceSpeed = 0;	// Auto speed
	pAdapter->AiForceDpx = 0;	// Auto FDX

	/* If we are the 10/100 device, and gigabit is somehow requested then
	 * knock it down to 100 full.
	 */
	if ((pAdapter->DeviceID == ET131X_PCI_DEVICE_ID_FAST) &&
	    (pAdapter->SpeedDuplex == 5)) {
		pAdapter->SpeedDuplex = 4;
	}

	switch (pAdapter->SpeedDuplex) {
	case 1:		// 10Mb   Half-Duplex
		pAdapter->AiForceSpeed = 10;
		pAdapter->AiForceDpx = 1;
		break;

	case 2:		// 10Mb   Full-Duplex
		pAdapter->AiForceSpeed = 10;
		pAdapter->AiForceDpx = 2;
		break;

	case 3:		// 100Mb  Half-Duplex
		pAdapter->AiForceSpeed = 100;
		pAdapter->AiForceDpx = 1;
		break;

	case 4:		// 100Mb  Full-Duplex
		pAdapter->AiForceSpeed = 100;
		pAdapter->AiForceDpx = 2;
		break;

	case 5:		// 1000Mb Full-Duplex
		pAdapter->AiForceSpeed = 1000;
		pAdapter->AiForceDpx = 2;
		break;
	}

	DBG_LEAVE(et131x_dbginfo);
}
