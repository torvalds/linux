#include <linux/etherdevice.h>
#include <linux/if_macvlan.h>
#include <linux/interrupt.h>
#include <linux/nsproxy.h>
#include <linux/compat.h>
#include <linux/if_tun.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/cache.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/wait.h>
#include <linux/cdev.h>
#include <linux/fs.h>

#include <net/net_namespace.h>
#include <net/rtnetlink.h>
#include <net/sock.h>

/*
 * A macvtap queue is the central object of this driver, it connects
 * an open character device to a macvlan interface. There can be
 * multiple queues on one interface, which map back to queues
 * implemented in hardware on the underlying device.
 *
 * macvtap_proto is used to allocate queues through the sock allocation
 * mechanism.
 *
 * TODO: multiqueue support is currently not implemented, even though
 * macvtap is basically prepared for that. We will need to add this
 * here as well as in virtio-net and qemu to get line rate on 10gbit
 * adapters from a guest.
 */
struct macvtap_queue {
	struct sock sk;
	struct socket sock;
	struct macvlan_dev *vlan;
	struct file *file;
};

static struct proto macvtap_proto = {
	.name = "macvtap",
	.owner = THIS_MODULE,
	.obj_size = sizeof (struct macvtap_queue),
};

/*
 * Minor number matches netdev->ifindex, so need a potentially
 * large value. This also makes it possible to split the
 * tap functionality out again in the future by offering it
 * from other drivers besides macvtap. As long as every device
 * only has one tap, the interface numbers assure that the
 * device nodes are unique.
 */
static unsigned int macvtap_major;
#define MACVTAP_NUM_DEVS 65536
static struct class *macvtap_class;
static struct cdev macvtap_cdev;

static const struct proto_ops macvtap_socket_ops;

/*
 * RCU usage:
 * The macvtap_queue and the macvlan_dev are loosely coupled, the
 * pointers from one to the other can only be read while rcu_read_lock
 * or macvtap_lock is held.
 *
 * Both the file and the macvlan_dev hold a reference on the macvtap_queue
 * through sock_hold(&q->sk). When the macvlan_dev goes away first,
 * q->vlan becomes inaccessible. When the files gets closed,
 * macvtap_get_queue() fails.
 *
 * There may still be references to the struct sock inside of the
 * queue from outbound SKBs, but these never reference back to the
 * file or the dev. The data structure is freed through __sk_free
 * when both our references and any pending SKBs are gone.
 */
static DEFINE_SPINLOCK(macvtap_lock);

/*
 * Choose the next free queue, for now there is only one
 */
static int macvtap_set_queue(struct net_device *dev, struct file *file,
				struct macvtap_queue *q)
{
	struct macvlan_dev *vlan = netdev_priv(dev);
	int err = -EBUSY;

	spin_lock(&macvtap_lock);
	if (rcu_dereference(vlan->tap))
		goto out;

	err = 0;
	rcu_assign_pointer(q->vlan, vlan);
	rcu_assign_pointer(vlan->tap, q);
	sock_hold(&q->sk);

	q->file = file;
	file->private_data = q;

out:
	spin_unlock(&macvtap_lock);
	return err;
}

/*
 * The file owning the queue got closed, give up both
 * the reference that the files holds as well as the
 * one from the macvlan_dev if that still exists.
 *
 * Using the spinlock makes sure that we don't get
 * to the queue again after destroying it.
 */
static void macvtap_put_queue(struct macvtap_queue *q)
{
	struct macvlan_dev *vlan;

	spin_lock(&macvtap_lock);
	vlan = rcu_dereference(q->vlan);
	if (vlan) {
		rcu_assign_pointer(vlan->tap, NULL);
		rcu_assign_pointer(q->vlan, NULL);
		sock_put(&q->sk);
	}

	spin_unlock(&macvtap_lock);

	synchronize_rcu();
	sock_put(&q->sk);
}

/*
 * Since we only support one queue, just dereference the pointer.
 */
