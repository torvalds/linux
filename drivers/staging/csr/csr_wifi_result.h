/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2011
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

#ifndef CSR_WIFI_RESULT_H__
#define CSR_WIFI_RESULT_H__

#include "csr_result.h"

/* THIS FILE SHOULD CONTAIN ONLY RESULT CODES */

/* Result Codes */
#define CSR_WIFI_HIP_RESULT_INVALID_VALUE    ((CsrResult) 1) /* Invalid argument value */
#define CSR_WIFI_HIP_RESULT_NO_DEVICE        ((CsrResult) 2) /* The specified device is no longer present */
#define CSR_WIFI_HIP_RESULT_NO_SPACE         ((CsrResult) 3) /* A queue or buffer is full */
#define CSR_WIFI_HIP_RESULT_NO_MEMORY        ((CsrResult) 4) /* Fatal error, no memory */
#define CSR_WIFI_HIP_RESULT_RANGE            ((CsrResult) 5) /* Request exceeds the range of a file or a buffer */
#define CSR_WIFI_HIP_RESULT_NOT_FOUND        ((CsrResult) 6) /* A file (typically a f/w patch) is not found */

#endif /* CSR_WIFI_RESULT_H__ */

