/*
 * Copyright (C) 2017 Netronome Systems, Inc.
 *
 * This software is dual licensed under the GNU General License Version 2,
 * June 1991 as shown in the file COPYING in the top-level directory of this
 * source tree or the BSD 2-Clause License provided below.  You have the
 * option to license this software under the complete terms of either license.
 *
 * The BSD 2-Clause License:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      1. Redistributions of source code must retain the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer.
 *
 *      2. Redistributions in binary form must reproduce the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer in the documentation and/or other materials
 *         provided with the distribution.
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

#include <linux/kernel.h>
#include <linux/slab.h>

#include "nfp.h"
#include "nfp_nsp.h"

struct nsp_identify {
	u8 version[40];
	u8 flags;
	u8 br_primary;
	u8 br_secondary;
	u8 br_nsp;
	__le16 primary;
	__le16 secondary;
	__le16 nsp;
	__le16 reserved;
};

struct nfp_nsp_identify *__nfp_nsp_identify(struct nfp_nsp *nsp)
{
	struct nfp_nsp_identify *nspi = NULL;
	struct nsp_identify *ni;
	int ret;

	if (nfp_nsp_get_abi_ver_minor(nsp) < 15)
		return NULL;

	ni = kzalloc(sizeof(*ni), GFP_KERNEL);
	if (!ni)
		return NULL;

	ret = nfp_nsp_read_identify(nsp, ni, sizeof(*ni));
	if (ret < 0) {
		nfp_err(nfp_nsp_cpp(nsp), "reading bsp version failed %d\n",
			ret);
		goto exit_free;
	}

	nspi = kzalloc(sizeof(*nspi), GFP_KERNEL);
	if (!nspi)
		goto exit_free;

	memcpy(nspi->version, ni->version, sizeof(nspi->version));
	nspi->version[sizeof(nspi->version) - 1] = '\0';
	nspi->flags = ni->flags;
	nspi->br_primary = ni->br_primary;
	nspi->br_secondary = ni->br_secondary;
	nspi->br_nsp = ni->br_nsp;
	nspi->primary = le16_to_cpu(ni->primary);
	nspi->secondary = le16_to_cpu(ni->secondary);
	nspi->nsp = le16_to_cpu(ni->nsp);

exit_free:
	kfree(ni);
	return nspi;
}
