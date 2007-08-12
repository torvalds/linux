/*
 * Copyright (c) 2007 Nick Kossifidis <mickflemm@gmail.com>
 * Copyright (c) 2004, 2005, 2006, 2007 Reyk Floeter <reyk@openbsd.org>
 * Copyright (c) 2007 Michael Taylor <mike.taylor@apprion.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */

/*
 * Register values for Atheros 5210/5211/5212 cards from OpenBSD's ar5k
 * maintained by Reyk Floeter
 *
 * I tried to document those registers by looking at ar5k code, some
 * 802.11 (802.11e mostly) papers and by reading various public available
 * Atheros presentations and papers like these:
 *
 * 5210 - http://nova.stanford.edu/~bbaas/ps/isscc2002_slides.pdf
 *        http://www.it.iitb.ac.in/~janak/wifire/01222734.pdf
 *
 * 5211 - http://www.hotchips.org/archives/hc14/3_Tue/16_mcfarland.pdf
 */



/*====MAC DMA REGISTERS====*/

/*
 * AR5210-Specific TXDP registers
 * 5210 has only 2 transmit queues so no DCU/QCU, just
 * 2 transmit descriptor pointers...
 */
#define AR5K_NOQCU_TXDP0	0x0000		/* Queue 0 - data */
#define AR5K_NOQCU_TXDP1	0x0004		/* Queue 1 - beacons */

/*
 * Mac Control Register
 */
#define	AR5K_CR		0x0008			/* Register Address */
#define AR5K_CR_TXE0	0x00000001	/* TX Enable for queue 0 on 5210 */
#define AR5K_CR_TXE1	0x00000002	/* TX Enable for queue 1 on 5210 */
#define	AR5K_CR_RXE	0x00000004	/* RX Enable */
#define AR5K_CR_TXD0	0x00000008	/* TX Disable for queue 0 on 5210 */
#define AR5K_CR_TXD1	0x00000010	/* TX Disable for queue 1 on 5210 */
#define	AR5K_CR_RXD	0x00000020	/* RX Disable */
#define	AR5K_CR_SWI	0x00000040

/*
 * RX Descriptor Pointer register
 */
#define	AR5K_RXDP	0x000c

/*
 * Configuration and status register
 */
#define	AR5K_CFG		0x0014			/* Register Address */
#define	AR5K_CFG_SWTD		0x00000001	/* Byte-swap TX descriptor (for big endian archs) */
#define	AR5K_CFG_SWTB		0x00000002	/* Byte-swap TX buffer (?) */
#define	AR5K_CFG_SWRD		0x00000004	/* Byte-swap RX descriptor */
#define	AR5K_CFG_SWRB		0x00000008	/* Byte-swap RX buffer (?) */
#define	AR5K_CFG_SWRG		0x00000010	/* Byte-swap Register values (?) */
#define AR5K_CFG_ADHOC		0x00000020 	/* [5211+] */
#define AR5K_CFG_PHY_OK		0x00000100	/* [5211+] */
#define AR5K_CFG_EEBS		0x00000200	/* EEPROM is busy */
#define	AR5K_CFG_CLKGD		0x00000400	/* Clock gated (?) */
#define AR5K_CFG_TXCNT		0x00007800	/* Tx frame count (?) [5210] */
#define AR5K_CFG_TXCNT_S	11
#define AR5K_CFG_TXFSTAT	0x00008000	/* Tx frame status (?) [5210] */
#define AR5K_CFG_TXFSTRT	0x00010000	/* [5210] */
#define	AR5K_CFG_PCI_THRES	0x00060000	/* [5211+] */
#define	AR5K_CFG_PCI_THRES_S	17

/*
 * Interrupt enable register
 */
#define AR5K_IER		0x0024		/* Register Address */
#define AR5K_IER_DISABLE	0x00000000	/* Disable card interrupts */
#define AR5K_IER_ENABLE		0x00000001	/* Enable card interrupts */


/*
 * 0x0028 is Beacon Control Register on 5210
 * and first RTS duration register on 5211
 */

/*
 * Beacon control register [5210]
 */
#define AR5K_BCR		0x0028		/* Register Address */
#define AR5K_BCR_AP		0x00000000	/* AP mode */
#define AR5K_BCR_ADHOC		0x00000001	/* Ad-Hoc mode */
#define AR5K_BCR_BDMAE		0x00000002	/* DMA enable */
#define AR5K_BCR_TQ1FV		0x00000004	/* Use Queue1 for CAB traffic */
#define AR5K_BCR_TQ1V		0x00000008	/* Use Queue1 for Beacon traffic */
#define AR5K_BCR_BCGET		0x00000010

/*
 * First RTS duration register [5211]
 */
#define AR5K_RTSD0		0x0028		/* Register Address */
#define	AR5K_RTSD0_6		0x000000ff	/* 6Mb RTS duration mask (?) */
#define	AR5K_RTSD0_6_S		0		/* 6Mb RTS duration shift (?) */
#define	AR5K_RTSD0_9		0x0000ff00	/* 9Mb*/
#define	AR5K_RTSD0_9_S		8
#define	AR5K_RTSD0_12		0x00ff0000	/* 12Mb*/
#define	AR5K_RTSD0_12_S		16
#define	AR5K_RTSD0_18		0xff000000	/* 16Mb*/
#define	AR5K_RTSD0_18_S		24


/*
 * 0x002c is Beacon Status Register on 5210
 * and second RTS duration register on 5211
 */

/*
 * Beacon status register [5210]
 *
 * As i can see in ar5k_ar5210_tx_start Reyk uses some of the values of BCR
 * for this register, so i guess TQ1V,TQ1FV and BDMAE have the same meaning
 * here and SNP/SNAP means "snapshot" (so this register gets synced with BCR).
 * So SNAPPEDBCRVALID sould also stand for "snapped BCR -values- valid", so i
 * renamed it to SNAPSHOTSVALID to make more sense. I realy have no idea what
 * else can it be. I also renamed SNPBCMD to SNPADHOC to match BCR.
 */
#define AR5K_BSR		0x002c			/* Register Address */
#define AR5K_BSR_BDLYSW		0x00000001	/* SW Beacon delay (?) */
#define AR5K_BSR_BDLYDMA	0x00000002	/* DMA Beacon delay (?) */
#define AR5K_BSR_TXQ1F		0x00000004	/* Beacon queue (1) finished */
#define AR5K_BSR_ATIMDLY	0x00000008	/* ATIM delay (?) */
#define AR5K_BSR_SNPADHOC	0x00000100	/* Ad-hoc mode set (?) */
#define AR5K_BSR_SNPBDMAE	0x00000200	/* Beacon DMA enabled (?) */
#define AR5K_BSR_SNPTQ1FV	0x00000400	/* Queue1 is used for CAB traffic (?) */
#define AR5K_BSR_SNPTQ1V	0x00000800	/* Queue1 is used for Beacon traffic (?) */
#define AR5K_BSR_SNAPSHOTSVALID	0x00001000	/* BCR snapshots are valid (?) */
#define AR5K_BSR_SWBA_CNT	0x00ff0000

/*
 * Second RTS duration register [5211]
 */
#define AR5K_RTSD1		0x002c			/* Register Address */
#define	AR5K_RTSD1_24		0x000000ff	/* 24Mb */
#define	AR5K_RTSD1_24_S		0
#define	AR5K_RTSD1_36		0x0000ff00	/* 36Mb */
#define	AR5K_RTSD1_36_S		8
#define	AR5K_RTSD1_48		0x00ff0000	/* 48Mb */
#define	AR5K_RTSD1_48_S		16
#define	AR5K_RTSD1_54		0xff000000	/* 54Mb */
#define	AR5K_RTSD1_54_S		24


/*
 * Transmit configuration register
 */
#define AR5K_TXCFG		0x0030			/* Register Address */
#define AR5K_TXCFG_SDMAMR	0x00000007	/* DMA size */
#define AR5K_TXCFG_SDMAMR_S	0
#define AR5K_TXCFG_B_MODE	0x00000008	/* Set b mode for 5111 (enable 2111) */
#define AR5K_TXCFG_TXFSTP	0x00000008	/* TX DMA full Stop [5210] */
#define AR5K_TXCFG_TXFULL	0x000003f0	/* TX Triger level mask */
#define AR5K_TXCFG_TXFULL_S	4
#define AR5K_TXCFG_TXFULL_0B	0x00000000
#define AR5K_TXCFG_TXFULL_64B	0x00000010
#define AR5K_TXCFG_TXFULL_128B	0x00000020
#define AR5K_TXCFG_TXFULL_192B	0x00000030
#define AR5K_TXCFG_TXFULL_256B	0x00000040
#define AR5K_TXCFG_TXCONT_EN	0x00000080
#define AR5K_TXCFG_DMASIZE	0x00000100	/* Flag for passing DMA size [5210] */
#define AR5K_TXCFG_JUMBO_TXE	0x00000400	/* Enable jumbo frames transmition (?) [5211+] */
#define AR5K_TXCFG_RTSRND	0x00001000	/* [5211+] */
#define AR5K_TXCFG_FRMPAD_DIS	0x00002000	/* [5211+] */
#define AR5K_TXCFG_RDY_DIS	0x00004000	/* [5211+] */

/*
 * Receive configuration register
 */
#define AR5K_RXCFG		0x0034			/* Register Address */
#define AR5K_RXCFG_SDMAMW	0x00000007	/* DMA size */
#define AR5K_RXCFG_SDMAMW_S	0
#define	AR5K_RXCFG_DEF_ANTENNA	0x00000008	/* Default antenna */
#define AR5K_RXCFG_ZLFDMA	0x00000010	/* Zero-length DMA */
#define AR5K_RXCFG_JUMBO_RXE	0x00000020	/* Enable jumbo frames reception (?) [5211+] */
#define AR5K_RXCFG_JUMBO_WRAP	0x00000040	/* Wrap jumbo frames (?) [5211+] */

/*
 * Receive jumbo descriptor last address register
 * Only found in 5211 (?)
 */
#define AR5K_RXJLA		0x0038

/*
 * MIB control register
 */
#define AR5K_MIBC		0x0040			/* Register Address */
#define AR5K_MIBC_COW		0x00000001
#define AR5K_MIBC_FMC		0x00000002	/* Freeze Mib Counters (?) */
#define AR5K_MIBC_CMC		0x00000004	/* Clean Mib Counters (?) */
#define AR5K_MIBC_MCS		0x00000008

/*
 * Timeout prescale register
 */
#define AR5K_TOPS		0x0044
#define	AR5K_TOPS_M		0x0000ffff	/* [5211+] (?) */

/*
 * Receive timeout register (no frame received)
 */
#define AR5K_RXNOFRM		0x0048
#define	AR5K_RXNOFRM_M		0x000003ff	/* [5211+] (?) */

/*
 * Transmit timeout register (no frame sent)
 */
#define AR5K_TXNOFRM		0x004c
#define	AR5K_TXNOFRM_M		0x000003ff	/* [5211+] (?) */
#define	AR5K_TXNOFRM_QCU	0x000ffc00	/* [5211+] (?) */

/*
 * Receive frame gap timeout register
 */
#define AR5K_RPGTO		0x0050
#define AR5K_RPGTO_M		0x000003ff	/* [5211+] (?) */

/*
 * Receive frame count limit register
 */
#define AR5K_RFCNT		0x0054
#define AR5K_RFCNT_M		0x0000001f	/* [5211+] (?) */
#define AR5K_RFCNT_RFCL		0x0000000f	/* [5210] */

/*
 * Misc settings register
 */
#define AR5K_MISC		0x0058			/* Register Address */
#define	AR5K_MISC_DMA_OBS_M	0x000001e0
#define	AR5K_MISC_DMA_OBS_S	5
#define	AR5K_MISC_MISC_OBS_M	0x00000e00
#define	AR5K_MISC_MISC_OBS_S	9
#define	AR5K_MISC_MAC_OBS_LSB_M	0x00007000
#define	AR5K_MISC_MAC_OBS_LSB_S	12
#define	AR5K_MISC_MAC_OBS_MSB_M	0x00038000
#define	AR5K_MISC_MAC_OBS_MSB_S	15
#define AR5K_MISC_LED_DECAY	0x001c0000	/* [5210] */
#define AR5K_MISC_LED_BLINK	0x00e00000	/* [5210] */

/*
 * QCU/DCU clock gating register (5311)
 */
#define	AR5K_QCUDCU_CLKGT	0x005c			/* Register Address (?) */
#define	AR5K_QCUDCU_CLKGT_QCU	0x0000ffff	/* Mask for QCU clock */
#define	AR5K_QCUDCU_CLKGT_DCU	0x07ff0000	/* Mask for DCU clock */

/*
 * Interrupt Status Registers
 *
 * For 5210 there is only one status register but for
 * 5211/5212 we have one primary and 4 secondary registers.
 * So we have AR5K_ISR for 5210 and AR5K_PISR /SISRx for 5211/5212.
 * Most of these bits are common for all chipsets.
 */
