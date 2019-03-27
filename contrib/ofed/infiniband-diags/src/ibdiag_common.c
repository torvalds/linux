/*
 * Copyright (c) 2006-2007 The Regents of the University of California.
 * Copyright (c) 2004-2009 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2010 Mellanox Technologies LTD. All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved.
 * Copyright (c) 2009 HNR Consulting. All rights reserved.
 * Copyright (c) 2011 Lawrence Livermore National Security. All rights reserved.
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

/**
 * Define common functions which can be included in the various C based diags.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sys/types.h>
#include <unistd.h>
#include <ctype.h>
#include <config.h>
#include <getopt.h>
#include <limits.h>
#include <sys/stat.h>
#include <stdarg.h>

#include <infiniband/umad.h>
#include <infiniband/mad.h>
#include <ibdiag_common.h>
#include <ibdiag_version.h>

int ibverbose;
enum MAD_DEST ibd_dest_type = IB_DEST_LID;
ib_portid_t *ibd_sm_id;
static ib_portid_t sm_portid = { 0 };

/* general config options */
#define IBDIAG_CONFIG_GENERAL IBDIAG_CONFIG_PATH"/ibdiag.conf"
char *ibd_ca = NULL;
int ibd_ca_port = 0;
int ibd_timeout = 0;
uint32_t ibd_ibnetdisc_flags = IBND_CONFIG_MLX_EPI;
uint64_t ibd_mkey;
uint64_t ibd_sakey = 0;
int show_keys = 0;
char *ibd_nd_format = NULL;

static const char *prog_name;
static const char *prog_args;
static const char **prog_examples;
static struct option *long_opts = NULL;
static const struct ibdiag_opt *opts_map[256];

static const char *get_build_version(void)
{
	return "BUILD VERSION: " IBDIAG_VERSION;
}

static void pretty_print(int start, int width, const char *str)
{
	int len = width - start;
	const char *p, *e;

	while (1) {
		while (isspace(*str))
			str++;
		p = str;
		do {
			e = p + 1;
			p = strchr(e, ' ');
		} while (p && p - str < len);
		if (!p) {
			fprintf(stderr, "%s", str);
			break;
		}
		if (e - str == 1)
			e = p;
		fprintf(stderr, "%.*s\n%*s", (int)(e - str), str, start, "");
		str = e;
	}
}

static inline int val_str_true(const char *val_str)
{
	return ((strncmp(val_str, "TRUE", strlen("TRUE")) == 0) ||
		(strncmp(val_str, "true", strlen("true")) == 0));
}

void read_ibdiag_config(const char *file)
{
	char buf[1024];
	FILE *config_fd = NULL;
	char *p_prefix, *p_last;
	char *name;
	char *val_str;
	struct stat statbuf;

	/* silently ignore missing config file */
	if (stat(file, &statbuf))
		return;

	config_fd = fopen(file, "r");
	if (!config_fd)
		return;

	while (fgets(buf, sizeof buf, config_fd) != NULL) {
		p_prefix = strtok_r(buf, "\n", &p_last);
		if (!p_prefix)
			continue; /* ignore blank lines */

		if (*p_prefix == '#')
			continue; /* ignore comment lines */

		name = strtok_r(p_prefix, "=", &p_last);
		val_str = strtok_r(NULL, "\n", &p_last);

		if (strncmp(name, "CA", strlen("CA")) == 0) {
			free(ibd_ca);
			ibd_ca = strdup(val_str);
		} else if (strncmp(name, "Port", strlen("Port")) == 0) {
			ibd_ca_port = strtoul(val_str, NULL, 0);
		} else if (strncmp(name, "timeout", strlen("timeout")) == 0) {
			ibd_timeout = strtoul(val_str, NULL, 0);
		} else if (strncmp(name, "MLX_EPI", strlen("MLX_EPI")) == 0) {
			if (val_str_true(val_str)) {
				ibd_ibnetdisc_flags |= IBND_CONFIG_MLX_EPI;
			} else {
				ibd_ibnetdisc_flags &= ~IBND_CONFIG_MLX_EPI;
			}
		} else if (strncmp(name, "m_key", strlen("m_key")) == 0) {
			ibd_mkey = strtoull(val_str, 0, 0);
		} else if (strncmp(name, "sa_key",
				   strlen("sa_key")) == 0) {
			ibd_sakey = strtoull(val_str, 0, 0);
		} else if (strncmp(name, "nd_format",
				   strlen("nd_format")) == 0) {
			ibd_nd_format = strdup(val_str);
		}
	}

	fclose(config_fd);
}


