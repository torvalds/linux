//============================================================================
//  Copyright (c) 1996-2005 Winbond Electronic Corporation
//
//  Module Name:
//    wblinux.c
//
//  Abstract:
//    Linux releated routines
//
//============================================================================
#include <linux/netdevice.h>

#include "mds_f.h"
#include "mto_f.h"
#include "os_common.h"
#include "wbhal_f.h"
#include "wblinux_f.h"

void
WBLINUX_stop(  struct wbsoft_priv * adapter )
{
	struct sk_buff *pSkb;

	if (atomic_inc_return(&adapter->ThreadCount) == 1) {
		// Shutdown module immediately
		adapter->shutdown = 1;

		while (adapter->skb_array[ adapter->skb_GetIndex ]) {
			// Trying to free the un-sending packet
			pSkb = adapter->skb_array[ adapter->skb_GetIndex ];
			adapter->skb_array[ adapter->skb_GetIndex ] = NULL;
			if( in_irq() )
				dev_kfree_skb_irq( pSkb );
			else
				dev_kfree_skb( pSkb );

			adapter->skb_GetIndex++;
			adapter->skb_GetIndex %= WBLINUX_PACKET_ARRAY_SIZE;
		}

#ifdef _PE_STATE_DUMP_
		WBDEBUG(( "[w35und] SKB_RELEASE OK\n" ));
#endif
	}

	atomic_dec(&adapter->ThreadCount);
}
