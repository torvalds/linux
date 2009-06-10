/**
 * Airgo MIMO wireless driver
 *
 * Copyright (c) 2007 Li YanBo <dreamfly281@gmail.com>

 * Thanks for Jeff Williams <angelbane@gmail.com> do reverse engineer
 * works and published the SPECS at http://airgo.wdwconsulting.net/mymoin

 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/etherdevice.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include "agnx.h"
#include "debug.h"
#include "phy.h"
#include "table.h"
#include "sta.h"
#include "xmit.h"

u8 read_from_eeprom(struct agnx_priv *priv, u16 address)
{
	void __iomem *ctl = priv->ctl;
	struct agnx_eeprom cmd;
	u32 reg;

	memset(&cmd, 0, sizeof(cmd));
	cmd.cmd = EEPROM_CMD_READ << AGNX_EEPROM_COMMAND_SHIFT;
	cmd.address = address;
	/* Verify that the Status bit is clear */
	/* Read Command and Address are written to the Serial Interface */
	iowrite32(*(__le32 *)&cmd, ctl + AGNX_CIR_SERIALITF);
	/* Wait for the Status bit to clear again */
	eeprom_delay();
	/* Read from Data */
	reg = ioread32(ctl + AGNX_CIR_SERIALITF);

	cmd = *(struct agnx_eeprom *)&reg;

	return cmd.data;
}

static int card_full_reset(struct agnx_priv *priv)
{
	void __iomem *ctl = priv->ctl;
	u32 reg;
	AGNX_TRACE;

	reg = agnx_read32(ctl, AGNX_CIR_BLKCTL);
	agnx_write32(ctl, AGNX_CIR_BLKCTL, 0x80);
	reg = agnx_read32(ctl, AGNX_CIR_BLKCTL);
	return 0;
}

inline void enable_power_saving(struct agnx_priv *priv)
{
	void __iomem *ctl = priv->ctl;
	u32 reg;

	reg = agnx_read32(ctl, AGNX_PM_PMCTL);
	reg &= ~0x8;
	agnx_write32(ctl, AGNX_PM_PMCTL, reg);
}

inline void disable_power_saving(struct agnx_priv *priv)
{
	void __iomem *ctl = priv->ctl;
	u32 reg;

	reg = agnx_read32(ctl, AGNX_PM_PMCTL);
	reg |= 0x8;
	agnx_write32(ctl, AGNX_PM_PMCTL, reg);
}


void disable_receiver(struct agnx_priv *priv)
{
	void __iomem *ctl = priv->ctl;
	AGNX_TRACE;

	/* FIXME Disable the receiver */
	agnx_write32(ctl, AGNX_GCR_DISCOVMOD, 0x0);
	/* Set gain control reset */
	agnx_write32(ctl, AGNX_GCR_RSTGCTL, 0x1);
	/* Reset gain control reset */
	agnx_write32(ctl, AGNX_GCR_RSTGCTL, 0x0);
}


/* Fixme this shoule be disable RX, above is enable RX */
void enable_receiver(struct agnx_priv *priv)
{
	void __iomem *ctl = priv->ctl;
	AGNX_TRACE;

	/* Set adaptive gain control discovery mode */
	agnx_write32(ctl, AGNX_GCR_DISCOVMOD, 0x3);
	/* Set gain control reset */
	agnx_write32(ctl, AGNX_GCR_RSTGCTL, 0x1);
	/* Clear gain control reset */
	agnx_write32(ctl, AGNX_GCR_RSTGCTL, 0x0);
}

static void mac_address_set(struct agnx_priv *priv)
{
	void __iomem *ctl = priv->ctl;
	u8 *mac_addr = priv->mac_addr;
	u32 reg;

	/* FIXME */
	reg = (mac_addr[0] << 24) | (mac_addr[1] << 16) | mac_addr[2] << 8 | mac_addr[3];
	iowrite32(reg, ctl + AGNX_RXM_MACHI);
	reg = (mac_addr[4] << 8) | mac_addr[5];
	iowrite32(reg, ctl + AGNX_RXM_MACLO);
}

static void receiver_bssid_set(struct agnx_priv *priv, u8 *bssid)
{
	void __iomem *ctl = priv->ctl;
	u32 reg;

	disable_receiver(priv);
	/* FIXME */
	reg = bssid[0] << 24 | (bssid[1] << 16) | (bssid[2] << 8) | bssid[3];
	iowrite32(reg, ctl + AGNX_RXM_BSSIDHI);
	reg = (bssid[4] << 8) | bssid[5];
	iowrite32(reg, ctl + AGNX_RXM_BSSIDLO);

	/* Enable the receiver */
	enable_receiver(priv);

	/* Clear the TSF */
/* 	agnx_write32(ctl, AGNX_TXM_TSFLO, 0x0); */
/* 	agnx_write32(ctl, AGNX_TXM_TSFHI, 0x0); */
	/* Clear the TBTT */
	agnx_write32(ctl, AGNX_TXM_TBTTLO, 0x0);
	agnx_write32(ctl, AGNX_TXM_TBTTHI, 0x0);
	disable_receiver(priv);
} /* receiver_bssid_set */