void ibdiag_show_usage()
{
	struct option *o = long_opts;
	int n;

	fprintf(stderr, "\nUsage: %s [options] %s\n\n", prog_name,
		prog_args ? prog_args : "");

	if (long_opts[0].name)
		fprintf(stderr, "Options:\n");
	for (o = long_opts; o->name; o++) {
		const struct ibdiag_opt *io = opts_map[o->val];
		n = fprintf(stderr, "  --%s", io->name);
		if (isprint(io->letter))
			n += fprintf(stderr, ", -%c", io->letter);
		if (io->has_arg)
			n += fprintf(stderr, " %s",
				     io->arg_tmpl ? io->arg_tmpl : "<val>");
		if (io->description && *io->description) {
			n += fprintf(stderr, "%*s  ", 24 - n > 0 ? 24 - n : 0,
				     "");
			pretty_print(n, 74, io->description);
		}
		fprintf(stderr, "\n");
	}

	if (prog_examples) {
		const char **p;
		fprintf(stderr, "\nExamples:\n");
		for (p = prog_examples; *p && **p; p++)
			fprintf(stderr, "  %s %s\n", prog_name, *p);
	}

	fprintf(stderr, "\n");

	exit(2);
}

static int process_opt(int ch, char *optarg)
{
	char *endp;
	long val;

	switch (ch) {
	case 'z':
		read_ibdiag_config(optarg);
		break;
	case 'h':
		ibdiag_show_usage();
		break;
	case 'V':
		fprintf(stderr, "%s %s\n", prog_name, get_build_version());
		exit(0);
	case 'e':
		madrpc_show_errors(1);
		break;
	case 'v':
		ibverbose++;
		break;
	case 'd':
		ibdebug++;
		madrpc_show_errors(1);
		umad_debug(ibdebug - 1);
		break;
	case 'C':
		ibd_ca = optarg;
		break;
	case 'P':
		ibd_ca_port = strtoul(optarg, 0, 0);
		if (ibd_ca_port < 0)
			IBEXIT("cannot resolve CA port %d", ibd_ca_port);
		break;
	case 'D':
		ibd_dest_type = IB_DEST_DRPATH;
		break;
	case 'L':
		ibd_dest_type = IB_DEST_LID;
		break;
	case 'G':
		ibd_dest_type = IB_DEST_GUID;
		break;
	case 't':
		errno = 0;
		val = strtol(optarg, &endp, 0);
		if (errno || (endp && *endp != '\0') || val <= 0 ||
		    val > INT_MAX)
			IBEXIT("Invalid timeout \"%s\".  Timeout requires a "
				"positive integer value < %d.", optarg, INT_MAX);
		else {
			madrpc_set_timeout((int)val);
			ibd_timeout = (int)val;
		}
		break;
	case 's':
		/* srcport is not required when resolving via IB_DEST_LID */
		if (resolve_portid_str(ibd_ca, ibd_ca_port, &sm_portid, optarg,
				IB_DEST_LID, 0, NULL) < 0)
			IBEXIT("cannot resolve SM destination port %s",
				optarg);
		ibd_sm_id = &sm_portid;
		break;
	case 'K':
		show_keys = 1;
		break;
	case 'y':
		errno = 0;
		ibd_mkey = strtoull(optarg, &endp, 0);
		if (errno || *endp != '\0') {
			errno = 0;
			ibd_mkey = strtoull(getpass("M_Key: "), &endp, 0);
			if (errno || *endp != '\0') {
				IBEXIT("Bad M_Key");
			}
                }
                break;
	default:
		return -1;
	}

	return 0;
}

