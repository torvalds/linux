/*
 * EXTLOG Module log ID to log Format String mapping table
 *
 * Copyright (C) 2012, Broadcom Corporation
 * All Rights Reserved.
 * 
 * This is UNPUBLISHED PROPRIETARY SOURCE CODE of Broadcom Corporation;
 * the contents of this file may not be disclosed to third parties, copied
 * or duplicated in any form, in whole or in part, without the prior
 * written permission of Broadcom Corporation.
 *
 * $Id: wlc_extlog_idstr.h 241182 2011-02-17 21:50:03Z $
 */
#ifndef _WLC_EXTLOG_IDSTR_H_
#define _WLC_EXTLOG_IDSTR_H_

#include "wlioctl.h"

/* Strings corresponding to the IDs defined in wlioctl.h
 * This file is only included by the apps and not included by the external driver
 * Formats of pre-existing ids should NOT be changed
 */
log_idstr_t extlog_fmt_str[ ] = {
	{FMTSTR_DRIVER_UP_ID, 0, LOG_ARGTYPE_NULL,
	"Driver is Up\n"},

	{FMTSTR_DRIVER_DOWN_ID, 0, LOG_ARGTYPE_NULL,
	"Driver is Down\n"},

	{FMTSTR_SUSPEND_MAC_FAIL_ID, 0, LOG_ARGTYPE_INT,
	"wlc_suspend_mac_and_wait() failed with psmdebug 0x%08x\n"},

	{FMTSTR_NO_PROGRESS_ID, 0, LOG_ARGTYPE_INT,
	"No Progress on TX for %d seconds\n"},

	{FMTSTR_RFDISABLE_ID, 0, LOG_ARGTYPE_INT,
	"Detected a change in RF Disable Input 0x%x\n"},

	{FMTSTR_REG_PRINT_ID, 0, LOG_ARGTYPE_STR_INT,
	"Register %s = 0x%x\n"},

	{FMTSTR_EXPTIME_ID, FMTSTRF_USER, LOG_ARGTYPE_NULL,
	"Strong RF interference detected\n"},

	{FMTSTR_JOIN_START_ID, FMTSTRF_USER, LOG_ARGTYPE_STR,
	"Searching for networks with ssid %s\n"},

	{FMTSTR_JOIN_COMPLETE_ID, FMTSTRF_USER, LOG_ARGTYPE_STR,
	"Successfully joined network with BSSID %s\n"},

	{FMTSTR_NO_NETWORKS_ID, FMTSTRF_USER, LOG_ARGTYPE_NULL,
	"No networks found. Please check if the network exists and is in range\n"},

	{FMTSTR_SECURITY_MISMATCH_ID, FMTSTRF_USER, LOG_ARGTYPE_NULL,
	"AP rejected due to security mismatch. Change the security settings and try again...\n"},

	{FMTSTR_RATE_MISMATCH_ID, FMTSTRF_USER, LOG_ARGTYPE_NULL,
	"AP rejected due to rate mismatch\n"},

	{FMTSTR_AP_PRUNED_ID, 0, LOG_ARGTYPE_INT,
	"AP rejected due to reason %d\n"},

	{FMTSTR_KEY_INSERTED_ID, 0, LOG_ARGTYPE_INT,
	"Inserting keys for algorithm %d\n"},

	{FMTSTR_DEAUTH_ID, FMTSTRF_USER, LOG_ARGTYPE_STR_INT,
	"Received Deauth from %s with Reason %d\n"},

	{FMTSTR_DISASSOC_ID, FMTSTRF_USER, LOG_ARGTYPE_STR_INT,
	"Received Disassoc from %s with Reason %d\n"},

	{FMTSTR_LINK_UP_ID, FMTSTRF_USER, LOG_ARGTYPE_NULL,
	"Link Up\n"},

	{FMTSTR_LINK_DOWN_ID, FMTSTRF_USER, LOG_ARGTYPE_NULL,
	"Link Down\n"},

	{FMTSTR_RADIO_HW_OFF_ID, FMTSTRF_USER, LOG_ARGTYPE_NULL,
	"Radio button is turned OFF. Please turn it on...\n"},

	{FMTSTR_RADIO_HW_ON_ID, FMTSTRF_USER, LOG_ARGTYPE_NULL,
	"Hardware Radio button is turned ON\n"},

	{FMTSTR_EVENT_DESC_ID, 0, LOG_ARGTYPE_INT_STR,
	"Generated event id %d: (result status) is (%s)\n"},

	{FMTSTR_PNP_SET_POWER_ID, 0, LOG_ARGTYPE_INT,
	"Device going into power state %d\n"},

	{FMTSTR_RADIO_SW_OFF_ID, FMTSTRF_USER, LOG_ARGTYPE_NULL,
	"Software Radio is disabled. Please enable it through the UI...\n"},

	{FMTSTR_RADIO_SW_ON_ID, FMTSTRF_USER, LOG_ARGTYPE_NULL,
	"Software Radio is enabled\n"},

	{FMTSTR_PWD_MISMATCH_ID, FMTSTRF_USER, LOG_ARGTYPE_NULL,
	"Potential passphrase mismatch. Please try a different one...\n"},

	{FMTSTR_FATAL_ERROR_ID, 0, LOG_ARGTYPE_INT,
	"Fatal Error: intstatus 0x%x\n"},

	{FMTSTR_AUTH_FAIL_ID, 0, LOG_ARGTYPE_STR_INT,
	"Authentication to %s Failed with status %d\n"},

	{FMTSTR_ASSOC_FAIL_ID, 0, LOG_ARGTYPE_STR_INT,
	"Association to %s Failed with status %d\n"},

	{FMTSTR_IBSS_FAIL_ID, FMTSTRF_USER, LOG_ARGTYPE_NULL,
	"Unable to start IBSS since PeerNet is already active\n"},

	{FMTSTR_EXTAP_FAIL_ID, FMTSTRF_USER, LOG_ARGTYPE_NULL,
	"Unable to start Ext-AP since PeerNet is already active\n"},

	{FMTSTR_MAX_ID, 0, 0, "\0"}
};

#endif /* _WLC_EXTLOG_IDSTR_H_ */
