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
#include <linux/platform_device.h>

#include <asm/irq.h>
#include <asm/uaccess.h>

#ifdef CONFIG_8xx
#include <asm/8xx_immap.h>
#include <asm/pgtable.h>
#include <asm/mpc8xx.h>
#include <asm/commproc.h>
#endif

#include "fs_enet.h"

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


/* CRC polynomium used by the FEC for the multicast group filtering */
#define FEC_CRC_POLY   0x04C11DB7

#define FEC_MAX_MULTICAST_ADDRS	64

/* Interrupt events/masks.
*/
#define FEC_ENET_HBERR	0x80000000U	/* Heartbeat error          */
#define FEC_ENET_BABR	0x40000000U	/* Babbling receiver        */
#define FEC_ENET_BABT	0x20000000U	/* Babbling transmitter     */
#define FEC_ENET_GRA	0x10000000U	/* Graceful stop complete   */
#define FEC_ENET_TXF	0x08000000U	/* Full frame transmitted   */
#define FEC_ENET_TXB	0x04000000U	/* A buffer was transmitted */
#define FEC_ENET_RXF	0x02000000U	/* Full frame received      */
#define FEC_ENET_RXB	0x01000000U	/* A buffer was received    */
#define FEC_ENET_MII	0x00800000U	/* MII interrupt            */
#define FEC_ENET_EBERR	0x00400000U	/* SDMA bus error           */

#define FEC_ECNTRL_PINMUX	0x00000004
#define FEC_ECNTRL_ETHER_EN	0x00000002
#define FEC_ECNTRL_RESET	0x00000001

#define FEC_RCNTRL_BC_REJ	0x00000010
#define FEC_RCNTRL_PROM		0x00000008
#define FEC_RCNTRL_MII_MODE	0x00000004
#define FEC_RCNTRL_DRT		0x00000002
#define FEC_RCNTRL_LOOP		0x00000001

#define FEC_TCNTRL_FDEN		0x00000004
#define FEC_TCNTRL_HBC		0x00000002
#define FEC_TCNTRL_GTS		0x00000001


/* Make MII read/write commands for the FEC.
*/
#define mk_mii_read(REG)	(0x60020000 | ((REG & 0x1f) << 18))
#define mk_mii_write(REG, VAL)	(0x50020000 | ((REG & 0x1f) << 18) | (VAL & 0xffff))
#define mk_mii_end		0

#define FEC_MII_LOOPS	10000

/*
 * Delay to wait for FEC reset command to complete (in us) 
 */
#define FEC_RESET_DELAY		50

static int whack_reset(fec_t * fecp)
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
	struct platform_device *pdev = to_platform_device(fep->dev); 
	struct resource	*r;
	
	/* Fill out IRQ field */
	fep->interrupt = platform_get_irq_byname(pdev,"interrupt");
	if (fep->interrupt < 0)
		return -EINVAL;
	
	r = platform_get_resource_byname(pdev, IORESOURCE_MEM, "regs");
	fep->fec.fecp =(void*)r->start;

	if(fep->fec.fecp == NULL)
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

	if(fep->ring_base)
		dma_free_coherent(fep->dev, (fpi->tx_ring + fpi->rx_ring)
					* sizeof(cbd_t),
					fep->ring_base,
					fep->ring_mem_addr);
}

static void cleanup_data(struct net_device *dev)
{
	/* nothing */
}

