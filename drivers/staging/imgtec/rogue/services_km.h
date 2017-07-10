/*************************************************************************/ /*!
@File
@Title          Services API Kernel mode Header
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Exported services API details
@License        Dual MIT/GPLv2

The contents of this file are subject to the MIT license as set out below.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

Alternatively, the contents of this file may be used under the terms of
the GNU General Public License Version 2 ("GPL") in which case the provisions
of GPL are applicable instead of those above.

If you wish to allow use of your version of this file only under the terms of
GPL, and not to allow others to use your version of this file under the terms
of the MIT license, indicate your decision by deleting the provisions above
and replace them with the notice and other provisions required by GPL as set
out in the file called "GPL-COPYING" included in this distribution. If you do
not delete the provisions above, a recipient may use your version of this file
under the terms of either the MIT license or GPL.

This License is also included in this distribution in the file called
"MIT-COPYING".

EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/ /**************************************************************************/

#ifndef SERVICES_KM_H
#define SERVICES_KM_H

#if defined(SUPPORT_GPUVIRT_VALIDATION)
#include "virt_validation_defs.h"
#endif

/*! 4k page size definition */
#define PVRSRV_4K_PAGE_SIZE					4096UL      /*!< Size of a 4K Page */
#define PVRSRV_4K_PAGE_SIZE_ALIGNSHIFT		12          /*!< Amount to shift an address by so that
                                                          it is always page-aligned */
/*! 16k page size definition */
#define PVRSRV_16K_PAGE_SIZE					16384UL      /*!< Size of a 16K Page */
#define PVRSRV_16K_PAGE_SIZE_ALIGNSHIFT		14          /*!< Amount to shift an address by so that
                                                          it is always page-aligned */
/*! 64k page size definition */
#define PVRSRV_64K_PAGE_SIZE					65536UL      /*!< Size of a 64K Page */
#define PVRSRV_64K_PAGE_SIZE_ALIGNSHIFT		16          /*!< Amount to shift an address by so that
                                                          it is always page-aligned */
/*! 256k page size definition */
#define PVRSRV_256K_PAGE_SIZE					262144UL      /*!< Size of a 256K Page */
#define PVRSRV_256K_PAGE_SIZE_ALIGNSHIFT		18          /*!< Amount to shift an address by so that
                                                          it is always page-aligned */
/*! 1MB page size definition */
#define PVRSRV_1M_PAGE_SIZE					1048576UL      /*!< Size of a 1M Page */
#define PVRSRV_1M_PAGE_SIZE_ALIGNSHIFT		20          /*!< Amount to shift an address by so that
                                                          it is always page-aligned */
/*! 2MB page size definition */
#define PVRSRV_2M_PAGE_SIZE					2097152UL      /*!< Size of a 2M Page */
#define PVRSRV_2M_PAGE_SIZE_ALIGNSHIFT		21          /*!< Amount to shift an address by so that
                                                          it is always page-aligned */

/*!
 * Forward declaration (look on connection.h)
 */
typedef struct _PVRSRV_DEV_CONNECTION_ PVRSRV_DEV_CONNECTION;

/*!
	Flags for Services connection.
	Allows to define per-client policy for Services
*/
#define SRV_FLAGS_INIT_PROCESS          (1U << 1)  /*!< Allows connect to succeed if SrvInit
											* has not yet run (used by SrvInit itself) */

#define SRV_WORKEST_ENABLED             (1U << 2)  /*!< If Workload Estimation is enabled */
#define SRV_PDVFS_ENABLED               (1U << 3)  /*!< If PDVFS is enabled */

#define SRV_NO_HWPERF_CLIENT_STREAM     (1U << 4)  /*!< Don't create HWPerf for this connection */

/*
 * Bits 20 - 27 are used to pass information needed for validation
 * of the GPU Virtualisation Validation mechanism. In particular:
 *
 * Bits:
 * [20 - 22]: OSid of the memory region that will be used for allocations
 * [23 - 25]: OSid that will be emitted by the Firmware for all memory accesses
 *            regarding that memory context.
 *      [26]: If the AXI Protection register will be set to secure for that OSid
 *      [27]: If the Emulator Wrapper Register checking for protection violation
 *            will be set to secure for that OSid
 */

#define VIRTVAL_FLAG_OSID_SHIFT        (20)
#define SRV_VIRTVAL_FLAG_OSID_MASK     (7U << VIRTVAL_FLAG_OSID_SHIFT)

#define VIRTVAL_FLAG_OSIDREG_SHIFT     (23)
#define SRV_VIRTVAL_FLAG_OSIDREG_MASK  (7U << VIRTVAL_FLAG_OSIDREG_SHIFT)

#define VIRTVAL_FLAG_AXIPREG_SHIFT     (26)
#define SRV_VIRTVAL_FLAG_AXIPREG_MASK  (1U << VIRTVAL_FLAG_AXIPREG_SHIFT)

#define VIRTVAL_FLAG_AXIPTD_SHIFT      (27)
#define SRV_VIRTVAL_FLAG_AXIPTD_MASK   (1U << VIRTVAL_FLAG_AXIPTD_SHIFT)

#define SRV_FLAGS_PDUMPCTRL             (1U << 31) /*!< PDump Ctrl client flag */

/*
    Pdump flags which are accessible to Services clients
*/
#define PDUMP_NONE		0x00000000UL /*<! No flags */

#define PDUMP_CONT	0x40000000UL /*<! Output this entry always regardless of framed capture range,
                                                          used by client applications being dumped. */

/* Status of the device. */
typedef enum
{
	PVRSRV_DEVICE_STATUS_UNKNOWN,        /* status of the device is unknown */
	PVRSRV_DEVICE_STATUS_OK,             /* the device is operational */
	PVRSRV_DEVICE_STATUS_NOT_RESPONDING, /* the device is not responding */
	PVRSRV_DEVICE_STATUS_DEVICE_ERROR    /* the device is not operational */
} PVRSRV_DEVICE_STATUS;

#endif /* SERVICES_KM_H */
/**************************************************************************//**
End of file (services_km.h)
******************************************************************************/
