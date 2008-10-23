#include "os_common.h"

void
Mds_reset_descriptor(PADAPTER Adapter)
{
	PMDS pMds = &Adapter->Mds;

	pMds->TxPause = 0;
	pMds->TxThreadCount = 0;
	pMds->TxFillIndex = 0;
	pMds->TxDesIndex = 0;
	pMds->ScanTxPause = 0;
	memset(pMds->TxOwner, 0, ((MAX_USB_TX_BUFFER_NUMBER + 3) & ~0x03));
}

unsigned char
Mds_initial(PADAPTER Adapter)
{
	PMDS pMds = &Adapter->Mds;

	pMds->TxPause = FALSE;
	pMds->TxRTSThreshold = DEFAULT_RTSThreshold;
	pMds->TxFragmentThreshold = DEFAULT_FRAGMENT_THRESHOLD;

	vRxTimerInit(Adapter);//for WPA countermeasure

	return hal_get_tx_buffer( &Adapter->sHwData, &pMds->pTxBuffer );
}

void
Mds_Destroy(PADAPTER Adapter)
{
	vRxTimerStop(Adapter);
}

void
Mds_Tx(PADAPTER Adapter)
{
	phw_data_t	pHwData = &Adapter->sHwData;
	PMDS		pMds = &Adapter->Mds;
	DESCRIPTOR	TxDes;
	PDESCRIPTOR	pTxDes = &TxDes;
	PUCHAR		XmitBufAddress;
	u16		XmitBufSize, PacketSize, stmp, CurrentSize, FragmentThreshold;
	u8		FillIndex, TxDesIndex, FragmentCount, FillCount;
	unsigned char	BufferFilled = FALSE, MICAdd = 0;


	if (pMds->TxPause)
		return;
	if (!hal_driver_init_OK(pHwData))
		return;

	//Only one thread can be run here
	if (!OS_ATOMIC_INC( Adapter, &pMds->TxThreadCount) == 1)
		goto cleanup;

	// Start to fill the data
	do {
		FillIndex = pMds->TxFillIndex;
		if (pMds->TxOwner[FillIndex]) { // Is owned by software 0:Yes 1:No
#ifdef _PE_TX_DUMP_
			WBDEBUG(("[Mds_Tx] Tx Owner is H/W.\n"));
#endif
			break;
		}

		XmitBufAddress = pMds->pTxBuffer + (MAX_USB_TX_BUFFER * FillIndex); //Get buffer
		XmitBufSize = 0;
		FillCount = 0;
		do {
			PacketSize = Adapter->sMlmeFrame.len;
			if (!PacketSize)
				break;

			//For Check the buffer resource
			FragmentThreshold = CURRENT_FRAGMENT_THRESHOLD;
			//931130.5.b
			FragmentCount = PacketSize/FragmentThreshold + 1;
			stmp = PacketSize + FragmentCount*32 + 8;//931130.5.c 8:MIC
			if ((XmitBufSize + stmp) >= MAX_USB_TX_BUFFER) {
				printk("[Mds_Tx] Excess max tx buffer.\n");
				break; // buffer is not enough
			}


			//
			// Start transmitting
			//
			BufferFilled = TRUE;

			/* Leaves first u8 intact */
			memset((PUCHAR)pTxDes + 1, 0, sizeof(DESCRIPTOR) - 1);

			TxDesIndex = pMds->TxDesIndex;//Get the current ID
			pTxDes->Descriptor_ID = TxDesIndex;
			pMds->TxDesFrom[ TxDesIndex ] = 2;//Storing the information of source comming from
			pMds->TxDesIndex++;
			pMds->TxDesIndex %= MAX_USB_TX_DESCRIPTOR;

			MLME_GetNextPacket( Adapter, pTxDes );

			// Copy header. 8byte USB + 24byte 802.11Hdr. Set TxRate, Preamble type
			Mds_HeaderCopy( Adapter, pTxDes, XmitBufAddress );

			// For speed up Key setting
			if (pTxDes->EapFix) {
#ifdef _PE_TX_DUMP_
				WBDEBUG(("35: EPA 4th frame detected. Size = %d\n", PacketSize));
#endif
				pHwData->IsKeyPreSet = 1;
			}

			// Copy (fragment) frame body, and set USB, 802.11 hdr flag
			CurrentSize = Mds_BodyCopy(Adapter, pTxDes, XmitBufAddress);

			// Set RTS/CTS and Normal duration field into buffer
			Mds_DurationSet(Adapter, pTxDes, XmitBufAddress);

			//
			// Calculation MIC from buffer which maybe fragment, then fill into temporary address 8 byte
			// 931130.5.e
			if (MICAdd)
				Mds_MicFill( Adapter, pTxDes, XmitBufAddress );

			//Shift to the next address
			XmitBufSize += CurrentSize;
			XmitBufAddress += CurrentSize;

#ifdef _IBSS_BEACON_SEQ_STICK_
			if ((XmitBufAddress[ DOT_11_DA_OFFSET+8 ] & 0xfc) != MAC_SUBTYPE_MNGMNT_PROBE_REQUEST) // +8 for USB hdr
#endif
				pMds->TxToggle = TRUE;

			// Get packet to transmit completed, 1:TESTSTA 2:MLME 3: Ndis data
			MLME_SendComplete(Adapter, 0, TRUE);

			// Software TSC count 20060214
			pMds->TxTsc++;
			if (pMds->TxTsc == 0)
				pMds->TxTsc_2++;

			FillCount++; // 20060928
		} while (HAL_USB_MODE_BURST(pHwData)); // End of multiple MSDU copy loop. FALSE = single TRUE = multiple sending

		// Move to the next one, if necessary
		if (BufferFilled) {
			// size setting
			pMds->TxBufferSize[ FillIndex ] = XmitBufSize;

			// 20060928 set Tx count
			pMds->TxCountInBuffer[FillIndex] = FillCount;

			// Set owner flag
			pMds->TxOwner[FillIndex] = 1;

			pMds->TxFillIndex++;
			pMds->TxFillIndex %= MAX_USB_TX_BUFFER_NUMBER;
			BufferFilled = FALSE;
		} else
			break;

		if (!PacketSize) // No more pk for transmitting
			break;

	} while(TRUE);

	//
	// Start to send by lower module
	//
	if (!pHwData->IsKeyPreSet)
		Wb35Tx_start(pHwData);

 cleanup:
	OS_ATOMIC_DEC( Adapter, &pMds->TxThreadCount );
}

