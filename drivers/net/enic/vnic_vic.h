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

#ifndef _VNIC_VIC_H_
#define _VNIC_VIC_H_

/* Note: All integer fields in NETWORK byte order */

/* Note: String field lengths include null char */

#define VIC_PROVINFO_CISCO_OUI		{ 0x00, 0x00, 0x0c }
#define VIC_PROVINFO_LINUX_TYPE		0x2

enum vic_linux_prov_tlv_type {
	VIC_LINUX_PROV_TLV_PORT_PROFILE_NAME_STR = 0,
	VIC_LINUX_PROV_TLV_CLIENT_MAC_ADDR = 1,			/* u8[6] */
	VIC_LINUX_PROV_TLV_CLIENT_NAME_STR = 2,
	VIC_LINUX_PROV_TLV_HOST_UUID_STR = 8,
	VIC_LINUX_PROV_TLV_CLIENT_UUID_STR = 9,
};

struct vic_provinfo {
	u8 oui[3];		/* OUI of data provider */
	u8 type;		/* provider-specific type */
	u32 length;		/* length of data below */
	u32 num_tlvs;		/* number of tlvs */
	struct vic_provinfo_tlv {
		u16 type;
		u16 length;
		u8 value[0];
	} tlv[0];
} __packed;

#define VIC_PROVINFO_MAX_DATA		1385
#define VIC_PROVINFO_MAX_TLV_DATA (VIC_PROVINFO_MAX_DATA - \
	sizeof(struct vic_provinfo))

struct vic_provinfo *vic_provinfo_alloc(gfp_t flags, u8 *oui, u8 type);
void vic_provinfo_free(struct vic_provinfo *vp);
int vic_provinfo_add_tlv(struct vic_provinfo *vp, u16 type, u16 length,
	void *value);
size_t vic_provinfo_size(struct vic_provinfo *vp);

#endif	/* _VNIC_VIC_H_ */
