/*
 * hostapd / main()
 * Copyright (c) 2002-2018, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "utils/includes.h"
#ifndef CONFIG_NATIVE_WINDOWS
#include <syslog.h>
#include <grp.h>
#endif /* CONFIG_NATIVE_WINDOWS */

#include "utils/common.h"
#include "utils/eloop.h"
#include "utils/uuid.h"
#include "crypto/random.h"
#include "crypto/tls.h"
#include "common/version.h"
#include "drivers/driver.h"
#include "eap_server/eap.h"
#include "eap_server/tncs.h"
#include "ap/hostapd.h"
#include "ap/ap_config.h"
#include "ap/ap_drv_ops.h"
#include "ap/dpp_hostapd.h"
#include "fst/fst.h"
#include "config_file.h"
#include "eap_register.h"
#include "ctrl_iface.h"


struct hapd_global {
	void **drv_priv;
	size_t drv_count;
};

static struct hapd_global global;


#ifndef CONFIG_NO_HOSTAPD_LOGGER
static void hostapd_logger_cb(void *ctx, const u8 *addr, unsigned int module,
			      int level, const char *txt, size_t len)
{
	struct hostapd_data *hapd = ctx;
	char *format, *module_str;
	int maxlen;
	int conf_syslog_level, conf_stdout_level;
	unsigned int conf_syslog, conf_stdout;

	maxlen = len + 100;
	format = os_malloc(maxlen);
	if (!format)
		return;

	if (hapd && hapd->conf) {
		conf_syslog_level = hapd->conf->logger_syslog_level;
		conf_stdout_level = hapd->conf->logger_stdout_level;
		conf_syslog = hapd->conf->logger_syslog;
		conf_stdout = hapd->conf->logger_stdout;
	} else {
		conf_syslog_level = conf_stdout_level = 0;
		conf_syslog = conf_stdout = (unsigned int) -1;
	}

	switch (module) {
	case HOSTAPD_MODULE_IEEE80211:
		module_str = "IEEE 802.11";
		break;
	case HOSTAPD_MODULE_IEEE8021X:
		module_str = "IEEE 802.1X";
		break;
	case HOSTAPD_MODULE_RADIUS:
		module_str = "RADIUS";
		break;
	case HOSTAPD_MODULE_WPA:
		module_str = "WPA";
		break;
	case HOSTAPD_MODULE_DRIVER:
		module_str = "DRIVER";
		break;
	case HOSTAPD_MODULE_IAPP:
		module_str = "IAPP";
		break;
	case HOSTAPD_MODULE_MLME:
		module_str = "MLME";
		break;
	default:
		module_str = NULL;
		break;
	}

	if (hapd && hapd->conf && addr)
		os_snprintf(format, maxlen, "%s: STA " MACSTR "%s%s: %s",
			    hapd->conf->iface, MAC2STR(addr),
			    module_str ? " " : "", module_str ? module_str : "",
			    txt);
	else if (hapd && hapd->conf)
		os_snprintf(format, maxlen, "%s:%s%s %s",
			    hapd->conf->iface, module_str ? " " : "",
			    module_str ? module_str : "", txt);
	else if (addr)
		os_snprintf(format, maxlen, "STA " MACSTR "%s%s: %s",
			    MAC2STR(addr), module_str ? " " : "",
			    module_str ? module_str : "", txt);
	else
		os_snprintf(format, maxlen, "%s%s%s",
			    module_str ? module_str : "",
			    module_str ? ": " : "", txt);

#ifdef CONFIG_DEBUG_SYSLOG
	if (wpa_debug_syslog)
		conf_stdout = 0;
#endif /* CONFIG_DEBUG_SYSLOG */
	if ((conf_stdout & module) && level >= conf_stdout_level) {
		wpa_debug_print_timestamp();
		wpa_printf(MSG_INFO, "%s", format);
	}

#ifndef CONFIG_NATIVE_WINDOWS
	if ((conf_syslog & module) && level >= conf_syslog_level) {
		int priority;
		switch (level) {
		case HOSTAPD_LEVEL_DEBUG_VERBOSE:
		case HOSTAPD_LEVEL_DEBUG:
			priority = LOG_DEBUG;
			break;
		case HOSTAPD_LEVEL_INFO:
			priority = LOG_INFO;
			break;
		case HOSTAPD_LEVEL_NOTICE:
			priority = LOG_NOTICE;
			break;
		case HOSTAPD_LEVEL_WARNING:
			priority = LOG_WARNING;
			break;
		default:
			priority = LOG_INFO;
			break;
		}
		syslog(priority, "%s", format);
	}
#endif /* CONFIG_NATIVE_WINDOWS */

	os_free(format);
}
#endif /* CONFIG_NO_HOSTAPD_LOGGER */


