/* DO NOT EDIT - Generated automatically by script_asm.pl */
static u32 SCRIPT[] = {
/*
; Script for the NCR (or symbios) 53c700 and 53c700-66 chip
;
; Copyright (C) 2001 James.Bottomley@HansenPartnership.com
;;-----------------------------------------------------------------------------
;;  
;;  This program is free software; you can redistribute it and/or modify
;;  it under the terms of the GNU General Public License as published by
;;  the Free Software Foundation; either version 2 of the License, or
;;  (at your option) any later version.
;;
;;  This program is distributed in the hope that it will be useful,
;;  but WITHOUT ANY WARRANTY; without even the implied warranty of
;;  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
;;  GNU General Public License for more details.
;;
;;  You should have received a copy of the GNU General Public License
;;  along with this program; if not, write to the Free Software
;;  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
;;
;;-----------------------------------------------------------------------------
;
; This script is designed to be modified for the particular command in
; operation.  The particular variables pertaining to the commands are:
;
ABSOLUTE	Device_ID = 0		; ID of target for command
ABSOLUTE	MessageCount = 0	; Number of bytes in message
ABSOLUTE	MessageLocation = 0	; Addr of message
ABSOLUTE	CommandCount = 0	; Number of bytes in command
ABSOLUTE	CommandAddress = 0	; Addr of Command
ABSOLUTE	StatusAddress = 0	; Addr to receive status return
ABSOLUTE	ReceiveMsgAddress = 0	; Addr to receive msg
;
; This is the magic component for handling scatter-gather.  Each of the
; SG components is preceeded by a script fragment which moves the
; necessary amount of data and jumps to the next SG segment.  The final
; SG segment jumps back to .  However, this address is the first SG script
; segment.
;
ABSOLUTE	SGScriptStartAddress = 0

; The following represent status interrupts we use 3 hex digits for
; this: 0xPRS where 

; P:
ABSOLUTE	AFTER_SELECTION 	= 0x100
ABSOLUTE	BEFORE_CMD 		= 0x200
ABSOLUTE	AFTER_CMD 		= 0x300
ABSOLUTE	AFTER_STATUS 		= 0x400
ABSOLUTE	AFTER_DATA_IN		= 0x500
ABSOLUTE	AFTER_DATA_OUT		= 0x600
ABSOLUTE	DURING_DATA_IN		= 0x700

; R:
ABSOLUTE	NOT_MSG_OUT 		= 0x10
ABSOLUTE	UNEXPECTED_PHASE 	= 0x20
ABSOLUTE	NOT_MSG_IN 		= 0x30
ABSOLUTE	UNEXPECTED_MSG		= 0x40
ABSOLUTE	MSG_IN			= 0x50
ABSOLUTE	SDTR_MSG_R		= 0x60
ABSOLUTE	REJECT_MSG_R		= 0x70
ABSOLUTE	DISCONNECT		= 0x80
ABSOLUTE	MSG_OUT			= 0x90
ABSOLUTE	WDTR_MSG_R		= 0xA0

; S:
ABSOLUTE	GOOD_STATUS 		= 0x1

; Combinations, since the script assembler can't process |
ABSOLUTE	NOT_MSG_OUT_AFTER_SELECTION = 0x110
ABSOLUTE	UNEXPECTED_PHASE_BEFORE_CMD = 0x220
ABSOLUTE	UNEXPECTED_PHASE_AFTER_CMD = 0x320
ABSOLUTE	NOT_MSG_IN_AFTER_STATUS = 0x430
ABSOLUTE	GOOD_STATUS_AFTER_STATUS = 0x401
ABSOLUTE	UNEXPECTED_PHASE_AFTER_DATA_IN = 0x520
ABSOLUTE	UNEXPECTED_PHASE_AFTER_DATA_OUT = 0x620
ABSOLUTE	UNEXPECTED_MSG_BEFORE_CMD = 0x240
ABSOLUTE	MSG_IN_BEFORE_CMD = 0x250
ABSOLUTE	MSG_IN_AFTER_CMD = 0x350
ABSOLUTE	SDTR_MSG_BEFORE_CMD = 0x260
ABSOLUTE	REJECT_MSG_BEFORE_CMD = 0x270
ABSOLUTE	DISCONNECT_AFTER_CMD = 0x380
ABSOLUTE	SDTR_MSG_AFTER_CMD = 0x360
ABSOLUTE	WDTR_MSG_AFTER_CMD = 0x3A0
ABSOLUTE	MSG_IN_AFTER_STATUS = 0x440
ABSOLUTE	DISCONNECT_AFTER_DATA = 0x580
ABSOLUTE	MSG_IN_AFTER_DATA_IN = 0x550
ABSOLUTE	MSG_IN_AFTER_DATA_OUT = 0x650
ABSOLUTE	MSG_OUT_AFTER_DATA_IN = 0x590
ABSOLUTE	DATA_IN_AFTER_DATA_IN = 0x5a0
ABSOLUTE	MSG_IN_DURING_DATA_IN = 0x750
ABSOLUTE	DISCONNECT_DURING_DATA = 0x780

;
; Other interrupt conditions
; 
ABSOLUTE	RESELECTED_DURING_SELECTION = 0x1000
ABSOLUTE	COMPLETED_SELECTION_AS_TARGET = 0x1001
ABSOLUTE	RESELECTION_IDENTIFIED = 0x1003
;
; Fatal interrupt conditions.  If you add to this, also add to the
; array of corresponding messages
;
ABSOLUTE	FATAL = 0x2000
ABSOLUTE	FATAL_UNEXPECTED_RESELECTION_MSG = 0x2000
ABSOLUTE	FATAL_SEND_MSG = 0x2001
ABSOLUTE	FATAL_NOT_MSG_IN_AFTER_SELECTION = 0x2002
ABSOLUTE	FATAL_ILLEGAL_MSG_LENGTH = 0x2003

ABSOLUTE	DEBUG_INTERRUPT	= 0x3000
ABSOLUTE	DEBUG_INTERRUPT1 = 0x3001
ABSOLUTE	DEBUG_INTERRUPT2 = 0x3002
ABSOLUTE	DEBUG_INTERRUPT3 = 0x3003
ABSOLUTE	DEBUG_INTERRUPT4 = 0x3004
ABSOLUTE	DEBUG_INTERRUPT5 = 0x3005
ABSOLUTE	DEBUG_INTERRUPT6 = 0x3006


;
; SCSI Messages we interpret in the script
;
ABSOLUTE	COMMAND_COMPLETE_MSG	= 0x00
ABSOLUTE	EXTENDED_MSG		= 0x01
ABSOLUTE	SDTR_MSG		= 0x01
ABSOLUTE	SAVE_DATA_PTRS_MSG	= 0x02
ABSOLUTE	RESTORE_DATA_PTRS_MSG	= 0x03
ABSOLUTE	WDTR_MSG		= 0x03
ABSOLUTE	DISCONNECT_MSG		= 0x04
ABSOLUTE	REJECT_MSG		= 0x07
ABSOLUTE	PARITY_ERROR_MSG	= 0x09
ABSOLUTE	SIMPLE_TAG_MSG		= 0x20
ABSOLUTE	IDENTIFY_MSG		= 0x80
ABSOLUTE	IDENTIFY_MSG_MASK	= 0x7F
ABSOLUTE	TWO_BYTE_MSG		= 0x20
ABSOLUTE	TWO_BYTE_MSG_MASK	= 0x0F

; This is where the script begins

ENTRY	StartUp

StartUp:
	SELECT	ATN Device_ID, Reselect

at 0x00000000 : */	0x41000000,0x00000020,
/*
	JUMP	Finish, WHEN STATUS

at 0x00000002 : */	0x830b0000,0x00000460,
/*
	JUMP	SendIdentifyMsg, IF MSG_OUT

at 0x00000004 : */	0x860a0000,0x000001b0,
/*
	INT	NOT_MSG_OUT_AFTER_SELECTION

at 0x00000006 : */	0x98080000,0x00000110,
/*

Reselect:
	WAIT	RESELECT SelectedAsTarget

at 0x00000008 : */	0x50000000,0x00000058,
/*
	INT	RESELECTED_DURING_SELECTION, WHEN MSG_IN

at 0x0000000a : */	0x9f0b0000,0x00001000,
/*
	INT	FATAL_NOT_MSG_IN_AFTER_SELECTION

at 0x0000000c : */	0x98080000,0x00002002,
/*

	ENTRY	GetReselectionData
GetReselectionData:
	MOVE	1, ReceiveMsgAddress, WHEN MSG_IN

at 0x0000000e : */	0x0f000001,0x00000000,
/*
	INT	RESELECTION_IDENTIFIED

at 0x00000010 : */	0x98080000,0x00001003,
/*

	ENTRY	GetReselectionWithTag
GetReselectionWithTag:
	MOVE	3, ReceiveMsgAddress, WHEN MSG_IN

at 0x00000012 : */	0x0f000003,0x00000000,
/*
	INT	RESELECTION_IDENTIFIED

at 0x00000014 : */	0x98080000,0x00001003,
/*
	
	ENTRY	SelectedAsTarget
SelectedAsTarget:
; Basically tell the selecting device that there's nothing here
	SET	TARGET

at 0x00000016 : */	0x58000200,0x00000000,
/*
	DISCONNECT

at 0x00000018 : */	0x48000000,0x00000000,
/*
	CLEAR	TARGET

at 0x0000001a : */	0x60000200,0x00000000,
/*
	INT	COMPLETED_SELECTION_AS_TARGET

at 0x0000001c : */	0x98080000,0x00001001,
/*
;
; These are the messaging entries
;
; Send a message.  Message count should be correctly patched
	ENTRY	SendMessage
SendMessage:
	MOVE	MessageCount, MessageLocation, WHEN MSG_OUT

at 0x0000001e : */	0x0e000000,0x00000000,
/*
ResumeSendMessage:
	RETURN,	WHEN NOT MSG_OUT

at 0x00000020 : */	0x96030000,0x00000000,
/*
	INT	FATAL_SEND_MSG

at 0x00000022 : */	0x98080000,0x00002001,
/*

	ENTRY	SendMessagePhaseMismatch
SendMessagePhaseMismatch:
	CLEAR	ACK

at 0x00000024 : */	0x60000040,0x00000000,
/*
	JUMP	ResumeSendMessage

at 0x00000026 : */	0x80080000,0x00000080,
/*
;
; Receive a message.  Need to identify the message to
; receive it correctly
	ENTRY	ReceiveMessage
ReceiveMessage:
	MOVE	1, ReceiveMsgAddress, WHEN MSG_IN

at 0x00000028 : */	0x0f000001,0x00000000,
/*
;
; Use this entry if we've just tried to look at the first byte
; of the message and want to process it further
ProcessReceiveMessage:
	JUMP	ReceiveExtendedMessage, IF EXTENDED_MSG

at 0x0000002a : */	0x800c0001,0x000000d0,
/*
	RETURN,	IF NOT TWO_BYTE_MSG, AND MASK TWO_BYTE_MSG_MASK

at 0x0000002c : */	0x90040f20,0x00000000,
/*
	CLEAR	ACK

at 0x0000002e : */	0x60000040,0x00000000,
/*
	MOVE	1, ReceiveMsgAddress + 1, WHEN MSG_IN

at 0x00000030 : */	0x0f000001,0x00000001,
/*
	RETURN

at 0x00000032 : */	0x90080000,0x00000000,
/*
ReceiveExtendedMessage:
	CLEAR	ACK

at 0x00000034 : */	0x60000040,0x00000000,
/*
	MOVE	1, ReceiveMsgAddress + 1, WHEN MSG_IN

at 0x00000036 : */	0x0f000001,0x00000001,
/*
	JUMP	Receive1Byte, IF 0x01

at 0x00000038 : */	0x800c0001,0x00000110,
/*
	JUMP	Receive2Byte, IF 0x02

at 0x0000003a : */	0x800c0002,0x00000128,
/*
	JUMP	Receive3Byte, IF 0x03

at 0x0000003c : */	0x800c0003,0x00000140,
/*
	JUMP	Receive4Byte, IF 0x04

at 0x0000003e : */	0x800c0004,0x00000158,
/*
	JUMP	Receive5Byte, IF 0x05

at 0x00000040 : */	0x800c0005,0x00000170,
/*
	INT	FATAL_ILLEGAL_MSG_LENGTH

at 0x00000042 : */	0x98080000,0x00002003,
/*
Receive1Byte:
	CLEAR	ACK

at 0x00000044 : */	0x60000040,0x00000000,
/*
	MOVE	1, ReceiveMsgAddress + 2, WHEN MSG_IN

at 0x00000046 : */	0x0f000001,0x00000002,
/*
	RETURN

at 0x00000048 : */	0x90080000,0x00000000,
/*
Receive2Byte:
	CLEAR	ACK

at 0x0000004a : */	0x60000040,0x00000000,
/*
	MOVE	2, ReceiveMsgAddress + 2, WHEN MSG_IN

at 0x0000004c : */	0x0f000002,0x00000002,
/*
	RETURN

at 0x0000004e : */	0x90080000,0x00000000,
/*
Receive3Byte:
	CLEAR	ACK

at 0x00000050 : */	0x60000040,0x00000000,
/*
	MOVE	3, ReceiveMsgAddress + 2, WHEN MSG_IN

at 0x00000052 : */	0x0f000003,0x00000002,
/*
	RETURN

at 0x00000054 : */	0x90080000,0x00000000,
/*
Receive4Byte:
	CLEAR	ACK

at 0x00000056 : */	0x60000040,0x00000000,
/*
	MOVE	4, ReceiveMsgAddress + 2, WHEN MSG_IN

at 0x00000058 : */	0x0f000004,0x00000002,
/*
	RETURN

at 0x0000005a : */	0x90080000,0x00000000,
/*
Receive5Byte:
	CLEAR	ACK

at 0x0000005c : */	0x60000040,0x00000000,
/*
	MOVE	5, ReceiveMsgAddress + 2, WHEN MSG_IN

at 0x0000005e : */	0x0f000005,0x00000002,
/*
	RETURN

at 0x00000060 : */	0x90080000,0x00000000,
/*
;
; Come here from the message processor to ignore the message
;
	ENTRY	IgnoreMessage
IgnoreMessage:
	CLEAR	ACK

at 0x00000062 : */	0x60000040,0x00000000,
/*
	RETURN

at 0x00000064 : */	0x90080000,0x00000000,
/*
;
; Come here to send a reply to a message
;
	ENTRY	SendMessageWithATN
SendMessageWithATN:
	SET	ATN

at 0x00000066 : */	0x58000008,0x00000000,
/*
	CLEAR	ACK

at 0x00000068 : */	0x60000040,0x00000000,
/*
	JUMP	SendMessage

at 0x0000006a : */	0x80080000,0x00000078,
/*

SendIdentifyMsg:
	CALL	SendMessage

at 0x0000006c : */	0x88080000,0x00000078,
/*
	CLEAR	ATN

at 0x0000006e : */	0x60000008,0x00000000,
/*

IgnoreMsgBeforeCommand:
	CLEAR	ACK

at 0x00000070 : */	0x60000040,0x00000000,
/*
	ENTRY	SendCommand
SendCommand:
	JUMP	Finish, WHEN STATUS

at 0x00000072 : */	0x830b0000,0x00000460,
/*
	JUMP	MsgInBeforeCommand, IF MSG_IN

at 0x00000074 : */	0x870a0000,0x000002c0,
/*
	INT	UNEXPECTED_PHASE_BEFORE_CMD, IF NOT CMD

at 0x00000076 : */	0x9a020000,0x00000220,
/*
	MOVE	CommandCount, CommandAddress, WHEN CMD

at 0x00000078 : */	0x0a000000,0x00000000,
/*
ResumeSendCommand:
	JUMP	Finish, WHEN STATUS

at 0x0000007a : */	0x830b0000,0x00000460,
/*
	JUMP	MsgInAfterCmd, IF MSG_IN

at 0x0000007c : */	0x870a0000,0x00000248,
/*
	JUMP	DataIn, IF DATA_IN

at 0x0000007e : */	0x810a0000,0x000002f8,
/*
	JUMP	DataOut, IF DATA_OUT

at 0x00000080 : */	0x800a0000,0x00000338,
/*
	INT	UNEXPECTED_PHASE_AFTER_CMD

at 0x00000082 : */	0x98080000,0x00000320,
/*

IgnoreMsgDuringData:
	CLEAR	ACK

at 0x00000084 : */	0x60000040,0x00000000,
/*
	; fall through to MsgInDuringData

Entry MsgInDuringData
MsgInDuringData:
;
; Could be we have nothing more to transfer
;
	JUMP	Finish, WHEN STATUS

at 0x00000086 : */	0x830b0000,0x00000460,
/*
	MOVE	1, ReceiveMsgAddress, WHEN MSG_IN

at 0x00000088 : */	0x0f000001,0x00000000,
/*
	JUMP	DisconnectDuringDataIn, IF DISCONNECT_MSG

at 0x0000008a : */	0x800c0004,0x00000398,
/*
	JUMP	IgnoreMsgDuringData, IF SAVE_DATA_PTRS_MSG

at 0x0000008c : */	0x800c0002,0x00000210,
/*
	JUMP	IgnoreMsgDuringData, IF RESTORE_DATA_PTRS_MSG

at 0x0000008e : */	0x800c0003,0x00000210,
/*
	INT	MSG_IN_DURING_DATA_IN

at 0x00000090 : */	0x98080000,0x00000750,
/*

MsgInAfterCmd:
	MOVE	1, ReceiveMsgAddress, WHEN MSG_IN

at 0x00000092 : */	0x0f000001,0x00000000,
/*
	JUMP	DisconnectAfterCmd, IF DISCONNECT_MSG

at 0x00000094 : */	0x800c0004,0x00000298,
/*
	JUMP	IgnoreMsgInAfterCmd, IF SAVE_DATA_PTRS_MSG

at 0x00000096 : */	0x800c0002,0x00000288,
/*
	JUMP	IgnoreMsgInAfterCmd, IF RESTORE_DATA_PTRS_MSG

at 0x00000098 : */	0x800c0003,0x00000288,
/*
	CALL	ProcessReceiveMessage

at 0x0000009a : */	0x88080000,0x000000a8,
/*
	INT	MSG_IN_AFTER_CMD

at 0x0000009c : */	0x98080000,0x00000350,
/*
	CLEAR	ACK

at 0x0000009e : */	0x60000040,0x00000000,
/*
	JUMP	ResumeSendCommand

at 0x000000a0 : */	0x80080000,0x000001e8,
/*

IgnoreMsgInAfterCmd:
	CLEAR	ACK

at 0x000000a2 : */	0x60000040,0x00000000,
/*
	JUMP	ResumeSendCommand

at 0x000000a4 : */	0x80080000,0x000001e8,
/*

DisconnectAfterCmd:
	CLEAR	ACK

at 0x000000a6 : */	0x60000040,0x00000000,
/*
	WAIT	DISCONNECT

at 0x000000a8 : */	0x48000000,0x00000000,
/*
	ENTRY	Disconnect1
Disconnect1:
	INT	DISCONNECT_AFTER_CMD

at 0x000000aa : */	0x98080000,0x00000380,
/*
	ENTRY	Disconnect2
Disconnect2:
; We return here after a reselection
	CLEAR	ACK

at 0x000000ac : */	0x60000040,0x00000000,
/*
	JUMP	ResumeSendCommand

at 0x000000ae : */	0x80080000,0x000001e8,
/*

MsgInBeforeCommand:
	MOVE	1, ReceiveMsgAddress, WHEN MSG_IN

at 0x000000b0 : */	0x0f000001,0x00000000,
/*
	JUMP	IgnoreMsgBeforeCommand, IF SAVE_DATA_PTRS_MSG

at 0x000000b2 : */	0x800c0002,0x000001c0,
/*
	JUMP	IgnoreMsgBeforeCommand, IF RESTORE_DATA_PTRS_MSG

at 0x000000b4 : */	0x800c0003,0x000001c0,
/*
	CALL	ProcessReceiveMessage

at 0x000000b6 : */	0x88080000,0x000000a8,
/*
	INT	MSG_IN_BEFORE_CMD

at 0x000000b8 : */	0x98080000,0x00000250,
/*
	CLEAR	ACK

at 0x000000ba : */	0x60000040,0x00000000,
/*
	JUMP	SendCommand

at 0x000000bc : */	0x80080000,0x000001c8,
/*

DataIn:
	CALL	SGScriptStartAddress

at 0x000000be : */	0x88080000,0x00000000,
/*
ResumeDataIn:
	JUMP	Finish, WHEN STATUS

at 0x000000c0 : */	0x830b0000,0x00000460,
/*
	JUMP	MsgInAfterDataIn, IF MSG_IN

at 0x000000c2 : */	0x870a0000,0x00000358,
/*
	JUMP	DataInAfterDataIn, if DATA_IN

at 0x000000c4 : */	0x810a0000,0x00000328,
/*
	INT	MSG_OUT_AFTER_DATA_IN, if MSG_OUT

at 0x000000c6 : */	0x9e0a0000,0x00000590,
/*
	INT	UNEXPECTED_PHASE_AFTER_DATA_IN

at 0x000000c8 : */	0x98080000,0x00000520,
/*

DataInAfterDataIn:
	INT	DATA_IN_AFTER_DATA_IN

at 0x000000ca : */	0x98080000,0x000005a0,
/*
	JUMP	ResumeDataIn

at 0x000000cc : */	0x80080000,0x00000300,
/*

DataOut:
	CALL	SGScriptStartAddress

at 0x000000ce : */	0x88080000,0x00000000,
/*
ResumeDataOut:
	JUMP	Finish, WHEN STATUS

at 0x000000d0 : */	0x830b0000,0x00000460,
/*
	JUMP	MsgInAfterDataOut, IF MSG_IN

at 0x000000d2 : */	0x870a0000,0x000003e8,
/*
	INT	UNEXPECTED_PHASE_AFTER_DATA_OUT

at 0x000000d4 : */	0x98080000,0x00000620,
/*

MsgInAfterDataIn:
	MOVE	1, ReceiveMsgAddress, WHEN MSG_IN

at 0x000000d6 : */	0x0f000001,0x00000000,
/*
	JUMP	DisconnectAfterDataIn, IF DISCONNECT_MSG

at 0x000000d8 : */	0x800c0004,0x000003c0,
/*
	JUMP	IgnoreMsgAfterData, IF SAVE_DATA_PTRS_MSG

at 0x000000da : */	0x800c0002,0x00000428,
/*
	JUMP	IgnoreMsgAfterData, IF RESTORE_DATA_PTRS_MSG

at 0x000000dc : */	0x800c0003,0x00000428,
/*
	CALL	ProcessReceiveMessage

at 0x000000de : */	0x88080000,0x000000a8,
/*
	INT	MSG_IN_AFTER_DATA_IN

at 0x000000e0 : */	0x98080000,0x00000550,
/*
	CLEAR	ACK

at 0x000000e2 : */	0x60000040,0x00000000,
/*
	JUMP	ResumeDataIn

at 0x000000e4 : */	0x80080000,0x00000300,
/*

DisconnectDuringDataIn:
	CLEAR	ACK

at 0x000000e6 : */	0x60000040,0x00000000,
/*
	WAIT	DISCONNECT

at 0x000000e8 : */	0x48000000,0x00000000,
/*
	ENTRY	Disconnect3
Disconnect3:
	INT	DISCONNECT_DURING_DATA

at 0x000000ea : */	0x98080000,0x00000780,
/*
	ENTRY	Disconnect4
Disconnect4:
; we return here after a reselection
	CLEAR	ACK

at 0x000000ec : */	0x60000040,0x00000000,
/*
	JUMP	ResumeSendCommand

at 0x000000ee : */	0x80080000,0x000001e8,
/*


DisconnectAfterDataIn:
	CLEAR	ACK

at 0x000000f0 : */	0x60000040,0x00000000,
/*
	WAIT	DISCONNECT

at 0x000000f2 : */	0x48000000,0x00000000,
/*
	ENTRY	Disconnect5
Disconnect5:
	INT	DISCONNECT_AFTER_DATA

at 0x000000f4 : */	0x98080000,0x00000580,
/*
	ENTRY	Disconnect6
Disconnect6:
; we return here after a reselection
	CLEAR	ACK

at 0x000000f6 : */	0x60000040,0x00000000,
/*
	JUMP	ResumeDataIn

at 0x000000f8 : */	0x80080000,0x00000300,
/*

MsgInAfterDataOut:
	MOVE	1, ReceiveMsgAddress, WHEN MSG_IN

at 0x000000fa : */	0x0f000001,0x00000000,
/*
	JUMP	DisconnectAfterDataOut, if DISCONNECT_MSG

at 0x000000fc : */	0x800c0004,0x00000438,
/*
	JUMP	IgnoreMsgAfterData, IF SAVE_DATA_PTRS_MSG

at 0x000000fe : */	0x800c0002,0x00000428,
/*
	JUMP	IgnoreMsgAfterData, IF RESTORE_DATA_PTRS_MSG

at 0x00000100 : */	0x800c0003,0x00000428,
/*
	CALL	ProcessReceiveMessage

at 0x00000102 : */	0x88080000,0x000000a8,
/*
	INT	MSG_IN_AFTER_DATA_OUT

at 0x00000104 : */	0x98080000,0x00000650,
/*
	CLEAR	ACK

at 0x00000106 : */	0x60000040,0x00000000,
/*
	JUMP	ResumeDataOut

at 0x00000108 : */	0x80080000,0x00000340,
/*

IgnoreMsgAfterData:
	CLEAR	ACK

at 0x0000010a : */	0x60000040,0x00000000,
/*
; Data in and out do the same thing on resume, so pick one
	JUMP	ResumeDataIn

at 0x0000010c : */	0x80080000,0x00000300,
/*

DisconnectAfterDataOut:
	CLEAR	ACK

at 0x0000010e : */	0x60000040,0x00000000,
/*
	WAIT	DISCONNECT

at 0x00000110 : */	0x48000000,0x00000000,
/*
	ENTRY	Disconnect7
Disconnect7:
	INT	DISCONNECT_AFTER_DATA

at 0x00000112 : */	0x98080000,0x00000580,
/*
	ENTRY	Disconnect8
Disconnect8:
; we return here after a reselection
	CLEAR	ACK

at 0x00000114 : */	0x60000040,0x00000000,
/*
	JUMP	ResumeDataOut

at 0x00000116 : */	0x80080000,0x00000340,
/*

Finish:
	MOVE	1, StatusAddress, WHEN STATUS

at 0x00000118 : */	0x0b000001,0x00000000,
/*
	INT	NOT_MSG_IN_AFTER_STATUS, WHEN NOT MSG_IN

at 0x0000011a : */	0x9f030000,0x00000430,
/*
	MOVE	1, ReceiveMsgAddress, WHEN MSG_IN

at 0x0000011c : */	0x0f000001,0x00000000,
/*
	JUMP	FinishCommandComplete, IF COMMAND_COMPLETE_MSG

at 0x0000011e : */	0x800c0000,0x00000490,
/*
	CALL	ProcessReceiveMessage

at 0x00000120 : */	0x88080000,0x000000a8,
/*
	INT	MSG_IN_AFTER_STATUS

at 0x00000122 : */	0x98080000,0x00000440,
/*
	ENTRY	FinishCommandComplete
FinishCommandComplete:
	CLEAR	ACK

at 0x00000124 : */	0x60000040,0x00000000,
/*
	WAIT	DISCONNECT

at 0x00000126 : */	0x48000000,0x00000000,
/*
	ENTRY	Finish1
Finish1:
	INT	GOOD_STATUS_AFTER_STATUS

at 0x00000128 : */	0x98080000,0x00000401,
};

