/*
 * this file included by nicstar.c
 */

/*
 * nicstarmac.c
 * Read this ForeRunner's MAC address from eprom/eeprom
 */

#include <linux/kernel.h>

typedef void __iomem *virt_addr_t;

#define CYCLE_DELAY 5

/*
   This was the original definition
#define osp_MicroDelay(microsec) \
    do { int _i = 4*microsec; while (--_i > 0) { __SLOW_DOWN_IO; }} while (0)
*/
#define osp_MicroDelay(microsec) {unsigned long useconds = (microsec); \
                                  udelay((useconds));}
/*
 * The following tables represent the timing diagrams found in
 * the Data Sheet for the Xicor X25020 EEProm.  The #defines below
 * represent the bits in the NICStAR's General Purpose register
 * that must be toggled for the corresponding actions on the EEProm
 * to occur.
 */

/* Write Data To EEProm from SI line on rising edge of CLK */
/* Read Data From EEProm on falling edge of CLK */

#define CS_HIGH		0x0002	/* Chip select high */
#define CS_LOW		0x0000	/* Chip select low (active low) */
#define CLK_HIGH	0x0004	/* Clock high */
#define CLK_LOW		0x0000	/* Clock low  */
#define SI_HIGH		0x0001	/* Serial input data high */
#define SI_LOW		0x0000	/* Serial input data low */

/* Read Status Register = 0000 0101b */
#if 0
static u_int32_t rdsrtab[] = {
	CS_HIGH | CLK_HIGH,
	CS_LOW | CLK_LOW,
	CLK_HIGH,		/* 0 */
	CLK_LOW,
	CLK_HIGH,		/* 0 */
	CLK_LOW,
	CLK_HIGH,		/* 0 */
	CLK_LOW,
	CLK_HIGH,		/* 0 */
	CLK_LOW,
	CLK_HIGH,		/* 0 */
	CLK_LOW | SI_HIGH,
	CLK_HIGH | SI_HIGH,	/* 1 */
	CLK_LOW | SI_LOW,
	CLK_HIGH,		/* 0 */
	CLK_LOW | SI_HIGH,
	CLK_HIGH | SI_HIGH	/* 1 */
};
#endif /*  0  */

/* Read from EEPROM = 0000 0011b */
static u_int32_t readtab[] = {
	/*
	   CS_HIGH | CLK_HIGH,
	 */
	CS_LOW | CLK_LOW,
	CLK_HIGH,		/* 0 */
	CLK_LOW,
	CLK_HIGH,		/* 0 */
	CLK_LOW,
	CLK_HIGH,		/* 0 */
	CLK_LOW,
	CLK_HIGH,		/* 0 */
	CLK_LOW,
	CLK_HIGH,		/* 0 */
	CLK_LOW,
	CLK_HIGH,		/* 0 */
	CLK_LOW | SI_HIGH,
	CLK_HIGH | SI_HIGH,	/* 1 */
	CLK_LOW | SI_HIGH,
	CLK_HIGH | SI_HIGH	/* 1 */
};

/* Clock to read from/write to the eeprom */
static u_int32_t clocktab[] = {
	CLK_LOW,
	CLK_HIGH,
	CLK_LOW,
	CLK_HIGH,
	CLK_LOW,
	CLK_HIGH,
	CLK_LOW,
	CLK_HIGH,
	CLK_LOW,
	CLK_HIGH,
	CLK_LOW,
	CLK_HIGH,
	CLK_LOW,
	CLK_HIGH,
	CLK_LOW,
	CLK_HIGH,
	CLK_LOW
};

#define NICSTAR_REG_WRITE(bs, reg, val) \
	while ( readl(bs + STAT) & 0x0200 ) ; \
	writel((val),(base)+(reg))
#define NICSTAR_REG_READ(bs, reg) \
	readl((base)+(reg))
#define NICSTAR_REG_GENERAL_PURPOSE GP

/*
 * This routine will clock the Read_Status_reg function into the X2520
 * eeprom, then pull the result from bit 16 of the NicSTaR's General Purpose 
 * register.  
 */
