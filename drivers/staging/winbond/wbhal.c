#include "sysdef.h"
#include "wbhal_f.h"
#include "wblinux_f.h"

void hal_set_ethernet_address( struct hw_data * pHwData, u8 *current_address )
{
	u32 ltmp[2];

	if( pHwData->SurpriseRemove ) return;

	memcpy( pHwData->CurrentMacAddress, current_address, ETH_ALEN );

	ltmp[0]= cpu_to_le32( *(u32 *)pHwData->CurrentMacAddress );
	ltmp[1]= cpu_to_le32( *(u32 *)(pHwData->CurrentMacAddress + 4) ) & 0xffff;

	Wb35Reg_BurstWrite( pHwData, 0x03e8, ltmp, 2, AUTO_INCREMENT );
}

void hal_get_permanent_address( struct hw_data * pHwData, u8 *pethernet_address )
{
	if( pHwData->SurpriseRemove ) return;

	memcpy( pethernet_address, pHwData->PermanentMacAddress, 6 );
}

//---------------------------------------------------------------------------------------------------
void hal_set_beacon_period(  struct hw_data * pHwData,  u16 beacon_period )
{
	u32	tmp;

	if( pHwData->SurpriseRemove ) return;

	pHwData->BeaconPeriod = beacon_period;
	tmp = pHwData->BeaconPeriod << 16;
	tmp |= pHwData->ProbeDelay;
	Wb35Reg_Write( pHwData, 0x0848, tmp );
}


static void hal_set_current_channel_ex(  struct hw_data * pHwData,  ChanInfo channel )
{
	struct wb35_reg *reg = &pHwData->reg;

	if( pHwData->SurpriseRemove )
		return;

	printk("Going to channel: %d/%d\n", channel.band, channel.ChanNo);

	RFSynthesizer_SwitchingChannel( pHwData, channel );// Switch channel
	pHwData->Channel = channel.ChanNo;
	pHwData->band = channel.band;
	#ifdef _PE_STATE_DUMP_
	printk("Set channel is %d, band =%d\n", pHwData->Channel, pHwData->band);
	#endif
	reg->M28_MacControl &= ~0xff; // Clean channel information field
	reg->M28_MacControl |= channel.ChanNo;
	Wb35Reg_WriteWithCallbackValue( pHwData, 0x0828, reg->M28_MacControl,
					(s8 *)&channel, sizeof(ChanInfo));
}
//---------------------------------------------------------------------------------------------------
void hal_set_current_channel(  struct hw_data * pHwData,  ChanInfo channel )
{
	hal_set_current_channel_ex( pHwData, channel );
}
//---------------------------------------------------------------------------------------------------
void hal_set_accept_broadcast(  struct hw_data * pHwData,  u8 enable )
{
	struct wb35_reg *reg = &pHwData->reg;

	if( pHwData->SurpriseRemove ) return;

	reg->M00_MacControl &= ~0x02000000;//The HW value

	if (enable)
		reg->M00_MacControl |= 0x02000000;//The HW value

	Wb35Reg_Write( pHwData, 0x0800, reg->M00_MacControl );
}

//for wep key error detection, we need to accept broadcast packets to be received temporary.
void hal_set_accept_promiscuous( struct hw_data * pHwData,  u8 enable)
{
	struct wb35_reg *reg = &pHwData->reg;

	if (pHwData->SurpriseRemove) return;
	if (enable) {
		reg->M00_MacControl |= 0x00400000;
		Wb35Reg_Write( pHwData, 0x0800, reg->M00_MacControl );
	} else {
		reg->M00_MacControl&=~0x00400000;
		Wb35Reg_Write( pHwData, 0x0800, reg->M00_MacControl );
	}
}

