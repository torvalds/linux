#include "os_common.h"

void hal_get_ethernet_address( phw_data_t pHwData, u8 *current_address )
{
	if( pHwData->SurpriseRemove ) return;

	memcpy( current_address, pHwData->CurrentMacAddress, ETH_LENGTH_OF_ADDRESS );
}

void hal_set_ethernet_address( phw_data_t pHwData, u8 *current_address )
{
	u32 ltmp[2];

	if( pHwData->SurpriseRemove ) return;

	memcpy( pHwData->CurrentMacAddress, current_address, ETH_LENGTH_OF_ADDRESS );

	ltmp[0]= cpu_to_le32( *(u32 *)pHwData->CurrentMacAddress );
	ltmp[1]= cpu_to_le32( *(u32 *)(pHwData->CurrentMacAddress + 4) ) & 0xffff;

	Wb35Reg_BurstWrite( pHwData, 0x03e8, ltmp, 2, AUTO_INCREMENT );
}

void hal_get_permanent_address( phw_data_t pHwData, u8 *pethernet_address )
{
	if( pHwData->SurpriseRemove ) return;

	memcpy( pethernet_address, pHwData->PermanentMacAddress, 6 );
}

u8 hal_init_hardware(phw_data_t pHwData, struct wb35_adapter * adapter)
{
	u16 SoftwareSet;
	pHwData->adapter = adapter;

	// Initial the variable
	pHwData->MaxReceiveLifeTime = DEFAULT_MSDU_LIFE_TIME; // Setting Rx maximum MSDU life time
	pHwData->FragmentThreshold = DEFAULT_FRAGMENT_THRESHOLD; // Setting default fragment threshold

	pHwData->InitialResource = 1;
	if( Wb35Reg_initial(pHwData)) {
		pHwData->InitialResource = 2;
		if (Wb35Tx_initial(pHwData)) {
			pHwData->InitialResource = 3;
			if (Wb35Rx_initial(pHwData)) {
				pHwData->InitialResource = 4;
				OS_TIMER_INITIAL( &pHwData->LEDTimer, hal_led_control, pHwData );
				OS_TIMER_SET( &pHwData->LEDTimer, 1000 ); // 20060623

				//
				// For restrict to vendor's hardware
				//
				SoftwareSet = hal_software_set( pHwData );

				#ifdef Vendor2
				// Try to make sure the EEPROM contain
				SoftwareSet >>= 8;
				if( SoftwareSet != 0x82 )
					return false;
				#endif

				Wb35Rx_start( pHwData );
				Wb35Tx_EP2VM_start( pHwData );

				return true;
			}
		}
	}

	pHwData->SurpriseRemove = 1;
	return false;
}


void hal_halt(phw_data_t pHwData, void *ppa_data)
{
	switch( pHwData->InitialResource )
	{
		case 4:
		case 3: OS_TIMER_CANCEL( &pHwData->LEDTimer, &cancel );
			msleep(100); // Wait for Timer DPC exit 940623.2
			Wb35Rx_destroy( pHwData ); // Release the Rx
		case 2: Wb35Tx_destroy( pHwData ); // Release the Tx
		case 1: Wb35Reg_destroy( pHwData ); // Release the Wb35 Regisster resources
	}
}

