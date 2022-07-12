/*
 * Broadcom Secure Standard Library.
 *
 * Portions of this code are copyright (c) 2022 Cypress Semiconductor Corporation
 *
 * Copyright (C) 1999-2017, Broadcom Corporation
 *
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 *
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 *
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * * $Id $
 */

#ifndef	_bcmstdlib_s_h_
#define	_bcmstdlib_s_h_

#ifndef BWL_NO_INTERNAL_STDLIB_SUPPORT
#if !defined(__STDC_WANT_SECURE_LIB__) && !(defined(__STDC_LIB_EXT1__) && \
	defined(__STDC_WANT_LIB_EXT1__))
extern int memmove_s(void *dest, size_t destsz, const void *src, size_t n);
extern int memcpy_s(void *dest, size_t destsz, const void *src, size_t n);
extern int memset_s(void *dest, size_t destsz, int c, size_t n);
#endif /* !__STDC_WANT_SECURE_LIB__ && !(__STDC_LIB_EXT1__ && __STDC_WANT_LIB_EXT1__) */
#if !defined(FREEBSD) && !defined(BCM_USE_PLATFORM_STRLCPY)
extern size_t strlcpy(char *dest, const char *src, size_t size);
#endif // endif
extern size_t strlcat_s(char *dest, const char *src, size_t size);
#endif /* !BWL_NO_INTERNAL_STDLIB_SUPPORT */
#endif /* _bcmstdlib_s_h_ */
