/*
 * Copyright (c) 2010 Lawrence Livermore National Lab.  All rights reserved.
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

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <inttypes.h>

#include <infiniband/mad.h>
#include <infiniband/ibnetdisc.h>

#include "ibdiag_common.h"

uint64_t switchguid_before = 0;
uint64_t switchguid_after = 0;
int switchguid_flag = 0;

uint64_t caguid_before = 0;
uint64_t caguid_after = 0;
int caguid_flag = 0;

uint64_t sysimgguid_before = 0;
uint64_t sysimgguid_after = 0;
int sysimgguid_flag = 0;

uint64_t portguid_nodeguid = 0;
uint64_t portguid_before = 0;
uint64_t portguid_after = 0;
int portguid_flag = 0;

struct guids {
	uint64_t searchguid;
	int searchguid_found;
	uint64_t before;
	uint64_t after;
	int found;
};

static int parse_beforeafter(char *arg, uint64_t *before, uint64_t *after)
{
	char *ptr;
	char *before_str;
	char *after_str;

	ptr = strchr(optarg, ':');
	if (!ptr || !(*(ptr + 1))) {
		fprintf(stderr, "invalid input '%s'\n", arg);
		return -1;
	}
	(*ptr) = '\0';
	before_str = arg;
	after_str = ptr + 1;

	(*before) = strtoull(before_str, 0, 0);
	(*after) = strtoull(after_str, 0, 0);
	return 0;
}

static int parse_guidbeforeafter(char *arg,
				 uint64_t *guid,
				 uint64_t *before,
				 uint64_t *after)
{
	char *ptr1;
	char *ptr2;
	char *guid_str;
	char *before_str;
	char *after_str;

	ptr1 = strchr(optarg, ':');
	if (!ptr1 || !(*(ptr1 + 1))) {
		fprintf(stderr, "invalid input '%s'\n", arg);
		return -1;
	}
	guid_str = arg;
	before_str = ptr1 + 1;

	ptr2 = strchr(before_str, ':');
	if (!ptr2 || !(*(ptr2 + 1))) {
		fprintf(stderr, "invalid input '%s'\n", arg);
		return -1;
	}
	(*ptr1) = '\0';
	(*ptr2) = '\0';
	after_str = ptr2 + 1;

	(*guid) = strtoull(guid_str, 0, 0);
	(*before) = strtoull(before_str, 0, 0);
	(*after) = strtoull(after_str, 0, 0);
	return 0;
}

static int process_opt(void *context, int ch, char *optarg)
{
	switch (ch) {
	case 1:
		if (parse_beforeafter(optarg,
				      &switchguid_before,
				      &switchguid_after) < 0)
			return -1;
		switchguid_flag++;
		break;
	case 2:
		if (parse_beforeafter(optarg,
				      &caguid_before,
				      &caguid_after) < 0)
			return -1;
		caguid_flag++;
		break;
	case 3:
		if (parse_beforeafter(optarg,
				      &sysimgguid_before,
				      &sysimgguid_after) < 0)
			return -1;
		sysimgguid_flag++;
		break;
	case 4:
		if (parse_guidbeforeafter(optarg,
					  &portguid_nodeguid,
					  &portguid_before,
					  &portguid_after) < 0)
			return -1;
		portguid_flag++;
		break;
	default:
		return -1;
	}

	return 0;
}

static void update_switchportguids(ibnd_node_t *node)
{
	ibnd_port_t *port;
	int p;

	for (p = 0; p <= node->numports; p++) {
		port = node->ports[p];
		if (port)
			port->guid = node->guid;
	}
}

static void replace_node_guid(ibnd_node_t *node, void *user_data)
{
	struct guids *guids;

	guids = (struct guids *)user_data;

	if (node->guid == guids->before) {

		node->guid = guids->after;

		/* port guids are identical to switch guids on
		 * switches, so update port guids too
		 */
		if (node->type == IB_NODE_SWITCH)
			update_switchportguids(node);

		guids->found++;
	}
}

static void replace_sysimgguid(ibnd_node_t *node, void *user_data)
{
	struct guids *guids;
	uint64_t sysimgguid;

	guids = (struct guids *)user_data;

	sysimgguid = mad_get_field64(node->info, 0, IB_NODE_SYSTEM_GUID_F);
	if (sysimgguid == guids->before) {
		mad_set_field64(node->info, 0, IB_NODE_SYSTEM_GUID_F,
				guids->after);
		guids->found++;
	}
}

