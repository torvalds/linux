#!/bin/sh
#
# fixciphers - remove unsupported ciphers from man pages.
# Usage: fixpaths /path/to/sed cipher1 [cipher2] <infile >outfile 
#
# Author: Darren Tucker (dtucker at zip com.au).  Placed in the public domain.

die() {
	echo $*
	exit -1
}

SED=$1
shift

for c in $*; do
	subs="$subs -e /.Dq.$c.*$/d"
	subs="$subs -e s/$c,//g"
done

# now remove any entirely empty lines
subs="$subs -e /^$/d"

${SED} $subs

exit 0
