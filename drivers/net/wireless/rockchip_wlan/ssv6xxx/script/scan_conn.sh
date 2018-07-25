#!/bin/bash

wpa_ret ( )
{
	if [ $1 != "OK" ]
		then
		if [ $3 == 1 ]
			then
			wpa_cli remove_network $2>/dev/null
			echo "wpa_cli command failed on $2"
		fi
		exit 1
	fi
}

connect_ap ( )
{
	#if [ $1 -eq 1 ]
		#then
		ssid=\""$2"\"
		pw=\""$3"\"
		declare -i scantimes=1
		nw_no=$(wpa_cli add_network | sed '1d')
		ret=$(wpa_cli set_network $nw_no ssid $ssid | sed '1d')
		echo "Set ssid for nw $nw_no: $ret"
		wpa_ret $ret $nw_no 1
		if [ $5 == "WPA" -o $5 == "WPA2" ]
			then
			ret=$(wpa_cli set_network $nw_no psk $pw | sed '1d')
                       	echo "Set PSK: $ret"
                        wpa_ret $ret $nw_no 1
		elif [ $5 == "WEP40" -o $5 == "WEP128" -o $5 == "OPEN" ]
			then
			ret=$(wpa_cli set_network $nw_no key_mgmt NONE | sed '1d')
                       	echo "Set Key-mgmt: $ret"
                        wpa_ret $ret $nw_no 1
			ret=$(wpa_cli set_network $nw_no auth_alg OPEN | sed '1d')
                        echo "Set Aith-alg: $ret"
                        wpa_ret $ret $nw_no 1
			if [ $5 != "OPEN" ]
				then
				ret=$(wpa_cli set_network $nw_no wep_key0 $pw | sed '1d')
                        	echo "Set wep key: $ret"
                        	wpa_ret $ret $nw_no 1
			fi
		fi 
		#echo "Set proto"
		#ret=$(wpa_cli set_network $nw_no proto WPA | sed '1d')
		#wpa_cli status #echo $ret
		#wpa_ret $ret $nw_no 1	
		#echo "Set key_mgmt"
		#ret=$(wpa_cli set_network $nw_no key_mgmt WPA-PSK | sed '1d')
		#wpa_cli status #echo $ret
		#wpa_ret $ret $nw_no 1	
		#echo "Set pairwise"
		#ret=$(wpa_cli set_network $nw_no pairwise CCMP | sed '1d')
		#wpa_cli status #echo $ret
		#wpa_ret $ret $nw_no 1	
		#echo "Set group"
		#ret=$(wpa_cli set_network $nw_no group CCMP | sed '1d')
		#wpa_cli status #echo $ret
		#wpa_ret $ret $nw_no 1	
		ret=$(wpa_cli enable_network $nw_no | sed '1d')
		echo "Enable nw $nw_no: $ret"
		wpa_ret $ret $nw_no 1	
		#echo "Connect"
		#ret=$(wpa_cli reconnect | sed '1d')
		#wpa_cli status #echo $ret
		#wpa_ret $ret $nw_no 1	
		state=$(wpa_cli status -i $1 | grep wpa_state | sed 's/wpa_state=//g')
		form_st="INACTIVE"
		while [ $state != "COMPLETED" ]
		do
			if [ $form_st != $state ]
				then
				echo "Status: $state"
			fi
			if [ $state == "SCANNING" ]
				then
				sleep 1
			elif [ $state == "DISCONNECTED" ]
				then
				if [ $scantimes <= $4]
					then
					echo "Connect"
                			ret=$(wpa_cli reconnect | sed '1d')
					wpa_ret $ret $nw_no 1
					$scantimes=$scantimes + 1
				else
					echo "exceed scan times=$scantimes"
				fi
			fi
			form_st=$state
			state=$(wpa_cli status -i $1 | grep wpa_state | sed 's/wpa_state=//g')
		done
		if [ $state == "COMPLETED" ]
			then
			echo "Connected and request for IP address"
			dhclient -4 $1 >/dev/null
		fi
	#fi
}

if [ $# -eq 5 ]
	then
	echo "Scanning"
	declare -i total_scantime=$4
        for((i=1; i<=$4; i=i+1))
	do
		echo "scan loop:$i" 
		scan_st=$(wpa_cli scan -i $1)
		total_scantime=total_scantime-1
		sleep 2
		result=$(wpa_cli scan_results -i $1 | grep -c $2)
		if [ $result -eq 1 ]
			then
			echo "Target AP $2 is found."
			break;
		fi
	done
        if [ $result -eq 1 ]
		then
		connect_ap $1 $2 $3 $total_scantime $5
	else
		echo "Target ap is not found"
	fi


elif [ $3 == "off" ]
	then
	echo "Disconnect AP"
	dhclient -r $1 >/dev/null
	dhclient -x >/dev/null 
	nw_no=$(wpa_cli list_network | grep CURRENT | awk '{print $1}')
	echo "Disable network"
	ret=$(wpa_cli disable_network $nw_no | sed '1d')
	wpa_ret $ret $nw_no 1	
	echo "Remove network"
	ret=$(wpa_cli remove_network $nw_no | sed '1d')
	wpa_ret $ret $nw_no 0	
else
	echo "====================================================================================="
	echo "manual scan & connect script"
	echo "./scan_conn.sh <interface> <BSSID> <PW> <scan_times>"
	echo "./scan_conn.sh <interface> <BSSID> off"
	echo "====================================================================================="
fi

