// SPDX-License-Identifier: GPL-2.0-only
/* chmc.c: Driver for UltraSPARC-III memory controller.
 *
 * Copyright (C) 2001, 2007, 2008 David S. Miller (davem@davemloft.net)
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <asm/spitfire.h>
#include <asm/chmctrl.h>
#include <asm/cpudata.h>
#include <asm/oplib.h>
#include <asm/prom.h>
#include <asm/head.h>
#include <asm/io.h>
#include <asm/memctrl.h>

#define DRV_MODULE_NAME		"chmc"
#define PFX DRV_MODULE_NAME	": "
#define DRV_MODULE_VERSION	"0.2"

MODULE_AUTHOR("David S. Miller (davem@davemloft.net)");
MODULE_DESCRIPTION("UltraSPARC-III memory controller driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_MODULE_VERSION);

static int mc_type;
#define MC_TYPE_SAFARI		1
#define MC_TYPE_JBUS		2

static dimm_printer_t us3mc_dimm_printer;

#define CHMCTRL_NDGRPS	2
#define CHMCTRL_NDIMMS	4

#define CHMC_DIMMS_PER_MC	(CHMCTRL_NDGRPS * CHMCTRL_NDIMMS)

/* OBP memory-layout property format. */
struct chmc_obp_map {
	unsigned char	dimm_map[144];
	unsigned char	pin_map[576];
};

#define DIMM_LABEL_SZ	8

struct chmc_obp_mem_layout {
	/* One max 8-byte string label per DIMM.  Usually
	 * this matches the label on the motherboard where
	 * that DIMM resides.
	 */
	char			dimm_labels[CHMC_DIMMS_PER_MC][DIMM_LABEL_SZ];

	/* If symmetric use map[0], else it is
	 * asymmetric and map[1] should be used.
	 */
	char			symmetric;

	struct chmc_obp_map	map[2];
};

#define CHMCTRL_NBANKS	4

struct chmc_bank_info {
	struct chmc		*p;
	int			bank_id;

	u64			raw_reg;
	int			valid;
	int			uk;
	int			um;
	int			lk;
	int			lm;
	int			interleave;
	unsigned long		base;
	unsigned long		size;
};

struct chmc {
	struct list_head		list;
	int				portid;

	struct chmc_obp_mem_layout	layout_prop;
	int				layout_size;

	void __iomem			*regs;

	u64				timing_control1;
	u64				timing_control2;
	u64				timing_control3;
	u64				timing_control4;
	u64				memaddr_control;

	struct chmc_bank_info		logical_banks[CHMCTRL_NBANKS];
};

#define JBUSMC_REGS_SIZE		8

#define JB_MC_REG1_DIMM2_BANK3		0x8000000000000000UL
#define JB_MC_REG1_DIMM1_BANK1		0x4000000000000000UL
#define JB_MC_REG1_DIMM2_BANK2		0x2000000000000000UL
#define JB_MC_REG1_DIMM1_BANK0		0x1000000000000000UL
#define JB_MC_REG1_XOR			0x0000010000000000UL
#define JB_MC_REG1_ADDR_GEN_2		0x000000e000000000UL
#define JB_MC_REG1_ADDR_GEN_2_SHIFT	37
#define JB_MC_REG1_ADDR_GEN_1		0x0000001c00000000UL
#define JB_MC_REG1_ADDR_GEN_1_SHIFT	34
#define JB_MC_REG1_INTERLEAVE		0x0000000001800000UL
#define JB_MC_REG1_INTERLEAVE_SHIFT	23
#define JB_MC_REG1_DIMM2_PTYPE		0x0000000000200000UL
#define JB_MC_REG1_DIMM2_PTYPE_SHIFT	21
#define JB_MC_REG1_DIMM1_PTYPE		0x0000000000100000UL
#define JB_MC_REG1_DIMM1_PTYPE_SHIFT	20

#define PART_TYPE_X8		0
#define PART_TYPE_X4		1

#define INTERLEAVE_NONE		0
#define INTERLEAVE_SAME		1
#define INTERLEAVE_INTERNAL	2
#define INTERLEAVE_BOTH		3

