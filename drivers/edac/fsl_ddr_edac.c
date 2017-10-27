/*
 * Freescale Memory Controller kernel module
 *
 * Support Power-based SoCs including MPC85xx, MPC86xx, MPC83xx and
 * ARM-based Layerscape SoCs including LS2xxx. Originally split
 * out from mpc85xx_edac EDAC driver.
 *
 * Parts Copyrighted (c) 2013 by Freescale Semiconductor, Inc.
 *
 * Author: Dave Jiang <djiang@mvista.com>
 *
 * 2006-2007 (c) MontaVista Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/ctype.h>
#include <linux/io.h>
#include <linux/mod_devicetable.h>
#include <linux/edac.h>
#include <linux/smp.h>
#include <linux/gfp.h>

#include <linux/of_platform.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include "edac_module.h"
#include "fsl_ddr_edac.h"

#define EDAC_MOD_STR	"fsl_ddr_edac"

static int edac_mc_idx;

static u32 orig_ddr_err_disable;
static u32 orig_ddr_err_sbe;
static bool little_endian;

static inline u32 ddr_in32(void __iomem *addr)
{
	return little_endian ? ioread32(addr) : ioread32be(addr);
}

static inline void ddr_out32(void __iomem *addr, u32 value)
{
	if (little_endian)
		iowrite32(value, addr);
	else
		iowrite32be(value, addr);
}

/************************ MC SYSFS parts ***********************************/

#define to_mci(k) container_of(k, struct mem_ctl_info, dev)

static ssize_t fsl_mc_inject_data_hi_show(struct device *dev,
					  struct device_attribute *mattr,
					  char *data)
{
	struct mem_ctl_info *mci = to_mci(dev);
	struct fsl_mc_pdata *pdata = mci->pvt_info;
	return sprintf(data, "0x%08x",
		       ddr_in32(pdata->mc_vbase + FSL_MC_DATA_ERR_INJECT_HI));
}

static ssize_t fsl_mc_inject_data_lo_show(struct device *dev,
					  struct device_attribute *mattr,
					      char *data)
{
	struct mem_ctl_info *mci = to_mci(dev);
	struct fsl_mc_pdata *pdata = mci->pvt_info;
	return sprintf(data, "0x%08x",
		       ddr_in32(pdata->mc_vbase + FSL_MC_DATA_ERR_INJECT_LO));
}

static ssize_t fsl_mc_inject_ctrl_show(struct device *dev,
				       struct device_attribute *mattr,
					   char *data)
{
	struct mem_ctl_info *mci = to_mci(dev);
	struct fsl_mc_pdata *pdata = mci->pvt_info;
	return sprintf(data, "0x%08x",
		       ddr_in32(pdata->mc_vbase + FSL_MC_ECC_ERR_INJECT));
}

static ssize_t fsl_mc_inject_data_hi_store(struct device *dev,
					   struct device_attribute *mattr,
					       const char *data, size_t count)
{
	struct mem_ctl_info *mci = to_mci(dev);
	struct fsl_mc_pdata *pdata = mci->pvt_info;
	unsigned long val;
	int rc;

	if (isdigit(*data)) {
		rc = kstrtoul(data, 0, &val);
		if (rc)
			return rc;

		ddr_out32(pdata->mc_vbase + FSL_MC_DATA_ERR_INJECT_HI, val);
		return count;
	}
	return 0;
}

static ssize_t fsl_mc_inject_data_lo_store(struct device *dev,
					   struct device_attribute *mattr,
					       const char *data, size_t count)
{
	struct mem_ctl_info *mci = to_mci(dev);
	struct fsl_mc_pdata *pdata = mci->pvt_info;
	unsigned long val;
	int rc;

	if (isdigit(*data)) {
		rc = kstrtoul(data, 0, &val);
		if (rc)
			return rc;

		ddr_out32(pdata->mc_vbase + FSL_MC_DATA_ERR_INJECT_LO, val);
		return count;
	}
	return 0;
}

static ssize_t fsl_mc_inject_ctrl_store(struct device *dev,
					struct device_attribute *mattr,
					       const char *data, size_t count)
{
	struct mem_ctl_info *mci = to_mci(dev);
	struct fsl_mc_pdata *pdata = mci->pvt_info;
	unsigned long val;
	int rc;

	if (isdigit(*data)) {
		rc = kstrtoul(data, 0, &val);
		if (rc)
			return rc;

		ddr_out32(pdata->mc_vbase + FSL_MC_ECC_ERR_INJECT, val);
		return count;
	}
	return 0;
}

