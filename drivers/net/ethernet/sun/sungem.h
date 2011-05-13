/* $Id: sungem.h,v 1.10.2.4 2002/03/11 08:54:48 davem Exp $
 * sungem.h: Definitions for Sun GEM ethernet driver.
 *
 * Copyright (C) 2000 David S. Miller (davem@redhat.com)
 */

#ifndef _SUNGEM_H
#define _SUNGEM_H

/* Global Registers */
#define GREG_SEBSTATE	0x0000UL	/* SEB State Register		*/
#define GREG_CFG	0x0004UL	/* Configuration Register	*/
#define GREG_STAT	0x000CUL	/* Status Register		*/
#define GREG_IMASK	0x0010UL	/* Interrupt Mask Register	*/
#define GREG_IACK	0x0014UL	/* Interrupt ACK Register	*/
#define GREG_STAT2	0x001CUL	/* Alias of GREG_STAT		*/
#define GREG_PCIESTAT	0x1000UL	/* PCI Error Status Register	*/
#define GREG_PCIEMASK	0x1004UL	/* PCI Error Mask Register	*/
#define GREG_BIFCFG	0x1008UL	/* BIF Configuration Register	*/
#define GREG_BIFDIAG	0x100CUL	/* BIF Diagnostics Register	*/
#define GREG_SWRST	0x1010UL	/* Software Reset Register	*/

/* Global SEB State Register */
#define GREG_SEBSTATE_ARB	0x00000003	/* State of Arbiter		*/
#define GREG_SEBSTATE_RXWON	0x00000004	/* RX won internal arbitration	*/

/* Global Configuration Register */
#define GREG_CFG_IBURST		0x00000001	/* Infinite Burst		*/
#define GREG_CFG_TXDMALIM	0x0000003e	/* TX DMA grant limit		*/
#define GREG_CFG_RXDMALIM	0x000007c0	/* RX DMA grant limit		*/
#define GREG_CFG_RONPAULBIT	0x00000800	/* Use mem read multiple for PCI read
						 * after infinite burst (Apple) */
#define GREG_CFG_ENBUG2FIX	0x00001000	/* Fix Rx hang after overflow */

/* Global Interrupt Status Register.
 *
 * Reading this register automatically clears bits 0 through 6.
 * This auto-clearing does not occur when the alias at GREG_STAT2
 * is read instead.  The rest of the interrupt bits only clear when
 * the secondary interrupt status register corresponding to that
 * bit is read (ie. if GREG_STAT_PCS is set, it will be cleared by
 * reading PCS_ISTAT).
 */
#define GREG_STAT_TXINTME	0x00000001	/* TX INTME frame transferred	*/
#define GREG_STAT_TXALL		0x00000002	/* All TX frames transferred	*/
#define GREG_STAT_TXDONE	0x00000004	/* One TX frame transferred	*/
#define GREG_STAT_RXDONE	0x00000010	/* One RX frame arrived		*/
#define GREG_STAT_RXNOBUF	0x00000020	/* No free RX buffers available	*/
#define GREG_STAT_RXTAGERR	0x00000040	/* RX tag framing is corrupt	*/
#define GREG_STAT_PCS		0x00002000	/* PCS signalled interrupt	*/
#define GREG_STAT_TXMAC		0x00004000	/* TX MAC signalled interrupt	*/
#define GREG_STAT_RXMAC		0x00008000	/* RX MAC signalled interrupt	*/
#define GREG_STAT_MAC		0x00010000	/* MAC Control signalled irq	*/
#define GREG_STAT_MIF		0x00020000	/* MIF signalled interrupt	*/
#define GREG_STAT_PCIERR	0x00040000	/* PCI Error interrupt		*/
#define GREG_STAT_TXNR		0xfff80000	/* == TXDMA_TXDONE reg val	*/
#define GREG_STAT_TXNR_SHIFT	19

#define GREG_STAT_ABNORMAL	(GREG_STAT_RXNOBUF | GREG_STAT_RXTAGERR | \
				 GREG_STAT_PCS | GREG_STAT_TXMAC | GREG_STAT_RXMAC | \
				 GREG_STAT_MAC | GREG_STAT_MIF | GREG_STAT_PCIERR)

#define GREG_STAT_NAPI		(GREG_STAT_TXALL  | GREG_STAT_TXINTME | \
				 GREG_STAT_RXDONE | GREG_STAT_ABNORMAL)

/* The layout of GREG_IMASK and GREG_IACK is identical to GREG_STAT.
 * Bits set in GREG_IMASK will prevent that interrupt type from being
 * signalled to the cpu.  GREG_IACK can be used to clear specific top-level
 * interrupt conditions in GREG_STAT, ie. it only works for bits 0 through 6.
 * Setting the bit will clear that interrupt, clear bits will have no effect
 * on GREG_STAT.
 */

/* Global PCI Error Status Register */
#define GREG_PCIESTAT_BADACK	0x00000001	/* No ACK64# during ABS64 cycle	*/
#define GREG_PCIESTAT_DTRTO	0x00000002	/* Delayed transaction timeout	*/
#define GREG_PCIESTAT_OTHER	0x00000004	/* Other PCI error, check cfg space */

/* The layout of the GREG_PCIEMASK is identical to that of GREG_PCIESTAT.
 * Bits set in GREG_PCIEMASK will prevent that interrupt type from being
 * signalled to the cpu.
 */

/* Global BIF Configuration Register */
#define GREG_BIFCFG_SLOWCLK	0x00000001	/* Set if PCI runs < 25Mhz	*/
#define GREG_BIFCFG_B64DIS	0x00000002	/* Disable 64bit wide data cycle*/
#define GREG_BIFCFG_M66EN	0x00000004	/* Set if on 66Mhz PCI segment	*/

/* Global BIF Diagnostics Register */
#define GREG_BIFDIAG_BURSTSM	0x007f0000	/* PCI Burst state machine	*/
#define GREG_BIFDIAG_BIFSM	0xff000000	/* BIF state machine		*/

/* Global Software Reset Register.
 *
 * This register is used to perform a global reset of the RX and TX portions
 * of the GEM asic.  Setting the RX or TX reset bit will start the reset.
 * The driver _MUST_ poll these bits until they clear.  One may not attempt
 * to program any other part of GEM until the bits clear.
 */
#define GREG_SWRST_TXRST	0x00000001	/* TX Software Reset		*/
#define GREG_SWRST_RXRST	0x00000002	/* RX Software Reset		*/
#define GREG_SWRST_RSTOUT	0x00000004	/* Force RST# pin active	*/
#define GREG_SWRST_CACHESIZE	0x00ff0000	/* RIO only: cache line size	*/
#define GREG_SWRST_CACHE_SHIFT	16