//---------------------------------------------------------------------------------------------------
void hal_set_rates(phw_data_t pHwData, u8 *pbss_rates,
		   u8 length, unsigned char basic_rate_set)
{
	struct wb35_reg *reg = &pHwData->reg;
	u32		tmp, tmp1;
	u8		Rate[12]={ 2, 4, 11, 22, 12, 18, 24, 36, 48, 72, 96, 108 };
	u8		SupportedRate[16];
	u8		i, j, k, Count1, Count2, Byte;

	if( pHwData->SurpriseRemove ) return;

	if (basic_rate_set) {
		reg->M28_MacControl &= ~0x000fff00;
		tmp1 = 0x00000100;
	} else {
		reg->M28_MacControl &= ~0xfff00000;
		tmp1 = 0x00100000;
	}

	tmp = 0;
	for (i=0; i<length; i++) {
		Byte = pbss_rates[i] & 0x7f;
		for (j=0; j<12; j++) {
			if( Byte == Rate[j] )
				break;
		}

		if (j < 12)
			tmp |= (tmp1<<j);
	}

	reg->M28_MacControl |= tmp;
	Wb35Reg_Write( pHwData, 0x0828, reg->M28_MacControl );

	// 930206.2.c M78 setting
	j = k = Count1 = Count2 = 0;
	memset( SupportedRate, 0, 16 );
	tmp = 0x00100000;
	tmp1 = 0x00000100;
	for (i=0; i<12; i++) { // Get the supported rate
		if (tmp & reg->M28_MacControl) {
			SupportedRate[j] = Rate[i];

			if (tmp1 & reg->M28_MacControl)
				SupportedRate[j] |= 0x80;

			if (k)
				Count2++;
			else
				Count1++;

			j++;
		}

		if (i==4 && k==0) {
			if( !(reg->M28_MacControl & 0x000ff000) ) // if basic rate in 11g domain)
			{
				k = 1;
				j = 8;
			}
		}

		tmp <<= 1;
		tmp1 <<= 1;
	}

	// Fill data into support rate until buffer full
	//---20060926 add by anson's endian
	for (i=0; i<4; i++)
		*(u32 *)(SupportedRate+(i<<2)) = cpu_to_le32( *(u32 *)(SupportedRate+(i<<2)) );
	//--- end 20060926 add by anson's endian
	Wb35Reg_BurstWrite( pHwData,0x087c, (u32 *)SupportedRate, 4, AUTO_INCREMENT );
	reg->M7C_MacControl = ((u32 *)SupportedRate)[0];
	reg->M80_MacControl = ((u32 *)SupportedRate)[1];
	reg->M84_MacControl = ((u32 *)SupportedRate)[2];
	reg->M88_MacControl = ((u32 *)SupportedRate)[3];

	// Fill length
	tmp = Count1<<28 | Count2<<24;
	reg->M78_ERPInformation &= ~0xff000000;
	reg->M78_ERPInformation |= tmp;
	Wb35Reg_Write( pHwData, 0x0878, reg->M78_ERPInformation );
}


//---------------------------------------------------------------------------------------------------
void hal_set_beacon_period(  phw_data_t pHwData,  u16 beacon_period )
{
	u32	tmp;

	if( pHwData->SurpriseRemove ) return;

	pHwData->BeaconPeriod = beacon_period;
	tmp = pHwData->BeaconPeriod << 16;
	tmp |= pHwData->ProbeDelay;
	Wb35Reg_Write( pHwData, 0x0848, tmp );
}


void hal_set_current_channel_ex(  phw_data_t pHwData,  ChanInfo channel )
{
	struct wb35_reg *reg = &pHwData->reg;

	if( pHwData->SurpriseRemove )
		return;

	printk("Going to channel: %d/%d\n", channel.band, channel.ChanNo);

	RFSynthesizer_SwitchingChannel( pHwData, channel );// Switch channel
	pHwData->Channel = channel.ChanNo;
	pHwData->band = channel.band;
	#ifdef _PE_STATE_DUMP_
	WBDEBUG(("Set channel is %d, band =%d\n", pHwData->Channel, pHwData->band));
	#endif
	reg->M28_MacControl &= ~0xff; // Clean channel information field
	reg->M28_MacControl |= channel.ChanNo;
	Wb35Reg_WriteWithCallbackValue( pHwData, 0x0828, reg->M28_MacControl,
					(s8 *)&channel, sizeof(ChanInfo));
}
//---------------------------------------------------------------------------------------------------
void hal_set_current_channel(  phw_data_t pHwData,  ChanInfo channel )
{
	hal_set_current_channel_ex( pHwData, channel );
}
//---------------------------------------------------------------------------------------------------
void hal_get_current_channel(  phw_data_t pHwData,  ChanInfo *channel )
{
	channel->ChanNo = pHwData->Channel;
	channel->band = pHwData->band;
}
//---------------------------------------------------------------------------------------------------
void hal_set_accept_broadcast(  phw_data_t pHwData,  u8 enable )
{
	struct wb35_reg *reg = &pHwData->reg;

	if( pHwData->SurpriseRemove ) return;

	reg->M00_MacControl &= ~0x02000000;//The HW value

	if (enable)
		reg->M00_MacControl |= 0x02000000;//The HW value

	Wb35Reg_Write( pHwData, 0x0800, reg->M00_MacControl );
}

