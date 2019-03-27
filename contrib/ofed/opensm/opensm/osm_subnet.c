/*
 * Copyright (c) 2004-2009 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2011 Mellanox Technologies LTD. All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved.
 * Copyright (c) 2008 Xsigo Systems Inc.  All rights reserved.
 * Copyright (c) 2009 System Fabric Works, Inc. All rights reserved.
 * Copyright (c) 2009 HNR Consulting. All rights reserved.
 * Copyright (c) 2009-2015 ZIH, TU Dresden, Federal Republic of Germany. All rights reserved.
 * Copyright (c) 2013 Oracle and/or its affiliates. All rights reserved.
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

/*
 * Abstract:
 *    Implementation of osm_subn_t.
 * This object represents an IBA subnet.
 * This object is part of the opensm family of objects.
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <limits.h>
#include <errno.h>
#include <ctype.h>
#include <complib/cl_debug.h>
#include <complib/cl_log.h>
#include <opensm/osm_file_ids.h>
#define FILE_ID OSM_FILE_SUBNET_C
#include <opensm/osm_subnet.h>
#include <opensm/osm_opensm.h>
#include <opensm/osm_log.h>
#include <opensm/osm_madw.h>
#include <opensm/osm_port.h>
#include <opensm/osm_switch.h>
#include <opensm/osm_remote_sm.h>
#include <opensm/osm_partition.h>
#include <opensm/osm_node.h>
#include <opensm/osm_guid.h>
#include <opensm/osm_multicast.h>
#include <opensm/osm_inform.h>
#include <opensm/osm_console.h>
#include <opensm/osm_perfmgr.h>
#include <opensm/osm_congestion_control.h>
#include <opensm/osm_event_plugin.h>
#include <opensm/osm_qos_policy.h>
#include <opensm/osm_service.h>
#include <opensm/osm_db.h>
#include <opensm/osm_db_pack.h>

static const char null_str[] = "(null)";

#define OPT_OFFSET(opt) offsetof(osm_subn_opt_t, opt)
#define ARR_SIZE(a) (sizeof(a)/sizeof((a)[0]))

typedef struct opt_rec {
	const char *name;
	unsigned long opt_offset;
	void (*parse_fn)(osm_subn_t *p_subn, char *p_key, char *p_val_str,
			 void *p_val1, void *p_val2,
			 void (*)(osm_subn_t *, void *));
	void (*setup_fn)(osm_subn_t *p_subn, void *p_val);
	int  can_update;
} opt_rec_t;

static const char *module_name_str[] = {
	"main.c",
	"osm_console.c",
	"osm_console_io.c",
	"osm_db_files.c",
	"osm_db_pack.c",
	"osm_drop_mgr.c",
	"osm_dump.c",
	"osm_event_plugin.c",
	"osm_guid_info_rcv.c",
	"osm_guid_mgr.c",
	"osm_helper.c",
	"osm_inform.c",
	"osm_lid_mgr.c",
	"osm_lin_fwd_rcv.c",
	"osm_link_mgr.c",
	"osm_log.c",
	"osm_mad_pool.c",
	"osm_mcast_fwd_rcv.c",
	"osm_mcast_mgr.c",
	"osm_mcast_tbl.c",
	"osm_mcm_port.c",
	"osm_mesh.c",
	"osm_mlnx_ext_port_info_rcv.c",
	"osm_mtree.c",
	"osm_multicast.c",
	"osm_node.c",
	"osm_node_desc_rcv.c",
	"osm_node_info_rcv.c",
	"osm_opensm.c",
	"osm_perfmgr.c",
	"osm_perfmgr_db.c",
	"osm_pkey.c",
	"osm_pkey_mgr.c",
	"osm_pkey_rcv.c",
	"osm_port.c",
	"osm_port_info_rcv.c",
	"osm_prtn.c",
	"osm_prtn_config.c",
	"osm_qos.c",
	"osm_qos_parser_l.c",
	"osm_qos_parser_y.c",
	"osm_qos_policy.c",
	"osm_remote_sm.c",
	"osm_req.c",
	"osm_resp.c",
	"osm_router.c",
	"osm_sa.c",
	"osm_sa_class_port_info.c",
	"osm_sa_guidinfo_record.c",
	"osm_sa_informinfo.c",
	"osm_sa_lft_record.c",
	"osm_sa_link_record.c",
	"osm_sa_mad_ctrl.c",
	"osm_sa_mcmember_record.c",
	"osm_sa_mft_record.c",
	"osm_sa_multipath_record.c",
	"osm_sa_node_record.c",
	"osm_sa_path_record.c",
	"osm_sa_pkey_record.c",
	"osm_sa_portinfo_record.c",
	"osm_sa_service_record.c",
	"osm_sa_slvl_record.c",
	"osm_sa_sminfo_record.c",
	"osm_sa_sw_info_record.c",
	"osm_sa_vlarb_record.c",
	"osm_service.c",
	"osm_slvl_map_rcv.c",
	"osm_sm.c",
	"osm_sminfo_rcv.c",
	"osm_sm_mad_ctrl.c",
	"osm_sm_state_mgr.c",
	"osm_state_mgr.c",
	"osm_subnet.c",
	"osm_sw_info_rcv.c",
	"osm_switch.c",
	"osm_torus.c",
	"osm_trap_rcv.c",
	"osm_ucast_cache.c",
	"osm_ucast_dnup.c",
	"osm_ucast_file.c",
	"osm_ucast_ftree.c",
	"osm_ucast_lash.c",
	"osm_ucast_mgr.c",
	"osm_ucast_updn.c",
	"osm_vendor_ibumad.c",
	"osm_vl15intf.c",
	"osm_vl_arb_rcv.c",
	"st.c",
	"osm_ucast_dfsssp.c",
	"osm_congestion_control.c",
	/* Add new module names here ... */
	/* FILE_ID define in those modules must be identical to index here */
	/* last FILE_ID is currently 89 */
};

#define MOD_NAME_STR_UNKNOWN_VAL (ARR_SIZE(module_name_str))

static int find_module_name(const char *name, uint8_t *file_id)
{
	uint8_t i;

	for (i = 0; i < MOD_NAME_STR_UNKNOWN_VAL; i++) {
		if (strcmp(name, module_name_str[i]) == 0) {
			if (file_id)
				*file_id = i;
			return 0;
		}
	}
	return 1;
}

static void log_report(const char *fmt, ...)
{
	char buf[128];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
	printf("%s", buf);
	cl_log_event("OpenSM", CL_LOG_INFO, buf, NULL, 0);
}

static void log_config_value(char *name, const char *fmt, ...)
{
	char buf[128];
	va_list args;
	unsigned n;
	va_start(args, fmt);
	n = snprintf(buf, sizeof(buf), " Loading Cached Option:%s = ", name);
	if (n > sizeof(buf))
		n = sizeof(buf);
	n += vsnprintf(buf + n, sizeof(buf) - n, fmt, args);
	if (n > sizeof(buf) - 2)
		n = sizeof(buf) - 2;
	snprintf(buf + n, sizeof(buf) - n, "\n");
	va_end(args);
	printf("%s", buf);
	cl_log_event("OpenSM", CL_LOG_INFO, buf, NULL, 0);
}

static void opts_setup_log_flags(osm_subn_t *p_subn, void *p_val)
{
	p_subn->p_osm->log.level = *((uint8_t *) p_val);
}

static void opts_setup_force_log_flush(osm_subn_t *p_subn, void *p_val)
{
	p_subn->p_osm->log.flush = *((boolean_t *) p_val);
}

static void opts_setup_accum_log_file(osm_subn_t *p_subn, void *p_val)
{
	p_subn->p_osm->log.accum_log_file = *((boolean_t *) p_val);
}

static void opts_setup_log_max_size(osm_subn_t *p_subn, void *p_val)
{
	uint32_t log_max_size = *((uint32_t *) p_val);

	p_subn->p_osm->log.max_size = (unsigned long)log_max_size << 20; /* convert from MB to bytes */
}

static void opts_setup_sminfo_polling_timeout(osm_subn_t *p_subn, void *p_val)
{
	osm_sm_t *p_sm = &p_subn->p_osm->sm;
	uint32_t sminfo_polling_timeout = *((uint32_t *) p_val);

	cl_timer_stop(&p_sm->polling_timer);
	cl_timer_start(&p_sm->polling_timer, sminfo_polling_timeout);
}

static void opts_setup_sm_priority(osm_subn_t *p_subn, void *p_val)
{
	osm_sm_t *p_sm = &p_subn->p_osm->sm;
	uint8_t sm_priority = *((uint8_t *) p_val);

	osm_set_sm_priority(p_sm, sm_priority);
}

static int opts_strtoul(uint32_t *val, IN char *p_val_str,
			IN char *p_key, uint32_t max_value)
{
	char *endptr;
	unsigned long int tmp_val;

	errno = 0;
	tmp_val = strtoul(p_val_str, &endptr, 0);
	*val = tmp_val;
	if (*p_val_str == '\0' || *endptr != '\0') {
		log_report("-E- Parsing error in field %s, expected "
			   "numeric input received: %s\n", p_key, p_val_str);
		return -1;
	}
	if (tmp_val > max_value ||
	    ((tmp_val == ULONG_MAX) && errno == ERANGE)) {
		log_report("-E- Parsing error in field %s, value out of range\n", p_key);
		return -1;
	}
	return 0;
}

static int opts_strtoull(uint64_t *val, IN char *p_val_str,
			 IN char *p_key, uint64_t max_value)
{
	char *endptr;
	unsigned long long int tmp_val;

	errno = 0;
	tmp_val = strtoull(p_val_str, &endptr, 0);
	*val = tmp_val;
	if (*p_val_str == '\0' || *endptr != '\0') {
		log_report("-E- Parsing error in field %s, expected "
			   "numeric input received: %s\n", p_key, p_val_str);
		return -1;
	}
	if (tmp_val > max_value || (tmp_val == ULLONG_MAX && errno == ERANGE)) {
		log_report("-E- Parsing error in field %s, value out of range\n", p_key);
		return -1;
	}
	return 0;
}

static void opts_parse_net64(IN osm_subn_t *p_subn, IN char *p_key,
			     IN char *p_val_str, void *p_v1, void *p_v2,
			     void (*pfn)(osm_subn_t *, void *))
{
	uint64_t *p_val1 = p_v1, *p_val2 = p_v2;
	uint64_t val;

	if (opts_strtoull(&val, p_val_str, p_key, UINT64_MAX))
		return;

	if (cl_hton64(val) != *p_val1) {
		log_config_value(p_key, "0x%016" PRIx64, val);
		if (pfn)
			pfn(p_subn, &val);
		*p_val1 = *p_val2 = cl_ntoh64(val);
	}
}

static void opts_parse_uint32(IN osm_subn_t *p_subn, IN char *p_key,
			      IN char *p_val_str, void *p_v1, void *p_v2,
			      void (*pfn)(osm_subn_t *, void *))
{
	uint32_t *p_val1 = p_v1, *p_val2 = p_v2;
	uint32_t val;

	if (opts_strtoul(&val, p_val_str, p_key, UINT32_MAX))
		return;

	if (val != *p_val1) {
		log_config_value(p_key, "%u", val);
		if (pfn)
			pfn(p_subn, &val);
		*p_val1 = *p_val2 = val;
	}
}

static void opts_parse_net32(IN osm_subn_t *p_subn, IN char *p_key,
			     IN char *p_val_str, void *p_v1, void *p_v2,
			     void (*pfn)(osm_subn_t *, void *))
{
	uint32_t *p_val1 = p_v1, *p_val2 = p_v2;
	uint32_t val;

	if (opts_strtoul(&val, p_val_str, p_key, UINT32_MAX))
		return;

	if (cl_hton32(val) != *p_val1) {
		log_config_value(p_key, "%u", val);
		if (pfn)
			pfn(p_subn, &val);
		*p_val1 = *p_val2 = cl_hton32(val);
	}
}

static void opts_parse_int32(IN osm_subn_t *p_subn, IN char *p_key,
			     IN char *p_val_str, void *p_v1, void *p_v2,
			     void (*pfn)(osm_subn_t *, void *))
{
	int32_t *p_val1 = p_v1, *p_val2 = p_v2;
	int32_t val = strtol(p_val_str, NULL, 0);

	if (val != *p_val1) {
		log_config_value(p_key, "%d", val);
		if (pfn)
			pfn(p_subn, &val);
		*p_val1 = *p_val2 = val;
	}
}

static void opts_parse_uint16(IN osm_subn_t *p_subn, IN char *p_key,
			      IN char *p_val_str, void *p_v1, void *p_v2,
			      void (*pfn)(osm_subn_t *, void *))
{
	uint16_t *p_val1 = p_v1, *p_val2 = p_v2;
	uint32_t tmp_val;

	if (opts_strtoul(&tmp_val, p_val_str, p_key, UINT16_MAX))
		return;

	uint16_t val = (uint16_t) tmp_val;
	if (val != *p_val1) {
		log_config_value(p_key, "%u", val);
		if (pfn)
			pfn(p_subn, &val);
		*p_val1 = *p_val2 = val;
	}
}

static void opts_parse_net16(IN osm_subn_t *p_subn, IN char *p_key,
			     IN char *p_val_str, void *p_v1, void *p_v2,
			     void (*pfn)(osm_subn_t *, void *))
{
	uint16_t *p_val1 = p_v1, *p_val2 = p_v2;
	uint32_t tmp_val;

	if (opts_strtoul(&tmp_val, p_val_str, p_key, UINT16_MAX))
		return;

	uint16_t val = (uint16_t) tmp_val;
	if (cl_hton16(val) != *p_val1) {
		log_config_value(p_key, "0x%04x", val);
		if (pfn)
			pfn(p_subn, &val);
		*p_val1 = *p_val2 = cl_hton16(val);
	}
}

static void opts_parse_uint8(IN osm_subn_t *p_subn, IN char *p_key,
			     IN char *p_val_str, void *p_v1, void *p_v2,
			     void (*pfn)(osm_subn_t *, void *))
{
	uint8_t *p_val1 = p_v1, *p_val2 = p_v2;
	uint32_t tmp_val;

	if (opts_strtoul(&tmp_val, p_val_str, p_key, UINT8_MAX))
		return;

	uint8_t val = (uint8_t) tmp_val;
	if (val != *p_val1) {
		log_config_value(p_key, "%u", val);
		if (pfn)
			pfn(p_subn, &val);
		*p_val1 = *p_val2 = val;
	}
}

static void opts_parse_boolean(IN osm_subn_t *p_subn, IN char *p_key,
			       IN char *p_val_str, void *p_v1, void *p_v2,
			       void (*pfn)(osm_subn_t *, void *))
{
	boolean_t *p_val1 = p_v1, *p_val2 = p_v2;
	boolean_t val;

	if (!p_val_str)
		return;

	if (strcmp("TRUE", p_val_str))
		val = FALSE;
	else
		val = TRUE;

	if (val != *p_val1) {
		log_config_value(p_key, "%s", p_val_str);
		if (pfn)
			pfn(p_subn, &val);
		*p_val1 = *p_val2 = val;
	}
}

static void opts_parse_charp(IN osm_subn_t *p_subn, IN char *p_key,
			     IN char *p_val_str, void *p_v1, void *p_v2,
			     void (*pfn)(osm_subn_t *, void *))
{
	char **p_val1 = p_v1, **p_val2 = p_v2;
	const char *current_str = *p_val1 ? *p_val1 : null_str;

	if (p_val_str && strcmp(p_val_str, current_str)) {
		char *new;
		log_config_value(p_key, "%s", p_val_str);
		/* special case the "(null)" string */
		new = strcmp(null_str, p_val_str) ? strdup(p_val_str) : NULL;
		if (pfn)
			pfn(p_subn, new);
		if (*p_val1 && *p_val1 != *p_val2)
			free(*p_val1);
		if (*p_val2)
			free(*p_val2);
		*p_val1 = *p_val2 = new;
	}
}

static void opts_parse_256bit(IN osm_subn_t *p_subn, IN char *p_key,
			      IN char *p_val_str, void *p_v1, void *p_v2,
			      void (*pfn)(osm_subn_t *, void *))
{
	uint8_t *p_val1 = p_v1, *p_val2 = p_v2;
	uint8_t val[IB_CC_PORT_MASK_DATA_SIZE] = { 0 };
	char tmpbuf[3] = { 0 };
	uint8_t tmpint;
	int numdigits = 0;
	int startindex;
	char *strptr = p_val_str;
	char *ptr;
	int i;

	/* parse like it's hypothetically a 256 bit integer code
	 *
	 * store "big endian"
	 */

	if (!strncmp(strptr, "0x", 2) || !strncmp(strptr, "0X", 2))
		strptr+=2;

	for (ptr = strptr; *ptr; ptr++) {
		if (!isxdigit(*ptr)) {
			log_report("invalid hex digit in bitmask\n");
			return;
		}
		numdigits++;
	}

	if (!numdigits) {
		log_report("invalid length bitmask\n");
		return;
	}

	/* max of 2 hex chars per byte */
	if (numdigits > IB_CC_PORT_MASK_DATA_SIZE * 2)
		numdigits = IB_CC_PORT_MASK_DATA_SIZE * 2;

	startindex = IB_CC_PORT_MASK_DATA_SIZE - ((numdigits - 1) / 2) - 1;

	if (numdigits % 2) {
		memcpy(tmpbuf, strptr, 1);
		strptr += 1;
	}
	else {
		memcpy(tmpbuf, strptr, 2);
		strptr += 2;
	}

	tmpint = strtoul(tmpbuf, NULL, 16);
	val[startindex] = tmpint;

	for (i = (startindex + 1); i < IB_CC_PORT_MASK_DATA_SIZE; i++) {
		memcpy(tmpbuf, strptr, 2);
		strptr += 2;
		tmpint = strtoul(tmpbuf, NULL, 16);
		val[i] = tmpint;
	}

	if (memcmp(val, p_val1, IB_CC_PORT_MASK_DATA_SIZE)) {
		log_config_value(p_key, "%s", p_val_str);
		if (pfn)
			pfn(p_subn, val);
		memcpy(p_val1, val, IB_CC_PORT_MASK_DATA_SIZE);
		memcpy(p_val2, val, IB_CC_PORT_MASK_DATA_SIZE);
	}

}

