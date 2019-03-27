#	$NetBSD: t_ra.sh,v 1.24 2017/01/13 08:11:01 ozaki-r Exp $
#
# Copyright (c) 2015 Internet Initiative Japan Inc.
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

RUMPSRV=unix://r1
RUMPSRV1_2=unix://r12
RUMPCLI=unix://r2
RUMPSRV3=unix://r3
RUMPSRV4=unix://r4
IP6SRV=fc00:1::1
IP6SRV1_2=fc00:1::2
IP6SRV_PREFIX=fc00:1:
IP6CLI=fc00:2::2
IP6SRV3=fc00:3::1
IP6SRV3_PREFIX=fc00:3:
IP6SRV4=fc00:4::1
IP6SRV4_PREFIX=fc00:4:
PIDFILE=./rump.rtadvd.pid
PIDFILE1_2=./rump.rtadvd.pid12
PIDFILE3=./rump.rtadvd.pid3
PIDFILE4=./rump.rtadvd.pid4
CONFIG=./rtadvd.conf
WAITTIME=2
DEBUG=${DEBUG:-true}

init_server()
{

	export RUMP_SERVER=$1
	atf_check -s exit:0 -o match:'0.->.1' rump.sysctl -w net.inet6.ip6.forwarding=1
	export LD_PRELOAD=/usr/lib/librumphijack.so
	atf_check -s exit:0 mkdir -p /rump/var/chroot/rtadvd
	unset LD_PRELOAD
	unset RUMP_SERVER
}

setup_shmif0()
{
	local sock=$1
	local IP6ADDR=$2

	rump_server_add_iface $sock shmif0 bus1

	export RUMP_SERVER=$sock
	atf_check -s exit:0 rump.ifconfig shmif0 inet6 ${IP6ADDR}
	atf_check -s exit:0 rump.ifconfig shmif0 up
	atf_check -s exit:0 rump.ifconfig -w 10

	$DEBUG && rump.ifconfig
}

wait_term()
{
	local PIDFILE=${1}
	shift

	while [ -f ${PIDFILE} ]
	do
		sleep 0.2
	done

	return 0
}

create_rtadvdconfig()
{

	cat << _EOF > ${CONFIG}
shmif0:\
	:mtu#1300:maxinterval#4:mininterval#3:
_EOF
}

start_rtadvd()
{
	local sock=$1
	local pidfile=$2

	export RUMP_SERVER=$sock
	atf_check -s exit:0 rump.rtadvd -c ${CONFIG} -p $pidfile shmif0
	while [ ! -f $pidfile ]; do
		sleep 0.2
	done
	unset RUMP_SERVER
}

check_entries()
{
	local cli=$1
	local srv=$2
	local addr_prefix=$3
	local mac_srv= ll_srv=

	ll_srv=$(get_linklocal_addr $srv shmif0)
	mac_srv=$(get_macaddr $srv shmif0)

	export RUMP_SERVER=$cli
	$DEBUG && dump_entries
	atf_check -s exit:0 -o match:'if=shmif0' rump.ndp -r
	atf_check -s exit:0 -o match:'advertised' rump.ndp -p
	atf_check -s exit:0 -o match:"${ll_srv}%shmif0 \(reachable\)" rump.ndp -p
	atf_check -s exit:0 -o match:'linkmtu=1300' rump.ndp -n -i shmif0
	atf_check -s exit:0 \
	    -o match:"$ll_srv%shmif0 +$mac_srv +shmif0 +(23h59m|1d0h0m)..s S R" \
	    rump.ndp -n -a
	atf_check -s exit:0 -o match:$addr_prefix rump.ndp -n -a
	atf_check -s exit:0 \
	    -o match:"$addr_prefix.+<(TENTATIVE,)?AUTOCONF>" \
	    rump.ifconfig shmif0 inet6
	unset RUMP_SERVER
}

dump_entries()
{

	echo ndp -n -a
	rump.ndp -n -a
	echo ndp -p
	rump.ndp -p
	echo ndp -r
	rump.ndp -r
}

