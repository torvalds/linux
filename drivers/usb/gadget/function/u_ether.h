/*
 * u_ether.h -- interface to USB gadget "ethernet link" utilities
 *
 * Copyright (C) 2003-2005,2008 David Brownell
 * Copyright (C) 2003-2004 Robert Schwebel, Benedikt Spranger
 * Copyright (C) 2008 Nokia Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __U_ETHER_H
#define __U_ETHER_H

#include <linux/err.h>
#include <linux/if_ether.h>
#include <linux/usb/composite.h>
#include <linux/usb/cdc.h>
#include <linux/netdevice.h>

#define QMULT_DEFAULT 5

/*
 * dev_addr: initial value
 * changed by "ifconfig usb0 hw ether xx:xx:xx:xx:xx:xx"
 * host_addr: this address is invisible to ifconfig
 */
#define USB_ETHERNET_MODULE_PARAMETERS() \
	static unsigned qmult = QMULT_DEFAULT;				\
	module_param(qmult, uint, S_IRUGO|S_IWUSR);			\
	MODULE_PARM_DESC(qmult, "queue length multiplier at high/super speed");\
									\
	static char *dev_addr;						\
	module_param(dev_addr, charp, S_IRUGO);				\
	MODULE_PARM_DESC(dev_addr, "Device Ethernet Address");		\
									\
	static char *host_addr;						\
	module_param(host_addr, charp, S_IRUGO);			\
	MODULE_PARM_DESC(host_addr, "Host Ethernet Address")

struct eth_dev;

/*
 * This represents the USB side of an "ethernet" link, managed by a USB
 * function which provides control and (maybe) framing.  Two functions
 * in different configurations could share the same ethernet link/netdev,
 * using different host interaction models.
 *
 * There is a current limitation that only one instance of this link may
 * be present in any given configuration.  When that's a problem, network
 * layer facilities can be used to package multiple logical links on this
 * single "physical" one.
 */
struct gether {
	struct usb_function		func;

	/* updated by gether_{connect,disconnect} */
	struct eth_dev			*ioport;

	/* endpoints handle full and/or high speeds */
	struct usb_ep			*in_ep;
	struct usb_ep			*out_ep;

	bool				is_zlp_ok;

	u16				cdc_filter;

	/* hooks for added framing, as needed for RNDIS and EEM. */
	u32				header_len;
	/* NCM requires fixed size bundles */
	bool				is_fixed;
	u32				fixed_out_len;
	u32				fixed_in_len;
	bool				supports_multi_frame;
	struct sk_buff			*(*wrap)(struct gether *port,
						struct sk_buff *skb);
	int				(*unwrap)(struct gether *port,
						struct sk_buff *skb,
						struct sk_buff_head *list);

	/* called on network open/close */
	void				(*open)(struct gether *);
	void				(*close)(struct gether *);
};

#define	DEFAULT_FILTER	(USB_CDC_PACKET_TYPE_BROADCAST \
			|USB_CDC_PACKET_TYPE_ALL_MULTICAST \
			|USB_CDC_PACKET_TYPE_PROMISCUOUS \
			|USB_CDC_PACKET_TYPE_DIRECTED)

/* variant of gether_setup that allows customizing network device name */
struct eth_dev *gether_setup_name(struct usb_gadget *g,
		const char *dev_addr, const char *host_addr,
		u8 ethaddr[ETH_ALEN], unsigned qmult, const char *netname);

/* netdev setup/teardown as directed by the gadget driver */
/* gether_setup - initialize one ethernet-over-usb link
 * @g: gadget to associated with these links
 * @ethaddr: NULL, or a buffer in which the ethernet address of the
 *	host side of the link is recorded
 * Context: may sleep
 *
 * This sets up the single network link that may be exported by a
 * gadget driver using this framework.  The link layer addresses are
 * set up using module parameters.
 *
 * Returns a eth_dev pointer on success, or an ERR_PTR on failure
 */
static inline struct eth_dev *gether_setup(struct usb_gadget *g,
		const char *dev_addr, const char *host_addr,
		u8 ethaddr[ETH_ALEN], unsigned qmult)
{
	return gether_setup_name(g, dev_addr, host_addr, ethaddr, qmult, "usb");
}

/*
 * variant of gether_setup_default that allows customizing
 * network device name
 */
struct net_device *gether_setup_name_default(const char *netname);

/*
 * gether_register_netdev - register the net device
 * @net: net device to register
 *
 * Registers the net device associated with this ethernet-over-usb link
 *
 */
int gether_register_netdev(struct net_device *net);

