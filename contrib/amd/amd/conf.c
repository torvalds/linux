/*
 * Copyright (c) 1997-2014 Erez Zadok
 * Copyright (c) 1990 Jan-Simon Pendry
 * Copyright (c) 1990 Imperial College of Science, Technology & Medicine
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Jan-Simon Pendry at Imperial College, London.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *
 * File: am-utils/amd/conf.c
 *
 */

/*
 * Functions to handle the configuration file.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */
#include <am_defs.h>
#include <amd.h>


/*
 * MACROS:
 */
/* Turn on to show some info about maps being configured */
/* #define DEBUG_CONF */

/*
 * TYPEDEFS:
 */
typedef int (*OptFuncPtr)(const char *);

/*
 * STRUCTURES:
 */
struct _func_map {
  char *name;
  OptFuncPtr func;
};

/*
 * FORWARD DECLARATIONS:
 */
static int gopt_arch(const char *val);
static int gopt_auto_attrcache(const char *val);
static int gopt_auto_dir(const char *val);
static int gopt_auto_nfs_version(const char *val);
static int gopt_autofs_use_lofs(const char *val);
static int gopt_browsable_dirs(const char *val);
static int gopt_cache_duration(const char *val);
static int gopt_cluster(const char *val);
static int gopt_debug_mtab_file(const char *val);
static int gopt_debug_options(const char *val);
static int gopt_dismount_interval(const char *val);
static int gopt_domain_strip(const char *val);
static int gopt_exec_map_timeout(const char *val);
static int gopt_forced_unmounts(const char *val);
static int gopt_full_os(const char *val);
static int gopt_fully_qualified_hosts(const char *val);
static int gopt_hesiod_base(const char *val);
static int gopt_karch(const char *val);
static int gopt_ldap_base(const char *val);
static int gopt_ldap_cache_maxmem(const char *val);
static int gopt_ldap_cache_seconds(const char *val);
static int gopt_ldap_hostports(const char *val);
static int gopt_ldap_proto_version(const char *val);
static int gopt_local_domain(const char *val);
static int gopt_localhost_address(const char *val);
static int gopt_log_file(const char *val);
static int gopt_log_options(const char *val);
static int gopt_map_defaults(const char *val);
static int gopt_map_options(const char *val);
static int gopt_map_reload_interval(const char *val);
static int gopt_map_type(const char *val);
static int gopt_mount_type(const char *val);
static int gopt_pid_file(const char *val);
static int gopt_portmap_program(const char *val);
static int gopt_preferred_amq_port(const char *val);
static int gopt_nfs_allow_any_interface(const char *val);
static int gopt_nfs_allow_insecure_port(const char *val);
static int gopt_nfs_proto(const char *val);
static int gopt_nfs_retransmit_counter(const char *val);
static int gopt_nfs_retransmit_counter_udp(const char *val);
static int gopt_nfs_retransmit_counter_tcp(const char *val);
static int gopt_nfs_retransmit_counter_toplvl(const char *val);
static int gopt_nfs_retry_interval(const char *val);
static int gopt_nfs_retry_interval_udp(const char *val);
static int gopt_nfs_retry_interval_tcp(const char *val);
static int gopt_nfs_retry_interval_toplvl(const char *val);
static int gopt_nfs_vers(const char *val);
static int gopt_nfs_vers_ping(const char *val);
static int gopt_nis_domain(const char *val);
static int gopt_normalize_hostnames(const char *val);
static int gopt_normalize_slashes(const char *val);
static int gopt_os(const char *val);
static int gopt_osver(const char *val);
static int gopt_plock(const char *val);
static int gopt_print_pid(const char *val);
static int gopt_print_version(const char *val);
static int gopt_restart_mounts(const char *val);
static int gopt_search_path(const char *val);
static int gopt_selectors_in_defaults(const char *val);
static int gopt_show_statfs_entries(const char *val);
static int gopt_sun_map_syntax(const char *val);
static int gopt_truncate_log(const char *val);
static int gopt_unmount_on_exit(const char *val);
static int gopt_use_tcpwrappers(const char *val);
static int gopt_vendor(const char *val);
static int process_global_option(const char *key, const char *val);
static int process_one_regular_map(const cf_map_t *cfm);
static int process_regular_option(const char *section, const char *key, const char *val, cf_map_t *cfm);
static int ropt_browsable_dirs(const char *val, cf_map_t *cfm);
static int ropt_map_name(const char *val, cf_map_t *cfm);
static int ropt_map_defaults(const char *val, cf_map_t *cfm);
static int ropt_map_options(const char *val, cf_map_t *cfm);
static int ropt_map_type(const char *val, cf_map_t *cfm);
static int ropt_mount_type(const char *val, cf_map_t *cfm);
static int ropt_search_path(const char *val, cf_map_t *cfm);
static int ropt_sun_map_syntax(const char *val, cf_map_t *cfm);
static int ropt_tag(const char *val, cf_map_t *cfm);
static void init_cf_map(cf_map_t *cfm);


/*
 * STATIC VARIABLES:
 */
static cf_map_t *head_map, *cur_map;

