#ifndef _IBM_LANA_INCLUDE_
#define _IBM_LANA_INCLUDE_

#ifdef _IBM_LANA_DRIVER_

/* maximum packet size */

#define PKTSIZE 1524

/* number of transmit buffers */

#define TXBUFCNT 4

/* Adapter ID's */
#define IBM_LANA_ID 0xffe0

/* media enumeration - defined in a way that it fits onto the LAN/A's
   POS registers... */

typedef enum {
	Media_10BaseT, Media_10Base5,
	Media_Unknown, Media_10Base2, Media_Count
} ibmlana_medium;

/* private structure */

typedef struct {
	unsigned int slot;		/* MCA-Slot-#                       */
	int realirq;			/* memorizes actual IRQ, even when
					   currently not allocated          */
	ibmlana_medium medium;		/* physical cannector               */
	u32 	tdastart, txbufstart,	/* addresses                        */
		rrastart, rxbufstart, rdastart, rxbufcnt, txusedcnt;
	int 	nextrxdescr,		/* next rx descriptor to be used    */
		lastrxdescr,		/* last free rx descriptor          */
		nexttxdescr,		/* last tx descriptor to be used    */
		currtxdescr,		/* tx descriptor currently tx'ed    */
		txused[TXBUFCNT];	/* busy flags                       */
	void __iomem *base;
	spinlock_t lock;
} ibmlana_priv;

/* this card uses quite a lot of I/O ports...luckily the MCA bus decodes
   a full 64K I/O range... */

#define IBM_LANA_IORANGE 0xa0

/* Command Register: */

#define SONIC_CMDREG     0x00
#define CMDREG_HTX       0x0001	/* halt transmission                */
#define CMDREG_TXP       0x0002	/* start transmission               */
#define CMDREG_RXDIS     0x0004	/* disable receiver                 */
#define CMDREG_RXEN      0x0008	/* enable receiver                  */
#define CMDREG_STP       0x0010	/* stop timer                       */
#define CMDREG_ST        0x0020	/* start timer                      */
#define CMDREG_RST       0x0080	/* software reset                   */
#define CMDREG_RRRA      0x0100	/* force SONIC to read first RRA    */
#define CMDREG_LCAM      0x0200	/* force SONIC to read CAM descrs   */

/* Data Configuration Register */

#define SONIC_DCREG      0x02
#define DCREG_EXBUS      0x8000	/* Extended Bus Mode                */
#define DCREG_LBR        0x2000	/* Latched Bus Retry                */
#define DCREG_PO1        0x1000	/* Programmable Outputs             */
#define DCREG_PO0        0x0800
#define DCREG_SBUS       0x0400	/* Synchronous Bus Mode             */
#define DCREG_USR1       0x0200	/* User Definable Pins              */
#define DCREG_USR0       0x0100
#define DCREG_WC0        0x0000	/* 0..3 Wait States                 */
#define DCREG_WC1        0x0040
#define DCREG_WC2        0x0080
#define DCREG_WC3        0x00c0
#define DCREG_DW16       0x0000	/* 16 bit Bus Mode                  */
#define DCREG_DW32       0x0020	/* 32 bit Bus Mode                  */
#define DCREG_BMS        0x0010	/* Block Mode Select                */
#define DCREG_RFT4       0x0000	/* 4/8/16/24 bytes RX  Threshold    */
#define DCREG_RFT8       0x0004
#define DCREG_RFT16      0x0008
#define DCREG_RFT24      0x000c
#define DCREG_TFT8       0x0000	/* 8/16/24/28 bytes TX Threshold    */
#define DCREG_TFT16      0x0001
#define DCREG_TFT24      0x0002
#define DCREG_TFT28      0x0003

/* Receive Control Register */

