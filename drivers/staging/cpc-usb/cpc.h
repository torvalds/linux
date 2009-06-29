/*
 * CPC CAN Interface Definitions
 *
 * Copyright (C) 2000-2008 EMS Dr. Thomas Wuensche
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 */
#ifndef CPC_HEADER
#define CPC_HEADER

// the maximum length of the union members within a CPC_MSG
// this value can be defined by the customer, but has to be
// >= 64 bytes
// however, if not defined before, we set a length of 64 byte
#if !defined(CPC_MSG_LEN) || (CPC_MSG_LEN < 64)
#undef CPC_MSG_LEN
#define CPC_MSG_LEN 64
#endif

// check the operating system used
#ifdef _WIN32 // running a Windows OS

// define basic types on Windows platforms
#ifdef _MSC_VER // Visual Studio
	typedef unsigned __int8 u8;
	typedef unsigned __int16 u16;
	typedef unsigned __int32 u32;
#else // Borland Compiler
	typedef unsigned char u8;
	typedef unsigned short u16;
	typedef unsigned int u32;
#endif
	// on Windows OS we use a byte alignment of 1
	#pragma pack(push, 1)

	// set the calling conventions for the library function calls
	#define CALL_CONV __stdcall
#else
	// Kernel headers already define this types
	#ifndef __KERNEL__
		// define basic types
		typedef unsigned char u8;
		typedef unsigned short u16;
		typedef unsigned int u32;
	#endif

	// Linux does not use this calling convention
	#define CALL_CONV
#endif

// Transmission of events from CPC interfaces to PC can be individually
// controlled per event type. Default state is: don't transmit
// Control values are constructed by bit-or of Subject and Action
// and passed to CPC_Control()

// Control-Values for CPC_Control() Command Subject Selection
#define CONTR_CAN_Message 0x04
#define CONTR_Busload	  0x08
#define	CONTR_CAN_State	  0x0C
#define	CONTR_SendAck	  0x10
#define	CONTR_Filter	  0x14
#define CONTR_CmdQueue    0x18	// reserved, do not use
#define CONTR_BusError    0x1C

// Control Command Actions
#define CONTR_CONT_OFF    0
#define CONTR_CONT_ON     1
#define CONTR_SING_ON     2
// CONTR_SING_ON doesn't change CONTR_CONT_ON state, so it should be
// read as: transmit at least once

// defines for confirmed request
#define DO_NOT_CONFIRM 0
#define DO_CONFIRM     1

// event flags
#define EVENT_READ 0x01
#define EVENT_WRITE 0x02

// Messages from CPC to PC contain a message object type field.
// The following message types are sent by CPC and can be used in
// handlers, others should be ignored.
#define CPC_MSG_T_RESYNC        0 // Normally to be ignored
#define CPC_MSG_T_CAN           1 // CAN data frame
#define CPC_MSG_T_BUSLOAD       2 // Busload message
#define CPC_MSG_T_STRING        3 // Normally to be ignored
#define CPC_MSG_T_CONTI         4 // Normally to be ignored
#define CPC_MSG_T_MEM           7 // Normally not to be handled
#define	CPC_MSG_T_RTR           8 // CAN remote frame
#define CPC_MSG_T_TXACK	        9 // Send acknowledge
#define CPC_MSG_T_POWERUP      10 // Power-up message
#define	CPC_MSG_T_CMD_NO       11 // Normally to be ignored
#define	CPC_MSG_T_CAN_PRMS     12 // Actual CAN parameters
#define	CPC_MSG_T_ABORTED      13 // Command aborted message
#define	CPC_MSG_T_CANSTATE     14 // CAN state message
#define CPC_MSG_T_RESET        15 // used to reset CAN-Controller
#define	CPC_MSG_T_XCAN         16 // XCAN data frame
#define CPC_MSG_T_XRTR         17 // XCAN remote frame
#define CPC_MSG_T_INFO         18 // information strings
#define CPC_MSG_T_CONTROL      19 // used for control of interface/driver behaviour
#define CPC_MSG_T_CONFIRM      20 // response type for confirmed requests
#define CPC_MSG_T_OVERRUN      21 // response type for overrun conditions
#define CPC_MSG_T_KEEPALIVE    22 // response type for keep alive conditions
#define CPC_MSG_T_CANERROR     23 // response type for bus error conditions
#define CPC_MSG_T_DISCONNECTED 24 // response type for a disconnected interface
#define CPC_MSG_T_ERR_COUNTER  25 // RX/TX error counter of CAN controller

