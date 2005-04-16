/******************************************************************************
             Device driver for Interphase ATM PCI adapter cards 
                    Author: Peter Wang  <pwang@iphase.com>            
                   Interphase Corporation  <www.iphase.com>           
                               Version: 1.0   
               iphase.h:  This is the header file for iphase.c. 
*******************************************************************************
      
      This software may be used and distributed according to the terms
      of the GNU General Public License (GPL), incorporated herein by reference.
      Drivers based on this skeleton fall under the GPL and must retain
      the authorship (implicit copyright) notice.

      This program is distributed in the hope that it will be useful, but
      WITHOUT ANY WARRANTY; without even the implied warranty of
      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
      General Public License for more details.
      
      Modified from an incomplete driver for Interphase 5575 1KVC 1M card which 
      was originally written by Monalisa Agrawal at UNH. Now this driver 
      supports a variety of varients of Interphase ATM PCI (i)Chip adapter 
      card family (See www.iphase.com/products/ClassSheet.cfm?ClassID=ATM) 
      in terms of PHY type, the size of control memory and the size of 
      packet memory. The followings are the change log and history:
     
          Bugfix the Mona's UBR driver.
          Modify the basic memory allocation and dma logic.
          Port the driver to the latest kernel from 2.0.46.
          Complete the ABR logic of the driver, and added the ABR work-
              around for the hardware anormalies.
          Add the CBR support.
	  Add the flow control logic to the driver to allow rate-limit VC.
          Add 4K VC support to the board with 512K control memory.
          Add the support of all the variants of the Interphase ATM PCI 
          (i)Chip adapter cards including x575 (155M OC3 and UTP155), x525
          (25M UTP25) and x531 (DS3 and E3).
          Add SMP support.

      Support and updates available at: ftp://ftp.iphase.com/pub/atm

*******************************************************************************/
  
#ifndef IPHASE_H  
#define IPHASE_H  

#include <linux/config.h>

/************************ IADBG DEFINE *********************************/
/* IADebugFlag Bit Map */ 
#define IF_IADBG_INIT_ADAPTER   0x00000001        // init adapter info
#define IF_IADBG_TX             0x00000002        // debug TX
#define IF_IADBG_RX             0x00000004        // debug RX
#define IF_IADBG_QUERY_INFO     0x00000008        // debug Request call
#define IF_IADBG_SHUTDOWN       0x00000010        // debug shutdown event
#define IF_IADBG_INTR           0x00000020        // debug interrupt DPC
#define IF_IADBG_TXPKT          0x00000040  	  // debug TX PKT
#define IF_IADBG_RXPKT          0x00000080  	  // debug RX PKT
#define IF_IADBG_ERR            0x00000100        // debug system error
#define IF_IADBG_EVENT          0x00000200        // debug event
#define IF_IADBG_DIS_INTR       0x00001000        // debug disable interrupt
#define IF_IADBG_EN_INTR        0x00002000        // debug enable interrupt
#define IF_IADBG_LOUD           0x00004000        // debugging info
#define IF_IADBG_VERY_LOUD      0x00008000        // excessive debugging info
#define IF_IADBG_CBR            0x00100000  	  //
#define IF_IADBG_UBR            0x00200000  	  //
#define IF_IADBG_ABR            0x00400000        //
#define IF_IADBG_DESC           0x01000000        //
#define IF_IADBG_SUNI_STAT      0x02000000        // suni statistics
#define IF_IADBG_RESET          0x04000000        

#define IF_IADBG(f) if (IADebugFlag & (f))

#ifdef  CONFIG_ATM_IA_DEBUG   /* Debug build */

#define IF_LOUD(A) IF_IADBG(IF_IADBG_LOUD) { A }
#define IF_ERR(A) IF_IADBG(IF_IADBG_ERR) { A }
#define IF_VERY_LOUD(A) IF_IADBG( IF_IADBG_VERY_LOUD ) { A }

#define IF_INIT_ADAPTER(A) IF_IADBG( IF_IADBG_INIT_ADAPTER ) { A }
#define IF_INIT(A) IF_IADBG( IF_IADBG_INIT_ADAPTER ) { A }
#define IF_SUNI_STAT(A) IF_IADBG( IF_IADBG_SUNI_STAT ) { A }
#define IF_QUERY_INFO(A) IF_IADBG( IF_IADBG_QUERY_INFO ) { A }
#define IF_COPY_OVER(A) IF_IADBG( IF_IADBG_COPY_OVER ) { A }

#define IF_INTR(A) IF_IADBG( IF_IADBG_INTR ) { A }
#define IF_DIS_INTR(A) IF_IADBG( IF_IADBG_DIS_INTR ) { A }
#define IF_EN_INTR(A) IF_IADBG( IF_IADBG_EN_INTR ) { A }

#define IF_TX(A) IF_IADBG( IF_IADBG_TX ) { A }
#define IF_RX(A) IF_IADBG( IF_IADBG_RX ) { A }
#define IF_TXPKT(A) IF_IADBG( IF_IADBG_TXPKT ) { A }
#define IF_RXPKT(A) IF_IADBG( IF_IADBG_RXPKT ) { A }

#define IF_SHUTDOWN(A) IF_IADBG(IF_IADBG_SHUTDOWN) { A }
#define IF_CBR(A) IF_IADBG( IF_IADBG_CBR ) { A }
#define IF_UBR(A) IF_IADBG( IF_IADBG_UBR ) { A }
#define IF_ABR(A) IF_IADBG( IF_IADBG_ABR ) { A }
#define IF_EVENT(A) IF_IADBG( IF_IADBG_EVENT) { A }

#else /* free build */
#define IF_LOUD(A)
#define IF_VERY_LOUD(A)
#define IF_INIT_ADAPTER(A)
#define IF_INIT(A)
#define IF_SUNI_STAT(A)
#define IF_PVC_CHKPKT(A)
#define IF_QUERY_INFO(A)
#define IF_COPY_OVER(A)
#define IF_HANG(A)
#define IF_INTR(A)
#define IF_DIS_INTR(A)
#define IF_EN_INTR(A)
#define IF_TX(A)
#define IF_RX(A)
#define IF_TXDEBUG(A)
#define IF_VC(A)
#define IF_ERR(A) 
#define IF_CBR(A)
#define IF_UBR(A)
#define IF_ABR(A)
#define IF_SHUTDOWN(A)
#define DbgPrint(A)
#define IF_EVENT(A)
#define IF_TXPKT(A) 
#define IF_RXPKT(A)
#endif /* CONFIG_ATM_IA_DEBUG */ 

#define isprint(a) ((a >=' ')&&(a <= '~'))  
#define ATM_DESC(skb) (skb->protocol)
#define IA_SKB_STATE(skb) (skb->protocol)
#define IA_DLED   1
#define IA_TX_DONE 2

/* iadbg defines */
#define IA_CMD   0x7749
typedef struct {
	int cmd;
        int sub_cmd;
        int len;
        u32 maddr;
        int status;
        void __user *buf;
} IA_CMDBUF, *PIA_CMDBUF;

/* cmds */
#define MEMDUMP     		0x01

/* sub_cmds */
#define MEMDUMP_SEGREG          0x2
#define MEMDUMP_DEV  		0x1
#define MEMDUMP_REASSREG        0x3
#define MEMDUMP_FFL             0x4
#define READ_REG                0x5
#define WAKE_DBG_WAIT           0x6

/************************ IADBG DEFINE END ***************************/

#define Boolean(x)    	((x) ? 1 : 0)
#define NR_VCI 1024		/* number of VCIs */  
#define NR_VCI_LD 10		/* log2(NR_VCI) */  
#define NR_VCI_4K 4096 		/* number of VCIs */  
#define NR_VCI_4K_LD 12		/* log2(NR_VCI) */  
#define MEM_VALID 0xfffffff0	/* mask base address with this */  
  
#ifndef PCI_VENDOR_ID_IPHASE  
#define PCI_VENDOR_ID_IPHASE 0x107e  
#endif  
#ifndef PCI_DEVICE_ID_IPHASE_5575  
#define PCI_DEVICE_ID_IPHASE_5575 0x0008  
#endif  
#define DEV_LABEL 	"ia"  
#define PCR	207692  
#define ICR	100000  
#define MCR	0  
#define TBE	1000  
#define FRTT	1  
#define RIF	2		  
#define RDF	4  
#define NRMCODE 5	/* 0 - 7 */  
#define TRMCODE	3	/* 0 - 7 */  
#define CDFCODE	6  
#define ATDFCODE 2	/* 0 - 15 */  
  
/*---------------------- Packet/Cell Memory ------------------------*/  
#define TX_PACKET_RAM 	0x00000 /* start of Trasnmit Packet memory - 0 */  
#define DFL_TX_BUF_SZ	10240	/* 10 K buffers */  
#define DFL_TX_BUFFERS     50 	/* number of packet buffers for Tx   
					- descriptor 0 unused */  
