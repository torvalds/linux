/*
 * Copyright(c) 2007 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Maintained at www.Open-FCoE.org
 */

/*
 * Frame allocation.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/crc32.h>

#include <scsi/fc_frame.h>

/*
 * Check the CRC in a frame.
 */
u32 fc_frame_crc_check(struct fc_frame *fp)
{
	u32 crc;
	u32 error;
	const u8 *bp;
	unsigned int len;

	WARN_ON(!fc_frame_is_linear(fp));
	fr_flags(fp) &= ~FCPHF_CRC_UNCHECKED;
	len = (fr_len(fp) + 3) & ~3;	/* round up length to include fill */
	bp = (const u8 *) fr_hdr(fp);
	crc = ~crc32(~0, bp, len);
	error = crc ^ fr_crc(fp);
	return error;
}
EXPORT_SYMBOL(fc_frame_crc_check);

/*
 * Allocate a frame intended to be sent via fcoe_xmit.
 * Get an sk_buff for the frame and set the length.
 */
struct fc_frame *__fc_frame_alloc(size_t len)
{
	struct fc_frame *fp;
	struct sk_buff *skb;

	WARN_ON((len % sizeof(u32)) != 0);
	len += sizeof(struct fc_frame_header);
	skb = dev_alloc_skb(len + FC_FRAME_HEADROOM + FC_FRAME_TAILROOM);
	if (!skb)
		return NULL;
	fp = (struct fc_frame *) skb;
	fc_frame_init(fp);
	skb_reserve(skb, FC_FRAME_HEADROOM);
	skb_put(skb, len);
	return fp;
}
EXPORT_SYMBOL(__fc_frame_alloc);


struct fc_frame *fc_frame_alloc_fill(struct fc_lport *lp, size_t payload_len)
{
	struct fc_frame *fp;
	size_t fill;

	fill = payload_len % 4;
	if (fill != 0)
		fill = 4 - fill;
	fp = __fc_frame_alloc(payload_len + fill);
	if (fp) {
		memset((char *) fr_hdr(fp) + payload_len, 0, fill);
		/* trim is OK, we just allocated it so there are no fragments */
		skb_trim(fp_skb(fp),
			 payload_len + sizeof(struct fc_frame_header));
	}
	return fp;
}
