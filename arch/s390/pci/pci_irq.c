// SPDX-License-Identifier: GPL-2.0
#define KMSG_COMPONENT "zpci"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/kernel.h>
#include <linux/irq.h>
#include <linux/kernel_stat.h>
#include <linux/pci.h>
#include <linux/msi.h>
#include <linux/smp.h>

#include <asm/isc.h>
#include <asm/airq.h>

static enum {FLOATING, DIRECTED} irq_delivery;

#define	SIC_IRQ_MODE_ALL		0
#define	SIC_IRQ_MODE_SINGLE		1
#define	SIC_IRQ_MODE_DIRECT		4
#define	SIC_IRQ_MODE_D_ALL		16
#define	SIC_IRQ_MODE_D_SINGLE		17
#define	SIC_IRQ_MODE_SET_CPU		18

/*
 * summary bit vector
 * FLOATING - summary bit per function
 * DIRECTED - summary bit per cpu (only used in fallback path)
 */
static struct airq_iv *zpci_sbv;

/*
 * interrupt bit vectors
 * FLOATING - interrupt bit vector per function
 * DIRECTED - interrupt bit vector per cpu
 */
static struct airq_iv **zpci_ibv;

/* Modify PCI: Register adapter interruptions */
static int zpci_set_airq(struct zpci_dev *zdev)
{
	u64 req = ZPCI_CREATE_REQ(zdev->fh, 0, ZPCI_MOD_FC_REG_INT);
	struct zpci_fib fib = {0};
	u8 status;

	fib.fmt0.isc = PCI_ISC;
	fib.fmt0.sum = 1;	/* enable summary notifications */
	fib.fmt0.noi = airq_iv_end(zdev->aibv);
	fib.fmt0.aibv = (unsigned long) zdev->aibv->vector;
	fib.fmt0.aibvo = 0;	/* each zdev has its own interrupt vector */
	fib.fmt0.aisb = (unsigned long) zpci_sbv->vector + (zdev->aisb/64)*8;
	fib.fmt0.aisbo = zdev->aisb & 63;

	return zpci_mod_fc(req, &fib, &status) ? -EIO : 0;
}

/* Modify PCI: Unregister adapter interruptions */
static int zpci_clear_airq(struct zpci_dev *zdev)
{
	u64 req = ZPCI_CREATE_REQ(zdev->fh, 0, ZPCI_MOD_FC_DEREG_INT);
	struct zpci_fib fib = {0};
	u8 cc, status;

	cc = zpci_mod_fc(req, &fib, &status);
	if (cc == 3 || (cc == 1 && status == 24))
		/* Function already gone or IRQs already deregistered. */
		cc = 0;

	return cc ? -EIO : 0;
}

/* Modify PCI: Register CPU directed interruptions */
static int zpci_set_directed_irq(struct zpci_dev *zdev)
{
	u64 req = ZPCI_CREATE_REQ(zdev->fh, 0, ZPCI_MOD_FC_REG_INT_D);
	struct zpci_fib fib = {0};
	u8 status;

	fib.fmt = 1;
	fib.fmt1.noi = zdev->msi_nr_irqs;
	fib.fmt1.dibvo = zdev->msi_first_bit;

	return zpci_mod_fc(req, &fib, &status) ? -EIO : 0;
}

/* Modify PCI: Unregister CPU directed interruptions */
static int zpci_clear_directed_irq(struct zpci_dev *zdev)
{
	u64 req = ZPCI_CREATE_REQ(zdev->fh, 0, ZPCI_MOD_FC_DEREG_INT_D);
	struct zpci_fib fib = {0};
	u8 cc, status;

	fib.fmt = 1;
	cc = zpci_mod_fc(req, &fib, &status);
	if (cc == 3 || (cc == 1 && status == 24))
		/* Function already gone or IRQs already deregistered. */
		cc = 0;

	return cc ? -EIO : 0;
}

static int zpci_set_irq_affinity(struct irq_data *data, const struct cpumask *dest,
				 bool force)
{
	struct msi_desc *entry = irq_get_msi_desc(data->irq);
	struct msi_msg msg = entry->msg;

	msg.address_lo &= 0xff0000ff;
	msg.address_lo |= (cpumask_first(dest) << 8);
	pci_write_msi_msg(data->irq, &msg);

	return IRQ_SET_MASK_OK;
}

