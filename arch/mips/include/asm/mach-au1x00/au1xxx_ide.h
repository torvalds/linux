/*
 * include/asm-mips/mach-au1x00/au1xxx_ide.h  version 01.30.00   Aug. 02 2005
 *
 * BRIEF MODULE DESCRIPTION
 * AMD Alchemy Au1xxx IDE interface routines over the Static Bus
 *
 * Copyright (c) 2003-2005 AMD, Personal Connectivity Solutions
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any later
 * version.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Note: for more information, please refer "AMD Alchemy Au1200/Au1550 IDE
 *       Interface and Linux Device Driver" Application Note.
 */

#ifdef CONFIG_BLK_DEV_IDE_AU1XXX_MDMA2_DBDMA
#define DMA_WAIT_TIMEOUT	100
#define NUM_DESCRIPTORS 	PRD_ENTRIES
#else /* CONFIG_BLK_DEV_IDE_AU1XXX_PIO_DBDMA */
#define NUM_DESCRIPTORS 	2
#endif

#ifndef AU1XXX_ATA_RQSIZE
#define AU1XXX_ATA_RQSIZE	128
#endif

/* Disable Burstable-Support for DBDMA */
#ifndef CONFIG_BLK_DEV_IDE_AU1XXX_BURSTABLE_ON
#define CONFIG_BLK_DEV_IDE_AU1XXX_BURSTABLE_ON	0
#endif

typedef struct {
	u32			tx_dev_id, rx_dev_id, target_dev_id;
	u32			tx_chan, rx_chan;
	void			*tx_desc_head, *rx_desc_head;
	ide_hwif_t		*hwif;
#ifdef CONFIG_BLK_DEV_IDE_AU1XXX_MDMA2_DBDMA
	ide_drive_t		*drive;
	struct dbdma_cmd	*dma_table_cpu;
	dma_addr_t		dma_table_dma;
#endif
	int			irq;
	u32			regbase;
} _auide_hwif;

/******************************************************************************/
/* PIO Mode timing calculation :					      */
/*									      */
/* Static Bus Spec   ATA Spec						      */
/*	Tcsoe	   =	t1						      */
/*	Toecs	   =	t9						      */
/*	Twcs	   =	t9						      */
/*	Tcsh	   =	t2i | t2					      */
/*	Tcsoff	   =	t2i | t2					      */
/*	Twp	   =	t2						      */
/*	Tcsw	   =	t1						      */
/*	Tpm	   =	0						      */
/*	Ta	   =	t1+t2						      */
/******************************************************************************/

#define TCSOE_MASK		(0x07 << 29)
#define TOECS_MASK		(0x07 << 26)
#define TWCS_MASK		(0x07 << 28)
#define TCSH_MASK		(0x0F << 24)
#define TCSOFF_MASK		(0x07 << 20)
#define TWP_MASK		(0x3F << 14)
#define TCSW_MASK		(0x0F << 10)
#define TPM_MASK		(0x0F << 6)
#define TA_MASK 		(0x3F << 0)
#define TS_MASK 		(1 << 8)

