/*
 * comedi/drivers/pcl812.c
 *
 * Author: Michal Dobes <dobes@tesnet.cz>
 *
 * hardware driver for Advantech cards
 *  card:   PCL-812, PCL-812PG, PCL-813, PCL-813B
 *  driver: pcl812,  pcl812pg,  pcl813,  pcl813b
 * and for ADlink cards
 *  card:   ACL-8112DG, ACL-8112HG, ACL-8112PG, ACL-8113, ACL-8216
 *  driver: acl8112dg,  acl8112hg,  acl8112pg,  acl8113,  acl8216
 * and for ICP DAS cards
 *  card:   ISO-813, A-821PGH, A-821PGL, A-821PGL-NDA, A-822PGH, A-822PGL,
 *  driver: iso813,  a821pgh,  a-821pgl, a-821pglnda,  a822pgh,  a822pgl,
 *  card:   A-823PGH, A-823PGL, A-826PG
 * driver:  a823pgh,  a823pgl,  a826pg
 */
/*
Driver: pcl812
Description: Advantech PCL-812/PG, PCL-813/B,
             ADLink ACL-8112DG/HG/PG, ACL-8113, ACL-8216,
             ICP DAS A-821PGH/PGL/PGL-NDA, A-822PGH/PGL, A-823PGH/PGL, A-826PG,
             ICP DAS ISO-813
Author: Michal Dobes <dobes@tesnet.cz>
Devices: [Advantech] PCL-812 (pcl812), PCL-812PG (pcl812pg),
  PCL-813 (pcl813), PCL-813B (pcl813b), [ADLink] ACL-8112DG (acl8112dg),
  ACL-8112HG (acl8112hg), ACL-8113 (acl-8113), ACL-8216 (acl8216),
  [ICP] ISO-813 (iso813), A-821PGH (a821pgh), A-821PGL (a821pgl),
  A-821PGL-NDA (a821pclnda), A-822PGH (a822pgh), A-822PGL (a822pgl),
  A-823PGH (a823pgh), A-823PGL (a823pgl), A-826PG (a826pg)
Updated: Mon, 06 Aug 2007 12:03:15 +0100
Status: works (I hope. My board fire up under my hands
               and I cann't test all features.)

This driver supports insn and cmd interfaces. Some boards support only insn
becouse their hardware don't allow more (PCL-813/B, ACL-8113, ISO-813).
Data transfer over DMA is supported only when you measure only one
channel, this is too hardware limitation of these boards.

Options for PCL-812:
  [0] - IO Base
  [1] - IRQ  (0=disable, 2, 3, 4, 5, 6, 7; 10, 11, 12, 14, 15)
  [2] - DMA  (0=disable, 1, 3)
  [3] - 0=trigger source is internal 8253 with 2MHz clock
        1=trigger source is external
  [4] - 0=A/D input range is +/-10V
        1=A/D input range is +/-5V
        2=A/D input range is +/-2.5V
        3=A/D input range is +/-1.25V
        4=A/D input range is +/-0.625V
        5=A/D input range is +/-0.3125V
  [5] - 0=D/A outputs 0-5V  (internal reference -5V)
        1=D/A outputs 0-10V (internal reference -10V)
        2=D/A outputs unknow (external reference)

Options for PCL-812PG, ACL-8112PG:
  [0] - IO Base
  [1] - IRQ  (0=disable, 2, 3, 4, 5, 6, 7; 10, 11, 12, 14, 15)
  [2] - DMA  (0=disable, 1, 3)
  [3] - 0=trigger source is internal 8253 with 2MHz clock
        1=trigger source is external
  [4] - 0=A/D have max +/-5V input
        1=A/D have max +/-10V input
  [5] - 0=D/A outputs 0-5V  (internal reference -5V)
        1=D/A outputs 0-10V (internal reference -10V)
        2=D/A outputs unknow (external reference)

Options for ACL-8112DG/HG, A-822PGL/PGH, A-823PGL/PGH, ACL-8216, A-826PG:
  [0] - IO Base
  [1] - IRQ  (0=disable, 2, 3, 4, 5, 6, 7; 10, 11, 12, 14, 15)
  [2] - DMA  (0=disable, 1, 3)
  [3] - 0=trigger source is internal 8253 with 2MHz clock
        1=trigger source is external
  [4] - 0=A/D channels are S.E.
        1=A/D channels are DIFF
  [5] - 0=D/A outputs 0-5V  (internal reference -5V)
        1=D/A outputs 0-10V (internal reference -10V)
        2=D/A outputs unknow (external reference)

Options for A-821PGL/PGH:
  [0] - IO Base
  [1] - IRQ  (0=disable, 2, 3, 4, 5, 6, 7)
  [2] - 0=A/D channels are S.E.
        1=A/D channels are DIFF
  [3] - 0=D/A output 0-5V  (internal reference -5V)
        1=D/A output 0-10V (internal reference -10V)

Options for A-821PGL-NDA:
  [0] - IO Base
  [1] - IRQ  (0=disable, 2, 3, 4, 5, 6, 7)
  [2] - 0=A/D channels are S.E.
        1=A/D channels are DIFF

Options for PCL-813:
  [0] - IO Base

Options for PCL-813B:
  [0] - IO Base
  [1] - 0= bipolar inputs
        1= unipolar inputs

Options for ACL-8113, ISO-813:
  [0] - IO Base
  [1] - 0= 10V bipolar inputs
        1= 10V unipolar inputs
        2= 20V bipolar inputs
        3= 20V unipolar inputs
*/

#include "../comedidev.h"

#include <linux/delay.h>
#include <linux/ioport.h>
#include <asm/dma.h>

#include "8253.h"

#undef PCL812_EXTDEBUG		/* if this is defined then a lot of messages is printed */

/* hardware types of the cards */
#define boardPCL812PG 		 0	/* and ACL-8112PG */
#define boardPCL813B 		 1
#define boardPCL812		 2
#define boardPCL813 		 3
#define boardISO813 		 5
#define boardACL8113 		 6
#define boardACL8112 		 7	/* ACL-8112DG/HG, A-822PGL/PGH, A-823PGL/PGH */
#define boardACL8216		 8	/* and ICP DAS A-826PG */
#define boardA821		 9	/* PGH, PGL, PGL/NDA versions */

#define PCLx1x_IORANGE 		16

#define PCL812_CTR0		 0
#define PCL812_CTR1		 1
#define PCL812_CTR2		 2
#define PCL812_CTRCTL		 3
#define PCL812_AD_LO		 4
#define PCL812_DA1_LO		 4
#define PCL812_AD_HI		 5
#define PCL812_DA1_HI		 5
#define PCL812_DA2_LO		 6
#define PCL812_DI_LO		 6
#define PCL812_DA2_HI		 7
#define PCL812_DI_HI		 7
#define PCL812_CLRINT		 8
#define PCL812_GAIN		 9
#define PCL812_MUX		10
#define PCL812_MODE		11
#define PCL812_CNTENABLE 	10
#define PCL812_SOFTTRIG 	12
#define PCL812_DO_LO		13
#define PCL812_DO_HI 		14

#define PCL812_DRDY 		0x10	/* =0 data ready */

#define ACL8216_STATUS 		 8	/* 5. bit signalize data ready */

#define ACL8216_DRDY 		0x20	/* =0 data ready */

#define MAX_CHANLIST_LEN	256	/* length of scan list */

