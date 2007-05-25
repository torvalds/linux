/******************************************************************* 
 * ident "$Id: idt77252.h,v 1.2 2001/11/11 08:13:54 ecd Exp $"
 *
 * $Author: ecd $
 * $Date: 2001/11/11 08:13:54 $
 *
 * Copyright (c) 2000 ATecoM GmbH 
 *
 * The author may be reached at ecd@atecom.com.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR   IMPLIED
 * WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 * NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT,  INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 * USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * You should have received a copy of the  GNU General Public License along
 * with this program; if not, write  to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *******************************************************************/

#ifndef _IDT77252_H
#define _IDT77252_H 1


#include <linux/ptrace.h>
#include <linux/skbuff.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>

/*****************************************************************************/
/*                                                                           */
/* Makros                                                                    */
/*                                                                           */
/*****************************************************************************/
#define VPCI2VC(card, vpi, vci) \
        (((vpi) << card->vcibits) | ((vci) & card->vcimask))

/*****************************************************************************/
/*                                                                           */
/*   DEBUGGING definitions                                                   */
/*                                                                           */
/*****************************************************************************/

#define DBG_RAW_CELL	0x00000400
#define DBG_TINY	0x00000200
#define DBG_GENERAL     0x00000100
#define DBG_XGENERAL    0x00000080
#define DBG_INIT        0x00000040
#define DBG_DEINIT      0x00000020
#define DBG_INTERRUPT   0x00000010
#define DBG_OPEN_CONN   0x00000008
#define DBG_CLOSE_CONN  0x00000004
#define DBG_RX_DATA     0x00000002
#define DBG_TX_DATA     0x00000001

#ifdef CONFIG_ATM_IDT77252_DEBUG

#define CPRINTK(args...)   do { if (debug & DBG_CLOSE_CONN) printk(args); } while(0)
#define OPRINTK(args...)   do { if (debug & DBG_OPEN_CONN)  printk(args); } while(0)
#define IPRINTK(args...)   do { if (debug & DBG_INIT)       printk(args); } while(0)
#define INTPRINTK(args...) do { if (debug & DBG_INTERRUPT)  printk(args); } while(0)
#define DIPRINTK(args...)  do { if (debug & DBG_DEINIT)     printk(args); } while(0)
#define TXPRINTK(args...)  do { if (debug & DBG_TX_DATA)    printk(args); } while(0)
#define RXPRINTK(args...)  do { if (debug & DBG_RX_DATA)    printk(args); } while(0)
#define XPRINTK(args...)   do { if (debug & DBG_XGENERAL)   printk(args); } while(0)
#define DPRINTK(args...)   do { if (debug & DBG_GENERAL)    printk(args); } while(0)
#define NPRINTK(args...)   do { if (debug & DBG_TINY)	    printk(args); } while(0)
#define RPRINTK(args...)   do { if (debug & DBG_RAW_CELL)   printk(args); } while(0)

#else

#define CPRINTK(args...)	do { } while(0)
#define OPRINTK(args...)	do { } while(0)
#define IPRINTK(args...)	do { } while(0)
#define INTPRINTK(args...)	do { } while(0)
#define DIPRINTK(args...)	do { } while(0)
#define TXPRINTK(args...)	do { } while(0)
#define RXPRINTK(args...)	do { } while(0)
#define XPRINTK(args...)	do { } while(0)
#define DPRINTK(args...)	do { } while(0)
#define NPRINTK(args...)	do { } while(0)
#define RPRINTK(args...)	do { } while(0)

#endif

#define SCHED_UBR0		0
#define SCHED_UBR		1
#define SCHED_VBR		2
#define SCHED_ABR		3
#define SCHED_CBR		4

#define SCQFULL_TIMEOUT		HZ

/*****************************************************************************/
/*                                                                           */
/*   Free Buffer Queue Layout                                                */
/*                                                                           */
/*****************************************************************************/
#define SAR_FB_SIZE_0		(2048 - 256)
#define SAR_FB_SIZE_1		(4096 - 256)
#define SAR_FB_SIZE_2		(8192 - 256)
#define SAR_FB_SIZE_3		(16384 - 256)

#define SAR_FBQ0_LOW		4
#define SAR_FBQ0_HIGH		8
#define SAR_FBQ1_LOW		2
#define SAR_FBQ1_HIGH		4
#define SAR_FBQ2_LOW		1
#define SAR_FBQ2_HIGH		2
#define SAR_FBQ3_LOW		1
#define SAR_FBQ3_HIGH		2

#if 0
#define SAR_TST_RESERVED	44	/* Num TST reserved for UBR/ABR/VBR */
#else
#define SAR_TST_RESERVED	0	/* Num TST reserved for UBR/ABR/VBR */
#endif

