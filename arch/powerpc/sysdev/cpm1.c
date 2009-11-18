/*
 * General Purpose functions for the global management of the
 * Communication Processor Module.
 * Copyright (c) 1997 Dan error_act (dmalek@jlc.net)
 *
 * In addition to the individual control of the communication
 * channels, there are a few functions that globally affect the
 * communication processor.
 *
 * Buffer descriptors must be allocated from the dual ported memory
 * space.  The allocator for that is here.  When the communication
 * process is reset, we reclaim the memory available.  There is
 * currently no deallocator for this memory.
 * The amount of space available is platform dependent.  On the
 * MBX, the EPPC software loads additional microcode into the
 * communication processor, and uses some of the DP ram for this
 * purpose.  Current, the first 512 bytes and the last 256 bytes of
 * memory are used.  Right now I am conservative and only use the
 * memory that can never be used for microcode.  If there are
 * applications that require more DP ram, we can expand the boundaries
 * but then we have to be careful of any downloaded microcode.
 */
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/dma-mapping.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/8xx_immap.h>
#include <asm/cpm1.h>
#include <asm/io.h>
#include <asm/tlbflush.h>
#include <asm/rheap.h>
#include <asm/prom.h>
#include <asm/cpm.h>

#include <asm/fs_pd.h>

#ifdef CONFIG_8xx_GPIO
#include <linux/of_gpio.h>
#endif

#define CPM_MAP_SIZE    (0x4000)

cpm8xx_t __iomem *cpmp;  /* Pointer to comm processor space */
immap_t __iomem *mpc8xx_immr;
static cpic8xx_t __iomem *cpic_reg;

static struct irq_host *cpm_pic_host;

static void cpm_mask_irq(unsigned int irq)
{
	unsigned int cpm_vec = (unsigned int)irq_map[irq].hwirq;

	clrbits32(&cpic_reg->cpic_cimr, (1 << cpm_vec));
}

static void cpm_unmask_irq(unsigned int irq)
{
	unsigned int cpm_vec = (unsigned int)irq_map[irq].hwirq;

	setbits32(&cpic_reg->cpic_cimr, (1 << cpm_vec));
}

static void cpm_end_irq(unsigned int irq)
{
	unsigned int cpm_vec = (unsigned int)irq_map[irq].hwirq;

	out_be32(&cpic_reg->cpic_cisr, (1 << cpm_vec));
}

static struct irq_chip cpm_pic = {
	.name = " CPM PIC ",
	.mask = cpm_mask_irq,
	.unmask = cpm_unmask_irq,
	.eoi = cpm_end_irq,
};

int cpm_get_irq(void)
{
	int cpm_vec;

	/* Get the vector by setting the ACK bit and then reading
	 * the register.
	 */
	out_be16(&cpic_reg->cpic_civr, 1);
	cpm_vec = in_be16(&cpic_reg->cpic_civr);
	cpm_vec >>= 11;

	return irq_linear_revmap(cpm_pic_host, cpm_vec);
}

static int cpm_pic_host_map(struct irq_host *h, unsigned int virq,
			  irq_hw_number_t hw)
{
	pr_debug("cpm_pic_host_map(%d, 0x%lx)\n", virq, hw);

	irq_to_desc(virq)->status |= IRQ_LEVEL;
	set_irq_chip_and_handler(virq, &cpm_pic, handle_fasteoi_irq);
	return 0;
}

/* The CPM can generate the error interrupt when there is a race condition
 * between generating and masking interrupts.  All we have to do is ACK it
 * and return.  This is a no-op function so we don't need any special
 * tests in the interrupt handler.
 */
static irqreturn_t cpm_error_interrupt(int irq, void *dev)
{
	return IRQ_HANDLED;
}

static struct irqaction cpm_error_irqaction = {
	.handler = cpm_error_interrupt,
	.name = "error",
};

static struct irq_host_ops cpm_pic_host_ops = {
	.map = cpm_pic_host_map,
};

