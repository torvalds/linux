/*
 * Copyright 2003 Digi International (www.digi.com)
 *	Scott H Kilau <Scott_Kilau at digi dot com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY, EXPRESS OR IMPLIED; without even the 
 * implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR 
 * PURPOSE.  See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *
 *	NOTE TO LINUX KERNEL HACKERS:  DO NOT REFORMAT THIS CODE!
 *
 *	This is shared code between Digi's CVS archive and the
 *	Linux Kernel sources.
 *	Changing the source just for reformatting needlessly breaks
 *	our CVS diff history.
 *
 *	Send any bug fixes/changes to:  Eng.Linux at digi dot com.
 *	Thank you.
 *
 * $Id: dgap_fep5.c,v 1.2 2011/06/21 10:35:40 markh Exp $
 */


#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/delay.h>	/* For udelay */
#include <asm/uaccess.h>	/* For copy_from_user/copy_to_user */
#include <linux/tty.h>
#include <linux/tty_flip.h>	/* For tty_schedule_flip */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,39)
#include <linux/sched.h>
#endif

#include "dgap_driver.h"
#include "dgap_pci.h"
#include "dgap_fep5.h"
#include "dgap_tty.h"
#include "dgap_conf.h"
#include "dgap_parse.h"
#include "dgap_trace.h"

/*
 * Our function prototypes
 */
static void dgap_cmdw_ext(struct channel_t *ch, u16 cmd, u16 word, uint ncmds);
static int dgap_event(struct board_t *bd);

/*
 * internal variables
 */
static uint dgap_count = 500;


/*
 * Loads the dgap.conf config file from the user.
 */
void dgap_do_config_load(uchar __user *uaddr, int len)
{
	int orig_len = len;
	char *to_addr;
	uchar __user *from_addr = uaddr;
	char buf[U2BSIZE];
	int n;

	to_addr = dgap_config_buf = dgap_driver_kzmalloc(len + 1, GFP_ATOMIC);
	if (!dgap_config_buf) {
		DPR_INIT(("dgap_do_config_load - unable to allocate memory for file\n"));
		dgap_driver_state = DRIVER_NEED_CONFIG_LOAD;
		return;
	}

	n = U2BSIZE;
	while (len) {

		if (n > len)
			n = len;

		if (copy_from_user((char *) &buf, from_addr, n) == -1 )
			return;

		/* Copy data from buffer to kernel memory */
		memcpy(to_addr, buf, n);

		/* increment counts */
		len -= n;
		to_addr += n;
		from_addr += n;
		n = U2BSIZE;
        }

	dgap_config_buf[orig_len] = '\0';

	to_addr = dgap_config_buf;
	dgap_parsefile(&to_addr, TRUE);

	DPR_INIT(("dgap_config_load() finish\n"));

	return;
}


int dgap_after_config_loaded(void)
{
	int i = 0;
	int rc = 0;

	/*
	 * Register our ttys, now that we have the config loaded.
	 */
	for (i = 0; i < dgap_NumBoards; ++i) {

		/*
		 * Initialize KME waitqueues...
		 */
		init_waitqueue_head(&(dgap_Board[i]->kme_wait));

		/*
		 * allocate flip buffer for board.
		 */
		dgap_Board[i]->flipbuf = dgap_driver_kzmalloc(MYFLIPLEN, GFP_ATOMIC);
		dgap_Board[i]->flipflagbuf = dgap_driver_kzmalloc(MYFLIPLEN, GFP_ATOMIC);
	}

	return (rc);
}



/*=======================================================================
 *
 *      usertoboard - copy from user space to board space.
 *
 *=======================================================================*/
static int dgap_usertoboard(struct board_t *brd, char *to_addr, char __user *from_addr, int len)
{
	char buf[U2BSIZE];
	int n = U2BSIZE;

	if (!brd || brd->magic != DGAP_BOARD_MAGIC)
		return(-EFAULT);

	while (len) {
		if (n > len)
			n = len;

		if (copy_from_user((char *) &buf, from_addr, n) == -1 ) {
			return(-EFAULT);
		}

		/* Copy data from buffer to card memory */
		memcpy_toio(to_addr, buf, n);

		/* increment counts */
		len -= n;
		to_addr += n;
		from_addr += n;   
		n = U2BSIZE;
        }
	return(0);
}


/*
 * Copies the BIOS code from the user to the board,
 * and starts the BIOS running.
 */
void dgap_do_bios_load(struct board_t *brd, uchar __user *ubios, int len)
{
	uchar *addr;
	uint offset;
	int i;

	if (!brd || (brd->magic != DGAP_BOARD_MAGIC) || !brd->re_map_membase)
		return;

	DPR_INIT(("dgap_do_bios_load() start\n"));

	addr = brd->re_map_membase;

	/*
	 * clear POST area
	 */
	for (i = 0; i < 16; i++)
		writeb(0, addr + POSTAREA + i);
                                
	/*
	 * Download bios
	 */
	offset = 0x1000;
	if (dgap_usertoboard(brd, addr + offset, ubios, len) == -1 ) {
		brd->state = BOARD_FAILED;
		brd->dpastatus = BD_NOFEP;
		return;
	}

	writel(0x0bf00401, addr);
	writel(0, (addr + 4));

	/* Clear the reset, and change states. */
	writeb(FEPCLR, brd->re_map_port);
	brd->state = WAIT_BIOS_LOAD;
}


/*
 * Checks to see if the BIOS completed running on the card.
 */
static void dgap_do_wait_for_bios(struct board_t *brd)
{
	uchar *addr;
	u16 word;

	if (!brd || (brd->magic != DGAP_BOARD_MAGIC) || !brd->re_map_membase)
		return;

	addr = brd->re_map_membase;
	word = readw(addr + POSTAREA);

	/* Check to see if BIOS thinks board is good. (GD). */
	if (word == *(u16 *) "GD") {
		DPR_INIT(("GOT GD in memory, moving states.\n"));
		brd->state = FINISHED_BIOS_LOAD;
		return;
	}

	/* Give up on board after too long of time taken */
	if (brd->wait_for_bios++ > 5000) {
		u16 err1 = readw(addr + SEQUENCE);
		u16 err2 = readw(addr + ERROR);
		APR(("***WARNING*** %s failed diagnostics.  Error #(%x,%x).\n",
			brd->name, err1, err2));
		brd->state = BOARD_FAILED;
		brd->dpastatus = BD_NOFEP;
	}
}


/*
 * Copies the FEP code from the user to the board,
 * and starts the FEP running.
 */
void dgap_do_fep_load(struct board_t *brd, uchar __user *ufep, int len)
{
	uchar *addr;
	uint offset;

	if (!brd || (brd->magic != DGAP_BOARD_MAGIC) || !brd->re_map_membase)
		return;

	addr = brd->re_map_membase;

	DPR_INIT(("dgap_do_fep_load() for board %s : start\n", brd->name));

	/*
	 * Download FEP
	 */
	offset = 0x1000;
	if (dgap_usertoboard(brd, addr + offset, ufep, len) == -1 ) {
		brd->state = BOARD_FAILED;
		brd->dpastatus = BD_NOFEP;
		return;
	}

	/*
	 * If board is a concentrator product, we need to give
	 * it its config string describing how the concentrators look.
	 */
	if ((brd->type == PCX) || (brd->type == PEPC)) {
		uchar string[100];
		uchar *config, *xconfig;
		int i = 0;

		xconfig = dgap_create_config_string(brd, string);

		/* Write string to board memory */
		config = addr + CONFIG;
		for (; i < CONFIGSIZE; i++, config++, xconfig++) {
			writeb(*xconfig, config);
			if ((*xconfig & 0xff) == 0xff)
				break;
		}
	}

	writel(0xbfc01004, (addr + 0xc34));
	writel(0x3, (addr + 0xc30));

	/* change states. */
	brd->state = WAIT_FEP_LOAD;

	DPR_INIT(("dgap_do_fep_load() for board %s : finish\n", brd->name));

}


