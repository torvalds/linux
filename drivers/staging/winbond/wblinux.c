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
	if (atomic_inc_return(&adapter->ThreadCount) == 1) {
		// Shutdown module immediately
		adapter->shutdown = 1;
#ifdef _PE_STATE_DUMP_
		WBDEBUG(( "[w35und] SKB_RELEASE OK\n" ));
#endif
	}

	atomic_dec(&adapter->ThreadCount);
}
