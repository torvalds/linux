# $NetBSD: t_hostent.sh,v 1.10 2014/01/13 11:08:14 gson Exp $
#
# Copyright (c) 2008 The NetBSD Foundation, Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
# ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
# TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
# PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
# BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.
#

n6="sixthavenue.astron.com"
a6="2620:106:3003:1f00:3e4a:92ff:fef4:e180"
ans6="name=$n6, length=16, addrtype=24, aliases=[] addr_list=[$a6]\n"

n4="sixthavenue.astron.com"
a4="38.117.134.16"
ans4="name=$n4, length=4, addrtype=2, aliases=[] addr_list=[$a4]\n"

l6="localhost"
al6="::1"
loc6="name=$l6, length=16, addrtype=24, aliases=[localhost. localhost.localdomain.] addr_list=[$al6]\n"

l4="localhost"
al4="127.0.0.1"
loc4="name=$l4, length=4, addrtype=2, aliases=[localhost. localhost.localdomain.] addr_list=[$al4]\n"

dir="$(atf_get_srcdir)"
res="-r ${dir}/resolv.conf"

# Hijack DNS traffic using a single rump server instance and a DNS
# server listening on its loopback address.

start_dns_server() {
	export RUMP_SERVER=unix:///tmp/rumpserver
	rump_server -lrumpdev -lrumpnet -lrumpnet_net -lrumpnet_netinet \
	    -lrumpnet_netinet6 -lrumpnet_local $RUMP_SERVER
	HIJACK_DNS="LD_PRELOAD=/usr/lib/librumphijack.so RUMPHIJACK='socket=inet:inet6'"
	eval $HIJACK_DNS ${dir}/h_dns_server $1
}

stop_dns_server() {
	export RUMP_SERVER=unix:///tmp/rumpserver
	kill $(cat dns_server_$1.pid)
	rump.halt
}

atf_test_case gethostbyname4 cleanup
gethostbyname4_head()
{
	atf_set "descr" "Checks gethostbyname2(3) for AF_INET (auto, as determined by nsswitch.conf(5)"
}
gethostbyname4_body()
{
	start_dns_server 4
	atf_check -o inline:"$ans4" -x "$HIJACK_DNS ${dir}/h_hostent ${res} -t auto -4 $n4"
}
gethostbyname4_cleanup()
{
	stop_dns_server 4
}

atf_test_case gethostbyname6 cleanup cleanup
gethostbyname6_head()
{
	atf_set "descr" "Checks gethostbyname2(3) for AF_INET6 (auto, as determined by nsswitch.conf(5)"
}
gethostbyname6_body()
{
	start_dns_server 4
	atf_check -o inline:"$ans6" -x "$HIJACK_DNS ${dir}/h_hostent ${res} -t auto -6 $n6"
}
gethostbyname6_cleanup()
{
	stop_dns_server 4
}

atf_test_case gethostbyaddr4 cleanup
gethostbyaddr4_head()
{
	atf_set "descr" "Checks gethostbyaddr(3) for AF_INET (auto, as determined by nsswitch.conf(5)"
}
gethostbyaddr4_body()
{
	start_dns_server 4
        atf_check -o inline:"$ans4" -x "$HIJACK_DNS ${dir}/h_hostent ${res} -t auto -a $a4"
}
gethostbyaddr4_cleanup()
{
	stop_dns_server 4
}

atf_test_case gethostbyaddr6 cleanup
gethostbyaddr6_head()
{
	atf_set "descr" "Checks gethostbyaddr(3) for AF_INET6 (auto, as determined by nsswitch.conf(5)"
}
gethostbyaddr6_body()
{
	start_dns_server 4
	atf_check -o inline:"$ans6" -x "$HIJACK_DNS ${dir}/h_hostent ${res} -t auto -a $a6"
}
gethostbyaddr6_cleanup()
{
	stop_dns_server 4
}

