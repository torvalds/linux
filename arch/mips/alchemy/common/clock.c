/*
 * Alchemy clocks.
 *
 * Exposes all configurable internal clock sources to the clk framework.
 *
 * We have:
 *  - Root source, usually 12MHz supplied by an external crystal
 *  - 3 PLLs which generate multiples of root rate [AUX, CPU, AUX2]
 *
 * Dividers:
 *  - 6 clock dividers with:
 *   * selectable source [one of the PLLs],
 *   * output divided between [2 .. 512 in steps of 2] (!Au1300)
 *     or [1 .. 256 in steps of 1] (Au1300),
 *   * can be enabled individually.
 *
 * - up to 6 "internal" (fixed) consumers which:
 *   * take either AUXPLL or one of the above 6 dividers as input,
 *   * divide this input by 1, 2, or 4 (and 3 on Au1300).
 *   * can be disabled separately.
 *
 * Misc clocks:
 * - sysbus clock: CPU core clock (CPUPLL) divided by 2, 3 or 4.
 *    depends on board design and should be set by bootloader, read-only.
 * - peripheral clock: half the rate of sysbus clock, source for a lot
 *    of peripheral blocks, read-only.
 * - memory clock: clk rate to main memory chips, depends on board
 *    design and is read-only,
 * - lrclk: the static bus clock signal for synchronous operation.
 *    depends on board design, must be set by bootloader,
 *    but may be required to correctly configure devices attached to
 *    the static bus. The Au1000/1500/1100 manuals call it LCLK, on
 *    later models it's called RCLK.
 */

#include <linux/init.h>
#include <linux/io.h>
#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <asm/mach-au1x00/au1000.h>

/* Base clock: 12MHz is the default in all databooks, and I haven't
 * found any board yet which uses a different rate.
 */
#define ALCHEMY_ROOTCLK_RATE	12000000

/*
 * the internal sources which can be driven by the PLLs and dividers.
 * Names taken from the databooks, refer to them for more information,
 * especially which ones are share a clock line.
 */
static const char * const alchemy_au1300_intclknames[] = {
	"lcd_intclk", "gpemgp_clk", "maempe_clk", "maebsa_clk",
	"EXTCLK0", "EXTCLK1"
};

static const char * const alchemy_au1200_intclknames[] = {
	"lcd_intclk", NULL, NULL, NULL, "EXTCLK0", "EXTCLK1"
};

static const char * const alchemy_au1550_intclknames[] = {
	"usb_clk", "psc0_intclk", "psc1_intclk", "pci_clko",
	"EXTCLK0", "EXTCLK1"
};

static const char * const alchemy_au1100_intclknames[] = {
	"usb_clk", "lcd_intclk", NULL, "i2s_clk", "EXTCLK0", "EXTCLK1"
};

static const char * const alchemy_au1500_intclknames[] = {
	NULL, "usbd_clk", "usbh_clk", "pci_clko", "EXTCLK0", "EXTCLK1"
};

static const char * const alchemy_au1000_intclknames[] = {
	"irda_clk", "usbd_clk", "usbh_clk", "i2s_clk", "EXTCLK0",
	"EXTCLK1"
};

/* aliases for a few on-chip sources which are either shared
 * or have gone through name changes.
 */
static struct clk_aliastable {
	char *alias;
	char *base;
	int cputype;
} alchemy_clk_aliases[] __initdata = {
	{ "usbh_clk", "usb_clk",    ALCHEMY_CPU_AU1100 },
	{ "usbd_clk", "usb_clk",    ALCHEMY_CPU_AU1100 },
	{ "irda_clk", "usb_clk",    ALCHEMY_CPU_AU1100 },
	{ "usbh_clk", "usb_clk",    ALCHEMY_CPU_AU1550 },
	{ "usbd_clk", "usb_clk",    ALCHEMY_CPU_AU1550 },
	{ "psc2_intclk", "usb_clk", ALCHEMY_CPU_AU1550 },
	{ "psc3_intclk", "EXTCLK0", ALCHEMY_CPU_AU1550 },
	{ "psc0_intclk", "EXTCLK0", ALCHEMY_CPU_AU1200 },
	{ "psc1_intclk", "EXTCLK1", ALCHEMY_CPU_AU1200 },
	{ "psc0_intclk", "EXTCLK0", ALCHEMY_CPU_AU1300 },
	{ "psc2_intclk", "EXTCLK0", ALCHEMY_CPU_AU1300 },
	{ "psc1_intclk", "EXTCLK1", ALCHEMY_CPU_AU1300 },
	{ "psc3_intclk", "EXTCLK1", ALCHEMY_CPU_AU1300 },

	{ NULL, NULL, 0 },
};

#define IOMEM(x)	((void __iomem *)(KSEG1ADDR(CPHYSADDR(x))))

