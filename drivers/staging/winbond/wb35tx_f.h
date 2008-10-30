#ifndef __WINBOND_WB35TX_F_H
#define __WINBOND_WB35TX_F_H

#include "core.h"
#include "wbhal_f.h"

//====================================
// Interface function declare
//====================================
unsigned char Wb35Tx_initial(	 phw_data_t pHwData );
void Wb35Tx_destroy(  phw_data_t pHwData );
unsigned char Wb35Tx_get_tx_buffer(  phw_data_t pHwData,  u8 **pBuffer );

void Wb35Tx_EP2VM_start(struct wbsoft_priv *adapter);

void Wb35Tx_start(struct wbsoft_priv *adapter);
void Wb35Tx_stop(  phw_data_t pHwData );

void Wb35Tx_CurrentTime(struct wbsoft_priv *adapter,  u32 TimeCount);

#endif