static void band_management_init(struct agnx_priv *priv)
{
	void __iomem *ctl = priv->ctl;
	void __iomem *data = priv->data;
	u32 reg;
	int i;
	AGNX_TRACE;

	agnx_write32(ctl, AGNX_BM_TXWADDR, AGNX_PDU_TX_WQ);
	agnx_write32(ctl, AGNX_CIR_ADDRWIN, 0x0);
	memset_io(data + AGNX_PDUPOOL, 0x0, AGNX_PDUPOOL_SIZE);
	agnx_write32(ctl, AGNX_BM_BMCTL, 0x200);

	agnx_write32(ctl, AGNX_BM_CIPDUWCNT, 0x40);
	agnx_write32(ctl, AGNX_BM_SPPDUWCNT, 0x2);
	agnx_write32(ctl, AGNX_BM_RFPPDUWCNT, 0x0);
	agnx_write32(ctl, AGNX_BM_RHPPDUWCNT, 0x22);

	/* FIXME Initialize the Free Pool Linked List */
	/*    1. Write the Address of the Next Node ((0x41800 + node*size)/size)
	      to the first word of each node.  */
	for (i = 0; i < PDU_FREE_CNT; i++) {
		iowrite32((AGNX_PDU_FREE + (i+1)*PDU_SIZE)/PDU_SIZE,
			  data + AGNX_PDU_FREE + (PDU_SIZE * i));
		/* The last node should be set to 0x0 */
		if ((i + 1) == PDU_FREE_CNT)
			memset_io(data + AGNX_PDU_FREE + (PDU_SIZE * i),
				  0x0, PDU_SIZE);
	}

	/* Head is First Pool address (0x41800) / size (0x80) */
	agnx_write32(ctl, AGNX_BM_FPLHP, AGNX_PDU_FREE/PDU_SIZE);
	/* Tail is Last Pool Address (0x47f80) / size (0x80) */
	agnx_write32(ctl, AGNX_BM_FPLTP, 0x47f80/PDU_SIZE);
	/* Count is Number of Nodes in the Pool (0xd0) */
	agnx_write32(ctl, AGNX_BM_FPCNT, PDU_FREE_CNT);

	/* Start all workqueue */
	agnx_write32(ctl, AGNX_BM_CIWQCTL, 0x80000);
	agnx_write32(ctl, AGNX_BM_CPULWCTL, 0x80000);
	agnx_write32(ctl, AGNX_BM_CPUHWCTL, 0x80000);
	agnx_write32(ctl, AGNX_BM_CPUTXWCTL, 0x80000);
	agnx_write32(ctl, AGNX_BM_CPURXWCTL, 0x80000);
	agnx_write32(ctl, AGNX_BM_SPRXWCTL, 0x80000);
	agnx_write32(ctl, AGNX_BM_SPTXWCTL, 0x80000);
	agnx_write32(ctl, AGNX_BM_RFPWCTL, 0x80000);

	/* Enable the Band Management */
	reg = agnx_read32(ctl, AGNX_BM_BMCTL);
	reg |= 0x1;
	agnx_write32(ctl, AGNX_BM_BMCTL, reg);
} /* band_managment_init */


static void system_itf_init(struct agnx_priv *priv)
{
	void __iomem *ctl = priv->ctl;
	u32 reg;
	AGNX_TRACE;

	agnx_write32(ctl, AGNX_SYSITF_GPIOUT, 0x0);
	agnx_write32(ctl, AGNX_PM_TESTPHY, 0x11e143a);

	if (priv->revid == 0) {
		reg = agnx_read32(ctl, AGNX_SYSITF_SYSMODE);
		reg |= 0x11;
		agnx_write32(ctl, AGNX_SYSITF_SYSMODE, reg);
	}
	/* ??? What is that means? it should difference for differice type
	 of cards */
	agnx_write32(ctl, AGNX_CIR_SERIALITF, 0xfff81006);

	agnx_write32(ctl, AGNX_SYSITF_GPIOIN, 0x1f0000);
	agnx_write32(ctl, AGNX_SYSITF_GPIOUT, 0x5);
	reg = agnx_read32(ctl, AGNX_SYSITF_GPIOIN);
}

static void encryption_init(struct agnx_priv *priv)
{
	void __iomem *ctl = priv->ctl;
	AGNX_TRACE;

	agnx_write32(ctl, AGNX_ENCRY_WEPKEY0, 0x0);
	agnx_write32(ctl, AGNX_ENCRY_WEPKEY1, 0x0);
	agnx_write32(ctl, AGNX_ENCRY_WEPKEY2, 0x0);
	agnx_write32(ctl, AGNX_ENCRY_WEPKEY3, 0x0);
	agnx_write32(ctl, AGNX_ENCRY_CCMRECTL, 0x8);
}

