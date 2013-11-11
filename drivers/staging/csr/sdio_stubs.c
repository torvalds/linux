/*
 * Stubs for some of the bottom edge functions.
 *
 * These stubs are optional functions in the bottom edge (SDIO driver
 * interface) API that not all platforms or SDIO drivers may support.
 *
 * They're declared as weak symbols so they can be overridden by
 * simply providing a non-weak declaration.
 *
 * Copyright (C) 2007-2008 by Cambridge Silicon Radio Ltd.
 *
 * Refer to LICENSE.txt included with this source code for details on
 * the license terms.
 *
 */
#include "csr_wifi_hip_unifi.h"

void __attribute__((weak)) CsrSdioFunctionIdle(CsrSdioFunction *function)
{
}

void __attribute__((weak)) CsrSdioFunctionActive(CsrSdioFunction *function)
{
}

CsrResult __attribute__((weak)) CsrSdioPowerOn(CsrSdioFunction *function)
{
    return CSR_RESULT_SUCCESS;
}

void __attribute__((weak)) CsrSdioPowerOff(CsrSdioFunction *function)
{
}

CsrResult __attribute__((weak)) CsrSdioHardReset(CsrSdioFunction *function)
{
    return CSR_SDIO_RESULT_NOT_RESET;
}

CsrResult __attribute__((weak)) CsrSdioBlockSizeSet(CsrSdioFunction *function,
                                                   u16 blockSize)
{
    return CSR_RESULT_SUCCESS;
}

CsrResult __attribute__((weak)) CsrSdioSuspend(CsrSdioFunction *function)
{
    return CSR_RESULT_SUCCESS;
}

CsrResult __attribute__((weak)) CsrSdioResume(CsrSdioFunction *function)
{
    return CSR_RESULT_SUCCESS;
}

int __attribute__((weak)) csr_sdio_linux_install_irq(CsrSdioFunction *function)
{
    return 0;
}

int __attribute__((weak)) csr_sdio_linux_remove_irq(CsrSdioFunction *function)
{
    return 0;
}

void __attribute__((weak)) CsrSdioInsertedAcknowledge(CsrSdioFunction *function, CsrResult result)
{
}

void __attribute__((weak)) CsrSdioRemovedAcknowledge(CsrSdioFunction *function)
{
}

void __attribute__((weak)) CsrSdioSuspendAcknowledge(CsrSdioFunction *function, CsrResult result)
{
}

void __attribute__((weak)) CsrSdioResumeAcknowledge(CsrSdioFunction *function, CsrResult result)
{
}


