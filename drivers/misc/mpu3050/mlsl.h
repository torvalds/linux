/*
 $License:
    Copyright (C) 2010 InvenSense Corporation, All Rights Reserved.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
  $
 */

#ifndef __MSSL_H__
#define __MSSL_H__

#include "mltypes.h"
#include "mpu.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------ */
/* - Defines. - */
/* ------------ */

/*
 * NOTE : to properly support Yamaha compass reads,
 * the max transfer size should be at least 9 B.
 * Length in bytes, typically a power of 2 >= 2
 */
#define SERIAL_MAX_TRANSFER_SIZE 128

/* ---------------------- */
/* - Types definitions. - */
/* ---------------------- */

/* --------------------- */
/* - Function p-types. - */
/* --------------------- */

	tMLError MLSLSerialOpen(char const *port,
				void **sl_handle);
	tMLError MLSLSerialReset(void *sl_handle);
	tMLError MLSLSerialClose(void *sl_handle);

	tMLError MLSLSerialWriteSingle(void *sl_handle,
				       unsigned char slaveAddr,
				       unsigned char registerAddr,
				       unsigned char data);

	tMLError MLSLSerialRead(void *sl_handle,
				unsigned char slaveAddr,
				unsigned char registerAddr,
				unsigned short length,
				unsigned char *data);

	tMLError MLSLSerialWrite(void *sl_handle,
				 unsigned char slaveAddr,
				 unsigned short length,
				 unsigned char const *data);

	tMLError MLSLSerialReadMem(void *sl_handle,
				   unsigned char slaveAddr,
				   unsigned short memAddr,
				   unsigned short length,
				   unsigned char *data);

	tMLError MLSLSerialWriteMem(void *sl_handle,
				    unsigned char slaveAddr,
				    unsigned short memAddr,
				    unsigned short length,
				    unsigned char const *data);

	tMLError MLSLSerialReadFifo(void *sl_handle,
				    unsigned char slaveAddr,
				    unsigned short length,
				    unsigned char *data);

	tMLError MLSLSerialWriteFifo(void *sl_handle,
				     unsigned char slaveAddr,
				     unsigned short length,
				     unsigned char const *data);

	tMLError MLSLWriteCal(unsigned char *cal, unsigned int len);
	tMLError MLSLReadCal(unsigned char *cal, unsigned int len);
	tMLError MLSLGetCalLength(unsigned int *len);

#ifdef __cplusplus
}
#endif

/**
 * @}
 */
#endif				/* MLSL_H */
