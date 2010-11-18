#ifndef __MACH_SH2007_H
#define __MACH_SH2007_H

#define CS5BCR		0xff802050
#define CS5WCR		0xff802058
#define CS5PCR		0xff802070

#define BUS_SZ8		1
#define BUS_SZ16	2
#define BUS_SZ32	3

#define PCMCIA_IODYN	1
#define PCMCIA_ATA	0
#define PCMCIA_IO8	2
#define PCMCIA_IO16	3
#define PCMCIA_COMM8	4
#define PCMCIA_COMM16	5
#define PCMCIA_ATTR8	6
#define PCMCIA_ATTR16	7

#define TYPE_SRAM	0
#define TYPE_PCMCIA	4

/* write-read/write-write delay (0-7:0,1,2,3,4,5,6,7) */
#define IWW5		0
#define IWW6		3
/* different area, read-write delay (0-7:0,1,2,3,4,5,6,7) */
#define IWRWD5		2
#define IWRWD6		2
/* same area, read-write delay (0-7:0,1,2,3,4,5,6,7) */
#define IWRWS5		2
#define IWRWS6		2
/* different area, read-read delay (0-7:0,1,2,3,4,5,6,7) */
#define IWRRD5		2
#define IWRRD6		2
/* same area, read-read delay (0-7:0,1,2,3,4,5,6,7) */
#define IWRRS5		0
#define IWRRS6		2
/* burst count (0-3:4,8,16,32) */
#define BST5		0
#define BST6		0
/* bus size */
#define SZ5		BUS_SZ16
#define SZ6		BUS_SZ16
/* RD hold for SRAM (0-1:0,1) */
#define RDSPL5		0
#define RDSPL6		0
/* Burst pitch (0-7:0,1,2,3,4,5,6,7) */
#define BW5		0
#define BW6		0
/* Multiplex (0-1:0,1) */
#define MPX5		0
#define MPX6		0
/* device type */
#define TYPE5		TYPE_PCMCIA
#define TYPE6		TYPE_PCMCIA
/* address setup before assert CSn for SRAM (0-7:0,1,2,3,4,5,6,7) */
#define ADS5		0
#define ADS6		0
/* address hold after negate CSn for SRAM (0-7:0,1,2,3,4,5,6,7) */
#define ADH5		0
#define ADH6		0
/* CSn assert to RD assert delay for SRAM (0-7:0,1,2,3,4,5,6,7) */
#define RDS5		0
#define RDS6		0
/* RD negate to CSn negate delay for SRAM (0-7:0,1,2,3,4,5,6,7) */
#define RDH5		0
#define RDH6		0
/* CSn assert to WE assert delay for SRAM (0-7:0,1,2,3,4,5,6,7) */
#define WTS5		0
#define WTS6		0
/* WE negate to CSn negate delay for SRAM (0-7:0,1,2,3,4,5,6,7) */
#define WTH5		0
#define WTH6		0
/* BS hold (0-1:1,2) */
#define BSH5		0
#define BSH6		0
/* wait cycle (0-15:0,1,2,3,4,5,6,7,8,9,11,13,15,17,21,25) */
#define IW5		6	/* 60ns PIO mode 4 */
#define IW6		15	/* 250ns */

#define SAA5		PCMCIA_IODYN	/* IDE area b4000000-b5ffffff */
#define SAB5		PCMCIA_IODYN	/* CF  area b6000000-b7ffffff */
#define PCWA5		0	/* additional wait A (0-3:0,15,30,50) */
#define PCWB5		0	/* additional wait B (0-3:0,15,30,50) */
/* wait B (0-15:0,1,2,3,4,5,6,7,8,9,11,13,15,17,21,25) */
#define PCIW5		12
/* Address->OE/WE assert delay A (0-7:0,1,2,3,6,9,12,15) */
#define TEDA5		2
/* Address->OE/WE assert delay B (0-7:0,1,2,3,6,9,12,15) */
#define TEDB5		4
/* OE/WE negate->Address delay A (0-7:0,1,2,3,6,9,12,15) */
#define TEHA5		2
/* OE/WE negate->Address delay B (0-7:0,1,2,3,6,9,12,15) */
#define TEHB5		3

#define CS5BCR_D	((IWW5<<28)|(IWRWD5<<24)|(IWRWS5<<20)|		\
			(IWRRD5<<16)|(IWRRS5<<12)|(BST5<<10)|		\
			(SZ5<<8)|(RDSPL5<<7)|(BW5<<4)|(MPX5<<3)|TYPE5)
#define CS5WCR_D	((ADS5<<28)|(ADH5<<24)|(RDS5<<20)|	\
			(RDH5<<16)|(WTS5<<12)|(WTH5<<8)|(BSH5<<4)|IW5)
#define CS5PCR_D	((SAA5<<28)|(SAB5<<24)|(PCWA5<<22)|		\
			(PCWB5<<20)|(PCIW5<<16)|(TEDA5<<12)|		\
			(TEDB5<<8)|(TEHA5<<4)|TEHB5)

#define SMC0_BASE       0xb0800000      /* eth0 */
#define SMC1_BASE       0xb0900000      /* eth1 */
#define CF_BASE         0xb6100000      /* Compact Flash (I/O area) */
#define IDE_BASE        0xb4000000      /* IDE */
#define PC104_IO_BASE   0xb8000000
#define PC104_MEM_BASE  0xba000000
#define SMC_IO_SIZE     0x100

#define CF_OFFSET       0x1f0
#define IDE_OFFSET      0x170

#endif /* __MACH_SH2007_H */
