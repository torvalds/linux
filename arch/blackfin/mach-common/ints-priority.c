/*
 * Set up the interrupt priorities
 *
 * Copyright  2004-2009 Analog Devices Inc.
 *                 2003 Bas Vermeulen <bas@buyways.nl>
 *                 2002 Arcturus Networks Inc. MaTed <mated@sympatico.ca>
 *            2000-2001 Lineo, Inc. D. Jefff Dionne <jeff@lineo.ca>
 *                 1999 D. Jeff Dionne <jeff@uclinux.org>
 *                 1996 Roman Zippel
 *
 * Licensed under the GPL-2
 */

#include <linux/module.h>
#include <linux/kernel_stat.h>
#include <linux/seq_file.h>
#include <linux/irq.h>
#include <linux/sched.h>
#include <linux/syscore_ops.h>
#include <asm/delay.h>
#ifdef CONFIG_IPIPE
#include <linux/ipipe.h>
#endif
#include <asm/traps.h>
#include <asm/blackfin.h>
#include <asm/gpio.h>
#include <asm/irq_handler.h>
#include <asm/dpmc.h>
#include <asm/traps.h>

/*
 * NOTES:
 * - we have separated the physical Hardware interrupt from the
 * levels that the LINUX kernel sees (see the description in irq.h)
 * -
 */

#ifndef CONFIG_SMP
/* Initialize this to an actual value to force it into the .data
 * section so that we know it is properly initialized at entry into
 * the kernel but before bss is initialized to zero (which is where
 * it would live otherwise).  The 0x1f magic represents the IRQs we
 * cannot actually mask out in hardware.
 */
unsigned long bfin_irq_flags = 0x1f;
EXPORT_SYMBOL(bfin_irq_flags);
#endif

#ifdef CONFIG_PM
unsigned long bfin_sic_iwr[3];	/* Up to 3 SIC_IWRx registers */
unsigned vr_wakeup;
#endif

#ifndef SEC_GCTL
static struct ivgx {
	/* irq number for request_irq, available in mach-bf5xx/irq.h */
	unsigned int irqno;
	/* corresponding bit in the SIC_ISR register */
	unsigned int isrflag;
} ivg_table[NR_PERI_INTS];

static struct ivg_slice {
	/* position of first irq in ivg_table for given ivg */
	struct ivgx *ifirst;
	struct ivgx *istop;
} ivg7_13[IVG13 - IVG7 + 1];


/*
 * Search SIC_IAR and fill tables with the irqvalues
 * and their positions in the SIC_ISR register.
 */
static void __init search_IAR(void)
{
	unsigned ivg, irq_pos = 0;
	for (ivg = 0; ivg <= IVG13 - IVG7; ivg++) {
		int irqN;

		ivg7_13[ivg].istop = ivg7_13[ivg].ifirst = &ivg_table[irq_pos];

		for (irqN = 0; irqN < NR_PERI_INTS; irqN += 4) {
			int irqn;
			u32 iar =
				bfin_read32((unsigned long *)SIC_IAR0 +
#if defined(CONFIG_BF51x) || defined(CONFIG_BF52x) || \
	defined(CONFIG_BF538) || defined(CONFIG_BF539)
				((irqN % 32) >> 3) + ((irqN / 32) * ((SIC_IAR4 - SIC_IAR0) / 4))
#else
				(irqN >> 3)
#endif
				);
			for (irqn = irqN; irqn < irqN + 4; ++irqn) {
				int iar_shift = (irqn & 7) * 4;
				if (ivg == (0xf & (iar >> iar_shift))) {
					ivg_table[irq_pos].irqno = IVG7 + irqn;
					ivg_table[irq_pos].isrflag = 1 << (irqn % 32);
					ivg7_13[ivg].istop++;
					irq_pos++;
				}
			}
		}
	}
}
#endif

/*
 * This is for core internal IRQs
 */
void bfin_ack_noop(struct irq_data *d)
{
	/* Dummy function.  */
}

static void bfin_core_mask_irq(struct irq_data *d)
{
	bfin_irq_flags &= ~(1 << d->irq);
	if (!hard_irqs_disabled())
		hard_local_irq_enable();
}

static void bfin_core_unmask_irq(struct irq_data *d)
{
	bfin_irq_flags |= 1 << d->irq;
	/*
	 * If interrupts are enabled, IMASK must contain the same value
	 * as bfin_irq_flags.  Make sure that invariant holds.  If interrupts
	 * are currently disabled we need not do anything; one of the
	 * callers will take care of setting IMASK to the proper value
	 * when reenabling interrupts.
	 * local_irq_enable just does "STI bfin_irq_flags", so it's exactly
	 * what we need.
	 */
	if (!hard_irqs_disabled())
		hard_local_irq_enable();
	return;
}

#ifndef SEC_GCTL
void bfin_internal_mask_irq(unsigned int irq)
{
	unsigned long flags = hard_local_irq_save();
#ifdef SIC_IMASK0
	unsigned mask_bank = BFIN_SYSIRQ(irq) / 32;
	unsigned mask_bit = BFIN_SYSIRQ(irq) % 32;
	bfin_write_SIC_IMASK(mask_bank, bfin_read_SIC_IMASK(mask_bank) &
			~(1 << mask_bit));
# if defined(CONFIG_SMP) || defined(CONFIG_ICC)
	bfin_write_SICB_IMASK(mask_bank, bfin_read_SICB_IMASK(mask_bank) &
			~(1 << mask_bit));
# endif
#else
	bfin_write_SIC_IMASK(bfin_read_SIC_IMASK() &
			~(1 << BFIN_SYSIRQ(irq)));
#endif /* end of SIC_IMASK0 */
	hard_local_irq_restore(flags);
}

static void bfin_internal_mask_irq_chip(struct irq_data *d)
{
	bfin_internal_mask_irq(d->irq);
}

#ifdef CONFIG_SMP
void bfin_internal_unmask_irq_affinity(unsigned int irq,
		const struct cpumask *affinity)
#else
void bfin_internal_unmask_irq(unsigned int irq)
#endif
{
	unsigned long flags = hard_local_irq_save();

#ifdef SIC_IMASK0
	unsigned mask_bank = BFIN_SYSIRQ(irq) / 32;
	unsigned mask_bit = BFIN_SYSIRQ(irq) % 32;
# ifdef CONFIG_SMP
	if (cpumask_test_cpu(0, affinity))
# endif
		bfin_write_SIC_IMASK(mask_bank,
				bfin_read_SIC_IMASK(mask_bank) |
				(1 << mask_bit));
# ifdef CONFIG_SMP
	if (cpumask_test_cpu(1, affinity))
		bfin_write_SICB_IMASK(mask_bank,
				bfin_read_SICB_IMASK(mask_bank) |
				(1 << mask_bit));
# endif
#else
	bfin_write_SIC_IMASK(bfin_read_SIC_IMASK() |
			(1 << BFIN_SYSIRQ(irq)));
#endif
	hard_local_irq_restore(flags);
}

#ifdef CONFIG_SMP
static void bfin_internal_unmask_irq_chip(struct irq_data *d)
{
	bfin_internal_unmask_irq_affinity(d->irq, d->affinity);
}

static int bfin_internal_set_affinity(struct irq_data *d,
				      const struct cpumask *mask, bool force)
{
	bfin_internal_mask_irq(d->irq);
	bfin_internal_unmask_irq_affinity(d->irq, mask);

	return 0;
}
#else
static void bfin_internal_unmask_irq_chip(struct irq_data *d)
{
	bfin_internal_unmask_irq(d->irq);
}
#endif

