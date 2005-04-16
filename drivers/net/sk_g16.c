/*-
 * Copyright (C) 1994 by PJD Weichmann & SWS Bern, Switzerland
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 * Module         : sk_g16.c
 *
 * Version        : $Revision: 1.1 $
 *
 * Author         : Patrick J.D. Weichmann
 *
 * Date Created   : 94/05/26
 * Last Updated   : $Date: 1994/06/30 16:25:15 $
 *
 * Description    : Schneider & Koch G16 Ethernet Device Driver for
 *                  Linux Kernel >= 1.1.22
 * Update History :
 *                  Paul Gortmaker, 03/97: Fix for v2.1.x to use read{b,w}
 *                  write{b,w} and memcpy -> memcpy_{to,from}io
 *
 *		    Jeff Garzik, 06/2000, Modularize
 *
-*/

static const char rcsid[] = "$Id: sk_g16.c,v 1.1 1994/06/30 16:25:15 root Exp $";

/*
 * The Schneider & Koch (SK) G16 Network device driver is based
 * on the 'ni6510' driver from Michael Hipp which can be found at
 * ftp://sunsite.unc.edu/pub/Linux/system/Network/drivers/nidrivers.tar.gz
 * 
 * Sources: 1) ni6510.c by M. Hipp
 *          2) depca.c  by D.C. Davies
 *          3) skeleton.c by D. Becker
 *          4) Am7990 Local Area Network Controller for Ethernet (LANCE),
 *             AMD, Pub. #05698, June 1989
 *
 * Many Thanks for helping me to get things working to: 
 *                 
 *                 A. Cox (A.Cox@swansea.ac.uk)
 *                 M. Hipp (mhipp@student.uni-tuebingen.de)
 *                 R. Bolz (Schneider & Koch, Germany)
 *
 * To Do: 
 *        - Support of SK_G8 and other SK Network Cards.
 *        - Autoset memory mapped RAM. Check for free memory and then
 *          configure RAM correctly. 
 *        - SK_close should really set card in to initial state.
 *        - Test if IRQ 3 is not switched off. Use autoirq() functionality.
 *          (as in /drivers/net/skeleton.c)
 *        - Implement Multicast addressing. At minimum something like
 *          in depca.c. 
 *        - Redo the statistics part.
 *        - Try to find out if the board is in 8 Bit or 16 Bit slot.
 *          If in 8 Bit mode don't use IRQ 11.
 *        - (Try to make it slightly faster.) 
 *	  - Power management support
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fcntl.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/string.h> 
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/bitops.h>

#include <asm/system.h>
#include <asm/io.h>

#include "sk_g16.h"

/* 
 * Schneider & Koch Card Definitions 
 * =================================
 */  

#define SK_NAME   "SK_G16"

/*
 * SK_G16 Configuration
 * --------------------
 */ 

/* 
 * Abbreviations
 * -------------
 *  
 * RAM - used for the 16KB shared memory 
 * Boot_ROM, ROM - are used for referencing the BootEPROM
 *
 * SK_BOOT_ROM and SK_ADDR are symbolic constants used to configure
 * the behaviour of the driver and the SK_G16.
 *
 * ! See sk_g16.install on how to install and configure the driver !   
 *
 * SK_BOOT_ROM defines if the Boot_ROM should be switched off or not.
 *
 * SK_ADDR defines the address where the RAM will be mapped into the real
 *         host memory.
 *         valid addresses are from 0xa0000 to 0xfc000 in 16Kbyte steps.
 */  
 
#define SK_BOOT_ROM     1              /* 1=BootROM on 0=off */

#define SK_ADDR         0xcc000

/* 
 * In POS3 are bits A14-A19 of the address bus. These bits can be set
 * to choose the RAM address. That's why we only can choose the RAM address
 * in 16KB steps.
 */

#define POS_ADDR       (rom_addr>>14)  /* Do not change this line */

/* 
 * SK_G16 I/O PORT's + IRQ's + Boot_ROM locations
 * ----------------------------------------------
 */

/* 
 * As nearly every card has also SK_G16 a specified I/O Port region and
 * only a few possible IRQ's.
 * In the Installation Guide from Schneider & Koch is listed a possible
 * Interrupt IRQ2. IRQ2 is always IRQ9 in boards with two cascaded interrupt
 * controllers. So we use in SK_IRQS IRQ9.
 */

/* Don't touch any of the following #defines. */

#define SK_IO_PORTS     { 0x100, 0x180, 0x208, 0x220, 0x288, 0x320, 0x328, 0x390, 0 }

#define SK_IRQS         { 3, 5, 9, 11, 0 }

#define SK_BOOT_ROM_LOCATIONS { 0xc0000, 0xc4000, 0xc8000, 0xcc000, 0xd0000, 0xd4000, 0xd8000, 0xdc000, 0 }

#define SK_BOOT_ROM_ID  { 0x55, 0xaa, 0x10, 0x50, 0x06, 0x33 }

/* 
 * SK_G16 POS REGISTERS 
 * --------------------
 */

/*
 * SK_G16 has a Programmable Option Select (POS) Register.
 * The POS is composed of 8 separate registers (POS0-7) which 
 * are I/O mapped on an address set by the W1 switch.                    
 *
 */

#define SK_POS_SIZE 8           /* 8 I/O Ports are used by SK_G16 */

#define SK_POS0     ioaddr      /* Card-ID Low (R) */
#define SK_POS1     ioaddr+1    /* Card-ID High (R) */
#define SK_POS2     ioaddr+2    /* Card-Enable, Boot-ROM Disable (RW) */
#define SK_POS3     ioaddr+3    /* Base address of RAM */
#define SK_POS4     ioaddr+4    /* IRQ */

/* POS5 - POS7 are unused */

/* 
 * SK_G16 MAC PREFIX 
 * -----------------
 */

/* 
 * Scheider & Koch manufacturer code (00:00:a5).
 * This must be checked, that we are sure it is a SK card.
 */

#define SK_MAC0         0x00
#define SK_MAC1         0x00
#define SK_MAC2         0x5a

/* 
 * SK_G16 ID 
 * ---------
 */ 

/* 
 * If POS0,POS1 contain the following ID, then we know
 * at which I/O Port Address we are. 
 */

#define SK_IDLOW  0xfd 
#define SK_IDHIGH 0x6a


/* 
 * LANCE POS Bit definitions 
 * -------------------------
 */

#define SK_ROM_RAM_ON  (POS2_CARD)
#define SK_ROM_RAM_OFF (POS2_EPROM)
#define SK_ROM_ON      (inb(SK_POS2) & POS2_CARD)
#define SK_ROM_OFF     (inb(SK_POS2) | POS2_EPROM)
#define SK_RAM_ON      (inb(SK_POS2) | POS2_CARD)
#define SK_RAM_OFF     (inb(SK_POS2) & POS2_EPROM) 

#define POS2_CARD  0x0001              /* 1 = SK_G16 on      0 = off */
#define POS2_EPROM 0x0002              /* 1 = Boot EPROM off 0 = on */ 

/* 
 * SK_G16 Memory mapped Registers
 * ------------------------------
 *
 */ 

#define SK_IOREG        (&board->ioreg) /* LANCE data registers.     */ 
#define SK_PORT         (&board->port)  /* Control, Status register  */
#define SK_IOCOM        (&board->iocom) /* I/O Command               */

/* 
 * SK_G16 Status/Control Register bits
 * -----------------------------------
 *
 * (C) Controlreg (S) Statusreg 
 */

/* 
 * Register transfer: 0 = no transfer
 *                    1 = transferring data between LANCE and I/O reg 
 */
#define SK_IORUN        0x20   

/* 
 * LANCE interrupt: 0 = LANCE interrupt occurred	
 *                  1 = no LANCE interrupt occurred
 */
#define SK_IRQ          0x10   
			
#define SK_RESET        0x08   /* Reset SK_CARD: 0 = RESET 1 = normal */
#define SK_RW           0x02   /* 0 = write to 1 = read from */
#define SK_ADR          0x01   /* 0 = REG DataPort 1 = RAP Reg addr port */

  
#define SK_RREG         SK_RW  /* Transferdirection to read from lance */
#define SK_WREG         0      /* Transferdirection to write to lance */
#define SK_RAP          SK_ADR /* Destination Register RAP */
#define SK_RDATA        0      /* Destination Register REG DataPort */

