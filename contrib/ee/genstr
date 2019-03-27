#!/bin/sh

set -x

if [ $# -lt 2 ]
then
	echo usage $0 source_file dest_file 
	exit 1
fi

trap 'rm -f /tmp/$$.out; exit 0' 0           # set up traps to clean up
trap 'rm -f /tmp/$$.out; exit 1' 1 2 3 15    # on errors AND normal exit

if [ -f $2 ]
then
	rm $2
fi

cat $1 | grep 'catgetlocal.*\"*\"' | 
	sed -e 's/^.*catgetlocal(//' | 
	sed -e 's/^[ 	]*//'	|
	sed -e 's/, \"/ \"/'	|
	sed -e 's/);//' > /tmp/$$.out

cat > $2 <<EOF
\$ 
\$ 
\$set 1
\$quote "
EOF

sort -n < /tmp/$$.out >> $2
