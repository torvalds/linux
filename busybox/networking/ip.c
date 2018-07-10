/* vi: set sw=4 ts=4: */
/*
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 *
 * Authors: Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 *
 * Changes:
 * Rani Assaf <rani@magic.metawire.com> 980929: resolve addresses
 * Bernhard Reutner-Fischer rewrote to use index_in_substr_array
 */
//config:config IP
//config:	bool "ip (34 kb)"
//config:	default y
//config:	select PLATFORM_LINUX
//config:	help
//config:	The "ip" applet is a TCP/IP interface configuration and routing
//config:	utility.
//config:	Short forms (enabled below) are busybox-specific extensions.
//config:	The standard "ip" utility does not provide them. If you are
//config:	trying to be portable, it's better to use "ip CMD" forms.
//config:
//config:config IPADDR
//config:	bool "ipaddr (14 kb)"
//config:	default y
//config:	select FEATURE_IP_ADDRESS
//config:	select PLATFORM_LINUX
//config:	help
//config:	Short form of "ip addr"
//config:
//config:config IPLINK
//config:	bool "iplink (16 kb)"
//config:	default y
//config:	select FEATURE_IP_LINK
//config:	select PLATFORM_LINUX
//config:	help
//config:	Short form of "ip link"
//config:
//config:config IPROUTE
//config:	bool "iproute (15 kb)"
//config:	default y
//config:	select FEATURE_IP_ROUTE
//config:	select PLATFORM_LINUX
//config:	help
//config:	Short form of "ip route"
//config:
//config:config IPTUNNEL
//config:	bool "iptunnel (9.6 kb)"
//config:	default y
//config:	select FEATURE_IP_TUNNEL
//config:	select PLATFORM_LINUX
//config:	help
//config:	Short form of "ip tunnel"
//config:
//config:config IPRULE
//config:	bool "iprule (10 kb)"
//config:	default y
//config:	select FEATURE_IP_RULE
//config:	select PLATFORM_LINUX
//config:	help
//config:	Short form of "ip rule"
//config:
//config:config IPNEIGH
//config:	bool "ipneigh (8.3 kb)"
//config:	default y
//config:	select FEATURE_IP_NEIGH
//config:	select PLATFORM_LINUX
//config:	help
//config:	Short form of "ip neigh"
//config:
//config:config FEATURE_IP_ADDRESS
//config:	bool "ip address"
//config:	default y
//config:	depends on IP || IPADDR
//config:	help
//config:	Address manipulation support for the "ip" applet.
//config:
//config:config FEATURE_IP_LINK
//config:	bool "ip link"
//config:	default y
//config:	depends on IP || IPLINK
//config:	help
//config:	Configure network devices with "ip".
//config:
//config:config FEATURE_IP_ROUTE
//config:	bool "ip route"
//config:	default y
//config:	depends on IP || IPROUTE
//config:	help
//config:	Add support for routing table management to "ip".
//config:
//config:config FEATURE_IP_ROUTE_DIR
//config:	string "ip route configuration directory"
//config:	default "/etc/iproute2"
//config:	depends on FEATURE_IP_ROUTE
//config:	help
//config:	Location of the "ip" applet routing configuration.
//config:
//config:config FEATURE_IP_TUNNEL
//config:	bool "ip tunnel"
//config:	default y
//config:	depends on IP || IPTUNNEL
//config:	help
//config:	Add support for tunneling commands to "ip".
//config:
//config:config FEATURE_IP_RULE
//config:	bool "ip rule"
//config:	default y
//config:	depends on IP || IPRULE
//config:	help
//config:	Add support for rule commands to "ip".
//config:
//config:config FEATURE_IP_NEIGH
//config:	bool "ip neighbor"
//config:	default y
//config:	depends on IP || IPNEIGH
//config:	help
//config:	Add support for neighbor commands to "ip".
//config:
//config:config FEATURE_IP_RARE_PROTOCOLS
//config:	bool "Support displaying rarely used link types"
//config:	default n
//config:	depends on IP || IPADDR || IPLINK || IPROUTE || IPTUNNEL || IPRULE || IPNEIGH
//config:	help
//config:	If you are not going to use links of type "frad", "econet",
//config:	"bif" etc, you probably don't need to enable this.
//config:	Ethernet, wireless, infrared, ppp/slip, ip tunnelling
//config:	link types are supported without this option selected.