/**
 * hostapd_driver_init - Preparate driver interface
 */
static int hostapd_driver_init(struct hostapd_iface *iface)
{
	struct wpa_init_params params;
	size_t i;
	struct hostapd_data *hapd = iface->bss[0];
	struct hostapd_bss_config *conf = hapd->conf;
	u8 *b = conf->bssid;
	struct wpa_driver_capa capa;

	if (hapd->driver == NULL || hapd->driver->hapd_init == NULL) {
		wpa_printf(MSG_ERROR, "No hostapd driver wrapper available");
		return -1;
	}

	/* Initialize the driver interface */
	if (!(b[0] | b[1] | b[2] | b[3] | b[4] | b[5]))
		b = NULL;

	os_memset(&params, 0, sizeof(params));
	for (i = 0; wpa_drivers[i]; i++) {
		if (wpa_drivers[i] != hapd->driver)
			continue;

		if (global.drv_priv[i] == NULL &&
		    wpa_drivers[i]->global_init) {
			global.drv_priv[i] =
				wpa_drivers[i]->global_init(iface->interfaces);
			if (global.drv_priv[i] == NULL) {
				wpa_printf(MSG_ERROR, "Failed to initialize "
					   "driver '%s'",
					   wpa_drivers[i]->name);
				return -1;
			}
		}

		params.global_priv = global.drv_priv[i];
		break;
	}
	params.bssid = b;
	params.ifname = hapd->conf->iface;
	params.driver_params = hapd->iconf->driver_params;
	params.use_pae_group_addr = hapd->conf->use_pae_group_addr;

	params.num_bridge = hapd->iface->num_bss;
	params.bridge = os_calloc(hapd->iface->num_bss, sizeof(char *));
	if (params.bridge == NULL)
		return -1;
	for (i = 0; i < hapd->iface->num_bss; i++) {
		struct hostapd_data *bss = hapd->iface->bss[i];
		if (bss->conf->bridge[0])
			params.bridge[i] = bss->conf->bridge;
	}

	params.own_addr = hapd->own_addr;

	hapd->drv_priv = hapd->driver->hapd_init(hapd, &params);
	os_free(params.bridge);
	if (hapd->drv_priv == NULL) {
		wpa_printf(MSG_ERROR, "%s driver initialization failed.",
			   hapd->driver->name);
		hapd->driver = NULL;
		return -1;
	}

	if (hapd->driver->get_capa &&
	    hapd->driver->get_capa(hapd->drv_priv, &capa) == 0) {
		struct wowlan_triggers *triggs;

		iface->drv_flags = capa.flags;
		iface->smps_modes = capa.smps_modes;
		iface->probe_resp_offloads = capa.probe_resp_offloads;
		/*
		 * Use default extended capa values from per-radio information
		 */
		iface->extended_capa = capa.extended_capa;
		iface->extended_capa_mask = capa.extended_capa_mask;
		iface->extended_capa_len = capa.extended_capa_len;
		iface->drv_max_acl_mac_addrs = capa.max_acl_mac_addrs;

		/*
		 * Override extended capa with per-interface type (AP), if
		 * available from the driver.
		 */
		hostapd_get_ext_capa(iface);

		triggs = wpa_get_wowlan_triggers(conf->wowlan_triggers, &capa);
		if (triggs && hapd->driver->set_wowlan) {
			if (hapd->driver->set_wowlan(hapd->drv_priv, triggs))
				wpa_printf(MSG_ERROR, "set_wowlan failed");
		}
		os_free(triggs);
	}

	return 0;
}


/**
 * hostapd_interface_init - Read configuration file and init BSS data
 *
 * This function is used to parse configuration file for a full interface (one
 * or more BSSes sharing the same radio) and allocate memory for the BSS
 * interfaces. No actual driver operations are started.
 */
