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

/**************************************************************
This file defines the driver FIPS internal function, used by the driver itself.
***************************************************************/
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <crypto/des.h>

#include "ssi_config.h"
#include "ssi_driver.h"
#include "cc_hal.h"


#define FIPS_POWER_UP_TEST_CIPHER	1
#define FIPS_POWER_UP_TEST_CMAC		1
#define FIPS_POWER_UP_TEST_HASH		1
#define FIPS_POWER_UP_TEST_HMAC		1
#define FIPS_POWER_UP_TEST_CCM		1
#define FIPS_POWER_UP_TEST_GCM		1

static bool ssi_fips_support = 1;
module_param(ssi_fips_support, bool, 0644);
MODULE_PARM_DESC(ssi_fips_support, "FIPS supported flag: 0 - off , 1 - on (default)");

static void fips_dsr(unsigned long devarg);

struct ssi_fips_handle {
#ifdef COMP_IN_WQ
	struct workqueue_struct *workq;
	struct delayed_work fipswork;
#else
	struct tasklet_struct fipstask;
#endif
};


extern int ssi_fips_get_state(ssi_fips_state_t *p_state);
extern int ssi_fips_get_error(ssi_fips_error_t *p_err);
extern int ssi_fips_ext_set_state(ssi_fips_state_t state);
extern int ssi_fips_ext_set_error(ssi_fips_error_t err);

/* FIPS power-up tests */
extern ssi_fips_error_t ssi_cipher_fips_power_up_tests(struct ssi_drvdata *drvdata, void *cpu_addr_buffer, dma_addr_t dma_coherent_buffer);
extern ssi_fips_error_t ssi_cmac_fips_power_up_tests(struct ssi_drvdata *drvdata, void *cpu_addr_buffer, dma_addr_t dma_coherent_buffer);
extern ssi_fips_error_t ssi_hash_fips_power_up_tests(struct ssi_drvdata *drvdata, void *cpu_addr_buffer, dma_addr_t dma_coherent_buffer);
extern ssi_fips_error_t ssi_hmac_fips_power_up_tests(struct ssi_drvdata *drvdata, void *cpu_addr_buffer, dma_addr_t dma_coherent_buffer);
extern ssi_fips_error_t ssi_ccm_fips_power_up_tests(struct ssi_drvdata *drvdata, void *cpu_addr_buffer, dma_addr_t dma_coherent_buffer);
extern ssi_fips_error_t ssi_gcm_fips_power_up_tests(struct ssi_drvdata *drvdata, void *cpu_addr_buffer, dma_addr_t dma_coherent_buffer);
extern size_t ssi_fips_max_mem_alloc_size(void);


/* The function called once at driver entry point to check whether TEE FIPS error occured.*/
static enum ssi_fips_error ssi_fips_get_tee_error(struct ssi_drvdata *drvdata)
{
	uint32_t regVal;
	void __iomem *cc_base = drvdata->cc_base;

	regVal = CC_HAL_READ_REGISTER(CC_REG_OFFSET(HOST_RGF, GPR_HOST));
	if (regVal == (CC_FIPS_SYNC_TEE_STATUS | CC_FIPS_SYNC_MODULE_OK)) {
		return CC_REE_FIPS_ERROR_OK;
	}
	return CC_REE_FIPS_ERROR_FROM_TEE;
}


/*
 This function should push the FIPS REE library status towards the TEE library.
 By writing the error state to HOST_GPR0 register. The function is called from  						.
 driver entry point so no need to protect by mutex.
*/
static void ssi_fips_update_tee_upon_ree_status(struct ssi_drvdata *drvdata, ssi_fips_error_t err)
{
	void __iomem *cc_base = drvdata->cc_base;
	if (err == CC_REE_FIPS_ERROR_OK) {
		CC_HAL_WRITE_REGISTER(CC_REG_OFFSET(HOST_RGF, HOST_GPR0), (CC_FIPS_SYNC_REE_STATUS|CC_FIPS_SYNC_MODULE_OK));
	} else {
		CC_HAL_WRITE_REGISTER(CC_REG_OFFSET(HOST_RGF, HOST_GPR0), (CC_FIPS_SYNC_REE_STATUS|CC_FIPS_SYNC_MODULE_ERROR));
	}
}



