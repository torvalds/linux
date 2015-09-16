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

/* Generic success will return 0 */
#define WILC_SUCCESS	0       /** Generic success */

/* Negative numbers to indicate failures */
#define WILC_FAIL			-100 /** Generic Fail */
#define WILC_BUSY			-101 /** Busy with another operation*/
#define WILC_INVALID_ARGUMENT		-102 /** A given argument is invalid*/
#define WILC_INVALID_STATE		-103 /** An API request would violate the Driver state machine (i.e. to start PID while not camped)*/
#define WILC_BUFFER_OVERFLOW		-104 /** In copy operations if the copied data is larger than the allocated buffer*/
#define WILC_NULL_PTR			-105 /** null pointer is passed or used */
#define WILC_TIMEOUT			-109
#define WILC_NOT_FOUND			-113 /** Cant find the file to load */
#define WILC_NO_MEM			-114

#endif
