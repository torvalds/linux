/*
 * arch/arm/include/asm/hardware/sa1111.h
 *
 * Copyright (C) 2000 John G Dorsey <john+@cs.cmu.edu>
 *
 * This file contains definitions for the SA-1111 Companion Chip.
 * (Structure and naming borrowed from SA-1101.h, by Peter Danielsson.)
 *
 * Macro that calculates real address for registers in the SA-1111
 */

#ifndef _ASM_ARCH_SA1111
#define _ASM_ARCH_SA1111

#include <mach/bitfield.h>

/*
 * The SA1111 is always located at virtual 0xf4000000, and is always
 * "native" endian.
 */

#define SA1111_VBASE		0xf4000000

/* Don't use these! */
#define SA1111_p2v( x )         ((x) - SA1111_BASE + SA1111_VBASE)
#define SA1111_v2p( x )         ((x) - SA1111_VBASE + SA1111_BASE)

#ifndef __ASSEMBLY__
#define _SA1111(x)	((x) + sa1111->resource.start)
#endif

#define sa1111_writel(val,addr)	__raw_writel(val, addr)
#define sa1111_readl(addr)	__raw_readl(addr)

/*
 * 26 bits of the SA-1110 address bus are available to the SA-1111.
 * Use these when feeding target addresses to the DMA engines.
 */

#define SA1111_ADDR_WIDTH	(26)
#define SA1111_ADDR_MASK	((1<<SA1111_ADDR_WIDTH)-1)
#define SA1111_DMA_ADDR(x)	((x)&SA1111_ADDR_MASK)

/*
 * Don't ask the (SAC) DMA engines to move less than this amount.
 */

#define SA1111_SAC_DMA_MIN_XFER	(0x800)

/*
 * System Bus Interface (SBI)
 *
 * Registers
 *    SKCR	Control Register
 *    SMCR	Shared Memory Controller Register
 *    SKID	ID Register
 */
#define SA1111_SKCR	0x0000
#define SA1111_SMCR	0x0004
#define SA1111_SKID	0x0008

#define SKCR_PLL_BYPASS	(1<<0)
#define SKCR_RCLKEN	(1<<1)
#define SKCR_SLEEP	(1<<2)
#define SKCR_DOZE	(1<<3)
#define SKCR_VCO_OFF	(1<<4)
#define SKCR_SCANTSTEN	(1<<5)
#define SKCR_CLKTSTEN	(1<<6)
#define SKCR_RDYEN	(1<<7)
#define SKCR_SELAC	(1<<8)
#define SKCR_OPPC	(1<<9)
#define SKCR_PLLTSTEN	(1<<10)
#define SKCR_USBIOTSTEN	(1<<11)
/*
 * Don't believe the specs!  Take them, throw them outside.  Leave them
 * there for a week.  Spit on them.  Walk on them.  Stamp on them.
 * Pour gasoline over them and finally burn them.  Now think about coding.
 *  - The October 1999 errata (278260-007) says its bit 13, 1 to enable.
 *  - The Feb 2001 errata (278260-010) says that the previous errata
 *    (278260-009) is wrong, and its bit actually 12, fixed in spec
 *    278242-003.
 *  - The SA1111 manual (278242) says bit 12, but 0 to enable.
 *  - Reality is bit 13, 1 to enable.
 *      -- rmk
 */
#define SKCR_OE_EN	(1<<13)

#define SMCR_DTIM	(1<<0)
#define SMCR_MBGE	(1<<1)
#define SMCR_DRAC_0	(1<<2)
#define SMCR_DRAC_1	(1<<3)
#define SMCR_DRAC_2	(1<<4)
#define SMCR_DRAC	Fld(3, 2)
#define SMCR_CLAT	(1<<5)

#define SKID_SIREV_MASK	(0x000000f0)
#define SKID_MTREV_MASK (0x0000000f)
#define SKID_ID_MASK	(0xffffff00)
#define SKID_SA1111_ID	(0x690cc200)

