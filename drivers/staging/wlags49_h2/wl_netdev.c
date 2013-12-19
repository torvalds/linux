/*******************************************************************************
 * Agere Systems Inc.
 * Wireless device driver for Linux (wlags49).
 *
 * Copyright (c) 1998-2003 Agere Systems Inc.
 * All rights reserved.
 *   http://www.agere.com
 *
 * Initially developed by TriplePoint, Inc.
 *   http://www.triplepoint.com
 *
 *------------------------------------------------------------------------------
 *
 *   This file contains handler functions registered with the net_device
 *   structure.
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
 * Copyright © 2003 Agere Systems Inc.
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
 * THIS SOFTWARE IS PROVIDED AS IS AND ANY EXPRESS OR IMPLIED WARRANTIES,
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
 ******************************************************************************/

/*******************************************************************************
 * include files
 ******************************************************************************/
#include <wl_version.h>

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/kernel.h>
// #include <linux/sched.h>
// #include <linux/ptrace.h>
// #include <linux/slab.h>
// #include <linux/ctype.h>
// #include <linux/string.h>
//#include <linux/timer.h>
// #include <linux/interrupt.h>
// #include <linux/in.h>
// #include <linux/delay.h>
// #include <linux/skbuff.h>
// #include <asm/io.h>
// // #include <asm/bitops.h>

#include <linux/netdevice.h>
#include <linux/ethtool.h>
#include <linux/etherdevice.h>
// #include <linux/skbuff.h>
// #include <linux/if_arp.h>
// #include <linux/ioport.h>

#include <debug.h>

#include <hcf.h>
#include <dhf.h>
// #include <hcfdef.h>

#include <wl_if.h>
#include <wl_internal.h>
#include <wl_util.h>
#include <wl_priv.h>
#include <wl_main.h>
#include <wl_netdev.h>
#include <wl_wext.h>

#ifdef USE_PROFILE
#include <wl_profile.h>
#endif  /* USE_PROFILE */

#ifdef BUS_PCMCIA
#include <wl_cs.h>
#endif  /* BUS_PCMCIA */

#ifdef BUS_PCI
#include <wl_pci.h>
#endif  /* BUS_PCI */


/*******************************************************************************
 * global variables
 ******************************************************************************/
#if DBG
extern dbg_info_t *DbgInfo;
#endif  /* DBG */


#if HCF_ENCAP
#define MTU_MAX (HCF_MAX_MSG - ETH_HLEN - 8)
#else
#define MTU_MAX (HCF_MAX_MSG - ETH_HLEN)
#endif

//static int mtu = MTU_MAX;
//MODULE_PARM(mtu, "i");
//MODULE_PARM_DESC(mtu, "MTU");

/*******************************************************************************
 * macros
 ******************************************************************************/
#define BLOCK_INPUT(buf, len) \
    desc->buf_addr = buf; \
    desc->BUF_SIZE = len; \
    status = hcf_rcv_msg(&(lp->hcfCtx), desc, 0)

#define BLOCK_INPUT_DMA(buf, len) memcpy( buf, desc_next->buf_addr, pktlen )

/*******************************************************************************
 * function prototypes
 ******************************************************************************/

/*******************************************************************************
 *	wl_init()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      We never need to do anything when a "Wireless" device is "initialized"
 *  by the net software, because we only register already-found cards.
 *
 *  PARAMETERS:
 *
 *      dev - a pointer to the device's net_device structure
 *
 *  RETURNS:
 *
 *      0 on success
 *      errno value otherwise
 *
 ******************************************************************************/
int wl_init( struct net_device *dev )
{
//    unsigned long       flags;
//    struct wl_private   *lp = wl_priv(dev);
    /*------------------------------------------------------------------------*/

    DBG_FUNC( "wl_init" );
    DBG_ENTER( DbgInfo );

    DBG_PARAM( DbgInfo, "dev", "%s (0x%p)", dev->name, dev );

    /* Nothing to do, but grab the spinlock anyway just in case we ever need
       this routine */
//  wl_lock( lp, &flags );
//  wl_unlock( lp, &flags );

    DBG_LEAVE( DbgInfo );
    return 0;
} // wl_init
/*============================================================================*/

/*******************************************************************************
 *	wl_config()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Implement the SIOCSIFMAP interface.
 *
 *  PARAMETERS:
 *
 *      dev - a pointer to the device's net_device structure
 *      map - a pointer to the device's ifmap structure
 *
 *  RETURNS:
 *
 *      0 on success
 *      errno otherwise
 *
 ******************************************************************************/
int wl_config( struct net_device *dev, struct ifmap *map )
{
    DBG_FUNC( "wl_config" );
    DBG_ENTER( DbgInfo );

    DBG_PARAM( DbgInfo, "dev", "%s (0x%p)", dev->name, dev );
    DBG_PARAM( DbgInfo, "map", "0x%p", map );

    /* The only thing we care about here is a port change. Since this not needed,
       ignore the request. */
    DBG_TRACE(DbgInfo, "%s: %s called.\n", dev->name, __func__);

    DBG_LEAVE( DbgInfo );
    return 0;
} // wl_config
/*============================================================================*/

/*******************************************************************************
 *	wl_stats()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Return the current device statistics.
 *
 *  PARAMETERS:
 *
 *      dev - a pointer to the device's net_device structure
 *
 *  RETURNS:
 *
 *      a pointer to a net_device_stats structure containing the network
 *      statistics.
 *
 ******************************************************************************/
struct net_device_stats *wl_stats( struct net_device *dev )
{
#ifdef USE_WDS
    int                         count;
#endif  /* USE_WDS */
    unsigned long               flags;
    struct net_device_stats     *pStats;
    struct wl_private           *lp = wl_priv(dev);
    /*------------------------------------------------------------------------*/

    //DBG_FUNC( "wl_stats" );
    //DBG_ENTER( DbgInfo );
    //DBG_PARAM( DbgInfo, "dev", "%s (0x%p)", dev->name, dev );

    pStats = NULL;

    wl_lock( lp, &flags );

#ifdef USE_RTS
    if( lp->useRTS == 1 ) {
	wl_unlock( lp, &flags );

	//DBG_LEAVE( DbgInfo );
	return NULL;
    }
#endif  /* USE_RTS */

    /* Return the statistics for the appropriate device */
#ifdef USE_WDS

    for( count = 0; count < NUM_WDS_PORTS; count++ ) {
	if( dev == lp->wds_port[count].dev ) {
	    pStats = &( lp->wds_port[count].stats );
	}
    }

#endif  /* USE_WDS */

    /* If pStats is still NULL, then the device is not a WDS port */
    if( pStats == NULL ) {
        pStats = &( lp->stats );
    }

    wl_unlock( lp, &flags );

    //DBG_LEAVE( DbgInfo );

    return pStats;
} // wl_stats
/*============================================================================*/

/*******************************************************************************
 *	wl_open()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Open the device.
 *
 *  PARAMETERS:
 *
 *      dev - a pointer to the device's net_device structure
 *
 *  RETURNS:
 *
 *      0 on success
 *      errno otherwise
 *
 ******************************************************************************/
