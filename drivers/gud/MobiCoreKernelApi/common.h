/**
 *
 * Common data types
 *
 * <!-- Copyright Giesecke & Devrient GmbH 2009 - 2011 -->
 */
#ifndef COMMON_H
#define COMMON_H

#include "connection.h"
#include "mci.h"

void mcapi_insert_connection(
	connection_t *connection
);

void mcapi_remove_connection(
	uint32_t seq
);

unsigned int mcapi_unique_id(
	void
);


#define MC_DAEMON_PID 0xFFFFFFFF
#define MC_DRV_MOD_DEVNODE_FULLPATH "/dev/mobicore"

/* dummy function helper macro. */
#define DUMMY_FUNCTION()    do {} while (0)

#define MCDRV_ERROR(txt, ...) \
	printk(KERN_ERR "mcKernelApi %s() ### ERROR: " txt, \
		__func__, \
		##__VA_ARGS__)

#if defined(DEBUG)

/* #define DEBUG_VERBOSE */
#if defined(DEBUG_VERBOSE)
#define MCDRV_DBG_VERBOSE          MCDRV_DBG
#else
#define MCDRV_DBG_VERBOSE(...)     DUMMY_FUNCTION()
#endif

#define MCDRV_DBG(txt, ...) \
	printk(KERN_INFO "mcKernelApi %s(): " txt, \
		__func__, \
		##__VA_ARGS__)

#define MCDRV_DBG_WARN(txt, ...) \
	printk(KERN_WARNING "mcKernelApi %s() WARNING: " txt, \
		__func__, \
		##__VA_ARGS__)

#define MCDRV_DBG_ERROR(txt, ...) \
	printk(KERN_ERR "mcKernelApi %s() ### ERROR: " txt, \
		__func__, \
		##__VA_ARGS__)


#define MCDRV_ASSERT(cond) \
	do { \
		if (unlikely(!(cond))) { \
			panic("mcKernelApi Assertion failed: %s:%d\n", \
				__FILE__, __LINE__); \
		} \
	} while (0)

#elif defined(NDEBUG)

#define MCDRV_DBG_VERBOSE(...)	DUMMY_FUNCTION()
#define MCDRV_DBG(...)		DUMMY_FUNCTION()
#define MCDRV_DBG_WARN(...)	DUMMY_FUNCTION()
#define MCDRV_DBG_ERROR(...)	DUMMY_FUNCTION()

#define MCDRV_ASSERT(...)	DUMMY_FUNCTION()

#else
#error "Define DEBUG or NDEBUG"
#endif /* [not] defined(DEBUG_MCMODULE) */


#define LOG_I MCDRV_DBG_VERBOSE
#define LOG_W MCDRV_DBG_WARN
#define LOG_E MCDRV_DBG_ERROR


#define assert(expr) MCDRV_ASSERT(expr)

#endif /* COMMON_H */

/** @} */
