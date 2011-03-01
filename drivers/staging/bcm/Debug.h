/*
 * Debug.h
 *
 * Dynamic (runtime) debug framework implementation.
 * -kaiwan.
 */
#ifndef _DEBUG_H
#define _DEBUG_H
#include <linux/string.h>
#define NONE 0xFFFF


//--------------------------------------------------------------------------------

/* TYPE and SUBTYPE
 * Define valid TYPE (or category or code-path, however you like to think of it)
 * and SUBTYPE s.
 * Type and SubType are treated as bitmasks.
 */
/*-----------------BEGIN TYPEs------------------------------------------*/
#define DBG_TYPE_INITEXIT		(1 << 0)	// 1
#define DBG_TYPE_TX				(1 << 1)	// 2
#define DBG_TYPE_RX				(1 << 2)	// 4
#define DBG_TYPE_OTHERS			(1 << 3)	// 8
/*-----------------END TYPEs------------------------------------------*/
#define NUMTYPES			4		// careful!

/*-----------------BEGIN SUBTYPEs---------------------------------------*/

/*-SUBTYPEs for TX :  TYPE is DBG_TYPE_TX -----//
 Transmit.c ,Arp.c, LeakyBucket.c, And Qos.c
 total 17 macros */
// Transmit.c
#define TX 			1
#define MP_SEND  	(TX<<0)
#define NEXT_SEND   (TX<<1)
#define TX_FIFO  	(TX<<2)
#define TX_CONTROL 	(TX<<3)

// Arp.c
#define IP_ADDR  	(TX<<4)
#define ARP_REQ  	(TX<<5)
#define ARP_RESP 	(TX<<6)

// dhcp.c
//#define DHCP TX
//#define DHCP_REQ (DHCP<<7)

// Leakybucket.c
#define TOKEN_COUNTS (TX<<8)
#define CHECK_TOKENS (TX<<9)
#define TX_PACKETS   (TX<<10)
#define TIMER  		 (TX<<11)

// Qos.c
#define QOS TX
#define QUEUE_INDEX (QOS<<12)
#define IPV4_DBG 	(QOS<<13)
#define IPV6_DBG 	(QOS<<14)
#define PRUNE_QUEUE (QOS<<15)
#define SEND_QUEUE 	(QOS<<16)

//TX_Misc
#define TX_OSAL_DBG (TX<<17)


//--SUBTYPEs for ------INIT & EXIT---------------------
/*------------ TYPE is DBG_TYPE_INITEXIT -----//
DriverEntry.c, bcmfwup.c, ChipDetectTask.c, HaltnReset.c, InterfaceDDR.c */
#define MP 1
#define DRV_ENTRY 	(MP<<0)
#define MP_INIT  	(MP<<1)
#define READ_REG 	(MP<<3)
#define DISPATCH 	(MP<<2)
#define CLAIM_ADAP 	(MP<<4)
#define REG_IO_PORT (MP<<5)
#define INIT_DISP 	(MP<<6)
#define RX_INIT  	(MP<<7)


//-SUBTYPEs for --RX----------------------------------
//------------RX  :  TYPE is DBG_TYPE_RX -----//
// Receive.c
#define RX 1
#define RX_DPC  	(RX<<0)
#define RX_CTRL 	(RX<<3)
#define RX_DATA 	(RX<<4)
#define MP_RETURN 	(RX<<1)
#define LINK_MSG 	(RX<<2)


//-SUBTYPEs for ----OTHER ROUTINES------------------
//------------OTHERS  :  TYPE is DBG_TYPE_OTHER -----//
// HaltnReset,CheckForHang,PnP,Misc,CmHost
// total 12 macros
#define OTHERS 1
// ??ISR.C

#define ISR OTHERS
#define MP_DPC  (ISR<<0)

// HaltnReset.c
#define HALT OTHERS
#define MP_HALT  		(HALT<<1)
#define CHECK_HANG 		(HALT<<2)
#define MP_RESET 		(HALT<<3)
#define MP_SHUTDOWN 	(HALT<<4)

// pnp.c
#define PNP OTHERS
#define MP_PNP  		(PNP<<5)

// Misc.c
#define MISC OTHERS
#define DUMP_INFO 		(MISC<<6)
#define CLASSIFY 		(MISC<<7)
#define LINK_UP_MSG 	(MISC<<8)
#define CP_CTRL_PKT 	(MISC<<9)
#define DUMP_CONTROL 	(MISC<<10)
#define LED_DUMP_INFO 	(MISC<<11)

// CmHost.c
#define CMHOST OTHERS


#define SERIAL  		(OTHERS<<12)
#define IDLE_MODE 		(OTHERS<<13)

#define WRM   			(OTHERS<<14)
#define RDM   			(OTHERS<<15)

// TODO - put PHS_SEND in Tx PHS_RECEIVE in Rx path ?
#define PHS_SEND    	(OTHERS<<16)
#define PHS_RECIEVE 	(OTHERS<<17)
#define PHS_MODULE 	    (OTHERS<<18)

#define INTF_INIT    	(OTHERS<<19)
#define INTF_ERR     	(OTHERS<<20)
#define INTF_WARN    	(OTHERS<<21)
#define INTF_NORM 		(OTHERS<<22)

