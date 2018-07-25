#!/bin/bash

read_cfg( )
{
	cat $1|grep $2|awk '{print $3}'
	return 
	
}

if [ "$1" == "icqc" ] 
	then
	:;
# for user manual
elif [ "$1" == "h" ] 
	then

echo "====================================================================================="
echo "Unload/Reload & scan Flows"
#echo "Format = ./scan_test.sh <total.times.of.unload/reload> <target.IP.for.pinging> <timeout period> <dev name> <SSID> <PW> <scan times> <Saved file name>"
echo "Format = ./scan_test.sh <config file> <Saved file name>"
echo "System Log of each unload/reload will be saved in folder [UnloadReloadScan]"
echo "====================================================================================="


else
	# f=failed times;  p=passed times
	declare -i f=0
	declare -i p=0
	declare -i pingok=0
	
	#expired parameter, ready to be removed.	
	declare -i taketime=0
	
	setting=(total_times_of_unload target_IP_for_pinging timeout_period security device_name SSID PSK_phrase scan_times)

	test ! -f $1 && echo "The cfg file is not existed!!!"
	for((index=0 ; index < ${#setting[@]} ; index = index + 1))
   		do
 		VARS[$index]=$(cat $1|grep ${setting[$index]}|awk '{print $3}')
		#echo $index : ${VARS[$index]}
	done
	#exit 0
	echo "################################################################################" >> $2
	echo "Unload/Reload test start at   : `date`" >> $2 	
	echo "Prepare $1 times unload/reload test"
	mkdir UnloadReloadScan
	#start test loop, loop count in [i]	
	for ((i = 1 ; i <= ${VARS[0]} ; i = i + 1))
	do
	
	echo "Loading driver..."
	sleep 2
	echo "!!!Current round of testing will begin in 3 seconds, please prepare to sniffer packets!!!"
	sleep 3
	./load.sh
	./scan_conn.sh ${VARS[4]} ${VARS[5]} ${VARS[6]} ${VARS[7]} ${VARS[3]} & #$4 $5 $6 $7 $3 &
	# take $3 parameter as the period of [waiting for association ready and DHCP protocol completed].
	echo "****** Ping start!!  ******"
	for((w=1;w<=${VARS[2]};w=w+1))
	do
		if ping -W 1 -c 1 ${VARS[1]} >/dev/null; then
			pingok=1
			echo "LOOP[$i] : Connection established in $w seconds. (Passed / Failed / Total = $p / $f / ${VARS[0]})" >> $2		
			break
		else
			pingok=0
		fi

		echo $w
		sleep 1
	done

	if [ "$pingok" = 1 ]; then
		pingok=0
    		p=$p+1
		echo "****** Ping Passed!! (Passed / Failed / Total = $p / $f / ${VARS[0]})******"
		dmesg -c > "./UnloadReloadScan/Log.of.Passed.Loop[$i].txt"
		echo "================================================================================" >> "./UnloadReloadScan/Log.of.Passed.Loop[$i].txt"
		echo "`date` :Interface Information:" >> "./UnloadReloadScan/Log.of.Passed.Loop[$i].txt"
		ifconfig >> "./UnloadReloadScan/Log.of.Passed.Loop[$i].txt"
		echo "================================================================================" >> "./UnloadReloadScan/Log.of.Passed.Loop[$i].txt"

		sleep 2
	else    
		pingok=0		
		f=$f+1
		echo "LOOP[$i] : Connection establishing timeout ! (Passed / Failed / Total = $p / $f / ${VARS[0]})" >> $2
		echo "****** Ping Failed!! (Passed / Failed / Total = $p / $f / ${VARS[0]})******"
		dmesg -c > "./UnloadReloadScan/Log.of.Failed.Loop[$i].txt"
		echo "================================================================================" >> "./UnloadReloadScan/Log.of.Failed.Loop[$i].txt"
		echo "`date` :Interface Information:" >> "./UnloadReloadScan/Log.of.Failed.Loop[$i].txt"
		ifconfig >> "./UnloadReloadScan/Log.of.Failed.Loop[$i].txt"
		echo "================================================================================" >> "./UnloadReloadScan/Log.of.Failed.Loop[$i].txt"
	fi
	
	./scan_conn.sh ${VARS[4]} ${VARS[5]} off
	sleep 1
	echo "unloading driver"
	./unload.sh
	echo "========================================================="
	echo "Current loop = $i"		
	echo "Current Result : Passed / Failed / Total = $p / $f / ${VARS[0]}"		
	echo "========================================================="
	if [ "$i" = "${VARS[0]}" ]; then
		echo "All test loops were finished..."
		echo "Unload/Reload test finished at: `date`" >> $2
		echo "Result : Passed / Failed / Total = $p / $f / ${VARS[0]}" >> $2
		echo "################################################################################" >> $2
		echo " " >> $2		
	else		
		#take a break for next loop.
		echo "!!!Current round of testing is ended, please stop sniffer packets!!!"
		sleep 3
	fi
	done
fi