#define ADDR_GEN_128MB		0
#define ADDR_GEN_256MB		1
#define ADDR_GEN_512MB		2
#define ADDR_GEN_1GB		3

#define JB_NUM_DIMM_GROUPS	2
#define JB_NUM_DIMMS_PER_GROUP	2
#define JB_NUM_DIMMS		(JB_NUM_DIMM_GROUPS * JB_NUM_DIMMS_PER_GROUP)

struct jbusmc_obp_map {
	unsigned char	dimm_map[18];
	unsigned char	pin_map[144];
};

struct jbusmc_obp_mem_layout {
	/* One max 8-byte string label per DIMM.  Usually
	 * this matches the label on the motherboard where
	 * that DIMM resides.
	 */
	char		dimm_labels[JB_NUM_DIMMS][DIMM_LABEL_SZ];

	/* If symmetric use map[0], else it is
	 * asymmetric and map[1] should be used.
	 */
	char			symmetric;

	struct jbusmc_obp_map	map;

	char			_pad;
};

struct jbusmc_dimm_group {
	struct jbusmc			*controller;
	int				index;
	u64				base_addr;
	u64				size;
};

struct jbusmc {
	void __iomem			*regs;
	u64				mc_reg_1;
	u32				portid;
	struct jbusmc_obp_mem_layout	layout;
	int				layout_len;
	int				num_dimm_groups;
	struct jbusmc_dimm_group	dimm_groups[JB_NUM_DIMM_GROUPS];
	struct list_head		list;
};

static DEFINE_SPINLOCK(mctrl_list_lock);
static LIST_HEAD(mctrl_list);

static void mc_list_add(struct list_head *list)
{
	spin_lock(&mctrl_list_lock);
	list_add(list, &mctrl_list);
	spin_unlock(&mctrl_list_lock);
}

static void mc_list_del(struct list_head *list)
{
	spin_lock(&mctrl_list_lock);
	list_del_init(list);
	spin_unlock(&mctrl_list_lock);
}

#define SYNDROME_MIN	-1
#define SYNDROME_MAX	144

/* Covert syndrome code into the way the bits are positioned
 * on the bus.
 */
static int syndrome_to_qword_code(int syndrome_code)
{
	if (syndrome_code < 128)
		syndrome_code += 16;
	else if (syndrome_code < 128 + 9)
		syndrome_code -= (128 - 7);
	else if (syndrome_code < (128 + 9 + 3))
		syndrome_code -= (128 + 9 - 4);
	else
		syndrome_code -= (128 + 9 + 3);
	return syndrome_code;
}

/* All this magic has to do with how a cache line comes over the wire
 * on Safari and JBUS.  A 64-bit line comes over in 1 or more quadword
 * cycles, each of which transmit ECC/MTAG info as well as the actual
 * data.
 */
#define L2_LINE_SIZE		64
#define L2_LINE_ADDR_MSK	(L2_LINE_SIZE - 1)
#define QW_PER_LINE		4
#define QW_BYTES		(L2_LINE_SIZE / QW_PER_LINE)
#define QW_BITS			144
#define SAFARI_LAST_BIT		(576 - 1)
#define JBUS_LAST_BIT		(144 - 1)

