#!/bin/sh

. /etc/rc.subr
. /etc/network.subr

load_rc_config netif

#
# Customized per-interface setup, e.g. hyperv_vfup.hn1
#
# NOTE-CUSTOMIZE:
# Comment this out, if this script is used as template
# for the customized per-interface setup.
#
if [ -f /usr/libexec/hyperv/hyperv_vfup.$1 ]
then
	/usr/libexec/hyperv/hyperv_vfup.$1
	exit $?
fi

# NOTE-CUSTOMIZE:
#hn=${0##*.}
hn=$1
hn_unit=`echo $hn | sed 's/[^0-9]*//g'`

vf=`sysctl -n dev.hn.$hn_unit.vf`
if [ ! $vf ]
then
	# Race happened; VF was removed, before we ran.
	echo "$hn: VF was detached"
	exit 0
fi

#
# Create laggX for hnX.
# Add VF and hnX to laggX.
#

lagg=lagg$hn_unit

ifconfig $lagg > /dev/null 2>&1
if [ $? -ne 0 ]
then
	#
	# No laggX, create it now.
	#
	ifconfig $lagg create > /dev/null 2>&1
	if [ $? -ne 0 ]
	then
		echo "$lagg creation failed"
		exit 1
	fi

	#
	# Configure laggX (failover), add hnX and VF to it.
	#
	ifconfig $lagg laggproto failover laggport $hn laggport $vf
	ifconfig $lagg inet6 no_dad

	#
	# Stop dhclient on hnX, if any.
	#
	pidfile=/var/run/dhclient.$hn.pid
	if [ -f $pidfile ]
	then
		kill -TERM `cat $pidfile`
	fi

	#
	# Remove all configured IPv4 addresses on hnX, e.g.
	# configured by dhclient.  laggX will take over the
	# network operations.
	#
	while true
	do
		ifconfig $hn -alias > /dev/null 2>&1
		if [ $? -ne 0 ]
		then
			break
		fi
	done

	# TODO: Remove IPv6 addresses on hnX

	#
	# Use hnX's configuration for laggX
	#
	# NOTE-CUSTOMIZE:
	# If this script is used as template for the customized
	# per-interface setup, replace this with whatever you
	# want to do with the laggX.
	#
	if dhcpif $hn;
	then
		ifconfig $lagg up
		if syncdhcpif $hn;
		then
			dhclient $lagg
		else
			dhclient -b $lagg
		fi
	else
		ifconfig_args=`ifconfig_getargs $hn`
		if [ -n "$ifconfig_args" ]
		then
			ifconfig $lagg $ifconfig_args
		fi
	fi
else
	#
	# laggX exists.  Check whether VF was there or not.
	# If VF was not added to laggX, add it now.
	#
	ifconfig $lagg | grep "laggport: $vf" > /dev/null 2>&1
	if [ $? -ne 0 ]
	then
		ifconfig $lagg laggport $vf
	fi
fi
