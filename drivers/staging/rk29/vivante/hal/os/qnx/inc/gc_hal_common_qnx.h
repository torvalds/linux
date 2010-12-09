/****************************************************************************
*
*    Copyright (c) 2005 - 2010 by Vivante Corp.  All rights reserved.
*
*    The material in this file is confidential and contains trade secrets
*    of Vivante Corporation. This is proprietary information owned by
*    Vivante Corporation. No part of this work may be disclosed,
*    reproduced, copied, transmitted, or used in any way for any purpose,
*    without the express written permission of Vivante Corporation.
*
*****************************************************************************
*
*    Auto-generated file on 12/8/2010. Do not edit!!!
*
*****************************************************************************/


/*
 * gc_hal_common_qnx.h
 *
 *  Created on: Jul 7, 2010
 *      Author: tarang
 */

#ifndef GC_HAL_COMMON_QNX_H_
#define GC_HAL_COMMON_QNX_H_

/******************************************************************************\
******************************* QNX Control Codes ******************************
\******************************************************************************/
#ifndef _IOMGR_VIVANTE
#define _IOMGR_VIVANTE 					(_IOMGR_PRIVATE_BASE + 0x301)
#endif

/*******************************************************************************
**	Signal management.
**
**	Is much simpler in Neutrino versus Linux :-)
**
**	Neutrino pulses are equivalent to RT signals (queued, small payload) except
**	they are explicitly received on a channel. We therefore dedicate a thread
**	to handle them.
**
**	We don't use RT signals because:
**	1. They can be delivered on any thread.
**	2. We don't support SA_RESTART so blocking kernel calls can fail. It would be
**	   impossible to robustly handle this condition in all libraries.
**
**	Only downside is that more information needs to be passed between client/server
**	(signals require only PID, pulses require connection ID and receive ID).
*/

typedef struct _gcsSIGNAL
{
	/* Pointer to gcoOS object. */
	gcoOS			os;

	/* Signaled state. */
	gctBOOL			state;

	/* Manual reset flag. */
	gctBOOL			manual;

	/* Mutex. */
	pthread_mutex_t	mutex;

	/* Condition. */
	pthread_cond_t	condition;

	/* Number of signals pending in the command queue. */
	gctINT			pending;

	/* Number of signals received. */
	gctINT			received;
}
gcsSIGNAL;

#endif /* GC_HAL_COMMON_QNX_H_ */

