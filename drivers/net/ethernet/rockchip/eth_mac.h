#ifndef _ETH_MAC_H_
#define _ETH_MAC_H_
/*
 *  eth_mac/eth_mac.h
 *
 *  Copyright (C) 2001 Russell King.
 *
 * This file is placed under the LGPL.
 *
 *
 * 
 */
//int eth_mac_read_from_IDB(u8 *mac)

int eth_mac_idb(u8 *eth_mac);
int eth_mac_wifi(u8 *eth_mac);
#endif /* _ETH_MAC_H_ */