#define A_AFTER_CMD	0x00000300
static u32 A_AFTER_CMD_used[] __attribute((unused)) = {
};

#define A_AFTER_DATA_IN	0x00000500
static u32 A_AFTER_DATA_IN_used[] __attribute((unused)) = {
};

#define A_AFTER_DATA_OUT	0x00000600
static u32 A_AFTER_DATA_OUT_used[] __attribute((unused)) = {
};

#define A_AFTER_SELECTION	0x00000100
static u32 A_AFTER_SELECTION_used[] __attribute((unused)) = {
};

#define A_AFTER_STATUS	0x00000400
static u32 A_AFTER_STATUS_used[] __attribute((unused)) = {
};

#define A_BEFORE_CMD	0x00000200
static u32 A_BEFORE_CMD_used[] __attribute((unused)) = {
};

#define A_COMMAND_COMPLETE_MSG	0x00000000
static u32 A_COMMAND_COMPLETE_MSG_used[] __attribute((unused)) = {
	0x0000011e,
};

#define A_COMPLETED_SELECTION_AS_TARGET	0x00001001
static u32 A_COMPLETED_SELECTION_AS_TARGET_used[] __attribute((unused)) = {
	0x0000001d,
};

#define A_CommandAddress	0x00000000
static u32 A_CommandAddress_used[] __attribute((unused)) = {
	0x00000079,
};