/* access locks to SYS_FREQCTRL0/1 and SYS_CLKSRC registers */
static spinlock_t alchemy_clk_fg0_lock;
static spinlock_t alchemy_clk_fg1_lock;
static spinlock_t alchemy_clk_csrc_lock;

/* CPU Core clock *****************************************************/

static unsigned long alchemy_clk_cpu_recalc(struct clk_hw *hw,
					    unsigned long parent_rate)
{
	unsigned long t;

	/*
	 * On early Au1000, sys_cpupll was write-only. Since these
	 * silicon versions of Au1000 are not sold, we don't bend
	 * over backwards trying to determine the frequency.
	 */
	if (unlikely(au1xxx_cpu_has_pll_wo()))
		t = 396000000;
	else {
		t = alchemy_rdsys(AU1000_SYS_CPUPLL) & 0x7f;
		if (alchemy_get_cputype() < ALCHEMY_CPU_AU1300)
			t &= 0x3f;
		t *= parent_rate;
	}

	return t;
}

void __init alchemy_set_lpj(void)
{
	preset_lpj = alchemy_clk_cpu_recalc(NULL, ALCHEMY_ROOTCLK_RATE);
	preset_lpj /= 2 * HZ;
}

static struct clk_ops alchemy_clkops_cpu = {
	.recalc_rate	= alchemy_clk_cpu_recalc,
};

static struct clk __init *alchemy_clk_setup_cpu(const char *parent_name,
						int ctype)
{
	struct clk_init_data id;
	struct clk_hw *h;

	h = kzalloc(sizeof(*h), GFP_KERNEL);
	if (!h)
		return ERR_PTR(-ENOMEM);

	id.name = ALCHEMY_CPU_CLK;
	id.parent_names = &parent_name;
	id.num_parents = 1;
	id.flags = CLK_IS_BASIC;
	id.ops = &alchemy_clkops_cpu;
	h->init = &id;

	return clk_register(NULL, h);
}

/* AUXPLLs ************************************************************/

struct alchemy_auxpll_clk {
	struct clk_hw hw;
	unsigned long reg;	/* au1300 has also AUXPLL2 */
	int maxmult;		/* max multiplier */
};
#define to_auxpll_clk(x) container_of(x, struct alchemy_auxpll_clk, hw)

static unsigned long alchemy_clk_aux_recalc(struct clk_hw *hw,
					    unsigned long parent_rate)
{
	struct alchemy_auxpll_clk *a = to_auxpll_clk(hw);

	return (alchemy_rdsys(a->reg) & 0xff) * parent_rate;
}

static int alchemy_clk_aux_setr(struct clk_hw *hw,
				unsigned long rate,
				unsigned long parent_rate)
{
	struct alchemy_auxpll_clk *a = to_auxpll_clk(hw);
	unsigned long d = rate;

	if (rate)
		d /= parent_rate;
	else
		d = 0;

	/* minimum is 84MHz, max is 756-1032 depending on variant */
	if (((d < 7) && (d != 0)) || (d > a->maxmult))
		return -EINVAL;

	alchemy_wrsys(d, a->reg);
	return 0;
}

static long alchemy_clk_aux_roundr(struct clk_hw *hw,
					    unsigned long rate,
					    unsigned long *parent_rate)
{
	struct alchemy_auxpll_clk *a = to_auxpll_clk(hw);
	unsigned long mult;

	if (!rate || !*parent_rate)
		return 0;

	mult = rate / (*parent_rate);

	if (mult && (mult < 7))
		mult = 7;
	if (mult > a->maxmult)
		mult = a->maxmult;

	return (*parent_rate) * mult;
}

static struct clk_ops alchemy_clkops_aux = {
	.recalc_rate	= alchemy_clk_aux_recalc,
	.set_rate	= alchemy_clk_aux_setr,
	.round_rate	= alchemy_clk_aux_roundr,
};

static struct clk __init *alchemy_clk_setup_aux(const char *parent_name,
						char *name, int maxmult,
						unsigned long reg)
{
	struct clk_init_data id;
	struct clk *c;
	struct alchemy_auxpll_clk *a;

	a = kzalloc(sizeof(*a), GFP_KERNEL);
	if (!a)
		return ERR_PTR(-ENOMEM);

	id.name = name;
	id.parent_names = &parent_name;
	id.num_parents = 1;
	id.flags = CLK_GET_RATE_NOCACHE;
	id.ops = &alchemy_clkops_aux;

	a->reg = reg;
	a->maxmult = maxmult;
	a->hw.init = &id;

	c = clk_register(NULL, &a->hw);
	if (!IS_ERR(c))
		clk_register_clkdev(c, name, NULL);
	else
		kfree(a);

	return c;
}

/* sysbus_clk *********************************************************/

static struct clk __init  *alchemy_clk_setup_sysbus(const char *pn)
{
	unsigned long v = (alchemy_rdsys(AU1000_SYS_POWERCTRL) & 3) + 2;
	struct clk *c;

