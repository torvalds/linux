//====================================
// Interface function declare
//====================================
unsigned char Wb35Reg_initial(  phw_data_t pHwData );
void Uxx_power_on_procedure(  phw_data_t pHwData );
void Uxx_power_off_procedure(  phw_data_t pHwData );
void Uxx_ReadEthernetAddress(  phw_data_t pHwData );
void Dxx_initial(  phw_data_t pHwData );
void Mxx_initial(  phw_data_t pHwData );
void RFSynthesizer_initial(  phw_data_t pHwData );
//void RFSynthesizer_SwitchingChannel(  phw_data_t pHwData,  s8 Channel );
void RFSynthesizer_SwitchingChannel(  phw_data_t pHwData,  ChanInfo Channel );
void BBProcessor_initial(  phw_data_t pHwData );
void BBProcessor_RateChanging(  phw_data_t pHwData,  u8 rate ); // 20060613.1
//void RF_RateChanging(  phw_data_t pHwData,  u8 rate ); // 20060626.5.c Add
u8 RFSynthesizer_SetPowerIndex(  phw_data_t pHwData,  u8 PowerIndex );
u8 RFSynthesizer_SetMaxim2828_24Power(  phw_data_t,  u8 index );
u8 RFSynthesizer_SetMaxim2828_50Power(  phw_data_t,  u8 index );
u8 RFSynthesizer_SetMaxim2827_24Power(  phw_data_t,  u8 index );
u8 RFSynthesizer_SetMaxim2827_50Power(  phw_data_t,  u8 index );
u8 RFSynthesizer_SetMaxim2825Power(  phw_data_t,  u8 index );
u8 RFSynthesizer_SetAiroha2230Power(  phw_data_t,  u8 index );
u8 RFSynthesizer_SetAiroha7230Power(  phw_data_t,  u8 index );
u8 RFSynthesizer_SetWinbond242Power(  phw_data_t,  u8 index );
void GetTxVgaFromEEPROM(  phw_data_t pHwData );
void EEPROMTxVgaAdjust(  phw_data_t pHwData ); // 20060619.5 Add

#define RFWriteControlData( _A, _V ) Wb35Reg_Write( _A, 0x0864, _V )

void Wb35Reg_destroy(  phw_data_t pHwData );

unsigned char Wb35Reg_Read(  phw_data_t pHwData,  u16 RegisterNo,   PULONG pRegisterValue );
unsigned char Wb35Reg_ReadSync(  phw_data_t pHwData,  u16 RegisterNo,   PULONG pRegisterValue );
unsigned char Wb35Reg_Write(  phw_data_t pHwData,  u16 RegisterNo,  u32 RegisterValue );
unsigned char Wb35Reg_WriteSync(  phw_data_t pHwData,  u16 RegisterNo,  u32 RegisterValue );
unsigned char Wb35Reg_WriteWithCallbackValue(  phw_data_t pHwData,
								 u16 RegisterNo,
								 u32 RegisterValue,
								 PCHAR pValue,
								 s8	Len);
unsigned char Wb35Reg_BurstWrite(  phw_data_t pHwData,  u16 RegisterNo,  PULONG pRegisterData,  u8 NumberOfData,  u8 Flag );

void Wb35Reg_EP0VM(  phw_data_t pHwData );
void Wb35Reg_EP0VM_start(  phw_data_t pHwData );
void Wb35Reg_EP0VM_complete(  PURB pUrb );

u32 BitReverse( u32 dwData, u32 DataLength);

void CardGetMulticastBit(   u8 Address[MAC_ADDR_LENGTH],  u8 *Byte,  u8 *Value );
u32 CardComputeCrc(  PUCHAR Buffer,  u32 Length );

void Wb35Reg_phy_calibration(  phw_data_t pHwData );
void Wb35Reg_Update(  phw_data_t pHwData,  u16 RegisterNo,  u32 RegisterValue );
unsigned char adjust_TXVGA_for_iq_mag(  phw_data_t pHwData );