static void tx_management_init(struct agnx_priv *priv)
{
	void __iomem *ctl = priv->ctl;
	void __iomem *data = priv->data;
	u32 reg;
	AGNX_TRACE;

	/* Fill out the ComputationalEngineLookupTable
	 * starting at memory #2 offset 0x800
	 */
	tx_engine_lookup_tbl_init(priv);
	memset_io(data + 0x1000, 0, 0xfe0);
	/* Enable Transmission Management Functions */
	agnx_write32(ctl, AGNX_TXM_ETMF, 0x3ff);
	/* Write 0x3f to Transmission Template */
	agnx_write32(ctl, AGNX_TXM_TXTEMP, 0x3f);

	if (priv->revid >= 2)
		agnx_write32(ctl, AGNX_TXM_SIFSPIFS, 0x1e140a0b);
	else
		agnx_write32(ctl, AGNX_TXM_SIFSPIFS, 0x1e190a0b);

	reg = agnx_read32(ctl, AGNX_TXM_TIFSEIFS);
	reg &= 0xff00;
	reg |= 0xb;
	agnx_write32(ctl, AGNX_TXM_TIFSEIFS, reg);
	reg = agnx_read32(ctl, AGNX_TXM_TIFSEIFS);
	reg &= 0xffff00ff;
	reg |= 0xa00;
	agnx_write32(ctl, AGNX_TXM_TIFSEIFS, reg);
	/* Enable TIFS */
	agnx_write32(ctl, AGNX_TXM_CTL, 0x40000);

	reg = agnx_read32(ctl, AGNX_TXM_TIFSEIFS);
	reg &= 0xff00ffff;
	reg |= 0x510000;
	agnx_write32(ctl, AGNX_TXM_TIFSEIFS, reg);
	reg = agnx_read32(ctl, AGNX_TXM_PROBDELAY);
	reg &= 0xff00ffff;
	agnx_write32(ctl, AGNX_TXM_PROBDELAY, reg);
	reg = agnx_read32(ctl, AGNX_TXM_TIFSEIFS);
	reg &= 0x00ffffff;
	reg |= 0x1c000000;
	agnx_write32(ctl, AGNX_TXM_TIFSEIFS, reg);
	reg = agnx_read32(ctl, AGNX_TXM_PROBDELAY);
	reg &= 0x00ffffff;
	reg |= 0x01000000;
	agnx_write32(ctl, AGNX_TXM_PROBDELAY, reg);

	/* # Set DIF 0-1,2-3,4-5,6-7 to defaults */
	agnx_write32(ctl, AGNX_TXM_DIF01, 0x321d321d);
	agnx_write32(ctl, AGNX_TXM_DIF23, 0x321d321d);
	agnx_write32(ctl, AGNX_TXM_DIF45, 0x321d321d);
	agnx_write32(ctl, AGNX_TXM_DIF67, 0x321d321d);

	/* Max Ack timeout limit */
	agnx_write32(ctl, AGNX_TXM_MAXACKTIM, 0x1e19);
	/* Max RX Data Timeout count, */
	reg = agnx_read32(ctl, AGNX_TXM_MAXRXTIME);
	reg &= 0xffff0000;
	reg |= 0xff;
	agnx_write32(ctl, AGNX_TXM_MAXRXTIME, reg);

	/* CF poll RX Timeout count */
	reg = agnx_read32(ctl, AGNX_TXM_CFPOLLRXTIM);
	reg &= 0xffff;
	reg |= 0xff0000;
	agnx_write32(ctl, AGNX_TXM_CFPOLLRXTIM, reg);

	/* Max Timeout Exceeded count, */
	reg = agnx_read32(ctl, AGNX_TXM_MAXTIMOUT);
	reg &= 0xff00ffff;
	reg |= 0x190000;
	agnx_write32(ctl, AGNX_TXM_MAXTIMOUT, reg);

	/* CF ack timeout limit for 11b */
	reg = agnx_read32(ctl, AGNX_TXM_CFACKT11B);
	reg &= 0xff00;
	reg |= 0x1e;
	agnx_write32(ctl, AGNX_TXM_CFACKT11B, reg);

	/* Max CF Poll Timeout Count */
	reg = agnx_read32(ctl, AGNX_TXM_CFPOLLRXTIM);
	reg &= 0xffff0000;
	reg |= 0x19;
	agnx_write32(ctl, AGNX_TXM_CFPOLLRXTIM, reg);
	/* CF Poll RX Timeout Count */
	reg = agnx_read32(ctl, AGNX_TXM_CFPOLLRXTIM);
	reg &= 0xffff0000;
	reg |= 0x1e;
	agnx_write32(ctl, AGNX_TXM_CFPOLLRXTIM, reg);

	/* # write default to */
	/*    1. Schedule Empty Count */
	agnx_write32(ctl, AGNX_TXM_SCHEMPCNT, 0x5);
	/*    2. CFP Period Count */
	agnx_write32(ctl, AGNX_TXM_CFPERCNT, 0x1);
	/*    3. CFP MDV  */
	agnx_write32(ctl, AGNX_TXM_CFPMDV, 0x10000);

	/* Probe Delay */
	reg = agnx_read32(ctl, AGNX_TXM_PROBDELAY);
	reg &= 0xffff0000;
	reg |= 0x400;
	agnx_write32(ctl, AGNX_TXM_PROBDELAY, reg);

	/* Max CCA count Slot */
	reg = agnx_read32(ctl, AGNX_TXM_MAXCCACNTSLOT);
	reg &= 0xffff00ff;
	reg |= 0x900;
	agnx_write32(ctl, AGNX_TXM_MAXCCACNTSLOT, reg);

	/* Slot limit/1 msec Limit */
	reg = agnx_read32(ctl, AGNX_TXM_SLOTLIMIT);
	reg &= 0xff00ffff;
	reg |= 0x140077;
	agnx_write32(ctl, AGNX_TXM_SLOTLIMIT, reg);

	/* # Set CW #(0-7) to default */
	agnx_write32(ctl, AGNX_TXM_CW0, 0xff0007);
	agnx_write32(ctl, AGNX_TXM_CW1, 0xff0007);
	agnx_write32(ctl, AGNX_TXM_CW2, 0xff0007);
	agnx_write32(ctl, AGNX_TXM_CW3, 0xff0007);
	agnx_write32(ctl, AGNX_TXM_CW4, 0xff0007);
	agnx_write32(ctl, AGNX_TXM_CW5, 0xff0007);
	agnx_write32(ctl, AGNX_TXM_CW6, 0xff0007);
	agnx_write32(ctl, AGNX_TXM_CW7, 0xff0007);

	/* # Set Short/Long limit #(0-7) to default */
	agnx_write32(ctl, AGNX_TXM_SLBEALIM0,  0xa000a);
	agnx_write32(ctl, AGNX_TXM_SLBEALIM1,  0xa000a);
	agnx_write32(ctl, AGNX_TXM_SLBEALIM2,  0xa000a);
	agnx_write32(ctl, AGNX_TXM_SLBEALIM3,  0xa000a);
	agnx_write32(ctl, AGNX_TXM_SLBEALIM4,  0xa000a);
	agnx_write32(ctl, AGNX_TXM_SLBEALIM5,  0xa000a);
	agnx_write32(ctl, AGNX_TXM_SLBEALIM6,  0xa000a);
	agnx_write32(ctl, AGNX_TXM_SLBEALIM7,  0xa000a);

	reg = agnx_read32(ctl, AGNX_TXM_CTL);
	reg |= 0x1400;
	agnx_write32(ctl, AGNX_TXM_CTL, reg);
	/* Wait for bit 0 in Control Reg to clear  */
	udelay(80);
	reg = agnx_read32(ctl, AGNX_TXM_CTL);
	/* Or 0x18000 to Control reg */
	reg = agnx_read32(ctl, AGNX_TXM_CTL);
	reg |= 0x18000;
	agnx_write32(ctl, AGNX_TXM_CTL, reg);
	/* Wait for bit 0 in Control Reg to clear */
	udelay(80);
	reg = agnx_read32(ctl, AGNX_TXM_CTL);

	/* Set Listen Interval Count to default */
	agnx_write32(ctl, AGNX_TXM_LISINTERCNT, 0x1);
	/* Set DTIM period count to default */
	agnx_write32(ctl, AGNX_TXM_DTIMPERICNT, 0x2000);
} /* tx_management_init */

