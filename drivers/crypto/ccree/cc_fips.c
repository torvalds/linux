// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2012-2019 ARM Limited (or its affiliates). */

#include <linux/kernel.h>
#include <linux/fips.h>
#include <linux/notifier.h>

#include "cc_driver.h"
#include "cc_fips.h"

static void fips_dsr(unsigned long devarg);

struct cc_fips_handle {
	struct tasklet_struct tasklet;
	struct notifier_block nb;
	struct cc_drvdata *drvdata;
};

/* The function called once at driver entry point to check
 * whether TEE FIPS error occurred.
 */
static bool cc_get_tee_fips_status(struct cc_drvdata *drvdata)
{
	u32 reg;

	reg = cc_ioread(drvdata, CC_REG(GPR_HOST));
	/* Did the TEE report status? */
	if (reg & CC_FIPS_SYNC_TEE_STATUS)
		/* Yes. Is it OK? */
		return (reg & CC_FIPS_SYNC_MODULE_OK);

	/* No. It's either not in use or will be reported later */
	return true;
}

/*
 * This function should push the FIPS REE library status towards the TEE library
 * by writing the error state to HOST_GPR0 register.
 */
void cc_set_ree_fips_status(struct cc_drvdata *drvdata, bool status)
{
	int val = CC_FIPS_SYNC_REE_STATUS;

	if (drvdata->hw_rev < CC_HW_REV_712)
		return;

	val |= (status ? CC_FIPS_SYNC_MODULE_OK : CC_FIPS_SYNC_MODULE_ERROR);

	cc_iowrite(drvdata, CC_REG(HOST_GPR0), val);
}

/* Push REE side FIPS test failure to TEE side */
static int cc_ree_fips_failure(struct notifier_block *nb, unsigned long unused1,
			       void *unused2)
{
	struct cc_fips_handle *fips_h =
				container_of(nb, struct cc_fips_handle, nb);
	struct cc_drvdata *drvdata = fips_h->drvdata;
	struct device *dev = drvdata_to_dev(drvdata);

	cc_set_ree_fips_status(drvdata, false);
	dev_info(dev, "Notifying TEE of FIPS test failure...\n");

	return NOTIFY_OK;
}

void cc_fips_fini(struct cc_drvdata *drvdata)
{
	struct cc_fips_handle *fips_h = drvdata->fips_handle;

	if (drvdata->hw_rev < CC_HW_REV_712 || !fips_h)
		return;

	atomic_notifier_chain_unregister(&fips_fail_notif_chain, &fips_h->nb);

	/* Kill tasklet */
	tasklet_kill(&fips_h->tasklet);
	drvdata->fips_handle = NULL;
}

void fips_handler(struct cc_drvdata *drvdata)
{
	struct cc_fips_handle *fips_handle_ptr = drvdata->fips_handle;

	if (drvdata->hw_rev < CC_HW_REV_712)
		return;

	tasklet_schedule(&fips_handle_ptr->tasklet);
}

static inline void tee_fips_error(struct device *dev)
{
	if (fips_enabled)
		panic("ccree: TEE reported cryptographic error in fips mode!\n");
	else
		dev_err(dev, "TEE reported error!\n");
}

/*
 * This function check if cryptocell tee fips error occurred
 * and in such case triggers system error
 */
void cc_tee_handle_fips_error(struct cc_drvdata *p_drvdata)
{
	struct device *dev = drvdata_to_dev(p_drvdata);

	if (!cc_get_tee_fips_status(p_drvdata))
		tee_fips_error(dev);
}

/* Deferred service handler, run as interrupt-fired tasklet */
static void fips_dsr(unsigned long devarg)
{
	struct cc_drvdata *drvdata = (struct cc_drvdata *)devarg;
	u32 irq, val;

	irq = (drvdata->irq & (CC_GPR0_IRQ_MASK));

	if (irq) {
		cc_tee_handle_fips_error(drvdata);
	}

	/* after verifying that there is nothing to do,
	 * unmask AXI completion interrupt.
	 */
	val = (CC_REG(HOST_IMR) & ~irq);
	cc_iowrite(drvdata, CC_REG(HOST_IMR), val);
}

/* The function called once at driver entry point .*/
int cc_fips_init(struct cc_drvdata *p_drvdata)
{
	struct cc_fips_handle *fips_h;
	struct device *dev = drvdata_to_dev(p_drvdata);

	if (p_drvdata->hw_rev < CC_HW_REV_712)
		return 0;

	fips_h = devm_kzalloc(dev, sizeof(*fips_h), GFP_KERNEL);
	if (!fips_h)
		return -ENOMEM;

	p_drvdata->fips_handle = fips_h;

	dev_dbg(dev, "Initializing fips tasklet\n");
	tasklet_init(&fips_h->tasklet, fips_dsr, (unsigned long)p_drvdata);
	fips_h->drvdata = p_drvdata;
	fips_h->nb.notifier_call = cc_ree_fips_failure;
	atomic_notifier_chain_register(&fips_fail_notif_chain, &fips_h->nb);

	cc_tee_handle_fips_error(p_drvdata);

	return 0;
}