/*
 * Waits for the FEP to report thats its ready for us to use.
 */
static void dgap_do_wait_for_fep(struct board_t *brd)
{
	uchar *addr;
	u16 word;

	if (!brd || (brd->magic != DGAP_BOARD_MAGIC) || !brd->re_map_membase)
		return;

	addr = brd->re_map_membase;

	DPR_INIT(("dgap_do_wait_for_fep() for board %s : start. addr: %p\n", brd->name, addr));

	word = readw(addr + FEPSTAT);

	/* Check to see if FEP is up and running now. */
	if (word == *(u16 *) "OS") {
		DPR_INIT(("GOT OS in memory for board %s, moving states.\n", brd->name));
		brd->state = FINISHED_FEP_LOAD;

		/*
		 * Check to see if the board can support FEP5+ commands.
		 */
		word = readw(addr + FEP5_PLUS);
		if (word == *(u16 *) "5A") {
			DPR_INIT(("GOT 5A in memory for board %s, board supports extended FEP5 commands.\n", brd->name));
			brd->bd_flags |= BD_FEP5PLUS;
		}

		return;
	}

	/* Give up on board after too long of time taken */
	if (brd->wait_for_fep++ > 5000) {
		u16 err1 = readw(addr + SEQUENCE);
		u16 err2 = readw(addr + ERROR);
		APR(("***WARNING*** FEPOS for %s not functioning.  Error #(%x,%x).\n",
			brd->name, err1, err2));
		brd->state = BOARD_FAILED;
		brd->dpastatus = BD_NOFEP;
	}

	DPR_INIT(("dgap_do_wait_for_fep() for board %s : finish\n", brd->name));
}


/*
 * Physically forces the FEP5 card to reset itself.
 */
static void dgap_do_reset_board(struct board_t *brd)
{
	uchar check;
	u32 check1;
	u32 check2;
	int i = 0;

	if (!brd || (brd->magic != DGAP_BOARD_MAGIC) || !brd->re_map_membase || !brd->re_map_port) {
		DPR_INIT(("dgap_do_reset_board() start. bad values. brd: %p mem: %p io: %p\n", 
			brd, brd ? brd->re_map_membase : 0, brd ? brd->re_map_port : 0));
		return;
	}

	DPR_INIT(("dgap_do_reset_board() start. io: %p\n", brd->re_map_port));

	/* FEPRST does not vary among supported boards */
	writeb(FEPRST, brd->re_map_port);

	for (i = 0; i <= 1000; i++) {
		check = readb(brd->re_map_port) & 0xe;
		if (check == FEPRST)
			break;
		udelay(10);

	}
	if (i > 1000) {
		APR(("*** WARNING *** Board not resetting...  Failing board.\n"));
		brd->state = BOARD_FAILED;
		brd->dpastatus = BD_NOFEP;
		goto failed;
	}

	/*
	 * Make sure there really is memory out there.
	 */
	writel(0xa55a3cc3, (brd->re_map_membase + LOWMEM));
	writel(0x5aa5c33c, (brd->re_map_membase + HIGHMEM));
	check1 = readl(brd->re_map_membase + LOWMEM);
	check2 = readl(brd->re_map_membase + HIGHMEM);

	if ((check1 != 0xa55a3cc3) || (check2 != 0x5aa5c33c)) {
		APR(("*** Warning *** No memory at %p for board.\n", brd->re_map_membase));
		brd->state = BOARD_FAILED;
		brd->dpastatus = BD_NOFEP;
		goto failed;
	}

	if (brd->state != BOARD_FAILED)
		brd->state = FINISHED_RESET;

failed:
	DPR_INIT(("dgap_do_reset_board() finish\n"));
}


/*
 * Sends a concentrator image into the FEP5 board.
 */
void dgap_do_conc_load(struct board_t *brd, uchar *uaddr, int len)
{
	char *vaddr;
	u16 offset = 0;
	struct downld_t *to_dp;

	if (!brd || (brd->magic != DGAP_BOARD_MAGIC) || !brd->re_map_membase)
		return;

	vaddr = brd->re_map_membase;

	offset = readw((u16 *) (vaddr + DOWNREQ));
	to_dp = (struct downld_t *) (vaddr + (int) offset);

	/*
	 * The image was already read into kernel space,
	 * we do NOT need a user space read here
	 */
	memcpy_toio((char *) to_dp, uaddr, sizeof(struct downld_t));

	/* Tell card we have data for it */
	writew(0, vaddr + (DOWNREQ));

	brd->conc_dl_status = NO_PENDING_CONCENTRATOR_REQUESTS;
}


#define EXPANSION_ROM_SIZE	(64 * 1024)
#define FEP5_ROM_MAGIC		(0xFEFFFFFF)

static void dgap_get_vpd(struct board_t *brd)
{
	u32 magic;
	u32 base_offset;
	u16 rom_offset;
	u16 vpd_offset;
	u16 image_length;
	u16 i;
	uchar byte1;
	uchar byte2;

	/*
	 * Poke the magic number at the PCI Rom Address location.
	 * If VPD is supported, the value read from that address
	 * will be non-zero.
	 */
	magic = FEP5_ROM_MAGIC;
	pci_write_config_dword(brd->pdev, PCI_ROM_ADDRESS, magic);
	pci_read_config_dword(brd->pdev, PCI_ROM_ADDRESS, &magic);

	/* VPD not supported, bail */
	if (!magic)
		return;

	/*
	 * To get to the OTPROM memory, we have to send the boards base
         * address or'ed with 1 into the PCI Rom Address location.
	 */
	magic = brd->membase | 0x01;
	pci_write_config_dword(brd->pdev, PCI_ROM_ADDRESS, magic);
	pci_read_config_dword(brd->pdev, PCI_ROM_ADDRESS, &magic);

	byte1 = readb(brd->re_map_membase);
	byte2 = readb(brd->re_map_membase + 1);

	/*
	 * If the board correctly swapped to the OTPROM memory,
	 * the first 2 bytes (header) should be 0x55, 0xAA
	 */
	if (byte1 == 0x55 && byte2 == 0xAA) {

		base_offset = 0;

		/*
		 * We have to run through all the OTPROM memory looking
		 * for the VPD offset.
		 */
		while (base_offset <= EXPANSION_ROM_SIZE) {
                
			/*
			 * Lots of magic numbers here.
			 *
			 * The VPD offset is located inside the ROM Data Structure.
			 * We also have to remember the length of each
			 * ROM Data Structure, so we can "hop" to the next
			 * entry if the VPD isn't in the current
			 * ROM Data Structure.
			 */
			rom_offset = readw(brd->re_map_membase + base_offset + 0x18);
			image_length = readw(brd->re_map_membase + rom_offset + 0x10) * 512;
			vpd_offset = readw(brd->re_map_membase + rom_offset + 0x08);

			/* Found the VPD entry */
			if (vpd_offset)
				break;

			/* We didn't find a VPD entry, go to next ROM entry. */
			base_offset += image_length;

			byte1 = readb(brd->re_map_membase + base_offset);
			byte2 = readb(brd->re_map_membase + base_offset + 1);

			/*
			 * If the new ROM offset doesn't have 0x55, 0xAA
			 * as its header, we have run out of ROM.
			 */
			if (byte1 != 0x55 || byte2 != 0xAA)
				break;
		}

		/*
		 * If we have a VPD offset, then mark the board
		 * as having a valid VPD, and copy VPDSIZE (512) bytes of
		 * that VPD to the buffer we have in our board structure.
		 */
		if (vpd_offset) {
			brd->bd_flags |= BD_HAS_VPD;
			for (i = 0; i < VPDSIZE; i++)
				brd->vpd[i] = readb(brd->re_map_membase + vpd_offset + i);
		}
	}

	/*
	 * We MUST poke the magic number at the PCI Rom Address location again.
	 * This makes the card report the regular board memory back to us,
	 * rather than the OTPROM memory.
	 */
	magic = FEP5_ROM_MAGIC;
	pci_write_config_dword(brd->pdev, PCI_ROM_ADDRESS, magic);
}


