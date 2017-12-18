/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#ifndef __STRING_SUPPORT_H_INCLUDED__
#define __STRING_SUPPORT_H_INCLUDED__
#include <platform_support.h>
#include <type_support.h>

#if !defined(_MSC_VER)
/*
 * For all non microsoft cases, we need the following functions
 */


/** @brief Copy from src_buf to dest_buf.
 *
 * @param[out] dest_buf. Destination buffer to copy to
 * @param[in]  dest_size. The size of the destination buffer in bytes
 * @param[in]  src_buf. The source buffer
 * @param[in]  src_size. The size of the source buffer in bytes
 * @return     0 on success, error code on failure
 * @return     EINVAL on Invalid arguments
 * @return     ERANGE on Destination size too small
 */
static inline int memcpy_s(
	void* dest_buf,
	size_t dest_size,
	const void* src_buf,
	size_t src_size)
{
	if ((src_buf == NULL) || (dest_buf == NULL)) {
		/* Invalid arguments*/
		return EINVAL;
	}

	if ((dest_size < src_size) || (src_size == 0)) {
		/* Destination too small*/
		return ERANGE;
	}

	memcpy(dest_buf, src_buf, src_size);
	return 0;
}

/** @brief Get the length of the string, excluding the null terminator
 *
 * @param[in]  src_str. The source string
 * @param[in]  max_len. Look only for max_len bytes in the string
 * @return     Return the string length excluding null character
 * @return     Return max_len if no null character in the first max_len bytes
 * @return     Returns 0 if src_str is NULL
 */
static size_t strnlen_s(
	const char* src_str,
	size_t max_len)
{
	size_t ix;
	if (src_str == NULL) {
		/* Invalid arguments*/
		return 0;
	}

	for (ix = 0; ix < max_len && src_str[ix] != '\0'; ix++)
		;

	/* On Error, it will return src_size == max_len*/
	return ix;
}

/** @brief Copy string from src_str to dest_str
 *
 * @param[out] dest_str. Destination buffer to copy to
 * @param[in]  dest_size. The size of the destination buffer in bytes
 * @param[in]  src_str. The source buffer
 * @param[in]  src_size. The size of the source buffer in bytes
 * @return     Returns 0 on success
 * @return     Returns EINVAL on invalid arguments
 * @return     Returns ERANGE on destination size too small
 */
static inline int strncpy_s(
	char* dest_str,
	size_t dest_size,
	const char* src_str,
	size_t src_size)
{
	size_t len;
	if (dest_str == NULL) {
		/* Invalid arguments*/
		return EINVAL;
	}

	if ((src_str == NULL) || (dest_size == 0)) {
		/* Invalid arguments*/
		dest_str[0] = '\0';
		return EINVAL;
	}

	len = strnlen_s(src_str, src_size);

	if (len >= dest_size) {
		/* Destination too small*/
		dest_str[0] = '\0';
		return ERANGE;
	}

	/* dest_str is big enough for the len */
	strncpy(dest_str, src_str, len);
	dest_str[len] = '\0';
	return 0;
}

/** @brief Copy string from src_str to dest_str
 *
 * @param[out] dest_str. Destination buffer to copy to
 * @param[in]  dest_size. The size of the destination buffer in bytes
 * @param[in]  src_str. The source buffer
 * @return     Returns 0 on success
 * @return     Returns EINVAL on invalid arguments
 * @return     Returns ERANGE on destination size too small
 */
static inline int strcpy_s(
	char* dest_str,
	size_t dest_size,
	const char* src_str)
{
	size_t len;
	if (dest_str == NULL) {
		/* Invalid arguments*/
		return EINVAL;
	}

	if ((src_str == NULL) || (dest_size == 0)) {
		/* Invalid arguments*/
		dest_str[0] = '\0';
		return EINVAL;
	}

	len = strnlen_s(src_str, dest_size);

	if (len >= dest_size) {
		/* Destination too small*/
		dest_str[0] = '\0';
		return ERANGE;
	}

	/* dest_str is big enough for the len */
	strncpy(dest_str, src_str, len);
	dest_str[len] = '\0';
	return 0;
}

#endif /*!defined(_MSC_VER)*/

#endif /* __STRING_SUPPORT_H_INCLUDED__ */
