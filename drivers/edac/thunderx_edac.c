/*
 * Cavium ThunderX memory controller kernel module
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright Cavium, Inc. (C) 2015-2017. All rights reserved.
 *
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/edac.h>
#include <linux/interrupt.h>
#include <linux/string.h>
#include <linux/stop_machine.h>
#include <linux/delay.h>
#include <linux/sizes.h>
#include <linux/atomic.h>
#include <linux/bitfield.h>
#include <linux/circ_buf.h>

#include <asm/page.h>

#include "edac_module.h"

#define phys_to_pfn(phys)	(PFN_DOWN(phys))

#define THUNDERX_NODE		GENMASK(45, 44)

enum {
	ERR_CORRECTED	= 1,
	ERR_UNCORRECTED	= 2,
	ERR_UNKNOWN	= 3,
};

#define MAX_SYNDROME_REGS 4

struct error_syndrome {
	u64 reg[MAX_SYNDROME_REGS];
};

struct error_descr {
	int	type;
	u64	mask;
	char	*descr;
};

static void decode_register(char *str, size_t size,
			   const struct error_descr *descr,
			   const uint64_t reg)
{
	int ret = 0;

	while (descr->type && descr->mask && descr->descr) {
		if (reg & descr->mask) {
			ret = snprintf(str, size, "\n\t%s, %s",
				       descr->type == ERR_CORRECTED ?
					 "Corrected" : "Uncorrected",
				       descr->descr);
			str += ret;
			size -= ret;
		}
		descr++;
	}
}

static unsigned long get_bits(unsigned long data, int pos, int width)
{
	return (data >> pos) & ((1 << width) - 1);
}

#define L2C_CTL			0x87E080800000
#define L2C_CTL_DISIDXALIAS	BIT(0)

#define PCI_DEVICE_ID_THUNDER_LMC 0xa022

#define LMC_FADR		0x20
#define LMC_FADR_FDIMM(x)	((x >> 37) & 0x1)
#define LMC_FADR_FBUNK(x)	((x >> 36) & 0x1)
#define LMC_FADR_FBANK(x)	((x >> 32) & 0xf)
#define LMC_FADR_FROW(x)	((x >> 14) & 0xffff)
#define LMC_FADR_FCOL(x)	((x >> 0) & 0x1fff)

#define LMC_NXM_FADR		0x28
#define LMC_ECC_SYND		0x38

#define LMC_ECC_PARITY_TEST	0x108

#define LMC_INT_W1S		0x150

#define LMC_INT_ENA_W1C		0x158
#define LMC_INT_ENA_W1S		0x160

#define LMC_CONFIG		0x188

#define LMC_CONFIG_BG2		BIT(62)
#define LMC_CONFIG_RANK_ENA	BIT(42)
#define LMC_CONFIG_PBANK_LSB(x)	(((x) >> 5) & 0xF)
#define LMC_CONFIG_ROW_LSB(x)	(((x) >> 2) & 0x7)

#define LMC_CONTROL		0x190
#define LMC_CONTROL_XOR_BANK	BIT(16)

#define LMC_INT			0x1F0

#define LMC_INT_DDR_ERR		BIT(11)
#define LMC_INT_DED_ERR		(0xFUL << 5)
#define LMC_INT_SEC_ERR         (0xFUL << 1)
#define LMC_INT_NXM_WR_MASK	BIT(0)

#define LMC_DDR_PLL_CTL		0x258
#define LMC_DDR_PLL_CTL_DDR4	BIT(29)

#define LMC_FADR_SCRAMBLED	0x330

#define LMC_INT_UE              (LMC_INT_DDR_ERR | LMC_INT_DED_ERR | \
				 LMC_INT_NXM_WR_MASK)

#define LMC_INT_CE		(LMC_INT_SEC_ERR)

static const struct error_descr lmc_errors[] = {
	{
		.type  = ERR_CORRECTED,
		.mask  = LMC_INT_SEC_ERR,
		.descr = "Single-bit ECC error",
	},
	{
		.type  = ERR_UNCORRECTED,
		.mask  = LMC_INT_DDR_ERR,
		.descr = "DDR chip error",
	},
	{
		.type  = ERR_UNCORRECTED,
		.mask  = LMC_INT_DED_ERR,
		.descr = "Double-bit ECC error",
	},
	{
		.type = ERR_UNCORRECTED,
		.mask = LMC_INT_NXM_WR_MASK,
		.descr = "Non-existent memory write",
	},
	{0, 0, NULL},
};

#define LMC_INT_EN_DDR_ERROR_ALERT_ENA	BIT(5)
#define LMC_INT_EN_DLCRAM_DED_ERR	BIT(4)
#define LMC_INT_EN_DLCRAM_SEC_ERR	BIT(3)
#define LMC_INT_INTR_DED_ENA		BIT(2)
#define LMC_INT_INTR_SEC_ENA		BIT(1)
#define LMC_INT_INTR_NXM_WR_ENA		BIT(0)

#define LMC_INT_ENA_ALL			GENMASK(5, 0)

#define LMC_DDR_PLL_CTL		0x258
#define LMC_DDR_PLL_CTL_DDR4	BIT(29)

#define LMC_CONTROL		0x190
#define LMC_CONTROL_RDIMM	BIT(0)

#define LMC_SCRAM_FADR		0x330

#define LMC_CHAR_MASK0		0x228
#define LMC_CHAR_MASK2		0x238

#define RING_ENTRIES	8

struct debugfs_entry {
	const char *name;
	umode_t mode;
	const struct file_operations fops;
};

struct lmc_err_ctx {
	u64 reg_int;
	u64 reg_fadr;
	u64 reg_nxm_fadr;
	u64 reg_scram_fadr;
	u64 reg_ecc_synd;
};

struct thunderx_lmc {
	void __iomem *regs;
	struct pci_dev *pdev;
	struct msix_entry msix_ent;

	atomic_t ecc_int;

	u64 mask0;
	u64 mask2;
	u64 parity_test;
	u64 node;

	int xbits;
	int bank_width;
	int pbank_lsb;
	int dimm_lsb;
	int rank_lsb;
	int bank_lsb;
	int row_lsb;
	int col_hi_lsb;

	int xor_bank;
	int l2c_alias;

	struct page *mem;

	struct lmc_err_ctx err_ctx[RING_ENTRIES];
	unsigned long ring_head;
	unsigned long ring_tail;
};

#define ring_pos(pos, size) ((pos) & (size - 1))

#define DEBUGFS_STRUCT(_name, _mode, _write, _read)			    \
static struct debugfs_entry debugfs_##_name = {				    \
	.name = __stringify(_name),					    \
	.mode = VERIFY_OCTAL_PERMISSIONS(_mode),			    \
	.fops = {							    \
		.open = simple_open,					    \
		.write = _write,					    \
		.read  = _read,						    \
		.llseek = generic_file_llseek,				    \
	},								    \
}

#define DEBUGFS_FIELD_ATTR(_type, _field)				    \
static ssize_t thunderx_##_type##_##_field##_read(struct file *file,	    \
					    char __user *data,		    \
					    size_t count, loff_t *ppos)	    \
{									    \
	struct thunderx_##_type *pdata = file->private_data;		    \
	char buf[20];							    \
									    \
	snprintf(buf, count, "0x%016llx", pdata->_field);		    \
	return simple_read_from_buffer(data, count, ppos,		    \
				       buf, sizeof(buf));		    \
}									    \
									    \
static ssize_t thunderx_##_type##_##_field##_write(struct file *file,	    \
					     const char __user *data,	    \
					     size_t count, loff_t *ppos)    \
{									    \
	struct thunderx_##_type *pdata = file->private_data;		    \
	int res;							    \
									    \
	res = kstrtoull_from_user(data, count, 0, &pdata->_field);	    \
									    \
	return res ? res : count;					    \
}									    \
									    \
DEBUGFS_STRUCT(_field, 0600,						    \
		   thunderx_##_type##_##_field##_write,			    \
		   thunderx_##_type##_##_field##_read)			    \

#define DEBUGFS_REG_ATTR(_type, _name, _reg)				    \
static ssize_t thunderx_##_type##_##_name##_read(struct file *file,	    \
					   char __user *data,		    \
					   size_t count, loff_t *ppos)      \
{									    \
	struct thunderx_##_type *pdata = file->private_data;		    \
	char buf[20];							    \
									    \
	sprintf(buf, "0x%016llx", readq(pdata->regs + _reg));		    \
	return simple_read_from_buffer(data, count, ppos,		    \
				       buf, sizeof(buf));		    \
}									    \
									    \
static ssize_t thunderx_##_type##_##_name##_write(struct file *file,	    \
					    const char __user *data,	    \
					    size_t count, loff_t *ppos)     \
{									    \
	struct thunderx_##_type *pdata = file->private_data;		    \
	u64 val;							    \
	int res;							    \
									    \
	res = kstrtoull_from_user(data, count, 0, &val);		    \
									    \
	if (!res) {							    \
		writeq(val, pdata->regs + _reg);			    \
		res = count;						    \
	}								    \
									    \
	return res;							    \
}									    \
									    \
DEBUGFS_STRUCT(_name, 0600,						    \
	       thunderx_##_type##_##_name##_write,			    \
	       thunderx_##_type##_##_name##_read)

#define LMC_DEBUGFS_ENT(_field)	DEBUGFS_FIELD_ATTR(lmc, _field)

/*
 * To get an ECC error injected, the following steps are needed:
 * - Setup the ECC injection by writing the appropriate parameters:
 *	echo <bit mask value> > /sys/kernel/debug/<device number>/ecc_mask0
 *	echo <bit mask value> > /sys/kernel/debug/<device number>/ecc_mask2
 *	echo 0x802 > /sys/kernel/debug/<device number>/ecc_parity_test
 * - Do the actual injection:
 *	echo 1 > /sys/kernel/debug/<device number>/inject_ecc
 */
static ssize_t thunderx_lmc_inject_int_write(struct file *file,
					     const char __user *data,
					     size_t count, loff_t *ppos)
{
	struct thunderx_lmc *lmc = file->private_data;
	u64 val;
	int res;

	res = kstrtoull_from_user(data, count, 0, &val);

	if (!res) {
		/* Trigger the interrupt */
		writeq(val, lmc->regs + LMC_INT_W1S);
		res = count;
	}

	return res;
}

