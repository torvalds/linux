#ifndef __WINBOND_WB35TX_F_H
#define __WINBOND_WB35TX_F_H

#include "adapter.h"
#include "wbhal_f.h"

//====================================
// Interface function declare
//====================================
unsigned char Wb35Tx_initial(	 phw_data_t pHwData );
void Wb35Tx_destroy(  phw_data_t pHwData );
unsigned char Wb35Tx_get_tx_buffer(  phw_data_t pHwData,  u8 **pBuffer );

void Wb35Tx_EP2VM(struct wb35_adapter *adapter);
void Wb35Tx_EP2VM_start(struct wb35_adapter *adapter);
void Wb35Tx_EP2VM_complete(struct urb *urb);

void Wb35Tx_start(struct wb35_adapter *adapter);
void Wb35Tx_stop(  phw_data_t pHwData );
void Wb35Tx(struct wb35_adapter *adapter);
void Wb35Tx_complete(struct urb *urb);
void Wb35Tx_reset_descriptor(  phw_data_t pHwData );

void Wb35Tx_CurrentTime(struct wb35_adapter *adapter,  u32 TimeCount);

#endif
