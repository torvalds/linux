// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * drivers/net/bond/bond_options.c - bonding options
 * Copyright (c) 2013 Jiri Pirko <jiri@resnulli.us>
 * Copyright (c) 2013 Scott Feldman <sfeldma@cumulusnetworks.com>
 */

#include <linux/errno.h>
#include <linux/if.h>
#include <linux/netdevice.h>
#include <linux/spinlock.h>
#include <linux/rcupdate.h>
#include <linux/ctype.h>
#include <linux/inet.h>
#include <linux/sched/signal.h>

#include <net/bonding.h>

static int bond_option_active_slave_set(struct bonding *bond,
					const struct bond_opt_value *newval);
static int bond_option_miimon_set(struct bonding *bond,
				  const struct bond_opt_value *newval);
static int bond_option_updelay_set(struct bonding *bond,
				   const struct bond_opt_value *newval);
static int bond_option_downdelay_set(struct bonding *bond,
				     const struct bond_opt_value *newval);
static int bond_option_peer_notif_delay_set(struct bonding *bond,
					    const struct bond_opt_value *newval);
static int bond_option_use_carrier_set(struct bonding *bond,
				       const struct bond_opt_value *newval);
static int bond_option_arp_interval_set(struct bonding *bond,
					const struct bond_opt_value *newval);
static int bond_option_arp_ip_target_add(struct bonding *bond, __be32 target);
static int bond_option_arp_ip_target_rem(struct bonding *bond, __be32 target);
static int bond_option_arp_ip_targets_set(struct bonding *bond,
					  const struct bond_opt_value *newval);
static int bond_option_ns_ip6_targets_set(struct bonding *bond,
					  const struct bond_opt_value *newval);
static int bond_option_arp_validate_set(struct bonding *bond,
					const struct bond_opt_value *newval);
static int bond_option_arp_all_targets_set(struct bonding *bond,
					   const struct bond_opt_value *newval);
static int bond_option_primary_set(struct bonding *bond,
				   const struct bond_opt_value *newval);
static int bond_option_primary_reselect_set(struct bonding *bond,
					    const struct bond_opt_value *newval);
static int bond_option_fail_over_mac_set(struct bonding *bond,
					 const struct bond_opt_value *newval);
static int bond_option_xmit_hash_policy_set(struct bonding *bond,
					    const struct bond_opt_value *newval);
static int bond_option_resend_igmp_set(struct bonding *bond,
				       const struct bond_opt_value *newval);
static int bond_option_num_peer_notif_set(struct bonding *bond,
					  const struct bond_opt_value *newval);
static int bond_option_all_slaves_active_set(struct bonding *bond,
					     const struct bond_opt_value *newval);
static int bond_option_min_links_set(struct bonding *bond,
				     const struct bond_opt_value *newval);
static int bond_option_lp_interval_set(struct bonding *bond,
				       const struct bond_opt_value *newval);
static int bond_option_pps_set(struct bonding *bond,
			       const struct bond_opt_value *newval);
static int bond_option_lacp_active_set(struct bonding *bond,
				       const struct bond_opt_value *newval);
static int bond_option_lacp_rate_set(struct bonding *bond,
				     const struct bond_opt_value *newval);
static int bond_option_ad_select_set(struct bonding *bond,
				     const struct bond_opt_value *newval);
static int bond_option_queue_id_set(struct bonding *bond,
				    const struct bond_opt_value *newval);
static int bond_option_mode_set(struct bonding *bond,
				const struct bond_opt_value *newval);
static int bond_option_slaves_set(struct bonding *bond,
				  const struct bond_opt_value *newval);
static int bond_option_tlb_dynamic_lb_set(struct bonding *bond,
				  const struct bond_opt_value *newval);
static int bond_option_ad_actor_sys_prio_set(struct bonding *bond,
					     const struct bond_opt_value *newval);
static int bond_option_ad_actor_system_set(struct bonding *bond,
					   const struct bond_opt_value *newval);
static int bond_option_ad_user_port_key_set(struct bonding *bond,
					    const struct bond_opt_value *newval);
static int bond_option_missed_max_set(struct bonding *bond,
				      const struct bond_opt_value *newval);


static const struct bond_opt_value bond_mode_tbl[] = {
	{ "balance-rr",    BOND_MODE_ROUNDROBIN,   BOND_VALFLAG_DEFAULT},
	{ "active-backup", BOND_MODE_ACTIVEBACKUP, 0},
	{ "balance-xor",   BOND_MODE_XOR,          0},
	{ "broadcast",     BOND_MODE_BROADCAST,    0},
	{ "802.3ad",       BOND_MODE_8023AD,       0},
	{ "balance-tlb",   BOND_MODE_TLB,          0},
	{ "balance-alb",   BOND_MODE_ALB,          0},
	{ NULL,            -1,                     0},
};

static const struct bond_opt_value bond_pps_tbl[] = {
	{ "default", 1,         BOND_VALFLAG_DEFAULT},
	{ "maxval",  USHRT_MAX, BOND_VALFLAG_MAX},
	{ NULL,      -1,        0},
};

static const struct bond_opt_value bond_xmit_hashtype_tbl[] = {
	{ "layer2",      BOND_XMIT_POLICY_LAYER2,      BOND_VALFLAG_DEFAULT},
	{ "layer3+4",    BOND_XMIT_POLICY_LAYER34,     0},
	{ "layer2+3",    BOND_XMIT_POLICY_LAYER23,     0},
	{ "encap2+3",    BOND_XMIT_POLICY_ENCAP23,     0},
	{ "encap3+4",    BOND_XMIT_POLICY_ENCAP34,     0},
	{ "vlan+srcmac", BOND_XMIT_POLICY_VLAN_SRCMAC, 0},
	{ NULL,          -1,                           0},
};

static const struct bond_opt_value bond_arp_validate_tbl[] = {
	{ "none",		BOND_ARP_VALIDATE_NONE,		BOND_VALFLAG_DEFAULT},
	{ "active",		BOND_ARP_VALIDATE_ACTIVE,	0},
	{ "backup",		BOND_ARP_VALIDATE_BACKUP,	0},
	{ "all",		BOND_ARP_VALIDATE_ALL,		0},
	{ "filter",		BOND_ARP_FILTER,		0},
	{ "filter_active",	BOND_ARP_FILTER_ACTIVE,		0},
	{ "filter_backup",	BOND_ARP_FILTER_BACKUP,		0},
	{ NULL,			-1,				0},
};

static const struct bond_opt_value bond_arp_all_targets_tbl[] = {
	{ "any", BOND_ARP_TARGETS_ANY, BOND_VALFLAG_DEFAULT},
	{ "all", BOND_ARP_TARGETS_ALL, 0},
	{ NULL,  -1,                   0},
};

static const struct bond_opt_value bond_fail_over_mac_tbl[] = {
	{ "none",   BOND_FOM_NONE,   BOND_VALFLAG_DEFAULT},
	{ "active", BOND_FOM_ACTIVE, 0},
	{ "follow", BOND_FOM_FOLLOW, 0},
	{ NULL,     -1,              0},
};

static const struct bond_opt_value bond_intmax_tbl[] = {
	{ "off",     0,       BOND_VALFLAG_DEFAULT},
	{ "maxval",  INT_MAX, BOND_VALFLAG_MAX},
	{ NULL,      -1,      0}
};

static const struct bond_opt_value bond_lacp_active[] = {
	{ "off", 0,  0},
	{ "on",  1,  BOND_VALFLAG_DEFAULT},
	{ NULL,  -1, 0}
};