#define CPC_MSG_T_FIRMWARE    100 // response type for USB firmware download

// Messages from the PC to the CPC interface contain a command field
// Most of the command types are wrapped by the library functions and have therefore
// normally not to be used.
// However, programmers who wish to circumvent the library and talk directly
// to the drivers (mainly Linux programmers) can use the following
// command types:

#define CPC_CMD_T_CAN                 1	// CAN data frame
#define CPC_CMD_T_CONTROL             3	// used for control of interface/driver behaviour
#define	CPC_CMD_T_CAN_PRMS            6	// set CAN parameters
#define	CPC_CMD_T_CLEARBUF            8	// clears input queue; this is depricated, use CPC_CMD_T_CLEAR_MSG_QUEUE instead
#define	CPC_CMD_T_INQ_CAN_PARMS      11	// inquire actual CAN parameters
#define	CPC_CMD_T_FILTER_PRMS        12	// set filter parameter
#define	CPC_CMD_T_RTR                13	// CAN remote frame
#define	CPC_CMD_T_CANSTATE           14	// CAN state message
#define	CPC_CMD_T_XCAN               15	// XCAN data frame
#define CPC_CMD_T_XRTR               16	// XCAN remote frame
#define CPC_CMD_T_RESET              17	// used to reset CAN-Controller
#define CPC_CMD_T_INQ_INFO           18	// miscellanous information strings
#define CPC_CMD_T_OPEN_CHAN          19	// open a channel
#define CPC_CMD_T_CLOSE_CHAN         20	// close a channel
#define CPC_CMD_T_CNTBUF             21	// this is depricated, use CPC_CMD_T_INQ_MSG_QUEUE_CNT instead
#define CPC_CMD_T_CAN_EXIT          200 // exit the CAN (disable interrupts; reset bootrate; reset output_cntr; mode = 1)

#define CPC_CMD_T_INQ_MSG_QUEUE_CNT  CPC_CMD_T_CNTBUF   // inquires the count of elements in the message queue
#define CPC_CMD_T_INQ_ERR_COUNTER    25	                // request the CAN controllers error counter
#define	CPC_CMD_T_CLEAR_MSG_QUEUE    CPC_CMD_T_CLEARBUF // clear CPC_MSG queue
#define	CPC_CMD_T_CLEAR_CMD_QUEUE    28	                // clear CPC_CMD queue
#define CPC_CMD_T_FIRMWARE          100                 // reserved, must not be used
#define CPC_CMD_T_USB_RESET         101                 // reserved, must not be used
#define CPC_CMD_T_WAIT_NOTIFY       102                 // reserved, must not be used
#define CPC_CMD_T_WAIT_SETUP        103                 // reserved, must not be used
#define	CPC_CMD_T_ABORT             255                 // Normally not to be used

// definitions for CPC_MSG_T_INFO
// information sources
#define CPC_INFOMSG_T_UNKNOWN_SOURCE 0
#define CPC_INFOMSG_T_INTERFACE      1
#define CPC_INFOMSG_T_DRIVER         2
#define CPC_INFOMSG_T_LIBRARY        3

// information types
#define CPC_INFOMSG_T_UNKNOWN_TYPE   0
#define CPC_INFOMSG_T_VERSION        1
#define CPC_INFOMSG_T_SERIAL         2