#define AR5K_ISR		0x001c			/* Register Address [5210] */
#define AR5K_PISR		0x0080			/* Register Address [5211+] */
#define AR5K_ISR_RXOK		0x00000001	/* Frame successfuly recieved */
#define AR5K_ISR_RXDESC		0x00000002	/* RX descriptor request */
#define AR5K_ISR_RXERR		0x00000004	/* Receive error */
#define AR5K_ISR_RXNOFRM	0x00000008	/* No frame received (receive timeout) */
#define AR5K_ISR_RXEOL		0x00000010	/* Empty RX descriptor */
#define AR5K_ISR_RXORN		0x00000020	/* Receive FIFO overrun */
#define AR5K_ISR_TXOK		0x00000040	/* Frame successfuly transmited */
#define AR5K_ISR_TXDESC		0x00000080	/* TX descriptor request */
#define AR5K_ISR_TXERR		0x00000100	/* Transmit error */
#define AR5K_ISR_TXNOFRM	0x00000200	/* No frame transmited (transmit timeout) */
#define AR5K_ISR_TXEOL		0x00000400	/* Empty TX descriptor */
#define AR5K_ISR_TXURN		0x00000800	/* Transmit FIFO underrun */
#define AR5K_ISR_MIB		0x00001000	/* Update MIB counters */
#define AR5K_ISR_SWI		0x00002000	/* Software interrupt (?) */
#define AR5K_ISR_RXPHY		0x00004000	/* PHY error */
#define AR5K_ISR_RXKCM		0x00008000
#define AR5K_ISR_SWBA		0x00010000	/* Software beacon alert */
#define AR5K_ISR_BRSSI		0x00020000
#define AR5K_ISR_BMISS		0x00040000	/* Beacon missed */
#define AR5K_ISR_HIUERR		0x00080000	/* Host Interface Unit error [5211+] */
#define AR5K_ISR_BNR		0x00100000 	/* Beacon not ready [5211+] */
#define AR5K_ISR_MCABT		0x00100000	/* [5210] */
#define AR5K_ISR_RXCHIRP	0x00200000	/* [5212+] */
#define AR5K_ISR_SSERR		0x00200000	/* [5210] */
#define AR5K_ISR_DPERR		0x00400000	/* [5210] */
#define AR5K_ISR_TIM		0x00800000	/* [5210] */
#define AR5K_ISR_BCNMISC	0x00800000	/* [5212+] */
#define AR5K_ISR_GPIO		0x01000000	/* GPIO (rf kill)*/
#define AR5K_ISR_QCBRORN	0x02000000	/* CBR overrun (?)  [5211+] */
#define AR5K_ISR_QCBRURN	0x04000000	/* CBR underrun (?) [5211+] */
#define AR5K_ISR_QTRIG		0x08000000	/* [5211+] */

/*
 * Secondary status registers [5211+] (0 - 4)
 *
 * I guess from the names that these give the status for each
 * queue, that's why only masks are defined here, haven't got
 * any info about them (couldn't find them anywhere in ar5k code).
 */
#define AR5K_SISR0		0x0084			/* Register Address [5211+] */
#define AR5K_SISR0_QCU_TXOK	0x000003ff	/* Mask for QCU_TXOK */
#define AR5K_SISR0_QCU_TXDESC	0x03ff0000	/* Mask for QCU_TXDESC */

#define AR5K_SISR1		0x0088			/* Register Address [5211+] */
#define AR5K_SISR1_QCU_TXERR	0x000003ff	/* Mask for QCU_TXERR */
#define AR5K_SISR1_QCU_TXEOL	0x03ff0000	/* Mask for QCU_TXEOL */

#define AR5K_SISR2		0x008c			/* Register Address [5211+] */
#define AR5K_SISR2_QCU_TXURN	0x000003ff	/* Mask for QCU_TXURN */
#define	AR5K_SISR2_MCABT	0x00100000
#define	AR5K_SISR2_SSERR	0x00200000
#define	AR5K_SISR2_DPERR	0x00400000
#define	AR5K_SISR2_TIM		0x01000000	/* [5212+] */
#define	AR5K_SISR2_CAB_END	0x02000000	/* [5212+] */
#define	AR5K_SISR2_DTIM_SYNC	0x04000000	/* [5212+] */
#define	AR5K_SISR2_BCN_TIMEOUT	0x08000000	/* [5212+] */
#define	AR5K_SISR2_CAB_TIMEOUT	0x10000000	/* [5212+] */
#define	AR5K_SISR2_DTIM		0x20000000	/* [5212+] */

#define AR5K_SISR3		0x0090			/* Register Address [5211+] */
#define AR5K_SISR3_QCBRORN	0x000003ff	/* Mask for QCBRORN */
#define AR5K_SISR3_QCBRURN	0x03ff0000	/* Mask for QCBRURN */

#define AR5K_SISR4		0x0094			/* Register Address [5211+] */
#define AR5K_SISR4_QTRIG	0x000003ff	/* Mask for QTRIG */

/*
 * Shadow read-and-clear interrupt status registers [5211+]
 */
#define AR5K_RAC_PISR		0x00c0		/* Read and clear PISR */
#define AR5K_RAC_SISR0		0x00c4		/* Read and clear SISR0 */
#define AR5K_RAC_SISR1		0x00c8		/* Read and clear SISR1 */
#define AR5K_RAC_SISR2		0x00cc		/* Read and clear SISR2 */
#define AR5K_RAC_SISR3		0x00d0		/* Read and clear SISR3 */
#define AR5K_RAC_SISR4		0x00d4		/* Read and clear SISR4 */

/*
 * Interrupt Mask Registers
 *
 * As whith ISRs 5210 has one IMR (AR5K_IMR) and 5211/5212 has one primary
 * (AR5K_PIMR) and 4 secondary IMRs (AR5K_SIMRx). Note that ISR/IMR flags match.
 */
#define	AR5K_IMR		0x0020			/* Register Address [5210] */
#define AR5K_PIMR		0x00a0			/* Register Address [5211+] */
#define AR5K_IMR_RXOK		0x00000001	/* Frame successfuly recieved*/
#define AR5K_IMR_RXDESC		0x00000002	/* RX descriptor request*/
#define AR5K_IMR_RXERR		0x00000004	/* Receive error*/
#define AR5K_IMR_RXNOFRM	0x00000008	/* No frame received (receive timeout)*/
#define AR5K_IMR_RXEOL		0x00000010	/* Empty RX descriptor*/
#define AR5K_IMR_RXORN		0x00000020	/* Receive FIFO overrun*/
#define AR5K_IMR_TXOK		0x00000040	/* Frame successfuly transmited*/
#define AR5K_IMR_TXDESC		0x00000080	/* TX descriptor request*/
#define AR5K_IMR_TXERR		0x00000100	/* Transmit error*/
#define AR5K_IMR_TXNOFRM	0x00000200	/* No frame transmited (transmit timeout)*/
#define AR5K_IMR_TXEOL		0x00000400	/* Empty TX descriptor*/
#define AR5K_IMR_TXURN		0x00000800	/* Transmit FIFO underrun*/
#define AR5K_IMR_MIB		0x00001000	/* Update MIB counters*/
#define AR5K_IMR_SWI		0x00002000
#define AR5K_IMR_RXPHY		0x00004000	/* PHY error*/
#define AR5K_IMR_RXKCM		0x00008000
#define AR5K_IMR_SWBA		0x00010000	/* Software beacon alert*/
#define AR5K_IMR_BRSSI		0x00020000
#define AR5K_IMR_BMISS		0x00040000	/* Beacon missed*/
#define AR5K_IMR_HIUERR		0x00080000	/* Host Interface Unit error [5211+] */
#define AR5K_IMR_BNR		0x00100000 	/* Beacon not ready [5211+] */
#define AR5K_IMR_MCABT		0x00100000	/* [5210] */
#define AR5K_IMR_RXCHIRP	0x00200000	/* [5212+]*/
#define AR5K_IMR_SSERR		0x00200000	/* [5210] */
#define AR5K_IMR_DPERR		0x00400000	/* [5210] */
#define AR5K_IMR_TIM		0x00800000	/* [5211+] */
#define AR5K_IMR_BCNMISC	0x00800000	/* [5212+] */
#define AR5K_IMR_GPIO		0x01000000	/* GPIO (rf kill)*/
#define AR5K_IMR_QCBRORN	0x02000000	/* CBR overrun (?) [5211+] */
#define AR5K_IMR_QCBRURN	0x04000000	/* CBR underrun (?) [5211+] */
#define AR5K_IMR_QTRIG		0x08000000	/* [5211+] */

/*
 * Secondary interrupt mask registers [5211+] (0 - 4)
 */
#define AR5K_SIMR0		0x00a4			/* Register Address [5211+] */
#define AR5K_SIMR0_QCU_TXOK	0x000003ff	/* Mask for QCU_TXOK */
#define AR5K_SIMR0_QCU_TXOK_S	0
#define AR5K_SIMR0_QCU_TXDESC	0x03ff0000	/* Mask for QCU_TXDESC */
#define AR5K_SIMR0_QCU_TXDESC_S	16

#define AR5K_SIMR1		0x00a8			/* Register Address [5211+] */
#define AR5K_SIMR1_QCU_TXERR	0x000003ff	/* Mask for QCU_TXERR */
#define AR5K_SIMR1_QCU_TXERR_S	0
#define AR5K_SIMR1_QCU_TXEOL	0x03ff0000	/* Mask for QCU_TXEOL */
#define AR5K_SIMR1_QCU_TXEOL_S	16

#define AR5K_SIMR2		0x00ac			/* Register Address [5211+] */
#define AR5K_SIMR2_QCU_TXURN	0x000003ff	/* Mask for QCU_TXURN */
#define AR5K_SIMR2_QCU_TXURN_S	0
#define	AR5K_SIMR2_MCABT	0x00100000
#define	AR5K_SIMR2_SSERR	0x00200000
#define	AR5K_SIMR2_DPERR	0x00400000
#define	AR5K_SIMR2_TIM		0x01000000	/* [5212+] */
#define	AR5K_SIMR2_CAB_END	0x02000000	/* [5212+] */
#define	AR5K_SIMR2_DTIM_SYNC	0x04000000	/* [5212+] */
#define	AR5K_SIMR2_BCN_TIMEOUT	0x08000000	/* [5212+] */
#define	AR5K_SIMR2_CAB_TIMEOUT	0x10000000	/* [5212+] */
#define	AR5K_SIMR2_DTIM		0x20000000	/* [5212+] */

#define AR5K_SIMR3		0x00b0			/* Register Address [5211+] */
#define AR5K_SIMR3_QCBRORN	0x000003ff	/* Mask for QCBRORN */
#define AR5K_SIMR3_QCBRORN_S	0
#define AR5K_SIMR3_QCBRURN	0x03ff0000	/* Mask for QCBRURN */
#define AR5K_SIMR3_QCBRURN_S	16

#define AR5K_SIMR4		0x00b4			/* Register Address [5211+] */
#define AR5K_SIMR4_QTRIG	0x000003ff	/* Mask for QTRIG */
#define AR5K_SIMR4_QTRIG_S	0


/*
 * Decompression mask registers [5212+]
 */
#define AR5K_DCM_ADDR		0x0400		/*Decompression mask address (?)*/
#define AR5K_DCM_DATA		0x0404		/*Decompression mask data (?)*/

/*
 * Decompression configuration registers [5212+]
 */
#define AR5K_DCCFG		0x0420

/*
 * Compression configuration registers [5212+]
 */
#define AR5K_CCFG		0x0600
#define AR5K_CCFG_CUP		0x0604

/*
 * Compression performance counter registers [5212+]
 */
#define AR5K_CPC0		0x0610		/* Compression performance counter 0 */
#define AR5K_CPC1		0x0614		/* Compression performance counter 1*/
#define AR5K_CPC2		0x0618		/* Compression performance counter 2 */
#define AR5K_CPC3		0x061c		/* Compression performance counter 3 */
#define AR5K_CPCORN		0x0620		/* Compression performance overrun (?) */


/*
 * Queue control unit (QCU) registers [5211+]
 *
 * Card has 12 TX Queues but i see that only 0-9 are used (?)
 * both in binary HAL (see ah.h) and ar5k. Each queue has it's own
 * TXDP at addresses 0x0800 - 0x082c, a CBR (Constant Bit Rate)
 * configuration register (0x08c0 - 0x08ec), a ready time configuration
 * register (0x0900 - 0x092c), a misc configuration register (0x09c0 -
 * 0x09ec) and a status register (0x0a00 - 0x0a2c). We also have some
 * global registers, QCU transmit enable/disable and "one shot arm (?)"
 * set/clear, which contain status for all queues (we shift by 1 for each
 * queue). To access these registers easily we define some macros here
 * that are used inside HAL. For more infos check out *_tx_queue functs.
 *
 * TODO: Boundary checking on macros (here?)
 */

/*
 * Generic QCU Register access macros
 */
#define	AR5K_QUEUE_REG(_r, _q)		(((_q) << 2) + _r)
#define AR5K_QCU_GLOBAL_READ(_r, _q)	(AR5K_REG_READ(_r) & (1 << _q))
#define AR5K_QCU_GLOBAL_WRITE(_r, _q)	AR5K_REG_WRITE(_r, (1 << _q))

/*
 * QCU Transmit descriptor pointer registers
 */
#define AR5K_QCU_TXDP_BASE	0x0800		/* Register Address - Queue0 TXDP */
#define AR5K_QUEUE_TXDP(_q)	AR5K_QUEUE_REG(AR5K_QCU_TXDP_BASE, _q)

/*
 * QCU Transmit enable register
 */
#define AR5K_QCU_TXE		0x0840
#define AR5K_ENABLE_QUEUE(_q)	AR5K_QCU_GLOBAL_WRITE(AR5K_QCU_TXE, _q)
#define AR5K_QUEUE_ENABLED(_q)	AR5K_QCU_GLOBAL_READ(AR5K_QCU_TXE, _q)

/*
 * QCU Transmit disable register
 */
