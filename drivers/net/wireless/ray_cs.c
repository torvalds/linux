// SPDX-License-Identifier: GPL-2.0-only
/*=============================================================================
 *
 * A  PCMCIA client driver for the Raylink wireless LAN card.
 * The starting point for this module was the skeleton.c in the
 * PCMCIA 2.9.12 package written by David Hinds, dahinds@users.sourceforge.net
 *
 * Copyright (c) 1998  Corey Thomas (corey@world.std.com)
 *
 * Changes:
 * Arnaldo Carvalho de Melo <acme@conectiva.com.br> - 08/08/2000
 * - reorganize kmallocs in ray_attach, checking all for failure
 *   and releasing the previous allocations if one fails
 *
 * Daniele Bellucci <bellucda@tiscali.it> - 07/10/2003
 * - Audit copy_to_user in ioctl(SIOCGIWESSID)
 *
=============================================================================*/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/ptrace.h>
#include <linux/seq_file.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/if_arp.h>
#include <linux/ioport.h>
#include <linux/skbuff.h>
#include <linux/ieee80211.h>

#include <pcmcia/cistpl.h>
#include <pcmcia/cisreg.h>
#include <pcmcia/ds.h>

#include <linux/wireless.h>
#include <net/iw_handler.h>

#include <asm/io.h>
#include <asm/byteorder.h>
#include <linux/uaccess.h>

/* Warning : these stuff will slow down the driver... */
#define WIRELESS_SPY		/* Enable spying addresses */
/* Definitions we need for spy */
typedef struct iw_statistics iw_stats;
typedef u_char mac_addr[ETH_ALEN];	/* Hardware address */

#include "rayctl.h"
#include "ray_cs.h"


/** Prototypes based on PCMCIA skeleton driver *******************************/
static int ray_config(struct pcmcia_device *link);
static void ray_release(struct pcmcia_device *link);
static void ray_detach(struct pcmcia_device *p_dev);

/***** Prototypes indicated by device structure ******************************/
static int ray_dev_close(struct net_device *dev);
static int ray_dev_config(struct net_device *dev, struct ifmap *map);
static struct net_device_stats *ray_get_stats(struct net_device *dev);
static int ray_dev_init(struct net_device *dev);

static int ray_open(struct net_device *dev);
static netdev_tx_t ray_dev_start_xmit(struct sk_buff *skb,
					    struct net_device *dev);
static void set_multicast_list(struct net_device *dev);
static void ray_update_multi_list(struct net_device *dev, int all);
static int translate_frame(ray_dev_t *local, struct tx_msg __iomem *ptx,
			   unsigned char *data, int len);
static void ray_build_header(ray_dev_t *local, struct tx_msg __iomem *ptx,
			     UCHAR msg_type, unsigned char *data);
static void untranslate(ray_dev_t *local, struct sk_buff *skb, int len);
static iw_stats *ray_get_wireless_stats(struct net_device *dev);
static const struct iw_handler_def ray_handler_def;

/***** Prototypes for raylink functions **************************************/
static void authenticate(ray_dev_t *local);
static int build_auth_frame(ray_dev_t *local, UCHAR *dest, int auth_type);
static void authenticate_timeout(struct timer_list *t);
static int get_free_ccs(ray_dev_t *local);
static int get_free_tx_ccs(ray_dev_t *local);
static void init_startup_params(ray_dev_t *local);
static int parse_addr(char *in_str, UCHAR *out);
static int ray_hw_xmit(unsigned char *data, int len, struct net_device *dev, UCHAR type);
static int ray_init(struct net_device *dev);
static int interrupt_ecf(ray_dev_t *local, int ccs);
static void ray_reset(struct net_device *dev);
static void ray_update_parm(struct net_device *dev, UCHAR objid, UCHAR *value, int len);
static void verify_dl_startup(struct timer_list *t);

/* Prototypes for interrpt time functions **********************************/
static irqreturn_t ray_interrupt(int reg, void *dev_id);
static void clear_interrupt(ray_dev_t *local);
static void rx_deauthenticate(ray_dev_t *local, struct rcs __iomem *prcs,
			      unsigned int pkt_addr, int rx_len);
static int copy_from_rx_buff(ray_dev_t *local, UCHAR *dest, int pkt_addr, int len);
static void ray_rx(struct net_device *dev, ray_dev_t *local, struct rcs __iomem *prcs);
static void release_frag_chain(ray_dev_t *local, struct rcs __iomem *prcs);
static void rx_authenticate(ray_dev_t *local, struct rcs __iomem *prcs,
			    unsigned int pkt_addr, int rx_len);
static void rx_data(struct net_device *dev, struct rcs __iomem *prcs,
		    unsigned int pkt_addr, int rx_len);
static void associate(ray_dev_t *local);

/* Card command functions */
static int dl_startup_params(struct net_device *dev);
static void join_net(struct timer_list *t);
static void start_net(struct timer_list *t);

/*===========================================================================*/
/* Parameters that can be set with 'insmod' */

/* ADHOC=0, Infrastructure=1 */
static int net_type = ADHOC;

/* Hop dwell time in Kus (1024 us units defined by 802.11) */
static int hop_dwell = 128;

/* Beacon period in Kus */
static int beacon_period = 256;

/* power save mode (0 = off, 1 = save power) */
static int psm;

/* String for network's Extended Service Set ID. 32 Characters max */
static char *essid;

/* Default to encapsulation unless translation requested */
static bool translate = true;

static int country = USA;

static int sniffer;

static int bc;

/* 48 bit physical card address if overriding card's real physical
 * address is required.  Since IEEE 802.11 addresses are 48 bits
 * like ethernet, an int can't be used, so a string is used. To
 * allow use of addresses starting with a decimal digit, the first
 * character must be a letter and will be ignored. This letter is
 * followed by up to 12 hex digits which are the address.  If less
 * than 12 digits are used, the address will be left filled with 0's.
 * Note that bit 0 of the first byte is the broadcast bit, and evil
 * things will happen if it is not 0 in a card address.
 */
static char *phy_addr = NULL;

static unsigned int ray_mem_speed = 500;

/* WARNING: THIS DRIVER IS NOT CAPABLE OF HANDLING MULTIPLE DEVICES! */
static struct pcmcia_device *this_device = NULL;

MODULE_AUTHOR("Corey Thomas <corey@world.std.com>");
MODULE_DESCRIPTION("Raylink/WebGear wireless LAN driver");
MODULE_LICENSE("GPL");

module_param(net_type, int, 0);
module_param(hop_dwell, int, 0);
module_param(beacon_period, int, 0);
module_param(psm, int, 0);
module_param(essid, charp, 0);
module_param(translate, bool, 0);
module_param(country, int, 0);
module_param(sniffer, int, 0);
module_param(bc, int, 0);
module_param(phy_addr, charp, 0);
module_param(ray_mem_speed, int, 0);

static const UCHAR b5_default_startup_parms[] = {
	0, 0,			/* Adhoc station */
	'L', 'I', 'N', 'U', 'X', 0, 0, 0,	/* 32 char ESSID */
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	1, 0,			/* Active scan, CA Mode */
	0, 0, 0, 0, 0, 0,	/* No default MAC addr  */
	0x7f, 0xff,		/* Frag threshold */
	0x00, 0x80,		/* Hop time 128 Kus */
	0x01, 0x00,		/* Beacon period 256 Kus */
	0x01, 0x07, 0xa3,	/* DTIM, retries, ack timeout */
	0x1d, 0x82, 0x4e,	/* SIFS, DIFS, PIFS */
	0x7f, 0xff,		/* RTS threshold */
	0x04, 0xe2, 0x38, 0xA4,	/* scan_dwell, max_scan_dwell */
	0x05,			/* assoc resp timeout thresh */
	0x08, 0x02, 0x08,	/* adhoc, infra, super cycle max */
	0,			/* Promiscuous mode */
	0x0c, 0x0bd,		/* Unique word */
	0x32,			/* Slot time */
	0xff, 0xff,		/* roam-low snr, low snr count */
	0x05, 0xff,		/* Infra, adhoc missed bcn thresh */
	0x01, 0x0b, 0x4f,	/* USA, hop pattern, hop pat length */
/* b4 - b5 differences start here */
	0x00, 0x3f,		/* CW max */
	0x00, 0x0f,		/* CW min */
	0x04, 0x08,		/* Noise gain, limit offset */
	0x28, 0x28,		/* det rssi, med busy offsets */
	7,			/* det sync thresh */
	0, 2, 2,		/* test mode, min, max */
	0,			/* allow broadcast SSID probe resp */
	0, 0,			/* privacy must start, can join */
	2, 0, 0, 0, 0, 0, 0, 0	/* basic rate set */
};

static const UCHAR b4_default_startup_parms[] = {
	0, 0,			/* Adhoc station */
	'L', 'I', 'N', 'U', 'X', 0, 0, 0,	/* 32 char ESSID */
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	1, 0,			/* Active scan, CA Mode */
	0, 0, 0, 0, 0, 0,	/* No default MAC addr  */
	0x7f, 0xff,		/* Frag threshold */
	0x02, 0x00,		/* Hop time */
	0x00, 0x01,		/* Beacon period */
	0x01, 0x07, 0xa3,	/* DTIM, retries, ack timeout */
	0x1d, 0x82, 0xce,	/* SIFS, DIFS, PIFS */
	0x7f, 0xff,		/* RTS threshold */
	0xfb, 0x1e, 0xc7, 0x5c,	/* scan_dwell, max_scan_dwell */
	0x05,			/* assoc resp timeout thresh */
	0x04, 0x02, 0x4,	/* adhoc, infra, super cycle max */
	0,			/* Promiscuous mode */
	0x0c, 0x0bd,		/* Unique word */
	0x4e,			/* Slot time (TBD seems wrong) */
	0xff, 0xff,		/* roam-low snr, low snr count */
	0x05, 0xff,		/* Infra, adhoc missed bcn thresh */
	0x01, 0x0b, 0x4e,	/* USA, hop pattern, hop pat length */
/* b4 - b5 differences start here */
	0x3f, 0x0f,		/* CW max, min */
	0x04, 0x08,		/* Noise gain, limit offset */
	0x28, 0x28,		/* det rssi, med busy offsets */
	7,			/* det sync thresh */
	0, 2, 2,		/* test mode, min, max */
	0,			/* rx/tx delay */
	0, 0, 0, 0, 0, 0,	/* current BSS id */
	0			/* hop set */
};

/*===========================================================================*/
static const u8 eth2_llc[] = { 0xaa, 0xaa, 3, 0, 0, 0 };

static const char hop_pattern_length[] = { 1,
	USA_HOP_MOD, EUROPE_HOP_MOD,
	JAPAN_HOP_MOD, KOREA_HOP_MOD,
	SPAIN_HOP_MOD, FRANCE_HOP_MOD,
	ISRAEL_HOP_MOD, AUSTRALIA_HOP_MOD,
	JAPAN_TEST_HOP_MOD
};

static const char rcsid[] =
    "Raylink/WebGear wireless LAN - Corey <Thomas corey@world.std.com>";

static const struct net_device_ops ray_netdev_ops = {
	.ndo_init 		= ray_dev_init,
	.ndo_open 		= ray_open,
	.ndo_stop 		= ray_dev_close,
	.ndo_start_xmit		= ray_dev_start_xmit,
	.ndo_set_config		= ray_dev_config,
	.ndo_get_stats		= ray_get_stats,
	.ndo_set_rx_mode	= set_multicast_list,
	.ndo_set_mac_address 	= eth_mac_addr,
	.ndo_validate_addr	= eth_validate_addr,
};

static int ray_probe(struct pcmcia_device *p_dev)
{
	ray_dev_t *local;
	struct net_device *dev;

	dev_dbg(&p_dev->dev, "ray_attach()\n");

	/* Allocate space for private device-specific data */
	dev = alloc_etherdev(sizeof(ray_dev_t));
	if (!dev)
		goto fail_alloc_dev;

	local = netdev_priv(dev);
	local->finder = p_dev;

	/* The io structure describes IO port mapping. None used here */
	p_dev->resource[0]->end = 0;
	p_dev->resource[0]->flags |= IO_DATA_PATH_WIDTH_8;

	/* General socket configuration */
	p_dev->config_flags |= CONF_ENABLE_IRQ;
	p_dev->config_index = 1;

	p_dev->priv = dev;

	local->finder = p_dev;
	local->card_status = CARD_INSERTED;
	local->authentication_state = UNAUTHENTICATED;
	local->num_multi = 0;
	dev_dbg(&p_dev->dev, "ray_attach p_dev = %p,  dev = %p,  local = %p, intr = %p\n",
	      p_dev, dev, local, &ray_interrupt);

	/* Raylink entries in the device structure */
	dev->netdev_ops = &ray_netdev_ops;
	dev->wireless_handlers = &ray_handler_def;
#ifdef WIRELESS_SPY
	local->wireless_data.spy_data = &local->spy_data;
	dev->wireless_data = &local->wireless_data;
#endif /* WIRELESS_SPY */


	dev_dbg(&p_dev->dev, "ray_cs ray_attach calling ether_setup.)\n");
	netif_stop_queue(dev);

	timer_setup(&local->timer, NULL, 0);

	this_device = p_dev;
	return ray_config(p_dev);

fail_alloc_dev:
	return -ENOMEM;
} /* ray_attach */

