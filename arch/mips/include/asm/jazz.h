/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995 - 1998 by Andreas Busse and Ralf Baechle
 */
#ifndef __ASM_JAZZ_H
#define __ASM_JAZZ_H

/*
 * The addresses below are virtual address. The mappings are
 * created on startup via wired entries in the tlb. The Mips
 * Magnum R3000 and R4000 machines are similar in many aspects,
 * but many hardware register are accessible at 0xb9000000 in
 * instead of 0xe0000000.
 */

#define JAZZ_LOCAL_IO_SPACE	0xe0000000

/*
 * Revision numbers in PICA_ASIC_REVISION
 *
 * 0xf0000000 - Rev1
 * 0xf0000001 - Rev2
 * 0xf0000002 - Rev3
 */
#define PICA_ASIC_REVISION	0xe0000008

/*
 * The segments of the seven segment LED are mapped
 * to the control bits as follows:
 *
 *	   (7)
 *	---------
 *	|	|
 *  (2) |	| (6)
 *	|  (1)	|
 *	---------
 *	|	|
 *  (3) |	| (5)
 *	|  (4)	|
 *	--------- . (0)
 */
#define PICA_LED		0xe000f000

/*
 * Some characters for the LED control registers
 * The original Mips machines seem to have a LED display
 * with integrated decoder while the Acer machines can
 * control each of the seven segments and the dot independently.
 * It's only a toy, anyway...
 */
#define LED_DOT			0x01
#define LED_SPACE		0x00
#define LED_0			0xfc
#define LED_1			0x60
#define LED_2			0xda
#define LED_3			0xf2
#define LED_4			0x66
#define LED_5			0xb6
#define LED_6			0xbe
#define LED_7			0xe0
#define LED_8			0xfe
#define LED_9			0xf6
#define LED_A			0xee
#define LED_b			0x3e
#define LED_C			0x9c
#define LED_d			0x7a
#define LED_E			0x9e
#define LED_F			0x8e

#ifndef __ASSEMBLY__

static __inline__ void pica_set_led(unsigned int bits)
{
	volatile unsigned int *led_register = (unsigned int *) PICA_LED;

	*led_register = bits;
}

#endif /* !__ASSEMBLY__ */

/*
 * Base address of the Sonic Ethernet adapter in Jazz machines.
 */
#define JAZZ_ETHERNET_BASE  0xe0001000

/*
 * Base address of the 53C94 SCSI hostadapter in Jazz machines.
 */
#define JAZZ_SCSI_BASE		0xe0002000

/*
 * i8042 keyboard controller for JAZZ and PICA chipsets.
 * This address is just a guess and seems to differ from
 * other mips machines such as RC3xxx...
 */
#define JAZZ_KEYBOARD_ADDRESS	0xe0005000
#define JAZZ_KEYBOARD_DATA	0xe0005000
#define JAZZ_KEYBOARD_COMMAND	0xe0005001

#ifndef __ASSEMBLY__

typedef struct {
	unsigned char data;
	unsigned char command;
} jazz_keyboard_hardware;

#define jazz_kh ((keyboard_hardware *) JAZZ_KEYBOARD_ADDRESS)

typedef struct {
	unsigned char pad0[3];
	unsigned char data;
	unsigned char pad1[3];
	unsigned char command;
} mips_keyboard_hardware;

/*
 * For now. Needs to be changed for RC3xxx support. See below.
 */
#define keyboard_hardware	jazz_keyboard_hardware

#endif /* !__ASSEMBLY__ */

/*
 * i8042 keyboard controller for most other Mips machines.
 */
#define MIPS_KEYBOARD_ADDRESS	0xb9005000
#define MIPS_KEYBOARD_DATA	0xb9005003
#define MIPS_KEYBOARD_COMMAND	0xb9005007

/*
 * Serial and parallel ports (WD 16C552) on the Mips JAZZ
 */
#define JAZZ_SERIAL1_BASE	(unsigned int)0xe0006000
#define JAZZ_SERIAL2_BASE	(unsigned int)0xe0007000
#define JAZZ_PARALLEL_BASE	(unsigned int)0xe0008000

/*
 * Dummy Device Address. Used in jazzdma.c
 */
#define JAZZ_DUMMY_DEVICE	0xe000d000

/*
 * JAZZ timer registers and interrupt no.
 * Note that the hardware timer interrupt is actually on
 * cpu level 6, but to keep compatibility with PC stuff
 * it is remapped to vector 0. See arch/mips/kernel/entry.S.
 */
#define JAZZ_TIMER_INTERVAL	0xe0000228
#define JAZZ_TIMER_REGISTER	0xe0000230

/*
 * DRAM configuration register
 */
#ifndef __ASSEMBLY__
#ifdef __MIPSEL__
typedef struct {
	unsigned int bank2 : 3;
	unsigned int bank1 : 3;
	unsigned int mem_bus_width : 1;
	unsigned int reserved2 : 1;
	unsigned int page_mode : 1;
	unsigned int reserved1 : 23;
} dram_configuration;
#else /* defined (__MIPSEB__) */
typedef struct {
	unsigned int reserved1 : 23;
	unsigned int page_mode : 1;
	unsigned int reserved2 : 1;
	unsigned int mem_bus_width : 1;
	unsigned int bank1 : 3;
	unsigned int bank2 : 3;
} dram_configuration;
#endif
#endif /* !__ASSEMBLY__ */

#define PICA_DRAM_CONFIG	0xe00fffe0

/*
 * JAZZ interrupt control registers
 */
#define JAZZ_IO_IRQ_SOURCE	0xe0010000
#define JAZZ_IO_IRQ_ENABLE	0xe0010002

