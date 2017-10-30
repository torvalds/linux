#ifndef __PVCALLS_FRONT_H__
#define __PVCALLS_FRONT_H__

#include <linux/net.h>

int pvcalls_front_socket(struct socket *sock);
int pvcalls_front_connect(struct socket *sock, struct sockaddr *addr,
			  int addr_len, int flags);
int pvcalls_front_bind(struct socket *sock,
		       struct sockaddr *addr,
		       int addr_len);
int pvcalls_front_listen(struct socket *sock, int backlog);

#endif
