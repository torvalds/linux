/*
 * Definitions for TX4937/TX4938
 * Copyright (C) 2000-2001 Toshiba Corporation
 *
 * 2003-2005 (c) MontaVista Software, Inc. This file is licensed under the
 * terms of the GNU General Public License version 2. This program is
 * licensed "as is" without any warranty of any kind, whether express
 * or implied.
 *
 * Support for TX4938 in 2.6 - Manish Lachwani (mlachwani@mvista.com)
 */
#ifndef __ASM_TXX9_TX4938_H
#define __ASM_TXX9_TX4938_H

/* some controllers are compatible with 4927 */
#include <asm/txx9/tx4927.h>

#ifdef CONFIG_64BIT
#define TX4938_REG_BASE	0xffffffffff1f0000UL /* == TX4937_REG_BASE */
#else
#define TX4938_REG_BASE	0xff1f0000UL /* == TX4937_REG_BASE */
#endif
#define TX4938_REG_SIZE	0x00010000 /* == TX4937_REG_SIZE */

/* NDFMC, SRAMC, PCIC1, SPIC: TX4938 only */
#define TX4938_NDFMC_REG	(TX4938_REG_BASE + 0x5000)
#define TX4938_SRAMC_REG	(TX4938_REG_BASE + 0x6000)
#define TX4938_PCIC1_REG	(TX4938_REG_BASE + 0x7000)
#define TX4938_SDRAMC_REG	(TX4938_REG_BASE + 0x8000)
#define TX4938_EBUSC_REG	(TX4938_REG_BASE + 0x9000)
#define TX4938_DMA_REG(ch)	(TX4938_REG_BASE + 0xb000 + (ch) * 0x800)
#define TX4938_PCIC_REG		(TX4938_REG_BASE + 0xd000)
#define TX4938_CCFG_REG		(TX4938_REG_BASE + 0xe000)
#define TX4938_NR_TMR	3
#define TX4938_TMR_REG(ch)	((TX4938_REG_BASE + 0xf000) + (ch) * 0x100)
#define TX4938_NR_SIO	2
#define TX4938_SIO_REG(ch)	((TX4938_REG_BASE + 0xf300) + (ch) * 0x100)
#define TX4938_PIO_REG		(TX4938_REG_BASE + 0xf500)
#define TX4938_IRC_REG		(TX4938_REG_BASE + 0xf600)
#define TX4938_ACLC_REG		(TX4938_REG_BASE + 0xf700)
#define TX4938_SPI_REG		(TX4938_REG_BASE + 0xf800)

struct tx4938_sramc_reg {
	u64 cr;
};

struct tx4938_ccfg_reg {
	u64 ccfg;
	u64 crir;
	u64 pcfg;
	u64 toea;
	u64 clkctr;
	u64 unused0;
	u64 garbc;
	u64 unused1;
	u64 unused2;
	u64 ramp;
	u64 unused3;
	u64 jmpadr;
};

/*
 * IRC
 */

#define TX4938_IR_ECCERR	0
#define TX4938_IR_WTOERR	1
#define TX4938_NUM_IR_INT	6
#define TX4938_IR_INT(n)	(2 + (n))
#define TX4938_NUM_IR_SIO	2
#define TX4938_IR_SIO(n)	(8 + (n))
#define TX4938_NUM_IR_DMA	4
#define TX4938_IR_DMA(ch, n)	((ch ? 27 : 10) + (n)) /* 10-13, 27-30 */
#define TX4938_IR_PIO	14
#define TX4938_IR_PDMAC	15
#define TX4938_IR_PCIC	16
#define TX4938_NUM_IR_TMR	3
#define TX4938_IR_TMR(n)	(17 + (n))
#define TX4938_IR_NDFMC	21
#define TX4938_IR_PCIERR	22
#define TX4938_IR_PCIPME	23
#define TX4938_IR_ACLC	24
#define TX4938_IR_ACLCPME	25
#define TX4938_IR_PCIC1	26
#define TX4938_IR_SPI	31
#define TX4938_NUM_IR	32
/* multiplex */
#define TX4938_IR_ETH0	TX4938_IR_INT(4)
#define TX4938_IR_ETH1	TX4938_IR_INT(3)

#define TX4938_IRC_INT	2	/* IP[2] in Status register */

#define TX4938_NUM_PIO	16

/*
 * CCFG
 */
