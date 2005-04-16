/*
 * A collection of structures, addresses, and values associated with
 * the Embedded Planet RPX6 (or RPX Super) MPC8260 board.
 * Copied from the RPX-Classic and SBS8260 stuff.
 *
 * Copyright (c) 2001 Dan Malek <dan@embeddededge.com>
 */
#ifdef __KERNEL__
#ifndef __ASM_PLATFORMS_RPX8260_H__
#define __ASM_PLATFORMS_RPX8260_H__

/* A Board Information structure that is given to a program when
 * prom starts it up.
 */
typedef struct bd_info {
	unsigned int	bi_memstart;	/* Memory start address */
	unsigned int	bi_memsize;	/* Memory (end) size in bytes */
	unsigned int	bi_nvsize;	/* NVRAM size in bytes (can be 0) */
	unsigned int	bi_intfreq;	/* Internal Freq, in Hz */
	unsigned int	bi_busfreq;	/* Bus Freq, in MHz */
	unsigned int	bi_cpmfreq;	/* CPM Freq, in MHz */
	unsigned int	bi_brgfreq;	/* BRG Freq, in MHz */
	unsigned int	bi_vco;		/* VCO Out from PLL */
	unsigned int	bi_baudrate;	/* Default console baud rate */
	unsigned int	bi_immr;	/* IMMR when called from boot rom */
	unsigned char	bi_enetaddr[6];
} bd_t;

extern bd_t m8xx_board_info;

/* Memory map is configured by the PROM startup.
 * We just map a few things we need.  The CSR is actually 4 byte-wide
 * registers that can be accessed as 8-, 16-, or 32-bit values.
 */
#define CPM_MAP_ADDR		((uint)0xf0000000)
#define RPX_CSR_ADDR		((uint)0xfa000000)
#define RPX_CSR_SIZE		((uint)(512 * 1024))
#define RPX_NVRTC_ADDR		((uint)0xfa080000)
#define RPX_NVRTC_SIZE		((uint)(512 * 1024))

/* The RPX6 has 16, byte wide control/status registers.
 * Not all are used (yet).
 */
extern volatile u_char *rpx6_csr_addr;

/* Things of interest in the CSR.
*/
#define BCSR0_ID_MASK		((u_char)0xf0)		/* Read only */
#define BCSR0_SWITCH_MASK	((u_char)0x0f)		/* Read only */
#define BCSR1_XCVR_SMC1		((u_char)0x80)
#define BCSR1_XCVR_SMC2		((u_char)0x40)
#define BCSR2_FLASH_WENABLE	((u_char)0x20)
#define BCSR2_NVRAM_ENABLE	((u_char)0x10)
#define BCSR2_ALT_IRQ2		((u_char)0x08)
#define BCSR2_ALT_IRQ3		((u_char)0x04)
#define BCSR2_PRST		((u_char)0x02)		/* Force reset */
#define BCSR2_ENPRST		((u_char)0x01)		/* Enable POR */
#define BCSR3_MODCLK_MASK	((u_char)0xe0)
#define BCSR3_ENCLKHDR		((u_char)0x10)
#define BCSR3_LED5		((u_char)0x04)		/* 0 == on */
#define BCSR3_LED6		((u_char)0x02)		/* 0 == on */
#define BCSR3_LED7		((u_char)0x01)		/* 0 == on */
#define BCSR4_EN_PHY		((u_char)0x80)		/* Enable PHY */
#define BCSR4_EN_MII		((u_char)0x40)		/* Enable PHY */
#define BCSR4_MII_READ		((u_char)0x04)
#define BCSR4_MII_MDC		((u_char)0x02)
#define BCSR4_MII_MDIO		((u_char)0x01)
#define BCSR13_FETH_IRQMASK	((u_char)0xf0)
#define BCSR15_FETH_IRQ		((u_char)0x20)

#define PHY_INTERRUPT	SIU_INT_IRQ7

/* For our show_cpuinfo hooks. */
#define CPUINFO_VENDOR		"Embedded Planet"
#define CPUINFO_MACHINE		"EP8260 PowerPC"

/* Warm reset vector. */
#define BOOTROM_RESTART_ADDR	((uint)0xfff00104)

#endif /* __ASM_PLATFORMS_RPX8260_H__ */
#endif /* __KERNEL__ */