#define AR5K_QCU_TXD		0x0880
#define AR5K_DISABLE_QUEUE(_q)	AR5K_QCU_GLOBAL_WRITE(AR5K_QCU_TXD, _q)
#define AR5K_QUEUE_DISABLED(_q)	AR5K_QCU_GLOBAL_READ(AR5K_QCU_TXD, _q)

/*
 * QCU Constant Bit Rate configuration registers
 */
#define	AR5K_QCU_CBRCFG_BASE		0x08c0	/* Register Address - Queue0 CBRCFG */
#define	AR5K_QCU_CBRCFG_INTVAL		0x00ffffff	/* CBR Interval mask */
#define AR5K_QCU_CBRCFG_INTVAL_S	0
#define	AR5K_QCU_CBRCFG_ORN_THRES	0xff000000	/* CBR overrun threshold mask */
#define AR5K_QCU_CBRCFG_ORN_THRES_S	24
#define	AR5K_QUEUE_CBRCFG(_q)		AR5K_QUEUE_REG(AR5K_QCU_CBRCFG_BASE, _q)

/*
 * QCU Ready time configuration registers
 */
#define	AR5K_QCU_RDYTIMECFG_BASE	0x0900	/* Register Address - Queue0 RDYTIMECFG */
#define	AR5K_QCU_RDYTIMECFG_INTVAL	0x00ffffff	/* Ready time interval mask */
#define AR5K_QCU_RDYTIMECFG_INTVAL_S	0
#define	AR5K_QCU_RDYTIMECFG_DURATION	0x00ffffff	/* Ready time duration mask */
#define	AR5K_QCU_RDYTIMECFG_ENABLE	0x01000000	/* Ready time enable mask */
#define AR5K_QUEUE_RDYTIMECFG(_q)	AR5K_QUEUE_REG(AR5K_QCU_RDYTIMECFG_BASE, _q)

/*
 * QCU one shot arm set registers
 */
#define	AR5K_QCU_ONESHOTARM_SET		0x0940	/* Register Address -QCU "one shot arm set (?)" */
#define	AR5K_QCU_ONESHOTARM_SET_M	0x0000ffff

/*
 * QCU one shot arm clear registers
 */
#define	AR5K_QCU_ONESHOTARM_CLEAR	0x0980	/* Register Address -QCU "one shot arm clear (?)" */
#define	AR5K_QCU_ONESHOTARM_CLEAR_M	0x0000ffff

/*
 * QCU misc registers
 */
#define AR5K_QCU_MISC_BASE		0x09c0			/* Register Address -Queue0 MISC */
#define	AR5K_QCU_MISC_FRSHED_M		0x0000000f	/* Frame sheduling mask */
#define	AR5K_QCU_MISC_FRSHED_ASAP	0		/* ASAP */
#define	AR5K_QCU_MISC_FRSHED_CBR	1		/* Constant Bit Rate */
#define	AR5K_QCU_MISC_FRSHED_DBA_GT	2		/* DMA Beacon alert gated (?) */
#define	AR5K_QCU_MISC_FRSHED_TIM_GT	3		/* Time gated (?) */
#define	AR5K_QCU_MISC_FRSHED_BCN_SENT_GT	4	/* Beacon sent gated (?) */
#define	AR5K_QCU_MISC_ONESHOT_ENABLE	0x00000010	/* Oneshot enable */
#define	AR5K_QCU_MISC_CBREXP		0x00000020	/* CBR expired (normal queue) */
#define	AR5K_QCU_MISC_CBREXP_BCN	0x00000040	/* CBR expired (beacon queue) */
#define	AR5K_QCU_MISC_BCN_ENABLE	0x00000080	/* Beacons enabled */
#define	AR5K_QCU_MISC_CBR_THRES_ENABLE	0x00000100	/* CBR threshold enabled (?) */
#define	AR5K_QCU_MISC_TXE		0x00000200	/* TXE reset when RDYTIME enalbed (?) */
#define	AR5K_QCU_MISC_CBR		0x00000400	/* CBR threshold reset (?) */
#define	AR5K_QCU_MISC_DCU_EARLY		0x00000800	/* DCU reset (?) */
#define AR5K_QUEUE_MISC(_q)		AR5K_QUEUE_REG(AR5K_QCU_MISC_BASE, _q)


/*
 * QCU status registers
 */
#define AR5K_QCU_STS_BASE	0x0a00			/* Register Address - Queue0 STS */
#define	AR5K_QCU_STS_FRMPENDCNT	0x00000003	/* Frames pending counter */
#define	AR5K_QCU_STS_CBREXPCNT	0x0000ff00	/* CBR expired counter (?) */
#define	AR5K_QUEUE_STATUS(_q)	AR5K_QUEUE_REG(AR5K_QCU_STS_BASE, _q)

/*
 * QCU ready time shutdown register
 */
#define AR5K_QCU_RDYTIMESHDN	0x0a40
#define AR5K_QCU_RDYTIMESHDN_M	0x000003ff

/*
 * QCU compression buffer base registers [5212+]
 */
#define AR5K_QCU_CBB_SELECT	0x0b00
#define AR5K_QCU_CBB_ADDR	0x0b04

/*
 * QCU compression buffer configuration register [5212+]
 */
#define AR5K_QCU_CBCFG		0x0b08



/*
 * Distributed Coordination Function (DCF) control unit (DCU)
 * registers [5211+]
 *
 * These registers control the various characteristics of each queue
 * for 802.11e (WME) combatibility so they go together with
 * QCU registers in pairs. For each queue we have a QCU mask register,
 * (0x1000 - 0x102c), a local-IFS settings register (0x1040 - 0x106c),
 * a retry limit register (0x1080 - 0x10ac), a channel time register
 * (0x10c0 - 0x10ec), a misc-settings register (0x1100 - 0x112c) and
 * a sequence number register (0x1140 - 0x116c). It seems that "global"
 * registers here afect all queues (see use of DCU_GBL_IFS_SLOT in ar5k).
 * We use the same macros here for easier register access.
 *
 */

/*
 * DCU QCU mask registers
 */
#define AR5K_DCU_QCUMASK_BASE	0x1000		/* Register Address -Queue0 DCU_QCUMASK */
#define AR5K_DCU_QCUMASK_M	0x000003ff
#define AR5K_QUEUE_QCUMASK(_q)	AR5K_QUEUE_REG(AR5K_DCU_QCUMASK_BASE, _q)

/*
 * DCU local Inter Frame Space settings register
 */
#define AR5K_DCU_LCL_IFS_BASE		0x1040			/* Register Address -Queue0 DCU_LCL_IFS */
#define	AR5K_DCU_LCL_IFS_CW_MIN	        0x000003ff	/* Minimum Contention Window */
#define	AR5K_DCU_LCL_IFS_CW_MIN_S	0
#define	AR5K_DCU_LCL_IFS_CW_MAX	        0x000ffc00	/* Maximum Contention Window */
#define	AR5K_DCU_LCL_IFS_CW_MAX_S	10
#define	AR5K_DCU_LCL_IFS_AIFS		0x0ff00000	/* Arbitrated Interframe Space */
#define	AR5K_DCU_LCL_IFS_AIFS_S		20
#define	AR5K_QUEUE_DFS_LOCAL_IFS(_q)	AR5K_QUEUE_REG(AR5K_DCU_LCL_IFS_BASE, _q)

/*
 * DCU retry limit registers
 */
#define AR5K_DCU_RETRY_LMT_BASE		0x1080			/* Register Address -Queue0 DCU_RETRY_LMT */
#define AR5K_DCU_RETRY_LMT_SH_RETRY	0x0000000f	/* Short retry limit mask */
#define AR5K_DCU_RETRY_LMT_SH_RETRY_S	0
#define AR5K_DCU_RETRY_LMT_LG_RETRY	0x000000f0	/* Long retry limit mask */
#define AR5K_DCU_RETRY_LMT_LG_RETRY_S	4
#define AR5K_DCU_RETRY_LMT_SSH_RETRY	0x00003f00	/* Station short retry limit mask (?) */
#define AR5K_DCU_RETRY_LMT_SSH_RETRY_S	8
#define AR5K_DCU_RETRY_LMT_SLG_RETRY	0x000fc000	/* Station long retry limit mask (?) */
#define AR5K_DCU_RETRY_LMT_SLG_RETRY_S	14
#define	AR5K_QUEUE_DFS_RETRY_LIMIT(_q)	AR5K_QUEUE_REG(AR5K_DCU_RETRY_LMT_BASE, _q)

/*
 * DCU channel time registers
 */
#define AR5K_DCU_CHAN_TIME_BASE		0x10c0			/* Register Address -Queue0 DCU_CHAN_TIME */
#define	AR5K_DCU_CHAN_TIME_DUR		0x000fffff	/* Channel time duration */
#define	AR5K_DCU_CHAN_TIME_DUR_S	0
#define	AR5K_DCU_CHAN_TIME_ENABLE	0x00100000	/* Enable channel time */
#define AR5K_QUEUE_DFS_CHANNEL_TIME(_q)	AR5K_QUEUE_REG(AR5K_DCU_CHAN_TIME_BASE, _q)

/*
 * DCU misc registers [5211+]
 *
 * For some of the registers i couldn't find in the code
 * (only backoff stuff is there realy) i tried to match the
 * names with 802.11e parameters etc, so i guess VIRTCOL here
 * means Virtual Collision and HCFPOLL means Hybrid Coordination
 * factor Poll (CF- Poll). Arbiter lockout control controls the
 * behaviour on low priority queues when we have multiple queues
 * with pending frames. Intra-frame lockout means we wait until
 * the queue's current frame transmits (with post frame backoff and bursting)
 * before we transmit anything else and global lockout means we
 * wait for the whole queue to finish before higher priority queues
 * can transmit (this is used on beacon and CAB queues).
 * No lockout means there is no special handling.
 */
#define AR5K_DCU_MISC_BASE		0x1100			/* Register Address -Queue0 DCU_MISC */
#define	AR5K_DCU_MISC_BACKOFF		0x000007ff	/* Mask for backoff setting (?) */
#define AR5K_DCU_MISC_BACKOFF_FRAG	0x00000200	/* Enable backoff while bursting */
#define	AR5K_DCU_MISC_HCFPOLL_ENABLE	0x00000800	/* CF - Poll (?) */
#define	AR5K_DCU_MISC_BACKOFF_PERSIST	0x00001000	/* Persistent backoff (?) */
#define	AR5K_DCU_MISC_FRMPRFTCH_ENABLE	0x00002000	/* Enable frame pre-fetch (?) */
#define	AR5K_DCU_MISC_VIRTCOL		0x0000c000	/* Mask for Virtual Collision (?) */
#define	AR5K_DCU_MISC_VIRTCOL_NORMAL	0
#define	AR5K_DCU_MISC_VIRTCOL_MODIFIED	1
#define	AR5K_DCU_MISC_VIRTCOL_IGNORE	2
#define	AR5K_DCU_MISC_BCN_ENABLE	0x00010000	/* Beacon enable (?) */
#define	AR5K_DCU_MISC_ARBLOCK_CTL	0x00060000	/* Arbiter lockout control mask */
#define	AR5K_DCU_MISC_ARBLOCK_CTL_S	17
#define	AR5K_DCU_MISC_ARBLOCK_CTL_NONE	0		/* No arbiter lockout */
#define	AR5K_DCU_MISC_ARBLOCK_CTL_INTFRM	1	/* Intra-frame lockout */
#define	AR5K_DCU_MISC_ARBLOCK_CTL_GLOBAL	2	/* Global lockout */
#define	AR5K_DCU_MISC_ARBLOCK_IGNORE	0x00080000
#define	AR5K_DCU_MISC_SEQ_NUM_INCR_DIS	0x00100000	/* Disable sequence number increment (?) */
#define	AR5K_DCU_MISC_POST_FR_BKOFF_DIS	0x00200000	/* Disable post-frame backoff (?) */
#define	AR5K_DCU_MISC_VIRT_COLL_POLICY	0x00400000	/* Virtual Collision policy (?) */
#define	AR5K_DCU_MISC_BLOWN_IFS_POLICY	0x00800000
#define	AR5K_DCU_MISC_SEQNUM_CTL	0x01000000	/* Sequence number control (?) */
#define AR5K_QUEUE_DFS_MISC(_q)		AR5K_QUEUE_REG(AR5K_DCU_MISC_BASE, _q)

/*
 * DCU frame sequence number registers
 */
#define AR5K_DCU_SEQNUM_BASE	0x1140
#define	AR5K_DCU_SEQNUM_M	0x00000fff
#define	AR5K_QUEUE_DFS_SEQNUM(_q)	AR5K_QUEUE_REG(AR5K_DCU_SEQNUM_BASE, _q)

/*
 * DCU global IFS SIFS registers
 */
#define AR5K_DCU_GBL_IFS_SIFS	0x1030
#define AR5K_DCU_GBL_IFS_SIFS_M	0x0000ffff

/*
 * DCU global IFS slot interval registers
 */
#define AR5K_DCU_GBL_IFS_SLOT	0x1070
#define AR5K_DCU_GBL_IFS_SLOT_M	0x0000ffff

/*
 * DCU global IFS EIFS registers
 */
#define AR5K_DCU_GBL_IFS_EIFS	0x10b0
#define AR5K_DCU_GBL_IFS_EIFS_M	0x0000ffff

/*
 * DCU global IFS misc registers
 */
