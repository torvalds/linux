/*
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include "includes.h"
#include "hardware.h"
#include "card.h"

MODULE_DESCRIPTION("ISDN4Linux: Driver for Spellcaster card");
MODULE_AUTHOR("Spellcaster Telecommunications Inc.");
MODULE_LICENSE("GPL");

board *sc_adapter[MAX_CARDS];
int cinst;

static char devname[] = "scX";
const char version[] = "2.0b1";

const char *boardname[] = { "DataCommute/BRI", "DataCommute/PRI", "TeleCommute/BRI" };

/* insmod set parameters */
static unsigned int io[] = {0,0,0,0};
static unsigned char irq[] = {0,0,0,0};
static unsigned long ram[] = {0,0,0,0};
static int do_reset = 0;

module_param_array(io, int, NULL, 0);
module_param_array(irq, int, NULL, 0);
module_param_array(ram, int, NULL, 0);
module_param(do_reset, bool, 0);

static int sup_irq[] = { 11, 10, 9, 5, 12, 14, 7, 3, 4, 6 };
#define MAX_IRQS	10

extern irqreturn_t interrupt_handler(int, void *, struct pt_regs *);
extern int sndpkt(int, int, int, struct sk_buff *);
extern int command(isdn_ctrl *);
extern int indicate_status(int, int, ulong, char*);
extern int reset(int);

int identify_board(unsigned long, unsigned int);

int irq_supported(int irq_x)
{
	int i;
	for(i=0 ; i < MAX_IRQS ; i++) {
		if(sup_irq[i] == irq_x)
			return 1;
	}
	return 0;
}