static const struct bond_opt_value bond_lacp_rate_tbl[] = {
	{ "slow", AD_LACP_SLOW, 0},
	{ "fast", AD_LACP_FAST, 0},
	{ NULL,   -1,           0},
};

static const struct bond_opt_value bond_ad_select_tbl[] = {
	{ "stable",    BOND_AD_STABLE,    BOND_VALFLAG_DEFAULT},
	{ "bandwidth", BOND_AD_BANDWIDTH, 0},
	{ "count",     BOND_AD_COUNT,     0},
	{ NULL,        -1,                0},
};

static const struct bond_opt_value bond_num_peer_notif_tbl[] = {
	{ "off",     0,   0},
	{ "maxval",  255, BOND_VALFLAG_MAX},
	{ "default", 1,   BOND_VALFLAG_DEFAULT},
	{ NULL,      -1,  0}
};

static const struct bond_opt_value bond_primary_reselect_tbl[] = {
	{ "always",  BOND_PRI_RESELECT_ALWAYS,  BOND_VALFLAG_DEFAULT},
	{ "better",  BOND_PRI_RESELECT_BETTER,  0},
	{ "failure", BOND_PRI_RESELECT_FAILURE, 0},
	{ NULL,      -1},
};

static const struct bond_opt_value bond_use_carrier_tbl[] = {
	{ "off", 0,  0},
	{ "on",  1,  BOND_VALFLAG_DEFAULT},
	{ NULL,  -1, 0}
};

static const struct bond_opt_value bond_all_slaves_active_tbl[] = {
	{ "off", 0,  BOND_VALFLAG_DEFAULT},
	{ "on",  1,  0},
	{ NULL,  -1, 0}
};

static const struct bond_opt_value bond_resend_igmp_tbl[] = {
	{ "off",     0,   0},
	{ "maxval",  255, BOND_VALFLAG_MAX},
	{ "default", 1,   BOND_VALFLAG_DEFAULT},
	{ NULL,      -1,  0}
};

static const struct bond_opt_value bond_lp_interval_tbl[] = {
	{ "minval",  1,       BOND_VALFLAG_MIN | BOND_VALFLAG_DEFAULT},
	{ "maxval",  INT_MAX, BOND_VALFLAG_MAX},
	{ NULL,      -1,      0},
};

static const struct bond_opt_value bond_tlb_dynamic_lb_tbl[] = {
	{ "off", 0,  0},
	{ "on",  1,  BOND_VALFLAG_DEFAULT},
	{ NULL,  -1, 0}
};

static const struct bond_opt_value bond_ad_actor_sys_prio_tbl[] = {
	{ "minval",  1,     BOND_VALFLAG_MIN},
	{ "maxval",  65535, BOND_VALFLAG_MAX | BOND_VALFLAG_DEFAULT},
	{ NULL,      -1,    0},
};

static const struct bond_opt_value bond_ad_user_port_key_tbl[] = {
	{ "minval",  0,     BOND_VALFLAG_MIN | BOND_VALFLAG_DEFAULT},
	{ "maxval",  1023,  BOND_VALFLAG_MAX},
	{ NULL,      -1,    0},
};

static const struct bond_opt_value bond_missed_max_tbl[] = {
	{ "minval",	1,	BOND_VALFLAG_MIN},
	{ "maxval",	255,	BOND_VALFLAG_MAX},
	{ "default",	2,	BOND_VALFLAG_DEFAULT},
	{ NULL,		-1,	0},
};

