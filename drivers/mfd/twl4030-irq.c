/*
 * twl4030-irq.c - TWL4030/TPS659x0 irq support
 *
 * Copyright (C) 2005-2006 Texas Instruments, Inc.
 *
 * Modifications to defer interrupt handling to a kernel thread:
 * Copyright (C) 2006 MontaVista Software, Inc.
 *
 * Based on tlv320aic23.c:
 * Copyright (c) by Kai Svahn <kai.svahn@nokia.com>
 *
 * Code cleanup and modifications to IRQ handler.
 * by syed khasim <x0khasim@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/export.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/irqdomain.h>
#include <linux/i2c/twl.h>

#include "twl-core.h"

/*
 * TWL4030 IRQ handling has two stages in hardware, and thus in software.
 * The Primary Interrupt Handler (PIH) stage exposes status bits saying
 * which Secondary Interrupt Handler (SIH) stage is raising an interrupt.
 * SIH modules are more traditional IRQ components, which support per-IRQ
 * enable/disable and trigger controls; they do most of the work.
 *
 * These chips are designed to support IRQ handling from two different
 * I2C masters.  Each has a dedicated IRQ line, and dedicated IRQ status
 * and mask registers in the PIH and SIH modules.
 *
 * We set up IRQs starting at a platform-specified base, always starting
 * with PIH and the SIH for PWR_INT and then usually adding GPIO:
 *	base + 0  .. base + 7	PIH
 *	base + 8  .. base + 15	SIH for PWR_INT
 *	base + 16 .. base + 33	SIH for GPIO
 */
#define TWL4030_CORE_NR_IRQS	8
#define TWL4030_PWR_NR_IRQS	8

/* PIH register offsets */
#define REG_PIH_ISR_P1			0x01
#define REG_PIH_ISR_P2			0x02
#define REG_PIH_SIR			0x03	/* for testing */

/* Linux could (eventually) use either IRQ line */
static int irq_line;

struct sih {
	char	name[8];
	u8	module;			/* module id */
	u8	control_offset;		/* for SIH_CTRL */
	bool	set_cor;

	u8	bits;			/* valid in isr/imr */
	u8	bytes_ixr;		/* bytelen of ISR/IMR/SIR */

	u8	edr_offset;
	u8	bytes_edr;		/* bytelen of EDR */

	u8	irq_lines;		/* number of supported irq lines */

	/* SIR ignored -- set interrupt, for testing only */
	struct sih_irq_data {
		u8	isr_offset;
		u8	imr_offset;
	} mask[2];
	/* + 2 bytes padding */
};

static const struct sih *sih_modules;
static int nr_sih_modules;

#define SIH_INITIALIZER(modname, nbits) \
	.module		= TWL4030_MODULE_ ## modname, \
	.control_offset = TWL4030_ ## modname ## _SIH_CTRL, \
	.bits		= nbits, \
	.bytes_ixr	= DIV_ROUND_UP(nbits, 8), \
	.edr_offset	= TWL4030_ ## modname ## _EDR, \
	.bytes_edr	= DIV_ROUND_UP((2*(nbits)), 8), \
	.irq_lines	= 2, \
	.mask = { { \
		.isr_offset	= TWL4030_ ## modname ## _ISR1, \
		.imr_offset	= TWL4030_ ## modname ## _IMR1, \
	}, \
	{ \
		.isr_offset	= TWL4030_ ## modname ## _ISR2, \
		.imr_offset	= TWL4030_ ## modname ## _IMR2, \
	}, },

/* register naming policies are inconsistent ... */
#define TWL4030_INT_PWR_EDR		TWL4030_INT_PWR_EDR1
#define TWL4030_MODULE_KEYPAD_KEYP	TWL4030_MODULE_KEYPAD
#define TWL4030_MODULE_INT_PWR		TWL4030_MODULE_INT


/*
 * Order in this table matches order in PIH_ISR.  That is,
 * BIT(n) in PIH_ISR is sih_modules[n].
 */