static void ray_detach(struct pcmcia_device *link)
{
	struct net_device *dev;
	ray_dev_t *local;

	dev_dbg(&link->dev, "ray_detach\n");

	this_device = NULL;
	dev = link->priv;

	ray_release(link);

	local = netdev_priv(dev);
	del_timer_sync(&local->timer);

	if (link->priv) {
		unregister_netdev(dev);
		free_netdev(dev);
	}
	dev_dbg(&link->dev, "ray_cs ray_detach ending\n");
} /* ray_detach */

#define MAX_TUPLE_SIZE 128
static int ray_config(struct pcmcia_device *link)
{
	int ret = 0;
	int i;
	struct net_device *dev = (struct net_device *)link->priv;
	ray_dev_t *local = netdev_priv(dev);

	dev_dbg(&link->dev, "ray_config\n");

	/* Determine card type and firmware version */
	printk(KERN_INFO "ray_cs Detected: %s%s%s%s\n",
	       link->prod_id[0] ? link->prod_id[0] : " ",
	       link->prod_id[1] ? link->prod_id[1] : " ",
	       link->prod_id[2] ? link->prod_id[2] : " ",
	       link->prod_id[3] ? link->prod_id[3] : " ");

	/* Now allocate an interrupt line.  Note that this does not
	   actually assign a handler to the interrupt.
	 */
	ret = pcmcia_request_irq(link, ray_interrupt);
	if (ret)
		goto failed;
	dev->irq = link->irq;

	ret = pcmcia_enable_device(link);
	if (ret)
		goto failed;

/*** Set up 32k window for shared memory (transmit and control) ************/
	link->resource[2]->flags |= WIN_DATA_WIDTH_8 | WIN_MEMORY_TYPE_CM | WIN_ENABLE | WIN_USE_WAIT;
	link->resource[2]->start = 0;
	link->resource[2]->end = 0x8000;
	ret = pcmcia_request_window(link, link->resource[2], ray_mem_speed);
	if (ret)
		goto failed;
	ret = pcmcia_map_mem_page(link, link->resource[2], 0);
	if (ret)
		goto failed;
	local->sram = ioremap(link->resource[2]->start,
			resource_size(link->resource[2]));
	if (!local->sram)
		goto failed;

/*** Set up 16k window for shared memory (receive buffer) ***************/
	link->resource[3]->flags |=
	    WIN_DATA_WIDTH_8 | WIN_MEMORY_TYPE_CM | WIN_ENABLE | WIN_USE_WAIT;
	link->resource[3]->start = 0;
	link->resource[3]->end = 0x4000;
	ret = pcmcia_request_window(link, link->resource[3], ray_mem_speed);
	if (ret)
		goto failed;
	ret = pcmcia_map_mem_page(link, link->resource[3], 0x8000);
	if (ret)
		goto failed;
	local->rmem = ioremap(link->resource[3]->start,
			resource_size(link->resource[3]));
	if (!local->rmem)
		goto failed;

/*** Set up window for attribute memory ***********************************/
	link->resource[4]->flags |=
	    WIN_DATA_WIDTH_8 | WIN_MEMORY_TYPE_AM | WIN_ENABLE | WIN_USE_WAIT;
	link->resource[4]->start = 0;
	link->resource[4]->end = 0x1000;
	ret = pcmcia_request_window(link, link->resource[4], ray_mem_speed);
	if (ret)
		goto failed;
	ret = pcmcia_map_mem_page(link, link->resource[4], 0);
	if (ret)
		goto failed;
	local->amem = ioremap(link->resource[4]->start,
			resource_size(link->resource[4]));
	if (!local->amem)
		goto failed;

	dev_dbg(&link->dev, "ray_config sram=%p\n", local->sram);
	dev_dbg(&link->dev, "ray_config rmem=%p\n", local->rmem);
	dev_dbg(&link->dev, "ray_config amem=%p\n", local->amem);
	if (ray_init(dev) < 0) {
		ray_release(link);
		return -ENODEV;
	}

	SET_NETDEV_DEV(dev, &link->dev);
	i = register_netdev(dev);
	if (i != 0) {
		printk("ray_config register_netdev() failed\n");
		ray_release(link);
		return i;
	}

	printk(KERN_INFO "%s: RayLink, irq %d, hw_addr %pM\n",
	       dev->name, dev->irq, dev->dev_addr);

	return 0;

failed:
	ray_release(link);
	return -ENODEV;
} /* ray_config */

static inline struct ccs __iomem *ccs_base(ray_dev_t *dev)
{
	return dev->sram + CCS_BASE;
}

static inline struct rcs __iomem *rcs_base(ray_dev_t *dev)
{
	/*
	 * This looks nonsensical, since there is a separate
	 * RCS_BASE. But the difference between a "struct rcs"
	 * and a "struct ccs" ends up being in the _index_ off
	 * the base, so the base pointer is the same for both
	 * ccs/rcs.
	 */
	return dev->sram + CCS_BASE;
}

/*===========================================================================*/
static int ray_init(struct net_device *dev)
{
	int i;
	struct ccs __iomem *pccs;
	ray_dev_t *local = netdev_priv(dev);
	struct pcmcia_device *link = local->finder;
	dev_dbg(&link->dev, "ray_init(0x%p)\n", dev);
	if (!(pcmcia_dev_present(link))) {
		dev_dbg(&link->dev, "ray_init - device not present\n");
		return -1;
	}

	local->net_type = net_type;
	local->sta_type = TYPE_STA;

	/* Copy the startup results to local memory */
	memcpy_fromio(&local->startup_res, local->sram + ECF_TO_HOST_BASE,
		      sizeof(struct startup_res_6));

	/* Check Power up test status and get mac address from card */
	if (local->startup_res.startup_word != 0x80) {
		printk(KERN_INFO "ray_init ERROR card status = %2x\n",
		       local->startup_res.startup_word);
		local->card_status = CARD_INIT_ERROR;
		return -1;
	}

	local->fw_ver = local->startup_res.firmware_version[0];
	local->fw_bld = local->startup_res.firmware_version[1];
	local->fw_var = local->startup_res.firmware_version[2];
	dev_dbg(&link->dev, "ray_init firmware version %d.%d\n", local->fw_ver,
	      local->fw_bld);

	local->tib_length = 0x20;
	if ((local->fw_ver == 5) && (local->fw_bld >= 30))
		local->tib_length = local->startup_res.tib_length;
	dev_dbg(&link->dev, "ray_init tib_length = 0x%02x\n", local->tib_length);
	/* Initialize CCS's to buffer free state */
	pccs = ccs_base(local);
	for (i = 0; i < NUMBER_OF_CCS; i++) {
		writeb(CCS_BUFFER_FREE, &(pccs++)->buffer_status);
	}
	init_startup_params(local);

	/* copy mac address to startup parameters */
	if (!parse_addr(phy_addr, local->sparm.b4.a_mac_addr)) {
		memcpy(&local->sparm.b4.a_mac_addr,
		       &local->startup_res.station_addr, ADDRLEN);
	}

	clear_interrupt(local);	/* Clear any interrupt from the card */
	local->card_status = CARD_AWAITING_PARAM;
	dev_dbg(&link->dev, "ray_init ending\n");
	return 0;
} /* ray_init */

/*===========================================================================*/
/* Download startup parameters to the card and command it to read them       */
static int dl_startup_params(struct net_device *dev)
{
	int ccsindex;
	ray_dev_t *local = netdev_priv(dev);
	struct ccs __iomem *pccs;
	struct pcmcia_device *link = local->finder;

	dev_dbg(&link->dev, "dl_startup_params entered\n");
	if (!(pcmcia_dev_present(link))) {
		dev_dbg(&link->dev, "ray_cs dl_startup_params - device not present\n");
		return -1;
	}

	/* Copy parameters to host to ECF area */
	if (local->fw_ver == 0x55)
		memcpy_toio(local->sram + HOST_TO_ECF_BASE, &local->sparm.b4,
			    sizeof(struct b4_startup_params));
	else
		memcpy_toio(local->sram + HOST_TO_ECF_BASE, &local->sparm.b5,
			    sizeof(struct b5_startup_params));

	/* Fill in the CCS fields for the ECF */
	if ((ccsindex = get_free_ccs(local)) < 0)
		return -1;
	local->dl_param_ccs = ccsindex;
	pccs = ccs_base(local) + ccsindex;
	writeb(CCS_DOWNLOAD_STARTUP_PARAMS, &pccs->cmd);
	dev_dbg(&link->dev, "dl_startup_params start ccsindex = %d\n",
	      local->dl_param_ccs);
	/* Interrupt the firmware to process the command */
	if (interrupt_ecf(local, ccsindex)) {
		printk(KERN_INFO "ray dl_startup_params failed - "
		       "ECF not ready for intr\n");
		local->card_status = CARD_DL_PARAM_ERROR;
		writeb(CCS_BUFFER_FREE, &(pccs++)->buffer_status);
		return -2;
	}
	local->card_status = CARD_DL_PARAM;
	/* Start kernel timer to wait for dl startup to complete. */
	local->timer.expires = jiffies + HZ / 2;
	local->timer.function = verify_dl_startup;
	add_timer(&local->timer);
	dev_dbg(&link->dev,
	      "ray_cs dl_startup_params started timer for verify_dl_startup\n");
	return 0;
} /* dl_startup_params */

/*===========================================================================*/
static void init_startup_params(ray_dev_t *local)
{
	int i;

	if (country > JAPAN_TEST)
		country = USA;
	else if (country < USA)
		country = USA;
	/* structure for hop time and beacon period is defined here using
	 * New 802.11D6.1 format.  Card firmware is still using old format
	 * until version 6.
	 *    Before                    After
	 *    a_hop_time ms byte        a_hop_time ms byte
	 *    a_hop_time 2s byte        a_hop_time ls byte
	 *    a_hop_time ls byte        a_beacon_period ms byte
	 *    a_beacon_period           a_beacon_period ls byte
	 *
	 *    a_hop_time = uS           a_hop_time = KuS
	 *    a_beacon_period = hops    a_beacon_period = KuS
	 *//* 64ms = 010000 */
	if (local->fw_ver == 0x55) {
		memcpy(&local->sparm.b4, b4_default_startup_parms,
		       sizeof(struct b4_startup_params));
		/* Translate sane kus input values to old build 4/5 format */
		/* i = hop time in uS truncated to 3 bytes */
		i = (hop_dwell * 1024) & 0xffffff;
		local->sparm.b4.a_hop_time[0] = (i >> 16) & 0xff;
		local->sparm.b4.a_hop_time[1] = (i >> 8) & 0xff;
		local->sparm.b4.a_beacon_period[0] = 0;
		local->sparm.b4.a_beacon_period[1] =
		    ((beacon_period / hop_dwell) - 1) & 0xff;
		local->sparm.b4.a_curr_country_code = country;
		local->sparm.b4.a_hop_pattern_length =
		    hop_pattern_length[(int)country] - 1;
		if (bc) {
			local->sparm.b4.a_ack_timeout = 0x50;
			local->sparm.b4.a_sifs = 0x3f;
		}
	} else { /* Version 5 uses real kus values */
		memcpy((UCHAR *) &local->sparm.b5, b5_default_startup_parms,
		       sizeof(struct b5_startup_params));

		local->sparm.b5.a_hop_time[0] = (hop_dwell >> 8) & 0xff;
		local->sparm.b5.a_hop_time[1] = hop_dwell & 0xff;
		local->sparm.b5.a_beacon_period[0] =
		    (beacon_period >> 8) & 0xff;
		local->sparm.b5.a_beacon_period[1] = beacon_period & 0xff;
		if (psm)
			local->sparm.b5.a_power_mgt_state = 1;
		local->sparm.b5.a_curr_country_code = country;
		local->sparm.b5.a_hop_pattern_length =
		    hop_pattern_length[(int)country];
	}

	local->sparm.b4.a_network_type = net_type & 0x01;
	local->sparm.b4.a_acting_as_ap_status = TYPE_STA;

	if (essid != NULL)
		strncpy(local->sparm.b4.a_current_ess_id, essid, ESSID_SIZE);
} /* init_startup_params */

/*===========================================================================*/
static void verify_dl_startup(struct timer_list *t)
{
	ray_dev_t *local = from_timer(local, t, timer);
	struct ccs __iomem *pccs = ccs_base(local) + local->dl_param_ccs;
	UCHAR status;
	struct pcmcia_device *link = local->finder;

	if (!(pcmcia_dev_present(link))) {
		dev_dbg(&link->dev, "ray_cs verify_dl_startup - device not present\n");
		return;
	}
#if 0
	{
		int i;
		printk(KERN_DEBUG
		       "verify_dl_startup parameters sent via ccs %d:\n",
		       local->dl_param_ccs);
		for (i = 0; i < sizeof(struct b5_startup_params); i++) {
			printk(" %2x",
			       (unsigned int)readb(local->sram +
						   HOST_TO_ECF_BASE + i));
		}
		printk("\n");
	}
#endif

	status = readb(&pccs->buffer_status);
	if (status != CCS_BUFFER_FREE) {
		printk(KERN_INFO
		       "Download startup params failed.  Status = %d\n",
		       status);
		local->card_status = CARD_DL_PARAM_ERROR;
		return;
	}
	if (local->sparm.b4.a_network_type == ADHOC)
		start_net(&local->timer);
	else
		join_net(&local->timer);
} /* end verify_dl_startup */

