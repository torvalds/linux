/*
 * arch/ppc/platforms/mpc8272ads_setup.c
 *
 * MPC82xx Board-specific PlatformDevice descriptions
 *
 * 2005 (c) MontaVista Software, Inc.
 * Vitaly Bordug <vbordug@ru.mvista.com>
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */


#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/ioport.h>
#include <linux/fs_enet_pd.h>
#include <linux/platform_device.h>
#include <linux/phy.h>

#include <asm/io.h>
#include <asm/mpc8260.h>
#include <asm/cpm2.h>
#include <asm/immap_cpm2.h>
#include <asm/irq.h>
#include <asm/ppc_sys.h>
#include <asm/ppcboot.h>
#include <linux/fs_uart_pd.h>

#include "pq2ads_pd.h"

static void init_fcc1_ioports(struct fs_platform_info*);
static void init_fcc2_ioports(struct fs_platform_info*);
static void init_scc1_uart_ioports(struct fs_uart_platform_info*);
static void init_scc4_uart_ioports(struct fs_uart_platform_info*);

static struct fs_uart_platform_info mpc8272_uart_pdata[] = {
	[fsid_scc1_uart] = {
		.init_ioports 	= init_scc1_uart_ioports,
		.fs_no		= fsid_scc1_uart,
		.brg		= 1,
		.tx_num_fifo	= 4,
		.tx_buf_size	= 32,
		.rx_num_fifo	= 4,
		.rx_buf_size	= 32,
	},
	[fsid_scc4_uart] = {
		.init_ioports 	= init_scc4_uart_ioports,
		.fs_no		= fsid_scc4_uart,
		.brg		= 4,
		.tx_num_fifo	= 4,
		.tx_buf_size	= 32,
		.rx_num_fifo	= 4,
		.rx_buf_size	= 32,
	},
};

static struct fs_mii_bb_platform_info m82xx_mii_bb_pdata = {
	.mdio_dat.bit	= 18,
	.mdio_dir.bit	= 18,
	.mdc_dat.bit	= 19,
	.delay		= 1,
};

static struct fs_platform_info mpc82xx_enet_pdata[] = {
	[fsid_fcc1] = {
		.fs_no		= fsid_fcc1,
		.cp_page	= CPM_CR_FCC1_PAGE,
		.cp_block 	= CPM_CR_FCC1_SBLOCK,

		.clk_trx 	= (PC_F1RXCLK | PC_F1TXCLK),
		.clk_route	= CMX1_CLK_ROUTE,
		.clk_mask	= CMX1_CLK_MASK,
		.init_ioports 	= init_fcc1_ioports,

		.mem_offset	= FCC1_MEM_OFFSET,

		.rx_ring	= 32,
		.tx_ring	= 32,
		.rx_copybreak	= 240,
		.use_napi	= 0,
		.napi_weight	= 17,
		.bus_id		= "0:00",
	},
	[fsid_fcc2] = {
		.fs_no		= fsid_fcc2,
		.cp_page	= CPM_CR_FCC2_PAGE,
		.cp_block 	= CPM_CR_FCC2_SBLOCK,
		.clk_trx 	= (PC_F2RXCLK | PC_F2TXCLK),
		.clk_route	= CMX2_CLK_ROUTE,
		.clk_mask	= CMX2_CLK_MASK,
		.init_ioports	= init_fcc2_ioports,

		.mem_offset	= FCC2_MEM_OFFSET,

		.rx_ring	= 32,
		.tx_ring	= 32,
		.rx_copybreak	= 240,
		.use_napi	= 0,
		.napi_weight	= 17,
		.bus_id		= "0:03",
	},
};

static void init_fcc1_ioports(struct fs_platform_info* pdata)
{
	struct io_port *io;
	u32 tempval;
	cpm2_map_t* immap = ioremap(CPM_MAP_ADDR, sizeof(cpm2_map_t));
	u32 *bcsr = ioremap(BCSR_ADDR+4, sizeof(u32));

	io = &immap->im_ioport;

	/* Enable the PHY */
	clrbits32(bcsr, BCSR1_FETHIEN);
	setbits32(bcsr, BCSR1_FETH_RST);

	/* FCC1 pins are on port A/C. */
	/* Configure port A and C pins for FCC1 Ethernet. */

	tempval = in_be32(&io->iop_pdira);
	tempval &= ~PA1_DIRA0;
	tempval |= PA1_DIRA1;
	out_be32(&io->iop_pdira, tempval);

	tempval = in_be32(&io->iop_psora);
	tempval &= ~PA1_PSORA0;
	tempval |= PA1_PSORA1;
	out_be32(&io->iop_psora, tempval);

	setbits32(&io->iop_ppara,PA1_DIRA0 | PA1_DIRA1);

	/* Alter clocks */
	tempval = PC_F1TXCLK|PC_F1RXCLK;

	clrbits32(&io->iop_psorc, tempval);
	clrbits32(&io->iop_pdirc, tempval);
	setbits32(&io->iop_pparc, tempval);

	clrbits32(&immap->im_cpmux.cmx_fcr, CMX1_CLK_MASK);
	setbits32(&immap->im_cpmux.cmx_fcr, CMX1_CLK_ROUTE);
	iounmap(bcsr);
	iounmap(immap);
}

