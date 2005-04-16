/* 
 * tms380tr.h: TI TMS380 Token Ring driver for Linux
 *
 * Authors:
 * - Christoph Goos <cgoos@syskonnect.de>
 * - Adam Fritzler <mid@auk.cx>
 */

#ifndef __LINUX_TMS380TR_H
#define __LINUX_TMS380TR_H

#ifdef __KERNEL__

#include <linux/interrupt.h>

/* module prototypes */
int tms380tr_open(struct net_device *dev);
int tms380tr_close(struct net_device *dev);
irqreturn_t tms380tr_interrupt(int irq, void *dev_id, struct pt_regs *regs);
int tmsdev_init(struct net_device *dev, unsigned long dmalimit,
		struct pci_dev *pdev);
void tmsdev_term(struct net_device *dev);
void tms380tr_wait(unsigned long time);

#define TMS380TR_MAX_ADAPTERS 7

#define SEND_TIMEOUT 10*HZ

#define TR_RCF_LONGEST_FRAME_MASK 0x0070
#define TR_RCF_FRAME4K 0x0030

/*------------------------------------------------------------------*/
/*  Bit order for adapter communication with DMA		    */
/*  --------------------------------------------------------------  */
/*  Bit  8 | 9| 10| 11|| 12| 13| 14| 15|| 0| 1| 2| 3|| 4| 5| 6| 7|  */
/*  --------------------------------------------------------------  */
/*  The bytes in a word must be byte swapped. Also, if a double	    */
/*  word is used for storage, then the words, as well as the bytes, */
/*  must be swapped. 						    */
/*  Bit order for adapter communication with DIO 		    */
/*  --------------------------------------------------------------  */
/*  Bit  0 | 1| 2| 3|| 4| 5| 6| 7|| 8| 9| 10| 11|| 12| 13| 14| 15|  */
/*  --------------------------------------------------------------  */
/*------------------------------------------------------------------*/

/* Swap words of a long.                        */
#define SWAPW(x) (((x) << 16) | ((x) >> 16))

/* Get the low byte of a word.                      */
#define LOBYTE(w)       ((unsigned char)(w))

/* Get the high byte of a word.                     */
#define HIBYTE(w)       ((unsigned char)((unsigned short)(w) >> 8))

/* Get the low word of a long.                      */
#define LOWORD(l)       ((unsigned short)(l))

/* Get the high word of a long.                     */
#define HIWORD(l)       ((unsigned short)((unsigned long)(l) >> 16))



/* Token ring adapter I/O addresses for normal mode. */

/*
 * The SIF registers.  Common to all adapters.
 */
/* Basic SIF (SRSX = 0) */
#define SIFDAT      		0x00	/* SIF/DMA data. */
#define SIFINC      		0x02  	/* IO Word data with auto increment. */
#define SIFINH      		0x03  	/* IO Byte data with auto increment. */
#define SIFADR      		0x04  	/* SIF/DMA Address. */
#define SIFCMD      		0x06  	/* SIF Command. */
#define SIFSTS      		0x06  	/* SIF Status. */

/* "Extended" SIF (SRSX = 1) */
#define SIFACL      		0x08  	/* SIF Adapter Control Register. */
#define SIFADD      		0x0a 	/* SIF/DMA Address. -- 0x0a */
#define SIFADX      		0x0c     /* 0x0c */
#define DMALEN      		0x0e 	/* SIF DMA length. -- 0x0e */

/*
 * POS Registers.  Only for ISA Adapters.
 */
#define POSREG      		0x10 	/* Adapter Program Option Select (POS)
			 		 * Register: base IO address + 16 byte.
			 		 */
#define POSREG_2    		24L 	/* only for TR4/16+ adapter
			 		 * base IO address + 24 byte. -- 0x18
			 		 */

/* SIFCMD command codes (high-low) */
#define CMD_INTERRUPT_ADAPTER   0x8000  /* Cause internal adapter interrupt */
#define CMD_ADAPTER_RESET   	0x4000  /* Hardware reset of adapter */
#define CMD_SSB_CLEAR		0x2000  /* Acknowledge to adapter to
					 * system interrupts.
					 */
#define CMD_EXECUTE		0x1000	/* Execute SCB command */
#define CMD_SCB_REQUEST		0x0800  /* Request adapter to interrupt
					 * system when SCB is available for
					 * another command.
					 */
#define CMD_RX_CONTINUE		0x0400  /* Continue receive after odd pointer
					 * stop. (odd pointer receive method)
					 */
#define CMD_RX_VALID		0x0200  /* Now actual RPL is valid. */
#define CMD_TX_VALID		0x0100  /* Now actual TPL is valid. (valid
					 * bit receive/transmit method)
					 */
#define CMD_SYSTEM_IRQ		0x0080  /* Adapter-to-attached-system
					 * interrupt is reset.
					 */
#define CMD_CLEAR_SYSTEM_IRQ	0x0080	/* Clear SYSTEM_INTERRUPT bit.
					 * (write: 1=ignore, 0=reset)
					 */
#define EXEC_SOFT_RESET		0xFF00  /* adapter soft reset. (restart
					 * adapter after hardware reset)
					 */


/* ACL commands (high-low) */
#define ACL_SWHLDA		0x0800  /* Software hold acknowledge. */
#define ACL_SWDDIR		0x0400  /* Data transfer direction. */
#define ACL_SWHRQ		0x0200  /* Pseudo DMA operation. */
#define ACL_PSDMAEN		0x0100  /* Enable pseudo system DMA. */
#define ACL_ARESET		0x0080  /* Adapter hardware reset command.
					 * (held in reset condition as
					 * long as bit is set)
					 */
#define ACL_CPHALT		0x0040  /* Communication processor halt.
					 * (can only be set while ACL_ARESET
					 * bit is set; prevents adapter
					 * processor from executing code while
					 * downloading firmware)
					 */
#define ACL_BOOT		0x0020
#define ACL_SINTEN		0x0008  /* System interrupt enable/disable
					 * (1/0): can be written if ACL_ARESET
					 * is zero.
					 */
#define ACL_PEN                 0x0004

