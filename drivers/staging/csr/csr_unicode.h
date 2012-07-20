#ifndef CSR_UNICODE_H__
#define CSR_UNICODE_H__
/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2010
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

#ifdef __cplusplus
extern "C" {
#endif

u16 *CsrUint32ToUtf16String(u32 number);

u32 CsrUtf16StringToUint32(const u16 *unicodeString);
u32 CsrUtf16StrLen(const u16 *unicodeString);

u8 *CsrUtf16String2Utf8(const u16 *source);

u16 *CsrUtf82Utf16String(const u8 *utf8String);

u16 *CsrUtf16StrCpy(u16 *target, const u16 *source);
u16 *CsrUtf16StringDuplicate(const u16 *source);

u16 CsrUtf16StrICmp(const u16 *string1, const u16 *string2);
u16 CsrUtf16StrNICmp(const u16 *string1, const u16 *string2, u32 count);

u16 *CsrUtf16MemCpy(u16 *dest, const u16 *src, u32 count);
u16 *CsrUtf16ConcatenateTexts(const u16 *inputText1, const u16 *inputText2,
    const u16 *inputText3, const u16 *inputText4);

u16 *CsrUtf16String2XML(u16 *str);
u16 *CsrXML2Utf16String(u16 *str);

u32 CsrUtf8StringLengthInBytes(const u8 *string);

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
u8 *CsrUtf8StrTruncate(u8 *target, size_t count);

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
u8 *CsrUtf8StrDup(const u8 *source);

/*
 * UCS2
 *
 * D-13157
 */
typedef u8 CsrUcs2String;

size_t CsrUcs2ByteStrLen(const CsrUcs2String *ucs2String);
size_t CsrConverterUcs2ByteStrLen(const CsrUcs2String *str);

u8 *CsrUcs2ByteString2Utf8(const CsrUcs2String *ucs2String);
CsrUcs2String *CsrUtf82Ucs2ByteString(const u8 *utf8String);

u8 *CsrUtf16String2Ucs2ByteString(const u16 *source);
u16 *CsrUcs2ByteString2Utf16String(const u8 *source);

#ifdef __cplusplus
}
#endif

#endif