#define A_CommandCount	0x00000000
static u32 A_CommandCount_used[] __attribute((unused)) = {
	0x00000078,
};

#define A_DATA_IN_AFTER_DATA_IN	0x000005a0
static u32 A_DATA_IN_AFTER_DATA_IN_used[] __attribute((unused)) = {
	0x000000cb,
};

#define A_DEBUG_INTERRUPT	0x00003000
static u32 A_DEBUG_INTERRUPT_used[] __attribute((unused)) = {
};

#define A_DEBUG_INTERRUPT1	0x00003001
static u32 A_DEBUG_INTERRUPT1_used[] __attribute((unused)) = {
};

#define A_DEBUG_INTERRUPT2	0x00003002
static u32 A_DEBUG_INTERRUPT2_used[] __attribute((unused)) = {
};

#define A_DEBUG_INTERRUPT3	0x00003003
static u32 A_DEBUG_INTERRUPT3_used[] __attribute((unused)) = {
};

#define A_DEBUG_INTERRUPT4	0x00003004
static u32 A_DEBUG_INTERRUPT4_used[] __attribute((unused)) = {
};

#define A_DEBUG_INTERRUPT5	0x00003005
static u32 A_DEBUG_INTERRUPT5_used[] __attribute((unused)) = {
};

#define A_DEBUG_INTERRUPT6	0x00003006
static u32 A_DEBUG_INTERRUPT6_used[] __attribute((unused)) = {
};

