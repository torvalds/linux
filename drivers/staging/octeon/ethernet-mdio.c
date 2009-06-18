/**********************************************************************
 * Author: Cavium Networks
 *
 * Contact: support@caviumnetworks.com
 * This file is part of the OCTEON SDK
 *
 * Copyright (c) 2003-2007 Cavium Networks
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, Version 2, as
 * published by the Free Software Foundation.
 *
 * This file is distributed in the hope that it will be useful, but
 * AS-IS and WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE, TITLE, or
 * NONINFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this file; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 * or visit http://www.gnu.org/licenses/.
 *
 * This file may also be available under a different license from Cavium.
 * Contact Cavium Networks for more information
**********************************************************************/
#include <linux/kernel.h>
#include <linux/ethtool.h>
#include <linux/mii.h>
#include <net/dst.h>

#include <asm/octeon/octeon.h>

#include "ethernet-defines.h"
#include "octeon-ethernet.h"
#include "ethernet-mdio.h"

#include "cvmx-helper-board.h"

#include "cvmx-smix-defs.h"

DECLARE_MUTEX(mdio_sem);

/**
 * Perform an MII read. Called by the generic MII routines
 *
 * @dev:      Device to perform read for
 * @phy_id:   The MII phy id
 * @location: Register location to read
 * Returns Result from the read or zero on failure
 */
static int cvm_oct_mdio_read(struct net_device *dev, int phy_id, int location)
{
	union cvmx_smix_cmd smi_cmd;
	union cvmx_smix_rd_dat smi_rd;

	smi_cmd.u64 = 0;
	smi_cmd.s.phy_op = 1;
	smi_cmd.s.phy_adr = phy_id;
	smi_cmd.s.reg_adr = location;
	cvmx_write_csr(CVMX_SMIX_CMD(0), smi_cmd.u64);

	do {
		if (!in_interrupt())
			yield();
		smi_rd.u64 = cvmx_read_csr(CVMX_SMIX_RD_DAT(0));
	} while (smi_rd.s.pending);

	if (smi_rd.s.val)
		return smi_rd.s.dat;
	else
		return 0;
}

static int cvm_oct_mdio_dummy_read(struct net_device *dev, int phy_id,
				   int location)
{
	return 0xffff;
}

/**
 * Perform an MII write. Called by the generic MII routines
 *
 * @dev:      Device to perform write for
 * @phy_id:   The MII phy id
 * @location: Register location to write
 * @val:      Value to write
 */
static void cvm_oct_mdio_write(struct net_device *dev, int phy_id, int location,
			       int val)
{
	union cvmx_smix_cmd smi_cmd;
	union cvmx_smix_wr_dat smi_wr;

	smi_wr.u64 = 0;
	smi_wr.s.dat = val;
	cvmx_write_csr(CVMX_SMIX_WR_DAT(0), smi_wr.u64);

	smi_cmd.u64 = 0;
	smi_cmd.s.phy_op = 0;
	smi_cmd.s.phy_adr = phy_id;
	smi_cmd.s.reg_adr = location;
	cvmx_write_csr(CVMX_SMIX_CMD(0), smi_cmd.u64);

	do {
		if (!in_interrupt())
			yield();
		smi_wr.u64 = cvmx_read_csr(CVMX_SMIX_WR_DAT(0));
	} while (smi_wr.s.pending);
}

static void cvm_oct_mdio_dummy_write(struct net_device *dev, int phy_id,
				     int location, int val)
{
}

static void cvm_oct_get_drvinfo(struct net_device *dev,
				struct ethtool_drvinfo *info)
{
	strcpy(info->driver, "cavium-ethernet");
	strcpy(info->version, OCTEON_ETHERNET_VERSION);
	strcpy(info->bus_info, "Builtin");
}

static int cvm_oct_get_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct octeon_ethernet *priv = netdev_priv(dev);
	int ret;

	down(&mdio_sem);
	ret = mii_ethtool_gset(&priv->mii_info, cmd);
	up(&mdio_sem);

	return ret;
}

static int cvm_oct_set_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct octeon_ethernet *priv = netdev_priv(dev);
	int ret;

	down(&mdio_sem);
	ret = mii_ethtool_sset(&priv->mii_info, cmd);
	up(&mdio_sem);

	return ret;
}

static int cvm_oct_nway_reset(struct net_device *dev)
{
	struct octeon_ethernet *priv = netdev_priv(dev);
	int ret;

	down(&mdio_sem);
	ret = mii_nway_restart(&priv->mii_info);
	up(&mdio_sem);

	return ret;
}

static u32 cvm_oct_get_link(struct net_device *dev)
{
	struct octeon_ethernet *priv = netdev_priv(dev);
	u32 ret;

	down(&mdio_sem);
	ret = mii_link_ok(&priv->mii_info);
	up(&mdio_sem);

	return ret;
}

struct ethtool_ops cvm_oct_ethtool_ops = {
	.get_drvinfo = cvm_oct_get_drvinfo,
	.get_settings = cvm_oct_get_settings,
	.set_settings = cvm_oct_set_settings,
	.nway_reset = cvm_oct_nway_reset,
	.get_link = cvm_oct_get_link,
	.get_sg = ethtool_op_get_sg,
	.get_tx_csum = ethtool_op_get_tx_csum,
};

/**
 * IOCTL support for PHY control
 *
 * @dev:    Device to change
 * @rq:     the request
 * @cmd:    the command
 * Returns Zero on success
 */
int cvm_oct_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct octeon_ethernet *priv = netdev_priv(dev);
	struct mii_ioctl_data *data = if_mii(rq);
	unsigned int duplex_chg;
	int ret;

	down(&mdio_sem);
	ret = generic_mii_ioctl(&priv->mii_info, data, cmd, &duplex_chg);
	up(&mdio_sem);

	return ret;
}

/**
 * Setup the MDIO device structures
 *
 * @dev:    Device to setup
 *
 * Returns Zero on success, negative on failure
 */
int cvm_oct_mdio_setup_device(struct net_device *dev)
{
	struct octeon_ethernet *priv = netdev_priv(dev);
	int phy_id = cvmx_helper_board_get_mii_address(priv->port);
	if (phy_id != -1) {
		priv->mii_info.dev = dev;
		priv->mii_info.phy_id = phy_id;
		priv->mii_info.phy_id_mask = 0xff;
		priv->mii_info.supports_gmii = 1;
		priv->mii_info.reg_num_mask = 0x1f;
		priv->mii_info.mdio_read = cvm_oct_mdio_read;
		priv->mii_info.mdio_write = cvm_oct_mdio_write;
	} else {
		/* Supply dummy MDIO routines so the kernel won't crash
		   if the user tries to read them */
		priv->mii_info.mdio_read = cvm_oct_mdio_dummy_read;
		priv->mii_info.mdio_write = cvm_oct_mdio_dummy_write;
	}
	return 0;
}