static const struct ibdiag_opt common_opts[] = {
	{"config", 'z', 1, "<config>", "use config file, default: " IBDIAG_CONFIG_GENERAL},
	{"Ca", 'C', 1, "<ca>", "Ca name to use"},
	{"Port", 'P', 1, "<port>", "Ca port number to use"},
	{"Direct", 'D', 0, NULL, "use Direct address argument"},
	{"Lid", 'L', 0, NULL, "use LID address argument"},
	{"Guid", 'G', 0, NULL, "use GUID address argument"},
	{"timeout", 't', 1, "<ms>", "timeout in ms"},
	{"sm_port", 's', 1, "<lid>", "SM port lid"},
	{"show_keys", 'K', 0, NULL, "display security keys in output"},
	{"m_key", 'y', 1, "<key>", "M_Key to use in request"},
	{"errors", 'e', 0, NULL, "show send and receive errors"},
	{"verbose", 'v', 0, NULL, "increase verbosity level"},
	{"debug", 'd', 0, NULL, "raise debug level"},
	{"help", 'h', 0, NULL, "help message"},
	{"version", 'V', 0, NULL, "show version"},
	{0}
};

static void make_opt(struct option *l, const struct ibdiag_opt *o,
		     const struct ibdiag_opt *map[])
{
	l->name = o->name;
	l->has_arg = o->has_arg;
	l->flag = NULL;
	l->val = o->letter;
	if (!map[l->val])
		map[l->val] = o;
}

static struct option *make_long_opts(const char *exclude_str,
				     const struct ibdiag_opt *custom_opts,
				     const struct ibdiag_opt *map[])
{
	struct option *long_opts, *l;
	const struct ibdiag_opt *o;
	unsigned n = 0;

	if (custom_opts)
		for (o = custom_opts; o->name; o++)
			n++;

	long_opts = malloc((sizeof(common_opts) / sizeof(common_opts[0]) + n) *
			   sizeof(*long_opts));
	if (!long_opts)
		return NULL;

	l = long_opts;

	if (custom_opts)
		for (o = custom_opts; o->name; o++)
			make_opt(l++, o, map);

	for (o = common_opts; o->name; o++) {
		if (exclude_str && strchr(exclude_str, o->letter))
			continue;
		make_opt(l++, o, map);
	}

	memset(l, 0, sizeof(*l));

	return long_opts;
}

static void make_str_opts(const struct option *o, char *p, unsigned size)
{
	unsigned i, n = 0;

	for (n = 0; o->name && n + 2 + o->has_arg < size; o++) {
		p[n++] = (char)o->val;
		for (i = 0; i < (unsigned)o->has_arg; i++)
			p[n++] = ':';
	}
	p[n] = '\0';
}

int ibdiag_process_opts(int argc, char *const argv[], void *cxt,
			const char *exclude_common_str,
			const struct ibdiag_opt custom_opts[],
			int (*custom_handler) (void *cxt, int val,
					       char *optarg),
			const char *usage_args, const char *usage_examples[])
{
	char str_opts[1024];
	const struct ibdiag_opt *o;

	prog_name = argv[0];
	prog_args = usage_args;
	prog_examples = usage_examples;

	if (long_opts)
		free(long_opts);

	long_opts = make_long_opts(exclude_common_str, custom_opts, opts_map);
	if (!long_opts)
		return -1;

	read_ibdiag_config(IBDIAG_CONFIG_GENERAL);

	make_str_opts(long_opts, str_opts, sizeof(str_opts));

	while (1) {
		int ch = getopt_long(argc, argv, str_opts, long_opts, NULL);
		if (ch == -1)
			break;
		o = opts_map[ch];
		if (!o)
			ibdiag_show_usage();
		if (custom_handler) {
			if (custom_handler(cxt, ch, optarg) &&
			    process_opt(ch, optarg))
				ibdiag_show_usage();
		} else if (process_opt(ch, optarg))
			ibdiag_show_usage();
	}

	return 0;
}

void ibexit(const char *fn, char *msg, ...)
{
	char buf[512];
	va_list va;
	int n;

	va_start(va, msg);
	n = vsprintf(buf, msg, va);
	va_end(va);
	buf[n] = 0;

	if (ibdebug)
		printf("%s: iberror: [pid %d] %s: failed: %s\n",
		       prog_name ? prog_name : "", getpid(), fn, buf);
	else
		printf("%s: iberror: failed: %s\n",
		       prog_name ? prog_name : "", buf);

	exit(-1);
}

