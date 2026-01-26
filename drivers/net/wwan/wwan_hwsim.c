// SPDX-License-Identifier: GPL-2.0-only
/*
 * WWAN device simulator for WWAN framework testing.
 *
 * Copyright (c) 2021, 2025, Sergey Ryazanov <ryazanov.s.a@gmail.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/spinlock.h>
#include <linux/time.h>
#include <linux/list.h>
#include <linux/skbuff.h>
#include <linux/timer.h>
#include <linux/netdevice.h>
#include <linux/wwan.h>
#include <linux/debugfs.h>
#include <linux/workqueue.h>

#include <net/arp.h>

static int wwan_hwsim_devsnum = 2;
module_param_named(devices, wwan_hwsim_devsnum, int, 0444);
MODULE_PARM_DESC(devices, "Number of simulated devices");

static const struct class wwan_hwsim_class = {
	.name = "wwan_hwsim",
};

static struct dentry *wwan_hwsim_debugfs_topdir;
static struct dentry *wwan_hwsim_debugfs_devcreate;

static DEFINE_SPINLOCK(wwan_hwsim_devs_lock);
static LIST_HEAD(wwan_hwsim_devs);
static unsigned int wwan_hwsim_dev_idx;
static struct workqueue_struct *wwan_wq;

struct wwan_hwsim_dev {
	struct list_head list;
	unsigned int id;
	struct device dev;
	struct work_struct del_work;
	struct dentry *debugfs_topdir;
	struct dentry *debugfs_portcreate;
	spinlock_t ports_lock;	/* Serialize ports creation/deletion */
	unsigned int port_idx;
	struct list_head ports;
};

struct wwan_hwsim_port {
	struct list_head list;
	unsigned int id;
	struct wwan_hwsim_dev *dev;
	struct wwan_port *wwan;
	struct work_struct del_work;
	struct dentry *debugfs_topdir;
	union {
		struct {
			enum {	/* AT command parser state */
				AT_PARSER_WAIT_A,
				AT_PARSER_WAIT_T,
				AT_PARSER_WAIT_TERM,
				AT_PARSER_SKIP_LINE,
			} pstate;
		} at_emul;
		struct {
			struct timer_list timer;
		} nmea_emul;
	};
};

static const struct file_operations wwan_hwsim_debugfs_portdestroy_fops;
static const struct file_operations wwan_hwsim_debugfs_portcreate_fops;
static const struct file_operations wwan_hwsim_debugfs_devdestroy_fops;
static void wwan_hwsim_port_del_work(struct work_struct *work);
static void wwan_hwsim_dev_del_work(struct work_struct *work);

static netdev_tx_t wwan_hwsim_netdev_xmit(struct sk_buff *skb,
					  struct net_device *ndev)
{
	ndev->stats.tx_packets++;
	ndev->stats.tx_bytes += skb->len;
	consume_skb(skb);
	return NETDEV_TX_OK;
}

static const struct net_device_ops wwan_hwsim_netdev_ops = {
	.ndo_start_xmit = wwan_hwsim_netdev_xmit,
};

static void wwan_hwsim_netdev_setup(struct net_device *ndev)
{
	ndev->netdev_ops = &wwan_hwsim_netdev_ops;
	ndev->needs_free_netdev = true;

	ndev->mtu = ETH_DATA_LEN;
	ndev->min_mtu = ETH_MIN_MTU;
	ndev->max_mtu = ETH_MAX_MTU;

	ndev->type = ARPHRD_NONE;
	ndev->flags = IFF_POINTOPOINT | IFF_NOARP;
}

static const struct wwan_ops wwan_hwsim_wwan_rtnl_ops = {
	.priv_size = 0,			/* No private data */
	.setup = wwan_hwsim_netdev_setup,
};

static int wwan_hwsim_at_emul_start(struct wwan_port *wport)
{
	struct wwan_hwsim_port *port = wwan_port_get_drvdata(wport);

	port->at_emul.pstate = AT_PARSER_WAIT_A;

	return 0;
}

static void wwan_hwsim_at_emul_stop(struct wwan_port *wport)
{
}

