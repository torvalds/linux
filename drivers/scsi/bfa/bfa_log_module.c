/*
 * Copyright (c) 2005-2009 Brocade Communications Systems, Inc.
 * All rights reserved
 * www.brocade.com
 *
 * Linux driver for Brocade Fibre Channel Host Bus Adapter.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License (GPL) Version 2 as
 * published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#include <cs/bfa_log.h>
#include <aen/bfa_aen_adapter.h>
#include <aen/bfa_aen_audit.h>
#include <aen/bfa_aen_ethport.h>
#include <aen/bfa_aen_ioc.h>
#include <aen/bfa_aen_itnim.h>
#include <aen/bfa_aen_lport.h>
#include <aen/bfa_aen_port.h>
#include <aen/bfa_aen_rport.h>
#include <log/bfa_log_fcs.h>
#include <log/bfa_log_hal.h>
#include <log/bfa_log_linux.h>
#include <log/bfa_log_wdrv.h>

struct bfa_log_msgdef_s bfa_log_msg_array[] = {


/* messages define for BFA_AEN_CAT_ADAPTER Module */
{BFA_AEN_ADAPTER_ADD, BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG, BFA_LOG_INFO,
 "BFA_AEN_ADAPTER_ADD",
 "New adapter found: SN = %s, base port WWN = %s.",
 ((BFA_LOG_S << BFA_LOG_ARG0) | (BFA_LOG_S << BFA_LOG_ARG1) | 0), 2},

{BFA_AEN_ADAPTER_REMOVE, BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG,
 BFA_LOG_WARNING, "BFA_AEN_ADAPTER_REMOVE",
 "Adapter removed: SN = %s.",
 ((BFA_LOG_S << BFA_LOG_ARG0) | 0), 1},




/* messages define for BFA_AEN_CAT_AUDIT Module */
{BFA_AEN_AUDIT_AUTH_ENABLE, BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG,
 BFA_LOG_INFO, "BFA_AEN_AUDIT_AUTH_ENABLE",
 "Authentication enabled for base port: WWN = %s.",
 ((BFA_LOG_S << BFA_LOG_ARG0) | 0), 1},

{BFA_AEN_AUDIT_AUTH_DISABLE, BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG,
 BFA_LOG_INFO, "BFA_AEN_AUDIT_AUTH_DISABLE",
 "Authentication disabled for base port: WWN = %s.",
 ((BFA_LOG_S << BFA_LOG_ARG0) | 0), 1},




/* messages define for BFA_AEN_CAT_ETHPORT Module */
{BFA_AEN_ETHPORT_LINKUP, BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG, BFA_LOG_INFO,
 "BFA_AEN_ETHPORT_LINKUP",
 "Base port ethernet linkup: mac = %s.",
 ((BFA_LOG_S << BFA_LOG_ARG0) | 0), 1},

{BFA_AEN_ETHPORT_LINKDOWN, BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG, BFA_LOG_INFO,
 "BFA_AEN_ETHPORT_LINKDOWN",
 "Base port ethernet linkdown: mac = %s.",
 ((BFA_LOG_S << BFA_LOG_ARG0) | 0), 1},

{BFA_AEN_ETHPORT_ENABLE, BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG, BFA_LOG_INFO,
 "BFA_AEN_ETHPORT_ENABLE",
 "Base port ethernet interface enabled: mac = %s.",
 ((BFA_LOG_S << BFA_LOG_ARG0) | 0), 1},

{BFA_AEN_ETHPORT_DISABLE, BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG, BFA_LOG_INFO,
 "BFA_AEN_ETHPORT_DISABLE",
 "Base port ethernet interface disabled: mac = %s.",
 ((BFA_LOG_S << BFA_LOG_ARG0) | 0), 1},




/* messages define for BFA_AEN_CAT_IOC Module */
{BFA_AEN_IOC_HBGOOD, BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG, BFA_LOG_INFO,
 "BFA_AEN_IOC_HBGOOD",
 "Heart Beat of IOC %d is good.",
 ((BFA_LOG_D << BFA_LOG_ARG0) | 0), 1},

{BFA_AEN_IOC_HBFAIL, BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG, BFA_LOG_CRITICAL,
 "BFA_AEN_IOC_HBFAIL",
 "Heart Beat of IOC %d has failed.",
 ((BFA_LOG_D << BFA_LOG_ARG0) | 0), 1},

