/*
 * Copyright (C) 2006-2007 Micronas
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 only, as published by the Free Software Foundation.
 *
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA
 * Or, point your browser to http://www.gnu.org/copyleft/gpl.html
 */

#ifndef _NGENE_IOCTLS_H_
#define _NGENE_IOCTLS_H_

#include <linux/ioctl.h>
#include <linux/types.h>

#define NGENE_MAGIC 'n'

typedef struct {
	unsigned char       I2CAddress;
	unsigned char       OutLength;      /* bytes to write first */
	unsigned char       InLength;       /* bytes to read */
	unsigned char       OutData[256];   /* output data */
	unsigned char       InData[256];    /* input data */
} MIC_I2C_READ, *PMIC_I2C_READ;

#define IOCTL_MIC_I2C_READ          _IOWR(NGENE_MAGIC, 0x00, MIC_I2C_READ)


typedef struct {
	unsigned char       I2CAddress;
	unsigned char       Length;
	unsigned char       Data[250];
} MIC_I2C_WRITE, *PMIC_I2C_WRITE;

typedef struct {
	unsigned char       Length;
	unsigned char       Data[250];
} MIC_I2C_CONTINUE_WRITE, *PMIC_I2C_CONTINUE_WRITE;

#define IOCTL_MIC_I2C_WRITE                   _IOW(NGENE_MAGIC, 0x01, \
						   MIC_I2C_WRITE)
#define IOCTL_MIC_I2C_WRITE_NOSTOP            _IOW(NGENE_MAGIC, 0x0c, \
						   MIC_I2C_WRITE)
#define IOCTL_MIC_I2C_CONTINUE_WRITE_NOSTOP   _IOW(NGENE_MAGIC, 0x0d, \
						   MIC_I2C_CONTINUE_WRITE)
#define IOCTL_MIC_I2C_CONTINUE_WRITE          _IOW(NGENE_MAGIC, 0x0e, \
						   MIC_I2C_CONTINUE_WRITE)

typedef struct {
	unsigned char       ModeSelect;     /* see bellow */
	unsigned char       OutLength;      /* bytes to write first */
	unsigned char       InLength;       /* bytes to read */
	unsigned char       OutData[250];   /* output data */
} MIC_SPI_READ, *PMIC_SPI_READ;

#define IOCTL_MIC_SPI_READ          _IOWR(NGENE_MAGIC, 0x02, MIC_SPI_READ)

typedef struct {
	unsigned char       ModeSelect;     /* see below */
	unsigned char       Length;
	unsigned char       Data[250];
} MIC_SPI_WRITE, *PMIC_SPI_WRITE;

#define IOCTL_MIC_SPI_WRITE         _IOW(NGENE_MAGIC, 0x03, MIC_SPI_READ)

#define IOCTL_MIC_DOWNLOAD_FIRMWARE _IOW(NGENE_MAGIC, 0x06, unsigned char)

#define IOCTL_MIC_NO_OP             _IO(NGENE_MAGIC, 0x18)

#define IOCTL_MIC_TUN_RDY           _IO(NGENE_MAGIC, 0x07)
#define IOCTL_MIC_DEC_SRATE         _IOW(NGENE_MAGIC, 0x0a, int)
#define IOCTL_MIC_DEC_RDY           _IO(NGENE_MAGIC, 0x09)
#define IOCTL_MIC_DEC_FREESYNC      _IOW(NGENE_MAGIC, 0x08, int)
#define IOCTL_MIC_TUN_DETECT        _IOWR(NGENE_MAGIC, 0x0b, int)

typedef struct {
	unsigned char       Stream; /* < UVI1, UVI2, or TVOUT */
	unsigned char       Control;
	unsigned char       Mode;
	unsigned short      nLines;
	unsigned short      nBytesPerLine;
	unsigned short      nVBILines;
	unsigned short      nBytesPerVBILine;
} MIC_STREAM_CONTROL, *PMIC_STREAM_CONTROL;

enum MIC_STREAM_CONTROL_MODE_BITS {
	MSC_MODE_LOOPBACK         = 0x80,
	MSC_MODE_AVLOOP           = 0x40,
	MSC_MODE_AUDIO_SPDIF      = 0x20,
	MSC_MODE_AVSYNC           = 0x10,
	MSC_MODE_TRANSPORT_STREAM = 0x08,
	MSC_MODE_AUDIO_CAPTURE    = 0x04,
	MSC_MODE_VBI_CAPTURE      = 0x02,
	MSC_MODE_VIDEO_CAPTURE    = 0x01
};

