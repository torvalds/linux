#! /bin/sh
# collate and present sysfs information about AoE storage

set -e
format="%8s\t%8s\t%8s\n"
me=`basename $0`
sysd=${sysfs_dir:-/sys}

# printf "$format" device mac netif state

# Suse 9.1 Pro doesn't put /sys in /etc/mtab
#test -z "`mount | grep sysfs`" && {
test ! -d "$sysd/block" && {
	echo "$me Error: sysfs is not mounted" 1>&2
	exit 1
}

for d in `ls -d $sysd/block/etherd* 2>/dev/null | grep -v p` end; do
	# maybe ls comes up empty, so we use "end"
	test $d = end && continue

	dev=`echo "$d" | sed 's/.*!//'`
	printf "$format" \
		"$dev" \
		"`cat \"$d/netif\"`" \
		"`cat \"$d/state\"`"
done | sort