{BFA_AEN_IOC_ENABLE, BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG, BFA_LOG_INFO,
 "BFA_AEN_IOC_ENABLE",
 "IOC %d is enabled.",
 ((BFA_LOG_D << BFA_LOG_ARG0) | 0), 1},

{BFA_AEN_IOC_DISABLE, BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG, BFA_LOG_INFO,
 "BFA_AEN_IOC_DISABLE",
 "IOC %d is disabled.",
 ((BFA_LOG_D << BFA_LOG_ARG0) | 0), 1},

{BFA_AEN_IOC_FWMISMATCH, BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG,
 BFA_LOG_CRITICAL, "BFA_AEN_IOC_FWMISMATCH",
 "Running firmware version is incompatible with the driver version.",
 (0), 0},

{BFA_AEN_IOC_FWCFG_ERROR, BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG,
 BFA_LOG_CRITICAL, "BFA_AEN_IOC_FWCFG_ERROR",
 "Link initialization failed due to firmware configuration read error:"
 " WWN = %s.",
 ((BFA_LOG_S << BFA_LOG_ARG0) | 0), 1},

{BFA_AEN_IOC_INVALID_VENDOR, BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG,
 BFA_LOG_ERROR, "BFA_AEN_IOC_INVALID_VENDOR",
 "Unsupported switch vendor. Link initialization failed: WWN = %s.",
 ((BFA_LOG_S << BFA_LOG_ARG0) | 0), 1},

{BFA_AEN_IOC_INVALID_NWWN, BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG,
 BFA_LOG_ERROR, "BFA_AEN_IOC_INVALID_NWWN",
 "Invalid NWWN. Link initialization failed: NWWN = 00:00:00:00:00:00:00:00.",
 (0), 0},

{BFA_AEN_IOC_INVALID_PWWN, BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG,
 BFA_LOG_ERROR, "BFA_AEN_IOC_INVALID_PWWN",
 "Invalid PWWN. Link initialization failed: PWWN = 00:00:00:00:00:00:00:00.",
 (0), 0},




/* messages define for BFA_AEN_CAT_ITNIM Module */
{BFA_AEN_ITNIM_ONLINE, BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG, BFA_LOG_INFO,
 "BFA_AEN_ITNIM_ONLINE",
 "Target (WWN = %s) is online for initiator (WWN = %s).",
 ((BFA_LOG_S << BFA_LOG_ARG0) | (BFA_LOG_S << BFA_LOG_ARG1) | 0), 2},

{BFA_AEN_ITNIM_OFFLINE, BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG, BFA_LOG_INFO,
 "BFA_AEN_ITNIM_OFFLINE",
 "Target (WWN = %s) offlined by initiator (WWN = %s).",
 ((BFA_LOG_S << BFA_LOG_ARG0) | (BFA_LOG_S << BFA_LOG_ARG1) | 0), 2},

{BFA_AEN_ITNIM_DISCONNECT, BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG,
 BFA_LOG_ERROR, "BFA_AEN_ITNIM_DISCONNECT",
 "Target (WWN = %s) connectivity lost for initiator (WWN = %s).",
 ((BFA_LOG_S << BFA_LOG_ARG0) | (BFA_LOG_S << BFA_LOG_ARG1) | 0), 2},




/* messages define for BFA_AEN_CAT_LPORT Module */
{BFA_AEN_LPORT_NEW, BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG, BFA_LOG_INFO,
 "BFA_AEN_LPORT_NEW",
 "New logical port created: WWN = %s, Role = %s.",
 ((BFA_LOG_S << BFA_LOG_ARG0) | (BFA_LOG_S << BFA_LOG_ARG1) | 0), 2},

{BFA_AEN_LPORT_DELETE, BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG, BFA_LOG_INFO,
 "BFA_AEN_LPORT_DELETE",
 "Logical port deleted: WWN = %s, Role = %s.",
 ((BFA_LOG_S << BFA_LOG_ARG0) | (BFA_LOG_S << BFA_LOG_ARG1) | 0), 2},

{BFA_AEN_LPORT_ONLINE, BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG, BFA_LOG_INFO,
 "BFA_AEN_LPORT_ONLINE",
 "Logical port online: WWN = %s, Role = %s.",
 ((BFA_LOG_S << BFA_LOG_ARG0) | (BFA_LOG_S << BFA_LOG_ARG1) | 0), 2},