/*===========================================================================*/
/* Command card to start a network */
static void start_net(struct timer_list *t)
{
	ray_dev_t *local = from_timer(local, t, timer);
	struct ccs __iomem *pccs;
	int ccsindex;
	struct pcmcia_device *link = local->finder;
	if (!(pcmcia_dev_present(link))) {
		dev_dbg(&link->dev, "ray_cs start_net - device not present\n");
		return;
	}
	/* Fill in the CCS fields for the ECF */
	if ((ccsindex = get_free_ccs(local)) < 0)
		return;
	pccs = ccs_base(local) + ccsindex;
	writeb(CCS_START_NETWORK, &pccs->cmd);
	writeb(0, &pccs->var.start_network.update_param);
	/* Interrupt the firmware to process the command */
	if (interrupt_ecf(local, ccsindex)) {
		dev_dbg(&link->dev, "ray start net failed - card not ready for intr\n");
		writeb(CCS_BUFFER_FREE, &(pccs++)->buffer_status);
		return;
	}
	local->card_status = CARD_DOING_ACQ;
} /* end start_net */

/*===========================================================================*/
/* Command card to join a network */
static void join_net(struct timer_list *t)
{
	ray_dev_t *local = from_timer(local, t, timer);

	struct ccs __iomem *pccs;
	int ccsindex;
	struct pcmcia_device *link = local->finder;

	if (!(pcmcia_dev_present(link))) {
		dev_dbg(&link->dev, "ray_cs join_net - device not present\n");
		return;
	}
	/* Fill in the CCS fields for the ECF */
	if ((ccsindex = get_free_ccs(local)) < 0)
		return;
	pccs = ccs_base(local) + ccsindex;
	writeb(CCS_JOIN_NETWORK, &pccs->cmd);
	writeb(0, &pccs->var.join_network.update_param);
	writeb(0, &pccs->var.join_network.net_initiated);
	/* Interrupt the firmware to process the command */
	if (interrupt_ecf(local, ccsindex)) {
		dev_dbg(&link->dev, "ray join net failed - card not ready for intr\n");
		writeb(CCS_BUFFER_FREE, &(pccs++)->buffer_status);
		return;
	}
	local->card_status = CARD_DOING_ACQ;
}


static void ray_release(struct pcmcia_device *link)
{
	struct net_device *dev = link->priv;
	ray_dev_t *local = netdev_priv(dev);

	dev_dbg(&link->dev, "ray_release\n");

	del_timer(&local->timer);

	iounmap(local->sram);
	iounmap(local->rmem);
	iounmap(local->amem);
	pcmcia_disable_device(link);

	dev_dbg(&link->dev, "ray_release ending\n");
}

static int ray_suspend(struct pcmcia_device *link)
{
	struct net_device *dev = link->priv;

	if (link->open)
		netif_device_detach(dev);

	return 0;
}

static int ray_resume(struct pcmcia_device *link)
{
	struct net_device *dev = link->priv;

	if (link->open) {
		ray_reset(dev);
		netif_device_attach(dev);
	}

	return 0;
}

/*===========================================================================*/
static int ray_dev_init(struct net_device *dev)
{
#ifdef RAY_IMMEDIATE_INIT
	int i;
#endif /* RAY_IMMEDIATE_INIT */
	ray_dev_t *local = netdev_priv(dev);
	struct pcmcia_device *link = local->finder;

	dev_dbg(&link->dev, "ray_dev_init(dev=%p)\n", dev);
	if (!(pcmcia_dev_present(link))) {
		dev_dbg(&link->dev, "ray_dev_init - device not present\n");
		return -1;
	}
#ifdef RAY_IMMEDIATE_INIT
	/* Download startup parameters */
	if ((i = dl_startup_params(dev)) < 0) {
		printk(KERN_INFO "ray_dev_init dl_startup_params failed - "
		       "returns 0x%x\n", i);
		return -1;
	}
#else /* RAY_IMMEDIATE_INIT */
	/* Postpone the card init so that we can still configure the card,
	 * for example using the Wireless Extensions. The init will happen
	 * in ray_open() - Jean II */
	dev_dbg(&link->dev,
	      "ray_dev_init: postponing card init to ray_open() ; Status = %d\n",
	      local->card_status);
#endif /* RAY_IMMEDIATE_INIT */

	/* copy mac and broadcast addresses to linux device */
	eth_hw_addr_set(dev, local->sparm.b4.a_mac_addr);
	eth_broadcast_addr(dev->broadcast);

	dev_dbg(&link->dev, "ray_dev_init ending\n");
	return 0;
}

/*===========================================================================*/
static int ray_dev_config(struct net_device *dev, struct ifmap *map)
{
	ray_dev_t *local = netdev_priv(dev);
	struct pcmcia_device *link = local->finder;
	/* Dummy routine to satisfy device structure */
	dev_dbg(&link->dev, "ray_dev_config(dev=%p,ifmap=%p)\n", dev, map);
	if (!(pcmcia_dev_present(link))) {
		dev_dbg(&link->dev, "ray_dev_config - device not present\n");
		return -1;
	}

	return 0;
}

/*===========================================================================*/
static netdev_tx_t ray_dev_start_xmit(struct sk_buff *skb,
					    struct net_device *dev)
{
	ray_dev_t *local = netdev_priv(dev);
	struct pcmcia_device *link = local->finder;
	short length = skb->len;

	if (!pcmcia_dev_present(link)) {
		dev_dbg(&link->dev, "ray_dev_start_xmit - device not present\n");
		dev_kfree_skb(skb);
		return NETDEV_TX_OK;
	}

	dev_dbg(&link->dev, "ray_dev_start_xmit(skb=%p, dev=%p)\n", skb, dev);
	if (local->authentication_state == NEED_TO_AUTH) {
		dev_dbg(&link->dev, "ray_cs Sending authentication request.\n");
		if (!build_auth_frame(local, local->auth_id, OPEN_AUTH_REQUEST)) {
			local->authentication_state = AUTHENTICATED;
			netif_stop_queue(dev);
			return NETDEV_TX_BUSY;
		}
	}

	if (length < ETH_ZLEN) {
		if (skb_padto(skb, ETH_ZLEN))
			return NETDEV_TX_OK;
		length = ETH_ZLEN;
	}
	switch (ray_hw_xmit(skb->data, length, dev, DATA_TYPE)) {
	case XMIT_NO_CCS:
	case XMIT_NEED_AUTH:
		netif_stop_queue(dev);
		return NETDEV_TX_BUSY;
	case XMIT_NO_INTR:
	case XMIT_MSG_BAD:
	case XMIT_OK:
	default:
		dev_kfree_skb(skb);
	}

	return NETDEV_TX_OK;
} /* ray_dev_start_xmit */

/*===========================================================================*/
static int ray_hw_xmit(unsigned char *data, int len, struct net_device *dev,
		       UCHAR msg_type)
{
	ray_dev_t *local = netdev_priv(dev);
	struct ccs __iomem *pccs;
	int ccsindex;
	int offset;
	struct tx_msg __iomem *ptx;	/* Address of xmit buffer in PC space */
	short int addr;		/* Address of xmit buffer in card space */

	pr_debug("ray_hw_xmit(data=%p, len=%d, dev=%p)\n", data, len, dev);
	if (len + TX_HEADER_LENGTH > TX_BUF_SIZE) {
		printk(KERN_INFO "ray_hw_xmit packet too large: %d bytes\n",
		       len);
		return XMIT_MSG_BAD;
	}
	switch (ccsindex = get_free_tx_ccs(local)) {
	case ECCSBUSY:
		pr_debug("ray_hw_xmit tx_ccs table busy\n");
		fallthrough;
	case ECCSFULL:
		pr_debug("ray_hw_xmit No free tx ccs\n");
		fallthrough;
	case ECARDGONE:
		netif_stop_queue(dev);
		return XMIT_NO_CCS;
	default:
		break;
	}
	addr = TX_BUF_BASE + (ccsindex << 11);

	if (msg_type == DATA_TYPE) {
		local->stats.tx_bytes += len;
		local->stats.tx_packets++;
	}

	ptx = local->sram + addr;

	ray_build_header(local, ptx, msg_type, data);
	if (translate) {
		offset = translate_frame(local, ptx, data, len);
	} else { /* Encapsulate frame */
		/* TBD TIB length will move address of ptx->var */
		memcpy_toio(&ptx->var, data, len);
		offset = 0;
	}

	/* fill in the CCS */
	pccs = ccs_base(local) + ccsindex;
	len += TX_HEADER_LENGTH + offset;
	writeb(CCS_TX_REQUEST, &pccs->cmd);
	writeb(addr >> 8, &pccs->var.tx_request.tx_data_ptr[0]);
	writeb(local->tib_length, &pccs->var.tx_request.tx_data_ptr[1]);
	writeb(len >> 8, &pccs->var.tx_request.tx_data_length[0]);
	writeb(len & 0xff, &pccs->var.tx_request.tx_data_length[1]);
/* TBD still need psm_cam? */
	writeb(PSM_CAM, &pccs->var.tx_request.pow_sav_mode);
	writeb(local->net_default_tx_rate, &pccs->var.tx_request.tx_rate);
	writeb(0, &pccs->var.tx_request.antenna);
	pr_debug("ray_hw_xmit default_tx_rate = 0x%x\n",
	      local->net_default_tx_rate);

	/* Interrupt the firmware to process the command */
	if (interrupt_ecf(local, ccsindex)) {
		pr_debug("ray_hw_xmit failed - ECF not ready for intr\n");
/* TBD very inefficient to copy packet to buffer, and then not
   send it, but the alternative is to queue the messages and that
   won't be done for a while.  Maybe set tbusy until a CCS is free?
*/
		writeb(CCS_BUFFER_FREE, &pccs->buffer_status);
		return XMIT_NO_INTR;
	}
	return XMIT_OK;
} /* end ray_hw_xmit */

/*===========================================================================*/
static int translate_frame(ray_dev_t *local, struct tx_msg __iomem *ptx,
			   unsigned char *data, int len)
{
	__be16 proto = ((struct ethhdr *)data)->h_proto;
	if (ntohs(proto) >= ETH_P_802_3_MIN) { /* DIX II ethernet frame */
		pr_debug("ray_cs translate_frame DIX II\n");
		/* Copy LLC header to card buffer */
		memcpy_toio(&ptx->var, eth2_llc, sizeof(eth2_llc));
		memcpy_toio(((void __iomem *)&ptx->var) + sizeof(eth2_llc),
			    (UCHAR *) &proto, 2);
		if (proto == htons(ETH_P_AARP) || proto == htons(ETH_P_IPX)) {
			/* This is the selective translation table, only 2 entries */
			writeb(0xf8,
			       &((struct snaphdr_t __iomem *)ptx->var)->org[2]);
		}
		/* Copy body of ethernet packet without ethernet header */
		memcpy_toio((void __iomem *)&ptx->var +
			    sizeof(struct snaphdr_t), data + ETH_HLEN,
			    len - ETH_HLEN);
		return (int)sizeof(struct snaphdr_t) - ETH_HLEN;
	} else { /* already  802 type, and proto is length */
		pr_debug("ray_cs translate_frame 802\n");
		if (proto == htons(0xffff)) { /* evil netware IPX 802.3 without LLC */
			pr_debug("ray_cs translate_frame evil IPX\n");
			memcpy_toio(&ptx->var, data + ETH_HLEN, len - ETH_HLEN);
			return 0 - ETH_HLEN;
		}
		memcpy_toio(&ptx->var, data + ETH_HLEN, len - ETH_HLEN);
		return 0 - ETH_HLEN;
	}
	/* TBD do other frame types */
} /* end translate_frame */

/*===========================================================================*/
static void ray_build_header(ray_dev_t *local, struct tx_msg __iomem *ptx,
			     UCHAR msg_type, unsigned char *data)
{
	writeb(PROTOCOL_VER | msg_type, &ptx->mac.frame_ctl_1);
/*** IEEE 802.11 Address field assignments *************
		TODS	FROMDS	addr_1		addr_2		addr_3	addr_4
Adhoc		0	0	dest		src (terminal)	BSSID	N/A
AP to Terminal	0	1	dest		AP(BSSID)	source	N/A
Terminal to AP	1	0	AP(BSSID)	src (terminal)	dest	N/A
AP to AP	1	1	dest AP		src AP		dest	source
*******************************************************/
	if (local->net_type == ADHOC) {
		writeb(0, &ptx->mac.frame_ctl_2);
		memcpy_toio(ptx->mac.addr_1, ((struct ethhdr *)data)->h_dest,
			    ADDRLEN);
		memcpy_toio(ptx->mac.addr_2, ((struct ethhdr *)data)->h_source,
			    ADDRLEN);
		memcpy_toio(ptx->mac.addr_3, local->bss_id, ADDRLEN);
	} else { /* infrastructure */

		if (local->sparm.b4.a_acting_as_ap_status) {
			writeb(FC2_FROM_DS, &ptx->mac.frame_ctl_2);
			memcpy_toio(ptx->mac.addr_1,
				    ((struct ethhdr *)data)->h_dest, ADDRLEN);
			memcpy_toio(ptx->mac.addr_2, local->bss_id, 6);
			memcpy_toio(ptx->mac.addr_3,
				    ((struct ethhdr *)data)->h_source, ADDRLEN);
		} else { /* Terminal */

			writeb(FC2_TO_DS, &ptx->mac.frame_ctl_2);
			memcpy_toio(ptx->mac.addr_1, local->bss_id, ADDRLEN);
			memcpy_toio(ptx->mac.addr_2,
				    ((struct ethhdr *)data)->h_source, ADDRLEN);
			memcpy_toio(ptx->mac.addr_3,
				    ((struct ethhdr *)data)->h_dest, ADDRLEN);
		}
	}
} /* end encapsulate_frame */