static void get_pin_and_dimm_str(int syndrome_code, unsigned long paddr,
				 int *pin_p, char **dimm_str_p, void *_prop,
				 int base_dimm_offset)
{
	int qword_code = syndrome_to_qword_code(syndrome_code);
	int cache_line_offset;
	int offset_inverse;
	int dimm_map_index;
	int map_val;

	if (mc_type == MC_TYPE_JBUS) {
		struct jbusmc_obp_mem_layout *p = _prop;

		/* JBUS */
		cache_line_offset = qword_code;
		offset_inverse = (JBUS_LAST_BIT - cache_line_offset);
		dimm_map_index = offset_inverse / 8;
		map_val = p->map.dimm_map[dimm_map_index];
		map_val = ((map_val >> ((7 - (offset_inverse & 7)))) & 1);
		*dimm_str_p = p->dimm_labels[base_dimm_offset + map_val];
		*pin_p = p->map.pin_map[cache_line_offset];
	} else {
		struct chmc_obp_mem_layout *p = _prop;
		struct chmc_obp_map *mp;
		int qword;

		/* Safari */
		if (p->symmetric)
			mp = &p->map[0];
		else
			mp = &p->map[1];

		qword = (paddr & L2_LINE_ADDR_MSK) / QW_BYTES;
		cache_line_offset = ((3 - qword) * QW_BITS) + qword_code;
		offset_inverse = (SAFARI_LAST_BIT - cache_line_offset);
		dimm_map_index = offset_inverse >> 2;
		map_val = mp->dimm_map[dimm_map_index];
		map_val = ((map_val >> ((3 - (offset_inverse & 3)) << 1)) & 0x3);
		*dimm_str_p = p->dimm_labels[base_dimm_offset + map_val];
		*pin_p = mp->pin_map[cache_line_offset];
	}
}

static struct jbusmc_dimm_group *jbusmc_find_dimm_group(unsigned long phys_addr)
{
	struct jbusmc *p;

	list_for_each_entry(p, &mctrl_list, list) {
		int i;

		for (i = 0; i < p->num_dimm_groups; i++) {
			struct jbusmc_dimm_group *dp = &p->dimm_groups[i];

			if (phys_addr < dp->base_addr ||
			    (dp->base_addr + dp->size) <= phys_addr)
				continue;

			return dp;
		}
	}
	return NULL;
}

static int jbusmc_print_dimm(int syndrome_code,
			     unsigned long phys_addr,
			     char *buf, int buflen)
{
	struct jbusmc_obp_mem_layout *prop;
	struct jbusmc_dimm_group *dp;
	struct jbusmc *p;
	int first_dimm;

	dp = jbusmc_find_dimm_group(phys_addr);
	if (dp == NULL ||
	    syndrome_code < SYNDROME_MIN ||
	    syndrome_code > SYNDROME_MAX) {
		buf[0] = '?';
		buf[1] = '?';
		buf[2] = '?';
		buf[3] = '\0';
		return 0;
	}
	p = dp->controller;
	prop = &p->layout;

	first_dimm = dp->index * JB_NUM_DIMMS_PER_GROUP;

	if (syndrome_code != SYNDROME_MIN) {
		char *dimm_str;
		int pin;

		get_pin_and_dimm_str(syndrome_code, phys_addr, &pin,
				     &dimm_str, prop, first_dimm);
		sprintf(buf, "%s, pin %3d", dimm_str, pin);
	} else {
		int dimm;

		/* Multi-bit error, we just dump out all the
		 * dimm labels associated with this dimm group.
		 */
		for (dimm = 0; dimm < JB_NUM_DIMMS_PER_GROUP; dimm++) {
			sprintf(buf, "%s ",
				prop->dimm_labels[first_dimm + dimm]);
			buf += strlen(buf);
		}
	}

	return 0;
}

static u64 jbusmc_dimm_group_size(u64 base,
				  const struct linux_prom64_registers *mem_regs,
				  int num_mem_regs)
{
	u64 max = base + (8UL * 1024 * 1024 * 1024);
	u64 max_seen = base;
	int i;

	for (i = 0; i < num_mem_regs; i++) {
		const struct linux_prom64_registers *ent;
		u64 this_base;
		u64 this_end;

		ent = &mem_regs[i];
		this_base = ent->phys_addr;
		this_end = this_base + ent->reg_size;
		if (base < this_base || base >= this_end)
			continue;
		if (this_end > max)
			this_end = max;
		if (this_end > max_seen)
			max_seen = this_end;
	}

	return max_seen - base;
}

