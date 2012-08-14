/**
* \file $Id: bsp_tuner.h,v 1.5 2009/10/19 22:15:13 dingtao Exp $
*
* \brief Tuner dependable type definitions, macro's and functions
*
*/

/*
* $(c) 2004-2006,2008-2009 Trident Microsystems, Inc. - All rights reserved.
*
* This software and related documentation (the 'Software') are intellectual
* property owned by Trident and are copyright of Trident, unless specifically
* noted otherwise.
*
* Any use of the Software is permitted only pursuant to the terms of the
* license agreement, if any, which accompanies, is included with or applicable
* to the Software ('License Agreement') or upon express written consent of
* Trident. Any copying, reproduction or redistribution of the Software in
* whole or in part by any means not in accordance with the License Agreement
* or as agreed in writing by Trident is expressly prohibited.
*
* THE SOFTWARE IS WARRANTED, IF AT ALL, ONLY ACCORDING TO THE TERMS OF THE
* LICENSE AGREEMENT. EXCEPT AS WARRANTED IN THE LICENSE AGREEMENT THE SOFTWARE
* IS DELIVERED 'AS IS' AND TRIDENT HEREBY DISCLAIMS ALL WARRANTIES AND
* CONDITIONS WITH REGARD TO THE SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES
* AND CONDITIONS OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, QUIT
* ENJOYMENT, TITLE AND NON-INFRINGEMENT OF ANY THIRD PARTY INTELLECTUAL
* PROPERTY OR OTHER RIGHTS WHICH MAY RESULT FROM THE USE OR THE INABILITY
* TO USE THE SOFTWARE.
*
* IN NO EVENT SHALL TRIDENT BE LIABLE FOR INDIRECT, INCIDENTAL, CONSEQUENTIAL,
* PUNITIVE, SPECIAL OR OTHER DAMAGES WHATSOEVER INCLUDING WITHOUT LIMITATION,
* DAMAGES FOR LOSS OF BUSINESS PROFITS, BUSINESS INTERRUPTION, LOSS OF BUSINESS
* INFORMATION, AND THE LIKE, ARISING OUT OF OR RELATING TO THE USE OF OR THE
* INABILITY TO USE THE SOFTWARE, EVEN IF TRIDENT HAS BEEN ADVISED OF THE
* POSSIBILITY OF SUCH DAMAGES, EXCEPT PERSONAL INJURY OR DEATH RESULTING FROM
* TRIDENT'S NEGLIGENCE.                                                        $
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
#define TUNER_MODE_SUB0    0x0001   /* for sub-mode (e.g. RF-AGC setting) */
#define TUNER_MODE_SUB1    0x0002   /* for sub-mode (e.g. RF-AGC setting) */
#define TUNER_MODE_SUB2    0x0004   /* for sub-mode (e.g. RF-AGC setting) */
#define TUNER_MODE_SUB3    0x0008   /* for sub-mode (e.g. RF-AGC setting) */
#define TUNER_MODE_SUB4    0x0010   /* for sub-mode (e.g. RF-AGC setting) */
#define TUNER_MODE_SUB5    0x0020   /* for sub-mode (e.g. RF-AGC setting) */
#define TUNER_MODE_SUB6    0x0040   /* for sub-mode (e.g. RF-AGC setting) */
#define TUNER_MODE_SUB7    0x0080   /* for sub-mode (e.g. RF-AGC setting) */

#define TUNER_MODE_DIGITAL 0x0100   /* for digital channel (e.g. DVB-T)   */
#define TUNER_MODE_ANALOG  0x0200   /* for analog channel  (e.g. PAL)     */
#define TUNER_MODE_SWITCH  0x0400   /* during channel switch & scanning   */
#define TUNER_MODE_LOCK    0x0800   /* after tuner has locked             */
#define TUNER_MODE_6MHZ    0x1000   /* for 6MHz bandwidth channels        */
#define TUNER_MODE_7MHZ    0x2000   /* for 7MHz bandwidth channels        */
#define TUNER_MODE_8MHZ    0x4000   /* for 8MHz bandwidth channels        */

#define TUNER_MODE_SUB_MAX 8
#define TUNER_MODE_SUBALL  (  TUNER_MODE_SUB0 | TUNER_MODE_SUB1 | \
			      TUNER_MODE_SUB2 | TUNER_MODE_SUB3 | \
			      TUNER_MODE_SUB4 | TUNER_MODE_SUB5 | \
			      TUNER_MODE_SUB6 | TUNER_MODE_SUB7 )

/*------------------------------------------------------------------------------
TYPEDEFS
------------------------------------------------------------------------------*/

typedef u32_t TUNERMode_t;
typedef pu32_t pTUNERMode_t;