/*====================================================================*/

/*------------------------------------------------------------------*/
/*
 * Wireless Handler : get protocol name
 */
static int ray_get_name(struct net_device *dev, struct iw_request_info *info,
			union iwreq_data *wrqu, char *extra)
{
	strcpy(wrqu->name, "IEEE 802.11-FH");
	return 0;
}

/*------------------------------------------------------------------*/
/*
 * Wireless Handler : set frequency
 */
static int ray_set_freq(struct net_device *dev, struct iw_request_info *info,
			union iwreq_data *wrqu, char *extra)
{
	ray_dev_t *local = netdev_priv(dev);
	int err = -EINPROGRESS;	/* Call commit handler */

	/* Reject if card is already initialised */
	if (local->card_status != CARD_AWAITING_PARAM)
		return -EBUSY;

	/* Setting by channel number */
	if ((wrqu->freq.m > USA_HOP_MOD) || (wrqu->freq.e > 0))
		err = -EOPNOTSUPP;
	else
		local->sparm.b5.a_hop_pattern = wrqu->freq.m;

	return err;
}

/*------------------------------------------------------------------*/
/*
 * Wireless Handler : get frequency
 */
static int ray_get_freq(struct net_device *dev, struct iw_request_info *info,
			union iwreq_data *wrqu, char *extra)
{
	ray_dev_t *local = netdev_priv(dev);

	wrqu->freq.m = local->sparm.b5.a_hop_pattern;
	wrqu->freq.e = 0;
	return 0;
}

/*------------------------------------------------------------------*/
/*
 * Wireless Handler : set ESSID
 */
static int ray_set_essid(struct net_device *dev, struct iw_request_info *info,
			 union iwreq_data *wrqu, char *extra)
{
	ray_dev_t *local = netdev_priv(dev);

	/* Reject if card is already initialised */
	if (local->card_status != CARD_AWAITING_PARAM)
		return -EBUSY;

	/* Check if we asked for `any' */
	if (wrqu->essid.flags == 0)
		/* Corey : can you do that ? */
		return -EOPNOTSUPP;

	/* Check the size of the string */
	if (wrqu->essid.length > IW_ESSID_MAX_SIZE)
		return -E2BIG;

	/* Set the ESSID in the card */
	memset(local->sparm.b5.a_current_ess_id, 0, IW_ESSID_MAX_SIZE);
	memcpy(local->sparm.b5.a_current_ess_id, extra, wrqu->essid.length);

	return -EINPROGRESS;	/* Call commit handler */
}

/*------------------------------------------------------------------*/
/*
 * Wireless Handler : get ESSID
 */
static int ray_get_essid(struct net_device *dev, struct iw_request_info *info,
			 union iwreq_data *wrqu, char *extra)
{
	ray_dev_t *local = netdev_priv(dev);
	UCHAR tmp[IW_ESSID_MAX_SIZE + 1];

	/* Get the essid that was set */
	memcpy(extra, local->sparm.b5.a_current_ess_id, IW_ESSID_MAX_SIZE);
	memcpy(tmp, local->sparm.b5.a_current_ess_id, IW_ESSID_MAX_SIZE);
	tmp[IW_ESSID_MAX_SIZE] = '\0';

	/* Push it out ! */
	wrqu->essid.length = strlen(tmp);
	wrqu->essid.flags = 1;	/* active */

	return 0;
}

/*------------------------------------------------------------------*/
/*
 * Wireless Handler : get AP address
 */
static int ray_get_wap(struct net_device *dev, struct iw_request_info *info,
		       union iwreq_data *wrqu, char *extra)
{
	ray_dev_t *local = netdev_priv(dev);

	memcpy(wrqu->ap_addr.sa_data, local->bss_id, ETH_ALEN);
	wrqu->ap_addr.sa_family = ARPHRD_ETHER;

	return 0;
}

/*------------------------------------------------------------------*/
/*
 * Wireless Handler : set Bit-Rate
 */
static int ray_set_rate(struct net_device *dev, struct iw_request_info *info,
			union iwreq_data *wrqu, char *extra)
{
	ray_dev_t *local = netdev_priv(dev);

	/* Reject if card is already initialised */
	if (local->card_status != CARD_AWAITING_PARAM)
		return -EBUSY;

	/* Check if rate is in range */
	if ((wrqu->bitrate.value != 1000000) && (wrqu->bitrate.value != 2000000))
		return -EINVAL;

	/* Hack for 1.5 Mb/s instead of 2 Mb/s */
	if ((local->fw_ver == 0x55) &&	/* Please check */
	    (wrqu->bitrate.value == 2000000))
		local->net_default_tx_rate = 3;
	else
		local->net_default_tx_rate = wrqu->bitrate.value / 500000;

	return 0;
}

/*------------------------------------------------------------------*/
/*
 * Wireless Handler : get Bit-Rate
 */
static int ray_get_rate(struct net_device *dev, struct iw_request_info *info,
			union iwreq_data *wrqu, char *extra)
{
	ray_dev_t *local = netdev_priv(dev);

	if (local->net_default_tx_rate == 3)
		wrqu->bitrate.value = 2000000;	/* Hum... */
	else
		wrqu->bitrate.value = local->net_default_tx_rate * 500000;
	wrqu->bitrate.fixed = 0;	/* We are in auto mode */

	return 0;
}

/*------------------------------------------------------------------*/
/*
 * Wireless Handler : set RTS threshold
 */
static int ray_set_rts(struct net_device *dev, struct iw_request_info *info,
		       union iwreq_data *wrqu, char *extra)
{
	ray_dev_t *local = netdev_priv(dev);
	int rthr = wrqu->rts.value;

	/* Reject if card is already initialised */
	if (local->card_status != CARD_AWAITING_PARAM)
		return -EBUSY;

	/* if(wrq->u.rts.fixed == 0) we should complain */
	if (wrqu->rts.disabled)
		rthr = 32767;
	else {
		if ((rthr < 0) || (rthr > 2347))   /* What's the max packet size ??? */
			return -EINVAL;
	}
	local->sparm.b5.a_rts_threshold[0] = (rthr >> 8) & 0xFF;
	local->sparm.b5.a_rts_threshold[1] = rthr & 0xFF;

	return -EINPROGRESS;	/* Call commit handler */
}

/*------------------------------------------------------------------*/
/*
 * Wireless Handler : get RTS threshold
 */
static int ray_get_rts(struct net_device *dev, struct iw_request_info *info,
		       union iwreq_data *wrqu, char *extra)
{
	ray_dev_t *local = netdev_priv(dev);

	wrqu->rts.value = (local->sparm.b5.a_rts_threshold[0] << 8)
	    + local->sparm.b5.a_rts_threshold[1];
	wrqu->rts.disabled = (wrqu->rts.value == 32767);
	wrqu->rts.fixed = 1;

	return 0;
}

/*------------------------------------------------------------------*/
/*
 * Wireless Handler : set Fragmentation threshold
 */
static int ray_set_frag(struct net_device *dev, struct iw_request_info *info,
			union iwreq_data *wrqu, char *extra)
{
	ray_dev_t *local = netdev_priv(dev);
	int fthr = wrqu->frag.value;

	/* Reject if card is already initialised */
	if (local->card_status != CARD_AWAITING_PARAM)
		return -EBUSY;

	/* if(wrq->u.frag.fixed == 0) should complain */
	if (wrqu->frag.disabled)
		fthr = 32767;
	else {
		if ((fthr < 256) || (fthr > 2347))	/* To check out ! */
			return -EINVAL;
	}
	local->sparm.b5.a_frag_threshold[0] = (fthr >> 8) & 0xFF;
	local->sparm.b5.a_frag_threshold[1] = fthr & 0xFF;

	return -EINPROGRESS;	/* Call commit handler */
}

/*------------------------------------------------------------------*/
/*
 * Wireless Handler : get Fragmentation threshold
 */
static int ray_get_frag(struct net_device *dev, struct iw_request_info *info,
			union iwreq_data *wrqu, char *extra)
{
	ray_dev_t *local = netdev_priv(dev);

	wrqu->frag.value = (local->sparm.b5.a_frag_threshold[0] << 8)
	    + local->sparm.b5.a_frag_threshold[1];
	wrqu->frag.disabled = (wrqu->frag.value == 32767);
	wrqu->frag.fixed = 1;

	return 0;
}

/*------------------------------------------------------------------*/
/*
 * Wireless Handler : set Mode of Operation
 */
static int ray_set_mode(struct net_device *dev, struct iw_request_info *info,
			union iwreq_data *wrqu, char *extra)
{
	ray_dev_t *local = netdev_priv(dev);
	int err = -EINPROGRESS;	/* Call commit handler */
	char card_mode = 1;

	/* Reject if card is already initialised */
	if (local->card_status != CARD_AWAITING_PARAM)
		return -EBUSY;

	switch (wrqu->mode) {
	case IW_MODE_ADHOC:
		card_mode = 0;
		fallthrough;
	case IW_MODE_INFRA:
		local->sparm.b5.a_network_type = card_mode;
		break;
	default:
		err = -EINVAL;
	}

	return err;
}

/*------------------------------------------------------------------*/
/*
 * Wireless Handler : get Mode of Operation
 */
static int ray_get_mode(struct net_device *dev, struct iw_request_info *info,
			union iwreq_data *wrqu, char *extra)
{
	ray_dev_t *local = netdev_priv(dev);

	if (local->sparm.b5.a_network_type)
		wrqu->mode = IW_MODE_INFRA;
	else
		wrqu->mode = IW_MODE_ADHOC;

	return 0;
}

/*------------------------------------------------------------------*/
/*
 * Wireless Handler : get range info
 */
static int ray_get_range(struct net_device *dev, struct iw_request_info *info,
			 union iwreq_data *wrqu, char *extra)
{
	struct iw_range *range = (struct iw_range *)extra;

	memset(range, 0, sizeof(struct iw_range));

	/* Set the length (very important for backward compatibility) */
	wrqu->data.length = sizeof(struct iw_range);

	/* Set the Wireless Extension versions */
	range->we_version_compiled = WIRELESS_EXT;
	range->we_version_source = 9;

	/* Set information in the range struct */
	range->throughput = 1.1 * 1000 * 1000;	/* Put the right number here */
	range->num_channels = hop_pattern_length[(int)country];
	range->num_frequency = 0;
	range->max_qual.qual = 0;
	range->max_qual.level = 255;	/* What's the correct value ? */
	range->max_qual.noise = 255;	/* Idem */
	range->num_bitrates = 2;
	range->bitrate[0] = 1000000;	/* 1 Mb/s */
	range->bitrate[1] = 2000000;	/* 2 Mb/s */
	return 0;
}

/*------------------------------------------------------------------*/
/*
 * Wireless Private Handler : set framing mode
 */
static int ray_set_framing(struct net_device *dev, struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	translate = !!*(extra);	/* Set framing mode */

	return 0;
}

/*------------------------------------------------------------------*/
/*
 * Wireless Private Handler : get framing mode
 */
static int ray_get_framing(struct net_device *dev, struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	*(extra) = translate;

	return 0;
}

/*------------------------------------------------------------------*/
/*
 * Wireless Private Handler : get country
 */
static int ray_get_country(struct net_device *dev, struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	*(extra) = country;

	return 0;
}

/*------------------------------------------------------------------*/
/*
 * Commit handler : called after a bunch of SET operations
 */
static int ray_commit(struct net_device *dev, struct iw_request_info *info,
		      union iwreq_data *wrqu, char *extra)
{
	return 0;
}

/*------------------------------------------------------------------*/
/*
 * Stats handler : return Wireless Stats
 */
static iw_stats *ray_get_wireless_stats(struct net_device *dev)
{
	ray_dev_t *local = netdev_priv(dev);
	struct pcmcia_device *link = local->finder;
	struct status __iomem *p = local->sram + STATUS_BASE;

	local->wstats.status = local->card_status;
#ifdef WIRELESS_SPY
	if ((local->spy_data.spy_number > 0)
	    && (local->sparm.b5.a_network_type == 0)) {
		/* Get it from the first node in spy list */
		local->wstats.qual.qual = local->spy_data.spy_stat[0].qual;
		local->wstats.qual.level = local->spy_data.spy_stat[0].level;
		local->wstats.qual.noise = local->spy_data.spy_stat[0].noise;
		local->wstats.qual.updated =
		    local->spy_data.spy_stat[0].updated;
	}
#endif /* WIRELESS_SPY */

	if (pcmcia_dev_present(link)) {
		local->wstats.qual.noise = readb(&p->rxnoise);
		local->wstats.qual.updated |= 4;
	}

	return &local->wstats;
} /* end ray_get_wireless_stats */