/* Implements a minimalistic AT commands parser that echo input back and
 * reply with 'OK' to each input command. See AT command protocol details in the
 * ITU-T V.250 recomendations document.
 *
 * Be aware that this processor is not fully V.250 compliant.
 */
static int wwan_hwsim_at_emul_tx(struct wwan_port *wport, struct sk_buff *in)
{
	struct wwan_hwsim_port *port = wwan_port_get_drvdata(wport);
	struct sk_buff *out;
	int i, n, s;

	/* Estimate a max possible number of commands by counting the number of
	 * termination chars (S3 param, CR by default). And then allocate the
	 * output buffer that will be enough to fit the echo and result codes of
	 * all commands.
	 */
	for (i = 0, n = 0; i < in->len; ++i)
		if (in->data[i] == '\r')
			n++;
	n = in->len + n * (2 + 2 + 2);	/* Output buffer size */
	out = alloc_skb(n, GFP_KERNEL);
	if (!out)
		return -ENOMEM;

	for (i = 0, s = 0; i < in->len; ++i) {
		char c = in->data[i];

		if (port->at_emul.pstate == AT_PARSER_WAIT_A) {
			if (c == 'A' || c == 'a')
				port->at_emul.pstate = AT_PARSER_WAIT_T;
			else if (c != '\n')	/* Ignore formating char */
				port->at_emul.pstate = AT_PARSER_SKIP_LINE;
		} else if (port->at_emul.pstate == AT_PARSER_WAIT_T) {
			if (c == 'T' || c == 't')
				port->at_emul.pstate = AT_PARSER_WAIT_TERM;
			else
				port->at_emul.pstate = AT_PARSER_SKIP_LINE;
		} else if (port->at_emul.pstate == AT_PARSER_WAIT_TERM) {
			if (c != '\r')
				continue;
			/* Consume the trailing formatting char as well */
			if ((i + 1) < in->len && in->data[i + 1] == '\n')
				i++;
			n = i - s + 1;
			skb_put_data(out, &in->data[s], n);/* Echo */
			skb_put_data(out, "\r\nOK\r\n", 6);
			s = i + 1;
			port->at_emul.pstate = AT_PARSER_WAIT_A;
		} else if (port->at_emul.pstate == AT_PARSER_SKIP_LINE) {
			if (c != '\r')
				continue;
			port->at_emul.pstate = AT_PARSER_WAIT_A;
		}
	}

	if (i > s) {
		/* Echo the processed portion of a not yet completed command */
		n = i - s;
		skb_put_data(out, &in->data[s], n);
	}

	consume_skb(in);

	wwan_port_rx(wport, out);

	return 0;
}

static const struct wwan_port_ops wwan_hwsim_at_emul_port_ops = {
	.start = wwan_hwsim_at_emul_start,
	.stop = wwan_hwsim_at_emul_stop,
	.tx = wwan_hwsim_at_emul_tx,
};

#if IS_ENABLED(CONFIG_GNSS)
#define NMEA_MAX_LEN		82	/* Max sentence length */
#define NMEA_TRAIL_LEN		5	/* '*' + Checksum + <CR><LF> */
#define NMEA_MAX_DATA_LEN	(NMEA_MAX_LEN - NMEA_TRAIL_LEN)

static __printf(2, 3)
void wwan_hwsim_nmea_skb_push_sentence(struct sk_buff *skb,
				       const char *fmt, ...)
{
	unsigned char *s, *p;
	va_list ap;
	u8 cs = 0;
	int len;

	s = skb_put(skb, NMEA_MAX_LEN + 1);	/* +'\0' */
	if (!s)
		return;

	va_start(ap, fmt);
	len = vsnprintf(s, NMEA_MAX_DATA_LEN + 1, fmt, ap);
	va_end(ap);
	if (WARN_ON_ONCE(len > NMEA_MAX_DATA_LEN))/* No space for trailer */
		return;

	for (p = s + 1; *p != '\0'; ++p)/* Skip leading '$' or '!' */
		cs ^= *p;
	p += snprintf(p, 5 + 1, "*%02X\r\n", cs);

	len = (p - s) - (NMEA_MAX_LEN + 1);	/* exp. vs real length diff */
	skb->tail += len;			/* Adjust tail to real length */
	skb->len += len;
}