static int __init sc_init(void)
{
	int b = -1;
	int i, j;
	int status = -ENODEV;

	unsigned long memsize = 0;
	unsigned long features = 0;
	isdn_if *interface;
	unsigned char channels;
	unsigned char pgport;
	unsigned long magic;
	int model;
	int last_base = IOBASE_MIN;
	int probe_exhasted = 0;

#ifdef MODULE
	pr_info("SpellCaster ISA ISDN Adapter Driver rev. %s Loaded\n", version);
#else
	pr_info("SpellCaster ISA ISDN Adapter Driver rev. %s\n", version);
#endif
	pr_info("Copyright (C) 1996 SpellCaster Telecommunications Inc.\n");

	while(b++ < MAX_CARDS - 1) {
		pr_debug("Probing for adapter #%d\n", b);
		/*
		 * Initialize reusable variables
		 */
		model = -1;
		magic = 0;
		channels = 0;
		pgport = 0;

		/* 
		 * See if we should probe for IO base 
		 */
		pr_debug("I/O Base for board %d is 0x%x, %s probe\n", b, io[b],
			io[b] == 0 ? "will" : "won't");
		if(io[b]) {
			/*
			 * No, I/O Base has been provided
			 */
			for (i = 0 ; i < MAX_IO_REGS - 1 ; i++) {
				if(!request_region(io[b] + i * 0x400, 1, "sc test")) {
					pr_debug("check_region for 0x%x failed\n", io[b] + i * 0x400);
					io[b] = 0;
					break;
				} else
					release_region(io[b] + i * 0x400, 1);
			}

			/*
			 * Confirm the I/O Address with a test
			 */
			if(io[b] == 0) {
				pr_debug("I/O Address 0x%x is in use.\n");
				continue;
			}

			outb(0x18, io[b] + 0x400 * EXP_PAGE0);
			if(inb(io[b] + 0x400 * EXP_PAGE0) != 0x18) {
				pr_debug("I/O Base 0x%x fails test\n");
				continue;
			}
		}
		else {
			/*
			 * Yes, probe for I/O Base
			 */
			if(probe_exhasted) {
				pr_debug("All probe addresses exhasted, skipping\n");
				continue;
			}
			pr_debug("Probing for I/O...\n");
			for (i = last_base ; i <= IOBASE_MAX ; i += IOBASE_OFFSET) {
				int found_io = 1;
				if (i == IOBASE_MAX) {
					probe_exhasted = 1; /* No more addresses to probe */
					pr_debug("End of Probes\n");
				}
				last_base = i + IOBASE_OFFSET;
				pr_debug("  checking 0x%x...", i);
				for ( j = 0 ; j < MAX_IO_REGS - 1 ; j++) {
					if(!request_region(i + j * 0x400, 1, "sc test")) {
						pr_debug("Failed\n");
						found_io = 0;
						break;
					} else
						release_region(i + j * 0x400, 1);
				}	

				if(found_io) {
					io[b] = i;
					outb(0x18, io[b] + 0x400 * EXP_PAGE0);
					if(inb(io[b] + 0x400 * EXP_PAGE0) != 0x18) { 
						pr_debug("Failed by test\n");
						continue;
					}
					pr_debug("Passed\n");
					break;
				}
			}
			if(probe_exhasted) {
				continue;
			}
		}

		/*
		 * See if we should probe for shared RAM
		 */
		if(do_reset) {
			pr_debug("Doing a SAFE probe reset\n");
			outb(0xFF, io[b] + RESET_OFFSET);
			msleep_interruptible(10000);
		}
		pr_debug("RAM Base for board %d is 0x%x, %s probe\n", b, ram[b],
			ram[b] == 0 ? "will" : "won't");

		if(ram[b]) {
			/*
			 * No, the RAM base has been provided
			 * Just look for a signature and ID the
			 * board model
			 */
			if(request_region(ram[b], SRAM_PAGESIZE, "sc test")) {
				pr_debug("request_region for RAM base 0x%x succeeded\n", ram[b]);
			 	model = identify_board(ram[b], io[b]);
				release_region(ram[b], SRAM_PAGESIZE);
			}
		}
		else {
			/*
			 * Yes, probe for free RAM and look for
			 * a signature and id the board model
			 */
			for (i = SRAM_MIN ; i < SRAM_MAX ; i += SRAM_PAGESIZE) {
				pr_debug("Checking RAM address 0x%x...\n", i);
				if(request_region(i, SRAM_PAGESIZE, "sc test")) {
					pr_debug("  check_region succeeded\n");
					model = identify_board(i, io[b]);
					release_region(i, SRAM_PAGESIZE);
					if (model >= 0) {
						pr_debug("  Identified a %s\n",
							boardname[model]);
						ram[b] = i;
						break;
					}
					pr_debug("  Unidentifed or inaccessible\n");
					continue;
				}
				pr_debug("  request failed\n");
			}
		}
		/*
		 * See if we found free RAM and the board model
		 */
		if(!ram[b] || model < 0) {
			/*
			 * Nope, there was no place in RAM for the
			 * board, or it couldn't be identified
			 */
			 pr_debug("Failed to find an adapter at 0x%x\n", ram[b]);
			 continue;
		}

		/*
		 * Set the board's magic number, memory size and page register
		 */
		switch(model) {
		case PRI_BOARD:
			channels = 23;
			magic = 0x20000;
			memsize = 0x100000;
			features = PRI_FEATURES;
			break;

		case BRI_BOARD:
		case POTS_BOARD:
			channels = 2;
			magic = 0x60000;
			memsize = 0x10000;
			features = BRI_FEATURES;
			break;
		}
		switch(ram[b] >> 12 & 0x0F) {
		case 0x0:
			pr_debug("RAM Page register set to EXP_PAGE0\n");
			pgport = EXP_PAGE0;
			break;

		case 0x4:
			pr_debug("RAM Page register set to EXP_PAGE1\n");
			pgport = EXP_PAGE1;
			break;

		case 0x8:
			pr_debug("RAM Page register set to EXP_PAGE2\n");
			pgport = EXP_PAGE2;
			break;

		case 0xC:
			pr_debug("RAM Page register set to EXP_PAGE3\n");
			pgport = EXP_PAGE3;
			break;

		default:
			pr_debug("RAM base address doesn't fall on 16K boundary\n");
			continue;
		}

		pr_debug("current IRQ: %d  b: %d\n",irq[b],b);

		/*
		 * Make sure we got an IRQ
		 */
		if(!irq[b]) {
			/*
			 * No interrupt could be used
			 */
			pr_debug("Failed to acquire an IRQ line\n");
			continue;
		}

		/*
		 * Horray! We found a board, Make sure we can register
		 * it with ISDN4Linux
		 */
		interface = kmalloc(sizeof(isdn_if), GFP_KERNEL);
		if (interface == NULL) {
			/*
			 * Oops, can't malloc isdn_if
			 */
			continue;
		}
		memset(interface, 0, sizeof(isdn_if));

		interface->owner = THIS_MODULE;
		interface->hl_hdrlen = 0;
		interface->channels = channels;
		interface->maxbufsize = BUFFER_SIZE;
		interface->features = features;
		interface->writebuf_skb = sndpkt;
		interface->writecmd = NULL;
		interface->command = command;
		strcpy(interface->id, devname);
		interface->id[2] = '0' + cinst;

		/*
		 * Allocate the board structure
		 */
		sc_adapter[cinst] = kmalloc(sizeof(board), GFP_KERNEL);
		if (sc_adapter[cinst] == NULL) {
			/*
			 * Oops, can't alloc memory for the board
			 */
			kfree(interface);
			continue;
		}
		memset(sc_adapter[cinst], 0, sizeof(board));
		spin_lock_init(&sc_adapter[cinst]->lock);

		if(!register_isdn(interface)) {
			/*
			 * Oops, couldn't register for some reason
			 */
			kfree(interface);
			kfree(sc_adapter[cinst]);
			continue;
		}

		sc_adapter[cinst]->card = interface;
		sc_adapter[cinst]->driverId = interface->channels;
		strcpy(sc_adapter[cinst]->devicename, interface->id);
		sc_adapter[cinst]->nChannels = channels;
		sc_adapter[cinst]->ramsize = memsize;
		sc_adapter[cinst]->shmem_magic = magic;
		sc_adapter[cinst]->shmem_pgport = pgport;
		sc_adapter[cinst]->StartOnReset = 1;

		/*
		 * Allocate channels status structures
		 */
		sc_adapter[cinst]->channel = kmalloc(sizeof(bchan) * channels, GFP_KERNEL);
		if (sc_adapter[cinst]->channel == NULL) {
			/*
			 * Oops, can't alloc memory for the channels
			 */
			indicate_status(cinst, ISDN_STAT_UNLOAD, 0, NULL);	/* Fix me */
			kfree(interface);
			kfree(sc_adapter[cinst]);
			continue;
		}
		memset(sc_adapter[cinst]->channel, 0, sizeof(bchan) * channels);

		/*
		 * Lock down the hardware resources
		 */
		sc_adapter[cinst]->interrupt = irq[b];
		if (request_irq(sc_adapter[cinst]->interrupt, interrupt_handler,
				SA_INTERRUPT, interface->id, NULL))
		{
			kfree(sc_adapter[cinst]->channel);
			indicate_status(cinst, ISDN_STAT_UNLOAD, 0, NULL);	/* Fix me */
			kfree(interface);
			kfree(sc_adapter[cinst]);
			continue;
			
		}
		sc_adapter[cinst]->iobase = io[b];
		for(i = 0 ; i < MAX_IO_REGS - 1 ; i++) {
			sc_adapter[cinst]->ioport[i] = io[b] + i * 0x400;
			request_region(sc_adapter[cinst]->ioport[i], 1,
					interface->id);
			pr_debug("Requesting I/O Port %#x\n",
				sc_adapter[cinst]->ioport[i]);
		}
		sc_adapter[cinst]->ioport[IRQ_SELECT] = io[b] + 0x2;
		request_region(sc_adapter[cinst]->ioport[IRQ_SELECT], 1,
				interface->id);
		pr_debug("Requesting I/O Port %#x\n",
				sc_adapter[cinst]->ioport[IRQ_SELECT]);
		sc_adapter[cinst]->rambase = ram[b];
		request_region(sc_adapter[cinst]->rambase, SRAM_PAGESIZE,
				interface->id);

		pr_info("  %s (%d) - %s %d channels IRQ %d, I/O Base 0x%x, RAM Base 0x%lx\n", 
			sc_adapter[cinst]->devicename,
			sc_adapter[cinst]->driverId,
			boardname[model], channels, irq[b], io[b], ram[b]);
		
		/*
		 * reset the adapter to put things in motion
		 */
		reset(cinst);

		cinst++;
		status = 0;
	}
	if (status) 
		pr_info("Failed to find any adapters, driver unloaded\n");
	return status;
}

