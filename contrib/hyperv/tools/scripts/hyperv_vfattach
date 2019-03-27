#!/bin/sh

#
# If transparent VF is enabled, don't do anything.
#

sysctl -n hw.hn.vf_transparent > /dev/null 2>&1
if [ $? -ne 0 ]
then
	# Old kernel; no transparent VF.
	vf_transparent=0
else
	vf_transparent=`sysctl -n hw.hn.vf_transparent`
fi

if [ $vf_transparent -ne 0 ]
then
	# Transparent VF; done!
	exit 0
fi

iface=$1
delay=$2

if [ $delay -gt 0 ]
then
	#
	# Delayed VF up.
	#
	sleep $delay
	ifconfig $iface up
	# Done!
	exit $?
fi

#
# Check to see whether $iface is a VF or not.
# If $iface is a VF, bring it up now.
#

# for hyperv_vf_delay
. /etc/rc.conf

sysctl -n hw.hn.vflist > /dev/null 2>&1
if [ $? -ne 0 ]
then
	# Old kernel; nothing could be done properly.
	exit 0
fi
vf_list=`sysctl -n hw.hn.vflist`

for vf in $vf_list
do
	if [ $vf = $iface ]
	then
		#
		# Linger a little bit (at least 2 seconds) mainly to
		# make sure that $iface is fully attached.
		#
		# NOTE:
		# In Azure hyperv_vf_delay should be configured to a
		# large value, e.g. 120 seconds, to avoid racing cloud
		# agent goofs.
		#
		test $hyperv_vf_delay -ge 2 > /dev/null 2>&1
		if [ $? -ne 0 ]
		then
			hyperv_vf_delay=2
		fi
		#
		# NOTE:
		# "(sleep ..; ifconfig .. up) > /dev/null 2>&1 &"
		# does _not_ work.
		#
		daemon -f /usr/libexec/hyperv/hyperv_vfattach \
		    $iface $hyperv_vf_delay
		break
	fi
done