static const struct comedi_lrange range_pcl812pg_ai = { 5, {
			BIP_RANGE(5),
			BIP_RANGE(2.5),
			BIP_RANGE(1.25),
			BIP_RANGE(0.625),
			BIP_RANGE(0.3125),
	}
};
static const struct comedi_lrange range_pcl812pg2_ai = { 5, {
			BIP_RANGE(10),
			BIP_RANGE(5),
			BIP_RANGE(2.5),
			BIP_RANGE(1.25),
			BIP_RANGE(0.625),
	}
};
static const struct comedi_lrange range812_bipolar1_25 = { 1, {
			BIP_RANGE(1.25),
	}
};
static const struct comedi_lrange range812_bipolar0_625 = { 1, {
			BIP_RANGE(0.625),
	}
};
static const struct comedi_lrange range812_bipolar0_3125 = { 1, {
			BIP_RANGE(0.3125),
	}
};
static const struct comedi_lrange range_pcl813b_ai = { 4, {
			BIP_RANGE(5),
			BIP_RANGE(2.5),
			BIP_RANGE(1.25),
			BIP_RANGE(0.625),
	}
};
static const struct comedi_lrange range_pcl813b2_ai = { 4, {
			UNI_RANGE(10),
			UNI_RANGE(5),
			UNI_RANGE(2.5),
			UNI_RANGE(1.25),
	}
};
static const struct comedi_lrange range_iso813_1_ai = { 5, {
			BIP_RANGE(5),
			BIP_RANGE(2.5),
			BIP_RANGE(1.25),
			BIP_RANGE(0.625),
			BIP_RANGE(0.3125),
	}
};
static const struct comedi_lrange range_iso813_1_2_ai = { 5, {
			UNI_RANGE(10),
			UNI_RANGE(5),
			UNI_RANGE(2.5),
			UNI_RANGE(1.25),
			UNI_RANGE(0.625),
	}
};
static const struct comedi_lrange range_iso813_2_ai = { 4, {
			BIP_RANGE(5),
			BIP_RANGE(2.5),
			BIP_RANGE(1.25),
			BIP_RANGE(0.625),
	}
};
static const struct comedi_lrange range_iso813_2_2_ai = { 4, {
			UNI_RANGE(10),
			UNI_RANGE(5),
			UNI_RANGE(2.5),
			UNI_RANGE(1.25),
	}
};
static const struct comedi_lrange range_acl8113_1_ai = { 4, {
			BIP_RANGE(5),
			BIP_RANGE(2.5),
			BIP_RANGE(1.25),
			BIP_RANGE(0.625),
	}
};
static const struct comedi_lrange range_acl8113_1_2_ai = { 4, {
			UNI_RANGE(10),
			UNI_RANGE(5),
			UNI_RANGE(2.5),
			UNI_RANGE(1.25),
	}
};
static const struct comedi_lrange range_acl8113_2_ai = { 3, {
			BIP_RANGE(5),
			BIP_RANGE(2.5),
			BIP_RANGE(1.25),
	}
};
static const struct comedi_lrange range_acl8113_2_2_ai = { 3, {
			UNI_RANGE(10),
			UNI_RANGE(5),
			UNI_RANGE(2.5),
	}
};
static const struct comedi_lrange range_acl8112dg_ai = { 9, {
			BIP_RANGE(5),
			BIP_RANGE(2.5),
			BIP_RANGE(1.25),
			BIP_RANGE(0.625),
			UNI_RANGE(10),
			UNI_RANGE(5),
			UNI_RANGE(2.5),
			UNI_RANGE(1.25),
			BIP_RANGE(10),
	}
};
static const struct comedi_lrange range_acl8112hg_ai = { 12, {
			BIP_RANGE(5),
			BIP_RANGE(0.5),
			BIP_RANGE(0.05),
			BIP_RANGE(0.005),
			UNI_RANGE(10),
			UNI_RANGE(1),
			UNI_RANGE(0.1),
			UNI_RANGE(0.01),
			BIP_RANGE(10),
			BIP_RANGE(1),
			BIP_RANGE(0.1),
			BIP_RANGE(0.01),
	}
};
static const struct comedi_lrange range_a821pgh_ai = { 4, {
			BIP_RANGE(5),
			BIP_RANGE(0.5),
			BIP_RANGE(0.05),
			BIP_RANGE(0.005),
	}
};

static int pcl812_attach(struct comedi_device *dev, struct comedi_devconfig *it);
static int pcl812_detach(struct comedi_device *dev);

struct pcl812_board {

	const char *name;	/*  board name */
	int board_type;		/*  type of this board */
	int n_aichan;		/*  num of AI chans in S.E. */
	int n_aichan_diff;	/*  DIFF num of chans */
	int n_aochan;		/*  num of DA chans */
	int n_dichan;		/*  DI and DO chans */
	int n_dochan;
	int ai_maxdata;		/*  AI resolution */
	unsigned int ai_ns_min;	/*  max sample speed of card v ns */
	unsigned int i8254_osc_base;	/*  clock base */
	const struct comedi_lrange *rangelist_ai;	/*  rangelist for A/D */
	const struct comedi_lrange *rangelist_ao;	/*  rangelist for D/A */
	unsigned int IRQbits;	/*  allowed IRQ */
	unsigned char DMAbits;	/*  allowed DMA chans */
	unsigned char io_range;	/*  iorange for this board */
	unsigned char haveMPC508;	/*  1=board use MPC508A multiplexor */
};


static const struct pcl812_board boardtypes[] = {
	{"pcl812", boardPCL812, 16, 0, 2, 16, 16, 0x0fff,
			33000, 500, &range_bipolar10, &range_unipolar5,
		0xdcfc, 0x0a, PCLx1x_IORANGE, 0},
	{"pcl812pg", boardPCL812PG, 16, 0, 2, 16, 16, 0x0fff,
			33000, 500, &range_pcl812pg_ai, &range_unipolar5,
		0xdcfc, 0x0a, PCLx1x_IORANGE, 0},
	{"acl8112pg", boardPCL812PG, 16, 0, 2, 16, 16, 0x0fff,
			10000, 500, &range_pcl812pg_ai, &range_unipolar5,
		0xdcfc, 0x0a, PCLx1x_IORANGE, 0},
	{"acl8112dg", boardACL8112, 16, 8, 2, 16, 16, 0x0fff,
			10000, 500, &range_acl8112dg_ai, &range_unipolar5,
		0xdcfc, 0x0a, PCLx1x_IORANGE, 1},
	{"acl8112hg", boardACL8112, 16, 8, 2, 16, 16, 0x0fff,
			10000, 500, &range_acl8112hg_ai, &range_unipolar5,
		0xdcfc, 0x0a, PCLx1x_IORANGE, 1},
	{"a821pgl", boardA821, 16, 8, 1, 16, 16, 0x0fff,
			10000, 500, &range_pcl813b_ai, &range_unipolar5,
		0x000c, 0x00, PCLx1x_IORANGE, 0},
	{"a821pglnda", boardA821, 16, 8, 0, 0, 0, 0x0fff,
			10000, 500, &range_pcl813b_ai, NULL,
		0x000c, 0x00, PCLx1x_IORANGE, 0},
	{"a821pgh", boardA821, 16, 8, 1, 16, 16, 0x0fff,
			10000, 500, &range_a821pgh_ai, &range_unipolar5,
		0x000c, 0x00, PCLx1x_IORANGE, 0},
	{"a822pgl", boardACL8112, 16, 8, 2, 16, 16, 0x0fff,
			10000, 500, &range_acl8112dg_ai, &range_unipolar5,
		0xdcfc, 0x0a, PCLx1x_IORANGE, 0},
	{"a822pgh", boardACL8112, 16, 8, 2, 16, 16, 0x0fff,
			10000, 500, &range_acl8112hg_ai, &range_unipolar5,
		0xdcfc, 0x0a, PCLx1x_IORANGE, 0},
	{"a823pgl", boardACL8112, 16, 8, 2, 16, 16, 0x0fff,
			8000, 500, &range_acl8112dg_ai, &range_unipolar5,
		0xdcfc, 0x0a, PCLx1x_IORANGE, 0},
	{"a823pgh", boardACL8112, 16, 8, 2, 16, 16, 0x0fff,
			8000, 500, &range_acl8112hg_ai, &range_unipolar5,
		0xdcfc, 0x0a, PCLx1x_IORANGE, 0},
	{"pcl813", boardPCL813, 32, 0, 0, 0, 0, 0x0fff,
			0, 0, &range_pcl813b_ai, NULL,
		0x0000, 0x00, PCLx1x_IORANGE, 0},
	{"pcl813b", boardPCL813B, 32, 0, 0, 0, 0, 0x0fff,
			0, 0, &range_pcl813b_ai, NULL,
		0x0000, 0x00, PCLx1x_IORANGE, 0},
	{"acl8113", boardACL8113, 32, 0, 0, 0, 0, 0x0fff,
			0, 0, &range_acl8113_1_ai, NULL,
		0x0000, 0x00, PCLx1x_IORANGE, 0},
	{"iso813", boardISO813, 32, 0, 0, 0, 0, 0x0fff,
			0, 0, &range_iso813_1_ai, NULL,
		0x0000, 0x00, PCLx1x_IORANGE, 0},
	{"acl8216", boardACL8216, 16, 8, 2, 16, 16, 0xffff,
			10000, 500, &range_pcl813b2_ai, &range_unipolar5,
		0xdcfc, 0x0a, PCLx1x_IORANGE, 1},
	{"a826pg", boardACL8216, 16, 8, 2, 16, 16, 0xffff,
			10000, 500, &range_pcl813b2_ai, &range_unipolar5,
		0xdcfc, 0x0a, PCLx1x_IORANGE, 0},
};