static void __exit sc_exit(void)
{
	int i, j;

	for(i = 0 ; i < cinst ; i++) {
		pr_debug("Cleaning up after adapter %d\n", i);
		/*
		 * kill the timers
		 */
		del_timer(&(sc_adapter[i]->reset_timer));
		del_timer(&(sc_adapter[i]->stat_timer));

		/*
		 * Tell I4L we're toast
		 */
		indicate_status(i, ISDN_STAT_STOP, 0, NULL);
		indicate_status(i, ISDN_STAT_UNLOAD, 0, NULL);

		/*
		 * Release shared RAM
		 */
		release_region(sc_adapter[i]->rambase, SRAM_PAGESIZE);

		/*
		 * Release the IRQ
		 */
		FREE_IRQ(sc_adapter[i]->interrupt, NULL);

		/*
		 * Reset for a clean start
		 */
		outb(0xFF, sc_adapter[i]->ioport[SFT_RESET]);

		/*
		 * Release the I/O Port regions
		 */
		for(j = 0 ; j < MAX_IO_REGS - 1; j++) {
			release_region(sc_adapter[i]->ioport[j], 1);
			pr_debug("Releasing I/O Port %#x\n",
				sc_adapter[i]->ioport[j]);
		}
		release_region(sc_adapter[i]->ioport[IRQ_SELECT], 1);
		pr_debug("Releasing I/O Port %#x\n",
			sc_adapter[i]->ioport[IRQ_SELECT]);

		/*
		 * Release any memory we alloced
		 */
		kfree(sc_adapter[i]->channel);
		kfree(sc_adapter[i]->card);
		kfree(sc_adapter[i]);
	}
	pr_info("SpellCaster ISA ISDN Adapter Driver Unloaded.\n");
}