static DEVICE_ATTR(inject_data_hi, S_IRUGO | S_IWUSR,
		   fsl_mc_inject_data_hi_show, fsl_mc_inject_data_hi_store);
static DEVICE_ATTR(inject_data_lo, S_IRUGO | S_IWUSR,
		   fsl_mc_inject_data_lo_show, fsl_mc_inject_data_lo_store);
static DEVICE_ATTR(inject_ctrl, S_IRUGO | S_IWUSR,
		   fsl_mc_inject_ctrl_show, fsl_mc_inject_ctrl_store);

static struct attribute *fsl_ddr_dev_attrs[] = {
	&dev_attr_inject_data_hi.attr,
	&dev_attr_inject_data_lo.attr,
	&dev_attr_inject_ctrl.attr,
	NULL
};

ATTRIBUTE_GROUPS(fsl_ddr_dev);

/**************************** MC Err device ***************************/

/*
 * Taken from table 8-55 in the MPC8641 User's Manual and/or 9-61 in the
 * MPC8572 User's Manual.  Each line represents a syndrome bit column as a
 * 64-bit value, but split into an upper and lower 32-bit chunk.  The labels
 * below correspond to Freescale's manuals.
 */
static unsigned int ecc_table[16] = {
	/* MSB           LSB */
	/* [0:31]    [32:63] */
	0xf00fe11e, 0xc33c0ff7,	/* Syndrome bit 7 */
	0x00ff00ff, 0x00fff0ff,
	0x0f0f0f0f, 0x0f0fff00,
	0x11113333, 0x7777000f,
	0x22224444, 0x8888222f,
	0x44448888, 0xffff4441,
	0x8888ffff, 0x11118882,
	0xffff1111, 0x22221114,	/* Syndrome bit 0 */
};

/*
 * Calculate the correct ECC value for a 64-bit value specified by high:low
 */
static u8 calculate_ecc(u32 high, u32 low)
{
	u32 mask_low;
	u32 mask_high;
	int bit_cnt;
	u8 ecc = 0;
	int i;
	int j;

	for (i = 0; i < 8; i++) {
		mask_high = ecc_table[i * 2];
		mask_low = ecc_table[i * 2 + 1];
		bit_cnt = 0;

		for (j = 0; j < 32; j++) {
			if ((mask_high >> j) & 1)
				bit_cnt ^= (high >> j) & 1;
			if ((mask_low >> j) & 1)
				bit_cnt ^= (low >> j) & 1;
		}

		ecc |= bit_cnt << i;
	}

	return ecc;
}

/*
 * Create the syndrome code which is generated if the data line specified by
 * 'bit' failed.  Eg generate an 8-bit codes seen in Table 8-55 in the MPC8641
 * User's Manual and 9-61 in the MPC8572 User's Manual.
 */
static u8 syndrome_from_bit(unsigned int bit) {
	int i;
	u8 syndrome = 0;

	/*
	 * Cycle through the upper or lower 32-bit portion of each value in
	 * ecc_table depending on if 'bit' is in the upper or lower half of
	 * 64-bit data.
	 */
	for (i = bit < 32; i < 16; i += 2)
		syndrome |= ((ecc_table[i] >> (bit % 32)) & 1) << (i / 2);

	return syndrome;
}

/*
 * Decode data and ecc syndrome to determine what went wrong
 * Note: This can only decode single-bit errors
 */
static void sbe_ecc_decode(u32 cap_high, u32 cap_low, u32 cap_ecc,
		       int *bad_data_bit, int *bad_ecc_bit)
{
	int i;
	u8 syndrome;

	*bad_data_bit = -1;
	*bad_ecc_bit = -1;

	/*
	 * Calculate the ECC of the captured data and XOR it with the captured
	 * ECC to find an ECC syndrome value we can search for
	 */
	syndrome = calculate_ecc(cap_high, cap_low) ^ cap_ecc;

	/* Check if a data line is stuck... */
	for (i = 0; i < 64; i++) {
		if (syndrome == syndrome_from_bit(i)) {
			*bad_data_bit = i;
			return;
		}
	}

	/* If data is correct, check ECC bits for errors... */
	for (i = 0; i < 8; i++) {
		if ((syndrome >> i) & 0x1) {
			*bad_ecc_bit = i;
			return;
		}
	}
}

#define make64(high, low) (((u64)(high) << 32) | (low))

