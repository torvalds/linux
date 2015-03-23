/*
 * WPA Supplicant - roboswitch driver interface
 * Copyright (c) 2008-2009 Jouke Witteveen
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Alternatively, this software may be distributed under the terms of BSD
 * license.
 *
 * See README and COPYING for more details.
 */

#include "includes.h"
#include <sys/ioctl.h>
#include <linux/sockios.h>
#include <linux/if_ether.h>
#include <linux/mii.h>
#include <net/if.h>

#include "common.h"
#include "driver.h"
#include "l2_packet/l2_packet.h"

#define ROBO_PHY_ADDR		0x1e	/* RoboSwitch PHY address */

/* MII access registers */
#define ROBO_MII_PAGE		0x10	/* MII page register */
#define ROBO_MII_ADDR		0x11	/* MII address register */
#define ROBO_MII_DATA_OFFSET	0x18	/* Start of MII data registers */

#define ROBO_MII_PAGE_ENABLE	0x01	/* MII page op code */
#define ROBO_MII_ADDR_WRITE	0x01	/* MII address write op code */
#define ROBO_MII_ADDR_READ	0x02	/* MII address read op code */
#define ROBO_MII_DATA_MAX	   4	/* Consecutive MII data registers */
#define ROBO_MII_RETRY_MAX	  10	/* Read attempts before giving up */

/* Page numbers */
#define ROBO_ARLCTRL_PAGE	0x04	/* ARL control page */
#define ROBO_VLAN_PAGE		0x34	/* VLAN page */

/* ARL control page registers */
#define ROBO_ARLCTRL_CONF	0x00	/* ARL configuration register */
#define ROBO_ARLCTRL_ADDR_1	0x10	/* Multiport address 1 */
#define ROBO_ARLCTRL_VEC_1	0x16	/* Multiport vector 1 */
#define ROBO_ARLCTRL_ADDR_2	0x20	/* Multiport address 2 */
#define ROBO_ARLCTRL_VEC_2	0x26	/* Multiport vector 2 */

/* VLAN page registers */
#define ROBO_VLAN_ACCESS	0x08	/* VLAN table access register */
#define ROBO_VLAN_ACCESS_5350	0x06	/* VLAN table access register (5350) */
#define ROBO_VLAN_READ		0x0c	/* VLAN read register */
#define ROBO_VLAN_MAX		0xff	/* Maximum number of VLANs */


static const u8 pae_group_addr[ETH_ALEN] =
{ 0x01, 0x80, 0xc2, 0x00, 0x00, 0x03 };


struct wpa_driver_roboswitch_data {
	void *ctx;
	struct l2_packet_data *l2;
	char ifname[IFNAMSIZ + 1];
	u8 own_addr[ETH_ALEN];
	struct ifreq ifr;
	int fd, is_5350;
	u16 ports;
};


/* Copied from the kernel-only part of mii.h. */
static inline struct mii_ioctl_data *if_mii(struct ifreq *rq)
{
	return (struct mii_ioctl_data *) &rq->ifr_ifru;
}


/*
 * RoboSwitch uses 16-bit Big Endian addresses.
 * The ordering of the words is reversed in the MII registers.
 */
static void wpa_driver_roboswitch_addr_be16(const u8 addr[ETH_ALEN], u16 *be)
{
	int i;
	for (i = 0; i < ETH_ALEN; i += 2)
		be[(ETH_ALEN - i) / 2 - 1] = WPA_GET_BE16(addr + i);
}


static u16 wpa_driver_roboswitch_mdio_read(
	struct wpa_driver_roboswitch_data *drv, u8 reg)
{
	struct mii_ioctl_data *mii = if_mii(&drv->ifr);

	mii->phy_id = ROBO_PHY_ADDR;
	mii->reg_num = reg;

	if (ioctl(drv->fd, SIOCGMIIREG, &drv->ifr) < 0) {
		perror("ioctl[SIOCGMIIREG]");
		return 0x00;
	}
	return mii->val_out;
}


