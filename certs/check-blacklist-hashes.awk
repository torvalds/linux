#!/usr/bin/awk -f
# SPDX-License-Identifier: GPL-2.0
#
# Copyright © 2020, Microsoft Corporation. All rights reserved.
#
# Author: Mickaël Salaün <mic@linux.microsoft.com>
#
# Check that a CONFIG_SYSTEM_BLACKLIST_HASH_LIST file contains a valid array of
# hash strings.  Such string must start with a prefix ("tbs" or "bin"), then a
# colon (":"), and finally an even number of hexadecimal lowercase characters
# (up to 128).

BEGIN {
	RS = ","
}
{
	if (!match($0, "^[ \t\n\r]*\"([^\"]*)\"[ \t\n\r]*$", part1)) {
		print "Not a string (item " NR "):", $0;
		exit 1;
	}
	if (!match(part1[1], "^(tbs|bin):(.*)$", part2)) {
		print "Unknown prefix (item " NR "):", part1[1];
		exit 1;
	}
	if (!match(part2[2], "^([0-9a-f]+)$", part3)) {
		print "Not a lowercase hexadecimal string (item " NR "):", part2[2];
		exit 1;
	}
	if (length(part3[1]) > 128) {
		print "Hash string too long (item " NR "):", part3[1];
		exit 1;
	}
	if (length(part3[1]) % 2 == 1) {
		print "Not an even number of hexadecimal characters (item " NR "):", part3[1];
		exit 1;
	}
}
