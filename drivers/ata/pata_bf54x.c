/*
 * File:         drivers/ata/pata_bf54x.c
 * Author:       Sonic Zhang <sonic.zhang@analog.com>
 *
 * Created:
 * Description:  PATA Driver for blackfin 54x
 *
 * Modified:
 *               Copyright 2007 Analog Devices Inc.
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <scsi/scsi_host.h>
#include <linux/libata.h>
#include <linux/platform_device.h>
#include <asm/dma.h>
#include <asm/gpio.h>
#include <asm/portmux.h>

#define DRV_NAME		"pata-bf54x"
#define DRV_VERSION		"0.9"

#define ATA_REG_CTRL		0x0E
#define ATA_REG_ALTSTATUS	ATA_REG_CTRL

/* These are the offset of the controller's registers */
#define ATAPI_OFFSET_CONTROL		0x00
#define ATAPI_OFFSET_STATUS		0x04
#define ATAPI_OFFSET_DEV_ADDR		0x08
#define ATAPI_OFFSET_DEV_TXBUF		0x0c
#define ATAPI_OFFSET_DEV_RXBUF		0x10
#define ATAPI_OFFSET_INT_MASK		0x14
#define ATAPI_OFFSET_INT_STATUS		0x18
#define ATAPI_OFFSET_XFER_LEN		0x1c
#define ATAPI_OFFSET_LINE_STATUS	0x20
#define ATAPI_OFFSET_SM_STATE		0x24
#define ATAPI_OFFSET_TERMINATE		0x28
#define ATAPI_OFFSET_PIO_TFRCNT		0x2c
#define ATAPI_OFFSET_DMA_TFRCNT		0x30
#define ATAPI_OFFSET_UMAIN_TFRCNT	0x34
#define ATAPI_OFFSET_UDMAOUT_TFRCNT	0x38
#define ATAPI_OFFSET_REG_TIM_0		0x40
#define ATAPI_OFFSET_PIO_TIM_0		0x44
#define ATAPI_OFFSET_PIO_TIM_1		0x48
#define ATAPI_OFFSET_MULTI_TIM_0	0x50
#define ATAPI_OFFSET_MULTI_TIM_1	0x54
#define ATAPI_OFFSET_MULTI_TIM_2	0x58
#define ATAPI_OFFSET_ULTRA_TIM_0	0x60
#define ATAPI_OFFSET_ULTRA_TIM_1	0x64
#define ATAPI_OFFSET_ULTRA_TIM_2	0x68
#define ATAPI_OFFSET_ULTRA_TIM_3	0x6c


#define ATAPI_GET_CONTROL(base)\
	bfin_read16(base + ATAPI_OFFSET_CONTROL)
#define ATAPI_SET_CONTROL(base, val)\
	bfin_write16(base + ATAPI_OFFSET_CONTROL, val)
#define ATAPI_GET_STATUS(base)\
	bfin_read16(base + ATAPI_OFFSET_STATUS)
#define ATAPI_GET_DEV_ADDR(base)\
	bfin_read16(base + ATAPI_OFFSET_DEV_ADDR)
#define ATAPI_SET_DEV_ADDR(base, val)\
	bfin_write16(base + ATAPI_OFFSET_DEV_ADDR, val)
#define ATAPI_GET_DEV_TXBUF(base)\
	bfin_read16(base + ATAPI_OFFSET_DEV_TXBUF)
#define ATAPI_SET_DEV_TXBUF(base, val)\
	bfin_write16(base + ATAPI_OFFSET_DEV_TXBUF, val)
#define ATAPI_GET_DEV_RXBUF(base)\
	bfin_read16(base + ATAPI_OFFSET_DEV_RXBUF)
#define ATAPI_SET_DEV_RXBUF(base, val)\
	bfin_write16(base + ATAPI_OFFSET_DEV_RXBUF, val)
#define ATAPI_GET_INT_MASK(base)\
	bfin_read16(base + ATAPI_OFFSET_INT_MASK)
#define ATAPI_SET_INT_MASK(base, val)\
	bfin_write16(base + ATAPI_OFFSET_INT_MASK, val)
#define ATAPI_GET_INT_STATUS(base)\
	bfin_read16(base + ATAPI_OFFSET_INT_STATUS)
#define ATAPI_SET_INT_STATUS(base, val)\
	bfin_write16(base + ATAPI_OFFSET_INT_STATUS, val)
#define ATAPI_GET_XFER_LEN(base)\
	bfin_read16(base + ATAPI_OFFSET_XFER_LEN)
#define ATAPI_SET_XFER_LEN(base, val)\
	bfin_write16(base + ATAPI_OFFSET_XFER_LEN, val)
#define ATAPI_GET_LINE_STATUS(base)\
	bfin_read16(base + ATAPI_OFFSET_LINE_STATUS)
#define ATAPI_GET_SM_STATE(base)\
	bfin_read16(base + ATAPI_OFFSET_SM_STATE)
#define ATAPI_GET_TERMINATE(base)\
	bfin_read16(base + ATAPI_OFFSET_TERMINATE)
#define ATAPI_SET_TERMINATE(base, val)\
	bfin_write16(base + ATAPI_OFFSET_TERMINATE, val)
#define ATAPI_GET_PIO_TFRCNT(base)\
	bfin_read16(base + ATAPI_OFFSET_PIO_TFRCNT)
#define ATAPI_GET_DMA_TFRCNT(base)\
	bfin_read16(base + ATAPI_OFFSET_DMA_TFRCNT)
#define ATAPI_GET_UMAIN_TFRCNT(base)\
	bfin_read16(base + ATAPI_OFFSET_UMAIN_TFRCNT)
#define ATAPI_GET_UDMAOUT_TFRCNT(base)\
	bfin_read16(base + ATAPI_OFFSET_UDMAOUT_TFRCNT)
#define ATAPI_GET_REG_TIM_0(base)\
	bfin_read16(base + ATAPI_OFFSET_REG_TIM_0)
#define ATAPI_SET_REG_TIM_0(base, val)\
	bfin_write16(base + ATAPI_OFFSET_REG_TIM_0, val)
#define ATAPI_GET_PIO_TIM_0(base)\
	bfin_read16(base + ATAPI_OFFSET_PIO_TIM_0)
#define ATAPI_SET_PIO_TIM_0(base, val)\
	bfin_write16(base + ATAPI_OFFSET_PIO_TIM_0, val)
#define ATAPI_GET_PIO_TIM_1(base)\
	bfin_read16(base + ATAPI_OFFSET_PIO_TIM_1)
#define ATAPI_SET_PIO_TIM_1(base, val)\
	bfin_write16(base + ATAPI_OFFSET_PIO_TIM_1, val)
#define ATAPI_GET_MULTI_TIM_0(base)\
	bfin_read16(base + ATAPI_OFFSET_MULTI_TIM_0)
#define ATAPI_SET_MULTI_TIM_0(base, val)\
	bfin_write16(base + ATAPI_OFFSET_MULTI_TIM_0, val)
#define ATAPI_GET_MULTI_TIM_1(base)\
	bfin_read16(base + ATAPI_OFFSET_MULTI_TIM_1)
#define ATAPI_SET_MULTI_TIM_1(base, val)\
	bfin_write16(base + ATAPI_OFFSET_MULTI_TIM_1, val)
#define ATAPI_GET_MULTI_TIM_2(base)\
	bfin_read16(base + ATAPI_OFFSET_MULTI_TIM_2)
#define ATAPI_SET_MULTI_TIM_2(base, val)\
	bfin_write16(base + ATAPI_OFFSET_MULTI_TIM_2, val)
#define ATAPI_GET_ULTRA_TIM_0(base)\
	bfin_read16(base + ATAPI_OFFSET_ULTRA_TIM_0)
#define ATAPI_SET_ULTRA_TIM_0(base, val)\
	bfin_write16(base + ATAPI_OFFSET_ULTRA_TIM_0, val)
#define ATAPI_GET_ULTRA_TIM_1(base)\
	bfin_read16(base + ATAPI_OFFSET_ULTRA_TIM_1)