/* 
 * SK_G16 I/O Command 
 * ------------------
 */

/* 
 * Any bitcombination sets the internal I/O bit (transfer will start) 
 * when written to I/O Command
 */

#define SK_DOIO         0x80   /* Do Transfer */ 
 
/* 
 * LANCE RAP (Register Address Port). 
 * ---------------------------------
 */

/*   
 * The LANCE internal registers are selected through the RAP. 
 * The Registers are:
 *
 * CSR0 - Status and Control flags 
 * CSR1 - Low order bits of initialize block (bits 15:00)
 * CSR2 - High order bits of initialize block (bits 07:00, 15:08 are reserved)
 * CSR3 - Allows redefinition of the Bus Master Interface.
 *        This register must be set to 0x0002, which means BSWAP = 0,
 *        ACON = 1, BCON = 0;
 *
 */
 
#define CSR0            0x00   
#define CSR1            0x01  
#define CSR2            0x02 
#define CSR3            0x03

/* 
 * General Definitions 
 * ===================
 */

/* 
 * Set the number of Tx and Rx buffers, using Log_2(# buffers).
 * We have 16KB RAM which can be accessed by the LANCE. In the 
 * memory are not only the buffers but also the ring descriptors and
 * the initialize block. 
 * Don't change anything unless you really know what you do.
 */

#define LC_LOG_TX_BUFFERS 1               /* (2 == 2^^1) 2 Transmit buffers */
#define LC_LOG_RX_BUFFERS 3               /* (8 == 2^^3) 8 Receive buffers */

/* Descriptor ring sizes */

#define TMDNUM (1 << (LC_LOG_TX_BUFFERS)) /* 2 Transmit descriptor rings */
#define RMDNUM (1 << (LC_LOG_RX_BUFFERS)) /* 8 Receive Buffers */

/* Define Mask for setting RMD, TMD length in the LANCE init_block */

#define TMDNUMMASK (LC_LOG_TX_BUFFERS << 29)
#define RMDNUMMASK (LC_LOG_RX_BUFFERS << 29)

/*
 * Data Buffer size is set to maximum packet length.
 */

#define PKT_BUF_SZ              1518 

/* 
 * The number of low I/O ports used by the ethercard. 
 */

#define ETHERCARD_TOTAL_SIZE    SK_POS_SIZE

/* 
 * SK_DEBUG
 *
 * Here you can choose what level of debugging wanted.
 *
 * If SK_DEBUG and SK_DEBUG2 are undefined, then only the
 *  necessary messages will be printed.
 *
 * If SK_DEBUG is defined, there will be many debugging prints
 *  which can help to find some mistakes in configuration or even
 *  in the driver code.
 *
 * If SK_DEBUG2 is defined, many many messages will be printed 
 *  which normally you don't need. I used this to check the interrupt
 *  routine. 
 *
 * (If you define only SK_DEBUG2 then only the messages for 
 *  checking interrupts will be printed!)
 *
 * Normal way of live is: 
 *
 * For the whole thing get going let both symbolic constants
 * undefined. If you face any problems and you know what's going
 * on (you know something about the card and you can interpret some
 * hex LANCE register output) then define SK_DEBUG
 * 
 */

#undef  SK_DEBUG	/* debugging */
#undef  SK_DEBUG2	/* debugging with more verbose report */

#ifdef SK_DEBUG
#define PRINTK(x) printk x
#else
#define PRINTK(x) /**/
#endif

#ifdef SK_DEBUG2
#define PRINTK2(x) printk x
#else
#define PRINTK2(x) /**/
#endif

/* 
 * SK_G16 RAM
 *
 * The components are memory mapped and can be set in a region from
 * 0x00000 through 0xfc000 in 16KB steps. 
 *
 * The Network components are: dual ported RAM, Prom, I/O Reg, Status-,
 * Controlregister and I/O Command.
 *
 * dual ported RAM: This is the only memory region which the LANCE chip
 *      has access to. From the Lance it is addressed from 0x0000 to
 *      0x3fbf. The host accesses it normally.
 *
 * PROM: The PROM obtains the ETHERNET-MAC-Address. It is realised as a
 *       8-Bit PROM, this means only the 16 even addresses are used of the
 *       32 Byte Address region. Access to an odd address results in invalid
 *       data.
 * 
 * LANCE I/O Reg: The I/O Reg is build of 4 single Registers, Low-Byte Write,
 *       Hi-Byte Write, Low-Byte Read, Hi-Byte Read.
 *       Transfer from or to the LANCE is always in 16Bit so Low and High
 *       registers are always relevant.
 *
 *       The Data from the Readregister is not the data in the Writeregister!!
 *       
 * Port: Status- and Controlregister. 
 *       Two different registers which share the same address, Status is 
 *       read-only, Control is write-only.
 *    
 * I/O Command: 
 *       Any bitcombination written in here starts the transmission between
 *       Host and LANCE.
 */

typedef struct
{
	unsigned char  ram[0x3fc0];   /* 16KB dual ported ram */
	unsigned char  rom[0x0020];   /* 32Byte PROM containing 6Byte MAC */
	unsigned char  res1[0x0010];  /* reserved */
	unsigned volatile short ioreg;/* LANCE I/O Register */
	unsigned volatile char  port; /* Statusregister and Controlregister */
	unsigned char  iocom;         /* I/O Command Register */
} SK_RAM;

/* struct  */

/* 
 * This is the structure for the dual ported ram. We
 * have exactly 16 320 Bytes. In here there must be:
 *
 *     - Initialize Block   (starting at a word boundary)
 *     - Receive and Transmit Descriptor Rings (quadword boundary)
 *     - Data Buffers (arbitrary boundary)
 *
 * This is because LANCE has on SK_G16 only access to the dual ported
 * RAM and nowhere else.
 */

struct SK_ram
{
    struct init_block ib;
    struct tmd tmde[TMDNUM];
    struct rmd rmde[RMDNUM];
    char tmdbuf[TMDNUM][PKT_BUF_SZ];
    char rmdbuf[RMDNUM][PKT_BUF_SZ];
};

/* 
 * Structure where all necessary information is for ring buffer 
 * management and statistics.
 */

struct priv
{
    struct SK_ram *ram;  /* dual ported ram structure */
    struct rmd *rmdhead; /* start of receive ring descriptors */
    struct tmd *tmdhead; /* start of transmit ring descriptors */
    int        rmdnum;   /* actual used ring descriptor */
    int        tmdnum;   /* actual transmit descriptor for transmitting data */
    int        tmdlast;  /* last sent descriptor used for error handling, etc */
    void       *rmdbufs[RMDNUM]; /* pointer to the receive buffers */
    void       *tmdbufs[TMDNUM]; /* pointer to the transmit buffers */
    struct net_device_stats stats; /* Device driver statistics */
};

/* global variable declaration */

/* IRQ map used to reserve a IRQ (see SK_open()) */

/* static variables */

static SK_RAM *board;  /* pointer to our memory mapped board components */
static DEFINE_SPINLOCK(SK_lock);

/* Macros */


/* Function Prototypes */

/*
 * Device Driver functions
 * -----------------------
 * See for short explanation of each function its definitions header.
 */

static int   SK_probe(struct net_device *dev, short ioaddr);

static void  SK_timeout(struct net_device *dev);
static int   SK_open(struct net_device *dev);
static int   SK_send_packet(struct sk_buff *skb, struct net_device *dev);
static irqreturn_t SK_interrupt(int irq, void *dev_id, struct pt_regs * regs);
static void  SK_rxintr(struct net_device *dev);
static void  SK_txintr(struct net_device *dev);
static int   SK_close(struct net_device *dev);

static struct net_device_stats *SK_get_stats(struct net_device *dev);

unsigned int SK_rom_addr(void);

static void set_multicast_list(struct net_device *dev);

/*
 * LANCE Functions
 * ---------------
 */

static int SK_lance_init(struct net_device *dev, unsigned short mode);
void   SK_reset_board(void);
void   SK_set_RAP(int reg_number);
int    SK_read_reg(int reg_number);
int    SK_rread_reg(void);
void   SK_write_reg(int reg_number, int value);

/* 
 * Debugging functions
 * -------------------
 */

