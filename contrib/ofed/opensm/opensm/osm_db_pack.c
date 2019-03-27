/*
 * Copyright (c) 2004-2009 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2006 Mellanox Technologies LTD. All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved.
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
 *
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <stdlib.h>
#include <complib/cl_debug.h>
#include <opensm/osm_file_ids.h>
#define FILE_ID OSM_FILE_DB_PACK_C
#include <opensm/osm_db_pack.h>

static inline void pack_guid(uint64_t guid, char *p_guid_str)
{
	sprintf(p_guid_str, "0x%016" PRIx64, guid);
}

static inline uint64_t unpack_guid(char *p_guid_str)
{
	return strtoull(p_guid_str, NULL, 0);
}

static inline void pack_lids(uint16_t min_lid, uint16_t max_lid, char *lid_str)
{
	sprintf(lid_str, "0x%04x 0x%04x", min_lid, max_lid);
}

static inline int unpack_lids(IN char *p_lid_str, OUT uint16_t * p_min_lid,
			      OUT uint16_t * p_max_lid)
{
	unsigned long tmp;
	char *p_next;
	char *p_num;
	char lids_str[24];

	strncpy(lids_str, p_lid_str, 23);
	lids_str[23] = '\0';
	p_num = strtok_r(lids_str, " \t", &p_next);
	if (!p_num)
		return 1;
	tmp = strtoul(p_num, NULL, 0);
	if (tmp >= 0xC000)
		return 1;

	*p_min_lid = (uint16_t) tmp;

	p_num = strtok_r(NULL, " \t", &p_next);
	if (!p_num)
		return 1;
	tmp = strtoul(p_num, NULL, 0);
	if (tmp >= 0xC000)
		return 1;

	*p_max_lid = (uint16_t) tmp;

	return 0;
}

static inline void pack_mkey(uint64_t mkey, char *p_mkey_str)
{
	sprintf(p_mkey_str, "0x%016" PRIx64, mkey);
}

static inline uint64_t unpack_mkey(char *p_mkey_str)
{
	return strtoull(p_mkey_str, NULL, 0);
}

static inline void pack_neighbor(uint64_t guid, uint8_t portnum, char *p_str)
{
	sprintf(p_str, "0x%016" PRIx64 ":%u", guid, portnum);
}

static inline int unpack_neighbor(char *p_str, uint64_t *guid,
				  uint8_t *portnum)
{
	char tmp_str[24];
	char *p_num, *p_next;
	unsigned long tmp_port;

	strncpy(tmp_str, p_str, 23);
	tmp_str[23] = '\0';
	p_num = strtok_r(tmp_str, ":", &p_next);
	if (!p_num)
		return 1;
	if (guid)
		*guid = strtoull(p_num, NULL, 0);

	p_num = strtok_r(NULL, ":", &p_next);
	if (!p_num)
		return 1;
	if (portnum) {
		tmp_port = strtoul(p_num, NULL, 0);
		CL_ASSERT(tmp_port < 0x100);
		*portnum = (uint8_t) tmp_port;
	}

	return 0;
}

int osm_db_guid2lid_guids(IN osm_db_domain_t * p_g2l,
			  OUT cl_qlist_t * p_guid_list)
{
	char *p_key;
	cl_list_t keys;
	osm_db_guid_elem_t *p_guid_elem;

	cl_list_construct(&keys);
	cl_list_init(&keys, 10);

	if (osm_db_keys(p_g2l, &keys))
		return 1;

	while ((p_key = cl_list_remove_head(&keys)) != NULL) {
		p_guid_elem =
		    (osm_db_guid_elem_t *) malloc(sizeof(osm_db_guid_elem_t));
		CL_ASSERT(p_guid_elem != NULL);

		p_guid_elem->guid = unpack_guid(p_key);
		cl_qlist_insert_head(p_guid_list, &p_guid_elem->item);
	}

	cl_list_destroy(&keys);
	return 0;
}

int osm_db_guid2lid_get(IN osm_db_domain_t * p_g2l, IN uint64_t guid,
			OUT uint16_t * p_min_lid, OUT uint16_t * p_max_lid)
{
	char guid_str[20];
	char *p_lid_str;
	uint16_t min_lid, max_lid;

	pack_guid(guid, guid_str);
	p_lid_str = osm_db_lookup(p_g2l, guid_str);
	if (!p_lid_str)
		return 1;
	if (unpack_lids(p_lid_str, &min_lid, &max_lid))
		return 1;

	if (p_min_lid)
		*p_min_lid = min_lid;
	if (p_max_lid)
		*p_max_lid = max_lid;

	return 0;
}

int osm_db_guid2lid_set(IN osm_db_domain_t * p_g2l, IN uint64_t guid,
			IN uint16_t min_lid, IN uint16_t max_lid)
{
	char guid_str[20];
	char lid_str[16];

	pack_guid(guid, guid_str);
	pack_lids(min_lid, max_lid, lid_str);

	return osm_db_update(p_g2l, guid_str, lid_str);
}

int osm_db_guid2lid_delete(IN osm_db_domain_t * p_g2l, IN uint64_t guid)
{
	char guid_str[20];
	pack_guid(guid, guid_str);
	return osm_db_delete(p_g2l, guid_str);
}

int osm_db_guid2mkey_guids(IN osm_db_domain_t * p_g2m,
			   OUT cl_qlist_t * p_guid_list)
{
	char *p_key;
	cl_list_t keys;
	osm_db_guid_elem_t *p_guid_elem;

	cl_list_construct(&keys);
	cl_list_init(&keys, 10);

	if (osm_db_keys(p_g2m, &keys))
		return 1;

	while ((p_key = cl_list_remove_head(&keys)) != NULL) {
		p_guid_elem =
		    (osm_db_guid_elem_t *) malloc(sizeof(osm_db_guid_elem_t));
		CL_ASSERT(p_guid_elem != NULL);

		p_guid_elem->guid = unpack_guid(p_key);
		cl_qlist_insert_head(p_guid_list, &p_guid_elem->item);
	}

	cl_list_destroy(&keys);
	return 0;
}

int osm_db_guid2mkey_get(IN osm_db_domain_t * p_g2m, IN uint64_t guid,
			 OUT uint64_t * p_mkey)
{
	char guid_str[20];
	char *p_mkey_str;

	pack_guid(guid, guid_str);
	p_mkey_str = osm_db_lookup(p_g2m, guid_str);
	if (!p_mkey_str)
		return 1;

	if (p_mkey)
		*p_mkey = unpack_mkey(p_mkey_str);

	return 0;
}

int osm_db_guid2mkey_set(IN osm_db_domain_t * p_g2m, IN uint64_t guid,
			 IN uint64_t mkey)
{
	char guid_str[20];
	char mkey_str[20];

	pack_guid(guid, guid_str);
	pack_mkey(mkey, mkey_str);

	return osm_db_update(p_g2m, guid_str, mkey_str);
}

int osm_db_guid2mkey_delete(IN osm_db_domain_t * p_g2m, IN uint64_t guid)
{
	char guid_str[20];
	pack_guid(guid, guid_str);
	return osm_db_delete(p_g2m, guid_str);
}

int osm_db_neighbor_guids(IN osm_db_domain_t * p_neighbor,
			  OUT cl_qlist_t * p_neighbor_list)
{
	char *p_key;
	cl_list_t keys;
	osm_db_neighbor_elem_t *p_neighbor_elem;

	cl_list_construct(&keys);
	cl_list_init(&keys, 10);

	if (osm_db_keys(p_neighbor, &keys))
		return 1;

	while ((p_key = cl_list_remove_head(&keys)) != NULL) {
		p_neighbor_elem =
		    (osm_db_neighbor_elem_t *) malloc(sizeof(osm_db_neighbor_elem_t));
		CL_ASSERT(p_neighbor_elem != NULL);

		unpack_neighbor(p_key, &p_neighbor_elem->guid,
				&p_neighbor_elem->portnum);
		cl_qlist_insert_head(p_neighbor_list, &p_neighbor_elem->item);
	}

	cl_list_destroy(&keys);
	return 0;
}

int osm_db_neighbor_get(IN osm_db_domain_t * p_neighbor, IN uint64_t guid1,
			IN uint8_t portnum1, OUT uint64_t * p_guid2,
			OUT uint8_t * p_portnum2)
{
	char neighbor_str[24];
	char *p_other_str;
	uint64_t temp_guid;
	uint8_t temp_portnum;

	pack_neighbor(guid1, portnum1, neighbor_str);
	p_other_str = osm_db_lookup(p_neighbor, neighbor_str);
	if (!p_other_str)
		return 1;
	if (unpack_neighbor(p_other_str, &temp_guid, &temp_portnum))
		return 1;

	if (p_guid2)
		*p_guid2 = temp_guid;
	if (p_portnum2)
		*p_portnum2 = temp_portnum;

	return 0;
}

int osm_db_neighbor_set(IN osm_db_domain_t * p_neighbor, IN uint64_t guid1,
			IN uint8_t portnum1, IN uint64_t guid2,
			IN uint8_t portnum2)
{
	char n1_str[24], n2_str[24];

	pack_neighbor(guid1, portnum1, n1_str);
	pack_neighbor(guid2, portnum2, n2_str);

	return osm_db_update(p_neighbor, n1_str, n2_str);
}

int osm_db_neighbor_delete(IN osm_db_domain_t * p_neighbor, IN uint64_t guid,
			   IN uint8_t portnum)
{
	char n_str[24];

	pack_neighbor(guid, portnum, n_str);
	return osm_db_delete(p_neighbor, n_str);
}