//applet:IF_IP(      APPLET_NOEXEC(ip      , ip      , BB_DIR_SBIN, BB_SUID_DROP, ip      ))
//applet:IF_IPADDR(  APPLET_NOEXEC(ipaddr  , ipaddr  , BB_DIR_SBIN, BB_SUID_DROP, ipaddr  ))
//applet:IF_IPLINK(  APPLET_NOEXEC(iplink  , iplink  , BB_DIR_SBIN, BB_SUID_DROP, iplink  ))
//applet:IF_IPROUTE( APPLET_NOEXEC(iproute , iproute , BB_DIR_SBIN, BB_SUID_DROP, iproute ))
//applet:IF_IPRULE(  APPLET_NOEXEC(iprule  , iprule  , BB_DIR_SBIN, BB_SUID_DROP, iprule  ))
//applet:IF_IPTUNNEL(APPLET_NOEXEC(iptunnel, iptunnel, BB_DIR_SBIN, BB_SUID_DROP, iptunnel))
//applet:IF_IPNEIGH( APPLET_NOEXEC(ipneigh , ipneigh , BB_DIR_SBIN, BB_SUID_DROP, ipneigh ))

//kbuild:lib-$(CONFIG_IP) += ip.o
//kbuild:lib-$(CONFIG_IPADDR) += ip.o
//kbuild:lib-$(CONFIG_IPLINK) += ip.o
//kbuild:lib-$(CONFIG_IPROUTE) += ip.o
//kbuild:lib-$(CONFIG_IPRULE) += ip.o
//kbuild:lib-$(CONFIG_IPTUNNEL) += ip.o
//kbuild:lib-$(CONFIG_IPNEIGH) += ip.o

