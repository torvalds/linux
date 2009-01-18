/*
 * Copyright (c) 2007-2008 Atheros Communications Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
/*                                                                      */
/*  Module Name : zdusb.h                                               */
/*                                                                      */
/*  Abstract                                                            */
/*     This module contains definitions for USB device driver           */
/*                                                                      */
/*  NOTES                                                               */
/*     Platform dependent.                                              */
/*                                                                      */
/************************************************************************/

#ifndef _ZDUSB_H
#define _ZDUSB_H

#ifndef DRIVER_NAME
#define DRIVER_NAME             "arusb"
#endif

#define VERSIONID               "0.0.0.999"

/* Define these values to match your device */
#define VENDOR_ATHR             0x0CF3  //Atheros
#define PRODUCT_AR9170          0x9170

#define VENDOR_DLINK            0x07D1  //Dlink
#define PRODUCT_DWA160A         0x3C10

#endif
