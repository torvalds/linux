#!/bin/sh

prog=$(basename $0)
usage()
{
	echo "usage: ${prog} symbol" 1>&2
	exit 1
}

if [ $# -ne 1 ]; then
	usage
fi

sed 's/\(["\]\)/\\\1/g' | \
${AWK:-awk} -v sym=$1 '
BEGIN	{ printf "const char " sym "[] = \""; }
	{ printf $0 "\\n"; }
END	{ print "\";"; }'

exit 0
