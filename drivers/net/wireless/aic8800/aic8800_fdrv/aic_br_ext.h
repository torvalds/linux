/******************************************************************************
 *
 * Copyright(c) 2007 - 2017 Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 *****************************************************************************/
#ifndef _AIC_BR_EXT_H_
#define _AIC_BR_EXT_H_

#define CL_IPV6_PASS	1
#define MACADDRLEN		6
#define WLAN_ETHHDR_LEN		14

#define NAT25_HASH_BITS		4
#define NAT25_HASH_SIZE		(1 << NAT25_HASH_BITS)
#define NAT25_AGEING_TIME	300

#define NDEV_FMT "%s"
#define NDEV_ARG(ndev) ndev->name
#define ADPT_FMT "%s"
//#define ADPT_ARG(adapter) (adapter->pnetdev ? adapter->pnetdev->name : NULL)
#define FUNC_NDEV_FMT "%s(%s)"
#define FUNC_NDEV_ARG(ndev) __func__, ndev->name
#define FUNC_ADPT_FMT "%s(%s)"
//#define FUNC_ADPT_ARG(adapter) __func__, (adapter->pnetdev ? adapter->pnetdev->name : NULL)
#define MAC_FMT "%02x:%02x:%02x:%02x:%02x:%02x"
#define MAC_ARG(x) ((u8 *)(x))[0], ((u8 *)(x))[1], ((u8 *)(x))[2], ((u8 *)(x))[3], ((u8 *)(x))[4], ((u8 *)(x))[5]


#ifdef CL_IPV6_PASS
	#define MAX_NETWORK_ADDR_LEN	17
#else
	#define MAX_NETWORK_ADDR_LEN	11
#endif

struct nat25_network_db_entry {
	struct nat25_network_db_entry	*next_hash;
	struct nat25_network_db_entry	**pprev_hash;
	atomic_t						use_count;
	unsigned char					macAddr[6];
	unsigned long					ageing_timer;
	unsigned char				networkAddr[MAX_NETWORK_ADDR_LEN];
};

enum NAT25_METHOD {
	NAT25_MIN,
	NAT25_CHECK,
	NAT25_INSERT,
	NAT25_LOOKUP,
	NAT25_PARSE,
	NAT25_MAX
};

struct br_ext_info {
	unsigned int	nat25_disable;
	unsigned int	macclone_enable;
	unsigned int	dhcp_bcst_disable;
	int		addPPPoETag;		/* 1: Add PPPoE relay-SID, 0: disable */
	unsigned char	nat25_dmzMac[MACADDRLEN];
	unsigned int	nat25sc_disable;
};

void nat25_db_cleanup(struct rwnx_vif *vif);

#endif /* _AIC_BR_EXT_H_ */
