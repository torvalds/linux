/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2002 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 */

#ifndef __UM_NET_USER_H__
#define __UM_NET_USER_H__

#define ETH_ADDR_LEN (6)
#define ETH_HEADER_ETHERTAP (16)
#define ETH_HEADER_OTHER (26) /* 14 for ethernet + VLAN + MPLS for crazy people */
#define ETH_MAX_PACKET (1500)

#define UML_NET_VERSION (4)

struct net_user_info {
	int (*init)(void *, void *);
	int (*open)(void *);
	void (*close)(int, void *);
	void (*remove)(void *);
	void (*add_address)(unsigned char *, unsigned char *, void *);
	void (*delete_address)(unsigned char *, unsigned char *, void *);
	int max_packet;
	int mtu;
};

extern void ether_user_init(void *data, void *dev);
extern void iter_addresses(void *d, void (*cb)(unsigned char *,
					       unsigned char *, void *),
			   void *arg);

extern void *get_output_buffer(int *len_out);
extern void free_output_buffer(void *buffer);

extern int tap_open_common(void *dev, char *gate_addr);
extern void tap_check_ips(char *gate_addr, unsigned char *eth_addr);

extern void read_output(int fd, char *output_out, int len);

extern int net_read(int fd, void *buf, int len);
extern int net_recvfrom(int fd, void *buf, int len);
extern int net_write(int fd, void *buf, int len);
extern int net_send(int fd, void *buf, int len);
extern int net_sendto(int fd, void *buf, int len, void *to, int sock_len);

extern void open_addr(unsigned char *addr, unsigned char *netmask, void *arg);
extern void close_addr(unsigned char *addr, unsigned char *netmask, void *arg);

extern char *split_if_spec(char *str, ...);

extern int dev_netmask(void *d, void *m);

#endif