{BFA_AEN_LPORT_OFFLINE, BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG, BFA_LOG_INFO,
 "BFA_AEN_LPORT_OFFLINE",
 "Logical port taken offline: WWN = %s, Role = %s.",
 ((BFA_LOG_S << BFA_LOG_ARG0) | (BFA_LOG_S << BFA_LOG_ARG1) | 0), 2},

{BFA_AEN_LPORT_DISCONNECT, BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG,
 BFA_LOG_ERROR, "BFA_AEN_LPORT_DISCONNECT",
 "Logical port lost fabric connectivity: WWN = %s, Role = %s.",
 ((BFA_LOG_S << BFA_LOG_ARG0) | (BFA_LOG_S << BFA_LOG_ARG1) | 0), 2},

{BFA_AEN_LPORT_NEW_PROP, BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG, BFA_LOG_INFO,
 "BFA_AEN_LPORT_NEW_PROP",
 "New virtual port created using proprietary interface: WWN = %s, Role = %s.",
 ((BFA_LOG_S << BFA_LOG_ARG0) | (BFA_LOG_S << BFA_LOG_ARG1) | 0), 2},

{BFA_AEN_LPORT_DELETE_PROP, BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG,
 BFA_LOG_INFO, "BFA_AEN_LPORT_DELETE_PROP",
 "Virtual port deleted using proprietary interface: WWN = %s, Role = %s.",
 ((BFA_LOG_S << BFA_LOG_ARG0) | (BFA_LOG_S << BFA_LOG_ARG1) | 0), 2},

{BFA_AEN_LPORT_NEW_STANDARD, BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG,
 BFA_LOG_INFO, "BFA_AEN_LPORT_NEW_STANDARD",
 "New virtual port created using standard interface: WWN = %s, Role = %s.",
 ((BFA_LOG_S << BFA_LOG_ARG0) | (BFA_LOG_S << BFA_LOG_ARG1) | 0), 2},

{BFA_AEN_LPORT_DELETE_STANDARD, BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG,
 BFA_LOG_INFO, "BFA_AEN_LPORT_DELETE_STANDARD",
 "Virtual port deleted using standard interface: WWN = %s, Role = %s.",
 ((BFA_LOG_S << BFA_LOG_ARG0) | (BFA_LOG_S << BFA_LOG_ARG1) | 0), 2},

{BFA_AEN_LPORT_NPIV_DUP_WWN, BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG,
 BFA_LOG_WARNING, "BFA_AEN_LPORT_NPIV_DUP_WWN",
 "Virtual port login failed. Duplicate WWN = %s reported by fabric.",
 ((BFA_LOG_S << BFA_LOG_ARG0) | 0), 1},

{BFA_AEN_LPORT_NPIV_FABRIC_MAX, BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG,
 BFA_LOG_WARNING, "BFA_AEN_LPORT_NPIV_FABRIC_MAX",
 "Virtual port (WWN = %s) login failed. Max NPIV ports already exist in"
 " fabric/fport.",
 ((BFA_LOG_S << BFA_LOG_ARG0) | 0), 1},

{BFA_AEN_LPORT_NPIV_UNKNOWN, BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG,
 BFA_LOG_WARNING, "BFA_AEN_LPORT_NPIV_UNKNOWN",
 "Virtual port (WWN = %s) login failed.",
 ((BFA_LOG_S << BFA_LOG_ARG0) | 0), 1},




/* messages define for BFA_AEN_CAT_PORT Module */
{BFA_AEN_PORT_ONLINE, BFA_LOG_ATTR_NONE, BFA_LOG_INFO, "BFA_AEN_PORT_ONLINE",
 "Base port online: WWN = %s.",
 ((BFA_LOG_S << BFA_LOG_ARG0) | 0), 1},

{BFA_AEN_PORT_OFFLINE, BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG, BFA_LOG_WARNING,
 "BFA_AEN_PORT_OFFLINE",
 "Base port offline: WWN = %s.",
 ((BFA_LOG_S << BFA_LOG_ARG0) | 0), 1},

{BFA_AEN_PORT_RLIR, BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG, BFA_LOG_INFO,
 "BFA_AEN_PORT_RLIR",
 "RLIR event not supported.",
 (0), 0},

