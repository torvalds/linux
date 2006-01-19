/*
 * Copyright (C) 2001 Lennert Buytenhek (buytenh@gnu.org) and 
 * James Leu (jleu@mindspring.net).
 * Copyright (C) 2001 by various other people who didn't put their name here.
 * Licensed under the GPL.
 */

#include "linux/config.h"
#include "linux/kernel.h"
#include "linux/netdevice.h"
#include "linux/rtnetlink.h"
#include "linux/skbuff.h"
#include "linux/socket.h"
#include "linux/spinlock.h"
#include "linux/module.h"
#include "linux/init.h"
#include "linux/etherdevice.h"
#include "linux/list.h"
#include "linux/inetdevice.h"
#include "linux/ctype.h"
#include "linux/bootmem.h"
#include "linux/ethtool.h"
#include "linux/platform_device.h"
#include "asm/uaccess.h"
#include "user_util.h"
#include "kern_util.h"
#include "net_kern.h"
#include "net_user.h"
#include "mconsole_kern.h"
#include "init.h"
#include "irq_user.h"
#include "irq_kern.h"

#define DRIVER_NAME "uml-netdev"

static DEFINE_SPINLOCK(opened_lock);
static LIST_HEAD(opened);

static int uml_net_rx(struct net_device *dev)
{
	struct uml_net_private *lp = dev->priv;
	int pkt_len;
	struct sk_buff *skb;

	/* If we can't allocate memory, try again next round. */
	skb = dev_alloc_skb(dev->mtu);
	if (skb == NULL) {
		lp->stats.rx_dropped++;
		return 0;
	}

	skb->dev = dev;
	skb_put(skb, dev->mtu);
	skb->mac.raw = skb->data;
	pkt_len = (*lp->read)(lp->fd, &skb, lp);

	if (pkt_len > 0) {
		skb_trim(skb, pkt_len);
		skb->protocol = (*lp->protocol)(skb);
		netif_rx(skb);

		lp->stats.rx_bytes += skb->len;
		lp->stats.rx_packets++;
		return pkt_len;
	}

	kfree_skb(skb);
	return pkt_len;
}

static void uml_dev_close(void* dev)
{
	dev_close( (struct net_device *) dev);
}

irqreturn_t uml_net_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct net_device *dev = dev_id;
	struct uml_net_private *lp = dev->priv;
	int err;

	if(!netif_running(dev))
		return(IRQ_NONE);

	spin_lock(&lp->lock);
	while((err = uml_net_rx(dev)) > 0) ;
	if(err < 0) {
		DECLARE_WORK(close_work, uml_dev_close, dev);
		printk(KERN_ERR 
		       "Device '%s' read returned %d, shutting it down\n", 
		       dev->name, err);
		/* dev_close can't be called in interrupt context, and takes
		 * again lp->lock.
		 * And dev_close() can be safely called multiple times on the
		 * same device, since it tests for (dev->flags & IFF_UP). So
		 * there's no harm in delaying the device shutdown. */
		schedule_work(&close_work);
		goto out;
	}
	reactivate_fd(lp->fd, UM_ETH_IRQ);

out:
	spin_unlock(&lp->lock);
	return(IRQ_HANDLED);
}

static int uml_net_open(struct net_device *dev)
{
	struct uml_net_private *lp = dev->priv;
	int err;

	spin_lock(&lp->lock);

	if(lp->fd >= 0){
		err = -ENXIO;
		goto out;
	}

	if(!lp->have_mac){
 		dev_ip_addr(dev, &lp->mac[2]);
 		set_ether_mac(dev, lp->mac);
	}

	lp->fd = (*lp->open)(&lp->user);
	if(lp->fd < 0){
		err = lp->fd;
		goto out;
	}

	err = um_request_irq(dev->irq, lp->fd, IRQ_READ, uml_net_interrupt,
			     SA_INTERRUPT | SA_SHIRQ, dev->name, dev);
	if(err != 0){
		printk(KERN_ERR "uml_net_open: failed to get irq(%d)\n", err);
		if(lp->close != NULL) (*lp->close)(lp->fd, &lp->user);
		lp->fd = -1;
		err = -ENETUNREACH;
	}

	lp->tl.data = (unsigned long) &lp->user;
	netif_start_queue(dev);

	/* clear buffer - it can happen that the host side of the interface
	 * is full when we get here.  In this case, new data is never queued,
	 * SIGIOs never arrive, and the net never works.
	 */
	while((err = uml_net_rx(dev)) > 0) ;

 out:
	spin_unlock(&lp->lock);
	return(err);
}