#define A_DISCONNECT	0x00000080
static u32 A_DISCONNECT_used[] __attribute((unused)) = {
};

#define A_DISCONNECT_AFTER_CMD	0x00000380
static u32 A_DISCONNECT_AFTER_CMD_used[] __attribute((unused)) = {
	0x000000ab,
};

#define A_DISCONNECT_AFTER_DATA	0x00000580
static u32 A_DISCONNECT_AFTER_DATA_used[] __attribute((unused)) = {
	0x000000f5,
	0x00000113,
};

#define A_DISCONNECT_DURING_DATA	0x00000780
static u32 A_DISCONNECT_DURING_DATA_used[] __attribute((unused)) = {
	0x000000eb,
};

#define A_DISCONNECT_MSG	0x00000004
static u32 A_DISCONNECT_MSG_used[] __attribute((unused)) = {
	0x0000008a,
	0x00000094,
	0x000000d8,
	0x000000fc,
};

#define A_DURING_DATA_IN	0x00000700
static u32 A_DURING_DATA_IN_used[] __attribute((unused)) = {
};

#define A_Device_ID	0x00000000
static u32 A_Device_ID_used[] __attribute((unused)) = {
	0x00000000,
};

#define A_EXTENDED_MSG	0x00000001
static u32 A_EXTENDED_MSG_used[] __attribute((unused)) = {
	0x0000002a,
};

