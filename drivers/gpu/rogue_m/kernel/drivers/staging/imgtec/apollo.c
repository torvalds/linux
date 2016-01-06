/*************************************************************************/ /*!
@File           apollo.c
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@License        Dual MIT/GPLv2

The contents of this file are subject to the MIT license as set out below.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

Alternatively, the contents of this file may be used under the terms of
the GNU General Public License Version 2 ("GPL") in which case the provisions
of GPL are applicable instead of those above.

If you wish to allow use of your version of this file only under the terms of
GPL, and not to allow others to use your version of this file under the terms
of the MIT license, indicate your decision by deleting the provisions above
and replace them with the notice and other provisions required by GPL as set
out in the file called "GPL-COPYING" included in this distribution. If you do
not delete the provisions above, a recipient may use your version of this file
under the terms of either the MIT license or GPL.

This License is also included in this distribution in the file called
"MIT-COPYING".

EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/ /**************************************************************************/
/* vi: set ts=8: */

/*
 * This is a device driver for the apollo testchip framework. It creates
 * platform devices for the pdp and rogue sub-devices, and exports functions
 * to manage the shared interrupt handling
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/moduleparam.h>
#include <linux/platform_device.h>

#include <asm/mtrr.h>

#include "apollo_drv.h"

#include "apollo_regs.h"
#include "tcf_clk_ctrl.h"
#include "tcf_pll.h"

#include "pvrmodule.h"
#include "rgxdefs_km.h"

#if defined(SUPPORT_ION)
#include "ion_lma_heap.h"
#endif

#define DRV_NAME "apollo"

/* How much memory to give to the PDP heap (used for pdp buffers). */
#define APOLLO_PDP_MEM_SIZE		(384*1024*1024)

#define PCI_VENDOR_ID_POWERVR		0x1010
#define DEVICE_ID_PCI_APOLLO_FPGA	0x1CF1
#define DEVICE_ID_PCIE_APOLLO_FPGA	0x1CF2

#define DCPDP_REG_PCI_BASENUM		(0)
#define APOLLO_MEM_PCI_BASENUM		(2)

#define APOLLO_INTERRUPT_FLAG_PDP	(1 << PDP1_INT_SHIFT)
#define APOLLO_INTERRUPT_FLAG_ROGUE	(1 << EXT_INT_SHIFT)

MODULE_DESCRIPTION("APOLLO testchip framework driver");

static int apollo_core_clock = RGX_TC_CORE_CLOCK_SPEED;
static int apollo_mem_clock = RGX_TC_MEM_CLOCK_SPEED;

module_param(apollo_core_clock, int, S_IRUSR | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(apollo_core_clock, "Apollo Rogue core clock speed");
module_param(apollo_mem_clock, int, S_IRUSR | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(apollo_mem_clock, "Apollo memory clock speed");

#define APOLLO_ION_HEAP_COUNT 3

struct apollo_device {
	struct pci_dev *pdev;
	resource_size_t tcf_register_resource;
	void __iomem *tcf_registers;
	spinlock_t interrupt_handler_lock;
	spinlock_t interrupt_enable_lock;

	struct {
		bool enabled;
		void (*handler_function)(void *);
		void *handler_data;
	} interrupt_handlers[APOLLO_INTERRUPT_COUNT];

	struct platform_device *pdp_dev;
	struct platform_device *rogue_dev;


	resource_size_t apollo_mem_base;
	resource_size_t apollo_mem_size;

	resource_size_t pdp_heap_mem_base;
	resource_size_t pdp_heap_mem_size;
	resource_size_t rogue_heap_mem_base;
	resource_size_t rogue_heap_mem_size;

#if defined(SUPPORT_ION)
	struct ion_device *ion_device;
	struct ion_heap *ion_heaps[APOLLO_ION_HEAP_COUNT];
	int ion_heap_count;
#endif
};

static int request_pci_io_addr(struct pci_dev *pdev, u32 index,
	resource_size_t offset, resource_size_t length)
{
	resource_size_t start, end;
	start = pci_resource_start(pdev, index);
	end = pci_resource_end(pdev, index);

	if ((start + offset + length - 1) > end)
		return -EIO;
	if (pci_resource_flags(pdev, index) & IORESOURCE_IO) {
		if (request_region(start + offset, length, DRV_NAME) == NULL)
			return -EIO;
	} else {
		if (request_mem_region(start + offset, length, DRV_NAME)
			== NULL)
			return -EIO;
	}
	return 0;
}

static void release_pci_io_addr(struct pci_dev *pdev, u32 index,
	resource_size_t start, resource_size_t length)
{
	if (pci_resource_flags(pdev, index) & IORESOURCE_IO)
		release_region(start, length);
	else
		release_mem_region(start, length);
}

static int apollo_set_clocks(struct apollo_device *apollo)
{
	int err = 0;
	u32 val;

	resource_size_t pll_clock_resource;
	void __iomem *pll_regs;

	err = request_pci_io_addr(apollo->pdev, SYS_APOLLO_REG_PCI_BASENUM,
		SYS_APOLLO_REG_PLL_OFFSET, SYS_APOLLO_REG_PLL_SIZE);
	if (err) {
		dev_err(&apollo->pdev->dev,
			"Failed to request apollo PLL register region\n");
		goto err_out;
	}
	pll_clock_resource =
		pci_resource_start(apollo->pdev, SYS_APOLLO_REG_PCI_BASENUM)
			+ SYS_APOLLO_REG_PLL_OFFSET;
	pll_regs = ioremap_nocache(pll_clock_resource, SYS_APOLLO_REG_PLL_SIZE);
	if (!pll_regs) {
		dev_err(&apollo->pdev->dev,
			"Failed to map apollo PLL registers\n");
		err = -EIO;
		goto err_release_registers;
	}

#if !((RGX_BVNC_KM_B == 1) && (RGX_BVNC_KM_V == 82) && \
	  (RGX_BVNC_KM_N == 4) && (RGX_BVNC_KM_C == 5))
	/* This is if 0 out since the current FPGA builds do not like their core
	 * clocks being set (it takes apollo down). */
	val = apollo_core_clock / 1000000;
	iowrite32(val, pll_regs + TCF_PLL_PLL_CORE_CLK0);
	val = 0x1 << PLL_CORE_DRP_GO_SHIFT;
	iowrite32(val, pll_regs + TCF_PLL_PLL_CORE_DRP_GO);
#endif

	val = apollo_mem_clock / 1000000;
	iowrite32(val, pll_regs + TCF_PLL_PLL_MEMIF_CLK0);
	val = 0x1 << PLL_MEM_DRP_GO_SHIFT;
	iowrite32(val, pll_regs + TCF_PLL_PLL_MEM_DRP_GO);

	dev_dbg(&apollo->pdev->dev, "Setting clocks to %uMHz/%uMHz\n",
			 apollo_core_clock / 1000000,
			 apollo_mem_clock / 1000000);
	udelay(400);

	iounmap(pll_regs);
err_release_registers:
	release_pci_io_addr(apollo->pdev, SYS_APOLLO_REG_PCI_BASENUM,
		pll_clock_resource, SYS_APOLLO_REG_PLL_SIZE);
err_out:
	return err;
}

static int mtrr_setup(struct pci_dev *pdev,
		      resource_size_t mem_start,
		      resource_size_t mem_size)
{
	int err = 0;
	int mtrr;

	/* Reset MTRR */
	mtrr = mtrr_add(mem_start, mem_size, MTRR_TYPE_UNCACHABLE, 0);
	if (mtrr < 0) {
		dev_err(&pdev->dev, "%d - %s: mtrr_add failed (%d)\n",
			__LINE__, __func__, mtrr);
		err = mtrr;
		goto err_out;
	}

	err = mtrr_del(mtrr, mem_start, mem_size);
	if (err < 0) {
		dev_err(&pdev->dev, "%d - %s: mtrr_del failed (%d)\n",
			__LINE__, __func__, err);
		goto err_out;
	}

	mtrr = mtrr_add(mem_start, mem_size, MTRR_TYPE_WRBACK, 0);
	if (mtrr < 0) {
		/* Stop, but not an error as this may be already be setup */
		dev_dbg(&pdev->dev, "%d - %s: mtrr_del failed (%d) - probably means the mtrr is already setup\n",
			__LINE__, __func__, err);
		err = 0;
		goto err_out;
	}

	err = mtrr_del(mtrr, mem_start, mem_size);
	if (err < 0) {
		dev_err(&pdev->dev, "%d - %s: mtrr_del failed (%d)\n",
			__LINE__, __func__, err);
		goto err_out;
	}

	if (mtrr == 0) {
		/* Replace 0 with a non-overlapping WRBACK mtrr */
		err = mtrr_add(0, mem_start, MTRR_TYPE_WRBACK, 0);
		if (err < 0) {
			dev_err(&pdev->dev, "%d - %s: mtrr_add failed (%d)\n",
				__LINE__, __func__, err);
			goto err_out;
		}
	}

	mtrr = mtrr_add(mem_start, mem_size, MTRR_TYPE_WRCOMB, 0);

	if (mtrr < 0)
		dev_err(&pdev->dev, "%d - %s: mtrr_add failed (%d)\n",
			__LINE__, __func__, mtrr);
	err = 0;

err_out:
	return err;
}

static void apollo_init_memory(struct apollo_device *apollo)
{
	u32 val;

	val = ioread32(apollo->tcf_registers + TCF_CLK_CTRL_TEST_CTRL);
	val &= ~(ADDRESS_FORCE_MASK | PCI_TEST_MODE_MASK | HOST_ONLY_MODE_MASK
		| HOST_PHY_MODE_MASK);
	val |= (0x1 << ADDRESS_FORCE_SHIFT);
	iowrite32(val, apollo->tcf_registers + TCF_CLK_CTRL_TEST_CTRL);
}

static void apollo_deinit_memory(struct apollo_device *apollo)
{
	iowrite32(0x1 << ADDRESS_FORCE_SHIFT,
		apollo->tcf_registers + TCF_CLK_CTRL_TEST_CTRL);
}

static void apollo_devres_release(struct device *dev, void *res)
{
	/* No extra cleanup needed */
}

#if ((RGX_BVNC_KM_B == 1) && (RGX_BVNC_KM_V == 82) && \
	(RGX_BVNC_KM_N == 4) && (RGX_BVNC_KM_C == 5)) || \
    ((RGX_BVNC_KM_B == 4) && (RGX_BVNC_KM_V == 31) && \
	(RGX_BVNC_KM_N == 4) && (RGX_BVNC_KM_C == 55))

