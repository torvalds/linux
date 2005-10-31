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

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
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

#include <asm/immap_cpm2.h>
#include <asm/mpc8260.h>
#include <asm/cpm2.h>

#include <asm/pgtable.h>
#include <asm/irq.h>
#include <asm/uaccess.h>

#include "fs_enet.h"

/*************************************************/

/* FCC access macros */

#define __fcc_out32(addr, x)	out_be32((unsigned *)addr, x)
#define __fcc_out16(addr, x)	out_be16((unsigned short *)addr, x)
#define __fcc_out8(addr, x)	out_8((unsigned char *)addr, x)
#define __fcc_in32(addr)	in_be32((unsigned *)addr)
#define __fcc_in16(addr)	in_be16((unsigned short *)addr)
#define __fcc_in8(addr)		in_8((unsigned char *)addr)

/* parameter space */

/* write, read, set bits, clear bits */
#define W32(_p, _m, _v)	__fcc_out32(&(_p)->_m, (_v))
#define R32(_p, _m)	__fcc_in32(&(_p)->_m)
#define S32(_p, _m, _v)	W32(_p, _m, R32(_p, _m) | (_v))
#define C32(_p, _m, _v)	W32(_p, _m, R32(_p, _m) & ~(_v))

#define W16(_p, _m, _v)	__fcc_out16(&(_p)->_m, (_v))
#define R16(_p, _m)	__fcc_in16(&(_p)->_m)
#define S16(_p, _m, _v)	W16(_p, _m, R16(_p, _m) | (_v))
#define C16(_p, _m, _v)	W16(_p, _m, R16(_p, _m) & ~(_v))

#define W8(_p, _m, _v)	__fcc_out8(&(_p)->_m, (_v))
#define R8(_p, _m)	__fcc_in8(&(_p)->_m)
#define S8(_p, _m, _v)	W8(_p, _m, R8(_p, _m) | (_v))
#define C8(_p, _m, _v)	W8(_p, _m, R8(_p, _m) & ~(_v))

/*************************************************/

#define FCC_MAX_MULTICAST_ADDRS	64

#define mk_mii_read(REG)	(0x60020000 | ((REG & 0x1f) << 18))
#define mk_mii_write(REG, VAL)	(0x50020000 | ((REG & 0x1f) << 18) | (VAL & 0xffff))
#define mk_mii_end		0

#define MAX_CR_CMD_LOOPS	10000

static inline int fcc_cr_cmd(struct fs_enet_private *fep, u32 mcn, u32 op)
{
	const struct fs_platform_info *fpi = fep->fpi;

	cpm2_map_t *immap = fs_enet_immap;
	cpm_cpm2_t *cpmp = &immap->im_cpm;
	u32 v;
	int i;

	/* Currently I don't know what feature call will look like. But 
	   I guess there'd be something like do_cpm_cmd() which will require page & sblock */
	v = mk_cr_cmd(fpi->cp_page, fpi->cp_block, mcn, op);
	W32(cpmp, cp_cpcr, v | CPM_CR_FLG);
	for (i = 0; i < MAX_CR_CMD_LOOPS; i++)
		if ((R32(cpmp, cp_cpcr) & CPM_CR_FLG) == 0)
			break;

	if (i >= MAX_CR_CMD_LOOPS) {
		printk(KERN_ERR "%s(): Not able to issue CPM command\n",
		       __FUNCTION__);
		return 1;
	}

	return 0;
}

static int do_pd_setup(struct fs_enet_private *fep)
{
	struct platform_device *pdev = to_platform_device(fep->dev);
	struct resource *r;

	/* Fill out IRQ field */
	fep->interrupt = platform_get_irq(pdev, 0);

	/* Attach the memory for the FCC Parameter RAM */
	r = platform_get_resource_byname(pdev, IORESOURCE_MEM, "fcc_pram");
	fep->fcc.ep = (void *)r->start;

	if (fep->fcc.ep == NULL)
		return -EINVAL;

	r = platform_get_resource_byname(pdev, IORESOURCE_MEM, "fcc_regs");
	fep->fcc.fccp = (void *)r->start;

	if (fep->fcc.fccp == NULL)
		return -EINVAL;

	fep->fcc.fcccp = (void *)fep->fpi->fcc_regs_c;

	if (fep->fcc.fcccp == NULL)
		return -EINVAL;

	return 0;
}