static int uml_net_close(struct net_device *dev)
{
	struct uml_net_private *lp = dev->priv;
	
	netif_stop_queue(dev);
	spin_lock(&lp->lock);

	free_irq(dev->irq, dev);
	if(lp->close != NULL)
		(*lp->close)(lp->fd, &lp->user);
	lp->fd = -1;
	list_del(&lp->list);

	spin_unlock(&lp->lock);
	return 0;
}

static int uml_net_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct uml_net_private *lp = dev->priv;
	unsigned long flags;
	int len;

	netif_stop_queue(dev);

	spin_lock_irqsave(&lp->lock, flags);

	len = (*lp->write)(lp->fd, &skb, lp);

	if(len == skb->len) {
		lp->stats.tx_packets++;
		lp->stats.tx_bytes += skb->len;
		dev->trans_start = jiffies;
		netif_start_queue(dev);

		/* this is normally done in the interrupt when tx finishes */
		netif_wake_queue(dev);
	} 
	else if(len == 0){
		netif_start_queue(dev);
		lp->stats.tx_dropped++;
	}
	else {
		netif_start_queue(dev);
		printk(KERN_ERR "uml_net_start_xmit: failed(%d)\n", len);
	}

	spin_unlock_irqrestore(&lp->lock, flags);

	dev_kfree_skb(skb);

	return 0;
}

static struct net_device_stats *uml_net_get_stats(struct net_device *dev)
{
	struct uml_net_private *lp = dev->priv;
	return &lp->stats;
}

static void uml_net_set_multicast_list(struct net_device *dev)
{
	if (dev->flags & IFF_PROMISC) return;
	else if (dev->mc_count)	dev->flags |= IFF_ALLMULTI;
	else dev->flags &= ~IFF_ALLMULTI;
}

static void uml_net_tx_timeout(struct net_device *dev)
{
	dev->trans_start = jiffies;
	netif_wake_queue(dev);
}

static int uml_net_set_mac(struct net_device *dev, void *addr)
{
	struct uml_net_private *lp = dev->priv;
	struct sockaddr *hwaddr = addr;

	spin_lock(&lp->lock);
	memcpy(dev->dev_addr, hwaddr->sa_data, ETH_ALEN);
	spin_unlock(&lp->lock);

	return(0);
}

static int uml_net_change_mtu(struct net_device *dev, int new_mtu)
{
	struct uml_net_private *lp = dev->priv;
	int err = 0;

	spin_lock(&lp->lock);

	new_mtu = (*lp->set_mtu)(new_mtu, &lp->user);
	if(new_mtu < 0){
		err = new_mtu;
		goto out;
	}

	dev->mtu = new_mtu;

 out:
	spin_unlock(&lp->lock);
	return err;
}

static void uml_net_get_drvinfo(struct net_device *dev,
				struct ethtool_drvinfo *info)
{
	strcpy(info->driver, DRIVER_NAME);
	strcpy(info->version, "42");
}

static struct ethtool_ops uml_net_ethtool_ops = {
	.get_drvinfo	= uml_net_get_drvinfo,
	.get_link	= ethtool_op_get_link,
};

void uml_net_user_timer_expire(unsigned long _conn)
{
#ifdef undef
	struct connection *conn = (struct connection *)_conn;

	dprintk(KERN_INFO "uml_net_user_timer_expire [%p]\n", conn);
	do_connect(conn);
#endif
}

static DEFINE_SPINLOCK(devices_lock);
static LIST_HEAD(devices);

static struct platform_driver uml_net_driver = {
	.driver = {
		.name  = DRIVER_NAME,
	},
};
static int driver_registered;

