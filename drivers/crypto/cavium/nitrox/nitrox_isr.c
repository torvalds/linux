// SPDX-License-Identifier: GPL-2.0
#include <linux/pci.h>
#include <linux/printk.h>
#include <linux/slab.h>

#include "nitrox_dev.h"
#include "nitrox_csr.h"
#include "nitrox_common.h"
#include "nitrox_hal.h"
#include "nitrox_isr.h"
#include "nitrox_mbx.h"

/*
 * One vector for each type of ring
 *  - NPS packet ring, AQMQ ring and ZQMQ ring
 */
#define NR_RING_VECTORS 3
#define NR_NON_RING_VECTORS 1
/* base entry for packet ring/port */
#define PKT_RING_MSIX_BASE 0
#define NON_RING_MSIX_BASE 192

/**
 * nps_pkt_slc_isr - IRQ handler for NPS solicit port
 * @irq: irq number
 * @data: argument
 */
static irqreturn_t nps_pkt_slc_isr(int irq, void *data)
{
	struct nitrox_q_vector *qvec = data;
	union nps_pkt_slc_cnts slc_cnts;
	struct nitrox_cmdq *cmdq = qvec->cmdq;

	slc_cnts.value = readq(cmdq->compl_cnt_csr_addr);
	/* New packet on SLC output port */
	if (slc_cnts.s.slc_int)
		tasklet_hi_schedule(&qvec->resp_tasklet);

	return IRQ_HANDLED;
}

static void clear_nps_core_err_intr(struct nitrox_device *ndev)
{
	u64 value;

	/* Write 1 to clear */
	value = nitrox_read_csr(ndev, NPS_CORE_INT);
	nitrox_write_csr(ndev, NPS_CORE_INT, value);

	dev_err_ratelimited(DEV(ndev), "NSP_CORE_INT  0x%016llx\n", value);
}

static void clear_nps_pkt_err_intr(struct nitrox_device *ndev)
{
	union nps_pkt_int pkt_int;
	unsigned long value, offset;
	int i;

	pkt_int.value = nitrox_read_csr(ndev, NPS_PKT_INT);
	dev_err_ratelimited(DEV(ndev), "NPS_PKT_INT  0x%016llx\n",
			    pkt_int.value);

	if (pkt_int.s.slc_err) {
		offset = NPS_PKT_SLC_ERR_TYPE;
		value = nitrox_read_csr(ndev, offset);
		nitrox_write_csr(ndev, offset, value);
		dev_err_ratelimited(DEV(ndev),
				    "NPS_PKT_SLC_ERR_TYPE  0x%016lx\n", value);

		offset = NPS_PKT_SLC_RERR_LO;
		value = nitrox_read_csr(ndev, offset);
		nitrox_write_csr(ndev, offset, value);
		/* enable the solicit ports */
		for_each_set_bit(i, &value, BITS_PER_LONG)
			enable_pkt_solicit_port(ndev, i);

		dev_err_ratelimited(DEV(ndev),
				    "NPS_PKT_SLC_RERR_LO  0x%016lx\n", value);

		offset = NPS_PKT_SLC_RERR_HI;
		value = nitrox_read_csr(ndev, offset);
		nitrox_write_csr(ndev, offset, value);
		dev_err_ratelimited(DEV(ndev),
				    "NPS_PKT_SLC_RERR_HI  0x%016lx\n", value);
	}

	if (pkt_int.s.in_err) {
		offset = NPS_PKT_IN_ERR_TYPE;
		value = nitrox_read_csr(ndev, offset);
		nitrox_write_csr(ndev, offset, value);
		dev_err_ratelimited(DEV(ndev),
				    "NPS_PKT_IN_ERR_TYPE  0x%016lx\n", value);
		offset = NPS_PKT_IN_RERR_LO;
		value = nitrox_read_csr(ndev, offset);
		nitrox_write_csr(ndev, offset, value);
		/* enable the input ring */
		for_each_set_bit(i, &value, BITS_PER_LONG)
			enable_pkt_input_ring(ndev, i);

		dev_err_ratelimited(DEV(ndev),
				    "NPS_PKT_IN_RERR_LO  0x%016lx\n", value);

		offset = NPS_PKT_IN_RERR_HI;
		value = nitrox_read_csr(ndev, offset);
		nitrox_write_csr(ndev, offset, value);
		dev_err_ratelimited(DEV(ndev),
				    "NPS_PKT_IN_RERR_HI  0x%016lx\n", value);
	}
}

