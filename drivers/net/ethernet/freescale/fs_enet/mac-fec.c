/*
 * Freescale Ethernet controllers
 *
 * Copyright (c) 2005 Intracom S.A.
 *  by Pantelis Antoniou <panto@intracom.gr>
 *
 * 2005 (c) MontaVista Software, Inc.
 * Vitaly Bordug <vbordug@ru.mvista.com>
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <linux/mii.h>
#include <linux/ethtool.h>
#include <linux/bitops.h>
#include <linux/fs.h>
#include <linux/platform_device.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/gfp.h>

#include <asm/irq.h>
#include <asm/uaccess.h>

#ifdef CONFIG_8xx
#include <asm/8xx_immap.h>
#include <asm/pgtable.h>
#include <asm/mpc8xx.h>
#include <asm/cpm1.h>
#endif

#include "fs_enet.h"
#include "fec.h"

/*************************************************/

#if defined(CONFIG_CPM1)
/* for a CPM1 __raw_xxx's are sufficient */
#define __fs_out32(addr, x)	__raw_writel(x, addr)
#define __fs_out16(addr, x)	__raw_writew(x, addr)
#define __fs_in32(addr)	__raw_readl(addr)
#define __fs_in16(addr)	__raw_readw(addr)
#else
/* for others play it safe */
#define __fs_out32(addr, x)	out_be32(addr, x)
#define __fs_out16(addr, x)	out_be16(addr, x)
#define __fs_in32(addr)	in_be32(addr)
#define __fs_in16(addr)	in_be16(addr)
#endif

/* write */
#define FW(_fecp, _reg, _v) __fs_out32(&(_fecp)->fec_ ## _reg, (_v))

/* read */
#define FR(_fecp, _reg)	__fs_in32(&(_fecp)->fec_ ## _reg)

/* set bits */
#define FS(_fecp, _reg, _v) FW(_fecp, _reg, FR(_fecp, _reg) | (_v))

/* clear bits */
#define FC(_fecp, _reg, _v) FW(_fecp, _reg, FR(_fecp, _reg) & ~(_v))

/*
 * Delay to wait for FEC reset command to complete (in us)
 */
#define FEC_RESET_DELAY		50

static int whack_reset(struct fec __iomem *fecp)
{
	int i;

	FW(fecp, ecntrl, FEC_ECNTRL_PINMUX | FEC_ECNTRL_RESET);
	for (i = 0; i < FEC_RESET_DELAY; i++) {
		if ((FR(fecp, ecntrl) & FEC_ECNTRL_RESET) == 0)
			return 0;	/* OK */
		udelay(1);
	}

	return -1;
}

static int do_pd_setup(struct fs_enet_private *fep)
{
	struct platform_device *ofdev = to_platform_device(fep->dev);

	fep->interrupt = irq_of_parse_and_map(ofdev->dev.of_node, 0);
	if (fep->interrupt == NO_IRQ)
		return -EINVAL;

	fep->fec.fecp = of_iomap(ofdev->dev.of_node, 0);
	if (!fep->fcc.fccp)
		return -EINVAL;

	return 0;
}

#define FEC_NAPI_RX_EVENT_MSK	(FEC_ENET_RXF | FEC_ENET_RXB)
#define FEC_RX_EVENT		(FEC_ENET_RXF)
#define FEC_TX_EVENT		(FEC_ENET_TXF)
#define FEC_ERR_EVENT_MSK	(FEC_ENET_HBERR | FEC_ENET_BABR | \
				 FEC_ENET_BABT | FEC_ENET_EBERR)

static int setup_data(struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);

	if (do_pd_setup(fep) != 0)
		return -EINVAL;

	fep->fec.hthi = 0;
	fep->fec.htlo = 0;

	fep->ev_napi_rx = FEC_NAPI_RX_EVENT_MSK;
	fep->ev_rx = FEC_RX_EVENT;
	fep->ev_tx = FEC_TX_EVENT;
	fep->ev_err = FEC_ERR_EVENT_MSK;

	return 0;
}