/* sih_modules_twl4030 is used both in twl4030 and twl5030 */
static const struct sih sih_modules_twl4030[6] = {
	[0] = {
		.name		= "gpio",
		.module		= TWL4030_MODULE_GPIO,
		.control_offset	= REG_GPIO_SIH_CTRL,
		.set_cor	= true,
		.bits		= TWL4030_GPIO_MAX,
		.bytes_ixr	= 3,
		/* Note: *all* of these IRQs default to no-trigger */
		.edr_offset	= REG_GPIO_EDR1,
		.bytes_edr	= 5,
		.irq_lines	= 2,
		.mask = { {
			.isr_offset	= REG_GPIO_ISR1A,
			.imr_offset	= REG_GPIO_IMR1A,
		}, {
			.isr_offset	= REG_GPIO_ISR1B,
			.imr_offset	= REG_GPIO_IMR1B,
		}, },
	},
	[1] = {
		.name		= "keypad",
		.set_cor	= true,
		SIH_INITIALIZER(KEYPAD_KEYP, 4)
	},
	[2] = {
		.name		= "bci",
		.module		= TWL4030_MODULE_INTERRUPTS,
		.control_offset	= TWL4030_INTERRUPTS_BCISIHCTRL,
		.set_cor	= true,
		.bits		= 12,
		.bytes_ixr	= 2,
		.edr_offset	= TWL4030_INTERRUPTS_BCIEDR1,
		/* Note: most of these IRQs default to no-trigger */
		.bytes_edr	= 3,
		.irq_lines	= 2,
		.mask = { {
			.isr_offset	= TWL4030_INTERRUPTS_BCIISR1A,
			.imr_offset	= TWL4030_INTERRUPTS_BCIIMR1A,
		}, {
			.isr_offset	= TWL4030_INTERRUPTS_BCIISR1B,
			.imr_offset	= TWL4030_INTERRUPTS_BCIIMR1B,
		}, },
	},
	[3] = {
		.name		= "madc",
		SIH_INITIALIZER(MADC, 4)
	},
	[4] = {
		/* USB doesn't use the same SIH organization */
		.name		= "usb",
	},
	[5] = {
		.name		= "power",
		.set_cor	= true,
		SIH_INITIALIZER(INT_PWR, 8)
	},
		/* there are no SIH modules #6 or #7 ... */
};

static const struct sih sih_modules_twl5031[8] = {
	[0] = {
		.name		= "gpio",
		.module		= TWL4030_MODULE_GPIO,
		.control_offset	= REG_GPIO_SIH_CTRL,
		.set_cor	= true,
		.bits		= TWL4030_GPIO_MAX,
		.bytes_ixr	= 3,
		/* Note: *all* of these IRQs default to no-trigger */
		.edr_offset	= REG_GPIO_EDR1,
		.bytes_edr	= 5,
		.irq_lines	= 2,
		.mask = { {
			.isr_offset	= REG_GPIO_ISR1A,
			.imr_offset	= REG_GPIO_IMR1A,
		}, {
			.isr_offset	= REG_GPIO_ISR1B,
			.imr_offset	= REG_GPIO_IMR1B,
		}, },
	},
	[1] = {
		.name		= "keypad",
		.set_cor	= true,
		SIH_INITIALIZER(KEYPAD_KEYP, 4)
	},
	[2] = {
		.name		= "bci",
		.module		= TWL5031_MODULE_INTERRUPTS,
		.control_offset	= TWL5031_INTERRUPTS_BCISIHCTRL,
		.bits		= 7,
		.bytes_ixr	= 1,
		.edr_offset	= TWL5031_INTERRUPTS_BCIEDR1,
		/* Note: most of these IRQs default to no-trigger */
		.bytes_edr	= 2,
		.irq_lines	= 2,
		.mask = { {
			.isr_offset	= TWL5031_INTERRUPTS_BCIISR1,
			.imr_offset	= TWL5031_INTERRUPTS_BCIIMR1,
		}, {
			.isr_offset	= TWL5031_INTERRUPTS_BCIISR2,
			.imr_offset	= TWL5031_INTERRUPTS_BCIIMR2,
		}, },
	},
	[3] = {
		.name		= "madc",
		SIH_INITIALIZER(MADC, 4)
	},
	[4] = {
		/* USB doesn't use the same SIH organization */
		.name		= "usb",
	},
	[5] = {
		.name		= "power",
		.set_cor	= true,
		SIH_INITIALIZER(INT_PWR, 8)
	},
	[6] = {
		/*
		 * ECI/DBI doesn't use the same SIH organization.
		 * For example, it supports only one interrupt output line.
		 * That is, the interrupts are seen on both INT1 and INT2 lines.
		 */
		.name		= "eci_dbi",
		.module		= TWL5031_MODULE_ACCESSORY,
		.bits		= 9,
		.bytes_ixr	= 2,
		.irq_lines	= 1,
		.mask = { {
			.isr_offset	= TWL5031_ACIIDR_LSB,
			.imr_offset	= TWL5031_ACIIMR_LSB,
		}, },

	},
	[7] = {
		/* Audio accessory */
		.name		= "audio",
		.module		= TWL5031_MODULE_ACCESSORY,
		.control_offset	= TWL5031_ACCSIHCTRL,
		.bits		= 2,
		.bytes_ixr	= 1,
		.edr_offset	= TWL5031_ACCEDR1,
		/* Note: most of these IRQs default to no-trigger */
		.bytes_edr	= 1,
		.irq_lines	= 2,
		.mask = { {
			.isr_offset	= TWL5031_ACCISR1,
			.imr_offset	= TWL5031_ACCIMR1,
		}, {
			.isr_offset	= TWL5031_ACCISR2,
			.imr_offset	= TWL5031_ACCIMR2,
		}, },
	},
};

