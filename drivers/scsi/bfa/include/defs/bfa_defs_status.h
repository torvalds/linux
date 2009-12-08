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
#ifndef __BFA_DEFS_STATUS_H__
#define __BFA_DEFS_STATUS_H__

/**
 * API status return values
 *
 * NOTE: The error msgs are auto generated from the comments. Only singe line
 * comments are supported
 */
enum bfa_status {
	BFA_STATUS_OK 		= 0,	/*  Success */
	BFA_STATUS_FAILED 	= 1,	/*  Operation failed */
	BFA_STATUS_EINVAL 	= 2,	/*  Invalid params Check input
					 * parameters */
	BFA_STATUS_ENOMEM 	= 3,	/*  Out of resources */
	BFA_STATUS_ENOSYS 	= 4,	/*  Function not implemented */
	BFA_STATUS_ETIMER 	= 5,	/*  Timer expired - Retry, if
					 * persists, contact support */
	BFA_STATUS_EPROTOCOL 	= 6,	/*  Protocol error */
	BFA_STATUS_ENOFCPORTS 	= 7,	/*  No FC ports resources */
	BFA_STATUS_NOFLASH 	= 8,	/*  Flash not present */
	BFA_STATUS_BADFLASH 	= 9,	/*  Flash is corrupted or bad */
	BFA_STATUS_SFP_UNSUPP 	= 10,	/*  Unsupported SFP - Replace SFP */
	BFA_STATUS_UNKNOWN_VFID = 11,	/*  VF_ID not found */
	BFA_STATUS_DATACORRUPTED = 12,	/*  Diag returned data corrupted
					 * contact support */
	BFA_STATUS_DEVBUSY 	= 13,	/*  Device busy - Retry operation */
	BFA_STATUS_ABORTED 	= 14,	/*  Operation aborted */
	BFA_STATUS_NODEV 	= 15,	/*  Dev is not present */
	BFA_STATUS_HDMA_FAILED 	= 16,	/*  Host dma failed contact support */
	BFA_STATUS_FLASH_BAD_LEN = 17,	/*  Flash bad length */
	BFA_STATUS_UNKNOWN_LWWN = 18,	/*  LPORT PWWN not found */
	BFA_STATUS_UNKNOWN_RWWN = 19,	/*  RPORT PWWN not found */
	BFA_STATUS_FCPT_LS_RJT 	= 20,	/*  Got LS_RJT for FC Pass
					 * through Req */
	BFA_STATUS_VPORT_EXISTS = 21,	/*  VPORT already exists */
	BFA_STATUS_VPORT_MAX 	= 22,	/*  Reached max VPORT supported
					 * limit */
	BFA_STATUS_UNSUPP_SPEED = 23,	/*  Invalid Speed Check speed
					 * setting */
	BFA_STATUS_INVLD_DFSZ 	= 24,	/*  Invalid Max data field size */
	BFA_STATUS_CNFG_FAILED 	= 25,	/*  Setting can not be persisted */
	BFA_STATUS_CMD_NOTSUPP 	= 26,	/*  Command/API not supported */
	BFA_STATUS_NO_ADAPTER 	= 27,	/*  No Brocade Adapter Found */
	BFA_STATUS_LINKDOWN 	= 28,	/*  Link is down - Check or replace
					 * SFP/cable */
	BFA_STATUS_FABRIC_RJT 	= 29,	/*  Reject from attached fabric */
	BFA_STATUS_UNKNOWN_VWWN = 30,	/*  VPORT PWWN not found */
	BFA_STATUS_NSLOGIN_FAILED = 31,	/*  Nameserver login failed */
	BFA_STATUS_NO_RPORTS 	= 32,	/*  No remote ports found */
	BFA_STATUS_NSQUERY_FAILED = 33,	/*  Nameserver query failed */
	BFA_STATUS_PORT_OFFLINE = 34,	/*  Port is not online */
	BFA_STATUS_RPORT_OFFLINE = 35,	/*  RPORT is not online */
	BFA_STATUS_TGTOPEN_FAILED = 36,	/*  Remote SCSI target open failed */
	BFA_STATUS_BAD_LUNS 	= 37,	/*  No valid LUNs found */
	BFA_STATUS_IO_FAILURE 	= 38,	/*  SCSI target IO failure */
	BFA_STATUS_NO_FABRIC 	= 39,	/*  No switched fabric present */
	BFA_STATUS_EBADF 	= 40,	/*  Bad file descriptor */
	BFA_STATUS_EINTR 	= 41,	/*  A signal was caught during ioctl */
	BFA_STATUS_EIO 		= 42,	/*  I/O error */
	BFA_STATUS_ENOTTY 	= 43,	/*  Inappropriate I/O control
					 * operation */
	BFA_STATUS_ENXIO 	= 44,	/*  No such device or address */
	BFA_STATUS_EFOPEN 	= 45,	/*  Failed to open file */
	BFA_STATUS_VPORT_WWN_BP = 46,	/*  WWN is same as base port's WWN */
	BFA_STATUS_PORT_NOT_DISABLED = 47, /*  Port not disabled disable port
					    * first */
	BFA_STATUS_BADFRMHDR 	= 48,	/*  Bad frame header */
	BFA_STATUS_BADFRMSZ 	= 49,	/*  Bad frame size check and replace
					 * SFP/cable */
	BFA_STATUS_MISSINGFRM 	= 50,	/*  Missing frame check and replace
					 * SFP/cable */
	BFA_STATUS_LINKTIMEOUT 	= 51,	/*  Link timeout check and replace
					 * SFP/cable */
	BFA_STATUS_NO_FCPIM_NEXUS = 52,	/*  No FCP Nexus exists with the
					 * rport */
	BFA_STATUS_CHECKSUM_FAIL = 53,	/*  checksum failure */
	BFA_STATUS_GZME_FAILED 	= 54,	/*  Get zone member query failed */
	BFA_STATUS_SCSISTART_REQD = 55,	/*  SCSI disk require START command */
	BFA_STATUS_IOC_FAILURE 	= 56,	/*  IOC failure - Retry, if persists
					 * contact support */
	BFA_STATUS_INVALID_WWN 	= 57,	/*  Invalid WWN */
	BFA_STATUS_MISMATCH 	= 58,	/*  Version mismatch */
	BFA_STATUS_IOC_ENABLED 	= 59,	/*  IOC is already enabled */
	BFA_STATUS_ADAPTER_ENABLED = 60, /*  Adapter is not disabled disable
					  * adapter first */
	BFA_STATUS_IOC_NON_OP 	= 61,	/*  IOC is not operational. Enable IOC
					 * and if it still fails,
					 * contact support */
	BFA_STATUS_ADDR_MAP_FAILURE = 62, /*  PCI base address not mapped
					   * in OS */
	BFA_STATUS_SAME_NAME 	= 63,	/*  Name exists! use a different
					 * name */
	BFA_STATUS_PENDING      = 64,   /*  API completes asynchronously */
	BFA_STATUS_8G_SPD	= 65,	/*  Speed setting not valid for
					 * 8G HBA */
	BFA_STATUS_4G_SPD	= 66,	/*  Speed setting not valid for
					 * 4G HBA */
	BFA_STATUS_AD_IS_ENABLE = 67,	/*  Adapter is already enabled */
	BFA_STATUS_EINVAL_TOV 	= 68,	/*  Invalid path failover TOV */
	BFA_STATUS_EINVAL_QDEPTH = 69,	/*  Invalid queue depth value */
	BFA_STATUS_VERSION_FAIL = 70,	/*  Application/Driver version
					 * mismatch */
	BFA_STATUS_DIAG_BUSY    = 71,	/*  diag busy */
	BFA_STATUS_BEACON_ON	= 72,	/*  Port Beacon already on */
	BFA_STATUS_BEACON_OFF	= 73,	/*  Port Beacon already off */
	BFA_STATUS_LBEACON_ON   = 74,	/*  Link End-to-End Beacon already
					 * on */
	BFA_STATUS_LBEACON_OFF	= 75,	/*  Link End-to-End Beacon already
					 * off */
	BFA_STATUS_PORT_NOT_INITED = 76, /*  Port not initialized */
	BFA_STATUS_RPSC_ENABLED = 77, /*  Target has a valid speed */
	BFA_STATUS_ENOFSAVE = 78,	/*  No saved firmware trace */
	BFA_STATUS_BAD_FILE = 79,	/*  Not a valid Brocade Boot Code
					 * file */
	BFA_STATUS_RLIM_EN = 80,	/*  Target rate limiting is already
					 * enabled */
	BFA_STATUS_RLIM_DIS = 81,  /*  Target rate limiting is already
				    * disabled */
	BFA_STATUS_IOC_DISABLED = 82,   /*  IOC is already disabled */
	BFA_STATUS_ADAPTER_DISABLED = 83,   /*  Adapter is already disabled */
	BFA_STATUS_BIOS_DISABLED = 84,   /*  Bios is already disabled */
	BFA_STATUS_AUTH_ENABLED = 85,   /*  Authentication is already
					 * enabled */
	BFA_STATUS_AUTH_DISABLED = 86,   /*  Authentication is already
					 * disabled */
	BFA_STATUS_ERROR_TRL_ENABLED = 87,   /*  Target rate limiting is
					      * enabled */
	BFA_STATUS_ERROR_QOS_ENABLED = 88,   /*  QoS is enabled */
	BFA_STATUS_NO_SFP_DEV = 89, /*  No SFP device check or replace SFP */
	BFA_STATUS_MEMTEST_FAILED = 90,	/*  Memory test failed contact
					 * support */
	BFA_STATUS_INVALID_DEVID = 91,	/*  Invalid device id provided */
	BFA_STATUS_QOS_ENABLED = 92, /*  QOS is already enabled */
	BFA_STATUS_QOS_DISABLED = 93, /*  QOS is already disabled */
	BFA_STATUS_INCORRECT_DRV_CONFIG = 94, /*  Check configuration
					       * key/value pair */
	BFA_STATUS_REG_FAIL = 95, /*  Can't read windows registry */
	BFA_STATUS_IM_INV_CODE = 96, /*  Invalid IOCTL code */
	BFA_STATUS_IM_INV_VLAN = 97, /*  Invalid VLAN ID */
	BFA_STATUS_IM_INV_ADAPT_NAME = 98, /*  Invalid adapter name */
	BFA_STATUS_IM_LOW_RESOURCES = 99, /*  Memory allocation failure in
					   * driver */
	BFA_STATUS_IM_VLANID_IS_PVID = 100, /*  Given VLAN id same as PVID */
	BFA_STATUS_IM_VLANID_EXISTS = 101, /*  Given VLAN id already exists */
	BFA_STATUS_IM_FW_UPDATE_FAIL = 102, /*  Updating firmware with new
					     * VLAN ID failed */
	BFA_STATUS_PORTLOG_ENABLED = 103, /*  Port Log is already enabled */
	BFA_STATUS_PORTLOG_DISABLED = 104, /*  Port Log is already disabled */
	BFA_STATUS_FILE_NOT_FOUND = 105, /*  Specified file could not be
					  * found */
	BFA_STATUS_QOS_FC_ONLY = 106, /*  QOS can be enabled for FC mode
				       * only */
	BFA_STATUS_RLIM_FC_ONLY = 107, /*  RATELIM can be enabled for FC mode
					* only */
	BFA_STATUS_CT_SPD = 108, /*  Invalid speed selection for Catapult. */
	BFA_STATUS_LEDTEST_OP = 109, /*  LED test is operating */
	BFA_STATUS_CEE_NOT_DN = 110, /*  eth port is not at down state, please
				      * bring down first */
	BFA_STATUS_10G_SPD = 111, /*  Speed setting not valid for 10G HBA */
	BFA_STATUS_IM_INV_TEAM_NAME = 112, /*  Invalid team name */
	BFA_STATUS_IM_DUP_TEAM_NAME = 113, /*  Given team name already
					    * exists */
	BFA_STATUS_IM_ADAPT_ALREADY_IN_TEAM = 114, /*  Given adapter is part
						    * of another team */
	BFA_STATUS_IM_ADAPT_HAS_VLANS = 115, /*  Adapter has VLANs configured.
					      * Delete all VLANs before
					      * creating team */
	BFA_STATUS_IM_PVID_MISMATCH = 116, /*  Mismatching PVIDs configured
					    * for adapters */
	BFA_STATUS_IM_LINK_SPEED_MISMATCH = 117, /*  Mismatching link speeds
						  * configured for adapters */
	BFA_STATUS_IM_MTU_MISMATCH = 118, /*  Mismatching MTUs configured for
					   * adapters */
	BFA_STATUS_IM_RSS_MISMATCH = 119, /*  Mismatching RSS parameters
					   * configured for adapters */
	BFA_STATUS_IM_HDS_MISMATCH = 120, /*  Mismatching HDS parameters
					   * configured for adapters */
	BFA_STATUS_IM_OFFLOAD_MISMATCH = 121, /*  Mismatching offload
					       * parameters configured for
					       * adapters */
	BFA_STATUS_IM_PORT_PARAMS = 122, /*  Error setting port parameters */
	BFA_STATUS_IM_PORT_NOT_IN_TEAM = 123, /*  Port is not part of team */
	BFA_STATUS_IM_CANNOT_REM_PRI = 124, /*  Primary adapter cannot be
					     * removed. Change primary before
					     * removing */
	BFA_STATUS_IM_MAX_PORTS_REACHED = 125, /*  Exceeding maximum ports
						* per team */
	BFA_STATUS_IM_LAST_PORT_DELETE = 126, /*  Last port in team being
					       * deleted */
	BFA_STATUS_IM_NO_DRIVER = 127, /*  IM driver is not installed */
	BFA_STATUS_IM_MAX_VLANS_REACHED = 128, /*  Exceeding maximum VLANs
						* per port */
	BFA_STATUS_TOMCAT_SPD_NOT_ALLOWED = 129, /* Bios speed config not
						  * allowed for CNA */
	BFA_STATUS_NO_MINPORT_DRIVER = 130, /*  Miniport driver is not
					     * loaded */
	BFA_STATUS_CARD_TYPE_MISMATCH = 131, /*  Card type mismatch */
	BFA_STATUS_BAD_ASICBLK = 132, /*  Bad ASIC block */
	BFA_STATUS_NO_DRIVER = 133, /*  Storage/Ethernet driver not loaded */
	BFA_STATUS_INVALID_MAC = 134, /*  Invalid mac address */
	BFA_STATUS_IM_NO_VLAN = 135, /*  No VLANs configured on the adapter */
	BFA_STATUS_IM_ETH_LB_FAILED = 136, /*  Ethernet loopback test failed */
	BFA_STATUS_IM_PVID_REMOVE = 137, /*  Cannot remove port vlan (PVID) */
	BFA_STATUS_IM_PVID_EDIT = 138, /*  Cannot edit port vlan (PVID) */
	BFA_STATUS_CNA_NO_BOOT = 139, /*  Boot upload not allowed for CNA */
	BFA_STATUS_IM_PVID_NON_ZERO = 140, /*  Port VLAN ID (PVID) is Set to
					    * Non-Zero Value */
	BFA_STATUS_IM_INETCFG_LOCK_FAILED = 141, /*  Acquiring Network
						  * Subsytem Lock Failed.Please
						  * try after some time */
	BFA_STATUS_IM_GET_INETCFG_FAILED = 142, /*  Acquiring Network Subsytem
						 * handle Failed. Please try
						 * after some time */
	BFA_STATUS_IM_NOT_BOUND = 143, /*  Brocade 10G Ethernet Service is not
					* Enabled on this port */
	BFA_STATUS_INSUFFICIENT_PERMS = 144, /*  User doesn't have sufficient
					      * permissions to execute the BCU
					      * application */
	BFA_STATUS_IM_INV_VLAN_NAME = 145, /*  Invalid/Reserved Vlan name
					    * string. The name is not allowed
					    * for the normal Vlans */
	BFA_STATUS_CMD_NOTSUPP_CNA = 146, /*  Command not supported for CNA */
	BFA_STATUS_IM_PASSTHRU_EDIT = 147, /*  Can not edit passthru vlan id */
	BFA_STATUS_IM_BIND_FAILED = 148, /*! < IM Driver bind operation
					  * failed */
	BFA_STATUS_IM_UNBIND_FAILED = 149, /* ! < IM Driver unbind operation
					    * failed */
	BFA_STATUS_MAX_VAL		/*  Unknown error code */
};
#define bfa_status_t enum bfa_status

enum bfa_eproto_status {
	BFA_EPROTO_BAD_ACCEPT = 0,
	BFA_EPROTO_UNKNOWN_RSP = 1
};
#define bfa_eproto_status_t enum bfa_eproto_status

#endif /* __BFA_DEFS_STATUS_H__ */
