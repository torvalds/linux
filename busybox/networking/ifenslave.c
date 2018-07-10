/* Mode: C;
 *
 * Mini ifenslave implementation for busybox
 * Copyright (C) 2005 by Marc Leeman <marc.leeman@barco.com>
 *
 * ifenslave.c: Configure network interfaces for parallel routing.
 *
 *      This program controls the Linux implementation of running multiple
 *      network interfaces in parallel.
 *
 * Author:      Donald Becker <becker@cesdis.gsfc.nasa.gov>
 *              Copyright 1994-1996 Donald Becker
 *
 *              This program is free software; you can redistribute it
 *              and/or modify it under the terms of the GNU General Public
 *              License as published by the Free Software Foundation.
 *
 *      The author may be reached as becker@CESDIS.gsfc.nasa.gov, or C/O
 *      Center of Excellence in Space Data and Information Sciences
 *         Code 930.5, Goddard Space Flight Center, Greenbelt MD 20771
 *
 *  Changes :
 *    - 2000/10/02 Willy Tarreau <willy at meta-x.org> :
 *       - few fixes. Master's MAC address is now correctly taken from
 *         the first device when not previously set ;
 *       - detach support : call BOND_RELEASE to detach an enslaved interface.
 *       - give a mini-howto from command-line help : # ifenslave -h
 *
 *    - 2001/02/16 Chad N. Tindel <ctindel at ieee dot org> :
 *       - Master is now brought down before setting the MAC address.  In
 *         the 2.4 kernel you can't change the MAC address while the device is
 *         up because you get EBUSY.
 *
 *    - 2001/09/13 Takao Indoh <indou dot takao at jp dot fujitsu dot com>
 *       - Added the ability to change the active interface on a mode 1 bond
 *         at runtime.
 *
 *    - 2001/10/23 Chad N. Tindel <ctindel at ieee dot org> :
 *       - No longer set the MAC address of the master.  The bond device will
 *         take care of this itself
 *       - Try the SIOC*** versions of the bonding ioctls before using the
 *         old versions
 *    - 2002/02/18 Erik Habbinga <erik_habbinga @ hp dot com> :
 *       - ifr2.ifr_flags was not initialized in the hwaddr_notset case,
 *         SIOCGIFFLAGS now called before hwaddr_notset test
 *
 *    - 2002/10/31 Tony Cureington <tony.cureington * hp_com> :
 *       - If the master does not have a hardware address when the first slave
 *         is enslaved, the master is assigned the hardware address of that
 *         slave - there is a comment in bonding.c stating "ifenslave takes
 *         care of this now." This corrects the problem of slaves having
 *         different hardware addresses in active-backup mode when
 *         multiple interfaces are specified on a single ifenslave command
 *         (ifenslave bond0 eth0 eth1).
 *
 *    - 2003/03/18 - Tsippy Mendelson <tsippy.mendelson at intel dot com> and
 *                   Shmulik Hen <shmulik.hen at intel dot com>
 *       - Moved setting the slave's mac address and opening it, from
 *         the application to the driver. This enables support of modes
 *         that need to use the unique mac address of each slave.
 *         The driver also takes care of closing the slave and restoring its
 *         original mac address upon release.
 *         In addition, block possibility of enslaving before the master is up.
 *         This prevents putting the system in an undefined state.
 *
 *    - 2003/05/01 - Amir Noam <amir.noam at intel dot com>
 *       - Added ABI version control to restore compatibility between
 *         new/old ifenslave and new/old bonding.
 *       - Prevent adding an adapter that is already a slave.
 *         Fixes the problem of stalling the transmission and leaving
 *         the slave in a down state.
 *
 *    - 2003/05/01 - Shmulik Hen <shmulik.hen at intel dot com>
 *       - Prevent enslaving if the bond device is down.
 *         Fixes the problem of leaving the system in unstable state and
 *         halting when trying to remove the module.
 *       - Close socket on all abnormal exists.
 *       - Add versioning scheme that follows that of the bonding driver.
 *         current version is 1.0.0 as a base line.
 *
 *    - 2003/05/22 - Jay Vosburgh <fubar at us dot ibm dot com>
 *       - ifenslave -c was broken; it's now fixed
 *       - Fixed problem with routes vanishing from master during enslave
 *         processing.
 *
 *    - 2003/05/27 - Amir Noam <amir.noam at intel dot com>
 *       - Fix backward compatibility issues:
 *         For drivers not using ABI versions, slave was set down while
 *         it should be left up before enslaving.
 *         Also, master was not set down and the default set_mac_address()
 *         would fail and generate an error message in the system log.
 *       - For opt_c: slave should not be set to the master's setting
 *         while it is running. It was already set during enslave. To
 *         simplify things, it is now handeled separately.
 *
 *    - 2003/12/01 - Shmulik Hen <shmulik.hen at intel dot com>
 *       - Code cleanup and style changes
 *         set version to 1.1.0
 */
