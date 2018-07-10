#!/bin/sh
#
# This should work with the GNU version of cpio and gzip!
# This should work with the bash or ash shell!
# Requires the programs (cpio, gzip, and the pager more or less).
#
usage() {
	cat <<EOF
Usage: unrpm -l package.rpm            <List contents of rpm package>
       unrpm -x package.rpm /foo/boo   <Extract rpm package to this directory,
                                        put . for current directory>
EOF
	exit
}

rpm=$2

exist() {
	if [ -z "${rpm}" ]; then
		usage
	elif [ ! -s "${rpm}" ]; then
		echo "Can't find ${rpm}!"
		exit 1
	fi
}

if [ -z "$1" ]; then
	usage
elif [ "$1" = "-l" ]; then
	exist
	type more >/dev/null 2>&1 && pager=more
	type less >/dev/null 2>&1 && pager=less
	[ "$pager" = "" ] && echo "No pager found!" && exit
	(
		printf "\nPress enter to scroll, q to Quit!\n\n"
		rpm2cpio "${rpm}" | cpio -tv --quiet
	) | ${pager}
	exit
elif [ "$1" = "-x" ]; then
	exist
	if [ -z "$3" ]; then
		usage
	elif [ ! -d "$3" ]; then
		echo "No such directory $3!"
		exit 1
	fi
	rpm2cpio "${rpm}" | (umask 0 ; cd "$3" ; cpio -idmuv) || exit
	echo
	echo "Extracted ${rpm} to $3!"
	exit
else
	usage
fi
