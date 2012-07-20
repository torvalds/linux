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
/* Bits - intended to operate on u32 values */
/*------------------------------------------------------------------*/
u8 CsrBitCountSparse(u32 n);
u8 CsrBitCountDense(u32 n);

/*------------------------------------------------------------------*/
/* Base conversion */
/*------------------------------------------------------------------*/
CsrBool CsrHexStrToUint8(const CsrCharString *string, u8 *returnValue);
CsrBool CsrHexStrToUint16(const CsrCharString *string, u16 *returnValue);
CsrBool CsrHexStrToUint32(const CsrCharString *string, u32 *returnValue);
u32 CsrPow(u32 base, u32 exponent);
void CsrIntToBase10(s32 number, CsrCharString *str);
void CsrUInt16ToHex(u16 number, CsrCharString *str);
void CsrUInt32ToHex(u32 number, CsrCharString *str);

/*------------------------------------------------------------------*/
/* Standard C Library functions */
/*------------------------------------------------------------------*/
#ifdef CSR_USE_STDC_LIB
#define CsrMemCpy memcpy
#define CsrMemMove memmove
#define CsrStrCpy strcpy
#define CsrStrNCpy strncpy
#define CsrStrCat strcat
#define CsrStrNCat strncat
#define CsrMemCmp(s1, s2, n) ((s32) memcmp((s1), (s2), (n)))
#define CsrStrCmp(s1, s2) ((s32) strcmp((s1), (s2)))
#define CsrStrNCmp(s1, s2, n) ((s32) strncmp((s1), (s2), (n)))
#define CsrStrChr strchr
#define CsrStrStr strstr
#define CsrMemSet memset
#define CsrStrLen strlen
#else /* !CSR_USE_STDC_LIB */
void *CsrMemCpy(void *dest, const void *src, CsrSize count);
void *CsrMemMove(void *dest, const void *src, CsrSize count);
CsrCharString *CsrStrCpy(CsrCharString *dest, const CsrCharString *src);
CsrCharString *CsrStrNCpy(CsrCharString *dest, const CsrCharString *src, CsrSize count);
CsrCharString *CsrStrCat(CsrCharString *dest, const CsrCharString *src);
CsrCharString *CsrStrNCat(CsrCharString *dest, const CsrCharString *src, CsrSize count);
s32 CsrMemCmp(const void *buf1, const void *buf2, CsrSize count);
s32 CsrStrCmp(const CsrCharString *string1, const CsrCharString *string2);
s32 CsrStrNCmp(const CsrCharString *string1, const CsrCharString *string2, CsrSize count);
CsrCharString *CsrStrChr(const CsrCharString *string, CsrCharString c);
CsrCharString *CsrStrStr(const CsrCharString *string1, const CsrCharString *string2);
void *CsrMemSet(void *dest, u8 c, CsrSize count);
CsrSize CsrStrLen(const CsrCharString *string);
#endif /* !CSR_USE_STDC_LIB */
s32 CsrVsnprintf(CsrCharString *string, CsrSize count, const CsrCharString *format, va_list args);

/*------------------------------------------------------------------*/
/* Non-standard utility functions */
/*------------------------------------------------------------------*/
void *CsrMemDup(const void *buf1, CsrSize count);
int CsrStrNICmp(const CsrCharString *string1, const CsrCharString *string2, CsrSize count);
CsrCharString *CsrStrDup(const CsrCharString *string);
u32 CsrStrToInt(const CsrCharString *string);
CsrCharString *CsrStrNCpyZero(CsrCharString *dest, const CsrCharString *src, CsrSize count);

/*------------------------------------------------------------------*/
/* Filename */
/*------------------------------------------------------------------*/
const CsrCharString *CsrGetBaseName(const CsrCharString *file);

/*------------------------------------------------------------------*/
/* Misc */
/*------------------------------------------------------------------*/
CsrBool CsrIsSpace(u8 c);
#define CsrOffsetOf(st, m)  ((CsrSize) & ((st *) 0)->m)

#ifdef __cplusplus
}
#endif

#endif