static struct irq_chip zpci_irq_chip = {
	.name = "PCI-MSI",
	.irq_unmask = pci_msi_unmask_irq,
	.irq_mask = pci_msi_mask_irq,
	.irq_set_affinity = zpci_set_irq_affinity,
};

static void zpci_handle_cpu_local_irq(bool rescan)
{
	struct airq_iv *dibv = zpci_ibv[smp_processor_id()];
	unsigned long bit;
	int irqs_on = 0;

	for (bit = 0;;) {
		/* Scan the directed IRQ bit vector */
		bit = airq_iv_scan(dibv, bit, airq_iv_end(dibv));
		if (bit == -1UL) {
			if (!rescan || irqs_on++)
				/* End of second scan with interrupts on. */
				break;
			/* First scan complete, reenable interrupts. */
			if (zpci_set_irq_ctrl(SIC_IRQ_MODE_D_SINGLE, PCI_ISC))
				break;
			bit = 0;
			continue;
		}
		inc_irq_stat(IRQIO_MSI);
		generic_handle_irq(airq_iv_get_data(dibv, bit));
	}
}

struct cpu_irq_data {
	call_single_data_t csd;
	atomic_t scheduled;
};
static DEFINE_PER_CPU_SHARED_ALIGNED(struct cpu_irq_data, irq_data);

static void zpci_handle_remote_irq(void *data)
{
	atomic_t *scheduled = data;

	do {
		zpci_handle_cpu_local_irq(false);
	} while (atomic_dec_return(scheduled));
}

static void zpci_handle_fallback_irq(void)
{
	struct cpu_irq_data *cpu_data;
	unsigned long cpu;
	int irqs_on = 0;

	for (cpu = 0;;) {
		cpu = airq_iv_scan(zpci_sbv, cpu, airq_iv_end(zpci_sbv));
		if (cpu == -1UL) {
			if (irqs_on++)
				/* End of second scan with interrupts on. */
				break;
			/* First scan complete, reenable interrupts. */
			if (zpci_set_irq_ctrl(SIC_IRQ_MODE_SINGLE, PCI_ISC))
				break;
			cpu = 0;
			continue;
		}
		cpu_data = &per_cpu(irq_data, cpu);
		if (atomic_inc_return(&cpu_data->scheduled) > 1)
			continue;

		cpu_data->csd.func = zpci_handle_remote_irq;
		cpu_data->csd.info = &cpu_data->scheduled;
		cpu_data->csd.flags = 0;
		smp_call_function_single_async(cpu, &cpu_data->csd);
	}
}

static void zpci_directed_irq_handler(struct airq_struct *airq, bool floating)
{
	if (floating) {
		inc_irq_stat(IRQIO_PCF);
		zpci_handle_fallback_irq();
	} else {
		inc_irq_stat(IRQIO_PCD);
		zpci_handle_cpu_local_irq(true);
	}
}

static void zpci_floating_irq_handler(struct airq_struct *airq, bool floating)
{
	unsigned long si, ai;
	struct airq_iv *aibv;
	int irqs_on = 0;

	inc_irq_stat(IRQIO_PCF);
	for (si = 0;;) {
		/* Scan adapter summary indicator bit vector */
		si = airq_iv_scan(zpci_sbv, si, airq_iv_end(zpci_sbv));
		if (si == -1UL) {
			if (irqs_on++)
				/* End of second scan with interrupts on. */
				break;
			/* First scan complete, reenable interrupts. */
			if (zpci_set_irq_ctrl(SIC_IRQ_MODE_SINGLE, PCI_ISC))
				break;
			si = 0;
			continue;
		}

		/* Scan the adapter interrupt vector for this device. */
		aibv = zpci_ibv[si];
		for (ai = 0;;) {
			ai = airq_iv_scan(aibv, ai, airq_iv_end(aibv));
			if (ai == -1UL)
				break;
			inc_irq_stat(IRQIO_MSI);
			airq_iv_lock(aibv, ai);
			generic_handle_irq(airq_iv_get_data(aibv, ai));
			airq_iv_unlock(aibv, ai);
		}
	}
}