static struct hostapd_iface *
hostapd_interface_init(struct hapd_interfaces *interfaces, const char *if_name,
		       const char *config_fname, int debug)
{
	struct hostapd_iface *iface;
	int k;

	wpa_printf(MSG_ERROR, "Configuration file: %s", config_fname);
	iface = hostapd_init(interfaces, config_fname);
	if (!iface)
		return NULL;

	if (if_name) {
		os_strlcpy(iface->conf->bss[0]->iface, if_name,
			   sizeof(iface->conf->bss[0]->iface));
	}

	iface->interfaces = interfaces;

	for (k = 0; k < debug; k++) {
		if (iface->bss[0]->conf->logger_stdout_level > 0)
			iface->bss[0]->conf->logger_stdout_level--;
	}

	if (iface->conf->bss[0]->iface[0] == '\0' &&
	    !hostapd_drv_none(iface->bss[0])) {
		wpa_printf(MSG_ERROR,
			   "Interface name not specified in %s, nor by '-i' parameter",
			   config_fname);
		hostapd_interface_deinit_free(iface);
		return NULL;
	}

	return iface;
}


/**
 * handle_term - SIGINT and SIGTERM handler to terminate hostapd process
 */
static void handle_term(int sig, void *signal_ctx)
{
	wpa_printf(MSG_DEBUG, "Signal %d received - terminating", sig);
	eloop_terminate();
}


#ifndef CONFIG_NATIVE_WINDOWS

static int handle_reload_iface(struct hostapd_iface *iface, void *ctx)
{
	if (hostapd_reload_config(iface) < 0) {
		wpa_printf(MSG_WARNING, "Failed to read new configuration "
			   "file - continuing with old.");
	}
	return 0;
}


/**
 * handle_reload - SIGHUP handler to reload configuration
 */
static void handle_reload(int sig, void *signal_ctx)
{
	struct hapd_interfaces *interfaces = signal_ctx;
	wpa_printf(MSG_DEBUG, "Signal %d received - reloading configuration",
		   sig);
	hostapd_for_each_interface(interfaces, handle_reload_iface, NULL);
}


static void handle_dump_state(int sig, void *signal_ctx)
{
	/* Not used anymore - ignore signal */
}
#endif /* CONFIG_NATIVE_WINDOWS */


static int hostapd_global_init(struct hapd_interfaces *interfaces,
			       const char *entropy_file)
{
	int i;

	os_memset(&global, 0, sizeof(global));

	hostapd_logger_register_cb(hostapd_logger_cb);

	if (eap_server_register_methods()) {
		wpa_printf(MSG_ERROR, "Failed to register EAP methods");
		return -1;
	}

	if (eloop_init()) {
		wpa_printf(MSG_ERROR, "Failed to initialize event loop");
		return -1;
	}
	interfaces->eloop_initialized = 1;

	random_init(entropy_file);

#ifndef CONFIG_NATIVE_WINDOWS
	eloop_register_signal(SIGHUP, handle_reload, interfaces);
	eloop_register_signal(SIGUSR1, handle_dump_state, interfaces);
#endif /* CONFIG_NATIVE_WINDOWS */
	eloop_register_signal_terminate(handle_term, interfaces);

#ifndef CONFIG_NATIVE_WINDOWS
	openlog("hostapd", 0, LOG_DAEMON);
#endif /* CONFIG_NATIVE_WINDOWS */

	for (i = 0; wpa_drivers[i]; i++)
		global.drv_count++;
	if (global.drv_count == 0) {
		wpa_printf(MSG_ERROR, "No drivers enabled");
		return -1;
	}
	global.drv_priv = os_calloc(global.drv_count, sizeof(void *));
	if (global.drv_priv == NULL)
		return -1;

	return 0;
}


static void hostapd_global_deinit(const char *pid_file, int eloop_initialized)
{
	int i;

	for (i = 0; wpa_drivers[i] && global.drv_priv; i++) {
		if (!global.drv_priv[i])
			continue;
		wpa_drivers[i]->global_deinit(global.drv_priv[i]);
	}
	os_free(global.drv_priv);
	global.drv_priv = NULL;

#ifdef EAP_SERVER_TNC
	tncs_global_deinit();
#endif /* EAP_SERVER_TNC */

	random_deinit();

	if (eloop_initialized)
		eloop_destroy();

#ifndef CONFIG_NATIVE_WINDOWS
	closelog();
#endif /* CONFIG_NATIVE_WINDOWS */

	eap_server_unregister_methods();

	os_daemonize_terminate(pid_file);
}