#define A_FATAL	0x00002000
static u32 A_FATAL_used[] __attribute((unused)) = {
};

#define A_FATAL_ILLEGAL_MSG_LENGTH	0x00002003
static u32 A_FATAL_ILLEGAL_MSG_LENGTH_used[] __attribute((unused)) = {
	0x00000043,
};

#define A_FATAL_NOT_MSG_IN_AFTER_SELECTION	0x00002002
static u32 A_FATAL_NOT_MSG_IN_AFTER_SELECTION_used[] __attribute((unused)) = {
	0x0000000d,
};

#define A_FATAL_SEND_MSG	0x00002001
static u32 A_FATAL_SEND_MSG_used[] __attribute((unused)) = {
	0x00000023,
};

#define A_FATAL_UNEXPECTED_RESELECTION_MSG	0x00002000
static u32 A_FATAL_UNEXPECTED_RESELECTION_MSG_used[] __attribute((unused)) = {
};

#define A_GOOD_STATUS	0x00000001
static u32 A_GOOD_STATUS_used[] __attribute((unused)) = {
};

#define A_GOOD_STATUS_AFTER_STATUS	0x00000401
static u32 A_GOOD_STATUS_AFTER_STATUS_used[] __attribute((unused)) = {
	0x00000129,
};

#define A_IDENTIFY_MSG	0x00000080
static u32 A_IDENTIFY_MSG_used[] __attribute((unused)) = {
};

