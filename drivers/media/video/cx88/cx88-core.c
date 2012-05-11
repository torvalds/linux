/*
 *
 * device driver for Conexant 2388x based TV cards
 * driver core
 *
 * (c) 2003 Gerd Knorr <kraxel@bytesex.org> [SuSE Labs]
 *
 * (c) 2005-2006 Mauro Carvalho Chehab <mchehab@infradead.org>
 *     - Multituner support
 *     - video_ioctl2 conversion
 *     - PAL/M fixes
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
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/init.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/kmod.h>
#include <linux/sound.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/videodev2.h>
#include <linux/mutex.h>

#include "cx88.h"
#include <media/v4l2-common.h>
#include <media/v4l2-ioctl.h>

MODULE_DESCRIPTION("v4l2 driver module for cx2388x based TV cards");
MODULE_AUTHOR("Gerd Knorr <kraxel@bytesex.org> [SuSE Labs]");
MODULE_LICENSE("GPL");

/* ------------------------------------------------------------------ */

static unsigned int core_debug;
module_param(core_debug,int,0644);
MODULE_PARM_DESC(core_debug,"enable debug messages [core]");

static unsigned int nicam;
module_param(nicam,int,0644);
MODULE_PARM_DESC(nicam,"tv audio is nicam");

static unsigned int nocomb;
module_param(nocomb,int,0644);
MODULE_PARM_DESC(nocomb,"disable comb filter");

