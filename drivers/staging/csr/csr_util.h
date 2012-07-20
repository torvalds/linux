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
u8 CsrHexStrToUint8(const char *string, u8 *returnValue);
u8 CsrHexStrToUint16(const char *string, u16 *returnValue);
u8 CsrHexStrToUint32(const char *string, u32 *returnValue);
u32 CsrPow(u32 base, u32 exponent);
void CsrIntToBase10(s32 number, char *str);
void CsrUInt16ToHex(u16 number, char *str);
void CsrUInt32ToHex(u32 number, char *str);

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
void *CsrMemCpy(void *dest, const void *src, size_t count);
void *CsrMemMove(void *dest, const void *src, size_t count);
char *CsrStrCpy(char *dest, const char *src);
char *CsrStrNCpy(char *dest, const char *src, size_t count);
char *CsrStrCat(char *dest, const char *src);
char *CsrStrNCat(char *dest, const char *src, size_t count);
s32 CsrMemCmp(const void *buf1, const void *buf2, size_t count);
s32 CsrStrCmp(const char *string1, const char *string2);
s32 CsrStrNCmp(const char *string1, const char *string2, size_t count);
char *CsrStrChr(const char *string, char c);
char *CsrStrStr(const char *string1, const char *string2);
void *CsrMemSet(void *dest, u8 c, size_t count);
size_t CsrStrLen(const char *string);
#endif /* !CSR_USE_STDC_LIB */
s32 CsrVsnprintf(char *string, size_t count, const char *format, va_list args);

/*------------------------------------------------------------------*/
/* Non-standard utility functions */
/*------------------------------------------------------------------*/
void *CsrMemDup(const void *buf1, size_t count);
int CsrStrNICmp(const char *string1, const char *string2, size_t count);
char *CsrStrDup(const char *string);
u32 CsrStrToInt(const char *string);
char *CsrStrNCpyZero(char *dest, const char *src, size_t count);

/*------------------------------------------------------------------*/
/* Filename */
/*------------------------------------------------------------------*/
const char *CsrGetBaseName(const char *file);

/*------------------------------------------------------------------*/
/* Misc */
/*------------------------------------------------------------------*/
u8 CsrIsSpace(u8 c);
#define CsrOffsetOf(st, m)  ((size_t) & ((st *) 0)->m)

#ifdef __cplusplus
}
#endif

#endif