static int hostapd_global_run(struct hapd_interfaces *ifaces, int daemonize,
			      const char *pid_file)
{
#ifdef EAP_SERVER_TNC
	int tnc = 0;
	size_t i, k;

	for (i = 0; !tnc && i < ifaces->count; i++) {
		for (k = 0; k < ifaces->iface[i]->num_bss; k++) {
			if (ifaces->iface[i]->bss[0]->conf->tnc) {
				tnc++;
				break;
			}
		}
	}

	if (tnc && tncs_global_init() < 0) {
		wpa_printf(MSG_ERROR, "Failed to initialize TNCS");
		return -1;
	}
#endif /* EAP_SERVER_TNC */

	if (daemonize) {
		if (os_daemonize(pid_file)) {
			wpa_printf(MSG_ERROR, "daemon: %s", strerror(errno));
			return -1;
		}
		if (eloop_sock_requeue()) {
			wpa_printf(MSG_ERROR, "eloop_sock_requeue: %s",
				   strerror(errno));
			return -1;
		}
	}

	eloop_run();

	return 0;
}


static void show_version(void)
{
	fprintf(stderr,
		"hostapd v" VERSION_STR "\n"
		"User space daemon for IEEE 802.11 AP management,\n"
		"IEEE 802.1X/WPA/WPA2/EAP/RADIUS Authenticator\n"
		"Copyright (c) 2002-2018, Jouni Malinen <j@w1.fi> "
		"and contributors\n");
}


static void usage(void)
{
	show_version();
	fprintf(stderr,
		"\n"
		"usage: hostapd [-hdBKtv] [-P <PID file>] [-e <entropy file>] "
		"\\\n"
		"         [-g <global ctrl_iface>] [-G <group>]\\\n"
		"         [-i <comma-separated list of interface names>]\\\n"
		"         <configuration file(s)>\n"
		"\n"
		"options:\n"
		"   -h   show this usage\n"
		"   -d   show more debug messages (-dd for even more)\n"
		"   -B   run daemon in the background\n"
		"   -e   entropy file\n"
		"   -g   global control interface path\n"
		"   -G   group for control interfaces\n"
		"   -P   PID file\n"
		"   -K   include key data in debug messages\n"
#ifdef CONFIG_DEBUG_FILE
		"   -f   log output to debug file instead of stdout\n"
#endif /* CONFIG_DEBUG_FILE */
#ifdef CONFIG_DEBUG_LINUX_TRACING
		"   -T   record to Linux tracing in addition to logging\n"
		"        (records all messages regardless of debug verbosity)\n"
#endif /* CONFIG_DEBUG_LINUX_TRACING */
		"   -i   list of interface names to use\n"
#ifdef CONFIG_DEBUG_SYSLOG
		"   -s   log output to syslog instead of stdout\n"
#endif /* CONFIG_DEBUG_SYSLOG */
		"   -S   start all the interfaces synchronously\n"
		"   -t   include timestamps in some debug messages\n"
		"   -v   show hostapd version\n");

	exit(1);
}


static const char * hostapd_msg_ifname_cb(void *ctx)
{
	struct hostapd_data *hapd = ctx;
	if (hapd && hapd->conf)
		return hapd->conf->iface;
	return NULL;
}


static int hostapd_get_global_ctrl_iface(struct hapd_interfaces *interfaces,
					 const char *path)
{
#ifndef CONFIG_CTRL_IFACE_UDP
	char *pos;
#endif /* !CONFIG_CTRL_IFACE_UDP */

	os_free(interfaces->global_iface_path);
	interfaces->global_iface_path = os_strdup(path);
	if (interfaces->global_iface_path == NULL)
		return -1;

#ifndef CONFIG_CTRL_IFACE_UDP
	pos = os_strrchr(interfaces->global_iface_path, '/');
	if (pos == NULL) {
		wpa_printf(MSG_ERROR, "No '/' in the global control interface "
			   "file");
		os_free(interfaces->global_iface_path);
		interfaces->global_iface_path = NULL;
		return -1;
	}

	*pos = '\0';
	interfaces->global_iface_name = pos + 1;
#endif /* !CONFIG_CTRL_IFACE_UDP */

	return 0;
}