#define AR5K_DCU_GBL_IFS_MISC			0x10f0			/* Register Address */
#define	AR5K_DCU_GBL_IFS_MISC_LFSR_SLICE	0x00000007
#define	AR5K_DCU_GBL_IFS_MISC_TURBO_MODE	0x00000008	/* Turbo mode (?) */
#define	AR5K_DCU_GBL_IFS_MISC_SIFS_DUR_USEC	0x000003f0	/* SIFS Duration mask (?) */
#define	AR5K_DCU_GBL_IFS_MISC_USEC_DUR		0x000ffc00
#define	AR5K_DCU_GBL_IFS_MISC_DCU_ARB_DELAY	0x00300000

/*
 * DCU frame prefetch control register
 */
#define AR5K_DCU_FP		0x1230

/*
 * DCU transmit pause control/status register
 */
#define AR5K_DCU_TXP		0x1270			/* Register Address */
#define	AR5K_DCU_TXP_M		0x000003ff	/* Tx pause mask (?) */
#define	AR5K_DCU_TXP_STATUS	0x00010000	/* Tx pause status (?) */

/*
 * DCU transmit filter register
 */
#define AR5K_DCU_TX_FILTER	0x1038

/*
 * DCU clear transmit filter register
 */
#define AR5K_DCU_TX_FILTER_CLR	0x143c

/*
 * DCU set transmit filter register
 */
#define AR5K_DCU_TX_FILTER_SET	0x147c

/*
 * Reset control register
 *
 * 4 and 8 are not used in 5211/5212 and
 * 2 means "baseband reset" on 5211/5212.
 */
#define AR5K_RESET_CTL		0x4000			/* Register Address */
#define AR5K_RESET_CTL_PCU	0x00000001	/* Protocol Control Unit reset */
#define AR5K_RESET_CTL_DMA	0x00000002	/* DMA (Rx/Tx) reset [5210] */
#define	AR5K_RESET_CTL_BASEBAND	0x00000002	/* Baseband reset [5211+] */
#define AR5K_RESET_CTL_MAC	0x00000004	/* MAC reset (PCU+Baseband ?) [5210] */
#define AR5K_RESET_CTL_PHY	0x00000008	/* PHY reset [5210] */
#define AR5K_RESET_CTL_PCI	0x00000010	/* PCI Core reset (interrupts etc) */
#define AR5K_RESET_CTL_CHIP	(AR5K_RESET_CTL_PCU | AR5K_RESET_CTL_DMA |	\
				AR5K_RESET_CTL_MAC | AR5K_RESET_CTL_PHY)

/*
 * Sleep control register
 */
#define AR5K_SLEEP_CTL			0x4004			/* Register Address */
#define AR5K_SLEEP_CTL_SLDUR		0x0000ffff	/* Sleep duration mask */
#define AR5K_SLEEP_CTL_SLDUR_S		0
#define AR5K_SLEEP_CTL_SLE		0x00030000	/* Sleep enable mask */
#define AR5K_SLEEP_CTL_SLE_S		16
#define AR5K_SLEEP_CTL_SLE_WAKE		0x00000000	/* Force chip awake */
#define AR5K_SLEEP_CTL_SLE_SLP		0x00010000	/* Force chip sleep */
#define AR5K_SLEEP_CTL_SLE_ALLOW	0x00020000
#define AR5K_SLEEP_CTL_SLE_UNITS	0x00000008	/* [5211+] */

/*
 * Interrupt pending register
 */
#define AR5K_INTPEND	0x4008
#define AR5K_INTPEND_M	0x00000001

/*
 * Sleep force register
 */
#define AR5K_SFR	0x400c
#define AR5K_SFR_M	0x00000001

/*
 * PCI configuration register
 */
#define AR5K_PCICFG			0x4010			/* Register Address */
#define AR5K_PCICFG_EEAE		0x00000001	/* Eeprom access enable [5210] */
#define AR5K_PCICFG_CLKRUNEN		0x00000004	/* CLKRUN enable [5211+] */
#define AR5K_PCICFG_EESIZE		0x00000018	/* Mask for EEPROM size [5211+] */
#define AR5K_PCICFG_EESIZE_S		3
#define AR5K_PCICFG_EESIZE_4K		0		/* 4K */
#define AR5K_PCICFG_EESIZE_8K		1		/* 8K */
#define AR5K_PCICFG_EESIZE_16K		2		/* 16K */
#define AR5K_PCICFG_EESIZE_FAIL		3		/* Failed to get size (?) [5211+] */
#define AR5K_PCICFG_LED			0x00000060	/* Led status [5211+] */
#define AR5K_PCICFG_LED_NONE		0x00000000	/* Default [5211+] */
#define AR5K_PCICFG_LED_PEND		0x00000020	/* Scan / Auth pending */
#define AR5K_PCICFG_LED_ASSOC		0x00000040	/* Associated */
#define	AR5K_PCICFG_BUS_SEL		0x00000380	/* Mask for "bus select" [5211+] (?) */
#define	AR5K_PCICFG_CBEFIX_DIS		0x00000400	/* Disable CBE fix (?) */
#define AR5K_PCICFG_SL_INTEN		0x00000800	/* Enable interrupts when asleep (?) */
#define AR5K_PCICFG_LED_BCTL		0x00001000	/* Led blink (?) [5210] */
#define AR5K_PCICFG_SL_INPEN		0x00002800	/* Sleep even whith pending interrupts (?) */
#define AR5K_PCICFG_SPWR_DN		0x00010000	/* Mask for power status */
#define AR5K_PCICFG_LEDMODE		0x000e0000	/* Ledmode [5211+] */
#define AR5K_PCICFG_LEDMODE_PROP	0x00000000	/* Blink on standard traffic [5211+] */
#define AR5K_PCICFG_LEDMODE_PROM	0x00020000	/* Default mode (blink on any traffic) [5211+] */
#define AR5K_PCICFG_LEDMODE_PWR		0x00040000	/* Some other blinking mode  (?) [5211+] */
#define AR5K_PCICFG_LEDMODE_RAND	0x00060000	/* Random blinking (?) [5211+] */
#define AR5K_PCICFG_LEDBLINK		0x00700000
#define AR5K_PCICFG_LEDBLINK_S		20
#define AR5K_PCICFG_LEDSLOW		0x00800000	/* Slow led blink rate (?) [5211+] */
#define AR5K_PCICFG_LEDSTATE				\
	(AR5K_PCICFG_LED | AR5K_PCICFG_LEDMODE |	\
	AR5K_PCICFG_LEDBLINK | AR5K_PCICFG_LEDSLOW)

/*
 * "General Purpose Input/Output" (GPIO) control register
 *
 * I'm not sure about this but after looking at the code
 * for all chipsets here is what i got.
 *
 * We have 6 GPIOs (pins), each GPIO has 4 modes (2 bits)
 * Mode 0 -> always input
 * Mode 1 -> output when GPIODO for this GPIO is set to 0
 * Mode 2 -> output when GPIODO for this GPIO is set to 1
 * Mode 3 -> always output
 *
 * For more infos check out get_gpio/set_gpio and
 * set_gpio_input/set_gpio_output functs.
 * For more infos on gpio interrupt check out set_gpio_intr.
 */
#define AR5K_NUM_GPIO	6

#define AR5K_GPIOCR		0x4014				/* Register Address */
#define AR5K_GPIOCR_INT_ENA	0x00008000		/* Enable GPIO interrupt */
#define AR5K_GPIOCR_INT_SELL	0x00000000		/* Generate interrupt when pin is off (?) */
#define AR5K_GPIOCR_INT_SELH	0x00010000		/* Generate interrupt when pin is on */
#define AR5K_GPIOCR_IN(n)	(0 << ((n) * 2))	/* Mode 0 for pin n */
#define AR5K_GPIOCR_OUT0(n)	(1 << ((n) * 2))	/* Mode 1 for pin n */
#define AR5K_GPIOCR_OUT1(n)	(2 << ((n) * 2))	/* Mode 2 for pin n */
#define AR5K_GPIOCR_OUT(n)	(3 << ((n) * 2))	/* Mode 3 for pin n */
#define AR5K_GPIOCR_INT_SEL(n)	((n) << 12)		/* Interrupt for GPIO pin n */

/*
 * "General Purpose Input/Output" (GPIO) data output register
 */
#define AR5K_GPIODO	0x4018

/*
 * "General Purpose Input/Output" (GPIO) data input register
 */
#define AR5K_GPIODI	0x401c
#define AR5K_GPIODI_M	0x0000002f


/*
 * Silicon revision register
 */
#define AR5K_SREV		0x4020			/* Register Address */
#define AR5K_SREV_REV		0x0000000f	/* Mask for revision */
#define AR5K_SREV_REV_S		0
#define AR5K_SREV_VER		0x000000ff	/* Mask for version */
#define AR5K_SREV_VER_S		4



/*====EEPROM REGISTERS====*/

/*
 * EEPROM access registers
 *
 * Here we got a difference between 5210/5211-12
 * read data register for 5210 is at 0x6800 and
 * status register is at 0x6c00. There is also
 * no eeprom command register on 5210 and the
 * offsets are different.
 *
 * To read eeprom data for a specific offset:
 * 5210 - enable eeprom access (AR5K_PCICFG_EEAE)
 *        read AR5K_EEPROM_BASE +(4 * offset)
 *        check the eeprom status register
 *        and read eeprom data register.
 *
 * 5211 - write offset to AR5K_EEPROM_BASE
 * 5212   write AR5K_EEPROM_CMD_READ on AR5K_EEPROM_CMD
 *        check the eeprom status register
 *        and read eeprom data register.
 *
 * To write eeprom data for a specific offset:
 * 5210 - enable eeprom access (AR5K_PCICFG_EEAE)
 *        write data to AR5K_EEPROM_BASE +(4 * offset)
 *        check the eeprom status register
 * 5211 - write AR5K_EEPROM_CMD_RESET on AR5K_EEPROM_CMD
 * 5212   write offset to AR5K_EEPROM_BASE
 *        write data to data register
 *	  write AR5K_EEPROM_CMD_WRITE on AR5K_EEPROM_CMD
 *        check the eeprom status register
 *
 * For more infos check eeprom_* functs and the ar5k.c
 * file posted in madwifi-devel mailing list.
 * http://sourceforge.net/mailarchive/message.php?msg_id=8966525
 *
 */
#define AR5K_EEPROM_BASE	0x6000

/*
 * Common ar5xxx EEPROM data offsets (set these on AR5K_EEPROM_BASE)
 */
#define AR5K_EEPROM_MAGIC		0x003d	/* EEPROM Magic number */
#define AR5K_EEPROM_MAGIC_VALUE		0x5aa5	/* Default - found on EEPROM */
#define AR5K_EEPROM_MAGIC_5212		0x0000145c /* 5212 */
#define AR5K_EEPROM_MAGIC_5211		0x0000145b /* 5211 */
#define AR5K_EEPROM_MAGIC_5210		0x0000145a /* 5210 */

#define AR5K_EEPROM_PROTECT		0x003f	/* EEPROM protect status */
#define AR5K_EEPROM_PROTECT_RD_0_31	0x0001	/* Read protection bit for offsets 0x0 - 0x1f */
#define AR5K_EEPROM_PROTECT_WR_0_31	0x0002	/* Write protection bit for offsets 0x0 - 0x1f */
#define AR5K_EEPROM_PROTECT_RD_32_63	0x0004	/* 0x20 - 0x3f */
#define AR5K_EEPROM_PROTECT_WR_32_63	0x0008
#define AR5K_EEPROM_PROTECT_RD_64_127	0x0010	/* 0x40 - 0x7f */
#define AR5K_EEPROM_PROTECT_WR_64_127	0x0020
#define AR5K_EEPROM_PROTECT_RD_128_191	0x0040	/* 0x80 - 0xbf (regdom) */
#define AR5K_EEPROM_PROTECT_WR_128_191	0x0080
#define AR5K_EEPROM_PROTECT_RD_192_207	0x0100	/* 0xc0 - 0xcf */
#define AR5K_EEPROM_PROTECT_WR_192_207	0x0200
#define AR5K_EEPROM_PROTECT_RD_208_223	0x0400	/* 0xd0 - 0xdf */
#define AR5K_EEPROM_PROTECT_WR_208_223	0x0800
#define AR5K_EEPROM_PROTECT_RD_224_239	0x1000	/* 0xe0 - 0xef */
#define AR5K_EEPROM_PROTECT_WR_224_239	0x2000
#define AR5K_EEPROM_PROTECT_RD_240_255	0x4000	/* 0xf0 - 0xff */
#define AR5K_EEPROM_PROTECT_WR_240_255	0x8000
#define AR5K_EEPROM_REG_DOMAIN		0x00bf	/* EEPROM regdom */
#define AR5K_EEPROM_INFO_BASE		0x00c0	/* EEPROM header */
#define AR5K_EEPROM_INFO_MAX		(0x400 - AR5K_EEPROM_INFO_BASE)
#define AR5K_EEPROM_INFO_CKSUM		0xffff
#define AR5K_EEPROM_INFO(_n)		(AR5K_EEPROM_INFO_BASE + (_n))