static ssize_t thunderx_lmc_int_read(struct file *file,
				     char __user *data,
				     size_t count, loff_t *ppos)
{
	struct thunderx_lmc *lmc = file->private_data;
	char buf[20];
	u64 lmc_int = readq(lmc->regs + LMC_INT);

	snprintf(buf, sizeof(buf), "0x%016llx", lmc_int);
	return simple_read_from_buffer(data, count, ppos, buf, sizeof(buf));
}

#define TEST_PATTERN 0xa5

static int inject_ecc_fn(void *arg)
{
	struct thunderx_lmc *lmc = arg;
	uintptr_t addr, phys;
	unsigned int cline_size = cache_line_size();
	const unsigned int lines = PAGE_SIZE / cline_size;
	unsigned int i, cl_idx;

	addr = (uintptr_t)page_address(lmc->mem);
	phys = (uintptr_t)page_to_phys(lmc->mem);

	cl_idx = (phys & 0x7f) >> 4;
	lmc->parity_test &= ~(7ULL << 8);
	lmc->parity_test |= (cl_idx << 8);

	writeq(lmc->mask0, lmc->regs + LMC_CHAR_MASK0);
	writeq(lmc->mask2, lmc->regs + LMC_CHAR_MASK2);
	writeq(lmc->parity_test, lmc->regs + LMC_ECC_PARITY_TEST);

	readq(lmc->regs + LMC_CHAR_MASK0);
	readq(lmc->regs + LMC_CHAR_MASK2);
	readq(lmc->regs + LMC_ECC_PARITY_TEST);

	for (i = 0; i < lines; i++) {
		memset((void *)addr, TEST_PATTERN, cline_size);
		barrier();

		/*
		 * Flush L1 cachelines to the PoC (L2).
		 * This will cause cacheline eviction to the L2.
		 */
		asm volatile("dc civac, %0\n"
			     "dsb sy\n"
			     : : "r"(addr + i * cline_size));
	}

	for (i = 0; i < lines; i++) {
		/*
		 * Flush L2 cachelines to the DRAM.
		 * This will cause cacheline eviction to the DRAM
		 * and ECC corruption according to the masks set.
		 */
		__asm__ volatile("sys #0,c11,C1,#2, %0\n"
				 : : "r"(phys + i * cline_size));
	}

	for (i = 0; i < lines; i++) {
		/*
		 * Invalidate L2 cachelines.
		 * The subsequent load will cause cacheline fetch
		 * from the DRAM and an error interrupt
		 */
		__asm__ volatile("sys #0,c11,C1,#1, %0"
				 : : "r"(phys + i * cline_size));
	}

	for (i = 0; i < lines; i++) {
		/*
		 * Invalidate L1 cachelines.
		 * The subsequent load will cause cacheline fetch
		 * from the L2 and/or DRAM
		 */
		asm volatile("dc ivac, %0\n"
			     "dsb sy\n"
			     : : "r"(addr + i * cline_size));
	}

	return 0;
}

static ssize_t thunderx_lmc_inject_ecc_write(struct file *file,
					     const char __user *data,
					     size_t count, loff_t *ppos)
{
	struct thunderx_lmc *lmc = file->private_data;
	unsigned int cline_size = cache_line_size();
	u8 *tmp;
	void __iomem *addr;
	unsigned int offs, timeout = 100000;

	atomic_set(&lmc->ecc_int, 0);

	lmc->mem = alloc_pages_node(lmc->node, GFP_KERNEL, 0);
	if (!lmc->mem)
		return -ENOMEM;

	tmp = kmalloc(cline_size, GFP_KERNEL);
	if (!tmp) {
		__free_pages(lmc->mem, 0);
		return -ENOMEM;
	}

	addr = page_address(lmc->mem);

	while (!atomic_read(&lmc->ecc_int) && timeout--) {
		stop_machine(inject_ecc_fn, lmc, NULL);

		for (offs = 0; offs < PAGE_SIZE; offs += cline_size) {
			/*
			 * Do a load from the previously rigged location
			 * This should generate an error interrupt.
			 */
			memcpy(tmp, addr + offs, cline_size);
			asm volatile("dsb ld\n");
		}
	}

	kfree(tmp);
	__free_pages(lmc->mem, 0);

	return count;
}

LMC_DEBUGFS_ENT(mask0);
LMC_DEBUGFS_ENT(mask2);
LMC_DEBUGFS_ENT(parity_test);

DEBUGFS_STRUCT(inject_int, 0200, thunderx_lmc_inject_int_write, NULL);
DEBUGFS_STRUCT(inject_ecc, 0200, thunderx_lmc_inject_ecc_write, NULL);
DEBUGFS_STRUCT(int_w1c, 0400, NULL, thunderx_lmc_int_read);

static struct debugfs_entry *lmc_dfs_ents[] = {
	&debugfs_mask0,
	&debugfs_mask2,
	&debugfs_parity_test,
	&debugfs_inject_ecc,
	&debugfs_inject_int,
	&debugfs_int_w1c,
};

static int thunderx_create_debugfs_nodes(struct dentry *parent,
					  struct debugfs_entry *attrs[],
					  void *data,
					  size_t num)
{
	int i;
	struct dentry *ent;

	if (!IS_ENABLED(CONFIG_EDAC_DEBUG))
		return 0;

	if (!parent)
		return -ENOENT;

	for (i = 0; i < num; i++) {
		ent = edac_debugfs_create_file(attrs[i]->name, attrs[i]->mode,
					       parent, data, &attrs[i]->fops);

		if (!ent)
			break;
	}

	return i;
}

static phys_addr_t thunderx_faddr_to_phys(u64 faddr, struct thunderx_lmc *lmc)
{
	phys_addr_t addr = 0;
	int bank, xbits;

	addr |= lmc->node << 40;
	addr |= LMC_FADR_FDIMM(faddr) << lmc->dimm_lsb;
	addr |= LMC_FADR_FBUNK(faddr) << lmc->rank_lsb;
	addr |= LMC_FADR_FROW(faddr) << lmc->row_lsb;
	addr |= (LMC_FADR_FCOL(faddr) >> 4) << lmc->col_hi_lsb;

	bank = LMC_FADR_FBANK(faddr) << lmc->bank_lsb;

	if (lmc->xor_bank)
		bank ^= get_bits(addr, 12 + lmc->xbits, lmc->bank_width);

	addr |= bank << lmc->bank_lsb;

	xbits = PCI_FUNC(lmc->pdev->devfn);

	if (lmc->l2c_alias)
		xbits ^= get_bits(addr, 20, lmc->xbits) ^
			 get_bits(addr, 12, lmc->xbits);

	addr |= xbits << 7;

	return addr;
}

static unsigned int thunderx_get_num_lmcs(unsigned int node)
{
	unsigned int number = 0;
	struct pci_dev *pdev = NULL;

	do {
		pdev = pci_get_device(PCI_VENDOR_ID_CAVIUM,
				      PCI_DEVICE_ID_THUNDER_LMC,
				      pdev);
		if (pdev) {
#ifdef CONFIG_NUMA
			if (pdev->dev.numa_node == node)
				number++;
#else
			number++;
#endif
		}
	} while (pdev);

	return number;
}

#define LMC_MESSAGE_SIZE	120
#define LMC_OTHER_SIZE		(50 * ARRAY_SIZE(lmc_errors))

static irqreturn_t thunderx_lmc_err_isr(int irq, void *dev_id)
{
	struct mem_ctl_info *mci = dev_id;
	struct thunderx_lmc *lmc = mci->pvt_info;

	unsigned long head = ring_pos(lmc->ring_head, ARRAY_SIZE(lmc->err_ctx));
	struct lmc_err_ctx *ctx = &lmc->err_ctx[head];

	writeq(0, lmc->regs + LMC_CHAR_MASK0);
	writeq(0, lmc->regs + LMC_CHAR_MASK2);
	writeq(0x2, lmc->regs + LMC_ECC_PARITY_TEST);

	ctx->reg_int = readq(lmc->regs + LMC_INT);
	ctx->reg_fadr = readq(lmc->regs + LMC_FADR);
	ctx->reg_nxm_fadr = readq(lmc->regs + LMC_NXM_FADR);
	ctx->reg_scram_fadr = readq(lmc->regs + LMC_SCRAM_FADR);
	ctx->reg_ecc_synd = readq(lmc->regs + LMC_ECC_SYND);

	lmc->ring_head++;

	atomic_set(&lmc->ecc_int, 1);

	/* Clear the interrupt */
	writeq(ctx->reg_int, lmc->regs + LMC_INT);

	return IRQ_WAKE_THREAD;
}

static irqreturn_t thunderx_lmc_threaded_isr(int irq, void *dev_id)
{
	struct mem_ctl_info *mci = dev_id;
	struct thunderx_lmc *lmc = mci->pvt_info;
	phys_addr_t phys_addr;

	unsigned long tail;
	struct lmc_err_ctx *ctx;

	irqreturn_t ret = IRQ_NONE;

	char *msg;
	char *other;

	msg = kmalloc(LMC_MESSAGE_SIZE, GFP_KERNEL);
	other =  kmalloc(LMC_OTHER_SIZE, GFP_KERNEL);

	if (!msg || !other)
		goto err_free;

	while (CIRC_CNT(lmc->ring_head, lmc->ring_tail,
		ARRAY_SIZE(lmc->err_ctx))) {
		tail = ring_pos(lmc->ring_tail, ARRAY_SIZE(lmc->err_ctx));

		ctx = &lmc->err_ctx[tail];

		dev_dbg(&lmc->pdev->dev, "LMC_INT: %016llx\n",
			ctx->reg_int);
		dev_dbg(&lmc->pdev->dev, "LMC_FADR: %016llx\n",
			ctx->reg_fadr);
		dev_dbg(&lmc->pdev->dev, "LMC_NXM_FADR: %016llx\n",
			ctx->reg_nxm_fadr);
		dev_dbg(&lmc->pdev->dev, "LMC_SCRAM_FADR: %016llx\n",
			ctx->reg_scram_fadr);
		dev_dbg(&lmc->pdev->dev, "LMC_ECC_SYND: %016llx\n",
			ctx->reg_ecc_synd);

		snprintf(msg, LMC_MESSAGE_SIZE,
			 "DIMM %lld rank %lld bank %lld row %lld col %lld",
			 LMC_FADR_FDIMM(ctx->reg_scram_fadr),
			 LMC_FADR_FBUNK(ctx->reg_scram_fadr),
			 LMC_FADR_FBANK(ctx->reg_scram_fadr),
			 LMC_FADR_FROW(ctx->reg_scram_fadr),
			 LMC_FADR_FCOL(ctx->reg_scram_fadr));

		decode_register(other, LMC_OTHER_SIZE, lmc_errors,
				ctx->reg_int);

		phys_addr = thunderx_faddr_to_phys(ctx->reg_fadr, lmc);

		if (ctx->reg_int & LMC_INT_UE)
			edac_mc_handle_error(HW_EVENT_ERR_UNCORRECTED, mci, 1,
					     phys_to_pfn(phys_addr),
					     offset_in_page(phys_addr),
					     0, -1, -1, -1, msg, other);
		else if (ctx->reg_int & LMC_INT_CE)
			edac_mc_handle_error(HW_EVENT_ERR_CORRECTED, mci, 1,
					     phys_to_pfn(phys_addr),
					     offset_in_page(phys_addr),
					     0, -1, -1, -1, msg, other);

		lmc->ring_tail++;
	}

	ret = IRQ_HANDLED;

err_free:
	kfree(msg);
	kfree(other);

	return ret;
}

