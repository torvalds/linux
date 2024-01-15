// SPDX-License-Identifier: GPL-2.0
/* Marvell PTP driver
 *
 * Copyright (C) 2020 Marvell.
 *
 */

#include <linux/bitfield.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>

#include "mbox.h"
#include "ptp.h"
#include "rvu.h"

#define DRV_NAME				"Marvell PTP Driver"

#define PCI_DEVID_OCTEONTX2_PTP			0xA00C
#define PCI_SUBSYS_DEVID_OCTX2_98xx_PTP		0xB100
#define PCI_SUBSYS_DEVID_OCTX2_96XX_PTP		0xB200
#define PCI_SUBSYS_DEVID_OCTX2_95XX_PTP		0xB300
#define PCI_SUBSYS_DEVID_OCTX2_95XXN_PTP	0xB400
#define PCI_SUBSYS_DEVID_OCTX2_95MM_PTP		0xB500
#define PCI_SUBSYS_DEVID_OCTX2_95XXO_PTP	0xB600
#define PCI_DEVID_OCTEONTX2_RST			0xA085
#define PCI_DEVID_CN10K_PTP			0xA09E
#define PCI_SUBSYS_DEVID_CN10K_A_PTP		0xB900
#define PCI_SUBSYS_DEVID_CNF10K_A_PTP		0xBA00
#define PCI_SUBSYS_DEVID_CNF10K_B_PTP		0xBC00

#define PCI_PTP_BAR_NO				0

#define PTP_CLOCK_CFG				0xF00ULL
#define PTP_CLOCK_CFG_PTP_EN			BIT_ULL(0)
#define PTP_CLOCK_CFG_EXT_CLK_EN		BIT_ULL(1)
#define PTP_CLOCK_CFG_EXT_CLK_IN_MASK		GENMASK_ULL(7, 2)
#define PTP_CLOCK_CFG_TSTMP_EDGE		BIT_ULL(9)
#define PTP_CLOCK_CFG_TSTMP_EN			BIT_ULL(8)
#define PTP_CLOCK_CFG_TSTMP_IN_MASK		GENMASK_ULL(15, 10)
#define PTP_CLOCK_CFG_ATOMIC_OP_MASK		GENMASK_ULL(28, 26)
#define PTP_CLOCK_CFG_PPS_EN			BIT_ULL(30)
#define PTP_CLOCK_CFG_PPS_INV			BIT_ULL(31)

#define PTP_PPS_HI_INCR				0xF60ULL
#define PTP_PPS_LO_INCR				0xF68ULL
#define PTP_PPS_THRESH_LO			0xF50ULL
#define PTP_PPS_THRESH_HI			0xF58ULL

#define PTP_CLOCK_LO				0xF08ULL
#define PTP_CLOCK_HI				0xF10ULL
#define PTP_CLOCK_COMP				0xF18ULL
#define PTP_TIMESTAMP				0xF20ULL
#define PTP_CLOCK_SEC				0xFD0ULL
#define PTP_SEC_ROLLOVER			0xFD8ULL
/* Atomic update related CSRs */
#define PTP_FRNS_TIMESTAMP			0xFE0ULL
#define PTP_NXT_ROLLOVER_SET			0xFE8ULL
#define PTP_CURR_ROLLOVER_SET			0xFF0ULL
#define PTP_NANO_TIMESTAMP			0xFF8ULL
#define PTP_SEC_TIMESTAMP			0x1000ULL

#define CYCLE_MULT				1000

#define is_rev_A0(ptp) (((ptp)->pdev->revision & 0x0F) == 0x0)
#define is_rev_A1(ptp) (((ptp)->pdev->revision & 0x0F) == 0x1)

/* PTP atomic update operation type */
enum atomic_opcode {
	ATOMIC_SET = 1,
	ATOMIC_INC = 3,
	ATOMIC_DEC = 4
};

static struct ptp *first_ptp_block;
static const struct pci_device_id ptp_id_table[];