/*
 * Our board poller function.
 */
void dgap_poll_tasklet(unsigned long data)
{
        struct board_t *bd = (struct board_t *) data;
	ulong  lock_flags;
	ulong  lock_flags2;
	char *vaddr;
	u16 head, tail;
	u16 *chk_addr;
	u16 check = 0;

	if (!bd || (bd->magic != DGAP_BOARD_MAGIC)) {
		APR(("dgap_poll_tasklet() - NULL or bad bd.\n"));
		return;
	}

	if (bd->inhibit_poller)
		return;

	DGAP_LOCK(bd->bd_lock, lock_flags);

	vaddr = bd->re_map_membase;

	/*
	 * If board is ready, parse deeper to see if there is anything to do.
	 */
	if (bd->state == BOARD_READY) {

		struct ev_t *eaddr = NULL;

		if (!bd->re_map_membase) {
			DGAP_UNLOCK(bd->bd_lock, lock_flags);
			return;
		}
		if (!bd->re_map_port) {
			DGAP_UNLOCK(bd->bd_lock, lock_flags);
			return;
		}

		if (!bd->nasync) {
			goto out;
		}

		/*
		 * If this is a CX or EPCX, we need to see if the firmware
		 * is requesting a concentrator image from us.
		 */
		if ((bd->type == PCX) || (bd->type == PEPC)) {
			chk_addr = (u16 *) (vaddr + DOWNREQ);
			check = readw(chk_addr);
			/* Nonzero if FEP is requesting concentrator image. */
			if (check) {
				if (bd->conc_dl_status == NO_PENDING_CONCENTRATOR_REQUESTS)
					bd->conc_dl_status = NEED_CONCENTRATOR;
				/*
				 * Signal downloader, its got some work to do.
				 */
				DGAP_LOCK(dgap_dl_lock, lock_flags2);
				if (dgap_dl_action != 1) {
					dgap_dl_action = 1;
					wake_up_interruptible(&dgap_dl_wait);
				}
				DGAP_UNLOCK(dgap_dl_lock, lock_flags2);

			}
		}

		eaddr = (struct ev_t *) (vaddr + EVBUF);

		/* Get our head and tail */
		head = readw(&(eaddr->ev_head));
		tail = readw(&(eaddr->ev_tail));

		/*
		 * If there is an event pending. Go service it.
		 */
		if (head != tail) {
			DGAP_UNLOCK(bd->bd_lock, lock_flags);
			dgap_event(bd);
			DGAP_LOCK(bd->bd_lock, lock_flags);
		}

out:
		/*
		 * If board is doing interrupts, ACK the interrupt.
		 */
		if (bd && bd->intr_running) {
			readb(bd->re_map_port + 2);
		}

		DGAP_UNLOCK(bd->bd_lock, lock_flags);
		return;
	}

	/* Our state machine to get the board up and running */

	/* Reset board */
	if (bd->state == NEED_RESET) {

		/* Get VPD info */
		dgap_get_vpd(bd);

		dgap_do_reset_board(bd);
	}

	/* Move to next state */
	if (bd->state == FINISHED_RESET) {
		bd->state = NEED_CONFIG;
	}

	if (bd->state == NEED_CONFIG) {
		/*
		 * Match this board to a config the user created for us.
		 */
		bd->bd_config = dgap_find_config(bd->type, bd->pci_bus, bd->pci_slot);

		/*
		 * Because the 4 port Xr products share the same PCI ID
		 * as the 8 port Xr products, if we receive a NULL config
		 * back, and this is a PAPORT8 board, retry with a
		 * PAPORT4 attempt as well.
		 */
		if (bd->type == PAPORT8 && !bd->bd_config) {
			bd->bd_config = dgap_find_config(PAPORT4, bd->pci_bus, bd->pci_slot);
		}

		/*
		 * Register the ttys (if any) into the kernel.
		 */
		if (bd->bd_config) {
			bd->state = FINISHED_CONFIG;
		}
		else {
			bd->state = CONFIG_NOT_FOUND;
		}
	}

	/* Move to next state */
	if (bd->state == FINISHED_CONFIG) {
		bd->state = NEED_DEVICE_CREATION;
	}

	/* Move to next state */
	if (bd->state == NEED_DEVICE_CREATION) {
		/*
		 * Signal downloader, its got some work to do.
		 */
		DGAP_LOCK(dgap_dl_lock, lock_flags2);
		if (dgap_dl_action != 1) {
			dgap_dl_action = 1;
			wake_up_interruptible(&dgap_dl_wait);
		}
		DGAP_UNLOCK(dgap_dl_lock, lock_flags2);
	}

	/* Move to next state */
	if (bd->state == FINISHED_DEVICE_CREATION) {
		bd->state = NEED_BIOS_LOAD;
	}

	/* Move to next state */
	if (bd->state == NEED_BIOS_LOAD) {
		/*
		 * Signal downloader, its got some work to do.
		 */
		DGAP_LOCK(dgap_dl_lock, lock_flags2);
		if (dgap_dl_action != 1) {
			dgap_dl_action = 1;
			wake_up_interruptible(&dgap_dl_wait);
		}
		DGAP_UNLOCK(dgap_dl_lock, lock_flags2);
	}

	/* Wait for BIOS to test board... */
	if (bd->state == WAIT_BIOS_LOAD) {
		dgap_do_wait_for_bios(bd);
	}

	/* Move to next state */
	if (bd->state == FINISHED_BIOS_LOAD) {
		bd->state = NEED_FEP_LOAD;

		/*
		 * Signal downloader, its got some work to do.
		 */
		DGAP_LOCK(dgap_dl_lock, lock_flags2);
		if (dgap_dl_action != 1) {
			dgap_dl_action = 1;
			wake_up_interruptible(&dgap_dl_wait);
		}
		DGAP_UNLOCK(dgap_dl_lock, lock_flags2);
	}

	/* Wait for FEP to load on board... */
	if (bd->state == WAIT_FEP_LOAD) {
		dgap_do_wait_for_fep(bd);
	}


	/* Move to next state */
	if (bd->state == FINISHED_FEP_LOAD) {

		/*
		 * Do tty device initialization.
		 */
		int rc = dgap_tty_init(bd);

		if (rc < 0) {
			dgap_tty_uninit(bd);
			APR(("Can't init tty devices (%d)\n", rc));
			bd->state = BOARD_FAILED;
			bd->dpastatus = BD_NOFEP;
		}
		else {
			bd->state = NEED_PROC_CREATION;

			/*
			 * Signal downloader, its got some work to do.
			 */
			DGAP_LOCK(dgap_dl_lock, lock_flags2);
			if (dgap_dl_action != 1) {
				dgap_dl_action = 1;
				wake_up_interruptible(&dgap_dl_wait);
			}
			DGAP_UNLOCK(dgap_dl_lock, lock_flags2);
		}
	}

	/* Move to next state */
	if (bd->state == FINISHED_PROC_CREATION) {

		bd->state = BOARD_READY;
		bd->dpastatus = BD_RUNNING;

		/*
		 * If user requested the board to run in interrupt mode,
		 * go and set it up on the board.
		 */
		if (bd->intr_used) {
			writew(1, (bd->re_map_membase + ENABLE_INTR));
			/*
			 * Tell the board to poll the UARTS as fast as possible.
			 */
			writew(FEPPOLL_MIN, (bd->re_map_membase + FEPPOLL));
			bd->intr_running = 1;
		}

		/* Wake up anyone waiting for board state to change to ready */
		wake_up_interruptible(&bd->state_wait);
	}

	DGAP_UNLOCK(bd->bd_lock, lock_flags);
}