unsigned int cpm_pic_init(void)
{
	struct device_node *np = NULL;
	struct resource res;
	unsigned int sirq = NO_IRQ, hwirq, eirq;
	int ret;

	pr_debug("cpm_pic_init\n");

	np = of_find_compatible_node(NULL, NULL, "fsl,cpm1-pic");
	if (np == NULL)
		np = of_find_compatible_node(NULL, "cpm-pic", "CPM");
	if (np == NULL) {
		printk(KERN_ERR "CPM PIC init: can not find cpm-pic node\n");
		return sirq;
	}

	ret = of_address_to_resource(np, 0, &res);
	if (ret)
		goto end;

	cpic_reg = ioremap(res.start, res.end - res.start + 1);
	if (cpic_reg == NULL)
		goto end;

	sirq = irq_of_parse_and_map(np, 0);
	if (sirq == NO_IRQ)
		goto end;

	/* Initialize the CPM interrupt controller. */
	hwirq = (unsigned int)irq_map[sirq].hwirq;
	out_be32(&cpic_reg->cpic_cicr,
	    (CICR_SCD_SCC4 | CICR_SCC_SCC3 | CICR_SCB_SCC2 | CICR_SCA_SCC1) |
		((hwirq/2) << 13) | CICR_HP_MASK);

	out_be32(&cpic_reg->cpic_cimr, 0);

	cpm_pic_host = irq_alloc_host(np, IRQ_HOST_MAP_LINEAR,
				      64, &cpm_pic_host_ops, 64);
	if (cpm_pic_host == NULL) {
		printk(KERN_ERR "CPM2 PIC: failed to allocate irq host!\n");
		sirq = NO_IRQ;
		goto end;
	}

	/* Install our own error handler. */
	np = of_find_compatible_node(NULL, NULL, "fsl,cpm1");
	if (np == NULL)
		np = of_find_node_by_type(NULL, "cpm");
	if (np == NULL) {
		printk(KERN_ERR "CPM PIC init: can not find cpm node\n");
		goto end;
	}

	eirq = irq_of_parse_and_map(np, 0);
	if (eirq == NO_IRQ)
		goto end;

	if (setup_irq(eirq, &cpm_error_irqaction))
		printk(KERN_ERR "Could not allocate CPM error IRQ!");

	setbits32(&cpic_reg->cpic_cicr, CICR_IEN);

end:
	of_node_put(np);
	return sirq;
}

void __init cpm_reset(void)
{
	sysconf8xx_t __iomem *siu_conf;

	mpc8xx_immr = ioremap(get_immrbase(), 0x4000);
	if (!mpc8xx_immr) {
		printk(KERN_CRIT "Could not map IMMR\n");
		return;
	}

	cpmp = &mpc8xx_immr->im_cpm;

#ifndef CONFIG_PPC_EARLY_DEBUG_CPM
	/* Perform a reset.
	*/
	out_be16(&cpmp->cp_cpcr, CPM_CR_RST | CPM_CR_FLG);

	/* Wait for it.
	*/
	while (in_be16(&cpmp->cp_cpcr) & CPM_CR_FLG);
#endif

#ifdef CONFIG_UCODE_PATCH
	cpm_load_patch(cpmp);
#endif

	/* Set SDMA Bus Request priority 5.
	 * On 860T, this also enables FEC priority 6.  I am not sure
	 * this is what we realy want for some applications, but the
	 * manual recommends it.
	 * Bit 25, FAM can also be set to use FEC aggressive mode (860T).
	 */
	siu_conf = immr_map(im_siu_conf);
	out_be32(&siu_conf->sc_sdcr, 1);
	immr_unmap(siu_conf);

	cpm_muram_init();
}

static DEFINE_SPINLOCK(cmd_lock);

#define MAX_CR_CMD_LOOPS        10000

int cpm_command(u32 command, u8 opcode)
{
	int i, ret;
	unsigned long flags;

	if (command & 0xffffff0f)
		return -EINVAL;

	spin_lock_irqsave(&cmd_lock, flags);

	ret = 0;
	out_be16(&cpmp->cp_cpcr, command | CPM_CR_FLG | (opcode << 8));
	for (i = 0; i < MAX_CR_CMD_LOOPS; i++)
		if ((in_be16(&cpmp->cp_cpcr) & CPM_CR_FLG) == 0)
			goto out;

	printk(KERN_ERR "%s(): Not able to issue CPM command\n", __func__);
	ret = -EIO;
out:
	spin_unlock_irqrestore(&cmd_lock, flags);
	return ret;
}
EXPORT_SYMBOL(cpm_command);