static void rx_management_init(struct agnx_priv *priv)
{
	void __iomem *ctl = priv->ctl;
	AGNX_TRACE;

	/* Initialize the Routing Table */
	routing_table_init(priv);

	if (priv->revid >= 3) {
		agnx_write32(ctl, 0x2074, 0x1f171710);
		agnx_write32(ctl, 0x2078, 0x10100d0d);
		agnx_write32(ctl, 0x207c, 0x11111010);
	} else {
		agnx_write32(ctl, AGNX_RXM_DELAY11, 0x0);
	}
	agnx_write32(ctl, AGNX_RXM_REQRATE, 0x8195e00);
}


static void agnx_timer_init(struct agnx_priv *priv)
{
	void __iomem *ctl = priv->ctl;
	AGNX_TRACE;

/* 	/\* Write 0x249f00 (tick duration?) to Timer 1 *\/ */
/* 	agnx_write32(ctl, AGNX_TIMCTL_TIMER1, 0x249f00); */
/* 	/\* Write 0xe2 to Timer 1 Control *\/ */
/* 	agnx_write32(ctl, AGNX_TIMCTL_TIM1CTL, 0xe2); */

	/* Write 0x249f00 (tick duration?) to Timer 1 */
	agnx_write32(ctl, AGNX_TIMCTL_TIMER1, 0x0);
	/* Write 0xe2 to Timer 1 Control */
	agnx_write32(ctl, AGNX_TIMCTL_TIM1CTL, 0x0);

	iowrite32(0xFFFFFFFF, priv->ctl + AGNX_TXM_BEACON_CTL);
}

static void power_manage_init(struct agnx_priv *priv)
{
	void __iomem *ctl = priv->ctl;
	u32 reg;
	AGNX_TRACE;

	agnx_write32(ctl, AGNX_PM_MACMSW, 0x1f);
	agnx_write32(ctl, AGNX_PM_RFCTL, 0x1f);

	reg = agnx_read32(ctl, AGNX_PM_PMCTL);
	reg &= 0xf00f;
	reg |= 0xa0;
	agnx_write32(ctl, AGNX_PM_PMCTL, reg);

	if (priv->revid >= 3) {
		reg = agnx_read32(ctl, AGNX_PM_SOFTRST);
		reg |= 0x18;
		agnx_write32(ctl, AGNX_PM_SOFTRST, reg);
	}
} /* power_manage_init */


static void gain_ctlcnt_init(struct agnx_priv *priv)
{
	void __iomem *ctl = priv->ctl;
	u32 reg;
	AGNX_TRACE;

	agnx_write32(ctl, AGNX_GCR_TRACNT5, 0x119);
	agnx_write32(ctl, AGNX_GCR_TRACNT6, 0x118);
	agnx_write32(ctl, AGNX_GCR_TRACNT7, 0x117);

	reg = agnx_read32(ctl, AGNX_PM_PMCTL);
	reg |= 0x8;
	agnx_write32(ctl, AGNX_PM_PMCTL, reg);

	reg = agnx_read32(ctl, AGNX_PM_PMCTL);
	reg &= ~0x8;
	agnx_write32(ctl, AGNX_PM_PMCTL, reg);

	agnx_write32(ctl, AGNX_CIR_ADDRWIN, 0x0);

	/* FIXME Write the initial Station Descriptor for the card */
	sta_init(priv, LOCAL_STAID);
	sta_init(priv, BSSID_STAID);

	/* Enable staion 0 and 1 can do TX */
	/* It seemed if we set other bit to 1 the bit 0 will
	   be auto change to 0 */
	agnx_write32(ctl, AGNX_BM_TXTOPEER, 0x2 | 0x1);
/*	agnx_write32(ctl, AGNX_BM_TXTOPEER, 0x1); */
} /* gain_ctlcnt_init */


