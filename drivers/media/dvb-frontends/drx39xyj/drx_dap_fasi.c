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

/*******************************************************************************
* FILENAME: $Id: drx_dap_fasi.c,v 1.7 2009/12/28 14:36:21 carlo Exp $
*
* DESCRIPTION:
* Part of DRX driver.
* Data access protocol: Fast Access Sequential Interface (fasi)
* Fast access, because of short addressing format (16 instead of 32 bits addr)
* Sequential, because of I2C.
* These functions know how the chip's memory and registers are to be accessed,
* but nothing more.
*
* These functions should not need adapting to a new platform.
*
* USAGE:
* -
*
* NOTES:
*
*
*******************************************************************************/

#include "drx_dap_fasi.h"
#include "bsp_host.h"		/* for DRXBSP_HST_Memcpy() */

/*============================================================================*/

/* Function prototypes */
static DRXStatus_t DRXDAP_FASI_WriteBlock(struct i2c_device_addr *devAddr,	/* address of I2C device        */
					  DRXaddr_t addr,	/* address of register/memory   */
					  u16_t datasize,	/* size of data                 */
					  pu8_t data,	/* data to send                 */
					  DRXflags_t flags);	/* special device flags         */

static DRXStatus_t DRXDAP_FASI_ReadBlock(struct i2c_device_addr *devAddr,	/* address of I2C device        */
					 DRXaddr_t addr,	/* address of register/memory   */
					 u16_t datasize,	/* size of data                 */
					 pu8_t data,	/* data to send                 */
					 DRXflags_t flags);	/* special device flags         */

static DRXStatus_t DRXDAP_FASI_WriteReg8(struct i2c_device_addr *devAddr,	/* address of I2C device        */
					 DRXaddr_t addr,	/* address of register          */
					 u8_t data,	/* data to write                */
					 DRXflags_t flags);	/* special device flags         */

static DRXStatus_t DRXDAP_FASI_ReadReg8(struct i2c_device_addr *devAddr,	/* address of I2C device        */
					DRXaddr_t addr,	/* address of register          */
					pu8_t data,	/* buffer to receive data       */
					DRXflags_t flags);	/* special device flags         */

static DRXStatus_t DRXDAP_FASI_ReadModifyWriteReg8(struct i2c_device_addr *devAddr,	/* address of I2C device        */
						   DRXaddr_t waddr,	/* address of register          */
						   DRXaddr_t raddr,	/* address to read back from    */
						   u8_t datain,	/* data to send                 */
						   pu8_t dataout);	/* data to receive back         */

static DRXStatus_t DRXDAP_FASI_WriteReg16(struct i2c_device_addr *devAddr,	/* address of I2C device        */
					  DRXaddr_t addr,	/* address of register          */
					  u16_t data,	/* data to write                */
					  DRXflags_t flags);	/* special device flags         */

static DRXStatus_t DRXDAP_FASI_ReadReg16(struct i2c_device_addr *devAddr,	/* address of I2C device        */
					 DRXaddr_t addr,	/* address of register          */
					 pu16_t data,	/* buffer to receive data       */
					 DRXflags_t flags);	/* special device flags         */

static DRXStatus_t DRXDAP_FASI_ReadModifyWriteReg16(struct i2c_device_addr *devAddr,	/* address of I2C device        */
						    DRXaddr_t waddr,	/* address of register          */
						    DRXaddr_t raddr,	/* address to read back from    */
						    u16_t datain,	/* data to send                 */
						    pu16_t dataout);	/* data to receive back         */

static DRXStatus_t DRXDAP_FASI_WriteReg32(struct i2c_device_addr *devAddr,	/* address of I2C device        */
					  DRXaddr_t addr,	/* address of register          */
					  u32_t data,	/* data to write                */
					  DRXflags_t flags);	/* special device flags         */

static DRXStatus_t DRXDAP_FASI_ReadReg32(struct i2c_device_addr *devAddr,	/* address of I2C device        */
					 DRXaddr_t addr,	/* address of register          */
					 pu32_t data,	/* buffer to receive data       */
					 DRXflags_t flags);	/* special device flags         */