/* Set a baud rate generator.  This needs lots of work.  There are
 * four BRGs, any of which can be wired to any channel.
 * The internal baud rate clock is the system clock divided by 16.
 * This assumes the baudrate is 16x oversampled by the uart.
 */
#define BRG_INT_CLK		(get_brgfreq())
#define BRG_UART_CLK		(BRG_INT_CLK/16)
#define BRG_UART_CLK_DIV16	(BRG_UART_CLK/16)

void
cpm_setbrg(uint brg, uint rate)
{
	u32 __iomem *bp;

	/* This is good enough to get SMCs running.....
	*/
	bp = &cpmp->cp_brgc1;
	bp += brg;
	/* The BRG has a 12-bit counter.  For really slow baud rates (or
	 * really fast processors), we may have to further divide by 16.
	 */
	if (((BRG_UART_CLK / rate) - 1) < 4096)
		out_be32(bp, (((BRG_UART_CLK / rate) - 1) << 1) | CPM_BRG_EN);
	else
		out_be32(bp, (((BRG_UART_CLK_DIV16 / rate) - 1) << 1) |
			      CPM_BRG_EN | CPM_BRG_DIV16);
}

struct cpm_ioport16 {
	__be16 dir, par, odr_sor, dat, intr;
	__be16 res[3];
};

struct cpm_ioport32b {
	__be32 dir, par, odr, dat;
};

struct cpm_ioport32e {
	__be32 dir, par, sor, odr, dat;
};

static void cpm1_set_pin32(int port, int pin, int flags)
{
	struct cpm_ioport32e __iomem *iop;
	pin = 1 << (31 - pin);

	if (port == CPM_PORTB)
		iop = (struct cpm_ioport32e __iomem *)
		      &mpc8xx_immr->im_cpm.cp_pbdir;
	else
		iop = (struct cpm_ioport32e __iomem *)
		      &mpc8xx_immr->im_cpm.cp_pedir;

	if (flags & CPM_PIN_OUTPUT)
		setbits32(&iop->dir, pin);
	else
		clrbits32(&iop->dir, pin);

	if (!(flags & CPM_PIN_GPIO))
		setbits32(&iop->par, pin);
	else
		clrbits32(&iop->par, pin);

	if (port == CPM_PORTB) {
		if (flags & CPM_PIN_OPENDRAIN)
			setbits16(&mpc8xx_immr->im_cpm.cp_pbodr, pin);
		else
			clrbits16(&mpc8xx_immr->im_cpm.cp_pbodr, pin);
	}

	if (port == CPM_PORTE) {
		if (flags & CPM_PIN_SECONDARY)
			setbits32(&iop->sor, pin);
		else
			clrbits32(&iop->sor, pin);

		if (flags & CPM_PIN_OPENDRAIN)
			setbits32(&mpc8xx_immr->im_cpm.cp_peodr, pin);
		else
			clrbits32(&mpc8xx_immr->im_cpm.cp_peodr, pin);
	}
}

static void cpm1_set_pin16(int port, int pin, int flags)
{
	struct cpm_ioport16 __iomem *iop =
		(struct cpm_ioport16 __iomem *)&mpc8xx_immr->im_ioport;

	pin = 1 << (15 - pin);

	if (port != 0)
		iop += port - 1;

	if (flags & CPM_PIN_OUTPUT)
		setbits16(&iop->dir, pin);
	else
		clrbits16(&iop->dir, pin);

	if (!(flags & CPM_PIN_GPIO))
		setbits16(&iop->par, pin);
	else
		clrbits16(&iop->par, pin);

	if (port == CPM_PORTA) {
		if (flags & CPM_PIN_OPENDRAIN)
			setbits16(&iop->odr_sor, pin);
		else
			clrbits16(&iop->odr_sor, pin);
	}
	if (port == CPM_PORTC) {
		if (flags & CPM_PIN_SECONDARY)
			setbits16(&iop->odr_sor, pin);
		else
			clrbits16(&iop->odr_sor, pin);
	}
}

