#!/bin/sh

IFNAME=$1
CMD=$2

kill_daemon() {
    NAME=$1
    PF=$2

    if [ ! -r $PF ]; then
	return
    fi

    PID=`cat $PF`
    if [ $PID -gt 0 ]; then
	if ps $PID | grep -q $NAME; then
	    kill $PID
	fi
    fi
    rm $PF
}


echo "$1 $2 $3 $4"
if [ "$CMD" = "P2P-GROUP-STARTED" ]; then
    GIFNAME=$3
    echo "P2P-GROUP-STARTED $3 $4"
    if [ "$4" = "GO" ]; then
	kill_daemon dhclient /var/run/dhclient-$GIFNAME.pid
	rm /var/run/dhclient.leases-$GIFNAME
	kill_daemon dnsmasq /var/run/dnsmasq.pid-$GIFNAME
	ifconfig $GIFNAME 192.168.42.1 up
	if ! dnsmasq -x /var/run/dnsmasq.pid-$GIFNAME \
	    -i $GIFNAME \
	    -F192.168.42.11,192.168.42.99; then
	    # another dnsmasq instance may be running and blocking us; try to
	    # start with -z to avoid that
	    dnsmasq -x /var/run/dnsmasq.pid-$GIFNAME \
		-i $GIFNAME \
		-F192.168.42.11,192.168.42.99 --listen-address 192.168.42.1 -z -p 0
	fi
    fi
    if [ "$4" = "client" ]; then
	kill_daemon dhclient /var/run/dhclient-$GIFNAME.pid
	rm /var/run/dhclient.leases-$GIFNAME
	kill_daemon dnsmasq /var/run/dnsmasq.pid-$GIFNAME
	ipaddr=`echo "$*" | sed 's/.* ip_addr=\([^ ]*\).*/\1/'`
	ipmask=`echo "$*" | sed 's/.* ip_mask=\([^ ]*\).*/\1/'`
	goipaddr=`echo "$*" | sed 's/.* go_ip_addr=\([^ ]*\).*/\1/'`
	if echo "$ipaddr$ipmask$goipaddr" | grep -q ' '; then
	    ipaddr=""
	    ipmask=""
	    goipaddr=""
	fi
	if [ -n "$ipaddr" ]; then
	    sudo ifconfig $GIFNAME "$ipaddr" netmask "$ipmask"
	    sudo ip ro re default via "$goipaddr"
	    exit 0
	fi
	dhclient -pf /var/run/dhclient-$GIFNAME.pid \
	    -lf /var/run/dhclient.leases-$GIFNAME \
	    -nw \
	    $GIFNAME
    fi
fi
#if [ "$CMD" = "CONNECTED" ]; then

#   echo connect $IFNAME, dhclient for it > /dev/console

#   dhclient $IFNAME

#fi

#if [ "$CMD" = "DISCONNECTED" ]; then

#   echo disconnect $IFNAME, kill dhclient for it > /dev/console

#   killall dhclient

#fi

if [ "$CMD" = "P2P-GROUP-REMOVED" ]; then
    GIFNAME=$3
    if [ "$4" = "GO" ]; then
	kill_daemon dnsmasq /var/run/dnsmasq.pid-$GIFNAME
	ifconfig $GIFNAME 0.0.0.0
    fi
    if [ "$4" = "client" ]; then
	kill_daemon dhclient /var/run/dhclient-$GIFNAME.pid
	rm /var/run/dhclient.leases-$GIFNAME
	ifconfig $GIFNAME 0.0.0.0
    fi
fi

if [ "$CMD" = "P2P-CROSS-CONNECT-ENABLE" ]; then
    GIFNAME=$3
    UPLINK=$4
    echo "p2p-corss-connect-enable ifname=$GIFNAME upLink=$UPLINK"
    # enable NAT/masquarade $GIFNAME -> $UPLINK
    iptables -P FORWARD DROP
    iptables -t nat -A POSTROUTING -o $UPLINK -j MASQUERADE
    iptables -A FORWARD -i $UPLINK -o $GIFNAME -m state --state RELATED,ESTABLISHED -j ACCEPT
    iptables -A FORWARD -i $GIFNAME -o $UPLINK -j ACCEPT
    sysctl net.ipv4.ip_forward=1
fi

if [ "$CMD" = "P2P-CROSS-CONNECT-DISABLE" ]; then
    GIFNAME=$3
    UPLINK=$4
    # disable NAT/masquarade $GIFNAME -> $UPLINK
    sysctl net.ipv4.ip_forward=0
    iptables -t nat -D POSTROUTING -o $UPLINK -j MASQUERADE
    iptables -D FORWARD -i $UPLINK -o $GIFNAME -m state --state RELATED,ESTABLISHED -j ACCEPT
    iptables -D FORWARD -i $GIFNAME -o $UPLINK -j ACCEPT
fi