void SK_print_pos(struct net_device *dev, char *text);
void SK_print_dev(struct net_device *dev, char *text);
void SK_print_ram(struct net_device *dev);


/*-
 * Function       : SK_init
 * Author         : Patrick J.D. Weichmann
 * Date Created   : 94/05/26
 *
 * Description    : Check for a SK_G16 network adaptor and initialize it.
 *                  This function gets called by dev_init which initializes
 *                  all Network devices.
 *
 * Parameters     : I : struct net_device *dev - structure preconfigured 
 *                                           from Space.c
 * Return Value   : 0 = Driver Found and initialized 
 * Errors         : ENODEV - no device found
 *                  ENXIO  - not probed
 * Globals        : None
 * Update History :
 *     YY/MM/DD  uid  Description
-*/

static int io;	/* 0 == probe */

/* 
 * Check for a network adaptor of this type, and return '0' if one exists.
 * If dev->base_addr == 0, probe all likely locations.
 * If dev->base_addr == 1, always return failure.
 */

struct net_device * __init SK_init(int unit)
{
	int *port, ports[] = SK_IO_PORTS;  /* SK_G16 supported ports */
	static unsigned version_printed;
	struct net_device *dev = alloc_etherdev(sizeof(struct priv));
	int err = -ENODEV;

	if (!dev)
		return ERR_PTR(-ENOMEM);

	if (unit >= 0) {
		sprintf(dev->name, "eth%d", unit);
		netdev_boot_setup_check(dev);
		io = dev->base_addr;
	}

	if (version_printed++ == 0)
	        PRINTK(("%s: %s", SK_NAME, rcsid));

	if (io > 0xff) {        /* Check a single specified address */
		err = -EBUSY;
		/* Check if on specified address is a SK_G16 */
		if (request_region(io, ETHERCARD_TOTAL_SIZE, "sk_g16")) {
			err = SK_probe(dev, io);
			if (!err)
				goto got_it;
			release_region(io, ETHERCARD_TOTAL_SIZE);
		}
	} else if (io > 0) {       /* Don't probe at all */
		err = -ENXIO;
	} else {
		/* Autoprobe base_addr */
		for (port = &ports[0]; *port; port++) {
			io = *port;

			/* Check if I/O Port region is used by another board */
			if (!request_region(io, ETHERCARD_TOTAL_SIZE, "sk_g16"))
				continue;       /* Try next Port address */

			/* Check if at ioaddr is a SK_G16 */
			if (SK_probe(dev, io) == 0)
				goto got_it;

			release_region(io, ETHERCARD_TOTAL_SIZE);
		}
	}
err_out:
	free_netdev(dev);
	return ERR_PTR(err);

got_it:
	err = register_netdev(dev);
	if (err) {
		release_region(dev->base_addr, ETHERCARD_TOTAL_SIZE);
		goto err_out;
	}
	return dev;

} /* End of SK_init */


MODULE_AUTHOR("Patrick J.D. Weichmann");
MODULE_DESCRIPTION("Schneider & Koch G16 Ethernet Device Driver");
MODULE_LICENSE("GPL");
MODULE_PARM(io, "i");
MODULE_PARM_DESC(io, "0 to probe common ports (unsafe), or the I/O base of the board");


#ifdef MODULE

static struct net_device *SK_dev;

static int __init SK_init_module (void)
{
 	SK_dev = SK_init(-1);
 	return IS_ERR(SK_dev) ? PTR_ERR(SK_dev) : 0;
}

static void __exit SK_cleanup_module (void)
{
 	unregister_netdev(SK_dev);
 	release_region(SK_dev->base_addr, ETHERCARD_TOTAL_SIZE);
 	free_netdev(SK_dev);
}

module_init(SK_init_module);
module_exit(SK_cleanup_module);
#endif


/*-
 * Function       : SK_probe
 * Author         : Patrick J.D. Weichmann
 * Date Created   : 94/05/26
 *
 * Description    : This function is called by SK_init and 
 *                  does the main part of initialization.
 *                  
 * Parameters     : I : struct net_device *dev - SK_G16 device structure
 *                  I : short ioaddr       - I/O Port address where POS is.
 * Return Value   : 0 = Initialization done             
 * Errors         : ENODEV - No SK_G16 found
 *                  -1     - Configuration problem
 * Globals        : board       - pointer to SK_RAM
 * Update History :
 *     YY/MM/DD  uid  Description
 *     94/06/30  pwe  SK_ADDR now checked and at the correct place
-*/

int __init SK_probe(struct net_device *dev, short ioaddr)
{
    int i,j;                /* Counters */
    int sk_addr_flag = 0;   /* SK ADDR correct? 1 - no, 0 - yes */
    unsigned int rom_addr;  /* used to store RAM address used for POS_ADDR */

    struct priv *p = netdev_priv(dev);	/* SK_G16 private structure */

    if (inb(SK_POS0) != SK_IDLOW || inb(SK_POS1) != SK_IDHIGH)
	return -ENODEV;
    dev->base_addr = ioaddr;

    if (SK_ADDR & 0x3fff || SK_ADDR < 0xa0000)
    {
      
       sk_addr_flag = 1;

       /* 
        * Now here we could use a routine which searches for a free
        * place in the ram and set SK_ADDR if found. TODO. 
        */
    }

    if (SK_BOOT_ROM)            /* Shall we keep Boot_ROM on ? */
    {
        PRINTK(("## %s: SK_BOOT_ROM is set.\n", SK_NAME));

        rom_addr = SK_rom_addr();

	if (rom_addr == 0)      /* No Boot_ROM found */
	{
            if (sk_addr_flag)   /* No or Invalid SK_ADDR is defined */ 
            {
                printk("%s: SK_ADDR %#08x is not valid. Check configuration.\n",
                       dev->name, SK_ADDR);
                return -1;
            }

	    rom_addr = SK_ADDR; /* assign predefined address */

	    PRINTK(("## %s: NO Bootrom found \n", SK_NAME));

	    outb(SK_ROM_RAM_OFF, SK_POS2); /* Boot_ROM + RAM off */
	    outb(POS_ADDR, SK_POS3);       /* Set RAM address */
	    outb(SK_RAM_ON, SK_POS2);      /* enable RAM */
	}
	else if (rom_addr == SK_ADDR) 
	{
            printk("%s: RAM + ROM are set to the same address %#08x\n"
                   "   Check configuration. Now switching off Boot_ROM\n",
                   SK_NAME, rom_addr);

	    outb(SK_ROM_RAM_OFF, SK_POS2); /* Boot_ROM + RAM off*/
	    outb(POS_ADDR, SK_POS3);       /* Set RAM address */
	    outb(SK_RAM_ON, SK_POS2);      /* enable RAM */
	}
	else
	{
            PRINTK(("## %s: Found ROM at %#08x\n", SK_NAME, rom_addr));
	    PRINTK(("## %s: Keeping Boot_ROM on\n", SK_NAME));

            if (sk_addr_flag)       /* No or Invalid SK_ADDR is defined */ 
            {
                printk("%s: SK_ADDR %#08x is not valid. Check configuration.\n",
                       dev->name, SK_ADDR);
                return -1;
            }

	    rom_addr = SK_ADDR;

	    outb(SK_ROM_RAM_OFF, SK_POS2); /* Boot_ROM + RAM off */ 
	    outb(POS_ADDR, SK_POS3);       /* Set RAM address */
	    outb(SK_ROM_RAM_ON, SK_POS2);  /* RAM on, BOOT_ROM on */
	}
    }
    else /* Don't keep Boot_ROM */
    {
        PRINTK(("## %s: SK_BOOT_ROM is not set.\n", SK_NAME));

        if (sk_addr_flag)           /* No or Invalid SK_ADDR is defined */ 
        {
            printk("%s: SK_ADDR %#08x is not valid. Check configuration.\n",
                   dev->name, SK_ADDR);
            return -1;
        }

	rom_addr = SK_rom_addr();          /* Try to find a Boot_ROM */

	/* IF we find a Boot_ROM disable it */

	outb(SK_ROM_RAM_OFF, SK_POS2);     /* Boot_ROM + RAM off */  

        /* We found a Boot_ROM and it's gone. Set RAM address on
         * Boot_ROM address. 
         */ 

	if (rom_addr) 
	{
            printk("%s: We found Boot_ROM at %#08x. Now setting RAM on"
                   "that address\n", SK_NAME, rom_addr);

	    outb(POS_ADDR, SK_POS3);       /* Set RAM on Boot_ROM address */
	}
	else /* We did not find a Boot_ROM, use predefined SK_ADDR for ram */
	{
            if (sk_addr_flag)       /* No or Invalid SK_ADDR is defined */ 
            {
                printk("%s: SK_ADDR %#08x is not valid. Check configuration.\n",
                       dev->name, SK_ADDR);
                return -1;
            }

	    rom_addr = SK_ADDR;

	    outb(POS_ADDR, SK_POS3);       /* Set RAM address */ 
	}
	outb(SK_RAM_ON, SK_POS2);          /* enable RAM */
    }

#ifdef SK_DEBUG
    SK_print_pos(dev, "POS registers after ROM, RAM config");
#endif

    board = (SK_RAM *) isa_bus_to_virt(rom_addr);

    /* Read in station address */
    for (i = 0, j = 0; i < ETH_ALEN; i++, j+=2)
    {
	dev->dev_addr[i] = readb(board->rom+j);          
    }

    /* Check for manufacturer code */
    if (!(dev->dev_addr[0] == SK_MAC0 &&
	  dev->dev_addr[1] == SK_MAC1 &&
	  dev->dev_addr[2] == SK_MAC2) )
    {
        PRINTK(("## %s: We did not find SK_G16 at RAM location.\n",
                SK_NAME)); 
	return -ENODEV;                     /* NO SK_G16 found */
    }

    printk("%s: %s found at %#3x, HW addr: %#04x:%02x:%02x:%02x:%02x:%02x\n",
	    dev->name,
	    "Schneider & Koch Netcard",
	    (unsigned int) dev->base_addr,
	    dev->dev_addr[0],
	    dev->dev_addr[1],
	    dev->dev_addr[2],
	    dev->dev_addr[3],
	    dev->dev_addr[4],
	    dev->dev_addr[5]);

    memset((char *) dev->priv, 0, sizeof(struct priv)); /* clear memory */

    /* Assign our Device Driver functions */

    dev->open                   = SK_open;
    dev->stop                   = SK_close;
    dev->hard_start_xmit        = SK_send_packet;
    dev->get_stats              = SK_get_stats;
    dev->set_multicast_list     = set_multicast_list;
    dev->tx_timeout		= SK_timeout;
    dev->watchdog_timeo		= HZ/7;


    dev->flags &= ~IFF_MULTICAST;

    /* Initialize private structure */

    p->ram = (struct SK_ram *) rom_addr; /* Set dual ported RAM addr */
    p->tmdhead = &(p->ram)->tmde[0];     /* Set TMD head */
    p->rmdhead = &(p->ram)->rmde[0];     /* Set RMD head */

    /* Initialize buffer pointers */

    for (i = 0; i < TMDNUM; i++)
    {
	p->tmdbufs[i] = &(p->ram)->tmdbuf[i];
    }

    for (i = 0; i < RMDNUM; i++)
    {
	p->rmdbufs[i] = &(p->ram)->rmdbuf[i]; 
    }

#ifdef SK_DEBUG
    SK_print_pos(dev, "End of SK_probe");
    SK_print_ram(dev);
#endif 
    return 0;                            /* Initialization done */
} /* End of SK_probe() */