int arch_setup_msi_irqs(struct pci_dev *pdev, int nvec, int type)
{
	struct zpci_dev *zdev = to_zpci(pdev);
	unsigned int hwirq, msi_vecs, cpu;
	unsigned long bit;
	struct msi_desc *msi;
	struct msi_msg msg;
	int rc, irq;

	zdev->aisb = -1UL;
	zdev->msi_first_bit = -1U;
	if (type == PCI_CAP_ID_MSI && nvec > 1)
		return 1;
	msi_vecs = min_t(unsigned int, nvec, zdev->max_msi);

	if (irq_delivery == DIRECTED) {
		/* Allocate cpu vector bits */
		bit = airq_iv_alloc(zpci_ibv[0], msi_vecs);
		if (bit == -1UL)
			return -EIO;
	} else {
		/* Allocate adapter summary indicator bit */
		bit = airq_iv_alloc_bit(zpci_sbv);
		if (bit == -1UL)
			return -EIO;
		zdev->aisb = bit;

		/* Create adapter interrupt vector */
		zdev->aibv = airq_iv_create(msi_vecs, AIRQ_IV_DATA | AIRQ_IV_BITLOCK);
		if (!zdev->aibv)
			return -ENOMEM;

		/* Wire up shortcut pointer */
		zpci_ibv[bit] = zdev->aibv;
		/* Each function has its own interrupt vector */
		bit = 0;
	}

	/* Request MSI interrupts */
	hwirq = bit;
	for_each_pci_msi_entry(msi, pdev) {
		rc = -EIO;
		if (hwirq - bit >= msi_vecs)
			break;
		irq = __irq_alloc_descs(-1, 0, 1, 0, THIS_MODULE, msi->affinity);
		if (irq < 0)
			return -ENOMEM;
		rc = irq_set_msi_desc(irq, msi);
		if (rc)
			return rc;
		irq_set_chip_and_handler(irq, &zpci_irq_chip,
					 handle_percpu_irq);
		msg.data = hwirq - bit;
		if (irq_delivery == DIRECTED) {
			msg.address_lo = zdev->msi_addr & 0xff0000ff;
			msg.address_lo |= msi->affinity ?
				(cpumask_first(&msi->affinity->mask) << 8) : 0;
			for_each_possible_cpu(cpu) {
				airq_iv_set_data(zpci_ibv[cpu], hwirq, irq);
			}
		} else {
			msg.address_lo = zdev->msi_addr & 0xffffffff;
			airq_iv_set_data(zdev->aibv, hwirq, irq);
		}
		msg.address_hi = zdev->msi_addr >> 32;
		pci_write_msi_msg(irq, &msg);
		hwirq++;
	}

	zdev->msi_first_bit = bit;
	zdev->msi_nr_irqs = msi_vecs;

	if (irq_delivery == DIRECTED)
		rc = zpci_set_directed_irq(zdev);
	else
		rc = zpci_set_airq(zdev);
	if (rc)
		return rc;

	return (msi_vecs == nvec) ? 0 : msi_vecs;
}

void arch_teardown_msi_irqs(struct pci_dev *pdev)
{
	struct zpci_dev *zdev = to_zpci(pdev);
	struct msi_desc *msi;
	int rc;

	/* Disable interrupts */
	if (irq_delivery == DIRECTED)
		rc = zpci_clear_directed_irq(zdev);
	else
		rc = zpci_clear_airq(zdev);
	if (rc)
		return;

	/* Release MSI interrupts */
	for_each_pci_msi_entry(msi, pdev) {
		if (!msi->irq)
			continue;
		if (msi->msi_attrib.is_msix)
			__pci_msix_desc_mask_irq(msi, 1);
		else
			__pci_msi_desc_mask_irq(msi, 1, 1);
		irq_set_msi_desc(msi->irq, NULL);
		irq_free_desc(msi->irq);
		msi->msg.address_lo = 0;
		msi->msg.address_hi = 0;
		msi->msg.data = 0;
		msi->irq = 0;
	}

	if (zdev->aisb != -1UL) {
		zpci_ibv[zdev->aisb] = NULL;
		airq_iv_free_bit(zpci_sbv, zdev->aisb);
		zdev->aisb = -1UL;
	}
	if (zdev->aibv) {
		airq_iv_release(zdev->aibv);
		zdev->aibv = NULL;
	}

	if ((irq_delivery == DIRECTED) && zdev->msi_first_bit != -1U)
		airq_iv_free(zpci_ibv[0], zdev->msi_first_bit, zdev->msi_nr_irqs);
}