static const struct pci_device_id thunderx_lmc_pci_tbl[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_CAVIUM, PCI_DEVICE_ID_THUNDER_LMC) },
	{ 0, },
};

static inline int pci_dev_to_mc_idx(struct pci_dev *pdev)
{
	int node = dev_to_node(&pdev->dev);
	int ret = PCI_FUNC(pdev->devfn);

	ret += max(node, 0) << 3;

	return ret;
}

static int thunderx_lmc_probe(struct pci_dev *pdev,
				const struct pci_device_id *id)
{
	struct thunderx_lmc *lmc;
	struct edac_mc_layer layer;
	struct mem_ctl_info *mci;
	u64 lmc_control, lmc_ddr_pll_ctl, lmc_config;
	int ret;
	u64 lmc_int;
	void *l2c_ioaddr;

	layer.type = EDAC_MC_LAYER_SLOT;
	layer.size = 2;
	layer.is_virt_csrow = false;

	ret = pcim_enable_device(pdev);
	if (ret) {
		dev_err(&pdev->dev, "Cannot enable PCI device: %d\n", ret);
		return ret;
	}

	ret = pcim_iomap_regions(pdev, BIT(0), "thunderx_lmc");
	if (ret) {
		dev_err(&pdev->dev, "Cannot map PCI resources: %d\n", ret);
		return ret;
	}

	mci = edac_mc_alloc(pci_dev_to_mc_idx(pdev), 1, &layer,
			    sizeof(struct thunderx_lmc));
	if (!mci)
		return -ENOMEM;

	mci->pdev = &pdev->dev;
	lmc = mci->pvt_info;

	pci_set_drvdata(pdev, mci);

	lmc->regs = pcim_iomap_table(pdev)[0];

	lmc_control = readq(lmc->regs + LMC_CONTROL);
	lmc_ddr_pll_ctl = readq(lmc->regs + LMC_DDR_PLL_CTL);
	lmc_config = readq(lmc->regs + LMC_CONFIG);

	if (lmc_control & LMC_CONTROL_RDIMM) {
		mci->mtype_cap = FIELD_GET(LMC_DDR_PLL_CTL_DDR4,
					   lmc_ddr_pll_ctl) ?
				MEM_RDDR4 : MEM_RDDR3;
	} else {
		mci->mtype_cap = FIELD_GET(LMC_DDR_PLL_CTL_DDR4,
					   lmc_ddr_pll_ctl) ?
				MEM_DDR4 : MEM_DDR3;
	}

	mci->edac_ctl_cap = EDAC_FLAG_NONE | EDAC_FLAG_SECDED;
	mci->edac_cap = EDAC_FLAG_SECDED;

	mci->mod_name = "thunderx-lmc";
	mci->ctl_name = "thunderx-lmc";
	mci->dev_name = dev_name(&pdev->dev);
	mci->scrub_mode = SCRUB_NONE;

	lmc->pdev = pdev;
	lmc->msix_ent.entry = 0;

	lmc->ring_head = 0;
	lmc->ring_tail = 0;

	ret = pci_enable_msix_exact(pdev, &lmc->msix_ent, 1);
	if (ret) {
		dev_err(&pdev->dev, "Cannot enable interrupt: %d\n", ret);
		goto err_free;
	}

	ret = devm_request_threaded_irq(&pdev->dev, lmc->msix_ent.vector,
					thunderx_lmc_err_isr,
					thunderx_lmc_threaded_isr, 0,
					"[EDAC] ThunderX LMC", mci);
	if (ret) {
		dev_err(&pdev->dev, "Cannot set ISR: %d\n", ret);
		goto err_free;
	}

	lmc->node = FIELD_GET(THUNDERX_NODE, pci_resource_start(pdev, 0));

	lmc->xbits = thunderx_get_num_lmcs(lmc->node) >> 1;
	lmc->bank_width = (FIELD_GET(LMC_DDR_PLL_CTL_DDR4, lmc_ddr_pll_ctl) &&
			   FIELD_GET(LMC_CONFIG_BG2, lmc_config)) ? 4 : 3;

	lmc->pbank_lsb = (lmc_config >> 5) & 0xf;
	lmc->dimm_lsb  = 28 + lmc->pbank_lsb + lmc->xbits;
	lmc->rank_lsb = lmc->dimm_lsb;
	lmc->rank_lsb -= FIELD_GET(LMC_CONFIG_RANK_ENA, lmc_config) ? 1 : 0;
	lmc->bank_lsb = 7 + lmc->xbits;
	lmc->row_lsb = 14 + LMC_CONFIG_ROW_LSB(lmc_config) + lmc->xbits;

	lmc->col_hi_lsb = lmc->bank_lsb + lmc->bank_width;

	lmc->xor_bank = lmc_control & LMC_CONTROL_XOR_BANK;

	l2c_ioaddr = ioremap(L2C_CTL | FIELD_PREP(THUNDERX_NODE, lmc->node), PAGE_SIZE);
	if (!l2c_ioaddr) {
		dev_err(&pdev->dev, "Cannot map L2C_CTL\n");
		ret = -ENOMEM;
		goto err_free;
	}

	lmc->l2c_alias = !(readq(l2c_ioaddr) & L2C_CTL_DISIDXALIAS);

	iounmap(l2c_ioaddr);

	ret = edac_mc_add_mc(mci);
	if (ret) {
		dev_err(&pdev->dev, "Cannot add the MC: %d\n", ret);
		goto err_free;
	}

	lmc_int = readq(lmc->regs + LMC_INT);
	writeq(lmc_int, lmc->regs + LMC_INT);

	writeq(LMC_INT_ENA_ALL, lmc->regs + LMC_INT_ENA_W1S);

	if (IS_ENABLED(CONFIG_EDAC_DEBUG)) {
		ret = thunderx_create_debugfs_nodes(mci->debugfs,
						    lmc_dfs_ents,
						    lmc,
						    ARRAY_SIZE(lmc_dfs_ents));

		if (ret != ARRAY_SIZE(lmc_dfs_ents)) {
			dev_warn(&pdev->dev, "Error creating debugfs entries: %d%s\n",
				 ret, ret >= 0 ? " created" : "");
		}
	}

	return 0;

err_free:
	pci_set_drvdata(pdev, NULL);
	edac_mc_free(mci);

	return ret;
}

static void thunderx_lmc_remove(struct pci_dev *pdev)
{
	struct mem_ctl_info *mci = pci_get_drvdata(pdev);
	struct thunderx_lmc *lmc = mci->pvt_info;

	writeq(LMC_INT_ENA_ALL, lmc->regs + LMC_INT_ENA_W1C);

	edac_mc_del_mc(&pdev->dev);
	edac_mc_free(mci);
}

MODULE_DEVICE_TABLE(pci, thunderx_lmc_pci_tbl);

static struct pci_driver thunderx_lmc_driver = {
	.name     = "thunderx_lmc_edac",
	.probe    = thunderx_lmc_probe,
	.remove   = thunderx_lmc_remove,
	.id_table = thunderx_lmc_pci_tbl,
};

/*---------------------- OCX driver ---------------------------------*/

#define PCI_DEVICE_ID_THUNDER_OCX 0xa013

#define OCX_LINK_INTS		3
#define OCX_INTS		(OCX_LINK_INTS + 1)
#define OCX_RX_LANES		24
#define OCX_RX_LANE_STATS	15

#define OCX_COM_INT		0x100
#define OCX_COM_INT_W1S		0x108
#define OCX_COM_INT_ENA_W1S	0x110
#define OCX_COM_INT_ENA_W1C	0x118

#define OCX_COM_IO_BADID		BIT(54)
#define OCX_COM_MEM_BADID		BIT(53)
#define OCX_COM_COPR_BADID		BIT(52)
#define OCX_COM_WIN_REQ_BADID		BIT(51)
#define OCX_COM_WIN_REQ_TOUT		BIT(50)
#define OCX_COM_RX_LANE			GENMASK(23, 0)

#define OCX_COM_INT_CE			(OCX_COM_IO_BADID      | \
					 OCX_COM_MEM_BADID     | \
					 OCX_COM_COPR_BADID    | \
					 OCX_COM_WIN_REQ_BADID | \
					 OCX_COM_WIN_REQ_TOUT)

static const struct error_descr ocx_com_errors[] = {
	{
		.type  = ERR_CORRECTED,
		.mask  = OCX_COM_IO_BADID,
		.descr = "Invalid IO transaction node ID",
	},
	{
		.type  = ERR_CORRECTED,
		.mask  = OCX_COM_MEM_BADID,
		.descr = "Invalid memory transaction node ID",
	},
	{
		.type  = ERR_CORRECTED,
		.mask  = OCX_COM_COPR_BADID,
		.descr = "Invalid coprocessor transaction node ID",
	},
	{
		.type  = ERR_CORRECTED,
		.mask  = OCX_COM_WIN_REQ_BADID,
		.descr = "Invalid SLI transaction node ID",
	},
	{
		.type  = ERR_CORRECTED,
		.mask  = OCX_COM_WIN_REQ_TOUT,
		.descr = "Window/core request timeout",
	},
	{0, 0, NULL},
};

#define OCX_COM_LINKX_INT(x)		(0x120 + (x) * 8)
#define OCX_COM_LINKX_INT_W1S(x)	(0x140 + (x) * 8)
#define OCX_COM_LINKX_INT_ENA_W1S(x)	(0x160 + (x) * 8)
#define OCX_COM_LINKX_INT_ENA_W1C(x)	(0x180 + (x) * 8)

