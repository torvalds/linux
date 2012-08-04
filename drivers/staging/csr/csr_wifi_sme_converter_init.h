/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2012
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

/* Note: this is an auto-generated file. */

#ifndef CSR_WIFI_SME_CONVERTER_INIT_H__
#define CSR_WIFI_SME_CONVERTER_INIT_H__

#ifdef __cplusplus
extern "C" {
#endif

#ifndef EXCLUDE_CSR_WIFI_SME_MODULE

#include "csr_msgconv.h"

#ifdef CSR_LOG_ENABLE
#include "csr_log.h"

extern const CsrLogPrimitiveInformation* CsrWifiSmeTechInfoGet(void);
#endif /* CSR_LOG_ENABLE */

extern void CsrWifiSmeConverterInit(void);

#else /* EXCLUDE_CSR_WIFI_SME_MODULE */

#define CsrWifiSmeConverterInit()

#endif /* EXCLUDE_CSR_WIFI_SME_MODULE */

#ifdef __cplusplus
}
#endif

#endif /* CSR_WIFI_SME_CONVERTER_INIT_H__ */
