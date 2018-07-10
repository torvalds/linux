#!/usr/bin/awk -f
# AWK script to check for missing help entries for config options
#
# Copyright (C) 2006 Bernhard Reutner-Fischer
#
# This file is distributed under the terms and conditions of the
# MIT/X public licenses. See http://opensource.org/licenses/mit-license.html
# and notice http://www.gnu.org/licenses/license-list.html#X11License


/^choice/ { is_choice = 1; }
/^endchoice/ { is_choice = 0; }
/^config/ {
	pos++;
	conf[pos] = $2;
	file[pos] = FILENAME;
	if (is_choice) {
		help[pos] = 1; # do not warn about 'choice' config entries.
	} else {
		help[pos] = 0;
	}
}
/^[ \t]*help[ \t]*$/ {
	help[pos] = 1;
}
/^[ \t]*bool[ \t]*$/ {
	help[pos] = 1; # ignore options which are not selectable
}
BEGIN {
	pos = -1;
	is_choice = 0;
}
END {
	for (i = 0; i <= pos; i++) {
#	printf("%s: help for #%i '%s' == %i\n", file[i], i, conf[i], help[i]);
		if (help[i] == 0) {
			printf("%s: No helptext for '%s'\n", file[i], conf[i]);
		}
	}
}
