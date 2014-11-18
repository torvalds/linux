/*
 * Ethernet on Serial Communications Controller (SCC) driver for Motorola MPC8xx and MPC82xx.
 *
 * Copyright (c) 2003 Intracom S.A.
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
#include <linux/of_irq.h>
#include <linux/of_platform.h>

#include <asm/irq.h>
#include <asm/uaccess.h>

#ifdef CONFIG_8xx
#include <asm/8xx_immap.h>
#include <asm/pgtable.h>
#include <asm/cpm1.h>
#endif

#include "fs_enet.h"

/*************************************************/
#if defined(CONFIG_CPM1)
/* for a 8xx __raw_xxx's are sufficient */
#define __fs_out32(addr, x)	__raw_writel(x, addr)
#define __fs_out16(addr, x)	__raw_writew(x, addr)
#define __fs_out8(addr, x)	__raw_writeb(x, addr)
#define __fs_in32(addr)	__raw_readl(addr)
#define __fs_in16(addr)	__raw_readw(addr)
#define __fs_in8(addr)	__raw_readb(addr)
#else
/* for others play it safe */
#define __fs_out32(addr, x)	out_be32(addr, x)
#define __fs_out16(addr, x)	out_be16(addr, x)
#define __fs_in32(addr)	in_be32(addr)
#define __fs_in16(addr)	in_be16(addr)
#define __fs_out8(addr, x)	out_8(addr, x)
#define __fs_in8(addr)	in_8(addr)
#endif

/* write, read, set bits, clear bits */
#define W32(_p, _m, _v) __fs_out32(&(_p)->_m, (_v))
#define R32(_p, _m)     __fs_in32(&(_p)->_m)
#define S32(_p, _m, _v) W32(_p, _m, R32(_p, _m) | (_v))
#define C32(_p, _m, _v) W32(_p, _m, R32(_p, _m) & ~(_v))

#define W16(_p, _m, _v) __fs_out16(&(_p)->_m, (_v))
#define R16(_p, _m)     __fs_in16(&(_p)->_m)
#define S16(_p, _m, _v) W16(_p, _m, R16(_p, _m) | (_v))
#define C16(_p, _m, _v) W16(_p, _m, R16(_p, _m) & ~(_v))

#define W8(_p, _m, _v)  __fs_out8(&(_p)->_m, (_v))
#define R8(_p, _m)      __fs_in8(&(_p)->_m)
#define S8(_p, _m, _v)  W8(_p, _m, R8(_p, _m) | (_v))
#define C8(_p, _m, _v)  W8(_p, _m, R8(_p, _m) & ~(_v))

#define SCC_MAX_MULTICAST_ADDRS	64

/*
 * Delay to wait for SCC reset command to complete (in us)
 */
#define SCC_RESET_DELAY		50

static inline int scc_cr_cmd(struct fs_enet_private *fep, u32 op)
{
	const struct fs_platform_info *fpi = fep->fpi;

	return cpm_command(fpi->cp_command, op);
}

static int do_pd_setup(struct fs_enet_private *fep)
{
	struct platform_device *ofdev = to_platform_device(fep->dev);

	fep->interrupt = irq_of_parse_and_map(ofdev->dev.of_node, 0);
	if (fep->interrupt == NO_IRQ)
		return -EINVAL;

	fep->scc.sccp = of_iomap(ofdev->dev.of_node, 0);
	if (!fep->scc.sccp)
		return -EINVAL;

	fep->scc.ep = of_iomap(ofdev->dev.of_node, 1);
	if (!fep->scc.ep) {
		iounmap(fep->scc.sccp);
		return -EINVAL;
	}

	return 0;
}

#define SCC_NAPI_RX_EVENT_MSK	(SCCE_ENET_RXF | SCCE_ENET_RXB)
#define SCC_NAPI_TX_EVENT_MSK	(SCCE_ENET_TXB)
#define SCC_RX_EVENT		(SCCE_ENET_RXF)
#define SCC_TX_EVENT		(SCCE_ENET_TXB)
#define SCC_ERR_EVENT_MSK	(SCCE_ENET_TXE | SCCE_ENET_BSY)