/*------------------------------------------------------------------*/
/*
 * Structures to export the Wireless Handlers
 */

static const iw_handler ray_handler[] = {
	IW_HANDLER(SIOCSIWCOMMIT, ray_commit),
	IW_HANDLER(SIOCGIWNAME, ray_get_name),
	IW_HANDLER(SIOCSIWFREQ, ray_set_freq),
	IW_HANDLER(SIOCGIWFREQ, ray_get_freq),
	IW_HANDLER(SIOCSIWMODE, ray_set_mode),
	IW_HANDLER(SIOCGIWMODE, ray_get_mode),
	IW_HANDLER(SIOCGIWRANGE, ray_get_range),
#ifdef WIRELESS_SPY
	IW_HANDLER(SIOCSIWSPY, iw_handler_set_spy),
	IW_HANDLER(SIOCGIWSPY, iw_handler_get_spy),
	IW_HANDLER(SIOCSIWTHRSPY, iw_handler_set_thrspy),
	IW_HANDLER(SIOCGIWTHRSPY, iw_handler_get_thrspy),
#endif /* WIRELESS_SPY */
	IW_HANDLER(SIOCGIWAP, ray_get_wap),
	IW_HANDLER(SIOCSIWESSID, ray_set_essid),
	IW_HANDLER(SIOCGIWESSID, ray_get_essid),
	IW_HANDLER(SIOCSIWRATE, ray_set_rate),
	IW_HANDLER(SIOCGIWRATE, ray_get_rate),
	IW_HANDLER(SIOCSIWRTS, ray_set_rts),
	IW_HANDLER(SIOCGIWRTS, ray_get_rts),
	IW_HANDLER(SIOCSIWFRAG, ray_set_frag),
	IW_HANDLER(SIOCGIWFRAG, ray_get_frag),
};

#define SIOCSIPFRAMING	SIOCIWFIRSTPRIV	/* Set framing mode */
#define SIOCGIPFRAMING	SIOCIWFIRSTPRIV + 1	/* Get framing mode */
#define SIOCGIPCOUNTRY	SIOCIWFIRSTPRIV + 3	/* Get country code */

static const iw_handler ray_private_handler[] = {
	[0] = ray_set_framing,
	[1] = ray_get_framing,
	[3] = ray_get_country,
};

static const struct iw_priv_args ray_private_args[] = {
/* cmd,		set_args,	get_args,	name */
	{SIOCSIPFRAMING, IW_PRIV_TYPE_BYTE | IW_PRIV_SIZE_FIXED | 1, 0,
	 "set_framing"},
	{SIOCGIPFRAMING, 0, IW_PRIV_TYPE_BYTE | IW_PRIV_SIZE_FIXED | 1,
	 "get_framing"},
	{SIOCGIPCOUNTRY, 0, IW_PRIV_TYPE_BYTE | IW_PRIV_SIZE_FIXED | 1,
	 "get_country"},
};

static const struct iw_handler_def ray_handler_def = {
	.num_standard = ARRAY_SIZE(ray_handler),
	.num_private = ARRAY_SIZE(ray_private_handler),
	.num_private_args = ARRAY_SIZE(ray_private_args),
	.standard = ray_handler,
	.private = ray_private_handler,
	.private_args = ray_private_args,
	.get_wireless_stats = ray_get_wireless_stats,
};

/*===========================================================================*/
static int ray_open(struct net_device *dev)
{
	ray_dev_t *local = netdev_priv(dev);
	struct pcmcia_device *link;
	link = local->finder;

	dev_dbg(&link->dev, "ray_open('%s')\n", dev->name);

	if (link->open == 0)
		local->num_multi = 0;
	link->open++;

	/* If the card is not started, time to start it ! - Jean II */
	if (local->card_status == CARD_AWAITING_PARAM) {
		int i;

		dev_dbg(&link->dev, "ray_open: doing init now !\n");

		/* Download startup parameters */
		if ((i = dl_startup_params(dev)) < 0) {
			printk(KERN_INFO
			       "ray_dev_init dl_startup_params failed - "
			       "returns 0x%x\n", i);
			return -1;
		}
	}

	if (sniffer)
		netif_stop_queue(dev);
	else
		netif_start_queue(dev);

	dev_dbg(&link->dev, "ray_open ending\n");
	return 0;
} /* end ray_open */

/*===========================================================================*/
static int ray_dev_close(struct net_device *dev)
{
	ray_dev_t *local = netdev_priv(dev);
	struct pcmcia_device *link;
	link = local->finder;

	dev_dbg(&link->dev, "ray_dev_close('%s')\n", dev->name);

	link->open--;
	netif_stop_queue(dev);

	/* In here, we should stop the hardware (stop card from beeing active)
	 * and set local->card_status to CARD_AWAITING_PARAM, so that while the
	 * card is closed we can chage its configuration.
	 * Probably also need a COR reset to get sane state - Jean II */

	return 0;
} /* end ray_dev_close */

/*===========================================================================*/
static void ray_reset(struct net_device *dev)
{
	pr_debug("ray_reset entered\n");
}

/*===========================================================================*/
/* Cause a firmware interrupt if it is ready for one                         */
/* Return nonzero if not ready                                               */
static int interrupt_ecf(ray_dev_t *local, int ccs)
{
	int i = 50;
	struct pcmcia_device *link = local->finder;

	if (!(pcmcia_dev_present(link))) {
		dev_dbg(&link->dev, "ray_cs interrupt_ecf - device not present\n");
		return -1;
	}
	dev_dbg(&link->dev, "interrupt_ecf(local=%p, ccs = 0x%x\n", local, ccs);

	while (i &&
	       (readb(local->amem + CIS_OFFSET + ECF_INTR_OFFSET) &
		ECF_INTR_SET))
		i--;
	if (i == 0) {
		dev_dbg(&link->dev, "ray_cs interrupt_ecf card not ready for interrupt\n");
		return -1;
	}
	/* Fill the mailbox, then kick the card */
	writeb(ccs, local->sram + SCB_BASE);
	writeb(ECF_INTR_SET, local->amem + CIS_OFFSET + ECF_INTR_OFFSET);
	return 0;
} /* interrupt_ecf */

/*===========================================================================*/
/* Get next free transmit CCS                                                */
/* Return - index of current tx ccs                                          */
static int get_free_tx_ccs(ray_dev_t *local)
{
	int i;
	struct ccs __iomem *pccs = ccs_base(local);
	struct pcmcia_device *link = local->finder;

	if (!(pcmcia_dev_present(link))) {
		dev_dbg(&link->dev, "ray_cs get_free_tx_ccs - device not present\n");
		return ECARDGONE;
	}

	if (test_and_set_bit(0, &local->tx_ccs_lock)) {
		dev_dbg(&link->dev, "ray_cs tx_ccs_lock busy\n");
		return ECCSBUSY;
	}

	for (i = 0; i < NUMBER_OF_TX_CCS; i++) {
		if (readb(&(pccs + i)->buffer_status) == CCS_BUFFER_FREE) {
			writeb(CCS_BUFFER_BUSY, &(pccs + i)->buffer_status);
			writeb(CCS_END_LIST, &(pccs + i)->link);
			local->tx_ccs_lock = 0;
			return i;
		}
	}
	local->tx_ccs_lock = 0;
	dev_dbg(&link->dev, "ray_cs ERROR no free tx CCS for raylink card\n");
	return ECCSFULL;
} /* get_free_tx_ccs */

/*===========================================================================*/
/* Get next free CCS                                                         */
/* Return - index of current ccs                                             */
static int get_free_ccs(ray_dev_t *local)
{
	int i;
	struct ccs __iomem *pccs = ccs_base(local);
	struct pcmcia_device *link = local->finder;

	if (!(pcmcia_dev_present(link))) {
		dev_dbg(&link->dev, "ray_cs get_free_ccs - device not present\n");
		return ECARDGONE;
	}
	if (test_and_set_bit(0, &local->ccs_lock)) {
		dev_dbg(&link->dev, "ray_cs ccs_lock busy\n");
		return ECCSBUSY;
	}

	for (i = NUMBER_OF_TX_CCS; i < NUMBER_OF_CCS; i++) {
		if (readb(&(pccs + i)->buffer_status) == CCS_BUFFER_FREE) {
			writeb(CCS_BUFFER_BUSY, &(pccs + i)->buffer_status);
			writeb(CCS_END_LIST, &(pccs + i)->link);
			local->ccs_lock = 0;
			return i;
		}
	}
	local->ccs_lock = 0;
	dev_dbg(&link->dev, "ray_cs ERROR no free CCS for raylink card\n");
	return ECCSFULL;
} /* get_free_ccs */

/*===========================================================================*/
static void authenticate_timeout(struct timer_list *t)
{
	ray_dev_t *local = from_timer(local, t, timer);
	del_timer(&local->timer);
	printk(KERN_INFO "ray_cs Authentication with access point failed"
	       " - timeout\n");
	join_net(&local->timer);
}

/*===========================================================================*/
static int parse_addr(char *in_str, UCHAR *out)
{
	int len;
	int i, j, k;
	int status;

	if (in_str == NULL)
		return 0;
	if ((len = strlen(in_str)) < 2)
		return 0;
	memset(out, 0, ADDRLEN);

	status = 1;
	j = len - 1;
	if (j > 12)
		j = 12;
	i = 5;

	while (j > 0) {
		if ((k = hex_to_bin(in_str[j--])) != -1)
			out[i] = k;
		else
			return 0;

		if (j == 0)
			break;
		if ((k = hex_to_bin(in_str[j--])) != -1)
			out[i] += k << 4;
		else
			return 0;
		if (!i--)
			break;
	}
	return status;
}

/*===========================================================================*/
static struct net_device_stats *ray_get_stats(struct net_device *dev)
{
	ray_dev_t *local = netdev_priv(dev);
	struct pcmcia_device *link = local->finder;
	struct status __iomem *p = local->sram + STATUS_BASE;
	if (!(pcmcia_dev_present(link))) {
		dev_dbg(&link->dev, "ray_cs net_device_stats - device not present\n");
		return &local->stats;
	}
	if (readb(&p->mrx_overflow_for_host)) {
		local->stats.rx_over_errors += swab16(readw(&p->mrx_overflow));
		writeb(0, &p->mrx_overflow);
		writeb(0, &p->mrx_overflow_for_host);
	}
	if (readb(&p->mrx_checksum_error_for_host)) {
		local->stats.rx_crc_errors +=
		    swab16(readw(&p->mrx_checksum_error));
		writeb(0, &p->mrx_checksum_error);
		writeb(0, &p->mrx_checksum_error_for_host);
	}
	if (readb(&p->rx_hec_error_for_host)) {
		local->stats.rx_frame_errors += swab16(readw(&p->rx_hec_error));
		writeb(0, &p->rx_hec_error);
		writeb(0, &p->rx_hec_error_for_host);
	}
	return &local->stats;
}

/*===========================================================================*/
static void ray_update_parm(struct net_device *dev, UCHAR objid, UCHAR *value,
			    int len)
{
	ray_dev_t *local = netdev_priv(dev);
	struct pcmcia_device *link = local->finder;
	int ccsindex;
	int i;
	struct ccs __iomem *pccs;

	if (!(pcmcia_dev_present(link))) {
		dev_dbg(&link->dev, "ray_update_parm - device not present\n");
		return;
	}

	if ((ccsindex = get_free_ccs(local)) < 0) {
		dev_dbg(&link->dev, "ray_update_parm - No free ccs\n");
		return;
	}
	pccs = ccs_base(local) + ccsindex;
	writeb(CCS_UPDATE_PARAMS, &pccs->cmd);
	writeb(objid, &pccs->var.update_param.object_id);
	writeb(1, &pccs->var.update_param.number_objects);
	writeb(0, &pccs->var.update_param.failure_cause);
	for (i = 0; i < len; i++) {
		writeb(value[i], local->sram + HOST_TO_ECF_BASE);
	}
	/* Interrupt the firmware to process the command */
	if (interrupt_ecf(local, ccsindex)) {
		dev_dbg(&link->dev, "ray_cs associate failed - ECF not ready for intr\n");
		writeb(CCS_BUFFER_FREE, &(pccs++)->buffer_status);
	}
}

