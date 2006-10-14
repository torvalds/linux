/*
 * NAND flash simulator.
 *
 * Author: Artem B. Bityuckiy <dedekind@oktetlabs.ru>, <dedekind@infradead.org>
 *
 * Copyright (C) 2004 Nokia Corporation
 *
 * Note: NS means "NAND Simulator".
 * Note: Input means input TO flash chip, output means output FROM chip.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
 * Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA
 *
 * $Id: nandsim.c,v 1.8 2005/03/19 15:33:56 dedekind Exp $
 */

#include <linux/init.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>
#include <linux/delay.h>

/* Default simulator parameters values */
#if !defined(CONFIG_NANDSIM_FIRST_ID_BYTE)  || \
    !defined(CONFIG_NANDSIM_SECOND_ID_BYTE) || \
    !defined(CONFIG_NANDSIM_THIRD_ID_BYTE)  || \
    !defined(CONFIG_NANDSIM_FOURTH_ID_BYTE)
#define CONFIG_NANDSIM_FIRST_ID_BYTE  0x98
#define CONFIG_NANDSIM_SECOND_ID_BYTE 0x39
#define CONFIG_NANDSIM_THIRD_ID_BYTE  0xFF /* No byte */
#define CONFIG_NANDSIM_FOURTH_ID_BYTE 0xFF /* No byte */
#endif

#ifndef CONFIG_NANDSIM_ACCESS_DELAY
#define CONFIG_NANDSIM_ACCESS_DELAY 25
#endif
#ifndef CONFIG_NANDSIM_PROGRAMM_DELAY
#define CONFIG_NANDSIM_PROGRAMM_DELAY 200
#endif
#ifndef CONFIG_NANDSIM_ERASE_DELAY
#define CONFIG_NANDSIM_ERASE_DELAY 2
#endif
#ifndef CONFIG_NANDSIM_OUTPUT_CYCLE
#define CONFIG_NANDSIM_OUTPUT_CYCLE 40
#endif
#ifndef CONFIG_NANDSIM_INPUT_CYCLE
#define CONFIG_NANDSIM_INPUT_CYCLE  50
#endif
#ifndef CONFIG_NANDSIM_BUS_WIDTH
#define CONFIG_NANDSIM_BUS_WIDTH  8
#endif
#ifndef CONFIG_NANDSIM_DO_DELAYS
#define CONFIG_NANDSIM_DO_DELAYS  0
#endif
#ifndef CONFIG_NANDSIM_LOG
#define CONFIG_NANDSIM_LOG        0
#endif
#ifndef CONFIG_NANDSIM_DBG
#define CONFIG_NANDSIM_DBG        0
#endif

static uint first_id_byte  = CONFIG_NANDSIM_FIRST_ID_BYTE;
static uint second_id_byte = CONFIG_NANDSIM_SECOND_ID_BYTE;
static uint third_id_byte  = CONFIG_NANDSIM_THIRD_ID_BYTE;
static uint fourth_id_byte = CONFIG_NANDSIM_FOURTH_ID_BYTE;
static uint access_delay   = CONFIG_NANDSIM_ACCESS_DELAY;
static uint programm_delay = CONFIG_NANDSIM_PROGRAMM_DELAY;
static uint erase_delay    = CONFIG_NANDSIM_ERASE_DELAY;
static uint output_cycle   = CONFIG_NANDSIM_OUTPUT_CYCLE;
static uint input_cycle    = CONFIG_NANDSIM_INPUT_CYCLE;
static uint bus_width      = CONFIG_NANDSIM_BUS_WIDTH;
static uint do_delays      = CONFIG_NANDSIM_DO_DELAYS;
static uint log            = CONFIG_NANDSIM_LOG;
static uint dbg            = CONFIG_NANDSIM_DBG;

module_param(first_id_byte,  uint, 0400);
module_param(second_id_byte, uint, 0400);
module_param(third_id_byte,  uint, 0400);
module_param(fourth_id_byte, uint, 0400);
module_param(access_delay,   uint, 0400);
module_param(programm_delay, uint, 0400);
module_param(erase_delay,    uint, 0400);
module_param(output_cycle,   uint, 0400);
module_param(input_cycle,    uint, 0400);
module_param(bus_width,      uint, 0400);
module_param(do_delays,      uint, 0400);
module_param(log,            uint, 0400);
module_param(dbg,            uint, 0400);

MODULE_PARM_DESC(first_id_byte,  "The fist byte returned by NAND Flash 'read ID' command (manufaturer ID)");
MODULE_PARM_DESC(second_id_byte, "The second byte returned by NAND Flash 'read ID' command (chip ID)");
MODULE_PARM_DESC(third_id_byte,  "The third byte returned by NAND Flash 'read ID' command");
MODULE_PARM_DESC(fourth_id_byte, "The fourth byte returned by NAND Flash 'read ID' command");
MODULE_PARM_DESC(access_delay,   "Initial page access delay (microiseconds)");
MODULE_PARM_DESC(programm_delay, "Page programm delay (microseconds");
MODULE_PARM_DESC(erase_delay,    "Sector erase delay (milliseconds)");
MODULE_PARM_DESC(output_cycle,   "Word output (from flash) time (nanodeconds)");
MODULE_PARM_DESC(input_cycle,    "Word input (to flash) time (nanodeconds)");
MODULE_PARM_DESC(bus_width,      "Chip's bus width (8- or 16-bit)");
MODULE_PARM_DESC(do_delays,      "Simulate NAND delays using busy-waits if not zero");
MODULE_PARM_DESC(log,            "Perform logging if not zero");
MODULE_PARM_DESC(dbg,            "Output debug information if not zero");

/* The largest possible page size */
#define NS_LARGEST_PAGE_SIZE	2048

/* The prefix for simulator output */
#define NS_OUTPUT_PREFIX "[nandsim]"

/* Simulator's output macros (logging, debugging, warning, error) */
#define NS_LOG(args...) \
	do { if (log) printk(KERN_DEBUG NS_OUTPUT_PREFIX " log: " args); } while(0)
#define NS_DBG(args...) \
	do { if (dbg) printk(KERN_DEBUG NS_OUTPUT_PREFIX " debug: " args); } while(0)
#define NS_WARN(args...) \
	do { printk(KERN_WARNING NS_OUTPUT_PREFIX " warnig: " args); } while(0)
#define NS_ERR(args...) \
	do { printk(KERN_ERR NS_OUTPUT_PREFIX " errorr: " args); } while(0)

/* Busy-wait delay macros (microseconds, milliseconds) */
#define NS_UDELAY(us) \
        do { if (do_delays) udelay(us); } while(0)
#define NS_MDELAY(us) \
        do { if (do_delays) mdelay(us); } while(0)

/* Is the nandsim structure initialized ? */
#define NS_IS_INITIALIZED(ns) ((ns)->geom.totsz != 0)

/* Good operation completion status */
#define NS_STATUS_OK(ns) (NAND_STATUS_READY | (NAND_STATUS_WP * ((ns)->lines.wp == 0)))

/* Operation failed completion status */
#define NS_STATUS_FAILED(ns) (NAND_STATUS_FAIL | NS_STATUS_OK(ns))

/* Calculate the page offset in flash RAM image by (row, column) address */
#define NS_RAW_OFFSET(ns) \
	(((ns)->regs.row << (ns)->geom.pgshift) + ((ns)->regs.row * (ns)->geom.oobsz) + (ns)->regs.column)