void
Mds_SendComplete(PADAPTER Adapter, PT02_DESCRIPTOR pT02)
{
	PMDS	pMds = &Adapter->Mds;
	phw_data_t	pHwData = &Adapter->sHwData;
	u8	PacketId = (u8)pT02->T02_Tx_PktID;
	unsigned char	SendOK = TRUE;
	u8	RetryCount, TxRate;

	if (pT02->T02_IgnoreResult) // Don't care the result
		return;
	if (pT02->T02_IsLastMpdu) {
		//TODO: DTO -- get the retry count and fragment count
		// Tx rate
		TxRate = pMds->TxRate[ PacketId ][ 0 ];
		RetryCount = (u8)pT02->T02_MPDU_Cnt;
		if (pT02->value & FLAG_ERROR_TX_MASK) {
			SendOK = FALSE;

			if (pT02->T02_transmit_abort || pT02->T02_out_of_MaxTxMSDULiftTime) {
				//retry error
				pHwData->dto_tx_retry_count += (RetryCount+1);
				//[for tx debug]
				if (RetryCount<7)
					pHwData->tx_retry_count[RetryCount] += RetryCount;
				else
					pHwData->tx_retry_count[7] += RetryCount;
				#ifdef _PE_STATE_DUMP_
				WBDEBUG(("dto_tx_retry_count =%d\n", pHwData->dto_tx_retry_count));
				#endif
				MTO_SetTxCount(Adapter, TxRate, RetryCount);
			}
			pHwData->dto_tx_frag_count += (RetryCount+1);

			//[for tx debug]
			if (pT02->T02_transmit_abort_due_to_TBTT)
				pHwData->tx_TBTT_start_count++;
			if (pT02->T02_transmit_without_encryption_due_to_wep_on_false)
				pHwData->tx_WepOn_false_count++;
			if (pT02->T02_discard_due_to_null_wep_key)
				pHwData->tx_Null_key_count++;
		} else {
			if (pT02->T02_effective_transmission_rate)
				pHwData->tx_ETR_count++;
			MTO_SetTxCount(Adapter, TxRate, RetryCount);
		}

		// Clear send result buffer
		pMds->TxResult[ PacketId ] = 0;
	} else
		pMds->TxResult[ PacketId ] |= ((u16)(pT02->value & 0x0ffff));
}