	c = clk_register_fixed_factor(NULL, ALCHEMY_SYSBUS_CLK,
				      pn, 0, 1, v);
	if (!IS_ERR(c))
		clk_register_clkdev(c, ALCHEMY_SYSBUS_CLK, NULL);
	return c;
}

/* Peripheral Clock ***************************************************/

static struct clk __init *alchemy_clk_setup_periph(const char *pn)
{
	/* Peripheral clock runs at half the rate of sysbus clk */
	struct clk *c;

	c = clk_register_fixed_factor(NULL, ALCHEMY_PERIPH_CLK,
				      pn, 0, 1, 2);
	if (!IS_ERR(c))
		clk_register_clkdev(c, ALCHEMY_PERIPH_CLK, NULL);
	return c;
}

/* mem clock **********************************************************/

static struct clk __init *alchemy_clk_setup_mem(const char *pn, int ct)
{
	void __iomem *addr = IOMEM(AU1000_MEM_PHYS_ADDR);
	unsigned long v;
	struct clk *c;
	int div;

	switch (ct) {
	case ALCHEMY_CPU_AU1550:
	case ALCHEMY_CPU_AU1200:
		v = __raw_readl(addr + AU1550_MEM_SDCONFIGB);
		div = (v & (1 << 15)) ? 1 : 2;
		break;
	case ALCHEMY_CPU_AU1300:
		v = __raw_readl(addr + AU1550_MEM_SDCONFIGB);
		div = (v & (1 << 31)) ? 1 : 2;
		break;
	case ALCHEMY_CPU_AU1000:
	case ALCHEMY_CPU_AU1500:
	case ALCHEMY_CPU_AU1100:
	default:
		div = 2;
		break;
	}

	c = clk_register_fixed_factor(NULL, ALCHEMY_MEM_CLK, pn,
				      0, 1, div);
	if (!IS_ERR(c))
		clk_register_clkdev(c, ALCHEMY_MEM_CLK, NULL);
	return c;
}

/* lrclk: external synchronous static bus clock ***********************/

static struct clk __init *alchemy_clk_setup_lrclk(const char *pn, int t)
{
	/* Au1000, Au1500: MEM_STCFG0[11]: If bit is set, lrclk=pclk/5,
	 * otherwise lrclk=pclk/4.
	 * All other variants: MEM_STCFG0[15:13] = divisor.
	 * L/RCLK = periph_clk / (divisor + 1)
	 * On Au1000, Au1500, Au1100 it's called LCLK,
	 * on later models it's called RCLK, but it's the same thing.
	 */
	struct clk *c;
	unsigned long v = alchemy_rdsmem(AU1000_MEM_STCFG0);

	switch (t) {
	case ALCHEMY_CPU_AU1000:
	case ALCHEMY_CPU_AU1500:
		v = 4 + ((v >> 11) & 1);
		break;
	default:	/* all other models */
		v = ((v >> 13) & 7) + 1;
	}
	c = clk_register_fixed_factor(NULL, ALCHEMY_LR_CLK,
				      pn, 0, 1, v);
	if (!IS_ERR(c))
		clk_register_clkdev(c, ALCHEMY_LR_CLK, NULL);
	return c;
}

/* Clock dividers and muxes *******************************************/

/* data for fgen and csrc mux-dividers */
struct alchemy_fgcs_clk {
	struct clk_hw hw;
	spinlock_t *reglock;	/* register lock		  */
	unsigned long reg;	/* SYS_FREQCTRL0/1		  */
	int shift;		/* offset in register		  */
	int parent;		/* parent before disable [Au1300] */
	int isen;		/* is it enabled?		  */
	int *dt;		/* dividertable for csrc	  */
};
#define to_fgcs_clk(x) container_of(x, struct alchemy_fgcs_clk, hw)

static long alchemy_calc_div(unsigned long rate, unsigned long prate,
			       int scale, int maxdiv, unsigned long *rv)
{
	long div1, div2;

	div1 = prate / rate;
	if ((prate / div1) > rate)
		div1++;

	if (scale == 2) {	/* only div-by-multiple-of-2 possible */
		if (div1 & 1)
			div1++;	/* stay <=prate */
	}

	div2 = (div1 / scale) - 1;	/* value to write to register */

	if (div2 > maxdiv)
		div2 = maxdiv;
	if (rv)
		*rv = div2;

	div1 = ((div2 + 1) * scale);
	return div1;
}