static int setup_data(struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);

	do_pd_setup(fep);

	fep->scc.hthi = 0;
	fep->scc.htlo = 0;

	fep->ev_napi_rx = SCC_NAPI_RX_EVENT_MSK;
	fep->ev_napi_tx = SCC_NAPI_TX_EVENT_MSK;
	fep->ev_rx = SCC_RX_EVENT;
	fep->ev_tx = SCC_TX_EVENT | SCCE_ENET_TXE;
	fep->ev_err = SCC_ERR_EVENT_MSK;

	return 0;
}

static int allocate_bd(struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	const struct fs_platform_info *fpi = fep->fpi;

	fep->ring_mem_addr = cpm_dpalloc((fpi->tx_ring + fpi->rx_ring) *
					 sizeof(cbd_t), 8);
	if (IS_ERR_VALUE(fep->ring_mem_addr))
		return -ENOMEM;

	fep->ring_base = (void __iomem __force*)
		cpm_dpram_addr(fep->ring_mem_addr);

	return 0;
}

static void free_bd(struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);

	if (fep->ring_base)
		cpm_dpfree(fep->ring_mem_addr);
}

static void cleanup_data(struct net_device *dev)
{
	/* nothing */
}

static void set_promiscuous_mode(struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	scc_t __iomem *sccp = fep->scc.sccp;

	S16(sccp, scc_psmr, SCC_PSMR_PRO);
}

static void set_multicast_start(struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	scc_enet_t __iomem *ep = fep->scc.ep;

	W16(ep, sen_gaddr1, 0);
	W16(ep, sen_gaddr2, 0);
	W16(ep, sen_gaddr3, 0);
	W16(ep, sen_gaddr4, 0);
}

static void set_multicast_one(struct net_device *dev, const u8 * mac)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	scc_enet_t __iomem *ep = fep->scc.ep;
	u16 taddrh, taddrm, taddrl;

	taddrh = ((u16) mac[5] << 8) | mac[4];
	taddrm = ((u16) mac[3] << 8) | mac[2];
	taddrl = ((u16) mac[1] << 8) | mac[0];

	W16(ep, sen_taddrh, taddrh);
	W16(ep, sen_taddrm, taddrm);
	W16(ep, sen_taddrl, taddrl);
	scc_cr_cmd(fep, CPM_CR_SET_GADDR);
}