/* TX DMA Registers */
#define TXDMA_KICK	0x2000UL	/* TX Kick Register		*/
#define TXDMA_CFG	0x2004UL	/* TX Configuration Register	*/
#define TXDMA_DBLOW	0x2008UL	/* TX Desc. Base Low		*/
#define TXDMA_DBHI	0x200CUL	/* TX Desc. Base High		*/
#define TXDMA_FWPTR	0x2014UL	/* TX FIFO Write Pointer	*/
#define TXDMA_FSWPTR	0x2018UL	/* TX FIFO Shadow Write Pointer	*/
#define TXDMA_FRPTR	0x201CUL	/* TX FIFO Read Pointer		*/
#define TXDMA_FSRPTR	0x2020UL	/* TX FIFO Shadow Read Pointer	*/
#define TXDMA_PCNT	0x2024UL	/* TX FIFO Packet Counter	*/
#define TXDMA_SMACHINE	0x2028UL	/* TX State Machine Register	*/
#define TXDMA_DPLOW	0x2030UL	/* TX Data Pointer Low		*/
#define TXDMA_DPHI	0x2034UL	/* TX Data Pointer High		*/
#define TXDMA_TXDONE	0x2100UL	/* TX Completion Register	*/
#define TXDMA_FADDR	0x2104UL	/* TX FIFO Address		*/
#define TXDMA_FTAG	0x2108UL	/* TX FIFO Tag			*/
#define TXDMA_DLOW	0x210CUL	/* TX FIFO Data Low		*/
#define TXDMA_DHIT1	0x2110UL	/* TX FIFO Data HighT1		*/
#define TXDMA_DHIT0	0x2114UL	/* TX FIFO Data HighT0		*/
#define TXDMA_FSZ	0x2118UL	/* TX FIFO Size			*/

/* TX Kick Register.
 *
 * This 13-bit register is programmed by the driver to hold the descriptor
 * entry index which follows the last valid transmit descriptor.
 */

/* TX Completion Register.
 *
 * This 13-bit register is updated by GEM to hold to descriptor entry index
 * which follows the last descriptor already processed by GEM.  Note that
 * this value is mirrored in GREG_STAT which eliminates the need to even
 * access this register in the driver during interrupt processing.
 */

/* TX Configuration Register.
 *
 * Note that TXDMA_CFG_FTHRESH, the TX FIFO Threshold, is an obsolete feature
 * that was meant to be used with jumbo packets.  It should be set to the
 * maximum value of 0x4ff, else one risks getting TX MAC Underrun errors.
 */
#define TXDMA_CFG_ENABLE	0x00000001	/* Enable TX DMA channel	*/
#define TXDMA_CFG_RINGSZ	0x0000001e	/* TX descriptor ring size	*/
#define TXDMA_CFG_RINGSZ_32	0x00000000	/* 32 TX descriptors		*/
#define TXDMA_CFG_RINGSZ_64	0x00000002	/* 64 TX descriptors		*/
#define TXDMA_CFG_RINGSZ_128	0x00000004	/* 128 TX descriptors		*/
#define TXDMA_CFG_RINGSZ_256	0x00000006	/* 256 TX descriptors		*/
#define TXDMA_CFG_RINGSZ_512	0x00000008	/* 512 TX descriptors		*/
#define TXDMA_CFG_RINGSZ_1K	0x0000000a	/* 1024 TX descriptors		*/
#define TXDMA_CFG_RINGSZ_2K	0x0000000c	/* 2048 TX descriptors		*/
#define TXDMA_CFG_RINGSZ_4K	0x0000000e	/* 4096 TX descriptors		*/
#define TXDMA_CFG_RINGSZ_8K	0x00000010	/* 8192 TX descriptors		*/
#define TXDMA_CFG_PIOSEL	0x00000020	/* Enable TX FIFO PIO from cpu	*/
#define TXDMA_CFG_FTHRESH	0x001ffc00	/* TX FIFO Threshold, obsolete	*/
#define TXDMA_CFG_PMODE		0x00200000	/* TXALL irq means TX FIFO empty*/

/* TX Descriptor Base Low/High.
 *
 * These two registers store the 53 most significant bits of the base address
 * of the TX descriptor table.  The 11 least significant bits are always
 * zero.  As a result, the TX descriptor table must be 2K aligned.
 */

/* The rest of the TXDMA_* registers are for diagnostics and debug, I will document
 * them later. -DaveM
 */

/* WakeOnLan Registers	*/
#define WOL_MATCH0	0x3000UL
#define WOL_MATCH1	0x3004UL
#define WOL_MATCH2	0x3008UL
#define WOL_MCOUNT	0x300CUL
#define WOL_WAKECSR	0x3010UL

/* WOL Match count register
 */
#define WOL_MCOUNT_N		0x00000010
#define WOL_MCOUNT_M		0x00000000 /* 0 << 8 */

#define WOL_WAKECSR_ENABLE	0x00000001
#define WOL_WAKECSR_MII		0x00000002
#define WOL_WAKECSR_SEEN	0x00000004
#define WOL_WAKECSR_FILT_UCAST	0x00000008
#define WOL_WAKECSR_FILT_MCAST	0x00000010
#define WOL_WAKECSR_FILT_BCAST	0x00000020
#define WOL_WAKECSR_FILT_SEEN	0x00000040


/* Receive DMA Registers */
#define RXDMA_CFG	0x4000UL	/* RX Configuration Register	*/
#define RXDMA_DBLOW	0x4004UL	/* RX Descriptor Base Low	*/
#define RXDMA_DBHI	0x4008UL	/* RX Descriptor Base High	*/
#define RXDMA_FWPTR	0x400CUL	/* RX FIFO Write Pointer	*/
#define RXDMA_FSWPTR	0x4010UL	/* RX FIFO Shadow Write Pointer	*/
#define RXDMA_FRPTR	0x4014UL	/* RX FIFO Read Pointer		*/
#define RXDMA_PCNT	0x4018UL	/* RX FIFO Packet Counter	*/
#define RXDMA_SMACHINE	0x401CUL	/* RX State Machine Register	*/
#define RXDMA_PTHRESH	0x4020UL	/* Pause Thresholds		*/
#define RXDMA_DPLOW	0x4024UL	/* RX Data Pointer Low		*/
#define RXDMA_DPHI	0x4028UL	/* RX Data Pointer High		*/
#define RXDMA_KICK	0x4100UL	/* RX Kick Register		*/
#define RXDMA_DONE	0x4104UL	/* RX Completion Register	*/
#define RXDMA_BLANK	0x4108UL	/* RX Blanking Register		*/
#define RXDMA_FADDR	0x410CUL	/* RX FIFO Address		*/
#define RXDMA_FTAG	0x4110UL	/* RX FIFO Tag			*/
#define RXDMA_DLOW	0x4114UL	/* RX FIFO Data Low		*/
#define RXDMA_DHIT1	0x4118UL	/* RX FIFO Data HighT0		*/
#define RXDMA_DHIT0	0x411CUL	/* RX FIFO Data HighT1		*/
#define RXDMA_FSZ	0x4120UL	/* RX FIFO Size			*/

