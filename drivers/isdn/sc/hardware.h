/*
 * Hardware specific macros, defines and structures
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#ifndef HARDWARE_H
#define HARDWARE_H

#include <asm/param.h>			/* For HZ */

/*
 * General hardware parameters common to all ISA adapters
 */

#define MAX_CARDS	4		/* The maximum number of cards to
					   control or probe for. */

#define SIGNATURE	0x87654321	/* Board reset signature */
#define SIG_OFFSET	0x1004		/* Where to find signature in shared RAM */
#define TRACE_OFFSET	0x1008		/* Trace enable word offset in shared RAM */
#define BUFFER_OFFSET	0x1800		/* Beginning of buffers */

/* I/O Port parameters */
#define IOBASE_MIN	0x180		/* Lowest I/O port address */
#define IOBASE_MAX	0x3C0		/* Highest I/O port address */
#define IOBASE_OFFSET	0x20		/* Inter-board I/O port gap used during
					   probing */
#define FIFORD_OFFSET	0x0
#define FIFOWR_OFFSET	0x400
#define FIFOSTAT_OFFSET	0x1000
#define RESET_OFFSET	0x2800
#define PG0_OFFSET	0x3000		/* Offset from I/O Base for Page 0 register */
#define PG1_OFFSET	0x3400		/* Offset from I/O Base for Page 1 register */
#define PG2_OFFSET	0x3800		/* Offset from I/O Base for Page 2 register */
#define PG3_OFFSET	0x3C00		/* Offset from I/O Base for Page 3 register */

#define FIFO_READ	0		/* FIFO Read register */
#define FIFO_WRITE	1		/* FIFO Write rgister */
#define LO_ADDR_PTR	2		/* Extended RAM Low Addr Pointer */
#define HI_ADDR_PTR	3		/* Extended RAM High Addr Pointer */
#define NOT_USED_1	4
#define FIFO_STATUS	5		/* FIFO Status Register */
#define NOT_USED_2	6
#define MEM_OFFSET	7
#define SFT_RESET	10		/* Reset Register */
#define EXP_BASE	11		/* Shared RAM Base address */
#define EXP_PAGE0	12		/* Shared RAM Page0 register */
#define EXP_PAGE1	13		/* Shared RAM Page1 register */
#define EXP_PAGE2	14		/* Shared RAM Page2 register */
#define EXP_PAGE3	15		/* Shared RAM Page3 register */
#define IRQ_SELECT	16		/* IRQ selection register */
#define MAX_IO_REGS	17		/* Total number of I/O ports */

/* FIFO register values */
#define RF_HAS_DATA	0x01		/* fifo has data */
#define RF_QUART_FULL	0x02		/* fifo quarter full */
#define RF_HALF_FULL	0x04		/* fifo half full */
#define RF_NOT_FULL	0x08		/* fifo not full */
#define WF_HAS_DATA	0x10		/* fifo has data */
#define WF_QUART_FULL	0x20		/* fifo quarter full */
#define WF_HALF_FULL	0x40		/* fifo half full */
#define WF_NOT_FULL	0x80		/* fifo not full */

/* Shared RAM parameters */
#define SRAM_MIN	0xC0000         /* Lowest host shared RAM address */
#define SRAM_MAX	0xEFFFF         /* Highest host shared RAM address */
#define SRAM_PAGESIZE	0x4000		/* Size of one RAM page (16K) */

/* Shared RAM buffer parameters */
#define BUFFER_SIZE	0x800		/* The size of a buffer in bytes */
#define BUFFER_BASE	BUFFER_OFFSET	/* Offset from start of shared RAM
					   where buffer start */
#define BUFFERS_MAX	16		/* Maximum number of send/receive
					   buffers per channel */
#define HDLC_PROTO	0x01		/* Frame Format for Layer 2 */

#define BRI_BOARD	0
#define POTS_BOARD	1
#define PRI_BOARD	2

/*
 * Specific hardware parameters for the DataCommute/BRI
 */
#define BRI_CHANNELS	2		/* Number of B channels */
#define BRI_BASEPG_VAL	0x98
#define BRI_MAGIC	0x60000		/* Magic Number */
#define BRI_MEMSIZE	0x10000		/* Ammount of RAM (64K) */
#define BRI_PARTNO	"72-029"
#define BRI_FEATURES	ISDN_FEATURE_L2_HDLC | ISDN_FEATURE_L3_TRANS;
/*
 * Specific hardware parameters for the DataCommute/PRI
 */
#define PRI_CHANNELS	23		/* Number of B channels */
#define PRI_BASEPG_VAL	0x88
#define PRI_MAGIC	0x20000		/* Magic Number */
#define PRI_MEMSIZE	0x100000	/* Amount of RAM (1M) */
#define PRI_PARTNO	"72-030"
#define PRI_FEATURES	ISDN_FEATURE_L2_HDLC | ISDN_FEATURE_L3_TRANS;

/*
 * Some handy macros
 */

/* Determine if a channel number is valid for the adapter */
#define IS_VALID_CHANNEL(y,x)	((x>0) && (x <= sc_adapter[y]->channels))

#endif