//config:config IFENSLAVE
//config:	bool "ifenslave (13 kb)"
//config:	default y
//config:	select PLATFORM_LINUX
//config:	help
//config:	Userspace application to bind several interfaces
//config:	to a logical interface (use with kernel bonding driver).

//applet:IF_IFENSLAVE(APPLET_NOEXEC(ifenslave, ifenslave, BB_DIR_SBIN, BB_SUID_DROP, ifenslave))

//kbuild:lib-$(CONFIG_IFENSLAVE) += ifenslave.o interface.o

//usage:#define ifenslave_trivial_usage
//usage:       "[-cdf] MASTER_IFACE SLAVE_IFACE..."
//usage:#define ifenslave_full_usage "\n\n"
//usage:       "Configure network interfaces for parallel routing\n"
//usage:     "\n	-c	Change active slave"
//usage:     "\n	-d	Remove slave interface from bonding device"
//usage:     "\n	-f	Force, even if interface is not Ethernet"
/* //usage:  "\n	-r	Create a receive-only slave" */
//usage:
//usage:#define ifenslave_example_usage
//usage:       "To create a bond device, simply follow these three steps:\n"
//usage:       "- ensure that the required drivers are properly loaded:\n"
//usage:       "  # modprobe bonding ; modprobe <3c59x|eepro100|pcnet32|tulip|...>\n"
//usage:       "- assign an IP address to the bond device:\n"
//usage:       "  # ifconfig bond0 <addr> netmask <mask> broadcast <bcast>\n"
//usage:       "- attach all the interfaces you need to the bond device:\n"
//usage:       "  # ifenslave bond0 eth0 eth1 eth2\n"
//usage:       "  If bond0 didn't have a MAC address, it will take eth0's. Then, all\n"
//usage:       "  interfaces attached AFTER this assignment will get the same MAC addr.\n\n"
//usage:       "  To detach a dead interface without setting the bond device down:\n"
//usage:       "  # ifenslave -d bond0 eth1\n\n"
//usage:       "  To set the bond device down and automatically release all the slaves:\n"
//usage:       "  # ifconfig bond0 down\n\n"
//usage:       "  To change active slave:\n"
//usage:       "  # ifenslave -c bond0 eth0\n"

#include "libbb.h"

/* #include <net/if.h> - no. linux/if_bonding.h pulls in linux/if.h */
#include <linux/if.h>
//#include <net/if_arp.h> - not needed?
#include <linux/if_bonding.h>
#include <linux/sockios.h>
#include "fix_u32.h" /* hack, so we may include kernel's ethtool.h */
#include <linux/ethtool.h>
#ifndef BOND_ABI_VERSION
# define BOND_ABI_VERSION 2
#endif
#ifndef IFNAMSIZ
# define IFNAMSIZ 16
#endif


struct dev_data {
	struct ifreq mtu, flags, hwaddr;
};


enum { skfd = 3 };      /* AF_INET socket for ioctl() calls. */
struct globals {
	unsigned abi_ver;       /* userland - kernel ABI version */
	smallint hwaddr_set;    /* Master's hwaddr is set */
	struct dev_data master;
	struct dev_data slave;
};
#define G (*ptr_to_globals)
#define abi_ver    (G.abi_ver   )
#define hwaddr_set (G.hwaddr_set)
#define master     (G.master    )
#define slave      (G.slave     )
#define INIT_G() do { \
	SET_PTR_TO_GLOBALS(xzalloc(sizeof(G))); \
} while (0)


/* NOINLINEs are placed where it results in smaller code (gcc 4.3.1) */

static int ioctl_on_skfd(unsigned request, struct ifreq *ifr)
{
	return ioctl(skfd, request, ifr);
}

static int set_ifrname_and_do_ioctl(unsigned request, struct ifreq *ifr, const char *ifname)
{
	strncpy_IFNAMSIZ(ifr->ifr_name, ifname);
	return ioctl_on_skfd(request, ifr);
}

static int get_if_settings(char *ifname, struct dev_data *dd)
{
	int res;

	res = set_ifrname_and_do_ioctl(SIOCGIFMTU, &dd->mtu, ifname);
	res |= set_ifrname_and_do_ioctl(SIOCGIFFLAGS, &dd->flags, ifname);
	res |= set_ifrname_and_do_ioctl(SIOCGIFHWADDR, &dd->hwaddr, ifname);

	return res;
}