#if defined(CONFIG_PM)
int bfin_internal_set_wake(unsigned int irq, unsigned int state)
{
	u32 bank, bit, wakeup = 0;
	unsigned long flags;
	bank = BFIN_SYSIRQ(irq) / 32;
	bit = BFIN_SYSIRQ(irq) % 32;

	switch (irq) {
#ifdef IRQ_RTC
	case IRQ_RTC:
	wakeup |= WAKE;
	break;
#endif
#ifdef IRQ_CAN0_RX
	case IRQ_CAN0_RX:
	wakeup |= CANWE;
	break;
#endif
#ifdef IRQ_CAN1_RX
	case IRQ_CAN1_RX:
	wakeup |= CANWE;
	break;
#endif
#ifdef IRQ_USB_INT0
	case IRQ_USB_INT0:
	wakeup |= USBWE;
	break;
#endif
#ifdef CONFIG_BF54x
	case IRQ_CNT:
	wakeup |= ROTWE;
	break;
#endif
	default:
	break;
	}

	flags = hard_local_irq_save();

	if (state) {
		bfin_sic_iwr[bank] |= (1 << bit);
		vr_wakeup  |= wakeup;

	} else {
		bfin_sic_iwr[bank] &= ~(1 << bit);
		vr_wakeup  &= ~wakeup;
	}

	hard_local_irq_restore(flags);

	return 0;
}

static int bfin_internal_set_wake_chip(struct irq_data *d, unsigned int state)
{
	return bfin_internal_set_wake(d->irq, state);
}
#else
inline int bfin_internal_set_wake(unsigned int irq, unsigned int state)
{
	return 0;
}
# define bfin_internal_set_wake_chip NULL
#endif

#else /* SEC_GCTL */
static void bfin_sec_preflow_handler(struct irq_data *d)
{
	unsigned long flags = hard_local_irq_save();
	unsigned int sid = BFIN_SYSIRQ(d->irq);

	bfin_write_SEC_SCI(0, SEC_CSID, sid);

	hard_local_irq_restore(flags);
}

static void bfin_sec_mask_ack_irq(struct irq_data *d)
{
	unsigned long flags = hard_local_irq_save();
	unsigned int sid = BFIN_SYSIRQ(d->irq);

	bfin_write_SEC_SCI(0, SEC_CSID, sid);

	hard_local_irq_restore(flags);
}

static void bfin_sec_unmask_irq(struct irq_data *d)
{
	unsigned long flags = hard_local_irq_save();
	unsigned int sid = BFIN_SYSIRQ(d->irq);

	bfin_write32(SEC_END, sid);

	hard_local_irq_restore(flags);
}

static void bfin_sec_enable_ssi(unsigned int sid)
{
	unsigned long flags = hard_local_irq_save();
	uint32_t reg_sctl = bfin_read_SEC_SCTL(sid);

	reg_sctl |= SEC_SCTL_SRC_EN;
	bfin_write_SEC_SCTL(sid, reg_sctl);

	hard_local_irq_restore(flags);
}

static void bfin_sec_disable_ssi(unsigned int sid)
{
	unsigned long flags = hard_local_irq_save();
	uint32_t reg_sctl = bfin_read_SEC_SCTL(sid);

	reg_sctl &= ((uint32_t)~SEC_SCTL_SRC_EN);
	bfin_write_SEC_SCTL(sid, reg_sctl);

	hard_local_irq_restore(flags);
}

static void bfin_sec_set_ssi_coreid(unsigned int sid, unsigned int coreid)
{
	unsigned long flags = hard_local_irq_save();
	uint32_t reg_sctl = bfin_read_SEC_SCTL(sid);

	reg_sctl &= ((uint32_t)~SEC_SCTL_CTG);
	bfin_write_SEC_SCTL(sid, reg_sctl | ((coreid << 20) & SEC_SCTL_CTG));

	hard_local_irq_restore(flags);
}

static void bfin_sec_enable_sci(unsigned int sid)
{
	unsigned long flags = hard_local_irq_save();
	uint32_t reg_sctl = bfin_read_SEC_SCTL(sid);

	if (sid == BFIN_SYSIRQ(IRQ_WATCH0))
		reg_sctl |= SEC_SCTL_FAULT_EN;
	else
		reg_sctl |= SEC_SCTL_INT_EN;
	bfin_write_SEC_SCTL(sid, reg_sctl);

	hard_local_irq_restore(flags);
}

static void bfin_sec_disable_sci(unsigned int sid)
{
	unsigned long flags = hard_local_irq_save();
	uint32_t reg_sctl = bfin_read_SEC_SCTL(sid);

	reg_sctl &= ((uint32_t)~SEC_SCTL_INT_EN);
	bfin_write_SEC_SCTL(sid, reg_sctl);

	hard_local_irq_restore(flags);
}

static void bfin_sec_enable(struct irq_data *d)
{
	unsigned long flags = hard_local_irq_save();
	unsigned int sid = BFIN_SYSIRQ(d->irq);

	bfin_sec_enable_sci(sid);
	bfin_sec_enable_ssi(sid);

	hard_local_irq_restore(flags);
}

static void bfin_sec_disable(struct irq_data *d)
{
	unsigned long flags = hard_local_irq_save();
	unsigned int sid = BFIN_SYSIRQ(d->irq);

	bfin_sec_disable_sci(sid);
	bfin_sec_disable_ssi(sid);

	hard_local_irq_restore(flags);
}

static void bfin_sec_set_priority(unsigned int sec_int_levels, u8 *sec_int_priority)
{
	unsigned long flags = hard_local_irq_save();
	uint32_t reg_sctl;
	int i;

	bfin_write_SEC_SCI(0, SEC_CPLVL, sec_int_levels);

	for (i = 0; i < SYS_IRQS - BFIN_IRQ(0); i++) {
		reg_sctl = bfin_read_SEC_SCTL(i) & ~SEC_SCTL_PRIO;
		reg_sctl |= sec_int_priority[i] << SEC_SCTL_PRIO_OFFSET;
		bfin_write_SEC_SCTL(i, reg_sctl);
	}

	hard_local_irq_restore(flags);
}

void bfin_sec_raise_irq(unsigned int irq)
{
	unsigned long flags = hard_local_irq_save();
	unsigned int sid = BFIN_SYSIRQ(irq);

	bfin_write32(SEC_RAISE, sid);

	hard_local_irq_restore(flags);
}

static void init_software_driven_irq(void)
{
	bfin_sec_set_ssi_coreid(34, 0);
	bfin_sec_set_ssi_coreid(35, 1);

	bfin_sec_enable_sci(35);
	bfin_sec_enable_ssi(35);
	bfin_sec_set_ssi_coreid(36, 0);
	bfin_sec_set_ssi_coreid(37, 1);
	bfin_sec_enable_sci(37);
	bfin_sec_enable_ssi(37);
}

void bfin_sec_resume(void)
{
	bfin_write_SEC_SCI(0, SEC_CCTL, SEC_CCTL_RESET);
	udelay(100);
	bfin_write_SEC_GCTL(SEC_GCTL_EN);
	bfin_write_SEC_SCI(0, SEC_CCTL, SEC_CCTL_EN | SEC_CCTL_NMI_EN);
}

void handle_sec_sfi_fault(uint32_t gstat)
{

}

void handle_sec_sci_fault(uint32_t gstat)
{
	uint32_t core_id;
	uint32_t cstat;

	core_id = gstat & SEC_GSTAT_SCI;
	cstat = bfin_read_SEC_SCI(core_id, SEC_CSTAT);
	if (cstat & SEC_CSTAT_ERR) {
		switch (cstat & SEC_CSTAT_ERRC) {
		case SEC_CSTAT_ACKERR:
			printk(KERN_DEBUG "sec ack err\n");
			break;
		default:
			printk(KERN_DEBUG "sec sci unknow err\n");
		}
	}

}

