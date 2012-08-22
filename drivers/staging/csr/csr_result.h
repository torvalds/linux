#ifndef CSR_RESULT_H__
#define CSR_RESULT_H__
/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2010
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

#ifdef __cplusplus
extern "C" {
#endif

typedef u16 CsrResult;
#define CSR_RESULT_SUCCESS  ((CsrResult) 0x0000)
#define CSR_RESULT_FAILURE  ((CsrResult) 0xFFFF)

#ifdef __cplusplus
}
#endif

#endif