#define ATAPI_SET_ULTRA_TIM_1(base, val)\
	bfin_write16(base + ATAPI_OFFSET_ULTRA_TIM_1, val)
#define ATAPI_GET_ULTRA_TIM_2(base)\
	bfin_read16(base + ATAPI_OFFSET_ULTRA_TIM_2)
#define ATAPI_SET_ULTRA_TIM_2(base, val)\
	bfin_write16(base + ATAPI_OFFSET_ULTRA_TIM_2, val)
#define ATAPI_GET_ULTRA_TIM_3(base)\
	bfin_read16(base + ATAPI_OFFSET_ULTRA_TIM_3)
#define ATAPI_SET_ULTRA_TIM_3(base, val)\
	bfin_write16(base + ATAPI_OFFSET_ULTRA_TIM_3, val)

/**
 * PIO Mode - Frequency compatibility
 */
/* mode: 0         1         2         3         4 */
static const u32 pio_fsclk[] =
{ 33333333, 33333333, 33333333, 33333333, 33333333 };

/**
 * MDMA Mode - Frequency compatibility
 */
/*               mode:      0         1         2        */
static const u32 mdma_fsclk[] = { 33333333, 33333333, 33333333 };

/**
 * UDMA Mode - Frequency compatibility
 *
 * UDMA5 - 100 MB/s   - SCLK  = 133 MHz
 * UDMA4 - 66 MB/s    - SCLK >=  80 MHz
 * UDMA3 - 44.4 MB/s  - SCLK >=  50 MHz
 * UDMA2 - 33 MB/s    - SCLK >=  40 MHz
 */
/* mode: 0         1         2         3         4          5 */
static const u32 udma_fsclk[] =
{ 33333333, 33333333, 40000000, 50000000, 80000000, 133333333 };

/**
 * Register transfer timing table
 */
/*               mode:       0    1    2    3    4    */
/* Cycle Time                     */
static const u32 reg_t0min[]   = { 600, 383, 330, 180, 120 };
/* DIOR/DIOW to end cycle         */
static const u32 reg_t2min[]   = { 290, 290, 290, 70,  25  };
/* DIOR/DIOW asserted pulse width */
static const u32 reg_teocmin[] = { 290, 290, 290, 80,  70  };

/**
 * PIO timing table
 */
/*               mode:       0    1    2    3    4    */
/* Cycle Time                     */
static const u32 pio_t0min[]   = { 600, 383, 240, 180, 120 };
/* Address valid to DIOR/DIORW    */
static const u32 pio_t1min[]   = { 70,  50,  30,  30,  25  };
/* DIOR/DIOW to end cycle         */
static const u32 pio_t2min[]   = { 165, 125, 100, 80,  70  };
/* DIOR/DIOW asserted pulse width */
static const u32 pio_teocmin[] = { 165, 125, 100, 70,  25  };
/* DIOW data hold                 */
static const u32 pio_t4min[]   = { 30,  20,  15,  10,  10  };

/* ******************************************************************
 * Multiword DMA timing table
 * ******************************************************************
 */
/*               mode:       0   1    2        */
/* Cycle Time                     */
static const u32 mdma_t0min[]  = { 480, 150, 120 };
/* DIOR/DIOW asserted pulse width */
static const u32 mdma_tdmin[]  = { 215, 80,  70  };
/* DMACK to read data released    */
static const u32 mdma_thmin[]  = { 20,  15,  10  };
/* DIOR/DIOW to DMACK hold        */
static const u32 mdma_tjmin[]  = { 20,  5,   5   };
/* DIOR negated pulse width       */
static const u32 mdma_tkrmin[] = { 50,  50,  25  };
/* DIOR negated pulse width       */
static const u32 mdma_tkwmin[] = { 215, 50,  25  };
/* CS[1:0] valid to DIOR/DIOW     */
static const u32 mdma_tmmin[]  = { 50,  30,  25  };
/* DMACK to read data released    */
static const u32 mdma_tzmax[]  = { 20,  25,  25  };

/**
 * Ultra DMA timing table
 */
/*               mode:         0    1    2    3    4    5       */
static const u32 udma_tcycmin[]  = { 112, 73,  54,  39,  25,  17 };
static const u32 udma_tdvsmin[]  = { 70,  48,  31,  20,  7,   5  };
static const u32 udma_tenvmax[]  = { 70,  70,  70,  55,  55,  50 };
static const u32 udma_trpmin[]   = { 160, 125, 100, 100, 100, 85 };
static const u32 udma_tmin[]     = { 5,   5,   5,   5,   3,   3  };


static const u32 udma_tmlimin = 20;
static const u32 udma_tzahmin = 20;
static const u32 udma_tenvmin = 20;
static const u32 udma_tackmin = 20;
static const u32 udma_tssmin = 50;

/**
 *
 *	Function:       num_clocks_min
 *
 *	Description:
 *	calculate number of SCLK cycles to meet minimum timing
 */
static unsigned short num_clocks_min(unsigned long tmin,
				unsigned long fsclk)
{
	unsigned long tmp ;
	unsigned short result;

	tmp = tmin * (fsclk/1000/1000) / 1000;
	result = (unsigned short)tmp;
	if ((tmp*1000*1000) < (tmin*(fsclk/1000))) {
		result++;
	}

	return result;
}

/**
 *	bfin_set_piomode - Initialize host controller PATA PIO timings
 *	@ap: Port whose timings we are configuring
 *	@adev: um
 *
 *	Set PIO mode for device.
 *
 *	LOCKING:
 *	None (inherited from caller).
 */

static void bfin_set_piomode(struct ata_port *ap, struct ata_device *adev)
{
	int mode = adev->pio_mode - XFER_PIO_0;
	void __iomem *base = (void __iomem *)ap->ioaddr.ctl_addr;
	unsigned int fsclk = get_sclk();
	unsigned short teoc_reg, t2_reg, teoc_pio;
	unsigned short t4_reg, t2_pio, t1_reg;
	unsigned short n0, n6, t6min = 5;

	/* the most restrictive timing value is t6 and tc, the DIOW - data hold
	* If one SCLK pulse is longer than this minimum value then register
	* transfers cannot be supported at this frequency.
	*/
	n6 = num_clocks_min(t6min, fsclk);
	if (mode >= 0 && mode <= 4 && n6 >= 1) {
		dev_dbg(adev->link->ap->dev, "set piomode: mode=%d, fsclk=%ud\n", mode, fsclk);
		/* calculate the timing values for register transfers. */
		while (mode > 0 && pio_fsclk[mode] > fsclk)
			mode--;

		/* DIOR/DIOW to end cycle time */
		t2_reg = num_clocks_min(reg_t2min[mode], fsclk);
		/* DIOR/DIOW asserted pulse width */
		teoc_reg = num_clocks_min(reg_teocmin[mode], fsclk);
		/* Cycle Time */
		n0  = num_clocks_min(reg_t0min[mode], fsclk);

		/* increase t2 until we meed the minimum cycle length */
		if (t2_reg + teoc_reg < n0)
			t2_reg = n0 - teoc_reg;

		/* calculate the timing values for pio transfers. */

		/* DIOR/DIOW to end cycle time */
		t2_pio = num_clocks_min(pio_t2min[mode], fsclk);
		/* DIOR/DIOW asserted pulse width */
		teoc_pio = num_clocks_min(pio_teocmin[mode], fsclk);
		/* Cycle Time */
		n0  = num_clocks_min(pio_t0min[mode], fsclk);

		/* increase t2 until we meed the minimum cycle length */
		if (t2_pio + teoc_pio < n0)
			t2_pio = n0 - teoc_pio;

		/* Address valid to DIOR/DIORW */
		t1_reg = num_clocks_min(pio_t1min[mode], fsclk);

		/* DIOW data hold */
		t4_reg = num_clocks_min(pio_t4min[mode], fsclk);

		ATAPI_SET_REG_TIM_0(base, (teoc_reg<<8 | t2_reg));
		ATAPI_SET_PIO_TIM_0(base, (t4_reg<<12 | t2_pio<<4 | t1_reg));
		ATAPI_SET_PIO_TIM_1(base, teoc_pio);
		if (mode > 2) {
			ATAPI_SET_CONTROL(base,
				ATAPI_GET_CONTROL(base) | IORDY_EN);
		} else {
			ATAPI_SET_CONTROL(base,
				ATAPI_GET_CONTROL(base) & ~IORDY_EN);
		}

		/* Disable host ATAPI PIO interrupts */
		ATAPI_SET_INT_MASK(base, ATAPI_GET_INT_MASK(base)
			& ~(PIO_DONE_MASK | HOST_TERM_XFER_MASK));
		SSYNC();
	}
}