atf_test_case ra_basic cleanup
ra_basic_head()
{

	atf_set "descr" "Tests for basic functions of router advaertisement(RA)"
	atf_set "require.progs" "rump_server rump.rtadvd rump.ndp rump.ifconfig"
}

ra_basic_body()
{

	rump_server_fs_start $RUMPSRV netinet6
	rump_server_start $RUMPCLI netinet6

	setup_shmif0 ${RUMPSRV} ${IP6SRV}
	init_server $RUMPSRV

	setup_shmif0 ${RUMPCLI} ${IP6CLI}
	export RUMP_SERVER=${RUMPCLI}
	$DEBUG && rump.ndp -n -a
	atf_check -s exit:0 -o match:'= 0' rump.sysctl net.inet6.ip6.accept_rtadv
	unset RUMP_SERVER

	create_rtadvdconfig
	start_rtadvd $RUMPSRV $PIDFILE
	sleep $WAITTIME

	export RUMP_SERVER=${RUMPCLI}
	atf_check -s exit:0 -o empty rump.ndp -r
	atf_check -s exit:0 -o not-match:'advertised' rump.ndp -p
	atf_check -s exit:0 -o match:'linkmtu=0' rump.ndp -n -i shmif0
	atf_check -s exit:0 -o not-match:'S R' rump.ndp -n -a
	atf_check -s exit:0 -o not-match:'fc00:1:' rump.ndp -n -a
	atf_check -s exit:0 -o not-match:'fc00:1:' rump.ifconfig shmif0 inet6
	unset RUMP_SERVER

	atf_check -s exit:0 kill -TERM `cat ${PIDFILE}`
	wait_term ${PIDFILE}

	export RUMP_SERVER=${RUMPCLI}
	atf_check -s exit:0 -o match:'0.->.1' rump.sysctl -w net.inet6.ip6.accept_rtadv=1
	unset RUMP_SERVER

	start_rtadvd $RUMPSRV $PIDFILE
	sleep $WAITTIME

	check_entries $RUMPCLI $RUMPSRV $IP6SRV_PREFIX

	atf_check -s exit:0 kill -TERM `cat ${PIDFILE}`
	wait_term ${PIDFILE}

	rump_server_destroy_ifaces
}

ra_basic_cleanup()
{

	if [ -f ${PIDFILE} ]; then
		kill -TERM `cat ${PIDFILE}`
		wait_term ${PIDFILE}
	fi

	$DEBUG && dump
	cleanup
}

atf_test_case ra_flush_prefix_entries cleanup
ra_flush_prefix_entries_head()
{

	atf_set "descr" "Tests for flushing prefixes (ndp -P)"
	atf_set "require.progs" "rump_server rump.rtadvd rump.ndp rump.ifconfig"
}

ra_flush_prefix_entries_body()
{

	rump_server_fs_start $RUMPSRV netinet6
	rump_server_start $RUMPCLI netinet6

	setup_shmif0 ${RUMPSRV} ${IP6SRV}
	setup_shmif0 ${RUMPCLI} ${IP6CLI}

	init_server $RUMPSRV

	create_rtadvdconfig

	export RUMP_SERVER=${RUMPCLI}
	atf_check -s exit:0 -o match:'0.->.1' rump.sysctl -w net.inet6.ip6.accept_rtadv=1
	unset RUMP_SERVER

	start_rtadvd $RUMPSRV $PIDFILE
	sleep $WAITTIME

	check_entries $RUMPCLI $RUMPSRV $IP6SRV_PREFIX

	export RUMP_SERVER=${RUMPCLI}

	# Terminate rtadvd to prevent new RA messages from coming
	# Note that ifconfig down; kill -TERM doesn't work
	kill -KILL `cat ${PIDFILE}`

	# Flush all the entries in the prefix list
	atf_check -s exit:0 rump.ndp -P

	$DEBUG && dump_entries
	atf_check -s exit:0 -o match:'if=shmif0' rump.ndp -r
	atf_check -s exit:0 -o empty rump.ndp -p
	atf_check -s exit:0 -o match:'linkmtu=1300' rump.ndp -n -i shmif0
	atf_check -s exit:0 -o match:'(23h59m|1d0h0m)..s S R' rump.ndp -n -a
	atf_check -s exit:0 -o match:'fc00:1:' rump.ndp -n -a
	atf_check -s exit:0 -o not-match:'fc00:1:' rump.ifconfig shmif0 inet6
	unset RUMP_SERVER

	rump_server_destroy_ifaces
}