static int eth_configure(int n, void *init, char *mac,
			 struct transport *transport)
{
	struct uml_net *device;
	struct net_device *dev;
	struct uml_net_private *lp;
	int save, err, size;

	size = transport->private_size + sizeof(struct uml_net_private) + 
		sizeof(((struct uml_net_private *) 0)->user);

	device = kmalloc(sizeof(*device), GFP_KERNEL);
	if (device == NULL) {
		printk(KERN_ERR "eth_configure failed to allocate uml_net\n");
		return(1);
	}

	memset(device, 0, sizeof(*device));
	INIT_LIST_HEAD(&device->list);
	device->index = n;

	spin_lock(&devices_lock);
	list_add(&device->list, &devices);
	spin_unlock(&devices_lock);

	if (setup_etheraddr(mac, device->mac))
		device->have_mac = 1;

	printk(KERN_INFO "Netdevice %d ", n);
	if (device->have_mac)
		printk("(%02x:%02x:%02x:%02x:%02x:%02x) ",
		       device->mac[0], device->mac[1],
		       device->mac[2], device->mac[3],
		       device->mac[4], device->mac[5]);
	printk(": ");
	dev = alloc_etherdev(size);
	if (dev == NULL) {
		printk(KERN_ERR "eth_configure: failed to allocate device\n");
		return 1;
	}

	lp = dev->priv;
	/* This points to the transport private data. It's still clear, but we
	 * must memset it to 0 *now*. Let's help the drivers. */
	memset(lp, 0, size);

	/* sysfs register */
	if (!driver_registered) {
		platform_driver_register(&uml_net_driver);
		driver_registered = 1;
	}
	device->pdev.id = n;
	device->pdev.name = DRIVER_NAME;
	platform_device_register(&device->pdev);
	SET_NETDEV_DEV(dev,&device->pdev.dev);

	/* If this name ends up conflicting with an existing registered
	 * netdevice, that is OK, register_netdev{,ice}() will notice this
	 * and fail.
	 */
	snprintf(dev->name, sizeof(dev->name), "eth%d", n);
	device->dev = dev;

	(*transport->kern->init)(dev, init);

	dev->mtu = transport->user->max_packet;
	dev->open = uml_net_open;
	dev->hard_start_xmit = uml_net_start_xmit;
	dev->stop = uml_net_close;
	dev->get_stats = uml_net_get_stats;
	dev->set_multicast_list = uml_net_set_multicast_list;
	dev->tx_timeout = uml_net_tx_timeout;
	dev->set_mac_address = uml_net_set_mac;
	dev->change_mtu = uml_net_change_mtu;
	dev->ethtool_ops = &uml_net_ethtool_ops;
	dev->watchdog_timeo = (HZ >> 1);
	dev->irq = UM_ETH_IRQ;

	rtnl_lock();
	err = register_netdevice(dev);
	rtnl_unlock();
	if (err) {
		device->dev = NULL;
		/* XXX: should we call ->remove() here? */
		free_netdev(dev);
		return 1;
	}

	/* lp.user is the first four bytes of the transport data, which
	 * has already been initialized.  This structure assignment will
	 * overwrite that, so we make sure that .user gets overwritten with
	 * what it already has.
	 */
	save = lp->user[0];
	*lp = ((struct uml_net_private)
		{ .list  		= LIST_HEAD_INIT(lp->list),
		  .dev 			= dev,
		  .fd 			= -1,
		  .mac 			= { 0xfe, 0xfd, 0x0, 0x0, 0x0, 0x0},
		  .have_mac 		= device->have_mac,
		  .protocol 		= transport->kern->protocol,
		  .open 		= transport->user->open,
		  .close 		= transport->user->close,
		  .remove 		= transport->user->remove,
		  .read 		= transport->kern->read,
		  .write 		= transport->kern->write,
		  .add_address 		= transport->user->add_address,
		  .delete_address  	= transport->user->delete_address,
		  .set_mtu 		= transport->user->set_mtu,
		  .user  		= { save } });