int wl_open(struct net_device *dev)
{
    int                 status = HCF_SUCCESS;
    struct wl_private   *lp = wl_priv(dev);
    unsigned long       flags;
    /*------------------------------------------------------------------------*/

    DBG_FUNC( "wl_open" );
    DBG_ENTER( DbgInfo );

    wl_lock( lp, &flags );

#ifdef USE_RTS
    if( lp->useRTS == 1 ) {
	DBG_TRACE( DbgInfo, "Skipping device open, in RTS mode\n" );
	wl_unlock( lp, &flags );
	DBG_LEAVE( DbgInfo );
	return -EIO;
    }
#endif  /* USE_RTS */

#ifdef USE_PROFILE
    parse_config( dev );
#endif

    if( lp->portState == WVLAN_PORT_STATE_DISABLED ) {
	DBG_TRACE( DbgInfo, "Enabling Port 0\n" );
	status = wl_enable( lp );

        if( status != HCF_SUCCESS ) {
            DBG_TRACE( DbgInfo, "Enable port 0 failed: 0x%x\n", status );
        }
    }

    // Holding the lock too long, make a gap to allow other processes
    wl_unlock(lp, &flags);
    wl_lock( lp, &flags );

    if ( strlen( lp->fw_image_filename ) ) {
	DBG_TRACE( DbgInfo, ";???? Kludgy way to force a download\n" );
	status = wl_go( lp );
    } else {
	status = wl_apply( lp );
    }

    // Holding the lock too long, make a gap to allow other processes
    wl_unlock(lp, &flags);
    wl_lock( lp, &flags );

    if( status != HCF_SUCCESS ) {
	// Unsuccessful, try reset of the card to recover
	status = wl_reset( dev );
    }

    // Holding the lock too long, make a gap to allow other processes
    wl_unlock(lp, &flags);
    wl_lock( lp, &flags );

    if( status == HCF_SUCCESS ) {
	netif_carrier_on( dev );
	WL_WDS_NETIF_CARRIER_ON( lp );

	lp->is_handling_int = WL_HANDLING_INT; // Start handling interrupts
        wl_act_int_on( lp );

	netif_start_queue( dev );
	WL_WDS_NETIF_START_QUEUE( lp );
    } else {
        wl_hcf_error( dev, status );		/* Report the error */
        netif_device_detach( dev );		/* Stop the device and queue */
    }

    wl_unlock( lp, &flags );

    DBG_LEAVE( DbgInfo );
    return status;
} // wl_open
/*============================================================================*/

/*******************************************************************************
 *	wl_close()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Close the device.
 *
 *  PARAMETERS:
 *
 *      dev - a pointer to the device's net_device structure
 *
 *  RETURNS:
 *
 *      0 on success
 *      errno otherwise
 *
 ******************************************************************************/
int wl_close( struct net_device *dev )
{
    struct wl_private   *lp = wl_priv(dev);
    unsigned long   flags;
    /*------------------------------------------------------------------------*/

    DBG_FUNC("wl_close");
    DBG_ENTER(DbgInfo);
    DBG_PARAM(DbgInfo, "dev", "%s (0x%p)", dev->name, dev);

    /* Mark the adapter as busy */
    netif_stop_queue( dev );
    WL_WDS_NETIF_STOP_QUEUE( lp );

    netif_carrier_off( dev );
    WL_WDS_NETIF_CARRIER_OFF( lp );

    /* Shutdown the adapter:
            Disable adapter interrupts
            Stop Tx/Rx
            Update statistics
            Set low power mode
    */

    wl_lock( lp, &flags );

    wl_act_int_off( lp );
    lp->is_handling_int = WL_NOT_HANDLING_INT; // Stop handling interrupts

#ifdef USE_RTS
    if( lp->useRTS == 1 ) {
	DBG_TRACE( DbgInfo, "Skipping device close, in RTS mode\n" );
	wl_unlock( lp, &flags );
	DBG_LEAVE( DbgInfo );
	return -EIO;
    }
#endif  /* USE_RTS */

    /* Disable the ports */
    wl_disable( lp );

    wl_unlock( lp, &flags );

    DBG_LEAVE( DbgInfo );
    return 0;
} // wl_close
/*============================================================================*/

static void wl_get_drvinfo(struct net_device *dev, struct ethtool_drvinfo *info)
{
    strlcpy(info->driver, DRIVER_NAME, sizeof(info->driver));
    strlcpy(info->version, DRV_VERSION_STR, sizeof(info->version));
//	strlcpy(info.fw_version, priv->fw_name,
//	sizeof(info.fw_version));

    if (dev->dev.parent) {
    	dev_set_name(dev->dev.parent, "%s", info->bus_info);
	//strlcpy(info->bus_info, dev->dev.parent->bus_id,
	//	sizeof(info->bus_info));
    } else {
	snprintf(info->bus_info, sizeof(info->bus_info),
		"PCMCIA FIXME");
//		    "PCMCIA 0x%lx", priv->hw.iobase);
    }
} // wl_get_drvinfo

static struct ethtool_ops wl_ethtool_ops = {
    .get_drvinfo = wl_get_drvinfo,
    .get_link = ethtool_op_get_link,
};


/*******************************************************************************
 *	wl_ioctl()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      The IOCTL handler for the device.
 *
 *  PARAMETERS:
 *
 *      dev - a pointer to the device's net_device struct.
 *      rq  - a pointer to the IOCTL request buffer.
 *      cmd - the IOCTL command code.
 *
 *  RETURNS:
 *
 *      0 on success
 *      errno value otherwise
 *
 ******************************************************************************/
int wl_ioctl( struct net_device *dev, struct ifreq *rq, int cmd )
{
    struct wl_private  *lp = wl_priv(dev);
    unsigned long           flags;
    int                     ret = 0;
    /*------------------------------------------------------------------------*/

    DBG_FUNC( "wl_ioctl" );
    DBG_ENTER(DbgInfo);
    DBG_PARAM(DbgInfo, "dev", "%s (0x%p)", dev->name, dev);
    DBG_PARAM(DbgInfo, "rq", "0x%p", rq);
    DBG_PARAM(DbgInfo, "cmd", "0x%04x", cmd);

    wl_lock( lp, &flags );

    wl_act_int_off( lp );

#ifdef USE_RTS
    if( lp->useRTS == 1 ) {
	/* Handle any RTS IOCTL here */
	if( cmd == WL_IOCTL_RTS ) {
	    DBG_TRACE( DbgInfo, "IOCTL: WL_IOCTL_RTS\n" );
	    ret = wvlan_rts( (struct rtsreq *)rq, dev->base_addr );
	} else {
	    DBG_TRACE( DbgInfo, "IOCTL not supported in RTS mode: 0x%X\n", cmd );
	    ret = -EOPNOTSUPP;
	}

	goto out_act_int_on_unlock;
    }
#endif  /* USE_RTS */

    /* Only handle UIL IOCTL requests when the UIL has the system blocked. */
    if( !(( lp->flags & WVLAN2_UIL_BUSY ) && ( cmd != WVLAN2_IOCTL_UIL ))) {
#ifdef USE_UIL
	struct uilreq  *urq = (struct uilreq *)rq;
#endif /* USE_UIL */

	switch( cmd ) {
		// ================== Private IOCTLs (up to 16) ==================
#ifdef USE_UIL
	case WVLAN2_IOCTL_UIL:
	     DBG_TRACE( DbgInfo, "IOCTL: WVLAN2_IOCTL_UIL\n" );
	     ret = wvlan_uil( urq, lp );
	     break;
#endif  /* USE_UIL */

	default:
	     DBG_TRACE(DbgInfo, "IOCTL CODE NOT SUPPORTED: 0x%X\n", cmd );
	     ret = -EOPNOTSUPP;
	     break;
	}
    } else {
	DBG_WARNING( DbgInfo, "DEVICE IS BUSY, CANNOT PROCESS REQUEST\n" );
	ret = -EBUSY;
    }

#ifdef USE_RTS
out_act_int_on_unlock:
#endif  /* USE_RTS */
    wl_act_int_on( lp );

    wl_unlock( lp, &flags );

    DBG_LEAVE( DbgInfo );
    return ret;
} // wl_ioctl
/*============================================================================*/