void handle_sec_ssi_fault(uint32_t gstat)
{
	uint32_t sid;
	uint32_t sstat;

	sid = gstat & SEC_GSTAT_SID;
	sstat = bfin_read_SEC_SSTAT(sid);

}

void handle_sec_fault(unsigned int irq, struct irq_desc *desc)
{
	uint32_t sec_gstat;

	raw_spin_lock(&desc->lock);

	sec_gstat = bfin_read32(SEC_GSTAT);
	if (sec_gstat & SEC_GSTAT_ERR) {

		switch (sec_gstat & SEC_GSTAT_ERRC) {
		case 0:
			handle_sec_sfi_fault(sec_gstat);
			break;
		case SEC_GSTAT_SCIERR:
			handle_sec_sci_fault(sec_gstat);
			break;
		case SEC_GSTAT_SSIERR:
			handle_sec_ssi_fault(sec_gstat);
			break;
		}


	}

	raw_spin_unlock(&desc->lock);

	handle_fasteoi_irq(irq, desc);
}

void handle_core_fault(unsigned int irq, struct irq_desc *desc)
{
	struct pt_regs *fp = get_irq_regs();

	raw_spin_lock(&desc->lock);

	switch (irq) {
	case IRQ_C0_DBL_FAULT:
		double_fault_c(fp);
		break;
	case IRQ_C0_HW_ERR:
		dump_bfin_process(fp);
		dump_bfin_mem(fp);
		show_regs(fp);
		printk(KERN_NOTICE "Kernel Stack\n");
		show_stack(current, NULL);
		print_modules();
		panic("Core 0 hardware error");
		break;
	case IRQ_C0_NMI_L1_PARITY_ERR:
		panic("Core 0 NMI L1 parity error");
		break;
	default:
		panic("Core 1 fault %d occurs unexpectedly", irq);
	}

	raw_spin_unlock(&desc->lock);
}
#endif /* SEC_GCTL */

static struct irq_chip bfin_core_irqchip = {
	.name = "CORE",
	.irq_mask = bfin_core_mask_irq,
	.irq_unmask = bfin_core_unmask_irq,
};

#ifndef SEC_GCTL
static struct irq_chip bfin_internal_irqchip = {
	.name = "INTN",
	.irq_mask = bfin_internal_mask_irq_chip,
	.irq_unmask = bfin_internal_unmask_irq_chip,
	.irq_disable = bfin_internal_mask_irq_chip,
	.irq_enable = bfin_internal_unmask_irq_chip,
#ifdef CONFIG_SMP
	.irq_set_affinity = bfin_internal_set_affinity,
#endif
	.irq_set_wake = bfin_internal_set_wake_chip,
};
#else
static struct irq_chip bfin_sec_irqchip = {
	.name = "SEC",
	.irq_mask_ack = bfin_sec_mask_ack_irq,
	.irq_mask = bfin_sec_mask_ack_irq,
	.irq_unmask = bfin_sec_unmask_irq,
	.irq_eoi = bfin_sec_unmask_irq,
	.irq_disable = bfin_sec_disable,
	.irq_enable = bfin_sec_enable,
};
#endif

void bfin_handle_irq(unsigned irq)
{
#ifdef CONFIG_IPIPE
	struct pt_regs regs;    /* Contents not used. */
	ipipe_trace_irq_entry(irq);
	__ipipe_handle_irq(irq, &regs);
	ipipe_trace_irq_exit(irq);
#else /* !CONFIG_IPIPE */
	generic_handle_irq(irq);
#endif  /* !CONFIG_IPIPE */
}

#if defined(CONFIG_BFIN_MAC) || defined(CONFIG_BFIN_MAC_MODULE)
static int mac_stat_int_mask;

static void bfin_mac_status_ack_irq(unsigned int irq)
{
	switch (irq) {
	case IRQ_MAC_MMCINT:
		bfin_write_EMAC_MMC_TIRQS(
			bfin_read_EMAC_MMC_TIRQE() &
			bfin_read_EMAC_MMC_TIRQS());
		bfin_write_EMAC_MMC_RIRQS(
			bfin_read_EMAC_MMC_RIRQE() &
			bfin_read_EMAC_MMC_RIRQS());
		break;
	case IRQ_MAC_RXFSINT:
		bfin_write_EMAC_RX_STKY(
			bfin_read_EMAC_RX_IRQE() &
			bfin_read_EMAC_RX_STKY());
		break;
	case IRQ_MAC_TXFSINT:
		bfin_write_EMAC_TX_STKY(
			bfin_read_EMAC_TX_IRQE() &
			bfin_read_EMAC_TX_STKY());
		break;
	case IRQ_MAC_WAKEDET:
		 bfin_write_EMAC_WKUP_CTL(
			bfin_read_EMAC_WKUP_CTL() | MPKS | RWKS);
		break;
	default:
		/* These bits are W1C */
		bfin_write_EMAC_SYSTAT(1L << (irq - IRQ_MAC_PHYINT));
		break;
	}
}

static void bfin_mac_status_mask_irq(struct irq_data *d)
{
	unsigned int irq = d->irq;

	mac_stat_int_mask &= ~(1L << (irq - IRQ_MAC_PHYINT));
#ifdef BF537_FAMILY
	switch (irq) {
	case IRQ_MAC_PHYINT:
		bfin_write_EMAC_SYSCTL(bfin_read_EMAC_SYSCTL() & ~PHYIE);
		break;
	default:
		break;
	}
#else
	if (!mac_stat_int_mask)
		bfin_internal_mask_irq(IRQ_MAC_ERROR);
#endif
	bfin_mac_status_ack_irq(irq);
}

static void bfin_mac_status_unmask_irq(struct irq_data *d)
{
	unsigned int irq = d->irq;

#ifdef BF537_FAMILY
	switch (irq) {
	case IRQ_MAC_PHYINT:
		bfin_write_EMAC_SYSCTL(bfin_read_EMAC_SYSCTL() | PHYIE);
		break;
	default:
		break;
	}
#else
	if (!mac_stat_int_mask)
		bfin_internal_unmask_irq(IRQ_MAC_ERROR);
#endif
	mac_stat_int_mask |= 1L << (irq - IRQ_MAC_PHYINT);
}

#ifdef CONFIG_PM
int bfin_mac_status_set_wake(struct irq_data *d, unsigned int state)
{
#ifdef BF537_FAMILY
	return bfin_internal_set_wake(IRQ_GENERIC_ERROR, state);
#else
	return bfin_internal_set_wake(IRQ_MAC_ERROR, state);
#endif
}
#else
# define bfin_mac_status_set_wake NULL
#endif

static struct irq_chip bfin_mac_status_irqchip = {
	.name = "MACST",
	.irq_mask = bfin_mac_status_mask_irq,
	.irq_unmask = bfin_mac_status_unmask_irq,
	.irq_set_wake = bfin_mac_status_set_wake,
};

void bfin_demux_mac_status_irq(unsigned int int_err_irq,
			       struct irq_desc *inta_desc)
{
	int i, irq = 0;
	u32 status = bfin_read_EMAC_SYSTAT();

	for (i = 0; i <= (IRQ_MAC_STMDONE - IRQ_MAC_PHYINT); i++)
		if (status & (1L << i)) {
			irq = IRQ_MAC_PHYINT + i;
			break;
		}