static struct macvtap_queue *macvtap_get_queue(struct net_device *dev,
					       struct sk_buff *skb)
{
	struct macvlan_dev *vlan = netdev_priv(dev);

	return rcu_dereference(vlan->tap);
}

/*
 * The net_device is going away, give up the reference
 * that it holds on the queue (all the queues one day)
 * and safely set the pointer from the queues to NULL.
 */
static void macvtap_del_queues(struct net_device *dev)
{
	struct macvlan_dev *vlan = netdev_priv(dev);
	struct macvtap_queue *q;

	spin_lock(&macvtap_lock);
	q = rcu_dereference(vlan->tap);
	if (!q) {
		spin_unlock(&macvtap_lock);
		return;
	}

	rcu_assign_pointer(vlan->tap, NULL);
	rcu_assign_pointer(q->vlan, NULL);
	spin_unlock(&macvtap_lock);

	synchronize_rcu();
	sock_put(&q->sk);
}

/*
 * Forward happens for data that gets sent from one macvlan
 * endpoint to another one in bridge mode. We just take
 * the skb and put it into the receive queue.
 */
static int macvtap_forward(struct net_device *dev, struct sk_buff *skb)
{
	struct macvtap_queue *q = macvtap_get_queue(dev, skb);
	if (!q)
		return -ENOLINK;

	skb_queue_tail(&q->sk.sk_receive_queue, skb);
	wake_up_interruptible_poll(q->sk.sk_sleep, POLLIN | POLLRDNORM | POLLRDBAND);
	return 0;
}

/*
 * Receive is for data from the external interface (lowerdev),
 * in case of macvtap, we can treat that the same way as
 * forward, which macvlan cannot.
 */
static int macvtap_receive(struct sk_buff *skb)
{
	skb_push(skb, ETH_HLEN);
	return macvtap_forward(skb->dev, skb);
}

static int macvtap_newlink(struct net *src_net,
			   struct net_device *dev,
			   struct nlattr *tb[],
			   struct nlattr *data[])
{
	struct device *classdev;
	dev_t devt;
	int err;

	err = macvlan_common_newlink(src_net, dev, tb, data,
				     macvtap_receive, macvtap_forward);
	if (err)
		goto out;

	devt = MKDEV(MAJOR(macvtap_major), dev->ifindex);

	classdev = device_create(macvtap_class, &dev->dev, devt,
				 dev, "tap%d", dev->ifindex);
	if (IS_ERR(classdev)) {
		err = PTR_ERR(classdev);
		macvtap_del_queues(dev);
	}

out:
	return err;
}

static void macvtap_dellink(struct net_device *dev,
			    struct list_head *head)
{
	device_destroy(macvtap_class,
		       MKDEV(MAJOR(macvtap_major), dev->ifindex));

	macvtap_del_queues(dev);
	macvlan_dellink(dev, head);
}

static struct rtnl_link_ops macvtap_link_ops __read_mostly = {
	.kind		= "macvtap",
	.newlink	= macvtap_newlink,
	.dellink	= macvtap_dellink,
};


static void macvtap_sock_write_space(struct sock *sk)
{
	if (!sock_writeable(sk) ||
	    !test_and_clear_bit(SOCK_ASYNC_NOSPACE, &sk->sk_socket->flags))
		return;

	if (sk->sk_sleep && waitqueue_active(sk->sk_sleep))
		wake_up_interruptible_poll(sk->sk_sleep, POLLOUT | POLLWRNORM | POLLWRBAND);
}

