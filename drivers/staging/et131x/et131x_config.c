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

#define PARM_FLOW_CTL_DEF       0
#define PARM_FLOW_CTL_MIN       0
#define PARM_FLOW_CTL_MAX       3

#define PARM_JUMBO_PKT_DEF      1514
#define PARM_JUMBO_PKT_MIN      1514
#define PARM_JUMBO_PKT_MAX      9216

#define PARM_PHY_COMA_DEF       0
#define PARM_PHY_COMA_MIN       0
#define PARM_PHY_COMA_MAX       1

#define PARM_SC_GAIN_DEF        7
#define PARM_SC_GAIN_MIN        0
#define PARM_SC_GAIN_MAX        7

#define PARM_PM_WOL_DEF         0
#define PARM_PM_WOL_MIN         0
#define PARM_PM_WOL_MAX         1

#define PARM_NMI_DISABLE_DEF    0
#define PARM_NMI_DISABLE_MIN    0
#define PARM_NMI_DISABLE_MAX    2



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
 * @etdev: pointer to the private adapter struct
 *
 * Parses a configuration from some location (module parameters, for example)
 * into the private adapter struct. This really has no sensible analogy in
 * Linux as sysfs parameters are dynamic. Several things that were hee could
 * go into sysfs, but other stuff like speed handling is part of the mii
 * interfaces/ethtool.
 */
void et131x_config_parse(struct et131x_adapter *etdev)
{
	static const u8 default_mac[] = { 0x00, 0x05, 0x3d, 0x00, 0x02, 0x00 };
	static const u8 duplex[] = { 0, 1, 2, 1, 2, 2 };
	static const u16 speed[] = { 0, 10, 10, 100, 100, 1000 };

	DBG_ENTER(et131x_dbginfo);

	etdev->SpeedDuplex = et131x_speed_set;

	if (et131x_speed_set < PARM_SPEED_DUPLEX_MIN ||
	    et131x_speed_set > PARM_SPEED_DUPLEX_MAX) {
	    	dev_warn(&etdev->pdev->dev, "invalid speed setting ignored.\n");
	    	et131x_speed_set = PARM_SPEED_DUPLEX_DEF;
	}
	else if (et131x_speed_set != PARM_SPEED_DUPLEX_DEF)
		DBG_VERBOSE(et131x_dbginfo, "Speed set manually to : %d \n",
			    et131x_speed_set);

	/*  etdev->SpeedDuplex            = PARM_SPEED_DUPLEX_DEF; */

	etdev->RegistryFlowControl = PARM_FLOW_CTL_DEF;
	etdev->RegistryJumboPacket = PARM_JUMBO_PKT_DEF;
	etdev->RegistryPhyComa = PARM_PHY_COMA_DEF;

	if (et131x_nmi_disable != PARM_NMI_DISABLE_DEF)
		etdev->RegistryNMIDisable = et131x_nmi_disable;
	else
		etdev->RegistryNMIDisable = PARM_NMI_DISABLE_DEF;

	etdev->RegistryPhyLoopbk = 0;	/* 0 off 1 on */

	/* Set the MAC address to a default */
	memcpy(etdev->CurrentAddress, default_mac, ETH_ALEN);
	etdev->bOverrideAddress = false;

	/* Decode SpeedDuplex
	 *
	 * Set up as if we are auto negotiating always and then change if we
	 * go into force mode
	 *
	 * If we are the 10/100 device, and gigabit is somehow requested then
	 * knock it down to 100 full.
	 */
	if (etdev->pdev->device == ET131X_PCI_DEVICE_ID_FAST &&
	    etdev->SpeedDuplex == 5)
		etdev->SpeedDuplex = 4;

	etdev->AiForceSpeed = speed[etdev->SpeedDuplex];
	etdev->AiForceDpx = duplex[etdev->SpeedDuplex];	/* Auto FDX */

	DBG_LEAVE(et131x_dbginfo);
}
