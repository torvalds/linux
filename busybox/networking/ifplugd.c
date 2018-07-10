/* vi: set sw=4 ts=4: */
/*
 * ifplugd for busybox, based on ifplugd 0.28 (written by Lennart Poettering).
 *
 * Copyright (C) 2009 Maksym Kryzhanovskyy <xmaks@email.cz>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config IFPLUGD
//config:	bool "ifplugd (9.9 kb)"
//config:	default y
//config:	select PLATFORM_LINUX
//config:	help
//config:	Network interface plug detection daemon.

//applet:IF_IFPLUGD(APPLET(ifplugd, BB_DIR_USR_SBIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_IFPLUGD) += ifplugd.o

//usage:#define ifplugd_trivial_usage
//usage:       "[OPTIONS]"
//usage:#define ifplugd_full_usage "\n\n"
//usage:       "Network interface plug detection daemon\n"
//usage:     "\n	-n		Don't daemonize"
//usage:     "\n	-s		Don't log to syslog"
//usage:     "\n	-i IFACE	Interface"
//usage:     "\n	-f/-F		Treat link detection error as link down/link up"
//usage:     "\n			(otherwise exit on error)"
//usage:     "\n	-a		Don't up interface at each link probe"
//usage:     "\n	-M		Monitor creation/destruction of interface"
//usage:     "\n			(otherwise it must exist)"
//usage:     "\n	-r PROG		Script to run"
//usage:     "\n	-x ARG		Extra argument for script"
//usage:     "\n	-I		Don't exit on nonzero exit code from script"
//usage:     "\n	-p		Don't run \"up\" script on startup"
//usage:     "\n	-q		Don't run \"down\" script on exit"
//usage:     "\n	-l		Always run script on startup"
//usage:     "\n	-t SECS		Poll time in seconds"
//usage:     "\n	-u SECS		Delay before running script after link up"
//usage:     "\n	-d SECS		Delay after link down"
//usage:     "\n	-m MODE		API mode (mii, priv, ethtool, wlan, iff, auto)"
//usage:     "\n	-k		Kill running daemon"

#include "libbb.h"

#include "fix_u32.h"
#include <linux/if.h>
#include <linux/mii.h>
#include <linux/ethtool.h>
#ifdef HAVE_NET_ETHERNET_H
/* musl breakage:
 * In file included from /usr/include/net/ethernet.h:10,
 *                  from networking/ifplugd.c:41:
 * /usr/include/netinet/if_ether.h:96: error: redefinition of 'struct ethhdr'
 *
 * Build succeeds without it on musl. Commented it out.
 * If on your system you need it, consider removing <linux/ethtool.h>
 * and copy-pasting its definitions here (<linux/ethtool.h> is what pulls in
 * conflicting definition of struct ethhdr on musl).
 */
/* # include <net/ethernet.h> */
#endif
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/sockios.h>
#include <syslog.h>

#define __user
#include <linux/wireless.h>

#ifndef ETH_ALEN
# define ETH_ALEN  6
#endif

/*
From initial port to busybox, removed most of the redundancy by
converting implementation of a polymorphic interface to the strict
functional style. The main role is run a script when link state
changed, other activities like audio signal or detailed reports
are on the script itself.

One questionable point of the design is netlink usage:

We have 1 second timeout by default to poll the link status,
it is short enough so that there are no real benefits in
using netlink to get "instantaneous" interface creation/deletion
notifications. We can check for interface existence by just
doing some fast ioctl using its name.

Netlink code then can be just dropped (1k or more?)
*/


#define IFPLUGD_ENV_PREVIOUS "IFPLUGD_PREVIOUS"
#define IFPLUGD_ENV_CURRENT "IFPLUGD_CURRENT"