#define REASS_RAM_SIZE 0x10000  /* for 64K 1K VC board */  
#define RX_PACKET_RAM 	0x80000 /* start of Receive Packet memory - 512K */  
#define DFL_RX_BUF_SZ	10240	/* 10k buffers */  
#define DFL_RX_BUFFERS      50	/* number of packet buffers for Rx   
					- descriptor 0 unused */  
  
struct cpcs_trailer 
{  
	u_short control;  
	u_short length;  
	u_int	crc32;  
};  

struct cpcs_trailer_desc
{
	struct cpcs_trailer *cpcs;
	dma_addr_t dma_addr;
};

struct ia_vcc 
{ 
	int rxing;	 
	int txing;		 
        int NumCbrEntry;
        u32 pcr;
        u32 saved_tx_quota;
        int flow_inc;
        struct sk_buff_head txing_skb; 
        int  ltimeout;                  
        u8  vc_desc_cnt;                
                
};  
  
struct abr_vc_table 
{  
	u_char status;  
	u_char rdf;  
	u_short air;  
	u_int res[3];  
	u_int req_rm_cell_data1;  
	u_int req_rm_cell_data2;  
	u_int add_rm_cell_data1;  
	u_int add_rm_cell_data2;  
};  
    
/* 32 byte entries */  
struct main_vc 
{  
	u_short 	type;  
#define ABR	0x8000  
#define UBR 	0xc000  
#define CBR	0x0000  
	/* ABR fields */  
	u_short 	nrm;	 
 	u_short 	trm;	   
	u_short 	rm_timestamp_hi;  
	u_short 	rm_timestamp_lo:8,  
			crm:8;		  
	u_short 	remainder; 	/* ABR and UBR fields - last 10 bits*/  
	u_short 	next_vc_sched;  
	u_short 	present_desc;	/* all classes */  
	u_short 	last_cell_slot;	/* ABR and UBR */  
	u_short 	pcr;  
	u_short 	fraction;  
	u_short 	icr;  
	u_short 	atdf;  
	u_short 	mcr;  
	u_short 	acr;		 
	u_short 	unack:8,  
			status:8;	/* all classes */  
#define UIOLI 0x80  
#define CRC_APPEND 0x40			/* for status field - CRC-32 append */  
#define ABR_STATE 0x02  
  
};  
  
  
/* 8 byte entries */  
struct ext_vc 
{  
	u_short 	atm_hdr1;  
	u_short 	atm_hdr2;  
	u_short 	last_desc;  
      	u_short 	out_of_rate_link;   /* reserved for UBR and CBR */  
};  
  
  
#define DLE_ENTRIES 256  
#define DMA_INT_ENABLE 0x0002	/* use for both Tx and Rx */  
#define TX_DLE_PSI 0x0001  
#define DLE_TOTAL_SIZE (sizeof(struct dle)*DLE_ENTRIES)
  
/* Descriptor List Entries (DLE) */  
struct dle 
{  
	u32 	sys_pkt_addr;  
	u32 	local_pkt_addr;  
	u32 	bytes;  
	u16 	prq_wr_ptr_data;  
	u16 	mode;  
};  
  
struct dle_q 
{  
	struct dle 	*start;  
	struct dle 	*end;  
	struct dle 	*read;  
	struct dle 	*write;  
};  
  
struct free_desc_q 
{  
	int 	desc;	/* Descriptor number */  
	struct free_desc_q *next;  
};  
  
struct tx_buf_desc {  
	unsigned short desc_mode;  
	unsigned short vc_index;  
	unsigned short res1;		/* reserved field */  
	unsigned short bytes;  
	unsigned short buf_start_hi;  
	unsigned short buf_start_lo;  
	unsigned short res2[10];	/* reserved field */  
};  
	  
  
struct rx_buf_desc { 
	unsigned short desc_mode;
	unsigned short vc_index;
	unsigned short vpi; 
	unsigned short bytes; 
	unsigned short buf_start_hi;  
	unsigned short buf_start_lo;  
	unsigned short dma_start_hi;  
	unsigned short dma_start_lo;  
	unsigned short crc_upper;  
	unsigned short crc_lower;  
	unsigned short res:8, timeout:8;  
	unsigned short res2[5];	/* reserved field */  
};  
  
/*--------SAR stuff ---------------------*/  
  
#define EPROM_SIZE 0x40000	/* says 64K in the docs ??? */  
#define MAC1_LEN	4	   					  
#define MAC2_LEN	2  
   
/*------------ PCI Memory Space Map, 128K SAR memory ----------------*/  
#define IPHASE5575_PCI_CONFIG_REG_BASE	0x0000  
#define IPHASE5575_BUS_CONTROL_REG_BASE 0x1000	/* offsets 0x00 - 0x3c */  
#define IPHASE5575_FRAG_CONTROL_REG_BASE 0x2000  
#define IPHASE5575_REASS_CONTROL_REG_BASE 0x3000  
#define IPHASE5575_DMA_CONTROL_REG_BASE	0x4000  
#define IPHASE5575_FRONT_END_REG_BASE IPHASE5575_DMA_CONTROL_REG_BASE  
#define IPHASE5575_FRAG_CONTROL_RAM_BASE 0x10000  
#define IPHASE5575_REASS_CONTROL_RAM_BASE 0x20000  
  
/*------------ Bus interface control registers -----------------*/  
#define IPHASE5575_BUS_CONTROL_REG	0x00  
#define IPHASE5575_BUS_STATUS_REG	0x01	/* actual offset 0x04 */  
#define IPHASE5575_MAC1			0x02  
#define IPHASE5575_REV			0x03  
#define IPHASE5575_MAC2			0x03	/*actual offset 0x0e-reg 0x0c*/  
#define IPHASE5575_EXT_RESET		0x04  
#define IPHASE5575_INT_RESET		0x05	/* addr 1c ?? reg 0x06 */  
#define IPHASE5575_PCI_ADDR_PAGE	0x07	/* reg 0x08, 0x09 ?? */  
#define IPHASE5575_EEPROM_ACCESS	0x0a	/* actual offset 0x28 */  
#define IPHASE5575_CELL_FIFO_QUEUE_SZ	0x0b  
#define IPHASE5575_CELL_FIFO_MARK_STATE	0x0c  
#define IPHASE5575_CELL_FIFO_READ_PTR	0x0d  
#define IPHASE5575_CELL_FIFO_WRITE_PTR	0x0e  
#define IPHASE5575_CELL_FIFO_CELLS_AVL	0x0f	/* actual offset 0x3c */  
  
/* Bus Interface Control Register bits */  
#define CTRL_FE_RST	0x80000000  
#define CTRL_LED	0x40000000  
#define CTRL_25MBPHY	0x10000000  
#define CTRL_ENCMBMEM	0x08000000  
#define CTRL_ENOFFSEG	0x01000000  
#define CTRL_ERRMASK	0x00400000  
#define CTRL_DLETMASK	0x00100000  
#define CTRL_DLERMASK	0x00080000  
#define CTRL_FEMASK	0x00040000  
#define CTRL_SEGMASK	0x00020000  
#define CTRL_REASSMASK	0x00010000  
#define CTRL_CSPREEMPT	0x00002000  
#define CTRL_B128	0x00000200  
#define CTRL_B64	0x00000100  
#define CTRL_B48	0x00000080  
#define CTRL_B32	0x00000040  
#define CTRL_B16	0x00000020  
#define CTRL_B8		0x00000010  
  
/* Bus Interface Status Register bits */  
#define STAT_CMEMSIZ	0xc0000000  
#define STAT_ADPARCK	0x20000000  
#define STAT_RESVD	0x1fffff80  
#define STAT_ERRINT	0x00000040  
#define STAT_MARKINT	0x00000020  
#define STAT_DLETINT	0x00000010  
#define STAT_DLERINT	0x00000008  
#define STAT_FEINT	0x00000004  
#define STAT_SEGINT	0x00000002  
#define STAT_REASSINT	0x00000001  
  
  
/*--------------- Segmentation control registers -----------------*/  
/* The segmentation registers are 16 bits access and the addresses  
	are defined as such so the addresses are the actual "offsets" */  
#define IDLEHEADHI	0x00  
#define IDLEHEADLO	0x01  
#define MAXRATE		0x02  
/* Values for MAXRATE register for 155Mbps and 25.6 Mbps operation */  
#define RATE155		0x64b1 // 16 bits float format 
#define MAX_ATM_155     352768 // Cells/second p.118
#define RATE25		0x5f9d  
  
#define STPARMS		0x03  
#define STPARMS_1K	0x008c  
#define STPARMS_2K	0x0049  
#define STPARMS_4K	0x0026  
#define COMP_EN		0x4000  
#define CBR_EN		0x2000  
#define ABR_EN		0x0800  
#define UBR_EN		0x0400  
  