void hal_set_accept_multicast(  struct hw_data * pHwData,  u8 enable )
{
	struct wb35_reg *reg = &pHwData->reg;

	if( pHwData->SurpriseRemove ) return;

	reg->M00_MacControl &= ~0x01000000;//The HW value
	if (enable)  reg->M00_MacControl |= 0x01000000;//The HW value
	Wb35Reg_Write( pHwData, 0x0800, reg->M00_MacControl );
}

void hal_set_accept_beacon(  struct hw_data * pHwData,  u8 enable )
{
	struct wb35_reg *reg = &pHwData->reg;

	if( pHwData->SurpriseRemove ) return;

	// 20040108 debug
	if( !enable )//Due to SME and MLME are not suitable for 35
		return;

	reg->M00_MacControl &= ~0x04000000;//The HW value
	if( enable )
		reg->M00_MacControl |= 0x04000000;//The HW value

	Wb35Reg_Write( pHwData, 0x0800, reg->M00_MacControl );
}
//---------------------------------------------------------------------------------------------------

void hal_stop(  struct hw_data * pHwData )
{
	struct wb35_reg *reg = &pHwData->reg;

	pHwData->Wb35Rx.rx_halt = 1;
	Wb35Rx_stop( pHwData );

	pHwData->Wb35Tx.tx_halt = 1;
	Wb35Tx_stop( pHwData );

	reg->D00_DmaControl &= ~0xc0000000;//Tx Off, Rx Off
	Wb35Reg_Write( pHwData, 0x0400, reg->D00_DmaControl );
}

unsigned char hal_idle(struct hw_data * pHwData)
{
	struct wb35_reg *reg = &pHwData->reg;
	struct wb_usb *pWbUsb = &pHwData->WbUsb;

	if( !pHwData->SurpriseRemove && ( pWbUsb->DetectCount || reg->EP0vm_state!=VM_STOP ) )
		return false;

	return true;
}

void hal_set_radio_mode( struct hw_data * pHwData,  unsigned char radio_off)
{
	struct wb35_reg *reg = &pHwData->reg;

	if( pHwData->SurpriseRemove ) return;

	if (radio_off)	//disable Baseband receive off
	{
		pHwData->CurrentRadioSw = 1; // off
		reg->M24_MacControl &= 0xffffffbf;
	}
	else
	{
		pHwData->CurrentRadioSw = 0; // on
		reg->M24_MacControl |= 0x00000040;
	}
	Wb35Reg_Write( pHwData, 0x0824, reg->M24_MacControl );
}

u8 hal_get_antenna_number(  struct hw_data * pHwData )
{
	struct wb35_reg *reg = &pHwData->reg;

	if ((reg->BB2C & BIT(11)) == 0)
		return 0;
	else
		return 1;
}

//----------------------------------------------------------------------------------------------------
//0 : radio on; 1: radio off
u8 hal_get_hw_radio_off(  struct hw_data * pHwData )
{
	struct wb35_reg *reg = &pHwData->reg;

	if( pHwData->SurpriseRemove ) return 1;

	//read the bit16 of register U1B0
	Wb35Reg_Read( pHwData, 0x3b0, &reg->U1B0 );
	if ((reg->U1B0 & 0x00010000)) {
		pHwData->CurrentRadioHw = 1;
		return 1;
	} else {
		pHwData->CurrentRadioHw = 0;
		return 0;
	}
}

unsigned char hal_get_dxx_reg(  struct hw_data * pHwData,  u16 number,  u32 * pValue )
{
	if( number < 0x1000 )
		number += 0x1000;
	return Wb35Reg_ReadSync( pHwData, number, pValue );
}

unsigned char hal_set_dxx_reg(  struct hw_data * pHwData,  u16 number,  u32 value )
{
	unsigned char	ret;

	if( number < 0x1000 )
		number += 0x1000;
	ret = Wb35Reg_WriteSync( pHwData, number, value );
	return ret;
}

void hal_set_rf_power(struct hw_data * pHwData, u8 PowerIndex)
{
	RFSynthesizer_SetPowerIndex( pHwData, PowerIndex );
}