static void opts_parse_cct_entry(IN osm_subn_t *p_subn, IN char *p_key,
				 IN char *p_val_str, void *p_v1, void *p_v2,
				 void (*pfn)(osm_subn_t *, void *))
{
	osm_cct_entry_t *p_cct1 = p_v1, *p_cct2 = p_v2;
	osm_cct_entry_t cct;
	char buf[512] = { 0 };
	char *ptr;

	strncpy(buf, p_val_str, 511);

	if (!(ptr = strchr(buf, ':'))) {
		log_report("invalid CCT entry\n");
		return;
	}

	*ptr = '\0';
	ptr++;

	cct.shift = strtoul(buf, NULL, 0);
	cct.multiplier = strtoul(ptr, NULL, 0);

	if (cct.shift != p_cct1->shift
	    || cct.multiplier != p_cct1->multiplier) {
		log_config_value(p_key, "%s", p_val_str);
		if (pfn)
			pfn(p_subn, &cct);
		p_cct1->shift = p_cct2->shift = cct.shift;
		p_cct1->multiplier = p_cct2->multiplier = cct.multiplier;
	}
}

static void opts_parse_cc_cct(IN osm_subn_t *p_subn, IN char *p_key,
			      IN char *p_val_str, void *p_v1, void *p_v2,
			      void (*pfn)(osm_subn_t *, void *))
{
	osm_cct_t *p_val1 = p_v1, *p_val2 = p_v2;
	const char *current_str = p_val1->input_str ? p_val1->input_str : null_str;

	if (p_val_str && strcmp(p_val_str, current_str)) {
		osm_cct_t newcct;
		char *new;
		unsigned int len = 0;
		char *lasts;
		char *tok;
		char *ptr;

		/* special case the "(null)" string */
		new = strcmp(null_str, p_val_str) ? strdup(p_val_str) : NULL;

		if (!new) {
			log_config_value(p_key, "%s", p_val_str);
			if (pfn)
				pfn(p_subn, NULL);
			memset(p_val1->entries, '\0', sizeof(p_val1->entries));
			memset(p_val2->entries, '\0', sizeof(p_val2->entries));
			p_val1->entries_len = p_val2->entries_len = 0;
			p_val1->input_str = p_val2->input_str = NULL;
			return;
		}

		memset(&newcct, '\0', sizeof(newcct));

		tok = strtok_r(new, ",", &lasts);
		while (tok && len < OSM_CCT_ENTRY_MAX) {

			if (!(ptr = strchr(tok, ':'))) {
				log_report("invalid CCT entry\n");
				free(new);
				return;
			}
			*ptr = '\0';
			ptr++;

			newcct.entries[len].shift = strtoul(tok, NULL, 0);
			newcct.entries[len].multiplier = strtoul(ptr, NULL, 0);
			len++;
			tok = strtok_r(NULL, ",", &lasts);
		}

		free(new);

		newcct.entries_len = len;
		newcct.input_str = strdup(p_val_str);

		log_config_value(p_key, "%s", p_val_str);
		if (pfn)
			pfn(p_subn, &newcct);
		if (p_val1->input_str && p_val1->input_str != p_val2->input_str)
			free(p_val1->input_str);
		if (p_val2->input_str)
			free(p_val2->input_str);
		memcpy(p_val1->entries, newcct.entries, sizeof(newcct.entries));
		memcpy(p_val2->entries, newcct.entries, sizeof(newcct.entries));
		p_val1->entries_len = p_val2->entries_len = newcct.entries_len;
		p_val1->input_str = p_val2->input_str = newcct.input_str;
	}
}

static int parse_ca_cong_common(char *p_val_str, uint8_t *sl, unsigned int *val_offset) {
	char *new, *lasts, *sl_str, *val_str;
	uint8_t sltmp;

	new = strcmp(null_str, p_val_str) ? strdup(p_val_str) : NULL;
	if (!new)
		return -1;

	sl_str = strtok_r(new, " \t", &lasts);
	val_str = strtok_r(NULL, " \t", &lasts);

	if (!val_str) {
		log_report("value must be specified in addition to SL\n");
		free(new);
		return -1;
	}

	sltmp = strtoul(sl_str, NULL, 0);
	if (sltmp >= IB_CA_CONG_ENTRY_DATA_SIZE) {
		log_report("invalid SL specified\n");
		free(new);
		return -1;
	}

	*sl = sltmp;
	*val_offset = (unsigned int)(val_str - new);

	free(new);
	return 0;
}

static void opts_parse_ccti_timer(IN osm_subn_t *p_subn, IN char *p_key,
				  IN char *p_val_str, void *p_v1, void *p_v2,
				  void (*pfn)(osm_subn_t *, void *))
{
	osm_cacongestion_entry_t *p_val1 = p_v1, *p_val2 = p_v2;
	unsigned int val_offset = 0;
	uint8_t sl = 0;

	if (parse_ca_cong_common(p_val_str, &sl, &val_offset) < 0)
		return;

	opts_parse_net16(p_subn, p_key, p_val_str + val_offset,
			 &p_val1[sl].ccti_timer,
			 &p_val2[sl].ccti_timer,
			 pfn);
}

static void opts_parse_ccti_increase(IN osm_subn_t *p_subn, IN char *p_key,
				     IN char *p_val_str, void *p_v1, void *p_v2,
				     void (*pfn)(osm_subn_t *, void *))
{
	osm_cacongestion_entry_t *p_val1 = p_v1, *p_val2 = p_v2;
	unsigned int val_offset = 0;
	uint8_t sl = 0;

	if (parse_ca_cong_common(p_val_str, &sl, &val_offset) < 0)
		return;

	opts_parse_uint8(p_subn, p_key, p_val_str + val_offset,
			 &p_val1[sl].ccti_increase,
			 &p_val2[sl].ccti_increase,
			 pfn);
}

static void opts_parse_trigger_threshold(IN osm_subn_t *p_subn, IN char *p_key,
					 IN char *p_val_str, void *p_v1, void *p_v2,
					 void (*pfn)(osm_subn_t *, void *))
{
	osm_cacongestion_entry_t *p_val1 = p_v1, *p_val2 = p_v2;
	unsigned int val_offset = 0;
	uint8_t sl = 0;

	if (parse_ca_cong_common(p_val_str, &sl, &val_offset) < 0)
		return;

	opts_parse_uint8(p_subn, p_key, p_val_str + val_offset,
			 &p_val1[sl].trigger_threshold,
			 &p_val2[sl].trigger_threshold,
			 pfn);
}

static void opts_parse_ccti_min(IN osm_subn_t *p_subn, IN char *p_key,
				IN char *p_val_str, void *p_v1, void *p_v2,
				void (*pfn)(osm_subn_t *, void *))
{
	osm_cacongestion_entry_t *p_val1 = p_v1, *p_val2 = p_v2;
	unsigned int val_offset = 0;
	uint8_t sl = 0;

	if (parse_ca_cong_common(p_val_str, &sl, &val_offset) < 0)
		return;

	opts_parse_uint8(p_subn, p_key, p_val_str + val_offset,
			 &p_val1[sl].ccti_min,
			 &p_val2[sl].ccti_min,
			 pfn);
}