static int macvtap_open(struct inode *inode, struct file *file)
{
	struct net *net = current->nsproxy->net_ns;
	struct net_device *dev = dev_get_by_index(net, iminor(inode));
	struct macvtap_queue *q;
	int err;

	err = -ENODEV;
	if (!dev)
		goto out;

	/* check if this is a macvtap device */
	err = -EINVAL;
	if (dev->rtnl_link_ops != &macvtap_link_ops)
		goto out;

	err = -ENOMEM;
	q = (struct macvtap_queue *)sk_alloc(net, AF_UNSPEC, GFP_KERNEL,
					     &macvtap_proto);
	if (!q)
		goto out;

	init_waitqueue_head(&q->sock.wait);
	q->sock.type = SOCK_RAW;
	q->sock.state = SS_CONNECTED;
	q->sock.file = file;
	q->sock.ops = &macvtap_socket_ops;
	sock_init_data(&q->sock, &q->sk);
	q->sk.sk_write_space = macvtap_sock_write_space;

	err = macvtap_set_queue(dev, file, q);
	if (err)
		sock_put(&q->sk);

out:
	if (dev)
		dev_put(dev);

	return err;
}

static int macvtap_release(struct inode *inode, struct file *file)
{
	struct macvtap_queue *q = file->private_data;
	macvtap_put_queue(q);
	return 0;
}

static unsigned int macvtap_poll(struct file *file, poll_table * wait)
{
	struct macvtap_queue *q = file->private_data;
	unsigned int mask = POLLERR;

	if (!q)
		goto out;

	mask = 0;
	poll_wait(file, &q->sock.wait, wait);

	if (!skb_queue_empty(&q->sk.sk_receive_queue))
		mask |= POLLIN | POLLRDNORM;

	if (sock_writeable(&q->sk) ||
	    (!test_and_set_bit(SOCK_ASYNC_NOSPACE, &q->sock.flags) &&
	     sock_writeable(&q->sk)))
		mask |= POLLOUT | POLLWRNORM;

out:
	return mask;
}

/* Get packet from user space buffer */
static ssize_t macvtap_get_user(struct macvtap_queue *q,
				const struct iovec *iv, size_t count,
				int noblock)
{
	struct sk_buff *skb;
	struct macvlan_dev *vlan;
	size_t len = count;
	int err;

	if (unlikely(len < ETH_HLEN))
		return -EINVAL;

	skb = sock_alloc_send_skb(&q->sk, NET_IP_ALIGN + len, noblock, &err);
	if (!skb)
		goto err;

	skb_reserve(skb, NET_IP_ALIGN);
	skb_put(skb, count);

	err = skb_copy_datagram_from_iovec(skb, 0, iv, 0, len);
	if (err)
		goto err;

	skb_set_network_header(skb, ETH_HLEN);
	rcu_read_lock_bh();
	vlan = rcu_dereference(q->vlan);
	if (vlan)
		macvlan_start_xmit(skb, vlan->dev);
	else
		kfree_skb(skb);
	rcu_read_unlock_bh();

	return count;

err:
	rcu_read_lock_bh();
	vlan = rcu_dereference(q->vlan);
	if (vlan)
		macvlan_count_rx(q->vlan, 0, false, false);
	rcu_read_unlock_bh();

	kfree_skb(skb);

	return err;
}

static ssize_t macvtap_aio_write(struct kiocb *iocb, const struct iovec *iv,
				 unsigned long count, loff_t pos)
{
	struct file *file = iocb->ki_filp;
	ssize_t result = -ENOLINK;
	struct macvtap_queue *q = file->private_data;

	result = macvtap_get_user(q, iv, iov_length(iv, count),
			      file->f_flags & O_NONBLOCK);
	return result;
}

/* Put packet to the user space buffer */
static ssize_t macvtap_put_user(struct macvtap_queue *q,
				const struct sk_buff *skb,
				const struct iovec *iv, int len)
{
	struct macvlan_dev *vlan;
	int ret;

	len = min_t(int, skb->len, len);

	ret = skb_copy_datagram_const_iovec(skb, 0, iv, 0, len);

	rcu_read_lock_bh();
	vlan = rcu_dereference(q->vlan);
	if (vlan)
		macvlan_count_rx(vlan, len, ret == 0, 0);
	rcu_read_unlock_bh();

	return ret ? ret : len;
}