static struct _func_map glob_functable[] = {
  {"arch",			gopt_arch},
  {"auto_attrcache",		gopt_auto_attrcache},
  {"auto_dir",			gopt_auto_dir},
  {"auto_nfs_version",		gopt_auto_nfs_version},
  {"autofs_use_lofs",		gopt_autofs_use_lofs},
  {"browsable_dirs",		gopt_browsable_dirs},
  {"cache_duration",		gopt_cache_duration},
  {"cluster",			gopt_cluster},
  {"debug_mtab_file",           gopt_debug_mtab_file},
  {"debug_options",		gopt_debug_options},
  {"dismount_interval",		gopt_dismount_interval},
  {"domain_strip",		gopt_domain_strip},
  {"exec_map_timeout",		gopt_exec_map_timeout},
  {"forced_unmounts",		gopt_forced_unmounts},
  {"fully_qualified_hosts",	gopt_fully_qualified_hosts},
  {"full_os",			gopt_full_os},
  {"hesiod_base",		gopt_hesiod_base},
  {"karch",			gopt_karch},
  {"ldap_base",			gopt_ldap_base},
  {"ldap_cache_maxmem",		gopt_ldap_cache_maxmem},
  {"ldap_cache_seconds",	gopt_ldap_cache_seconds},
  {"ldap_hostports",		gopt_ldap_hostports},
  {"ldap_proto_version",	gopt_ldap_proto_version},
  {"local_domain",		gopt_local_domain},
  {"localhost_address",		gopt_localhost_address},
  {"log_file",			gopt_log_file},
  {"log_options",		gopt_log_options},
  {"map_defaults",		gopt_map_defaults},
  {"map_options",		gopt_map_options},
  {"map_reload_interval",	gopt_map_reload_interval},
  {"map_type",			gopt_map_type},
  {"mount_type",		gopt_mount_type},
  {"pid_file",			gopt_pid_file},
  {"portmap_program",		gopt_portmap_program},
  {"preferred_amq_port",	gopt_preferred_amq_port},
  {"nfs_allow_any_interface",	gopt_nfs_allow_any_interface},
  {"nfs_allow_insecure_port",	gopt_nfs_allow_insecure_port},
  {"nfs_proto",			gopt_nfs_proto},
  {"nfs_retransmit_counter",	gopt_nfs_retransmit_counter},
  {"nfs_retransmit_counter_udp",	gopt_nfs_retransmit_counter_udp},
  {"nfs_retransmit_counter_tcp",	gopt_nfs_retransmit_counter_tcp},
  {"nfs_retransmit_counter_toplvl",	gopt_nfs_retransmit_counter_toplvl},
  {"nfs_retry_interval",	gopt_nfs_retry_interval},
  {"nfs_retry_interval_udp",	gopt_nfs_retry_interval_udp},
  {"nfs_retry_interval_tcp",	gopt_nfs_retry_interval_tcp},
  {"nfs_retry_interval_toplvl",	gopt_nfs_retry_interval_toplvl},
  {"nfs_vers",			gopt_nfs_vers},
  {"nfs_vers_ping",		gopt_nfs_vers_ping},
  {"nis_domain",		gopt_nis_domain},
  {"normalize_hostnames",	gopt_normalize_hostnames},
  {"normalize_slashes",		gopt_normalize_slashes},
  {"os",			gopt_os},
  {"osver",			gopt_osver},
  {"plock",			gopt_plock},
  {"print_pid",			gopt_print_pid},
  {"print_version",		gopt_print_version},
  {"restart_mounts",		gopt_restart_mounts},
  {"search_path",		gopt_search_path},
  {"selectors_on_default",	gopt_selectors_in_defaults},
  {"selectors_in_defaults",	gopt_selectors_in_defaults},
  {"show_statfs_entries",	gopt_show_statfs_entries},
  {"sun_map_syntax",		gopt_sun_map_syntax},
  {"truncate_log",		gopt_truncate_log},
  {"unmount_on_exit",		gopt_unmount_on_exit},
  {"use_tcpwrappers",		gopt_use_tcpwrappers},
  {"vendor",			gopt_vendor},
  {NULL, NULL}
};


/*
 * Initialize a map from [global] defaults.
 */
static void
init_cf_map(cf_map_t *cfm)
{
  if (!cfm)
    return;

  /*
   * Initialize a regular map's flags and other variables from the
   * global ones, so that they are applied to all maps.  Of course, each map
   * can then override the flags individually.
   *
   * NOTES:
   * (1): Will only work for maps that appear after [global].
   * (2): I'm assigning pointers directly from the global map.
   */

  /* initialize map_type from [global] */
  cfm->cfm_type = gopt.map_type;

  /* initialize map_defaults from [global] */
  cfm->cfm_defaults = gopt.map_defaults;

  /* initialize map_opts from [global] */
  cfm->cfm_opts = gopt.map_options;

  /* initialize search_path from [global] */
  cfm->cfm_search_path = gopt.search_path;

  /*
   * Initialize flags that are common both to [global] and a local map
   * (that is, they could be inherited from the global section).
   */
  cfm->cfm_flags = gopt.flags & (CFM_BROWSABLE_DIRS |
				 CFM_BROWSABLE_DIRS_FULL |
				 CFM_MOUNT_TYPE_AUTOFS |
				 CFM_SELECTORS_IN_DEFAULTS |
				 CFM_SUN_MAP_SYNTAX );
}


/*
 * Process configuration file options (called from YACC parser).
 * Return 0 if OK, 1 otherwise.
 */
