#!/bin/sh
#
# Just process the CPP output from systbl_chk.c and complain
# if anything is out of order.
#
# Copyright © 2008 IBM Corporation
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version
# 2 of the License, or (at your option) any later version.

awk	'BEGIN { num = -1; }	# Ignore the beginning of the file
	/^#/ { next; }
	/^[ \t]*$/ { next; }
	/^START_TABLE/ { num = 0; next; }
	/^END_TABLE/ {
		if (num != $2) {
			printf "Error: NR_syscalls (%s) is not one more than the last syscall (%s)\n",
				$2, num - 1;
			exit(1);
		}
		num = -1;	# Ignore the rest of the file
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
