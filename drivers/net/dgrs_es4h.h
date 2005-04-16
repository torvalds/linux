/************************************************************************/
/*									*/
/*	es4h.h:	Hardware definition of the ES/4h Ethernet Switch, from	*/
/*		both the host and the 3051's point of view.		*/
/*		NOTE: this name is a misnomer now that there is a PCI	*/
/*		board.  Everything that says "es4h" should really be	*/
/*		"se4".  But we'll keep the old name for now.		*/
/*									*/
/*	$Id: es4h.h,v 1.10 1996/08/22 17:16:53 rick Exp $		*/
/*									*/
/************************************************************************/

/************************************************************************/
/*									*/
/*	EISA I/O Registers.  These are located at 0x1000 * slot-number	*/
/*	plus the indicated address.  I.E. 0x4000-0x4009 for slot 4.	*/
/*									*/
/************************************************************************/

#define	ES4H_MANUFmsb	0x00		/* Read-only */
#define	ES4H_MANUFlsb	0x01		/* Read-only */
#	define ES4H_MANUF_CODE		0x1049	/* = "DBI" */

#define	ES4H_PRODUCT	0x02		/* Read-only */
#	define ES4H_PRODUCT_CODE	0x0A
#	define EPC_PRODUCT_CODE		0x03

#define	ES4H_REVISION	0x03		/* Read-only */
#	define ES4H_REVISION_CODE	0x01

#define	ES4H_EC		0x04		/* EISA Control */
#	define ES4H_EC_RESET		0x04	/* WO, EISA reset */
#	define ES4H_EC_ENABLE		0x01	/* RW, EISA enable - set to */
						/* 1 before memory enable */
#define	ES4H_PC		0x05		/* Processor Control */
#	define ES4H_PC_RESET		0x04	/* RW, 3051 reset */
#	define ES4H_PC_INT		0x08	/* WO, assert 3051 intr. 3 */

#define	ES4H_MW		0x06		/* Memory Window select and enable */
#	define ES4H_MW_ENABLE		0x80	/* WO, enable memory */
#	define ES4H_MW_SELECT_MASK	0x1f	/* WO, 32k window selected */

#define	ES4H_IS		0x07		/* Interrupt, addr select */
#	define ES4H_IS_INTMASK		0x07	/* WO, interrupt select */
#	define ES4H_IS_INTOFF		0x00		/* No IRQ */
#	define ES4H_IS_INT3		0x03		/* IRQ 3 */
#	define ES4H_IS_INT5		0x02		/* IRQ 5 */
#	define ES4H_IS_INT7		0x01		/* IRQ 7 */
#	define ES4H_IS_INT10		0x04		/* IRQ 10 */
#	define ES4H_IS_INT11		0x05		/* IRQ 11 */
#	define ES4H_IS_INT12		0x06		/* IRQ 12 */
#	define ES4H_IS_INT15		0x07		/* IRQ 15 */
#	define ES4H_IS_INTACK		0x10	/* WO, interrupt ack */
#	define ES4H_IS_INTPEND		0x10	/* RO, interrupt pending */
#	define ES4H_IS_LINEAR		0x40	/* WO, no memory windowing */
#	define ES4H_IS_AS15		0x80	/* RW, address select bit 15 */

#define	ES4H_AS_23_16	0x08		/* Address select bits 23-16 */
#define	ES4H_AS_31_24	0x09		/* Address select bits 31-24 */

#define ES4H_IO_MAX		0x09		/* Size of I/O space */

/*
 * PCI
 */
#define SE6_RESET		PLX_USEROUT

/************************************************************************/
/*									*/
/*	3051 Memory Map							*/
/*									*/
/*	Note: 3051 has 4K I-cache, 2K D-cache.  1 cycle is 50 nsec.	*/
/*									*/
/************************************************************************/
#define	SE4_NPORTS		4		/* # of ethernet ports */
#define	SE6_NPORTS		6		/* # of ethernet ports */
#define	SE_NPORTS		6		/* Max # of ethernet ports */

#define	ES4H_RAM_BASE		0x83000000	/* Base address of RAM */
#define	ES4H_RAM_SIZE		0x00200000	/* Size of RAM (2MB) */
#define	ES4H_RAM_INTBASE	0x83800000	/* Base of int-on-write RAM */
						/* a.k.a. PKT RAM */

						/* Ethernet controllers */
						/* See: i82596.h */
#define	ES4H_ETHER0_PORT	0xA2000000
#define	ES4H_ETHER0_CMD		0xA2000100
#define	ES4H_ETHER1_PORT	0xA2000200
#define	ES4H_ETHER1_CMD		0xA2000300
#define	ES4H_ETHER2_PORT	0xA2000400
#define	ES4H_ETHER2_CMD		0xA2000500
#define	ES4H_ETHER3_PORT	0xA2000600
#define	ES4H_ETHER3_CMD		0xA2000700
#define	ES4H_ETHER4_PORT	0xA2000800	/* RS SE-6 only */
#define	ES4H_ETHER4_CMD		0xA2000900	/* RS SE-6 only */
#define	ES4H_ETHER5_PORT	0xA2000A00	/* RS SE-6 only */
#define	ES4H_ETHER5_CMD		0xA2000B00	/* RS SE-6 only */

#define	ES4H_I8254		0xA2040000	/* 82C54 timers */
						/* See: i8254.h */

