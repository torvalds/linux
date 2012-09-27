#ifndef _RDA5890_DEFS_H_
#define _RDA5890_DEFS_H_

#include <linux/spinlock.h>

#define RDA5890_SDIOWIFI_VER_MAJ     0
#define RDA5890_SDIOWIFI_VER_MIN     3
#define RDA5890_SDIOWIFI_VER_BLD     1

#define WIFI_POWER_MANAGER //if need wifi sleep for power save should open this 


#define WIFI_UNLOCK_SYSTEM
#define GET_SCAN_FROM_NETWORK_INFO

#define USE_MAC_DYNAMIC_ONCE
//#define WIFI_TEST_MODE

#define DEBUG

extern int rda5890_dbg_level;
extern int rda5890_dbg_area;

typedef enum {
	RDA5890_DL_ALL   = 0, 
	RDA5890_DL_CRIT  = 1,
	RDA5890_DL_TRACE = 2,
	RDA5890_DL_NORM  = 3,
	RDA5890_DL_DEBUG = 4,
	RDA5890_DL_VERB  = 5,
} RDA5890_DBG_LEVEL;

#define RDA5890_DA_MAIN            (1 << 0)
#define RDA5890_DA_SDIO            (1 << 1)
#define RDA5890_DA_ETHER           (1 << 2)
#define RDA5890_DA_WID             (1 << 3)
#define RDA5890_DA_WEXT            (1 << 4)
#define RDA5890_DA_TXRX            (1 << 5)
#define RDA5890_DA_PM              (1 << 6)
#define RDA5890_DA_ALL             0x0000007f

#define RDA5890_LOG "RDA5890: "
#ifndef DEBUG
#define DEBUG
#endif

#ifdef DEBUG 
#define RDA5890_DBGLA(area, lvl)                                             \
	(((lvl)<=rda5890_dbg_level) && ((area)&rda5890_dbg_area))
#define RDA5890_DBGLAP(area,lvl, x...)                                       \
	do{                                                                  \
		if (((lvl)<=rda5890_dbg_level) && ((area)&rda5890_dbg_area)) \
			printk(KERN_INFO RDA5890_LOG x );                    \
	}while(0)
#define RDA5890_DBGP(x...)                                                   \
	do{                                                                  \
		printk(KERN_INFO RDA5890_LOG x );                            \
	}while(0)
#else
#define RDA5890_DBGLA(area, lvl)    0
#define RDA5890_DBGLAP(area,lvl, x...)  do {} while (0) 
#define RDA5890_DBGP(x...)  do {} while (0) 
#endif

#define RDA5890_ERRP(fmt, args...)                                          \
	do{                                                                 \
		printk(KERN_ERR RDA5890_LOG "%s: "fmt, __func__, ## args ); \
	}while(0)

#endif