#define AR5K_EEPROM_VERSION		AR5K_EEPROM_INFO(1)	/* EEPROM Version */
#define AR5K_EEPROM_VERSION_3_0		0x3000	/* No idea what's going on before this version */
#define AR5K_EEPROM_VERSION_3_1		0x3001	/* ob/db values for 2Ghz (ar5211_rfregs) */
#define AR5K_EEPROM_VERSION_3_2		0x3002	/* different frequency representation (eeprom_bin2freq) */
#define AR5K_EEPROM_VERSION_3_3		0x3003	/* offsets changed, has 32 CTLs (see below) and ee_false_detect (eeprom_read_modes) */
#define AR5K_EEPROM_VERSION_3_4		0x3004	/* has ee_i_gain ee_cck_ofdm_power_delta (eeprom_read_modes) */
#define AR5K_EEPROM_VERSION_4_0		0x4000	/* has ee_misc*, ee_cal_pier, ee_turbo_max_power and ee_xr_power (eeprom_init) */
#define AR5K_EEPROM_VERSION_4_1		0x4001	/* has ee_margin_tx_rx (eeprom_init) */
#define AR5K_EEPROM_VERSION_4_2		0x4002	/* has ee_cck_ofdm_gain_delta (eeprom_init) */
#define AR5K_EEPROM_VERSION_4_3		0x4003
#define AR5K_EEPROM_VERSION_4_4		0x4004
#define AR5K_EEPROM_VERSION_4_5		0x4005
#define AR5K_EEPROM_VERSION_4_6		0x4006	/* has ee_scaled_cck_delta */
#define AR5K_EEPROM_VERSION_4_7		0x3007

#define AR5K_EEPROM_MODE_11A		0
#define AR5K_EEPROM_MODE_11B		1
#define AR5K_EEPROM_MODE_11G		2

#define AR5K_EEPROM_HDR			AR5K_EEPROM_INFO(2)	/* Header that contains the device caps */
#define AR5K_EEPROM_HDR_11A(_v)		(((_v) >> AR5K_EEPROM_MODE_11A) & 0x1)
#define AR5K_EEPROM_HDR_11B(_v)		(((_v) >> AR5K_EEPROM_MODE_11B) & 0x1)
#define AR5K_EEPROM_HDR_11G(_v)		(((_v) >> AR5K_EEPROM_MODE_11G) & 0x1)
#define AR5K_EEPROM_HDR_T_2GHZ_DIS(_v)	(((_v) >> 3) & 0x1)	/* Disable turbo for 2Ghz (?) */
#define AR5K_EEPROM_HDR_T_5GHZ_DBM(_v)	(((_v) >> 4) & 0x7f)	/* Max turbo power for a/XR mode (eeprom_init) */
#define AR5K_EEPROM_HDR_DEVICE(_v)	(((_v) >> 11) & 0x7)
#define AR5K_EEPROM_HDR_T_5GHZ_DIS(_v)	(((_v) >> 15) & 0x1)	/* Disable turbo for 5Ghz (?) */
#define AR5K_EEPROM_HDR_RFKILL(_v)	(((_v) >> 14) & 0x1)	/* Device has RFKill support */

#define AR5K_EEPROM_RFKILL_GPIO_SEL	0x0000001c
#define AR5K_EEPROM_RFKILL_GPIO_SEL_S	2
#define AR5K_EEPROM_RFKILL_POLARITY	0x00000002
#define AR5K_EEPROM_RFKILL_POLARITY_S	1

/* Newer EEPROMs are using a different offset */
#define AR5K_EEPROM_OFF(_v, _v3_0, _v3_3) \
	(((_v) >= AR5K_EEPROM_VERSION_3_3) ? _v3_3 : _v3_0)

#define AR5K_EEPROM_ANT_GAIN(_v)	AR5K_EEPROM_OFF(_v, 0x00c4, 0x00c3)
#define AR5K_EEPROM_ANT_GAIN_5GHZ(_v)	((int8_t)(((_v) >> 8) & 0xff))
#define AR5K_EEPROM_ANT_GAIN_2GHZ(_v)	((int8_t)((_v) & 0xff))

/* calibration settings */
#define AR5K_EEPROM_MODES_11A(_v)	AR5K_EEPROM_OFF(_v, 0x00c5, 0x00d4)
#define AR5K_EEPROM_MODES_11B(_v)	AR5K_EEPROM_OFF(_v, 0x00d0, 0x00f2)
#define AR5K_EEPROM_MODES_11G(_v)	AR5K_EEPROM_OFF(_v, 0x00da, 0x010d)
#define AR5K_EEPROM_CTL(_v)		AR5K_EEPROM_OFF(_v, 0x00e4, 0x0128)	/* Conformance test limits */

/* [3.1 - 3.3] */
#define AR5K_EEPROM_OBDB0_2GHZ		0x00ec
#define AR5K_EEPROM_OBDB1_2GHZ		0x00ed

/* Misc values available since EEPROM 4.0 */
#define AR5K_EEPROM_MISC0		0x00c4
#define AR5K_EEPROM_EARSTART(_v)	((_v) & 0xfff)
#define AR5K_EEPROM_EEMAP(_v)		(((_v) >> 14) & 0x3)
#define AR5K_EEPROM_MISC1		0x00c5
#define AR5K_EEPROM_TARGET_PWRSTART(_v)	((_v) & 0xfff)
#define AR5K_EEPROM_HAS32KHZCRYSTAL(_v)	(((_v) >> 14) & 0x1)

/*
 * EEPROM data register
 */
#define AR5K_EEPROM_DATA_5211	0x6004
#define AR5K_EEPROM_DATA_5210	0x6800
#define	AR5K_EEPROM_DATA	(ah->ah_version == AR5K_AR5210 ? \
				AR5K_EEPROM_DATA_5210 : AR5K_EEPROM_DATA_5211)

/*
 * EEPROM command register
 */
#define AR5K_EEPROM_CMD		0x6008			/* Register Addres */
#define AR5K_EEPROM_CMD_READ	0x00000001	/* EEPROM read */
#define AR5K_EEPROM_CMD_WRITE	0x00000002	/* EEPROM write */
#define AR5K_EEPROM_CMD_RESET	0x00000004	/* EEPROM reset */

/*
 * EEPROM status register
 */
#define AR5K_EEPROM_STAT_5210	0x6c00			/* Register Address [5210] */
#define AR5K_EEPROM_STAT_5211	0x600c			/* Register Address [5211+] */
#define	AR5K_EEPROM_STATUS	(ah->ah_version == AR5K_AR5210 ? \
				AR5K_EEPROM_STAT_5210 : AR5K_EEPROM_STAT_5211)
#define AR5K_EEPROM_STAT_RDERR	0x00000001	/* EEPROM read failed */
#define AR5K_EEPROM_STAT_RDDONE	0x00000002	/* EEPROM read successful */
#define AR5K_EEPROM_STAT_WRERR	0x00000004	/* EEPROM write failed */
#define AR5K_EEPROM_STAT_WRDONE	0x00000008	/* EEPROM write successful */

/*
 * EEPROM config register (?)
 */
#define AR5K_EEPROM_CFG	0x6010



/*
 * Protocol Control Unit (PCU) registers
 */
/*
 * Used for checking initial register writes
 * during channel reset (see reset func)
 */
#define AR5K_PCU_MIN	0x8000
#define AR5K_PCU_MAX	0x8fff

/*
 * First station id register (MAC address in lower 32 bits)
 */
#define AR5K_STA_ID0	0x8000

/*
 * Second station id register (MAC address in upper 16 bits)
 */
#define AR5K_STA_ID1			0x8004			/* Register Address */
#define AR5K_STA_ID1_AP			0x00010000	/* Set AP mode */
#define AR5K_STA_ID1_ADHOC		0x00020000	/* Set Ad-Hoc mode */
#define AR5K_STA_ID1_PWR_SV		0x00040000	/* Power save reporting (?) */
#define AR5K_STA_ID1_NO_KEYSRCH		0x00080000	/* No key search */
#define AR5K_STA_ID1_NO_PSPOLL		0x00100000	/* No power save polling [5210] */
#define AR5K_STA_ID1_PCF_5211		0x00100000	/* Enable PCF on [5211+] */
#define AR5K_STA_ID1_PCF_5210		0x00200000	/* Enable PCF on [5210]*/
#define	AR5K_STA_ID1_PCF		(ah->ah_version == AR5K_AR5210 ? \
					AR5K_STA_ID1_PCF_5210 : AR5K_STA_ID1_PCF_5211)
#define AR5K_STA_ID1_DEFAULT_ANTENNA	0x00200000	/* Use default antenna */
#define AR5K_STA_ID1_DESC_ANTENNA	0x00400000	/* Update antenna from descriptor */
#define AR5K_STA_ID1_RTS_DEF_ANTENNA	0x00800000	/* Use default antenna for RTS (?) */
#define AR5K_STA_ID1_ACKCTS_6MB		0x01000000	/* Use 6Mbit/s for ACK/CTS (?) */
#define AR5K_STA_ID1_BASE_RATE_11B	0x02000000	/* Use 11b base rate (for ACK/CTS ?) [5211+] */

/*
 * First BSSID register (MAC address, lower 32bits)
 */
#define AR5K_BSS_ID0	0x8008

/*
 * Second BSSID register (MAC address in upper 16 bits)
 *
 * AID: Association ID
 */
#define AR5K_BSS_ID1		0x800c
#define AR5K_BSS_ID1_AID	0xffff0000
#define AR5K_BSS_ID1_AID_S	16

/*
 * Backoff slot time register
 */
#define AR5K_SLOT_TIME	0x8010

/*
 * ACK/CTS timeout register
 */
#define AR5K_TIME_OUT		0x8014			/* Register Address */
#define AR5K_TIME_OUT_ACK	0x00001fff	/* ACK timeout mask */
#define AR5K_TIME_OUT_ACK_S	0
#define AR5K_TIME_OUT_CTS	0x1fff0000	/* CTS timeout mask */
#define AR5K_TIME_OUT_CTS_S	16

/*
 * RSSI threshold register
 */
#define AR5K_RSSI_THR			0x8018		/* Register Address */
#define AR5K_RSSI_THR_M			0x000000ff	/* Mask for RSSI threshold [5211+] */
#define AR5K_RSSI_THR_BMISS_5210	0x00000700	/* Mask for Beacon Missed threshold [5210] */
#define AR5K_RSSI_THR_BMISS_5210_S	8
#define AR5K_RSSI_THR_BMISS_5211	0x0000ff00	/* Mask for Beacon Missed threshold [5211+] */
#define AR5K_RSSI_THR_BMISS_5211_S	8
#define	AR5K_RSSI_THR_BMISS		(ah->ah_version == AR5K_AR5210 ? \
					AR5K_RSSI_THR_BMISS_5210 : AR5K_RSSI_THR_BMISS_5211)
#define	AR5K_RSSI_THR_BMISS_S		8

/*
 * 5210 has more PCU registers because there is no QCU/DCU
 * so queue parameters are set here, this way a lot common
 * registers have different address for 5210. To make things
 * easier we define a macro based on ah->ah_version for common
 * registers with different addresses and common flags.
 */

/*
 * Retry limit register
 *
 * Retry limit register for 5210 (no QCU/DCU so it's done in PCU)
 */
#define AR5K_NODCU_RETRY_LMT		0x801c			/*Register Address */
#define AR5K_NODCU_RETRY_LMT_SH_RETRY	0x0000000f	/* Short retry limit mask */
#define AR5K_NODCU_RETRY_LMT_SH_RETRY_S	0
#define AR5K_NODCU_RETRY_LMT_LG_RETRY	0x000000f0	/* Long retry mask */
#define AR5K_NODCU_RETRY_LMT_LG_RETRY_S	4
#define AR5K_NODCU_RETRY_LMT_SSH_RETRY	0x00003f00	/* Station short retry limit mask */
#define AR5K_NODCU_RETRY_LMT_SSH_RETRY_S	8
#define AR5K_NODCU_RETRY_LMT_SLG_RETRY	0x000fc000	/* Station long retry limit mask */
#define AR5K_NODCU_RETRY_LMT_SLG_RETRY_S	14
#define AR5K_NODCU_RETRY_LMT_CW_MIN	0x3ff00000	/* Minimum contention window mask */
#define AR5K_NODCU_RETRY_LMT_CW_MIN_S	20

/*
 * Transmit latency register
 */
#define AR5K_USEC_5210			0x8020			/* Register Address [5210] */
#define AR5K_USEC_5211			0x801c			/* Register Address [5211+] */
#define AR5K_USEC			(ah->ah_version == AR5K_AR5210 ? \
					AR5K_USEC_5210 : AR5K_USEC_5211)
#define AR5K_USEC_1			0x0000007f
#define AR5K_USEC_1_S			0
#define AR5K_USEC_32			0x00003f80
#define AR5K_USEC_32_S			7
#define AR5K_USEC_TX_LATENCY_5211	0x007fc000
#define AR5K_USEC_TX_LATENCY_5211_S	14
#define AR5K_USEC_RX_LATENCY_5211	0x1f800000
#define AR5K_USEC_RX_LATENCY_5211_S	23
#define AR5K_USEC_TX_LATENCY_5210	0x000fc000	/* also for 5311 */
#define AR5K_USEC_TX_LATENCY_5210_S	14
#define AR5K_USEC_RX_LATENCY_5210	0x03f00000	/* also for 5311 */
#define AR5K_USEC_RX_LATENCY_5210_S	20

/*
 * PCU beacon control register
 */
#define AR5K_BEACON_5210	0x8024
#define AR5K_BEACON_5211	0x8020
#define AR5K_BEACON		(ah->ah_version == AR5K_AR5210 ? \
				AR5K_BEACON_5210 : AR5K_BEACON_5211)
#define AR5K_BEACON_PERIOD	0x0000ffff
#define AR5K_BEACON_PERIOD_S	0
#define AR5K_BEACON_TIM		0x007f0000
#define AR5K_BEACON_TIM_S	16
#define AR5K_BEACON_ENABLE	0x00800000
#define AR5K_BEACON_RESET_TSF	0x01000000

/*
 * CFP period register
 */
#define AR5K_CFP_PERIOD_5210	0x8028
#define AR5K_CFP_PERIOD_5211	0x8024
#define AR5K_CFP_PERIOD		(ah->ah_version == AR5K_AR5210 ? \
				AR5K_CFP_PERIOD_5210 : AR5K_CFP_PERIOD_5211)

