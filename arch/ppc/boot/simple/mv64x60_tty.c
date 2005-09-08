/*
 * arch/ppc/boot/simple/mv64x60_tty.c
 *
 * Bootloader version of the embedded MPSC/UART driver for the Marvell 64x60.
 * Note: Due to a GT64260A erratum, DMA will be used for UART input (via SDMA).
 *
 * Author: Mark A. Greer <mgreer@mvista.com>
 *
 * 2001 (c) MontaVista Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

/* This code assumes that the data cache has been disabled (L1, L2, L3). */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/serial_reg.h>
#include <asm/serial.h>
#include <asm/io.h>
#include <asm/mv64x60_defs.h>
#include <mpsc_defs.h>

#ifdef CONFIG_EV64360
#include <platforms/ev64360.h>
u32	mv64x60_console_baud = EV64360_DEFAULT_BAUD;
u32	mv64x60_mpsc_clk_src = EV64360_MPSC_CLK_SRC; /* TCLK */
u32	mv64x60_mpsc_clk_freq = EV64360_MPSC_CLK_FREQ;
#else
u32	mv64x60_console_baud = 9600;
u32	mv64x60_mpsc_clk_src = 8; /* TCLK */
u32	mv64x60_mpsc_clk_freq = 100000000;
#endif

extern void udelay(long);
static void stop_dma(int chan);

static void __iomem *mv64x60_base = (void __iomem *)CONFIG_MV64X60_NEW_BASE;

struct sdma_regs {
	u32	sdc;
	u32	sdcm;
	u32	rx_desc;
	u32	rx_buf_ptr;
	u32	scrdp;
	u32	tx_desc;
	u32	sctdp;
	u32	sftdp;
};

static struct sdma_regs	sdma_regs[2];

#define	SDMA_REGS_INIT(s, reg_base) {			\
	(s)->sdc	= (reg_base) + SDMA_SDC;	\
	(s)->sdcm	= (reg_base) + SDMA_SDCM;	\
	(s)->rx_desc	= (reg_base) + SDMA_RX_DESC;	\
	(s)->rx_buf_ptr = (reg_base) + SDMA_RX_BUF_PTR;	\
	(s)->scrdp	= (reg_base) + SDMA_SCRDP;	\
	(s)->tx_desc	= (reg_base) + SDMA_TX_DESC;	\
	(s)->sctdp	= (reg_base) + SDMA_SCTDP;	\
	(s)->sftdp	= (reg_base) + SDMA_SFTDP;	\
}

static u32	mpsc_base[2] = { MV64x60_MPSC_0_OFFSET, MV64x60_MPSC_1_OFFSET };

struct mv64x60_rx_desc {
	u16	bufsize;
	u16	bytecnt;
	u32	cmd_stat;
	u32	next_desc_ptr;
	u32	buffer;
};

struct mv64x60_tx_desc {
	u16	bytecnt;
	u16	shadow;
	u32	cmd_stat;
	u32	next_desc_ptr;
	u32	buffer;
};

#define	MAX_RESET_WAIT	10000
#define	MAX_TX_WAIT	10000

#define	RX_NUM_DESC	2
#define	TX_NUM_DESC	2

#define	RX_BUF_SIZE	32
#define	TX_BUF_SIZE	32

static struct mv64x60_rx_desc rd[2][RX_NUM_DESC] __attribute__ ((aligned(32)));
static struct mv64x60_tx_desc td[2][TX_NUM_DESC] __attribute__ ((aligned(32)));

static char rx_buf[2][RX_NUM_DESC * RX_BUF_SIZE] __attribute__ ((aligned(32)));
static char tx_buf[2][TX_NUM_DESC * TX_BUF_SIZE] __attribute__ ((aligned(32)));

static int cur_rd[2] = { 0, 0 };
static int cur_td[2] = { 0, 0 };

static char chan_initialized[2] = { 0, 0 };


