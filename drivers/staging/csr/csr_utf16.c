/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2010
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/
#include <linux/module.h>
#include "csr_pmem.h"
#include "csr_unicode.h"
#include "csr_util.h"

#define UNI_SUR_HIGH_START   ((u32) 0xD800)
#define UNI_SUR_HIGH_END     ((u32) 0xDBFF)
#define UNI_SUR_LOW_START    ((u32) 0xDC00)
#define UNI_SUR_LOW_END      ((u32) 0xDFFF)
#define UNI_REPLACEMENT_CHAR ((u32) 0xFFFD)
#define UNI_HALF_SHIFT       ((u8) 10)  /* used for shifting by 10 bits */
#define UNI_HALF_BASE        ((u32) 0x00010000)
#define UNI_BYTEMASK         ((u32) 0xBF)
#define UNI_BYTEMARK         ((u32) 0x80)

#define CAPITAL(x)    ((x >= 'a') && (x <= 'z') ? ((x) & 0x00DF) : (x))

/*
*  Index into the table with the first byte to get the number of trailing bytes in a utf-8 character.
*  -1 if the byte has an invalid value.
*
*  Legal sequences are:
*
*  byte  1st      2nd      3rd      4th
*
*       00-7F
*       C2-DF    80-BF
*       E0       A0-BF    80-BF
*       E1-EC    80-BF    80-BF
*       ED       80-9F    80-BF
*       EE-EF    80-BF    80-BF
*       F0       90-BF    80-BF    80-BF
*       F1-F3    80-BF    80-BF    80-BF
*       F4       80-8F    80-BF    80-BF
*/
static const s8 trailingBytesForUtf8[256] =
{
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,                                 /* 0x00 - 0x1F */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,                                 /* 0x20 - 0x3F */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,                                 /* 0x40 - 0x5F */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,                                 /* 0x60 - 0x7F */
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* 0x80 - 0x9F */
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /* 0xA0 - 0xBF */
    -1, -1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,                               /* 0xC0 - 0xDF */
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,                      /* 0xE0 - 0xFF */
};

/* Values to be substracted from a u32 when converting from UTF8 to UTF16 */
static const u32 offsetsFromUtf8[4] =
{
    0x00000000, 0x00003080, 0x000E2080, 0x03C82080
};

/********************************************************************************
*
*   Name:           CsrUint32ToUtf16String
*
*   Description:    The function converts an 32 bit number to an UTF-16 string
*                   that is allocated and 0-terminated.
*
*   Input:          32 bit number.
*
*   Output:         A string of UTF-16 characters.
*
*********************************************************************************/
u16 *CsrUint32ToUtf16String(u32 number)
{
    u16 count, noOfDigits;
    u16 *output;
    u32 tempNumber;

    /* calculate the number of digits in the output */
    tempNumber = number;
    noOfDigits = 1;
    while (tempNumber >= 10)
    {
        tempNumber = tempNumber / 10;
        noOfDigits++;
    }

    output = (u16 *) CsrPmemAlloc(sizeof(u16) * (noOfDigits + 1)); /*add space for 0-termination*/

    tempNumber = number;
    for (count = noOfDigits; count > 0; count--)
    {
        output[count - 1] = (u16) ((tempNumber % 10) + '0');
        tempNumber = tempNumber / 10;
    }
    output[noOfDigits] = '\0';

    return output;
}

/********************************************************************************
*
*   Name:           CsrUtf16StringToUint32
*
*   Description:    The function converts an UTF-16 string that is
*                   0-terminated into a 32 bit number.
*
*   Input:          A string of UTF-16 characters containig a number.
*
*   Output:         32 bit number.
*
*********************************************************************************/
u32 CsrUtf16StringToUint32(const u16 *unicodeString)
{
    u16 numLen, count;
    u32 newNumber = 0;

    numLen = (u16) CsrUtf16StrLen(unicodeString);

    if ((numLen > 10) || (numLen == 0) || (unicodeString == NULL)) /*CSRMAX number is 4.294.967.295 */
    {
        return 0;
    }

    for (count = 0; count < numLen; count++)
    {
        u16 input = unicodeString[count];
        if ((input < 0x30) || (input > 0x39) || ((newNumber == 0x19999999) && (input > 0x35)) || (newNumber > 0x19999999)) /* chars are present or number is too large now causing number to get to large when *10 */
        {
            return 0;
        }

        newNumber = (newNumber * 10) + (input - 0x30);
    }
    return newNumber;
}

