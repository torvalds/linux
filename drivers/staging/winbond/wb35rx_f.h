#ifndef __WINBOND_WB35RX_F_H
#define __WINBOND_WB35RX_F_H

#include "wbhal_s.h"

//====================================
// Interface function declare
//====================================
void		Wb35Rx_reset_descriptor(  phw_data_t pHwData );
unsigned char		Wb35Rx_initial(  phw_data_t pHwData );
void		Wb35Rx_destroy(  phw_data_t pHwData );
void		Wb35Rx_stop(  phw_data_t pHwData );
u16		Wb35Rx_indicate(  phw_data_t pHwData );
void		Wb35Rx_adjust(  PDESCRIPTOR pRxDes );
void		Wb35Rx_start(  phw_data_t pHwData );

void		Wb35Rx(  phw_data_t pHwData );
void		Wb35Rx_Complete(struct urb *urb);

#endif