/*
 * Next beacon time register
 */
#define AR5K_TIMER0_5210	0x802c
#define AR5K_TIMER0_5211	0x8028
#define AR5K_TIMER0		(ah->ah_version == AR5K_AR5210 ? \
				AR5K_TIMER0_5210 : AR5K_TIMER0_5211)

/*
 * Next DMA beacon alert register
 */
#define AR5K_TIMER1_5210	0x8030
#define AR5K_TIMER1_5211	0x802c
#define AR5K_TIMER1		(ah->ah_version == AR5K_AR5210 ? \
				AR5K_TIMER1_5210 : AR5K_TIMER1_5211)

/*
 * Next software beacon alert register
 */
#define AR5K_TIMER2_5210	0x8034
#define AR5K_TIMER2_5211	0x8030
#define AR5K_TIMER2		(ah->ah_version == AR5K_AR5210 ? \
				AR5K_TIMER2_5210 : AR5K_TIMER2_5211)

/*
 * Next ATIM window time register
 */
#define AR5K_TIMER3_5210	0x8038
#define AR5K_TIMER3_5211	0x8034
#define AR5K_TIMER3		(ah->ah_version == AR5K_AR5210 ? \
				AR5K_TIMER3_5210 : AR5K_TIMER3_5211)


/*
 * 5210 First inter frame spacing register (IFS)
 */
#define AR5K_IFS0		0x8040
#define AR5K_IFS0_SIFS		0x000007ff
#define AR5K_IFS0_SIFS_S	0
#define AR5K_IFS0_DIFS		0x007ff800
#define AR5K_IFS0_DIFS_S	11

/*
 * 5210 Second inter frame spacing register (IFS)
 */
#define AR5K_IFS1		0x8044
#define AR5K_IFS1_PIFS		0x00000fff
#define AR5K_IFS1_PIFS_S	0
#define AR5K_IFS1_EIFS		0x03fff000
#define AR5K_IFS1_EIFS_S	12
#define AR5K_IFS1_CS_EN		0x04000000


/*
 * CFP duration register
 */
#define AR5K_CFP_DUR_5210	0x8048
#define AR5K_CFP_DUR_5211	0x8038
#define AR5K_CFP_DUR		(ah->ah_version == AR5K_AR5210 ? \
				AR5K_CFP_DUR_5210 : AR5K_CFP_DUR_5211)

/*
 * Receive filter register
 * TODO: Get these out of ar5xxx.h on ath5k
 */
#define AR5K_RX_FILTER_5210	0x804c			/* Register Address [5210] */
#define AR5K_RX_FILTER_5211	0x803c			/* Register Address [5211+] */
#define AR5K_RX_FILTER		(ah->ah_version == AR5K_AR5210 ? \
				AR5K_RX_FILTER_5210 : AR5K_RX_FILTER_5211)
#define	AR5K_RX_FILTER_UCAST 	0x00000001	/* Don't filter unicast frames */
#define	AR5K_RX_FILTER_MCAST 	0x00000002	/* Don't filter multicast frames */
#define	AR5K_RX_FILTER_BCAST 	0x00000004	/* Don't filter broadcast frames */
#define	AR5K_RX_FILTER_CONTROL 	0x00000008	/* Don't filter control frames */
#define	AR5K_RX_FILTER_BEACON 	0x00000010	/* Don't filter beacon frames */
#define	AR5K_RX_FILTER_PROM 	0x00000020	/* Set promiscuous mode */
#define	AR5K_RX_FILTER_XRPOLL 	0x00000040	/* Don't filter XR poll frame [5212+] */
#define	AR5K_RX_FILTER_PROBEREQ 0x00000080	/* Don't filter probe requests [5212+] */
#define	AR5K_RX_FILTER_PHYERR_5212	0x00000100	/* Don't filter phy errors [5212+] */
#define	AR5K_RX_FILTER_RADARERR_5212 	0x00000200	/* Don't filter phy radar errors [5212+] */
#define AR5K_RX_FILTER_PHYERR_5211	0x00000040	/* [5211] */
#define AR5K_RX_FILTER_RADARERR_5211	0x00000080	/* [5211] */
#define AR5K_RX_FILTER_PHYERR  \
	((ah->ah_version == AR5K_AR5211 ? \
	AR5K_RX_FILTER_PHYERR_5211 : AR5K_RX_FILTER_PHYERR_5212))
#define        AR5K_RX_FILTER_RADARERR \
	((ah->ah_version == AR5K_AR5211 ? \
	AR5K_RX_FILTER_RADARERR_5211 : AR5K_RX_FILTER_RADARERR_5212))

/*
 * Multicast filter register (lower 32 bits)
 */
#define AR5K_MCAST_FILTER0_5210	0x8050
#define AR5K_MCAST_FILTER0_5211	0x8040
#define AR5K_MCAST_FILTER0	(ah->ah_version == AR5K_AR5210 ? \
				AR5K_MCAST_FILTER0_5210 : AR5K_MCAST_FILTER0_5211)

/*
 * Multicast filter register (higher 16 bits)
 */
#define AR5K_MCAST_FILTER1_5210	0x8054
#define AR5K_MCAST_FILTER1_5211	0x8044
#define AR5K_MCAST_FILTER1	(ah->ah_version == AR5K_AR5210 ? \
				AR5K_MCAST_FILTER1_5210 : AR5K_MCAST_FILTER1_5211)


/*
 * Transmit mask register (lower 32 bits) [5210]
 */
#define AR5K_TX_MASK0	0x8058

/*
 * Transmit mask register (higher 16 bits) [5210]
 */
#define AR5K_TX_MASK1	0x805c

/*
 * Clear transmit mask [5210]
 */
#define AR5K_CLR_TMASK	0x8060

/*
 * Trigger level register (before transmission) [5210]
 */
#define AR5K_TRIG_LVL	0x8064


/*
 * PCU control register
 *
 * Only DIS_RX is used in the code, the rest i guess are
 * for tweaking/diagnostics.
 */
#define AR5K_DIAG_SW_5210		0x8068			/* Register Address [5210] */
#define AR5K_DIAG_SW_5211		0x8048			/* Register Address [5211+] */
#define AR5K_DIAG_SW			(ah->ah_version == AR5K_AR5210 ? \
					AR5K_DIAG_SW_5210 : AR5K_DIAG_SW_5211)
#define AR5K_DIAG_SW_DIS_WEP_ACK	0x00000001
#define AR5K_DIAG_SW_DIS_ACK		0x00000002	/* Disable ACKs (?) */
#define AR5K_DIAG_SW_DIS_CTS		0x00000004	/* Disable CTSs (?) */
#define AR5K_DIAG_SW_DIS_ENC		0x00000008	/* Disable encryption (?) */
#define AR5K_DIAG_SW_DIS_DEC		0x00000010	/* Disable decryption (?) */
#define AR5K_DIAG_SW_DIS_TX		0x00000020	/* Disable transmit [5210] */
#define AR5K_DIAG_SW_DIS_RX_5210	0x00000040	/* Disable recieve */
#define AR5K_DIAG_SW_DIS_RX_5211	0x00000020
#define	AR5K_DIAG_SW_DIS_RX		(ah->ah_version == AR5K_AR5210 ? \
					AR5K_DIAG_SW_DIS_RX_5210 : AR5K_DIAG_SW_DIS_RX_5211)
#define AR5K_DIAG_SW_LOOP_BACK_5210	0x00000080	/* Loopback (i guess it goes with DIS_TX) [5210] */
#define AR5K_DIAG_SW_LOOP_BACK_5211	0x00000040
#define AR5K_DIAG_SW_LOOP_BACK		(ah->ah_version == AR5K_AR5210 ? \
					AR5K_DIAG_SW_LOOP_BACK_5210 : AR5K_DIAG_SW_LOOP_BACK_5211)
#define AR5K_DIAG_SW_CORR_FCS_5210	0x00000100
#define AR5K_DIAG_SW_CORR_FCS_5211	0x00000080
#define AR5K_DIAG_SW_CORR_FCS		(ah->ah_version == AR5K_AR5210 ? \
					AR5K_DIAG_SW_CORR_FCS_5210 : AR5K_DIAG_SW_CORR_FCS_5211)
#define AR5K_DIAG_SW_CHAN_INFO_5210	0x00000200
#define AR5K_DIAG_SW_CHAN_INFO_5211	0x00000100
#define AR5K_DIAG_SW_CHAN_INFO		(ah->ah_version == AR5K_AR5210 ? \
					AR5K_DIAG_SW_CHAN_INFO_5210 : AR5K_DIAG_SW_CHAN_INFO_5211)
#define AR5K_DIAG_SW_EN_SCRAM_SEED_5211	0x00000200	/* Scrambler seed (?) */
#define AR5K_DIAG_SW_EN_SCRAM_SEED_5210	0x00000400
#define AR5K_DIAG_SW_EN_SCRAM_SEED	(ah->ah_version == AR5K_AR5210 ? \
					AR5K_DIAG_SW_EN_SCRAM_SEED_5210 : AR5K_DIAG_SW_EN_SCRAM_SEED_5211)
#define AR5K_DIAG_SW_ECO_ENABLE		0x00000400	/* [5211+] */
#define AR5K_DIAG_SW_SCVRAM_SEED	0x0003f800	/* [5210] */
#define AR5K_DIAG_SW_SCRAM_SEED_M	0x0001fc00	/* Scrambler seed mask (?) */
#define AR5K_DIAG_SW_SCRAM_SEED_S	10
#define AR5K_DIAG_SW_DIS_SEQ_INC	0x00040000	/* Disable seqnum increment (?)[5210] */
#define AR5K_DIAG_SW_FRAME_NV0_5210	0x00080000
#define AR5K_DIAG_SW_FRAME_NV0_5211	0x00020000
#define	AR5K_DIAG_SW_FRAME_NV0		(ah->ah_version == AR5K_AR5210 ? \
					AR5K_DIAG_SW_FRAME_NV0_5210 : AR5K_DIAG_SW_FRAME_NV0_5211)
#define AR5K_DIAG_SW_OBSPT_M		0x000c0000
#define AR5K_DIAG_SW_OBSPT_S		18

/*
 * TSF (clock) register (lower 32 bits)
 */
#define AR5K_TSF_L32_5210	0x806c
#define AR5K_TSF_L32_5211	0x804c
#define	AR5K_TSF_L32		(ah->ah_version == AR5K_AR5210 ? \
				AR5K_TSF_L32_5210 : AR5K_TSF_L32_5211)

/*
 * TSF (clock) register (higher 32 bits)
 */
#define AR5K_TSF_U32_5210	0x8070
#define AR5K_TSF_U32_5211	0x8050
#define	AR5K_TSF_U32		(ah->ah_version == AR5K_AR5210 ? \
				AR5K_TSF_U32_5210 : AR5K_TSF_U32_5211)

/*
 * Last beacon timestamp register
 */
#define AR5K_LAST_TSTP	0x8080

/*
 * ADDAC test register [5211+]
 */
#define AR5K_ADDAC_TEST	0x8054
#define AR5K_ADDAC_TEST_TXCONT 0x00000001

/*
 * Default antenna register [5211+]
 */
#define AR5K_DEFAULT_ANTENNA	0x8058



/*
 * Retry count register [5210]
 */
#define AR5K_RETRY_CNT		0x8084			/* Register Address [5210] */
#define AR5K_RETRY_CNT_SSH	0x0000003f	/* Station short retry count (?) */
#define AR5K_RETRY_CNT_SLG	0x00000fc0	/* Station long retry count (?) */

/*
 * Back-off status register [5210]
 */
#define AR5K_BACKOFF		0x8088			/* Register Address [5210] */
#define AR5K_BACKOFF_CW		0x000003ff	/* Backoff Contention Window (?) */
#define AR5K_BACKOFF_CNT	0x03ff0000	/* Backoff count (?) */



/*
 * NAV register (current)
 */
#define AR5K_NAV_5210		0x808c
#define AR5K_NAV_5211		0x8084
#define	AR5K_NAV		(ah->ah_version == AR5K_AR5210 ? \
				AR5K_NAV_5210 : AR5K_NAV_5211)

/*
 * RTS success register
 */
#define AR5K_RTS_OK_5210	0x8090
#define AR5K_RTS_OK_5211	0x8088
#define	AR5K_RTS_OK		(ah->ah_version == AR5K_AR5210 ? \
				AR5K_RTS_OK_5210 : AR5K_RTS_OK_5211)

/*
 * RTS failure register
 */
#define AR5K_RTS_FAIL_5210	0x8094
#define AR5K_RTS_FAIL_5211	0x808c
#define	AR5K_RTS_FAIL		(ah->ah_version == AR5K_AR5210 ? \
				AR5K_RTS_FAIL_5210 : AR5K_RTS_FAIL_5211)

/*
 * ACK failure register
 */
#define AR5K_ACK_FAIL_5210	0x8098
#define AR5K_ACK_FAIL_5211	0x8090
#define	AR5K_ACK_FAIL		(ah->ah_version == AR5K_AR5210 ? \
				AR5K_ACK_FAIL_5210 : AR5K_ACK_FAIL_5211)

/*
 * FCS failure register
 */
#define AR5K_FCS_FAIL_5210	0x809c
#define AR5K_FCS_FAIL_5211	0x8094
#define	AR5K_FCS_FAIL		(ah->ah_version == AR5K_AR5210 ? \
				AR5K_FCS_FAIL_5210 : AR5K_FCS_FAIL_5211)