/*===========================================================================*/
static void ray_update_multi_list(struct net_device *dev, int all)
{
	int ccsindex;
	struct ccs __iomem *pccs;
	ray_dev_t *local = netdev_priv(dev);
	struct pcmcia_device *link = local->finder;
	void __iomem *p = local->sram + HOST_TO_ECF_BASE;

	if (!(pcmcia_dev_present(link))) {
		dev_dbg(&link->dev, "ray_update_multi_list - device not present\n");
		return;
	} else
		dev_dbg(&link->dev, "ray_update_multi_list(%p)\n", dev);
	if ((ccsindex = get_free_ccs(local)) < 0) {
		dev_dbg(&link->dev, "ray_update_multi - No free ccs\n");
		return;
	}
	pccs = ccs_base(local) + ccsindex;
	writeb(CCS_UPDATE_MULTICAST_LIST, &pccs->cmd);

	if (all) {
		writeb(0xff, &pccs->var);
		local->num_multi = 0xff;
	} else {
		struct netdev_hw_addr *ha;
		int i = 0;

		/* Copy the kernel's list of MC addresses to card */
		netdev_for_each_mc_addr(ha, dev) {
			memcpy_toio(p, ha->addr, ETH_ALEN);
			dev_dbg(&link->dev, "ray_update_multi add addr %pm\n",
				ha->addr);
			p += ETH_ALEN;
			i++;
		}
		if (i > 256 / ADDRLEN)
			i = 256 / ADDRLEN;
		writeb((UCHAR) i, &pccs->var);
		dev_dbg(&link->dev, "ray_cs update_multi %d addresses in list\n", i);
		/* Interrupt the firmware to process the command */
		local->num_multi = i;
	}
	if (interrupt_ecf(local, ccsindex)) {
		dev_dbg(&link->dev,
		      "ray_cs update_multi failed - ECF not ready for intr\n");
		writeb(CCS_BUFFER_FREE, &(pccs++)->buffer_status);
	}
} /* end ray_update_multi_list */

/*===========================================================================*/
static void set_multicast_list(struct net_device *dev)
{
	ray_dev_t *local = netdev_priv(dev);
	UCHAR promisc;

	pr_debug("ray_cs set_multicast_list(%p)\n", dev);

	if (dev->flags & IFF_PROMISC) {
		if (local->sparm.b5.a_promiscuous_mode == 0) {
			pr_debug("ray_cs set_multicast_list promisc on\n");
			local->sparm.b5.a_promiscuous_mode = 1;
			promisc = 1;
			ray_update_parm(dev, OBJID_promiscuous_mode,
					&promisc, sizeof(promisc));
		}
	} else {
		if (local->sparm.b5.a_promiscuous_mode == 1) {
			pr_debug("ray_cs set_multicast_list promisc off\n");
			local->sparm.b5.a_promiscuous_mode = 0;
			promisc = 0;
			ray_update_parm(dev, OBJID_promiscuous_mode,
					&promisc, sizeof(promisc));
		}
	}

	if (dev->flags & IFF_ALLMULTI)
		ray_update_multi_list(dev, 1);
	else {
		if (local->num_multi != netdev_mc_count(dev))
			ray_update_multi_list(dev, 0);
	}
} /* end set_multicast_list */

/*=============================================================================
 * All routines below here are run at interrupt time.
=============================================================================*/
static irqreturn_t ray_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = (struct net_device *)dev_id;
	struct pcmcia_device *link;
	ray_dev_t *local;
	struct ccs __iomem *pccs;
	struct rcs __iomem *prcs;
	UCHAR rcsindex;
	UCHAR tmp;
	UCHAR cmd;
	UCHAR status;
	UCHAR memtmp[ESSID_SIZE + 1];


	if (dev == NULL)	/* Note that we want interrupts with dev->start == 0 */
		return IRQ_NONE;

	pr_debug("ray_cs: interrupt for *dev=%p\n", dev);

	local = netdev_priv(dev);
	link = local->finder;
	if (!pcmcia_dev_present(link)) {
		pr_debug(
			"ray_cs interrupt from device not present or suspended.\n");
		return IRQ_NONE;
	}
	rcsindex = readb(&((struct scb __iomem *)(local->sram))->rcs_index);

	if (rcsindex >= (NUMBER_OF_CCS + NUMBER_OF_RCS)) {
		dev_dbg(&link->dev, "ray_cs interrupt bad rcsindex = 0x%x\n", rcsindex);
		clear_interrupt(local);
		return IRQ_HANDLED;
	}
	if (rcsindex < NUMBER_OF_CCS) { /* If it's a returned CCS */
		pccs = ccs_base(local) + rcsindex;
		cmd = readb(&pccs->cmd);
		status = readb(&pccs->buffer_status);
		switch (cmd) {
		case CCS_DOWNLOAD_STARTUP_PARAMS:	/* Happens in firmware someday */
			del_timer(&local->timer);
			if (status == CCS_COMMAND_COMPLETE) {
				dev_dbg(&link->dev,
				      "ray_cs interrupt download_startup_parameters OK\n");
			} else {
				dev_dbg(&link->dev,
				      "ray_cs interrupt download_startup_parameters fail\n");
			}
			break;
		case CCS_UPDATE_PARAMS:
			dev_dbg(&link->dev, "ray_cs interrupt update params done\n");
			if (status != CCS_COMMAND_COMPLETE) {
				tmp =
				    readb(&pccs->var.update_param.
					  failure_cause);
				dev_dbg(&link->dev,
				      "ray_cs interrupt update params failed - reason %d\n",
				      tmp);
			}
			break;
		case CCS_REPORT_PARAMS:
			dev_dbg(&link->dev, "ray_cs interrupt report params done\n");
			break;
		case CCS_UPDATE_MULTICAST_LIST:	/* Note that this CCS isn't returned */
			dev_dbg(&link->dev,
			      "ray_cs interrupt CCS Update Multicast List done\n");
			break;
		case CCS_UPDATE_POWER_SAVINGS_MODE:
			dev_dbg(&link->dev,
			      "ray_cs interrupt update power save mode done\n");
			break;
		case CCS_START_NETWORK:
		case CCS_JOIN_NETWORK:
			memcpy(memtmp, local->sparm.b4.a_current_ess_id,
								ESSID_SIZE);
			memtmp[ESSID_SIZE] = '\0';

			if (status == CCS_COMMAND_COMPLETE) {
				if (readb
				    (&pccs->var.start_network.net_initiated) ==
				    1) {
					dev_dbg(&link->dev,
					      "ray_cs interrupt network \"%s\" started\n",
					      memtmp);
				} else {
					dev_dbg(&link->dev,
					      "ray_cs interrupt network \"%s\" joined\n",
					      memtmp);
				}
				memcpy_fromio(&local->bss_id,
					      pccs->var.start_network.bssid,
					      ADDRLEN);

				if (local->fw_ver == 0x55)
					local->net_default_tx_rate = 3;
				else
					local->net_default_tx_rate =
					    readb(&pccs->var.start_network.
						  net_default_tx_rate);
				local->encryption =
				    readb(&pccs->var.start_network.encryption);
				if (!sniffer && (local->net_type == INFRA)
				    && !(local->sparm.b4.a_acting_as_ap_status)) {
					authenticate(local);
				}
				local->card_status = CARD_ACQ_COMPLETE;
			} else {
				local->card_status = CARD_ACQ_FAILED;

				del_timer(&local->timer);
				local->timer.expires = jiffies + HZ * 5;
				if (status == CCS_START_NETWORK) {
					dev_dbg(&link->dev,
					      "ray_cs interrupt network \"%s\" start failed\n",
					      memtmp);
					local->timer.function = start_net;
				} else {
					dev_dbg(&link->dev,
					      "ray_cs interrupt network \"%s\" join failed\n",
					      memtmp);
					local->timer.function = join_net;
				}
				add_timer(&local->timer);
			}
			break;
		case CCS_START_ASSOCIATION:
			if (status == CCS_COMMAND_COMPLETE) {
				local->card_status = CARD_ASSOC_COMPLETE;
				dev_dbg(&link->dev, "ray_cs association successful\n");
			} else {
				dev_dbg(&link->dev, "ray_cs association failed,\n");
				local->card_status = CARD_ASSOC_FAILED;
				join_net(&local->timer);
			}
			break;
		case CCS_TX_REQUEST:
			if (status == CCS_COMMAND_COMPLETE) {
				dev_dbg(&link->dev,
				      "ray_cs interrupt tx request complete\n");
			} else {
				dev_dbg(&link->dev,
				      "ray_cs interrupt tx request failed\n");
			}
			if (!sniffer)
				netif_start_queue(dev);
			netif_wake_queue(dev);
			break;
		case CCS_TEST_MEMORY:
			dev_dbg(&link->dev, "ray_cs interrupt mem test done\n");
			break;
		case CCS_SHUTDOWN:
			dev_dbg(&link->dev,
			      "ray_cs interrupt Unexpected CCS returned - Shutdown\n");
			break;
		case CCS_DUMP_MEMORY:
			dev_dbg(&link->dev, "ray_cs interrupt dump memory done\n");
			break;
		case CCS_START_TIMER:
			dev_dbg(&link->dev,
			      "ray_cs interrupt DING - raylink timer expired\n");
			break;
		default:
			dev_dbg(&link->dev,
			      "ray_cs interrupt Unexpected CCS 0x%x returned 0x%x\n",
			      rcsindex, cmd);
		}
		writeb(CCS_BUFFER_FREE, &pccs->buffer_status);
	} else { /* It's an RCS */

		prcs = rcs_base(local) + rcsindex;

		switch (readb(&prcs->interrupt_id)) {
		case PROCESS_RX_PACKET:
			ray_rx(dev, local, prcs);
			break;
		case REJOIN_NET_COMPLETE:
			dev_dbg(&link->dev, "ray_cs interrupt rejoin net complete\n");
			local->card_status = CARD_ACQ_COMPLETE;
			/* do we need to clear tx buffers CCS's? */
			if (local->sparm.b4.a_network_type == ADHOC) {
				if (!sniffer)
					netif_start_queue(dev);
			} else {
				memcpy_fromio(&local->bss_id,
					      prcs->var.rejoin_net_complete.
					      bssid, ADDRLEN);
				dev_dbg(&link->dev, "ray_cs new BSSID = %pm\n",
					local->bss_id);
				if (!sniffer)
					authenticate(local);
			}
			break;
		case ROAMING_INITIATED:
			dev_dbg(&link->dev, "ray_cs interrupt roaming initiated\n");
			netif_stop_queue(dev);
			local->card_status = CARD_DOING_ACQ;
			break;
		case JAPAN_CALL_SIGN_RXD:
			dev_dbg(&link->dev, "ray_cs interrupt japan call sign rx\n");
			break;
		default:
			dev_dbg(&link->dev,
			      "ray_cs Unexpected interrupt for RCS 0x%x cmd = 0x%x\n",
			      rcsindex,
			      (unsigned int)readb(&prcs->interrupt_id));
			break;
		}
		writeb(CCS_BUFFER_FREE, &prcs->buffer_status);
	}
	clear_interrupt(local);
	return IRQ_HANDLED;
} /* ray_interrupt */

/*===========================================================================*/
static void ray_rx(struct net_device *dev, ray_dev_t *local,
		   struct rcs __iomem *prcs)
{
	int rx_len;
	unsigned int pkt_addr;
	void __iomem *pmsg;
	pr_debug("ray_rx process rx packet\n");

	/* Calculate address of packet within Rx buffer */
	pkt_addr = ((readb(&prcs->var.rx_packet.rx_data_ptr[0]) << 8)
		    + readb(&prcs->var.rx_packet.rx_data_ptr[1])) & RX_BUFF_END;
	/* Length of first packet fragment */
	rx_len = (readb(&prcs->var.rx_packet.rx_data_length[0]) << 8)
	    + readb(&prcs->var.rx_packet.rx_data_length[1]);

	local->last_rsl = readb(&prcs->var.rx_packet.rx_sig_lev);
	pmsg = local->rmem + pkt_addr;
	switch (readb(pmsg)) {
	case DATA_TYPE:
		pr_debug("ray_rx data type\n");
		rx_data(dev, prcs, pkt_addr, rx_len);
		break;
	case AUTHENTIC_TYPE:
		pr_debug("ray_rx authentic type\n");
		if (sniffer)
			rx_data(dev, prcs, pkt_addr, rx_len);
		else
			rx_authenticate(local, prcs, pkt_addr, rx_len);
		break;
	case DEAUTHENTIC_TYPE:
		pr_debug("ray_rx deauth type\n");
		if (sniffer)
			rx_data(dev, prcs, pkt_addr, rx_len);
		else
			rx_deauthenticate(local, prcs, pkt_addr, rx_len);
		break;
	case NULL_MSG_TYPE:
		pr_debug("ray_cs rx NULL msg\n");
		break;
	case BEACON_TYPE:
		pr_debug("ray_rx beacon type\n");
		if (sniffer)
			rx_data(dev, prcs, pkt_addr, rx_len);

		copy_from_rx_buff(local, (UCHAR *) &local->last_bcn, pkt_addr,
				  rx_len < sizeof(struct beacon_rx) ?
				  rx_len : sizeof(struct beacon_rx));

		local->beacon_rxed = 1;
		/* Get the statistics so the card counters never overflow */
		ray_get_stats(dev);
		break;
	default:
		pr_debug("ray_cs unknown pkt type %2x\n",
		      (unsigned int)readb(pmsg));
		break;
	}

} /* end ray_rx */