static void wpa_driver_roboswitch_mdio_write(
	struct wpa_driver_roboswitch_data *drv, u8 reg, u16 val)
{
	struct mii_ioctl_data *mii = if_mii(&drv->ifr);

	mii->phy_id = ROBO_PHY_ADDR;
	mii->reg_num = reg;
	mii->val_in = val;

	if (ioctl(drv->fd, SIOCSMIIREG, &drv->ifr) < 0) {
		perror("ioctl[SIOCSMIIREG");
	}
}


static int wpa_driver_roboswitch_reg(struct wpa_driver_roboswitch_data *drv,
				     u8 page, u8 reg, u8 op)
{
	int i;

	/* set page number */
	wpa_driver_roboswitch_mdio_write(drv, ROBO_MII_PAGE,
					 (page << 8) | ROBO_MII_PAGE_ENABLE);
	/* set register address */
	wpa_driver_roboswitch_mdio_write(drv, ROBO_MII_ADDR, (reg << 8) | op);

	/* check if operation completed */
	for (i = 0; i < ROBO_MII_RETRY_MAX; ++i) {
		if ((wpa_driver_roboswitch_mdio_read(drv, ROBO_MII_ADDR) & 3)
		    == 0)
			return 0;
	}
	/* timeout */
	return -1;
}


static int wpa_driver_roboswitch_read(struct wpa_driver_roboswitch_data *drv,
				      u8 page, u8 reg, u16 *val, int len)
{
	int i;

	if (len > ROBO_MII_DATA_MAX ||
	    wpa_driver_roboswitch_reg(drv, page, reg, ROBO_MII_ADDR_READ) < 0)
		return -1;

	for (i = 0; i < len; ++i) {
		val[i] = wpa_driver_roboswitch_mdio_read(
			drv, ROBO_MII_DATA_OFFSET + i);
	}

	return 0;
}


static int wpa_driver_roboswitch_write(struct wpa_driver_roboswitch_data *drv,
				       u8 page, u8 reg, u16 *val, int len)
{
	int i;

	if (len > ROBO_MII_DATA_MAX) return -1;
	for (i = 0; i < len; ++i) {
		wpa_driver_roboswitch_mdio_write(drv, ROBO_MII_DATA_OFFSET + i,
						 val[i]);
	}
	return wpa_driver_roboswitch_reg(drv, page, reg, ROBO_MII_ADDR_WRITE);
}


static void wpa_driver_roboswitch_receive(void *priv, const u8 *src_addr,
					  const u8 *buf, size_t len)
{
	struct wpa_driver_roboswitch_data *drv = priv;

	if (len > 14 && WPA_GET_BE16(buf + 12) == ETH_P_EAPOL &&
	    os_memcmp(buf, drv->own_addr, ETH_ALEN) == 0)
		drv_event_eapol_rx(drv->ctx, src_addr, buf + 14, len - 14);
}


static int wpa_driver_roboswitch_get_ssid(void *priv, u8 *ssid)
{
	ssid[0] = 0;
	return 0;
}


static int wpa_driver_roboswitch_get_bssid(void *priv, u8 *bssid)
{
	/* Report PAE group address as the "BSSID" for wired connection. */
	os_memcpy(bssid, pae_group_addr, ETH_ALEN);
	return 0;
}


static int wpa_driver_roboswitch_get_capa(void *priv,
					  struct wpa_driver_capa *capa)
{
	os_memset(capa, 0, sizeof(*capa));
	capa->flags = WPA_DRIVER_FLAGS_WIRED;
	return 0;
}


static int wpa_driver_roboswitch_set_param(void *priv, const char *param)
{
	struct wpa_driver_roboswitch_data *drv = priv;
	char *sep;

	if (param == NULL || os_strstr(param, "multicast_only=1") == NULL) {
		sep = drv->ifname + os_strlen(drv->ifname);
		*sep = '.';
		drv->l2 = l2_packet_init(drv->ifname, NULL, ETH_P_ALL,
					 wpa_driver_roboswitch_receive, drv,
					 1);
		if (drv->l2 == NULL) {
			wpa_printf(MSG_INFO, "%s: Unable to listen on %s",
				   __func__, drv->ifname);
			return -1;
		}
		*sep = '\0';
		l2_packet_get_own_addr(drv->l2, drv->own_addr);
	} else {
		wpa_printf(MSG_DEBUG, "%s: Ignoring unicast frames", __func__);
		drv->l2 = NULL;
	}
	return 0;
}