static void phy_init(struct agnx_priv *priv)
{
	void __iomem *ctl = priv->ctl;
	void __iomem *data = priv->data;
	u32 reg;
	AGNX_TRACE;

	/* Load InitialGainTable */
	gain_table_init(priv);

	agnx_write32(ctl, AGNX_CIR_ADDRWIN, 0x2000000);

	/* Clear the following offsets in Memory Range #2: */
	memset_io(data + 0x5040, 0, 0xa * 4);
	memset_io(data + 0x5080, 0, 0xa * 4);
	memset_io(data + 0x50c0, 0, 0xa * 4);
	memset_io(data + 0x5400, 0, 0x80 * 4);
	memset_io(data + 0x6000, 0, 0x280 * 4);
	memset_io(data + 0x7000, 0, 0x280 * 4);
	memset_io(data + 0x8000, 0, 0x280 * 4);

	/* Initialize the Following Registers According to PCI Revision ID */
	if (priv->revid == 0) {
		/* fixme the part hasn't been update but below has been update
		   based on WGM511 */
		agnx_write32(ctl, AGNX_ACI_LEN, 0xf);
		agnx_write32(ctl, AGNX_ACI_TIMER1, 0x1d);
		agnx_write32(ctl, AGNX_ACI_TIMER2, 0x3);
		agnx_write32(ctl, AGNX_ACI_AICCHA0OVE, 0x11);
		agnx_write32(ctl, AGNX_ACI_AICCHA1OVE, 0x0);
		agnx_write32(ctl, AGNX_GCR_THD0A, 0x64);
		agnx_write32(ctl, AGNX_GCR_THD0AL, 0x4b);
		agnx_write32(ctl, AGNX_GCR_THD0B, 0x4b);
		agnx_write32(ctl, AGNX_GCR_DUNSAT, 0x14);
		agnx_write32(ctl, AGNX_GCR_DSAT, 0x24);
		agnx_write32(ctl, AGNX_GCR_DFIRCAL, 0x8);
		agnx_write32(ctl, AGNX_GCR_DGCTL11A, 0x1a);
		agnx_write32(ctl, AGNX_GCR_DGCTL11B, 0x3);
		agnx_write32(ctl, AGNX_GCR_GAININIT, 0xd);
		agnx_write32(ctl, AGNX_GCR_THNOSIG, 0x1);
		agnx_write32(ctl, AGNX_GCR_COARSTEP, 0x7);
		agnx_write32(ctl, AGNX_GCR_SIFST11A, 0x28);
		agnx_write32(ctl, AGNX_GCR_SIFST11B, 0x28);
		reg = agnx_read32(ctl, AGNX_GCR_CWDETEC);
		reg |= 0x1;
		agnx_write32(ctl, AGNX_GCR_CWDETEC, reg);
		agnx_write32(ctl, AGNX_GCR_0X38, 0x1e);
		agnx_write32(ctl, AGNX_GCR_BOACT, 0x26);
		agnx_write32(ctl, AGNX_GCR_DISCOVMOD, 0x3);
		agnx_write32(ctl, AGNX_GCR_NLISTANT, 0x3);
		agnx_write32(ctl, AGNX_GCR_NACTIANT, 0x3);
		agnx_write32(ctl, AGNX_GCR_NMEASANT, 0x3);
		agnx_write32(ctl, AGNX_GCR_NCAPTANT, 0x3);
		agnx_write32(ctl, AGNX_GCR_THCAP11A, 0x0);
		agnx_write32(ctl, AGNX_GCR_THCAP11B, 0x0);
		agnx_write32(ctl, AGNX_GCR_THCAPRX11A, 0x0);
		agnx_write32(ctl, AGNX_GCR_THCAPRX11B, 0x0);
		agnx_write32(ctl, AGNX_GCR_THLEVDRO, 0x10);
		agnx_write32(ctl, AGNX_GCR_MAXRXTIME11A, 0x1);
		agnx_write32(ctl, AGNX_GCR_MAXRXTIME11B, 0x1);
		agnx_write32(ctl, AGNX_GCR_CORRTIME, 0x190);
		agnx_write32(ctl, AGNX_GCR_SIGHTH, 0x78);
		agnx_write32(ctl, AGNX_GCR_SIGLTH, 0x1c);
		agnx_write32(ctl, AGNX_GCR_CORRDROP, 0x0);
		agnx_write32(ctl, AGNX_GCR_THCD, 0x0);
		agnx_write32(ctl, AGNX_GCR_MAXPOWDIFF, 0x1);
		agnx_write32(ctl, AGNX_GCR_TESTBUS, 0x0);
		agnx_write32(ctl, AGNX_GCR_ANTCFG, 0x1f);
		agnx_write32(ctl, AGNX_GCR_THJUMP, 0x14);
		agnx_write32(ctl, AGNX_GCR_THPOWER, 0x0);
		agnx_write32(ctl, AGNX_GCR_THPOWCLIP, 0x30);
		agnx_write32(ctl, AGNX_GCR_THD0BTFEST, 0x32);
		agnx_write32(ctl, AGNX_GCR_THRX11BPOWMIN, 0x19);
		agnx_write32(ctl, AGNX_GCR_0X14c, 0x0);
		agnx_write32(ctl, AGNX_GCR_0X150, 0x0);
		agnx_write32(ctl, 0x9400, 0x0);
		agnx_write32(ctl, 0x940c, 0x6ff);
		agnx_write32(ctl, 0x9428, 0xa0);
		agnx_write32(ctl, 0x9434, 0x0);
		agnx_write32(ctl, 0x9c04, 0x15);
		agnx_write32(ctl, 0x9c0c, 0x7f);
		agnx_write32(ctl, 0x9c34, 0x0);
		agnx_write32(ctl, 0xc000, 0x38d);
		agnx_write32(ctl, 0x14018, 0x0);
		agnx_write32(ctl, 0x16000, 0x1);
		agnx_write32(ctl, 0x11004, 0x0);
		agnx_write32(ctl, 0xec54, 0xa);
		agnx_write32(ctl, 0xec1c, 0x5);
	} else if (priv->revid > 0) {
		agnx_write32(ctl, AGNX_ACI_LEN, 0xf);
		agnx_write32(ctl, AGNX_ACI_TIMER1, 0x21);
		agnx_write32(ctl, AGNX_ACI_TIMER2, 0x27);
		agnx_write32(ctl, AGNX_ACI_AICCHA0OVE, 0x11);
		agnx_write32(ctl, AGNX_ACI_AICCHA1OVE, 0x0);
		agnx_write32(ctl, AGNX_GCR_DUNSAT, 0x14);
		agnx_write32(ctl, AGNX_GCR_DSAT, 0x24);
		agnx_write32(ctl, AGNX_GCR_DFIRCAL, 0x8);
		agnx_write32(ctl, AGNX_GCR_DGCTL11A, 0x1a);
		agnx_write32(ctl, AGNX_GCR_DGCTL11B, 0x3);
		agnx_write32(ctl, AGNX_GCR_GAININIT, 0xd);
		agnx_write32(ctl, AGNX_GCR_THNOSIG, 0x1);
		agnx_write32(ctl, AGNX_GCR_COARSTEP, 0x7);
		agnx_write32(ctl, AGNX_GCR_SIFST11A, 0x28);
		agnx_write32(ctl, AGNX_GCR_SIFST11B, 0x28);
		agnx_write32(ctl, AGNX_GCR_CWDETEC, 0x0);
		agnx_write32(ctl, AGNX_GCR_0X38, 0x1e);
/*		agnx_write32(ctl, AGNX_GCR_BOACT, 0x26);*/
		agnx_write32(ctl, AGNX_GCR_DISCOVMOD, 0x3);

		agnx_write32(ctl, AGNX_GCR_THCAP11A, 0x32);
		agnx_write32(ctl, AGNX_GCR_THCAP11B, 0x32);
		agnx_write32(ctl, AGNX_GCR_THCAPRX11A, 0x32);
		agnx_write32(ctl, AGNX_GCR_THCAPRX11B, 0x32);
		agnx_write32(ctl, AGNX_GCR_THLEVDRO, 0x10);
		agnx_write32(ctl, AGNX_GCR_MAXRXTIME11A, 0x1ad);
		agnx_write32(ctl, AGNX_GCR_MAXRXTIME11B, 0xa10);
		agnx_write32(ctl, AGNX_GCR_CORRTIME, 0x190);
		agnx_write32(ctl, AGNX_GCR_CORRDROP, 0x0);
		agnx_write32(ctl, AGNX_GCR_THCD, 0x0);
		agnx_write32(ctl, AGNX_GCR_THCS, 0x0);
		agnx_write32(ctl, AGNX_GCR_MAXPOWDIFF, 0x4);
		agnx_write32(ctl, AGNX_GCR_TESTBUS, 0x0);
		agnx_write32(ctl, AGNX_GCR_THJUMP, 0x1e);
		agnx_write32(ctl, AGNX_GCR_THPOWER, 0x0);
		agnx_write32(ctl, AGNX_GCR_THPOWCLIP, 0x2a);
		agnx_write32(ctl, AGNX_GCR_THD0BTFEST, 0x3c);
		agnx_write32(ctl, AGNX_GCR_THRX11BPOWMIN, 0x19);
		agnx_write32(ctl, AGNX_GCR_0X14c, 0x0);
		agnx_write32(ctl, AGNX_GCR_0X150, 0x0);
		agnx_write32(ctl, AGNX_GCR_RXOVERIDE, 0x0);
		agnx_write32(ctl, AGNX_GCR_WATCHDOG, 0x37);
		agnx_write32(ctl, 0x9400, 0x0);
		agnx_write32(ctl, 0x940c, 0x6ff);
		agnx_write32(ctl, 0x9428, 0xa0);
		agnx_write32(ctl, 0x9434, 0x0);
		agnx_write32(ctl, 0x9c04, 0x15);
		agnx_write32(ctl, 0x9c0c, 0x7f);
		agnx_write32(ctl, 0x9c34, 0x0);
		agnx_write32(ctl, 0xc000, 0x38d);
		agnx_write32(ctl, 0x14014, 0x1000);
		agnx_write32(ctl, 0x14018, 0x0);
		agnx_write32(ctl, 0x16000, 0x1);
		agnx_write32(ctl, 0x11004, 0x0);
		agnx_write32(ctl, 0xec54, 0xa);
		agnx_write32(ctl, 0xec1c, 0x50);
	} else if (priv->revid > 1) {
		reg = agnx_read32(ctl, 0xec18);
		reg |= 0x8;
		agnx_write32(ctl, 0xec18, reg);
	}

	/* Write the TX Fir Coefficient Table */
	tx_fir_table_init(priv);

	reg = agnx_read32(ctl, AGNX_PM_PMCTL);
	reg &= ~0x8;
	agnx_write32(ctl, AGNX_PM_PMCTL, reg);
	reg = agnx_read32(ctl, AGNX_PM_PLLCTL);
	reg |= 0x1;
	agnx_write32(ctl, AGNX_PM_PLLCTL, reg);

/* 	reg = agnx_read32(ctl, 0x1a030); */
/* 	reg &= ~0x4; */
/* 	agnx_write32(ctl, 0x1a030, reg); */

	agnx_write32(ctl, AGNX_GCR_TRACNT4, 0x113);
} /* phy_init */

