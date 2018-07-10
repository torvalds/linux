#!/bin/sh
#
# This should work with the GNU version of tar and gzip!
# This should work with the bash or ash shell!
# Requires the programs (ar, tar, gzip, and the pager more or less).
#
usage() {
	cat <<EOF
Usage: undeb -c package.deb            <Print control file info>
       undeb -l package.deb            <List contents of deb package>
       undeb -x package.deb /foo/boo   <Extract deb package to this directory,
                                        put . for current directory>
EOF
	exit
}

deb=$2

exist() {
	if [ -z "${deb}" ]; then
		usage
	elif [ ! -s "${deb}" ]; then
		echo "Can't find ${deb}!"
		exit 1
	fi
}

if [ -z "$1" ]; then
	usage
elif [ "$1" = "-l" ]; then
	exist
	type more >/dev/null 2>&1 && pager=more
	type less >/dev/null 2>&1 && pager=less
	[ -z "${pager}" ] && echo "No pager found!" && exit 1
	(
		ar -p "${deb}" control.tar.gz | tar -xzO *control
		printf "\nPress enter to scroll, q to Quit!\n\n"
		ar -p "${deb}" data.tar.gz | tar -tzv
	) | ${pager}
	exit
elif [ "$1" = "-c" ]; then
	exist
	ar -p "${deb}" control.tar.gz | tar -xzO *control
	exit
elif [ "$1" = "-x" ]; then
	exist
	if [ -z "$3" ]; then
		usage
	elif [ ! -d "$3" ]; then
		echo "No such directory $3!"
		exit 1
	fi
	ar -p "${deb}" data.tar.gz | tar -xzvpf - -C "$3" || exit
	echo
	echo "Extracted ${deb} to $3!"
	exit
else
	usage
fi