#define ACL_NSELOUT0            0x0002 
#define ACL_NSELOUT1            0x0001	/* NSELOUTx have a card-specific
					 * meaning for setting ring speed.
					 */

#define PS_DMA_MASK		(ACL_SWHRQ | ACL_PSDMAEN)


/* SIFSTS register return codes (high-low) */
#define STS_SYSTEM_IRQ		0x0080	/* Adapter-to-attached-system
					 * interrupt is valid.
					 */
#define STS_INITIALIZE		0x0040  /* INITIALIZE status. (ready to
					 * initialize)
					 */
#define STS_TEST		0x0020  /* TEST status. (BUD not completed) */
#define STS_ERROR		0x0010  /* ERROR status. (unrecoverable
					 * HW error occurred)
					 */
#define STS_MASK		0x00F0  /* Mask interesting status bits. */
#define STS_ERROR_MASK		0x000F  /* Get Error Code by masking the
					 * interrupt code bits.
					 */
#define ADAPTER_INT_PTRS	0x0A00  /* Address offset of adapter internal
					 * pointers 01:0a00 (high-low) have to
					 * be read after init and before open.
					 */


/* Interrupt Codes (only MAC IRQs) */
#define STS_IRQ_ADAPTER_CHECK	0x0000	/* unrecoverable hardware or
					 * software error.
					 */ 
#define STS_IRQ_RING_STATUS	0x0004  /* SSB is updated with ring status. */
#define STS_IRQ_LLC_STATUS	0x0005	/* Not used in MAC-only microcode */
#define STS_IRQ_SCB_CLEAR	0x0006	/* SCB clear, following an
					 * SCB_REQUEST IRQ.
					 */
#define STS_IRQ_TIMER		0x0007	/* Not normally used in MAC ucode */
#define STS_IRQ_COMMAND_STATUS	0x0008	/* SSB is updated with command 
					 * status.
					 */ 
#define STS_IRQ_RECEIVE_STATUS	0x000A	/* SSB is updated with receive
					 * status.
					 */
#define STS_IRQ_TRANSMIT_STATUS	0x000C	/* SSB is updated with transmit
                                         * status
					 */
#define STS_IRQ_RECEIVE_PENDING	0x000E	/* Not used in MAC-only microcode */
#define STS_IRQ_MASK		0x000F	/* = STS_ERROR_MASK. */


/* TRANSMIT_STATUS completion code: (SSB.Parm[0]) */
#define COMMAND_COMPLETE	0x0080	/* TRANSMIT command completed
                                         * (avoid this!) issue another transmit
					 * to send additional frames.
					 */
#define FRAME_COMPLETE		0x0040	/* Frame has been transmitted;
					 * INTERRUPT_FRAME bit was set in the
					 * CSTAT request; indication of possibly
					 * more than one frame transmissions!
					 * SSB.Parm[0-1]: 32 bit pointer to
					 * TPL of last frame.
					 */
#define LIST_ERROR		0x0020	/* Error in one of the TPLs that
					 * compose the frame; TRANSMIT
					 * terminated; Parm[1-2]: 32bit pointer
					 * to TPL which starts the error
					 * frame; error details in bits 8-13.
					 * (14?)
					 */
#define FRAME_SIZE_ERROR	0x8000	/* FRAME_SIZE does not equal the sum of
					 * the valid DATA_COUNT fields;
					 * FRAME_SIZE less than header plus
					 * information field. (15 bytes +
					 * routing field) Or if FRAME_SIZE
					 * was specified as zero in one list.
					 */
#define TX_THRESHOLD		0x4000	/* FRAME_SIZE greater than (BUFFER_SIZE
					 * - 9) * TX_BUF_MAX.
					 */
#define ODD_ADDRESS		0x2000	/* Odd forward pointer value is
					 * read on a list without END_FRAME
					 * indication.
					 */
#define FRAME_ERROR		0x1000	/* START_FRAME bit (not) anticipated,
					 * but (not) set.
					 */
#define ACCESS_PRIORITY_ERROR	0x0800	/* Access priority requested has not
					 * been allowed.
					 */
#define UNENABLED_MAC_FRAME	0x0400	/* MAC frame has source class of zero
					 * or MAC frame PCF ATTN field is
					 * greater than one.
					 */
#define ILLEGAL_FRAME_FORMAT	0x0200	/* Bit 0 or FC field was set to one. */


/*
 * Since we need to support some functions even if the adapter is in a
 * CLOSED state, we have a (pseudo-) command queue which holds commands
 * that are outstandig to be executed.
 *
 * Each time a command completes, an interrupt occurs and the next
 * command is executed. The command queue is actually a simple word with 
 * a bit for each outstandig command. Therefore the commands will not be
 * executed in the order they have been queued.
 *
 * The following defines the command code bits and the command queue:
 */
#define OC_OPEN			0x0001	/* OPEN command */
#define OC_TRANSMIT		0x0002	/* TRANSMIT command */
#define OC_TRANSMIT_HALT	0x0004	/* TRANSMIT_HALT command */
#define OC_RECEIVE		0x0008	/* RECEIVE command */
#define OC_CLOSE		0x0010	/* CLOSE command */
#define OC_SET_GROUP_ADDR	0x0020	/* SET_GROUP_ADDR command */
#define OC_SET_FUNCT_ADDR	0x0040	/* SET_FUNCT_ADDR command */
#define OC_READ_ERROR_LOG	0x0080	/* READ_ERROR_LOG command */
#define OC_READ_ADAPTER		0x0100	/* READ_ADAPTER command */
#define OC_MODIFY_OPEN_PARMS	0x0400	/* MODIFY_OPEN_PARMS command */
#define OC_RESTORE_OPEN_PARMS	0x0800	/* RESTORE_OPEN_PARMS command */
#define OC_SET_FIRST_16_GROUP	0x1000	/* SET_FIRST_16_GROUP command */
#define OC_SET_BRIDGE_PARMS	0x2000	/* SET_BRIDGE_PARMS command */
#define OC_CONFIG_BRIDGE_PARMS	0x4000	/* CONFIG_BRIDGE_PARMS command */