static int allocate_bd(struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	const struct fs_platform_info *fpi = fep->fpi;

	fep->ring_base = (void __force __iomem *)dma_alloc_coherent(fep->dev,
					    (fpi->tx_ring + fpi->rx_ring) *
					    sizeof(cbd_t), &fep->ring_mem_addr,
					    GFP_KERNEL);
	if (fep->ring_base == NULL)
		return -ENOMEM;

	return 0;
}

static void free_bd(struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	const struct fs_platform_info *fpi = fep->fpi;

	if(fep->ring_base)
		dma_free_coherent(fep->dev, (fpi->tx_ring + fpi->rx_ring)
					* sizeof(cbd_t),
					(void __force *)fep->ring_base,
					fep->ring_mem_addr);
}

static void cleanup_data(struct net_device *dev)
{
	/* nothing */
}

static void set_promiscuous_mode(struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	struct fec __iomem *fecp = fep->fec.fecp;

	FS(fecp, r_cntrl, FEC_RCNTRL_PROM);
}

static void set_multicast_start(struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);

	fep->fec.hthi = 0;
	fep->fec.htlo = 0;
}

static void set_multicast_one(struct net_device *dev, const u8 *mac)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	int temp, hash_index, i, j;
	u32 crc, csrVal;
	u8 byte, msb;

	crc = 0xffffffff;
	for (i = 0; i < 6; i++) {
		byte = mac[i];
		for (j = 0; j < 8; j++) {
			msb = crc >> 31;
			crc <<= 1;
			if (msb ^ (byte & 0x1))
				crc ^= FEC_CRC_POLY;
			byte >>= 1;
		}
	}

	temp = (crc & 0x3f) >> 1;
	hash_index = ((temp & 0x01) << 4) |
		     ((temp & 0x02) << 2) |
		     ((temp & 0x04)) |
		     ((temp & 0x08) >> 2) |
		     ((temp & 0x10) >> 4);
	csrVal = 1 << hash_index;
	if (crc & 1)
		fep->fec.hthi |= csrVal;
	else
		fep->fec.htlo |= csrVal;
}

static void set_multicast_finish(struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	struct fec __iomem *fecp = fep->fec.fecp;

	/* if all multi or too many multicasts; just enable all */
	if ((dev->flags & IFF_ALLMULTI) != 0 ||
	    netdev_mc_count(dev) > FEC_MAX_MULTICAST_ADDRS) {
		fep->fec.hthi = 0xffffffffU;
		fep->fec.htlo = 0xffffffffU;
	}

	FC(fecp, r_cntrl, FEC_RCNTRL_PROM);
	FW(fecp, grp_hash_table_high, fep->fec.hthi);
	FW(fecp, grp_hash_table_low, fep->fec.htlo);
}

static void set_multicast_list(struct net_device *dev)
{
	struct netdev_hw_addr *ha;

	if ((dev->flags & IFF_PROMISC) == 0) {
		set_multicast_start(dev);
		netdev_for_each_mc_addr(ha, dev)
			set_multicast_one(dev, ha->addr);
		set_multicast_finish(dev);
	} else
		set_promiscuous_mode(dev);
}

