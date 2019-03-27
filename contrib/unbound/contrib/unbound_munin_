#!/bin/sh
#
# plugin for munin to monitor usage of unbound servers.
# To install copy this to /usr/local/share/munin/plugins/unbound_munin_
# and use munin-node-configure (--suggest, --shell).
#
# (C) 2008 W.C.A. Wijngaards.  BSD Licensed.
#
# To install; enable statistics and unbound-control in unbound.conf
#	server:		extended-statistics: yes
#			statistics-cumulative: no
#			statistics-interval: 0
#	remote-control:	control-enable: yes
# Run the command unbound-control-setup to generate the key files.
#
# Environment variables for this script
#	statefile	- where to put temporary statefile.
#	unbound_conf	- where the unbound.conf file is located.
#	unbound_control	- where to find unbound-control executable.
#	spoof_warn	- what level to warn about spoofing
#	spoof_crit	- what level to crit about spoofing
#
# You can set them in your munin/plugin-conf.d/plugins.conf file
# with:
# [unbound*]
# user root
# env.statefile /usr/local/var/munin/plugin-state/unbound-state
# env.unbound_conf /usr/local/etc/unbound/unbound.conf
# env.unbound_control /usr/local/sbin/unbound-control
# env.spoof_warn 1000
# env.spoof_crit 100000
#
# This plugin can create different graphs depending on what name
# you link it as (with ln -s) into the plugins directory
# You can link it multiple times.
# If you are only a casual user, the _hits and _by_type are most interesting,
# possibly followed by _by_rcode.
#
#	unbound_munin_hits	- base volume, cache hits, unwanted traffic
#	unbound_munin_queue	- to monitor the internal requestlist
#	unbound_munin_memory	- memory usage
#	unbound_munin_by_type	- incoming queries by type
#	unbound_munin_by_class	- incoming queries by class
#	unbound_munin_by_opcode	- incoming queries by opcode
#	unbound_munin_by_rcode	- answers by rcode, validation status
#	unbound_munin_by_flags	- incoming queries by flags
#	unbound_munin_histogram	- histogram of query resolving times
#
# Magic markers - optional - used by installation scripts and
# munin-config:  (originally contrib family but munin-node-configure ignores it)
#
#%# family=auto
#%# capabilities=autoconf suggest

# POD documentation
: <<=cut
=head1 NAME

unbound_munin_ - Munin plugin to monitor the Unbound DNS resolver.

=head1 APPLICABLE SYSTEMS

System with unbound daemon.

=head1 CONFIGURATION

  [unbound*]
  user root
  env.statefile /usr/local/var/munin/plugin-state/unbound-state
  env.unbound_conf /usr/local/etc/unbound/unbound.conf
  env.unbound_control /usr/local/sbin/unbound-control
  env.spoof_warn 1000
  env.spoof_crit 100000

Use the .env settings to override the defaults.

=head1 USAGE

Can be used to present different graphs. Use ln -s for that name in
the plugins directory to enable the graph.
unbound_munin_hits	- base volume, cache hits, unwanted traffic
unbound_munin_queue	- to monitor the internal requestlist
unbound_munin_memory	- memory usage
unbound_munin_by_type	- incoming queries by type
unbound_munin_by_class	- incoming queries by class
unbound_munin_by_opcode	- incoming queries by opcode
unbound_munin_by_rcode	- answers by rcode, validation status
unbound_munin_by_flags	- incoming queries by flags
unbound_munin_histogram - histogram of query resolving times

=head1 AUTHOR

Copyright 2008 W.C.A. Wijngaards

=head1 LICENSE

BSD

=cut

state=${statefile:-/usr/local/var/munin/plugin-state/unbound-state}
conf=${unbound_conf:-/usr/local/etc/unbound/unbound.conf}
ctrl=${unbound_control:-/usr/local/sbin/unbound-control}
warn=${spoof_warn:-1000}
crit=${spoof_crit:-100000}
lock=$state.lock

# number of seconds between polling attempts.
# makes the statefile hang around for at least this many seconds,
# so that multiple links of this script can share the results.
lee=55

# to keep things within 19 characters
ABBREV="-e s/total/t/ -e s/thread/t/ -e s/num/n/ -e s/query/q/ -e s/answer/a/ -e s/unwanted/u/ -e s/requestlist/ql/ -e s/type/t/ -e s/class/c/ -e s/opcode/o/ -e s/rcode/r/ -e s/edns/e/ -e s/mem/m/ -e s/cache/c/ -e s/mod/m/"