static const char * wpa_driver_roboswitch_get_ifname(void *priv)
{
	struct wpa_driver_roboswitch_data *drv = priv;
	return drv->ifname;
}


static int wpa_driver_roboswitch_join(struct wpa_driver_roboswitch_data *drv,
				      u16 ports, const u8 *addr)
{
	u16 read1[3], read2[3], addr_be16[3];

	wpa_driver_roboswitch_addr_be16(addr, addr_be16);

	if (wpa_driver_roboswitch_read(drv, ROBO_ARLCTRL_PAGE,
				       ROBO_ARLCTRL_CONF, read1, 1) < 0)
		return -1;
	if (!(read1[0] & (1 << 4))) {
		/* multiport addresses are not yet enabled */
		read1[0] |= 1 << 4;
		wpa_driver_roboswitch_write(drv, ROBO_ARLCTRL_PAGE,
					    ROBO_ARLCTRL_ADDR_1, addr_be16, 3);
		wpa_driver_roboswitch_write(drv, ROBO_ARLCTRL_PAGE,
					    ROBO_ARLCTRL_VEC_1, &ports, 1);
		wpa_driver_roboswitch_write(drv, ROBO_ARLCTRL_PAGE,
					    ROBO_ARLCTRL_ADDR_2, addr_be16, 3);
		wpa_driver_roboswitch_write(drv, ROBO_ARLCTRL_PAGE,
					    ROBO_ARLCTRL_VEC_2, &ports, 1);
		wpa_driver_roboswitch_write(drv, ROBO_ARLCTRL_PAGE,
					    ROBO_ARLCTRL_CONF, read1, 1);
	} else {
		/* if both multiport addresses are the same we can add */
		wpa_driver_roboswitch_read(drv, ROBO_ARLCTRL_PAGE,
					   ROBO_ARLCTRL_ADDR_1, read1, 3);
		wpa_driver_roboswitch_read(drv, ROBO_ARLCTRL_PAGE,
					   ROBO_ARLCTRL_ADDR_2, read2, 3);
		if (os_memcmp(read1, read2, 6) != 0)
			return -1;
		wpa_driver_roboswitch_read(drv, ROBO_ARLCTRL_PAGE,
					   ROBO_ARLCTRL_VEC_1, read1, 1);
		wpa_driver_roboswitch_read(drv, ROBO_ARLCTRL_PAGE,
					   ROBO_ARLCTRL_VEC_2, read2, 1);
		if (read1[0] != read2[0])
			return -1;
		wpa_driver_roboswitch_write(drv, ROBO_ARLCTRL_PAGE,
					    ROBO_ARLCTRL_ADDR_1, addr_be16, 3);
		wpa_driver_roboswitch_write(drv, ROBO_ARLCTRL_PAGE,
					    ROBO_ARLCTRL_VEC_1, &ports, 1);
	}
	return 0;
}


