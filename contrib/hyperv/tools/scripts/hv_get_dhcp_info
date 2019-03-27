#!/bin/sh

# This is the script retrieves the DHCP state of a given interface.
# The kvp daemon code invokes this external script to gather
# DHCP setting for the specific interface.
#
# Input: Name of the interface
#
# Output: The script prints the string "Enabled" to stdout to indicate
#	that DHCP is enabled on the interface. If DHCP is not enabled,
#	the script prints the string "Disabled" to stdout.
#

. /etc/rc.subr
. /etc/network.subr

load_rc_config netif

if dhcpif hn0;
then
echo "Enabled"
else
echo "Disabled"
fi