/* Calculate the OOB offset in flash RAM image by (row, column) address */
#define NS_RAW_OFFSET_OOB(ns) (NS_RAW_OFFSET(ns) + ns->geom.pgsz)

/* After a command is input, the simulator goes to one of the following states */
#define STATE_CMD_READ0        0x00000001 /* read data from the beginning of page */
#define STATE_CMD_READ1        0x00000002 /* read data from the second half of page */
#define STATE_CMD_READSTART      0x00000003 /* read data second command (large page devices) */
#define STATE_CMD_PAGEPROG     0x00000004 /* start page programm */
#define STATE_CMD_READOOB      0x00000005 /* read OOB area */
#define STATE_CMD_ERASE1       0x00000006 /* sector erase first command */
#define STATE_CMD_STATUS       0x00000007 /* read status */
#define STATE_CMD_STATUS_M     0x00000008 /* read multi-plane status (isn't implemented) */
#define STATE_CMD_SEQIN        0x00000009 /* sequential data imput */
#define STATE_CMD_READID       0x0000000A /* read ID */
#define STATE_CMD_ERASE2       0x0000000B /* sector erase second command */
#define STATE_CMD_RESET        0x0000000C /* reset */
#define STATE_CMD_MASK         0x0000000F /* command states mask */

/* After an addres is input, the simulator goes to one of these states */
#define STATE_ADDR_PAGE        0x00000010 /* full (row, column) address is accepted */
#define STATE_ADDR_SEC         0x00000020 /* sector address was accepted */
#define STATE_ADDR_ZERO        0x00000030 /* one byte zero address was accepted */
#define STATE_ADDR_MASK        0x00000030 /* address states mask */

/* Durind data input/output the simulator is in these states */
#define STATE_DATAIN           0x00000100 /* waiting for data input */
#define STATE_DATAIN_MASK      0x00000100 /* data input states mask */

#define STATE_DATAOUT          0x00001000 /* waiting for page data output */
#define STATE_DATAOUT_ID       0x00002000 /* waiting for ID bytes output */
#define STATE_DATAOUT_STATUS   0x00003000 /* waiting for status output */
#define STATE_DATAOUT_STATUS_M 0x00004000 /* waiting for multi-plane status output */
#define STATE_DATAOUT_MASK     0x00007000 /* data output states mask */

/* Previous operation is done, ready to accept new requests */
#define STATE_READY            0x00000000

/* This state is used to mark that the next state isn't known yet */
#define STATE_UNKNOWN          0x10000000

/* Simulator's actions bit masks */
#define ACTION_CPY       0x00100000 /* copy page/OOB to the internal buffer */
#define ACTION_PRGPAGE   0x00200000 /* programm the internal buffer to flash */
#define ACTION_SECERASE  0x00300000 /* erase sector */
#define ACTION_ZEROOFF   0x00400000 /* don't add any offset to address */
#define ACTION_HALFOFF   0x00500000 /* add to address half of page */
#define ACTION_OOBOFF    0x00600000 /* add to address OOB offset */
#define ACTION_MASK      0x00700000 /* action mask */

#define NS_OPER_NUM      12 /* Number of operations supported by the simulator */
#define NS_OPER_STATES   6  /* Maximum number of states in operation */

#define OPT_ANY          0xFFFFFFFF /* any chip supports this operation */
#define OPT_PAGE256      0x00000001 /* 256-byte  page chips */
#define OPT_PAGE512      0x00000002 /* 512-byte  page chips */
#define OPT_PAGE2048     0x00000008 /* 2048-byte page chips */
#define OPT_SMARTMEDIA   0x00000010 /* SmartMedia technology chips */
#define OPT_AUTOINCR     0x00000020 /* page number auto inctimentation is possible */
#define OPT_PAGE512_8BIT 0x00000040 /* 512-byte page chips with 8-bit bus width */
#define OPT_LARGEPAGE    (OPT_PAGE2048) /* 2048-byte page chips */
#define OPT_SMALLPAGE    (OPT_PAGE256  | OPT_PAGE512)  /* 256 and 512-byte page chips */

/* Remove action bits ftom state */
#define NS_STATE(x) ((x) & ~ACTION_MASK)

/*
 * Maximum previous states which need to be saved. Currently saving is
 * only needed for page programm operation with preceeded read command
 * (which is only valid for 512-byte pages).
 */
#define NS_MAX_PREVSTATES 1

/*
 * A union to represent flash memory contents and flash buffer.
 */
union ns_mem {
	u_char *byte;    /* for byte access */
	uint16_t *word;  /* for 16-bit word access */
};

/*
 * The structure which describes all the internal simulator data.
 */
struct nandsim {
	struct mtd_partition part;

	uint busw;              /* flash chip bus width (8 or 16) */
	u_char ids[4];          /* chip's ID bytes */
	uint32_t options;       /* chip's characteristic bits */
	uint32_t state;         /* current chip state */
	uint32_t nxstate;       /* next expected state */

	uint32_t *op;           /* current operation, NULL operations isn't known yet  */
	uint32_t pstates[NS_MAX_PREVSTATES]; /* previous states */
	uint16_t npstates;      /* number of previous states saved */
	uint16_t stateidx;      /* current state index */

	/* The simulated NAND flash pages array */
	union ns_mem *pages;

	/* Internal buffer of page + OOB size bytes */
	union ns_mem buf;

	/* NAND flash "geometry" */
	struct nandsin_geometry {
		uint32_t totsz;     /* total flash size, bytes */
		uint32_t secsz;     /* flash sector (erase block) size, bytes */
		uint pgsz;          /* NAND flash page size, bytes */
		uint oobsz;         /* page OOB area size, bytes */
		uint32_t totszoob;  /* total flash size including OOB, bytes */
		uint pgszoob;       /* page size including OOB , bytes*/
		uint secszoob;      /* sector size including OOB, bytes */
		uint pgnum;         /* total number of pages */
		uint pgsec;         /* number of pages per sector */
		uint secshift;      /* bits number in sector size */
		uint pgshift;       /* bits number in page size */
		uint oobshift;      /* bits number in OOB size */
		uint pgaddrbytes;   /* bytes per page address */
		uint secaddrbytes;  /* bytes per sector address */
		uint idbytes;       /* the number ID bytes that this chip outputs */
	} geom;

	/* NAND flash internal registers */
	struct nandsim_regs {
		unsigned command; /* the command register */
		u_char   status;  /* the status register */
		uint     row;     /* the page number */
		uint     column;  /* the offset within page */
		uint     count;   /* internal counter */
		uint     num;     /* number of bytes which must be processed */
		uint     off;     /* fixed page offset */
	} regs;

	/* NAND flash lines state */
        struct ns_lines_status {
                int ce;  /* chip Enable */
                int cle; /* command Latch Enable */
                int ale; /* address Latch Enable */
                int wp;  /* write Protect */
        } lines;
};

/*
 * Operations array. To perform any operation the simulator must pass
 * through the correspondent states chain.
 */