static int wpa_driver_roboswitch_leave(struct wpa_driver_roboswitch_data *drv,
				       u16 ports, const u8 *addr)
{
	u16 _read, addr_be16[3], addr_read[3], ports_read;

	wpa_driver_roboswitch_addr_be16(addr, addr_be16);

	wpa_driver_roboswitch_read(drv, ROBO_ARLCTRL_PAGE, ROBO_ARLCTRL_CONF,
				   &_read, 1);
	/* If ARL control is disabled, there is nothing to leave. */
	if (!(_read & (1 << 4))) return -1;

	wpa_driver_roboswitch_read(drv, ROBO_ARLCTRL_PAGE,
				   ROBO_ARLCTRL_ADDR_1, addr_read, 3);
	wpa_driver_roboswitch_read(drv, ROBO_ARLCTRL_PAGE, ROBO_ARLCTRL_VEC_1,
				   &ports_read, 1);
	/* check if we occupy multiport address 1 */
	if (os_memcmp(addr_read, addr_be16, 6) == 0 && ports_read == ports) {
		wpa_driver_roboswitch_read(drv, ROBO_ARLCTRL_PAGE,
					   ROBO_ARLCTRL_ADDR_2, addr_read, 3);
		wpa_driver_roboswitch_read(drv, ROBO_ARLCTRL_PAGE,
					   ROBO_ARLCTRL_VEC_2, &ports_read, 1);
		/* and multiport address 2 */
		if (os_memcmp(addr_read, addr_be16, 6) == 0 &&
		    ports_read == ports) {
			_read &= ~(1 << 4);
			wpa_driver_roboswitch_write(drv, ROBO_ARLCTRL_PAGE,
						    ROBO_ARLCTRL_CONF, &_read,
						    1);
		} else {
			wpa_driver_roboswitch_read(drv, ROBO_ARLCTRL_PAGE,
						   ROBO_ARLCTRL_ADDR_1,
						   addr_read, 3);
			wpa_driver_roboswitch_read(drv, ROBO_ARLCTRL_PAGE,
						   ROBO_ARLCTRL_VEC_1,
						   &ports_read, 1);
			wpa_driver_roboswitch_write(drv, ROBO_ARLCTRL_PAGE,
						    ROBO_ARLCTRL_ADDR_2,
						    addr_read, 3);
			wpa_driver_roboswitch_write(drv, ROBO_ARLCTRL_PAGE,
						    ROBO_ARLCTRL_VEC_2,
						    &ports_read, 1);
		}
	} else {
		wpa_driver_roboswitch_read(drv, ROBO_ARLCTRL_PAGE,
					   ROBO_ARLCTRL_ADDR_2, addr_read, 3);
		wpa_driver_roboswitch_read(drv, ROBO_ARLCTRL_PAGE,
					   ROBO_ARLCTRL_VEC_2, &ports_read, 1);
		/* or multiport address 2 */
		if (os_memcmp(addr_read, addr_be16, 6) == 0 &&
		    ports_read == ports) {
			wpa_driver_roboswitch_write(drv, ROBO_ARLCTRL_PAGE,
						    ROBO_ARLCTRL_ADDR_1,
						    addr_read, 3);
			wpa_driver_roboswitch_write(drv, ROBO_ARLCTRL_PAGE,
						    ROBO_ARLCTRL_VEC_1,
						    &ports_read, 1);
		} else return -1;
	}
	return 0;
}