static void jbusmc_construct_one_dimm_group(struct jbusmc *p,
					    unsigned long index,
					    const struct linux_prom64_registers *mem_regs,
					    int num_mem_regs)
{
	struct jbusmc_dimm_group *dp = &p->dimm_groups[index];

	dp->controller = p;
	dp->index = index;

	dp->base_addr  = (p->portid * (64UL * 1024 * 1024 * 1024));
	dp->base_addr += (index * (8UL * 1024 * 1024 * 1024));
	dp->size = jbusmc_dimm_group_size(dp->base_addr, mem_regs, num_mem_regs);
}

static void jbusmc_construct_dimm_groups(struct jbusmc *p,
					 const struct linux_prom64_registers *mem_regs,
					 int num_mem_regs)
{
	if (p->mc_reg_1 & JB_MC_REG1_DIMM1_BANK0) {
		jbusmc_construct_one_dimm_group(p, 0, mem_regs, num_mem_regs);
		p->num_dimm_groups++;
	}
	if (p->mc_reg_1 & JB_MC_REG1_DIMM2_BANK2) {
		jbusmc_construct_one_dimm_group(p, 1, mem_regs, num_mem_regs);
		p->num_dimm_groups++;
	}
}

static int jbusmc_probe(struct platform_device *op)
{
	const struct linux_prom64_registers *mem_regs;
	struct device_node *mem_node;
	int err, len, num_mem_regs;
	struct jbusmc *p;
	const u32 *prop;
	const void *ml;

	err = -ENODEV;
	mem_node = of_find_node_by_path("/memory");
	if (!mem_node) {
		printk(KERN_ERR PFX "Cannot find /memory node.\n");
		goto out;
	}
	mem_regs = of_get_property(mem_node, "reg", &len);
	if (!mem_regs) {
		printk(KERN_ERR PFX "Cannot get reg property of /memory node.\n");
		goto out;
	}
	num_mem_regs = len / sizeof(*mem_regs);

	err = -ENOMEM;
	p = kzalloc(sizeof(*p), GFP_KERNEL);
	if (!p) {
		printk(KERN_ERR PFX "Cannot allocate struct jbusmc.\n");
		goto out;
	}

	INIT_LIST_HEAD(&p->list);

	err = -ENODEV;
	prop = of_get_property(op->dev.of_node, "portid", &len);
	if (!prop || len != 4) {
		printk(KERN_ERR PFX "Cannot find portid.\n");
		goto out_free;
	}

	p->portid = *prop;

	prop = of_get_property(op->dev.of_node, "memory-control-register-1", &len);
	if (!prop || len != 8) {
		printk(KERN_ERR PFX "Cannot get memory control register 1.\n");
		goto out_free;
	}

	p->mc_reg_1 = ((u64)prop[0] << 32) | (u64) prop[1];

	err = -ENOMEM;
	p->regs = of_ioremap(&op->resource[0], 0, JBUSMC_REGS_SIZE, "jbusmc");
	if (!p->regs) {
		printk(KERN_ERR PFX "Cannot map jbusmc regs.\n");
		goto out_free;
	}

	err = -ENODEV;
	ml = of_get_property(op->dev.of_node, "memory-layout", &p->layout_len);
	if (!ml) {
		printk(KERN_ERR PFX "Cannot get memory layout property.\n");
		goto out_iounmap;
	}
	if (p->layout_len > sizeof(p->layout)) {
		printk(KERN_ERR PFX "Unexpected memory-layout size %d\n",
		       p->layout_len);
		goto out_iounmap;
	}
	memcpy(&p->layout, ml, p->layout_len);

	jbusmc_construct_dimm_groups(p, mem_regs, num_mem_regs);

	mc_list_add(&p->list);

	printk(KERN_INFO PFX "UltraSPARC-IIIi memory controller at %pOF\n",
	       op->dev.of_node);

	dev_set_drvdata(&op->dev, p);

	err = 0;

out:
	return err;

out_iounmap:
	of_iounmap(&op->resource[0], p->regs, JBUSMC_REGS_SIZE);

out_free:
	kfree(p);
	goto out;
}

