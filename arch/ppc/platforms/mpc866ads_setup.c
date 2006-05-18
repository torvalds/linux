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
#include <asm/commproc.h>
#include <asm/ppc_sys.h>
#include <asm/mpc8xx.h>

extern unsigned char __res[];

static void setup_fec1_ioports(void);
static void setup_scc1_ioports(void);
static void setup_smc1_ioports(void);
static void setup_smc2_ioports(void);

static struct fs_mii_bus_info fec_mii_bus_info = {
	.method = fsmii_fec,
	.id = 0,
};

static struct fs_mii_bus_info scc_mii_bus_info = {
	.method = fsmii_fixed,
	.id = 0,
	.i.fixed.speed = 10,
	.i.fixed.duplex = 0,
};

static struct fs_platform_info mpc8xx_fec_pdata[] = {
	{
	 .rx_ring = 128,
	 .tx_ring = 16,
	 .rx_copybreak = 240,

	 .use_napi = 1,
	 .napi_weight = 17,

	 .phy_addr = 15,
	 .phy_irq = -1,

	 .use_rmii = 0,

	 .bus_info = &fec_mii_bus_info,
	 }
};

static struct fs_platform_info mpc8xx_scc_pdata = {
	.rx_ring = 64,
	.tx_ring = 8,
	.rx_copybreak = 240,

	.use_napi = 1,
	.napi_weight = 17,

	.phy_addr = -1,
	.phy_irq = -1,

	.bus_info = &scc_mii_bus_info,

};