static int alchemy_clk_fgcs_detr(struct clk_hw *hw,
				 struct clk_rate_request *req,
				 int scale, int maxdiv)
{
	struct clk *pc, *bpc, *free;
	long tdv, tpr, pr, nr, br, bpr, diff, lastdiff;
	int j;

	lastdiff = INT_MAX;
	bpr = 0;
	bpc = NULL;
	br = -EINVAL;
	free = NULL;

	/* look at the rates each enabled parent supplies and select
	 * the one that gets closest to but not over the requested rate.
	 */
	for (j = 0; j < 7; j++) {
		pc = clk_get_parent_by_index(hw->clk, j);
		if (!pc)
			break;

		/* if this parent is currently unused, remember it.
		 * XXX: we would actually want clk_has_active_children()
		 * but this is a good-enough approximation for now.
		 */
		if (!__clk_is_prepared(pc)) {
			if (!free)
				free = pc;
		}

		pr = clk_get_rate(pc);
		if (pr < req->rate)
			continue;

		/* what can hardware actually provide */
		tdv = alchemy_calc_div(req->rate, pr, scale, maxdiv, NULL);
		nr = pr / tdv;
		diff = req->rate - nr;
		if (nr > req->rate)
			continue;

		if (diff < lastdiff) {
			lastdiff = diff;
			bpr = pr;
			bpc = pc;
			br = nr;
		}
		if (diff == 0)
			break;
	}

	/* if we couldn't get the exact rate we wanted from the enabled
	 * parents, maybe we can tell an available disabled/inactive one
	 * to give us a rate we can divide down to the requested rate.
	 */
	if (lastdiff && free) {
		for (j = (maxdiv == 4) ? 1 : scale; j <= maxdiv; j += scale) {
			tpr = req->rate * j;
			if (tpr < 0)
				break;
			pr = clk_round_rate(free, tpr);

			tdv = alchemy_calc_div(req->rate, pr, scale, maxdiv,
					       NULL);
			nr = pr / tdv;
			diff = req->rate - nr;
			if (nr > req->rate)
				continue;
			if (diff < lastdiff) {
				lastdiff = diff;
				bpr = pr;
				bpc = free;
				br = nr;
			}
			if (diff == 0)
				break;
		}
	}

	if (br < 0)
		return br;

	req->best_parent_rate = bpr;
	req->best_parent_hw = __clk_get_hw(bpc);
	req->rate = br;

	return 0;
}

static int alchemy_clk_fgv1_en(struct clk_hw *hw)
{
	struct alchemy_fgcs_clk *c = to_fgcs_clk(hw);
	unsigned long v, flags;

	spin_lock_irqsave(c->reglock, flags);
	v = alchemy_rdsys(c->reg);
	v |= (1 << 1) << c->shift;
	alchemy_wrsys(v, c->reg);
	spin_unlock_irqrestore(c->reglock, flags);

	return 0;
}

static int alchemy_clk_fgv1_isen(struct clk_hw *hw)
{
	struct alchemy_fgcs_clk *c = to_fgcs_clk(hw);
	unsigned long v = alchemy_rdsys(c->reg) >> (c->shift + 1);

	return v & 1;
}

static void alchemy_clk_fgv1_dis(struct clk_hw *hw)
{
	struct alchemy_fgcs_clk *c = to_fgcs_clk(hw);
	unsigned long v, flags;

	spin_lock_irqsave(c->reglock, flags);
	v = alchemy_rdsys(c->reg);
	v &= ~((1 << 1) << c->shift);
	alchemy_wrsys(v, c->reg);
	spin_unlock_irqrestore(c->reglock, flags);
}

static int alchemy_clk_fgv1_setp(struct clk_hw *hw, u8 index)
{
	struct alchemy_fgcs_clk *c = to_fgcs_clk(hw);
	unsigned long v, flags;

	spin_lock_irqsave(c->reglock, flags);
	v = alchemy_rdsys(c->reg);
	if (index)
		v |= (1 << c->shift);
	else
		v &= ~(1 << c->shift);
	alchemy_wrsys(v, c->reg);
	spin_unlock_irqrestore(c->reglock, flags);

	return 0;
}

static u8 alchemy_clk_fgv1_getp(struct clk_hw *hw)
{
	struct alchemy_fgcs_clk *c = to_fgcs_clk(hw);

	return (alchemy_rdsys(c->reg) >> c->shift) & 1;
}

static int alchemy_clk_fgv1_setr(struct clk_hw *hw, unsigned long rate,
				 unsigned long parent_rate)
{
	struct alchemy_fgcs_clk *c = to_fgcs_clk(hw);
	unsigned long div, v, flags, ret;
	int sh = c->shift + 2;

	if (!rate || !parent_rate || rate > (parent_rate / 2))
		return -EINVAL;
	ret = alchemy_calc_div(rate, parent_rate, 2, 512, &div);
	spin_lock_irqsave(c->reglock, flags);
	v = alchemy_rdsys(c->reg);
	v &= ~(0xff << sh);
	v |= div << sh;
	alchemy_wrsys(v, c->reg);
	spin_unlock_irqrestore(c->reglock, flags);

	return 0;
}