static void set_multicast_finish(struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	scc_t __iomem *sccp = fep->scc.sccp;
	scc_enet_t __iomem *ep = fep->scc.ep;

	/* clear promiscuous always */
	C16(sccp, scc_psmr, SCC_PSMR_PRO);

	/* if all multi or too many multicasts; just enable all */
	if ((dev->flags & IFF_ALLMULTI) != 0 ||
	    netdev_mc_count(dev) > SCC_MAX_MULTICAST_ADDRS) {

		W16(ep, sen_gaddr1, 0xffff);
		W16(ep, sen_gaddr2, 0xffff);
		W16(ep, sen_gaddr3, 0xffff);
		W16(ep, sen_gaddr4, 0xffff);
	}
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

/*
 * This function is called to start or restart the FEC during a link
 * change.  This only happens when switching between half and full
 * duplex.
 */
static void restart(struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	scc_t __iomem *sccp = fep->scc.sccp;
	scc_enet_t __iomem *ep = fep->scc.ep;
	const struct fs_platform_info *fpi = fep->fpi;
	u16 paddrh, paddrm, paddrl;
	const unsigned char *mac;
	int i;

	C32(sccp, scc_gsmrl, SCC_GSMRL_ENR | SCC_GSMRL_ENT);

	/* clear everything (slow & steady does it) */
	for (i = 0; i < sizeof(*ep); i++)
		__fs_out8((u8 __iomem *)ep + i, 0);

	/* point to bds */
	W16(ep, sen_genscc.scc_rbase, fep->ring_mem_addr);
	W16(ep, sen_genscc.scc_tbase,
	    fep->ring_mem_addr + sizeof(cbd_t) * fpi->rx_ring);

	/* Initialize function code registers for big-endian.
	 */
#ifndef CONFIG_NOT_COHERENT_CACHE
	W8(ep, sen_genscc.scc_rfcr, SCC_EB | SCC_GBL);
	W8(ep, sen_genscc.scc_tfcr, SCC_EB | SCC_GBL);
#else
	W8(ep, sen_genscc.scc_rfcr, SCC_EB);
	W8(ep, sen_genscc.scc_tfcr, SCC_EB);
#endif

	/* Set maximum bytes per receive buffer.
	 * This appears to be an Ethernet frame size, not the buffer
	 * fragment size.  It must be a multiple of four.
	 */
	W16(ep, sen_genscc.scc_mrblr, 0x5f0);

	/* Set CRC preset and mask.
	 */
	W32(ep, sen_cpres, 0xffffffff);
	W32(ep, sen_cmask, 0xdebb20e3);

	W32(ep, sen_crcec, 0);	/* CRC Error counter */
	W32(ep, sen_alec, 0);	/* alignment error counter */
	W32(ep, sen_disfc, 0);	/* discard frame counter */

	W16(ep, sen_pads, 0x8888);	/* Tx short frame pad character */
	W16(ep, sen_retlim, 15);	/* Retry limit threshold */

	W16(ep, sen_maxflr, 0x5ee);	/* maximum frame length register */

	W16(ep, sen_minflr, PKT_MINBUF_SIZE);	/* minimum frame length register */

	W16(ep, sen_maxd1, 0x000005f0);	/* maximum DMA1 length */
	W16(ep, sen_maxd2, 0x000005f0);	/* maximum DMA2 length */

	/* Clear hash tables.
	 */
	W16(ep, sen_gaddr1, 0);
	W16(ep, sen_gaddr2, 0);
	W16(ep, sen_gaddr3, 0);
	W16(ep, sen_gaddr4, 0);
	W16(ep, sen_iaddr1, 0);
	W16(ep, sen_iaddr2, 0);
	W16(ep, sen_iaddr3, 0);
	W16(ep, sen_iaddr4, 0);

	/* set address
	 */
	mac = dev->dev_addr;
	paddrh = ((u16) mac[5] << 8) | mac[4];
	paddrm = ((u16) mac[3] << 8) | mac[2];
	paddrl = ((u16) mac[1] << 8) | mac[0];

	W16(ep, sen_paddrh, paddrh);
	W16(ep, sen_paddrm, paddrm);
	W16(ep, sen_paddrl, paddrl);

	W16(ep, sen_pper, 0);
	W16(ep, sen_taddrl, 0);
	W16(ep, sen_taddrm, 0);
	W16(ep, sen_taddrh, 0);

	fs_init_bds(dev);

	scc_cr_cmd(fep, CPM_CR_INIT_TRX);

	W16(sccp, scc_scce, 0xffff);

	/* Enable interrupts we wish to service.
	 */
	W16(sccp, scc_sccm, SCCE_ENET_TXE | SCCE_ENET_RXF | SCCE_ENET_TXB);

	/* Set GSMR_H to enable all normal operating modes.
	 * Set GSMR_L to enable Ethernet to MC68160.
	 */
	W32(sccp, scc_gsmrh, 0);
	W32(sccp, scc_gsmrl,
	    SCC_GSMRL_TCI | SCC_GSMRL_TPL_48 | SCC_GSMRL_TPP_10 |
	    SCC_GSMRL_MODE_ENET);

	/* Set sync/delimiters.
	 */
	W16(sccp, scc_dsr, 0xd555);

	/* Set processing mode.  Use Ethernet CRC, catch broadcast, and
	 * start frame search 22 bit times after RENA.
	 */
	W16(sccp, scc_psmr, SCC_PSMR_ENCRC | SCC_PSMR_NIB22);

	/* Set full duplex mode if needed */
	if (fep->phydev->duplex)
		S16(sccp, scc_psmr, SCC_PSMR_LPB | SCC_PSMR_FDE);

	S32(sccp, scc_gsmrl, SCC_GSMRL_ENR | SCC_GSMRL_ENT);
}

static void stop(struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	scc_t __iomem *sccp = fep->scc.sccp;
	int i;

	for (i = 0; (R16(sccp, scc_sccm) == 0) && i < SCC_RESET_DELAY; i++)
		udelay(1);

	if (i == SCC_RESET_DELAY)
		dev_warn(fep->dev, "SCC timeout on graceful transmit stop\n");

	W16(sccp, scc_sccm, 0);
	C32(sccp, scc_gsmrl, SCC_GSMRL_ENR | SCC_GSMRL_ENT);

	fs_cleanup_bds(dev);
}

static void napi_clear_rx_event(struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	scc_t __iomem *sccp = fep->scc.sccp;

	W16(sccp, scc_scce, SCC_NAPI_RX_EVENT_MSK);
}

static void napi_enable_rx(struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	scc_t __iomem *sccp = fep->scc.sccp;

	S16(sccp, scc_sccm, SCC_NAPI_RX_EVENT_MSK);
}

static void napi_disable_rx(struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	scc_t __iomem *sccp = fep->scc.sccp;

	C16(sccp, scc_sccm, SCC_NAPI_RX_EVENT_MSK);
}

static void napi_clear_tx_event(struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	scc_t __iomem *sccp = fep->scc.sccp;

	W16(sccp, scc_scce, SCC_NAPI_TX_EVENT_MSK);
}

static void napi_enable_tx(struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	scc_t __iomem *sccp = fep->scc.sccp;

	S16(sccp, scc_sccm, SCC_NAPI_TX_EVENT_MSK);
}

static void napi_disable_tx(struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	scc_t __iomem *sccp = fep->scc.sccp;

	C16(sccp, scc_sccm, SCC_NAPI_TX_EVENT_MSK);
}

static void rx_bd_done(struct net_device *dev)
{
	/* nothing */
}

static void tx_kickstart(struct net_device *dev)
{
	/* nothing */
}

static u32 get_int_events(struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	scc_t __iomem *sccp = fep->scc.sccp;

	return (u32) R16(sccp, scc_scce);
}

static void clear_int_events(struct net_device *dev, u32 int_events)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	scc_t __iomem *sccp = fep->scc.sccp;

	W16(sccp, scc_scce, int_events & 0xffff);
}

