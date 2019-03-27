#!/usr/local/bin/perl
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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2014, 2016 by Delphix. All rights reserved.
#

require 5.8.4;

$PNAME = $0;
$PNAME =~ s:.*/::;
$USAGE = "Usage: $PNAME [file ...]\n";
$errs = 0;

sub err
{
	my($msg) = @_;

	print "$file: $lineno: $msg\n";
	$errs++;
}

sub dstyle
{
	open(FILE, "$file");
	$lineno = 0;
	$inclause = 0;
	$skipnext = 0;

	while (<FILE>) {
		$lineno++;

		chop;

		if ($skipnext) {
			$skipnext = 0;
			next;
		}

		#
		# Amazingly, some ident strings are longer than 80 characters!
		#
		if (/^#pragma ident/) {
			next;
		}

		#
		# The algorithm to calculate line length from cstyle.
		#
		$line = $_;
		if ($line =~ tr/\t/\t/ * 7 + length($line) > 80) {
			# yes, there is a chance.
			# replace tabs with spaces and check again.
			$eline = $line;
			1 while $eline =~
			    s/\t+/' ' x (length($&) * 8 - length($`) % 8)/e;

			if (length($eline) > 80) {
				err "line > 80 characters";
			}
		}

		if (/\/\*DSTYLED\*\//) {
			$skipnext = 1;
			next;
		}

		if (/^#pragma/) {
			next;
		}

		if (/^#include/) {
			next;
		}

		#
		# Before we do any more analysis, we want to prune out any
		# quoted strings.  This is a bit tricky because we need
		# to be careful of backslashed quotes within quoted strings.
		# I'm sure there is a very crafty way to do this with a
		# single regular expression, but that will have to wait for
		# somone with better regex juju that I; we do this by first
		# eliminating the backslashed quotes, and then eliminating
		# whatever quoted strings are left.  Note that we eliminate
		# the string by replacing it with "quotedstr"; this is to
		# allow lines to end with a quoted string.  (If we simply
		# eliminated the quoted string, dstyle might complain about
		# the line ending in a space or tab.)
		# 
		s/\\\"//g;
		s/\"[^\"]*\"/quotedstr/g;

		if (/[ \t]$/) {
			err "space or tab at end of line";
		}

		if (/^[\t]+[ ]+[\t]+/) {
			err "spaces between tabs";
		}

		if (/^[\t]* \*/) {
			next;
		}

		if (/^        /) {
			err "indented by spaces not tabs";
		}

		if (/^{}$/) {
			next;
		}

		if (!/^enum/ && !/^\t*struct/ && !/^\t*union/ && !/^typedef/ &&
		    !/^translator/ && !/^provider/ && !/\tif / &&
		    !/ else /) {
			if (/[\w\s]+{/) {
				err "left brace not on its own line";
			}

			if (/{[\w\s]+/) {
				err "left brace not on its own line";
			}
		}

		if (!/;$/ && !/\t*}$/ && !/ else /) {
			if (/[\w\s]+}/) {
				err "right brace not on its own line";
			}

			if (/}[\w\s]+/) {
				err "right brace not on its own line";
			}
		}

		if (/^}/) {
			$inclause = 0;
		}

		if (!$inclause && /^[\w ]+\//) {
			err "predicate not at beginning of line";
		}

		if (!$inclause && /^\/[ \t]+\w/) {
			err "space between '/' and expression in predicate";
		}

		if (!$inclause && /\w[ \t]+\/$/) {
			err "space between expression and '/' in predicate";
		}

		if (!$inclause && /\s,/) {
			err "space before comma in probe description";
		}

		if (!$inclause && /\w,[\w\s]/ && !/;$/) {
			if (!/extern/ && !/\(/ && !/inline/) {
				err "multiple probe descriptions on same line";
			}
		}

		if ($inclause && /sizeof\(/) {
			err "missing space after sizeof";
		}

		if ($inclause && /^[\w ]/) {
			err "line doesn't begin with a tab";
		}

		if ($inclause && /,[\w]/) {
			err "comma without trailing space";
		}

		if (/\w&&/ || /&&\w/ || /\w\|\|/ || /\|\|\w/) {
			err "logical operator not set off with spaces";
		}

		#
		# We want to catch "i<0" variants, but we don't want to
		# erroneously flag translators.
		#
		if (!/\w<\w+>\(/) {
			if (/\w>/ || / >\w/ || /\w</ || /<\w/) {
				err "comparison operator not set " . 
				    "off with spaces";
			}
		}

		if (/\w==/ || /==\w/ || /\w<=/ || />=\w/ || /\w!=/ || /!=\w/) {
			err "comparison operator not set off with spaces";
		}

		if (/\w=/ || /=\w/) {
			err "assignment operator not set off with spaces";
		}

		if (/^{/) {
			$inclause = 1;
		}
        }
}

foreach $arg (@ARGV) {
	if (-f $arg) {
		push(@files, $arg);
	} else {
		die "$PNAME: $arg is not a valid file\n";
	}
}

die $USAGE if (scalar(@files) == 0);

foreach $file (@files) {
	dstyle($file);
}

exit($errs != 0);