#define n_boardtypes (sizeof(boardtypes)/sizeof(struct pcl812_board))
#define this_board ((const struct pcl812_board *)dev->board_ptr)

static struct comedi_driver driver_pcl812 = {
      driver_name:"pcl812",
      module:THIS_MODULE,
      attach:pcl812_attach,
      detach:pcl812_detach,
      board_name:&boardtypes[0].name,
      num_names:n_boardtypes,
      offset:sizeof(struct pcl812_board),
};

COMEDI_INITCLEANUP(driver_pcl812);

struct pcl812_private {

	unsigned char valid;	/*  =1 device is OK */
	unsigned char dma;	/*  >0 use dma ( usedDMA channel) */
	unsigned char use_diff;	/*  =1 diff inputs */
	unsigned char use_MPC;	/*  1=board uses MPC508A multiplexor */
	unsigned char use_ext_trg;	/*  1=board uses external trigger */
	unsigned char range_correction;	/*  =1 we must add 1 to range number */
	unsigned char old_chan_reg;	/*  lastly used chan/gain pair */
	unsigned char old_gain_reg;
	unsigned char mode_reg_int;	/*  there is stored INT number for some card */
	unsigned char ai_neverending;	/*  =1 we do unlimited AI */
	unsigned char ai_eos;	/*  1=EOS wake up */
	unsigned char ai_dma;	/*  =1 we use DMA */
	unsigned int ai_poll_ptr;	/*  how many sampes transfer poll */
	unsigned int ai_scans;	/*  len of scanlist */
	unsigned int ai_act_scan;	/*  how many scans we finished */
	unsigned int ai_chanlist[MAX_CHANLIST_LEN];	/*  our copy of channel/range list */
	unsigned int ai_n_chan;	/*  how many channels is measured */
	unsigned int ai_flags;	/*  flaglist */
	unsigned int ai_data_len;	/*  len of data buffer */
	short *ai_data;	/*  data buffer */
	unsigned int ai_is16b;	/*  =1 we have 16 bit card */
	unsigned long dmabuf[2];	/*  PTR to DMA buf */
	unsigned int dmapages[2];	/*  how many pages we have allocated */
	unsigned int hwdmaptr[2];	/*  HW PTR to DMA buf */
	unsigned int hwdmasize[2];	/*  DMA buf size in bytes */
	unsigned int dmabytestomove[2];	/*  how many bytes DMA transfer */
	int next_dma_buf;	/*  which buffer is next to use */
	unsigned int dma_runs_to_end;	/*  how many times we must switch DMA buffers */
	unsigned int last_dma_run;	/*  how many bytes to transfer on last DMA buffer */
	unsigned int max_812_ai_mode0_rangewait;	/*  setling time for gain */
	unsigned int ao_readback[2];	/*  data for AO readback */
};


#define devpriv ((struct pcl812_private *)dev->private)

/*
==============================================================================
*/
static void start_pacer(struct comedi_device *dev, int mode, unsigned int divisor1,
	unsigned int divisor2);
static void setup_range_channel(struct comedi_device *dev, struct comedi_subdevice *s,
	unsigned int rangechan, char wait);
static int pcl812_ai_cancel(struct comedi_device *dev, struct comedi_subdevice *s);
/*
==============================================================================
*/
static int pcl812_ai_insn_read(struct comedi_device *dev, struct comedi_subdevice *s,
	struct comedi_insn *insn, unsigned int *data)
{
	int n;
	int timeout, hi;

	outb(devpriv->mode_reg_int | 1, dev->iobase + PCL812_MODE);	/* select software trigger */
	setup_range_channel(dev, s, insn->chanspec, 1);	/*  select channel and renge */
	for (n = 0; n < insn->n; n++) {
		outb(255, dev->iobase + PCL812_SOFTTRIG);	/* start conversion */
		comedi_udelay(5);
		timeout = 50;	/* wait max 50us, it must finish under 33us */
		while (timeout--) {
			hi = inb(dev->iobase + PCL812_AD_HI);
			if (!(hi & PCL812_DRDY))
				goto conv_finish;
			comedi_udelay(1);
		}
		rt_printk
			("comedi%d: pcl812: (%s at 0x%lx) A/D insn read timeout\n",
			dev->minor, dev->board_name, dev->iobase);
		outb(devpriv->mode_reg_int | 0, dev->iobase + PCL812_MODE);
		return -ETIME;

	      conv_finish:
		data[n] = ((hi & 0xf) << 8) | inb(dev->iobase + PCL812_AD_LO);
	}
	outb(devpriv->mode_reg_int | 0, dev->iobase + PCL812_MODE);
	return n;
}

/*
==============================================================================
*/
static int acl8216_ai_insn_read(struct comedi_device *dev, struct comedi_subdevice *s,
	struct comedi_insn *insn, unsigned int *data)
{
	int n;
	int timeout;

	outb(1, dev->iobase + PCL812_MODE);	/* select software trigger */
	setup_range_channel(dev, s, insn->chanspec, 1);	/*  select channel and renge */
	for (n = 0; n < insn->n; n++) {
		outb(255, dev->iobase + PCL812_SOFTTRIG);	/* start conversion */
		comedi_udelay(5);
		timeout = 50;	/* wait max 50us, it must finish under 33us */
		while (timeout--) {
			if (!(inb(dev->iobase + ACL8216_STATUS) & ACL8216_DRDY))
				goto conv_finish;
			comedi_udelay(1);
		}
		rt_printk
			("comedi%d: pcl812: (%s at 0x%lx) A/D insn read timeout\n",
			dev->minor, dev->board_name, dev->iobase);
		outb(0, dev->iobase + PCL812_MODE);
		return -ETIME;

	      conv_finish:
		data[n] =
			(inb(dev->iobase +
				PCL812_AD_HI) << 8) | inb(dev->iobase +
			PCL812_AD_LO);
	}
	outb(0, dev->iobase + PCL812_MODE);
	return n;
}