static struct fs_uart_platform_info mpc866_uart_pdata[] = {
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

void __init board_init(void)
{
	volatile cpm8xx_t *cp = cpmp;
	unsigned *bcsr_io;

	bcsr_io = ioremap(BCSR1, sizeof(unsigned long));

	if (bcsr_io == NULL) {
		printk(KERN_CRIT "Could not remap BCSR1\n");
		return;
	}

#ifdef CONFIG_SERIAL_CPM_SMC1
	cp->cp_simode &= ~(0xe0000000 >> 17);	/* brg1 */
	clrbits32(bcsr_io,(0x80000000 >> 7));
	cp->cp_smc[0].smc_smcm |= (SMCM_RX | SMCM_TX);
	cp->cp_smc[0].smc_smcmr &= ~(SMCMR_REN | SMCMR_TEN);
#else
	setbits32(bcsr_io,(0x80000000 >> 7));

	cp->cp_pbpar &= ~(0x000000c0);
	cp->cp_pbdir |= 0x000000c0;
	cp->cp_smc[0].smc_smcmr = 0;
	cp->cp_smc[0].smc_smce = 0;
#endif

#ifdef CONFIG_SERIAL_CPM_SMC2
	cp->cp_simode &= ~(0xe0000000 >> 1);
	cp->cp_simode |= (0x20000000 >> 1);	/* brg2 */
	clrbits32(bcsr_io,(0x80000000 >> 13));
	cp->cp_smc[1].smc_smcm |= (SMCM_RX | SMCM_TX);
	cp->cp_smc[1].smc_smcmr &= ~(SMCMR_REN | SMCMR_TEN);
#else
	clrbits32(bcsr_io,(0x80000000 >> 13));
	cp->cp_pbpar &= ~(0x00000c00);
	cp->cp_pbdir |= 0x00000c00;
	cp->cp_smc[1].smc_smcmr = 0;
	cp->cp_smc[1].smc_smce = 0;
#endif
	iounmap(bcsr_io);
}

static void setup_fec1_ioports(void)
{
	immap_t *immap = (immap_t *) IMAP_ADDR;

	setbits16(&immap->im_ioport.iop_pdpar, 0x1fff);
	setbits16(&immap->im_ioport.iop_pddir, 0x1fff);
}

static void setup_scc1_ioports(void)
{
	immap_t *immap = (immap_t *) IMAP_ADDR;
	unsigned *bcsr_io;

	bcsr_io = ioremap(BCSR1, sizeof(unsigned long));

	if (bcsr_io == NULL) {
		printk(KERN_CRIT "Could not remap BCSR1\n");
		return;
	}

	/* Enable the PHY.
	 */
	clrbits32(bcsr_io,BCSR1_ETHEN);

	/* Configure port A pins for Txd and Rxd.
	 */
	/* Disable receive and transmit in case EPPC-Bug started it.
	 */
	setbits16(&immap->im_ioport.iop_papar, PA_ENET_RXD | PA_ENET_TXD);
	clrbits16(&immap->im_ioport.iop_padir, PA_ENET_RXD | PA_ENET_TXD);
	clrbits16(&immap->im_ioport.iop_paodr, PA_ENET_TXD);

	/* Configure port C pins to enable CLSN and RENA.
	 */
	clrbits16(&immap->im_ioport.iop_pcpar, PC_ENET_CLSN | PC_ENET_RENA);
	clrbits16(&immap->im_ioport.iop_pcdir, PC_ENET_CLSN | PC_ENET_RENA);
	setbits16(&immap->im_ioport.iop_pcso, PC_ENET_CLSN | PC_ENET_RENA);
	/* Configure port A for TCLK and RCLK.
	 */
	setbits16(&immap->im_ioport.iop_papar, PA_ENET_TCLK | PA_ENET_RCLK);
	clrbits16(&immap->im_ioport.iop_padir, PA_ENET_TCLK | PA_ENET_RCLK);
	clrbits32(&immap->im_cpm.cp_pbpar, PB_ENET_TENA);
	clrbits32(&immap->im_cpm.cp_pbdir, PB_ENET_TENA);

	/* Configure Serial Interface clock routing.
	 * First, clear all SCC bits to zero, then set the ones we want.
	 */
	clrbits32(&immap->im_cpm.cp_sicr, SICR_ENET_MASK);
	setbits32(&immap->im_cpm.cp_sicr, SICR_ENET_CLKRT);

	/* In the original SCC enet driver the following code is placed at
	the end of the initialization */
	setbits32(&immap->im_cpm.cp_pbpar, PB_ENET_TENA);
	setbits32(&immap->im_cpm.cp_pbdir, PB_ENET_TENA);

}

static void mpc866ads_fixup_enet_pdata(struct platform_device *pdev, int fs_no)
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
	case fsid_scc1:
		fpi = &mpc8xx_scc_pdata;
		fpi->init_ioports = &setup_scc1_ioports;

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

static void mpc866ads_fixup_fec_enet_pdata(struct platform_device *pdev,
					   int idx)
{
	/* This is for FEC devices only */
	if (!pdev || !pdev->name || (!strstr(pdev->name, "fsl-cpm-fec")))
		return;
	mpc866ads_fixup_enet_pdata(pdev, fsid_fec1 + pdev->id - 1);
}

static void mpc866ads_fixup_scc_enet_pdata(struct platform_device *pdev,
					   int idx)
{
	/* This is for SCC devices only */
	if (!pdev || !pdev->name || (!strstr(pdev->name, "fsl-cpm-scc")))
		return;

	mpc866ads_fixup_enet_pdata(pdev, fsid_scc1 + pdev->id - 1);
}

static void setup_smc1_ioports(void)
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

static void setup_smc2_ioports(void)
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

static void __init mpc866ads_fixup_uart_pdata(struct platform_device *pdev,
                                              int idx)
{
	bd_t *bd = (bd_t *) __res;
	struct fs_uart_platform_info *pinfo;
	int num = ARRAY_SIZE(mpc866_uart_pdata);

	int id = fs_uart_id_smc2fsid(idx);

	/* no need to alter anything if console */
	if ((id <= num) && (!pdev->dev.platform_data)) {
		pinfo = &mpc866_uart_pdata[id];
		pinfo->uart_clk = bd->bi_intfreq;
		pdev->dev.platform_data = pinfo;
	}
}

static int mpc866ads_platform_notify(struct device *dev)
{
	static const struct platform_notify_dev_map dev_map[] = {
		{
			.bus_id = "fsl-cpm-fec",
			.rtn = mpc866ads_fixup_fec_enet_pdata,
		},
		{
			.bus_id = "fsl-cpm-scc",
			.rtn = mpc866ads_fixup_scc_enet_pdata,
		},
		{
			.bus_id = "fsl-cpm-smc:uart",
			.rtn = mpc866ads_fixup_uart_pdata
		},
		{
			.bus_id = NULL
		}
	};

	platform_notify_map(dev_map,dev);

	return 0;
}

int __init mpc866ads_init(void)
{
	printk(KERN_NOTICE "mpc866ads: Init\n");

	platform_notify = mpc866ads_platform_notify;

	ppc_sys_device_initfunc();
	ppc_sys_device_disable_all();

#ifdef MPC8xx_SECOND_ETH_SCC1
	ppc_sys_device_enable(MPC8xx_CPM_SCC1);
#endif
	ppc_sys_device_enable(MPC8xx_CPM_FEC1);

/* Since either of the uarts could be used as console, they need to ready */
#ifdef CONFIG_SERIAL_CPM_SMC1
	ppc_sys_device_enable(MPC8xx_CPM_SMC1);
	ppc_sys_device_setfunc(MPC8xx_CPM_SMC1, PPC_SYS_FUNC_UART);
#endif

#ifdef CONFIG_SERIAL_CPM_SMC
	ppc_sys_device_enable(MPC8xx_CPM_SMC2);
	ppc_sys_device_setfunc(MPC8xx_CPM_SMC2, PPC_SYS_FUNC_UART);
#endif

	return 0;
}

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
		pinfo = &mpc866_uart_pdata[1];
	} else { /*over SMC1*/
		pdev = &ppc_sys_platform_devices[MPC8xx_CPM_SMC1];
		pinfo = &mpc866_uart_pdata[0];
	}

	pinfo->uart_clk = bd->bi_intfreq;
	pdev->dev.platform_data = pinfo;
	ppc_sys_fixup_mem_resource(pdev, IMAP_ADDR);
	return NULL;
}

arch_initcall(mpc866ads_init);