/*=======================================================================
 *
 *      dgap_cmdb - Sends a 2 byte command to the FEP.
 *
 *              ch      - Pointer to channel structure.
 *              cmd     - Command to be sent.
 *              byte1   - Integer containing first byte to be sent.
 *              byte2   - Integer containing second byte to be sent.
 *              ncmds   - Wait until ncmds or fewer cmds are left
 *                        in the cmd buffer before returning.
 *
 *=======================================================================*/
void dgap_cmdb(struct channel_t *ch, uchar cmd, uchar byte1, uchar byte2, uint ncmds)
{                       
	char		*vaddr = NULL;
	struct cm_t	*cm_addr = NULL;
	uint		count;
	uint		n;
	u16		head;
        u16		tail;

	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return;

	/*
	 * Check if board is still alive.
	 */
	if (ch->ch_bd->state == BOARD_FAILED) {
		DPR_CORE(("%s:%d board is in failed state.\n", __FILE__, __LINE__));
		return;
        }               

	/*
	 * Make sure the pointers are in range before
	 * writing to the FEP memory.
	 */
	vaddr = ch->ch_bd->re_map_membase;

	if (!vaddr)
		return;

	cm_addr = (struct cm_t *) (vaddr + CMDBUF);
	head = readw(&(cm_addr->cm_head));

	/* 
	 * Forget it if pointers out of range.
	 */
	if (head >= (CMDMAX - CMDSTART) || (head & 03)) {
		DPR_CORE(("%s:%d pointers out of range, failing board!\n", __FILE__, __LINE__));
		ch->ch_bd->state = BOARD_FAILED;
		return; 
	}

	/*
	 * Put the data in the circular command buffer.
	 */
	writeb(cmd, (char *) (vaddr + head + CMDSTART + 0));
	writeb((uchar) ch->ch_portnum, (char *) (vaddr + head + CMDSTART + 1));
	writeb(byte1, (char *) (vaddr + head + CMDSTART + 2));
	writeb(byte2, (char *) (vaddr + head + CMDSTART + 3));

	head = (head + 4) & (CMDMAX - CMDSTART - 4);

	writew(head, &(cm_addr->cm_head));

	/*
	 * Wait if necessary before updating the head  
	 * pointer to limit the number of outstanding
	 * commands to the FEP.   If the time spent waiting
	 * is outlandish, declare the FEP dead.
	 */
	for (count = dgap_count ;;) {

		head = readw(&(cm_addr->cm_head));
		tail = readw(&(cm_addr->cm_tail));

		n = (head - tail) & (CMDMAX - CMDSTART - 4);

		if (n <= ncmds * sizeof(struct cm_t))
			break;

		if (--count == 0) {
			DPR_CORE(("%s:%d failing board.\n",__FILE__, __LINE__));
			ch->ch_bd->state = BOARD_FAILED;
			return;
		}
		udelay(10);
	}  
}


/*=======================================================================
 *
 *      dgap_cmdw - Sends a 1 word command to the FEP.
 *      
 *              ch      - Pointer to channel structure.
 *              cmd     - Command to be sent.
 *              word    - Integer containing word to be sent.
 *              ncmds   - Wait until ncmds or fewer cmds are left
 *                        in the cmd buffer before returning.
 *
 *=======================================================================*/
void dgap_cmdw(struct channel_t *ch, uchar cmd, u16 word, uint ncmds)
{
	char		*vaddr = NULL;
	struct cm_t	*cm_addr = NULL;
	uint		count;
	uint		n;
	u16		head;
	u16		tail;

	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return;

	/*
	 * Check if board is still alive.
	 */
	if (ch->ch_bd->state == BOARD_FAILED) {
		DPR_CORE(("%s:%d board is failed!\n", __FILE__, __LINE__));
		return;
	}

	/*
	 * Make sure the pointers are in range before
	 * writing to the FEP memory.
	 */
	vaddr = ch->ch_bd->re_map_membase;
	if (!vaddr)
		return;

	cm_addr = (struct cm_t *) (vaddr + CMDBUF);
	head = readw(&(cm_addr->cm_head));

	/* 
	 * Forget it if pointers out of range.
	 */
	if (head >= (CMDMAX - CMDSTART) || (head & 03)) {
		DPR_CORE(("%s:%d Pointers out of range.  Failing board.\n",__FILE__, __LINE__));
		ch->ch_bd->state = BOARD_FAILED;
		return;
	}

	/*
	 * Put the data in the circular command buffer.
	 */
	writeb(cmd, (char *) (vaddr + head + CMDSTART + 0));
	writeb((uchar) ch->ch_portnum, (char *) (vaddr + head + CMDSTART + 1));
	writew((u16) word, (char *) (vaddr + head + CMDSTART + 2));

	head = (head + 4) & (CMDMAX - CMDSTART - 4);

	writew(head, &(cm_addr->cm_head));

	/*
	 * Wait if necessary before updating the head
	 * pointer to limit the number of outstanding  
	 * commands to the FEP.   If the time spent waiting
	 * is outlandish, declare the FEP dead.
	 */
	for (count = dgap_count ;;) {

		head = readw(&(cm_addr->cm_head));
		tail = readw(&(cm_addr->cm_tail));

		n = (head - tail) & (CMDMAX - CMDSTART - 4);

		if (n <= ncmds * sizeof(struct cm_t))
			break;

		if (--count == 0) {
			DPR_CORE(("%s:%d Failing board.\n",__FILE__, __LINE__));
			ch->ch_bd->state = BOARD_FAILED;
			return;
		}
		udelay(10);
	}  
}



/*=======================================================================
 *
 *      dgap_cmdw_ext - Sends a extended word command to the FEP.
 *      
 *              ch      - Pointer to channel structure.
 *              cmd     - Command to be sent.
 *              word    - Integer containing word to be sent.
 *              ncmds   - Wait until ncmds or fewer cmds are left
 *                        in the cmd buffer before returning.
 *
 *=======================================================================*/