static void chip_init(struct agnx_priv *priv)
{
	void __iomem *ctl = priv->ctl;
	u32 reg;
	AGNX_TRACE;

	band_management_init(priv);

	rf_chips_init(priv);

	reg = agnx_read32(ctl, AGNX_PM_PMCTL);
	reg |= 0x8;
	agnx_write32(ctl, AGNX_PM_PMCTL, reg);

	/* Initialize the PHY */
	phy_init(priv);

	encryption_init(priv);

	tx_management_init(priv);

	rx_management_init(priv);

	power_manage_init(priv);

	/* Initialize the Timers */
	agnx_timer_init(priv);

	/* Write 0xc390bf9 to Interrupt Mask (Disable TX) */
	reg = 0xc390bf9 & ~IRQ_TX_BEACON;
	reg &= ~IRQ_TX_DISABLE;
	agnx_write32(ctl, AGNX_INT_MASK, reg);

	reg = agnx_read32(ctl, AGNX_CIR_BLKCTL);
	reg |= 0x800;
	agnx_write32(ctl, AGNX_CIR_BLKCTL, reg);

	/* set it when need get multicast enable? */
	agnx_write32(ctl, AGNX_BM_MTSM, 0xff);
} /* chip_init */


static inline void set_promis_and_managed(struct agnx_priv *priv)
{
	void __iomem *ctl = priv->ctl;
	agnx_write32(ctl, AGNX_SYSITF_SYSMODE, 0x10 | 0x2);
	agnx_write32(ctl, AGNX_SYSITF_SYSMODE, 0x10 | 0x2);
}
static inline void set_learn_mode(struct agnx_priv *priv)
{
	void __iomem *ctl = priv->ctl;
	agnx_write32(ctl, AGNX_SYSITF_SYSMODE, 0x8);
}
static inline void set_scan_mode(struct agnx_priv *priv)
{
	void __iomem *ctl = priv->ctl;
	agnx_write32(ctl, AGNX_SYSITF_SYSMODE, 0x20);
}
static inline void set_promiscuous_mode(struct agnx_priv *priv)
{
	void __iomem *ctl = priv->ctl;
	/* agnx_write32(ctl, AGNX_SYSITF_SYSMODE, 0x210);*/
	agnx_write32(ctl, AGNX_SYSITF_SYSMODE, 0x10);
}
static inline void set_managed_mode(struct agnx_priv *priv)
{
	void __iomem *ctl = priv->ctl;
	agnx_write32(ctl, AGNX_SYSITF_SYSMODE, 0x2);
}
static inline void set_adhoc_mode(struct agnx_priv *priv)
{
	void __iomem *ctl = priv->ctl;
	agnx_write32(ctl, AGNX_SYSITF_SYSMODE, 0x0);
}