static const opt_rec_t opt_tbl[] = {
	{ "guid", OPT_OFFSET(guid), opts_parse_net64, NULL, 0 },
	{ "m_key", OPT_OFFSET(m_key), opts_parse_net64, NULL, 1 },
	{ "sm_key", OPT_OFFSET(sm_key), opts_parse_net64, NULL, 1 },
	{ "sa_key", OPT_OFFSET(sa_key), opts_parse_net64, NULL, 1 },
	{ "subnet_prefix", OPT_OFFSET(subnet_prefix), opts_parse_net64, NULL, 0 },
	{ "m_key_lease_period", OPT_OFFSET(m_key_lease_period), opts_parse_net16, NULL, 1 },
	{ "m_key_protection_level", OPT_OFFSET(m_key_protect_bits), opts_parse_uint8, NULL, 1 },
	{ "m_key_lookup", OPT_OFFSET(m_key_lookup), opts_parse_boolean, NULL, 1 },
	{ "sweep_interval", OPT_OFFSET(sweep_interval), opts_parse_uint32, NULL, 1 },
	{ "max_wire_smps", OPT_OFFSET(max_wire_smps), opts_parse_uint32, NULL, 1 },
	{ "max_wire_smps2", OPT_OFFSET(max_wire_smps2), opts_parse_uint32, NULL, 1 },
	{ "max_smps_timeout", OPT_OFFSET(max_smps_timeout), opts_parse_uint32, NULL, 1 },
	{ "console", OPT_OFFSET(console), opts_parse_charp, NULL, 0 },
	{ "console_port", OPT_OFFSET(console_port), opts_parse_uint16, NULL, 0 },
	{ "transaction_timeout", OPT_OFFSET(transaction_timeout), opts_parse_uint32, NULL, 0 },
	{ "transaction_retries", OPT_OFFSET(transaction_retries), opts_parse_uint32, NULL, 0 },
	{ "max_msg_fifo_timeout", OPT_OFFSET(max_msg_fifo_timeout), opts_parse_uint32, NULL, 1 },
	{ "sm_priority", OPT_OFFSET(sm_priority), opts_parse_uint8, opts_setup_sm_priority, 1 },
	{ "lmc", OPT_OFFSET(lmc), opts_parse_uint8, NULL, 0 },
	{ "lmc_esp0", OPT_OFFSET(lmc_esp0), opts_parse_boolean, NULL, 0 },
	{ "max_op_vls", OPT_OFFSET(max_op_vls), opts_parse_uint8, NULL, 1 },
	{ "force_link_speed", OPT_OFFSET(force_link_speed), opts_parse_uint8, NULL, 1 },
	{ "force_link_speed_ext", OPT_OFFSET(force_link_speed_ext), opts_parse_uint8, NULL, 1 },
	{ "fdr10", OPT_OFFSET(fdr10), opts_parse_uint8, NULL, 1 },
	{ "reassign_lids", OPT_OFFSET(reassign_lids), opts_parse_boolean, NULL, 1 },
	{ "ignore_other_sm", OPT_OFFSET(ignore_other_sm), opts_parse_boolean, NULL, 1 },
	{ "single_thread", OPT_OFFSET(single_thread), opts_parse_boolean, NULL, 0 },
	{ "disable_multicast", OPT_OFFSET(disable_multicast), opts_parse_boolean, NULL, 1 },
	{ "subnet_timeout", OPT_OFFSET(subnet_timeout), opts_parse_uint8, NULL, 1 },
	{ "packet_life_time", OPT_OFFSET(packet_life_time), opts_parse_uint8, NULL, 1 },
	{ "vl_stall_count", OPT_OFFSET(vl_stall_count), opts_parse_uint8, NULL, 1 },
	{ "leaf_vl_stall_count", OPT_OFFSET(leaf_vl_stall_count), opts_parse_uint8, NULL, 1 },
	{ "head_of_queue_lifetime", OPT_OFFSET(head_of_queue_lifetime), opts_parse_uint8, NULL, 1 },
	{ "leaf_head_of_queue_lifetime", OPT_OFFSET(leaf_head_of_queue_lifetime), opts_parse_uint8, NULL, 1 },
	{ "local_phy_errors_threshold", OPT_OFFSET(local_phy_errors_threshold), opts_parse_uint8, NULL, 1 },
	{ "overrun_errors_threshold", OPT_OFFSET(overrun_errors_threshold), opts_parse_uint8, NULL, 1 },
	{ "use_mfttop", OPT_OFFSET(use_mfttop), opts_parse_boolean, NULL, 1},
	{ "sminfo_polling_timeout", OPT_OFFSET(sminfo_polling_timeout), opts_parse_uint32, opts_setup_sminfo_polling_timeout, 1 },
	{ "polling_retry_number", OPT_OFFSET(polling_retry_number), opts_parse_uint32, NULL, 1 },
	{ "force_heavy_sweep", OPT_OFFSET(force_heavy_sweep), opts_parse_boolean, NULL, 1 },
	{ "port_prof_ignore_file", OPT_OFFSET(port_prof_ignore_file), opts_parse_charp, NULL, 0 },
	{ "hop_weights_file", OPT_OFFSET(hop_weights_file), opts_parse_charp, NULL, 0 },
	{ "dimn_ports_file", OPT_OFFSET(port_search_ordering_file), opts_parse_charp, NULL, 0 },
	{ "port_search_ordering_file", OPT_OFFSET(port_search_ordering_file), opts_parse_charp, NULL, 0 },
	{ "port_profile_switch_nodes", OPT_OFFSET(port_profile_switch_nodes), opts_parse_boolean, NULL, 1 },
	{ "sweep_on_trap", OPT_OFFSET(sweep_on_trap), opts_parse_boolean, NULL, 1 },
	{ "routing_engine", OPT_OFFSET(routing_engine_names), opts_parse_charp, NULL, 0 },
	{ "connect_roots", OPT_OFFSET(connect_roots), opts_parse_boolean, NULL, 1 },
	{ "use_ucast_cache", OPT_OFFSET(use_ucast_cache), opts_parse_boolean, NULL, 0 },
	{ "log_file", OPT_OFFSET(log_file), opts_parse_charp, NULL, 0 },
	{ "log_max_size", OPT_OFFSET(log_max_size), opts_parse_uint32, opts_setup_log_max_size, 1 },
	{ "log_flags", OPT_OFFSET(log_flags), opts_parse_uint8, opts_setup_log_flags, 1 },
	{ "force_log_flush", OPT_OFFSET(force_log_flush), opts_parse_boolean, opts_setup_force_log_flush, 1 },
	{ "accum_log_file", OPT_OFFSET(accum_log_file), opts_parse_boolean, opts_setup_accum_log_file, 1 },
	{ "partition_config_file", OPT_OFFSET(partition_config_file), opts_parse_charp, NULL, 0 },
	{ "no_partition_enforcement", OPT_OFFSET(no_partition_enforcement), opts_parse_boolean, NULL, 1 },
	{ "part_enforce", OPT_OFFSET(part_enforce), opts_parse_charp, NULL, 1 },
	{ "allow_both_pkeys", OPT_OFFSET(allow_both_pkeys), opts_parse_boolean, NULL, 0 },
	{ "sm_assigned_guid", OPT_OFFSET(sm_assigned_guid), opts_parse_uint8, NULL, 1 },
	{ "qos", OPT_OFFSET(qos), opts_parse_boolean, NULL, 1 },
	{ "qos_policy_file", OPT_OFFSET(qos_policy_file), opts_parse_charp, NULL, 0 },
	{ "suppress_sl2vl_mad_status_errors", OPT_OFFSET(suppress_sl2vl_mad_status_errors), opts_parse_boolean, NULL, 1 },
	{ "dump_files_dir", OPT_OFFSET(dump_files_dir), opts_parse_charp, NULL, 0 },
	{ "lid_matrix_dump_file", OPT_OFFSET(lid_matrix_dump_file), opts_parse_charp, NULL, 0 },
	{ "lfts_file", OPT_OFFSET(lfts_file), opts_parse_charp, NULL, 0 },
	{ "root_guid_file", OPT_OFFSET(root_guid_file), opts_parse_charp, NULL, 0 },
	{ "cn_guid_file", OPT_OFFSET(cn_guid_file), opts_parse_charp, NULL, 0 },
	{ "io_guid_file", OPT_OFFSET(io_guid_file), opts_parse_charp, NULL, 0 },
	{ "port_shifting", OPT_OFFSET(port_shifting), opts_parse_boolean, NULL, 1 },
	{ "scatter_ports", OPT_OFFSET(scatter_ports), opts_parse_uint32, NULL, 1 },
	{ "max_reverse_hops", OPT_OFFSET(max_reverse_hops), opts_parse_uint16, NULL, 0 },
	{ "ids_guid_file", OPT_OFFSET(ids_guid_file), opts_parse_charp, NULL, 0 },
	{ "guid_routing_order_file", OPT_OFFSET(guid_routing_order_file), opts_parse_charp, NULL, 0 },
	{ "guid_routing_order_no_scatter", OPT_OFFSET(guid_routing_order_no_scatter), opts_parse_boolean, NULL, 0 },
	{ "sa_db_file", OPT_OFFSET(sa_db_file), opts_parse_charp, NULL, 0 },
	{ "sa_db_dump", OPT_OFFSET(sa_db_dump), opts_parse_boolean, NULL, 1 },
	{ "torus_config", OPT_OFFSET(torus_conf_file), opts_parse_charp, NULL, 1 },
	{ "do_mesh_analysis", OPT_OFFSET(do_mesh_analysis), opts_parse_boolean, NULL, 1 },
	{ "exit_on_fatal", OPT_OFFSET(exit_on_fatal), opts_parse_boolean, NULL, 1 },
	{ "honor_guid2lid_file", OPT_OFFSET(honor_guid2lid_file), opts_parse_boolean, NULL, 1 },
	{ "daemon", OPT_OFFSET(daemon), opts_parse_boolean, NULL, 0 },
	{ "sm_inactive", OPT_OFFSET(sm_inactive), opts_parse_boolean, NULL, 1 },
	{ "babbling_port_policy", OPT_OFFSET(babbling_port_policy), opts_parse_boolean, NULL, 1 },
	{ "drop_event_subscriptions", OPT_OFFSET(drop_event_subscriptions), opts_parse_boolean, NULL, 1 },
	{ "ipoib_mcgroup_creation_validation", OPT_OFFSET(ipoib_mcgroup_creation_validation), opts_parse_boolean, NULL, 1 },
	{ "mcgroup_join_validation", OPT_OFFSET(mcgroup_join_validation), opts_parse_boolean, NULL, 1 },
	{ "use_optimized_slvl", OPT_OFFSET(use_optimized_slvl), opts_parse_boolean, NULL, 1 },
	{ "fsync_high_avail_files", OPT_OFFSET(fsync_high_avail_files), opts_parse_boolean, NULL, 1 },
#ifdef ENABLE_OSM_PERF_MGR
	{ "perfmgr", OPT_OFFSET(perfmgr), opts_parse_boolean, NULL, 0 },
	{ "perfmgr_redir", OPT_OFFSET(perfmgr_redir), opts_parse_boolean, NULL, 0 },
	{ "perfmgr_sweep_time_s", OPT_OFFSET(perfmgr_sweep_time_s), opts_parse_uint16, NULL, 0 },
	{ "perfmgr_max_outstanding_queries", OPT_OFFSET(perfmgr_max_outstanding_queries), opts_parse_uint32, NULL, 0 },
	{ "perfmgr_ignore_cas", OPT_OFFSET(perfmgr_ignore_cas), opts_parse_boolean, NULL, 0 },
	{ "event_db_dump_file", OPT_OFFSET(event_db_dump_file), opts_parse_charp, NULL, 0 },
	{ "perfmgr_rm_nodes", OPT_OFFSET(perfmgr_rm_nodes), opts_parse_boolean, NULL, 0 },
	{ "perfmgr_log_errors", OPT_OFFSET(perfmgr_log_errors), opts_parse_boolean, NULL, 0 },
	{ "perfmgr_query_cpi", OPT_OFFSET(perfmgr_query_cpi), opts_parse_boolean, NULL, 0 },
	{ "perfmgr_xmit_wait_log", OPT_OFFSET(perfmgr_xmit_wait_log), opts_parse_boolean, NULL, 0 },
	{ "perfmgr_xmit_wait_threshold", OPT_OFFSET(perfmgr_xmit_wait_threshold), opts_parse_uint32, NULL, 0 },
#endif				/* ENABLE_OSM_PERF_MGR */
	{ "event_plugin_name", OPT_OFFSET(event_plugin_name), opts_parse_charp, NULL, 0 },
	{ "event_plugin_options", OPT_OFFSET(event_plugin_options), opts_parse_charp, NULL, 0 },
	{ "node_name_map_name", OPT_OFFSET(node_name_map_name), opts_parse_charp, NULL, 0 },
	{ "qos_max_vls", OPT_OFFSET(qos_options.max_vls), opts_parse_uint32, NULL, 1 },
	{ "qos_high_limit", OPT_OFFSET(qos_options.high_limit), opts_parse_int32, NULL, 1 },
	{ "qos_vlarb_high", OPT_OFFSET(qos_options.vlarb_high), opts_parse_charp, NULL, 1 },
	{ "qos_vlarb_low", OPT_OFFSET(qos_options.vlarb_low), opts_parse_charp, NULL, 1 },
	{ "qos_sl2vl", OPT_OFFSET(qos_options.sl2vl), opts_parse_charp, NULL, 1 },
	{ "qos_ca_max_vls", OPT_OFFSET(qos_ca_options.max_vls), opts_parse_uint32, NULL, 1 },
	{ "qos_ca_high_limit", OPT_OFFSET(qos_ca_options.high_limit), opts_parse_int32, NULL, 1 },
	{ "qos_ca_vlarb_high", OPT_OFFSET(qos_ca_options.vlarb_high), opts_parse_charp, NULL, 1 },
	{ "qos_ca_vlarb_low", OPT_OFFSET(qos_ca_options.vlarb_low), opts_parse_charp, NULL, 1 },
	{ "qos_ca_sl2vl", OPT_OFFSET(qos_ca_options.sl2vl), opts_parse_charp, NULL, 1 },
	{ "qos_sw0_max_vls", OPT_OFFSET(qos_sw0_options.max_vls), opts_parse_uint32, NULL, 1 },
	{ "qos_sw0_high_limit", OPT_OFFSET(qos_sw0_options.high_limit), opts_parse_int32, NULL, 1 },
	{ "qos_sw0_vlarb_high", OPT_OFFSET(qos_sw0_options.vlarb_high), opts_parse_charp, NULL, 1 },
	{ "qos_sw0_vlarb_low", OPT_OFFSET(qos_sw0_options.vlarb_low), opts_parse_charp, NULL, 1 },
	{ "qos_sw0_sl2vl", OPT_OFFSET(qos_sw0_options.sl2vl), opts_parse_charp, NULL, 1 },
	{ "qos_swe_max_vls", OPT_OFFSET(qos_swe_options.max_vls), opts_parse_uint32, NULL, 1 },
	{ "qos_swe_high_limit", OPT_OFFSET(qos_swe_options.high_limit), opts_parse_int32, NULL, 1 },
	{ "qos_swe_vlarb_high", OPT_OFFSET(qos_swe_options.vlarb_high), opts_parse_charp, NULL, 1 },
	{ "qos_swe_vlarb_low", OPT_OFFSET(qos_swe_options.vlarb_low), opts_parse_charp, NULL, 1 },
	{ "qos_swe_sl2vl", OPT_OFFSET(qos_swe_options.sl2vl), opts_parse_charp, NULL, 1 },
	{ "qos_rtr_max_vls", OPT_OFFSET(qos_rtr_options.max_vls), opts_parse_uint32, NULL, 1 },
	{ "qos_rtr_high_limit", OPT_OFFSET(qos_rtr_options.high_limit), opts_parse_int32, NULL, 1 },
	{ "qos_rtr_vlarb_high", OPT_OFFSET(qos_rtr_options.vlarb_high), opts_parse_charp, NULL, 1 },
	{ "qos_rtr_vlarb_low", OPT_OFFSET(qos_rtr_options.vlarb_low), opts_parse_charp, NULL, 1 },
	{ "qos_rtr_sl2vl", OPT_OFFSET(qos_rtr_options.sl2vl), opts_parse_charp, NULL, 1 },
	{ "congestion_control", OPT_OFFSET(congestion_control), opts_parse_boolean, NULL, 1 },
	{ "cc_key", OPT_OFFSET(cc_key), opts_parse_net64, NULL, 0},
	{ "cc_max_outstanding_mads", OPT_OFFSET(cc_max_outstanding_mads), opts_parse_uint32, NULL, 0 },
	{ "cc_sw_cong_setting_control_map", OPT_OFFSET(cc_sw_cong_setting_control_map), opts_parse_net32, NULL, 1},
	{ "cc_sw_cong_setting_victim_mask", OPT_OFFSET(cc_sw_cong_setting_victim_mask), opts_parse_256bit, NULL, 1},
	{ "cc_sw_cong_setting_credit_mask", OPT_OFFSET(cc_sw_cong_setting_credit_mask), opts_parse_256bit, NULL, 1},
	{ "cc_sw_cong_setting_threshold", OPT_OFFSET(cc_sw_cong_setting_threshold), opts_parse_uint8, NULL, 1},
	{ "cc_sw_cong_setting_packet_size", OPT_OFFSET(cc_sw_cong_setting_packet_size), opts_parse_uint8, NULL, 1},
	{ "cc_sw_cong_setting_credit_starvation_threshold", OPT_OFFSET(cc_sw_cong_setting_credit_starvation_threshold), opts_parse_uint8, NULL, 1},
	{ "cc_sw_cong_setting_credit_starvation_return_delay", OPT_OFFSET(cc_sw_cong_setting_credit_starvation_return_delay), opts_parse_cct_entry, NULL, 1},
	{ "cc_sw_cong_setting_marking_rate", OPT_OFFSET(cc_sw_cong_setting_marking_rate), opts_parse_net16, NULL, 1},
	{ "cc_ca_cong_setting_port_control", OPT_OFFSET(cc_ca_cong_setting_port_control), opts_parse_net16, NULL, 1},
	{ "cc_ca_cong_setting_control_map", OPT_OFFSET(cc_ca_cong_setting_control_map), opts_parse_net16, NULL, 1},
	{ "cc_ca_cong_setting_ccti_timer", OPT_OFFSET(cc_ca_cong_entries), opts_parse_ccti_timer, NULL, 1},
	{ "cc_ca_cong_setting_ccti_increase", OPT_OFFSET(cc_ca_cong_entries), opts_parse_ccti_increase, NULL, 1},
	{ "cc_ca_cong_setting_trigger_threshold", OPT_OFFSET(cc_ca_cong_entries), opts_parse_trigger_threshold, NULL, 1},
	{ "cc_ca_cong_setting_ccti_min", OPT_OFFSET(cc_ca_cong_entries), opts_parse_ccti_min, NULL, 1},
	{ "cc_cct", OPT_OFFSET(cc_cct), opts_parse_cc_cct, NULL, 1},
	{ "enable_quirks", OPT_OFFSET(enable_quirks), opts_parse_boolean, NULL, 1 },
	{ "no_clients_rereg", OPT_OFFSET(no_clients_rereg), opts_parse_boolean, NULL, 1 },
	{ "prefix_routes_file", OPT_OFFSET(prefix_routes_file), opts_parse_charp, NULL, 0 },
	{ "consolidate_ipv6_snm_req", OPT_OFFSET(consolidate_ipv6_snm_req), opts_parse_boolean, NULL, 1 },
	{ "lash_start_vl", OPT_OFFSET(lash_start_vl), opts_parse_uint8, NULL, 1 },
	{ "sm_sl", OPT_OFFSET(sm_sl), opts_parse_uint8, NULL, 1 },
	{ "log_prefix", OPT_OFFSET(log_prefix), opts_parse_charp, NULL, 1 },
	{ "per_module_logging_file", OPT_OFFSET(per_module_logging_file), opts_parse_charp, NULL, 0 },
	{ "quasi_ftree_indexing", OPT_OFFSET(quasi_ftree_indexing), opts_parse_boolean, NULL, 1 },
	{0}
};

static int compar_mgids(const void *m1, const void *m2)
{
	return memcmp(m1, m2, sizeof(ib_gid_t));
}

static void subn_validate_g2m(osm_subn_t *p_subn)
{
	cl_qlist_t guids;
	osm_db_guid_elem_t *p_item;
	uint64_t mkey;
	boolean_t valid_entry;

	OSM_LOG_ENTER(&(p_subn->p_osm->log));
	cl_qlist_init(&guids);

	if (osm_db_guid2mkey_guids(p_subn->p_g2m, &guids)) {
		OSM_LOG(&(p_subn->p_osm->log), OSM_LOG_ERROR, "ERR 7506: "
			"could not get mkey guid list\n");
		goto Exit;
	}

	while ((p_item = (osm_db_guid_elem_t *) cl_qlist_remove_head(&guids))
	       != (osm_db_guid_elem_t *) cl_qlist_end(&guids)) {
		valid_entry = TRUE;

		if (p_item->guid == 0) {
			OSM_LOG(&(p_subn->p_osm->log), OSM_LOG_ERROR,
				"ERR 7507: found invalid zero guid");
			valid_entry = FALSE;
		} else if (osm_db_guid2mkey_get(p_subn->p_g2m, p_item->guid,
						&mkey)) {
			OSM_LOG(&(p_subn->p_osm->log), OSM_LOG_ERROR,
				"ERR 7508: could not get mkey for guid:0x%016"
				PRIx64 "\n", p_item->guid);
			valid_entry = FALSE;
		}

		if (valid_entry == FALSE) {
			if (osm_db_guid2mkey_delete(p_subn->p_g2m,
						    p_item->guid))
				OSM_LOG(&(p_subn->p_osm->log), OSM_LOG_ERROR,
					"ERR 7509: failed to delete entry for "
					"guid:0x%016" PRIx64 "\n",
					p_item->guid);
		}
		free(p_item);
	}

Exit:
	OSM_LOG_EXIT(&(p_subn->p_osm->log));
}

static void subn_validate_neighbor(osm_subn_t *p_subn)
{
	cl_qlist_t entries;
	osm_db_neighbor_elem_t *p_item;
	boolean_t valid_entry;
	uint64_t guid;
	uint8_t port;

	OSM_LOG_ENTER(&(p_subn->p_osm->log));
	cl_qlist_init(&entries);

	if (osm_db_neighbor_guids(p_subn->p_neighbor, &entries)) {
		OSM_LOG(&(p_subn->p_osm->log), OSM_LOG_ERROR, "ERR 7512: "
			"could not get neighbor entry list\n");
		goto Exit;
	}

	while ((p_item =
		(osm_db_neighbor_elem_t *) cl_qlist_remove_head(&entries))
	       != (osm_db_neighbor_elem_t *) cl_qlist_end(&entries)) {
		valid_entry = TRUE;

		OSM_LOG(&(p_subn->p_osm->log), OSM_LOG_DEBUG,
			"Validating neighbor for guid:0x%016" PRIx64
			", port %d\n",
			p_item->guid, p_item->portnum);
		if (p_item->guid == 0) {
			OSM_LOG(&(p_subn->p_osm->log), OSM_LOG_ERROR,
				"ERR 7513: found invalid zero guid\n");
			valid_entry = FALSE;
		} else if (p_item->portnum == 0) {
			OSM_LOG(&(p_subn->p_osm->log), OSM_LOG_ERROR,
				"ERR 7514: found invalid zero port for "
				"guid: 0x%016" PRIx64 "\n",
				p_item->guid);
			valid_entry = FALSE;
		} else if (osm_db_neighbor_get(p_subn->p_neighbor,
					       p_item->guid, p_item->portnum,
					       &guid, &port)) {
			OSM_LOG(&(p_subn->p_osm->log), OSM_LOG_ERROR,
				"ERR 7515: could not find neighbor for "
				"guid: 0x%016" PRIx64 ", port %d\n",
				p_item->guid, p_item->portnum);
			valid_entry = FALSE;
		} else if (guid == 0) {
			OSM_LOG(&(p_subn->p_osm->log), OSM_LOG_ERROR,
				"ERR 7516: found invalid neighbor "
				"zero guid for guid: 0x%016" PRIx64
				", port %d\n",
				p_item->guid, p_item->portnum);
			valid_entry = FALSE;
		} else if (port == 0) {
			OSM_LOG(&(p_subn->p_osm->log), OSM_LOG_ERROR,
				"ERR 7517: found invalid neighbor "
				"zero port for guid: 0x%016" PRIx64
				", port %d\n",
				p_item->guid, p_item->portnum);
			valid_entry = FALSE;
		} else if (osm_db_neighbor_get(p_subn->p_neighbor,
					       guid, port, &guid, &port) ||
			   guid != p_item->guid || port != p_item->portnum) {
			OSM_LOG(&(p_subn->p_osm->log), OSM_LOG_ERROR,
				"ERR 7518: neighbor does not point "
				"back at us (guid: 0x%016" PRIx64
				", port %d)\n",
				p_item->guid, p_item->portnum);
			valid_entry = FALSE;
		}

		if (valid_entry == FALSE) {
			if (osm_db_neighbor_delete(p_subn->p_neighbor,
						   p_item->guid,
						   p_item->portnum))
				OSM_LOG(&(p_subn->p_osm->log), OSM_LOG_ERROR,
					"ERR 7519: failed to delete entry for "
					"guid:0x%016" PRIx64 " port:%u\n",
					p_item->guid, p_item->portnum);
		}
		free(p_item);
	}

Exit:
	OSM_LOG_EXIT(&(p_subn->p_osm->log));
}

void osm_subn_construct(IN osm_subn_t * p_subn)
{
	memset(p_subn, 0, sizeof(*p_subn));
	cl_ptr_vector_construct(&p_subn->port_lid_tbl);
	cl_qmap_init(&p_subn->sw_guid_tbl);
	cl_qmap_init(&p_subn->node_guid_tbl);
	cl_qmap_init(&p_subn->port_guid_tbl);
	cl_qmap_init(&p_subn->alias_port_guid_tbl);
	cl_qmap_init(&p_subn->assigned_guids_tbl);
	cl_qmap_init(&p_subn->sm_guid_tbl);
	cl_qlist_init(&p_subn->sa_sr_list);
	cl_qlist_init(&p_subn->sa_infr_list);
	cl_qlist_init(&p_subn->alias_guid_list);
	cl_qlist_init(&p_subn->prefix_routes_list);
	cl_qmap_init(&p_subn->rtr_guid_tbl);
	cl_qmap_init(&p_subn->prtn_pkey_tbl);
	cl_fmap_init(&p_subn->mgrp_mgid_tbl, compar_mgids);
}