static bool is_ptp_dev_cnf10ka(struct ptp *ptp)
{
	return ptp->pdev->subsystem_device == PCI_SUBSYS_DEVID_CNF10K_A_PTP;
}

static bool is_ptp_dev_cn10ka(struct ptp *ptp)
{
	return ptp->pdev->subsystem_device == PCI_SUBSYS_DEVID_CN10K_A_PTP;
}

static bool cn10k_ptp_errata(struct ptp *ptp)
{
	if ((is_ptp_dev_cn10ka(ptp) || is_ptp_dev_cnf10ka(ptp)) &&
	    (is_rev_A0(ptp) || is_rev_A1(ptp)))
		return true;

	return false;
}

static bool is_tstmp_atomic_update_supported(struct rvu *rvu)
{
	struct ptp *ptp = rvu->ptp;

	if (is_rvu_otx2(rvu))
		return false;

	/* On older silicon variants of CN10K, atomic update feature
	 * is not available.
	 */
	if ((is_ptp_dev_cn10ka(ptp) || is_ptp_dev_cnf10ka(ptp)) &&
	    (is_rev_A0(ptp) || is_rev_A1(ptp)))
		return false;

	return true;
}

static enum hrtimer_restart ptp_reset_thresh(struct hrtimer *hrtimer)
{
	struct ptp *ptp = container_of(hrtimer, struct ptp, hrtimer);
	ktime_t curr_ts = ktime_get();
	ktime_t delta_ns, period_ns;
	u64 ptp_clock_hi;

	/* calculate the elapsed time since last restart */
	delta_ns = ktime_to_ns(ktime_sub(curr_ts, ptp->last_ts));

	/* if the ptp clock value has crossed 0.5 seconds,
	 * its too late to update pps threshold value, so
	 * update threshold after 1 second.
	 */
	ptp_clock_hi = readq(ptp->reg_base + PTP_CLOCK_HI);
	if (ptp_clock_hi > 500000000) {
		period_ns = ktime_set(0, (NSEC_PER_SEC + 100 - ptp_clock_hi));
	} else {
		writeq(500000000, ptp->reg_base + PTP_PPS_THRESH_HI);
		period_ns = ktime_set(0, (NSEC_PER_SEC + 100 - delta_ns));
	}

	hrtimer_forward_now(hrtimer, period_ns);
	ptp->last_ts = curr_ts;

	return HRTIMER_RESTART;
}

static void ptp_hrtimer_start(struct ptp *ptp, ktime_t start_ns)
{
	ktime_t period_ns;

	period_ns = ktime_set(0, (NSEC_PER_SEC + 100 - start_ns));
	hrtimer_start(&ptp->hrtimer, period_ns, HRTIMER_MODE_REL);
	ptp->last_ts = ktime_get();
}

static u64 read_ptp_tstmp_sec_nsec(struct ptp *ptp)
{
	u64 sec, sec1, nsec;
	unsigned long flags;

	spin_lock_irqsave(&ptp->ptp_lock, flags);
	sec = readq(ptp->reg_base + PTP_CLOCK_SEC) & 0xFFFFFFFFUL;
	nsec = readq(ptp->reg_base + PTP_CLOCK_HI);
	sec1 = readq(ptp->reg_base + PTP_CLOCK_SEC) & 0xFFFFFFFFUL;
	/* check nsec rollover */
	if (sec1 > sec) {
		nsec = readq(ptp->reg_base + PTP_CLOCK_HI);
		sec = sec1;
	}
	spin_unlock_irqrestore(&ptp->ptp_lock, flags);

	return sec * NSEC_PER_SEC + nsec;
}

static u64 read_ptp_tstmp_nsec(struct ptp *ptp)
{
	return readq(ptp->reg_base + PTP_CLOCK_HI);
}