static void ev_error(struct net_device *dev, u32 int_events)
{
	struct fs_enet_private *fep = netdev_priv(dev);

	dev_warn(fep->dev, "SCC ERROR(s) 0x%x\n", int_events);
}

static int get_regs(struct net_device *dev, void *p, int *sizep)
{
	struct fs_enet_private *fep = netdev_priv(dev);

	if (*sizep < sizeof(scc_t) + sizeof(scc_enet_t __iomem *))
		return -EINVAL;

	memcpy_fromio(p, fep->scc.sccp, sizeof(scc_t));
	p = (char *)p + sizeof(scc_t);

	memcpy_fromio(p, fep->scc.ep, sizeof(scc_enet_t __iomem *));

	return 0;
}

static int get_regs_len(struct net_device *dev)
{
	return sizeof(scc_t) + sizeof(scc_enet_t __iomem *);
}

static void tx_restart(struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);

	scc_cr_cmd(fep, CPM_CR_RESTART_TX);
}



/*************************************************************************/

const struct fs_ops fs_scc_ops = {
	.setup_data		= setup_data,
	.cleanup_data		= cleanup_data,
	.set_multicast_list	= set_multicast_list,
	.restart		= restart,
	.stop			= stop,
	.napi_clear_rx_event	= napi_clear_rx_event,
	.napi_enable_rx		= napi_enable_rx,
	.napi_disable_rx	= napi_disable_rx,
	.napi_clear_tx_event	= napi_clear_tx_event,
	.napi_enable_tx		= napi_enable_tx,
	.napi_disable_tx	= napi_disable_tx,
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