void cpm1_set_pin(enum cpm_port port, int pin, int flags)
{
	if (port == CPM_PORTB || port == CPM_PORTE)
		cpm1_set_pin32(port, pin, flags);
	else
		cpm1_set_pin16(port, pin, flags);
}

int cpm1_clk_setup(enum cpm_clk_target target, int clock, int mode)
{
	int shift;
	int i, bits = 0;
	u32 __iomem *reg;
	u32 mask = 7;

	u8 clk_map[][3] = {
		{CPM_CLK_SCC1, CPM_BRG1, 0},
		{CPM_CLK_SCC1, CPM_BRG2, 1},
		{CPM_CLK_SCC1, CPM_BRG3, 2},
		{CPM_CLK_SCC1, CPM_BRG4, 3},
		{CPM_CLK_SCC1, CPM_CLK1, 4},
		{CPM_CLK_SCC1, CPM_CLK2, 5},
		{CPM_CLK_SCC1, CPM_CLK3, 6},
		{CPM_CLK_SCC1, CPM_CLK4, 7},

		{CPM_CLK_SCC2, CPM_BRG1, 0},
		{CPM_CLK_SCC2, CPM_BRG2, 1},
		{CPM_CLK_SCC2, CPM_BRG3, 2},
		{CPM_CLK_SCC2, CPM_BRG4, 3},
		{CPM_CLK_SCC2, CPM_CLK1, 4},
		{CPM_CLK_SCC2, CPM_CLK2, 5},
		{CPM_CLK_SCC2, CPM_CLK3, 6},
		{CPM_CLK_SCC2, CPM_CLK4, 7},

		{CPM_CLK_SCC3, CPM_BRG1, 0},
		{CPM_CLK_SCC3, CPM_BRG2, 1},
		{CPM_CLK_SCC3, CPM_BRG3, 2},
		{CPM_CLK_SCC3, CPM_BRG4, 3},
		{CPM_CLK_SCC3, CPM_CLK5, 4},
		{CPM_CLK_SCC3, CPM_CLK6, 5},
		{CPM_CLK_SCC3, CPM_CLK7, 6},
		{CPM_CLK_SCC3, CPM_CLK8, 7},

		{CPM_CLK_SCC4, CPM_BRG1, 0},
		{CPM_CLK_SCC4, CPM_BRG2, 1},
		{CPM_CLK_SCC4, CPM_BRG3, 2},
		{CPM_CLK_SCC4, CPM_BRG4, 3},
		{CPM_CLK_SCC4, CPM_CLK5, 4},
		{CPM_CLK_SCC4, CPM_CLK6, 5},
		{CPM_CLK_SCC4, CPM_CLK7, 6},
		{CPM_CLK_SCC4, CPM_CLK8, 7},

		{CPM_CLK_SMC1, CPM_BRG1, 0},
		{CPM_CLK_SMC1, CPM_BRG2, 1},
		{CPM_CLK_SMC1, CPM_BRG3, 2},
		{CPM_CLK_SMC1, CPM_BRG4, 3},
		{CPM_CLK_SMC1, CPM_CLK1, 4},
		{CPM_CLK_SMC1, CPM_CLK2, 5},
		{CPM_CLK_SMC1, CPM_CLK3, 6},
		{CPM_CLK_SMC1, CPM_CLK4, 7},

		{CPM_CLK_SMC2, CPM_BRG1, 0},
		{CPM_CLK_SMC2, CPM_BRG2, 1},
		{CPM_CLK_SMC2, CPM_BRG3, 2},
		{CPM_CLK_SMC2, CPM_BRG4, 3},
		{CPM_CLK_SMC2, CPM_CLK5, 4},
		{CPM_CLK_SMC2, CPM_CLK6, 5},
		{CPM_CLK_SMC2, CPM_CLK7, 6},
		{CPM_CLK_SMC2, CPM_CLK8, 7},
	};

	switch (target) {
	case CPM_CLK_SCC1:
		reg = &mpc8xx_immr->im_cpm.cp_sicr;
		shift = 0;
		break;

	case CPM_CLK_SCC2:
		reg = &mpc8xx_immr->im_cpm.cp_sicr;
		shift = 8;
		break;

	case CPM_CLK_SCC3:
		reg = &mpc8xx_immr->im_cpm.cp_sicr;
		shift = 16;
		break;

	case CPM_CLK_SCC4:
		reg = &mpc8xx_immr->im_cpm.cp_sicr;
		shift = 24;
		break;

	case CPM_CLK_SMC1:
		reg = &mpc8xx_immr->im_cpm.cp_simode;
		shift = 12;
		break;

	case CPM_CLK_SMC2:
		reg = &mpc8xx_immr->im_cpm.cp_simode;
		shift = 28;
		break;

	default:
		printk(KERN_ERR "cpm1_clock_setup: invalid clock target\n");
		return -EINVAL;
	}

	if (reg == &mpc8xx_immr->im_cpm.cp_sicr && mode == CPM_CLK_RX)
		shift += 3;

	for (i = 0; i < ARRAY_SIZE(clk_map); i++) {
		if (clk_map[i][0] == target && clk_map[i][1] == clock) {
			bits = clk_map[i][2];
			break;
		}
	}

	if (i == ARRAY_SIZE(clk_map)) {
		printk(KERN_ERR "cpm1_clock_setup: invalid clock combination\n");
		return -EINVAL;
	}

	bits <<= shift;
	mask <<= shift;
	out_be32(reg, (in_be32(reg) & ~mask) | bits);

	return 0;
}