static const struct bond_option bond_opts[BOND_OPT_LAST] = {
	[BOND_OPT_MODE] = {
		.id = BOND_OPT_MODE,
		.name = "mode",
		.desc = "bond device mode",
		.flags = BOND_OPTFLAG_NOSLAVES | BOND_OPTFLAG_IFDOWN,
		.values = bond_mode_tbl,
		.set = bond_option_mode_set
	},
	[BOND_OPT_PACKETS_PER_SLAVE] = {
		.id = BOND_OPT_PACKETS_PER_SLAVE,
		.name = "packets_per_slave",
		.desc = "Packets to send per slave in RR mode",
		.unsuppmodes = BOND_MODE_ALL_EX(BIT(BOND_MODE_ROUNDROBIN)),
		.values = bond_pps_tbl,
		.set = bond_option_pps_set
	},
	[BOND_OPT_XMIT_HASH] = {
		.id = BOND_OPT_XMIT_HASH,
		.name = "xmit_hash_policy",
		.desc = "balance-xor, 802.3ad, and tlb hashing method",
		.values = bond_xmit_hashtype_tbl,
		.set = bond_option_xmit_hash_policy_set
	},
	[BOND_OPT_ARP_VALIDATE] = {
		.id = BOND_OPT_ARP_VALIDATE,
		.name = "arp_validate",
		.desc = "validate src/dst of ARP probes",
		.unsuppmodes = BIT(BOND_MODE_8023AD) | BIT(BOND_MODE_TLB) |
			       BIT(BOND_MODE_ALB),
		.values = bond_arp_validate_tbl,
		.set = bond_option_arp_validate_set
	},
	[BOND_OPT_ARP_ALL_TARGETS] = {
		.id = BOND_OPT_ARP_ALL_TARGETS,
		.name = "arp_all_targets",
		.desc = "fail on any/all arp targets timeout",
		.values = bond_arp_all_targets_tbl,
		.set = bond_option_arp_all_targets_set
	},
	[BOND_OPT_FAIL_OVER_MAC] = {
		.id = BOND_OPT_FAIL_OVER_MAC,
		.name = "fail_over_mac",
		.desc = "For active-backup, do not set all slaves to the same MAC",
		.flags = BOND_OPTFLAG_NOSLAVES,
		.values = bond_fail_over_mac_tbl,
		.set = bond_option_fail_over_mac_set
	},
	[BOND_OPT_ARP_INTERVAL] = {
		.id = BOND_OPT_ARP_INTERVAL,
		.name = "arp_interval",
		.desc = "arp interval in milliseconds",
		.unsuppmodes = BIT(BOND_MODE_8023AD) | BIT(BOND_MODE_TLB) |
			       BIT(BOND_MODE_ALB),
		.values = bond_intmax_tbl,
		.set = bond_option_arp_interval_set
	},
	[BOND_OPT_MISSED_MAX] = {
		.id = BOND_OPT_MISSED_MAX,
		.name = "arp_missed_max",
		.desc = "Maximum number of missed ARP interval",
		.unsuppmodes = BIT(BOND_MODE_8023AD) | BIT(BOND_MODE_TLB) |
			       BIT(BOND_MODE_ALB),
		.values = bond_missed_max_tbl,
		.set = bond_option_missed_max_set
	},
	[BOND_OPT_ARP_TARGETS] = {
		.id = BOND_OPT_ARP_TARGETS,
		.name = "arp_ip_target",
		.desc = "arp targets in n.n.n.n form",
		.flags = BOND_OPTFLAG_RAWVAL,
		.set = bond_option_arp_ip_targets_set
	},
	[BOND_OPT_NS_TARGETS] = {
		.id = BOND_OPT_NS_TARGETS,
		.name = "ns_ip6_target",
		.desc = "NS targets in ffff:ffff::ffff:ffff form",
		.flags = BOND_OPTFLAG_RAWVAL,
		.set = bond_option_ns_ip6_targets_set
	},
	[BOND_OPT_DOWNDELAY] = {
		.id = BOND_OPT_DOWNDELAY,
		.name = "downdelay",
		.desc = "Delay before considering link down, in milliseconds",
		.values = bond_intmax_tbl,
		.set = bond_option_downdelay_set
	},
	[BOND_OPT_UPDELAY] = {
		.id = BOND_OPT_UPDELAY,
		.name = "updelay",
		.desc = "Delay before considering link up, in milliseconds",
		.values = bond_intmax_tbl,
		.set = bond_option_updelay_set
	},
	[BOND_OPT_LACP_ACTIVE] = {
		.id = BOND_OPT_LACP_ACTIVE,
		.name = "lacp_active",
		.desc = "Send LACPDU frames with configured lacp rate or acts as speak when spoken to",
		.flags = BOND_OPTFLAG_IFDOWN,
		.unsuppmodes = BOND_MODE_ALL_EX(BIT(BOND_MODE_8023AD)),
		.values = bond_lacp_active,
		.set = bond_option_lacp_active_set
	},
	[BOND_OPT_LACP_RATE] = {
		.id = BOND_OPT_LACP_RATE,
		.name = "lacp_rate",
		.desc = "LACPDU tx rate to request from 802.3ad partner",
		.flags = BOND_OPTFLAG_IFDOWN,
		.unsuppmodes = BOND_MODE_ALL_EX(BIT(BOND_MODE_8023AD)),
		.values = bond_lacp_rate_tbl,
		.set = bond_option_lacp_rate_set
	},
	[BOND_OPT_MINLINKS] = {
		.id = BOND_OPT_MINLINKS,
		.name = "min_links",
		.desc = "Minimum number of available links before turning on carrier",
		.values = bond_intmax_tbl,
		.set = bond_option_min_links_set
	},
	[BOND_OPT_AD_SELECT] = {
		.id = BOND_OPT_AD_SELECT,
		.name = "ad_select",
		.desc = "803.ad aggregation selection logic",
		.flags = BOND_OPTFLAG_IFDOWN,
		.values = bond_ad_select_tbl,
		.set = bond_option_ad_select_set
	},
	[BOND_OPT_NUM_PEER_NOTIF] = {
		.id = BOND_OPT_NUM_PEER_NOTIF,
		.name = "num_unsol_na",
		.desc = "Number of peer notifications to send on failover event",
		.values = bond_num_peer_notif_tbl,
		.set = bond_option_num_peer_notif_set
	},
	[BOND_OPT_MIIMON] = {
		.id = BOND_OPT_MIIMON,
		.name = "miimon",
		.desc = "Link check interval in milliseconds",
		.values = bond_intmax_tbl,
		.set = bond_option_miimon_set
	},
	[BOND_OPT_PRIMARY] = {
		.id = BOND_OPT_PRIMARY,
		.name = "primary",
		.desc = "Primary network device to use",
		.flags = BOND_OPTFLAG_RAWVAL,
		.unsuppmodes = BOND_MODE_ALL_EX(BIT(BOND_MODE_ACTIVEBACKUP) |
						BIT(BOND_MODE_TLB) |
						BIT(BOND_MODE_ALB)),
		.set = bond_option_primary_set
	},
	[BOND_OPT_PRIMARY_RESELECT] = {
		.id = BOND_OPT_PRIMARY_RESELECT,
		.name = "primary_reselect",
		.desc = "Reselect primary slave once it comes up",
		.values = bond_primary_reselect_tbl,
		.set = bond_option_primary_reselect_set
	},
	[BOND_OPT_USE_CARRIER] = {
		.id = BOND_OPT_USE_CARRIER,
		.name = "use_carrier",
		.desc = "Use netif_carrier_ok (vs MII ioctls) in miimon",
		.values = bond_use_carrier_tbl,
		.set = bond_option_use_carrier_set
	},
	[BOND_OPT_ACTIVE_SLAVE] = {
		.id = BOND_OPT_ACTIVE_SLAVE,
		.name = "active_slave",
		.desc = "Currently active slave",
		.flags = BOND_OPTFLAG_RAWVAL,
		.unsuppmodes = BOND_MODE_ALL_EX(BIT(BOND_MODE_ACTIVEBACKUP) |
						BIT(BOND_MODE_TLB) |
						BIT(BOND_MODE_ALB)),
		.set = bond_option_active_slave_set
	},
	[BOND_OPT_QUEUE_ID] = {
		.id = BOND_OPT_QUEUE_ID,
		.name = "queue_id",
		.desc = "Set queue id of a slave",
		.flags = BOND_OPTFLAG_RAWVAL,
		.set = bond_option_queue_id_set
	},
	[BOND_OPT_ALL_SLAVES_ACTIVE] = {
		.id = BOND_OPT_ALL_SLAVES_ACTIVE,
		.name = "all_slaves_active",
		.desc = "Keep all frames received on an interface by setting active flag for all slaves",
		.values = bond_all_slaves_active_tbl,
		.set = bond_option_all_slaves_active_set
	},
	[BOND_OPT_RESEND_IGMP] = {
		.id = BOND_OPT_RESEND_IGMP,
		.name = "resend_igmp",
		.desc = "Number of IGMP membership reports to send on link failure",
		.values = bond_resend_igmp_tbl,
		.set = bond_option_resend_igmp_set
	},
	[BOND_OPT_LP_INTERVAL] = {
		.id = BOND_OPT_LP_INTERVAL,
		.name = "lp_interval",
		.desc = "The number of seconds between instances where the bonding driver sends learning packets to each slave's peer switch",
		.values = bond_lp_interval_tbl,
		.set = bond_option_lp_interval_set
	},
	[BOND_OPT_SLAVES] = {
		.id = BOND_OPT_SLAVES,
		.name = "slaves",
		.desc = "Slave membership management",
		.flags = BOND_OPTFLAG_RAWVAL,
		.set = bond_option_slaves_set
	},
	[BOND_OPT_TLB_DYNAMIC_LB] = {
		.id = BOND_OPT_TLB_DYNAMIC_LB,
		.name = "tlb_dynamic_lb",
		.desc = "Enable dynamic flow shuffling",
		.unsuppmodes = BOND_MODE_ALL_EX(BIT(BOND_MODE_TLB) | BIT(BOND_MODE_ALB)),
		.values = bond_tlb_dynamic_lb_tbl,
		.flags = BOND_OPTFLAG_IFDOWN,
		.set = bond_option_tlb_dynamic_lb_set,
	},
	[BOND_OPT_AD_ACTOR_SYS_PRIO] = {
		.id = BOND_OPT_AD_ACTOR_SYS_PRIO,
		.name = "ad_actor_sys_prio",
		.unsuppmodes = BOND_MODE_ALL_EX(BIT(BOND_MODE_8023AD)),
		.values = bond_ad_actor_sys_prio_tbl,
		.set = bond_option_ad_actor_sys_prio_set,
	},
	[BOND_OPT_AD_ACTOR_SYSTEM] = {
		.id = BOND_OPT_AD_ACTOR_SYSTEM,
		.name = "ad_actor_system",
		.unsuppmodes = BOND_MODE_ALL_EX(BIT(BOND_MODE_8023AD)),
		.flags = BOND_OPTFLAG_RAWVAL,
		.set = bond_option_ad_actor_system_set,
	},
	[BOND_OPT_AD_USER_PORT_KEY] = {
		.id = BOND_OPT_AD_USER_PORT_KEY,
		.name = "ad_user_port_key",
		.unsuppmodes = BOND_MODE_ALL_EX(BIT(BOND_MODE_8023AD)),
		.flags = BOND_OPTFLAG_IFDOWN,
		.values = bond_ad_user_port_key_tbl,
		.set = bond_option_ad_user_port_key_set,
	},
	[BOND_OPT_NUM_PEER_NOTIF_ALIAS] = {
		.id = BOND_OPT_NUM_PEER_NOTIF_ALIAS,
		.name = "num_grat_arp",
		.desc = "Number of peer notifications to send on failover event",
		.values = bond_num_peer_notif_tbl,
		.set = bond_option_num_peer_notif_set
	},
	[BOND_OPT_PEER_NOTIF_DELAY] = {
		.id = BOND_OPT_PEER_NOTIF_DELAY,
		.name = "peer_notif_delay",
		.desc = "Delay between each peer notification on failover event, in milliseconds",
		.values = bond_intmax_tbl,
		.set = bond_option_peer_notif_delay_set
	}
};