//for wep key error detection, we need to accept broadcast packets to be received temporary.
void hal_set_accept_promiscuous( phw_data_t pHwData,  u8 enable)
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

void hal_set_accept_multicast(  phw_data_t pHwData,  u8 enable )
{
	struct wb35_reg *reg = &pHwData->reg;

	if( pHwData->SurpriseRemove ) return;

	reg->M00_MacControl &= ~0x01000000;//The HW value
	if (enable)  reg->M00_MacControl |= 0x01000000;//The HW value
	Wb35Reg_Write( pHwData, 0x0800, reg->M00_MacControl );
}

void hal_set_accept_beacon(  phw_data_t pHwData,  u8 enable )
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
void hal_set_multicast_address( phw_data_t pHwData, u8 *address, u8 number )
{
	struct wb35_reg *reg = &pHwData->reg;
	u8		Byte, Bit;

	if( pHwData->SurpriseRemove ) return;

	//Erases and refills the card multicast registers. Used when an address
	//    has been deleted and all bits must be recomputed.
	reg->M04_MulticastAddress1 = 0;
	reg->M08_MulticastAddress2 = 0;

	while( number )
	{
		number--;
		CardGetMulticastBit( (address+(number*ETH_LENGTH_OF_ADDRESS)), &Byte, &Bit);
		reg->Multicast[Byte] |= Bit;
	}

	// Updating register
	Wb35Reg_BurstWrite( pHwData, 0x0804, (u32 *)reg->Multicast, 2, AUTO_INCREMENT );
}
//---------------------------------------------------------------------------------------------------
u8 hal_get_accept_beacon(  phw_data_t pHwData )
{
	struct wb35_reg *reg = &pHwData->reg;

	if( pHwData->SurpriseRemove ) return 0;

	if( reg->M00_MacControl & 0x04000000 )
		return 1;
	else
		return 0;
}

unsigned char hal_reset_hardware( phw_data_t pHwData, void* ppa )
{
	// Not implement yet
	return true;
}

void hal_stop(  phw_data_t pHwData )
{
	struct wb35_reg *reg = &pHwData->reg;

	pHwData->Wb35Rx.rx_halt = 1;
	Wb35Rx_stop( pHwData );

	pHwData->Wb35Tx.tx_halt = 1;
	Wb35Tx_stop( pHwData );

	reg->D00_DmaControl &= ~0xc0000000;//Tx Off, Rx Off
	Wb35Reg_Write( pHwData, 0x0400, reg->D00_DmaControl );
}

unsigned char hal_idle(phw_data_t pHwData)
{
	struct wb35_reg *reg = &pHwData->reg;
	PWBUSB	pWbUsb = &pHwData->WbUsb;

	if( !pHwData->SurpriseRemove && ( pWbUsb->DetectCount || reg->EP0vm_state!=VM_STOP ) )
		return false;

	return true;
}
//---------------------------------------------------------------------------------------------------
void hal_set_cwmin(  phw_data_t pHwData,  u8	cwin_min )
{
	struct wb35_reg *reg = &pHwData->reg;

	if( pHwData->SurpriseRemove ) return;

	pHwData->cwmin = cwin_min;
	reg->M2C_MacControl &= ~0x7c00;	//bit 10 ~ 14
	reg->M2C_MacControl |= (pHwData->cwmin<<10);
	Wb35Reg_Write( pHwData, 0x082c, reg->M2C_MacControl );
}