/*===========================================================================*/
static void rx_data(struct net_device *dev, struct rcs __iomem *prcs,
		    unsigned int pkt_addr, int rx_len)
{
	struct sk_buff *skb = NULL;
	struct rcs __iomem *prcslink = prcs;
	ray_dev_t *local = netdev_priv(dev);
	UCHAR *rx_ptr;
	int total_len;
	int tmp;
#ifdef WIRELESS_SPY
	int siglev = local->last_rsl;
	u_char linksrcaddr[ETH_ALEN];	/* Other end of the wireless link */
#endif

	if (!sniffer) {
		if (translate) {
/* TBD length needs fixing for translated header */
			if (rx_len < (ETH_HLEN + RX_MAC_HEADER_LENGTH) ||
			    rx_len >
			    (dev->mtu + RX_MAC_HEADER_LENGTH + ETH_HLEN +
			     FCS_LEN)) {
				pr_debug(
				      "ray_cs invalid packet length %d received\n",
				      rx_len);
				return;
			}
		} else { /* encapsulated ethernet */

			if (rx_len < (ETH_HLEN + RX_MAC_HEADER_LENGTH) ||
			    rx_len >
			    (dev->mtu + RX_MAC_HEADER_LENGTH + ETH_HLEN +
			     FCS_LEN)) {
				pr_debug(
				      "ray_cs invalid packet length %d received\n",
				      rx_len);
				return;
			}
		}
	}
	pr_debug("ray_cs rx_data packet\n");
	/* If fragmented packet, verify sizes of fragments add up */
	if (readb(&prcs->var.rx_packet.next_frag_rcs_index) != 0xFF) {
		pr_debug("ray_cs rx'ed fragment\n");
		tmp = (readb(&prcs->var.rx_packet.totalpacketlength[0]) << 8)
		    + readb(&prcs->var.rx_packet.totalpacketlength[1]);
		total_len = tmp;
		prcslink = prcs;
		do {
			tmp -=
			    (readb(&prcslink->var.rx_packet.rx_data_length[0])
			     << 8)
			    + readb(&prcslink->var.rx_packet.rx_data_length[1]);
			if (readb(&prcslink->var.rx_packet.next_frag_rcs_index)
			    == 0xFF || tmp < 0)
				break;
			prcslink = rcs_base(local)
			    + readb(&prcslink->link_field);
		} while (1);

		if (tmp < 0) {
			pr_debug(
			      "ray_cs rx_data fragment lengths don't add up\n");
			local->stats.rx_dropped++;
			release_frag_chain(local, prcs);
			return;
		}
	} else { /* Single unfragmented packet */
		total_len = rx_len;
	}

	skb = dev_alloc_skb(total_len + 5);
	if (skb == NULL) {
		pr_debug("ray_cs rx_data could not allocate skb\n");
		local->stats.rx_dropped++;
		if (readb(&prcs->var.rx_packet.next_frag_rcs_index) != 0xFF)
			release_frag_chain(local, prcs);
		return;
	}
	skb_reserve(skb, 2);	/* Align IP on 16 byte (TBD check this) */

	pr_debug("ray_cs rx_data total_len = %x, rx_len = %x\n", total_len,
	      rx_len);

/************************/
	/* Reserve enough room for the whole damn packet. */
	rx_ptr = skb_put(skb, total_len);
	/* Copy the whole packet to sk_buff */
	rx_ptr +=
	    copy_from_rx_buff(local, rx_ptr, pkt_addr & RX_BUFF_END, rx_len);
	/* Get source address */
#ifdef WIRELESS_SPY
	skb_copy_from_linear_data_offset(skb,
					 offsetof(struct mac_header, addr_2),
					 linksrcaddr, ETH_ALEN);
#endif
	/* Now, deal with encapsulation/translation/sniffer */
	if (!sniffer) {
		if (!translate) {
			/* Encapsulated ethernet, so just lop off 802.11 MAC header */
/* TBD reserve            skb_reserve( skb, RX_MAC_HEADER_LENGTH); */
			skb_pull(skb, RX_MAC_HEADER_LENGTH);
		} else {
			/* Do translation */
			untranslate(local, skb, total_len);
		}
	} else { /* sniffer mode, so just pass whole packet */
	}

/************************/
	/* Now pick up the rest of the fragments if any */
	tmp = 17;
	if (readb(&prcs->var.rx_packet.next_frag_rcs_index) != 0xFF) {
		prcslink = prcs;
		pr_debug("ray_cs rx_data in fragment loop\n");
		do {
			prcslink = rcs_base(local)
			    +
			    readb(&prcslink->var.rx_packet.next_frag_rcs_index);
			rx_len =
			    ((readb(&prcslink->var.rx_packet.rx_data_length[0])
			      << 8)
			     +
			     readb(&prcslink->var.rx_packet.rx_data_length[1]))
			    & RX_BUFF_END;
			pkt_addr =
			    ((readb(&prcslink->var.rx_packet.rx_data_ptr[0]) <<
			      8)
			     + readb(&prcslink->var.rx_packet.rx_data_ptr[1]))
			    & RX_BUFF_END;

			rx_ptr +=
			    copy_from_rx_buff(local, rx_ptr, pkt_addr, rx_len);

		} while (tmp-- &&
			 readb(&prcslink->var.rx_packet.next_frag_rcs_index) !=
			 0xFF);
		release_frag_chain(local, prcs);
	}

	skb->protocol = eth_type_trans(skb, dev);
	netif_rx(skb);
	local->stats.rx_packets++;
	local->stats.rx_bytes += total_len;

	/* Gather signal strength per address */
#ifdef WIRELESS_SPY
	/* For the Access Point or the node having started the ad-hoc net
	 * note : ad-hoc work only in some specific configurations, but we
	 * kludge in ray_get_wireless_stats... */
	if (!memcmp(linksrcaddr, local->bss_id, ETH_ALEN)) {
		/* Update statistics */
		/*local->wstats.qual.qual = none ? */
		local->wstats.qual.level = siglev;
		/*local->wstats.qual.noise = none ? */
		local->wstats.qual.updated = 0x2;
	}
	/* Now, update the spy stuff */
	{
		struct iw_quality wstats;
		wstats.level = siglev;
		/* wstats.noise = none ? */
		/* wstats.qual = none ? */
		wstats.updated = 0x2;
		/* Update spy records */
		wireless_spy_update(dev, linksrcaddr, &wstats);
	}
#endif /* WIRELESS_SPY */
} /* end rx_data */

/*===========================================================================*/
static void untranslate(ray_dev_t *local, struct sk_buff *skb, int len)
{
	snaphdr_t *psnap = (snaphdr_t *) (skb->data + RX_MAC_HEADER_LENGTH);
	struct ieee80211_hdr *pmac = (struct ieee80211_hdr *)skb->data;
	__be16 type = *(__be16 *) psnap->ethertype;
	int delta;
	struct ethhdr *peth;
	UCHAR srcaddr[ADDRLEN];
	UCHAR destaddr[ADDRLEN];
	static const UCHAR org_bridge[3] = { 0, 0, 0xf8 };
	static const UCHAR org_1042[3] = { 0, 0, 0 };

	memcpy(destaddr, ieee80211_get_DA(pmac), ADDRLEN);
	memcpy(srcaddr, ieee80211_get_SA(pmac), ADDRLEN);

#if 0
	if {
		print_hex_dump(KERN_DEBUG, "skb->data before untranslate: ",
			       DUMP_PREFIX_NONE, 16, 1,
			       skb->data, 64, true);
		printk(KERN_DEBUG
		       "type = %08x, xsap = %02x%02x%02x, org = %02x02x02x\n",
		       ntohs(type), psnap->dsap, psnap->ssap, psnap->ctrl,
		       psnap->org[0], psnap->org[1], psnap->org[2]);
		printk(KERN_DEBUG "untranslate skb->data = %p\n", skb->data);
	}
#endif

	if (psnap->dsap != 0xaa || psnap->ssap != 0xaa || psnap->ctrl != 3) {
		/* not a snap type so leave it alone */
		pr_debug("ray_cs untranslate NOT SNAP %02x %02x %02x\n",
		      psnap->dsap, psnap->ssap, psnap->ctrl);

		delta = RX_MAC_HEADER_LENGTH - ETH_HLEN;
		peth = (struct ethhdr *)(skb->data + delta);
		peth->h_proto = htons(len - RX_MAC_HEADER_LENGTH);
	} else { /* Its a SNAP */
		if (memcmp(psnap->org, org_bridge, 3) == 0) {
		/* EtherII and nuke the LLC */
			pr_debug("ray_cs untranslate Bridge encap\n");
			delta = RX_MAC_HEADER_LENGTH
			    + sizeof(struct snaphdr_t) - ETH_HLEN;
			peth = (struct ethhdr *)(skb->data + delta);
			peth->h_proto = type;
		} else if (memcmp(psnap->org, org_1042, 3) == 0) {
			switch (ntohs(type)) {
			case ETH_P_IPX:
			case ETH_P_AARP:
				pr_debug("ray_cs untranslate RFC IPX/AARP\n");
				delta = RX_MAC_HEADER_LENGTH - ETH_HLEN;
				peth = (struct ethhdr *)(skb->data + delta);
				peth->h_proto =
				    htons(len - RX_MAC_HEADER_LENGTH);
				break;
			default:
				pr_debug("ray_cs untranslate RFC default\n");
				delta = RX_MAC_HEADER_LENGTH +
				    sizeof(struct snaphdr_t) - ETH_HLEN;
				peth = (struct ethhdr *)(skb->data + delta);
				peth->h_proto = type;
				break;
			}
		} else {
			printk("ray_cs untranslate very confused by packet\n");
			delta = RX_MAC_HEADER_LENGTH - ETH_HLEN;
			peth = (struct ethhdr *)(skb->data + delta);
			peth->h_proto = type;
		}
	}
/* TBD reserve  skb_reserve(skb, delta); */
	skb_pull(skb, delta);
	pr_debug("untranslate after skb_pull(%d), skb->data = %p\n", delta,
	      skb->data);
	memcpy(peth->h_dest, destaddr, ADDRLEN);
	memcpy(peth->h_source, srcaddr, ADDRLEN);
#if 0
	{
		int i;
		printk(KERN_DEBUG "skb->data after untranslate:");
		for (i = 0; i < 64; i++)
			printk("%02x ", skb->data[i]);
		printk("\n");
	}
#endif
} /* end untranslate */

/*===========================================================================*/
/* Copy data from circular receive buffer to PC memory.
 * dest     = destination address in PC memory
 * pkt_addr = source address in receive buffer
 * len      = length of packet to copy
 */
static int copy_from_rx_buff(ray_dev_t *local, UCHAR *dest, int pkt_addr,
			     int length)
{
	int wrap_bytes = (pkt_addr + length) - (RX_BUFF_END + 1);
	if (wrap_bytes <= 0) {
		memcpy_fromio(dest, local->rmem + pkt_addr, length);
	} else { /* Packet wrapped in circular buffer */

		memcpy_fromio(dest, local->rmem + pkt_addr,
			      length - wrap_bytes);
		memcpy_fromio(dest + length - wrap_bytes, local->rmem,
			      wrap_bytes);
	}
	return length;
}

/*===========================================================================*/
static void release_frag_chain(ray_dev_t *local, struct rcs __iomem *prcs)
{
	struct rcs __iomem *prcslink = prcs;
	int tmp = 17;
	unsigned rcsindex = readb(&prcs->var.rx_packet.next_frag_rcs_index);

	while (tmp--) {
		writeb(CCS_BUFFER_FREE, &prcslink->buffer_status);
		if (rcsindex >= (NUMBER_OF_CCS + NUMBER_OF_RCS)) {
			pr_debug("ray_cs interrupt bad rcsindex = 0x%x\n",
			      rcsindex);
			break;
		}
		prcslink = rcs_base(local) + rcsindex;
		rcsindex = readb(&prcslink->var.rx_packet.next_frag_rcs_index);
	}
	writeb(CCS_BUFFER_FREE, &prcslink->buffer_status);
}

/*===========================================================================*/
static void authenticate(ray_dev_t *local)
{
	struct pcmcia_device *link = local->finder;
	dev_dbg(&link->dev, "ray_cs Starting authentication.\n");
	if (!(pcmcia_dev_present(link))) {
		dev_dbg(&link->dev, "ray_cs authenticate - device not present\n");
		return;
	}

	del_timer(&local->timer);
	if (build_auth_frame(local, local->bss_id, OPEN_AUTH_REQUEST)) {
		local->timer.function = join_net;
	} else {
		local->timer.function = authenticate_timeout;
	}
	local->timer.expires = jiffies + HZ * 2;
	add_timer(&local->timer);
	local->authentication_state = AWAITING_RESPONSE;
} /* end authenticate */