/**
 *	bfin_set_dmamode - Initialize host controller PATA DMA timings
 *	@ap: Port whose timings we are configuring
 *	@adev: um
 *	@udma: udma mode, 0 - 6
 *
 *	Set UDMA mode for device.
 *
 *	LOCKING:
 *	None (inherited from caller).
 */

static void bfin_set_dmamode(struct ata_port *ap, struct ata_device *adev)
{
	int mode;
	void __iomem *base = (void __iomem *)ap->ioaddr.ctl_addr;
	unsigned long fsclk = get_sclk();
	unsigned short tenv, tack, tcyc_tdvs, tdvs, tmli, tss, trp, tzah;
	unsigned short tm, td, tkr, tkw, teoc, th;
	unsigned short n0, nf, tfmin = 5;
	unsigned short nmin, tcyc;

	mode = adev->dma_mode - XFER_UDMA_0;
	if (mode >= 0 && mode <= 5) {
		dev_dbg(adev->link->ap->dev, "set udmamode: mode=%d\n", mode);
		/* the most restrictive timing value is t6 and tc,
		 * the DIOW - data hold. If one SCLK pulse is longer
		 * than this minimum value then register
		 * transfers cannot be supported at this frequency.
		 */
		while (mode > 0 && udma_fsclk[mode] > fsclk)
			mode--;

		nmin = num_clocks_min(udma_tmin[mode], fsclk);
		if (nmin >= 1) {
			/* calculate the timing values for Ultra DMA. */
			tdvs = num_clocks_min(udma_tdvsmin[mode], fsclk);
			tcyc = num_clocks_min(udma_tcycmin[mode], fsclk);
			tcyc_tdvs = 2;

			/* increase tcyc - tdvs (tcyc_tdvs) until we meed
			 * the minimum cycle length
			 */
			if (tdvs + tcyc_tdvs < tcyc)
				tcyc_tdvs = tcyc - tdvs;

			/* Mow assign the values required for the timing
			 * registers
			 */
			if (tcyc_tdvs < 2)
				tcyc_tdvs = 2;

			if (tdvs < 2)
				tdvs = 2;

			tack = num_clocks_min(udma_tackmin, fsclk);
			tss = num_clocks_min(udma_tssmin, fsclk);
			tmli = num_clocks_min(udma_tmlimin, fsclk);
			tzah = num_clocks_min(udma_tzahmin, fsclk);
			trp = num_clocks_min(udma_trpmin[mode], fsclk);
			tenv = num_clocks_min(udma_tenvmin, fsclk);
			if (tenv <= udma_tenvmax[mode]) {
				ATAPI_SET_ULTRA_TIM_0(base, (tenv<<8 | tack));
				ATAPI_SET_ULTRA_TIM_1(base,
					(tcyc_tdvs<<8 | tdvs));
				ATAPI_SET_ULTRA_TIM_2(base, (tmli<<8 | tss));
				ATAPI_SET_ULTRA_TIM_3(base, (trp<<8 | tzah));

				/* Enable host ATAPI Untra DMA interrupts */
				ATAPI_SET_INT_MASK(base,
					ATAPI_GET_INT_MASK(base)
					| UDMAIN_DONE_MASK
					| UDMAOUT_DONE_MASK
					| UDMAIN_TERM_MASK
					| UDMAOUT_TERM_MASK);
			}
		}
	}

	mode = adev->dma_mode - XFER_MW_DMA_0;
	if (mode >= 0 && mode <= 2) {
		dev_dbg(adev->link->ap->dev, "set mdmamode: mode=%d\n", mode);
		/* the most restrictive timing value is tf, the DMACK to
		 * read data released. If one SCLK pulse is longer than
		 * this maximum value then the MDMA mode
		 * cannot be supported at this frequency.
		 */
		while (mode > 0 && mdma_fsclk[mode] > fsclk)
			mode--;

		nf = num_clocks_min(tfmin, fsclk);
		if (nf >= 1) {
			/* calculate the timing values for Multi-word DMA. */

			/* DIOR/DIOW asserted pulse width */
			td = num_clocks_min(mdma_tdmin[mode], fsclk);

			/* DIOR negated pulse width */
			tkw = num_clocks_min(mdma_tkwmin[mode], fsclk);

			/* Cycle Time */
			n0  = num_clocks_min(mdma_t0min[mode], fsclk);

			/* increase tk until we meed the minimum cycle length */
			if (tkw + td < n0)
				tkw = n0 - td;

			/* DIOR negated pulse width - read */
			tkr = num_clocks_min(mdma_tkrmin[mode], fsclk);
			/* CS{1:0] valid to DIOR/DIOW */
			tm = num_clocks_min(mdma_tmmin[mode], fsclk);
			/* DIOR/DIOW to DMACK hold */
			teoc = num_clocks_min(mdma_tjmin[mode], fsclk);
			/* DIOW Data hold */
			th = num_clocks_min(mdma_thmin[mode], fsclk);

			ATAPI_SET_MULTI_TIM_0(base, (tm<<8 | td));
			ATAPI_SET_MULTI_TIM_1(base, (tkr<<8 | tkw));
			ATAPI_SET_MULTI_TIM_2(base, (teoc<<8 | th));

			/* Enable host ATAPI Multi DMA interrupts */
			ATAPI_SET_INT_MASK(base, ATAPI_GET_INT_MASK(base)
				| MULTI_DONE_MASK | MULTI_TERM_MASK);
			SSYNC();
		}
	}
	return;
}

/**
 *
 *    Function:       wait_complete
 *
 *    Description:    Waits the interrupt from device
 *
 */
static inline void wait_complete(void __iomem *base, unsigned short mask)
{
	unsigned short status;
	unsigned int i = 0;

#define PATA_BF54X_WAIT_TIMEOUT		10000

	for (i = 0; i < PATA_BF54X_WAIT_TIMEOUT; i++) {
		status = ATAPI_GET_INT_STATUS(base) & mask;
		if (status)
			break;
	}

	ATAPI_SET_INT_STATUS(base, mask);
}

/**
 *
 *    Function:       write_atapi_register
 *
 *    Description:    Writes to ATA Device Resgister
 *
 */

static void write_atapi_register(void __iomem *base,
		unsigned long ata_reg, unsigned short value)
{
	/* Program the ATA_DEV_TXBUF register with write data (to be
	 * written into the device).
	 */
	ATAPI_SET_DEV_TXBUF(base, value);

	/* Program the ATA_DEV_ADDR register with address of the
	 * device register (0x01 to 0x0F).
	 */
	ATAPI_SET_DEV_ADDR(base, ata_reg);

	/* Program the ATA_CTRL register with dir set to write (1)
	 */
	ATAPI_SET_CONTROL(base, (ATAPI_GET_CONTROL(base) | XFER_DIR));

	/* ensure PIO DMA is not set */
	ATAPI_SET_CONTROL(base, (ATAPI_GET_CONTROL(base) & ~PIO_USE_DMA));

	/* and start the transfer */
	ATAPI_SET_CONTROL(base, (ATAPI_GET_CONTROL(base) | PIO_START));

	/* Wait for the interrupt to indicate the end of the transfer.
	 * (We need to wait on and clear rhe ATA_DEV_INT interrupt status)
	 */
	wait_complete(base, PIO_DONE_INT);
}