/* Does BANK decode PHYS_ADDR? */
static int chmc_bank_match(struct chmc_bank_info *bp, unsigned long phys_addr)
{
	unsigned long upper_bits = (phys_addr & PA_UPPER_BITS) >> PA_UPPER_BITS_SHIFT;
	unsigned long lower_bits = (phys_addr & PA_LOWER_BITS) >> PA_LOWER_BITS_SHIFT;

	/* Bank must be enabled to match. */
	if (bp->valid == 0)
		return 0;

	/* Would BANK match upper bits? */
	upper_bits ^= bp->um;		/* What bits are different? */
	upper_bits  = ~upper_bits;	/* Invert. */
	upper_bits |= bp->uk;		/* What bits don't matter for matching? */
	upper_bits  = ~upper_bits;	/* Invert. */

	if (upper_bits)
		return 0;

	/* Would BANK match lower bits? */
	lower_bits ^= bp->lm;		/* What bits are different? */
	lower_bits  = ~lower_bits;	/* Invert. */
	lower_bits |= bp->lk;		/* What bits don't matter for matching? */
	lower_bits  = ~lower_bits;	/* Invert. */

	if (lower_bits)
		return 0;

	/* I always knew you'd be the one. */
	return 1;
}

/* Given PHYS_ADDR, search memory controller banks for a match. */
static struct chmc_bank_info *chmc_find_bank(unsigned long phys_addr)
{
	struct chmc *p;

	list_for_each_entry(p, &mctrl_list, list) {
		int bank_no;

		for (bank_no = 0; bank_no < CHMCTRL_NBANKS; bank_no++) {
			struct chmc_bank_info *bp;

			bp = &p->logical_banks[bank_no];
			if (chmc_bank_match(bp, phys_addr))
				return bp;
		}
	}

	return NULL;
}

/* This is the main purpose of this driver. */
static int chmc_print_dimm(int syndrome_code,
			   unsigned long phys_addr,
			   char *buf, int buflen)
{
	struct chmc_bank_info *bp;
	struct chmc_obp_mem_layout *prop;
	int bank_in_controller, first_dimm;

	bp = chmc_find_bank(phys_addr);
	if (bp == NULL ||
	    syndrome_code < SYNDROME_MIN ||
	    syndrome_code > SYNDROME_MAX) {
		buf[0] = '?';
		buf[1] = '?';
		buf[2] = '?';
		buf[3] = '\0';
		return 0;
	}

	prop = &bp->p->layout_prop;
	bank_in_controller = bp->bank_id & (CHMCTRL_NBANKS - 1);
	first_dimm  = (bank_in_controller & (CHMCTRL_NDGRPS - 1));
	first_dimm *= CHMCTRL_NDIMMS;

	if (syndrome_code != SYNDROME_MIN) {
		char *dimm_str;
		int pin;

		get_pin_and_dimm_str(syndrome_code, phys_addr, &pin,
				     &dimm_str, prop, first_dimm);
		sprintf(buf, "%s, pin %3d", dimm_str, pin);
	} else {
		int dimm;

		/* Multi-bit error, we just dump out all the
		 * dimm labels associated with this bank.
		 */
		for (dimm = 0; dimm < CHMCTRL_NDIMMS; dimm++) {
			sprintf(buf, "%s ",
				prop->dimm_labels[first_dimm + dimm]);
			buf += strlen(buf);
		}
	}
	return 0;
}

/* Accessing the registers is slightly complicated.  If you want
 * to get at the memory controller which is on the same processor
 * the code is executing, you must use special ASI load/store else
 * you go through the global mapping.
 */
static u64 chmc_read_mcreg(struct chmc *p, unsigned long offset)
{
	unsigned long ret, this_cpu;

	preempt_disable();

	this_cpu = real_hard_smp_processor_id();

	if (p->portid == this_cpu) {
		__asm__ __volatile__("ldxa	[%1] %2, %0"
				     : "=r" (ret)
				     : "r" (offset), "i" (ASI_MCU_CTRL_REG));
	} else {
		__asm__ __volatile__("ldxa	[%1] %2, %0"
				     : "=r" (ret)
				     : "r" (p->regs + offset),
				       "i" (ASI_PHYS_BYPASS_EC_E));
	}

	preempt_enable();

	return ret;
}