/*
 * JAZZ Interrupt Level definitions
 *
 * This is somewhat broken.  For reasons which nobody can remember anymore
 * we remap the Jazz interrupts to the usual ISA style interrupt numbers.
 */
#define JAZZ_IRQ_START		24
#define JAZZ_IRQ_END		(24 + 9)
#define JAZZ_PARALLEL_IRQ	(JAZZ_IRQ_START + 0)
#define JAZZ_FLOPPY_IRQ		(JAZZ_IRQ_START + 1)
#define JAZZ_SOUND_IRQ		(JAZZ_IRQ_START + 2)
#define JAZZ_VIDEO_IRQ		(JAZZ_IRQ_START + 3)
#define JAZZ_ETHERNET_IRQ	(JAZZ_IRQ_START + 4)
#define JAZZ_SCSI_IRQ		(JAZZ_IRQ_START + 5)
#define JAZZ_KEYBOARD_IRQ	(JAZZ_IRQ_START + 6)
#define JAZZ_MOUSE_IRQ		(JAZZ_IRQ_START + 7)
#define JAZZ_SERIAL1_IRQ	(JAZZ_IRQ_START + 8)
#define JAZZ_SERIAL2_IRQ	(JAZZ_IRQ_START + 9)

#define JAZZ_TIMER_IRQ		(MIPS_CPU_IRQ_BASE+6)


/*
 * JAZZ DMA Channels
 * Note: Channels 4...7 are not used with respect to the Acer PICA-61
 * chipset which does not provide these DMA channels.
 */
#define JAZZ_SCSI_DMA		0	       /* SCSI */
#define JAZZ_FLOPPY_DMA		1	       /* FLOPPY */
#define JAZZ_AUDIOL_DMA		2	       /* AUDIO L */
#define JAZZ_AUDIOR_DMA		3	       /* AUDIO R */

/*
 * JAZZ R4030 MCT_ADR chip (DMA controller)
 * Note: Virtual Addresses !
 */
#define JAZZ_R4030_CONFIG	0xE0000000	/* R4030 config register */
#define JAZZ_R4030_REVISION	0xE0000008	/* same as PICA_ASIC_REVISION */
#define JAZZ_R4030_INV_ADDR	0xE0000010	/* Invalid Address register */

#define JAZZ_R4030_TRSTBL_BASE	0xE0000018	/* Translation Table Base */
#define JAZZ_R4030_TRSTBL_LIM	0xE0000020	/* Translation Table Limit */
#define JAZZ_R4030_TRSTBL_INV	0xE0000028	/* Translation Table Invalidate */

#define JAZZ_R4030_CACHE_MTNC	0xE0000030	/* Cache Maintenance */
#define JAZZ_R4030_R_FAIL_ADDR	0xE0000038	/* Remote Failed Address */
#define JAZZ_R4030_M_FAIL_ADDR	0xE0000040	/* Memory Failed Address */

#define JAZZ_R4030_CACHE_PTAG	0xE0000048	/* I/O Cache Physical Tag */
#define JAZZ_R4030_CACHE_LTAG	0xE0000050	/* I/O Cache Logical Tag */
#define JAZZ_R4030_CACHE_BMASK	0xE0000058	/* I/O Cache Byte Mask */
#define JAZZ_R4030_CACHE_BWIN	0xE0000060	/* I/O Cache Buffer Window */

/*
 * Remote Speed Registers.
 *
 *  0: free,	  1: Ethernet,	2: SCSI,      3: Floppy,
 *  4: RTC,	  5: Kb./Mouse	6: serial 1,  7: serial 2,
 *  8: parallel,  9: NVRAM,    10: CPU,	     11: PROM,
 * 12: reserved, 13: free,     14: 7seg LED, 15: ???
 */
#define JAZZ_R4030_REM_SPEED	0xE0000070	/* 16 Remote Speed Registers */
						/* 0xE0000070,78,80... 0xE00000E8 */
#define JAZZ_R4030_IRQ_ENABLE	0xE00000E8	/* Internal Interrupt Enable */
#define JAZZ_R4030_INVAL_ADDR	0xE0000010	/* Invalid address Register */
#define JAZZ_R4030_IRQ_SOURCE	0xE0000200	/* Interrupt Source Register */
#define JAZZ_R4030_I386_ERROR	0xE0000208	/* i386/EISA Bus Error */

/*
 * Virtual (E)ISA controller address
 */
#define JAZZ_EISA_IRQ_ACK	0xE0000238	/* EISA interrupt acknowledge */

/*
 * Access the R4030 DMA and I/O Controller
 */
#ifndef __ASSEMBLY__

static inline void r4030_delay(void)
{
__asm__ __volatile__(
	".set\tnoreorder\n\t"
	"nop\n\t"
	"nop\n\t"
	"nop\n\t"
	"nop\n\t"
	".set\treorder");
}

static inline unsigned short r4030_read_reg16(unsigned long addr)
{
	unsigned short ret = *((volatile unsigned short *)addr);
	r4030_delay();
	return ret;
}

static inline unsigned int r4030_read_reg32(unsigned long addr)
{
	unsigned int ret = *((volatile unsigned int *)addr);
	r4030_delay();
	return ret;
}

static inline void r4030_write_reg16(unsigned long addr, unsigned val)
{
	*((volatile unsigned short *)addr) = val;
	r4030_delay();
}

static inline void r4030_write_reg32(unsigned long addr, unsigned val)
{
	*((volatile unsigned int *)addr) = val;
	r4030_delay();
}

#endif /* !__ASSEMBLY__ */

#define JAZZ_FDC_BASE	0xe0003000
#define JAZZ_RTC_BASE	0xe0004000
#define JAZZ_PORT_BASE	0xe2000000

#define JAZZ_EISA_BASE	0xe3000000

#endif /* __ASM_JAZZ_H */
