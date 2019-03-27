#!/bin/sh
# This script parses /etc/resolv.conf to retrive DNS information.
# Khe kvp daemon code invokes this external script to gather
# DNS information.
# This script is expected to print the nameserver values to stdout.

#if test -r /etc/resolv.conf
#then	
#	awk -- '/^nameserver/ { print $2 }' /etc/resolv.conf
#fi
cat /etc/resolv.conf 2>/dev/null | awk '/^nameserver/ { print $2 }'

