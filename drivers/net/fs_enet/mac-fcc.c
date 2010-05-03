/*
 * FCC driver for Motorola MPC82xx (PQ2).
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
#include <linux/phy.h>
#include <linux/of_device.h>
#include <linux/gfp.h>

#include <asm/immap_cpm2.h>
#include <asm/mpc8260.h>
#include <asm/cpm2.h>

#include <asm/pgtable.h>
#include <asm/irq.h>
#include <asm/uaccess.h>

#include "fs_enet.h"

/*************************************************/

/* FCC access macros */

/* write, read, set bits, clear bits */
#define W32(_p, _m, _v)	out_be32(&(_p)->_m, (_v))
#define R32(_p, _m)	in_be32(&(_p)->_m)
#define S32(_p, _m, _v)	W32(_p, _m, R32(_p, _m) | (_v))
#define C32(_p, _m, _v)	W32(_p, _m, R32(_p, _m) & ~(_v))

#define W16(_p, _m, _v)	out_be16(&(_p)->_m, (_v))
#define R16(_p, _m)	in_be16(&(_p)->_m)
#define S16(_p, _m, _v)	W16(_p, _m, R16(_p, _m) | (_v))
#define C16(_p, _m, _v)	W16(_p, _m, R16(_p, _m) & ~(_v))

#define W8(_p, _m, _v)	out_8(&(_p)->_m, (_v))
#define R8(_p, _m)	in_8(&(_p)->_m)
#define S8(_p, _m, _v)	W8(_p, _m, R8(_p, _m) | (_v))
#define C8(_p, _m, _v)	W8(_p, _m, R8(_p, _m) & ~(_v))

/*************************************************/

#define FCC_MAX_MULTICAST_ADDRS	64

#define mk_mii_read(REG)	(0x60020000 | ((REG & 0x1f) << 18))
#define mk_mii_write(REG, VAL)	(0x50020000 | ((REG & 0x1f) << 18) | (VAL & 0xffff))
#define mk_mii_end		0

#define MAX_CR_CMD_LOOPS	10000

static inline int fcc_cr_cmd(struct fs_enet_private *fep, u32 op)
{
	const struct fs_platform_info *fpi = fep->fpi;

	return cpm_command(fpi->cp_command, op);
}

static int do_pd_setup(struct fs_enet_private *fep)
{
	struct of_device *ofdev = to_of_device(fep->dev);
	struct fs_platform_info *fpi = fep->fpi;
	int ret = -EINVAL;

	fep->interrupt = of_irq_to_resource(ofdev->node, 0, NULL);
	if (fep->interrupt == NO_IRQ)
		goto out;

	fep->fcc.fccp = of_iomap(ofdev->node, 0);
	if (!fep->fcc.fccp)
		goto out;

	fep->fcc.ep = of_iomap(ofdev->node, 1);
	if (!fep->fcc.ep)
		goto out_fccp;

	fep->fcc.fcccp = of_iomap(ofdev->node, 2);
	if (!fep->fcc.fcccp)
		goto out_ep;

	fep->fcc.mem = (void __iomem *)cpm2_immr;
	fpi->dpram_offset = cpm_dpalloc(128, 8);
	if (IS_ERR_VALUE(fpi->dpram_offset)) {
		ret = fpi->dpram_offset;
		goto out_fcccp;
	}

	return 0;

out_fcccp:
	iounmap(fep->fcc.fcccp);
out_ep:
	iounmap(fep->fcc.ep);
out_fccp:
	iounmap(fep->fcc.fccp);
out:
	return ret;
}

#define FCC_NAPI_RX_EVENT_MSK	(FCC_ENET_RXF | FCC_ENET_RXB)
#define FCC_RX_EVENT		(FCC_ENET_RXF)
#define FCC_TX_EVENT		(FCC_ENET_TXB)
#define FCC_ERR_EVENT_MSK	(FCC_ENET_TXE)

static int setup_data(struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);

	if (do_pd_setup(fep) != 0)
		return -EINVAL;

	fep->ev_napi_rx = FCC_NAPI_RX_EVENT_MSK;
	fep->ev_rx = FCC_RX_EVENT;
	fep->ev_tx = FCC_TX_EVENT;
	fep->ev_err = FCC_ERR_EVENT_MSK;

	return 0;
}