#undef TWL4030_MODULE_KEYPAD_KEYP
#undef TWL4030_MODULE_INT_PWR
#undef TWL4030_INT_PWR_EDR

/*----------------------------------------------------------------------*/

static unsigned twl4030_irq_base;

/*
 * handle_twl4030_pih() is the desc->handle method for the twl4030 interrupt.
 * This is a chained interrupt, so there is no desc->action method for it.
 * Now we need to query the interrupt controller in the twl4030 to determine
 * which module is generating the interrupt request.  However, we can't do i2c
 * transactions in interrupt context, so we must defer that work to a kernel
 * thread.  All we do here is acknowledge and mask the interrupt and wakeup
 * the kernel thread.
 */
static irqreturn_t handle_twl4030_pih(int irq, void *devid)
{
	irqreturn_t	ret;
	u8		pih_isr;

	ret = twl_i2c_read_u8(TWL_MODULE_PIH, &pih_isr,
			      REG_PIH_ISR_P1);
	if (ret) {
		pr_warn("twl4030: I2C error %d reading PIH ISR\n", ret);
		return IRQ_NONE;
	}

	while (pih_isr) {
		unsigned long	pending = __ffs(pih_isr);
		unsigned int	irq;

		pih_isr &= ~BIT(pending);
		irq = pending + twl4030_irq_base;
		handle_nested_irq(irq);
	}

	return IRQ_HANDLED;
}

/*----------------------------------------------------------------------*/

/*
 * twl4030_init_sih_modules() ... start from a known state where no
 * IRQs will be coming in, and where we can quickly enable them then
 * handle them as they arrive.  Mask all IRQs: maybe init SIH_CTRL.
 *
 * NOTE:  we don't touch EDR registers here; they stay with hardware
 * defaults or whatever the last value was.  Note that when both EDR
 * bits for an IRQ are clear, that's as if its IMR bit is set...
 */
static int twl4030_init_sih_modules(unsigned line)
{
	const struct sih *sih;
	u8 buf[4];
	int i;
	int status;

	/* line 0 == int1_n signal; line 1 == int2_n signal */
	if (line > 1)
		return -EINVAL;

	irq_line = line;

	/* disable all interrupts on our line */
	memset(buf, 0xff, sizeof(buf));
	sih = sih_modules;
	for (i = 0; i < nr_sih_modules; i++, sih++) {
		/* skip USB -- it's funky */
		if (!sih->bytes_ixr)
			continue;

		/* Not all the SIH modules support multiple interrupt lines */
		if (sih->irq_lines <= line)
			continue;

		status = twl_i2c_write(sih->module, buf,
				sih->mask[line].imr_offset, sih->bytes_ixr);
		if (status < 0)
			pr_err("twl4030: err %d initializing %s %s\n",
					status, sih->name, "IMR");

		/*
		 * Maybe disable "exclusive" mode; buffer second pending irq;
		 * set Clear-On-Read (COR) bit.
		 *
		 * NOTE that sometimes COR polarity is documented as being
		 * inverted:  for MADC, COR=1 means "clear on write".
		 * And for PWR_INT it's not documented...
		 */
		if (sih->set_cor) {
			status = twl_i2c_write_u8(sih->module,
					TWL4030_SIH_CTRL_COR_MASK,
					sih->control_offset);
			if (status < 0)
				pr_err("twl4030: err %d initializing %s %s\n",
						status, sih->name, "SIH_CTRL");
		}
	}

	sih = sih_modules;
	for (i = 0; i < nr_sih_modules; i++, sih++) {
		u8 rxbuf[4];
		int j;

		/* skip USB */
		if (!sih->bytes_ixr)
			continue;

		/* Not all the SIH modules support multiple interrupt lines */
		if (sih->irq_lines <= line)
			continue;

		/*
		 * Clear pending interrupt status.  Either the read was
		 * enough, or we need to write those bits.  Repeat, in
		 * case an IRQ is pending (PENDDIS=0) ... that's not
		 * uncommon with PWR_INT.PWRON.
		 */
		for (j = 0; j < 2; j++) {
			status = twl_i2c_read(sih->module, rxbuf,
				sih->mask[line].isr_offset, sih->bytes_ixr);
			if (status < 0)
				pr_warn("twl4030: err %d initializing %s %s\n",
					status, sih->name, "ISR");

			if (!sih->set_cor) {
				status = twl_i2c_write(sih->module, buf,
					sih->mask[line].isr_offset,
					sih->bytes_ixr);
				if (status < 0)
					pr_warn("twl4030: write failed: %d\n",
						status);
			}
			/*
			 * else COR=1 means read sufficed.
			 * (for most SIH modules...)
			 */
		}
	}

	return 0;
}