/* Searches for an option by name */
const struct bond_option *bond_opt_get_by_name(const char *name)
{
	const struct bond_option *opt;
	int option;

	for (option = 0; option < BOND_OPT_LAST; option++) {
		opt = bond_opt_get(option);
		if (opt && !strcmp(opt->name, name))
			return opt;
	}

	return NULL;
}

/* Searches for a value in opt's values[] table */
const struct bond_opt_value *bond_opt_get_val(unsigned int option, u64 val)
{
	const struct bond_option *opt;
	int i;

	opt = bond_opt_get(option);
	if (WARN_ON(!opt))
		return NULL;
	for (i = 0; opt->values && opt->values[i].string; i++)
		if (opt->values[i].value == val)
			return &opt->values[i];

	return NULL;
}

/* Searches for a value in opt's values[] table which matches the flagmask */
static const struct bond_opt_value *bond_opt_get_flags(const struct bond_option *opt,
						       u32 flagmask)
{
	int i;

	for (i = 0; opt->values && opt->values[i].string; i++)
		if (opt->values[i].flags & flagmask)
			return &opt->values[i];

	return NULL;
}

/* If maxval is missing then there's no range to check. In case minval is
 * missing then it's considered to be 0.
 */
static bool bond_opt_check_range(const struct bond_option *opt, u64 val)
{
	const struct bond_opt_value *minval, *maxval;

	minval = bond_opt_get_flags(opt, BOND_VALFLAG_MIN);
	maxval = bond_opt_get_flags(opt, BOND_VALFLAG_MAX);
	if (!maxval || (minval && val < minval->value) || val > maxval->value)
		return false;

	return true;
}

/**
 * bond_opt_parse - parse option value
 * @opt: the option to parse against
 * @val: value to parse
 *
 * This function tries to extract the value from @val and check if it's
 * a possible match for the option and returns NULL if a match isn't found,
 * or the struct_opt_value that matched. It also strips the new line from
 * @val->string if it's present.
 */
const struct bond_opt_value *bond_opt_parse(const struct bond_option *opt,
					    struct bond_opt_value *val)
{
	char *p, valstr[BOND_OPT_MAX_NAMELEN + 1] = { 0, };
	const struct bond_opt_value *tbl;
	const struct bond_opt_value *ret = NULL;
	bool checkval;
	int i, rv;

	/* No parsing if the option wants a raw val */
	if (opt->flags & BOND_OPTFLAG_RAWVAL)
		return val;

	tbl = opt->values;
	if (!tbl)
		goto out;

	/* ULLONG_MAX is used to bypass string processing */
	checkval = val->value != ULLONG_MAX;
	if (!checkval) {
		if (!val->string)
			goto out;
		p = strchr(val->string, '\n');
		if (p)
			*p = '\0';
		for (p = val->string; *p; p++)
			if (!(isdigit(*p) || isspace(*p)))
				break;
		/* The following code extracts the string to match or the value
		 * and sets checkval appropriately
		 */
		if (*p) {
			rv = sscanf(val->string, "%32s", valstr);
		} else {
			rv = sscanf(val->string, "%llu", &val->value);
			checkval = true;
		}
		if (!rv)
			goto out;
	}

	for (i = 0; tbl[i].string; i++) {
		/* Check for exact match */
		if (checkval) {
			if (val->value == tbl[i].value)
				ret = &tbl[i];
		} else {
			if (!strcmp(valstr, "default") &&
			    (tbl[i].flags & BOND_VALFLAG_DEFAULT))
				ret = &tbl[i];

			if (!strcmp(valstr, tbl[i].string))
				ret = &tbl[i];
		}
		/* Found an exact match */
		if (ret)
			goto out;
	}
	/* Possible range match */
	if (checkval && bond_opt_check_range(opt, val->value))
		ret = val;
out:
	return ret;
}

/* Check opt's dependencies against bond mode and currently set options */
static int bond_opt_check_deps(struct bonding *bond,
			       const struct bond_option *opt)
{
	struct bond_params *params = &bond->params;

	if (test_bit(params->mode, &opt->unsuppmodes))
		return -EACCES;
	if ((opt->flags & BOND_OPTFLAG_NOSLAVES) && bond_has_slaves(bond))
		return -ENOTEMPTY;
	if ((opt->flags & BOND_OPTFLAG_IFDOWN) && (bond->dev->flags & IFF_UP))
		return -EBUSY;

	return 0;
}

static void bond_opt_dep_print(struct bonding *bond,
			       const struct bond_option *opt)
{
	const struct bond_opt_value *modeval;
	struct bond_params *params;

	params = &bond->params;
	modeval = bond_opt_get_val(BOND_OPT_MODE, params->mode);
	if (test_bit(params->mode, &opt->unsuppmodes))
		netdev_err(bond->dev, "option %s: mode dependency failed, not supported in mode %s(%llu)\n",
			   opt->name, modeval->string, modeval->value);
}

static void bond_opt_error_interpret(struct bonding *bond,
				     const struct bond_option *opt,
				     int error, const struct bond_opt_value *val)
{
	const struct bond_opt_value *minval, *maxval;
	char *p;

	switch (error) {
	case -EINVAL:
		if (val) {
			if (val->string) {
				/* sometimes RAWVAL opts may have new lines */
				p = strchr(val->string, '\n');
				if (p)
					*p = '\0';
				netdev_err(bond->dev, "option %s: invalid value (%s)\n",
					   opt->name, val->string);
			} else {
				netdev_err(bond->dev, "option %s: invalid value (%llu)\n",
					   opt->name, val->value);
			}
		}
		minval = bond_opt_get_flags(opt, BOND_VALFLAG_MIN);
		maxval = bond_opt_get_flags(opt, BOND_VALFLAG_MAX);
		if (!maxval)
			break;
		netdev_err(bond->dev, "option %s: allowed values %llu - %llu\n",
			   opt->name, minval ? minval->value : 0, maxval->value);
		break;
	case -EACCES:
		bond_opt_dep_print(bond, opt);
		break;
	case -ENOTEMPTY:
		netdev_err(bond->dev, "option %s: unable to set because the bond device has slaves\n",
			   opt->name);
		break;
	case -EBUSY:
		netdev_err(bond->dev, "option %s: unable to set because the bond device is up\n",
			   opt->name);
		break;
	case -ENODEV:
		if (val && val->string) {
			p = strchr(val->string, '\n');
			if (p)
				*p = '\0';
			netdev_err(bond->dev, "option %s: interface %s does not exist!\n",
				   opt->name, val->string);
		}
		break;
	default:
		break;
	}
}