static DRXStatus_t DRXDAP_FASI_ReadModifyWriteReg32(struct i2c_device_addr *devAddr,	/* address of I2C device        */
						    DRXaddr_t waddr,	/* address of register          */
						    DRXaddr_t raddr,	/* address to read back from    */
						    u32_t datain,	/* data to send                 */
						    pu32_t dataout);	/* data to receive back         */

/* The version structure of this protocol implementation */
char drxDapFASIModuleName[] = "FASI Data Access Protocol";
char drxDapFASIVersionText[] = "";

DRXVersion_t drxDapFASIVersion = {
	DRX_MODULE_DAP,	      /**< type identifier of the module */
	drxDapFASIModuleName, /**< name or description of module */

	0,		      /**< major version number */
	0,		      /**< minor version number */
	0,		      /**< patch version number */
	drxDapFASIVersionText /**< version as text string */
};

/* The structure containing the protocol interface */
DRXAccessFunc_t drxDapFASIFunct_g = {
	&drxDapFASIVersion,
	DRXDAP_FASI_WriteBlock,	/* Supported */
	DRXDAP_FASI_ReadBlock,	/* Supported */
	DRXDAP_FASI_WriteReg8,	/* Not supported */
	DRXDAP_FASI_ReadReg8,	/* Not supported */
	DRXDAP_FASI_ReadModifyWriteReg8,	/* Not supported */
	DRXDAP_FASI_WriteReg16,	/* Supported */
	DRXDAP_FASI_ReadReg16,	/* Supported */
	DRXDAP_FASI_ReadModifyWriteReg16,	/* Supported */
	DRXDAP_FASI_WriteReg32,	/* Supported */
	DRXDAP_FASI_ReadReg32,	/* Supported */
	DRXDAP_FASI_ReadModifyWriteReg32	/* Not supported */
};

/*============================================================================*/

/* Functions not supported by protocol*/

static DRXStatus_t DRXDAP_FASI_WriteReg8(struct i2c_device_addr *devAddr,	/* address of I2C device        */
					 DRXaddr_t addr,	/* address of register          */
					 u8_t data,	/* data to write                */
					 DRXflags_t flags)
{				/* special device flags         */
	return DRX_STS_ERROR;
}

static DRXStatus_t DRXDAP_FASI_ReadReg8(struct i2c_device_addr *devAddr,	/* address of I2C device        */
					DRXaddr_t addr,	/* address of register          */
					pu8_t data,	/* buffer to receive data       */
					DRXflags_t flags)
{				/* special device flags         */
	return DRX_STS_ERROR;
}

static DRXStatus_t DRXDAP_FASI_ReadModifyWriteReg8(struct i2c_device_addr *devAddr,	/* address of I2C device        */
						   DRXaddr_t waddr,	/* address of register          */
						   DRXaddr_t raddr,	/* address to read back from    */
						   u8_t datain,	/* data to send                 */
						   pu8_t dataout)
{				/* data to receive back         */
	return DRX_STS_ERROR;
}

static DRXStatus_t DRXDAP_FASI_ReadModifyWriteReg32(struct i2c_device_addr *devAddr,	/* address of I2C device        */
						    DRXaddr_t waddr,	/* address of register          */
						    DRXaddr_t raddr,	/* address to read back from    */
						    u32_t datain,	/* data to send                 */
						    pu32_t dataout)
{				/* data to receive back         */
	return DRX_STS_ERROR;
}

/*============================================================================*/

/******************************
*
* DRXStatus_t DRXDAP_FASI_ReadBlock (
*      struct i2c_device_addr *devAddr,      -- address of I2C device
*      DRXaddr_t        addr,         -- address of chip register/memory
*      u16_t            datasize,     -- number of bytes to read
*      pu8_t            data,         -- data to receive
*      DRXflags_t       flags)        -- special device flags
*
* Read block data from chip address. Because the chip is word oriented,
* the number of bytes to read must be even.
*
* Make sure that the buffer to receive the data is large enough.
*
* Although this function expects an even number of bytes, it is still byte
* oriented, and the data read back is NOT translated to the endianness of
* the target platform.
*
* Output:
* - DRX_STS_OK     if reading was successful
*                  in that case: data read is in *data.
* - DRX_STS_ERROR  if anything went wrong
*
******************************/

