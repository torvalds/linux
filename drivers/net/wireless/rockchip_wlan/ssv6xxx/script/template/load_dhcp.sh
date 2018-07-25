#/bin/bash
BLUE='\e[1;34m'
GREEN='\e[1;32m'
CYAN='\e[1;36m'
RED='\e[1;31m'
PURPLE='\e[1;35m'
YELLOW='\e[1;33m'
# No Color
NC='\e[0m'

echo -e "${YELLOW}Load DHCP...${NC}"

echo -e "${GREEN}Config IP.${NC}"
ifconfig wlan@@ up
ifconfig wlan@@ 192.168.0.1 netmask 255.255.255.0

echo -e "${GREEN}Config DHCP Server.${NC}"
service isc-dhcp-server start
sleep 3

echo -e "${GREEN}Config routing table.${NC}"
bash -c "echo 1 >/proc/sys/net/ipv4/ip_forward"
iptables -t nat -A POSTROUTING -o eth0 -j MASQUERADE
