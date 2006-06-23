/* $Id: chmc.c,v 1.4 2002/01/08 16:00:14 davem Exp $
 * memctrlr.c: Driver for UltraSPARC-III memory controller.
 *
 * Copyright (C) 2001 David S. Miller (davem@redhat.com)
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
#include <asm/spitfire.h>
#include <asm/chmctrl.h>
#include <asm/oplib.h>
#include <asm/prom.h>
#include <asm/io.h>

#define CHMCTRL_NDGRPS	2
#define CHMCTRL_NDIMMS	4

#define DIMMS_PER_MC	(CHMCTRL_NDGRPS * CHMCTRL_NDIMMS)

/* OBP memory-layout property format. */
struct obp_map {
	unsigned char	dimm_map[144];
	unsigned char	pin_map[576];
};

#define DIMM_LABEL_SZ	8

struct obp_mem_layout {
	/* One max 8-byte string label per DIMM.  Usually
	 * this matches the label on the motherboard where
	 * that DIMM resides.
	 */
	char		dimm_labels[DIMMS_PER_MC][DIMM_LABEL_SZ];

	/* If symmetric use map[0], else it is
	 * asymmetric and map[1] should be used.
	 */
	char		symmetric;

	struct obp_map	map[2];
};

#define CHMCTRL_NBANKS	4

struct bank_info {
	struct mctrl_info	*mp;
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

struct mctrl_info {
	struct list_head	list;
	int			portid;

	struct obp_mem_layout	layout_prop;
	int			layout_size;

	void __iomem		*regs;

	u64			timing_control1;
	u64			timing_control2;
	u64			timing_control3;
	u64			timing_control4;
	u64			memaddr_control;

	struct bank_info	logical_banks[CHMCTRL_NBANKS];
};

static LIST_HEAD(mctrl_list);

/* Does BANK decode PHYS_ADDR? */
static int bank_match(struct bank_info *bp, unsigned long phys_addr)
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
static struct bank_info *find_bank(unsigned long phys_addr)
{
	struct list_head *mctrl_head = &mctrl_list;
	struct list_head *mctrl_entry = mctrl_head->next;

	for (;;) {
		struct mctrl_info *mp =
			list_entry(mctrl_entry, struct mctrl_info, list);
		int bank_no;

		if (mctrl_entry == mctrl_head)
			break;
		mctrl_entry = mctrl_entry->next;

		for (bank_no = 0; bank_no < CHMCTRL_NBANKS; bank_no++) {
			struct bank_info *bp;

			bp = &mp->logical_banks[bank_no];
			if (bank_match(bp, phys_addr))
				return bp;
		}
	}

