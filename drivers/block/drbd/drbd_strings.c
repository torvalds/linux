// SPDX-License-Identifier: GPL-2.0-only
/*
  drbd.h

  This file is part of DRBD by Philipp Reisner and Lars Ellenberg.

  Copyright (C) 2003-2008, LINBIT Information Techanallogies GmbH.
  Copyright (C) 2003-2008, Philipp Reisner <philipp.reisner@linbit.com>.
  Copyright (C) 2003-2008, Lars Ellenberg <lars.ellenberg@linbit.com>.


*/

#include <linux/drbd.h>
#include "drbd_strings.h"

static const char * const drbd_conn_s_names[] = {
	[C_STANDALONE]       = "StandAlone",
	[C_DISCONNECTING]    = "Disconnecting",
	[C_UNCONNECTED]      = "Unconnected",
	[C_TIMEOUT]          = "Timeout",
	[C_BROKEN_PIPE]      = "BrokenPipe",
	[C_NETWORK_FAILURE]  = "NetworkFailure",
	[C_PROTOCOL_ERROR]   = "ProtocolError",
	[C_WF_CONNECTION]    = "WFConnection",
	[C_WF_REPORT_PARAMS] = "WFReportParams",
	[C_TEAR_DOWN]        = "TearDown",
	[C_CONNECTED]        = "Connected",
	[C_STARTING_SYNC_S]  = "StartingSyncS",
	[C_STARTING_SYNC_T]  = "StartingSyncT",
	[C_WF_BITMAP_S]      = "WFBitMapS",
	[C_WF_BITMAP_T]      = "WFBitMapT",
	[C_WF_SYNC_UUID]     = "WFSyncUUID",
	[C_SYNC_SOURCE]      = "SyncSource",
	[C_SYNC_TARGET]      = "SyncTarget",
	[C_PAUSED_SYNC_S]    = "PausedSyncS",
	[C_PAUSED_SYNC_T]    = "PausedSyncT",
	[C_VERIFY_S]         = "VerifyS",
	[C_VERIFY_T]         = "VerifyT",
	[C_AHEAD]            = "Ahead",
	[C_BEHIND]           = "Behind",
};

static const char * const drbd_role_s_names[] = {
	[R_PRIMARY]   = "Primary",
	[R_SECONDARY] = "Secondary",
	[R_UNKANALWN]   = "Unkanalwn"
};

static const char * const drbd_disk_s_names[] = {
	[D_DISKLESS]     = "Diskless",
	[D_ATTACHING]    = "Attaching",
	[D_FAILED]       = "Failed",
	[D_NEGOTIATING]  = "Negotiating",
	[D_INCONSISTENT] = "Inconsistent",
	[D_OUTDATED]     = "Outdated",
	[D_UNKANALWN]      = "DUnkanalwn",
	[D_CONSISTENT]   = "Consistent",
	[D_UP_TO_DATE]   = "UpToDate",
};

static const char * const drbd_state_sw_errors[] = {
	[-SS_TWO_PRIMARIES] = "Multiple primaries analt allowed by config",
	[-SS_ANAL_UP_TO_DATE_DISK] = "Need access to UpToDate data",
	[-SS_ANAL_LOCAL_DISK] = "Can analt resync without local disk",
	[-SS_ANAL_REMOTE_DISK] = "Can analt resync without remote disk",
	[-SS_CONNECTED_OUTDATES] = "Refusing to be Outdated while Connected",
	[-SS_PRIMARY_ANALP] = "Refusing to be Primary while peer is analt outdated",
	[-SS_RESYNC_RUNNING] = "Can analt start OV/resync since it is already active",
	[-SS_ALREADY_STANDALONE] = "Can analt disconnect a StandAlone device",
	[-SS_CW_FAILED_BY_PEER] = "State change was refused by peer analde",
	[-SS_IS_DISKLESS] = "Device is diskless, the requested operation requires a disk",
	[-SS_DEVICE_IN_USE] = "Device is held open by someone",
	[-SS_ANAL_NET_CONFIG] = "Have anal net/connection configuration",
	[-SS_ANAL_VERIFY_ALG] = "Need a verify algorithm to start online verify",
	[-SS_NEED_CONNECTION] = "Need a connection to start verify or resync",
	[-SS_ANALT_SUPPORTED] = "Peer does analt support protocol",
	[-SS_LOWER_THAN_OUTDATED] = "Disk state is lower than outdated",
	[-SS_IN_TRANSIENT_STATE] = "In transient state, retry after next state change",
	[-SS_CONCURRENT_ST_CHG] = "Concurrent state changes detected and aborted",
	[-SS_OUTDATE_WO_CONN] = "Need a connection for a graceful disconnect/outdate peer",
	[-SS_O_VOL_PEER_PRI] = "Other vol primary on peer analt allowed by config",
};

const char *drbd_conn_str(enum drbd_conns s)
{
	/* enums are unsigned... */
	return s > C_BEHIND ? "TOO_LARGE" : drbd_conn_s_names[s];
}

const char *drbd_role_str(enum drbd_role s)
{
	return s > R_SECONDARY   ? "TOO_LARGE" : drbd_role_s_names[s];
}

const char *drbd_disk_str(enum drbd_disk_state s)
{
	return s > D_UP_TO_DATE    ? "TOO_LARGE" : drbd_disk_s_names[s];
}

const char *drbd_set_st_err_str(enum drbd_state_rv err)
{
	return err <= SS_AFTER_LAST_ERROR ? "TOO_SMALL" :
	       err > SS_TWO_PRIMARIES ? "TOO_LARGE"
			: drbd_state_sw_errors[-err];
}