static int get_slave_flags(char *slave_ifname)
{
	return set_ifrname_and_do_ioctl(SIOCGIFFLAGS, &slave.flags, slave_ifname);
}

static int set_hwaddr(char *ifname, struct sockaddr *hwaddr)
{
	struct ifreq ifr;

	memcpy(&(ifr.ifr_hwaddr), hwaddr, sizeof(*hwaddr));
	return set_ifrname_and_do_ioctl(SIOCSIFHWADDR, &ifr, ifname);
}

static int set_mtu(char *ifname, int mtu)
{
	struct ifreq ifr;

	ifr.ifr_mtu = mtu;
	return set_ifrname_and_do_ioctl(SIOCSIFMTU, &ifr, ifname);
}

static int set_if_flags(char *ifname, int flags)
{
	struct ifreq ifr;

	ifr.ifr_flags = flags;
	return set_ifrname_and_do_ioctl(SIOCSIFFLAGS, &ifr, ifname);
}

static int set_if_up(char *ifname, int flags)
{
	int res = set_if_flags(ifname, flags | IFF_UP);
	if (res)
		bb_perror_msg("%s: can't up", ifname);
	return res;
}

static int set_if_down(char *ifname, int flags)
{
	int res = set_if_flags(ifname, flags & ~IFF_UP);
	if (res)
		bb_perror_msg("%s: can't down", ifname);
	return res;
}

static int clear_if_addr(char *ifname)
{
	struct ifreq ifr;

	ifr.ifr_addr.sa_family = AF_INET;
	memset(ifr.ifr_addr.sa_data, 0, sizeof(ifr.ifr_addr.sa_data));
	return set_ifrname_and_do_ioctl(SIOCSIFADDR, &ifr, ifname);
}

static int set_if_addr(char *master_ifname, char *slave_ifname)
{
#if (SIOCGIFADDR | SIOCSIFADDR \
  | SIOCGIFDSTADDR | SIOCSIFDSTADDR \
  | SIOCGIFBRDADDR | SIOCSIFBRDADDR \
  | SIOCGIFNETMASK | SIOCSIFNETMASK) <= 0xffff
#define INT uint16_t
#else
#define INT int
#endif
	static const struct {
		INT g_ioctl;
		INT s_ioctl;
	} ifra[] = {
		{ SIOCGIFADDR,    SIOCSIFADDR    },
		{ SIOCGIFDSTADDR, SIOCSIFDSTADDR },
		{ SIOCGIFBRDADDR, SIOCSIFBRDADDR },
		{ SIOCGIFNETMASK, SIOCSIFNETMASK },
	};

	struct ifreq ifr;
	int res;
	unsigned i;

	for (i = 0; i < ARRAY_SIZE(ifra); i++) {
		res = set_ifrname_and_do_ioctl(ifra[i].g_ioctl, &ifr, master_ifname);
		if (res < 0) {
			ifr.ifr_addr.sa_family = AF_INET;
			memset(ifr.ifr_addr.sa_data, 0,
				sizeof(ifr.ifr_addr.sa_data));
		}

		res = set_ifrname_and_do_ioctl(ifra[i].s_ioctl, &ifr, slave_ifname);
		if (res < 0)
			return res;
	}

	return 0;
}

static void change_active(char *master_ifname, char *slave_ifname)
{
	struct ifreq ifr;

	if (!(slave.flags.ifr_flags & IFF_SLAVE)) {
		bb_error_msg_and_die("%s is not a slave", slave_ifname);
	}

	strncpy_IFNAMSIZ(ifr.ifr_slave, slave_ifname);
	if (set_ifrname_and_do_ioctl(SIOCBONDCHANGEACTIVE, &ifr, master_ifname)
	 && ioctl_on_skfd(BOND_CHANGE_ACTIVE_OLD, &ifr)
	) {
		bb_perror_msg_and_die(
			"master %s, slave %s: can't "
			"change active",
			master_ifname, slave_ifname);
	}
}

