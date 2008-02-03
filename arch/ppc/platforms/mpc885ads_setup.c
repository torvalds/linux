/*arch/ppc/platforms/mpc885ads_setup.c
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

#include <linux/init.h>
#include <linux/module.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/ioport.h>
#include <linux/device.h>

#include <linux/fs_enet_pd.h>
#include <linux/fs_uart_pd.h>
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
#include <asm/cpm1.h>
#include <asm/ppc_sys.h>

extern unsigned char __res[];
static void setup_smc1_ioports(struct fs_uart_platform_info*);
static void setup_smc2_ioports(struct fs_uart_platform_info*);

static struct fs_mii_fec_platform_info	mpc8xx_mdio_fec_pdata;
static void setup_fec1_ioports(struct fs_platform_info*);
static void setup_fec2_ioports(struct fs_platform_info*);
static void setup_scc3_ioports(struct fs_platform_info*);

static struct fs_uart_platform_info mpc885_uart_pdata[] = {
	[fsid_smc1_uart] = {
		.brg		= 1,
 		.fs_no 		= fsid_smc1_uart,
 		.init_ioports	= setup_smc1_ioports,
		.tx_num_fifo	= 4,
		.tx_buf_size	= 32,
		.rx_num_fifo	= 4,
		.rx_buf_size	= 32,
 	},
 	[fsid_smc2_uart] = {
 		.brg		= 2,
 		.fs_no 		= fsid_smc2_uart,
 		.init_ioports	= setup_smc2_ioports,
		.tx_num_fifo	= 4,
		.tx_buf_size	= 32,
		.rx_num_fifo	= 4,
		.rx_buf_size	= 32,
 	},
};

static struct fs_platform_info mpc8xx_enet_pdata[] = {
	[fsid_fec1] = {
	 .rx_ring = 128,
	 .tx_ring = 16,
	 .rx_copybreak = 240,

	 .use_napi = 1,
	 .napi_weight = 17,

	 .init_ioports = setup_fec1_ioports,

          .bus_id = "0:00",
          .has_phy = 1,
	 },
	[fsid_fec2] = {
	     .rx_ring = 128,
	     .tx_ring = 16,
	     .rx_copybreak = 240,

	     .use_napi = 1,
	     .napi_weight = 17,

	     .init_ioports = setup_fec2_ioports,

 	     .bus_id = "0:01",
 	     .has_phy = 1,
	     },
	[fsid_scc3] = {
		.rx_ring = 64,
		.tx_ring = 8,
		.rx_copybreak = 240,

		.use_napi = 1,
		.napi_weight = 17,

		.init_ioports = setup_scc3_ioports,
#ifdef CONFIG_FIXED_MII_10_FDX
		.bus_id = "fixed@100:1",
#else
		.bus_id = "0:02",
 #endif
	},
};

void __init board_init(void)
{
	cpm8xx_t *cp = cpmp;
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
        cp->cp_smc[0].smc_smcm |= (SMCM_RX | SMCM_TX);
        cp->cp_smc[0].smc_smcmr &= ~(SMCMR_REN | SMCMR_TEN);
#else
	setbits32(bcsr_io,BCSR1_RS232EN_1);
	cp->cp_smc[0].smc_smcmr = 0;
	cp->cp_smc[0].smc_smce = 0;
#endif

#ifdef CONFIG_SERIAL_CPM_SMC2
	cp->cp_simode &= ~(0xe0000000 >> 1);
	cp->cp_simode |= (0x20000000 >> 1);	/* brg2 */
	clrbits32(bcsr_io,BCSR1_RS232EN_2);
        cp->cp_smc[1].smc_smcm |= (SMCM_RX | SMCM_TX);
        cp->cp_smc[1].smc_smcmr &= ~(SMCMR_REN | SMCMR_TEN);
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
	bcsr_io = ioremap(BCSR5, sizeof(unsigned long));
	clrbits32(bcsr_io,BCSR5_MII1_EN);
	clrbits32(bcsr_io,BCSR5_MII1_RST);