/* CCFG : Chip Configuration */
#define TX4938_CCFG_WDRST	0x0000020000000000ULL
#define TX4938_CCFG_WDREXEN	0x0000010000000000ULL
#define TX4938_CCFG_BCFG_MASK	0x000000ff00000000ULL
#define TX4938_CCFG_TINTDIS	0x01000000
#define TX4938_CCFG_PCI66	0x00800000
#define TX4938_CCFG_PCIMODE	0x00400000
#define TX4938_CCFG_PCI1_66	0x00200000
#define TX4938_CCFG_DIVMODE_MASK	0x001e0000
#define TX4938_CCFG_DIVMODE_2	(0x4 << 17)
#define TX4938_CCFG_DIVMODE_2_5	(0xf << 17)
#define TX4938_CCFG_DIVMODE_3	(0x5 << 17)
#define TX4938_CCFG_DIVMODE_4	(0x6 << 17)
#define TX4938_CCFG_DIVMODE_4_5	(0xd << 17)
#define TX4938_CCFG_DIVMODE_8	(0x0 << 17)
#define TX4938_CCFG_DIVMODE_10	(0xb << 17)
#define TX4938_CCFG_DIVMODE_12	(0x1 << 17)
#define TX4938_CCFG_DIVMODE_16	(0x2 << 17)
#define TX4938_CCFG_DIVMODE_18	(0x9 << 17)
#define TX4938_CCFG_BEOW	0x00010000
#define TX4938_CCFG_WR	0x00008000
#define TX4938_CCFG_TOE	0x00004000
#define TX4938_CCFG_PCIARB	0x00002000
#define TX4938_CCFG_PCIDIVMODE_MASK	0x00001c00
#define TX4938_CCFG_PCIDIVMODE_4	(0x1 << 10)
#define TX4938_CCFG_PCIDIVMODE_4_5	(0x3 << 10)
#define TX4938_CCFG_PCIDIVMODE_5	(0x5 << 10)
#define TX4938_CCFG_PCIDIVMODE_5_5	(0x7 << 10)
#define TX4938_CCFG_PCIDIVMODE_8	(0x0 << 10)
#define TX4938_CCFG_PCIDIVMODE_9	(0x2 << 10)
#define TX4938_CCFG_PCIDIVMODE_10	(0x4 << 10)
#define TX4938_CCFG_PCIDIVMODE_11	(0x6 << 10)
#define TX4938_CCFG_PCI1DMD	0x00000100
#define TX4938_CCFG_SYSSP_MASK	0x000000c0
#define TX4938_CCFG_ENDIAN	0x00000004
#define TX4938_CCFG_HALT	0x00000002
#define TX4938_CCFG_ACEHOLD	0x00000001

/* PCFG : Pin Configuration */
#define TX4938_PCFG_ETH0_SEL	0x8000000000000000ULL
#define TX4938_PCFG_ETH1_SEL	0x4000000000000000ULL
#define TX4938_PCFG_ATA_SEL	0x2000000000000000ULL
#define TX4938_PCFG_ISA_SEL	0x1000000000000000ULL
#define TX4938_PCFG_SPI_SEL	0x0800000000000000ULL
#define TX4938_PCFG_NDF_SEL	0x0400000000000000ULL
#define TX4938_PCFG_SDCLKDLY_MASK	0x30000000
#define TX4938_PCFG_SDCLKDLY(d)	((d)<<28)
#define TX4938_PCFG_SYSCLKEN	0x08000000
#define TX4938_PCFG_SDCLKEN_ALL	0x07800000
#define TX4938_PCFG_SDCLKEN(ch)	(0x00800000<<(ch))
#define TX4938_PCFG_PCICLKEN_ALL	0x003f0000
#define TX4938_PCFG_PCICLKEN(ch)	(0x00010000<<(ch))
#define TX4938_PCFG_SEL2	0x00000200
#define TX4938_PCFG_SEL1	0x00000100
#define TX4938_PCFG_DMASEL_ALL	0x0000000f
#define TX4938_PCFG_DMASEL0_DRQ0	0x00000000
#define TX4938_PCFG_DMASEL0_SIO1	0x00000001
#define TX4938_PCFG_DMASEL1_DRQ1	0x00000000
#define TX4938_PCFG_DMASEL1_SIO1	0x00000002
#define TX4938_PCFG_DMASEL2_DRQ2	0x00000000
#define TX4938_PCFG_DMASEL2_SIO0	0x00000004
#define TX4938_PCFG_DMASEL3_DRQ3	0x00000000
#define TX4938_PCFG_DMASEL3_SIO0	0x00000008