static int is_interface_aligned(u32 eyes, u32 clk_taps, u32 train_ack)
{
	u32	max_eye_start = eyes >> 16;
	u32	min_eye_end   = eyes & 0xffff;

	/* If either the training or training ack failed, we haven't aligned */
	if (!(clk_taps & 0x10000) || !(train_ack & 0x100))
		return 0;

	/* If the max eye >= min eye it means the readings are nonsense */
	if (max_eye_start >= min_eye_end)
		return 0;

	/* If we failed the ack pattern more than 4 times */
	if (((train_ack & 0xf0) >> 4) > 4)
		return 0;

	/* If there is less than 7 taps (240ps @40ps/tap, this number should be
	 * lower for the fpga, since its taps are bigger We should really
	 * calculate the "7" based on the interface clock speed. */
	if ((min_eye_end - max_eye_start) < 7)
		return 0;

	return 1;
}

static u32 sai_read(struct apollo_device *apollo, u32 addr)
{
	iowrite32(0x200 | addr, apollo->tcf_registers + 0x300);
	iowrite32(0x1 | addr, apollo->tcf_registers + 0x318);
	return ioread32(apollo->tcf_registers + 0x310);
}

static void spi_write(struct apollo_device *apollo,
					  u32 off, u32 val)
{
	iowrite32(off, apollo->tcf_registers +
			  TCF_CLK_CTRL_TCF_SPI_MST_ADDR_RDNWR);
	iowrite32(val, apollo->tcf_registers +
			  TCF_CLK_CTRL_TCF_SPI_MST_WDATA);
	iowrite32(TCF_SPI_MST_GO_MASK, apollo->tcf_registers +
			  TCF_CLK_CTRL_TCF_SPI_MST_GO);
	udelay(1000);
}

static int spi_read(struct apollo_device *apollo,
					u32 off, u32 *val)
{
	int cnt = 0;

	iowrite32(0x40000 | off, apollo->tcf_registers +
			  TCF_CLK_CTRL_TCF_SPI_MST_ADDR_RDNWR);
	iowrite32(TCF_SPI_MST_GO_MASK, apollo->tcf_registers +
			  TCF_CLK_CTRL_TCF_SPI_MST_GO);
	udelay(100);

	while ((ioread32(apollo->tcf_registers +
				TCF_CLK_CTRL_TCF_SPI_MST_STATUS) != 0x08) &&
		   (cnt < 10000))
		++cnt;

	if (cnt == 10000) {
		dev_err(&apollo->pdev->dev,
			"spi_read: Time out reading SPI register (0x%x)\n",
			off);
		return -1;
	}

	*val = ioread32(apollo->tcf_registers + TCF_CLK_CTRL_TCF_SPI_MST_RDATA);

	return 0;
}

static int apollo_hard_reset(struct apollo_device *apollo)
{
	u32 reg;
	u32 reg_reset_n;

	/* For displaying some build info */
	u32 build_inc;
	u32 build_owner;

	int aligned = 0;
	int reset_cnt = 0;

	/* This is required for SPI reset which is not yet implemented. */
	/*u32 aux_reset_n;*/

#if !((RGX_BVNC_KM_B == 4) && (RGX_BVNC_KM_V == 31) && \
	 (RGX_BVNC_KM_N == 4) && (RGX_BVNC_KM_C == 55))
	/* Power down */
	reg = ioread32(apollo->tcf_registers + TCF_CLK_CTRL_DUT_CONTROL_1);
	reg &= ~DUT_CTRL_VCC_0V9EN;
	reg &= ~DUT_CTRL_VCC_1V8EN;
	reg |= DUT_CTRL_VCC_IO_INH;
	reg |= DUT_CTRL_VCC_CORE_INH;
	iowrite32(reg, apollo->tcf_registers + TCF_CLK_CTRL_DUT_CONTROL_1);
	msleep(500);
#endif

	/* Set clock speed here, before reset. */
	apollo_set_clocks(apollo);

	/* Put DCM, DUT, DDR, PDP1, and PDP2 into reset */
	reg_reset_n  = (0x1 << GLB_CLKG_EN_SHIFT);
	reg_reset_n |= (0x1 << SCB_RESETN_SHIFT);
	iowrite32(reg_reset_n, apollo->tcf_registers +
		  TCF_CLK_CTRL_CLK_AND_RST_CTRL);
	msleep(100);