static inline void activate_irq(int irq)
{
#ifdef CONFIG_ARM
	/*
	 * ARM requires an extra step to clear IRQ_NOREQUEST, which it
	 * sets on behalf of every irq_chip.  Also sets IRQ_NOPROBE.
	 */
	set_irq_flags(irq, IRQF_VALID);
#else
	/* same effect on other architectures */
	irq_set_noprobe(irq);
#endif
}

/*----------------------------------------------------------------------*/

struct sih_agent {
	int			irq_base;
	const struct sih	*sih;

	u32			imr;
	bool			imr_change_pending;

	u32			edge_change;

	struct mutex		irq_lock;
	char			*irq_name;
};

/*----------------------------------------------------------------------*/

/*
 * All irq_chip methods get issued from code holding irq_desc[irq].lock,
 * which can't perform the underlying I2C operations (because they sleep).
 * So we must hand them off to a thread (workqueue) and cope with asynch
 * completion, potentially including some re-ordering, of these requests.
 */

static void twl4030_sih_mask(struct irq_data *data)
{
	struct sih_agent *agent = irq_data_get_irq_chip_data(data);

	agent->imr |= BIT(data->irq - agent->irq_base);
	agent->imr_change_pending = true;
}

static void twl4030_sih_unmask(struct irq_data *data)
{
	struct sih_agent *agent = irq_data_get_irq_chip_data(data);

	agent->imr &= ~BIT(data->irq - agent->irq_base);
	agent->imr_change_pending = true;
}

static int twl4030_sih_set_type(struct irq_data *data, unsigned trigger)
{
	struct sih_agent *agent = irq_data_get_irq_chip_data(data);

	if (trigger & ~(IRQ_TYPE_EDGE_FALLING | IRQ_TYPE_EDGE_RISING))
		return -EINVAL;

	if (irqd_get_trigger_type(data) != trigger)
		agent->edge_change |= BIT(data->irq - agent->irq_base);

	return 0;
}

static void twl4030_sih_bus_lock(struct irq_data *data)
{
	struct sih_agent	*agent = irq_data_get_irq_chip_data(data);

	mutex_lock(&agent->irq_lock);
}