#define OCX_COM_LINK_BAD_WORD			BIT(13)
#define OCX_COM_LINK_ALIGN_FAIL			BIT(12)
#define OCX_COM_LINK_ALIGN_DONE			BIT(11)
#define OCX_COM_LINK_UP				BIT(10)
#define OCX_COM_LINK_STOP			BIT(9)
#define OCX_COM_LINK_BLK_ERR			BIT(8)
#define OCX_COM_LINK_REINIT			BIT(7)
#define OCX_COM_LINK_LNK_DATA			BIT(6)
#define OCX_COM_LINK_RXFIFO_DBE			BIT(5)
#define OCX_COM_LINK_RXFIFO_SBE			BIT(4)
#define OCX_COM_LINK_TXFIFO_DBE			BIT(3)
#define OCX_COM_LINK_TXFIFO_SBE			BIT(2)
#define OCX_COM_LINK_REPLAY_DBE			BIT(1)
#define OCX_COM_LINK_REPLAY_SBE			BIT(0)

static const struct error_descr ocx_com_link_errors[] = {
	{
		.type  = ERR_CORRECTED,
		.mask  = OCX_COM_LINK_REPLAY_SBE,
		.descr = "Replay buffer single-bit error",
	},
	{
		.type  = ERR_CORRECTED,
		.mask  = OCX_COM_LINK_TXFIFO_SBE,
		.descr = "TX FIFO single-bit error",
	},
	{
		.type  = ERR_CORRECTED,
		.mask  = OCX_COM_LINK_RXFIFO_SBE,
		.descr = "RX FIFO single-bit error",
	},
	{
		.type  = ERR_CORRECTED,
		.mask  = OCX_COM_LINK_BLK_ERR,
		.descr = "Block code error",
	},
	{
		.type  = ERR_CORRECTED,
		.mask  = OCX_COM_LINK_ALIGN_FAIL,
		.descr = "Link alignment failure",
	},
	{
		.type  = ERR_CORRECTED,
		.mask  = OCX_COM_LINK_BAD_WORD,
		.descr = "Bad code word",
	},
	{
		.type  = ERR_UNCORRECTED,
		.mask  = OCX_COM_LINK_REPLAY_DBE,
		.descr = "Replay buffer double-bit error",
	},
	{
		.type  = ERR_UNCORRECTED,
		.mask  = OCX_COM_LINK_TXFIFO_DBE,
		.descr = "TX FIFO double-bit error",
	},
	{
		.type  = ERR_UNCORRECTED,
		.mask  = OCX_COM_LINK_RXFIFO_DBE,
		.descr = "RX FIFO double-bit error",
	},
	{
		.type  = ERR_UNCORRECTED,
		.mask  = OCX_COM_LINK_STOP,
		.descr = "Link stopped",
	},
	{0, 0, NULL},
};

#define OCX_COM_LINK_INT_UE       (OCX_COM_LINK_REPLAY_DBE | \
				   OCX_COM_LINK_TXFIFO_DBE | \
				   OCX_COM_LINK_RXFIFO_DBE | \
				   OCX_COM_LINK_STOP)

#define OCX_COM_LINK_INT_CE       (OCX_COM_LINK_REPLAY_SBE | \
				   OCX_COM_LINK_TXFIFO_SBE | \
				   OCX_COM_LINK_RXFIFO_SBE | \
				   OCX_COM_LINK_BLK_ERR    | \
				   OCX_COM_LINK_ALIGN_FAIL | \
				   OCX_COM_LINK_BAD_WORD)

#define OCX_LNE_INT(x)			(0x8018 + (x) * 0x100)
#define OCX_LNE_INT_EN(x)		(0x8020 + (x) * 0x100)
#define OCX_LNE_BAD_CNT(x)		(0x8028 + (x) * 0x100)
#define OCX_LNE_CFG(x)			(0x8000 + (x) * 0x100)
#define OCX_LNE_STAT(x, y)		(0x8040 + (x) * 0x100 + (y) * 8)

#define OCX_LNE_CFG_RX_BDRY_LOCK_DIS		BIT(8)
#define OCX_LNE_CFG_RX_STAT_WRAP_DIS		BIT(2)
#define OCX_LNE_CFG_RX_STAT_RDCLR		BIT(1)
#define OCX_LNE_CFG_RX_STAT_ENA			BIT(0)


#define OCX_LANE_BAD_64B67B			BIT(8)
#define OCX_LANE_DSKEW_FIFO_OVFL		BIT(5)
#define OCX_LANE_SCRM_SYNC_LOSS			BIT(4)
#define OCX_LANE_UKWN_CNTL_WORD			BIT(3)
#define OCX_LANE_CRC32_ERR			BIT(2)
#define OCX_LANE_BDRY_SYNC_LOSS			BIT(1)
#define OCX_LANE_SERDES_LOCK_LOSS		BIT(0)

#define OCX_COM_LANE_INT_UE       (0)
#define OCX_COM_LANE_INT_CE       (OCX_LANE_SERDES_LOCK_LOSS | \
				   OCX_LANE_BDRY_SYNC_LOSS   | \
				   OCX_LANE_CRC32_ERR        | \
				   OCX_LANE_UKWN_CNTL_WORD   | \
				   OCX_LANE_SCRM_SYNC_LOSS   | \
				   OCX_LANE_DSKEW_FIFO_OVFL  | \
				   OCX_LANE_BAD_64B67B)

static const struct error_descr ocx_lane_errors[] = {
	{
		.type  = ERR_CORRECTED,
		.mask  = OCX_LANE_SERDES_LOCK_LOSS,
		.descr = "RX SerDes lock lost",
	},
	{
		.type  = ERR_CORRECTED,
		.mask  = OCX_LANE_BDRY_SYNC_LOSS,
		.descr = "RX word boundary lost",
	},
	{
		.type  = ERR_CORRECTED,
		.mask  = OCX_LANE_CRC32_ERR,
		.descr = "CRC32 error",
	},
	{
		.type  = ERR_CORRECTED,
		.mask  = OCX_LANE_UKWN_CNTL_WORD,
		.descr = "Unknown control word",
	},
	{
		.type  = ERR_CORRECTED,
		.mask  = OCX_LANE_SCRM_SYNC_LOSS,
		.descr = "Scrambler synchronization lost",
	},
	{
		.type  = ERR_CORRECTED,
		.mask  = OCX_LANE_DSKEW_FIFO_OVFL,
		.descr = "RX deskew FIFO overflow",
	},
	{
		.type  = ERR_CORRECTED,
		.mask  = OCX_LANE_BAD_64B67B,
		.descr = "Bad 64B/67B codeword",
	},
	{0, 0, NULL},
};

#define OCX_LNE_INT_ENA_ALL		(GENMASK(9, 8) | GENMASK(6, 0))
#define OCX_COM_INT_ENA_ALL		(GENMASK(54, 50) | GENMASK(23, 0))
#define OCX_COM_LINKX_INT_ENA_ALL	(GENMASK(13, 12) | \
					 GENMASK(9, 7) | GENMASK(5, 0))

#define OCX_TLKX_ECC_CTL(x)		(0x10018 + (x) * 0x2000)
#define OCX_RLKX_ECC_CTL(x)		(0x18018 + (x) * 0x2000)

struct ocx_com_err_ctx {
	u64 reg_com_int;
	u64 reg_lane_int[OCX_RX_LANES];
	u64 reg_lane_stat11[OCX_RX_LANES];
};

struct ocx_link_err_ctx {
	u64 reg_com_link_int;
	int link;
};

struct thunderx_ocx {
	void __iomem *regs;
	int com_link;
	struct pci_dev *pdev;
	struct edac_device_ctl_info *edac_dev;

	struct dentry *debugfs;
	struct msix_entry msix_ent[OCX_INTS];

	struct ocx_com_err_ctx com_err_ctx[RING_ENTRIES];
	struct ocx_link_err_ctx link_err_ctx[RING_ENTRIES];

	unsigned long com_ring_head;
	unsigned long com_ring_tail;

	unsigned long link_ring_head;
	unsigned long link_ring_tail;
};

#define OCX_MESSAGE_SIZE	SZ_1K
#define OCX_OTHER_SIZE		(50 * ARRAY_SIZE(ocx_com_link_errors))

/* This handler is threaded */
static irqreturn_t thunderx_ocx_com_isr(int irq, void *irq_id)
{
	struct msix_entry *msix = irq_id;
	struct thunderx_ocx *ocx = container_of(msix, struct thunderx_ocx,
						msix_ent[msix->entry]);

	int lane;
	unsigned long head = ring_pos(ocx->com_ring_head,
				      ARRAY_SIZE(ocx->com_err_ctx));
	struct ocx_com_err_ctx *ctx = &ocx->com_err_ctx[head];

	ctx->reg_com_int = readq(ocx->regs + OCX_COM_INT);

	for (lane = 0; lane < OCX_RX_LANES; lane++) {
		ctx->reg_lane_int[lane] =
			readq(ocx->regs + OCX_LNE_INT(lane));
		ctx->reg_lane_stat11[lane] =
			readq(ocx->regs + OCX_LNE_STAT(lane, 11));

		writeq(ctx->reg_lane_int[lane], ocx->regs + OCX_LNE_INT(lane));
	}

	writeq(ctx->reg_com_int, ocx->regs + OCX_COM_INT);

	ocx->com_ring_head++;

	return IRQ_WAKE_THREAD;
}