#if !((RGX_BVNC_KM_B == 4) && (RGX_BVNC_KM_V == 31) && \
	 (RGX_BVNC_KM_N == 4) && (RGX_BVNC_KM_C == 55))
	/* Enable the voltage control regulators on DUT */
	reg = ioread32(apollo->tcf_registers + TCF_CLK_CTRL_DUT_CONTROL_1);
	reg |= DUT_CTRL_VCC_0V9EN;
	reg |= DUT_CTRL_VCC_1V8EN;
	reg &= ~DUT_CTRL_VCC_IO_INH;
	reg &= ~DUT_CTRL_VCC_CORE_INH;
	iowrite32(reg, apollo->tcf_registers + TCF_CLK_CTRL_DUT_CONTROL_1);
	msleep(300);
#endif

	/* Take DCM, DDR, PDP1, and PDP2 out of reset */
	reg_reset_n |= (0x1 << DDR_RESETN_SHIFT);
	reg_reset_n |= (0x1 << DUT_DCM_RESETN_SHIFT);
	reg_reset_n |= (0x1 << PDP1_RESETN_SHIFT);
	reg_reset_n |= (0x1 << PDP2_RESETN_SHIFT);
	iowrite32(reg_reset_n, apollo->tcf_registers +
		  TCF_CLK_CTRL_CLK_AND_RST_CTRL);

#if !((RGX_BVNC_KM_B == 4) && (RGX_BVNC_KM_V == 31) && \
	 (RGX_BVNC_KM_N == 4) && (RGX_BVNC_KM_C == 55))
	/* Set ODT to a specific value that seems to provide the most stable
	 * signals. */
	spi_write(apollo, 0x11, 0x413130);
#endif

	/* Take DUT out of reset */
	reg_reset_n |= (0x1 << DUT_RESETN_SHIFT);
	iowrite32(reg_reset_n, apollo->tcf_registers +
		  TCF_CLK_CTRL_CLK_AND_RST_CTRL);
	msleep(100);

	/* Try to enable the core clock PLL */
	spi_write(apollo, 0x1, 0x0);
	reg  = ioread32(apollo->tcf_registers + 0x320);
	reg |= 0x1;
	iowrite32(reg, apollo->tcf_registers + 0x320);
	reg &= 0xfffffffe;
	iowrite32(reg, apollo->tcf_registers + 0x320);
	msleep(1000);

	if (spi_read(apollo, 0x2, &reg))
		dev_err(&apollo->pdev->dev,
				"Unable to read PLL status\n");

	if (reg == 0x1) {
		/* Select DUT PLL as core clock */
		reg  = ioread32(apollo->tcf_registers + 0x108);
		reg &= 0xfffffff7;
		iowrite32(reg, apollo->tcf_registers + 0x108);
	} else {
		dev_err(&apollo->pdev->dev,
			"PLL has failed to lock, status = %x\n", reg);
	}

	while (!aligned && reset_cnt < 10) {
		int bank;
		u32 eyes;
		u32 clk_taps;
		u32 train_ack;

		++reset_cnt;

		/* Reset the DUT to allow the SAI to retrain */
		reg_reset_n &= ~(0x1 << DUT_RESETN_SHIFT);
		iowrite32(reg_reset_n, apollo->tcf_registers +
			  TCF_CLK_CTRL_CLK_AND_RST_CTRL);
		udelay(100);
		reg_reset_n |= (0x1 << DUT_RESETN_SHIFT);
		iowrite32(reg_reset_n, apollo->tcf_registers +
			  TCF_CLK_CTRL_CLK_AND_RST_CTRL);
		udelay(100);

		/* Assume alignment passed, if any bank fails on either DUT or
		 * FPGA we will set this to false and try again for a max of 10
		 * times. */
		aligned = 1;

		/* For each of the banks */
		for (bank = 0; bank < 10; bank++) {
			/* Check alignment on the DUT */
			u32 bank_base = 0x7000 + (0x1000 * bank);

			spi_read(apollo, bank_base + 0x4, &eyes);
			spi_read(apollo, bank_base + 0x3, &clk_taps);
			spi_read(apollo, bank_base + 0x6, &train_ack);

			if (!is_interface_aligned(eyes, clk_taps, train_ack)) {
				dev_warn(&apollo->pdev->dev, "Alignment check failed, retrying\n");
				aligned = 0;
				break;
			}

			/* Check alignment on the FPGA */
			bank_base = 0xb0 + (0x10 * bank);

			eyes = sai_read(apollo, bank_base + 0x4);
			clk_taps = sai_read(apollo, bank_base + 0x3);
			train_ack = sai_read(apollo, bank_base + 0x6);

			if (!is_interface_aligned(eyes, clk_taps, train_ack)) {
				dev_warn(&apollo->pdev->dev, "Alignment check failed, retrying\n");
				aligned = 0;
				break;
			}
		}
	}

	if (!aligned) {
		dev_err(&apollo->pdev->dev,
				"Unable to intialise the testchip (interface alignment failure), "
				"please restart the system.\n");
		return -1;
	}

	if (reset_cnt > 1) {
		dev_dbg(&apollo->pdev->dev, "Note: The testchip required more than one reset to find a good interface alignment!\n");
		dev_dbg(&apollo->pdev->dev, "      This should be harmless, but if you do suspect foul play, please reset the machine.\n");
		dev_dbg(&apollo->pdev->dev, "      If you continue to see this message you may want to report it to IMGWORKS.\n");
	}

#if !((RGX_BVNC_KM_B == 4) && (RGX_BVNC_KM_V == 31) && \
	 (RGX_BVNC_KM_N == 4) && (RGX_BVNC_KM_C == 55))
	/* Enable the temperature sensor */
	spi_write(apollo, 0xc, 0); /* power up */
	spi_write(apollo, 0xc, 2); /* reset */
	spi_write(apollo, 0xc, 6); /* init & run */
#endif

	/* Check the build */
	reg = ioread32(apollo->tcf_registers + 0x10);
	build_inc = (reg >> 12) & 0xff;
	build_owner = (reg >> 20) & 0xf;

	if (build_inc) {
		dev_alert(&apollo->pdev->dev, "BE WARNED: You are not running a tagged release of the FPGA image!\n");
		dev_alert(&apollo->pdev->dev, "Owner: 0x%01x, Inc: 0x%02x\n",
			  build_owner, build_inc);
	}

	dev_dbg(&apollo->pdev->dev, "FPGA Release: %u.%02u\n", reg >> 8 & 0xf,
		reg & 0xff);

	return 0;
}

static int apollo_hw_init(struct apollo_device *apollo)
{
	apollo_hard_reset(apollo);
	apollo_init_memory(apollo);

#if ((RGX_BVNC_KM_B == 4) && (RGX_BVNC_KM_V == 31) && \
	 (RGX_BVNC_KM_N == 4) && (RGX_BVNC_KM_C == 55))
	{	
		u32 reg;
		/* Enable ASTC via SPI */
		if (spi_read(apollo, 0xf, &reg)) {
			dev_err(&apollo->pdev->dev, "Failed to read apollo ASTC register\n");
			goto err_out;
		}    

		reg |= 0x1 << 4;
		spi_write(apollo, 0xf, reg);
	}
err_out:
#endif 

	return 0;
}

static void apollo_hw_fini(struct apollo_device *apollo)
{
	apollo_deinit_memory(apollo);
}

