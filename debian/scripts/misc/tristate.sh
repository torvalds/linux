#!/bin/bash

#
# Find config variables that might be able to transition from =y to =m
#
# Example: debian/scripts/misc/tristate.sh debian.master/config/config.common.ubuntu
#

KC=Kconfig.tmp
rm -f ${KC}
find .|grep Kconfig | while read f
do
	cat $f >> ${KC}
done

grep =y $1 | sed -e 's/CONFIG_//' -e 's/=y//' | while read c
do
	cat <<EOF > tristate.awk
BEGIN { tristate=0; }
/^config ${c}\$/ { tristate=1; next; }
/tristate/ { if (tristate == 1) printf("CONFIG_%s=m\n","${c}"); next; }
{ if (tristate == 1) exit; }
EOF

	gawk -f tristate.awk ${KC}
done