static int allocate_bd(struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	const struct fs_platform_info *fpi = fep->fpi;

	fep->ring_base = (void __iomem __force *)dma_alloc_coherent(fep->dev,
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

	if (fep->ring_base)
		dma_free_coherent(fep->dev,
			(fpi->tx_ring + fpi->rx_ring) * sizeof(cbd_t),
			(void __force *)fep->ring_base, fep->ring_mem_addr);
}

static void cleanup_data(struct net_device *dev)
{
	/* nothing */
}

static void set_promiscuous_mode(struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	fcc_t __iomem *fccp = fep->fcc.fccp;

	S32(fccp, fcc_fpsmr, FCC_PSMR_PRO);
}

static void set_multicast_start(struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	fcc_enet_t __iomem *ep = fep->fcc.ep;

	W32(ep, fen_gaddrh, 0);
	W32(ep, fen_gaddrl, 0);
}

static void set_multicast_one(struct net_device *dev, const u8 *mac)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	fcc_enet_t __iomem *ep = fep->fcc.ep;
	u16 taddrh, taddrm, taddrl;

	taddrh = ((u16)mac[5] << 8) | mac[4];
	taddrm = ((u16)mac[3] << 8) | mac[2];
	taddrl = ((u16)mac[1] << 8) | mac[0];

	W16(ep, fen_taddrh, taddrh);
	W16(ep, fen_taddrm, taddrm);
	W16(ep, fen_taddrl, taddrl);
	fcc_cr_cmd(fep, CPM_CR_SET_GADDR);
}

static void set_multicast_finish(struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	fcc_t __iomem *fccp = fep->fcc.fccp;
	fcc_enet_t __iomem *ep = fep->fcc.ep;

	/* clear promiscuous always */
	C32(fccp, fcc_fpsmr, FCC_PSMR_PRO);

	/* if all multi or too many multicasts; just enable all */
	if ((dev->flags & IFF_ALLMULTI) != 0 ||
	    netdev_mc_count(dev) > FCC_MAX_MULTICAST_ADDRS) {

		W32(ep, fen_gaddrh, 0xffffffff);
		W32(ep, fen_gaddrl, 0xffffffff);
	}

	/* read back */
	fep->fcc.gaddrh = R32(ep, fen_gaddrh);
	fep->fcc.gaddrl = R32(ep, fen_gaddrl);
}

static void set_multicast_list(struct net_device *dev)
{
	struct dev_mc_list *pmc;

	if ((dev->flags & IFF_PROMISC) == 0) {
		set_multicast_start(dev);
		netdev_for_each_mc_addr(pmc, dev)
			set_multicast_one(dev, pmc->dmi_addr);
		set_multicast_finish(dev);
	} else
		set_promiscuous_mode(dev);
}