int apollo_sys_info(struct device *dev, u32 *tmp, u32 *pll)
{
	int err = -ENODEV;
	struct apollo_device *apollo = devres_find(dev, apollo_devres_release,
		NULL, NULL);

	if (!apollo) {
		dev_err(dev, "No apollo device resources found\n");
		goto err_out;
	}

#if !((RGX_BVNC_KM_B == 4) && (RGX_BVNC_KM_V == 31) && \
	  (RGX_BVNC_KM_N == 4) && (RGX_BVNC_KM_C == 55))
	if (spi_read(apollo, TCF_TEMP_SENSOR_SPI_OFFSET, tmp)) {
		dev_err(dev, "Failed to read apollo temperature sensor\n");
		goto err_out;
	}
	
	*tmp = TCF_TEMP_SENSOR_TO_C(*tmp);
#endif

	if (spi_read(apollo, 0x2, pll)) {
		dev_err(dev, "Failed to read PLL status\n");
		goto err_out;
	}

	err = 0;
err_out:
	return err;
}
EXPORT_SYMBOL(apollo_sys_info);

#else

static int iopol32(u32 val, u32 mask, void __iomem *addr)
{
	int polnum;
	for (polnum = 0; polnum < 500; polnum++) {
		if ((ioread32(addr) & mask) == val)
			break;
		/* NOTE: msleep() < 20 ms may sleep up to 20ms */
		msleep(1);
	}
	if (polnum == 500) {
		pr_err(DRV_NAME " iopol32 timeout\n");
		return -ETIME;
	}
	return 0;
}


static int apollo_rogue_bist(struct apollo_device *apollo)
{
	int err = 0;
	void __iomem *rogue_regs;
	resource_size_t regs_resource;
	resource_size_t regs_resource_size;
	int i;
	int instance;

	regs_resource =
		pci_resource_start(apollo->pdev, SYS_RGX_REG_PCI_BASENUM);
	regs_resource_size =
		pci_resource_len(apollo->pdev, SYS_RGX_REG_PCI_BASENUM);

	if (regs_resource_size < SYS_RGX_REG_REGION_SIZE) {
		dev_err(&apollo->pdev->dev,
			"Rogue register region too small\n");
		err = -EIO;
		goto err_out;
	}

	err = pci_request_region(apollo->pdev, SYS_RGX_REG_PCI_BASENUM,
		DRV_NAME);
	if (err) {
		dev_err(&apollo->pdev->dev,
			"Failed to request rogue register region\n");
		goto err_out;
	}
	rogue_regs = ioremap_nocache(regs_resource, regs_resource_size);
	if (!rogue_regs) {
		dev_err(&apollo->pdev->dev,
			"Failed to map rogue register region\n");
		err = -EIO;
		goto err_release_registers;
	}
	/* Force clocks on */
	iowrite32(0x55555555, rogue_regs + 0);
	iowrite32(0x55555555, rogue_regs + 4);

	iopol32(0x05000000, 0x05000000, rogue_regs + 0xa18);
	iowrite32(0x048000b0, rogue_regs + 0xa10);
	iowrite32(0x55111111, rogue_regs + 0xa08);
	iopol32(0x05000000, 0x05000000, rogue_regs + 0xa18);

	/* Clear PDS CSRM and USRM to prevent ERRORs at end of test */
	iowrite32(0x1, rogue_regs + 0x630);
	iowrite32(0x1, rogue_regs + 0x648);
	iowrite32(0x1, rogue_regs + 0x608);

	/* Run BIST for SLC (43) */
	/* Reset BIST */
	iowrite32(0x8, rogue_regs + 0x7000);
	udelay(100);

	/* Clear BIST controller */
	iowrite32(0x10, rogue_regs + 0x7000);
	iowrite32(0, rogue_regs + 0x7000);
	udelay(100);

	for (i = 0; i < 3; i++) {
		u32 polval = i == 2 ? 0x10000 : 0x20000;

		/* Start BIST */
		iowrite32(0x4, rogue_regs + 0x7000);

		udelay(100);

		/* Wait for pause */
		iopol32(polval, polval, rogue_regs + 0x7000);
	}
	udelay(100);

	/* Check results for 43 RAMs */
	iopol32(0xffffffff, 0xffffffff, rogue_regs + 0x7010);
	iopol32(0x7, 0x7, rogue_regs + 0x7014);

	iowrite32(8, rogue_regs + 0x7000);
	iowrite32(0, rogue_regs + 0x7008);
	iowrite32(6, rogue_regs + 0x7000);
	iopol32(0x00010000, 0x00010000, rogue_regs + 0x7000);
	udelay(100);

	iopol32(0, ~0U, rogue_regs + 0x75B0);
	iopol32(0, ~0U, rogue_regs + 0x75B4);
	iopol32(0, ~0U, rogue_regs + 0x75B8);
	iopol32(0, ~0U, rogue_regs + 0x75BC);
	iopol32(0, ~0U, rogue_regs + 0x75C0);
	iopol32(0, ~0U, rogue_regs + 0x75C4);
	iopol32(0, ~0U, rogue_regs + 0x75C8);
	iopol32(0, ~0U, rogue_regs + 0x75CC);

	/* Sidekick */
	iowrite32(8, rogue_regs + 0x7040);
	udelay(100);

	iowrite32(0x10, rogue_regs + 0x7040);
	udelay(100);

	for (i = 0; i < 3; i++) {
		u32 polval = i == 2 ? 0x10000 : 0x20000;

		iowrite32(4, rogue_regs + 0x7040);
		udelay(100);
		iopol32(polval, polval, rogue_regs + 0x7040);
	}

	udelay(100);
	iopol32(0xffffffff, 0xffffffff, rogue_regs + 0x7050);
	iopol32(0xffffffff, 0xffffffff, rogue_regs + 0x7054);
	iopol32(0x1, 0x1, rogue_regs + 0x7058);

	/* USC */
	for (instance = 0; instance < 4; instance++) {
		iowrite32(instance, rogue_regs + 0x8010);

		iowrite32(8, rogue_regs + 0x7088);
		udelay(100);

		iowrite32(0x10, rogue_regs + 0x7088);
		udelay(100);

		for (i = 0; i < 3; i++) {
			u32 polval = i == 2 ? 0x10000 : 0x20000;

			iowrite32(4, rogue_regs + 0x7088);
			udelay(100);
			iopol32(polval, polval, rogue_regs + 0x7088);
		}

		udelay(100);
		iopol32(0xffffffff, 0xffffffff, rogue_regs + 0x7098);
		iopol32(0xffffffff, 0xffffffff, rogue_regs + 0x709c);
		iopol32(0x3f, 0x3f, rogue_regs + 0x70a0);
	}

	/* tpumcul0 DustA and DustB */
	for (instance = 0; instance < 2; instance++) {
		iowrite32(instance, rogue_regs + 0x8018);

		iowrite32(8, rogue_regs + 0x7380);
		udelay(100);

		iowrite32(0x10, rogue_regs + 0x7380);
		udelay(100);

		for (i = 0; i < 3; i++) {
			u32 polval = i == 2 ? 0x10000 : 0x20000;

			iowrite32(4, rogue_regs + 0x7380);
			udelay(100);
			iopol32(polval, polval, rogue_regs + 0x7380);
		}

		udelay(100);
		iopol32(0x1fff, 0x1fff, rogue_regs + 0x7390);
	}

	/* TA */
	iowrite32(8, rogue_regs + 0x7500);
	udelay(100);

	iowrite32(0x10, rogue_regs + 0x7500);
	udelay(100);

	for (i = 0; i < 3; i++) {
		u32 polval = i == 2 ? 0x10000 : 0x20000;

		iowrite32(4, rogue_regs + 0x7500);
		udelay(100);
		iopol32(polval, polval, rogue_regs + 0x7500);
	}

	udelay(100);
	iopol32(0x1fffffff, 0x1fffffff, rogue_regs + 0x7510);

	/* Rasterisation */
	iowrite32(8, rogue_regs + 0x7540);
	udelay(100);

	iowrite32(0x10, rogue_regs + 0x7540);
	udelay(100);

	for (i = 0; i < 3; i++) {
		u32 polval = i == 2 ? 0x10000 : 0x20000;

		iowrite32(4, rogue_regs + 0x7540);
		udelay(100);
		iopol32(polval, polval, rogue_regs + 0x7540);
	}

	udelay(100);
	iopol32(0xffffffff, 0xffffffff, rogue_regs + 0x7550);
	iopol32(0xffffffff, 0xffffffff, rogue_regs + 0x7554);
	iopol32(0xf, 0xf, rogue_regs + 0x7558);

	/* hub_bifpmache */
	iowrite32(8, rogue_regs + 0x7588);
	udelay(100);

	iowrite32(0x10, rogue_regs + 0x7588);
	udelay(100);

	for (i = 0; i < 3; i++) {
		u32 polval = i == 2 ? 0x10000 : 0x20000;

		iowrite32(4, rogue_regs + 0x7588);
		udelay(100);
		iopol32(polval, polval, rogue_regs + 0x7588);
	}

	udelay(100);
	iopol32(0xffffffff, 0xffffffff, rogue_regs + 0x7598);
	iopol32(0xffffffff, 0xffffffff, rogue_regs + 0x759c);
	iopol32(0x1111111f, 0x1111111f, rogue_regs + 0x75a0);

	iounmap(rogue_regs);
err_release_registers:
	pci_release_region(apollo->pdev, SYS_RGX_REG_PCI_BASENUM);
err_out:
	return err;
}