static void clear_pom_err_intr(struct nitrox_device *ndev)
{
	u64 value;

	value = nitrox_read_csr(ndev, POM_INT);
	nitrox_write_csr(ndev, POM_INT, value);
	dev_err_ratelimited(DEV(ndev), "POM_INT  0x%016llx\n", value);
}

static void clear_pem_err_intr(struct nitrox_device *ndev)
{
	u64 value;

	value = nitrox_read_csr(ndev, PEM0_INT);
	nitrox_write_csr(ndev, PEM0_INT, value);
	dev_err_ratelimited(DEV(ndev), "PEM(0)_INT  0x%016llx\n", value);
}

static void clear_lbc_err_intr(struct nitrox_device *ndev)
{
	union lbc_int lbc_int;
	u64 value, offset;
	int i;

	lbc_int.value = nitrox_read_csr(ndev, LBC_INT);
	dev_err_ratelimited(DEV(ndev), "LBC_INT  0x%016llx\n", lbc_int.value);

	if (lbc_int.s.dma_rd_err) {
		for (i = 0; i < NR_CLUSTERS; i++) {
			offset = EFL_CORE_VF_ERR_INT0X(i);
			value = nitrox_read_csr(ndev, offset);
			nitrox_write_csr(ndev, offset, value);
			offset = EFL_CORE_VF_ERR_INT1X(i);
			value = nitrox_read_csr(ndev, offset);
			nitrox_write_csr(ndev, offset, value);
		}
	}

	if (lbc_int.s.cam_soft_err) {
		dev_err_ratelimited(DEV(ndev), "CAM_SOFT_ERR, invalidating LBC\n");
		invalidate_lbc(ndev);
	}

	if (lbc_int.s.pref_dat_len_mismatch_err) {
		offset = LBC_PLM_VF1_64_INT;
		value = nitrox_read_csr(ndev, offset);
		nitrox_write_csr(ndev, offset, value);
		offset = LBC_PLM_VF65_128_INT;
		value = nitrox_read_csr(ndev, offset);
		nitrox_write_csr(ndev, offset, value);
	}

	if (lbc_int.s.rd_dat_len_mismatch_err) {
		offset = LBC_ELM_VF1_64_INT;
		value = nitrox_read_csr(ndev, offset);
		nitrox_write_csr(ndev, offset, value);
		offset = LBC_ELM_VF65_128_INT;
		value = nitrox_read_csr(ndev, offset);
		nitrox_write_csr(ndev, offset, value);
	}
	nitrox_write_csr(ndev, LBC_INT, lbc_int.value);
}

static void clear_efl_err_intr(struct nitrox_device *ndev)
{
	int i;

	for (i = 0; i < NR_CLUSTERS; i++) {
		union efl_core_int core_int;
		u64 value, offset;

		offset = EFL_CORE_INTX(i);
		core_int.value = nitrox_read_csr(ndev, offset);
		nitrox_write_csr(ndev, offset, core_int.value);
		dev_err_ratelimited(DEV(ndev), "ELF_CORE(%d)_INT  0x%016llx\n",
				    i, core_int.value);
		if (core_int.s.se_err) {
			offset = EFL_CORE_SE_ERR_INTX(i);
			value = nitrox_read_csr(ndev, offset);
			nitrox_write_csr(ndev, offset, value);
		}
	}
}

static void clear_bmi_err_intr(struct nitrox_device *ndev)
{
	u64 value;

	value = nitrox_read_csr(ndev, BMI_INT);
	nitrox_write_csr(ndev, BMI_INT, value);
	dev_err_ratelimited(DEV(ndev), "BMI_INT  0x%016llx\n", value);
}

static void nps_core_int_tasklet(unsigned long data)
{
	struct nitrox_q_vector *qvec = (void *)(uintptr_t)(data);
	struct nitrox_device *ndev = qvec->ndev;

	/* if pf mode do queue recovery */
	if (ndev->mode == __NDEV_MODE_PF) {
	} else {
		/**
		 * if VF(s) enabled communicate the error information
		 * to VF(s)
		 */
	}
}