#define ABRUBR_ARB	0x04  
#define RM_TYPE		0x05  
/*Value for RM_TYPE register for ATM Forum Traffic Mangement4.0 support*/  
#define RM_TYPE_4_0	0x0100  
  
#define SEG_COMMAND_REG		0x17  
/* Values for the command register */  
#define RESET_SEG 0x0055  
#define RESET_SEG_STATE	0x00aa  
#define RESET_TX_CELL_CTR 0x00cc  
  
#define CBR_PTR_BASE	0x20  
#define ABR_SBPTR_BASE	0x22  
#define UBR_SBPTR_BASE  0x23  
#define ABRWQ_BASE	0x26  
#define UBRWQ_BASE	0x27  
#define VCT_BASE	0x28  
#define VCTE_BASE	0x29  
#define CBR_TAB_BEG	0x2c  
#define CBR_TAB_END	0x2d  
#define PRQ_ST_ADR	0x30  
#define PRQ_ED_ADR	0x31  
#define PRQ_RD_PTR	0x32  
#define PRQ_WR_PTR	0x33  
#define TCQ_ST_ADR	0x34  
#define TCQ_ED_ADR 	0x35  
#define TCQ_RD_PTR	0x36  
#define TCQ_WR_PTR	0x37  
#define SEG_QUEUE_BASE	0x40  
#define SEG_DESC_BASE	0x41  
#define MODE_REG_0	0x45  
#define T_ONLINE	0x0002		/* (i)chipSAR is online */  
  
#define MODE_REG_1	0x46  
#define MODE_REG_1_VAL	0x0400		/*for propoer device operation*/  
  
#define SEG_INTR_STATUS_REG 0x47  
#define SEG_MASK_REG	0x48  
#define TRANSMIT_DONE 0x0200
#define TCQ_NOT_EMPTY 0x1000	/* this can be used for both the interrupt   
				status registers as well as the mask register */  
  
#define CELL_CTR_HIGH_AUTO 0x49  
#define CELL_CTR_HIGH_NOAUTO 0xc9  
#define CELL_CTR_LO_AUTO 0x4a  
#define CELL_CTR_LO_NOAUTO 0xca  
  
/* Diagnostic registers */  
#define NEXTDESC 	0x59  
#define NEXTVC		0x5a  
#define PSLOTCNT	0x5d  
#define NEWDN		0x6a  
#define NEWVC		0x6b  
#define SBPTR		0x6c  
#define ABRWQ_WRPTR	0x6f  
#define ABRWQ_RDPTR	0x70  
#define UBRWQ_WRPTR	0x71  
#define UBRWQ_RDPTR	0x72  
#define CBR_VC		0x73  
#define ABR_SBVC	0x75  
#define UBR_SBVC	0x76  
#define ABRNEXTLINK	0x78  
#define UBRNEXTLINK	0x79  
  
  
/*----------------- Reassembly control registers ---------------------*/  
/* The reassembly registers are 16 bits access and the addresses  
	are defined as such so the addresses are the actual "offsets" */  
#define MODE_REG	0x00  
#define R_ONLINE	0x0002		/* (i)chip is online */  
#define IGN_RAW_FL     	0x0004
  
#define PROTOCOL_ID	0x01  
#define REASS_MASK_REG	0x02  
#define REASS_INTR_STATUS_REG	0x03  
/* Interrupt Status register bits */  
#define RX_PKT_CTR_OF	0x8000  
#define RX_ERR_CTR_OF	0x4000  
#define RX_CELL_CTR_OF	0x1000  
#define RX_FREEQ_EMPT	0x0200  
#define RX_EXCPQ_FL	0x0080  
#define	RX_RAWQ_FL	0x0010  
#define RX_EXCP_RCVD	0x0008  
#define RX_PKT_RCVD	0x0004  
#define RX_RAW_RCVD	0x0001  
  
#define DRP_PKT_CNTR	0x04  
#define ERR_CNTR	0x05  
#define RAW_BASE_ADR	0x08  
#define CELL_CTR0	0x0c  
#define CELL_CTR1	0x0d  
#define REASS_COMMAND_REG	0x0f  
/* Values for command register */  
#define RESET_REASS	0x0055  
#define RESET_REASS_STATE 0x00aa  
#define RESET_DRP_PKT_CNTR 0x00f1  
#define RESET_ERR_CNTR	0x00f2  
#define RESET_CELL_CNTR 0x00f8  
#define RESET_REASS_ALL_REGS 0x00ff  
  
#define REASS_DESC_BASE	0x10  
#define VC_LKUP_BASE	0x11  
#define REASS_TABLE_BASE 0x12  
#define REASS_QUEUE_BASE 0x13  
#define PKT_TM_CNT	0x16  
#define TMOUT_RANGE	0x17  
#define INTRVL_CNTR	0x18  
#define TMOUT_INDX	0x19  
#define VP_LKUP_BASE	0x1c  
#define VP_FILTER	0x1d  
#define ABR_LKUP_BASE	0x1e  
#define FREEQ_ST_ADR	0x24  
#define FREEQ_ED_ADR	0x25  
#define FREEQ_RD_PTR	0x26  
#define FREEQ_WR_PTR	0x27  
#define PCQ_ST_ADR	0x28  
#define PCQ_ED_ADR	0x29  
#define PCQ_RD_PTR	0x2a  
#define PCQ_WR_PTR	0x2b  
#define EXCP_Q_ST_ADR	0x2c  
#define EXCP_Q_ED_ADR	0x2d  
#define EXCP_Q_RD_PTR	0x2e  
#define EXCP_Q_WR_PTR	0x2f  
#define CC_FIFO_ST_ADR	0x34  
#define CC_FIFO_ED_ADR	0x35  
#define CC_FIFO_RD_PTR	0x36  
#define CC_FIFO_WR_PTR	0x37  
#define STATE_REG	0x38  
#define BUF_SIZE	0x42  
#define XTRA_RM_OFFSET	0x44  
#define DRP_PKT_CNTR_NC	0x84  
#define ERR_CNTR_NC	0x85  
#define CELL_CNTR0_NC	0x8c  
#define CELL_CNTR1_NC	0x8d  
  
/* State Register bits */  
#define EXCPQ_EMPTY	0x0040  
#define PCQ_EMPTY	0x0010  
#define FREEQ_EMPTY	0x0004  
  
  
/*----------------- Front End registers/ DMA control --------------*/  
/* There is a lot of documentation error regarding these offsets ???   
	eg:- 2 offsets given 800, a00 for rx counter  
	similarly many others  
   Remember again that the offsets are to be 4*register number, so  
	correct the #defines here   
*/  
#define IPHASE5575_TX_COUNTER		0x200	/* offset - 0x800 */  
#define IPHASE5575_RX_COUNTER		0x280	/* offset - 0xa00 */  
#define IPHASE5575_TX_LIST_ADDR		0x300	/* offset - 0xc00 */  
#define IPHASE5575_RX_LIST_ADDR		0x380	/* offset - 0xe00 */  
  
/*--------------------------- RAM ---------------------------*/  
/* These memory maps are actually offsets from the segmentation and reassembly  RAM base addresses */  
  
/* Segmentation Control Memory map */  
#define TX_DESC_BASE	0x0000	/* Buffer Decriptor Table */  
#define TX_COMP_Q	0x1000	/* Transmit Complete Queue */  
#define PKT_RDY_Q	0x1400	/* Packet Ready Queue */  
#define CBR_SCHED_TABLE	0x1800	/* CBR Table */  
#define UBR_SCHED_TABLE	0x3000	/* UBR Table */  
#define UBR_WAIT_Q	0x4000	/* UBR Wait Queue */  
#define ABR_SCHED_TABLE	0x5000	/* ABR Table */  
#define ABR_WAIT_Q	0x5800	/* ABR Wait Queue */  
#define EXT_VC_TABLE	0x6000	/* Extended VC Table */  
#define MAIN_VC_TABLE	0x8000	/* Main VC Table */  
#define SCHEDSZ		1024	/* ABR and UBR Scheduling Table size */  
#define TX_DESC_TABLE_SZ 128	/* Number of entries in the Transmit   
					Buffer Descriptor Table */  
  
/* These are used as table offsets in Descriptor Table address generation */  
#define DESC_MODE	0x0  
#define VC_INDEX	0x1  
#define BYTE_CNT	0x3  
#define PKT_START_HI	0x4  
#define PKT_START_LO	0x5  
  
/* Descriptor Mode Word Bits */  
#define EOM_EN	0x0800  
#define AAL5	0x0100  
#define APP_CRC32 0x0400  
#define CMPL_INT  0x1000
  
#define TABLE_ADDRESS(db, dn, to) \
	(((unsigned long)(db & 0x04)) << 16) | (dn << 5) | (to << 1)  
  