static void twl4030_sih_bus_sync_unlock(struct irq_data *data)
{
	struct sih_agent	*agent = irq_data_get_irq_chip_data(data);
	const struct sih	*sih = agent->sih;
	int			status;

	if (agent->imr_change_pending) {
		union {
			u32	word;
			u8	bytes[4];
		} imr;

		/* byte[0] gets overwritten as we write ... */
		imr.word = cpu_to_le32(agent->imr);
		agent->imr_change_pending = false;

		/* write the whole mask ... simpler than subsetting it */
		status = twl_i2c_write(sih->module, imr.bytes,
				sih->mask[irq_line].imr_offset,
				sih->bytes_ixr);
		if (status)
			pr_err("twl4030: %s, %s --> %d\n", __func__,
					"write", status);
	}

	if (agent->edge_change) {
		u32		edge_change;
		u8		bytes[6];

		edge_change = agent->edge_change;
		agent->edge_change = 0;

		/*
		 * Read, reserving first byte for write scratch.  Yes, this
		 * could be cached for some speedup ... but be careful about
		 * any processor on the other IRQ line, EDR registers are
		 * shared.
		 */
		status = twl_i2c_read(sih->module, bytes,
				sih->edr_offset, sih->bytes_edr);
		if (status) {
			pr_err("twl4030: %s, %s --> %d\n", __func__,
					"read", status);
			return;
		}

		/* Modify only the bits we know must change */
		while (edge_change) {
			int		i = fls(edge_change) - 1;
			int		byte = i >> 2;
			int		off = (i & 0x3) * 2;
			unsigned int	type;

			bytes[byte] &= ~(0x03 << off);

			type = irq_get_trigger_type(i + agent->irq_base);
			if (type & IRQ_TYPE_EDGE_RISING)
				bytes[byte] |= BIT(off + 1);
			if (type & IRQ_TYPE_EDGE_FALLING)
				bytes[byte] |= BIT(off + 0);

			edge_change &= ~BIT(i);
		}

		/* Write */
		status = twl_i2c_write(sih->module, bytes,
				sih->edr_offset, sih->bytes_edr);
		if (status)
			pr_err("twl4030: %s, %s --> %d\n", __func__,
					"write", status);
	}

	mutex_unlock(&agent->irq_lock);
}

static struct irq_chip twl4030_sih_irq_chip = {
	.name		= "twl4030",
	.irq_mask	= twl4030_sih_mask,
	.irq_unmask	= twl4030_sih_unmask,
	.irq_set_type	= twl4030_sih_set_type,
	.irq_bus_lock	= twl4030_sih_bus_lock,
	.irq_bus_sync_unlock = twl4030_sih_bus_sync_unlock,
	.flags		= IRQCHIP_SKIP_SET_WAKE,
};

/*----------------------------------------------------------------------*/

static inline int sih_read_isr(const struct sih *sih)
{
	int status;
	union {
		u8 bytes[4];
		u32 word;
	} isr;

	/* FIXME need retry-on-error ... */

	isr.word = 0;
	status = twl_i2c_read(sih->module, isr.bytes,
			sih->mask[irq_line].isr_offset, sih->bytes_ixr);

	return (status < 0) ? status : le32_to_cpu(isr.word);
}

/*
 * Generic handler for SIH interrupts ... we "know" this is called
 * in task context, with IRQs enabled.
 */
static irqreturn_t handle_twl4030_sih(int irq, void *data)
{
	struct sih_agent *agent = irq_get_handler_data(irq);
	const struct sih *sih = agent->sih;
	int isr;

	/* reading ISR acks the IRQs, using clear-on-read mode */
	isr = sih_read_isr(sih);

	if (isr < 0) {
		pr_err("twl4030: %s SIH, read ISR error %d\n",
			sih->name, isr);
		/* REVISIT:  recover; eventually mask it all, etc */
		return IRQ_HANDLED;
	}

	while (isr) {
		irq = fls(isr);
		irq--;
		isr &= ~BIT(irq);

		if (irq < sih->bits)
			handle_nested_irq(agent->irq_base + irq);
		else
			pr_err("twl4030: %s SIH, invalid ISR bit %d\n",
				sih->name, irq);
	}
	return IRQ_HANDLED;
}

/* returns the first IRQ used by this SIH bank, or negative errno */
int twl4030_sih_setup(struct device *dev, int module, int irq_base)
{
	int			sih_mod;
	const struct sih	*sih = NULL;
	struct sih_agent	*agent;
	int			i, irq;
	int			status = -EINVAL;

	/* only support modules with standard clear-on-read for now */
	for (sih_mod = 0, sih = sih_modules; sih_mod < nr_sih_modules;
			sih_mod++, sih++) {
		if (sih->module == module && sih->set_cor) {
			status = 0;
			break;
		}
	}

	if (status < 0)
		return status;

	agent = kzalloc(sizeof(*agent), GFP_KERNEL);
	if (!agent)
		return -ENOMEM;

	agent->irq_base = irq_base;
	agent->sih = sih;
	agent->imr = ~0;
	mutex_init(&agent->irq_lock);

	for (i = 0; i < sih->bits; i++) {
		irq = irq_base + i;

		irq_set_chip_data(irq, agent);
		irq_set_chip_and_handler(irq, &twl4030_sih_irq_chip,
					 handle_edge_irq);
		irq_set_nested_thread(irq, 1);
		activate_irq(irq);
	}

	/* replace generic PIH handler (handle_simple_irq) */
	irq = sih_mod + twl4030_irq_base;
	irq_set_handler_data(irq, agent);
	agent->irq_name = kasprintf(GFP_KERNEL, "twl4030_%s", sih->name);
	status = request_threaded_irq(irq, NULL, handle_twl4030_sih,
				      IRQF_EARLY_RESUME | IRQF_ONESHOT,
				      agent->irq_name ?: sih->name, NULL);

	dev_info(dev, "%s (irq %d) chaining IRQs %d..%d\n", sih->name,
			irq, irq_base, irq_base + i - 1);

	return status < 0 ? status : irq_base;
}