static void restart(struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	struct fec __iomem *fecp = fep->fec.fecp;
	const struct fs_platform_info *fpi = fep->fpi;
	dma_addr_t rx_bd_base_phys, tx_bd_base_phys;
	int r;
	u32 addrhi, addrlo;

	struct mii_bus* mii = fep->phydev->bus;
	struct fec_info* fec_inf = mii->priv;

	r = whack_reset(fep->fec.fecp);
	if (r != 0)
		dev_err(fep->dev, "FEC Reset FAILED!\n");
	/*
	 * Set station address.
	 */
	addrhi = ((u32) dev->dev_addr[0] << 24) |
		 ((u32) dev->dev_addr[1] << 16) |
		 ((u32) dev->dev_addr[2] <<  8) |
		  (u32) dev->dev_addr[3];
	addrlo = ((u32) dev->dev_addr[4] << 24) |
		 ((u32) dev->dev_addr[5] << 16);
	FW(fecp, addr_low, addrhi);
	FW(fecp, addr_high, addrlo);

	/*
	 * Reset all multicast.
	 */
	FW(fecp, grp_hash_table_high, fep->fec.hthi);
	FW(fecp, grp_hash_table_low, fep->fec.htlo);

	/*
	 * Set maximum receive buffer size.
	 */
	FW(fecp, r_buff_size, PKT_MAXBLR_SIZE);
#ifdef CONFIG_FS_ENET_MPC5121_FEC
	FW(fecp, r_cntrl, PKT_MAXBUF_SIZE << 16);
#else
	FW(fecp, r_hash, PKT_MAXBUF_SIZE);
#endif

	/* get physical address */
	rx_bd_base_phys = fep->ring_mem_addr;
	tx_bd_base_phys = rx_bd_base_phys + sizeof(cbd_t) * fpi->rx_ring;

	/*
	 * Set receive and transmit descriptor base.
	 */
	FW(fecp, r_des_start, rx_bd_base_phys);
	FW(fecp, x_des_start, tx_bd_base_phys);

	fs_init_bds(dev);

	/*
	 * Enable big endian and don't care about SDMA FC.
	 */
#ifdef CONFIG_FS_ENET_MPC5121_FEC
	FS(fecp, dma_control, 0xC0000000);
#else
	FW(fecp, fun_code, 0x78000000);
#endif

	/*
	 * Set MII speed.
	 */
	FW(fecp, mii_speed, fec_inf->mii_speed);

	/*
	 * Clear any outstanding interrupt.
	 */
	FW(fecp, ievent, 0xffc0);
#ifndef CONFIG_FS_ENET_MPC5121_FEC
	FW(fecp, ivec, (virq_to_hw(fep->interrupt) / 2) << 29);

	FW(fecp, r_cntrl, FEC_RCNTRL_MII_MODE);	/* MII enable */
#else
	/*
	 * Only set MII/RMII mode - do not touch maximum frame length
	 * configured before.
	 */
	FS(fecp, r_cntrl, fpi->use_rmii ?
			FEC_RCNTRL_RMII_MODE : FEC_RCNTRL_MII_MODE);
#endif
	/*
	 * adjust to duplex mode
	 */
	if (fep->phydev->duplex) {
		FC(fecp, r_cntrl, FEC_RCNTRL_DRT);
		FS(fecp, x_cntrl, FEC_TCNTRL_FDEN);	/* FD enable */
	} else {
		FS(fecp, r_cntrl, FEC_RCNTRL_DRT);
		FC(fecp, x_cntrl, FEC_TCNTRL_FDEN);	/* FD disable */
	}

	/*
	 * Enable interrupts we wish to service.
	 */
	FW(fecp, imask, FEC_ENET_TXF | FEC_ENET_TXB |
	   FEC_ENET_RXF | FEC_ENET_RXB);

	/*
	 * And last, enable the transmit and receive processing.
	 */
	FW(fecp, ecntrl, FEC_ECNTRL_PINMUX | FEC_ECNTRL_ETHER_EN);
	FW(fecp, r_des_active, 0x01000000);
}