{BFA_AEN_PORT_SFP_INSERT, BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG, BFA_LOG_INFO,
 "BFA_AEN_PORT_SFP_INSERT",
 "New SFP found: WWN/MAC = %s.",
 ((BFA_LOG_S << BFA_LOG_ARG0) | 0), 1},

{BFA_AEN_PORT_SFP_REMOVE, BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG,
 BFA_LOG_WARNING, "BFA_AEN_PORT_SFP_REMOVE",
 "SFP removed: WWN/MAC = %s.",
 ((BFA_LOG_S << BFA_LOG_ARG0) | 0), 1},

{BFA_AEN_PORT_SFP_POM, BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG, BFA_LOG_WARNING,
 "BFA_AEN_PORT_SFP_POM",
 "SFP POM level to %s: WWN/MAC = %s.",
 ((BFA_LOG_S << BFA_LOG_ARG0) | (BFA_LOG_S << BFA_LOG_ARG1) | 0), 2},

{BFA_AEN_PORT_ENABLE, BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG, BFA_LOG_INFO,
 "BFA_AEN_PORT_ENABLE",
 "Base port enabled: WWN = %s.",
 ((BFA_LOG_S << BFA_LOG_ARG0) | 0), 1},

{BFA_AEN_PORT_DISABLE, BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG, BFA_LOG_INFO,
 "BFA_AEN_PORT_DISABLE",
 "Base port disabled: WWN = %s.",
 ((BFA_LOG_S << BFA_LOG_ARG0) | 0), 1},

{BFA_AEN_PORT_AUTH_ON, BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG, BFA_LOG_INFO,
 "BFA_AEN_PORT_AUTH_ON",
 "Authentication successful for base port: WWN = %s.",
 ((BFA_LOG_S << BFA_LOG_ARG0) | 0), 1},

{BFA_AEN_PORT_AUTH_OFF, BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG, BFA_LOG_ERROR,
 "BFA_AEN_PORT_AUTH_OFF",
 "Authentication unsuccessful for base port: WWN = %s.",
 ((BFA_LOG_S << BFA_LOG_ARG0) | 0), 1},

{BFA_AEN_PORT_DISCONNECT, BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG, BFA_LOG_ERROR,
 "BFA_AEN_PORT_DISCONNECT",
 "Base port (WWN = %s) lost fabric connectivity.",
 ((BFA_LOG_S << BFA_LOG_ARG0) | 0), 1},

{BFA_AEN_PORT_QOS_NEG, BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG, BFA_LOG_WARNING,
 "BFA_AEN_PORT_QOS_NEG",
 "QOS negotiation failed for base port: WWN = %s.",
 ((BFA_LOG_S << BFA_LOG_ARG0) | 0), 1},

{BFA_AEN_PORT_FABRIC_NAME_CHANGE, BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG,
 BFA_LOG_WARNING, "BFA_AEN_PORT_FABRIC_NAME_CHANGE",
 "Base port WWN = %s, Fabric WWN = %s.",
 ((BFA_LOG_S << BFA_LOG_ARG0) | (BFA_LOG_S << BFA_LOG_ARG1) | 0), 2},

{BFA_AEN_PORT_SFP_ACCESS_ERROR, BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG,
 BFA_LOG_WARNING, "BFA_AEN_PORT_SFP_ACCESS_ERROR",
 "SFP access error: WWN/MAC = %s.",
 ((BFA_LOG_S << BFA_LOG_ARG0) | 0), 1},

{BFA_AEN_PORT_SFP_UNSUPPORT, BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG,
 BFA_LOG_WARNING, "BFA_AEN_PORT_SFP_UNSUPPORT",
 "Unsupported SFP found: WWN/MAC = %s.",
 ((BFA_LOG_S << BFA_LOG_ARG0) | 0), 1},




/* messages define for BFA_AEN_CAT_RPORT Module */
{BFA_AEN_RPORT_ONLINE, BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG, BFA_LOG_INFO,
 "BFA_AEN_RPORT_ONLINE",
 "Remote port (WWN = %s) online for logical port (WWN = %s).",
 ((BFA_LOG_S << BFA_LOG_ARG0) | (BFA_LOG_S << BFA_LOG_ARG1) | 0), 2},