int
set_conf_kv(const char *section, const char *key, const char *val)
{
  int ret;

#ifdef DEBUG_CONF
  fprintf(stderr, "set_conf_kv: section=%s, key=%s, val=%s\n",
	  section, key, val);
#endif /* DEBUG_CONF */

  /*
   * If global section, process kv pairs one at a time.
   */
  if (STREQ(section, "global")) {
    /*
     * Check if a regular map was configured before "global",
     * and warn about it.
     */
    if (cur_map && cur_map->cfm_dir) {
      static short printed_this_error;
      if (!printed_this_error) {
	fprintf(stderr, "found regular map \"%s\" before global one.\n",
		cur_map->cfm_dir);
	printed_this_error = 1;
      }
    }

    /* process the global option first */
    ret = process_global_option(key, val);

    /* return status from the processing of the global option */
    return ret;
  }

  /*
   * Otherwise we found a non-global option: store it after some testing.
   */

  /* initialize (static) global list head and current map pointer */
  if (!head_map && !cur_map) {
    cur_map = CALLOC(cf_map_t);
    if (!cur_map) {
      perror("calloc");
      exit(1);
    }
    /* initialize first head map from global defaults */
    init_cf_map(cur_map);
    head_map = cur_map;
  }

  /* check if we found a new map, then allocate and initialize it */
  if (cur_map->cfm_dir && !STREQ(cur_map->cfm_dir, section)) {
    /* allocate new map struct */
    cf_map_t *tmp_map = CALLOC(cf_map_t);
    if (!tmp_map) {
      perror("calloc");
      exit(1);
    }
    /* initialize it from global defaults */
    init_cf_map(tmp_map);
    /* append it to end of linked list */
    cur_map->cfm_next = tmp_map;
    cur_map = tmp_map;
  }

  /* now process a single entry of a regular map */
  return process_regular_option(section, key, val, cur_map);
}


/*
 * Process global section of configuration file options.
 * Return 0 upon success, 1 otherwise.
 */
static int
process_global_option(const char *key, const char *val)
{
  struct _func_map *gfp;

  /* ensure that val is valid */
  if (!val || val[0] == '\0')
    return 1;

  /*
   * search for global function.
   */
  for (gfp = glob_functable; gfp->name; gfp++)
    if (FSTREQ(gfp->name, key))
      return (gfp->func)(val);

  fprintf(stderr, "conf: unknown global key: \"%s\"\n", key);
  return 1;			/* failed to match any command */
}


static int
gopt_arch(const char *val)
{
  gopt.arch = xstrdup(val);
  return 0;
}


static int
gopt_auto_attrcache(const char *val)
{
  gopt.auto_attrcache = atoi(val);
  if (gopt.auto_attrcache < 0) {
    fprintf(stderr, "conf: bad attrcache value: \"%s\"\n", val);
    return 1;
  }
  return 0;
}


static int
gopt_auto_dir(const char *val)
{
  gopt.auto_dir = xstrdup(val);
  return 0;
}

static int
gopt_auto_nfs_version(const char *val)
{
  if (strcmp(val, "2") == 0)
    nfs_dispatcher = nfs_program_2;
  else if (strcmp(val, "3") == 0)
    nfs_dispatcher = nfs_program_3;
  else {
    fprintf(stderr, "conf: bad auto nfs version : \"%s\"\n", val);
    return 1;
  }
  return 0;
}

static int
gopt_autofs_use_lofs(const char *val)
{
  if (STREQ(val, "yes")) {
    gopt.flags |= CFM_AUTOFS_USE_LOFS;
    return 0;
  } else if (STREQ(val, "no")) {
    gopt.flags &= ~CFM_AUTOFS_USE_LOFS;
    return 0;
  }

  fprintf(stderr, "conf: unknown value to autofs_use_lofs \"%s\"\n", val);
  return 1;			/* unknown value */
}


static int
gopt_browsable_dirs(const char *val)
{
  if (STREQ(val, "full")) {
    gopt.flags |= CFM_BROWSABLE_DIRS_FULL;
    return 0;
  } else if (STREQ(val, "yes")) {
    gopt.flags |= CFM_BROWSABLE_DIRS;
    return 0;
  } else if (STREQ(val, "no")) {
    gopt.flags &= ~CFM_BROWSABLE_DIRS;
    return 0;
  }

  fprintf(stderr, "conf: unknown value to browsable_dirs \"%s\"\n", val);
  return 1;			/* unknown value */
}


static int
gopt_cache_duration(const char *val)
{
  gopt.am_timeo = atoi(val);
  if (gopt.am_timeo <= 0)
    gopt.am_timeo = AM_TTL;
  return 0;
}


static int
gopt_cluster(const char *val)
{
  gopt.cluster = xstrdup(val);
  return 0;
}


static int
gopt_debug_mtab_file(const char *val)
{
  gopt.debug_mtab_file = xstrdup(val);
  return 0;
}


static int
gopt_debug_options(const char *val)
{
#ifdef DEBUG
  usage += debug_option((char *)val);
  return 0;
#else /* not DEBUG */
  fprintf(stderr, "%s: not compiled with DEBUG option -- sorry.\n",
	  am_get_progname());
  return 1;
#endif /* not DEBUG */
}


static int
gopt_dismount_interval(const char *val)
{
  gopt.am_timeo_w = atoi(val);
  if (gopt.am_timeo_w <= 0)
    gopt.am_timeo_w = AM_TTL_W;
  return 0;
}


