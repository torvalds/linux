/*
 * Synergy compatible API -- helpers for Windows Driver Framework drivers.
 *
 *
 * Refer to LICENSE.txt included with this source code for details on
 * the license terms.
 */
#ifndef CSR_SDIO_WDF_H__
#define CSR_SDIO_WDF_H__

#include <wdf.h>

NTSTATUS CsrSdioWdfDeviceInit(WDFDEVICE device);
void CsrSdioWdfDeviceCleanup(WDFDEVICE device);

NTSTATUS CsrSdioWdfDeviceAdd(WDFDEVICE device);
void CsrSdioWdfDeviceDel(WDFDEVICE device);

NTSTATUS CsrSdioWdfDeviceSuspend(WDFDEVICE device);
NTSTATUS CsrSdioWdfDeviceResume(WDFDEVICE device);

#endif /* #ifndef CSR_SDIO_WDF_H__ */
