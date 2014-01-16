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
#include "drx_driver.h"		/* for drxbsp_hst_memcpy() */

/*============================================================================*/

/* Function prototypes */
static int drxdap_fasi_write_block(struct i2c_device_addr *dev_addr,	/* address of I2C device        */
					  dr_xaddr_t addr,	/* address of register/memory   */
					  u16 datasize,	/* size of data                 */
					  u8 *data,	/* data to send                 */
					  dr_xflags_t flags);	/* special device flags         */

static int drxdap_fasi_read_block(struct i2c_device_addr *dev_addr,	/* address of I2C device        */
					 dr_xaddr_t addr,	/* address of register/memory   */
					 u16 datasize,	/* size of data                 */
					 u8 *data,	/* data to send                 */
					 dr_xflags_t flags);	/* special device flags         */

static int drxdap_fasi_write_reg8(struct i2c_device_addr *dev_addr,	/* address of I2C device        */
					 dr_xaddr_t addr,	/* address of register          */
					 u8 data,	/* data to write                */
					 dr_xflags_t flags);	/* special device flags         */

static int drxdap_fasi_read_reg8(struct i2c_device_addr *dev_addr,	/* address of I2C device        */
					dr_xaddr_t addr,	/* address of register          */
					u8 *data,	/* buffer to receive data       */
					dr_xflags_t flags);	/* special device flags         */

static int drxdap_fasi_read_modify_write_reg8(struct i2c_device_addr *dev_addr,	/* address of I2C device        */
						   dr_xaddr_t waddr,	/* address of register          */
						   dr_xaddr_t raddr,	/* address to read back from    */
						   u8 datain,	/* data to send                 */
						   u8 *dataout);	/* data to receive back         */

static int drxdap_fasi_write_reg16(struct i2c_device_addr *dev_addr,	/* address of I2C device        */
					  dr_xaddr_t addr,	/* address of register          */
					  u16 data,	/* data to write                */
					  dr_xflags_t flags);	/* special device flags         */

static int drxdap_fasi_read_reg16(struct i2c_device_addr *dev_addr,	/* address of I2C device        */
					 dr_xaddr_t addr,	/* address of register          */
					 u16 *data,	/* buffer to receive data       */
					 dr_xflags_t flags);	/* special device flags         */

static int drxdap_fasi_read_modify_write_reg16(struct i2c_device_addr *dev_addr,	/* address of I2C device        */
						    dr_xaddr_t waddr,	/* address of register          */
						    dr_xaddr_t raddr,	/* address to read back from    */
						    u16 datain,	/* data to send                 */
						    u16 *dataout);	/* data to receive back         */

static int drxdap_fasi_write_reg32(struct i2c_device_addr *dev_addr,	/* address of I2C device        */
					  dr_xaddr_t addr,	/* address of register          */
					  u32 data,	/* data to write                */
					  dr_xflags_t flags);	/* special device flags         */

static int drxdap_fasi_read_reg32(struct i2c_device_addr *dev_addr,	/* address of I2C device        */
					 dr_xaddr_t addr,	/* address of register          */
					 u32 *data,	/* buffer to receive data       */
					 dr_xflags_t flags);	/* special device flags         */

static int drxdap_fasi_read_modify_write_reg32(struct i2c_device_addr *dev_addr,	/* address of I2C device        */
						    dr_xaddr_t waddr,	/* address of register          */
						    dr_xaddr_t raddr,	/* address to read back from    */
						    u32 datain,	/* data to send                 */
						    u32 *dataout);	/* data to receive back         */

/* The version structure of this protocol implementation */
char drx_dap_fasi_module_name[] = "FASI Data Access Protocol";
char drx_dap_fasi_version_text[] = "";

drx_version_t drx_dap_fasi_version = {
	DRX_MODULE_DAP,	      /**< type identifier of the module */
	drx_dap_fasi_module_name, /**< name or description of module */

	0,		      /**< major version number */
	0,		      /**< minor version number */
	0,		      /**< patch version number */
	drx_dap_fasi_version_text /**< version as text string */
};