static void restart(struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	const struct fs_platform_info *fpi = fep->fpi;
	fcc_t __iomem *fccp = fep->fcc.fccp;
	fcc_c_t __iomem *fcccp = fep->fcc.fcccp;
	fcc_enet_t __iomem *ep = fep->fcc.ep;
	dma_addr_t rx_bd_base_phys, tx_bd_base_phys;
	u16 paddrh, paddrm, paddrl;
	const unsigned char *mac;
	int i;

	C32(fccp, fcc_gfmr, FCC_GFMR_ENR | FCC_GFMR_ENT);

	/* clear everything (slow & steady does it) */
	for (i = 0; i < sizeof(*ep); i++)
		out_8((u8 __iomem *)ep + i, 0);

	/* get physical address */
	rx_bd_base_phys = fep->ring_mem_addr;
	tx_bd_base_phys = rx_bd_base_phys + sizeof(cbd_t) * fpi->rx_ring;

	/* point to bds */
	W32(ep, fen_genfcc.fcc_rbase, rx_bd_base_phys);
	W32(ep, fen_genfcc.fcc_tbase, tx_bd_base_phys);

	/* Set maximum bytes per receive buffer.
	 * It must be a multiple of 32.
	 */
	W16(ep, fen_genfcc.fcc_mrblr, PKT_MAXBLR_SIZE);

	W32(ep, fen_genfcc.fcc_rstate, (CPMFCR_GBL | CPMFCR_EB) << 24);
	W32(ep, fen_genfcc.fcc_tstate, (CPMFCR_GBL | CPMFCR_EB) << 24);

	/* Allocate space in the reserved FCC area of DPRAM for the
	 * internal buffers.  No one uses this space (yet), so we
	 * can do this.  Later, we will add resource management for
	 * this area.
	 */

	W16(ep, fen_genfcc.fcc_riptr, fpi->dpram_offset);
	W16(ep, fen_genfcc.fcc_tiptr, fpi->dpram_offset + 32);

	W16(ep, fen_padptr, fpi->dpram_offset + 64);

	/* fill with special symbol...  */
	memset_io(fep->fcc.mem + fpi->dpram_offset + 64, 0x88, 32);

	W32(ep, fen_genfcc.fcc_rbptr, 0);
	W32(ep, fen_genfcc.fcc_tbptr, 0);
	W32(ep, fen_genfcc.fcc_rcrc, 0);
	W32(ep, fen_genfcc.fcc_tcrc, 0);
	W16(ep, fen_genfcc.fcc_res1, 0);
	W32(ep, fen_genfcc.fcc_res2, 0);

	/* no CAM */
	W32(ep, fen_camptr, 0);

	/* Set CRC preset and mask */
	W32(ep, fen_cmask, 0xdebb20e3);
	W32(ep, fen_cpres, 0xffffffff);

	W32(ep, fen_crcec, 0);		/* CRC Error counter       */
	W32(ep, fen_alec, 0);		/* alignment error counter */
	W32(ep, fen_disfc, 0);		/* discard frame counter   */
	W16(ep, fen_retlim, 15);	/* Retry limit threshold   */
	W16(ep, fen_pper, 0);		/* Normal persistence      */

	/* set group address */
	W32(ep, fen_gaddrh, fep->fcc.gaddrh);
	W32(ep, fen_gaddrl, fep->fcc.gaddrh);

	/* Clear hash filter tables */
	W32(ep, fen_iaddrh, 0);
	W32(ep, fen_iaddrl, 0);

	/* Clear the Out-of-sequence TxBD  */
	W16(ep, fen_tfcstat, 0);
	W16(ep, fen_tfclen, 0);
	W32(ep, fen_tfcptr, 0);

	W16(ep, fen_mflr, PKT_MAXBUF_SIZE);	/* maximum frame length register */
	W16(ep, fen_minflr, PKT_MINBUF_SIZE);	/* minimum frame length register */

	/* set address */
	mac = dev->dev_addr;
	paddrh = ((u16)mac[5] << 8) | mac[4];
	paddrm = ((u16)mac[3] << 8) | mac[2];
	paddrl = ((u16)mac[1] << 8) | mac[0];

	W16(ep, fen_paddrh, paddrh);
	W16(ep, fen_paddrm, paddrm);
	W16(ep, fen_paddrl, paddrl);

	W16(ep, fen_taddrh, 0);
	W16(ep, fen_taddrm, 0);
	W16(ep, fen_taddrl, 0);

	W16(ep, fen_maxd1, 1520);	/* maximum DMA1 length */
	W16(ep, fen_maxd2, 1520);	/* maximum DMA2 length */

	/* Clear stat counters, in case we ever enable RMON */
	W32(ep, fen_octc, 0);
	W32(ep, fen_colc, 0);
	W32(ep, fen_broc, 0);
	W32(ep, fen_mulc, 0);
	W32(ep, fen_uspc, 0);
	W32(ep, fen_frgc, 0);
	W32(ep, fen_ospc, 0);
	W32(ep, fen_jbrc, 0);
	W32(ep, fen_p64c, 0);
	W32(ep, fen_p65c, 0);
	W32(ep, fen_p128c, 0);
	W32(ep, fen_p256c, 0);
	W32(ep, fen_p512c, 0);
	W32(ep, fen_p1024c, 0);

	W16(ep, fen_rfthr, 0);	/* Suggested by manual */
	W16(ep, fen_rfcnt, 0);
	W16(ep, fen_cftype, 0);

	fs_init_bds(dev);

	/* adjust to speed (for RMII mode) */
	if (fpi->use_rmii) {
		if (fep->phydev->speed == 100)
			C8(fcccp, fcc_gfemr, 0x20);
		else
			S8(fcccp, fcc_gfemr, 0x20);
	}

	fcc_cr_cmd(fep, CPM_CR_INIT_TRX);

	/* clear events */
	W16(fccp, fcc_fcce, 0xffff);

	/* Enable interrupts we wish to service */
	W16(fccp, fcc_fccm, FCC_ENET_TXE | FCC_ENET_RXF | FCC_ENET_TXB);

	/* Set GFMR to enable Ethernet operating mode */
	W32(fccp, fcc_gfmr, FCC_GFMR_TCI | FCC_GFMR_MODE_ENET);

	/* set sync/delimiters */
	W16(fccp, fcc_fdsr, 0xd555);

	W32(fccp, fcc_fpsmr, FCC_PSMR_ENCRC);

	if (fpi->use_rmii)
		S32(fccp, fcc_fpsmr, FCC_PSMR_RMII);

	/* adjust to duplex mode */
	if (fep->phydev->duplex)
		S32(fccp, fcc_fpsmr, FCC_PSMR_FDE | FCC_PSMR_LPB);
	else
		C32(fccp, fcc_fpsmr, FCC_PSMR_FDE | FCC_PSMR_LPB);

	/* Restore multicast and promiscuous settings */
	set_multicast_list(dev);

	S32(fccp, fcc_gfmr, FCC_GFMR_ENR | FCC_GFMR_ENT);
}