/*
==============================================================================
*/
static int pcl812_ao_insn_write(struct comedi_device *dev, struct comedi_subdevice *s,
	struct comedi_insn *insn, unsigned int *data)
{
	int chan = CR_CHAN(insn->chanspec);
	int i;

	for (i = 0; i < insn->n; i++) {
		outb((data[i] & 0xff),
			dev->iobase + (chan ? PCL812_DA2_LO : PCL812_DA1_LO));
		outb((data[i] >> 8) & 0x0f,
			dev->iobase + (chan ? PCL812_DA2_HI : PCL812_DA1_HI));
		devpriv->ao_readback[chan] = data[i];
	}

	return i;
}

/*
==============================================================================
*/
static int pcl812_ao_insn_read(struct comedi_device *dev, struct comedi_subdevice *s,
	struct comedi_insn *insn, unsigned int *data)
{
	int chan = CR_CHAN(insn->chanspec);
	int i;

	for (i = 0; i < insn->n; i++) {
		data[i] = devpriv->ao_readback[chan];
	}

	return i;
}

/*
==============================================================================
*/
static int pcl812_di_insn_bits(struct comedi_device *dev, struct comedi_subdevice *s,
	struct comedi_insn *insn, unsigned int *data)
{
	if (insn->n != 2)
		return -EINVAL;

	data[1] = inb(dev->iobase + PCL812_DI_LO);
	data[1] |= inb(dev->iobase + PCL812_DI_HI) << 8;

	return 2;
}

/*
==============================================================================
*/
static int pcl812_do_insn_bits(struct comedi_device *dev, struct comedi_subdevice *s,
	struct comedi_insn *insn, unsigned int *data)
{
	if (insn->n != 2)
		return -EINVAL;

	if (data[0]) {
		s->state &= ~data[0];
		s->state |= data[0] & data[1];
		outb(s->state & 0xff, dev->iobase + PCL812_DO_LO);
		outb((s->state >> 8), dev->iobase + PCL812_DO_HI);
	}
	data[1] = s->state;

	return 2;
}

#ifdef PCL812_EXTDEBUG
/*
==============================================================================
*/
static void pcl812_cmdtest_out(int e, struct comedi_cmd *cmd)
{
	rt_printk("pcl812 e=%d startsrc=%x scansrc=%x convsrc=%x\n", e,
		cmd->start_src, cmd->scan_begin_src, cmd->convert_src);
	rt_printk("pcl812 e=%d startarg=%d scanarg=%d convarg=%d\n", e,
		cmd->start_arg, cmd->scan_begin_arg, cmd->convert_arg);
	rt_printk("pcl812 e=%d stopsrc=%x scanend=%x\n", e, cmd->stop_src,
		cmd->scan_end_src);
	rt_printk("pcl812 e=%d stoparg=%d scanendarg=%d chanlistlen=%d\n", e,
		cmd->stop_arg, cmd->scan_end_arg, cmd->chanlist_len);
}
#endif

/*
==============================================================================
*/
static int pcl812_ai_cmdtest(struct comedi_device *dev, struct comedi_subdevice *s,
	struct comedi_cmd *cmd)
{
	int err = 0;
	int tmp, divisor1, divisor2;

#ifdef PCL812_EXTDEBUG
	rt_printk("pcl812 EDBG: BGN: pcl812_ai_cmdtest(...)\n");
	pcl812_cmdtest_out(-1, cmd);
#endif
	/* step 1: make sure trigger sources are trivially valid */

	tmp = cmd->start_src;
	cmd->start_src &= TRIG_NOW;
	if (!cmd->start_src || tmp != cmd->start_src)
		err++;

	tmp = cmd->scan_begin_src;
	cmd->scan_begin_src &= TRIG_FOLLOW;
	if (!cmd->scan_begin_src || tmp != cmd->scan_begin_src)
		err++;

	tmp = cmd->convert_src;
	if (devpriv->use_ext_trg) {
		cmd->convert_src &= TRIG_EXT;
	} else {
		cmd->convert_src &= TRIG_TIMER;
	}
	if (!cmd->convert_src || tmp != cmd->convert_src)
		err++;

	tmp = cmd->scan_end_src;
	cmd->scan_end_src &= TRIG_COUNT;
	if (!cmd->scan_end_src || tmp != cmd->scan_end_src)
		err++;

	tmp = cmd->stop_src;
	cmd->stop_src &= TRIG_COUNT | TRIG_NONE;
	if (!cmd->stop_src || tmp != cmd->stop_src)
		err++;

	if (err) {
#ifdef PCL812_EXTDEBUG
		pcl812_cmdtest_out(1, cmd);
		rt_printk
			("pcl812 EDBG: BGN: pcl812_ai_cmdtest(...) err=%d ret=1\n",
			err);
#endif
		return 1;
	}

	/* step 2: make sure trigger sources are unique and mutually compatible */

	if (cmd->start_src != TRIG_NOW) {
		cmd->start_src = TRIG_NOW;
		err++;
	}

	if (cmd->scan_begin_src != TRIG_FOLLOW) {
		cmd->scan_begin_src = TRIG_FOLLOW;
		err++;
	}

	if (devpriv->use_ext_trg) {
		if (cmd->convert_src != TRIG_EXT) {
			cmd->convert_src = TRIG_EXT;
			err++;
		}
	} else {
		if (cmd->convert_src != TRIG_TIMER) {
			cmd->convert_src = TRIG_TIMER;
			err++;
		}
	}

	if (cmd->scan_end_src != TRIG_COUNT) {
		cmd->scan_end_src = TRIG_COUNT;
		err++;
	}

	if (cmd->stop_src != TRIG_NONE && cmd->stop_src != TRIG_COUNT)
		err++;

	if (err) {
#ifdef PCL812_EXTDEBUG
		pcl812_cmdtest_out(2, cmd);
		rt_printk
			("pcl812 EDBG: BGN: pcl812_ai_cmdtest(...) err=%d ret=2\n",
			err);
#endif
		return 2;
	}

	/* step 3: make sure arguments are trivially compatible */

	if (cmd->start_arg != 0) {
		cmd->start_arg = 0;
		err++;
	}

	if (cmd->scan_begin_arg != 0) {
		cmd->scan_begin_arg = 0;
		err++;
	}

	if (cmd->convert_src == TRIG_TIMER) {
		if (cmd->convert_arg < this_board->ai_ns_min) {
			cmd->convert_arg = this_board->ai_ns_min;
			err++;
		}
	} else {		/* TRIG_EXT */
		if (cmd->convert_arg != 0) {
			cmd->convert_arg = 0;
			err++;
		}
	}

	if (!cmd->chanlist_len) {
		cmd->chanlist_len = 1;
		err++;
	}
	if (cmd->chanlist_len > MAX_CHANLIST_LEN) {
		cmd->chanlist_len = this_board->n_aichan;
		err++;
	}
	if (cmd->scan_end_arg != cmd->chanlist_len) {
		cmd->scan_end_arg = cmd->chanlist_len;
		err++;
	}
	if (cmd->stop_src == TRIG_COUNT) {
		if (!cmd->stop_arg) {
			cmd->stop_arg = 1;
			err++;
		}
	} else {		/* TRIG_NONE */
		if (cmd->stop_arg != 0) {
			cmd->stop_arg = 0;
			err++;
		}
	}

	if (err) {
#ifdef PCL812_EXTDEBUG
		pcl812_cmdtest_out(3, cmd);
		rt_printk
			("pcl812 EDBG: BGN: pcl812_ai_cmdtest(...) err=%d ret=3\n",
			err);
#endif
		return 3;
	}