#define OPEN			0x0300	/* C: open command. S: completion. */
#define TRANSMIT		0x0400	/* C: transmit command. S: completion
					 * status. (reject: COMMAND_REJECT if
					 * adapter not opened, TRANSMIT already
					 * issued or address passed in the SCB
					 * not word aligned)
					 */
#define TRANSMIT_HALT		0x0500	/* C: interrupt TX TPL chain; if no
					 * TRANSMIT command issued, the command
					 * is ignored (completion with TRANSMIT
					 * status (0x0400)!)
					 */
#define RECEIVE			0x0600	/* C: receive command. S: completion
					 * status. (reject: COMMAND_REJECT if
					 * adapter not opened, RECEIVE already
					 * issued or address passed in the SCB 
					 * not word aligned)
					 */
#define CLOSE			0x0700	/* C: close adapter. S: completion.
					 * (COMMAND_REJECT if adapter not open)
					 */
#define SET_GROUP_ADDR		0x0800	/* C: alter adapter group address after
					 * OPEN. S: completion. (COMMAND_REJECT
					 * if adapter not open)
					 */
#define SET_FUNCT_ADDR		0x0900	/* C: alter adapter functional address
					 * after OPEN. S: completion.
					 * (COMMAND_REJECT if adapter not open)
					 */
#define READ_ERROR_LOG		0x0A00	/* C: read adapter error counters.
					 * S: completion. (command ignored
					 * if adapter not open!)
					 */
#define READ_ADAPTER		0x0B00	/* C: read data from adapter memory.
					 * (important: after init and before
					 * open!) S: completion. (ADAPTER_CHECK
					 * interrupt if undefined storage area
					 * read)
					 */
#define MODIFY_OPEN_PARMS	0x0D00	/* C: modify some adapter operational
					 * parameters. (bit correspondend to
					 * WRAP_INTERFACE is ignored)
					 * S: completion. (reject: 
					 * COMMAND_REJECT)
					 */
#define RESTORE_OPEN_PARMS	0x0E00	/* C: modify some adapter operational
					 * parameters. (bit correspondend
					 * to WRAP_INTERFACE is ignored)
					 * S: completion. (reject:
					 * COMMAND_REJECT)
					 */
#define SET_FIRST_16_GROUP	0x0F00	/* C: alter the first two bytes in
					 * adapter group address.
					 * S: completion. (reject:
					 * COMMAND_REJECT)
					 */
#define SET_BRIDGE_PARMS	0x1000	/* C: values and conditions for the
					 * adapter hardware to use when frames
					 * are copied for forwarding.
					 * S: completion. (reject:
					 * COMMAND_REJECT)
					 */
#define CONFIG_BRIDGE_PARMS	0x1100	/* C: ..
					 * S: completion. (reject:
					 * COMMAND_REJECT)
					 */

#define SPEED_4			4
#define SPEED_16		16	/* Default transmission speed  */


/* Initialization Parameter Block (IPB); word alignment necessary! */
#define BURST_SIZE	0x0018	/* Default burst size */
#define BURST_MODE	0x9F00	/* Burst mode enable */
#define DMA_RETRIES	0x0505	/* Magic DMA retry number... */

#define CYCLE_TIME	3	/* Default AT-bus cycle time: 500 ns
				 * (later adapter version: fix  cycle time!)
				 */
#define LINE_SPEED_BIT	0x80

/* Macro definition for the wait function. */
#define ONE_SECOND_TICKS	1000000
#define HALF_SECOND		(ONE_SECOND_TICKS / 2)
#define ONE_SECOND		(ONE_SECOND_TICKS)
#define TWO_SECONDS		(ONE_SECOND_TICKS * 2)
#define THREE_SECONDS		(ONE_SECOND_TICKS * 3)
#define FOUR_SECONDS		(ONE_SECOND_TICKS * 4)
#define FIVE_SECONDS		(ONE_SECOND_TICKS * 5)

#define BUFFER_SIZE 		2048	/* Buffers on Adapter */

#pragma pack(1)
typedef struct {
	unsigned short Init_Options;	/* Initialize with burst mode;
					 * LLC disabled. (MAC only)
					 */

	/* Interrupt vectors the adapter places on attached system bus. */
	u_int8_t  CMD_Status_IV;    /* Interrupt vector: command status. */
	u_int8_t  TX_IV;	    /* Interrupt vector: transmit. */
	u_int8_t  RX_IV;	    /* Interrupt vector: receive. */
	u_int8_t  Ring_Status_IV;   /* Interrupt vector: ring status. */
	u_int8_t  SCB_Clear_IV;	    /* Interrupt vector: SCB clear. */
	u_int8_t  Adapter_CHK_IV;   /* Interrupt vector: adapter check. */

	u_int16_t RX_Burst_Size;    /* Max. number of transfer cycles. */
	u_int16_t TX_Burst_Size;    /* During DMA burst; even value! */
	u_int16_t DMA_Abort_Thrhld; /* Number of DMA retries. */

	u_int32_t SCB_Addr;   /* SCB address: even, word aligned, high-low */
	u_int32_t SSB_Addr;   /* SSB address: even, word aligned, high-low */
} IPB, *IPB_Ptr;
#pragma pack()

/*
 * OPEN Command Parameter List (OCPL) (can be reused, if the adapter has to
 * be reopened)
 */
#define BUFFER_SIZE	2048		/* Buffers on Adapter. */
#define TPL_SIZE	8+6*TX_FRAG_NUM /* Depending on fragments per TPL. */
#define RPL_SIZE	14		/* (with TI firmware v2.26 handling
					 * up to nine fragments possible)
					 */
#define TX_BUF_MIN	20		/* ??? (Stephan: calculation with */
#define TX_BUF_MAX	40		/* BUFFER_SIZE and MAX_FRAME_SIZE) ??? 
					 */
#define DISABLE_EARLY_TOKEN_RELEASE 	0x1000

/* OPEN Options (high-low) */
#define WRAP_INTERFACE		0x0080	/* Inserting omitted for test
					 * purposes; transmit data appears
					 * as receive data. (useful for
					 * testing; change: CLOSE necessary)
					 */
#define DISABLE_HARD_ERROR	0x0040	/* On HARD_ERROR & TRANSMIT_BEACON
					 * no RING.STATUS interrupt.
					 */