//--------------123456789.123456789.123456789.123456789.123456789.123456789.123456789.123....79
//usage:#define ipaddr_trivial_usage
//usage:       "add|del IFADDR dev IFACE | show|flush [dev IFACE] [to PREFIX]"
//usage:#define ipaddr_full_usage "\n\n"
//usage:       "ipaddr add|change|replace|delete dev IFACE IFADDR\n"
//usage:       "	IFADDR := PREFIX | ADDR peer PREFIX [broadcast ADDR|+|-]\n"
//usage:       "		[anycast ADDR] [label STRING] [scope SCOPE]\n"
//usage:       "	PREFIX := ADDR[/MASK]\n"
//usage:       "	SCOPE := [host|link|global|NUMBER]\n"
//usage:       "ipaddr show|flush [dev IFACE] [scope SCOPE] [to PREFIX] [label PATTERN]"
//usage:
//--------------123456789.123456789.123456789.123456789.123456789.123456789.123456789.123....79
//usage:#define iplink_trivial_usage
//usage:       /*Usage:iplink*/"set IFACE [up|down] [arp on|off] [multicast on|off]\n"
//usage:       "	[promisc on|off] [mtu NUM] [name NAME] [qlen NUM] [address MAC]\n"
//usage:       "	[master IFACE | nomaster]"
// * short help shows only "set" command, long help continues (with just one "\n")
// * and shows all other commands:
//usage:#define iplink_full_usage "\n"
//usage:       "iplink add [link IFACE] IFACE [address MAC] type TYPE [ARGS]\n"
//usage:       "iplink delete IFACE type TYPE [ARGS]\n"
//usage:       "	TYPE ARGS := vlan VLANARGS | vrf table NUM\n"
//usage:       "	VLANARGS := id VLANID [protocol 802.1q|802.1ad] [reorder_hdr on|off]\n"
//usage:       "		[gvrp on|off] [mvrp on|off] [loose_binding on|off]\n"
//usage:       "iplink show [IFACE]"
//upstream man ip-link:
//=====================
//ip link add [link DEV] [ name ] NAME
//                   [ txqueuelen PACKETS ]
//                   [ address LLADDR ]
//                   [ broadcast LLADDR ]
//                   [ mtu MTU ] [index IDX ]
//                   [ numtxqueues QUEUE_COUNT ]
//                   [ numrxqueues QUEUE_COUNT ]
//                   type TYPE [ ARGS ]
//       ip link delete { DEVICE | dev DEVICE | group DEVGROUP } type TYPE [ ARGS ]
//       ip link set { DEVICE | dev DEVICE | group DEVGROUP } [ { up | down } ]
//                      [ arp { on | off } ]
//                      [ dynamic { on | off } ]
//                      [ multicast { on | off } ]
//                      [ allmulticast { on | off } ]
//                      [ promisc { on | off } ]
//                      [ trailers { on | off } ]
//                      [ txqueuelen PACKETS ]
//                      [ name NEWNAME ]
//                      [ address LLADDR ]
//                      [ broadcast LLADDR ]
//                      [ mtu MTU ]
//                      [ netns { PID | NAME } ]
//                      [ link-netnsid ID ]
//	      [ alias NAME ]
//                      [ vf NUM [ mac LLADDR ]
//		   [ vlan VLANID [ qos VLAN-QOS ] ]
//		   [ rate TXRATE ]
//		   [ spoofchk { on | off} ]
//		   [ query_rss { on | off} ]
//		   [ state { auto | enable | disable} ] ]
//		   [ trust { on | off} ] ]
//	      [ master DEVICE ]
//	      [ nomaster ]
//	      [ addrgenmode { eui64 | none | stable_secret | random } ]
//                      [ protodown { on | off } ]
//       ip link show [ DEVICE | group GROUP ] [up] [master DEV] [type TYPE]
//       ip link help [ TYPE ]
//TYPE := { vlan | veth | vcan | dummy | ifb | macvlan | macvtap |
//          bridge | bond | ipoib | ip6tnl | ipip | sit | vxlan |
//          gre | gretap | ip6gre | ip6gretap | vti | nlmon |
//          bond_slave | ipvlan | geneve | bridge_slave | vrf }
//usage:
//--------------123456789.123456789.123456789.123456789.123456789.123456789.123456789.123....79
//usage:#define iproute_trivial_usage
//usage:       "list|flush|add|del|change|append|replace|test ROUTE"
//usage:#define iproute_full_usage "\n\n"
//usage:       "iproute list|flush SELECTOR\n"
//usage:       "	SELECTOR := [root PREFIX] [match PREFIX] [proto RTPROTO]\n"
//usage:       "	PREFIX := default|ADDR[/MASK]\n"
//usage:       "iproute get ADDR [from ADDR iif IFACE]\n"
//usage:       "	[oif IFACE] [tos TOS]\n"
//usage:       "iproute add|del|change|append|replace|test ROUTE\n"
//usage:       "	ROUTE := NODE_SPEC [INFO_SPEC]\n"
//usage:       "	NODE_SPEC := PREFIX"IF_FEATURE_IP_RULE(" [table TABLE_ID]")" [proto RTPROTO] [scope SCOPE] [metric METRIC]\n"
//usage:       "	INFO_SPEC := NH OPTIONS\n"
//usage:       "	NH := [via [inet|inet6] ADDR] [dev IFACE] [src ADDR] [onlink]\n"
//usage:       "	OPTIONS := [mtu [lock] NUM] [advmss [lock] NUM]"
//upstream man ip-route:
//======================
//ip route { show | flush } SELECTOR
//ip route save SELECTOR
//ip route restore
//ip route get ADDRESS [ from ADDRESS iif STRING  ] [ oif STRING ] [ tos TOS ]
//ip route { add | del | change | append | replace } ROUTE
//SELECTOR := [ root PREFIX ] [ match PREFIX ] [ exact PREFIX ] [ table TABLE_ID ] [ proto RTPROTO ] [ type TYPE ] [ scope SCOPE ]
//ROUTE := NODE_SPEC [ INFO_SPEC ]
//NODE_SPEC := [ TYPE ] PREFIX [ tos TOS ] [ table TABLE_ID ] [ proto RTPROTO ] [ scope SCOPE ] [ metric METRIC ]
//INFO_SPEC := NH OPTIONS FLAGS [ nexthop NH ] ...
//NH := [ encap ENCAP ] [ via [ FAMILY ] ADDRESS ] [ dev STRING ] [ weight NUMBER ] NHFLAGS
// ..............................................................^ I guess [src ADDRESS] should be here
//FAMILY := [ inet | inet6 | ipx | dnet | mpls | bridge | link ]
//OPTIONS := FLAGS [ mtu NUMBER ] [ advmss NUMBER ] [ as [ to ] ADDRESS ] rtt TIME ] [ rttvar TIME ] [ reordering NUMBER ] [ window NUMBER ] [ cwnd NUMBER ] [ ssthresh REALM ] [ realms REALM ]
//        [ rto_min TIME ] [ initcwnd NUMBER ] [ initrwnd NUMBER ] [ features FEATURES ] [ quickack BOOL ] [ congctl NAME ] [ pref PREF ] [ expires TIME ]
//TYPE := [ unicast | local | broadcast | multicast | throw | unreachable | prohibit | blackhole | nat ]
//TABLE_ID := [ local | main | default | all | NUMBER ]
//SCOPE := [ host | link | global | NUMBER ]
//NHFLAGS := [ onlink | pervasive ]
//RTPROTO := [ kernel | boot | static | NUMBER ]
//FEATURES := [ ecn | ]
//PREF := [ low | medium | high ]
//ENCAP := [ MPLS | IP ]
//ENCAP_MPLS := mpls [ LABEL ]
//ENCAP_IP := ip id TUNNEL_ID dst REMOTE_IP [ tos TOS ] [ ttl TTL ]
//usage:
//--------------123456789.123456789.123456789.123456789.123456789.123456789.123456789.123....79
//usage:#define iprule_trivial_usage
//usage:       "[list] | add|del SELECTOR ACTION"
//usage:#define iprule_full_usage "\n\n"
//usage:       "	SELECTOR := [from PREFIX] [to PREFIX] [tos TOS] [fwmark FWMARK]\n"
//usage:       "			[dev IFACE] [pref NUMBER]\n"
//usage:       "	ACTION := [table TABLE_ID] [nat ADDR]\n"
//usage:       "			[prohibit|reject|unreachable]\n"
//usage:       "			[realms [SRCREALM/]DSTREALM]\n"
//usage:       "	TABLE_ID := [local|main|default|NUMBER]"
//usage:
//--------------123456789.123456789.123456789.123456789.123456789.123456789.123456789.123....79
//usage:#define iptunnel_trivial_usage
//usage:       "add|change|del|show [NAME]\n"
//usage:       "	[mode ipip|gre|sit]\n"
//usage:       "	[remote ADDR] [local ADDR] [ttl TTL]"
//usage:#define iptunnel_full_usage "\n\n"
//usage:       "iptunnel add|change|del|show [NAME]\n"
//usage:       "	[mode ipip|gre|sit] [remote ADDR] [local ADDR]\n"
//usage:       "	[[i|o]seq] [[i|o]key KEY] [[i|o]csum]\n"
//usage:       "	[ttl TTL] [tos TOS] [[no]pmtudisc] [dev PHYS_DEV]"
//usage:
//usage:#define ipneigh_trivial_usage
//usage:       "show|flush [to PREFIX] [dev DEV] [nud STATE]"
//usage:#define ipneigh_full_usage ""
//usage:
//usage:#if ENABLE_FEATURE_IP_ADDRESS || ENABLE_FEATURE_IP_ROUTE
//usage:# define IP_BAR_LINK   "|"
//usage:#else
//usage:# define IP_BAR_LINK   ""
//usage:#endif
//usage:#if ENABLE_FEATURE_IP_ADDRESS || ENABLE_FEATURE_IP_ROUTE || ENABLE_FEATURE_IP_LINK
//usage:# define IP_BAR_TUNNEL "|"
//usage:#else
//usage:# define IP_BAR_TUNNEL ""
//usage:#endif
//usage:#if ENABLE_FEATURE_IP_ADDRESS || ENABLE_FEATURE_IP_ROUTE || ENABLE_FEATURE_IP_LINK || ENABLE_FEATURE_IP_TUNNEL
//usage:# define IP_BAR_NEIGH  "|"
//usage:#else
//usage:# define IP_BAR_NEIGH  ""
//usage:#endif
//usage:#if ENABLE_FEATURE_IP_ADDRESS || ENABLE_FEATURE_IP_ROUTE || ENABLE_FEATURE_IP_LINK || ENABLE_FEATURE_IP_TUNNEL || ENABLE_FEATURE_IP_NEIGH
//usage:# define IP_BAR_RULE   "|"
//usage:#else
//usage:# define IP_BAR_RULE   ""
//usage:#endif
//usage:
//usage:#define ip_trivial_usage
//usage:       "[OPTIONS] "
//usage:	IF_FEATURE_IP_ADDRESS("address")
//usage:	IF_FEATURE_IP_ROUTE(  IF_FEATURE_IP_ADDRESS("|")"route")
//usage:	IF_FEATURE_IP_LINK(   IP_BAR_LINK  "link")
//usage:	IF_FEATURE_IP_TUNNEL( IP_BAR_TUNNEL"tunnel")
//usage:	IF_FEATURE_IP_NEIGH(  IP_BAR_NEIGH "neigh")
//usage:	IF_FEATURE_IP_RULE(   IP_BAR_RULE  "rule")
//usage:       " [COMMAND]"
//usage:#define ip_full_usage "\n\n"
//usage:       "OPTIONS := -f[amily] inet|inet6|link | -o[neline]\n"
//usage:       "COMMAND :="
//usage:	IF_FEATURE_IP_ADDRESS("\n"
//usage:	"ip addr "ipaddr_trivial_usage)
//usage:	IF_FEATURE_IP_ROUTE("\n"
//usage:	"ip route "iproute_trivial_usage)
//usage:	IF_FEATURE_IP_LINK("\n"
//usage:	"ip link "iplink_trivial_usage)
//usage:	IF_FEATURE_IP_TUNNEL("\n"
//usage:	"ip tunnel "iptunnel_trivial_usage)
//usage:	IF_FEATURE_IP_NEIGH("\n"
//usage:	"ip neigh "ipneigh_trivial_usage)
//usage:	IF_FEATURE_IP_RULE("\n"
//usage:	"ip rule "iprule_trivial_usage)