static void subn_destroy_qos_options(osm_qos_options_t *opt)
{
	free(opt->vlarb_high);
	free(opt->vlarb_low);
	free(opt->sl2vl);
}

static void subn_opt_destroy(IN osm_subn_opt_t * p_opt)
{
	free(p_opt->console);
	free(p_opt->port_prof_ignore_file);
	free(p_opt->hop_weights_file);
	free(p_opt->port_search_ordering_file);
	free(p_opt->routing_engine_names);
	free(p_opt->log_file);
	free(p_opt->partition_config_file);
	free(p_opt->qos_policy_file);
	free(p_opt->dump_files_dir);
	free(p_opt->part_enforce);
	free(p_opt->lid_matrix_dump_file);
	free(p_opt->lfts_file);
	free(p_opt->root_guid_file);
	free(p_opt->cn_guid_file);
	free(p_opt->io_guid_file);
	free(p_opt->ids_guid_file);
	free(p_opt->guid_routing_order_file);
	free(p_opt->sa_db_file);
	free(p_opt->torus_conf_file);
#ifdef ENABLE_OSM_PERF_MGR
	free(p_opt->event_db_dump_file);
#endif /* ENABLE_OSM_PERF_MGR */
	free(p_opt->event_plugin_name);
	free(p_opt->event_plugin_options);
	free(p_opt->node_name_map_name);
	free(p_opt->prefix_routes_file);
	free(p_opt->log_prefix);
	subn_destroy_qos_options(&p_opt->qos_options);
	subn_destroy_qos_options(&p_opt->qos_ca_options);
	subn_destroy_qos_options(&p_opt->qos_sw0_options);
	subn_destroy_qos_options(&p_opt->qos_swe_options);
	subn_destroy_qos_options(&p_opt->qos_rtr_options);
	free(p_opt->cc_cct.input_str);
}

void osm_subn_destroy(IN osm_subn_t * p_subn)
{
	int i;
	osm_node_t *p_node, *p_next_node;
	osm_assigned_guids_t *p_assigned_guids, *p_next_assigned_guids;
	osm_alias_guid_t *p_alias_guid, *p_next_alias_guid;
	osm_port_t *p_port, *p_next_port;
	osm_switch_t *p_sw, *p_next_sw;
	osm_remote_sm_t *p_rsm, *p_next_rsm;
	osm_prtn_t *p_prtn, *p_next_prtn;
	osm_infr_t *p_infr, *p_next_infr;
	osm_svcr_t *p_svcr, *p_next_svcr;

	/* it might be a good idea to de-allocate all known objects */
	p_next_node = (osm_node_t *) cl_qmap_head(&p_subn->node_guid_tbl);
	while (p_next_node !=
	       (osm_node_t *) cl_qmap_end(&p_subn->node_guid_tbl)) {
		p_node = p_next_node;
		p_next_node = (osm_node_t *) cl_qmap_next(&p_node->map_item);
		osm_node_delete(&p_node);
	}

	p_next_assigned_guids = (osm_assigned_guids_t *) cl_qmap_head(&p_subn->assigned_guids_tbl);
	while (p_next_assigned_guids !=
	       (osm_assigned_guids_t *) cl_qmap_end(&p_subn->assigned_guids_tbl)) {
		p_assigned_guids = p_next_assigned_guids;
		p_next_assigned_guids = (osm_assigned_guids_t *) cl_qmap_next(&p_assigned_guids->map_item);
		osm_assigned_guids_delete(&p_assigned_guids);
	}

	p_next_alias_guid = (osm_alias_guid_t *) cl_qmap_head(&p_subn->alias_port_guid_tbl);
	while (p_next_alias_guid !=
	       (osm_alias_guid_t *) cl_qmap_end(&p_subn->alias_port_guid_tbl)) {
		p_alias_guid = p_next_alias_guid;
		p_next_alias_guid = (osm_alias_guid_t *) cl_qmap_next(&p_alias_guid->map_item);
		osm_alias_guid_delete(&p_alias_guid);
	}

	while (cl_qlist_count(&p_subn->alias_guid_list))
		osm_guid_work_obj_delete((osm_guidinfo_work_obj_t *) cl_qlist_remove_head(&p_subn->alias_guid_list));

	p_next_port = (osm_port_t *) cl_qmap_head(&p_subn->port_guid_tbl);
	while (p_next_port !=
	       (osm_port_t *) cl_qmap_end(&p_subn->port_guid_tbl)) {
		p_port = p_next_port;
		p_next_port = (osm_port_t *) cl_qmap_next(&p_port->map_item);
		osm_port_delete(&p_port);
	}

	p_next_sw = (osm_switch_t *) cl_qmap_head(&p_subn->sw_guid_tbl);
	while (p_next_sw != (osm_switch_t *) cl_qmap_end(&p_subn->sw_guid_tbl)) {
		p_sw = p_next_sw;
		p_next_sw = (osm_switch_t *) cl_qmap_next(&p_sw->map_item);
		osm_switch_delete(&p_sw);
	}

	p_next_rsm = (osm_remote_sm_t *) cl_qmap_head(&p_subn->sm_guid_tbl);
	while (p_next_rsm !=
	       (osm_remote_sm_t *) cl_qmap_end(&p_subn->sm_guid_tbl)) {
		p_rsm = p_next_rsm;
		p_next_rsm = (osm_remote_sm_t *) cl_qmap_next(&p_rsm->map_item);
		free(p_rsm);
	}

	p_next_prtn = (osm_prtn_t *) cl_qmap_head(&p_subn->prtn_pkey_tbl);
	while (p_next_prtn !=
	       (osm_prtn_t *) cl_qmap_end(&p_subn->prtn_pkey_tbl)) {
		p_prtn = p_next_prtn;
		p_next_prtn = (osm_prtn_t *) cl_qmap_next(&p_prtn->map_item);
		osm_prtn_delete(p_subn, &p_prtn);
	}

	cl_fmap_remove_all(&p_subn->mgrp_mgid_tbl);

	for (i = 0; i <= p_subn->max_mcast_lid_ho - IB_LID_MCAST_START_HO;
	     i++)
		if (p_subn->mboxes[i])
			osm_mgrp_box_delete(p_subn->mboxes[i]);

	p_next_infr = (osm_infr_t *) cl_qlist_head(&p_subn->sa_infr_list);
	while (p_next_infr !=
	       (osm_infr_t *) cl_qlist_end(&p_subn->sa_infr_list)) {
		p_infr = p_next_infr;
		p_next_infr = (osm_infr_t *) cl_qlist_next(&p_infr->list_item);
		osm_infr_delete(p_infr);
	}

	p_next_svcr = (osm_svcr_t *) cl_qlist_head(&p_subn->sa_sr_list);
	while (p_next_svcr !=
	       (osm_svcr_t *) cl_qlist_end(&p_subn->sa_sr_list)) {
		p_svcr = p_next_svcr;
		p_next_svcr = (osm_svcr_t *) cl_qlist_next(&p_svcr->list_item);
		osm_svcr_delete(p_svcr);
	}

	cl_ptr_vector_destroy(&p_subn->port_lid_tbl);

	osm_qos_policy_destroy(p_subn->p_qos_policy);

	while (!cl_is_qlist_empty(&p_subn->prefix_routes_list)) {
		cl_list_item_t *item = cl_qlist_remove_head(&p_subn->prefix_routes_list);
		free(item);
	}

	subn_opt_destroy(&p_subn->opt);
	free(p_subn->opt.file_opts);
}

ib_api_status_t osm_subn_init(IN osm_subn_t * p_subn, IN osm_opensm_t * p_osm,
			      IN const osm_subn_opt_t * p_opt)
{
	cl_status_t status;

	p_subn->p_osm = p_osm;

	status = cl_ptr_vector_init(&p_subn->port_lid_tbl,
				    OSM_SUBNET_VECTOR_MIN_SIZE,
				    OSM_SUBNET_VECTOR_GROW_SIZE);
	if (status != CL_SUCCESS)
		return status;

	status = cl_ptr_vector_set_capacity(&p_subn->port_lid_tbl,
					    OSM_SUBNET_VECTOR_CAPACITY);
	if (status != CL_SUCCESS)
		return status;

	/*
	   LID zero is not valid.  NULL out this entry for the
	   convenience of other code.
	 */
	cl_ptr_vector_set(&p_subn->port_lid_tbl, 0, NULL);

	p_subn->opt = *p_opt;
	p_subn->max_ucast_lid_ho = IB_LID_UCAST_END_HO;
	p_subn->max_mcast_lid_ho = IB_LID_MCAST_END_HO;
	p_subn->min_ca_mtu = IB_MAX_MTU;
	p_subn->min_ca_rate = IB_PATH_RECORD_RATE_300_GBS;
	p_subn->min_data_vls = IB_MAX_NUM_VLS - 1;
	p_subn->min_sw_data_vls = IB_MAX_NUM_VLS - 1;
	p_subn->ignore_existing_lfts = TRUE;

	/* we assume master by default - so we only need to set it true if STANDBY */
	p_subn->coming_out_of_standby = FALSE;
	p_subn->sweeping_enabled = TRUE;
	p_subn->last_sm_port_state = 1;

	/* Initialize the guid2mkey database */
	p_subn->p_g2m = osm_db_domain_init(&(p_osm->db), "guid2mkey");
	if (!p_subn->p_g2m) {
		OSM_LOG(&(p_osm->log), OSM_LOG_ERROR, "ERR 7510: "
			"Error initializing Guid-to-MKey persistent database\n");
		return IB_ERROR;
	}

	if (osm_db_restore(p_subn->p_g2m)) {
#ifndef __WIN__
		/*
		 * When Windows is BSODing, it might corrupt files that
		 * were previously opened for writing, even if the files
		 * are closed, so we might see corrupted guid2mkey file.
		 */
		if (p_subn->opt.exit_on_fatal) {
			osm_log(&(p_osm->log), OSM_LOG_SYS,
				"FATAL: Error restoring Guid-to-Mkey "
				"persistent database\n");
			return IB_ERROR;
		} else
#endif
			OSM_LOG(&(p_osm->log), OSM_LOG_ERROR,
				"ERR 7511: Error restoring Guid-to-Mkey "
				"persistent database\n");
	}

	subn_validate_g2m(p_subn);

	/* Initialize the neighbor database */
	p_subn->p_neighbor = osm_db_domain_init(&(p_osm->db), "neighbors");
	if (!p_subn->p_neighbor) {
		OSM_LOG(&(p_osm->log), OSM_LOG_ERROR, "ERR 7520: Error "
			"initializing neighbor link persistent database\n");
		return IB_ERROR;
	}

	if (osm_db_restore(p_subn->p_neighbor)) {
#ifndef __WIN__
		/*
		 * When Windows is BSODing, it might corrupt files that
		 * were previously opened for writing, even if the files
		 * are closed, so we might see corrupted neighbors file.
		 */
		if (p_subn->opt.exit_on_fatal) {
			osm_log(&(p_osm->log), OSM_LOG_SYS,
				"FATAL: Error restoring neighbor link "
				"persistent database\n");
			return IB_ERROR;
		} else
#endif
			OSM_LOG(&(p_osm->log), OSM_LOG_ERROR,
				"ERR 7521: Error restoring neighbor link "
				"persistent database\n");
	}

	subn_validate_neighbor(p_subn);

	return IB_SUCCESS;
}

osm_port_t *osm_get_port_by_mad_addr(IN osm_log_t * p_log,
				     IN const osm_subn_t * p_subn,
				     IN osm_mad_addr_t * p_mad_addr)
{
	osm_port_t *port = osm_get_port_by_lid(p_subn, p_mad_addr->dest_lid);
	if (!port)
		OSM_LOG(p_log, OSM_LOG_ERROR, "ERR 7504: "
			"Lid is out of range: %u\n",
			cl_ntoh16(p_mad_addr->dest_lid));

	return port;
}

ib_api_status_t osm_get_gid_by_mad_addr(IN osm_log_t * p_log,
					IN const osm_subn_t * p_subn,
					IN osm_mad_addr_t * p_mad_addr,
					OUT ib_gid_t * p_gid)
{
	const osm_port_t *p_port;

	if (p_gid == NULL) {
		OSM_LOG(p_log, OSM_LOG_ERROR, "ERR 7505: "
			"Provided output GID is NULL\n");
		return IB_INVALID_PARAMETER;
	}

	p_port = osm_get_port_by_mad_addr(p_log, p_subn, p_mad_addr);
	if (!p_port)
		return IB_INVALID_PARAMETER;

	p_gid->unicast.interface_id = p_port->p_physp->port_guid;
	p_gid->unicast.prefix = p_subn->opt.subnet_prefix;

	return IB_SUCCESS;
}

osm_physp_t *osm_get_physp_by_mad_addr(IN osm_log_t * p_log,
				       IN const osm_subn_t * p_subn,
				       IN osm_mad_addr_t * p_mad_addr)
{
	osm_port_t *p_port;

	p_port = osm_get_port_by_mad_addr(p_log, p_subn, p_mad_addr);
	if (!p_port)
		return NULL;

	return p_port->p_physp;
}

osm_switch_t *osm_get_switch_by_guid(IN const osm_subn_t * p_subn,
				     IN ib_net64_t guid)
{
	osm_switch_t *p_switch;

	p_switch = (osm_switch_t *) cl_qmap_get(&(p_subn->sw_guid_tbl), guid);
	if (p_switch == (osm_switch_t *) cl_qmap_end(&(p_subn->sw_guid_tbl)))
		p_switch = NULL;
	return p_switch;
}

osm_node_t *osm_get_node_by_guid(IN osm_subn_t const *p_subn, IN ib_net64_t guid)
{
	osm_node_t *p_node;

	p_node = (osm_node_t *) cl_qmap_get(&(p_subn->node_guid_tbl), guid);
	if (p_node == (osm_node_t *) cl_qmap_end(&(p_subn->node_guid_tbl)))
		p_node = NULL;
	return p_node;
}

osm_port_t *osm_get_port_by_guid(IN osm_subn_t const *p_subn, IN ib_net64_t guid)
{
	osm_port_t *p_port;

	p_port = (osm_port_t *) cl_qmap_get(&(p_subn->port_guid_tbl), guid);
	if (p_port == (osm_port_t *) cl_qmap_end(&(p_subn->port_guid_tbl)))
		p_port = NULL;
	return p_port;
}

osm_alias_guid_t *osm_get_alias_guid_by_guid(IN osm_subn_t const *p_subn,
					     IN ib_net64_t guid)
{
	osm_alias_guid_t *p_alias_guid;

	p_alias_guid = (osm_alias_guid_t *) cl_qmap_get(&(p_subn->alias_port_guid_tbl), guid);
	if (p_alias_guid == (osm_alias_guid_t *) cl_qmap_end(&(p_subn->alias_port_guid_tbl)))
		return NULL;
	return p_alias_guid;
}

osm_port_t *osm_get_port_by_alias_guid(IN osm_subn_t const *p_subn,
				       IN ib_net64_t guid)
{
	osm_alias_guid_t *p_alias_guid;

	p_alias_guid = osm_get_alias_guid_by_guid(p_subn, guid);
	if (!p_alias_guid)
		return NULL;
	return p_alias_guid->p_base_port;
}

osm_assigned_guids_t *osm_assigned_guids_new(IN const ib_net64_t port_guid,
					     IN const uint32_t num_guids)
{
	osm_assigned_guids_t *p_assigned_guids;

	p_assigned_guids = calloc(1, sizeof(*p_assigned_guids) +
				     sizeof(ib_net64_t) * (num_guids - 1));
	if (p_assigned_guids)
		p_assigned_guids->port_guid = port_guid;
	return p_assigned_guids;
}

void osm_assigned_guids_delete(IN OUT osm_assigned_guids_t ** pp_assigned_guids)
{
	free(*pp_assigned_guids);
	*pp_assigned_guids = NULL;
}

osm_assigned_guids_t *osm_get_assigned_guids_by_guid(IN osm_subn_t const *p_subn,
						     IN ib_net64_t port_guid)
{
	osm_assigned_guids_t *p_assigned_guids;

	p_assigned_guids = (osm_assigned_guids_t *) cl_qmap_get(&(p_subn->assigned_guids_tbl), port_guid);
	if (p_assigned_guids == (osm_assigned_guids_t *) cl_qmap_end(&(p_subn->assigned_guids_tbl)))
		return NULL;
	return p_assigned_guids;
}

osm_port_t *osm_get_port_by_lid_ho(IN osm_subn_t const * subn, IN uint16_t lid)
{
	if (lid < cl_ptr_vector_get_size(&subn->port_lid_tbl))
		return cl_ptr_vector_get(&subn->port_lid_tbl, lid);
	return NULL;
}

osm_mgrp_t *osm_get_mgrp_by_mgid(IN osm_subn_t * subn, IN ib_gid_t * mgid)
{
	osm_mgrp_t *mgrp;

	mgrp = (osm_mgrp_t *)cl_fmap_get(&subn->mgrp_mgid_tbl, mgid);
	if (mgrp != (osm_mgrp_t *)cl_fmap_end(&subn->mgrp_mgid_tbl))
		return mgrp;
	return NULL;
}

