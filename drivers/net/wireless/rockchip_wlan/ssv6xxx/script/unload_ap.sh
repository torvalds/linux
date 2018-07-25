#/bin/bash

hostapd_pid=`pgrep hostapd`
[ $? -eq 0 ] && (echo "\nKilling hostapd..."; kill -KILL $hostapd_pid)

dhcpd_pid=`pgrep dhcpd`
[ $? -eq 0 ] && (echo "\nKilling dhcpd..."; kill -KILL $dhcpd_pid)

