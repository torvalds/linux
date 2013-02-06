#include <linux/module.h>
#include <linux/init.h>

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/netlink.h>
#include <linux/kthread.h>
#include <net/sock.h>

#include <linux/list.h>

#include "connection.h"
#include "common.h"

#define MC_DAEMON_NETLINK  17

struct mcKernelApiCtx
{
	struct sock *sk;
	struct list_head peers;
	atomic_t counter;
};

struct mcKernelApiCtx* modCtx = NULL;

/*----------------------------------------------------------------------------*/
/* get a unique ID */
unsigned int mcapi_unique_id(
	void
)
{
	return (unsigned int)atomic_inc_return(
		&(modCtx->counter));
}


//------------------------------------------------------------------------------
static connection_t* mcapi_find_connection(
	uint32_t seq
)
{
	connection_t *tmp;
	struct list_head *pos;

	// Get session_t for sessionId
	list_for_each(pos, &modCtx->peers) {
		tmp=list_entry(pos, connection_t, list);
		if (tmp->sequenceMagic == seq) {
			return tmp;
		}
	}

	return NULL;
}

//------------------------------------------------------------------------------
void mcapi_insert_connection(
	connection_t *connection
)
{
	list_add_tail(&(connection->list), &(modCtx->peers));
	connection->socketDescriptor = modCtx->sk;
}

void mcapi_remove_connection(
	uint32_t seq
)
{
	connection_t *tmp;
	struct list_head *pos, *q;

	// Delete all session objects. Usually this should not be needed as
	// closeDevice() requires that all sessions have been closed before.
	list_for_each_safe(pos, q, &modCtx->peers) {
		tmp=list_entry(pos, connection_t, list);
		if (tmp->sequenceMagic == seq) {
			list_del(pos);
			break;
		}
	}
}

//------------------------------------------------------------------------------
static int mcapi_process(
	struct sk_buff *skb,
	struct nlmsghdr *nlh
)
{
	connection_t *c;
	int	length;
	int		seq;
	pid_t	pid;

	pid = nlh->nlmsg_pid;
	length = nlh->nlmsg_len;
	seq = nlh->nlmsg_seq;
	MCDRV_DBG_VERBOSE("nlmsg len %d type %d pid 0x%X seq %d\n",
		   length, nlh->nlmsg_type, pid, seq );

	c = mcapi_find_connection(seq);
	if(!c){
		MCDRV_ERROR("Invalid incomming connection - seq=%u!", seq);
		return -1;
	}

	// Pass the buffer to the appropriate connection
	connection_process(c, skb);

	return 0;
}

//------------------------------------------------------------------------------
static void mcapi_callback(
	struct sk_buff *skb
)
{
	struct nlmsghdr *nlh = nlmsg_hdr(skb);
	int len = skb->len;
	int err = 0;

	while (NLMSG_OK(nlh, len)) {
		err = mcapi_process(skb, nlh);

		/* if err or if this message says it wants a response */
		if (err || (nlh->nlmsg_flags & NLM_F_ACK))
			netlink_ack(skb, nlh, err);

		nlh = NLMSG_NEXT(nlh, len);
	}
}

//------------------------------------------------------------------------------
static int __init mcapi_init(void)
{
	printk(KERN_INFO "Mobicore API module initialized!\n");

	modCtx = kzalloc(sizeof(struct mcKernelApiCtx), GFP_KERNEL);

	/* start kernel thread */
	modCtx->sk = netlink_kernel_create(&init_net, MC_DAEMON_NETLINK, 0,
					mcapi_callback, NULL, THIS_MODULE);

	if (!modCtx->sk) {
		MCDRV_ERROR("register of recieve handler failed");
		kfree(modCtx);
		return -EFAULT;
	}

	INIT_LIST_HEAD(&modCtx->peers);
	return 0;
}

static void __exit mcapi_exit(void)
{
	printk(KERN_INFO "Unloading Mobicore API module.\n");

	if (modCtx->sk != NULL)
	{
		netlink_kernel_release(modCtx->sk);
		modCtx->sk = NULL;
	}
	kfree(modCtx);
	modCtx = NULL;
}

module_init(mcapi_init);
module_exit(mcapi_exit);

MODULE_AUTHOR("Giesecke & Devrient GmbH");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("MobiCore API driver");