	/* step 4: fix up any arguments */

	if (cmd->convert_src == TRIG_TIMER) {
		tmp = cmd->convert_arg;
		i8253_cascade_ns_to_timer(this_board->i8254_osc_base, &divisor1,
			&divisor2, &cmd->convert_arg,
			cmd->flags & TRIG_ROUND_MASK);
		if (cmd->convert_arg < this_board->ai_ns_min)
			cmd->convert_arg = this_board->ai_ns_min;
		if (tmp != cmd->convert_arg)
			err++;
	}

	if (err) {
#ifdef PCL812_EXTDEBUG
		rt_printk
			("pcl812 EDBG: BGN: pcl812_ai_cmdtest(...) err=%d ret=4\n",
			err);
#endif
		return 4;
	}

	return 0;
}

/*
==============================================================================
*/
static int pcl812_ai_cmd(struct comedi_device *dev, struct comedi_subdevice *s)
{
	unsigned int divisor1 = 0, divisor2 = 0, i, dma_flags, bytes;
	struct comedi_cmd *cmd = &s->async->cmd;

#ifdef PCL812_EXTDEBUG
	rt_printk("pcl812 EDBG: BGN: pcl812_ai_cmd(...)\n");
#endif

	if (cmd->start_src != TRIG_NOW)
		return -EINVAL;
	if (cmd->scan_begin_src != TRIG_FOLLOW)
		return -EINVAL;
	if (devpriv->use_ext_trg) {
		if (cmd->convert_src != TRIG_EXT)
			return -EINVAL;
	} else {
		if (cmd->convert_src != TRIG_TIMER)
			return -EINVAL;
	}
	if (cmd->scan_end_src != TRIG_COUNT)
		return -EINVAL;
	if (cmd->scan_end_arg != cmd->chanlist_len)
		return -EINVAL;
	if (cmd->chanlist_len > MAX_CHANLIST_LEN)
		return -EINVAL;

	if (cmd->convert_src == TRIG_TIMER) {
		if (cmd->convert_arg < this_board->ai_ns_min)
			cmd->convert_arg = this_board->ai_ns_min;
		i8253_cascade_ns_to_timer(this_board->i8254_osc_base,
			&divisor1, &divisor2, &cmd->convert_arg,
			cmd->flags & TRIG_ROUND_MASK);
	}

	start_pacer(dev, -1, 0, 0);	/*  stop pacer */

	devpriv->ai_n_chan = cmd->chanlist_len;
	memcpy(devpriv->ai_chanlist, cmd->chanlist,
		sizeof(unsigned int) * cmd->scan_end_arg);
	setup_range_channel(dev, s, devpriv->ai_chanlist[0], 1);	/*  select first channel and range */

	if (devpriv->dma) {	/*  check if we can use DMA transfer */
		devpriv->ai_dma = 1;
		for (i = 1; i < devpriv->ai_n_chan; i++)
			if (devpriv->ai_chanlist[0] != devpriv->ai_chanlist[i]) {
				devpriv->ai_dma = 0;	/*  we cann't use DMA :-( */
				break;
			}
	} else
		devpriv->ai_dma = 0;

	devpriv->ai_flags = cmd->flags;
	devpriv->ai_data_len = s->async->prealloc_bufsz;
	devpriv->ai_data = s->async->prealloc_buf;
	if (cmd->stop_src == TRIG_COUNT) {
		devpriv->ai_scans = cmd->stop_arg;
		devpriv->ai_neverending = 0;
	} else {
		devpriv->ai_scans = 0;
		devpriv->ai_neverending = 1;
	}

	devpriv->ai_act_scan = 0;
	devpriv->ai_poll_ptr = 0;
	s->async->cur_chan = 0;

	if ((devpriv->ai_flags & TRIG_WAKE_EOS)) {	/*  don't we want wake up every scan? */
		devpriv->ai_eos = 1;
		if (devpriv->ai_n_chan == 1)
			devpriv->ai_dma = 0;	/*  DMA is useless for this situation */
	}

	if (devpriv->ai_dma) {
		if (devpriv->ai_eos) {	/*  we use EOS, so adapt DMA buffer to one scan */
			devpriv->dmabytestomove[0] =
				devpriv->ai_n_chan * sizeof(short);
			devpriv->dmabytestomove[1] =
				devpriv->ai_n_chan * sizeof(short);
			devpriv->dma_runs_to_end = 1;
		} else {
			devpriv->dmabytestomove[0] = devpriv->hwdmasize[0];
			devpriv->dmabytestomove[1] = devpriv->hwdmasize[1];
			if (devpriv->ai_data_len < devpriv->hwdmasize[0])
				devpriv->dmabytestomove[0] =
					devpriv->ai_data_len;
			if (devpriv->ai_data_len < devpriv->hwdmasize[1])
				devpriv->dmabytestomove[1] =
					devpriv->ai_data_len;
			if (devpriv->ai_neverending) {
				devpriv->dma_runs_to_end = 1;
			} else {
				bytes = devpriv->ai_n_chan * devpriv->ai_scans * sizeof(short);	/*  how many samples we must transfer? */
				devpriv->dma_runs_to_end = bytes / devpriv->dmabytestomove[0];	/*  how many DMA pages we must fill */
				devpriv->last_dma_run = bytes % devpriv->dmabytestomove[0];	/* on last dma transfer must be moved */
				if (devpriv->dma_runs_to_end == 0)
					devpriv->dmabytestomove[0] =
						devpriv->last_dma_run;
				devpriv->dma_runs_to_end--;
			}
		}
		if (devpriv->dmabytestomove[0] > devpriv->hwdmasize[0]) {
			devpriv->dmabytestomove[0] = devpriv->hwdmasize[0];
			devpriv->ai_eos = 0;
		}
		if (devpriv->dmabytestomove[1] > devpriv->hwdmasize[1]) {
			devpriv->dmabytestomove[1] = devpriv->hwdmasize[1];
			devpriv->ai_eos = 0;
		}
		devpriv->next_dma_buf = 0;
		set_dma_mode(devpriv->dma, DMA_MODE_READ);
		dma_flags = claim_dma_lock();
		clear_dma_ff(devpriv->dma);
		set_dma_addr(devpriv->dma, devpriv->hwdmaptr[0]);
		set_dma_count(devpriv->dma, devpriv->dmabytestomove[0]);
		release_dma_lock(dma_flags);
		enable_dma(devpriv->dma);
#ifdef PCL812_EXTDEBUG
		rt_printk
			("pcl812 EDBG:   DMA %d PTR 0x%0x/0x%0x LEN %u/%u EOS %d\n",
			devpriv->dma, devpriv->hwdmaptr[0],
			devpriv->hwdmaptr[1], devpriv->dmabytestomove[0],
			devpriv->dmabytestomove[1], devpriv->ai_eos);
#endif
	}

	switch (cmd->convert_src) {
	case TRIG_TIMER:
		start_pacer(dev, 1, divisor1, divisor2);
		break;
	}

	if (devpriv->ai_dma) {
		outb(devpriv->mode_reg_int | 2, dev->iobase + PCL812_MODE);	/*  let's go! */
	} else {
		outb(devpriv->mode_reg_int | 6, dev->iobase + PCL812_MODE);	/*  let's go! */
	}

#ifdef PCL812_EXTDEBUG
	rt_printk("pcl812 EDBG: END: pcl812_ai_cmd(...)\n");
#endif

	return 0;
}