/* CLKCTR : Clock Control */
#define TX4938_CLKCTR_NDFCKD	0x0001000000000000ULL
#define TX4938_CLKCTR_NDFRST	0x0000000100000000ULL
#define TX4938_CLKCTR_ETH1CKD	0x80000000
#define TX4938_CLKCTR_ETH0CKD	0x40000000
#define TX4938_CLKCTR_SPICKD	0x20000000
#define TX4938_CLKCTR_SRAMCKD	0x10000000
#define TX4938_CLKCTR_PCIC1CKD	0x08000000
#define TX4938_CLKCTR_DMA1CKD	0x04000000
#define TX4938_CLKCTR_ACLCKD	0x02000000
#define TX4938_CLKCTR_PIOCKD	0x01000000
#define TX4938_CLKCTR_DMACKD	0x00800000
#define TX4938_CLKCTR_PCICKD	0x00400000
#define TX4938_CLKCTR_TM0CKD	0x00100000
#define TX4938_CLKCTR_TM1CKD	0x00080000
#define TX4938_CLKCTR_TM2CKD	0x00040000
#define TX4938_CLKCTR_SIO0CKD	0x00020000
#define TX4938_CLKCTR_SIO1CKD	0x00010000
#define TX4938_CLKCTR_ETH1RST	0x00008000
#define TX4938_CLKCTR_ETH0RST	0x00004000
#define TX4938_CLKCTR_SPIRST	0x00002000
#define TX4938_CLKCTR_SRAMRST	0x00001000
#define TX4938_CLKCTR_PCIC1RST	0x00000800
#define TX4938_CLKCTR_DMA1RST	0x00000400
#define TX4938_CLKCTR_ACLRST	0x00000200
#define TX4938_CLKCTR_PIORST	0x00000100
#define TX4938_CLKCTR_DMARST	0x00000080
#define TX4938_CLKCTR_PCIRST	0x00000040
#define TX4938_CLKCTR_TM0RST	0x00000010
#define TX4938_CLKCTR_TM1RST	0x00000008
#define TX4938_CLKCTR_TM2RST	0x00000004
#define TX4938_CLKCTR_SIO0RST	0x00000002
#define TX4938_CLKCTR_SIO1RST	0x00000001

/*
 * DMA
 */
/* bits for MCR */
#define TX4938_DMA_MCR_EIS(ch)	(0x10000000<<(ch))
#define TX4938_DMA_MCR_DIS(ch)	(0x01000000<<(ch))
#define TX4938_DMA_MCR_RSFIF	0x00000080
#define TX4938_DMA_MCR_FIFUM(ch)	(0x00000008<<(ch))
#define TX4938_DMA_MCR_RPRT	0x00000002
#define TX4938_DMA_MCR_MSTEN	0x00000001

/* bits for CCRn */
#define TX4938_DMA_CCR_IMMCHN	0x20000000
#define TX4938_DMA_CCR_USEXFSZ	0x10000000
#define TX4938_DMA_CCR_LE	0x08000000
#define TX4938_DMA_CCR_DBINH	0x04000000
#define TX4938_DMA_CCR_SBINH	0x02000000
#define TX4938_DMA_CCR_CHRST	0x01000000
#define TX4938_DMA_CCR_RVBYTE	0x00800000
#define TX4938_DMA_CCR_ACKPOL	0x00400000
#define TX4938_DMA_CCR_REQPL	0x00200000
#define TX4938_DMA_CCR_EGREQ	0x00100000
#define TX4938_DMA_CCR_CHDN	0x00080000
#define TX4938_DMA_CCR_DNCTL	0x00060000
#define TX4938_DMA_CCR_EXTRQ	0x00010000
#define TX4938_DMA_CCR_INTRQD	0x0000e000
#define TX4938_DMA_CCR_INTENE	0x00001000
#define TX4938_DMA_CCR_INTENC	0x00000800
#define TX4938_DMA_CCR_INTENT	0x00000400
#define TX4938_DMA_CCR_CHNEN	0x00000200
#define TX4938_DMA_CCR_XFACT	0x00000100
#define TX4938_DMA_CCR_SMPCHN	0x00000020
#define TX4938_DMA_CCR_XFSZ(order)	(((order) << 2) & 0x0000001c)
#define TX4938_DMA_CCR_XFSZ_1W	TX4938_DMA_CCR_XFSZ(2)
#define TX4938_DMA_CCR_XFSZ_2W	TX4938_DMA_CCR_XFSZ(3)
#define TX4938_DMA_CCR_XFSZ_4W	TX4938_DMA_CCR_XFSZ(4)
#define TX4938_DMA_CCR_XFSZ_8W	TX4938_DMA_CCR_XFSZ(5)
#define TX4938_DMA_CCR_XFSZ_16W	TX4938_DMA_CCR_XFSZ(6)
#define TX4938_DMA_CCR_XFSZ_32W	TX4938_DMA_CCR_XFSZ(7)
#define TX4938_DMA_CCR_MEMIO	0x00000002
#define TX4938_DMA_CCR_SNGAD	0x00000001