// definitions for controller types
#define PCA82C200   1 // Philips basic CAN controller, replaced by SJA1000
#define SJA1000     2 // Philips basic CAN controller
#define AN82527     3 // Intel full CAN controller
#define M16C_BASIC  4 // M16C controller running in basic CAN (not full CAN) mode

// channel open error codes
#define CPC_ERR_NO_FREE_CHANNEL            -1	// no more free space within the channel array
#define CPC_ERR_CHANNEL_ALREADY_OPEN       -2	// the channel is already open
#define CPC_ERR_CHANNEL_NOT_ACTIVE         -3	// access to a channel not active failed
#define CPC_ERR_NO_DRIVER_PRESENT          -4	// no driver at the location searched by the library
#define CPC_ERR_NO_INIFILE_PRESENT         -5	// the library could not find the inifile
#define CPC_ERR_WRONG_PARAMETERS           -6	// wrong parameters in the inifile
#define CPC_ERR_NO_INTERFACE_PRESENT       -7	// 1. The specified interface is not connected
						// 2. The interface (mostly CPC-USB) was disconnected upon operation
#define CPC_ERR_NO_MATCHING_CHANNEL        -8	// the driver couldn't find a matching channel
#define CPC_ERR_NO_BUFFER_AVAILABLE        -9	// the driver couldn't allocate buffer for messages
#define CPC_ERR_NO_INTERRUPT               -10	// the requested interrupt couldn't be claimed
#define CPC_ERR_NO_MATCHING_INTERFACE      -11	// no interface type related to this channel was found
#define CPC_ERR_NO_RESOURCES               -12	// the requested resources could not be claimed
#define CPC_ERR_SOCKET                     -13	// error concerning TCP sockets

// init error codes
#define CPC_ERR_WRONG_CONTROLLER_TYPE      -14	// wrong CAN controller type within initialization
#define CPC_ERR_NO_RESET_MODE              -15	// the controller could not be set into reset mode
#define CPC_ERR_NO_CAN_ACCESS              -16	// the CAN controller could not be accessed

// transmit error codes
#define CPC_ERR_CAN_WRONG_ID               -20	// the provided CAN id is too big
#define CPC_ERR_CAN_WRONG_LENGTH           -21	// the provided CAN length is too long
#define CPC_ERR_CAN_NO_TRANSMIT_BUF        -22	// the transmit buffer was occupied
#define CPC_ERR_CAN_TRANSMIT_TIMEOUT       -23	// The message could not be sent within a
						// specified time

// other error codes
#define CPC_ERR_SERVICE_NOT_SUPPORTED      -30	// the requested service is not supported by the interface
#define CPC_ERR_IO_TRANSFER                -31	// a transmission error down to the driver occurred
#define CPC_ERR_TRANSMISSION_FAILED        -32	// a transmission error down to the interface occurred
#define CPC_ERR_TRANSMISSION_TIMEOUT       -33	// a timeout occurred within transmission to the interface
#define CPC_ERR_OP_SYS_NOT_SUPPORTED       -35	// the operating system is not supported
#define CPC_ERR_UNKNOWN                    -40	// an unknown error ocurred (mostly IOCTL errors)

#define CPC_ERR_LOADING_DLL                -50	// the library 'cpcwin.dll' could not be loaded
#define CPC_ERR_ASSIGNING_FUNCTION         -51	// the specified function could not be assigned
#define CPC_ERR_DLL_INITIALIZATION         -52	// the DLL was not initialized correctly
#define CPC_ERR_MISSING_LICFILE            -55	// the file containing the licenses does not exist
#define CPC_ERR_MISSING_LICENSE            -56	// a required license was not found

// CAN state bit values. Ignore any bits not listed
#define CPC_CAN_STATE_BUSOFF     0x80
#define CPC_CAN_STATE_ERROR      0x40

// Mask to help ignore undefined bits
#define CPC_CAN_STATE_MASK       0xc0

