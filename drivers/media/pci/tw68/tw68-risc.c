/*
 *  tw68_risc.c
 *  Part of the device driver for Techwell 68xx based cards
 *
 *  Much of this code is derived from the cx88 and sa7134 drivers, which
 *  were in turn derived from the bt87x driver.  The original work was by
 *  Gerd Knorr; more recently the code was enhanced by Mauro Carvalho Chehab,
 *  Hans Verkuil, Andy Walls and many others.  Their work is gratefully
 *  acknowledged.  Full credit goes to them - any problems within this code
 *  are mine.
 *
 *  Copyright (C) 2009  William M. Brack <wbrack@mmm.com.hk>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "tw68.h"

#define NO_SYNC_LINE (-1U)

/**
 *  @rp		pointer to current risc program position
 *  @sglist	pointer to "scatter-gather list" of buffer pointers
 *  @offset	offset to target memory buffer
 *  @sync_line	0 -> no sync, 1 -> odd sync, 2 -> even sync
 *  @bpl	number of bytes per scan line
 *  @padding	number of bytes of padding to add
 *  @lines	number of lines in field
 *  @lpi	lines per IRQ, or 0 to not generate irqs
 *		Note: IRQ to be generated _after_ lpi lines are transferred
 */
static __le32 *tw68_risc_field(__le32 *rp, struct scatterlist *sglist,
			    unsigned int offset, u32 sync_line,
			    unsigned int bpl, unsigned int padding,
			    unsigned int lines, unsigned int lpi)
{
	struct scatterlist *sg;
	unsigned int line, todo, done;

	/* sync instruction */
	if (sync_line != NO_SYNC_LINE) {
		if (sync_line == 1)
			*(rp++) = cpu_to_le32(RISC_SYNCO);
		else
			*(rp++) = cpu_to_le32(RISC_SYNCE);
		*(rp++) = 0;
	}
	/* scan lines */
	sg = sglist;
	for (line = 0; line < lines; line++) {
		/* calculate next starting position */
		while (offset && offset >= sg_dma_len(sg)) {
			offset -= sg_dma_len(sg);
			sg++;
		}
		if (bpl <= sg_dma_len(sg) - offset) {
			/* fits into current chunk */
			*(rp++) = cpu_to_le32(RISC_LINESTART |
					      /* (offset<<12) |*/  bpl);
			*(rp++) = cpu_to_le32(sg_dma_address(sg) + offset);
			offset += bpl;
		} else {
			/*
			 * scanline needs to be split.  Put the start in
			 * whatever memory remains using RISC_LINESTART,
			 * then the remainder into following addresses
			 * given by the scatter-gather list.
			 */
			todo = bpl;	/* one full line to be done */
			/* first fragment */
			done = (sg_dma_len(sg) - offset);
			*(rp++) = cpu_to_le32(RISC_LINESTART |
						(7 << 24) |
						done);
			*(rp++) = cpu_to_le32(sg_dma_address(sg) + offset);
			todo -= done;
			sg++;
			/* succeeding fragments have no offset */
			while (todo > sg_dma_len(sg)) {
				*(rp++) = cpu_to_le32(RISC_INLINE |
						(done << 12) |
						sg_dma_len(sg));
				*(rp++) = cpu_to_le32(sg_dma_address(sg));
				todo -= sg_dma_len(sg);
				sg++;
				done += sg_dma_len(sg);
			}
			if (todo) {
				/* final chunk - offset 0, count 'todo' */
				*(rp++) = cpu_to_le32(RISC_INLINE |
							(done << 12) |
							todo);
				*(rp++) = cpu_to_le32(sg_dma_address(sg));
			}
			offset = todo;
		}
		offset += padding;
		/* If this line needs an interrupt, put it in */
		if (lpi && line > 0 && !(line % lpi))
			*(rp-2) |= RISC_INT_BIT;
	}

	return rp;
}

/**
 * tw68_risc_buffer
 *
 * 	This routine is called by tw68-video.  It allocates
 * 	memory for the dma controller "program" and then fills in that
 * 	memory with the appropriate "instructions".
 *
 * 	@pci_dev	structure with info about the pci
 * 			slot which our device is in.
 * 	@risc		structure with info about the memory
 * 			used for our controller program.
 * 	@sglist		scatter-gather list entry
 * 	@top_offset	offset within the risc program area for the
 * 			first odd frame line
 * 	@bottom_offset	offset within the risc program area for the
 * 			first even frame line
 * 	@bpl		number of data bytes per scan line
 * 	@padding	number of extra bytes to add at end of line
 * 	@lines		number of scan lines
 */