static struct nandsim_operations {
	uint32_t reqopts;  /* options which are required to perform the operation */
	uint32_t states[NS_OPER_STATES]; /* operation's states */
} ops[NS_OPER_NUM] = {
	/* Read page + OOB from the beginning */
	{OPT_SMALLPAGE, {STATE_CMD_READ0 | ACTION_ZEROOFF, STATE_ADDR_PAGE | ACTION_CPY,
			STATE_DATAOUT, STATE_READY}},
	/* Read page + OOB from the second half */
	{OPT_PAGE512_8BIT, {STATE_CMD_READ1 | ACTION_HALFOFF, STATE_ADDR_PAGE | ACTION_CPY,
			STATE_DATAOUT, STATE_READY}},
	/* Read OOB */
	{OPT_SMALLPAGE, {STATE_CMD_READOOB | ACTION_OOBOFF, STATE_ADDR_PAGE | ACTION_CPY,
			STATE_DATAOUT, STATE_READY}},
	/* Programm page starting from the beginning */
	{OPT_ANY, {STATE_CMD_SEQIN, STATE_ADDR_PAGE, STATE_DATAIN,
			STATE_CMD_PAGEPROG | ACTION_PRGPAGE, STATE_READY}},
	/* Programm page starting from the beginning */
	{OPT_SMALLPAGE, {STATE_CMD_READ0, STATE_CMD_SEQIN | ACTION_ZEROOFF, STATE_ADDR_PAGE,
			      STATE_DATAIN, STATE_CMD_PAGEPROG | ACTION_PRGPAGE, STATE_READY}},
	/* Programm page starting from the second half */
	{OPT_PAGE512, {STATE_CMD_READ1, STATE_CMD_SEQIN | ACTION_HALFOFF, STATE_ADDR_PAGE,
			      STATE_DATAIN, STATE_CMD_PAGEPROG | ACTION_PRGPAGE, STATE_READY}},
	/* Programm OOB */
	{OPT_SMALLPAGE, {STATE_CMD_READOOB, STATE_CMD_SEQIN | ACTION_OOBOFF, STATE_ADDR_PAGE,
			      STATE_DATAIN, STATE_CMD_PAGEPROG | ACTION_PRGPAGE, STATE_READY}},
	/* Erase sector */
	{OPT_ANY, {STATE_CMD_ERASE1, STATE_ADDR_SEC, STATE_CMD_ERASE2 | ACTION_SECERASE, STATE_READY}},
	/* Read status */
	{OPT_ANY, {STATE_CMD_STATUS, STATE_DATAOUT_STATUS, STATE_READY}},
	/* Read multi-plane status */
	{OPT_SMARTMEDIA, {STATE_CMD_STATUS_M, STATE_DATAOUT_STATUS_M, STATE_READY}},
	/* Read ID */
	{OPT_ANY, {STATE_CMD_READID, STATE_ADDR_ZERO, STATE_DATAOUT_ID, STATE_READY}},
	/* Large page devices read page */
	{OPT_LARGEPAGE, {STATE_CMD_READ0, STATE_ADDR_PAGE, STATE_CMD_READSTART | ACTION_CPY,
			       STATE_DATAOUT, STATE_READY}}
};

/* MTD structure for NAND controller */
static struct mtd_info *nsmtd;

static u_char ns_verify_buf[NS_LARGEST_PAGE_SIZE];

/*
 * Allocate array of page pointers and initialize the array to NULL
 * pointers.
 *
 * RETURNS: 0 if success, -ENOMEM if memory alloc fails.
 */
static int alloc_device(struct nandsim *ns)
{
	int i;

	ns->pages = vmalloc(ns->geom.pgnum * sizeof(union ns_mem));
	if (!ns->pages) {
		NS_ERR("alloc_map: unable to allocate page array\n");
		return -ENOMEM;
	}
	for (i = 0; i < ns->geom.pgnum; i++) {
		ns->pages[i].byte = NULL;
	}

	return 0;
}

/*
 * Free any allocated pages, and free the array of page pointers.
 */
static void free_device(struct nandsim *ns)
{
	int i;

	if (ns->pages) {
		for (i = 0; i < ns->geom.pgnum; i++) {
			if (ns->pages[i].byte)
				kfree(ns->pages[i].byte);
		}
		vfree(ns->pages);
	}
}

/*
 * Initialize the nandsim structure.
 *
 * RETURNS: 0 if success, -ERRNO if failure.
 */
static int init_nandsim(struct mtd_info *mtd)
{
	struct nand_chip *chip = (struct nand_chip *)mtd->priv;
	struct nandsim   *ns   = (struct nandsim *)(chip->priv);
	int i;

	if (NS_IS_INITIALIZED(ns)) {
		NS_ERR("init_nandsim: nandsim is already initialized\n");
		return -EIO;
	}

	/* Force mtd to not do delays */
	chip->chip_delay = 0;

	/* Initialize the NAND flash parameters */
	ns->busw = chip->options & NAND_BUSWIDTH_16 ? 16 : 8;
	ns->geom.totsz    = mtd->size;
	ns->geom.pgsz     = mtd->writesize;
	ns->geom.oobsz    = mtd->oobsize;
	ns->geom.secsz    = mtd->erasesize;
	ns->geom.pgszoob  = ns->geom.pgsz + ns->geom.oobsz;
	ns->geom.pgnum    = ns->geom.totsz / ns->geom.pgsz;
	ns->geom.totszoob = ns->geom.totsz + ns->geom.pgnum * ns->geom.oobsz;
	ns->geom.secshift = ffs(ns->geom.secsz) - 1;
	ns->geom.pgshift  = chip->page_shift;
	ns->geom.oobshift = ffs(ns->geom.oobsz) - 1;
	ns->geom.pgsec    = ns->geom.secsz / ns->geom.pgsz;
	ns->geom.secszoob = ns->geom.secsz + ns->geom.oobsz * ns->geom.pgsec;
	ns->options = 0;

	if (ns->geom.pgsz == 256) {
		ns->options |= OPT_PAGE256;
	}
	else if (ns->geom.pgsz == 512) {
		ns->options |= (OPT_PAGE512 | OPT_AUTOINCR);
		if (ns->busw == 8)
			ns->options |= OPT_PAGE512_8BIT;
	} else if (ns->geom.pgsz == 2048) {
		ns->options |= OPT_PAGE2048;
	} else {
		NS_ERR("init_nandsim: unknown page size %u\n", ns->geom.pgsz);
		return -EIO;
	}

	if (ns->options & OPT_SMALLPAGE) {
		if (ns->geom.totsz < (64 << 20)) {
			ns->geom.pgaddrbytes  = 3;
			ns->geom.secaddrbytes = 2;
		} else {
			ns->geom.pgaddrbytes  = 4;
			ns->geom.secaddrbytes = 3;
		}
	} else {
		if (ns->geom.totsz <= (128 << 20)) {
			ns->geom.pgaddrbytes  = 5;
			ns->geom.secaddrbytes = 2;
		} else {
			ns->geom.pgaddrbytes  = 5;
			ns->geom.secaddrbytes = 3;
		}
	}

	/* Detect how many ID bytes the NAND chip outputs */
        for (i = 0; nand_flash_ids[i].name != NULL; i++) {
                if (second_id_byte != nand_flash_ids[i].id)
                        continue;
		if (!(nand_flash_ids[i].options & NAND_NO_AUTOINCR))
			ns->options |= OPT_AUTOINCR;
	}

	if (ns->busw == 16)
		NS_WARN("16-bit flashes support wasn't tested\n");

	printk("flash size: %u MiB\n",          ns->geom.totsz >> 20);
	printk("page size: %u bytes\n",         ns->geom.pgsz);
	printk("OOB area size: %u bytes\n",     ns->geom.oobsz);
	printk("sector size: %u KiB\n",         ns->geom.secsz >> 10);
	printk("pages number: %u\n",            ns->geom.pgnum);
	printk("pages per sector: %u\n",        ns->geom.pgsec);
	printk("bus width: %u\n",               ns->busw);
	printk("bits in sector size: %u\n",     ns->geom.secshift);
	printk("bits in page size: %u\n",       ns->geom.pgshift);
	printk("bits in OOB size: %u\n",        ns->geom.oobshift);
	printk("flash size with OOB: %u KiB\n", ns->geom.totszoob >> 10);
	printk("page address bytes: %u\n",      ns->geom.pgaddrbytes);
	printk("sector address bytes: %u\n",    ns->geom.secaddrbytes);
	printk("options: %#x\n",                ns->options);

	if (alloc_device(ns) != 0)
		goto error;

	/* Allocate / initialize the internal buffer */
	ns->buf.byte = kmalloc(ns->geom.pgszoob, GFP_KERNEL);
	if (!ns->buf.byte) {
		NS_ERR("init_nandsim: unable to allocate %u bytes for the internal buffer\n",
			ns->geom.pgszoob);
		goto error;
	}
	memset(ns->buf.byte, 0xFF, ns->geom.pgszoob);

	/* Fill the partition_info structure */
	ns->part.name   = "NAND simulator partition";
	ns->part.offset = 0;
	ns->part.size   = ns->geom.totsz;

	return 0;

error:
	free_device(ns);

	return -ENOMEM;
}