/*
==============================================================================
*/
static irqreturn_t interrupt_pcl812_ai_int(int irq, void *d)
{
	char err = 1;
	unsigned int mask, timeout;
	struct comedi_device *dev = d;
	struct comedi_subdevice *s = dev->subdevices + 0;

	s->async->events = 0;

	timeout = 50;		/* wait max 50us, it must finish under 33us */
	if (devpriv->ai_is16b) {
		mask = 0xffff;
		while (timeout--) {
			if (!(inb(dev->iobase + ACL8216_STATUS) & ACL8216_DRDY)) {
				err = 0;
				break;
			}
			comedi_udelay(1);
		}
	} else {
		mask = 0x0fff;
		while (timeout--) {
			if (!(inb(dev->iobase + PCL812_AD_HI) & PCL812_DRDY)) {
				err = 0;
				break;
			}
			comedi_udelay(1);
		}
	}

	if (err) {
		rt_printk
			("comedi%d: pcl812: (%s at 0x%lx) A/D cmd IRQ without DRDY!\n",
			dev->minor, dev->board_name, dev->iobase);
		pcl812_ai_cancel(dev, s);
		s->async->events |= COMEDI_CB_EOA | COMEDI_CB_ERROR;
		comedi_event(dev, s);
		return IRQ_HANDLED;
	}

	comedi_buf_put(s->async,
		((inb(dev->iobase + PCL812_AD_HI) << 8) | inb(dev->iobase +
				PCL812_AD_LO)) & mask);

	outb(0, dev->iobase + PCL812_CLRINT);	/* clear INT request */

	if (s->async->cur_chan == 0) {	/* one scan done */
		devpriv->ai_act_scan++;
		if (!(devpriv->ai_neverending))
			if (devpriv->ai_act_scan >= devpriv->ai_scans) {	/* all data sampled */
				pcl812_ai_cancel(dev, s);
				s->async->events |= COMEDI_CB_EOA;
			}
	}

	comedi_event(dev, s);
	return IRQ_HANDLED;
}

/*
==============================================================================
*/
static void transfer_from_dma_buf(struct comedi_device *dev, struct comedi_subdevice *s,
	short *ptr, unsigned int bufptr, unsigned int len)
{
	unsigned int i;

	s->async->events = 0;
	for (i = len; i; i--) {
		comedi_buf_put(s->async, ptr[bufptr++]);	/*  get one sample */

		if (s->async->cur_chan == 0) {
			devpriv->ai_act_scan++;
			if (!devpriv->ai_neverending)
				if (devpriv->ai_act_scan >= devpriv->ai_scans) {	/* all data sampled */
					pcl812_ai_cancel(dev, s);
					s->async->events |= COMEDI_CB_EOA;
					break;
				}
		}
	}

	comedi_event(dev, s);
}

/*
==============================================================================
*/
static irqreturn_t interrupt_pcl812_ai_dma(int irq, void *d)
{
	struct comedi_device *dev = d;
	struct comedi_subdevice *s = dev->subdevices + 0;
	unsigned long dma_flags;
	int len, bufptr;
	short *ptr;

#ifdef PCL812_EXTDEBUG
	rt_printk("pcl812 EDBG: BGN: interrupt_pcl812_ai_dma(...)\n");
#endif
	ptr = (short *) devpriv->dmabuf[devpriv->next_dma_buf];
	len = (devpriv->dmabytestomove[devpriv->next_dma_buf] >> 1) -
		devpriv->ai_poll_ptr;

	devpriv->next_dma_buf = 1 - devpriv->next_dma_buf;
	disable_dma(devpriv->dma);
	set_dma_mode(devpriv->dma, DMA_MODE_READ);
	dma_flags = claim_dma_lock();
	set_dma_addr(devpriv->dma, devpriv->hwdmaptr[devpriv->next_dma_buf]);
	if (devpriv->ai_eos) {
		set_dma_count(devpriv->dma,
			devpriv->dmabytestomove[devpriv->next_dma_buf]);
	} else {
		if (devpriv->dma_runs_to_end) {
			set_dma_count(devpriv->dma,
				devpriv->dmabytestomove[devpriv->next_dma_buf]);
		} else {
			set_dma_count(devpriv->dma, devpriv->last_dma_run);
		}
		devpriv->dma_runs_to_end--;
	}
	release_dma_lock(dma_flags);
	enable_dma(devpriv->dma);

	outb(0, dev->iobase + PCL812_CLRINT);	/* clear INT request */

	bufptr = devpriv->ai_poll_ptr;
	devpriv->ai_poll_ptr = 0;

	transfer_from_dma_buf(dev, s, ptr, bufptr, len);

#ifdef PCL812_EXTDEBUG
	rt_printk("pcl812 EDBG: END: interrupt_pcl812_ai_dma(...)\n");
#endif
	return IRQ_HANDLED;
}

/*
==============================================================================
*/
static irqreturn_t interrupt_pcl812(int irq, void *d)
{
	struct comedi_device *dev = d;

	if (!dev->attached) {
		comedi_error(dev, "spurious interrupt");
		return IRQ_HANDLED;
	}
	if (devpriv->ai_dma) {
		return interrupt_pcl812_ai_dma(irq, d);
	} else {
		return interrupt_pcl812_ai_int(irq, d);
	};
}

/*
==============================================================================
*/
static int pcl812_ai_poll(struct comedi_device *dev, struct comedi_subdevice *s)
{
	unsigned long flags;
	unsigned int top1, top2, i;

	if (!devpriv->ai_dma)
		return 0;	/*  poll is valid only for DMA transfer */

	comedi_spin_lock_irqsave(&dev->spinlock, flags);

	for (i = 0; i < 10; i++) {
		top1 = get_dma_residue(devpriv->ai_dma);	/*  where is now DMA */
		top2 = get_dma_residue(devpriv->ai_dma);
		if (top1 == top2)
			break;
	}

	if (top1 != top2) {
		comedi_spin_unlock_irqrestore(&dev->spinlock, flags);
		return 0;
	}

	top1 = devpriv->dmabytestomove[1 - devpriv->next_dma_buf] - top1;	/*  where is now DMA in buffer */
	top1 >>= 1;		/*  sample position */
	top2 = top1 - devpriv->ai_poll_ptr;
	if (top2 < 1) {		/*  no new samples */
		comedi_spin_unlock_irqrestore(&dev->spinlock, flags);
		return 0;
	}

	transfer_from_dma_buf(dev, s,
		(void *)devpriv->dmabuf[1 - devpriv->next_dma_buf],
		devpriv->ai_poll_ptr, top2);

	devpriv->ai_poll_ptr = top1;	/*  new buffer position */

	comedi_spin_unlock_irqrestore(&dev->spinlock, flags);

	return s->async->buf_write_count - s->async->buf_read_count;
}

/*
==============================================================================
*/
static void setup_range_channel(struct comedi_device *dev, struct comedi_subdevice *s,
	unsigned int rangechan, char wait)
{
	unsigned char chan_reg = CR_CHAN(rangechan);	/*  normal board */
	unsigned char gain_reg = CR_RANGE(rangechan) + devpriv->range_correction;	/*  gain index */

	if ((chan_reg == devpriv->old_chan_reg)
		&& (gain_reg == devpriv->old_gain_reg))
		return;		/*  we can return, no change */

	devpriv->old_chan_reg = chan_reg;
	devpriv->old_gain_reg = gain_reg;

	if (devpriv->use_MPC) {
		if (devpriv->use_diff) {
			chan_reg = chan_reg | 0x30;	/*  DIFF inputs */
		} else {
			if (chan_reg & 0x80) {
				chan_reg = chan_reg | 0x20;	/*  SE inputs 8-15 */
			} else {
				chan_reg = chan_reg | 0x10;	/*  SE inputs 0-7 */
			}
		}
	}

	outb(chan_reg, dev->iobase + PCL812_MUX);	/* select channel */
	outb(gain_reg, dev->iobase + PCL812_GAIN);	/* select gain */

	if (wait) {
		comedi_udelay(devpriv->max_812_ai_mode0_rangewait);	/*  XXX this depends on selected range and can be very long for some high gain ranges! */
	}
}

