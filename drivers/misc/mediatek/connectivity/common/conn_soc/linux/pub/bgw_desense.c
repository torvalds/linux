/*
* Copyright (C) 2011-2014 MediaTek Inc.
*
* This program is free software: you can redistribute it and/or modify it under the terms of the
* GNU General Public License version 2 as published by the Free Software Foundation.
*
* This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
* without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
* See the GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License along with this program.
* If not, see <http://www.gnu.org/licenses/>.
*/

#include <linux/version.h>
#include <linux/netlink.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include <linux/socket.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/genetlink.h>
#include "bgw_desense.h"

static struct sock *g_nl_sk;
/* static struct sockaddr_nl src_addr, des_addr; */
/* static struct iovec iov; */
static int pid;
/* static struct msghdr msg; */

void bgw_destroy_netlink_kernel(void)
{
	if (g_nl_sk != NULL) {
		/* sock_release(g_nl_sk->sk_socket); */
		netlink_kernel_release(g_nl_sk);
		MSG("release socket\n");
		return;
	}
	ERR("no socket yet\n");
}

void send_command_to_daemon(const int command /*struct sk_buff *skb */)
{
/*
	struct iphdr *iph;
	struct ethhdr *ehdr;
	*/
	struct nlmsghdr *nlh;
	struct sk_buff *nl_skb;
	int res;

	MSG("here we will send command to native daemon\n");
/*	if(skb == NULL)
	{
		ERR("invalid sk_buff\n");
		return;
	}
*/
	if (!g_nl_sk) {
		ERR("invalid socket\n");
		return;
	}
	if (pid == 0) {
		ERR("invalid native process pid\n");
		return;
	}
	/*alloc data buffer for sending to native */
	/*malloc data space at least 1500 bytes, which is ethernet data length */
	nl_skb = alloc_skb(NLMSG_SPACE(MAX_NL_MSG_LEN), GFP_ATOMIC);
	if (nl_skb == NULL) {
		ERR("malloc skb error\n");
		return;
	}
	MSG("malloc data space done\n");
	/*
	   ehdr = eth_hdr(skb);
	   iph = ip_hdr(skb);
	 */

/* nlh = NLMSG_PUT(nl_skb, 0, 0, 0, NLMSG_SPACE(1500)-sizeof(struct nlmsghdr)); */
	nlh = nlmsg_put(nl_skb, 0, 0, 0, MAX_NL_MSG_LEN, 0);
	if (nlh == NULL) {
		MSG("nlh is NULL\n");
		kfree_skb(nl_skb);
		return;
	}
	NETLINK_CB(nl_skb).portid = 0;

/* memcpy(NLMSG_DATA(nlh), ACK, 5); */
	*(char *)NLMSG_DATA(nlh) = command;
	res = netlink_unicast(g_nl_sk, nl_skb, pid, MSG_DONTWAIT);
	if (res == 0) {
		MSG("send to user space process error\n");
		return;
	}
	ERR("send to user space process done, data length = %d\n", res);
}

static void nl_data_handler(struct sk_buff *__skb)
{
	struct sk_buff *skb;
	struct nlmsghdr *nlh;
	int i;
	int len;
	char str[128];

	MSG("we got netlink message\n");
	len = NLMSG_SPACE(MAX_NL_MSG_LEN);
	skb = skb_get(__skb);
	if (skb == NULL)
		ERR("skb_get return NULL");
	if (skb->len >= NLMSG_SPACE(0)) {	/*presume there is 5byte payload at leaset */
		MSG("length is enough\n");
		nlh = nlmsg_hdr(skb);	/* point to data which include in skb */
		memcpy(str, NLMSG_DATA(nlh), sizeof(str));
		for (i = 0; i < 3; i++)
			MSG("str[%d = %c]", i, str[i]);
		MSG("str[0] = %d, str[1] = %d, str[2] = %d\n", str[0], str[1], str[2]);
		if (str[0] == 'B' && str[1] == 'G' && str[2] == 'W') {
			MSG("got native daemon init command, record it's pid\n");
			pid = nlh->nlmsg_pid;	/*record the native process PID */
			MSG("native daemon pid is %d\n", pid);
		} else {
			ERR("this is not BGW message, ignore it\n");
			return;
		}
	} else {
		ERR("not engouth data length\n");
		return;
	}

	kfree_skb(skb);

	send_command_to_daemon(ACK);
}

int bgw_init_socket(void)
{
	struct netlink_kernel_cfg cfg;

	memset(&cfg, 0, sizeof(cfg));
	cfg.input = nl_data_handler;

	g_nl_sk = __netlink_kernel_create(&init_net, NETLINK_TEST, THIS_MODULE, &cfg);

	if (g_nl_sk == NULL) {
		ERR("netlink_kernel_create error\n");
		return -1;
	}
	MSG("netlink_kernel_create ok\n");
	return 0;
}