char *
conv_cnt_human_readable(uint64_t val64, float *val, int data)
{
	uint64_t tmp = val64;
	int ui = 0;
	int div = 1;

	tmp /= 1024;
	while (tmp) {
		ui++;
		tmp /= 1024;
		div *= 1024;
	}

	*val = (float)(val64);
	if (data) {
		*val *= 4;
		if (*val/div > 1024) {
			ui++;
			div *= 1024;
		}
	}
	*val /= div;

	if (data) {
		switch (ui) {
			case 0:
				return ("B");
			case 1:
				return ("KB");
			case 2:
				return ("MB");
			case 3:
				return ("GB");
			case 4:
				return ("TB");
			case 5:
				return ("PB");
			case 6:
				return ("EB");
			default:
				return ("");
		}
	} else {
		switch (ui) {
			case 0:
				return ("");
			case 1:
				return ("K");
			case 2:
				return ("M");
			case 3:
				return ("G");
			case 4:
				return ("T");
			case 5:
				return ("P");
			case 6:
				return ("E");
			default:
				return ("");
		}
	}
	return ("");
}

int is_port_info_extended_supported(ib_portid_t * dest, int port,
				    struct ibmad_port *srcport)
{
	uint8_t data[IB_SMP_DATA_SIZE] = { 0 };
	uint32_t cap_mask;
	uint16_t cap_mask2;

	if (!smp_query_via(data, dest, IB_ATTR_PORT_INFO, port, 0, srcport))
		IBEXIT("port info query failed");

	mad_decode_field(data, IB_PORT_CAPMASK_F, &cap_mask);
	if (cap_mask & CL_NTOH32(IB_PORT_CAP_HAS_CAP_MASK2)) {
		mad_decode_field(data, IB_PORT_CAPMASK2_F, &cap_mask2);
		if (!(cap_mask2 &
		      CL_NTOH16(IB_PORT_CAP2_IS_PORT_INFO_EXT_SUPPORTED))) {
			IBWARN("port info capability mask2 = 0x%x doesn't"
			       " indicate PortInfoExtended support", cap_mask2);
			return 0;
		}
	} else {
		IBWARN("port info capability mask2 not supported");
		return 0;
	}

	return 1;
}

int is_mlnx_ext_port_info_supported(uint32_t vendorid,
				    uint16_t devid)
{
	if (ibd_ibnetdisc_flags & IBND_CONFIG_MLX_EPI) {

		if ((devid >= 0xc738 && devid <= 0xc73b) || devid == 0xcb20 || devid == 0xcf08 ||
		    ((vendorid == 0x119f) &&
		     /* Bull SwitchX */
		     (devid == 0x1b02 || devid == 0x1b50 ||
		      /* Bull SwitchIB and SwitchIB2 */
		      devid == 0x1ba0 ||
		      (devid >= 0x1bd0 && devid <= 0x1bd5))))
			return 1;
		if ((devid >= 0x1003 && devid <= 0x1017) ||
		    ((vendorid == 0x119f) &&
		     /* Bull ConnectX3 */
		     (devid == 0x1b33 || devid == 0x1b73 ||
		      devid == 0x1b40 || devid == 0x1b41 ||
		      devid == 0x1b60 || devid == 0x1b61 ||
		      /* Bull ConnectIB */
		      devid == 0x1b83 ||
		      devid == 0x1b93 || devid == 0x1b94 ||
		      /* Bull ConnectX4 */
		      devid == 0x1bb4 || devid == 0x1bb5 ||
		      devid == 0x1bc4)))
			return 1;
	}

	return 0;
}

/** =========================================================================
 * Resolve the SM portid using the umad layer rather than using
 * ib_resolve_smlid_via which requires a PortInfo query on the local port.
 */
int resolve_sm_portid(char *ca_name, uint8_t portnum, ib_portid_t *sm_id)
{
	umad_port_t port;
	int rc;

	if (!sm_id)
		return (-1);

	if ((rc = umad_get_port(ca_name, portnum, &port)) < 0)
		return rc;

	memset(sm_id, 0, sizeof(*sm_id));
	sm_id->lid = port.sm_lid;
	sm_id->sl = port.sm_sl;

	umad_release_port(&port);

	return 0;
}