#if 0
static void unknow_register_write(struct agnx_priv *priv)
{
	void __iomem *ctl = priv->ctl;

	agnx_write32(ctl, AGNX_UNKNOWN_BASE + 0x0, 0x3e);
	agnx_write32(ctl, AGNX_UNKNOWN_BASE + 0x4, 0xb2);
	agnx_write32(ctl, AGNX_UNKNOWN_BASE + 0x8, 0x140);
	agnx_write32(ctl, AGNX_UNKNOWN_BASE + 0xc, 0x1C0);
	agnx_write32(ctl, AGNX_UNKNOWN_BASE + 0x10, 0x1FF);
	agnx_write32(ctl, AGNX_UNKNOWN_BASE + 0x14, 0x1DD);
	agnx_write32(ctl, AGNX_UNKNOWN_BASE + 0x18, 0x15F);
	agnx_write32(ctl, AGNX_UNKNOWN_BASE + 0x1c, 0xA1);
	agnx_write32(ctl, AGNX_UNKNOWN_BASE + 0x20, 0x3E7);
	agnx_write32(ctl, AGNX_UNKNOWN_BASE + 0x24, 0x36B);
	agnx_write32(ctl, AGNX_UNKNOWN_BASE + 0x28, 0x348);
	agnx_write32(ctl, AGNX_UNKNOWN_BASE + 0x2c, 0x37D);
	agnx_write32(ctl, AGNX_UNKNOWN_BASE + 0x30, 0x3DE);
	agnx_write32(ctl, AGNX_UNKNOWN_BASE + 0x34, 0x36);
	agnx_write32(ctl, AGNX_UNKNOWN_BASE + 0x38, 0x64);
	agnx_write32(ctl, AGNX_UNKNOWN_BASE + 0x3c, 0x57);
	agnx_write32(ctl, AGNX_UNKNOWN_BASE + 0x40, 0x23);
	agnx_write32(ctl, AGNX_UNKNOWN_BASE + 0x44, 0x3ED);
	agnx_write32(ctl, AGNX_UNKNOWN_BASE + 0x48, 0x3C9);
	agnx_write32(ctl, AGNX_UNKNOWN_BASE + 0x4c, 0x3CA);
	agnx_write32(ctl, AGNX_UNKNOWN_BASE + 0x50, 0x3E7);
	agnx_write32(ctl, AGNX_UNKNOWN_BASE + 0x54, 0x8);
	agnx_write32(ctl, AGNX_UNKNOWN_BASE + 0x58, 0x1F);
	agnx_write32(ctl, AGNX_UNKNOWN_BASE + 0x5c, 0x1a);
}
#endif

