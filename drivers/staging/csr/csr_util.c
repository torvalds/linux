/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2010
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

#include <linux/kernel.h>
#include <linux/module.h>
#include <stdarg.h>

#include "csr_types.h"
#include "csr_pmem.h"
#include "csr_util.h"

/*------------------------------------------------------------------*/
/* Bits */
/*------------------------------------------------------------------*/

/* Time proportional with the number of 1's */
u8 CsrBitCountSparse(u32 n)
{
    u8 count = 0;

    while (n)
    {
        count++;
        n &= (n - 1);
    }

    return count;
}

/* Time proportional with the number of 0's */
u8 CsrBitCountDense(u32 n)
{
    u8 count = 8 * sizeof(u32);

    n ^= (u32) (-1);

    while (n)
    {
        count--;
        n &= (n - 1);
    }

    return count;
}

/*------------------------------------------------------------------*/
/* Base conversion */
/*------------------------------------------------------------------*/
CsrBool CsrHexStrToUint8(const CsrCharString *string, u8 *returnValue)
{
    u16 currentIndex = 0;
    *returnValue = 0;
    if ((string[currentIndex] == '0') && (CSR_TOUPPER(string[currentIndex + 1]) == 'X'))
    {
        string += 2;
    }
    if (((string[currentIndex] >= '0') && (string[currentIndex] <= '9')) || ((CSR_TOUPPER(string[currentIndex]) >= 'A') && (CSR_TOUPPER(string[currentIndex]) <= 'F')))
    {
        while (((string[currentIndex] >= '0') && (string[currentIndex] <= '9')) || ((CSR_TOUPPER(string[currentIndex]) >= 'A') && (CSR_TOUPPER(string[currentIndex]) <= 'F')))
        {
            *returnValue = (u8) (*returnValue * 16 + (((string[currentIndex] >= '0') && (string[currentIndex] <= '9')) ? string[currentIndex] - '0' : CSR_TOUPPER(string[currentIndex]) - 'A' + 10));
            currentIndex++;
            if (currentIndex >= 2)
            {
                break;
            }
        }
        return TRUE;
    }
    return FALSE;
}

CsrBool CsrHexStrToUint16(const CsrCharString *string, u16 *returnValue)
{
    u16 currentIndex = 0;
    *returnValue = 0;
    if ((string[currentIndex] == '0') && (CSR_TOUPPER(string[currentIndex + 1]) == 'X'))
    {
        string += 2;
    }
    if (((string[currentIndex] >= '0') && (string[currentIndex] <= '9')) || ((CSR_TOUPPER(string[currentIndex]) >= 'A') && (CSR_TOUPPER(string[currentIndex]) <= 'F')))
    {
        while (((string[currentIndex] >= '0') && (string[currentIndex] <= '9')) || ((CSR_TOUPPER(string[currentIndex]) >= 'A') && (CSR_TOUPPER(string[currentIndex]) <= 'F')))
        {
            *returnValue = (u16) (*returnValue * 16 + (((string[currentIndex] >= '0') && (string[currentIndex] <= '9')) ? string[currentIndex] - '0' : CSR_TOUPPER(string[currentIndex]) - 'A' + 10));
            currentIndex++;
            if (currentIndex >= 4)
            {
                break;
            }
        }
        return TRUE;
    }
    return FALSE;
}

CsrBool CsrHexStrToUint32(const CsrCharString *string, u32 *returnValue)
{
    u16 currentIndex = 0;
    *returnValue = 0;
    if ((string[currentIndex] == '0') && (CSR_TOUPPER(string[currentIndex + 1]) == 'X'))
    {
        string += 2;
    }
    if (((string[currentIndex] >= '0') && (string[currentIndex] <= '9')) || ((CSR_TOUPPER(string[currentIndex]) >= 'A') && (CSR_TOUPPER(string[currentIndex]) <= 'F')))
    {
        while (((string[currentIndex] >= '0') && (string[currentIndex] <= '9')) || ((CSR_TOUPPER(string[currentIndex]) >= 'A') && (CSR_TOUPPER(string[currentIndex]) <= 'F')))
        {
            *returnValue = *returnValue * 16 + (((string[currentIndex] >= '0') && (string[currentIndex] <= '9')) ? string[currentIndex] - '0' : CSR_TOUPPER(string[currentIndex]) - 'A' + 10);
            currentIndex++;
            if (currentIndex >= 8)
            {
                break;
            }
        }
        return TRUE;
    }
    return FALSE;
}