static unsigned long alchemy_clk_fgv1_recalc(struct clk_hw *hw,
					     unsigned long parent_rate)
{
	struct alchemy_fgcs_clk *c = to_fgcs_clk(hw);
	unsigned long v = alchemy_rdsys(c->reg) >> (c->shift + 2);

	v = ((v & 0xff) + 1) * 2;
	return parent_rate / v;
}

static int alchemy_clk_fgv1_detr(struct clk_hw *hw,
				 struct clk_rate_request *req)
{
	return alchemy_clk_fgcs_detr(hw, req, 2, 512);
}

/* Au1000, Au1100, Au15x0, Au12x0 */
static struct clk_ops alchemy_clkops_fgenv1 = {
	.recalc_rate	= alchemy_clk_fgv1_recalc,
	.determine_rate	= alchemy_clk_fgv1_detr,
	.set_rate	= alchemy_clk_fgv1_setr,
	.set_parent	= alchemy_clk_fgv1_setp,
	.get_parent	= alchemy_clk_fgv1_getp,
	.enable		= alchemy_clk_fgv1_en,
	.disable	= alchemy_clk_fgv1_dis,
	.is_enabled	= alchemy_clk_fgv1_isen,
};

static void __alchemy_clk_fgv2_en(struct alchemy_fgcs_clk *c)
{
	unsigned long v = alchemy_rdsys(c->reg);

	v &= ~(3 << c->shift);
	v |= (c->parent & 3) << c->shift;
	alchemy_wrsys(v, c->reg);
	c->isen = 1;
}

static int alchemy_clk_fgv2_en(struct clk_hw *hw)
{
	struct alchemy_fgcs_clk *c = to_fgcs_clk(hw);
	unsigned long flags;

	/* enable by setting the previous parent clock */
	spin_lock_irqsave(c->reglock, flags);
	__alchemy_clk_fgv2_en(c);
	spin_unlock_irqrestore(c->reglock, flags);

	return 0;
}

static int alchemy_clk_fgv2_isen(struct clk_hw *hw)
{
	struct alchemy_fgcs_clk *c = to_fgcs_clk(hw);

	return ((alchemy_rdsys(c->reg) >> c->shift) & 3) != 0;
}

static void alchemy_clk_fgv2_dis(struct clk_hw *hw)
{
	struct alchemy_fgcs_clk *c = to_fgcs_clk(hw);
	unsigned long v, flags;

	spin_lock_irqsave(c->reglock, flags);
	v = alchemy_rdsys(c->reg);
	v &= ~(3 << c->shift);	/* set input mux to "disabled" state */
	alchemy_wrsys(v, c->reg);
	c->isen = 0;
	spin_unlock_irqrestore(c->reglock, flags);
}

static int alchemy_clk_fgv2_setp(struct clk_hw *hw, u8 index)
{
	struct alchemy_fgcs_clk *c = to_fgcs_clk(hw);
	unsigned long flags;

	spin_lock_irqsave(c->reglock, flags);
	c->parent = index + 1;	/* value to write to register */
	if (c->isen)
		__alchemy_clk_fgv2_en(c);
	spin_unlock_irqrestore(c->reglock, flags);

	return 0;
}

static u8 alchemy_clk_fgv2_getp(struct clk_hw *hw)
{
	struct alchemy_fgcs_clk *c = to_fgcs_clk(hw);
	unsigned long flags, v;

	spin_lock_irqsave(c->reglock, flags);
	v = c->parent - 1;
	spin_unlock_irqrestore(c->reglock, flags);
	return v;
}

/* fg0-2 and fg4-6 share a "scale"-bit. With this bit cleared, the
 * dividers behave exactly as on previous models (dividers are multiples
 * of 2); with the bit set, dividers are multiples of 1, halving their
 * range, but making them also much more flexible.
 */
static int alchemy_clk_fgv2_setr(struct clk_hw *hw, unsigned long rate,
				 unsigned long parent_rate)
{
	struct alchemy_fgcs_clk *c = to_fgcs_clk(hw);
	int sh = c->shift + 2;
	unsigned long div, v, flags, ret;

	if (!rate || !parent_rate || rate > parent_rate)
		return -EINVAL;

	v = alchemy_rdsys(c->reg) & (1 << 30); /* test "scale" bit */
	ret = alchemy_calc_div(rate, parent_rate, v ? 1 : 2,
			       v ? 256 : 512, &div);

	spin_lock_irqsave(c->reglock, flags);
	v = alchemy_rdsys(c->reg);
	v &= ~(0xff << sh);
	v |= (div & 0xff) << sh;
	alchemy_wrsys(v, c->reg);
	spin_unlock_irqrestore(c->reglock, flags);

	return 0;
}

