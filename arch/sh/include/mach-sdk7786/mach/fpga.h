#ifndef __MACH_SDK7786_FPGA_H
#define __MACH_SDK7786_FPGA_H

#include <linux/io.h>
#include <linux/types.h>
#include <linux/bitops.h>

#define SRSTR		0x000
#define  SRSTR_MAGIC	0x1971	/* Fixed magical read value */

#define INTASR		0x010
#define INTAMR		0x020
#define MODSWR		0x030
#define INTTESTR	0x040
#define SYSSR		0x050
#define NRGPR		0x060
#define NMISR		0x070

#define NMIMR		0x080
#define  NMIMR_MAN_NMIM	BIT(0)	/* Manual NMI mask */
#define  NMIMR_AUX_NMIM	BIT(1)	/* Auxiliary NMI mask */

#define INTBSR		0x090
#define INTBMR		0x0a0
#define USRLEDR		0x0b0
#define MAPSWR		0x0c0
#define FPGAVR		0x0d0
#define FPGADR		0x0e0
#define PCBRR		0x0f0
#define RSR		0x100
#define EXTASR		0x110
#define SPCAR		0x120
#define INTMSR		0x130
#define PCIECR		0x140
#define FAER		0x150
#define USRGPIR		0x160
/* 0x170 reserved */
#define LCLASR		0x180

#define SBCR		0x190
#define  SCBR_I2CMEN	BIT(0)	/* FPGA I2C master enable */
#define  SCBR_I2CCEN	BIT(1)	/* CPU I2C master enable */

#define PWRCR		0x1a0
#define  PWRCR_SCISEL0	BIT(0)
#define  PWRCR_SCISEL1	BIT(1)
#define  PWRCR_SCIEN	BIT(2)	/* Serial port enable */
#define  PWRCR_PDWNACK	BIT(5)	/* Power down acknowledge */
#define  PWRCR_PDWNREQ	BIT(7)	/* Power down request */
#define  PWRCR_INT2	BIT(11)	/* INT2 connection to power manager */
#define  PWRCR_BUPINIT	BIT(13)	/* DDR backup initialize */
#define  PWRCR_BKPRST	BIT(15) /* Backup power reset */

#define SPCBR		0x1b0
#define SPICR		0x1c0
#define SPIDR		0x1d0
#define I2CCR		0x1e0
#define I2CDR		0x1f0
#define FPGACR		0x200
#define IASELR1		0x210
#define IASELR2		0x220
#define IASELR3		0x230
#define IASELR4		0x240
#define IASELR5		0x250
#define IASELR6		0x260
#define IASELR7		0x270
#define IASELR8		0x280
#define IASELR9		0x290
#define IASELR10	0x2a0
#define IASELR11	0x2b0
#define IASELR12	0x2c0
#define IASELR13	0x2d0
#define IASELR14	0x2e0
#define IASELR15	0x2f0
/* 0x300 reserved */
#define IBSELR1		0x310
#define IBSELR2		0x320
#define IBSELR3		0x330
#define IBSELR4		0x340
#define IBSELR5		0x350
#define IBSELR6		0x360
#define IBSELR7		0x370
#define IBSELR8		0x380
#define IBSELR9		0x390
#define IBSELR10	0x3a0
#define IBSELR11	0x3b0
#define IBSELR12	0x3c0
#define IBSELR13	0x3d0
#define IBSELR14	0x3e0
#define IBSELR15	0x3f0
#define USRACR		0x400
#define BEEPR		0x410
#define USRLCDR		0x420
#define SMBCR		0x430
#define SMBDR		0x440
#define USBCR		0x450
#define AMSR		0x460
#define ACCR		0x470
#define SDIFCR		0x480

/* arch/sh/boards/mach-sdk7786/fpga.c */
extern void __iomem *sdk7786_fpga_base;
extern void sdk7786_fpga_init(void);

#define SDK7786_FPGA_REGADDR(reg)	(sdk7786_fpga_base + (reg))

/*
 * A convenience wrapper from register offset to internal I2C address,
 * when the FPGA is in I2C slave mode.
 */
#define SDK7786_FPGA_I2CADDR(reg)	((reg) >> 3)

static inline u16 fpga_read_reg(unsigned int reg)
{
	return ioread16(sdk7786_fpga_base + reg);
}

static inline void fpga_write_reg(u16 val, unsigned int reg)
{
	iowrite16(val, sdk7786_fpga_base + reg);
}

#endif /* __MACH_SDK7786_FPGA_H */