#define FCC_NAPI_RX_EVENT_MSK	(FCC_ENET_RXF | FCC_ENET_RXB)
#define FCC_RX_EVENT		(FCC_ENET_RXF)
#define FCC_TX_EVENT		(FCC_ENET_TXB)
#define FCC_ERR_EVENT_MSK	(FCC_ENET_TXE | FCC_ENET_BSY)

static int setup_data(struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	const struct fs_platform_info *fpi = fep->fpi;

	fep->fcc.idx = fs_get_fcc_index(fpi->fs_no);
	if ((unsigned int)fep->fcc.idx >= 3)	/* max 3 FCCs */
		return -EINVAL;

	fep->fcc.mem = (void *)fpi->mem_offset;

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

	fep->ring_base = dma_alloc_coherent(fep->dev,
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
			fep->ring_base, fep->ring_mem_addr);
}

static void cleanup_data(struct net_device *dev)
{
	/* nothing */
}

static void set_promiscuous_mode(struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	fcc_t *fccp = fep->fcc.fccp;

	S32(fccp, fcc_fpsmr, FCC_PSMR_PRO);
}

static void set_multicast_start(struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	fcc_enet_t *ep = fep->fcc.ep;

	W32(ep, fen_gaddrh, 0);
	W32(ep, fen_gaddrl, 0);
}

static void set_multicast_one(struct net_device *dev, const u8 *mac)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	fcc_enet_t *ep = fep->fcc.ep;
	u16 taddrh, taddrm, taddrl;

	taddrh = ((u16)mac[5] << 8) | mac[4];
	taddrm = ((u16)mac[3] << 8) | mac[2];
	taddrl = ((u16)mac[1] << 8) | mac[0];

	W16(ep, fen_taddrh, taddrh);
	W16(ep, fen_taddrm, taddrm);
	W16(ep, fen_taddrl, taddrl);
	fcc_cr_cmd(fep, 0x0C, CPM_CR_SET_GADDR);
}

static void set_multicast_finish(struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	fcc_t *fccp = fep->fcc.fccp;
	fcc_enet_t *ep = fep->fcc.ep;

	/* clear promiscuous always */
	C32(fccp, fcc_fpsmr, FCC_PSMR_PRO);

	/* if all multi or too many multicasts; just enable all */
	if ((dev->flags & IFF_ALLMULTI) != 0 ||
	    dev->mc_count > FCC_MAX_MULTICAST_ADDRS) {

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
		for (pmc = dev->mc_list; pmc != NULL; pmc = pmc->next)
			set_multicast_one(dev, pmc->dmi_addr);
		set_multicast_finish(dev);
	} else
		set_promiscuous_mode(dev);
}

static void restart(struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	const struct fs_platform_info *fpi = fep->fpi;
	fcc_t *fccp = fep->fcc.fccp;
	fcc_c_t *fcccp = fep->fcc.fcccp;
	fcc_enet_t *ep = fep->fcc.ep;
	dma_addr_t rx_bd_base_phys, tx_bd_base_phys;
	u16 paddrh, paddrm, paddrl;
	u16 mem_addr;
	const unsigned char *mac;
	int i;

	C32(fccp, fcc_gfmr, FCC_GFMR_ENR | FCC_GFMR_ENT);

	/* clear everything (slow & steady does it) */
	for (i = 0; i < sizeof(*ep); i++)
		__fcc_out8((char *)ep + i, 0);

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

	mem_addr = (u32) fep->fcc.mem;	/* de-fixup dpram offset */

	W16(ep, fen_genfcc.fcc_riptr, (mem_addr & 0xffff));
	W16(ep, fen_genfcc.fcc_tiptr, ((mem_addr + 32) & 0xffff));
	W16(ep, fen_padptr, mem_addr + 64);

	/* fill with special symbol...  */
	memset(fep->fcc.mem + fpi->dpram_offset + 64, 0x88, 32);

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
		if (fep->speed == 100)
			C8(fcccp, fcc_gfemr, 0x20);
		else
			S8(fcccp, fcc_gfemr, 0x20);
	}

	fcc_cr_cmd(fep, 0x0c, CPM_CR_INIT_TRX);

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
	if (fep->duplex)
		S32(fccp, fcc_fpsmr, FCC_PSMR_FDE | FCC_PSMR_LPB);
	else
		C32(fccp, fcc_fpsmr, FCC_PSMR_FDE | FCC_PSMR_LPB);

	S32(fccp, fcc_gfmr, FCC_GFMR_ENR | FCC_GFMR_ENT);
}

