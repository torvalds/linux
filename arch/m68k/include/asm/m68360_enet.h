/***********************************
 * $Id: m68360_enet.h,v 1.1 2002/03/02 15:01:07 gerg Exp $
 ***********************************
 *
 ***************************************
 * Definitions for the ETHERNET controllers
 ***************************************
 */

#ifndef __ETHER_H
#define __ETHER_H

#include <asm/quicc_simple.h>

/*
 * transmit BD's
 */
#define T_R     0x8000          /* ready bit */
#define E_T_PAD 0x4000          /* short frame padding */
#define T_W     0x2000          /* wrap bit */
#define T_I     0x1000          /* interrupt on completion */
#define T_L     0x0800          /* last in frame */
#define T_TC    0x0400          /* transmit CRC (when last) */

#define T_DEF   0x0200          /* defer indication */
#define T_HB    0x0100          /* heartbeat */
#define T_LC    0x0080          /* error: late collision */
#define T_RL    0x0040          /* error: retransmission limit */
#define T_RC    0x003c          /* retry count */
#define T_UN    0x0002          /* error: underrun */
#define T_CSL   0x0001          /* carier sense lost */
#define T_ERROR (T_HB | T_LC | T_RL | T_UN | T_CSL)

/*
 * receive BD's
 */
#define R_E     0x8000          /* buffer empty */
#define R_W     0x2000          /* wrap bit */
#define R_I     0x1000          /* interrupt on reception */
#define R_L     0x0800          /* last BD in frame */
#define R_F     0x0400          /* first BD in frame */
#define R_M     0x0100          /* received because of promisc. mode */

#define R_LG    0x0020          /* frame too long */
#define R_NO    0x0010          /* non-octet aligned */
#define R_SH    0x0008          /* short frame */
#define R_CR    0x0004          /* receive CRC error */
#define R_OV    0x0002          /* receive overrun */
#define R_CL    0x0001          /* collision */
#define ETHER_R_ERROR (R_LG | R_NO | R_SH | R_CR | R_OV | R_CL)


/*
 * ethernet interrupts
 */
#define ETHERNET_GRA    0x0080  /* graceful stop complete */
#define ETHERNET_TXE    0x0010  /* transmit error         */
#define ETHERNET_RXF    0x0008  /* receive frame          */
#define ETHERNET_BSY    0x0004  /* busy condition         */
#define ETHERNET_TXB    0x0002  /* transmit buffer        */
#define ETHERNET_RXB    0x0001  /* receive buffer         */

/*
 * ethernet protocol specific mode register (PSMR)
 */
#define ETHER_HBC       0x8000    /* heartbeat checking      */
#define ETHER_FC        0x4000    /* force collision         */
#define ETHER_RSH       0x2000    /* receive short frames    */
#define ETHER_IAM       0x1000    /* individual address mode */
#define ETHER_CRC_32    (0x2<<10) /* Enable CRC              */
#define ETHER_PRO       0x0200    /* promiscuous             */
#define ETHER_BRO       0x0100    /* broadcast address       */
#define ETHER_SBT       0x0080    /* stop backoff timer      */
#define ETHER_LPB       0x0040    /* Loop Back Mode          */
#define ETHER_SIP       0x0020    /* sample input pins       */
#define ETHER_LCW       0x0010    /* late collision window   */
#define ETHER_NIB_13    (0x0<<1)  /* # of ignored bits 13    */
#define ETHER_NIB_14    (0x1<<1)  /* # of ignored bits 14    */
#define ETHER_NIB_15    (0x2<<1)  /* # of ignored bits 15    */
#define ETHER_NIB_16    (0x3<<1)  /* # of ignored bits 16    */
#define ETHER_NIB_21    (0x4<<1)  /* # of ignored bits 21    */
#define ETHER_NIB_22    (0x5<<1)  /* # of ignored bits 22    */
#define ETHER_NIB_23    (0x6<<1)  /* # of ignored bits 23    */
#define ETHER_NIB_24    (0x7<<1)  /* # of ignored bits 24    */

/*
 * ethernet specific parameters
 */
#define CRC_WORD 4          /* Length in bytes of CRC */               
#define C_PRES   0xffffffff /* preform 32 bit CRC */
#define C_MASK   0xdebb20e3 /* comply with 32 bit CRC */       
#define CRCEC    0x00000000
#define ALEC     0x00000000
#define DISFC    0x00000000
#define PADS     0x00000000
#define RET_LIM  0x000f     /* retry 15 times to send a frame before interrupt */
#define ETH_MFLR 0x05ee     /* 1518 max frame size */
#define MINFLR   0x0040     /* Minimum frame size 64 */
#define MAXD1    0x05ee     /* Max dma count 1518 */
#define MAXD2    0x05ee
#define GADDR1   0x00000000 /* Clear group address */  
#define GADDR2   0x00000000
#define GADDR3   0x00000000    
#define GADDR4   0x00000000    
#define P_PER    0x00000000 /*not used */              
#define IADDR1   0x00000000 /* Individual hash table not used */       
#define IADDR2   0x00000000
#define IADDR3   0x00000000    
#define IADDR4   0x00000000            
#define TADDR_H  0x00000000 /* clear this regs */              
#define TADDR_M  0x00000000            
#define TADDR_L  0x00000000            

/*       SCC Parameter Ram */
#define RFCR    0x18 /* normal operation */
#define TFCR    0x18 /* normal operation */
#define E_MRBLR 1518 /* Max ethernet frame length */

/*
 * ethernet specific structure
 */
typedef union {
        unsigned char b[6];
        struct {
            unsigned short high;
            unsigned short middl;
            unsigned short low;
        } w;
} ETHER_ADDR;

typedef struct {
    int        max_frame_length;
    int        promisc_mode;
    int        reject_broadcast;
    ETHER_ADDR phys_adr;
} ETHER_SPECIFIC;

typedef struct {
    ETHER_ADDR     dst_addr;
    ETHER_ADDR     src_addr;
    unsigned short type_or_len;
    unsigned char  data[1];
} ETHER_FRAME;

#define MAX_DATALEN 1500
typedef struct {
    ETHER_ADDR     dst_addr;
    ETHER_ADDR     src_addr;
    unsigned short type_or_len;
    unsigned char  data[MAX_DATALEN];
    unsigned char  fcs[CRC_WORD];
} ETHER_MAX_FRAME;


/*
 * Internal ethernet function prototypes
 */
void        ether_interrupt(int scc_num);
/* mleslie: debug */
/* static void ethernet_rx_internal(int scc_num); */
/* static void ethernet_tx_internal(int scc_num); */

/*
 * User callable routines prototypes (ethernet specific)
 */
void ethernet_init(int                       scc_number,
                   alloc_routine             *alloc_buffer,
                   free_routine              *free_buffer,
                   store_rx_buffer_routine   *store_rx_buffer,
                   handle_tx_error_routine   *handle_tx_error,
                   handle_rx_error_routine   *handle_rx_error,
                   handle_lost_error_routine *handle_lost_error,
                   ETHER_SPECIFIC            *ether_spec);
int  ethernet_tx(int scc_number, void *buf, int length);

#endif

