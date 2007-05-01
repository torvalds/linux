/* $Id: cris-ide-driver.patch,v 1.1 2005/06/29 21:39:07 akpm Exp $
 *
 * Etrax specific IDE functions, like init and PIO-mode setting etc.
 * Almost the entire ide.c is used for the rest of the Etrax ATA driver.
 * Copyright (c) 2000-2005 Axis Communications AB
 *
 * Authors:    Bjorn Wesen        (initial version)
 *             Mikael Starvik     (crisv32 port)
 */

/* Regarding DMA:
 *
 * There are two forms of DMA - "DMA handshaking" between the interface and the drive,
 * and DMA between the memory and the interface. We can ALWAYS use the latter, since it's
 * something built-in in the Etrax. However only some drives support the DMA-mode handshaking
 * on the ATA-bus. The normal PC driver and Triton interface disables memory-if DMA when the
 * device can't do DMA handshaking for some stupid reason. We don't need to do that.
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>
#include <linux/ide.h>
#include <linux/init.h>

#include <asm/io.h>
#include <asm/dma.h>

/* number of DMA descriptors */
#define MAX_DMA_DESCRS 64

/* number of times to retry busy-flags when reading/writing IDE-registers
 * this can't be too high because a hung harddisk might cause the watchdog
 * to trigger (sometimes INB and OUTB are called with irq's disabled)
 */

#define IDE_REGISTER_TIMEOUT 300

#define LOWDB(x)
#define D(x)

enum /* Transfer types */
{
	TYPE_PIO,
	TYPE_DMA,
	TYPE_UDMA
};

/* CRISv32 specifics */
#ifdef CONFIG_ETRAX_ARCH_V32
#include <asm/arch/hwregs/ata_defs.h>
#include <asm/arch/hwregs/dma_defs.h>
#include <asm/arch/hwregs/dma.h>
#include <asm/arch/pinmux.h>

#define ATA_UDMA2_CYC    2
#define ATA_UDMA2_DVS    3
#define ATA_UDMA1_CYC    2
#define ATA_UDMA1_DVS    4
#define ATA_UDMA0_CYC    4
#define ATA_UDMA0_DVS    6
#define ATA_DMA2_STROBE  7
#define ATA_DMA2_HOLD    1
#define ATA_DMA1_STROBE  8
#define ATA_DMA1_HOLD    3
#define ATA_DMA0_STROBE 25
#define ATA_DMA0_HOLD   19
#define ATA_PIO4_SETUP   3
#define ATA_PIO4_STROBE  7
#define ATA_PIO4_HOLD    1
#define ATA_PIO3_SETUP   3
#define ATA_PIO3_STROBE  9
#define ATA_PIO3_HOLD    3
#define ATA_PIO2_SETUP   3
#define ATA_PIO2_STROBE 13
#define ATA_PIO2_HOLD    5
#define ATA_PIO1_SETUP   5
#define ATA_PIO1_STROBE 23
#define ATA_PIO1_HOLD    9
#define ATA_PIO0_SETUP   9
#define ATA_PIO0_STROBE 39
#define ATA_PIO0_HOLD    9

int
cris_ide_ack_intr(ide_hwif_t* hwif)
{
	reg_ata_rw_ctrl2 ctrl2 = REG_TYPE_CONV(reg_ata_rw_ctrl2,
	                         int, hwif->io_ports[0]);
	REG_WR_INT(ata, regi_ata, rw_ack_intr, 1 << ctrl2.sel);
	return 1;
}

static inline int
cris_ide_busy(void)
{
	reg_ata_rs_stat_data stat_data;
	stat_data = REG_RD(ata, regi_ata, rs_stat_data);
	return stat_data.busy;
}

static inline int
cris_ide_ready(void)
{
	return !cris_ide_busy();
}

static inline int
cris_ide_data_available(unsigned short* data)
{
	reg_ata_rs_stat_data stat_data;
	stat_data = REG_RD(ata, regi_ata, rs_stat_data);
	*data = stat_data.data;
	return stat_data.dav;
}

static void
cris_ide_write_command(unsigned long command)
{
	REG_WR_INT(ata, regi_ata, rw_ctrl2, command); /* write data to the drive's register */
}

static void
cris_ide_set_speed(int type, int setup, int strobe, int hold)
{
	reg_ata_rw_ctrl0 ctrl0 = REG_RD(ata, regi_ata, rw_ctrl0);
	reg_ata_rw_ctrl1 ctrl1 = REG_RD(ata, regi_ata, rw_ctrl1);

	if (type == TYPE_PIO) {
		ctrl0.pio_setup = setup;
		ctrl0.pio_strb = strobe;
		ctrl0.pio_hold = hold;
	} else if (type == TYPE_DMA) {
		ctrl0.dma_strb = strobe;
		ctrl0.dma_hold = hold;
	} else if (type == TYPE_UDMA) {
		ctrl1.udma_tcyc = setup;
		ctrl1.udma_tdvs = strobe;
	}
	REG_WR(ata, regi_ata, rw_ctrl0, ctrl0);
	REG_WR(ata, regi_ata, rw_ctrl1, ctrl1);
}