void ssi_fips_fini(struct ssi_drvdata *drvdata)
{
	struct ssi_fips_handle *fips_h = drvdata->fips_handle;

	if (fips_h == NULL)
		return; /* Not allocated */

#ifdef COMP_IN_WQ
	if (fips_h->workq != NULL) {
		flush_workqueue(fips_h->workq);
		destroy_workqueue(fips_h->workq);
	}
#else
	/* Kill tasklet */
	tasklet_kill(&fips_h->fipstask);
#endif
	memset(fips_h, 0, sizeof(struct ssi_fips_handle));
	kfree(fips_h);
	drvdata->fips_handle = NULL;
}

void fips_handler(struct ssi_drvdata *drvdata)
{
	struct ssi_fips_handle *fips_handle_ptr =
						drvdata->fips_handle;
#ifdef COMP_IN_WQ
	queue_delayed_work(fips_handle_ptr->workq, &fips_handle_ptr->fipswork, 0);
#else
	tasklet_schedule(&fips_handle_ptr->fipstask);
#endif
}



#ifdef COMP_IN_WQ
static void fips_wq_handler(struct work_struct *work)
{
	struct ssi_drvdata *drvdata =
		container_of(work, struct ssi_drvdata, fipswork.work);

	fips_dsr((unsigned long)drvdata);
}
#endif

/* Deferred service handler, run as interrupt-fired tasklet */
static void fips_dsr(unsigned long devarg)
{
	struct ssi_drvdata *drvdata = (struct ssi_drvdata *)devarg;
	void __iomem *cc_base = drvdata->cc_base;
	uint32_t irq;
	uint32_t teeFipsError = 0;

	irq = (drvdata->irq & (SSI_GPR0_IRQ_MASK));

	if (irq & SSI_GPR0_IRQ_MASK) {
		teeFipsError = CC_HAL_READ_REGISTER(CC_REG_OFFSET(HOST_RGF, GPR_HOST));
		if (teeFipsError != (CC_FIPS_SYNC_TEE_STATUS | CC_FIPS_SYNC_MODULE_OK)) {
			ssi_fips_set_error(drvdata, CC_REE_FIPS_ERROR_FROM_TEE);
		}
	}

	/* after verifing that there is nothing to do, Unmask AXI completion interrupt */
	CC_HAL_WRITE_REGISTER(CC_REG_OFFSET(HOST_RGF, HOST_IMR),
		CC_HAL_READ_REGISTER(
		CC_REG_OFFSET(HOST_RGF, HOST_IMR)) & ~irq);
}


