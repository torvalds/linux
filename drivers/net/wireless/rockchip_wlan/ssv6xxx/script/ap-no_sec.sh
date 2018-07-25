#!/bin/bash
# ------------------------------
SSV_WLAN=`./find_ssv_wlan`

if [[ _$SSV_WLAN != _wlan* ]]; then
    echo "No SSV WLAN device found."
    exit 1;
fi

echo SSV device is $SSV_WLAN

nmcli nm wifi off
sudo rfkill unblock wlan

./unload_ap.sh
 
ifconfig $SSV_WLAN 192.168.33.1 netmask 255.255.255.0
dhcpd -c dhcpd.cfg -pf /var/run/dhcp-server/dhcpd.pid $SSV_WLAN
bash -c "echo 1 >/proc/sys/net/ipv4/ip_forward" 
iptables -t nat -A POSTROUTING -o eth0 -j MASQUERADE
cat hostapd.conf.NO_SEC.template | sed -s s/HOSTAPD_IF/$SSV_WLAN/g  > hostapd.conf
hostapd hostapd.conf

nmcli nm wifi on
# ------------------------------------
