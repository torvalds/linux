#ifndef CSR_UNICODE_H__
#define CSR_UNICODE_H__
/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2010
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

#include "csr_types.h"

#ifdef __cplusplus
extern "C" {
#endif

CsrUtf16String *CsrUint32ToUtf16String(CsrUint32 number);

CsrUint32 CsrUtf16StringToUint32(const CsrUtf16String *unicodeString);
CsrUint32 CsrUtf16StrLen(const CsrUtf16String *unicodeString);

CsrUtf8String *CsrUtf16String2Utf8(const CsrUtf16String *source);

CsrUtf16String *CsrUtf82Utf16String(const CsrUtf8String *utf8String);

CsrUtf16String *CsrUtf16StrCpy(CsrUtf16String *target, const CsrUtf16String *source);
CsrUtf16String *CsrUtf16StringDuplicate(const CsrUtf16String *source);

CsrUint16 CsrUtf16StrICmp(const CsrUtf16String *string1, const CsrUtf16String *string2);
CsrUint16 CsrUtf16StrNICmp(const CsrUtf16String *string1, const CsrUtf16String *string2, CsrUint32 count);

CsrUtf16String *CsrUtf16MemCpy(CsrUtf16String *dest, const CsrUtf16String *src, CsrUint32 count);
CsrUtf16String *CsrUtf16ConcatenateTexts(const CsrUtf16String *inputText1, const CsrUtf16String *inputText2,
    const CsrUtf16String *inputText3, const CsrUtf16String *inputText4);

CsrUtf16String *CsrUtf16String2XML(CsrUtf16String *str);
CsrUtf16String *CsrXML2Utf16String(CsrUtf16String *str);

CsrInt32 CsrUtf8StrCmp(const CsrUtf8String *string1, const CsrUtf8String *string2);
CsrInt32 CsrUtf8StrNCmp(const CsrUtf8String *string1, const CsrUtf8String *string2, CsrSize count);
CsrUint32 CsrUtf8StringLengthInBytes(const CsrUtf8String *string);

/*******************************************************************************

    NAME
        CsrUtf8StrTruncate

    DESCRIPTION
        In-place truncate a string on a UTF-8 character boundary by writing a
        null character somewhere in the range target[count - 3]:target[count].

        Please note that memory passed must be at least of length count + 1, to
        ensure space for a full length string that is terminated at
        target[count], in the event that target[count - 1] is the final byte of
        a UTF-8 character.

    PARAMETERS
        target - Target string to truncate.
        count - The desired length, in bytes, of the resulting string. Depending
                on the contents, the resulting string length will be between
                count - 3 and count.

    RETURNS
        Returns target

*******************************************************************************/
CsrUtf8String *CsrUtf8StrTruncate(CsrUtf8String *target, CsrSize count);

/*******************************************************************************

    NAME
        CsrUtf8StrCpy

    DESCRIPTION
        Copies the null terminated UTF-8 string pointed at by source into the
        memory pointed at by target, including the terminating null character.

        To avoid overflows, the size of the memory pointed at by target shall be
        long enough to contain the same UTF-8 string as source (including the
        terminating null character), and should not overlap in memory with
        source.

    PARAMETERS
        target - Pointer to the target memory where the content is to be copied.
        source - UTF-8 string to be copied.

    RETURNS
        Returns target

*******************************************************************************/
CsrUtf8String *CsrUtf8StrCpy(CsrUtf8String *target, const CsrUtf8String *source);

/*******************************************************************************

    NAME
        CsrUtf8StrNCpy

    DESCRIPTION
        Copies the first count bytes of source to target. If the end of the
        source UTF-8 string (which is signaled by a null-character) is found
        before count bytes have been copied, target is padded with null
        characters until a total of count bytes have been written to it.

        No null-character is implicitly appended to the end of target, so target
        will only be null-terminated if the length of the UTF-8 string in source
        is less than count.

    PARAMETERS
        target - Pointer to the target memory where the content is to be copied.
        source - UTF-8 string to be copied.
        count - Maximum number of bytes to be written to target.

    RETURNS
        Returns target

*******************************************************************************/
CsrUtf8String *CsrUtf8StrNCpy(CsrUtf8String *target, const CsrUtf8String *source, CsrSize count);

/*******************************************************************************

    NAME
        CsrUtf8StrNCpyZero

    DESCRIPTION
        Equivalent to CsrUtf8StrNCpy, but if the length of source is equal to or
        greater than count the target string is truncated on a UTF-8 character
        boundary by writing a null character somewhere in the range
        target[count - 4]:target[count - 1], leaving the target string
        unconditionally null terminated in all cases.

        Please note that if the length of source is shorter than count, no
        truncation will be applied, and the target string will be a one to one
        copy of source.

    PARAMETERS
        target - Pointer to the target memory where the content is to be copied.
        source - UTF-8 string to be copied.
        count - Maximum number of bytes to be written to target.

    RETURNS
        Returns target

*******************************************************************************/
CsrUtf8String *CsrUtf8StrNCpyZero(CsrUtf8String *target, const CsrUtf8String *source, CsrSize count);

/*******************************************************************************

    NAME
        CsrUtf8StrDup

    DESCRIPTION
        This function will allocate memory and copy the source string into the
        allocated memory, which is then returned as a duplicate of the original
        string. The memory returned must be freed by calling CsrPmemFree when
        the duplicate is no longer needed.

    PARAMETERS
        source - UTF-8 string to be duplicated.

    RETURNS
        Returns a duplicate of source.

*******************************************************************************/
CsrUtf8String *CsrUtf8StrDup(const CsrUtf8String *source);

CsrUtf8String *CsrUtf8StringConcatenateTexts(const CsrUtf8String *inputText1, const CsrUtf8String *inputText2, const CsrUtf8String *inputText3, const CsrUtf8String *inputText4);

/*
 * UCS2
 *
 * D-13157
 */
typedef u8 CsrUcs2String;

CsrSize CsrUcs2ByteStrLen(const CsrUcs2String *ucs2String);
CsrSize CsrConverterUcs2ByteStrLen(const CsrUcs2String *str);

u8 *CsrUcs2ByteString2Utf8(const CsrUcs2String *ucs2String);
CsrUcs2String *CsrUtf82Ucs2ByteString(const u8 *utf8String);

u8 *CsrUtf16String2Ucs2ByteString(const CsrUtf16String *source);
CsrUtf16String *CsrUcs2ByteString2Utf16String(const u8 *source);

#ifdef __cplusplus
}
#endif

#endif