static irqreturn_t thunderx_ocx_com_threaded_isr(int irq, void *irq_id)
{
	struct msix_entry *msix = irq_id;
	struct thunderx_ocx *ocx = container_of(msix, struct thunderx_ocx,
						msix_ent[msix->entry]);

	irqreturn_t ret = IRQ_NONE;

	unsigned long tail;
	struct ocx_com_err_ctx *ctx;
	int lane;
	char *msg;
	char *other;

	msg = kmalloc(OCX_MESSAGE_SIZE, GFP_KERNEL);
	other = kmalloc(OCX_OTHER_SIZE, GFP_KERNEL);

	if (!msg || !other)
		goto err_free;

	while (CIRC_CNT(ocx->com_ring_head, ocx->com_ring_tail,
			ARRAY_SIZE(ocx->com_err_ctx))) {
		tail = ring_pos(ocx->com_ring_tail,
				ARRAY_SIZE(ocx->com_err_ctx));
		ctx = &ocx->com_err_ctx[tail];

		snprintf(msg, OCX_MESSAGE_SIZE, "%s: OCX_COM_INT: %016llx",
			ocx->edac_dev->ctl_name, ctx->reg_com_int);

		decode_register(other, OCX_OTHER_SIZE,
				ocx_com_errors, ctx->reg_com_int);

		strncat(msg, other, OCX_MESSAGE_SIZE);

		for (lane = 0; lane < OCX_RX_LANES; lane++)
			if (ctx->reg_com_int & BIT(lane)) {
				snprintf(other, OCX_OTHER_SIZE,
					 "\n\tOCX_LNE_INT[%02d]: %016llx OCX_LNE_STAT11[%02d]: %016llx",
					 lane, ctx->reg_lane_int[lane],
					 lane, ctx->reg_lane_stat11[lane]);

				strncat(msg, other, OCX_MESSAGE_SIZE);

				decode_register(other, OCX_OTHER_SIZE,
						ocx_lane_errors,
						ctx->reg_lane_int[lane]);
				strncat(msg, other, OCX_MESSAGE_SIZE);
			}

		if (ctx->reg_com_int & OCX_COM_INT_CE)
			edac_device_handle_ce(ocx->edac_dev, 0, 0, msg);

		ocx->com_ring_tail++;
	}

	ret = IRQ_HANDLED;

err_free:
	kfree(other);
	kfree(msg);

	return ret;
}

static irqreturn_t thunderx_ocx_lnk_isr(int irq, void *irq_id)
{
	struct msix_entry *msix = irq_id;
	struct thunderx_ocx *ocx = container_of(msix, struct thunderx_ocx,
						msix_ent[msix->entry]);
	unsigned long head = ring_pos(ocx->link_ring_head,
				      ARRAY_SIZE(ocx->link_err_ctx));
	struct ocx_link_err_ctx *ctx = &ocx->link_err_ctx[head];

	ctx->link = msix->entry;
	ctx->reg_com_link_int = readq(ocx->regs + OCX_COM_LINKX_INT(ctx->link));

	writeq(ctx->reg_com_link_int, ocx->regs + OCX_COM_LINKX_INT(ctx->link));

	ocx->link_ring_head++;

	return IRQ_WAKE_THREAD;
}

static irqreturn_t thunderx_ocx_lnk_threaded_isr(int irq, void *irq_id)
{
	struct msix_entry *msix = irq_id;
	struct thunderx_ocx *ocx = container_of(msix, struct thunderx_ocx,
						msix_ent[msix->entry]);
	irqreturn_t ret = IRQ_NONE;
	unsigned long tail;
	struct ocx_link_err_ctx *ctx;

	char *msg;
	char *other;

	msg = kmalloc(OCX_MESSAGE_SIZE, GFP_KERNEL);
	other = kmalloc(OCX_OTHER_SIZE, GFP_KERNEL);

	if (!msg || !other)
		goto err_free;

	while (CIRC_CNT(ocx->link_ring_head, ocx->link_ring_tail,
			ARRAY_SIZE(ocx->link_err_ctx))) {
		tail = ring_pos(ocx->link_ring_head,
				ARRAY_SIZE(ocx->link_err_ctx));

		ctx = &ocx->link_err_ctx[tail];

		snprintf(msg, OCX_MESSAGE_SIZE,
			 "%s: OCX_COM_LINK_INT[%d]: %016llx",
			 ocx->edac_dev->ctl_name,
			 ctx->link, ctx->reg_com_link_int);

		decode_register(other, OCX_OTHER_SIZE,
				ocx_com_link_errors, ctx->reg_com_link_int);

		strncat(msg, other, OCX_MESSAGE_SIZE);

		if (ctx->reg_com_link_int & OCX_COM_LINK_INT_UE)
			edac_device_handle_ue(ocx->edac_dev, 0, 0, msg);
		else if (ctx->reg_com_link_int & OCX_COM_LINK_INT_CE)
			edac_device_handle_ce(ocx->edac_dev, 0, 0, msg);

		ocx->link_ring_tail++;
	}

	ret = IRQ_HANDLED;
err_free:
	kfree(other);
	kfree(msg);

	return ret;
}

#define OCX_DEBUGFS_ATTR(_name, _reg)	DEBUGFS_REG_ATTR(ocx, _name, _reg)

OCX_DEBUGFS_ATTR(tlk0_ecc_ctl, OCX_TLKX_ECC_CTL(0));
OCX_DEBUGFS_ATTR(tlk1_ecc_ctl, OCX_TLKX_ECC_CTL(1));
OCX_DEBUGFS_ATTR(tlk2_ecc_ctl, OCX_TLKX_ECC_CTL(2));

OCX_DEBUGFS_ATTR(rlk0_ecc_ctl, OCX_RLKX_ECC_CTL(0));
OCX_DEBUGFS_ATTR(rlk1_ecc_ctl, OCX_RLKX_ECC_CTL(1));
OCX_DEBUGFS_ATTR(rlk2_ecc_ctl, OCX_RLKX_ECC_CTL(2));

OCX_DEBUGFS_ATTR(com_link0_int, OCX_COM_LINKX_INT_W1S(0));
OCX_DEBUGFS_ATTR(com_link1_int, OCX_COM_LINKX_INT_W1S(1));
OCX_DEBUGFS_ATTR(com_link2_int, OCX_COM_LINKX_INT_W1S(2));

OCX_DEBUGFS_ATTR(lne00_badcnt, OCX_LNE_BAD_CNT(0));
OCX_DEBUGFS_ATTR(lne01_badcnt, OCX_LNE_BAD_CNT(1));
OCX_DEBUGFS_ATTR(lne02_badcnt, OCX_LNE_BAD_CNT(2));
OCX_DEBUGFS_ATTR(lne03_badcnt, OCX_LNE_BAD_CNT(3));
OCX_DEBUGFS_ATTR(lne04_badcnt, OCX_LNE_BAD_CNT(4));
OCX_DEBUGFS_ATTR(lne05_badcnt, OCX_LNE_BAD_CNT(5));
OCX_DEBUGFS_ATTR(lne06_badcnt, OCX_LNE_BAD_CNT(6));
OCX_DEBUGFS_ATTR(lne07_badcnt, OCX_LNE_BAD_CNT(7));

OCX_DEBUGFS_ATTR(lne08_badcnt, OCX_LNE_BAD_CNT(8));
OCX_DEBUGFS_ATTR(lne09_badcnt, OCX_LNE_BAD_CNT(9));
OCX_DEBUGFS_ATTR(lne10_badcnt, OCX_LNE_BAD_CNT(10));
OCX_DEBUGFS_ATTR(lne11_badcnt, OCX_LNE_BAD_CNT(11));
OCX_DEBUGFS_ATTR(lne12_badcnt, OCX_LNE_BAD_CNT(12));
OCX_DEBUGFS_ATTR(lne13_badcnt, OCX_LNE_BAD_CNT(13));
OCX_DEBUGFS_ATTR(lne14_badcnt, OCX_LNE_BAD_CNT(14));
OCX_DEBUGFS_ATTR(lne15_badcnt, OCX_LNE_BAD_CNT(15));

OCX_DEBUGFS_ATTR(lne16_badcnt, OCX_LNE_BAD_CNT(16));
OCX_DEBUGFS_ATTR(lne17_badcnt, OCX_LNE_BAD_CNT(17));
OCX_DEBUGFS_ATTR(lne18_badcnt, OCX_LNE_BAD_CNT(18));
OCX_DEBUGFS_ATTR(lne19_badcnt, OCX_LNE_BAD_CNT(19));
OCX_DEBUGFS_ATTR(lne20_badcnt, OCX_LNE_BAD_CNT(20));
OCX_DEBUGFS_ATTR(lne21_badcnt, OCX_LNE_BAD_CNT(21));
OCX_DEBUGFS_ATTR(lne22_badcnt, OCX_LNE_BAD_CNT(22));
OCX_DEBUGFS_ATTR(lne23_badcnt, OCX_LNE_BAD_CNT(23));

OCX_DEBUGFS_ATTR(com_int, OCX_COM_INT_W1S);

static struct debugfs_entry *ocx_dfs_ents[] = {
	&debugfs_tlk0_ecc_ctl,
	&debugfs_tlk1_ecc_ctl,
	&debugfs_tlk2_ecc_ctl,

	&debugfs_rlk0_ecc_ctl,
	&debugfs_rlk1_ecc_ctl,
	&debugfs_rlk2_ecc_ctl,

	&debugfs_com_link0_int,
	&debugfs_com_link1_int,
	&debugfs_com_link2_int,

	&debugfs_lne00_badcnt,
	&debugfs_lne01_badcnt,
	&debugfs_lne02_badcnt,
	&debugfs_lne03_badcnt,
	&debugfs_lne04_badcnt,
	&debugfs_lne05_badcnt,
	&debugfs_lne06_badcnt,
	&debugfs_lne07_badcnt,
	&debugfs_lne08_badcnt,
	&debugfs_lne09_badcnt,
	&debugfs_lne10_badcnt,
	&debugfs_lne11_badcnt,
	&debugfs_lne12_badcnt,
	&debugfs_lne13_badcnt,
	&debugfs_lne14_badcnt,
	&debugfs_lne15_badcnt,
	&debugfs_lne16_badcnt,
	&debugfs_lne17_badcnt,
	&debugfs_lne18_badcnt,
	&debugfs_lne19_badcnt,
	&debugfs_lne20_badcnt,
	&debugfs_lne21_badcnt,
	&debugfs_lne22_badcnt,
	&debugfs_lne23_badcnt,

	&debugfs_com_int,
};

static const struct pci_device_id thunderx_ocx_pci_tbl[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_CAVIUM, PCI_DEVICE_ID_THUNDER_OCX) },
	{ 0, },
};

static void thunderx_ocx_clearstats(struct thunderx_ocx *ocx)
{
	int lane, stat, cfg;

	for (lane = 0; lane < OCX_RX_LANES; lane++) {
		cfg = readq(ocx->regs + OCX_LNE_CFG(lane));
		cfg |= OCX_LNE_CFG_RX_STAT_RDCLR;
		cfg &= ~OCX_LNE_CFG_RX_STAT_ENA;
		writeq(cfg, ocx->regs + OCX_LNE_CFG(lane));

		for (stat = 0; stat < OCX_RX_LANE_STATS; stat++)
			readq(ocx->regs + OCX_LNE_STAT(lane, stat));
	}
}