static void init_fcc2_ioports(struct fs_platform_info* pdata)
{
	cpm2_map_t* immap = ioremap(CPM_MAP_ADDR, sizeof(cpm2_map_t));
	u32 *bcsr = ioremap(BCSR_ADDR+12, sizeof(u32));

	struct io_port *io;
	u32 tempval;

	immap = cpm2_immr;

	io = &immap->im_ioport;

	/* Enable the PHY */
	clrbits32(bcsr, BCSR3_FETHIEN2);
	setbits32(bcsr, BCSR3_FETH2_RST);

	/* FCC2 are port B/C. */
	/* Configure port A and C pins for FCC2 Ethernet. */

	tempval = in_be32(&io->iop_pdirb);
	tempval &= ~PB2_DIRB0;
	tempval |= PB2_DIRB1;
	out_be32(&io->iop_pdirb, tempval);

	tempval = in_be32(&io->iop_psorb);
	tempval &= ~PB2_PSORB0;
	tempval |= PB2_PSORB1;
	out_be32(&io->iop_psorb, tempval);

	setbits32(&io->iop_pparb,PB2_DIRB0 | PB2_DIRB1);

	tempval = PC_F2RXCLK|PC_F2TXCLK;

	/* Alter clocks */
	clrbits32(&io->iop_psorc,tempval);
	clrbits32(&io->iop_pdirc,tempval);
	setbits32(&io->iop_pparc,tempval);

	clrbits32(&immap->im_cpmux.cmx_fcr, CMX2_CLK_MASK);
	setbits32(&immap->im_cpmux.cmx_fcr, CMX2_CLK_ROUTE);

	iounmap(bcsr);
	iounmap(immap);
}


static void __init mpc8272ads_fixup_enet_pdata(struct platform_device *pdev,
					      int idx)
{
	bd_t* bi = (void*)__res;
	int fs_no = fsid_fcc1+pdev->id-1;

	if(fs_no >= ARRAY_SIZE(mpc82xx_enet_pdata)) {
		return;
	}

	mpc82xx_enet_pdata[fs_no].dpram_offset=
			(u32)cpm2_immr->im_dprambase;
	mpc82xx_enet_pdata[fs_no].fcc_regs_c =
			(u32)cpm2_immr->im_fcc_c;
	memcpy(&mpc82xx_enet_pdata[fs_no].macaddr,bi->bi_enetaddr,6);

	/* prevent dup mac */
	if(fs_no == fsid_fcc2)
		mpc82xx_enet_pdata[fs_no].macaddr[5] ^= 1;

	pdev->dev.platform_data = &mpc82xx_enet_pdata[fs_no];
}

static void mpc8272ads_fixup_uart_pdata(struct platform_device *pdev,
					      int idx)
{
	bd_t *bd = (bd_t *) __res;
	struct fs_uart_platform_info *pinfo;
	int num = ARRAY_SIZE(mpc8272_uart_pdata);
	int id = fs_uart_id_scc2fsid(idx);

	/* no need to alter anything if console */
	if ((id < num) && (!pdev->dev.platform_data)) {
		pinfo = &mpc8272_uart_pdata[id];
		pinfo->uart_clk = bd->bi_intfreq;
		pdev->dev.platform_data = pinfo;
	}
}

static void init_scc1_uart_ioports(struct fs_uart_platform_info* pdata)
{
	cpm2_map_t* immap = ioremap(CPM_MAP_ADDR, sizeof(cpm2_map_t));

        /* SCC1 is only on port D */
	setbits32(&immap->im_ioport.iop_ppard,0x00000003);
	clrbits32(&immap->im_ioport.iop_psord,0x00000001);
	setbits32(&immap->im_ioport.iop_psord,0x00000002);
	clrbits32(&immap->im_ioport.iop_pdird,0x00000001);
	setbits32(&immap->im_ioport.iop_pdird,0x00000002);

        /* Wire BRG1 to SCC1 */
	clrbits32(&immap->im_cpmux.cmx_scr,0x00ffffff);

	iounmap(immap);
}