static unsigned long
cris_ide_base_address(int bus)
{
	reg_ata_rw_ctrl2 ctrl2 = {0};
	ctrl2.sel = bus;
	return REG_TYPE_CONV(int, reg_ata_rw_ctrl2, ctrl2);
}

static unsigned long
cris_ide_reg_addr(unsigned long addr, int cs0, int cs1)
{
	reg_ata_rw_ctrl2 ctrl2 = {0};
	ctrl2.addr = addr;
	ctrl2.cs1 = cs1;
	ctrl2.cs0 = cs0;
	return REG_TYPE_CONV(int, reg_ata_rw_ctrl2, ctrl2);
}

static __init void
cris_ide_reset(unsigned val)
{
	reg_ata_rw_ctrl0 ctrl0 = {0};
	ctrl0.rst = val ? regk_ata_active : regk_ata_inactive;
	REG_WR(ata, regi_ata, rw_ctrl0, ctrl0);
}

static __init void
cris_ide_init(void)
{
	reg_ata_rw_ctrl0 ctrl0 = {0};
	reg_ata_rw_intr_mask intr_mask = {0};

	ctrl0.en = regk_ata_yes;
	REG_WR(ata, regi_ata, rw_ctrl0, ctrl0);

	intr_mask.bus0 = regk_ata_yes;
	intr_mask.bus1 = regk_ata_yes;
	intr_mask.bus2 = regk_ata_yes;
	intr_mask.bus3 = regk_ata_yes;

	REG_WR(ata, regi_ata, rw_intr_mask, intr_mask);

	crisv32_request_dma(2, "ETRAX FS built-in ATA", DMA_VERBOSE_ON_ERROR, 0, dma_ata);
	crisv32_request_dma(3, "ETRAX FS built-in ATA", DMA_VERBOSE_ON_ERROR, 0, dma_ata);

	crisv32_pinmux_alloc_fixed(pinmux_ata);
	crisv32_pinmux_alloc_fixed(pinmux_ata0);
	crisv32_pinmux_alloc_fixed(pinmux_ata1);
	crisv32_pinmux_alloc_fixed(pinmux_ata2);
	crisv32_pinmux_alloc_fixed(pinmux_ata3);

	DMA_RESET(regi_dma2);
	DMA_ENABLE(regi_dma2);
	DMA_RESET(regi_dma3);
	DMA_ENABLE(regi_dma3);

	DMA_WR_CMD (regi_dma2, regk_dma_set_w_size2);
	DMA_WR_CMD (regi_dma3, regk_dma_set_w_size2);
}

static dma_descr_context mycontext __attribute__ ((__aligned__(32)));

#define cris_dma_descr_type dma_descr_data
#define cris_pio_read regk_ata_rd
#define cris_ultra_mask 0x7
#define MAX_DESCR_SIZE 0xffffffffUL

static unsigned long
cris_ide_get_reg(unsigned long reg)
{
	return (reg & 0x0e000000) >> 25;
}

static void
cris_ide_fill_descriptor(cris_dma_descr_type *d, void* buf, unsigned int len, int last)
{
	d->buf = (char*)virt_to_phys(buf);
	d->after = d->buf + len;
	d->eol = last;
}

static void
cris_ide_start_dma(ide_drive_t *drive, cris_dma_descr_type *d, int dir,int type,int len)
{
	reg_ata_rw_ctrl2 ctrl2 = REG_TYPE_CONV(reg_ata_rw_ctrl2, int, IDE_DATA_REG);
	reg_ata_rw_trf_cnt trf_cnt = {0};

	mycontext.saved_data = (dma_descr_data*)virt_to_phys(d);
	mycontext.saved_data_buf = d->buf;
	/* start the dma channel */
	DMA_START_CONTEXT(dir ? regi_dma3 : regi_dma2, virt_to_phys(&mycontext));

	/* initiate a multi word dma read using PIO handshaking */
	trf_cnt.cnt = len >> 1;
	/* Due to a "feature" the transfer count has to be one extra word for UDMA. */
	if (type == TYPE_UDMA)
		trf_cnt.cnt++;
	REG_WR(ata, regi_ata, rw_trf_cnt, trf_cnt);

	ctrl2.rw = dir ? regk_ata_rd : regk_ata_wr;
	ctrl2.trf_mode = regk_ata_dma;
	ctrl2.hsh = type == TYPE_PIO ? regk_ata_pio :
	            type == TYPE_DMA ? regk_ata_dma : regk_ata_udma;
	ctrl2.multi = regk_ata_yes;
	ctrl2.dma_size = regk_ata_word;
	REG_WR(ata, regi_ata, rw_ctrl2, ctrl2);
}

static void
cris_ide_wait_dma(int dir)
{
	reg_dma_rw_stat status;
	do
	{
		status = REG_RD(dma, dir ? regi_dma3 : regi_dma2, rw_stat);
	} while(status.list_state != regk_dma_data_at_eol);
}

