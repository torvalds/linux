#include <linux/version.h>

#ifndef _SK_MCA_INCLUDE_
#define _SK_MCA_INCLUDE_

#ifdef _SK_MCA_DRIVER_

/* Adapter ID's */
#define SKNET_MCA_ID 0x6afd
#define SKNET_JUNIOR_MCA_ID 0x6be9

/* media enumeration - defined in a way that it fits onto the MC2+'s
   POS registers... */

typedef enum { Media_10Base2, Media_10BaseT,
	Media_10Base5, Media_Unknown, Media_Count
} skmca_medium;

/* private structure */
typedef struct {
	unsigned int slot;	/* MCA-Slot-#                       */
	void __iomem *base;
	void __iomem *macbase;	/* base address of MAC address PROM */
	void __iomem *ioregaddr;/* address of I/O-register (Lo)     */
	void __iomem *ctrladdr;	/* address of control/stat register */
	void __iomem *cmdaddr;	/* address of I/O-command register  */
	int nextrx;		/* index of next RX descriptor to
				   be read                          */
	int nexttxput;		/* index of next free TX descriptor */
	int nexttxdone;		/* index of next TX descriptor to 
				   be finished                      */
	int txbusy;		/* # of busy TX descriptors         */
	struct net_device_stats stat;	/* packet statistics            */
	int realirq;		/* memorizes actual IRQ, even when 
				   currently not allocated          */
	skmca_medium medium;	/* physical cannector               */
	spinlock_t lock;
} skmca_priv;

/* card registers: control/status register bits */

#define CTRL_ADR_DATA      0	/* Bit 0 = 0 ->access data register  */
#define CTRL_ADR_RAP       1	/* Bit 0 = 1 ->access RAP register   */
#define CTRL_RW_WRITE      0	/* Bit 1 = 0 ->write register        */
#define CTRL_RW_READ       2	/* Bit 1 = 1 ->read register         */
#define CTRL_RESET_ON      0	/* Bit 3 = 0 ->reset board           */
#define CTRL_RESET_OFF     8	/* Bit 3 = 1 ->no reset of board     */

#define STAT_ADR_DATA      0	/* Bit 0 of ctrl register read back  */
#define STAT_ADR_RAP       1
#define STAT_RW_WRITE      0	/* Bit 1 of ctrl register read back  */
#define STAT_RW_READ       2
#define STAT_RESET_ON      0	/* Bit 3 of ctrl register read back  */
#define STAT_RESET_OFF     8
#define STAT_IRQ_ACT       0	/* interrupt pending                 */
#define STAT_IRQ_NOACT     16	/* no interrupt pending              */
#define STAT_IO_NOBUSY     0	/* no transfer busy                  */
#define STAT_IO_BUSY       32	/* transfer busy                     */

/* I/O command register bits */

#define IOCMD_GO           128	/* Bit 7 = 1 -> start register xfer  */

/* LANCE registers */

#define LANCE_CSR0         0	/* Status/Control                    */

#define CSR0_ERR           0x8000	/* general error flag                */
#define CSR0_BABL          0x4000	/* transmitter timeout               */
#define CSR0_CERR          0x2000	/* collision error                   */
#define CSR0_MISS          0x1000	/* lost Rx block                     */
#define CSR0_MERR          0x0800	/* memory access error               */
#define CSR0_RINT          0x0400	/* receiver interrupt                */
#define CSR0_TINT          0x0200	/* transmitter interrupt             */
#define CSR0_IDON          0x0100	/* initialization done               */
#define CSR0_INTR          0x0080	/* general interrupt flag            */
#define CSR0_INEA          0x0040	/* interrupt enable                  */
#define CSR0_RXON          0x0020	/* receiver enabled                  */
#define CSR0_TXON          0x0010	/* transmitter enabled               */
#define CSR0_TDMD          0x0008	/* force transmission now            */
#define CSR0_STOP          0x0004	/* stop LANCE                        */
#define CSR0_STRT          0x0002	/* start LANCE                       */
#define CSR0_INIT          0x0001	/* read initialization block         */

#define LANCE_CSR1         1	/* addr bit 0..15 of initialization  */
#define LANCE_CSR2         2	/*          16..23 block             */

#define LANCE_CSR3         3	/* Bus control                       */
#define CSR3_BCON_HOLD     0	/* Bit 0 = 0 -> BM1,BM0,HOLD         */
#define CSR3_BCON_BUSRQ    1	/* Bit 0 = 1 -> BUSAK0,BYTE,BUSRQ    */
#define CSR3_ALE_HIGH      0	/* Bit 1 = 0 -> ALE asserted high    */
#define CSR3_ALE_LOW       2	/* Bit 1 = 1 -> ALE asserted low     */
#define CSR3_BSWAP_OFF     0	/* Bit 2 = 0 -> no byte swap         */
#define CSR3_BSWAP_ON      4	/* Bit 2 = 1 -> byte swap            */