/**
 *
 *	Function:       read_atapi_register
 *
 *Description:    Reads from ATA Device Resgister
 *
 */

static unsigned short read_atapi_register(void __iomem *base,
		unsigned long ata_reg)
{
	/* Program the ATA_DEV_ADDR register with address of the
	 * device register (0x01 to 0x0F).
	 */
	ATAPI_SET_DEV_ADDR(base, ata_reg);

	/* Program the ATA_CTRL register with dir set to read (0) and
	 */
	ATAPI_SET_CONTROL(base, (ATAPI_GET_CONTROL(base) & ~XFER_DIR));

	/* ensure PIO DMA is not set */
	ATAPI_SET_CONTROL(base, (ATAPI_GET_CONTROL(base) & ~PIO_USE_DMA));

	/* and start the transfer */
	ATAPI_SET_CONTROL(base, (ATAPI_GET_CONTROL(base) | PIO_START));

	/* Wait for the interrupt to indicate the end of the transfer.
	 * (PIO_DONE interrupt is set and it doesn't seem to matter
	 * that we don't clear it)
	 */
	wait_complete(base, PIO_DONE_INT);

	/* Read the ATA_DEV_RXBUF register with write data (to be
	 * written into the device).
	 */
	return ATAPI_GET_DEV_RXBUF(base);
}

/**
 *
 *    Function:       write_atapi_register_data
 *
 *    Description:    Writes to ATA Device Resgister
 *
 */

static void write_atapi_data(void __iomem *base,
		int len, unsigned short *buf)
{
	int i;

	/* Set transfer length to 1 */
	ATAPI_SET_XFER_LEN(base, 1);

	/* Program the ATA_DEV_ADDR register with address of the
	 * ATA_REG_DATA
	 */
	ATAPI_SET_DEV_ADDR(base, ATA_REG_DATA);

	/* Program the ATA_CTRL register with dir set to write (1)
	 */
	ATAPI_SET_CONTROL(base, (ATAPI_GET_CONTROL(base) | XFER_DIR));

	/* ensure PIO DMA is not set */
	ATAPI_SET_CONTROL(base, (ATAPI_GET_CONTROL(base) & ~PIO_USE_DMA));

	for (i = 0; i < len; i++) {
		/* Program the ATA_DEV_TXBUF register with write data (to be
		 * written into the device).
		 */
		ATAPI_SET_DEV_TXBUF(base, buf[i]);

		/* and start the transfer */
		ATAPI_SET_CONTROL(base, (ATAPI_GET_CONTROL(base) | PIO_START));

		/* Wait for the interrupt to indicate the end of the transfer.
		 * (We need to wait on and clear rhe ATA_DEV_INT
		 * interrupt status)
		 */
		wait_complete(base, PIO_DONE_INT);
	}
}

/**
 *
 *	Function:       read_atapi_register_data
 *
 *	Description:    Reads from ATA Device Resgister
 *
 */

static void read_atapi_data(void __iomem *base,
		int len, unsigned short *buf)
{
	int i;

	/* Set transfer length to 1 */
	ATAPI_SET_XFER_LEN(base, 1);

	/* Program the ATA_DEV_ADDR register with address of the
	 * ATA_REG_DATA
	 */
	ATAPI_SET_DEV_ADDR(base, ATA_REG_DATA);

	/* Program the ATA_CTRL register with dir set to read (0) and
	 */
	ATAPI_SET_CONTROL(base, (ATAPI_GET_CONTROL(base) & ~XFER_DIR));

	/* ensure PIO DMA is not set */
	ATAPI_SET_CONTROL(base, (ATAPI_GET_CONTROL(base) & ~PIO_USE_DMA));

	for (i = 0; i < len; i++) {
		/* and start the transfer */
		ATAPI_SET_CONTROL(base, (ATAPI_GET_CONTROL(base) | PIO_START));

		/* Wait for the interrupt to indicate the end of the transfer.
		 * (PIO_DONE interrupt is set and it doesn't seem to matter
		 * that we don't clear it)
		 */
		wait_complete(base, PIO_DONE_INT);

		/* Read the ATA_DEV_RXBUF register with write data (to be
		 * written into the device).
		 */
		buf[i] = ATAPI_GET_DEV_RXBUF(base);
	}
}

/**
 *	bfin_tf_load - send taskfile registers to host controller
 *	@ap: Port to which output is sent
 *	@tf: ATA taskfile register set
 *
 *	Note: Original code is ata_sff_tf_load().
 */

static void bfin_tf_load(struct ata_port *ap, const struct ata_taskfile *tf)
{
	void __iomem *base = (void __iomem *)ap->ioaddr.ctl_addr;
	unsigned int is_addr = tf->flags & ATA_TFLAG_ISADDR;

	if (tf->ctl != ap->last_ctl) {
		write_atapi_register(base, ATA_REG_CTRL, tf->ctl);
		ap->last_ctl = tf->ctl;
		ata_wait_idle(ap);
	}

	if (is_addr) {
		if (tf->flags & ATA_TFLAG_LBA48) {
			write_atapi_register(base, ATA_REG_FEATURE,
						tf->hob_feature);
			write_atapi_register(base, ATA_REG_NSECT,
						tf->hob_nsect);
			write_atapi_register(base, ATA_REG_LBAL, tf->hob_lbal);
			write_atapi_register(base, ATA_REG_LBAM, tf->hob_lbam);
			write_atapi_register(base, ATA_REG_LBAH, tf->hob_lbah);
			dev_dbg(ap->dev, "hob: feat 0x%X nsect 0x%X, lba 0x%X "
				 "0x%X 0x%X\n",
				tf->hob_feature,
				tf->hob_nsect,
				tf->hob_lbal,
				tf->hob_lbam,
				tf->hob_lbah);
		}

		write_atapi_register(base, ATA_REG_FEATURE, tf->feature);
		write_atapi_register(base, ATA_REG_NSECT, tf->nsect);
		write_atapi_register(base, ATA_REG_LBAL, tf->lbal);
		write_atapi_register(base, ATA_REG_LBAM, tf->lbam);
		write_atapi_register(base, ATA_REG_LBAH, tf->lbah);
		dev_dbg(ap->dev, "feat 0x%X nsect 0x%X lba 0x%X 0x%X 0x%X\n",
			tf->feature,
			tf->nsect,
			tf->lbal,
			tf->lbam,
			tf->lbah);
	}

	if (tf->flags & ATA_TFLAG_DEVICE) {
		write_atapi_register(base, ATA_REG_DEVICE, tf->device);
		dev_dbg(ap->dev, "device 0x%X\n", tf->device);
	}

	ata_wait_idle(ap);
}

/**
 *	bfin_check_status - Read device status reg & clear interrupt
 *	@ap: port where the device is
 *
 *	Note: Original code is ata_check_status().
 */

static u8 bfin_check_status(struct ata_port *ap)
{
	void __iomem *base = (void __iomem *)ap->ioaddr.ctl_addr;
	return read_atapi_register(base, ATA_REG_STATUS);
}

/**
 *	bfin_tf_read - input device's ATA taskfile shadow registers
 *	@ap: Port from which input is read
 *	@tf: ATA taskfile register set for storing input
 *
 *	Note: Original code is ata_sff_tf_read().
 */

static void bfin_tf_read(struct ata_port *ap, struct ata_taskfile *tf)
{
	void __iomem *base = (void __iomem *)ap->ioaddr.ctl_addr;

	tf->command = bfin_check_status(ap);
	tf->feature = read_atapi_register(base, ATA_REG_ERR);
	tf->nsect = read_atapi_register(base, ATA_REG_NSECT);
	tf->lbal = read_atapi_register(base, ATA_REG_LBAL);
	tf->lbam = read_atapi_register(base, ATA_REG_LBAM);
	tf->lbah = read_atapi_register(base, ATA_REG_LBAH);
	tf->device = read_atapi_register(base, ATA_REG_DEVICE);

	if (tf->flags & ATA_TFLAG_LBA48) {
		write_atapi_register(base, ATA_REG_CTRL, tf->ctl | ATA_HOB);
		tf->hob_feature = read_atapi_register(base, ATA_REG_ERR);
		tf->hob_nsect = read_atapi_register(base, ATA_REG_NSECT);
		tf->hob_lbal = read_atapi_register(base, ATA_REG_LBAL);
		tf->hob_lbam = read_atapi_register(base, ATA_REG_LBAM);
		tf->hob_lbah = read_atapi_register(base, ATA_REG_LBAH);
	}
}

