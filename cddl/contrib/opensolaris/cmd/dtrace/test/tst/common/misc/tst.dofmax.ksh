#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#

#
# Copyright (c) 2012, Joyent, Inc. All rights reserved.
#

let j=8

enable()
{
	prog=/var/tmp/dtest.$$.d
	err=/var/tmp/dtest.$$.err

	nawk -v nprobes=$1 'BEGIN { \
		for (i = 0; i < nprobes - 1; i++) { 		\
			printf("dtrace:::BEGIN,\n");		\
		}						\
								\
		printf("dtrace:::BEGIN { exit(0); }\n");	\
	}' /dev/null > $prog

	dtrace -qs $prog > /dev/null 2> $err

	if [[ "$?" -eq 0 ]]; then
		return 0
	else
		if ! grep "DIF program exceeds maximum program size" $err \
		    1> /dev/null 2>&1 ; then 
			echo "failed to enable $prog: `cat $err`"
			exit 1
		fi

		return 1
	fi
}

#
# First, establish an upper bound
#
let upper=1

while enable $upper ; do
	let lower=upper
	let upper=upper+upper
	echo success at $lower, raised to $upper
done

#
# Now search for the highest value that can be enabled
#
while [[ "$lower" -lt "$upper" ]]; do
	let guess=$(((lower + upper) / 2))
	echo "lower is $lower; upper is $upper; guess is $guess\c"

	if enable $guess ; then
		if [[ $((upper - lower)) -le 2 ]]; then
			let upper=guess
		fi

		echo " (success)"
		let lower=guess
	else
		echo " (failure)"
		let upper=guess
	fi
done

let expected=10000

if [[ "$lower" -lt "$expected" ]]; then
	echo "expected support for enablings of at least $expected probes; \c"
	echo "found $lower"
	exit 1
fi

echo "maximum supported enabled probes found to be $lower"
exit 0