ssi_fips_error_t cc_fips_run_power_up_tests(struct ssi_drvdata *drvdata)
{
	ssi_fips_error_t fips_error = CC_REE_FIPS_ERROR_OK;
	void * cpu_addr_buffer = NULL;
	dma_addr_t dma_handle;
	size_t alloc_buff_size = ssi_fips_max_mem_alloc_size();
	struct device *dev = &drvdata->plat_dev->dev;

	// allocate memory using dma_alloc_coherent - for phisical, consecutive and cache coherent buffer (memory map is not needed)
	// the return value is the virtual address - use it to copy data into the buffer
	// the dma_handle is the returned phy address - use it in the HW descriptor
	FIPS_DBG("dma_alloc_coherent \n");
	cpu_addr_buffer = dma_alloc_coherent(dev, alloc_buff_size, &dma_handle, GFP_KERNEL);
	if (cpu_addr_buffer == NULL) {
		return CC_REE_FIPS_ERROR_GENERAL;
	}
	FIPS_DBG("allocated coherent buffer - addr 0x%08X , size = %d \n", (size_t)cpu_addr_buffer, alloc_buff_size);

#if FIPS_POWER_UP_TEST_CIPHER
	FIPS_DBG("ssi_cipher_fips_power_up_tests ...\n");
	fips_error = ssi_cipher_fips_power_up_tests(drvdata, cpu_addr_buffer, dma_handle);
	FIPS_DBG("ssi_cipher_fips_power_up_tests - done. (fips_error = %d) \n", fips_error);
#endif
#if FIPS_POWER_UP_TEST_CMAC
	if (likely(fips_error == CC_REE_FIPS_ERROR_OK)) {
		FIPS_DBG("ssi_cmac_fips_power_up_tests ...\n");
		fips_error = ssi_cmac_fips_power_up_tests(drvdata, cpu_addr_buffer, dma_handle);
		FIPS_DBG("ssi_cmac_fips_power_up_tests - done. (fips_error = %d) \n", fips_error);
	}
#endif
#if FIPS_POWER_UP_TEST_HASH
	if (likely(fips_error == CC_REE_FIPS_ERROR_OK)) {
		FIPS_DBG("ssi_hash_fips_power_up_tests ...\n");
		fips_error = ssi_hash_fips_power_up_tests(drvdata, cpu_addr_buffer, dma_handle);
		FIPS_DBG("ssi_hash_fips_power_up_tests - done. (fips_error = %d) \n", fips_error);
	}
#endif
#if FIPS_POWER_UP_TEST_HMAC
	if (likely(fips_error == CC_REE_FIPS_ERROR_OK)) {
		FIPS_DBG("ssi_hmac_fips_power_up_tests ...\n");
		fips_error = ssi_hmac_fips_power_up_tests(drvdata, cpu_addr_buffer, dma_handle);
		FIPS_DBG("ssi_hmac_fips_power_up_tests - done. (fips_error = %d) \n", fips_error);
	}
#endif
#if FIPS_POWER_UP_TEST_CCM
	if (likely(fips_error == CC_REE_FIPS_ERROR_OK)) {
		FIPS_DBG("ssi_ccm_fips_power_up_tests ...\n");
		fips_error = ssi_ccm_fips_power_up_tests(drvdata, cpu_addr_buffer, dma_handle);
		FIPS_DBG("ssi_ccm_fips_power_up_tests - done. (fips_error = %d) \n", fips_error);
	}
#endif
#if FIPS_POWER_UP_TEST_GCM
	if (likely(fips_error == CC_REE_FIPS_ERROR_OK)) {
		FIPS_DBG("ssi_gcm_fips_power_up_tests ...\n");
		fips_error = ssi_gcm_fips_power_up_tests(drvdata, cpu_addr_buffer, dma_handle);
		FIPS_DBG("ssi_gcm_fips_power_up_tests - done. (fips_error = %d) \n", fips_error);
	}
#endif
	/* deallocate the buffer when all tests are done... */
	FIPS_DBG("dma_free_coherent \n");
	dma_free_coherent(dev, alloc_buff_size, cpu_addr_buffer, dma_handle);

	return fips_error;
}



/* The function checks if FIPS supported and FIPS error exists.*
*  It should be used in every driver API.*/
int ssi_fips_check_fips_error(void)
{
	ssi_fips_state_t fips_state;

	if (ssi_fips_get_state(&fips_state) != 0) {
		FIPS_LOG("ssi_fips_get_state FAILED, returning.. \n");
		return -ENOEXEC;
	}
	if (fips_state == CC_FIPS_STATE_ERROR) {
		FIPS_LOG("ssi_fips_get_state: fips_state is %d, returning.. \n", fips_state);
		return -ENOEXEC;
	}
	return 0;
}


/* The function sets the REE FIPS state.*
*  It should be used while driver is being loaded .*/
int ssi_fips_set_state(ssi_fips_state_t state)
{
	return ssi_fips_ext_set_state(state);
}

