#!/bin/bash

if [ "`which iwconfig`" = "" ] ; then 
	echo "WARNING:Wireless tool not exist!"
	echo "        Please install it!"
	exit
else
	if [ `uname -r | cut -d. -f2` -eq 4 ]; then
		wpa_supplicant -D ipw -c wpa1.conf -i wlan0	
	else
	if [ `iwconfig -v |awk '{print $4}' | head -n 1` -lt  18 ] ; then
		wpa_supplicant -D ipw -c wpa1.conf -i wlan0  
	else	  
		wpa_supplicant -D wext -c wpa1.conf -i wlan0 
	fi

	fi
fi