static int thunderx_ocx_probe(struct pci_dev *pdev,
			      const struct pci_device_id *id)
{
	struct thunderx_ocx *ocx;
	struct edac_device_ctl_info *edac_dev;
	char name[32];
	int idx;
	int i;
	int ret;
	u64 reg;

	ret = pcim_enable_device(pdev);
	if (ret) {
		dev_err(&pdev->dev, "Cannot enable PCI device: %d\n", ret);
		return ret;
	}

	ret = pcim_iomap_regions(pdev, BIT(0), "thunderx_ocx");
	if (ret) {
		dev_err(&pdev->dev, "Cannot map PCI resources: %d\n", ret);
		return ret;
	}

	idx = edac_device_alloc_index();
	snprintf(name, sizeof(name), "OCX%d", idx);
	edac_dev = edac_device_alloc_ctl_info(sizeof(struct thunderx_ocx),
					      name, 1, "CCPI", 1,
					      0, NULL, 0, idx);
	if (!edac_dev) {
		dev_err(&pdev->dev, "Cannot allocate EDAC device\n");
		return -ENOMEM;
	}
	ocx = edac_dev->pvt_info;
	ocx->edac_dev = edac_dev;
	ocx->com_ring_head = 0;
	ocx->com_ring_tail = 0;
	ocx->link_ring_head = 0;
	ocx->link_ring_tail = 0;

	ocx->regs = pcim_iomap_table(pdev)[0];
	if (!ocx->regs) {
		dev_err(&pdev->dev, "Cannot map PCI resources\n");
		ret = -ENODEV;
		goto err_free;
	}

	ocx->pdev = pdev;

	for (i = 0; i < OCX_INTS; i++) {
		ocx->msix_ent[i].entry = i;
		ocx->msix_ent[i].vector = 0;
	}

	ret = pci_enable_msix_exact(pdev, ocx->msix_ent, OCX_INTS);
	if (ret) {
		dev_err(&pdev->dev, "Cannot enable interrupt: %d\n", ret);
		goto err_free;
	}

	for (i = 0; i < OCX_INTS; i++) {
		ret = devm_request_threaded_irq(&pdev->dev,
						ocx->msix_ent[i].vector,
						(i == 3) ?
						 thunderx_ocx_com_isr :
						 thunderx_ocx_lnk_isr,
						(i == 3) ?
						 thunderx_ocx_com_threaded_isr :
						 thunderx_ocx_lnk_threaded_isr,
						0, "[EDAC] ThunderX OCX",
						&ocx->msix_ent[i]);
		if (ret)
			goto err_free;
	}

	edac_dev->dev = &pdev->dev;
	edac_dev->dev_name = dev_name(&pdev->dev);
	edac_dev->mod_name = "thunderx-ocx";
	edac_dev->ctl_name = "thunderx-ocx";

	ret = edac_device_add_device(edac_dev);
	if (ret) {
		dev_err(&pdev->dev, "Cannot add EDAC device: %d\n", ret);
		goto err_free;
	}

	if (IS_ENABLED(CONFIG_EDAC_DEBUG)) {
		ocx->debugfs = edac_debugfs_create_dir(pdev->dev.kobj.name);

		ret = thunderx_create_debugfs_nodes(ocx->debugfs,
						    ocx_dfs_ents,
						    ocx,
						    ARRAY_SIZE(ocx_dfs_ents));
		if (ret != ARRAY_SIZE(ocx_dfs_ents)) {
			dev_warn(&pdev->dev, "Error creating debugfs entries: %d%s\n",
				 ret, ret >= 0 ? " created" : "");
		}
	}

	pci_set_drvdata(pdev, edac_dev);

	thunderx_ocx_clearstats(ocx);

	for (i = 0; i < OCX_RX_LANES; i++) {
		writeq(OCX_LNE_INT_ENA_ALL,
		       ocx->regs + OCX_LNE_INT_EN(i));

		reg = readq(ocx->regs + OCX_LNE_INT(i));
		writeq(reg, ocx->regs + OCX_LNE_INT(i));

	}

	for (i = 0; i < OCX_LINK_INTS; i++) {
		reg = readq(ocx->regs + OCX_COM_LINKX_INT(i));
		writeq(reg, ocx->regs + OCX_COM_LINKX_INT(i));

		writeq(OCX_COM_LINKX_INT_ENA_ALL,
		       ocx->regs + OCX_COM_LINKX_INT_ENA_W1S(i));
	}

	reg = readq(ocx->regs + OCX_COM_INT);
	writeq(reg, ocx->regs + OCX_COM_INT);

	writeq(OCX_COM_INT_ENA_ALL, ocx->regs + OCX_COM_INT_ENA_W1S);

	return 0;
err_free:
	edac_device_free_ctl_info(edac_dev);

	return ret;
}

static void thunderx_ocx_remove(struct pci_dev *pdev)
{
	struct edac_device_ctl_info *edac_dev = pci_get_drvdata(pdev);
	struct thunderx_ocx *ocx = edac_dev->pvt_info;
	int i;

	writeq(OCX_COM_INT_ENA_ALL, ocx->regs + OCX_COM_INT_ENA_W1C);

	for (i = 0; i < OCX_INTS; i++) {
		writeq(OCX_COM_LINKX_INT_ENA_ALL,
		       ocx->regs + OCX_COM_LINKX_INT_ENA_W1C(i));
	}

	edac_debugfs_remove_recursive(ocx->debugfs);

	edac_device_del_device(&pdev->dev);
	edac_device_free_ctl_info(edac_dev);
}

MODULE_DEVICE_TABLE(pci, thunderx_ocx_pci_tbl);

static struct pci_driver thunderx_ocx_driver = {
	.name     = "thunderx_ocx_edac",
	.probe    = thunderx_ocx_probe,
	.remove   = thunderx_ocx_remove,
	.id_table = thunderx_ocx_pci_tbl,
};

/*---------------------- L2C driver ---------------------------------*/

#define PCI_DEVICE_ID_THUNDER_L2C_TAD 0xa02e
#define PCI_DEVICE_ID_THUNDER_L2C_CBC 0xa02f
#define PCI_DEVICE_ID_THUNDER_L2C_MCI 0xa030

#define L2C_TAD_INT_W1C		0x40000
#define L2C_TAD_INT_W1S		0x40008

#define L2C_TAD_INT_ENA_W1C	0x40020
#define L2C_TAD_INT_ENA_W1S	0x40028


#define L2C_TAD_INT_L2DDBE	 BIT(1)
#define L2C_TAD_INT_SBFSBE	 BIT(2)
#define L2C_TAD_INT_SBFDBE	 BIT(3)
#define L2C_TAD_INT_FBFSBE	 BIT(4)
#define L2C_TAD_INT_FBFDBE	 BIT(5)
#define L2C_TAD_INT_TAGDBE	 BIT(9)
#define L2C_TAD_INT_RDDISLMC	 BIT(15)
#define L2C_TAD_INT_WRDISLMC	 BIT(16)
#define L2C_TAD_INT_LFBTO	 BIT(17)
#define L2C_TAD_INT_GSYNCTO	 BIT(18)
#define L2C_TAD_INT_RTGSBE	 BIT(32)
#define L2C_TAD_INT_RTGDBE	 BIT(33)
#define L2C_TAD_INT_RDDISOCI	 BIT(34)
#define L2C_TAD_INT_WRDISOCI	 BIT(35)

#define L2C_TAD_INT_ECC		(L2C_TAD_INT_L2DDBE | \
				 L2C_TAD_INT_SBFSBE | L2C_TAD_INT_SBFDBE | \
				 L2C_TAD_INT_FBFSBE | L2C_TAD_INT_FBFDBE)

#define L2C_TAD_INT_CE          (L2C_TAD_INT_SBFSBE | \
				 L2C_TAD_INT_FBFSBE)

#define L2C_TAD_INT_UE          (L2C_TAD_INT_L2DDBE | \
				 L2C_TAD_INT_SBFDBE | \
				 L2C_TAD_INT_FBFDBE | \
				 L2C_TAD_INT_TAGDBE | \
				 L2C_TAD_INT_RTGDBE | \
				 L2C_TAD_INT_WRDISOCI | \
				 L2C_TAD_INT_RDDISOCI | \
				 L2C_TAD_INT_WRDISLMC | \
				 L2C_TAD_INT_RDDISLMC | \
				 L2C_TAD_INT_LFBTO    | \
				 L2C_TAD_INT_GSYNCTO)

static const struct error_descr l2_tad_errors[] = {
	{
		.type  = ERR_CORRECTED,
		.mask  = L2C_TAD_INT_SBFSBE,
		.descr = "SBF single-bit error",
	},
	{
		.type  = ERR_CORRECTED,
		.mask  = L2C_TAD_INT_FBFSBE,
		.descr = "FBF single-bit error",
	},
	{
		.type  = ERR_UNCORRECTED,
		.mask  = L2C_TAD_INT_L2DDBE,
		.descr = "L2D double-bit error",
	},
	{
		.type  = ERR_UNCORRECTED,
		.mask  = L2C_TAD_INT_SBFDBE,
		.descr = "SBF double-bit error",
	},
	{
		.type  = ERR_UNCORRECTED,
		.mask  = L2C_TAD_INT_FBFDBE,
		.descr = "FBF double-bit error",
	},
	{
		.type  = ERR_UNCORRECTED,
		.mask  = L2C_TAD_INT_TAGDBE,
		.descr = "TAG double-bit error",
	},
	{
		.type  = ERR_UNCORRECTED,
		.mask  = L2C_TAD_INT_RTGDBE,
		.descr = "RTG double-bit error",
	},
	{
		.type  = ERR_UNCORRECTED,
		.mask  = L2C_TAD_INT_WRDISOCI,
		.descr = "Write to a disabled CCPI",
	},
	{
		.type  = ERR_UNCORRECTED,
		.mask  = L2C_TAD_INT_RDDISOCI,
		.descr = "Read from a disabled CCPI",
	},
	{
		.type  = ERR_UNCORRECTED,
		.mask  = L2C_TAD_INT_WRDISLMC,
		.descr = "Write to a disabled LMC",
	},
	{
		.type  = ERR_UNCORRECTED,
		.mask  = L2C_TAD_INT_RDDISLMC,
		.descr = "Read from a disabled LMC",
	},
	{
		.type  = ERR_UNCORRECTED,
		.mask  = L2C_TAD_INT_LFBTO,
		.descr = "LFB entry timeout",
	},
	{
		.type  = ERR_UNCORRECTED,
		.mask  = L2C_TAD_INT_GSYNCTO,
		.descr = "Global sync CCPI timeout",
	},
	{0, 0, NULL},
};

