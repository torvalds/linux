/* vi: set sw=4 ts=4: */
/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * Authors: Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 */
#include "libbb.h"
#include "rt_names.h"

#define CONFDIR          CONFIG_FEATURE_IP_ROUTE_DIR

typedef struct rtnl_tab_t {
	const char *cached_str;
	unsigned cached_result;
	/* upstream version switched to a hash table and removed
	 * id < 256 limit. For now bbox bumps this array size from 256
	 * to 1024. If you plan to change this to a hash table,
	 * consider merging several hash tables we have (for example,
	 * awk has resizable one!
	 */
#define RT_TABLE_MAX 1023
	const char *tab[RT_TABLE_MAX+1];
} rtnl_tab_t;

static void rtnl_tab_initialize(const char *file, const char **tab)
{
	char *token[2];
	char fullname[sizeof(CONFDIR"/rt_dsfield") + 8];
	parser_t *parser;

	sprintf(fullname, CONFDIR"/rt_%s", file);
	parser = config_open2(fullname, fopen_for_read);
	while (config_read(parser, token, 2, 2, "# \t", PARSE_NORMAL)) {
		unsigned id = bb_strtou(token[0], NULL, 0);
		if (id > RT_TABLE_MAX) {
			bb_error_msg("database %s is corrupted at line %d",
				file, parser->lineno);
			break;
		}
		tab[id] = xstrdup(token[1]);
	}
	config_close(parser);
}

static int rtnl_a2n(rtnl_tab_t *tab, uint32_t *id, const char *arg, int base)
{
	unsigned i;

	if (tab->cached_str && strcmp(tab->cached_str, arg) == 0) {
		*id = tab->cached_result;
		return 0;
	}

	for (i = 0; i <= RT_TABLE_MAX; i++) {
		if (tab->tab[i]
		 && strcmp(tab->tab[i], arg) == 0
		) {
			tab->cached_str = tab->tab[i];
			tab->cached_result = i;
			*id = i;
			return 0;
		}
	}

	i = bb_strtou(arg, NULL, base);
	if (i > RT_TABLE_MAX)
		return -1;
	*id = i;
	return 0;
}


static rtnl_tab_t *rtnl_rtprot_tab;

static void rtnl_rtprot_initialize(void)
{
	static const char *const init_tab[] = {
		"none",
		"redirect",
		"kernel",
		"boot",
		"static",
		NULL,
		NULL,
		NULL,
		"gated",
		"ra",
		"mrt",
		"zebra",
		"bird",
	};

	if (rtnl_rtprot_tab)
		return;
	rtnl_rtprot_tab = xzalloc(sizeof(*rtnl_rtprot_tab));
	memcpy(rtnl_rtprot_tab->tab, init_tab, sizeof(init_tab));
	rtnl_tab_initialize("protos", rtnl_rtprot_tab->tab);
}

#if 0 /* UNUSED */
const char* FAST_FUNC rtnl_rtprot_n2a(int id)
{
	if (id < 0 || id > RT_TABLE_MAX) {
		return itoa(id);
	}

	rtnl_rtprot_initialize();

	if (rtnl_rtprot_tab->tab[id])
		return rtnl_rtprot_tab->tab[id];
	return itoa(id);
}
#endif

int FAST_FUNC rtnl_rtprot_a2n(uint32_t *id, char *arg)
{
	rtnl_rtprot_initialize();
	return rtnl_a2n(rtnl_rtprot_tab, id, arg, 0);
}


static rtnl_tab_t *rtnl_rtscope_tab;

static void rtnl_rtscope_initialize(void)
{
	if (rtnl_rtscope_tab)
		return;
	rtnl_rtscope_tab = xzalloc(sizeof(*rtnl_rtscope_tab));
	rtnl_rtscope_tab->tab[0] = "global";
	rtnl_rtscope_tab->tab[255] = "nowhere";
	rtnl_rtscope_tab->tab[254] = "host";
	rtnl_rtscope_tab->tab[253] = "link";
	rtnl_rtscope_tab->tab[200] = "site";
	rtnl_tab_initialize("scopes", rtnl_rtscope_tab->tab);
}