#if 0 /* currently unused */
static void chmc_write_mcreg(struct chmc *p, unsigned long offset, u64 val)
{
	if (p->portid == smp_processor_id()) {
		__asm__ __volatile__("stxa	%0, [%1] %2"
				     : : "r" (val),
				         "r" (offset), "i" (ASI_MCU_CTRL_REG));
	} else {
		__asm__ __volatile__("ldxa	%0, [%1] %2"
				     : : "r" (val),
				         "r" (p->regs + offset),
				         "i" (ASI_PHYS_BYPASS_EC_E));
	}
}
#endif

static void chmc_interpret_one_decode_reg(struct chmc *p, int which_bank, u64 val)
{
	struct chmc_bank_info *bp = &p->logical_banks[which_bank];

	bp->p = p;
	bp->bank_id = (CHMCTRL_NBANKS * p->portid) + which_bank;
	bp->raw_reg = val;
	bp->valid = (val & MEM_DECODE_VALID) >> MEM_DECODE_VALID_SHIFT;
	bp->uk = (val & MEM_DECODE_UK) >> MEM_DECODE_UK_SHIFT;
	bp->um = (val & MEM_DECODE_UM) >> MEM_DECODE_UM_SHIFT;
	bp->lk = (val & MEM_DECODE_LK) >> MEM_DECODE_LK_SHIFT;
	bp->lm = (val & MEM_DECODE_LM) >> MEM_DECODE_LM_SHIFT;

	bp->base  =  (bp->um);
	bp->base &= ~(bp->uk);
	bp->base <<= PA_UPPER_BITS_SHIFT;

	switch(bp->lk) {
	case 0xf:
	default:
		bp->interleave = 1;
		break;

	case 0xe:
		bp->interleave = 2;
		break;

	case 0xc:
		bp->interleave = 4;
		break;

	case 0x8:
		bp->interleave = 8;
		break;

	case 0x0:
		bp->interleave = 16;
		break;
	}

	/* UK[10] is reserved, and UK[11] is not set for the SDRAM
	 * bank size definition.
	 */
	bp->size = (((unsigned long)bp->uk &
		     ((1UL << 10UL) - 1UL)) + 1UL) << PA_UPPER_BITS_SHIFT;
	bp->size /= bp->interleave;
}

static void chmc_fetch_decode_regs(struct chmc *p)
{
	if (p->layout_size == 0)
		return;

	chmc_interpret_one_decode_reg(p, 0,
				      chmc_read_mcreg(p, CHMCTRL_DECODE1));
	chmc_interpret_one_decode_reg(p, 1,
				      chmc_read_mcreg(p, CHMCTRL_DECODE2));
	chmc_interpret_one_decode_reg(p, 2,
				      chmc_read_mcreg(p, CHMCTRL_DECODE3));
	chmc_interpret_one_decode_reg(p, 3,
				      chmc_read_mcreg(p, CHMCTRL_DECODE4));
}