enum {
	FLAG_NO_AUTO			= 1 <<  0, // -a, Do not enable interface automatically
	FLAG_NO_DAEMON			= 1 <<  1, // -n, Do not daemonize
	FLAG_NO_SYSLOG			= 1 <<  2, // -s, Do not use syslog, use stderr instead
	FLAG_IGNORE_FAIL		= 1 <<  3, // -f, Ignore detection failure, retry instead (failure is treated as DOWN)
	FLAG_IGNORE_FAIL_POSITIVE	= 1 <<  4, // -F, Ignore detection failure, retry instead (failure is treated as UP)
	FLAG_IFACE			= 1 <<  5, // -i, Specify ethernet interface
	FLAG_RUN			= 1 <<  6, // -r, Specify program to execute
	FLAG_IGNORE_RETVAL		= 1 <<  7, // -I, Don't exit on nonzero return value of program executed
	FLAG_POLL_TIME			= 1 <<  8, // -t, Specify poll time in seconds
	FLAG_DELAY_UP			= 1 <<  9, // -u, Specify delay for configuring interface
	FLAG_DELAY_DOWN			= 1 << 10, // -d, Specify delay for deconfiguring interface
	FLAG_API_MODE			= 1 << 11, // -m, Force API mode (mii, priv, ethtool, wlan, auto)
	FLAG_NO_STARTUP			= 1 << 12, // -p, Don't run script on daemon startup
	FLAG_NO_SHUTDOWN		= 1 << 13, // -q, Don't run script on daemon quit
	FLAG_INITIAL_DOWN		= 1 << 14, // -l, Run "down" script on startup if no cable is detected
	FLAG_EXTRA_ARG			= 1 << 15, // -x, Specify an extra argument for action script
	FLAG_MONITOR			= 1 << 16, // -M, Use interface monitoring
#if ENABLE_FEATURE_PIDFILE
	FLAG_KILL			= 1 << 17, // -k, Kill a running daemon
#endif
};
#if ENABLE_FEATURE_PIDFILE
# define OPTION_STR "+ansfFi:r:It:+u:+d:+m:pqlx:Mk"
#else
# define OPTION_STR "+ansfFi:r:It:+u:+d:+m:pqlx:M"
#endif

enum { // interface status
	IFSTATUS_ERR = -1,
	IFSTATUS_DOWN = 0,
	IFSTATUS_UP = 1,
};

enum { // constant fds
	ioctl_fd = 3,
	netlink_fd = 4,
};

struct globals {
	smallint iface_last_status;
	smallint iface_prev_status;
	smallint iface_exists;
	smallint api_method_num;

	/* Used in getopt32, must have sizeof == sizeof(int) */
	unsigned poll_time;
	unsigned delay_up;
	unsigned delay_down;

	const char *iface;
	const char *api_mode;
	const char *script_name;
	const char *extra_arg;
};
#define G (*ptr_to_globals)
#define INIT_G() do { \
	SET_PTR_TO_GLOBALS(xzalloc(sizeof(G))); \
	G.iface_last_status = -1; \
	G.iface_exists   = 1; \
	G.poll_time      = 1; \
	G.delay_down     = 5; \
	G.iface          = "eth0"; \
	G.api_mode       = "a"; \
	G.script_name    = "/etc/ifplugd/ifplugd.action"; \
} while (0)


/* Utility routines */

static void set_ifreq_to_ifname(struct ifreq *ifreq)
{
	memset(ifreq, 0, sizeof(struct ifreq));
	strncpy_IFNAMSIZ(ifreq->ifr_name, G.iface);
}

static int network_ioctl(int request, void* data, const char *errmsg)
{
	int r = ioctl(ioctl_fd, request, data);
	if (r < 0 && errmsg)
		bb_perror_msg("%s failed", errmsg);
	return r;
}

/* Link detection routines and table */

static smallint detect_link_mii(void)
{
	/* char buffer instead of bona-fide struct avoids aliasing warning */
	char buf[sizeof(struct ifreq)];
	struct ifreq *const ifreq = (void *)buf;

	struct mii_ioctl_data *mii = (void *)&ifreq->ifr_data;

	set_ifreq_to_ifname(ifreq);

	if (network_ioctl(SIOCGMIIPHY, ifreq, "SIOCGMIIPHY") < 0) {
		return IFSTATUS_ERR;
	}

	mii->reg_num = 1;

	if (network_ioctl(SIOCGMIIREG, ifreq, "SIOCGMIIREG") < 0) {
		return IFSTATUS_ERR;
	}

	return (mii->val_out & 0x0004) ? IFSTATUS_UP : IFSTATUS_DOWN;
}

