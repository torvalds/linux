/*arch/ppc/platforms/mpc885ads-setup.c
 *
 * Platform setup for the Freescale mpc885ads board
 *
 * Vitaly Bordug <vbordug@ru.mvista.com>
 *
 * Copyright 2005 MontaVista Software Inc.
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/ioport.h>
#include <linux/device.h>

#include <linux/fs_enet_pd.h>
#include <linux/mii.h>

#include <asm/delay.h>
#include <asm/io.h>
#include <asm/machdep.h>
#include <asm/page.h>
#include <asm/processor.h>
#include <asm/system.h>
#include <asm/time.h>
#include <asm/ppcboot.h>
#include <asm/8xx_immap.h>
#include <asm/commproc.h>
#include <asm/ppc_sys.h>

extern unsigned char __res[];

static void __init mpc885ads_scc_phy_init(char);

static struct fs_mii_bus_info fec_mii_bus_info = {
	.method = fsmii_fec,
	.id = 0,
};

static struct fs_mii_bus_info scc_mii_bus_info = {
#ifdef CONFIG_SCC_ENET_8xx_FIXED
	.method = fsmii_fixed,
#else
	.method = fsmii_fec,
#endif

	.id = 0,
};

static struct fs_platform_info mpc8xx_fec_pdata[] = {
	{
	 .rx_ring = 128,
	 .tx_ring = 16,
	 .rx_copybreak = 240,

	 .use_napi = 1,
	 .napi_weight = 17,

	 .phy_addr = 0,
	 .phy_irq = SIU_IRQ7,

	 .bus_info = &fec_mii_bus_info,
	 }, {
	     .rx_ring = 128,
	     .tx_ring = 16,
	     .rx_copybreak = 240,

	     .use_napi = 1,
	     .napi_weight = 17,

	     .phy_addr = 1,
	     .phy_irq = SIU_IRQ7,

	     .bus_info = &fec_mii_bus_info,
	     }
};

static struct fs_platform_info mpc8xx_scc_pdata = {
	.rx_ring = 64,
	.tx_ring = 8,
	.rx_copybreak = 240,

	.use_napi = 1,
	.napi_weight = 17,

	.phy_addr = 2,
#ifdef CONFIG_MPC8xx_SCC_ENET_FIXED
	.phy_irq = -1,
#else
	.phy_irq = SIU_IRQ7,
#endif

	.bus_info = &scc_mii_bus_info,
};

void __init board_init(void)
{
	volatile cpm8xx_t *cp = cpmp;
	unsigned int *bcsr_io;

#ifdef CONFIG_FS_ENET
	immap_t *immap = (immap_t *) IMAP_ADDR;
#endif
	bcsr_io = ioremap(BCSR1, sizeof(unsigned long));

	if (bcsr_io == NULL) {
		printk(KERN_CRIT "Could not remap BCSR\n");
		return;
	}
#ifdef CONFIG_SERIAL_CPM_SMC1
	cp->cp_simode &= ~(0xe0000000 >> 17);	/* brg1 */
	clrbits32(bcsr_io, BCSR1_RS232EN_1);
#else
	setbits32(bcsr_io,BCSR1_RS232EN_1);
	cp->cp_smc[0].smc_smcmr = 0;
	cp->cp_smc[0].smc_smce = 0;
#endif

#ifdef CONFIG_SERIAL_CPM_SMC2
	cp->cp_simode &= ~(0xe0000000 >> 1);
	cp->cp_simode |= (0x20000000 >> 1);	/* brg2 */
	clrbits32(bcsr_io,BCSR1_RS232EN_2);
#else
	setbits32(bcsr_io,BCSR1_RS232EN_2);
	cp->cp_smc[1].smc_smcmr = 0;
	cp->cp_smc[1].smc_smce = 0;
#endif
	iounmap(bcsr_io);

#ifdef CONFIG_FS_ENET
	/* use MDC for MII (common) */
	setbits16(&immap->im_ioport.iop_pdpar, 0x0080);
	clrbits16(&immap->im_ioport.iop_pddir, 0x0080);
#endif
}

static void setup_fec1_ioports(void)
{
	immap_t *immap = (immap_t *) IMAP_ADDR;

	/* configure FEC1 pins  */
	setbits16(&immap->im_ioport.iop_papar, 0xf830);
	setbits16(&immap->im_ioport.iop_padir, 0x0830);
	clrbits16(&immap->im_ioport.iop_padir, 0xf000);
	setbits32(&immap->im_cpm.cp_pbpar, 0x00001001);

	clrbits32(&immap->im_cpm.cp_pbdir, 0x00001001);
	setbits16(&immap->im_ioport.iop_pcpar, 0x000c);
	clrbits16(&immap->im_ioport.iop_pcdir, 0x000c);
	setbits32(&immap->im_cpm.cp_pepar, 0x00000003);

	setbits32(&immap->im_cpm.cp_pedir, 0x00000003);
	clrbits32(&immap->im_cpm.cp_peso, 0x00000003);
	clrbits32(&immap->im_cpm.cp_cptr, 0x00000100);
}

