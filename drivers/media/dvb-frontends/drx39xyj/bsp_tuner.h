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

/**
* \file $Id: bsp_tuner.h,v 1.5 2009/10/19 22:15:13 dingtao Exp $
*
* \brief Tuner dependable type definitions, macro's and functions
*
*/

#ifndef __DRXBSP_TUNER_H__
#define __DRXBSP_TUNER_H__
/*------------------------------------------------------------------------------
INCLUDES
------------------------------------------------------------------------------*/
#include "bsp_types.h"
#include "bsp_i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

/*------------------------------------------------------------------------------
DEFINES
------------------------------------------------------------------------------*/

	/* Sub-mode bits should be adjacent and incremental */
#define TUNER_MODE_SUB0    0x0001	/* for sub-mode (e.g. RF-AGC setting) */
#define TUNER_MODE_SUB1    0x0002	/* for sub-mode (e.g. RF-AGC setting) */
#define TUNER_MODE_SUB2    0x0004	/* for sub-mode (e.g. RF-AGC setting) */
#define TUNER_MODE_SUB3    0x0008	/* for sub-mode (e.g. RF-AGC setting) */
#define TUNER_MODE_SUB4    0x0010	/* for sub-mode (e.g. RF-AGC setting) */
#define TUNER_MODE_SUB5    0x0020	/* for sub-mode (e.g. RF-AGC setting) */
#define TUNER_MODE_SUB6    0x0040	/* for sub-mode (e.g. RF-AGC setting) */
#define TUNER_MODE_SUB7    0x0080	/* for sub-mode (e.g. RF-AGC setting) */

#define TUNER_MODE_DIGITAL 0x0100	/* for digital channel (e.g. DVB-T)   */
#define TUNER_MODE_ANALOG  0x0200	/* for analog channel  (e.g. PAL)     */
#define TUNER_MODE_SWITCH  0x0400	/* during channel switch & scanning   */
#define TUNER_MODE_LOCK    0x0800	/* after tuner has locked             */
#define TUNER_MODE_6MHZ    0x1000	/* for 6MHz bandwidth channels        */
#define TUNER_MODE_7MHZ    0x2000	/* for 7MHz bandwidth channels        */
#define TUNER_MODE_8MHZ    0x4000	/* for 8MHz bandwidth channels        */

#define TUNER_MODE_SUB_MAX 8
#define TUNER_MODE_SUBALL  (TUNER_MODE_SUB0 | TUNER_MODE_SUB1 | \
			      TUNER_MODE_SUB2 | TUNER_MODE_SUB3 | \
			      TUNER_MODE_SUB4 | TUNER_MODE_SUB5 | \
			      TUNER_MODE_SUB6 | TUNER_MODE_SUB7)

/*------------------------------------------------------------------------------
TYPEDEFS
------------------------------------------------------------------------------*/

	typedef u32 TUNERMode_t;
	typedef u32 *pTUNERMode_t;

	typedef char *TUNERSubMode_t;	/* description of submode */
	typedef TUNERSubMode_t *pTUNERSubMode_t;

	typedef enum {

		TUNER_LOCKED,
		TUNER_NOT_LOCKED
	} TUNERLockStatus_t, *pTUNERLockStatus_t;

	typedef struct {

		char *name;	/* Tuner brand & type name */
		s32 minFreqRF;	/* Lowest  RF input frequency, in kHz */
		s32 maxFreqRF;	/* Highest RF input frequency, in kHz */

		u8 subMode;	/* Index to sub-mode in use */
		pTUNERSubMode_t subModeDescriptions;	/* Pointer to description of sub-modes */
		u8 subModes;	/* Number of available sub-modes      */

		/* The following fields will be either 0, NULL or false and do not need
		   initialisation */
		void *selfCheck;	/* gives proof of initialization  */
		bool programmed;	/* only valid if selfCheck is OK  */
		s32 RFfrequency;	/* only valid if programmed       */
		s32 IFfrequency;	/* only valid if programmed       */

		void *myUserData;	/* pointer to associated demod instance */
		u16 myCapabilities;	/* value for storing application flags  */

	} TUNERCommonAttr_t, *pTUNERCommonAttr_t;

/*
* Generic functions for DRX devices.
*/
	typedef struct TUNERInstance_s *pTUNERInstance_t;

	typedef DRXStatus_t(*TUNEROpenFunc_t) (pTUNERInstance_t tuner);
	typedef DRXStatus_t(*TUNERCloseFunc_t) (pTUNERInstance_t tuner);

	typedef DRXStatus_t(*TUNERSetFrequencyFunc_t) (pTUNERInstance_t tuner,
						       TUNERMode_t mode,
						       s32
						       frequency);

	typedef DRXStatus_t(*TUNERGetFrequencyFunc_t) (pTUNERInstance_t tuner,
						       TUNERMode_t mode,
						       s32 *
						       RFfrequency,
						       s32 *
						       IFfrequency);

	typedef DRXStatus_t(*TUNERLockStatusFunc_t) (pTUNERInstance_t tuner,
						     pTUNERLockStatus_t
						     lockStat);

	typedef DRXStatus_t(*TUNERi2cWriteReadFunc_t) (pTUNERInstance_t tuner,
						       struct i2c_device_addr *
						       wDevAddr, u16 wCount,
						       u8 *wData,
						       struct i2c_device_addr *
						       rDevAddr, u16 rCount,
						       u8 *rData);

	typedef struct {
		TUNEROpenFunc_t openFunc;
		TUNERCloseFunc_t closeFunc;
		TUNERSetFrequencyFunc_t setFrequencyFunc;
		TUNERGetFrequencyFunc_t getFrequencyFunc;
		TUNERLockStatusFunc_t lockStatusFunc;
		TUNERi2cWriteReadFunc_t i2cWriteReadFunc;

	} TUNERFunc_t, *pTUNERFunc_t;

	typedef struct TUNERInstance_s {

		struct i2c_device_addr myI2CDevAddr;
		pTUNERCommonAttr_t myCommonAttr;
		void *myExtAttr;
		pTUNERFunc_t myFunct;

	} TUNERInstance_t;

/*------------------------------------------------------------------------------
ENUM
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
STRUCTS
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
Exported FUNCTIONS
------------------------------------------------------------------------------*/

	DRXStatus_t DRXBSP_TUNER_Open(pTUNERInstance_t tuner);

	DRXStatus_t DRXBSP_TUNER_Close(pTUNERInstance_t tuner);

	DRXStatus_t DRXBSP_TUNER_SetFrequency(pTUNERInstance_t tuner,
					      TUNERMode_t mode,
					      s32 frequency);

	DRXStatus_t DRXBSP_TUNER_GetFrequency(pTUNERInstance_t tuner,
					      TUNERMode_t mode,
					      s32 *RFfrequency,
					      s32 *IFfrequency);

	DRXStatus_t DRXBSP_TUNER_LockStatus(pTUNERInstance_t tuner,
					    pTUNERLockStatus_t lockStat);

	DRXStatus_t DRXBSP_TUNER_DefaultI2CWriteRead(pTUNERInstance_t tuner,
						     struct i2c_device_addr *wDevAddr,
						     u16 wCount,
						     u8 *wData,
						     struct i2c_device_addr *rDevAddr,
						     u16 rCount, u8 *rData);

/*------------------------------------------------------------------------------
THE END
------------------------------------------------------------------------------*/
#ifdef __cplusplus
}
#endif
#endif				/* __DRXBSP_TUNER_H__ */
/* End of file */
