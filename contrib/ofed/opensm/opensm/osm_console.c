/*
 * Copyright (c) 2005-2009 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2009,2010 HNR Consulting. All rights reserved.
 * Copyright (c) 2010,2011 Mellanox Technologies LTD. All rights reserved.
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

#define	_WITH_GETLINE		/* for getline */
#include <stdio.h>
#include <stdlib.h>
#include <sys/poll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <regex.h>
#ifdef ENABLE_OSM_CONSOLE_LOOPBACK
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <sys/time.h>
#include <opensm/osm_file_ids.h>
#define FILE_ID OSM_FILE_CONSOLE_C
#include <opensm/osm_console.h>
#include <complib/cl_passivelock.h>
#include <opensm/osm_perfmgr.h>
#include <opensm/osm_subnet.h>

extern void osm_update_node_desc(IN osm_opensm_t *osm);

struct command {
	const char *name;
	void (*help_function) (FILE * out, int detail);
	void (*parse_function) (char **p_last, osm_opensm_t * p_osm,
				FILE * out);
};

static struct {
	int on;
	int delay_s;
	time_t previous;
	void (*loop_function) (osm_opensm_t * p_osm, FILE * out);
} loop_command = {
.on = 0, .delay_s = 2, .loop_function = NULL};

static const struct command console_cmds[];

static char *next_token(char **p_last)
{
	return strtok_r(NULL, " \t\n\r", p_last);
}

#ifdef ENABLE_OSM_PERF_MGR
static char *name_token(char **p_last)
{
	return strtok_r(NULL, "\t\n\r", p_last);
}
#endif

static void help_command(FILE * out, int detail)
{
	int i;

	fprintf(out, "Supported commands and syntax:\n");
	fprintf(out, "help [<command>]\n");
	/* skip help command */
	for (i = 1; console_cmds[i].name; i++)
		console_cmds[i].help_function(out, 0);
}

static void help_quit(FILE * out, int detail)
{
	fprintf(out, "quit (not valid in local mode; use ctl-c)\n");
}

static void help_loglevel(FILE * out, int detail)
{
	fprintf(out, "loglevel [<log-level>]\n");
	if (detail) {
		fprintf(out, "   log-level is OR'ed from the following\n");
		fprintf(out, "   OSM_LOG_NONE             0x%02X\n",
			OSM_LOG_NONE);
		fprintf(out, "   OSM_LOG_ERROR            0x%02X\n",
			OSM_LOG_ERROR);
		fprintf(out, "   OSM_LOG_INFO             0x%02X\n",
			OSM_LOG_INFO);
		fprintf(out, "   OSM_LOG_VERBOSE          0x%02X\n",
			OSM_LOG_VERBOSE);
		fprintf(out, "   OSM_LOG_DEBUG            0x%02X\n",
			OSM_LOG_DEBUG);
		fprintf(out, "   OSM_LOG_FUNCS            0x%02X\n",
			OSM_LOG_FUNCS);
		fprintf(out, "   OSM_LOG_FRAMES           0x%02X\n",
			OSM_LOG_FRAMES);
		fprintf(out, "   OSM_LOG_ROUTING          0x%02X\n",
			OSM_LOG_ROUTING);
		fprintf(out, "   OSM_LOG_SYS              0x%02X\n",
			OSM_LOG_SYS);
		fprintf(out, "\n");
		fprintf(out, "   OSM_LOG_DEFAULT_LEVEL    0x%02X\n",
			OSM_LOG_DEFAULT_LEVEL);
	}
}

static void help_permodlog(FILE * out, int detail)
{
	fprintf(out, "permodlog\n");
}

static void help_priority(FILE * out, int detail)
{
	fprintf(out, "priority [<sm-priority>]\n");
}

static void help_resweep(FILE * out, int detail)
{
	fprintf(out, "resweep [heavy|light]\n");
}

static void help_reroute(FILE * out, int detail)
{
	fprintf(out, "reroute\n");
	if (detail) {
		fprintf(out, "reroute the fabric\n");
	}
}

static void help_sweep(FILE * out, int detail)
{
	fprintf(out, "sweep [on|off]\n");
	if (detail) {
		fprintf(out, "enable or disable sweeping\n");
		fprintf(out, "   [on] sweep normally\n");
		fprintf(out, "   [off] inhibit all sweeping\n");
	}
}

static void help_status(FILE * out, int detail)
{
	fprintf(out, "status [loop]\n");
	if (detail) {
		fprintf(out, "   loop -- type \"q<ret>\" to quit\n");
	}
}

static void help_logflush(FILE * out, int detail)
{
	fprintf(out, "logflush [on|off] -- toggle opensm.log file flushing\n");
}

static void help_querylid(FILE * out, int detail)
{
	fprintf(out,
		"querylid lid -- print internal information about the lid specified\n");
}

static void help_portstatus(FILE * out, int detail)
{
	fprintf(out, "portstatus [ca|switch|router]\n");
	if (detail) {
		fprintf(out, "summarize port status\n");
		fprintf(out,
			"   [ca|switch|router] -- limit the results to the node type specified\n");
	}

}

static void help_switchbalance(FILE * out, int detail)
{
	fprintf(out, "switchbalance [verbose] [guid]\n");
	if (detail) {
		fprintf(out, "output switch balancing information\n");
		fprintf(out,
			"  [verbose] -- verbose output\n"
			"  [guid] -- limit results to specified guid\n");
	}
}

static void help_lidbalance(FILE * out, int detail)
{
	fprintf(out, "lidbalance [switchguid]\n");
	if (detail) {
		fprintf(out, "output lid balanced forwarding information\n");
		fprintf(out,
			"  [switchguid] -- limit results to specified switch guid\n");
	}
}

static void help_dump_conf(FILE *out, int detail)
{
	fprintf(out, "dump_conf\n");
	if (detail) {
		fprintf(out, "dump current opensm configuration\n");
	}
}

static void help_update_desc(FILE *out, int detail)
{
	fprintf(out, "update_desc\n");
	if (detail) {
		fprintf(out, "update node description for all nodes\n");
	}
}

#ifdef ENABLE_OSM_PERF_MGR
static void help_perfmgr(FILE * out, int detail)
{
	fprintf(out,
		"perfmgr(pm) [enable|disable\n"
		"             |clear_counters|dump_counters|print_counters(pc)|print_errors(pe)\n"
		"             |set_rm_nodes|clear_rm_nodes|clear_inactive\n"
		"             |set_query_cpi|clear_query_cpi\n"
		"             |dump_redir|clear_redir\n"
		"             |sweep|sweep_time[seconds]]\n");
	if (detail) {
		fprintf(out,
			"perfmgr -- print the performance manager state\n");
		fprintf(out,
			"   [enable|disable] -- change the perfmgr state\n");
		fprintf(out,
			"   [sweep] -- Initiate a sweep of the fabric\n");
		fprintf(out,
			"   [sweep_time] -- change the perfmgr sweep time (requires [seconds] option)\n");
		fprintf(out,
			"   [clear_counters] -- clear the counters stored\n");
		fprintf(out,
			"   [dump_counters [mach]] -- dump the counters (optionally in [mach]ine readable format)\n");
		fprintf(out,
			"   [print_counters [<nodename|nodeguid>][:<port>]] -- print the internal counters\n"
			"                                                      Optionally limit output by name, guid, or port\n");
		fprintf(out,
			"   [pc [<nodename|nodeguid>][:<port>]] -- same as print_counters\n");
		fprintf(out,
			"   [print_errors [<nodename|nodeguid>]] -- print only ports with errors\n"
			"                                           Optionally limit output by name or guid\n");
		fprintf(out,
			"   [pe [<nodename|nodeguid>]] -- same as print_errors\n");
		fprintf(out,
			"   [dump_redir [<nodename|nodeguid>]] -- dump the redirection table\n");
		fprintf(out,
			"   [clear_redir [<nodename|nodeguid>]] -- clear the redirection table\n");
		fprintf(out,
			"   [[set|clear]_rm_nodes] -- enable/disable the removal of \"inactive\" nodes from the DB\n"
			"                             Inactive nodes are those which no longer appear on the fabric\n");
		fprintf(out,
			"   [[set|clear]_query_cpi] -- enable/disable PerfMgrGet(ClassPortInfo)\n"
			"                             ClassPortInfo indicates hardware support for extended attributes such as PortCountersExtended\n");
		fprintf(out,
			"   [clear_inactive] -- Delete inactive nodes from the DB\n");
	}
}
static void help_pm(FILE *out, int detail)
{
	if (detail)
		help_perfmgr(out, detail);
}
#endif				/* ENABLE_OSM_PERF_MGR */

/* more help routines go here */