static int cris_dma_test_irq(ide_drive_t *drive)
{
	int intr = REG_RD_INT(ata, regi_ata, r_intr);
	reg_ata_rw_ctrl2 ctrl2 = REG_TYPE_CONV(reg_ata_rw_ctrl2, int, IDE_DATA_REG);
	return intr & (1 << ctrl2.sel) ? 1 : 0;
}

static void cris_ide_initialize_dma(int dir)
{
}

#else
/* CRISv10 specifics */
#include <asm/arch/svinto.h>
#include <asm/arch/io_interface_mux.h>

/* PIO timing (in R_ATA_CONFIG)
 *
 *                        _____________________________
 * ADDRESS :     ________/
 *
 *                            _______________
 * DIOR    :     ____________/               \__________
 *
 *                               _______________
 * DATA    :     XXXXXXXXXXXXXXXX_______________XXXXXXXX
 *
 *
 * DIOR is unbuffered while address and data is buffered.
 * This creates two problems:
 * 1. The DIOR pulse is to early (because it is unbuffered)
 * 2. The rise time of DIOR is long
 *
 * There are at least three different plausible solutions
 * 1. Use a pad capable of larger currents in Etrax
 * 2. Use an external buffer
 * 3. Make the strobe pulse longer
 *
 * Some of the strobe timings below are modified to compensate
 * for this. This implies a slight performance decrease.
 *
 * THIS SHOULD NEVER BE CHANGED!
 *
 * TODO: Is this true for the latest LX boards still ?
 */

#define ATA_UDMA2_CYC    0 /* No UDMA supported, just to make it compile. */
#define ATA_UDMA2_DVS    0
#define ATA_UDMA1_CYC    0
#define ATA_UDMA1_DVS    0
#define ATA_UDMA0_CYC    0
#define ATA_UDMA0_DVS    0
#define ATA_DMA2_STROBE  4
#define ATA_DMA2_HOLD    0
#define ATA_DMA1_STROBE  4
#define ATA_DMA1_HOLD    1
#define ATA_DMA0_STROBE 12
#define ATA_DMA0_HOLD    9
#define ATA_PIO4_SETUP   1
#define ATA_PIO4_STROBE  5
#define ATA_PIO4_HOLD    0
#define ATA_PIO3_SETUP   1
#define ATA_PIO3_STROBE  5
#define ATA_PIO3_HOLD    1
#define ATA_PIO2_SETUP   1
#define ATA_PIO2_STROBE  6
#define ATA_PIO2_HOLD    2
#define ATA_PIO1_SETUP   2
#define ATA_PIO1_STROBE 11
#define ATA_PIO1_HOLD    4
#define ATA_PIO0_SETUP   4
#define ATA_PIO0_STROBE 19
#define ATA_PIO0_HOLD    4

int
cris_ide_ack_intr(ide_hwif_t* hwif)
{
	return 1;
}

static inline int
cris_ide_busy(void)
{
	return *R_ATA_STATUS_DATA & IO_MASK(R_ATA_STATUS_DATA, busy) ;
}

static inline int
cris_ide_ready(void)
{
	return *R_ATA_STATUS_DATA & IO_MASK(R_ATA_STATUS_DATA, tr_rdy) ;
}

static inline int
cris_ide_data_available(unsigned short* data)
{
	unsigned long status = *R_ATA_STATUS_DATA;
	*data = (unsigned short)status;
	return status & IO_MASK(R_ATA_STATUS_DATA, dav);
}

static void
cris_ide_write_command(unsigned long command)
{
	*R_ATA_CTRL_DATA = command;
}

static void
cris_ide_set_speed(int type, int setup, int strobe, int hold)
{
	static int pio_setup = ATA_PIO4_SETUP;
	static int pio_strobe = ATA_PIO4_STROBE;
	static int pio_hold = ATA_PIO4_HOLD;
	static int dma_strobe = ATA_DMA2_STROBE;
	static int dma_hold = ATA_DMA2_HOLD;

	if (type == TYPE_PIO) {
		pio_setup = setup;
		pio_strobe = strobe;
		pio_hold = hold;
	} else if (type == TYPE_DMA) {
		dma_strobe = strobe;
	  dma_hold = hold;
	}
	*R_ATA_CONFIG = ( IO_FIELD( R_ATA_CONFIG, enable, 1 ) |
	  IO_FIELD( R_ATA_CONFIG, dma_strobe, dma_strobe ) |
		IO_FIELD( R_ATA_CONFIG, dma_hold,   dma_hold ) |
		IO_FIELD( R_ATA_CONFIG, pio_setup,  pio_setup ) |
		IO_FIELD( R_ATA_CONFIG, pio_strobe, pio_strobe ) |
		IO_FIELD( R_ATA_CONFIG, pio_hold,   pio_hold ) );
}

static unsigned long
cris_ide_base_address(int bus)
{
	return IO_FIELD(R_ATA_CTRL_DATA, sel, bus);
}