/*
 * nps_core_int_isr - interrupt handler for NITROX errors and
 *   mailbox communication
 */
static irqreturn_t nps_core_int_isr(int irq, void *data)
{
	struct nitrox_q_vector *qvec = data;
	struct nitrox_device *ndev = qvec->ndev;
	union nps_core_int_active core_int;

	core_int.value = nitrox_read_csr(ndev, NPS_CORE_INT_ACTIVE);

	if (core_int.s.nps_core)
		clear_nps_core_err_intr(ndev);

	if (core_int.s.nps_pkt)
		clear_nps_pkt_err_intr(ndev);

	if (core_int.s.pom)
		clear_pom_err_intr(ndev);

	if (core_int.s.pem)
		clear_pem_err_intr(ndev);

	if (core_int.s.lbc)
		clear_lbc_err_intr(ndev);

	if (core_int.s.efl)
		clear_efl_err_intr(ndev);

	if (core_int.s.bmi)
		clear_bmi_err_intr(ndev);

	/* Mailbox interrupt */
	if (core_int.s.mbox)
		nitrox_pf2vf_mbox_handler(ndev);

	/* If more work callback the ISR, set resend */
	core_int.s.resend = 1;
	nitrox_write_csr(ndev, NPS_CORE_INT_ACTIVE, core_int.value);

	return IRQ_HANDLED;
}

void nitrox_unregister_interrupts(struct nitrox_device *ndev)
{
	struct pci_dev *pdev = ndev->pdev;
	int i;

	for (i = 0; i < ndev->num_vecs; i++) {
		struct nitrox_q_vector *qvec;
		int vec;

		qvec = ndev->qvec + i;
		if (!qvec->valid)
			continue;

		/* get the vector number */
		vec = pci_irq_vector(pdev, i);
		irq_set_affinity_hint(vec, NULL);
		free_irq(vec, qvec);

		tasklet_disable(&qvec->resp_tasklet);
		tasklet_kill(&qvec->resp_tasklet);
		qvec->valid = false;
	}
	kfree(ndev->qvec);
	ndev->qvec = NULL;
	pci_free_irq_vectors(pdev);
}

int nitrox_register_interrupts(struct nitrox_device *ndev)
{
	struct pci_dev *pdev = ndev->pdev;
	struct nitrox_q_vector *qvec;
	int nr_vecs, vec, cpu;
	int ret, i;

	/*
	 * PF MSI-X vectors
	 *
	 * Entry 0: NPS PKT ring 0
	 * Entry 1: AQMQ ring 0
	 * Entry 2: ZQM ring 0
	 * Entry 3: NPS PKT ring 1
	 * Entry 4: AQMQ ring 1
	 * Entry 5: ZQM ring 1
	 * ....
	 * Entry 192: NPS_CORE_INT_ACTIVE
	 */
	nr_vecs = pci_msix_vec_count(pdev);

	/* Enable MSI-X */
	ret = pci_alloc_irq_vectors(pdev, nr_vecs, nr_vecs, PCI_IRQ_MSIX);
	if (ret < 0) {
		dev_err(DEV(ndev), "msix vectors %d alloc failed\n", nr_vecs);
		return ret;
	}
	ndev->num_vecs = nr_vecs;

	ndev->qvec = kcalloc(nr_vecs, sizeof(*qvec), GFP_KERNEL);
	if (!ndev->qvec) {
		pci_free_irq_vectors(pdev);
		return -ENOMEM;
	}

	/* request irqs for packet rings/ports */
	for (i = PKT_RING_MSIX_BASE; i < (nr_vecs - 1); i += NR_RING_VECTORS) {
		qvec = &ndev->qvec[i];

		qvec->ring = i / NR_RING_VECTORS;
		if (qvec->ring >= ndev->nr_queues)
			break;

		qvec->cmdq = &ndev->pkt_inq[qvec->ring];
		snprintf(qvec->name, IRQ_NAMESZ, "nitrox-pkt%d", qvec->ring);
		/* get the vector number */
		vec = pci_irq_vector(pdev, i);
		ret = request_irq(vec, nps_pkt_slc_isr, 0, qvec->name, qvec);
		if (ret) {
			dev_err(DEV(ndev), "irq failed for pkt ring/port%d\n",
				qvec->ring);
			goto irq_fail;
		}
		cpu = qvec->ring % num_online_cpus();
		irq_set_affinity_hint(vec, get_cpu_mask(cpu));

		tasklet_init(&qvec->resp_tasklet, pkt_slc_resp_tasklet,
			     (unsigned long)qvec);
		qvec->valid = true;
	}

	/* request irqs for non ring vectors */
	i = NON_RING_MSIX_BASE;
	qvec = &ndev->qvec[i];
	qvec->ndev = ndev;

	snprintf(qvec->name, IRQ_NAMESZ, "nitrox-core-int%d", i);
	/* get the vector number */
	vec = pci_irq_vector(pdev, i);
	ret = request_irq(vec, nps_core_int_isr, 0, qvec->name, qvec);
	if (ret) {
		dev_err(DEV(ndev), "irq failed for nitrox-core-int%d\n", i);
		goto irq_fail;
	}
	cpu = num_online_cpus();
	irq_set_affinity_hint(vec, get_cpu_mask(cpu));

	tasklet_init(&qvec->resp_tasklet, nps_core_int_tasklet,
		     (unsigned long)qvec);
	qvec->valid = true;

	return 0;

irq_fail:
	nitrox_unregister_interrupts(ndev);
	return ret;
}