#define SONIC_RCREG      0x04
#define RCREG_ERR        0x8000	/* accept damaged and collided pkts */
#define RCREG_RNT        0x4000	/* accept packets that are < 64     */
#define RCREG_BRD        0x2000	/* accept broadcasts                */
#define RCREG_PRO        0x1000	/* promiscous mode                  */
#define RCREG_AMC        0x0800	/* accept all multicasts            */
#define RCREG_LB_NONE    0x0000	/* no loopback                      */
#define RCREG_LB_MAC     0x0200	/* MAC loopback                     */
#define RCREG_LB_ENDEC   0x0400	/* ENDEC loopback                   */
#define RCREG_LB_XVR     0x0600	/* Transceiver loopback             */
#define RCREG_MC         0x0100	/* Multicast received               */
#define RCREG_BC         0x0080	/* Broadcast received               */
#define RCREG_LPKT       0x0040	/* last packet in RBA               */
#define RCREG_CRS        0x0020	/* carrier sense present            */
#define RCREG_COL        0x0010	/* recv'd packet with collision     */
#define RCREG_CRCR       0x0008	/* recv'd packet with CRC error     */
#define RCREG_FAER       0x0004	/* recv'd packet with inv. framing  */
#define RCREG_LBK        0x0002	/* recv'd loopback packet           */
#define RCREG_PRX        0x0001	/* recv'd packet is OK              */

/* Transmit Control Register */

#define SONIC_TCREG      0x06
#define TCREG_PINT       0x8000	/* generate interrupt after TDA read */
#define TCREG_POWC       0x4000	/* timer start out of window detect */
#define TCREG_CRCI       0x2000	/* inhibit CRC generation           */
#define TCREG_EXDIS      0x1000	/* disable excessive deferral timer */
#define TCREG_EXD        0x0400	/* excessive deferral occurred       */
#define TCREG_DEF        0x0200	/* single deferral occurred          */
#define TCREG_NCRS       0x0100	/* no carrier detected              */
#define TCREG_CRSL       0x0080	/* carrier lost                     */
#define TCREG_EXC        0x0040	/* excessive collisions occurred     */
#define TCREG_OWC        0x0020	/* out of window collision occurred  */
#define TCREG_PMB        0x0008	/* packet monitored bad             */
#define TCREG_FU         0x0004	/* FIFO underrun                    */
#define TCREG_BCM        0x0002	/* byte count mismatch of fragments */
#define TCREG_PTX        0x0001	/* packet transmitted OK            */

/* Interrupt Mask Register */

#define SONIC_IMREG      0x08
#define IMREG_BREN       0x4000	/* interrupt when bus retry occurred */
#define IMREG_HBLEN      0x2000	/* interrupt when heartbeat lost    */
#define IMREG_LCDEN      0x1000	/* interrupt when CAM loaded        */
#define IMREG_PINTEN     0x0800	/* interrupt when PINT in TDA set   */
#define IMREG_PRXEN      0x0400	/* interrupt when packet received   */
#define IMREG_PTXEN      0x0200	/* interrupt when packet was sent   */
#define IMREG_TXEREN     0x0100	/* interrupt when send failed       */
#define IMREG_TCEN       0x0080	/* interrupt when timer completed   */
#define IMREG_RDEEN      0x0040	/* interrupt when RDA exhausted     */
#define IMREG_RBEEN      0x0020	/* interrupt when RBA exhausted     */
#define IMREG_RBAEEN     0x0010	/* interrupt when RBA too short     */
#define IMREG_CRCEN      0x0008	/* interrupt when CRC counter rolls */
#define IMREG_FAEEN      0x0004	/* interrupt when FAE counter rolls */
#define IMREG_MPEN       0x0002	/* interrupt when MP counter rolls  */
#define IMREG_RFOEN      0x0001	/* interrupt when Rx FIFO overflows */

/* Interrupt Status Register */

#define SONIC_ISREG      0x0a
#define ISREG_BR         0x4000	/* bus retry occurred                */
#define ISREG_HBL        0x2000	/* heartbeat lost                   */
#define ISREG_LCD        0x1000	/* CAM loaded                       */
#define ISREG_PINT       0x0800	/* PINT in TDA set                  */
#define ISREG_PKTRX      0x0400	/* packet received                  */
#define ISREG_TXDN       0x0200	/* packet was sent                  */
#define ISREG_TXER       0x0100	/* send failed                      */
#define ISREG_TC         0x0080	/* timer completed                  */
#define ISREG_RDE        0x0040	/* RDA exhausted                    */
#define ISREG_RBE        0x0020	/* RBA exhausted                    */
#define ISREG_RBAE       0x0010	/* RBA too short for received frame */
#define ISREG_CRC        0x0008	/* CRC counter rolls over           */
#define ISREG_FAE        0x0004	/* FAE counter rolls over           */
#define ISREG_MP         0x0002	/* MP counter rolls  over           */
#define ISREG_RFO        0x0001	/* Rx FIFO overflows                */

