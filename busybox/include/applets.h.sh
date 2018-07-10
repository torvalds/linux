#!/bin/sh
#
# This script allows to check whether every applet has a separate option
# enabling it. Run it after applets.h is generated.

# CONFIG_applet names
grep ^IF_ applets.h | grep -v ^IF_FEATURE_ | sed 's/IF_\([A-Z0-9._-]*\)(.*/\1/' \
| sort | uniq \
>applets_APP1

# command line applet names
grep ^IF_ applets.h | sed -e's/ //g' -e's/.*(\([a-z[][^,]*\),.*/\1/' \
| grep -v '^bash$' \
| grep -v '^sh$' \
| tr a-z A-Z \
| sed 's/^SYSCTL$/BB_SYSCTL/' \
| sed 's/^\[\[$/TEST1/' \
| sed 's/^\[$/TEST2/' \
| sort | uniq \
>applets_APP2

diff -u applets_APP1 applets_APP2 >applets_APP.diff
#rm applets_APP1 applets_APP2