#define DISABLE_SOFT_ERROR	0x0020	/* On SOFT_ERROR, no RING.STATUS
					 * interrupt.
					 */
#define PASS_ADAPTER_MAC_FRAMES	0x0010	/* Passing unsupported MAC frames
					 * to system.
					 */
#define PASS_ATTENTION_FRAMES	0x0008	/* All changed attention MAC frames are
					 * passed to the system.
					 */
#define PAD_ROUTING_FIELD	0x0004	/* Routing field is padded to 18
					 * bytes.
					 */
#define FRAME_HOLD		0x0002	/*Adapter waits for entire frame before
					 * initiating DMA transfer; otherwise:
					 * DMA transfer initiation if internal
					 * buffer filled.
					 */
#define CONTENDER		0x0001	/* Adapter participates in the monitor
					 * contention process.
					 */
#define PASS_BEACON_MAC_FRAMES	0x8000	/* Adapter passes beacon MAC frames
					 * to the system.
					 */
#define EARLY_TOKEN_RELEASE 	0x1000	/* Only valid in 16 Mbps operation;
					 * 0 = ETR. (no effect in 4 Mbps
					 * operation)
					 */
#define COPY_ALL_MAC_FRAMES	0x0400	/* All MAC frames are copied to
					 * the system. (after OPEN: duplicate
					 * address test (DAT) MAC frame is 
					 * first received frame copied to the
					 * system)
					 */
#define COPY_ALL_NON_MAC_FRAMES	0x0200	/* All non MAC frames are copied to
					 * the system.
					 */
#define PASS_FIRST_BUF_ONLY	0x0100	/* Passes only first internal buffer
					 * of each received frame; FrameSize
					 * of RPLs must contain internal
					 * BUFFER_SIZE bits for promiscous mode.
					 */
#define ENABLE_FULL_DUPLEX_SELECTION	0x2000 
 					/* Enable the use of full-duplex
					 * settings with bits in byte 22 in
					 * ocpl. (new feature in firmware
					 * version 3.09)
					 */

/* Full-duplex settings */
#define OPEN_FULL_DUPLEX_OFF	0x0000
#define OPEN_FULL_DUPLEX_ON	0x00c0
#define OPEN_FULL_DUPLEX_AUTO	0x0080

#define PROD_ID_SIZE	18	/* Length of product ID. */

#define TX_FRAG_NUM	3	 /* Number of fragments used in one TPL. */
#define TX_MORE_FRAGMENTS 0x8000 /* Bit set in DataCount to indicate more
				  * fragments following.
				  */

/* XXX is there some better way to do this? */
#define ISA_MAX_ADDRESS 	0x00ffffff
#define PCI_MAX_ADDRESS		0xffffffff

#pragma pack(1)
typedef struct {
	u_int16_t OPENOptions;
	u_int8_t  NodeAddr[6];	/* Adapter node address; use ROM 
				 * address
				 */
	u_int32_t GroupAddr;	/* Multicast: high order
				 * bytes = 0xC000
				 */
	u_int32_t FunctAddr;	/* High order bytes = 0xC000 */
	u_int16_t RxListSize;	/* RPL size: 0 (=26), 14, 20 or
				 * 26 bytes read by the adapter.
				 * (Depending on the number of 
				 * fragments/list)
				 */
	u_int16_t TxListSize;	/* TPL size */
	u_int16_t BufSize;	/* Is automatically rounded up to the
				 * nearest nK boundary.
				 */
	u_int16_t FullDuplex;
	u_int16_t Reserved;
	u_int8_t  TXBufMin;	/* Number of adapter buffers reserved
				 * for transmission a minimum of 2
				 * buffers must be allocated.
				 */
	u_int8_t  TXBufMax;	/* Maximum number of adapter buffers
				 * for transmit; a minimum of 2 buffers
				 * must be available for receive.
				 * Default: 6
				 */
	u_int16_t ProdIDAddr[2];/* Pointer to product ID. */
} OPB, *OPB_Ptr;
#pragma pack()

/*
 * SCB: adapter commands enabled by the host system started by writing
 * CMD_INTERRUPT_ADAPTER | CMD_EXECUTE (|SCB_REQUEST) to the SIFCMD IO
 * register. (special case: | CMD_SYSTEM_IRQ for initialization)
 */
#pragma pack(1)
typedef struct {
	u_int16_t CMD;		/* Command code */
	u_int16_t Parm[2];	/* Pointer to Command Parameter Block */
} SCB;	/* System Command Block (32 bit physical address; big endian)*/
#pragma pack()

/*
 * SSB: adapter command return status can be evaluated after COMMAND_STATUS
 * adapter to system interrupt after reading SSB, the availability of the SSB
 * has to be told the adapter by writing CMD_INTERRUPT_ADAPTER | CMD_SSB_CLEAR
 * in the SIFCMD IO register.
 */
#pragma pack(1)
typedef struct {
	u_int16_t STS;		/* Status code */
	u_int16_t Parm[3];	/* Parameter or pointer to Status Parameter
				 * Block.
				 */
} SSB;	/* System Status Block (big endian - physical address)  */
#pragma pack()

typedef struct {
	unsigned short BurnedInAddrPtr;	/* Pointer to adapter burned in
					 * address. (BIA)
					 */
	unsigned short SoftwareLevelPtr;/* Pointer to software level data. */
	unsigned short AdapterAddrPtr;	/* Pointer to adapter addresses. */
	unsigned short AdapterParmsPtr;	/* Pointer to adapter parameters. */
	unsigned short MACBufferPtr;	/* Pointer to MAC buffer. (internal) */
	unsigned short LLCCountersPtr;	/* Pointer to LLC counters.  */
	unsigned short SpeedFlagPtr;	/* Pointer to data rate flag.
					 * (4/16 Mbps)
					 */
	unsigned short AdapterRAMPtr;	/* Pointer to adapter RAM found. (KB) */
} INTPTRS;	/* Adapter internal pointers */