#ifdef CONFIG_MPC8xx_SECOND_ETH_FEC2
	clrbits32(bcsr_io,BCSR5_MII2_EN);
	clrbits32(bcsr_io,BCSR5_MII2_RST);
#endif
	iounmap(bcsr_io);
#endif
}

static void setup_fec1_ioports(struct fs_platform_info* pdata)
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

static void setup_fec2_ioports(struct fs_platform_info* pdata)
{
	immap_t *immap = (immap_t *) IMAP_ADDR;

	/* configure FEC2 pins */
	setbits32(&immap->im_cpm.cp_pepar, 0x0003fffc);
	setbits32(&immap->im_cpm.cp_pedir, 0x0003fffc);
	clrbits32(&immap->im_cpm.cp_peso, 0x000087fc);
	setbits32(&immap->im_cpm.cp_peso, 0x00037800);
	clrbits32(&immap->im_cpm.cp_cptr, 0x00000080);
}

static void setup_scc3_ioports(struct fs_platform_info* pdata)
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
	clrbits32(bcsr_io+4, BCSR4_ETH10_RST);
	udelay(1000);
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

	setbits32(bcsr_io+4, BCSR1_ETHEN);
	iounmap(bcsr_io);
}

static int mac_count = 0;

static void mpc885ads_fixup_enet_pdata(struct platform_device *pdev, int fs_no)
{
 	struct fs_platform_info *fpi;
	bd_t *bd = (bd_t *) __res;
	char *e;
	int i;

	if(fs_no >= ARRAY_SIZE(mpc8xx_enet_pdata)) {
		printk(KERN_ERR"No network-suitable #%d device on bus", fs_no);
		return;
	}

	fpi = &mpc8xx_enet_pdata[fs_no];

	switch (fs_no) {
	case fsid_fec1:
		fpi->init_ioports = &setup_fec1_ioports;
		break;
	case fsid_fec2:
		fpi->init_ioports = &setup_fec2_ioports;
		break;
	case fsid_scc3:
		fpi->init_ioports = &setup_scc3_ioports;
		break;
	default:
    	        printk(KERN_WARNING "Device %s is not supported!\n", pdev->name);
	        return;
	}

	pdev->dev.platform_data = fpi;
	fpi->fs_no = fs_no;

	e = (unsigned char *)&bd->bi_enetaddr;
	for (i = 0; i < 6; i++)
		fpi->macaddr[i] = *e++;

	fpi->macaddr[5] += mac_count++;

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

static void setup_smc1_ioports(struct fs_uart_platform_info* pdata)
{
        immap_t *immap = (immap_t *) IMAP_ADDR;
        unsigned *bcsr_io;
        unsigned int iobits = 0x000000c0;

        bcsr_io = ioremap(BCSR1, sizeof(unsigned long));

        if (bcsr_io == NULL) {
                printk(KERN_CRIT "Could not remap BCSR1\n");
                return;
        }
        clrbits32(bcsr_io,BCSR1_RS232EN_1);
        iounmap(bcsr_io);

        setbits32(&immap->im_cpm.cp_pbpar, iobits);
        clrbits32(&immap->im_cpm.cp_pbdir, iobits);
        clrbits16(&immap->im_cpm.cp_pbodr, iobits);
}

static void setup_smc2_ioports(struct fs_uart_platform_info* pdata)
{
        immap_t *immap = (immap_t *) IMAP_ADDR;
        unsigned *bcsr_io;
        unsigned int iobits = 0x00000c00;

        bcsr_io = ioremap(BCSR1, sizeof(unsigned long));

        if (bcsr_io == NULL) {
                printk(KERN_CRIT "Could not remap BCSR1\n");
                return;
        }
        clrbits32(bcsr_io,BCSR1_RS232EN_2);
        iounmap(bcsr_io);

#ifndef CONFIG_SERIAL_CPM_ALT_SMC2
        setbits32(&immap->im_cpm.cp_pbpar, iobits);
        clrbits32(&immap->im_cpm.cp_pbdir, iobits);
        clrbits16(&immap->im_cpm.cp_pbodr, iobits);
#else
        setbits16(&immap->im_ioport.iop_papar, iobits);
        clrbits16(&immap->im_ioport.iop_padir, iobits);
        clrbits16(&immap->im_ioport.iop_paodr, iobits);
#endif
}

static void __init mpc885ads_fixup_uart_pdata(struct platform_device *pdev,
                                              int idx)
{
	bd_t *bd = (bd_t *) __res;
	struct fs_uart_platform_info *pinfo;
	int num = ARRAY_SIZE(mpc885_uart_pdata);

	int id = fs_uart_id_smc2fsid(idx);

	/* no need to alter anything if console */
	if ((id < num) && (!pdev->dev.platform_data)) {
		pinfo = &mpc885_uart_pdata[id];
		pinfo->uart_clk = bd->bi_intfreq;
		pdev->dev.platform_data = pinfo;
	}
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
			.bus_id = "fsl-cpm-smc:uart",
			.rtn = mpc885ads_fixup_uart_pdata
		},
		{
			.bus_id = NULL
		}
	};

	platform_notify_map(dev_map,dev);

	return 0;
}