const char* FAST_FUNC rtnl_rtscope_n2a(int id)
{
	if (id < 0 || id > RT_TABLE_MAX) {
		return itoa(id);
	}

	rtnl_rtscope_initialize();

	if (rtnl_rtscope_tab->tab[id])
		return rtnl_rtscope_tab->tab[id];
	return itoa(id);
}

int FAST_FUNC rtnl_rtscope_a2n(uint32_t *id, char *arg)
{
	rtnl_rtscope_initialize();
	return rtnl_a2n(rtnl_rtscope_tab, id, arg, 0);
}


static rtnl_tab_t *rtnl_rtrealm_tab;

static void rtnl_rtrealm_initialize(void)
{
	if (rtnl_rtrealm_tab) return;
	rtnl_rtrealm_tab = xzalloc(sizeof(*rtnl_rtrealm_tab));
	rtnl_rtrealm_tab->tab[0] = "unknown";
	rtnl_tab_initialize("realms", rtnl_rtrealm_tab->tab);
}

int FAST_FUNC rtnl_rtrealm_a2n(uint32_t *id, char *arg)
{
	rtnl_rtrealm_initialize();
	return rtnl_a2n(rtnl_rtrealm_tab, id, arg, 0);
}

#if ENABLE_FEATURE_IP_RULE
const char* FAST_FUNC rtnl_rtrealm_n2a(int id)
{
	if (id < 0 || id > RT_TABLE_MAX) {
		return itoa(id);
	}

	rtnl_rtrealm_initialize();

	if (rtnl_rtrealm_tab->tab[id])
		return rtnl_rtrealm_tab->tab[id];
	return itoa(id);
}
#endif


static rtnl_tab_t *rtnl_rtdsfield_tab;

static void rtnl_rtdsfield_initialize(void)
{
	if (rtnl_rtdsfield_tab) return;
	rtnl_rtdsfield_tab = xzalloc(sizeof(*rtnl_rtdsfield_tab));
	rtnl_rtdsfield_tab->tab[0] = "0";
	rtnl_tab_initialize("dsfield", rtnl_rtdsfield_tab->tab);
}

const char* FAST_FUNC rtnl_dsfield_n2a(int id)
{
	if (id < 0 || id > RT_TABLE_MAX) {
		return itoa(id);
	}

	rtnl_rtdsfield_initialize();

	if (rtnl_rtdsfield_tab->tab[id])
		return rtnl_rtdsfield_tab->tab[id];
	return itoa(id);
}

int FAST_FUNC rtnl_dsfield_a2n(uint32_t *id, char *arg)
{
	rtnl_rtdsfield_initialize();
	return rtnl_a2n(rtnl_rtdsfield_tab, id, arg, 16);
}


#if ENABLE_FEATURE_IP_RULE
static rtnl_tab_t *rtnl_rttable_tab;

static void rtnl_rttable_initialize(void)
{
	if (rtnl_rttable_tab)
		return;

	rtnl_rttable_tab = xzalloc(sizeof(*rtnl_rttable_tab));
	rtnl_rttable_tab->tab[0] = "unspec";
	rtnl_rttable_tab->tab[255] = "local";
	rtnl_rttable_tab->tab[254] = "main";
	rtnl_rttable_tab->tab[253] = "default";
	rtnl_tab_initialize("tables", rtnl_rttable_tab->tab);
}

const char* FAST_FUNC rtnl_rttable_n2a(int id)
{
	if (id < 0 || id > RT_TABLE_MAX) {
		return itoa(id);
	}

	rtnl_rttable_initialize();

	if (rtnl_rttable_tab->tab[id])
		return rtnl_rttable_tab->tab[id];
	return itoa(id);
}

int FAST_FUNC rtnl_rttable_a2n(uint32_t *id, char *arg)
{
	rtnl_rttable_initialize();
	return rtnl_a2n(rtnl_rttable_tab, id, arg, 0);
}

#endif