#pragma pack(1)
typedef struct {
	u_int8_t  Line_Error;		/* Line error: code violation in
					 * frame or in a token, or FCS error.
					 */
	u_int8_t  Internal_Error;	/* IBM specific. (Reserved_1) */
	u_int8_t  Burst_Error;
	u_int8_t  ARI_FCI_Error;	/* ARI/FCI bit zero in AMP or
					 * SMP MAC frame.
					 */
	u_int8_t  AbortDelimeters;	/* IBM specific. (Reserved_2) */
	u_int8_t  Reserved_3;
	u_int8_t  Lost_Frame_Error;	/* Receive of end of transmitted
					 * frame failed.
					 */
	u_int8_t  Rx_Congest_Error;	/* Adapter in repeat mode has not
					 * enough buffer space to copy incoming
					 * frame.
					 */
	u_int8_t  Frame_Copied_Error;	/* ARI bit not zero in frame
					 * addressed to adapter.
					 */
	u_int8_t  Frequency_Error;	/* IBM specific. (Reserved_4) */
	u_int8_t  Token_Error;		/* (active only in monitor station) */
	u_int8_t  Reserved_5;
	u_int8_t  DMA_Bus_Error;	/* DMA bus errors not exceeding the
					 * abort thresholds.
					 */
	u_int8_t  DMA_Parity_Error;	/* DMA parity errors not exceeding
					 * the abort thresholds.
					 */
} ERRORTAB;	/* Adapter error counters */
#pragma pack()


/*--------------------- Send and Receive definitions -------------------*/
#pragma pack(1)
typedef struct {
	u_int16_t DataCount;	/* Value 0, even and odd values are
				 * permitted; value is unaltered most
				 * significant bit set: following
				 * fragments last fragment: most
				 * significant bit is not evaluated.
				 * (???)
				 */
	u_int32_t DataAddr;	/* Pointer to frame data fragment;
				 * even or odd.
				 */
} Fragment;
#pragma pack()

#define MAX_FRAG_NUMBERS    9	/* Maximal number of fragments possible to use
				 * in one RPL/TPL. (depending on TI firmware 
				 * version)
				 */

/*
 * AC (1), FC (1), Dst (6), Src (6), RIF (18), Data (4472) = 4504
 * The packet size can be one of the follows: 548, 1502, 2084, 4504, 8176,
 * 11439, 17832. Refer to TMS380 Second Generation Token Ring User's Guide
 * Page 2-27.
 */
#define HEADER_SIZE		(1 + 1 + 6 + 6)
#define SRC_SIZE		18
#define MIN_DATA_SIZE		516
#define DEFAULT_DATA_SIZE	4472
#define MAX_DATA_SIZE		17800

#define DEFAULT_PACKET_SIZE (HEADER_SIZE + SRC_SIZE + DEFAULT_DATA_SIZE)
#define MIN_PACKET_SIZE     (HEADER_SIZE + SRC_SIZE + MIN_DATA_SIZE)
#define MAX_PACKET_SIZE     (HEADER_SIZE + SRC_SIZE + MAX_DATA_SIZE)

/*
 * Macros to deal with the frame status field.
 */
#define AC_NOT_RECOGNIZED	0x00
#define GROUP_BIT		0x80
#define GET_TRANSMIT_STATUS_HIGH_BYTE(Ts) ((unsigned char)((Ts) >> 8))
#define GET_FRAME_STATUS_HIGH_AC(Fs)	  ((unsigned char)(((Fs) & 0xC0) >> 6))
#define GET_FRAME_STATUS_LOW_AC(Fs)       ((unsigned char)(((Fs) & 0x0C) >> 2))
#define DIRECTED_FRAME(Context)           (!((Context)->MData[2] & GROUP_BIT))


/*--------------------- Send Functions ---------------------------------*/
/* define TX_CSTAT _REQUEST (R) and _COMPLETE (C) values (high-low) */

#define TX_VALID		0x0080	/* R: set via TRANSMIT.VALID interrupt.
					 * C: always reset to zero!
					 */
#define TX_FRAME_COMPLETE	0x0040	/* R: must be reset to zero.
					 * C: set to one.
					 */
#define TX_START_FRAME		0x0020  /* R: start of a frame: 1 
					 * C: unchanged.
					 */
#define TX_END_FRAME		0x0010  /* R: end of a frame: 1
					 * C: unchanged.
					 */
#define TX_FRAME_IRQ		0x0008  /* R: request interrupt generation
					 * after transmission.
					 * C: unchanged.
					 */
#define TX_ERROR		0x0004  /* R: reserved.
					 * C: set to one if Error occurred.
					 */
#define TX_INTERFRAME_WAIT	0x0004
#define TX_PASS_CRC		0x0002  /* R: set if CRC value is already
					 * calculated. (valid only in
					 * FRAME_START TPL)
					 * C: unchanged.
					 */
#define TX_PASS_SRC_ADDR	0x0001  /* R: adapter uses explicit frame
					 * source address and does not overwrite
					 * with the adapter node address.
					 * (valid only in FRAME_START TPL)
					 *
					 * C: unchanged.
					 */
#define TX_STRIP_FS		0xFF00  /* R: reserved.
					 * C: if no Transmission Error,
					 * field contains copy of FS byte after
					 * stripping of frame.
					 */

/*
 * Structure of Transmit Parameter Lists (TPLs) (only one frame every TPL,
 * but possibly multiple TPLs for one frame) the length of the TPLs has to be
 * initialized in the OPL. (OPEN parameter list)
 */
#define TPL_NUM		3	/* Number of Transmit Parameter Lists.
				 * !! MUST BE >= 3 !!
				 */

#pragma pack(1)
typedef struct s_TPL TPL;

struct s_TPL {	/* Transmit Parameter List (align on even word boundaries) */
	u_int32_t NextTPLAddr;		/* Pointer to next TPL in chain; if
					 * pointer is odd: this is the last
					 * TPL. Pointing to itself can cause
					 * problems!
					 */
	volatile u_int16_t Status;	/* Initialized by the adapter:
					 * CSTAT_REQUEST important: update least
					 * significant bit first! Set by the
					 * adapter: CSTAT_COMPLETE status.
					 */
	u_int16_t FrameSize;		/* Number of bytes to be transmitted
					 * as a frame including AC/FC,
					 * Destination, Source, Routing field
					 * not including CRC, FS, End Delimiter
					 * (valid only if START_FRAME bit in 
					 * CSTAT nonzero) must not be zero in
					 * any list; maximum value: (BUFFER_SIZE
					 * - 8) * TX_BUF_MAX sum of DataCount
					 * values in FragmentList must equal
					 * Frame_Size value in START_FRAME TPL!
					 * frame data fragment list.
					 */