/** =========================================================================
 * Resolve local CA characteristics using the umad layer rather than using
 * ib_resolve_self_via which requires SMP queries on the local port.
 */
int resolve_self(char *ca_name, uint8_t ca_port, ib_portid_t *portid,
		 int *portnum, ibmad_gid_t *gid)
{
	umad_port_t port;
	uint64_t prefix, guid;
	int rc;

	if (!(portid || portnum || gid))
		return (-1);

	if ((rc = umad_get_port(ca_name, ca_port, &port)) < 0)
		return rc;

	if (portid) {
		memset(portid, 0, sizeof(*portid));
		portid->lid = port.base_lid;
		portid->sl = port.sm_sl;
	}
	if (portnum)
		*portnum = port.portnum;
	if (gid) {
		memset(gid, 0, sizeof(*gid));
		prefix = cl_ntoh64(port.gid_prefix);
		guid = cl_ntoh64(port.port_guid);
		mad_encode_field(*gid, IB_GID_PREFIX_F, &prefix);
		mad_encode_field(*gid, IB_GID_GUID_F, &guid);
	}

	umad_release_port(&port);

	return 0;
}

int resolve_gid(char *ca_name, uint8_t ca_port, ib_portid_t * portid,
		ibmad_gid_t gid, ib_portid_t * sm_id,
		const struct ibmad_port *srcport)
{
	ib_portid_t sm_portid;
	char buf[IB_SA_DATA_SIZE] = { 0 };

	if (!sm_id) {
		sm_id = &sm_portid;
		if (resolve_sm_portid(ca_name, ca_port, sm_id) < 0)
			return -1;
	}

	if ((portid->lid =
	     ib_path_query_via(srcport, gid, gid, sm_id, buf)) < 0)
		return -1;

	return 0;
}

int resolve_guid(char *ca_name, uint8_t ca_port, ib_portid_t *portid,
		 uint64_t *guid, ib_portid_t *sm_id,
		 const struct ibmad_port *srcport)
{
	ib_portid_t sm_portid;
	uint8_t buf[IB_SA_DATA_SIZE] = { 0 };
	uint64_t prefix;
	ibmad_gid_t selfgid;

	if (!sm_id) {
		sm_id = &sm_portid;
		if (resolve_sm_portid(ca_name, ca_port, sm_id) < 0)
			return -1;
	}

	if (resolve_self(ca_name, ca_port, NULL, NULL, &selfgid) < 0)
		return -1;

	memcpy(&prefix, portid->gid, sizeof(prefix));
	if (!prefix)
		mad_set_field64(portid->gid, 0, IB_GID_PREFIX_F,
				IB_DEFAULT_SUBN_PREFIX);
	if (guid)
		mad_set_field64(portid->gid, 0, IB_GID_GUID_F, *guid);

	if ((portid->lid =
	     ib_path_query_via(srcport, selfgid, portid->gid, sm_id, buf)) < 0)
		return -1;

	mad_decode_field(buf, IB_SA_PR_SL_F, &portid->sl);
	return 0;
}

/*
 * Callers of this function should ensure their ibmad_port has been opened with
 * IB_SA_CLASS as this function may require the SA to resolve addresses.
 */
int resolve_portid_str(char *ca_name, uint8_t ca_port, ib_portid_t * portid,
		       char *addr_str, enum MAD_DEST dest_type,
		       ib_portid_t *sm_id, const struct ibmad_port *srcport)
{
	ibmad_gid_t gid;
	uint64_t guid;
	int lid;
	char *routepath;
	ib_portid_t selfportid = { 0 };
	int selfport = 0;

	memset(portid, 0, sizeof *portid);

	switch (dest_type) {
	case IB_DEST_LID:
		lid = strtol(addr_str, 0, 0);
		if (!IB_LID_VALID(lid))
			return -1;
		return ib_portid_set(portid, lid, 0, 0);

	case IB_DEST_DRPATH:
		if (str2drpath(&portid->drpath, addr_str, 0, 0) < 0)
			return -1;
		return 0;

	case IB_DEST_GUID:
		if (!(guid = strtoull(addr_str, 0, 0)))
			return -1;

		/* keep guid in portid? */
		return resolve_guid(ca_name, ca_port, portid, &guid, sm_id,
				    srcport);

	case IB_DEST_DRSLID:
		lid = strtol(addr_str, &routepath, 0);
		routepath++;
		if (!IB_LID_VALID(lid))
			return -1;
		ib_portid_set(portid, lid, 0, 0);

		/* handle DR parsing and set DrSLID to local lid */
		if (resolve_self(ca_name, ca_port, &selfportid, &selfport,
				 NULL) < 0)
			return -1;
		if (str2drpath(&portid->drpath, routepath, selfportid.lid, 0) <
		    0)
			return -1;
		return 0;

	case IB_DEST_GID:
		if (inet_pton(AF_INET6, addr_str, &gid) <= 0)
			return -1;
		return resolve_gid(ca_name, ca_port, portid, gid, sm_id,
				   srcport);
	default:
		IBWARN("bad dest_type %d", dest_type);
	}

	return -1;
}