/* The function sets the REE FIPS error, and pushes the error to TEE library. *
*  It should be used when any of the KAT tests fails .*/
int ssi_fips_set_error(struct ssi_drvdata *p_drvdata, ssi_fips_error_t err)
{
	int rc = 0;
        ssi_fips_error_t current_err;

        FIPS_LOG("ssi_fips_set_error - fips_error = %d \n", err);

	// setting no error is not allowed
	if (err == CC_REE_FIPS_ERROR_OK) {
                return -ENOEXEC;
	}
        // If error exists, do not set new error
        if (ssi_fips_get_error(&current_err) != 0) {
                return -ENOEXEC;
        }
        if (current_err != CC_REE_FIPS_ERROR_OK) {
                return -ENOEXEC;
        }
        // set REE internal error and state
	rc = ssi_fips_ext_set_error(err);
	if (rc != 0) {
                return -ENOEXEC;
	}
	rc = ssi_fips_ext_set_state(CC_FIPS_STATE_ERROR);
	if (rc != 0) {
                return -ENOEXEC;
	}

        // push error towards TEE libraray, if it's not TEE error
	if (err != CC_REE_FIPS_ERROR_FROM_TEE) {
		ssi_fips_update_tee_upon_ree_status(p_drvdata, err);
	}
	return rc;
}


/* The function called once at driver entry point .*/
int ssi_fips_init(struct ssi_drvdata *p_drvdata)
{
	ssi_fips_error_t rc = CC_REE_FIPS_ERROR_OK;
	struct ssi_fips_handle *fips_h;

	FIPS_DBG("CC FIPS code ..  (fips=%d) \n", ssi_fips_support);

	fips_h = kzalloc(sizeof(struct ssi_fips_handle),GFP_KERNEL);
	if (fips_h == NULL) {
		ssi_fips_set_error(p_drvdata, CC_REE_FIPS_ERROR_GENERAL);
		return -ENOMEM;
	}

	p_drvdata->fips_handle = fips_h;

#ifdef COMP_IN_WQ
	SSI_LOG_DEBUG("Initializing fips workqueue\n");
	fips_h->workq = create_singlethread_workqueue("arm_cc7x_fips_wq");
	if (unlikely(fips_h->workq == NULL)) {
		SSI_LOG_ERR("Failed creating fips work queue\n");
		ssi_fips_set_error(p_drvdata, CC_REE_FIPS_ERROR_GENERAL);
		rc = -ENOMEM;
		goto ssi_fips_init_err;
	}
	INIT_DELAYED_WORK(&fips_h->fipswork, fips_wq_handler);
#else
	SSI_LOG_DEBUG("Initializing fips tasklet\n");
	tasklet_init(&fips_h->fipstask, fips_dsr, (unsigned long)p_drvdata);
#endif

	/* init fips driver data */
	rc = ssi_fips_set_state((ssi_fips_support == 0)? CC_FIPS_STATE_NOT_SUPPORTED : CC_FIPS_STATE_SUPPORTED);
	if (unlikely(rc != 0)) {
		ssi_fips_set_error(p_drvdata, CC_REE_FIPS_ERROR_GENERAL);
		rc = -EAGAIN;
		goto ssi_fips_init_err;
	}

	/* Run power up tests (before registration and operating the HW engines) */
	FIPS_DBG("ssi_fips_get_tee_error \n");
	rc = ssi_fips_get_tee_error(p_drvdata);
	if (unlikely(rc != CC_REE_FIPS_ERROR_OK)) {
		ssi_fips_set_error(p_drvdata, CC_REE_FIPS_ERROR_FROM_TEE);
		rc = -EAGAIN;
		goto ssi_fips_init_err;
	}

	FIPS_DBG("cc_fips_run_power_up_tests \n");
	rc = cc_fips_run_power_up_tests(p_drvdata);
	if (unlikely(rc != CC_REE_FIPS_ERROR_OK)) {
		ssi_fips_set_error(p_drvdata, rc);
		rc = -EAGAIN;
		goto ssi_fips_init_err;
	}
	FIPS_LOG("cc_fips_run_power_up_tests - done  ...  fips_error = %d \n", rc);

	/* when all tests passed, update TEE with fips OK status after power up tests */
	ssi_fips_update_tee_upon_ree_status(p_drvdata, CC_REE_FIPS_ERROR_OK);

	if (unlikely(rc != 0)) {
		rc = -EAGAIN;
		ssi_fips_set_error(p_drvdata, CC_REE_FIPS_ERROR_GENERAL);
		goto ssi_fips_init_err;
	}

	return 0;

ssi_fips_init_err:
	ssi_fips_fini(p_drvdata);
	return rc;
}