/* RX Configuration Register. */
#define RXDMA_CFG_ENABLE	0x00000001	/* Enable RX DMA channel	*/
#define RXDMA_CFG_RINGSZ	0x0000001e	/* RX descriptor ring size	*/
#define RXDMA_CFG_RINGSZ_32	0x00000000	/* - 32   entries		*/
#define RXDMA_CFG_RINGSZ_64	0x00000002	/* - 64   entries		*/
#define RXDMA_CFG_RINGSZ_128	0x00000004	/* - 128  entries		*/
#define RXDMA_CFG_RINGSZ_256	0x00000006	/* - 256  entries		*/
#define RXDMA_CFG_RINGSZ_512	0x00000008	/* - 512  entries		*/
#define RXDMA_CFG_RINGSZ_1K	0x0000000a	/* - 1024 entries		*/
#define RXDMA_CFG_RINGSZ_2K	0x0000000c	/* - 2048 entries		*/
#define RXDMA_CFG_RINGSZ_4K	0x0000000e	/* - 4096 entries		*/
#define RXDMA_CFG_RINGSZ_8K	0x00000010	/* - 8192 entries		*/
#define RXDMA_CFG_RINGSZ_BDISAB	0x00000020	/* Disable RX desc batching	*/
#define RXDMA_CFG_FBOFF		0x00001c00	/* Offset of first data byte	*/
#define RXDMA_CFG_CSUMOFF	0x000fe000	/* Skip bytes before csum calc	*/
#define RXDMA_CFG_FTHRESH	0x07000000	/* RX FIFO dma start threshold	*/
#define RXDMA_CFG_FTHRESH_64	0x00000000	/* - 64   bytes			*/
#define RXDMA_CFG_FTHRESH_128	0x01000000	/* - 128  bytes			*/
#define RXDMA_CFG_FTHRESH_256	0x02000000	/* - 256  bytes			*/
#define RXDMA_CFG_FTHRESH_512	0x03000000	/* - 512  bytes			*/
#define RXDMA_CFG_FTHRESH_1K	0x04000000	/* - 1024 bytes			*/
#define RXDMA_CFG_FTHRESH_2K	0x05000000	/* - 2048 bytes			*/

/* RX Descriptor Base Low/High.
 *
 * These two registers store the 53 most significant bits of the base address
 * of the RX descriptor table.  The 11 least significant bits are always
 * zero.  As a result, the RX descriptor table must be 2K aligned.
 */

/* RX PAUSE Thresholds.
 *
 * These values determine when XOFF and XON PAUSE frames are emitted by
 * GEM.  The thresholds measure RX FIFO occupancy in units of 64 bytes.
 */
#define RXDMA_PTHRESH_OFF	0x000001ff	/* XOFF emitted w/FIFO > this	*/
#define RXDMA_PTHRESH_ON	0x001ff000	/* XON emitted w/FIFO < this	*/

/* RX Kick Register.
 *
 * This 13-bit register is written by the host CPU and holds the last
 * valid RX descriptor number plus one.  This is, if 'N' is written to
 * this register, it means that all RX descriptors up to but excluding
 * 'N' are valid.
 *
 * The hardware requires that RX descriptors are posted in increments
 * of 4.  This means 'N' must be a multiple of four.  For the best
 * performance, the first new descriptor being posted should be (PCI)
 * cache line aligned.
 */

/* RX Completion Register.
 *
 * This 13-bit register is updated by GEM to indicate which RX descriptors
 * have already been used for receive frames.  All descriptors up to but
 * excluding the value in this register are ready to be processed.  GEM
 * updates this register value after the RX FIFO empties completely into
 * the RX descriptor's buffer, but before the RX_DONE bit is set in the
 * interrupt status register.
 */

/* RX Blanking Register. */
#define RXDMA_BLANK_IPKTS	0x000001ff	/* RX_DONE asserted after this
						 * many packets received since
						 * previous RX_DONE.
						 */
#define RXDMA_BLANK_ITIME	0x000ff000	/* RX_DONE asserted after this
						 * many clocks (measured in 2048
						 * PCI clocks) were counted since
						 * the previous RX_DONE.
						 */

/* RX FIFO Size.
 *
 * This 11-bit read-only register indicates how large, in units of 64-bytes,
 * the RX FIFO is.  The driver uses this to properly configure the RX PAUSE
 * thresholds.
 */

/* The rest of the RXDMA_* registers are for diagnostics and debug, I will document
 * them later. -DaveM
 */

/* MAC Registers */
#define MAC_TXRST	0x6000UL	/* TX MAC Software Reset Command*/
#define MAC_RXRST	0x6004UL	/* RX MAC Software Reset Command*/
#define MAC_SNDPAUSE	0x6008UL	/* Send Pause Command Register	*/
#define MAC_TXSTAT	0x6010UL	/* TX MAC Status Register	*/
#define MAC_RXSTAT	0x6014UL	/* RX MAC Status Register	*/
#define MAC_CSTAT	0x6018UL	/* MAC Control Status Register	*/
#define MAC_TXMASK	0x6020UL	/* TX MAC Mask Register		*/
#define MAC_RXMASK	0x6024UL	/* RX MAC Mask Register		*/
#define MAC_MCMASK	0x6028UL	/* MAC Control Mask Register	*/
#define MAC_TXCFG	0x6030UL	/* TX MAC Configuration Register*/
#define MAC_RXCFG	0x6034UL	/* RX MAC Configuration Register*/
#define MAC_MCCFG	0x6038UL	/* MAC Control Config Register	*/
#define MAC_XIFCFG	0x603CUL	/* XIF Configuration Register	*/
#define MAC_IPG0	0x6040UL	/* InterPacketGap0 Register	*/
#define MAC_IPG1	0x6044UL	/* InterPacketGap1 Register	*/
#define MAC_IPG2	0x6048UL	/* InterPacketGap2 Register	*/
#define MAC_STIME	0x604CUL	/* SlotTime Register		*/
#define MAC_MINFSZ	0x6050UL	/* MinFrameSize Register	*/
#define MAC_MAXFSZ	0x6054UL	/* MaxFrameSize Register	*/
#define MAC_PASIZE	0x6058UL	/* PA Size Register		*/
#define MAC_JAMSIZE	0x605CUL	/* JamSize Register		*/
#define MAC_ATTLIM	0x6060UL	/* Attempt Limit Register	*/
#define MAC_MCTYPE	0x6064UL	/* MAC Control Type Register	*/
#define MAC_ADDR0	0x6080UL	/* MAC Address 0 Register	*/
#define MAC_ADDR1	0x6084UL	/* MAC Address 1 Register	*/
#define MAC_ADDR2	0x6088UL	/* MAC Address 2 Register	*/
#define MAC_ADDR3	0x608CUL	/* MAC Address 3 Register	*/
#define MAC_ADDR4	0x6090UL	/* MAC Address 4 Register	*/
#define MAC_ADDR5	0x6094UL	/* MAC Address 5 Register	*/
#define MAC_ADDR6	0x6098UL	/* MAC Address 6 Register	*/
#define MAC_ADDR7	0x609CUL	/* MAC Address 7 Register	*/
#define MAC_ADDR8	0x60A0UL	/* MAC Address 8 Register	*/
#define MAC_AFILT0	0x60A4UL	/* Address Filter 0 Register	*/
#define MAC_AFILT1	0x60A8UL	/* Address Filter 1 Register	*/
#define MAC_AFILT2	0x60ACUL	/* Address Filter 2 Register	*/
#define MAC_AF21MSK	0x60B0UL	/* Address Filter 2&1 Mask Reg	*/
#define MAC_AF0MSK	0x60B4UL	/* Address Filter 0 Mask Reg	*/
#define MAC_HASH0	0x60C0UL	/* Hash Table 0 Register	*/
#define MAC_HASH1	0x60C4UL	/* Hash Table 1 Register	*/
#define MAC_HASH2	0x60C8UL	/* Hash Table 2 Register	*/
#define MAC_HASH3	0x60CCUL	/* Hash Table 3 Register	*/
#define MAC_HASH4	0x60D0UL	/* Hash Table 4 Register	*/
#define MAC_HASH5	0x60D4UL	/* Hash Table 5 Register	*/
#define MAC_HASH6	0x60D8UL	/* Hash Table 6 Register	*/
#define MAC_HASH7	0x60DCUL	/* Hash Table 7 Register	*/
#define MAC_HASH8	0x60E0UL	/* Hash Table 8 Register	*/
#define MAC_HASH9	0x60E4UL	/* Hash Table 9 Register	*/
#define MAC_HASH10	0x60E8UL	/* Hash Table 10 Register	*/
#define MAC_HASH11	0x60ECUL	/* Hash Table 11 Register	*/
#define MAC_HASH12	0x60F0UL	/* Hash Table 12 Register	*/
#define MAC_HASH13	0x60F4UL	/* Hash Table 13 Register	*/
#define MAC_HASH14	0x60F8UL	/* Hash Table 14 Register	*/
#define MAC_HASH15	0x60FCUL	/* Hash Table 15 Register	*/
#define MAC_NCOLL	0x6100UL	/* Normal Collision Counter	*/
#define MAC_FASUCC	0x6104UL	/* First Attmpt. Succ Coll Ctr.	*/
#define MAC_ECOLL	0x6108UL	/* Excessive Collision Counter	*/
#define MAC_LCOLL	0x610CUL	/* Late Collision Counter	*/
#define MAC_DTIMER	0x6110UL	/* Defer Timer			*/
#define MAC_PATMPS	0x6114UL	/* Peak Attempts Register	*/
#define MAC_RFCTR	0x6118UL	/* Receive Frame Counter	*/
#define MAC_LERR	0x611CUL	/* Length Error Counter		*/
#define MAC_AERR	0x6120UL	/* Alignment Error Counter	*/
#define MAC_FCSERR	0x6124UL	/* FCS Error Counter		*/
#define MAC_RXCVERR	0x6128UL	/* RX code Violation Error Ctr	*/
#define MAC_RANDSEED	0x6130UL	/* Random Number Seed Register	*/
#define MAC_SMACHINE	0x6134UL	/* State Machine Register	*/

