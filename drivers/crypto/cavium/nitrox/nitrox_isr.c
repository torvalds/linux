// SPDX-License-Identifier: GPL-2.0
#include <linux/pci.h>
#include <linux/printk.h>
#include <linux/slab.h>

#include "nitrox_dev.h"
#include "nitrox_csr.h"
#include "nitrox_common.h"

#define NR_RING_VECTORS 3
#define NPS_CORE_INT_ACTIVE_ENTRY 192

/**
 * nps_pkt_slc_isr - IRQ handler for NPS solicit port
 * @irq: irq number
 * @data: argument
 */
static irqreturn_t nps_pkt_slc_isr(int irq, void *data)
{
	struct bh_data *slc = data;
	union nps_pkt_slc_cnts pkt_slc_cnts;

	pkt_slc_cnts.value = readq(slc->completion_cnt_csr_addr);
	/* New packet on SLC output port */
	if (pkt_slc_cnts.s.slc_int)
		tasklet_hi_schedule(&slc->resp_handler);

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

/**
 * clear_nps_core_int_active - clear NPS_CORE_INT_ACTIVE interrupts
 * @ndev: NITROX device
 */
static void clear_nps_core_int_active(struct nitrox_device *ndev)
{
	union nps_core_int_active core_int_active;

	core_int_active.value = nitrox_read_csr(ndev, NPS_CORE_INT_ACTIVE);

	if (core_int_active.s.nps_core)
		clear_nps_core_err_intr(ndev);

	if (core_int_active.s.nps_pkt)
		clear_nps_pkt_err_intr(ndev);

	if (core_int_active.s.pom)
		clear_pom_err_intr(ndev);

	if (core_int_active.s.pem)
		clear_pem_err_intr(ndev);

	if (core_int_active.s.lbc)
		clear_lbc_err_intr(ndev);

	if (core_int_active.s.efl)
		clear_efl_err_intr(ndev);

	if (core_int_active.s.bmi)
		clear_bmi_err_intr(ndev);

	/* If more work callback the ISR, set resend */
	core_int_active.s.resend = 1;
	nitrox_write_csr(ndev, NPS_CORE_INT_ACTIVE, core_int_active.value);
}

static irqreturn_t nps_core_int_isr(int irq, void *data)
{
	struct nitrox_device *ndev = data;

	clear_nps_core_int_active(ndev);

	return IRQ_HANDLED;
}

static int nitrox_enable_msix(struct nitrox_device *ndev)
{
	struct msix_entry *entries;
	char **names;
	int i, nr_entries, ret;

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
	nr_entries = (ndev->nr_queues * NR_RING_VECTORS) + 1;
	entries = kzalloc_node(nr_entries * sizeof(struct msix_entry),
			       GFP_KERNEL, ndev->node);
	if (!entries)
		return -ENOMEM;

	names = kcalloc(nr_entries, sizeof(char *), GFP_KERNEL);
	if (!names) {
		kfree(entries);
		return -ENOMEM;
	}

	/* fill entires */
	for (i = 0; i < (nr_entries - 1); i++)
		entries[i].entry = i;

	entries[i].entry = NPS_CORE_INT_ACTIVE_ENTRY;

	for (i = 0; i < nr_entries; i++) {
		*(names + i) = kzalloc(MAX_MSIX_VECTOR_NAME, GFP_KERNEL);
		if (!(*(names + i))) {
			ret = -ENOMEM;
			goto msix_fail;
		}
	}
	ndev->msix.entries = entries;
	ndev->msix.names = names;
	ndev->msix.nr_entries = nr_entries;

	ret = pci_enable_msix_exact(ndev->pdev, ndev->msix.entries,
				    ndev->msix.nr_entries);
	if (ret) {
		dev_err(&ndev->pdev->dev, "Failed to enable MSI-X IRQ(s) %d\n",
			ret);
		goto msix_fail;
	}
	return 0;

msix_fail:
	for (i = 0; i < nr_entries; i++)
		kfree(*(names + i));

	kfree(entries);
	kfree(names);
	return ret;
}

static void nitrox_cleanup_pkt_slc_bh(struct nitrox_device *ndev)
{
	int i;

	if (!ndev->bh.slc)
		return;

	for (i = 0; i < ndev->nr_queues; i++) {
		struct bh_data *bh = &ndev->bh.slc[i];

		tasklet_disable(&bh->resp_handler);
		tasklet_kill(&bh->resp_handler);
	}
	kfree(ndev->bh.slc);
	ndev->bh.slc = NULL;
}

static int nitrox_setup_pkt_slc_bh(struct nitrox_device *ndev)
{
	u32 size;
	int i;

	size = ndev->nr_queues * sizeof(struct bh_data);
	ndev->bh.slc = kzalloc(size, GFP_KERNEL);
	if (!ndev->bh.slc)
		return -ENOMEM;

	for (i = 0; i < ndev->nr_queues; i++) {
		struct bh_data *bh = &ndev->bh.slc[i];
		u64 offset;

		offset = NPS_PKT_SLC_CNTSX(i);
		/* pre calculate completion count address */
		bh->completion_cnt_csr_addr = NITROX_CSR_ADDR(ndev, offset);
		bh->cmdq = &ndev->pkt_cmdqs[i];

		tasklet_init(&bh->resp_handler, pkt_slc_resp_handler,
			     (unsigned long)bh);
	}

	return 0;
}

static int nitrox_request_irqs(struct nitrox_device *ndev)
{
	struct pci_dev *pdev = ndev->pdev;
	struct msix_entry *msix_ent = ndev->msix.entries;
	int nr_ring_vectors, i = 0, ring, cpu, ret;
	char *name;

	/*
	 * PF MSI-X vectors
	 *
	 * Entry 0: NPS PKT ring 0
	 * Entry 1: AQMQ ring 0
	 * Entry 2: ZQM ring 0
	 * Entry 3: NPS PKT ring 1
	 * ....
	 * Entry 192: NPS_CORE_INT_ACTIVE
	 */
	nr_ring_vectors = ndev->nr_queues * NR_RING_VECTORS;

	/* request irq for pkt ring/ports only */
	while (i < nr_ring_vectors) {
		name = *(ndev->msix.names + i);
		ring = (i / NR_RING_VECTORS);
		snprintf(name, MAX_MSIX_VECTOR_NAME, "n5(%d)-slc-ring%d",
			 ndev->idx, ring);

		ret = request_irq(msix_ent[i].vector, nps_pkt_slc_isr, 0,
				  name, &ndev->bh.slc[ring]);
		if (ret) {
			dev_err(&pdev->dev, "failed to get irq %d for %s\n",
				msix_ent[i].vector, name);
			return ret;
		}
		cpu = ring % num_online_cpus();
		irq_set_affinity_hint(msix_ent[i].vector, get_cpu_mask(cpu));

		set_bit(i, ndev->msix.irqs);
		i += NR_RING_VECTORS;
	}

	/* Request IRQ for NPS_CORE_INT_ACTIVE */
	name = *(ndev->msix.names + i);
	snprintf(name, MAX_MSIX_VECTOR_NAME, "n5(%d)-nps-core-int", ndev->idx);
	ret = request_irq(msix_ent[i].vector, nps_core_int_isr, 0, name, ndev);
	if (ret) {
		dev_err(&pdev->dev, "failed to get irq %d for %s\n",
			msix_ent[i].vector, name);
		return ret;
	}
	set_bit(i, ndev->msix.irqs);

	return 0;
}

static void nitrox_disable_msix(struct nitrox_device *ndev)
{
	struct msix_entry *msix_ent = ndev->msix.entries;
	char **names = ndev->msix.names;
	int i = 0, ring, nr_ring_vectors;

	nr_ring_vectors = ndev->msix.nr_entries - 1;

	/* clear pkt ring irqs */
	while (i < nr_ring_vectors) {
		if (test_and_clear_bit(i, ndev->msix.irqs)) {
			ring = (i / NR_RING_VECTORS);
			irq_set_affinity_hint(msix_ent[i].vector, NULL);
			free_irq(msix_ent[i].vector, &ndev->bh.slc[ring]);
		}
		i += NR_RING_VECTORS;
	}
	irq_set_affinity_hint(msix_ent[i].vector, NULL);
	free_irq(msix_ent[i].vector, ndev);
	clear_bit(i, ndev->msix.irqs);

	kfree(ndev->msix.entries);
	for (i = 0; i < ndev->msix.nr_entries; i++)
		kfree(*(names + i));

	kfree(names);
	pci_disable_msix(ndev->pdev);
}

/**
 * nitrox_pf_cleanup_isr: Cleanup PF MSI-X and IRQ
 * @ndev: NITROX device
 */
void nitrox_pf_cleanup_isr(struct nitrox_device *ndev)
{
	nitrox_disable_msix(ndev);
	nitrox_cleanup_pkt_slc_bh(ndev);
}

/**
 * nitrox_init_isr - Initialize PF MSI-X vectors and IRQ
 * @ndev: NITROX device
 *
 * Return: 0 on success, a negative value on failure.
 */
int nitrox_pf_init_isr(struct nitrox_device *ndev)
{
	int err;

	err = nitrox_setup_pkt_slc_bh(ndev);
	if (err)
		return err;

	err = nitrox_enable_msix(ndev);
	if (err)
		goto msix_fail;

	err = nitrox_request_irqs(ndev);
	if (err)
		goto irq_fail;

	return 0;

irq_fail:
	nitrox_disable_msix(ndev);
msix_fail:
	nitrox_cleanup_pkt_slc_bh(ndev);
	return err;
}