/********************************************************************************
*
*   Name:           CsrUtf16MemCpy
*
*   Description:    The function copies count number of 16 bit data elements
*                   from srv to dest.
*
*   Input:          A pointer to an unicoded string.
*
*   Output:         A pointer to an unicoded string.
*
*********************************************************************************/
u16 *CsrUtf16MemCpy(u16 *dest, const u16 *src, u32 count)
{
    return CsrMemCpy((u8 *) dest, (u8 *) src, count * sizeof(u16));
}

/********************************************************************************
*
*   Name:           CsrUtf16ConcatenateTexts
*
*   Description:    The function merge the contents of 4 unicoded input pointers
*                   into a new string.
*
*   Input:          4 unicoded input strings (UTF-16).
*
*   Output:         A new unicoded string (UTF-16) containing the combined strings.
*
*********************************************************************************/
u16 *CsrUtf16ConcatenateTexts(const u16 *inputText1, const u16 *inputText2,
    const u16 *inputText3, const u16 *inputText4)
{
    u16 *outputText;
    u32 textLen, textLen1, textLen2, textLen3, textLen4;

    textLen1 = CsrUtf16StrLen(inputText1);
    textLen2 = CsrUtf16StrLen(inputText2);
    textLen3 = CsrUtf16StrLen(inputText3);
    textLen4 = CsrUtf16StrLen(inputText4);

    textLen = textLen1 + textLen2 + textLen3 + textLen4;

    if (textLen == 0) /*stop here is all lengths are 0*/
    {
        return NULL;
    }

    outputText = (u16 *) CsrPmemAlloc((textLen + 1) * sizeof(u16)); /* add space for 0-termination*/


    if (inputText1 != NULL)
    {
        CsrUtf16MemCpy(outputText, inputText1, textLen1);
    }

    if (inputText2 != NULL)
    {
        CsrUtf16MemCpy(&(outputText[textLen1]), inputText2, textLen2);
    }

    if (inputText3 != NULL)
    {
        CsrUtf16MemCpy(&(outputText[textLen1 + textLen2]), inputText3, textLen3);
    }

    if (inputText4 != NULL)
    {
        CsrUtf16MemCpy(&(outputText[textLen1 + textLen2 + textLen3]), inputText4, textLen4);
    }

    outputText[textLen] = '\0';

    return outputText;
}

/********************************************************************************
*
*   Name:           CsrUtf16StrLen
*
*   Description:    The function returns the number of 16 bit elements present
*                   in the 0-terminated string.
*
*   Input:          0-terminated string of 16 bit unicoded characters.
*
*   Output:         The number of 16 bit elements in the string.
*
*********************************************************************************/
u32 CsrUtf16StrLen(const u16 *unicodeString)
{
    u32 length;

    length = 0;
    if (unicodeString != NULL)
    {
        while (*unicodeString)
        {
            length++;
            unicodeString++;
        }
    }
    return length;
}

