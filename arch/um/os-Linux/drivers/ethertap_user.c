// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2001 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Copyright (C) 2001 Lennert Buytenhek (buytenh@gnu.org) and
 * James Leu (jleu@mindspring.net).
 * Copyright (C) 2001 by various other people who didn't put their name here.
 */

#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include "etap.h"
#include <os.h>
#include <net_user.h>
#include <um_malloc.h>

#define MAX_PACKET ETH_MAX_PACKET

static int etap_user_init(void *data, void *dev)
{
	struct ethertap_data *pri = data;

	pri->dev = dev;
	return 0;
}

struct addr_change {
	enum { ADD_ADDR, DEL_ADDR } what;
	unsigned char addr[4];
	unsigned char netmask[4];
};

static void etap_change(int op, unsigned char *addr, unsigned char *netmask,
			int fd)
{
	struct addr_change change;
	char *output;
	int n;

	change.what = op;
	memcpy(change.addr, addr, sizeof(change.addr));
	memcpy(change.netmask, netmask, sizeof(change.netmask));
	CATCH_EINTR(n = write(fd, &change, sizeof(change)));
	if (n != sizeof(change)) {
		printk(UM_KERN_ERR "etap_change - request failed, err = %d\n",
		       errno);
		return;
	}

	output = uml_kmalloc(UM_KERN_PAGE_SIZE, UM_GFP_KERNEL);
	if (output == NULL)
		printk(UM_KERN_ERR "etap_change : Failed to allocate output "
		       "buffer\n");
	read_output(fd, output, UM_KERN_PAGE_SIZE);
	if (output != NULL) {
		printk("%s", output);
		kfree(output);
	}
}

static void etap_open_addr(unsigned char *addr, unsigned char *netmask,
			   void *arg)
{
	etap_change(ADD_ADDR, addr, netmask, *((int *) arg));
}

static void etap_close_addr(unsigned char *addr, unsigned char *netmask,
			    void *arg)
{
	etap_change(DEL_ADDR, addr, netmask, *((int *) arg));
}

struct etap_pre_exec_data {
	int control_remote;
	int control_me;
	int data_me;
};

static void etap_pre_exec(void *arg)
{
	struct etap_pre_exec_data *data = arg;

	dup2(data->control_remote, 1);
	close(data->data_me);
	close(data->control_me);
}

static int etap_tramp(char *dev, char *gate, int control_me,
		      int control_remote, int data_me, int data_remote)
{
	struct etap_pre_exec_data pe_data;
	int pid, err, n;
	char version_buf[sizeof("nnnnn\0")];
	char data_fd_buf[sizeof("nnnnnn\0")];
	char gate_buf[sizeof("nnn.nnn.nnn.nnn\0")];
	char *setup_args[] = { "uml_net", version_buf, "ethertap", dev,
			       data_fd_buf, gate_buf, NULL };
	char *nosetup_args[] = { "uml_net", version_buf, "ethertap",
				 dev, data_fd_buf, NULL };
	char **args, c;

	sprintf(data_fd_buf, "%d", data_remote);
	sprintf(version_buf, "%d", UML_NET_VERSION);
	if (gate != NULL) {
		strncpy(gate_buf, gate, 15);
		args = setup_args;
	}
	else args = nosetup_args;

	err = 0;
	pe_data.control_remote = control_remote;
	pe_data.control_me = control_me;
	pe_data.data_me = data_me;
	pid = run_helper(etap_pre_exec, &pe_data, args);

	if (pid < 0)
		err = pid;
	close(data_remote);
	close(control_remote);
	CATCH_EINTR(n = read(control_me, &c, sizeof(c)));
	if (n != sizeof(c)) {
		err = -errno;
		printk(UM_KERN_ERR "etap_tramp : read of status failed, "
		       "err = %d\n", -err);
		return err;
	}
	if (c != 1) {
		printk(UM_KERN_ERR "etap_tramp : uml_net failed\n");
		err = helper_wait(pid);
	}
	return err;
}

static int etap_open(void *data)
{
	struct ethertap_data *pri = data;
	char *output;
	int data_fds[2], control_fds[2], err, output_len;

	err = tap_open_common(pri->dev, pri->gate_addr);
	if (err)
		return err;

	err = socketpair(AF_UNIX, SOCK_DGRAM, 0, data_fds);
	if (err) {
		err = -errno;
		printk(UM_KERN_ERR "etap_open - data socketpair failed - "
		       "err = %d\n", errno);
		return err;
	}

	err = socketpair(AF_UNIX, SOCK_STREAM, 0, control_fds);
	if (err) {
		err = -errno;
		printk(UM_KERN_ERR "etap_open - control socketpair failed - "
		       "err = %d\n", errno);
		goto out_close_data;
	}

	err = etap_tramp(pri->dev_name, pri->gate_addr, control_fds[0],
			 control_fds[1], data_fds[0], data_fds[1]);
	output_len = UM_KERN_PAGE_SIZE;
	output = uml_kmalloc(output_len, UM_GFP_KERNEL);
	read_output(control_fds[0], output, output_len);

	if (output == NULL)
		printk(UM_KERN_ERR "etap_open : failed to allocate output "
		       "buffer\n");
	else {
		printk("%s", output);
		kfree(output);
	}

	if (err < 0) {
		printk(UM_KERN_ERR "etap_tramp failed - err = %d\n", -err);
		goto out_close_control;
	}

	pri->data_fd = data_fds[0];
	pri->control_fd = control_fds[0];
	iter_addresses(pri->dev, etap_open_addr, &pri->control_fd);
	return data_fds[0];

out_close_control:
	close(control_fds[0]);
	close(control_fds[1]);
out_close_data:
	close(data_fds[0]);
	close(data_fds[1]);
	return err;
}

static void etap_close(int fd, void *data)
{
	struct ethertap_data *pri = data;

	iter_addresses(pri->dev, etap_close_addr, &pri->control_fd);
	close(fd);

	if (shutdown(pri->data_fd, SHUT_RDWR) < 0)
		printk(UM_KERN_ERR "etap_close - shutdown data socket failed, "
		       "errno = %d\n", errno);

	if (shutdown(pri->control_fd, SHUT_RDWR) < 0)
		printk(UM_KERN_ERR "etap_close - shutdown control socket "
		       "failed, errno = %d\n", errno);

	close(pri->data_fd);
	pri->data_fd = -1;
	close(pri->control_fd);
	pri->control_fd = -1;
}

static void etap_add_addr(unsigned char *addr, unsigned char *netmask,
			  void *data)
{
	struct ethertap_data *pri = data;

	tap_check_ips(pri->gate_addr, addr);
	if (pri->control_fd == -1)
		return;
	etap_open_addr(addr, netmask, &pri->control_fd);
}

static void etap_del_addr(unsigned char *addr, unsigned char *netmask,
			  void *data)
{
	struct ethertap_data *pri = data;

	if (pri->control_fd == -1)
		return;

	etap_close_addr(addr, netmask, &pri->control_fd);
}

const struct net_user_info ethertap_user_info = {
	.init		= etap_user_init,
	.open		= etap_open,
	.close	 	= etap_close,
	.remove	 	= NULL,
	.add_address	= etap_add_addr,
	.delete_address = etap_del_addr,
	.mtu		= ETH_MAX_PACKET,
	.max_packet	= ETH_MAX_PACKET + ETH_HEADER_ETHERTAP,
};