static void apollo_hard_reset(struct apollo_device *apollo)
{
	u32 reg;

	reg =  (0x1 << GLB_CLKG_EN_SHIFT);
	reg |= (0x1 << SCB_RESETN_SHIFT);
	reg |= (0x1 << PDP2_RESETN_SHIFT);
	reg |= (0x1 << PDP1_RESETN_SHIFT);
	iowrite32(reg, apollo->tcf_registers + TCF_CLK_CTRL_CLK_AND_RST_CTRL);

	reg |= (0x1 << DDR_RESETN_SHIFT);
	iowrite32(reg, apollo->tcf_registers + TCF_CLK_CTRL_CLK_AND_RST_CTRL);

	reg |= (0x1 << DUT_RESETN_SHIFT);
	iowrite32(reg, apollo->tcf_registers + TCF_CLK_CTRL_CLK_AND_RST_CTRL);

	reg |= (0x1 << DUT_DCM_RESETN_SHIFT);
	iowrite32(reg, apollo->tcf_registers + TCF_CLK_CTRL_CLK_AND_RST_CTRL);

	/* NOTE: msleep() < 20 ms may sleep up to 20ms */
	msleep(4);
	iopol32(0x7, DCM_LOCK_STATUS_MASK,
		apollo->tcf_registers + TCF_CLK_CTRL_DCM_LOCK_STATUS);
}

static int apollo_set_rogue_pll(struct apollo_device *apollo)
{
	int err = 0;
	/* Enable the rogue PLL (defaults to 3x), giving a Rogue clock of
	 * 3 x RGX_TC_CORE_CLOCK_SPEED */
	u32 val = ioread32(apollo->tcf_registers + TCF_CLK_CTRL_DUT_CONTROL_1);
	iowrite32(val & 0xFFFFFFFB,
		apollo->tcf_registers + TCF_CLK_CTRL_DUT_CONTROL_1);

	return err;
}

static int apollo_hw_init(struct apollo_device *apollo)
{
	int err;

	apollo_hard_reset(apollo);
	err = apollo_rogue_bist(apollo);
	if (err) {
		dev_err(&apollo->pdev->dev, "Failed to run BIST on rogue\n");
		goto exit;
	}
	apollo_hard_reset(apollo);
	apollo_init_memory(apollo);

	err = apollo_set_clocks(apollo);
	if (err) {
		dev_err(&apollo->pdev->dev, "Failed to init clocks\n");
		goto exit;
	}

	err = apollo_set_rogue_pll(apollo);
	if (err) {
		dev_err(&apollo->pdev->dev, "Failed to set rogue PLL\n");
		goto exit;
	}

exit:
	return err;
}

static void apollo_hw_fini(struct apollo_device *apollo)
{
	apollo_deinit_memory(apollo);
}

int apollo_sys_info(struct device *dev, u32 *tmp, u32 *pll)
{
	/* Not implemented for TC1 */
	return -1;
}
EXPORT_SYMBOL(apollo_sys_info);

#endif

int apollo_core_clock_speed(struct device *dev)
{
	return apollo_core_clock;
}
EXPORT_SYMBOL(apollo_core_clock_speed);

#define HEX2DEC(v) ((((v) >> 4) * 10) + ((v) & 0x0F))
int apollo_sys_strings(struct device *dev,
		       char *str_fpga_rev, size_t size_fpga_rev,
		       char *str_tcf_core_rev, size_t size_tcf_core_rev,
		       char *str_tcf_core_target_build_id,
		       size_t size_tcf_core_target_build_id,
		       char *str_pci_ver, size_t size_pci_ver,
		       char *str_macro_ver, size_t size_macro_ver)
{
	int err = 0;
	u32 val;
	resource_size_t host_fpga_register_resource;
	void __iomem *host_fpga_registers;

	struct apollo_device *apollo = devres_find(dev, apollo_devres_release,
		NULL, NULL);

	if (!str_fpga_rev || !size_fpga_rev ||
	    !str_tcf_core_rev || !size_tcf_core_rev ||
	    !str_tcf_core_target_build_id || !size_tcf_core_target_build_id ||
	    !str_pci_ver || !size_pci_ver ||
	    !str_macro_ver || !size_macro_ver) {
		err = -EINVAL;
		goto err_out;
	}

	if (!apollo) {
		dev_err(dev, "No apollo device resources found\n");
		err = -ENODEV;
		goto err_out;
	}

	/* To get some of the version information we need to read from a
	   register that we don't normally have mapped. Map it temporarily
	   (without trying to reserve it) to get the information we need. */
	host_fpga_register_resource =
		pci_resource_start(apollo->pdev, SYS_APOLLO_REG_PCI_BASENUM)
		+ 0x40F0;

	host_fpga_registers = ioremap_nocache(host_fpga_register_resource,
					      0x04);
	if (!host_fpga_registers) {
		dev_err(&apollo->pdev->dev, "Failed to map host fpga registers\n");
		err = -EIO;
		goto err_out;
	}

	/* Create the components of the PCI and macro versions */
	val = ioread32(host_fpga_registers);
	snprintf(str_pci_ver, size_pci_ver, "%d",
		 HEX2DEC((val & 0x00FF0000) >> 16));
	snprintf(str_macro_ver, size_macro_ver, "%d.%d",
		 (val & 0x00000F00) >> 8,
		 HEX2DEC((val & 0x000000FF) >> 0));

	/* Unmap the register now that we no longer need it */
	iounmap(host_fpga_registers);

	/* Create the components of the FPGA revision number */
	val = ioread32(apollo->tcf_registers + TCF_CLK_CTRL_FPGA_REV_REG);
	snprintf(str_fpga_rev, size_fpga_rev, "%d.%d.%d",
		 HEX2DEC((val & FPGA_REV_REG_MAJOR_MASK)
			 >> FPGA_REV_REG_MAJOR_SHIFT),
		 HEX2DEC((val & FPGA_REV_REG_MINOR_MASK)
			 >> FPGA_REV_REG_MINOR_SHIFT),
		 HEX2DEC((val & FPGA_REV_REG_MAINT_MASK)
			 >> FPGA_REV_REG_MAINT_SHIFT));

	/* Create the components of the TCF core revision number */
	val = ioread32(apollo->tcf_registers + TCF_CLK_CTRL_TCF_CORE_REV_REG);
	snprintf(str_tcf_core_rev, size_tcf_core_rev, "%d.%d.%d",
		 HEX2DEC((val & TCF_CORE_REV_REG_MAJOR_MASK)
			 >> TCF_CORE_REV_REG_MAJOR_SHIFT),
		 HEX2DEC((val & TCF_CORE_REV_REG_MINOR_MASK)
			 >> TCF_CORE_REV_REG_MINOR_SHIFT),
		 HEX2DEC((val & TCF_CORE_REV_REG_MAINT_MASK)
			 >> TCF_CORE_REV_REG_MAINT_SHIFT));

	/* Create the component of the TCF core target build ID */
	val = ioread32(apollo->tcf_registers +
		       TCF_CLK_CTRL_TCF_CORE_TARGET_BUILD_CFG);
	snprintf(str_tcf_core_target_build_id, size_tcf_core_target_build_id,
		 "%d",
		 (val & TCF_CORE_TARGET_BUILD_ID_MASK)
		 >> TCF_CORE_TARGET_BUILD_ID_SHIFT);

err_out:
	return err;
}
EXPORT_SYMBOL(apollo_sys_strings);

