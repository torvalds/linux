#!/bin/sh
# update-anchor.sh, update a trust anchor.
# Copyright 2008, W.C.A. Wijngaards
# This file is BSD licensed, see doc/LICENSE.

# which validating lookup to use.
ubhost=unbound-host

usage ( )
{
	echo "usage: update-anchor [-r hs] [-b] <zone name> <trust anchor file>"
	echo "    performs an update of trust anchor file"
	echo "    the trust anchor file is overwritten with the latest keys"
	echo "    the trust anchor file should contain only keys for one zone"
	echo "    -b causes keyfile to be made in bind format."
	echo "       without -b the file is made in unbound format."
	echo "    "
	echo "alternate:"
	echo "    update-anchor [-r hints] [-b] -d directory"
	echo "    update all <zone>.anchor files in the directory."
	echo "    "
	echo "    name the files br.anchor se.anchor ..., and include them in"
	echo "    the validating resolver config file."
	echo "    put keys for the root in a file with the name root.anchor."
	echo ""
	echo "-r root.hints	use different root hints. Strict option order."
	echo ""
	echo "Exit code 0 means anchors updated, 1 no changes, others are errors."
	exit 2
}

if test $# -eq 0; then
	usage
fi
bindformat="no"
filearg='-f'
roothints=""
if test X"$1" = "X-r"; then
	shift
	roothints="$1"
	shift
fi
if test X"$1" = "X-b"; then
	shift
	bindformat="yes"
	filearg='-F'
fi
if test $# -ne 2; then
	echo "arguments wrong."
	usage
fi

do_update ( ) {
	# arguments: <zonename> <keyfile>
	zonename="$1"
	keyfile="$2"
	tmpfile="/tmp/update-anchor.$$"
	tmp2=$tmpfile.2
	tmp3=$tmpfile.3
	rh=""
	if test -n "$roothints"; then
		echo "server: root-hints: '$roothints'" > $tmp3
		rh="-C $tmp3"
	fi
	$ubhost -v $rh $filearg "$keyfile" -t DNSKEY "$zonename" >$tmpfile
	if test $? -ne 0; then
		rm -f $tmpfile
		echo "Error: Could not update zone $zonename anchor file $keyfile"
		echo "Cause: $ubhost lookup failed" 
		echo "    (Is the domain decommissioned? Is connectivity lost?)"
		return 2
	fi

	# has the lookup been DNSSEC validated?
	if grep '(secure)$' $tmpfile >/dev/null 2>&1; then
		:
	else
		rm -f $tmpfile
		echo "Error: Could not update zone $zonename anchor file $keyfile"
		echo "Cause: result of lookup was not secure" 
		echo "    (keys too far out of date? domain changed ownership? need root hints?)"
		return 3
	fi

	if test $bindformat = "yes"; then
		# are there any KSK keys on board?
		echo 'trusted-keys {' > "$tmp2"
		if grep ' has DNSKEY record 257' $tmpfile >/dev/null 2>&1; then
			# store KSK keys in anchor file
			grep '(secure)$' $tmpfile | \
			grep ' has DNSKEY record 257' | \
			sed -e 's/ (secure)$/";/' | \
			sed -e 's/ has DNSKEY record \([0-9]*\) \([0-9]*\) \([0-9]*\) /. \1 \2 \3 "/' | \
			sed -e 's/^\.\././' | sort >> "$tmp2"
		else
			# store all keys in the anchor file
			grep '(secure)$' $tmpfile | \
			sed -e 's/ (secure)$/";/' | \
			sed -e 's/ has DNSKEY record \([0-9]*\) \([0-9]*\) \([0-9]*\) /. \1 \2 \3 "/' | \
			sed -e 's/^\.\././' | sort >> "$tmp2"
		fi
		echo '};' >> "$tmp2"
	else #not bindformat
		# are there any KSK keys on board?
		if grep ' has DNSKEY record 257' $tmpfile >/dev/null 2>&1; then
			# store KSK keys in anchor file
			grep '(secure)$' $tmpfile | \
			grep ' has DNSKEY record 257' | \
			sed -e 's/ (secure)$//' | \
			sed -e 's/ has DNSKEY record /. IN DNSKEY /' | \
			sed -e 's/^\.\././' | sort > "$tmp2"
		else
			# store all keys in the anchor file
			grep '(secure)$' $tmpfile | \
			sed -e 's/ (secure)$//' | \
			sed -e 's/ has DNSKEY record /. IN DNSKEY /' | \
			sed -e 's/^\.\././' | sort > "$tmp2"
		fi
	fi # endif-bindformat

	# copy over if changed
	diff $tmp2 $keyfile >/dev/null 2>&1
	if test $? -eq 1; then   # 0 means no change, 2 means trouble.
		cat $tmp2 > $keyfile
		no_updated=0
		echo "$zonename key file $keyfile updated."
	else
		echo "$zonename key file $keyfile unchanged."
	fi

	rm -f $tmpfile $tmp2 $tmp3
}

no_updated=1
if test X"$1" = "X-d"; then
	tdir="$2"
	echo "start updating in $2"
	for x in $tdir/*.anchor; do
		if test `basename "$x"` = "root.anchor"; then
			zname="."
		else
			zname=`basename "$x" .anchor`
		fi
		do_update "$zname" "$x"
	done
	echo "done updating in $2"
else
	# regular invocation
	if test X"$1" = "X."; then
		zname="$1"
	else
		# strip trailing dot from zone name
		zname="`echo $1 | sed -e 's/\.$//'`"
	fi
	kfile="$2"
	do_update $zname $kfile
fi
exit $no_updated