int __init mpc885ads_init(void)
{
	struct fs_mii_fec_platform_info* fmpi;
	bd_t *bd = (bd_t *) __res;

	printk(KERN_NOTICE "mpc885ads: Init\n");

	platform_notify = mpc885ads_platform_notify;

	ppc_sys_device_initfunc();
	ppc_sys_device_disable_all();

	ppc_sys_device_enable(MPC8xx_CPM_FEC1);

	ppc_sys_device_enable(MPC8xx_MDIO_FEC);
	fmpi = ppc_sys_platform_devices[MPC8xx_MDIO_FEC].dev.platform_data =
		&mpc8xx_mdio_fec_pdata;

	fmpi->mii_speed = ((((bd->bi_intfreq + 4999999) / 2500000) / 2) & 0x3F) << 1;

	/* No PHY interrupt line here */
	fmpi->irq[0xf] = SIU_IRQ7;

#ifdef CONFIG_MPC8xx_SECOND_ETH_SCC3
	ppc_sys_device_enable(MPC8xx_CPM_SCC3);

#endif
#ifdef CONFIG_MPC8xx_SECOND_ETH_FEC2
	ppc_sys_device_enable(MPC8xx_CPM_FEC2);
#endif

#ifdef CONFIG_SERIAL_CPM_SMC1
	ppc_sys_device_enable(MPC8xx_CPM_SMC1);
	ppc_sys_device_setfunc(MPC8xx_CPM_SMC1, PPC_SYS_FUNC_UART);
#endif

#ifdef CONFIG_SERIAL_CPM_SMC2
	ppc_sys_device_enable(MPC8xx_CPM_SMC2);
	ppc_sys_device_setfunc(MPC8xx_CPM_SMC2, PPC_SYS_FUNC_UART);
#endif
	return 0;
}

arch_initcall(mpc885ads_init);

/*
   To prevent confusion, console selection is gross:
   by 0 assumed SMC1 and by 1 assumed SMC2
 */
struct platform_device* early_uart_get_pdev(int index)
{
	bd_t *bd = (bd_t *) __res;
	struct fs_uart_platform_info *pinfo;

	struct platform_device* pdev = NULL;
	if(index) { /*assume SMC2 here*/
		pdev = &ppc_sys_platform_devices[MPC8xx_CPM_SMC2];
		pinfo = &mpc885_uart_pdata[1];
	} else { /*over SMC1*/
		pdev = &ppc_sys_platform_devices[MPC8xx_CPM_SMC1];
		pinfo = &mpc885_uart_pdata[0];
	}

	pinfo->uart_clk = bd->bi_intfreq;
	pdev->dev.platform_data = pinfo;
	ppc_sys_fixup_mem_resource(pdev, IMAP_ADDR);
	return NULL;
}

