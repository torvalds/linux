#ifndef __WILC_ERRORSUPPORT_H__
#define __WILC_ERRORSUPPORT_H__

/*!
 *  @file		wilc_errorsupport.h
 *  @brief		Error reporting and handling support
 *  @author		syounan
 *  @sa			wilc_oswrapper.h top level OS wrapper file
 *  @date		10 Aug 2010
 *  @version		1.0
 */

#include "linux_wlan_common.h"

/* Psitive Numbers to indicate sucess with special status */
#define WILC_ALREADY_EXSIT	(+100)    /** The requested object already exists */

/* Generic success will return 0 */
#define WILC_SUCCESS	0       /** Generic success */

/* Negative numbers to indicate failures */
#define WILC_FAIL			-100 /** Generic Fail */
#define WILC_BUSY			-101 /** Busy with another operation*/
#define WILC_INVALID_ARGUMENT		-102 /** A given argument is invalid*/
#define WILC_INVALID_STATE		-103 /** An API request would violate the Driver state machine (i.e. to start PID while not camped)*/
#define WILC_BUFFER_OVERFLOW		-104 /** In copy operations if the copied data is larger than the allocated buffer*/
#define WILC_NULL_PTR			-105 /** null pointer is passed or used */
#define WILC_EMPTY			-107
#define WILC_FULL			-108
#define WILC_TIMEOUT			-109
#define WILC_CANCELED			-110 /** The required operation have been canceled by the user*/
#define WILC_INVALID_FILE		-112 /** The Loaded file is corruped or having an invalid format */
#define WILC_NOT_FOUND			-113 /** Cant find the file to load */
#define WILC_NO_MEM			-114
#define WILC_UNSUPPORTED_VERSION	-115
#define WILC_FILE_EOF			-116


/* Error type */
typedef s32 WILC_ErrNo;

#define WILC_IS_ERR(__status__) (__status__ < WILC_SUCCESS)

#define WILC_ERRORCHECK(__status__) do { \
		if (WILC_IS_ERR(__status__)) { \
			PRINT_ER("PRINT_ER(%d)\n", __status__);	\
			goto ERRORHANDLER; \
		} \
} while (0)

#define WILC_ERRORREPORT(__status__, __err__) do { \
		PRINT_ER("PRINT_ER(%d)\n", __err__); \
		__status__ = __err__; \
		goto ERRORHANDLER; \
} while (0)

#define  WILC_NULLCHECK(__status__, __ptr__)	do { \
		if (__ptr__ == NULL) { \
			WILC_ERRORREPORT(__status__, WILC_NULL_PTR); \
		} \
} while (0)

#define WILC_CATCH(__status__) \
ERRORHANDLER: \
	if (WILC_IS_ERR(__status__)) \

#endif