/*
 * Free the nandsim structure.
 */
static void free_nandsim(struct nandsim *ns)
{
	kfree(ns->buf.byte);
	free_device(ns);

	return;
}

/*
 * Returns the string representation of 'state' state.
 */
static char *get_state_name(uint32_t state)
{
	switch (NS_STATE(state)) {
		case STATE_CMD_READ0:
			return "STATE_CMD_READ0";
		case STATE_CMD_READ1:
			return "STATE_CMD_READ1";
		case STATE_CMD_PAGEPROG:
			return "STATE_CMD_PAGEPROG";
		case STATE_CMD_READOOB:
			return "STATE_CMD_READOOB";
		case STATE_CMD_READSTART:
			return "STATE_CMD_READSTART";
		case STATE_CMD_ERASE1:
			return "STATE_CMD_ERASE1";
		case STATE_CMD_STATUS:
			return "STATE_CMD_STATUS";
		case STATE_CMD_STATUS_M:
			return "STATE_CMD_STATUS_M";
		case STATE_CMD_SEQIN:
			return "STATE_CMD_SEQIN";
		case STATE_CMD_READID:
			return "STATE_CMD_READID";
		case STATE_CMD_ERASE2:
			return "STATE_CMD_ERASE2";
		case STATE_CMD_RESET:
			return "STATE_CMD_RESET";
		case STATE_ADDR_PAGE:
			return "STATE_ADDR_PAGE";
		case STATE_ADDR_SEC:
			return "STATE_ADDR_SEC";
		case STATE_ADDR_ZERO:
			return "STATE_ADDR_ZERO";
		case STATE_DATAIN:
			return "STATE_DATAIN";
		case STATE_DATAOUT:
			return "STATE_DATAOUT";
		case STATE_DATAOUT_ID:
			return "STATE_DATAOUT_ID";
		case STATE_DATAOUT_STATUS:
			return "STATE_DATAOUT_STATUS";
		case STATE_DATAOUT_STATUS_M:
			return "STATE_DATAOUT_STATUS_M";
		case STATE_READY:
			return "STATE_READY";
		case STATE_UNKNOWN:
			return "STATE_UNKNOWN";
	}

	NS_ERR("get_state_name: unknown state, BUG\n");
	return NULL;
}

/*
 * Check if command is valid.
 *
 * RETURNS: 1 if wrong command, 0 if right.
 */
static int check_command(int cmd)
{
	switch (cmd) {

	case NAND_CMD_READ0:
	case NAND_CMD_READSTART:
	case NAND_CMD_PAGEPROG:
	case NAND_CMD_READOOB:
	case NAND_CMD_ERASE1:
	case NAND_CMD_STATUS:
	case NAND_CMD_SEQIN:
	case NAND_CMD_READID:
	case NAND_CMD_ERASE2:
	case NAND_CMD_RESET:
	case NAND_CMD_READ1:
		return 0;

	case NAND_CMD_STATUS_MULTI:
	default:
		return 1;
	}
}

/*
 * Returns state after command is accepted by command number.
 */
static uint32_t get_state_by_command(unsigned command)
{
	switch (command) {
		case NAND_CMD_READ0:
			return STATE_CMD_READ0;
		case NAND_CMD_READ1:
			return STATE_CMD_READ1;
		case NAND_CMD_PAGEPROG:
			return STATE_CMD_PAGEPROG;
		case NAND_CMD_READSTART:
			return STATE_CMD_READSTART;
		case NAND_CMD_READOOB:
			return STATE_CMD_READOOB;
		case NAND_CMD_ERASE1:
			return STATE_CMD_ERASE1;
		case NAND_CMD_STATUS:
			return STATE_CMD_STATUS;
		case NAND_CMD_STATUS_MULTI:
			return STATE_CMD_STATUS_M;
		case NAND_CMD_SEQIN:
			return STATE_CMD_SEQIN;
		case NAND_CMD_READID:
			return STATE_CMD_READID;
		case NAND_CMD_ERASE2:
			return STATE_CMD_ERASE2;
		case NAND_CMD_RESET:
			return STATE_CMD_RESET;
	}

	NS_ERR("get_state_by_command: unknown command, BUG\n");
	return 0;
}

/*
 * Move an address byte to the correspondent internal register.
 */
static inline void accept_addr_byte(struct nandsim *ns, u_char bt)
{
	uint byte = (uint)bt;

	if (ns->regs.count < (ns->geom.pgaddrbytes - ns->geom.secaddrbytes))
		ns->regs.column |= (byte << 8 * ns->regs.count);
	else {
		ns->regs.row |= (byte << 8 * (ns->regs.count -
						ns->geom.pgaddrbytes +
						ns->geom.secaddrbytes));
	}

	return;
}

/*
 * Switch to STATE_READY state.
 */
static inline void switch_to_ready_state(struct nandsim *ns, u_char status)
{
	NS_DBG("switch_to_ready_state: switch to %s state\n", get_state_name(STATE_READY));

	ns->state       = STATE_READY;
	ns->nxstate     = STATE_UNKNOWN;
	ns->op          = NULL;
	ns->npstates    = 0;
	ns->stateidx    = 0;
	ns->regs.num    = 0;
	ns->regs.count  = 0;
	ns->regs.off    = 0;
	ns->regs.row    = 0;
	ns->regs.column = 0;
	ns->regs.status = status;
}

/*
 * If the operation isn't known yet, try to find it in the global array
 * of supported operations.
 *
 * Operation can be unknown because of the following.
 *   1. New command was accepted and this is the firs call to find the
 *      correspondent states chain. In this case ns->npstates = 0;
 *   2. There is several operations which begin with the same command(s)
 *      (for example program from the second half and read from the
 *      second half operations both begin with the READ1 command). In this
 *      case the ns->pstates[] array contains previous states.
 *
 * Thus, the function tries to find operation containing the following
 * states (if the 'flag' parameter is 0):
 *    ns->pstates[0], ... ns->pstates[ns->npstates], ns->state
 *
 * If (one and only one) matching operation is found, it is accepted (
 * ns->ops, ns->state, ns->nxstate are initialized, ns->npstate is
 * zeroed).
 *
 * If there are several maches, the current state is pushed to the
 * ns->pstates.
 *
 * The operation can be unknown only while commands are input to the chip.
 * As soon as address command is accepted, the operation must be known.
 * In such situation the function is called with 'flag' != 0, and the
 * operation is searched using the following pattern:
 *     ns->pstates[0], ... ns->pstates[ns->npstates], <address input>
 *
 * It is supposed that this pattern must either match one operation on
 * none. There can't be ambiguity in that case.
 *
 * If no matches found, the functions does the following:
 *   1. if there are saved states present, try to ignore them and search
 *      again only using the last command. If nothing was found, switch
 *      to the STATE_READY state.
 *   2. if there are no saved states, switch to the STATE_READY state.
 *
 * RETURNS: -2 - no matched operations found.
 *          -1 - several matches.
 *           0 - operation is found.
 */