	if (irq) {
		if (mac_stat_int_mask & (1L << (irq - IRQ_MAC_PHYINT))) {
			bfin_handle_irq(irq);
		} else {
			bfin_mac_status_ack_irq(irq);
			pr_debug("IRQ %d:"
					" MASKED MAC ERROR INTERRUPT ASSERTED\n",
					irq);
		}
	} else
		printk(KERN_ERR
				"%s : %s : LINE %d :\nIRQ ?: MAC ERROR"
				" INTERRUPT ASSERTED BUT NO SOURCE FOUND"
				"(EMAC_SYSTAT=0x%X)\n",
				__func__, __FILE__, __LINE__, status);
}
#endif

static inline void bfin_set_irq_handler(unsigned irq, irq_flow_handler_t handle)
{
#ifdef CONFIG_IPIPE
	handle = handle_level_irq;
#endif
	__irq_set_handler_locked(irq, handle);
}

static DECLARE_BITMAP(gpio_enabled, MAX_BLACKFIN_GPIOS);
extern void bfin_gpio_irq_prepare(unsigned gpio);

#if !BFIN_GPIO_PINT

static void bfin_gpio_ack_irq(struct irq_data *d)
{
	/* AFAIK ack_irq in case mask_ack is provided
	 * get's only called for edge sense irqs
	 */
	set_gpio_data(irq_to_gpio(d->irq), 0);
}

static void bfin_gpio_mask_ack_irq(struct irq_data *d)
{
	unsigned int irq = d->irq;
	u32 gpionr = irq_to_gpio(irq);

	if (!irqd_is_level_type(d))
		set_gpio_data(gpionr, 0);

	set_gpio_maska(gpionr, 0);
}

static void bfin_gpio_mask_irq(struct irq_data *d)
{
	set_gpio_maska(irq_to_gpio(d->irq), 0);
}

static void bfin_gpio_unmask_irq(struct irq_data *d)
{
	set_gpio_maska(irq_to_gpio(d->irq), 1);
}

static unsigned int bfin_gpio_irq_startup(struct irq_data *d)
{
	u32 gpionr = irq_to_gpio(d->irq);

	if (__test_and_set_bit(gpionr, gpio_enabled))
		bfin_gpio_irq_prepare(gpionr);

	bfin_gpio_unmask_irq(d);

	return 0;
}

static void bfin_gpio_irq_shutdown(struct irq_data *d)
{
	u32 gpionr = irq_to_gpio(d->irq);

	bfin_gpio_mask_irq(d);
	__clear_bit(gpionr, gpio_enabled);
	bfin_gpio_irq_free(gpionr);
}

static int bfin_gpio_irq_type(struct irq_data *d, unsigned int type)
{
	unsigned int irq = d->irq;
	int ret;
	char buf[16];
	u32 gpionr = irq_to_gpio(irq);

	if (type == IRQ_TYPE_PROBE) {
		/* only probe unenabled GPIO interrupt lines */
		if (test_bit(gpionr, gpio_enabled))
			return 0;
		type = IRQ_TYPE_EDGE_RISING | IRQ_TYPE_EDGE_FALLING;
	}

	if (type & (IRQ_TYPE_EDGE_RISING | IRQ_TYPE_EDGE_FALLING |
		    IRQ_TYPE_LEVEL_HIGH | IRQ_TYPE_LEVEL_LOW)) {

		snprintf(buf, 16, "gpio-irq%d", irq);
		ret = bfin_gpio_irq_request(gpionr, buf);
		if (ret)
			return ret;

		if (__test_and_set_bit(gpionr, gpio_enabled))
			bfin_gpio_irq_prepare(gpionr);

	} else {
		__clear_bit(gpionr, gpio_enabled);
		return 0;
	}

	set_gpio_inen(gpionr, 0);
	set_gpio_dir(gpionr, 0);

	if ((type & (IRQ_TYPE_EDGE_RISING | IRQ_TYPE_EDGE_FALLING))
	    == (IRQ_TYPE_EDGE_RISING | IRQ_TYPE_EDGE_FALLING))
		set_gpio_both(gpionr, 1);
	else
		set_gpio_both(gpionr, 0);

	if ((type & (IRQ_TYPE_EDGE_FALLING | IRQ_TYPE_LEVEL_LOW)))
		set_gpio_polar(gpionr, 1);	/* low or falling edge denoted by one */
	else
		set_gpio_polar(gpionr, 0);	/* high or rising edge denoted by zero */

	if (type & (IRQ_TYPE_EDGE_RISING | IRQ_TYPE_EDGE_FALLING)) {
		set_gpio_edge(gpionr, 1);
		set_gpio_inen(gpionr, 1);
		set_gpio_data(gpionr, 0);

	} else {
		set_gpio_edge(gpionr, 0);
		set_gpio_inen(gpionr, 1);
	}

	if (type & (IRQ_TYPE_EDGE_RISING | IRQ_TYPE_EDGE_FALLING))
		bfin_set_irq_handler(irq, handle_edge_irq);
	else
		bfin_set_irq_handler(irq, handle_level_irq);

	return 0;
}

#ifdef CONFIG_PM
static int bfin_gpio_set_wake(struct irq_data *d, unsigned int state)
{
	return gpio_pm_wakeup_ctrl(irq_to_gpio(d->irq), state);
}
#else
# define bfin_gpio_set_wake NULL
#endif

static void bfin_demux_gpio_block(unsigned int irq)
{
	unsigned int gpio, mask;

	gpio = irq_to_gpio(irq);
	mask = get_gpiop_data(gpio) & get_gpiop_maska(gpio);

	while (mask) {
		if (mask & 1)
			bfin_handle_irq(irq);
		irq++;
		mask >>= 1;
	}
}

void bfin_demux_gpio_irq(unsigned int inta_irq,
			struct irq_desc *desc)
{
	unsigned int irq;

	switch (inta_irq) {
#if defined(BF537_FAMILY)
	case IRQ_PF_INTA_PG_INTA:
		bfin_demux_gpio_block(IRQ_PF0);
		irq = IRQ_PG0;
		break;
	case IRQ_PH_INTA_MAC_RX:
		irq = IRQ_PH0;
		break;
#elif defined(BF533_FAMILY)
	case IRQ_PROG_INTA:
		irq = IRQ_PF0;
		break;
#elif defined(BF538_FAMILY)
	case IRQ_PORTF_INTA:
		irq = IRQ_PF0;
		break;
#elif defined(CONFIG_BF52x) || defined(CONFIG_BF51x)
	case IRQ_PORTF_INTA:
		irq = IRQ_PF0;
		break;
	case IRQ_PORTG_INTA:
		irq = IRQ_PG0;
		break;
	case IRQ_PORTH_INTA:
		irq = IRQ_PH0;
		break;
#elif defined(CONFIG_BF561)
	case IRQ_PROG0_INTA:
		irq = IRQ_PF0;
		break;
	case IRQ_PROG1_INTA:
		irq = IRQ_PF16;
		break;
	case IRQ_PROG2_INTA:
		irq = IRQ_PF32;
		break;
#endif
	default:
		BUG();
		return;
	}

	bfin_demux_gpio_block(irq);
}

#else

#define NR_PINT_BITS		32
#define IRQ_NOT_AVAIL		0xFF

#define PINT_2_BANK(x)		((x) >> 5)
#define PINT_2_BIT(x)		((x) & 0x1F)
#define PINT_BIT(x)		(1 << (PINT_2_BIT(x)))

static unsigned char irq2pint_lut[NR_PINTS];
static unsigned char pint2irq_lut[NR_PINT_SYS_IRQS * NR_PINT_BITS];