#include "libbb.h"

#include "libiproute/utils.h"
#include "libiproute/ip_common.h"

typedef int FAST_FUNC (*ip_func_ptr_t)(char**);

#if ENABLE_IPADDR \
 || ENABLE_IPLINK \
 || ENABLE_IPROUTE \
 || ENABLE_IPRULE \
 || ENABLE_IPTUNNEL \
 || ENABLE_IPNEIGH
static int ip_do(ip_func_ptr_t ip_func, char **argv)
{
	argv = ip_parse_common_args(argv + 1);
	return ip_func(argv);
}
#endif

#if ENABLE_IPADDR
int ipaddr_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int ipaddr_main(int argc UNUSED_PARAM, char **argv)
{
	return ip_do(do_ipaddr, argv);
}
#endif
#if ENABLE_IPLINK
int iplink_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int iplink_main(int argc UNUSED_PARAM, char **argv)
{
	return ip_do(do_iplink, argv);
}
#endif
#if ENABLE_IPROUTE
int iproute_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int iproute_main(int argc UNUSED_PARAM, char **argv)
{
	return ip_do(do_iproute, argv);
}
#endif
#if ENABLE_IPRULE
int iprule_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int iprule_main(int argc UNUSED_PARAM, char **argv)
{
	return ip_do(do_iprule, argv);
}
#endif
#if ENABLE_IPTUNNEL
int iptunnel_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int iptunnel_main(int argc UNUSED_PARAM, char **argv)
{
	return ip_do(do_iptunnel, argv);
}
#endif
#if ENABLE_IPNEIGH
int ipneigh_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int ipneigh_main(int argc UNUSED_PARAM, char **argv)
{
	return ip_do(do_ipneigh, argv);
}
#endif