#define TCT_CBR			0x00000000
#define TCT_UBR			0x00000000
#define TCT_VBR			0x40000000
#define TCT_ABR			0x80000000
#define TCT_TYPE		0xc0000000

#define TCT_RR			0x20000000
#define TCT_LMCR		0x08000000
#define TCT_SCD_MASK		0x0007ffff

#define TCT_TSIF		0x00004000
#define TCT_HALT		0x80000000
#define TCT_IDLE		0x40000000
#define TCT_FLAG_UBR		0x80000000

/*****************************************************************************/
/*                                                                           */
/*   Structure describing an IDT77252                                        */
/*                                                                           */
/*****************************************************************************/

struct scqe
{
	u32		word_1;
	u32		word_2;
	u32		word_3;
	u32		word_4;
};

#define SCQ_ENTRIES	64
#define SCQ_SIZE	(SCQ_ENTRIES * sizeof(struct scqe))
#define SCQ_MASK	(SCQ_SIZE - 1)

struct scq_info
{
	struct scqe		*base;
	struct scqe		*next;
	struct scqe		*last;
	dma_addr_t		paddr;
	spinlock_t		lock;
	atomic_t		used;
	unsigned long		trans_start;
        unsigned long		scd;
	spinlock_t		skblock;
	struct sk_buff_head	transmit;
	struct sk_buff_head	pending;
};

struct rx_pool {
	struct sk_buff		*first;
	struct sk_buff		**last;
	unsigned int		len;
	unsigned int		count;
};

struct aal1 {
	unsigned int		total;
	unsigned int		count;
	struct sk_buff		*data;
	unsigned char		sequence;
};

struct rate_estimator {
	struct timer_list	timer;
	unsigned int		interval;
	unsigned int		ewma_log;
	u64			cells;
	u64			last_cells;
	long			avcps;
	u32			cps;
	u32			maxcps;
};

struct vc_map {
	unsigned int		index;
	unsigned long		flags;
#define VCF_TX		0
#define VCF_RX		1
#define VCF_IDLE	2
#define VCF_RSV		3
	unsigned int		class;
	u8			init_er;
	u8			lacr;
	u8			max_er;
	unsigned int		ntste;
	spinlock_t		lock;
	struct atm_vcc		*tx_vcc;
	struct atm_vcc		*rx_vcc;
	struct idt77252_dev	*card;
	struct scq_info		*scq;		/* To keep track of the SCQ */
	struct rate_estimator	*estimator;
	int			scd_index;
	union {
		struct rx_pool	rx_pool;
		struct aal1	aal1;
	} rcv;
};

/*****************************************************************************/
/*                                                                           */
/*   RCTE - Receive Connection Table Entry                                   */
/*                                                                           */
/*****************************************************************************/

struct rct_entry
{
	u32		word_1;
	u32		buffer_handle;
	u32		dma_address;
	u32		aal5_crc32;
};

/*****************************************************************************/
/*                                                                           */
/*   RSQ - Receive Status Queue                                              */
/*                                                                           */
/*****************************************************************************/

#define SAR_RSQE_VALID      0x80000000
#define SAR_RSQE_IDLE       0x40000000
#define SAR_RSQE_BUF_MASK   0x00030000
#define SAR_RSQE_BUF_ASGN   0x00008000
#define SAR_RSQE_NZGFC      0x00004000
#define SAR_RSQE_EPDU       0x00002000
#define SAR_RSQE_BUF_CONT   0x00001000
#define SAR_RSQE_EFCIE      0x00000800
#define SAR_RSQE_CLP        0x00000400
#define SAR_RSQE_CRC        0x00000200
#define SAR_RSQE_CELLCNT    0x000001FF


#define RSQSIZE            8192
#define RSQ_NUM_ENTRIES    (RSQSIZE / 16)
#define RSQ_ALIGNMENT      8192

struct rsq_entry {
	u32			word_1;
	u32			word_2;
	u32			word_3;
	u32			word_4;
};

struct rsq_info {
	struct rsq_entry	*base;
	struct rsq_entry	*next;
	struct rsq_entry	*last;
	dma_addr_t		paddr;
};


/*****************************************************************************/
/*                                                                           */
/*   TSQ - Transmit Status Queue                                             */
/*                                                                           */
/*****************************************************************************/

#define SAR_TSQE_INVALID         0x80000000
#define SAR_TSQE_TIMESTAMP       0x00FFFFFF
#define SAR_TSQE_TYPE		 0x60000000
#define SAR_TSQE_TYPE_TIMER      0x00000000
#define SAR_TSQE_TYPE_TSR        0x20000000
#define SAR_TSQE_TYPE_IDLE       0x40000000
#define SAR_TSQE_TYPE_TBD_COMP   0x60000000