static void dgap_cmdw_ext(struct channel_t *ch, u16 cmd, u16 word, uint ncmds)
{
	char		*vaddr = NULL;
	struct cm_t	*cm_addr = NULL;
	uint		count;
	uint		n;
	u16		head;
	u16		tail;

	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return;

	/*
	 * Check if board is still alive.
	 */
	if (ch->ch_bd->state == BOARD_FAILED) {
		DPR_CORE(("%s:%d board is failed!\n", __FILE__, __LINE__));
		return;
	}

	/*
	 * Make sure the pointers are in range before
	 * writing to the FEP memory.
	 */
	vaddr = ch->ch_bd->re_map_membase;
	if (!vaddr)
		return;

	cm_addr = (struct cm_t *) (vaddr + CMDBUF);
	head = readw(&(cm_addr->cm_head));

	/* 
	 * Forget it if pointers out of range.
	 */
	if (head >= (CMDMAX - CMDSTART) || (head & 03)) {
		DPR_CORE(("%s:%d Pointers out of range.  Failing board.\n",__FILE__, __LINE__));
		ch->ch_bd->state = BOARD_FAILED;
		return;
	}

	/*
	 * Put the data in the circular command buffer.
	 */

	/* Write an FF to tell the FEP that we want an extended command */
	writeb((uchar) 0xff, (char *) (vaddr + head + CMDSTART + 0));

	writeb((uchar) ch->ch_portnum, (uchar *) (vaddr + head + CMDSTART + 1));
	writew((u16) cmd, (char *) (vaddr + head + CMDSTART + 2));

	/*
	 * If the second part of the command won't fit,
	 * put it at the beginning of the circular buffer.
	 */
	if (((head + 4) >= ((CMDMAX - CMDSTART)) || (head & 03))) {
		writew((u16) word, (char *) (vaddr + CMDSTART));
	} else {
		writew((u16) word, (char *) (vaddr + head + CMDSTART + 4));
	}

	head = (head + 8) & (CMDMAX - CMDSTART - 4);

	writew(head, &(cm_addr->cm_head));

	/*
	 * Wait if necessary before updating the head
	 * pointer to limit the number of outstanding  
	 * commands to the FEP.   If the time spent waiting
	 * is outlandish, declare the FEP dead.
	 */
	for (count = dgap_count ;;) {

		head = readw(&(cm_addr->cm_head));
		tail = readw(&(cm_addr->cm_tail));

		n = (head - tail) & (CMDMAX - CMDSTART - 4);

		if (n <= ncmds * sizeof(struct cm_t))
			break;

		if (--count == 0) {
			DPR_CORE(("%s:%d Failing board.\n",__FILE__, __LINE__));
			ch->ch_bd->state = BOARD_FAILED;
			return;
		}
		udelay(10);
	}  
}


/*=======================================================================
 *
 *      dgap_wmove - Write data to FEP buffer.
 *
 *              ch      - Pointer to channel structure.
 *              buf     - Poiter to characters to be moved.
 *              cnt     - Number of characters to move.
 *
 *=======================================================================*/
void dgap_wmove(struct channel_t *ch, char *buf, uint cnt)
{
	int    n;
	char   *taddr;
	struct bs_t    *bs;
	u16    head;

	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return;
 
	/*
	 * Check parameters.
	 */
	bs   = ch->ch_bs;
	head = readw(&(bs->tx_head));

	/*
	 * If pointers are out of range, just return.
	 */
	if ((cnt > ch->ch_tsize) || (unsigned)(head - ch->ch_tstart) >= ch->ch_tsize) {
		DPR_CORE(("%s:%d pointer out of range", __FILE__, __LINE__));
		return;
	}

	/*
	 * If the write wraps over the top of the circular buffer,
	 * move the portion up to the wrap point, and reset the
	 * pointers to the bottom.
	 */
	n = ch->ch_tstart + ch->ch_tsize - head;

	if (cnt >= n) {
		cnt -= n;
		taddr = ch->ch_taddr + head;
		memcpy_toio(taddr, buf, n);
		head = ch->ch_tstart;
		buf += n;
	}

	/*
	 * Move rest of data.
	 */
	taddr = ch->ch_taddr + head;
	n = cnt;
	memcpy_toio(taddr, buf, n);
	head += cnt;

	writew(head, &(bs->tx_head));
}

/*
 * Retrives the current custom baud rate from FEP memory,
 * and returns it back to the user.
 * Returns 0 on error.
 */
uint dgap_get_custom_baud(struct channel_t *ch)
{
	uchar *vaddr;
	ulong offset = 0;
	uint value = 0;

	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC) {
		return (0);
	}

	if (!ch->ch_bd || ch->ch_bd->magic != DGAP_BOARD_MAGIC) {
		return (0);
	}

	if (!(ch->ch_bd->bd_flags & BD_FEP5PLUS))
		return (0);

	vaddr = ch->ch_bd->re_map_membase;

	if (!vaddr)
		return (0);

	/*
	 * Go get from fep mem, what the fep
	 * believes the custom baud rate is. 
	 */
	offset = ((((*(unsigned short *)(vaddr + ECS_SEG)) << 4) +  
		(ch->ch_portnum * 0x28) + LINE_SPEED));

	value = readw(vaddr + offset);
	return (value);
}


/*
 * Calls the firmware to reset this channel.
 */
void dgap_firmware_reset_port(struct channel_t *ch)
{
	dgap_cmdb(ch, CHRESET, 0, 0, 0);

	/*
	 * Now that the channel is reset, we need to make sure
	 * all the current settings get reapplied to the port
	 * in the firmware.
	 *
	 * So we will set the driver's cache of firmware
	 * settings all to 0, and then call param.
	 */
	ch->ch_fepiflag = 0;
	ch->ch_fepcflag = 0;
	ch->ch_fepoflag = 0;
	ch->ch_fepstartc = 0;
	ch->ch_fepstopc = 0;
	ch->ch_fepastartc = 0;
	ch->ch_fepastopc = 0;
	ch->ch_mostat = 0;
	ch->ch_hflow = 0;
}


/*=======================================================================
 *      
 *      dgap_param - Set Digi parameters.
 *
 *              struct tty_struct *     - TTY for port.
 *
 *=======================================================================*/