static int find_operation(struct nandsim *ns, uint32_t flag)
{
	int opsfound = 0;
	int i, j, idx = 0;

	for (i = 0; i < NS_OPER_NUM; i++) {

		int found = 1;

		if (!(ns->options & ops[i].reqopts))
			/* Ignore operations we can't perform */
			continue;

		if (flag) {
			if (!(ops[i].states[ns->npstates] & STATE_ADDR_MASK))
				continue;
		} else {
			if (NS_STATE(ns->state) != NS_STATE(ops[i].states[ns->npstates]))
				continue;
		}

		for (j = 0; j < ns->npstates; j++)
			if (NS_STATE(ops[i].states[j]) != NS_STATE(ns->pstates[j])
				&& (ns->options & ops[idx].reqopts)) {
				found = 0;
				break;
			}

		if (found) {
			idx = i;
			opsfound += 1;
		}
	}

	if (opsfound == 1) {
		/* Exact match */
		ns->op = &ops[idx].states[0];
		if (flag) {
			/*
			 * In this case the find_operation function was
			 * called when address has just began input. But it isn't
			 * yet fully input and the current state must
			 * not be one of STATE_ADDR_*, but the STATE_ADDR_*
			 * state must be the next state (ns->nxstate).
			 */
			ns->stateidx = ns->npstates - 1;
		} else {
			ns->stateidx = ns->npstates;
		}
		ns->npstates = 0;
		ns->state = ns->op[ns->stateidx];
		ns->nxstate = ns->op[ns->stateidx + 1];
		NS_DBG("find_operation: operation found, index: %d, state: %s, nxstate %s\n",
				idx, get_state_name(ns->state), get_state_name(ns->nxstate));
		return 0;
	}

	if (opsfound == 0) {
		/* Nothing was found. Try to ignore previous commands (if any) and search again */
		if (ns->npstates != 0) {
			NS_DBG("find_operation: no operation found, try again with state %s\n",
					get_state_name(ns->state));
			ns->npstates = 0;
			return find_operation(ns, 0);

		}
		NS_DBG("find_operation: no operations found\n");
		switch_to_ready_state(ns, NS_STATUS_FAILED(ns));
		return -2;
	}

	if (flag) {
		/* This shouldn't happen */
		NS_DBG("find_operation: BUG, operation must be known if address is input\n");
		return -2;
	}

	NS_DBG("find_operation: there is still ambiguity\n");

	ns->pstates[ns->npstates++] = ns->state;

	return -1;
}

/*
 * Returns a pointer to the current page.
 */
static inline union ns_mem *NS_GET_PAGE(struct nandsim *ns)
{
	return &(ns->pages[ns->regs.row]);
}

/*
 * Retuns a pointer to the current byte, within the current page.
 */
static inline u_char *NS_PAGE_BYTE_OFF(struct nandsim *ns)
{
	return NS_GET_PAGE(ns)->byte + ns->regs.column + ns->regs.off;
}

/*
 * Fill the NAND buffer with data read from the specified page.
 */
static void read_page(struct nandsim *ns, int num)
{
	union ns_mem *mypage;

	mypage = NS_GET_PAGE(ns);
	if (mypage->byte == NULL) {
		NS_DBG("read_page: page %d not allocated\n", ns->regs.row);
		memset(ns->buf.byte, 0xFF, num);
	} else {
		NS_DBG("read_page: page %d allocated, reading from %d\n",
			ns->regs.row, ns->regs.column + ns->regs.off);
		memcpy(ns->buf.byte, NS_PAGE_BYTE_OFF(ns), num);
	}
}

/*
 * Erase all pages in the specified sector.
 */
static void erase_sector(struct nandsim *ns)
{
	union ns_mem *mypage;
	int i;

	mypage = NS_GET_PAGE(ns);
	for (i = 0; i < ns->geom.pgsec; i++) {
		if (mypage->byte != NULL) {
			NS_DBG("erase_sector: freeing page %d\n", ns->regs.row+i);
			kfree(mypage->byte);
			mypage->byte = NULL;
		}
		mypage++;
	}
}

/*
 * Program the specified page with the contents from the NAND buffer.
 */
static int prog_page(struct nandsim *ns, int num)
{
	union ns_mem *mypage;
	u_char *pg_off;

	mypage = NS_GET_PAGE(ns);
	if (mypage->byte == NULL) {
		NS_DBG("prog_page: allocating page %d\n", ns->regs.row);
		mypage->byte = kmalloc(ns->geom.pgszoob, GFP_KERNEL);
		if (mypage->byte == NULL) {
			NS_ERR("prog_page: error allocating memory for page %d\n", ns->regs.row);
			return -1;
		}
		memset(mypage->byte, 0xFF, ns->geom.pgszoob);
	}

	pg_off = NS_PAGE_BYTE_OFF(ns);
	memcpy(pg_off, ns->buf.byte, num);

	return 0;
}

/*
 * If state has any action bit, perform this action.
 *
 * RETURNS: 0 if success, -1 if error.
 */
