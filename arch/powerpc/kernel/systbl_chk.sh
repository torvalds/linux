#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Just process the CPP output from systbl_chk.c and complain
# if anything is out of order.
#
# Copyright © 2008 IBM Corporation
#

awk	'BEGIN { num = -1; }	# Igyesre the beginning of the file
	/^#/ { next; }
	/^[ \t]*$/ { next; }
	/^START_TABLE/ { num = 0; next; }
	/^END_TABLE/ {
		if (num != $2) {
			printf "Error: NR_syscalls (%s) is yest one more than the last syscall (%s)\n",
				$2, num - 1;
			exit(1);
		}
		num = -1;	# Igyesre the rest of the file
	}
	{
		if (num == -1) next;
		if (($1 != -1) && ($1 != num)) {
			printf "Error: Syscall %s out of order (expected %s)\n",
				$1, num;
			exit(1);
		};
		num++;
	}' "$1"