static void help_parse(char **p_last, osm_opensm_t * p_osm, FILE * out)
{
	char *p_cmd;
	int i, found = 0;

	p_cmd = next_token(p_last);
	if (!p_cmd)
		help_command(out, 0);
	else {
		for (i = 1; console_cmds[i].name; i++) {
			if (!strcmp(p_cmd, console_cmds[i].name)) {
				found = 1;
				console_cmds[i].help_function(out, 1);
				break;
			}
		}
		if (!found) {
			fprintf(out, "%s : Command not found\n\n", p_cmd);
			help_command(out, 0);
		}
	}
}

static void loglevel_parse(char **p_last, osm_opensm_t * p_osm, FILE * out)
{
	char *p_cmd;
	int level;

	p_cmd = next_token(p_last);
	if (!p_cmd)
		fprintf(out, "Current log level is 0x%x\n",
			osm_log_get_level(&p_osm->log));
	else {
		/* Handle x, 0x, and decimal specification of log level */
		if (!strncmp(p_cmd, "x", 1)) {
			p_cmd++;
			level = strtoul(p_cmd, NULL, 16);
		} else {
			if (!strncmp(p_cmd, "0x", 2)) {
				p_cmd += 2;
				level = strtoul(p_cmd, NULL, 16);
			} else
				level = strtol(p_cmd, NULL, 10);
		}
		if ((level >= 0) && (level < 256)) {
			fprintf(out, "Setting log level to 0x%x\n", level);
			osm_log_set_level(&p_osm->log, level);
		} else
			fprintf(out, "Invalid log level 0x%x\n", level);
	}
}

static void permodlog_parse(char **p_last, osm_opensm_t * p_osm, FILE * out)
{
	FILE *fp;
	char buf[1024];

	if (p_osm->subn.opt.per_module_logging_file != NULL) {
		fp = fopen(p_osm->subn.opt.per_module_logging_file, "r");
		if (!fp) {
			if (errno == ENOENT)
				return;
			fprintf(out, "fopen(%s) failed: %s\n",
				p_osm->subn.opt.per_module_logging_file,
				strerror(errno));
			return;
		}

		fprintf(out, "Per module logging file: %s\n",
			p_osm->subn.opt.per_module_logging_file);
		while (fgets(buf, sizeof buf, fp) != NULL)
			fprintf(out, "%s", buf);
		fclose(fp);
		fprintf(out, "\n");
	}
}

static void priority_parse(char **p_last, osm_opensm_t * p_osm, FILE * out)
{
	char *p_cmd;
	int priority;

	p_cmd = next_token(p_last);
	if (!p_cmd)
		fprintf(out, "Current sm-priority is %d\n",
			p_osm->subn.opt.sm_priority);
	else {
		priority = strtol(p_cmd, NULL, 0);
		if (0 > priority || 15 < priority)
			fprintf(out,
				"Invalid sm-priority %d; must be between 0 and 15\n",
				priority);
		else {
			fprintf(out, "Setting sm-priority to %d\n", priority);
			osm_set_sm_priority(&p_osm->sm, (uint8_t)priority);
		}
	}
}

static const char *sm_state_str(int state)
{
	switch (state) {
	case IB_SMINFO_STATE_DISCOVERING:
		return "Discovering";
	case IB_SMINFO_STATE_STANDBY:
		return "Standby    ";
	case IB_SMINFO_STATE_NOTACTIVE:
		return "Not Active ";
	case IB_SMINFO_STATE_MASTER:
		return "Master     ";
	}
	return "UNKNOWN    ";
}

static const char *sa_state_str(osm_sa_state_t state)
{
	switch (state) {
	case OSM_SA_STATE_INIT:
		return "Init";
	case OSM_SA_STATE_READY:
		return "Ready";
	}
	return "UNKNOWN";
}

static void dump_sms(osm_opensm_t * p_osm, FILE * out)
{
	osm_subn_t *p_subn = &p_osm->subn;
	osm_remote_sm_t *p_rsm;

	fprintf(out, "\n   Known SMs\n"
		     "   ---------\n");
	fprintf(out, "   Port GUID       SM State    Priority\n");
	fprintf(out, "   ---------       --------    --------\n");
	fprintf(out, "   0x%" PRIx64 " %s %d        SELF\n",
		cl_ntoh64(p_subn->sm_port_guid),
		sm_state_str(p_subn->sm_state),
		p_subn->opt.sm_priority);

	CL_PLOCK_ACQUIRE(p_osm->sm.p_lock);
	p_rsm = (osm_remote_sm_t *) cl_qmap_head(&p_subn->sm_guid_tbl);
	while (p_rsm != (osm_remote_sm_t *) cl_qmap_end(&p_subn->sm_guid_tbl)) {
		fprintf(out, "   0x%" PRIx64 " %s %d\n",
			cl_ntoh64(p_rsm->smi.guid),
			sm_state_str(ib_sminfo_get_state(&p_rsm->smi)),
			ib_sminfo_get_priority(&p_rsm->smi));
		p_rsm = (osm_remote_sm_t *) cl_qmap_next(&p_rsm->map_item);
	}
	CL_PLOCK_RELEASE(p_osm->sm.p_lock);
}

static void print_status(osm_opensm_t * p_osm, FILE * out)
{
	cl_list_item_t *item;

	if (out) {
		const char *re_str;

		cl_plock_acquire(&p_osm->lock);
		fprintf(out, "   OpenSM Version       : %s\n", p_osm->osm_version);
		fprintf(out, "   SM State             : %s\n",
			sm_state_str(p_osm->subn.sm_state));
		fprintf(out, "   SM Priority          : %d\n",
			p_osm->subn.opt.sm_priority);
		fprintf(out, "   SA State             : %s\n",
			sa_state_str(p_osm->sa.state));

		re_str = p_osm->routing_engine_used ?
			osm_routing_engine_type_str(p_osm->routing_engine_used->type) :
			osm_routing_engine_type_str(OSM_ROUTING_ENGINE_TYPE_NONE);
		fprintf(out, "   Routing Engine       : %s\n", re_str);

		fprintf(out, "   Loaded event plugins :");
		if (cl_qlist_head(&p_osm->plugin_list) ==
			cl_qlist_end(&p_osm->plugin_list)) {
			fprintf(out, " <none>");
		}
		for (item = cl_qlist_head(&p_osm->plugin_list);
		     item != cl_qlist_end(&p_osm->plugin_list);
		     item = cl_qlist_next(item))
			fprintf(out, " %s",
				((osm_epi_plugin_t *)item)->plugin_name);
		fprintf(out, "\n");

#ifdef ENABLE_OSM_PERF_MGR
		fprintf(out, "\n   PerfMgr state/sweep state : %s/%s\n",
			osm_perfmgr_get_state_str(&p_osm->perfmgr),
			osm_perfmgr_get_sweep_state_str(&p_osm->perfmgr));
#endif
		fprintf(out, "\n   MAD stats\n"
			"   ---------\n"
			"   QP0 MADs outstanding           : %u\n"
			"   QP0 MADs outstanding (on wire) : %u\n"
			"   QP0 MADs rcvd                  : %u\n"
			"   QP0 MADs sent                  : %u\n"
			"   QP0 unicasts sent              : %u\n"
			"   QP0 unknown MADs rcvd          : %u\n"
			"   SA MADs outstanding            : %u\n"
			"   SA MADs rcvd                   : %u\n"
			"   SA MADs sent                   : %u\n"
			"   SA unknown MADs rcvd           : %u\n"
			"   SA MADs ignored                : %u\n",
			(uint32_t)p_osm->stats.qp0_mads_outstanding,
			(uint32_t)p_osm->stats.qp0_mads_outstanding_on_wire,
			(uint32_t)p_osm->stats.qp0_mads_rcvd,
			(uint32_t)p_osm->stats.qp0_mads_sent,
			(uint32_t)p_osm->stats.qp0_unicasts_sent,
			(uint32_t)p_osm->stats.qp0_mads_rcvd_unknown,
			(uint32_t)p_osm->stats.sa_mads_outstanding,
			(uint32_t)p_osm->stats.sa_mads_rcvd,
			(uint32_t)p_osm->stats.sa_mads_sent,
			(uint32_t)p_osm->stats.sa_mads_rcvd_unknown,
			(uint32_t)p_osm->stats.sa_mads_ignored);
		fprintf(out, "\n   Subnet flags\n"
			"   ------------\n"
			"   Sweeping enabled               : %d\n"
			"   Sweep interval (seconds)       : %u\n"
			"   Ignore existing lfts           : %d\n"
			"   Subnet Init errors             : %d\n"
			"   In sweep hop 0                 : %d\n"
			"   First time master sweep        : %d\n"
			"   Coming out of standby          : %d\n",
			p_osm->subn.sweeping_enabled,
			p_osm->subn.opt.sweep_interval,
			p_osm->subn.ignore_existing_lfts,
			p_osm->subn.subnet_initialization_error,
			p_osm->subn.in_sweep_hop_0,
			p_osm->subn.first_time_master_sweep,
			p_osm->subn.coming_out_of_standby);
		dump_sms(p_osm, out);
		fprintf(out, "\n");
		cl_plock_release(&p_osm->lock);
	}
}

