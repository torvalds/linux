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
#include <linux/of_device.h>
#include <asm/spitfire.h>
#include <asm/chmctrl.h>
#include <asm/cpudata.h>
#include <asm/oplib.h>
#include <asm/prom.h>
#include <asm/head.h>
#include <asm/io.h>

#define DRV_MODULE_NAME		"chmc"
#define PFX DRV_MODULE_NAME	": "
#define DRV_MODULE_VERSION	"0.2"

MODULE_AUTHOR("David S. Miller (davem@davemloft.net)");
MODULE_DESCRIPTION("UltraSPARC-III memory controller driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_MODULE_VERSION);

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

static LIST_HEAD(mctrl_list);

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
	struct list_head *mctrl_head = &mctrl_list;
	struct list_head *mctrl_entry = mctrl_head->next;

	for (;;) {
		struct chmc *p = list_entry(mctrl_entry, struct chmc, list);
		int bank_no;

		if (mctrl_entry == mctrl_head)
			break;
		mctrl_entry = mctrl_entry->next;

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
#define SYNDROME_MIN	-1
#define SYNDROME_MAX	144
int chmc_getunumber(int syndrome_code,
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
		struct chmc_obp_map *map;
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
	};

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

static int __devinit chmc_probe(struct of_device *op,
				const struct of_device_id *match)
{
	struct device_node *dp = op->node;
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

	list_add(&p->list, &mctrl_list);

	/* Report the device. */
	printk(KERN_INFO PFX "UltraSPARC-III memory controller at %s [%s]\n",
	       dp->full_name,
	       (p->layout_size ? "ACTIVE" : "INACTIVE"));

	dev_set_drvdata(&op->dev, p);

	err = 0;

out:
	return err;

out_free:
	kfree(p);
	goto out;
}

static int __devexit chmc_remove(struct of_device *op)
{
	struct chmc *p = dev_get_drvdata(&op->dev);

	if (p) {
		list_del(&p->list);
		of_iounmap(&op->resource[0], p->regs, 0x48);
		kfree(p);
	}
	return 0;
}

static struct of_device_id chmc_match[] = {
	{
		.name = "memory-controller",
	},
	{},
};
MODULE_DEVICE_TABLE(of, chmc_match);

static struct of_platform_driver chmc_driver = {
	.name		= "chmc",
	.match_table	= chmc_match,
	.probe		= chmc_probe,
	.remove		= __devexit_p(chmc_remove),
};

static inline bool chmc_platform(void)
{
	if (tlb_type == cheetah || tlb_type == cheetah_plus)
		return true;
	return false;
}

static int __init chmc_init(void)
{
	if (!chmc_platform())
		return -ENODEV;

	return of_register_driver(&chmc_driver, &of_bus_type);
}

static void __exit chmc_cleanup(void)
{
	if (chmc_platform())
		of_unregister_driver(&chmc_driver);
}

module_init(chmc_init);
module_exit(chmc_cleanup);