static u64 ptp_calc_adjusted_comp(u64 ptp_clock_freq)
{
	u64 comp, adj = 0, cycles_per_sec, ns_drift = 0;
	u32 ptp_clock_nsec, cycle_time;
	int cycle;

	/* Errata:
	 * Issue #1: At the time of 1 sec rollover of the nano-second counter,
	 * the nano-second counter is set to 0. However, it should be set to
	 * (existing counter_value - 10^9).
	 *
	 * Issue #2: The nano-second counter rolls over at 0x3B9A_C9FF.
	 * It should roll over at 0x3B9A_CA00.
	 */

	/* calculate ptp_clock_comp value */
	comp = ((u64)1000000000ULL << 32) / ptp_clock_freq;
	/* use CYCLE_MULT to avoid accuracy loss due to integer arithmetic */
	cycle_time = NSEC_PER_SEC * CYCLE_MULT / ptp_clock_freq;
	/* cycles per sec */
	cycles_per_sec = ptp_clock_freq;

	/* check whether ptp nanosecond counter rolls over early */
	cycle = cycles_per_sec - 1;
	ptp_clock_nsec = (cycle * comp) >> 32;
	while (ptp_clock_nsec < NSEC_PER_SEC) {
		if (ptp_clock_nsec == 0x3B9AC9FF)
			goto calc_adj_comp;
		cycle++;
		ptp_clock_nsec = (cycle * comp) >> 32;
	}
	/* compute nanoseconds lost per second when nsec counter rolls over */
	ns_drift = ptp_clock_nsec - NSEC_PER_SEC;
	/* calculate ptp_clock_comp adjustment */
	if (ns_drift > 0) {
		adj = comp * ns_drift;
		adj = adj / 1000000000ULL;
	}
	/* speed up the ptp clock to account for nanoseconds lost */
	comp += adj;
	return comp;

calc_adj_comp:
	/* slow down the ptp clock to not rollover early */
	adj = comp * cycle_time;
	adj = adj / 1000000000ULL;
	adj = adj / CYCLE_MULT;
	comp -= adj;

	return comp;
}

struct ptp *ptp_get(void)
{
	struct ptp *ptp = first_ptp_block;

	/* Check PTP block is present in hardware */
	if (!pci_dev_present(ptp_id_table))
		return ERR_PTR(-ENODEV);
	/* Check driver is bound to PTP block */
	if (!ptp)
		ptp = ERR_PTR(-EPROBE_DEFER);
	else if (!IS_ERR(ptp))
		pci_dev_get(ptp->pdev);

	return ptp;
}

void ptp_put(struct ptp *ptp)
{
	if (!ptp)
		return;

	pci_dev_put(ptp->pdev);
}

static void ptp_atomic_update(struct ptp *ptp, u64 timestamp)
{
	u64 regval, curr_rollover_set, nxt_rollover_set;

	/* First setup NSECs and SECs */
	writeq(timestamp, ptp->reg_base + PTP_NANO_TIMESTAMP);
	writeq(0, ptp->reg_base + PTP_FRNS_TIMESTAMP);
	writeq(timestamp / NSEC_PER_SEC,
	       ptp->reg_base + PTP_SEC_TIMESTAMP);

	nxt_rollover_set = roundup(timestamp, NSEC_PER_SEC);
	curr_rollover_set = nxt_rollover_set - NSEC_PER_SEC;
	writeq(nxt_rollover_set, ptp->reg_base + PTP_NXT_ROLLOVER_SET);
	writeq(curr_rollover_set, ptp->reg_base + PTP_CURR_ROLLOVER_SET);

	/* Now, initiate atomic update */
	regval = readq(ptp->reg_base + PTP_CLOCK_CFG);
	regval &= ~PTP_CLOCK_CFG_ATOMIC_OP_MASK;
	regval |= (ATOMIC_SET << 26);
	writeq(regval, ptp->reg_base + PTP_CLOCK_CFG);
}