#if ENABLE_IP
static int FAST_FUNC ip_print_help(char **argv UNUSED_PARAM)
{
	bb_show_usage();
}

int ip_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int ip_main(int argc UNUSED_PARAM, char **argv)
{
	static const char keywords[] ALIGN1 = ""
		IF_FEATURE_IP_ADDRESS("address\0")
		IF_FEATURE_IP_ROUTE("route\0")
		IF_FEATURE_IP_ROUTE("r\0")
		IF_FEATURE_IP_LINK("link\0")
		IF_FEATURE_IP_TUNNEL("tunnel\0")
		IF_FEATURE_IP_TUNNEL("tunl\0")
		IF_FEATURE_IP_RULE("rule\0")
		IF_FEATURE_IP_NEIGH("neigh\0")
		;
	static const ip_func_ptr_t ip_func_ptrs[] = {
		ip_print_help,
		IF_FEATURE_IP_ADDRESS(do_ipaddr,)
		IF_FEATURE_IP_ROUTE(do_iproute,)
		IF_FEATURE_IP_ROUTE(do_iproute,)
		IF_FEATURE_IP_LINK(do_iplink,)
		IF_FEATURE_IP_TUNNEL(do_iptunnel,)
		IF_FEATURE_IP_TUNNEL(do_iptunnel,)
		IF_FEATURE_IP_RULE(do_iprule,)
		IF_FEATURE_IP_NEIGH(do_ipneigh,)
	};
	ip_func_ptr_t ip_func;
	int key;

	argv = ip_parse_common_args(argv + 1);
	if (ARRAY_SIZE(ip_func_ptrs) > 1 && *argv)
		key = index_in_substrings(keywords, *argv++);
	else
		key = -1;
	ip_func = ip_func_ptrs[key + 1];

	return ip_func(argv);
}
#endif
