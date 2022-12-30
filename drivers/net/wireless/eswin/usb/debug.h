

#ifndef _ECRNX_DBG
#define _ECRNX_DBG
#include <net/mac80211.h>

//#define CONFIG_HIF_PRINT_TX_DATA
#define CONFIG_USE_TXQ

//#define CONFIG_NRC_HIF_PRINT_FLOW_CONTROL
//#define CONFIG_SHOW_TX_SPEED
//#define CONFIG_SHOW_RX_SPEED

#define FRAME_SIZE         512

void eswin_dump_wim(struct sk_buff *skb);
void eswin_init_debugfs(struct eswin *tr);
void sdio_rx_tx_test_schedule(void);
void ecrnx_hw_set(void* init_ecrnx_hw);

#endif