/* gether_setup_default - initialize one ethernet-over-usb link
 * Context: may sleep
 *
 * This sets up the single network link that may be exported by a
 * gadget driver using this framework.  The link layer addresses
 * are set to random values.
 *
 * Returns negative errno, or zero on success
 */
static inline struct net_device *gether_setup_default(void)
{
	return gether_setup_name_default("usb");
}

/**
 * gether_set_gadget - initialize one ethernet-over-usb link with a gadget
 * @net: device representing this link
 * @g: the gadget to initialize with
 *
 * This associates one ethernet-over-usb link with a gadget.
 */
void gether_set_gadget(struct net_device *net, struct usb_gadget *g);

/**
 * gether_set_dev_addr - initialize an ethernet-over-usb link with eth address
 * @net: device representing this link
 * @dev_addr: eth address of this device
 *
 * This sets the device-side Ethernet address of this ethernet-over-usb link
 * if dev_addr is correct.
 * Returns negative errno if the new address is incorrect.
 */
int gether_set_dev_addr(struct net_device *net, const char *dev_addr);

/**
 * gether_get_dev_addr - get an ethernet-over-usb link eth address
 * @net: device representing this link
 * @dev_addr: place to store device's eth address
 * @len: length of the @dev_addr buffer
 *
 * This gets the device-side Ethernet address of this ethernet-over-usb link.
 * Returns zero on success, else negative errno.
 */
int gether_get_dev_addr(struct net_device *net, char *dev_addr, int len);

/**
 * gether_set_host_addr - initialize an ethernet-over-usb link with host address
 * @net: device representing this link
 * @host_addr: eth address of the host
 *
 * This sets the host-side Ethernet address of this ethernet-over-usb link
 * if host_addr is correct.
 * Returns negative errno if the new address is incorrect.
 */
int gether_set_host_addr(struct net_device *net, const char *host_addr);

/**
 * gether_get_host_addr - get an ethernet-over-usb link host address
 * @net: device representing this link
 * @host_addr: place to store eth address of the host
 * @len: length of the @host_addr buffer
 *
 * This gets the host-side Ethernet address of this ethernet-over-usb link.
 * Returns zero on success, else negative errno.
 */
int gether_get_host_addr(struct net_device *net, char *host_addr, int len);

/**
 * gether_get_host_addr_cdc - get an ethernet-over-usb link host address
 * @net: device representing this link
 * @host_addr: place to store eth address of the host
 * @len: length of the @host_addr buffer
 *
 * This gets the CDC formatted host-side Ethernet address of this
 * ethernet-over-usb link.
 * Returns zero on success, else negative errno.
 */
int gether_get_host_addr_cdc(struct net_device *net, char *host_addr, int len);

/**
 * gether_get_host_addr_u8 - get an ethernet-over-usb link host address
 * @net: device representing this link
 * @host_mac: place to store the eth address of the host
 *
 * This gets the binary formatted host-side Ethernet address of this
 * ethernet-over-usb link.
 */
void gether_get_host_addr_u8(struct net_device *net, u8 host_mac[ETH_ALEN]);

/**
 * gether_set_qmult - initialize an ethernet-over-usb link with a multiplier
 * @net: device representing this link
 * @qmult: queue multiplier
 *
 * This sets the queue length multiplier of this ethernet-over-usb link.
 * For higher speeds use longer queues.
 */
void gether_set_qmult(struct net_device *net, unsigned qmult);

/**
 * gether_get_qmult - get an ethernet-over-usb link multiplier
 * @net: device representing this link
 *
 * This gets the queue length multiplier of this ethernet-over-usb link.
 */
unsigned gether_get_qmult(struct net_device *net);

/**
 * gether_get_ifname - get an ethernet-over-usb link interface name
 * @net: device representing this link
 * @name: place to store the interface name
 * @len: length of the @name buffer
 *
 * This gets the interface name of this ethernet-over-usb link.
 * Returns zero on success, else negative errno.
 */
int gether_get_ifname(struct net_device *net, char *name, int len);

void gether_cleanup(struct eth_dev *dev);

/* connect/disconnect is handled by individual functions */
struct net_device *gether_connect(struct gether *);
void gether_disconnect(struct gether *);

/* Some controllers can't support CDC Ethernet (ECM) ... */
static inline bool can_support_ecm(struct usb_gadget *gadget)
{
	if (!gadget_is_altset_supported(gadget))
		return false;

	/* Everything else is *presumably* fine ... but this is a bit
	 * chancy, so be **CERTAIN** there are no hardware issues with
	 * your controller.  Add it above if it can't handle CDC.
	 */
	return true;
}

#endif /* __U_ETHER_H */