static void fsl_mc_check(struct mem_ctl_info *mci)
{
	struct fsl_mc_pdata *pdata = mci->pvt_info;
	struct csrow_info *csrow;
	u32 bus_width;
	u32 err_detect;
	u32 syndrome;
	u64 err_addr;
	u32 pfn;
	int row_index;
	u32 cap_high;
	u32 cap_low;
	int bad_data_bit;
	int bad_ecc_bit;

	err_detect = ddr_in32(pdata->mc_vbase + FSL_MC_ERR_DETECT);
	if (!err_detect)
		return;

	fsl_mc_printk(mci, KERN_ERR, "Err Detect Register: %#8.8x\n",
		      err_detect);

	/* no more processing if not ECC bit errors */
	if (!(err_detect & (DDR_EDE_SBE | DDR_EDE_MBE))) {
		ddr_out32(pdata->mc_vbase + FSL_MC_ERR_DETECT, err_detect);
		return;
	}

	syndrome = ddr_in32(pdata->mc_vbase + FSL_MC_CAPTURE_ECC);

	/* Mask off appropriate bits of syndrome based on bus width */
	bus_width = (ddr_in32(pdata->mc_vbase + FSL_MC_DDR_SDRAM_CFG) &
		     DSC_DBW_MASK) ? 32 : 64;
	if (bus_width == 64)
		syndrome &= 0xff;
	else
		syndrome &= 0xffff;

	err_addr = make64(
		ddr_in32(pdata->mc_vbase + FSL_MC_CAPTURE_EXT_ADDRESS),
		ddr_in32(pdata->mc_vbase + FSL_MC_CAPTURE_ADDRESS));
	pfn = err_addr >> PAGE_SHIFT;

	for (row_index = 0; row_index < mci->nr_csrows; row_index++) {
		csrow = mci->csrows[row_index];
		if ((pfn >= csrow->first_page) && (pfn <= csrow->last_page))
			break;
	}

	cap_high = ddr_in32(pdata->mc_vbase + FSL_MC_CAPTURE_DATA_HI);
	cap_low = ddr_in32(pdata->mc_vbase + FSL_MC_CAPTURE_DATA_LO);

	/*
	 * Analyze single-bit errors on 64-bit wide buses
	 * TODO: Add support for 32-bit wide buses
	 */
	if ((err_detect & DDR_EDE_SBE) && (bus_width == 64)) {
		sbe_ecc_decode(cap_high, cap_low, syndrome,
				&bad_data_bit, &bad_ecc_bit);

		if (bad_data_bit != -1)
			fsl_mc_printk(mci, KERN_ERR,
				"Faulty Data bit: %d\n", bad_data_bit);
		if (bad_ecc_bit != -1)
			fsl_mc_printk(mci, KERN_ERR,
				"Faulty ECC bit: %d\n", bad_ecc_bit);

		fsl_mc_printk(mci, KERN_ERR,
			"Expected Data / ECC:\t%#8.8x_%08x / %#2.2x\n",
			cap_high ^ (1 << (bad_data_bit - 32)),
			cap_low ^ (1 << bad_data_bit),
			syndrome ^ (1 << bad_ecc_bit));
	}

	fsl_mc_printk(mci, KERN_ERR,
			"Captured Data / ECC:\t%#8.8x_%08x / %#2.2x\n",
			cap_high, cap_low, syndrome);
	fsl_mc_printk(mci, KERN_ERR, "Err addr: %#8.8llx\n", err_addr);
	fsl_mc_printk(mci, KERN_ERR, "PFN: %#8.8x\n", pfn);

	/* we are out of range */
	if (row_index == mci->nr_csrows)
		fsl_mc_printk(mci, KERN_ERR, "PFN out of range!\n");

	if (err_detect & DDR_EDE_SBE)
		edac_mc_handle_error(HW_EVENT_ERR_CORRECTED, mci, 1,
				     pfn, err_addr & ~PAGE_MASK, syndrome,
				     row_index, 0, -1,
				     mci->ctl_name, "");

	if (err_detect & DDR_EDE_MBE)
		edac_mc_handle_error(HW_EVENT_ERR_UNCORRECTED, mci, 1,
				     pfn, err_addr & ~PAGE_MASK, syndrome,
				     row_index, 0, -1,
				     mci->ctl_name, "");

	ddr_out32(pdata->mc_vbase + FSL_MC_ERR_DETECT, err_detect);
}