static void wwan_hwsim_nmea_emul_timer(struct timer_list *t)
{
	/* 43.74754722298909 N 11.25759835922875 E in DMM format */
	static const unsigned int coord[4 * 2] = { 43, 44, 8528, 0,
						   11, 15, 4559, 0 };
	struct wwan_hwsim_port *port = timer_container_of(port, t, nmea_emul.timer);
	struct sk_buff *skb;
	struct tm tm;

	time64_to_tm(ktime_get_real_seconds(), 0, &tm);

	mod_timer(&port->nmea_emul.timer, jiffies + HZ);	/* 1 second */

	skb = alloc_skb(NMEA_MAX_LEN * 2 + 2, GFP_ATOMIC);	/* GGA + RMC */
	if (!skb)
		return;

	wwan_hwsim_nmea_skb_push_sentence(skb,
					  "$GPGGA,%02u%02u%02u.000,%02u%02u.%04u,%c,%03u%02u.%04u,%c,1,7,1.03,176.2,M,55.2,M,,",
					  tm.tm_hour, tm.tm_min, tm.tm_sec,
					  coord[0], coord[1], coord[2],
					  coord[3] ? 'S' : 'N',
					  coord[4], coord[5], coord[6],
					  coord[7] ? 'W' : 'E');

	wwan_hwsim_nmea_skb_push_sentence(skb,
					  "$GPRMC,%02u%02u%02u.000,A,%02u%02u.%04u,%c,%03u%02u.%04u,%c,0.02,31.66,%02u%02u%02u,,,A",
					  tm.tm_hour, tm.tm_min, tm.tm_sec,
					  coord[0], coord[1], coord[2],
					  coord[3] ? 'S' : 'N',
					  coord[4], coord[5], coord[6],
					  coord[7] ? 'W' : 'E',
					  tm.tm_mday, tm.tm_mon + 1,
					  (unsigned int)tm.tm_year - 100);

	wwan_port_rx(port->wwan, skb);
}

static int wwan_hwsim_nmea_emul_start(struct wwan_port *wport)
{
	struct wwan_hwsim_port *port = wwan_port_get_drvdata(wport);

	timer_setup(&port->nmea_emul.timer, wwan_hwsim_nmea_emul_timer, 0);
	wwan_hwsim_nmea_emul_timer(&port->nmea_emul.timer);

	return 0;
}

static void wwan_hwsim_nmea_emul_stop(struct wwan_port *wport)
{
	struct wwan_hwsim_port *port = wwan_port_get_drvdata(wport);

	timer_delete_sync(&port->nmea_emul.timer);
}

static int wwan_hwsim_nmea_emul_tx(struct wwan_port *wport, struct sk_buff *in)
{
	consume_skb(in);

	return 0;
}

static const struct wwan_port_ops wwan_hwsim_nmea_emul_port_ops = {
	.start = wwan_hwsim_nmea_emul_start,
	.stop = wwan_hwsim_nmea_emul_stop,
	.tx = wwan_hwsim_nmea_emul_tx,
};
#endif

static struct wwan_hwsim_port *wwan_hwsim_port_new(struct wwan_hwsim_dev *dev,
						   enum wwan_port_type type)
{
	const struct wwan_port_ops *ops;
	struct wwan_hwsim_port *port;
	char name[0x10];
	int err;

	if (type == WWAN_PORT_AT)
		ops = &wwan_hwsim_at_emul_port_ops;
#if IS_ENABLED(CONFIG_GNSS)
	else if (type == WWAN_PORT_NMEA)
		ops = &wwan_hwsim_nmea_emul_port_ops;
#endif
	else
		return ERR_PTR(-EINVAL);

	port = kzalloc(sizeof(*port), GFP_KERNEL);
	if (!port)
		return ERR_PTR(-ENOMEM);

	port->dev = dev;

