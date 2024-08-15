/*
 * Copyright (c) 2004 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Mellanox Technologies. All rights reserved.
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

#ifndef MTHCA_CONFIG_REG_H
#define MTHCA_CONFIG_REG_H

#define MTHCA_HCR_BASE         0x80680
#define MTHCA_HCR_SIZE         0x0001c
#define MTHCA_ECR_BASE         0x80700
#define MTHCA_ECR_SIZE         0x00008
#define MTHCA_ECR_CLR_BASE     0x80708
#define MTHCA_ECR_CLR_SIZE     0x00008
#define MTHCA_MAP_ECR_SIZE     (MTHCA_ECR_SIZE + MTHCA_ECR_CLR_SIZE)
#define MTHCA_CLR_INT_BASE     0xf00d8
#define MTHCA_CLR_INT_SIZE     0x00008
#define MTHCA_EQ_SET_CI_SIZE   (8 * 32)

#endif /* MTHCA_CONFIG_REG_H */
