/****************************************************************************

  (c) SYSTEC electronic GmbH, D-07973 Greiz, August-Bebel-Str. 29
      www.systec-electronic.com

  Project:      openPOWERLINK

  Description:  Virtual Ethernet Driver for Linux

  License:

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions
    are met:

    1. Redistributions of source code must retain the above copyright
       notice, this list of conditions and the following disclaimer.

    2. Redistributions in binary form must reproduce the above copyright
       notice, this list of conditions and the following disclaimer in the
       documentation and/or other materials provided with the distribution.

    3. Neither the name of SYSTEC electronic GmbH nor the names of its
       contributors may be used to endorse or promote products derived
       from this software without prior written permission. For written
       permission, please contact info@systec-electronic.com.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
    "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
    LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
    FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
    COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
    INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
    BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
    LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
    CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
    LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
    ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
    POSSIBILITY OF SUCH DAMAGE.

    Severability Clause:

        If a provision of this License is or becomes illegal, invalid or
        unenforceable in any jurisdiction, that shall not affect:
        1. the validity or enforceability in that jurisdiction of any other
           provision of this License; or
        2. the validity or enforceability in other jurisdictions of that or
           any other provision of this License.

  -------------------------------------------------------------------------

                $RCSfile: VirtualEthernetLinux.c,v $

                $Author: D.Krueger $

                $Revision: 1.8 $  $Date: 2008/11/20 17:06:51 $

                $State: Exp $

                Build Environment:

  -------------------------------------------------------------------------

  Revision History:

  2006/06/12 -ar:   start of the implementation, version 1.00

  2006/09/18 d.k.:  integration into EPL DLLk module

  ToDo:

  void netif_carrier_off(struct net_device *dev);
  void netif_carrier_on(struct net_device *dev);

****************************************************************************/

#include <linux/version.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/if_arp.h>
#include <net/arp.h>

#include <net/protocol.h>
#include <net/pkt_sched.h>
#include <linux/if_ether.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <linux/mm.h>
#include <linux/types.h>
#include <linux/skbuff.h>	/* for struct sk_buff */

#include "kernel/VirtualEthernet.h"
#include "kernel/EplDllkCal.h"
#include "kernel/EplDllk.h"

#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_VETH)) != 0)

/***************************************************************************/
/*                                                                         */
/*                                                                         */
/*          G L O B A L   D E F I N I T I O N S                            */
/*                                                                         */
/*                                                                         */
/***************************************************************************/

//---------------------------------------------------------------------------
// const defines
//---------------------------------------------------------------------------

#ifndef EPL_VETH_TX_TIMEOUT
//#define EPL_VETH_TX_TIMEOUT (2*HZ)
#define EPL_VETH_TX_TIMEOUT 0	// d.k.: we use no timeout
#endif

//---------------------------------------------------------------------------
// local types
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// modul globale vars
//---------------------------------------------------------------------------

static struct net_device *pVEthNetDevice_g = NULL;

//---------------------------------------------------------------------------
// local function prototypes
//---------------------------------------------------------------------------

static int VEthOpen(struct net_device *pNetDevice_p);
static int VEthClose(struct net_device *pNetDevice_p);
static int VEthXmit(struct sk_buff *pSkb_p, struct net_device *pNetDevice_p);
static struct net_device_stats *VEthGetStats(struct net_device *pNetDevice_p);
static void VEthTimeout(struct net_device *pNetDevice_p);
static tEplKernel VEthRecvFrame(tEplFrameInfo * pFrameInfo_p);

//=========================================================================//
//                                                                         //
//          P U B L I C   F U N C T I O N S                                //
//                                                                         //
//=========================================================================//

//---------------------------------------------------------------------------
//
// Function:
//
// Description:
//
//
//
// Parameters:
//
//
// Returns:
//
//
// State:
//
//---------------------------------------------------------------------------

static int VEthOpen(struct net_device *pNetDevice_p)
{
	tEplKernel Ret = kEplSuccessful;

	//open the device
//	struct net_device_stats* pStats = netdev_priv(pNetDevice_p);

	//start the interface queue for the network subsystem
	netif_start_queue(pNetDevice_p);

	// register callback function in DLL
	Ret = EplDllkRegAsyncHandler(VEthRecvFrame);

	EPL_DBGLVL_VETH_TRACE1
	    ("VEthOpen: EplDllkRegAsyncHandler returned 0x%02X\n", Ret);

	return 0;
}

static int VEthClose(struct net_device *pNetDevice_p)
{
	tEplKernel Ret = kEplSuccessful;

	EPL_DBGLVL_VETH_TRACE0("VEthClose\n");

	Ret = EplDllkDeregAsyncHandler(VEthRecvFrame);

	//stop the interface queue for the network subsystem
	netif_stop_queue(pNetDevice_p);
	return 0;
}

