# GNU objdump version checker
#
# Usage:
# objdump -v | awk -f chkobjdump.awk
BEGIN {
	# objdump version 2.19 or later is OK for the test.
	od_ver = 2;
	od_sver = 19;
}

/^GNU/ {
	split($3, ver, ".");
	if (ver[1] > od_ver ||
	    (ver[1] == od_ver && ver[2] >= od_sver)) {
		exit 1;
	} else {
		printf("Warning: objdump version %s is older than %d.%d\n",
		       $4, od_ver, od_sver);
		print("Warning: Skipping posttest.");
		# Logic is inverted, because we just skip test without error.
		exit 0;
	}
}