/*
 * System Controller
 *
 * Registers
 *    SKPCR	Power Control Register
 *    SKCDR	Clock Divider Register
 *    SKAUD	Audio Clock Divider Register
 *    SKPMC	PS/2 Mouse Clock Divider Register
 *    SKPTC	PS/2 Track Pad Clock Divider Register
 *    SKPEN0	PWM0 Enable Register
 *    SKPWM0	PWM0 Clock Register
 *    SKPEN1	PWM1 Enable Register
 *    SKPWM1	PWM1 Clock Register
 */
#define SA1111_SKPCR	0x0200
#define SA1111_SKCDR	0x0204
#define SA1111_SKAUD	0x0208
#define SA1111_SKPMC	0x020c
#define SA1111_SKPTC	0x0210
#define SA1111_SKPEN0	0x0214
#define SA1111_SKPWM0	0x0218
#define SA1111_SKPEN1	0x021c
#define SA1111_SKPWM1	0x0220

#define SKPCR_UCLKEN	(1<<0)
#define SKPCR_ACCLKEN	(1<<1)
#define SKPCR_I2SCLKEN	(1<<2)
#define SKPCR_L3CLKEN	(1<<3)
#define SKPCR_SCLKEN	(1<<4)
#define SKPCR_PMCLKEN	(1<<5)
#define SKPCR_PTCLKEN	(1<<6)
#define SKPCR_DCLKEN	(1<<7)
#define SKPCR_PWMCLKEN	(1<<8)

/* USB Host controller */
#define SA1111_USB		0x0400

/*
 * Serial Audio Controller
 *
 * Registers
 *    SACR0             Serial Audio Common Control Register
 *    SACR1             Serial Audio Alternate Mode (I2C/MSB) Control Register
 *    SACR2             Serial Audio AC-link Control Register
 *    SASR0             Serial Audio I2S/MSB Interface & FIFO Status Register
 *    SASR1             Serial Audio AC-link Interface & FIFO Status Register
 *    SASCR             Serial Audio Status Clear Register
 *    L3_CAR            L3 Control Bus Address Register
 *    L3_CDR            L3 Control Bus Data Register
 *    ACCAR             AC-link Command Address Register
 *    ACCDR             AC-link Command Data Register
 *    ACSAR             AC-link Status Address Register
 *    ACSDR             AC-link Status Data Register
 *    SADTCS            Serial Audio DMA Transmit Control/Status Register
 *    SADTSA            Serial Audio DMA Transmit Buffer Start Address A
 *    SADTCA            Serial Audio DMA Transmit Buffer Count Register A
 *    SADTSB            Serial Audio DMA Transmit Buffer Start Address B
 *    SADTCB            Serial Audio DMA Transmit Buffer Count Register B
 *    SADRCS            Serial Audio DMA Receive Control/Status Register
 *    SADRSA            Serial Audio DMA Receive Buffer Start Address A
 *    SADRCA            Serial Audio DMA Receive Buffer Count Register A
 *    SADRSB            Serial Audio DMA Receive Buffer Start Address B
 *    SADRCB            Serial Audio DMA Receive Buffer Count Register B
 *    SAITR             Serial Audio Interrupt Test Register
 *    SADR              Serial Audio Data Register (16 x 32-bit)
 */

#define SA1111_SERAUDIO		0x0600

/*
 * These are offsets from the above base.
 */
#define SA1111_SACR0		0x00
#define SA1111_SACR1		0x04
#define SA1111_SACR2		0x08
#define SA1111_SASR0		0x0c
#define SA1111_SASR1		0x10
#define SA1111_SASCR		0x18
#define SA1111_L3_CAR		0x1c
#define SA1111_L3_CDR		0x20
#define SA1111_ACCAR		0x24
#define SA1111_ACCDR		0x28
#define SA1111_ACSAR		0x2c
#define SA1111_ACSDR		0x30
#define SA1111_SADTCS		0x34
#define SA1111_SADTSA		0x38
#define SA1111_SADTCA		0x3c
#define SA1111_SADTSB		0x40
#define SA1111_SADTCB		0x44
#define SA1111_SADRCS		0x48
#define SA1111_SADRSA		0x4c
#define SA1111_SADRCA		0x50
#define SA1111_SADRSB		0x54
#define SA1111_SADRCB		0x58
#define SA1111_SAITR		0x5c
#define SA1111_SADR		0x80

