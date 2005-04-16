/*
 * FEC instantatiation file for NETTA
 */

#include <linux/config.h>
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

#include <asm/8xx_immap.h>
#include <asm/pgtable.h>
#include <asm/mpc8xx.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/commproc.h>

#include "fec_8xx.h"

/*************************************************/

static struct fec_platform_info fec1_info = {
	.fec_no = 0,
	.use_mdio = 1,
	.phy_addr = 8,
	.fec_irq = SIU_LEVEL1,
	.phy_irq = CPM_IRQ_OFFSET + CPMVEC_PIO_PC6,
	.rx_ring = 128,
	.tx_ring = 16,
	.rx_copybreak = 240,
	.use_napi = 1,
	.napi_weight = 17,
};

static struct fec_platform_info fec2_info = {
	.fec_no = 1,
	.use_mdio = 1,
	.phy_addr = 2,
	.fec_irq = SIU_LEVEL3,
	.phy_irq = CPM_IRQ_OFFSET + CPMVEC_PIO_PC7,
	.rx_ring = 128,
	.tx_ring = 16,
	.rx_copybreak = 240,
	.use_napi = 1,
	.napi_weight = 17,
};

static struct net_device *fec1_dev;
static struct net_device *fec2_dev;

/* XXX custom u-boot & Linux startup needed */
extern const char *__fw_getenv(const char *var);

/* access ports */
#define setbits32(_addr, _v) __fec_out32(&(_addr), __fec_in32(&(_addr)) |  (_v))
#define clrbits32(_addr, _v) __fec_out32(&(_addr), __fec_in32(&(_addr)) & ~(_v))

#define setbits16(_addr, _v) __fec_out16(&(_addr), __fec_in16(&(_addr)) |  (_v))
#define clrbits16(_addr, _v) __fec_out16(&(_addr), __fec_in16(&(_addr)) & ~(_v))

int fec_8xx_platform_init(void)
{
	immap_t *immap = (immap_t *)IMAP_ADDR;
	bd_t *bd = (bd_t *) __res;
	const char *s;
	char *e;
	int i;

	/* use MDC for MII */
	setbits16(immap->im_ioport.iop_pdpar, 0x0080);
	clrbits16(immap->im_ioport.iop_pddir, 0x0080);

	/* configure FEC1 pins */
	setbits16(immap->im_ioport.iop_papar, 0xe810);
	setbits16(immap->im_ioport.iop_padir, 0x0810);
	clrbits16(immap->im_ioport.iop_padir, 0xe000);

	setbits32(immap->im_cpm.cp_pbpar, 0x00000001);
	clrbits32(immap->im_cpm.cp_pbdir, 0x00000001);

	setbits32(immap->im_cpm.cp_cptr, 0x00000100);
	clrbits32(immap->im_cpm.cp_cptr, 0x00000050);

	clrbits16(immap->im_ioport.iop_pcpar, 0x0200);
	clrbits16(immap->im_ioport.iop_pcdir, 0x0200);
	clrbits16(immap->im_ioport.iop_pcso, 0x0200);
	setbits16(immap->im_ioport.iop_pcint, 0x0200);

	/* configure FEC2 pins */
	setbits32(immap->im_cpm.cp_pepar, 0x00039620);
	setbits32(immap->im_cpm.cp_pedir, 0x00039620);
	setbits32(immap->im_cpm.cp_peso, 0x00031000);
	clrbits32(immap->im_cpm.cp_peso, 0x00008620);

	setbits32(immap->im_cpm.cp_cptr, 0x00000080);
	clrbits32(immap->im_cpm.cp_cptr, 0x00000028);

	clrbits16(immap->im_ioport.iop_pcpar, 0x0200);
	clrbits16(immap->im_ioport.iop_pcdir, 0x0200);
	clrbits16(immap->im_ioport.iop_pcso, 0x0200);
	setbits16(immap->im_ioport.iop_pcint, 0x0200);

	/* fill up */
	fec1_info.sys_clk = bd->bi_intfreq;
	fec2_info.sys_clk = bd->bi_intfreq;

	s = __fw_getenv("ethaddr");
	if (s != NULL) {
		for (i = 0; i < 6; i++) {
			fec1_info.macaddr[i] = simple_strtoul(s, &e, 16);
			if (*e)
				s = e + 1;
		}
	}

	s = __fw_getenv("eth1addr");
	if (s != NULL) {
		for (i = 0; i < 6; i++) {
			fec2_info.macaddr[i] = simple_strtoul(s, &e, 16);
			if (*e)
				s = e + 1;
		}
	}

	fec_8xx_init_one(&fec1_info, &fec1_dev);
	fec_8xx_init_one(&fec2_info, &fec2_dev);

	return fec1_dev != NULL && fec2_dev != NULL ? 0 : -1;
}

void fec_8xx_platform_cleanup(void)
{
	if (fec2_dev != NULL)
		fec_8xx_cleanup_one(fec2_dev);

	if (fec1_dev != NULL)
		fec_8xx_cleanup_one(fec1_dev);
}