static struct bfin_pint_regs * const pint[NR_PINT_SYS_IRQS] = {
	(struct bfin_pint_regs *)PINT0_MASK_SET,
	(struct bfin_pint_regs *)PINT1_MASK_SET,
	(struct bfin_pint_regs *)PINT2_MASK_SET,
	(struct bfin_pint_regs *)PINT3_MASK_SET,
#ifdef CONFIG_BF60x
	(struct bfin_pint_regs *)PINT4_MASK_SET,
	(struct bfin_pint_regs *)PINT5_MASK_SET,
#endif
};

inline unsigned int get_irq_base(u32 bank, u8 bmap)
{
	unsigned int irq_base;

#ifndef CONFIG_BF60x
	if (bank < 2) {		/*PA-PB */
		irq_base = IRQ_PA0 + bmap * 16;
	} else {		/*PC-PJ */
		irq_base = IRQ_PC0 + bmap * 16;
	}
#else
	irq_base = IRQ_PA0 + bank * 16 + bmap * 16;
#endif
	return irq_base;
}

	/* Whenever PINTx_ASSIGN is altered init_pint_lut() must be executed! */
void init_pint_lut(void)
{
	u16 bank, bit, irq_base, bit_pos;
	u32 pint_assign;
	u8 bmap;

	memset(irq2pint_lut, IRQ_NOT_AVAIL, sizeof(irq2pint_lut));

	for (bank = 0; bank < NR_PINT_SYS_IRQS; bank++) {

		pint_assign = pint[bank]->assign;

		for (bit = 0; bit < NR_PINT_BITS; bit++) {

			bmap = (pint_assign >> ((bit / 8) * 8)) & 0xFF;

			irq_base = get_irq_base(bank, bmap);

			irq_base += (bit % 8) + ((bit / 8) & 1 ? 8 : 0);
			bit_pos = bit + bank * NR_PINT_BITS;

			pint2irq_lut[bit_pos] = irq_base - SYS_IRQS;
			irq2pint_lut[irq_base - SYS_IRQS] = bit_pos;
		}
	}
}

static void bfin_gpio_ack_irq(struct irq_data *d)
{
	u32 pint_val = irq2pint_lut[d->irq - SYS_IRQS];
	u32 pintbit = PINT_BIT(pint_val);
	u32 bank = PINT_2_BANK(pint_val);

	if (irqd_get_trigger_type(d) == IRQ_TYPE_EDGE_BOTH) {
		if (pint[bank]->invert_set & pintbit)
			pint[bank]->invert_clear = pintbit;
		else
			pint[bank]->invert_set = pintbit;
	}
	pint[bank]->request = pintbit;

}

static void bfin_gpio_mask_ack_irq(struct irq_data *d)
{
	u32 pint_val = irq2pint_lut[d->irq - SYS_IRQS];
	u32 pintbit = PINT_BIT(pint_val);
	u32 bank = PINT_2_BANK(pint_val);

	if (irqd_get_trigger_type(d) == IRQ_TYPE_EDGE_BOTH) {
		if (pint[bank]->invert_set & pintbit)
			pint[bank]->invert_clear = pintbit;
		else
			pint[bank]->invert_set = pintbit;
	}

	pint[bank]->request = pintbit;
	pint[bank]->mask_clear = pintbit;
}

static void bfin_gpio_mask_irq(struct irq_data *d)
{
	u32 pint_val = irq2pint_lut[d->irq - SYS_IRQS];

	pint[PINT_2_BANK(pint_val)]->mask_clear = PINT_BIT(pint_val);
}

static void bfin_gpio_unmask_irq(struct irq_data *d)
{
	u32 pint_val = irq2pint_lut[d->irq - SYS_IRQS];
	u32 pintbit = PINT_BIT(pint_val);
	u32 bank = PINT_2_BANK(pint_val);

	pint[bank]->mask_set = pintbit;
}

static unsigned int bfin_gpio_irq_startup(struct irq_data *d)
{
	unsigned int irq = d->irq;
	u32 gpionr = irq_to_gpio(irq);
	u32 pint_val = irq2pint_lut[irq - SYS_IRQS];

	if (pint_val == IRQ_NOT_AVAIL) {
		printk(KERN_ERR
		"GPIO IRQ %d :Not in PINT Assign table "
		"Reconfigure Interrupt to Port Assignemt\n", irq);
		return -ENODEV;
	}

	if (__test_and_set_bit(gpionr, gpio_enabled))
		bfin_gpio_irq_prepare(gpionr);

	bfin_gpio_unmask_irq(d);

	return 0;
}

static void bfin_gpio_irq_shutdown(struct irq_data *d)
{
	u32 gpionr = irq_to_gpio(d->irq);

	bfin_gpio_mask_irq(d);
	__clear_bit(gpionr, gpio_enabled);
	bfin_gpio_irq_free(gpionr);
}

static int bfin_gpio_irq_type(struct irq_data *d, unsigned int type)
{
	unsigned int irq = d->irq;
	int ret;
	char buf[16];
	u32 gpionr = irq_to_gpio(irq);
	u32 pint_val = irq2pint_lut[irq - SYS_IRQS];
	u32 pintbit = PINT_BIT(pint_val);
	u32 bank = PINT_2_BANK(pint_val);

	if (pint_val == IRQ_NOT_AVAIL)
		return -ENODEV;

	if (type == IRQ_TYPE_PROBE) {
		/* only probe unenabled GPIO interrupt lines */
		if (test_bit(gpionr, gpio_enabled))
			return 0;
		type = IRQ_TYPE_EDGE_RISING | IRQ_TYPE_EDGE_FALLING;
	}

	if (type & (IRQ_TYPE_EDGE_RISING | IRQ_TYPE_EDGE_FALLING |
		    IRQ_TYPE_LEVEL_HIGH | IRQ_TYPE_LEVEL_LOW)) {

		snprintf(buf, 16, "gpio-irq%d", irq);
		ret = bfin_gpio_irq_request(gpionr, buf);
		if (ret)
			return ret;

		if (__test_and_set_bit(gpionr, gpio_enabled))
			bfin_gpio_irq_prepare(gpionr);

	} else {
		__clear_bit(gpionr, gpio_enabled);
		return 0;
	}

	if ((type & (IRQ_TYPE_EDGE_FALLING | IRQ_TYPE_LEVEL_LOW)))
		pint[bank]->invert_set = pintbit;	/* low or falling edge denoted by one */
	else
		pint[bank]->invert_clear = pintbit;	/* high or rising edge denoted by zero */

	if ((type & (IRQ_TYPE_EDGE_RISING | IRQ_TYPE_EDGE_FALLING))
	    == (IRQ_TYPE_EDGE_RISING | IRQ_TYPE_EDGE_FALLING)) {
		if (gpio_get_value(gpionr))
			pint[bank]->invert_set = pintbit;
		else
			pint[bank]->invert_clear = pintbit;
	}

	if (type & (IRQ_TYPE_EDGE_RISING | IRQ_TYPE_EDGE_FALLING)) {
		pint[bank]->edge_set = pintbit;
		bfin_set_irq_handler(irq, handle_edge_irq);
	} else {
		pint[bank]->edge_clear = pintbit;
		bfin_set_irq_handler(irq, handle_level_irq);
	}

	return 0;
}

#ifdef CONFIG_PM
static struct bfin_pm_pint_save save_pint_reg[NR_PINT_SYS_IRQS];
static u32 save_pint_sec_ctl[NR_PINT_SYS_IRQS];