static void * wpa_driver_roboswitch_init(void *ctx, const char *ifname)
{
	struct wpa_driver_roboswitch_data *drv;
	char *sep;
	u16 vlan = 0, _read[2];

	drv = os_zalloc(sizeof(*drv));
	if (drv == NULL) return NULL;
	drv->ctx = ctx;
	drv->own_addr[0] = '\0';

	/* copy ifname and take a pointer to the second to last character */
	sep = drv->ifname +
	      os_strlcpy(drv->ifname, ifname, sizeof(drv->ifname)) - 2;
	/* find the '.' seperating <interface> and <vlan> */
	while (sep > drv->ifname && *sep != '.') sep--;
	if (sep <= drv->ifname) {
		wpa_printf(MSG_INFO, "%s: No <interface>.<vlan> pair in "
			   "interface name %s", __func__, drv->ifname);
		os_free(drv);
		return NULL;
	}
	*sep = '\0';
	while (*++sep) {
		if (*sep < '0' || *sep > '9') {
			wpa_printf(MSG_INFO, "%s: Invalid vlan specification "
				   "in interface name %s", __func__, ifname);
			os_free(drv);
			return NULL;
		}
		vlan *= 10;
		vlan += *sep - '0';
		if (vlan > ROBO_VLAN_MAX) {
			wpa_printf(MSG_INFO, "%s: VLAN out of range in "
				   "interface name %s", __func__, ifname);
			os_free(drv);
			return NULL;
		}
	}

	drv->fd = socket(PF_INET, SOCK_DGRAM, 0);
	if (drv->fd < 0) {
		wpa_printf(MSG_INFO, "%s: Unable to create socket", __func__);
		os_free(drv);
		return NULL;
	}

	os_memset(&drv->ifr, 0, sizeof(drv->ifr));
	os_strlcpy(drv->ifr.ifr_name, drv->ifname, IFNAMSIZ);
	if (ioctl(drv->fd, SIOCGMIIPHY, &drv->ifr) < 0) {
		perror("ioctl[SIOCGMIIPHY]");
		os_free(drv);
		return NULL;
	}
	if (if_mii(&drv->ifr)->phy_id != ROBO_PHY_ADDR) {
		wpa_printf(MSG_INFO, "%s: Invalid phy address (not a "
			   "RoboSwitch?)", __func__);
		os_free(drv);
		return NULL;
	}

	/* set and read back to see if the register can be used */
	_read[0] = ROBO_VLAN_MAX;
	wpa_driver_roboswitch_write(drv, ROBO_VLAN_PAGE, ROBO_VLAN_ACCESS_5350,
				    _read, 1);
	wpa_driver_roboswitch_read(drv, ROBO_VLAN_PAGE, ROBO_VLAN_ACCESS_5350,
				   _read + 1, 1);
	drv->is_5350 = _read[0] == _read[1];

	/* set the read bit */
	vlan |= 1 << 13;
	wpa_driver_roboswitch_write(drv, ROBO_VLAN_PAGE,
				    drv->is_5350 ? ROBO_VLAN_ACCESS_5350
						 : ROBO_VLAN_ACCESS,
				    &vlan, 1);
	wpa_driver_roboswitch_read(drv, ROBO_VLAN_PAGE, ROBO_VLAN_READ, _read,
				   drv->is_5350 ? 2 : 1);
	if (!(drv->is_5350 ? _read[1] & (1 << 4) : _read[0] & (1 << 14))) {
		wpa_printf(MSG_INFO, "%s: Could not get port information for "
				     "VLAN %d", __func__, vlan & ~(1 << 13));
		os_free(drv);
		return NULL;
	}
	drv->ports = _read[0] & 0x001F;
	/* add the MII port */
	drv->ports |= 1 << 8;
	if (wpa_driver_roboswitch_join(drv, drv->ports, pae_group_addr) < 0) {
		wpa_printf(MSG_INFO, "%s: Unable to join PAE group", __func__);
		os_free(drv);
		return NULL;
	} else {
		wpa_printf(MSG_DEBUG, "%s: Added PAE group address to "
			   "RoboSwitch ARL", __func__);
	}

	return drv;
}


static void wpa_driver_roboswitch_deinit(void *priv)
{
	struct wpa_driver_roboswitch_data *drv = priv;

	if (drv->l2) {
		l2_packet_deinit(drv->l2);
		drv->l2 = NULL;
	}
	if (wpa_driver_roboswitch_leave(drv, drv->ports, pae_group_addr) < 0) {
		wpa_printf(MSG_DEBUG, "%s: Unable to leave PAE group",
			   __func__);
	}

	close(drv->fd);
	os_free(drv);
}


const struct wpa_driver_ops wpa_driver_roboswitch_ops = {
	.name = "roboswitch",
	.desc = "wpa_supplicant roboswitch driver",
	.get_ssid = wpa_driver_roboswitch_get_ssid,
	.get_bssid = wpa_driver_roboswitch_get_bssid,
	.get_capa = wpa_driver_roboswitch_get_capa,
	.init = wpa_driver_roboswitch_init,
	.deinit = wpa_driver_roboswitch_deinit,
	.set_param = wpa_driver_roboswitch_set_param,
	.get_ifname = wpa_driver_roboswitch_get_ifname,
};
