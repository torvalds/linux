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

#define CPM_MAP_SIZE    (0x4000)

#ifndef CONFIG_PPC_CPM_NEW_BINDING
static void m8xx_cpm_dpinit(void);
#endif
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
	.typename = " CPM PIC ",
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

	get_irq_desc(virq)->status |= IRQ_LEVEL;
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
	.mask = CPU_MASK_NONE,
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

	cpm_pic_host = irq_alloc_host(of_node_get(np), IRQ_HOST_MAP_LINEAR,
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

#ifdef CONFIG_PPC_CPM_NEW_BINDING
	cpm_muram_init();
#else
	/* Reclaim the DP memory for our use. */
	m8xx_cpm_dpinit();
#endif
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

	printk(KERN_ERR "%s(): Not able to issue CPM command\n", __FUNCTION__);
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

#ifndef CONFIG_PPC_CPM_NEW_BINDING
/*
 * dpalloc / dpfree bits.
 */
static spinlock_t cpm_dpmem_lock;
/*
 * 16 blocks should be enough to satisfy all requests
 * until the memory subsystem goes up...
 */
static rh_block_t cpm_boot_dpmem_rh_block[16];
static rh_info_t cpm_dpmem_info;

#define CPM_DPMEM_ALIGNMENT	8
static u8 __iomem *dpram_vbase;
static phys_addr_t dpram_pbase;

static void m8xx_cpm_dpinit(void)
{
	spin_lock_init(&cpm_dpmem_lock);

	dpram_vbase = cpmp->cp_dpmem;
	dpram_pbase = get_immrbase() + offsetof(immap_t, im_cpm.cp_dpmem);

	/* Initialize the info header */
	rh_init(&cpm_dpmem_info, CPM_DPMEM_ALIGNMENT,
			sizeof(cpm_boot_dpmem_rh_block) /
			sizeof(cpm_boot_dpmem_rh_block[0]),
			cpm_boot_dpmem_rh_block);

	/*
	 * Attach the usable dpmem area.
	 * XXX: This is actually crap.  CPM_DATAONLY_BASE and
	 * CPM_DATAONLY_SIZE are a subset of the available dparm.  It varies
	 * with the processor and the microcode patches applied / activated.
	 * But the following should be at least safe.
	 */
	rh_attach_region(&cpm_dpmem_info, CPM_DATAONLY_BASE, CPM_DATAONLY_SIZE);
}

/*
 * Allocate the requested size worth of DP memory.
 * This function returns an offset into the DPRAM area.
 * Use cpm_dpram_addr() to get the virtual address of the area.
 */
unsigned long cpm_dpalloc(uint size, uint align)
{
	unsigned long start;
	unsigned long flags;

	spin_lock_irqsave(&cpm_dpmem_lock, flags);
	cpm_dpmem_info.alignment = align;
	start = rh_alloc(&cpm_dpmem_info, size, "commproc");
	spin_unlock_irqrestore(&cpm_dpmem_lock, flags);

	return (uint)start;
}
EXPORT_SYMBOL(cpm_dpalloc);

int cpm_dpfree(unsigned long offset)
{
	int ret;
	unsigned long flags;

	spin_lock_irqsave(&cpm_dpmem_lock, flags);
	ret = rh_free(&cpm_dpmem_info, offset);
	spin_unlock_irqrestore(&cpm_dpmem_lock, flags);

	return ret;
}
EXPORT_SYMBOL(cpm_dpfree);

unsigned long cpm_dpalloc_fixed(unsigned long offset, uint size, uint align)
{
	unsigned long start;
	unsigned long flags;

	spin_lock_irqsave(&cpm_dpmem_lock, flags);
	cpm_dpmem_info.alignment = align;
	start = rh_alloc_fixed(&cpm_dpmem_info, offset, size, "commproc");
	spin_unlock_irqrestore(&cpm_dpmem_lock, flags);

	return start;
}
EXPORT_SYMBOL(cpm_dpalloc_fixed);

void cpm_dpdump(void)
{
	rh_dump(&cpm_dpmem_info);
}
EXPORT_SYMBOL(cpm_dpdump);

void *cpm_dpram_addr(unsigned long offset)
{
	return (void *)(dpram_vbase + offset);
}
EXPORT_SYMBOL(cpm_dpram_addr);

uint cpm_dpram_phys(u8 *addr)
{
	return (dpram_pbase + (uint)(addr - dpram_vbase));
}
EXPORT_SYMBOL(cpm_dpram_phys);
#endif /* !CONFIG_PPC_CPM_NEW_BINDING */

struct cpm_ioport16 {
	__be16 dir, par, odr_sor, dat, intr;
	__be16 res[3];
};

struct cpm_ioport32 {
	__be32 dir, par, sor;
};

static void cpm1_set_pin32(int port, int pin, int flags)
{
	struct cpm_ioport32 __iomem *iop;
	pin = 1 << (31 - pin);

	if (port == CPM_PORTB)
		iop = (struct cpm_ioport32 __iomem *)
		      &mpc8xx_immr->im_cpm.cp_pbdir;
	else
		iop = (struct cpm_ioport32 __iomem *)
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
