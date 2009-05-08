#ifndef __WINBOND_WB35REG_F_H
#define __WINBOND_WB35REG_F_H

#include "wbhal_s.h"

//====================================
// Interface function declare
//====================================
unsigned char Wb35Reg_initial(  struct hw_data * pHwData );
void Uxx_power_on_procedure(  struct hw_data * pHwData );
void Uxx_power_off_procedure(  struct hw_data * pHwData );
void Uxx_ReadEthernetAddress(  struct hw_data * pHwData );
void Dxx_initial(  struct hw_data * pHwData );
void Mxx_initial(  struct hw_data * pHwData );
void RFSynthesizer_initial(  struct hw_data * pHwData );
//void RFSynthesizer_SwitchingChannel(  struct hw_data * pHwData,  s8 Channel );
void RFSynthesizer_SwitchingChannel(  struct hw_data * pHwData,  ChanInfo Channel );
void BBProcessor_initial(  struct hw_data * pHwData );
void BBProcessor_RateChanging(  struct hw_data * pHwData,  u8 rate ); // 20060613.1
//void RF_RateChanging(  struct hw_data * pHwData,  u8 rate ); // 20060626.5.c Add
u8 RFSynthesizer_SetPowerIndex(  struct hw_data * pHwData,  u8 PowerIndex );
u8 RFSynthesizer_SetMaxim2828_24Power(  struct hw_data *,  u8 index );
u8 RFSynthesizer_SetMaxim2828_50Power(  struct hw_data *,  u8 index );
u8 RFSynthesizer_SetMaxim2827_24Power(  struct hw_data *,  u8 index );
u8 RFSynthesizer_SetMaxim2827_50Power(  struct hw_data *,  u8 index );
u8 RFSynthesizer_SetMaxim2825Power(  struct hw_data *,  u8 index );
u8 RFSynthesizer_SetAiroha2230Power(  struct hw_data *,  u8 index );
u8 RFSynthesizer_SetAiroha7230Power(  struct hw_data *,  u8 index );
u8 RFSynthesizer_SetWinbond242Power(  struct hw_data *,  u8 index );
void GetTxVgaFromEEPROM(  struct hw_data * pHwData );
void EEPROMTxVgaAdjust(  struct hw_data * pHwData ); // 20060619.5 Add

#define RFWriteControlData( _A, _V ) Wb35Reg_Write( _A, 0x0864, _V )

void Wb35Reg_destroy(  struct hw_data * pHwData );

unsigned char Wb35Reg_Read(  struct hw_data * pHwData,  u16 RegisterNo,   u32 * pRegisterValue );
unsigned char Wb35Reg_ReadSync(  struct hw_data * pHwData,  u16 RegisterNo,   u32 * pRegisterValue );
unsigned char Wb35Reg_Write(  struct hw_data * pHwData,  u16 RegisterNo,  u32 RegisterValue );
unsigned char Wb35Reg_WriteSync(  struct hw_data * pHwData,  u16 RegisterNo,  u32 RegisterValue );
unsigned char Wb35Reg_WriteWithCallbackValue(  struct hw_data * pHwData,
								 u16 RegisterNo,
								 u32 RegisterValue,
								 s8 *pValue,
								 s8 Len);
unsigned char Wb35Reg_BurstWrite(  struct hw_data * pHwData,  u16 RegisterNo,  u32 * pRegisterData,  u8 NumberOfData,  u8 Flag );

void Wb35Reg_EP0VM(  struct hw_data * pHwData );
void Wb35Reg_EP0VM_start(  struct hw_data * pHwData );
void Wb35Reg_EP0VM_complete(struct urb *urb);

u32 BitReverse( u32 dwData, u32 DataLength);

void CardGetMulticastBit(   u8 Address[MAC_ADDR_LENGTH],  u8 *Byte,  u8 *Value );
u32 CardComputeCrc(  u8 * Buffer,  u32 Length );

void Wb35Reg_phy_calibration(  struct hw_data * pHwData );
void Wb35Reg_Update(  struct hw_data * pHwData,  u16 RegisterNo,  u32 RegisterValue );
unsigned char adjust_TXVGA_for_iq_mag(  struct hw_data * pHwData );

#endif