static unsigned int get_max_width(unsigned int num)
{
	unsigned r = 0;			/* 1x */

	if (num & 8)
		r = 3;			/* 12x */
	else {
		if (num & 4)
			r = 2;		/* 8x */
		else if (num & 2)
			r = 1;		/* 4x */
		else if (num & 0x10)
			r = 4;		/* 2x */
	}

        return (1 << r);
}

static unsigned int get_max(unsigned int num)
{
	unsigned r = 0;		// r will be lg(num)

	while (num >>= 1)	// unroll for more speed...
		r++;

	return (1 << r);
}

void get_max_msg(char *width_msg, char *speed_msg, int msg_size, ibnd_port_t * port)
{
	char buf[64];
	uint32_t max_speed = 0;
	uint32_t cap_mask, rem_cap_mask, fdr10;
	uint8_t *info = NULL;

	uint32_t max_width = get_max_width(mad_get_field(port->info, 0,
						   IB_PORT_LINK_WIDTH_SUPPORTED_F)
				     & mad_get_field(port->remoteport->info, 0,
						     IB_PORT_LINK_WIDTH_SUPPORTED_F));
	if ((max_width & mad_get_field(port->info, 0,
				       IB_PORT_LINK_WIDTH_ACTIVE_F)) == 0)
		// we are not at the max supported width
		// print what we could be at.
		snprintf(width_msg, msg_size, "Could be %s",
			 mad_dump_val(IB_PORT_LINK_WIDTH_ACTIVE_F,
				      buf, 64, &max_width));

	if (port->node->type == IB_NODE_SWITCH) {
		if (port->node->ports[0])
			info = (uint8_t *)&port->node->ports[0]->info;
	}
	else
		info = (uint8_t *)&port->info;

	if (info)
		cap_mask = mad_get_field(info, 0, IB_PORT_CAPMASK_F);
	else
		cap_mask = 0;

	info = NULL;
	if (port->remoteport->node->type == IB_NODE_SWITCH) {
		if (port->remoteport->node->ports[0])
			info = (uint8_t *)&port->remoteport->node->ports[0]->info;
	} else
		info = (uint8_t *)&port->remoteport->info;

	if (info)
		rem_cap_mask = mad_get_field(info, 0, IB_PORT_CAPMASK_F);
	else
		rem_cap_mask = 0;
	if (cap_mask & CL_NTOH32(IB_PORT_CAP_HAS_EXT_SPEEDS) &&
	    rem_cap_mask & CL_NTOH32(IB_PORT_CAP_HAS_EXT_SPEEDS))
		goto check_ext_speed;
check_fdr10_supp:
	fdr10 = (mad_get_field(port->ext_info, 0,
			       IB_MLNX_EXT_PORT_LINK_SPEED_SUPPORTED_F) & FDR10)
		&& (mad_get_field(port->remoteport->ext_info, 0,
				  IB_MLNX_EXT_PORT_LINK_SPEED_SUPPORTED_F) & FDR10);
	if (fdr10)
		goto check_fdr10_active;

	max_speed = get_max(mad_get_field(port->info, 0,
					  IB_PORT_LINK_SPEED_SUPPORTED_F)
			    & mad_get_field(port->remoteport->info, 0,
					    IB_PORT_LINK_SPEED_SUPPORTED_F));
	if ((max_speed & mad_get_field(port->info, 0,
				       IB_PORT_LINK_SPEED_ACTIVE_F)) == 0)
		// we are not at the max supported speed
		// print what we could be at.
		snprintf(speed_msg, msg_size, "Could be %s",
			 mad_dump_val(IB_PORT_LINK_SPEED_ACTIVE_F,
				      buf, 64, &max_speed));
	return;

check_ext_speed:
	if (mad_get_field(port->info, 0,
			  IB_PORT_LINK_SPEED_EXT_SUPPORTED_F) == 0 ||
	    mad_get_field(port->remoteport->info, 0,
			  IB_PORT_LINK_SPEED_EXT_SUPPORTED_F) == 0)
		goto check_fdr10_supp;
	max_speed = get_max(mad_get_field(port->info, 0,
					  IB_PORT_LINK_SPEED_EXT_SUPPORTED_F)
			    & mad_get_field(port->remoteport->info, 0,
					    IB_PORT_LINK_SPEED_EXT_SUPPORTED_F));
	if ((max_speed & mad_get_field(port->info, 0,
				       IB_PORT_LINK_SPEED_EXT_ACTIVE_F)) == 0)
		// we are not at the max supported extended speed
		// print what we could be at.
		snprintf(speed_msg, msg_size, "Could be %s",
			 mad_dump_val(IB_PORT_LINK_SPEED_EXT_ACTIVE_F,
				      buf, 64, &max_speed));
	return;

check_fdr10_active:
	if ((mad_get_field(port->ext_info, 0,
			   IB_MLNX_EXT_PORT_LINK_SPEED_ACTIVE_F) & FDR10) == 0) {
		/* Special case QDR to try to avoid confusion with FDR10 */
		if (mad_get_field(port->info, 0, IB_PORT_LINK_SPEED_ACTIVE_F) == 4)	/* QDR (10.0 Gbps) */
			snprintf(speed_msg, msg_size,
				 "Could be FDR10 (Found link at QDR but expected speed is FDR10)");
		else
			snprintf(speed_msg, msg_size, "Could be FDR10");
	}
}