/*
==============================================================================
*/
static void start_pacer(struct comedi_device *dev, int mode, unsigned int divisor1,
	unsigned int divisor2)
{
#ifdef PCL812_EXTDEBUG
	rt_printk("pcl812 EDBG: BGN: start_pacer(%d,%u,%u)\n", mode, divisor1,
		divisor2);
#endif
	outb(0xb4, dev->iobase + PCL812_CTRCTL);
	outb(0x74, dev->iobase + PCL812_CTRCTL);
	comedi_udelay(1);

	if (mode == 1) {
		outb(divisor2 & 0xff, dev->iobase + PCL812_CTR2);
		outb((divisor2 >> 8) & 0xff, dev->iobase + PCL812_CTR2);
		outb(divisor1 & 0xff, dev->iobase + PCL812_CTR1);
		outb((divisor1 >> 8) & 0xff, dev->iobase + PCL812_CTR1);
	}
#ifdef PCL812_EXTDEBUG
	rt_printk("pcl812 EDBG: END: start_pacer(...)\n");
#endif
}

/*
==============================================================================
*/
static void free_resources(struct comedi_device *dev)
{

	if (dev->private) {
		if (devpriv->dmabuf[0])
			free_pages(devpriv->dmabuf[0], devpriv->dmapages[0]);
		if (devpriv->dmabuf[1])
			free_pages(devpriv->dmabuf[1], devpriv->dmapages[1]);
		if (devpriv->dma)
			free_dma(devpriv->dma);
	}
	if (dev->irq)
		comedi_free_irq(dev->irq, dev);
	if (dev->iobase)
		release_region(dev->iobase, this_board->io_range);
}

/*
==============================================================================
*/
static int pcl812_ai_cancel(struct comedi_device *dev, struct comedi_subdevice *s)
{
#ifdef PCL812_EXTDEBUG
	rt_printk("pcl812 EDBG: BGN: pcl812_ai_cancel(...)\n");
#endif
	if (devpriv->ai_dma)
		disable_dma(devpriv->dma);
	outb(0, dev->iobase + PCL812_CLRINT);	/* clear INT request */
	outb(devpriv->mode_reg_int | 0, dev->iobase + PCL812_MODE);	/* Stop A/D */
	start_pacer(dev, -1, 0, 0);	/*  stop 8254 */
	outb(0, dev->iobase + PCL812_CLRINT);	/* clear INT request */
#ifdef PCL812_EXTDEBUG
	rt_printk("pcl812 EDBG: END: pcl812_ai_cancel(...)\n");
#endif
	return 0;
}

/*
==============================================================================
*/
static void pcl812_reset(struct comedi_device *dev)
{
#ifdef PCL812_EXTDEBUG
	rt_printk("pcl812 EDBG: BGN: pcl812_reset(...)\n");
#endif
	outb(0, dev->iobase + PCL812_MUX);
	outb(0 + devpriv->range_correction, dev->iobase + PCL812_GAIN);
	devpriv->old_chan_reg = -1;	/*  invalidate chain/gain memory */
	devpriv->old_gain_reg = -1;

	switch (this_board->board_type) {
	case boardPCL812PG:
	case boardPCL812:
	case boardACL8112:
	case boardACL8216:
		outb(0, dev->iobase + PCL812_DA2_LO);
		outb(0, dev->iobase + PCL812_DA2_HI);
	case boardA821:
		outb(0, dev->iobase + PCL812_DA1_LO);
		outb(0, dev->iobase + PCL812_DA1_HI);
		start_pacer(dev, -1, 0, 0);	/*  stop 8254 */
		outb(0, dev->iobase + PCL812_DO_HI);
		outb(0, dev->iobase + PCL812_DO_LO);
		outb(devpriv->mode_reg_int | 0, dev->iobase + PCL812_MODE);
		outb(0, dev->iobase + PCL812_CLRINT);
		break;
	case boardPCL813B:
	case boardPCL813:
	case boardISO813:
	case boardACL8113:
		comedi_udelay(5);
		break;
	}
	comedi_udelay(5);
#ifdef PCL812_EXTDEBUG
	rt_printk("pcl812 EDBG: END: pcl812_reset(...)\n");
#endif
}