#ifdef CONFIG_NET_POLL_CONTROLLER
void wl_poll(struct net_device *dev)
{
    struct wl_private *lp = wl_priv(dev);
    unsigned long flags;
    struct pt_regs regs;

    wl_lock( lp, &flags );
    wl_isr(dev->irq, dev, &regs);
    wl_unlock( lp, &flags );
}
#endif

/*******************************************************************************
 *	wl_tx_timeout()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      The handler called when, for some reason, a Tx request is not completed.
 *
 *  PARAMETERS:
 *
 *      dev - a pointer to the device's net_device struct.
 *
 *  RETURNS:
 *
 *      N/A
 *
 ******************************************************************************/
void wl_tx_timeout( struct net_device *dev )
{
#ifdef USE_WDS
    int                     count;
#endif  /* USE_WDS */
    unsigned long           flags;
    struct wl_private       *lp = wl_priv(dev);
    struct net_device_stats *pStats = NULL;
    /*------------------------------------------------------------------------*/

    DBG_FUNC( "wl_tx_timeout" );
    DBG_ENTER( DbgInfo );

    DBG_WARNING( DbgInfo, "%s: Transmit timeout.\n", dev->name );

    wl_lock( lp, &flags );

#ifdef USE_RTS
    if( lp->useRTS == 1 ) {
	DBG_TRACE( DbgInfo, "Skipping tx_timeout handler, in RTS mode\n" );
	wl_unlock( lp, &flags );

	DBG_LEAVE( DbgInfo );
	return;
    }
#endif  /* USE_RTS */

    /* Figure out which device (the "root" device or WDS port) this timeout
       is for */
#ifdef USE_WDS

    for( count = 0; count < NUM_WDS_PORTS; count++ ) {
	if( dev == lp->wds_port[count].dev ) {
	    pStats = &( lp->wds_port[count].stats );

	    /* Break the loop so that we can use the counter to access WDS
	       information in the private structure */
	    break;
	}
    }

#endif  /* USE_WDS */

    /* If pStats is still NULL, then the device is not a WDS port */
    if( pStats == NULL ) {
	pStats = &( lp->stats );
    }

    /* Accumulate the timeout error */
    pStats->tx_errors++;

    wl_unlock( lp, &flags );

    DBG_LEAVE( DbgInfo );
} // wl_tx_timeout
/*============================================================================*/

/*******************************************************************************
 *	wl_send()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      The routine which performs data transmits.
 *
 *  PARAMETERS:
 *
 *      lp  - a pointer to the device's wl_private struct.
 *
 *  RETURNS:
 *
 *      0 on success
 *      1 on error
 *
 ******************************************************************************/
int wl_send( struct wl_private *lp )
{

    int                 status;
    DESC_STRCT          *desc;
    WVLAN_LFRAME        *txF = NULL;
    struct list_head    *element;
    int                 len;
    /*------------------------------------------------------------------------*/

    DBG_FUNC( "wl_send" );

    if( lp == NULL ) {
        DBG_ERROR( DbgInfo, "Private adapter struct is NULL\n" );
        return FALSE;
    }
    if( lp->dev == NULL ) {
        DBG_ERROR( DbgInfo, "net_device struct in wl_private is NULL\n" );
        return FALSE;
    }

    /* Check for the availability of FIDs; if none are available, don't take any
       frames off the txQ */
    if( lp->hcfCtx.IFB_RscInd == 0 ) {
        return FALSE;
    }

    /* Reclaim the TxQ Elements and place them back on the free queue */
    if( !list_empty( &( lp->txQ[0] ))) {
        element = lp->txQ[0].next;

        txF = (WVLAN_LFRAME * )list_entry( element, WVLAN_LFRAME, node );
        if( txF != NULL ) {
            lp->txF.skb  = txF->frame.skb;
            lp->txF.port = txF->frame.port;

            txF->frame.skb  = NULL;
            txF->frame.port = 0;

            list_del( &( txF->node ));
            list_add( element, &( lp->txFree ));

            lp->txQ_count--;

            if( lp->txQ_count < TX_Q_LOW_WATER_MARK ) {
                if( lp->netif_queue_on == FALSE ) {
                    DBG_TX( DbgInfo, "Kickstarting Q: %d\n", lp->txQ_count );
                    netif_wake_queue( lp->dev );
                    WL_WDS_NETIF_WAKE_QUEUE( lp );
                    lp->netif_queue_on = TRUE;
                }
            }
        }
    }

    if( lp->txF.skb == NULL ) {
        return FALSE;
    }

    /* If the device has resources (FIDs) available, then Tx the packet */
    /* Format the TxRequest and send it to the adapter */
    len = lp->txF.skb->len < ETH_ZLEN ? ETH_ZLEN : lp->txF.skb->len;

    desc                    = &( lp->desc_tx );
    desc->buf_addr          = lp->txF.skb->data;
    desc->BUF_CNT           = len;
    desc->next_desc_addr    = NULL;

    status = hcf_send_msg( &( lp->hcfCtx ), desc, lp->txF.port );

    if( status == HCF_SUCCESS ) {
        lp->dev->trans_start = jiffies;

        DBG_TX( DbgInfo, "Transmit...\n" );

        if( lp->txF.port == HCF_PORT_0 ) {
            lp->stats.tx_packets++;
            lp->stats.tx_bytes += lp->txF.skb->len;
        }

#ifdef USE_WDS
        else
        {
            lp->wds_port[(( lp->txF.port >> 8 ) - 1)].stats.tx_packets++;
            lp->wds_port[(( lp->txF.port >> 8 ) - 1)].stats.tx_bytes += lp->txF.skb->len;
        }

#endif  /* USE_WDS */

        /* Free the skb and perform queue cleanup, as the buffer was
            transmitted successfully */
        dev_kfree_skb( lp->txF.skb );

        lp->txF.skb = NULL;
        lp->txF.port = 0;
    }

    return TRUE;
} // wl_send
/*============================================================================*/

/*******************************************************************************
 *	wl_tx()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      The Tx handler function for the network layer.
 *
 *  PARAMETERS:
 *
 *      skb - a pointer to the sk_buff structure containing the data to transfer.
 *      dev - a pointer to the device's net_device structure.
 *
 *  RETURNS:
 *
 *      0 on success
 *      1 on error
 *
 ******************************************************************************/
