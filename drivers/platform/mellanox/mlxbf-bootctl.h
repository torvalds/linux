/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2019, Mellanox Technologies. All rights reserved.
 */

#ifndef __MLXBF_BOOTCTL_H__
#define __MLXBF_BOOTCTL_H__

/*
 * Request that the on-chip watchdog be enabled, or disabled, after
 * the next chip soft reset. This call does not affect the current
 * status of the on-chip watchdog. If non-zero, the argument
 * specifies the watchdog interval in seconds. If zero, the watchdog
 * will not be enabled after the next soft reset. Non-zero errors are
 * returned as documented below.
 */
#define MLXBF_BOOTCTL_SET_POST_RESET_WDOG	0x82000000

/*
 * Query the status which has been requested for the on-chip watchdog
 * after the next chip soft reset. Returns the interval as set by
 * MLXBF_BOOTCTL_SET_POST_RESET_WDOG.
 */
#define MLXBF_BOOTCTL_GET_POST_RESET_WDOG	0x82000001

/*
 * Request that a specific boot action be taken at the next soft
 * reset. By default, the boot action is set by external chip pins,
 * which are sampled on hard reset. Note that the boot action
 * requested by this call will persist on subsequent resets unless
 * this service, or the MLNX_SET_SECOND_RESET_ACTION service, is
 * invoked. See below for the available MLNX_BOOT_xxx parameter
 * values. Non-zero errors are returned as documented below.
 */
#define MLXBF_BOOTCTL_SET_RESET_ACTION		0x82000002

/*
 * Return the specific boot action which will be taken at the next
 * soft reset. Returns the reset action (see below for the parameter
 * values for MLXBF_BOOTCTL_SET_RESET_ACTION).
 */
#define MLXBF_BOOTCTL_GET_RESET_ACTION		0x82000003

/*
 * Request that a specific boot action be taken at the soft reset
 * after the next soft reset. For a specified valid boot mode, the
 * effect of this call is identical to that of invoking
 * MLXBF_BOOTCTL_SET_RESET_ACTION after the next chip soft reset; in
 * particular, after that reset, the action for the now next reset can
 * be queried with MLXBF_BOOTCTL_GET_RESET_ACTION and modified with
 * MLXBF_BOOTCTL_SET_RESET_ACTION. You may also specify the parameter as
 * MLNX_BOOT_NONE, which is equivalent to specifying that no call to
 * MLXBF_BOOTCTL_SET_RESET_ACTION be taken after the next chip soft reset.
 * This call does not affect the action to be taken at the next soft
 * reset. Non-zero errors are returned as documented below.
 */
#define MLXBF_BOOTCTL_SET_SECOND_RESET_ACTION	0x82000004

/*
 * Return the specific boot action which will be taken at the soft
 * reset after the next soft reset; this will be one of the valid
 * actions for MLXBF_BOOTCTL_SET_SECOND_RESET_ACTION.
 */
#define MLXBF_BOOTCTL_GET_SECOND_RESET_ACTION	0x82000005

/*
 * Return the fuse status of the current chip. The caller should specify
 * with the second argument if the state of the lifecycle fuses or the
 * version of secure boot fuse keys left should be returned.
 */
#define MLXBF_BOOTCTL_GET_TBB_FUSE_STATUS	0x82000006

/* Reset eMMC by programming the RST_N register. */
#define MLXBF_BOOTCTL_SET_EMMC_RST_N		0x82000007

#define MLXBF_BOOTCTL_GET_DIMM_INFO		0x82000008

/*
 * Initiate Firmware Reset via TYU. This might be invoked during the reset
 * flow in isolation mode.
 */
#define MLXBF_BOOTCTL_FW_RESET  0x8200000D

/* SMC function IDs for SiP Service queries */
#define MLXBF_BOOTCTL_SIP_SVC_CALL_COUNT	0x8200ff00
#define MLXBF_BOOTCTL_SIP_SVC_UID		0x8200ff01
#define MLXBF_BOOTCTL_SIP_SVC_VERSION		0x8200ff03

/* ARM Standard Service Calls version numbers */
#define MLXBF_BOOTCTL_SVC_VERSION_MAJOR		0x0
#define MLXBF_BOOTCTL_SVC_VERSION_MINOR		0x2

/* Number of svc calls defined. */
#define MLXBF_BOOTCTL_NUM_SVC_CALLS 12

/* Valid reset actions for MLXBF_BOOTCTL_SET_RESET_ACTION. */
#define MLXBF_BOOTCTL_EXTERNAL	0 /* Not boot from eMMC */
#define MLXBF_BOOTCTL_EMMC	1 /* From primary eMMC boot partition */
#define MLNX_BOOTCTL_SWAP_EMMC	2 /* Swap eMMC boot partitions and reboot */
#define MLXBF_BOOTCTL_EMMC_LEGACY	3 /* From primary eMMC in legacy mode */

/* Valid arguments for requesting the fuse status. */
#define MLXBF_BOOTCTL_FUSE_STATUS_LIFECYCLE	0 /* Return lifecycle status. */
#define MLXBF_BOOTCTL_FUSE_STATUS_KEYS	1 /* Return secure boot key status */

/* Additional value to disable the MLXBF_BOOTCTL_SET_SECOND_RESET_ACTION. */
#define MLXBF_BOOTCTL_NONE	0x7fffffff /* Don't change next boot action */

#endif /* __MLXBF_BOOTCTL_H__ */
