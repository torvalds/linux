// SPDX-License-Identifier: GPL-2.0-only
 /*
  * Copyright (c) 1997-2000 LAN Media Corporation (LMC)
  * All rights reserved.  www.lanmedia.com
  *
  * This code is written by:
  * Andrew Stanley-Jones (asj@cban.com)
  * Rob Braun (bbraun@vix.com),
  * Michael Graff (explorer@vix.com) and
  * Matt Thomas (matt@3am-software.com).
  *
  * With Help By:
  * David Boggs
  * Ron Crane
  * Allan Cox
  *
  * Driver for the LanMedia LMC5200, LMC5245, LMC1000, LMC1200 cards.
  */

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/in.h>
#include <linux/if_arp.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/inet.h>
#include <linux/workqueue.h>
#include <linux/proc_fs.h>
#include <linux/bitops.h>
#include <asm/processor.h>             /* Processor type for cache alignment. */
#include <asm/io.h>
#include <asm/dma.h>
#include <linux/smp.h>

#include "lmc.h"
#include "lmc_var.h"
#include "lmc_debug.h"
#include "lmc_ioctl.h"
#include "lmc_proto.h"

// attach
void lmc_proto_attach(lmc_softc_t *sc) /*FOLD00*/
{
    lmc_trace(sc->lmc_device, "lmc_proto_attach in");
    if (sc->if_type == LMC_NET) {
            struct net_device *dev = sc->lmc_device;
            /*
	     * They set a few basics because they don't use HDLC
             */
            dev->flags |= IFF_POINTOPOINT;
            dev->hard_header_len = 0;
            dev->addr_len = 0;
        }
    lmc_trace(sc->lmc_device, "lmc_proto_attach out");
}

int lmc_proto_ioctl(lmc_softc_t *sc, struct ifreq *ifr, int cmd)
{
	lmc_trace(sc->lmc_device, "lmc_proto_ioctl");
	if (sc->if_type == LMC_PPP)
		return hdlc_ioctl(sc->lmc_device, ifr, cmd);
	return -EOPNOTSUPP;
}

int lmc_proto_open(lmc_softc_t *sc)
{
	int ret = 0;

	lmc_trace(sc->lmc_device, "lmc_proto_open in");

	if (sc->if_type == LMC_PPP) {
		ret = hdlc_open(sc->lmc_device);
		if (ret < 0)
			printk(KERN_WARNING "%s: HDLC open failed: %d\n",
			       sc->name, ret);
	}

	lmc_trace(sc->lmc_device, "lmc_proto_open out");
	return ret;
}

void lmc_proto_close(lmc_softc_t *sc)
{
	lmc_trace(sc->lmc_device, "lmc_proto_close in");

	if (sc->if_type == LMC_PPP)
		hdlc_close(sc->lmc_device);

	lmc_trace(sc->lmc_device, "lmc_proto_close out");
}

__be16 lmc_proto_type(lmc_softc_t *sc, struct sk_buff *skb) /*FOLD00*/
{
    lmc_trace(sc->lmc_device, "lmc_proto_type in");
    switch(sc->if_type){
    case LMC_PPP:
	    return hdlc_type_trans(skb, sc->lmc_device);
	    break;
    case LMC_NET:
        return htons(ETH_P_802_2);
        break;
    case LMC_RAW: /* Packet type for skbuff kind of useless */
        return htons(ETH_P_802_2);
        break;
    default:
        printk(KERN_WARNING "%s: No protocol set for this interface, assuming 802.2 (which is wrong!!)\n", sc->name);
        return htons(ETH_P_802_2);
        break;
    }
    lmc_trace(sc->lmc_device, "lmc_proto_tye out");

}

void lmc_proto_netif(lmc_softc_t *sc, struct sk_buff *skb) /*FOLD00*/
{
    lmc_trace(sc->lmc_device, "lmc_proto_netif in");
    switch(sc->if_type){
    case LMC_PPP:
    case LMC_NET:
    default:
        netif_rx(skb);
        break;
    case LMC_RAW:
        break;
    }
    lmc_trace(sc->lmc_device, "lmc_proto_netif out");
}