static int bfin_gpio_set_wake(struct irq_data *d, unsigned int state)
{
	u32 pint_irq;
	u32 pint_val = irq2pint_lut[d->irq - SYS_IRQS];
	u32 bank = PINT_2_BANK(pint_val);

	switch (bank) {
	case 0:
		pint_irq = IRQ_PINT0;
		break;
	case 2:
		pint_irq = IRQ_PINT2;
		break;
	case 3:
		pint_irq = IRQ_PINT3;
		break;
	case 1:
		pint_irq = IRQ_PINT1;
		break;
#ifdef CONFIG_BF60x
	case 4:
		pint_irq = IRQ_PINT4;
		break;
	case 5:
		pint_irq = IRQ_PINT5;
		break;
#endif
	default:
		return -EINVAL;
	}

#ifndef SEC_GCTL
	bfin_internal_set_wake(pint_irq, state);
#endif

	return 0;
}

void bfin_pint_suspend(void)
{
	u32 bank;

	for (bank = 0; bank < NR_PINT_SYS_IRQS; bank++) {
		save_pint_reg[bank].mask_set = pint[bank]->mask_set;
		save_pint_reg[bank].assign = pint[bank]->assign;
		save_pint_reg[bank].edge_set = pint[bank]->edge_set;
		save_pint_reg[bank].invert_set = pint[bank]->invert_set;
	}
}

void bfin_pint_resume(void)
{
	u32 bank;

	for (bank = 0; bank < NR_PINT_SYS_IRQS; bank++) {
		pint[bank]->mask_set = save_pint_reg[bank].mask_set;
		pint[bank]->assign = save_pint_reg[bank].assign;
		pint[bank]->edge_set = save_pint_reg[bank].edge_set;
		pint[bank]->invert_set = save_pint_reg[bank].invert_set;
	}
}

#ifdef SEC_GCTL
static int sec_suspend(void)
{
	u32 bank;

	for (bank = 0; bank < NR_PINT_SYS_IRQS; bank++)
		save_pint_sec_ctl[bank] = bfin_read_SEC_SCTL(bank + BFIN_SYSIRQ(IRQ_PINT0));
	return 0;
}

static void sec_resume(void)
{
	u32 bank;

	bfin_write_SEC_SCI(0, SEC_CCTL, SEC_CCTL_RESET);
	udelay(100);
	bfin_write_SEC_GCTL(SEC_GCTL_EN);
	bfin_write_SEC_SCI(0, SEC_CCTL, SEC_CCTL_EN | SEC_CCTL_NMI_EN);

	for (bank = 0; bank < NR_PINT_SYS_IRQS; bank++)
		bfin_write_SEC_SCTL(bank + BFIN_SYSIRQ(IRQ_PINT0), save_pint_sec_ctl[bank]);
}

static struct syscore_ops sec_pm_syscore_ops = {
	.suspend = sec_suspend,
	.resume = sec_resume,
};

#endif
#else
# define bfin_gpio_set_wake NULL
#endif

void bfin_demux_gpio_irq(unsigned int inta_irq,
			struct irq_desc *desc)
{
	u32 bank, pint_val;
	u32 request, irq;
	u32 level_mask;
	int umask = 0;
	struct irq_chip *chip = irq_desc_get_chip(desc);

	if (chip->irq_mask_ack) {
		chip->irq_mask_ack(&desc->irq_data);
	} else {
		chip->irq_mask(&desc->irq_data);
		if (chip->irq_ack)
			chip->irq_ack(&desc->irq_data);
	}

	switch (inta_irq) {
	case IRQ_PINT0:
		bank = 0;
		break;
	case IRQ_PINT2:
		bank = 2;
		break;
	case IRQ_PINT3:
		bank = 3;
		break;
	case IRQ_PINT1:
		bank = 1;
		break;
#ifdef CONFIG_BF60x
	case IRQ_PINT4:
		bank = 4;
		break;
	case IRQ_PINT5:
		bank = 5;
		break;
#endif
	default:
		return;
	}

	pint_val = bank * NR_PINT_BITS;

	request = pint[bank]->request;

	level_mask = pint[bank]->edge_set & request;

	while (request) {
		if (request & 1) {
			irq = pint2irq_lut[pint_val] + SYS_IRQS;
			if (level_mask & PINT_BIT(pint_val)) {
				umask = 1;
				chip->irq_unmask(&desc->irq_data);
			}
			bfin_handle_irq(irq);
		}
		pint_val++;
		request >>= 1;
	}

	if (!umask)
		chip->irq_unmask(&desc->irq_data);
}
#endif

static struct irq_chip bfin_gpio_irqchip = {
	.name = "GPIO",
	.irq_ack = bfin_gpio_ack_irq,
	.irq_mask = bfin_gpio_mask_irq,
	.irq_mask_ack = bfin_gpio_mask_ack_irq,
	.irq_unmask = bfin_gpio_unmask_irq,
	.irq_disable = bfin_gpio_mask_irq,
	.irq_enable = bfin_gpio_unmask_irq,
	.irq_set_type = bfin_gpio_irq_type,
	.irq_startup = bfin_gpio_irq_startup,
	.irq_shutdown = bfin_gpio_irq_shutdown,
	.irq_set_wake = bfin_gpio_set_wake,
};

void init_exception_vectors(void)
{
	/* cannot program in software:
	 * evt0 - emulation (jtag)
	 * evt1 - reset
	 */
	bfin_write_EVT2(evt_nmi);
	bfin_write_EVT3(trap);
	bfin_write_EVT5(evt_ivhw);
	bfin_write_EVT6(evt_timer);
	bfin_write_EVT7(evt_evt7);
	bfin_write_EVT8(evt_evt8);
	bfin_write_EVT9(evt_evt9);
	bfin_write_EVT10(evt_evt10);
	bfin_write_EVT11(evt_evt11);
	bfin_write_EVT12(evt_evt12);
	bfin_write_EVT13(evt_evt13);
	bfin_write_EVT14(evt_evt14);
	bfin_write_EVT15(evt_system_call);
	CSYNC();
}

#ifndef SEC_GCTL
/*
 * This function should be called during kernel startup to initialize
 * the BFin IRQ handling routines.
 */