	/* TPL/RPL size in OPEN parameter list depending on maximal
	 * numbers of fragments used in one parameter list.
	 */
	Fragment FragList[TX_FRAG_NUM];	/* Maximum: nine frame fragments in one
					 * TPL actual version of firmware: 9
					 * fragments possible.
					 */
#pragma pack()

	/* Special proprietary data and precalculations */

	TPL *NextTPLPtr;		/* Pointer to next TPL in chain. */
	unsigned char *MData;
	struct sk_buff *Skb;
	unsigned char TPLIndex;
	volatile unsigned char BusyFlag;/* Flag: TPL busy? */
	dma_addr_t DMABuff;		/* DMA IO bus address from pci_map */
};

/* ---------------------Receive Functions-------------------------------*
 * define RECEIVE_CSTAT_REQUEST (R) and RECEIVE_CSTAT_COMPLETE (C) values.
 * (high-low)
 */
#define RX_VALID		0x0080	/* R: set; tell adapter with
					 * RECEIVE.VALID interrupt.
					 * C: reset to zero.
					 */
#define RX_FRAME_COMPLETE	0x0040  /* R: must be reset to zero,
					 * C: set to one.
					 */
#define RX_START_FRAME		0x0020  /* R: must be reset to zero.
					 * C: set to one on the list.
					 */
#define RX_END_FRAME		0x0010  /* R: must be reset to zero.
					 * C: set to one on the list
					 * that ends the frame.
					 */
#define RX_FRAME_IRQ		0x0008  /* R: request interrupt generation
					 * after receive.
					 * C: unchanged.
					 */
#define RX_INTERFRAME_WAIT	0x0004  /* R: after receiving a frame:
					 * interrupt and wait for a
					 * RECEIVE.CONTINUE.
					 * C: unchanged.
					 */
#define RX_PASS_CRC		0x0002  /* R: if set, the adapter includes
					 * the CRC in data passed. (last four 
					 * bytes; valid only if FRAME_START is
					 * set)
					 * C: set, if CRC is included in
					 * received data.
					 */
#define RX_PASS_SRC_ADDR	0x0001  /* R: adapter uses explicit frame
					 * source address and does not
					 * overwrite with the adapter node
					 * address. (valid only if FRAME_START
					 * is set)
					 * C: unchanged.
					 */
#define RX_RECEIVE_FS		0xFC00  /* R: reserved; must be reset to zero.
					 * C: on lists with START_FRAME, field
					 * contains frame status field from
					 * received frame; otherwise cleared.
					 */
#define RX_ADDR_MATCH		0x0300  /* R: reserved; must be reset to zero.
					 * C: address match code mask.
					 */ 
#define RX_STATUS_MASK		0x00FF  /* Mask for receive status bits. */

#define RX_INTERN_ADDR_MATCH    0x0100  /* C: internally address match. */
#define RX_EXTERN_ADDR_MATCH    0x0200  /* C: externally matched via
					 * XMATCH/XFAIL interface.
					 */
#define RX_INTEXT_ADDR_MATCH    0x0300  /* C: internally and externally
					 * matched.
					 */
#define RX_READY (RX_VALID | RX_FRAME_IRQ) /* Ready for receive. */

/* Constants for Command Status Interrupt.
 * COMMAND_REJECT status field bit functions (SSB.Parm[0])
 */
#define ILLEGAL_COMMAND		0x0080	/* Set if an unknown command
					 * is issued to the adapter
					 */
#define ADDRESS_ERROR		0x0040  /* Set if any address field in
					 * the SCB is odd. (not word aligned)
					 */
#define ADAPTER_OPEN		0x0020  /* Command issued illegal with
					 * open adapter.
					 */
#define ADAPTER_CLOSE		0x0010  /* Command issued illegal with
					 * closed adapter.
					 */
#define SAME_COMMAND		0x0008  /* Command issued with same command
					 * already executing.
					 */

/* OPEN_COMPLETION values (SSB.Parm[0], MSB) */
#define NODE_ADDR_ERROR		0x0040  /* Wrong address or BIA read
					 * zero address.
					 */
#define LIST_SIZE_ERROR		0x0020  /* If List_Size value not in 0,
					 * 14, 20, 26.
					 */
#define BUF_SIZE_ERROR		0x0010  /* Not enough available memory for
					 * two buffers.
					 */
#define TX_BUF_COUNT_ERROR	0x0004  /* Remaining receive buffers less than
					 * two.
					 */
#define OPEN_ERROR		0x0002	/* Error during ring insertion; more
					 * information in bits 8-15.
					 */

/* Standard return codes */
#define GOOD_COMPLETION		0x0080  /* =OPEN_SUCCESSFULL */
#define INVALID_OPEN_OPTION	0x0001  /* OPEN options are not supported by
					 * the adapter.
					 */

/* OPEN phases; details of OPEN_ERROR (SSB.Parm[0], LSB)            */
#define OPEN_PHASES_MASK            0xF000  /* Check only the bits 8-11. */
#define LOBE_MEDIA_TEST             0x1000
#define PHYSICAL_INSERTION          0x2000
#define ADDRESS_VERIFICATION        0x3000
#define PARTICIPATION_IN_RING_POLL  0x4000
#define REQUEST_INITIALISATION      0x5000
#define FULLDUPLEX_CHECK            0x6000

/* OPEN error codes; details of OPEN_ERROR (SSB.Parm[0], LSB) */
#define OPEN_ERROR_CODES_MASK	0x0F00  /* Check only the bits 12-15. */
#define OPEN_FUNCTION_FAILURE   0x0100  /* Unable to transmit to itself or
					 * frames received before insertion.
					 */