/* Timing parameters PIO mode 0 */
#define SBC_IDE_PIO0_TCSOE	(0x04 << 29)
#define SBC_IDE_PIO0_TOECS	(0x01 << 26)
#define SBC_IDE_PIO0_TWCS	(0x02 << 28)
#define SBC_IDE_PIO0_TCSH	(0x08 << 24)
#define SBC_IDE_PIO0_TCSOFF	(0x07 << 20)
#define SBC_IDE_PIO0_TWP	(0x10 << 14)
#define SBC_IDE_PIO0_TCSW	(0x04 << 10)
#define SBC_IDE_PIO0_TPM	(0x00 << 6)
#define SBC_IDE_PIO0_TA 	(0x15 << 0)
/* Timing parameters PIO mode 1 */
#define SBC_IDE_PIO1_TCSOE	(0x03 << 29)
#define SBC_IDE_PIO1_TOECS	(0x01 << 26)
#define SBC_IDE_PIO1_TWCS	(0x01 << 28)
#define SBC_IDE_PIO1_TCSH	(0x06 << 24)
#define SBC_IDE_PIO1_TCSOFF	(0x06 << 20)
#define SBC_IDE_PIO1_TWP	(0x08 << 14)
#define SBC_IDE_PIO1_TCSW	(0x03 << 10)
#define SBC_IDE_PIO1_TPM	(0x00 << 6)
#define SBC_IDE_PIO1_TA 	(0x0B << 0)
/* Timing parameters PIO mode 2 */
#define SBC_IDE_PIO2_TCSOE	(0x05 << 29)
#define SBC_IDE_PIO2_TOECS	(0x01 << 26)
#define SBC_IDE_PIO2_TWCS	(0x01 << 28)
#define SBC_IDE_PIO2_TCSH	(0x07 << 24)
#define SBC_IDE_PIO2_TCSOFF	(0x07 << 20)
#define SBC_IDE_PIO2_TWP	(0x1F << 14)
#define SBC_IDE_PIO2_TCSW	(0x05 << 10)
#define SBC_IDE_PIO2_TPM	(0x00 << 6)
#define SBC_IDE_PIO2_TA 	(0x22 << 0)
/* Timing parameters PIO mode 3 */
#define SBC_IDE_PIO3_TCSOE	(0x05 << 29)
#define SBC_IDE_PIO3_TOECS	(0x01 << 26)
#define SBC_IDE_PIO3_TWCS	(0x01 << 28)
#define SBC_IDE_PIO3_TCSH	(0x0D << 24)
#define SBC_IDE_PIO3_TCSOFF	(0x0D << 20)
#define SBC_IDE_PIO3_TWP	(0x15 << 14)
#define SBC_IDE_PIO3_TCSW	(0x05 << 10)
#define SBC_IDE_PIO3_TPM	(0x00 << 6)
#define SBC_IDE_PIO3_TA 	(0x1A << 0)
/* Timing parameters PIO mode 4 */
#define SBC_IDE_PIO4_TCSOE	(0x04 << 29)
#define SBC_IDE_PIO4_TOECS	(0x01 << 26)
#define SBC_IDE_PIO4_TWCS	(0x01 << 28)
#define SBC_IDE_PIO4_TCSH	(0x04 << 24)
#define SBC_IDE_PIO4_TCSOFF	(0x04 << 20)
#define SBC_IDE_PIO4_TWP	(0x0D << 14)
#define SBC_IDE_PIO4_TCSW	(0x03 << 10)
#define SBC_IDE_PIO4_TPM	(0x00 << 6)
#define SBC_IDE_PIO4_TA 	(0x12 << 0)
/* Timing parameters MDMA mode 0 */
#define SBC_IDE_MDMA0_TCSOE	(0x03 << 29)
#define SBC_IDE_MDMA0_TOECS	(0x01 << 26)
#define SBC_IDE_MDMA0_TWCS	(0x01 << 28)
#define SBC_IDE_MDMA0_TCSH	(0x07 << 24)
#define SBC_IDE_MDMA0_TCSOFF	(0x07 << 20)
#define SBC_IDE_MDMA0_TWP	(0x0C << 14)
#define SBC_IDE_MDMA0_TCSW	(0x03 << 10)
#define SBC_IDE_MDMA0_TPM	(0x00 << 6)
#define SBC_IDE_MDMA0_TA	(0x0F << 0)
/* Timing parameters MDMA mode 1 */
#define SBC_IDE_MDMA1_TCSOE	(0x05 << 29)
#define SBC_IDE_MDMA1_TOECS	(0x01 << 26)
#define SBC_IDE_MDMA1_TWCS	(0x01 << 28)
#define SBC_IDE_MDMA1_TCSH	(0x05 << 24)
#define SBC_IDE_MDMA1_TCSOFF	(0x05 << 20)
#define SBC_IDE_MDMA1_TWP	(0x0F << 14)
#define SBC_IDE_MDMA1_TCSW	(0x05 << 10)
#define SBC_IDE_MDMA1_TPM	(0x00 << 6)
#define SBC_IDE_MDMA1_TA	(0x15 << 0)
/* Timing parameters MDMA mode 2 */
#define SBC_IDE_MDMA2_TCSOE	(0x04 << 29)
#define SBC_IDE_MDMA2_TOECS	(0x01 << 26)
#define SBC_IDE_MDMA2_TWCS	(0x01 << 28)
#define SBC_IDE_MDMA2_TCSH	(0x04 << 24)
#define SBC_IDE_MDMA2_TCSOFF	(0x04 << 20)
#define SBC_IDE_MDMA2_TWP	(0x0D << 14)
#define SBC_IDE_MDMA2_TCSW	(0x04 << 10)
#define SBC_IDE_MDMA2_TPM	(0x00 << 6)
#define SBC_IDE_MDMA2_TA	(0x12 << 0)

#define SBC_IDE_TIMING(mode) \
	(SBC_IDE_##mode##_TWCS | \
	 SBC_IDE_##mode##_TCSH | \
	 SBC_IDE_##mode##_TCSOFF | \
	 SBC_IDE_##mode##_TWP | \
	 SBC_IDE_##mode##_TCSW | \
	 SBC_IDE_##mode##_TPM | \
	 SBC_IDE_##mode##_TA)