#define	RX_INIT_RDP(rdp) {			\
	(rdp)->bufsize = 2;			\
	(rdp)->bytecnt = 0;			\
	(rdp)->cmd_stat = SDMA_DESC_CMDSTAT_L | SDMA_DESC_CMDSTAT_F |	\
		SDMA_DESC_CMDSTAT_O;	\
}

#ifdef CONFIG_MV64360
static u32 cpu2mem_tab[MV64x60_CPU2MEM_WINDOWS][2] = {
		{ MV64x60_CPU2MEM_0_BASE, MV64x60_CPU2MEM_0_SIZE },
		{ MV64x60_CPU2MEM_1_BASE, MV64x60_CPU2MEM_1_SIZE },
		{ MV64x60_CPU2MEM_2_BASE, MV64x60_CPU2MEM_2_SIZE },
		{ MV64x60_CPU2MEM_3_BASE, MV64x60_CPU2MEM_3_SIZE }
};

static u32 com2mem_tab[MV64x60_CPU2MEM_WINDOWS][2] = {
		{ MV64360_MPSC2MEM_0_BASE, MV64360_MPSC2MEM_0_SIZE },
		{ MV64360_MPSC2MEM_1_BASE, MV64360_MPSC2MEM_1_SIZE },
		{ MV64360_MPSC2MEM_2_BASE, MV64360_MPSC2MEM_2_SIZE },
		{ MV64360_MPSC2MEM_3_BASE, MV64360_MPSC2MEM_3_SIZE }
};

static u32 dram_selects[MV64x60_CPU2MEM_WINDOWS] = { 0xe, 0xd, 0xb, 0x7 };
#endif