/*
 * GPIO LIB API implementation
 */
#ifdef CONFIG_8xx_GPIO

struct cpm1_gpio16_chip {
	struct of_mm_gpio_chip mm_gc;
	spinlock_t lock;

	/* shadowed data register to clear/set bits safely */
	u16 cpdata;
};

static inline struct cpm1_gpio16_chip *
to_cpm1_gpio16_chip(struct of_mm_gpio_chip *mm_gc)
{
	return container_of(mm_gc, struct cpm1_gpio16_chip, mm_gc);
}

static void cpm1_gpio16_save_regs(struct of_mm_gpio_chip *mm_gc)
{
	struct cpm1_gpio16_chip *cpm1_gc = to_cpm1_gpio16_chip(mm_gc);
	struct cpm_ioport16 __iomem *iop = mm_gc->regs;

	cpm1_gc->cpdata = in_be16(&iop->dat);
}

static int cpm1_gpio16_get(struct gpio_chip *gc, unsigned int gpio)
{
	struct of_mm_gpio_chip *mm_gc = to_of_mm_gpio_chip(gc);
	struct cpm_ioport16 __iomem *iop = mm_gc->regs;
	u16 pin_mask;

	pin_mask = 1 << (15 - gpio);

	return !!(in_be16(&iop->dat) & pin_mask);
}

static void __cpm1_gpio16_set(struct of_mm_gpio_chip *mm_gc, u16 pin_mask,
	int value)
{
	struct cpm1_gpio16_chip *cpm1_gc = to_cpm1_gpio16_chip(mm_gc);
	struct cpm_ioport16 __iomem *iop = mm_gc->regs;

	if (value)
		cpm1_gc->cpdata |= pin_mask;
	else
		cpm1_gc->cpdata &= ~pin_mask;

	out_be16(&iop->dat, cpm1_gc->cpdata);
}

static void cpm1_gpio16_set(struct gpio_chip *gc, unsigned int gpio, int value)
{
	struct of_mm_gpio_chip *mm_gc = to_of_mm_gpio_chip(gc);
	struct cpm1_gpio16_chip *cpm1_gc = to_cpm1_gpio16_chip(mm_gc);
	unsigned long flags;
	u16 pin_mask = 1 << (15 - gpio);

	spin_lock_irqsave(&cpm1_gc->lock, flags);

	__cpm1_gpio16_set(mm_gc, pin_mask, value);

	spin_unlock_irqrestore(&cpm1_gc->lock, flags);
}