int is_mlnx_ext_port_info_supported(ib_net32_t vendid, ib_net16_t devid)
{
	uint32_t vendid_ho;
	uint16_t devid_ho;

	devid_ho = cl_ntoh16(devid);
	if ((devid_ho >= 0xc738 && devid_ho <= 0xc73b) || devid_ho == 0xcb20 ||
	    devid_ho == 0xcf08 || devid == 0x1b02)
		return 1;
	if (devid_ho >= 0x1003 && devid_ho <= 0x1017)
		return 1;

	vendid_ho = cl_ntoh32(vendid);
	if (vendid_ho == 0x119f) {
		/* Bull Switch-X */
		if (devid_ho == 0x1b02 || devid_ho == 0x1b50)
			return 1;
		/* Bull Switch-IB/IB2 */
		if (devid_ho == 0x1ba0 ||
		    (devid_ho >= 0x1bd0 && devid_ho <= 0x1bd5))
			return 1;
		/* Bull Connect-X3 */
		if (devid_ho == 0x1b33 || devid_ho == 0x1b73 ||
		    devid_ho == 0x1b40 || devid_ho == 0x1b41 ||
		    devid_ho == 0x1b60 || devid_ho == 0x1b61)
			return 1;
		/* Bull Connect-IB */
		if (devid_ho == 0x1b83 ||
		    devid_ho == 0x1b93 || devid_ho == 0x1b94)
			return 1;
		/* Bull Connect-X4 */
		if (devid_ho == 0x1bb4 || devid_ho == 0x1bb5 ||
		    devid_ho == 0x1bc4)
			return 1;
	}
	return 0;
}

static void subn_init_qos_options(osm_qos_options_t *opt, osm_qos_options_t *f)
{
	opt->max_vls = 0;
	opt->high_limit = -1;
	if (opt->vlarb_high)
		free(opt->vlarb_high);
	opt->vlarb_high = NULL;
	if (opt->vlarb_low)
		free(opt->vlarb_low);
	opt->vlarb_low = NULL;
	if (opt->sl2vl)
		free(opt->sl2vl);
	opt->sl2vl = NULL;
	if (f)
		memcpy(f, opt, sizeof(*f));
}

void osm_subn_set_default_opt(IN osm_subn_opt_t * p_opt)
{
	memset(p_opt, 0, sizeof(osm_subn_opt_t));
	p_opt->guid = 0;
	p_opt->m_key = OSM_DEFAULT_M_KEY;
	p_opt->sm_key = OSM_DEFAULT_SM_KEY;
	p_opt->sa_key = OSM_DEFAULT_SA_KEY;
	p_opt->subnet_prefix = IB_DEFAULT_SUBNET_PREFIX;
	p_opt->m_key_lease_period = 0;
	p_opt->m_key_protect_bits = 0;
	p_opt->m_key_lookup = TRUE;
	p_opt->sweep_interval = OSM_DEFAULT_SWEEP_INTERVAL_SECS;
	p_opt->max_wire_smps = OSM_DEFAULT_SMP_MAX_ON_WIRE;
	p_opt->max_wire_smps2 = p_opt->max_wire_smps;
	p_opt->console = strdup(OSM_DEFAULT_CONSOLE);
	p_opt->console_port = OSM_DEFAULT_CONSOLE_PORT;
	p_opt->transaction_timeout = OSM_DEFAULT_TRANS_TIMEOUT_MILLISEC;
	p_opt->transaction_retries = OSM_DEFAULT_RETRY_COUNT;
	p_opt->max_smps_timeout = 1000 * p_opt->transaction_timeout *
				  p_opt->transaction_retries;
	/* by default we will consider waiting for 50x transaction timeout normal */
	p_opt->max_msg_fifo_timeout = 50 * OSM_DEFAULT_TRANS_TIMEOUT_MILLISEC;
	p_opt->sm_priority = OSM_DEFAULT_SM_PRIORITY;
	p_opt->lmc = OSM_DEFAULT_LMC;
	p_opt->lmc_esp0 = FALSE;
	p_opt->max_op_vls = OSM_DEFAULT_MAX_OP_VLS;
	p_opt->force_link_speed = 15;
	p_opt->force_link_speed_ext = 31;
	p_opt->fdr10 = 1;
	p_opt->reassign_lids = FALSE;
	p_opt->ignore_other_sm = FALSE;
	p_opt->single_thread = FALSE;
	p_opt->disable_multicast = FALSE;
	p_opt->force_log_flush = FALSE;
	p_opt->subnet_timeout = OSM_DEFAULT_SUBNET_TIMEOUT;
	p_opt->packet_life_time = OSM_DEFAULT_SWITCH_PACKET_LIFE;
	p_opt->vl_stall_count = OSM_DEFAULT_VL_STALL_COUNT;
	p_opt->leaf_vl_stall_count = OSM_DEFAULT_LEAF_VL_STALL_COUNT;
	p_opt->head_of_queue_lifetime = OSM_DEFAULT_HEAD_OF_QUEUE_LIFE;
	p_opt->leaf_head_of_queue_lifetime =
	    OSM_DEFAULT_LEAF_HEAD_OF_QUEUE_LIFE;
	p_opt->local_phy_errors_threshold = OSM_DEFAULT_ERROR_THRESHOLD;
	p_opt->overrun_errors_threshold = OSM_DEFAULT_ERROR_THRESHOLD;
	p_opt->use_mfttop = TRUE;
	p_opt->sminfo_polling_timeout =
	    OSM_SM_DEFAULT_POLLING_TIMEOUT_MILLISECS;
	p_opt->polling_retry_number = OSM_SM_DEFAULT_POLLING_RETRY_NUMBER;
	p_opt->force_heavy_sweep = FALSE;
	p_opt->log_flags = OSM_LOG_DEFAULT_LEVEL;
	p_opt->honor_guid2lid_file = FALSE;
	p_opt->daemon = FALSE;
	p_opt->sm_inactive = FALSE;
	p_opt->babbling_port_policy = FALSE;
	p_opt->drop_event_subscriptions = FALSE;
	p_opt->ipoib_mcgroup_creation_validation = TRUE;
	p_opt->mcgroup_join_validation = TRUE;
	p_opt->use_optimized_slvl = FALSE;
	p_opt->fsync_high_avail_files = TRUE;
#ifdef ENABLE_OSM_PERF_MGR
	p_opt->perfmgr = FALSE;
	p_opt->perfmgr_redir = TRUE;
	p_opt->perfmgr_sweep_time_s = OSM_PERFMGR_DEFAULT_SWEEP_TIME_S;
	p_opt->perfmgr_max_outstanding_queries =
	    OSM_PERFMGR_DEFAULT_MAX_OUTSTANDING_QUERIES;
	p_opt->perfmgr_ignore_cas = FALSE;
	p_opt->event_db_dump_file = NULL; /* use default */
	p_opt->perfmgr_rm_nodes = TRUE;
	p_opt->perfmgr_log_errors = TRUE;
	p_opt->perfmgr_query_cpi = TRUE;
	p_opt->perfmgr_xmit_wait_log = FALSE;
	p_opt->perfmgr_xmit_wait_threshold = OSM_PERFMGR_DEFAULT_XMIT_WAIT_THRESHOLD;
#endif				/* ENABLE_OSM_PERF_MGR */

	p_opt->event_plugin_name = NULL;
	p_opt->event_plugin_options = NULL;
	p_opt->node_name_map_name = NULL;

	p_opt->dump_files_dir = getenv("OSM_TMP_DIR");
	if (!p_opt->dump_files_dir || !(*p_opt->dump_files_dir))
		p_opt->dump_files_dir = strdup(OSM_DEFAULT_TMP_DIR);
	else
		p_opt->dump_files_dir = strdup(p_opt->dump_files_dir);
	p_opt->log_file = strdup(OSM_DEFAULT_LOG_FILE);
	p_opt->log_max_size = 0;
	p_opt->partition_config_file = strdup(OSM_DEFAULT_PARTITION_CONFIG_FILE);
	p_opt->no_partition_enforcement = FALSE;
	p_opt->part_enforce = strdup(OSM_PARTITION_ENFORCE_BOTH);
	p_opt->allow_both_pkeys = FALSE;
	p_opt->sm_assigned_guid = 0;
	p_opt->qos = FALSE;
	p_opt->qos_policy_file = strdup(OSM_DEFAULT_QOS_POLICY_FILE);
	p_opt->suppress_sl2vl_mad_status_errors = FALSE;
	p_opt->accum_log_file = TRUE;
	p_opt->port_prof_ignore_file = NULL;
	p_opt->hop_weights_file = NULL;
	p_opt->port_search_ordering_file = NULL;
	p_opt->port_profile_switch_nodes = FALSE;
	p_opt->sweep_on_trap = TRUE;
	p_opt->use_ucast_cache = FALSE;
	p_opt->routing_engine_names = NULL;
	p_opt->connect_roots = FALSE;
	p_opt->lid_matrix_dump_file = NULL;
	p_opt->lfts_file = NULL;
	p_opt->root_guid_file = NULL;
	p_opt->cn_guid_file = NULL;
	p_opt->io_guid_file = NULL;
	p_opt->port_shifting = FALSE;
	p_opt->scatter_ports = OSM_DEFAULT_SCATTER_PORTS;
	p_opt->max_reverse_hops = 0;
	p_opt->ids_guid_file = NULL;
	p_opt->guid_routing_order_file = NULL;
	p_opt->guid_routing_order_no_scatter = FALSE;
	p_opt->sa_db_file = NULL;
	p_opt->sa_db_dump = FALSE;
	p_opt->torus_conf_file = strdup(OSM_DEFAULT_TORUS_CONF_FILE);
	p_opt->do_mesh_analysis = FALSE;
	p_opt->exit_on_fatal = TRUE;
	p_opt->congestion_control = FALSE;
	p_opt->cc_key = OSM_DEFAULT_CC_KEY;
	p_opt->cc_max_outstanding_mads = OSM_CC_DEFAULT_MAX_OUTSTANDING_QUERIES;
	p_opt->enable_quirks = FALSE;
	p_opt->no_clients_rereg = FALSE;
	p_opt->prefix_routes_file = strdup(OSM_DEFAULT_PREFIX_ROUTES_FILE);
	p_opt->consolidate_ipv6_snm_req = FALSE;
	p_opt->lash_start_vl = 0;
	p_opt->sm_sl = OSM_DEFAULT_SL;
	p_opt->log_prefix = NULL;
	p_opt->per_module_logging_file = strdup(OSM_DEFAULT_PER_MOD_LOGGING_CONF_FILE);
	subn_init_qos_options(&p_opt->qos_options, NULL);
	subn_init_qos_options(&p_opt->qos_ca_options, NULL);
	subn_init_qos_options(&p_opt->qos_sw0_options, NULL);
	subn_init_qos_options(&p_opt->qos_swe_options, NULL);
	subn_init_qos_options(&p_opt->qos_rtr_options, NULL);
	p_opt->cc_cct.entries_len = 0;
	p_opt->cc_cct.input_str = NULL;
	p_opt->quasi_ftree_indexing = FALSE;
}

static char *clean_val(char *val)
{
	char *p = val;
	/* clean leading spaces */
	while (isspace(*p))
		p++;
	val = p;
	if (!*val)
		return val;
	/* clean trailing spaces */
	p = val + strlen(val) - 1;
	while (p > val && isspace(*p))
		p--;
	p[1] = '\0';
	/* clean quotas */
	if ((*val == '\"' && *p == '\"') || (*val == '\'' && *p == '\'')) {
		val++;
		*p-- = '\0';
	}
	return val;
}

static int subn_dump_qos_options(FILE * file, const char *set_name,
				 const char *prefix, osm_qos_options_t * opt)
{
	return fprintf(file, "# %s\n"
		       "%s_max_vls %u\n"
		       "%s_high_limit %d\n"
		       "%s_vlarb_high %s\n"
		       "%s_vlarb_low %s\n"
		       "%s_sl2vl %s\n",
		       set_name,
		       prefix, opt->max_vls,
		       prefix, opt->high_limit,
		       prefix, opt->vlarb_high,
		       prefix, opt->vlarb_low, prefix, opt->sl2vl);
}

static ib_api_status_t append_prefix_route(IN osm_subn_t * p_subn,
					   uint64_t prefix, uint64_t guid)
{
	osm_prefix_route_t *route;

	route = malloc(sizeof *route);
	if (! route) {
		OSM_LOG(&p_subn->p_osm->log, OSM_LOG_ERROR, "out of memory");
		return IB_ERROR;
	}

	route->prefix = cl_hton64(prefix);
	route->guid = cl_hton64(guid);
	cl_qlist_insert_tail(&p_subn->prefix_routes_list, &route->list_item);
	return IB_SUCCESS;
}

static ib_api_status_t parse_prefix_routes_file(IN osm_subn_t * p_subn)
{
	osm_log_t *log = &p_subn->p_osm->log;
	FILE *fp;
	char buf[1024];
	int line = 0;
	int errors = 0;

	while (!cl_is_qlist_empty(&p_subn->prefix_routes_list)) {
		cl_list_item_t *item = cl_qlist_remove_head(&p_subn->prefix_routes_list);
		free(item);
	}

	fp = fopen(p_subn->opt.prefix_routes_file, "r");
	if (! fp) {
		if (errno == ENOENT)
			return IB_SUCCESS;

		OSM_LOG(log, OSM_LOG_ERROR, "fopen(%s) failed: %s",
			p_subn->opt.prefix_routes_file, strerror(errno));
		return IB_ERROR;
	}

	while (fgets(buf, sizeof buf, fp) != NULL) {
		char *p_prefix, *p_guid, *p_extra, *p_last, *p_end;
		uint64_t prefix, guid;

		line++;
		if (errors > 10)
			break;

		p_prefix = strtok_r(buf, " \t\n", &p_last);
		if (! p_prefix)
			continue; /* ignore blank lines */

		if (*p_prefix == '#')
			continue; /* ignore comment lines */

		p_guid = strtok_r(NULL, " \t\n", &p_last);
		if (! p_guid) {
			OSM_LOG(log, OSM_LOG_ERROR, "%s:%d: missing GUID\n",
				p_subn->opt.prefix_routes_file, line);
			errors++;
			continue;
		}

		p_extra = strtok_r(NULL, " \t\n", &p_last);
		if (p_extra && *p_extra != '#') {
			OSM_LOG(log, OSM_LOG_INFO, "%s:%d: extra tokens ignored\n",
				p_subn->opt.prefix_routes_file, line);
		}

		if (strcmp(p_prefix, "*") == 0)
			prefix = 0;
		else {
			prefix = strtoull(p_prefix, &p_end, 16);
			if (*p_end != '\0') {
				OSM_LOG(log, OSM_LOG_ERROR, "%s:%d: illegal prefix: %s\n",
					p_subn->opt.prefix_routes_file, line, p_prefix);
				errors++;
				continue;
			}
		}

		if (strcmp(p_guid, "*") == 0)
			guid = 0;
		else {
			guid = strtoull(p_guid, &p_end, 16);
			if (*p_end != '\0' && *p_end != '#') {
				OSM_LOG(log, OSM_LOG_ERROR, "%s:%d: illegal GUID: %s\n",
					p_subn->opt.prefix_routes_file, line, p_guid);
				errors++;
				continue;
			}
		}

		if (append_prefix_route(p_subn, prefix, guid) != IB_SUCCESS) {
			errors++;
			break;
		}
	}

	fclose(fp);
	return (errors == 0) ? IB_SUCCESS : IB_ERROR;
}

static ib_api_status_t insert_per_module_debug(IN osm_subn_t * p_subn,
					       char *mod_name,
					       osm_log_level_t level)
{
	uint8_t index;

	if (find_module_name(mod_name, &index)) {
		OSM_LOG(&p_subn->p_osm->log, OSM_LOG_ERROR,
			"Module name %s not found\n", mod_name);
		return IB_ERROR;
	}
	osm_set_log_per_module(&p_subn->p_osm->log, index, level);
	return IB_SUCCESS;
}

static ib_api_status_t parse_per_mod_logging_file(IN osm_subn_t * p_subn)
{
	osm_log_t *log = &p_subn->p_osm->log;
	FILE *fp;
	char buf[1024];
	int line = 0;
	int errors = 0;

	osm_reset_log_per_module(log);

	if (p_subn->opt.per_module_logging_file == NULL)
		return IB_SUCCESS;

	fp = fopen(p_subn->opt.per_module_logging_file, "r");
	if (!fp) {
		if (errno == ENOENT)
			return IB_SUCCESS;

		OSM_LOG(log, OSM_LOG_ERROR, "fopen(%s) failed: %s",
			p_subn->opt.per_module_logging_file, strerror(errno));
		return IB_ERROR;
	}

	while (fgets(buf, sizeof buf, fp) != NULL) {
		char *p_mod_name, *p_level, *p_extra, *p_last;
		osm_log_level_t level;

		line++;
		if (errors > 10)
			break;

		p_mod_name = strtok_r(buf, " =,\t\n", &p_last);
		if (!p_mod_name)
			continue; /* ignore blank lines */

		if (*p_mod_name == '#')
			continue; /* ignore comment lines */

		p_level = strtok_r(NULL, " \t\n", &p_last);
		if (!p_level) {
			OSM_LOG(log, OSM_LOG_ERROR, "%s:%d: missing log level\n",
				p_subn->opt.per_module_logging_file, line);
			errors++;
			continue;
		}
		p_extra = strtok_r(NULL, " \t\n", &p_last);
		if (p_extra && *p_extra != '#') {
			OSM_LOG(log, OSM_LOG_INFO, "%s:%d: extra tokens ignored\n",
				p_subn->opt.per_module_logging_file, line);
		}

		level = strtoul(p_level, NULL, 0);
		if (insert_per_module_debug(p_subn, p_mod_name, level) != IB_SUCCESS) {
			errors++;
			break;
		}
	}

	fclose(fp);
	return (errors == 0) ? IB_SUCCESS : IB_ERROR;
}