static void init_scc4_uart_ioports(struct fs_uart_platform_info* pdata)
{
	cpm2_map_t* immap = ioremap(CPM_MAP_ADDR, sizeof(cpm2_map_t));

	setbits32(&immap->im_ioport.iop_ppard,0x00000600);
	clrbits32(&immap->im_ioport.iop_psord,0x00000600);
	clrbits32(&immap->im_ioport.iop_pdird,0x00000200);
	setbits32(&immap->im_ioport.iop_pdird,0x00000400);

        /* Wire BRG4 to SCC4 */
	clrbits32(&immap->im_cpmux.cmx_scr,0x000000ff);
	setbits32(&immap->im_cpmux.cmx_scr,0x0000001b);

	iounmap(immap);
}

static void __init mpc8272ads_fixup_mdio_pdata(struct platform_device *pdev,
					      int idx)
{
	m82xx_mii_bb_pdata.irq[0] = PHY_INTERRUPT;
	m82xx_mii_bb_pdata.irq[1] = PHY_POLL;
	m82xx_mii_bb_pdata.irq[2] = PHY_POLL;
	m82xx_mii_bb_pdata.irq[3] = PHY_INTERRUPT;
	m82xx_mii_bb_pdata.irq[31] = PHY_POLL;


	m82xx_mii_bb_pdata.mdio_dat.offset =
				(u32)&cpm2_immr->im_ioport.iop_pdatc;

	m82xx_mii_bb_pdata.mdio_dir.offset =
				(u32)&cpm2_immr->im_ioport.iop_pdirc;

	m82xx_mii_bb_pdata.mdc_dat.offset =
				(u32)&cpm2_immr->im_ioport.iop_pdatc;


	pdev->dev.platform_data = &m82xx_mii_bb_pdata;
}

static int mpc8272ads_platform_notify(struct device *dev)
{
	static const struct platform_notify_dev_map dev_map[] = {
		{
			.bus_id = "fsl-cpm-fcc",
			.rtn = mpc8272ads_fixup_enet_pdata,
		},
		{
			.bus_id = "fsl-cpm-scc:uart",
			.rtn = mpc8272ads_fixup_uart_pdata,
		},
		{
			.bus_id = "fsl-bb-mdio",
			.rtn = mpc8272ads_fixup_mdio_pdata,
		},
		{
			.bus_id = NULL
		}
	};
	platform_notify_map(dev_map,dev);

	return 0;

}

int __init mpc8272ads_init(void)
{
	printk(KERN_NOTICE "mpc8272ads: Init\n");

	platform_notify = mpc8272ads_platform_notify;

	ppc_sys_device_initfunc();

	ppc_sys_device_disable_all();
	ppc_sys_device_enable(MPC82xx_CPM_FCC1);
	ppc_sys_device_enable(MPC82xx_CPM_FCC2);

	/* to be ready for console, let's attach pdata here */
#ifdef CONFIG_SERIAL_CPM_SCC1
	ppc_sys_device_setfunc(MPC82xx_CPM_SCC1, PPC_SYS_FUNC_UART);
	ppc_sys_device_enable(MPC82xx_CPM_SCC1);

#endif

#ifdef CONFIG_SERIAL_CPM_SCC4
	ppc_sys_device_setfunc(MPC82xx_CPM_SCC4, PPC_SYS_FUNC_UART);
	ppc_sys_device_enable(MPC82xx_CPM_SCC4);
#endif

	ppc_sys_device_enable(MPC82xx_MDIO_BB);

	return 0;
}

/*
   To prevent confusion, console selection is gross:
   by 0 assumed SCC1 and by 1 assumed SCC4
 */
struct platform_device* early_uart_get_pdev(int index)
{
	bd_t *bd = (bd_t *) __res;
	struct fs_uart_platform_info *pinfo;

	struct platform_device* pdev = NULL;
	if(index) { /*assume SCC4 here*/
		pdev = &ppc_sys_platform_devices[MPC82xx_CPM_SCC4];
		pinfo = &mpc8272_uart_pdata[fsid_scc4_uart];
	} else { /*over SCC1*/
		pdev = &ppc_sys_platform_devices[MPC82xx_CPM_SCC1];
		pinfo = &mpc8272_uart_pdata[fsid_scc1_uart];
	}

	pinfo->uart_clk = bd->bi_intfreq;
	pdev->dev.platform_data = pinfo;
	ppc_sys_fixup_mem_resource(pdev, CPM_MAP_ADDR);
	return NULL;
}

arch_initcall(mpc8272ads_init);