static irqreturn_t apollo_irq_handler(int irq, void *data)
{
	u32 interrupt_status;
	u32 interrupt_clear = 0;
	unsigned long flags;
	irqreturn_t ret = IRQ_NONE;
	struct apollo_device *apollo = (struct apollo_device *)data;

	spin_lock_irqsave(&apollo->interrupt_handler_lock, flags);

	interrupt_status = ioread32(apollo->tcf_registers
		+ TCF_CLK_CTRL_INTERRUPT_STATUS);

	if (interrupt_status & APOLLO_INTERRUPT_FLAG_ROGUE) {
		if (apollo->interrupt_handlers[APOLLO_INTERRUPT_ROGUE].enabled
		    && apollo->interrupt_handlers[APOLLO_INTERRUPT_ROGUE]
			.handler_function) {
			apollo->interrupt_handlers[APOLLO_INTERRUPT_ROGUE]
				.handler_function(apollo->interrupt_handlers
					[APOLLO_INTERRUPT_ROGUE].handler_data);
			interrupt_clear |= APOLLO_INTERRUPT_FLAG_ROGUE;
		}
		ret = IRQ_HANDLED;
	}
	if (interrupt_status & APOLLO_INTERRUPT_FLAG_PDP) {
		if (apollo->interrupt_handlers[APOLLO_INTERRUPT_PDP].enabled
		    && apollo->interrupt_handlers[APOLLO_INTERRUPT_PDP]
			.handler_function) {
			apollo->interrupt_handlers[APOLLO_INTERRUPT_PDP]
				.handler_function(apollo->interrupt_handlers
					[APOLLO_INTERRUPT_PDP].handler_data);
			interrupt_clear |= APOLLO_INTERRUPT_FLAG_PDP;
		}
		ret = IRQ_HANDLED;
	}

	if (interrupt_clear)
		iowrite32(0xffffffff,
			apollo->tcf_registers + TCF_CLK_CTRL_INTERRUPT_CLEAR);

	spin_unlock_irqrestore(&apollo->interrupt_handler_lock, flags);

	return ret;
}

static int apollo_enable_irq(struct apollo_device *apollo)
{
	int err = 0;
	iowrite32(0, apollo->tcf_registers + TCF_CLK_CTRL_INTERRUPT_ENABLE);
	iowrite32(0xffffffff,
		apollo->tcf_registers + TCF_CLK_CTRL_INTERRUPT_CLEAR);
	err = request_irq(apollo->pdev->irq, apollo_irq_handler, IRQF_SHARED,
		DRV_NAME, apollo);
	return err;
}

static void apollo_disable_irq(struct apollo_device *apollo)
{
	iowrite32(0, apollo->tcf_registers + TCF_CLK_CTRL_INTERRUPT_ENABLE);
	iowrite32(0xffffffff,
		apollo->tcf_registers + TCF_CLK_CTRL_INTERRUPT_CLEAR);
	free_irq(apollo->pdev->irq, apollo);
}

static int register_pdp_device(struct apollo_device *apollo)
{
	int err = 0;
	struct apollo_pdp_platform_data pdata = {
		.pdev = apollo->pdev,
#if defined(SUPPORT_ION)
		.ion_device = apollo->ion_device,
		.ion_heap_id = ION_HEAP_APOLLO_PDP,
#endif
		.apollo_memory_base = apollo->apollo_mem_base,
	};
	struct platform_device_info pdp_device_info = {
		.parent = &apollo->pdev->dev,
		.name = APOLLO_DEVICE_NAME_PDP,
		.id = -1,
		.res = NULL,
		.num_res = 0,
		.data = &pdata,
		.size_data = sizeof(pdata),
		.dma_mask = DMA_BIT_MASK(32),
	};

	apollo->pdp_dev = platform_device_register_full(&pdp_device_info);
	if (IS_ERR(apollo->pdp_dev)) {
		err = PTR_ERR(apollo->pdp_dev);
		dev_err(&apollo->pdev->dev,
			"Failed to register PDP device (%d)\n", err);
		apollo->pdp_dev = NULL;
		goto err;
	}
err:
	return err;
}

static int register_rogue_device(struct apollo_device *apollo)
{
	int err = 0;
	struct apollo_rogue_platform_data pdata = {
		.pdev = apollo->pdev,
#if defined(SUPPORT_ION)
		.ion_device = apollo->ion_device,
		.ion_heap_id = ION_HEAP_APOLLO_ROGUE,
#endif
		.apollo_memory_base = apollo->apollo_mem_base,
		.pdp_heap_memory_base = apollo->pdp_heap_mem_base,
		.pdp_heap_memory_size = apollo->pdp_heap_mem_size,
		.rogue_heap_memory_base = apollo->rogue_heap_mem_base,
		.rogue_heap_memory_size = apollo->rogue_heap_mem_size,
	};
	struct platform_device_info rogue_device_info = {
		.parent = &apollo->pdev->dev,
		.name = APOLLO_DEVICE_NAME_ROGUE,
		.id = -1,
		.res = NULL,
		.num_res = 0,
		.data = &pdata,
		.size_data = sizeof(pdata),
		.dma_mask = DMA_BIT_MASK(32),
	};
	apollo->rogue_dev = platform_device_register_full(&rogue_device_info);
	if (IS_ERR(apollo->rogue_dev)) {
		err = PTR_ERR(apollo->rogue_dev);
		dev_err(&apollo->pdev->dev,
			"Failed to register rogue device (%d)\n", err);
		apollo->rogue_dev = NULL;
		goto err;
	}
err:
	return err;
}

