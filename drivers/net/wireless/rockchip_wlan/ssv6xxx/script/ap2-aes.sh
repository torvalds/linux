#!/bin/bash
# ------------------------------

if [[ _$1 = _wlan* ]]; then
    SSV_WLAN=$1
else
    SSV_WLAN=`./find_ssv_wlan`

    if     [[ _$SSV_WLAN != _wlan* ]]; then
        echo "No SSV WLAN device found."
        exit 1;
    fi
fi
echo SSV device for AP mode is $SSV_WLAN

#nmcli nm wifi off
#sudo rfkill unblock wlan

./unload_ap.sh
 
ifconfig $SSV_WLAN 192.168.33.1 netmask 255.255.255.0
cp dhcpd.conf /tmp/dhcpd_$SSV_WLAN.conf
dhcpd -cf /tmp/dhcpd_$SSV_WLAN.conf -pf /var/run/dhcp-server/dhcpd.pid $SSV_WLAN
bash -c "echo 1 >/proc/sys/net/ipv4/ip_forward" 
iptables -t nat -A POSTROUTING -o eth0 -j MASQUERADE
cat hostapd.conf.AES.template | sed -s s/HOSTAPD_IF/$SSV_WLAN/g  > hostapd.conf
hostapd hostapd.conf

#nmcli nm wifi on

echo AP mode stopped
# ------------------------------------