/*
 * Beacon count register
 */
#define AR5K_BEACON_CNT_5210	0x80a0
#define AR5K_BEACON_CNT_5211	0x8098
#define	AR5K_BEACON_CNT		(ah->ah_version == AR5K_AR5210 ? \
				AR5K_BEACON_CNT_5210 : AR5K_BEACON_CNT_5211)


/*===5212 Specific PCU registers===*/

/*
 * XR (eXtended Range) mode register
 */
#define AR5K_XRMODE			0x80c0
#define	AR5K_XRMODE_POLL_TYPE_M		0x0000003f
#define	AR5K_XRMODE_POLL_TYPE_S		0
#define	AR5K_XRMODE_POLL_SUBTYPE_M	0x0000003c
#define	AR5K_XRMODE_POLL_SUBTYPE_S	2
#define	AR5K_XRMODE_POLL_WAIT_ALL	0x00000080
#define	AR5K_XRMODE_SIFS_DELAY		0x000fff00
#define	AR5K_XRMODE_FRAME_HOLD_M	0xfff00000
#define	AR5K_XRMODE_FRAME_HOLD_S	20

/*
 * XR delay register
 */
#define AR5K_XRDELAY			0x80c4
#define AR5K_XRDELAY_SLOT_DELAY_M	0x0000ffff
#define AR5K_XRDELAY_SLOT_DELAY_S	0
#define AR5K_XRDELAY_CHIRP_DELAY_M	0xffff0000
#define AR5K_XRDELAY_CHIRP_DELAY_S	16

/*
 * XR timeout register
 */
#define AR5K_XRTIMEOUT			0x80c8
#define AR5K_XRTIMEOUT_CHIRP_M		0x0000ffff
#define AR5K_XRTIMEOUT_CHIRP_S		0
#define AR5K_XRTIMEOUT_POLL_M		0xffff0000
#define AR5K_XRTIMEOUT_POLL_S		16

/*
 * XR chirp register
 */
#define AR5K_XRCHIRP			0x80cc
#define AR5K_XRCHIRP_SEND		0x00000001
#define AR5K_XRCHIRP_GAP		0xffff0000

/*
 * XR stomp register
 */
#define AR5K_XRSTOMP			0x80d0
#define AR5K_XRSTOMP_TX			0x00000001
#define AR5K_XRSTOMP_RX_ABORT		0x00000002
#define AR5K_XRSTOMP_RSSI_THRES		0x0000ff00

/*
 * First enhanced sleep register
 */
#define AR5K_SLEEP0			0x80d4
#define AR5K_SLEEP0_NEXT_DTIM		0x0007ffff
#define AR5K_SLEEP0_NEXT_DTIM_S		0
#define AR5K_SLEEP0_ASSUME_DTIM		0x00080000
#define AR5K_SLEEP0_ENH_SLEEP_EN	0x00100000
#define AR5K_SLEEP0_CABTO		0xff000000
#define AR5K_SLEEP0_CABTO_S		24

/*
 * Second enhanced sleep register
 */
#define AR5K_SLEEP1			0x80d8
#define AR5K_SLEEP1_NEXT_TIM		0x0007ffff
#define AR5K_SLEEP1_NEXT_TIM_S		0
#define AR5K_SLEEP1_BEACON_TO		0xff000000
#define AR5K_SLEEP1_BEACON_TO_S		24

/*
 * Third enhanced sleep register
 */
#define AR5K_SLEEP2			0x80dc
#define AR5K_SLEEP2_TIM_PER		0x0000ffff
#define AR5K_SLEEP2_TIM_PER_S		0
#define AR5K_SLEEP2_DTIM_PER		0xffff0000
#define AR5K_SLEEP2_DTIM_PER_S		16

/*
 * BSSID mask registers
 */
#define AR5K_BSS_IDM0			0x80e0
#define AR5K_BSS_IDM1			0x80e4

/*
 * TX power control (TPC) register
 */
#define AR5K_TXPC			0x80e8
#define AR5K_TXPC_ACK_M			0x0000003f
#define AR5K_TXPC_ACK_S			0
#define AR5K_TXPC_CTS_M			0x00003f00
#define AR5K_TXPC_CTS_S			8
#define AR5K_TXPC_CHIRP_M		0x003f0000
#define AR5K_TXPC_CHIRP_S		22

/*
 * Profile count registers
 */
#define AR5K_PROFCNT_TX			0x80ec
#define AR5K_PROFCNT_RX			0x80f0
#define AR5K_PROFCNT_RXCLR		0x80f4
#define AR5K_PROFCNT_CYCLE		0x80f8

/*
 * TSF parameter register
 */
#define AR5K_TSF_PARM			0x8104
#define AR5K_TSF_PARM_INC_M		0x000000ff
#define AR5K_TSF_PARM_INC_S		0

/*
 * PHY error filter register
 */
#define AR5K_PHY_ERR_FIL		0x810c
#define AR5K_PHY_ERR_FIL_RADAR		0x00000020
#define AR5K_PHY_ERR_FIL_OFDM		0x00020000
#define AR5K_PHY_ERR_FIL_CCK		0x02000000

/*
 * Rate duration register
 */
#define AR5K_RATE_DUR_BASE		0x8700
#define AR5K_RATE_DUR(_n)		(AR5K_RATE_DUR_BASE + ((_n) << 2))

/*===5212 end===*/

/*
 * Key table (WEP) register
 */
#define AR5K_KEYTABLE_0_5210		0x9000
#define AR5K_KEYTABLE_0_5211		0x8800
#define AR5K_KEYTABLE_5210(_n)		(AR5K_KEYTABLE_0_5210 + ((_n) << 5))
#define AR5K_KEYTABLE_5211(_n)		(AR5K_KEYTABLE_0_5211 + ((_n) << 5))
#define	AR5K_KEYTABLE(_n)		(ah->ah_version == AR5K_AR5210 ? \
					AR5K_KEYTABLE_5210(_n) : AR5K_KEYTABLE_5211(_n))
#define AR5K_KEYTABLE_OFF(_n, x)	(AR5K_KEYTABLE(_n) + (x << 2))
#define AR5K_KEYTABLE_TYPE(_n)		AR5K_KEYTABLE_OFF(_n, 5)
#define AR5K_KEYTABLE_TYPE_40		0x00000000
#define AR5K_KEYTABLE_TYPE_104		0x00000001
#define AR5K_KEYTABLE_TYPE_128		0x00000003
#define AR5K_KEYTABLE_TYPE_TKIP		0x00000004	/* [5212+] */
#define AR5K_KEYTABLE_TYPE_AES		0x00000005	/* [5211+] */
#define AR5K_KEYTABLE_TYPE_CCM		0x00000006	/* [5212+] */
#define AR5K_KEYTABLE_TYPE_NULL		0x00000007	/* [5211+] */
#define AR5K_KEYTABLE_ANTENNA		0x00000008	/* [5212+] */
#define AR5K_KEYTABLE_MAC0(_n)		AR5K_KEYTABLE_OFF(_n, 6)
#define AR5K_KEYTABLE_MAC1(_n)		AR5K_KEYTABLE_OFF(_n, 7)
#define AR5K_KEYTABLE_VALID		0x00008000

/* WEP 40-bit	= 40-bit  entered key + 24 bit IV = 64-bit
 * WEP 104-bit	= 104-bit entered key + 24-bit IV = 128-bit
 * WEP 128-bit	= 128-bit entered key + 24 bit IV = 152-bit
 *
 * Some vendors have introduced bigger WEP keys to address
 * security vulnerabilities in WEP. This includes:
 *
 * WEP 232-bit = 232-bit entered key + 24 bit IV = 256-bit
 *
 * We can expand this if we find ar5k Atheros cards with a larger
 * key table size.
 */
#define AR5K_KEYTABLE_SIZE_5210		64
#define AR5K_KEYTABLE_SIZE_5211		128
#define	AR5K_KEYTABLE_SIZE		(ah->ah_version == AR5K_AR5210 ? \
					AR5K_KEYTABLE_SIZE_5210 : AR5K_KEYTABLE_SIZE_5211)


/*===PHY REGISTERS===*/

/*
 * PHY register
 */
#define	AR5K_PHY_BASE			0x9800
#define	AR5K_PHY(_n)			(AR5K_PHY_BASE + ((_n) << 2))
#define AR5K_PHY_SHIFT_2GHZ		0x00004007
#define AR5K_PHY_SHIFT_5GHZ		0x00000007

/*
 * PHY frame control register [5110] /turbo mode register [5111+]
 *
 * There is another frame control register for [5111+]
 * at address 0x9944 (see below) but the 2 first flags
 * are common here between 5110 frame control register
 * and [5111+] turbo mode register, so this also works as
 * a "turbo mode register" for 5110. We treat this one as
 * a frame control register for 5110 below.
 */
#define	AR5K_PHY_TURBO			0x9804
#define	AR5K_PHY_TURBO_MODE		0x00000001
#define	AR5K_PHY_TURBO_SHORT		0x00000002

/*
 * PHY agility command register
 */
#define	AR5K_PHY_AGC			0x9808
#define	AR5K_PHY_AGC_DISABLE		0x08000000

/*
 * PHY timing register [5112+]
 */
#define	AR5K_PHY_TIMING_3		0x9814
#define	AR5K_PHY_TIMING_3_DSC_MAN	0xfffe0000
#define	AR5K_PHY_TIMING_3_DSC_MAN_S	17
#define	AR5K_PHY_TIMING_3_DSC_EXP	0x0001e000
#define	AR5K_PHY_TIMING_3_DSC_EXP_S	13

/*
 * PHY chip revision register
 */
#define	AR5K_PHY_CHIP_ID		0x9818

/*
 * PHY activation register
 */
#define	AR5K_PHY_ACT			0x981c
#define	AR5K_PHY_ACT_ENABLE		0x00000001
#define	AR5K_PHY_ACT_DISABLE		0x00000002

/*
 * PHY signal register
 */
#define	AR5K_PHY_SIG			0x9858
#define	AR5K_PHY_SIG_FIRSTEP		0x0003f000
#define	AR5K_PHY_SIG_FIRSTEP_S		12
#define	AR5K_PHY_SIG_FIRPWR		0x03fc0000
#define	AR5K_PHY_SIG_FIRPWR_S		18

/*
 * PHY coarse agility control register
 */
#define	AR5K_PHY_AGCCOARSE		0x985c
#define	AR5K_PHY_AGCCOARSE_LO		0x00007f80
#define	AR5K_PHY_AGCCOARSE_LO_S		7
#define	AR5K_PHY_AGCCOARSE_HI		0x003f8000
#define	AR5K_PHY_AGCCOARSE_HI_S		15

/*
 * PHY agility control register
 */
#define	AR5K_PHY_AGCCTL			0x9860			/* Register address */
#define	AR5K_PHY_AGCCTL_CAL		0x00000001	/* Enable PHY calibration */
#define	AR5K_PHY_AGCCTL_NF		0x00000002	/* Enable Noise Floor calibration */

/*
 * PHY noise floor status register
 */
#define AR5K_PHY_NF			0x9864
#define AR5K_PHY_NF_M			0x000001ff
#define AR5K_PHY_NF_ACTIVE		0x00000100
#define AR5K_PHY_NF_RVAL(_n)		(((_n) >> 19) & AR5K_PHY_NF_M)
#define AR5K_PHY_NF_AVAL(_n)		(-((_n) ^ AR5K_PHY_NF_M) + 1)
#define AR5K_PHY_NF_SVAL(_n)		(((_n) & AR5K_PHY_NF_M) | (1 << 9))

/*
 * PHY ADC saturation register [5110]
 */
#define	AR5K_PHY_ADCSAT			0x9868
#define	AR5K_PHY_ADCSAT_ICNT		0x0001f800
#define	AR5K_PHY_ADCSAT_ICNT_S		11
#define	AR5K_PHY_ADCSAT_THR		0x000007e0
#define	AR5K_PHY_ADCSAT_THR_S		5

/*
 * PHY sleep registers [5112+]
 */
#define AR5K_PHY_SCR			0x9870
#define AR5K_PHY_SCR_32MHZ		0x0000001f
#define AR5K_PHY_SLMT			0x9874
#define AR5K_PHY_SLMT_32MHZ		0x0000007f
#define AR5K_PHY_SCAL			0x9878
#define AR5K_PHY_SCAL_32MHZ		0x0000000e

/*
 * PHY PLL (Phase Locked Loop) control register
 */
#define	AR5K_PHY_PLL			0x987c
#define	AR5K_PHY_PLL_20MHZ		0x13	/* For half rate (?) [5111+] */
#define	AR5K_PHY_PLL_40MHZ_5211		0x18	/* For 802.11a */
#define	AR5K_PHY_PLL_40MHZ_5212		0x000000aa
#define	AR5K_PHY_PLL_40MHZ		(ah->ah_version == AR5K_AR5211 ? \
					AR5K_PHY_PLL_40MHZ_5211 : AR5K_PHY_PLL_40MHZ_5212)
#define	AR5K_PHY_PLL_44MHZ_5211		0x19	/* For 802.11b/g */
#define	AR5K_PHY_PLL_44MHZ_5212		0x000000ab
#define	AR5K_PHY_PLL_44MHZ		(ah->ah_version == AR5K_AR5211 ? \
					AR5K_PHY_PLL_44MHZ_5211 : AR5K_PHY_PLL_44MHZ_5212)
#define AR5K_PHY_PLL_RF5111		0x00000000
#define AR5K_PHY_PLL_RF5112		0x00000040

