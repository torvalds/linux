#ifndef __WINBOND_WB35RX_F_H
#define __WINBOND_WB35RX_F_H

#include <net/mac80211.h>
#include "wbhal.h"

//====================================
// Interface function declare
//====================================
unsigned char		Wb35Rx_initial(  struct hw_data * pHwData );
void		Wb35Rx_destroy(  struct hw_data * pHwData );
void		Wb35Rx_stop(  struct hw_data * pHwData );
void		Wb35Rx_start(struct ieee80211_hw *hw);

#endif
