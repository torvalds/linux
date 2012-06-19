/*
 * Synergy compatible API -- SDIO utility library.
 *
 * Copyright (C) 2010 Cambridge Silicon Radio Ltd.
 *
 * Refer to LICENSE.txt included with this source code for details on
 * the license terms.
 */
#ifndef CSR_SDIO_LIB_H__
#define CSR_SDIO_LIB_H__

#include <csr_sdio.h>

#ifdef __cplusplus
extern "C" {
#endif

CsrResult CsrSdioFunctionReenable(CsrSdioFunction *function);

typedef int CsrStatus; /* platform specific */
#define CSR_STATUS_FAILURE(status) ((status) < 0) /* platform specific */

CsrResult CsrSdioStatusToResult(CsrStatus status);
CsrStatus CsrSdioResultToStatus(CsrResult result);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* #ifndef CSR_SDIO_LIB_H__ */