static unsigned long alchemy_clk_fgv2_recalc(struct clk_hw *hw,
					     unsigned long parent_rate)
{
	struct alchemy_fgcs_clk *c = to_fgcs_clk(hw);
	int sh = c->shift + 2;
	unsigned long v, t;

	v = alchemy_rdsys(c->reg);
	t = parent_rate / (((v >> sh) & 0xff) + 1);
	if ((v & (1 << 30)) == 0)		/* test scale bit */
		t /= 2;

	return t;
}

static int alchemy_clk_fgv2_detr(struct clk_hw *hw,
				 struct clk_rate_request *req)
{
	struct alchemy_fgcs_clk *c = to_fgcs_clk(hw);
	int scale, maxdiv;

	if (alchemy_rdsys(c->reg) & (1 << 30)) {
		scale = 1;
		maxdiv = 256;
	} else {
		scale = 2;
		maxdiv = 512;
	}

	return alchemy_clk_fgcs_detr(hw, req, scale, maxdiv);
}

/* Au1300 larger input mux, no separate disable bit, flexible divider */
static struct clk_ops alchemy_clkops_fgenv2 = {
	.recalc_rate	= alchemy_clk_fgv2_recalc,
	.determine_rate	= alchemy_clk_fgv2_detr,
	.set_rate	= alchemy_clk_fgv2_setr,
	.set_parent	= alchemy_clk_fgv2_setp,
	.get_parent	= alchemy_clk_fgv2_getp,
	.enable		= alchemy_clk_fgv2_en,
	.disable	= alchemy_clk_fgv2_dis,
	.is_enabled	= alchemy_clk_fgv2_isen,
};

static const char * const alchemy_clk_fgv1_parents[] = {
	ALCHEMY_CPU_CLK, ALCHEMY_AUXPLL_CLK
};

static const char * const alchemy_clk_fgv2_parents[] = {
	ALCHEMY_AUXPLL2_CLK, ALCHEMY_CPU_CLK, ALCHEMY_AUXPLL_CLK
};

static const char * const alchemy_clk_fgen_names[] = {
	ALCHEMY_FG0_CLK, ALCHEMY_FG1_CLK, ALCHEMY_FG2_CLK,
	ALCHEMY_FG3_CLK, ALCHEMY_FG4_CLK, ALCHEMY_FG5_CLK };

static int __init alchemy_clk_init_fgens(int ctype)
{
	struct clk *c;
	struct clk_init_data id;
	struct alchemy_fgcs_clk *a;
	unsigned long v;
	int i, ret;

	switch (ctype) {
	case ALCHEMY_CPU_AU1000...ALCHEMY_CPU_AU1200:
		id.ops = &alchemy_clkops_fgenv1;
		id.parent_names = alchemy_clk_fgv1_parents;
		id.num_parents = 2;
		break;
	case ALCHEMY_CPU_AU1300:
		id.ops = &alchemy_clkops_fgenv2;
		id.parent_names = alchemy_clk_fgv2_parents;
		id.num_parents = 3;
		break;
	default:
		return -ENODEV;
	}
	id.flags = CLK_SET_RATE_PARENT | CLK_GET_RATE_NOCACHE;

	a = kzalloc((sizeof(*a)) * 6, GFP_KERNEL);
	if (!a)
		return -ENOMEM;

	spin_lock_init(&alchemy_clk_fg0_lock);
	spin_lock_init(&alchemy_clk_fg1_lock);
	ret = 0;
	for (i = 0; i < 6; i++) {
		id.name = alchemy_clk_fgen_names[i];
		a->shift = 10 * (i < 3 ? i : i - 3);
		if (i > 2) {
			a->reg = AU1000_SYS_FREQCTRL1;
			a->reglock = &alchemy_clk_fg1_lock;
		} else {
			a->reg = AU1000_SYS_FREQCTRL0;
			a->reglock = &alchemy_clk_fg0_lock;
		}

		/* default to first parent if bootloader has set
		 * the mux to disabled state.
		 */
		if (ctype == ALCHEMY_CPU_AU1300) {
			v = alchemy_rdsys(a->reg);
			a->parent = (v >> a->shift) & 3;
			if (!a->parent) {
				a->parent = 1;
				a->isen = 0;
			} else
				a->isen = 1;
		}

		a->hw.init = &id;
		c = clk_register(NULL, &a->hw);
		if (IS_ERR(c))
			ret++;
		else
			clk_register_clkdev(c, id.name, NULL);
		a++;
	}

	return ret;
}

/* internal sources muxes *********************************************/

static int alchemy_clk_csrc_isen(struct clk_hw *hw)
{
	struct alchemy_fgcs_clk *c = to_fgcs_clk(hw);
	unsigned long v = alchemy_rdsys(c->reg);

	return (((v >> c->shift) >> 2) & 7) != 0;
}

static void __alchemy_clk_csrc_en(struct alchemy_fgcs_clk *c)
{
	unsigned long v = alchemy_rdsys(c->reg);

	v &= ~((7 << 2) << c->shift);
	v |= ((c->parent & 7) << 2) << c->shift;
	alchemy_wrsys(v, c->reg);
	c->isen = 1;
}

