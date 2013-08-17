/*
 * Copyright (c) 2006, 2007 QLogic Corporation. All rights reserved.
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
 * This file is conditionally built on PowerPC only.  Otherwise weak symbol
 * versions of the functions exported from here are used.
 */

#include "ipath_kernel.h"

/**
 * ipath_enable_wc - enable write combining for MMIO writes to the device
 * @dd: infinipath device
 *
 * Nothing to do on PowerPC, so just return without error.
 */
int ipath_enable_wc(struct ipath_devdata *dd)
{
	return 0;
}

/**
 * ipath_unordered_wc - indicate whether write combining is unordered
 *
 * Because our performance depends on our ability to do write
 * combining mmio writes in the most efficient way, we need to
 * know if we are on a processor that may reorder stores when
 * write combining.
 */
int ipath_unordered_wc(void)
{
	return 1;
}
