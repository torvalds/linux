#ifndef __WINBOND_WB35_TX_S_H
#define __WINBOND_WB35_TX_S_H

#include "mds_s.h"

//====================================
// IS89C35 Tx related definition
//====================================
#define TX_INTERFACE			0	// Interface 1
#define TX_PIPE					3	// endpoint 4
#define TX_INTERRUPT			1	// endpoint 2
#define MAX_INTERRUPT_LENGTH	64	// It must be 64 for EP2 hardware



//====================================
// Internal variable for module
//====================================


struct wb35_tx {
	// For Tx buffer
	u8	TxBuffer[ MAX_USB_TX_BUFFER_NUMBER ][ MAX_USB_TX_BUFFER ];

	// For Interrupt pipe
	u8	EP2_buf[MAX_INTERRUPT_LENGTH];

	atomic_t	TxResultCount;// For thread control of EP2 931130.4.m
	atomic_t	TxFireCounter;// For thread control of EP4 931130.4.n
	u32			ByteTransfer;

	u32	    TxSendIndex;// The next index of Mds array to be sent
	u32	    EP2vm_state; // for EP2vm state
	u32	    EP4vm_state; // for EP4vm state
	u32	    tx_halt; // Stopping VM

	struct urb *				Tx4Urb;
	struct urb *				Tx2Urb;

	int		EP2VM_status;
	int		EP4VM_status;

	u32	TxFillCount; // 20060928
	u32	TxTimer; // 20060928 Add if sending packet is greater than 13
};

#endif
