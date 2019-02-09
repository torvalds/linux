/*
 * Copyright (C) 2018 Netronome Systems, Inc.
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

#include <linux/bitfield.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/types.h>

#include "nfp_net_ctrl.h"
#include "nfp_net.h"

static void nfp_net_tlv_caps_reset(struct nfp_net_tlv_caps *caps)
{
	memset(caps, 0, sizeof(*caps));
	caps->me_freq_mhz = 1200;
	caps->mbox_off = NFP_NET_CFG_MBOX_BASE;
	caps->mbox_len = NFP_NET_CFG_MBOX_VAL_MAX_SZ;
}

int nfp_net_tlv_caps_parse(struct device *dev, u8 __iomem *ctrl_mem,
			   struct nfp_net_tlv_caps *caps)
{
	u8 __iomem *data = ctrl_mem + NFP_NET_CFG_TLV_BASE;
	u8 __iomem *end = ctrl_mem + NFP_NET_CFG_BAR_SZ;
	u32 hdr;

	nfp_net_tlv_caps_reset(caps);

	hdr = readl(data);
	if (!hdr)
		return 0;

	while (true) {
		unsigned int length, offset;
		u32 hdr = readl(data);

		length = FIELD_GET(NFP_NET_CFG_TLV_HEADER_LENGTH, hdr);
		offset = data - ctrl_mem;

		/* Advance past the header */
		data += 4;

		if (length % NFP_NET_CFG_TLV_LENGTH_INC) {
			dev_err(dev, "TLV size not multiple of %u len:%u\n",
				NFP_NET_CFG_TLV_LENGTH_INC, length);
			return -EINVAL;
		}
		if (data + length > end) {
			dev_err(dev, "oversized TLV offset:%u len:%u\n",
				offset, length);
			return -EINVAL;
		}

		switch (FIELD_GET(NFP_NET_CFG_TLV_HEADER_TYPE, hdr)) {
		case NFP_NET_CFG_TLV_TYPE_UNKNOWN:
			dev_err(dev, "NULL TLV at offset:%u\n", offset);
			return -EINVAL;
		case NFP_NET_CFG_TLV_TYPE_RESERVED:
			break;
		case NFP_NET_CFG_TLV_TYPE_END:
			if (!length)
				return 0;

			dev_err(dev, "END TLV should be empty, has len:%d\n",
				length);
			return -EINVAL;
		case NFP_NET_CFG_TLV_TYPE_ME_FREQ:
			if (length != 4) {
				dev_err(dev,
					"ME FREQ TLV should be 4B, is %dB\n",
					length);
				return -EINVAL;
			}

			caps->me_freq_mhz = readl(data);
			break;
		case NFP_NET_CFG_TLV_TYPE_MBOX:
			if (!length) {
				caps->mbox_off = 0;
				caps->mbox_len = 0;
			} else {
				caps->mbox_off = data - ctrl_mem;
				caps->mbox_len = length;
			}
			break;
		default:
			if (!FIELD_GET(NFP_NET_CFG_TLV_HEADER_REQUIRED, hdr))
				break;

			dev_err(dev, "unknown TLV type:%u offset:%u len:%u\n",
				FIELD_GET(NFP_NET_CFG_TLV_HEADER_TYPE, hdr),
				offset, length);
			return -EINVAL;
		}

		data += length;
		if (data + 4 > end) {
			dev_err(dev, "reached end of BAR without END TLV\n");
			return -EINVAL;
		}
	}

	/* Not reached */
	return -EINVAL;
}
