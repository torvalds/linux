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
/*                                                                          */
/*  Module Name : cwm.h                                                     */
/*                                                                          */
/*  Abstract                                                                */
/*      This module contains channel width relatived functions.             */
/*                                                                          */
/*  NOTES                                                                   */
/*      None                                                                */
/*                                                                          */
/****************************************************************************/
/*Revision History:                                                         */
/*    Who         When        What                                          */
/*    --------    --------    ----------------------------------------------*/
/*                                                                          */
/*    Honda       3-19-07     created                                       */
/*                                                                          */
/****************************************************************************/

#ifndef _CWM_H
#define _CWM_H

#define ATH_CWM_EXTCH_BUSY_THRESHOLD  30  /* Extension Channel Busy Threshold (0-100%) */

void zfCwmInit(zdev_t* dev);
void zfCoreCwmBusy(zdev_t* dev, u16_t busy);
u16_t zfCwmIsExtChanBusy(u32_t ctlBusy, u32_t extBusy);



#endif /* #ifndef _CWM_H */