static int
gopt_domain_strip(const char *val)
{
  if (STREQ(val, "yes")) {
    gopt.flags |= CFM_DOMAIN_STRIP;
    return 0;
  } else if (STREQ(val, "no")) {
    gopt.flags &= ~CFM_DOMAIN_STRIP;
    return 0;
  }

  fprintf(stderr, "conf: unknown value to domain_strip \"%s\"\n", val);
  return 1;                     /* unknown value */
}


static int
gopt_exec_map_timeout(const char *val)
{
  gopt.exec_map_timeout = atoi(val);
  if (gopt.exec_map_timeout <= 0)
    gopt.exec_map_timeout = AMFS_EXEC_MAP_TIMEOUT; /* default exec map timeout */
  return 0;
}


static int
gopt_forced_unmounts(const char *val)
{
  if (STREQ(val, "yes")) {
#if !defined(MNT2_GEN_OPT_DETACH) && !defined(MNT2_GEN_OPT_FORCE)
    fprintf(stderr, "conf: forced_unmounts unsupported on this system.\n");
    return 1;
#else /* defined(MNT2_GEN_OPT_DETACH) || defined(MNT2_GEN_OPT_FORCE) */
# ifdef __linux__
    /*
     * HACK ALERT: Linux has had MNT_FORCE since 2.2, but it hasn't gotten
     * stable until 2.4.  And it had MNT_DETACH since 2.4, but it hasn't
     * gotten stable since 2.6.  So alert users if they're trying to use a
     * feature that may not work well on their older kernel.
     */
    {
      struct utsname un;
      if (uname(&un) >= 0) {
#  ifdef MNT2_GEN_OPT_FORCE
	if (strcmp(un.release, "2.4.0") < 0)
	  fprintf(stderr, "warning: forced-unmounts (MNT_FORCE) may not work well before 2.4.0\n");
#  endif /* MNT2_GEN_OPT_FORCE */
#  ifdef MNT2_GEN_OPT_DETACH
	if (strcmp(un.release, "2.6.0") < 0)
	  fprintf(stderr, "warning: lazy-unmounts (MNT_DETACH) may not work well before 2.6.0\n");
#  endif /* MNT2_GEN_OPT_DETACH */
      }
    }
# endif /* __linux__ */
    gopt.flags |= CFM_FORCED_UNMOUNTS;
    return 0;
#endif /* defined(MNT2_GEN_OPT_DETACH) || defined(MNT2_GEN_OPT_FORCE) */
  } else if (STREQ(val, "no")) {
    gopt.flags &= ~CFM_FORCED_UNMOUNTS;
    return 0;
  }

  fprintf(stderr, "conf: unknown value to unmount_on_exit \"%s\"\n", val);
  return 1;			/* unknown value */
}


static int
gopt_full_os(const char *val)
{
  gopt.op_sys_full = xstrdup(val);
  return 0;
}


static int
gopt_fully_qualified_hosts(const char *val)
{
  if (STREQ(val, "yes")) {
    gopt.flags |= CFM_FULLY_QUALIFIED_HOSTS;
    return 0;
  } else if (STREQ(val, "no")) {
    gopt.flags &= ~CFM_FULLY_QUALIFIED_HOSTS;
    return 0;
  }

  fprintf(stderr, "conf: unknown value to fully_qualified_hosts \"%s\"\n", val);
  return 1;			/* unknown value */
}


static int
gopt_hesiod_base(const char *val)
{
#ifdef HAVE_MAP_HESIOD
  gopt.hesiod_base = xstrdup(val);
  return 0;
#else /* not HAVE_MAP_HESIOD */
  fprintf(stderr, "conf: hesiod_base option ignored.  No Hesiod support available.\n");
  return 1;
#endif /* not HAVE_MAP_HESIOD */
}


static int
gopt_karch(const char *val)
{
  gopt.karch = xstrdup(val);
  return 0;
}


static int
gopt_pid_file(const char *val)
{
  gopt.pid_file = xstrdup(val);
  return 0;
}


static int
gopt_local_domain(const char *val)
{
  gopt.sub_domain = xstrdup(val);
  return 0;
}


static int
gopt_localhost_address(const char *val)
{
  gopt.localhost_address = xstrdup(val);
  return 0;
}


static int
gopt_ldap_base(const char *val)
{
#ifdef HAVE_MAP_LDAP
  gopt.ldap_base = xstrdup(val);
  return 0;
#else /* not HAVE_MAP_LDAP */
  fprintf(stderr, "conf: ldap_base option ignored.  No LDAP support available.\n");
  return 1;
#endif /* not HAVE_MAP_LDAP */
}


static int
gopt_ldap_cache_seconds(const char *val)
{
#ifdef HAVE_MAP_LDAP
  char *end;

  gopt.ldap_cache_seconds = strtol((char *)val, &end, 10);
  if (end == val) {
    fprintf(stderr, "conf: bad LDAP cache (seconds) option: %s\n",val);
    return 1;
  }
  return 0;
#else /* not HAVE_MAP_LDAP */
  fprintf(stderr, "conf: ldap_cache_seconds option ignored.  No LDAP support available.\n");
  return 1;
#endif /* not HAVE_MAP_LDAP */
}


