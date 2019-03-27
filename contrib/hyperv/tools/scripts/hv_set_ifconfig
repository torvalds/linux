#!/bin/sh

# This script activates an interface based on the specified
# configuration. The kvp daemon code invokes this external script
# to configure the interface.
#
# The only argument to this script is the configuration file that is to
# be used to configure the interface.
#
# Here is the format of the ip configuration file:
#
# HWADDR=macaddr
# IF_NAME=interface name
# DHCP=yes (This is optional; if yes, DHCP is configured)
#
# IPADDR=ipaddr1
# IPADDR_1=ipaddr2
# IPADDR_x=ipaddry (where y = x + 1)
#
# NETMASK=netmask1
# NETMASK_x=netmasky (where y = x + 1)
#
# GATEWAY=ipaddr1
# GATEWAY_x=ipaddry (where y = x + 1)
#
# DNSx=ipaddrx (where first DNS address is tagged as DNS1 etc)
#
# IPV6 addresses will be tagged as IPV6ADDR, IPV6 gateway will be
# tagged as IPV6_DEFAULTGW and IPV6 NETMASK will be tagged as
# IPV6NETMASK.
#
# The host can specify multiple ipv4 and ipv6 addresses to be
# configured for the interface. Furthermore, the configuration
# needs to be persistent. A subsequent GET call on the interface
# is expected to return the configuration that is set via the SET
# call.
#

. $1

sed -i".bak" '/ifconfig_hn0="SYNCDHCP"/d' /etc/rc.conf
sed -i".bak" '/ifconfig_hn0="DHCP"/d' /etc/rc.conf

# MAC Address
ifconfig $IF_NAME ether $HWADDR 

# IP and Subnet Mask
ifconfig $IF_NAME inet $IP_ADDR netmask $SUBNET 

# DNS
sed -i".bak" '/nameserver/d' /etc/resolv.conf
echo "nameserver" $DNS >> /etc/resolv.conf 

#Gateway
# Need to implment if Gateway is not present 
route flush
route add default $GATEWAY
#route change default $GATEWAY

#/etc/rc.d/netif restart 
#/etc/rc.d/routing restart


# DHCP
if [ $DHCP -eq 1 ]
then
	echo ifconfig_hn0=\"DHCP\" >> /etc/rc.conf
	echo Enabled 
else
	echo Disabled DHCP >> /var/log/messages
	echo Disabled
fi
echo "Set IP-Injection Success"