/*
==============================================================================
*/
static int pcl812_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	int ret, subdev;
	unsigned long iobase;
	unsigned int irq;
	unsigned int dma;
	unsigned long pages;
	struct comedi_subdevice *s;
	int n_subdevices;

	iobase = it->options[0];
	printk("comedi%d: pcl812:  board=%s, ioport=0x%03lx", dev->minor,
		this_board->name, iobase);

	if (!request_region(iobase, this_board->io_range, "pcl812")) {
		printk("I/O port conflict\n");
		return -EIO;
	}
	dev->iobase = iobase;

	ret = alloc_private(dev, sizeof(struct pcl812_private));
	if (ret < 0) {
		free_resources(dev);
		return ret;	/* Can't alloc mem */
	}

	dev->board_name = this_board->name;

	irq = 0;
	if (this_board->IRQbits != 0) {	/* board support IRQ */
		irq = it->options[1];
		if (irq) {	/* we want to use IRQ */
			if (((1 << irq) & this_board->IRQbits) == 0) {
				printk(", IRQ %u is out of allowed range, DISABLING IT", irq);
				irq = 0;	/* Bad IRQ */
			} else {
				if (comedi_request_irq(irq, interrupt_pcl812, 0,
						"pcl812", dev)) {
					printk(", unable to allocate IRQ %u, DISABLING IT", irq);
					irq = 0;	/* Can't use IRQ */
				} else {
					printk(", irq=%u", irq);
				}
			}
		}
	}

	dev->irq = irq;

	dma = 0;
	devpriv->dma = dma;
	if (!dev->irq)
		goto no_dma;	/* if we haven't IRQ, we can't use DMA */
	if (this_board->DMAbits != 0) {	/* board support DMA */
		dma = it->options[2];
		if (((1 << dma) & this_board->DMAbits) == 0) {
			printk(", DMA is out of allowed range, FAIL!\n");
			return -EINVAL;	/* Bad DMA */
		}
		ret = request_dma(dma, "pcl812");
		if (ret) {
			printk(", unable to allocate DMA %u, FAIL!\n", dma);
			return -EBUSY;	/* DMA isn't free */
		}
		devpriv->dma = dma;
		printk(", dma=%u", dma);
		pages = 1;	/* we want 8KB */
		devpriv->dmabuf[0] = __get_dma_pages(GFP_KERNEL, pages);
		if (!devpriv->dmabuf[0]) {
			printk(", unable to allocate DMA buffer, FAIL!\n");
			/* maybe experiment with try_to_free_pages() will help .... */
			free_resources(dev);
			return -EBUSY;	/* no buffer :-( */
		}
		devpriv->dmapages[0] = pages;
		devpriv->hwdmaptr[0] = virt_to_bus((void *)devpriv->dmabuf[0]);
		devpriv->hwdmasize[0] = PAGE_SIZE * (1 << pages);
		devpriv->dmabuf[1] = __get_dma_pages(GFP_KERNEL, pages);
		if (!devpriv->dmabuf[1]) {
			printk(", unable to allocate DMA buffer, FAIL!\n");
			free_resources(dev);
			return -EBUSY;
		}
		devpriv->dmapages[1] = pages;
		devpriv->hwdmaptr[1] = virt_to_bus((void *)devpriv->dmabuf[1]);
		devpriv->hwdmasize[1] = PAGE_SIZE * (1 << pages);
	}
      no_dma:

	n_subdevices = 0;
	if (this_board->n_aichan > 0)
		n_subdevices++;
	if (this_board->n_aochan > 0)
		n_subdevices++;
	if (this_board->n_dichan > 0)
		n_subdevices++;
	if (this_board->n_dochan > 0)
		n_subdevices++;

	ret = alloc_subdevices(dev, n_subdevices);
	if (ret < 0) {
		free_resources(dev);
		return ret;
	}

	subdev = 0;

	/* analog input */
	if (this_board->n_aichan > 0) {
		s = dev->subdevices + subdev;
		s->type = COMEDI_SUBD_AI;
		s->subdev_flags = SDF_READABLE;
		switch (this_board->board_type) {
		case boardA821:
			if (it->options[2] == 1) {
				s->n_chan = this_board->n_aichan_diff;
				s->subdev_flags |= SDF_DIFF;
				devpriv->use_diff = 1;
			} else {
				s->n_chan = this_board->n_aichan;
				s->subdev_flags |= SDF_GROUND;
			}
			break;
		case boardACL8112:
		case boardACL8216:
			if (it->options[4] == 1) {
				s->n_chan = this_board->n_aichan_diff;
				s->subdev_flags |= SDF_DIFF;
				devpriv->use_diff = 1;
			} else {
				s->n_chan = this_board->n_aichan;
				s->subdev_flags |= SDF_GROUND;
			}
			break;
		default:
			s->n_chan = this_board->n_aichan;
			s->subdev_flags |= SDF_GROUND;
			break;
		}
		s->maxdata = this_board->ai_maxdata;
		s->len_chanlist = MAX_CHANLIST_LEN;
		s->range_table = this_board->rangelist_ai;
		if (this_board->board_type == boardACL8216) {
			s->insn_read = acl8216_ai_insn_read;
		} else {
			s->insn_read = pcl812_ai_insn_read;
		}
		devpriv->use_MPC = this_board->haveMPC508;
		s->cancel = pcl812_ai_cancel;
		if (dev->irq) {
			dev->read_subdev = s;
			s->subdev_flags |= SDF_CMD_READ;
			s->do_cmdtest = pcl812_ai_cmdtest;
			s->do_cmd = pcl812_ai_cmd;
			s->poll = pcl812_ai_poll;
		}
		switch (this_board->board_type) {
		case boardPCL812PG:
			if (it->options[4] == 1)
				s->range_table = &range_pcl812pg2_ai;
			break;
		case boardPCL812:
			switch (it->options[4]) {
			case 0:
				s->range_table = &range_bipolar10;
				break;
			case 1:
				s->range_table = &range_bipolar5;
				break;
			case 2:
				s->range_table = &range_bipolar2_5;
				break;
			case 3:
				s->range_table = &range812_bipolar1_25;
				break;
			case 4:
				s->range_table = &range812_bipolar0_625;
				break;
			case 5:
				s->range_table = &range812_bipolar0_3125;
				break;
			default:
				s->range_table = &range_bipolar10;
				break;
				printk(", incorrect range number %d, changing to 0 (+/-10V)", it->options[4]);
				break;
			}
			break;
			break;
		case boardPCL813B:
			if (it->options[1] == 1)
				s->range_table = &range_pcl813b2_ai;
			break;
		case boardISO813:
			switch (it->options[1]) {
			case 0:
				s->range_table = &range_iso813_1_ai;
				break;
			case 1:
				s->range_table = &range_iso813_1_2_ai;
				break;
			case 2:
				s->range_table = &range_iso813_2_ai;
				devpriv->range_correction = 1;
				break;
			case 3:
				s->range_table = &range_iso813_2_2_ai;
				devpriv->range_correction = 1;
				break;
			default:
				s->range_table = &range_iso813_1_ai;
				break;
				printk(", incorrect range number %d, changing to 0 ", it->options[1]);
				break;
			}
			break;
		case boardACL8113:
			switch (it->options[1]) {
			case 0:
				s->range_table = &range_acl8113_1_ai;
				break;
			case 1:
				s->range_table = &range_acl8113_1_2_ai;
				break;
			case 2:
				s->range_table = &range_acl8113_2_ai;
				devpriv->range_correction = 1;
				break;
			case 3:
				s->range_table = &range_acl8113_2_2_ai;
				devpriv->range_correction = 1;
				break;
			default:
				s->range_table = &range_acl8113_1_ai;
				break;
				printk(", incorrect range number %d, changing to 0 ", it->options[1]);
				break;
			}
			break;
		}
		subdev++;
	}

	/* analog output */
	if (this_board->n_aochan > 0) {
		s = dev->subdevices + subdev;
		s->type = COMEDI_SUBD_AO;
		s->subdev_flags = SDF_WRITABLE | SDF_GROUND;
		s->n_chan = this_board->n_aochan;
		s->maxdata = 0xfff;
		s->len_chanlist = 1;
		s->range_table = this_board->rangelist_ao;
		s->insn_read = pcl812_ao_insn_read;
		s->insn_write = pcl812_ao_insn_write;
		switch (this_board->board_type) {
		case boardA821:
			if (it->options[3] == 1)
				s->range_table = &range_unipolar10;
			break;
		case boardPCL812:
		case boardACL8112:
		case boardPCL812PG:
		case boardACL8216:
			if (it->options[5] == 1)
				s->range_table = &range_unipolar10;
			if (it->options[5] == 2)
				s->range_table = &range_unknown;
			break;
		}
		subdev++;
	}

	/* digital input */
	if (this_board->n_dichan > 0) {
		s = dev->subdevices + subdev;
		s->type = COMEDI_SUBD_DI;
		s->subdev_flags = SDF_READABLE;
		s->n_chan = this_board->n_dichan;
		s->maxdata = 1;
		s->len_chanlist = this_board->n_dichan;
		s->range_table = &range_digital;
		s->insn_bits = pcl812_di_insn_bits;
		subdev++;
	}

	/* digital output */
	if (this_board->n_dochan > 0) {
		s = dev->subdevices + subdev;
		s->type = COMEDI_SUBD_DO;
		s->subdev_flags = SDF_WRITABLE;
		s->n_chan = this_board->n_dochan;
		s->maxdata = 1;
		s->len_chanlist = this_board->n_dochan;
		s->range_table = &range_digital;
		s->insn_bits = pcl812_do_insn_bits;
		subdev++;
	}

	switch (this_board->board_type) {
	case boardACL8216:
		devpriv->ai_is16b = 1;
	case boardPCL812PG:
	case boardPCL812:
	case boardACL8112:
		devpriv->max_812_ai_mode0_rangewait = 1;
		if (it->options[3] > 0)
			devpriv->use_ext_trg = 1;	/*  we use external trigger */
	case boardA821:
		devpriv->max_812_ai_mode0_rangewait = 1;
		devpriv->mode_reg_int = (irq << 4) & 0xf0;
		break;
	case boardPCL813B:
	case boardPCL813:
	case boardISO813:
	case boardACL8113:
		devpriv->max_812_ai_mode0_rangewait = 5;	/* maybe there must by greatest timeout */
		break;
	}

	printk("\n");
	devpriv->valid = 1;

	pcl812_reset(dev);

	return 0;
}

/*
==============================================================================
 */
static int pcl812_detach(struct comedi_device *dev)
{

#ifdef PCL812_EXTDEBUG
	rt_printk("comedi%d: pcl812: remove\n", dev->minor);
#endif
	free_resources(dev);
	return 0;
}
