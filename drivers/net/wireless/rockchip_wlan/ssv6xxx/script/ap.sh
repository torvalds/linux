#!/bin/bash

echo "=================================================="
echo "1.Copy firmware"
echo "=================================================="
cp ../image/ssv6200-sw.bin /lib/firmware/

echo "=================================================="
echo "1.Unload Module"
echo "=================================================="
./unload.sh

echo "=================================================="
echo "2.Set Hardware Capability"
echo "=================================================="

eth0_local_mac=`ifconfig eth0  | grep HWaddr | sed -e 's/.*HWaddr //g' | sed -e 's/ //g' | cut -d ':' -f 4,5,6`
[ "$eth0_local_mac" == "" ] && eth0_local_mac="45:67:89"
local_mac=00:aa:bb:$eth0_local_mac
local_mac_2=00:00:00:00:00:00
echo WLAN MAC is $local_mac

cat sta.cfg.template | sed -e "s/MAC_ADDR/$local_mac/g" | sed -e "s/MAC2ADDR/$local_mac_2/g" > sta_local_mac.cfg
./ssvcfg.sh sta_local_mac.cfg

echo "=================================================="
echo "3.Load MMC Module"
echo "=================================================="
modprobe mmc_core sdiomaxclock=25000000
modprobe sdhci
modprobe sdhci-pci
modprobe mmc_block

echo "=================================================="
echo "4.Load SSV6200 Driver"
echo "=================================================="
echo 6 > /proc/sys/kernel/printk

modprobe ssv6200_sdio