int wl_tx( struct sk_buff *skb, struct net_device *dev, int port )
{
    unsigned long           flags;
    struct wl_private       *lp = wl_priv(dev);
    WVLAN_LFRAME            *txF = NULL;
    struct list_head        *element;
    /*------------------------------------------------------------------------*/

    DBG_FUNC( "wl_tx" );

    /* Grab the spinlock */
    wl_lock( lp, &flags );

    if( lp->flags & WVLAN2_UIL_BUSY ) {
        DBG_WARNING( DbgInfo, "UIL has device blocked\n" );
        /* Start dropping packets here??? */
	wl_unlock( lp, &flags );
        return 1;
    }

#ifdef USE_RTS
    if( lp->useRTS == 1 ) {
        DBG_PRINT( "RTS: we're getting a Tx...\n" );
	wl_unlock( lp, &flags );
        return 1;
    }
#endif  /* USE_RTS */

    if( !lp->use_dma ) {
        /* Get an element from the queue */
        element = lp->txFree.next;
        txF = (WVLAN_LFRAME *)list_entry( element, WVLAN_LFRAME, node );
        if( txF == NULL ) {
            DBG_ERROR( DbgInfo, "Problem with list_entry\n" );
	    wl_unlock( lp, &flags );
            return 1;
        }
        /* Fill out the frame */
        txF->frame.skb = skb;
        txF->frame.port = port;
        /* Move the frame to the txQ */
        /* NOTE: Here's where we would do priority queueing */
        list_move(&(txF->node), &(lp->txQ[0]));

        lp->txQ_count++;
        if( lp->txQ_count >= DEFAULT_NUM_TX_FRAMES ) {
            DBG_TX( DbgInfo, "Q Full: %d\n", lp->txQ_count );
            if( lp->netif_queue_on == TRUE ) {
                netif_stop_queue( lp->dev );
                WL_WDS_NETIF_STOP_QUEUE( lp );
                lp->netif_queue_on = FALSE;
            }
        }
    }
    wl_act_int_off( lp ); /* Disable Interrupts */

    /* Send the data to the hardware using the appropriate method */
#ifdef ENABLE_DMA
    if( lp->use_dma ) {
        wl_send_dma( lp, skb, port );
    }
    else
#endif
    {
        wl_send( lp );
    }
    /* Re-enable Interrupts, release the spinlock and return */
    wl_act_int_on( lp );
    wl_unlock( lp, &flags );
    return 0;
} // wl_tx
/*============================================================================*/

/*******************************************************************************
 *	wl_rx()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      The routine which performs data reception.
 *
 *  PARAMETERS:
 *
 *      dev - a pointer to the device's net_device structure.
 *
 *  RETURNS:
 *
 *      0 on success
 *      1 on error
 *
 ******************************************************************************/
int wl_rx(struct net_device *dev)
{
    int                     port;
    struct sk_buff          *skb;
    struct wl_private       *lp = wl_priv(dev);
    int                     status;
    hcf_16                  pktlen;
    hcf_16                  hfs_stat;
    DESC_STRCT              *desc;
    /*------------------------------------------------------------------------*/

    DBG_FUNC("wl_rx")
    DBG_PARAM(DbgInfo, "dev", "%s (0x%p)", dev->name, dev);

    if(!( lp->flags & WVLAN2_UIL_BUSY )) {

#ifdef USE_RTS
        if( lp->useRTS == 1 ) {
            DBG_PRINT( "RTS: We're getting an Rx...\n" );
            return -EIO;
        }
#endif  /* USE_RTS */

        /* Read the HFS_STAT register from the lookahead buffer */
        hfs_stat = (hcf_16)(( lp->lookAheadBuf[HFS_STAT] ) |
                            ( lp->lookAheadBuf[HFS_STAT + 1] << 8 ));

        /* Make sure the frame isn't bad */
        if(( hfs_stat & HFS_STAT_ERR ) != HCF_SUCCESS ) {
            DBG_WARNING( DbgInfo, "HFS_STAT_ERROR (0x%x) in Rx Packet\n",
                         lp->lookAheadBuf[HFS_STAT] );
            return -EIO;
        }

        /* Determine what port this packet is for */
        port = ( hfs_stat >> 8 ) & 0x0007;
        DBG_RX( DbgInfo, "Rx frame for port %d\n", port );

        pktlen = lp->hcfCtx.IFB_RxLen;
        if (pktlen != 0) {
            skb = ALLOC_SKB(pktlen);
            if (skb != NULL) {
                /* Set the netdev based on the port */
                switch( port ) {
#ifdef USE_WDS
                case 1:
                case 2:
                case 3:
                case 4:
                case 5:
                case 6:
                    skb->dev = lp->wds_port[port-1].dev;
                    break;
#endif  /* USE_WDS */

                case 0:
                default:
                    skb->dev = dev;
                    break;
                }

                desc = &( lp->desc_rx );

                desc->next_desc_addr = NULL;

/*
#define BLOCK_INPUT(buf, len) \
    desc->buf_addr = buf; \
    desc->BUF_SIZE = len; \
    status = hcf_rcv_msg(&(lp->hcfCtx), desc, 0)
*/

                GET_PACKET( skb->dev, skb, pktlen );

                if( status == HCF_SUCCESS ) {
                    netif_rx( skb );

                    if( port == 0 ) {
                        lp->stats.rx_packets++;
                        lp->stats.rx_bytes += pktlen;
                    }
#ifdef USE_WDS
                    else
                    {
                        lp->wds_port[port-1].stats.rx_packets++;
                        lp->wds_port[port-1].stats.rx_bytes += pktlen;
                    }
#endif  /* USE_WDS */

                    dev->last_rx = jiffies;

#ifdef WIRELESS_EXT
#ifdef WIRELESS_SPY
                    if( lp->spydata.spy_number > 0 ) {
                        char *srcaddr = skb->mac.raw + MAC_ADDR_SIZE;

                        wl_spy_gather( dev, srcaddr );
                    }
#endif /* WIRELESS_SPY */
#endif /* WIRELESS_EXT */
                } else {
                    DBG_ERROR( DbgInfo, "Rx request to card FAILED\n" );

                    if( port == 0 ) {
                        lp->stats.rx_dropped++;
                    }
#ifdef USE_WDS
                    else
                    {
                        lp->wds_port[port-1].stats.rx_dropped++;
                    }
#endif  /* USE_WDS */

                    dev_kfree_skb( skb );
                }
            } else {
                DBG_ERROR( DbgInfo, "Could not alloc skb\n" );

                if( port == 0 ) {
                    lp->stats.rx_dropped++;
                }
#ifdef USE_WDS
                else
                {
                    lp->wds_port[port-1].stats.rx_dropped++;
                }
#endif  /* USE_WDS */
            }
        }
    }

    return 0;
} // wl_rx
/*============================================================================*/

/*******************************************************************************
 *	wl_multicast()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Function to handle multicast packets
 *
 *  PARAMETERS:
 *
 *      dev - a pointer to the device's net_device structure.
 *
 *  RETURNS:
 *
 *      N/A
 *
 ******************************************************************************/
#ifdef NEW_MULTICAST