/* The structure containing the protocol interface */
drx_access_func_t drx_dap_fasi_funct_g = {
	&drx_dap_fasi_version,
	drxdap_fasi_write_block,	/* Supported */
	drxdap_fasi_read_block,	/* Supported */
	drxdap_fasi_write_reg8,	/* Not supported */
	drxdap_fasi_read_reg8,	/* Not supported */
	drxdap_fasi_read_modify_write_reg8,	/* Not supported */
	drxdap_fasi_write_reg16,	/* Supported */
	drxdap_fasi_read_reg16,	/* Supported */
	drxdap_fasi_read_modify_write_reg16,	/* Supported */
	drxdap_fasi_write_reg32,	/* Supported */
	drxdap_fasi_read_reg32,	/* Supported */
	drxdap_fasi_read_modify_write_reg32	/* Not supported */
};

/*============================================================================*/

/* Functions not supported by protocol*/

static int drxdap_fasi_write_reg8(struct i2c_device_addr *dev_addr,	/* address of I2C device        */
					 dr_xaddr_t addr,	/* address of register          */
					 u8 data,	/* data to write                */
					 dr_xflags_t flags)
{				/* special device flags         */
	return DRX_STS_ERROR;
}

static int drxdap_fasi_read_reg8(struct i2c_device_addr *dev_addr,	/* address of I2C device        */
					dr_xaddr_t addr,	/* address of register          */
					u8 *data,	/* buffer to receive data       */
					dr_xflags_t flags)
{				/* special device flags         */
	return DRX_STS_ERROR;
}

static int drxdap_fasi_read_modify_write_reg8(struct i2c_device_addr *dev_addr,	/* address of I2C device        */
						   dr_xaddr_t waddr,	/* address of register          */
						   dr_xaddr_t raddr,	/* address to read back from    */
						   u8 datain,	/* data to send                 */
						   u8 *dataout)
{				/* data to receive back         */
	return DRX_STS_ERROR;
}

static int drxdap_fasi_read_modify_write_reg32(struct i2c_device_addr *dev_addr,	/* address of I2C device        */
						    dr_xaddr_t waddr,	/* address of register          */
						    dr_xaddr_t raddr,	/* address to read back from    */
						    u32 datain,	/* data to send                 */
						    u32 *dataout)
{				/* data to receive back         */
	return DRX_STS_ERROR;
}

/*============================================================================*/

/******************************
*
* int drxdap_fasi_read_block (
*      struct i2c_device_addr *dev_addr,      -- address of I2C device
*      dr_xaddr_t        addr,         -- address of chip register/memory
*      u16            datasize,     -- number of bytes to read
*      u8 *data,         -- data to receive
*      dr_xflags_t       flags)        -- special device flags
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

static int drxdap_fasi_read_block(struct i2c_device_addr *dev_addr,
					 dr_xaddr_t addr,
					 u16 datasize,
					 u8 *data, dr_xflags_t flags)
{
	u8 buf[4];
	u16 bufx;
	int rc;
	u16 overhead_size = 0;

	/* Check parameters ******************************************************* */
	if (dev_addr == NULL) {
		return DRX_STS_INVALID_ARG;
	}

	overhead_size = (IS_I2C_10BIT(dev_addr->i2c_addr) ? 2 : 1) +
	    (DRXDAP_FASI_LONG_FORMAT(addr) ? 4 : 2);

	if ((DRXDAP_FASI_OFFSET_TOO_LARGE(addr)) ||
	    ((!(DRXDAPFASI_LONG_ADDR_ALLOWED)) &&
	     DRXDAP_FASI_LONG_FORMAT(addr)) ||
	    (overhead_size > (DRXDAP_MAX_WCHUNKSIZE)) ||
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
		u16 todo = (datasize < DRXDAP_MAX_RCHUNKSIZE ?
			      datasize : DRXDAP_MAX_RCHUNKSIZE);

		bufx = 0;

		addr &= ~DRXDAP_FASI_FLAGS;
		addr |= flags;

#if ((DRXDAPFASI_LONG_ADDR_ALLOWED == 1) && \
      (DRXDAPFASI_SHORT_ADDR_ALLOWED == 1))
		/* short format address preferred but long format otherwise */
		if (DRXDAP_FASI_LONG_FORMAT(addr)) {
#endif
#if (DRXDAPFASI_LONG_ADDR_ALLOWED == 1)
			buf[bufx++] = (u8) (((addr << 1) & 0xFF) | 0x01);
			buf[bufx++] = (u8) ((addr >> 16) & 0xFF);
			buf[bufx++] = (u8) ((addr >> 24) & 0xFF);
			buf[bufx++] = (u8) ((addr >> 7) & 0xFF);
#endif
#if ((DRXDAPFASI_LONG_ADDR_ALLOWED == 1) && \
      (DRXDAPFASI_SHORT_ADDR_ALLOWED == 1))
		} else {
#endif
#if (DRXDAPFASI_SHORT_ADDR_ALLOWED == 1)
			buf[bufx++] = (u8) ((addr << 1) & 0xFF);
			buf[bufx++] =
			    (u8) (((addr >> 16) & 0x0F) |
				    ((addr >> 18) & 0xF0));
#endif
#if ((DRXDAPFASI_LONG_ADDR_ALLOWED == 1) && \
      (DRXDAPFASI_SHORT_ADDR_ALLOWED == 1))
		}