static unsigned long
cris_ide_reg_addr(unsigned long addr, int cs0, int cs1)
{
	return IO_FIELD(R_ATA_CTRL_DATA, addr, addr) |
	       IO_FIELD(R_ATA_CTRL_DATA, cs0, cs0) |
	       IO_FIELD(R_ATA_CTRL_DATA, cs1, cs1);
}

static __init void
cris_ide_reset(unsigned val)
{
#ifdef CONFIG_ETRAX_IDE_G27_RESET
	REG_SHADOW_SET(R_PORT_G_DATA, port_g_data_shadow, 27, val);
#endif
#ifdef CONFIG_ETRAX_IDE_CSE1_16_RESET
	REG_SHADOW_SET(port_cse1_addr, port_cse1_shadow, 16, val);
#endif
#ifdef CONFIG_ETRAX_IDE_CSP0_8_RESET
	REG_SHADOW_SET(port_csp0_addr, port_csp0_shadow, 8, val);
#endif
#ifdef CONFIG_ETRAX_IDE_PB7_RESET
	port_pb_dir_shadow = port_pb_dir_shadow |
		IO_STATE(R_PORT_PB_DIR, dir7, output);
	*R_PORT_PB_DIR = port_pb_dir_shadow;
	REG_SHADOW_SET(R_PORT_PB_DATA, port_pb_data_shadow, 7, val);
#endif
}

static __init void
cris_ide_init(void)
{
	volatile unsigned int dummy;

	*R_ATA_CTRL_DATA = 0;
	*R_ATA_TRANSFER_CNT = 0;
	*R_ATA_CONFIG = 0;

	if (cris_request_io_interface(if_ata, "ETRAX100LX IDE")) {
		printk(KERN_CRIT "ide: Failed to get IO interface\n");
		return;
	} else if (cris_request_dma(ATA_TX_DMA_NBR,
		                          "ETRAX100LX IDE TX",
		                          DMA_VERBOSE_ON_ERROR,
		                          dma_ata)) {
		cris_free_io_interface(if_ata);
		printk(KERN_CRIT "ide: Failed to get Tx DMA channel\n");
		return;
	} else if (cris_request_dma(ATA_RX_DMA_NBR,
		                          "ETRAX100LX IDE RX",
		                          DMA_VERBOSE_ON_ERROR,
		                          dma_ata)) {
		cris_free_dma(ATA_TX_DMA_NBR, "ETRAX100LX IDE Tx");
		cris_free_io_interface(if_ata);
		printk(KERN_CRIT "ide: Failed to get Rx DMA channel\n");
		return;
	}

	/* make a dummy read to set the ata controller in a proper state */
	dummy = *R_ATA_STATUS_DATA;

	*R_ATA_CONFIG = ( IO_FIELD( R_ATA_CONFIG, enable, 1 ));
	*R_ATA_CTRL_DATA = ( IO_STATE( R_ATA_CTRL_DATA, rw,   read) |
	                     IO_FIELD( R_ATA_CTRL_DATA, addr, 1   ) );

	while(*R_ATA_STATUS_DATA & IO_MASK(R_ATA_STATUS_DATA, busy)); /* wait for busy flag*/

	*R_IRQ_MASK0_SET = ( IO_STATE( R_IRQ_MASK0_SET, ata_irq0, set ) |
	                     IO_STATE( R_IRQ_MASK0_SET, ata_irq1, set ) |
	                     IO_STATE( R_IRQ_MASK0_SET, ata_irq2, set ) |
	                     IO_STATE( R_IRQ_MASK0_SET, ata_irq3, set ) );

	/* reset the dma channels we will use */

	RESET_DMA(ATA_TX_DMA_NBR);
	RESET_DMA(ATA_RX_DMA_NBR);
	WAIT_DMA(ATA_TX_DMA_NBR);
	WAIT_DMA(ATA_RX_DMA_NBR);
}

#define cris_dma_descr_type etrax_dma_descr
#define cris_pio_read IO_STATE(R_ATA_CTRL_DATA, rw, read)
#define cris_ultra_mask 0x0
#define MAX_DESCR_SIZE 0x10000UL

static unsigned long
cris_ide_get_reg(unsigned long reg)
{
	return (reg & 0x0e000000) >> 25;
}

static void
cris_ide_fill_descriptor(cris_dma_descr_type *d, void* buf, unsigned int len, int last)
{
	d->buf = virt_to_phys(buf);
	d->sw_len = len == MAX_DESCR_SIZE ? 0 : len;
	if (last)
		d->ctrl |= d_eol;
}