u32 CsrPow(u32 base, u32 exponent)
{
    if (exponent == 0)
    {
        return 1;
    }
    else
    {
        u32 i, t = base;

        for (i = 1; i < exponent; i++)
        {
            t = t * base;
        }
        return t;
    }
}

/* Convert signed 32 bit (or less) integer to string */
#define I2B10_MAX 12
void CsrIntToBase10(CsrInt32 number, CsrCharString *str)
{
    CsrInt32 digit;
    u8 index;
    CsrCharString res[I2B10_MAX];
    CsrBool foundDigit = FALSE;

    for (digit = 0; digit < I2B10_MAX; digit++)
    {
        res[digit] = '\0';
    }

    /* Catch sign - and deal with positive numbers only afterwards */
    index = 0;
    if (number < 0)
    {
        res[index++] = '-';
        number = -1 * number;
    }

    digit = 1000000000;
    if (number > 0)
    {
        while ((index < I2B10_MAX - 1) && (digit > 0))
        {
            /* If the foundDigit flag is TRUE, this routine should be proceeded.
            Otherwise the number which has '0' digit cannot be converted correctly */
            if (((number / digit) > 0) || foundDigit)
            {
                foundDigit = TRUE; /* set foundDigit flag to TRUE*/
                res[index++] = (char) ('0' + (number / digit));
                number = number % digit;
            }

            digit = digit / 10;
        }
    }
    else
    {
        res[index] = (char) '0';
    }

    CsrStrCpy(str, res);
}

void CsrUInt16ToHex(u16 number, CsrCharString *str)
{
    u16 index;
    u16 currentValue;

    for (index = 0; index < 4; index++)
    {
        currentValue = (u16) (number & 0x000F);
        number >>= 4;
        str[3 - index] = (char) (currentValue > 9 ? currentValue + 55 : currentValue + '0');
    }
    str[4] = '\0';
}

void CsrUInt32ToHex(u32 number, CsrCharString *str)
{
    u16 index;
    u32 currentValue;

    for (index = 0; index < 8; index++)
    {
        currentValue = (u32) (number & 0x0000000F);
        number >>= 4;
        str[7 - index] = (char) (currentValue > 9 ? currentValue + 55 : currentValue + '0');
    }
    str[8] = '\0';
}

/*------------------------------------------------------------------*/
/*  String */
/*------------------------------------------------------------------*/
#ifndef CSR_USE_STDC_LIB
void *CsrMemCpy(void *dest, const void *src, CsrSize count)
{
    return memcpy(dest, src, count);
}
EXPORT_SYMBOL_GPL(CsrMemCpy);

void *CsrMemSet(void *dest, u8 c, CsrSize count)
{
    return memset(dest, c, count);
}
EXPORT_SYMBOL_GPL(CsrMemSet);

void *CsrMemMove(void *dest, const void *src, CsrSize count)
{
    return memmove(dest, src, count);
}
EXPORT_SYMBOL_GPL(CsrMemMove);

CsrInt32 CsrMemCmp(const void *buf1, const void *buf2, CsrSize count)
{
    return memcmp(buf1, buf2, count);
}
EXPORT_SYMBOL_GPL(CsrMemCmp);

void *CsrMemDup(const void *buf1, CsrSize count)
{
    void *buf2 = NULL;

    if (buf1)
    {
        buf2 = CsrPmemAlloc(count);
        CsrMemCpy(buf2, buf1, count);
    }

    return buf2;
}
#endif

#ifndef CSR_USE_STDC_LIB
CsrCharString *CsrStrCpy(CsrCharString *dest, const CsrCharString *src)
{
    return strcpy(dest, src);
}

CsrCharString *CsrStrNCpy(CsrCharString *dest, const CsrCharString *src, CsrSize count)
{
    return strncpy(dest, src, count);
}