#define SAR_TSQE_TAG(stat)	(((stat) >> 24) & 0x1f)

#define TSQSIZE            8192
#define TSQ_NUM_ENTRIES    1024
#define TSQ_ALIGNMENT      8192

struct tsq_entry
{
	u32			word_1;
	u32			word_2;
};

struct tsq_info
{
	struct tsq_entry	*base;
	struct tsq_entry	*next;
	struct tsq_entry	*last;
	dma_addr_t		paddr;
};

struct tst_info
{
	struct vc_map		*vc;
	u32			tste;
};

#define TSTE_MASK		0x601fffff

#define TSTE_OPC_MASK		0x60000000
#define TSTE_OPC_NULL		0x00000000
#define TSTE_OPC_CBR		0x20000000
#define TSTE_OPC_VAR		0x40000000
#define TSTE_OPC_JMP		0x60000000

#define TSTE_PUSH_IDLE		0x01000000
#define TSTE_PUSH_ACTIVE	0x02000000

#define TST_SWITCH_DONE		0
#define TST_SWITCH_PENDING	1
#define TST_SWITCH_WAIT		2

#define FBQ_SHIFT		9
#define FBQ_SIZE		(1 << FBQ_SHIFT)
#define FBQ_MASK		(FBQ_SIZE - 1)

struct sb_pool
{
	unsigned int		index;
	struct sk_buff		*skb[FBQ_SIZE];
};

#define POOL_HANDLE(queue, index)	(((queue + 1) << 16) | (index))
#define POOL_QUEUE(handle)		(((handle) >> 16) - 1)
#define POOL_INDEX(handle)		((handle) & 0xffff)

struct idt77252_dev
{
        struct tsq_info		tsq;		/* Transmit Status Queue */
        struct rsq_info		rsq;		/* Receive Status Queue */

	struct pci_dev		*pcidev;	/* PCI handle (desriptor) */
	struct atm_dev		*atmdev;	/* ATM device desriptor */

	void __iomem		*membase;	/* SAR's memory base address */
	unsigned long		srambase;	/* SAR's sram  base address */
	void __iomem		*fbq[4];	/* FBQ fill addresses */

	struct mutex		mutex;
	spinlock_t		cmd_lock;	/* for r/w utility/sram */

	unsigned long		softstat;
	unsigned long		flags;		/* see blow */

	struct work_struct	tqueue;

	unsigned long		tct_base;	/* TCT base address in SRAM */
        unsigned long		rct_base;	/* RCT base address in SRAM */
        unsigned long		rt_base;	/* Rate Table base in SRAM */
        unsigned long		scd_base;	/* SCD base address in SRAM */
        unsigned long		tst[2];		/* TST base address in SRAM */
	unsigned long		abrst_base;	/* ABRST base address in SRAM */
        unsigned long		fifo_base;	/* RX FIFO base in SRAM */

	unsigned long		irqstat[16];

	unsigned int		sramsize;	/* SAR's sram size */

        unsigned int		tct_size;	/* total TCT entries */
        unsigned int		rct_size;	/* total RCT entries */
        unsigned int		scd_size;	/* length of SCD */
        unsigned int		tst_size;	/* total TST entries */
        unsigned int		tst_free;	/* free TSTEs in TST */
        unsigned int		abrst_size;	/* size of ABRST in words */
        unsigned int		fifo_size;	/* size of RX FIFO in words */

        unsigned int		vpibits;	/* Bits used for VPI index */
        unsigned int		vcibits;	/* Bits used for VCI index */
        unsigned int		vcimask;	/* Mask for VCI index */

	unsigned int		utopia_pcr;	/* Utopia Itf's Cell Rate */
	unsigned int		link_pcr;	/* PHY's Peek Cell Rate */

	struct vc_map		**vcs;		/* Open Connections */
	struct vc_map		**scd2vc;	/* SCD to Connection map */

	struct tst_info		*soft_tst;	/* TST to Connection map */
	unsigned int		tst_index;	/* Current TST in use */
	struct timer_list	tst_timer;
	spinlock_t		tst_lock;
	unsigned long		tst_state;

	struct sb_pool		sbpool[4];	/* Pool of RX skbuffs */
	struct sk_buff		*raw_cell_head; /* Pointer to raw cell queue */
	u32			*raw_cell_hnd;	/* Pointer to RCQ handle */
	dma_addr_t		raw_cell_paddr;

	int			index;		/* SAR's ID */
	int			revision;	/* chip revision */

	char			name[16];	/* Device name */

	struct idt77252_dev	*next;
};


/* definition for flag field above */
#define IDT77252_BIT_INIT		1
#define IDT77252_BIT_INTERRUPT		2