#define A_IDENTIFY_MSG_MASK	0x0000007f
static u32 A_IDENTIFY_MSG_MASK_used[] __attribute((unused)) = {
};

#define A_MSG_IN	0x00000050
static u32 A_MSG_IN_used[] __attribute((unused)) = {
};

#define A_MSG_IN_AFTER_CMD	0x00000350
static u32 A_MSG_IN_AFTER_CMD_used[] __attribute((unused)) = {
	0x0000009d,
};

#define A_MSG_IN_AFTER_DATA_IN	0x00000550
static u32 A_MSG_IN_AFTER_DATA_IN_used[] __attribute((unused)) = {
	0x000000e1,
};

#define A_MSG_IN_AFTER_DATA_OUT	0x00000650
static u32 A_MSG_IN_AFTER_DATA_OUT_used[] __attribute((unused)) = {
	0x00000105,
};

#define A_MSG_IN_AFTER_STATUS	0x00000440
static u32 A_MSG_IN_AFTER_STATUS_used[] __attribute((unused)) = {
	0x00000123,
};

#define A_MSG_IN_BEFORE_CMD	0x00000250
static u32 A_MSG_IN_BEFORE_CMD_used[] __attribute((unused)) = {
	0x000000b9,
};

#define A_MSG_IN_DURING_DATA_IN	0x00000750
static u32 A_MSG_IN_DURING_DATA_IN_used[] __attribute((unused)) = {
	0x00000091,
};