atf_test_case hostsbynamelookup4
hostsbynamelookup4_head()
{
	atf_set "descr" "Checks /etc/hosts name lookup for AF_INET"
}
hostsbynamelookup4_body()
{
	atf_check -o inline:"$loc4" -x "${dir}/h_hostent -f ${dir}/hosts -t file -4 $l4"
}

atf_test_case hostsbynamelookup6
hostsbynamelookup6_head()
{
	atf_set "descr" "Checks /etc/hosts name lookup for AF_INET6"
}
hostsbynamelookup6_body()
{
	atf_check -o inline:"$loc6" -x "${dir}/h_hostent -f ${dir}/hosts -t file -6 $l6"
}

atf_test_case hostsbyaddrlookup4
hostsbyaddrlookup4_head()
{
	atf_set "descr" "Checks /etc/hosts address lookup for AF_INET"
}
hostsbyaddrlookup4_body()
{
	atf_check -o inline:"$loc4" -x "${dir}/h_hostent -f ${dir}/hosts -t file -4 -a $al4"
}

atf_test_case hostsbyaddrlookup6
hostsbyaddrlookup6_head()
{
	atf_set "descr" "Checks /etc/hosts address lookup for AF_INET6"
}
hostsbyaddrlookup6_body()
{
	atf_check -o inline:"$loc6" -x "${dir}/h_hostent -f ${dir}/hosts -t file -6 -a $al6"
}

atf_test_case dnsbynamelookup4 cleanup
dnsbynamelookup4_head()
{
	atf_set "descr" "Checks DNS name lookup for AF_INET"
}
dnsbynamelookup4_body()
{
	start_dns_server 4
	atf_check -o inline:"$ans4" -x "$HIJACK_DNS ${dir}/h_hostent ${res} -t dns -4 $n4"
}
dnsbynamelookup4_cleanup()
{
	stop_dns_server 4
}

atf_test_case dnsbynamelookup6 cleanup
dnsbynamelookup6_head()
{
	atf_set "descr" "Checks DNS name lookup for AF_INET6"
}
dnsbynamelookup6_body()
{
	start_dns_server 4
	atf_check -o inline:"$ans6" -x "$HIJACK_DNS ${dir}/h_hostent ${res} -t dns -6 $n6"
}
dnsbynamelookup6_cleanup()
{
	stop_dns_server 4
}

atf_test_case dnsbyaddrlookup4 cleanup
dnsbyaddrlookup4_head()
{
	atf_set "descr" "Checks DNS address lookup for AF_INET"
}
dnsbyaddrlookup4_body()
{
	start_dns_server 4
	atf_check -o inline:"$ans4" -x "$HIJACK_DNS ${dir}/h_hostent ${res} -t dns -4 -a $a4"
}
dnsbyaddrlookup4_cleanup()
{
	stop_dns_server 4
}

atf_test_case dnsbyaddrlookup6 cleanup
dnsbyaddrlookup6_head()
{
	atf_set "descr" "Checks dns address lookup for AF_INET6"
}
dnsbyaddrlookup6_body()
{
	start_dns_server 4
	atf_check -o inline:"$ans6" -x "$HIJACK_DNS ${dir}/h_hostent ${res} -t dns -6 -a $a6"
}
dnsbyaddrlookup6_cleanup()
{
	stop_dns_server 4
}

atf_init_test_cases()
{
	atf_add_test_case gethostbyname4
	atf_add_test_case gethostbyname6
	atf_add_test_case gethostbyaddr4
	atf_add_test_case gethostbyaddr6

	atf_add_test_case hostsbynamelookup4
	atf_add_test_case hostsbynamelookup6
	atf_add_test_case hostsbyaddrlookup4
	atf_add_test_case hostsbyaddrlookup6

	atf_add_test_case dnsbynamelookup4
	atf_add_test_case dnsbynamelookup6
	atf_add_test_case dnsbyaddrlookup4
	atf_add_test_case dnsbyaddrlookup6
}