void wl_multicast( struct net_device *dev )
{
#if 1 //;? (HCF_TYPE) & HCF_TYPE_STA //;?should we return an error status in AP mode
//;?seems reasonable that even an AP-only driver could afford this small additional footprint

    int                 x;
    struct netdev_hw_addr *ha;
    struct wl_private   *lp = wl_priv(dev);
    unsigned long       flags;
    /*------------------------------------------------------------------------*/

    DBG_FUNC( "wl_multicast" );
    DBG_ENTER( DbgInfo );
    DBG_PARAM( DbgInfo, "dev", "%s (0x%p)", dev->name, dev );

    if( !wl_adapter_is_open( dev )) {
        DBG_LEAVE( DbgInfo );
        return;
    }

#if DBG
    if( DBG_FLAGS( DbgInfo ) & DBG_PARAM_ON ) {
        DBG_PRINT("  flags: %s%s%s\n",
            ( dev->flags & IFF_PROMISC ) ? "Promiscuous " : "",
            ( dev->flags & IFF_MULTICAST ) ? "Multicast " : "",
            ( dev->flags & IFF_ALLMULTI ) ? "All-Multicast" : "" );

        DBG_PRINT( "  mc_count: %d\n", netdev_mc_count(dev));

	netdev_for_each_mc_addr(ha, dev)
	DBG_PRINT("    %pM (%d)\n", ha->addr, dev->addr_len);
    }
#endif /* DBG */

    if(!( lp->flags & WVLAN2_UIL_BUSY )) {

#ifdef USE_RTS
        if( lp->useRTS == 1 ) {
            DBG_TRACE( DbgInfo, "Skipping multicast, in RTS mode\n" );

            DBG_LEAVE( DbgInfo );
            return;
        }
#endif  /* USE_RTS */

        wl_lock( lp, &flags );
        wl_act_int_off( lp );

		if ( CNV_INT_TO_LITTLE( lp->hcfCtx.IFB_FWIdentity.comp_id ) == COMP_ID_FW_STA  ) {
            if( dev->flags & IFF_PROMISC ) {
                /* Enable promiscuous mode */
                lp->ltvRecord.len       = 2;
                lp->ltvRecord.typ       = CFG_PROMISCUOUS_MODE;
                lp->ltvRecord.u.u16[0]  = CNV_INT_TO_LITTLE( 1 );
                DBG_PRINT( "Enabling Promiscuous mode (IFF_PROMISC)\n" );
                hcf_put_info( &( lp->hcfCtx ), (LTVP)&( lp->ltvRecord ));
            }
            else if ((netdev_mc_count(dev) > HCF_MAX_MULTICAST) ||
                    ( dev->flags & IFF_ALLMULTI )) {
                /* Shutting off this filter will enable all multicast frames to
                   be sent up from the device; however, this is a static RID, so
                   a call to wl_apply() is needed */
                lp->ltvRecord.len       = 2;
                lp->ltvRecord.typ       = CFG_CNF_RX_ALL_GROUP_ADDR;
                lp->ltvRecord.u.u16[0]  = CNV_INT_TO_LITTLE( 0 );
                DBG_PRINT( "Enabling all multicast mode (IFF_ALLMULTI)\n" );
                hcf_put_info( &( lp->hcfCtx ), (LTVP)&( lp->ltvRecord ));
                wl_apply( lp );
            }
            else if (!netdev_mc_empty(dev)) {
                /* Set the multicast addresses */
                lp->ltvRecord.len = ( netdev_mc_count(dev) * 3 ) + 1;
                lp->ltvRecord.typ = CFG_GROUP_ADDR;

		x = 0;
		netdev_for_each_mc_addr(ha, dev)
                    memcpy(&(lp->ltvRecord.u.u8[x++ * ETH_ALEN]),
			   ha->addr, ETH_ALEN);
                DBG_PRINT( "Setting multicast list\n" );
                hcf_put_info( &( lp->hcfCtx ), (LTVP)&( lp->ltvRecord ));
            } else {
                /* Disable promiscuous mode */
                lp->ltvRecord.len       = 2;
                lp->ltvRecord.typ       = CFG_PROMISCUOUS_MODE;
                lp->ltvRecord.u.u16[0]  = CNV_INT_TO_LITTLE( 0 );
                DBG_PRINT( "Disabling Promiscuous mode\n" );
                hcf_put_info( &( lp->hcfCtx ), (LTVP)&( lp->ltvRecord ));

                /* Disable multicast mode */
                lp->ltvRecord.len = 2;
                lp->ltvRecord.typ = CFG_GROUP_ADDR;
                DBG_PRINT( "Disabling Multicast mode\n" );
                hcf_put_info( &( lp->hcfCtx ), (LTVP)&( lp->ltvRecord ));

                /* Turning on this filter will prevent all multicast frames from
                   being sent up from the device; however, this is a static RID,
                   so a call to wl_apply() is needed */
                lp->ltvRecord.len       = 2;
                lp->ltvRecord.typ       = CFG_CNF_RX_ALL_GROUP_ADDR;
                lp->ltvRecord.u.u16[0]  = CNV_INT_TO_LITTLE( 1 );
                DBG_PRINT( "Disabling all multicast mode (IFF_ALLMULTI)\n" );
                hcf_put_info( &( lp->hcfCtx ), (LTVP)&( lp->ltvRecord ));
                wl_apply( lp );
            }
        }
        wl_act_int_on( lp );
	wl_unlock( lp, &flags );
    }
    DBG_LEAVE( DbgInfo );
#endif /* HCF_STA */
} // wl_multicast
/*============================================================================*/

#else /* NEW_MULTICAST */

void wl_multicast( struct net_device *dev, int num_addrs, void *addrs )
{
    DBG_FUNC( "wl_multicast");
    DBG_ENTER(DbgInfo);

    DBG_PARAM( DbgInfo, "dev", "%s (0x%p)", dev->name, dev );
    DBG_PARAM( DbgInfo, "num_addrs", "%d", num_addrs );
    DBG_PARAM( DbgInfo, "addrs", "0x%p", addrs );

#error Obsolete set multicast interface!

    DBG_LEAVE( DbgInfo );
} // wl_multicast
/*============================================================================*/

#endif /* NEW_MULTICAST */

static const struct net_device_ops wl_netdev_ops =
{
    .ndo_start_xmit         = &wl_tx_port0,

    .ndo_set_config         = &wl_config,
    .ndo_get_stats          = &wl_stats,
    .ndo_set_rx_mode        = &wl_multicast,

    .ndo_init               = &wl_insert,
    .ndo_open               = &wl_adapter_open,
    .ndo_stop               = &wl_adapter_close,
    .ndo_do_ioctl           = &wl_ioctl,

    .ndo_tx_timeout         = &wl_tx_timeout,

#ifdef CONFIG_NET_POLL_CONTROLLER
    .ndo_poll_controller    = wl_poll,
#endif
};

/*******************************************************************************
 *	wl_device_alloc()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Create instances of net_device and wl_private for the new adapter
 *  and register the device's entry points in the net_device structure.
 *
 *  PARAMETERS:
 *
 *      N/A
 *
 *  RETURNS:
 *
 *      a pointer to an allocated and initialized net_device struct for this
 *      device.
 *
 ******************************************************************************/
struct net_device * wl_device_alloc( void )
{
    struct net_device   *dev = NULL;
    struct wl_private   *lp = NULL;
    /*------------------------------------------------------------------------*/

    DBG_FUNC( "wl_device_alloc" );
    DBG_ENTER( DbgInfo );

    /* Alloc a net_device struct */
    dev = alloc_etherdev(sizeof(struct wl_private));
    if (!dev)
        return NULL;

    /* Initialize the 'next' pointer in the struct. Currently only used for PCI,
       but do it here just in case it's used for other buses in the future */
    lp = wl_priv(dev);


    /* Check MTU */
    if( dev->mtu > MTU_MAX )
    {
	    DBG_WARNING( DbgInfo, "%s: MTU set too high, limiting to %d.\n",
                        dev->name, MTU_MAX );
    	dev->mtu = MTU_MAX;
    }

    /* Setup the function table in the device structure. */

    dev->wireless_handlers = (struct iw_handler_def *)&wl_iw_handler_def;
    lp->wireless_data.spy_data = &lp->spy_data;
    dev->wireless_data = &lp->wireless_data;

