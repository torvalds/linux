/*
 * Copyright (C) 2012-2017 ARM Limited or its affiliates.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _CC_PAL_LOG_H_
#define _CC_PAL_LOG_H_

#include "cc_pal_types.h"
#include "cc_pal_log_plat.h"

/*!
@file
@brief This file contains the PAL layer log definitions, by default the log is disabled.
@defgroup cc_pal_log CryptoCell PAL logging APIs and definitions
@{
@ingroup cc_pal
*/

/* PAL log levels (to be used in CC_PAL_logLevel) */
/*! PAL log level - disabled. */
#define CC_PAL_LOG_LEVEL_NULL      (-1) /*!< \internal Disable logging */
/*! PAL log level - error. */
#define CC_PAL_LOG_LEVEL_ERR       0
/*! PAL log level - warning. */
#define CC_PAL_LOG_LEVEL_WARN      1
/*! PAL log level - info. */
#define CC_PAL_LOG_LEVEL_INFO      2
/*! PAL log level - debug. */
#define CC_PAL_LOG_LEVEL_DEBUG     3
/*! PAL log level - trace. */
#define CC_PAL_LOG_LEVEL_TRACE     4
/*! PAL log level - data. */
#define CC_PAL_LOG_LEVEL_DATA      5

#ifndef CC_PAL_LOG_CUR_COMPONENT
/* Setting default component mask in case caller did not define */
/* (a mask that is always on for every log mask value but full masking) */
/*! Default log debugged component.*/
#define CC_PAL_LOG_CUR_COMPONENT 0xFFFFFFFF
#endif
#ifndef CC_PAL_LOG_CUR_COMPONENT_NAME
/*! Default log debugged component.*/
#define CC_PAL_LOG_CUR_COMPONENT_NAME "CC"
#endif

/* Select compile time log level (default if not explicitly specified by caller) */
#ifndef CC_PAL_MAX_LOG_LEVEL /* Can be overriden by external definition of this constant */
#ifdef DEBUG
/*! Default debug log level (when debug is set to on).*/
#define CC_PAL_MAX_LOG_LEVEL  CC_PAL_LOG_LEVEL_ERR /*CC_PAL_LOG_LEVEL_DEBUG*/
#else /* Disable logging */
/*! Default debug log level (when debug is set to on).*/
#define CC_PAL_MAX_LOG_LEVEL CC_PAL_LOG_LEVEL_NULL
#endif
#endif /*CC_PAL_MAX_LOG_LEVEL*/
/*! Evaluate CC_PAL_MAX_LOG_LEVEL in case provided by caller */
#define __CC_PAL_LOG_LEVEL_EVAL(level) level
/*! Maximal log level defintion.*/
#define _CC_PAL_MAX_LOG_LEVEL __CC_PAL_LOG_LEVEL_EVAL(CC_PAL_MAX_LOG_LEVEL)


#ifdef ARM_DSM
/*! Log init function. */
#define CC_PalLogInit() do {} while (0)
/*! Log set level function - sets the level of logging in case of debug. */
#define CC_PalLogLevelSet(setLevel) do {} while (0)
/*! Log set mask function - sets the component masking in case of debug. */
#define CC_PalLogMaskSet(setMask) do {} while (0)
#else
#if _CC_PAL_MAX_LOG_LEVEL > CC_PAL_LOG_LEVEL_NULL
/*! Log init function. */
void CC_PalLogInit(void);
/*! Log set level function - sets the level of logging in case of debug. */
void CC_PalLogLevelSet(int setLevel);
/*! Log set mask function - sets the component masking in case of debug. */
void CC_PalLogMaskSet(uint32_t setMask);
/*! Global variable for log level */
extern int CC_PAL_logLevel;
/*! Global variable for log mask */
extern uint32_t CC_PAL_logMask;
#else /* No log */
/*! Log init function. */
static inline void CC_PalLogInit(void) {}
/*! Log set level function - sets the level of logging in case of debug. */
static inline void CC_PalLogLevelSet(int setLevel) {CC_UNUSED_PARAM(setLevel);}
/*! Log set mask function - sets the component masking in case of debug. */
static inline void CC_PalLogMaskSet(uint32_t setMask) {CC_UNUSED_PARAM(setMask);}
#endif
#endif

