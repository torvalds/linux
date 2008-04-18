#ifndef _IPATH_7220_H
#define _IPATH_7220_H
/*
 * Copyright (c) 2007 QLogic Corporation. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*
 * This header file provides the declarations and common definitions
 * for (mostly) manipulation of the SerDes blocks within the IBA7220.
 * the functions declared should only be called from within other
 * 7220-related files such as ipath_iba7220.c or ipath_sd7220.c.
 */
int ipath_sd7220_presets(struct ipath_devdata *dd);
int ipath_sd7220_init(struct ipath_devdata *dd, int was_reset);
int ipath_sd7220_prog_ld(struct ipath_devdata *dd, int sdnum, u8 *img,
	int len, int offset);
int ipath_sd7220_prog_vfy(struct ipath_devdata *dd, int sdnum, const u8 *img,
	int len, int offset);
/*
 * Below used for sdnum parameter, selecting one of the two sections
 * used for PCIe, or the single SerDes used for IB, which is the
 * only one currently used
 */
#define IB_7220_SERDES 2

int ipath_sd7220_ib_load(struct ipath_devdata *dd);
int ipath_sd7220_ib_vfy(struct ipath_devdata *dd);

#endif /* _IPATH_7220_H */