unsigned long
serial_init(int chan, void *ignored)
{
	u32		mpsc_routing_base, sdma_base, brg_bcr, cdv;
	int		i;

	chan = (chan == 1); /* default to chan 0 if anything but 1 */

	if (chan_initialized[chan])
		return chan;

	chan_initialized[chan] = 1;

	if (chan == 0) {
		sdma_base = MV64x60_SDMA_0_OFFSET;
		brg_bcr = MV64x60_BRG_0_OFFSET + BRG_BCR;
		SDMA_REGS_INIT(&sdma_regs[0], MV64x60_SDMA_0_OFFSET);
	} else {
		sdma_base = MV64x60_SDMA_1_OFFSET;
		brg_bcr = MV64x60_BRG_1_OFFSET + BRG_BCR;
		SDMA_REGS_INIT(&sdma_regs[0], MV64x60_SDMA_1_OFFSET);
	}

	mpsc_routing_base = MV64x60_MPSC_ROUTING_OFFSET;

	stop_dma(chan);

	/* Set up ring buffers */
	for (i=0; i<RX_NUM_DESC; i++) {
		RX_INIT_RDP(&rd[chan][i]);
		rd[chan][i].buffer = (u32)&rx_buf[chan][i * RX_BUF_SIZE];
		rd[chan][i].next_desc_ptr = (u32)&rd[chan][i+1];
	}
	rd[chan][RX_NUM_DESC - 1].next_desc_ptr = (u32)&rd[chan][0];

	for (i=0; i<TX_NUM_DESC; i++) {
		td[chan][i].bytecnt = 0;
		td[chan][i].shadow = 0;
		td[chan][i].buffer = (u32)&tx_buf[chan][i * TX_BUF_SIZE];
		td[chan][i].cmd_stat = SDMA_DESC_CMDSTAT_F|SDMA_DESC_CMDSTAT_L;
		td[chan][i].next_desc_ptr = (u32)&td[chan][i+1];
	}
	td[chan][TX_NUM_DESC - 1].next_desc_ptr = (u32)&td[chan][0];

	/* Set MPSC Routing */
	out_le32(mv64x60_base + mpsc_routing_base + MPSC_MRR, 0x3ffffe38);

#ifdef CONFIG_GT64260
	out_le32(mv64x60_base + GT64260_MPP_SERIAL_PORTS_MULTIPLEX, 0x00001102);
#else /* Must be MV64360 or MV64460 */
	{
	u32	enables, prot_bits, v;

	/* Set up comm unit to memory mapping windows */
	/* Note: Assumes MV64x60_CPU2MEM_WINDOWS == 4 */

	enables = in_le32(mv64x60_base + MV64360_CPU_BAR_ENABLE) & 0xf;
	prot_bits = 0;

	for (i=0; i<MV64x60_CPU2MEM_WINDOWS; i++) {
		if (!(enables & (1 << i))) {
			v = in_le32(mv64x60_base + cpu2mem_tab[i][0]);
			v = ((v & 0xffff) << 16) | (dram_selects[i] << 8);
			out_le32(mv64x60_base + com2mem_tab[i][0], v);

			v = in_le32(mv64x60_base + cpu2mem_tab[i][1]);
			v = (v & 0xffff) << 16;
			out_le32(mv64x60_base + com2mem_tab[i][1], v);

			prot_bits |= (0x3 << (i << 1)); /* r/w access */
		}
	}

	out_le32(mv64x60_base + MV64360_MPSC_0_REMAP, 0);
	out_le32(mv64x60_base + MV64360_MPSC_1_REMAP, 0);
	out_le32(mv64x60_base + MV64360_MPSC2MEM_ACC_PROT_0, prot_bits);
	out_le32(mv64x60_base + MV64360_MPSC2MEM_ACC_PROT_1, prot_bits);
	out_le32(mv64x60_base + MV64360_MPSC2MEM_BAR_ENABLE, enables);
	}
#endif

	/* MPSC 0/1 Rx & Tx get clocks BRG0/1 */
	out_le32(mv64x60_base + mpsc_routing_base + MPSC_RCRR, 0x00000100);
	out_le32(mv64x60_base + mpsc_routing_base + MPSC_TCRR, 0x00000100);

	/* clear pending interrupts */
	out_le32(mv64x60_base + MV64x60_SDMA_INTR_OFFSET + SDMA_INTR_MASK, 0);

	out_le32(mv64x60_base + SDMA_SCRDP + sdma_base, (int)&rd[chan][0]);
	out_le32(mv64x60_base + SDMA_SCTDP + sdma_base,
		(int)&td[chan][TX_NUM_DESC - 1]);
	out_le32(mv64x60_base + SDMA_SFTDP + sdma_base,
		(int)&td[chan][TX_NUM_DESC - 1]);

	out_le32(mv64x60_base + SDMA_SDC + sdma_base,
		SDMA_SDC_RFT | SDMA_SDC_SFM | SDMA_SDC_BLMR | SDMA_SDC_BLMT |
		(3 << 12));

	cdv = ((mv64x60_mpsc_clk_freq/(32*mv64x60_console_baud))-1);
	out_le32(mv64x60_base + brg_bcr,
		((mv64x60_mpsc_clk_src << 18) | (1 << 16) | cdv));

	/* Put MPSC into UART mode, no null modem, 16x clock mode */
	out_le32(mv64x60_base + MPSC_MMCRL + mpsc_base[chan], 0x000004c4);
	out_le32(mv64x60_base + MPSC_MMCRH + mpsc_base[chan], 0x04400400);

	out_le32(mv64x60_base + MPSC_CHR_1 + mpsc_base[chan], 0);
	out_le32(mv64x60_base + MPSC_CHR_9 + mpsc_base[chan], 0);
	out_le32(mv64x60_base + MPSC_CHR_10 + mpsc_base[chan], 0);
	out_le32(mv64x60_base + MPSC_CHR_3 + mpsc_base[chan], 4);
	out_le32(mv64x60_base + MPSC_CHR_4 + mpsc_base[chan], 0);
	out_le32(mv64x60_base + MPSC_CHR_5 + mpsc_base[chan], 0);
	out_le32(mv64x60_base + MPSC_CHR_6 + mpsc_base[chan], 0);
	out_le32(mv64x60_base + MPSC_CHR_7 + mpsc_base[chan], 0);
	out_le32(mv64x60_base + MPSC_CHR_8 + mpsc_base[chan], 0);

	/* 8 data bits, 1 stop bit */
	out_le32(mv64x60_base + MPSC_MPCR + mpsc_base[chan], (3 << 12));
	out_le32(mv64x60_base + SDMA_SDCM + sdma_base, SDMA_SDCM_ERD);
	out_le32(mv64x60_base + MPSC_CHR_2 + mpsc_base[chan], MPSC_CHR_2_EH);

	udelay(100);

	return chan;
}