static int do_state_action(struct nandsim *ns, uint32_t action)
{
	int num;
	int busdiv = ns->busw == 8 ? 1 : 2;

	action &= ACTION_MASK;

	/* Check that page address input is correct */
	if (action != ACTION_SECERASE && ns->regs.row >= ns->geom.pgnum) {
		NS_WARN("do_state_action: wrong page number (%#x)\n", ns->regs.row);
		return -1;
	}

	switch (action) {

	case ACTION_CPY:
		/*
		 * Copy page data to the internal buffer.
		 */

		/* Column shouldn't be very large */
		if (ns->regs.column >= (ns->geom.pgszoob - ns->regs.off)) {
			NS_ERR("do_state_action: column number is too large\n");
			break;
		}
		num = ns->geom.pgszoob - ns->regs.off - ns->regs.column;
		read_page(ns, num);

		NS_DBG("do_state_action: (ACTION_CPY:) copy %d bytes to int buf, raw offset %d\n",
			num, NS_RAW_OFFSET(ns) + ns->regs.off);

		if (ns->regs.off == 0)
			NS_LOG("read page %d\n", ns->regs.row);
		else if (ns->regs.off < ns->geom.pgsz)
			NS_LOG("read page %d (second half)\n", ns->regs.row);
		else
			NS_LOG("read OOB of page %d\n", ns->regs.row);

		NS_UDELAY(access_delay);
		NS_UDELAY(input_cycle * ns->geom.pgsz / 1000 / busdiv);

		break;

	case ACTION_SECERASE:
		/*
		 * Erase sector.
		 */

		if (ns->lines.wp) {
			NS_ERR("do_state_action: device is write-protected, ignore sector erase\n");
			return -1;
		}

		if (ns->regs.row >= ns->geom.pgnum - ns->geom.pgsec
			|| (ns->regs.row & ~(ns->geom.secsz - 1))) {
			NS_ERR("do_state_action: wrong sector address (%#x)\n", ns->regs.row);
			return -1;
		}

		ns->regs.row = (ns->regs.row <<
				8 * (ns->geom.pgaddrbytes - ns->geom.secaddrbytes)) | ns->regs.column;
		ns->regs.column = 0;

		NS_DBG("do_state_action: erase sector at address %#x, off = %d\n",
				ns->regs.row, NS_RAW_OFFSET(ns));
		NS_LOG("erase sector %d\n", ns->regs.row >> (ns->geom.secshift - ns->geom.pgshift));

		erase_sector(ns);

		NS_MDELAY(erase_delay);

		break;

	case ACTION_PRGPAGE:
		/*
		 * Programm page - move internal buffer data to the page.
		 */

		if (ns->lines.wp) {
			NS_WARN("do_state_action: device is write-protected, programm\n");
			return -1;
		}

		num = ns->geom.pgszoob - ns->regs.off - ns->regs.column;
		if (num != ns->regs.count) {
			NS_ERR("do_state_action: too few bytes were input (%d instead of %d)\n",
					ns->regs.count, num);
			return -1;
		}

		if (prog_page(ns, num) == -1)
			return -1;

		NS_DBG("do_state_action: copy %d bytes from int buf to (%#x, %#x), raw off = %d\n",
			num, ns->regs.row, ns->regs.column, NS_RAW_OFFSET(ns) + ns->regs.off);
		NS_LOG("programm page %d\n", ns->regs.row);

		NS_UDELAY(programm_delay);
		NS_UDELAY(output_cycle * ns->geom.pgsz / 1000 / busdiv);

		break;

	case ACTION_ZEROOFF:
		NS_DBG("do_state_action: set internal offset to 0\n");
		ns->regs.off = 0;
		break;

	case ACTION_HALFOFF:
		if (!(ns->options & OPT_PAGE512_8BIT)) {
			NS_ERR("do_state_action: BUG! can't skip half of page for non-512"
				"byte page size 8x chips\n");
			return -1;
		}
		NS_DBG("do_state_action: set internal offset to %d\n", ns->geom.pgsz/2);
		ns->regs.off = ns->geom.pgsz/2;
		break;

	case ACTION_OOBOFF:
		NS_DBG("do_state_action: set internal offset to %d\n", ns->geom.pgsz);
		ns->regs.off = ns->geom.pgsz;
		break;

	default:
		NS_DBG("do_state_action: BUG! unknown action\n");
	}

	return 0;
}

/*
 * Switch simulator's state.
 */
static void switch_state(struct nandsim *ns)
{
	if (ns->op) {
		/*
		 * The current operation have already been identified.
		 * Just follow the states chain.
		 */

		ns->stateidx += 1;
		ns->state = ns->nxstate;
		ns->nxstate = ns->op[ns->stateidx + 1];

		NS_DBG("switch_state: operation is known, switch to the next state, "
			"state: %s, nxstate: %s\n",
			get_state_name(ns->state), get_state_name(ns->nxstate));

		/* See, whether we need to do some action */
		if ((ns->state & ACTION_MASK) && do_state_action(ns, ns->state) < 0) {
			switch_to_ready_state(ns, NS_STATUS_FAILED(ns));
			return;
		}

	} else {
		/*
		 * We don't yet know which operation we perform.
		 * Try to identify it.
		 */

		/*
		 *  The only event causing the switch_state function to
		 *  be called with yet unknown operation is new command.
		 */
		ns->state = get_state_by_command(ns->regs.command);

		NS_DBG("switch_state: operation is unknown, try to find it\n");

		if (find_operation(ns, 0) != 0)
			return;

		if ((ns->state & ACTION_MASK) && do_state_action(ns, ns->state) < 0) {
			switch_to_ready_state(ns, NS_STATUS_FAILED(ns));
			return;
		}
	}

	/* For 16x devices column means the page offset in words */
	if ((ns->nxstate & STATE_ADDR_MASK) && ns->busw == 16) {
		NS_DBG("switch_state: double the column number for 16x device\n");
		ns->regs.column <<= 1;
	}

	if (NS_STATE(ns->nxstate) == STATE_READY) {
		/*
		 * The current state is the last. Return to STATE_READY
		 */

		u_char status = NS_STATUS_OK(ns);

		/* In case of data states, see if all bytes were input/output */
		if ((ns->state & (STATE_DATAIN_MASK | STATE_DATAOUT_MASK))
			&& ns->regs.count != ns->regs.num) {
			NS_WARN("switch_state: not all bytes were processed, %d left\n",
					ns->regs.num - ns->regs.count);
			status = NS_STATUS_FAILED(ns);
		}

		NS_DBG("switch_state: operation complete, switch to STATE_READY state\n");

		switch_to_ready_state(ns, status);

		return;
	} else if (ns->nxstate & (STATE_DATAIN_MASK | STATE_DATAOUT_MASK)) {
		/*
		 * If the next state is data input/output, switch to it now
		 */

		ns->state      = ns->nxstate;
		ns->nxstate    = ns->op[++ns->stateidx + 1];
		ns->regs.num   = ns->regs.count = 0;

		NS_DBG("switch_state: the next state is data I/O, switch, "
			"state: %s, nxstate: %s\n",
			get_state_name(ns->state), get_state_name(ns->nxstate));

		/*
		 * Set the internal register to the count of bytes which
		 * are expected to be input or output
		 */
		switch (NS_STATE(ns->state)) {
			case STATE_DATAIN:
			case STATE_DATAOUT:
				ns->regs.num = ns->geom.pgszoob - ns->regs.off - ns->regs.column;
				break;

			case STATE_DATAOUT_ID:
				ns->regs.num = ns->geom.idbytes;
				break;

			case STATE_DATAOUT_STATUS:
			case STATE_DATAOUT_STATUS_M:
				ns->regs.count = ns->regs.num = 0;
				break;

			default:
				NS_ERR("switch_state: BUG! unknown data state\n");
		}

	} else if (ns->nxstate & STATE_ADDR_MASK) {
		/*
		 * If the next state is address input, set the internal
		 * register to the number of expected address bytes
		 */

		ns->regs.count = 0;

		switch (NS_STATE(ns->nxstate)) {
			case STATE_ADDR_PAGE:
				ns->regs.num = ns->geom.pgaddrbytes;

				break;
			case STATE_ADDR_SEC:
				ns->regs.num = ns->geom.secaddrbytes;
				break;

			case STATE_ADDR_ZERO:
				ns->regs.num = 1;
				break;

			default:
				NS_ERR("switch_state: BUG! unknown address state\n");
		}
	} else {
		/*
		 * Just reset internal counters.
		 */

		ns->regs.num = 0;
		ns->regs.count = 0;
	}
}