#define A_MSG_OUT	0x00000090
static u32 A_MSG_OUT_used[] __attribute((unused)) = {
};

#define A_MSG_OUT_AFTER_DATA_IN	0x00000590
static u32 A_MSG_OUT_AFTER_DATA_IN_used[] __attribute((unused)) = {
	0x000000c7,
};

#define A_MessageCount	0x00000000
static u32 A_MessageCount_used[] __attribute((unused)) = {
	0x0000001e,
};

#define A_MessageLocation	0x00000000
static u32 A_MessageLocation_used[] __attribute((unused)) = {
	0x0000001f,
};

#define A_NOT_MSG_IN	0x00000030
static u32 A_NOT_MSG_IN_used[] __attribute((unused)) = {
};

#define A_NOT_MSG_IN_AFTER_STATUS	0x00000430
static u32 A_NOT_MSG_IN_AFTER_STATUS_used[] __attribute((unused)) = {
	0x0000011b,
};

#define A_NOT_MSG_OUT	0x00000010
static u32 A_NOT_MSG_OUT_used[] __attribute((unused)) = {
};

#define A_NOT_MSG_OUT_AFTER_SELECTION	0x00000110
static u32 A_NOT_MSG_OUT_AFTER_SELECTION_used[] __attribute((unused)) = {
	0x00000007,
};

#define A_PARITY_ERROR_MSG	0x00000009
static u32 A_PARITY_ERROR_MSG_used[] __attribute((unused)) = {
};

#define A_REJECT_MSG	0x00000007
static u32 A_REJECT_MSG_used[] __attribute((unused)) = {
};

#define A_REJECT_MSG_BEFORE_CMD	0x00000270
static u32 A_REJECT_MSG_BEFORE_CMD_used[] __attribute((unused)) = {
};

#define A_REJECT_MSG_R	0x00000070
static u32 A_REJECT_MSG_R_used[] __attribute((unused)) = {
};

#define A_RESELECTED_DURING_SELECTION	0x00001000
static u32 A_RESELECTED_DURING_SELECTION_used[] __attribute((unused)) = {
	0x0000000b,
};

#define A_RESELECTION_IDENTIFIED	0x00001003
static u32 A_RESELECTION_IDENTIFIED_used[] __attribute((unused)) = {
	0x00000011,
	0x00000015,
};

#define A_RESTORE_DATA_PTRS_MSG	0x00000003
static u32 A_RESTORE_DATA_PTRS_MSG_used[] __attribute((unused)) = {
	0x0000008e,
	0x00000098,
	0x000000b4,
	0x000000dc,
	0x00000100,
};

#define A_ReceiveMsgAddress	0x00000000
static u32 A_ReceiveMsgAddress_used[] __attribute((unused)) = {
	0x0000000f,
	0x00000013,
	0x00000029,
	0x00000031,
	0x00000037,
	0x00000047,
	0x0000004d,
	0x00000053,
	0x00000059,
	0x0000005f,
	0x00000089,
	0x00000093,
	0x000000b1,
	0x000000d7,
	0x000000fb,
	0x0000011d,
};

#define A_SAVE_DATA_PTRS_MSG	0x00000002
static u32 A_SAVE_DATA_PTRS_MSG_used[] __attribute((unused)) = {
	0x0000008c,
	0x00000096,
	0x000000b2,
	0x000000da,
	0x000000fe,
};

#define A_SDTR_MSG	0x00000001
static u32 A_SDTR_MSG_used[] __attribute((unused)) = {
};

#define A_SDTR_MSG_AFTER_CMD	0x00000360
static u32 A_SDTR_MSG_AFTER_CMD_used[] __attribute((unused)) = {
};

#define A_SDTR_MSG_BEFORE_CMD	0x00000260
static u32 A_SDTR_MSG_BEFORE_CMD_used[] __attribute((unused)) = {
};