/* bits for CSRn */
#define TX4938_DMA_CSR_CHNEN	0x00000400
#define TX4938_DMA_CSR_STLXFER	0x00000200
#define TX4938_DMA_CSR_CHNACT	0x00000100
#define TX4938_DMA_CSR_ABCHC	0x00000080
#define TX4938_DMA_CSR_NCHNC	0x00000040
#define TX4938_DMA_CSR_NTRNFC	0x00000020
#define TX4938_DMA_CSR_EXTDN	0x00000010
#define TX4938_DMA_CSR_CFERR	0x00000008
#define TX4938_DMA_CSR_CHERR	0x00000004
#define TX4938_DMA_CSR_DESERR	0x00000002
#define TX4938_DMA_CSR_SORERR	0x00000001

#define tx4938_sdramcptr	tx4927_sdramcptr
#define tx4938_ebuscptr		tx4927_ebuscptr
#define tx4938_pcicptr		tx4927_pcicptr
#define tx4938_pcic1ptr \
		((struct tx4927_pcic_reg __iomem *)TX4938_PCIC1_REG)
#define tx4938_ccfgptr \
		((struct tx4938_ccfg_reg __iomem *)TX4938_CCFG_REG)
#define tx4938_pioptr		((struct txx9_pio_reg __iomem *)TX4938_PIO_REG)
#define tx4938_sramcptr \
		((struct tx4938_sramc_reg __iomem *)TX4938_SRAMC_REG)


#define TX4938_REV_PCODE()	\
	((__u32)__raw_readq(&tx4938_ccfgptr->crir) >> 16)

#define tx4938_ccfg_clear(bits)	tx4927_ccfg_clear(bits)
#define tx4938_ccfg_set(bits)	tx4927_ccfg_set(bits)
#define tx4938_ccfg_change(change, new)	tx4927_ccfg_change(change, new)

#define TX4938_SDRAMC_CR(ch)	TX4927_SDRAMC_CR(ch)
#define TX4938_SDRAMC_BA(ch)	TX4927_SDRAMC_BA(ch)
#define TX4938_SDRAMC_SIZE(ch)	TX4927_SDRAMC_SIZE(ch)

#define TX4938_EBUSC_CR(ch)	TX4927_EBUSC_CR(ch)
#define TX4938_EBUSC_BA(ch)	TX4927_EBUSC_BA(ch)
#define TX4938_EBUSC_SIZE(ch)	TX4927_EBUSC_SIZE(ch)
#define TX4938_EBUSC_WIDTH(ch)	TX4927_EBUSC_WIDTH(ch)

#define tx4938_get_mem_size() tx4927_get_mem_size()
void tx4938_wdt_init(void);
void tx4938_setup(void);
void tx4938_time_init(unsigned int tmrnr);
void tx4938_sio_init(unsigned int sclk, unsigned int cts_mask);
void tx4938_spi_init(int busid);
void tx4938_ethaddr_init(unsigned char *addr0, unsigned char *addr1);
int tx4938_report_pciclk(void);
void tx4938_report_pci1clk(void);
int tx4938_pciclk66_setup(void);
struct pci_dev;
int tx4938_pcic1_map_irq(const struct pci_dev *dev, u8 slot);
void tx4938_setup_pcierr_irq(void);
void tx4938_irq_init(void);
void tx4938_mtd_init(int ch);
void tx4938_ndfmc_init(unsigned int hold, unsigned int spw);

struct tx4938ide_platform_info {
	/*
	 * I/O port shift, for platforms with ports that are
	 * constantly spaced and need larger than the 1-byte
	 * spacing used by ata_std_ports().
	 */
	unsigned int ioport_shift;
	unsigned int gbus_clock;	/*  0 means no PIO mode tuning. */
	unsigned int ebus_ch;
};

void tx4938_ata_init(unsigned int irq, unsigned int shift, int tune);
void tx4938_dmac_init(int memcpy_chan0, int memcpy_chan1);
void tx4938_aclc_init(void);
void tx4938_sramc_init(void);

#endif