/********************************************************************************
*
*   Name:           CsrUtf16String2Utf8
*
*   Description:    The function decodes an UTF-16 string into an UTF8 byte
*                   oriented string.
*
*   Input:          0-terminated UTF-16 string characters.
*
*   Output:         0-terminated string of byte oriented UTF8 coded characters.
*
*********************************************************************************/
u8 *CsrUtf16String2Utf8(const u16 *source)
{
    u8 *dest, *destStart = NULL;
    u32 i;
    u32 ch;
    u32 length;
    u32 sourceLength;
    u8 bytes;
    u8 appendNull = FALSE;

    u8 firstByteMark[5] = {0x00, 0x00, 0xC0, 0xE0, 0xF0};

    if (!source)
    {
        return NULL;
    }

    length = 0;
    sourceLength = CsrUtf16StrLen(source) + 1;

    for (i = 0; i < sourceLength; i++)
    {
        ch = source[i];
        if ((ch >= UNI_SUR_HIGH_START) && (ch <= UNI_SUR_HIGH_END)) /* This is a high surrogate */
        {
            if (i + 1 < sourceLength) /* The low surrogate is in the source */
            {
                u32 ch2 = source[++i];
                if ((ch2 >= UNI_SUR_LOW_START) && (ch2 <= UNI_SUR_LOW_END)) /* And it is a legal low surrogate */
                {
                    length += 4;
                }
                else /* It is not a low surrogate, instead put a Unicode
                     'REPLACEMENT CHARACTER' (U+FFFD) */
                {
                    length += 3;
                    i--; /* Substract 1 again as the conversion must continue after the ill-formed code unit */
                }
            }
            else /* The low surrogate does not exist, instead put a Unicode
                 'REPLACEMENT CHARACTER' (U+FFFD), and the null terminated character */
            {
                length += 4;
            }
        }
        else if ((ch >= UNI_SUR_LOW_START) && (ch <= UNI_SUR_LOW_END)) /* The value of UTF-16 is not allowed to be in this range, instead put
             a Unicode 'REPLACEMENT CHARACTER' (U+FFFD) */
        {
            length += 3;
        }
        else /* Figure out how many bytes that are required */
        {
            if (ch < 0x0080)
            {
                length++;
            }
            else if (ch < 0x0800)
            {
                length += 2;
            }
            else
            {
                length += 3;
            }
        }
    }

    dest = CsrPmemAlloc(length);
    destStart = dest;

    for (i = 0; i < sourceLength; i++)
    {
        ch = source[i];
        if ((ch >= UNI_SUR_HIGH_START) && (ch <= UNI_SUR_HIGH_END)) /* This is a high surrogate */
        {
            if (i + 1 < sourceLength) /* The low surrogate is in the source */
            {
                u32 ch2 = source[++i];
                if ((ch2 >= UNI_SUR_LOW_START) && (ch2 <= UNI_SUR_LOW_END)) /* And it is a legal low surrogate, convert to UTF-32 */
                {
                    ch = ((ch - UNI_SUR_HIGH_START) << UNI_HALF_SHIFT) + (ch2 - UNI_SUR_LOW_START) + UNI_HALF_BASE;
                }
                else /* It is not a low surrogate, instead put a Unicode
                     'REPLACEMENT CHARACTER' (U+FFFD) */
                {
                    ch = UNI_REPLACEMENT_CHAR;
                    i--; /* Substract 1 again as the conversion must continue after the ill-formed code unit */
                }
            }
            else /* The low surrogate does not exist, instead put a Unicode
                 'REPLACEMENT CHARACTER' (U+FFFD), and the null terminated character */
            {
                ch = UNI_REPLACEMENT_CHAR;
                appendNull = TRUE;
            }
        }
        else if ((ch >= UNI_SUR_LOW_START) && (ch <= UNI_SUR_LOW_END)) /* The value of UTF-16 is not allowed to be in this range, instead put
             a Unicode 'REPLACEMENT CHARACTER' (U+FFFD) */
        {
            ch = UNI_REPLACEMENT_CHAR;
        }

        /* Figure out how many bytes that are required */
        if (ch < (u32) 0x80)
        {
            bytes = 1;
        }
        else if (ch < (u32) 0x800)
        {
            bytes = 2;
        }
        else if (ch < (u32) 0x10000)
        {
            bytes = 3;
        }
        else if (ch < (u32) 0x110000)
        {
            bytes = 4;
        }
        else
        {
            bytes = 3;
            ch = UNI_REPLACEMENT_CHAR;
        }

        dest += bytes;

        switch (bytes) /* Convert character to UTF-8. Note: everything falls through. */
        {
            case 4:
            {
                *--dest = (u8) ((ch | UNI_BYTEMARK) & UNI_BYTEMASK);
                ch >>= 6;
            }
            /* FALLTHROUGH */
            case 3:
            {
                *--dest = (u8) ((ch | UNI_BYTEMARK) & UNI_BYTEMASK);
                ch >>= 6;
            }
            /* FALLTHROUGH */
            case 2:
            {
                *--dest = (u8) ((ch | UNI_BYTEMARK) & UNI_BYTEMASK);
                ch >>= 6;
            }
            /* FALLTHROUGH */
            case 1:
            {
                *--dest = (u8) (ch | firstByteMark[bytes]);
            }
            /* FALLTHROUGH */
            default:
            {
                break;
            }
        }

        dest += bytes;
    }

    if (appendNull) /* Append the \0 character */
    {
        *dest = '\0';
    }

    return destStart;
}