#endif

#if DRXDAP_SINGLE_MASTER
		/*
		 * In single master mode, split the read and write actions.
		 * No special action is needed for write chunks here.
		 */
		rc = drxbsp_i2c_write_read(dev_addr, bufx, buf, 0, 0, 0);
		if (rc == DRX_STS_OK) {
			rc = drxbsp_i2c_write_read(0, 0, 0, dev_addr, todo, data);
		}
#else
		/* In multi master mode, do everything in one RW action */
		rc = drxbsp_i2c_write_read(dev_addr, bufx, buf, dev_addr, todo,
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
* int drxdap_fasi_read_modify_write_reg16 (
*      struct i2c_device_addr *dev_addr,   -- address of I2C device
*      dr_xaddr_t        waddr,     -- address of chip register/memory
*      dr_xaddr_t        raddr,     -- chip address to read back from
*      u16            wdata,     -- data to send
*      u16 *rdata)     -- data to receive back
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

static int drxdap_fasi_read_modify_write_reg16(struct i2c_device_addr *dev_addr,
						    dr_xaddr_t waddr,
						    dr_xaddr_t raddr,
						    u16 wdata, u16 *rdata)
{
	int rc = DRX_STS_ERROR;

#if (DRXDAPFASI_LONG_ADDR_ALLOWED == 1)
	if (rdata == NULL) {
		return DRX_STS_INVALID_ARG;
	}

	rc = drxdap_fasi_write_reg16(dev_addr, waddr, wdata, DRXDAP_FASI_RMW);
	if (rc == DRX_STS_OK) {
		rc = drxdap_fasi_read_reg16(dev_addr, raddr, rdata, 0);
	}
#endif

	return rc;
}

/******************************
*
* int drxdap_fasi_read_reg16 (
*     struct i2c_device_addr *dev_addr, -- address of I2C device
*     dr_xaddr_t        addr,    -- address of chip register/memory
*     u16 *data,    -- data to receive
*     dr_xflags_t       flags)   -- special device flags
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

static int drxdap_fasi_read_reg16(struct i2c_device_addr *dev_addr,
					 dr_xaddr_t addr,
					 u16 *data, dr_xflags_t flags)
{
	u8 buf[sizeof(*data)];
	int rc;

	if (!data) {
		return DRX_STS_INVALID_ARG;
	}
	rc = drxdap_fasi_read_block(dev_addr, addr, sizeof(*data), buf, flags);
	*data = buf[0] + (((u16) buf[1]) << 8);
	return rc;
}

/******************************
*
* int drxdap_fasi_read_reg32 (
*     struct i2c_device_addr *dev_addr, -- address of I2C device
*     dr_xaddr_t        addr,    -- address of chip register/memory
*     u32 *data,    -- data to receive
*     dr_xflags_t       flags)   -- special device flags
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

static int drxdap_fasi_read_reg32(struct i2c_device_addr *dev_addr,
					 dr_xaddr_t addr,
					 u32 *data, dr_xflags_t flags)
{
	u8 buf[sizeof(*data)];
	int rc;

	if (!data) {
		return DRX_STS_INVALID_ARG;
	}
	rc = drxdap_fasi_read_block(dev_addr, addr, sizeof(*data), buf, flags);
	*data = (((u32) buf[0]) << 0) +
	    (((u32) buf[1]) << 8) +
	    (((u32) buf[2]) << 16) + (((u32) buf[3]) << 24);
	return rc;
}

/******************************
*
* int drxdap_fasi_write_block (
*      struct i2c_device_addr *dev_addr,    -- address of I2C device
*      dr_xaddr_t        addr,       -- address of chip register/memory
*      u16            datasize,   -- number of bytes to read
*      u8 *data,       -- data to receive
*      dr_xflags_t       flags)      -- special device flags
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

static int drxdap_fasi_write_block(struct i2c_device_addr *dev_addr,
					  dr_xaddr_t addr,
					  u16 datasize,
					  u8 *data, dr_xflags_t flags)
{
	u8 buf[DRXDAP_MAX_WCHUNKSIZE];
	int st = DRX_STS_ERROR;
	int first_err = DRX_STS_OK;
	u16 overhead_size = 0;
	u16 block_size = 0;

	/* Check parameters ******************************************************* */
	if (dev_addr == NULL) {
		return DRX_STS_INVALID_ARG;
	}

	overhead_size = (IS_I2C_10BIT(dev_addr->i2c_addr) ? 2 : 1) +
	    (DRXDAP_FASI_LONG_FORMAT(addr) ? 4 : 2);

	if ((DRXDAP_FASI_OFFSET_TOO_LARGE(addr)) ||
	    ((!(DRXDAPFASI_LONG_ADDR_ALLOWED)) &&
	     DRXDAP_FASI_LONG_FORMAT(addr)) ||
	    (overhead_size > (DRXDAP_MAX_WCHUNKSIZE)) ||
	    ((datasize != 0) && (data == NULL)) || ((datasize & 1) == 1)) {
		return DRX_STS_INVALID_ARG;
	}

	flags &= DRXDAP_FASI_FLAGS;
	flags &= ~DRXDAP_FASI_MODEFLAGS;
#if DRXDAP_SINGLE_MASTER
	flags |= DRXDAP_FASI_SINGLE_MASTER;
#endif

	/* Write block to I2C ***************************************************** */
	block_size = ((DRXDAP_MAX_WCHUNKSIZE) - overhead_size) & ~1;
	do {
		u16 todo = 0;
		u16 bufx = 0;

		/* Buffer device address */
		addr &= ~DRXDAP_FASI_FLAGS;
		addr |= flags;
#if (((DRXDAPFASI_LONG_ADDR_ALLOWED) == 1) && \
      ((DRXDAPFASI_SHORT_ADDR_ALLOWED) == 1))
		/* short format address preferred but long format otherwise */
		if (DRXDAP_FASI_LONG_FORMAT(addr)) {
#endif
#if ((DRXDAPFASI_LONG_ADDR_ALLOWED) == 1)
			buf[bufx++] = (u8) (((addr << 1) & 0xFF) | 0x01);
			buf[bufx++] = (u8) ((addr >> 16) & 0xFF);
			buf[bufx++] = (u8) ((addr >> 24) & 0xFF);
			buf[bufx++] = (u8) ((addr >> 7) & 0xFF);
#endif
#if (((DRXDAPFASI_LONG_ADDR_ALLOWED) == 1) && \
      ((DRXDAPFASI_SHORT_ADDR_ALLOWED) == 1))
		} else {
#endif
#if ((DRXDAPFASI_SHORT_ADDR_ALLOWED) == 1)
			buf[bufx++] = (u8) ((addr << 1) & 0xFF);
			buf[bufx++] =
			    (u8) (((addr >> 16) & 0x0F) |
				    ((addr >> 18) & 0xF0));
#endif
#if (((DRXDAPFASI_LONG_ADDR_ALLOWED) == 1) && \
      ((DRXDAPFASI_SHORT_ADDR_ALLOWED) == 1))
		}