void
Mds_HeaderCopy(PADAPTER Adapter, PDESCRIPTOR pDes, PUCHAR TargetBuffer)
{
	PMDS	pMds = &Adapter->Mds;
	PUCHAR	src_buffer = pDes->buffer_address[0];//931130.5.g
	PT00_DESCRIPTOR	pT00;
	PT01_DESCRIPTOR	pT01;
	u16	stmp;
	u8	i, ctmp1, ctmp2, ctmpf;
	u16	FragmentThreshold = CURRENT_FRAGMENT_THRESHOLD;


	stmp = pDes->buffer_total_size;
	//
	// Set USB header 8 byte
	//
	pT00 = (PT00_DESCRIPTOR)TargetBuffer;
	TargetBuffer += 4;
	pT01 = (PT01_DESCRIPTOR)TargetBuffer;
	TargetBuffer += 4;

	pT00->value = 0;// Clear
	pT01->value = 0;// Clear

	pT00->T00_tx_packet_id = pDes->Descriptor_ID;// Set packet ID
	pT00->T00_header_length = 24;// Set header length
	pT01->T01_retry_abort_ebable = 1;//921013 931130.5.h

	// Key ID setup
	pT01->T01_wep_id = 0;

	FragmentThreshold = DEFAULT_FRAGMENT_THRESHOLD;	//Do not fragment
	// Copy full data, the 1'st buffer contain all the data 931130.5.j
	memcpy( TargetBuffer, src_buffer, DOT_11_MAC_HEADER_SIZE );// Copy header
	pDes->buffer_address[0] = src_buffer + DOT_11_MAC_HEADER_SIZE;
	pDes->buffer_total_size -= DOT_11_MAC_HEADER_SIZE;
	pDes->buffer_size[0] = pDes->buffer_total_size;

	// Set fragment threshold
	FragmentThreshold -= (DOT_11_MAC_HEADER_SIZE + 4);
	pDes->FragmentThreshold = FragmentThreshold;

	// Set more frag bit
	TargetBuffer[1] |= 0x04;// Set more frag bit

	//
	// Set tx rate
	//
	stmp = *(PUSHORT)(TargetBuffer+30); // 2n alignment address

	//Use basic rate
	ctmp1 = ctmpf = CURRENT_TX_RATE_FOR_MNG;

	pDes->TxRate = ctmp1;
	#ifdef _PE_TX_DUMP_
	WBDEBUG(("Tx rate =%x\n", ctmp1));
	#endif

	pT01->T01_modulation_type = (ctmp1%3) ? 0 : 1;

	for( i=0; i<2; i++ ) {
		if( i == 1 )
			ctmp1 = ctmpf;

		pMds->TxRate[pDes->Descriptor_ID][i] = ctmp1; // backup the ta rate and fall back rate

		if( ctmp1 == 108) ctmp2 = 7;
		else if( ctmp1 == 96 ) ctmp2 = 6; // Rate convert for USB
		else if( ctmp1 == 72 ) ctmp2 = 5;
		else if( ctmp1 == 48 ) ctmp2 = 4;
		else if( ctmp1 == 36 ) ctmp2 = 3;
		else if( ctmp1 == 24 ) ctmp2 = 2;
		else if( ctmp1 == 18 ) ctmp2 = 1;
		else if( ctmp1 == 12 ) ctmp2 = 0;
		else if( ctmp1 == 22 ) ctmp2 = 3;
		else if( ctmp1 == 11 ) ctmp2 = 2;
		else if( ctmp1 == 4  ) ctmp2 = 1;
		else ctmp2 = 0; // if( ctmp1 == 2  ) or default

		if( i == 0 )
			pT01->T01_transmit_rate = ctmp2;
		else
			pT01->T01_fall_back_rate = ctmp2;
	}

	//
	// Set preamble type
	//
	if ((pT01->T01_modulation_type == 0) && (pT01->T01_transmit_rate == 0))	// RATE_1M
		pDes->PreambleMode =  WLAN_PREAMBLE_TYPE_LONG;
	else
		pDes->PreambleMode =  CURRENT_PREAMBLE_MODE;
	pT01->T01_plcp_header_length = pDes->PreambleMode;	// Set preamble

}

