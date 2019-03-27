block out log quick on qfe0 from 192.168.7.0/24 to any
block out log quick on nf0 from 192.168.6.0/24 to any
pass in quick on nf0 proto tcp from any to any port = 111 flags S keep state
pass in quick on nf0 proto udp from any to any port = 111 keep state
block return-rst in log quick on nf0 proto tcp from any to any
block in log quick on nf0 from 192.168.7.0/24 to any
block return-rst in log quick on qfe0 proto tcp from any to any
block in log quick on qfe0 from 192.168.6.0/24 to any