#define L2C_TAD_INT_TAG		(L2C_TAD_INT_TAGDBE)

#define L2C_TAD_INT_RTG		(L2C_TAD_INT_RTGDBE)

#define L2C_TAD_INT_DISLMC	(L2C_TAD_INT_WRDISLMC | L2C_TAD_INT_RDDISLMC)

#define L2C_TAD_INT_DISOCI	(L2C_TAD_INT_WRDISOCI | L2C_TAD_INT_RDDISOCI)

#define L2C_TAD_INT_ENA_ALL	(L2C_TAD_INT_ECC | L2C_TAD_INT_TAG | \
				 L2C_TAD_INT_RTG | \
				 L2C_TAD_INT_DISLMC | L2C_TAD_INT_DISOCI | \
				 L2C_TAD_INT_LFBTO)

#define L2C_TAD_TIMETWO		0x50000
#define L2C_TAD_TIMEOUT		0x50100
#define L2C_TAD_ERR		0x60000
#define L2C_TAD_TQD_ERR		0x60100
#define L2C_TAD_TTG_ERR		0x60200


#define L2C_CBC_INT_W1C		0x60000

#define L2C_CBC_INT_RSDSBE	 BIT(0)
#define L2C_CBC_INT_RSDDBE	 BIT(1)

#define L2C_CBC_INT_RSD		 (L2C_CBC_INT_RSDSBE | L2C_CBC_INT_RSDDBE)

#define L2C_CBC_INT_MIBSBE	 BIT(4)
#define L2C_CBC_INT_MIBDBE	 BIT(5)

#define L2C_CBC_INT_MIB		 (L2C_CBC_INT_MIBSBE | L2C_CBC_INT_MIBDBE)

#define L2C_CBC_INT_IORDDISOCI	 BIT(6)
#define L2C_CBC_INT_IOWRDISOCI	 BIT(7)

#define L2C_CBC_INT_IODISOCI	 (L2C_CBC_INT_IORDDISOCI | \
				  L2C_CBC_INT_IOWRDISOCI)

#define L2C_CBC_INT_CE		 (L2C_CBC_INT_RSDSBE | L2C_CBC_INT_MIBSBE)
#define L2C_CBC_INT_UE		 (L2C_CBC_INT_RSDDBE | L2C_CBC_INT_MIBDBE)


static const struct error_descr l2_cbc_errors[] = {
	{
		.type  = ERR_CORRECTED,
		.mask  = L2C_CBC_INT_RSDSBE,
		.descr = "RSD single-bit error",
	},
	{
		.type  = ERR_CORRECTED,
		.mask  = L2C_CBC_INT_MIBSBE,
		.descr = "MIB single-bit error",
	},
	{
		.type  = ERR_UNCORRECTED,
		.mask  = L2C_CBC_INT_RSDDBE,
		.descr = "RSD double-bit error",
	},
	{
		.type  = ERR_UNCORRECTED,
		.mask  = L2C_CBC_INT_MIBDBE,
		.descr = "MIB double-bit error",
	},
	{
		.type  = ERR_UNCORRECTED,
		.mask  = L2C_CBC_INT_IORDDISOCI,
		.descr = "Read from a disabled CCPI",
	},
	{
		.type  = ERR_UNCORRECTED,
		.mask  = L2C_CBC_INT_IOWRDISOCI,
		.descr = "Write to a disabled CCPI",
	},
	{0, 0, NULL},
};

#define L2C_CBC_INT_W1S		0x60008
#define L2C_CBC_INT_ENA_W1C	0x60020

#define L2C_CBC_INT_ENA_ALL	 (L2C_CBC_INT_RSD | L2C_CBC_INT_MIB | \
				  L2C_CBC_INT_IODISOCI)

#define L2C_CBC_INT_ENA_W1S	0x60028

#define L2C_CBC_IODISOCIERR	0x80008
#define L2C_CBC_IOCERR		0x80010
#define L2C_CBC_RSDERR		0x80018
#define L2C_CBC_MIBERR		0x80020


#define L2C_MCI_INT_W1C		0x0

#define L2C_MCI_INT_VBFSBE	 BIT(0)
#define L2C_MCI_INT_VBFDBE	 BIT(1)

static const struct error_descr l2_mci_errors[] = {
	{
		.type  = ERR_CORRECTED,
		.mask  = L2C_MCI_INT_VBFSBE,
		.descr = "VBF single-bit error",
	},
	{
		.type  = ERR_UNCORRECTED,
		.mask  = L2C_MCI_INT_VBFDBE,
		.descr = "VBF double-bit error",
	},
	{0, 0, NULL},
};

#define L2C_MCI_INT_W1S		0x8
#define L2C_MCI_INT_ENA_W1C	0x20

#define L2C_MCI_INT_ENA_ALL	 (L2C_MCI_INT_VBFSBE | L2C_MCI_INT_VBFDBE)

#define L2C_MCI_INT_ENA_W1S	0x28

#define L2C_MCI_ERR		0x10000

#define L2C_MESSAGE_SIZE	SZ_1K
#define L2C_OTHER_SIZE		(50 * ARRAY_SIZE(l2_tad_errors))

struct l2c_err_ctx {
	char *reg_ext_name;
	u64  reg_int;
	u64  reg_ext;
};

struct thunderx_l2c {
	void __iomem *regs;
	struct pci_dev *pdev;
	struct edac_device_ctl_info *edac_dev;

	struct dentry *debugfs;

	int index;

	struct msix_entry msix_ent;

	struct l2c_err_ctx err_ctx[RING_ENTRIES];
	unsigned long ring_head;
	unsigned long ring_tail;
};

static irqreturn_t thunderx_l2c_tad_isr(int irq, void *irq_id)
{
	struct msix_entry *msix = irq_id;
	struct thunderx_l2c *tad = container_of(msix, struct thunderx_l2c,
						msix_ent);

	unsigned long head = ring_pos(tad->ring_head, ARRAY_SIZE(tad->err_ctx));
	struct l2c_err_ctx *ctx = &tad->err_ctx[head];

	ctx->reg_int = readq(tad->regs + L2C_TAD_INT_W1C);

	if (ctx->reg_int & L2C_TAD_INT_ECC) {
		ctx->reg_ext_name = "TQD_ERR";
		ctx->reg_ext = readq(tad->regs + L2C_TAD_TQD_ERR);
	} else if (ctx->reg_int & L2C_TAD_INT_TAG) {
		ctx->reg_ext_name = "TTG_ERR";
		ctx->reg_ext = readq(tad->regs + L2C_TAD_TTG_ERR);
	} else if (ctx->reg_int & L2C_TAD_INT_LFBTO) {
		ctx->reg_ext_name = "TIMEOUT";
		ctx->reg_ext = readq(tad->regs + L2C_TAD_TIMEOUT);
	} else if (ctx->reg_int & L2C_TAD_INT_DISOCI) {
		ctx->reg_ext_name = "ERR";
		ctx->reg_ext = readq(tad->regs + L2C_TAD_ERR);
	}

	writeq(ctx->reg_int, tad->regs + L2C_TAD_INT_W1C);

	tad->ring_head++;

	return IRQ_WAKE_THREAD;
}

static irqreturn_t thunderx_l2c_cbc_isr(int irq, void *irq_id)
{
	struct msix_entry *msix = irq_id;
	struct thunderx_l2c *cbc = container_of(msix, struct thunderx_l2c,
						msix_ent);

	unsigned long head = ring_pos(cbc->ring_head, ARRAY_SIZE(cbc->err_ctx));
	struct l2c_err_ctx *ctx = &cbc->err_ctx[head];

	ctx->reg_int = readq(cbc->regs + L2C_CBC_INT_W1C);

	if (ctx->reg_int & L2C_CBC_INT_RSD) {
		ctx->reg_ext_name = "RSDERR";
		ctx->reg_ext = readq(cbc->regs + L2C_CBC_RSDERR);
	} else if (ctx->reg_int & L2C_CBC_INT_MIB) {
		ctx->reg_ext_name = "MIBERR";
		ctx->reg_ext = readq(cbc->regs + L2C_CBC_MIBERR);
	} else if (ctx->reg_int & L2C_CBC_INT_IODISOCI) {
		ctx->reg_ext_name = "IODISOCIERR";
		ctx->reg_ext = readq(cbc->regs + L2C_CBC_IODISOCIERR);
	}

	writeq(ctx->reg_int, cbc->regs + L2C_CBC_INT_W1C);

	cbc->ring_head++;

	return IRQ_WAKE_THREAD;
}

static irqreturn_t thunderx_l2c_mci_isr(int irq, void *irq_id)
{
	struct msix_entry *msix = irq_id;
	struct thunderx_l2c *mci = container_of(msix, struct thunderx_l2c,
						msix_ent);

	unsigned long head = ring_pos(mci->ring_head, ARRAY_SIZE(mci->err_ctx));
	struct l2c_err_ctx *ctx = &mci->err_ctx[head];

	ctx->reg_int = readq(mci->regs + L2C_MCI_INT_W1C);
	ctx->reg_ext = readq(mci->regs + L2C_MCI_ERR);

	writeq(ctx->reg_int, mci->regs + L2C_MCI_INT_W1C);

	ctx->reg_ext_name = "ERR";

	mci->ring_head++;

	return IRQ_WAKE_THREAD;
}