// The function return the 4n size of usb pk
u16
Mds_BodyCopy(PADAPTER Adapter, PDESCRIPTOR pDes, PUCHAR TargetBuffer)
{
	PT00_DESCRIPTOR	pT00;
	PMDS	pMds = &Adapter->Mds;
	PUCHAR	buffer, src_buffer, pctmp;
	u16	Size = 0;
	u16	SizeLeft, CopySize, CopyLeft, stmp;
	u8	buf_index, FragmentCount = 0;


	// Copy fragment body
	buffer = TargetBuffer; // shift 8B usb + 24B 802.11
	SizeLeft = pDes->buffer_total_size;
	buf_index = pDes->buffer_start_index;

	pT00 = (PT00_DESCRIPTOR)buffer;
	while (SizeLeft) {
		pT00 = (PT00_DESCRIPTOR)buffer;
		CopySize = SizeLeft;
		if (SizeLeft > pDes->FragmentThreshold) {
			CopySize = pDes->FragmentThreshold;
			pT00->T00_frame_length = 24 + CopySize;//Set USB length
		} else
			pT00->T00_frame_length = 24 + SizeLeft;//Set USB length

		SizeLeft -= CopySize;

		// 1 Byte operation
		pctmp = (PUCHAR)( buffer + 8 + DOT_11_SEQUENCE_OFFSET );
		*pctmp &= 0xf0;
		*pctmp |= FragmentCount;//931130.5.m
		if( !FragmentCount )
			pT00->T00_first_mpdu = 1;

		buffer += 32; // 8B usb + 24B 802.11 header
		Size += 32;

		// Copy into buffer
		stmp = CopySize + 3;
		stmp &= ~0x03;//4n Alignment
		Size += stmp;// Current 4n offset of mpdu

		while (CopySize) {
			// Copy body
			src_buffer = pDes->buffer_address[buf_index];
			CopyLeft = CopySize;
			if (CopySize >= pDes->buffer_size[buf_index]) {
				CopyLeft = pDes->buffer_size[buf_index];

				// Get the next buffer of descriptor
				buf_index++;
				buf_index %= MAX_DESCRIPTOR_BUFFER_INDEX;
			} else {
				PUCHAR	pctmp = pDes->buffer_address[buf_index];
				pctmp += CopySize;
				pDes->buffer_address[buf_index] = pctmp;
				pDes->buffer_size[buf_index] -= CopySize;
			}

			memcpy(buffer, src_buffer, CopyLeft);
			buffer += CopyLeft;
			CopySize -= CopyLeft;
		}

		// 931130.5.n
		if (pMds->MicAdd) {
			if (!SizeLeft) {
				pMds->MicWriteAddress[ pMds->MicWriteIndex ] = buffer - pMds->MicAdd;
				pMds->MicWriteSize[ pMds->MicWriteIndex ] = pMds->MicAdd;
				pMds->MicAdd = 0;
			}
			else if( SizeLeft < 8 ) //931130.5.p
			{
				pMds->MicAdd = SizeLeft;
				pMds->MicWriteAddress[ pMds->MicWriteIndex ] = buffer - ( 8 - SizeLeft );
				pMds->MicWriteSize[ pMds->MicWriteIndex ] = 8 - SizeLeft;
				pMds->MicWriteIndex++;
			}
		}

		// Does it need to generate the new header for next mpdu?
		if (SizeLeft) {
			buffer = TargetBuffer + Size; // Get the next 4n start address
			memcpy( buffer, TargetBuffer, 32 );//Copy 8B USB +24B 802.11
			pT00 = (PT00_DESCRIPTOR)buffer;
			pT00->T00_first_mpdu = 0;
		}

		FragmentCount++;
	}

	pT00->T00_last_mpdu = 1;
	pT00->T00_IsLastMpdu = 1;
	buffer = (PUCHAR)pT00 + 8; // +8 for USB hdr
	buffer[1] &= ~0x04; // Clear more frag bit of 802.11 frame control
	pDes->FragmentCount = FragmentCount; // Update the correct fragment number
	return Size;
}