static void
stop_dma(int chan)
{
	int	i;

	/* Abort MPSC Rx (aborting Tx messes things up) */
	out_le32(mv64x60_base + MPSC_CHR_2 + mpsc_base[chan], MPSC_CHR_2_RA);

	/* Abort SDMA Rx, Tx */
	out_le32(mv64x60_base + sdma_regs[chan].sdcm,
		SDMA_SDCM_AR | SDMA_SDCM_STD);

	for (i=0; i<MAX_RESET_WAIT; i++) {
		if ((in_le32(mv64x60_base + sdma_regs[chan].sdcm) &
				(SDMA_SDCM_AR | SDMA_SDCM_AT)) == 0)
			break;

		udelay(100);
	}
}

static int
wait_for_ownership(int chan)
{
	int	i;

	for (i=0; i<MAX_TX_WAIT; i++) {
		if ((in_le32(mv64x60_base + sdma_regs[chan].sdcm) &
				SDMA_SDCM_TXD) == 0)
			break;

		udelay(1000);
	}

	return (i < MAX_TX_WAIT);
}

void
serial_putc(unsigned long com_port, unsigned char c)
{
	struct mv64x60_tx_desc	*tdp;

	if (wait_for_ownership(com_port) == 0)
		return;

	tdp = &td[com_port][cur_td[com_port]];
	if (++cur_td[com_port] >= TX_NUM_DESC)
		cur_td[com_port] = 0;

	*(unchar *)(tdp->buffer ^ 7) = c;
	tdp->bytecnt = 1;
	tdp->shadow = 1;
	tdp->cmd_stat = SDMA_DESC_CMDSTAT_L | SDMA_DESC_CMDSTAT_F |
		SDMA_DESC_CMDSTAT_O;

	out_le32(mv64x60_base + sdma_regs[com_port].sctdp, (int)tdp);
	out_le32(mv64x60_base + sdma_regs[com_port].sftdp, (int)tdp);
	out_le32(mv64x60_base + sdma_regs[com_port].sdcm,
		in_le32(mv64x60_base + sdma_regs[com_port].sdcm) |
			SDMA_SDCM_TXD);
}

unsigned char
serial_getc(unsigned long com_port)
{
	struct mv64x60_rx_desc	*rdp;
	unchar			c = '\0';

	rdp = &rd[com_port][cur_rd[com_port]];

	if ((rdp->cmd_stat & (SDMA_DESC_CMDSTAT_O|SDMA_DESC_CMDSTAT_ES)) == 0) {
		c = *(unchar *)(rdp->buffer ^ 7);
		RX_INIT_RDP(rdp);
		if (++cur_rd[com_port] >= RX_NUM_DESC)
			cur_rd[com_port] = 0;
	}

	return c;
}

int
serial_tstc(unsigned long com_port)
{
	struct mv64x60_rx_desc	*rdp;
	int			loop_count = 0;
	int			rc = 0;

	rdp = &rd[com_port][cur_rd[com_port]];

	/* Go thru rcv desc's until empty looking for one with data (no error)*/
	while (((rdp->cmd_stat & SDMA_DESC_CMDSTAT_O) == 0) &&
		(loop_count++ < RX_NUM_DESC)) {

		/* If there was an error, reinit the desc & continue */
		if ((rdp->cmd_stat & SDMA_DESC_CMDSTAT_ES) != 0) {
			RX_INIT_RDP(rdp);
			if (++cur_rd[com_port] >= RX_NUM_DESC)
				cur_rd[com_port] = 0;
			rdp = (struct mv64x60_rx_desc *)rdp->next_desc_ptr;
		} else {
			rc = 1;
			break;
		}
	}

	return rc;
}

void
serial_close(unsigned long com_port)
{
	stop_dma(com_port);
}
