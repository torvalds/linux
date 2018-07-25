#!/bin/bash

echo "=================================================="
echo "1.Copy firmware"
echo "=================================================="
cp ../image/ssv6200-sw.bin /lib/firmware/

echo "=================================================="
echo "1.Unload Module"
echo "=================================================="
./unload_ap.sh
./unload.sh
./clean_log.sh

echo "=================================================="
echo "2.Set Hardware Capability"
echo "=================================================="
ifconfig eth0 down
ifconfig eth0 > /dev/null
if [ $? ]; then
    eth0_local_mac=`ifconfig eth0  | grep HWaddr | sed -e 's/.*HWaddr //g' | sed -e 's/ //g' | cut -d ':' -f 4,5,6`
    echo Use eth0 MAC address. 
else
    eth0_local_mac="45:67:89"
    echo No eth0 found use defaul MAC address.
fi

ap_prefix=`echo $eth0_local_mac | cut -d ':' -f 1,2`
ap_prefix=`echo $ap_prefix | sed -e 's/://g'`

local_mac=00:a8:b8:$eth0_local_mac
local_mac_2=`echo $local_mac | cut -d ':' -f 6`
local_mac_2=`printf '%x' $[ ( 16#$local_mac_2 + 1 ) % 4 + ( 16#$local_mac_2 & 16#FC ) ] `
ap_prefix="AP_$ap_prefix$local_mac_2"
local_mac_2="`echo $local_mac | cut -d ':' -f 1,2,3,4,5`:$local_mac_2"

echo Primary WLAN MAC is $local_mac
echo Secondary WLAN MAC is $local_mac_2
    
cat sta.cfg.template | sed -e "s/MAC_ADDR/$local_mac/g" | sed -e "s/MAC2ADDR/$local_mac_2/g" > sta_local_mac.cfg
./ssvcfg.sh sta_local_mac.cfg

echo "=================================================="
echo "3.Load MMC Module"
echo "=================================================="
modprobe mmc_core
modprobe sdhci
modprobe sdhci-pci
modprobe mmc_block

echo "=================================================="
echo "4.Load SSV6200 Driver"
echo "=================================================="
echo 6 > /proc/sys/kernel/printk

modprobe ssv6200_sdio

sleep 1

ssv_phy=`./find_ssv_phy`
if [ -z "$ssv_phy" ]; then
    echo SSV PHY device not found.;
    exit 1;
fi

ssv_wlan_1=`./find_ssv_wlan`
if [ -z "$ssv_wlan_1" ]; then
    echo SSV primary WLAN device not found.;
    exit 1;
fi

echo "Primary SSV WLAN interface is $ssv_wlan_1"

ssv_wlan_2=`echo $ssv_wlan_1 | sed -e s/wlan//g`
ssv_wlan_2=`expr $ssv_wlan_2 + 1`
ssv_wlan_2="wlan$ssv_wlan_2"
echo Second WLAN interface is $ssv_wlan_2

echo "Add second interface $ssv_wlan_2 to SSV PHY device $ssv_phy"
iw $ssv_phy interface add $ssv_wlan_2 type station

sleep 1

trap handle_stop INT

function handle_stop() {
    nmcli nm wifi on

    ./unload_ap.sh
    ./unload.sh
    
    ifconfig eth0 up
    
    echo AP mode stopped
}
        
ssv_wlans="`./find_ssv_wlan`"
for ssv_wlan in $ssv_wlans; do
    if [ $ssv_wlan != $ssv_wlan_1 ]; then
        echo Second SSV WLAN device is actually $ssv_wlan
        ./ap_sta.sh $ssv_wlan $ssv_wlan_1 $ap_prefix
        break;
    fi
done