#define OPEN_SIGNAL_LOSS	0x0200	/* Signal loss condition detected at
					 * receiver.
					 */
#define OPEN_TIMEOUT		0x0500	/* Insertion timer expired before
					 * logical insertion.
					 */
#define OPEN_RING_FAILURE	0x0600	/* Unable to receive own ring purge
					 * MAC frames.
					 */
#define OPEN_RING_BEACONING	0x0700	/* Beacon MAC frame received after
					 * ring insertion.
					 */
#define OPEN_DUPLICATE_NODEADDR	0x0800  /* Other station in ring found
					 * with the same address.
					 */
#define OPEN_REQUEST_INIT	0x0900	/* RPS present but does not respond. */
#define OPEN_REMOVE_RECEIVED    0x0A00  /* Adapter received a remove adapter
					 * MAC frame.
					 */
#define OPEN_FULLDUPLEX_SET	0x0D00	/* Got this with full duplex on when
					 * trying to connect to a normal ring.
					 */

/* SET_BRIDGE_PARMS return codes: */
#define BRIDGE_INVALID_MAX_LEN  0x4000  /* MAX_ROUTING_FIELD_LENGTH odd,
					 * less than 6 or > 30.
					 */
#define BRIDGE_INVALID_SRC_RING 0x2000  /* SOURCE_RING number zero, too large
					 * or = TARGET_RING.
					 */
#define BRIDGE_INVALID_TRG_RING 0x1000  /* TARGET_RING number zero, too large
					 * or = SOURCE_RING.
					 */
#define BRIDGE_INVALID_BRDGE_NO 0x0800  /* BRIDGE_NUMBER too large. */
#define BRIDGE_INVALID_OPTIONS  0x0400  /* Invalid bridge options. */
#define BRIDGE_DIAGS_FAILED     0x0200  /* Diagnostics of TMS380SRA failed. */
#define BRIDGE_NO_SRA           0x0100  /* The TMS380SRA does not exist in HW
					 * configuration.
					 */

/*
 * Bring Up Diagnostics error codes.
 */
#define BUD_INITIAL_ERROR       0x0
#define BUD_CHECKSUM_ERROR      0x1
#define BUD_ADAPTER_RAM_ERROR   0x2
#define BUD_INSTRUCTION_ERROR   0x3
#define BUD_CONTEXT_ERROR       0x4
#define BUD_PROTOCOL_ERROR      0x5
#define BUD_INTERFACE_ERROR	0x6

/* BUD constants */
#define BUD_MAX_RETRIES         3
#define BUD_MAX_LOOPCNT         6
#define BUD_TIMEOUT             3000

/* Initialization constants */
#define INIT_MAX_RETRIES        3	/* Maximum three retries. */
#define INIT_MAX_LOOPCNT        22      /* Maximum loop counts. */

/* RING STATUS field values (high/low) */
#define SIGNAL_LOSS             0x0080  /* Loss of signal on the ring
					 * detected.
					 */
#define HARD_ERROR              0x0040  /* Transmitting or receiving beacon
					 * frames.
					 */
#define SOFT_ERROR              0x0020  /* Report error MAC frame
					 * transmitted.
					 */
#define TRANSMIT_BEACON         0x0010  /* Transmitting beacon frames on the
					 * ring.
					 */
#define LOBE_WIRE_FAULT         0x0008  /* Open or short circuit in the
					 * cable to concentrator; adapter
					 * closed.
					 */
#define AUTO_REMOVAL_ERROR      0x0004  /* Lobe wrap test failed, deinserted;
					 * adapter closed.
					 */
#define REMOVE_RECEIVED         0x0001  /* Received a remove ring station MAC
					 * MAC frame request; adapter closed.
					 */
#define COUNTER_OVERFLOW        0x8000  /* Overflow of one of the adapters
					 * error counters; READ.ERROR.LOG.
					 */
#define SINGLE_STATION          0x4000  /* Adapter is the only station on the
					 * ring.
					 */
#define RING_RECOVERY           0x2000  /* Claim token MAC frames on the ring;
					 * reset after ring purge frame.
					 */

#define ADAPTER_CLOSED (LOBE_WIRE_FAULT | AUTO_REMOVAL_ERROR |\
                        REMOVE_RECEIVED)

/* Adapter_check_block.Status field bit assignments: */
#define DIO_PARITY              0x8000  /* Adapter detects bad parity
					 * through direct I/O access.
					 */
#define DMA_READ_ABORT          0x4000  /* Aborting DMA read operation
					 * from system Parm[0]: 0=timeout,
					 * 1=parity error, 2=bus error;
					 * Parm[1]: 32 bit pointer to host
					 * system address at failure.
					 */
#define DMA_WRITE_ABORT         0x2000  /* Aborting DMA write operation
					 * to system. (parameters analogous to
					 * DMA_READ_ABORT)
					 */
#define ILLEGAL_OP_CODE         0x1000  /* Illegal operation code in the
					 * the adapters firmware Parm[0]-2:
					 * communications processor registers
					 * R13-R15.
					 */
#define PARITY_ERRORS           0x0800  /* Adapter detects internal bus
					 * parity error.
					 */
#define RAM_DATA_ERROR          0x0080  /* Valid only during RAM testing;
					 * RAM data error Parm[0-1]: 32 bit
					 * pointer to RAM location.
					 */
#define RAM_PARITY_ERROR        0x0040  /* Valid only during RAM testing;
					 * RAM parity error Parm[0-1]: 32 bit
					 * pointer to RAM location.
					 */
#define RING_UNDERRUN           0x0020  /* Internal DMA underrun when
					 * transmitting onto ring.
					 */
#define INVALID_IRQ             0x0008  /* Unrecognized interrupt generated
					 * internal to adapter Parm[0-2]:
					 * adapter register R13-R15.
					 */
#define INVALID_ERROR_IRQ       0x0004  /* Unrecognized error interrupt
					 * generated Parm[0-2]: adapter register
					 * R13-R15.
					 */
#define INVALID_XOP             0x0002  /* Unrecognized XOP request in
					 * communication processor Parm[0-2]:
					 * adapter register R13-R15.
					 */
#define CHECKADDR               0x05E0  /* Adapter check status information
					 * address offset.
					 */