    dev->netdev_ops = &wl_netdev_ops;

    dev->watchdog_timeo     = TX_TIMEOUT;

    dev->ethtool_ops	    = &wl_ethtool_ops;

    netif_stop_queue( dev );

    /* Allocate virtual devices for WDS support if needed */
    WL_WDS_DEVICE_ALLOC( lp );

    DBG_LEAVE( DbgInfo );
    return dev;
} // wl_device_alloc
/*============================================================================*/

/*******************************************************************************
 *	wl_device_dealloc()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Free instances of net_device and wl_private strcutres for an adapter
 *  and perform basic cleanup.
 *
 *  PARAMETERS:
 *
 *      dev - a pointer to the device's net_device structure.
 *
 *  RETURNS:
 *
 *      N/A
 *
 ******************************************************************************/
void wl_device_dealloc( struct net_device *dev )
{
//    struct wl_private   *lp = wl_priv(dev);
    /*------------------------------------------------------------------------*/

    DBG_FUNC( "wl_device_dealloc" );
    DBG_ENTER( DbgInfo );

    /* Dealloc the WDS ports */
    WL_WDS_DEVICE_DEALLOC( lp );

    free_netdev( dev );

    DBG_LEAVE( DbgInfo );
} // wl_device_dealloc
/*============================================================================*/

/*******************************************************************************
 *	wl_tx_port0()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      The handler routine for Tx over HCF_PORT_0.
 *
 *  PARAMETERS:
 *
 *      skb - a pointer to the sk_buff to transmit.
 *      dev - a pointer to a net_device structure representing HCF_PORT_0.
 *
 *  RETURNS:
 *
 *      N/A
 *
 ******************************************************************************/
int wl_tx_port0( struct sk_buff *skb, struct net_device *dev )
{
    DBG_TX( DbgInfo, "Tx on Port 0\n" );

    return wl_tx( skb, dev, HCF_PORT_0 );
#ifdef ENABLE_DMA
    return wl_tx_dma( skb, dev, HCF_PORT_0 );
#endif
} // wl_tx_port0
/*============================================================================*/

#ifdef USE_WDS

/*******************************************************************************
 *	wl_tx_port1()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      The handler routine for Tx over HCF_PORT_1.
 *
 *  PARAMETERS:
 *
 *      skb - a pointer to the sk_buff to transmit.
 *      dev - a pointer to a net_device structure representing HCF_PORT_1.
 *
 *  RETURNS:
 *
 *      N/A
 *
 ******************************************************************************/
int wl_tx_port1( struct sk_buff *skb, struct net_device *dev )
{
    DBG_TX( DbgInfo, "Tx on Port 1\n" );
    return wl_tx( skb, dev, HCF_PORT_1 );
} // wl_tx_port1
/*============================================================================*/

/*******************************************************************************
 *	wl_tx_port2()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      The handler routine for Tx over HCF_PORT_2.
 *
 *  PARAMETERS:
 *
 *      skb - a pointer to the sk_buff to transmit.
 *      dev - a pointer to a net_device structure representing HCF_PORT_2.
 *
 *  RETURNS:
 *
 *      N/A
 *
 ******************************************************************************/
int wl_tx_port2( struct sk_buff *skb, struct net_device *dev )
{
    DBG_TX( DbgInfo, "Tx on Port 2\n" );
    return wl_tx( skb, dev, HCF_PORT_2 );
} // wl_tx_port2
/*============================================================================*/

/*******************************************************************************
 *	wl_tx_port3()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      The handler routine for Tx over HCF_PORT_3.
 *
 *  PARAMETERS:
 *
 *      skb - a pointer to the sk_buff to transmit.
 *      dev - a pointer to a net_device structure representing HCF_PORT_3.
 *
 *  RETURNS:
 *
 *      N/A
 *
 ******************************************************************************/
int wl_tx_port3( struct sk_buff *skb, struct net_device *dev )
{
    DBG_TX( DbgInfo, "Tx on Port 3\n" );
    return wl_tx( skb, dev, HCF_PORT_3 );
} // wl_tx_port3
/*============================================================================*/

/*******************************************************************************
 *	wl_tx_port4()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      The handler routine for Tx over HCF_PORT_4.
 *
 *  PARAMETERS:
 *
 *      skb - a pointer to the sk_buff to transmit.
 *      dev - a pointer to a net_device structure representing HCF_PORT_4.
 *
 *  RETURNS:
 *
 *      N/A
 *
 ******************************************************************************/
int wl_tx_port4( struct sk_buff *skb, struct net_device *dev )
{
    DBG_TX( DbgInfo, "Tx on Port 4\n" );
    return wl_tx( skb, dev, HCF_PORT_4 );
} // wl_tx_port4
/*============================================================================*/

/*******************************************************************************
 *	wl_tx_port5()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      The handler routine for Tx over HCF_PORT_5.
 *
 *  PARAMETERS:
 *
 *      skb - a pointer to the sk_buff to transmit.
 *      dev - a pointer to a net_device structure representing HCF_PORT_5.
 *
 *  RETURNS:
 *
 *      N/A
 *
 ******************************************************************************/
int wl_tx_port5( struct sk_buff *skb, struct net_device *dev )
{
    DBG_TX( DbgInfo, "Tx on Port 5\n" );
    return wl_tx( skb, dev, HCF_PORT_5 );
} // wl_tx_port5
/*============================================================================*/

/*******************************************************************************
 *	wl_tx_port6()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      The handler routine for Tx over HCF_PORT_6.
 *
 *  PARAMETERS:
 *
 *      skb - a pointer to the sk_buff to transmit.
 *      dev - a pointer to a net_device structure representing HCF_PORT_6.
 *
 *  RETURNS:
 *
 *      N/A
 *
 ******************************************************************************/
int wl_tx_port6( struct sk_buff *skb, struct net_device *dev )
{
    DBG_TX( DbgInfo, "Tx on Port 6\n" );
    return wl_tx( skb, dev, HCF_PORT_6 );
} // wl_tx_port6
/*============================================================================*/

/*******************************************************************************
 *	wl_wds_device_alloc()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Create instances of net_device to represent the WDS ports, and register
 *  the device's entry points in the net_device structure.
 *
 *  PARAMETERS:
 *
 *      lp  - a pointer to the device's private adapter structure
 *
 *  RETURNS:
 *
 *      N/A, but will place pointers to the allocated and initialized net_device
 *      structs in the private adapter structure.
 *
 ******************************************************************************/