static void stop(struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	fcc_t *fccp = fep->fcc.fccp;

	/* stop ethernet */
	C32(fccp, fcc_gfmr, FCC_GFMR_ENR | FCC_GFMR_ENT);

	/* clear events */
	W16(fccp, fcc_fcce, 0xffff);

	/* clear interrupt mask */
	W16(fccp, fcc_fccm, 0);

	fs_cleanup_bds(dev);
}

static void pre_request_irq(struct net_device *dev, int irq)
{
	/* nothing */
}

static void post_free_irq(struct net_device *dev, int irq)
{
	/* nothing */
}

static void napi_clear_rx_event(struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	fcc_t *fccp = fep->fcc.fccp;

	W16(fccp, fcc_fcce, FCC_NAPI_RX_EVENT_MSK);
}

static void napi_enable_rx(struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	fcc_t *fccp = fep->fcc.fccp;

	S16(fccp, fcc_fccm, FCC_NAPI_RX_EVENT_MSK);
}

static void napi_disable_rx(struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	fcc_t *fccp = fep->fcc.fccp;

	C16(fccp, fcc_fccm, FCC_NAPI_RX_EVENT_MSK);
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
	fcc_t *fccp = fep->fcc.fccp;

	return (u32)R16(fccp, fcc_fcce);
}

static void clear_int_events(struct net_device *dev, u32 int_events)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	fcc_t *fccp = fep->fcc.fccp;

	W16(fccp, fcc_fcce, int_events & 0xffff);
}

static void ev_error(struct net_device *dev, u32 int_events)
{
	printk(KERN_WARNING DRV_MODULE_NAME
	       ": %s FS_ENET ERROR(s) 0x%x\n", dev->name, int_events);
}

int get_regs(struct net_device *dev, void *p, int *sizep)
{
	struct fs_enet_private *fep = netdev_priv(dev);

	if (*sizep < sizeof(fcc_t) + sizeof(fcc_c_t) + sizeof(fcc_enet_t))
		return -EINVAL;

	memcpy_fromio(p, fep->fcc.fccp, sizeof(fcc_t));
	p = (char *)p + sizeof(fcc_t);

	memcpy_fromio(p, fep->fcc.fcccp, sizeof(fcc_c_t));
	p = (char *)p + sizeof(fcc_c_t);

	memcpy_fromio(p, fep->fcc.ep, sizeof(fcc_enet_t));

	return 0;
}

int get_regs_len(struct net_device *dev)
{
	return sizeof(fcc_t) + sizeof(fcc_c_t) + sizeof(fcc_enet_t);
}

/* Some transmit errors cause the transmitter to shut
 * down.  We now issue a restart transmit.  Since the
 * errors close the BD and update the pointers, the restart
 * _should_ pick up without having to reset any of our
 * pointers either.  Also, To workaround 8260 device erratum 
 * CPM37, we must disable and then re-enable the transmitter
 * following a Late Collision, Underrun, or Retry Limit error.
 */
void tx_restart(struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	fcc_t *fccp = fep->fcc.fccp;

	C32(fccp, fcc_gfmr, FCC_GFMR_ENT);
	udelay(10);
	S32(fccp, fcc_gfmr, FCC_GFMR_ENT);

	fcc_cr_cmd(fep, 0x0C, CPM_CR_RESTART_TX);
}

/*************************************************************************/

const struct fs_ops fs_fcc_ops = {
	.setup_data		= setup_data,
	.cleanup_data		= cleanup_data,
	.set_multicast_list	= set_multicast_list,
	.restart		= restart,
	.stop			= stop,
	.pre_request_irq	= pre_request_irq,
	.post_free_irq		= post_free_irq,
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