static irqreturn_t thunderx_l2c_threaded_isr(int irq, void *irq_id)
{
	struct msix_entry *msix = irq_id;
	struct thunderx_l2c *l2c = container_of(msix, struct thunderx_l2c,
						msix_ent);

	unsigned long tail = ring_pos(l2c->ring_tail, ARRAY_SIZE(l2c->err_ctx));
	struct l2c_err_ctx *ctx = &l2c->err_ctx[tail];
	irqreturn_t ret = IRQ_NONE;

	u64 mask_ue, mask_ce;
	const struct error_descr *l2_errors;
	char *reg_int_name;

	char *msg;
	char *other;

	msg = kmalloc(OCX_MESSAGE_SIZE, GFP_KERNEL);
	other = kmalloc(OCX_OTHER_SIZE, GFP_KERNEL);

	if (!msg || !other)
		goto err_free;

	switch (l2c->pdev->device) {
	case PCI_DEVICE_ID_THUNDER_L2C_TAD:
		reg_int_name = "L2C_TAD_INT";
		mask_ue = L2C_TAD_INT_UE;
		mask_ce = L2C_TAD_INT_CE;
		l2_errors = l2_tad_errors;
		break;
	case PCI_DEVICE_ID_THUNDER_L2C_CBC:
		reg_int_name = "L2C_CBC_INT";
		mask_ue = L2C_CBC_INT_UE;
		mask_ce = L2C_CBC_INT_CE;
		l2_errors = l2_cbc_errors;
		break;
	case PCI_DEVICE_ID_THUNDER_L2C_MCI:
		reg_int_name = "L2C_MCI_INT";
		mask_ue = L2C_MCI_INT_VBFDBE;
		mask_ce = L2C_MCI_INT_VBFSBE;
		l2_errors = l2_mci_errors;
		break;
	default:
		dev_err(&l2c->pdev->dev, "Unsupported device: %04x\n",
			l2c->pdev->device);
		goto err_free;
	}

	while (CIRC_CNT(l2c->ring_head, l2c->ring_tail,
			ARRAY_SIZE(l2c->err_ctx))) {
		snprintf(msg, L2C_MESSAGE_SIZE,
			 "%s: %s: %016llx, %s: %016llx",
			 l2c->edac_dev->ctl_name, reg_int_name, ctx->reg_int,
			 ctx->reg_ext_name, ctx->reg_ext);

		decode_register(other, L2C_OTHER_SIZE, l2_errors, ctx->reg_int);

		strncat(msg, other, L2C_MESSAGE_SIZE);

		if (ctx->reg_int & mask_ue)
			edac_device_handle_ue(l2c->edac_dev, 0, 0, msg);
		else if (ctx->reg_int & mask_ce)
			edac_device_handle_ce(l2c->edac_dev, 0, 0, msg);

		l2c->ring_tail++;
	}

	ret = IRQ_HANDLED;

err_free:
	kfree(other);
	kfree(msg);

	return ret;
}

#define L2C_DEBUGFS_ATTR(_name, _reg)	DEBUGFS_REG_ATTR(l2c, _name, _reg)

L2C_DEBUGFS_ATTR(tad_int, L2C_TAD_INT_W1S);

static struct debugfs_entry *l2c_tad_dfs_ents[] = {
	&debugfs_tad_int,
};

L2C_DEBUGFS_ATTR(cbc_int, L2C_CBC_INT_W1S);

static struct debugfs_entry *l2c_cbc_dfs_ents[] = {
	&debugfs_cbc_int,
};

L2C_DEBUGFS_ATTR(mci_int, L2C_MCI_INT_W1S);

static struct debugfs_entry *l2c_mci_dfs_ents[] = {
	&debugfs_mci_int,
};

static const struct pci_device_id thunderx_l2c_pci_tbl[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_CAVIUM, PCI_DEVICE_ID_THUNDER_L2C_TAD), },
	{ PCI_DEVICE(PCI_VENDOR_ID_CAVIUM, PCI_DEVICE_ID_THUNDER_L2C_CBC), },
	{ PCI_DEVICE(PCI_VENDOR_ID_CAVIUM, PCI_DEVICE_ID_THUNDER_L2C_MCI), },
	{ 0, },
};

static int thunderx_l2c_probe(struct pci_dev *pdev,
			      const struct pci_device_id *id)
{
	struct thunderx_l2c *l2c;
	struct edac_device_ctl_info *edac_dev;
	struct debugfs_entry **l2c_devattr;
	size_t dfs_entries;
	irqreturn_t (*thunderx_l2c_isr)(int, void *) = NULL;
	char name[32];
	const char *fmt;
	u64 reg_en_offs, reg_en_mask;
	int idx;
	int ret;

	ret = pcim_enable_device(pdev);
	if (ret) {
		dev_err(&pdev->dev, "Cannot enable PCI device: %d\n", ret);
		return ret;
	}

	ret = pcim_iomap_regions(pdev, BIT(0), "thunderx_l2c");
	if (ret) {
		dev_err(&pdev->dev, "Cannot map PCI resources: %d\n", ret);
		return ret;
	}

	switch (pdev->device) {
	case PCI_DEVICE_ID_THUNDER_L2C_TAD:
		thunderx_l2c_isr = thunderx_l2c_tad_isr;
		l2c_devattr = l2c_tad_dfs_ents;
		dfs_entries = ARRAY_SIZE(l2c_tad_dfs_ents);
		fmt = "L2C-TAD%d";
		reg_en_offs = L2C_TAD_INT_ENA_W1S;
		reg_en_mask = L2C_TAD_INT_ENA_ALL;
		break;
	case PCI_DEVICE_ID_THUNDER_L2C_CBC:
		thunderx_l2c_isr = thunderx_l2c_cbc_isr;
		l2c_devattr = l2c_cbc_dfs_ents;
		dfs_entries = ARRAY_SIZE(l2c_cbc_dfs_ents);
		fmt = "L2C-CBC%d";
		reg_en_offs = L2C_CBC_INT_ENA_W1S;
		reg_en_mask = L2C_CBC_INT_ENA_ALL;
		break;
	case PCI_DEVICE_ID_THUNDER_L2C_MCI:
		thunderx_l2c_isr = thunderx_l2c_mci_isr;
		l2c_devattr = l2c_mci_dfs_ents;
		dfs_entries = ARRAY_SIZE(l2c_mci_dfs_ents);
		fmt = "L2C-MCI%d";
		reg_en_offs = L2C_MCI_INT_ENA_W1S;
		reg_en_mask = L2C_MCI_INT_ENA_ALL;
		break;
	default:
		//Should never ever get here
		dev_err(&pdev->dev, "Unsupported PCI device: %04x\n",
			pdev->device);
		return -EINVAL;
	}

	idx = edac_device_alloc_index();
	snprintf(name, sizeof(name), fmt, idx);

	edac_dev = edac_device_alloc_ctl_info(sizeof(struct thunderx_l2c),
					      name, 1, "L2C", 1, 0,
					      NULL, 0, idx);
	if (!edac_dev) {
		dev_err(&pdev->dev, "Cannot allocate EDAC device\n");
		return -ENOMEM;
	}

	l2c = edac_dev->pvt_info;
	l2c->edac_dev = edac_dev;

	l2c->regs = pcim_iomap_table(pdev)[0];
	if (!l2c->regs) {
		dev_err(&pdev->dev, "Cannot map PCI resources\n");
		ret = -ENODEV;
		goto err_free;
	}

	l2c->pdev = pdev;

	l2c->ring_head = 0;
	l2c->ring_tail = 0;

	l2c->msix_ent.entry = 0;
	l2c->msix_ent.vector = 0;

	ret = pci_enable_msix_exact(pdev, &l2c->msix_ent, 1);
	if (ret) {
		dev_err(&pdev->dev, "Cannot enable interrupt: %d\n", ret);
		goto err_free;
	}

	ret = devm_request_threaded_irq(&pdev->dev, l2c->msix_ent.vector,
					thunderx_l2c_isr,
					thunderx_l2c_threaded_isr,
					0, "[EDAC] ThunderX L2C",
					&l2c->msix_ent);
	if (ret)
		goto err_free;

	edac_dev->dev = &pdev->dev;
	edac_dev->dev_name = dev_name(&pdev->dev);
	edac_dev->mod_name = "thunderx-l2c";
	edac_dev->ctl_name = "thunderx-l2c";

	ret = edac_device_add_device(edac_dev);
	if (ret) {
		dev_err(&pdev->dev, "Cannot add EDAC device: %d\n", ret);
		goto err_free;
	}

	if (IS_ENABLED(CONFIG_EDAC_DEBUG)) {
		l2c->debugfs = edac_debugfs_create_dir(pdev->dev.kobj.name);

		ret = thunderx_create_debugfs_nodes(l2c->debugfs, l2c_devattr,
					      l2c, dfs_entries);

		if (ret != dfs_entries) {
			dev_warn(&pdev->dev, "Error creating debugfs entries: %d%s\n",
				 ret, ret >= 0 ? " created" : "");
		}
	}

	pci_set_drvdata(pdev, edac_dev);

	writeq(reg_en_mask, l2c->regs + reg_en_offs);

	return 0;

err_free:
	edac_device_free_ctl_info(edac_dev);

	return ret;
}

static void thunderx_l2c_remove(struct pci_dev *pdev)
{
	struct edac_device_ctl_info *edac_dev = pci_get_drvdata(pdev);
	struct thunderx_l2c *l2c = edac_dev->pvt_info;

	switch (pdev->device) {
	case PCI_DEVICE_ID_THUNDER_L2C_TAD:
		writeq(L2C_TAD_INT_ENA_ALL, l2c->regs + L2C_TAD_INT_ENA_W1C);
		break;
	case PCI_DEVICE_ID_THUNDER_L2C_CBC:
		writeq(L2C_CBC_INT_ENA_ALL, l2c->regs + L2C_CBC_INT_ENA_W1C);
		break;
	case PCI_DEVICE_ID_THUNDER_L2C_MCI:
		writeq(L2C_MCI_INT_ENA_ALL, l2c->regs + L2C_MCI_INT_ENA_W1C);
		break;
	}

	edac_debugfs_remove_recursive(l2c->debugfs);

	edac_device_del_device(&pdev->dev);
	edac_device_free_ctl_info(edac_dev);
}

MODULE_DEVICE_TABLE(pci, thunderx_l2c_pci_tbl);

static struct pci_driver thunderx_l2c_driver = {
	.name     = "thunderx_l2c_edac",
	.probe    = thunderx_l2c_probe,
	.remove   = thunderx_l2c_remove,
	.id_table = thunderx_l2c_pci_tbl,
};

static int __init thunderx_edac_init(void)
{
	int rc = 0;

	if (ghes_get_devices())
		return -EBUSY;

	rc = pci_register_driver(&thunderx_lmc_driver);
	if (rc)
		return rc;

	rc = pci_register_driver(&thunderx_ocx_driver);
	if (rc)
		goto err_lmc;

	rc = pci_register_driver(&thunderx_l2c_driver);
	if (rc)
		goto err_ocx;

	return rc;
err_ocx:
	pci_unregister_driver(&thunderx_ocx_driver);
err_lmc:
	pci_unregister_driver(&thunderx_lmc_driver);

	return rc;
}

static void __exit thunderx_edac_exit(void)
{
	pci_unregister_driver(&thunderx_l2c_driver);
	pci_unregister_driver(&thunderx_ocx_driver);
	pci_unregister_driver(&thunderx_lmc_driver);

}

module_init(thunderx_edac_init);
module_exit(thunderx_edac_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Cavium, Inc.");
MODULE_DESCRIPTION("EDAC Driver for Cavium ThunderX");
