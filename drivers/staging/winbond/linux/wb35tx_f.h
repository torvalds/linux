//====================================
// Interface function declare
//====================================
unsigned char Wb35Tx_initial(	 phw_data_t pHwData );
void Wb35Tx_destroy(  phw_data_t pHwData );
unsigned char Wb35Tx_get_tx_buffer(  phw_data_t pHwData,  PUCHAR *pBuffer );

void Wb35Tx_EP2VM(  phw_data_t pHwData );
void Wb35Tx_EP2VM_start(  phw_data_t pHwData );
void Wb35Tx_EP2VM_complete(  PURB purb );

void Wb35Tx_start(  phw_data_t pHwData );
void Wb35Tx_stop(  phw_data_t pHwData );
void Wb35Tx(  phw_data_t pHwData );
void Wb35Tx_complete(  PURB purb );
void Wb35Tx_reset_descriptor(  phw_data_t pHwData );

void Wb35Tx_CurrentTime(  phw_data_t pHwData,  u32 TimeCount );