/**
 *	bfin_exec_command - issue ATA command to host controller
 *	@ap: port to which command is being issued
 *	@tf: ATA taskfile register set
 *
 *	Note: Original code is ata_sff_exec_command().
 */

static void bfin_exec_command(struct ata_port *ap,
			      const struct ata_taskfile *tf)
{
	void __iomem *base = (void __iomem *)ap->ioaddr.ctl_addr;
	dev_dbg(ap->dev, "ata%u: cmd 0x%X\n", ap->print_id, tf->command);

	write_atapi_register(base, ATA_REG_CMD, tf->command);
	ata_sff_pause(ap);
}

/**
 *	bfin_check_altstatus - Read device alternate status reg
 *	@ap: port where the device is
 */

static u8 bfin_check_altstatus(struct ata_port *ap)
{
	void __iomem *base = (void __iomem *)ap->ioaddr.ctl_addr;
	return read_atapi_register(base, ATA_REG_ALTSTATUS);
}

/**
 *	bfin_dev_select - Select device 0/1 on ATA bus
 *	@ap: ATA channel to manipulate
 *	@device: ATA device (numbered from zero) to select
 *
 *	Note: Original code is ata_sff_dev_select().
 */

static void bfin_dev_select(struct ata_port *ap, unsigned int device)
{
	void __iomem *base = (void __iomem *)ap->ioaddr.ctl_addr;
	u8 tmp;

	if (device == 0)
		tmp = ATA_DEVICE_OBS;
	else
		tmp = ATA_DEVICE_OBS | ATA_DEV1;

	write_atapi_register(base, ATA_REG_DEVICE, tmp);
	ata_sff_pause(ap);
}

/**
 *	bfin_bmdma_setup - Set up IDE DMA transaction
 *	@qc: Info associated with this ATA transaction.
 *
 *	Note: Original code is ata_bmdma_setup().
 */

static void bfin_bmdma_setup(struct ata_queued_cmd *qc)
{
	unsigned short config = WDSIZE_16;
	struct scatterlist *sg;
	unsigned int si;

	dev_dbg(qc->ap->dev, "in atapi dma setup\n");
	/* Program the ATA_CTRL register with dir */
	if (qc->tf.flags & ATA_TFLAG_WRITE) {
		/* fill the ATAPI DMA controller */
		set_dma_config(CH_ATAPI_TX, config);
		set_dma_x_modify(CH_ATAPI_TX, 2);
		for_each_sg(qc->sg, sg, qc->n_elem, si) {
			set_dma_start_addr(CH_ATAPI_TX, sg_dma_address(sg));
			set_dma_x_count(CH_ATAPI_TX, sg_dma_len(sg) >> 1);
		}
	} else {
		config |= WNR;
		/* fill the ATAPI DMA controller */
		set_dma_config(CH_ATAPI_RX, config);
		set_dma_x_modify(CH_ATAPI_RX, 2);
		for_each_sg(qc->sg, sg, qc->n_elem, si) {
			set_dma_start_addr(CH_ATAPI_RX, sg_dma_address(sg));
			set_dma_x_count(CH_ATAPI_RX, sg_dma_len(sg) >> 1);
		}
	}
}

/**
 *	bfin_bmdma_start - Start an IDE DMA transaction
 *	@qc: Info associated with this ATA transaction.
 *
 *	Note: Original code is ata_bmdma_start().
 */

static void bfin_bmdma_start(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	void __iomem *base = (void __iomem *)ap->ioaddr.ctl_addr;
	struct scatterlist *sg;
	unsigned int si;

	dev_dbg(qc->ap->dev, "in atapi dma start\n");
	if (!(ap->udma_mask || ap->mwdma_mask))
		return;

	/* start ATAPI DMA controller*/
	if (qc->tf.flags & ATA_TFLAG_WRITE) {
		/*
		 * On blackfin arch, uncacheable memory is not
		 * allocated with flag GFP_DMA. DMA buffer from
		 * common kenel code should be flushed if WB
		 * data cache is enabled. Otherwise, this loop
		 * is an empty loop and optimized out.
		 */
		for_each_sg(qc->sg, sg, qc->n_elem, si) {
			flush_dcache_range(sg_dma_address(sg),
				sg_dma_address(sg) + sg_dma_len(sg));
		}
		enable_dma(CH_ATAPI_TX);
		dev_dbg(qc->ap->dev, "enable udma write\n");

		/* Send ATA DMA write command */
		bfin_exec_command(ap, &qc->tf);

		/* set ATA DMA write direction */
		ATAPI_SET_CONTROL(base, (ATAPI_GET_CONTROL(base)
			| XFER_DIR));
	} else {
		enable_dma(CH_ATAPI_RX);
		dev_dbg(qc->ap->dev, "enable udma read\n");

		/* Send ATA DMA read command */
		bfin_exec_command(ap, &qc->tf);

		/* set ATA DMA read direction */
		ATAPI_SET_CONTROL(base, (ATAPI_GET_CONTROL(base)
			& ~XFER_DIR));
	}

	/* Reset all transfer count */
	ATAPI_SET_CONTROL(base, ATAPI_GET_CONTROL(base) | TFRCNT_RST);

		/* Set transfer length to buffer len */
	for_each_sg(qc->sg, sg, qc->n_elem, si) {
		ATAPI_SET_XFER_LEN(base, (sg_dma_len(sg) >> 1));
	}

	/* Enable ATA DMA operation*/
	if (ap->udma_mask)
		ATAPI_SET_CONTROL(base, ATAPI_GET_CONTROL(base)
			| ULTRA_START);
	else
		ATAPI_SET_CONTROL(base, ATAPI_GET_CONTROL(base)
			| MULTI_START);
}

/**
 *	bfin_bmdma_stop - Stop IDE DMA transfer
 *	@qc: Command we are ending DMA for
 */

static void bfin_bmdma_stop(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct scatterlist *sg;
	unsigned int si;

	dev_dbg(qc->ap->dev, "in atapi dma stop\n");
	if (!(ap->udma_mask || ap->mwdma_mask))
		return;

	/* stop ATAPI DMA controller*/
	if (qc->tf.flags & ATA_TFLAG_WRITE)
		disable_dma(CH_ATAPI_TX);
	else {
		disable_dma(CH_ATAPI_RX);
		if (ap->hsm_task_state & HSM_ST_LAST) {
			/*
			 * On blackfin arch, uncacheable memory is not
			 * allocated with flag GFP_DMA. DMA buffer from
			 * common kenel code should be invalidated if
			 * data cache is enabled. Otherwise, this loop
			 * is an empty loop and optimized out.
			 */
			for_each_sg(qc->sg, sg, qc->n_elem, si) {
				invalidate_dcache_range(
					sg_dma_address(sg),
					sg_dma_address(sg)
					+ sg_dma_len(sg));
			}
		}
	}
}

/**
 *	bfin_devchk - PATA device presence detection
 *	@ap: ATA channel to examine
 *	@device: Device to examine (starting at zero)
 *
 *	Note: Original code is ata_devchk().
 */