	init_timer(&lp->tl);
	spin_lock_init(&lp->lock);
	lp->tl.function = uml_net_user_timer_expire;
	if (lp->have_mac)
		memcpy(lp->mac, device->mac, sizeof(lp->mac));

	if (transport->user->init) 
		(*transport->user->init)(&lp->user, dev);

	if (device->have_mac)
		set_ether_mac(dev, device->mac);

	spin_lock(&opened_lock);
	list_add(&lp->list, &opened);
	spin_unlock(&opened_lock);

	return(0);
}

static struct uml_net *find_device(int n)
{
	struct uml_net *device;
	struct list_head *ele;

	spin_lock(&devices_lock);
	list_for_each(ele, &devices){
		device = list_entry(ele, struct uml_net, list);
		if(device->index == n)
			goto out;
	}
	device = NULL;
 out:
	spin_unlock(&devices_lock);
	return(device);
}

static int eth_parse(char *str, int *index_out, char **str_out)
{
	char *end;
	int n;

	n = simple_strtoul(str, &end, 0);
	if(end == str){
		printk(KERN_ERR "eth_setup: Failed to parse '%s'\n", str);
		return(1);
	}
	if(n < 0){
		printk(KERN_ERR "eth_setup: device %d is negative\n", n);
		return(1);
	}
	str = end;
	if(*str != '='){
		printk(KERN_ERR 
		       "eth_setup: expected '=' after device number\n");
		return(1);
	}
	str++;
	if(find_device(n)){
		printk(KERN_ERR "eth_setup: Device %d already configured\n",
		       n);
		return(1);
	}
	if(index_out) *index_out = n;
	*str_out = str;
	return(0);
}

struct eth_init {
	struct list_head list;
	char *init;
	int index;
};

/* Filled in at boot time.  Will need locking if the transports become
 * modular.
 */
struct list_head transports = LIST_HEAD_INIT(transports);

/* Filled in during early boot */
struct list_head eth_cmd_line = LIST_HEAD_INIT(eth_cmd_line);

static int check_transport(struct transport *transport, char *eth, int n,
			   void **init_out, char **mac_out)
{
	int len;

	len = strlen(transport->name);
	if(strncmp(eth, transport->name, len))
		return(0);

	eth += len;
	if(*eth == ',')
		eth++;
	else if(*eth != '\0')
		return(0);

	*init_out = kmalloc(transport->setup_size, GFP_KERNEL);
	if(*init_out == NULL)
		return(1);

	if(!transport->setup(eth, mac_out, *init_out)){
		kfree(*init_out);
		*init_out = NULL;
	}
	return(1);
}

void register_transport(struct transport *new)
{
	struct list_head *ele, *next;
	struct eth_init *eth;
	void *init;
	char *mac = NULL;
	int match;

	list_add(&new->list, &transports);

	list_for_each_safe(ele, next, &eth_cmd_line){
		eth = list_entry(ele, struct eth_init, list);
		match = check_transport(new, eth->init, eth->index, &init,
					&mac);
		if(!match)
			continue;
		else if(init != NULL){
			eth_configure(eth->index, init, mac, new);
			kfree(init);
		}
		list_del(&eth->list);
	}
}

static int eth_setup_common(char *str, int index)
{
	struct list_head *ele;
	struct transport *transport;
	void *init;
	char *mac = NULL;

	list_for_each(ele, &transports){
		transport = list_entry(ele, struct transport, list);
	        if(!check_transport(transport, str, index, &init, &mac))
			continue;
		if(init != NULL){
			eth_configure(index, init, mac, transport);
			kfree(init);
		}
		return(1);
	}
	return(0);
}

static int eth_setup(char *str)
{
	struct eth_init *new;
	int n, err;

	err = eth_parse(str, &n, &str);
	if(err) return(1);

	new = alloc_bootmem(sizeof(new));
	if (new == NULL){
		printk("eth_init : alloc_bootmem failed\n");
		return(1);
	}

	INIT_LIST_HEAD(&new->list);
	new->index = n;
	new->init = str;

	list_add_tail(&new->list, &eth_cmd_line);
	return(1);
}