#define IOCTL_MIC_STREAM_CONTROL    _IOW(NGENE_MAGIC, 0x22, MIC_STREAM_CONTROL)

typedef struct {
	unsigned char       Stream; /* < UVI1, UVI2 */
	unsigned int        Rate;   /* < Rate in 100nsec to release the buffers
					 to the stream filters */
} MIC_SIMULATE_CONTROL, *PMIC_SIMULATE_CONTROL;

#define IOCTL_MIC_SIMULATE_CONTROL  _IOW(NGENE_MAGIC, 0x23, \
					 MIC_SIMULATE_CONTROL)

/*
 * IOCTL definitions for the test driver
 *
 *   NOTE: the test driver also supports following IOCTL defined above:
 *      IOCTL_MIC_NO_OP:
 *      IOCTL_MIC_RECEIVE_BUFFER:
 *      IOCTL_MIC_STREAM_CONTROL:
 *      IOCTL_MIC_I2C_READ:
 *      IOCTL_MIC_I2C_WRITE:
 *
 *
 *  VI2C access to NGene memory (read)
 *
 *   GETMEM  in  : ULONG start offset
 *           out : read data (length defined by size of output buffer)
 *   SETMEM  in  : ULONG start offset followed by data to be written
 *                 (length defined by size of input buffer)
 */

typedef struct {
	__u32 Start;
	__u32 Length;
	__u8 *Data;
} MIC_MEM;

#define IOCTL_MIC_TEST_GETMEM       _IOWR(NGENE_MAGIC, 0x90, MIC_MEM)
#define IOCTL_MIC_TEST_SETMEM       _IOW(NGENE_MAGIC, 0x91, MIC_MEM)

typedef struct {
	__u8 Address;
	__u8 Data;
} MIC_IMEM;

#define IOCTL_MIC_SFR_READ          _IOWR(NGENE_MAGIC, 0xa2, MIC_IMEM)
#define IOCTL_MIC_SFR_WRITE         _IOWR(NGENE_MAGIC, 0xa3, MIC_IMEM)

#define IOCTL_MIC_IRAM_READ         _IOWR(NGENE_MAGIC, 0xa4, MIC_IMEM)
#define IOCTL_MIC_IRAM_WRITE        _IOWR(NGENE_MAGIC, 0xa5, MIC_IMEM)

/*
 * Set Ngene gpio bit
 */
typedef struct {
	unsigned char   Select;
	unsigned char   Level;
} MIC_SET_GPIO_PIN, *PMIC_SET_GPIO_PIN;

#define IOCTL_MIC_SET_GPIO_PIN      _IOWR(NGENE_MAGIC, 0xa6, MIC_SET_GPIO_PIN)

/*
 * Uart ioctls:
 *   These are implemented in the test driver.
 *
 * Enable UART
 *
 *   In:  1 byte containing baud rate: 0 = 19200, 1 = 9600, 2 = 4800, 3 = 2400
 *   Out: nothing
 */
#define IOCTL_MIC_UART_ENABLE       _IOW(NGENE_MAGIC,  0xa9, unsigned char)

/*
 * Enable UART
 *
 *   In:  nothing
 *   Out: nothing
 */
#define IOCTL_MIC_UART_DISABLE      _IO(NGENE_MAGIC, 0xAA)

/*
 * Write UART
 *
 *   In:  data to write
 *   Out: nothing
 *   Note: Call returns immediatly, data are send out asynchrounsly
 */
#define IOCTL_MIC_UART_WRITE        _IOW(NGENE_MAGIC, 0xAB, unsigned char)

/*
 * Read UART
 *
 *   In:  nothing
 *   Out: Data read (since last call)
 *   Note: Call returns immediatly
 */
#define IOCTL_MIC_UART_READ         _IOR(NGENE_MAGIC, 0xAC, unsigned char)

/*
 * UART Status
 *
 *   In:  nothing
 *   Out: Byte 0 : Transmitter busy,
 *        Byte 1 : Nbr of characters available for read.
 *   Note: Call returns immediatly
 */
#define IOCTL_MIC_UART_STATUS       _IOR(NGENE_MAGIC, 0xAD, unsigned char)

#endif
