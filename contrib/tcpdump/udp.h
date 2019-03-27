/*
 * Copyright (c) 1982, 1986, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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
 *	@(#)udp.h	8.1 (Berkeley) 6/10/93
 */

/*
 * Udp protocol header.
 * Per RFC 768, September, 1981.
 */
struct udphdr {
	uint16_t	uh_sport;		/* source port */
	uint16_t	uh_dport;		/* destination port */
	uint16_t	uh_ulen;		/* udp length */
	uint16_t	uh_sum;			/* udp checksum */
};

#ifndef NAMESERVER_PORT
#define NAMESERVER_PORT			53
#endif
#ifndef TACACS_DB_PORT
#define TACACS_DB_PORT			65	/*XXX*/
#endif
#ifndef ORACLE_SQLNET_PORT
#define ORACLE_SQLNET_PORT		66	/*XXX*/
#endif
#ifndef BOOTPS_PORT
#define BOOTPS_PORT			67	/* RFC951 */
#endif
#ifndef BOOTPC_PORT
#define BOOTPC_PORT			68	/* RFC951 */
#endif
#ifndef TFTP_PORT
#define TFTP_PORT			69	/*XXX*/
#endif
#ifndef KERBEROS_PORT
#define KERBEROS_PORT			88	/*XXX*/
#endif
#ifndef SUNRPC_PORT
#define SUNRPC_PORT			111	/*XXX*/
#endif
#ifndef NTP_PORT
#define NTP_PORT			123	/*XXX*/
#endif
#ifndef NETBIOS_NS_PORT
#define NETBIOS_NS_PORT			137	/* RFC 1001, RFC 1002 */
#endif
#ifndef NETBIOS_DGRAM_PORT
#define NETBIOS_DGRAM_PORT		138	/* RFC 1001, RFC 1002 */
#endif
#ifndef NETBIOS_SSN_PORT
#define NETBIOS_SSN_PORT		139	/* RFC 1001, RFC 1002 */
#endif
#ifndef SNMP_PORT
#define SNMP_PORT			161	/*XXX*/
#endif
#ifndef SNMPTRAP_PORT
#define SNMPTRAP_PORT			162	/*XXX*/
#endif
#ifndef BGP_PORT
#define BGP_PORT			179	/*XXX*/
#endif
#ifndef APPLETALK_RTMP_PORT
#define APPLETALK_RTMP_PORT		201	/*XXX*/
#endif
#ifndef APPLETALK_NB_PORT
#define APPLETALK_NB_PORT		202	/*XXX*/
#endif
#ifndef APPLETALK_ECHO
#define APPLETALK_ECHO			204	/*XXX*/
#endif
#ifndef APPLETALK_ZONE_INFO_PORT
#define APPLETALK_ZONE_INFO_PORT	206	/*XXX*/
#endif
#ifndef LDAP_PORT
#define LDAP_PORT			389	/*XXX*/
#endif
#ifndef HTTPS_PORT
#define HTTPS_PORT			443	/*XXX*/
#endif
#ifndef MICROSOFT_DS_PORT
#define MICROSOFT_DS_PORT		445	/*XXX*/
#endif
#ifndef KERBEROS5_PASSWD_PORT
#define KERBEROS5_PASSWD_PORT		464	/* PER IANA */
#endif
#ifndef CISCO_AUTORP_PORT
#define CISCO_AUTORP_PORT		496	/*XXX*/
#endif
#ifndef ISAKMP_PORT
#define ISAKMP_PORT			500	/*XXX*/
#endif
#ifndef SYSLOG_PORT
#define SYSLOG_PORT			514	/* rfc3164 */
#endif
#ifndef RIP_PORT
#define RIP_PORT			520	/*XXX*/
#endif
#ifndef RIPNG_PORT
#define RIPNG_PORT			521	/* RFC 2080 */
#endif
#ifndef TIMED_PORT
#define TIMED_PORT			525	/*XXX*/
#endif
#ifndef KERBEROS_LOGIN_PORT
#define KERBEROS_LOGIN_PORT		543	/*XXX*/
#endif
#ifndef KERBEROS_SHELL_PORT
#define KERBEROS_SHELL_PORT		544	/*XXX*/
#endif
#ifndef DHCP6_SERV_PORT
#define DHCP6_SERV_PORT			546	/*XXX*/
#endif
#ifndef DHCP6_CLI_PORT
#define DHCP6_CLI_PORT			547	/*XXX*/
#endif
#ifndef LDAPS_PORT
#define LDAPS_PORT			636	/*XXX - LDAP over TLS/SSL */
#endif
#ifndef LDP_PORT
#define LDP_PORT			646
#endif
#ifndef DHCP_FAILOVER_PORT
#define DHCP_FAILOVER_PORT		647	/*XXX*/
#endif
#ifndef AQDV_PORT
#define AODV_PORT			654	/*XXX*/
#endif
#ifndef OLSR_PORT
#define OLSR_PORT			698	/* rfc3626 */
#endif
#ifndef LMP_PORT
#define LMP_PORT			701	/* rfc4204 */
#endif
#ifndef CISCO_TDP_PORT
#define CISCO_TDP_PORT			711	/*XXX*/
#endif
#ifndef KERBEROS_ADM_PORT
#define KERBEROS_ADM_PORT		749	/*XXX - Kerberos v5 */
#endif
#ifndef KERBEROS_SEC_PORT
#define KERBEROS_SEC_PORT		750	/*XXX - Kerberos v4 */
#endif
#ifndef RSYNC_PORT
#define RSYNC_PORT			873	/*XXX*/
#endif
#ifndef LWRES_PORT
#define LWRES_PORT			921	/*XXX*/
#endif
#ifndef OPENSSL_PORT
#define OPENSSL_PORT			1194	/*XXX*/
#endif
#ifndef LOTUS_NOTES_PORT
#define LOTUS_NOTES_PORT		1352	/*XXX*/
#endif
#ifndef MS_SQL_SERVER_PORT
#define MS_SQL_SERVER_PORT		1433	/*XXX*/
#endif
#ifndef MS_SQL_SERVER_MONITOR
#define MS_SQL_SERVER_MONITOR		1434	/*XXX*/
#endif
#ifndef INGRESLOCK_PORT
#define INGRESLOCK_PORT			1524	/*XXX*/
#endif
#ifndef VQP_PORT
#define VQP_PORT			1589	/*XXX*/
#endif
#ifndef RADIUS_PORT
#define RADIUS_PORT			1645	/*XXX*/
#endif
#ifndef RADIUS_ACCOUNTING_PORT
#define RADIUS_ACCOUNTING_PORT		1646
#endif
#ifndef RADIUS_CISCO_COA_PORT
#define RADIUS_CISCO_COA_PORT		1700
#endif
#ifndef L2TP_PORT
#define L2TP_PORT			1701	/*XXX*/
#endif
#ifndef RADIUS_NEW_PORT
#define RADIUS_NEW_PORT			1812	/*XXX*/
#endif
#ifndef RADIUS_NEW_ACCOUNTING_PORT
#define RADIUS_NEW_ACCOUNTING_PORT	1813
#endif
#ifndef HSRP_PORT
#define HSRP_PORT			1985	/*XXX*/
#endif
#ifndef NFS_DAEMON_PORT
#define NFS_DAEMON_PORT			2049	/*XXX*/
#endif
#ifndef ZEPHYR_SRV_PORT
#define ZEPHYR_SRV_PORT			2103	/*XXX*/
#endif
#ifndef ZEPHYR_CLI_PORT
#define ZEPHYR_CLT_PORT			2104	/*XXX*/
#endif
#ifndef MYSQL_PORT
#define MYSQL_PORT			3306	/*XXX*/
#endif
#ifndef MS_RDP_PORT
#define MS_RDP_PORT			3389	/*XXX*/
#endif
#ifndef VAT_PORT
#define VAT_PORT			3456	/*XXX*/
#endif
#ifndef MPLS_LSP_PING_PORT
#define MPLS_LSP_PING_PORT		3503	/* draft-ietf-mpls-lsp-ping-02.txt */
#endif
#ifndef SUBVERSION_PORT
#define SUBVERSION_PORT			3690	/*XXX*/
#endif
#ifndef BFD_CONTROL_PORT
#define BFD_CONTROL_PORT		3784	/* RFC 5881 */
#endif
#ifndef BFD_ECHO_PORT
#define BFD_ECHO_PORT			3785	/* RFC 5881 */
#endif
#ifndef RADIUS_COA_PORT
#define RADIUS_COA_PORT			3799	/* RFC 5176 */
#endif
#ifndef NFS_LOCK_DAEMON_PORT
#define NFS_LOCK_DAEMON_PORT		4045	/*XXX*/
#endif
#ifndef LISP_CONTROL_PORT
#define LISP_CONTROL_PORT		4342	/* RFC 6830 */
#endif
#ifndef ISAKMP_PORT_NATT
#define ISAKMP_PORT_NATT		4500	/* rfc3948 */
#endif
#ifndef WB_PORT
#define WB_PORT				4567
#endif
#ifndef VXLAN_PORT
#define VXLAN_PORT			4789	/* RFC 7348 */
#endif
#ifndef VXLAN_GPE_PORT
#define VXLAN_GPE_PORT			4790	/* draft-ietf-nvo3-vxlan-gpe-01 */
#endif
#ifndef SIP_DS_PORT
#define SIP_DS_PORT			5059	/*XXX*/
#endif
#ifndef SIP_PORT
#define SIP_PORT			5060
#endif
#ifndef MULTICASTDNS_PORT
#define MULTICASTDNS_PORT		5353	/* RFC 6762 */
#endif
#ifndef AHCP_PORT
#define AHCP_PORT			5359	/* draft-chroboczek-ahcp-00 */
#endif
#ifndef GENEVE_PORT
#define GENEVE_PORT			6081	/* draft-gross-geneve-02 */
#endif
#ifndef SFLOW_PORT
#define SFLOW_PORT			6343	/* http://www.sflow.org/developers/specifications.php */
#endif
#ifndef BABEL_PORT
#define BABEL_PORT			6696	/* RFC 6126 errata */
#endif
#ifndef BABEL_PORT_OLD
#define BABEL_PORT_OLD			6697	/* RFC 6126 */
#endif
#ifndef RX_PORT_LOW
#define RX_PORT_LOW			7000	/*XXX*/
#endif
#ifndef RX_PORT_HIGH
#define RX_PORT_HIGH			7009	/*XXX*/
#endif
#ifndef ISAKMP_PORT_USER1
#define ISAKMP_PORT_USER1		7500	/*XXX - nonstandard*/
#endif
#ifndef HNCP_PORT
#define HNCP_PORT			8231	/* RFC 7788 */
#endif
#ifndef OTV_PORT
#define OTV_PORT			8472	/* draft-hasmit-otv-04 */
#endif
#ifndef ISAKMP_PORT_USER2
#define ISAKMP_PORT_USER2		8500	/*XXX - nonstandard*/
#endif
#ifndef LWAPP_DATA_PORT
#define LWAPP_DATA_PORT			12222	/* RFC 5412 */
#endif
#ifndef LWAPP_CONTROL_PORT
#define LWAPP_CONTROL_PORT		12223	/* RFC 5412 */
#endif