/* Reassembly Control Memory Map */  
#define RX_DESC_BASE	0x0000	/* Buffer Descriptor Table */  
#define VP_TABLE	0x5c00	/* VP Table */  
#define EXCEPTION_Q	0x5e00	/* Exception Queue */  
#define FREE_BUF_DESC_Q	0x6000	/* Free Buffer Descriptor Queue */  
#define PKT_COMP_Q	0x6800	/* Packet Complete Queue */  
#define REASS_TABLE	0x7000	/* Reassembly Table */  
#define RX_VC_TABLE	0x7800	/* VC Table */  
#define ABR_VC_TABLE	0x8000	/* ABR VC Table */  
#define RX_DESC_TABLE_SZ 736	/* Number of entries in the Receive   
					Buffer Descriptor Table */  
#define VP_TABLE_SZ	256	 /* Number of entries in VPTable */   
#define RX_VC_TABLE_SZ 	1024	/* Number of entries in VC Table */   
#define REASS_TABLE_SZ 	1024	/* Number of entries in Reassembly Table */  
 /* Buffer Descriptor Table */  
#define RX_ACT	0x8000  
#define RX_VPVC	0x4000  
#define RX_CNG	0x0040  
#define RX_CER	0x0008  
#define RX_PTE	0x0004  
#define RX_OFL	0x0002  
#define NUM_RX_EXCP   32

/* Reassembly Table */  
#define NO_AAL5_PKT	0x0000  
#define AAL5_PKT_REASSEMBLED 0x4000  
#define AAL5_PKT_TERMINATED 0x8000  
#define RAW_PKT		0xc000  
#define REASS_ABR	0x2000  
  
/*-------------------- Base Registers --------------------*/  
#define REG_BASE IPHASE5575_BUS_CONTROL_REG_BASE  
#define RAM_BASE IPHASE5575_FRAG_CONTROL_RAM_BASE  
#define PHY_BASE IPHASE5575_FRONT_END_REG_BASE  
#define SEG_BASE IPHASE5575_FRAG_CONTROL_REG_BASE  
#define REASS_BASE IPHASE5575_REASS_CONTROL_REG_BASE  

typedef volatile u_int  freg_t;
typedef u_int   rreg_t;

typedef struct _ffredn_t {
        freg_t  idlehead_high;  /* Idle cell header (high)              */
        freg_t  idlehead_low;   /* Idle cell header (low)               */
        freg_t  maxrate;        /* Maximum rate                         */
        freg_t  stparms;        /* Traffic Management Parameters        */
        freg_t  abrubr_abr;     /* ABRUBR Priority Byte 1, TCR Byte 0   */
        freg_t  rm_type;        /*                                      */
        u_int   filler5[0x17 - 0x06];
        freg_t  cmd_reg;        /* Command register                     */
        u_int   filler18[0x20 - 0x18];
        freg_t  cbr_base;       /* CBR Pointer Base                     */
        freg_t  vbr_base;       /* VBR Pointer Base                     */
        freg_t  abr_base;       /* ABR Pointer Base                     */
        freg_t  ubr_base;       /* UBR Pointer Base                     */
        u_int   filler24;
        freg_t  vbrwq_base;     /* VBR Wait Queue Base                  */
        freg_t  abrwq_base;     /* ABR Wait Queue Base                  */
        freg_t  ubrwq_base;     /* UBR Wait Queue Base                  */
        freg_t  vct_base;       /* Main VC Table Base                   */
        freg_t  vcte_base;      /* Extended Main VC Table Base          */
        u_int   filler2a[0x2C - 0x2A];
        freg_t  cbr_tab_beg;    /* CBR Table Begin                      */
        freg_t  cbr_tab_end;    /* CBR Table End                        */
        freg_t  cbr_pointer;    /* CBR Pointer                          */
        u_int   filler2f[0x30 - 0x2F];
        freg_t  prq_st_adr;     /* Packet Ready Queue Start Address     */
        freg_t  prq_ed_adr;     /* Packet Ready Queue End Address       */
        freg_t  prq_rd_ptr;     /* Packet Ready Queue read pointer      */
        freg_t  prq_wr_ptr;     /* Packet Ready Queue write pointer     */
        freg_t  tcq_st_adr;     /* Transmit Complete Queue Start Address*/
        freg_t  tcq_ed_adr;     /* Transmit Complete Queue End Address  */
        freg_t  tcq_rd_ptr;     /* Transmit Complete Queue read pointer */
        freg_t  tcq_wr_ptr;     /* Transmit Complete Queue write pointer*/
        u_int   filler38[0x40 - 0x38];
        freg_t  queue_base;     /* Base address for PRQ and TCQ         */
        freg_t  desc_base;      /* Base address of descriptor table     */
        u_int   filler42[0x45 - 0x42];
        freg_t  mode_reg_0;     /* Mode register 0                      */
        freg_t  mode_reg_1;     /* Mode register 1                      */
        freg_t  intr_status_reg;/* Interrupt Status register            */
        freg_t  mask_reg;       /* Mask Register                        */
        freg_t  cell_ctr_high1; /* Total cell transfer count (high)     */
        freg_t  cell_ctr_lo1;   /* Total cell transfer count (low)      */
        freg_t  state_reg;      /* Status register                      */
        u_int   filler4c[0x58 - 0x4c];
        freg_t  curr_desc_num;  /* Contains the current descriptor num  */
        freg_t  next_desc;      /* Next descriptor                      */
        freg_t  next_vc;        /* Next VC                              */
        u_int   filler5b[0x5d - 0x5b];
        freg_t  present_slot_cnt;/* Present slot count                  */
        u_int   filler5e[0x6a - 0x5e];
        freg_t  new_desc_num;   /* New descriptor number                */
        freg_t  new_vc;         /* New VC                               */
        freg_t  sched_tbl_ptr;  /* Schedule table pointer               */
        freg_t  vbrwq_wptr;     /* VBR wait queue write pointer         */
        freg_t  vbrwq_rptr;     /* VBR wait queue read pointer          */
        freg_t  abrwq_wptr;     /* ABR wait queue write pointer         */
        freg_t  abrwq_rptr;     /* ABR wait queue read pointer          */
        freg_t  ubrwq_wptr;     /* UBR wait queue write pointer         */
        freg_t  ubrwq_rptr;     /* UBR wait queue read pointer          */
        freg_t  cbr_vc;         /* CBR VC                               */
        freg_t  vbr_sb_vc;      /* VBR SB VC                            */
        freg_t  abr_sb_vc;      /* ABR SB VC                            */
        freg_t  ubr_sb_vc;      /* UBR SB VC                            */
        freg_t  vbr_next_link;  /* VBR next link                        */
        freg_t  abr_next_link;  /* ABR next link                        */
        freg_t  ubr_next_link;  /* UBR next link                        */
        u_int   filler7a[0x7c-0x7a];
        freg_t  out_rate_head;  /* Out of rate head                     */
        u_int   filler7d[0xca-0x7d]; /* pad out to full address space   */
        freg_t  cell_ctr_high1_nc;/* Total cell transfer count (high)   */
        freg_t  cell_ctr_lo1_nc;/* Total cell transfer count (low)      */
        u_int   fillercc[0x100-0xcc]; /* pad out to full address space   */
} ffredn_t;