static unsigned int bfin_devchk(struct ata_port *ap,
				unsigned int device)
{
	void __iomem *base = (void __iomem *)ap->ioaddr.ctl_addr;
	u8 nsect, lbal;

	bfin_dev_select(ap, device);

	write_atapi_register(base, ATA_REG_NSECT, 0x55);
	write_atapi_register(base, ATA_REG_LBAL, 0xaa);

	write_atapi_register(base, ATA_REG_NSECT, 0xaa);
	write_atapi_register(base, ATA_REG_LBAL, 0x55);

	write_atapi_register(base, ATA_REG_NSECT, 0x55);
	write_atapi_register(base, ATA_REG_LBAL, 0xaa);

	nsect = read_atapi_register(base, ATA_REG_NSECT);
	lbal = read_atapi_register(base, ATA_REG_LBAL);

	if ((nsect == 0x55) && (lbal == 0xaa))
		return 1;	/* we found a device */

	return 0;		/* nothing found */
}

/**
 *	bfin_bus_post_reset - PATA device post reset
 *
 *	Note: Original code is ata_bus_post_reset().
 */

static void bfin_bus_post_reset(struct ata_port *ap, unsigned int devmask)
{
	void __iomem *base = (void __iomem *)ap->ioaddr.ctl_addr;
	unsigned int dev0 = devmask & (1 << 0);
	unsigned int dev1 = devmask & (1 << 1);
	unsigned long timeout;

	/* if device 0 was found in ata_devchk, wait for its
	 * BSY bit to clear
	 */
	if (dev0)
		ata_sff_busy_sleep(ap, ATA_TMOUT_BOOT_QUICK, ATA_TMOUT_BOOT);

	/* if device 1 was found in ata_devchk, wait for
	 * register access, then wait for BSY to clear
	 */
	timeout = jiffies + ATA_TMOUT_BOOT;
	while (dev1) {
		u8 nsect, lbal;

		bfin_dev_select(ap, 1);
		nsect = read_atapi_register(base, ATA_REG_NSECT);
		lbal = read_atapi_register(base, ATA_REG_LBAL);
		if ((nsect == 1) && (lbal == 1))
			break;
		if (time_after(jiffies, timeout)) {
			dev1 = 0;
			break;
		}
		msleep(50);	/* give drive a breather */
	}
	if (dev1)
		ata_sff_busy_sleep(ap, ATA_TMOUT_BOOT_QUICK, ATA_TMOUT_BOOT);

	/* is all this really necessary? */
	bfin_dev_select(ap, 0);
	if (dev1)
		bfin_dev_select(ap, 1);
	if (dev0)
		bfin_dev_select(ap, 0);
}

/**
 *	bfin_bus_softreset - PATA device software reset
 *
 *	Note: Original code is ata_bus_softreset().
 */

static unsigned int bfin_bus_softreset(struct ata_port *ap,
				       unsigned int devmask)
{
	void __iomem *base = (void __iomem *)ap->ioaddr.ctl_addr;

	/* software reset.  causes dev0 to be selected */
	write_atapi_register(base, ATA_REG_CTRL, ap->ctl);
	udelay(20);
	write_atapi_register(base, ATA_REG_CTRL, ap->ctl | ATA_SRST);
	udelay(20);
	write_atapi_register(base, ATA_REG_CTRL, ap->ctl);

	/* spec mandates ">= 2ms" before checking status.
	 * We wait 150ms, because that was the magic delay used for
	 * ATAPI devices in Hale Landis's ATADRVR, for the period of time
	 * between when the ATA command register is written, and then
	 * status is checked.  Because waiting for "a while" before
	 * checking status is fine, post SRST, we perform this magic
	 * delay here as well.
	 *
	 * Old drivers/ide uses the 2mS rule and then waits for ready
	 */
	msleep(150);

	/* Before we perform post reset processing we want to see if
	 * the bus shows 0xFF because the odd clown forgets the D7
	 * pulldown resistor.
	 */
	if (bfin_check_status(ap) == 0xFF)
		return 0;

	bfin_bus_post_reset(ap, devmask);

	return 0;
}

/**
 *	bfin_softreset - reset host port via ATA SRST
 *	@ap: port to reset
 *	@classes: resulting classes of attached devices
 *
 *	Note: Original code is ata_sff_softreset().
 */

static int bfin_softreset(struct ata_link *link, unsigned int *classes,
			  unsigned long deadline)
{
	struct ata_port *ap = link->ap;
	unsigned int slave_possible = ap->flags & ATA_FLAG_SLAVE_POSS;
	unsigned int devmask = 0, err_mask;
	u8 err;

	/* determine if device 0/1 are present */
	if (bfin_devchk(ap, 0))
		devmask |= (1 << 0);
	if (slave_possible && bfin_devchk(ap, 1))
		devmask |= (1 << 1);

	/* select device 0 again */
	bfin_dev_select(ap, 0);

	/* issue bus reset */
	err_mask = bfin_bus_softreset(ap, devmask);
	if (err_mask) {
		ata_port_printk(ap, KERN_ERR, "SRST failed (err_mask=0x%x)\n",
				err_mask);
		return -EIO;
	}

	/* determine by signature whether we have ATA or ATAPI devices */
	classes[0] = ata_sff_dev_classify(&ap->link.device[0],
				devmask & (1 << 0), &err);
	if (slave_possible && err != 0x81)
		classes[1] = ata_sff_dev_classify(&ap->link.device[1],
					devmask & (1 << 1), &err);

	return 0;
}

/**
 *	bfin_bmdma_status - Read IDE DMA status
 *	@ap: Port associated with this ATA transaction.
 */

static unsigned char bfin_bmdma_status(struct ata_port *ap)
{
	unsigned char host_stat = 0;
	void __iomem *base = (void __iomem *)ap->ioaddr.ctl_addr;
	unsigned short int_status = ATAPI_GET_INT_STATUS(base);

	if (ATAPI_GET_STATUS(base) & (MULTI_XFER_ON|ULTRA_XFER_ON))
		host_stat |= ATA_DMA_ACTIVE;
	if (int_status & (MULTI_DONE_INT|UDMAIN_DONE_INT|UDMAOUT_DONE_INT|
		ATAPI_DEV_INT))
		host_stat |= ATA_DMA_INTR;
	if (int_status & (MULTI_TERM_INT|UDMAIN_TERM_INT|UDMAOUT_TERM_INT))
		host_stat |= ATA_DMA_ERR|ATA_DMA_INTR;

	dev_dbg(ap->dev, "ATAPI: host_stat=0x%x\n", host_stat);

	return host_stat;
}

/**
 *	bfin_data_xfer - Transfer data by PIO
 *	@adev: device for this I/O
 *	@buf: data buffer
 *	@buflen: buffer length
 *	@write_data: read/write
 *
 *	Note: Original code is ata_sff_data_xfer().
 */

static unsigned int bfin_data_xfer(struct ata_device *dev, unsigned char *buf,
				   unsigned int buflen, int rw)
{
	struct ata_port *ap = dev->link->ap;
	void __iomem *base = (void __iomem *)ap->ioaddr.ctl_addr;
	unsigned int words = buflen >> 1;
	unsigned short *buf16 = (u16 *)buf;

	/* Transfer multiple of 2 bytes */
	if (rw == READ)
		read_atapi_data(base, words, buf16);
	else
		write_atapi_data(base, words, buf16);

	/* Transfer trailing 1 byte, if any. */
	if (unlikely(buflen & 0x01)) {
		unsigned short align_buf[1] = { 0 };
		unsigned char *trailing_buf = buf + buflen - 1;

		if (rw == READ) {
			read_atapi_data(base, 1, align_buf);
			memcpy(trailing_buf, align_buf, 1);
		} else {
			memcpy(align_buf, trailing_buf, 1);
			write_atapi_data(base, 1, align_buf);
		}
		words++;
	}

	return words << 1;
}

/**
 *	bfin_irq_clear - Clear ATAPI interrupt.
 *	@ap: Port associated with this ATA transaction.
 *
 *	Note: Original code is ata_sff_irq_clear().
 */

static void bfin_irq_clear(struct ata_port *ap)
{
	void __iomem *base = (void __iomem *)ap->ioaddr.ctl_addr;

	dev_dbg(ap->dev, "in atapi irq clear\n");
	ATAPI_SET_INT_STATUS(base, ATAPI_GET_INT_STATUS(base)|ATAPI_DEV_INT
		| MULTI_DONE_INT | UDMAIN_DONE_INT | UDMAOUT_DONE_INT
		| MULTI_TERM_INT | UDMAIN_TERM_INT | UDMAOUT_TERM_INT);
}