s32 hal_get_rssi(  phw_data_t pHwData,  u32 *HalRssiArry,  u8 Count )
{
	struct wb35_reg *reg = &pHwData->reg;
	R01_DESCRIPTOR	r01;
	s32 ltmp = 0, tmp;
	u8	i;

	if( pHwData->SurpriseRemove ) return -200;
	if( Count > MAX_ACC_RSSI_COUNT ) // Because the TS may use this funtion
		Count = MAX_ACC_RSSI_COUNT;

	// RSSI = C1 + C2 * (agc_state[7:0] + offset_map(lna_state[1:0]))
	// C1 = -195, C2 = 0.66 = 85/128
	for (i=0; i<Count; i++)
	{
		r01.value = HalRssiArry[i];
		tmp = ((( r01.R01_AGC_state + reg->LNAValue[r01.R01_LNA_state]) * 85 ) >>7 ) - 195;
		ltmp += tmp;
	}
	ltmp /= Count;
	if( pHwData->phy_type == RF_AIROHA_2230 ) ltmp -= 5; // 10;
	if( pHwData->phy_type == RF_AIROHA_2230S ) ltmp -= 5; // 10; 20060420 Add this

	//if( ltmp < -200 ) ltmp = -200;
	if( ltmp < -110 ) ltmp = -110;// 1.0.24.0 For NJRC

	return ltmp;
}
//----------------------------------------------------------------------------------------------------
s32 hal_get_rssi_bss(  phw_data_t pHwData,  u16 idx,  u8 Count )
{
	struct wb35_reg *reg = &pHwData->reg;
	R01_DESCRIPTOR	r01;
	s32 ltmp = 0, tmp;
	u8	i, j;
	struct wb35_adapter *	adapter = pHwData->adapter;
//	u32 *HalRssiArry = psBSS(idx)->HalRssi;

	if( pHwData->SurpriseRemove ) return -200;
	if( Count > MAX_ACC_RSSI_COUNT ) // Because the TS may use this funtion
		Count = MAX_ACC_RSSI_COUNT;

	// RSSI = C1 + C2 * (agc_state[7:0] + offset_map(lna_state[1:0]))
	// C1 = -195, C2 = 0.66 = 85/128
#if 0
	for (i=0; i<Count; i++)
	{
		r01.value = HalRssiArry[i];
		tmp = ((( r01.R01_AGC_state + reg->LNAValue[r01.R01_LNA_state]) * 85 ) >>7 ) - 195;
		ltmp += tmp;
	}
#else
	if (psBSS(idx)->HalRssiIndex == 0)
		psBSS(idx)->HalRssiIndex = MAX_ACC_RSSI_COUNT;
	j = (u8)psBSS(idx)->HalRssiIndex-1;

	for (i=0; i<Count; i++)
	{
		r01.value = psBSS(idx)->HalRssi[j];
		tmp = ((( r01.R01_AGC_state + reg->LNAValue[r01.R01_LNA_state]) * 85 ) >>7 ) - 195;
		ltmp += tmp;
		if (j == 0)
		{
			j = MAX_ACC_RSSI_COUNT;
		}
		j--;
	}
#endif
	ltmp /= Count;
	if( pHwData->phy_type == RF_AIROHA_2230 ) ltmp -= 5; // 10;
	if( pHwData->phy_type == RF_AIROHA_2230S ) ltmp -= 5; // 10; 20060420 Add this

	//if( ltmp < -200 ) ltmp = -200;
	if( ltmp < -110 ) ltmp = -110;// 1.0.24.0 For NJRC

	return ltmp;
}

//---------------------------------------------------------------------------
void hal_led_control_1a(  phw_data_t pHwData )
{
	hal_led_control( NULL, pHwData, NULL, NULL );
}