static int loop_command_check_time(void)
{
	time_t cur = time(NULL);
	if ((loop_command.previous + loop_command.delay_s) < cur) {
		loop_command.previous = cur;
		return 1;
	}
	return 0;
}

static void status_parse(char **p_last, osm_opensm_t * p_osm, FILE * out)
{
	char *p_cmd;

	p_cmd = next_token(p_last);
	if (p_cmd) {
		if (strcmp(p_cmd, "loop") == 0) {
			fprintf(out, "Looping on status command...\n");
			fflush(out);
			loop_command.on = 1;
			loop_command.previous = time(NULL);
			loop_command.loop_function = print_status;
		} else {
			help_status(out, 1);
			return;
		}
	}
	print_status(p_osm, out);
}

static void resweep_parse(char **p_last, osm_opensm_t * p_osm, FILE * out)
{
	char *p_cmd;

	p_cmd = next_token(p_last);
	if (!p_cmd ||
	    (strcmp(p_cmd, "heavy") != 0 && strcmp(p_cmd, "light") != 0)) {
		fprintf(out, "Invalid resweep command\n");
		help_resweep(out, 1);
	} else {
		if (strcmp(p_cmd, "heavy") == 0)
			p_osm->subn.force_heavy_sweep = TRUE;
		osm_opensm_sweep(p_osm);
	}
}

static void reroute_parse(char **p_last, osm_opensm_t * p_osm, FILE * out)
{
	p_osm->subn.force_reroute = TRUE;
	osm_opensm_sweep(p_osm);
}

static void sweep_parse(char **p_last, osm_opensm_t * p_osm, FILE * out)
{
	char *p_cmd;

	p_cmd = next_token(p_last);
	if (!p_cmd ||
	    (strcmp(p_cmd, "on") != 0 && strcmp(p_cmd, "off") != 0)) {
		fprintf(out, "Invalid sweep command\n");
		help_sweep(out, 1);
	} else {
		if (strcmp(p_cmd, "on") == 0)
			p_osm->subn.sweeping_enabled = TRUE;
		else
			p_osm->subn.sweeping_enabled = FALSE;
	}
}

static void logflush_parse(char **p_last, osm_opensm_t * p_osm, FILE * out)
{
	char *p_cmd;

	p_cmd = next_token(p_last);
	if (!p_cmd ||
	    (strcmp(p_cmd, "on") != 0 && strcmp(p_cmd, "off") != 0)) {
		fprintf(out, "Invalid logflush command\n");
		help_sweep(out, 1);
	} else {
		if (strcmp(p_cmd, "on") == 0) {
			p_osm->log.flush = TRUE;
	                fflush(p_osm->log.out_port);
		} else
			p_osm->log.flush = FALSE;
	}
}

static void querylid_parse(char **p_last, osm_opensm_t * p_osm, FILE * out)
{
	unsigned int p = 0;
	uint16_t lid = 0;
	osm_port_t *p_port = NULL;
	char *p_cmd = next_token(p_last);

	if (!p_cmd) {
		fprintf(out, "no LID specified\n");
		help_querylid(out, 1);
		return;
	}

	lid = (uint16_t) strtoul(p_cmd, NULL, 0);
	cl_plock_acquire(&p_osm->lock);
	p_port = osm_get_port_by_lid_ho(&p_osm->subn, lid);
	if (!p_port)
		goto invalid_lid;

	fprintf(out, "Query results for LID %u\n", lid);
	fprintf(out,
		"   GUID                : 0x%016" PRIx64 "\n"
		"   Node Desc           : %s\n"
		"   Node Type           : %s\n"
		"   Num Ports           : %d\n",
		cl_ntoh64(p_port->guid),
		p_port->p_node->print_desc,
		ib_get_node_type_str(osm_node_get_type(p_port->p_node)),
		p_port->p_node->node_info.num_ports);

	if (p_port->p_node->sw)
		p = 0;
	else
		p = 1;
	for ( /* see above */ ; p < p_port->p_node->physp_tbl_size; p++) {
		fprintf(out,
			"   Port %u health       : %s\n",
			p,
			p_port->p_node->physp_table[p].
			healthy ? "OK" : "ERROR");
	}

	cl_plock_release(&p_osm->lock);
	return;

invalid_lid:
	cl_plock_release(&p_osm->lock);
	fprintf(out, "Invalid lid %d\n", lid);
	return;
}

/**
 * Data structures for the portstatus command
 */
typedef struct _port_report {
	struct _port_report *next;
	uint64_t node_guid;
	uint8_t port_num;
	char print_desc[IB_NODE_DESCRIPTION_SIZE + 1];
} port_report_t;

static void
__tag_port_report(port_report_t ** head, uint64_t node_guid,
		  uint8_t port_num, char *print_desc)
{
	port_report_t *rep = malloc(sizeof(*rep));
	if (!rep)
		return;

	rep->node_guid = node_guid;
	rep->port_num = port_num;
	memcpy(rep->print_desc, print_desc, IB_NODE_DESCRIPTION_SIZE + 1);
	rep->next = NULL;
	if (*head) {
		rep->next = *head;
		*head = rep;
	} else
		*head = rep;
}

static void __print_port_report(FILE * out, port_report_t * head)
{
	port_report_t *item = head;
	while (item != NULL) {
		fprintf(out, "      0x%016" PRIx64 " %d (%s)\n",
			item->node_guid, item->port_num, item->print_desc);
		port_report_t *next = item->next;
		free(item);
		item = next;
	}
}

typedef struct {
	uint8_t node_type_lim;	/* limit the results; 0 == ALL */
	uint64_t total_nodes;
	uint64_t total_ports;
	uint64_t ports_down;
	uint64_t ports_active;
	uint64_t ports_disabled;
	port_report_t *disabled_ports;
	uint64_t ports_1X;
	uint64_t ports_4X;
	uint64_t ports_8X;
	uint64_t ports_12X;
	uint64_t ports_2X;
	uint64_t ports_unknown_width;
	port_report_t *unknown_width_ports;
	uint64_t ports_unenabled_width;
	port_report_t *unenabled_width_ports;
	uint64_t ports_reduced_width;
	port_report_t *reduced_width_ports;
	uint64_t ports_sdr;
	uint64_t ports_ddr;
	uint64_t ports_qdr;
	uint64_t ports_fdr10;
	uint64_t ports_fdr;
	uint64_t ports_edr;
	uint64_t ports_unknown_speed;
	port_report_t *unknown_speed_ports;
	uint64_t ports_unenabled_speed;
	port_report_t *unenabled_speed_ports;
	uint64_t ports_reduced_speed;
	port_report_t *reduced_speed_ports;
} fabric_stats_t;

/**
 * iterator function to get portstatus on each node
 */