void wl_wds_device_alloc( struct wl_private *lp )
{
    int count;
    /*------------------------------------------------------------------------*/

    DBG_FUNC( "wl_wds_device_alloc" );
    DBG_ENTER( DbgInfo );

    /* WDS support requires additional net_device structs to be allocated,
       so that user space apps can use these virtual devices to specify the
       port on which to Tx/Rx */
    for( count = 0; count < NUM_WDS_PORTS; count++ ) {
        struct net_device *dev_wds = NULL;

	dev_wds = kzalloc(sizeof(struct net_device), GFP_KERNEL);
	if (!dev_wds) {
		DBG_LEAVE(DbgInfo);
		return;
	}

        ether_setup( dev_wds );

        lp->wds_port[count].dev = dev_wds;

        /* Re-use wl_init for all the devices, as it currently does nothing, but
           is required. Re-use the stats/tx_timeout handler for all as well; the
           WDS port which is requesting these operations can be determined by
           the net_device pointer. Set the private member of all devices to point
           to the same net_device struct; that way, all information gets
           funnelled through the one "real" net_device. Name the WDS ports
           "wds<n>" */
        lp->wds_port[count].dev->init           = &wl_init;
        lp->wds_port[count].dev->get_stats      = &wl_stats;
        lp->wds_port[count].dev->tx_timeout     = &wl_tx_timeout;
        lp->wds_port[count].dev->watchdog_timeo = TX_TIMEOUT;
        lp->wds_port[count].dev->priv           = lp;

        sprintf( lp->wds_port[count].dev->name, "wds%d", count );
    }

    /* Register the Tx handlers */
    lp->wds_port[0].dev->hard_start_xmit = &wl_tx_port1;
    lp->wds_port[1].dev->hard_start_xmit = &wl_tx_port2;
    lp->wds_port[2].dev->hard_start_xmit = &wl_tx_port3;
    lp->wds_port[3].dev->hard_start_xmit = &wl_tx_port4;
    lp->wds_port[4].dev->hard_start_xmit = &wl_tx_port5;
    lp->wds_port[5].dev->hard_start_xmit = &wl_tx_port6;

    WL_WDS_NETIF_STOP_QUEUE( lp );

    DBG_LEAVE( DbgInfo );
} // wl_wds_device_alloc
/*============================================================================*/

/*******************************************************************************
 *	wl_wds_device_dealloc()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Free instances of net_device structures used to support WDS.
 *
 *  PARAMETERS:
 *
 *      lp  - a pointer to the device's private adapter structure
 *
 *  RETURNS:
 *
 *      N/A
 *
 ******************************************************************************/
void wl_wds_device_dealloc( struct wl_private *lp )
{
    int count;
    /*------------------------------------------------------------------------*/

    DBG_FUNC( "wl_wds_device_dealloc" );
    DBG_ENTER( DbgInfo );

    for( count = 0; count < NUM_WDS_PORTS; count++ ) {
        struct net_device *dev_wds = NULL;

        dev_wds = lp->wds_port[count].dev;

        if( dev_wds != NULL ) {
            if( dev_wds->flags & IFF_UP ) {
                dev_close( dev_wds );
                dev_wds->flags &= ~( IFF_UP | IFF_RUNNING );
            }

            free_netdev(dev_wds);
            lp->wds_port[count].dev = NULL;
        }
    }

    DBG_LEAVE( DbgInfo );
} // wl_wds_device_dealloc
/*============================================================================*/

/*******************************************************************************
 *	wl_wds_netif_start_queue()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Used to start the netif queues of all the "virtual" network devices
 *      which represent the WDS ports.
 *
 *  PARAMETERS:
 *
 *      lp  - a pointer to the device's private adapter structure
 *
 *  RETURNS:
 *
 *      N/A
 *
 ******************************************************************************/
void wl_wds_netif_start_queue( struct wl_private *lp )
{
    int count;
    /*------------------------------------------------------------------------*/

    if( lp != NULL ) {
        for( count = 0; count < NUM_WDS_PORTS; count++ ) {
            if( lp->wds_port[count].is_registered &&
                lp->wds_port[count].netif_queue_on == FALSE ) {
                netif_start_queue( lp->wds_port[count].dev );
                lp->wds_port[count].netif_queue_on = TRUE;
            }
        }
    }
} // wl_wds_netif_start_queue
/*============================================================================*/

/*******************************************************************************
 *	wl_wds_netif_stop_queue()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Used to stop the netif queues of all the "virtual" network devices
 *      which represent the WDS ports.
 *
 *  PARAMETERS:
 *
 *      lp  - a pointer to the device's private adapter structure
 *
 *  RETURNS:
 *
 *      N/A
 *
 ******************************************************************************/
void wl_wds_netif_stop_queue( struct wl_private *lp )
{
    int count;
    /*------------------------------------------------------------------------*/

    if( lp != NULL ) {
        for( count = 0; count < NUM_WDS_PORTS; count++ ) {
            if( lp->wds_port[count].is_registered &&
                lp->wds_port[count].netif_queue_on == TRUE ) {
                netif_stop_queue( lp->wds_port[count].dev );
                lp->wds_port[count].netif_queue_on = FALSE;
            }
        }
    }
} // wl_wds_netif_stop_queue
/*============================================================================*/

/*******************************************************************************
 *	wl_wds_netif_wake_queue()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Used to wake the netif queues of all the "virtual" network devices
 *      which represent the WDS ports.
 *
 *  PARAMETERS:
 *
 *      lp  - a pointer to the device's private adapter structure
 *
 *  RETURNS:
 *
 *      N/A
 *
 ******************************************************************************/
void wl_wds_netif_wake_queue( struct wl_private *lp )
{
    int count;
    /*------------------------------------------------------------------------*/

    if( lp != NULL ) {
        for( count = 0; count < NUM_WDS_PORTS; count++ ) {
            if( lp->wds_port[count].is_registered &&
                lp->wds_port[count].netif_queue_on == FALSE ) {
                netif_wake_queue( lp->wds_port[count].dev );
                lp->wds_port[count].netif_queue_on = TRUE;
            }
        }
    }
} // wl_wds_netif_wake_queue
/*============================================================================*/

/*******************************************************************************
 *	wl_wds_netif_carrier_on()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Used to signal the network layer that carrier is present on all of the
 *      "virtual" network devices which represent the WDS ports.
 *
 *  PARAMETERS:
 *
 *      lp  - a pointer to the device's private adapter structure
 *
 *  RETURNS:
 *
 *      N/A
 *
 ******************************************************************************/
void wl_wds_netif_carrier_on( struct wl_private *lp )
{
    int count;
    /*------------------------------------------------------------------------*/

    if( lp != NULL ) {
        for( count = 0; count < NUM_WDS_PORTS; count++ ) {
            if( lp->wds_port[count].is_registered ) {
                netif_carrier_on( lp->wds_port[count].dev );
            }
        }
    }
} // wl_wds_netif_carrier_on
/*============================================================================*/

/*******************************************************************************
 *	wl_wds_netif_carrier_off()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Used to signal the network layer that carrier is NOT present on all of
 *      the "virtual" network devices which represent the WDS ports.
 *
 *  PARAMETERS:
 *
 *      lp  - a pointer to the device's private adapter structure
 *
 *  RETURNS:
 *
 *      N/A
 *
 ******************************************************************************/
void wl_wds_netif_carrier_off( struct wl_private *lp )
{
	int count;

	if(lp != NULL) {
		for(count = 0; count < NUM_WDS_PORTS; count++) {
			if(lp->wds_port[count].is_registered)
				netif_carrier_off(lp->wds_port[count].dev);
		}
	}

} // wl_wds_netif_carrier_off
/*============================================================================*/

#endif  /* USE_WDS */

#ifdef ENABLE_DMA
/*******************************************************************************
 *	wl_send_dma()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      The routine which performs data transmits when using busmaster DMA.
 *
 *  PARAMETERS:
 *
 *      lp   - a pointer to the device's wl_private struct.
 *      skb  - a pointer to the network layer's data buffer.
 *      port - the Hermes port on which to transmit.
 *
 *  RETURNS:
 *
 *      0 on success
 *      1 on error
 *
 ******************************************************************************/