static void cris_ide_start_dma(ide_drive_t *drive, cris_dma_descr_type *d, int dir, int type, int len)
{
	unsigned long cmd;

	if (dir) {
		/* need to do this before RX DMA due to a chip bug
		 * it is enough to just flush the part of the cache that
		 * corresponds to the buffers we start, but since HD transfers
		 * usually are more than 8 kB, it is easier to optimize for the
		 * normal case and just flush the entire cache. its the only
		 * way to be sure! (OB movie quote)
		 */
		flush_etrax_cache();
		*R_DMA_CH3_FIRST = virt_to_phys(d);
		*R_DMA_CH3_CMD   = IO_STATE(R_DMA_CH3_CMD, cmd, start);

	} else {
		*R_DMA_CH2_FIRST = virt_to_phys(d);
		*R_DMA_CH2_CMD   = IO_STATE(R_DMA_CH2_CMD, cmd, start);
	}

	/* initiate a multi word dma read using DMA handshaking */

	*R_ATA_TRANSFER_CNT =
		IO_FIELD(R_ATA_TRANSFER_CNT, count, len >> 1);

	cmd = dir ? IO_STATE(R_ATA_CTRL_DATA, rw, read) : IO_STATE(R_ATA_CTRL_DATA, rw, write);
	cmd |= type == TYPE_PIO ? IO_STATE(R_ATA_CTRL_DATA, handsh, pio) :
	                          IO_STATE(R_ATA_CTRL_DATA, handsh, dma);
	*R_ATA_CTRL_DATA =
		cmd |
		IO_FIELD(R_ATA_CTRL_DATA, data, IDE_DATA_REG) |
		IO_STATE(R_ATA_CTRL_DATA, src_dst,  dma)  |
		IO_STATE(R_ATA_CTRL_DATA, multi,    on)   |
		IO_STATE(R_ATA_CTRL_DATA, dma_size, word);
}

static void
cris_ide_wait_dma(int dir)
{
	if (dir)
		WAIT_DMA(ATA_RX_DMA_NBR);
	else
		WAIT_DMA(ATA_TX_DMA_NBR);
}

static int cris_dma_test_irq(ide_drive_t *drive)
{
	int intr = *R_IRQ_MASK0_RD;
	int bus = IO_EXTRACT(R_ATA_CTRL_DATA, sel, IDE_DATA_REG);
	return intr & (1 << (bus + IO_BITNR(R_IRQ_MASK0_RD, ata_irq0))) ? 1 : 0;
}


static void cris_ide_initialize_dma(int dir)
{
	if (dir)
	{
		RESET_DMA(ATA_RX_DMA_NBR); /* sometimes the DMA channel get stuck so we need to do this */
		WAIT_DMA(ATA_RX_DMA_NBR);
	}
	else
	{
		RESET_DMA(ATA_TX_DMA_NBR); /* sometimes the DMA channel get stuck so we need to do this */
		WAIT_DMA(ATA_TX_DMA_NBR);
	}
}

#endif

void
cris_ide_outw(unsigned short data, unsigned long reg) {
	int timeleft;

	LOWDB(printk("ow: data 0x%x, reg 0x%x\n", data, reg));

	/* note the lack of handling any timeouts. we stop waiting, but we don't
	 * really notify anybody.
	 */

	timeleft = IDE_REGISTER_TIMEOUT;
	/* wait for busy flag */
	do {
		timeleft--;
	} while(timeleft && cris_ide_busy());

	/*
	 * Fall through at a timeout, so the ongoing command will be
	 * aborted by the write below, which is expected to be a dummy
	 * command to the command register.  This happens when a faulty
	 * drive times out on a command.  See comment on timeout in
	 * INB.
	 */
	if(!timeleft)
		printk("ATA timeout reg 0x%lx := 0x%x\n", reg, data);

	cris_ide_write_command(reg|data); /* write data to the drive's register */

	timeleft = IDE_REGISTER_TIMEOUT;
	/* wait for transmitter ready */
	do {
		timeleft--;
	} while(timeleft && !cris_ide_ready());
}

void
cris_ide_outb(unsigned char data, unsigned long reg)
{
	cris_ide_outw(data, reg);
}

void
cris_ide_outbsync(ide_drive_t *drive, u8 addr, unsigned long port)
{
	cris_ide_outw(addr, port);
}

unsigned short
cris_ide_inw(unsigned long reg) {
	int timeleft;
	unsigned short val;

	timeleft = IDE_REGISTER_TIMEOUT;
	/* wait for busy flag */
	do {
		timeleft--;
	} while(timeleft && cris_ide_busy());

	if(!timeleft) {
		/*
		 * If we're asked to read the status register, like for
		 * example when a command does not complete for an
		 * extended time, but the ATA interface is stuck in a
		 * busy state at the *ETRAX* ATA interface level (as has
		 * happened repeatedly with at least one bad disk), then
		 * the best thing to do is to pretend that we read
		 * "busy" in the status register, so the IDE driver will
		 * time-out, abort the ongoing command and perform a
		 * reset sequence.  Note that the subsequent OUT_BYTE
		 * call will also timeout on busy, but as long as the
		 * write is still performed, everything will be fine.
		 */
		if (cris_ide_get_reg(reg) == IDE_STATUS_OFFSET)
			return BUSY_STAT;
		else
			/* For other rare cases we assume 0 is good enough.  */
			return 0;
	}

	cris_ide_write_command(reg | cris_pio_read);

	timeleft = IDE_REGISTER_TIMEOUT;
	/* wait for available */
	do {
		timeleft--;
	} while(timeleft && !cris_ide_data_available(&val));

	if(!timeleft)
		return 0;

	LOWDB(printk("inb: 0x%x from reg 0x%x\n", val & 0xff, reg));

	return val;
}

