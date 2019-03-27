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

if [ "$CMD" = "P2P-GROUP-STARTED" ]; then
    GIFNAME=$3
    if [ "$4" = "GO" ]; then
	kill_daemon udhcpc /var/run/udhcpc-$GIFNAME.pid
	ifconfig $GIFNAME 192.168.42.1 up
	udhcpd /etc/udhcpd-p2p.conf
    fi
    if [ "$4" = "client" ]; then
	kill_daemon udhcpc /var/run/udhcpc-$GIFNAME.pid
	kill_daemon udhcpd /var/run/udhcpd-$GIFNAME.pid
	udhcpc -i $GIFNAME -p /var/run/udhcpc-$GIFNAME.pid \
		-s /etc/udhcpc.script
    fi
fi

if [ "$CMD" = "P2P-GROUP-REMOVED" ]; then
    GIFNAME=$3
    if [ "$4" = "GO" ]; then
	kill_daemon udhcpd /var/run/udhcpd-$GIFNAME.pid
	ifconfig $GIFNAME 0.0.0.0
    fi
    if [ "$4" = "client" ]; then
	kill_daemon udhcpc /var/run/udhcpc-$GIFNAME.pid
	ifconfig $GIFNAME 0.0.0.0
    fi
fi

if [ "$CMD" = "P2P-CROSS-CONNECT-ENABLE" ]; then
    GIFNAME=$3
    UPLINK=$4
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