#define SONIC_UTDA       0x0c	/* current transmit descr address   */
#define SONIC_CTDA       0x0e

#define SONIC_URDA       0x1a	/* current receive descr address    */
#define SONIC_CRDA       0x1c

#define SONIC_CRBA0      0x1e	/* current receive buffer address   */
#define SONIC_CRBA1      0x20

#define SONIC_RBWC0      0x22	/* word count in receive buffer     */
#define SONIC_RBWC1      0x24

#define SONIC_EOBC       0x26	/* minimum space to be free in RBA  */

#define SONIC_URRA       0x28	/* upper address of CDA & Recv Area */

#define SONIC_RSA        0x2a	/* start of receive resource area   */

#define SONIC_REA        0x2c	/* end of receive resource area     */

#define SONIC_RRP        0x2e	/* resource read pointer            */

#define SONIC_RWP        0x30	/* resource write pointer           */

#define SONIC_CAMEPTR    0x42	/* CAM entry pointer                */

#define SONIC_CAMADDR2   0x44	/* CAM address ports                */
#define SONIC_CAMADDR1   0x46
#define SONIC_CAMADDR0   0x48

#define SONIC_CAMPTR     0x4c	/* lower address of CDA             */

#define SONIC_CAMCNT     0x4e	/* # of CAM descriptors to load     */

/* Data Configuration Register 2    */

#define SONIC_DCREG2     0x7e
#define DCREG2_EXPO3     0x8000	/* extended programmable outputs    */
#define DCREG2_EXPO2     0x4000
#define DCREG2_EXPO1     0x2000
#define DCREG2_EXPO0     0x1000
#define DCREG2_HD        0x0800	/* heartbeat disable                */
#define DCREG2_JD        0x0200	/* jabber timer disable             */
#define DCREG2_AUTO      0x0100	/* enable AUI/TP auto selection     */
#define DCREG2_XWRAP     0x0040	/* TP transceiver loopback          */
#define DCREG2_PH        0x0010	/* HOLD request timing              */
#define DCREG2_PCM       0x0004	/* packet compress when matched     */
#define DCREG2_PCNM      0x0002	/* packet compress when not matched */
#define DCREG2_RJCM      0x0001	/* inverse packet match via CAM     */

/* Board Control Register: Enable RAM, Interrupts... */

#define BCMREG           0x80
#define BCMREG_RAMEN     0x80	/* switch over to RAM               */
#define BCMREG_IPEND     0x40	/* interrupt pending ?              */
#define BCMREG_RESET     0x08	/* reset board                      */
#define BCMREG_16BIT     0x04	/* adapter in 16-bit slot           */
#define BCMREG_RAMWIN    0x02	/* enable RAM window                */
#define BCMREG_IEN       0x01	/* interrupt enable                 */

/* MAC Address PROM */

#define MACADDRPROM      0x92

/* structure of a CAM entry */

typedef struct {
	u32 index;		/* pointer into CAM area            */
	u32 addr0;		/* address part (bits 0..15 used)   */
	u32 addr1;
	u32 addr2;
} camentry_t;

/* structure of a receive resource */

typedef struct {
	u32 startlo;		/* start address (bits 0..15 used)  */
	u32 starthi;
	u32 cntlo;		/* size in 16-bit quantities        */
	u32 cnthi;
} rra_t;

/* structure of a receive descriptor */

typedef struct {
	u32 status;		/* packet status                    */
	u32 length;		/* length in bytes                  */
	u32 startlo;		/* start address                    */
	u32 starthi;
	u32 seqno;		/* frame sequence                   */
	u32 link;		/* pointer to next descriptor       */
	/* bit 0 = EOL                      */
	u32 inuse;		/* !=0 --> free for SONIC to write  */
} rda_t;

/* structure of a transmit descriptor */

typedef struct {
	u32 status;		/* transmit status                  */
	u32 config;		/* value for TCR                    */
	u32 length;		/* total length                     */
	u32 fragcount;		/* number of fragments              */
	u32 startlo;		/* start address of fragment        */
	u32 starthi;
	u32 fraglength;		/* length of this fragment          */
	/* more address/length triplets may */
	/* follow here                      */
	u32 link;		/* pointer to next descriptor       */
	/* bit 0 = EOL                      */
} tda_t;

#endif				/* _IBM_LANA_DRIVER_ */

#endif	/* _IBM_LANA_INCLUDE_ */
