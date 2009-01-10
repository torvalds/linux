#ifndef __WINBOND_WB35RX_F_H
#define __WINBOND_WB35RX_F_H

#include <net/mac80211.h>
#include "wbhal_s.h"

//====================================
// Interface function declare
//====================================
unsigned char		Wb35Rx_initial(  phw_data_t pHwData );
void		Wb35Rx_destroy(  phw_data_t pHwData );
void		Wb35Rx_stop(  phw_data_t pHwData );
void		Wb35Rx_start(struct ieee80211_hw *hw);

#endif
