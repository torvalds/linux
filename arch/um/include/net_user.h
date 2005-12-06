/* 
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef __UM_NET_USER_H__
#define __UM_NET_USER_H__

#define ETH_ADDR_LEN (6)
#define ETH_HEADER_ETHERTAP (16)
#define ETH_HEADER_OTHER (14)
#define ETH_MAX_PACKET (1500)

#define UML_NET_VERSION (4)

struct net_user_info {
	void (*init)(void *, void *);
	int (*open)(void *);
	void (*close)(int, void *);
	void (*remove)(void *);
	int (*set_mtu)(int mtu, void *);
	void (*add_address)(unsigned char *, unsigned char *, void *);
	void (*delete_address)(unsigned char *, unsigned char *, void *);
	int max_packet;
};

extern void ether_user_init(void *data, void *dev);
extern void dev_ip_addr(void *d, unsigned char *bin_buf);
extern void set_ether_mac(void *d, unsigned char *addr);
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

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
