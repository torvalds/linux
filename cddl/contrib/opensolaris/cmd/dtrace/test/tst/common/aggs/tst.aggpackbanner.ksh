#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#

#
# Copyright (c) 2013 Joyent, Inc.  All rights reserved.
#

let width=8

function outputchar
{
	banner $3 | /bin/nawk -v line=$1 -v pos=$2 -v width=$width '{ \
		for (i = 1; i <= length($0); i++) { \
			if (substr($0, i, 1) == " ") \
				continue; \
			printf("\t@letter%d[%d] = lquantize(%d, 0, 40, 1);\n", \
			    line, NR, i + (pos * width));
		} \
	}'
}

function outputstr
{
	let pos=0;
	let line=0

	printf "#pragma D option aggpack\n#pragma D option aggsortkey\n"

	printf "BEGIN\n{\n"
	for c in `echo "$1" | /bin/nawk '{ \
		for (i = 1; i <= length($0); i++) { \
			c = substr($0, i, 1); \
			printf("%s\n", c == " " ? "space" : \
			    c == "\n" ? "newline" : c); \
		} \
	}'`; do
		if [[ "$c" == "space" ]]; then
			let line=line+1
			let pos=0
			continue
		fi

		outputchar $line $pos $c
		let pos=pos+1
	done

	let i=0

	while [[ $i -le $line ]]; do
		printf "\tprinta(@letter%d);\n" $i
		let i=i+1
	done
	printf "\texit(0);\n}\n"
}

dtrace -qs /dev/stdin -x encoding=utf8 <<EOF
`outputstr "why must i do this"`
EOF

dtrace -qs /dev/stdin -x encoding=ascii -x aggzoom <<EOF
`outputstr "i am not well"`
EOF

dtrace -qs /dev/stdin -x encoding=utf8 -x aggzoom <<EOF
`outputstr "send help"`
EOF

