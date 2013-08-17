/*
 * Common data types for use by the MobiCore Kernel API Driver
 *
 * <-- Copyright Giesecke & Devrient GmbH 2009 - 2012 -->
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef _MC_KAPI_COMMON_H
#define _MC_KAPI_COMMON_H

#include "connection.h"
#include "mcinq.h"

void mcapi_insert_connection(struct connection *connection);
void mcapi_remove_connection(uint32_t seq);
unsigned int mcapi_unique_id(void);

#define MC_DAEMON_PID			0xFFFFFFFF
#define MC_DRV_MOD_DEVNODE_FULLPATH	"/dev/mobicore"

/* dummy function helper macro */
#define DUMMY_FUNCTION()		do {} while (0)

#define MCDRV_ERROR(txt, ...) \
	pr_err("mcKernelApi %s() ### ERROR: " txt, \
		__func__, \
		##__VA_ARGS__)

#if defined(DEBUG)

/* #define DEBUG_VERBOSE */
#if defined(DEBUG_VERBOSE)
#define MCDRV_DBG_VERBOSE		MCDRV_DBG
#else
#define MCDRV_DBG_VERBOSE(...)		DUMMY_FUNCTION()
#endif

#define MCDRV_DBG(txt, ...) \
	pr_info("mcKernelApi %s(): " txt, \
		__func__, \
		##__VA_ARGS__)

#define MCDRV_DBG_WARN(txt, ...) \
	pr_warn("mcKernelApi %s() WARNING: " txt, \
		__func__, \
		##__VA_ARGS__)

#define MCDRV_DBG_ERROR(txt, ...) \
	pr_err("mcKernelApi %s() ### ERROR: " txt, \
		__func__, \
		##__VA_ARGS__)


#define MCDRV_ASSERT(cond) \
	do { \
		if (unlikely(!(cond))) { \
			panic("mc_kernelapi Assertion failed: %s:%d\n", \
			      __FILE__, __LINE__); \
		} \
	} while (0)

#elif defined(NDEBUG)

#define MCDRV_DBG_VERBOSE(...)		DUMMY_FUNCTION()
#define MCDRV_DBG(...)			DUMMY_FUNCTION()
#define MCDRV_DBG_WARN(...)		DUMMY_FUNCTION()
#define MCDRV_DBG_ERROR(...)		DUMMY_FUNCTION()

#define MCDRV_ASSERT(...)		DUMMY_FUNCTION()

#else
#error "Define DEBUG or NDEBUG"
#endif /* [not] defined(DEBUG_MCMODULE) */

#define assert(expr)			MCDRV_ASSERT(expr)

#endif /* _MC_KAPI_COMMON_H */