static void stop(struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	const struct fs_platform_info *fpi = fep->fpi;
	struct fec __iomem *fecp = fep->fec.fecp;

	struct fec_info* feci= fep->phydev->bus->priv;

	int i;

	if ((FR(fecp, ecntrl) & FEC_ECNTRL_ETHER_EN) == 0)
		return;		/* already down */

	FW(fecp, x_cntrl, 0x01);	/* Graceful transmit stop */
	for (i = 0; ((FR(fecp, ievent) & 0x10000000) == 0) &&
	     i < FEC_RESET_DELAY; i++)
		udelay(1);

	if (i == FEC_RESET_DELAY)
		dev_warn(fep->dev, "FEC timeout on graceful transmit stop\n");
	/*
	 * Disable FEC. Let only MII interrupts.
	 */
	FW(fecp, imask, 0);
	FC(fecp, ecntrl, FEC_ECNTRL_ETHER_EN);

	fs_cleanup_bds(dev);

	/* shut down FEC1? that's where the mii bus is */
	if (fpi->has_phy) {
		FS(fecp, r_cntrl, fpi->use_rmii ?
				FEC_RCNTRL_RMII_MODE :
				FEC_RCNTRL_MII_MODE);	/* MII/RMII enable */
		FS(fecp, ecntrl, FEC_ECNTRL_PINMUX | FEC_ECNTRL_ETHER_EN);
		FW(fecp, ievent, FEC_ENET_MII);
		FW(fecp, mii_speed, feci->mii_speed);
	}
}

static void napi_clear_rx_event(struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	struct fec __iomem *fecp = fep->fec.fecp;

	FW(fecp, ievent, FEC_NAPI_RX_EVENT_MSK);
}

static void napi_enable_rx(struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	struct fec __iomem *fecp = fep->fec.fecp;

	FS(fecp, imask, FEC_NAPI_RX_EVENT_MSK);
}

static void napi_disable_rx(struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	struct fec __iomem *fecp = fep->fec.fecp;

	FC(fecp, imask, FEC_NAPI_RX_EVENT_MSK);
}

static void rx_bd_done(struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	struct fec __iomem *fecp = fep->fec.fecp;

	FW(fecp, r_des_active, 0x01000000);
}

static void tx_kickstart(struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	struct fec __iomem *fecp = fep->fec.fecp;

	FW(fecp, x_des_active, 0x01000000);
}

static u32 get_int_events(struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	struct fec __iomem *fecp = fep->fec.fecp;

	return FR(fecp, ievent) & FR(fecp, imask);
}

static void clear_int_events(struct net_device *dev, u32 int_events)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	struct fec __iomem *fecp = fep->fec.fecp;

	FW(fecp, ievent, int_events);
}

static void ev_error(struct net_device *dev, u32 int_events)
{
	struct fs_enet_private *fep = netdev_priv(dev);

	dev_warn(fep->dev, "FEC ERROR(s) 0x%x\n", int_events);
}

static int get_regs(struct net_device *dev, void *p, int *sizep)
{
	struct fs_enet_private *fep = netdev_priv(dev);

	if (*sizep < sizeof(struct fec))
		return -EINVAL;

	memcpy_fromio(p, fep->fec.fecp, sizeof(struct fec));

	return 0;
}

static int get_regs_len(struct net_device *dev)
{
	return sizeof(struct fec);
}

static void tx_restart(struct net_device *dev)
{
	/* nothing */
}

/*************************************************************************/

const struct fs_ops fs_fec_ops = {
	.setup_data		= setup_data,
	.cleanup_data		= cleanup_data,
	.set_multicast_list	= set_multicast_list,
	.restart		= restart,
	.stop			= stop,
	.napi_clear_rx_event	= napi_clear_rx_event,
	.napi_enable_rx		= napi_enable_rx,
	.napi_disable_rx	= napi_disable_rx,
	.rx_bd_done		= rx_bd_done,
	.tx_kickstart		= tx_kickstart,
	.get_int_events		= get_int_events,
	.clear_int_events	= clear_int_events,
	.ev_error		= ev_error,
	.get_regs		= get_regs,
	.get_regs_len		= get_regs_len,
	.tx_restart		= tx_restart,
	.allocate_bd		= allocate_bd,
	.free_bd		= free_bd,
};