#define	SE4_I8254_HZ		(23000000/4)	/* EISA clock input freq. */
#define	SE4_IDT_HZ		(46000000)	/* EISA CPU freq. */
#define	SE6_I8254_HZ		(20000000/4)	/* PCI clock input freq. */
#define	SE6_IDT_HZ		(50000000)	/* PCI CPU freq. */
#define	ES4H_I8254_HZ		(23000000/4)	/* EISA clock input freq. */

#define	ES4H_GPP		0xA2050000	/* General purpose port */
	/*
	 * SE-4 (EISA) GPP bits
	 */
#	define ES4H_GPP_C0_100		0x0001	/* WO, Chan 0: 100 ohm TP */
#	define ES4H_GPP_C0_SQE		0x0002	/* WO, Chan 0: normal squelch */
#	define ES4H_GPP_C1_100		0x0004	/* WO, Chan 1: 100 ohm TP */
#	define ES4H_GPP_C1_SQE		0x0008	/* WO, Chan 1: normal squelch */
#	define ES4H_GPP_C2_100		0x0010	/* WO, Chan 2: 100 ohm TP */
#	define ES4H_GPP_C2_SQE		0x0020	/* WO, Chan 2: normal squelch */
#	define ES4H_GPP_C3_100		0x0040	/* WO, Chan 3: 100 ohm TP */
#	define ES4H_GPP_C3_SQE		0x0080	/* WO, Chan 3: normal squelch */
#	define ES4H_GPP_SQE		0x00AA	/* WO, All: normal squelch */
#	define ES4H_GPP_100		0x0055	/* WO, All: 100 ohm TP */
#	define ES4H_GPP_HOSTINT		0x0100	/* RO, cause intr. to host */
						/* Hold high > 250 nsec */
#	define SE4_GPP_EED		0x0200	/* RW, EEPROM data bit */
#	define SE4_GPP_EECS		0x0400	/* RW, EEPROM chip select */
#	define SE4_GPP_EECK		0x0800	/* RW, EEPROM clock */

	/*
	 * SE-6 (PCI) GPP bits
	 */
#	define SE6_GPP_EED		0x0001	/* RW, EEPROM data bit */
#	define SE6_GPP_EECS		0x0002	/* RW, EEPROM chip select */
#	define SE6_GPP_EECK		0x0004	/* RW, EEPROM clock */
#	define SE6_GPP_LINK		0x00fc	/* R, Link status LEDs */

#define	ES4H_INTVEC		0xA2060000	/* RO: Interrupt Vector */
#	define ES4H_IV_DMA0		0x01	/* Chan 0 DMA interrupt */
#	define ES4H_IV_PKT0		0x02	/* Chan 0 PKT interrupt */
#	define ES4H_IV_DMA1		0x04	/* Chan 1 DMA interrupt */
#	define ES4H_IV_PKT1		0x08	/* Chan 1 PKT interrupt */
#	define ES4H_IV_DMA2		0x10	/* Chan 2 DMA interrupt */
#	define ES4H_IV_PKT2		0x20	/* Chan 2 PKT interrupt */
#	define ES4H_IV_DMA3		0x40	/* Chan 3 DMA interrupt */
#	define ES4H_IV_PKT3		0x80	/* Chan 3 PKT interrupt */

#define	ES4H_INTACK		0xA2060000	/* WO: Interrupt Ack */
#	define ES4H_INTACK_8254		0x01	/* Real Time Clock (int 0) */
#	define ES4H_INTACK_HOST		0x02	/* Host (int 1) */
#	define ES4H_INTACK_PKT0		0x04	/* Chan 0 Pkt (int 2) */
#	define ES4H_INTACK_PKT1		0x08	/* Chan 1 Pkt (int 3) */
#	define ES4H_INTACK_PKT2		0x10	/* Chan 2 Pkt (int 4) */
#	define ES4H_INTACK_PKT3		0x20	/* Chan 3 Pkt (int 5) */

#define	SE6_PLX			0xA2070000	/* PLX 9060, SE-6 (PCI) only */
						/* see plx9060.h */

#define	SE6_PCI_VENDOR_ID	0x114F		/* Digi PCI vendor ID */
#define	SE6_PCI_DEVICE_ID	0x0003		/* RS SE-6 device ID */
#define	SE6_PCI_ID		((SE6_PCI_DEVICE_ID<<16) | SE6_PCI_VENDOR_ID)

/*
 *	IDT Interrupts
 */
#define	ES4H_INT_8254		IDT_INT0
#define	ES4H_INT_HOST		IDT_INT1
#define	ES4H_INT_ETHER0		IDT_INT2
#define	ES4H_INT_ETHER1		IDT_INT3
#define	ES4H_INT_ETHER2		IDT_INT4
#define	ES4H_INT_ETHER3		IDT_INT5

/*
 *	Because there are differences between the SE-4 and the SE-6,
 *	we assume that the following globals will be set up at init
 *	time in main.c to containt the appropriate constants from above
 */
extern ushort	Gpp;		/* Softcopy of GPP register */
extern ushort	EEck;		/* Clock bit */
extern ushort	EEcs;		/* CS bit */
extern ushort	EEd;		/* Data bit */
extern ulong	I8254_Hz;	/* i8254 input frequency */
extern ulong	IDT_Hz;		/* IDT CPU frequency */
extern int	Nports;		/* Number of ethernet controllers */
extern int	Nchan;		/* Nports+1 */