ra_flush_prefix_entries_cleanup()
{

	$DEBUG && dump
	cleanup
}

atf_test_case ra_flush_defrouter_entries cleanup
ra_flush_defrouter_entries_head()
{

	atf_set "descr" "Tests for flushing default routers (ndp -R)"
	atf_set "require.progs" "rump_server rump.rtadvd rump.ndp rump.ifconfig"
}

ra_flush_defrouter_entries_body()
{

	rump_server_fs_start $RUMPSRV netinet6
	rump_server_start $RUMPCLI netinet6

	setup_shmif0 ${RUMPSRV} ${IP6SRV}
	setup_shmif0 ${RUMPCLI} ${IP6CLI}

	init_server $RUMPSRV

	create_rtadvdconfig

	export RUMP_SERVER=${RUMPCLI}
	atf_check -s exit:0 -o match:'0.->.1' rump.sysctl -w net.inet6.ip6.accept_rtadv=1
	unset RUMP_SERVER

	start_rtadvd $RUMPSRV $PIDFILE
	sleep $WAITTIME

	check_entries $RUMPCLI $RUMPSRV $IP6SRV_PREFIX

	export RUMP_SERVER=${RUMPCLI}

	# Terminate rtadvd to prevent new RA messages from coming
	# Note that ifconfig down; kill -TERM doesn't work
	kill -KILL `cat ${PIDFILE}`

	# Flush all the entries in the default router list
	atf_check -s exit:0 rump.ndp -R

	$DEBUG && dump_entries
	atf_check -s exit:0 -o empty rump.ndp -r
	atf_check -s exit:0 -o match:'No advertising router' rump.ndp -p
	atf_check -s exit:0 -o match:'linkmtu=1300' rump.ndp -n -i shmif0
	atf_check -s exit:0 -o match:'(23h59m|1d0h0m)..s S R' rump.ndp -n -a
	atf_check -s exit:0 -o match:'fc00:1:' rump.ndp -n -a
	atf_check -s exit:0 -o match:'fc00:1:' rump.ifconfig shmif0 inet6
	unset RUMP_SERVER

	rump_server_destroy_ifaces
}

ra_flush_defrouter_entries_cleanup()
{

	$DEBUG && dump
	cleanup
}

atf_test_case ra_delete_address cleanup
ra_delete_address_head()
{

	atf_set "descr" "Tests for deleting auto-configured address"
	atf_set "require.progs" "rump_server rump.rtadvd rump.ndp rump.ifconfig"
}

ra_delete_address_body()
{

	rump_server_fs_start $RUMPSRV netinet6
	rump_server_start $RUMPCLI netinet6

	setup_shmif0 ${RUMPSRV} ${IP6SRV}
	setup_shmif0 ${RUMPCLI} ${IP6CLI}

	init_server $RUMPSRV

	create_rtadvdconfig

	export RUMP_SERVER=${RUMPCLI}
	atf_check -s exit:0 -o match:'0.->.1' rump.sysctl -w net.inet6.ip6.accept_rtadv=1
	unset RUMP_SERVER

	start_rtadvd $RUMPSRV $PIDFILE
	sleep $WAITTIME

	check_entries $RUMPCLI $RUMPSRV $IP6SRV_PREFIX

	export RUMP_SERVER=${RUMPCLI}
	$DEBUG && rump.ifconfig shmif0
	atf_check -s exit:0 rump.ifconfig shmif0 inet6 \
	    $(rump.ifconfig shmif0 |awk '/AUTOCONF/ {print $2}') delete
	unset RUMP_SERVER

	atf_check -s exit:0 kill -TERM `cat ${PIDFILE}`
	wait_term ${PIDFILE}

	rump_server_destroy_ifaces
}