CsrCharString *CsrStrCat(CsrCharString *dest, const CsrCharString *src)
{
    return strcat(dest, src);
}

CsrCharString *CsrStrNCat(CsrCharString *dest, const CsrCharString *src, CsrSize count)
{
    return strncat(dest, src, count);
}

CsrCharString *CsrStrStr(const CsrCharString *string1, const CsrCharString *string2)
{
    return strstr(string1, string2);
}

CsrSize CsrStrLen(const CsrCharString *string)
{
    return strlen(string);
}
EXPORT_SYMBOL_GPL(CsrStrLen);

CsrInt32 CsrStrCmp(const CsrCharString *string1, const CsrCharString *string2)
{
    return strcmp(string1, string2);
}

CsrInt32 CsrStrNCmp(const CsrCharString *string1, const CsrCharString *string2, CsrSize count)
{
    return strncmp(string1, string2, count);
}

CsrCharString *CsrStrChr(const CsrCharString *string, CsrCharString c)
{
    return strchr(string, c);
}
#endif

CsrInt32 CsrVsnprintf(CsrCharString *string, CsrSize count, const CsrCharString *format, va_list args)
{
    return vsnprintf(string, count, format, args);
}
EXPORT_SYMBOL_GPL(CsrVsnprintf);

CsrCharString *CsrStrNCpyZero(CsrCharString *dest,
    const CsrCharString *src,
    CsrSize count)
{
    CsrStrNCpy(dest, src, count - 1);
    dest[count - 1] = '\0';
    return dest;
}

/* Convert string with base 10 to integer */
u32 CsrStrToInt(const CsrCharString *str)
{
    s16 i;
    u32 res;
    u32 digit;

    res = 0;
    digit = 1;

    /* Start from the string end */
    for (i = (u16) (CsrStrLen(str) - 1); i >= 0; i--)
    {
        /* Only convert numbers */
        if ((str[i] >= '0') && (str[i] <= '9'))
        {
            res += digit * (str[i] - '0');
            digit = digit * 10;
        }
    }

    return res;
}

CsrCharString *CsrStrDup(const CsrCharString *string)
{
    CsrCharString *copy;
    u32 len;

    copy = NULL;
    if (string != NULL)
    {
        len = CsrStrLen(string) + 1;
        copy = CsrPmemAlloc(len);
        CsrMemCpy(copy, string, len);
    }
    return copy;
}

int CsrStrNICmp(const CsrCharString *string1,
    const CsrCharString *string2,
    CsrSize count)
{
    u32 index;
    int returnValue = 0;

    for (index = 0; index < count; index++)
    {
        if (CSR_TOUPPER(string1[index]) != CSR_TOUPPER(string2[index]))
        {
            if (CSR_TOUPPER(string1[index]) > CSR_TOUPPER(string2[index]))
            {
                returnValue = 1;
            }
            else
            {
                returnValue = -1;
            }
            break;
        }
        if (string1[index] == '\0')
        {
            break;
        }
    }
    return returnValue;
}

const CsrCharString *CsrGetBaseName(const CsrCharString *file)
{
    const CsrCharString *pch;
    static const CsrCharString dotDir[] = ".";

    if (!file)
    {
        return NULL;
    }

    if (file[0] == '\0')
    {
        return dotDir;
    }

    pch = file + CsrStrLen(file) - 1;

    while (*pch != '\\' && *pch != '/' && *pch != ':')
    {
        if (pch == file)
        {
            return pch;
        }
        --pch;
    }

    return ++pch;
}

/*------------------------------------------------------------------*/
/* Misc */
/*------------------------------------------------------------------*/
CsrBool CsrIsSpace(u8 c)
{
    switch (c)
    {
        case '\t':
        case '\n':
        case '\f':
        case '\r':
        case ' ':
            return TRUE;
        default:
            return FALSE;
    }
}

MODULE_DESCRIPTION("CSR Operating System Kernel Abstraction");
MODULE_AUTHOR("Cambridge Silicon Radio Ltd.");
MODULE_LICENSE("GPL and additional rights");