static void subn_verify_max_vls(unsigned *max_vls, const char *prefix)
{
	if (!*max_vls || *max_vls > 15) {
		if (*max_vls)
			log_report(" Invalid Cached Option: %s_max_vls=%u: "
				   "Using Default = %u\n",
				   prefix, *max_vls, OSM_DEFAULT_QOS_MAX_VLS);
		*max_vls = 0;
	}
}

static void subn_verify_high_limit(int *high_limit, const char *prefix)
{
	if (*high_limit < 0 || *high_limit > 255) {
		if (*high_limit > 255)
			log_report(" Invalid Cached Option: %s_high_limit=%d: "
				   "Using Default: %d\n",
				   prefix, *high_limit,
				   OSM_DEFAULT_QOS_HIGH_LIMIT);
		*high_limit = -1;
	}
}

static void subn_verify_vlarb(char **vlarb, const char *prefix,
			      const char *suffix)
{
	char *str, *tok, *end, *ptr;
	int count = 0;

	if (*vlarb == NULL)
		return;

	str = strdup(*vlarb);

	tok = strtok_r(str, ",\n", &ptr);
	while (tok) {
		char *vl_str, *weight_str;

		vl_str = tok;
		weight_str = strchr(tok, ':');

		if (weight_str) {
			long vl, weight;

			*weight_str = '\0';
			weight_str++;

			vl = strtol(vl_str, &end, 0);

			if (*end)
				log_report(" Warning: Cached Option "
					   "%s_vlarb_%s:vl=%s"
					   " improperly formatted\n",
					   prefix, suffix, vl_str);
			else if (vl < 0 || vl > 14)
				log_report(" Warning: Cached Option "
					   "%s_vlarb_%s:vl=%ld out of range\n",
					   prefix, suffix, vl);

			weight = strtol(weight_str, &end, 0);

			if (*end)
				log_report(" Warning: Cached Option "
					   "%s_vlarb_%s:weight=%s "
					   "improperly formatted\n",
					   prefix, suffix, weight_str);
			else if (weight < 0 || weight > 255)
				log_report(" Warning: Cached Option "
					   "%s_vlarb_%s:weight=%ld "
					   "out of range\n",
					   prefix, suffix, weight);
		} else
			log_report(" Warning: Cached Option "
				   "%s_vlarb_%s:vl:weight=%s "
				   "improperly formatted\n",
				   prefix, suffix, tok);

		count++;
		tok = strtok_r(NULL, ",\n", &ptr);
	}

	if (count > 64)
		log_report(" Warning: Cached Option %s_vlarb_%s: > 64 listed:"
			   " excess vl:weight pairs will be dropped\n",
			   prefix, suffix);

	free(str);
}

static void subn_verify_sl2vl(char **sl2vl, const char *prefix)
{
	char *str, *tok, *end, *ptr;
	int count = 0;

	if (*sl2vl == NULL)
		return;

	str = strdup(*sl2vl);

	tok = strtok_r(str, ",\n", &ptr);
	while (tok) {
		long vl = strtol(tok, &end, 0);

		if (*end)
			log_report(" Warning: Cached Option %s_sl2vl:vl=%s "
				   "improperly formatted\n", prefix, tok);
		else if (vl < 0 || vl > 15)
			log_report(" Warning: Cached Option %s_sl2vl:vl=%ld "
				   "out of range\n", prefix, vl);

		count++;
		tok = strtok_r(NULL, ",\n", &ptr);
	}

	if (count < 16)
		log_report(" Warning: Cached Option %s_sl2vl: < 16 VLs "
			   "listed\n", prefix);
	else if (count > 16)
		log_report(" Warning: Cached Option %s_sl2vl: > 16 listed: "
			   "excess VLs will be dropped\n", prefix);

	free(str);
}

static void subn_verify_qos_set(osm_qos_options_t *set, const char *prefix)
{
	subn_verify_max_vls(&set->max_vls, prefix);
	subn_verify_high_limit(&set->high_limit, prefix);
	subn_verify_vlarb(&set->vlarb_low, prefix, "low");
	subn_verify_vlarb(&set->vlarb_high, prefix, "high");
	subn_verify_sl2vl(&set->sl2vl, prefix);
}

int osm_subn_verify_config(IN osm_subn_opt_t * p_opts)
{
	if (p_opts->lmc > 7) {
		log_report(" Invalid Cached Option Value:lmc = %u:"
			   "Using Default:%u\n", p_opts->lmc, OSM_DEFAULT_LMC);
		p_opts->lmc = OSM_DEFAULT_LMC;
	}

	if (15 < p_opts->sm_priority) {
		log_report(" Invalid Cached Option Value:sm_priority = %u:"
			   "Using Default:%u\n",
			   p_opts->sm_priority, OSM_DEFAULT_SM_PRIORITY);
		p_opts->sm_priority = OSM_DEFAULT_SM_PRIORITY;
	}

	if ((15 < p_opts->force_link_speed) ||
	    (p_opts->force_link_speed > 7 && p_opts->force_link_speed < 15)) {
		log_report(" Invalid Cached Option Value:force_link_speed = %u:"
			   "Using Default:%u\n", p_opts->force_link_speed,
			   IB_PORT_LINK_SPEED_ENABLED_MASK);
		p_opts->force_link_speed = IB_PORT_LINK_SPEED_ENABLED_MASK;
	}

	if ((31 < p_opts->force_link_speed_ext) ||
	    (p_opts->force_link_speed_ext > 3 && p_opts->force_link_speed_ext < 30)) {
		log_report(" Invalid Cached Option Value:force_link_speed_ext = %u:"
			   "Using Default:%u\n", p_opts->force_link_speed_ext,
			   31);
		p_opts->force_link_speed_ext = 31;
	}

	if (2 < p_opts->fdr10) {
		log_report(" Invalid Cached Option Value:fdr10 = %u:"
			   "Using Default:%u\n", p_opts->fdr10, 1);
		p_opts->fdr10 = 1;
	}

	if (p_opts->max_wire_smps == 0)
		p_opts->max_wire_smps = 0x7FFFFFFF;
	else if (p_opts->max_wire_smps > 0x7FFFFFFF) {
		log_report(" Invalid Cached Option Value: max_wire_smps = %u,"
			   " Using Default: %u\n",
			   p_opts->max_wire_smps, OSM_DEFAULT_SMP_MAX_ON_WIRE);
		p_opts->max_wire_smps = OSM_DEFAULT_SMP_MAX_ON_WIRE;
	}

	if (p_opts->max_wire_smps2 > 0x7FFFFFFF) {
		log_report(" Invalid Cached Option Value: max_wire_smps2 = %u,"
			   " Using Default: %u",
			   p_opts->max_wire_smps2, p_opts->max_wire_smps);
		p_opts->max_wire_smps2 = p_opts->max_wire_smps;
	}

	if (strcmp(p_opts->console, OSM_DISABLE_CONSOLE)
	    && strcmp(p_opts->console, OSM_LOCAL_CONSOLE)
#ifdef ENABLE_OSM_CONSOLE_LOOPBACK
	    && strcmp(p_opts->console, OSM_LOOPBACK_CONSOLE)
#endif
#ifdef ENABLE_OSM_CONSOLE_SOCKET
	    && strcmp(p_opts->console, OSM_REMOTE_CONSOLE)
#endif
	    ) {
		log_report(" Invalid Cached Option Value:console = %s"
			   ", Using Default:%s\n",
			   p_opts->console, OSM_DEFAULT_CONSOLE);
		free(p_opts->console);
		p_opts->console = strdup(OSM_DEFAULT_CONSOLE);
	}

	if (p_opts->no_partition_enforcement == TRUE) {
		strcpy(p_opts->part_enforce, OSM_PARTITION_ENFORCE_OFF);
		p_opts->part_enforce_enum = OSM_PARTITION_ENFORCE_TYPE_OFF;
	} else {
		if (strcmp(p_opts->part_enforce, OSM_PARTITION_ENFORCE_BOTH) == 0)
			p_opts->part_enforce_enum = OSM_PARTITION_ENFORCE_TYPE_BOTH;
		else if (strcmp(p_opts->part_enforce, OSM_PARTITION_ENFORCE_IN) == 0)
			p_opts->part_enforce_enum = OSM_PARTITION_ENFORCE_TYPE_IN;
		else if (strcmp(p_opts->part_enforce, OSM_PARTITION_ENFORCE_OUT) == 0)
			p_opts->part_enforce_enum = OSM_PARTITION_ENFORCE_TYPE_OUT;
		else if (strcmp(p_opts->part_enforce, OSM_PARTITION_ENFORCE_OFF) == 0)
			p_opts->part_enforce_enum = OSM_PARTITION_ENFORCE_TYPE_OFF;
		else {
			log_report(" Invalid Cached Option Value:part_enforce = %s"
	                           ", Using Default:%s\n",
	                           p_opts->part_enforce, OSM_PARTITION_ENFORCE_BOTH);
			strcpy(p_opts->part_enforce, OSM_PARTITION_ENFORCE_BOTH);
			p_opts->part_enforce_enum = OSM_PARTITION_ENFORCE_TYPE_BOTH;
		}
	}

	if (p_opts->qos) {
		subn_verify_qos_set(&p_opts->qos_options, "qos");
		subn_verify_qos_set(&p_opts->qos_ca_options, "qos_ca");
		subn_verify_qos_set(&p_opts->qos_sw0_options, "qos_sw0");
		subn_verify_qos_set(&p_opts->qos_swe_options, "qos_swe");
		subn_verify_qos_set(&p_opts->qos_rtr_options, "qos_rtr");
	}

#ifdef ENABLE_OSM_PERF_MGR
	if (p_opts->perfmgr_sweep_time_s < 1) {
		log_report(" Invalid Cached Option Value:perfmgr_sweep_time_s "
			   "= %u Using Default:%u\n",
			   p_opts->perfmgr_sweep_time_s,
			   OSM_PERFMGR_DEFAULT_SWEEP_TIME_S);
		p_opts->perfmgr_sweep_time_s = OSM_PERFMGR_DEFAULT_SWEEP_TIME_S;
	}
	if (p_opts->perfmgr_max_outstanding_queries < 1) {
		log_report(" Invalid Cached Option Value:"
			   "perfmgr_max_outstanding_queries = %u"
			   " Using Default:%u\n",
			   p_opts->perfmgr_max_outstanding_queries,
			   OSM_PERFMGR_DEFAULT_MAX_OUTSTANDING_QUERIES);
		p_opts->perfmgr_max_outstanding_queries =
		    OSM_PERFMGR_DEFAULT_MAX_OUTSTANDING_QUERIES;
	}
#endif

	if (p_opts->m_key_protect_bits > 3) {
		log_report(" Invalid Cached Option Value:"
			   "m_key_protection_level = %u Setting to %u "
			   "instead\n", p_opts->m_key_protect_bits, 2);
		p_opts->m_key_protect_bits = 2;
	}
	if (p_opts->m_key_protect_bits && p_opts->m_key_lease_period) {
		if (!p_opts->sweep_interval) {
			log_report(" Sweep disabled with protected mkey "
				   "leases in effect; re-enabling sweeping "
				   "with interval %u\n",
				   cl_ntoh16(p_opts->m_key_lease_period) - 1);
			p_opts->sweep_interval =
				cl_ntoh16(p_opts->m_key_lease_period) - 1;
		}
		if (p_opts->sweep_interval >=
			cl_ntoh16(p_opts->m_key_lease_period)) {
			log_report(" Sweep interval %u >= mkey lease period "
				   "%u. Setting lease period to %u\n",
				   p_opts->sweep_interval,
				   cl_ntoh16(p_opts->m_key_lease_period),
				   p_opts->sweep_interval + 1);
			p_opts->m_key_lease_period =
				cl_hton16(p_opts->sweep_interval + 1);
		}
	}

	return 0;
}

int osm_subn_parse_conf_file(const char *file_name, osm_subn_opt_t * p_opts)
{
	char line[1024];
	FILE *opts_file;
	char *p_key, *p_val, *pound_sign;
	const opt_rec_t *r;
	void *p_field1, *p_field2;
	int token_matched;

	opts_file = fopen(file_name, "r");
	if (!opts_file) {
		if (errno == ENOENT)
			return 1;
		printf("cannot open file \'%s\': %s\n",
		       file_name, strerror(errno));
		return -1;
	}

	printf(" Reading Cached Option File: %s\n", file_name);

	p_opts->config_file = file_name;
	if (!p_opts->file_opts && !(p_opts->file_opts = malloc(sizeof(*p_opts)))) {
		fclose(opts_file);
		return -1;
	}
	memcpy(p_opts->file_opts, p_opts, sizeof(*p_opts));
	p_opts->file_opts->file_opts = NULL;

	while (fgets(line, 1023, opts_file) != NULL) {
		pound_sign = strchr(line,'#');
		token_matched = 0;
		/* Truncate any comments. */
		if (pound_sign)
			*pound_sign = '\0';

		/* get the first token */
		p_key = strtok_r(line, " \t\n", &p_val);
		if (!p_key)
			continue;

		p_val = clean_val(p_val);

		for (r = opt_tbl; r->name; r++) {
			if (strcmp(r->name, p_key))
				continue;

			token_matched = 1;
			p_field1 = (void *)p_opts->file_opts + r->opt_offset;
			p_field2 = (void *)p_opts + r->opt_offset;
			/* don't call setup function first time */
			r->parse_fn(NULL, p_key, p_val, p_field1, p_field2,
				    NULL);
			break;
		}

		if (!token_matched)
			log_report(" Unrecognized token: \"%s\"\n", p_key);
	}
	fclose(opts_file);

	osm_subn_verify_config(p_opts);

	return 0;
}

int osm_subn_rescan_conf_files(IN osm_subn_t * p_subn)
{
	char line[1024];
	osm_subn_opt_t *p_opts = &p_subn->opt;
	const opt_rec_t *r;
	FILE *opts_file;
	char *p_key, *p_val, *pound_sign;
	void *p_field1, *p_field2;
	int token_matched;

	if (!p_opts->config_file)
		return 0;

	opts_file = fopen(p_opts->config_file, "r");
	if (!opts_file) {
		if (errno == ENOENT)
			return 1;
		OSM_LOG(&p_subn->p_osm->log, OSM_LOG_ERROR,
			"cannot open file \'%s\': %s\n",
			p_opts->config_file, strerror(errno));
		return -1;
	}

	subn_init_qos_options(&p_opts->qos_options,
			      &p_opts->file_opts->qos_options);
	subn_init_qos_options(&p_opts->qos_ca_options,
			      &p_opts->file_opts->qos_ca_options);
	subn_init_qos_options(&p_opts->qos_sw0_options,
			      &p_opts->file_opts->qos_sw0_options);
	subn_init_qos_options(&p_opts->qos_swe_options,
			      &p_opts->file_opts->qos_swe_options);
	subn_init_qos_options(&p_opts->qos_rtr_options,
			      &p_opts->file_opts->qos_rtr_options);

	while (fgets(line, 1023, opts_file) != NULL) {
		pound_sign = strchr(line,'#');
		token_matched = 0;

		/* Truncate any comments. */
		if (pound_sign)
			*pound_sign = '\0';

		/* get the first token */
		p_key = strtok_r(line, " \t\n", &p_val);
		if (!p_key)
			continue;

		p_val = clean_val(p_val);

		for (r = opt_tbl; r->name; r++) {
			if (strcmp(r->name, p_key))
				continue;

			token_matched = 1;

			if (!r->can_update)
				continue;

			p_field1 = (void *)p_opts->file_opts + r->opt_offset;
			p_field2 = (void *)p_opts + r->opt_offset;
			r->parse_fn(p_subn, p_key, p_val, p_field1, p_field2,
				    r->setup_fn);
			break;
		}
		if (!token_matched)
                       log_report(" Unrecognized token: \"%s\"\n", p_key);
	}
	fclose(opts_file);

	osm_subn_verify_config(p_opts);

	parse_prefix_routes_file(p_subn);

	parse_per_mod_logging_file(p_subn);

	return 0;
}