/*- 
 * Function       : SK_open
 * Author         : Patrick J.D. Weichmann
 * Date Created   : 94/05/26
 *
 * Description    : This function is called sometimes after booting 
 *                  when ifconfig program is run.
 *
 *                  This function requests an IRQ, sets the correct
 *                  IRQ in the card. Then calls SK_lance_init() to 
 *                  init and start the LANCE chip. Then if everything is 
 *                  ok returns with 0 (OK), which means SK_G16 is now
 *                  opened and operational.
 *
 *                  (Called by dev_open() /net/inet/dev.c)
 *
 * Parameters     : I : struct net_device *dev - SK_G16 device structure
 * Return Value   : 0 - Device opened
 * Errors         : -EAGAIN - Open failed
 * Side Effects   : None
 * Update History :
 *     YY/MM/DD  uid  Description
-*/

static int SK_open(struct net_device *dev)
{
    int i = 0;
    int irqval = 0;
    int ioaddr = dev->base_addr;

    int irqtab[] = SK_IRQS; 

    struct priv *p = netdev_priv(dev);

    PRINTK(("## %s: At beginning of SK_open(). CSR0: %#06x\n", 
           SK_NAME, SK_read_reg(CSR0)));

    if (dev->irq == 0) /* Autoirq */
    {
	i = 0;

	/* 
         * Check if one IRQ out of SK_IRQS is free and install 
	 * interrupt handler.
	 * Most done by request_irq(). 
	 * irqval: 0       - interrupt handler installed for IRQ irqtab[i]
	 *         -EBUSY  - interrupt busy 
         *         -EINVAL - irq > 15 or handler = NULL
	 */

	do
	{
	  irqval = request_irq(irqtab[i], &SK_interrupt, 0, "sk_g16", dev);
	  i++;
	} while (irqval && irqtab[i]);

	if (irqval) /* We tried every possible IRQ but no success */
	{
	    printk("%s: unable to get an IRQ\n", dev->name);
	    return -EAGAIN;
	}

	dev->irq = irqtab[--i]; 
	
	outb(i<<2, SK_POS4);           /* Set Card on probed IRQ */

    }
    else if (dev->irq == 2) /* IRQ2 is always IRQ9 */
    {
	if (request_irq(9, &SK_interrupt, 0, "sk_g16", dev))
	{
	    printk("%s: unable to get IRQ 9\n", dev->name);
	    return -EAGAIN;
	} 
	dev->irq = 9;
	
        /* 
         * Now we set card on IRQ2.
         * This can be confusing, but remember that IRQ2 on the network
         * card is in reality IRQ9
         */
	outb(0x08, SK_POS4);           /* set card to IRQ2 */

    }
    else  /* Check IRQ as defined in Space.c */
    {
	int i = 0;

	/* check if IRQ free and valid. Then install Interrupt handler */

	if (request_irq(dev->irq, &SK_interrupt, 0, "sk_g16", dev))
	{
	    printk("%s: unable to get selected IRQ\n", dev->name);
	    return -EAGAIN;
	}

	switch(dev->irq)
	{
	    case 3: i = 0;
		    break;
	    case 5: i = 1;
		    break;
	    case 2: i = 2;
		    break;
	    case 11:i = 3;
		    break;
	    default: 
		printk("%s: Preselected IRQ %d is invalid for %s boards",
		       dev->name,
		       dev->irq,
                       SK_NAME);
		return -EAGAIN;
	}      
  
	outb(i<<2, SK_POS4);           /* Set IRQ on card */
    }

    printk("%s: Schneider & Koch G16 at %#3x, IRQ %d, shared mem at %#08x\n",
	    dev->name, (unsigned int)dev->base_addr, 
	    (int) dev->irq, (unsigned int) p->ram);

    if (!(i = SK_lance_init(dev, 0)))  /* LANCE init OK? */
    {
	netif_start_queue(dev);

#ifdef SK_DEBUG

        /* 
         * This debug block tries to stop LANCE,
         * reinit LANCE with transmitter and receiver disabled,
         * then stop again and reinit with NORMAL_MODE
         */

        printk("## %s: After lance init. CSR0: %#06x\n", 
               SK_NAME, SK_read_reg(CSR0));
        SK_write_reg(CSR0, CSR0_STOP);
        printk("## %s: LANCE stopped. CSR0: %#06x\n", 
               SK_NAME, SK_read_reg(CSR0));
        SK_lance_init(dev, MODE_DTX | MODE_DRX);
        printk("## %s: Reinit with DTX + DRX off. CSR0: %#06x\n", 
               SK_NAME, SK_read_reg(CSR0));
        SK_write_reg(CSR0, CSR0_STOP);
        printk("## %s: LANCE stopped. CSR0: %#06x\n", 
               SK_NAME, SK_read_reg(CSR0));
        SK_lance_init(dev, MODE_NORMAL);
        printk("## %s: LANCE back to normal mode. CSR0: %#06x\n", 
               SK_NAME, SK_read_reg(CSR0));
        SK_print_pos(dev, "POS regs before returning OK");

#endif /* SK_DEBUG */
       
	return 0;              /* SK_open() is successful */
    }
    else /* LANCE init failed */
    {

	PRINTK(("## %s: LANCE init failed: CSR0: %#06x\n", 
               SK_NAME, SK_read_reg(CSR0)));

	return -EAGAIN;
    }

} /* End of SK_open() */


