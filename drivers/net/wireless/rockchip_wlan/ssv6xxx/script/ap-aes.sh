#!/bin/bash
# ------------------------------

# Clean up first.
./unload_ap.sh
./unload.sh
./clean_log.sh

# Load driver for AP mode.
./ap.sh

sleep 2

# Check interface
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

# Stop network manager from handling WiFi
nmcli nm wifi off
sudo rfkill unblock wlan

# Configure
ifconfig $SSV_WLAN 192.168.33.1 netmask 255.255.255.0
cp dhcpd.conf /tmp/dhcpd_$SSV_WLAN.conf
dhcpd -cf /tmp/dhcpd_$SSV_WLAN.conf -pf /var/run/dhcp-server/dhcpd.pid $SSV_WLAN
bash -c "echo 1 >/proc/sys/net/ipv4/ip_forward" 
iptables -t nat -A POSTROUTING -o eth0 -j MASQUERADE
ap_name=`./get_mac $SSV_WLAN | cut -d ':' -f 3,4,5 | sed -e s/://g`
ap_name=AP_$ap_name
cat hostapd.conf.AES.template | sed -s s/HOSTAPD_IF/$SSV_WLAN/g | sed -s s/TestAP/$ap_name/g > hostapd.conf

trap handle_stop INT

function handle_stop() {
    nmcli nm wifi on

    ./unload_ap.sh
    ./unload.sh
    
    echo AP mode stopped
}
        
/usr/sbin/hostapd hostapd.conf