int tw68_risc_buffer(struct pci_dev *pci,
			struct btcx_riscmem *risc,
			struct scatterlist *sglist,
			unsigned int top_offset,
			unsigned int bottom_offset,
			unsigned int bpl,
			unsigned int padding,
			unsigned int lines)
{
	u32 instructions, fields;
	__le32 *rp;
	int rc;

	fields = 0;
	if (UNSET != top_offset)
		fields++;
	if (UNSET != bottom_offset)
		fields++;
	/*
	 * estimate risc mem: worst case is one write per page border +
	 * one write per scan line + syncs + jump (all 2 dwords).
	 * Padding can cause next bpl to start close to a page border.
	 * First DMA region may be smaller than PAGE_SIZE
	 */
	instructions  = fields * (1 + (((bpl + padding) * lines) /
			 PAGE_SIZE) + lines) + 2;
	rc = btcx_riscmem_alloc(pci, risc, instructions * 8);
	if (rc < 0)
		return rc;

	/* write risc instructions */
	rp = risc->cpu;
	if (UNSET != top_offset)	/* generates SYNCO */
		rp = tw68_risc_field(rp, sglist, top_offset, 1,
				     bpl, padding, lines, 0);
	if (UNSET != bottom_offset)	/* generates SYNCE */
		rp = tw68_risc_field(rp, sglist, bottom_offset, 2,
				     bpl, padding, lines, 0);

	/* save pointer to jmp instruction address */
	risc->jmp = rp;
	/* assure risc buffer hasn't overflowed */
	BUG_ON((risc->jmp - risc->cpu + 2) * sizeof(*risc->cpu) > risc->size);
	return 0;
}

#if 0
/* ------------------------------------------------------------------ */
/* debug helper code                                                  */

static void tw68_risc_decode(u32 risc, u32 addr)
{
#define	RISC_OP(reg)	(((reg) >> 28) & 7)
	static struct instr_details {
		char *name;
		u8 has_data_type;
		u8 has_byte_info;
		u8 has_addr;
	} instr[8] = {
		[RISC_OP(RISC_SYNCO)]	  = {"syncOdd", 0, 0, 0},
		[RISC_OP(RISC_SYNCE)]	  = {"syncEven", 0, 0, 0},
		[RISC_OP(RISC_JUMP)]	  = {"jump", 0, 0, 1},
		[RISC_OP(RISC_LINESTART)] = {"lineStart", 1, 1, 1},
		[RISC_OP(RISC_INLINE)]	  = {"inline", 1, 1, 1},
	};
	u32 p;

	p = RISC_OP(risc);
	if (!(risc & 0x80000000) || !instr[p].name) {
		printk(KERN_DEBUG "0x%08x [ INVALID ]\n", risc);
		return;
	}
	printk(KERN_DEBUG "0x%08x %-9s IRQ=%d",
		risc, instr[p].name, (risc >> 27) & 1);
	if (instr[p].has_data_type)
		printk(KERN_DEBUG " Type=%d", (risc >> 24) & 7);
	if (instr[p].has_byte_info)
		printk(KERN_DEBUG " Start=0x%03x Count=%03u",
			(risc >> 12) & 0xfff, risc & 0xfff);
	if (instr[p].has_addr)
		printk(KERN_DEBUG " StartAddr=0x%08x", addr);
	printk(KERN_DEBUG "\n");
}

void tw68_risc_program_dump(struct tw68_core *core,
			    struct btcx_riscmem *risc)
{
	__le32 *addr;

	printk(KERN_DEBUG "%s: risc_program_dump: risc=%p, "
			  "risc->cpu=0x%p, risc->jmp=0x%p\n",
			  core->name, risc, risc->cpu, risc->jmp);
	for (addr = risc->cpu; addr <= risc->jmp; addr += 2)
		tw68_risc_decode(*addr, *(addr+1));
}
EXPORT_SYMBOL_GPL(tw68_risc_program_dump);
#endif

/*
 * tw68_risc_stopper
 * 	Normally, the risc code generated for a buffer ends with a
 * 	JUMP instruction to direct the DMAP processor to the code for
 * 	the next buffer.  However, when there is no additional buffer
 * 	currently available, the code instead jumps to this routine.
 *
 * 	My first try for a "stopper" program was just a simple
 * 	"jump to self" instruction.  Unfortunately, this caused the
 * 	video FIFO to overflow.  My next attempt was to just disable
 * 	the DMAP processor.  Unfortunately, this caused the video
 * 	decoder to lose its synchronization.  The solution to this was to
 * 	add a "Sync-Odd" instruction, which "eats" all the video data
 * 	until the start of the next odd field.
 */
int tw68_risc_stopper(struct pci_dev *pci, struct btcx_riscmem *risc)
{
	__le32 *rp;
	int rc;

	rc = btcx_riscmem_alloc(pci, risc, 8*4);
	if (rc < 0)
		return rc;

	/* write risc inststructions */
	rp = risc->cpu;
	*(rp++) = cpu_to_le32(RISC_SYNCO);
	*(rp++) = 0;
	*(rp++) = cpu_to_le32(RISC_JUMP);
	*(rp++) = cpu_to_le32(risc->dma);
	risc->jmp = risc->cpu;
	return 0;
}
