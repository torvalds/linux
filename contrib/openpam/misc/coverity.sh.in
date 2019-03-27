#!/bin/sh

usage() {
	echo "usage: ${0##*/} [-jN]" >&2
	exit 1
}

while getopts "j:" opt ; do
	case $opt in
	j)
		j="-j$OPTARG"
		;;
	*)
		usage
		;;
	esac
done

if which -s cov01 ; then
	cov01="$(which cov01)"
fi
covint="cov-int"
covlog="${covint}/build-log.txt"
srcdir="@abs_top_srcdir@"
cd "${srcdir}" || exit 1
rm -rf "${covint}"
gmake clean || exit 1
"${cov01:-:}" -q -u
"${cov01:-:}" -q -0
cov-build --dir "${covint}" gmake "$@"
"${cov01:-:}" -q -o
gmake clean
if tail -1 "${covlog}" | grep -q "completed successfully" ; then
	tar caf "@PACKAGE@-@PACKAGE_VERSION@-cov-int.txz" "${covint}"
else
	tail "${covlog}"
fi