static smallint detect_link_priv(void)
{
	/* char buffer instead of bona-fide struct avoids aliasing warning */
	char buf[sizeof(struct ifreq)];
	struct ifreq *const ifreq = (void *)buf;

	struct mii_ioctl_data *mii = (void *)&ifreq->ifr_data;

	set_ifreq_to_ifname(ifreq);

	if (network_ioctl(SIOCDEVPRIVATE, ifreq, "SIOCDEVPRIVATE") < 0) {
		return IFSTATUS_ERR;
	}

	mii->reg_num = 1;

	if (network_ioctl(SIOCDEVPRIVATE+1, ifreq, "SIOCDEVPRIVATE+1") < 0) {
		return IFSTATUS_ERR;
	}

	return (mii->val_out & 0x0004) ? IFSTATUS_UP : IFSTATUS_DOWN;
}

static smallint detect_link_ethtool(void)
{
	struct ifreq ifreq;
	struct ethtool_value edata;

	set_ifreq_to_ifname(&ifreq);

	edata.cmd = ETHTOOL_GLINK;
	ifreq.ifr_data = (void*) &edata;

	if (network_ioctl(SIOCETHTOOL, &ifreq, "ETHTOOL_GLINK") < 0) {
		return IFSTATUS_ERR;
	}

	return edata.data ? IFSTATUS_UP : IFSTATUS_DOWN;
}

static smallint detect_link_iff(void)
{
	struct ifreq ifreq;

	set_ifreq_to_ifname(&ifreq);

	if (network_ioctl(SIOCGIFFLAGS, &ifreq, "SIOCGIFFLAGS") < 0) {
		return IFSTATUS_ERR;
	}

	/* If IFF_UP is not set (interface is down), IFF_RUNNING is never set
	 * regardless of link status. Simply continue to report last status -
	 * no point in reporting spurious link downs if interface is disabled
	 * by admin. When/if it will be brought up,
	 * we'll report real link status.
	 */
	if (!(ifreq.ifr_flags & IFF_UP) && G.iface_last_status != IFSTATUS_ERR)
		return G.iface_last_status;

	return (ifreq.ifr_flags & IFF_RUNNING) ? IFSTATUS_UP : IFSTATUS_DOWN;
}

static smallint detect_link_wlan(void)
{
	int i;
	struct iwreq iwrequest;
	uint8_t mac[ETH_ALEN];

	memset(&iwrequest, 0, sizeof(iwrequest));
	strncpy_IFNAMSIZ(iwrequest.ifr_ifrn.ifrn_name, G.iface);

	if (network_ioctl(SIOCGIWAP, &iwrequest, "SIOCGIWAP") < 0) {
		return IFSTATUS_ERR;
	}

	memcpy(mac, &iwrequest.u.ap_addr.sa_data, ETH_ALEN);

	if (mac[0] == 0xFF || mac[0] == 0x44 || mac[0] == 0x00) {
		for (i = 1; i < ETH_ALEN; ++i) {
			if (mac[i] != mac[0])
				return IFSTATUS_UP;
		}
		return IFSTATUS_DOWN;
	}

	return IFSTATUS_UP;
}

enum { // api mode
	API_ETHTOOL, // 'e'
	API_MII,     // 'm'
	API_PRIVATE, // 'p'
	API_WLAN,    // 'w'
	API_IFF,     // 'i'
	API_AUTO,    // 'a'
};

static const char api_modes[] ALIGN1 = "empwia";

static const struct {
	const char *name;
	smallint (*func)(void);
} method_table[] = {
	{ "SIOCETHTOOL"       , &detect_link_ethtool },
	{ "SIOCGMIIPHY"       , &detect_link_mii     },
	{ "SIOCDEVPRIVATE"    , &detect_link_priv    },
	{ "wireless extension", &detect_link_wlan    },
	{ "IFF_RUNNING"       , &detect_link_iff     },
};