/*-
 * Function       : SK_lance_init
 * Author         : Patrick J.D. Weichmann
 * Date Created   : 94/05/26
 *
 * Description    : Reset LANCE chip, fill RMD, TMD structures with
 *                  start values and Start LANCE.
 *
 * Parameters     : I : struct net_device *dev - SK_G16 device structure
 *                  I : int mode - put LANCE into "mode" see data-sheet for
 *                                 more info.
 * Return Value   : 0  - Init done
 * Errors         : -1 - Init failed
 * Update History :
 *     YY/MM/DD  uid  Description
-*/

static int SK_lance_init(struct net_device *dev, unsigned short mode)
{
    int i;
    unsigned long flags;
    struct priv *p = netdev_priv(dev);
    struct tmd  *tmdp;
    struct rmd  *rmdp;

    PRINTK(("## %s: At beginning of LANCE init. CSR0: %#06x\n", 
           SK_NAME, SK_read_reg(CSR0)));

    /* Reset LANCE */
    SK_reset_board();

    /* Initialize TMD's with start values */
    p->tmdnum = 0;                   /* First descriptor for transmitting */ 
    p->tmdlast = 0;                  /* First descriptor for reading stats */

    for (i = 0; i < TMDNUM; i++)     /* Init all TMD's */
    {
	tmdp = p->tmdhead + i; 
   
	writel((unsigned long) p->tmdbufs[i], tmdp->u.buffer); /* assign buffer */
	
	/* Mark TMD as start and end of packet */
	writeb(TX_STP | TX_ENP, &tmdp->u.s.status);
    }


    /* Initialize RMD's with start values */

    p->rmdnum = 0;                   /* First RMD which will be used */
 
    for (i = 0; i < RMDNUM; i++)     /* Init all RMD's */
    {
	rmdp = p->rmdhead + i;

	
	writel((unsigned long) p->rmdbufs[i], rmdp->u.buffer); /* assign buffer */
	
	/* 
         * LANCE must be owner at beginning so that he can fill in 
	 * receiving packets, set status and release RMD 
	 */

	writeb(RX_OWN, &rmdp->u.s.status);

	writew(-PKT_BUF_SZ, &rmdp->blen); /* Buffer Size (two's complement) */

	writeb(0, &rmdp->mlen);           /* init message length */       
	
    }

    /* Fill LANCE Initialize Block */

    writew(mode, (&((p->ram)->ib.mode))); /* Set operation mode */

    for (i = 0; i < ETH_ALEN; i++)   /* Set physical address */
    {
	writeb(dev->dev_addr[i], (&((p->ram)->ib.paddr[i]))); 
    }

    for (i = 0; i < 8; i++)          /* Set multicast, logical address */
    {
	writeb(0, (&((p->ram)->ib.laddr[i]))); /* We do not use logical addressing */
    } 

    /* Set ring descriptor pointers and set number of descriptors */

    writel((int)p->rmdhead | RMDNUMMASK, (&((p->ram)->ib.rdrp)));
    writel((int)p->tmdhead | TMDNUMMASK, (&((p->ram)->ib.tdrp)));

    /* Prepare LANCE Control and Status Registers */

    spin_lock_irqsave(&SK_lock, flags);

    SK_write_reg(CSR3, CSR3_ACON);   /* Ale Control !!!THIS MUST BE SET!!!! */
 
    /* 
     * LANCE addresses the RAM from 0x0000 to 0x3fbf and has no access to
     * PC Memory locations.
     *
     * In structure SK_ram is defined that the first thing in ram
     * is the initialization block. So his address is for LANCE always
     * 0x0000
     *
     * CSR1 contains low order bits 15:0 of initialization block address
     * CSR2 is built of: 
     *    7:0  High order bits 23:16 of initialization block address
     *   15:8  reserved, must be 0
     */
    
    /* Set initialization block address (must be on word boundary) */
    SK_write_reg(CSR1, 0);          /* Set low order bits 15:0 */
    SK_write_reg(CSR2, 0);          /* Set high order bits 23:16 */ 
    

    PRINTK(("## %s: After setting CSR1-3. CSR0: %#06x\n", 
           SK_NAME, SK_read_reg(CSR0)));

    /* Initialize LANCE */

    /* 
     * INIT = Initialize, when set, causes the LANCE to begin the
     * initialization procedure and access the Init Block.
     */

    SK_write_reg(CSR0, CSR0_INIT); 

    spin_unlock_irqrestore(&SK_lock, flags);

    /* Wait until LANCE finished initialization */
    
    SK_set_RAP(CSR0);              /* Register Address Pointer to CSR0 */

    for (i = 0; (i < 100) && !(SK_rread_reg() & CSR0_IDON); i++) 
	; /* Wait until init done or go ahead if problems (i>=100) */

    if (i >= 100) /* Something is wrong ! */
    {
	printk("%s: can't init am7990, status: %04x "
	       "init_block: %#08x\n", 
		dev->name, (int) SK_read_reg(CSR0), 
		(unsigned int) &(p->ram)->ib);

#ifdef SK_DEBUG
	SK_print_pos(dev, "LANCE INIT failed");
	SK_print_dev(dev,"Device Structure:");
#endif

	return -1;                 /* LANCE init failed */
    }

    PRINTK(("## %s: init done after %d ticks\n", SK_NAME, i));

    /* Clear Initialize done, enable Interrupts, start LANCE */

    SK_write_reg(CSR0, CSR0_IDON | CSR0_INEA | CSR0_STRT);

    PRINTK(("## %s: LANCE started. CSR0: %#06x\n", SK_NAME, 
            SK_read_reg(CSR0)));

    return 0;                      /* LANCE is up and running */

} /* End of SK_lance_init() */



/*-
 * Function       : SK_send_packet
 * Author         : Patrick J.D. Weichmann
 * Date Created   : 94/05/27
 *
 * Description    : Writes an socket buffer into a transmit descriptor
 *                  and starts transmission.
 *
 * Parameters     : I : struct sk_buff *skb - packet to transfer
 *                  I : struct net_device *dev  - SK_G16 device structure
 * Return Value   : 0 - OK
 *                  1 - Could not transmit (dev_queue_xmit will queue it)
 *                      and try to sent it later
 * Globals        : None
 * Side Effects   : None
 * Update History :
 *     YY/MM/DD  uid  Description
-*/

static void SK_timeout(struct net_device *dev)
{
	printk(KERN_WARNING "%s: xmitter timed out, try to restart!\n", dev->name);
	SK_lance_init(dev, MODE_NORMAL); /* Reinit LANCE */
	netif_wake_queue(dev);		 /* Clear Transmitter flag */
	dev->trans_start = jiffies;      /* Mark Start of transmission */
}

static int SK_send_packet(struct sk_buff *skb, struct net_device *dev)
{
    struct priv *p = netdev_priv(dev);
    struct tmd *tmdp;
    static char pad[64];

    PRINTK2(("## %s: SK_send_packet() called, CSR0 %#04x.\n", 
	    SK_NAME, SK_read_reg(CSR0)));


    /* 
     * Block a timer-based transmit from overlapping. 
     * This means check if we are already in. 
     */

    netif_stop_queue (dev);

    {

	/* Evaluate Packet length */
	short len = ETH_ZLEN < skb->len ? skb->len : ETH_ZLEN; 
       
	tmdp = p->tmdhead + p->tmdnum; /* Which descriptor for transmitting */

	/* Fill in Transmit Message Descriptor */

	/* Copy data into dual ported ram */

	memcpy_toio((tmdp->u.buffer & 0x00ffffff), skb->data, skb->len);
	if (len != skb->len)
		memcpy_toio((tmdp->u.buffer & 0x00ffffff) + skb->len, pad, len-skb->len);

	writew(-len, &tmdp->blen);            /* set length to transmit */

	/* 
	 * Packet start and end is always set because we use the maximum
	 * packet length as buffer length.
	 * Relinquish ownership to LANCE
	 */

	writeb(TX_OWN | TX_STP | TX_ENP, &tmdp->u.s.status);
	
	/* Start Demand Transmission */
	SK_write_reg(CSR0, CSR0_TDMD | CSR0_INEA);

	dev->trans_start = jiffies;   /* Mark start of transmission */

	/* Set pointer to next transmit buffer */
	p->tmdnum++; 
	p->tmdnum &= TMDNUM-1; 

	/* Do we own the next transmit buffer ? */
	if (! (readb(&((p->tmdhead + p->tmdnum)->u.s.status)) & TX_OWN) )
	{
	   /* 
	    * We own next buffer and are ready to transmit, so
	    * clear busy flag
	    */
	   netif_start_queue(dev);
	}

	p->stats.tx_bytes += skb->len;

    }

    dev_kfree_skb(skb);
    return 0;  
} /* End of SK_send_packet */