int identify_board(unsigned long rambase, unsigned int iobase) 
{
	unsigned int pgport;
	unsigned long sig;
	DualPortMemory *dpm;
	RspMessage rcvmsg;
	ReqMessage sndmsg;
	HWConfig_pl hwci;
	int x;

	pr_debug("Attempting to identify adapter @ 0x%x io 0x%x\n",
		rambase, iobase);

	/*
	 * Enable the base pointer
	 */
	outb(rambase >> 12, iobase + 0x2c00);

	switch(rambase >> 12 & 0x0F) {
	case 0x0:
		pgport = iobase + PG0_OFFSET;
		pr_debug("Page Register offset is 0x%x\n", PG0_OFFSET);
		break;
		
	case 0x4:
		pgport = iobase + PG1_OFFSET;
		pr_debug("Page Register offset is 0x%x\n", PG1_OFFSET);
		break;

	case 0x8:
		pgport = iobase + PG2_OFFSET;
		pr_debug("Page Register offset is 0x%x\n", PG2_OFFSET);
		break;

	case 0xC:
		pgport = iobase + PG3_OFFSET;
		pr_debug("Page Register offset is 0x%x\n", PG3_OFFSET);
		break;
	default:
		pr_debug("Invalid rambase 0x%lx\n", rambase);
		return -1;
	}

	/*
	 * Try to identify a PRI card
	 */
	outb(PRI_BASEPG_VAL, pgport);
	msleep_interruptible(1000);
	sig = readl(rambase + SIG_OFFSET);
	pr_debug("Looking for a signature, got 0x%x\n", sig);
	if(sig == SIGNATURE)
		return PRI_BOARD;

	/*
	 * Try to identify a PRI card
	 */
	outb(BRI_BASEPG_VAL, pgport);
	msleep_interruptible(1000);
	sig = readl(rambase + SIG_OFFSET);
	pr_debug("Looking for a signature, got 0x%x\n", sig);
	if(sig == SIGNATURE)
		return BRI_BOARD;

	return -1;

	/*
	 * Try to spot a card
	 */
	sig = readl(rambase + SIG_OFFSET);
	pr_debug("Looking for a signature, got 0x%x\n", sig);
	if(sig != SIGNATURE)
		return -1;

	dpm = (DualPortMemory *) rambase;

	memset(&sndmsg, 0, MSG_LEN);
	sndmsg.msg_byte_cnt = 3;
	sndmsg.type = cmReqType1;
	sndmsg.class = cmReqClass0;
	sndmsg.code = cmReqHWConfig;
	memcpy_toio(&(dpm->req_queue[dpm->req_head++]), &sndmsg, MSG_LEN);
	outb(0, iobase + 0x400);
	pr_debug("Sent HWConfig message\n");
	/*
	 * Wait for the response
	 */
	x = 0;
	while((inb(iobase + FIFOSTAT_OFFSET) & RF_HAS_DATA) && x < 100) {
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(1);
		x++;
	}
	if(x == 100) {
		pr_debug("Timeout waiting for response\n");
		return -1;
	}

	memcpy_fromio(&rcvmsg, &(dpm->rsp_queue[dpm->rsp_tail]), MSG_LEN);
	pr_debug("Got HWConfig response, status = 0x%x\n", rcvmsg.rsp_status);
	memcpy(&hwci, &(rcvmsg.msg_data.HWCresponse), sizeof(HWConfig_pl));
	pr_debug("Hardware Config: Interface: %s, RAM Size: %d, Serial: %s\n"
		 "                 Part: %s, Rev: %s\n",
		 hwci.st_u_sense ? "S/T" : "U", hwci.ram_size,
		 hwci.serial_no, hwci.part_no, hwci.rev_no);

	if(!strncmp(PRI_PARTNO, hwci.part_no, 6))
		return PRI_BOARD;
	if(!strncmp(BRI_PARTNO, hwci.part_no, 6))
		return BRI_BOARD;
		
	return -1;
}

module_init(sc_init);
module_exit(sc_exit);