static int
gopt_ldap_cache_maxmem(const char *val)
{
#ifdef HAVE_MAP_LDAP
  char *end;

  gopt.ldap_cache_maxmem = strtol((char *)val, &end, 10);
  if (end == val) {
    fprintf(stderr, "conf: bad LDAP cache (maxmem) option: %s\n",val);
    return 1;
  }
  return 0;
#else /* not HAVE_MAP_LDAP */
  fprintf(stderr, "conf: ldap_cache_maxmem option ignored.  No LDAP support available.\n");
  return 1;
#endif /* not HAVE_MAP_LDAP */
}


static int
gopt_ldap_hostports(const char *val)
{
#ifdef HAVE_MAP_LDAP
  gopt.ldap_hostports = xstrdup(val);
  return 0;
#else /* not HAVE_MAP_LDAP */
  fprintf(stderr, "conf: ldap_hostports option ignored.  No LDAP support available.\n");
  return 1;
#endif /* not HAVE_MAP_LDAP */

}


static int
gopt_ldap_proto_version(const char *val)
{
#ifdef HAVE_MAP_LDAP
  char *end;

  gopt.ldap_proto_version = strtol((char *)val, &end, 10);
  if (end == val) {
    fprintf(stderr, "conf: bad ldap_proto_version option: %s\n",val);
    return 1;
  }

  if (gopt.ldap_proto_version < 0 || gopt.ldap_proto_version > LDAP_VERSION_MAX) {
    fprintf(stderr, "conf: bad ldap_proto_version option value: %s\n",val);
    return 1;
  }
  switch (gopt.ldap_proto_version) {
    /* XXX: what about LDAP_VERSION1? */
  case LDAP_VERSION2:
#ifdef LDAP_VERSION3
  case LDAP_VERSION3:
#endif /* LDAP_VERSION3 */
#ifdef LDAP_VERSION4
  case LDAP_VERSION4:
#endif /* LDAP_VERSION4 */
    break;
  default:
    fprintf(stderr, "conf: unsupported ldap_proto_version option value: %s\n",val);
    return 1;
  }
  return 0;
#else /* not HAVE_MAP_LDAP */
  fprintf(stderr, "conf: ldap_proto_version option ignored.  No LDAP support available.\n");
  return 1;
#endif /* not HAVE_MAP_LDAP */
}


static int
gopt_log_file(const char *val)
{
  gopt.logfile = xstrdup(val);
  return 0;
}


static int
gopt_log_options(const char *val)
{
  usage += switch_option((char *)val);
  return 0;
}


static int
gopt_map_defaults(const char *val)
{
  gopt.map_defaults = xstrdup(val);
  return 0;
}


static int
gopt_map_options(const char *val)
{
  gopt.map_options = xstrdup(val);
  return 0;
}


static int
gopt_map_reload_interval(const char *val)
{
  gopt.map_reload_interval = atoi(val);
  if (gopt.map_reload_interval <= 0)
    gopt.map_reload_interval = ONE_HOUR;
  return 0;
}


static int
gopt_map_type(const char *val)
{
  /* check if map type exist */
  if (!mapc_type_exists(val)) {
    fprintf(stderr, "conf: no such map type \"%s\"\n", val);
    return 1;
  }
  gopt.map_type = xstrdup(val);
  return 0;
}


static int
gopt_mount_type(const char *val)
{
  if (STREQ(val, "autofs")) {
#ifdef HAVE_FS_AUTOFS
    gopt.flags |= CFM_MOUNT_TYPE_AUTOFS;
    amd_use_autofs++;
    return 0;
#else /* not HAVE_FS_AUTOFS */
    fprintf(stderr, "conf: no autofs support available\n");
    return 1;
#endif /* not HAVE_FS_AUTOFS */
  } else if (STREQ(val, "nfs")) {
    gopt.flags &= ~CFM_MOUNT_TYPE_AUTOFS;
    return 0;
  }

  fprintf(stderr, "conf: unknown value to mount_type \"%s\"\n", val);
  return 1;			/* unknown value */
}


static int
gopt_portmap_program(const char *val)
{
  gopt.portmap_program = atol(val);
  /*
   * allow alternate program numbers to be no more than 10 offset from
   * official amd program number (300019).
   */
  if (gopt.portmap_program < AMQ_PROGRAM ||
      gopt.portmap_program > AMQ_PROGRAM + 10) {
    gopt.portmap_program = AMQ_PROGRAM;
    set_amd_program_number(gopt.portmap_program);
    fprintf(stderr, "conf: illegal amd program number \"%s\"\n", val);
    return 1;
  }

  set_amd_program_number(gopt.portmap_program);
  return 0;			/* all is OK */
}


static int
gopt_preferred_amq_port(const char *val)
{
  gopt.preferred_amq_port = atoi(val);

  /*
   * No need to check value: preferred_amq_port is an unsigned short and 0
   * is a valid number, meaning "any port".
   */
  return 0;			/* all is OK */
}


static int
gopt_nfs_allow_any_interface(const char *val)
{
  if (STREQ(val, "yes")) {
    gopt.flags |= CFM_NFS_ANY_INTERFACE;
    return 0;
  } else if (STREQ(val, "no")) {
    gopt.flags &= ~CFM_NFS_ANY_INTERFACE;
    return 0;
  }

  fprintf(stderr, "conf: unknown value to nfs_allow_insecure_port \"%s\"\n", val);
  return 1;			/* unknown value */
}