/*-
 * Function       : SK_interrupt
 * Author         : Patrick J.D. Weichmann
 * Date Created   : 94/05/27
 *
 * Description    : SK_G16 interrupt handler which checks for LANCE
 *                  Errors, handles transmit and receive interrupts
 *
 * Parameters     : I : int irq, void *dev_id, struct pt_regs * regs -
 * Return Value   : None
 * Errors         : None
 * Globals        : None
 * Side Effects   : None
 * Update History :
 *     YY/MM/DD  uid  Description
-*/

static irqreturn_t SK_interrupt(int irq, void *dev_id, struct pt_regs * regs)
{
    int csr0;
    struct net_device *dev = dev_id;
    struct priv *p = netdev_priv(dev);


    PRINTK2(("## %s: SK_interrupt(). status: %#06x\n", 
            SK_NAME, SK_read_reg(CSR0)));

    if (dev == NULL)
    {
	printk("SK_interrupt(): IRQ %d for unknown device.\n", irq);
    }
    
    spin_lock (&SK_lock);

    csr0 = SK_read_reg(CSR0);      /* store register for checking */

    /* 
     * Acknowledge all of the current interrupt sources, disable      
     * Interrupts (INEA = 0) 
     */

    SK_write_reg(CSR0, csr0 & CSR0_CLRALL); 

    if (csr0 & CSR0_ERR) /* LANCE Error */
    {
	printk("%s: error: %04x\n", dev->name, csr0);
      
        if (csr0 & CSR0_MISS)      /* No place to store packet ? */
        { 
            p->stats.rx_dropped++;
        }
    }

    if (csr0 & CSR0_RINT)          /* Receive Interrupt (packet arrived) */ 
    {
	SK_rxintr(dev); 
    }

    if (csr0 & CSR0_TINT)          /* Transmit interrupt (packet sent) */
    {
	SK_txintr(dev);
    }

    SK_write_reg(CSR0, CSR0_INEA); /* Enable Interrupts */

    spin_unlock (&SK_lock);
    return IRQ_HANDLED;
} /* End of SK_interrupt() */ 


/*-
 * Function       : SK_txintr
 * Author         : Patrick J.D. Weichmann
 * Date Created   : 94/05/27
 *
 * Description    : After sending a packet we check status, update
 *                  statistics and relinquish ownership of transmit 
 *                  descriptor ring.
 *
 * Parameters     : I : struct net_device *dev - SK_G16 device structure
 * Return Value   : None
 * Errors         : None
 * Globals        : None
 * Update History :
 *     YY/MM/DD  uid  Description
-*/

static void SK_txintr(struct net_device *dev)
{
    int tmdstat;
    struct tmd *tmdp;
    struct priv *p = netdev_priv(dev);


    PRINTK2(("## %s: SK_txintr() status: %#06x\n", 
            SK_NAME, SK_read_reg(CSR0)));

    tmdp = p->tmdhead + p->tmdlast;     /* Which buffer we sent at last ? */

    /* Set next buffer */
    p->tmdlast++;
    p->tmdlast &= TMDNUM-1;

    tmdstat = readb(&tmdp->u.s.status);

    /* 
     * We check status of transmitted packet.
     * see LANCE data-sheet for error explanation
     */
    if (tmdstat & TX_ERR) /* Error occurred */
    {
	int stat2 = readw(&tmdp->status2);

	printk("%s: TX error: %04x %04x\n", dev->name, tmdstat, stat2);

	if (stat2 & TX_TDR)    /* TDR problems? */
	{
	    printk("%s: tdr-problems \n", dev->name);
	}

	if (stat2 & TX_RTRY)   /* Failed in 16 attempts to transmit ? */
            p->stats.tx_aborted_errors++;   
        if (stat2 & TX_LCOL)   /* Late collision ? */
            p->stats.tx_window_errors++; 
	if (stat2 & TX_LCAR)   /* Loss of Carrier ? */  
            p->stats.tx_carrier_errors++;
        if (stat2 & TX_UFLO)   /* Underflow error ? */
        {
            p->stats.tx_fifo_errors++;

            /* 
             * If UFLO error occurs it will turn transmitter of.
             * So we must reinit LANCE
             */

            SK_lance_init(dev, MODE_NORMAL);
        }
	
	p->stats.tx_errors++;

	writew(0, &tmdp->status2);             /* Clear error flags */
    }
    else if (tmdstat & TX_MORE)        /* Collisions occurred ? */
    {
        /* 
         * Here I have a problem.
         * I only know that there must be one or up to 15 collisions.
         * That's why TX_MORE is set, because after 16 attempts TX_RTRY
         * will be set which means couldn't send packet aborted transfer.
         *
         * First I did not have this in but then I thought at minimum
         * we see that something was not ok.
         * If anyone knows something better than this to handle this
         * please report it.
         */ 

        p->stats.collisions++; 
    }
    else   /* Packet sent without any problems */
    {
        p->stats.tx_packets++; 
    }

    /* 
     * We mark transmitter not busy anymore, because now we have a free
     * transmit descriptor which can be filled by SK_send_packet and
     * afterwards sent by the LANCE
     * 
     * The function which do handle slow IRQ parts is do_bottom_half()
     * which runs at normal kernel priority, that means all interrupt are
     * enabled. (see kernel/irq.c)
     *  
     * net_bh does something like this:
     *  - check if already in net_bh
     *  - try to transmit something from the send queue
     *  - if something is in the receive queue send it up to higher 
     *    levels if it is a known protocol
     *  - try to transmit something from the send queue
     */

    netif_wake_queue(dev);

} /* End of SK_txintr() */


/*-
 * Function       : SK_rxintr
 * Author         : Patrick J.D. Weichmann
 * Date Created   : 94/05/27
 *
 * Description    : Buffer sent, check for errors, relinquish ownership
 *                  of the receive message descriptor. 
 *
 * Parameters     : I : SK_G16 device structure
 * Return Value   : None
 * Globals        : None
 * Update History :
 *     YY/MM/DD  uid  Description
-*/