#endif

		/*
		   In single master mode block_size can be 0. In such a case this I2C
		   sequense will be visible: (1) write address {i2c addr,
		   4 bytes chip address} (2) write data {i2c addr, 4 bytes data }
		   (3) write address (4) write data etc...
		   Addres must be rewriten because HI is reset after data transport and
		   expects an address.
		 */
		todo = (block_size < datasize ? block_size : datasize);
		if (todo == 0) {
			u16 overhead_sizeI2cAddr = 0;
			u16 data_block_size = 0;

			overhead_sizeI2cAddr =
			    (IS_I2C_10BIT(dev_addr->i2c_addr) ? 2 : 1);
			data_block_size =
			    (DRXDAP_MAX_WCHUNKSIZE - overhead_sizeI2cAddr) & ~1;

			/* write device address */
			st = drxbsp_i2c_write_read(dev_addr,
						  (u16) (bufx),
						  buf,
						  (struct i2c_device_addr *)(NULL),
						  0, (u8 *)(NULL));

			if ((st != DRX_STS_OK) && (first_err == DRX_STS_OK)) {
				/* at the end, return the first error encountered */
				first_err = st;
			}
			bufx = 0;
			todo =
			    (data_block_size <
			     datasize ? data_block_size : datasize);
		}
		drxbsp_hst_memcpy(&buf[bufx], data, todo);
		/* write (address if can do and) data */
		st = drxbsp_i2c_write_read(dev_addr,
					  (u16) (bufx + todo),
					  buf,
					  (struct i2c_device_addr *)(NULL),
					  0, (u8 *)(NULL));

		if ((st != DRX_STS_OK) && (first_err == DRX_STS_OK)) {
			/* at the end, return the first error encountered */
			first_err = st;
		}
		datasize -= todo;
		data += todo;
		addr += (todo >> 1);
	} while (datasize);

	return first_err;
}