#define ROM_PAGE_0              0x0000  /* Adapter ROM page 0. */

/*
 * RECEIVE.STATUS interrupt result SSB values: (high-low)
 * (RECEIVE_COMPLETE field bit definitions in SSB.Parm[0])
 */
#define RX_COMPLETE             0x0080  /* SSB.Parm[0]; SSB.Parm[1]: 32
					 * bit pointer to last RPL.
					 */
#define RX_SUSPENDED            0x0040  /* SSB.Parm[0]; SSB.Parm[1]: 32
					 * bit pointer to RPL with odd
					 * forward pointer.
					 */

/* Valid receive CSTAT: */
#define RX_FRAME_CONTROL_BITS (RX_VALID | RX_START_FRAME | RX_END_FRAME | \
			       RX_FRAME_COMPLETE)
#define VALID_SINGLE_BUFFER_FRAME (RX_START_FRAME | RX_END_FRAME | \
				   RX_FRAME_COMPLETE)

typedef enum SKB_STAT SKB_STAT;
enum SKB_STAT {
	SKB_UNAVAILABLE,
	SKB_DMA_DIRECT,
	SKB_DATA_COPY
};

/* Receive Parameter List (RPL) The length of the RPLs has to be initialized 
 * in the OPL. (OPEN parameter list)
 */
#define RPL_NUM		3

#define RX_FRAG_NUM     1	/* Maximal number of used fragments in one RPL.
				 * (up to firmware v2.24: 3, now: up to 9)
				 */

#pragma pack(1)
typedef struct s_RPL RPL;
struct s_RPL {	/* Receive Parameter List */
	u_int32_t NextRPLAddr;		/* Pointer to next RPL in chain
					 * (normalized = physical 32 bit
					 * address) if pointer is odd: this
					 * is last RPL. Pointing to itself can
					 * cause problems!
					 */
	volatile u_int16_t Status;	/* Set by creation of Receive Parameter
					 * List RECEIVE_CSTAT_COMPLETE set by
					 * adapter in lists that start or end
					 * a frame.
					 */
	volatile u_int16_t FrameSize;	 /* Number of bytes received as a
					 * frame including AC/FC, Destination,
					 * Source, Routing field not including 
					 * CRC, FS (Frame Status), End Delimiter
					 * (valid only if START_FRAME bit in 
					 * CSTAT nonzero) must not be zero in
					 * any list; maximum value: (BUFFER_SIZE
					 * - 8) * TX_BUF_MAX sum of DataCount
					 * values in FragmentList must equal
					 * Frame_Size value in START_FRAME TPL!
					 * frame data fragment list
					 */

	/* TPL/RPL size in OPEN parameter list depending on maximal numbers
	 * of fragments used in one parameter list.
	 */
	Fragment FragList[RX_FRAG_NUM];	/* Maximum: nine frame fragments in
					 * one TPL. Actual version of firmware:
					 * 9 fragments possible.
					 */
#pragma pack()

	/* Special proprietary data and precalculations. */
	RPL *NextRPLPtr;	/* Logical pointer to next RPL in chain. */
	unsigned char *MData;
	struct sk_buff *Skb;
	SKB_STAT SkbStat;
	int RPLIndex;
	dma_addr_t DMABuff;		/* DMA IO bus address from pci_map */
};

/* Information that need to be kept for each board. */
typedef struct net_local {
#pragma pack(1)
	IPB ipb;	/* Initialization Parameter Block. */
	SCB scb;	/* System Command Block: system to adapter 
			 * communication.
			 */
	SSB ssb;	/* System Status Block: adapter to system 
			 * communication.
			 */
	OPB ocpl;	/* Open Options Parameter Block. */

	ERRORTAB errorlogtable;	/* Adapter statistic error counters.
				 * (read from adapter memory)
				 */
	unsigned char ProductID[PROD_ID_SIZE + 1]; /* Product ID */
#pragma pack()

	TPL Tpl[TPL_NUM];
	TPL *TplFree;
	TPL *TplBusy;
	unsigned char LocalTxBuffers[TPL_NUM][DEFAULT_PACKET_SIZE];

	RPL Rpl[RPL_NUM];
	RPL *RplHead;
	RPL *RplTail;
	unsigned char LocalRxBuffers[RPL_NUM][DEFAULT_PACKET_SIZE];

	struct pci_dev *pdev;
	int DataRate;
	unsigned char ScbInUse;
	unsigned short CMDqueue;

	unsigned long AdapterOpenFlag:1;
	unsigned long AdapterVirtOpenFlag:1;
	unsigned long OpenCommandIssued:1;
	unsigned long TransmitCommandActive:1;
	unsigned long TransmitHaltScheduled:1;
	unsigned long HaltInProgress:1;
	unsigned long LobeWireFaultLogged:1;
	unsigned long ReOpenInProgress:1;
	unsigned long Sleeping:1;

	unsigned long LastOpenStatus;
	unsigned short CurrentRingStatus;
	unsigned long MaxPacketSize;
	
	unsigned long StartTime;
	unsigned long LastSendTime;

	struct tr_statistics MacStat;	/* MAC statistics structure */

	unsigned long dmalimit; /* the max DMA address (ie, ISA) */
	dma_addr_t    dmabuffer; /* the DMA bus address corresponding to
				    priv. Might be different from virt_to_bus()
				    for architectures with IO MMU (Alpha) */

	struct timer_list timer;

	wait_queue_head_t  wait_for_tok_int;

	INTPTRS intptrs;	/* Internal adapter pointer. Must be read
				 * before OPEN command.
				 */
	unsigned short (*setnselout)(struct net_device *);
	unsigned short (*sifreadb)(struct net_device *, unsigned short);
	void (*sifwriteb)(struct net_device *, unsigned short, unsigned short);
	unsigned short (*sifreadw)(struct net_device *, unsigned short);
	void (*sifwritew)(struct net_device *, unsigned short, unsigned short);

	spinlock_t lock;                /* SMP protection */
	void *tmspriv;
} NET_LOCAL;

#endif	/* __KERNEL__ */
#endif	/* __LINUX_TMS380TR_H */