static void setup_fec2_ioports(void)
{
	immap_t *immap = (immap_t *) IMAP_ADDR;

	/* configure FEC2 pins */
	setbits32(&immap->im_cpm.cp_pepar, 0x0003fffc);
	setbits32(&immap->im_cpm.cp_pedir, 0x0003fffc);
	setbits32(&immap->im_cpm.cp_peso, 0x00037800);
	clrbits32(&immap->im_cpm.cp_peso, 0x000087fc);
	clrbits32(&immap->im_cpm.cp_cptr, 0x00000080);
}

static void setup_scc3_ioports(void)
{
	immap_t *immap = (immap_t *) IMAP_ADDR;
	unsigned *bcsr_io;

	bcsr_io = ioremap(BCSR_ADDR, BCSR_SIZE);

	if (bcsr_io == NULL) {
		printk(KERN_CRIT "Could not remap BCSR\n");
		return;
	}

	/* Enable the PHY.
	 */
	setbits32(bcsr_io+4, BCSR4_ETH10_RST);
	/* Configure port A pins for Txd and Rxd.
	 */
	setbits16(&immap->im_ioport.iop_papar, PA_ENET_RXD | PA_ENET_TXD);
	clrbits16(&immap->im_ioport.iop_padir, PA_ENET_RXD | PA_ENET_TXD);

	/* Configure port C pins to enable CLSN and RENA.
	 */
	clrbits16(&immap->im_ioport.iop_pcpar, PC_ENET_CLSN | PC_ENET_RENA);
	clrbits16(&immap->im_ioport.iop_pcdir, PC_ENET_CLSN | PC_ENET_RENA);
	setbits16(&immap->im_ioport.iop_pcso, PC_ENET_CLSN | PC_ENET_RENA);

	/* Configure port E for TCLK and RCLK.
	 */
	setbits32(&immap->im_cpm.cp_pepar, PE_ENET_TCLK | PE_ENET_RCLK);
	clrbits32(&immap->im_cpm.cp_pepar, PE_ENET_TENA);
	clrbits32(&immap->im_cpm.cp_pedir,
		  PE_ENET_TCLK | PE_ENET_RCLK | PE_ENET_TENA);
	clrbits32(&immap->im_cpm.cp_peso, PE_ENET_TCLK | PE_ENET_RCLK);
	setbits32(&immap->im_cpm.cp_peso, PE_ENET_TENA);

	/* Configure Serial Interface clock routing.
	 * First, clear all SCC bits to zero, then set the ones we want.
	 */
	clrbits32(&immap->im_cpm.cp_sicr, SICR_ENET_MASK);
	setbits32(&immap->im_cpm.cp_sicr, SICR_ENET_CLKRT);

	/* Disable Rx and Tx. SMC1 sshould be stopped if SCC3 eternet are used.
	 */
	immap->im_cpm.cp_smc[0].smc_smcmr &= ~(SMCMR_REN | SMCMR_TEN);
	/* On the MPC885ADS SCC ethernet PHY is initialized in the full duplex mode
	 * by H/W setting after reset. SCC ethernet controller support only half duplex.
	 * This discrepancy of modes causes a lot of carrier lost errors.
	 */

	/* In the original SCC enet driver the following code is placed at
	   the end of the initialization */
	setbits32(&immap->im_cpm.cp_pepar, PE_ENET_TENA);
	clrbits32(&immap->im_cpm.cp_pedir, PE_ENET_TENA);
	setbits32(&immap->im_cpm.cp_peso, PE_ENET_TENA);

	setbits32(bcsr_io+1, BCSR1_ETHEN);
	iounmap(bcsr_io);
}

static void mpc885ads_fixup_enet_pdata(struct platform_device *pdev, int fs_no)
{
	struct fs_platform_info *fpi = pdev->dev.platform_data;

	volatile cpm8xx_t *cp;
	bd_t *bd = (bd_t *) __res;
	char *e;
	int i;

	/* Get pointer to Communication Processor */
	cp = cpmp;
	switch (fs_no) {
	case fsid_fec1:
		fpi = &mpc8xx_fec_pdata[0];
		fpi->init_ioports = &setup_fec1_ioports;
		break;
	case fsid_fec2:
		fpi = &mpc8xx_fec_pdata[1];
		fpi->init_ioports = &setup_fec2_ioports;
		break;
	case fsid_scc3:
		fpi = &mpc8xx_scc_pdata;
		fpi->init_ioports = &setup_scc3_ioports;
		mpc885ads_scc_phy_init(fpi->phy_addr);
		break;
	default:
    	        printk(KERN_WARNING"Device %s is not supported!\n", pdev->name);
	        return;
	}

	pdev->dev.platform_data = fpi;
	fpi->fs_no = fs_no;

	e = (unsigned char *)&bd->bi_enetaddr;
	for (i = 0; i < 6; i++)
		fpi->macaddr[i] = *e++;

	fpi->macaddr[5 - pdev->id]++;

}

