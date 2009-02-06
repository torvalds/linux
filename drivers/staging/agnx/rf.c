/**
 * Airgo MIMO wireless driver
 *
 * Copyright (c) 2007 Li YanBo <dreamfly281@gmail.com>

 * Thanks for Jeff Williams <angelbane@gmail.com> do reverse engineer
 * works and published the SPECS at http://airgo.wdwconsulting.net/mymoin

 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 */

#include <linux/pci.h>
#include <linux/delay.h>
#include "agnx.h"
#include "debug.h"
#include "phy.h"
#include "table.h"

/* FIXME! */
static inline void spi_write(void __iomem *region, u32 chip_ids, u32 sw,
		      u16 size, u32 control)
{
	u32 reg;
	u32 lsw = sw & 0xffff;		/* lower 16 bits of sw*/
	u32 msw = sw >> 16;		/* high 16 bits of sw */

	/* FIXME Write Most Significant Word of the 32bit data to MSW */
	/* FIXME And Least Significant Word to LSW */
	iowrite32((lsw), region + AGNX_SPI_WLSW);
	iowrite32((msw), region + AGNX_SPI_WMSW);
	reg = chip_ids | size | control;
	/* Write chip id(s), write size and busy control to Control Register */
	iowrite32((reg), region + AGNX_SPI_CTL);
	/* Wait for Busy control to clear */
	spi_delay();
}

/*
 * Write to SPI Synth register
 */
static inline void spi_sy_write(void __iomem *region, u32 chip_ids, u32 sw)
{
	/* FIXME the size 0x15 is a magic value*/
	spi_write(region, chip_ids, sw, 0x15, SPI_BUSY_CTL);
}

/*
 * Write to SPI RF register
 */
static inline void spi_rf_write(void __iomem *region, u32 chip_ids, u32 sw)
{
	/* FIXME the size 0xd is a magic value*/
	spi_write(region, chip_ids, sw, 0xd, SPI_BUSY_CTL);
} /* spi_rf_write */

/*
 * Write to SPI with Read Control bit set
 */
inline void spi_rc_write(void __iomem *region, u32 chip_ids, u32 sw)
{
	/* FIXME the size 0xe5 is a magic value */
	spi_write(region, chip_ids, sw, 0xe5, SPI_BUSY_CTL|SPI_READ_CTL);
}

/* Get the active chains's count */
static int get_active_chains(struct agnx_priv *priv)
{
	void __iomem *ctl = priv->ctl;
	int num = 0;
	u32 reg;
	AGNX_TRACE;

	spi_rc_write(ctl, RF_CHIP0, 0x21);
	reg = agnx_read32(ctl, AGNX_SPI_RLSW);
	if (reg == 1)
		num++;

	spi_rc_write(ctl, RF_CHIP1, 0x21);
	reg = agnx_read32(ctl, AGNX_SPI_RLSW);
	if (reg == 1)
		num++;

	spi_rc_write(ctl, RF_CHIP2, 0x21);
	reg = agnx_read32(ctl, AGNX_SPI_RLSW);
	if (reg == 1)
		num++;

	spi_rc_write(ctl, RF_CHIP0, 0x26);
	reg = agnx_read32(ctl, AGNX_SPI_RLSW);
	if (0x33 != reg)
		printk(KERN_WARNING PFX "Unmatched rf chips result\n");

	return num;
} /* get_active_chains */

void rf_chips_init(struct agnx_priv *priv)
{
	void __iomem *ctl = priv->ctl;
	u32 reg;
	int num;
	AGNX_TRACE;

	if (priv->revid == 1) {
		reg = agnx_read32(ctl, AGNX_SYSITF_GPIOUT);
		reg |= 0x8;
		agnx_write32(ctl, AGNX_SYSITF_GPIOUT, reg);
	}

	/* Set SPI clock speed to 200NS */
        reg = agnx_read32(ctl, AGNX_SPI_CFG);
        reg &= ~0xF;
        reg |= 0x3;
        agnx_write32(ctl, AGNX_SPI_CFG, reg);

        /* Set SPI clock speed to 50NS */
	reg = agnx_read32(ctl, AGNX_SPI_CFG);
	reg &= ~0xF;
	reg |= 0x1;
	agnx_write32(ctl, AGNX_SPI_CFG, reg);

	spi_rf_write(ctl, RF_CHIP0|RF_CHIP1|RF_CHIP2, 0x1101);

	num = get_active_chains(priv);
	printk(KERN_INFO PFX "Active chains are %d\n", num);

	reg = agnx_read32(ctl, AGNX_SPI_CFG);
	reg &= ~0xF;
	agnx_write32(ctl, AGNX_SPI_CFG, reg);

	spi_rf_write(ctl, RF_CHIP0|RF_CHIP1|RF_CHIP2, 0x1908);
} /* rf_chips_init */