static int cpm1_gpio16_dir_out(struct gpio_chip *gc, unsigned int gpio, int val)
{
	struct of_mm_gpio_chip *mm_gc = to_of_mm_gpio_chip(gc);
	struct cpm1_gpio16_chip *cpm1_gc = to_cpm1_gpio16_chip(mm_gc);
	struct cpm_ioport16 __iomem *iop = mm_gc->regs;
	unsigned long flags;
	u16 pin_mask = 1 << (15 - gpio);

	spin_lock_irqsave(&cpm1_gc->lock, flags);

	setbits16(&iop->dir, pin_mask);
	__cpm1_gpio16_set(mm_gc, pin_mask, val);

	spin_unlock_irqrestore(&cpm1_gc->lock, flags);

	return 0;
}

static int cpm1_gpio16_dir_in(struct gpio_chip *gc, unsigned int gpio)
{
	struct of_mm_gpio_chip *mm_gc = to_of_mm_gpio_chip(gc);
	struct cpm1_gpio16_chip *cpm1_gc = to_cpm1_gpio16_chip(mm_gc);
	struct cpm_ioport16 __iomem *iop = mm_gc->regs;
	unsigned long flags;
	u16 pin_mask = 1 << (15 - gpio);

	spin_lock_irqsave(&cpm1_gc->lock, flags);

	clrbits16(&iop->dir, pin_mask);

	spin_unlock_irqrestore(&cpm1_gc->lock, flags);

	return 0;
}

int cpm1_gpiochip_add16(struct device_node *np)
{
	struct cpm1_gpio16_chip *cpm1_gc;
	struct of_mm_gpio_chip *mm_gc;
	struct of_gpio_chip *of_gc;
	struct gpio_chip *gc;

	cpm1_gc = kzalloc(sizeof(*cpm1_gc), GFP_KERNEL);
	if (!cpm1_gc)
		return -ENOMEM;

	spin_lock_init(&cpm1_gc->lock);

	mm_gc = &cpm1_gc->mm_gc;
	of_gc = &mm_gc->of_gc;
	gc = &of_gc->gc;

	mm_gc->save_regs = cpm1_gpio16_save_regs;
	of_gc->gpio_cells = 2;
	gc->ngpio = 16;
	gc->direction_input = cpm1_gpio16_dir_in;
	gc->direction_output = cpm1_gpio16_dir_out;
	gc->get = cpm1_gpio16_get;
	gc->set = cpm1_gpio16_set;

	return of_mm_gpiochip_add(np, mm_gc);
}

struct cpm1_gpio32_chip {
	struct of_mm_gpio_chip mm_gc;
	spinlock_t lock;

	/* shadowed data register to clear/set bits safely */
	u32 cpdata;
};

static inline struct cpm1_gpio32_chip *
to_cpm1_gpio32_chip(struct of_mm_gpio_chip *mm_gc)
{
	return container_of(mm_gc, struct cpm1_gpio32_chip, mm_gc);
}

static void cpm1_gpio32_save_regs(struct of_mm_gpio_chip *mm_gc)
{
	struct cpm1_gpio32_chip *cpm1_gc = to_cpm1_gpio32_chip(mm_gc);
	struct cpm_ioport32b __iomem *iop = mm_gc->regs;

	cpm1_gc->cpdata = in_be32(&iop->dat);
}

static int cpm1_gpio32_get(struct gpio_chip *gc, unsigned int gpio)
{
	struct of_mm_gpio_chip *mm_gc = to_of_mm_gpio_chip(gc);
	struct cpm_ioport32b __iomem *iop = mm_gc->regs;
	u32 pin_mask;

	pin_mask = 1 << (31 - gpio);

	return !!(in_be32(&iop->dat) & pin_mask);
}

static void __cpm1_gpio32_set(struct of_mm_gpio_chip *mm_gc, u32 pin_mask,
	int value)
{
	struct cpm1_gpio32_chip *cpm1_gc = to_cpm1_gpio32_chip(mm_gc);
	struct cpm_ioport32b __iomem *iop = mm_gc->regs;

	if (value)
		cpm1_gc->cpdata |= pin_mask;
	else
		cpm1_gc->cpdata &= ~pin_mask;

	out_be32(&iop->dat, cpm1_gc->cpdata);
}