static DRXStatus_t DRXDAP_FASI_ReadBlock(struct i2c_device_addr *devAddr,
					 DRXaddr_t addr,
					 u16_t datasize,
					 pu8_t data, DRXflags_t flags)
{
	u8_t buf[4];
	u16_t bufx;
	DRXStatus_t rc;
	u16_t overheadSize = 0;

	/* Check parameters ******************************************************* */
	if (devAddr == NULL) {
		return DRX_STS_INVALID_ARG;
	}

	overheadSize = (IS_I2C_10BIT(devAddr->i2cAddr) ? 2 : 1) +
	    (DRXDAP_FASI_LONG_FORMAT(addr) ? 4 : 2);

	if ((DRXDAP_FASI_OFFSET_TOO_LARGE(addr)) ||
	    ((!(DRXDAPFASI_LONG_ADDR_ALLOWED)) &&
	     DRXDAP_FASI_LONG_FORMAT(addr)) ||
	    (overheadSize > (DRXDAP_MAX_WCHUNKSIZE)) ||
	    ((datasize != 0) && (data == NULL)) || ((datasize & 1) == 1)) {
		return DRX_STS_INVALID_ARG;
	}

	/* ReadModifyWrite & mode flag bits are not allowed */
	flags &= (~DRXDAP_FASI_RMW & ~DRXDAP_FASI_MODEFLAGS);
#if DRXDAP_SINGLE_MASTER
	flags |= DRXDAP_FASI_SINGLE_MASTER;
#endif

	/* Read block from I2C **************************************************** */
	do {
		u16_t todo = (datasize < DRXDAP_MAX_RCHUNKSIZE ?
			      datasize : DRXDAP_MAX_RCHUNKSIZE);

		bufx = 0;

		addr &= ~DRXDAP_FASI_FLAGS;
		addr |= flags;

#if ( ( DRXDAPFASI_LONG_ADDR_ALLOWED==1 ) && \
      ( DRXDAPFASI_SHORT_ADDR_ALLOWED==1 ) )
		/* short format address preferred but long format otherwise */
		if (DRXDAP_FASI_LONG_FORMAT(addr)) {
#endif
#if ( DRXDAPFASI_LONG_ADDR_ALLOWED==1 )
			buf[bufx++] = (u8_t) (((addr << 1) & 0xFF) | 0x01);
			buf[bufx++] = (u8_t) ((addr >> 16) & 0xFF);
			buf[bufx++] = (u8_t) ((addr >> 24) & 0xFF);
			buf[bufx++] = (u8_t) ((addr >> 7) & 0xFF);
#endif
#if ( ( DRXDAPFASI_LONG_ADDR_ALLOWED==1 ) && \
      ( DRXDAPFASI_SHORT_ADDR_ALLOWED==1 ) )
		} else {
#endif
#if ( DRXDAPFASI_SHORT_ADDR_ALLOWED==1 )
			buf[bufx++] = (u8_t) ((addr << 1) & 0xFF);
			buf[bufx++] =
			    (u8_t) (((addr >> 16) & 0x0F) |
				    ((addr >> 18) & 0xF0));
#endif
#if ( ( DRXDAPFASI_LONG_ADDR_ALLOWED==1 ) && \
      ( DRXDAPFASI_SHORT_ADDR_ALLOWED==1 ) )
		}
#endif

#if DRXDAP_SINGLE_MASTER
		/*
		 * In single master mode, split the read and write actions.
		 * No special action is needed for write chunks here.
		 */
		rc = DRXBSP_I2C_WriteRead(devAddr, bufx, buf, 0, 0, 0);
		if (rc == DRX_STS_OK) {
			rc = DRXBSP_I2C_WriteRead(0, 0, 0, devAddr, todo, data);
		}
#else
		/* In multi master mode, do everything in one RW action */
		rc = DRXBSP_I2C_WriteRead(devAddr, bufx, buf, devAddr, todo,
					  data);
#endif
		data += todo;
		addr += (todo >> 1);
		datasize -= todo;
	} while (datasize && rc == DRX_STS_OK);

	return rc;
}