/**
 *	bfin_irq_on - Enable interrupts on a port.
 *	@ap: Port on which interrupts are enabled.
 *
 *	Note: Original code is ata_sff_irq_on().
 */

static unsigned char bfin_irq_on(struct ata_port *ap)
{
	void __iomem *base = (void __iomem *)ap->ioaddr.ctl_addr;
	u8 tmp;

	dev_dbg(ap->dev, "in atapi irq on\n");
	ap->ctl &= ~ATA_NIEN;
	ap->last_ctl = ap->ctl;

	write_atapi_register(base, ATA_REG_CTRL, ap->ctl);
	tmp = ata_wait_idle(ap);

	bfin_irq_clear(ap);

	return tmp;
}

/**
 *	bfin_freeze - Freeze DMA controller port
 *	@ap: port to freeze
 *
 *	Note: Original code is ata_sff_freeze().
 */

static void bfin_freeze(struct ata_port *ap)
{
	void __iomem *base = (void __iomem *)ap->ioaddr.ctl_addr;

	dev_dbg(ap->dev, "in atapi dma freeze\n");
	ap->ctl |= ATA_NIEN;
	ap->last_ctl = ap->ctl;

	write_atapi_register(base, ATA_REG_CTRL, ap->ctl);

	/* Under certain circumstances, some controllers raise IRQ on
	 * ATA_NIEN manipulation.  Also, many controllers fail to mask
	 * previously pending IRQ on ATA_NIEN assertion.  Clear it.
	 */
	ap->ops->sff_check_status(ap);

	bfin_irq_clear(ap);
}

/**
 *	bfin_thaw - Thaw DMA controller port
 *	@ap: port to thaw
 *
 *	Note: Original code is ata_sff_thaw().
 */

void bfin_thaw(struct ata_port *ap)
{
	dev_dbg(ap->dev, "in atapi dma thaw\n");
	bfin_check_status(ap);
	bfin_irq_on(ap);
}

/**
 *	bfin_postreset - standard postreset callback
 *	@ap: the target ata_port
 *	@classes: classes of attached devices
 *
 *	Note: Original code is ata_sff_postreset().
 */

static void bfin_postreset(struct ata_link *link, unsigned int *classes)
{
	struct ata_port *ap = link->ap;
	void __iomem *base = (void __iomem *)ap->ioaddr.ctl_addr;

	/* re-enable interrupts */
	bfin_irq_on(ap);

	/* is double-select really necessary? */
	if (classes[0] != ATA_DEV_NONE)
		bfin_dev_select(ap, 1);
	if (classes[1] != ATA_DEV_NONE)
		bfin_dev_select(ap, 0);

	/* bail out if no device is present */
	if (classes[0] == ATA_DEV_NONE && classes[1] == ATA_DEV_NONE) {
		return;
	}

	/* set up device control */
	write_atapi_register(base, ATA_REG_CTRL, ap->ctl);
}

static void bfin_port_stop(struct ata_port *ap)
{
	dev_dbg(ap->dev, "in atapi port stop\n");
	if (ap->udma_mask != 0 || ap->mwdma_mask != 0) {
		free_dma(CH_ATAPI_RX);
		free_dma(CH_ATAPI_TX);
	}
}

static int bfin_port_start(struct ata_port *ap)
{
	dev_dbg(ap->dev, "in atapi port start\n");
	if (!(ap->udma_mask || ap->mwdma_mask))
		return 0;

	if (request_dma(CH_ATAPI_RX, "BFIN ATAPI RX DMA") >= 0) {
		if (request_dma(CH_ATAPI_TX,
			"BFIN ATAPI TX DMA") >= 0)
			return 0;

		free_dma(CH_ATAPI_RX);
	}

	ap->udma_mask = 0;
	ap->mwdma_mask = 0;
	dev_err(ap->dev, "Unable to request ATAPI DMA!"
		" Continue in PIO mode.\n");

	return 0;
}

static unsigned int bfin_ata_host_intr(struct ata_port *ap,
				   struct ata_queued_cmd *qc)
{
	struct ata_eh_info *ehi = &ap->link.eh_info;
	u8 status, host_stat = 0;

	VPRINTK("ata%u: protocol %d task_state %d\n",
		ap->print_id, qc->tf.protocol, ap->hsm_task_state);

	/* Check whether we are expecting interrupt in this state */
	switch (ap->hsm_task_state) {
	case HSM_ST_FIRST:
		/* Some pre-ATAPI-4 devices assert INTRQ
		 * at this state when ready to receive CDB.
		 */

		/* Check the ATA_DFLAG_CDB_INTR flag is enough here.
		 * The flag was turned on only for atapi devices.
		 * No need to check is_atapi_taskfile(&qc->tf) again.
		 */
		if (!(qc->dev->flags & ATA_DFLAG_CDB_INTR))
			goto idle_irq;
		break;
	case HSM_ST_LAST:
		if (qc->tf.protocol == ATA_PROT_DMA ||
		    qc->tf.protocol == ATAPI_PROT_DMA) {
			/* check status of DMA engine */
			host_stat = ap->ops->bmdma_status(ap);
			VPRINTK("ata%u: host_stat 0x%X\n",
				ap->print_id, host_stat);

			/* if it's not our irq... */
			if (!(host_stat & ATA_DMA_INTR))
				goto idle_irq;

			/* before we do anything else, clear DMA-Start bit */
			ap->ops->bmdma_stop(qc);

			if (unlikely(host_stat & ATA_DMA_ERR)) {
				/* error when transfering data to/from memory */
				qc->err_mask |= AC_ERR_HOST_BUS;
				ap->hsm_task_state = HSM_ST_ERR;
			}
		}
		break;
	case HSM_ST:
		break;
	default:
		goto idle_irq;
	}

	/* check altstatus */
	status = ap->ops->sff_check_altstatus(ap);
	if (status & ATA_BUSY)
		goto busy_ata;

	/* check main status, clearing INTRQ */
	status = ap->ops->sff_check_status(ap);
	if (unlikely(status & ATA_BUSY))
		goto busy_ata;

	/* ack bmdma irq events */
	ap->ops->sff_irq_clear(ap);

	ata_sff_hsm_move(ap, qc, status, 0);

	if (unlikely(qc->err_mask) && (qc->tf.protocol == ATA_PROT_DMA ||
				       qc->tf.protocol == ATAPI_PROT_DMA))
		ata_ehi_push_desc(ehi, "BMDMA stat 0x%x", host_stat);

busy_ata:
	return 1;	/* irq handled */

idle_irq:
	ap->stats.idle_irq++;

#ifdef ATA_IRQ_TRAP
	if ((ap->stats.idle_irq % 1000) == 0) {
		ap->ops->irq_ack(ap, 0); /* debug trap */
		ata_port_printk(ap, KERN_WARNING, "irq trap\n");
		return 1;
	}
#endif
	return 0;	/* irq not handled */
}

static irqreturn_t bfin_ata_interrupt(int irq, void *dev_instance)
{
	struct ata_host *host = dev_instance;
	unsigned int i;
	unsigned int handled = 0;
	unsigned long flags;

	/* TODO: make _irqsave conditional on x86 PCI IDE legacy mode */
	spin_lock_irqsave(&host->lock, flags);

	for (i = 0; i < host->n_ports; i++) {
		struct ata_port *ap;

		ap = host->ports[i];
		if (ap &&
		    !(ap->flags & ATA_FLAG_DISABLED)) {
			struct ata_queued_cmd *qc;

			qc = ata_qc_from_tag(ap, ap->link.active_tag);
			if (qc && (!(qc->tf.flags & ATA_TFLAG_POLLING)) &&
			    (qc->flags & ATA_QCFLAG_ACTIVE))
				handled |= bfin_ata_host_intr(ap, qc);
		}
	}

	spin_unlock_irqrestore(&host->lock, flags);

	return IRQ_RETVAL(handled);
}