static u_char ns_nand_read_byte(struct mtd_info *mtd)
{
        struct nandsim *ns = (struct nandsim *)((struct nand_chip *)mtd->priv)->priv;
	u_char outb = 0x00;

	/* Sanity and correctness checks */
	if (!ns->lines.ce) {
		NS_ERR("read_byte: chip is disabled, return %#x\n", (uint)outb);
		return outb;
	}
	if (ns->lines.ale || ns->lines.cle) {
		NS_ERR("read_byte: ALE or CLE pin is high, return %#x\n", (uint)outb);
		return outb;
	}
	if (!(ns->state & STATE_DATAOUT_MASK)) {
		NS_WARN("read_byte: unexpected data output cycle, state is %s "
			"return %#x\n", get_state_name(ns->state), (uint)outb);
		return outb;
	}

	/* Status register may be read as many times as it is wanted */
	if (NS_STATE(ns->state) == STATE_DATAOUT_STATUS) {
		NS_DBG("read_byte: return %#x status\n", ns->regs.status);
		return ns->regs.status;
	}

	/* Check if there is any data in the internal buffer which may be read */
	if (ns->regs.count == ns->regs.num) {
		NS_WARN("read_byte: no more data to output, return %#x\n", (uint)outb);
		return outb;
	}

	switch (NS_STATE(ns->state)) {
		case STATE_DATAOUT:
			if (ns->busw == 8) {
				outb = ns->buf.byte[ns->regs.count];
				ns->regs.count += 1;
			} else {
				outb = (u_char)cpu_to_le16(ns->buf.word[ns->regs.count >> 1]);
				ns->regs.count += 2;
			}
			break;
		case STATE_DATAOUT_ID:
			NS_DBG("read_byte: read ID byte %d, total = %d\n", ns->regs.count, ns->regs.num);
			outb = ns->ids[ns->regs.count];
			ns->regs.count += 1;
			break;
		default:
			BUG();
	}

	if (ns->regs.count == ns->regs.num) {
		NS_DBG("read_byte: all bytes were read\n");

		/*
		 * The OPT_AUTOINCR allows to read next conseqitive pages without
		 * new read operation cycle.
		 */
		if ((ns->options & OPT_AUTOINCR) && NS_STATE(ns->state) == STATE_DATAOUT) {
			ns->regs.count = 0;
			if (ns->regs.row + 1 < ns->geom.pgnum)
				ns->regs.row += 1;
			NS_DBG("read_byte: switch to the next page (%#x)\n", ns->regs.row);
			do_state_action(ns, ACTION_CPY);
		}
		else if (NS_STATE(ns->nxstate) == STATE_READY)
			switch_state(ns);

	}

	return outb;
}

static void ns_nand_write_byte(struct mtd_info *mtd, u_char byte)
{
        struct nandsim *ns = (struct nandsim *)((struct nand_chip *)mtd->priv)->priv;

	/* Sanity and correctness checks */
	if (!ns->lines.ce) {
		NS_ERR("write_byte: chip is disabled, ignore write\n");
		return;
	}
	if (ns->lines.ale && ns->lines.cle) {
		NS_ERR("write_byte: ALE and CLE pins are high simultaneously, ignore write\n");
		return;
	}

	if (ns->lines.cle == 1) {
		/*
		 * The byte written is a command.
		 */

		if (byte == NAND_CMD_RESET) {
			NS_LOG("reset chip\n");
			switch_to_ready_state(ns, NS_STATUS_OK(ns));
			return;
		}

		/*
		 * Chip might still be in STATE_DATAOUT
		 * (if OPT_AUTOINCR feature is supported), STATE_DATAOUT_STATUS or
		 * STATE_DATAOUT_STATUS_M state. If so, switch state.
		 */
		if (NS_STATE(ns->state) == STATE_DATAOUT_STATUS
			|| NS_STATE(ns->state) == STATE_DATAOUT_STATUS_M
			|| ((ns->options & OPT_AUTOINCR) && NS_STATE(ns->state) == STATE_DATAOUT))
			switch_state(ns);

		/* Check if chip is expecting command */
		if (NS_STATE(ns->nxstate) != STATE_UNKNOWN && !(ns->nxstate & STATE_CMD_MASK)) {
			/*
			 * We are in situation when something else (not command)
			 * was expected but command was input. In this case ignore
			 * previous command(s)/state(s) and accept the last one.
			 */
			NS_WARN("write_byte: command (%#x) wasn't expected, expected state is %s, "
				"ignore previous states\n", (uint)byte, get_state_name(ns->nxstate));
			switch_to_ready_state(ns, NS_STATUS_FAILED(ns));
		}

		/* Check that the command byte is correct */
		if (check_command(byte)) {
			NS_ERR("write_byte: unknown command %#x\n", (uint)byte);
			return;
		}

		NS_DBG("command byte corresponding to %s state accepted\n",
			get_state_name(get_state_by_command(byte)));
		ns->regs.command = byte;
		switch_state(ns);

	} else if (ns->lines.ale == 1) {
		/*
		 * The byte written is an address.
		 */

		if (NS_STATE(ns->nxstate) == STATE_UNKNOWN) {

			NS_DBG("write_byte: operation isn't known yet, identify it\n");

			if (find_operation(ns, 1) < 0)
				return;

			if ((ns->state & ACTION_MASK) && do_state_action(ns, ns->state) < 0) {
				switch_to_ready_state(ns, NS_STATUS_FAILED(ns));
				return;
			}

			ns->regs.count = 0;
			switch (NS_STATE(ns->nxstate)) {
				case STATE_ADDR_PAGE:
					ns->regs.num = ns->geom.pgaddrbytes;
					break;
				case STATE_ADDR_SEC:
					ns->regs.num = ns->geom.secaddrbytes;
					break;
				case STATE_ADDR_ZERO:
					ns->regs.num = 1;
					break;
				default:
					BUG();
			}
		}

		/* Check that chip is expecting address */
		if (!(ns->nxstate & STATE_ADDR_MASK)) {
			NS_ERR("write_byte: address (%#x) isn't expected, expected state is %s, "
				"switch to STATE_READY\n", (uint)byte, get_state_name(ns->nxstate));
			switch_to_ready_state(ns, NS_STATUS_FAILED(ns));
			return;
		}

		/* Check if this is expected byte */
		if (ns->regs.count == ns->regs.num) {
			NS_ERR("write_byte: no more address bytes expected\n");
			switch_to_ready_state(ns, NS_STATUS_FAILED(ns));
			return;
		}

		accept_addr_byte(ns, byte);

		ns->regs.count += 1;

		NS_DBG("write_byte: address byte %#x was accepted (%d bytes input, %d expected)\n",
				(uint)byte, ns->regs.count, ns->regs.num);

		if (ns->regs.count == ns->regs.num) {
			NS_DBG("address (%#x, %#x) is accepted\n", ns->regs.row, ns->regs.column);
			switch_state(ns);
		}

	} else {
		/*
		 * The byte written is an input data.
		 */

		/* Check that chip is expecting data input */
		if (!(ns->state & STATE_DATAIN_MASK)) {
			NS_ERR("write_byte: data input (%#x) isn't expected, state is %s, "
				"switch to %s\n", (uint)byte,
				get_state_name(ns->state), get_state_name(STATE_READY));
			switch_to_ready_state(ns, NS_STATUS_FAILED(ns));
			return;
		}

		/* Check if this is expected byte */
		if (ns->regs.count == ns->regs.num) {
			NS_WARN("write_byte: %u input bytes has already been accepted, ignore write\n",
					ns->regs.num);
			return;
		}

		if (ns->busw == 8) {
			ns->buf.byte[ns->regs.count] = byte;
			ns->regs.count += 1;
		} else {
			ns->buf.word[ns->regs.count >> 1] = cpu_to_le16((uint16_t)byte);
			ns->regs.count += 2;
		}
	}

	return;
}

static void ns_hwcontrol(struct mtd_info *mtd, int cmd, unsigned int bitmask)
{
	struct nandsim *ns = ((struct nand_chip *)mtd->priv)->priv;

	ns->lines.cle = bitmask & NAND_CLE ? 1 : 0;
	ns->lines.ale = bitmask & NAND_ALE ? 1 : 0;
	ns->lines.ce = bitmask & NAND_NCE ? 1 : 0;

	if (cmd != NAND_CMD_NONE)
		ns_nand_write_byte(mtd, cmd);
}