/**
 * __bond_opt_set - set a bonding option
 * @bond: target bond device
 * @option: option to set
 * @val: value to set it to
 *
 * This function is used to change the bond's option value, it can be
 * used for both enabling/changing an option and for disabling it. RTNL lock
 * must be obtained before calling this function.
 */
int __bond_opt_set(struct bonding *bond,
		   unsigned int option, struct bond_opt_value *val)
{
	const struct bond_opt_value *retval = NULL;
	const struct bond_option *opt;
	int ret = -ENOENT;

	ASSERT_RTNL();

	opt = bond_opt_get(option);
	if (WARN_ON(!val) || WARN_ON(!opt))
		goto out;
	ret = bond_opt_check_deps(bond, opt);
	if (ret)
		goto out;
	retval = bond_opt_parse(opt, val);
	if (!retval) {
		ret = -EINVAL;
		goto out;
	}
	ret = opt->set(bond, retval);
out:
	if (ret)
		bond_opt_error_interpret(bond, opt, ret, val);

	return ret;
}
/**
 * __bond_opt_set_notify - set a bonding option
 * @bond: target bond device
 * @option: option to set
 * @val: value to set it to
 *
 * This function is used to change the bond's option value and trigger
 * a notification to user sapce. It can be used for both enabling/changing
 * an option and for disabling it. RTNL lock must be obtained before calling
 * this function.
 */
int __bond_opt_set_notify(struct bonding *bond,
			  unsigned int option, struct bond_opt_value *val)
{
	int ret;

	ASSERT_RTNL();

	ret = __bond_opt_set(bond, option, val);

	if (!ret && (bond->dev->reg_state == NETREG_REGISTERED))
		call_netdevice_notifiers(NETDEV_CHANGEINFODATA, bond->dev);

	return ret;
}

/**
 * bond_opt_tryset_rtnl - try to acquire rtnl and call __bond_opt_set
 * @bond: target bond device
 * @option: option to set
 * @buf: value to set it to
 *
 * This function tries to acquire RTNL without blocking and if successful
 * calls __bond_opt_set. It is mainly used for sysfs option manipulation.
 */
int bond_opt_tryset_rtnl(struct bonding *bond, unsigned int option, char *buf)
{
	struct bond_opt_value optval;
	int ret;

	if (!rtnl_trylock())
		return restart_syscall();
	bond_opt_initstr(&optval, buf);
	ret = __bond_opt_set_notify(bond, option, &optval);
	rtnl_unlock();

	return ret;
}

/**
 * bond_opt_get - get a pointer to an option
 * @option: option for which to return a pointer
 *
 * This function checks if option is valid and if so returns a pointer
 * to its entry in the bond_opts[] option array.
 */
const struct bond_option *bond_opt_get(unsigned int option)
{
	if (!BOND_OPT_VALID(option))
		return NULL;

	return &bond_opts[option];
}

static bool bond_set_xfrm_features(struct bonding *bond)
{
	if (!IS_ENABLED(CONFIG_XFRM_OFFLOAD))
		return false;

	if (BOND_MODE(bond) == BOND_MODE_ACTIVEBACKUP)
		bond->dev->wanted_features |= BOND_XFRM_FEATURES;
	else
		bond->dev->wanted_features &= ~BOND_XFRM_FEATURES;

	return true;
}

static bool bond_set_tls_features(struct bonding *bond)
{
	if (!IS_ENABLED(CONFIG_TLS_DEVICE))
		return false;

	if (bond_sk_check(bond))
		bond->dev->wanted_features |= BOND_TLS_FEATURES;
	else
		bond->dev->wanted_features &= ~BOND_TLS_FEATURES;

	return true;
}

static int bond_option_mode_set(struct bonding *bond,
				const struct bond_opt_value *newval)
{
	if (!bond_mode_uses_arp(newval->value)) {
		if (bond->params.arp_interval) {
			netdev_dbg(bond->dev, "%s mode is incompatible with arp monitoring, start mii monitoring\n",
				   newval->string);
			/* disable arp monitoring */
			bond->params.arp_interval = 0;
		}

		if (!bond->params.miimon) {
			/* set miimon to default value */
			bond->params.miimon = BOND_DEFAULT_MIIMON;
			netdev_dbg(bond->dev, "Setting MII monitoring interval to %d\n",
				   bond->params.miimon);
		}
	}

	if (newval->value == BOND_MODE_ALB)
		bond->params.tlb_dynamic_lb = 1;

	/* don't cache arp_validate between modes */
	bond->params.arp_validate = BOND_ARP_VALIDATE_NONE;
	bond->params.mode = newval->value;

	if (bond->dev->reg_state == NETREG_REGISTERED) {
		bool update = false;

		update |= bond_set_xfrm_features(bond);
		update |= bond_set_tls_features(bond);

		if (update)
			netdev_update_features(bond->dev);
	}

	return 0;
}

static int bond_option_active_slave_set(struct bonding *bond,
					const struct bond_opt_value *newval)
{
	char ifname[IFNAMSIZ] = { 0, };
	struct net_device *slave_dev;
	int ret = 0;

	sscanf(newval->string, "%15s", ifname); /* IFNAMSIZ */
	if (!strlen(ifname) || newval->string[0] == '\n') {
		slave_dev = NULL;
	} else {
		slave_dev = __dev_get_by_name(dev_net(bond->dev), ifname);
		if (!slave_dev)
			return -ENODEV;
	}

	if (slave_dev) {
		if (!netif_is_bond_slave(slave_dev)) {
			slave_err(bond->dev, slave_dev, "Device is not bonding slave\n");
			return -EINVAL;
		}

		if (bond->dev != netdev_master_upper_dev_get(slave_dev)) {
			slave_err(bond->dev, slave_dev, "Device is not our slave\n");
			return -EINVAL;
		}
	}

	block_netpoll_tx();
	/* check to see if we are clearing active */
	if (!slave_dev) {
		netdev_dbg(bond->dev, "Clearing current active slave\n");
		RCU_INIT_POINTER(bond->curr_active_slave, NULL);
		bond_select_active_slave(bond);
	} else {
		struct slave *old_active = rtnl_dereference(bond->curr_active_slave);
		struct slave *new_active = bond_slave_get_rtnl(slave_dev);

		BUG_ON(!new_active);

		if (new_active == old_active) {
			/* do nothing */
			slave_dbg(bond->dev, new_active->dev, "is already the current active slave\n");
		} else {
			if (old_active && (new_active->link == BOND_LINK_UP) &&
			    bond_slave_is_up(new_active)) {
				slave_dbg(bond->dev, new_active->dev, "Setting as active slave\n");
				bond_change_active_slave(bond, new_active);
			} else {
				slave_err(bond->dev, new_active->dev, "Could not set as active slave; either %s is down or the link is down\n",
					  new_active->dev->name);
				ret = -EINVAL;
			}
		}
	}
	unblock_netpoll_tx();

	return ret;
}

/* There are two tricky bits here.  First, if MII monitoring is activated, then
 * we must disable ARP monitoring.  Second, if the timer isn't running, we must
 * start it.
 */