#if 0
u_int32_t nicstar_read_eprom_status(virt_addr_t base)
{
	u_int32_t val;
	u_int32_t rbyte;
	int32_t i, j;

	/* Send read instruction */
	val = NICSTAR_REG_READ(base, NICSTAR_REG_GENERAL_PURPOSE) & 0xFFFFFFF0;

	for (i = 0; i < ARRAY_SIZE(rdsrtab); i++) {
		NICSTAR_REG_WRITE(base, NICSTAR_REG_GENERAL_PURPOSE,
				  (val | rdsrtab[i]));
		osp_MicroDelay(CYCLE_DELAY);
	}

	/* Done sending instruction - now pull data off of bit 16, MSB first */
	/* Data clocked out of eeprom on falling edge of clock */

	rbyte = 0;
	for (i = 7, j = 0; i >= 0; i--) {
		NICSTAR_REG_WRITE(base, NICSTAR_REG_GENERAL_PURPOSE,
				  (val | clocktab[j++]));
		rbyte |= (((NICSTAR_REG_READ(base, NICSTAR_REG_GENERAL_PURPOSE)
			    & 0x00010000) >> 16) << i);
		NICSTAR_REG_WRITE(base, NICSTAR_REG_GENERAL_PURPOSE,
				  (val | clocktab[j++]));
		osp_MicroDelay(CYCLE_DELAY);
	}
	NICSTAR_REG_WRITE(base, NICSTAR_REG_GENERAL_PURPOSE, 2);
	osp_MicroDelay(CYCLE_DELAY);
	return rbyte;
}
#endif /*  0  */

/*
 * This routine will clock the Read_data function into the X2520
 * eeprom, followed by the address to read from, through the NicSTaR's General
 * Purpose register.  
 */

static u_int8_t read_eprom_byte(virt_addr_t base, u_int8_t offset)
{
	u_int32_t val = 0;
	int i, j = 0;
	u_int8_t tempread = 0;

	val = NICSTAR_REG_READ(base, NICSTAR_REG_GENERAL_PURPOSE) & 0xFFFFFFF0;

	/* Send READ instruction */
	for (i = 0; i < ARRAY_SIZE(readtab); i++) {
		NICSTAR_REG_WRITE(base, NICSTAR_REG_GENERAL_PURPOSE,
				  (val | readtab[i]));
		osp_MicroDelay(CYCLE_DELAY);
	}

	/* Next, we need to send the byte address to read from */
	for (i = 7; i >= 0; i--) {
		NICSTAR_REG_WRITE(base, NICSTAR_REG_GENERAL_PURPOSE,
				  (val | clocktab[j++] | ((offset >> i) & 1)));
		osp_MicroDelay(CYCLE_DELAY);
		NICSTAR_REG_WRITE(base, NICSTAR_REG_GENERAL_PURPOSE,
				  (val | clocktab[j++] | ((offset >> i) & 1)));
		osp_MicroDelay(CYCLE_DELAY);
	}

	j = 0;

	/* Now, we can read data from the eeprom by clocking it in */
	for (i = 7; i >= 0; i--) {
		NICSTAR_REG_WRITE(base, NICSTAR_REG_GENERAL_PURPOSE,
				  (val | clocktab[j++]));
		osp_MicroDelay(CYCLE_DELAY);
		tempread |=
		    (((NICSTAR_REG_READ(base, NICSTAR_REG_GENERAL_PURPOSE)
		       & 0x00010000) >> 16) << i);
		NICSTAR_REG_WRITE(base, NICSTAR_REG_GENERAL_PURPOSE,
				  (val | clocktab[j++]));
		osp_MicroDelay(CYCLE_DELAY);
	}

	NICSTAR_REG_WRITE(base, NICSTAR_REG_GENERAL_PURPOSE, 2);
	osp_MicroDelay(CYCLE_DELAY);
	return tempread;
}

static void nicstar_init_eprom(virt_addr_t base)
{
	u_int32_t val;

	/*
	 * turn chip select off
	 */
	val = NICSTAR_REG_READ(base, NICSTAR_REG_GENERAL_PURPOSE) & 0xFFFFFFF0;

	NICSTAR_REG_WRITE(base, NICSTAR_REG_GENERAL_PURPOSE,
			  (val | CS_HIGH | CLK_HIGH));
	osp_MicroDelay(CYCLE_DELAY);

	NICSTAR_REG_WRITE(base, NICSTAR_REG_GENERAL_PURPOSE,
			  (val | CS_HIGH | CLK_LOW));
	osp_MicroDelay(CYCLE_DELAY);

	NICSTAR_REG_WRITE(base, NICSTAR_REG_GENERAL_PURPOSE,
			  (val | CS_HIGH | CLK_HIGH));
	osp_MicroDelay(CYCLE_DELAY);

	NICSTAR_REG_WRITE(base, NICSTAR_REG_GENERAL_PURPOSE,
			  (val | CS_HIGH | CLK_LOW));
	osp_MicroDelay(CYCLE_DELAY);
}

/*
 * This routine will be the interface to the ReadPromByte function
 * above.
 */

static void
nicstar_read_eprom(virt_addr_t base,
		   u_int8_t prom_offset, u_int8_t * buffer, u_int32_t nbytes)
{
	u_int i;

	for (i = 0; i < nbytes; i++) {
		buffer[i] = read_eprom_byte(base, prom_offset);
		++prom_offset;
		osp_MicroDelay(CYCLE_DELAY);
	}
}