#ifndef CONFIG_ARCH_PXA

#define SACR0_ENB	(1<<0)
#define SACR0_BCKD	(1<<2)
#define SACR0_RST	(1<<3)

#define SACR1_AMSL	(1<<0)
#define SACR1_L3EN	(1<<1)
#define SACR1_L3MB	(1<<2)
#define SACR1_DREC	(1<<3)
#define SACR1_DRPL	(1<<4)
#define SACR1_ENLBF	(1<<5)

#define SACR2_TS3V	(1<<0)
#define SACR2_TS4V	(1<<1)
#define SACR2_WKUP	(1<<2)
#define SACR2_DREC	(1<<3)
#define SACR2_DRPL	(1<<4)
#define SACR2_ENLBF	(1<<5)
#define SACR2_RESET	(1<<6)

#define SASR0_TNF	(1<<0)
#define SASR0_RNE	(1<<1)
#define SASR0_BSY	(1<<2)
#define SASR0_TFS	(1<<3)
#define SASR0_RFS	(1<<4)
#define SASR0_TUR	(1<<5)
#define SASR0_ROR	(1<<6)
#define SASR0_L3WD	(1<<16)
#define SASR0_L3RD	(1<<17)

#define SASR1_TNF	(1<<0)
#define SASR1_RNE	(1<<1)
#define SASR1_BSY	(1<<2)
#define SASR1_TFS	(1<<3)
#define SASR1_RFS	(1<<4)
#define SASR1_TUR	(1<<5)
#define SASR1_ROR	(1<<6)
#define SASR1_CADT	(1<<16)
#define SASR1_SADR	(1<<17)
#define SASR1_RSTO	(1<<18)
#define SASR1_CLPM	(1<<19)
#define SASR1_CRDY	(1<<20)
#define SASR1_RS3V	(1<<21)
#define SASR1_RS4V	(1<<22)

#define SASCR_TUR	(1<<5)
#define SASCR_ROR	(1<<6)
#define SASCR_DTS	(1<<16)
#define SASCR_RDD	(1<<17)
#define SASCR_STO	(1<<18)

#define SADTCS_TDEN	(1<<0)
#define SADTCS_TDIE	(1<<1)
#define SADTCS_TDBDA	(1<<3)
#define SADTCS_TDSTA	(1<<4)
#define SADTCS_TDBDB	(1<<5)
#define SADTCS_TDSTB	(1<<6)
#define SADTCS_TBIU	(1<<7)

#define SADRCS_RDEN	(1<<0)
#define SADRCS_RDIE	(1<<1)
#define SADRCS_RDBDA	(1<<3)
#define SADRCS_RDSTA	(1<<4)
#define SADRCS_RDBDB	(1<<5)
#define SADRCS_RDSTB	(1<<6)
#define SADRCS_RBIU	(1<<7)

#define SAD_CS_DEN	(1<<0)
#define SAD_CS_DIE	(1<<1)	/* Not functional on metal 1 */
#define SAD_CS_DBDA	(1<<3)	/* Not functional on metal 1 */
#define SAD_CS_DSTA	(1<<4)
#define SAD_CS_DBDB	(1<<5)	/* Not functional on metal 1 */
#define SAD_CS_DSTB	(1<<6)
#define SAD_CS_BIU	(1<<7)	/* Not functional on metal 1 */

#define SAITR_TFS	(1<<0)
#define SAITR_RFS	(1<<1)
#define SAITR_TUR	(1<<2)
#define SAITR_ROR	(1<<3)
#define SAITR_CADT	(1<<4)
#define SAITR_SADR	(1<<5)
#define SAITR_RSTO	(1<<6)
#define SAITR_TDBDA	(1<<8)
#define SAITR_TDBDB	(1<<9)
#define SAITR_RDBDA	(1<<10)
#define SAITR_RDBDB	(1<<11)