int dgap_param(struct tty_struct *tty)
{
	struct ktermios *ts;
	struct board_t *bd;
	struct channel_t *ch;
	struct bs_t   *bs;
	struct un_t   *un;
	u16	head;
	u16	cflag;
	u16	iflag;
	uchar	mval;
	uchar	hflow;

	if (!tty || tty->magic != TTY_MAGIC) {
		return (-ENXIO);
	}

	un = (struct un_t *) tty->driver_data;
	if (!un || un->magic != DGAP_UNIT_MAGIC) {
		return (-ENXIO);
	}

	ch = un->un_ch;
	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC) {
		return (-ENXIO);
	}

	bd = ch->ch_bd;
	if (!bd || bd->magic != DGAP_BOARD_MAGIC) {
		return (-ENXIO);
	}

        bs = ch->ch_bs;
	if (bs == 0) {
		return (-ENXIO);
	}

	DPR_PARAM(("param start: tdev: %x cflags: %x oflags: %x iflags: %x\n",
		ch->ch_tun.un_dev, ch->ch_c_cflag, ch->ch_c_oflag, ch->ch_c_iflag));

	ts = &tty->termios;

	/*
	 * If baud rate is zero, flush queues, and set mval to drop DTR.
	 */
	if ((ch->ch_c_cflag & (CBAUD)) == 0) {

		/* flush rx */
		head = readw(&(ch->ch_bs->rx_head));
		writew(head, &(ch->ch_bs->rx_tail));

		/* flush tx */
		head = readw(&(ch->ch_bs->tx_head));
		writew(head, &(ch->ch_bs->tx_tail));

		ch->ch_flags |= (CH_BAUD0);

		/* Drop RTS and DTR */
		ch->ch_mval &= ~(D_RTS(ch)|D_DTR(ch));
		mval = D_DTR(ch) | D_RTS(ch);
		ch->ch_baud_info = 0;

	} else if (ch->ch_custom_speed && (bd->bd_flags & BD_FEP5PLUS)) {
		/*
		 * Tell the fep to do the command
		 */

		DPR_PARAM(("param: Want %d speed\n", ch->ch_custom_speed));

		dgap_cmdw_ext(ch, 0xff01, ch->ch_custom_speed, 0);

		/*
		 * Now go get from fep mem, what the fep
		 * believes the custom baud rate is. 
		 */
		ch->ch_baud_info = ch->ch_custom_speed = dgap_get_custom_baud(ch);

		DPR_PARAM(("param: Got %d speed\n", ch->ch_custom_speed));

		/* Handle transition from B0 */   
		if (ch->ch_flags & CH_BAUD0) {
			ch->ch_flags &= ~(CH_BAUD0);
			ch->ch_mval |= (D_RTS(ch)|D_DTR(ch));
		}
		mval = D_DTR(ch) | D_RTS(ch);

	} else {
		/*
		 * Set baud rate, character size, and parity.
		 */


		int iindex = 0;
		int jindex = 0;
		int baud = 0;

		ulong bauds[4][16] = {
			{ /* slowbaud */
				0,	50,	75,	110,
				134,	150,	200,	300,
				600,	1200,	1800,	2400,
				4800,	9600,	19200,	38400 },
			{ /* slowbaud & CBAUDEX */
				0,	57600,	115200,	230400,
				460800,	150,	200,	921600,
				600,	1200,	1800,	2400,
				4800,	9600,	19200,	38400 },
			{ /* fastbaud */
				0,	57600,	76800,	115200,
				14400,	57600,	230400,	76800,
				115200,	230400,	28800,	460800,
				921600,	9600,	19200,	38400 },
			{ /* fastbaud & CBAUDEX */
				0,	57600,	115200,	230400,
				460800,	150,	200,	921600,
				600,	1200,	1800,	2400,
				4800,	9600,	19200,	38400 }
		};

		/* Only use the TXPrint baud rate if the terminal unit is NOT open */
		if (!(ch->ch_tun.un_flags & UN_ISOPEN) && (un->un_type == DGAP_PRINT))
			baud = C_BAUD(ch->ch_pun.un_tty) & 0xff;
		else
			baud = C_BAUD(ch->ch_tun.un_tty) & 0xff;

		if (ch->ch_c_cflag & CBAUDEX)
			iindex = 1;

		if (ch->ch_digi.digi_flags & DIGI_FAST)
			iindex += 2;

		jindex = baud;

		if ((iindex >= 0) && (iindex < 4) && (jindex >= 0) && (jindex < 16)) {
			baud = bauds[iindex][jindex];
		} else {
			DPR_IOCTL(("baud indices were out of range (%d)(%d)",
				iindex, jindex));
			baud = 0;
		}

		if (baud == 0)  
			baud = 9600;

		ch->ch_baud_info = baud;


		/*
		 * CBAUD has bit position 0x1000 set these days to indicate Linux
		 * baud rate remap.
		 * We use a different bit assignment for high speed.  Clear this
		 * bit out while grabbing the parts of "cflag" we want.
		 */
		cflag = ch->ch_c_cflag & ((CBAUD ^ CBAUDEX) | PARODD | PARENB | CSTOPB | CSIZE);

		/*
		 * HUPCL bit is used by FEP to indicate fast baud
		 * table is to be used.
		 */
		if ((ch->ch_digi.digi_flags & DIGI_FAST) || (ch->ch_c_cflag & CBAUDEX))
			cflag |= HUPCL;


		if ((ch->ch_c_cflag & CBAUDEX) && !(ch->ch_digi.digi_flags & DIGI_FAST)) {
		/*
		 * The below code is trying to guarantee that only baud rates
		 * 115200, 230400, 460800, 921600 are remapped.  We use exclusive or
		 * because the various baud rates share common bit positions
		 * and therefore can't be tested for easily.
		 */
			tcflag_t tcflag = (ch->ch_c_cflag & CBAUD) | CBAUDEX;
			int baudpart = 0;

			/* Map high speed requests to index into FEP's baud table */
			switch (tcflag) {
			case B57600 :
				baudpart = 1;
				break;
#ifdef B76800
			case B76800 :
				baudpart = 2;
				break;
#endif
			case B115200 :
				baudpart = 3;
				break;
			case B230400 :
				baudpart = 9;
				break;
			case B460800 :
				baudpart = 11;
				break;
#ifdef B921600
			case B921600 :
				baudpart = 12;
				break;
#endif
			default:
				baudpart = 0;
			}

			if (baudpart)
				cflag = (cflag & ~(CBAUD | CBAUDEX)) | baudpart;
		}

		cflag &= 0xffff;

		if (cflag != ch->ch_fepcflag) {
			ch->ch_fepcflag = (u16) (cflag & 0xffff);

			/* Okay to have channel and board locks held calling this */
			dgap_cmdw(ch, SCFLAG, (u16) cflag, 0);
		}

		/* Handle transition from B0 */   
		if (ch->ch_flags & CH_BAUD0) {
			ch->ch_flags &= ~(CH_BAUD0);
			ch->ch_mval |= (D_RTS(ch)|D_DTR(ch));
		}
		mval = D_DTR(ch) | D_RTS(ch);
	}

	/*
	 * Get input flags.
	 */
	iflag = ch->ch_c_iflag & (IGNBRK | BRKINT | IGNPAR | PARMRK | INPCK | ISTRIP | IXON | IXANY | IXOFF);

	if ((ch->ch_startc == _POSIX_VDISABLE) || (ch->ch_stopc == _POSIX_VDISABLE)) {
		iflag &= ~(IXON | IXOFF);
		ch->ch_c_iflag &= ~(IXON | IXOFF);
	}

	/*
	 * Only the IBM Xr card can switch between
	 * 232 and 422 modes on the fly
	 */
	if (bd->device == PCI_DEVICE_XR_IBM_DID) {
		if (ch->ch_digi.digi_flags & DIGI_422)
			dgap_cmdb(ch, SCOMMODE, MODE_422, 0, 0);
		else
			dgap_cmdb(ch, SCOMMODE, MODE_232, 0, 0);
	}

	if (ch->ch_digi.digi_flags & DIGI_ALTPIN)
		iflag |= IALTPIN ;

	if (iflag != ch->ch_fepiflag) {
		ch->ch_fepiflag = iflag;

		/* Okay to have channel and board locks held calling this */
		dgap_cmdw(ch, SIFLAG, (u16) ch->ch_fepiflag, 0);
	}

	/*
	 * Select hardware handshaking.
	 */
	hflow = 0;

	if (ch->ch_c_cflag & CRTSCTS) {
		hflow |= (D_RTS(ch) | D_CTS(ch));
	}
	if (ch->ch_digi.digi_flags & RTSPACE)
		hflow |= D_RTS(ch);
	if (ch->ch_digi.digi_flags & DTRPACE)
		hflow |= D_DTR(ch);  
	if (ch->ch_digi.digi_flags & CTSPACE)
		hflow |= D_CTS(ch);
	if (ch->ch_digi.digi_flags & DSRPACE)
		hflow |= D_DSR(ch);
	if (ch->ch_digi.digi_flags & DCDPACE)
		hflow |= D_CD(ch);

	if (hflow != ch->ch_hflow) {
		ch->ch_hflow = hflow;

		/* Okay to have channel and board locks held calling this */
		dgap_cmdb(ch, SHFLOW, (uchar) hflow, 0xff, 0);
        }


	/*
	 * Set RTS and/or DTR Toggle if needed, but only if product is FEP5+ based.
	 */
	if (bd->bd_flags & BD_FEP5PLUS) {
		u16 hflow2 = 0;
		if (ch->ch_digi.digi_flags & DIGI_RTS_TOGGLE) {
			hflow2 |= (D_RTS(ch));
		}
		if (ch->ch_digi.digi_flags & DIGI_DTR_TOGGLE) {
			hflow2 |= (D_DTR(ch));
		}

		dgap_cmdw_ext(ch, 0xff03, hflow2, 0);
	}

	/*
	 * Set modem control lines.  
	 */

	mval ^= ch->ch_mforce & (mval ^ ch->ch_mval);

	DPR_PARAM(("dgap_param: mval: %x ch_mforce: %x ch_mval: %x ch_mostat: %x\n",
		mval, ch->ch_mforce, ch->ch_mval, ch->ch_mostat));

	if (ch->ch_mostat ^ mval) {
		ch->ch_mostat = mval;

		/* Okay to have channel and board locks held calling this */
		DPR_PARAM(("dgap_param: Sending SMODEM mval: %x\n", mval));
		dgap_cmdb(ch, SMODEM, (uchar) mval, D_RTS(ch)|D_DTR(ch), 0);
	}

	/*
	 * Read modem signals, and then call carrier function.             
	 */
	ch->ch_mistat = readb(&(bs->m_stat));
	dgap_carrier(ch);

	/*      
	 * Set the start and stop characters.
	 */
	if (ch->ch_startc != ch->ch_fepstartc || ch->ch_stopc != ch->ch_fepstopc) {
		ch->ch_fepstartc = ch->ch_startc;
		ch->ch_fepstopc =  ch->ch_stopc;

		/* Okay to have channel and board locks held calling this */
		dgap_cmdb(ch, SFLOWC, ch->ch_fepstartc, ch->ch_fepstopc, 0);
	}

	/*
	 * Set the Auxiliary start and stop characters.
	 */     
	if (ch->ch_astartc != ch->ch_fepastartc || ch->ch_astopc != ch->ch_fepastopc) {
		ch->ch_fepastartc = ch->ch_astartc;
		ch->ch_fepastopc = ch->ch_astopc;

		/* Okay to have channel and board locks held calling this */
		dgap_cmdb(ch, SAFLOWC, ch->ch_fepastartc, ch->ch_fepastopc, 0);
	}

	DPR_PARAM(("param finish\n"));

	return (0);
}