static void set_promiscuous_mode(struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	fec_t *fecp = fep->fec.fecp;

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
	fec_t *fecp = fep->fec.fecp;

	/* if all multi or too many multicasts; just enable all */
	if ((dev->flags & IFF_ALLMULTI) != 0 ||
	    dev->mc_count > FEC_MAX_MULTICAST_ADDRS) {
		fep->fec.hthi = 0xffffffffU;
		fep->fec.htlo = 0xffffffffU;
	}

	FC(fecp, r_cntrl, FEC_RCNTRL_PROM);
	FW(fecp, hash_table_high, fep->fec.hthi);
	FW(fecp, hash_table_low, fep->fec.htlo);
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
#ifdef CONFIG_DUET
	immap_t *immap = fs_enet_immap;
	u32 cptr;
#endif
	struct fs_enet_private *fep = netdev_priv(dev);
	fec_t *fecp = fep->fec.fecp;
	const struct fs_platform_info *fpi = fep->fpi;
	dma_addr_t rx_bd_base_phys, tx_bd_base_phys;
	int r;
	u32 addrhi, addrlo;

	r = whack_reset(fep->fec.fecp);
	if (r != 0)
		printk(KERN_ERR DRV_MODULE_NAME
				": %s FEC Reset FAILED!\n", dev->name);

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
	FW(fecp, hash_table_high, fep->fec.hthi);
	FW(fecp, hash_table_low, fep->fec.htlo);

	/*
	 * Set maximum receive buffer size. 
	 */
	FW(fecp, r_buff_size, PKT_MAXBLR_SIZE);
	FW(fecp, r_hash, PKT_MAXBUF_SIZE);

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
	FW(fecp, fun_code, 0x78000000);

	/*
	 * Set MII speed. 
	 */
	FW(fecp, mii_speed, fep->mii_bus->fec.mii_speed);

	/*
	 * Clear any outstanding interrupt. 
	 */
	FW(fecp, ievent, 0xffc0);
	FW(fecp, ivec, (fep->interrupt / 2) << 29);
	

	/*
	 * adjust to speed (only for DUET & RMII) 
	 */
#ifdef CONFIG_DUET
	if (fpi->use_rmii) {
		cptr = in_be32(&immap->im_cpm.cp_cptr);
		switch (fs_get_fec_index(fpi->fs_no)) {
		case 0:
			cptr |= 0x100;
			if (fep->speed == 10)
				cptr |= 0x0000010;
			else if (fep->speed == 100)
				cptr &= ~0x0000010;
			break;
		case 1:
			cptr |= 0x80;
			if (fep->speed == 10)
				cptr |= 0x0000008;
			else if (fep->speed == 100)
				cptr &= ~0x0000008;
			break;
		default:
			BUG();	/* should never happen */
			break;
		}
		out_be32(&immap->im_cpm.cp_cptr, cptr);
	}
#endif

	FW(fecp, r_cntrl, FEC_RCNTRL_MII_MODE);	/* MII enable */
	/*
	 * adjust to duplex mode 
	 */
	if (fep->duplex) {
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
	fec_t *fecp = fep->fec.fecp;
	struct fs_enet_mii_bus *bus = fep->mii_bus;
	const struct fs_mii_bus_info *bi = bus->bus_info;
	int i;

	if ((FR(fecp, ecntrl) & FEC_ECNTRL_ETHER_EN) == 0)
		return;		/* already down */

	FW(fecp, x_cntrl, 0x01);	/* Graceful transmit stop */
	for (i = 0; ((FR(fecp, ievent) & 0x10000000) == 0) &&
	     i < FEC_RESET_DELAY; i++)
		udelay(1);

	if (i == FEC_RESET_DELAY)
		printk(KERN_WARNING DRV_MODULE_NAME
		       ": %s FEC timeout on graceful transmit stop\n",
		       dev->name);
	/*
	 * Disable FEC. Let only MII interrupts. 
	 */
	FW(fecp, imask, 0);
	FC(fecp, ecntrl, FEC_ECNTRL_ETHER_EN);

	fs_cleanup_bds(dev);

	/* shut down FEC1? that's where the mii bus is */
	if (fep->fec.idx == 0 && bus->refs > 1 && bi->method == fsmii_fec) {
		FS(fecp, r_cntrl, FEC_RCNTRL_MII_MODE);	/* MII enable */
		FS(fecp, ecntrl, FEC_ECNTRL_PINMUX | FEC_ECNTRL_ETHER_EN);
		FW(fecp, ievent, FEC_ENET_MII);
		FW(fecp, mii_speed, bus->fec.mii_speed);
	}
}

static void pre_request_irq(struct net_device *dev, int irq)
{
	immap_t *immap = fs_enet_immap;
	u32 siel;

	/* SIU interrupt */
	if (irq >= SIU_IRQ0 && irq < SIU_LEVEL7) {

		siel = in_be32(&immap->im_siu_conf.sc_siel);
		if ((irq & 1) == 0)
			siel |= (0x80000000 >> irq);
		else
			siel &= ~(0x80000000 >> (irq & ~1));
		out_be32(&immap->im_siu_conf.sc_siel, siel);
	}
}

static void post_free_irq(struct net_device *dev, int irq)
{
	/* nothing */
}

static void napi_clear_rx_event(struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	fec_t *fecp = fep->fec.fecp;

	FW(fecp, ievent, FEC_NAPI_RX_EVENT_MSK);
}

static void napi_enable_rx(struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	fec_t *fecp = fep->fec.fecp;

	FS(fecp, imask, FEC_NAPI_RX_EVENT_MSK);
}

static void napi_disable_rx(struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	fec_t *fecp = fep->fec.fecp;

	FC(fecp, imask, FEC_NAPI_RX_EVENT_MSK);
}

static void rx_bd_done(struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	fec_t *fecp = fep->fec.fecp;

	FW(fecp, r_des_active, 0x01000000);
}

static void tx_kickstart(struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	fec_t *fecp = fep->fec.fecp;

	FW(fecp, x_des_active, 0x01000000);
}

static u32 get_int_events(struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	fec_t *fecp = fep->fec.fecp;

	return FR(fecp, ievent) & FR(fecp, imask);
}

static void clear_int_events(struct net_device *dev, u32 int_events)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	fec_t *fecp = fep->fec.fecp;

	FW(fecp, ievent, int_events);
}