static int ns_device_ready(struct mtd_info *mtd)
{
	NS_DBG("device_ready\n");
	return 1;
}

static uint16_t ns_nand_read_word(struct mtd_info *mtd)
{
	struct nand_chip *chip = (struct nand_chip *)mtd->priv;

	NS_DBG("read_word\n");

	return chip->read_byte(mtd) | (chip->read_byte(mtd) << 8);
}

static void ns_nand_write_buf(struct mtd_info *mtd, const u_char *buf, int len)
{
        struct nandsim *ns = (struct nandsim *)((struct nand_chip *)mtd->priv)->priv;

	/* Check that chip is expecting data input */
	if (!(ns->state & STATE_DATAIN_MASK)) {
		NS_ERR("write_buf: data input isn't expected, state is %s, "
			"switch to STATE_READY\n", get_state_name(ns->state));
		switch_to_ready_state(ns, NS_STATUS_FAILED(ns));
		return;
	}

	/* Check if these are expected bytes */
	if (ns->regs.count + len > ns->regs.num) {
		NS_ERR("write_buf: too many input bytes\n");
		switch_to_ready_state(ns, NS_STATUS_FAILED(ns));
		return;
	}

	memcpy(ns->buf.byte + ns->regs.count, buf, len);
	ns->regs.count += len;

	if (ns->regs.count == ns->regs.num) {
		NS_DBG("write_buf: %d bytes were written\n", ns->regs.count);
	}
}

static void ns_nand_read_buf(struct mtd_info *mtd, u_char *buf, int len)
{
        struct nandsim *ns = (struct nandsim *)((struct nand_chip *)mtd->priv)->priv;

	/* Sanity and correctness checks */
	if (!ns->lines.ce) {
		NS_ERR("read_buf: chip is disabled\n");
		return;
	}
	if (ns->lines.ale || ns->lines.cle) {
		NS_ERR("read_buf: ALE or CLE pin is high\n");
		return;
	}
	if (!(ns->state & STATE_DATAOUT_MASK)) {
		NS_WARN("read_buf: unexpected data output cycle, current state is %s\n",
			get_state_name(ns->state));
		return;
	}

	if (NS_STATE(ns->state) != STATE_DATAOUT) {
		int i;

		for (i = 0; i < len; i++)
			buf[i] = ((struct nand_chip *)mtd->priv)->read_byte(mtd);

		return;
	}

	/* Check if these are expected bytes */
	if (ns->regs.count + len > ns->regs.num) {
		NS_ERR("read_buf: too many bytes to read\n");
		switch_to_ready_state(ns, NS_STATUS_FAILED(ns));
		return;
	}

	memcpy(buf, ns->buf.byte + ns->regs.count, len);
	ns->regs.count += len;

	if (ns->regs.count == ns->regs.num) {
		if ((ns->options & OPT_AUTOINCR) && NS_STATE(ns->state) == STATE_DATAOUT) {
			ns->regs.count = 0;
			if (ns->regs.row + 1 < ns->geom.pgnum)
				ns->regs.row += 1;
			NS_DBG("read_buf: switch to the next page (%#x)\n", ns->regs.row);
			do_state_action(ns, ACTION_CPY);
		}
		else if (NS_STATE(ns->nxstate) == STATE_READY)
			switch_state(ns);
	}

	return;
}

static int ns_nand_verify_buf(struct mtd_info *mtd, const u_char *buf, int len)
{
	ns_nand_read_buf(mtd, (u_char *)&ns_verify_buf[0], len);

	if (!memcmp(buf, &ns_verify_buf[0], len)) {
		NS_DBG("verify_buf: the buffer is OK\n");
		return 0;
	} else {
		NS_DBG("verify_buf: the buffer is wrong\n");
		return -EFAULT;
	}
}

/*
 * Module initialization function
 */
static int __init ns_init_module(void)
{
	struct nand_chip *chip;
	struct nandsim *nand;
	int retval = -ENOMEM;

	if (bus_width != 8 && bus_width != 16) {
		NS_ERR("wrong bus width (%d), use only 8 or 16\n", bus_width);
		return -EINVAL;
	}

	/* Allocate and initialize mtd_info, nand_chip and nandsim structures */
	nsmtd = kmalloc(sizeof(struct mtd_info) + sizeof(struct nand_chip)
				+ sizeof(struct nandsim), GFP_KERNEL);
	if (!nsmtd) {
		NS_ERR("unable to allocate core structures.\n");
		return -ENOMEM;
	}
	memset(nsmtd, 0, sizeof(struct mtd_info) + sizeof(struct nand_chip) +
			sizeof(struct nandsim));
	chip        = (struct nand_chip *)(nsmtd + 1);
        nsmtd->priv = (void *)chip;
	nand        = (struct nandsim *)(chip + 1);
	chip->priv  = (void *)nand;

	/*
	 * Register simulator's callbacks.
	 */
	chip->cmd_ctrl	 = ns_hwcontrol;
	chip->read_byte  = ns_nand_read_byte;
	chip->dev_ready  = ns_device_ready;
	chip->write_buf  = ns_nand_write_buf;
	chip->read_buf   = ns_nand_read_buf;
	chip->verify_buf = ns_nand_verify_buf;
	chip->read_word  = ns_nand_read_word;
	chip->ecc.mode   = NAND_ECC_SOFT;
	chip->options   |= NAND_SKIP_BBTSCAN;

	/*
	 * Perform minimum nandsim structure initialization to handle
	 * the initial ID read command correctly
	 */
	if (third_id_byte != 0xFF || fourth_id_byte != 0xFF)
		nand->geom.idbytes = 4;
	else
		nand->geom.idbytes = 2;
	nand->regs.status = NS_STATUS_OK(nand);
	nand->nxstate = STATE_UNKNOWN;
	nand->options |= OPT_PAGE256; /* temporary value */
	nand->ids[0] = first_id_byte;
	nand->ids[1] = second_id_byte;
	nand->ids[2] = third_id_byte;
	nand->ids[3] = fourth_id_byte;
	if (bus_width == 16) {
		nand->busw = 16;
		chip->options |= NAND_BUSWIDTH_16;
	}

	nsmtd->owner = THIS_MODULE;

	if ((retval = nand_scan(nsmtd, 1)) != 0) {
		NS_ERR("can't register NAND Simulator\n");
		if (retval > 0)
			retval = -ENXIO;
		goto error;
	}

	if ((retval = init_nandsim(nsmtd)) != 0) {
		NS_ERR("scan_bbt: can't initialize the nandsim structure\n");
		goto error;
	}

	if ((retval = nand_default_bbt(nsmtd)) != 0) {
		free_nandsim(nand);
		goto error;
	}

	/* Register NAND as one big partition */
	add_mtd_partitions(nsmtd, &nand->part, 1);

        return 0;

error:
	kfree(nsmtd);

	return retval;
}

module_init(ns_init_module);

/*
 * Module clean-up function
 */
static void __exit ns_cleanup_module(void)
{
	struct nandsim *ns = (struct nandsim *)(((struct nand_chip *)nsmtd->priv)->priv);

	free_nandsim(ns);    /* Free nandsim private resources */
	nand_release(nsmtd); /* Unregisterd drived */
	kfree(nsmtd);        /* Free other structures */
}

module_exit(ns_cleanup_module);

MODULE_LICENSE ("GPL");
MODULE_AUTHOR ("Artem B. Bityuckiy");
MODULE_DESCRIPTION ("The NAND flash simulator");