static void __get_stats(cl_map_item_t * const p_map_item, void *context)
{
	fabric_stats_t *fs = (fabric_stats_t *) context;
	osm_node_t *node = (osm_node_t *) p_map_item;
	osm_physp_t *physp0;
	ib_port_info_t *pi0;
	uint8_t num_ports = osm_node_get_num_physp(node);
	uint8_t port = 0;

	/* Skip nodes we are not interested in */
	if (fs->node_type_lim != 0
	    && fs->node_type_lim != node->node_info.node_type)
		return;

	fs->total_nodes++;

	if (osm_node_get_type(node) == IB_NODE_TYPE_SWITCH) {
		physp0 = osm_node_get_physp_ptr(node, 0);
		pi0 = &physp0->port_info;
	} else
		pi0 = NULL;

	for (port = 1; port < num_ports; port++) {
		osm_physp_t *phys = osm_node_get_physp_ptr(node, port);
		ib_port_info_t *pi = NULL;
		ib_mlnx_ext_port_info_t *epi = NULL;
		uint8_t active_speed = 0;
		uint8_t enabled_speed = 0;
		uint8_t active_width = 0;
		uint8_t enabled_width = 0;
		uint8_t port_state = 0;
		uint8_t port_phys_state = 0;

		if (!phys)
			continue;

		pi = &phys->port_info;
		epi = &phys->ext_port_info;
		if (!pi0)
			pi0 = pi;
		active_speed = ib_port_info_get_link_speed_active(pi);
		enabled_speed = ib_port_info_get_link_speed_enabled(pi);
		active_width = pi->link_width_active;
		enabled_width = pi->link_width_enabled;
		port_state = ib_port_info_get_port_state(pi);
		port_phys_state = ib_port_info_get_port_phys_state(pi);

		if (port_state == IB_LINK_DOWN)
			fs->ports_down++;
		else if (port_state == IB_LINK_ACTIVE)
			fs->ports_active++;
		if (port_phys_state == IB_PORT_PHYS_STATE_DISABLED) {
			__tag_port_report(&(fs->disabled_ports),
					  cl_ntoh64(node->node_info.node_guid),
					  port, node->print_desc);
			fs->ports_disabled++;
		}

		fs->total_ports++;

		if (port_state == IB_LINK_DOWN)
			continue;

		if (!(active_width & enabled_width)) {
			__tag_port_report(&(fs->unenabled_width_ports),
					  cl_ntoh64(node->node_info.node_guid),
					  port, node->print_desc);
			fs->ports_unenabled_width++;
		}
		else if ((enabled_width ^ active_width) > active_width) {
			__tag_port_report(&(fs->reduced_width_ports),
					  cl_ntoh64(node->node_info.node_guid),
					  port, node->print_desc);
			fs->ports_reduced_width++;
		}

		/* unenabled speed usually due to problems with force_link_speed */
		if (!(active_speed & enabled_speed)) {
			__tag_port_report(&(fs->unenabled_speed_ports),
					  cl_ntoh64(node->node_info.node_guid),
					  port, node->print_desc);
			fs->ports_unenabled_speed++;
		}
		else if ((enabled_speed ^ active_speed) > active_speed) {
			__tag_port_report(&(fs->reduced_speed_ports),
					  cl_ntoh64(node->node_info.node_guid),
					  port, node->print_desc);
			fs->ports_reduced_speed++;
		}

		switch (active_speed) {
		case IB_LINK_SPEED_ACTIVE_2_5:
			fs->ports_sdr++;
			break;
		case IB_LINK_SPEED_ACTIVE_5:
			fs->ports_ddr++;
			break;
		case IB_LINK_SPEED_ACTIVE_10:
			if (!(pi0->capability_mask & IB_PORT_CAP_HAS_EXT_SPEEDS) ||
			    !ib_port_info_get_link_speed_ext_active(pi)) {
				if (epi->link_speed_active & FDR10)
					fs->ports_fdr10++;
				else {
					fs->ports_qdr++;
					/* check for speed reduced from FDR10 */
					if (epi->link_speed_enabled & FDR10) {
						__tag_port_report(&(fs->reduced_speed_ports),
								  cl_ntoh64(node->node_info.node_guid),
								  port, node->print_desc);
						fs->ports_reduced_speed++;
					}
				}
			}
			break;
		case IB_LINK_SPEED_ACTIVE_EXTENDED:
			break;
		default:
			__tag_port_report(&(fs->unknown_speed_ports),
					  cl_ntoh64(node->node_info.node_guid),
					  port, node->print_desc);
			fs->ports_unknown_speed++;
			break;
		}
		if (pi0->capability_mask & IB_PORT_CAP_HAS_EXT_SPEEDS &&
		    ib_port_info_get_link_speed_ext_sup(pi) &&
		    (enabled_speed = ib_port_info_get_link_speed_ext_enabled(pi)) != IB_LINK_SPEED_EXT_DISABLE &&
		    active_speed == IB_LINK_SPEED_ACTIVE_10) {
			active_speed = ib_port_info_get_link_speed_ext_active(pi);
			if (!(active_speed & enabled_speed)) {
				__tag_port_report(&(fs->unenabled_speed_ports),
						  cl_ntoh64(node->node_info.node_guid),
						  port, node->print_desc);
				fs->ports_unenabled_speed++;
			}
			else if ((enabled_speed ^ active_speed) > active_speed) {
				__tag_port_report(&(fs->reduced_speed_ports),
						  cl_ntoh64(node->node_info.node_guid),
						  port, node->print_desc);
				fs->ports_reduced_speed++;
			}
			switch (active_speed) {
			case IB_LINK_SPEED_EXT_ACTIVE_14:
				fs->ports_fdr++;
				break;
			case IB_LINK_SPEED_EXT_ACTIVE_25:
				fs->ports_edr++;
				break;
			case IB_LINK_SPEED_EXT_ACTIVE_NONE:
				break;
			default:
				__tag_port_report(&(fs->unknown_speed_ports),
						  cl_ntoh64(node->node_info.node_guid),
						  port, node->print_desc);
				fs->ports_unknown_speed++;
				break;
			}
		}
		switch (active_width) {
		case IB_LINK_WIDTH_ACTIVE_1X:
			fs->ports_1X++;
			break;
		case IB_LINK_WIDTH_ACTIVE_4X:
			fs->ports_4X++;
			break;
		case IB_LINK_WIDTH_ACTIVE_8X:
			fs->ports_8X++;
			break;
		case IB_LINK_WIDTH_ACTIVE_12X:
			fs->ports_12X++;
			break;
		case IB_LINK_WIDTH_ACTIVE_2X:
			fs->ports_2X++;
			break;
		default:
			__tag_port_report(&(fs->unknown_width_ports),
					  cl_ntoh64(node->node_info.node_guid),
					  port, node->print_desc);
			fs->ports_unknown_width++;
			break;
		}
	}
}

static void portstatus_parse(char **p_last, osm_opensm_t * p_osm, FILE * out)
{
	fabric_stats_t fs;
	struct timeval before, after;
	char *p_cmd;

	memset(&fs, 0, sizeof(fs));

	p_cmd = next_token(p_last);
	if (p_cmd) {
		if (strcmp(p_cmd, "ca") == 0) {
			fs.node_type_lim = IB_NODE_TYPE_CA;
		} else if (strcmp(p_cmd, "switch") == 0) {
			fs.node_type_lim = IB_NODE_TYPE_SWITCH;
		} else if (strcmp(p_cmd, "router") == 0) {
			fs.node_type_lim = IB_NODE_TYPE_ROUTER;
		} else {
			fprintf(out, "Node type not understood\n");
			help_portstatus(out, 1);
			return;
		}
	}

	gettimeofday(&before, NULL);

	/* for each node in the system gather the stats */
	cl_plock_acquire(&p_osm->lock);
	cl_qmap_apply_func(&(p_osm->subn.node_guid_tbl), __get_stats,
			   (void *)&fs);
	cl_plock_release(&p_osm->lock);

	gettimeofday(&after, NULL);

	/* report the stats */
	fprintf(out, "\"%s\" port status:\n",
		fs.node_type_lim ? ib_get_node_type_str(fs.
							node_type_lim) : "ALL");
	fprintf(out,
		"   %" PRIu64 " port(s) scanned on %" PRIu64
		" nodes in %lu us\n", fs.total_ports, fs.total_nodes,
		after.tv_usec - before.tv_usec);

	if (fs.ports_down)
		fprintf(out, "   %" PRIu64 " down\n", fs.ports_down);
	if (fs.ports_active)
		fprintf(out, "   %" PRIu64 " active\n", fs.ports_active);
	if (fs.ports_1X)
		fprintf(out, "   %" PRIu64 " at 1X\n", fs.ports_1X);
	if (fs.ports_4X)
		fprintf(out, "   %" PRIu64 " at 4X\n", fs.ports_4X);
	if (fs.ports_8X)
		fprintf(out, "   %" PRIu64 " at 8X\n", fs.ports_8X);
	if (fs.ports_12X)
		fprintf(out, "   %" PRIu64 " at 12X\n", fs.ports_12X);

	if (fs.ports_sdr)
		fprintf(out, "   %" PRIu64 " at 2.5 Gbps\n", fs.ports_sdr);
	if (fs.ports_ddr)
		fprintf(out, "   %" PRIu64 " at 5.0 Gbps\n", fs.ports_ddr);
	if (fs.ports_qdr)
		fprintf(out, "   %" PRIu64 " at 10.0 Gbps\n", fs.ports_qdr);
	if (fs.ports_fdr10)
		fprintf(out, "   %" PRIu64 " at 10.0 Gbps (FDR10)\n", fs.ports_fdr10);
	if (fs.ports_fdr)
		fprintf(out, "   %" PRIu64 " at 14.0625 Gbps\n", fs.ports_fdr);
	if (fs.ports_edr)
		fprintf(out, "   %" PRIu64 " at 25.78125 Gbps\n", fs.ports_edr);

	if (fs.ports_disabled + fs.ports_reduced_speed + fs.ports_reduced_width
	    + fs.ports_unenabled_width + fs.ports_unenabled_speed
	    + fs.ports_unknown_width + fs.ports_unknown_speed > 0) {
		fprintf(out, "\nPossible issues:\n");
	}
	if (fs.ports_disabled) {
		fprintf(out, "   %" PRIu64 " disabled\n", fs.ports_disabled);
		__print_port_report(out, fs.disabled_ports);
	}
	if (fs.ports_unenabled_speed) {
		fprintf(out, "   %" PRIu64 " with unenabled speed\n",
			fs.ports_unenabled_speed);
		__print_port_report(out, fs.unenabled_speed_ports);
	}
	if (fs.ports_reduced_speed) {
		fprintf(out, "   %" PRIu64 " with reduced speed\n",
			fs.ports_reduced_speed);
		__print_port_report(out, fs.reduced_speed_ports);
	}
	if (fs.ports_unknown_speed) {
		fprintf(out, "   %" PRIu64 " with unknown speed\n",
			fs.ports_unknown_speed);
		__print_port_report(out, fs.unknown_speed_ports);
	}
	if (fs.ports_unenabled_width) {
		fprintf(out, "   %" PRIu64 " with unenabled width\n",
			fs.ports_unenabled_width);
		__print_port_report(out, fs.unenabled_width_ports);
	}
	if (fs.ports_reduced_width) {
		fprintf(out, "   %" PRIu64 " with reduced width\n",
			fs.ports_reduced_width);
		__print_port_report(out, fs.reduced_width_ports);
	}
	if (fs.ports_unknown_width) {
		fprintf(out, "   %" PRIu64 " with unknown width\n",
			fs.ports_unknown_width);
		__print_port_report(out, fs.unknown_width_ports);
	}
	fprintf(out, "\n");
}