void osm_subn_output_conf(FILE *out, IN osm_subn_opt_t * p_opts)
{
	int cacongoutputcount = 0;
	int i;

	fprintf(out,
		"#\n# DEVICE ATTRIBUTES OPTIONS\n#\n"
		"# The port GUID on which the OpenSM is running\n"
		"guid 0x%016" PRIx64 "\n\n"
		"# M_Key value sent to all ports qualifying all Set(PortInfo)\n"
		"m_key 0x%016" PRIx64 "\n\n"
		"# The lease period used for the M_Key on this subnet in [sec]\n"
		"m_key_lease_period %u\n\n"
		"# The protection level used for the M_Key on this subnet\n"
		"m_key_protection_level %u\n\n"
		"# If TRUE, SM tries to determine the m_key of unknown ports from guid2mkey file\n"
		"# If FALSE, SM won't try to determine the m_key of unknown ports.\n"
		"# Preconfigured m_key will be used instead\n"
		"m_key_lookup %s\n\n"
		"# SM_Key value of the SM used for SM authentication\n"
		"sm_key 0x%016" PRIx64 "\n\n"
		"# SM_Key value to qualify rcv SA queries as 'trusted'\n"
		"sa_key 0x%016" PRIx64 "\n\n"
		"# Note that for both values above (sm_key and sa_key)\n"
		"# OpenSM version 3.2.1 and below used the default value '1'\n"
		"# in a host byte order, it is fixed now but you may need to\n"
		"# change the values to interoperate with old OpenSM running\n"
		"# on a little endian machine.\n\n"
		"# Subnet prefix used on this subnet\n"
		"subnet_prefix 0x%016" PRIx64 "\n\n"
		"# The LMC value used on this subnet\n"
		"lmc %u\n\n"
		"# lmc_esp0 determines whether LMC value used on subnet is used for\n"
		"# enhanced switch port 0. If TRUE, LMC value for subnet is used for\n"
		"# ESP0. Otherwise, LMC value for ESP0s is 0.\n"
		"lmc_esp0 %s\n\n"
		"# sm_sl determines SMSL used for SM/SA communication\n"
		"sm_sl %u\n\n"
		"# The code of maximal time a packet can live in a switch\n"
		"# The actual time is 4.096usec * 2^<packet_life_time>\n"
		"# The value 0x14 disables this mechanism\n"
		"packet_life_time 0x%02x\n\n"
		"# The number of sequential packets dropped that cause the port\n"
		"# to enter the VLStalled state. The result of setting this value to\n"
		"# zero is undefined.\n"
		"vl_stall_count 0x%02x\n\n"
		"# The number of sequential packets dropped that cause the port\n"
		"# to enter the VLStalled state. This value is for switch ports\n"
		"# driving a CA or router port. The result of setting this value\n"
		"# to zero is undefined.\n"
		"leaf_vl_stall_count 0x%02x\n\n"
		"# The code of maximal time a packet can wait at the head of\n"
		"# transmission queue.\n"
		"# The actual time is 4.096usec * 2^<head_of_queue_lifetime>\n"
		"# The value 0x14 disables this mechanism\n"
		"head_of_queue_lifetime 0x%02x\n\n"
		"# The maximal time a packet can wait at the head of queue on\n"
		"# switch port connected to a CA or router port\n"
		"leaf_head_of_queue_lifetime 0x%02x\n\n"
		"# Limit the maximal operational VLs\n"
		"max_op_vls %u\n\n"
		"# Force PortInfo:LinkSpeedEnabled on switch ports\n"
		"# If 0, don't modify PortInfo:LinkSpeedEnabled on switch port\n"
		"# Otherwise, use value for PortInfo:LinkSpeedEnabled on switch port\n"
		"# Values are (IB Spec 1.2.1, 14.2.5.6 Table 146 \"PortInfo\")\n"
		"#    1: 2.5 Gbps\n"
		"#    3: 2.5 or 5.0 Gbps\n"
		"#    5: 2.5 or 10.0 Gbps\n"
		"#    7: 2.5 or 5.0 or 10.0 Gbps\n"
		"#    2,4,6,8-14 Reserved\n"
		"#    Default 15: set to PortInfo:LinkSpeedSupported\n"
		"force_link_speed %u\n\n"
		"# Force PortInfo:LinkSpeedExtEnabled on ports\n"
		"# If 0, don't modify PortInfo:LinkSpeedExtEnabled on port\n"
		"# Otherwise, use value for PortInfo:LinkSpeedExtEnabled on port\n"
		"# Values are (MgtWG RefID #4722)\n"
		"#    1: 14.0625 Gbps\n"
		"#    2: 25.78125 Gbps\n"
		"#    3: 14.0625 Gbps or 25.78125 Gbps\n"
		"#    30: Disable extended link speeds\n"
		"#    Default 31: set to PortInfo:LinkSpeedExtSupported\n"
		"force_link_speed_ext %u\n\n"
		"# FDR10 on ports on devices that support FDR10\n"
		"# Values are:\n"
		"#    0: don't use fdr10 (no MLNX ExtendedPortInfo MADs)\n"
		"#    Default 1: enable fdr10 when supported\n"
		"#    2: disable fdr10 when supported\n"
		"fdr10 %u\n\n"
		"# The subnet_timeout code that will be set for all the ports\n"
		"# The actual timeout is 4.096usec * 2^<subnet_timeout>\n"
		"subnet_timeout %u\n\n"
		"# Threshold of local phy errors for sending Trap 129\n"
		"local_phy_errors_threshold 0x%02x\n\n"
		"# Threshold of credit overrun errors for sending Trap 130\n"
		"overrun_errors_threshold 0x%02x\n\n"
		"# Use SwitchInfo:MulticastFDBTop if advertised in PortInfo:CapabilityMask\n"
		"use_mfttop %s\n\n",
		cl_ntoh64(p_opts->guid),
		cl_ntoh64(p_opts->m_key),
		cl_ntoh16(p_opts->m_key_lease_period),
		p_opts->m_key_protect_bits,
		p_opts->m_key_lookup ? "TRUE" : "FALSE",
		cl_ntoh64(p_opts->sm_key),
		cl_ntoh64(p_opts->sa_key),
		cl_ntoh64(p_opts->subnet_prefix),
		p_opts->lmc,
		p_opts->lmc_esp0 ? "TRUE" : "FALSE",
		p_opts->sm_sl,
		p_opts->packet_life_time,
		p_opts->vl_stall_count,
		p_opts->leaf_vl_stall_count,
		p_opts->head_of_queue_lifetime,
		p_opts->leaf_head_of_queue_lifetime,
		p_opts->max_op_vls,
		p_opts->force_link_speed,
		p_opts->force_link_speed_ext,
		p_opts->fdr10,
		p_opts->subnet_timeout,
		p_opts->local_phy_errors_threshold,
		p_opts->overrun_errors_threshold,
		p_opts->use_mfttop ? "TRUE" : "FALSE");

	fprintf(out,
		"#\n# PARTITIONING OPTIONS\n#\n"
		"# Partition configuration file to be used\n"
		"partition_config_file %s\n\n"
		"# Disable partition enforcement by switches (DEPRECATED)\n"
		"# This option is DEPRECATED. Please use part_enforce instead\n"
		"no_partition_enforcement %s\n\n"
		"# Partition enforcement type (for switches)\n"
		"# Values are both, out, in and off\n"
		"# Default is both (outbound and inbound enforcement)\n"
		"part_enforce %s\n\n"
		"# Allow both full and limited membership on the same partition\n"
		"allow_both_pkeys %s\n\n"
		"# SM assigned GUID byte where GUID is formed from OpenFabrics OUI\n"
		"# followed by 40 bits xy 00 ab cd ef where xy is the SM assigned GUID byte\n"
		"# and ab cd ef is an SM autogenerated 24 bits\n"
		"# SM assigned GUID byte should be configured as subnet unique\n"
		"sm_assigned_guid 0x%02x\n\n",
		p_opts->partition_config_file,
		p_opts->no_partition_enforcement ? "TRUE" : "FALSE",
		p_opts->part_enforce,
		p_opts->allow_both_pkeys ? "TRUE" : "FALSE",
		p_opts->sm_assigned_guid);

	fprintf(out,
		"#\n# SWEEP OPTIONS\n#\n"
		"# The number of seconds between subnet sweeps (0 disables it)\n"
		"sweep_interval %u\n\n"
		"# If TRUE cause all lids to be reassigned\n"
		"reassign_lids %s\n\n"
		"# If TRUE forces every sweep to be a heavy sweep\n"
		"force_heavy_sweep %s\n\n"
		"# If TRUE every trap 128 and 144 will cause a heavy sweep.\n"
		"# NOTE: successive identical traps (>10) are suppressed\n"
		"sweep_on_trap %s\n\n",
		p_opts->sweep_interval,
		p_opts->reassign_lids ? "TRUE" : "FALSE",
		p_opts->force_heavy_sweep ? "TRUE" : "FALSE",
		p_opts->sweep_on_trap ? "TRUE" : "FALSE");

	fprintf(out,
		"#\n# ROUTING OPTIONS\n#\n"
		"# If TRUE count switches as link subscriptions\n"
		"port_profile_switch_nodes %s\n\n",
		p_opts->port_profile_switch_nodes ? "TRUE" : "FALSE");

	fprintf(out,
		"# Name of file with port guids to be ignored by port profiling\n"
		"port_prof_ignore_file %s\n\n", p_opts->port_prof_ignore_file ?
		p_opts->port_prof_ignore_file : null_str);

	fprintf(out,
		"# The file holding routing weighting factors per output port\n"
		"hop_weights_file %s\n\n",
		p_opts->hop_weights_file ? p_opts->hop_weights_file : null_str);

	fprintf(out,
		"# The file holding non-default port order per switch for routing\n"
		"port_search_ordering_file %s\n\n",
		p_opts->port_search_ordering_file ?
		p_opts->port_search_ordering_file : null_str);

	fprintf(out,
		"# Routing engine\n"
		"# Multiple routing engines can be specified separated by\n"
		"# commas so that specific ordering of routing algorithms will\n"
		"# be tried if earlier routing engines fail.\n"
		"# Supported engines: minhop, updn, dnup, file, ftree, lash,\n"
		"#    dor, torus-2QoS, dfsssp, sssp\n"
		"routing_engine %s\n\n", p_opts->routing_engine_names ?
		p_opts->routing_engine_names : null_str);

	fprintf(out,
		"# Connect roots (use FALSE if unsure)\n"
		"connect_roots %s\n\n",
		p_opts->connect_roots ? "TRUE" : "FALSE");

	fprintf(out,
		"# Use unicast routing cache (use FALSE if unsure)\n"
		"use_ucast_cache %s\n\n",
		p_opts->use_ucast_cache ? "TRUE" : "FALSE");

	fprintf(out,
		"# Lid matrix dump file name\n"
		"lid_matrix_dump_file %s\n\n", p_opts->lid_matrix_dump_file ?
		p_opts->lid_matrix_dump_file : null_str);

	fprintf(out,
		"# LFTs file name\nlfts_file %s\n\n",
		p_opts->lfts_file ? p_opts->lfts_file : null_str);

	fprintf(out,
		"# The file holding the root node guids (for fat-tree or Up/Down)\n"
		"# One guid in each line\nroot_guid_file %s\n\n",
		p_opts->root_guid_file ? p_opts->root_guid_file : null_str);

	fprintf(out,
		"# The file holding the fat-tree compute node guids\n"
		"# One guid in each line\ncn_guid_file %s\n\n",
		p_opts->cn_guid_file ? p_opts->cn_guid_file : null_str);

	fprintf(out,
		"# The file holding the fat-tree I/O node guids\n"
		"# One guid in each line.\n"
		"# If only io_guid file is provided, the rest of nodes\n"
		"# are considered as compute nodes.\n"
		"io_guid_file %s\n\n",
		p_opts->io_guid_file ? p_opts->io_guid_file : null_str);

        fprintf(out,
		"# If TRUE enables alternative indexing policy for ftree routing\n"
		"# in quasi-ftree topologies that can improve shift-pattern support.\n"
		"# The switch indexing starts from root switch and leaf switches\n"
		"# are termination points of BFS algorithm\n"
		"# If FALSE, the indexing starts from leaf switch (default)\n"
		"quasi_ftree_indexing %s\n\n",
		p_opts->quasi_ftree_indexing ? "TRUE" : "FALSE");

	fprintf(out,
		"# Number of reverse hops allowed for I/O nodes\n"
		"# Used for connectivity between I/O nodes connected to Top Switches\nmax_reverse_hops %d\n\n",
		p_opts->max_reverse_hops);

	fprintf(out,
		"# The file holding the node ids which will be used by"
		" Up/Down algorithm instead\n# of GUIDs (one guid and"
		" id in each line)\nids_guid_file %s\n\n",
		p_opts->ids_guid_file ? p_opts->ids_guid_file : null_str);

	fprintf(out,
		"# The file holding guid routing order guids (for MinHop and Up/Down)\n"
		"guid_routing_order_file %s\n\n",
		p_opts->guid_routing_order_file ? p_opts->guid_routing_order_file : null_str);

	fprintf(out,
		"# Do mesh topology analysis (for LASH algorithm)\n"
		"do_mesh_analysis %s\n\n",
		p_opts->do_mesh_analysis ? "TRUE" : "FALSE");

	fprintf(out,
		"# Starting VL for LASH algorithm\n"
		"lash_start_vl %u\n\n",
		p_opts->lash_start_vl);

	fprintf(out,
		"# Port Shifting (use FALSE if unsure)\n"
		"port_shifting %s\n\n",
		p_opts->port_shifting ? "TRUE" : "FALSE");

	fprintf(out,
		"# Assign ports in a random order instead of round-robin\n"
		"# If zero disable (default), otherwise use the value as a random seed\n"
		"scatter_ports %d\n\n",
		p_opts->scatter_ports);

	fprintf(out,
		"# Don't use scatter for ports defined in\n"
		"# guid_routing_order file\n"
		"guid_routing_order_no_scatter %s\n\n",
		p_opts->guid_routing_order_no_scatter ? "TRUE" : "FALSE");

	fprintf(out,
		"# SA database file name\nsa_db_file %s\n\n",
		p_opts->sa_db_file ? p_opts->sa_db_file : null_str);

	fprintf(out,
		"# If TRUE causes OpenSM to dump SA database at the end of\n"
		"# every light sweep, regardless of the verbosity level\n"
		"sa_db_dump %s\n\n",
		p_opts->sa_db_dump ? "TRUE" : "FALSE");

	fprintf(out,
		"# Torus-2QoS configuration file name\ntorus_config %s\n\n",
		p_opts->torus_conf_file ? p_opts->torus_conf_file : null_str);

	fprintf(out,
		"#\n# HANDOVER - MULTIPLE SMs OPTIONS\n#\n"
		"# SM priority used for deciding who is the master\n"
		"# Range goes from 0 (lowest priority) to 15 (highest).\n"
		"sm_priority %u\n\n"
		"# If TRUE other SMs on the subnet should be ignored\n"
		"ignore_other_sm %s\n\n"
		"# Timeout in [msec] between two polls of active master SM\n"
		"sminfo_polling_timeout %u\n\n"
		"# Number of failing polls of remote SM that declares it dead\n"
		"polling_retry_number %u\n\n"
		"# If TRUE honor the guid2lid file when coming out of standby\n"
		"# state, if such file exists and is valid\n"
		"honor_guid2lid_file %s\n\n",
		p_opts->sm_priority,
		p_opts->ignore_other_sm ? "TRUE" : "FALSE",
		p_opts->sminfo_polling_timeout,
		p_opts->polling_retry_number,
		p_opts->honor_guid2lid_file ? "TRUE" : "FALSE");

	fprintf(out,
		"#\n# TIMING AND THREADING OPTIONS\n#\n"
		"# Maximum number of SMPs sent in parallel\n"
		"max_wire_smps %u\n\n"
		"# Maximum number of timeout based SMPs allowed to be outstanding\n"
		"# A value less than or equal to max_wire_smps disables this mechanism\n"
		"max_wire_smps2 %u\n\n"
		"# The timeout in [usec] used for sending SMPs above max_wire_smps limit\n"
		"# and below max_wire_smps2 limit\n"
		"max_smps_timeout %u\n\n"
		"# The maximum time in [msec] allowed for a transaction to complete\n"
		"transaction_timeout %u\n\n"
		"# The maximum number of retries allowed for a transaction to complete\n"
		"transaction_retries %u\n\n"
		"# Maximal time in [msec] a message can stay in the incoming message queue.\n"
		"# If there is more than one message in the queue and the last message\n"
		"# stayed in the queue more than this value, any SA request will be\n"
		"# immediately be dropped but BUSY status is not currently returned.\n"
		"max_msg_fifo_timeout %u\n\n"
		"# Use a single thread for handling SA queries\n"
		"single_thread %s\n\n",
		p_opts->max_wire_smps,
		p_opts->max_wire_smps2,
		p_opts->max_smps_timeout,
		p_opts->transaction_timeout,
		p_opts->transaction_retries,
		p_opts->max_msg_fifo_timeout,
		p_opts->single_thread ? "TRUE" : "FALSE");

	fprintf(out,
		"#\n# MISC OPTIONS\n#\n"
		"# Daemon mode\n"
		"daemon %s\n\n"
		"# SM Inactive\n"
		"sm_inactive %s\n\n"
		"# Babbling Port Policy\n"
		"babbling_port_policy %s\n\n"
		"# Drop event subscriptions (InformInfo and ServiceRecord) on port removal and SM coming out of STANDBY\n"
		"drop_event_subscriptions %s\n\n"
		"# Validate IPoIB non-broadcast group creation parameters against\n"
		"# broadcast group parameters per IETF RFC 4391 (default TRUE)\n"
		"ipoib_mcgroup_creation_validation %s\n\n"
		"# Validate multicast join parameters against multicast group\n"
		"# parameters when MC group already exists\n"
		"mcgroup_join_validation %s\n\n"
		"# Use Optimized SLtoVLMapping programming if supported by device\n"
		"use_optimized_slvl %s\n\n"
		"# Sync in memory files used for high availability with storage\n"
		"fsync_high_avail_files %s\n\n",
		p_opts->daemon ? "TRUE" : "FALSE",
		p_opts->sm_inactive ? "TRUE" : "FALSE",
		p_opts->babbling_port_policy ? "TRUE" : "FALSE",
		p_opts->drop_event_subscriptions ? "TRUE" : "FALSE",
		p_opts->ipoib_mcgroup_creation_validation ? "TRUE" : "FALSE",
		p_opts->mcgroup_join_validation ? "TRUE" : "FALSE",
		p_opts->use_optimized_slvl ? "TRUE" : "FALSE",
		p_opts->fsync_high_avail_files ? "TRUE" : "FALSE");

#ifdef ENABLE_OSM_PERF_MGR
	fprintf(out,
		"#\n# Performance Manager Options\n#\n"
		"# perfmgr enable\n"
		"# PerfMgr is enabled if TRUE and disabled if FALSE (default FALSE)\n"
		"perfmgr %s\n\n"
		"# redirection enable\n"
		"# Redirection supported if TRUE and not supported if FALSE (default TRUE)\n"
		"perfmgr_redir %s\n\n"
		"# sweep time in seconds (default %u seconds)\n"
		"perfmgr_sweep_time_s %u\n\n"
		"# Max outstanding queries (default %u)\n"
		"perfmgr_max_outstanding_queries %u\n\n"
		"# Ignore CAs on sweep (default FALSE)\n"
		"perfmgr_ignore_cas %s\n\n"
		"# Remove missing nodes from DB (default TRUE)\n"
		"perfmgr_rm_nodes %s\n\n"
		"# Log error counters to opensm.log (default TRUE)\n"
		"perfmgr_log_errors %s\n\n"
		"# Query PerfMgt Get(ClassPortInfo) for extended capabilities\n"
		"# Extended capabilities include 64 bit extended counters\n"
		"# and transmit wait support (default TRUE)\n"
		"perfmgr_query_cpi %s\n\n"
		"# Log xmit_wait errors (default FALSE)\n"
		"perfmgr_xmit_wait_log %s\n\n"
		"# If logging xmit_wait's; set threshold (default %u)\n"
		"perfmgr_xmit_wait_threshold %u\n\n"
		,
		p_opts->perfmgr ? "TRUE" : "FALSE",
		p_opts->perfmgr_redir ? "TRUE" : "FALSE",
		OSM_PERFMGR_DEFAULT_SWEEP_TIME_S,
		p_opts->perfmgr_sweep_time_s,
		OSM_PERFMGR_DEFAULT_MAX_OUTSTANDING_QUERIES,
		p_opts->perfmgr_max_outstanding_queries,
		p_opts->perfmgr_ignore_cas ? "TRUE" : "FALSE",
		p_opts->perfmgr_rm_nodes ? "TRUE" : "FALSE",
		p_opts->perfmgr_log_errors ? "TRUE" : "FALSE",
		p_opts->perfmgr_query_cpi ? "TRUE" : "FALSE",
		p_opts->perfmgr_xmit_wait_log ? "TRUE" : "FALSE",
		OSM_PERFMGR_DEFAULT_XMIT_WAIT_THRESHOLD,
		p_opts->perfmgr_xmit_wait_threshold);

	fprintf(out,
		"#\n# Event DB Options\n#\n"
		"# Dump file to dump the events to\n"
		"event_db_dump_file %s\n\n", p_opts->event_db_dump_file ?
		p_opts->event_db_dump_file : null_str);
#endif				/* ENABLE_OSM_PERF_MGR */

	fprintf(out,
		"#\n# Event Plugin Options\n#\n"
		"# Event plugin name(s)\n"
		"event_plugin_name %s\n\n"
		"# Options string that would be passed to the plugin(s)\n"
		"event_plugin_options %s\n\n",
		p_opts->event_plugin_name ?
		p_opts->event_plugin_name : null_str,
		p_opts->event_plugin_options ?
		p_opts->event_plugin_options : null_str);

	fprintf(out,
		"#\n# Node name map for mapping node's to more descriptive node descriptions\n"
		"# (man ibnetdiscover for more information)\n#\n"
		"node_name_map_name %s\n\n", p_opts->node_name_map_name ?
		p_opts->node_name_map_name : null_str);

	fprintf(out,
		"#\n# DEBUG FEATURES\n#\n"
		"# The log flags used\n"
		"log_flags 0x%02x\n\n"
		"# Force flush of the log file after each log message\n"
		"force_log_flush %s\n\n"
		"# Log file to be used\n"
		"log_file %s\n\n"
		"# Limit the size of the log file in MB. If overrun, log is restarted\n"
		"log_max_size %u\n\n"
		"# If TRUE will accumulate the log over multiple OpenSM sessions\n"
		"accum_log_file %s\n\n"
		"# Per module logging configuration file\n"
		"# Each line in config file contains <module_name><separator><log_flags>\n"
		"# where module_name is file name including .c\n"
		"# separator is either = , space, or tab\n"
		"# log_flags is the same flags as used in the coarse/overall logging\n"
		"per_module_logging_file %s\n\n"
		"# The directory to hold the file OpenSM dumps\n"
		"dump_files_dir %s\n\n"
		"# If TRUE enables new high risk options and hardware specific quirks\n"
		"enable_quirks %s\n\n"
		"# If TRUE disables client reregistration\n"
		"no_clients_rereg %s\n\n"
		"# If TRUE OpenSM should disable multicast support and\n"
		"# no multicast routing is performed if TRUE\n"
		"disable_multicast %s\n\n"
		"# If TRUE opensm will exit on fatal initialization issues\n"
		"exit_on_fatal %s\n\n" "# console [off|local"
#ifdef ENABLE_OSM_CONSOLE_LOOPBACK
		"|loopback"
#endif
#ifdef ENABLE_OSM_CONSOLE_SOCKET
		"|socket]\n"
#else
		"]\n"
#endif
		"console %s\n\n"
		"# Telnet port for console (default %d)\n"
		"console_port %d\n\n",
		p_opts->log_flags,
		p_opts->force_log_flush ? "TRUE" : "FALSE",
		p_opts->log_file,
		p_opts->log_max_size,
		p_opts->accum_log_file ? "TRUE" : "FALSE",
		p_opts->per_module_logging_file ?
			p_opts->per_module_logging_file : null_str,
		p_opts->dump_files_dir,
		p_opts->enable_quirks ? "TRUE" : "FALSE",
		p_opts->no_clients_rereg ? "TRUE" : "FALSE",
		p_opts->disable_multicast ? "TRUE" : "FALSE",
		p_opts->exit_on_fatal ? "TRUE" : "FALSE",
		p_opts->console,
		OSM_DEFAULT_CONSOLE_PORT, p_opts->console_port);

	fprintf(out,
		"#\n# QoS OPTIONS\n#\n"
		"# Enable QoS setup\n"
		"qos %s\n\n"
		"# QoS policy file to be used\n"
		"qos_policy_file %s\n"
		"# Supress QoS MAD status errors\n"
		"suppress_sl2vl_mad_status_errors %s\n\n",
		p_opts->qos ? "TRUE" : "FALSE", p_opts->qos_policy_file,
		p_opts->suppress_sl2vl_mad_status_errors ? "TRUE" : "FALSE");

	subn_dump_qos_options(out,
			      "QoS default options", "qos",
			      &p_opts->qos_options);
	fprintf(out, "\n");
	subn_dump_qos_options(out,
			      "QoS CA options", "qos_ca",
			      &p_opts->qos_ca_options);
	fprintf(out, "\n");
	subn_dump_qos_options(out,
			      "QoS Switch Port 0 options", "qos_sw0",
			      &p_opts->qos_sw0_options);
	fprintf(out, "\n");
	subn_dump_qos_options(out,
			      "QoS Switch external ports options", "qos_swe",
			      &p_opts->qos_swe_options);
	fprintf(out, "\n");
	subn_dump_qos_options(out,
			      "QoS Router ports options", "qos_rtr",
			      &p_opts->qos_rtr_options);
	fprintf(out, "\n");

	fprintf(out,
		"#\n# Congestion Control OPTIONS (EXPERIMENTAL)\n#\n\n"
		"# Enable Congestion Control Configuration\n"
		"congestion_control %s\n\n"
		"# CCKey to use when configuring congestion control\n"
		"# note that this does not configure a new CCkey, only the CCkey to use\n"
		"cc_key 0x%016" PRIx64 "\n\n"
		"# Congestion Control Max outstanding MAD\n"
		"cc_max_outstanding_mads %u\n\n",
		p_opts->congestion_control ? "TRUE" : "FALSE",
		cl_ntoh64(p_opts->cc_key),
		p_opts->cc_max_outstanding_mads);

	fprintf(out,
		"#\n# Congestion Control SwitchCongestionSetting options\n#\n"
		"# Control Map - bitmask indicating which of the following are to be used\n"
		"# bit 0 - victim mask\n"
		"# bit 1 - credit mask\n"
		"# bit 2 - threshold + packet size\n"
		"# bit 3 - credit starvation threshold + return delay valid\n"
		"# bit 4 - marking rate valid\n"
		"cc_sw_cong_setting_control_map 0x%X\n\n",
		cl_ntoh32(p_opts->cc_sw_cong_setting_control_map));

	fprintf(out,
		"# Victim Mask - 256 bit mask representing switch ports, mark packets with FECN\n"
		"# whether they are the source or victim of congestion\n"
		"# bit 0 - port 0 (enhanced port)\n"
		"# bit 1 - port 1\n"
		"# ...\n"
		"# bit 254 - port 254\n"
		"# bit 255 - reserved\n"
		"cc_sw_cong_setting_victim_mask 0x");

	for (i = 0; i < IB_CC_PORT_MASK_DATA_SIZE; i++)
		fprintf(out, "%02X", p_opts->cc_sw_cong_setting_victim_mask[i]);
	fprintf(out, "\n\n");

	fprintf(out,
		"# Credit Mask - 256 bit mask representing switch ports to apply credit starvation\n"
		"# bit 0 - port 0 (enhanced port)\n"
		"# bit 1 - port 1\n"
		"# ...\n"
		"# bit 254 - port 254\n"
		"# bit 255 - reserved\n"
		"cc_sw_cong_setting_credit_mask 0x");

	for (i = 0; i < IB_CC_PORT_MASK_DATA_SIZE; i++)
		fprintf(out, "%02X", p_opts->cc_sw_cong_setting_credit_mask[i]);
	fprintf(out, "\n\n");

	fprintf(out,
		"# Threshold - value indicating aggressiveness of congestion marking\n"
		"# 0x0 - none, 0x1 - loose, ..., 0xF - aggressive\n"
		"cc_sw_cong_setting_threshold 0x%02X\n\n"
		"# Packet Size - any packet less than this size will not be marked with a FECN\n"
		"# units are in credits\n"
		"cc_sw_cong_setting_packet_size %u\n\n"
		"# Credit Starvation Threshold - value indicating aggressiveness of credit starvation\n"
		"# 0x0 - none, 0x1 - loose, ..., 0xF - aggressive\n"
		"cc_sw_cong_setting_credit_starvation_threshold 0x%02X\n\n"
		"# Credit Starvation Return Delay - in CCT entry shift:multiplier format, see IB spec\n"
		"cc_sw_cong_setting_credit_starvation_return_delay %u:%u\n\n"
		"# Marking Rate - mean number of packets between markings\n"
		"cc_sw_cong_setting_marking_rate %u\n\n",
		p_opts->cc_sw_cong_setting_threshold,
		p_opts->cc_sw_cong_setting_packet_size,
		p_opts->cc_sw_cong_setting_credit_starvation_threshold,
		p_opts->cc_sw_cong_setting_credit_starvation_return_delay.shift,
		p_opts->cc_sw_cong_setting_credit_starvation_return_delay.multiplier,
		cl_ntoh16(p_opts->cc_sw_cong_setting_marking_rate));

	fprintf(out,
		"#\n# Congestion Control CA Congestion Setting options\n#\n"
		"# Port Control\n"
		"# bit 0 = 0, QP based congestion control\n"
		"# bit 0 = 1, SL/port based congestion control\n"
		"cc_ca_cong_setting_port_control 0x%04X\n\n"
		"# Control Map - 16 bit bitmask indicating which SLs should be configured\n"
		"cc_ca_cong_setting_control_map 0x%04X\n\n",
		cl_ntoh16(p_opts->cc_ca_cong_setting_port_control),
		cl_ntoh16(p_opts->cc_ca_cong_setting_control_map));

	fprintf(out,
		"#\n# CA Congestion Setting Entries\n#\n"
		"# Each of congestion control settings below configures the CA Congestion\n"
		"# Settings for an individual SL.  The SL must be specified before the value.\n"
		"# These options may be specified multiple times to configure different values\n"
		"# for different SLs.\n"
		"#\n"
		"# ccti timer - when expires decrements 1 from the CCTI\n"
		"# ccti increase - number to be added to the table index on receipt of a BECN\n"
		"# trigger threshold - when the ccti is equal to this, an event is logged\n"
		"# ccti min - the minimum value for the ccti.  This imposes a minimum rate\n"
		"#            on the injection rate\n\n");

	for (i = 0; i < IB_CA_CONG_ENTRY_DATA_SIZE; i++) {
		/* Don't output unless one of the settings has been set, there's no need
		 * to output 16 chunks of this with all defaults of 0 */
		if (p_opts->cc_ca_cong_entries[i].ccti_timer
		    || p_opts->cc_ca_cong_entries[i].ccti_increase
		    || p_opts->cc_ca_cong_entries[i].trigger_threshold
		    || p_opts->cc_ca_cong_entries[i].ccti_min) {
			fprintf(out,
				"# SL = %u\n"
				"cc_ca_cong_setting_ccti_timer %u %u\n"
				"cc_ca_cong_setting_ccti_increase %u %u\n"
				"cc_ca_cong_setting_trigger_threshold %u %u\n"
				"cc_ca_cong_setting_ccti_min %u %u\n\n",
				i,
				i,
				cl_ntoh16(p_opts->cc_ca_cong_entries[i].ccti_timer),
				i,
				p_opts->cc_ca_cong_entries[i].ccti_increase,
				i,
				p_opts->cc_ca_cong_entries[i].trigger_threshold,
				i,
				p_opts->cc_ca_cong_entries[i].ccti_min);
			cacongoutputcount++;
		}
	}

	/* If by chance all the CA Cong Settings are default, output at least 1 chunk
         * for illustration */
	if (!cacongoutputcount)
		fprintf(out,
			"# SL = 0\n"
			"cc_ca_cong_setting_ccti_timer 0 %u\n"
			"cc_ca_cong_setting_ccti_increase 0 %u\n"
			"cc_ca_cong_setting_trigger_threshold 0 %u\n"
			"cc_ca_cong_setting_ccti_min 0 %u\n\n",
			cl_ntoh16(p_opts->cc_ca_cong_entries[0].ccti_timer),
			p_opts->cc_ca_cong_entries[0].ccti_increase,
			p_opts->cc_ca_cong_entries[0].trigger_threshold,
			p_opts->cc_ca_cong_entries[0].ccti_min);

	fprintf(out,
		"#\n# Congestion Control Table\n#\n"
		"# Comma separated list of CCT entries representing CCT.\n"
		"# Format is shift:multipler,shift_multiplier,shift:multiplier,...\n"
		"cc_cct ");

	if (!p_opts->cc_cct.entries_len) {
		fprintf(out, "%s\n", null_str);
	}
	else {
		fprintf(out, "%u:%u",
			p_opts->cc_cct.entries[0].shift,
			p_opts->cc_cct.entries[0].multiplier);
		for (i = 1; i < p_opts->cc_cct.entries_len; i++) {
			fprintf(out, ",%u:%u",
				p_opts->cc_cct.entries[i].shift,
				p_opts->cc_cct.entries[i].multiplier);
		}
		fprintf(out, "\n");
	}
	fprintf(out, "\n");

	fprintf(out,
		"# Prefix routes file name\n"
		"prefix_routes_file %s\n\n",
		p_opts->prefix_routes_file);

	fprintf(out,
		"#\n# IPv6 Solicited Node Multicast (SNM) Options\n#\n"
		"consolidate_ipv6_snm_req %s\n\n",
		p_opts->consolidate_ipv6_snm_req ? "TRUE" : "FALSE");

	fprintf(out, "# Log prefix\nlog_prefix %s\n\n", p_opts->log_prefix);

	/* optional string attributes ... */

}

int osm_subn_write_conf_file(char *file_name, IN osm_subn_opt_t * p_opts)
{
	FILE *opts_file;

	opts_file = fopen(file_name, "w");
	if (!opts_file) {
		printf("cannot open file \'%s\' for writing: %s\n",
			file_name, strerror(errno));
		return -1;
	}

	osm_subn_output_conf(opts_file, p_opts);

	fclose(opts_file);

	return 0;
}