#define A_SDTR_MSG_R	0x00000060
static u32 A_SDTR_MSG_R_used[] __attribute((unused)) = {
};

#define A_SGScriptStartAddress	0x00000000
static u32 A_SGScriptStartAddress_used[] __attribute((unused)) = {
	0x000000bf,
	0x000000cf,
};

#define A_SIMPLE_TAG_MSG	0x00000020
static u32 A_SIMPLE_TAG_MSG_used[] __attribute((unused)) = {
};

#define A_StatusAddress	0x00000000
static u32 A_StatusAddress_used[] __attribute((unused)) = {
	0x00000119,
};

#define A_TWO_BYTE_MSG	0x00000020
static u32 A_TWO_BYTE_MSG_used[] __attribute((unused)) = {
	0x0000002c,
};

#define A_TWO_BYTE_MSG_MASK	0x0000000f
static u32 A_TWO_BYTE_MSG_MASK_used[] __attribute((unused)) = {
	0x0000002c,
};

#define A_UNEXPECTED_MSG	0x00000040
static u32 A_UNEXPECTED_MSG_used[] __attribute((unused)) = {
};

#define A_UNEXPECTED_MSG_BEFORE_CMD	0x00000240
static u32 A_UNEXPECTED_MSG_BEFORE_CMD_used[] __attribute((unused)) = {
};

#define A_UNEXPECTED_PHASE	0x00000020
static u32 A_UNEXPECTED_PHASE_used[] __attribute((unused)) = {
};

#define A_UNEXPECTED_PHASE_AFTER_CMD	0x00000320
static u32 A_UNEXPECTED_PHASE_AFTER_CMD_used[] __attribute((unused)) = {
	0x00000083,
};

#define A_UNEXPECTED_PHASE_AFTER_DATA_IN	0x00000520
static u32 A_UNEXPECTED_PHASE_AFTER_DATA_IN_used[] __attribute((unused)) = {
	0x000000c9,
};

#define A_UNEXPECTED_PHASE_AFTER_DATA_OUT	0x00000620
static u32 A_UNEXPECTED_PHASE_AFTER_DATA_OUT_used[] __attribute((unused)) = {
	0x000000d5,
};

#define A_UNEXPECTED_PHASE_BEFORE_CMD	0x00000220
static u32 A_UNEXPECTED_PHASE_BEFORE_CMD_used[] __attribute((unused)) = {
	0x00000077,
};

#define A_WDTR_MSG	0x00000003
static u32 A_WDTR_MSG_used[] __attribute((unused)) = {
};

#define A_WDTR_MSG_AFTER_CMD	0x000003a0
static u32 A_WDTR_MSG_AFTER_CMD_used[] __attribute((unused)) = {
};

#define A_WDTR_MSG_R	0x000000a0
static u32 A_WDTR_MSG_R_used[] __attribute((unused)) = {
};

#define Ent_Disconnect1	0x000002a8
#define Ent_Disconnect2	0x000002b0
#define Ent_Disconnect3	0x000003a8
#define Ent_Disconnect4	0x000003b0
#define Ent_Disconnect5	0x000003d0
#define Ent_Disconnect6	0x000003d8
#define Ent_Disconnect7	0x00000448
#define Ent_Disconnect8	0x00000450
#define Ent_Finish1	0x000004a0
#define Ent_Finish2	0x000004a8
#define Ent_FinishCommandComplete	0x00000490
#define Ent_GetReselectionData	0x00000038
#define Ent_GetReselectionWithTag	0x00000048
#define Ent_IgnoreMessage	0x00000188
#define Ent_MsgInDuringData	0x00000218
#define Ent_ReceiveMessage	0x000000a0
#define Ent_SelectedAsTarget	0x00000058
#define Ent_SendCommand	0x000001c8
#define Ent_SendMessage	0x00000078
#define Ent_SendMessagePhaseMismatch	0x00000090
#define Ent_SendMessageWithATN	0x00000198
#define Ent_StartUp	0x00000000
static u32 LABELPATCHES[] __attribute((unused)) = {
	0x00000001,
	0x00000003,
	0x00000005,
	0x00000009,
	0x00000027,
	0x0000002b,
	0x00000039,
	0x0000003b,
	0x0000003d,
	0x0000003f,
	0x00000041,
	0x0000006b,
	0x0000006d,
	0x00000073,
	0x00000075,
	0x0000007b,
	0x0000007d,
	0x0000007f,
	0x00000081,
	0x00000087,
	0x0000008b,
	0x0000008d,
	0x0000008f,
	0x00000095,
	0x00000097,
	0x00000099,
	0x0000009b,
	0x000000a1,
	0x000000a5,
	0x000000af,
	0x000000b3,
	0x000000b5,
	0x000000b7,
	0x000000bd,
	0x000000c1,
	0x000000c3,
	0x000000c5,
	0x000000cd,
	0x000000d1,
	0x000000d3,
	0x000000d9,
	0x000000db,
	0x000000dd,
	0x000000df,
	0x000000e5,
	0x000000ef,
	0x000000f9,
	0x000000fd,
	0x000000ff,
	0x00000101,
	0x00000103,
	0x00000109,
	0x0000010d,
	0x00000117,
	0x0000011f,
	0x00000121,
};

static struct {
	u32	offset;
	void		*address;
} EXTERNAL_PATCHES[] __attribute((unused)) = {
};

static u32 INSTRUCTIONS __attribute((unused))	= 149;
static u32 PATCHES __attribute((unused))	= 56;
static u32 EXTERNAL_PATCHES_LEN __attribute((unused))	= 0;