__setup("eth", eth_setup);
__uml_help(eth_setup,
"eth[0-9]+=<transport>,<options>\n"
"    Configure a network device.\n\n"
);

#if 0
static int eth_init(void)
{
	struct list_head *ele, *next;
	struct eth_init *eth;

	list_for_each_safe(ele, next, &eth_cmd_line){
		eth = list_entry(ele, struct eth_init, list);

		if(eth_setup_common(eth->init, eth->index))
			list_del(&eth->list);
	}
	
	return(1);
}
__initcall(eth_init);
#endif

static int net_config(char *str)
{
	int n, err;

	err = eth_parse(str, &n, &str);
	if(err) return(err);

	str = kstrdup(str, GFP_KERNEL);
	if(str == NULL){
		printk(KERN_ERR "net_config failed to strdup string\n");
		return(-1);
	}
	err = !eth_setup_common(str, n);
	if(err) 
		kfree(str);
	return(err);
}

static int net_id(char **str, int *start_out, int *end_out)
{
        char *end;
        int n;

	n = simple_strtoul(*str, &end, 0);
	if((*end != '\0') || (end == *str))
		return -1;

        *start_out = n;
        *end_out = n;
        *str = end;
        return n;
}

static int net_remove(int n)
{
	struct uml_net *device;
	struct net_device *dev;
	struct uml_net_private *lp;

	device = find_device(n);
	if(device == NULL)
		return -ENODEV;

	dev = device->dev;
	lp = dev->priv;
	if(lp->fd > 0)
                return -EBUSY;
	if(lp->remove != NULL) (*lp->remove)(&lp->user);
	unregister_netdev(dev);
	platform_device_unregister(&device->pdev);

	list_del(&device->list);
	kfree(device);
	free_netdev(dev);
	return 0;
}

static struct mc_device net_mc = {
	.name		= "eth",
	.config		= net_config,
	.get_config	= NULL,
        .id		= net_id,
	.remove		= net_remove,
};

static int uml_inetaddr_event(struct notifier_block *this, unsigned long event,
			      void *ptr)
{
	struct in_ifaddr *ifa = ptr;
	struct net_device *dev = ifa->ifa_dev->dev;
	struct uml_net_private *lp;
	void (*proc)(unsigned char *, unsigned char *, void *);
	unsigned char addr_buf[4], netmask_buf[4];

	if(dev->open != uml_net_open) return(NOTIFY_DONE);

	lp = dev->priv;

	proc = NULL;
	switch (event){
	case NETDEV_UP:
		proc = lp->add_address;
		break;
	case NETDEV_DOWN:
		proc = lp->delete_address;
		break;
	}
	if(proc != NULL){
		memcpy(addr_buf, &ifa->ifa_address, sizeof(addr_buf));
		memcpy(netmask_buf, &ifa->ifa_mask, sizeof(netmask_buf));
		(*proc)(addr_buf, netmask_buf, &lp->user);
	}
	return(NOTIFY_DONE);
}

struct notifier_block uml_inetaddr_notifier = {
	.notifier_call		= uml_inetaddr_event,
};

static int uml_net_init(void)
{
	struct list_head *ele;
	struct uml_net_private *lp;	
	struct in_device *ip;
	struct in_ifaddr *in;

	mconsole_register_dev(&net_mc);
	register_inetaddr_notifier(&uml_inetaddr_notifier);

	/* Devices may have been opened already, so the uml_inetaddr_notifier
	 * didn't get a chance to run for them.  This fakes it so that
	 * addresses which have already been set up get handled properly.
	 */
	list_for_each(ele, &opened){
		lp = list_entry(ele, struct uml_net_private, list);
		ip = lp->dev->ip_ptr;
		if(ip == NULL) continue;
		in = ip->ifa_list;
		while(in != NULL){
			uml_inetaddr_event(NULL, NETDEV_UP, in);
			in = in->ifa_next;
		}
	}	

	return(0);
}

__initcall(uml_net_init);

