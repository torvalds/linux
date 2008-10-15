#ifndef __ASM_SH_ETH_H__
#define __ASM_SH_ETH_H__

enum {EDMAC_LITTLE_ENDIAN, EDMAC_BIG_ENDIAN};

struct sh_eth_plat_data {
	int phy;
	int edmac_endian;
};

#endif