/* FIXME need a call to reverse twl4030_sih_setup() ... */

/*----------------------------------------------------------------------*/

/* FIXME pass in which interrupt line we'll use ... */
#define twl_irq_line	0

int twl4030_init_irq(struct device *dev, int irq_num)
{
	static struct irq_chip	twl4030_irq_chip;
	int			status, i;
	int			irq_base, irq_end, nr_irqs;
	struct			device_node *node = dev->of_node;

	/*
	 * TWL core and pwr interrupts must be contiguous because
	 * the hwirqs numbers are defined contiguously from 1 to 15.
	 * Create only one domain for both.
	 */
	nr_irqs = TWL4030_PWR_NR_IRQS + TWL4030_CORE_NR_IRQS;

	irq_base = irq_alloc_descs(-1, 0, nr_irqs, 0);
	if (IS_ERR_VALUE(irq_base)) {
		dev_err(dev, "Fail to allocate IRQ descs\n");
		return irq_base;
	}

	irq_domain_add_legacy(node, nr_irqs, irq_base, 0,
			      &irq_domain_simple_ops, NULL);

	irq_end = irq_base + TWL4030_CORE_NR_IRQS;

	/*
	 * Mask and clear all TWL4030 interrupts since initially we do
	 * not have any TWL4030 module interrupt handlers present
	 */
	status = twl4030_init_sih_modules(twl_irq_line);
	if (status < 0)
		return status;

	twl4030_irq_base = irq_base;

	/*
	 * Install an irq handler for each of the SIH modules;
	 * clone dummy irq_chip since PIH can't *do* anything
	 */
	twl4030_irq_chip = dummy_irq_chip;
	twl4030_irq_chip.name = "twl4030";

	twl4030_sih_irq_chip.irq_ack = dummy_irq_chip.irq_ack;

	for (i = irq_base; i < irq_end; i++) {
		irq_set_chip_and_handler(i, &twl4030_irq_chip,
					 handle_simple_irq);
		irq_set_nested_thread(i, 1);
		activate_irq(i);
	}

	dev_info(dev, "%s (irq %d) chaining IRQs %d..%d\n", "PIH",
			irq_num, irq_base, irq_end);

	/* ... and the PWR_INT module ... */
	status = twl4030_sih_setup(dev, TWL4030_MODULE_INT, irq_end);
	if (status < 0) {
		dev_err(dev, "sih_setup PWR INT --> %d\n", status);
		goto fail;
	}

	/* install an irq handler to demultiplex the TWL4030 interrupt */
	status = request_threaded_irq(irq_num, NULL, handle_twl4030_pih,
				      IRQF_ONESHOT,
				      "TWL4030-PIH", NULL);
	if (status < 0) {
		dev_err(dev, "could not claim irq%d: %d\n", irq_num, status);
		goto fail_rqirq;
	}
	enable_irq_wake(irq_num);

	return irq_base;
fail_rqirq:
	/* clean up twl4030_sih_setup */
fail:
	for (i = irq_base; i < irq_end; i++) {
		irq_set_nested_thread(i, 0);
		irq_set_chip_and_handler(i, NULL, NULL);
	}

	return status;
}

int twl4030_exit_irq(void)
{
	/* FIXME undo twl_init_irq() */
	if (twl4030_irq_base) {
		pr_err("twl4030: can't yet clean up IRQs?\n");
		return -ENOSYS;
	}
	return 0;
}

int twl4030_init_chip_irq(const char *chip)
{
	if (!strcmp(chip, "twl5031")) {
		sih_modules = sih_modules_twl5031;
		nr_sih_modules = ARRAY_SIZE(sih_modules_twl5031);
	} else {
		sih_modules = sih_modules_twl4030;
		nr_sih_modules = ARRAY_SIZE(sih_modules_twl4030);
	}

	return 0;
}