static struct airq_struct zpci_airq = {
	.handler = zpci_floating_irq_handler,
	.isc = PCI_ISC,
};

static void __init cpu_enable_directed_irq(void *unused)
{
	union zpci_sic_iib iib = {{0}};

	iib.cdiib.dibv_addr = (u64) zpci_ibv[smp_processor_id()]->vector;

	__zpci_set_irq_ctrl(SIC_IRQ_MODE_SET_CPU, 0, &iib);
	zpci_set_irq_ctrl(SIC_IRQ_MODE_D_SINGLE, PCI_ISC);
}

static int __init zpci_directed_irq_init(void)
{
	union zpci_sic_iib iib = {{0}};
	unsigned int cpu;

	zpci_sbv = airq_iv_create(num_possible_cpus(), 0);
	if (!zpci_sbv)
		return -ENOMEM;

	iib.diib.isc = PCI_ISC;
	iib.diib.nr_cpus = num_possible_cpus();
	iib.diib.disb_addr = (u64) zpci_sbv->vector;
	__zpci_set_irq_ctrl(SIC_IRQ_MODE_DIRECT, 0, &iib);

	zpci_ibv = kcalloc(num_possible_cpus(), sizeof(*zpci_ibv),
			   GFP_KERNEL);
	if (!zpci_ibv)
		return -ENOMEM;

	for_each_possible_cpu(cpu) {
		/*
		 * Per CPU IRQ vectors look the same but bit-allocation
		 * is only done on the first vector.
		 */
		zpci_ibv[cpu] = airq_iv_create(cache_line_size() * BITS_PER_BYTE,
					       AIRQ_IV_DATA |
					       AIRQ_IV_CACHELINE |
					       (!cpu ? AIRQ_IV_ALLOC : 0));
		if (!zpci_ibv[cpu])
			return -ENOMEM;
	}
	on_each_cpu(cpu_enable_directed_irq, NULL, 1);

	zpci_irq_chip.irq_set_affinity = zpci_set_irq_affinity;

	return 0;
}

static int __init zpci_floating_irq_init(void)
{
	zpci_ibv = kcalloc(ZPCI_NR_DEVICES, sizeof(*zpci_ibv), GFP_KERNEL);
	if (!zpci_ibv)
		return -ENOMEM;

	zpci_sbv = airq_iv_create(ZPCI_NR_DEVICES, AIRQ_IV_ALLOC);
	if (!zpci_sbv)
		goto out_free;

	return 0;

out_free:
	kfree(zpci_ibv);
	return -ENOMEM;
}

int __init zpci_irq_init(void)
{
	int rc;

	irq_delivery = sclp.has_dirq ? DIRECTED : FLOATING;
	if (s390_pci_force_floating)
		irq_delivery = FLOATING;

	if (irq_delivery == DIRECTED)
		zpci_airq.handler = zpci_directed_irq_handler;

	rc = register_adapter_interrupt(&zpci_airq);
	if (rc)
		goto out;
	/* Set summary to 1 to be called every time for the ISC. */
	*zpci_airq.lsi_ptr = 1;

	switch (irq_delivery) {
	case FLOATING:
		rc = zpci_floating_irq_init();
		break;
	case DIRECTED:
		rc = zpci_directed_irq_init();
		break;
	}

	if (rc)
		goto out_airq;

	/*
	 * Enable floating IRQs (with suppression after one IRQ). When using
	 * directed IRQs this enables the fallback path.
	 */
	zpci_set_irq_ctrl(SIC_IRQ_MODE_SINGLE, PCI_ISC);

	return 0;
out_airq:
	unregister_adapter_interrupt(&zpci_airq);
out:
	return rc;
}

void __init zpci_irq_exit(void)
{
	unsigned int cpu;

	if (irq_delivery == DIRECTED) {
		for_each_possible_cpu(cpu) {
			airq_iv_release(zpci_ibv[cpu]);
		}
	}
	kfree(zpci_ibv);
	if (zpci_sbv)
		airq_iv_release(zpci_sbv);
	unregister_adapter_interrupt(&zpci_airq);
}
