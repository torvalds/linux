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
	*ptr &= ~EEPROM_DATO;	\
	*ptr &= ~EEPROM_ECLK;	\
	*ptr &= ~EEPROM_EPROT;	\
	delay();		\
	*ptr |= EEPROM_CSEL;	\
	*ptr |= EEPROM_ECLK; })

		
#define eeprom_cs_off(ptr) ({	\
	*ptr &= ~EEPROM_ECLK;	\
	*ptr &= ~EEPROM_CSEL;	\
	*ptr |= EEPROM_EPROT;	\
	*ptr |= EEPROM_ECLK; })

#define	BITS_IN_COMMAND	11
/*
 * clock in the nvram command and the register number. For the
 * national semiconductor nv ram chip the op code is 3 bits and
 * the address is 6/8 bits. 
 */
static inline void eeprom_cmd(volatile unsigned int *ctrl, unsigned cmd,
			      unsigned reg)
{
	unsigned short ser_cmd;
	int i;

	ser_cmd = cmd | (reg << (16 - BITS_IN_COMMAND));
	for (i = 0; i < BITS_IN_COMMAND; i++) {
		if (ser_cmd & (1<<15))	/* if high order bit set */
			*ctrl |= EEPROM_DATO;
		else
			*ctrl &= ~EEPROM_DATO;
		*ctrl &= ~EEPROM_ECLK;
		*ctrl |= EEPROM_ECLK;
		ser_cmd <<= 1;
	}
	*ctrl &= ~EEPROM_DATO;	/* see data sheet timing diagram */
}

unsigned short ip22_eeprom_read(volatile unsigned int *ctrl, int reg)
{
	unsigned short res = 0;
	int i;

	*ctrl &= ~EEPROM_EPROT;
	eeprom_cs_on(ctrl);
	eeprom_cmd(ctrl, EEPROM_READ, reg);

	/* clock the data ouf of serial mem */
	for (i = 0; i < 16; i++) {
		*ctrl &= ~EEPROM_ECLK;
		delay();
		*ctrl |= EEPROM_ECLK;
		delay();
		res <<= 1;
		if (*ctrl & EEPROM_DATI)
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