void hal_led_control(  void* S1,  phw_data_t pHwData,  void* S3,  void* S4 )
{
	struct wb35_adapter *	adapter = pHwData->adapter;
	struct wb35_reg *reg = &pHwData->reg;
	u32	LEDSet = (pHwData->SoftwareSet & HAL_LED_SET_MASK) >> HAL_LED_SET_SHIFT;
	u8	LEDgray[20] = { 0,3,4,6,8,10,11,12,13,14,15,14,13,12,11,10,8,6,4,2 };
	u8	LEDgray2[30] = { 7,8,9,10,11,12,13,14,15,0,0,0,0,0,0,0,0,0,0,0,0,0,15,14,13,12,11,10,9,8 };
	u32	TimeInterval = 500, ltmp, ltmp2;
        ltmp=0;

	if( pHwData->SurpriseRemove ) return;

	if( pHwData->LED_control ) {
		ltmp2 = pHwData->LED_control & 0xff;
		if( ltmp2 == 5 ) // 5 is WPS mode
		{
			TimeInterval = 100;
			ltmp2 = (pHwData->LED_control>>8) & 0xff;
			switch( ltmp2 )
			{
				case 1: // [0.2 On][0.1 Off]...
					pHwData->LED_Blinking %= 3;
					ltmp = 0x1010; // Led 1 & 0 Green and Red
					if( pHwData->LED_Blinking == 2 ) // Turn off
						ltmp = 0;
					break;
				case 2: // [0.1 On][0.1 Off]...
					pHwData->LED_Blinking %= 2;
					ltmp = 0x0010; // Led 0 red color
					if( pHwData->LED_Blinking ) // Turn off
						ltmp = 0;
					break;
				case 3: // [0.1 On][0.1 Off][0.1 On][0.1 Off][0.1 On][0.1 Off][0.1 On][0.1 Off][0.1 On][0.1 Off][0.5 Off]...
					pHwData->LED_Blinking %= 15;
					ltmp = 0x0010; // Led 0 red color
					if( (pHwData->LED_Blinking >= 9) || (pHwData->LED_Blinking%2) ) // Turn off 0.6 sec
						ltmp = 0;
					break;
				case 4: // [300 On][ off ]
					ltmp = 0x1000; // Led 1 Green color
					if( pHwData->LED_Blinking >= 3000 )
						ltmp = 0; // led maybe on after 300sec * 32bit counter overlap.
					break;
			}
			pHwData->LED_Blinking++;

			reg->U1BC_LEDConfigure = ltmp;
			if( LEDSet != 7 ) // Only 111 mode has 2 LEDs on PCB.
			{
				reg->U1BC_LEDConfigure |= (ltmp &0xff)<<8; // Copy LED result to each LED control register
				reg->U1BC_LEDConfigure |= (ltmp &0xff00)>>8;
			}
			Wb35Reg_Write( pHwData, 0x03bc, reg->U1BC_LEDConfigure );
		}
	}
	else if( pHwData->CurrentRadioSw || pHwData->CurrentRadioHw ) // If radio off
	{
		if( reg->U1BC_LEDConfigure & 0x1010 )
		{
			reg->U1BC_LEDConfigure &= ~0x1010;
			Wb35Reg_Write( pHwData, 0x03bc, reg->U1BC_LEDConfigure );
		}
	}
	else
	{
		switch( LEDSet )
		{
			case 4: // [100] Only 1 Led be placed on PCB and use pin 21 of IC. Use LED_0 for showing
				if( !pHwData->LED_LinkOn ) // Blink only if not Link On
				{
					// Blinking if scanning is on progress
					if( pHwData->LED_Scanning )
					{
						if( pHwData->LED_Blinking == 0 )
						{
							reg->U1BC_LEDConfigure |= 0x10;
							Wb35Reg_Write( pHwData, 0x03bc, reg->U1BC_LEDConfigure ); // LED_0 On
							pHwData->LED_Blinking = 1;
							TimeInterval = 300;
						}
						else
						{
							reg->U1BC_LEDConfigure &= ~0x10;
							Wb35Reg_Write( pHwData, 0x03bc, reg->U1BC_LEDConfigure ); // LED_0 Off
							pHwData->LED_Blinking = 0;
							TimeInterval = 300;
						}
					}
					else
					{
						//Turn Off LED_0
						if( reg->U1BC_LEDConfigure & 0x10 )
						{
							reg->U1BC_LEDConfigure &= ~0x10;
							Wb35Reg_Write( pHwData, 0x03bc, reg->U1BC_LEDConfigure ); // LED_0 Off
						}
					}
				}
				else
				{
					// Turn On LED_0
					if( (reg->U1BC_LEDConfigure & 0x10) == 0 )
					{
						reg->U1BC_LEDConfigure |= 0x10;
						Wb35Reg_Write( pHwData, 0x03bc, reg->U1BC_LEDConfigure ); // LED_0 Off
					}
				}
				break;

			case 6: // [110] Only 1 Led be placed on PCB and use pin 21 of IC. Use LED_0 for showing
				if( !pHwData->LED_LinkOn ) // Blink only if not Link On
				{
					// Blinking if scanning is on progress
					if( pHwData->LED_Scanning )
					{
						if( pHwData->LED_Blinking == 0 )
						{
							reg->U1BC_LEDConfigure &= ~0xf;
							reg->U1BC_LEDConfigure |= 0x10;
							Wb35Reg_Write( pHwData, 0x03bc, reg->U1BC_LEDConfigure ); // LED_0 On
							pHwData->LED_Blinking = 1;
							TimeInterval = 300;
						}
						else
						{
							reg->U1BC_LEDConfigure &= ~0x1f;
							Wb35Reg_Write( pHwData, 0x03bc, reg->U1BC_LEDConfigure ); // LED_0 Off
							pHwData->LED_Blinking = 0;
							TimeInterval = 300;
						}
					}
					else
					{
						// 20060901 Gray blinking if in disconnect state and not scanning
						ltmp = reg->U1BC_LEDConfigure;
						reg->U1BC_LEDConfigure &= ~0x1f;
						if( LEDgray2[(pHwData->LED_Blinking%30)] )
						{
							reg->U1BC_LEDConfigure |= 0x10;
							reg->U1BC_LEDConfigure |= LEDgray2[ (pHwData->LED_Blinking%30) ];
						}
						pHwData->LED_Blinking++;
						if( reg->U1BC_LEDConfigure != ltmp )
							Wb35Reg_Write( pHwData, 0x03bc, reg->U1BC_LEDConfigure ); // LED_0 Off
						TimeInterval = 100;
					}
				}
				else
				{
					// Turn On LED_0
					if( (reg->U1BC_LEDConfigure & 0x10) == 0 )
					{
						reg->U1BC_LEDConfigure |= 0x10;
						Wb35Reg_Write( pHwData, 0x03bc, reg->U1BC_LEDConfigure ); // LED_0 Off
					}
				}
				break;

			case 5: // [101] Only 1 Led be placed on PCB and use LED_1 for showing
				if( !pHwData->LED_LinkOn ) // Blink only if not Link On
				{
					// Blinking if scanning is on progress
					if( pHwData->LED_Scanning )
					{
						if( pHwData->LED_Blinking == 0 )
						{
							reg->U1BC_LEDConfigure |= 0x1000;
							Wb35Reg_Write( pHwData, 0x03bc, reg->U1BC_LEDConfigure ); // LED_1 On
							pHwData->LED_Blinking = 1;
							TimeInterval = 300;
						}
						else
						{
							reg->U1BC_LEDConfigure &= ~0x1000;
							Wb35Reg_Write( pHwData, 0x03bc, reg->U1BC_LEDConfigure ); // LED_1 Off
							pHwData->LED_Blinking = 0;
							TimeInterval = 300;
						}
					}
					else
					{
						//Turn Off LED_1
						if( reg->U1BC_LEDConfigure & 0x1000 )
						{
							reg->U1BC_LEDConfigure &= ~0x1000;
							Wb35Reg_Write( pHwData, 0x03bc, reg->U1BC_LEDConfigure ); // LED_1 Off
						}
					}
				}
				else
				{
					// Is transmitting/receiving ??
					if( (OS_CURRENT_RX_BYTE( adapter ) != pHwData->RxByteCountLast ) ||
						(OS_CURRENT_TX_BYTE( adapter ) != pHwData->TxByteCountLast ) )
					{
						if( (reg->U1BC_LEDConfigure & 0x3000) != 0x3000 )
						{
							reg->U1BC_LEDConfigure |= 0x3000;
							Wb35Reg_Write( pHwData, 0x03bc, reg->U1BC_LEDConfigure ); // LED_1 On
						}

						// Update variable
						pHwData->RxByteCountLast = OS_CURRENT_RX_BYTE( adapter );
						pHwData->TxByteCountLast = OS_CURRENT_TX_BYTE( adapter );
						TimeInterval = 200;
					}
					else
					{
						// Turn On LED_1 and blinking if transmitting/receiving
						 if( (reg->U1BC_LEDConfigure & 0x3000) != 0x1000 )
						 {
							 reg->U1BC_LEDConfigure &= ~0x3000;
							 reg->U1BC_LEDConfigure |= 0x1000;
							 Wb35Reg_Write( pHwData, 0x03bc, reg->U1BC_LEDConfigure ); // LED_1 On
						 }
					}
				}
				break;

			default: // Default setting. 2 LED be placed on PCB. LED_0: Link On LED_1 Active
				if( (reg->U1BC_LEDConfigure & 0x3000) != 0x3000 )
				{
					reg->U1BC_LEDConfigure |= 0x3000;// LED_1 is always on and event enable
					Wb35Reg_Write( pHwData, 0x03bc, reg->U1BC_LEDConfigure );
				}

				if( pHwData->LED_Blinking )
				{
					// Gray blinking
					reg->U1BC_LEDConfigure &= ~0x0f;
					reg->U1BC_LEDConfigure |= 0x10;
					reg->U1BC_LEDConfigure |= LEDgray[ (pHwData->LED_Blinking-1)%20 ];
					Wb35Reg_Write( pHwData, 0x03bc, reg->U1BC_LEDConfigure );

					pHwData->LED_Blinking += 2;
					if( pHwData->LED_Blinking < 40 )
						TimeInterval = 100;
					else
					{
						pHwData->LED_Blinking = 0; // Stop blinking
						reg->U1BC_LEDConfigure &= ~0x0f;
						Wb35Reg_Write( pHwData, 0x03bc, reg->U1BC_LEDConfigure );
					}
					break;
				}

				if( pHwData->LED_LinkOn )
				{
					if( !(reg->U1BC_LEDConfigure & 0x10) ) // Check the LED_0
					{
						//Try to turn ON LED_0 after gray blinking
						reg->U1BC_LEDConfigure |= 0x10;
						pHwData->LED_Blinking = 1; //Start blinking
						TimeInterval = 50;
					}
				}
				else
				{
					if( reg->U1BC_LEDConfigure & 0x10 ) // Check the LED_0
					{
						reg->U1BC_LEDConfigure &= ~0x10;
						Wb35Reg_Write( pHwData, 0x03bc, reg->U1BC_LEDConfigure );
					}
				}
				break;
		}

		//20060828.1 Active send null packet to avoid AP disconnect
		if( pHwData->LED_LinkOn )
		{
			pHwData->NullPacketCount += TimeInterval;
			if( pHwData->NullPacketCount >= DEFAULT_NULL_PACKET_COUNT )
			{
				pHwData->NullPacketCount = 0;
			}
		}
	}

	pHwData->time_count += TimeInterval;
	Wb35Tx_CurrentTime( pHwData, pHwData->time_count ); // 20060928 add
	OS_TIMER_SET( &pHwData->LEDTimer, TimeInterval ); // 20060623.1
}