static void switchbalance_check(osm_opensm_t * p_osm,
				osm_switch_t * p_sw, FILE * out, int verbose)
{
	uint8_t port_num;
	uint8_t num_ports;
	const cl_qmap_t *p_port_tbl;
	osm_port_t *p_port;
	osm_physp_t *p_physp;
	osm_physp_t *p_rem_physp;
	osm_node_t *p_rem_node;
	uint32_t count[255];	/* max ports is a uint8_t */
	uint8_t output_ports[255];
	uint8_t output_ports_count = 0;
	uint32_t min_count = 0xFFFFFFFF;
	uint32_t max_count = 0;
	unsigned int i;

	memset(count, '\0', sizeof(uint32_t) * 255);

	/* Count port usage */
	p_port_tbl = &p_osm->subn.port_guid_tbl;
	for (p_port = (osm_port_t *) cl_qmap_head(p_port_tbl);
	     p_port != (osm_port_t *) cl_qmap_end(p_port_tbl);
	     p_port = (osm_port_t *) cl_qmap_next(&p_port->map_item)) {
		uint16_t min_lid_ho;
		uint16_t max_lid_ho;
		uint16_t lid_ho;

		/* Don't count switches in port usage */
		if (osm_node_get_type(p_port->p_node) == IB_NODE_TYPE_SWITCH)
			continue;

		osm_port_get_lid_range_ho(p_port, &min_lid_ho, &max_lid_ho);

		if (min_lid_ho == 0 || max_lid_ho == 0)
			continue;

		for (lid_ho = min_lid_ho; lid_ho <= max_lid_ho; lid_ho++) {
			port_num = osm_switch_get_port_by_lid(p_sw, lid_ho,
							      OSM_NEW_LFT);
			if (port_num == OSM_NO_PATH)
				continue;

			count[port_num]++;
		}
	}

	num_ports = p_sw->num_ports;
	for (port_num = 1; port_num < num_ports; port_num++) {
		p_physp = osm_node_get_physp_ptr(p_sw->p_node, port_num);

		/* if port is down/unhealthy, don't consider it in
		 * min/max calculations
		 */
		if (!p_physp || !osm_physp_is_healthy(p_physp)
		    || !osm_physp_get_remote(p_physp))
			continue;

		p_rem_physp = osm_physp_get_remote(p_physp);
		p_rem_node = osm_physp_get_node_ptr(p_rem_physp);

		/* If we are directly connected to a CA/router, its not really
		 * up for balancing consideration.
		 */
		if (osm_node_get_type(p_rem_node) != IB_NODE_TYPE_SWITCH)
			continue;

		output_ports[output_ports_count] = port_num;
		output_ports_count++;

		if (count[port_num] < min_count)
			min_count = count[port_num];
		if (count[port_num] > max_count)
			max_count = count[port_num];
	}

	if (verbose || ((max_count - min_count) > 1)) {
		if ((max_count - min_count) > 1)
			fprintf(out,
				"Unbalanced Switch: 0x%016" PRIx64 " (%s)\n",
				cl_ntoh64(p_sw->p_node->node_info.node_guid),
				p_sw->p_node->print_desc);
		else
			fprintf(out,
				"Switch: 0x%016" PRIx64 " (%s)\n",
				cl_ntoh64(p_sw->p_node->node_info.node_guid),
				p_sw->p_node->print_desc);

		for (i = 0; i < output_ports_count; i++) {
			fprintf(out,
				"Port %d: %d\n",
				output_ports[i], count[output_ports[i]]);
		}
	}
}

static void switchbalance_parse(char **p_last, osm_opensm_t * p_osm, FILE * out)
{
	char *p_cmd;
	uint64_t guid = 0;
	osm_switch_t *p_sw;
	int verbose = 0;

	p_cmd = next_token(p_last);
	if (p_cmd) {
		char *p_end;

		if (strcmp(p_cmd, "verbose") == 0) {
			verbose++;
			p_cmd = next_token(p_last);
		}

		if (p_cmd) {
			guid = strtoull(p_cmd, &p_end, 0);
			if (!guid || *p_end != '\0') {
				fprintf(out, "Invalid guid specified\n");
				help_switchbalance(out, 1);
				return;
			}
		}
	}

	cl_plock_acquire(&p_osm->lock);
	if (guid) {
		p_sw = osm_get_switch_by_guid(&p_osm->subn, cl_hton64(guid));
		if (!p_sw) {
			fprintf(out, "guid not found\n");
			goto lock_exit;
		}

		switchbalance_check(p_osm, p_sw, out, verbose);
	} else {
		cl_qmap_t *p_sw_guid_tbl = &p_osm->subn.sw_guid_tbl;
		for (p_sw = (osm_switch_t *) cl_qmap_head(p_sw_guid_tbl);
		     p_sw != (osm_switch_t *) cl_qmap_end(p_sw_guid_tbl);
		     p_sw = (osm_switch_t *) cl_qmap_next(&p_sw->map_item))
			switchbalance_check(p_osm, p_sw, out, verbose);
	}
lock_exit:
	cl_plock_release(&p_osm->lock);
	return;
}