#define IRP_COMPLETION 	(OTHERS<<23)
#define SF_DESCRIPTOR_CNTS (OTHERS<<24)
#define PHS_DISPATCH 	(OTHERS << 25)
#define OSAL_DBG 		(OTHERS << 26)
#define NVM_RW      	(OTHERS << 27)

#define HOST_MIBS   	(OTHERS << 28)
#define CONN_MSG    	(CMHOST << 29)
//#define OTHERS_MISC		(OTHERS << 29)	// ProcSupport.c
/*-----------------END SUBTYPEs------------------------------------------*/


/* Debug level
 * We have 8 debug levels, in (numerical) increasing order of verbosity.
 * IMP: Currently implementing ONLY DBG_LVL_ALL , i.e. , all debug prints will
 * appear (of course, iff global debug flag is ON and we match the Type and SubType).
 * Finer granularity debug levels are currently not in use, although the feature exists.
 *
 * Another way to say this:
 * All the debug prints currently have 'debug_level' set to DBG_LVL_ALL .
 * You can compile-time change that to any of the below, if you wish to. However, as of now, there's
 * no dynamic facility to have the userspace 'TestApp' set debug_level. Slated for future expansion.
 */
#define BCM_ALL			7
#define	BCM_LOW			6
#define	BCM_PRINT		5
#define	BCM_NORMAL		4
#define	BCM_MEDIUM		3
#define	BCM_SCREAM		2
#define	BCM_ERR			1
/* Not meant for developer in debug prints.
 * To be used to disable all prints by setting the DBG_LVL_CURR to this value */
#define	BCM_NONE		0

/* The current driver logging level.
 * Everything at this level and (numerically) lower (meaning higher prio)
 * is logged.
* Replace 'BCM_ALL' in the DBG_LVL_CURR macro with the logging level desired.
 * For eg. to set the logging level to 'errors only' use:
 *	 #define DBG_LVL_CURR	(BCM_ERR)
 */

#define DBG_LVL_CURR	(BCM_ALL)
#define DBG_LVL_ALL		BCM_ALL

/*---Userspace mapping of Debug State.
 * Delibrately matches that of the Windows driver..
 * The TestApp's ioctl passes this struct to us.
 */
typedef struct
{
	unsigned int Subtype, Type;
	unsigned int OnOff;
//	unsigned int debug_level;	 /* future expansion */
} __attribute__((packed)) USER_BCM_DBG_STATE;

//---Kernel-space mapping of Debug State
typedef struct _S_BCM_DEBUG_STATE {
	UINT type;
	/* A bitmap of 32 bits for Subtype per Type.
	 * Valid indexes in 'subtype' array are *only* 1,2,4 and 8,
	 * corresponding to valid Type values. Hence we use the 'Type' field
	 * as the index value, ignoring the array entries 0,3,5,6,7 !
	 */
	UINT subtype[(NUMTYPES*2)+1];
	UINT debug_level;
} S_BCM_DEBUG_STATE;
/* Instantiated in the Adapter structure */
/* We'll reuse the debug level parameter to include a bit (the MSB) to indicate whether or not
 * we want the function's name printed.  */
#define DBG_NO_FUNC_PRINT	1 << 31
#define DBG_LVL_BITMASK		0xFF

//--- Only for direct printk's; "hidden" to API.
#define DBG_TYPE_PRINTK		3

#define BCM_DEBUG_PRINT(Adapter, Type, SubType, dbg_level, string, args...) \
	do {								\
		if (DBG_TYPE_PRINTK == Type)				\
			pr_info("%s:" string, __func__, ##args);	\
		else if (Adapter &&					\
			 (dbg_level & DBG_LVL_BITMASK) <= Adapter->stDebugState.debug_level && \
			 (Type & Adapter->stDebugState.type) &&		\
			 (SubType & Adapter->stDebugState.subtype[Type])) { \
			if (dbg_level & DBG_NO_FUNC_PRINT)		\
				printk(KERN_DEBUG string, ##args);	\
			else						\
				printk(KERN_DEBUG "%s:" string, __func__, ##args);	\
		}							\
	} while (0)

#define BCM_DEBUG_PRINT_BUFFER(Adapter, Type, SubType, dbg_level,  buffer, bufferlen) do { \
	if (DBG_TYPE_PRINTK == Type ||					\
	    (Adapter &&							\
	     (dbg_level & DBG_LVL_BITMASK) <= Adapter->stDebugState.debug_level  && \
	     (Type & Adapter->stDebugState.type) &&			\
	     (SubType & Adapter->stDebugState.subtype[Type]))) {	\
		printk(KERN_DEBUG "%s:\n", __func__);			\
		print_hex_dump(KERN_DEBUG, " ", DUMP_PREFIX_OFFSET,	\
			       16, 1, buffer, bufferlen, false);	\
	}								\
} while(0)


#define BCM_SHOW_DEBUG_BITMAP(Adapter)	do { \
	int i;									\
	for (i=0; i<(NUMTYPES*2)+1; i++) {		\
		if ((i == 1) || (i == 2) || (i == 4) || (i == 8)) {		\
		/* CAUTION! Forcefully turn on ALL debug paths and subpaths!	\
		Adapter->stDebugState.subtype[i] = 0xffffffff;	*/ \
		BCM_DEBUG_PRINT (Adapter, DBG_TYPE_PRINTK, 0, 0, "subtype[%d] = 0x%08x\n", 	\
		i, Adapter->stDebugState.subtype[i]);	\
		}	\
	}		\
} while (0)

#endif