typedef struct _rfredn_t {
        rreg_t  mode_reg_0;     /* Mode register 0                      */
        rreg_t  protocol_id;    /* Protocol ID                          */
        rreg_t  mask_reg;       /* Mask Register                        */
        rreg_t  intr_status_reg;/* Interrupt status register            */
        rreg_t  drp_pkt_cntr;   /* Dropped packet cntr (clear on read)  */
        rreg_t  err_cntr;       /* Error Counter (cleared on read)      */
        u_int   filler6[0x08 - 0x06];
        rreg_t  raw_base_adr;   /* Base addr for raw cell Q             */
        u_int   filler2[0x0c - 0x09];
        rreg_t  cell_ctr0;      /* Cell Counter 0 (cleared when read)   */
        rreg_t  cell_ctr1;      /* Cell Counter 1 (cleared when read)   */
        u_int   filler3[0x0f - 0x0e];
        rreg_t  cmd_reg;        /* Command register                     */
        rreg_t  desc_base;      /* Base address for description table   */
        rreg_t  vc_lkup_base;   /* Base address for VC lookup table     */
        rreg_t  reass_base;     /* Base address for reassembler table   */
        rreg_t  queue_base;     /* Base address for Communication queue */
        u_int   filler14[0x16 - 0x14];
        rreg_t  pkt_tm_cnt;     /* Packet Timeout and count register    */
        rreg_t  tmout_range;    /* Range of reassembley IDs for timeout */
        rreg_t  intrvl_cntr;    /* Packet aging interval counter        */
        rreg_t  tmout_indx;     /* index of pkt being tested for aging  */
        u_int   filler1a[0x1c - 0x1a];
        rreg_t  vp_lkup_base;   /* Base address for VP lookup table     */
        rreg_t  vp_filter;      /* VP filter register                   */
        rreg_t  abr_lkup_base;  /* Base address of ABR VC Table         */
        u_int   filler1f[0x24 - 0x1f];
        rreg_t  fdq_st_adr;     /* Free desc queue start address        */
        rreg_t  fdq_ed_adr;     /* Free desc queue end address          */
        rreg_t  fdq_rd_ptr;     /* Free desc queue read pointer         */
        rreg_t  fdq_wr_ptr;     /* Free desc queue write pointer        */
        rreg_t  pcq_st_adr;     /* Packet Complete queue start address  */
        rreg_t  pcq_ed_adr;     /* Packet Complete queue end address    */
        rreg_t  pcq_rd_ptr;     /* Packet Complete queue read pointer   */
        rreg_t  pcq_wr_ptr;     /* Packet Complete queue write pointer  */
        rreg_t  excp_st_adr;    /* Exception queue start address        */
        rreg_t  excp_ed_adr;    /* Exception queue end address          */
        rreg_t  excp_rd_ptr;    /* Exception queue read pointer         */
        rreg_t  excp_wr_ptr;    /* Exception queue write pointer        */
        u_int   filler30[0x34 - 0x30];
        rreg_t  raw_st_adr;     /* Raw Cell start address               */
        rreg_t  raw_ed_adr;     /* Raw Cell end address                 */
        rreg_t  raw_rd_ptr;     /* Raw Cell read pointer                */
        rreg_t  raw_wr_ptr;     /* Raw Cell write pointer               */
        rreg_t  state_reg;      /* State Register                       */
        u_int   filler39[0x42 - 0x39];
        rreg_t  buf_size;       /* Buffer size                          */
        u_int   filler43;
        rreg_t  xtra_rm_offset; /* Offset of the additional turnaround RM */
        u_int   filler45[0x84 - 0x45];
        rreg_t  drp_pkt_cntr_nc;/* Dropped Packet cntr, Not clear on rd */
        rreg_t  err_cntr_nc;    /* Error Counter, Not clear on read     */
        u_int   filler86[0x8c - 0x86];
        rreg_t  cell_ctr0_nc;   /* Cell Counter 0,  Not clear on read   */
        rreg_t  cell_ctr1_nc;   /* Cell Counter 1, Not clear on read    */
        u_int   filler8e[0x100-0x8e]; /* pad out to full address space   */
} rfredn_t;

typedef struct {
        /* Atlantic */
        ffredn_t        ffredn;         /* F FRED                       */
        rfredn_t        rfredn;         /* R FRED                       */
} ia_regs_t;

typedef struct {
	u_short		f_vc_type;	/* VC type              */
	u_short		f_nrm;		/* Nrm			*/
	u_short		f_nrmexp;	/* Nrm Exp              */
	u_short		reserved6;	/* 			*/
	u_short		f_crm;		/* Crm			*/
	u_short		reserved10;	/* Reserved		*/
	u_short		reserved12;	/* Reserved		*/
	u_short		reserved14;	/* Reserved		*/
	u_short		last_cell_slot;	/* last_cell_slot_count	*/
	u_short		f_pcr;		/* Peak Cell Rate	*/
	u_short		fraction;	/* fraction		*/
	u_short		f_icr;		/* Initial Cell Rate	*/
	u_short		f_cdf;		/* */
	u_short		f_mcr;		/* Minimum Cell Rate	*/
	u_short		f_acr;		/* Allowed Cell Rate	*/
	u_short		f_status;	/* */
} f_vc_abr_entry;

typedef struct {
        u_short         r_status_rdf;   /* status + RDF         */
        u_short         r_air;          /* AIR                  */
        u_short         reserved4[14];  /* Reserved             */
} r_vc_abr_entry;   

#define MRM 3

typedef struct srv_cls_param {
        u32 class_type;         /* CBR/VBR/ABR/UBR; use the enum above */
        u32 pcr;                /* Peak Cell Rate (24-bit) */ 
        /* VBR parameters */
        u32 scr;                /* sustainable cell rate */
        u32 max_burst_size;     /* ?? cell rate or data rate */
 
        /* ABR only UNI 4.0 Parameters */
        u32 mcr;                /* Min Cell Rate (24-bit) */
        u32 icr;                /* Initial Cell Rate (24-bit) */
        u32 tbe;                /* Transient Buffer Exposure (24-bit) */
        u32 frtt;               /* Fixed Round Trip Time (24-bit) */
 
#if 0   /* Additional Parameters of TM 4.0 */
bits  31          30           29          28       27-25 24-22 21-19  18-9
-----------------------------------------------------------------------------
| NRM present | TRM prsnt | CDF prsnt | ADTF prsnt | NRM | TRM | CDF | ADTF |
-----------------------------------------------------------------------------
#endif /* 0 */
 
        u8 nrm;                 /* Max # of Cells for each forward RM
                                        cell (3-bit) */
        u8 trm;                 /* Time between forward RM cells (3-bit) */
        u16 adtf;               /* ACR Decrease Time Factor (10-bit) */
        u8 cdf;                 /* Cutoff Decrease Factor (3-bit) */
        u8 rif;                 /* Rate Increment Factor (4-bit) */
        u8 rdf;                 /* Rate Decrease Factor (4-bit) */
        u8 reserved;            /* 8 bits to keep structure word aligned */
} srv_cls_param_t;

struct testTable_t {
	u16 lastTime; 
	u16 fract; 
	u8 vc_status;
}; 

typedef struct {
	u16 vci;
	u16 error;
} RX_ERROR_Q;

typedef struct {
	u8 active: 1; 
	u8 abr: 1; 
	u8 ubr: 1; 
	u8 cnt: 5;
#define VC_ACTIVE 	0x01
#define VC_ABR		0x02
#define VC_UBR		0x04
} vcstatus_t;
  
struct ia_rfL_t {
    	u32  fdq_st;     /* Free desc queue start address        */
        u32  fdq_ed;     /* Free desc queue end address          */
        u32  fdq_rd;     /* Free desc queue read pointer         */
        u32  fdq_wr;     /* Free desc queue write pointer        */
        u32  pcq_st;     /* Packet Complete queue start address  */
        u32  pcq_ed;     /* Packet Complete queue end address    */
        u32  pcq_rd;     /* Packet Complete queue read pointer   */
        u32  pcq_wr;     /* Packet Complete queue write pointer  */ 
};

struct ia_ffL_t {
	u32  prq_st;     /* Packet Ready Queue Start Address     */
        u32  prq_ed;     /* Packet Ready Queue End Address       */
        u32  prq_wr;     /* Packet Ready Queue write pointer     */
        u32  tcq_st;     /* Transmit Complete Queue Start Address*/
        u32  tcq_ed;     /* Transmit Complete Queue End Address  */
        u32  tcq_rd;     /* Transmit Complete Queue read pointer */
};

struct desc_tbl_t {
        u32 timestamp;
        struct ia_vcc *iavcc;
        struct sk_buff *txskb;
}; 

typedef struct ia_rtn_q {
   struct desc_tbl_t data;
   struct ia_rtn_q *next, *tail;
} IARTN_Q;

