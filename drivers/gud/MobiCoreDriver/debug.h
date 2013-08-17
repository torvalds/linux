/*
 * MobiCore driver module.(interface to the secure world SWD)
 *
 * <-- Copyright Giesecke & Devrient GmbH 2009-2012 -->
 * <-- Copyright Trustonic Limited 2013 -->
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _MC_DEBUG_H_
#define _MC_DEBUG_H_
/* Found in main.c */
extern struct device *mcd;

#define MCDRV_DBG_ERROR(dev, txt, ...) \
	dev_err(dev, "MobiCore %s() ### ERROR: " txt, \
		__func__, \
		##__VA_ARGS__)

/* dummy function helper macro. */
#define DUMMY_FUNCTION()	do {} while (0)

#if defined(DEBUG)

/* #define DEBUG_VERBOSE */
#if defined(DEBUG_VERBOSE)
#define MCDRV_DBG_VERBOSE	MCDRV_DBG
#else
#define MCDRV_DBG_VERBOSE(...)	DUMMY_FUNCTION()
#endif

#define MCDRV_DBG(dev, txt, ...) \
	dev_info(dev, "MobiCore %s(): " txt, \
		 __func__, \
		 ##__VA_ARGS__)

#define MCDRV_DBG_WARN(dev, txt, ...) \
	dev_warn(dev, "MobiCore %s() WARNING: " txt, \
		 __func__, \
		 ##__VA_ARGS__)

#define MCDRV_ASSERT(cond) \
	do { \
		if (unlikely(!(cond))) { \
			panic("Assertion failed: %s:%d\n", \
			      __FILE__, __LINE__); \
		} \
	} while (0)

#else

#define MCDRV_DBG_VERBOSE(...)	DUMMY_FUNCTION()
#define MCDRV_DBG(...)		DUMMY_FUNCTION()
#define MCDRV_DBG_WARN(...)	DUMMY_FUNCTION()

#define MCDRV_ASSERT(...)	DUMMY_FUNCTION()

#endif /* [not] defined(DEBUG) */

#endif /* _MC_DEBUG_H_ */
