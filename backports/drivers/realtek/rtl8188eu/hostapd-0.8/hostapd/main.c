/*
 * hostapd / main()
 * Copyright (c) 2002-2011, Jouni Malinen <j@w1.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Alternatively, this software may be distributed under the terms of BSD
 * license.
 *
 * See README and COPYING for more details.
 */

#include "utils/includes.h"
#ifndef CONFIG_NATIVE_WINDOWS
#include <syslog.h>
#endif /* CONFIG_NATIVE_WINDOWS */

#include "utils/common.h"
#include "utils/eloop.h"
#include "crypto/random.h"
#include "crypto/tls.h"
#include "common/version.h"
#include "drivers/driver.h"
#include "eap_server/eap.h"
#include "eap_server/tncs.h"
#include "ap/hostapd.h"
#include "ap/ap_config.h"
#include "config_file.h"
#include "eap_register.h"
#include "dump_state.h"
#include "ctrl_iface.h"


extern int wpa_debug_level;
extern int wpa_debug_show_keys;
extern int wpa_debug_timestamp;


struct hapd_interfaces {
	size_t count;
	struct hostapd_iface **iface;
};


static int hostapd_for_each_interface(struct hapd_interfaces *interfaces,
				      int (*cb)(struct hostapd_iface *iface,
						void *ctx), void *ctx)
{
	size_t i;
	int ret;

	for (i = 0; i < interfaces->count; i++) {
		ret = cb(interfaces->iface[i], ctx);
		if (ret)
			return ret;
	}

	return 0;
}


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
			    module_str ? " " : "", module_str, txt);
	else if (hapd && hapd->conf)
		os_snprintf(format, maxlen, "%s:%s%s %s",
			    hapd->conf->iface, module_str ? " " : "",
			    module_str, txt);
	else if (addr)
		os_snprintf(format, maxlen, "STA " MACSTR "%s%s: %s",
			    MAC2STR(addr), module_str ? " " : "",
			    module_str, txt);
	else
		os_snprintf(format, maxlen, "%s%s%s",
			    module_str, module_str ? ": " : "", txt);

	if ((conf_stdout & module) && level >= conf_stdout_level) {
		wpa_debug_print_timestamp();
		printf("%s\n", format);
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
 * hostapd_init - Allocate and initialize per-interface data
 * @config_file: Path to the configuration file
 * Returns: Pointer to the allocated interface data or %NULL on failure
 *
 * This function is used to allocate main data structures for per-interface
 * data. The allocated data buffer will be freed by calling
 * hostapd_cleanup_iface().
 */
static struct hostapd_iface * hostapd_init(const char *config_file)
{
	struct hostapd_iface *hapd_iface = NULL;
	struct hostapd_config *conf = NULL;
	struct hostapd_data *hapd;
	size_t i;

	hapd_iface = os_zalloc(sizeof(*hapd_iface));
	if (hapd_iface == NULL)
		goto fail;

	hapd_iface->reload_config = hostapd_reload_config;
	hapd_iface->config_read_cb = hostapd_config_read;
	hapd_iface->config_fname = os_strdup(config_file);
	if (hapd_iface->config_fname == NULL)
		goto fail;
	hapd_iface->ctrl_iface_init = hostapd_ctrl_iface_init;
	hapd_iface->ctrl_iface_deinit = hostapd_ctrl_iface_deinit;
	hapd_iface->for_each_interface = hostapd_for_each_interface;

	conf = hostapd_config_read(hapd_iface->config_fname);
	if (conf == NULL)
		goto fail;
	hapd_iface->conf = conf;

	hapd_iface->num_bss = conf->num_bss;
	hapd_iface->bss = os_zalloc(conf->num_bss *
				    sizeof(struct hostapd_data *));
	if (hapd_iface->bss == NULL)
		goto fail;

	for (i = 0; i < conf->num_bss; i++) {
		hapd = hapd_iface->bss[i] =
			hostapd_alloc_bss_data(hapd_iface, conf,
					       &conf->bss[i]);
		if (hapd == NULL)
			goto fail;
		hapd->msg_ctx = hapd;
	}

	return hapd_iface;

fail:
	if (conf)
		hostapd_config_free(conf);
	if (hapd_iface) {
		os_free(hapd_iface->config_fname);
		os_free(hapd_iface->bss);
		os_free(hapd_iface);
	}
	return NULL;
}


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
	params.bssid = b;
	params.ifname = hapd->conf->iface;
	params.ssid = (const u8 *) hapd->conf->ssid.ssid;
	params.ssid_len = hapd->conf->ssid.ssid_len;
	params.test_socket = hapd->conf->test_socket;
	params.use_pae_group_addr = hapd->conf->use_pae_group_addr;

	params.num_bridge = hapd->iface->num_bss;
	params.bridge = os_zalloc(hapd->iface->num_bss * sizeof(char *));
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
	    hapd->driver->get_capa(hapd->drv_priv, &capa) == 0)
		iface->drv_flags = capa.flags;

	return 0;
}


static void hostapd_interface_deinit_free(struct hostapd_iface *iface)
{
	const struct wpa_driver_ops *driver;
	void *drv_priv;
	if (iface == NULL)
		return;
	driver = iface->bss[0]->driver;
	drv_priv = iface->bss[0]->drv_priv;
	hostapd_interface_deinit(iface);
	if (driver && driver->hapd_deinit)
		driver->hapd_deinit(drv_priv);
	hostapd_interface_free(iface);
}