#define ATM_CELL_PAYLOAD         48

#define FREEBUF_ALIGNMENT        16

/*****************************************************************************/
/*                                                                           */
/* Makros                                                                    */
/*                                                                           */
/*****************************************************************************/
#define ALIGN_ADDRESS(addr, alignment) \
        ((((u32)(addr)) + (((u32)(alignment))-1)) & ~(((u32)(alignment)) - 1))


/*****************************************************************************/
/*                                                                           */
/*   ABR SAR Network operation Register                                      */
/*                                                                           */
/*****************************************************************************/

#define SAR_REG_DR0	(card->membase + 0x00)
#define SAR_REG_DR1	(card->membase + 0x04)
#define SAR_REG_DR2	(card->membase + 0x08)
#define SAR_REG_DR3	(card->membase + 0x0C)
#define SAR_REG_CMD	(card->membase + 0x10)
#define SAR_REG_CFG	(card->membase + 0x14)
#define SAR_REG_STAT	(card->membase + 0x18)
#define SAR_REG_RSQB	(card->membase + 0x1C)
#define SAR_REG_RSQT	(card->membase + 0x20)
#define SAR_REG_RSQH	(card->membase + 0x24)
#define SAR_REG_CDC	(card->membase + 0x28)
#define SAR_REG_VPEC	(card->membase + 0x2C)
#define SAR_REG_ICC	(card->membase + 0x30)
#define SAR_REG_RAWCT	(card->membase + 0x34)
#define SAR_REG_TMR	(card->membase + 0x38)
#define SAR_REG_TSTB	(card->membase + 0x3C)
#define SAR_REG_TSQB	(card->membase + 0x40)
#define SAR_REG_TSQT	(card->membase + 0x44)
#define SAR_REG_TSQH	(card->membase + 0x48)
#define SAR_REG_GP	(card->membase + 0x4C)
#define SAR_REG_VPM	(card->membase + 0x50)
#define SAR_REG_RXFD	(card->membase + 0x54)
#define SAR_REG_RXFT	(card->membase + 0x58)
#define SAR_REG_RXFH	(card->membase + 0x5C)
#define SAR_REG_RAWHND	(card->membase + 0x60)
#define SAR_REG_RXSTAT	(card->membase + 0x64)
#define SAR_REG_ABRSTD	(card->membase + 0x68)
#define SAR_REG_ABRRQ	(card->membase + 0x6C)
#define SAR_REG_VBRRQ	(card->membase + 0x70)
#define SAR_REG_RTBL	(card->membase + 0x74)
#define SAR_REG_MDFCT	(card->membase + 0x78)
#define SAR_REG_TXSTAT	(card->membase + 0x7C)
#define SAR_REG_TCMDQ	(card->membase + 0x80)
#define SAR_REG_IRCP	(card->membase + 0x84)
#define SAR_REG_FBQP0	(card->membase + 0x88)
#define SAR_REG_FBQP1	(card->membase + 0x8C)
#define SAR_REG_FBQP2	(card->membase + 0x90)
#define SAR_REG_FBQP3	(card->membase + 0x94)
#define SAR_REG_FBQS0	(card->membase + 0x98)
#define SAR_REG_FBQS1	(card->membase + 0x9C)
#define SAR_REG_FBQS2	(card->membase + 0xA0)
#define SAR_REG_FBQS3	(card->membase + 0xA4)
#define SAR_REG_FBQWP0	(card->membase + 0xA8)
#define SAR_REG_FBQWP1	(card->membase + 0xAC)
#define SAR_REG_FBQWP2	(card->membase + 0xB0)
#define SAR_REG_FBQWP3	(card->membase + 0xB4)
#define SAR_REG_NOW	(card->membase + 0xB8)


/*****************************************************************************/
/*                                                                           */
/*   Commands                                                                */
/*                                                                           */
/*****************************************************************************/

#define SAR_CMD_NO_OPERATION         0x00000000
#define SAR_CMD_OPENCLOSE_CONNECTION 0x20000000
#define SAR_CMD_WRITE_SRAM           0x40000000
#define SAR_CMD_READ_SRAM            0x50000000
#define SAR_CMD_READ_UTILITY         0x80000000
#define SAR_CMD_WRITE_UTILITY        0x90000000

#define SAR_CMD_OPEN_CONNECTION     (SAR_CMD_OPENCLOSE_CONNECTION | 0x00080000)
#define SAR_CMD_CLOSE_CONNECTION     SAR_CMD_OPENCLOSE_CONNECTION


/*****************************************************************************/
/*                                                                           */
/*   Configuration Register bits                                             */
/*                                                                           */
/*****************************************************************************/