{BFA_AEN_RPORT_OFFLINE, BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG, BFA_LOG_INFO,
 "BFA_AEN_RPORT_OFFLINE",
 "Remote port (WWN = %s) offlined by logical port (WWN = %s).",
 ((BFA_LOG_S << BFA_LOG_ARG0) | (BFA_LOG_S << BFA_LOG_ARG1) | 0), 2},

{BFA_AEN_RPORT_DISCONNECT, BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG,
 BFA_LOG_ERROR, "BFA_AEN_RPORT_DISCONNECT",
 "Remote port (WWN = %s) connectivity lost for logical port (WWN = %s).",
 ((BFA_LOG_S << BFA_LOG_ARG0) | (BFA_LOG_S << BFA_LOG_ARG1) | 0), 2},

{BFA_AEN_RPORT_QOS_PRIO, BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG, BFA_LOG_INFO,
 "BFA_AEN_RPORT_QOS_PRIO",
 "QOS priority changed to %s: RPWWN = %s and LPWWN = %s.",
 ((BFA_LOG_S << BFA_LOG_ARG0) | (BFA_LOG_S << BFA_LOG_ARG1) |
  (BFA_LOG_S << BFA_LOG_ARG2) | 0), 3},

{BFA_AEN_RPORT_QOS_FLOWID, BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG, BFA_LOG_INFO,
 "BFA_AEN_RPORT_QOS_FLOWID",
 "QOS flow ID changed to %d: RPWWN = %s and LPWWN = %s.",
 ((BFA_LOG_D << BFA_LOG_ARG0) | (BFA_LOG_S << BFA_LOG_ARG1) |
  (BFA_LOG_S << BFA_LOG_ARG2) | 0), 3},




/* messages define for FCS Module */
{BFA_LOG_FCS_FABRIC_NOSWITCH, BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG,
 BFA_LOG_INFO, "FCS_FABRIC_NOSWITCH",
 "No switched fabric presence is detected.",
 (0), 0},

{BFA_LOG_FCS_FABRIC_ISOLATED, BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG,
 BFA_LOG_INFO, "FCS_FABRIC_ISOLATED",
 "Port is isolated due to VF_ID mismatch. PWWN: %s, Port VF_ID: %04x and"
 " switch port VF_ID: %04x.",
 ((BFA_LOG_S << BFA_LOG_ARG0) | (BFA_LOG_X << BFA_LOG_ARG1) |
  (BFA_LOG_X << BFA_LOG_ARG2) | 0), 3},




/* messages define for HAL Module */
{BFA_LOG_HAL_ASSERT, BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG, BFA_LOG_ERROR,
 "HAL_ASSERT",
 "Assertion failure: %s:%d: %s",
 ((BFA_LOG_S << BFA_LOG_ARG0) | (BFA_LOG_D << BFA_LOG_ARG1) |
  (BFA_LOG_S << BFA_LOG_ARG2) | 0), 3},

{BFA_LOG_HAL_HEARTBEAT_FAILURE, BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG,
 BFA_LOG_CRITICAL, "HAL_HEARTBEAT_FAILURE",
 "Firmware heartbeat failure at %d",
 ((BFA_LOG_D << BFA_LOG_ARG0) | 0), 1},

{BFA_LOG_HAL_FCPIM_PARM_INVALID, BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG,
 BFA_LOG_INFO, "HAL_FCPIM_PARM_INVALID",
 "Driver configuration %s value %d is invalid. Value should be within"
 " %d and %d.",
 ((BFA_LOG_S << BFA_LOG_ARG0) | (BFA_LOG_D << BFA_LOG_ARG1) |
  (BFA_LOG_D << BFA_LOG_ARG2) | (BFA_LOG_D << BFA_LOG_ARG3) | 0), 4},

{BFA_LOG_HAL_SM_ASSERT, BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG, BFA_LOG_ERROR,
 "HAL_SM_ASSERT",
 "SM Assertion failure: %s:%d: event = %d",
 ((BFA_LOG_S << BFA_LOG_ARG0) | (BFA_LOG_D << BFA_LOG_ARG1) |
  (BFA_LOG_D << BFA_LOG_ARG2) | 0), 3},

{BFA_LOG_HAL_DRIVER_ERROR, BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG,
 BFA_LOG_INFO, "HAL_DRIVER_ERROR",
 "%s",
 ((BFA_LOG_S << BFA_LOG_ARG0) | 0), 1},