static void stop(struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	fcc_t __iomem *fccp = fep->fcc.fccp;

	/* stop ethernet */
	C32(fccp, fcc_gfmr, FCC_GFMR_ENR | FCC_GFMR_ENT);

	/* clear events */
	W16(fccp, fcc_fcce, 0xffff);

	/* clear interrupt mask */
	W16(fccp, fcc_fccm, 0);

	fs_cleanup_bds(dev);
}

static void napi_clear_rx_event(struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	fcc_t __iomem *fccp = fep->fcc.fccp;

	W16(fccp, fcc_fcce, FCC_NAPI_RX_EVENT_MSK);
}

static void napi_enable_rx(struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	fcc_t __iomem *fccp = fep->fcc.fccp;

	S16(fccp, fcc_fccm, FCC_NAPI_RX_EVENT_MSK);
}

static void napi_disable_rx(struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	fcc_t __iomem *fccp = fep->fcc.fccp;

	C16(fccp, fcc_fccm, FCC_NAPI_RX_EVENT_MSK);
}

static void rx_bd_done(struct net_device *dev)
{
	/* nothing */
}

static void tx_kickstart(struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	fcc_t __iomem *fccp = fep->fcc.fccp;

	S16(fccp, fcc_ftodr, 0x8000);
}

static u32 get_int_events(struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	fcc_t __iomem *fccp = fep->fcc.fccp;

	return (u32)R16(fccp, fcc_fcce);
}

static void clear_int_events(struct net_device *dev, u32 int_events)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	fcc_t __iomem *fccp = fep->fcc.fccp;

	W16(fccp, fcc_fcce, int_events & 0xffff);
}

static void ev_error(struct net_device *dev, u32 int_events)
{
	struct fs_enet_private *fep = netdev_priv(dev);

	dev_warn(fep->dev, "FS_ENET ERROR(s) 0x%x\n", int_events);
}

static int get_regs(struct net_device *dev, void *p, int *sizep)
{
	struct fs_enet_private *fep = netdev_priv(dev);

	if (*sizep < sizeof(fcc_t) + sizeof(fcc_enet_t) + 1)
		return -EINVAL;

	memcpy_fromio(p, fep->fcc.fccp, sizeof(fcc_t));
	p = (char *)p + sizeof(fcc_t);

	memcpy_fromio(p, fep->fcc.ep, sizeof(fcc_enet_t));
	p = (char *)p + sizeof(fcc_enet_t);

	memcpy_fromio(p, fep->fcc.fcccp, 1);
	return 0;
}

static int get_regs_len(struct net_device *dev)
{
	return sizeof(fcc_t) + sizeof(fcc_enet_t) + 1;
}

/* Some transmit errors cause the transmitter to shut
 * down.  We now issue a restart transmit.  Since the
 * errors close the BD and update the pointers, the restart
 * _should_ pick up without having to reset any of our
 * pointers either.  Also, To workaround 8260 device erratum
 * CPM37, we must disable and then re-enable the transmitter
 * following a Late Collision, Underrun, or Retry Limit error.
 */
static void tx_restart(struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	fcc_t __iomem *fccp = fep->fcc.fccp;

	C32(fccp, fcc_gfmr, FCC_GFMR_ENT);
	udelay(10);
	S32(fccp, fcc_gfmr, FCC_GFMR_ENT);

	fcc_cr_cmd(fep, CPM_CR_RESTART_TX);
}

/*************************************************************************/

const struct fs_ops fs_fcc_ops = {
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