/******************************
*
* DRXStatus_t DRXDAP_FASI_ReadModifyWriteReg16 (
*      struct i2c_device_addr *devAddr,   -- address of I2C device
*      DRXaddr_t        waddr,     -- address of chip register/memory
*      DRXaddr_t        raddr,     -- chip address to read back from
*      u16_t            wdata,     -- data to send
*      pu16_t           rdata)     -- data to receive back
*
* Write 16-bit data, then read back the original contents of that location.
* Requires long addressing format to be allowed.
*
* Before sending data, the data is converted to little endian. The
* data received back is converted back to the target platform's endianness.
*
* WARNING: This function is only guaranteed to work if there is one
* master on the I2C bus.
*
* Output:
* - DRX_STS_OK     if reading was successful
*                  in that case: read back data is at *rdata
* - DRX_STS_ERROR  if anything went wrong
*
******************************/

static DRXStatus_t DRXDAP_FASI_ReadModifyWriteReg16(struct i2c_device_addr *devAddr,
						    DRXaddr_t waddr,
						    DRXaddr_t raddr,
						    u16_t wdata, pu16_t rdata)
{
	DRXStatus_t rc = DRX_STS_ERROR;

#if ( DRXDAPFASI_LONG_ADDR_ALLOWED==1 )
	if (rdata == NULL) {
		return DRX_STS_INVALID_ARG;
	}

	rc = DRXDAP_FASI_WriteReg16(devAddr, waddr, wdata, DRXDAP_FASI_RMW);
	if (rc == DRX_STS_OK) {
		rc = DRXDAP_FASI_ReadReg16(devAddr, raddr, rdata, 0);
	}
#endif

	return rc;
}

/******************************
*
* DRXStatus_t DRXDAP_FASI_ReadReg16 (
*     struct i2c_device_addr *devAddr, -- address of I2C device
*     DRXaddr_t        addr,    -- address of chip register/memory
*     pu16_t           data,    -- data to receive
*     DRXflags_t       flags)   -- special device flags
*
* Read one 16-bit register or memory location. The data received back is
* converted back to the target platform's endianness.
*
* Output:
* - DRX_STS_OK     if reading was successful
*                  in that case: read data is at *data
* - DRX_STS_ERROR  if anything went wrong
*
******************************/

static DRXStatus_t DRXDAP_FASI_ReadReg16(struct i2c_device_addr *devAddr,
					 DRXaddr_t addr,
					 pu16_t data, DRXflags_t flags)
{
	u8_t buf[sizeof(*data)];
	DRXStatus_t rc;

	if (!data) {
		return DRX_STS_INVALID_ARG;
	}
	rc = DRXDAP_FASI_ReadBlock(devAddr, addr, sizeof(*data), buf, flags);
	*data = buf[0] + (((u16_t) buf[1]) << 8);
	return rc;
}

/******************************
*
* DRXStatus_t DRXDAP_FASI_ReadReg32 (
*     struct i2c_device_addr *devAddr, -- address of I2C device
*     DRXaddr_t        addr,    -- address of chip register/memory
*     pu32_t           data,    -- data to receive
*     DRXflags_t       flags)   -- special device flags
*
* Read one 32-bit register or memory location. The data received back is
* converted back to the target platform's endianness.
*
* Output:
* - DRX_STS_OK     if reading was successful
*                  in that case: read data is at *data
* - DRX_STS_ERROR  if anything went wrong
*
******************************/

static DRXStatus_t DRXDAP_FASI_ReadReg32(struct i2c_device_addr *devAddr,
					 DRXaddr_t addr,
					 pu32_t data, DRXflags_t flags)
{
	u8_t buf[sizeof(*data)];
	DRXStatus_t rc;

	if (!data) {
		return DRX_STS_INVALID_ARG;
	}
	rc = DRXDAP_FASI_ReadBlock(devAddr, addr, sizeof(*data), buf, flags);
	*data = (((u32_t) buf[0]) << 0) +
	    (((u32_t) buf[1]) << 8) +
	    (((u32_t) buf[2]) << 16) + (((u32_t) buf[3]) << 24);
	return rc;
}

