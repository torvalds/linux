/*
  Copyright (c), 2004-2005,2007-2010 Trident Microsystems, Inc.
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:

  * Redistributions of source code must retain the above copyright notice,
    this list of conditions and the following disclaimer.
  * Redistributions in binary form must reproduce the above copyright notice,
    this list of conditions and the following disclaimer in the documentation
	and/or other materials provided with the distribution.
  * Neither the name of Trident Microsystems nor Hauppauge Computer Works
    nor the names of its contributors may be used to endorse or promote
	products derived from this software without specific prior written
	permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
  POSSIBILITY OF SUCH DAMAGE.
*/

#include <linux/kernel.h>

#ifndef __BSP_TYPES_H__
#define __BSP_TYPES_H__

/*-------------------------------------------------------------------------
ENUM
-------------------------------------------------------------------------*/

/**
* \enum DRXStatus_t
* \brief Various return statusses
*/
	typedef enum {
		DRX_STS_READY = 3,  /**< device/service is ready     */
		DRX_STS_BUSY = 2,   /**< device/service is busy      */
		DRX_STS_OK = 1,	    /**< everything is OK            */
		DRX_STS_INVALID_ARG = -1,
				    /**< invalid arguments           */
		DRX_STS_ERROR = -2, /**< general error               */
		DRX_STS_FUNC_NOT_AVAILABLE = -3
				    /**< unavailable functionality   */
	} DRXStatus_t, *pDRXStatus_t;

#endif				/* __BSP_TYPES_H__ */