static u32 channel_tbl[15][9] = {
	{0,  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
	{1,  0x00, 0x00, 0x624, 0x00, 0x1a4, 0x28, 0x00, 0x1e},
	{2,  0x00, 0x00, 0x615, 0x00, 0x1ae, 0x28, 0x00, 0x1e},
	{3,  0x00, 0x00, 0x61a, 0x00, 0x1ae, 0x28, 0x00, 0x1e},
	{4,  0x00, 0x00, 0x61f, 0x00, 0x1ae, 0x28, 0x00, 0x1e},
	{5,  0x00, 0x00, 0x624, 0x00, 0x1ae, 0x28, 0x00, 0x1e},
	{6,  0x00, 0x00, 0x61f, 0x00, 0x1b3, 0x28, 0x00, 0x1e},
	{7,  0x00, 0x00, 0x624, 0x00, 0x1b3, 0x28, 0x00, 0x1e},
	{8,  0x00, 0x00, 0x629, 0x00, 0x1b3, 0x28, 0x00, 0x1e},
	{9,  0x00, 0x00, 0x624, 0x00, 0x1b8, 0x28, 0x00, 0x1e},
	{10, 0x00, 0x00, 0x629, 0x00, 0x1b8, 0x28, 0x00, 0x1e},
	{11, 0x00, 0x00, 0x62e, 0x00, 0x1b8, 0x28, 0x00, 0x1e},
	{12, 0x00, 0x00, 0x633, 0x00, 0x1b8, 0x28, 0x00, 0x1e},
	{13, 0x00, 0x00, 0x628, 0x00, 0x1b8, 0x28, 0x00, 0x1e},
	{14, 0x00, 0x00, 0x644, 0x00, 0x1b8, 0x28, 0x00, 0x1e},
};


static inline void
channel_tbl_write(struct agnx_priv *priv, unsigned int channel, unsigned int reg_num)
{
	void __iomem *ctl = priv->ctl;
	u32 reg;

	reg = channel_tbl[channel][reg_num];
	reg <<= 4;
	reg |= reg_num;
	spi_sy_write(ctl, SYNTH_CHIP, reg);
}

static void synth_freq_set(struct agnx_priv *priv, unsigned int channel)
{
	void __iomem *ctl = priv->ctl;
	u32 reg;
	AGNX_TRACE;

	spi_rf_write(ctl, RF_CHIP0|RF_CHIP1, 0x1201);

	/* Set the Clock bits to 50NS */
	reg = agnx_read32(ctl, AGNX_SPI_CFG);
	reg &= ~0xF;
	reg |= 0x1;
	agnx_write32(ctl, AGNX_SPI_CFG, reg);

	/* Write 0x00c0 to LSW and 0x3 to MSW of Synth Chip */
	spi_sy_write(ctl, SYNTH_CHIP, 0x300c0);

	spi_sy_write(ctl, SYNTH_CHIP, 0x32);

	/* # Write to Register 1 on the Synth Chip */
	channel_tbl_write(priv, channel, 1);
	/* # Write to Register 3 on the Synth Chip */
	channel_tbl_write(priv, channel, 3);
	/* # Write to Register 6 on the Synth Chip */
	channel_tbl_write(priv, channel, 6);
	/* # Write to Register 5 on the Synth Chip */
	channel_tbl_write(priv, channel, 5);
	/* # Write to register 8 on the Synth Chip */
	channel_tbl_write(priv, channel, 8);

	/* FIXME Clear the clock bits */
	reg = agnx_read32(ctl, AGNX_SPI_CFG);
	reg &= ~0xf;
	agnx_write32(ctl, AGNX_SPI_CFG, reg);
} /* synth_chip_init */


static void antenna_init(struct agnx_priv *priv, int num_antenna)
{
	void __iomem *ctl = priv->ctl;

	switch (num_antenna) {
	case 1:
		agnx_write32(ctl, AGNX_GCR_NLISTANT, 1);
		agnx_write32(ctl, AGNX_GCR_NMEASANT, 1);
		agnx_write32(ctl, AGNX_GCR_NACTIANT, 1);
		agnx_write32(ctl, AGNX_GCR_NCAPTANT, 1);

		agnx_write32(ctl, AGNX_GCR_ANTCFG, 7);
		agnx_write32(ctl, AGNX_GCR_BOACT, 34);
		agnx_write32(ctl, AGNX_GCR_BOINACT, 34);
		agnx_write32(ctl, AGNX_GCR_BODYNA, 30);

		agnx_write32(ctl, AGNX_GCR_THD0A, 125);
		agnx_write32(ctl, AGNX_GCR_THD0AL, 100);
		agnx_write32(ctl, AGNX_GCR_THD0B, 90);

		agnx_write32(ctl, AGNX_GCR_THD0BTFEST, 80);
		agnx_write32(ctl, AGNX_GCR_SIGHTH, 100);
		agnx_write32(ctl, AGNX_GCR_SIGLTH, 16);
		break;
	case 2:
		agnx_write32(ctl, AGNX_GCR_NLISTANT, 2);
		agnx_write32(ctl, AGNX_GCR_NMEASANT, 2);
		agnx_write32(ctl, AGNX_GCR_NACTIANT, 2);
		agnx_write32(ctl, AGNX_GCR_NCAPTANT, 2);
		agnx_write32(ctl, AGNX_GCR_ANTCFG, 15);
		agnx_write32(ctl, AGNX_GCR_BOACT, 36);
		agnx_write32(ctl, AGNX_GCR_BOINACT, 36);
		agnx_write32(ctl, AGNX_GCR_BODYNA, 32);
		agnx_write32(ctl, AGNX_GCR_THD0A, 120);
		agnx_write32(ctl, AGNX_GCR_THD0AL, 100);
		agnx_write32(ctl, AGNX_GCR_THD0B, 80);
		agnx_write32(ctl, AGNX_GCR_THD0BTFEST, 70);
		agnx_write32(ctl, AGNX_GCR_SIGHTH, 100);
		agnx_write32(ctl, AGNX_GCR_SIGLTH, 32);
		break;
	case 3:
		agnx_write32(ctl, AGNX_GCR_NLISTANT, 3);
		agnx_write32(ctl, AGNX_GCR_NMEASANT, 3);
		agnx_write32(ctl, AGNX_GCR_NACTIANT, 3);
		agnx_write32(ctl, AGNX_GCR_NCAPTANT, 3);
		agnx_write32(ctl, AGNX_GCR_ANTCFG, 31);
		agnx_write32(ctl, AGNX_GCR_BOACT, 36);
		agnx_write32(ctl, AGNX_GCR_BOINACT, 36);
		agnx_write32(ctl, AGNX_GCR_BODYNA, 32);
		agnx_write32(ctl, AGNX_GCR_THD0A, 100);
		agnx_write32(ctl, AGNX_GCR_THD0AL, 100);
		agnx_write32(ctl, AGNX_GCR_THD0B, 70);
		agnx_write32(ctl, AGNX_GCR_THD0BTFEST, 70);
		agnx_write32(ctl, AGNX_GCR_SIGHTH, 100);
		agnx_write32(ctl, AGNX_GCR_SIGLTH, 48);
//		agnx_write32(ctl, AGNX_GCR_SIGLTH, 16);
		break;
	default:
		printk(KERN_WARNING PFX "Unknow antenna number\n");
	}
} /* antenna_init */

static void chain_update(struct agnx_priv *priv, u32 chain)
{
	void __iomem *ctl = priv->ctl;
	u32 reg;
	AGNX_TRACE;

	spi_rc_write(ctl, RF_CHIP0, 0x20);
	reg = agnx_read32(ctl, AGNX_SPI_RLSW);

	if (reg == 0x4)
		spi_rf_write(ctl, RF_CHIP0|RF_CHIP1, reg|0x1000);
	else if (reg != 0x0)
     		spi_rf_write(ctl, RF_CHIP0|RF_CHIP1|RF_CHIP2, reg|0x1000);
        else {
		if (chain == 3 || chain == 6) {
			spi_rf_write(ctl, RF_CHIP0|RF_CHIP1|RF_CHIP2, reg|0x1000);
			agnx_write32(ctl, AGNX_GCR_RXOVERIDE, 0x0);
		} else if (chain == 2 || chain == 4) {
			spi_rf_write(ctl, RF_CHIP0|RF_CHIP1, reg|0x1000);
			spi_rf_write(ctl, RF_CHIP2, 0x1005);
			agnx_write32(ctl, AGNX_GCR_RXOVERIDE, 0x824);
		} else if (chain == 1) {
			spi_rf_write(ctl, RF_CHIP0, reg|0x1000);
			spi_rf_write(ctl, RF_CHIP1|RF_CHIP2, 0x1004);
			agnx_write32(ctl, AGNX_GCR_RXOVERIDE, 0xc36);
		}
	}

	spi_rc_write(ctl, RF_CHIP0, 0x22);
	reg = agnx_read32(ctl, AGNX_SPI_RLSW);

	switch (reg) {
	case 0:
		spi_rf_write(ctl, RF_CHIP0|RF_CHIP1|RF_CHIP2, 0x1005);
		break;
	case 1:
		spi_rf_write(ctl, RF_CHIP0|RF_CHIP1, 0x1201);
		break;
	case 2:
		if (chain == 6 || chain == 4) {
			spi_rf_write(ctl, RF_CHIP0|RF_CHIP1, 0x1202);
			spi_rf_write(ctl, RF_CHIP2, 0x1005);
		} else if (chain < 3) {
			spi_rf_write(ctl, RF_CHIP0, 0x1202);
			spi_rf_write(ctl, RF_CHIP1|RF_CHIP2, 0x1005);
		}
		break;
	default:
		if (chain == 3) {
			spi_rf_write(ctl, RF_CHIP0|RF_CHIP1, 0x1203);
			spi_rf_write(ctl, RF_CHIP2, 0x1201);
		} else if (chain == 2) {
			spi_rf_write(ctl, RF_CHIP0, 0x1203);
			spi_rf_write(ctl, RF_CHIP2, 0x1200);
			spi_rf_write(ctl, RF_CHIP1, 0x1201);
		} else if (chain == 1) {
			spi_rf_write(ctl, RF_CHIP0, 0x1203);
			spi_rf_write(ctl, RF_CHIP1|RF_CHIP2, 0x1200);
		} else if (chain == 4) {
			spi_rf_write(ctl, RF_CHIP0|RF_CHIP1, 0x1203);
			spi_rf_write(ctl, RF_CHIP2, 0x1201);
		} else {
			spi_rf_write(ctl, RF_CHIP0, 0x1203);
			spi_rf_write(ctl, RF_CHIP1|RF_CHIP2, 0x1201);
		}
	}
} /* chain_update */

static void antenna_config(struct agnx_priv *priv)
{
	void __iomem *ctl = priv->ctl;
	u32 reg;
	AGNX_TRACE;

	/* Write 0x0 to the TX Management Control Register Enable bit */
	reg = agnx_read32(ctl, AGNX_TXM_CTL);
	reg &= ~0x1;
	agnx_write32(ctl, AGNX_TXM_CTL, reg);

	/* FIXME */
	/* Set initial value based on number of Antennae */
	antenna_init(priv, 3);

	/* FIXME Update Power Templates for current valid Stations */
	/* sta_power_init(priv, 0);*/

	/* FIXME the number of chains should get from eeprom*/
	chain_update(priv, AGNX_CHAINS_MAX);
} /* antenna_config */

void calibrate_oscillator(struct agnx_priv *priv)
{
	void __iomem *ctl = priv->ctl;
	u32 reg;
	AGNX_TRACE;

	spi_rc_write(ctl, RF_CHIP0|RF_CHIP1, 0x1201);
	reg = agnx_read32(ctl, AGNX_GCR_GAINSET1);
	reg |= 0x10;
	agnx_write32(ctl, AGNX_GCR_GAINSET1, reg);

	agnx_write32(ctl, AGNX_GCR_GAINSETWRITE, 1);
	agnx_write32(ctl, AGNX_GCR_RSTGCTL, 1);

	agnx_write32(ctl, AGNX_ACI_LEN, 0x3ff);

	agnx_write32(ctl, AGNX_ACI_TIMER1, 0x27);
	agnx_write32(ctl, AGNX_ACI_TIMER2, 0x27);
	/* (Residual DC Calibration) to Calibration Mode */
	agnx_write32(ctl, AGNX_ACI_MODE, 0x2);

	spi_rc_write(ctl, RF_CHIP0|RF_CHIP1, 0x1004);
	agnx_write32(ctl, AGNX_ACI_LEN, 0x3ff);
	/* (TX LO Calibration) to Calibration Mode */
	agnx_write32(ctl, AGNX_ACI_MODE, 0x4);

	do {
		u32  reg1, reg2, reg3;
		/* Enable Power Saving Control */
		enable_power_saving(priv);
		/* Save the following registers to restore */
		reg1 = ioread32(ctl + 0x11000);
		reg2 = ioread32(ctl + 0xec50);
		reg3 = ioread32(ctl + 0xec54);
		wmb();

		agnx_write32(ctl, 0x11000, 0xcfdf);
		agnx_write32(ctl, 0xec50, 0x70);
		/* Restore the registers */
		agnx_write32(ctl, 0x11000, reg1);
		agnx_write32(ctl, 0xec50, reg2);
		agnx_write32(ctl, 0xec54, reg3);
		/* Disable Power Saving Control */
		disable_power_saving(priv);
	} while (0);

	agnx_write32(ctl, AGNX_GCR_RSTGCTL, 0);
} /* calibrate_oscillator */


static void radio_channel_set(struct agnx_priv *priv, unsigned int channel)
{
	void __iomem *ctl = priv->ctl;
	unsigned int freq = priv->band.channels[channel - 1].center_freq;
	u32 reg;
	AGNX_TRACE;

	spi_rf_write(ctl, RF_CHIP0|RF_CHIP1, 0x1201);
	/* Set SPI Clock to 50 Ns */
	reg = agnx_read32(ctl, AGNX_SPI_CFG);
	reg &= ~0xF;
	reg |= 0x1;
	agnx_write32(ctl, AGNX_SPI_CFG, reg);

	/* Clear the Disable Tx interrupt bit in Interrupt Mask */
/* 	reg = agnx_read32(ctl, AGNX_INT_MASK); */
/* 	reg &= ~IRQ_TX_DISABLE; */
/* 	agnx_write32(ctl, AGNX_INT_MASK, reg); */

	/* Band Selection */
	reg = agnx_read32(ctl, AGNX_SYSITF_GPIOUT);
	reg |= 0x8;
	agnx_write32(ctl, AGNX_SYSITF_GPIOUT, reg);

	/* FIXME Set the SiLabs Chip Frequency */
	synth_freq_set(priv, channel);

	reg = agnx_read32(ctl, AGNX_PM_SOFTRST);
	reg |= 0x80100030;
	agnx_write32(ctl, AGNX_PM_SOFTRST, reg);
	reg = agnx_read32(ctl, AGNX_PM_PLLCTL);
	reg |= 0x20009;
	agnx_write32(ctl, AGNX_PM_PLLCTL, reg);

	agnx_write32(ctl, AGNX_SYSITF_GPIOUT, 0x5);

	spi_rf_write(ctl, RF_CHIP0|RF_CHIP1|RF_CHIP2, 0x1100);

	/* Load the MonitorGain Table */
	monitor_gain_table_init(priv);

	/* Load the TX Fir table */
	tx_fir_table_init(priv);

	reg = agnx_read32(ctl, AGNX_PM_PMCTL);
	reg |= 0x8;
	agnx_write32(ctl, AGNX_PM_PMCTL, reg);

	spi_rc_write(ctl, RF_CHIP0|RF_CHIP1, 0x22);
	udelay(80);
	reg = agnx_read32(ctl, AGNX_SPI_RLSW);


	agnx_write32(ctl, AGNX_GCR_RXOVERIDE, 0xff);
	agnx_write32(ctl, AGNX_GCR_DISCOVMOD, 0x3);

	reg = agnx_read32(ctl, 0xec50);
	reg |= 0x4f;
	agnx_write32(ctl, 0xec50, reg);

	spi_rf_write(ctl, RF_CHIP0|RF_CHIP1, 0x1201);
	agnx_write32(ctl, 0x11008, 0x1);
	agnx_write32(ctl, 0x1100c, 0x0);
	agnx_write32(ctl, 0x11008, 0x0);
	agnx_write32(ctl, 0xec50, 0xc);

	agnx_write32(ctl, AGNX_GCR_DISCOVMOD, 0x3);
	agnx_write32(ctl, AGNX_GCR_RXOVERIDE, 0x0);
	agnx_write32(ctl, 0x11010, 0x6e);
	agnx_write32(ctl, 0x11014, 0x6c);

	spi_rf_write(ctl, RF_CHIP0|RF_CHIP1, 0x1201);

	/* Calibrate the Antenna */
	/* antenna_calibrate(priv); */
	/* Calibrate the TxLocalOscillator */
	calibrate_oscillator(priv);

	reg = agnx_read32(ctl, AGNX_PM_PMCTL);
	reg &= ~0x8;
	agnx_write32(ctl, AGNX_PM_PMCTL, reg);
	agnx_write32(ctl, AGNX_GCR_GAININIT, 0xa);
	agnx_write32(ctl, AGNX_GCR_THCD, 0x0);

	agnx_write32(ctl, 0x11018, 0xb);
	agnx_write32(ctl, AGNX_GCR_DISCOVMOD, 0x0);

	/* Write Frequency to Gain Control Channel */
	agnx_write32(ctl, AGNX_GCR_RXCHANEL, freq);
	/* Write 0x140000/Freq to 0x9c08 */
	reg = 0x140000/freq;
	agnx_write32(ctl, 0x9c08, reg);

	reg = agnx_read32(ctl, AGNX_PM_SOFTRST);
	reg &= ~0x80100030;
	agnx_write32(ctl, AGNX_PM_SOFTRST, reg);

	reg = agnx_read32(ctl, AGNX_PM_PLLCTL);
	reg &= ~0x20009;
	reg |= 0x1;
	agnx_write32(ctl, AGNX_PM_PLLCTL, reg);

	agnx_write32(ctl, AGNX_ACI_MODE, 0x0);

/* FIXME According to Number of Chains: */

/* 			   1. 1: */
/*          1. Write 0x1203 to RF Chip 0 */
/*          2. Write 0x1200 to RF Chips 1 +2  */
/* 			   2. 2: */
/*          1. Write 0x1203 to RF Chip 0 */
/*          2. Write 0x1200 to RF Chip 2 */
/*          3. Write 0x1201 to RF Chip 1  */
/* 			   3. 3: */
/*          1. Write 0x1203 to RF Chip 0 */
/*          2. Write 0x1201 to RF Chip 1 + 2  */
/* 			   4. 4: */
/*          1. Write 0x1203 to RF Chip 0 + 1 */
/*          2. Write 0x1200 to RF Chip 2  */

/* 			   5. 6: */
	spi_rf_write(ctl, RF_CHIP0|RF_CHIP1, 0x1203);
	spi_rf_write(ctl, RF_CHIP2, 0x1201);

	spi_rf_write(ctl, RF_CHIP0|RF_CHIP1|RF_CHIP2, 0x1000);
	agnx_write32(ctl, AGNX_GCR_RXOVERIDE, 0x0);

	/* FIXME Set the Disable Tx interrupt bit in Interrupt Mask
	   (Or 0x20000 to Interrupt Mask) */
/* 	reg = agnx_read32(ctl, AGNX_INT_MASK); */
/* 	reg |= IRQ_TX_DISABLE; */
/* 	agnx_write32(ctl, AGNX_INT_MASK, reg); */

	agnx_write32(ctl, AGNX_GCR_RSTGCTL, 0x1);
	agnx_write32(ctl, AGNX_GCR_RSTGCTL, 0x0);

	/* Configure the Antenna */
	antenna_config(priv);

	/* Write 0x0 to Discovery Mode Enable detect G, B, A packet? */
	agnx_write32(ctl, AGNX_GCR_DISCOVMOD, 0);

	reg = agnx_read32(ctl, AGNX_RXM_REQRATE);
	reg |= 0x80000000;
	agnx_write32(ctl, AGNX_RXM_REQRATE, reg);
	agnx_write32(ctl, AGNX_GCR_RSTGCTL, 0x1);
	agnx_write32(ctl, AGNX_GCR_RSTGCTL, 0x0);

	/* enable radio on and the power LED */
	reg = agnx_read32(ctl, AGNX_SYSITF_GPIOUT);
	reg &= ~0x1;
	reg |= 0x2;
	agnx_write32(ctl, AGNX_SYSITF_GPIOUT, reg);

	reg = agnx_read32(ctl, AGNX_TXM_CTL);
	reg |= 0x1;
	agnx_write32(ctl, AGNX_TXM_CTL, reg);
} /* radio_channel_set */

static void base_band_filter_calibrate(struct agnx_priv *priv)
{
	void __iomem *ctl = priv->ctl;

	spi_rf_write(ctl, RF_CHIP0|RF_CHIP1|RF_CHIP2, 0x1700);
	spi_rf_write(ctl, RF_CHIP0|RF_CHIP1|RF_CHIP2, 0x1001);
	agnx_write32(ctl, AGNX_GCR_FORCECTLCLK, 0x0);
	spi_rc_write(ctl, RF_CHIP0, 0x27);
	spi_rc_write(ctl, RF_CHIP1, 0x27);
	spi_rc_write(ctl, RF_CHIP2, 0x27);
	agnx_write32(ctl, AGNX_GCR_FORCECTLCLK, 0x1);
}

static void print_offset(struct agnx_priv *priv, u32 chain)
{
	void __iomem *ctl = priv->ctl;
	u32 offset;

	iowrite32((chain), ctl + AGNX_ACI_SELCHAIN);
	udelay(10);
	offset = (ioread32(ctl + AGNX_ACI_OFFSET));
	printk(PFX "Chain is 0x%x, Offset is 0x%x\n", chain, offset);
}

void print_offsets(struct agnx_priv *priv)
{
	print_offset(priv, 0);
	print_offset(priv, 4);
	print_offset(priv, 1);
	print_offset(priv, 5);
	print_offset(priv, 2);
	print_offset(priv, 6);
}


struct chains {
	u32 cali;		/* calibrate  value*/

#define  NEED_CALIBRATE		0
#define  SUCCESS_CALIBRATE	1
	int status;
};

static void chain_calibrate(struct agnx_priv *priv, struct chains *chains,
			    unsigned int num)
{
	void __iomem *ctl = priv->ctl;
	u32 calibra = chains[num].cali;

	if (num < 3)
		calibra |= 0x1400;
	else
		calibra |= 0x1500;

	switch (num) {
	case 0:
	case 4:
		spi_rf_write(ctl, RF_CHIP0, calibra);
		break;
	case 1:
	case 5:
		spi_rf_write(ctl, RF_CHIP1, calibra);
		break;
	case 2:
	case 6:
		spi_rf_write(ctl, RF_CHIP2, calibra);
		break;
	default:
		BUG();
	}
} /* chain_calibrate */


static void inline get_calibrete_value(struct agnx_priv *priv, struct chains *chains,
				       unsigned int num)
{
	void __iomem *ctl = priv->ctl;
	u32 offset;

	iowrite32((num), ctl + AGNX_ACI_SELCHAIN);
	/* FIXME */
	udelay(10);
	offset = (ioread32(ctl + AGNX_ACI_OFFSET));

	if (offset < 0xf) {
		chains[num].status = SUCCESS_CALIBRATE;
		return;
	}

	if (num == 0 || num == 1 || num == 2) {
		if ( 0 == chains[num].cali)
			chains[num].cali = 0xff;
		else
			chains[num].cali--;
	} else
		chains[num].cali++;

	chains[num].status = NEED_CALIBRATE;
}

static inline void calibra_delay(struct agnx_priv *priv)
{
	void __iomem *ctl = priv->ctl;
	u32 reg;
	unsigned int i = 100;

	wmb();
	while (i--) {
		reg = (ioread32(ctl + AGNX_ACI_STATUS));
		if (reg == 0x4000)
			break;
		udelay(10);
	}
	if (!i)
		printk(PFX "calibration failed\n");
}

void do_calibration(struct agnx_priv *priv)
{
	void __iomem *ctl = priv->ctl;
	struct chains chains[7];
	unsigned int i, j;
	AGNX_TRACE;

	for (i = 0; i < 7; i++) {
		if (i == 3)
			continue;

		chains[i].cali = 0x7f;
		chains[i].status = NEED_CALIBRATE;
	}

	/* FIXME 0x300 is a magic number */
	for (j = 0; j < 0x300; j++) {
		if (chains[0].status == SUCCESS_CALIBRATE &&
		    chains[1].status == SUCCESS_CALIBRATE &&
		    chains[2].status == SUCCESS_CALIBRATE &&
		    chains[4].status == SUCCESS_CALIBRATE &&
		    chains[5].status == SUCCESS_CALIBRATE &&
		    chains[6].status == SUCCESS_CALIBRATE)
			break;

		/* Attention, there is no chain 3 */
		for (i = 0; i < 7; i++) {
			if (i == 3)
				continue;
			if (chains[i].status == NEED_CALIBRATE)
				chain_calibrate(priv, chains, i);
		}
		/* Write 0x1 to Calibration Measure */
		iowrite32((0x1), ctl + AGNX_ACI_MEASURE);
		calibra_delay(priv);

		for (i = 0; i < 7; i++) {
			if (i == 3)
				continue;

			get_calibrete_value(priv, chains, i);
		}
	}
	printk(PFX "Clibrate times is %d\n", j);
	print_offsets(priv);
} /* do_calibration */

void antenna_calibrate(struct agnx_priv *priv)
{
	void __iomem *ctl = priv->ctl;
	u32 reg;
	AGNX_TRACE;

	agnx_write32(ctl, AGNX_GCR_NLISTANT, 0x3);
	agnx_write32(ctl, AGNX_GCR_NMEASANT, 0x3);
	agnx_write32(ctl, AGNX_GCR_NACTIANT, 0x3);
	agnx_write32(ctl, AGNX_GCR_NCAPTANT, 0x3);

	agnx_write32(ctl, AGNX_GCR_ANTCFG, 0x1f);
	agnx_write32(ctl, AGNX_GCR_BOACT, 0x24);
	agnx_write32(ctl, AGNX_GCR_BOINACT, 0x24);
	agnx_write32(ctl, AGNX_GCR_BODYNA, 0x20);
	agnx_write32(ctl, AGNX_GCR_THD0A, 0x64);
	agnx_write32(ctl, AGNX_GCR_THD0AL, 0x64);
	agnx_write32(ctl, AGNX_GCR_THD0B, 0x46);
	agnx_write32(ctl, AGNX_GCR_THD0BTFEST, 0x3c);
	agnx_write32(ctl, AGNX_GCR_SIGHTH, 0x64);
	agnx_write32(ctl, AGNX_GCR_SIGLTH, 0x30);

	spi_rc_write(ctl, RF_CHIP0, 0x20);
	/* Fixme */
	udelay(80);
	/*    1. Should read 0x0  */
	reg = agnx_read32(ctl, AGNX_SPI_RLSW);
	if (0x0 != reg)
		printk(KERN_WARNING PFX "Unmatched rf chips result\n");
	spi_rf_write(ctl, RF_CHIP0|RF_CHIP1|RF_CHIP2, 0x1000);

	agnx_write32(ctl, AGNX_GCR_RXOVERIDE, 0x0);

	spi_rc_write(ctl, RF_CHIP0, 0x22);
	udelay(80);
	reg = agnx_read32(ctl, AGNX_SPI_RLSW);
	if (0x0 != reg)
		printk(KERN_WARNING PFX "Unmatched rf chips result\n");
	spi_rf_write(ctl, RF_CHIP0|RF_CHIP1|RF_CHIP2, 0x1005);

	agnx_write32(ctl, AGNX_GCR_RSTGCTL, 0x1);
	agnx_write32(ctl, AGNX_GCR_RSTGCTL, 0x0);

	reg = agnx_read32(ctl, AGNX_PM_SOFTRST);
	reg |= 0x1c000032;
	agnx_write32(ctl, AGNX_PM_SOFTRST, reg);
	reg = agnx_read32(ctl, AGNX_PM_PLLCTL);
	reg |= 0x0003f07;
	agnx_write32(ctl, AGNX_PM_PLLCTL, reg);

	reg = agnx_read32(ctl, 0xec50);
	reg |= 0x40;
	agnx_write32(ctl, 0xec50, reg);

	agnx_write32(ctl, AGNX_GCR_RXOVERIDE, 0xff8);
	agnx_write32(ctl, AGNX_GCR_DISCOVMOD, 0x3);

	agnx_write32(ctl, AGNX_GCR_CHAINNUM, 0x6);
	agnx_write32(ctl, 0x19874, 0x0);
	spi_rf_write(ctl, RF_CHIP0|RF_CHIP1|RF_CHIP2, 0x1700);

	/* Calibrate the BaseBandFilter */
	base_band_filter_calibrate(priv);

	spi_rf_write(ctl, RF_CHIP0|RF_CHIP1|RF_CHIP2, 0x1002);

	agnx_write32(ctl, AGNX_GCR_GAINSET0, 0x1d);
	agnx_write32(ctl, AGNX_GCR_GAINSET1, 0x1d);
	agnx_write32(ctl, AGNX_GCR_GAINSET2, 0x1d);
	agnx_write32(ctl, AGNX_GCR_GAINSETWRITE, 0x1);

	agnx_write32(ctl, AGNX_ACI_MODE, 0x1);
	agnx_write32(ctl, AGNX_ACI_LEN, 0x3ff);

	agnx_write32(ctl, AGNX_ACI_TIMER1, 0x27);
	agnx_write32(ctl, AGNX_ACI_TIMER2, 0x27);

	spi_rf_write(ctl, RF_CHIP0|RF_CHIP1|RF_CHIP2, 0x1400);
	spi_rf_write(ctl, RF_CHIP0|RF_CHIP1|RF_CHIP2, 0x1500);

	/* Measure Calibration */
	agnx_write32(ctl, AGNX_ACI_MEASURE, 0x1);
	calibra_delay(priv);

	/* do calibration */
	do_calibration(priv);

	agnx_write32(ctl, AGNX_GCR_RXOVERIDE, 0x0);
	agnx_write32(ctl, AGNX_ACI_TIMER1, 0x21);
	agnx_write32(ctl, AGNX_ACI_TIMER2, 0x27);
	agnx_write32(ctl, AGNX_ACI_LEN, 0xf);

	reg = agnx_read32(ctl, AGNX_GCR_GAINSET0);
	reg &= 0xf;
	agnx_write32(ctl, AGNX_GCR_GAINSET0, reg);
	reg = agnx_read32(ctl, AGNX_GCR_GAINSET1);
	reg &= 0xf;
	agnx_write32(ctl, AGNX_GCR_GAINSET1, reg);
	reg = agnx_read32(ctl, AGNX_GCR_GAINSET2);
	reg &= 0xf;
	agnx_write32(ctl, AGNX_GCR_GAINSET2, reg);

	agnx_write32(ctl, AGNX_GCR_GAINSETWRITE, 0x0);
	disable_receiver(priv);
} /* antenna_calibrate */

void __antenna_calibrate(struct agnx_priv *priv)
{
	void __iomem *ctl = priv->ctl;
	u32 reg;

	/* Calibrate the BaseBandFilter */
	/* base_band_filter_calibrate(priv); */
	spi_rf_write(ctl, RF_CHIP0|RF_CHIP1|RF_CHIP2, 0x1002);


	agnx_write32(ctl, AGNX_GCR_GAINSET0, 0x1d);
	agnx_write32(ctl, AGNX_GCR_GAINSET1, 0x1d);
	agnx_write32(ctl, AGNX_GCR_GAINSET2, 0x1d);

	agnx_write32(ctl, AGNX_GCR_GAINSETWRITE, 0x1);

	agnx_write32(ctl, AGNX_ACI_MODE, 0x1);
	agnx_write32(ctl, AGNX_ACI_LEN, 0x3ff);


	agnx_write32(ctl, AGNX_ACI_TIMER1, 0x27);
	agnx_write32(ctl, AGNX_ACI_TIMER2, 0x27);
	spi_rf_write(ctl, RF_CHIP0|RF_CHIP1|RF_CHIP2, 0x1400);
	spi_rf_write(ctl, RF_CHIP0|RF_CHIP1|RF_CHIP2, 0x1500);
	/* Measure Calibration */
	agnx_write32(ctl, AGNX_ACI_MEASURE, 0x1);
	calibra_delay(priv);
	do_calibration(priv);
	agnx_write32(ctl, AGNX_GCR_RXOVERIDE, 0x0);

	agnx_write32(ctl, AGNX_ACI_TIMER1, 0x21);
	agnx_write32(ctl, AGNX_ACI_TIMER2, 0x27);

	agnx_write32(ctl, AGNX_ACI_LEN, 0xf);

	reg = agnx_read32(ctl, AGNX_GCR_GAINSET0);
	reg &= 0xf;
	agnx_write32(ctl, AGNX_GCR_GAINSET0, reg);
	reg = agnx_read32(ctl, AGNX_GCR_GAINSET1);
	reg &= 0xf;
	agnx_write32(ctl, AGNX_GCR_GAINSET1, reg);
	reg = agnx_read32(ctl, AGNX_GCR_GAINSET2);
	reg &= 0xf;
	agnx_write32(ctl, AGNX_GCR_GAINSET2, reg);


	agnx_write32(ctl, AGNX_GCR_GAINSETWRITE, 0x0);

	/* Write 0x3 Gain Control Discovery Mode */
	enable_receiver(priv);
}

int agnx_set_channel(struct agnx_priv *priv, unsigned int channel)
{
	AGNX_TRACE;

	printk(KERN_ERR PFX "Channel is %d %s\n", channel, __func__);
	radio_channel_set(priv, channel);
	return 0;
}
