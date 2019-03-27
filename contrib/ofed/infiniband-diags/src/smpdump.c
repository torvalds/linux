/*
 * Copyright (c) 2004-2009 Voltaire Inc.  All rights reserved.
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

#define _GNU_SOURCE

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <inttypes.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <netinet/in.h>

#include <infiniband/mad.h>
#include <infiniband/umad.h>

#include <ibdiag_common.h>

static int mad_agent;
static int drmad_tid = 0x123;

typedef struct {
	char path[64];
	int hop_cnt;
} DRPath;

struct drsmp {
	uint8_t base_version;
	uint8_t mgmt_class;
	uint8_t class_version;
	uint8_t method;
	uint16_t status;
	uint8_t hop_ptr;
	uint8_t hop_cnt;
	uint64_t tid;
	uint16_t attr_id;
	uint16_t resv;
	uint32_t attr_mod;
	uint64_t mkey;
	uint16_t dr_slid;
	uint16_t dr_dlid;
	uint8_t reserved[28];
	uint8_t data[64];
	uint8_t initial_path[64];
	uint8_t return_path[64];
};

void drsmp_get_init(void *umad, DRPath * path, int attr, int mod)
{
	struct drsmp *smp = (struct drsmp *)(umad_get_mad(umad));

	memset(smp, 0, sizeof(*smp));

	smp->base_version = 1;
	smp->mgmt_class = IB_SMI_DIRECT_CLASS;
	smp->class_version = 1;

	smp->method = 1;
	smp->attr_id = (uint16_t) htons((uint16_t) attr);
	smp->attr_mod = htonl(mod);
	smp->tid = htonll(drmad_tid++);
	smp->dr_slid = 0xffff;
	smp->dr_dlid = 0xffff;

	umad_set_addr(umad, 0xffff, 0, 0, 0);

	if (path)
		memcpy(smp->initial_path, path->path, path->hop_cnt + 1);

	smp->hop_cnt = (uint8_t) path->hop_cnt;
}

void smp_get_init(void *umad, int lid, int attr, int mod)
{
	struct drsmp *smp = (struct drsmp *)(umad_get_mad(umad));

	memset(smp, 0, sizeof(*smp));

	smp->base_version = 1;
	smp->mgmt_class = IB_SMI_CLASS;
	smp->class_version = 1;

	smp->method = 1;
	smp->attr_id = (uint16_t) htons((uint16_t) attr);
	smp->attr_mod = htonl(mod);
	smp->tid = htonll(drmad_tid++);

	umad_set_addr(umad, lid, 0, 0, 0);
}

void drsmp_set_init(void *umad, DRPath * path, int attr, int mod, void *data)
{
	struct drsmp *smp = (struct drsmp *)(umad_get_mad(umad));

	memset(smp, 0, sizeof(*smp));

	smp->method = 2;	/* SET */
	smp->attr_id = (uint16_t) htons((uint16_t) attr);
	smp->attr_mod = htonl(mod);
	smp->tid = htonll(drmad_tid++);
	smp->dr_slid = 0xffff;
	smp->dr_dlid = 0xffff;

	umad_set_addr(umad, 0xffff, 0, 0, 0);

	if (path)
		memcpy(smp->initial_path, path->path, path->hop_cnt + 1);

	if (data)
		memcpy(smp->data, data, sizeof smp->data);

	smp->hop_cnt = (uint8_t) path->hop_cnt;
}

char *drmad_status_str(struct drsmp *drsmp)
{
	switch (drsmp->status) {
	case 0:
		return "success";
	case ETIMEDOUT:
		return "timeout";
	}
	return "unknown error";
}

int str2DRPath(char *str, DRPath * path)
{
	char *s;

	path->hop_cnt = -1;

	DEBUG("DR str: %s", str);
	while (str && *str) {
		if ((s = strchr(str, ',')))
			*s = 0;
		path->path[++path->hop_cnt] = (char)atoi(str);
		if (!s)
			break;
		str = s + 1;
	}

#if 0
	if (path->path[0] != 0 ||
	    (path->hop_cnt > 0 && dev_port && path->path[1] != dev_port)) {
		DEBUG("hop 0 != 0 or hop 1 != dev_port");
		return -1;
	}
#endif

	return path->hop_cnt;
}