static ssize_t macvtap_do_read(struct macvtap_queue *q, struct kiocb *iocb,
			       const struct iovec *iv, unsigned long len,
			       int noblock)
{
	DECLARE_WAITQUEUE(wait, current);
	struct sk_buff *skb;
	ssize_t ret = 0;

	add_wait_queue(q->sk.sk_sleep, &wait);
	while (len) {
		current->state = TASK_INTERRUPTIBLE;

		/* Read frames from the queue */
		skb = skb_dequeue(&q->sk.sk_receive_queue);
		if (!skb) {
			if (noblock) {
				ret = -EAGAIN;
				break;
			}
			if (signal_pending(current)) {
				ret = -ERESTARTSYS;
				break;
			}
			/* Nothing to read, let's sleep */
			schedule();
			continue;
		}
		ret = macvtap_put_user(q, skb, iv, len);
		kfree_skb(skb);
		break;
	}

	current->state = TASK_RUNNING;
	remove_wait_queue(q->sk.sk_sleep, &wait);
	return ret;
}

static ssize_t macvtap_aio_read(struct kiocb *iocb, const struct iovec *iv,
				unsigned long count, loff_t pos)
{
	struct file *file = iocb->ki_filp;
	struct macvtap_queue *q = file->private_data;
	ssize_t len, ret = 0;

	len = iov_length(iv, count);
	if (len < 0) {
		ret = -EINVAL;
		goto out;
	}

	ret = macvtap_do_read(q, iocb, iv, len, file->f_flags & O_NONBLOCK);
	ret = min_t(ssize_t, ret, len); /* XXX copied from tun.c. Why? */
out:
	return ret;
}

/*
 * provide compatibility with generic tun/tap interface
 */
static long macvtap_ioctl(struct file *file, unsigned int cmd,
			  unsigned long arg)
{
	struct macvtap_queue *q = file->private_data;
	struct macvlan_dev *vlan;
	void __user *argp = (void __user *)arg;
	struct ifreq __user *ifr = argp;
	unsigned int __user *up = argp;
	unsigned int u;
	int ret;

	switch (cmd) {
	case TUNSETIFF:
		/* ignore the name, just look at flags */
		if (get_user(u, &ifr->ifr_flags))
			return -EFAULT;
		if (u != (IFF_TAP | IFF_NO_PI))
			return -EINVAL;
		return 0;

	case TUNGETIFF:
		rcu_read_lock_bh();
		vlan = rcu_dereference(q->vlan);
		if (vlan)
			dev_hold(vlan->dev);
		rcu_read_unlock_bh();

		if (!vlan)
			return -ENOLINK;

		ret = 0;
		if (copy_to_user(&ifr->ifr_name, q->vlan->dev->name, IFNAMSIZ) ||
		    put_user((TUN_TAP_DEV | TUN_NO_PI), &ifr->ifr_flags))
			ret = -EFAULT;
		dev_put(vlan->dev);
		return ret;

	case TUNGETFEATURES:
		if (put_user((IFF_TAP | IFF_NO_PI), up))
			return -EFAULT;
		return 0;

	case TUNSETSNDBUF:
		if (get_user(u, up))
			return -EFAULT;

		q->sk.sk_sndbuf = u;
		return 0;

	case TUNSETOFFLOAD:
		/* let the user check for future flags */
		if (arg & ~(TUN_F_CSUM | TUN_F_TSO4 | TUN_F_TSO6 |
			  TUN_F_TSO_ECN | TUN_F_UFO))
			return -EINVAL;

		/* TODO: add support for these, so far we don't
			 support any offload */
		if (arg & (TUN_F_CSUM | TUN_F_TSO4 | TUN_F_TSO6 |
			 TUN_F_TSO_ECN | TUN_F_UFO))
			return -EINVAL;

		return 0;

	default:
		return -EINVAL;
	}
}