	spin_lock(&dev->ports_lock);
	port->id = dev->port_idx++;
	spin_unlock(&dev->ports_lock);

	port->wwan = wwan_create_port(&dev->dev, type, ops, NULL, port);
	if (IS_ERR(port->wwan)) {
		err = PTR_ERR(port->wwan);
		goto err_free_port;
	}

	INIT_WORK(&port->del_work, wwan_hwsim_port_del_work);

	snprintf(name, sizeof(name), "port%u", port->id);
	port->debugfs_topdir = debugfs_create_dir(name, dev->debugfs_topdir);
	debugfs_create_file("destroy", 0200, port->debugfs_topdir, port,
			    &wwan_hwsim_debugfs_portdestroy_fops);

	return port;

err_free_port:
	kfree(port);

	return ERR_PTR(err);
}

static void wwan_hwsim_port_del(struct wwan_hwsim_port *port)
{
	debugfs_remove(port->debugfs_topdir);

	/* Make sure that there is no pending deletion work */
	if (current_work() != &port->del_work)
		cancel_work_sync(&port->del_work);

	wwan_remove_port(port->wwan);
	kfree(port);
}

static void wwan_hwsim_port_del_work(struct work_struct *work)
{
	struct wwan_hwsim_port *port =
				container_of(work, typeof(*port), del_work);
	struct wwan_hwsim_dev *dev = port->dev;

	spin_lock(&dev->ports_lock);
	if (list_empty(&port->list)) {
		/* Someone else deleting port at the moment */
		spin_unlock(&dev->ports_lock);
		return;
	}
	list_del_init(&port->list);
	spin_unlock(&dev->ports_lock);

	wwan_hwsim_port_del(port);
}

static void wwan_hwsim_dev_release(struct device *sysdev)
{
	struct wwan_hwsim_dev *dev = container_of(sysdev, typeof(*dev), dev);

	kfree(dev);
}

static struct wwan_hwsim_dev *wwan_hwsim_dev_new(void)
{
	struct wwan_hwsim_dev *dev;
	int err;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return ERR_PTR(-ENOMEM);

	spin_lock(&wwan_hwsim_devs_lock);
	dev->id = wwan_hwsim_dev_idx++;
	spin_unlock(&wwan_hwsim_devs_lock);

	dev->dev.release = wwan_hwsim_dev_release;
	dev->dev.class = &wwan_hwsim_class;
	dev_set_name(&dev->dev, "hwsim%u", dev->id);

	spin_lock_init(&dev->ports_lock);
	INIT_LIST_HEAD(&dev->ports);

	err = device_register(&dev->dev);
	if (err)
		goto err_free_dev;

	INIT_WORK(&dev->del_work, wwan_hwsim_dev_del_work);

	err = wwan_register_ops(&dev->dev, &wwan_hwsim_wwan_rtnl_ops, dev, 1);
	if (err)
		goto err_unreg_dev;

	dev->debugfs_topdir = debugfs_create_dir(dev_name(&dev->dev),
						 wwan_hwsim_debugfs_topdir);
	debugfs_create_file("destroy", 0200, dev->debugfs_topdir, dev,
			    &wwan_hwsim_debugfs_devdestroy_fops);
	dev->debugfs_portcreate =
		debugfs_create_file("portcreate", 0200,
				    dev->debugfs_topdir, dev,
				    &wwan_hwsim_debugfs_portcreate_fops);

	return dev;

err_unreg_dev:
	device_unregister(&dev->dev);
	/* Memory will be freed in the device release callback */

	return ERR_PTR(err);

err_free_dev:
	put_device(&dev->dev);

	return ERR_PTR(err);
}