/******************************
*
* DRXStatus_t DRXDAP_FASI_WriteBlock (
*      struct i2c_device_addr *devAddr,    -- address of I2C device
*      DRXaddr_t        addr,       -- address of chip register/memory
*      u16_t            datasize,   -- number of bytes to read
*      pu8_t            data,       -- data to receive
*      DRXflags_t       flags)      -- special device flags
*
* Write block data to chip address. Because the chip is word oriented,
* the number of bytes to write must be even.
*
* Although this function expects an even number of bytes, it is still byte
* oriented, and the data being written is NOT translated from the endianness of
* the target platform.
*
* Output:
* - DRX_STS_OK     if writing was successful
* - DRX_STS_ERROR  if anything went wrong
*
******************************/

static DRXStatus_t DRXDAP_FASI_WriteBlock(struct i2c_device_addr *devAddr,
					  DRXaddr_t addr,
					  u16_t datasize,
					  pu8_t data, DRXflags_t flags)
{
	u8_t buf[DRXDAP_MAX_WCHUNKSIZE];
	DRXStatus_t st = DRX_STS_ERROR;
	DRXStatus_t firstErr = DRX_STS_OK;
	u16_t overheadSize = 0;
	u16_t blockSize = 0;

	/* Check parameters ******************************************************* */
	if (devAddr == NULL) {
		return DRX_STS_INVALID_ARG;
	}

	overheadSize = (IS_I2C_10BIT(devAddr->i2cAddr) ? 2 : 1) +
	    (DRXDAP_FASI_LONG_FORMAT(addr) ? 4 : 2);

	if ((DRXDAP_FASI_OFFSET_TOO_LARGE(addr)) ||
	    ((!(DRXDAPFASI_LONG_ADDR_ALLOWED)) &&
	     DRXDAP_FASI_LONG_FORMAT(addr)) ||
	    (overheadSize > (DRXDAP_MAX_WCHUNKSIZE)) ||
	    ((datasize != 0) && (data == NULL)) || ((datasize & 1) == 1)) {
		return DRX_STS_INVALID_ARG;
	}

	flags &= DRXDAP_FASI_FLAGS;
	flags &= ~DRXDAP_FASI_MODEFLAGS;
#if DRXDAP_SINGLE_MASTER
	flags |= DRXDAP_FASI_SINGLE_MASTER;
#endif

	/* Write block to I2C ***************************************************** */
	blockSize = ((DRXDAP_MAX_WCHUNKSIZE) - overheadSize) & ~1;
	do {
		u16_t todo = 0;
		u16_t bufx = 0;

		/* Buffer device address */
		addr &= ~DRXDAP_FASI_FLAGS;
		addr |= flags;
#if ( ( (DRXDAPFASI_LONG_ADDR_ALLOWED)==1 ) && \
      ( (DRXDAPFASI_SHORT_ADDR_ALLOWED)==1 ) )
		/* short format address preferred but long format otherwise */
		if (DRXDAP_FASI_LONG_FORMAT(addr)) {
#endif
#if ( (DRXDAPFASI_LONG_ADDR_ALLOWED)==1 )
			buf[bufx++] = (u8_t) (((addr << 1) & 0xFF) | 0x01);
			buf[bufx++] = (u8_t) ((addr >> 16) & 0xFF);
			buf[bufx++] = (u8_t) ((addr >> 24) & 0xFF);
			buf[bufx++] = (u8_t) ((addr >> 7) & 0xFF);
#endif
#if ( ( (DRXDAPFASI_LONG_ADDR_ALLOWED)==1 ) && \
      ( (DRXDAPFASI_SHORT_ADDR_ALLOWED)==1 ) )
		} else {
#endif
#if ( (DRXDAPFASI_SHORT_ADDR_ALLOWED)==1 )
			buf[bufx++] = (u8_t) ((addr << 1) & 0xFF);
			buf[bufx++] =
			    (u8_t) (((addr >> 16) & 0x0F) |
				    ((addr >> 18) & 0xF0));
#endif
#if ( ( (DRXDAPFASI_LONG_ADDR_ALLOWED)==1 ) && \
      ( (DRXDAPFASI_SHORT_ADDR_ALLOWED)==1 ) )
		}