# get value from $1 into return variable $value
get_value ( ) {
	value="`grep '^'$1'=' $state | sed -e 's/^.*=//'`"
	if test "$value"x = ""x; then
		value="0"
	fi
}

# download the state from the unbound server.
get_state ( ) {
	# obtain lock for fetching the state
	# because there is a race condition in fetching and writing to file

	# see if the lock is stale, if so, take it 
	if test -f $lock ; then
		pid="`cat $lock 2>&1`"
		kill -0 "$pid" >/dev/null 2>&1
		if test $? -ne 0 -a "$pid" != $$ ; then
			echo $$ >$lock
		fi
	fi

	i=0
	while test ! -f $lock || test "`cat $lock 2>&1`" != $$; do
		while test -f $lock; do
			# wait
			i=`expr $i + 1`
			if test $i -gt 1000; then
				sleep 1;
			fi
			if test $i -gt 1500; then
				echo "error locking $lock" "=" `cat $lock`
				rm -f $lock
				exit 1
			fi
		done
		# try to get it
		if echo $$ >$lock ; then : ; else break; fi
	done
	# do not refetch if the file exists and only LEE seconds old
	if test -f $state; then
		now=`date +%s`
		get_value "time.now"
		value="`echo $value | sed -e 's/\..*$//'`"
		if test $now -lt `expr $value + $lee`; then
			rm -f $lock
			return
		fi
	fi
	$ctrl -c $conf stats > $state
	if test $? -ne 0; then
		echo "error retrieving data from unbound server"
		rm -f $lock
		exit 1
	fi
	rm -f $lock
}

if test "$1" = "autoconf" ; then
	if test ! -f $conf; then
		echo no "($conf does not exist)"
		exit 1
	fi
	if test ! -d `dirname $state`; then
		echo no "(`dirname $state` directory does not exist)"
		exit 1
	fi
	echo yes
	exit 0
fi

if test "$1" = "suggest" ; then
	echo "hits"
	echo "queue"
	echo "memory"
	echo "by_type"
	echo "by_class"
	echo "by_opcode"
	echo "by_rcode"
	echo "by_flags"
	echo "histogram"
	exit 0
fi

# determine my type, by name
id=`echo $0 | sed -e 's/^.*unbound_munin_//'`
if test "$id"x = ""x; then
	# some default to keep people sane.
	id="hits"
fi

# if $1 exists in statefile, config is echoed with label $2
exist_config ( ) {
	mn=`echo $1 | sed $ABBREV | tr . _`
	if grep '^'$1'=' $state >/dev/null 2>&1; then
		echo "$mn.label $2"
		echo "$mn.min 0"
		echo "$mn.type ABSOLUTE"
	fi
}

# print label and min 0 for a name $1 in unbound format
p_config ( ) {
	mn=`echo $1 | sed $ABBREV | tr . _`
	echo $mn.label "$2"
	echo $mn.min 0
	echo $mn.type $3
}