static int bond_option_miimon_set(struct bonding *bond,
				  const struct bond_opt_value *newval)
{
	netdev_dbg(bond->dev, "Setting MII monitoring interval to %llu\n",
		   newval->value);
	bond->params.miimon = newval->value;
	if (bond->params.updelay)
		netdev_dbg(bond->dev, "Note: Updating updelay (to %d) since it is a multiple of the miimon value\n",
			   bond->params.updelay * bond->params.miimon);
	if (bond->params.downdelay)
		netdev_dbg(bond->dev, "Note: Updating downdelay (to %d) since it is a multiple of the miimon value\n",
			   bond->params.downdelay * bond->params.miimon);
	if (bond->params.peer_notif_delay)
		netdev_dbg(bond->dev, "Note: Updating peer_notif_delay (to %d) since it is a multiple of the miimon value\n",
			   bond->params.peer_notif_delay * bond->params.miimon);
	if (newval->value && bond->params.arp_interval) {
		netdev_dbg(bond->dev, "MII monitoring cannot be used with ARP monitoring - disabling ARP monitoring...\n");
		bond->params.arp_interval = 0;
		if (bond->params.arp_validate)
			bond->params.arp_validate = BOND_ARP_VALIDATE_NONE;
	}
	if (bond->dev->flags & IFF_UP) {
		/* If the interface is up, we may need to fire off
		 * the MII timer. If the interface is down, the
		 * timer will get fired off when the open function
		 * is called.
		 */
		if (!newval->value) {
			cancel_delayed_work_sync(&bond->mii_work);
		} else {
			cancel_delayed_work_sync(&bond->arp_work);
			queue_delayed_work(bond->wq, &bond->mii_work, 0);
		}
	}

	return 0;
}

/* Set up, down and peer notification delays. These must be multiples
 * of the MII monitoring value, and are stored internally as the
 * multiplier. Thus, we must translate to MS for the real world.
 */
static int _bond_option_delay_set(struct bonding *bond,
				  const struct bond_opt_value *newval,
				  const char *name,
				  int *target)
{
	int value = newval->value;

	if (!bond->params.miimon) {
		netdev_err(bond->dev, "Unable to set %s as MII monitoring is disabled\n",
			   name);
		return -EPERM;
	}
	if ((value % bond->params.miimon) != 0) {
		netdev_warn(bond->dev,
			    "%s (%d) is not a multiple of miimon (%d), value rounded to %d ms\n",
			    name,
			    value, bond->params.miimon,
			    (value / bond->params.miimon) *
			    bond->params.miimon);
	}
	*target = value / bond->params.miimon;
	netdev_dbg(bond->dev, "Setting %s to %d\n",
		   name,
		   *target * bond->params.miimon);

	return 0;
}

static int bond_option_updelay_set(struct bonding *bond,
				   const struct bond_opt_value *newval)
{
	return _bond_option_delay_set(bond, newval, "up delay",
				      &bond->params.updelay);
}

static int bond_option_downdelay_set(struct bonding *bond,
				     const struct bond_opt_value *newval)
{
	return _bond_option_delay_set(bond, newval, "down delay",
				      &bond->params.downdelay);
}

static int bond_option_peer_notif_delay_set(struct bonding *bond,
					    const struct bond_opt_value *newval)
{
	int ret = _bond_option_delay_set(bond, newval,
					 "peer notification delay",
					 &bond->params.peer_notif_delay);
	return ret;
}

static int bond_option_use_carrier_set(struct bonding *bond,
				       const struct bond_opt_value *newval)
{
	netdev_dbg(bond->dev, "Setting use_carrier to %llu\n",
		   newval->value);
	bond->params.use_carrier = newval->value;

	return 0;
}

/* There are two tricky bits here.  First, if ARP monitoring is activated, then
 * we must disable MII monitoring.  Second, if the ARP timer isn't running,
 * we must start it.
 */
static int bond_option_arp_interval_set(struct bonding *bond,
					const struct bond_opt_value *newval)
{
	netdev_dbg(bond->dev, "Setting ARP monitoring interval to %llu\n",
		   newval->value);
	bond->params.arp_interval = newval->value;
	if (newval->value) {
		if (bond->params.miimon) {
			netdev_dbg(bond->dev, "ARP monitoring cannot be used with MII monitoring. Disabling MII monitoring\n");
			bond->params.miimon = 0;
		}
		if (!bond->params.arp_targets[0])
			netdev_dbg(bond->dev, "ARP monitoring has been set up, but no ARP targets have been specified\n");
	}
	if (bond->dev->flags & IFF_UP) {
		/* If the interface is up, we may need to fire off
		 * the ARP timer.  If the interface is down, the
		 * timer will get fired off when the open function
		 * is called.
		 */
		if (!newval->value) {
			if (bond->params.arp_validate)
				bond->recv_probe = NULL;
			cancel_delayed_work_sync(&bond->arp_work);
		} else {
			/* arp_validate can be set only in active-backup mode */
			bond->recv_probe = bond_rcv_validate;
			cancel_delayed_work_sync(&bond->mii_work);
			queue_delayed_work(bond->wq, &bond->arp_work, 0);
		}
	}

	return 0;
}

static void _bond_options_arp_ip_target_set(struct bonding *bond, int slot,
					    __be32 target,
					    unsigned long last_rx)
{
	__be32 *targets = bond->params.arp_targets;
	struct list_head *iter;
	struct slave *slave;

	if (slot >= 0 && slot < BOND_MAX_ARP_TARGETS) {
		bond_for_each_slave(bond, slave, iter)
			slave->target_last_arp_rx[slot] = last_rx;
		targets[slot] = target;
	}
}

static int _bond_option_arp_ip_target_add(struct bonding *bond, __be32 target)
{
	__be32 *targets = bond->params.arp_targets;
	int ind;

	if (!bond_is_ip_target_ok(target)) {
		netdev_err(bond->dev, "invalid ARP target %pI4 specified for addition\n",
			   &target);
		return -EINVAL;
	}

	if (bond_get_targets_ip(targets, target) != -1) { /* dup */
		netdev_err(bond->dev, "ARP target %pI4 is already present\n",
			   &target);
		return -EINVAL;
	}

	ind = bond_get_targets_ip(targets, 0); /* first free slot */
	if (ind == -1) {
		netdev_err(bond->dev, "ARP target table is full!\n");
		return -EINVAL;
	}

	netdev_dbg(bond->dev, "Adding ARP target %pI4\n", &target);

	_bond_options_arp_ip_target_set(bond, ind, target, jiffies);

	return 0;
}

static int bond_option_arp_ip_target_add(struct bonding *bond, __be32 target)
{
	return _bond_option_arp_ip_target_add(bond, target);
}

static int bond_option_arp_ip_target_rem(struct bonding *bond, __be32 target)
{
	__be32 *targets = bond->params.arp_targets;
	struct list_head *iter;
	struct slave *slave;
	unsigned long *targets_rx;
	int ind, i;

	if (!bond_is_ip_target_ok(target)) {
		netdev_err(bond->dev, "invalid ARP target %pI4 specified for removal\n",
			   &target);
		return -EINVAL;
	}

	ind = bond_get_targets_ip(targets, target);
	if (ind == -1) {
		netdev_err(bond->dev, "unable to remove nonexistent ARP target %pI4\n",
			   &target);
		return -EINVAL;
	}

	if (ind == 0 && !targets[1] && bond->params.arp_interval)
		netdev_warn(bond->dev, "Removing last arp target with arp_interval on\n");

	netdev_dbg(bond->dev, "Removing ARP target %pI4\n", &target);

	bond_for_each_slave(bond, slave, iter) {
		targets_rx = slave->target_last_arp_rx;
		for (i = ind; (i < BOND_MAX_ARP_TARGETS-1) && targets[i+1]; i++)
			targets_rx[i] = targets_rx[i+1];
		targets_rx[i] = 0;
	}
	for (i = ind; (i < BOND_MAX_ARP_TARGETS-1) && targets[i+1]; i++)
		targets[i] = targets[i+1];
	targets[i] = 0;

	return 0;
}