static void lidbalance_check(osm_opensm_t * p_osm,
			     osm_switch_t * p_sw, FILE * out)
{
	uint8_t port_num;
	const cl_qmap_t *p_port_tbl;
	osm_port_t *p_port;

	p_port_tbl = &p_osm->subn.port_guid_tbl;
	for (p_port = (osm_port_t *) cl_qmap_head(p_port_tbl);
	     p_port != (osm_port_t *) cl_qmap_end(p_port_tbl);
	     p_port = (osm_port_t *) cl_qmap_next(&p_port->map_item)) {
		uint32_t port_count[255];	/* max ports is a uint8_t */
		osm_node_t *rem_node[255];
		uint32_t rem_node_count;
		uint32_t rem_count[255];
		osm_physp_t *p_physp;
		osm_physp_t *p_rem_physp;
		osm_node_t *p_rem_node;
		uint32_t port_min_count = 0xFFFFFFFF;
		uint32_t port_max_count = 0;
		uint32_t rem_min_count = 0xFFFFFFFF;
		uint32_t rem_max_count = 0;
		uint16_t min_lid_ho;
		uint16_t max_lid_ho;
		uint16_t lid_ho;
		uint8_t num_ports;
		unsigned int i;

		/* we only care about non-switches */
		if (osm_node_get_type(p_port->p_node) == IB_NODE_TYPE_SWITCH)
			continue;

		osm_port_get_lid_range_ho(p_port, &min_lid_ho, &max_lid_ho);

		if (min_lid_ho == 0 || max_lid_ho == 0)
			continue;

		memset(port_count, '\0', sizeof(uint32_t) * 255);
		memset(rem_node, '\0', sizeof(osm_node_t *) * 255);
		rem_node_count = 0;
		memset(rem_count, '\0', sizeof(uint32_t) * 255);

		for (lid_ho = min_lid_ho; lid_ho <= max_lid_ho; lid_ho++) {
			boolean_t rem_node_found = FALSE;
			unsigned int indx = 0;

			port_num = osm_switch_get_port_by_lid(p_sw, lid_ho,
							      OSM_NEW_LFT);
			if (port_num == OSM_NO_PATH)
				continue;

			p_physp =
			    osm_node_get_physp_ptr(p_sw->p_node, port_num);

			/* if port is down/unhealthy, can't calculate */
			if (!p_physp || !osm_physp_is_healthy(p_physp)
			    || !osm_physp_get_remote(p_physp))
				continue;

			p_rem_physp = osm_physp_get_remote(p_physp);
			p_rem_node = osm_physp_get_node_ptr(p_rem_physp);

			/* determine if we've seen this remote node before.
			 * If not, store it.  If yes, update the counter
			 */
			for (i = 0; i < rem_node_count; i++) {
				if (rem_node[i] == p_rem_node) {
					rem_node_found = TRUE;
					indx = i;
					break;
				}
			}

			if (!rem_node_found) {
				rem_node[rem_node_count] = p_rem_node;
				rem_count[rem_node_count]++;
				indx = rem_node_count;
				rem_node_count++;
			} else
				rem_count[indx]++;

			port_count[port_num]++;
		}

		if (!rem_node_count)
			continue;

		for (i = 0; i < rem_node_count; i++) {
			if (rem_count[i] < rem_min_count)
				rem_min_count = rem_count[i];
			if (rem_count[i] > rem_max_count)
				rem_max_count = rem_count[i];
		}

		num_ports = p_sw->num_ports;
		for (i = 0; i < num_ports; i++) {
			if (!port_count[i])
				continue;
			if (port_count[i] < port_min_count)
				port_min_count = port_count[i];
			if (port_count[i] > port_max_count)
				port_max_count = port_count[i];
		}

		/* Output if this CA/router is being forwarded an unbalanced number of
		 * times to a destination.
		 */
		if ((rem_max_count - rem_min_count) > 1) {
			fprintf(out,
				"Unbalanced Remote Forwarding: Switch 0x%016"
				PRIx64 " (%s): ",
				cl_ntoh64(p_sw->p_node->node_info.node_guid),
				p_sw->p_node->print_desc);
			if (osm_node_get_type(p_port->p_node) ==
			    IB_NODE_TYPE_CA)
				fprintf(out, "CA");
			else if (osm_node_get_type(p_port->p_node) ==
				 IB_NODE_TYPE_ROUTER)
				fprintf(out, "Router");
			fprintf(out, " 0x%016" PRIx64 " (%s): ",
				cl_ntoh64(p_port->p_node->node_info.node_guid),
				p_port->p_node->print_desc);
			for (i = 0; i < rem_node_count; i++) {
				fprintf(out,
					"Dest 0x%016" PRIx64 "(%s) - %u ",
					cl_ntoh64(rem_node[i]->node_info.
						  node_guid),
					rem_node[i]->print_desc, rem_count[i]);
			}
			fprintf(out, "\n");
		}

		/* Output if this CA/router is being forwarded through a port
		 * an unbalanced number of times.
		 */
		if ((port_max_count - port_min_count) > 1) {
			fprintf(out,
				"Unbalanced Port Forwarding: Switch 0x%016"
				PRIx64 " (%s): ",
				cl_ntoh64(p_sw->p_node->node_info.node_guid),
				p_sw->p_node->print_desc);
			if (osm_node_get_type(p_port->p_node) ==
			    IB_NODE_TYPE_CA)
				fprintf(out, "CA");
			else if (osm_node_get_type(p_port->p_node) ==
				 IB_NODE_TYPE_ROUTER)
				fprintf(out, "Router");
			fprintf(out, " 0x%016" PRIx64 " (%s): ",
				cl_ntoh64(p_port->p_node->node_info.node_guid),
				p_port->p_node->print_desc);
			for (i = 0; i < num_ports; i++) {
				if (!port_count[i])
					continue;
				fprintf(out, "Port %u - %u: ", i,
					port_count[i]);
			}
			fprintf(out, "\n");
		}
	}
}

static void lidbalance_parse(char **p_last, osm_opensm_t * p_osm, FILE * out)
{
	char *p_cmd;
	uint64_t guid = 0;
	osm_switch_t *p_sw;

	p_cmd = next_token(p_last);
	if (p_cmd) {
		char *p_end;

		guid = strtoull(p_cmd, &p_end, 0);
		if (!guid || *p_end != '\0') {
			fprintf(out, "Invalid switchguid specified\n");
			help_lidbalance(out, 1);
			return;
		}
	}

	cl_plock_acquire(&p_osm->lock);
	if (guid) {
		p_sw = osm_get_switch_by_guid(&p_osm->subn, cl_hton64(guid));
		if (!p_sw) {
			fprintf(out, "switchguid not found\n");
			goto lock_exit;
		}
		lidbalance_check(p_osm, p_sw, out);
	} else {
		cl_qmap_t *p_sw_guid_tbl = &p_osm->subn.sw_guid_tbl;
		for (p_sw = (osm_switch_t *) cl_qmap_head(p_sw_guid_tbl);
		     p_sw != (osm_switch_t *) cl_qmap_end(p_sw_guid_tbl);
		     p_sw = (osm_switch_t *) cl_qmap_next(&p_sw->map_item))
			lidbalance_check(p_osm, p_sw, out);
	}

lock_exit:
	cl_plock_release(&p_osm->lock);
	return;
}

static void dump_conf_parse(char **p_last, osm_opensm_t * p_osm, FILE * out)
{
	osm_subn_output_conf(out, &p_osm->subn.opt);
}

static void update_desc_parse(char **p_last, osm_opensm_t * p_osm, FILE * out)
{
	osm_update_node_desc(p_osm);
}

#ifdef ENABLE_OSM_PERF_MGR
static monitored_node_t *find_node_by_name(osm_opensm_t * p_osm,
					   char *nodename)
{
	cl_map_item_t *item;
	monitored_node_t *node;

	item = cl_qmap_head(&p_osm->perfmgr.monitored_map);
	while (item != cl_qmap_end(&p_osm->perfmgr.monitored_map)) {
		node = (monitored_node_t *)item;
		if (strcmp(node->name, nodename) == 0)
			return node;
		item = cl_qmap_next(item);
	}

	return NULL;
}

static monitored_node_t *find_node_by_guid(osm_opensm_t * p_osm,
					   uint64_t guid)
{
	cl_map_item_t *node;

	node = cl_qmap_get(&p_osm->perfmgr.monitored_map, guid);
	if (node != cl_qmap_end(&p_osm->perfmgr.monitored_map))
		return (monitored_node_t *)node;

	return NULL;
}

static void dump_redir_entry(monitored_node_t *p_mon_node, FILE * out)
{
	int port, redir;

	/* only display monitored nodes with redirection info */
	redir = 0;
	for (port = (p_mon_node->esp0) ? 0 : 1;
	     port < p_mon_node->num_ports; port++) {
		if (p_mon_node->port[port].redirection) {
			if (!redir) {
				fprintf(out, "   Node GUID       ESP0   Name\n");
				fprintf(out, "   ---------       ----   ----\n");
				fprintf(out, "   0x%" PRIx64 " %d      %s\n",
					p_mon_node->guid, p_mon_node->esp0,
					p_mon_node->name);
				fprintf(out, "\n   Port Valid  LIDs     PKey  QP    PKey Index\n");
				fprintf(out, "   ---- -----  ----     ----  --    ----------\n");
				redir = 1;
			}
			fprintf(out, "   %d    %d      %u->%u  0x%x 0x%x   %d\n",
				port, p_mon_node->port[port].valid,
				cl_ntoh16(p_mon_node->port[port].orig_lid),
				cl_ntoh16(p_mon_node->port[port].lid),
				cl_ntoh16(p_mon_node->port[port].pkey),
				cl_ntoh32(p_mon_node->port[port].qp),
				p_mon_node->port[port].pkey_ix);
		}
	}
	if (redir)
		fprintf(out, "\n");
}

static void dump_redir(osm_opensm_t * p_osm, char *nodename, FILE * out)
{
	monitored_node_t *p_mon_node;
	uint64_t guid;

	if (!p_osm->subn.opt.perfmgr_redir)
		fprintf(out, "Perfmgr redirection not enabled\n");

	fprintf(out, "\nRedirection Table\n");
	fprintf(out, "-----------------\n");
	cl_plock_acquire(&p_osm->lock);
	if (nodename) {
		guid = strtoull(nodename, NULL, 0);
		if (guid == 0 && errno)
			p_mon_node = find_node_by_name(p_osm, nodename);
		else
			p_mon_node = find_node_by_guid(p_osm, guid);
		if (p_mon_node)
			dump_redir_entry(p_mon_node, out);
		else {
			if (guid == 0 && errno)
				fprintf(out, "Node %s not found...\n", nodename);
			else
				fprintf(out, "Node 0x%" PRIx64 " not found...\n", guid);
		}
	} else {
		p_mon_node = (monitored_node_t *) cl_qmap_head(&p_osm->perfmgr.monitored_map);
		while (p_mon_node != (monitored_node_t *) cl_qmap_end(&p_osm->perfmgr.monitored_map)) {
			dump_redir_entry(p_mon_node, out);
			p_mon_node = (monitored_node_t *) cl_qmap_next((const cl_map_item_t *)p_mon_node);
		}
	}
	cl_plock_release(&p_osm->lock);
}