static void wwan_hwsim_dev_del(struct wwan_hwsim_dev *dev)
{
	debugfs_remove(dev->debugfs_portcreate);	/* Avoid new ports */

	spin_lock(&dev->ports_lock);
	while (!list_empty(&dev->ports)) {
		struct wwan_hwsim_port *port;

		port = list_first_entry(&dev->ports, struct wwan_hwsim_port,
					list);
		list_del_init(&port->list);
		spin_unlock(&dev->ports_lock);
		wwan_hwsim_port_del(port);
		spin_lock(&dev->ports_lock);
	}
	spin_unlock(&dev->ports_lock);

	debugfs_remove(dev->debugfs_topdir);

	/* This will remove all child netdev(s) */
	wwan_unregister_ops(&dev->dev);

	/* Make sure that there is no pending deletion work */
	if (current_work() != &dev->del_work)
		cancel_work_sync(&dev->del_work);

	device_unregister(&dev->dev);
	/* Memory will be freed in the device release callback */
}

static void wwan_hwsim_dev_del_work(struct work_struct *work)
{
	struct wwan_hwsim_dev *dev = container_of(work, typeof(*dev), del_work);

	spin_lock(&wwan_hwsim_devs_lock);
	if (list_empty(&dev->list)) {
		/* Someone else deleting device at the moment */
		spin_unlock(&wwan_hwsim_devs_lock);
		return;
	}
	list_del_init(&dev->list);
	spin_unlock(&wwan_hwsim_devs_lock);

	wwan_hwsim_dev_del(dev);
}

static ssize_t wwan_hwsim_debugfs_portdestroy_write(struct file *file,
						    const char __user *usrbuf,
						    size_t count, loff_t *ppos)
{
	struct wwan_hwsim_port *port = file->private_data;

	/* We can not delete port here since it will cause a deadlock due to
	 * waiting this callback to finish in the debugfs_remove() call. So,
	 * use workqueue.
	 */
	queue_work(wwan_wq, &port->del_work);

	return count;
}

static const struct file_operations wwan_hwsim_debugfs_portdestroy_fops = {
	.write = wwan_hwsim_debugfs_portdestroy_write,
	.open = simple_open,
	.llseek = noop_llseek,
};

static ssize_t wwan_hwsim_debugfs_portcreate_write(struct file *file,
						   const char __user *usrbuf,
						   size_t count, loff_t *ppos)
{
	struct wwan_hwsim_dev *dev = file->private_data;
	struct wwan_hwsim_port *port;

	port = wwan_hwsim_port_new(dev, WWAN_PORT_AT);
	if (IS_ERR(port))
		return PTR_ERR(port);

	spin_lock(&dev->ports_lock);
	list_add_tail(&port->list, &dev->ports);
	spin_unlock(&dev->ports_lock);

	return count;
}

static const struct file_operations wwan_hwsim_debugfs_portcreate_fops = {
	.write = wwan_hwsim_debugfs_portcreate_write,
	.open = simple_open,
	.llseek = noop_llseek,
};

static ssize_t wwan_hwsim_debugfs_devdestroy_write(struct file *file,
						   const char __user *usrbuf,
						   size_t count, loff_t *ppos)
{
	struct wwan_hwsim_dev *dev = file->private_data;

	/* We can not delete device here since it will cause a deadlock due to
	 * waiting this callback to finish in the debugfs_remove() call. So,
	 * use workqueue.
	 */
	queue_work(wwan_wq, &dev->del_work);

	return count;
}

static const struct file_operations wwan_hwsim_debugfs_devdestroy_fops = {
	.write = wwan_hwsim_debugfs_devdestroy_write,
	.open = simple_open,
	.llseek = noop_llseek,
};

static ssize_t wwan_hwsim_debugfs_devcreate_write(struct file *file,
						  const char __user *usrbuf,
						  size_t count, loff_t *ppos)
{
	struct wwan_hwsim_dev *dev;

	dev = wwan_hwsim_dev_new();
	if (IS_ERR(dev))
		return PTR_ERR(dev);

	spin_lock(&wwan_hwsim_devs_lock);
	list_add_tail(&dev->list, &wwan_hwsim_devs);
	spin_unlock(&wwan_hwsim_devs_lock);

	return count;
}

static const struct file_operations wwan_hwsim_debugfs_devcreate_fops = {
	.write = wwan_hwsim_debugfs_devcreate_write,
	.open = simple_open,
	.llseek = noop_llseek,
};