static int
gopt_nfs_allow_insecure_port(const char *val)
{
  if (STREQ(val, "yes")) {
    gopt.flags |= CFM_NFS_INSECURE_PORT;
    return 0;
  } else if (STREQ(val, "no")) {
    gopt.flags &= ~CFM_NFS_INSECURE_PORT;
    return 0;
  }

  fprintf(stderr, "conf: unknown value to nfs_allow_insecure_port \"%s\"\n", val);
  return 1;			/* unknown value */
}


static int
gopt_nfs_proto(const char *val)
{
  if (STREQ(val, "udp") || STREQ(val, "tcp")) {
    gopt.nfs_proto = xstrdup(val);
    return 0;
  }
  fprintf(stderr, "conf: illegal nfs_proto \"%s\"\n", val);
  return 1;
}


static int
gopt_nfs_retransmit_counter(const char *val)
{
  int i;

  for (i=0; i<AMU_TYPE_MAX; ++i)
    gopt.amfs_auto_retrans[i] = atoi(val);
  return 0;
}


static int
gopt_nfs_retransmit_counter_udp(const char *val)
{
  gopt.amfs_auto_retrans[AMU_TYPE_UDP] = atoi(val);
  return 0;
}


static int
gopt_nfs_retransmit_counter_tcp(const char *val)
{
  gopt.amfs_auto_retrans[AMU_TYPE_TCP] = atoi(val);
  return 0;
}


static int
gopt_nfs_retransmit_counter_toplvl(const char *val)
{
  gopt.amfs_auto_retrans[AMU_TYPE_TOPLVL] = atoi(val);
  return 0;
}


static int
gopt_nfs_retry_interval(const char *val)
{
  int i;

  for (i=0; i<AMU_TYPE_MAX; ++i)
    gopt.amfs_auto_timeo[i] = atoi(val);
  return 0;
}


static int
gopt_nfs_retry_interval_udp(const char *val)
{
  gopt.amfs_auto_timeo[AMU_TYPE_UDP] = atoi(val);
  return 0;
}


static int
gopt_nfs_retry_interval_tcp(const char *val)
{
  gopt.amfs_auto_timeo[AMU_TYPE_TCP] = atoi(val);
  return 0;
}


static int
gopt_nfs_retry_interval_toplvl(const char *val)
{
  gopt.amfs_auto_timeo[AMU_TYPE_TOPLVL] = atoi(val);
  return 0;
}


static int
gopt_nfs_vers(const char *val)
{
  int i = atoi(val);

  if (i == 2 || i == 3 || i == 4) {
    gopt.nfs_vers = i;
    return 0;
  }
  fprintf(stderr, "conf: illegal nfs_vers \"%s\"\n", val);
  return 1;
}


static int
gopt_nfs_vers_ping(const char *val)
{
  int i = atoi(val);

  if (i == 2 || i == 3 || i == 4) {
    gopt.nfs_vers_ping = i;
    return 0;
  }
  fprintf(stderr, "conf: illegal nfs_vers_ping \"%s\"\n", val);
  return 1;
}

static int
gopt_nis_domain(const char *val)
{
#ifdef HAVE_MAP_NIS
  gopt.nis_domain = xstrdup(val);
  return 0;
#else /* not HAVE_MAP_NIS */
  fprintf(stderr, "conf: nis_domain option ignored.  No NIS support available.\n");
  return 1;
#endif /* not HAVE_MAP_NIS */
}


static int
gopt_normalize_hostnames(const char *val)
{
  if (STREQ(val, "yes")) {
    gopt.flags |= CFM_NORMALIZE_HOSTNAMES;
    return 0;
  } else if (STREQ(val, "no")) {
    gopt.flags &= ~CFM_NORMALIZE_HOSTNAMES;
    return 0;
  }

  fprintf(stderr, "conf: unknown value to normalize_hostnames \"%s\"\n", val);
  return 1;			/* unknown value */
}


static int
gopt_normalize_slashes(const char *val)
{
  if (STREQ(val, "yes")) {
    gopt.flags |= CFM_NORMALIZE_SLASHES;
    return 0;
  } else if (STREQ(val, "no")) {
    gopt.flags &= ~CFM_NORMALIZE_SLASHES;
    return 0;
  }

  fprintf(stderr, "conf: unknown value to normalize_slashes \"%s\"\n", val);
  return 1;			/* unknown value */
}


static int
gopt_os(const char *val)
{
  gopt.op_sys = xstrdup(val);
  return 0;
}


static int
gopt_osver(const char *val)
{
  gopt.op_sys_ver = xstrdup(val);
  return 0;
}


static int
gopt_plock(const char *val)
{
  if (STREQ(val, "yes")) {
    gopt.flags |= CFM_PROCESS_LOCK;
    return 0;
  } else if (STREQ(val, "no")) {
    gopt.flags &= ~CFM_PROCESS_LOCK;
    return 0;
  }

  fprintf(stderr, "conf: unknown value to plock \"%s\"\n", val);
  return 1;			/* unknown value */
}


static int
gopt_print_pid(const char *val)
{
  if (STREQ(val, "yes")) {
    gopt.flags |= CFM_PRINT_PID;
    return 0;
  } else if (STREQ(val, "no")) {
    gopt.flags &= ~CFM_PRINT_PID;
    return 0;
  }

  fprintf(stderr, "conf: unknown value to print_pid \"%s\"\n", val);
  return 1;			/* unknown value */
}


