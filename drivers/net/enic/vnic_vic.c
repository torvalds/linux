/*
 * Copyright 2010 Cisco Systems, Inc.  All rights reserved.
 *
 * This program is free software; you may redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/slab.h>

#include "vnic_vic.h"

struct vic_provinfo *vic_provinfo_alloc(gfp_t flags, u8 *oui, u8 type)
{
	struct vic_provinfo *vp;

	if (!oui)
		return NULL;

	vp = kzalloc(VIC_PROVINFO_MAX_DATA, flags);
	if (!vp)
		return NULL;

	memcpy(vp->oui, oui, sizeof(vp->oui));
	vp->type = type;
	vp->length = htonl(sizeof(vp->num_tlvs));

	return vp;
}

void vic_provinfo_free(struct vic_provinfo *vp)
{
	kfree(vp);
}

int vic_provinfo_add_tlv(struct vic_provinfo *vp, u16 type, u16 length,
	void *value)
{
	struct vic_provinfo_tlv *tlv;

	if (!vp || !value)
		return -EINVAL;

	if (ntohl(vp->length) + offsetof(struct vic_provinfo_tlv, value) +
		length > VIC_PROVINFO_MAX_TLV_DATA)
		return -ENOMEM;

	tlv = (struct vic_provinfo_tlv *)((u8 *)vp->tlv +
		ntohl(vp->length) - sizeof(vp->num_tlvs));

	tlv->type = htons(type);
	tlv->length = htons(length);
	memcpy(tlv->value, value, length);

	vp->num_tlvs = htonl(ntohl(vp->num_tlvs) + 1);
	vp->length = htonl(ntohl(vp->length) +
		offsetof(struct vic_provinfo_tlv, value) + length);

	return 0;
}

size_t vic_provinfo_size(struct vic_provinfo *vp)
{
	return vp ?  ntohl(vp->length) + sizeof(*vp) - sizeof(vp->num_tlvs) : 0;
}