int vsnprint_field(char *buf, size_t n, enum MAD_FIELDS f, int spacing,
		   const char *format, va_list va_args)
{
	int len, i, ret;

	len = strlen(mad_field_name(f));
	if (len + 2 > n || spacing + 1 > n)
		return 0;

	strncpy(buf, mad_field_name(f), n);
	buf[len] = ':';
	for (i = len+1; i < spacing+1; i++) {
		buf[i] = '.';
	}

	ret = vsnprintf(&buf[spacing+1], n - spacing, format, va_args);
	if (ret >= n - spacing)
		buf[n] = '\0';

	return ret + spacing;
}

int snprint_field(char *buf, size_t n, enum MAD_FIELDS f, int spacing,
		  const char *format, ...)
{
	va_list val;
	int ret;

	va_start(val, format);
	ret = vsnprint_field(buf, n, f, spacing, format, val);
	va_end(val);

	return ret;
}

void dump_portinfo(void *pi, int tabs)
{
	int field, i;
	char val[64];
	char buf[1024];

	for (field = IB_PORT_FIRST_F; field < IB_PORT_LAST_F; field++) {
		for (i=0;i<tabs;i++)
			printf("\t");
		if (field == IB_PORT_MKEY_F && show_keys == 0) {
			snprint_field(buf, 1024, field, 32, NOT_DISPLAYED_STR);
		} else {
			mad_decode_field(pi, field, val);
			if (!mad_dump_field(field, buf, 1024, val))
				return;
		}
		printf("%s\n", buf);
	}

	for (field = IB_PORT_CAPMASK2_F;
	     field < IB_PORT_LINK_SPEED_EXT_LAST_F; field++) {
		for (i=0;i<tabs;i++)
			printf("\t");
		mad_decode_field(pi, field, val);
		if (!mad_dump_field(field, buf, 1024, val))
			return;
		printf("%s\n", buf);
	}
}

op_fn_t *match_op(const match_rec_t match_tbl[], char *name)
{
	const match_rec_t *r;
	for (r = match_tbl; r->name; r++)
		if (!strcasecmp(r->name, name) ||
		    (r->alias && !strcasecmp(r->alias, name)))
			return r->fn;
	return NULL;
}