#define SAR_CFG_SWRST          0x80000000  /* Software reset                 */
#define SAR_CFG_LOOP           0x40000000  /* Internal Loopback              */
#define SAR_CFG_RXPTH          0x20000000  /* Receive Path Enable            */
#define SAR_CFG_IDLE_CLP       0x10000000  /* SAR set CLP Bits of Null Cells */
#define SAR_CFG_TX_FIFO_SIZE_1 0x04000000  /* TX FIFO Size = 1 cell          */
#define SAR_CFG_TX_FIFO_SIZE_2 0x08000000  /* TX FIFO Size = 2 cells         */
#define SAR_CFG_TX_FIFO_SIZE_4 0x0C000000  /* TX FIFO Size = 4 cells         */
#define SAR_CFG_TX_FIFO_SIZE_9 0x00000000  /* TX FIFO Size = 9 cells (full)  */
#define SAR_CFG_NO_IDLE        0x02000000  /* SAR sends no Null Cells        */
#define SAR_CFG_RSVD1          0x01000000  /* Reserved                       */
#define SAR_CFG_RXSTQ_SIZE_2k  0x00000000  /* RX Stat Queue Size = 2048 byte */
#define SAR_CFG_RXSTQ_SIZE_4k  0x00400000  /* RX Stat Queue Size = 4096 byte */
#define SAR_CFG_RXSTQ_SIZE_8k  0x00800000  /* RX Stat Queue Size = 8192 byte */
#define SAR_CFG_RXSTQ_SIZE_R   0x00C00000  /* RX Stat Queue Size = reserved  */
#define SAR_CFG_ICAPT          0x00200000  /* accept Invalid Cells           */
#define SAR_CFG_IGGFC          0x00100000  /* Ignore GFC                     */
#define SAR_CFG_VPVCS_0        0x00000000  /* VPI/VCI Select bit range       */
#define SAR_CFG_VPVCS_1        0x00040000  /* VPI/VCI Select bit range       */
#define SAR_CFG_VPVCS_2        0x00080000  /* VPI/VCI Select bit range       */
#define SAR_CFG_VPVCS_8        0x000C0000  /* VPI/VCI Select bit range       */
#define SAR_CFG_CNTBL_1k       0x00000000  /* Connection Table Size          */
#define SAR_CFG_CNTBL_4k       0x00010000  /* Connection Table Size          */
#define SAR_CFG_CNTBL_16k      0x00020000  /* Connection Table Size          */
#define SAR_CFG_CNTBL_512      0x00030000  /* Connection Table Size          */
#define SAR_CFG_VPECA          0x00008000  /* VPI/VCI Error Cell Accept      */
#define SAR_CFG_RXINT_NOINT    0x00000000  /* No Interrupt on PDU received   */
#define SAR_CFG_RXINT_NODELAY  0x00001000  /* Interrupt without delay to host*/
#define SAR_CFG_RXINT_256US    0x00002000  /* Interrupt with delay 256 usec  */
#define SAR_CFG_RXINT_505US    0x00003000  /* Interrupt with delay 505 usec  */
#define SAR_CFG_RXINT_742US    0x00004000  /* Interrupt with delay 742 usec  */
#define SAR_CFG_RAWIE          0x00000800  /* Raw Cell Queue Interrupt Enable*/
#define SAR_CFG_RQFIE          0x00000400  /* RSQ Almost Full Int Enable     */
#define SAR_CFG_RSVD2          0x00000200  /* Reserved                       */
#define SAR_CFG_CACHE          0x00000100  /* DMA on Cache Line Boundary     */
#define SAR_CFG_TMOIE          0x00000080  /* Timer Roll Over Int Enable     */
#define SAR_CFG_FBIE           0x00000040  /* Free Buffer Queue Int Enable   */
#define SAR_CFG_TXEN           0x00000020  /* Transmit Operation Enable      */
#define SAR_CFG_TXINT          0x00000010  /* Transmit status Int Enable     */
#define SAR_CFG_TXUIE          0x00000008  /* Transmit underrun Int Enable   */
#define SAR_CFG_UMODE          0x00000004  /* Utopia Mode Select             */
#define SAR_CFG_TXSFI          0x00000002  /* Transmit status Full Int Enable*/
#define SAR_CFG_PHYIE          0x00000001  /* PHY Interrupt Enable           */

#define SAR_CFG_TX_FIFO_SIZE_MASK 0x0C000000  /* TX FIFO Size Mask           */
#define SAR_CFG_RXSTQSIZE_MASK 0x00C00000
#define SAR_CFG_CNTBL_MASK     0x00030000
#define SAR_CFG_RXINT_MASK     0x00007000


/*****************************************************************************/
/*                                                                           */
/*   Status Register bits                                                    */
/*                                                                           */
/*****************************************************************************/