static NOINLINE int enslave(char *master_ifname, char *slave_ifname)
{
	struct ifreq ifr;
	int res;

	if (slave.flags.ifr_flags & IFF_SLAVE) {
		bb_error_msg(
			"%s is already a slave",
			slave_ifname);
		return 1;
	}

	res = set_if_down(slave_ifname, slave.flags.ifr_flags);
	if (res)
		return res;

	if (abi_ver < 2) {
		/* Older bonding versions would panic if the slave has no IP
		 * address, so get the IP setting from the master.
		 */
		res = set_if_addr(master_ifname, slave_ifname);
		if (res) {
			bb_perror_msg("%s: can't set address", slave_ifname);
			return res;
		}
	} else {
		res = clear_if_addr(slave_ifname);
		if (res) {
			bb_perror_msg("%s: can't clear address", slave_ifname);
			return res;
		}
	}

	if (master.mtu.ifr_mtu != slave.mtu.ifr_mtu) {
		res = set_mtu(slave_ifname, master.mtu.ifr_mtu);
		if (res) {
			bb_perror_msg("%s: can't set MTU", slave_ifname);
			return res;
		}
	}

	if (hwaddr_set) {
		/* Master already has an hwaddr
		 * so set it's hwaddr to the slave
		 */
		if (abi_ver < 1) {
			/* The driver is using an old ABI, so
			 * the application sets the slave's
			 * hwaddr
			 */
			if (set_hwaddr(slave_ifname, &(master.hwaddr.ifr_hwaddr))) {
				bb_perror_msg("%s: can't set hw address",
						slave_ifname);
				goto undo_mtu;
			}

			/* For old ABI the application needs to bring the
			 * slave back up
			 */
			if (set_if_up(slave_ifname, slave.flags.ifr_flags))
				goto undo_slave_mac;
		}
		/* The driver is using a new ABI,
		 * so the driver takes care of setting
		 * the slave's hwaddr and bringing
		 * it up again
		 */
	} else {
		/* No hwaddr for master yet, so
		 * set the slave's hwaddr to it
		 */
		if (abi_ver < 1) {
			/* For old ABI, the master needs to be
			 * down before setting it's hwaddr
			 */
			if (set_if_down(master_ifname, master.flags.ifr_flags))
				goto undo_mtu;
		}

		if (set_hwaddr(master_ifname, &(slave.hwaddr.ifr_hwaddr))) {
			bb_error_msg("%s: can't set hw address",
				master_ifname);
			goto undo_mtu;
		}

		if (abi_ver < 1) {
			/* For old ABI, bring the master
			 * back up
			 */
			if (set_if_up(master_ifname, master.flags.ifr_flags))
				goto undo_master_mac;
		}

		hwaddr_set = 1;
	}

	/* Do the real thing */
	strncpy_IFNAMSIZ(ifr.ifr_slave, slave_ifname);
	if (set_ifrname_and_do_ioctl(SIOCBONDENSLAVE, &ifr, master_ifname)
	 && ioctl_on_skfd(BOND_ENSLAVE_OLD, &ifr)
	) {
		goto undo_master_mac;
	}

	return 0;

/* rollback (best effort) */
 undo_master_mac:
	set_hwaddr(master_ifname, &(master.hwaddr.ifr_hwaddr));
	hwaddr_set = 0;
	goto undo_mtu;

 undo_slave_mac:
	set_hwaddr(slave_ifname, &(slave.hwaddr.ifr_hwaddr));
 undo_mtu:
	set_mtu(slave_ifname, slave.mtu.ifr_mtu);
	return 1;
}

static int release(char *master_ifname, char *slave_ifname)
{
	struct ifreq ifr;
	int res = 0;

	if (!(slave.flags.ifr_flags & IFF_SLAVE)) {
		bb_error_msg("%s is not a slave", slave_ifname);
		return 1;
	}

	strncpy_IFNAMSIZ(ifr.ifr_slave, slave_ifname);
	if (set_ifrname_and_do_ioctl(SIOCBONDRELEASE, &ifr, master_ifname) < 0
	 && ioctl_on_skfd(BOND_RELEASE_OLD, &ifr) < 0
	) {
		return 1;
	}
	if (abi_ver < 1) {
		/* The driver is using an old ABI, so we'll set the interface
		 * down to avoid any conflicts due to same MAC/IP
		 */
		res = set_if_down(slave_ifname, slave.flags.ifr_flags);
	}

	/* set to default mtu */
	set_mtu(slave_ifname, 1500);

	return res;
}

static NOINLINE void get_drv_info(char *master_ifname)
{
	struct ifreq ifr;
	struct ethtool_drvinfo info;

	memset(&ifr, 0, sizeof(ifr));
	ifr.ifr_data = (caddr_t)&info;
	info.cmd = ETHTOOL_GDRVINFO;
	/* both fields are 32 bytes long (long enough) */
	strcpy(info.driver, "ifenslave");
	strcpy(info.fw_version, utoa(BOND_ABI_VERSION));
	if (set_ifrname_and_do_ioctl(SIOCETHTOOL, &ifr, master_ifname) < 0) {
		if (errno == EOPNOTSUPP)
			return;
		bb_perror_msg_and_die("%s: SIOCETHTOOL error", master_ifname);
	}

	abi_ver = bb_strtou(info.fw_version, NULL, 0);
	if (errno)
		bb_error_msg_and_die("%s: SIOCETHTOOL error", master_ifname);
}