static int chmc_probe(struct platform_device *op)
{
	struct device_node *dp = op->dev.of_node;
	unsigned long ver;
	const void *pval;
	int len, portid;
	struct chmc *p;
	int err;

	err = -ENODEV;
	__asm__ ("rdpr %%ver, %0" : "=r" (ver));
	if ((ver >> 32UL) == __JALAPENO_ID ||
	    (ver >> 32UL) == __SERRANO_ID)
		goto out;

	portid = of_getintprop_default(dp, "portid", -1);
	if (portid == -1)
		goto out;

	pval = of_get_property(dp, "memory-layout", &len);
	if (pval && len > sizeof(p->layout_prop)) {
		printk(KERN_ERR PFX "Unexpected memory-layout property "
		       "size %d.\n", len);
		goto out;
	}

	err = -ENOMEM;
	p = kzalloc(sizeof(*p), GFP_KERNEL);
	if (!p) {
		printk(KERN_ERR PFX "Could not allocate struct chmc.\n");
		goto out;
	}

	p->portid = portid;
	p->layout_size = len;
	if (!pval)
		p->layout_size = 0;
	else
		memcpy(&p->layout_prop, pval, len);

	p->regs = of_ioremap(&op->resource[0], 0, 0x48, "chmc");
	if (!p->regs) {
		printk(KERN_ERR PFX "Could not map registers.\n");
		goto out_free;
	}

	if (p->layout_size != 0UL) {
		p->timing_control1 = chmc_read_mcreg(p, CHMCTRL_TCTRL1);
		p->timing_control2 = chmc_read_mcreg(p, CHMCTRL_TCTRL2);
		p->timing_control3 = chmc_read_mcreg(p, CHMCTRL_TCTRL3);
		p->timing_control4 = chmc_read_mcreg(p, CHMCTRL_TCTRL4);
		p->memaddr_control = chmc_read_mcreg(p, CHMCTRL_MACTRL);
	}

	chmc_fetch_decode_regs(p);

	mc_list_add(&p->list);

	printk(KERN_INFO PFX "UltraSPARC-III memory controller at %pOF [%s]\n",
	       dp,
	       (p->layout_size ? "ACTIVE" : "INACTIVE"));

	dev_set_drvdata(&op->dev, p);

	err = 0;

out:
	return err;

out_free:
	kfree(p);
	goto out;
}

static int us3mc_probe(struct platform_device *op)
{
	if (mc_type == MC_TYPE_SAFARI)
		return chmc_probe(op);
	else if (mc_type == MC_TYPE_JBUS)
		return jbusmc_probe(op);
	return -ENODEV;
}

static void chmc_destroy(struct platform_device *op, struct chmc *p)
{
	list_del(&p->list);
	of_iounmap(&op->resource[0], p->regs, 0x48);
	kfree(p);
}

static void jbusmc_destroy(struct platform_device *op, struct jbusmc *p)
{
	mc_list_del(&p->list);
	of_iounmap(&op->resource[0], p->regs, JBUSMC_REGS_SIZE);
	kfree(p);
}

static int us3mc_remove(struct platform_device *op)
{
	void *p = dev_get_drvdata(&op->dev);

	if (p) {
		if (mc_type == MC_TYPE_SAFARI)
			chmc_destroy(op, p);
		else if (mc_type == MC_TYPE_JBUS)
			jbusmc_destroy(op, p);
	}
	return 0;
}

static const struct of_device_id us3mc_match[] = {
	{
		.name = "memory-controller",
	},
	{},
};
MODULE_DEVICE_TABLE(of, us3mc_match);

static struct platform_driver us3mc_driver = {
	.driver = {
		.name = "us3mc",
		.of_match_table = us3mc_match,
	},
	.probe		= us3mc_probe,
	.remove		= us3mc_remove,
};

static inline bool us3mc_platform(void)
{
	if (tlb_type == cheetah || tlb_type == cheetah_plus)
		return true;
	return false;
}

static int __init us3mc_init(void)
{
	unsigned long ver;
	int ret;

	if (!us3mc_platform())
		return -ENODEV;

	__asm__ __volatile__("rdpr %%ver, %0" : "=r" (ver));
	if ((ver >> 32UL) == __JALAPENO_ID ||
	    (ver >> 32UL) == __SERRANO_ID) {
		mc_type = MC_TYPE_JBUS;
		us3mc_dimm_printer = jbusmc_print_dimm;
	} else {
		mc_type = MC_TYPE_SAFARI;
		us3mc_dimm_printer = chmc_print_dimm;
	}

	ret = register_dimm_printer(us3mc_dimm_printer);

	if (!ret) {
		ret = platform_driver_register(&us3mc_driver);
		if (ret)
			unregister_dimm_printer(us3mc_dimm_printer);
	}
	return ret;
}

static void __exit us3mc_cleanup(void)
{
	if (us3mc_platform()) {
		unregister_dimm_printer(us3mc_dimm_printer);
		platform_driver_unregister(&us3mc_driver);
	}
}

module_init(us3mc_init);
module_exit(us3mc_cleanup);