static void mpc885ads_fixup_fec_enet_pdata(struct platform_device *pdev,
					   int idx)
{
	/* This is for FEC devices only */
	if (!pdev || !pdev->name || (!strstr(pdev->name, "fsl-cpm-fec")))
		return;
	mpc885ads_fixup_enet_pdata(pdev, fsid_fec1 + pdev->id - 1);
}

static void __init mpc885ads_fixup_scc_enet_pdata(struct platform_device *pdev,
						  int idx)
{
	/* This is for SCC devices only */
	if (!pdev || !pdev->name || (!strstr(pdev->name, "fsl-cpm-scc")))
		return;

	mpc885ads_fixup_enet_pdata(pdev, fsid_scc1 + pdev->id - 1);
}

/* SCC ethernet controller does not have MII management channel. FEC1 MII
 * channel is used to communicate with the 10Mbit PHY.
 */

#define MII_ECNTRL_PINMUX        0x4
#define FEC_ECNTRL_PINMUX        0x00000004
#define FEC_RCNTRL_MII_MODE        0x00000004

/* Make MII read/write commands.
 */
#define mk_mii_write(REG, VAL, PHY_ADDR)    (0x50020000 | (((REG) & 0x1f) << 18) | \
                ((VAL) & 0xffff) | ((PHY_ADDR) << 23))

static void mpc885ads_scc_phy_init(char phy_addr)
{
	volatile immap_t *immap;
	volatile fec_t *fecp;
	bd_t *bd;

	bd = (bd_t *) __res;
	immap = (immap_t *) IMAP_ADDR;	/* pointer to internal registers */
	fecp = &(immap->im_cpm.cp_fec);

	/* Enable MII pins of the FEC1
	 */
	setbits16(&immap->im_ioport.iop_pdpar, 0x0080);
	clrbits16(&immap->im_ioport.iop_pddir, 0x0080);
	/* Set MII speed to 2.5 MHz
	 */
	out_be32(&fecp->fec_mii_speed,
		 ((((bd->bi_intfreq + 4999999) / 2500000) / 2) & 0x3F) << 1);

	/* Enable FEC pin MUX
	 */
	setbits32(&fecp->fec_ecntrl, MII_ECNTRL_PINMUX);
	setbits32(&fecp->fec_r_cntrl, FEC_RCNTRL_MII_MODE);

	out_be32(&fecp->fec_mii_data,
		 mk_mii_write(MII_BMCR, BMCR_ISOLATE, phy_addr));
	udelay(100);
	out_be32(&fecp->fec_mii_data,
		 mk_mii_write(MII_ADVERTISE,
			      ADVERTISE_10HALF | ADVERTISE_CSMA, phy_addr));
	udelay(100);

	/* Disable FEC MII settings
	 */
	clrbits32(&fecp->fec_ecntrl, MII_ECNTRL_PINMUX);
	clrbits32(&fecp->fec_r_cntrl, FEC_RCNTRL_MII_MODE);
	out_be32(&fecp->fec_mii_speed, 0);
}

static int mpc885ads_platform_notify(struct device *dev)
{

	static const struct platform_notify_dev_map dev_map[] = {
		{
			.bus_id = "fsl-cpm-fec",
			.rtn = mpc885ads_fixup_fec_enet_pdata,
		},
		{
			.bus_id = "fsl-cpm-scc",
			.rtn = mpc885ads_fixup_scc_enet_pdata,
		},
		{
			.bus_id = NULL
		}
	};

	platform_notify_map(dev_map,dev);

}

int __init mpc885ads_init(void)
{
	printk(KERN_NOTICE "mpc885ads: Init\n");

	platform_notify = mpc885ads_platform_notify;

	ppc_sys_device_initfunc();
	ppc_sys_device_disable_all();

	ppc_sys_device_enable(MPC8xx_CPM_FEC1);

#ifdef CONFIG_MPC8xx_SECOND_ETH_SCC3
	ppc_sys_device_enable(MPC8xx_CPM_SCC1);

#endif
#ifdef CONFIG_MPC8xx_SECOND_ETH_FEC2
	ppc_sys_device_enable(MPC8xx_CPM_FEC2);
#endif

	return 0;
}

arch_initcall(mpc885ads_init);