ra_delete_address_cleanup()
{

	if [ -f ${PIDFILE} ]; then
		kill -TERM `cat ${PIDFILE}`
		wait_term ${PIDFILE}
	fi

	$DEBUG && dump
	cleanup
}

atf_test_case ra_multiple_routers cleanup
ra_multiple_routers_head()
{

	atf_set "descr" "Tests for multiple routers"
	atf_set "require.progs" "rump_server rump.rtadvd rump.ndp rump.ifconfig"
}

ra_multiple_routers_body()
{
	local n=

	rump_server_fs_start $RUMPSRV netinet6
	rump_server_fs_start $RUMPSRV3 netinet6
	rump_server_start $RUMPCLI netinet6

	setup_shmif0 ${RUMPSRV} ${IP6SRV}
	setup_shmif0 ${RUMPSRV3} ${IP6SRV3}
	setup_shmif0 ${RUMPCLI} ${IP6CLI}

	init_server $RUMPSRV
	init_server $RUMPSRV3

	create_rtadvdconfig

	export RUMP_SERVER=${RUMPCLI}
	atf_check -s exit:0 -o match:'0.->.1' rump.sysctl -w net.inet6.ip6.accept_rtadv=1
	unset RUMP_SERVER

	start_rtadvd $RUMPSRV $PIDFILE
	start_rtadvd $RUMPSRV3 $PIDFILE3
	sleep $WAITTIME

	check_entries $RUMPCLI $RUMPSRV $IP6SRV_PREFIX
	check_entries $RUMPCLI $RUMPSRV3 $IP6SRV3_PREFIX

	export RUMP_SERVER=$RUMPCLI
	# Two prefixes are advertised by differnt two routers
	n=$(rump.ndp -p |grep 'advertised by' |wc -l)
	atf_check_equal $n 2
	unset RUMP_SERVER

	atf_check -s exit:0 kill -TERM `cat ${PIDFILE}`
	wait_term ${PIDFILE}
	atf_check -s exit:0 kill -TERM `cat ${PIDFILE3}`
	wait_term ${PIDFILE3}

	rump_server_destroy_ifaces
}

ra_multiple_routers_cleanup()
{

	if [ -f ${PIDFILE} ]; then
		kill -TERM `cat ${PIDFILE}`
		wait_term ${PIDFILE}
	fi
	if [ -f ${PIDFILE3} ]; then
		kill -TERM `cat ${PIDFILE3}`
		wait_term ${PIDFILE3}
	fi

	$DEBUG && dump
	cleanup
}

atf_test_case ra_multiple_routers_single_prefix cleanup
ra_multiple_routers_single_prefix_head()
{

	atf_set "descr" "Tests for multiple routers with a single prefix"
	atf_set "require.progs" "rump_server rump.rtadvd rump.ndp rump.ifconfig"
}

ra_multiple_routers_single_prefix_body()
{
	local n=

	rump_server_fs_start $RUMPSRV netinet6
	rump_server_fs_start $RUMPSRV1_2 netinet6
	rump_server_start $RUMPCLI netinet6

	setup_shmif0 ${RUMPSRV} ${IP6SRV}
	setup_shmif0 ${RUMPSRV1_2} ${IP6SRV1_2}
	setup_shmif0 ${RUMPCLI} ${IP6CLI}

	init_server $RUMPSRV
	init_server $RUMPSRV1_2

	create_rtadvdconfig

	export RUMP_SERVER=${RUMPCLI}
	atf_check -s exit:0 -o match:'0.->.1' rump.sysctl -w net.inet6.ip6.accept_rtadv=1
	unset RUMP_SERVER

	start_rtadvd $RUMPSRV $PIDFILE
	start_rtadvd $RUMPSRV1_2 $PIDFILE1_2
	sleep $WAITTIME

	check_entries $RUMPCLI $RUMPSRV $IP6SRV_PREFIX
	check_entries $RUMPCLI $RUMPSRV1_2 $IP6SRV_PREFIX

	export RUMP_SERVER=$RUMPCLI
	# One prefix is advertised by differnt two routers
	n=$(rump.ndp -p |grep 'advertised by' |wc -l)
	atf_check_equal $n 1
	unset RUMP_SERVER

	atf_check -s exit:0 kill -TERM `cat ${PIDFILE}`
	wait_term ${PIDFILE}
	atf_check -s exit:0 kill -TERM `cat ${PIDFILE1_2}`
	wait_term ${PIDFILE1_2}

	rump_server_destroy_ifaces
}