{BFA_LOG_HAL_DRIVER_CONFIG_ERROR,
 BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG, BFA_LOG_INFO,
 "HAL_DRIVER_CONFIG_ERROR",
 "%s",
 ((BFA_LOG_S << BFA_LOG_ARG0) | 0), 1},

{BFA_LOG_HAL_MBOX_ERROR, BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG,
 BFA_LOG_INFO, "HAL_MBOX_ERROR",
 "%s",
 ((BFA_LOG_S << BFA_LOG_ARG0) | 0), 1},




/* messages define for LINUX Module */
{BFA_LOG_LINUX_DEVICE_CLAIMED, BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG,
 BFA_LOG_INFO, "LINUX_DEVICE_CLAIMED",
 "bfa device at %s claimed.",
 ((BFA_LOG_S << BFA_LOG_ARG0) | 0), 1},

{BFA_LOG_LINUX_HASH_INIT_FAILED, BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG,
 BFA_LOG_INFO, "LINUX_HASH_INIT_FAILED",
 "Hash table initialization failure for the port %s.",
 ((BFA_LOG_S << BFA_LOG_ARG0) | 0), 1},

{BFA_LOG_LINUX_SYSFS_FAILED, BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG,
 BFA_LOG_INFO, "LINUX_SYSFS_FAILED",
 "sysfs file creation failure for the port %s.",
 ((BFA_LOG_S << BFA_LOG_ARG0) | 0), 1},

{BFA_LOG_LINUX_MEM_ALLOC_FAILED, BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG,
 BFA_LOG_INFO, "LINUX_MEM_ALLOC_FAILED",
 "Memory allocation failed: %s.  ",
 ((BFA_LOG_S << BFA_LOG_ARG0) | 0), 1},

{BFA_LOG_LINUX_DRIVER_REGISTRATION_FAILED,
 BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG, BFA_LOG_INFO,
 "LINUX_DRIVER_REGISTRATION_FAILED",
 "%s.  ",
 ((BFA_LOG_S << BFA_LOG_ARG0) | 0), 1},

{BFA_LOG_LINUX_ITNIM_FREE, BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG, BFA_LOG_INFO,
 "LINUX_ITNIM_FREE",
 "scsi%d: FCID: %s WWPN: %s",
 ((BFA_LOG_D << BFA_LOG_ARG0) | (BFA_LOG_S << BFA_LOG_ARG1) |
  (BFA_LOG_S << BFA_LOG_ARG2) | 0), 3},

{BFA_LOG_LINUX_ITNIM_ONLINE, BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG,
 BFA_LOG_INFO, "LINUX_ITNIM_ONLINE",
 "Target: %d:0:%d FCID: %s WWPN: %s",
 ((BFA_LOG_D << BFA_LOG_ARG0) | (BFA_LOG_D << BFA_LOG_ARG1) |
  (BFA_LOG_S << BFA_LOG_ARG2) | (BFA_LOG_S << BFA_LOG_ARG3) | 0), 4},

{BFA_LOG_LINUX_ITNIM_OFFLINE, BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG,
 BFA_LOG_INFO, "LINUX_ITNIM_OFFLINE",
 "Target: %d:0:%d FCID: %s WWPN: %s",
 ((BFA_LOG_D << BFA_LOG_ARG0) | (BFA_LOG_D << BFA_LOG_ARG1) |
  (BFA_LOG_S << BFA_LOG_ARG2) | (BFA_LOG_S << BFA_LOG_ARG3) | 0), 4},

{BFA_LOG_LINUX_SCSI_HOST_FREE, BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG,
 BFA_LOG_INFO, "LINUX_SCSI_HOST_FREE",
 "Free scsi%d",
 ((BFA_LOG_D << BFA_LOG_ARG0) | 0), 1},

{BFA_LOG_LINUX_SCSI_ABORT, BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG, BFA_LOG_INFO,
 "LINUX_SCSI_ABORT",
 "scsi%d: abort cmnd %p, iotag %x",
 ((BFA_LOG_D << BFA_LOG_ARG0) | (BFA_LOG_P << BFA_LOG_ARG1) |
  (BFA_LOG_X << BFA_LOG_ARG2) | 0), 3},