#endif  /* !CONFIG_ARCH_PXA */

/*
 * General-Purpose I/O Interface
 *
 * Registers
 *    PA_DDR		GPIO Block A Data Direction
 *    PA_DRR/PA_DWR	GPIO Block A Data Value Register (read/write)
 *    PA_SDR		GPIO Block A Sleep Direction
 *    PA_SSR		GPIO Block A Sleep State
 *    PB_DDR		GPIO Block B Data Direction
 *    PB_DRR/PB_DWR	GPIO Block B Data Value Register (read/write)
 *    PB_SDR		GPIO Block B Sleep Direction
 *    PB_SSR		GPIO Block B Sleep State
 *    PC_DDR		GPIO Block C Data Direction
 *    PC_DRR/PC_DWR	GPIO Block C Data Value Register (read/write)
 *    PC_SDR		GPIO Block C Sleep Direction
 *    PC_SSR		GPIO Block C Sleep State
 */

#define SA1111_GPIO	0x1000

#define SA1111_GPIO_PADDR	(0x000)
#define SA1111_GPIO_PADRR	(0x004)
#define SA1111_GPIO_PADWR	(0x004)
#define SA1111_GPIO_PASDR	(0x008)
#define SA1111_GPIO_PASSR	(0x00c)
#define SA1111_GPIO_PBDDR	(0x010)
#define SA1111_GPIO_PBDRR	(0x014)
#define SA1111_GPIO_PBDWR	(0x014)
#define SA1111_GPIO_PBSDR	(0x018)
#define SA1111_GPIO_PBSSR	(0x01c)
#define SA1111_GPIO_PCDDR	(0x020)
#define SA1111_GPIO_PCDRR	(0x024)
#define SA1111_GPIO_PCDWR	(0x024)
#define SA1111_GPIO_PCSDR	(0x028)
#define SA1111_GPIO_PCSSR	(0x02c)

#define GPIO_A0		(1 << 0)
#define GPIO_A1		(1 << 1)
#define GPIO_A2		(1 << 2)
#define GPIO_A3		(1 << 3)

#define GPIO_B0		(1 << 8)
#define GPIO_B1		(1 << 9)
#define GPIO_B2		(1 << 10)
#define GPIO_B3		(1 << 11)
#define GPIO_B4		(1 << 12)
#define GPIO_B5		(1 << 13)
#define GPIO_B6		(1 << 14)
#define GPIO_B7		(1 << 15)

#define GPIO_C0		(1 << 16)
#define GPIO_C1		(1 << 17)
#define GPIO_C2		(1 << 18)
#define GPIO_C3		(1 << 19)
#define GPIO_C4		(1 << 20)
#define GPIO_C5		(1 << 21)
#define GPIO_C6		(1 << 22)
#define GPIO_C7		(1 << 23)

/*
 * Interrupt Controller
 *
 * Registers
 *    INTTEST0		Test register 0
 *    INTTEST1		Test register 1
 *    INTEN0		Interrupt Enable register 0
 *    INTEN1		Interrupt Enable register 1
 *    INTPOL0		Interrupt Polarity selection 0
 *    INTPOL1		Interrupt Polarity selection 1
 *    INTTSTSEL		Interrupt source selection
 *    INTSTATCLR0	Interrupt Status/Clear 0
 *    INTSTATCLR1	Interrupt Status/Clear 1
 *    INTSET0		Interrupt source set 0
 *    INTSET1		Interrupt source set 1
 *    WAKE_EN0		Wake-up source enable 0
 *    WAKE_EN1		Wake-up source enable 1
 *    WAKE_POL0		Wake-up polarity selection 0
 *    WAKE_POL1		Wake-up polarity selection 1
 */
#define SA1111_INTC		0x1600

/*
 * These are offsets from the above base.
 */