ra_multiple_routers_single_prefix_cleanup()
{

	if [ -f ${PIDFILE} ]; then
		kill -TERM `cat ${PIDFILE}`
		wait_term ${PIDFILE}
	fi
	if [ -f ${PIDFILE1_2} ]; then
		kill -TERM `cat ${PIDFILE1_2}`
		wait_term ${PIDFILE1_2}
	fi

	$DEBUG && dump
	cleanup
}

atf_test_case ra_multiple_routers_maxifprefixes cleanup
ra_multiple_routers_maxifprefixes_head()
{

	atf_set "descr" "Tests for exceeding the number of maximum prefixes"
	atf_set "require.progs" "rump_server rump.rtadvd rump.ndp rump.ifconfig"
}

ra_multiple_routers_maxifprefixes_body()
{
	local n=

	rump_server_fs_start $RUMPSRV netinet6
	rump_server_fs_start $RUMPSRV3 netinet6
	rump_server_fs_start $RUMPSRV4 netinet6
	rump_server_start $RUMPCLI netinet6

	setup_shmif0 ${RUMPSRV} ${IP6SRV}
	setup_shmif0 ${RUMPSRV3} ${IP6SRV3}
	setup_shmif0 ${RUMPSRV4} ${IP6SRV4}
	setup_shmif0 ${RUMPCLI} ${IP6CLI}

	init_server $RUMPSRV
	init_server $RUMPSRV3
	init_server $RUMPSRV4

	create_rtadvdconfig

	export RUMP_SERVER=${RUMPCLI}
	atf_check -s exit:0 -o match:'0.->.1' \
	    rump.sysctl -w net.inet6.ip6.accept_rtadv=1
	# Limit the maximum number of prefix entries to 2
	atf_check -s exit:0 -o match:'16.->.2' \
	    rump.sysctl -w net.inet6.ip6.maxifprefixes=2
	unset RUMP_SERVER

	start_rtadvd $RUMPSRV $PIDFILE
	start_rtadvd $RUMPSRV3 $PIDFILE3
	sleep $WAITTIME

	check_entries $RUMPCLI $RUMPSRV $IP6SRV_PREFIX
	check_entries $RUMPCLI $RUMPSRV3 $IP6SRV3_PREFIX

	start_rtadvd $RUMPSRV4 $PIDFILE4
	sleep $WAITTIME

	export RUMP_SERVER=${RUMPCLI}
	$DEBUG && dump_entries
	# There should remain two prefixes
	n=$(rump.ndp -p |grep 'advertised by' |wc -l)
	atf_check_equal $n 2
	# TODO check other conditions
	unset RUMP_SERVER

	atf_check -s exit:0 kill -TERM `cat ${PIDFILE}`
	wait_term ${PIDFILE}
	atf_check -s exit:0 kill -TERM `cat ${PIDFILE3}`
	wait_term ${PIDFILE3}
	atf_check -s exit:0 kill -TERM `cat ${PIDFILE4}`
	wait_term ${PIDFILE4}

	rump_server_destroy_ifaces
}

ra_multiple_routers_maxifprefixes_cleanup()
{

	if [ -f ${PIDFILE} ]; then
		kill -TERM `cat ${PIDFILE}`
		wait_term ${PIDFILE}
	fi
	if [ -f ${PIDFILE3} ]; then
		kill -TERM `cat ${PIDFILE3}`
		wait_term ${PIDFILE3}
	fi
	if [ -f ${PIDFILE4} ]; then
		kill -TERM `cat ${PIDFILE4}`
		wait_term ${PIDFILE4}
	fi

	$DEBUG && dump
	cleanup
}

