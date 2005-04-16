#ifndef __ASSEMBLY__
/* Board information for various SBS 8260 cards, which should be generic for
 * all 8260 boards.  The IMMR is now given to us so the hard define
 * will soon be removed.  All of the clock values are computed from
 * the configuration SCMR and the Power-On-Reset word.
 */

#define CPM_MAP_ADDR	((uint)0xfe000000)


/* A Board Information structure that is given to a program when
 * prom starts it up.
 */
typedef struct bd_info {
	unsigned int	bi_memstart;	/* Memory start address */
	unsigned int	bi_memsize;	/* Memory (end) size in bytes */
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
#endif /* !__ASSEMBLY__ */