/* TX MAC Software Reset Command. */
#define MAC_TXRST_CMD	0x00000001	/* Start sw reset, self-clears	*/

/* RX MAC Software Reset Command. */
#define MAC_RXRST_CMD	0x00000001	/* Start sw reset, self-clears	*/

/* Send Pause Command. */
#define MAC_SNDPAUSE_TS	0x0000ffff	/* The pause_time operand used in
					 * Send_Pause and flow-control
					 * handshakes.
					 */
#define MAC_SNDPAUSE_SP	0x00010000	/* Setting this bit instructs the MAC
					 * to send a Pause Flow Control
					 * frame onto the network.
					 */

/* TX MAC Status Register. */
#define MAC_TXSTAT_XMIT	0x00000001	/* Frame Transmitted		*/
#define MAC_TXSTAT_URUN	0x00000002	/* TX Underrun			*/
#define MAC_TXSTAT_MPE	0x00000004	/* Max Packet Size Error	*/
#define MAC_TXSTAT_NCE	0x00000008	/* Normal Collision Cntr Expire	*/
#define MAC_TXSTAT_ECE	0x00000010	/* Excess Collision Cntr Expire	*/
#define MAC_TXSTAT_LCE	0x00000020	/* Late Collision Cntr Expire	*/
#define MAC_TXSTAT_FCE	0x00000040	/* First Collision Cntr Expire	*/
#define MAC_TXSTAT_DTE	0x00000080	/* Defer Timer Expire		*/
#define MAC_TXSTAT_PCE	0x00000100	/* Peak Attempts Cntr Expire	*/

/* RX MAC Status Register. */
#define MAC_RXSTAT_RCV	0x00000001	/* Frame Received		*/
#define MAC_RXSTAT_OFLW	0x00000002	/* Receive Overflow		*/
#define MAC_RXSTAT_FCE	0x00000004	/* Frame Cntr Expire		*/
#define MAC_RXSTAT_ACE	0x00000008	/* Align Error Cntr Expire	*/
#define MAC_RXSTAT_CCE	0x00000010	/* CRC Error Cntr Expire	*/
#define MAC_RXSTAT_LCE	0x00000020	/* Length Error Cntr Expire	*/
#define MAC_RXSTAT_VCE	0x00000040	/* Code Violation Cntr Expire	*/

/* MAC Control Status Register. */
#define MAC_CSTAT_PRCV	0x00000001	/* Pause Received		*/
#define MAC_CSTAT_PS	0x00000002	/* Paused State			*/
#define MAC_CSTAT_NPS	0x00000004	/* Not Paused State		*/
#define MAC_CSTAT_PTR	0xffff0000	/* Pause Time Received		*/

/* The layout of the MAC_{TX,RX,C}MASK registers is identical to that
 * of MAC_{TX,RX,C}STAT.  Bits set in MAC_{TX,RX,C}MASK will prevent
 * that interrupt type from being signalled to front end of GEM.  For
 * the interrupt to actually get sent to the cpu, it is necessary to
 * properly set the appropriate GREG_IMASK_{TX,RX,}MAC bits as well.
 */

/* TX MAC Configuration Register.
 *
 * NOTE: The TX MAC Enable bit must be cleared and polled until
 *	 zero before any other bits in this register are changed.
 *
 *	 Also, enabling the Carrier Extension feature of GEM is
 *	 a 3 step process 1) Set TX Carrier Extension 2) Set
 *	 RX Carrier Extension 3) Set Slot Time to 0x200.  This
 *	 mode must be enabled when in half-duplex at 1Gbps, else
 *	 it must be disabled.
 */
#define MAC_TXCFG_ENAB	0x00000001	/* TX MAC Enable		*/
#define MAC_TXCFG_ICS	0x00000002	/* Ignore Carrier Sense		*/
#define MAC_TXCFG_ICOLL	0x00000004	/* Ignore Collisions		*/
#define MAC_TXCFG_EIPG0	0x00000008	/* Enable IPG0			*/
#define MAC_TXCFG_NGU	0x00000010	/* Never Give Up		*/
#define MAC_TXCFG_NGUL	0x00000020	/* Never Give Up Limit		*/
#define MAC_TXCFG_NBO	0x00000040	/* No Backoff			*/
#define MAC_TXCFG_SD	0x00000080	/* Slow Down			*/
#define MAC_TXCFG_NFCS	0x00000100	/* No FCS			*/
#define MAC_TXCFG_TCE	0x00000200	/* TX Carrier Extension		*/

/* RX MAC Configuration Register.
 *
 * NOTE: The RX MAC Enable bit must be cleared and polled until
 *	 zero before any other bits in this register are changed.
 *
 *	 Similar rules apply to the Hash Filter Enable bit when
 *	 programming the hash table registers, and the Address Filter
 *	 Enable bit when programming the address filter registers.
 */
