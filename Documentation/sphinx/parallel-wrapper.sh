#!/bin/sh
# SPDX-License-Identifier: GPL-2.0+
#
# Figure out if we should follow a specific parallelism from the make
# environment (as exported by scripts/jobserver-exec), or fall back to
# the "auto" parallelism when "-jN" is not specified at the top-level
# "make" invocation.

sphinx="$1"
shift || true

parallel="$PARALLELISM"
if [ -z "$parallel" ] ; then
	# If no parallelism is specified at the top-level make, then
	# fall back to the expected "-jauto" mode that the "htmldocs"
	# target has had.
	auto=$(perl -e 'open IN,"'"$sphinx"' --version 2>&1 |";
			while (<IN>) {
				if (m/([\d\.]+)/) {
					print "auto" if ($1 >= "1.7")
				}
			}
			close IN')
	if [ -n "$auto" ] ; then
		parallel="$auto"
	fi
fi
# Only if some parallelism has been determined do we add the -jN option.
if [ -n "$parallel" ] ; then
	parallel="-j$parallel"
fi

exec "$sphinx" "$parallel" "$@"