static int VEthXmit(struct sk_buff *pSkb_p, struct net_device *pNetDevice_p)
{
	tEplKernel Ret = kEplSuccessful;
	tEplFrameInfo FrameInfo;

	//transmit function
	struct net_device_stats *pStats = netdev_priv(pNetDevice_p);

	//save timestemp
	pNetDevice_p->trans_start = jiffies;

	FrameInfo.m_pFrame = (tEplFrame *) pSkb_p->data;
	FrameInfo.m_uiFrameSize = pSkb_p->len;

	//call send fkt on DLL
	Ret = EplDllkCalAsyncSend(&FrameInfo, kEplDllAsyncReqPrioGeneric);
	if (Ret != kEplSuccessful) {
		EPL_DBGLVL_VETH_TRACE1
		    ("VEthXmit: EplDllkCalAsyncSend returned 0x%02X\n", Ret);
		netif_stop_queue(pNetDevice_p);
		goto Exit;
	} else {
		EPL_DBGLVL_VETH_TRACE0("VEthXmit: frame passed to DLL\n");
		dev_kfree_skb(pSkb_p);

		//set stats for the device
		pStats->tx_packets++;
		pStats->tx_bytes += FrameInfo.m_uiFrameSize;
	}

      Exit:
	return 0;

}

static struct net_device_stats *VEthGetStats(struct net_device *pNetDevice_p)
{
	EPL_DBGLVL_VETH_TRACE0("VEthGetStats\n");

	return netdev_priv(pNetDevice_p);
}

static void VEthTimeout(struct net_device *pNetDevice_p)
{
	EPL_DBGLVL_VETH_TRACE0("VEthTimeout(\n");

	// $$$ d.k.: move to extra function, which is called by DLL when new space is available in TxFifo
	if (netif_queue_stopped(pNetDevice_p)) {
		netif_wake_queue(pNetDevice_p);
	}
}

static tEplKernel VEthRecvFrame(tEplFrameInfo * pFrameInfo_p)
{
	tEplKernel Ret = kEplSuccessful;
	struct net_device *pNetDevice = pVEthNetDevice_g;
	struct net_device_stats *pStats = netdev_priv(pNetDevice);
	struct sk_buff *pSkb;

	EPL_DBGLVL_VETH_TRACE1("VEthRecvFrame: FrameSize=%u\n",
			       pFrameInfo_p->m_uiFrameSize);

	pSkb = dev_alloc_skb(pFrameInfo_p->m_uiFrameSize + 2);
	if (pSkb == NULL) {
		pStats->rx_dropped++;
		goto Exit;
	}
	pSkb->dev = pNetDevice;

	skb_reserve(pSkb, 2);

	memcpy((void *)skb_put(pSkb, pFrameInfo_p->m_uiFrameSize),
	       pFrameInfo_p->m_pFrame, pFrameInfo_p->m_uiFrameSize);

	pSkb->protocol = eth_type_trans(pSkb, pNetDevice);
	pSkb->ip_summed = CHECKSUM_UNNECESSARY;

	// call netif_rx with skb
	netif_rx(pSkb);

	EPL_DBGLVL_VETH_TRACE1("VEthRecvFrame: SrcMAC=0x%llx\n",
			       AmiGetQword48FromBe(pFrameInfo_p->m_pFrame->
						   m_be_abSrcMac));

	// update receive statistics
	pStats->rx_packets++;
	pStats->rx_bytes += pFrameInfo_p->m_uiFrameSize;

      Exit:
	return Ret;
}

tEplKernel PUBLIC VEthAddInstance(tEplDllkInitParam * pInitParam_p)
{
	tEplKernel Ret = kEplSuccessful;

	// allocate net device structure with priv pointing to stats structure
	pVEthNetDevice_g =
	    alloc_netdev(sizeof(struct net_device_stats), EPL_VETH_NAME,
			 ether_setup);
//    pVEthNetDevice_g = alloc_etherdev(sizeof (struct net_device_stats));

	if (pVEthNetDevice_g == NULL) {
		Ret = kEplNoResource;
		goto Exit;
	}

	pVEthNetDevice_g->open = VEthOpen;
	pVEthNetDevice_g->stop = VEthClose;
	pVEthNetDevice_g->get_stats = VEthGetStats;
	pVEthNetDevice_g->hard_start_xmit = VEthXmit;
	pVEthNetDevice_g->tx_timeout = VEthTimeout;
	pVEthNetDevice_g->watchdog_timeo = EPL_VETH_TX_TIMEOUT;
	pVEthNetDevice_g->destructor = free_netdev;

	// copy own MAC address to net device structure
	memcpy(pVEthNetDevice_g->dev_addr, pInitParam_p->m_be_abSrcMac, 6);

	//register VEth to the network subsystem
	if (register_netdev(pVEthNetDevice_g)) {
		EPL_DBGLVL_VETH_TRACE0
		    ("VEthAddInstance: Could not register VEth...\n");
	} else {
		EPL_DBGLVL_VETH_TRACE0
		    ("VEthAddInstance: Register VEth successfull...\n");
	}

      Exit:
	return Ret;
}

tEplKernel PUBLIC VEthDelInstance(void)
{
	tEplKernel Ret = kEplSuccessful;

	if (pVEthNetDevice_g != NULL) {
		//unregister VEth from the network subsystem
		unregister_netdev(pVEthNetDevice_g);
		// destructor was set to free_netdev,
		// so we do not need to call free_netdev here
		pVEthNetDevice_g = NULL;
	}

	return Ret;
}

#endif // (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_VETH)) != 0)