#define MAC_RXCFG_ENAB	0x00000001	/* RX MAC Enable		*/
#define MAC_RXCFG_SPAD	0x00000002	/* Strip Pad			*/
#define MAC_RXCFG_SFCS	0x00000004	/* Strip FCS			*/
#define MAC_RXCFG_PROM	0x00000008	/* Promiscuous Mode		*/
#define MAC_RXCFG_PGRP	0x00000010	/* Promiscuous Group		*/
#define MAC_RXCFG_HFE	0x00000020	/* Hash Filter Enable		*/
#define MAC_RXCFG_AFE	0x00000040	/* Address Filter Enable	*/
#define MAC_RXCFG_DDE	0x00000080	/* Disable Discard on Error	*/
#define MAC_RXCFG_RCE	0x00000100	/* RX Carrier Extension		*/

/* MAC Control Config Register. */
#define MAC_MCCFG_SPE	0x00000001	/* Send Pause Enable		*/
#define MAC_MCCFG_RPE	0x00000002	/* Receive Pause Enable		*/
#define MAC_MCCFG_PMC	0x00000004	/* Pass MAC Control		*/

/* XIF Configuration Register.
 *
 * NOTE: When leaving or entering loopback mode, a global hardware
 *       init of GEM should be performed.
 */
#define MAC_XIFCFG_OE	0x00000001	/* MII TX Output Driver Enable	*/
#define MAC_XIFCFG_LBCK	0x00000002	/* Loopback TX to RX		*/
#define MAC_XIFCFG_DISE	0x00000004	/* Disable RX path during TX	*/
#define MAC_XIFCFG_GMII	0x00000008	/* Use GMII clocks + datapath	*/
#define MAC_XIFCFG_MBOE	0x00000010	/* Controls MII_BUF_EN pin	*/
#define MAC_XIFCFG_LLED	0x00000020	/* Force LINKLED# active (low)	*/
#define MAC_XIFCFG_FLED	0x00000040	/* Force FDPLXLED# active (low)	*/

/* InterPacketGap0 Register.  This 8-bit value is used as an extension
 * to the InterPacketGap1 Register.  Specifically it contributes to the
 * timing of the RX-to-TX IPG.  This value is ignored and presumed to
 * be zero for TX-to-TX IPG calculations and/or when the Enable IPG0 bit
 * is cleared in the TX MAC Configuration Register.
 *
 * This value in this register in terms of media byte time.
 *
 * Recommended value: 0x00
 */

/* InterPacketGap1 Register.  This 8-bit value defines the first 2/3
 * portion of the Inter Packet Gap.
 *
 * This value in this register in terms of media byte time.
 *
 * Recommended value: 0x08
 */

/* InterPacketGap2 Register.  This 8-bit value defines the second 1/3
 * portion of the Inter Packet Gap.
 *
 * This value in this register in terms of media byte time.
 *
 * Recommended value: 0x04
 */

/* Slot Time Register.  This 10-bit value specifies the slot time
 * parameter in units of media byte time.  It determines the physical
 * span of the network.
 *
 * Recommended value: 0x40
 */

/* Minimum Frame Size Register.  This 10-bit register specifies the
 * smallest sized frame the TXMAC will send onto the medium, and the
 * RXMAC will receive from the medium.
 *
 * Recommended value: 0x40
 */

/* Maximum Frame and Burst Size Register.
 *
 * This register specifies two things.  First it specifies the maximum
 * sized frame the TXMAC will send and the RXMAC will recognize as
 * valid.  Second, it specifies the maximum run length of a burst of
 * packets sent in half-duplex gigabit modes.
 *
 * Recommended value: 0x200005ee
 */
#define MAC_MAXFSZ_MFS	0x00007fff	/* Max Frame Size		*/
#define MAC_MAXFSZ_MBS	0x7fff0000	/* Max Burst Size		*/

/* PA Size Register.  This 10-bit register specifies the number of preamble
 * bytes which will be transmitted at the beginning of each frame.  A
 * value of two or greater should be programmed here.
 *
 * Recommended value: 0x07
 */

/* Jam Size Register.  This 4-bit register specifies the duration of
 * the jam in units of media byte time.
 *
 * Recommended value: 0x04
 */

/* Attempts Limit Register.  This 8-bit register specifies the number
 * of attempts that the TXMAC will make to transmit a frame, before it
 * resets its Attempts Counter.  After reaching the Attempts Limit the
 * TXMAC may or may not drop the frame, as determined by the NGU
 * (Never Give Up) and NGUL (Never Give Up Limit) bits in the TXMAC
 * Configuration Register.
 *
 * Recommended value: 0x10
 */

/* MAX Control Type Register.  This 16-bit register specifies the
 * "type" field of a MAC Control frame.  The TXMAC uses this field to
 * encapsulate the MAC Control frame for transmission, and the RXMAC
 * uses it for decoding valid MAC Control frames received from the
 * network.
 *
 * Recommended value: 0x8808
 */

/* MAC Address Registers.  Each of these registers specify the
 * ethernet MAC of the interface, 16-bits at a time.  Register
 * 0 specifies bits [47:32], register 1 bits [31:16], and register
 * 2 bits [15:0].
 *
 * Registers 3 through and including 5 specify an alternate
 * MAC address for the interface.
 *
 * Registers 6 through and including 8 specify the MAC Control
 * Address, which must be the reserved multicast address for MAC
 * Control frames.
 *
 * Example: To program primary station address a:b:c:d:e:f into
 *	    the chip.
 *		MAC_Address_2 = (a << 8) | b
 *		MAC_Address_1 = (c << 8) | d
 *		MAC_Address_0 = (e << 8) | f
 */

/* Address Filter Registers.  Registers 0 through 2 specify bit
 * fields [47:32] through [15:0], respectively, of the address
 * filter.  The Address Filter 2&1 Mask Register denotes the 8-bit
 * nibble mask for Address Filter Registers 2 and 1.  The Address
 * Filter 0 Mask Register denotes the 16-bit mask for the Address
 * Filter Register 0.
 */

/* Hash Table Registers.  Registers 0 through 15 specify bit fields
 * [255:240] through [15:0], respectively, of the hash table.
 */

/* Statistics Registers.  All of these registers are 16-bits and
 * track occurrences of a specific event.  GEM can be configured
 * to interrupt the host cpu when any of these counters overflow.
 * They should all be explicitly initialized to zero when the interface
 * is brought up.
 */

/* Random Number Seed Register.  This 10-bit value is used as the
 * RNG seed inside GEM for the CSMA/CD backoff algorithm.  It is
 * recommended to program this register to the 10 LSB of the
 * interfaces MAC address.
 */

/* Pause Timer, read-only.  This 16-bit timer is used to time the pause
 * interval as indicated by a received pause flow control frame.
 * A non-zero value in this timer indicates that the MAC is currently in
 * the paused state.
 */