static const char *strstatus(int status)
{
	if (status == IFSTATUS_ERR)
		return "error";
	return "down\0up" + (status * 5);
}

static int run_script(const char *action)
{
	char *env_PREVIOUS, *env_CURRENT;
	char *argv[5];
	int r;

	bb_error_msg("executing '%s %s %s'", G.script_name, G.iface, action);

	argv[0] = (char*) G.script_name;
	argv[1] = (char*) G.iface;
	argv[2] = (char*) action;
	argv[3] = (char*) G.extra_arg;
	argv[4] = NULL;

	env_PREVIOUS = xasprintf("%s=%s", IFPLUGD_ENV_PREVIOUS, strstatus(G.iface_prev_status));
	putenv(env_PREVIOUS);
	env_CURRENT = xasprintf("%s=%s", IFPLUGD_ENV_CURRENT, strstatus(G.iface_last_status));
	putenv(env_CURRENT);

	/* r < 0 - can't exec, 0 <= r < 0x180 - exited, >=0x180 - killed by sig (r-0x180) */
	r = spawn_and_wait(argv);

	bb_unsetenv_and_free(env_PREVIOUS);
	bb_unsetenv_and_free(env_CURRENT);

	bb_error_msg("exit code: %d", r & 0xff);
	return (option_mask32 & FLAG_IGNORE_RETVAL) ? 0 : r;
}

static void up_iface(void)
{
	struct ifreq ifrequest;

	if (!G.iface_exists)
		return;

	set_ifreq_to_ifname(&ifrequest);
	if (network_ioctl(SIOCGIFFLAGS, &ifrequest, "getting interface flags") < 0) {
		G.iface_exists = 0;
		return;
	}

	if (!(ifrequest.ifr_flags & IFF_UP)) {
		ifrequest.ifr_flags |= IFF_UP;
		/* Let user know we mess up with interface */
		bb_error_msg("upping interface");
		if (network_ioctl(SIOCSIFFLAGS, &ifrequest, "setting interface flags") < 0) {
			if (errno != ENODEV && errno != EADDRNOTAVAIL)
				xfunc_die();
			G.iface_exists = 0;
			return;
		}
	}

#if 0 /* why do we mess with IP addr? It's not our business */
	if (network_ioctl(SIOCGIFADDR, &ifrequest, "can't get interface address") < 0) {
	} else if (ifrequest.ifr_addr.sa_family != AF_INET) {
		bb_perror_msg("the interface is not IP-based");
	} else {
		((struct sockaddr_in*)(&ifrequest.ifr_addr))->sin_addr.s_addr = INADDR_ANY;
		network_ioctl(SIOCSIFADDR, &ifrequest, "can't set interface address");
	}
	network_ioctl(SIOCGIFFLAGS, &ifrequest, "can't get interface flags");
#endif
}

static void maybe_up_new_iface(void)
{
	if (!(option_mask32 & FLAG_NO_AUTO))
		up_iface();

#if 0 /* bloat */
	struct ifreq ifrequest;
	struct ethtool_drvinfo driver_info;

	set_ifreq_to_ifname(&ifrequest);
	driver_info.cmd = ETHTOOL_GDRVINFO;
	ifrequest.ifr_data = &driver_info;
	if (network_ioctl(SIOCETHTOOL, &ifrequest, NULL) == 0) {
		char buf[sizeof("/xx:xx:xx:xx:xx:xx")];

		/* Get MAC */
		buf[0] = '\0';
		set_ifreq_to_ifname(&ifrequest);
		if (network_ioctl(SIOCGIFHWADDR, &ifrequest, NULL) == 0) {
			sprintf(buf, "/%02X:%02X:%02X:%02X:%02X:%02X",
				(uint8_t)(ifrequest.ifr_hwaddr.sa_data[0]),
				(uint8_t)(ifrequest.ifr_hwaddr.sa_data[1]),
				(uint8_t)(ifrequest.ifr_hwaddr.sa_data[2]),
				(uint8_t)(ifrequest.ifr_hwaddr.sa_data[3]),
				(uint8_t)(ifrequest.ifr_hwaddr.sa_data[4]),
				(uint8_t)(ifrequest.ifr_hwaddr.sa_data[5]));
		}

		bb_error_msg("using interface %s%s with driver<%s> (version: %s)",
			G.iface, buf, driver_info.driver, driver_info.version);
	}
#endif
	if (G.api_mode[0] == 'a')
		G.api_method_num = API_AUTO;
}