atf_test_case ra_temporary_address cleanup
ra_temporary_address_head()
{

	atf_set "descr" "Tests for IPv6 temporary address"
	atf_set "require.progs" "rump_server rump.rtadvd rump.ndp rump.ifconfig"
}

check_echo_request_pkt()
{
	local pkt="$2 > $3: .+ echo request"

	extract_new_packets $1 > ./out
	$DEBUG && echo $pkt
	$DEBUG && cat ./out
	atf_check -s exit:0 -o match:"$pkt" cat ./out
}

ra_temporary_address_body()
{
	local ip_auto= ip_temp=

	rump_server_fs_start $RUMPSRV netinet6
	rump_server_start $RUMPCLI netinet6

	setup_shmif0 $RUMPSRV $IP6SRV
	init_server $RUMPSRV
	setup_shmif0 $RUMPCLI $IP6CLI

	export RUMP_SERVER=$RUMPCLI
	atf_check -s exit:0 -o match:'0.->.1' \
	    rump.sysctl -w net.inet6.ip6.accept_rtadv=1
	atf_check -s exit:0 -o match:'0.->.1' \
	    rump.sysctl -w net.inet6.ip6.use_tempaddr=1
	unset RUMP_SERVER

	create_rtadvdconfig
	start_rtadvd $RUMPSRV $PIDFILE
	sleep $WAITTIME

	check_entries $RUMPCLI $RUMPSRV $IP6SRV_PREFIX

	export RUMP_SERVER=$RUMPCLI

	# Check temporary address
	atf_check -s exit:0 \
	    -o match:"$IP6SRV_PREFIX.+<(TENTATIVE,)?AUTOCONF,TEMPORARY>" \
	    rump.ifconfig shmif0 inet6

	#
	# Testing net.inet6.ip6.prefer_tempaddr
	#
	atf_check -s exit:0 rump.ifconfig -w 10
	$DEBUG && rump.ifconfig shmif0
	ip_auto=$(rump.ifconfig shmif0 |awk '/<AUTOCONF>/ {sub(/\/[0-9]*/, ""); print $2;}')
	ip_temp=$(rump.ifconfig shmif0 |awk '/<AUTOCONF,TEMPORARY>/ {sub(/\/[0-9]*/, ""); print $2;}')
	$DEBUG && echo $ip_auto $ip_temp

	# Ignore old packets
	extract_new_packets bus1 > /dev/null

	atf_check -s exit:0 -o ignore rump.ping6 -n -X 2 -c 1 $IP6SRV
	# autoconf (non-temporal) address should be used as the source address
	check_echo_request_pkt bus1 $ip_auto $IP6SRV

	# Enable net.inet6.ip6.prefer_tempaddr
	atf_check -s exit:0 -o match:'0.->.1' \
	    rump.sysctl -w net.inet6.ip6.prefer_tempaddr=1

	atf_check -s exit:0 -o ignore rump.ping6 -n -X 2 -c 1 $IP6SRV
	# autoconf, temporal address should be used as the source address
	check_echo_request_pkt bus1 $ip_temp $IP6SRV

	unset RUMP_SERVER

	atf_check -s exit:0 kill -TERM `cat ${PIDFILE}`
	wait_term $PIDFILE

	rump_server_destroy_ifaces
}

ra_temporary_address_cleanup()
{

	if [ -f ${PIDFILE} ]; then
		kill -TERM `cat ${PIDFILE}`
		wait_term ${PIDFILE}
	fi

	$DEBUG && dump
	cleanup
}

atf_init_test_cases()
{

	atf_add_test_case ra_basic
	atf_add_test_case ra_flush_prefix_entries
	atf_add_test_case ra_flush_defrouter_entries
	atf_add_test_case ra_delete_address
	atf_add_test_case ra_multiple_routers
	atf_add_test_case ra_multiple_routers_single_prefix
	atf_add_test_case ra_multiple_routers_maxifprefixes
	atf_add_test_case ra_temporary_address
}
