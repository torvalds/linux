/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * hvconsole.h
 * Copyright (C) 2004 Ryan S Arnold, IBM Corporation
 *
 * LPAR console support.
 */

#ifndef _PPC64_HVCONSOLE_H
#define _PPC64_HVCONSOLE_H
#ifdef __KERNEL__

/*
 * PSeries firmware will only send/recv up to 16 bytes of character data per
 * hcall.
 */
#define MAX_VIO_PUT_CHARS	16
#define SIZE_VIO_GET_CHARS	16

/*
 * Vio firmware always attempts to fetch MAX_VIO_GET_CHARS chars.  The 'count'
 * parm is included to conform to put_chars() function pointer template
 */
extern ssize_t hvc_get_chars(uint32_t vtermno, u8 *buf, size_t count);
extern ssize_t hvc_put_chars(uint32_t vtermno, const u8 *buf, size_t count);

/* Provided by HVC VIO */
void hvc_vio_init_early(void);

#endif /* __KERNEL__ */
#endif /* _PPC64_HVCONSOLE_H */
