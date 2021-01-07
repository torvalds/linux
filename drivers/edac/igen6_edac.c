// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for Intel client SoC with integrated memory controller using IBECC
 *
 * Copyright (C) 2020 Intel Corporation
 *
 * The In-Band ECC (IBECC) IP provides ECC protection to all or specific
 * regions of the physical memory space. It's used for memory controllers
 * that don't support the out-of-band ECC which often needs an additional
 * storage device to each channel for storing ECC data.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/irq_work.h>
#include <linux/llist.h>
#include <linux/genalloc.h>
#include <linux/edac.h>
#include <linux/bits.h>
#include <linux/io.h>
#include <asm/mach_traps.h>
#include <asm/nmi.h>

#include "edac_mc.h"
#include "edac_module.h"

#define IGEN6_REVISION	"v2.4"

#define EDAC_MOD_STR	"igen6_edac"
#define IGEN6_NMI_NAME	"igen6_ibecc"

/* Debug macros */
#define igen6_printk(level, fmt, arg...)		\
	edac_printk(level, "igen6", fmt, ##arg)

#define igen6_mc_printk(mci, level, fmt, arg...)	\
	edac_mc_chipset_printk(mci, level, "igen6", fmt, ##arg)

#define GET_BITFIELD(v, lo, hi) (((v) & GENMASK_ULL(hi, lo)) >> (lo))

#define NUM_IMC				1 /* Max memory controllers */
#define NUM_CHANNELS			2 /* Max channels */
#define NUM_DIMMS			2 /* Max DIMMs per channel */

#define _4GB				BIT_ULL(32)

/* Size of physical memory */
#define TOM_OFFSET			0xa0
/* Top of low usable DRAM */
#define TOLUD_OFFSET			0xbc
/* Capability register C */
#define CAPID_C_OFFSET			0xec
#define CAPID_C_IBECC			BIT(15)

/* Error Status */
#define ERRSTS_OFFSET			0xc8
#define ERRSTS_CE			BIT_ULL(6)
#define ERRSTS_UE			BIT_ULL(7)

/* Error Command */
#define ERRCMD_OFFSET			0xca
#define ERRCMD_CE			BIT_ULL(6)
#define ERRCMD_UE			BIT_ULL(7)

/* IBECC MMIO base address */
#define IBECC_BASE			(res_cfg->ibecc_base)
#define IBECC_ACTIVATE_OFFSET		IBECC_BASE
#define IBECC_ACTIVATE_EN		BIT(0)

/* IBECC error log */
#define ECC_ERROR_LOG_OFFSET		(IBECC_BASE + 0x170)
#define ECC_ERROR_LOG_CE		BIT_ULL(62)
#define ECC_ERROR_LOG_UE		BIT_ULL(63)
#define ECC_ERROR_LOG_ADDR_SHIFT	5
#define ECC_ERROR_LOG_ADDR(v)		GET_BITFIELD(v, 5, 38)
#define ECC_ERROR_LOG_SYND(v)		GET_BITFIELD(v, 46, 61)

/* Host MMIO base address */
#define MCHBAR_OFFSET			0x48
#define MCHBAR_EN			BIT_ULL(0)
#define MCHBAR_BASE(v)			(GET_BITFIELD(v, 16, 38) << 16)
#define MCHBAR_SIZE			0x10000

/* Parameters for the channel decode stage */
#define MAD_INTER_CHANNEL_OFFSET	0x5000
#define MAD_INTER_CHANNEL_DDR_TYPE(v)	GET_BITFIELD(v, 0, 2)
#define MAD_INTER_CHANNEL_ECHM(v)	GET_BITFIELD(v, 3, 3)
#define MAD_INTER_CHANNEL_CH_L_MAP(v)	GET_BITFIELD(v, 4, 4)
#define MAD_INTER_CHANNEL_CH_S_SIZE(v)	((u64)GET_BITFIELD(v, 12, 19) << 29)

/* Parameters for DRAM decode stage */
#define MAD_INTRA_CH0_OFFSET		0x5004
#define MAD_INTRA_CH_DIMM_L_MAP(v)	GET_BITFIELD(v, 0, 0)

/* DIMM characteristics */
#define MAD_DIMM_CH0_OFFSET		0x500c
#define MAD_DIMM_CH_DIMM_L_SIZE(v)	((u64)GET_BITFIELD(v, 0, 6) << 29)
#define MAD_DIMM_CH_DLW(v)		GET_BITFIELD(v, 7, 8)
#define MAD_DIMM_CH_DIMM_S_SIZE(v)	((u64)GET_BITFIELD(v, 16, 22) << 29)
#define MAD_DIMM_CH_DSW(v)		GET_BITFIELD(v, 24, 25)

/* Hash for channel selection */
#define CHANNEL_HASH_OFFSET		0X5024
/* Hash for enhanced channel selection */
#define CHANNEL_EHASH_OFFSET		0X5028
#define CHANNEL_HASH_MASK(v)		(GET_BITFIELD(v, 6, 19) << 6)
#define CHANNEL_HASH_LSB_MASK_BIT(v)	GET_BITFIELD(v, 24, 26)
#define CHANNEL_HASH_MODE(v)		GET_BITFIELD(v, 28, 28)

static struct res_config {
	int num_imc;
	u32 ibecc_base;
	bool (*ibecc_available)(struct pci_dev *pdev);
	/* Convert error address logged in IBECC to system physical address */
	u64 (*err_addr_to_sys_addr)(u64 eaddr);
	/* Convert error address logged in IBECC to integrated memory controller address */
	u64 (*err_addr_to_imc_addr)(u64 eaddr);
} *res_cfg;

struct igen6_imc {
	int mc;
	struct mem_ctl_info *mci;
	struct pci_dev *pdev;
	struct device dev;
	void __iomem *window;
	u64 ch_s_size;
	int ch_l_map;
	u64 dimm_s_size[NUM_CHANNELS];
	u64 dimm_l_size[NUM_CHANNELS];
	int dimm_l_map[NUM_CHANNELS];
};

static struct igen6_pvt {
	struct igen6_imc imc[NUM_IMC];
} *igen6_pvt;

/* The top of low usable DRAM */
static u32 igen6_tolud;
/* The size of physical memory */
static u64 igen6_tom;

struct decoded_addr {
	int mc;
	u64 imc_addr;
	u64 sys_addr;
	int channel_idx;
	u64 channel_addr;
	int sub_channel_idx;
	u64 sub_channel_addr;
};

struct ecclog_node {
	struct llist_node llnode;
	int mc;
	u64 ecclog;
};

/*
 * In the NMI handler, the driver uses the lock-less memory allocator
 * to allocate memory to store the IBECC error logs and links the logs
 * to the lock-less list. Delay printk() and the work of error reporting
 * to EDAC core in a worker.
 */
#define ECCLOG_POOL_SIZE	PAGE_SIZE
static LLIST_HEAD(ecclog_llist);
static struct gen_pool *ecclog_pool;
static char ecclog_buf[ECCLOG_POOL_SIZE];
static struct irq_work ecclog_irq_work;
static struct work_struct ecclog_work;

/* Compute die IDs for Elkhart Lake with IBECC */
#define DID_EHL_SKU5	0x4514
#define DID_EHL_SKU6	0x4528
#define DID_EHL_SKU7	0x452a
#define DID_EHL_SKU8	0x4516
#define DID_EHL_SKU9	0x452c
#define DID_EHL_SKU10	0x452e
#define DID_EHL_SKU11	0x4532
#define DID_EHL_SKU12	0x4518
#define DID_EHL_SKU13	0x451a
#define DID_EHL_SKU14	0x4534
#define DID_EHL_SKU15	0x4536

static bool ehl_ibecc_available(struct pci_dev *pdev)
{
	u32 v;

	if (pci_read_config_dword(pdev, CAPID_C_OFFSET, &v))
		return false;

	return !!(CAPID_C_IBECC & v);
}

static u64 ehl_err_addr_to_sys_addr(u64 eaddr)
{
	return eaddr;
}

static u64 ehl_err_addr_to_imc_addr(u64 eaddr)
{
	if (eaddr < igen6_tolud)
		return eaddr;

	if (igen6_tom <= _4GB)
		return eaddr + igen6_tolud - _4GB;

	if (eaddr < _4GB)
		return eaddr + igen6_tolud - igen6_tom;

	return eaddr;
}

static struct res_config ehl_cfg = {
	.num_imc	 = 1,
	.ibecc_base	 = 0xdc00,
	.ibecc_available = ehl_ibecc_available,
	.err_addr_to_sys_addr  = ehl_err_addr_to_sys_addr,
	.err_addr_to_imc_addr  = ehl_err_addr_to_imc_addr,
};

static const struct pci_device_id igen6_pci_tbl[] = {
	{ PCI_VDEVICE(INTEL, DID_EHL_SKU5), (kernel_ulong_t)&ehl_cfg },
	{ PCI_VDEVICE(INTEL, DID_EHL_SKU6), (kernel_ulong_t)&ehl_cfg },
	{ PCI_VDEVICE(INTEL, DID_EHL_SKU7), (kernel_ulong_t)&ehl_cfg },
	{ PCI_VDEVICE(INTEL, DID_EHL_SKU8), (kernel_ulong_t)&ehl_cfg },
	{ PCI_VDEVICE(INTEL, DID_EHL_SKU9), (kernel_ulong_t)&ehl_cfg },
	{ PCI_VDEVICE(INTEL, DID_EHL_SKU10), (kernel_ulong_t)&ehl_cfg },
	{ PCI_VDEVICE(INTEL, DID_EHL_SKU11), (kernel_ulong_t)&ehl_cfg },
	{ PCI_VDEVICE(INTEL, DID_EHL_SKU12), (kernel_ulong_t)&ehl_cfg },
	{ PCI_VDEVICE(INTEL, DID_EHL_SKU13), (kernel_ulong_t)&ehl_cfg },
	{ PCI_VDEVICE(INTEL, DID_EHL_SKU14), (kernel_ulong_t)&ehl_cfg },
	{ PCI_VDEVICE(INTEL, DID_EHL_SKU15), (kernel_ulong_t)&ehl_cfg },
	{ },
};
MODULE_DEVICE_TABLE(pci, igen6_pci_tbl);

static enum dev_type get_width(int dimm_l, u32 mad_dimm)
{
	u32 w = dimm_l ? MAD_DIMM_CH_DLW(mad_dimm) :
			 MAD_DIMM_CH_DSW(mad_dimm);

	switch (w) {
	case 0:
		return DEV_X8;
	case 1:
		return DEV_X16;
	case 2:
		return DEV_X32;
	default:
		return DEV_UNKNOWN;
	}
}

static enum mem_type get_memory_type(u32 mad_inter)
{
	u32 t = MAD_INTER_CHANNEL_DDR_TYPE(mad_inter);

	switch (t) {
	case 0:
		return MEM_DDR4;
	case 1:
		return MEM_DDR3;
	case 2:
		return MEM_LPDDR3;
	case 3:
		return MEM_LPDDR4;
	case 4:
		return MEM_WIO2;
	default:
		return MEM_UNKNOWN;
	}
}

static int decode_chan_idx(u64 addr, u64 mask, int intlv_bit)
{
	u64 hash_addr = addr & mask, hash = 0;
	u64 intlv = (addr >> intlv_bit) & 1;
	int i;

	for (i = 6; i < 20; i++)
		hash ^= (hash_addr >> i) & 1;

	return (int)hash ^ intlv;
}

static u64 decode_channel_addr(u64 addr, int intlv_bit)
{
	u64 channel_addr;

	/* Remove the interleave bit and shift upper part down to fill gap */
	channel_addr  = GET_BITFIELD(addr, intlv_bit + 1, 63) << intlv_bit;
	channel_addr |= GET_BITFIELD(addr, 0, intlv_bit - 1);

	return channel_addr;
}

static void decode_addr(u64 addr, u32 hash, u64 s_size, int l_map,
			int *idx, u64 *sub_addr)
{
	int intlv_bit = CHANNEL_HASH_LSB_MASK_BIT(hash) + 6;

	if (addr > 2 * s_size) {
		*sub_addr = addr - s_size;
		*idx = l_map;
		return;
	}

	if (CHANNEL_HASH_MODE(hash)) {
		*sub_addr = decode_channel_addr(addr, intlv_bit);
		*idx = decode_chan_idx(addr, CHANNEL_HASH_MASK(hash), intlv_bit);
	} else {
		*sub_addr = decode_channel_addr(addr, 6);
		*idx = GET_BITFIELD(addr, 6, 6);
	}
}

static int igen6_decode(struct decoded_addr *res)
{
	struct igen6_imc *imc = &igen6_pvt->imc[res->mc];
	u64 addr = res->imc_addr, sub_addr, s_size;
	int idx, l_map;
	u32 hash;

	if (addr >= igen6_tom) {
		edac_dbg(0, "Address 0x%llx out of range\n", addr);
		return -EINVAL;
	}

	/* Decode channel */
	hash   = readl(imc->window + CHANNEL_HASH_OFFSET);
	s_size = imc->ch_s_size;
	l_map  = imc->ch_l_map;
	decode_addr(addr, hash, s_size, l_map, &idx, &sub_addr);
	res->channel_idx  = idx;
	res->channel_addr = sub_addr;

	/* Decode sub-channel/DIMM */
	hash   = readl(imc->window + CHANNEL_EHASH_OFFSET);
	s_size = imc->dimm_s_size[idx];
	l_map  = imc->dimm_l_map[idx];
	decode_addr(res->channel_addr, hash, s_size, l_map, &idx, &sub_addr);
	res->sub_channel_idx  = idx;
	res->sub_channel_addr = sub_addr;

	return 0;
}

static void igen6_output_error(struct decoded_addr *res,
			       struct mem_ctl_info *mci, u64 ecclog)
{
	enum hw_event_mc_err_type type = ecclog & ECC_ERROR_LOG_UE ?
					 HW_EVENT_ERR_UNCORRECTED :
					 HW_EVENT_ERR_CORRECTED;

	edac_mc_handle_error(type, mci, 1,
			     res->sys_addr >> PAGE_SHIFT,
			     res->sys_addr & ~PAGE_MASK,
			     ECC_ERROR_LOG_SYND(ecclog),
			     res->channel_idx, res->sub_channel_idx,
			     -1, "", "");
}

static struct gen_pool *ecclog_gen_pool_create(void)
{
	struct gen_pool *pool;

	pool = gen_pool_create(ilog2(sizeof(struct ecclog_node)), -1);
	if (!pool)
		return NULL;

	if (gen_pool_add(pool, (unsigned long)ecclog_buf, ECCLOG_POOL_SIZE, -1)) {
		gen_pool_destroy(pool);
		return NULL;
	}

	return pool;
}

static int ecclog_gen_pool_add(int mc, u64 ecclog)
{
	struct ecclog_node *node;

	node = (void *)gen_pool_alloc(ecclog_pool, sizeof(*node));
	if (!node)
		return -ENOMEM;

	node->mc = mc;
	node->ecclog = ecclog;
	llist_add(&node->llnode, &ecclog_llist);

	return 0;
}

/*
 * Either the memory-mapped I/O status register ECC_ERROR_LOG or the PCI
 * configuration space status register ERRSTS can indicate whether a
 * correctable error or an uncorrectable error occurred. We only use the
 * ECC_ERROR_LOG register to check error type, but need to clear both
 * registers to enable future error events.
 */
static u64 ecclog_read_and_clear(struct igen6_imc *imc)
{
	u64 ecclog = readq(imc->window + ECC_ERROR_LOG_OFFSET);

	if (ecclog & (ECC_ERROR_LOG_CE | ECC_ERROR_LOG_UE)) {
		/* Clear CE/UE bits by writing 1s */
		writeq(ecclog, imc->window + ECC_ERROR_LOG_OFFSET);
		return ecclog;
	}

	return 0;
}

static void errsts_clear(struct igen6_imc *imc)
{
	u16 errsts;

	if (pci_read_config_word(imc->pdev, ERRSTS_OFFSET, &errsts)) {
		igen6_printk(KERN_ERR, "Failed to read ERRSTS\n");
		return;
	}

	/* Clear CE/UE bits by writing 1s */
	if (errsts & (ERRSTS_CE | ERRSTS_UE))
		pci_write_config_word(imc->pdev, ERRSTS_OFFSET, errsts);
}

static int errcmd_enable_error_reporting(bool enable)
{
	struct igen6_imc *imc = &igen6_pvt->imc[0];
	u16 errcmd;
	int rc;

	rc = pci_read_config_word(imc->pdev, ERRCMD_OFFSET, &errcmd);
	if (rc)
		return rc;

	if (enable)
		errcmd |= ERRCMD_CE | ERRSTS_UE;
	else
		errcmd &= ~(ERRCMD_CE | ERRSTS_UE);

	rc = pci_write_config_word(imc->pdev, ERRCMD_OFFSET, errcmd);
	if (rc)
		return rc;

	return 0;
}

static int ecclog_handler(void)
{
	struct igen6_imc *imc;
	int i, n = 0;
	u64 ecclog;

	for (i = 0; i < res_cfg->num_imc; i++) {
		imc = &igen6_pvt->imc[i];

		/* errsts_clear() isn't NMI-safe. Delay it in the IRQ context */

		ecclog = ecclog_read_and_clear(imc);
		if (!ecclog)
			continue;

		if (!ecclog_gen_pool_add(i, ecclog))
			irq_work_queue(&ecclog_irq_work);

		n++;
	}

	return n;
}

static void ecclog_work_cb(struct work_struct *work)
{
	struct ecclog_node *node, *tmp;
	struct mem_ctl_info *mci;
	struct llist_node *head;
	struct decoded_addr res;
	u64 eaddr;

	head = llist_del_all(&ecclog_llist);
	if (!head)
		return;

	llist_for_each_entry_safe(node, tmp, head, llnode) {
		memset(&res, 0, sizeof(res));
		eaddr = ECC_ERROR_LOG_ADDR(node->ecclog) <<
			ECC_ERROR_LOG_ADDR_SHIFT;
		res.mc	     = node->mc;
		res.sys_addr = res_cfg->err_addr_to_sys_addr(eaddr);
		res.imc_addr = res_cfg->err_addr_to_imc_addr(eaddr);

		mci = igen6_pvt->imc[res.mc].mci;

		edac_dbg(2, "MC %d, ecclog = 0x%llx\n", node->mc, node->ecclog);
		igen6_mc_printk(mci, KERN_DEBUG, "HANDLING IBECC MEMORY ERROR\n");
		igen6_mc_printk(mci, KERN_DEBUG, "ADDR 0x%llx ", res.sys_addr);

		if (!igen6_decode(&res))
			igen6_output_error(&res, mci, node->ecclog);

		gen_pool_free(ecclog_pool, (unsigned long)node, sizeof(*node));
	}
}

static void ecclog_irq_work_cb(struct irq_work *irq_work)
{
	int i;

	for (i = 0; i < res_cfg->num_imc; i++)
		errsts_clear(&igen6_pvt->imc[i]);

	if (!llist_empty(&ecclog_llist))
		schedule_work(&ecclog_work);
}

static int ecclog_nmi_handler(unsigned int cmd, struct pt_regs *regs)
{
	unsigned char reason;

	if (!ecclog_handler())
		return NMI_DONE;

	/*
	 * Both In-Band ECC correctable error and uncorrectable error are
	 * reported by SERR# NMI. The NMI generic code (see pci_serr_error())
	 * doesn't clear the bit NMI_REASON_CLEAR_SERR (in port 0x61) to
	 * re-enable the SERR# NMI after NMI handling. So clear this bit here
	 * to re-enable SERR# NMI for receiving future In-Band ECC errors.
	 */
	reason  = x86_platform.get_nmi_reason() & NMI_REASON_CLEAR_MASK;
	reason |= NMI_REASON_CLEAR_SERR;
	outb(reason, NMI_REASON_PORT);
	reason &= ~NMI_REASON_CLEAR_SERR;
	outb(reason, NMI_REASON_PORT);

	return NMI_HANDLED;
}

static bool igen6_check_ecc(struct igen6_imc *imc)
{
	u32 activate = readl(imc->window + IBECC_ACTIVATE_OFFSET);

	return !!(activate & IBECC_ACTIVATE_EN);
}

static int igen6_get_dimm_config(struct mem_ctl_info *mci)
{
	struct igen6_imc *imc = mci->pvt_info;
	u32 mad_inter, mad_intra, mad_dimm;
	int i, j, ndimms, mc = imc->mc;
	struct dimm_info *dimm;
	enum mem_type mtype;
	enum dev_type dtype;
	u64 dsize;
	bool ecc;

	edac_dbg(2, "\n");

	mad_inter = readl(imc->window + MAD_INTER_CHANNEL_OFFSET);
	mtype = get_memory_type(mad_inter);
	ecc = igen6_check_ecc(imc);
	imc->ch_s_size = MAD_INTER_CHANNEL_CH_S_SIZE(mad_inter);
	imc->ch_l_map  = MAD_INTER_CHANNEL_CH_L_MAP(mad_inter);

	for (i = 0; i < NUM_CHANNELS; i++) {
		mad_intra = readl(imc->window + MAD_INTRA_CH0_OFFSET + i * 4);
		mad_dimm  = readl(imc->window + MAD_DIMM_CH0_OFFSET + i * 4);

		imc->dimm_l_size[i] = MAD_DIMM_CH_DIMM_L_SIZE(mad_dimm);
		imc->dimm_s_size[i] = MAD_DIMM_CH_DIMM_S_SIZE(mad_dimm);
		imc->dimm_l_map[i]  = MAD_INTRA_CH_DIMM_L_MAP(mad_intra);
		ndimms = 0;

		for (j = 0; j < NUM_DIMMS; j++) {
			dimm = edac_get_dimm(mci, i, j, 0);

			if (j ^ imc->dimm_l_map[i]) {
				dtype = get_width(0, mad_dimm);
				dsize = imc->dimm_s_size[i];
			} else {
				dtype = get_width(1, mad_dimm);
				dsize = imc->dimm_l_size[i];
			}

			if (!dsize)
				continue;

			dimm->grain = 64;
			dimm->mtype = mtype;
			dimm->dtype = dtype;
			dimm->nr_pages  = MiB_TO_PAGES(dsize >> 20);
			dimm->edac_mode = EDAC_SECDED;
			snprintf(dimm->label, sizeof(dimm->label),
				 "MC#%d_Chan#%d_DIMM#%d", mc, i, j);
			edac_dbg(0, "MC %d, Channel %d, DIMM %d, Size %llu MiB (%u pages)\n",
				 mc, i, j, dsize >> 20, dimm->nr_pages);

			ndimms++;
		}

		if (ndimms && !ecc) {
			igen6_printk(KERN_ERR, "MC%d In-Band ECC is disabled\n", mc);
			return -ENODEV;
		}
	}

	return 0;
}

#ifdef CONFIG_EDAC_DEBUG
/* Top of upper usable DRAM */
static u64 igen6_touud;
#define TOUUD_OFFSET	0xa8

static void igen6_reg_dump(struct igen6_imc *imc)
{
	int i;

	edac_dbg(2, "CHANNEL_HASH     : 0x%x\n",
		 readl(imc->window + CHANNEL_HASH_OFFSET));
	edac_dbg(2, "CHANNEL_EHASH    : 0x%x\n",
		 readl(imc->window + CHANNEL_EHASH_OFFSET));
	edac_dbg(2, "MAD_INTER_CHANNEL: 0x%x\n",
		 readl(imc->window + MAD_INTER_CHANNEL_OFFSET));
	edac_dbg(2, "ECC_ERROR_LOG    : 0x%llx\n",
		 readq(imc->window + ECC_ERROR_LOG_OFFSET));

	for (i = 0; i < NUM_CHANNELS; i++) {
		edac_dbg(2, "MAD_INTRA_CH%d    : 0x%x\n", i,
			 readl(imc->window + MAD_INTRA_CH0_OFFSET + i * 4));
		edac_dbg(2, "MAD_DIMM_CH%d     : 0x%x\n", i,
			 readl(imc->window + MAD_DIMM_CH0_OFFSET + i * 4));
	}
	edac_dbg(2, "TOLUD            : 0x%x", igen6_tolud);
	edac_dbg(2, "TOUUD            : 0x%llx", igen6_touud);
	edac_dbg(2, "TOM              : 0x%llx", igen6_tom);
}

static struct dentry *igen6_test;

static int debugfs_u64_set(void *data, u64 val)
{
	u64 ecclog;

	if ((val >= igen6_tolud && val < _4GB) || val >= igen6_touud) {
		edac_dbg(0, "Address 0x%llx out of range\n", val);
		return 0;
	}

	pr_warn_once("Fake error to 0x%llx injected via debugfs\n", val);

	val  >>= ECC_ERROR_LOG_ADDR_SHIFT;
	ecclog = (val << ECC_ERROR_LOG_ADDR_SHIFT) | ECC_ERROR_LOG_CE;

	if (!ecclog_gen_pool_add(0, ecclog))
		irq_work_queue(&ecclog_irq_work);

	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(fops_u64_wo, NULL, debugfs_u64_set, "%llu\n");

static void igen6_debug_setup(void)
{
	igen6_test = edac_debugfs_create_dir("igen6_test");
	if (!igen6_test)
		return;

	if (!edac_debugfs_create_file("addr", 0200, igen6_test,
				      NULL, &fops_u64_wo)) {
		debugfs_remove(igen6_test);
		igen6_test = NULL;
	}
}

static void igen6_debug_teardown(void)
{
	debugfs_remove_recursive(igen6_test);
}
#else
static void igen6_reg_dump(struct igen6_imc *imc) {}
static void igen6_debug_setup(void) {}
static void igen6_debug_teardown(void) {}
#endif

static int igen6_pci_setup(struct pci_dev *pdev, u64 *mchbar)
{
	union  {
		u64 v;
		struct {
			u32 v_lo;
			u32 v_hi;
		};
	} u;

	edac_dbg(2, "\n");

	if (!res_cfg->ibecc_available(pdev)) {
		edac_dbg(2, "No In-Band ECC IP\n");
		goto fail;
	}

	if (pci_read_config_dword(pdev, TOLUD_OFFSET, &igen6_tolud)) {
		igen6_printk(KERN_ERR, "Failed to read TOLUD\n");
		goto fail;
	}

	igen6_tolud &= GENMASK(31, 20);

	if (pci_read_config_dword(pdev, TOM_OFFSET, &u.v_lo)) {
		igen6_printk(KERN_ERR, "Failed to read lower TOM\n");
		goto fail;
	}

	if (pci_read_config_dword(pdev, TOM_OFFSET + 4, &u.v_hi)) {
		igen6_printk(KERN_ERR, "Failed to read upper TOM\n");
		goto fail;
	}

	igen6_tom = u.v & GENMASK_ULL(38, 20);

	if (pci_read_config_dword(pdev, MCHBAR_OFFSET, &u.v_lo)) {
		igen6_printk(KERN_ERR, "Failed to read lower MCHBAR\n");
		goto fail;
	}

	if (pci_read_config_dword(pdev, MCHBAR_OFFSET + 4, &u.v_hi)) {
		igen6_printk(KERN_ERR, "Failed to read upper MCHBAR\n");
		goto fail;
	}

	if (!(u.v & MCHBAR_EN)) {
		igen6_printk(KERN_ERR, "MCHBAR is disabled\n");
		goto fail;
	}

	*mchbar = MCHBAR_BASE(u.v);

#ifdef CONFIG_EDAC_DEBUG
	if (pci_read_config_dword(pdev, TOUUD_OFFSET, &u.v_lo))
		edac_dbg(2, "Failed to read lower TOUUD\n");
	else if (pci_read_config_dword(pdev, TOUUD_OFFSET + 4, &u.v_hi))
		edac_dbg(2, "Failed to read upper TOUUD\n");
	else
		igen6_touud = u.v & GENMASK_ULL(38, 20);
#endif

	return 0;
fail:
	return -ENODEV;
}

static int igen6_register_mci(int mc, u64 mchbar, struct pci_dev *pdev)
{
	struct edac_mc_layer layers[2];
	struct mem_ctl_info *mci;
	struct igen6_imc *imc;
	void __iomem *window;
	int rc;

	edac_dbg(2, "\n");

	mchbar += mc * MCHBAR_SIZE;
	window = ioremap(mchbar, MCHBAR_SIZE);
	if (!window) {
		igen6_printk(KERN_ERR, "Failed to ioremap 0x%llx\n", mchbar);
		return -ENODEV;
	}

	layers[0].type = EDAC_MC_LAYER_CHANNEL;
	layers[0].size = NUM_CHANNELS;
	layers[0].is_virt_csrow = false;
	layers[1].type = EDAC_MC_LAYER_SLOT;
	layers[1].size = NUM_DIMMS;
	layers[1].is_virt_csrow = true;

	mci = edac_mc_alloc(mc, ARRAY_SIZE(layers), layers, 0);
	if (!mci) {
		rc = -ENOMEM;
		goto fail;
	}

	mci->ctl_name = kasprintf(GFP_KERNEL, "Intel_client_SoC MC#%d", mc);
	if (!mci->ctl_name) {
		rc = -ENOMEM;
		goto fail2;
	}

	mci->mtype_cap = MEM_FLAG_LPDDR4 | MEM_FLAG_DDR4;
	mci->edac_ctl_cap = EDAC_FLAG_SECDED;
	mci->edac_cap = EDAC_FLAG_SECDED;
	mci->mod_name = EDAC_MOD_STR;
	mci->dev_name = pci_name(pdev);
	mci->pvt_info = &igen6_pvt->imc[mc];

	imc = mci->pvt_info;
	device_initialize(&imc->dev);
	/*
	 * EDAC core uses mci->pdev(pointer of structure device) as
	 * memory controller ID. The client SoCs attach one or more
	 * memory controllers to single pci_dev (single pci_dev->dev
	 * can be for multiple memory controllers).
	 *
	 * To make mci->pdev unique, assign pci_dev->dev to mci->pdev
	 * for the first memory controller and assign a unique imc->dev
	 * to mci->pdev for each non-first memory controller.
	 */
	mci->pdev = mc ? &imc->dev : &pdev->dev;
	imc->mc	= mc;
	imc->pdev = pdev;
	imc->window = window;

	igen6_reg_dump(imc);

	rc = igen6_get_dimm_config(mci);
	if (rc)
		goto fail3;

	rc = edac_mc_add_mc(mci);
	if (rc) {
		igen6_printk(KERN_ERR, "Failed to register mci#%d\n", mc);
		goto fail3;
	}

	imc->mci = mci;
	return 0;
fail3:
	kfree(mci->ctl_name);
fail2:
	edac_mc_free(mci);
fail:
	iounmap(window);
	return rc;
}

static void igen6_unregister_mcis(void)
{
	struct mem_ctl_info *mci;
	struct igen6_imc *imc;
	int i;

	edac_dbg(2, "\n");

	for (i = 0; i < res_cfg->num_imc; i++) {
		imc = &igen6_pvt->imc[i];
		mci = imc->mci;
		if (!mci)
			continue;

		edac_mc_del_mc(mci->pdev);
		kfree(mci->ctl_name);
		edac_mc_free(mci);
		iounmap(imc->window);
	}
}

static int igen6_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	u64 mchbar;
	int i, rc;

	edac_dbg(2, "\n");

	igen6_pvt = kzalloc(sizeof(*igen6_pvt), GFP_KERNEL);
	if (!igen6_pvt)
		return -ENOMEM;

	res_cfg = (struct res_config *)ent->driver_data;

	rc = igen6_pci_setup(pdev, &mchbar);
	if (rc)
		goto fail;

	for (i = 0; i < res_cfg->num_imc; i++) {
		rc = igen6_register_mci(i, mchbar, pdev);
		if (rc)
			goto fail2;
	}

	ecclog_pool = ecclog_gen_pool_create();
	if (!ecclog_pool) {
		rc = -ENOMEM;
		goto fail2;
	}

	INIT_WORK(&ecclog_work, ecclog_work_cb);
	init_irq_work(&ecclog_irq_work, ecclog_irq_work_cb);

	/* Check if any pending errors before registering the NMI handler */
	ecclog_handler();

	rc = register_nmi_handler(NMI_SERR, ecclog_nmi_handler,
				  0, IGEN6_NMI_NAME);
	if (rc) {
		igen6_printk(KERN_ERR, "Failed to register NMI handler\n");
		goto fail3;
	}

	/* Enable error reporting */
	rc = errcmd_enable_error_reporting(true);
	if (rc) {
		igen6_printk(KERN_ERR, "Failed to enable error reporting\n");
		goto fail4;
	}

	igen6_debug_setup();
	return 0;
fail4:
	unregister_nmi_handler(NMI_SERR, IGEN6_NMI_NAME);
fail3:
	gen_pool_destroy(ecclog_pool);
fail2:
	igen6_unregister_mcis();
fail:
	kfree(igen6_pvt);
	return rc;
}

static void igen6_remove(struct pci_dev *pdev)
{
	edac_dbg(2, "\n");

	igen6_debug_teardown();
	errcmd_enable_error_reporting(false);
	unregister_nmi_handler(NMI_SERR, IGEN6_NMI_NAME);
	irq_work_sync(&ecclog_irq_work);
	flush_work(&ecclog_work);
	gen_pool_destroy(ecclog_pool);
	igen6_unregister_mcis();
	kfree(igen6_pvt);
}

static struct pci_driver igen6_driver = {
	.name     = EDAC_MOD_STR,
	.probe    = igen6_probe,
	.remove   = igen6_remove,
	.id_table = igen6_pci_tbl,
};

static int __init igen6_init(void)
{
	const char *owner;
	int rc;

	edac_dbg(2, "\n");

	owner = edac_get_owner();
	if (owner && strncmp(owner, EDAC_MOD_STR, sizeof(EDAC_MOD_STR)))
		return -ENODEV;

	edac_op_state = EDAC_OPSTATE_NMI;

	rc = pci_register_driver(&igen6_driver);
	if (rc)
		return rc;

	igen6_printk(KERN_INFO, "%s\n", IGEN6_REVISION);

	return 0;
}

static void __exit igen6_exit(void)
{
	edac_dbg(2, "\n");

	pci_unregister_driver(&igen6_driver);
}

module_init(igen6_init);
module_exit(igen6_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Qiuxu Zhuo");
MODULE_DESCRIPTION("MC Driver for Intel client SoC using In-Band ECC");
