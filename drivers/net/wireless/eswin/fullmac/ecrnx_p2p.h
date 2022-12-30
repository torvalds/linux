/**
 ****************************************************************************************
 *
 * @file rwnx_p2p.h
 *
 * @brief P2P Listen function declarations
 *
 * 
 *
 ****************************************************************************************
 */

#ifndef _ECRNX_P2P_H_
#define _ECRNX_P2P_H_

/**
 * INCLUDE FILES
 ****************************************************************************************
 */
#include <net/cfg80211.h>

/**
 * DEFINES
 ****************************************************************************************
 */

/**
 * TYPE DEFINITIONS
 ****************************************************************************************
 */
struct ecrnx_p2p_listen {
	struct ecrnx_vif *ecrnx_vif;
	struct ieee80211_channel listen_chan;
	bool listen_started;
	bool pending_req;
	u64 cookie;
	struct wireless_dev *pending_listen_wdev;
	unsigned int listen_duration;
	struct timer_list listen_timer; /* listen duration */
	struct work_struct listen_expired_work; /* listen expire */

    wait_queue_head_t   rxdataq;
    int                 rxdatas;
};

/**
 * FUNCTION DECLARATIONS
 ****************************************************************************************
 */
void ecrnx_p2p_listen_init(struct ecrnx_p2p_listen *p2p_listen);
void ecrnx_p2p_listen_expired(struct work_struct *work);

/**
 ****************************************************************************************
 * @brief TODO [LT]
 ****************************************************************************************
 */


#endif /* _RWNX_P2P_H_ */