int __init init_arch_irq(void)
{
	int irq;
	unsigned long ilat = 0;

	/*  Disable all the peripheral intrs  - page 4-29 HW Ref manual */
#ifdef SIC_IMASK0
	bfin_write_SIC_IMASK0(SIC_UNMASK_ALL);
	bfin_write_SIC_IMASK1(SIC_UNMASK_ALL);
# ifdef SIC_IMASK2
	bfin_write_SIC_IMASK2(SIC_UNMASK_ALL);
# endif
# if defined(CONFIG_SMP) || defined(CONFIG_ICC)
	bfin_write_SICB_IMASK0(SIC_UNMASK_ALL);
	bfin_write_SICB_IMASK1(SIC_UNMASK_ALL);
# endif
#else
	bfin_write_SIC_IMASK(SIC_UNMASK_ALL);
#endif

	local_irq_disable();

#if BFIN_GPIO_PINT
# ifdef CONFIG_PINTx_REASSIGN
	pint[0]->assign = CONFIG_PINT0_ASSIGN;
	pint[1]->assign = CONFIG_PINT1_ASSIGN;
	pint[2]->assign = CONFIG_PINT2_ASSIGN;
	pint[3]->assign = CONFIG_PINT3_ASSIGN;
# endif
	/* Whenever PINTx_ASSIGN is altered init_pint_lut() must be executed! */
	init_pint_lut();
#endif

	for (irq = 0; irq <= SYS_IRQS; irq++) {
		if (irq <= IRQ_CORETMR)
			irq_set_chip(irq, &bfin_core_irqchip);
		else
			irq_set_chip(irq, &bfin_internal_irqchip);

		switch (irq) {
#if BFIN_GPIO_PINT
		case IRQ_PINT0:
		case IRQ_PINT1:
		case IRQ_PINT2:
		case IRQ_PINT3:
#elif defined(BF537_FAMILY)
		case IRQ_PH_INTA_MAC_RX:
		case IRQ_PF_INTA_PG_INTA:
#elif defined(BF533_FAMILY)
		case IRQ_PROG_INTA:
#elif defined(CONFIG_BF52x) || defined(CONFIG_BF51x)
		case IRQ_PORTF_INTA:
		case IRQ_PORTG_INTA:
		case IRQ_PORTH_INTA:
#elif defined(CONFIG_BF561)
		case IRQ_PROG0_INTA:
		case IRQ_PROG1_INTA:
		case IRQ_PROG2_INTA:
#elif defined(BF538_FAMILY)
		case IRQ_PORTF_INTA:
#endif
			irq_set_chained_handler(irq, bfin_demux_gpio_irq);
			break;
#if defined(CONFIG_BFIN_MAC) || defined(CONFIG_BFIN_MAC_MODULE)
		case IRQ_MAC_ERROR:
			irq_set_chained_handler(irq,
						bfin_demux_mac_status_irq);
			break;
#endif
#if defined(CONFIG_SMP) || defined(CONFIG_ICC)
		case IRQ_SUPPLE_0:
		case IRQ_SUPPLE_1:
			irq_set_handler(irq, handle_percpu_irq);
			break;
#endif

#ifdef CONFIG_TICKSOURCE_CORETMR
		case IRQ_CORETMR:
# ifdef CONFIG_SMP
			irq_set_handler(irq, handle_percpu_irq);
# else
			irq_set_handler(irq, handle_simple_irq);
# endif
			break;
#endif

#ifdef CONFIG_TICKSOURCE_GPTMR0
		case IRQ_TIMER0:
			irq_set_handler(irq, handle_simple_irq);
			break;
#endif

		default:
#ifdef CONFIG_IPIPE
			irq_set_handler(irq, handle_level_irq);
#else
			irq_set_handler(irq, handle_simple_irq);
#endif
			break;
		}
	}

	init_mach_irq();

#if (defined(CONFIG_BFIN_MAC) || defined(CONFIG_BFIN_MAC_MODULE))
	for (irq = IRQ_MAC_PHYINT; irq <= IRQ_MAC_STMDONE; irq++)
		irq_set_chip_and_handler(irq, &bfin_mac_status_irqchip,
					 handle_level_irq);
#endif
	/* if configured as edge, then will be changed to do_edge_IRQ */
	for (irq = GPIO_IRQ_BASE;
		irq < (GPIO_IRQ_BASE + MAX_BLACKFIN_GPIOS); irq++)
		irq_set_chip_and_handler(irq, &bfin_gpio_irqchip,
					 handle_level_irq);
	bfin_write_IMASK(0);
	CSYNC();
	ilat = bfin_read_ILAT();
	CSYNC();
	bfin_write_ILAT(ilat);
	CSYNC();

	printk(KERN_INFO "Configuring Blackfin Priority Driven Interrupts\n");
	/* IMASK=xxx is equivalent to STI xx or bfin_irq_flags=xx,
	 * local_irq_enable()
	 */
	program_IAR();
	/* Therefore it's better to setup IARs before interrupts enabled */
	search_IAR();

	/* Enable interrupts IVG7-15 */
	bfin_irq_flags |= IMASK_IVG15 |
		IMASK_IVG14 | IMASK_IVG13 | IMASK_IVG12 | IMASK_IVG11 |
		IMASK_IVG10 | IMASK_IVG9 | IMASK_IVG8 | IMASK_IVG7 | IMASK_IVGHW;


	/* This implicitly covers ANOMALY_05000171
	 * Boot-ROM code modifies SICA_IWRx wakeup registers
	 */
#ifdef SIC_IWR0
	bfin_write_SIC_IWR0(IWR_DISABLE_ALL);
# ifdef SIC_IWR1
	/* BF52x/BF51x system reset does not properly reset SIC_IWR1 which
	 * will screw up the bootrom as it relies on MDMA0/1 waking it
	 * up from IDLE instructions.  See this report for more info:
	 * http://blackfin.uclinux.org/gf/tracker/4323
	 */
	if (ANOMALY_05000435)
		bfin_write_SIC_IWR1(IWR_ENABLE(10) | IWR_ENABLE(11));
	else
		bfin_write_SIC_IWR1(IWR_DISABLE_ALL);
# endif
# ifdef SIC_IWR2
	bfin_write_SIC_IWR2(IWR_DISABLE_ALL);
# endif
#else
	bfin_write_SIC_IWR(IWR_DISABLE_ALL);
#endif
	return 0;
}

#ifdef CONFIG_DO_IRQ_L1
__attribute__((l1_text))
#endif
static int vec_to_irq(int vec)
{
	struct ivgx *ivg = ivg7_13[vec - IVG7].ifirst;
	struct ivgx *ivg_stop = ivg7_13[vec - IVG7].istop;
	unsigned long sic_status[3];
	if (likely(vec == EVT_IVTMR_P))
		return IRQ_CORETMR;
#ifdef SIC_ISR
	sic_status[0] = bfin_read_SIC_IMASK() & bfin_read_SIC_ISR();
#else
	if (smp_processor_id()) {
# ifdef SICB_ISR0
		/* This will be optimized out in UP mode. */
		sic_status[0] = bfin_read_SICB_ISR0() & bfin_read_SICB_IMASK0();
		sic_status[1] = bfin_read_SICB_ISR1() & bfin_read_SICB_IMASK1();
# endif
	} else {
		sic_status[0] = bfin_read_SIC_ISR0() & bfin_read_SIC_IMASK0();
		sic_status[1] = bfin_read_SIC_ISR1() & bfin_read_SIC_IMASK1();
	}
#endif
#ifdef SIC_ISR2
	sic_status[2] = bfin_read_SIC_ISR2() & bfin_read_SIC_IMASK2();
#endif

	for (;; ivg++) {
		if (ivg >= ivg_stop)
			return -1;
#ifdef SIC_ISR
		if (sic_status[0] & ivg->isrflag)
#else
		if (sic_status[(ivg->irqno - IVG7) / 32] & ivg->isrflag)
#endif
			return ivg->irqno;
	}
}

#else /* SEC_GCTL */

/*
 * This function should be called during kernel startup to initialize
 * the BFin IRQ handling routines.
 */