/* MIF Registers */
#define MIF_BBCLK	0x6200UL	/* MIF Bit-Bang Clock		*/
#define MIF_BBDATA	0x6204UL	/* MIF Bit-Band Data		*/
#define MIF_BBOENAB	0x6208UL	/* MIF Bit-Bang Output Enable	*/
#define MIF_FRAME	0x620CUL	/* MIF Frame/Output Register	*/
#define MIF_CFG		0x6210UL	/* MIF Configuration Register	*/
#define MIF_MASK	0x6214UL	/* MIF Mask Register		*/
#define MIF_STATUS	0x6218UL	/* MIF Status Register		*/
#define MIF_SMACHINE	0x621CUL	/* MIF State Machine Register	*/

/* MIF Bit-Bang Clock.  This 1-bit register is used to generate the
 * MDC clock waveform on the MII Management Interface when the MIF is
 * programmed in the "Bit-Bang" mode.  Writing a '1' after a '0' into
 * this register will create a rising edge on the MDC, while writing
 * a '0' after a '1' will create a falling edge.  For every bit that
 * is transferred on the management interface, both edges have to be
 * generated.
 */

/* MIF Bit-Bang Data.  This 1-bit register is used to generate the
 * outgoing data (MDO) on the MII Management Interface when the MIF
 * is programmed in the "Bit-Bang" mode.  The daa will be steered to the
 * appropriate MDIO based on the state of the PHY_Select bit in the MIF
 * Configuration Register.
 */

/* MIF Big-Band Output Enable.  THis 1-bit register is used to enable
 * ('1') or disable ('0') the I-directional driver on the MII when the
 * MIF is programmed in the "Bit-Bang" mode.  The MDIO should be enabled
 * when data bits are transferred from the MIF to the transceiver, and it
 * should be disabled when the interface is idle or when data bits are
 * transferred from the transceiver to the MIF (data portion of a read
 * instruction).  Only one MDIO will be enabled at a given time, depending
 * on the state of the PHY_Select bit in the MIF Configuration Register.
 */

/* MIF Configuration Register.  This 15-bit register controls the operation
 * of the MIF.
 */
#define MIF_CFG_PSELECT	0x00000001	/* Xcvr slct: 0=mdio0 1=mdio1	*/
#define MIF_CFG_POLL	0x00000002	/* Enable polling mechanism	*/
#define MIF_CFG_BBMODE	0x00000004	/* 1=bit-bang 0=frame mode	*/
#define MIF_CFG_PRADDR	0x000000f8	/* Xcvr poll register address	*/
#define MIF_CFG_MDI0	0x00000100	/* MDIO_0 present or read-bit	*/
#define MIF_CFG_MDI1	0x00000200	/* MDIO_1 present or read-bit	*/
#define MIF_CFG_PPADDR	0x00007c00	/* Xcvr poll PHY address	*/

/* MIF Frame/Output Register.  This 32-bit register allows the host to
 * communicate with a transceiver in frame mode (as opposed to big-bang
 * mode).  Writes by the host specify an instrution.  After being issued
 * the host must poll this register for completion.  Also, after
 * completion this register holds the data returned by the transceiver
 * if applicable.
 */
#define MIF_FRAME_ST	0xc0000000	/* STart of frame		*/
#define MIF_FRAME_OP	0x30000000	/* OPcode			*/
#define MIF_FRAME_PHYAD	0x0f800000	/* PHY ADdress			*/
#define MIF_FRAME_REGAD	0x007c0000	/* REGister ADdress		*/
#define MIF_FRAME_TAMSB	0x00020000	/* Turn Around MSB		*/
#define MIF_FRAME_TALSB	0x00010000	/* Turn Around LSB		*/
#define MIF_FRAME_DATA	0x0000ffff	/* Instruction Payload		*/

/* MIF Status Register.  This register reports status when the MIF is
 * operating in the poll mode.  The poll status field is auto-clearing
 * on read.
 */
#define MIF_STATUS_DATA	0xffff0000	/* Live image of XCVR reg	*/
#define MIF_STATUS_STAT	0x0000ffff	/* Which bits have changed	*/

/* MIF Mask Register.  This 16-bit register is used when in poll mode
 * to say which bits of the polled register will cause an interrupt
 * when changed.
 */

/* PCS/Serialink Registers */
#define PCS_MIICTRL	0x9000UL	/* PCS MII Control Register	*/
#define PCS_MIISTAT	0x9004UL	/* PCS MII Status Register	*/
#define PCS_MIIADV	0x9008UL	/* PCS MII Advertisement Reg	*/
#define PCS_MIILP	0x900CUL	/* PCS MII Link Partner Ability	*/
#define PCS_CFG		0x9010UL	/* PCS Configuration Register	*/
#define PCS_SMACHINE	0x9014UL	/* PCS State Machine Register	*/
#define PCS_ISTAT	0x9018UL	/* PCS Interrupt Status Reg	*/
#define PCS_DMODE	0x9050UL	/* Datapath Mode Register	*/
#define PCS_SCTRL	0x9054UL	/* Serialink Control Register	*/
#define PCS_SOS		0x9058UL	/* Shared Output Select Reg	*/
#define PCS_SSTATE	0x905CUL	/* Serialink State Register	*/

/* PCD MII Control Register. */
#define PCS_MIICTRL_SPD	0x00000040	/* Read as one, writes ignored	*/
#define PCS_MIICTRL_CT	0x00000080	/* Force COL signal active	*/
#define PCS_MIICTRL_DM	0x00000100	/* Duplex mode, forced low	*/
#define PCS_MIICTRL_RAN	0x00000200	/* Restart auto-neg, self clear	*/
#define PCS_MIICTRL_ISO	0x00000400	/* Read as zero, writes ignored	*/
#define PCS_MIICTRL_PD	0x00000800	/* Read as zero, writes ignored	*/
#define PCS_MIICTRL_ANE	0x00001000	/* Auto-neg enable		*/
#define PCS_MIICTRL_SS	0x00002000	/* Read as zero, writes ignored	*/
#define PCS_MIICTRL_WB	0x00004000	/* Wrapback, loopback at 10-bit
					 * input side of Serialink
					 */
#define PCS_MIICTRL_RST	0x00008000	/* Resets PCS, self clearing	*/

/* PCS MII Status Register. */
#define PCS_MIISTAT_EC	0x00000001	/* Ext Capability: Read as zero	*/
#define PCS_MIISTAT_JD	0x00000002	/* Jabber Detect: Read as zero	*/
#define PCS_MIISTAT_LS	0x00000004	/* Link Status: 1=up 0=down	*/
#define PCS_MIISTAT_ANA	0x00000008	/* Auto-neg Ability, always 1	*/
#define PCS_MIISTAT_RF	0x00000010	/* Remote Fault			*/
#define PCS_MIISTAT_ANC	0x00000020	/* Auto-neg complete		*/
#define PCS_MIISTAT_ES	0x00000100	/* Extended Status, always 1	*/

/* PCS MII Advertisement Register. */
#define PCS_MIIADV_FD	0x00000020	/* Advertise Full Duplex	*/
#define PCS_MIIADV_HD	0x00000040	/* Advertise Half Duplex	*/
#define PCS_MIIADV_SP	0x00000080	/* Advertise Symmetric Pause	*/
#define PCS_MIIADV_AP	0x00000100	/* Advertise Asymmetric Pause	*/
#define PCS_MIIADV_RF	0x00003000	/* Remote Fault			*/
#define PCS_MIIADV_ACK	0x00004000	/* Read-only			*/
#define PCS_MIIADV_NP	0x00008000	/* Next-page, forced low	*/