static void ptp_atomic_adjtime(struct ptp *ptp, s64 delta)
{
	bool neg_adj = false, atomic_inc_dec = false;
	u64 regval, ptp_clock_hi;

	if (delta < 0) {
		delta = -delta;
		neg_adj = true;
	}

	/* use atomic inc/dec when delta < 1 second */
	if (delta < NSEC_PER_SEC)
		atomic_inc_dec = true;

	if (!atomic_inc_dec) {
		ptp_clock_hi = readq(ptp->reg_base + PTP_CLOCK_HI);
		if (neg_adj) {
			if (ptp_clock_hi > delta)
				ptp_clock_hi -= delta;
			else
				ptp_clock_hi = delta - ptp_clock_hi;
		} else {
			ptp_clock_hi += delta;
		}
		ptp_atomic_update(ptp, ptp_clock_hi);
	} else {
		writeq(delta, ptp->reg_base + PTP_NANO_TIMESTAMP);
		writeq(0, ptp->reg_base + PTP_FRNS_TIMESTAMP);

		/* initiate atomic inc/dec */
		regval = readq(ptp->reg_base + PTP_CLOCK_CFG);
		regval &= ~PTP_CLOCK_CFG_ATOMIC_OP_MASK;
		regval |= neg_adj ? (ATOMIC_DEC << 26) : (ATOMIC_INC << 26);
		writeq(regval, ptp->reg_base + PTP_CLOCK_CFG);
	}
}

static int ptp_adjfine(struct ptp *ptp, long scaled_ppm)
{
	bool neg_adj = false;
	u32 freq, freq_adj;
	u64 comp, adj;
	s64 ppb;

	if (scaled_ppm < 0) {
		neg_adj = true;
		scaled_ppm = -scaled_ppm;
	}

	/* The hardware adds the clock compensation value to the PTP clock
	 * on every coprocessor clock cycle. Typical convention is that it
	 * represent number of nanosecond betwen each cycle. In this
	 * convention compensation value is in 64 bit fixed-point
	 * representation where upper 32 bits are number of nanoseconds
	 * and lower is fractions of nanosecond.
	 * The scaled_ppm represent the ratio in "parts per million" by which
	 * the compensation value should be corrected.
	 * To calculate new compenstation value we use 64bit fixed point
	 * arithmetic on following formula
	 * comp = tbase + tbase * scaled_ppm / (1M * 2^16)
	 * where tbase is the basic compensation value calculated
	 * initialy in the probe function.
	 */
	/* convert scaled_ppm to ppb */
	ppb = 1 + scaled_ppm;
	ppb *= 125;
	ppb >>= 13;

	if (cn10k_ptp_errata(ptp)) {
		/* calculate the new frequency based on ppb */
		freq_adj = (ptp->clock_rate * ppb) / 1000000000ULL;
		freq = neg_adj ? ptp->clock_rate + freq_adj : ptp->clock_rate - freq_adj;
		comp = ptp_calc_adjusted_comp(freq);
	} else {
		comp = ((u64)1000000000ull << 32) / ptp->clock_rate;
		adj = comp * ppb;
		adj = div_u64(adj, 1000000000ull);
		comp = neg_adj ? comp - adj : comp + adj;
	}
	writeq(comp, ptp->reg_base + PTP_CLOCK_COMP);

	return 0;
}

static int ptp_get_clock(struct ptp *ptp, u64 *clk)
{
	/* Return the current PTP clock */
	*clk = ptp->read_ptp_tstmp(ptp);

	return 0;
}

