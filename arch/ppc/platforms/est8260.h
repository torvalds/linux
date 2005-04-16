/* Board information for the EST8260, which should be generic for
 * all 8260 boards.  The IMMR is now given to us so the hard define
 * will soon be removed.  All of the clock values are computed from
 * the configuration SCMR and the Power-On-Reset word.
 */
#ifndef __EST8260_PLATFORM
#define __EST8260_PLATFORM

#define CPM_MAP_ADDR		((uint)0xf0000000)

#define BOOTROM_RESTART_ADDR	((uint)0xff000104)

/* For our show_cpuinfo hooks. */
#define CPUINFO_VENDOR		"EST Corporation"
#define CPUINFO_MACHINE		"SBC8260 PowerPC"

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

#endif 	/* __EST8260_PLATFORM */