static int alchemy_clk_csrc_en(struct clk_hw *hw)
{
	struct alchemy_fgcs_clk *c = to_fgcs_clk(hw);
	unsigned long flags;

	/* enable by setting the previous parent clock */
	spin_lock_irqsave(c->reglock, flags);
	__alchemy_clk_csrc_en(c);
	spin_unlock_irqrestore(c->reglock, flags);

	return 0;
}

static void alchemy_clk_csrc_dis(struct clk_hw *hw)
{
	struct alchemy_fgcs_clk *c = to_fgcs_clk(hw);
	unsigned long v, flags;

	spin_lock_irqsave(c->reglock, flags);
	v = alchemy_rdsys(c->reg);
	v &= ~((3 << 2) << c->shift);	/* mux to "disabled" state */
	alchemy_wrsys(v, c->reg);
	c->isen = 0;
	spin_unlock_irqrestore(c->reglock, flags);
}

static int alchemy_clk_csrc_setp(struct clk_hw *hw, u8 index)
{
	struct alchemy_fgcs_clk *c = to_fgcs_clk(hw);
	unsigned long flags;

	spin_lock_irqsave(c->reglock, flags);
	c->parent = index + 1;	/* value to write to register */
	if (c->isen)
		__alchemy_clk_csrc_en(c);
	spin_unlock_irqrestore(c->reglock, flags);

	return 0;
}

static u8 alchemy_clk_csrc_getp(struct clk_hw *hw)
{
	struct alchemy_fgcs_clk *c = to_fgcs_clk(hw);

	return c->parent - 1;
}

static unsigned long alchemy_clk_csrc_recalc(struct clk_hw *hw,
					     unsigned long parent_rate)
{
	struct alchemy_fgcs_clk *c = to_fgcs_clk(hw);
	unsigned long v = (alchemy_rdsys(c->reg) >> c->shift) & 3;

	return parent_rate / c->dt[v];
}

static int alchemy_clk_csrc_setr(struct clk_hw *hw, unsigned long rate,
				 unsigned long parent_rate)
{
	struct alchemy_fgcs_clk *c = to_fgcs_clk(hw);
	unsigned long d, v, flags;
	int i;

	if (!rate || !parent_rate || rate > parent_rate)
		return -EINVAL;

	d = (parent_rate + (rate / 2)) / rate;
	if (d > 4)
		return -EINVAL;
	if ((d == 3) && (c->dt[2] != 3))
		d = 4;

	for (i = 0; i < 4; i++)
		if (c->dt[i] == d)
			break;

	if (i >= 4)
		return -EINVAL;	/* oops */

	spin_lock_irqsave(c->reglock, flags);
	v = alchemy_rdsys(c->reg);
	v &= ~(3 << c->shift);
	v |= (i & 3) << c->shift;
	alchemy_wrsys(v, c->reg);
	spin_unlock_irqrestore(c->reglock, flags);

	return 0;
}

static int alchemy_clk_csrc_detr(struct clk_hw *hw,
				 struct clk_rate_request *req)
{
	struct alchemy_fgcs_clk *c = to_fgcs_clk(hw);
	int scale = c->dt[2] == 3 ? 1 : 2; /* au1300 check */

	return alchemy_clk_fgcs_detr(hw, req, scale, 4);
}

static struct clk_ops alchemy_clkops_csrc = {
	.recalc_rate	= alchemy_clk_csrc_recalc,
	.determine_rate	= alchemy_clk_csrc_detr,
	.set_rate	= alchemy_clk_csrc_setr,
	.set_parent	= alchemy_clk_csrc_setp,
	.get_parent	= alchemy_clk_csrc_getp,
	.enable		= alchemy_clk_csrc_en,
	.disable	= alchemy_clk_csrc_dis,
	.is_enabled	= alchemy_clk_csrc_isen,
};

static const char * const alchemy_clk_csrc_parents[] = {
	/* disabled at index 0 */ ALCHEMY_AUXPLL_CLK,
	ALCHEMY_FG0_CLK, ALCHEMY_FG1_CLK, ALCHEMY_FG2_CLK,
	ALCHEMY_FG3_CLK, ALCHEMY_FG4_CLK, ALCHEMY_FG5_CLK
};

/* divider tables */
static int alchemy_csrc_dt1[] = { 1, 4, 1, 2 };	/* rest */
static int alchemy_csrc_dt2[] = { 1, 4, 3, 2 };	/* Au1300 */