#define SUNI_LOSV   	0x04
typedef struct {
        u32   suni_master_reset;      /* SUNI Master Reset and Identity     */
        u32   suni_master_config;     /* SUNI Master Configuration          */
        u32   suni_master_intr_stat;  /* SUNI Master Interrupt Status       */
        u32   suni_reserved1;         /* Reserved                           */
        u32   suni_master_clk_monitor;/* SUNI Master Clock Monitor          */
        u32   suni_master_control;    /* SUNI Master Clock Monitor          */
        u32   suni_reserved2[10];     /* Reserved                           */

        u32   suni_rsop_control;      /* RSOP Control/Interrupt Enable      */
        u32   suni_rsop_status;       /* RSOP Status/Interrupt States       */
        u32   suni_rsop_section_bip8l;/* RSOP Section BIP-8 LSB             */
        u32   suni_rsop_section_bip8m;/* RSOP Section BIP-8 MSB             */

        u32   suni_tsop_control;      /* TSOP Control                       */
        u32   suni_tsop_diag;         /* TSOP Disgnostics                   */
        u32   suni_tsop_reserved[2];  /* TSOP Reserved                      */

        u32   suni_rlop_cs;           /* RLOP Control/Status                */
        u32   suni_rlop_intr;         /* RLOP Interrupt Enable/Status       */
        u32   suni_rlop_line_bip24l;  /* RLOP Line BIP-24 LSB               */
        u32   suni_rlop_line_bip24;   /* RLOP Line BIP-24                   */
        u32   suni_rlop_line_bip24m;  /* RLOP Line BIP-24 MSB               */
        u32   suni_rlop_line_febel;   /* RLOP Line FEBE LSB                 */
        u32   suni_rlop_line_febe;    /* RLOP Line FEBE                     */
        u32   suni_rlop_line_febem;   /* RLOP Line FEBE MSB                 */

        u32   suni_tlop_control;      /* TLOP Control                       */
        u32   suni_tlop_disg;         /* TLOP Disgnostics                   */
        u32   suni_tlop_reserved[14]; /* TLOP Reserved                      */

        u32   suni_rpop_cs;           /* RPOP Status/Control                */
        u32   suni_rpop_intr;         /* RPOP Interrupt/Status              */
        u32   suni_rpop_reserved;     /* RPOP Reserved                      */
        u32   suni_rpop_intr_ena;     /* RPOP Interrupt Enable              */
        u32   suni_rpop_reserved1[3]; /* RPOP Reserved                      */
        u32   suni_rpop_path_sig;     /* RPOP Path Signal Label             */
        u32   suni_rpop_bip8l;        /* RPOP Path BIP-8 LSB                */
        u32   suni_rpop_bip8m;        /* RPOP Path BIP-8 MSB                */
        u32   suni_rpop_febel;        /* RPOP Path FEBE LSB                 */
        u32   suni_rpop_febem;        /* RPOP Path FEBE MSB                 */
        u32   suni_rpop_reserved2[4]; /* RPOP Reserved                      */

        u32   suni_tpop_cntrl_daig;   /* TPOP Control/Disgnostics           */
        u32   suni_tpop_pointer_ctrl; /* TPOP Pointer Control               */
        u32   suni_tpop_sourcer_ctrl; /* TPOP Source Control                */
        u32   suni_tpop_reserved1[2]; /* TPOP Reserved                      */
        u32   suni_tpop_arb_prtl;     /* TPOP Arbitrary Pointer LSB         */
        u32   suni_tpop_arb_prtm;     /* TPOP Arbitrary Pointer MSB         */
        u32   suni_tpop_reserved2;    /* TPOP Reserved                      */
        u32   suni_tpop_path_sig;     /* TPOP Path Signal Lable             */
        u32   suni_tpop_path_status;  /* TPOP Path Status                   */
        u32   suni_tpop_reserved3[6]; /* TPOP Reserved                      */              

        u32   suni_racp_cs;           /* RACP Control/Status                */
        u32   suni_racp_intr;         /* RACP Interrupt Enable/Status       */
        u32   suni_racp_hdr_pattern;  /* RACP Match Header Pattern          */
        u32   suni_racp_hdr_mask;     /* RACP Match Header Mask             */
        u32   suni_racp_corr_hcs;     /* RACP Correctable HCS Error Count   */
        u32   suni_racp_uncorr_hcs;   /* RACP Uncorrectable HCS Error Count */
        u32   suni_racp_reserved[10]; /* RACP Reserved                      */

        u32   suni_tacp_control;      /* TACP Control                       */
        u32   suni_tacp_idle_hdr_pat; /* TACP Idle Cell Header Pattern      */
        u32   suni_tacp_idle_pay_pay; /* TACP Idle Cell Payld Octet Pattern */
        u32   suni_tacp_reserved[5];  /* TACP Reserved                      */

        u32   suni_reserved3[24];     /* Reserved                           */

        u32   suni_master_test;       /* SUNI Master Test                   */
        u32   suni_reserved_test;     /* SUNI Reserved for Test             */
} IA_SUNI;


typedef struct _SUNI_STATS_
{
   u32 valid;                       // 1 = oc3 PHY card
   u32 carrier_detect;              // GPIN input
   // RSOP: receive section overhead processor
   u16 rsop_oof_state;              // 1 = out of frame
   u16 rsop_lof_state;              // 1 = loss of frame
   u16 rsop_los_state;              // 1 = loss of signal
   u32 rsop_los_count;              // loss of signal count
   u32 rsop_bse_count;              // section BIP-8 error count
   // RLOP: receive line overhead processor
   u16 rlop_ferf_state;             // 1 = far end receive failure
   u16 rlop_lais_state;             // 1 = line AIS
   u32 rlop_lbe_count;              // BIP-24 count
   u32 rlop_febe_count;             // FEBE count;
   // RPOP: receive path overhead processor
   u16 rpop_lop_state;              // 1 = LOP
   u16 rpop_pais_state;             // 1 = path AIS
   u16 rpop_pyel_state;             // 1 = path yellow alert
   u32 rpop_bip_count;              // path BIP-8 error count
   u32 rpop_febe_count;             // path FEBE error count
   u16 rpop_psig;                   // path signal label value
   // RACP: receive ATM cell processor
   u16 racp_hp_state;               // hunt/presync state
   u32 racp_fu_count;               // FIFO underrun count
   u32 racp_fo_count;               // FIFO overrun count
   u32 racp_chcs_count;             // correctable HCS error count
   u32 racp_uchcs_count;            // uncorrectable HCS error count
} IA_SUNI_STATS; 

typedef struct iadev_t {  
	/*-----base pointers into (i)chipSAR+ address space */   
	u32 __iomem *phy;		/* base pointer into phy(SUNI) */  
	u32 __iomem *dma;		/* base pointer into DMA control   
						registers */  
	u32 __iomem *reg;		/* base pointer to SAR registers  
					   - Bus Interface Control Regs */  
	u32 __iomem *seg_reg;		/* base pointer to segmentation engine  
						internal registers */  
	u32 __iomem *reass_reg;		/* base pointer to reassemble engine  
						internal registers */  
	u32 __iomem *ram;		/* base pointer to SAR RAM */  
	void __iomem *seg_ram;  
	void __iomem *reass_ram;  
	struct dle_q tx_dle_q;  
	struct free_desc_q *tx_free_desc_qhead;  
	struct sk_buff_head tx_dma_q, tx_backlog;  
        spinlock_t            tx_lock;
        IARTN_Q               tx_return_q;
        u32                   close_pending;
        wait_queue_head_t    close_wait;
        wait_queue_head_t    timeout_wait;
	struct cpcs_trailer_desc *tx_buf;
        u16 num_tx_desc, tx_buf_sz, rate_limit;
        u32 tx_cell_cnt, tx_pkt_cnt;
        void __iomem *MAIN_VC_TABLE_ADDR, *EXT_VC_TABLE_ADDR, *ABR_SCHED_TABLE_ADDR;
	struct dle_q rx_dle_q;  
	struct free_desc_q *rx_free_desc_qhead;  
	struct sk_buff_head rx_dma_q;  
        spinlock_t rx_lock, misc_lock;
	struct atm_vcc **rx_open;	/* list of all open VCs */  
        u16 num_rx_desc, rx_buf_sz, rxing;
        u32 rx_pkt_ram, rx_tmp_cnt, rx_tmp_jif;
        void __iomem *RX_DESC_BASE_ADDR;
        u32 drop_rxpkt, drop_rxcell, rx_cell_cnt, rx_pkt_cnt;
	struct atm_dev *next_board;	/* other iphase devices */  
	struct pci_dev *pci;  
	int mem;  
	unsigned int real_base;	/* real and virtual base address */  
	void __iomem *base;
	unsigned int pci_map_size;	/*pci map size of board */  
	unsigned char irq;  
	unsigned char bus;  
	unsigned char dev_fn;  
        u_short  phy_type;
        u_short  num_vc, memSize, memType;
        struct ia_ffL_t ffL;
        struct ia_rfL_t rfL;
        /* Suni stat */
        // IA_SUNI_STATS suni_stats;
        unsigned char carrier_detect;
        /* CBR related */
        // transmit DMA & Receive
        unsigned int tx_dma_cnt;     // number of elements on dma queue
        unsigned int rx_dma_cnt;     // number of elements on rx dma queue
        unsigned int NumEnabledCBR;  // number of CBR VCI's enabled.     CBR
        // receive MARK  for Cell FIFO
        unsigned int rx_mark_cnt;    // number of elements on mark queue
        unsigned int CbrTotEntries;  // Total CBR Entries in Scheduling Table.
        unsigned int CbrRemEntries;  // Remaining CBR Entries in Scheduling Table.
        unsigned int CbrEntryPt;     // CBR Sched Table Entry Point.
        unsigned int Granularity;    // CBR Granularity given Table Size.
        /* ABR related */
	unsigned int  sum_mcr, sum_cbr, LineRate;
	unsigned int  n_abr;
        struct desc_tbl_t *desc_tbl;
        u_short host_tcq_wr;
        struct testTable_t **testTable;
	dma_addr_t tx_dle_dma;
	dma_addr_t rx_dle_dma;
} IADEV;
  
  
#define INPH_IA_DEV(d) ((IADEV *) (d)->dev_data)  
#define INPH_IA_VCC(v) ((struct ia_vcc *) (v)->dev_data)  

/******************* IDT77105 25MB/s PHY DEFINE *****************************/
typedef struct {
	u_int	mb25_master_ctrl;	/* Master control		     */
	u_int	mb25_intr_status;	/* Interrupt status		     */
	u_int	mb25_diag_control;	/* Diagnostic control		     */
	u_int	mb25_led_hec;		/* LED driver and HEC status/control */
	u_int	mb25_low_byte_counter;	/* Low byte counter		     */
	u_int	mb25_high_byte_counter;	/* High byte counter		     */
} ia_mb25_t;