void nitrox_sriov_unregister_interrupts(struct nitrox_device *ndev)
{
	struct pci_dev *pdev = ndev->pdev;
	int i;

	for (i = 0; i < ndev->num_vecs; i++) {
		struct nitrox_q_vector *qvec;
		int vec;

		qvec = ndev->qvec + i;
		if (!qvec->valid)
			continue;

		vec = ndev->iov.msix.vector;
		irq_set_affinity_hint(vec, NULL);
		free_irq(vec, qvec);

		tasklet_disable(&qvec->resp_tasklet);
		tasklet_kill(&qvec->resp_tasklet);
		qvec->valid = false;
	}
	kfree(ndev->qvec);
	ndev->qvec = NULL;
	pci_disable_msix(pdev);
}

int nitrox_sriov_register_interupts(struct nitrox_device *ndev)
{
	struct pci_dev *pdev = ndev->pdev;
	struct nitrox_q_vector *qvec;
	int vec, cpu;
	int ret;

	/**
	 * only non ring vectors i.e Entry 192 is available
	 * for PF in SR-IOV mode.
	 */
	ndev->iov.msix.entry = NON_RING_MSIX_BASE;
	ret = pci_enable_msix_exact(pdev, &ndev->iov.msix, NR_NON_RING_VECTORS);
	if (ret) {
		dev_err(DEV(ndev), "failed to allocate nps-core-int%d\n",
			NON_RING_MSIX_BASE);
		return ret;
	}

	qvec = kcalloc(NR_NON_RING_VECTORS, sizeof(*qvec), GFP_KERNEL);
	if (!qvec) {
		pci_disable_msix(pdev);
		return -ENOMEM;
	}
	qvec->ndev = ndev;

	ndev->qvec = qvec;
	ndev->num_vecs = NR_NON_RING_VECTORS;
	snprintf(qvec->name, IRQ_NAMESZ, "nitrox-core-int%d",
		 NON_RING_MSIX_BASE);

	vec = ndev->iov.msix.vector;
	ret = request_irq(vec, nps_core_int_isr, 0, qvec->name, qvec);
	if (ret) {
		dev_err(DEV(ndev), "irq failed for nitrox-core-int%d\n",
			NON_RING_MSIX_BASE);
		goto iov_irq_fail;
	}
	cpu = num_online_cpus();
	irq_set_affinity_hint(vec, get_cpu_mask(cpu));

	tasklet_init(&qvec->resp_tasklet, nps_core_int_tasklet,
		     (unsigned long)qvec);
	qvec->valid = true;

	return 0;

iov_irq_fail:
	nitrox_sriov_unregister_interrupts(ndev);
	return ret;
}