int ifenslave_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int ifenslave_main(int argc UNUSED_PARAM, char **argv)
{
	char *master_ifname, *slave_ifname;
	int rv;
	int res;
	unsigned opt;
	enum {
		OPT_c = (1 << 0),
		OPT_d = (1 << 1),
		OPT_f = (1 << 2),
	};

	INIT_G();

	opt = getopt32long(argv, "cdfa",
		"change-active\0"  No_argument "c"
		"detach\0"         No_argument "d"
		"force\0"          No_argument "f"
		/* "all-interfaces\0" No_argument "a" */
	);
	argv += optind;
	if (opt & (opt-1)) /* Only one option can be given */
		bb_show_usage();

	master_ifname = *argv++;

	/* No interface names - show all interfaces. */
	if (!master_ifname) {
		display_interfaces(NULL);
		return EXIT_SUCCESS;
	}

	/* Open a basic socket */
	xmove_fd(xsocket(AF_INET, SOCK_DGRAM, 0), skfd);

	/* Exchange abi version with bonding module */
	get_drv_info(master_ifname);

	slave_ifname = *argv++;
	if (!slave_ifname) {
		if (opt & (OPT_d|OPT_c)) {
			/* --change or --detach, and no slaves given -
			 * show all interfaces. */
			display_interfaces(slave_ifname /* == NULL */);
			return 2; /* why 2? */
		}
		/* A single arg means show the
		 * configuration for this interface
		 */
		display_interfaces(master_ifname);
		return EXIT_SUCCESS;
	}

	if (get_if_settings(master_ifname, &master)) {
		/* Probably a good reason not to go on */
		bb_perror_msg_and_die("%s: can't get settings", master_ifname);
	}

	/* Check if master is indeed a master;
	 * if not then fail any operation
	 */
	if (!(master.flags.ifr_flags & IFF_MASTER))
		bb_error_msg_and_die("%s is not a master", master_ifname);

	/* Check if master is up; if not then fail any operation */
	if (!(master.flags.ifr_flags & IFF_UP))
		bb_error_msg_and_die("%s is not up", master_ifname);

#ifdef WHY_BOTHER
	/* Neither -c[hange] nor -d[etach] -> it's "enslave" then;
	 * and -f[orce] is not there too. Check that it's ethernet. */
	if (!(opt & (OPT_d|OPT_c|OPT_f))) {
		/* The family '1' is ARPHRD_ETHER for ethernet. */
		if (master.hwaddr.ifr_hwaddr.sa_family != 1) {
			bb_error_msg_and_die(
				"%s is not ethernet-like (-f overrides)",
				master_ifname);
		}
	}
#endif

	/* Accepts only one slave */
	if (opt & OPT_c) {
		/* Change active slave */
		if (get_slave_flags(slave_ifname)) {
			bb_perror_msg_and_die(
				"%s: can't get flags", slave_ifname);
		}
		change_active(master_ifname, slave_ifname);
		return EXIT_SUCCESS;
	}

	/* Accepts multiple slaves */
	res = 0;
	do {
		if (opt & OPT_d) {
			/* Detach a slave interface from the master */
			rv = get_slave_flags(slave_ifname);
			if (rv) {
				/* Can't work with this slave, */
				/* remember the error and skip it */
				bb_perror_msg(
					"skipping %s: can't get %s",
					slave_ifname, "flags");
				res = rv;
				continue;
			}
			rv = release(master_ifname, slave_ifname);
			if (rv) {
				bb_perror_msg("can't release %s from %s",
					slave_ifname, master_ifname);
				res = rv;
			}
		} else {
			/* Attach a slave interface to the master */
			rv = get_if_settings(slave_ifname, &slave);
			if (rv) {
				/* Can't work with this slave, */
				/* remember the error and skip it */
				bb_perror_msg(
					"skipping %s: can't get %s",
					slave_ifname, "settings");
				res = rv;
				continue;
			}
			rv = enslave(master_ifname, slave_ifname);
			if (rv) {
				bb_perror_msg("can't enslave %s to %s",
					slave_ifname, master_ifname);
				res = rv;
			}
		}
	} while ((slave_ifname = *argv++) != NULL);

	if (ENABLE_FEATURE_CLEAN_UP) {
		close(skfd);
	}

	return res;
}
