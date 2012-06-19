#ifndef CSR_UTIL_H__
#define CSR_UTIL_H__
/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2010
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

#ifdef __cplusplus
extern "C" {
#endif

#include "csr_types.h"
#include "csr_macro.h"

/*------------------------------------------------------------------*/
/* Bits - intended to operate on CsrUint32 values */
/*------------------------------------------------------------------*/
CsrUint8 CsrBitCountSparse(CsrUint32 n);
CsrUint8 CsrBitCountDense(CsrUint32 n);

/*------------------------------------------------------------------*/
/* Base conversion */
/*------------------------------------------------------------------*/
CsrBool CsrHexStrToUint8(const CsrCharString *string, CsrUint8 *returnValue);
CsrBool CsrHexStrToUint16(const CsrCharString *string, CsrUint16 *returnValue);
CsrBool CsrHexStrToUint32(const CsrCharString *string, CsrUint32 *returnValue);
CsrUint32 CsrPow(CsrUint32 base, CsrUint32 exponent);
void CsrIntToBase10(CsrInt32 number, CsrCharString *str);
void CsrUInt16ToHex(CsrUint16 number, CsrCharString *str);
void CsrUInt32ToHex(CsrUint32 number, CsrCharString *str);

/*------------------------------------------------------------------*/
/*  String */
/*------------------------------------------------------------------*/
void *CsrMemCpy(void *dest, const void *src, CsrSize count);
void *CsrMemSet(void *dest, CsrUint8 c, CsrSize count);
void *CsrMemMove(void *dest, const void *src, CsrSize count);
CsrInt32 CsrMemCmp(const void *buf1, const void *buf2, CsrSize count);
void *CsrMemDup(const void *buf1, CsrSize count);
CsrCharString *CsrStrCpy(CsrCharString *dest, const CsrCharString *src);
CsrCharString *CsrStrNCpy(CsrCharString *dest, const CsrCharString *src, CsrSize count);
int CsrStrNICmp(const CsrCharString *string1, const CsrCharString *string2, CsrSize count);
CsrCharString *CsrStrCat(CsrCharString *dest, const CsrCharString *src);
CsrCharString *CsrStrNCat(CsrCharString *dest, const CsrCharString *src, CsrSize count);
CsrCharString *CsrStrStr(const CsrCharString *string1, const CsrCharString *string2);
CsrSize CsrStrLen(const CsrCharString *string);
CsrInt32 CsrStrCmp(const CsrCharString *string1, const CsrCharString *string2);
CsrInt32 CsrStrNCmp(const CsrCharString *string1, const CsrCharString *string2, CsrSize count);
CsrCharString *CsrStrDup(const CsrCharString *string);
CsrCharString *CsrStrChr(const CsrCharString *string, CsrCharString c);
CsrUint32 CsrStrToInt(const CsrCharString *string);
CsrInt32 CsrVsnprintf(CsrCharString *string, CsrSize count, const CsrCharString *format, va_list args);
CsrCharString *CsrStrNCpyZero(CsrCharString *dest, const CsrCharString *src, CsrSize count);

/*------------------------------------------------------------------*/
/* Filename */
/*------------------------------------------------------------------*/
const CsrCharString *CsrGetBaseName(const CsrCharString *file);

/*------------------------------------------------------------------*/
/* Misc */
/*------------------------------------------------------------------*/
CsrBool CsrIsSpace(CsrUint8 c);
#define CsrOffsetOf(st, m)  ((CsrSize) & ((st *) 0)->m)

#ifdef __cplusplus
}
#endif

#endif