// CAN-Message representation in a CPC_MSG
// Message object type is CPC_MSG_T_CAN or CPC_MSG_T_RTR
// or CPC_MSG_T_XCAN or CPC_MSG_T_XRTR
typedef struct CPC_CAN_MSG {
	u32 id;
	u8 length;
	u8 msg[8];
} CPC_CAN_MSG_T;


// representation of the CAN parameters for the PCA82C200 controller
typedef struct CPC_PCA82C200_PARAMS {
	u8 acc_code;	// Acceptance-code for receive, Standard: 0
	u8 acc_mask;	// Acceptance-mask for receive, Standard: 0xff (everything)
	u8 btr0;	// Bus-timing register 0
	u8 btr1;	// Bus-timing register 1
	u8 outp_contr;	// Output-control register
} CPC_PCA82C200_PARAMS_T;

// representation of the CAN parameters for the SJA1000 controller
typedef struct CPC_SJA1000_PARAMS {
	u8 mode;	// enables single or dual acceptance filtering
	u8 acc_code0;	// Acceptance-code for receive, Standard: 0
	u8 acc_code1;
	u8 acc_code2;
	u8 acc_code3;
	u8 acc_mask0;	// Acceptance-mask for receive, Standard: 0xff (everything)
	u8 acc_mask1;
	u8 acc_mask2;
	u8 acc_mask3;
	u8 btr0;	// Bus-timing register 0
	u8 btr1;	// Bus-timing register 1
	u8 outp_contr;	// Output-control register
} CPC_SJA1000_PARAMS_T;

// representation of the CAN parameters for the M16C controller
// in basic CAN mode (means no full CAN)
typedef struct CPC_M16C_BASIC_PARAMS {
	u8 con0;
	u8 con1;
	u8 ctlr0;
	u8 ctlr1;
	u8 clk;
	u8 acc_std_code0;
	u8 acc_std_code1;
	u8 acc_ext_code0;
	u8 acc_ext_code1;
	u8 acc_ext_code2;
	u8 acc_ext_code3;
	u8 acc_std_mask0;
	u8 acc_std_mask1;
	u8 acc_ext_mask0;
	u8 acc_ext_mask1;
	u8 acc_ext_mask2;
	u8 acc_ext_mask3;
} CPC_M16C_BASIC_PARAMS_T;

// CAN params message representation
typedef struct CPC_CAN_PARAMS {
	u8 cc_type;	// represents the controller type
	union {
		CPC_M16C_BASIC_PARAMS_T m16c_basic;
		CPC_SJA1000_PARAMS_T sja1000;
		CPC_PCA82C200_PARAMS_T pca82c200;
	} cc_params;
} CPC_CAN_PARAMS_T;

// the following structures are slightly different for Windows and Linux
// To be able to use the 'Select' mechanism with Linux the application
// needs to know the devices file desciptor.
// This mechanism is not implemented within Windows and the file descriptor
// is therefore not needed
#ifdef _WIN32

// CAN init params message representation
typedef struct CPC_INIT_PARAMS {
	CPC_CAN_PARAMS_T canparams;
} CPC_INIT_PARAMS_T;

#else// Linux

// CHAN init params representation
typedef struct CPC_CHAN_PARAMS {
	int fd;
} CPC_CHAN_PARAMS_T;

// CAN init params message representation
typedef struct CPC_INIT_PARAMS {
	CPC_CHAN_PARAMS_T chanparams;
	CPC_CAN_PARAMS_T canparams;
} CPC_INIT_PARAMS_T;

#endif

// structure for confirmed message handling
typedef struct CPC_CONFIRM {
	u8 result; // error code
} CPC_CONFIRM_T;

// structure for information requests
typedef struct CPC_INFO {
	u8 source;                 // interface, driver or library
	u8 type;                   // version or serial number
	char msg[CPC_MSG_LEN - 2]; // string holding the requested information
} CPC_INFO_T;

// OVERRUN ///////////////////////////////////////
// In general two types of overrun may occur.
// A hardware overrun, where the CAN controller
// lost a message, because the interrupt was
// not handled before the next messgae comes in.
// Or a software overrun, where i.e. a received
// message could not be stored in the CPC_MSG
// buffer.

