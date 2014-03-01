/*
 * Copyright (c) 2013, Cisco Systems, Inc. All rights reserved.
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

#ifndef USNIC_CMN_UTIL_H
#define USNIC_CMN_UTIL_H

static inline void
usnic_mac_to_gid(const char *const mac, char *raw_gid)
{
	raw_gid[0] = 0xfe;
	raw_gid[1] = 0x80;
	memset(&raw_gid[2], 0, 6);
	raw_gid[8] = mac[0]^2;
	raw_gid[9] = mac[1];
	raw_gid[10] = mac[2];
	raw_gid[11] = 0xff;
	raw_gid[12] = 0xfe;
	raw_gid[13] = mac[3];
	raw_gid[14] = mac[4];
	raw_gid[15] = mac[5];
}

static inline void
usnic_mac_ip_to_gid(const char *const mac, const __be32 inaddr, char *raw_gid)
{
	raw_gid[0] = 0xfe;
	raw_gid[1] = 0x80;
	memset(&raw_gid[2], 0, 2);
	memcpy(&raw_gid[4], &inaddr, 4);
	raw_gid[8] = mac[0]^2;
	raw_gid[9] = mac[1];
	raw_gid[10] = mac[2];
	raw_gid[11] = 0xff;
	raw_gid[12] = 0xfe;
	raw_gid[13] = mac[3];
	raw_gid[14] = mac[4];
	raw_gid[15] = mac[5];
}

static inline void
usnic_write_gid_if_id_from_mac(char *mac, char *raw_gid)
{
	raw_gid[8] = mac[0]^2;
	raw_gid[9] = mac[1];
	raw_gid[10] = mac[2];
	raw_gid[11] = 0xff;
	raw_gid[12] = 0xfe;
	raw_gid[13] = mac[3];
	raw_gid[14] = mac[4];
	raw_gid[15] = mac[5];
}

#endif /* USNIC_COMMON_UTIL_H */