	return NULL;
}

/* This is the main purpose of this driver. */
#define SYNDROME_MIN	-1
#define SYNDROME_MAX	144
int chmc_getunumber(int syndrome_code,
		    unsigned long phys_addr,
		    char *buf, int buflen)
{
	struct bank_info *bp;
	struct obp_mem_layout *prop;
	int bank_in_controller, first_dimm;

	bp = find_bank(phys_addr);
	if (bp == NULL ||
	    syndrome_code < SYNDROME_MIN ||
	    syndrome_code > SYNDROME_MAX) {
		buf[0] = '?';
		buf[1] = '?';
		buf[2] = '?';
		buf[3] = '\0';
		return 0;
	}

	prop = &bp->mp->layout_prop;
	bank_in_controller = bp->bank_id & (CHMCTRL_NBANKS - 1);
	first_dimm  = (bank_in_controller & (CHMCTRL_NDGRPS - 1));
	first_dimm *= CHMCTRL_NDIMMS;

	if (syndrome_code != SYNDROME_MIN) {
		struct obp_map *map;
		int qword, where_in_line, where, map_index, map_offset;
		unsigned int map_val;

		/* Yaay, single bit error so we can figure out
		 * the exact dimm.
		 */
		if (prop->symmetric)
			map = &prop->map[0];
		else
			map = &prop->map[1];

		/* Covert syndrome code into the way the bits are
		 * positioned on the bus.
		 */
		if (syndrome_code < 144 - 16)
			syndrome_code += 16;
		else if (syndrome_code < 144)
			syndrome_code -= (144 - 7);
		else if (syndrome_code < (144 + 3))
			syndrome_code -= (144 + 3 - 4);
		else
			syndrome_code -= 144 + 3;

		/* All this magic has to do with how a cache line
		 * comes over the wire on Safari.  A 64-bit line
		 * comes over in 4 quadword cycles, each of which
		 * transmit ECC/MTAG info as well as the actual
		 * data.  144 bits per quadword, 576 total.
		 */
#define LINE_SIZE	64
#define LINE_ADDR_MSK	(LINE_SIZE - 1)
#define QW_PER_LINE	4
#define QW_BYTES	(LINE_SIZE / QW_PER_LINE)
#define QW_BITS		144
#define LAST_BIT	(576 - 1)

		qword = (phys_addr & LINE_ADDR_MSK) / QW_BYTES;
		where_in_line = ((3 - qword) * QW_BITS) + syndrome_code;
		where = (LAST_BIT - where_in_line);
		map_index = where >> 2;
		map_offset = where & 0x3;
		map_val = map->dimm_map[map_index];
		map_val = ((map_val >> ((3 - map_offset) << 1)) & (2 - 1));

		sprintf(buf, "%s, pin %3d",
			prop->dimm_labels[first_dimm + map_val],
			map->pin_map[where_in_line]);
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
static u64 read_mcreg(struct mctrl_info *mp, unsigned long offset)
{
	unsigned long ret;
	int this_cpu = get_cpu();

	if (mp->portid == this_cpu) {
		__asm__ __volatile__("ldxa	[%1] %2, %0"
				     : "=r" (ret)
				     : "r" (offset), "i" (ASI_MCU_CTRL_REG));
	} else {
		__asm__ __volatile__("ldxa	[%1] %2, %0"
				     : "=r" (ret)
				     : "r" (mp->regs + offset),
				       "i" (ASI_PHYS_BYPASS_EC_E));
	}
	put_cpu();

	return ret;
}

#if 0 /* currently unused */
static void write_mcreg(struct mctrl_info *mp, unsigned long offset, u64 val)
{
	if (mp->portid == smp_processor_id()) {
		__asm__ __volatile__("stxa	%0, [%1] %2"
				     : : "r" (val),
				         "r" (offset), "i" (ASI_MCU_CTRL_REG));
	} else {
		__asm__ __volatile__("ldxa	%0, [%1] %2"
				     : : "r" (val),
				         "r" (mp->regs + offset),
				         "i" (ASI_PHYS_BYPASS_EC_E));
	}
}
#endif

static void interpret_one_decode_reg(struct mctrl_info *mp, int which_bank, u64 val)
{
	struct bank_info *p = &mp->logical_banks[which_bank];

	p->mp = mp;
	p->bank_id = (CHMCTRL_NBANKS * mp->portid) + which_bank;
	p->raw_reg = val;
	p->valid = (val & MEM_DECODE_VALID) >> MEM_DECODE_VALID_SHIFT;
	p->uk = (val & MEM_DECODE_UK) >> MEM_DECODE_UK_SHIFT;
	p->um = (val & MEM_DECODE_UM) >> MEM_DECODE_UM_SHIFT;
	p->lk = (val & MEM_DECODE_LK) >> MEM_DECODE_LK_SHIFT;
	p->lm = (val & MEM_DECODE_LM) >> MEM_DECODE_LM_SHIFT;

	p->base  =  (p->um);
	p->base &= ~(p->uk);
	p->base <<= PA_UPPER_BITS_SHIFT;

	switch(p->lk) {
	case 0xf:
	default:
		p->interleave = 1;
		break;

	case 0xe:
		p->interleave = 2;
		break;

	case 0xc:
		p->interleave = 4;
		break;

	case 0x8:
		p->interleave = 8;
		break;

	case 0x0:
		p->interleave = 16;
		break;
	};

	/* UK[10] is reserved, and UK[11] is not set for the SDRAM
	 * bank size definition.
	 */
	p->size = (((unsigned long)p->uk &
		    ((1UL << 10UL) - 1UL)) + 1UL) << PA_UPPER_BITS_SHIFT;
	p->size /= p->interleave;
}

static void fetch_decode_regs(struct mctrl_info *mp)
{
	if (mp->layout_size == 0)
		return;

	interpret_one_decode_reg(mp, 0,
				 read_mcreg(mp, CHMCTRL_DECODE1));
	interpret_one_decode_reg(mp, 1,
				 read_mcreg(mp, CHMCTRL_DECODE2));
	interpret_one_decode_reg(mp, 2,
				 read_mcreg(mp, CHMCTRL_DECODE3));
	interpret_one_decode_reg(mp, 3,
				 read_mcreg(mp, CHMCTRL_DECODE4));
}

static int init_one_mctrl(struct device_node *dp)
{
	struct mctrl_info *mp = kmalloc(sizeof(*mp), GFP_KERNEL);
	int portid = of_getintprop_default(dp, "portid", -1);
	struct linux_prom64_registers *regs;
	void *pval;
	int len;

	if (!mp)
		return -1;
	memset(mp, 0, sizeof(*mp));
	if (portid == -1)
		goto fail;

	mp->portid = portid;
	pval = of_get_property(dp, "memory-layout", &len);
	mp->layout_size = len;
	if (!pval)
		mp->layout_size = 0;
	else {
		if (mp->layout_size > sizeof(mp->layout_prop))
			goto fail;
		memcpy(&mp->layout_prop, pval, len);
	}

	regs = of_get_property(dp, "reg", NULL);
	if (!regs || regs->reg_size != 0x48)
		goto fail;

	mp->regs = ioremap(regs->phys_addr, regs->reg_size);
	if (mp->regs == NULL)
		goto fail;

	if (mp->layout_size != 0UL) {
		mp->timing_control1 = read_mcreg(mp, CHMCTRL_TCTRL1);
		mp->timing_control2 = read_mcreg(mp, CHMCTRL_TCTRL2);
		mp->timing_control3 = read_mcreg(mp, CHMCTRL_TCTRL3);
		mp->timing_control4 = read_mcreg(mp, CHMCTRL_TCTRL4);
		mp->memaddr_control = read_mcreg(mp, CHMCTRL_MACTRL);
	}

	fetch_decode_regs(mp);

	list_add(&mp->list, &mctrl_list);

	/* Report the device. */
	printk(KERN_INFO "%s: US3 memory controller at %p [%s]\n",
	       dp->full_name,
	       mp->regs, (mp->layout_size ? "ACTIVE" : "INACTIVE"));

	return 0;

fail:
	if (mp) {
		if (mp->regs != NULL)
			iounmap(mp->regs);
		kfree(mp);
	}
	return -1;
}

static int __init chmc_init(void)
{
	struct device_node *dp;

	/* This driver is only for cheetah platforms. */
	if (tlb_type != cheetah && tlb_type != cheetah_plus)
		return -ENODEV;

	for_each_node_by_name(dp, "memory-controller")
		init_one_mctrl(dp);

	for_each_node_by_name(dp, "mc-us3")
		init_one_mctrl(dp);

	return 0;
}

static void __exit chmc_cleanup(void)
{
	struct list_head *head = &mctrl_list;
	struct list_head *tmp = head->next;

	for (;;) {
		struct mctrl_info *p =
			list_entry(tmp, struct mctrl_info, list);
		if (tmp == head)
			break;
		tmp = tmp->next;

		list_del(&p->list);
		iounmap(p->regs);
		kfree(p);
	}
}

module_init(chmc_init);
module_exit(chmc_cleanup);
