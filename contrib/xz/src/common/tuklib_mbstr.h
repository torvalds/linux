///////////////////////////////////////////////////////////////////////////////
//
/// \file       tuklib_mstr.h
/// \brief      Utility functions for handling multibyte strings
///
/// If not enough multibyte string support is available in the C library,
/// these functions keep working with the assumption that all strings
/// are in a single-byte character set without combining characters, e.g.
/// US-ASCII or ISO-8859-*.
//
//  Author:     Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#ifndef TUKLIB_MBSTR_H
#define TUKLIB_MBSTR_H

#include "tuklib_common.h"
TUKLIB_DECLS_BEGIN

#define tuklib_mbstr_width TUKLIB_SYMBOL(tuklib_mbstr_width)
extern size_t tuklib_mbstr_width(const char *str, size_t *bytes);
///<
/// \brief      Get the number of columns needed for the multibyte string
///
/// This is somewhat similar to wcswidth() but works on multibyte strings.
///
/// \param      str         String whose width is to be calculated. If the
///                         current locale uses a multibyte character set
///                         that has shift states, the string must begin
///                         and end in the initial shift state.
/// \param      bytes       If this is not NULL, *bytes is set to the
///                         value returned by strlen(str) (even if an
///                         error occurs when calculating the width).
///
/// \return     On success, the number of columns needed to display the
///             string e.g. in a terminal emulator is returned. On error,
///             (size_t)-1 is returned. Possible errors include invalid,
///             partial, or non-printable multibyte character in str, or
///             that str doesn't end in the initial shift state.

#define tuklib_mbstr_fw TUKLIB_SYMBOL(tuklib_mbstr_fw)
extern int tuklib_mbstr_fw(const char *str, int columns_min);
///<
/// \brief      Get the field width for printf() e.g. to align table columns
///
/// Printing simple tables to a terminal can be done using the field field
/// feature in the printf() format string, but it works only with single-byte
/// character sets. To do the same with multibyte strings, tuklib_mbstr_fw()
/// can be used to calculate appropriate field width.
///
/// The behavior of this function is undefined, if
///   - str is NULL or not terminated with '\0';
///   - columns_min <= 0; or
///   - the calculated field width exceeds INT_MAX.
///
/// \return     If tuklib_mbstr_width(str, NULL) fails, -1 is returned.
///             If str needs more columns than columns_min, zero is returned.
///             Otherwise a positive integer is returned, which can be
///             used as the field width, e.g. printf("%*s", fw, str).

TUKLIB_DECLS_END
#endif