static irqreturn_t fsl_mc_isr(int irq, void *dev_id)
{
	struct mem_ctl_info *mci = dev_id;
	struct fsl_mc_pdata *pdata = mci->pvt_info;
	u32 err_detect;

	err_detect = ddr_in32(pdata->mc_vbase + FSL_MC_ERR_DETECT);
	if (!err_detect)
		return IRQ_NONE;

	fsl_mc_check(mci);

	return IRQ_HANDLED;
}

static void fsl_ddr_init_csrows(struct mem_ctl_info *mci)
{
	struct fsl_mc_pdata *pdata = mci->pvt_info;
	struct csrow_info *csrow;
	struct dimm_info *dimm;
	u32 sdram_ctl;
	u32 sdtype;
	enum mem_type mtype;
	u32 cs_bnds;
	int index;

	sdram_ctl = ddr_in32(pdata->mc_vbase + FSL_MC_DDR_SDRAM_CFG);

	sdtype = sdram_ctl & DSC_SDTYPE_MASK;
	if (sdram_ctl & DSC_RD_EN) {
		switch (sdtype) {
		case 0x02000000:
			mtype = MEM_RDDR;
			break;
		case 0x03000000:
			mtype = MEM_RDDR2;
			break;
		case 0x07000000:
			mtype = MEM_RDDR3;
			break;
		case 0x05000000:
			mtype = MEM_RDDR4;
			break;
		default:
			mtype = MEM_UNKNOWN;
			break;
		}
	} else {
		switch (sdtype) {
		case 0x02000000:
			mtype = MEM_DDR;
			break;
		case 0x03000000:
			mtype = MEM_DDR2;
			break;
		case 0x07000000:
			mtype = MEM_DDR3;
			break;
		case 0x05000000:
			mtype = MEM_DDR4;
			break;
		default:
			mtype = MEM_UNKNOWN;
			break;
		}
	}

	for (index = 0; index < mci->nr_csrows; index++) {
		u32 start;
		u32 end;

		csrow = mci->csrows[index];
		dimm = csrow->channels[0]->dimm;

		cs_bnds = ddr_in32(pdata->mc_vbase + FSL_MC_CS_BNDS_0 +
				   (index * FSL_MC_CS_BNDS_OFS));

		start = (cs_bnds & 0xffff0000) >> 16;
		end   = (cs_bnds & 0x0000ffff);

		if (start == end)
			continue;	/* not populated */

		start <<= (24 - PAGE_SHIFT);
		end   <<= (24 - PAGE_SHIFT);
		end    |= (1 << (24 - PAGE_SHIFT)) - 1;

		csrow->first_page = start;
		csrow->last_page = end;

		dimm->nr_pages = end + 1 - start;
		dimm->grain = 8;
		dimm->mtype = mtype;
		dimm->dtype = DEV_UNKNOWN;
		if (sdram_ctl & DSC_X32_EN)
			dimm->dtype = DEV_X32;
		dimm->edac_mode = EDAC_SECDED;
	}
}