#if defined(SUPPORT_ION)
static int apollo_ion_init(struct apollo_device *apollo)
{
	int i, err = 0;
	struct ion_platform_heap ion_heap_data[APOLLO_ION_HEAP_COUNT] = {
		{
			.type = ION_HEAP_TYPE_SYSTEM,
			.id = ION_HEAP_TYPE_SYSTEM,
			.name = "system",
		},
		{
			.type = ION_HEAP_TYPE_CUSTOM,
			.id = ION_HEAP_APOLLO_PDP,
			.size = apollo->pdp_heap_mem_size,
			.base = apollo->pdp_heap_mem_base,
			.name = "apollo-pdp",
		},
		{
			.type = ION_HEAP_TYPE_CUSTOM,
			.id = ION_HEAP_APOLLO_ROGUE,
			.size = apollo->rogue_heap_mem_size,
			.base = apollo->rogue_heap_mem_base,
			.name = "apollo-rogue",
		}
	};

	apollo->ion_device = ion_device_create(NULL);
	if (IS_ERR_OR_NULL(apollo->ion_device)) {
		err = PTR_ERR(apollo->ion_device);
		goto err_out;
	}

	err = request_pci_io_addr(apollo->pdev, APOLLO_MEM_PCI_BASENUM, 0,
		apollo->apollo_mem_size);
	if (err) {
		dev_err(&apollo->pdev->dev,
			"Failed to request APOLLO memory (%d)\n", err);
		goto err_free_device;
	}

	apollo->ion_heaps[0] = ion_heap_create(&ion_heap_data[0]);
	if (IS_ERR_OR_NULL(apollo->ion_heaps[0])) {
		err = PTR_ERR(apollo->ion_heaps[0]);
		apollo->ion_heaps[0] = NULL;
		goto err_free_device;
	}
	ion_device_add_heap(apollo->ion_device, apollo->ion_heaps[0]);

	for (i = 1; i < APOLLO_ION_HEAP_COUNT; i++) {
		apollo->ion_heaps[i] = ion_lma_heap_create(&ion_heap_data[i]);
		if (IS_ERR_OR_NULL(apollo->ion_heaps[i])) {
			err = PTR_ERR(apollo->ion_heaps[i]);
			apollo->ion_heaps[i] = NULL;
			goto err_free_heaps;
		}
		ion_device_add_heap(apollo->ion_device, apollo->ion_heaps[i]);
	}

	return 0;

err_free_heaps:
	ion_heap_destroy(apollo->ion_heaps[0]);

	for (i = 1; i < APOLLO_ION_HEAP_COUNT; i++) {
		if (!apollo->ion_heaps[i])
			break;
		ion_lma_heap_destroy(apollo->ion_heaps[i]);
	}

	release_pci_io_addr(apollo->pdev, APOLLO_MEM_PCI_BASENUM,
		apollo->apollo_mem_base, apollo->apollo_mem_size);
err_free_device:
	ion_device_destroy(apollo->ion_device);
err_out:
	/* If the ptr was NULL, it is possible that err is 0 in the err path */
	if (err == 0)
		err = -ENOMEM;
	return err;
}

static void apollo_ion_deinit(struct apollo_device *apollo)
{
	int i = 0;
	ion_device_destroy(apollo->ion_device);
	ion_heap_destroy(apollo->ion_heaps[0]);
	for (i = 1; i < APOLLO_ION_HEAP_COUNT; i++)
		ion_lma_heap_destroy(apollo->ion_heaps[i]);
	release_pci_io_addr(apollo->pdev, APOLLO_MEM_PCI_BASENUM,
		apollo->apollo_mem_base, apollo->apollo_mem_size);
}
#endif /* defined(SUPPORT_ION) */

static int apollo_dev_init(struct apollo_device *apollo, struct pci_dev *pdev)
{
	int err;

	apollo->pdev = pdev;

	spin_lock_init(&apollo->interrupt_handler_lock);
	spin_lock_init(&apollo->interrupt_enable_lock);

	/* Reserve and map the TCF registers */
	err = request_pci_io_addr(pdev, SYS_APOLLO_REG_PCI_BASENUM,
		SYS_APOLLO_REG_SYS_OFFSET, SYS_APOLLO_REG_SYS_SIZE);
	if (err) {
		dev_err(&pdev->dev,
			"Failed to request apollo registers (err=%d)\n", err);
		return err;
	}

	apollo->tcf_register_resource =
		pci_resource_start(pdev, SYS_APOLLO_REG_PCI_BASENUM)
		+ SYS_APOLLO_REG_SYS_OFFSET;

	apollo->tcf_registers = ioremap_nocache(apollo->tcf_register_resource,
		SYS_APOLLO_REG_SYS_SIZE);
	if (!apollo->tcf_registers) {
		dev_err(&pdev->dev, "Failed to map TCF registers\n");
		err = -EIO;
		goto err_release_registers;
	}

	/* Setup card memory */
	apollo->apollo_mem_base =
		pci_resource_start(pdev, APOLLO_MEM_PCI_BASENUM);
	apollo->apollo_mem_size =
		pci_resource_len(pdev, APOLLO_MEM_PCI_BASENUM);

	err = mtrr_setup(pdev, apollo->apollo_mem_base,
			 apollo->apollo_mem_size);
	if (err)
		goto err_unmap_registers;

	/* Setup ranges for the device heaps */
	apollo->pdp_heap_mem_size = APOLLO_PDP_MEM_SIZE;
	apollo->rogue_heap_mem_size = apollo->apollo_mem_size
		- apollo->pdp_heap_mem_size;

	apollo->rogue_heap_mem_base = apollo->apollo_mem_base;
	apollo->pdp_heap_mem_base = apollo->apollo_mem_base +
		apollo->rogue_heap_mem_size;

#if defined(SUPPORT_ION)
	err = apollo_ion_init(apollo);
	if (err) {
		dev_err(&pdev->dev, "Failed to initialise ION\n");
		goto err_unmap_registers;
	}
#endif

	return 0;

err_unmap_registers:
	iounmap(apollo->tcf_registers);
err_release_registers:
	release_pci_io_addr(pdev, SYS_APOLLO_REG_PCI_BASENUM,
		apollo->tcf_register_resource, SYS_APOLLO_REG_SYS_SIZE);
	return err;
}

static void apollo_dev_cleanup(struct apollo_device *apollo)
{
#if defined(SUPPORT_ION)
	apollo_ion_deinit(apollo);
#endif
	iounmap(apollo->tcf_registers);
	release_pci_io_addr(apollo->pdev, SYS_APOLLO_REG_PCI_BASENUM,
		apollo->tcf_register_resource, SYS_APOLLO_REG_SYS_SIZE);
}

