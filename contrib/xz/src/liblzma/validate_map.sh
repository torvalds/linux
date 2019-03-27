#!/bin/sh

###############################################################################
#
# Check liblzma.map for certain types of errors
#
# Author: Lasse Collin
#
# This file has been put into the public domain.
# You can do whatever you want with this file.
#
###############################################################################

LC_ALL=C
export LC_ALL

STATUS=0

cd "$(dirname "$0")"

# Get the list of symbols that aren't defined in liblzma.map.
SYMS=$(sed -n 's/^extern LZMA_API([^)]*) \([a-z0-9_]*\)(.*$/\1;/p' \
		api/lzma/*.h \
	| sort \
	| grep -Fve "$(sed '/[{}:*]/d;/^$/d;s/^	//' liblzma.map)")

# Check that there are no old alpha or beta versions listed.
VER=$(cd ../.. && sh build-aux/version.sh)
NAMES=
case $VER in
	*alpha | *beta)
		NAMES=$(sed -n 's/^.*XZ_\([^ ]*\)\(alpha\|beta\) .*$/\1\2/p' \
			liblzma.map | grep -Fv "$VER")
		;;
esac

# Check for duplicate lines. It can catch missing dependencies.
DUPS=$(sort liblzma.map | sed '/^$/d;/^global:$/d' | uniq -d)

# Print error messages if needed.
if test -n "$SYMS$NAMES$DUPS"; then
	echo
	echo 'validate_map.sh found problems from liblzma.map:'
	echo

	if test -n "$SYMS"; then
		echo 'liblzma.map lacks the following symbols:'
		echo "$SYMS"
		echo
	fi

	if test -n "$NAMES"; then
		echo 'Obsolete alpha or beta version names:'
		echo "$NAMES"
		echo
	fi

	if test -n "$DUPS"; then
		echo 'Duplicate lines:'
		echo "$DUPS"
		echo
	fi

	STATUS=1
fi

# Exit status is 1 if problems were found, 0 otherwise.
exit "$STATUS"