static int
gopt_print_version(const char *val)
{
  if (STREQ(val, "yes")) {
    char *vers = get_version_string();
    fputs(vers, stderr);
    XFREE(vers);
    return 0;
  } else if (STREQ(val, "no")) {
    return 0;
  }

  fprintf(stderr, "conf: unknown value to print_version \"%s\"\n", val);
  return 1;			/* unknown value */
}


static int
gopt_restart_mounts(const char *val)
{
  if (STREQ(val, "yes")) {
    gopt.flags |= CFM_RESTART_EXISTING_MOUNTS;
    return 0;
  } else if (STREQ(val, "no")) {
    gopt.flags &= ~CFM_RESTART_EXISTING_MOUNTS;
    return 0;
  }

  fprintf(stderr, "conf: unknown value to restart_mounts \"%s\"\n", val);
  return 1;			/* unknown value */
}


static int
gopt_search_path(const char *val)
{
  gopt.search_path = xstrdup(val);
  return 0;
}


static int
gopt_selectors_in_defaults(const char *val)
{
  if (STREQ(val, "yes")) {
    gopt.flags |= CFM_SELECTORS_IN_DEFAULTS;
    return 0;
  } else if (STREQ(val, "no")) {
    gopt.flags &= ~CFM_SELECTORS_IN_DEFAULTS;
    return 0;
  }

  fprintf(stderr, "conf: unknown value to enable_default_selectors \"%s\"\n", val);
  return 1;			/* unknown value */
}


static int
gopt_show_statfs_entries(const char *val)
{
  if (STREQ(val, "yes")) {
    gopt.flags |= CFM_SHOW_STATFS_ENTRIES;
    return 0;
  } else if (STREQ(val, "no")) {
    gopt.flags &= ~CFM_SHOW_STATFS_ENTRIES;
    return 0;
  }

  fprintf(stderr, "conf: unknown value to show_statfs_entries \"%s\"\n", val);
  return 1;			/* unknown value */
}


static int
gopt_sun_map_syntax(const char *val)
{
  if (STREQ(val, "yes")) {
    gopt.flags |= CFM_SUN_MAP_SYNTAX;
    return 0;
  } else if (STREQ(val, "no")) {
    gopt.flags &= ~CFM_SUN_MAP_SYNTAX;
    return 0;
  }

  fprintf(stderr, "conf: unknown value to sun_map_syntax \"%s\"\n", val);
  return 1;			/* unknown value */
}


static int
gopt_truncate_log(const char *val)
{
  if (STREQ(val, "yes")) {
    gopt.flags |= CFM_TRUNCATE_LOG;
    return 0;
  } else if (STREQ(val, "no")) {
    gopt.flags &= ~CFM_TRUNCATE_LOG;
    return 0;
  }

  fprintf(stderr, "conf: unknown value to truncate_log \"%s\"\n", val);
  return 1;			/* unknown value */
}


static int
gopt_unmount_on_exit(const char *val)
{
  if (STREQ(val, "yes")) {
    gopt.flags |= CFM_UNMOUNT_ON_EXIT;
    return 0;
  } else if (STREQ(val, "no")) {
    gopt.flags &= ~CFM_UNMOUNT_ON_EXIT;
    return 0;
  }

  fprintf(stderr, "conf: unknown value to unmount_on_exit \"%s\"\n", val);
  return 1;			/* unknown value */
}


static int
gopt_use_tcpwrappers(const char *val)
{
#if defined(HAVE_TCPD_H) && defined(HAVE_LIBWRAP)
  if (STREQ(val, "yes")) {
    gopt.flags |= CFM_USE_TCPWRAPPERS;
    return 0;
  } else if (STREQ(val, "no")) {
    gopt.flags &= ~CFM_USE_TCPWRAPPERS;
    return 0;
  }
#else /* not defined(HAVE_TCPD_H) && defined(HAVE_LIBWRAP) */
    fprintf(stderr, "conf: no tcpd/libwrap support available\n");
    return 1;
#endif /* not defined(HAVE_TCPD_H) && defined(HAVE_LIBWRAP) */

  fprintf(stderr, "conf: unknown value to use_tcpwrappers \"%s\"\n", val);
  return 1;			/* unknown value */
}


static int
gopt_vendor(const char *val)
{
  gopt.op_sys_vendor = xstrdup(val);
  return 0;
}


/*
 * Collect one entry for a regular map
 */
static int
process_regular_option(const char *section, const char *key, const char *val, cf_map_t *cfm)
{
  /* ensure that val is valid */
  if (!section || section[0] == '\0' ||
      !key || key[0] == '\0' ||
      !val || val[0] == '\0' ||
      !cfm) {
    fprintf(stderr, "conf: process_regular_option: null entries\n");
    return 1;
  }

  /* check if initializing a new map */
  if (!cfm->cfm_dir)
    cfm->cfm_dir = xstrdup(section);

  /* check for each possible field */
  if (STREQ(key, "browsable_dirs"))
    return ropt_browsable_dirs(val, cfm);

  if (STREQ(key, "map_name"))
    return ropt_map_name(val, cfm);

  if (STREQ(key, "map_defaults"))
    return ropt_map_defaults(val, cfm);

  if (STREQ(key, "map_options"))
    return ropt_map_options(val, cfm);

  if (STREQ(key, "map_type"))
    return ropt_map_type(val, cfm);

  if (STREQ(key, "mount_type"))
    return ropt_mount_type(val, cfm);

  if (STREQ(key, "search_path"))
    return ropt_search_path(val, cfm);

  if (STREQ(key, "sun_map_syntax"))
    return ropt_sun_map_syntax(val, cfm);

  if (STREQ(key, "tag"))
    return ropt_tag(val, cfm);

  fprintf(stderr, "conf: unknown regular key \"%s\" for section \"%s\"\n",
	  key, section);
  return 1;			/* failed to match any command */
}