void ptp_start(struct rvu *rvu, u64 sclk, u32 ext_clk_freq, u32 extts)
{
	struct ptp *ptp = rvu->ptp;
	struct pci_dev *pdev;
	u64 clock_comp;
	u64 clock_cfg;

	if (!ptp)
		return;

	pdev = ptp->pdev;

	if (!sclk) {
		dev_err(&pdev->dev, "PTP input clock cannot be zero\n");
		return;
	}

	/* sclk is in MHz */
	ptp->clock_rate = sclk * 1000000;

	/* Program the seconds rollover value to 1 second */
	if (is_tstmp_atomic_update_supported(rvu)) {
		writeq(0, ptp->reg_base + PTP_NANO_TIMESTAMP);
		writeq(0, ptp->reg_base + PTP_FRNS_TIMESTAMP);
		writeq(0, ptp->reg_base + PTP_SEC_TIMESTAMP);
		writeq(0, ptp->reg_base + PTP_CURR_ROLLOVER_SET);
		writeq(0x3b9aca00, ptp->reg_base + PTP_NXT_ROLLOVER_SET);
		writeq(0x3b9aca00, ptp->reg_base + PTP_SEC_ROLLOVER);
	}

	/* Enable PTP clock */
	clock_cfg = readq(ptp->reg_base + PTP_CLOCK_CFG);

	if (ext_clk_freq) {
		ptp->clock_rate = ext_clk_freq;
		/* Set GPIO as PTP clock source */
		clock_cfg &= ~PTP_CLOCK_CFG_EXT_CLK_IN_MASK;
		clock_cfg |= PTP_CLOCK_CFG_EXT_CLK_EN;
	}

	if (extts) {
		clock_cfg |= PTP_CLOCK_CFG_TSTMP_EDGE;
		/* Set GPIO as timestamping source */
		clock_cfg &= ~PTP_CLOCK_CFG_TSTMP_IN_MASK;
		clock_cfg |= PTP_CLOCK_CFG_TSTMP_EN;
	}

	clock_cfg |= PTP_CLOCK_CFG_PTP_EN;
	writeq(clock_cfg, ptp->reg_base + PTP_CLOCK_CFG);
	clock_cfg = readq(ptp->reg_base + PTP_CLOCK_CFG);
	clock_cfg &= ~PTP_CLOCK_CFG_ATOMIC_OP_MASK;
	clock_cfg |= (ATOMIC_SET << 26);
	writeq(clock_cfg, ptp->reg_base + PTP_CLOCK_CFG);

	if (cn10k_ptp_errata(ptp))
		clock_comp = ptp_calc_adjusted_comp(ptp->clock_rate);
	else
		clock_comp = ((u64)1000000000ull << 32) / ptp->clock_rate;

	/* Initial compensation value to start the nanosecs counter */
	writeq(clock_comp, ptp->reg_base + PTP_CLOCK_COMP);
}

static int ptp_get_tstmp(struct ptp *ptp, u64 *clk)
{
	u64 timestamp;

	if (is_ptp_dev_cn10ka(ptp) || is_ptp_dev_cnf10ka(ptp)) {
		timestamp = readq(ptp->reg_base + PTP_TIMESTAMP);
		*clk = (timestamp >> 32) * NSEC_PER_SEC + (timestamp & 0xFFFFFFFF);
	} else {
		*clk = readq(ptp->reg_base + PTP_TIMESTAMP);
	}

	return 0;
}

static int ptp_set_thresh(struct ptp *ptp, u64 thresh)
{
	if (!cn10k_ptp_errata(ptp))
		writeq(thresh, ptp->reg_base + PTP_PPS_THRESH_HI);

	return 0;
}

static int ptp_config_hrtimer(struct ptp *ptp, int on)
{
	u64 ptp_clock_hi;

	if (on) {
		ptp_clock_hi = readq(ptp->reg_base + PTP_CLOCK_HI);
		ptp_hrtimer_start(ptp, (ktime_t)ptp_clock_hi);
	} else {
		if (hrtimer_active(&ptp->hrtimer))
			hrtimer_cancel(&ptp->hrtimer);
	}

	return 0;
}