static int dump_char, mgmt_class = IB_SMI_CLASS;

static int process_opt(void *context, int ch, char *optarg)
{
	switch (ch) {
	case 's':
		dump_char++;
		break;
	case 'D':
		mgmt_class = IB_SMI_DIRECT_CLASS;
		break;
	case 'L':
		mgmt_class = IB_SMI_CLASS;
		break;
	default:
		return -1;
	}
	return 0;
}

#ifndef strdupa
#define	strdupa(_s)						\
({								\
	char *_d;						\
	int _len;						\
								\
	_len = strlen(_s) + 1;					\
	_d = alloca(_len);					\
	if (_d)							\
		memcpy(_d, _s, _len);				\
	_d;							\
})
#endif

int main(int argc, char *argv[])
{
	int dlid = 0;
	void *umad;
	struct drsmp *smp;
	int i, portid, mod = 0, attr;
	DRPath path;
	uint8_t *desc;
	int length;

	const struct ibdiag_opt opts[] = {
		{"string", 's', 0, NULL, ""},
		{0}
	};
	char usage_args[] = "<dlid|dr_path> <attr> [mod]";
	const char *usage_examples[] = {
		" -- DR routed examples:",
		"-D 0,1,2,3,5 16	# NODE DESC",
		"-D 0,1,2 0x15 2	# PORT INFO, port 2",
		" -- LID routed examples:",
		"3 0x15 2	# PORT INFO, lid 3 port 2",
		"0xa0 0x11	# NODE INFO, lid 0xa0",
		NULL
	};

	ibd_timeout = 1000;

	ibdiag_process_opts(argc, argv, NULL, "GKs", opts, process_opt,
			    usage_args, usage_examples);

	argc -= optind;
	argv += optind;

	if (argc < 2)
		ibdiag_show_usage();

	if (mgmt_class == IB_SMI_DIRECT_CLASS &&
	    str2DRPath(strdupa(argv[0]), &path) < 0)
		IBPANIC("bad path str '%s'", argv[0]);

	if (mgmt_class == IB_SMI_CLASS)
		dlid = strtoul(argv[0], 0, 0);

	attr = strtoul(argv[1], 0, 0);
	if (argc > 2)
		mod = strtoul(argv[2], 0, 0);

	if (umad_init() < 0)
		IBPANIC("can't init UMAD library");

	if ((portid = umad_open_port(ibd_ca, ibd_ca_port)) < 0)
		IBPANIC("can't open UMAD port (%s:%d)", ibd_ca, ibd_ca_port);

	if ((mad_agent = umad_register(portid, mgmt_class, 1, 0, 0)) < 0)
		IBPANIC("Couldn't register agent for SMPs");

	if (!(umad = umad_alloc(1, umad_size() + IB_MAD_SIZE)))
		IBPANIC("can't alloc MAD");

	smp = umad_get_mad(umad);

	if (mgmt_class == IB_SMI_DIRECT_CLASS)
		drsmp_get_init(umad, &path, attr, mod);
	else
		smp_get_init(umad, dlid, attr, mod);

	if (ibdebug > 1)
		xdump(stderr, "before send:\n", smp, 256);

	length = IB_MAD_SIZE;
	if (umad_send(portid, mad_agent, umad, length, ibd_timeout, 0) < 0)
		IBPANIC("send failed");

	if (umad_recv(portid, umad, &length, -1) != mad_agent)
		IBPANIC("recv error: %s", drmad_status_str(smp));

	if (!dump_char) {
		xdump(stdout, 0, smp->data, 64);
		if (smp->status)
			fprintf(stdout, "SMP status: 0x%x\n",
				ntohs(smp->status));
		goto exit;
	}

	desc = smp->data;
	for (i = 0; i < 64; ++i) {
		if (!desc[i])
			break;
		putchar(desc[i]);
	}
	putchar('\n');
	if (smp->status)
		fprintf(stdout, "SMP status: 0x%x\n", ntohs(smp->status));

exit:
	umad_free(umad);
	return 0;
}