static int
ropt_browsable_dirs(const char *val, cf_map_t *cfm)
{
  if (STREQ(val, "full")) {
    cfm->cfm_flags |= CFM_BROWSABLE_DIRS_FULL;
    return 0;
  } else if (STREQ(val, "yes")) {
    cfm->cfm_flags |= CFM_BROWSABLE_DIRS;
    return 0;
  } else if (STREQ(val, "no")) {
    cfm->cfm_flags &= ~CFM_BROWSABLE_DIRS;
    return 0;
  }

  fprintf(stderr, "conf: unknown value to browsable_dirs \"%s\"\n", val);
  return 1;			/* unknown value */
}


static int
ropt_map_name(const char *val, cf_map_t *cfm)
{
  cfm->cfm_name = xstrdup(val);
  return 0;
}


static int
ropt_map_defaults(const char *val, cf_map_t *cfm)
{
  cfm->cfm_defaults = xstrdup(val);
  return 0;
}


static int
ropt_map_options(const char *val, cf_map_t *cfm)
{
  cfm->cfm_opts = xstrdup(val);
  return 0;
}


static int
ropt_map_type(const char *val, cf_map_t *cfm)
{
  /* check if map type exist */
  if (!mapc_type_exists(val)) {
    fprintf(stderr, "conf: no such map type \"%s\"\n", val);
    return 1;
  }
  cfm->cfm_type = xstrdup(val);
  return 0;
}


static int
ropt_mount_type(const char *val, cf_map_t *cfm)
{
  if (STREQ(val, "autofs")) {
#ifdef HAVE_FS_AUTOFS
    cfm->cfm_flags |= CFM_MOUNT_TYPE_AUTOFS;
    amd_use_autofs++;
    return 0;
#else /* not HAVE_FS_AUTOFS */
    fprintf(stderr, "conf: no autofs support available\n");
    return 1;
#endif /* not HAVE_FS_AUTOFS */
  } else if (STREQ(val, "nfs")) {
    cfm->cfm_flags &= ~CFM_MOUNT_TYPE_AUTOFS;
    return 0;
  }

  fprintf(stderr, "conf: unknown value to mount_type \"%s\"\n", val);
  return 1;			/* unknown value */
}


static int
ropt_search_path(const char *val, cf_map_t *cfm)
{
  cfm->cfm_search_path = xstrdup(val);
  return 0;
}


static int
ropt_sun_map_syntax(const char *val, cf_map_t *cfm)
{
  if (STREQ(val, "yes")) {
    cfm->cfm_flags |= CFM_SUN_MAP_SYNTAX;
    return 0;

  } else if (STREQ(val, "no")) {
    cfm->cfm_flags &= ~CFM_SUN_MAP_SYNTAX;
    return 0;
  }

  fprintf(stderr, "conf: unknown value to sun_map_syntax \"%s\"\n", val);
  return 1;			/* unknown value */
}


static int
ropt_tag(const char *val, cf_map_t *cfm)
{
  cfm->cfm_tag = xstrdup(val);
  return 0;
}


/*
 * Process one collected map.
 */
static int
process_one_regular_map(const cf_map_t *cfm)
{
  if (!cfm->cfm_name) {
    fprintf(stderr, "conf: map_name must be defined for map \"%s\"\n", cfm->cfm_dir);
    return 1;
  }
  /*
   * If map has no tag defined, process the map.
   * If no conf_tag was set in amd -T, process all untagged entries.
   * If a tag is defined, then process it only if it matches the map tag.
   */
  if (!cfm->cfm_tag ||
      (conf_tag && STREQ(cfm->cfm_tag, conf_tag))) {
#ifdef DEBUG_CONF
    fprintf(stderr, "processing map %s (flags=0x%x)...\n",
	    cfm->cfm_dir, cfm->cfm_flags);
#endif /* DEBUG_CONF */
    root_newmap(cfm->cfm_dir,
		cfm->cfm_opts ? cfm->cfm_opts : "",
		cfm->cfm_name,
		cfm);
  } else {
    fprintf(stderr, "skipping map %s...\n", cfm->cfm_dir);
  }

  return 0;
}


/*
 * Process all regular maps in conf file (if any)
 */
int
process_all_regular_maps(void)
{
  cf_map_t *tmp_map = head_map;

  /*
   * If the amd.conf file only has a [global] section (pretty useless
   * IMHO), there's nothing to process
   */
  if (!tmp_map)
    return 0;

  while (tmp_map) {
    if (process_one_regular_map(tmp_map) != 0)
      return 1;
    tmp_map = tmp_map->cfm_next;
  }
  return 0;
}


/*
 * Find a cf_map_t for a given map name.
 * Return NULL if not found.
 */
cf_map_t *
find_cf_map(const char *name)
{

  cf_map_t *tmp_map = head_map;

  if (!tmp_map || !name)
    return NULL;

  while (tmp_map) {
    if (STREQ(tmp_map->cfm_dir, name)) {
      return tmp_map;
    }
    tmp_map = tmp_map->cfm_next;
  }
  return NULL;
}