static void clear_redir_entry(monitored_node_t *p_mon_node)
{
	int port;
	ib_net16_t orig_lid;

	for (port = (p_mon_node->esp0) ? 0 : 1;
	     port < p_mon_node->num_ports; port++) {
		if (p_mon_node->port[port].redirection) {
			orig_lid = p_mon_node->port[port].orig_lid;
			memset(&p_mon_node->port[port], 0,
			       sizeof(monitored_port_t));
			p_mon_node->port[port].valid = TRUE;
			p_mon_node->port[port].orig_lid = orig_lid;
		}
	}
}

static void clear_redir(osm_opensm_t * p_osm, char *nodename, FILE * out)
{
	monitored_node_t *p_mon_node;
	uint64_t guid;

	if (!p_osm->subn.opt.perfmgr_redir)
		fprintf(out, "Perfmgr redirection not enabled\n");

	cl_plock_acquire(&p_osm->lock);
	if (nodename) {
		guid = strtoull(nodename, NULL, 0);
		if (guid == 0 && errno)
			p_mon_node = find_node_by_name(p_osm, nodename);
		else
			p_mon_node = find_node_by_guid(p_osm, guid);
		if (p_mon_node)
			clear_redir_entry(p_mon_node);
		else {
			if (guid == 0 && errno)
				fprintf(out, "Node %s not found...\n", nodename);
			else
				fprintf(out, "Node 0x%" PRIx64 " not found...\n", guid);
		}
	} else {
		p_mon_node = (monitored_node_t *) cl_qmap_head(&p_osm->perfmgr.monitored_map);
		while (p_mon_node != (monitored_node_t *) cl_qmap_end(&p_osm->perfmgr.monitored_map)) {
			clear_redir_entry(p_mon_node);
			p_mon_node = (monitored_node_t *) cl_qmap_next((const cl_map_item_t *)p_mon_node);
		}
	}
	cl_plock_release(&p_osm->lock);
}

static void perfmgr_parse(char **p_last, osm_opensm_t * p_osm, FILE * out)
{
	char *p_cmd;

	p_cmd = next_token(p_last);
	if (p_cmd) {
		if (strcmp(p_cmd, "enable") == 0) {
			osm_perfmgr_set_state(&p_osm->perfmgr,
					      PERFMGR_STATE_ENABLED);
		} else if (strcmp(p_cmd, "disable") == 0) {
			osm_perfmgr_set_state(&p_osm->perfmgr,
					      PERFMGR_STATE_DISABLE);
		} else if (strcmp(p_cmd, "clear_counters") == 0) {
			osm_perfmgr_clear_counters(&p_osm->perfmgr);
		} else if (strcmp(p_cmd, "set_rm_nodes") == 0) {
			osm_perfmgr_set_rm_nodes(&p_osm->perfmgr, 1);
		} else if (strcmp(p_cmd, "clear_rm_nodes") == 0) {
			osm_perfmgr_set_rm_nodes(&p_osm->perfmgr, 0);
		} else if (strcmp(p_cmd, "set_query_cpi") == 0) {
			osm_perfmgr_set_query_cpi(&p_osm->perfmgr, 1);
		} else if (strcmp(p_cmd, "clear_query_cpi") == 0) {
			osm_perfmgr_set_query_cpi(&p_osm->perfmgr, 0);
		} else if (strcmp(p_cmd, "dump_counters") == 0) {
			p_cmd = next_token(p_last);
			if (p_cmd && (strcmp(p_cmd, "mach") == 0)) {
				osm_perfmgr_dump_counters(&p_osm->perfmgr,
							  PERFMGR_EVENT_DB_DUMP_MR);
			} else {
				osm_perfmgr_dump_counters(&p_osm->perfmgr,
							  PERFMGR_EVENT_DB_DUMP_HR);
			}
		} else if (strcmp(p_cmd, "clear_inactive") == 0) {
			unsigned cnt = osm_perfmgr_delete_inactive(&p_osm->perfmgr);
			fprintf(out, "Removed %u nodes from Database\n", cnt);
		} else if (strcmp(p_cmd, "print_counters") == 0 ||
			   strcmp(p_cmd, "pc") == 0) {
			char *port = NULL;
			p_cmd = name_token(p_last);
			if (p_cmd) {
				port = strchr(p_cmd, ':');
				if (port) {
					*port = '\0';
					port++;
				}
			}
			osm_perfmgr_print_counters(&p_osm->perfmgr, p_cmd,
						   out, port, 0);
		} else if (strcmp(p_cmd, "print_errors") == 0 ||
			   strcmp(p_cmd, "pe") == 0) {
			p_cmd = name_token(p_last);
			osm_perfmgr_print_counters(&p_osm->perfmgr, p_cmd,
						   out, NULL, 1);
		} else if (strcmp(p_cmd, "dump_redir") == 0) {
			p_cmd = name_token(p_last);
			dump_redir(p_osm, p_cmd, out);
		} else if (strcmp(p_cmd, "clear_redir") == 0) {
			p_cmd = name_token(p_last);
			clear_redir(p_osm, p_cmd, out);
		} else if (strcmp(p_cmd, "sweep_time") == 0) {
			p_cmd = next_token(p_last);
			if (p_cmd) {
				uint16_t time_s = atoi(p_cmd);
				if (time_s < 1)
					fprintf(out,
						"sweep_time requires a "
						"positive time period "
						"(in seconds) to be "
						"specified\n");
				else
					osm_perfmgr_set_sweep_time_s(
							&p_osm->perfmgr,
							time_s);
			} else {
				fprintf(out,
					"sweep_time requires a time period "
					"(in seconds) to be specified\n");
			}
		} else if (strcmp(p_cmd, "sweep") == 0) {
			osm_sm_signal(&p_osm->sm, OSM_SIGNAL_PERFMGR_SWEEP);
			fprintf(out, "sweep initiated...\n");
		} else {
			fprintf(out, "\"%s\" option not found\n", p_cmd);
		}
	} else {
		fprintf(out, "Performance Manager status:\n"
			"state                        : %s\n"
			"sweep state                  : %s\n"
			"sweep time                   : %us\n"
			"outstanding queries/max      : %d/%u\n"
			"remove missing nodes from DB : %s\n"
			"query ClassPortInfo          : %s\n",
			osm_perfmgr_get_state_str(&p_osm->perfmgr),
			osm_perfmgr_get_sweep_state_str(&p_osm->perfmgr),
			osm_perfmgr_get_sweep_time_s(&p_osm->perfmgr),
			p_osm->perfmgr.outstanding_queries,
			p_osm->perfmgr.max_outstanding_queries,
			osm_perfmgr_get_rm_nodes(&p_osm->perfmgr)
						 ? "TRUE" : "FALSE",
			osm_perfmgr_get_query_cpi(&p_osm->perfmgr)
						 ? "TRUE" : "FALSE");
	}
}
#endif				/* ENABLE_OSM_PERF_MGR */

static void quit_parse(char **p_last, osm_opensm_t * p_osm, FILE * out)
{
	cio_close(&p_osm->console, &p_osm->log);
}

static void help_version(FILE * out, int detail)
{
	fprintf(out, "version -- print the OSM version\n");
}

static void version_parse(char **p_last, osm_opensm_t * p_osm, FILE * out)
{
	fprintf(out, "%s build %s %s\n", p_osm->osm_version, __DATE__, __TIME__);
}

/* more parse routines go here */
typedef struct _regexp_list {
	regex_t exp;
	struct _regexp_list *next;
} regexp_list_t;