/* LANCE structures */

typedef struct {		/* LANCE initialization block        */
	u16 Mode;		/* mode flags                        */
	u8 PAdr[6];		/* MAC address                       */
	u8 LAdrF[8];		/* Multicast filter                  */
	u32 RdrP;		/* Receive descriptor                */
	u32 TdrP;		/* Transmit descriptor               */
} LANCE_InitBlock;

/* Mode flags init block */

#define LANCE_INIT_PROM    0x8000	/* enable promiscous mode            */
#define LANCE_INIT_INTL    0x0040	/* internal loopback                 */
#define LANCE_INIT_DRTY    0x0020	/* disable retry                     */
#define LANCE_INIT_COLL    0x0010	/* force collision                   */
#define LANCE_INIT_DTCR    0x0008	/* disable transmit CRC              */
#define LANCE_INIT_LOOP    0x0004	/* loopback                          */
#define LANCE_INIT_DTX     0x0002	/* disable transmitter               */
#define LANCE_INIT_DRX     0x0001	/* disable receiver                  */

typedef struct {		/* LANCE Tx descriptor               */
	u16 LowAddr;		/* bit 0..15 of address              */
	u16 Flags;		/* bit 16..23 of address + Flags     */
	u16 Len;		/* 2s complement of packet length    */
	u16 Status;		/* Result of transmission            */
} LANCE_TxDescr;

#define TXDSCR_FLAGS_OWN   0x8000	/* LANCE owns descriptor             */
#define TXDSCR_FLAGS_ERR   0x4000	/* summary error flag                */
#define TXDSCR_FLAGS_MORE  0x1000	/* more than one retry needed?       */
#define TXDSCR_FLAGS_ONE   0x0800	/* one retry?                        */
#define TXDSCR_FLAGS_DEF   0x0400	/* transmission deferred?            */
#define TXDSCR_FLAGS_STP   0x0200	/* first packet in chain?            */
#define TXDSCR_FLAGS_ENP   0x0100	/* last packet in chain?             */

#define TXDSCR_STATUS_BUFF 0x8000	/* buffer error?                     */
#define TXDSCR_STATUS_UFLO 0x4000	/* silo underflow during transmit?   */
#define TXDSCR_STATUS_LCOL 0x1000	/* late collision?                   */
#define TXDSCR_STATUS_LCAR 0x0800	/* loss of carrier?                  */
#define TXDSCR_STATUS_RTRY 0x0400	/* retry error?                      */

typedef struct {		/* LANCE Rx descriptor               */
	u16 LowAddr;		/* bit 0..15 of address              */
	u16 Flags;		/* bit 16..23 of address + Flags     */
	u16 MaxLen;		/* 2s complement of buffer length    */
	u16 Len;		/* packet length                     */
} LANCE_RxDescr;

#define RXDSCR_FLAGS_OWN   0x8000	/* LANCE owns descriptor             */
#define RXDSCR_FLAGS_ERR   0x4000	/* summary error flag                */
#define RXDSCR_FLAGS_FRAM  0x2000	/* framing error flag                */
#define RXDSCR_FLAGS_OFLO  0x1000	/* FIFO overflow?                    */
#define RXDSCR_FLAGS_CRC   0x0800	/* CRC error?                        */
#define RXDSCR_FLAGS_BUFF  0x0400	/* buffer error?                     */
#define RXDSCR_FLAGS_STP   0x0200	/* first packet in chain?            */
#define RXDCSR_FLAGS_ENP   0x0100	/* last packet in chain?             */

/* RAM layout */

#define TXCOUNT            4	/* length of TX descriptor queue     */
#define LTXCOUNT           2	/* log2 of it                        */
#define RXCOUNT            4	/* length of RX descriptor queue     */
#define LRXCOUNT           2	/* log2 of it                        */

#define RAM_INITBASE       0	/* LANCE init block                  */
#define RAM_TXBASE         24	/* Start of TX descriptor queue      */
#define RAM_RXBASE         \
(RAM_TXBASE + (TXCOUNT * 8))	/* Start of RX descriptor queue      */
#define RAM_DATABASE       \
(RAM_RXBASE + (RXCOUNT * 8))	/* Start of data area for frames     */
#define RAM_BUFSIZE        1580	/* max. frame size - should never be
				   reached                           */

#endif				/* _SK_MCA_DRIVER_ */

#endif	/* _SK_MCA_INCLUDE_ */