#define SAR_STAT_FRAC_3     0xF0000000 /* Fraction of Free Buffer Queue 3 */
#define SAR_STAT_FRAC_2     0x0F000000 /* Fraction of Free Buffer Queue 2 */
#define SAR_STAT_FRAC_1     0x00F00000 /* Fraction of Free Buffer Queue 1 */
#define SAR_STAT_FRAC_0     0x000F0000 /* Fraction of Free Buffer Queue 0 */
#define SAR_STAT_TSIF       0x00008000 /* Transmit Status Indicator       */
#define SAR_STAT_TXICP      0x00004000 /* Transmit Status Indicator       */
#define SAR_STAT_RSVD1      0x00002000 /* Reserved                        */
#define SAR_STAT_TSQF       0x00001000 /* Transmit Status Queue full      */
#define SAR_STAT_TMROF      0x00000800 /* Timer overflow                  */
#define SAR_STAT_PHYI       0x00000400 /* PHY device Interrupt flag       */
#define SAR_STAT_CMDBZ      0x00000200 /* ABR SAR Comand Busy Flag        */
#define SAR_STAT_FBQ3A      0x00000100 /* Free Buffer Queue 3 Attention   */
#define SAR_STAT_FBQ2A      0x00000080 /* Free Buffer Queue 2 Attention   */
#define SAR_STAT_RSQF       0x00000040 /* Receive Status Queue full       */
#define SAR_STAT_EPDU       0x00000020 /* End Of PDU Flag                 */
#define SAR_STAT_RAWCF      0x00000010 /* Raw Cell Flag                   */ 
#define SAR_STAT_FBQ1A      0x00000008 /* Free Buffer Queue 1 Attention   */
#define SAR_STAT_FBQ0A      0x00000004 /* Free Buffer Queue 0 Attention   */
#define SAR_STAT_RSQAF      0x00000002 /* Receive Status Queue almost full*/  
#define SAR_STAT_RSVD2      0x00000001 /* Reserved                        */


/*****************************************************************************/
/*                                                                           */
/*   General Purpose Register bits                                           */
/*                                                                           */
/*****************************************************************************/

#define SAR_GP_TXNCC_MASK   0xff000000  /* Transmit Negative Credit Count   */
#define SAR_GP_EEDI         0x00010000  /* EEPROM Data In                   */
#define SAR_GP_BIGE         0x00008000  /* Big Endian Operation             */
#define SAR_GP_RM_NORMAL    0x00000000  /* Normal handling of RM cells      */
#define SAR_GP_RM_TO_RCQ    0x00002000  /* put RM cells into Raw Cell Queue */
#define SAR_GP_RM_RSVD      0x00004000  /* Reserved                         */
#define SAR_GP_RM_INHIBIT   0x00006000  /* Inhibit update of Connection tab */
#define SAR_GP_PHY_RESET    0x00000008  /* PHY Reset                        */
#define SAR_GP_EESCLK	    0x00000004	/* EEPROM SCLK			    */
#define SAR_GP_EECS	    0x00000002	/* EEPROM Chip Select		    */
#define SAR_GP_EEDO	    0x00000001	/* EEPROM Data Out		    */


/*****************************************************************************/
/*                                                                           */
/*   SAR local SRAM layout for 128k work SRAM                                */
/*                                                                           */
/*****************************************************************************/

#define SAR_SRAM_SCD_SIZE        12
#define SAR_SRAM_TCT_SIZE         8
#define SAR_SRAM_RCT_SIZE         4

#define SAR_SRAM_TCT_128_BASE    0x00000
#define SAR_SRAM_TCT_128_TOP     0x01fff
#define SAR_SRAM_RCT_128_BASE    0x02000
#define SAR_SRAM_RCT_128_TOP     0x02fff
#define SAR_SRAM_FB0_128_BASE    0x03000
#define SAR_SRAM_FB0_128_TOP     0x033ff
#define SAR_SRAM_FB1_128_BASE    0x03400
#define SAR_SRAM_FB1_128_TOP     0x037ff
#define SAR_SRAM_FB2_128_BASE    0x03800
#define SAR_SRAM_FB2_128_TOP     0x03bff
#define SAR_SRAM_FB3_128_BASE    0x03c00
#define SAR_SRAM_FB3_128_TOP     0x03fff
#define SAR_SRAM_SCD_128_BASE    0x04000
#define SAR_SRAM_SCD_128_TOP     0x07fff
#define SAR_SRAM_TST1_128_BASE   0x08000
#define SAR_SRAM_TST1_128_TOP    0x0bfff
#define SAR_SRAM_TST2_128_BASE   0x0c000
#define SAR_SRAM_TST2_128_TOP    0x0ffff
#define SAR_SRAM_ABRSTD_128_BASE 0x10000
#define SAR_SRAM_ABRSTD_128_TOP  0x13fff
#define SAR_SRAM_RT_128_BASE     0x14000
#define SAR_SRAM_RT_128_TOP      0x15fff

