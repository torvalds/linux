/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2009-2016 Cavium, Inc.
 */

#include <linux/delay.h>
#include <linux/module.h>
#include <linux/phy.h>
#include <linux/io.h>

#include "mdio-cavium.h"

static void cavium_mdiobus_set_mode(struct cavium_mdiobus *p,
				    enum cavium_mdiobus_mode m)
{
	union cvmx_smix_clk smi_clk;

	if (m == p->mode)
		return;

	smi_clk.u64 = oct_mdio_readq(p->register_base + SMI_CLK);
	smi_clk.s.mode = (m == C45) ? 1 : 0;
	smi_clk.s.preamble = 1;
	oct_mdio_writeq(smi_clk.u64, p->register_base + SMI_CLK);
	p->mode = m;
}

static int cavium_mdiobus_c45_addr(struct cavium_mdiobus *p,
				   int phy_id, int regnum)
{
	union cvmx_smix_cmd smi_cmd;
	union cvmx_smix_wr_dat smi_wr;
	int timeout = 1000;

	cavium_mdiobus_set_mode(p, C45);

	smi_wr.u64 = 0;
	smi_wr.s.dat = regnum & 0xffff;
	oct_mdio_writeq(smi_wr.u64, p->register_base + SMI_WR_DAT);

	regnum = (regnum >> 16) & 0x1f;

	smi_cmd.u64 = 0;
	smi_cmd.s.phy_op = 0; /* MDIO_CLAUSE_45_ADDRESS */
	smi_cmd.s.phy_adr = phy_id;
	smi_cmd.s.reg_adr = regnum;
	oct_mdio_writeq(smi_cmd.u64, p->register_base + SMI_CMD);

	do {
		/* Wait 1000 clocks so we don't saturate the RSL bus
		 * doing reads.
		 */
		__delay(1000);
		smi_wr.u64 = oct_mdio_readq(p->register_base + SMI_WR_DAT);
	} while (smi_wr.s.pending && --timeout);

	if (timeout <= 0)
		return -EIO;
	return 0;
}

int cavium_mdiobus_read(struct mii_bus *bus, int phy_id, int regnum)
{
	struct cavium_mdiobus *p = bus->priv;
	union cvmx_smix_cmd smi_cmd;
	union cvmx_smix_rd_dat smi_rd;
	unsigned int op = 1; /* MDIO_CLAUSE_22_READ */
	int timeout = 1000;

	if (regnum & MII_ADDR_C45) {
		int r = cavium_mdiobus_c45_addr(p, phy_id, regnum);

		if (r < 0)
			return r;

		regnum = (regnum >> 16) & 0x1f;
		op = 3; /* MDIO_CLAUSE_45_READ */
	} else {
		cavium_mdiobus_set_mode(p, C22);
	}

	smi_cmd.u64 = 0;
	smi_cmd.s.phy_op = op;
	smi_cmd.s.phy_adr = phy_id;
	smi_cmd.s.reg_adr = regnum;
	oct_mdio_writeq(smi_cmd.u64, p->register_base + SMI_CMD);

	do {
		/* Wait 1000 clocks so we don't saturate the RSL bus
		 * doing reads.
		 */
		__delay(1000);
		smi_rd.u64 = oct_mdio_readq(p->register_base + SMI_RD_DAT);
	} while (smi_rd.s.pending && --timeout);

	if (smi_rd.s.val)
		return smi_rd.s.dat;
	else
		return -EIO;
}
EXPORT_SYMBOL(cavium_mdiobus_read);

int cavium_mdiobus_write(struct mii_bus *bus, int phy_id, int regnum, u16 val)
{
	struct cavium_mdiobus *p = bus->priv;
	union cvmx_smix_cmd smi_cmd;
	union cvmx_smix_wr_dat smi_wr;
	unsigned int op = 0; /* MDIO_CLAUSE_22_WRITE */
	int timeout = 1000;

	if (regnum & MII_ADDR_C45) {
		int r = cavium_mdiobus_c45_addr(p, phy_id, regnum);

		if (r < 0)
			return r;

		regnum = (regnum >> 16) & 0x1f;
		op = 1; /* MDIO_CLAUSE_45_WRITE */
	} else {
		cavium_mdiobus_set_mode(p, C22);
	}

	smi_wr.u64 = 0;
	smi_wr.s.dat = val;
	oct_mdio_writeq(smi_wr.u64, p->register_base + SMI_WR_DAT);

	smi_cmd.u64 = 0;
	smi_cmd.s.phy_op = op;
	smi_cmd.s.phy_adr = phy_id;
	smi_cmd.s.reg_adr = regnum;
	oct_mdio_writeq(smi_cmd.u64, p->register_base + SMI_CMD);

	do {
		/* Wait 1000 clocks so we don't saturate the RSL bus
		 * doing reads.
		 */
		__delay(1000);
		smi_wr.u64 = oct_mdio_readq(p->register_base + SMI_WR_DAT);
	} while (smi_wr.s.pending && --timeout);

	if (timeout <= 0)
		return -EIO;

	return 0;
}
EXPORT_SYMBOL(cavium_mdiobus_write);

MODULE_DESCRIPTION("Common code for OCTEON and Thunder MDIO bus drivers");
MODULE_AUTHOR("David Daney");
MODULE_LICENSE("GPL");
