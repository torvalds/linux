#include "os_common.h"
#include "wbhal_f.h"
#include "wblinux_f.h"

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

static void hal_led_control(unsigned long data)
{
	struct wbsoft_priv *adapter = (struct wbsoft_priv *) data;
	phw_data_t pHwData = &adapter->sHwData;
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
					if( (adapter->RxByteCount != pHwData->RxByteCountLast ) ||
						(adapter->TxByteCount != pHwData->TxByteCountLast ) )
					{
						if( (reg->U1BC_LEDConfigure & 0x3000) != 0x3000 )
						{
							reg->U1BC_LEDConfigure |= 0x3000;
							Wb35Reg_Write( pHwData, 0x03bc, reg->U1BC_LEDConfigure ); // LED_1 On
						}

						// Update variable
						pHwData->RxByteCountLast = adapter->RxByteCount;
						pHwData->TxByteCountLast = adapter->TxByteCount;
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
	Wb35Tx_CurrentTime(adapter, pHwData->time_count); // 20060928 add
	pHwData->LEDTimer.expires = jiffies + msecs_to_jiffies(TimeInterval);
	add_timer(&pHwData->LEDTimer);
}

u8 hal_init_hardware(struct ieee80211_hw *hw)
{
	struct wbsoft_priv *priv = hw->priv;
	phw_data_t pHwData = &priv->sHwData;
	u16 SoftwareSet;

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
				init_timer(&pHwData->LEDTimer);
				pHwData->LEDTimer.function = hal_led_control;
				pHwData->LEDTimer.data = (unsigned long) priv;
				pHwData->LEDTimer.expires = jiffies + msecs_to_jiffies(1000);
				add_timer(&pHwData->LEDTimer);

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

				Wb35Rx_start(hw);
				Wb35Tx_EP2VM_start(priv);

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
		case 3: del_timer_sync(&pHwData->LEDTimer);
			msleep(100); // Wait for Timer DPC exit 940623.2
			Wb35Rx_destroy( pHwData ); // Release the Rx
		case 2: Wb35Tx_destroy( pHwData ); // Release the Tx
		case 1: Wb35Reg_destroy( pHwData ); // Release the Wb35 Regisster resources
	}
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


static void hal_set_current_channel_ex(  phw_data_t pHwData,  ChanInfo channel )
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
void hal_set_phy_type(  phw_data_t pHwData,  u8 PhyType )
{
	pHwData->phy_type = PhyType;
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

void hal_set_rf_power(phw_data_t pHwData, u8 PowerIndex)
{
	RFSynthesizer_SetPowerIndex( pHwData, PowerIndex );
}
