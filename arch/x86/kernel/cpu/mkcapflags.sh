#!/bin/sh
#
# Generate the x86_cap_flags[] array from include/asm/cpufeature.h
#

IN=$1
OUT=$2

TABS="$(printf '\t\t\t\t\t')"
trap 'rm "$OUT"' EXIT

(
	echo "#ifndef _ASM_X86_CPUFEATURE_H"
	echo "#include <asm/cpufeature.h>"
	echo "#endif"
	echo ""
	echo "const char * const x86_cap_flags[NCAPINTS*32] = {"

	# Iterate through any input lines starting with #define X86_FEATURE_
	sed -n -e 's/\t/ /g' -e 's/^ *# *define *X86_FEATURE_//p' $IN |
	while read i
	do
		# Name is everything up to the first whitespace
		NAME="$(echo "$i" | sed 's/ .*//')"

		# If the /* comment */ starts with a quote string, grab that.
		VALUE="$(echo "$i" | sed -n 's@.*/\* *\("[^"]*"\).*\*/@\1@p')"
		[ -z "$VALUE" ] && VALUE="\"$NAME\""
		[ "$VALUE" == '""' ] && continue

		# Name is uppercase, VALUE is all lowercase
		VALUE="$(echo "$VALUE" | tr A-Z a-z)"

		TABCOUNT=$(( ( 5*8 - 14 - $(echo "$NAME" | wc -c) ) / 8 ))
		printf "\t[%s]%.*s = %s,\n" \
			"X86_FEATURE_$NAME" "$TABCOUNT" "$TABS" "$VALUE"
	done
	echo "};"
) > $OUT

trap - EXIT