if test "$1" = "config" ; then
	if test ! -f $state; then
		get_state
	fi
	case $id in
	hits)
		echo "graph_title Unbound DNS traffic and cache hits"
		echo "graph_args --base 1000 -l 0"
		echo "graph_vlabel queries / \${graph_period}"
		echo "graph_scale no"
		echo "graph_category DNS"
		for x in `grep "^thread[0-9][0-9]*\.num\.queries=" $state |
			sed -e 's/=.*//'`; do
			exist_config $x "queries handled by `basename $x .num.queries`"
		done
		p_config "total.num.queries" "total queries from clients" "ABSOLUTE"
		p_config "total.num.cachehits" "cache hits" "ABSOLUTE"
		p_config "total.num.prefetch" "cache prefetch" "ABSOLUTE"
		p_config "num.query.tcp" "TCP queries" "ABSOLUTE"
		p_config "num.query.tcpout" "TCP out queries" "ABSOLUTE"
		p_config "num.query.ipv6" "IPv6 queries" "ABSOLUTE"
		p_config "unwanted.queries" "queries that failed acl" "ABSOLUTE"
		p_config "unwanted.replies" "unwanted or unsolicited replies" "ABSOLUTE"
		echo "u_replies.warning $warn"
		echo "u_replies.critical $crit"
		echo "graph_info DNS queries to the recursive resolver. The unwanted replies could be innocent duplicate packets, late replies, or spoof threats."
		;;
	queue)
		echo "graph_title Unbound requestlist size"
		echo "graph_args --base 1000 -l 0"
		echo "graph_vlabel number of queries"
		echo "graph_scale no"
		echo "graph_category DNS"
		p_config "total.requestlist.avg" "Average size of queue on insert" "GAUGE"
		p_config "total.requestlist.max" "Max size of queue (in 5 min)" "GAUGE"
		p_config "total.requestlist.overwritten" "Number of queries replaced by new ones" "GAUGE"
		p_config "total.requestlist.exceeded" "Number of queries dropped due to lack of space" "GAUGE"
		echo "graph_info The queries that did not hit the cache and need recursion service take up space in the requestlist. If there are too many queries, first queries get overwritten, and at last resort dropped."
		;;
	memory)
		echo "graph_title Unbound memory usage"
		echo "graph_args --base 1024 -l 0"
		echo "graph_vlabel memory used in bytes"
		echo "graph_category DNS"
		p_config "mem.cache.rrset" "RRset cache memory" "GAUGE"
		p_config "mem.cache.message" "Message cache memory" "GAUGE"
		p_config "mem.mod.iterator" "Iterator module memory" "GAUGE"
		p_config "mem.mod.validator" "Validator module and key cache memory" "GAUGE"
		p_config "msg.cache.count" "msg cache count" "GAUGE"
		p_config "rrset.cache.count" "rrset cache count" "GAUGE"
		p_config "infra.cache.count" "infra cache count" "GAUGE"
		p_config "key.cache.count" "key cache count" "GAUGE"
		echo "graph_info The memory used by unbound."
		;;
	by_type)
		echo "graph_title Unbound DNS queries by type"
		echo "graph_args --base 1000 -l 0"
		echo "graph_vlabel queries / \${graph_period}"
		echo "graph_scale no"
		echo "graph_category DNS"
		for x in `grep "^num.query.type" $state`; do
			nm=`echo $x | sed -e 's/=.*$//'`
			tp=`echo $nm | sed -e s/num.query.type.//`
			p_config "$nm" "$tp" "ABSOLUTE"
		done
		echo "graph_info queries by DNS RR type queried for"
		;;
	by_class)
		echo "graph_title Unbound DNS queries by class"
		echo "graph_args --base 1000 -l 0"
		echo "graph_vlabel queries / \${graph_period}"
		echo "graph_scale no"
		echo "graph_category DNS"
		for x in `grep "^num.query.class" $state`; do
			nm=`echo $x | sed -e 's/=.*$//'`
			tp=`echo $nm | sed -e s/num.query.class.//`
			p_config "$nm" "$tp" "ABSOLUTE"
		done
		echo "graph_info queries by DNS RR class queried for."
		;;
	by_opcode)
		echo "graph_title Unbound DNS queries by opcode"
		echo "graph_args --base 1000 -l 0"
		echo "graph_vlabel queries / \${graph_period}"
		echo "graph_scale no"
		echo "graph_category DNS"
		for x in `grep "^num.query.opcode" $state`; do
			nm=`echo $x | sed -e 's/=.*$//'`
			tp=`echo $nm | sed -e s/num.query.opcode.//`
			p_config "$nm" "$tp" "ABSOLUTE"
		done
		echo "graph_info queries by opcode in the query packet."
		;;
	by_rcode)
		echo "graph_title Unbound DNS answers by return code"
		echo "graph_args --base 1000 -l 0"
		echo "graph_vlabel answer packets / \${graph_period}"
		echo "graph_scale no"
		echo "graph_category DNS"
		for x in `grep "^num.answer.rcode" $state`; do
			nm=`echo $x | sed -e 's/=.*$//'`
			tp=`echo $nm | sed -e s/num.answer.rcode.//`
			p_config "$nm" "$tp" "ABSOLUTE"
		done
		p_config "num.answer.secure" "answer secure" "ABSOLUTE"
		p_config "num.answer.bogus" "answer bogus" "ABSOLUTE"
		p_config "num.rrset.bogus" "num rrsets marked bogus" "ABSOLUTE"
		echo "graph_info answers sorted by return value. rrsets bogus is the number of rrsets marked bogus per \${graph_period} by the validator"
		;;
	by_flags)
		echo "graph_title Unbound DNS incoming queries by flags"
		echo "graph_args --base 1000 -l 0"
		echo "graph_vlabel queries / \${graph_period}"
		echo "graph_scale no"
		echo "graph_category DNS"
		p_config "num.query.flags.QR" "QR (query reply) flag" "ABSOLUTE"
		p_config "num.query.flags.AA" "AA (auth answer) flag" "ABSOLUTE"
		p_config "num.query.flags.TC" "TC (truncated) flag" "ABSOLUTE"
		p_config "num.query.flags.RD" "RD (recursion desired) flag" "ABSOLUTE"
		p_config "num.query.flags.RA" "RA (rec avail) flag" "ABSOLUTE"
		p_config "num.query.flags.Z" "Z (zero) flag" "ABSOLUTE"
		p_config "num.query.flags.AD" "AD (auth data) flag" "ABSOLUTE"
		p_config "num.query.flags.CD" "CD (check disabled) flag" "ABSOLUTE"
		p_config "num.query.edns.present" "EDNS OPT present" "ABSOLUTE"
		p_config "num.query.edns.DO" "DO (DNSSEC OK) flag" "ABSOLUTE"
		echo "graph_info This graphs plots the flags inside incoming queries. For example, if QR, AA, TC, RA, Z flags are set, the query can be rejected. RD, AD, CD and DO are legitimately set by some software."
		;;
	histogram)
		echo "graph_title Unbound DNS histogram of reply time"
		echo "graph_args --base 1000 -l 0"
		echo "graph_vlabel queries / \${graph_period}"
		echo "graph_scale no"
		echo "graph_category DNS"
		echo hcache.label "cache hits"
		echo hcache.min 0
		echo hcache.type ABSOLUTE
		echo hcache.draw AREA
		echo hcache.colour 999999
		echo h64ms.label "0 msec - 66 msec"
		echo h64ms.min 0
		echo h64ms.type ABSOLUTE
		echo h64ms.draw STACK
		echo h64ms.colour 0000FF
		echo h128ms.label "66 msec - 131 msec"
		echo h128ms.min 0
		echo h128ms.type ABSOLUTE
		echo h128ms.colour 1F00DF
		echo h128ms.draw STACK
		echo h256ms.label "131 msec - 262 msec"
		echo h256ms.min 0
		echo h256ms.type ABSOLUTE
		echo h256ms.draw STACK
		echo h256ms.colour 3F00BF
		echo h512ms.label "262 msec - 524 msec"
		echo h512ms.min 0
		echo h512ms.type ABSOLUTE
		echo h512ms.draw STACK
		echo h512ms.colour 5F009F
		echo h1s.label "524 msec - 1 sec"
		echo h1s.min 0
		echo h1s.type ABSOLUTE
		echo h1s.draw STACK
		echo h1s.colour 7F007F
		echo h2s.label "1 sec - 2 sec"
		echo h2s.min 0
		echo h2s.type ABSOLUTE
		echo h2s.draw STACK
		echo h2s.colour 9F005F
		echo h4s.label "2 sec - 4 sec"
		echo h4s.min 0
		echo h4s.type ABSOLUTE
		echo h4s.draw STACK
		echo h4s.colour BF003F
		echo h8s.label "4 sec - 8 sec"
		echo h8s.min 0
		echo h8s.type ABSOLUTE
		echo h8s.draw STACK
		echo h8s.colour DF001F
		echo h16s.label "8 sec - ..."
		echo h16s.min 0
		echo h16s.type ABSOLUTE
		echo h16s.draw STACK
		echo h16s.colour FF0000
		echo "graph_info Histogram of the reply times for queries."
		;;
	esac

	exit 0
