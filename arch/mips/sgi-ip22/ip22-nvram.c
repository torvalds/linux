/*
 * ip22-nvram.c: NVRAM and serial EEPROM handling.
 *
 * Copyright (C) 2003 Ladislav Michl (ladis@linux-mips.org)
 */
#include <linux/module.h>

#include <asm/sgi/hpc3.h>
#include <asm/sgi/ip22.h>

/* Control opcode for serial eeprom  */
#define EEPROM_READ	0xc000	/* serial memory read */
#define EEPROM_WEN	0x9800	/* write enable before prog modes */
#define EEPROM_WRITE	0xa000	/* serial memory write */
#define EEPROM_WRALL	0x8800	/* write all registers */
#define EEPROM_WDS	0x8000	/* disable all programming */
#define	EEPROM_PRREAD	0xc000	/* read protect register */
#define	EEPROM_PREN	0x9800	/* enable protect register mode */
#define	EEPROM_PRCLEAR	0xffff	/* clear protect register */
#define	EEPROM_PRWRITE	0xa000	/* write protect register */
#define	EEPROM_PRDS	0x8000	/* disable protect register, forever */

#define EEPROM_EPROT	0x01	/* Protect register enable */
#define EEPROM_CSEL	0x02	/* Chip select */
#define EEPROM_ECLK	0x04	/* EEPROM clock */
#define EEPROM_DATO	0x08	/* Data out */
#define EEPROM_DATI	0x10	/* Data in */

/* We need to use these functions early... */
#define delay()	({						\
	int x;							\
	for (x=0; x<100000; x++) __asm__ __volatile__(""); })

#define eeprom_cs_on(ptr) ({	\
	__raw_writel(__raw_readl(ptr) & ~EEPROM_DATO, ptr);	\
	__raw_writel(__raw_readl(ptr) & ~EEPROM_ECLK, ptr);	\
	__raw_writel(__raw_readl(ptr) & ~EEPROM_EPROT, ptr);	\
	delay();		                                \
	__raw_writel(__raw_readl(ptr) | EEPROM_CSEL, ptr);	\
	__raw_writel(__raw_readl(ptr) | EEPROM_ECLK, ptr); })


#define eeprom_cs_off(ptr) ({	\
	__raw_writel(__raw_readl(ptr) & ~EEPROM_ECLK, ptr);	\
	__raw_writel(__raw_readl(ptr) & ~EEPROM_CSEL, ptr);	\
	__raw_writel(__raw_readl(ptr) | EEPROM_EPROT, ptr);	\
	__raw_writel(__raw_readl(ptr) | EEPROM_ECLK, ptr); })

#define	BITS_IN_COMMAND	11
/*
 * clock in the nvram command and the register number. For the
 * national semiconductor nv ram chip the op code is 3 bits and
 * the address is 6/8 bits.
 */
static inline void eeprom_cmd(unsigned int *ctrl, unsigned cmd, unsigned reg)
{
	unsigned short ser_cmd;
	int i;

	ser_cmd = cmd | (reg << (16 - BITS_IN_COMMAND));
	for (i = 0; i < BITS_IN_COMMAND; i++) {
		if (ser_cmd & (1<<15))	/* if high order bit set */
			__raw_writel(__raw_readl(ctrl) | EEPROM_DATO, ctrl);
		else
			__raw_writel(__raw_readl(ctrl) & ~EEPROM_DATO, ctrl);
		__raw_writel(__raw_readl(ctrl) & ~EEPROM_ECLK, ctrl);
		delay();
		__raw_writel(__raw_readl(ctrl) | EEPROM_ECLK, ctrl);
		delay();
		ser_cmd <<= 1;
	}
	/* see data sheet timing diagram */
	__raw_writel(__raw_readl(ctrl) & ~EEPROM_DATO, ctrl);
}

unsigned short ip22_eeprom_read(unsigned int *ctrl, int reg)
{
	unsigned short res = 0;
	int i;

	__raw_writel(__raw_readl(ctrl) & ~EEPROM_EPROT, ctrl);
	eeprom_cs_on(ctrl);
	eeprom_cmd(ctrl, EEPROM_READ, reg);

	/* clock the data ouf of serial mem */
	for (i = 0; i < 16; i++) {
		__raw_writel(__raw_readl(ctrl) & ~EEPROM_ECLK, ctrl);
		delay();
		__raw_writel(__raw_readl(ctrl) | EEPROM_ECLK, ctrl);
		delay();
		res <<= 1;
		if (__raw_readl(ctrl) & EEPROM_DATI)
			res |= 1;
	}

	eeprom_cs_off(ctrl);

	return res;
}

EXPORT_SYMBOL(ip22_eeprom_read);

/*
 * Read specified register from main NVRAM
 */
unsigned short ip22_nvram_read(int reg)
{
	if (ip22_is_fullhouse())
		/* IP22 (Indigo2 aka FullHouse) stores env variables into
		 * 93CS56 Microwire Bus EEPROM 2048 Bit (128x16) */
		return ip22_eeprom_read(&hpc3c0->eeprom, reg);
	else {
		unsigned short tmp;
		/* IP24 (Indy aka Guiness) uses DS1386 8K version */
		reg <<= 1;
		tmp = hpc3c0->bbram[reg++] & 0xff;
		return (tmp << 8) | (hpc3c0->bbram[reg] & 0xff);
	}
}

EXPORT_SYMBOL(ip22_nvram_read);