/*
 * Master Control
 */
#define	MB25_MC_UPLO	0x80		/* UPLO				     */
#define	MB25_MC_DREC	0x40		/* Discard receive cell errors	     */
#define	MB25_MC_ECEIO	0x20		/* Enable Cell Error Interrupts Only */
#define	MB25_MC_TDPC	0x10		/* Transmit data parity check	     */
#define	MB25_MC_DRIC	0x08		/* Discard receive idle cells	     */
#define	MB25_MC_HALTTX	0x04		/* Halt Tx			     */
#define	MB25_MC_UMS	0x02		/* UTOPIA mode select		     */
#define	MB25_MC_ENABLED	0x01		/* Enable interrupt		     */

/*
 * Interrupt Status
 */
#define	MB25_IS_GSB	0x40		/* GOOD Symbol Bit		     */	
#define	MB25_IS_HECECR	0x20		/* HEC error cell received	     */
#define	MB25_IS_SCR	0x10		/* "Short Cell" Received	     */
#define	MB25_IS_TPE	0x08		/* Trnamsit Parity Error	     */
#define	MB25_IS_RSCC	0x04		/* Receive Signal Condition change   */
#define	MB25_IS_RCSE	0x02		/* Received Cell Symbol Error	     */
#define	MB25_IS_RFIFOO	0x01		/* Received FIFO Overrun	     */

/*
 * Diagnostic Control
 */
#define	MB25_DC_FTXCD	0x80		/* Force TxClav deassert	     */	
#define	MB25_DC_RXCOS	0x40		/* RxClav operation select	     */
#define	MB25_DC_ECEIO	0x20		/* Single/Multi-PHY config select    */
#define	MB25_DC_RLFLUSH	0x10		/* Clear receive FIFO		     */
#define	MB25_DC_IXPE	0x08		/* Insert xmit payload error	     */
#define	MB25_DC_IXHECE	0x04		/* Insert Xmit HEC Error	     */
#define	MB25_DC_LB_MASK	0x03		/* Loopback control mask	     */

#define	MB25_DC_LL	0x03		/* Line Loopback		     */
#define	MB25_DC_PL	0x02		/* PHY Loopback			     */
#define	MB25_DC_NM	0x00		

#define FE_MASK 	0x00F0
#define FE_MULTI_MODE	0x0000
#define FE_SINGLE_MODE  0x0010 
#define FE_UTP_OPTION  	0x0020
#define FE_25MBIT_PHY	0x0040
#define FE_DS3_PHY      0x0080          /* DS3 */
#define FE_E3_PHY       0x0090          /* E3 */
		     
/*********************** SUNI_PM7345 PHY DEFINE HERE *********************/
typedef struct _suni_pm7345_t
{
    u_int suni_config;     /* SUNI Configuration */
    u_int suni_intr_enbl;  /* SUNI Interrupt Enable */
    u_int suni_intr_stat;  /* SUNI Interrupt Status */
    u_int suni_control;    /* SUNI Control */
    u_int suni_id_reset;   /* SUNI Reset and Identity */
    u_int suni_data_link_ctrl;
    u_int suni_rboc_conf_intr_enbl;
    u_int suni_rboc_stat;
    u_int suni_ds3_frm_cfg;
    u_int suni_ds3_frm_intr_enbl;
    u_int suni_ds3_frm_intr_stat;
    u_int suni_ds3_frm_stat;
    u_int suni_rfdl_cfg;
    u_int suni_rfdl_enbl_stat;
    u_int suni_rfdl_stat;
    u_int suni_rfdl_data;
    u_int suni_pmon_chng;
    u_int suni_pmon_intr_enbl_stat;
    u_int suni_reserved1[0x13-0x11];
    u_int suni_pmon_lcv_evt_cnt_lsb;
    u_int suni_pmon_lcv_evt_cnt_msb;
    u_int suni_pmon_fbe_evt_cnt_lsb;
    u_int suni_pmon_fbe_evt_cnt_msb;
    u_int suni_pmon_sez_det_cnt_lsb;
    u_int suni_pmon_sez_det_cnt_msb;
    u_int suni_pmon_pe_evt_cnt_lsb;
    u_int suni_pmon_pe_evt_cnt_msb;
    u_int suni_pmon_ppe_evt_cnt_lsb;
    u_int suni_pmon_ppe_evt_cnt_msb;
    u_int suni_pmon_febe_evt_cnt_lsb;
    u_int suni_pmon_febe_evt_cnt_msb;
    u_int suni_ds3_tran_cfg;
    u_int suni_ds3_tran_diag;
    u_int suni_reserved2[0x23-0x21];
    u_int suni_xfdl_cfg;
    u_int suni_xfdl_intr_st;
    u_int suni_xfdl_xmit_data;
    u_int suni_xboc_code;
    u_int suni_splr_cfg;
    u_int suni_splr_intr_en;
    u_int suni_splr_intr_st;
    u_int suni_splr_status;
    u_int suni_splt_cfg;
    u_int suni_splt_cntl;
    u_int suni_splt_diag_g1;
    u_int suni_splt_f1;
    u_int suni_cppm_loc_meters;
    u_int suni_cppm_chng_of_cppm_perf_meter;
    u_int suni_cppm_b1_err_cnt_lsb;
    u_int suni_cppm_b1_err_cnt_msb;
    u_int suni_cppm_framing_err_cnt_lsb;
    u_int suni_cppm_framing_err_cnt_msb;
    u_int suni_cppm_febe_cnt_lsb;
    u_int suni_cppm_febe_cnt_msb;
    u_int suni_cppm_hcs_err_cnt_lsb;
    u_int suni_cppm_hcs_err_cnt_msb;
    u_int suni_cppm_idle_un_cell_cnt_lsb;
    u_int suni_cppm_idle_un_cell_cnt_msb;
    u_int suni_cppm_rcv_cell_cnt_lsb;
    u_int suni_cppm_rcv_cell_cnt_msb;
    u_int suni_cppm_xmit_cell_cnt_lsb;
    u_int suni_cppm_xmit_cell_cnt_msb;
    u_int suni_rxcp_ctrl;
    u_int suni_rxcp_fctrl;
    u_int suni_rxcp_intr_en_sts;
    u_int suni_rxcp_idle_pat_h1;
    u_int suni_rxcp_idle_pat_h2;
    u_int suni_rxcp_idle_pat_h3;
    u_int suni_rxcp_idle_pat_h4;
    u_int suni_rxcp_idle_mask_h1;
    u_int suni_rxcp_idle_mask_h2;
    u_int suni_rxcp_idle_mask_h3;
    u_int suni_rxcp_idle_mask_h4;
    u_int suni_rxcp_cell_pat_h1;
    u_int suni_rxcp_cell_pat_h2;
    u_int suni_rxcp_cell_pat_h3;
    u_int suni_rxcp_cell_pat_h4;
    u_int suni_rxcp_cell_mask_h1;
    u_int suni_rxcp_cell_mask_h2;
    u_int suni_rxcp_cell_mask_h3;
    u_int suni_rxcp_cell_mask_h4;
    u_int suni_rxcp_hcs_cs;
    u_int suni_rxcp_lcd_cnt_threshold;
    u_int suni_reserved3[0x57-0x54];
    u_int suni_txcp_ctrl;
    u_int suni_txcp_intr_en_sts;
    u_int suni_txcp_idle_pat_h1;
    u_int suni_txcp_idle_pat_h2;
    u_int suni_txcp_idle_pat_h3;
    u_int suni_txcp_idle_pat_h4;
    u_int suni_txcp_idle_pat_h5;
    u_int suni_txcp_idle_payload;
    u_int suni_e3_frm_fram_options;
    u_int suni_e3_frm_maint_options;
    u_int suni_e3_frm_fram_intr_enbl;
    u_int suni_e3_frm_fram_intr_ind_stat;
    u_int suni_e3_frm_maint_intr_enbl;
    u_int suni_e3_frm_maint_intr_ind;
    u_int suni_e3_frm_maint_stat;
    u_int suni_reserved4;
    u_int suni_e3_tran_fram_options;
    u_int suni_e3_tran_stat_diag_options;
    u_int suni_e3_tran_bip_8_err_mask;
    u_int suni_e3_tran_maint_adapt_options;
    u_int suni_ttb_ctrl;
    u_int suni_ttb_trail_trace_id_stat;
    u_int suni_ttb_ind_addr;
    u_int suni_ttb_ind_data;
    u_int suni_ttb_exp_payload_type;
    u_int suni_ttb_payload_type_ctrl_stat;
    u_int suni_pad5[0x7f-0x71];
    u_int suni_master_test;
    u_int suni_pad6[0xff-0x80];
}suni_pm7345_t;