// After a software overrun has occurred
// we wait until we have CPC_OVR_GAP slots
// free in the CPC_MSG buffer.
#define CPC_OVR_GAP               10

// Two types of software overrun may occur.
// A received CAN message or a CAN state event
// can cause an overrun.
// Note: A CPC_CMD which would normally store
// its result immediately in the CPC_MSG
// queue may fail, because the message queue is full.
// This will not generate an overrun message, but
// will halt command execution, until this command
// is able to store its message in the message queue.
#define CPC_OVR_EVENT_CAN       0x01
#define CPC_OVR_EVENT_CANSTATE  0x02
#define CPC_OVR_EVENT_BUSERROR  0x04

// If the CAN controller lost a message
// we indicate it with the highest bit
// set in the count field.
#define CPC_OVR_HW              0x80

// structure for overrun conditions
typedef struct {
	u8 event;
	u8 count;
} CPC_OVERRUN_T;

// CAN errors ////////////////////////////////////
// Each CAN controller type has different
// registers to record errors.
// Therefor a structure containing the specific
// errors is set up for each controller here

// SJA1000 error structure
// see the SJA1000 datasheet for detailed
// explanation of the registers
typedef struct CPC_SJA1000_CAN_ERROR {
	u8 ecc;   // error capture code register
	u8 rxerr; // RX error counter register
	u8 txerr; // TX error counter register
} CPC_SJA1000_CAN_ERROR_T;

// M16C error structure
// see the M16C datasheet for detailed
// explanation of the registers
typedef struct CPC_M16C_CAN_ERROR {
	u8 tbd;	// to be defined
} CPC_M16C_CAN_ERROR_T;

// structure for CAN error conditions
#define  CPC_CAN_ECODE_ERRFRAME   0x01
typedef struct CPC_CAN_ERROR {
	u8 ecode;
	struct {
		u8 cc_type; // CAN controller type
		union {
			CPC_SJA1000_CAN_ERROR_T sja1000;
			CPC_M16C_CAN_ERROR_T m16c;
		} regs;
	} cc;
} CPC_CAN_ERROR_T;

// Structure containing RX/TX error counter.
// This structure is used to request the
// values of the CAN controllers TX and RX
// error counter.
typedef struct CPC_CAN_ERR_COUNTER {
	u8 rx;
	u8 tx;
} CPC_CAN_ERR_COUNTER_T;

// If this flag is set, transmissions from PC to CPC are protected against loss
#define CPC_SECURE_TO_CPC	0x01

// If this flag is set, transmissions from CPC to PC are protected against loss
#define CPC_SECURE_TO_PC	0x02

// If this flag is set, the CAN-transmit buffer is checked to be free before sending a message
#define CPC_SECURE_SEND		0x04

// If this flag is set, the transmission complete flag is checked
// after sending a message
// THIS IS CURRENTLY ONLY IMPLEMENTED IN THE PASSIVE INTERFACE DRIVERS
#define CPC_SECURE_TRANSMIT	0x08

// main message type used between library and application
typedef struct CPC_MSG {
	u8 type;	// type of message
	u8 length;	// length of data within union 'msg'
	u8 msgid;	// confirmation handle
	u32 ts_sec;	// timestamp in seconds
	u32 ts_nsec;	// timestamp in nano seconds
	union {
		u8 generic[CPC_MSG_LEN];
		CPC_CAN_MSG_T canmsg;
		CPC_CAN_PARAMS_T canparams;
		CPC_CONFIRM_T confirmation;
		CPC_INFO_T info;
		CPC_OVERRUN_T overrun;
		CPC_CAN_ERROR_T error;
		CPC_CAN_ERR_COUNTER_T err_counter;
		u8 busload;
		u8 canstate;
	} msg;
} CPC_MSG_T;

#ifdef _WIN32
#pragma pack(pop)		// reset the byte alignment
#endif

#endif				// CPC_HEADER
