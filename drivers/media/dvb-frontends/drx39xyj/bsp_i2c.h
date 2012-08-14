/**
* \file $Id: bsp_i2c.h,v 1.5 2009/07/07 14:20:30 justin Exp $
*
* \brief I2C API, implementation depends on board specifics
*
* This module encapsulates I2C access.In some applications several devices
* share one I2C bus. If these devices have the same I2C address some kind
* off "switch" must be implemented to ensure error free communication with
* one device.  In case such a "switch" is used, the device ID can be used
* to implement control over this "switch".
*
*
*/

/*
* $(c) 2004-2005,2008-2009 Trident Microsystems, Inc. - All rights reserved.
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

#ifndef __BSPI2C_H__
#define __BSPI2C_H__
/*------------------------------------------------------------------------------
INCLUDES
------------------------------------------------------------------------------*/
#include "bsp_types.h"

#ifdef __cplusplus
extern "C" {
#endif
/*------------------------------------------------------------------------------
TYPEDEFS
------------------------------------------------------------------------------*/
/**
* \typedef I2Caddr_t
* \brief I2C device address (7-bit or 10-bit)
*/
typedef u16_t I2Caddr_t;

/**
* \typedef I2CdevId_t
* \brief Device identifier.
*
* The device ID can be useful if several devices share an I2C address,
* or if multiple I2C busses are used.
* It can be used to control a "switch" selecting the correct device and/or
* I2C bus.
*
*/
typedef u16_t I2CdevId_t;

/**
* \struct _I2CDeviceAddr_t
* \brief I2C device parameters.
*
* This structure contains the I2C address, the device ID and a userData pointer.
* The userData pointer can be used for application specific purposes.
*
*/
struct _I2CDeviceAddr_t {
   I2Caddr_t  i2cAddr;        /**< The I2C address of the device. */
   I2CdevId_t i2cDevId;       /**< The device identifier. */
   void       *userData;      /**< User data pointer */
};

/**
* \typedef I2CDeviceAddr_t
* \brief I2C device parameters.
*
* This structure contains the I2C address and the device ID.
*
*/
typedef struct _I2CDeviceAddr_t I2CDeviceAddr_t;

/**
* \typedef pI2CDeviceAddr_t
* \brief Pointer to I2C device parameters.
*/
typedef I2CDeviceAddr_t *pI2CDeviceAddr_t;

/*------------------------------------------------------------------------------
DEFINES
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
MACROS
------------------------------------------------------------------------------*/

/**
* \def IS_I2C_10BIT( addr )
* \brief Determine if I2C address 'addr' is a 10 bits address or not.
* \param addr The I2C address.
* \return int.
* \retval 0 if address is not a 10 bits I2C address.
* \retval 1 if address is a 10 bits I2C address.
*/
#define IS_I2C_10BIT(addr) \
	 (((addr) & 0xF8) == 0xF0)

/*------------------------------------------------------------------------------
ENUM
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
STRUCTS
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
Exported FUNCTIONS
------------------------------------------------------------------------------*/


/**
* \fn DRXBSP_I2C_Init()
* \brief Initialize I2C communication module.
* \return DRXStatus_t Return status.
* \retval DRX_STS_OK Initialization successful.
* \retval DRX_STS_ERROR Initialization failed.
*/
DRXStatus_t DRXBSP_I2C_Init( void );


/**
* \fn DRXBSP_I2C_Term()
* \brief Terminate I2C communication module.
* \return DRXStatus_t Return status.
* \retval DRX_STS_OK Termination successful.
* \retval DRX_STS_ERROR Termination failed.
*/
DRXStatus_t DRXBSP_I2C_Term( void );

/**
* \fn DRXStatus_t DRXBSP_I2C_WriteRead( pI2CDeviceAddr_t wDevAddr,
*                                       u16_t wCount,
*                                       pu8_t wData,
*                                       pI2CDeviceAddr_t rDevAddr,
*                                       u16_t rCount,
*                                       pu8_t rData)
* \brief Read and/or write count bytes from I2C bus, store them in data[].
* \param wDevAddr The device i2c address and the device ID to write to
* \param wCount   The number of bytes to write
* \param wData    The array to write the data to
* \param rDevAddr The device i2c address and the device ID to read from
* \param rCount   The number of bytes to read
* \param rData    The array to read the data from
* \return DRXStatus_t Return status.
* \retval DRX_STS_OK Succes.
* \retval DRX_STS_ERROR Failure.
* \retval DRX_STS_INVALID_ARG Parameter 'wcount' is not zero but parameter
*                                       'wdata' contains NULL.
*                                       Idem for 'rcount' and 'rdata'.
*                                       Both wDevAddr and rDevAddr are NULL.
*
* This function must implement an atomic write and/or read action on the I2C bus
* No other process may use the I2C bus when this function is executing.
* The critical section of this function runs from and including the I2C
* write, up to and including the I2C read action.
*
* The device ID can be useful if several devices share an I2C address.
* It can be used to control a "switch" on the I2C bus to the correct device.
*/
DRXStatus_t DRXBSP_I2C_WriteRead(   pI2CDeviceAddr_t wDevAddr,
				    u16_t            wCount,
				    pu8_t            wData,
				    pI2CDeviceAddr_t rDevAddr,
				    u16_t            rCount,
				    pu8_t            rData);


/**
* \fn DRXBSP_I2C_ErrorText()
* \brief Returns a human readable error.
* Counter part of numerical DRX_I2C_Error_g.
*
* \return char* Pointer to human readable error text.
*/
char* DRXBSP_I2C_ErrorText( void );

/**
* \var DRX_I2C_Error_g;
* \brief I2C specific error codes, platform dependent.
*/
extern int DRX_I2C_Error_g;


/*------------------------------------------------------------------------------
THE END
------------------------------------------------------------------------------*/
#ifdef __cplusplus
}
#endif
#endif /* __BSPI2C_H__ */