void bond_option_arp_ip_targets_clear(struct bonding *bond)
{
	int i;

	for (i = 0; i < BOND_MAX_ARP_TARGETS; i++)
		_bond_options_arp_ip_target_set(bond, i, 0, 0);
}

static int bond_option_arp_ip_targets_set(struct bonding *bond,
					  const struct bond_opt_value *newval)
{
	int ret = -EPERM;
	__be32 target;

	if (newval->string) {
		if (!in4_pton(newval->string+1, -1, (u8 *)&target, -1, NULL)) {
			netdev_err(bond->dev, "invalid ARP target %pI4 specified\n",
				   &target);
			return ret;
		}
		if (newval->string[0] == '+')
			ret = bond_option_arp_ip_target_add(bond, target);
		else if (newval->string[0] == '-')
			ret = bond_option_arp_ip_target_rem(bond, target);
		else
			netdev_err(bond->dev, "no command found in arp_ip_targets file - use +<addr> or -<addr>\n");
	} else {
		target = newval->value;
		ret = bond_option_arp_ip_target_add(bond, target);
	}

	return ret;
}

#if IS_ENABLED(CONFIG_IPV6)
static void _bond_options_ns_ip6_target_set(struct bonding *bond, int slot,
					    struct in6_addr *target,
					    unsigned long last_rx)
{
	struct in6_addr *targets = bond->params.ns_targets;
	struct list_head *iter;
	struct slave *slave;

	if (slot >= 0 && slot < BOND_MAX_NS_TARGETS) {
		bond_for_each_slave(bond, slave, iter)
			slave->target_last_arp_rx[slot] = last_rx;
		targets[slot] = *target;
	}
}

void bond_option_ns_ip6_targets_clear(struct bonding *bond)
{
	struct in6_addr addr_any = in6addr_any;
	int i;

	for (i = 0; i < BOND_MAX_NS_TARGETS; i++)
		_bond_options_ns_ip6_target_set(bond, i, &addr_any, 0);
}

static int bond_option_ns_ip6_targets_set(struct bonding *bond,
					  const struct bond_opt_value *newval)
{
	struct in6_addr *target = (struct in6_addr *)newval->extra;
	struct in6_addr *targets = bond->params.ns_targets;
	struct in6_addr addr_any = in6addr_any;
	int index;

	if (!bond_is_ip6_target_ok(target)) {
		netdev_err(bond->dev, "invalid NS target %pI6c specified for addition\n",
			   target);
		return -EINVAL;
	}

	if (bond_get_targets_ip6(targets, target) != -1) { /* dup */
		netdev_err(bond->dev, "NS target %pI6c is already present\n",
			   target);
		return -EINVAL;
	}

	index = bond_get_targets_ip6(targets, &addr_any); /* first free slot */
	if (index == -1) {
		netdev_err(bond->dev, "NS target table is full!\n");
		return -EINVAL;
	}

	netdev_dbg(bond->dev, "Adding NS target %pI6c\n", target);

	_bond_options_ns_ip6_target_set(bond, index, target, jiffies);

	return 0;
}
#else
static int bond_option_ns_ip6_targets_set(struct bonding *bond,
					  const struct bond_opt_value *newval)
{
	return -EPERM;
}
#endif

static int bond_option_arp_validate_set(struct bonding *bond,
					const struct bond_opt_value *newval)
{
	netdev_dbg(bond->dev, "Setting arp_validate to %s (%llu)\n",
		   newval->string, newval->value);
	bond->params.arp_validate = newval->value;

	return 0;
}

static int bond_option_arp_all_targets_set(struct bonding *bond,
					   const struct bond_opt_value *newval)
{
	netdev_dbg(bond->dev, "Setting arp_all_targets to %s (%llu)\n",
		   newval->string, newval->value);
	bond->params.arp_all_targets = newval->value;

	return 0;
}

static int bond_option_missed_max_set(struct bonding *bond,
				      const struct bond_opt_value *newval)
{
	netdev_dbg(bond->dev, "Setting missed max to %s (%llu)\n",
		   newval->string, newval->value);
	bond->params.missed_max = newval->value;

	return 0;
}

static int bond_option_primary_set(struct bonding *bond,
				   const struct bond_opt_value *newval)
{
	char *p, *primary = newval->string;
	struct list_head *iter;
	struct slave *slave;

	block_netpoll_tx();

	p = strchr(primary, '\n');
	if (p)
		*p = '\0';
	/* check to see if we are clearing primary */
	if (!strlen(primary)) {
		netdev_dbg(bond->dev, "Setting primary slave to None\n");
		RCU_INIT_POINTER(bond->primary_slave, NULL);
		memset(bond->params.primary, 0, sizeof(bond->params.primary));
		bond_select_active_slave(bond);
		goto out;
	}

	bond_for_each_slave(bond, slave, iter) {
		if (strncmp(slave->dev->name, primary, IFNAMSIZ) == 0) {
			slave_dbg(bond->dev, slave->dev, "Setting as primary slave\n");
			rcu_assign_pointer(bond->primary_slave, slave);
			strcpy(bond->params.primary, slave->dev->name);
			bond->force_primary = true;
			bond_select_active_slave(bond);
			goto out;
		}
	}

	if (rtnl_dereference(bond->primary_slave)) {
		netdev_dbg(bond->dev, "Setting primary slave to None\n");
		RCU_INIT_POINTER(bond->primary_slave, NULL);
		bond_select_active_slave(bond);
	}
	strscpy_pad(bond->params.primary, primary, IFNAMSIZ);

	netdev_dbg(bond->dev, "Recording %s as primary, but it has not been enslaved yet\n",
		   primary);

out:
	unblock_netpoll_tx();

	return 0;
}

static int bond_option_primary_reselect_set(struct bonding *bond,
					    const struct bond_opt_value *newval)
{
	netdev_dbg(bond->dev, "Setting primary_reselect to %s (%llu)\n",
		   newval->string, newval->value);
	bond->params.primary_reselect = newval->value;

	block_netpoll_tx();
	bond_select_active_slave(bond);
	unblock_netpoll_tx();

	return 0;
}

static int bond_option_fail_over_mac_set(struct bonding *bond,
					 const struct bond_opt_value *newval)
{
	netdev_dbg(bond->dev, "Setting fail_over_mac to %s (%llu)\n",
		   newval->string, newval->value);
	bond->params.fail_over_mac = newval->value;

	return 0;
}

static int bond_option_xmit_hash_policy_set(struct bonding *bond,
					    const struct bond_opt_value *newval)
{
	netdev_dbg(bond->dev, "Setting xmit hash policy to %s (%llu)\n",
		   newval->string, newval->value);
	bond->params.xmit_policy = newval->value;

	if (bond->dev->reg_state == NETREG_REGISTERED)
		if (bond_set_tls_features(bond))
			netdev_update_features(bond->dev);

	return 0;
}

static int bond_option_resend_igmp_set(struct bonding *bond,
				       const struct bond_opt_value *newval)
{
	netdev_dbg(bond->dev, "Setting resend_igmp to %llu\n",
		   newval->value);
	bond->params.resend_igmp = newval->value;

	return 0;
}

