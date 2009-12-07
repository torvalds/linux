#ifndef __ASM_SH_ETH_H__
#define __ASM_SH_ETH_H__

enum {EDMAC_LITTLE_ENDIAN, EDMAC_BIG_ENDIAN};

struct sh_eth_plat_data {
	int phy;
	int edmac_endian;

	unsigned no_ether_link:1;
	unsigned ether_link_active_low:1;
};

#endif