void hal_set_phy_type(  phw_data_t pHwData,  u8 PhyType )
{
	pHwData->phy_type = PhyType;
}

void hal_get_phy_type(  phw_data_t pHwData,  u8 *PhyType )
{
	*PhyType = pHwData->phy_type;
}

void hal_reset_counter(  phw_data_t pHwData )
{
	pHwData->dto_tx_retry_count = 0;
	pHwData->dto_tx_frag_count = 0;
	memset( pHwData->tx_retry_count, 0, 8);
}

void hal_set_radio_mode( phw_data_t pHwData,  unsigned char radio_off)
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

u8 hal_get_antenna_number(  phw_data_t pHwData )
{
	struct wb35_reg *reg = &pHwData->reg;

	if ((reg->BB2C & BIT(11)) == 0)
		return 0;
	else
		return 1;
}

void hal_set_antenna_number(  phw_data_t pHwData, u8 number )
{

	struct wb35_reg *reg = &pHwData->reg;

	if (number == 1) {
		reg->BB2C |= BIT(11);
	} else {
		reg->BB2C &= ~BIT(11);
	}
	Wb35Reg_Write( pHwData, 0x102c, reg->BB2C );
#ifdef _PE_STATE_DUMP_
	WBDEBUG(("Current antenna number : %d\n", number));
#endif
}