/*! Filter logging based on logMask and dispatch to platform specific logging mechanism. */
#define _CC_PAL_LOG(level, format, ...)  \
	if (CC_PAL_logMask & CC_PAL_LOG_CUR_COMPONENT) \
		__CC_PAL_LOG_PLAT(CC_PAL_LOG_LEVEL_ ## level, "%s:%s: " format, CC_PAL_LOG_CUR_COMPONENT_NAME, __func__, ##__VA_ARGS__)

#if (_CC_PAL_MAX_LOG_LEVEL >= CC_PAL_LOG_LEVEL_ERR)
/*! Log messages according to log level.*/
#define CC_PAL_LOG_ERR(format, ... ) \
	_CC_PAL_LOG(ERR, format, ##__VA_ARGS__)
#else
/*! Log messages according to log level.*/
#define CC_PAL_LOG_ERR( ... ) do {} while (0)
#endif

#if (_CC_PAL_MAX_LOG_LEVEL >= CC_PAL_LOG_LEVEL_WARN)
/*! Log messages according to log level.*/
#define CC_PAL_LOG_WARN(format, ... ) \
	if (CC_PAL_logLevel >= CC_PAL_LOG_LEVEL_WARN) \
		_CC_PAL_LOG(WARN, format, ##__VA_ARGS__)
#else
/*! Log messages according to log level.*/
#define CC_PAL_LOG_WARN( ... ) do {} while (0)
#endif

#if (_CC_PAL_MAX_LOG_LEVEL >= CC_PAL_LOG_LEVEL_INFO)
/*! Log messages according to log level.*/
#define CC_PAL_LOG_INFO(format, ... ) \
	if (CC_PAL_logLevel >= CC_PAL_LOG_LEVEL_INFO) \
		_CC_PAL_LOG(INFO, format, ##__VA_ARGS__)
#else
/*! Log messages according to log level.*/
#define CC_PAL_LOG_INFO( ... ) do {} while (0)
#endif

#if (_CC_PAL_MAX_LOG_LEVEL >= CC_PAL_LOG_LEVEL_DEBUG)
/*! Log messages according to log level.*/
#define CC_PAL_LOG_DEBUG(format, ... ) \
	if (CC_PAL_logLevel >= CC_PAL_LOG_LEVEL_DEBUG) \
		_CC_PAL_LOG(DEBUG, format, ##__VA_ARGS__)

/*! Log message buffer.*/
#define CC_PAL_LOG_DUMP_BUF(msg, buf, size)		\
	do {						\
	int i;						\
	uint8_t	*pData = (uint8_t*)buf;			\
							\
	PRINTF("%s (%d):\n", msg, size);		\
	for (i = 0; i < size; i++) {			\
		PRINTF("0x%02X ", pData[i]);		\
		if ((i & 0xF) == 0xF) {			\
			PRINTF("\n");			\
		}					\
	}						\
	PRINTF("\n");					\
	} while (0)
#else
/*! Log debug messages.*/
#define CC_PAL_LOG_DEBUG( ... ) do {} while (0)
/*! Log debug buffer.*/
#define CC_PAL_LOG_DUMP_BUF(msg, buf, size)	do {} while (0)
#endif

#if (_CC_PAL_MAX_LOG_LEVEL >= CC_PAL_LOG_LEVEL_TRACE)
/*! Log debug trace.*/
#define CC_PAL_LOG_TRACE(format, ... ) \
	if (CC_PAL_logLevel >= CC_PAL_LOG_LEVEL_TRACE) \
		_CC_PAL_LOG(TRACE, format, ##__VA_ARGS__)
#else
/*! Log debug trace.*/
#define CC_PAL_LOG_TRACE(...) do {} while (0)
#endif

#if (_CC_PAL_MAX_LOG_LEVEL >= CC_PAL_LOG_LEVEL_TRACE)
/*! Log debug data.*/
#define CC_PAL_LOG_DATA(format, ...) \
	if (CC_PAL_logLevel >= CC_PAL_LOG_LEVEL_TRACE) \
		_CC_PAL_LOG(DATA, format, ##__VA_ARGS__)
#else
/*! Log debug data.*/
#define CC_PAL_LOG_DATA( ...) do {} while (0)
#endif
/**
@}
 */

#endif /*_CC_PAL_LOG_H_*/