unsigned char
cris_ide_inb(unsigned long reg)
{
	return (unsigned char)cris_ide_inw(reg);
}

static int cris_dma_check (ide_drive_t *drive);
static int cris_dma_end (ide_drive_t *drive);
static int cris_dma_setup (ide_drive_t *drive);
static void cris_dma_exec_cmd (ide_drive_t *drive, u8 command);
static int cris_dma_test_irq(ide_drive_t *drive);
static void cris_dma_start(ide_drive_t *drive);
static void cris_ide_input_data (ide_drive_t *drive, void *, unsigned int);
static void cris_ide_output_data (ide_drive_t *drive, void *, unsigned int);
static void cris_atapi_input_bytes(ide_drive_t *drive, void *, unsigned int);
static void cris_atapi_output_bytes(ide_drive_t *drive, void *, unsigned int);
static int cris_dma_on (ide_drive_t *drive);

static void cris_dma_off(ide_drive_t *drive)
{
}

static void tune_cris_ide(ide_drive_t *drive, u8 pio)
{
	int setup, strobe, hold;

	switch(pio)
	{
		case 0:
			setup = ATA_PIO0_SETUP;
			strobe = ATA_PIO0_STROBE;
			hold = ATA_PIO0_HOLD;
			break;
		case 1:
			setup = ATA_PIO1_SETUP;
			strobe = ATA_PIO1_STROBE;
			hold = ATA_PIO1_HOLD;
			break;
		case 2:
			setup = ATA_PIO2_SETUP;
			strobe = ATA_PIO2_STROBE;
			hold = ATA_PIO2_HOLD;
			break;
		case 3:
			setup = ATA_PIO3_SETUP;
			strobe = ATA_PIO3_STROBE;
			hold = ATA_PIO3_HOLD;
			break;
		case 4:
			setup = ATA_PIO4_SETUP;
			strobe = ATA_PIO4_STROBE;
			hold = ATA_PIO4_HOLD;
			break;
		default:
			return;
	}

	cris_ide_set_speed(TYPE_PIO, setup, strobe, hold);
}

static int speed_cris_ide(ide_drive_t *drive, u8 speed)
{
	int cyc = 0, dvs = 0, strobe = 0, hold = 0;

	if (speed >= XFER_PIO_0 && speed <= XFER_PIO_4) {
		tune_cris_ide(drive, speed - XFER_PIO_0);
		return 0;
	}

	switch(speed)
	{
		case XFER_UDMA_0:
			cyc = ATA_UDMA0_CYC;
			dvs = ATA_UDMA0_DVS;
			break;
		case XFER_UDMA_1:
			cyc = ATA_UDMA1_CYC;
			dvs = ATA_UDMA1_DVS;
			break;
		case XFER_UDMA_2:
			cyc = ATA_UDMA2_CYC;
			dvs = ATA_UDMA2_DVS;
			break;
		case XFER_MW_DMA_0:
			strobe = ATA_DMA0_STROBE;
			hold = ATA_DMA0_HOLD;
			break;
		case XFER_MW_DMA_1:
			strobe = ATA_DMA1_STROBE;
			hold = ATA_DMA1_HOLD;
			break;
		case XFER_MW_DMA_2:
			strobe = ATA_DMA2_STROBE;
			hold = ATA_DMA2_HOLD;
			break;
		default:
			return 0;
	}

	if (speed >= XFER_UDMA_0)
		cris_ide_set_speed(TYPE_UDMA, cyc, dvs, 0);
	else
		cris_ide_set_speed(TYPE_DMA, 0, strobe, hold);

	return 0;
}