typedef char*           TUNERSubMode_t;    /* description of submode */
typedef TUNERSubMode_t *pTUNERSubMode_t;


typedef enum {

   TUNER_LOCKED,
   TUNER_NOT_LOCKED

} TUNERLockStatus_t, *pTUNERLockStatus_t;


typedef struct {

   char           *name;         /* Tuner brand & type name */
   DRXFrequency_t minFreqRF;     /* Lowest  RF input frequency, in kHz */
   DRXFrequency_t maxFreqRF;     /* Highest RF input frequency, in kHz */

   u8_t            subMode;             /* Index to sub-mode in use */
   pTUNERSubMode_t subModeDescriptions; /* Pointer to description of sub-modes*/
   u8_t            subModes;            /* Number of available sub-modes      */

   /* The following fields will be either 0, NULL or FALSE and do not need
      initialisation */
   void           *selfCheck;     /* gives proof of initialization  */
   Bool_t         programmed;     /* only valid if selfCheck is OK  */
   DRXFrequency_t RFfrequency;    /* only valid if programmed       */
   DRXFrequency_t IFfrequency;    /* only valid if programmed       */

   void*          myUserData;     /* pointer to associated demod instance */
   u16_t          myCapabilities; /* value for storing application flags  */

} TUNERCommonAttr_t, *pTUNERCommonAttr_t;


/*
* Generic functions for DRX devices.
*/
typedef struct TUNERInstance_s *pTUNERInstance_t;

typedef DRXStatus_t (*TUNEROpenFunc_t)(         pTUNERInstance_t  tuner );
typedef DRXStatus_t (*TUNERCloseFunc_t)(        pTUNERInstance_t  tuner );

typedef DRXStatus_t (*TUNERSetFrequencyFunc_t)( pTUNERInstance_t  tuner,
						TUNERMode_t       mode,
						DRXFrequency_t    frequency );

typedef DRXStatus_t (*TUNERGetFrequencyFunc_t)( pTUNERInstance_t  tuner,
						TUNERMode_t       mode,
						pDRXFrequency_t   RFfrequency,
						pDRXFrequency_t   IFfrequency );

typedef DRXStatus_t (*TUNERLockStatusFunc_t)(   pTUNERInstance_t  tuner,
						pTUNERLockStatus_t lockStat );

typedef DRXStatus_t (*TUNERi2cWriteReadFunc_t)( pTUNERInstance_t  tuner,
						pI2CDeviceAddr_t  wDevAddr,
						u16_t             wCount,
						pu8_t             wData,
						pI2CDeviceAddr_t  rDevAddr,
						u16_t             rCount,
						pu8_t             rData );

typedef struct
{
   TUNEROpenFunc_t         openFunc;
   TUNERCloseFunc_t        closeFunc;
   TUNERSetFrequencyFunc_t setFrequencyFunc;
   TUNERGetFrequencyFunc_t getFrequencyFunc;
   TUNERLockStatusFunc_t   lockStatusFunc;
   TUNERi2cWriteReadFunc_t i2cWriteReadFunc;

} TUNERFunc_t, *pTUNERFunc_t;

typedef struct TUNERInstance_s {

   I2CDeviceAddr_t      myI2CDevAddr;
   pTUNERCommonAttr_t   myCommonAttr;
   void*                myExtAttr;
   pTUNERFunc_t         myFunct;

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

DRXStatus_t DRXBSP_TUNER_Open( pTUNERInstance_t tuner );

DRXStatus_t DRXBSP_TUNER_Close( pTUNERInstance_t tuner );

DRXStatus_t DRXBSP_TUNER_SetFrequency( pTUNERInstance_t tuner,
				       TUNERMode_t      mode,
				       DRXFrequency_t   frequency );

DRXStatus_t DRXBSP_TUNER_GetFrequency( pTUNERInstance_t tuner,
				       TUNERMode_t      mode,
				       pDRXFrequency_t  RFfrequency,
				       pDRXFrequency_t  IFfrequency );

DRXStatus_t DRXBSP_TUNER_LockStatus(   pTUNERInstance_t   tuner,
				       pTUNERLockStatus_t lockStat );

DRXStatus_t DRXBSP_TUNER_DefaultI2CWriteRead(   pTUNERInstance_t tuner,
						pI2CDeviceAddr_t wDevAddr,
						u16_t            wCount,
						pu8_t            wData,
						pI2CDeviceAddr_t rDevAddr,
						u16_t            rCount,
						pu8_t            rData);

/*------------------------------------------------------------------------------
THE END
------------------------------------------------------------------------------*/
#ifdef __cplusplus
}
#endif
#endif   /* __DRXBSP_TUNER_H__ */

/* End of file */