#define SA1111_INTTEST0		0x0000
#define SA1111_INTTEST1		0x0004
#define SA1111_INTEN0		0x0008
#define SA1111_INTEN1		0x000c
#define SA1111_INTPOL0		0x0010
#define SA1111_INTPOL1		0x0014
#define SA1111_INTTSTSEL	0x0018
#define SA1111_INTSTATCLR0	0x001c
#define SA1111_INTSTATCLR1	0x0020
#define SA1111_INTSET0		0x0024
#define SA1111_INTSET1		0x0028
#define SA1111_WAKEEN0		0x002c
#define SA1111_WAKEEN1		0x0030
#define SA1111_WAKEPOL0		0x0034
#define SA1111_WAKEPOL1		0x0038

/* PS/2 Trackpad and Mouse Interfaces */
#define SA1111_KBD		0x0a00
#define SA1111_MSE		0x0c00

/* PCMCIA Interface */
#define SA1111_PCMCIA		0x1600





extern struct bus_type sa1111_bus_type;

#define SA1111_DEVID_SBI	(1 << 0)
#define SA1111_DEVID_SK		(1 << 1)
#define SA1111_DEVID_USB	(1 << 2)
#define SA1111_DEVID_SAC	(1 << 3)
#define SA1111_DEVID_SSP	(1 << 4)
#define SA1111_DEVID_PS2	(3 << 5)
#define SA1111_DEVID_PS2_KBD	(1 << 5)
#define SA1111_DEVID_PS2_MSE	(1 << 6)
#define SA1111_DEVID_GPIO	(1 << 7)
#define SA1111_DEVID_INT	(1 << 8)
#define SA1111_DEVID_PCMCIA	(1 << 9)

struct sa1111_dev {
	struct device	dev;
	unsigned int	devid;
	struct resource	res;
	void __iomem	*mapbase;
	unsigned int	skpcr_mask;
	unsigned int	irq[6];
	u64		dma_mask;
};

#define to_sa1111_device(x)	container_of(x, struct sa1111_dev, dev)

#define sa1111_get_drvdata(d)	dev_get_drvdata(&(d)->dev)
#define sa1111_set_drvdata(d,p)	dev_set_drvdata(&(d)->dev, p)

struct sa1111_driver {
	struct device_driver	drv;
	unsigned int		devid;
	int (*probe)(struct sa1111_dev *);
	int (*remove)(struct sa1111_dev *);
	int (*suspend)(struct sa1111_dev *, pm_message_t);
	int (*resume)(struct sa1111_dev *);
	void (*shutdown)(struct sa1111_dev *);
};

#define SA1111_DRV(_d)	container_of((_d), struct sa1111_driver, drv)

#define SA1111_DRIVER_NAME(_sadev) ((_sadev)->dev.driver->name)

/*
 * These frob the SKPCR register, and call platform specific
 * enable/disable functions.
 */
int sa1111_enable_device(struct sa1111_dev *);
void sa1111_disable_device(struct sa1111_dev *);

int sa1111_get_irq(struct sa1111_dev *, unsigned num);

unsigned int sa1111_pll_clock(struct sa1111_dev *);

#define SA1111_AUDIO_ACLINK	0
#define SA1111_AUDIO_I2S	1

void sa1111_select_audio_mode(struct sa1111_dev *sadev, int mode);
int sa1111_set_audio_rate(struct sa1111_dev *sadev, int rate);
int sa1111_get_audio_rate(struct sa1111_dev *sadev);

int sa1111_check_dma_bug(dma_addr_t addr);

int sa1111_driver_register(struct sa1111_driver *);
void sa1111_driver_unregister(struct sa1111_driver *);

void sa1111_set_io_dir(struct sa1111_dev *sadev, unsigned int bits, unsigned int dir, unsigned int sleep_dir);
void sa1111_set_io(struct sa1111_dev *sadev, unsigned int bits, unsigned int v);
void sa1111_set_sleep_io(struct sa1111_dev *sadev, unsigned int bits, unsigned int v);

struct sa1111_platform_data {
	int	irq_base;	/* base for cascaded on-chip IRQs */
	unsigned disable_devs;
	void	*data;
	int	(*enable)(void *, unsigned);
	void	(*disable)(void *, unsigned);
};

#endif  /* _ASM_ARCH_SA1111 */