/*****************************************************************************

    NAME
        isLegalUtf8

    DESCRIPTION
        Returns TRUE if the given UFT-8 code unit is legal as defined by the
        Unicode standard (see Chapter 3: Conformance, Section 3.9: Unicode
        Encoding Forms, UTF-8).

        This function assumes that the length parameter is unconditionally
        correct and that the first byte is already validated by looking it up
        in the trailingBytesForUtf8 array, which also reveals the number of
        trailing bytes.

        Legal code units are composed of one of the following byte sequences:

        1st      2nd      3rd      4th
        --------------------------------
        00-7F
        C2-DF    80-BF
        E0       A0-BF    80-BF
        E1-EC    80-BF    80-BF
        ED       80-9F    80-BF
        EE-EF    80-BF    80-BF
        F0       90-BF    80-BF    80-BF
        F1-F3    80-BF    80-BF    80-BF
        F4       80-8F    80-BF    80-BF

        Please note that this function only checks whether the 2nd, 3rd and
        4th bytes fall into the valid ranges.

    PARAMETERS
        codeUnit - pointer to the first byte of the byte sequence composing
            the code unit to test.
        length - the number of bytes in the code unit. Valid range is 1 to 4.

    RETURNS
        TRUE if the given code unit is legal.

*****************************************************************************/
static u8 isLegalUtf8(const u8 *codeUnit, u32 length)
{
    const u8 *srcPtr = codeUnit + length;
    u8 byte;

    switch (length) /* Everything falls through except case 1 */
    {
        case 4:
        {
            byte = *--srcPtr;
            if ((byte < 0x80) || (byte > 0xBF))
            {
                return FALSE;
            }
        }
        /* Fallthrough */
        case 3:
        {
            byte = *--srcPtr;
            if ((byte < 0x80) || (byte > 0xBF))
            {
                return FALSE;
            }
        }
        /* Fallthrough */
        case 2:
        {
            byte = *--srcPtr;
            if (byte > 0xBF)
            {
                return FALSE;
            }

            switch (*codeUnit) /* No fallthrough */
            {
                case 0xE0:
                {
                    if (byte < 0xA0)
                    {
                        return FALSE;
                    }
                    break;
                }
                case 0xED:
                {
                    if ((byte < 0x80) || (byte > 0x9F))
                    {
                        return FALSE;
                    }
                    break;
                }
                case 0xF0:
                {
                    if (byte < 0x90)
                    {
                        return FALSE;
                    }
                    break;
                }
                case 0xF4:
                {
                    if ((byte < 0x80) || (byte > 0x8F))
                    {
                        return FALSE;
                    }
                    break;
                }
                default:
                {
                    if (byte < 0x80)
                    {
                        return FALSE;
                    }
                    break;
                }
            }
        }
        /* Fallthrough */
        case 1:
        default:
            /* The 1st byte and length are assumed correct */
            break;
    }

    return TRUE;
}