#ifdef CONFIG_COMPAT
static long macvtap_compat_ioctl(struct file *file, unsigned int cmd,
				 unsigned long arg)
{
	return macvtap_ioctl(file, cmd, (unsigned long)compat_ptr(arg));
}
#endif

static const struct file_operations macvtap_fops = {
	.owner		= THIS_MODULE,
	.open		= macvtap_open,
	.release	= macvtap_release,
	.aio_read	= macvtap_aio_read,
	.aio_write	= macvtap_aio_write,
	.poll		= macvtap_poll,
	.llseek		= no_llseek,
	.unlocked_ioctl	= macvtap_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= macvtap_compat_ioctl,
#endif
};

static int macvtap_sendmsg(struct kiocb *iocb, struct socket *sock,
			   struct msghdr *m, size_t total_len)
{
	struct macvtap_queue *q = container_of(sock, struct macvtap_queue, sock);
	return macvtap_get_user(q, m->msg_iov, total_len,
			    m->msg_flags & MSG_DONTWAIT);
}

static int macvtap_recvmsg(struct kiocb *iocb, struct socket *sock,
			   struct msghdr *m, size_t total_len,
			   int flags)
{
	struct macvtap_queue *q = container_of(sock, struct macvtap_queue, sock);
	int ret;
	if (flags & ~(MSG_DONTWAIT|MSG_TRUNC))
		return -EINVAL;
	ret = macvtap_do_read(q, iocb, m->msg_iov, total_len,
			  flags & MSG_DONTWAIT);
	if (ret > total_len) {
		m->msg_flags |= MSG_TRUNC;
		ret = flags & MSG_TRUNC ? ret : total_len;
	}
	return ret;
}

/* Ops structure to mimic raw sockets with tun */
static const struct proto_ops macvtap_socket_ops = {
	.sendmsg = macvtap_sendmsg,
	.recvmsg = macvtap_recvmsg,
};

/* Get an underlying socket object from tun file.  Returns error unless file is
 * attached to a device.  The returned object works like a packet socket, it
 * can be used for sock_sendmsg/sock_recvmsg.  The caller is responsible for
 * holding a reference to the file for as long as the socket is in use. */
struct socket *macvtap_get_socket(struct file *file)
{
	struct macvtap_queue *q;
	if (file->f_op != &macvtap_fops)
		return ERR_PTR(-EINVAL);
	q = file->private_data;
	if (!q)
		return ERR_PTR(-EBADFD);
	return &q->sock;
}
EXPORT_SYMBOL_GPL(macvtap_get_socket);

static int macvtap_init(void)
{
	int err;

	err = alloc_chrdev_region(&macvtap_major, 0,
				MACVTAP_NUM_DEVS, "macvtap");
	if (err)
		goto out1;

	cdev_init(&macvtap_cdev, &macvtap_fops);
	err = cdev_add(&macvtap_cdev, macvtap_major, MACVTAP_NUM_DEVS);
	if (err)
		goto out2;

	macvtap_class = class_create(THIS_MODULE, "macvtap");
	if (IS_ERR(macvtap_class)) {
		err = PTR_ERR(macvtap_class);
		goto out3;
	}

	err = macvlan_link_register(&macvtap_link_ops);
	if (err)
		goto out4;

	return 0;

out4:
	class_unregister(macvtap_class);
out3:
	cdev_del(&macvtap_cdev);
out2:
	unregister_chrdev_region(macvtap_major, MACVTAP_NUM_DEVS);
out1:
	return err;
}
module_init(macvtap_init);

static void macvtap_exit(void)
{
	rtnl_link_unregister(&macvtap_link_ops);
	class_unregister(macvtap_class);
	cdev_del(&macvtap_cdev);
	unregister_chrdev_region(macvtap_major, MACVTAP_NUM_DEVS);
}
module_exit(macvtap_exit);

MODULE_ALIAS_RTNL_LINK("macvtap");
MODULE_AUTHOR("Arnd Bergmann <arnd@arndb.de>");
MODULE_LICENSE("GPL");