static void replace_portguid(ibnd_node_t *node, void *user_data)
{
	struct guids *guids;

	guids = (struct guids *)user_data;

	if (node->guid != guids->searchguid)
		return;

	guids->searchguid_found++;

	if (node->type == IB_NODE_SWITCH) {
		/* port guids are identical to switch guids on
		 * switches, so update switch guid too
		 */
		if (node->guid == guids->before) {
			node->guid = guids->after;
			update_switchportguids(node);
			guids->found++;
		}
	}
	else {
		ibnd_port_t *port;
		int p;

		for (p = 1; p <= node->numports; p++) {
			port = node->ports[p];
			if (port
			    && port->guid == guids->before) {
				port->guid = guids->after;
				guids->found++;
				break;
			}
		}
	}
}

int main(int argc, char **argv)
{
	ibnd_fabric_t *fabric = NULL;
	char *orig_cache_file = NULL;
	char *new_cache_file = NULL;
	struct guids guids;

	const struct ibdiag_opt opts[] = {
		{"switchguid", 1, 1, "BEFOREGUID:AFTERGUID",
		 "Specify before and after switchguid to edit"},
		{"caguid", 2, 1, "BEFOREGUID:AFTERGUID",
		 "Specify before and after caguid to edit"},
		{"sysimgguid", 3, 1, "BEFOREGUID:AFTERGUID",
		 "Specify before and after sysimgguid to edit"},
		{"portguid", 4, 1, "NODEGUID:BEFOREGUID:AFTERGUID",
		 "Specify before and after port guid to edit"},
		{0}
	};
	char *usage_args = "<orig.cache> <new.cache>";

	ibdiag_process_opts(argc, argv, NULL, "CDdeGKLPstvy",
			    opts, process_opt, usage_args,
			    NULL);

	argc -= optind;
	argv += optind;

	orig_cache_file = argv[0];
	new_cache_file = argv[1];

	if (!orig_cache_file)
		IBEXIT("original cache file not specified");

	if (!new_cache_file)
		IBEXIT("new cache file not specified");

	if ((fabric = ibnd_load_fabric(orig_cache_file, 0)) == NULL)
		IBEXIT("loading original cached fabric failed");

	if (switchguid_flag) {
		guids.before = switchguid_before;
		guids.after = switchguid_after;
		guids.found = 0;
		ibnd_iter_nodes_type(fabric,
				     replace_node_guid,
				     IB_NODE_SWITCH,
				     &guids);

		if (!guids.found)
			IBEXIT("switchguid = %" PRIx64 " not found",
				switchguid_before);
	}

	if (caguid_flag) {
		guids.before = caguid_before;
		guids.after = caguid_after;
		guids.found = 0;
		ibnd_iter_nodes_type(fabric,
				     replace_node_guid,
				     IB_NODE_CA,
				     &guids);

		if (!guids.found)
			IBEXIT("caguid = %" PRIx64 " not found",
				caguid_before);
	}

	if (sysimgguid_flag) {
		guids.before = sysimgguid_before;
		guids.after = sysimgguid_after;
		guids.found = 0;
		ibnd_iter_nodes(fabric,
				replace_sysimgguid,
				&guids);

		if (!guids.found)
			IBEXIT("sysimgguid = %" PRIx64 " not found",
				sysimgguid_before);
	}

	if (portguid_flag) {
		guids.searchguid = portguid_nodeguid;
		guids.searchguid_found = 0;
		guids.before = portguid_before;
		guids.after = portguid_after;
		guids.found = 0;
		ibnd_iter_nodes(fabric,
				replace_portguid,
				&guids);

		if (!guids.searchguid_found)
			IBEXIT("nodeguid = %" PRIx64 " not found",
				portguid_nodeguid);

		if (!guids.found)
			IBEXIT("portguid = %" PRIx64 " not found",
				portguid_before);
	}

	if (ibnd_cache_fabric(fabric, new_cache_file, 0) < 0)
		IBEXIT("caching new cache data failed");

	ibnd_destroy_fabric(fabric);
	exit(0);
}