void __init
init_e100_ide (void)
{
	hw_regs_t hw;
	int ide_offsets[IDE_NR_PORTS];
	int h;
	int i;

	printk("ide: ETRAX FS built-in ATA DMA controller\n");

	for (i = IDE_DATA_OFFSET; i <= IDE_STATUS_OFFSET; i++)
		ide_offsets[i] = cris_ide_reg_addr(i, 0, 1);

	/* the IDE control register is at ATA address 6, with CS1 active instead of CS0 */
	ide_offsets[IDE_CONTROL_OFFSET] = cris_ide_reg_addr(6, 1, 0);

	/* first fill in some stuff in the ide_hwifs fields */

	for(h = 0; h < MAX_HWIFS; h++) {
		ide_hwif_t *hwif = &ide_hwifs[h];
		ide_setup_ports(&hw, cris_ide_base_address(h),
		                ide_offsets,
		                0, 0, cris_ide_ack_intr,
		                ide_default_irq(0));
		ide_register_hw(&hw, &hwif);
		hwif->mmio = 1;
		hwif->chipset = ide_etrax100;
		hwif->tuneproc = &tune_cris_ide;
		hwif->speedproc = &speed_cris_ide;
		hwif->ata_input_data = &cris_ide_input_data;
		hwif->ata_output_data = &cris_ide_output_data;
		hwif->atapi_input_bytes = &cris_atapi_input_bytes;
		hwif->atapi_output_bytes = &cris_atapi_output_bytes;
		hwif->ide_dma_check = &cris_dma_check;
		hwif->ide_dma_end = &cris_dma_end;
		hwif->dma_setup = &cris_dma_setup;
		hwif->dma_exec_cmd = &cris_dma_exec_cmd;
		hwif->ide_dma_test_irq = &cris_dma_test_irq;
		hwif->dma_start = &cris_dma_start;
		hwif->OUTB = &cris_ide_outb;
		hwif->OUTW = &cris_ide_outw;
		hwif->OUTBSYNC = &cris_ide_outbsync;
		hwif->INB = &cris_ide_inb;
		hwif->INW = &cris_ide_inw;
		hwif->dma_host_off = &cris_dma_off;
		hwif->dma_host_on = &cris_dma_on;
		hwif->dma_off_quietly = &cris_dma_off;
		hwif->udma_four = 0;
		hwif->ultra_mask = cris_ultra_mask;
		hwif->mwdma_mask = 0x07; /* Multiword DMA 0-2 */
		hwif->swdma_mask = 0x07; /* Singleword DMA 0-2 */
		hwif->autodma = 1;
		hwif->drives[0].autodma = 1;
		hwif->drives[1].autodma = 1;
	}

	/* Reset pulse */
	cris_ide_reset(0);
	udelay(25);
	cris_ide_reset(1);

	cris_ide_init();

	cris_ide_set_speed(TYPE_PIO, ATA_PIO4_SETUP, ATA_PIO4_STROBE, ATA_PIO4_HOLD);
	cris_ide_set_speed(TYPE_DMA, 0, ATA_DMA2_STROBE, ATA_DMA2_HOLD);
	cris_ide_set_speed(TYPE_UDMA, ATA_UDMA2_CYC, ATA_UDMA2_DVS, 0);
}

static int cris_dma_on (ide_drive_t *drive)
{
	return 0;
}


static cris_dma_descr_type mydescr __attribute__ ((__aligned__(16)));

/*
 * The following routines are mainly used by the ATAPI drivers.
 *
 * These routines will round up any request for an odd number of bytes,
 * so if an odd bytecount is specified, be sure that there's at least one
 * extra byte allocated for the buffer.
 */
static void
cris_atapi_input_bytes (ide_drive_t *drive, void *buffer, unsigned int bytecount)
{
	D(printk("atapi_input_bytes, buffer 0x%x, count %d\n",
	         buffer, bytecount));

	if(bytecount & 1) {
		printk("warning, odd bytecount in cdrom_in_bytes = %d.\n", bytecount);
		bytecount++; /* to round off */
	}

	/* setup DMA and start transfer */

	cris_ide_fill_descriptor(&mydescr, buffer, bytecount, 1);
	cris_ide_start_dma(drive, &mydescr, 1, TYPE_PIO, bytecount);

	/* wait for completion */
	LED_DISK_READ(1);
	cris_ide_wait_dma(1);
	LED_DISK_READ(0);
}

static void
cris_atapi_output_bytes (ide_drive_t *drive, void *buffer, unsigned int bytecount)
{
	D(printk("atapi_output_bytes, buffer 0x%x, count %d\n",
	         buffer, bytecount));

	if(bytecount & 1) {
		printk("odd bytecount %d in atapi_out_bytes!\n", bytecount);
		bytecount++;
	}

	cris_ide_fill_descriptor(&mydescr, buffer, bytecount, 1);
	cris_ide_start_dma(drive, &mydescr, 0, TYPE_PIO, bytecount);

	/* wait for completion */

	LED_DISK_WRITE(1);
	LED_DISK_READ(1);
	cris_ide_wait_dma(0);
	LED_DISK_WRITE(0);
}

/*
 * This is used for most PIO data transfers *from* the IDE interface
 */
static void
cris_ide_input_data (ide_drive_t *drive, void *buffer, unsigned int wcount)
{
	cris_atapi_input_bytes(drive, buffer, wcount << 2);
}

/*
 * This is used for most PIO data transfers *to* the IDE interface
 */
static void
cris_ide_output_data (ide_drive_t *drive, void *buffer, unsigned int wcount)
{
	cris_atapi_output_bytes(drive, buffer, wcount << 2);
}

/* we only have one DMA channel on the chip for ATA, so we can keep these statically */
static cris_dma_descr_type ata_descrs[MAX_DMA_DESCRS] __attribute__ ((__aligned__(16)));
static unsigned int ata_tot_size;

/*
 * cris_ide_build_dmatable() prepares a dma request.
 * Returns 0 if all went okay, returns 1 otherwise.
 */