static struct scsi_host_template bfin_sht = {
	ATA_BASE_SHT(DRV_NAME),
	.sg_tablesize		= SG_NONE,
	.dma_boundary		= ATA_DMA_BOUNDARY,
};

static struct ata_port_operations bfin_pata_ops = {
	.inherits		= &ata_sff_port_ops,

	.set_piomode		= bfin_set_piomode,
	.set_dmamode		= bfin_set_dmamode,

	.sff_tf_load		= bfin_tf_load,
	.sff_tf_read		= bfin_tf_read,
	.sff_exec_command	= bfin_exec_command,
	.sff_check_status	= bfin_check_status,
	.sff_check_altstatus	= bfin_check_altstatus,
	.sff_dev_select		= bfin_dev_select,

	.bmdma_setup		= bfin_bmdma_setup,
	.bmdma_start		= bfin_bmdma_start,
	.bmdma_stop		= bfin_bmdma_stop,
	.bmdma_status		= bfin_bmdma_status,
	.sff_data_xfer		= bfin_data_xfer,

	.qc_prep		= ata_noop_qc_prep,

	.freeze			= bfin_freeze,
	.thaw			= bfin_thaw,
	.softreset		= bfin_softreset,
	.postreset		= bfin_postreset,

	.sff_irq_clear		= bfin_irq_clear,
	.sff_irq_on		= bfin_irq_on,

	.port_start		= bfin_port_start,
	.port_stop		= bfin_port_stop,
};

static struct ata_port_info bfin_port_info[] = {
	{
		.flags		= ATA_FLAG_SLAVE_POSS
				| ATA_FLAG_MMIO
				| ATA_FLAG_NO_LEGACY,
		.pio_mask	= 0x1f,	/* pio0-4 */
		.mwdma_mask	= 0,
		.udma_mask	= 0,
		.port_ops	= &bfin_pata_ops,
	},
};

/**
 *	bfin_reset_controller - initialize BF54x ATAPI controller.
 */

static int bfin_reset_controller(struct ata_host *host)
{
	void __iomem *base = (void __iomem *)host->ports[0]->ioaddr.ctl_addr;
	int count;
	unsigned short status;

	/* Disable all ATAPI interrupts */
	ATAPI_SET_INT_MASK(base, 0);
	SSYNC();

	/* Assert the RESET signal 25us*/
	ATAPI_SET_CONTROL(base, ATAPI_GET_CONTROL(base) | DEV_RST);
	udelay(30);

	/* Negate the RESET signal for 2ms*/
	ATAPI_SET_CONTROL(base, ATAPI_GET_CONTROL(base) & ~DEV_RST);
	msleep(2);

	/* Wait on Busy flag to clear */
	count = 10000000;
	do {
		status = read_atapi_register(base, ATA_REG_STATUS);
	} while (--count && (status & ATA_BUSY));

	/* Enable only ATAPI Device interrupt */
	ATAPI_SET_INT_MASK(base, 1);
	SSYNC();

	return (!count);
}

/**
 *	atapi_io_port - define atapi peripheral port pins.
 */
static unsigned short atapi_io_port[] = {
	P_ATAPI_RESET,
	P_ATAPI_DIOR,
	P_ATAPI_DIOW,
	P_ATAPI_CS0,
	P_ATAPI_CS1,
	P_ATAPI_DMACK,
	P_ATAPI_DMARQ,
	P_ATAPI_INTRQ,
	P_ATAPI_IORDY,
	0
};

/**
 *	bfin_atapi_probe	-	attach a bfin atapi interface
 *	@pdev: platform device
 *
 *	Register a bfin atapi interface.
 *
 *
 *	Platform devices are expected to contain 2 resources per port:
 *
 *		- I/O Base (IORESOURCE_IO)
 *		- IRQ	   (IORESOURCE_IRQ)
 *
 */
static int __devinit bfin_atapi_probe(struct platform_device *pdev)
{
	int board_idx = 0;
	struct resource *res;
	struct ata_host *host;
	unsigned int fsclk = get_sclk();
	int udma_mode = 5;
	const struct ata_port_info *ppi[] =
		{ &bfin_port_info[board_idx], NULL };

	/*
	 * Simple resource validation ..
	 */
	if (unlikely(pdev->num_resources != 2)) {
		dev_err(&pdev->dev, "invalid number of resources\n");
		return -EINVAL;
	}

	/*
	 * Get the register base first
	 */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL)
		return -EINVAL;

	while (bfin_port_info[board_idx].udma_mask > 0 &&
			udma_fsclk[udma_mode] > fsclk) {
		udma_mode--;
		bfin_port_info[board_idx].udma_mask >>= 1;
	}

	/*
	 * Now that that's out of the way, wire up the port..
	 */
	host = ata_host_alloc_pinfo(&pdev->dev, ppi, 1);
	if (!host)
		return -ENOMEM;

	host->ports[0]->ioaddr.ctl_addr = (void *)res->start;

	if (peripheral_request_list(atapi_io_port, "atapi-io-port")) {
		dev_err(&pdev->dev, "Requesting Peripherals faild\n");
		return -EFAULT;
	}

	if (bfin_reset_controller(host)) {
		peripheral_free_list(atapi_io_port);
		dev_err(&pdev->dev, "Fail to reset ATAPI device\n");
		return -EFAULT;
	}

	if (ata_host_activate(host, platform_get_irq(pdev, 0),
		bfin_ata_interrupt, IRQF_SHARED, &bfin_sht) != 0) {
		peripheral_free_list(atapi_io_port);
		dev_err(&pdev->dev, "Fail to attach ATAPI device\n");
		return -ENODEV;
	}

	return 0;
}

/**
 *	bfin_atapi_remove	-	unplug a bfin atapi interface
 *	@pdev: platform device
 *
 *	A bfin atapi device has been unplugged. Perform the needed
 *	cleanup. Also called on module unload for any active devices.
 */
static int __devexit bfin_atapi_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct ata_host *host = dev_get_drvdata(dev);

	ata_host_detach(host);

	peripheral_free_list(atapi_io_port);

	return 0;
}

#ifdef CONFIG_PM
int bfin_atapi_suspend(struct platform_device *pdev, pm_message_t state)
{
	return 0;
}

int bfin_atapi_resume(struct platform_device *pdev)
{
	return 0;
}
#endif

static struct platform_driver bfin_atapi_driver = {
	.probe			= bfin_atapi_probe,
	.remove			= __devexit_p(bfin_atapi_remove),
	.driver = {
		.name		= DRV_NAME,
		.owner		= THIS_MODULE,
#ifdef CONFIG_PM
		.suspend	= bfin_atapi_suspend,
		.resume		= bfin_atapi_resume,
#endif
	},
};

#define ATAPI_MODE_SIZE		10
static char bfin_atapi_mode[ATAPI_MODE_SIZE];

static int __init bfin_atapi_init(void)
{
	pr_info("register bfin atapi driver\n");

	switch(bfin_atapi_mode[0]) {
	case 'p':
	case 'P':
		break;
	case 'm':
	case 'M':
		bfin_port_info[0].mwdma_mask = ATA_MWDMA2;
		break;
	default:
		bfin_port_info[0].udma_mask = ATA_UDMA5;
	};

	return platform_driver_register(&bfin_atapi_driver);
}

static void __exit bfin_atapi_exit(void)
{
	platform_driver_unregister(&bfin_atapi_driver);
}

module_init(bfin_atapi_init);
module_exit(bfin_atapi_exit);
/*
 * ATAPI mode:
 * pio/PIO
 * udma/UDMA (default)
 * mwdma/MWDMA
 */
module_param_string(bfin_atapi_mode, bfin_atapi_mode, ATAPI_MODE_SIZE, 0);

MODULE_AUTHOR("Sonic Zhang <sonic.zhang@analog.com>");
MODULE_DESCRIPTION("PATA driver for blackfin 54x ATAPI controller");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);
MODULE_ALIAS("platform:" DRV_NAME);