static void SK_rxintr(struct net_device *dev)
{

    struct rmd *rmdp;
    int rmdstat;
    struct priv *p = netdev_priv(dev);

    PRINTK2(("## %s: SK_rxintr(). CSR0: %#06x\n", 
            SK_NAME, SK_read_reg(CSR0)));

    rmdp = p->rmdhead + p->rmdnum;

    /* As long as we own the next entry, check status and send
     * it up to higher layer 
     */

    while (!( (rmdstat = readb(&rmdp->u.s.status)) & RX_OWN))
    {
	/* 
         * Start and end of packet must be set, because we use 
	 * the ethernet maximum packet length (1518) as buffer size.
	 * 
	 * Because our buffers are at maximum OFLO and BUFF errors are
	 * not to be concerned (see Data sheet)
	 */

	if ((rmdstat & (RX_STP | RX_ENP)) != (RX_STP | RX_ENP))
	{
	    /* Start of a frame > 1518 Bytes ? */

	    if (rmdstat & RX_STP) 
	    {
		p->stats.rx_errors++;        /* bad packet received */
		p->stats.rx_length_errors++; /* packet too long */

		printk("%s: packet too long\n", dev->name);
	    }
	    
	    /* 
             * All other packets will be ignored until a new frame with
	     * start (RX_STP) set follows.
	     * 
	     * What we do is just give descriptor free for new incoming
	     * packets. 
	     */

	    writeb(RX_OWN, &rmdp->u.s.status); /* Relinquish ownership to LANCE */ 

	}
	else if (rmdstat & RX_ERR)          /* Receive Error ? */
	{
	    printk("%s: RX error: %04x\n", dev->name, (int) rmdstat);
	    
	    p->stats.rx_errors++;

	    if (rmdstat & RX_FRAM) p->stats.rx_frame_errors++;
	    if (rmdstat & RX_CRC)  p->stats.rx_crc_errors++;

	    writeb(RX_OWN, &rmdp->u.s.status); /* Relinquish ownership to LANCE */

	}
	else /* We have a packet which can be queued for the upper layers */
	{

	    int len = readw(&rmdp->mlen) & 0x0fff;  /* extract message length from receive buffer */
	    struct sk_buff *skb;

	    skb = dev_alloc_skb(len+2); /* allocate socket buffer */ 

	    if (skb == NULL)                /* Could not get mem ? */
	    {
    
		/* 
                 * Couldn't allocate sk_buffer so we give descriptor back
		 * to Lance, update statistics and go ahead.
		 */

		writeb(RX_OWN, &rmdp->u.s.status); /* Relinquish ownership to LANCE */
		printk("%s: Couldn't allocate sk_buff, deferring packet.\n",
		       dev->name);
		p->stats.rx_dropped++;

		break;                      /* Jump out */
	    }
	    
	    /* Prepare sk_buff to queue for upper layers */

	    skb->dev = dev;
	    skb_reserve(skb,2);		/* Align IP header on 16 byte boundary */
	    
	    /* 
             * Copy data out of our receive descriptor into sk_buff.
	     *
	     * (rmdp->u.buffer & 0x00ffffff) -> get address of buffer and 
	     * ignore status fields) 
	     */

	    memcpy_fromio(skb_put(skb,len), (rmdp->u.buffer & 0x00ffffff), len);


	    /* 
             * Notify the upper protocol layers that there is another packet
	     * to handle
	     *
	     * netif_rx() always succeeds. see /net/inet/dev.c for more.
	     */

	    skb->protocol=eth_type_trans(skb,dev);
	    netif_rx(skb);                 /* queue packet and mark it for processing */
	   
	    /* 
             * Packet is queued and marked for processing so we
	     * free our descriptor and update statistics 
	     */

	    writeb(RX_OWN, &rmdp->u.s.status);
	    dev->last_rx = jiffies;
	    p->stats.rx_packets++;
	    p->stats.rx_bytes += len;


	    p->rmdnum++;
	    p->rmdnum %= RMDNUM;

	    rmdp = p->rmdhead + p->rmdnum;
	}
    }
} /* End of SK_rxintr() */


/*-
 * Function       : SK_close
 * Author         : Patrick J.D. Weichmann
 * Date Created   : 94/05/26
 *
 * Description    : close gets called from dev_close() and should
 *                  deinstall the card (free_irq, mem etc).
 *
 * Parameters     : I : struct net_device *dev - our device structure
 * Return Value   : 0 - closed device driver
 * Errors         : None
 * Globals        : None
 * Update History :
 *     YY/MM/DD  uid  Description
-*/

/* I have tried to set BOOT_ROM on and RAM off but then, after a 'ifconfig
 * down' the system stops. So I don't shut set card to init state.
 */

static int SK_close(struct net_device *dev)
{

    PRINTK(("## %s: SK_close(). CSR0: %#06x\n", 
           SK_NAME, SK_read_reg(CSR0)));

    netif_stop_queue(dev);	   /* Transmitter busy */

    printk("%s: Shutting %s down CSR0 %#06x\n", dev->name, SK_NAME, 
           (int) SK_read_reg(CSR0));

    SK_write_reg(CSR0, CSR0_STOP); /* STOP the LANCE */

    free_irq(dev->irq, dev);      /* Free IRQ */

    return 0; /* always succeed */
    
} /* End of SK_close() */


/*-
 * Function       : SK_get_stats
 * Author         : Patrick J.D. Weichmann
 * Date Created   : 94/05/26
 *
 * Description    : Return current status structure to upper layers.
 *                  It is called by sprintf_stats (dev.c).
 *
 * Parameters     : I : struct net_device *dev   - our device structure
 * Return Value   : struct net_device_stats * - our current statistics
 * Errors         : None
 * Side Effects   : None
 * Update History :
 *     YY/MM/DD  uid  Description
-*/

static struct net_device_stats *SK_get_stats(struct net_device *dev)
{

    struct priv *p = netdev_priv(dev);

    PRINTK(("## %s: SK_get_stats(). CSR0: %#06x\n", 
           SK_NAME, SK_read_reg(CSR0)));

    return &p->stats;             /* Return Device status */

} /* End of SK_get_stats() */


/*-
 * Function       : set_multicast_list
 * Author         : Patrick J.D. Weichmann
 * Date Created   : 94/05/26
 *
 * Description    : This function gets called when a program performs
 *                  a SIOCSIFFLAGS call. Ifconfig does this if you call
 *                  'ifconfig [-]allmulti' which enables or disables the
 *                  Promiscuous mode.
 *                  Promiscuous mode is when the Network card accepts all
 *                  packets, not only the packets which match our MAC 
 *                  Address. It is useful for writing a network monitor,
 *                  but it is also a security problem. You have to remember
 *                  that all information on the net is not encrypted.
 *
 * Parameters     : I : struct net_device *dev - SK_G16 device Structure
 * Return Value   : None
 * Errors         : None
 * Globals        : None
 * Update History :
 *     YY/MM/DD  uid  Description
 *     95/10/18  ACox  New multicast calling scheme
-*/


/* Set or clear the multicast filter for SK_G16.
 */

static void set_multicast_list(struct net_device *dev)
{

    if (dev->flags&IFF_PROMISC)
    {
	/* Reinitialize LANCE with MODE_PROM set */
	SK_lance_init(dev, MODE_PROM);
    }
    else if (dev->mc_count==0 && !(dev->flags&IFF_ALLMULTI))
    {
	/* Reinitialize LANCE without MODE_PROM */
	SK_lance_init(dev, MODE_NORMAL);
    }
    else
    {
	/* Multicast with logical address filter on */
	/* Reinitialize LANCE without MODE_PROM */
	SK_lance_init(dev, MODE_NORMAL);
	
	/* Not implemented yet. */
    }
} /* End of set_multicast_list() */



/*-
 * Function       : SK_rom_addr
 * Author         : Patrick J.D. Weichmann
 * Date Created   : 94/06/01
 *
 * Description    : Try to find a Boot_ROM at all possible locations
 *
 * Parameters     : None
 * Return Value   : Address where Boot_ROM is
 * Errors         : 0 - Did not find Boot_ROM
 * Globals        : None
 * Update History :
 *     YY/MM/DD  uid  Description
-*/

unsigned int __init SK_rom_addr(void)
{
    int i,j;
    int rom_found = 0;
    unsigned int rom_location[] = SK_BOOT_ROM_LOCATIONS;
    unsigned char rom_id[] = SK_BOOT_ROM_ID;
    unsigned char test_byte;

    /* Autodetect Boot_ROM */
    PRINTK(("## %s: Autodetection of Boot_ROM\n", SK_NAME));

    for (i = 0; (rom_location[i] != 0) && (rom_found == 0); i++)
    {
	
	PRINTK(("##   Trying ROM location %#08x", rom_location[i]));
	
	rom_found = 1; 
	for (j = 0; j < 6; j++)
	{
	    test_byte = readb(rom_location[i]+j);
	    PRINTK((" %02x ", *test_byte));

	    if(test_byte != rom_id[j])
	    {
		rom_found = 0;
	    } 
	}
	PRINTK(("\n"));
    }

    if (rom_found == 1)
    {
	PRINTK(("## %s: Boot_ROM found at %#08x\n", 
               SK_NAME, rom_location[(i-1)]));

	return (rom_location[--i]);
    }
    else
    {
	PRINTK(("%s: No Boot_ROM found\n", SK_NAME));
	return 0;
    }
} /* End of SK_rom_addr() */



/* LANCE access functions 
 *
 * ! CSR1-3 can only be accessed when in CSR0 the STOP bit is set !
 */