static void cpm1_gpio32_set(struct gpio_chip *gc, unsigned int gpio, int value)
{
	struct of_mm_gpio_chip *mm_gc = to_of_mm_gpio_chip(gc);
	struct cpm1_gpio32_chip *cpm1_gc = to_cpm1_gpio32_chip(mm_gc);
	unsigned long flags;
	u32 pin_mask = 1 << (31 - gpio);

	spin_lock_irqsave(&cpm1_gc->lock, flags);

	__cpm1_gpio32_set(mm_gc, pin_mask, value);

	spin_unlock_irqrestore(&cpm1_gc->lock, flags);
}

static int cpm1_gpio32_dir_out(struct gpio_chip *gc, unsigned int gpio, int val)
{
	struct of_mm_gpio_chip *mm_gc = to_of_mm_gpio_chip(gc);
	struct cpm1_gpio32_chip *cpm1_gc = to_cpm1_gpio32_chip(mm_gc);
	struct cpm_ioport32b __iomem *iop = mm_gc->regs;
	unsigned long flags;
	u32 pin_mask = 1 << (31 - gpio);

	spin_lock_irqsave(&cpm1_gc->lock, flags);

	setbits32(&iop->dir, pin_mask);
	__cpm1_gpio32_set(mm_gc, pin_mask, val);

	spin_unlock_irqrestore(&cpm1_gc->lock, flags);

	return 0;
}

static int cpm1_gpio32_dir_in(struct gpio_chip *gc, unsigned int gpio)
{
	struct of_mm_gpio_chip *mm_gc = to_of_mm_gpio_chip(gc);
	struct cpm1_gpio32_chip *cpm1_gc = to_cpm1_gpio32_chip(mm_gc);
	struct cpm_ioport32b __iomem *iop = mm_gc->regs;
	unsigned long flags;
	u32 pin_mask = 1 << (31 - gpio);

	spin_lock_irqsave(&cpm1_gc->lock, flags);

	clrbits32(&iop->dir, pin_mask);

	spin_unlock_irqrestore(&cpm1_gc->lock, flags);

	return 0;
}

int cpm1_gpiochip_add32(struct device_node *np)
{
	struct cpm1_gpio32_chip *cpm1_gc;
	struct of_mm_gpio_chip *mm_gc;
	struct of_gpio_chip *of_gc;
	struct gpio_chip *gc;

	cpm1_gc = kzalloc(sizeof(*cpm1_gc), GFP_KERNEL);
	if (!cpm1_gc)
		return -ENOMEM;

	spin_lock_init(&cpm1_gc->lock);

	mm_gc = &cpm1_gc->mm_gc;
	of_gc = &mm_gc->of_gc;
	gc = &of_gc->gc;

	mm_gc->save_regs = cpm1_gpio32_save_regs;
	of_gc->gpio_cells = 2;
	gc->ngpio = 32;
	gc->direction_input = cpm1_gpio32_dir_in;
	gc->direction_output = cpm1_gpio32_dir_out;
	gc->get = cpm1_gpio32_get;
	gc->set = cpm1_gpio32_set;

	return of_mm_gpiochip_add(np, mm_gc);
}

static int cpm_init_par_io(void)
{
	struct device_node *np;

	for_each_compatible_node(np, NULL, "fsl,cpm1-pario-bank-a")
		cpm1_gpiochip_add16(np);

	for_each_compatible_node(np, NULL, "fsl,cpm1-pario-bank-b")
		cpm1_gpiochip_add32(np);

	for_each_compatible_node(np, NULL, "fsl,cpm1-pario-bank-c")
		cpm1_gpiochip_add16(np);

	for_each_compatible_node(np, NULL, "fsl,cpm1-pario-bank-d")
		cpm1_gpiochip_add16(np);

	/* Port E uses CPM2 layout */
	for_each_compatible_node(np, NULL, "fsl,cpm1-pario-bank-e")
		cpm2_gpiochip_add32(np);
	return 0;
}
arch_initcall(cpm_init_par_io);

#endif /* CONFIG_8xx_GPIO */