/*
 * dgap_parity_scan()
 *
 * Convert the FEP5 way of reporting parity errors and breaks into
 * the Linux line discipline way.
 */
void dgap_parity_scan(struct channel_t *ch, unsigned char *cbuf, unsigned char *fbuf, int *len)
{
	int l = *len;
	int count = 0;
	unsigned char *in, *cout, *fout;
	unsigned char c;

	in = cbuf;
	cout = cbuf;
	fout = fbuf;

	DPR_PSCAN(("dgap_parity_scan start\n"));

	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return;

	while (l--) {
		c = *in++;
		switch (ch->pscan_state) {
		default:
			/* reset to sanity and fall through */
			ch->pscan_state = 0;

		case 0:
			/* No FF seen yet */
			if (c == (unsigned char) '\377') {
				/* delete this character from stream */
				ch->pscan_state = 1;
			} else {
				*cout++ = c;
				*fout++ = TTY_NORMAL;
				count += 1;
			}
			break;

		case 1:
			/* first FF seen */
			if (c == (unsigned char) '\377') {
				/* doubled ff, transform to single ff */
				*cout++ = c;
				*fout++ = TTY_NORMAL;
				count += 1;
				ch->pscan_state = 0;
			} else {
				/* save value examination in next state */
				ch->pscan_savechar = c;
				ch->pscan_state = 2; 
			}
			break;

		case 2:
			/* third character of ff sequence */

			*cout++ = c;

			if (ch->pscan_savechar == 0x0) {

				if (c == 0x0) {
					DPR_PSCAN(("dgap_parity_scan in 3rd char of ff seq. c: %x setting break.\n", c));
					ch->ch_err_break++;
					*fout++ = TTY_BREAK;
				}
				else {
					DPR_PSCAN(("dgap_parity_scan in 3rd char of ff seq. c: %x setting parity.\n", c));
					ch->ch_err_parity++;
					*fout++ = TTY_PARITY;
				}
			}
			else {
				DPR_PSCAN(("%s:%d Logic Error.\n", __FILE__, __LINE__));
			}

			count += 1;
			ch->pscan_state = 0;
		}       
	}
	*len = count;
	DPR_PSCAN(("dgap_parity_scan finish\n"));
}




/*=======================================================================
 *
 *      dgap_event - FEP to host event processing routine.
 *
 *              bd     - Board of current event.
 *
 *=======================================================================*/