static int cris_ide_build_dmatable (ide_drive_t *drive)
{
	ide_hwif_t *hwif = drive->hwif;
	struct scatterlist* sg;
	struct request *rq  = drive->hwif->hwgroup->rq;
	unsigned long size, addr;
	unsigned int count = 0;
	int i = 0;

	sg = hwif->sg_table;

	ata_tot_size = 0;

	ide_map_sg(drive, rq);
	i = hwif->sg_nents;

	while(i) {
		/*
		 * Determine addr and size of next buffer area.  We assume that
		 * individual virtual buffers are always composed linearly in
		 * physical memory.  For example, we assume that any 8kB buffer
		 * is always composed of two adjacent physical 4kB pages rather
		 * than two possibly non-adjacent physical 4kB pages.
		 */
		/* group sequential buffers into one large buffer */
		addr = page_to_phys(sg->page) + sg->offset;
		size = sg_dma_len(sg);
		while (sg++, --i) {
			if ((addr + size) != page_to_phys(sg->page) + sg->offset)
				break;
			size += sg_dma_len(sg);
		}

		/* did we run out of descriptors? */

		if(count >= MAX_DMA_DESCRS) {
			printk("%s: too few DMA descriptors\n", drive->name);
			return 1;
		}

		/* however, this case is more difficult - rw_trf_cnt cannot be more
		   than 65536 words per transfer, so in that case we need to either
		   1) use a DMA interrupt to re-trigger rw_trf_cnt and continue with
		      the descriptors, or
		   2) simply do the request here, and get dma_intr to only ide_end_request on
		      those blocks that were actually set-up for transfer.
		*/

		if(ata_tot_size + size > 131072) {
			printk("too large total ATA DMA request, %d + %d!\n", ata_tot_size, (int)size);
			return 1;
		}

		/* If size > MAX_DESCR_SIZE it has to be splitted into new descriptors. Since we
                   don't handle size > 131072 only one split is necessary */

		if(size > MAX_DESCR_SIZE) {
			cris_ide_fill_descriptor(&ata_descrs[count], (void*)addr, MAX_DESCR_SIZE, 0);
			count++;
			ata_tot_size += MAX_DESCR_SIZE;
			size -= MAX_DESCR_SIZE;
			addr += MAX_DESCR_SIZE;
		}

		cris_ide_fill_descriptor(&ata_descrs[count], (void*)addr, size,i ? 0 : 1);
		count++;
		ata_tot_size += size;
	}

	if (count) {
		/* return and say all is ok */
		return 0;
	}

	printk("%s: empty DMA table?\n", drive->name);
	return 1;	/* let the PIO routines handle this weirdness */
}

static int cris_config_drive_for_dma (ide_drive_t *drive)
{
	u8 speed = ide_dma_speed(drive, 1);

	if (!speed)
		return 0;

	speed_cris_ide(drive, speed);
	ide_config_drive_speed(drive, speed);

	return ide_dma_enable(drive);
}

/*
 * cris_dma_intr() is the handler for disk read/write DMA interrupts
 */
static ide_startstop_t cris_dma_intr (ide_drive_t *drive)
{
	LED_DISK_READ(0);
	LED_DISK_WRITE(0);

	return ide_dma_intr(drive);
}

/*
 * Functions below initiates/aborts DMA read/write operations on a drive.
 *
 * The caller is assumed to have selected the drive and programmed the drive's
 * sector address using CHS or LBA.  All that remains is to prepare for DMA
 * and then issue the actual read/write DMA/PIO command to the drive.
 *
 * For ATAPI devices, we just prepare for DMA and return. The caller should
 * then issue the packet command to the drive and call us again with
 * cris_dma_start afterwards.
 *
 * Returns 0 if all went well.
 * Returns 1 if DMA read/write could not be started, in which case
 * the caller should revert to PIO for the current request.
 */

static int cris_dma_check(ide_drive_t *drive)
{
	if (ide_use_dma(drive) && cris_config_drive_for_dma(drive))
		return 0;

	return -1;
}

static int cris_dma_end(ide_drive_t *drive)
{
	drive->waiting_for_dma = 0;
	return 0;
}

static int cris_dma_setup(ide_drive_t *drive)
{
	struct request *rq = drive->hwif->hwgroup->rq;

	cris_ide_initialize_dma(!rq_data_dir(rq));
	if (cris_ide_build_dmatable (drive)) {
		ide_map_sg(drive, rq);
		return 1;
	}

	drive->waiting_for_dma = 1;
	return 0;
}

static void cris_dma_exec_cmd(ide_drive_t *drive, u8 command)
{
	/* set the irq handler which will finish the request when DMA is done */
	ide_set_handler(drive, &cris_dma_intr, WAIT_CMD, NULL);

	/* issue cmd to drive */
	cris_ide_outb(command, IDE_COMMAND_REG);
}

static void cris_dma_start(ide_drive_t *drive)
{
	struct request *rq = drive->hwif->hwgroup->rq;
	int writing = rq_data_dir(rq);
	int type = TYPE_DMA;

	if (drive->current_speed >= XFER_UDMA_0)
		type = TYPE_UDMA;

	cris_ide_start_dma(drive, &ata_descrs[0], writing ? 0 : 1, type, ata_tot_size);

	if (writing) {
		LED_DISK_WRITE(1);
	} else {
		LED_DISK_READ(1);
	}
}