static int bond_option_num_peer_notif_set(struct bonding *bond,
				   const struct bond_opt_value *newval)
{
	bond->params.num_peer_notif = newval->value;

	return 0;
}

static int bond_option_all_slaves_active_set(struct bonding *bond,
					     const struct bond_opt_value *newval)
{
	struct list_head *iter;
	struct slave *slave;

	if (newval->value == bond->params.all_slaves_active)
		return 0;
	bond->params.all_slaves_active = newval->value;
	bond_for_each_slave(bond, slave, iter) {
		if (!bond_is_active_slave(slave)) {
			if (newval->value)
				slave->inactive = 0;
			else
				slave->inactive = 1;
		}
	}

	return 0;
}

static int bond_option_min_links_set(struct bonding *bond,
				     const struct bond_opt_value *newval)
{
	netdev_dbg(bond->dev, "Setting min links value to %llu\n",
		   newval->value);
	bond->params.min_links = newval->value;
	bond_set_carrier(bond);

	return 0;
}

static int bond_option_lp_interval_set(struct bonding *bond,
				       const struct bond_opt_value *newval)
{
	bond->params.lp_interval = newval->value;

	return 0;
}

static int bond_option_pps_set(struct bonding *bond,
			       const struct bond_opt_value *newval)
{
	netdev_dbg(bond->dev, "Setting packets per slave to %llu\n",
		   newval->value);
	bond->params.packets_per_slave = newval->value;
	if (newval->value > 0) {
		bond->params.reciprocal_packets_per_slave =
			reciprocal_value(newval->value);
	} else {
		/* reciprocal_packets_per_slave is unused if
		 * packets_per_slave is 0 or 1, just initialize it
		 */
		bond->params.reciprocal_packets_per_slave =
			(struct reciprocal_value) { 0 };
	}

	return 0;
}

static int bond_option_lacp_active_set(struct bonding *bond,
				       const struct bond_opt_value *newval)
{
	netdev_dbg(bond->dev, "Setting LACP active to %s (%llu)\n",
		   newval->string, newval->value);
	bond->params.lacp_active = newval->value;

	return 0;
}

static int bond_option_lacp_rate_set(struct bonding *bond,
				     const struct bond_opt_value *newval)
{
	netdev_dbg(bond->dev, "Setting LACP rate to %s (%llu)\n",
		   newval->string, newval->value);
	bond->params.lacp_fast = newval->value;
	bond_3ad_update_lacp_rate(bond);

	return 0;
}

static int bond_option_ad_select_set(struct bonding *bond,
				     const struct bond_opt_value *newval)
{
	netdev_dbg(bond->dev, "Setting ad_select to %s (%llu)\n",
		   newval->string, newval->value);
	bond->params.ad_select = newval->value;

	return 0;
}

static int bond_option_queue_id_set(struct bonding *bond,
				    const struct bond_opt_value *newval)
{
	struct slave *slave, *update_slave;
	struct net_device *sdev;
	struct list_head *iter;
	char *delim;
	int ret = 0;
	u16 qid;

	/* delim will point to queue id if successful */
	delim = strchr(newval->string, ':');
	if (!delim)
		goto err_no_cmd;

	/* Terminate string that points to device name and bump it
	 * up one, so we can read the queue id there.
	 */
	*delim = '\0';
	if (sscanf(++delim, "%hd\n", &qid) != 1)
		goto err_no_cmd;

	/* Check buffer length, valid ifname and queue id */
	if (!dev_valid_name(newval->string) ||
	    qid > bond->dev->real_num_tx_queues)
		goto err_no_cmd;

	/* Get the pointer to that interface if it exists */
	sdev = __dev_get_by_name(dev_net(bond->dev), newval->string);
	if (!sdev)
		goto err_no_cmd;

	/* Search for thes slave and check for duplicate qids */
	update_slave = NULL;
	bond_for_each_slave(bond, slave, iter) {
		if (sdev == slave->dev)
			/* We don't need to check the matching
			 * slave for dups, since we're overwriting it
			 */
			update_slave = slave;
		else if (qid && qid == slave->queue_id) {
			goto err_no_cmd;
		}
	}

	if (!update_slave)
		goto err_no_cmd;

	/* Actually set the qids for the slave */
	update_slave->queue_id = qid;

out:
	return ret;

err_no_cmd:
	netdev_dbg(bond->dev, "invalid input for queue_id set\n");
	ret = -EPERM;
	goto out;

}

static int bond_option_slaves_set(struct bonding *bond,
				  const struct bond_opt_value *newval)
{
	char command[IFNAMSIZ + 1] = { 0, };
	struct net_device *dev;
	char *ifname;
	int ret;

	sscanf(newval->string, "%16s", command); /* IFNAMSIZ*/
	ifname = command + 1;
	if ((strlen(command) <= 1) ||
	    (command[0] != '+' && command[0] != '-') ||
	    !dev_valid_name(ifname))
		goto err_no_cmd;

	dev = __dev_get_by_name(dev_net(bond->dev), ifname);
	if (!dev) {
		netdev_dbg(bond->dev, "interface %s does not exist!\n",
			   ifname);
		ret = -ENODEV;
		goto out;
	}

	switch (command[0]) {
	case '+':
		slave_dbg(bond->dev, dev, "Enslaving interface\n");
		ret = bond_enslave(bond->dev, dev, NULL);
		break;

	case '-':
		slave_dbg(bond->dev, dev, "Releasing interface\n");
		ret = bond_release(bond->dev, dev);
		break;

	default:
		/* should not run here. */
		goto err_no_cmd;
	}

out:
	return ret;

err_no_cmd:
	netdev_err(bond->dev, "no command found in slaves file - use +ifname or -ifname\n");
	ret = -EPERM;
	goto out;
}

static int bond_option_tlb_dynamic_lb_set(struct bonding *bond,
					  const struct bond_opt_value *newval)
{
	netdev_dbg(bond->dev, "Setting dynamic-lb to %s (%llu)\n",
		   newval->string, newval->value);
	bond->params.tlb_dynamic_lb = newval->value;

	return 0;
}

static int bond_option_ad_actor_sys_prio_set(struct bonding *bond,
					     const struct bond_opt_value *newval)
{
	netdev_dbg(bond->dev, "Setting ad_actor_sys_prio to %llu\n",
		   newval->value);

	bond->params.ad_actor_sys_prio = newval->value;
	bond_3ad_update_ad_actor_settings(bond);

	return 0;
}

static int bond_option_ad_actor_system_set(struct bonding *bond,
					   const struct bond_opt_value *newval)
{
	u8 macaddr[ETH_ALEN];
	u8 *mac;

	if (newval->string) {
		if (!mac_pton(newval->string, macaddr))
			goto err;
		mac = macaddr;
	} else {
		mac = (u8 *)&newval->value;
	}

	if (is_multicast_ether_addr(mac))
		goto err;

	netdev_dbg(bond->dev, "Setting ad_actor_system to %pM\n", mac);
	ether_addr_copy(bond->params.ad_actor_system, mac);
	bond_3ad_update_ad_actor_settings(bond);

	return 0;

err:
	netdev_err(bond->dev, "Invalid ad_actor_system MAC address.\n");
	return -EINVAL;
}

static int bond_option_ad_user_port_key_set(struct bonding *bond,
					    const struct bond_opt_value *newval)
{
	netdev_dbg(bond->dev, "Setting ad_user_port_key to %llu\n",
		   newval->value);

	bond->params.ad_user_port_key = newval->value;
	return 0;
}