{BFA_LOG_LINUX_SCSI_ABORT_COMP, BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG,
 BFA_LOG_INFO, "LINUX_SCSI_ABORT_COMP",
 "scsi%d: complete abort 0x%p, iotag 0x%x",
 ((BFA_LOG_D << BFA_LOG_ARG0) | (BFA_LOG_P << BFA_LOG_ARG1) |
  (BFA_LOG_X << BFA_LOG_ARG2) | 0), 3},

{BFA_LOG_LINUX_DRIVER_CONFIG_ERROR,
 BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG, BFA_LOG_INFO,
 "LINUX_DRIVER_CONFIG_ERROR",
 "%s",
 ((BFA_LOG_S << BFA_LOG_ARG0) | 0), 1},

{BFA_LOG_LINUX_BNA_STATE_MACHINE,
 BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG, BFA_LOG_INFO,
 "LINUX_BNA_STATE_MACHINE",
 "%s",
 ((BFA_LOG_S << BFA_LOG_ARG0) | 0), 1},

{BFA_LOG_LINUX_IOC_ERROR, BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG,
 BFA_LOG_INFO, "LINUX_IOC_ERROR",
 "%s",
 ((BFA_LOG_S << BFA_LOG_ARG0) | 0), 1},

{BFA_LOG_LINUX_RESOURCE_ALLOC_ERROR,
 BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG, BFA_LOG_INFO,
 "LINUX_RESOURCE_ALLOC_ERROR",
 "%s",
 ((BFA_LOG_S << BFA_LOG_ARG0) | 0), 1},

{BFA_LOG_LINUX_RING_BUFFER_ERROR,
 BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG, BFA_LOG_INFO,
 "LINUX_RING_BUFFER_ERROR",
 "%s",
 ((BFA_LOG_S << BFA_LOG_ARG0) | 0), 1},

{BFA_LOG_LINUX_DRIVER_ERROR, BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG,
 BFA_LOG_ERROR, "LINUX_DRIVER_ERROR",
 "%s",
 ((BFA_LOG_S << BFA_LOG_ARG0) | 0), 1},

{BFA_LOG_LINUX_DRIVER_INFO, BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG,
 BFA_LOG_INFO, "LINUX_DRIVER_INFO",
 "%s",
 ((BFA_LOG_S << BFA_LOG_ARG0) | 0), 1},

{BFA_LOG_LINUX_DRIVER_DIAG, BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG,
 BFA_LOG_INFO, "LINUX_DRIVER_DIAG",
 "%s",
 ((BFA_LOG_S << BFA_LOG_ARG0) | 0), 1},

{BFA_LOG_LINUX_DRIVER_AEN, BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG,
 BFA_LOG_INFO, "LINUX_DRIVER_AEN",
 "%s",
 ((BFA_LOG_S << BFA_LOG_ARG0) | 0), 1},




/* messages define for WDRV Module */
{BFA_LOG_WDRV_IOC_INIT_ERROR, BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG,
 BFA_LOG_INFO, "WDRV_IOC_INIT_ERROR",
 "IOC initialization has failed.",
 (0), 0},

{BFA_LOG_WDRV_IOC_INTERNAL_ERROR, BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG,
 BFA_LOG_INFO, "WDRV_IOC_INTERNAL_ERROR",
 "IOC internal error.  ",
 (0), 0},

{BFA_LOG_WDRV_IOC_START_ERROR, BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG,
 BFA_LOG_INFO, "WDRV_IOC_START_ERROR",
 "IOC could not be started.  ",
 (0), 0},

{BFA_LOG_WDRV_IOC_STOP_ERROR, BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG,
 BFA_LOG_INFO, "WDRV_IOC_STOP_ERROR",
 "IOC could not be stopped.  ",
 (0), 0},

{BFA_LOG_WDRV_INSUFFICIENT_RESOURCES, BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG,
 BFA_LOG_INFO, "WDRV_INSUFFICIENT_RESOURCES",
 "Insufficient memory.  ",
 (0), 0},

{BFA_LOG_WDRV_BASE_ADDRESS_MAP_ERROR, BFA_LOG_ATTR_NONE | BFA_LOG_ATTR_LOG,
 BFA_LOG_INFO, "WDRV_BASE_ADDRESS_MAP_ERROR",
 "Unable to map the IOC onto the system address space.  ",
 (0), 0},


{0, 0, 0, "", "", 0, 0},
};