static void ev_error(struct net_device *dev, u32 int_events)
{
	printk(KERN_WARNING DRV_MODULE_NAME
	       ": %s FEC ERROR(s) 0x%x\n", dev->name, int_events);
}

int get_regs(struct net_device *dev, void *p, int *sizep)
{
	struct fs_enet_private *fep = netdev_priv(dev);

	if (*sizep < sizeof(fec_t))
		return -EINVAL;

	memcpy_fromio(p, fep->fec.fecp, sizeof(fec_t));

	return 0;
}

int get_regs_len(struct net_device *dev)
{
	return sizeof(fec_t);
}

void tx_restart(struct net_device *dev)
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

/***********************************************************************/

static int mii_read(struct fs_enet_mii_bus *bus, int phy_id, int location)
{
	fec_t *fecp = bus->fec.fecp;
	int i, ret = -1;

	if ((FR(fecp, r_cntrl) & FEC_RCNTRL_MII_MODE) == 0)
		BUG();

	/* Add PHY address to register command.  */
	FW(fecp, mii_data, (phy_id << 23) | mk_mii_read(location));

	for (i = 0; i < FEC_MII_LOOPS; i++)
		if ((FR(fecp, ievent) & FEC_ENET_MII) != 0)
			break;

	if (i < FEC_MII_LOOPS) {
		FW(fecp, ievent, FEC_ENET_MII);
		ret = FR(fecp, mii_data) & 0xffff;
	}

	return ret;
}

static void mii_write(struct fs_enet_mii_bus *bus, int phy_id, int location, int value)
{
	fec_t *fecp = bus->fec.fecp;
	int i;

	/* this must never happen */
	if ((FR(fecp, r_cntrl) & FEC_RCNTRL_MII_MODE) == 0)
		BUG();

	/* Add PHY address to register command.  */
	FW(fecp, mii_data, (phy_id << 23) | mk_mii_write(location, value));

	for (i = 0; i < FEC_MII_LOOPS; i++)
		if ((FR(fecp, ievent) & FEC_ENET_MII) != 0)
			break;

	if (i < FEC_MII_LOOPS)
		FW(fecp, ievent, FEC_ENET_MII);
}

int fs_mii_fec_init(struct fs_enet_mii_bus *bus)
{
	bd_t *bd = (bd_t *)__res;
	const struct fs_mii_bus_info *bi = bus->bus_info;
	fec_t *fecp;

	if (bi->id != 0)
		return -1;

	bus->fec.fecp = &((immap_t *)fs_enet_immap)->im_cpm.cp_fec;
	bus->fec.mii_speed = ((((bd->bi_intfreq + 4999999) / 2500000) / 2)
				& 0x3F) << 1;

	fecp = bus->fec.fecp;

	FS(fecp, r_cntrl, FEC_RCNTRL_MII_MODE);	/* MII enable */
	FS(fecp, ecntrl, FEC_ECNTRL_PINMUX | FEC_ECNTRL_ETHER_EN);
	FW(fecp, ievent, FEC_ENET_MII);
	FW(fecp, mii_speed, bus->fec.mii_speed);

	bus->mii_read = mii_read;
	bus->mii_write = mii_write;

	return 0;
}
