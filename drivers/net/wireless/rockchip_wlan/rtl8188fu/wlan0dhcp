#!/bin/bash

var0=`ps aux|awk '/dhclient wlan0/'|awk '$11!="awk"{print $2}'`

kill $var0
cp ifcfg-wlan0 /etc/sysconfig/network-scripts/

dhclient wlan0

var1=`ifconfig wlan0 |awk '/inet/{print $2}'|awk -F: '{print $2}'`


rm -f /etc/sysconfig/network-scripts/ifcfg-wlan0

echo "get ip: $var1"