/********************************************************************************
*
*   Name:           CsrUtf82Utf16String
*
*   Description:    The function decodes an UTF8 byte oriented string into a
*                   UTF-16string.
*
*   Input:          0-terminated string of byte oriented UTF8 coded characters.
*
*   Output:         0-terminated string of UTF-16 characters.
*
*********************************************************************************/
u16 *CsrUtf82Utf16String(const u8 *utf8String)
{
    size_t i, length = 0;
    size_t sourceLength;
    u16 *dest = NULL;
    u16 *destStart = NULL;
    s8 extraBytes2Read;

    if (!utf8String)
    {
        return NULL;
    }
    sourceLength = CsrStrLen((char *) utf8String);

    for (i = 0; i < sourceLength; i++)
    {
        extraBytes2Read = trailingBytesForUtf8[utf8String[i]];

        if (extraBytes2Read == -1) /* Illegal byte value, instead put a Unicode 'REPLACEMENT CHARACTER' (U+FFFD) */
        {
            length += 1;
        }
        else if (i + extraBytes2Read > sourceLength) /* The extra bytes does not exist, instead put a Unicode 'REPLACEMENT
             CHARACTER' (U+FFFD), and the null terminated character */
        {
            length += 2;
            break;
        }
        else if (isLegalUtf8(&utf8String[i], extraBytes2Read + 1) == FALSE) /* It is not a legal utf-8 character, instead put a Unicode 'REPLACEMENT
             CHARACTER' (U+FFFD) */
        {
            length += 1;
        }
        else
        {
            if (utf8String[i] > 0xEF) /* Needs a high and a low surrogate */
            {
                length += 2;
            }
            else
            {
                length += 1;
            }
            i += extraBytes2Read;
        }
    }

    /* Create space for the null terminated character */
    dest = (u16 *) CsrPmemAlloc((1 + length) * sizeof(u16));
    destStart = dest;

    for (i = 0; i < sourceLength; i++)
    {
        extraBytes2Read = trailingBytesForUtf8[utf8String[i]];

        if (extraBytes2Read == -1) /* Illegal byte value, instead put a Unicode 'REPLACEMENT CHARACTER' (U+FFFD) */
        {
            *dest++ = UNI_REPLACEMENT_CHAR;
        }
        else if (i + extraBytes2Read > sourceLength) /* The extra bytes does not exist, instead put a Unicode 'REPLACEMENT
             CHARACTER' (U+FFFD), and the null terminated character */
        {
            *dest++ = UNI_REPLACEMENT_CHAR;
            *dest++ = '\0';
            break;
        }
        else if (isLegalUtf8(&utf8String[i], extraBytes2Read + 1) == FALSE) /* It is not a legal utf-8 character, instead put a Unicode 'REPLACEMENT
             CHARACTER' (U+FFFD) */
        {
            *dest++ = UNI_REPLACEMENT_CHAR;
        }
        else /* It is legal, convert the character to an u32 */
        {
            u32 ch = 0;

            switch (extraBytes2Read) /* Everything falls through */
            {
                case 3:
                {
                    ch += utf8String[i];
                    ch <<= 6;
                    i++;
                }
                /* FALLTHROUGH */
                case 2:
                {
                    ch += utf8String[i];
                    ch <<= 6;
                    i++;
                }
                /* FALLTHROUGH */
                case 1:
                {
                    ch += utf8String[i];
                    ch <<= 6;
                    i++;
                }
                /* FALLTHROUGH */
                case 0:
                {
                    ch += utf8String[i];
                }
                /* FALLTHROUGH */
                default:
                {
                    break;
                }
            }

            ch -= offsetsFromUtf8[extraBytes2Read];

            if (ch <= 0xFFFF) /* Character can be encoded in one u16 */
            {
                *dest++ = (u16) ch;
            }
            else /* The character needs two u16 */
            {
                ch -= UNI_HALF_BASE;
                *dest++ = (u16) ((ch >> UNI_HALF_SHIFT) | UNI_SUR_HIGH_START);
                *dest++ = (u16) ((ch & 0x03FF) | UNI_SUR_LOW_START);
            }
        }
    }

    destStart[length] = 0x00;

    return destStart;
}

/********************************************************************************
*
*   Name:           CsrUtf16StrCpy
*
*   Description:    The function copies the contents from one UTF-16 string
*                   to another UTF-16 string.
*
*   Input:          0-terminated UTF-16 string.
*
*   Output:         0-terminated UTF-16 string.
*
*********************************************************************************/
u16 *CsrUtf16StrCpy(u16 *target, const u16 *source)
{
    if (source) /* if source is not NULL*/
    {
        CsrMemCpy(target, source, (CsrUtf16StrLen(source) + 1) * sizeof(u16));
        return target;
    }
    else
    {
        return NULL;
    }
}