static int dgap_event(struct board_t *bd)
{
	struct channel_t *ch;
	ulong		lock_flags;
	ulong		lock_flags2;
	struct bs_t	*bs;
	uchar		*event;
	uchar		*vaddr = NULL;
	struct ev_t	*eaddr = NULL;
	uint		head;
	uint		tail;
	int		port;
	int		reason;
	int		modem;
	int		b1;

	if (!bd || bd->magic != DGAP_BOARD_MAGIC)
		return (-ENXIO);

	DGAP_LOCK(bd->bd_lock, lock_flags);

	vaddr = bd->re_map_membase;

	if (!vaddr) {
		DGAP_UNLOCK(bd->bd_lock, lock_flags);
		return (-ENXIO);
	}

	eaddr = (struct ev_t *) (vaddr + EVBUF);

	/* Get our head and tail */
	head = readw(&(eaddr->ev_head));
	tail = readw(&(eaddr->ev_tail));

	/*
	 * Forget it if pointers out of range.
	 */

	if (head >= EVMAX - EVSTART || tail >= EVMAX - EVSTART ||
	    (head | tail) & 03) {
		DPR_EVENT(("should be calling xxfail %d\n", __LINE__));
		/* Let go of board lock */
		DGAP_UNLOCK(bd->bd_lock, lock_flags);
		return (-ENXIO);
	}

	/*
	 * Loop to process all the events in the buffer.
	 */
	while (tail != head) {

		/*
		 * Get interrupt information.
		 */

		event = bd->re_map_membase + tail + EVSTART;

		port   = event[0];
		reason = event[1];
		modem  = event[2];
		b1     = event[3];

		DPR_EVENT(("event: jiffies: %ld port: %d reason: %x modem: %x\n",
			jiffies, port, reason, modem));

		/*
		 * Make sure the interrupt is valid.
		 */
                if ( port >= bd->nasync) {
			goto next;
		}

		if (!(reason & (IFMODEM | IFBREAK | IFTLW | IFTEM | IFDATA))) {
			goto next;
		}

		ch = bd->channels[port];

		if (!ch || ch->magic != DGAP_CHANNEL_MAGIC) {
			goto next;
		}

		/*
		 * If we have made it here, the event was valid.
		 * Lock down the channel.
		 */
		DGAP_LOCK(ch->ch_lock, lock_flags2);

		bs = ch->ch_bs;

		if (!bs) {
			DGAP_UNLOCK(ch->ch_lock, lock_flags2);
			goto next;
		}

		/*
		 * Process received data.
		 */
		if (reason & IFDATA) {

			/*
			 * ALL LOCKS *MUST* BE DROPPED BEFORE CALLING INPUT!
			 * input could send some data to ld, which in turn
			 * could do a callback to one of our other functions.
			 */
			DGAP_UNLOCK(ch->ch_lock, lock_flags2);
			DGAP_UNLOCK(bd->bd_lock, lock_flags);

			dgap_input(ch);

			DGAP_LOCK(bd->bd_lock, lock_flags);
			DGAP_LOCK(ch->ch_lock, lock_flags2);

			if (ch->ch_flags & CH_RACTIVE)
				ch->ch_flags |= CH_RENABLE;
			else
				writeb(1, &(bs->idata));

			if (ch->ch_flags & CH_RWAIT) {
				ch->ch_flags &= ~CH_RWAIT;

				wake_up_interruptible(&ch->ch_tun.un_flags_wait);
			}
		}

		/*
		 * Process Modem change signals. 
		 */
		if (reason & IFMODEM) {
			ch->ch_mistat = modem;
			dgap_carrier(ch);
		}

		/*
		 * Process break.
		 */
		if (reason & IFBREAK) {

			DPR_EVENT(("got IFBREAK\n"));

			if (ch->ch_tun.un_tty) {
				/* A break has been indicated */
				ch->ch_err_break++;
				tty_buffer_request_room(ch->ch_tun.un_tty->port, 1);
				tty_insert_flip_char(ch->ch_tun.un_tty->port, 0, TTY_BREAK);
				tty_flip_buffer_push(ch->ch_tun.un_tty->port);
			}
		}

		/*
		 * Process Transmit low.
		 */
		if (reason & IFTLW) {

			DPR_EVENT(("event: got low event\n"));

			if (ch->ch_tun.un_flags & UN_LOW) {
				ch->ch_tun.un_flags &= ~UN_LOW;

				if (ch->ch_tun.un_flags & UN_ISOPEN) {
					if ((ch->ch_tun.un_tty->flags & 
					   (1 << TTY_DO_WRITE_WAKEUP)) &&
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,31)
						ch->ch_tun.un_tty->ldisc->ops->write_wakeup)
#else
						ch->ch_tun.un_tty->ldisc.ops->write_wakeup)
#endif
					{
						DGAP_UNLOCK(ch->ch_lock, lock_flags2);
						DGAP_UNLOCK(bd->bd_lock, lock_flags);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,31)
						(ch->ch_tun.un_tty->ldisc->ops->write_wakeup)(ch->ch_tun.un_tty);
#else
						(ch->ch_tun.un_tty->ldisc.ops->write_wakeup)(ch->ch_tun.un_tty);
#endif
						DGAP_LOCK(bd->bd_lock, lock_flags);
						DGAP_LOCK(ch->ch_lock, lock_flags2);
					}
					wake_up_interruptible(&ch->ch_tun.un_tty->write_wait);
					wake_up_interruptible(&ch->ch_tun.un_flags_wait);

					DPR_EVENT(("event: Got low event. jiffies: %lu\n", jiffies));
				}
			}

			if (ch->ch_pun.un_flags & UN_LOW) {
				ch->ch_pun.un_flags &= ~UN_LOW;
				if (ch->ch_pun.un_flags & UN_ISOPEN) {
					if ((ch->ch_pun.un_tty->flags & 
					   (1 << TTY_DO_WRITE_WAKEUP)) &&
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,31)
						ch->ch_pun.un_tty->ldisc->ops->write_wakeup)
#else
						ch->ch_pun.un_tty->ldisc.ops->write_wakeup)
#endif
					{
						DGAP_UNLOCK(ch->ch_lock, lock_flags2);
						DGAP_UNLOCK(bd->bd_lock, lock_flags);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,31)
						(ch->ch_pun.un_tty->ldisc->ops->write_wakeup)(ch->ch_pun.un_tty);
#else
						(ch->ch_pun.un_tty->ldisc.ops->write_wakeup)(ch->ch_pun.un_tty);
#endif
						DGAP_LOCK(bd->bd_lock, lock_flags);
						DGAP_LOCK(ch->ch_lock, lock_flags2);
					}
					wake_up_interruptible(&ch->ch_pun.un_tty->write_wait);
					wake_up_interruptible(&ch->ch_pun.un_flags_wait);
				}
			}

			if (ch->ch_flags & CH_WLOW) {
				ch->ch_flags &= ~CH_WLOW;
				wake_up_interruptible(&ch->ch_flags_wait);
			}
		}

		/*
		 * Process Transmit empty.
		 */
		if (reason & IFTEM) {
			DPR_EVENT(("event: got empty event\n"));

			if (ch->ch_tun.un_flags & UN_EMPTY) {
				ch->ch_tun.un_flags &= ~UN_EMPTY;
				if (ch->ch_tun.un_flags & UN_ISOPEN) {
					if ((ch->ch_tun.un_tty->flags & 
					   (1 << TTY_DO_WRITE_WAKEUP)) &&
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,31)
						ch->ch_tun.un_tty->ldisc->ops->write_wakeup)
#else
						ch->ch_tun.un_tty->ldisc.ops->write_wakeup)
#endif
					{
						DGAP_UNLOCK(ch->ch_lock, lock_flags2);
						DGAP_UNLOCK(bd->bd_lock, lock_flags);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,31)
						(ch->ch_tun.un_tty->ldisc->ops->write_wakeup)(ch->ch_tun.un_tty);
#else
						(ch->ch_tun.un_tty->ldisc.ops->write_wakeup)(ch->ch_tun.un_tty);
#endif
						DGAP_LOCK(bd->bd_lock, lock_flags);
						DGAP_LOCK(ch->ch_lock, lock_flags2);
					}
					wake_up_interruptible(&ch->ch_tun.un_tty->write_wait);
					wake_up_interruptible(&ch->ch_tun.un_flags_wait);
				}
			}

			if (ch->ch_pun.un_flags & UN_EMPTY) {
				ch->ch_pun.un_flags &= ~UN_EMPTY;
				if (ch->ch_pun.un_flags & UN_ISOPEN) {
					if ((ch->ch_pun.un_tty->flags & 
					   (1 << TTY_DO_WRITE_WAKEUP)) &&
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,31)
						ch->ch_pun.un_tty->ldisc->ops->write_wakeup)
#else
						ch->ch_pun.un_tty->ldisc.ops->write_wakeup)
#endif
					{
						DGAP_UNLOCK(ch->ch_lock, lock_flags2);
						DGAP_UNLOCK(bd->bd_lock, lock_flags);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,31)
						(ch->ch_pun.un_tty->ldisc->ops->write_wakeup)(ch->ch_pun.un_tty);
#else
						(ch->ch_pun.un_tty->ldisc.ops->write_wakeup)(ch->ch_pun.un_tty);
#endif
						DGAP_LOCK(bd->bd_lock, lock_flags);
						DGAP_LOCK(ch->ch_lock, lock_flags2);
					}
					wake_up_interruptible(&ch->ch_pun.un_tty->write_wait);
					wake_up_interruptible(&ch->ch_pun.un_flags_wait);
				}
			}


			if (ch->ch_flags & CH_WEMPTY) {
				ch->ch_flags &= ~CH_WEMPTY;
				wake_up_interruptible(&ch->ch_flags_wait);
			}
		}

		DGAP_UNLOCK(ch->ch_lock, lock_flags2);

next:
		tail = (tail + 4) & (EVMAX - EVSTART - 4);
	}

	writew(tail, &(eaddr->ev_tail));
	DGAP_UNLOCK(bd->bd_lock, lock_flags);

	return (0);
}               
