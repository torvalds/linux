/*
 * Copyright (C) 2012-2017 ARM Limited or its affiliates.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/kernel.h>
#include <linux/fips.h>

#include "ssi_config.h"
#include "ssi_driver.h"
#include "ssi_fips.h"

static void fips_dsr(unsigned long devarg);

struct ssi_fips_handle {
	struct tasklet_struct tasklet;
};

/* The function called once at driver entry point to check
 * whether TEE FIPS error occurred.
 */
static bool cc_get_tee_fips_status(struct ssi_drvdata *drvdata)
{
	u32 reg;

	reg = cc_ioread(drvdata, CC_REG(GPR_HOST));
	return (reg == (CC_FIPS_SYNC_TEE_STATUS | CC_FIPS_SYNC_MODULE_OK));
}

/*
 * This function should push the FIPS REE library status towards the TEE library
 * by writing the error state to HOST_GPR0 register.
 */
void cc_set_ree_fips_status(struct ssi_drvdata *drvdata, bool status)
{
	int val = CC_FIPS_SYNC_REE_STATUS;

	val |= (status ? CC_FIPS_SYNC_MODULE_OK : CC_FIPS_SYNC_MODULE_ERROR);

	cc_iowrite(drvdata, CC_REG(HOST_GPR0), val);
}

void ssi_fips_fini(struct ssi_drvdata *drvdata)
{
	struct ssi_fips_handle *fips_h = drvdata->fips_handle;

	if (!fips_h)
		return; /* Not allocated */

	/* Kill tasklet */
	tasklet_kill(&fips_h->tasklet);

	kfree(fips_h);
	drvdata->fips_handle = NULL;
}

void fips_handler(struct ssi_drvdata *drvdata)
{
	struct ssi_fips_handle *fips_handle_ptr =
		drvdata->fips_handle;

	tasklet_schedule(&fips_handle_ptr->tasklet);
}

static inline void tee_fips_error(struct device *dev)
{
	if (fips_enabled)
		panic("ccree: TEE reported cryptographic error in fips mode!\n");
	else
		dev_err(dev, "TEE reported error!\n");
}

/* Deferred service handler, run as interrupt-fired tasklet */
static void fips_dsr(unsigned long devarg)
{
	struct ssi_drvdata *drvdata = (struct ssi_drvdata *)devarg;
	struct device *dev = drvdata_to_dev(drvdata);
	u32 irq, state, val;

	irq = (drvdata->irq & (SSI_GPR0_IRQ_MASK));

	if (irq) {
		state = cc_ioread(drvdata, CC_REG(GPR_HOST));

		if (state != (CC_FIPS_SYNC_TEE_STATUS | CC_FIPS_SYNC_MODULE_OK))
			tee_fips_error(dev);
	}

	/* after verifing that there is nothing to do,
	 * unmask AXI completion interrupt.
	 */
	val = (CC_REG(HOST_IMR) & ~irq);
	cc_iowrite(drvdata, CC_REG(HOST_IMR), val);
}

/* The function called once at driver entry point .*/
int ssi_fips_init(struct ssi_drvdata *p_drvdata)
{
	struct ssi_fips_handle *fips_h;
	struct device *dev = drvdata_to_dev(p_drvdata);

	fips_h = kzalloc(sizeof(*fips_h), GFP_KERNEL);
	if (!fips_h)
		return -ENOMEM;

	p_drvdata->fips_handle = fips_h;

	dev_dbg(dev, "Initializing fips tasklet\n");
	tasklet_init(&fips_h->tasklet, fips_dsr, (unsigned long)p_drvdata);

	if (!cc_get_tee_fips_status(p_drvdata))
		tee_fips_error(dev);

	return 0;
}