static struct hostapd_iface *
hostapd_interface_init(struct hapd_interfaces *interfaces,
		       const char *config_fname, int debug)
{
	struct hostapd_iface *iface;
	int k;

	wpa_printf(MSG_ERROR, "Configuration file: %s", config_fname);
	iface = hostapd_init(config_fname);
	if (!iface)
		return NULL;
	iface->interfaces = interfaces;

	for (k = 0; k < debug; k++) {
		if (iface->bss[0]->conf->logger_stdout_level > 0)
			iface->bss[0]->conf->logger_stdout_level--;
	}

	if (hostapd_driver_init(iface) ||
	    hostapd_setup_interface(iface)) {
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
#ifdef HOSTAPD_DUMP_STATE
	struct hapd_interfaces *interfaces = signal_ctx;
	hostapd_for_each_interface(interfaces, handle_dump_state_iface, NULL);
#endif /* HOSTAPD_DUMP_STATE */
}
#endif /* CONFIG_NATIVE_WINDOWS */


static int hostapd_global_init(struct hapd_interfaces *interfaces)
{
	hostapd_logger_register_cb(hostapd_logger_cb);

	if (eap_server_register_methods()) {
		wpa_printf(MSG_ERROR, "Failed to register EAP methods");
		return -1;
	}

	if (eloop_init()) {
		wpa_printf(MSG_ERROR, "Failed to initialize event loop");
		return -1;
	}

	random_init();

#ifndef CONFIG_NATIVE_WINDOWS
	eloop_register_signal(SIGHUP, handle_reload, interfaces);
	eloop_register_signal(SIGUSR1, handle_dump_state, interfaces);
#endif /* CONFIG_NATIVE_WINDOWS */
	eloop_register_signal_terminate(handle_term, interfaces);

#ifndef CONFIG_NATIVE_WINDOWS
	openlog("hostapd", 0, LOG_DAEMON);
#endif /* CONFIG_NATIVE_WINDOWS */

	return 0;
}


static void hostapd_global_deinit(const char *pid_file)
{
#ifdef EAP_SERVER_TNC
	tncs_global_deinit();
#endif /* EAP_SERVER_TNC */

	random_deinit();

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

	if (daemonize && os_daemonize(pid_file)) {
		perror("daemon");
		return -1;
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
		"Copyright (c) 2002-2011, Jouni Malinen <j@w1.fi> "
		"and contributors\n");
}


static void usage(void)
{
	show_version();
	fprintf(stderr,
		"\n"
		"usage: hostapd [-hdBKtv] [-P <PID file>] "
		"<configuration file(s)>\n"
		"\n"
		"options:\n"
		"   -h   show this usage\n"
		"   -d   show more debug messages (-dd for even more)\n"
		"   -B   run daemon in the background\n"
		"   -P   PID file\n"
		"   -K   include key data in debug messages\n"
#ifdef CONFIG_DEBUG_FILE
		"   -f   log output to debug file instead of stdout\n"
#endif /* CONFIG_DEBUG_FILE */
		"   -t   include timestamps in some debug messages\n"
		"   -v   show hostapd version\n");

	exit(1);
}


static const char * hostapd_msg_ifname_cb(void *ctx)
{
	struct hostapd_data *hapd = ctx;
	if (hapd && hapd->iconf && hapd->iconf->bss)
		return hapd->iconf->bss->iface;
	return NULL;
}


int main(int argc, char *argv[])
{
	struct hapd_interfaces interfaces;
	int ret = 1;
	size_t i;
	int c, debug = 0, daemonize = 0;
	char *pid_file = NULL;
	const char *log_file = NULL;

	if (os_program_init())
		return -1;

	for (;;) {
		c = getopt(argc, argv, "Bdf:hKP:tv");
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
		case 'v':
			show_version();
			exit(1);
			break;

		default:
			usage();
			break;
		}
	}

	if (optind == argc)
		usage();

	wpa_msg_register_ifname_cb(hostapd_msg_ifname_cb);

	if (log_file)
		wpa_debug_open_file(log_file);

	interfaces.count = argc - optind;
	interfaces.iface = os_zalloc(interfaces.count *
				     sizeof(struct hostapd_iface *));
	if (interfaces.iface == NULL) {
		wpa_printf(MSG_ERROR, "malloc failed");
		return -1;
	}

	if (hostapd_global_init(&interfaces))
		return -1;

	/* Initialize interfaces */
	for (i = 0; i < interfaces.count; i++) {
		interfaces.iface[i] = hostapd_interface_init(&interfaces,
							     argv[optind + i],
							     debug);
		if (!interfaces.iface[i])
			goto out;
	}

	if (hostapd_global_run(&interfaces, daemonize, pid_file))
		goto out;

	ret = 0;

 out:
	/* Deinitialize all interfaces */
	for (i = 0; i < interfaces.count; i++)
		hostapd_interface_deinit_free(interfaces.iface[i]);
	os_free(interfaces.iface);

	hostapd_global_deinit(pid_file);
	os_free(pid_file);

	if (log_file)
		wpa_debug_close_file();

	os_program_deinit();

	return ret;
}