static int hostapd_get_ctrl_iface_group(struct hapd_interfaces *interfaces,
					const char *group)
{
#ifndef CONFIG_NATIVE_WINDOWS
	struct group *grp;
	grp = getgrnam(group);
	if (grp == NULL) {
		wpa_printf(MSG_ERROR, "Unknown group '%s'", group);
		return -1;
	}
	interfaces->ctrl_iface_group = grp->gr_gid;
#endif /* CONFIG_NATIVE_WINDOWS */
	return 0;
}


static int hostapd_get_interface_names(char ***if_names,
				       size_t *if_names_size,
				       char *arg)
{
	char *if_name, *tmp, **nnames;
	size_t i;

	if (!arg)
		return -1;
	if_name = strtok_r(arg, ",", &tmp);

	while (if_name) {
		nnames = os_realloc_array(*if_names, 1 + *if_names_size,
					  sizeof(char *));
		if (!nnames)
			goto fail;
		*if_names = nnames;

		(*if_names)[*if_names_size] = os_strdup(if_name);
		if (!(*if_names)[*if_names_size])
			goto fail;
		(*if_names_size)++;
		if_name = strtok_r(NULL, ",", &tmp);
	}

	return 0;

fail:
	for (i = 0; i < *if_names_size; i++)
		os_free((*if_names)[i]);
	os_free(*if_names);
	*if_names = NULL;
	*if_names_size = 0;
	return -1;
}


#ifdef CONFIG_WPS
static int gen_uuid(const char *txt_addr)
{
	u8 addr[ETH_ALEN];
	u8 uuid[UUID_LEN];
	char buf[100];

	if (hwaddr_aton(txt_addr, addr) < 0)
		return -1;

	uuid_gen_mac_addr(addr, uuid);
	if (uuid_bin2str(uuid, buf, sizeof(buf)) < 0)
		return -1;

	printf("%s\n", buf);

	return 0;
}
#endif /* CONFIG_WPS */


#ifndef HOSTAPD_CLEANUP_INTERVAL
#define HOSTAPD_CLEANUP_INTERVAL 10
#endif /* HOSTAPD_CLEANUP_INTERVAL */

static int hostapd_periodic_call(struct hostapd_iface *iface, void *ctx)
{
	hostapd_periodic_iface(iface);
	return 0;
}


/* Periodic cleanup tasks */
static void hostapd_periodic(void *eloop_ctx, void *timeout_ctx)
{
	struct hapd_interfaces *interfaces = eloop_ctx;

	eloop_register_timeout(HOSTAPD_CLEANUP_INTERVAL, 0,
			       hostapd_periodic, interfaces, NULL);
	hostapd_for_each_interface(interfaces, hostapd_periodic_call, NULL);
}