/******************************
*
* int drxdap_fasi_write_reg16 (
*     struct i2c_device_addr *dev_addr, -- address of I2C device
*     dr_xaddr_t        addr,    -- address of chip register/memory
*     u16            data,    -- data to send
*     dr_xflags_t       flags)   -- special device flags
*
* Write one 16-bit register or memory location. The data being written is
* converted from the target platform's endianness to little endian.
*
* Output:
* - DRX_STS_OK     if writing was successful
* - DRX_STS_ERROR  if anything went wrong
*
******************************/

static int drxdap_fasi_write_reg16(struct i2c_device_addr *dev_addr,
					  dr_xaddr_t addr,
					  u16 data, dr_xflags_t flags)
{
	u8 buf[sizeof(data)];

	buf[0] = (u8) ((data >> 0) & 0xFF);
	buf[1] = (u8) ((data >> 8) & 0xFF);

	return drxdap_fasi_write_block(dev_addr, addr, sizeof(data), buf, flags);
}

/******************************
*
* int drxdap_fasi_write_reg32 (
*     struct i2c_device_addr *dev_addr, -- address of I2C device
*     dr_xaddr_t        addr,    -- address of chip register/memory
*     u32            data,    -- data to send
*     dr_xflags_t       flags)   -- special device flags
*
* Write one 32-bit register or memory location. The data being written is
* converted from the target platform's endianness to little endian.
*
* Output:
* - DRX_STS_OK     if writing was successful
* - DRX_STS_ERROR  if anything went wrong
*
******************************/

static int drxdap_fasi_write_reg32(struct i2c_device_addr *dev_addr,
					  dr_xaddr_t addr,
					  u32 data, dr_xflags_t flags)
{
	u8 buf[sizeof(data)];

	buf[0] = (u8) ((data >> 0) & 0xFF);
	buf[1] = (u8) ((data >> 8) & 0xFF);
	buf[2] = (u8) ((data >> 16) & 0xFF);
	buf[3] = (u8) ((data >> 24) & 0xFF);

	return drxdap_fasi_write_block(dev_addr, addr, sizeof(data), buf, flags);
}