/* PCS MII Link Partner Ability Register.   This register is equivalent
 * to the Link Partnet Ability Register of the standard MII register set.
 * It's layout corresponds to the PCS MII Advertisement Register.
 */

/* PCS Configuration Register. */
#define PCS_CFG_ENABLE	0x00000001	/* Must be zero while changing
					 * PCS MII advertisement reg.
					 */
#define PCS_CFG_SDO	0x00000002	/* Signal detect override	*/
#define PCS_CFG_SDL	0x00000004	/* Signal detect active low	*/
#define PCS_CFG_JS	0x00000018	/* Jitter-study:
					 * 0 = normal operation
					 * 1 = high-frequency test pattern
					 * 2 = low-frequency test pattern
					 * 3 = reserved
					 */
#define PCS_CFG_TO	0x00000020	/* 10ms auto-neg timer override	*/

/* PCS Interrupt Status Register.  This register is self-clearing
 * when read.
 */
#define PCS_ISTAT_LSC	0x00000004	/* Link Status Change		*/

/* Datapath Mode Register. */
#define PCS_DMODE_SM	0x00000001	/* 1 = use internal Serialink	*/
#define PCS_DMODE_ESM	0x00000002	/* External SERDES mode		*/
#define PCS_DMODE_MGM	0x00000004	/* MII/GMII mode		*/
#define PCS_DMODE_GMOE	0x00000008	/* GMII Output Enable		*/

/* Serialink Control Register.
 *
 * NOTE: When in SERDES mode, the loopback bit has inverse logic.
 */
#define PCS_SCTRL_LOOP	0x00000001	/* Loopback enable		*/
#define PCS_SCTRL_ESCD	0x00000002	/* Enable sync char detection	*/
#define PCS_SCTRL_LOCK	0x00000004	/* Lock to reference clock	*/
#define PCS_SCTRL_EMP	0x00000018	/* Output driver emphasis	*/
#define PCS_SCTRL_STEST	0x000001c0	/* Self test patterns		*/
#define PCS_SCTRL_PDWN	0x00000200	/* Software power-down		*/
#define PCS_SCTRL_RXZ	0x00000c00	/* PLL input to Serialink	*/
#define PCS_SCTRL_RXP	0x00003000	/* PLL input to Serialink	*/
#define PCS_SCTRL_TXZ	0x0000c000	/* PLL input to Serialink	*/
#define PCS_SCTRL_TXP	0x00030000	/* PLL input to Serialink	*/

/* Shared Output Select Register.  For test and debug, allows multiplexing
 * test outputs into the PROM address pins.  Set to zero for normal
 * operation.
 */
#define PCS_SOS_PADDR	0x00000003	/* PROM Address			*/

/* PROM Image Space */
#define PROM_START	0x100000UL	/* Expansion ROM run time access*/
#define PROM_SIZE	0x0fffffUL	/* Size of ROM			*/
#define PROM_END	0x200000UL	/* End of ROM			*/

/* MII definitions missing from mii.h */

#define BMCR_SPD2	0x0040		/* Gigabit enable? (bcm5411)	*/
#define LPA_PAUSE	0x0400

/* More PHY registers (specific to Broadcom models) */

/* MII BCM5201 MULTIPHY interrupt register */
#define MII_BCM5201_INTERRUPT			0x1A
#define MII_BCM5201_INTERRUPT_INTENABLE		0x4000

#define MII_BCM5201_AUXMODE2			0x1B
#define MII_BCM5201_AUXMODE2_LOWPOWER		0x0008

#define MII_BCM5201_MULTIPHY                    0x1E

/* MII BCM5201 MULTIPHY register bits */
#define MII_BCM5201_MULTIPHY_SERIALMODE         0x0002
#define MII_BCM5201_MULTIPHY_SUPERISOLATE       0x0008

/* MII BCM5400 1000-BASET Control register */
#define MII_BCM5400_GB_CONTROL			0x09
#define MII_BCM5400_GB_CONTROL_FULLDUPLEXCAP	0x0200

/* MII BCM5400 AUXCONTROL register */
#define MII_BCM5400_AUXCONTROL                  0x18
#define MII_BCM5400_AUXCONTROL_PWR10BASET       0x0004

/* MII BCM5400 AUXSTATUS register */
#define MII_BCM5400_AUXSTATUS                   0x19
#define MII_BCM5400_AUXSTATUS_LINKMODE_MASK     0x0700
#define MII_BCM5400_AUXSTATUS_LINKMODE_SHIFT    8

/* When it can, GEM internally caches 4 aligned TX descriptors
 * at a time, so that it can use full cacheline DMA reads.
 *
 * Note that unlike HME, there is no ownership bit in the descriptor
 * control word.  The same functionality is obtained via the TX-Kick
 * and TX-Complete registers.  As a result, GEM need not write back
 * updated values to the TX descriptor ring, it only performs reads.
 *
 * Since TX descriptors are never modified by GEM, the driver can
 * use the buffer DMA address as a place to keep track of allocated
 * DMA mappings for a transmitted packet.
 */
struct gem_txd {
	__le64	control_word;
	__le64	buffer;
};

#define TXDCTRL_BUFSZ	0x0000000000007fffULL	/* Buffer Size		*/
#define TXDCTRL_CSTART	0x00000000001f8000ULL	/* CSUM Start Offset	*/
#define TXDCTRL_COFF	0x000000001fe00000ULL	/* CSUM Stuff Offset	*/
#define TXDCTRL_CENAB	0x0000000020000000ULL	/* CSUM Enable		*/
#define TXDCTRL_EOF	0x0000000040000000ULL	/* End of Frame		*/
#define TXDCTRL_SOF	0x0000000080000000ULL	/* Start of Frame	*/
#define TXDCTRL_INTME	0x0000000100000000ULL	/* "Interrupt Me"	*/
#define TXDCTRL_NOCRC	0x0000000200000000ULL	/* No CRC Present	*/

/* GEM requires that RX descriptors are provided four at a time,
 * aligned.  Also, the RX ring may not wrap around.  This means that
 * there will be at least 4 unused descriptor entries in the middle
 * of the RX ring at all times.
 *
 * Similar to HME, GEM assumes that it can write garbage bytes before
 * the beginning of the buffer and right after the end in order to DMA
 * whole cachelines.
 *
 * Unlike for TX, GEM does update the status word in the RX descriptors
 * when packets arrive.  Therefore an ownership bit does exist in the
 * RX descriptors.  It is advisory, GEM clears it but does not check
 * it in any way.  So when buffers are posted to the RX ring (via the
 * RX Kick register) by the driver it must make sure the buffers are
 * truly ready and that the ownership bits are set properly.
 *
 * Even though GEM modifies the RX descriptors, it guarantees that the
 * buffer DMA address field will stay the same when it performs these
 * updates.  Therefore it can be used to keep track of DMA mappings
 * by the host driver just as in the TX descriptor case above.
 */