/*
 * RF Buffer register
 *
 * There are some special control registers on the RF chip
 * that hold various operation settings related mostly to
 * the analog parts (channel, gain adjustment etc).
 *
 * We don't write on those registers directly but
 * we send a data packet on the buffer register and
 * then write on another special register to notify hw
 * to apply the settings. This is done so that control registers
 * can be dynamicaly programmed during operation and the settings
 * are applied faster on the hw.
 *
 * We sent such data packets during rf initialization and channel change
 * through ath5k_hw_rf*_rfregs and ath5k_hw_rf*_channel functions.
 *
 * The data packets we send during initializadion are inside ath5k_ini_rf
 * struct (see ath5k_hw.h) and each one is related to an "rf register bank".
 * We use *rfregs functions to modify them  acording to current operation
 * mode and eeprom values and pass them all together to the chip.
 *
 * It's obvious from the code that 0x989c is the buffer register but
 * for the other special registers that we write to after sending each
 * packet, i have no idea. So i'll name them BUFFER_CONTROL_X registers
 * for now. It's interesting that they are also used for some other operations.
 *
 * Also check out hw.h and U.S. Patent 6677779 B1 (about buffer
 * registers and control registers):
 *
 * http://www.google.com/patents?id=qNURAAAAEBAJ
 */

#define AR5K_RF_BUFFER			0x989c
#define AR5K_RF_BUFFER_CONTROL_0	0x98c0	/* Channel on 5110 */
#define AR5K_RF_BUFFER_CONTROL_1	0x98c4	/* Bank 7 on 5112 */
#define AR5K_RF_BUFFER_CONTROL_2	0x98cc	/* Bank 7 on 5111 */

#define AR5K_RF_BUFFER_CONTROL_3	0x98d0	/* Bank 2 on 5112 */
						/* Channel set on 5111 */
						/* Used to read radio revision*/

#define AR5K_RF_BUFFER_CONTROL_4	0x98d4  /* RF Stage register on 5110 */
						/* Bank 0,1,2,6 on 5111 */
						/* Bank 1 on 5112 */
						/* Used during activation on 5111 */

#define AR5K_RF_BUFFER_CONTROL_5	0x98d8	/* Bank 3 on 5111 */
						/* Used during activation on 5111 */
						/* Channel on 5112 */
						/* Bank 6 on 5112 */

#define AR5K_RF_BUFFER_CONTROL_6	0x98dc	/* Bank 3 on 5112 */

/*
 * PHY RF stage register [5210]
 */
#define AR5K_PHY_RFSTG			0x98d4
#define AR5K_PHY_RFSTG_DISABLE		0x00000021

/*
 * PHY receiver delay register [5111+]
 */
#define	AR5K_PHY_RX_DELAY		0x9914
#define	AR5K_PHY_RX_DELAY_M		0x00003fff

/*
 * PHY timing I(nphase) Q(adrature) control register [5111+]
 */
#define	AR5K_PHY_IQ			0x9920		/* Register address */
#define	AR5K_PHY_IQ_CORR_Q_Q_COFF	0x0000001f	/* Mask for q correction info */
#define	AR5K_PHY_IQ_CORR_Q_I_COFF	0x000007e0	/* Mask for i correction info */
#define	AR5K_PHY_IQ_CORR_Q_I_COFF_S	5
#define	AR5K_PHY_IQ_CORR_ENABLE		0x00000800	/* Enable i/q correction */
#define	AR5K_PHY_IQ_CAL_NUM_LOG_MAX	0x0000f000
#define	AR5K_PHY_IQ_CAL_NUM_LOG_MAX_S	12
#define	AR5K_PHY_IQ_RUN			0x00010000	/* Run i/q calibration */


/*
 * PHY PAPD probe register [5111+ (?)]
 * Is this only present in 5212 ?
 * Because it's always 0 in 5211 initialization code
 */
#define	AR5K_PHY_PAPD_PROBE		0x9930
#define	AR5K_PHY_PAPD_PROBE_TXPOWER	0x00007e00
#define	AR5K_PHY_PAPD_PROBE_TXPOWER_S	9
#define	AR5K_PHY_PAPD_PROBE_TX_NEXT	0x00008000
#define	AR5K_PHY_PAPD_PROBE_TYPE	0x01800000	/* [5112+] */
#define	AR5K_PHY_PAPD_PROBE_TYPE_S	23
#define	AR5K_PHY_PAPD_PROBE_TYPE_OFDM	0
#define	AR5K_PHY_PAPD_PROBE_TYPE_XR	1
#define	AR5K_PHY_PAPD_PROBE_TYPE_CCK	2
#define	AR5K_PHY_PAPD_PROBE_GAINF	0xfe000000
#define	AR5K_PHY_PAPD_PROBE_GAINF_S	25
#define	AR5K_PHY_PAPD_PROBE_INI_5111	0x00004883	/* [5212+] */
#define	AR5K_PHY_PAPD_PROBE_INI_5112	0x00004882	/* [5212+] */


/*
 * PHY TX rate power registers [5112+]
 */
#define	AR5K_PHY_TXPOWER_RATE1			0x9934
#define	AR5K_PHY_TXPOWER_RATE2			0x9938
#define	AR5K_PHY_TXPOWER_RATE_MAX		0x993c
#define	AR5K_PHY_TXPOWER_RATE_MAX_TPC_ENABLE	0x00000040
#define	AR5K_PHY_TXPOWER_RATE3			0xa234
#define	AR5K_PHY_TXPOWER_RATE4			0xa238

/*
 * PHY frame control register [5111+]
 */
#define	AR5K_PHY_FRAME_CTL_5210		0x9804
#define	AR5K_PHY_FRAME_CTL_5211		0x9944
#define	AR5K_PHY_FRAME_CTL		(ah->ah_version == AR5K_AR5210 ? \
					AR5K_PHY_FRAME_CTL_5210 : AR5K_PHY_FRAME_CTL_5211)
/*---[5111+]---*/
#define	AR5K_PHY_FRAME_CTL_TX_CLIP	0x00000038
#define	AR5K_PHY_FRAME_CTL_TX_CLIP_S	3
/*---[5110/5111]---*/
#define	AR5K_PHY_FRAME_CTL_TIMING_ERR	0x01000000
#define	AR5K_PHY_FRAME_CTL_PARITY_ERR	0x02000000
#define	AR5K_PHY_FRAME_CTL_ILLRATE_ERR	0x04000000	/* illegal rate */
#define	AR5K_PHY_FRAME_CTL_ILLLEN_ERR	0x08000000	/* illegal length */
#define	AR5K_PHY_FRAME_CTL_SERVICE_ERR	0x20000000
#define	AR5K_PHY_FRAME_CTL_TXURN_ERR	0x40000000	/* tx underrun */
#define AR5K_PHY_FRAME_CTL_INI		AR5K_PHY_FRAME_CTL_SERVICE_ERR | \
			AR5K_PHY_FRAME_CTL_TXURN_ERR | \
			AR5K_PHY_FRAME_CTL_ILLLEN_ERR | \
			AR5K_PHY_FRAME_CTL_ILLRATE_ERR | \
			AR5K_PHY_FRAME_CTL_PARITY_ERR | \
			AR5K_PHY_FRAME_CTL_TIMING_ERR

/*
 * PHY radar detection register [5111+]
 */
#define	AR5K_PHY_RADAR			0x9954

/* Radar enable 			........ ........ ........ .......1 */
#define	AR5K_PHY_RADAR_ENABLE		0x00000001
#define	AR5K_PHY_RADAR_DISABLE          0x00000000
#define	AR5K_PHY_RADAR_ENABLE_S		0

/* This is the value found on the card  .1.111.1 .1.1.... 111....1 1...1...
at power on. */
#define	AR5K_PHY_RADAR_PWONDEF_AR5213	0x5d50e188

/* This is the value found on the card 	.1.1.111 ..11...1 .1...1.1 1...11.1
after DFS is enabled */
#define	AR5K_PHY_RADAR_ENABLED_AR5213	0x5731458d

/* Finite Impulse Response (FIR) filter .1111111 ........ ........ ........
 * power out threshold.
 * 7-bits, standard power range {0..127} in 1/2 dBm units. */
#define AR5K_PHY_RADAR_FIRPWROUTTHR    	0x7f000000
#define AR5K_PHY_RADAR_FIRPWROUTTHR_S	24

/* Radar RSSI/SNR threshold.		........ 111111.. ........ ........
 * 6-bits, dBm range {0..63} in dBm units. */
#define AR5K_PHY_RADAR_RADARRSSITHR    	0x00fc0000
#define AR5K_PHY_RADAR_RADARRSSITHR_S	18

/* Pulse height threshold 		........ ......11 1111.... ........
 * 6-bits, dBm range {0..63} in dBm units. */
#define AR5K_PHY_RADAR_PULSEHEIGHTTHR   0x0003f000
#define AR5K_PHY_RADAR_PULSEHEIGHTTHR_S	12

/* Pulse RSSI/SNR threshold		........ ........ ....1111 11......
 * 6-bits, dBm range {0..63} in dBm units. */
#define AR5K_PHY_RADAR_PULSERSSITHR    	0x00000fc0
#define AR5K_PHY_RADAR_PULSERSSITHR_S	6

/* Inband threshold  			........ ........ ........ ..11111.
 * 5-bits, units unknown {0..31} (? MHz ?) */
#define AR5K_PHY_RADAR_INBANDTHR    	0x0000003e
#define AR5K_PHY_RADAR_INBANDTHR_S	1

/*
 * PHY antenna switch table registers [5110]
 */
#define AR5K_PHY_ANT_SWITCH_TABLE_0	0x9960
#define AR5K_PHY_ANT_SWITCH_TABLE_1	0x9964

/*
 * PHY clock sleep registers [5112+]
 */
#define AR5K_PHY_SCLOCK			0x99f0
#define AR5K_PHY_SCLOCK_32MHZ		0x0000000c
#define AR5K_PHY_SDELAY			0x99f4
#define AR5K_PHY_SDELAY_32MHZ		0x000000ff
#define AR5K_PHY_SPENDING		0x99f8
#define AR5K_PHY_SPENDING_RF5111	0x00000018
#define AR5K_PHY_SPENDING_RF5112	0x00000014

/*
 * Misc PHY/radio registers [5110 - 5111]
 */
#define	AR5K_BB_GAIN_BASE		0x9b00 /* BaseBand Amplifier Gain table base address */
#define AR5K_BB_GAIN(_n)		(AR5K_BB_GAIN_BASE + ((_n) << 2))
#define	AR5K_RF_GAIN_BASE		0x9a00 /* RF Amplrifier Gain table base address */
#define AR5K_RF_GAIN(_n)		(AR5K_RF_GAIN_BASE + ((_n) << 2))

/*
 * PHY timing IQ calibration result register [5111+]
 */
#define	AR5K_PHY_IQRES_CAL_PWR_I	0x9c10 /* I (Inphase) power value */
#define	AR5K_PHY_IQRES_CAL_PWR_Q	0x9c14 /* Q (Quadrature) power value */
#define	AR5K_PHY_IQRES_CAL_CORR		0x9c18	/* I/Q Correlation */

/*
 * PHY current RSSI register [5111+]
 */
#define	AR5K_PHY_CURRENT_RSSI		0x9c1c

/*
 * PHY PCDAC TX power table
 */
#define	AR5K_PHY_PCDAC_TXPOWER_BASE_5211	0xa180
#define AR5K_PHY_PCDAC_TXPOWER_BASE_5413	0xa280
#define AR5K_PHY_PCDAC_TXPOWER_BASE	(ah->ah_radio >= AR5K_RF5413 ? \
					AR5K_PHY_PCDAC_TXPOWER_BASE_5413 :\
					AR5K_PHY_PCDAC_TXPOWER_BASE_5211)
#define	AR5K_PHY_PCDAC_TXPOWER(_n)	(AR5K_PHY_PCDAC_TXPOWER_BASE + ((_n) << 2))

/*
 * PHY mode register [5111+]
 */
#define	AR5K_PHY_MODE			0x0a200		/* Register address */
#define	AR5K_PHY_MODE_MOD		0x00000001	/* PHY Modulation mask*/
#define AR5K_PHY_MODE_MOD_OFDM		0
#define AR5K_PHY_MODE_MOD_CCK		1
#define AR5K_PHY_MODE_FREQ		0x00000002	/* Freq mode mask */
#define	AR5K_PHY_MODE_FREQ_5GHZ		0
#define	AR5K_PHY_MODE_FREQ_2GHZ		2
#define AR5K_PHY_MODE_MOD_DYN		0x00000004	/* Dynamic OFDM/CCK mode mask [5112+] */
#define AR5K_PHY_MODE_RAD		0x00000008	/* [5212+] */
#define AR5K_PHY_MODE_RAD_RF5111	0
#define AR5K_PHY_MODE_RAD_RF5112	8
#define AR5K_PHY_MODE_XR		0x00000010	/* [5112+] */

/*
 * PHY CCK transmit control register [5111+ (?)]
 */
#define AR5K_PHY_CCKTXCTL		0xa204
#define AR5K_PHY_CCKTXCTL_WORLD		0x00000000
#define AR5K_PHY_CCKTXCTL_JAPAN		0x00000010

/*
 * PHY 2GHz gain register [5111+]
 */
#define	AR5K_PHY_GAIN_2GHZ		0xa20c
#define	AR5K_PHY_GAIN_2GHZ_MARGIN_TXRX	0x00fc0000
#define	AR5K_PHY_GAIN_2GHZ_MARGIN_TXRX_S	18
#define	AR5K_PHY_GAIN_2GHZ_INI_5111	0x6480416c