static void close_devices(void)
{
	struct list_head *ele;
	struct uml_net_private *lp;

	list_for_each(ele, &opened){
		lp = list_entry(ele, struct uml_net_private, list);
		free_irq(lp->dev->irq, lp->dev);
		if((lp->close != NULL) && (lp->fd >= 0))
			(*lp->close)(lp->fd, &lp->user);
		if(lp->remove != NULL) (*lp->remove)(&lp->user);
	}
}

__uml_exitcall(close_devices);

int setup_etheraddr(char *str, unsigned char *addr)
{
	char *end;
	int i;

	if(str == NULL)
		return(0);
	for(i=0;i<6;i++){
		addr[i] = simple_strtoul(str, &end, 16);
		if((end == str) ||
		   ((*end != ':') && (*end != ',') && (*end != '\0'))){
			printk(KERN_ERR 
			       "setup_etheraddr: failed to parse '%s' "
			       "as an ethernet address\n", str);
			return(0);
		}
		str = end + 1;
	}
	if(addr[0] & 1){
		printk(KERN_ERR 
		       "Attempt to assign a broadcast ethernet address to a "
		       "device disallowed\n");
		return(0);
	}
	return(1);
}

void dev_ip_addr(void *d, unsigned char *bin_buf)
{
	struct net_device *dev = d;
	struct in_device *ip = dev->ip_ptr;
	struct in_ifaddr *in;

	if((ip == NULL) || ((in = ip->ifa_list) == NULL)){
		printk(KERN_WARNING "dev_ip_addr - device not assigned an "
		       "IP address\n");
		return;
	}
	memcpy(bin_buf, &in->ifa_address, sizeof(in->ifa_address));
}

void set_ether_mac(void *d, unsigned char *addr)
{
	struct net_device *dev = d;

	memcpy(dev->dev_addr, addr, ETH_ALEN);	
}

struct sk_buff *ether_adjust_skb(struct sk_buff *skb, int extra)
{
	if((skb != NULL) && (skb_tailroom(skb) < extra)){
	  	struct sk_buff *skb2;

		skb2 = skb_copy_expand(skb, 0, extra, GFP_ATOMIC);
		dev_kfree_skb(skb);
		skb = skb2;
	}
	if(skb != NULL) skb_put(skb, extra);
	return(skb);
}

void iter_addresses(void *d, void (*cb)(unsigned char *, unsigned char *, 
					void *), 
		    void *arg)
{
	struct net_device *dev = d;
	struct in_device *ip = dev->ip_ptr;
	struct in_ifaddr *in;
	unsigned char address[4], netmask[4];

	if(ip == NULL) return;
	in = ip->ifa_list;
	while(in != NULL){
		memcpy(address, &in->ifa_address, sizeof(address));
		memcpy(netmask, &in->ifa_mask, sizeof(netmask));
		(*cb)(address, netmask, arg);
		in = in->ifa_next;
	}
}

int dev_netmask(void *d, void *m)
{
	struct net_device *dev = d;
	struct in_device *ip = dev->ip_ptr;
	struct in_ifaddr *in;
	__u32 *mask_out = m;

	if(ip == NULL) 
		return(1);

	in = ip->ifa_list;
	if(in == NULL) 
		return(1);

	*mask_out = in->ifa_mask;
	return(0);
}

void *get_output_buffer(int *len_out)
{
	void *ret;

	ret = (void *) __get_free_pages(GFP_KERNEL, 0);
	if(ret) *len_out = PAGE_SIZE;
	else *len_out = 0;
	return(ret);
}

void free_output_buffer(void *buffer)
{
	free_pages((unsigned long) buffer, 0);
}

int tap_setup_common(char *str, char *type, char **dev_name, char **mac_out, 
		     char **gate_addr)
{
	char *remain;

	remain = split_if_spec(str, dev_name, mac_out, gate_addr, NULL);
	if(remain != NULL){
		printk("tap_setup_common - Extra garbage on specification : "
		       "'%s'\n", remain);
		return(1);
	}

	return(0);
}

unsigned short eth_protocol(struct sk_buff *skb)
{
	return(eth_type_trans(skb, skb->dev));
}

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