static int ptp_pps_on(struct ptp *ptp, int on, u64 period)
{
	u64 clock_cfg;

	clock_cfg = readq(ptp->reg_base + PTP_CLOCK_CFG);
	if (on) {
		if (cn10k_ptp_errata(ptp) && period != NSEC_PER_SEC) {
			dev_err(&ptp->pdev->dev, "Supports max period value as 1 second\n");
			return -EINVAL;
		}

		if (period > (8 * NSEC_PER_SEC)) {
			dev_err(&ptp->pdev->dev, "Supports max period as 8 seconds\n");
			return -EINVAL;
		}

		clock_cfg |= PTP_CLOCK_CFG_PPS_EN | PTP_CLOCK_CFG_PPS_INV;
		writeq(clock_cfg, ptp->reg_base + PTP_CLOCK_CFG);

		writeq(0, ptp->reg_base + PTP_PPS_THRESH_HI);
		writeq(0, ptp->reg_base + PTP_PPS_THRESH_LO);

		/* Configure high/low phase time */
		period = period / 2;
		writeq(((u64)period << 32), ptp->reg_base + PTP_PPS_HI_INCR);
		writeq(((u64)period << 32), ptp->reg_base + PTP_PPS_LO_INCR);
	} else {
		clock_cfg &= ~(PTP_CLOCK_CFG_PPS_EN | PTP_CLOCK_CFG_PPS_INV);
		writeq(clock_cfg, ptp->reg_base + PTP_CLOCK_CFG);
	}

	if (on && cn10k_ptp_errata(ptp)) {
		/* The ptp_clock_hi rollsover to zero once clock cycle before it
		 * reaches one second boundary. so, program the pps_lo_incr in
		 * such a way that the pps threshold value comparison at one
		 * second boundary will succeed and pps edge changes. After each
		 * one second boundary, the hrtimer handler will be invoked and
		 * reprograms the pps threshold value.
		 */
		ptp->clock_period = NSEC_PER_SEC / ptp->clock_rate;
		writeq((0x1dcd6500ULL - ptp->clock_period) << 32,
		       ptp->reg_base + PTP_PPS_LO_INCR);
	}

	if (cn10k_ptp_errata(ptp))
		ptp_config_hrtimer(ptp, on);

	return 0;
}

static int ptp_probe(struct pci_dev *pdev,
		     const struct pci_device_id *ent)
{
	struct ptp *ptp;
	int err;

	ptp = kzalloc(sizeof(*ptp), GFP_KERNEL);
	if (!ptp) {
		err = -ENOMEM;
		goto error;
	}

	ptp->pdev = pdev;

	err = pcim_enable_device(pdev);
	if (err)
		goto error_free;

	err = pcim_iomap_regions(pdev, 1 << PCI_PTP_BAR_NO, pci_name(pdev));
	if (err)
		goto error_free;

	ptp->reg_base = pcim_iomap_table(pdev)[PCI_PTP_BAR_NO];

	pci_set_drvdata(pdev, ptp);
	if (!first_ptp_block)
		first_ptp_block = ptp;

	spin_lock_init(&ptp->ptp_lock);
	if (cn10k_ptp_errata(ptp)) {
		ptp->read_ptp_tstmp = &read_ptp_tstmp_sec_nsec;
		hrtimer_init(&ptp->hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
		ptp->hrtimer.function = ptp_reset_thresh;
	} else {
		ptp->read_ptp_tstmp = &read_ptp_tstmp_nsec;
	}

	return 0;

error_free:
	kfree(ptp);

error:
	/* For `ptp_get()` we need to differentiate between the case
	 * when the core has not tried to probe this device and the case when
	 * the probe failed.  In the later case we keep the error in
	 * `dev->driver_data`.
	 */
	pci_set_drvdata(pdev, ERR_PTR(err));
	if (!first_ptp_block)
		first_ptp_block = ERR_PTR(err);

	return err;
}

static void ptp_remove(struct pci_dev *pdev)
{
	struct ptp *ptp = pci_get_drvdata(pdev);
	u64 clock_cfg;

	if (IS_ERR_OR_NULL(ptp))
		return;

	if (cn10k_ptp_errata(ptp) && hrtimer_active(&ptp->hrtimer))
		hrtimer_cancel(&ptp->hrtimer);

	/* Disable PTP clock */
	clock_cfg = readq(ptp->reg_base + PTP_CLOCK_CFG);
	clock_cfg &= ~PTP_CLOCK_CFG_PTP_EN;
	writeq(clock_cfg, ptp->reg_base + PTP_CLOCK_CFG);
	kfree(ptp);
}

static const struct pci_device_id ptp_id_table[] = {
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_CAVIUM, PCI_DEVID_OCTEONTX2_PTP,
			 PCI_VENDOR_ID_CAVIUM,
			 PCI_SUBSYS_DEVID_OCTX2_98xx_PTP) },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_CAVIUM, PCI_DEVID_OCTEONTX2_PTP,
			 PCI_VENDOR_ID_CAVIUM,
			 PCI_SUBSYS_DEVID_OCTX2_96XX_PTP) },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_CAVIUM, PCI_DEVID_OCTEONTX2_PTP,
			 PCI_VENDOR_ID_CAVIUM,
			 PCI_SUBSYS_DEVID_OCTX2_95XX_PTP) },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_CAVIUM, PCI_DEVID_OCTEONTX2_PTP,
			 PCI_VENDOR_ID_CAVIUM,
			 PCI_SUBSYS_DEVID_OCTX2_95XXN_PTP) },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_CAVIUM, PCI_DEVID_OCTEONTX2_PTP,
			 PCI_VENDOR_ID_CAVIUM,
			 PCI_SUBSYS_DEVID_OCTX2_95MM_PTP) },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_CAVIUM, PCI_DEVID_OCTEONTX2_PTP,
			 PCI_VENDOR_ID_CAVIUM,
			 PCI_SUBSYS_DEVID_OCTX2_95XXO_PTP) },
	{ PCI_DEVICE(PCI_VENDOR_ID_CAVIUM, PCI_DEVID_CN10K_PTP) },
	{ 0, }
};