#define SAR_SRAM_FIFO_128_BASE   0x18000
#define SAR_SRAM_FIFO_128_TOP    0x1ffff


/*****************************************************************************/
/*                                                                           */
/*   SAR local SRAM layout for 32k work SRAM                                 */
/*                                                                           */
/*****************************************************************************/

#define SAR_SRAM_TCT_32_BASE     0x00000
#define SAR_SRAM_TCT_32_TOP      0x00fff
#define SAR_SRAM_RCT_32_BASE     0x01000
#define SAR_SRAM_RCT_32_TOP      0x017ff
#define SAR_SRAM_FB0_32_BASE     0x01800
#define SAR_SRAM_FB0_32_TOP      0x01bff
#define SAR_SRAM_FB1_32_BASE     0x01c00
#define SAR_SRAM_FB1_32_TOP      0x01fff
#define SAR_SRAM_FB2_32_BASE     0x02000
#define SAR_SRAM_FB2_32_TOP      0x023ff
#define SAR_SRAM_FB3_32_BASE     0x02400
#define SAR_SRAM_FB3_32_TOP      0x027ff
#define SAR_SRAM_SCD_32_BASE     0x02800
#define SAR_SRAM_SCD_32_TOP      0x03fff
#define SAR_SRAM_TST1_32_BASE    0x04000
#define SAR_SRAM_TST1_32_TOP     0x04fff
#define SAR_SRAM_TST2_32_BASE    0x05000
#define SAR_SRAM_TST2_32_TOP     0x05fff
#define SAR_SRAM_ABRSTD_32_BASE  0x06000
#define SAR_SRAM_ABRSTD_32_TOP   0x067ff
#define SAR_SRAM_RT_32_BASE      0x06800
#define SAR_SRAM_RT_32_TOP       0x06fff
#define SAR_SRAM_FIFO_32_BASE    0x07000
#define SAR_SRAM_FIFO_32_TOP     0x07fff


/*****************************************************************************/
/*                                                                           */
/*   TSR - Transmit Status Request                                           */
/*                                                                           */
/*****************************************************************************/

#define SAR_TSR_TYPE_TSR  0x80000000
#define SAR_TSR_TYPE_TBD  0x00000000
#define SAR_TSR_TSIF      0x20000000
#define SAR_TSR_TAG_MASK  0x01F00000


/*****************************************************************************/
/*                                                                           */
/*   TBD - Transmit Buffer Descriptor                                        */
/*                                                                           */
/*****************************************************************************/

#define SAR_TBD_EPDU      0x40000000
#define SAR_TBD_TSIF      0x20000000
#define SAR_TBD_OAM       0x10000000
#define SAR_TBD_AAL0      0x00000000
#define SAR_TBD_AAL34     0x04000000
#define SAR_TBD_AAL5      0x08000000
#define SAR_TBD_GTSI      0x02000000
#define SAR_TBD_TAG_MASK  0x01F00000

#define SAR_TBD_VPI_MASK  0x0FF00000
#define SAR_TBD_VCI_MASK  0x000FFFF0
#define SAR_TBD_VC_MASK   (SAR_TBD_VPI_MASK | SAR_TBD_VCI_MASK)

#define SAR_TBD_VPI_SHIFT 20
#define SAR_TBD_VCI_SHIFT 4


/*****************************************************************************/
/*                                                                           */
/*   RXFD - Receive FIFO Descriptor                                          */
/*                                                                           */
/*****************************************************************************/

#define SAR_RXFD_SIZE_MASK     0x0F000000
#define SAR_RXFD_SIZE_512      0x00000000  /* 512 words                      */
#define SAR_RXFD_SIZE_1K       0x01000000  /* 1k words                       */
#define SAR_RXFD_SIZE_2K       0x02000000  /* 2k words                       */
#define SAR_RXFD_SIZE_4K       0x03000000  /* 4k words                       */
#define SAR_RXFD_SIZE_8K       0x04000000  /* 8k words                       */
#define SAR_RXFD_SIZE_16K      0x05000000  /* 16k words                      */
#define SAR_RXFD_SIZE_32K      0x06000000  /* 32k words                      */
#define SAR_RXFD_SIZE_64K      0x07000000  /* 64k words                      */
#define SAR_RXFD_SIZE_128K     0x08000000  /* 128k words                     */
#define SAR_RXFD_SIZE_256K     0x09000000  /* 256k words                     */
#define SAR_RXFD_ADDR_MASK     0x001ffc00