int fsl_mc_err_probe(struct platform_device *op)
{
	struct mem_ctl_info *mci;
	struct edac_mc_layer layers[2];
	struct fsl_mc_pdata *pdata;
	struct resource r;
	u32 sdram_ctl;
	int res;

	if (!devres_open_group(&op->dev, fsl_mc_err_probe, GFP_KERNEL))
		return -ENOMEM;

	layers[0].type = EDAC_MC_LAYER_CHIP_SELECT;
	layers[0].size = 4;
	layers[0].is_virt_csrow = true;
	layers[1].type = EDAC_MC_LAYER_CHANNEL;
	layers[1].size = 1;
	layers[1].is_virt_csrow = false;
	mci = edac_mc_alloc(edac_mc_idx, ARRAY_SIZE(layers), layers,
			    sizeof(*pdata));
	if (!mci) {
		devres_release_group(&op->dev, fsl_mc_err_probe);
		return -ENOMEM;
	}

	pdata = mci->pvt_info;
	pdata->name = "fsl_mc_err";
	mci->pdev = &op->dev;
	pdata->edac_idx = edac_mc_idx++;
	dev_set_drvdata(mci->pdev, mci);
	mci->ctl_name = pdata->name;
	mci->dev_name = pdata->name;

	/*
	 * Get the endianness of DDR controller registers.
	 * Default is big endian.
	 */
	little_endian = of_property_read_bool(op->dev.of_node, "little-endian");

	res = of_address_to_resource(op->dev.of_node, 0, &r);
	if (res) {
		pr_err("%s: Unable to get resource for MC err regs\n",
		       __func__);
		goto err;
	}

	if (!devm_request_mem_region(&op->dev, r.start, resource_size(&r),
				     pdata->name)) {
		pr_err("%s: Error while requesting mem region\n",
		       __func__);
		res = -EBUSY;
		goto err;
	}

	pdata->mc_vbase = devm_ioremap(&op->dev, r.start, resource_size(&r));
	if (!pdata->mc_vbase) {
		pr_err("%s: Unable to setup MC err regs\n", __func__);
		res = -ENOMEM;
		goto err;
	}

	sdram_ctl = ddr_in32(pdata->mc_vbase + FSL_MC_DDR_SDRAM_CFG);
	if (!(sdram_ctl & DSC_ECC_EN)) {
		/* no ECC */
		pr_warn("%s: No ECC DIMMs discovered\n", __func__);
		res = -ENODEV;
		goto err;
	}

	edac_dbg(3, "init mci\n");
	mci->mtype_cap = MEM_FLAG_DDR | MEM_FLAG_RDDR |
			 MEM_FLAG_DDR2 | MEM_FLAG_RDDR2 |
			 MEM_FLAG_DDR3 | MEM_FLAG_RDDR3 |
			 MEM_FLAG_DDR4 | MEM_FLAG_RDDR4;
	mci->edac_ctl_cap = EDAC_FLAG_NONE | EDAC_FLAG_SECDED;
	mci->edac_cap = EDAC_FLAG_SECDED;
	mci->mod_name = EDAC_MOD_STR;

	if (edac_op_state == EDAC_OPSTATE_POLL)
		mci->edac_check = fsl_mc_check;

	mci->ctl_page_to_phys = NULL;

	mci->scrub_mode = SCRUB_SW_SRC;

	fsl_ddr_init_csrows(mci);

	/* store the original error disable bits */
	orig_ddr_err_disable = ddr_in32(pdata->mc_vbase + FSL_MC_ERR_DISABLE);
	ddr_out32(pdata->mc_vbase + FSL_MC_ERR_DISABLE, 0);

	/* clear all error bits */
	ddr_out32(pdata->mc_vbase + FSL_MC_ERR_DETECT, ~0);

	res = edac_mc_add_mc_with_groups(mci, fsl_ddr_dev_groups);
	if (res) {
		edac_dbg(3, "failed edac_mc_add_mc()\n");
		goto err;
	}

	if (edac_op_state == EDAC_OPSTATE_INT) {
		ddr_out32(pdata->mc_vbase + FSL_MC_ERR_INT_EN,
			  DDR_EIE_MBEE | DDR_EIE_SBEE);

		/* store the original error management threshold */
		orig_ddr_err_sbe = ddr_in32(pdata->mc_vbase +
					    FSL_MC_ERR_SBE) & 0xff0000;

		/* set threshold to 1 error per interrupt */
		ddr_out32(pdata->mc_vbase + FSL_MC_ERR_SBE, 0x10000);

		/* register interrupts */
		pdata->irq = platform_get_irq(op, 0);
		res = devm_request_irq(&op->dev, pdata->irq,
				       fsl_mc_isr,
				       IRQF_SHARED,
				       "[EDAC] MC err", mci);
		if (res < 0) {
			pr_err("%s: Unable to request irq %d for FSL DDR DRAM ERR\n",
			       __func__, pdata->irq);
			res = -ENODEV;
			goto err2;
		}

		pr_info(EDAC_MOD_STR " acquired irq %d for MC\n",
		       pdata->irq);
	}

	devres_remove_group(&op->dev, fsl_mc_err_probe);
	edac_dbg(3, "success\n");
	pr_info(EDAC_MOD_STR " MC err registered\n");

	return 0;

err2:
	edac_mc_del_mc(&op->dev);
err:
	devres_release_group(&op->dev, fsl_mc_err_probe);
	edac_mc_free(mci);
	return res;
}

int fsl_mc_err_remove(struct platform_device *op)
{
	struct mem_ctl_info *mci = dev_get_drvdata(&op->dev);
	struct fsl_mc_pdata *pdata = mci->pvt_info;

	edac_dbg(0, "\n");

	if (edac_op_state == EDAC_OPSTATE_INT) {
		ddr_out32(pdata->mc_vbase + FSL_MC_ERR_INT_EN, 0);
	}

	ddr_out32(pdata->mc_vbase + FSL_MC_ERR_DISABLE,
		  orig_ddr_err_disable);
	ddr_out32(pdata->mc_vbase + FSL_MC_ERR_SBE, orig_ddr_err_sbe);

	edac_mc_del_mc(&op->dev);
	edac_mc_free(mci);
	return 0;
}