fi

# do the stats itself
get_state

# get the time elapsed
get_value "time.elapsed"
if test $value = 0 || test $value = "0.000000"; then
	echo "error: time elapsed 0 or could not retrieve data"
	exit 1
fi
elapsed="$value"

# print value for $1
print_value ( ) {
	mn=`echo $1 | sed $ABBREV | tr . _`
	get_value $1
	echo "$mn.value" $value
}

# print value if line already found in $2
print_value_line ( ) {
	mn=`echo $1 | sed $ABBREV | tr . _`
	value="`echo $2 | sed -e 's/^.*=//'`"
	echo "$mn.value" $value
}


case $id in
hits)
	for x in `grep "^thread[0-9][0-9]*\.num\.queries=" $state |
		sed -e 's/=.*//'` total.num.queries \
		total.num.cachehits total.num.prefetch num.query.tcp \
		num.query.tcpout num.query.ipv6 unwanted.queries \
		unwanted.replies; do
		if grep "^"$x"=" $state >/dev/null 2>&1; then
			print_value $x
		fi
	done
	;;
queue)
	for x in total.requestlist.avg total.requestlist.max \
		total.requestlist.overwritten total.requestlist.exceeded; do
		print_value $x
	done
	;;
memory)
	for x in mem.cache.rrset mem.cache.message mem.mod.iterator \
		mem.mod.validator msg.cache.count rrset.cache.count \
		infra.cache.count key.cache.count; do
		print_value $x
	done
	;;