void
Mds_DurationSet(  PADAPTER Adapter,  PDESCRIPTOR pDes,  PUCHAR buffer )
{
	PT00_DESCRIPTOR	pT00;
	PT01_DESCRIPTOR	pT01;
	u16	Duration, NextBodyLen, OffsetSize;
	u8	Rate, i;
	unsigned char	CTS_on = FALSE, RTS_on = FALSE;
	PT00_DESCRIPTOR pNextT00;
	u16 BodyLen;
	unsigned char boGroupAddr = FALSE;


	OffsetSize = pDes->FragmentThreshold + 32 + 3;
	OffsetSize &= ~0x03;
	Rate = pDes->TxRate >> 1;
	if (!Rate)
		Rate = 1;

	pT00 = (PT00_DESCRIPTOR)buffer;
	pT01 = (PT01_DESCRIPTOR)(buffer+4);
	pNextT00 = (PT00_DESCRIPTOR)(buffer+OffsetSize);

	if( buffer[ DOT_11_DA_OFFSET+8 ] & 0x1 ) // +8 for USB hdr
		boGroupAddr = TRUE;

	//========================================
	// Set RTS/CTS mechanism
	//========================================
	if (!boGroupAddr)
	{
		//NOTE : If the protection mode is enabled and the MSDU will be fragmented,
		//		 the tx rates of MPDUs will all be DSSS rates. So it will not use
		//		 CTS-to-self in this case. CTS-To-self will only be used when without
		//		 fragmentation. -- 20050112
		BodyLen = (u16)pT00->T00_frame_length;	//include 802.11 header
		BodyLen += 4;	//CRC

		if( BodyLen >= CURRENT_RTS_THRESHOLD )
			RTS_on = TRUE; // Using RTS
		else
		{
			if( pT01->T01_modulation_type ) // Is using OFDM
			{
				if( CURRENT_PROTECT_MECHANISM ) // Is using protect
					CTS_on = TRUE; // Using CTS
			}
		}
	}

	if( RTS_on || CTS_on )
	{
		if( pT01->T01_modulation_type) // Is using OFDM
		{
			//CTS duration
			// 2 SIFS + DATA transmit time + 1 ACK
			// ACK Rate : 24 Mega bps
			// ACK frame length = 14 bytes
			Duration = 2*DEFAULT_SIFSTIME +
					   2*PREAMBLE_PLUS_SIGNAL_PLUS_SIGNALEXTENSION +
					   ((BodyLen*8 + 22 + Rate*4 - 1)/(Rate*4))*Tsym +
					   ((112 + 22 + 95)/96)*Tsym;
		}
		else	//DSSS
		{
			//CTS duration
			// 2 SIFS + DATA transmit time + 1 ACK
			// Rate : ?? Mega bps
			// ACK frame length = 14 bytes
			if( pT01->T01_plcp_header_length ) //long preamble
				Duration = LONG_PREAMBLE_PLUS_PLCPHEADER_TIME*2;
			else
				Duration = SHORT_PREAMBLE_PLUS_PLCPHEADER_TIME*2;

			Duration += ( ((BodyLen + 14)*8 + Rate-1) / Rate +
						DEFAULT_SIFSTIME*2 );
		}

		if( RTS_on )
		{
			if( pT01->T01_modulation_type ) // Is using OFDM
			{
				//CTS + 1 SIFS + CTS duration
				//CTS Rate : 24 Mega bps
				//CTS frame length = 14 bytes
				Duration += (DEFAULT_SIFSTIME +
								PREAMBLE_PLUS_SIGNAL_PLUS_SIGNALEXTENSION +
								((112 + 22 + 95)/96)*Tsym);
			}
			else
			{
				//CTS + 1 SIFS + CTS duration
				//CTS Rate : ?? Mega bps
				//CTS frame length = 14 bytes
				if( pT01->T01_plcp_header_length ) //long preamble
					Duration += LONG_PREAMBLE_PLUS_PLCPHEADER_TIME;
				else
					Duration += SHORT_PREAMBLE_PLUS_PLCPHEADER_TIME;

				Duration += ( ((112 + Rate-1) / Rate) + DEFAULT_SIFSTIME );
			}
		}

		// Set the value into USB descriptor
		pT01->T01_add_rts = RTS_on ? 1 : 0;
		pT01->T01_add_cts = CTS_on ? 1 : 0;
		pT01->T01_rts_cts_duration = Duration;
	}

	//=====================================
	// Fill the more fragment descriptor
	//=====================================
	if( boGroupAddr )
		Duration = 0;
	else
	{
		for( i=pDes->FragmentCount-1; i>0; i-- )
		{
			NextBodyLen = (u16)pNextT00->T00_frame_length;
			NextBodyLen += 4;	//CRC

			if( pT01->T01_modulation_type )
			{
				//OFDM
				// data transmit time + 3 SIFS + 2 ACK
				// Rate : ??Mega bps
				// ACK frame length = 14 bytes, tx rate = 24M
				Duration = PREAMBLE_PLUS_SIGNAL_PLUS_SIGNALEXTENSION * 3;
				Duration += (((NextBodyLen*8 + 22 + Rate*4 - 1)/(Rate*4)) * Tsym +
							(((2*14)*8 + 22 + 95)/96)*Tsym +
							DEFAULT_SIFSTIME*3);
			}
			else
			{
				//DSSS
				// data transmit time + 2 ACK + 3 SIFS
				// Rate : ??Mega bps
				// ACK frame length = 14 bytes
				//TODO :
				if( pT01->T01_plcp_header_length ) //long preamble
					Duration = LONG_PREAMBLE_PLUS_PLCPHEADER_TIME*3;
				else
					Duration = SHORT_PREAMBLE_PLUS_PLCPHEADER_TIME*3;

				Duration += ( ((NextBodyLen + (2*14))*8 + Rate-1) / Rate +
							DEFAULT_SIFSTIME*3 );
			}

			((PUSHORT)buffer)[5] = cpu_to_le16(Duration);// 4 USHOR for skip 8B USB, 2USHORT=FC + Duration

			//----20061009 add by anson's endian
			pNextT00->value = cpu_to_le32(pNextT00->value);
			pT01->value = cpu_to_le32( pT01->value );
			//----end 20061009 add by anson's endian

			buffer += OffsetSize;
			pT01 = (PT01_DESCRIPTOR)(buffer+4);
			if (i != 1)	//The last fragment will not have the next fragment
				pNextT00 = (PT00_DESCRIPTOR)(buffer+OffsetSize);
		}

		//=====================================
		// Fill the last fragment descriptor
		//=====================================
		if( pT01->T01_modulation_type )
		{
			//OFDM
			// 1 SIFS + 1 ACK
			// Rate : 24 Mega bps
			// ACK frame length = 14 bytes
			Duration = PREAMBLE_PLUS_SIGNAL_PLUS_SIGNALEXTENSION;
			//The Tx rate of ACK use 24M
			Duration += (((112 + 22 + 95)/96)*Tsym + DEFAULT_SIFSTIME );
		}
		else
		{
			// DSSS
			// 1 ACK + 1 SIFS
			// Rate : ?? Mega bps
			// ACK frame length = 14 bytes(112 bits)
			if( pT01->T01_plcp_header_length ) //long preamble
				Duration = LONG_PREAMBLE_PLUS_PLCPHEADER_TIME;
			else
				Duration = SHORT_PREAMBLE_PLUS_PLCPHEADER_TIME;

			Duration += ( (112 + Rate-1)/Rate +	DEFAULT_SIFSTIME );
		}
	}

	((PUSHORT)buffer)[5] = cpu_to_le16(Duration);// 4 USHOR for skip 8B USB, 2USHORT=FC + Duration
	pT00->value = cpu_to_le32(pT00->value);
	pT01->value = cpu_to_le32(pT01->value);
	//--end 20061009 add

}

void MDS_EthernetPacketReceive(  PADAPTER Adapter,  PRXLAYER1 pRxLayer1 )
{
		OS_RECEIVE_PACKET_INDICATE( Adapter, pRxLayer1 );
}


