#/bin/bash

KVERSION="`uname -r`"
kern_mod=/lib/modules/$KVERSION/kernel/drivers/net/wireless/ssv6200/ssvdevicetype.ko
type_str=`lsmod | grep "ssvdevicetype"`
cfg_file=sta.cfg
if [ $# -ge 1 ]; then 
    cfg_file=$1; 
    echo Using configuration file $1
else
    echo Using default configuration file $cfg_file \($?\)
fi
cfg_cmds=(`cat $cfg_file  | grep '^[a-zA-Z0-9]' | sed 's/ //g'`)
#echo ${#cfg_cmds[*]}
#echo ${!cfg_cmds[*]}
#echo ${cfg_cmds[1]}

if [ "$type_str" != "" ]; then
    #rmmod ssv6200_sdio
    #rmmod ssv6200s_core
    #rmmod ssv6200_hci
    rmmod ssvdevicetype
fi


if [ -f $kern_mod ]; then
    insmod $kern_mod stacfgpath="$cfg_file"
    #insmod $kern_mod
    #./cli cfg reset
    #for cmd in ${cfg_cmds[*]}
    #do
	#./cli cfg `echo $cmd | sed 's/=/ = /g'`
    #done
fi

