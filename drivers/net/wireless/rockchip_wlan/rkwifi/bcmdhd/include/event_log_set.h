/*
 * EVENT_LOG system definitions
 *
 * Copyright (C) 2020, Broadcom.
 *
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 *
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 *
 *
 * <<Broadcom-WL-IPTag/Dual:>>
 */

#ifndef _EVENT_LOG_SET_H_
#define _EVENT_LOG_SET_H_

/* Set assignments */
#define EVENT_LOG_SET_BUS		(0u)
#define EVENT_LOG_SET_WL		(1u)
#define EVENT_LOG_SET_PSM		(2u)
#define EVENT_LOG_SET_ERROR		(3u)

/* MSCH logging */
#define EVENT_LOG_SET_MSCH_PROFILER	(4u)

/* A particular customer uses sets 5, 6, and 7. There is a request
 * to not name these log sets as that could limit their ability to
 * use different log sets in future.
 * Sets 5, 6, and 7 are instantiated by host
 * In such case, ecounters could be mapped to any set that host
 * configures. They may or may not use set 5.
 */
#define EVENT_LOG_SET_5			(5u)
#define EVENT_LOG_SET_ECOUNTERS		(EVENT_LOG_SET_5)
#define EVENT_LOG_SET_6			(6u)
#define EVENT_LOG_SET_7			(7u)

/* Temporary change to satisfy compilation across branches
 * Will be removed after checkin
 */
#define EVENT_LOG_SET_8			(8u)
#define EVENT_LOG_SET_PRSRV		(EVENT_LOG_SET_8)

#define EVENT_LOG_SET_9			(9u)
/* General purpose preserve chatty.
 * EVENT_LOG_SET_PRSRV_CHATTY log set should not be used by FW as it is
 * used by customer host. FW should use EVENT_LOG_SET_GP_PRSRV_CHATTY
 * for general purpose preserve chatty logs.
 */
#define EVENT_LOG_SET_GP_PRSRV_CHATTY	(EVENT_LOG_SET_9)
#define EVENT_LOG_SET_PRSRV_CHATTY	(EVENT_LOG_SET_6)

/* BUS preserve */
#define EVENT_LOG_SET_PRSRV_BUS		(10u)

/* WL preserve */
#define EVENT_LOG_SET_PRSRV_WL		(11u)

/* Slotted BSS set */
#define EVENT_LOG_SET_WL_SLOTTED_BSS    (12u)

/* PHY entity logging */
#define EVENT_LOG_SET_PHY		(13u)

/* PHY preserve */
#define EVENT_LOG_SET_PRSRV_PHY		(14u)

/* RTE entity */
#define EVENT_LOG_SET_RTE		(15u)

/* Malloc and free logging */
#define EVENT_LOG_SET_MEM_API		(16u)

/* Console buffer */
#define EVENT_LOG_SET_RTE_CONS_BUF	(17u)

/* three log sets for general debug purposes */
#define EVENT_LOG_SET_GENERAL_DBG_1	(18u)
#define EVENT_LOG_SET_GENERAL_DBG_2	(19u)
#define EVENT_LOG_SET_GENERAL_DBG_3	(20u)

/* Log sets for capturing power related logs. Note that these sets
 * are to be used across entire system and not just WL.
 */
#define EVENT_LOG_SET_POWER_1		(21u)
#define EVENT_LOG_SET_POWER_2		(22u)

/* Used for timestamp plotting, TS_LOG() */
#define EVENT_LOG_SET_TS_LOG		(23u)

/* BUS preserve chatty */
#define EVENT_LOG_SET_PRSRV_BUS_CHATTY	(24u)

/* PRESERVE_PREIODIC_LOG_SET */
/* flush if host is in D0 at every period */
#define EVENT_LOG_SET_PRSV_PERIODIC	(25u)

/* AMT logging and other related information */
#define EVENT_LOG_SET_AMT		(26u)

/* State machine logging. Part of preserve logs */
#define EVENT_LOG_SET_FSM		(27u)

/* wbus related logging */
#define EVENT_LOG_SET_WBUS		(28u)

/* bcm trace logging */
#define EVENT_LOG_SET_BCM_TRACE		(29u)

/* For PM alert related logging */
#define EVENT_LOG_SET_WL_PS_LOG		(30u)

#ifndef NUM_EVENT_LOG_SETS
/* Set a maximum number of sets here.  It is not dynamic for
 * efficiency of the EVENT_LOG calls. Old branches could define
 * this to an appropriat enumber in their makefiles to reduce
 * ROM invalidation
 */
#ifdef NUM_EVENT_LOG_SETS_V2
/* for v2, everything has became unsigned */
#define NUM_EVENT_LOG_SETS (31u)
#else /* NUM_EVENT_LOG_SETS_V2 */
#define NUM_EVENT_LOG_SETS (31)
#endif /* NUM_EVENT_LOG_SETS_V2 */
#endif /* NUM_EVENT_LOG_SETS */

/* send delayed logs when >= 50% of buffer is full */
#ifndef ECOUNTERS_DELAYED_FLUSH_PERCENTAGE
#define ECOUNTERS_DELAYED_FLUSH_PERCENTAGE	(50)
#endif

#endif /* _EVENT_LOG_SET_H_ */
