/* 
 * Copyright (C) 2001, 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/uio.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <linux/if_tun.h>
#include "net_user.h"
#include "tuntap.h"
#include "kern_util.h"
#include "user_util.h"
#include "user.h"
#include "os.h"

#define MAX_PACKET ETH_MAX_PACKET

void tuntap_user_init(void *data, void *dev)
{
	struct tuntap_data *pri = data;

	pri->dev = dev;
}

static void tuntap_add_addr(unsigned char *addr, unsigned char *netmask,
			    void *data)
{
	struct tuntap_data *pri = data;

	tap_check_ips(pri->gate_addr, addr);
	if((pri->fd == -1) || pri->fixed_config) return;
	open_addr(addr, netmask, pri->dev_name);
}

static void tuntap_del_addr(unsigned char *addr, unsigned char *netmask,
			    void *data)
{
	struct tuntap_data *pri = data;

	if((pri->fd == -1) || pri->fixed_config) return;
	close_addr(addr, netmask, pri->dev_name);
}

struct tuntap_pre_exec_data {
	int stdout;
	int close_me;
};

static void tuntap_pre_exec(void *arg)
{
	struct tuntap_pre_exec_data *data = arg;
	
	dup2(data->stdout, 1);
	os_close_file(data->close_me);
}

static int tuntap_open_tramp(char *gate, int *fd_out, int me, int remote,
			     char *buffer, int buffer_len, int *used_out)
{
	struct tuntap_pre_exec_data data;
	char version_buf[sizeof("nnnnn\0")];
	char *argv[] = { "uml_net", version_buf, "tuntap", "up", gate,
			 NULL };
	char buf[CMSG_SPACE(sizeof(*fd_out))];
	struct msghdr msg;
	struct cmsghdr *cmsg;
	struct iovec iov;
	int pid, n, err;

	sprintf(version_buf, "%d", UML_NET_VERSION);

	data.stdout = remote;
	data.close_me = me;

	pid = run_helper(tuntap_pre_exec, &data, argv, NULL);

	if(pid < 0) return(-pid);

	os_close_file(remote);

	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	if(buffer != NULL){
		iov = ((struct iovec) { buffer, buffer_len });
		msg.msg_iov = &iov;
		msg.msg_iovlen = 1;
	}
	else {
		msg.msg_iov = NULL;
		msg.msg_iovlen = 0;
	}
	msg.msg_control = buf;
	msg.msg_controllen = sizeof(buf);
	msg.msg_flags = 0;
	n = recvmsg(me, &msg, 0);
	*used_out = n;
	if(n < 0){
		err = -errno;
		printk("tuntap_open_tramp : recvmsg failed - errno = %d\n", 
		       errno);
		return err;
	}
	CATCH_EINTR(waitpid(pid, NULL, 0));

	cmsg = CMSG_FIRSTHDR(&msg);
	if(cmsg == NULL){
		printk("tuntap_open_tramp : didn't receive a message\n");
		return(-EINVAL);
	}
	if((cmsg->cmsg_level != SOL_SOCKET) || 
	   (cmsg->cmsg_type != SCM_RIGHTS)){
		printk("tuntap_open_tramp : didn't receive a descriptor\n");
		return(-EINVAL);
	}
	*fd_out = ((int *) CMSG_DATA(cmsg))[0];
	os_set_exec_close(*fd_out, 1);
	return(0);
}

static int tuntap_open(void *data)
{
	struct ifreq ifr;
	struct tuntap_data *pri = data;
	char *output, *buffer;
	int err, fds[2], len, used;

	err = tap_open_common(pri->dev, pri->gate_addr);
	if(err < 0)
		return(err);

	if(pri->fixed_config){
		pri->fd = os_open_file("/dev/net/tun",
				       of_cloexec(of_rdwr(OPENFLAGS())), 0);
		if(pri->fd < 0){
			printk("Failed to open /dev/net/tun, err = %d\n",
			       -pri->fd);
			return(pri->fd);
		}
		memset(&ifr, 0, sizeof(ifr));
		ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
		strlcpy(ifr.ifr_name, pri->dev_name, sizeof(ifr.ifr_name));
		if(ioctl(pri->fd, TUNSETIFF, (void *) &ifr) < 0){
			err = -errno;
			printk("TUNSETIFF failed, errno = %d\n", errno);
			os_close_file(pri->fd);
			return err;
		}
	}
	else {
		err = os_pipe(fds, 0, 0);
		if(err < 0){
			printk("tuntap_open : os_pipe failed - err = %d\n",
			       -err);
			return(err);
		}

		buffer = get_output_buffer(&len);
		if(buffer != NULL) len--;
		used = 0;

		err = tuntap_open_tramp(pri->gate_addr, &pri->fd, fds[0],
					fds[1], buffer, len, &used);

		output = buffer;
		if(err < 0) {
			printk("%s", output);
			free_output_buffer(buffer);
			printk("tuntap_open_tramp failed - err = %d\n", -err);
			return(err);
		}

		pri->dev_name = uml_strdup(buffer);
		output += IFNAMSIZ;
		printk("%s", output);
		free_output_buffer(buffer);

		os_close_file(fds[0]);
		iter_addresses(pri->dev, open_addr, pri->dev_name);
	}

	return(pri->fd);
}

static void tuntap_close(int fd, void *data)
{
	struct tuntap_data *pri = data;

	if(!pri->fixed_config) 
		iter_addresses(pri->dev, close_addr, pri->dev_name);
	os_close_file(fd);
	pri->fd = -1;
}

static int tuntap_set_mtu(int mtu, void *data)
{
	return(mtu);
}

const struct net_user_info tuntap_user_info = {
	.init		= tuntap_user_init,
	.open		= tuntap_open,
	.close	 	= tuntap_close,
	.remove	 	= NULL,
	.set_mtu	= tuntap_set_mtu,
	.add_address	= tuntap_add_addr,
	.delete_address = tuntap_del_addr,
	.max_packet	= MAX_PACKET
};

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