struct gem_rxd {
	__le64	status_word;
	__le64	buffer;
};

#define RXDCTRL_TCPCSUM	0x000000000000ffffULL	/* TCP Pseudo-CSUM	*/
#define RXDCTRL_BUFSZ	0x000000007fff0000ULL	/* Buffer Size		*/
#define RXDCTRL_OWN	0x0000000080000000ULL	/* GEM owns this entry	*/
#define RXDCTRL_HASHVAL	0x0ffff00000000000ULL	/* Hash Value		*/
#define RXDCTRL_HPASS	0x1000000000000000ULL	/* Passed Hash Filter	*/
#define RXDCTRL_ALTMAC	0x2000000000000000ULL	/* Matched ALT MAC	*/
#define RXDCTRL_BAD	0x4000000000000000ULL	/* Frame has bad CRC	*/

#define RXDCTRL_FRESH(gp)	\
	((((RX_BUF_ALLOC_SIZE(gp) - RX_OFFSET) << 16) & RXDCTRL_BUFSZ) | \
	 RXDCTRL_OWN)

#define TX_RING_SIZE 128
#define RX_RING_SIZE 128

#if TX_RING_SIZE == 32
#define TXDMA_CFG_BASE	TXDMA_CFG_RINGSZ_32
#elif TX_RING_SIZE == 64
#define TXDMA_CFG_BASE	TXDMA_CFG_RINGSZ_64
#elif TX_RING_SIZE == 128
#define TXDMA_CFG_BASE	TXDMA_CFG_RINGSZ_128
#elif TX_RING_SIZE == 256
#define TXDMA_CFG_BASE	TXDMA_CFG_RINGSZ_256
#elif TX_RING_SIZE == 512
#define TXDMA_CFG_BASE	TXDMA_CFG_RINGSZ_512
#elif TX_RING_SIZE == 1024
#define TXDMA_CFG_BASE	TXDMA_CFG_RINGSZ_1K
#elif TX_RING_SIZE == 2048
#define TXDMA_CFG_BASE	TXDMA_CFG_RINGSZ_2K
#elif TX_RING_SIZE == 4096
#define TXDMA_CFG_BASE	TXDMA_CFG_RINGSZ_4K
#elif TX_RING_SIZE == 8192
#define TXDMA_CFG_BASE	TXDMA_CFG_RINGSZ_8K
#else
#error TX_RING_SIZE value is illegal...
#endif

#if RX_RING_SIZE == 32
#define RXDMA_CFG_BASE	RXDMA_CFG_RINGSZ_32
#elif RX_RING_SIZE == 64
#define RXDMA_CFG_BASE	RXDMA_CFG_RINGSZ_64
#elif RX_RING_SIZE == 128
#define RXDMA_CFG_BASE	RXDMA_CFG_RINGSZ_128
#elif RX_RING_SIZE == 256
#define RXDMA_CFG_BASE	RXDMA_CFG_RINGSZ_256
#elif RX_RING_SIZE == 512
#define RXDMA_CFG_BASE	RXDMA_CFG_RINGSZ_512
#elif RX_RING_SIZE == 1024
#define RXDMA_CFG_BASE	RXDMA_CFG_RINGSZ_1K
#elif RX_RING_SIZE == 2048
#define RXDMA_CFG_BASE	RXDMA_CFG_RINGSZ_2K
#elif RX_RING_SIZE == 4096
#define RXDMA_CFG_BASE	RXDMA_CFG_RINGSZ_4K
#elif RX_RING_SIZE == 8192
#define RXDMA_CFG_BASE	RXDMA_CFG_RINGSZ_8K
#else
#error RX_RING_SIZE is illegal...
#endif

#define NEXT_TX(N)	(((N) + 1) & (TX_RING_SIZE - 1))
#define NEXT_RX(N)	(((N) + 1) & (RX_RING_SIZE - 1))

#define TX_BUFFS_AVAIL(GP)					\
	(((GP)->tx_old <= (GP)->tx_new) ?			\
	  (GP)->tx_old + (TX_RING_SIZE - 1) - (GP)->tx_new :	\
	  (GP)->tx_old - (GP)->tx_new - 1)

#define RX_OFFSET          2
#define RX_BUF_ALLOC_SIZE(gp)	((gp)->rx_buf_sz + 28 + RX_OFFSET + 64)

#define RX_COPY_THRESHOLD  256

#if TX_RING_SIZE < 128
#define INIT_BLOCK_TX_RING_SIZE		128
#else
#define INIT_BLOCK_TX_RING_SIZE		TX_RING_SIZE
#endif

#if RX_RING_SIZE < 128
#define INIT_BLOCK_RX_RING_SIZE		128
#else
#define INIT_BLOCK_RX_RING_SIZE		RX_RING_SIZE
#endif

struct gem_init_block {
	struct gem_txd	txd[INIT_BLOCK_TX_RING_SIZE];
	struct gem_rxd	rxd[INIT_BLOCK_RX_RING_SIZE];
};

enum gem_phy_type {
	phy_mii_mdio0,
	phy_mii_mdio1,
	phy_serialink,
	phy_serdes,
};

enum link_state {
	link_down = 0,	/* No link, will retry */
	link_aneg,	/* Autoneg in progress */
	link_force_try,	/* Try Forced link speed */
	link_force_ret,	/* Forced mode worked, retrying autoneg */
	link_force_ok,	/* Stay in forced mode */
	link_up		/* Link is up */
};

struct gem {
	void __iomem		*regs;
	int			rx_new, rx_old;
	int			tx_new, tx_old;

	unsigned int has_wol : 1;	/* chip supports wake-on-lan */
	unsigned int asleep_wol : 1;	/* was asleep with WOL enabled */

	int			cell_enabled;
	u32			msg_enable;
	u32			status;

	struct napi_struct	napi;

	int			tx_fifo_sz;
	int			rx_fifo_sz;
	int			rx_pause_off;
	int			rx_pause_on;
	int			rx_buf_sz;
	u64			pause_entered;
	u16			pause_last_time_recvd;
	u32			mac_rx_cfg;
	u32			swrst_base;

	int			want_autoneg;
	int			last_forced_speed;
	enum link_state		lstate;
	struct timer_list	link_timer;
	int			timer_ticks;
	int			wake_on_lan;
	struct work_struct	reset_task;
	volatile int		reset_task_pending;

	enum gem_phy_type	phy_type;
	struct mii_phy		phy_mii;
	int			mii_phy_addr;

	struct gem_init_block	*init_block;
	struct sk_buff		*rx_skbs[RX_RING_SIZE];
	struct sk_buff		*tx_skbs[TX_RING_SIZE];
	dma_addr_t		gblock_dvma;

	struct pci_dev		*pdev;
	struct net_device	*dev;
#if defined(CONFIG_PPC_PMAC) || defined(CONFIG_SPARC)
	struct device_node	*of_node;
#endif
};

#define found_mii_phy(gp) ((gp->phy_type == phy_mii_mdio0 || gp->phy_type == phy_mii_mdio1) && \
			   gp->phy_mii.def && gp->phy_mii.def->ops)

#endif /* _SUNGEM_H */