//----------------------------------------------------------------------------------------------------
//0 : radio on; 1: radio off
u8 hal_get_hw_radio_off(  phw_data_t pHwData )
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

unsigned char hal_get_dxx_reg(  phw_data_t pHwData,  u16 number,  u32 * pValue )
{
	if( number < 0x1000 )
		number += 0x1000;
	return Wb35Reg_ReadSync( pHwData, number, pValue );
}

unsigned char hal_set_dxx_reg(  phw_data_t pHwData,  u16 number,  u32 value )
{
	unsigned char	ret;

	if( number < 0x1000 )
		number += 0x1000;
	ret = Wb35Reg_WriteSync( pHwData, number, value );
	return ret;
}

void hal_scan_status_indicate(phw_data_t pHwData, unsigned char IsOnProgress)
{
	if( pHwData->SurpriseRemove ) return;
	pHwData->LED_Scanning = IsOnProgress ? 1 : 0;
}

void hal_system_power_change(phw_data_t pHwData, u32 PowerState)
{
	if( PowerState != 0 )
	{
		pHwData->SurpriseRemove = 1;
		if( pHwData->WbUsb.IsUsb20 )
			hal_stop( pHwData );
	}
	else
	{
		if( !pHwData->WbUsb.IsUsb20 )
			hal_stop( pHwData );
	}
}

void hal_surprise_remove(  phw_data_t pHwData )
{
	struct wb35_adapter * adapter = pHwData->adapter;
	if (OS_ATOMIC_INC( adapter, &pHwData->SurpriseRemoveCount ) == 1) {
		#ifdef _PE_STATE_DUMP_
		WBDEBUG(("Calling hal_surprise_remove\n"));
		#endif
		OS_STOP( adapter );
	}
}

void hal_rate_change(  phw_data_t pHwData ) // Notify the HAL rate is changing 20060613.1
{
	struct wb35_adapter *	adapter = pHwData->adapter;
	u8		rate = CURRENT_TX_RATE;

	BBProcessor_RateChanging( pHwData, rate );
}

void hal_set_rf_power(phw_data_t pHwData, u8 PowerIndex)
{
	RFSynthesizer_SetPowerIndex( pHwData, PowerIndex );
}

unsigned char hal_set_LED(phw_data_t pHwData, u32 Mode) // 20061108 for WPS led control
{
	pHwData->LED_Blinking = 0;
	pHwData->LED_control = Mode;
	OS_TIMER_SET( &pHwData->LEDTimer, 10 ); // 20060623
	return true;
}

