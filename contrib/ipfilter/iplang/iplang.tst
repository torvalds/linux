#
interface { ifname le0; mtu 1500; } ;

ipv4 {
	src 1.1.1.1; dst 2.2.2.2;
	tcp {
		seq 12345; ack 0; sport 9999; dport 23; flags S;
		data { value "abcdef"; } ;
	} ;
} ;
send { via 10.1.1.1; } ;