#endif

		/*
		   In single master mode blockSize can be 0. In such a case this I2C
		   sequense will be visible: (1) write address {i2c addr,
		   4 bytes chip address} (2) write data {i2c addr, 4 bytes data }
		   (3) write address (4) write data etc...
		   Addres must be rewriten because HI is reset after data transport and
		   expects an address.
		 */
		todo = (blockSize < datasize ? blockSize : datasize);
		if (todo == 0) {
			u16_t overheadSizeI2cAddr = 0;
			u16_t dataBlockSize = 0;

			overheadSizeI2cAddr =
			    (IS_I2C_10BIT(devAddr->i2cAddr) ? 2 : 1);
			dataBlockSize =
			    (DRXDAP_MAX_WCHUNKSIZE - overheadSizeI2cAddr) & ~1;

			/* write device address */
			st = DRXBSP_I2C_WriteRead(devAddr,
						  (u16_t) (bufx),
						  buf,
						  (struct i2c_device_addr *) (NULL),
						  0, (pu8_t) (NULL));

			if ((st != DRX_STS_OK) && (firstErr == DRX_STS_OK)) {
				/* at the end, return the first error encountered */
				firstErr = st;
			}
			bufx = 0;
			todo =
			    (dataBlockSize <
			     datasize ? dataBlockSize : datasize);
		}
		DRXBSP_HST_Memcpy(&buf[bufx], data, todo);
		/* write (address if can do and) data */
		st = DRXBSP_I2C_WriteRead(devAddr,
					  (u16_t) (bufx + todo),
					  buf,
					  (struct i2c_device_addr *) (NULL),
					  0, (pu8_t) (NULL));

		if ((st != DRX_STS_OK) && (firstErr == DRX_STS_OK)) {
			/* at the end, return the first error encountered */
			firstErr = st;
		}
		datasize -= todo;
		data += todo;
		addr += (todo >> 1);
	} while (datasize);

	return firstErr;
}

/******************************
*
* DRXStatus_t DRXDAP_FASI_WriteReg16 (
*     struct i2c_device_addr *devAddr, -- address of I2C device
*     DRXaddr_t        addr,    -- address of chip register/memory
*     u16_t            data,    -- data to send
*     DRXflags_t       flags)   -- special device flags
*
* Write one 16-bit register or memory location. The data being written is
* converted from the target platform's endianness to little endian.
*
* Output:
* - DRX_STS_OK     if writing was successful
* - DRX_STS_ERROR  if anything went wrong
*
******************************/

static DRXStatus_t DRXDAP_FASI_WriteReg16(struct i2c_device_addr *devAddr,
					  DRXaddr_t addr,
					  u16_t data, DRXflags_t flags)
{
	u8_t buf[sizeof(data)];

	buf[0] = (u8_t) ((data >> 0) & 0xFF);
	buf[1] = (u8_t) ((data >> 8) & 0xFF);

	return DRXDAP_FASI_WriteBlock(devAddr, addr, sizeof(data), buf, flags);
}

/******************************
*
* DRXStatus_t DRXDAP_FASI_WriteReg32 (
*     struct i2c_device_addr *devAddr, -- address of I2C device
*     DRXaddr_t        addr,    -- address of chip register/memory
*     u32_t            data,    -- data to send
*     DRXflags_t       flags)   -- special device flags
*
* Write one 32-bit register or memory location. The data being written is
* converted from the target platform's endianness to little endian.
*
* Output:
* - DRX_STS_OK     if writing was successful
* - DRX_STS_ERROR  if anything went wrong
*
******************************/

static DRXStatus_t DRXDAP_FASI_WriteReg32(struct i2c_device_addr *devAddr,
					  DRXaddr_t addr,
					  u32_t data, DRXflags_t flags)
{
	u8_t buf[sizeof(data)];

	buf[0] = (u8_t) ((data >> 0) & 0xFF);
	buf[1] = (u8_t) ((data >> 8) & 0xFF);
	buf[2] = (u8_t) ((data >> 16) & 0xFF);
	buf[3] = (u8_t) ((data >> 24) & 0xFF);

	return DRXDAP_FASI_WriteBlock(devAddr, addr, sizeof(data), buf, flags);
}
