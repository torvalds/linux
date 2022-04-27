/*************************************************************************/ /*!
@File
@Title          Services RGX OS Interface for loading the firmware
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    This file defines the OS interface through which the RGX
                device initialisation code in the kernel/server will obtain
                the RGX firmware binary image. The API is used during the
                initialisation of an RGX device via the
                PVRSRVCommonDeviceInitialise()
                call sequence.
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

#ifndef FWLOAD_H
#define FWLOAD_H

#include "img_defs.h"
#include "device_connection.h"
#include "device.h"

/*! Opaque type handle defined and known to the OS layer implementation of this
 * fwload.h OS API. This private data is allocated in the implementation of
 * OSLoadFirmware() and contains whatever data and information needed to be
 * able to acquire and return the firmware binary image to the Services
 * kernel/server during initialisation.
 * It is no longer required and may be freed when OSUnloadFirmware() is called.
 */
typedef struct OS_FW_IMAGE_t OS_FW_IMAGE;

#if defined(__linux__)

bool OSVerifyFirmware(OS_FW_IMAGE* psFWImage);

#endif

/*************************************************************************/ /*!
@Function     OSLoadFirmware
@Description  The OS implementation must load or acquire the firmware (FW)
              image binary needed by the driver stack.
              A handle to the common layer device node is given to identify
              which device instance in the system is being initialised. The
              BVNC string is also supplied so that the implementation knows
              which FW image to retrieve since each FW image only supports one
              GPU type/revision.
              The calling server code supports multiple GPU types and revisions
              and will detect the specific GPU type and revision before calling
              this API. It will also have runtime configuration of the VZ mode,
              hence this API must be able to retrieve different FW binary
              images based on the pszBVNCString given. The purpose of the end
              platform/system is key to understand which FW images must be
              available to the kernel server.
              On exit the implementation must return a pointer to some private
              data it uses to hold the FW image information and data. It will
              be passed onto later API calls by the kernel server code.
              NULL should be returned if the FW image could not be retrieved.
              The format of the BVNC string is as follows ([x] denotes
              optional field):
                "rgx.fw[.signed].B.V[p].N.C[.vz]"
              The implementation must first try to load the FW identified
              by the pszBVpNCString parameter. If this is not available then it
              should drop back to retrieving the FW identified by the
              pszBVNCString parameter. The fields in the string are:
                B, V, N, C are all unsigned integer identifying type/revision.
                [.signed] is present when RGX_FW_SIGNED=1 is defined in the
                  server build.
                [p] denotes a provisional (pre-silicon) GPU configuration.
                [.vz] is present when the kernel server is loaded on the HOST
                  of a virtualised platform. See the DriverMode server
                  AppHint for details.

@Input        psDeviceNode       Device instance identifier.
@Input        pszBVNCString      Identifier string of the FW image to
                                 be loaded/acquired in production driver.
@Input        pfnVerifyFirmware  Callback which checks validity of FW image.
@Output       ppsFWImage         Ptr to private data on success,
                                 NULL otherwise.
@Return       PVRSRV_ERROR       PVRSRV_OK on success,
                                 PVRSRV_ERROR_NOT_READY if filesystem is not
                                                        ready/initialised,
                                 PVRSRV_ERROR_NOT_FOUND if no suitable FW
                                                        image could be found
                                 PVRSRV_ERROR_OUT_OF_MEMORY if unable to alloc
                                                        memory for FW image
                                 PVRSRV_ERROR_NOT_AUTHENTICATED if FW image
                                                        cannot be verified.
*/ /**************************************************************************/
PVRSRV_ERROR OSLoadFirmware(PVRSRV_DEVICE_NODE *psDeviceNode,
                            const IMG_CHAR *pszBVNCString,
                            bool (*pfnVerifyFirmware)(OS_FW_IMAGE*),
                            OS_FW_IMAGE **ppsFWImage);

/*************************************************************************/ /*!
@Function     OSFirmwareData
@Description  This function returns a pointer to the start of the FW image
              binary data held in memory. It must remain valid until
              OSUnloadFirmware() is called.
@Input        psFWImage  Private data opaque handle
@Return       void*      Ptr to FW binary image to start on GPU.
*/ /**************************************************************************/
const void* OSFirmwareData(OS_FW_IMAGE *psFWImage);

/*************************************************************************/ /*!
@Function     OSFirmwareSize
@Description  This function returns the size of the FW image binary data.
@Input        psFWImage  Private data opaque handle
@Return       size_t     Size in bytes of the firmware binary image
*/ /**************************************************************************/
size_t OSFirmwareSize(OS_FW_IMAGE *psFWImage);

/*************************************************************************/ /*!
@Function     OSUnloadFirmware
@Description  This is called when the server has completed firmware
              initialisation and no longer needs the private data, possibly
              allocated by OSLoadFirmware().
@Input        psFWImage  Private data opaque handle
*/ /**************************************************************************/
void OSUnloadFirmware(OS_FW_IMAGE *psFWImage);

#endif /* FWLOAD_H */

/******************************************************************************
 End of file (fwload.h)
******************************************************************************/