#define dprintk(level,fmt, arg...)	if (core_debug >= level)	\
	printk(KERN_DEBUG "%s: " fmt, core->name , ## arg)

static unsigned int cx88_devcount;
static LIST_HEAD(cx88_devlist);
static DEFINE_MUTEX(devlist);

#define NO_SYNC_LINE (-1U)

/* @lpi: lines per IRQ, or 0 to not generate irqs. Note: IRQ to be
	 generated _after_ lpi lines are transferred. */
static __le32* cx88_risc_field(__le32 *rp, struct scatterlist *sglist,
			    unsigned int offset, u32 sync_line,
			    unsigned int bpl, unsigned int padding,
			    unsigned int lines, unsigned int lpi)
{
	struct scatterlist *sg;
	unsigned int line,todo,sol;

	/* sync instruction */
	if (sync_line != NO_SYNC_LINE)
		*(rp++) = cpu_to_le32(RISC_RESYNC | sync_line);

	/* scan lines */
	sg = sglist;
	for (line = 0; line < lines; line++) {
		while (offset && offset >= sg_dma_len(sg)) {
			offset -= sg_dma_len(sg);
			sg++;
		}
		if (lpi && line>0 && !(line % lpi))
			sol = RISC_SOL | RISC_IRQ1 | RISC_CNT_INC;
		else
			sol = RISC_SOL;
		if (bpl <= sg_dma_len(sg)-offset) {
			/* fits into current chunk */
			*(rp++)=cpu_to_le32(RISC_WRITE|sol|RISC_EOL|bpl);
			*(rp++)=cpu_to_le32(sg_dma_address(sg)+offset);
			offset+=bpl;
		} else {
			/* scanline needs to be split */
			todo = bpl;
			*(rp++)=cpu_to_le32(RISC_WRITE|sol|
					    (sg_dma_len(sg)-offset));
			*(rp++)=cpu_to_le32(sg_dma_address(sg)+offset);
			todo -= (sg_dma_len(sg)-offset);
			offset = 0;
			sg++;
			while (todo > sg_dma_len(sg)) {
				*(rp++)=cpu_to_le32(RISC_WRITE|
						    sg_dma_len(sg));
				*(rp++)=cpu_to_le32(sg_dma_address(sg));
				todo -= sg_dma_len(sg);
				sg++;
			}
			*(rp++)=cpu_to_le32(RISC_WRITE|RISC_EOL|todo);
			*(rp++)=cpu_to_le32(sg_dma_address(sg));
			offset += todo;
		}
		offset += padding;
	}

	return rp;
}

int cx88_risc_buffer(struct pci_dev *pci, struct btcx_riscmem *risc,
		     struct scatterlist *sglist,
		     unsigned int top_offset, unsigned int bottom_offset,
		     unsigned int bpl, unsigned int padding, unsigned int lines)
{
	u32 instructions,fields;
	__le32 *rp;
	int rc;

	fields = 0;
	if (UNSET != top_offset)
		fields++;
	if (UNSET != bottom_offset)
		fields++;

	/* estimate risc mem: worst case is one write per page border +
	   one write per scan line + syncs + jump (all 2 dwords).  Padding
	   can cause next bpl to start close to a page border.  First DMA
	   region may be smaller than PAGE_SIZE */
	instructions  = fields * (1 + ((bpl + padding) * lines) / PAGE_SIZE + lines);
	instructions += 2;
	if ((rc = btcx_riscmem_alloc(pci,risc,instructions*8)) < 0)
		return rc;

	/* write risc instructions */
	rp = risc->cpu;
	if (UNSET != top_offset)
		rp = cx88_risc_field(rp, sglist, top_offset, 0,
				     bpl, padding, lines, 0);
	if (UNSET != bottom_offset)
		rp = cx88_risc_field(rp, sglist, bottom_offset, 0x200,
				     bpl, padding, lines, 0);

	/* save pointer to jmp instruction address */
	risc->jmp = rp;
	BUG_ON((risc->jmp - risc->cpu + 2) * sizeof (*risc->cpu) > risc->size);
	return 0;
}

int cx88_risc_databuffer(struct pci_dev *pci, struct btcx_riscmem *risc,
			 struct scatterlist *sglist, unsigned int bpl,
			 unsigned int lines, unsigned int lpi)
{
	u32 instructions;
	__le32 *rp;
	int rc;

	/* estimate risc mem: worst case is one write per page border +
	   one write per scan line + syncs + jump (all 2 dwords).  Here
	   there is no padding and no sync.  First DMA region may be smaller
	   than PAGE_SIZE */
	instructions  = 1 + (bpl * lines) / PAGE_SIZE + lines;
	instructions += 1;
	if ((rc = btcx_riscmem_alloc(pci,risc,instructions*8)) < 0)
		return rc;

	/* write risc instructions */
	rp = risc->cpu;
	rp = cx88_risc_field(rp, sglist, 0, NO_SYNC_LINE, bpl, 0, lines, lpi);

	/* save pointer to jmp instruction address */
	risc->jmp = rp;
	BUG_ON((risc->jmp - risc->cpu + 2) * sizeof (*risc->cpu) > risc->size);
	return 0;
}

int cx88_risc_stopper(struct pci_dev *pci, struct btcx_riscmem *risc,
		      u32 reg, u32 mask, u32 value)
{
	__le32 *rp;
	int rc;

	if ((rc = btcx_riscmem_alloc(pci, risc, 4*16)) < 0)
		return rc;

	/* write risc instructions */
	rp = risc->cpu;
	*(rp++) = cpu_to_le32(RISC_WRITECR  | RISC_IRQ2 | RISC_IMM);
	*(rp++) = cpu_to_le32(reg);
	*(rp++) = cpu_to_le32(value);
	*(rp++) = cpu_to_le32(mask);
	*(rp++) = cpu_to_le32(RISC_JUMP);
	*(rp++) = cpu_to_le32(risc->dma);
	return 0;
}

void
cx88_free_buffer(struct videobuf_queue *q, struct cx88_buffer *buf)
{
	struct videobuf_dmabuf *dma=videobuf_to_dma(&buf->vb);

	BUG_ON(in_interrupt());
	videobuf_waiton(q, &buf->vb, 0, 0);
	videobuf_dma_unmap(q->dev, dma);
	videobuf_dma_free(dma);
	btcx_riscmem_free(to_pci_dev(q->dev), &buf->risc);
	buf->vb.state = VIDEOBUF_NEEDS_INIT;
}

/* ------------------------------------------------------------------ */
/* our SRAM memory layout                                             */

/* we are going to put all thr risc programs into host memory, so we
 * can use the whole SDRAM for the DMA fifos.  To simplify things, we
 * use a static memory layout.  That surely will waste memory in case
 * we don't use all DMA channels at the same time (which will be the
 * case most of the time).  But that still gives us enough FIFO space
 * to be able to deal with insane long pci latencies ...
 *
 * FIFO space allocations:
 *    channel  21    (y video)  - 10.0k
 *    channel  22    (u video)  -  2.0k
 *    channel  23    (v video)  -  2.0k
 *    channel  24    (vbi)      -  4.0k
 *    channels 25+26 (audio)    -  4.0k
 *    channel  28    (mpeg)     -  4.0k
 *    channel  27    (audio rds)-  3.0k
 *    TOTAL                     = 29.0k
 *
 * Every channel has 160 bytes control data (64 bytes instruction
 * queue and 6 CDT entries), which is close to 2k total.
 *
 * Address layout:
 *    0x0000 - 0x03ff    CMDs / reserved
 *    0x0400 - 0x0bff    instruction queues + CDs
 *    0x0c00 -           FIFOs
 */

const struct sram_channel const cx88_sram_channels[] = {
	[SRAM_CH21] = {
		.name       = "video y / packed",
		.cmds_start = 0x180040,
		.ctrl_start = 0x180400,
		.cdt        = 0x180400 + 64,
		.fifo_start = 0x180c00,
		.fifo_size  = 0x002800,
		.ptr1_reg   = MO_DMA21_PTR1,
		.ptr2_reg   = MO_DMA21_PTR2,
		.cnt1_reg   = MO_DMA21_CNT1,
		.cnt2_reg   = MO_DMA21_CNT2,
	},
	[SRAM_CH22] = {
		.name       = "video u",
		.cmds_start = 0x180080,
		.ctrl_start = 0x1804a0,
		.cdt        = 0x1804a0 + 64,
		.fifo_start = 0x183400,
		.fifo_size  = 0x000800,
		.ptr1_reg   = MO_DMA22_PTR1,
		.ptr2_reg   = MO_DMA22_PTR2,
		.cnt1_reg   = MO_DMA22_CNT1,
		.cnt2_reg   = MO_DMA22_CNT2,
	},
	[SRAM_CH23] = {
		.name       = "video v",
		.cmds_start = 0x1800c0,
		.ctrl_start = 0x180540,
		.cdt        = 0x180540 + 64,
		.fifo_start = 0x183c00,
		.fifo_size  = 0x000800,
		.ptr1_reg   = MO_DMA23_PTR1,
		.ptr2_reg   = MO_DMA23_PTR2,
		.cnt1_reg   = MO_DMA23_CNT1,
		.cnt2_reg   = MO_DMA23_CNT2,
	},
	[SRAM_CH24] = {
		.name       = "vbi",
		.cmds_start = 0x180100,
		.ctrl_start = 0x1805e0,
		.cdt        = 0x1805e0 + 64,
		.fifo_start = 0x184400,
		.fifo_size  = 0x001000,
		.ptr1_reg   = MO_DMA24_PTR1,
		.ptr2_reg   = MO_DMA24_PTR2,
		.cnt1_reg   = MO_DMA24_CNT1,
		.cnt2_reg   = MO_DMA24_CNT2,
	},
	[SRAM_CH25] = {
		.name       = "audio from",
		.cmds_start = 0x180140,
		.ctrl_start = 0x180680,
		.cdt        = 0x180680 + 64,
		.fifo_start = 0x185400,
		.fifo_size  = 0x001000,
		.ptr1_reg   = MO_DMA25_PTR1,
		.ptr2_reg   = MO_DMA25_PTR2,
		.cnt1_reg   = MO_DMA25_CNT1,
		.cnt2_reg   = MO_DMA25_CNT2,
	},
	[SRAM_CH26] = {
		.name       = "audio to",
		.cmds_start = 0x180180,
		.ctrl_start = 0x180720,
		.cdt        = 0x180680 + 64,  /* same as audio IN */
		.fifo_start = 0x185400,       /* same as audio IN */
		.fifo_size  = 0x001000,       /* same as audio IN */
		.ptr1_reg   = MO_DMA26_PTR1,
		.ptr2_reg   = MO_DMA26_PTR2,
		.cnt1_reg   = MO_DMA26_CNT1,
		.cnt2_reg   = MO_DMA26_CNT2,
	},
	[SRAM_CH28] = {
		.name       = "mpeg",
		.cmds_start = 0x180200,
		.ctrl_start = 0x1807C0,
		.cdt        = 0x1807C0 + 64,
		.fifo_start = 0x186400,
		.fifo_size  = 0x001000,
		.ptr1_reg   = MO_DMA28_PTR1,
		.ptr2_reg   = MO_DMA28_PTR2,
		.cnt1_reg   = MO_DMA28_CNT1,
		.cnt2_reg   = MO_DMA28_CNT2,
	},
	[SRAM_CH27] = {
		.name       = "audio rds",
		.cmds_start = 0x1801C0,
		.ctrl_start = 0x180860,
		.cdt        = 0x180860 + 64,
		.fifo_start = 0x187400,
		.fifo_size  = 0x000C00,
		.ptr1_reg   = MO_DMA27_PTR1,
		.ptr2_reg   = MO_DMA27_PTR2,
		.cnt1_reg   = MO_DMA27_CNT1,
		.cnt2_reg   = MO_DMA27_CNT2,
	},
};

int cx88_sram_channel_setup(struct cx88_core *core,
			    const struct sram_channel *ch,
			    unsigned int bpl, u32 risc)
{
	unsigned int i,lines;
	u32 cdt;

	bpl   = (bpl + 7) & ~7; /* alignment */
	cdt   = ch->cdt;
	lines = ch->fifo_size / bpl;
	if (lines > 6)
		lines = 6;
	BUG_ON(lines < 2);

	/* write CDT */
	for (i = 0; i < lines; i++)
		cx_write(cdt + 16*i, ch->fifo_start + bpl*i);

	/* write CMDS */
	cx_write(ch->cmds_start +  0, risc);
	cx_write(ch->cmds_start +  4, cdt);
	cx_write(ch->cmds_start +  8, (lines*16) >> 3);
	cx_write(ch->cmds_start + 12, ch->ctrl_start);
	cx_write(ch->cmds_start + 16, 64 >> 2);
	for (i = 20; i < 64; i += 4)
		cx_write(ch->cmds_start + i, 0);

	/* fill registers */
	cx_write(ch->ptr1_reg, ch->fifo_start);
	cx_write(ch->ptr2_reg, cdt);
	cx_write(ch->cnt1_reg, (bpl >> 3) -1);
	cx_write(ch->cnt2_reg, (lines*16) >> 3);

	dprintk(2,"sram setup %s: bpl=%d lines=%d\n", ch->name, bpl, lines);
	return 0;
}

/* ------------------------------------------------------------------ */
/* debug helper code                                                  */

static int cx88_risc_decode(u32 risc)
{
	static const char * const instr[16] = {
		[ RISC_SYNC    >> 28 ] = "sync",
		[ RISC_WRITE   >> 28 ] = "write",
		[ RISC_WRITEC  >> 28 ] = "writec",
		[ RISC_READ    >> 28 ] = "read",
		[ RISC_READC   >> 28 ] = "readc",
		[ RISC_JUMP    >> 28 ] = "jump",
		[ RISC_SKIP    >> 28 ] = "skip",
		[ RISC_WRITERM >> 28 ] = "writerm",
		[ RISC_WRITECM >> 28 ] = "writecm",
		[ RISC_WRITECR >> 28 ] = "writecr",
	};
	static int const incr[16] = {
		[ RISC_WRITE   >> 28 ] = 2,
		[ RISC_JUMP    >> 28 ] = 2,
		[ RISC_WRITERM >> 28 ] = 3,
		[ RISC_WRITECM >> 28 ] = 3,
		[ RISC_WRITECR >> 28 ] = 4,
	};
	static const char * const bits[] = {
		"12",   "13",   "14",   "resync",
		"cnt0", "cnt1", "18",   "19",
		"20",   "21",   "22",   "23",
		"irq1", "irq2", "eol",  "sol",
	};
	int i;

	printk("0x%08x [ %s", risc,
	       instr[risc >> 28] ? instr[risc >> 28] : "INVALID");
	for (i = ARRAY_SIZE(bits)-1; i >= 0; i--)
		if (risc & (1 << (i + 12)))
			printk(" %s",bits[i]);
	printk(" count=%d ]\n", risc & 0xfff);
	return incr[risc >> 28] ? incr[risc >> 28] : 1;
}


void cx88_sram_channel_dump(struct cx88_core *core,
			    const struct sram_channel *ch)
{
	static const char * const name[] = {
		"initial risc",
		"cdt base",
		"cdt size",
		"iq base",
		"iq size",
		"risc pc",
		"iq wr ptr",
		"iq rd ptr",
		"cdt current",
		"pci target",
		"line / byte",
	};
	u32 risc;
	unsigned int i,j,n;

	printk("%s: %s - dma channel status dump\n",
	       core->name,ch->name);
	for (i = 0; i < ARRAY_SIZE(name); i++)
		printk("%s:   cmds: %-12s: 0x%08x\n",
		       core->name,name[i],
		       cx_read(ch->cmds_start + 4*i));
	for (n = 1, i = 0; i < 4; i++) {
		risc = cx_read(ch->cmds_start + 4 * (i+11));
		printk("%s:   risc%d: ", core->name, i);
		if (--n)
			printk("0x%08x [ arg #%d ]\n", risc, n);
		else
			n = cx88_risc_decode(risc);
	}
	for (i = 0; i < 16; i += n) {
		risc = cx_read(ch->ctrl_start + 4 * i);
		printk("%s:   iq %x: ", core->name, i);
		n = cx88_risc_decode(risc);
		for (j = 1; j < n; j++) {
			risc = cx_read(ch->ctrl_start + 4 * (i+j));
			printk("%s:   iq %x: 0x%08x [ arg #%d ]\n",
			       core->name, i+j, risc, j);
		}
	}

	printk("%s: fifo: 0x%08x -> 0x%x\n",
	       core->name, ch->fifo_start, ch->fifo_start+ch->fifo_size);
	printk("%s: ctrl: 0x%08x -> 0x%x\n",
	       core->name, ch->ctrl_start, ch->ctrl_start+6*16);
	printk("%s:   ptr1_reg: 0x%08x\n",
	       core->name,cx_read(ch->ptr1_reg));
	printk("%s:   ptr2_reg: 0x%08x\n",
	       core->name,cx_read(ch->ptr2_reg));
	printk("%s:   cnt1_reg: 0x%08x\n",
	       core->name,cx_read(ch->cnt1_reg));
	printk("%s:   cnt2_reg: 0x%08x\n",
	       core->name,cx_read(ch->cnt2_reg));
}

static const char *cx88_pci_irqs[32] = {
	"vid", "aud", "ts", "vip", "hst", "5", "6", "tm1",
	"src_dma", "dst_dma", "risc_rd_err", "risc_wr_err",
	"brdg_err", "src_dma_err", "dst_dma_err", "ipb_dma_err",
	"i2c", "i2c_rack", "ir_smp", "gpio0", "gpio1"
};

void cx88_print_irqbits(const char *name, const char *tag, const char *strings[],
			int len, u32 bits, u32 mask)
{
	unsigned int i;

	printk(KERN_DEBUG "%s: %s [0x%x]", name, tag, bits);
	for (i = 0; i < len; i++) {
		if (!(bits & (1 << i)))
			continue;
		if (strings[i])
			printk(" %s", strings[i]);
		else
			printk(" %d", i);
		if (!(mask & (1 << i)))
			continue;
		printk("*");
	}
	printk("\n");
}

/* ------------------------------------------------------------------ */

int cx88_core_irq(struct cx88_core *core, u32 status)
{
	int handled = 0;

	if (status & PCI_INT_IR_SMPINT) {
		cx88_ir_irq(core);
		handled++;
	}
	if (!handled)
		cx88_print_irqbits(core->name, "irq pci",
				   cx88_pci_irqs, ARRAY_SIZE(cx88_pci_irqs),
				   status, core->pci_irqmask);
	return handled;
}

void cx88_wakeup(struct cx88_core *core,
		 struct cx88_dmaqueue *q, u32 count)
{
	struct cx88_buffer *buf;
	int bc;

	for (bc = 0;; bc++) {
		if (list_empty(&q->active))
			break;
		buf = list_entry(q->active.next,
				 struct cx88_buffer, vb.queue);
		/* count comes from the hw and is is 16bit wide --
		 * this trick handles wrap-arounds correctly for
		 * up to 32767 buffers in flight... */
		if ((s16) (count - buf->count) < 0)
			break;
		do_gettimeofday(&buf->vb.ts);
		dprintk(2,"[%p/%d] wakeup reg=%d buf=%d\n",buf,buf->vb.i,
			count, buf->count);
		buf->vb.state = VIDEOBUF_DONE;
		list_del(&buf->vb.queue);
		wake_up(&buf->vb.done);
	}
	if (list_empty(&q->active)) {
		del_timer(&q->timeout);
	} else {
		mod_timer(&q->timeout, jiffies+BUFFER_TIMEOUT);
	}
	if (bc != 1)
		dprintk(2, "%s: %d buffers handled (should be 1)\n",
			__func__, bc);
}

void cx88_shutdown(struct cx88_core *core)
{
	/* disable RISC controller + IRQs */
	cx_write(MO_DEV_CNTRL2, 0);

	/* stop dma transfers */
	cx_write(MO_VID_DMACNTRL, 0x0);
	cx_write(MO_AUD_DMACNTRL, 0x0);
	cx_write(MO_TS_DMACNTRL, 0x0);
	cx_write(MO_VIP_DMACNTRL, 0x0);
	cx_write(MO_GPHST_DMACNTRL, 0x0);

	/* stop interrupts */
	cx_write(MO_PCI_INTMSK, 0x0);
	cx_write(MO_VID_INTMSK, 0x0);
	cx_write(MO_AUD_INTMSK, 0x0);
	cx_write(MO_TS_INTMSK, 0x0);
	cx_write(MO_VIP_INTMSK, 0x0);
	cx_write(MO_GPHST_INTMSK, 0x0);

	/* stop capturing */
	cx_write(VID_CAPTURE_CONTROL, 0);
}

int cx88_reset(struct cx88_core *core)
{
	dprintk(1,"%s\n",__func__);
	cx88_shutdown(core);

	/* clear irq status */
	cx_write(MO_VID_INTSTAT, 0xFFFFFFFF); // Clear PIV int
	cx_write(MO_PCI_INTSTAT, 0xFFFFFFFF); // Clear PCI int
	cx_write(MO_INT1_STAT,   0xFFFFFFFF); // Clear RISC int

	/* wait a bit */
	msleep(100);

	/* init sram */
	cx88_sram_channel_setup(core, &cx88_sram_channels[SRAM_CH21], 720*4, 0);
	cx88_sram_channel_setup(core, &cx88_sram_channels[SRAM_CH22], 128, 0);
	cx88_sram_channel_setup(core, &cx88_sram_channels[SRAM_CH23], 128, 0);
	cx88_sram_channel_setup(core, &cx88_sram_channels[SRAM_CH24], 128, 0);
	cx88_sram_channel_setup(core, &cx88_sram_channels[SRAM_CH25], 128, 0);
	cx88_sram_channel_setup(core, &cx88_sram_channels[SRAM_CH26], 128, 0);
	cx88_sram_channel_setup(core, &cx88_sram_channels[SRAM_CH28], 188*4, 0);
	cx88_sram_channel_setup(core, &cx88_sram_channels[SRAM_CH27], 128, 0);

	/* misc init ... */
	cx_write(MO_INPUT_FORMAT, ((1 << 13) |   // agc enable
				   (1 << 12) |   // agc gain
				   (1 << 11) |   // adaptibe agc
				   (0 << 10) |   // chroma agc
				   (0 <<  9) |   // ckillen
				   (7)));

	/* setup image format */
	cx_andor(MO_COLOR_CTRL, 0x4000, 0x4000);

	/* setup FIFO Thresholds */
	cx_write(MO_PDMA_STHRSH,   0x0807);
	cx_write(MO_PDMA_DTHRSH,   0x0807);

	/* fixes flashing of image */
	cx_write(MO_AGC_SYNC_TIP1, 0x0380000F);
	cx_write(MO_AGC_BACK_VBI,  0x00E00555);

	cx_write(MO_VID_INTSTAT,   0xFFFFFFFF); // Clear PIV int
	cx_write(MO_PCI_INTSTAT,   0xFFFFFFFF); // Clear PCI int
	cx_write(MO_INT1_STAT,     0xFFFFFFFF); // Clear RISC int

	/* Reset on-board parts */
	cx_write(MO_SRST_IO, 0);
	msleep(10);
	cx_write(MO_SRST_IO, 1);

	return 0;
}

/* ------------------------------------------------------------------ */

static unsigned int inline norm_swidth(v4l2_std_id norm)
{
	return (norm & (V4L2_STD_MN & ~V4L2_STD_PAL_Nc)) ? 754 : 922;
}

static unsigned int inline norm_hdelay(v4l2_std_id norm)
{
	return (norm & (V4L2_STD_MN & ~V4L2_STD_PAL_Nc)) ? 135 : 186;
}

static unsigned int inline norm_vdelay(v4l2_std_id norm)
{
	return (norm & V4L2_STD_625_50) ? 0x24 : 0x18;
}

static unsigned int inline norm_fsc8(v4l2_std_id norm)
{
	if (norm & V4L2_STD_PAL_M)
		return 28604892;      // 3.575611 MHz

	if (norm & (V4L2_STD_PAL_Nc))
		return 28656448;      // 3.582056 MHz

	if (norm & V4L2_STD_NTSC) // All NTSC/M and variants
		return 28636360;      // 3.57954545 MHz +/- 10 Hz

	/* SECAM have also different sub carrier for chroma,
	   but step_db and step_dr, at cx88_set_tvnorm already handles that.

	   The same FSC applies to PAL/BGDKIH, PAL/60, NTSC/4.43 and PAL/N
	 */

	return 35468950;      // 4.43361875 MHz +/- 5 Hz
}

static unsigned int inline norm_htotal(v4l2_std_id norm)
{

	unsigned int fsc4=norm_fsc8(norm)/2;

	/* returns 4*FSC / vtotal / frames per seconds */
	return (norm & V4L2_STD_625_50) ?
				((fsc4+312)/625+12)/25 :
				((fsc4+262)/525*1001+15000)/30000;
}

static unsigned int inline norm_vbipack(v4l2_std_id norm)
{
	return (norm & V4L2_STD_625_50) ? 511 : 400;
}

int cx88_set_scale(struct cx88_core *core, unsigned int width, unsigned int height,
		   enum v4l2_field field)
{
	unsigned int swidth  = norm_swidth(core->tvnorm);
	unsigned int sheight = norm_maxh(core->tvnorm);
	u32 value;

	dprintk(1,"set_scale: %dx%d [%s%s,%s]\n", width, height,
		V4L2_FIELD_HAS_TOP(field)    ? "T" : "",
		V4L2_FIELD_HAS_BOTTOM(field) ? "B" : "",
		v4l2_norm_to_name(core->tvnorm));
	if (!V4L2_FIELD_HAS_BOTH(field))
		height *= 2;

	// recalc H delay and scale registers
	value = (width * norm_hdelay(core->tvnorm)) / swidth;
	value &= 0x3fe;
	cx_write(MO_HDELAY_EVEN,  value);
	cx_write(MO_HDELAY_ODD,   value);
	dprintk(1,"set_scale: hdelay  0x%04x (width %d)\n", value,swidth);

	value = (swidth * 4096 / width) - 4096;
	cx_write(MO_HSCALE_EVEN,  value);
	cx_write(MO_HSCALE_ODD,   value);
	dprintk(1,"set_scale: hscale  0x%04x\n", value);

	cx_write(MO_HACTIVE_EVEN, width);
	cx_write(MO_HACTIVE_ODD,  width);
	dprintk(1,"set_scale: hactive 0x%04x\n", width);

	// recalc V scale Register (delay is constant)
	cx_write(MO_VDELAY_EVEN, norm_vdelay(core->tvnorm));
	cx_write(MO_VDELAY_ODD,  norm_vdelay(core->tvnorm));
	dprintk(1,"set_scale: vdelay  0x%04x\n", norm_vdelay(core->tvnorm));

	value = (0x10000 - (sheight * 512 / height - 512)) & 0x1fff;
	cx_write(MO_VSCALE_EVEN,  value);
	cx_write(MO_VSCALE_ODD,   value);
	dprintk(1,"set_scale: vscale  0x%04x\n", value);

	cx_write(MO_VACTIVE_EVEN, sheight);
	cx_write(MO_VACTIVE_ODD,  sheight);
	dprintk(1,"set_scale: vactive 0x%04x\n", sheight);

	// setup filters
	value = 0;
	value |= (1 << 19);        // CFILT (default)
	if (core->tvnorm & V4L2_STD_SECAM) {
		value |= (1 << 15);
		value |= (1 << 16);
	}
	if (INPUT(core->input).type == CX88_VMUX_SVIDEO)
		value |= (1 << 13) | (1 << 5);
	if (V4L2_FIELD_INTERLACED == field)
		value |= (1 << 3); // VINT (interlaced vertical scaling)
	if (width < 385)
		value |= (1 << 0); // 3-tap interpolation
	if (width < 193)
		value |= (1 << 1); // 5-tap interpolation
	if (nocomb)
		value |= (3 << 5); // disable comb filter

	cx_andor(MO_FILTER_EVEN,  0x7ffc7f, value); /* preserve PEAKEN, PSEL */
	cx_andor(MO_FILTER_ODD,   0x7ffc7f, value);
	dprintk(1,"set_scale: filter  0x%04x\n", value);

	return 0;
}

static const u32 xtal = 28636363;

static int set_pll(struct cx88_core *core, int prescale, u32 ofreq)
{
	static const u32 pre[] = { 0, 0, 0, 3, 2, 1 };
	u64 pll;
	u32 reg;
	int i;

	if (prescale < 2)
		prescale = 2;
	if (prescale > 5)
		prescale = 5;

	pll = ofreq * 8 * prescale * (u64)(1 << 20);
	do_div(pll,xtal);
	reg = (pll & 0x3ffffff) | (pre[prescale] << 26);
	if (((reg >> 20) & 0x3f) < 14) {
		printk("%s/0: pll out of range\n",core->name);
		return -1;
	}

	dprintk(1,"set_pll:    MO_PLL_REG       0x%08x [old=0x%08x,freq=%d]\n",
		reg, cx_read(MO_PLL_REG), ofreq);
	cx_write(MO_PLL_REG, reg);
	for (i = 0; i < 100; i++) {
		reg = cx_read(MO_DEVICE_STATUS);
		if (reg & (1<<2)) {
			dprintk(1,"pll locked [pre=%d,ofreq=%d]\n",
				prescale,ofreq);
			return 0;
		}
		dprintk(1,"pll not locked yet, waiting ...\n");
		msleep(10);
	}
	dprintk(1,"pll NOT locked [pre=%d,ofreq=%d]\n",prescale,ofreq);
	return -1;
}

int cx88_start_audio_dma(struct cx88_core *core)
{
	/* constant 128 made buzz in analog Nicam-stereo for bigger fifo_size */
	int bpl = cx88_sram_channels[SRAM_CH25].fifo_size/4;

	int rds_bpl = cx88_sram_channels[SRAM_CH27].fifo_size/AUD_RDS_LINES;

	/* If downstream RISC is enabled, bail out; ALSA is managing DMA */
	if (cx_read(MO_AUD_DMACNTRL) & 0x10)
		return 0;

	/* setup fifo + format */
	cx88_sram_channel_setup(core, &cx88_sram_channels[SRAM_CH25], bpl, 0);
	cx88_sram_channel_setup(core, &cx88_sram_channels[SRAM_CH26], bpl, 0);
	cx88_sram_channel_setup(core, &cx88_sram_channels[SRAM_CH27],
				rds_bpl, 0);

	cx_write(MO_AUDD_LNGTH, bpl); /* fifo bpl size */
	cx_write(MO_AUDR_LNGTH, rds_bpl); /* fifo bpl size */

	/* enable Up, Down and Audio RDS fifo */
	cx_write(MO_AUD_DMACNTRL, 0x0007);

	return 0;
}

int cx88_stop_audio_dma(struct cx88_core *core)
{
	/* If downstream RISC is enabled, bail out; ALSA is managing DMA */
	if (cx_read(MO_AUD_DMACNTRL) & 0x10)
		return 0;

	/* stop dma */
	cx_write(MO_AUD_DMACNTRL, 0x0000);

	return 0;
}

static int set_tvaudio(struct cx88_core *core)
{
	v4l2_std_id norm = core->tvnorm;

	if (CX88_VMUX_TELEVISION != INPUT(core->input).type &&
	    CX88_VMUX_CABLE != INPUT(core->input).type)
		return 0;

	if (V4L2_STD_PAL_BG & norm) {
		core->tvaudio = WW_BG;

	} else if (V4L2_STD_PAL_DK & norm) {
		core->tvaudio = WW_DK;

	} else if (V4L2_STD_PAL_I & norm) {
		core->tvaudio = WW_I;

	} else if (V4L2_STD_SECAM_L & norm) {
		core->tvaudio = WW_L;

	} else if ((V4L2_STD_SECAM_B | V4L2_STD_SECAM_G | V4L2_STD_SECAM_H) & norm) {
		core->tvaudio = WW_BG;

	} else if (V4L2_STD_SECAM_DK & norm) {
		core->tvaudio = WW_DK;

	} else if ((V4L2_STD_NTSC_M & norm) ||
		   (V4L2_STD_PAL_M  & norm)) {
		core->tvaudio = WW_BTSC;

	} else if (V4L2_STD_NTSC_M_JP & norm) {
		core->tvaudio = WW_EIAJ;

	} else {
		printk("%s/0: tvaudio support needs work for this tv norm [%s], sorry\n",
		       core->name, v4l2_norm_to_name(core->tvnorm));
		core->tvaudio = WW_NONE;
		return 0;
	}

	cx_andor(MO_AFECFG_IO, 0x1f, 0x0);
	cx88_set_tvaudio(core);
	/* cx88_set_stereo(dev,V4L2_TUNER_MODE_STEREO); */

/*
   This should be needed only on cx88-alsa. It seems that some cx88 chips have
   bugs and does require DMA enabled for it to work.
 */
	cx88_start_audio_dma(core);
	return 0;
}



int cx88_set_tvnorm(struct cx88_core *core, v4l2_std_id norm)
{
	u32 fsc8;
	u32 adc_clock;
	u32 vdec_clock;
	u32 step_db,step_dr;
	u64 tmp64;
	u32 bdelay,agcdelay,htotal;
	u32 cxiformat, cxoformat;

	core->tvnorm = norm;
	fsc8       = norm_fsc8(norm);
	adc_clock  = xtal;
	vdec_clock = fsc8;
	step_db    = fsc8;
	step_dr    = fsc8;

	if (norm & V4L2_STD_NTSC_M_JP) {
		cxiformat = VideoFormatNTSCJapan;
		cxoformat = 0x181f0008;
	} else if (norm & V4L2_STD_NTSC_443) {
		cxiformat = VideoFormatNTSC443;
		cxoformat = 0x181f0008;
	} else if (norm & V4L2_STD_PAL_M) {
		cxiformat = VideoFormatPALM;
		cxoformat = 0x1c1f0008;
	} else if (norm & V4L2_STD_PAL_N) {
		cxiformat = VideoFormatPALN;
		cxoformat = 0x1c1f0008;
	} else if (norm & V4L2_STD_PAL_Nc) {
		cxiformat = VideoFormatPALNC;
		cxoformat = 0x1c1f0008;
	} else if (norm & V4L2_STD_PAL_60) {
		cxiformat = VideoFormatPAL60;
		cxoformat = 0x181f0008;
	} else if (norm & V4L2_STD_NTSC) {
		cxiformat = VideoFormatNTSC;
		cxoformat = 0x181f0008;
	} else if (norm & V4L2_STD_SECAM) {
		step_db = 4250000 * 8;
		step_dr = 4406250 * 8;

		cxiformat = VideoFormatSECAM;
		cxoformat = 0x181f0008;
	} else { /* PAL */
		cxiformat = VideoFormatPAL;
		cxoformat = 0x181f0008;
	}

	dprintk(1,"set_tvnorm: \"%s\" fsc8=%d adc=%d vdec=%d db/dr=%d/%d\n",
		v4l2_norm_to_name(core->tvnorm), fsc8, adc_clock, vdec_clock,
		step_db, step_dr);
	set_pll(core,2,vdec_clock);

	dprintk(1,"set_tvnorm: MO_INPUT_FORMAT  0x%08x [old=0x%08x]\n",
		cxiformat, cx_read(MO_INPUT_FORMAT) & 0x0f);
	/* Chroma AGC must be disabled if SECAM is used, we enable it
	   by default on PAL and NTSC */
	cx_andor(MO_INPUT_FORMAT, 0x40f,
		 norm & V4L2_STD_SECAM ? cxiformat : cxiformat | 0x400);

	// FIXME: as-is from DScaler
	dprintk(1,"set_tvnorm: MO_OUTPUT_FORMAT 0x%08x [old=0x%08x]\n",
		cxoformat, cx_read(MO_OUTPUT_FORMAT));
	cx_write(MO_OUTPUT_FORMAT, cxoformat);

	// MO_SCONV_REG = adc clock / video dec clock * 2^17
	tmp64  = adc_clock * (u64)(1 << 17);
	do_div(tmp64, vdec_clock);
	dprintk(1,"set_tvnorm: MO_SCONV_REG     0x%08x [old=0x%08x]\n",
		(u32)tmp64, cx_read(MO_SCONV_REG));
	cx_write(MO_SCONV_REG, (u32)tmp64);

	// MO_SUB_STEP = 8 * fsc / video dec clock * 2^22
	tmp64  = step_db * (u64)(1 << 22);
	do_div(tmp64, vdec_clock);
	dprintk(1,"set_tvnorm: MO_SUB_STEP      0x%08x [old=0x%08x]\n",
		(u32)tmp64, cx_read(MO_SUB_STEP));
	cx_write(MO_SUB_STEP, (u32)tmp64);

	// MO_SUB_STEP_DR = 8 * 4406250 / video dec clock * 2^22
	tmp64  = step_dr * (u64)(1 << 22);
	do_div(tmp64, vdec_clock);
	dprintk(1,"set_tvnorm: MO_SUB_STEP_DR   0x%08x [old=0x%08x]\n",
		(u32)tmp64, cx_read(MO_SUB_STEP_DR));
	cx_write(MO_SUB_STEP_DR, (u32)tmp64);

	// bdelay + agcdelay
	bdelay   = vdec_clock * 65 / 20000000 + 21;
	agcdelay = vdec_clock * 68 / 20000000 + 15;
	dprintk(1,"set_tvnorm: MO_AGC_BURST     0x%08x [old=0x%08x,bdelay=%d,agcdelay=%d]\n",
		(bdelay << 8) | agcdelay, cx_read(MO_AGC_BURST), bdelay, agcdelay);
	cx_write(MO_AGC_BURST, (bdelay << 8) | agcdelay);

	// htotal
	tmp64 = norm_htotal(norm) * (u64)vdec_clock;
	do_div(tmp64, fsc8);
	htotal = (u32)tmp64;
	dprintk(1,"set_tvnorm: MO_HTOTAL        0x%08x [old=0x%08x,htotal=%d]\n",
		htotal, cx_read(MO_HTOTAL), (u32)tmp64);
	cx_andor(MO_HTOTAL, 0x07ff, htotal);

	// vbi stuff, set vbi offset to 10 (for 20 Clk*2 pixels), this makes
	// the effective vbi offset ~244 samples, the same as the Bt8x8
	cx_write(MO_VBI_PACKET, (10<<11) | norm_vbipack(norm));

	// this is needed as well to set all tvnorm parameter
	cx88_set_scale(core, 320, 240, V4L2_FIELD_INTERLACED);

	// audio
	set_tvaudio(core);

	// tell i2c chips
	call_all(core, core, s_std, norm);

	/* The chroma_agc control should be inaccessible if the video format is SECAM */
	v4l2_ctrl_grab(core->chroma_agc, cxiformat == VideoFormatSECAM);

	// done
	return 0;
}

/* ------------------------------------------------------------------ */

struct video_device *cx88_vdev_init(struct cx88_core *core,
				    struct pci_dev *pci,
				    const struct video_device *template_,
				    const char *type)
{
	struct video_device *vfd;

	vfd = video_device_alloc();
	if (NULL == vfd)
		return NULL;
	*vfd = *template_;
	vfd->v4l2_dev = &core->v4l2_dev;
	vfd->release = video_device_release;
	snprintf(vfd->name, sizeof(vfd->name), "%s %s (%s)",
		 core->name, type, core->board.name);
	return vfd;
}

struct cx88_core* cx88_core_get(struct pci_dev *pci)
{
	struct cx88_core *core;

	mutex_lock(&devlist);
	list_for_each_entry(core, &cx88_devlist, devlist) {
		if (pci->bus->number != core->pci_bus)
			continue;
		if (PCI_SLOT(pci->devfn) != core->pci_slot)
			continue;

		if (0 != cx88_get_resources(core, pci)) {
			mutex_unlock(&devlist);
			return NULL;
		}
		atomic_inc(&core->refcount);
		mutex_unlock(&devlist);
		return core;
	}

	core = cx88_core_create(pci, cx88_devcount);
	if (NULL != core) {
		cx88_devcount++;
		list_add_tail(&core->devlist, &cx88_devlist);
	}

	mutex_unlock(&devlist);
	return core;
}

void cx88_core_put(struct cx88_core *core, struct pci_dev *pci)
{
	release_mem_region(pci_resource_start(pci,0),
			   pci_resource_len(pci,0));

	if (!atomic_dec_and_test(&core->refcount))
		return;

	mutex_lock(&devlist);
	cx88_ir_fini(core);
	if (0 == core->i2c_rc) {
		if (core->i2c_rtc)
			i2c_unregister_device(core->i2c_rtc);
		i2c_del_adapter(&core->i2c_adap);
	}
	list_del(&core->devlist);
	iounmap(core->lmmio);
	cx88_devcount--;
	mutex_unlock(&devlist);
	v4l2_ctrl_handler_free(&core->video_hdl);
	v4l2_ctrl_handler_free(&core->audio_hdl);
	v4l2_device_unregister(&core->v4l2_dev);
	kfree(core);
}

/* ------------------------------------------------------------------ */

EXPORT_SYMBOL(cx88_print_irqbits);

EXPORT_SYMBOL(cx88_core_irq);
EXPORT_SYMBOL(cx88_wakeup);
EXPORT_SYMBOL(cx88_reset);
EXPORT_SYMBOL(cx88_shutdown);

EXPORT_SYMBOL(cx88_risc_buffer);
EXPORT_SYMBOL(cx88_risc_databuffer);
EXPORT_SYMBOL(cx88_risc_stopper);
EXPORT_SYMBOL(cx88_free_buffer);

EXPORT_SYMBOL(cx88_sram_channels);
EXPORT_SYMBOL(cx88_sram_channel_setup);
EXPORT_SYMBOL(cx88_sram_channel_dump);

EXPORT_SYMBOL(cx88_set_tvnorm);
EXPORT_SYMBOL(cx88_set_scale);

EXPORT_SYMBOL(cx88_vdev_init);
EXPORT_SYMBOL(cx88_core_get);
EXPORT_SYMBOL(cx88_core_put);

EXPORT_SYMBOL(cx88_ir_start);
EXPORT_SYMBOL(cx88_ir_stop);

/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 * kate: eol "unix"; indent-width 3; remove-trailing-space on; replace-trailing-space-save on; tab-width 8; replace-tabs off; space-indent off; mixed-indent off
 */