static smallint detect_link(void)
{
	smallint status;

	if (!G.iface_exists)
		return (option_mask32 & FLAG_MONITOR) ? IFSTATUS_DOWN : IFSTATUS_ERR;

	/* Some drivers can't detect link status when the interface is down.
	 * I imagine detect_link_iff() is the most vulnerable.
	 * That's why -a "noauto" in an option, not a hardwired behavior.
	 */
	if (!(option_mask32 & FLAG_NO_AUTO))
		up_iface();

	if (G.api_method_num == API_AUTO) {
		int i;
		smallint sv_logmode;

		sv_logmode = logmode;
		for (i = 0; i < ARRAY_SIZE(method_table); i++) {
			logmode = LOGMODE_NONE;
			status = method_table[i].func();
			logmode = sv_logmode;
			if (status != IFSTATUS_ERR) {
				G.api_method_num = i;
				bb_error_msg("using %s detection mode", method_table[i].name);
				break;
			}
		}
	} else {
		status = method_table[G.api_method_num].func();
	}

	if (status == IFSTATUS_ERR) {
		if (option_mask32 & FLAG_IGNORE_FAIL)
			status = IFSTATUS_DOWN;
		else if (option_mask32 & FLAG_IGNORE_FAIL_POSITIVE)
			status = IFSTATUS_UP;
		else if (G.api_mode[0] == 'a')
			bb_error_msg("can't detect link status");
	}

	if (status != G.iface_last_status) {
		G.iface_prev_status = G.iface_last_status;
		G.iface_last_status = status;
	}

	return status;
}

static NOINLINE int check_existence_through_netlink(void)
{
	int iface_len;
	/* Buffer was 1K, but on linux-3.9.9 it was reported to be too small.
	 * netlink.h: "limit to 8K to avoid MSG_TRUNC when PAGE_SIZE is very large".
	 * Note: on error returns (-1) we exit, no need to free replybuf.
	 */
	enum { BUF_SIZE = 8 * 1024 };
	char *replybuf = xmalloc(BUF_SIZE);

	iface_len = strlen(G.iface);
	while (1) {
		struct nlmsghdr *mhdr;
		ssize_t bytes;

		bytes = recv(netlink_fd, replybuf, BUF_SIZE, MSG_DONTWAIT);
		if (bytes < 0) {
			if (errno == EAGAIN)
				goto ret;
			if (errno == EINTR)
				continue;
			bb_perror_msg("netlink: recv");
			return -1;
		}

		mhdr = (struct nlmsghdr*)replybuf;
		while (bytes > 0) {
			if (!NLMSG_OK(mhdr, bytes)) {
				bb_error_msg("netlink packet too small or truncated");
				return -1;
			}

			if (mhdr->nlmsg_type == RTM_NEWLINK || mhdr->nlmsg_type == RTM_DELLINK) {
				struct rtattr *attr;
				int attr_len;

				if (mhdr->nlmsg_len < NLMSG_LENGTH(sizeof(struct ifinfomsg))) {
					bb_error_msg("netlink packet too small or truncated");
					return -1;
				}

				attr = IFLA_RTA(NLMSG_DATA(mhdr));
				attr_len = IFLA_PAYLOAD(mhdr);

				while (RTA_OK(attr, attr_len)) {
					if (attr->rta_type == IFLA_IFNAME) {
						int len = RTA_PAYLOAD(attr);
						if (len > IFNAMSIZ)
							len = IFNAMSIZ;
						if (iface_len <= len
						 && strncmp(G.iface, RTA_DATA(attr), len) == 0
						) {
							G.iface_exists = (mhdr->nlmsg_type == RTM_NEWLINK);
						}
					}
					attr = RTA_NEXT(attr, attr_len);
				}
			}

			mhdr = NLMSG_NEXT(mhdr, bytes);
		}
	}

 ret:
	free(replybuf);
	return G.iface_exists;
}