static int apollo_init(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct apollo_device *apollo;
	int err = 0;
	u32 val;

	if (!devres_open_group(&pdev->dev, NULL, GFP_KERNEL))
		return -ENOMEM;

	apollo = devres_alloc(apollo_devres_release,
		sizeof(struct apollo_device), GFP_KERNEL);
	if (!apollo) {
		err = -ENOMEM;
		goto err_out;
	}
	devres_add(&pdev->dev, apollo);

	err = pci_enable_device(pdev);
	if (err) {
		dev_err(&pdev->dev, "pci_enable_device returned %d\n", err);
		goto err_out;
	}

	err = apollo_dev_init(apollo, pdev);
	if (err)
		goto err_disable_device;

	err = apollo_hw_init(apollo);
	if (err)
		goto err_dev_cleanup;

	err = apollo_enable_irq(apollo);
	if (err) {
		dev_err(&pdev->dev, "Failed to initialise IRQ\n");
		goto err_hw_fini;
	}

	/* Set sense to active high */
	val = ioread32(apollo->tcf_registers + TCF_CLK_CTRL_INTERRUPT_OP_CFG);
	val &= ~(INT_SENSE_MASK);
	iowrite32(val, apollo->tcf_registers + TCF_CLK_CTRL_INTERRUPT_OP_CFG);

	/* Register rogue and pdp platform devices
	 * Failures here aren't critical? */
	register_pdp_device(apollo);
	register_rogue_device(apollo);

	devres_remove_group(&pdev->dev, NULL);

	return err;

err_hw_fini:
	apollo_hw_fini(apollo);
err_dev_cleanup:
	apollo_dev_cleanup(apollo);
err_disable_device:
	pci_disable_device(pdev);
err_out:
	devres_release_group(&pdev->dev, NULL);
	dev_err(&pdev->dev, "Failed to initialise apollo device\n");
	return err;
}

static void apollo_exit(struct pci_dev *pdev)
{
	int i;
	struct apollo_device *apollo = devres_find(&pdev->dev,
		apollo_devres_release, NULL, NULL);

	if (apollo->pdp_dev)
		platform_device_unregister(apollo->pdp_dev);

	if (apollo->rogue_dev)
		platform_device_unregister(apollo->rogue_dev);

	for (i = 0; i < APOLLO_INTERRUPT_COUNT; i++)
		apollo_disable_interrupt(&pdev->dev, i);
	apollo_disable_irq(apollo);
	apollo_hw_fini(apollo);
	apollo_dev_cleanup(apollo);
	pci_disable_device(pdev);
}

DEFINE_PCI_DEVICE_TABLE(apollo_pci_tbl) = {
	{ PCI_VDEVICE(POWERVR, DEVICE_ID_PCI_APOLLO_FPGA) },
	{ PCI_VDEVICE(POWERVR, DEVICE_ID_PCIE_APOLLO_FPGA) },
	{ },
};

static struct pci_driver apollo_pci_driver = {
	.name		= DRV_NAME,
	.id_table	= apollo_pci_tbl,
	.probe		= apollo_init,
	.remove		= apollo_exit,
};

module_pci_driver(apollo_pci_driver);

MODULE_DEVICE_TABLE(pci, apollo_pci_tbl);

static u32 apollo_interrupt_id_to_flag(int interrupt_id)
{
	switch (interrupt_id) {
	case APOLLO_INTERRUPT_PDP:
		return APOLLO_INTERRUPT_FLAG_PDP;
	case APOLLO_INTERRUPT_ROGUE:
		return APOLLO_INTERRUPT_FLAG_ROGUE;
	default:
		BUG();
	}
}

static void apollo_enable_interrupt_register(struct apollo_device *apollo,
	int interrupt_id)
{
	u32 val;
	val = ioread32(apollo->tcf_registers + TCF_CLK_CTRL_INTERRUPT_ENABLE);
	val |= apollo_interrupt_id_to_flag(interrupt_id);
	iowrite32(val, apollo->tcf_registers + TCF_CLK_CTRL_INTERRUPT_ENABLE);

}

static void apollo_disable_interrupt_register(struct apollo_device *apollo,
	int interrupt_id)
{
	u32 val;
	val = ioread32(apollo->tcf_registers + TCF_CLK_CTRL_INTERRUPT_ENABLE);
	val &= ~(apollo_interrupt_id_to_flag(interrupt_id));
	iowrite32(val, apollo->tcf_registers + TCF_CLK_CTRL_INTERRUPT_ENABLE);
}

int apollo_set_interrupt_handler(struct device *dev, int interrupt_id,
	void (*handler_function)(void *), void *data)
{
	struct apollo_device *apollo = devres_find(dev, apollo_devres_release,
		NULL, NULL);
	int err = 0;
	unsigned long flags;
	if (!apollo) {
		dev_err(dev, "No apollo device resources found\n");
		err = -ENODEV;
		goto err_out;
	}

	if (interrupt_id < 0 || interrupt_id >= APOLLO_INTERRUPT_COUNT) {
		dev_err(dev, "Invalid interrupt ID (%d)\n", interrupt_id);
		err = -EINVAL;
		goto err_out;
	}

	if (!handler_function) {
		dev_err(dev, "Invalid handler function\n");
		err = -EINVAL;
		goto err_out;
	}

	spin_lock_irqsave(&apollo->interrupt_handler_lock, flags);

	apollo->interrupt_handlers[interrupt_id].handler_function =
		handler_function;
	apollo->interrupt_handlers[interrupt_id].handler_data = data;

	spin_unlock_irqrestore(&apollo->interrupt_handler_lock, flags);

err_out:
	return err;
}
EXPORT_SYMBOL(apollo_set_interrupt_handler);

int apollo_enable_interrupt(struct device *dev, int interrupt_id)
{
	struct apollo_device *apollo = devres_find(dev, apollo_devres_release,
		NULL, NULL);
	int err = 0;
	unsigned long flags;
	if (!apollo) {
		dev_err(dev, "No apollo device resources found\n");
		err = -ENODEV;
		goto err_out;
	}
	if (interrupt_id < 0 || interrupt_id >= APOLLO_INTERRUPT_COUNT) {
		dev_err(dev, "Invalid interrupt ID (%d)\n", interrupt_id);
		err = -EINVAL;
		goto err_out;
	}
	spin_lock_irqsave(&apollo->interrupt_enable_lock, flags);

	if (apollo->interrupt_handlers[interrupt_id].enabled) {
		dev_warn(dev, "Interrupt ID %d already enabled\n",
			interrupt_id);
		err = -EEXIST;
		goto err_unlock;
	}
	apollo->interrupt_handlers[interrupt_id].enabled = true;

	apollo_enable_interrupt_register(apollo, interrupt_id);

err_unlock:
	spin_unlock_irqrestore(&apollo->interrupt_enable_lock, flags);
err_out:
	return err;
}
EXPORT_SYMBOL(apollo_enable_interrupt);

int apollo_disable_interrupt(struct device *dev, int interrupt_id)
{
	struct apollo_device *apollo = devres_find(dev, apollo_devres_release,
		NULL, NULL);
	int err = 0;
	unsigned long flags;
	if (!apollo) {
		dev_err(dev, "No apollo device resources found\n");
		err = -ENODEV;
		goto err_out;
	}
	if (interrupt_id < 0 || interrupt_id >= APOLLO_INTERRUPT_COUNT) {
		dev_err(dev, "Invalid interrupt ID (%d)\n", interrupt_id);
		err = -EINVAL;
		goto err_out;
	}
	spin_lock_irqsave(&apollo->interrupt_enable_lock, flags);

	if (!apollo->interrupt_handlers[interrupt_id].enabled) {
		dev_warn(dev, "Interrupt ID %d already disabled\n",
			interrupt_id);
		err = -EEXIST;
		goto err_unlock;
	}
	apollo->interrupt_handlers[interrupt_id].enabled = false;

	apollo_disable_interrupt_register(apollo, interrupt_id);

err_unlock:
	spin_unlock_irqrestore(&apollo->interrupt_enable_lock, flags);
err_out:
	return err;
}
EXPORT_SYMBOL(apollo_disable_interrupt);
