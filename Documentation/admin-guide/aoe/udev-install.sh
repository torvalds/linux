# install the aoe-specific udev rules from udev.txt into 
# the system's udev configuration
# 

me="`basename $0`"

# find udev.conf, often /etc/udev/udev.conf
# (or environment can specify where to find udev.conf)
#
if test -z "$conf"; then
	if test -r /etc/udev/udev.conf; then
		conf=/etc/udev/udev.conf
	else
		conf="`find /etc -type f -name udev.conf 2> /dev/null`"
		if test -z "$conf" || test ! -r "$conf"; then
			echo "$me Error: no udev.conf found" 1>&2
			exit 1
		fi
	fi
fi

# find the directory where udev rules are stored, often
# /etc/udev/rules.d
#
rules_d="`sed -n '/^udev_rules=/{ s!udev_rules=!!; s!\"!!g; p; }' $conf`"
if test -z "$rules_d" ; then
	rules_d=/etc/udev/rules.d
fi
if test ! -d "$rules_d"; then
	echo "$me Error: cannot find udev rules directory" 1>&2
	exit 1
fi
sh -xc "cp `dirname $0`/udev.txt $rules_d/60-aoe.rules"