/*-
 * Function       : SK_reset_board
 *
 * Author         : Patrick J.D. Weichmann
 *
 * Date Created   : 94/05/25
 *
 * Description    : This function resets SK_G16 and all components, but
 *                  POS registers are not changed
 *
 * Parameters     : None
 * Return Value   : None
 * Errors         : None
 * Globals        : SK_RAM *board - SK_RAM structure pointer
 *
 * Update History :
 *     YY/MM/DD  uid  Description
-*/

void SK_reset_board(void)
{
    writeb(0x00, SK_PORT);       /* Reset active */
    mdelay(5);                /* Delay min 5ms */
    writeb(SK_RESET, SK_PORT);   /* Set back to normal operation */

} /* End of SK_reset_board() */


/*-
 * Function       : SK_set_RAP
 * Author         : Patrick J.D. Weichmann
 * Date Created   : 94/05/25
 *
 * Description    : Set LANCE Register Address Port to register
 *                  for later data transfer.
 *
 * Parameters     : I : reg_number - which CSR to read/write from/to
 * Return Value   : None
 * Errors         : None
 * Globals        : SK_RAM *board - SK_RAM structure pointer
 * Update History :
 *     YY/MM/DD  uid  Description
-*/

void SK_set_RAP(int reg_number)
{
    writew(reg_number, SK_IOREG);
    writeb(SK_RESET | SK_RAP | SK_WREG, SK_PORT);
    writeb(SK_DOIO, SK_IOCOM);

    while (readb(SK_PORT) & SK_IORUN) 
	barrier();
} /* End of SK_set_RAP() */


/*-
 * Function       : SK_read_reg
 * Author         : Patrick J.D. Weichmann
 * Date Created   : 94/05/25
 *
 * Description    : Set RAP and read data from a LANCE CSR register
 *
 * Parameters     : I : reg_number - which CSR to read from
 * Return Value   : Register contents
 * Errors         : None
 * Globals        : SK_RAM *board - SK_RAM structure pointer
 * Update History :
 *     YY/MM/DD  uid  Description
-*/

int SK_read_reg(int reg_number)
{
    SK_set_RAP(reg_number);

    writeb(SK_RESET | SK_RDATA | SK_RREG, SK_PORT);
    writeb(SK_DOIO, SK_IOCOM);

    while (readb(SK_PORT) & SK_IORUN)
	barrier();
    return (readw(SK_IOREG));

} /* End of SK_read_reg() */


/*-
 * Function       : SK_rread_reg
 * Author         : Patrick J.D. Weichmann
 * Date Created   : 94/05/28
 *
 * Description    : Read data from preseted register.
 *                  This function requires that you know which
 *                  Register is actually set. Be aware that CSR1-3
 *                  can only be accessed when in CSR0 STOP is set.
 *
 * Return Value   : Register contents
 * Errors         : None
 * Globals        : SK_RAM *board - SK_RAM structure pointer
 * Update History :
 *     YY/MM/DD  uid  Description
-*/

int SK_rread_reg(void)
{
    writeb(SK_RESET | SK_RDATA | SK_RREG, SK_PORT);

    writeb(SK_DOIO, SK_IOCOM);

    while (readb(SK_PORT) & SK_IORUN)
	barrier();
    return (readw(SK_IOREG));

} /* End of SK_rread_reg() */


/*-
 * Function       : SK_write_reg
 * Author         : Patrick J.D. Weichmann
 * Date Created   : 94/05/25
 *
 * Description    : This function sets the RAP then fills in the
 *                  LANCE I/O Reg and starts Transfer to LANCE.
 *                  It waits until transfer has ended which is max. 7 ms
 *                  and then it returns.
 *
 * Parameters     : I : reg_number - which CSR to write to
 *                  I : value      - what value to fill into register 
 * Return Value   : None
 * Errors         : None
 * Globals        : SK_RAM *board - SK_RAM structure pointer
 * Update History :
 *     YY/MM/DD  uid  Description
-*/

void SK_write_reg(int reg_number, int value)
{
    SK_set_RAP(reg_number);

    writew(value, SK_IOREG);
    writeb(SK_RESET | SK_RDATA | SK_WREG, SK_PORT);
    writeb(SK_DOIO, SK_IOCOM);

    while (readb(SK_PORT) & SK_IORUN)
	barrier();
} /* End of SK_write_reg */



/* 
 * Debugging functions
 * -------------------
 */

/*-
 * Function       : SK_print_pos
 * Author         : Patrick J.D. Weichmann
 * Date Created   : 94/05/25
 *
 * Description    : This function prints out the 4 POS (Programmable
 *                  Option Select) Registers. Used mainly to debug operation.
 *
 * Parameters     : I : struct net_device *dev - SK_G16 device structure
 *                  I : char * - Text which will be printed as title
 * Return Value   : None
 * Errors         : None
 * Update History :
 *     YY/MM/DD  uid  Description
-*/

void SK_print_pos(struct net_device *dev, char *text)
{
    int ioaddr = dev->base_addr;

    unsigned char pos0 = inb(SK_POS0),
		  pos1 = inb(SK_POS1),
		  pos2 = inb(SK_POS2),
		  pos3 = inb(SK_POS3),
		  pos4 = inb(SK_POS4);


    printk("## %s: %s.\n"
           "##   pos0=%#4x pos1=%#4x pos2=%#04x pos3=%#08x pos4=%#04x\n",
           SK_NAME, text, pos0, pos1, pos2, (pos3<<14), pos4);

} /* End of SK_print_pos() */



/*-
 * Function       : SK_print_dev
 * Author         : Patrick J.D. Weichmann
 * Date Created   : 94/05/25
 *
 * Description    : This function simply prints out the important fields
 *                  of the device structure.
 *
 * Parameters     : I : struct net_device *dev  - SK_G16 device structure
 *                  I : char *text - Title for printing
 * Return Value   : None
 * Errors         : None
 * Update History :
 *     YY/MM/DD  uid  Description
-*/

void SK_print_dev(struct net_device *dev, char *text)
{
    if (dev == NULL)
    {
	printk("## %s: Device Structure. %s\n", SK_NAME, text);
	printk("## DEVICE == NULL\n");
    }
    else
    {
	printk("## %s: Device Structure. %s\n", SK_NAME, text);
	printk("## Device Name: %s Base Address: %#06lx IRQ: %d\n", 
               dev->name, dev->base_addr, dev->irq);
	       
	printk("## next device: %#08x init function: %#08x\n", 
              (int) dev->next, (int) dev->init);
    }

} /* End of SK_print_dev() */



/*-
 * Function       : SK_print_ram
 * Author         : Patrick J.D. Weichmann
 * Date Created   : 94/06/02
 *
 * Description    : This function is used to check how are things set up
 *                  in the 16KB RAM. Also the pointers to the receive and 
 *                  transmit descriptor rings and rx and tx buffers locations.
 *                  It contains a minor bug in printing, but has no effect to the values
 *                  only newlines are not correct.
 *
 * Parameters     : I : struct net_device *dev - SK_G16 device structure
 * Return Value   : None
 * Errors         : None
 * Globals        : None
 * Update History :
 *     YY/MM/DD  uid  Description
-*/

void __init SK_print_ram(struct net_device *dev)
{

    int i;    
    struct priv *p = netdev_priv(dev);

    printk("## %s: RAM Details.\n"
           "##   RAM at %#08x tmdhead: %#08x rmdhead: %#08x initblock: %#08x\n",
           SK_NAME, 
           (unsigned int) p->ram,
           (unsigned int) p->tmdhead, 
           (unsigned int) p->rmdhead, 
           (unsigned int) &(p->ram)->ib);
           
    printk("##   ");

    for(i = 0; i < TMDNUM; i++)
    {
           if (!(i % 3)) /* Every third line do a newline */
           {
               printk("\n##   ");
           }
        printk("tmdbufs%d: %#08x ", (i+1), (int) p->tmdbufs[i]);
    }
    printk("##   ");

    for(i = 0; i < RMDNUM; i++)
    {
         if (!(i % 3)) /* Every third line do a newline */
           {
               printk("\n##   ");
           }
        printk("rmdbufs%d: %#08x ", (i+1), (int) p->rmdbufs[i]);
    } 
    printk("\n");

} /* End of SK_print_ram() */