struct pci_driver ptp_driver = {
	.name = DRV_NAME,
	.id_table = ptp_id_table,
	.probe = ptp_probe,
	.remove = ptp_remove,
};

int rvu_mbox_handler_ptp_op(struct rvu *rvu, struct ptp_req *req,
			    struct ptp_rsp *rsp)
{
	int err = 0;

	/* This function is the PTP mailbox handler invoked when
	 * called by AF consumers/netdev drivers via mailbox mechanism.
	 * It is used by netdev driver to get the PTP clock and to set
	 * frequency adjustments. Since mailbox can be called without
	 * notion of whether the driver is bound to ptp device below
	 * validation is needed as first step.
	 */
	if (!rvu->ptp)
		return -ENODEV;

	switch (req->op) {
	case PTP_OP_ADJFINE:
		err = ptp_adjfine(rvu->ptp, req->scaled_ppm);
		break;
	case PTP_OP_GET_CLOCK:
		err = ptp_get_clock(rvu->ptp, &rsp->clk);
		break;
	case PTP_OP_GET_TSTMP:
		err = ptp_get_tstmp(rvu->ptp, &rsp->clk);
		break;
	case PTP_OP_SET_THRESH:
		err = ptp_set_thresh(rvu->ptp, req->thresh);
		break;
	case PTP_OP_PPS_ON:
		err = ptp_pps_on(rvu->ptp, req->pps_on, req->period);
		break;
	case PTP_OP_ADJTIME:
		ptp_atomic_adjtime(rvu->ptp, req->delta);
		break;
	case PTP_OP_SET_CLOCK:
		ptp_atomic_update(rvu->ptp, (u64)req->clk);
		break;
	default:
		err = -EINVAL;
		break;
	}

	return err;
}

int rvu_mbox_handler_ptp_get_cap(struct rvu *rvu, struct msg_req *req,
				 struct ptp_get_cap_rsp *rsp)
{
	if (!rvu->ptp)
		return -ENODEV;

	if (is_tstmp_atomic_update_supported(rvu))
		rsp->cap |= PTP_CAP_HW_ATOMIC_UPDATE;
	else
		rsp->cap &= ~BIT_ULL_MASK(0);

	return 0;
}