/*===========================================================================*/
static void rx_authenticate(ray_dev_t *local, struct rcs __iomem *prcs,
			    unsigned int pkt_addr, int rx_len)
{
	UCHAR buff[256];
	struct ray_rx_msg *msg = (struct ray_rx_msg *) buff;

	del_timer(&local->timer);

	copy_from_rx_buff(local, buff, pkt_addr, rx_len & 0xff);
	/* if we are trying to get authenticated */
	if (local->sparm.b4.a_network_type == ADHOC) {
		pr_debug("ray_cs rx_auth var= %6ph\n", msg->var);
		if (msg->var[2] == 1) {
			pr_debug("ray_cs Sending authentication response.\n");
			if (!build_auth_frame
			    (local, msg->mac.addr_2, OPEN_AUTH_RESPONSE)) {
				local->authentication_state = NEED_TO_AUTH;
				memcpy(local->auth_id, msg->mac.addr_2,
				       ADDRLEN);
			}
		}
	} else { /* Infrastructure network */

		if (local->authentication_state == AWAITING_RESPONSE) {
			/* Verify authentication sequence #2 and success */
			if (msg->var[2] == 2) {
				if ((msg->var[3] | msg->var[4]) == 0) {
					pr_debug("Authentication successful\n");
					local->card_status = CARD_AUTH_COMPLETE;
					associate(local);
					local->authentication_state =
					    AUTHENTICATED;
				} else {
					pr_debug("Authentication refused\n");
					local->card_status = CARD_AUTH_REFUSED;
					join_net(&local->timer);
					local->authentication_state =
					    UNAUTHENTICATED;
				}
			}
		}
	}

} /* end rx_authenticate */

/*===========================================================================*/
static void associate(ray_dev_t *local)
{
	struct ccs __iomem *pccs;
	struct pcmcia_device *link = local->finder;
	struct net_device *dev = link->priv;
	int ccsindex;
	if (!(pcmcia_dev_present(link))) {
		dev_dbg(&link->dev, "ray_cs associate - device not present\n");
		return;
	}
	/* If no tx buffers available, return */
	if ((ccsindex = get_free_ccs(local)) < 0) {
/* TBD should never be here but... what if we are? */
		dev_dbg(&link->dev, "ray_cs associate - No free ccs\n");
		return;
	}
	dev_dbg(&link->dev, "ray_cs Starting association with access point\n");
	pccs = ccs_base(local) + ccsindex;
	/* fill in the CCS */
	writeb(CCS_START_ASSOCIATION, &pccs->cmd);
	/* Interrupt the firmware to process the command */
	if (interrupt_ecf(local, ccsindex)) {
		dev_dbg(&link->dev, "ray_cs associate failed - ECF not ready for intr\n");
		writeb(CCS_BUFFER_FREE, &(pccs++)->buffer_status);

		del_timer(&local->timer);
		local->timer.expires = jiffies + HZ * 2;
		local->timer.function = join_net;
		add_timer(&local->timer);
		local->card_status = CARD_ASSOC_FAILED;
		return;
	}
	if (!sniffer)
		netif_start_queue(dev);

} /* end associate */

/*===========================================================================*/
static void rx_deauthenticate(ray_dev_t *local, struct rcs __iomem *prcs,
			      unsigned int pkt_addr, int rx_len)
{
/*  UCHAR buff[256];
    struct ray_rx_msg *msg = (struct ray_rx_msg *) buff;
*/
	pr_debug("Deauthentication frame received\n");
	local->authentication_state = UNAUTHENTICATED;
	/* Need to reauthenticate or rejoin depending on reason code */
/*  copy_from_rx_buff(local, buff, pkt_addr, rx_len & 0xff);
 */
}

/*===========================================================================*/
static void clear_interrupt(ray_dev_t *local)
{
	writeb(0, local->amem + CIS_OFFSET + HCS_INTR_OFFSET);
}

/*===========================================================================*/
#ifdef CONFIG_PROC_FS
#define MAXDATA (PAGE_SIZE - 80)

static const char *card_status[] = {
	"Card inserted - uninitialized",	/* 0 */
	"Card not downloaded",			/* 1 */
	"Waiting for download parameters",	/* 2 */
	"Card doing acquisition",		/* 3 */
	"Acquisition complete",			/* 4 */
	"Authentication complete",		/* 5 */
	"Association complete",			/* 6 */
	"???", "???", "???", "???",		/* 7 8 9 10 undefined */
	"Card init error",			/* 11 */
	"Download parameters error",		/* 12 */
	"???",					/* 13 */
	"Acquisition failed",			/* 14 */
	"Authentication refused",		/* 15 */
	"Association failed"			/* 16 */
};

static const char *nettype[] = { "Adhoc", "Infra " };
static const char *framing[] = { "Encapsulation", "Translation" }

;
/*===========================================================================*/
static int ray_cs_proc_show(struct seq_file *m, void *v)
{
/* Print current values which are not available via other means
 * eg ifconfig
 */
	int i;
	struct pcmcia_device *link;
	struct net_device *dev;
	ray_dev_t *local;
	UCHAR *p;
	struct freq_hop_element *pfh;
	UCHAR c[33];

	link = this_device;
	if (!link)
		return 0;
	dev = (struct net_device *)link->priv;
	if (!dev)
		return 0;
	local = netdev_priv(dev);
	if (!local)
		return 0;

	seq_puts(m, "Raylink Wireless LAN driver status\n");
	seq_printf(m, "%s\n", rcsid);
	/* build 4 does not report version, and field is 0x55 after memtest */
	seq_puts(m, "Firmware version     = ");
	if (local->fw_ver == 0x55)
		seq_puts(m, "4 - Use dump_cis for more details\n");
	else
		seq_printf(m, "%2d.%02d.%02d\n",
			   local->fw_ver, local->fw_bld, local->fw_var);

	for (i = 0; i < 32; i++)
		c[i] = local->sparm.b5.a_current_ess_id[i];
	c[32] = 0;
	seq_printf(m, "%s network ESSID = \"%s\"\n",
		   nettype[local->sparm.b5.a_network_type], c);

	p = local->bss_id;
	seq_printf(m, "BSSID                = %pM\n", p);

	seq_printf(m, "Country code         = %d\n",
		   local->sparm.b5.a_curr_country_code);

	i = local->card_status;
	if (i < 0)
		i = 10;
	if (i > 16)
		i = 10;
	seq_printf(m, "Card status          = %s\n", card_status[i]);

	seq_printf(m, "Framing mode         = %s\n", framing[translate]);

	seq_printf(m, "Last pkt signal lvl  = %d\n", local->last_rsl);

	if (local->beacon_rxed) {
		/* Pull some fields out of last beacon received */
		seq_printf(m, "Beacon Interval      = %d Kus\n",
			   local->last_bcn.beacon_intvl[0]
			   + 256 * local->last_bcn.beacon_intvl[1]);

		p = local->last_bcn.elements;
		if (p[0] == C_ESSID_ELEMENT_ID)
			p += p[1] + 2;
		else {
			seq_printf(m,
				   "Parse beacon failed at essid element id = %d\n",
				   p[0]);
			return 0;
		}

		if (p[0] == C_SUPPORTED_RATES_ELEMENT_ID) {
			seq_puts(m, "Supported rate codes = ");
			for (i = 2; i < p[1] + 2; i++)
				seq_printf(m, "0x%02x ", p[i]);
			seq_putc(m, '\n');
			p += p[1] + 2;
		} else {
			seq_puts(m, "Parse beacon failed at rates element\n");
			return 0;
		}

		if (p[0] == C_FH_PARAM_SET_ELEMENT_ID) {
			pfh = (struct freq_hop_element *)p;
			seq_printf(m, "Hop dwell            = %d Kus\n",
				   pfh->dwell_time[0] +
				   256 * pfh->dwell_time[1]);
			seq_printf(m, "Hop set              = %d\n",
				   pfh->hop_set);
			seq_printf(m, "Hop pattern          = %d\n",
				   pfh->hop_pattern);
			seq_printf(m, "Hop index            = %d\n",
				   pfh->hop_index);
			p += p[1] + 2;
		} else {
			seq_puts(m,
				 "Parse beacon failed at FH param element\n");
			return 0;
		}
	} else {
		seq_puts(m, "No beacons received\n");
	}
	return 0;
}
#endif
/*===========================================================================*/
static int build_auth_frame(ray_dev_t *local, UCHAR *dest, int auth_type)
{
	int addr;
	struct ccs __iomem *pccs;
	struct tx_msg __iomem *ptx;
	int ccsindex;

	/* If no tx buffers available, return */
	if ((ccsindex = get_free_tx_ccs(local)) < 0) {
		pr_debug("ray_cs send authenticate - No free tx ccs\n");
		return -1;
	}

	pccs = ccs_base(local) + ccsindex;

	/* Address in card space */
	addr = TX_BUF_BASE + (ccsindex << 11);
	/* fill in the CCS */
	writeb(CCS_TX_REQUEST, &pccs->cmd);
	writeb(addr >> 8, pccs->var.tx_request.tx_data_ptr);
	writeb(0x20, pccs->var.tx_request.tx_data_ptr + 1);
	writeb(TX_AUTHENTICATE_LENGTH_MSB, pccs->var.tx_request.tx_data_length);
	writeb(TX_AUTHENTICATE_LENGTH_LSB,
	       pccs->var.tx_request.tx_data_length + 1);
	writeb(0, &pccs->var.tx_request.pow_sav_mode);

	ptx = local->sram + addr;
	/* fill in the mac header */
	writeb(PROTOCOL_VER | AUTHENTIC_TYPE, &ptx->mac.frame_ctl_1);
	writeb(0, &ptx->mac.frame_ctl_2);

	memcpy_toio(ptx->mac.addr_1, dest, ADDRLEN);
	memcpy_toio(ptx->mac.addr_2, local->sparm.b4.a_mac_addr, ADDRLEN);
	memcpy_toio(ptx->mac.addr_3, local->bss_id, ADDRLEN);

	/* Fill in msg body with protocol 00 00, sequence 01 00 ,status 00 00 */
	memset_io(ptx->var, 0, 6);
	writeb(auth_type & 0xff, ptx->var + 2);

	/* Interrupt the firmware to process the command */
	if (interrupt_ecf(local, ccsindex)) {
		pr_debug(
		      "ray_cs send authentication request failed - ECF not ready for intr\n");
		writeb(CCS_BUFFER_FREE, &(pccs++)->buffer_status);
		return -1;
	}
	return 0;
} /* End build_auth_frame */

/*===========================================================================*/
#ifdef CONFIG_PROC_FS
static ssize_t ray_cs_essid_proc_write(struct file *file,
		const char __user *buffer, size_t count, loff_t *pos)
{
	static char proc_essid[33];
	unsigned int len = count;

	if (len > 32)
		len = 32;
	memset(proc_essid, 0, 33);
	if (copy_from_user(proc_essid, buffer, len))
		return -EFAULT;
	essid = proc_essid;
	return count;
}

static const struct proc_ops ray_cs_essid_proc_ops = {
	.proc_write	= ray_cs_essid_proc_write,
	.proc_lseek	= noop_llseek,
};

static ssize_t int_proc_write(struct file *file, const char __user *buffer,
			      size_t count, loff_t *pos)
{
	static char proc_number[10];
	char *p;
	int nr, len;

	if (!count)
		return 0;

	if (count > 9)
		return -EINVAL;
	if (copy_from_user(proc_number, buffer, count))
		return -EFAULT;
	p = proc_number;
	nr = 0;
	len = count;
	do {
		unsigned int c = *p - '0';
		if (c > 9)
			return -EINVAL;
		nr = nr * 10 + c;
		p++;
	} while (--len);
	*(int *)pde_data(file_inode(file)) = nr;
	return count;
}

static const struct proc_ops int_proc_ops = {
	.proc_write	= int_proc_write,
	.proc_lseek	= noop_llseek,
};
#endif

static const struct pcmcia_device_id ray_ids[] = {
	PCMCIA_DEVICE_MANF_CARD(0x01a6, 0x0000),
	PCMCIA_DEVICE_NULL,
};

MODULE_DEVICE_TABLE(pcmcia, ray_ids);

static struct pcmcia_driver ray_driver = {
	.owner = THIS_MODULE,
	.name = "ray_cs",
	.probe = ray_probe,
	.remove = ray_detach,
	.id_table = ray_ids,
	.suspend = ray_suspend,
	.resume = ray_resume,
};

static int __init init_ray_cs(void)
{
	int rc;

	pr_debug("%s\n", rcsid);
	rc = pcmcia_register_driver(&ray_driver);
	pr_debug("raylink init_module register_pcmcia_driver returns 0x%x\n",
	      rc);
	if (rc)
		return rc;

#ifdef CONFIG_PROC_FS
	proc_mkdir("driver/ray_cs", NULL);

	proc_create_single("driver/ray_cs/ray_cs", 0, NULL, ray_cs_proc_show);
	proc_create("driver/ray_cs/essid", 0200, NULL, &ray_cs_essid_proc_ops);
	proc_create_data("driver/ray_cs/net_type", 0200, NULL, &int_proc_ops,
			 &net_type);
	proc_create_data("driver/ray_cs/translate", 0200, NULL, &int_proc_ops,
			 &translate);
#endif
	translate = !!translate;
	return 0;
} /* init_ray_cs */

/*===========================================================================*/

static void __exit exit_ray_cs(void)
{
	pr_debug("ray_cs: cleanup_module\n");

#ifdef CONFIG_PROC_FS
	remove_proc_subtree("driver/ray_cs", NULL);
#endif

	pcmcia_unregister_driver(&ray_driver);
} /* exit_ray_cs */

module_init(init_ray_cs);
module_exit(exit_ray_cs);

/*===========================================================================*/