static void dump_portguid_parse(char **p_last, osm_opensm_t * p_osm, FILE * out)
{
	cl_qmap_t *p_alias_port_guid_tbl;
	osm_alias_guid_t *p_alias_guid, *p_next_alias_guid;
	regexp_list_t *p_regexp, *p_head_regexp = NULL;
	FILE *output = out;

	while (1) {
		char *p_cmd = next_token(p_last);
		if (!p_cmd)
			break;

		if (strcmp(p_cmd, "file") == 0) {
			p_cmd = next_token(p_last);
			if (p_cmd) {
				output = fopen(p_cmd, "w+");
				if (output == NULL) {
					fprintf(out,
						"Could not open file %s: %s\n",
						p_cmd, strerror(errno));
					output = out;
				}
			} else
				fprintf(out, "No file name passed\n");
		} else if (!(p_regexp = malloc(sizeof(*p_regexp)))) {
			fprintf(out, "No memory\n");
			break;
		} else if (regcomp(&p_regexp->exp, p_cmd,
				   REG_NOSUB | REG_EXTENDED) != 0) {
			fprintf(out, "Cannot parse regular expression \'%s\'."
				" Skipping\n", p_cmd);
			free(p_regexp);
			continue;
		} else {
			p_regexp->next = p_head_regexp;
			p_head_regexp = p_regexp;
		}
	}

	/* Check we have at least one expression to match */
	if (p_head_regexp == NULL) {
		fprintf(out, "No valid expression provided. Aborting\n");
		goto Exit;
	}

	if (p_osm->sm.p_subn->need_update != 0) {
		fprintf(out, "Subnet is not ready yet. Try again later\n");
		goto Free_and_exit;
	}

	/* Subnet doesn't need to be updated so we can carry on */

	p_alias_port_guid_tbl = &(p_osm->sm.p_subn->alias_port_guid_tbl);
	CL_PLOCK_ACQUIRE(p_osm->sm.p_lock);

	p_next_alias_guid = (osm_alias_guid_t *) cl_qmap_head(p_alias_port_guid_tbl);
	while (p_next_alias_guid != (osm_alias_guid_t *) cl_qmap_end(p_alias_port_guid_tbl)) {

		p_alias_guid = p_next_alias_guid;
		p_next_alias_guid =
		    (osm_alias_guid_t *) cl_qmap_next(&p_next_alias_guid->map_item);

		for (p_regexp = p_head_regexp; p_regexp != NULL;
		     p_regexp = p_regexp->next)
			if (regexec(&p_regexp->exp,
				    p_alias_guid->p_base_port->p_node->print_desc,
				    0, NULL, 0) == 0) {
				fprintf(output, "0x%" PRIxLEAST64 "\n",
					cl_ntoh64(p_alias_guid->alias_guid));
				break;
			}
	}

	CL_PLOCK_RELEASE(p_osm->sm.p_lock);

Free_and_exit:
	for (; p_head_regexp; p_head_regexp = p_regexp) {
		p_regexp = p_head_regexp->next;
		regfree(&p_head_regexp->exp);
		free(p_head_regexp);
	}
Exit:
	if (output != out)
		fclose(output);
}

static void help_dump_portguid(FILE * out, int detail)
{
	fprintf(out,
		"dump_portguid [file filename] regexp1 [regexp2 [regexp3 ...]] -- Dump port GUID matching a regexp \n");
	if (detail) {
		fprintf(out,
			"getguidgetguid  -- Dump all the port GUID whom node_desc matches one of the provided regexp\n");
		fprintf(out,
			"   [file filename] -- Send the port GUID list to the specified file instead of regular output\n");
	}

}

static const struct command console_cmds[] = {
	{"help", &help_command, &help_parse},
	{"quit", &help_quit, &quit_parse},
	{"loglevel", &help_loglevel, &loglevel_parse},
	{"permodlog", &help_permodlog, &permodlog_parse},
	{"priority", &help_priority, &priority_parse},
	{"resweep", &help_resweep, &resweep_parse},
	{"reroute", &help_reroute, &reroute_parse},
	{"sweep", &help_sweep, &sweep_parse},
	{"status", &help_status, &status_parse},
	{"logflush", &help_logflush, &logflush_parse},
	{"querylid", &help_querylid, &querylid_parse},
	{"portstatus", &help_portstatus, &portstatus_parse},
	{"switchbalance", &help_switchbalance, &switchbalance_parse},
	{"lidbalance", &help_lidbalance, &lidbalance_parse},
	{"dump_conf", &help_dump_conf, &dump_conf_parse},
	{"update_desc", &help_update_desc, &update_desc_parse},
	{"version", &help_version, &version_parse},
#ifdef ENABLE_OSM_PERF_MGR
	{"perfmgr", &help_perfmgr, &perfmgr_parse},
	{"pm", &help_pm, &perfmgr_parse},
#endif				/* ENABLE_OSM_PERF_MGR */
	{"dump_portguid", &help_dump_portguid, &dump_portguid_parse},
	{NULL, NULL, NULL}	/* end of array */
};

static void parse_cmd_line(char *line, osm_opensm_t * p_osm)
{
	char *p_cmd, *p_last;
	int i, found = 0;
	FILE *out = p_osm->console.out;

	while (isspace(*line))
		line++;
	if (!*line)
		return;

	/* find first token which is the command */
	p_cmd = strtok_r(line, " \t\n\r", &p_last);
	if (p_cmd) {
		for (i = 0; console_cmds[i].name; i++) {
			if (loop_command.on) {
				if (!strcmp(p_cmd, "q")) {
					loop_command.on = 0;
				}
				found = 1;
				break;
			}
			if (!strcmp(p_cmd, console_cmds[i].name)) {
				found = 1;
				console_cmds[i].parse_function(&p_last, p_osm,
							       out);
				break;
			}
		}
		if (!found) {
			fprintf(out, "%s : Command not found\n\n", p_cmd);
			help_command(out, 0);
		}
	} else {
		fprintf(out, "Error parsing command line: `%s'\n", line);
	}
	if (loop_command.on) {
		fprintf(out, "use \"q<ret>\" to quit loop\n");
		fflush(out);
	}
}

int osm_console(osm_opensm_t * p_osm)
{
	struct pollfd pollfd[2];
	char *p_line;
	size_t len;
	ssize_t n;
	struct pollfd *fds;
	nfds_t nfds;
	osm_console_t *p_oct = &p_osm->console;

	pollfd[0].fd = p_oct->socket;
	pollfd[0].events = POLLIN;
	pollfd[0].revents = 0;

	pollfd[1].fd = p_oct->in_fd;
	pollfd[1].events = POLLIN;
	pollfd[1].revents = 0;

	fds = p_oct->socket < 0 ? &pollfd[1] : pollfd;
	nfds = p_oct->socket < 0 || pollfd[1].fd < 0 ? 1 : 2;

	if (loop_command.on && loop_command_check_time() &&
	    loop_command.loop_function) {
		if (p_oct->out) {
			loop_command.loop_function(p_osm, p_oct->out);
			fflush(p_oct->out);
		} else {
			loop_command.on = 0;
		}
	}

	if (poll(fds, nfds, 1000) <= 0)
		return 0;

#ifdef ENABLE_OSM_CONSOLE_LOOPBACK
	if (pollfd[0].revents & POLLIN) {
		int new_fd = 0;
		struct sockaddr_in sin;
		socklen_t len = sizeof(sin);
		struct hostent *hent;
		if ((new_fd = accept(p_oct->socket, (struct sockaddr *)&sin, &len)) < 0) {
			OSM_LOG(&p_osm->log, OSM_LOG_ERROR,
				"ERR 4B04: Failed to accept console socket: %s\n",
				strerror(errno));
			p_oct->in_fd = -1;
			return 0;
		}
		if (inet_ntop
		    (AF_INET, &sin.sin_addr, p_oct->client_ip,
		     sizeof(p_oct->client_ip)) == NULL) {
			snprintf(p_oct->client_ip, sizeof(p_oct->client_ip),
				 "STRING_UNKNOWN");
		}
		if ((hent = gethostbyaddr((const char *)&sin.sin_addr,
					  sizeof(struct in_addr),
					  AF_INET)) == NULL) {
			snprintf(p_oct->client_hn, sizeof(p_oct->client_hn),
				 "STRING_UNKNOWN");
		} else {
			snprintf(p_oct->client_hn, sizeof(p_oct->client_hn),
				 "%s", hent->h_name);
		}
		if (is_authorized(p_oct)) {
			cio_open(p_oct, new_fd, &p_osm->log);
		} else {
			OSM_LOG(&p_osm->log, OSM_LOG_ERROR,
				"ERR 4B05: Console connection denied: %s (%s)\n",
				p_oct->client_hn, p_oct->client_ip);
			close(new_fd);
		}
		return 0;
	}
#endif

	if (pollfd[1].revents & POLLIN) {
		p_line = NULL;
		/* Get input line */
		n = getline(&p_line, &len, p_oct->in);
		if (n > 0) {
			/* Parse and act on input */
			parse_cmd_line(p_line, p_osm);
			if (!loop_command.on) {
				osm_console_prompt(p_oct->out);
			}
		} else
			cio_close(p_oct, &p_osm->log);
		if (p_line)
			free(p_line);
		return 0;
	}
	/* input fd is closed (hanged up) */
	if (pollfd[1].revents & POLLHUP) {
#ifdef ENABLE_OSM_CONSOLE_LOOPBACK
		/* If we are using a socket, we close the current connection */
		if (p_oct->socket >= 0) {
			cio_close(p_oct, &p_osm->log);
			return 0;
		}
#endif
		/* If we use a local console, stdin is closed (most probable is pipe ended)
		 * so we close the local console */
		return -1;
	}

	return 0;
}