#define SUNI_PM7345_T suni_pm7345_t
#define SUNI_PM7345     0x20            /* Suni chip type */
#define SUNI_PM5346     0x30            /* Suni chip type */
/*
 * SUNI_PM7345 Configuration
 */
#define SUNI_PM7345_CLB         0x01    /* Cell loopback        */
#define SUNI_PM7345_PLB         0x02    /* Payload loopback     */
#define SUNI_PM7345_DLB         0x04    /* Diagnostic loopback  */
#define SUNI_PM7345_LLB         0x80    /* Line loopback        */
#define SUNI_PM7345_E3ENBL      0x40    /* E3 enable bit        */
#define SUNI_PM7345_LOOPT       0x10    /* LOOPT enable bit     */
#define SUNI_PM7345_FIFOBP      0x20    /* FIFO bypass          */
#define SUNI_PM7345_FRMRBP      0x08    /* Framer bypass        */
/*
 * DS3 FRMR Interrupt Enable
 */
#define SUNI_DS3_COFAE  0x80            /* Enable change of frame align */
#define SUNI_DS3_REDE   0x40            /* Enable DS3 RED state intr    */
#define SUNI_DS3_CBITE  0x20            /* Enable Appl ID channel intr  */
#define SUNI_DS3_FERFE  0x10            /* Enable Far End Receive Failure intr*/
#define SUNI_DS3_IDLE   0x08            /* Enable Idle signal intr      */
#define SUNI_DS3_AISE   0x04            /* Enable Alarm Indication signal intr*/
#define SUNI_DS3_OOFE   0x02            /* Enable Out of frame intr     */
#define SUNI_DS3_LOSE   0x01            /* Enable Loss of signal intr   */
 
/*
 * DS3 FRMR Status
 */
#define SUNI_DS3_ACE    0x80            /* Additional Configuration Reg */
#define SUNI_DS3_REDV   0x40            /* DS3 RED state                */
#define SUNI_DS3_CBITV  0x20            /* Application ID channel state */
#define SUNI_DS3_FERFV  0x10            /* Far End Receive Failure state*/
#define SUNI_DS3_IDLV   0x08            /* Idle signal state            */
#define SUNI_DS3_AISV   0x04            /* Alarm Indication signal state*/
#define SUNI_DS3_OOFV   0x02            /* Out of frame state           */
#define SUNI_DS3_LOSV   0x01            /* Loss of signal state         */

/*
 * E3 FRMR Interrupt/Status
 */
#define SUNI_E3_CZDI    0x40            /* Consecutive Zeros indicator  */
#define SUNI_E3_LOSI    0x20            /* Loss of signal intr status   */
#define SUNI_E3_LCVI    0x10            /* Line code violation intr     */
#define SUNI_E3_COFAI   0x08            /* Change of frame align intr   */
#define SUNI_E3_OOFI    0x04            /* Out of frame intr status     */
#define SUNI_E3_LOS     0x02            /* Loss of signal state         */
#define SUNI_E3_OOF     0x01            /* Out of frame state           */

/*
 * E3 FRMR Maintenance Status
 */
#define SUNI_E3_AISD    0x80            /* Alarm Indication signal state*/
#define SUNI_E3_FERF_RAI        0x40    /* FERF/RAI indicator           */
#define SUNI_E3_FEBE    0x20            /* Far End Block Error indicator*/

/*
 * RXCP Control/Status
 */
#define SUNI_DS3_HCSPASS        0x80    /* Pass cell with HEC errors    */
#define SUNI_DS3_HCSDQDB        0x40    /* Control octets in HCS calc   */
#define SUNI_DS3_HCSADD         0x20    /* Add coset poly               */
#define SUNI_DS3_HCK            0x10    /* Control FIFO data path integ chk*/
#define SUNI_DS3_BLOCK          0x08    /* Enable cell filtering        */
#define SUNI_DS3_DSCR           0x04    /* Disable payload descrambling */
#define SUNI_DS3_OOCDV          0x02    /* Cell delineation state       */
#define SUNI_DS3_FIFORST        0x01    /* Cell FIFO reset              */

/*
 * RXCP Interrupt Enable/Status
 */
#define SUNI_DS3_OOCDE  0x80            /* Intr enable, change in CDS   */
#define SUNI_DS3_HCSE   0x40            /* Intr enable, corr HCS errors */
#define SUNI_DS3_FIFOE  0x20            /* Intr enable, unco HCS errors */
#define SUNI_DS3_OOCDI  0x10            /* SYNC state                   */
#define SUNI_DS3_UHCSI  0x08            /* Uncorr. HCS errors detected  */
#define SUNI_DS3_COCAI  0x04            /* Corr. HCS errors detected    */
#define SUNI_DS3_FOVRI  0x02            /* FIFO overrun                 */
#define SUNI_DS3_FUDRI  0x01            /* FIFO underrun                */

///////////////////SUNI_PM7345 PHY DEFINE END /////////////////////////////

/* ia_eeprom define*/
#define MEM_SIZE_MASK   0x000F          /* mask of 4 bits defining memory size*/
#define MEM_SIZE_128K   0x0000          /* board has 128k buffer */
#define MEM_SIZE_512K   0x0001          /* board has 512K of buffer */
#define MEM_SIZE_1M     0x0002          /* board has 1M of buffer */
                                        /* 0x3 to 0xF are reserved for future */

#define FE_MASK         0x00F0          /* mask of 4 bits defining FE type */
#define FE_MULTI_MODE   0x0000          /* 155 MBit multimode fiber */
#define FE_SINGLE_MODE  0x0010          /* 155 MBit single mode laser */
#define FE_UTP_OPTION   0x0020          /* 155 MBit UTP front end */

#define	NOVRAM_SIZE	64
#define	CMD_LEN		10

/***********
 *
 *	Switches and defines for header files.
 *
 *	The following defines are used to turn on and off
 *	various options in the header files. Primarily useful
 *	for debugging.
 *
 ***********/

/*
 * a list of the commands that can be sent to the NOVRAM
 */

#define	EXTEND	0x100
#define	IAWRITE	0x140
#define	IAREAD	0x180
#define	ERASE	0x1c0

#define	EWDS	0x00
#define	WRAL	0x10
#define	ERAL	0x20
#define	EWEN	0x30

/*
 * these bits duplicate the hw_flip.h register settings
 * note: how the data in / out bits are defined in the flipper specification 
 */

#define	NVCE	0x02
#define	NVSK	0x01
#define	NVDO	0x08	
#define NVDI	0x04
/***********************
 *
 * This define ands the value and the current config register and puts
 * the result in the config register
 *
 ***********************/

#define	CFG_AND(val) { \
		u32 t; \
		t = readl(iadev->reg+IPHASE5575_EEPROM_ACCESS); \
		t &= (val); \
		writel(t, iadev->reg+IPHASE5575_EEPROM_ACCESS); \
	}

/***********************
 *
 * This define ors the value and the current config register and puts
 * the result in the config register
 *
 ***********************/

#define	CFG_OR(val) { \
		u32 t; \
		t =  readl(iadev->reg+IPHASE5575_EEPROM_ACCESS); \
		t |= (val); \
		writel(t, iadev->reg+IPHASE5575_EEPROM_ACCESS); \
	}

/***********************
 *
 * Send a command to the NOVRAM, the command is in cmd.
 *
 * clear CE and SK. Then assert CE.
 * Clock each of the command bits out in the correct order with SK
 * exit with CE still asserted
 *
 ***********************/

#define	NVRAM_CMD(cmd) { \
		int	i; \
		u_short c = cmd; \
		CFG_AND(~(NVCE|NVSK)); \
		CFG_OR(NVCE); \
		for (i=0; i<CMD_LEN; i++) { \
			NVRAM_CLKOUT((c & (1 << (CMD_LEN - 1))) ? 1 : 0); \
			c <<= 1; \
		} \
	}

/***********************
 *
 * clear the CE, this must be used after each command is complete
 *
 ***********************/

#define	NVRAM_CLR_CE	{CFG_AND(~NVCE)}

/***********************
 *
 * clock the data bit in bitval out to the NOVRAM.  The bitval must be
 * a 1 or 0, or the clockout operation is undefined
 *
 ***********************/

#define	NVRAM_CLKOUT(bitval) { \
		CFG_AND(~NVDI); \
		CFG_OR((bitval) ? NVDI : 0); \
		CFG_OR(NVSK); \
		CFG_AND( ~NVSK); \
	}

/***********************
 *
 * clock the data bit in and return a 1 or 0, depending on the value
 * that was received from the NOVRAM
 *
 ***********************/

#define	NVRAM_CLKIN(value) { \
		u32 _t; \
		CFG_OR(NVSK); \
		CFG_AND(~NVSK); \
		_t = readl(iadev->reg+IPHASE5575_EEPROM_ACCESS); \
		value = (_t & NVDO) ? 1 : 0; \
	}


#endif /* IPHASE_H */