static int __init wwan_hwsim_init_devs(void)
{
	struct wwan_hwsim_dev *dev;
	int i, j;

	for (i = 0; i < wwan_hwsim_devsnum; ++i) {
		struct wwan_hwsim_port *port;

		dev = wwan_hwsim_dev_new();
		if (IS_ERR(dev))
			return PTR_ERR(dev);

		spin_lock(&wwan_hwsim_devs_lock);
		list_add_tail(&dev->list, &wwan_hwsim_devs);
		spin_unlock(&wwan_hwsim_devs_lock);

		/* Create a few various ports per each device to accelerate
		 * the simulator readiness time.
		 */

		for (j = 0; j < 2; ++j) {
			port = wwan_hwsim_port_new(dev, WWAN_PORT_AT);
			if (IS_ERR(port))
				return PTR_ERR(port);

			spin_lock(&dev->ports_lock);
			list_add_tail(&port->list, &dev->ports);
			spin_unlock(&dev->ports_lock);
		}

#if IS_ENABLED(CONFIG_GNSS)
		port = wwan_hwsim_port_new(dev, WWAN_PORT_NMEA);
		if (IS_ERR(port)) {
			dev_warn(&dev->dev, "failed to create initial NMEA port: %d\n",
				 (int)PTR_ERR(port));
		} else {
			spin_lock(&dev->ports_lock);
			list_add_tail(&port->list, &dev->ports);
			spin_unlock(&dev->ports_lock);
		}
#endif
	}

	return 0;
}

static void wwan_hwsim_free_devs(void)
{
	struct wwan_hwsim_dev *dev;

	spin_lock(&wwan_hwsim_devs_lock);
	while (!list_empty(&wwan_hwsim_devs)) {
		dev = list_first_entry(&wwan_hwsim_devs, struct wwan_hwsim_dev,
				       list);
		list_del_init(&dev->list);
		spin_unlock(&wwan_hwsim_devs_lock);
		wwan_hwsim_dev_del(dev);
		spin_lock(&wwan_hwsim_devs_lock);
	}
	spin_unlock(&wwan_hwsim_devs_lock);
}

static int __init wwan_hwsim_init(void)
{
	int err;

	if (wwan_hwsim_devsnum < 0 || wwan_hwsim_devsnum > 128)
		return -EINVAL;

	wwan_wq = alloc_workqueue("wwan_wq", WQ_PERCPU, 0);
	if (!wwan_wq)
		return -ENOMEM;

	err = class_register(&wwan_hwsim_class);
	if (err)
		goto err_wq_destroy;

	wwan_hwsim_debugfs_topdir = debugfs_create_dir("wwan_hwsim", NULL);
	wwan_hwsim_debugfs_devcreate =
			debugfs_create_file("devcreate", 0200,
					    wwan_hwsim_debugfs_topdir, NULL,
					    &wwan_hwsim_debugfs_devcreate_fops);

	err = wwan_hwsim_init_devs();
	if (err)
		goto err_clean_devs;

	return 0;

err_clean_devs:
	debugfs_remove(wwan_hwsim_debugfs_devcreate);	/* Avoid new devs */
	wwan_hwsim_free_devs();
	flush_workqueue(wwan_wq);	/* Wait deletion works completion */
	debugfs_remove(wwan_hwsim_debugfs_topdir);
	class_unregister(&wwan_hwsim_class);
err_wq_destroy:
	destroy_workqueue(wwan_wq);

	return err;
}

static void __exit wwan_hwsim_exit(void)
{
	debugfs_remove(wwan_hwsim_debugfs_devcreate);	/* Avoid new devs */
	wwan_hwsim_free_devs();
	flush_workqueue(wwan_wq);	/* Wait deletion works completion */
	debugfs_remove(wwan_hwsim_debugfs_topdir);
	class_unregister(&wwan_hwsim_class);
	destroy_workqueue(wwan_wq);
}

module_init(wwan_hwsim_init);
module_exit(wwan_hwsim_exit);

MODULE_AUTHOR("Sergey Ryazanov");
MODULE_DESCRIPTION("Device simulator for WWAN framework");
MODULE_LICENSE("GPL");