static void card_interface_init(struct agnx_priv *priv)
{
	void __iomem *ctl = priv->ctl;
	u8 bssid[ETH_ALEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
	u32 reg;
	unsigned int i;
	AGNX_TRACE;

	might_sleep();
	/* Clear RX Control and Enable RX queues */
	agnx_write32(ctl, AGNX_CIR_RXCTL, 0x8);

	might_sleep();
	/* Do a full reset of the card */
	card_full_reset(priv);
	might_sleep();

	/* Check and set Card Endianness */
	reg = ioread32(priv->ctl + AGNX_CIR_ENDIAN);
	/* TODO If not 0xB3B2B1B0 set to 0xB3B2B1B0 */
	printk(KERN_INFO PFX "CIR_ENDIAN is %x\n", reg);


	/* Config the eeprom */
	agnx_write32(ctl, AGNX_CIR_SERIALITF, 0x7000086);
	udelay(10);
	reg = agnx_read32(ctl, AGNX_CIR_SERIALITF);


	agnx_write32(ctl, AGNX_PM_SOFTRST, 0x80000033);
	reg = agnx_read32(ctl, 0xec50);
	reg |= 0xf;
	agnx_write32(ctl, 0xec50, reg);
	agnx_write32(ctl, AGNX_PM_SOFTRST, 0x0);


	reg = agnx_read32(ctl, AGNX_SYSITF_GPIOIN);
	udelay(10);
	reg = agnx_read32(ctl, AGNX_CIR_SERIALITF);

	/* Dump the eeprom */
	do {
		char eeprom[0x100000/0x100];

		for (i = 0; i < 0x100000; i += 0x100) {
			agnx_write32(ctl, AGNX_CIR_SERIALITF, 0x3000000 + i);
			udelay(13);
			reg = agnx_read32(ctl, AGNX_CIR_SERIALITF);
			udelay(70);
			reg = agnx_read32(ctl, AGNX_CIR_SERIALITF);
			eeprom[i/0x100] = reg & 0xFF;
			udelay(10);
		}
		print_hex_dump_bytes(PFX "EEPROM: ", DUMP_PREFIX_NONE, eeprom,
				     ARRAY_SIZE(eeprom));
	} while (0);

	spi_rc_write(ctl, RF_CHIP0, 0x26);
	reg = agnx_read32(ctl, AGNX_SPI_RLSW);

	/* Initialize the system interface */
	system_itf_init(priv);

	might_sleep();
	/* Chip Initialization (Polaris) */
	chip_init(priv);
	might_sleep();

	/* Calibrate the antennae */
	antenna_calibrate(priv);

	reg = agnx_read32(ctl, 0xec50);
	reg &= ~0x40;
	agnx_write32(ctl, 0xec50, reg);
	agnx_write32(ctl, AGNX_PM_SOFTRST, 0x0);
	agnx_write32(ctl, AGNX_PM_PLLCTL, 0x1);

	reg = agnx_read32(ctl, AGNX_BM_BMCTL);
	reg |= 0x8000;
	agnx_write32(ctl, AGNX_BM_BMCTL, reg);
	enable_receiver(priv);
	reg = agnx_read32(ctl, AGNX_SYSITF_SYSMODE);
	reg |= 0x200;
	agnx_write32(ctl, AGNX_SYSITF_SYSMODE, reg);
	enable_receiver(priv);

	might_sleep();
	/* Initialize Gain Control Counts */
	gain_ctlcnt_init(priv);

	/* Write Initial Station Power Template for this station(#0) */
	sta_power_init(priv, LOCAL_STAID);

	might_sleep();
	/* Initialize the rx,td,tm rings, for each node in the ring */
	fill_rings(priv);

	might_sleep();


	agnx_write32(ctl, AGNX_PM_SOFTRST, 0x80000033);
	agnx_write32(ctl, 0xec50, 0xc);
	agnx_write32(ctl, AGNX_PM_SOFTRST, 0x0);

	/* FIXME Initialize the transmit control register */
	agnx_write32(ctl, AGNX_TXM_CTL, 0x194c1);

	enable_receiver(priv);

	might_sleep();
	/* FIXME Set the Receive Control Mac Address to card address */
	mac_address_set(priv);
	enable_receiver(priv);
	might_sleep();

	/* Set the recieve request rate */
	/* FIXME Enable the request */
	/* Check packet length */
	/* Set maximum packet length */
/*	agnx_write32(ctl, AGNX_RXM_REQRATE, 0x88195e00); */
/*	enable_receiver(priv); */

	/* Set the Receiver BSSID */
	receiver_bssid_set(priv, bssid);

	/* FIXME Set to managed mode */
	set_managed_mode(priv);
/*	set_promiscuous_mode(priv); */
/*	set_scan_mode(priv); */
/*	set_learn_mode(priv); */
/*	set_promis_and_managed(priv); */
/*	set_adhoc_mode(priv); */

	/* Set the recieve request rate */
	/* Check packet length */
	agnx_write32(ctl, AGNX_RXM_REQRATE, 0x08000000);
	reg = agnx_read32(ctl, AGNX_RXM_REQRATE);
	/* Set maximum packet length */
	reg |= 0x00195e00;
	agnx_write32(ctl, AGNX_RXM_REQRATE, reg);

	/* Configure the RX and TX interrupt */
	reg = ENABLE_RX_INTERRUPT | RX_CACHE_LINE | FRAG_LEN_2048 | FRAG_BE;
	agnx_write32(ctl, AGNX_CIR_RXCFG, reg);
	/* FIXME */
	reg = ENABLE_TX_INTERRUPT | TX_CACHE_LINE | FRAG_LEN_2048 | FRAG_BE;
	agnx_write32(ctl, AGNX_CIR_TXCFG, reg);

	/* Enable RX TX Interrupts */
	agnx_write32(ctl, AGNX_CIR_RXCTL, 0x80);
	agnx_write32(ctl, AGNX_CIR_TXMCTL, 0x80);
	agnx_write32(ctl, AGNX_CIR_TXDCTL, 0x80);

	/* FIXME Set the master control interrupt in block control */
	agnx_write32(ctl, AGNX_CIR_BLKCTL, 0x800);

	/* Enable RX and TX queues */
	reg = agnx_read32(ctl, AGNX_CIR_RXCTL);
	reg |= 0x8;
	agnx_write32(ctl, AGNX_CIR_RXCTL, reg);
	reg = agnx_read32(ctl, AGNX_CIR_TXMCTL);
	reg |= 0x8;
	agnx_write32(ctl, AGNX_CIR_TXMCTL, reg);
	reg = agnx_read32(ctl, AGNX_CIR_TXDCTL);
	reg |= 0x8;
	agnx_write32(ctl, AGNX_CIR_TXDCTL, reg);

	agnx_write32(ctl, AGNX_SYSITF_GPIOUT, 0x5);
	/* FIXME */
	/*  unknow_register_write(priv); */
	/* Update local card hash entry */
	hash_write(priv, priv->mac_addr, LOCAL_STAID);

	might_sleep();

	/* FIXME */
	agnx_set_channel(priv, 1);
	might_sleep();
} /* agnx_card_interface_init */


void agnx_hw_init(struct agnx_priv *priv)
{
	AGNX_TRACE;
	might_sleep();
	card_interface_init(priv);
}

int agnx_hw_reset(struct agnx_priv *priv)
{
	return card_full_reset(priv);
}

int agnx_set_ssid(struct agnx_priv *priv, u8 *ssid, size_t ssid_len)
{
	AGNX_TRACE;
	return 0;
}

void agnx_set_bssid(struct agnx_priv *priv, u8 *bssid)
{
	receiver_bssid_set(priv, bssid);
}