int main(int argc, char *argv[])
{
	struct hapd_interfaces interfaces;
	int ret = 1;
	size_t i, j;
	int c, debug = 0, daemonize = 0;
	char *pid_file = NULL;
	const char *log_file = NULL;
	const char *entropy_file = NULL;
	char **bss_config = NULL, **tmp_bss;
	size_t num_bss_configs = 0;
#ifdef CONFIG_DEBUG_LINUX_TRACING
	int enable_trace_dbg = 0;
#endif /* CONFIG_DEBUG_LINUX_TRACING */
	int start_ifaces_in_sync = 0;
	char **if_names = NULL;
	size_t if_names_size = 0;

	if (os_program_init())
		return -1;

	os_memset(&interfaces, 0, sizeof(interfaces));
	interfaces.reload_config = hostapd_reload_config;
	interfaces.config_read_cb = hostapd_config_read;
	interfaces.for_each_interface = hostapd_for_each_interface;
	interfaces.ctrl_iface_init = hostapd_ctrl_iface_init;
	interfaces.ctrl_iface_deinit = hostapd_ctrl_iface_deinit;
	interfaces.driver_init = hostapd_driver_init;
	interfaces.global_iface_path = NULL;
	interfaces.global_iface_name = NULL;
	interfaces.global_ctrl_sock = -1;
	dl_list_init(&interfaces.global_ctrl_dst);
#ifdef CONFIG_ETH_P_OUI
	dl_list_init(&interfaces.eth_p_oui);
#endif /* CONFIG_ETH_P_OUI */
#ifdef CONFIG_DPP
	hostapd_dpp_init_global(&interfaces);
#endif /* CONFIG_DPP */

	for (;;) {
		c = getopt(argc, argv, "b:Bde:f:hi:KP:sSTtu:vg:G:");
		if (c < 0)
			break;
		switch (c) {
		case 'h':
			usage();
			break;
		case 'd':
			debug++;
			if (wpa_debug_level > 0)
				wpa_debug_level--;
			break;
		case 'B':
			daemonize++;
			break;
		case 'e':
			entropy_file = optarg;
			break;
		case 'f':
			log_file = optarg;
			break;
		case 'K':
			wpa_debug_show_keys++;
			break;
		case 'P':
			os_free(pid_file);
			pid_file = os_rel2abs_path(optarg);
			break;
		case 't':
			wpa_debug_timestamp++;
			break;
#ifdef CONFIG_DEBUG_LINUX_TRACING
		case 'T':
			enable_trace_dbg = 1;
			break;
#endif /* CONFIG_DEBUG_LINUX_TRACING */
		case 'v':
			show_version();
			exit(1);
			break;
		case 'g':
			if (hostapd_get_global_ctrl_iface(&interfaces, optarg))
				return -1;
			break;
		case 'G':
			if (hostapd_get_ctrl_iface_group(&interfaces, optarg))
				return -1;
			break;
		case 'b':
			tmp_bss = os_realloc_array(bss_config,
						   num_bss_configs + 1,
						   sizeof(char *));
			if (tmp_bss == NULL)
				goto out;
			bss_config = tmp_bss;
			bss_config[num_bss_configs++] = optarg;
			break;
#ifdef CONFIG_DEBUG_SYSLOG
		case 's':
			wpa_debug_syslog = 1;
			break;
#endif /* CONFIG_DEBUG_SYSLOG */
		case 'S':
			start_ifaces_in_sync = 1;
			break;
#ifdef CONFIG_WPS
		case 'u':
			return gen_uuid(optarg);
#endif /* CONFIG_WPS */
		case 'i':
			if (hostapd_get_interface_names(&if_names,
							&if_names_size, optarg))
				goto out;
			break;
		default:
			usage();
			break;
		}
	}

	if (optind == argc && interfaces.global_iface_path == NULL &&
	    num_bss_configs == 0)
		usage();

	wpa_msg_register_ifname_cb(hostapd_msg_ifname_cb);

	if (log_file)
		wpa_debug_open_file(log_file);
	else
		wpa_debug_setup_stdout();
#ifdef CONFIG_DEBUG_SYSLOG
	if (wpa_debug_syslog)
		wpa_debug_open_syslog();
#endif /* CONFIG_DEBUG_SYSLOG */
#ifdef CONFIG_DEBUG_LINUX_TRACING
	if (enable_trace_dbg) {
		int tret = wpa_debug_open_linux_tracing();
		if (tret) {
			wpa_printf(MSG_ERROR, "Failed to enable trace logging");
			return -1;
		}
	}
#endif /* CONFIG_DEBUG_LINUX_TRACING */

	interfaces.count = argc - optind;
	if (interfaces.count || num_bss_configs) {
		interfaces.iface = os_calloc(interfaces.count + num_bss_configs,
					     sizeof(struct hostapd_iface *));
		if (interfaces.iface == NULL) {
			wpa_printf(MSG_ERROR, "malloc failed");
			return -1;
		}
	}

	if (hostapd_global_init(&interfaces, entropy_file)) {
		wpa_printf(MSG_ERROR, "Failed to initialize global context");
		return -1;
	}

	eloop_register_timeout(HOSTAPD_CLEANUP_INTERVAL, 0,
			       hostapd_periodic, &interfaces, NULL);

	if (fst_global_init()) {
		wpa_printf(MSG_ERROR,
			   "Failed to initialize global FST context");
		goto out;
	}

#if defined(CONFIG_FST) && defined(CONFIG_CTRL_IFACE)
	if (!fst_global_add_ctrl(fst_ctrl_cli))
		wpa_printf(MSG_WARNING, "Failed to add CLI FST ctrl");
#endif /* CONFIG_FST && CONFIG_CTRL_IFACE */

	/* Allocate and parse configuration for full interface files */
	for (i = 0; i < interfaces.count; i++) {
		char *if_name = NULL;

		if (i < if_names_size)
			if_name = if_names[i];

		interfaces.iface[i] = hostapd_interface_init(&interfaces,
							     if_name,
							     argv[optind + i],
							     debug);
		if (!interfaces.iface[i]) {
			wpa_printf(MSG_ERROR, "Failed to initialize interface");
			goto out;
		}
		if (start_ifaces_in_sync)
			interfaces.iface[i]->need_to_start_in_sync = 1;
	}

	/* Allocate and parse configuration for per-BSS files */
	for (i = 0; i < num_bss_configs; i++) {
		struct hostapd_iface *iface;
		char *fname;

		wpa_printf(MSG_INFO, "BSS config: %s", bss_config[i]);
		fname = os_strchr(bss_config[i], ':');
		if (fname == NULL) {
			wpa_printf(MSG_ERROR,
				   "Invalid BSS config identifier '%s'",
				   bss_config[i]);
			goto out;
		}
		*fname++ = '\0';
		iface = hostapd_interface_init_bss(&interfaces, bss_config[i],
						   fname, debug);
		if (iface == NULL)
			goto out;
		for (j = 0; j < interfaces.count; j++) {
			if (interfaces.iface[j] == iface)
				break;
		}
		if (j == interfaces.count) {
			struct hostapd_iface **tmp;
			tmp = os_realloc_array(interfaces.iface,
					       interfaces.count + 1,
					       sizeof(struct hostapd_iface *));
			if (tmp == NULL) {
				hostapd_interface_deinit_free(iface);
				goto out;
			}
			interfaces.iface = tmp;
			interfaces.iface[interfaces.count++] = iface;
		}
	}

	/*
	 * Enable configured interfaces. Depending on channel configuration,
	 * this may complete full initialization before returning or use a
	 * callback mechanism to complete setup in case of operations like HT
	 * co-ex scans, ACS, or DFS are needed to determine channel parameters.
	 * In such case, the interface will be enabled from eloop context within
	 * hostapd_global_run().
	 */
	interfaces.terminate_on_error = interfaces.count;
	for (i = 0; i < interfaces.count; i++) {
		if (hostapd_driver_init(interfaces.iface[i]) ||
		    hostapd_setup_interface(interfaces.iface[i]))
			goto out;
	}

	hostapd_global_ctrl_iface_init(&interfaces);

	if (hostapd_global_run(&interfaces, daemonize, pid_file)) {
		wpa_printf(MSG_ERROR, "Failed to start eloop");
		goto out;
	}

	ret = 0;

 out:
	hostapd_global_ctrl_iface_deinit(&interfaces);
	/* Deinitialize all interfaces */
	for (i = 0; i < interfaces.count; i++) {
		if (!interfaces.iface[i])
			continue;
		interfaces.iface[i]->driver_ap_teardown =
			!!(interfaces.iface[i]->drv_flags &
			   WPA_DRIVER_FLAGS_AP_TEARDOWN_SUPPORT);
		hostapd_interface_deinit_free(interfaces.iface[i]);
	}
	os_free(interfaces.iface);

#ifdef CONFIG_DPP
	hostapd_dpp_deinit_global(&interfaces);
#endif /* CONFIG_DPP */

	if (interfaces.eloop_initialized)
		eloop_cancel_timeout(hostapd_periodic, &interfaces, NULL);
	hostapd_global_deinit(pid_file, interfaces.eloop_initialized);
	os_free(pid_file);

	wpa_debug_close_syslog();
	if (log_file)
		wpa_debug_close_file();
	wpa_debug_close_linux_tracing();

	os_free(bss_config);

	for (i = 0; i < if_names_size; i++)
		os_free(if_names[i]);
	os_free(if_names);

	fst_global_deinit();

	os_program_deinit();

	return ret;
}