static int __init alchemy_clk_setup_imux(int ctype)
{
	struct alchemy_fgcs_clk *a;
	const char * const *names;
	struct clk_init_data id;
	unsigned long v;
	int i, ret, *dt;
	struct clk *c;

	id.ops = &alchemy_clkops_csrc;
	id.parent_names = alchemy_clk_csrc_parents;
	id.num_parents = 7;
	id.flags = CLK_SET_RATE_PARENT | CLK_GET_RATE_NOCACHE;

	dt = alchemy_csrc_dt1;
	switch (ctype) {
	case ALCHEMY_CPU_AU1000:
		names = alchemy_au1000_intclknames;
		break;
	case ALCHEMY_CPU_AU1500:
		names = alchemy_au1500_intclknames;
		break;
	case ALCHEMY_CPU_AU1100:
		names = alchemy_au1100_intclknames;
		break;
	case ALCHEMY_CPU_AU1550:
		names = alchemy_au1550_intclknames;
		break;
	case ALCHEMY_CPU_AU1200:
		names = alchemy_au1200_intclknames;
		break;
	case ALCHEMY_CPU_AU1300:
		dt = alchemy_csrc_dt2;
		names = alchemy_au1300_intclknames;
		break;
	default:
		return -ENODEV;
	}

	a = kzalloc((sizeof(*a)) * 6, GFP_KERNEL);
	if (!a)
		return -ENOMEM;

	spin_lock_init(&alchemy_clk_csrc_lock);
	ret = 0;

	for (i = 0; i < 6; i++) {
		id.name = names[i];
		if (!id.name)
			goto next;

		a->shift = i * 5;
		a->reg = AU1000_SYS_CLKSRC;
		a->reglock = &alchemy_clk_csrc_lock;
		a->dt = dt;

		/* default to first parent clock if mux is initially
		 * set to disabled state.
		 */
		v = alchemy_rdsys(a->reg);
		a->parent = ((v >> a->shift) >> 2) & 7;
		if (!a->parent) {
			a->parent = 1;
			a->isen = 0;
		} else
			a->isen = 1;

		a->hw.init = &id;
		c = clk_register(NULL, &a->hw);
		if (IS_ERR(c))
			ret++;
		else
			clk_register_clkdev(c, id.name, NULL);
next:
		a++;
	}

	return ret;
}


/**********************************************************************/


#define ERRCK(x)						\
	if (IS_ERR(x)) {					\
		ret = PTR_ERR(x);				\
		goto out;					\
	}

static int __init alchemy_clk_init(void)
{
	int ctype = alchemy_get_cputype(), ret, i;
	struct clk_aliastable *t = alchemy_clk_aliases;
	struct clk *c;

	/* Root of the Alchemy clock tree: external 12MHz crystal osc */
	c = clk_register_fixed_rate(NULL, ALCHEMY_ROOT_CLK, NULL,
					   CLK_IS_ROOT,
					   ALCHEMY_ROOTCLK_RATE);
	ERRCK(c)

	/* CPU core clock */
	c = alchemy_clk_setup_cpu(ALCHEMY_ROOT_CLK, ctype);
	ERRCK(c)

	/* AUXPLLs: max 1GHz on Au1300, 748MHz on older models */
	i = (ctype == ALCHEMY_CPU_AU1300) ? 84 : 63;
	c = alchemy_clk_setup_aux(ALCHEMY_ROOT_CLK, ALCHEMY_AUXPLL_CLK,
				  i, AU1000_SYS_AUXPLL);
	ERRCK(c)

	if (ctype == ALCHEMY_CPU_AU1300) {
		c = alchemy_clk_setup_aux(ALCHEMY_ROOT_CLK,
					  ALCHEMY_AUXPLL2_CLK, i,
					  AU1300_SYS_AUXPLL2);
		ERRCK(c)
	}

	/* sysbus clock: cpu core clock divided by 2, 3 or 4 */
	c = alchemy_clk_setup_sysbus(ALCHEMY_CPU_CLK);
	ERRCK(c)

	/* peripheral clock: runs at half rate of sysbus clk */
	c = alchemy_clk_setup_periph(ALCHEMY_SYSBUS_CLK);
	ERRCK(c)

	/* SDR/DDR memory clock */
	c = alchemy_clk_setup_mem(ALCHEMY_SYSBUS_CLK, ctype);
	ERRCK(c)

	/* L/RCLK: external static bus clock for synchronous mode */
	c = alchemy_clk_setup_lrclk(ALCHEMY_PERIPH_CLK, ctype);
	ERRCK(c)

	/* Frequency dividers 0-5 */
	ret = alchemy_clk_init_fgens(ctype);
	if (ret) {
		ret = -ENODEV;
		goto out;
	}

	/* diving muxes for internal sources */
	ret = alchemy_clk_setup_imux(ctype);
	if (ret) {
		ret = -ENODEV;
		goto out;
	}

	/* set up aliases drivers might look for */
	while (t->base) {
		if (t->cputype == ctype)
			clk_add_alias(t->alias, NULL, t->base, NULL);
		t++;
	}

	pr_info("Alchemy clocktree installed\n");
	return 0;

out:
	return ret;
}
postcore_initcall(alchemy_clk_init);