/********************************************************************************
*
*   Name:           CsrUtf16StringDuplicate
*
*   Description:    The function allocates a new pointer and copies the input to
*                   the new pointer.
*
*   Input:          0-terminated UTF-16 string.
*
*   Output:         Allocated variable0-terminated UTF-16 string.
*
*********************************************************************************/
u16 *CsrUtf16StringDuplicate(const u16 *source)
{
    u16 *target = NULL;
    u32 length;

    if (source) /* if source is not NULL*/
    {
        length = (CsrUtf16StrLen(source) + 1) * sizeof(u16);
        target = (u16 *) CsrPmemAlloc(length);
        CsrMemCpy(target, source, length);
    }
    return target;
}

/********************************************************************************
*
*   Name:           CsrUtf16StrICmp
*
*   Description:    The function compares two UTF-16 strings.
*
*   Input:          Two 0-terminated UTF-16 string.
*
*   Output:         0: if the strings are identical.
*
*********************************************************************************/
u16 CsrUtf16StrICmp(const u16 *string1, const u16 *string2)
{
    while (*string1 || *string2)
    {
        if (CAPITAL(*string1) != CAPITAL(*string2))
        {
            return *string1 - *string2;
        }
        string1++;
        string2++;
    }

    return 0;
}

/********************************************************************************
*
*   Name:           CsrUtf16StrNICmp
*
*   Description:    The function compares upto count number of elements in the
*                   two UTF-16 string.
*
*   Input:          Two 0-terminated UTF-16 string and a maximum
*                   number of elements to check.
*
*   Output:         0: if the strings are identical.
*
*********************************************************************************/
u16 CsrUtf16StrNICmp(const u16 *string1, const u16 *string2, u32 count)
{
    while ((*string1 || *string2) && count--)
    {
        if (CAPITAL(*string1) != CAPITAL(*string2))
        {
            return *string1 - *string2;
        }
        string1++;
        string2++;
    }

    return 0;
}

/********************************************************************************
*
*   Name:           CsrUtf16String2XML
*
*   Description:    The function converts an unicoded string (UTF-16) into an unicoded XML
*                   string where some special characters are encoded according to
*                   the XML spec.
*
*   Input:          A unicoded string (UTF-16) which is freed.
*
*   Output:         A new unicoded string (UTF-16) containing the converted output.
*
*********************************************************************************/
u16 *CsrUtf16String2XML(u16 *str)
{
    u16 *scanString;
    u16 *outputString = NULL;
    u16 *resultString = str;
    u32 stringLength = 0;
    u8 encodeChars = FALSE;

    scanString = str;
    if (scanString)
    {
        while (*scanString)
        {
            if (*scanString == L'&')
            {
                stringLength += 5;
                encodeChars = TRUE;
            }
            else if ((*scanString == L'<') || (*scanString == L'>'))
            {
                stringLength += 4;
                encodeChars = TRUE;
            }
            else
            {
                stringLength++;
            }

            scanString++;
        }

        stringLength++;

        if (encodeChars)
        {
            resultString = outputString = CsrPmemAlloc(stringLength * sizeof(u16));

            scanString = str;

            while (*scanString)
            {
                if (*scanString == L'&')
                {
                    *outputString++ = '&';
                    *outputString++ = 'a';
                    *outputString++ = 'm';
                    *outputString++ = 'p';
                    *outputString++ = ';';
                }
                else if (*scanString == L'<')
                {
                    *outputString++ = '&';
                    *outputString++ = 'l';
                    *outputString++ = 't';
                    *outputString++ = ';';
                }
                else if (*scanString == L'>')
                {
                    *outputString++ = '&';
                    *outputString++ = 'g';
                    *outputString++ = 't';
                    *outputString++ = ';';
                }
                else
                {
                    *outputString++ = *scanString;
                }

                scanString++;
            }

            *outputString++ = 0;

            CsrPmemFree(str);
        }
    }

    return resultString;
}

