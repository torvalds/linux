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
int pvcalls_front_accept(struct socket *sock,
			 struct socket *newsock,
			 struct proto_accept_arg *arg);
int pvcalls_front_sendmsg(struct socket *sock,
			  struct msghdr *msg,
			  size_t len);
int pvcalls_front_recvmsg(struct socket *sock,
			  struct msghdr *msg,
			  size_t len,
			  int flags);
__poll_t pvcalls_front_poll(struct file *file,
				struct socket *sock,
				poll_table *wait);
int pvcalls_front_release(struct socket *sock);

#endif