int __init init_arch_irq(void)
{
	int irq;
	unsigned long ilat = 0;

	bfin_write_SEC_GCTL(SEC_GCTL_RESET);

	local_irq_disable();

#if BFIN_GPIO_PINT
# ifdef CONFIG_PINTx_REASSIGN
	pint[0]->assign = CONFIG_PINT0_ASSIGN;
	pint[1]->assign = CONFIG_PINT1_ASSIGN;
	pint[2]->assign = CONFIG_PINT2_ASSIGN;
	pint[3]->assign = CONFIG_PINT3_ASSIGN;
	pint[4]->assign = CONFIG_PINT4_ASSIGN;
	pint[5]->assign = CONFIG_PINT5_ASSIGN;
# endif
	/* Whenever PINTx_ASSIGN is altered init_pint_lut() must be executed! */
	init_pint_lut();
#endif

	for (irq = 0; irq <= SYS_IRQS; irq++) {
		if (irq <= IRQ_CORETMR) {
			irq_set_chip_and_handler(irq, &bfin_core_irqchip,
				handle_simple_irq);
#if defined(CONFIG_TICKSOURCE_CORETMR) && defined(CONFIG_SMP)
			if (irq == IRQ_CORETMR)
				irq_set_handler(irq, handle_percpu_irq);
#endif
		} else if (irq >= BFIN_IRQ(21) && irq <= BFIN_IRQ(26)) {
			irq_set_chip(irq, &bfin_sec_irqchip);
			irq_set_chained_handler(irq, bfin_demux_gpio_irq);
		} else if (irq >= BFIN_IRQ(34) && irq <= BFIN_IRQ(37)) {
			irq_set_chip_and_handler(irq, &bfin_sec_irqchip,
				handle_percpu_irq);
		} else {
			irq_set_chip(irq, &bfin_sec_irqchip);
			if (irq == IRQ_SEC_ERR)
				irq_set_handler(irq, handle_sec_fault);
			else if (irq >= IRQ_C0_DBL_FAULT && irq < CORE_IRQS)
				irq_set_handler(irq, handle_core_fault);
			else
				irq_set_handler(irq, handle_fasteoi_irq);
			__irq_set_preflow_handler(irq, bfin_sec_preflow_handler);
		}
	}
	for (irq = GPIO_IRQ_BASE;
		irq < (GPIO_IRQ_BASE + MAX_BLACKFIN_GPIOS); irq++)
		irq_set_chip_and_handler(irq, &bfin_gpio_irqchip,
					handle_level_irq);

	bfin_write_IMASK(0);
	CSYNC();
	ilat = bfin_read_ILAT();
	CSYNC();
	bfin_write_ILAT(ilat);
	CSYNC();

	printk(KERN_INFO "Configuring Blackfin Priority Driven Interrupts\n");

	bfin_sec_set_priority(CONFIG_SEC_IRQ_PRIORITY_LEVELS, sec_int_priority);

	bfin_sec_set_priority(CONFIG_SEC_IRQ_PRIORITY_LEVELS, sec_int_priority);

	/* Enable interrupts IVG7-15 */
	bfin_irq_flags |= IMASK_IVG15 |
	    IMASK_IVG14 | IMASK_IVG13 | IMASK_IVG12 | IMASK_IVG11 |
	    IMASK_IVG10 | IMASK_IVG9 | IMASK_IVG8 | IMASK_IVG7 | IMASK_IVGHW;


	bfin_write_SEC_FCTL(SEC_FCTL_EN | SEC_FCTL_SYSRST_EN | SEC_FCTL_FLTIN_EN);
	bfin_sec_enable_sci(BFIN_SYSIRQ(IRQ_WATCH0));
	bfin_sec_enable_ssi(BFIN_SYSIRQ(IRQ_WATCH0));
	bfin_write_SEC_SCI(0, SEC_CCTL, SEC_CCTL_RESET);
	udelay(100);
	bfin_write_SEC_GCTL(SEC_GCTL_EN);
	bfin_write_SEC_SCI(0, SEC_CCTL, SEC_CCTL_EN | SEC_CCTL_NMI_EN);
	bfin_write_SEC_SCI(1, SEC_CCTL, SEC_CCTL_EN | SEC_CCTL_NMI_EN);

	init_software_driven_irq();

#ifdef CONFIG_PM
	register_syscore_ops(&sec_pm_syscore_ops);
#endif

	return 0;
}

#ifdef CONFIG_DO_IRQ_L1
__attribute__((l1_text))
#endif
static int vec_to_irq(int vec)
{
	if (likely(vec == EVT_IVTMR_P))
		return IRQ_CORETMR;

	return BFIN_IRQ(bfin_read_SEC_SCI(0, SEC_CSID));
}
#endif  /* SEC_GCTL */

#ifdef CONFIG_DO_IRQ_L1
__attribute__((l1_text))
#endif
void do_irq(int vec, struct pt_regs *fp)
{
	int irq = vec_to_irq(vec);
	if (irq == -1)
		return;
	asm_do_IRQ(irq, fp);
}

#ifdef CONFIG_IPIPE

int __ipipe_get_irq_priority(unsigned irq)
{
	int ient, prio;

	if (irq <= IRQ_CORETMR)
		return irq;

#ifdef SEC_GCTL
	if (irq >= BFIN_IRQ(0))
		return IVG11;
#else
	for (ient = 0; ient < NR_PERI_INTS; ient++) {
		struct ivgx *ivg = ivg_table + ient;
		if (ivg->irqno == irq) {
			for (prio = 0; prio <= IVG13-IVG7; prio++) {
				if (ivg7_13[prio].ifirst <= ivg &&
				    ivg7_13[prio].istop > ivg)
					return IVG7 + prio;
			}
		}
	}
#endif

	return IVG15;
}

/* Hw interrupts are disabled on entry (check SAVE_CONTEXT). */
#ifdef CONFIG_DO_IRQ_L1
__attribute__((l1_text))
#endif
asmlinkage int __ipipe_grab_irq(int vec, struct pt_regs *regs)
{
	struct ipipe_percpu_domain_data *p = ipipe_root_cpudom_ptr();
	struct ipipe_domain *this_domain = __ipipe_current_domain;
	int irq, s = 0;

	irq = vec_to_irq(vec);
	if (irq == -1)
		return 0;

	if (irq == IRQ_SYSTMR) {
#if !defined(CONFIG_GENERIC_CLOCKEVENTS) || defined(CONFIG_TICKSOURCE_GPTMR0)
		bfin_write_TIMER_STATUS(1); /* Latch TIMIL0 */
#endif
		/* This is basically what we need from the register frame. */
		__raw_get_cpu_var(__ipipe_tick_regs).ipend = regs->ipend;
		__raw_get_cpu_var(__ipipe_tick_regs).pc = regs->pc;
		if (this_domain != ipipe_root_domain)
			__raw_get_cpu_var(__ipipe_tick_regs).ipend &= ~0x10;
		else
			__raw_get_cpu_var(__ipipe_tick_regs).ipend |= 0x10;
	}

	/*
	 * We don't want Linux interrupt handlers to run at the
	 * current core priority level (i.e. < EVT15), since this
	 * might delay other interrupts handled by a high priority
	 * domain. Here is what we do instead:
	 *
	 * - we raise the SYNCDEFER bit to prevent
	 * __ipipe_handle_irq() to sync the pipeline for the root
	 * stage for the incoming interrupt. Upon return, that IRQ is
	 * pending in the interrupt log.
	 *
	 * - we raise the TIF_IRQ_SYNC bit for the current thread, so
	 * that _schedule_and_signal_from_int will eventually sync the
	 * pipeline from EVT15.
	 */
	if (this_domain == ipipe_root_domain) {
		s = __test_and_set_bit(IPIPE_SYNCDEFER_FLAG, &p->status);
		barrier();
	}

	ipipe_trace_irq_entry(irq);
	__ipipe_handle_irq(irq, regs);
	ipipe_trace_irq_exit(irq);

	if (user_mode(regs) &&
	    !ipipe_test_foreign_stack() &&
	    (current->ipipe_flags & PF_EVTRET) != 0) {
		/*
		 * Testing for user_regs() does NOT fully eliminate
		 * foreign stack contexts, because of the forged
		 * interrupt returns we do through
		 * __ipipe_call_irqtail. In that case, we might have
		 * preempted a foreign stack context in a high
		 * priority domain, with a single interrupt level now
		 * pending after the irqtail unwinding is done. In
		 * which case user_mode() is now true, and the event
		 * gets dispatched spuriously.
		 */
		current->ipipe_flags &= ~PF_EVTRET;
		__ipipe_dispatch_event(IPIPE_EVENT_RETURN, regs);
	}

	if (this_domain == ipipe_root_domain) {
		set_thread_flag(TIF_IRQ_SYNC);
		if (!s) {
			__clear_bit(IPIPE_SYNCDEFER_FLAG, &p->status);
			return !test_bit(IPIPE_STALL_FLAG, &p->status);
		}
	}

	return 0;
}

#endif /* CONFIG_IPIPE */