#if ENABLE_FEATURE_PIDFILE
static NOINLINE pid_t read_pid(const char *filename)
{
	int len;
	char buf[128];

	len = open_read_close(filename, buf, 127);
	if (len > 0) {
		buf[len] = '\0';
		/* returns ULONG_MAX on error => -1 */
		return bb_strtoul(buf, NULL, 10);
	}
	return 0;
}
#endif

int ifplugd_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int ifplugd_main(int argc UNUSED_PARAM, char **argv)
{
	int iface_status;
	int delay_time;
	const char *iface_status_str;
	struct pollfd netlink_pollfd[1];
	unsigned opts;
	const char *api_mode_found;
#if ENABLE_FEATURE_PIDFILE
	char *pidfile_name;
	pid_t pid_from_pidfile;
#endif

	INIT_G();

	opts = getopt32(argv, OPTION_STR,
		&G.iface, &G.script_name, &G.poll_time, &G.delay_up,
		&G.delay_down, &G.api_mode, &G.extra_arg);
	G.poll_time *= 1000;

	applet_name = xasprintf("ifplugd(%s)", G.iface);

#if ENABLE_FEATURE_PIDFILE
	pidfile_name = xasprintf(CONFIG_PID_FILE_PATH "/ifplugd.%s.pid", G.iface);
	pid_from_pidfile = read_pid(pidfile_name);

	if (opts & FLAG_KILL) {
		if (pid_from_pidfile > 0)
			/* Upstream tool use SIGINT for -k */
			kill(pid_from_pidfile, SIGINT);
		return EXIT_SUCCESS;
	}

	if (pid_from_pidfile > 0 && kill(pid_from_pidfile, 0) == 0)
		bb_error_msg_and_die("daemon already running");
#endif

	api_mode_found = strchr(api_modes, G.api_mode[0]);
	if (!api_mode_found)
		bb_error_msg_and_die("unknown API mode '%s'", G.api_mode);
	G.api_method_num = api_mode_found - api_modes;

	if (!(opts & FLAG_NO_DAEMON))
		bb_daemonize_or_rexec(DAEMON_CHDIR_ROOT, argv);

	xmove_fd(xsocket(AF_INET, SOCK_DGRAM, 0), ioctl_fd);
	if (opts & FLAG_MONITOR) {
		struct sockaddr_nl addr;
		int fd = xsocket(PF_NETLINK, SOCK_DGRAM, NETLINK_ROUTE);

		memset(&addr, 0, sizeof(addr));
		addr.nl_family = AF_NETLINK;
		addr.nl_groups = RTMGRP_LINK;
		addr.nl_pid = getpid();

		xbind(fd, (struct sockaddr*)&addr, sizeof(addr));
		xmove_fd(fd, netlink_fd);
	}

	write_pidfile(pidfile_name);

	/* this can't be moved before socket creation */
	if (!(opts & FLAG_NO_SYSLOG)) {
		openlog(applet_name, 0, LOG_DAEMON);
		logmode |= LOGMODE_SYSLOG;
	}

	bb_signals(0
		| (1 << SIGINT )
		| (1 << SIGTERM)
		| (1 << SIGQUIT)
		| (1 << SIGHUP ) /* why we ignore it? */
		/* | (1 << SIGCHLD) - run_script does not use it anymore */
		, record_signo);

	bb_error_msg("started: %s", bb_banner);

	if (opts & FLAG_MONITOR) {
		struct ifreq ifrequest;
		set_ifreq_to_ifname(&ifrequest);
		G.iface_exists = (network_ioctl(SIOCGIFINDEX, &ifrequest, NULL) == 0);
	}

	if (G.iface_exists)
		maybe_up_new_iface();

	iface_status = detect_link();
	if (iface_status == IFSTATUS_ERR)
		goto exiting;
	iface_status_str = strstatus(iface_status);

	if (opts & FLAG_MONITOR) {
		bb_error_msg("interface %s",
			G.iface_exists ? "exists"
			: "doesn't exist, waiting");
	}
	/* else we assume it always exists, but don't mislead user
	 * by potentially lying that it really exists */

	if (G.iface_exists) {
		bb_error_msg("link is %s", iface_status_str);
	}

	if ((!(opts & FLAG_NO_STARTUP)
	     && iface_status == IFSTATUS_UP
	    )
	 || (opts & FLAG_INITIAL_DOWN)
	) {
		if (run_script(iface_status_str) != 0)
			goto exiting;
	}

	/* Main loop */
	netlink_pollfd[0].fd = netlink_fd;
	netlink_pollfd[0].events = POLLIN;
	delay_time = 0;
	while (1) {
		int iface_status_old;

		switch (bb_got_signal) {
		case SIGINT:
		case SIGTERM:
			bb_got_signal = 0;
			goto cleanup;
		case SIGQUIT:
			bb_got_signal = 0;
			goto exiting;
		default:
			bb_got_signal = 0;
		/* do not clear bb_got_signal if already 0, this can lose signals */
		case 0:
			break;
		}

		if (poll(netlink_pollfd,
				(opts & FLAG_MONITOR) ? 1 : 0,
				G.poll_time
			) < 0
		) {
			if (errno == EINTR)
				continue;
			bb_perror_msg("poll");
			goto exiting;
		}

		if ((opts & FLAG_MONITOR)
		 && (netlink_pollfd[0].revents & POLLIN)
		) {
			int iface_exists_old;

			iface_exists_old = G.iface_exists;
			G.iface_exists = check_existence_through_netlink();
			if (G.iface_exists < 0) /* error */
				goto exiting;
			if (iface_exists_old != G.iface_exists) {
				bb_error_msg("interface %sappeared",
						G.iface_exists ? "" : "dis");
				if (G.iface_exists)
					maybe_up_new_iface();
			}
		}

		/* note: if !G.iface_exists, returns DOWN */
		iface_status_old = iface_status;
		iface_status = detect_link();
		if (iface_status == IFSTATUS_ERR) {
			if (!(opts & FLAG_MONITOR))
				goto exiting;
			iface_status = IFSTATUS_DOWN;
		}
		iface_status_str = strstatus(iface_status);

		if (iface_status_old != iface_status) {
			bb_error_msg("link is %s", iface_status_str);

			if (delay_time) {
				/* link restored its old status before
				 * we ran script. don't run the script: */
				delay_time = 0;
			} else {
				delay_time = monotonic_sec();
				if (iface_status == IFSTATUS_UP)
					delay_time += G.delay_up;
				if (iface_status == IFSTATUS_DOWN)
					delay_time += G.delay_down;
#if 0  /* if you are back in 1970... */
				if (delay_time == 0) {
					sleep(1);
					delay_time = 1;
				}
#endif
			}
		}

		if (delay_time && (int)(monotonic_sec() - delay_time) >= 0) {
			if (run_script(iface_status_str) != 0)
				goto exiting;
			delay_time = 0;
		}
	} /* while (1) */

 cleanup:
	if (!(opts & FLAG_NO_SHUTDOWN)
	 && (iface_status == IFSTATUS_UP
	     || (iface_status == IFSTATUS_DOWN && delay_time)
	    )
	) {
		setenv(IFPLUGD_ENV_PREVIOUS, strstatus(iface_status), 1);
		setenv(IFPLUGD_ENV_CURRENT, strstatus(-1), 1);
		run_script("down\0up"); /* reusing string */
	}

 exiting:
	remove_pidfile(pidfile_name);
	bb_error_msg_and_die("exiting");
}