/********************************************************************************
*
*   Name:           CsrXML2Utf16String
*
*   Description:    The function converts an unicoded XML string into an unicoded
*                   string (UTF-16) where some special XML characters are decoded according to
*                   the XML spec.
*
*   Input:          A unicoded XML string which is freed.
*
*   Output:         A new unicoded pointer containing the decoded output.
*
*********************************************************************************/
u16 *CsrXML2Utf16String(u16 *str)
{
    u16 *scanString;
    u16 *outputString = NULL;
    u16 *resultString = str;
    u32 stringLength = 0;
    u8 encodeChars = FALSE;

    scanString = str;
    if (scanString)
    {
        while (*scanString)
        {
            if (*scanString == (u16) L'&')
            {
                scanString++;

                if (!CsrUtf16StrNICmp(scanString, (u16 *) L"AMP;", 4))
                {
                    scanString += 3;
                    encodeChars = TRUE;
                }
                else if (!CsrUtf16StrNICmp(scanString, (u16 *) L"LT;", 3))
                {
                    scanString += 2;
                    encodeChars = TRUE;
                }
                else if (!CsrUtf16StrNICmp(scanString, (u16 *) L"GT;", 3))
                {
                    scanString += 2;
                    encodeChars = TRUE;
                }
                if (!CsrUtf16StrNICmp(scanString, (u16 *) L"APOS;", 5))
                {
                    scanString += 4;
                    encodeChars = TRUE;
                }
                if (!CsrUtf16StrNICmp(scanString, (u16 *) L"QUOT;", 5))
                {
                    scanString += 4;
                    encodeChars = TRUE;
                }
                else
                {
                    scanString--;
                }
            }

            stringLength++;
            scanString++;
        }

        stringLength++;

        if (encodeChars)
        {
            resultString = outputString = CsrPmemAlloc(stringLength * sizeof(u16));

            scanString = str;

            while (*scanString)
            {
                if (*scanString == L'&')
                {
                    scanString++;

                    if (!CsrUtf16StrNICmp(scanString, (u16 *) L"AMP;", 4))
                    {
                        *outputString++ = L'&';
                        scanString += 3;
                    }
                    else if (!CsrUtf16StrNICmp(scanString, (u16 *) L"LT;", 3))
                    {
                        *outputString++ = L'<';
                        scanString += 2;
                    }
                    else if (!CsrUtf16StrNICmp(scanString, (u16 *) L"GT;", 3))
                    {
                        *outputString++ = L'>';
                        scanString += 2;
                    }
                    else if (!CsrUtf16StrNICmp(scanString, (u16 *) L"APOS;", 5))
                    {
                        *outputString++ = L'\'';
                        scanString += 4;
                    }
                    else if (!CsrUtf16StrNICmp(scanString, (u16 *) L"QUOT;", 5))
                    {
                        *outputString++ = L'\"';
                        scanString += 4;
                    }
                    else
                    {
                        *outputString++ = L'&';
                        scanString--;
                    }
                }
                else
                {
                    *outputString++ = *scanString;
                }

                scanString++;
            }

            *outputString++ = 0;

            CsrPmemFree(str);
        }
    }

    return resultString;
}

s32 CsrUtf8StrCmp(const u8 *string1, const u8 *string2)
{
    return CsrStrCmp((const char *) string1, (const char *) string2);
}

s32 CsrUtf8StrNCmp(const u8 *string1, const u8 *string2, size_t count)
{
    return CsrStrNCmp((const char *) string1, (const char *) string2, count);
}

u32 CsrUtf8StringLengthInBytes(const u8 *string)
{
    size_t length = 0;
    if (string)
    {
        length = CsrStrLen((const char *) string);
    }
    return (u32) length;
}

u8 *CsrUtf8StrTruncate(u8 *target, size_t count)
{
    size_t lastByte = count - 1;

    target[count] = '\0';

    if (count && (target[lastByte] & 0x80))
    {
        /* the last byte contains non-ascii char */
        if (target[lastByte] & 0x40)
        {
            /* multi-byte char starting just before truncation */
            target[lastByte] = '\0';
        }
        else if ((target[lastByte - 1] & 0xE0) == 0xE0)
        {
            /* 3-byte char starting 2 bytes before truncation */
            target[lastByte - 1] = '\0';
        }
        else if ((target[lastByte - 2] & 0xF0) == 0xF0)
        {
            /* 4-byte char starting 3 bytes before truncation */
            target[lastByte - 2] = '\0';
        }
    }

    return target;
}

u8 *CsrUtf8StrDup(const u8 *source)
{
    return (u8 *) CsrStrDup((const char *) source);
}