int wl_send_dma( struct wl_private *lp, struct sk_buff *skb, int port )
{
    int         len;
    DESC_STRCT *desc = NULL;
    DESC_STRCT *desc_next = NULL;
    /*------------------------------------------------------------------------*/

    DBG_FUNC( "wl_send_dma" );

    if( lp == NULL ) {
        DBG_ERROR( DbgInfo, "Private adapter struct is NULL\n" );
        return FALSE;
    }

    if( lp->dev == NULL ) {
        DBG_ERROR( DbgInfo, "net_device struct in wl_private is NULL\n" );
        return FALSE;
    }

    /* AGAIN, ALL THE QUEUEING DONE HERE IN I/O MODE IS NOT PERFORMED */

    if( skb == NULL ) {
        DBG_WARNING (DbgInfo, "Nothing to send.\n");
        return FALSE;
    }

    len = skb->len;

    /* Get a free descriptor */
    desc = wl_pci_dma_get_tx_packet( lp );

    if( desc == NULL ) {
        if( lp->netif_queue_on == TRUE ) {
            netif_stop_queue( lp->dev );
            WL_WDS_NETIF_STOP_QUEUE( lp );
            lp->netif_queue_on = FALSE;

            dev_kfree_skb( skb );
            return 0;
        }
    }

    SET_BUF_CNT( desc, /*HCF_DMA_FD_CNT*/HFS_ADDR_DEST );
    SET_BUF_SIZE( desc, HCF_DMA_TX_BUF1_SIZE );

    desc_next = desc->next_desc_addr;

    if( desc_next->buf_addr == NULL ) {
        DBG_ERROR( DbgInfo, "DMA descriptor buf_addr is NULL\n" );
        return FALSE;
    }

    /* Copy the payload into the DMA packet */
    memcpy( desc_next->buf_addr, skb->data, len );

    SET_BUF_CNT( desc_next, len );
    SET_BUF_SIZE( desc_next, HCF_MAX_PACKET_SIZE );

    hcf_dma_tx_put( &( lp->hcfCtx ), desc, 0 );

    /* Free the skb and perform queue cleanup, as the buffer was
            transmitted successfully */
    dev_kfree_skb( skb );

    return TRUE;
} // wl_send_dma
/*============================================================================*/

/*******************************************************************************
 *	wl_rx_dma()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      The routine which performs data reception when using busmaster DMA.
 *
 *  PARAMETERS:
 *
 *      dev - a pointer to the device's net_device structure.
 *
 *  RETURNS:
 *
 *      0 on success
 *      1 on error
 *
 ******************************************************************************/
int wl_rx_dma( struct net_device *dev )
{
    int                      port;
    hcf_16                   pktlen;
    hcf_16                   hfs_stat;
    struct sk_buff          *skb;
    struct wl_private       *lp = NULL;
    DESC_STRCT              *desc, *desc_next;
    //CFG_MB_INFO_RANGE2_STRCT x;
    /*------------------------------------------------------------------------*/

    DBG_FUNC("wl_rx")
    DBG_PARAM(DbgInfo, "dev", "%s (0x%p)", dev->name, dev);

    if((( lp = dev->priv ) != NULL ) &&
	!( lp->flags & WVLAN2_UIL_BUSY )) {

#ifdef USE_RTS
        if( lp->useRTS == 1 ) {
            DBG_PRINT( "RTS: We're getting an Rx...\n" );
            return -EIO;
        }
#endif  /* USE_RTS */

        //if( lp->dma.status == 0 )
        //{
            desc = hcf_dma_rx_get( &( lp->hcfCtx ));

            if( desc != NULL )
            {
                /* Check and see if we rcvd. a WMP frame */
                /*
                if((( *(hcf_8 *)&desc->buf_addr[HFS_STAT] ) &
                    ( HFS_STAT_MSG_TYPE | HFS_STAT_ERR )) == HFS_STAT_WMP_MSG )
                {
                    DBG_TRACE( DbgInfo, "Got a WMP frame\n" );

                    x.len = sizeof( CFG_MB_INFO_RANGE2_STRCT ) / sizeof( hcf_16 );
				    x.typ = CFG_MB_INFO;
				    x.base_typ = CFG_WMP;
				    x.frag_cnt = 2;
				    x.frag_buf[0].frag_len  = GET_BUF_CNT( descp ) / sizeof( hcf_16 );
				    x.frag_buf[0].frag_addr = (hcf_8 *) descp->buf_addr ;
				    x.frag_buf[1].frag_len  = ( GET_BUF_CNT( descp->next_desc_addr ) + 1 ) / sizeof( hcf_16 );
				    x.frag_buf[1].frag_addr = (hcf_8 *) descp->next_desc_addr->buf_addr ;

                    hcf_put_info( &( lp->hcfCtx ), (LTVP)&x );
                }
                */

                desc_next = desc->next_desc_addr;

                /* Make sure the buffer isn't empty */
                if( GET_BUF_CNT( desc ) == 0 ) {
                    DBG_WARNING( DbgInfo, "Buffer is empty!\n" );

                    /* Give the descriptor back to the HCF */
                    hcf_dma_rx_put( &( lp->hcfCtx ), desc );
                    return -EIO;
                }

                /* Read the HFS_STAT register from the lookahead buffer */
                hfs_stat = (hcf_16)( desc->buf_addr[HFS_STAT/2] );

                /* Make sure the frame isn't bad */
                if(( hfs_stat & HFS_STAT_ERR ) != HCF_SUCCESS )
                {
                    DBG_WARNING( DbgInfo, "HFS_STAT_ERROR (0x%x) in Rx Packet\n",
                                desc->buf_addr[HFS_STAT/2] );

                    /* Give the descriptor back to the HCF */
                    hcf_dma_rx_put( &( lp->hcfCtx ), desc );
                    return -EIO;
                }

                /* Determine what port this packet is for */
                port = ( hfs_stat >> 8 ) & 0x0007;
                DBG_RX( DbgInfo, "Rx frame for port %d\n", port );

                pktlen = GET_BUF_CNT(desc_next);
                if (pktlen != 0) {
                    skb = ALLOC_SKB(pktlen);
                    if (skb != NULL) {
                        switch( port ) {
#ifdef USE_WDS
                        case 1:
                        case 2:
                        case 3:
                        case 4:
                        case 5:
                        case 6:
                            skb->dev = lp->wds_port[port-1].dev;
                            break;
#endif  /* USE_WDS */

                        case 0:
                        default:
                            skb->dev = dev;
                            break;
                        }

                        GET_PACKET_DMA( skb->dev, skb, pktlen );

                        /* Give the descriptor back to the HCF */
                        hcf_dma_rx_put( &( lp->hcfCtx ), desc );

                        netif_rx( skb );

                        if( port == 0 ) {
                            lp->stats.rx_packets++;
                            lp->stats.rx_bytes += pktlen;
                        }
#ifdef USE_WDS
                        else
                        {
                            lp->wds_port[port-1].stats.rx_packets++;
                            lp->wds_port[port-1].stats.rx_bytes += pktlen;
                        }
#endif  /* USE_WDS */

                        dev->last_rx = jiffies;

                    } else {
                        DBG_ERROR( DbgInfo, "Could not alloc skb\n" );

                        if( port == 0 )
	                    {
	                        lp->stats.rx_dropped++;
	                    }
#ifdef USE_WDS
                        else
                        {
                            lp->wds_port[port-1].stats.rx_dropped++;
                        }
#endif  /* USE_WDS */
                    }
                }
            }
        //}
    }

    return 0;
} // wl_rx_dma
/*============================================================================*/
#endif  // ENABLE_DMA
