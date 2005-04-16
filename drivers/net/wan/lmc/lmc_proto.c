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
  * This software may be used and distributed according to the terms
  * of the GNU General Public License version 2, incorporated herein by reference.
  *
  * Driver for the LanMedia LMC5200, LMC5245, LMC1000, LMC1200 cards.
  */

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/in.h>
#include <linux/if_arp.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/inet.h>
#include <linux/workqueue.h>
#include <linux/proc_fs.h>
#include <linux/bitops.h>

#include <net/syncppp.h>

#include <asm/processor.h>             /* Processor type for cache alignment. */
#include <asm/io.h>
#include <asm/dma.h>
#include <linux/smp.h>

#include "lmc.h"
#include "lmc_var.h"
#include "lmc_debug.h"
#include "lmc_ioctl.h"
#include "lmc_proto.h"

/*
 * The compile-time variable SPPPSTUP causes the module to be
 * compiled without referencing any of the sync ppp routines.
 */
#ifdef SPPPSTUB
#define SPPP_detach(d)	(void)0
#define SPPP_open(d)	0
#define SPPP_reopen(d)	(void)0
#define SPPP_close(d)	(void)0
#define SPPP_attach(d)	(void)0
#define SPPP_do_ioctl(d,i,c)	-EOPNOTSUPP
#else
#define SPPP_attach(x)	sppp_attach((x)->pd)
#define SPPP_detach(x)	sppp_detach((x)->pd->dev)
#define SPPP_open(x)	sppp_open((x)->pd->dev)
#define SPPP_reopen(x)	sppp_reopen((x)->pd->dev)
#define SPPP_close(x)	sppp_close((x)->pd->dev)
#define SPPP_do_ioctl(x, y, z)	sppp_do_ioctl((x)->pd->dev, (y), (z))
#endif

// init
void lmc_proto_init(lmc_softc_t *sc) /*FOLD00*/
{
    lmc_trace(sc->lmc_device, "lmc_proto_init in");
    switch(sc->if_type){
    case LMC_PPP:
        sc->pd = kmalloc(sizeof(struct ppp_device), GFP_KERNEL);
	if (!sc->pd) {
		printk("lmc_proto_init(): kmalloc failure!\n");
		return;
	}
        sc->pd->dev = sc->lmc_device;
        sc->if_ptr = sc->pd;
        break;
    case LMC_RAW:
        break;
    default:
        break;
    }
    lmc_trace(sc->lmc_device, "lmc_proto_init out");
}

// attach
void lmc_proto_attach(lmc_softc_t *sc) /*FOLD00*/
{
    lmc_trace(sc->lmc_device, "lmc_proto_attach in");
    switch(sc->if_type){
    case LMC_PPP:
        {
            struct net_device *dev = sc->lmc_device;
            SPPP_attach(sc);
            dev->do_ioctl = lmc_ioctl;
        }
        break;
    case LMC_NET:
        {
            struct net_device *dev = sc->lmc_device;
            /*
             * They set a few basics because they don't use sync_ppp
             */
            dev->flags |= IFF_POINTOPOINT;
            dev->hard_header = NULL;
            dev->hard_header_len = 0;
            dev->addr_len = 0;
        }
    case LMC_RAW: /* Setup the task queue, maybe we should notify someone? */
        {
        }
    default:
        break;
    }
    lmc_trace(sc->lmc_device, "lmc_proto_attach out");
}

// detach
void lmc_proto_detach(lmc_softc_t *sc) /*FOLD00*/
{
    switch(sc->if_type){
    case LMC_PPP:
        SPPP_detach(sc);
        break;
    case LMC_RAW: /* Tell someone we're detaching? */
        break;
    default:
        break;
    }

}

// reopen
void lmc_proto_reopen(lmc_softc_t *sc) /*FOLD00*/
{
    lmc_trace(sc->lmc_device, "lmc_proto_reopen in");
    switch(sc->if_type){
    case LMC_PPP:
        SPPP_reopen(sc);
        break;
    case LMC_RAW: /* Reset the interface after being down, prerape to receive packets again */
        break;
    default:
        break;
    }
    lmc_trace(sc->lmc_device, "lmc_proto_reopen out");
}


// ioctl
int lmc_proto_ioctl(lmc_softc_t *sc, struct ifreq *ifr, int cmd) /*FOLD00*/
{
    lmc_trace(sc->lmc_device, "lmc_proto_ioctl out");
    switch(sc->if_type){
    case LMC_PPP:
        return SPPP_do_ioctl (sc, ifr, cmd);
        break;
    default:
        return -EOPNOTSUPP;
        break;
    }
    lmc_trace(sc->lmc_device, "lmc_proto_ioctl out");
}

// open
void lmc_proto_open(lmc_softc_t *sc) /*FOLD00*/
{
    int ret;

    lmc_trace(sc->lmc_device, "lmc_proto_open in");
    switch(sc->if_type){
    case LMC_PPP:
        ret = SPPP_open(sc);
        if(ret < 0)
            printk("%s: syncPPP open failed: %d\n", sc->name, ret);
        break;
    case LMC_RAW: /* We're about to start getting packets! */
        break;
    default:
        break;
    }
    lmc_trace(sc->lmc_device, "lmc_proto_open out");
}

// close

void lmc_proto_close(lmc_softc_t *sc) /*FOLD00*/
{
    lmc_trace(sc->lmc_device, "lmc_proto_close in");
    switch(sc->if_type){
    case LMC_PPP:
        SPPP_close(sc);
        break;
    case LMC_RAW: /* Interface going down */
        break;
    default:
        break;
    }
    lmc_trace(sc->lmc_device, "lmc_proto_close out");
}

unsigned short lmc_proto_type(lmc_softc_t *sc, struct sk_buff *skb) /*FOLD00*/
{
    lmc_trace(sc->lmc_device, "lmc_proto_type in");
    switch(sc->if_type){
    case LMC_PPP:
        return htons(ETH_P_WAN_PPP);
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
        skb->dev->last_rx = jiffies;
        netif_rx(skb);
        break;
    case LMC_RAW:
        break;
    }
    lmc_trace(sc->lmc_device, "lmc_proto_netif out");
}