/*****************************************************************************/
/*                                                                           */
/*   ABRSTD - ABR + VBR Schedule Tables                                      */
/*                                                                           */
/*****************************************************************************/

#define SAR_ABRSTD_SIZE_MASK   0x07000000
#define SAR_ABRSTD_SIZE_512    0x00000000  /* 512 words                      */
#define SAR_ABRSTD_SIZE_1K     0x01000000  /* 1k words                       */
#define SAR_ABRSTD_SIZE_2K     0x02000000  /* 2k words                       */
#define SAR_ABRSTD_SIZE_4K     0x03000000  /* 4k words                       */
#define SAR_ABRSTD_SIZE_8K     0x04000000  /* 8k words                       */
#define SAR_ABRSTD_SIZE_16K    0x05000000  /* 16k words                      */
#define SAR_ABRSTD_ADDR_MASK   0x001ffc00


/*****************************************************************************/
/*                                                                           */
/*   RCTE - Receive Connection Table Entry                                   */
/*                                                                           */
/*****************************************************************************/

#define SAR_RCTE_IL_MASK       0xE0000000  /* inactivity limit               */
#define SAR_RCTE_IC_MASK       0x1C000000  /* inactivity count               */
#define SAR_RCTE_RSVD          0x02000000  /* reserved                       */
#define SAR_RCTE_LCD           0x01000000  /* last cell data                 */
#define SAR_RCTE_CI_VC         0x00800000  /* EFCI in previous cell of VC    */
#define SAR_RCTE_FBP_01        0x00000000  /* 1. cell->FBQ0, others->FBQ1    */
#define SAR_RCTE_FBP_1         0x00200000  /* use FBQ 1 for all cells        */
#define SAR_RCTE_FBP_2         0x00400000  /* use FBQ 2 for all cells        */
#define SAR_RCTE_FBP_3         0x00600000  /* use FBQ 3 for all cells        */
#define SAR_RCTE_NZ_GFC        0x00100000  /* non zero GFC in all cell of VC */
#define SAR_RCTE_CONNECTOPEN   0x00080000  /* VC is open                     */
#define SAR_RCTE_AAL_MASK      0x00070000  /* mask for AAL type field s.b.   */
#define SAR_RCTE_RAWCELLINTEN  0x00008000  /* raw cell interrupt enable      */
#define SAR_RCTE_RXCONCELLADDR 0x00004000  /* RX constant cell address       */
#define SAR_RCTE_BUFFSTAT_MASK 0x00003000  /* buffer status                  */
#define SAR_RCTE_EFCI          0x00000800  /* EFCI Congestion flag           */
#define SAR_RCTE_CLP           0x00000400  /* Cell Loss Priority flag        */
#define SAR_RCTE_CRC           0x00000200  /* Recieved CRC Error             */
#define SAR_RCTE_CELLCNT_MASK  0x000001FF  /* cell Count                     */

#define SAR_RCTE_AAL0          0x00000000  /* AAL types for ALL field        */
#define SAR_RCTE_AAL34         0x00010000
#define SAR_RCTE_AAL5          0x00020000
#define SAR_RCTE_RCQ           0x00030000
#define SAR_RCTE_OAM           0x00040000

#define TCMDQ_START		0x01000000
#define TCMDQ_LACR		0x02000000
#define TCMDQ_START_LACR	0x03000000
#define TCMDQ_INIT_ER		0x04000000
#define TCMDQ_HALT		0x05000000


struct idt77252_skb_prv {
	struct scqe	tbd;	/* Transmit Buffer Descriptor */
	dma_addr_t	paddr;	/* DMA handle */
	u32		pool;	/* sb_pool handle */
};

#define IDT77252_PRV_TBD(skb)	\
	(((struct idt77252_skb_prv *)(ATM_SKB(skb)+1))->tbd)
#define IDT77252_PRV_PADDR(skb)	\
	(((struct idt77252_skb_prv *)(ATM_SKB(skb)+1))->paddr)
#define IDT77252_PRV_POOL(skb)	\
	(((struct idt77252_skb_prv *)(ATM_SKB(skb)+1))->pool)

/*****************************************************************************/
/*                                                                           */
/*   PCI related items                                                       */
/*                                                                           */
/*****************************************************************************/

#ifndef PCI_VENDOR_ID_IDT
#define PCI_VENDOR_ID_IDT 0x111D
#endif /* PCI_VENDOR_ID_IDT */

#ifndef PCI_DEVICE_ID_IDT_IDT77252
#define PCI_DEVICE_ID_IDT_IDT77252 0x0003
#endif /* PCI_DEVICE_ID_IDT_IDT772052 */


#endif /* !(_IDT77252_H) */