by_type)
	for x in `grep "^num.query.type" $state`; do
		nm=`echo $x | sed -e 's/=.*$//'`
		print_value_line $nm $x
	done
	;;
by_class)
	for x in `grep "^num.query.class" $state`; do
		nm=`echo $x | sed -e 's/=.*$//'`
		print_value_line $nm $x
	done
	;;
by_opcode)
	for x in `grep "^num.query.opcode" $state`; do
		nm=`echo $x | sed -e 's/=.*$//'`
		print_value_line $nm $x
	done
	;;
by_rcode)
	for x in `grep "^num.answer.rcode" $state`; do
		nm=`echo $x | sed -e 's/=.*$//'`
		print_value_line $nm $x
	done
	print_value "num.answer.secure"
	print_value "num.answer.bogus"
	print_value "num.rrset.bogus"
	;;
by_flags)
	for x in num.query.flags.QR num.query.flags.AA num.query.flags.TC num.query.flags.RD num.query.flags.RA num.query.flags.Z num.query.flags.AD num.query.flags.CD num.query.edns.present num.query.edns.DO; do
		print_value $x
	done
	;;
histogram)
	get_value total.num.cachehits
	echo hcache.value $value
	r=0
	for x in histogram.000000.000000.to.000000.000001 \
		histogram.000000.000001.to.000000.000002 \
		histogram.000000.000002.to.000000.000004 \
		histogram.000000.000004.to.000000.000008 \
		histogram.000000.000008.to.000000.000016 \
		histogram.000000.000016.to.000000.000032 \
		histogram.000000.000032.to.000000.000064 \
		histogram.000000.000064.to.000000.000128 \
		histogram.000000.000128.to.000000.000256 \
		histogram.000000.000256.to.000000.000512 \
		histogram.000000.000512.to.000000.001024 \
		histogram.000000.001024.to.000000.002048 \
		histogram.000000.002048.to.000000.004096 \
		histogram.000000.004096.to.000000.008192 \
		histogram.000000.008192.to.000000.016384 \
		histogram.000000.016384.to.000000.032768 \
		histogram.000000.032768.to.000000.065536; do
		get_value $x
		r=`expr $r + $value`
	done
	echo h64ms.value $r
	get_value histogram.000000.065536.to.000000.131072
	echo h128ms.value $value
	get_value histogram.000000.131072.to.000000.262144
	echo h256ms.value $value
	get_value histogram.000000.262144.to.000000.524288
	echo h512ms.value $value
	get_value histogram.000000.524288.to.000001.000000
	echo h1s.value $value
	get_value histogram.000001.000000.to.000002.000000
	echo h2s.value $value
	get_value histogram.000002.000000.to.000004.000000
	echo h4s.value $value
	get_value histogram.000004.000000.to.000008.000000
	echo h8s.value $value
	r=0
	for x in histogram.000008.000000.to.000016.000000 \
		histogram.000016.000000.to.000032.000000 \
		histogram.000032.000000.to.000064.000000 \
		histogram.000064.000000.to.000128.000000 \
		histogram.000128.000000.to.000256.000000 \
		histogram.000256.000000.to.000512.000000 \
		histogram.000512.000000.to.001024.000000 \
		histogram.001024.000000.to.002048.000000 \
		histogram.002048.000000.to.004096.000000 \
		histogram.004096.000000.to.008192.000000 \
		histogram.008192.000000.to.016384.000000 \
		histogram.016384.000000.to.032768.000000 \
		histogram.032768.000000.to.065536.000000 \
		histogram.065536.000000.to.131072.000000 \
		histogram.131072.000000.to.262144.000000 \
		histogram.262144.000000.to.524288.000000; do
		get_value $x
		r=`expr $r + $value`
	done
	echo h16s.value $r
	;;
esac
