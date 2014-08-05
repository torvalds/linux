/**
 * IBM Accelerator Family 'GenWQE'
 *
 * (C) Copyright IBM Corp. 2013
 *
 * Author: Frank Haverkamp <haver@linux.vnet.ibm.com>
 * Author: Joerg-Stephan Vogt <jsvogt@de.ibm.com>
 * Author: Michael Jung <mijung@de.ibm.com>
 * Author: Michael Ruettger <michael@ibmra.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License (version 2 only)
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

/*
 * Module initialization and PCIe setup. Card health monitoring and
 * recovery functionality. Character device creation and deletion are
 * controlled from here.
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/err.h>
#include <linux/aer.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/device.h>
#include <linux/log2.h>

#include "card_base.h"
#include "card_ddcb.h"

MODULE_AUTHOR("Frank Haverkamp <haver@linux.vnet.ibm.com>");
MODULE_AUTHOR("Michael Ruettger <michael@ibmra.de>");
MODULE_AUTHOR("Joerg-Stephan Vogt <jsvogt@de.ibm.com>");
MODULE_AUTHOR("Michal Jung <mijung@de.ibm.com>");

MODULE_DESCRIPTION("GenWQE Card");
MODULE_VERSION(DRV_VERS_STRING);
MODULE_LICENSE("GPL");

static char genwqe_driver_name[] = GENWQE_DEVNAME;
static struct class *class_genwqe;
static struct dentry *debugfs_genwqe;
static struct genwqe_dev *genwqe_devices[GENWQE_CARD_NO_MAX];

/* PCI structure for identifying device by PCI vendor and device ID */
static const struct pci_device_id genwqe_device_table[] = {
	{ .vendor      = PCI_VENDOR_ID_IBM,
	  .device      = PCI_DEVICE_GENWQE,
	  .subvendor   = PCI_SUBVENDOR_ID_IBM,
	  .subdevice   = PCI_SUBSYSTEM_ID_GENWQE5,
	  .class       = (PCI_CLASSCODE_GENWQE5 << 8),
	  .class_mask  = ~0,
	  .driver_data = 0 },

	/* Initial SR-IOV bring-up image */
	{ .vendor      = PCI_VENDOR_ID_IBM,
	  .device      = PCI_DEVICE_GENWQE,
	  .subvendor   = PCI_SUBVENDOR_ID_IBM_SRIOV,
	  .subdevice   = PCI_SUBSYSTEM_ID_GENWQE5_SRIOV,
	  .class       = (PCI_CLASSCODE_GENWQE5_SRIOV << 8),
	  .class_mask  = ~0,
	  .driver_data = 0 },

	{ .vendor      = PCI_VENDOR_ID_IBM,  /* VF Vendor ID */
	  .device      = 0x0000,  /* VF Device ID */
	  .subvendor   = PCI_SUBVENDOR_ID_IBM_SRIOV,
	  .subdevice   = PCI_SUBSYSTEM_ID_GENWQE5_SRIOV,
	  .class       = (PCI_CLASSCODE_GENWQE5_SRIOV << 8),
	  .class_mask  = ~0,
	  .driver_data = 0 },

	/* Fixed up image */
	{ .vendor      = PCI_VENDOR_ID_IBM,
	  .device      = PCI_DEVICE_GENWQE,
	  .subvendor   = PCI_SUBVENDOR_ID_IBM_SRIOV,
	  .subdevice   = PCI_SUBSYSTEM_ID_GENWQE5,
	  .class       = (PCI_CLASSCODE_GENWQE5_SRIOV << 8),
	  .class_mask  = ~0,
	  .driver_data = 0 },

	{ .vendor      = PCI_VENDOR_ID_IBM,  /* VF Vendor ID */
	  .device      = 0x0000,  /* VF Device ID */
	  .subvendor   = PCI_SUBVENDOR_ID_IBM_SRIOV,
	  .subdevice   = PCI_SUBSYSTEM_ID_GENWQE5,
	  .class       = (PCI_CLASSCODE_GENWQE5_SRIOV << 8),
	  .class_mask  = ~0,
	  .driver_data = 0 },

	/* Even one more ... */
	{ .vendor      = PCI_VENDOR_ID_IBM,
	  .device      = PCI_DEVICE_GENWQE,
	  .subvendor   = PCI_SUBVENDOR_ID_IBM,
	  .subdevice   = PCI_SUBSYSTEM_ID_GENWQE5_NEW,
	  .class       = (PCI_CLASSCODE_GENWQE5 << 8),
	  .class_mask  = ~0,
	  .driver_data = 0 },

	{ 0, }			/* 0 terminated list. */
};

MODULE_DEVICE_TABLE(pci, genwqe_device_table);

/**
 * genwqe_dev_alloc() - Create and prepare a new card descriptor
 *
 * Return: Pointer to card descriptor, or ERR_PTR(err) on error
 */
static struct genwqe_dev *genwqe_dev_alloc(void)
{
	unsigned int i = 0, j;
	struct genwqe_dev *cd;

	for (i = 0; i < GENWQE_CARD_NO_MAX; i++) {
		if (genwqe_devices[i] == NULL)
			break;
	}
	if (i >= GENWQE_CARD_NO_MAX)
		return ERR_PTR(-ENODEV);

	cd = kzalloc(sizeof(struct genwqe_dev), GFP_KERNEL);
	if (!cd)
		return ERR_PTR(-ENOMEM);

	cd->card_idx = i;
	cd->class_genwqe = class_genwqe;
	cd->debugfs_genwqe = debugfs_genwqe;

	/*
	 * This comes from kernel config option and can be overritten via
	 * debugfs.
	 */
	cd->use_platform_recovery = CONFIG_GENWQE_PLATFORM_ERROR_RECOVERY;

	init_waitqueue_head(&cd->queue_waitq);

	spin_lock_init(&cd->file_lock);
	INIT_LIST_HEAD(&cd->file_list);

	cd->card_state = GENWQE_CARD_UNUSED;
	spin_lock_init(&cd->print_lock);

	cd->ddcb_software_timeout = genwqe_ddcb_software_timeout;
	cd->kill_timeout = genwqe_kill_timeout;

	for (j = 0; j < GENWQE_MAX_VFS; j++)
		cd->vf_jobtimeout_msec[j] = genwqe_vf_jobtimeout_msec;

	genwqe_devices[i] = cd;
	return cd;
}

static void genwqe_dev_free(struct genwqe_dev *cd)
{
	if (!cd)
		return;

	genwqe_devices[cd->card_idx] = NULL;
	kfree(cd);
}

/**
 * genwqe_bus_reset() - Card recovery
 *
 * pci_reset_function() will recover the device and ensure that the
 * registers are accessible again when it completes with success. If
 * not, the card will stay dead and registers will be unaccessible
 * still.
 */
static int genwqe_bus_reset(struct genwqe_dev *cd)
{
	int bars, rc = 0;
	struct pci_dev *pci_dev = cd->pci_dev;
	void __iomem *mmio;

	if (cd->err_inject & GENWQE_INJECT_BUS_RESET_FAILURE)
		return -EIO;

	mmio = cd->mmio;
	cd->mmio = NULL;
	pci_iounmap(pci_dev, mmio);

	bars = pci_select_bars(pci_dev, IORESOURCE_MEM);
	pci_release_selected_regions(pci_dev, bars);

	/*
	 * Firmware/BIOS might change memory mapping during bus reset.
	 * Settings like enable bus-mastering, ... are backuped and
	 * restored by the pci_reset_function().
	 */
	dev_dbg(&pci_dev->dev, "[%s] pci_reset function ...\n", __func__);
	rc = pci_reset_function(pci_dev);
	if (rc) {
		dev_err(&pci_dev->dev,
			"[%s] err: failed reset func (rc %d)\n", __func__, rc);
		return rc;
	}
	dev_dbg(&pci_dev->dev, "[%s] done with rc=%d\n", __func__, rc);

	/*
	 * Here is the right spot to clear the register read
	 * failure. pci_bus_reset() does this job in real systems.
	 */
	cd->err_inject &= ~(GENWQE_INJECT_HARDWARE_FAILURE |
			    GENWQE_INJECT_GFIR_FATAL |
			    GENWQE_INJECT_GFIR_INFO);

	rc = pci_request_selected_regions(pci_dev, bars, genwqe_driver_name);
	if (rc) {
		dev_err(&pci_dev->dev,
			"[%s] err: request bars failed (%d)\n", __func__, rc);
		return -EIO;
	}

	cd->mmio = pci_iomap(pci_dev, 0, 0);
	if (cd->mmio == NULL) {
		dev_err(&pci_dev->dev,
			"[%s] err: mapping BAR0 failed\n", __func__);
		return -ENOMEM;
	}
	return 0;
}

/*
 * Hardware circumvention section. Certain bitstreams in our test-lab
 * had different kinds of problems. Here is where we adjust those
 * bitstreams to function will with this version of our device driver.
 *
 * Thise circumventions are applied to the physical function only.
 * The magical numbers below are identifying development/manufacturing
 * versions of the bitstream used on the card.
 *
 * Turn off error reporting for old/manufacturing images.
 */

bool genwqe_need_err_masking(struct genwqe_dev *cd)
{
	return (cd->slu_unitcfg & 0xFFFF0ull) < 0x32170ull;
}

static void genwqe_tweak_hardware(struct genwqe_dev *cd)
{
	struct pci_dev *pci_dev = cd->pci_dev;

	/* Mask FIRs for development images */
	if (((cd->slu_unitcfg & 0xFFFF0ull) >= 0x32000ull) &&
	    ((cd->slu_unitcfg & 0xFFFF0ull) <= 0x33250ull)) {
		dev_warn(&pci_dev->dev,
			 "FIRs masked due to bitstream %016llx.%016llx\n",
			 cd->slu_unitcfg, cd->app_unitcfg);

		__genwqe_writeq(cd, IO_APP_SEC_LEM_DEBUG_OVR,
				0xFFFFFFFFFFFFFFFFull);

		__genwqe_writeq(cd, IO_APP_ERR_ACT_MASK,
				0x0000000000000000ull);
	}
}

/**
 * genwqe_recovery_on_fatal_gfir_required() - Version depended actions
 *
 * Bitstreams older than 2013-02-17 have a bug where fatal GFIRs must
 * be ignored. This is e.g. true for the bitstream we gave to the card
 * manufacturer, but also for some old bitstreams we released to our
 * test-lab.
 */
int genwqe_recovery_on_fatal_gfir_required(struct genwqe_dev *cd)
{
	return (cd->slu_unitcfg & 0xFFFF0ull) >= 0x32170ull;
}

int genwqe_flash_readback_fails(struct genwqe_dev *cd)
{
	return (cd->slu_unitcfg & 0xFFFF0ull) < 0x32170ull;
}

/**
 * genwqe_T_psec() - Calculate PF/VF timeout register content
 *
 * Note: From a design perspective it turned out to be a bad idea to
 * use codes here to specifiy the frequency/speed values. An old
 * driver cannot understand new codes and is therefore always a
 * problem. Better is to measure out the value or put the
 * speed/frequency directly into a register which is always a valid
 * value for old as well as for new software.
 */
/* T = 1/f */
static int genwqe_T_psec(struct genwqe_dev *cd)
{
	u16 speed;	/* 1/f -> 250,  200,  166,  175 */
	static const int T[] = { 4000, 5000, 6000, 5714 };

	speed = (u16)((cd->slu_unitcfg >> 28) & 0x0full);
	if (speed >= ARRAY_SIZE(T))
		return -1;	/* illegal value */

	return T[speed];
}

/**
 * genwqe_setup_pf_jtimer() - Setup PF hardware timeouts for DDCB execution
 *
 * Do this _after_ card_reset() is called. Otherwise the values will
 * vanish. The settings need to be done when the queues are inactive.
 *
 * The max. timeout value is 2^(10+x) * T (6ns for 166MHz) * 15/16.
 * The min. timeout value is 2^(10+x) * T (6ns for 166MHz) * 14/16.
 */
static bool genwqe_setup_pf_jtimer(struct genwqe_dev *cd)
{
	u32 T = genwqe_T_psec(cd);
	u64 x;

	if (genwqe_pf_jobtimeout_msec == 0)
		return false;

	/* PF: large value needed, flash update 2sec per block */
	x = ilog2(genwqe_pf_jobtimeout_msec *
		  16000000000uL/(T * 15)) - 10;

	genwqe_write_vreg(cd, IO_SLC_VF_APPJOB_TIMEOUT,
			  0xff00 | (x & 0xff), 0);
	return true;
}

/**
 * genwqe_setup_vf_jtimer() - Setup VF hardware timeouts for DDCB execution
 */
static bool genwqe_setup_vf_jtimer(struct genwqe_dev *cd)
{
	struct pci_dev *pci_dev = cd->pci_dev;
	unsigned int vf;
	u32 T = genwqe_T_psec(cd);
	u64 x;

	for (vf = 0; vf < pci_sriov_get_totalvfs(pci_dev); vf++) {

		if (cd->vf_jobtimeout_msec[vf] == 0)
			continue;

		x = ilog2(cd->vf_jobtimeout_msec[vf] *
			  16000000000uL/(T * 15)) - 10;

		genwqe_write_vreg(cd, IO_SLC_VF_APPJOB_TIMEOUT,
				  0xff00 | (x & 0xff), vf + 1);
	}
	return true;
}

static int genwqe_ffdc_buffs_alloc(struct genwqe_dev *cd)
{
	unsigned int type, e = 0;

	for (type = 0; type < GENWQE_DBG_UNITS; type++) {
		switch (type) {
		case GENWQE_DBG_UNIT0:
			e = genwqe_ffdc_buff_size(cd, 0);
			break;
		case GENWQE_DBG_UNIT1:
			e = genwqe_ffdc_buff_size(cd, 1);
			break;
		case GENWQE_DBG_UNIT2:
			e = genwqe_ffdc_buff_size(cd, 2);
			break;
		case GENWQE_DBG_REGS:
			e = GENWQE_FFDC_REGS;
			break;
		}

		/* currently support only the debug units mentioned here */
		cd->ffdc[type].entries = e;
		cd->ffdc[type].regs = kmalloc(e * sizeof(struct genwqe_reg),
					      GFP_KERNEL);
		/*
		 * regs == NULL is ok, the using code treats this as no regs,
		 * Printing warning is ok in this case.
		 */
	}
	return 0;
}

static void genwqe_ffdc_buffs_free(struct genwqe_dev *cd)
{
	unsigned int type;

	for (type = 0; type < GENWQE_DBG_UNITS; type++) {
		kfree(cd->ffdc[type].regs);
		cd->ffdc[type].regs = NULL;
	}
}

static int genwqe_read_ids(struct genwqe_dev *cd)
{
	int err = 0;
	int slu_id;
	struct pci_dev *pci_dev = cd->pci_dev;

	cd->slu_unitcfg = __genwqe_readq(cd, IO_SLU_UNITCFG);
	if (cd->slu_unitcfg == IO_ILLEGAL_VALUE) {
		dev_err(&pci_dev->dev,
			"err: SLUID=%016llx\n", cd->slu_unitcfg);
		err = -EIO;
		goto out_err;
	}

	slu_id = genwqe_get_slu_id(cd);
	if (slu_id < GENWQE_SLU_ARCH_REQ || slu_id == 0xff) {
		dev_err(&pci_dev->dev,
			"err: incompatible SLU Architecture %u\n", slu_id);
		err = -ENOENT;
		goto out_err;
	}

	cd->app_unitcfg = __genwqe_readq(cd, IO_APP_UNITCFG);
	if (cd->app_unitcfg == IO_ILLEGAL_VALUE) {
		dev_err(&pci_dev->dev,
			"err: APPID=%016llx\n", cd->app_unitcfg);
		err = -EIO;
		goto out_err;
	}
	genwqe_read_app_id(cd, cd->app_name, sizeof(cd->app_name));

	/*
	 * Is access to all registers possible? If we are a VF the
	 * answer is obvious. If we run fully virtualized, we need to
	 * check if we can access all registers. If we do not have
	 * full access we will cause an UR and some informational FIRs
	 * in the PF, but that should not harm.
	 */
	if (pci_dev->is_virtfn)
		cd->is_privileged = 0;
	else
		cd->is_privileged = (__genwqe_readq(cd, IO_SLU_BITSTREAM)
				     != IO_ILLEGAL_VALUE);

 out_err:
	return err;
}

static int genwqe_start(struct genwqe_dev *cd)
{
	int err;
	struct pci_dev *pci_dev = cd->pci_dev;

	err = genwqe_read_ids(cd);
	if (err)
		return err;

	if (genwqe_is_privileged(cd)) {
		/* do this after the tweaks. alloc fail is acceptable */
		genwqe_ffdc_buffs_alloc(cd);
		genwqe_stop_traps(cd);

		/* Collect registers e.g. FIRs, UNITIDs, traces ... */
		genwqe_read_ffdc_regs(cd, cd->ffdc[GENWQE_DBG_REGS].regs,
				      cd->ffdc[GENWQE_DBG_REGS].entries, 0);

		genwqe_ffdc_buff_read(cd, GENWQE_DBG_UNIT0,
				      cd->ffdc[GENWQE_DBG_UNIT0].regs,
				      cd->ffdc[GENWQE_DBG_UNIT0].entries);

		genwqe_ffdc_buff_read(cd, GENWQE_DBG_UNIT1,
				      cd->ffdc[GENWQE_DBG_UNIT1].regs,
				      cd->ffdc[GENWQE_DBG_UNIT1].entries);

		genwqe_ffdc_buff_read(cd, GENWQE_DBG_UNIT2,
				      cd->ffdc[GENWQE_DBG_UNIT2].regs,
				      cd->ffdc[GENWQE_DBG_UNIT2].entries);

		genwqe_start_traps(cd);

		if (cd->card_state == GENWQE_CARD_FATAL_ERROR) {
			dev_warn(&pci_dev->dev,
				 "[%s] chip reload/recovery!\n", __func__);

			/*
			 * Stealth Mode: Reload chip on either hot
			 * reset or PERST.
			 */
			cd->softreset = 0x7Cull;
			__genwqe_writeq(cd, IO_SLC_CFGREG_SOFTRESET,
				       cd->softreset);

			err = genwqe_bus_reset(cd);
			if (err != 0) {
				dev_err(&pci_dev->dev,
					"[%s] err: bus reset failed!\n",
					__func__);
				goto out;
			}

			/*
			 * Re-read the IDs because
			 * it could happen that the bitstream load
			 * failed!
			 */
			err = genwqe_read_ids(cd);
			if (err)
				goto out;
		}
	}

	err = genwqe_setup_service_layer(cd);  /* does a reset to the card */
	if (err != 0) {
		dev_err(&pci_dev->dev,
			"[%s] err: could not setup servicelayer!\n", __func__);
		err = -ENODEV;
		goto out;
	}

	if (genwqe_is_privileged(cd)) {	 /* code is running _after_ reset */
		genwqe_tweak_hardware(cd);

		genwqe_setup_pf_jtimer(cd);
		genwqe_setup_vf_jtimer(cd);
	}

	err = genwqe_device_create(cd);
	if (err < 0) {
		dev_err(&pci_dev->dev,
			"err: chdev init failed! (err=%d)\n", err);
		goto out_release_service_layer;
	}
	return 0;

 out_release_service_layer:
	genwqe_release_service_layer(cd);
 out:
	if (genwqe_is_privileged(cd))
		genwqe_ffdc_buffs_free(cd);
	return -EIO;
}

/**
 * genwqe_stop() - Stop card operation
 *
 * Recovery notes:
 *   As long as genwqe_thread runs we might access registers during
 *   error data capture. Same is with the genwqe_health_thread.
 *   When genwqe_bus_reset() fails this function might called two times:
 *   first by the genwqe_health_thread() and later by genwqe_remove() to
 *   unbind the device. We must be able to survive that.
 *
 * This function must be robust enough to be called twice.
 */
static int genwqe_stop(struct genwqe_dev *cd)
{
	genwqe_finish_queue(cd);	    /* no register access */
	genwqe_device_remove(cd);	    /* device removed, procs killed */
	genwqe_release_service_layer(cd);   /* here genwqe_thread is stopped */

	if (genwqe_is_privileged(cd)) {
		pci_disable_sriov(cd->pci_dev);	/* access pci config space */
		genwqe_ffdc_buffs_free(cd);
	}

	return 0;
}

/**
 * genwqe_recover_card() - Try to recover the card if it is possible
 *
 * If fatal_err is set no register access is possible anymore. It is
 * likely that genwqe_start fails in that situation. Proper error
 * handling is required in this case.
 *
 * genwqe_bus_reset() will cause the pci code to call genwqe_remove()
 * and later genwqe_probe() for all virtual functions.
 */
static int genwqe_recover_card(struct genwqe_dev *cd, int fatal_err)
{
	int rc;
	struct pci_dev *pci_dev = cd->pci_dev;

	genwqe_stop(cd);

	/*
	 * Make sure chip is not reloaded to maintain FFDC. Write SLU
	 * Reset Register, CPLDReset field to 0.
	 */
	if (!fatal_err) {
		cd->softreset = 0x70ull;
		__genwqe_writeq(cd, IO_SLC_CFGREG_SOFTRESET, cd->softreset);
	}

	rc = genwqe_bus_reset(cd);
	if (rc != 0) {
		dev_err(&pci_dev->dev,
			"[%s] err: card recovery impossible!\n", __func__);
		return rc;
	}

	rc = genwqe_start(cd);
	if (rc < 0) {
		dev_err(&pci_dev->dev,
			"[%s] err: failed to launch device!\n", __func__);
		return rc;
	}
	return 0;
}

static int genwqe_health_check_cond(struct genwqe_dev *cd, u64 *gfir)
{
	*gfir = __genwqe_readq(cd, IO_SLC_CFGREG_GFIR);
	return (*gfir & GFIR_ERR_TRIGGER) &&
		genwqe_recovery_on_fatal_gfir_required(cd);
}

/**
 * genwqe_fir_checking() - Check the fault isolation registers of the card
 *
 * If this code works ok, can be tried out with help of the genwqe_poke tool:
 *   sudo ./tools/genwqe_poke 0x8 0xfefefefefef
 *
 * Now the relevant FIRs/sFIRs should be printed out and the driver should
 * invoke recovery (devices are removed and readded).
 */
static u64 genwqe_fir_checking(struct genwqe_dev *cd)
{
	int j, iterations = 0;
	u64 mask, fir, fec, uid, gfir, gfir_masked, sfir, sfec;
	u32 fir_addr, fir_clr_addr, fec_addr, sfir_addr, sfec_addr;
	struct pci_dev *pci_dev = cd->pci_dev;

 healthMonitor:
	iterations++;
	if (iterations > 16) {
		dev_err(&pci_dev->dev, "* exit looping after %d times\n",
			iterations);
		goto fatal_error;
	}

	gfir = __genwqe_readq(cd, IO_SLC_CFGREG_GFIR);
	if (gfir != 0x0)
		dev_err(&pci_dev->dev, "* 0x%08x 0x%016llx\n",
				    IO_SLC_CFGREG_GFIR, gfir);
	if (gfir == IO_ILLEGAL_VALUE)
		goto fatal_error;

	/*
	 * Avoid printing when to GFIR bit is on prevents contignous
	 * printout e.g. for the following bug:
	 *   FIR set without a 2ndary FIR/FIR cannot be cleared
	 * Comment out the following if to get the prints:
	 */
	if (gfir == 0)
		return 0;

	gfir_masked = gfir & GFIR_ERR_TRIGGER;  /* fatal errors */

	for (uid = 0; uid < GENWQE_MAX_UNITS; uid++) { /* 0..2 in zEDC */

		/* read the primary FIR (pfir) */
		fir_addr = (uid << 24) + 0x08;
		fir = __genwqe_readq(cd, fir_addr);
		if (fir == 0x0)
			continue;  /* no error in this unit */

		dev_err(&pci_dev->dev, "* 0x%08x 0x%016llx\n", fir_addr, fir);
		if (fir == IO_ILLEGAL_VALUE)
			goto fatal_error;

		/* read primary FEC */
		fec_addr = (uid << 24) + 0x18;
		fec = __genwqe_readq(cd, fec_addr);

		dev_err(&pci_dev->dev, "* 0x%08x 0x%016llx\n", fec_addr, fec);
		if (fec == IO_ILLEGAL_VALUE)
			goto fatal_error;

		for (j = 0, mask = 1ULL; j < 64; j++, mask <<= 1) {

			/* secondary fir empty, skip it */
			if ((fir & mask) == 0x0)
				continue;

			sfir_addr = (uid << 24) + 0x100 + 0x08 * j;
			sfir = __genwqe_readq(cd, sfir_addr);

			if (sfir == IO_ILLEGAL_VALUE)
				goto fatal_error;
			dev_err(&pci_dev->dev,
				"* 0x%08x 0x%016llx\n", sfir_addr, sfir);

			sfec_addr = (uid << 24) + 0x300 + 0x08 * j;
			sfec = __genwqe_readq(cd, sfec_addr);

			if (sfec == IO_ILLEGAL_VALUE)
				goto fatal_error;
			dev_err(&pci_dev->dev,
				"* 0x%08x 0x%016llx\n", sfec_addr, sfec);

			gfir = __genwqe_readq(cd, IO_SLC_CFGREG_GFIR);
			if (gfir == IO_ILLEGAL_VALUE)
				goto fatal_error;

			/* gfir turned on during routine! get out and
			   start over. */
			if ((gfir_masked == 0x0) &&
			    (gfir & GFIR_ERR_TRIGGER)) {
				goto healthMonitor;
			}

			/* do not clear if we entered with a fatal gfir */
			if (gfir_masked == 0x0) {

				/* NEW clear by mask the logged bits */
				sfir_addr = (uid << 24) + 0x100 + 0x08 * j;
				__genwqe_writeq(cd, sfir_addr, sfir);

				dev_dbg(&pci_dev->dev,
					"[HM] Clearing  2ndary FIR 0x%08x "
					"with 0x%016llx\n", sfir_addr, sfir);

				/*
				 * note, these cannot be error-Firs
				 * since gfir_masked is 0 after sfir
				 * was read. Also, it is safe to do
				 * this write if sfir=0. Still need to
				 * clear the primary. This just means
				 * there is no secondary FIR.
				 */

				/* clear by mask the logged bit. */
				fir_clr_addr = (uid << 24) + 0x10;
				__genwqe_writeq(cd, fir_clr_addr, mask);

				dev_dbg(&pci_dev->dev,
					"[HM] Clearing primary FIR 0x%08x "
					"with 0x%016llx\n", fir_clr_addr,
					mask);
			}
		}
	}
	gfir = __genwqe_readq(cd, IO_SLC_CFGREG_GFIR);
	if (gfir == IO_ILLEGAL_VALUE)
		goto fatal_error;

	if ((gfir_masked == 0x0) && (gfir & GFIR_ERR_TRIGGER)) {
		/*
		 * Check once more that it didn't go on after all the
		 * FIRS were cleared.
		 */
		dev_dbg(&pci_dev->dev, "ACK! Another FIR! Recursing %d!\n",
			iterations);
		goto healthMonitor;
	}
	return gfir_masked;

 fatal_error:
	return IO_ILLEGAL_VALUE;
}

/**
 * genwqe_pci_fundamental_reset() - trigger a PCIe fundamental reset on the slot
 *
 * Note: pci_set_pcie_reset_state() is not implemented on all archs, so this
 * reset method will not work in all cases.
 *
 * Return: 0 on success or error code from pci_set_pcie_reset_state()
 */
static int genwqe_pci_fundamental_reset(struct pci_dev *pci_dev)
{
	int rc;

	/*
	 * lock pci config space access from userspace,
	 * save state and issue PCIe fundamental reset
	 */
	pci_cfg_access_lock(pci_dev);
	pci_save_state(pci_dev);
	rc = pci_set_pcie_reset_state(pci_dev, pcie_warm_reset);
	if (!rc) {
		/* keep PCIe reset asserted for 250ms */
		msleep(250);
		pci_set_pcie_reset_state(pci_dev, pcie_deassert_reset);
		/* Wait for 2s to reload flash and train the link */
		msleep(2000);
	}
	pci_restore_state(pci_dev);
	pci_cfg_access_unlock(pci_dev);
	return rc;
}


static int genwqe_platform_recovery(struct genwqe_dev *cd)
{
	struct pci_dev *pci_dev = cd->pci_dev;
	int rc;

	dev_info(&pci_dev->dev,
		 "[%s] resetting card for error recovery\n", __func__);

	/* Clear out error injection flags */
	cd->err_inject &= ~(GENWQE_INJECT_HARDWARE_FAILURE |
			    GENWQE_INJECT_GFIR_FATAL |
			    GENWQE_INJECT_GFIR_INFO);

	genwqe_stop(cd);

	/* Try recoverying the card with fundamental reset */
	rc = genwqe_pci_fundamental_reset(pci_dev);
	if (!rc) {
		rc = genwqe_start(cd);
		if (!rc)
			dev_info(&pci_dev->dev,
				 "[%s] card recovered\n", __func__);
		else
			dev_err(&pci_dev->dev,
				"[%s] err: cannot start card services! (err=%d)\n",
				__func__, rc);
	} else {
		dev_err(&pci_dev->dev,
			"[%s] card reset failed\n", __func__);
	}

	return rc;
}

/*
 * genwqe_reload_bistream() - reload card bitstream
 *
 * Set the appropriate register and call fundamental reset to reaload the card
 * bitstream.
 *
 * Return: 0 on success, error code otherwise
 */
static int genwqe_reload_bistream(struct genwqe_dev *cd)
{
	struct pci_dev *pci_dev = cd->pci_dev;
	int rc;

	dev_info(&pci_dev->dev,
		 "[%s] resetting card for bitstream reload\n",
		 __func__);

	genwqe_stop(cd);

	/*
	 * Cause a CPLD reprogram with the 'next_bitstream'
	 * partition on PCIe hot or fundamental reset
	 */
	__genwqe_writeq(cd, IO_SLC_CFGREG_SOFTRESET,
			(cd->softreset & 0xcull) | 0x70ull);

	rc = genwqe_pci_fundamental_reset(pci_dev);
	if (rc) {
		/*
		 * A fundamental reset failure can be caused
		 * by lack of support on the arch, so we just
		 * log the error and try to start the card
		 * again.
		 */
		dev_err(&pci_dev->dev,
			"[%s] err: failed to reset card for bitstream reload\n",
			__func__);
	}

	rc = genwqe_start(cd);
	if (rc) {
		dev_err(&pci_dev->dev,
			"[%s] err: cannot start card services! (err=%d)\n",
			__func__, rc);
		return rc;
	}
	dev_info(&pci_dev->dev,
		 "[%s] card reloaded\n", __func__);
	return 0;
}


/**
 * genwqe_health_thread() - Health checking thread
 *
 * This thread is only started for the PF of the card.
 *
 * This thread monitors the health of the card. A critical situation
 * is when we read registers which contain -1 (IO_ILLEGAL_VALUE). In
 * this case we need to be recovered from outside. Writing to
 * registers will very likely not work either.
 *
 * This thread must only exit if kthread_should_stop() becomes true.
 *
 * Condition for the health-thread to trigger:
 *   a) when a kthread_stop() request comes in or
 *   b) a critical GFIR occured
 *
 * Informational GFIRs are checked and potentially printed in
 * health_check_interval seconds.
 */
static int genwqe_health_thread(void *data)
{
	int rc, should_stop = 0;
	struct genwqe_dev *cd = data;
	struct pci_dev *pci_dev = cd->pci_dev;
	u64 gfir, gfir_masked, slu_unitcfg, app_unitcfg;

 health_thread_begin:
	while (!kthread_should_stop()) {
		rc = wait_event_interruptible_timeout(cd->health_waitq,
			 (genwqe_health_check_cond(cd, &gfir) ||
			  (should_stop = kthread_should_stop())),
				genwqe_health_check_interval * HZ);

		if (should_stop)
			break;

		if (gfir == IO_ILLEGAL_VALUE) {
			dev_err(&pci_dev->dev,
				"[%s] GFIR=%016llx\n", __func__, gfir);
			goto fatal_error;
		}

		slu_unitcfg = __genwqe_readq(cd, IO_SLU_UNITCFG);
		if (slu_unitcfg == IO_ILLEGAL_VALUE) {
			dev_err(&pci_dev->dev,
				"[%s] SLU_UNITCFG=%016llx\n",
				__func__, slu_unitcfg);
			goto fatal_error;
		}

		app_unitcfg = __genwqe_readq(cd, IO_APP_UNITCFG);
		if (app_unitcfg == IO_ILLEGAL_VALUE) {
			dev_err(&pci_dev->dev,
				"[%s] APP_UNITCFG=%016llx\n",
				__func__, app_unitcfg);
			goto fatal_error;
		}

		gfir = __genwqe_readq(cd, IO_SLC_CFGREG_GFIR);
		if (gfir == IO_ILLEGAL_VALUE) {
			dev_err(&pci_dev->dev,
				"[%s] %s: GFIR=%016llx\n", __func__,
				(gfir & GFIR_ERR_TRIGGER) ? "err" : "info",
				gfir);
			goto fatal_error;
		}

		gfir_masked = genwqe_fir_checking(cd);
		if (gfir_masked == IO_ILLEGAL_VALUE)
			goto fatal_error;

		/*
		 * GFIR ErrorTrigger bits set => reset the card!
		 * Never do this for old/manufacturing images!
		 */
		if ((gfir_masked) && !cd->skip_recovery &&
		    genwqe_recovery_on_fatal_gfir_required(cd)) {

			cd->card_state = GENWQE_CARD_FATAL_ERROR;

			rc = genwqe_recover_card(cd, 0);
			if (rc < 0) {
				/* FIXME Card is unusable and needs unbind! */
				goto fatal_error;
			}
		}

		if (cd->card_state == GENWQE_CARD_RELOAD_BITSTREAM) {
			/* Userspace requested card bitstream reload */
			rc = genwqe_reload_bistream(cd);
			if (rc)
				goto fatal_error;
		}

		cd->last_gfir = gfir;
		cond_resched();
	}

	return 0;

 fatal_error:
	if (cd->use_platform_recovery) {
		/*
		 * Since we use raw accessors, EEH errors won't be detected
		 * by the platform until we do a non-raw MMIO or config space
		 * read
		 */
		readq(cd->mmio + IO_SLC_CFGREG_GFIR);

		/* We do nothing if the card is going over PCI recovery */
		if (pci_channel_offline(pci_dev))
			return -EIO;

		/*
		 * If it's supported by the platform, we try a fundamental reset
		 * to recover from a fatal error. Otherwise, we continue to wait
		 * for an external recovery procedure to take care of it.
		 */
		rc = genwqe_platform_recovery(cd);
		if (!rc)
			goto health_thread_begin;
	}

	dev_err(&pci_dev->dev,
		"[%s] card unusable. Please trigger unbind!\n", __func__);

	/* Bring down logical devices to inform user space via udev remove. */
	cd->card_state = GENWQE_CARD_FATAL_ERROR;
	genwqe_stop(cd);

	/* genwqe_bus_reset failed(). Now wait for genwqe_remove(). */
	while (!kthread_should_stop())
		cond_resched();

	return -EIO;
}

static int genwqe_health_check_start(struct genwqe_dev *cd)
{
	int rc;

	if (genwqe_health_check_interval <= 0)
		return 0;	/* valid for disabling the service */

	/* moved before request_irq() */
	/* init_waitqueue_head(&cd->health_waitq); */

	cd->health_thread = kthread_run(genwqe_health_thread, cd,
					GENWQE_DEVNAME "%d_health",
					cd->card_idx);
	if (IS_ERR(cd->health_thread)) {
		rc = PTR_ERR(cd->health_thread);
		cd->health_thread = NULL;
		return rc;
	}
	return 0;
}

static int genwqe_health_thread_running(struct genwqe_dev *cd)
{
	return cd->health_thread != NULL;
}

static int genwqe_health_check_stop(struct genwqe_dev *cd)
{
	int rc;

	if (!genwqe_health_thread_running(cd))
		return -EIO;

	rc = kthread_stop(cd->health_thread);
	cd->health_thread = NULL;
	return 0;
}

/**
 * genwqe_pci_setup() - Allocate PCIe related resources for our card
 */
static int genwqe_pci_setup(struct genwqe_dev *cd)
{
	int err, bars;
	struct pci_dev *pci_dev = cd->pci_dev;

	bars = pci_select_bars(pci_dev, IORESOURCE_MEM);
	err = pci_enable_device_mem(pci_dev);
	if (err) {
		dev_err(&pci_dev->dev,
			"err: failed to enable pci memory (err=%d)\n", err);
		goto err_out;
	}

	/* Reserve PCI I/O and memory resources */
	err = pci_request_selected_regions(pci_dev, bars, genwqe_driver_name);
	if (err) {
		dev_err(&pci_dev->dev,
			"[%s] err: request bars failed (%d)\n", __func__, err);
		err = -EIO;
		goto err_disable_device;
	}

	/* check for 64-bit DMA address supported (DAC) */
	if (!pci_set_dma_mask(pci_dev, DMA_BIT_MASK(64))) {
		err = pci_set_consistent_dma_mask(pci_dev, DMA_BIT_MASK(64));
		if (err) {
			dev_err(&pci_dev->dev,
				"err: DMA64 consistent mask error\n");
			err = -EIO;
			goto out_release_resources;
		}
	/* check for 32-bit DMA address supported (SAC) */
	} else if (!pci_set_dma_mask(pci_dev, DMA_BIT_MASK(32))) {
		err = pci_set_consistent_dma_mask(pci_dev, DMA_BIT_MASK(32));
		if (err) {
			dev_err(&pci_dev->dev,
				"err: DMA32 consistent mask error\n");
			err = -EIO;
			goto out_release_resources;
		}
	} else {
		dev_err(&pci_dev->dev,
			"err: neither DMA32 nor DMA64 supported\n");
		err = -EIO;
		goto out_release_resources;
	}

	pci_set_master(pci_dev);
	pci_enable_pcie_error_reporting(pci_dev);

	/* EEH recovery requires PCIe fundamental reset */
	pci_dev->needs_freset = 1;

	/* request complete BAR-0 space (length = 0) */
	cd->mmio_len = pci_resource_len(pci_dev, 0);
	cd->mmio = pci_iomap(pci_dev, 0, 0);
	if (cd->mmio == NULL) {
		dev_err(&pci_dev->dev,
			"[%s] err: mapping BAR0 failed\n", __func__);
		err = -ENOMEM;
		goto out_release_resources;
	}

	cd->num_vfs = pci_sriov_get_totalvfs(pci_dev);

	err = genwqe_read_ids(cd);
	if (err)
		goto out_iounmap;

	return 0;

 out_iounmap:
	pci_iounmap(pci_dev, cd->mmio);
 out_release_resources:
	pci_release_selected_regions(pci_dev, bars);
 err_disable_device:
	pci_disable_device(pci_dev);
 err_out:
	return err;
}

/**
 * genwqe_pci_remove() - Free PCIe related resources for our card
 */
static void genwqe_pci_remove(struct genwqe_dev *cd)
{
	int bars;
	struct pci_dev *pci_dev = cd->pci_dev;

	if (cd->mmio)
		pci_iounmap(pci_dev, cd->mmio);

	bars = pci_select_bars(pci_dev, IORESOURCE_MEM);
	pci_release_selected_regions(pci_dev, bars);
	pci_disable_device(pci_dev);
}

/**
 * genwqe_probe() - Device initialization
 * @pdev:	PCI device information struct
 *
 * Callable for multiple cards. This function is called on bind.
 *
 * Return: 0 if succeeded, < 0 when failed
 */
static int genwqe_probe(struct pci_dev *pci_dev,
			const struct pci_device_id *id)
{
	int err;
	struct genwqe_dev *cd;

	genwqe_init_crc32();

	cd = genwqe_dev_alloc();
	if (IS_ERR(cd)) {
		dev_err(&pci_dev->dev, "err: could not alloc mem (err=%d)!\n",
			(int)PTR_ERR(cd));
		return PTR_ERR(cd);
	}

	dev_set_drvdata(&pci_dev->dev, cd);
	cd->pci_dev = pci_dev;

	err = genwqe_pci_setup(cd);
	if (err < 0) {
		dev_err(&pci_dev->dev,
			"err: problems with PCI setup (err=%d)\n", err);
		goto out_free_dev;
	}

	err = genwqe_start(cd);
	if (err < 0) {
		dev_err(&pci_dev->dev,
			"err: cannot start card services! (err=%d)\n", err);
		goto out_pci_remove;
	}

	if (genwqe_is_privileged(cd)) {
		err = genwqe_health_check_start(cd);
		if (err < 0) {
			dev_err(&pci_dev->dev,
				"err: cannot start health checking! "
				"(err=%d)\n", err);
			goto out_stop_services;
		}
	}
	return 0;

 out_stop_services:
	genwqe_stop(cd);
 out_pci_remove:
	genwqe_pci_remove(cd);
 out_free_dev:
	genwqe_dev_free(cd);
	return err;
}

/**
 * genwqe_remove() - Called when device is removed (hot-plugable)
 *
 * Or when driver is unloaded respecitively when unbind is done.
 */
static void genwqe_remove(struct pci_dev *pci_dev)
{
	struct genwqe_dev *cd = dev_get_drvdata(&pci_dev->dev);

	genwqe_health_check_stop(cd);

	/*
	 * genwqe_stop() must survive if it is called twice
	 * sequentially. This happens when the health thread calls it
	 * and fails on genwqe_bus_reset().
	 */
	genwqe_stop(cd);
	genwqe_pci_remove(cd);
	genwqe_dev_free(cd);
}

/*
 * genwqe_err_error_detected() - Error detection callback
 *
 * This callback is called by the PCI subsystem whenever a PCI bus
 * error is detected.
 */
static pci_ers_result_t genwqe_err_error_detected(struct pci_dev *pci_dev,
						 enum pci_channel_state state)
{
	struct genwqe_dev *cd;

	dev_err(&pci_dev->dev, "[%s] state=%d\n", __func__, state);

	cd = dev_get_drvdata(&pci_dev->dev);
	if (cd == NULL)
		return PCI_ERS_RESULT_DISCONNECT;

	/* Stop the card */
	genwqe_health_check_stop(cd);
	genwqe_stop(cd);

	/*
	 * On permanent failure, the PCI code will call device remove
	 * after the return of this function.
	 * genwqe_stop() can be called twice.
	 */
	if (state == pci_channel_io_perm_failure) {
		return PCI_ERS_RESULT_DISCONNECT;
	} else {
		genwqe_pci_remove(cd);
		return PCI_ERS_RESULT_NEED_RESET;
	}
}

static pci_ers_result_t genwqe_err_slot_reset(struct pci_dev *pci_dev)
{
	int rc;
	struct genwqe_dev *cd = dev_get_drvdata(&pci_dev->dev);

	rc = genwqe_pci_setup(cd);
	if (!rc) {
		return PCI_ERS_RESULT_RECOVERED;
	} else {
		dev_err(&pci_dev->dev,
			"err: problems with PCI setup (err=%d)\n", rc);
		return PCI_ERS_RESULT_DISCONNECT;
	}
}

static pci_ers_result_t genwqe_err_result_none(struct pci_dev *dev)
{
	return PCI_ERS_RESULT_NONE;
}

static void genwqe_err_resume(struct pci_dev *pci_dev)
{
	int rc;
	struct genwqe_dev *cd = dev_get_drvdata(&pci_dev->dev);

	rc = genwqe_start(cd);
	if (!rc) {
		rc = genwqe_health_check_start(cd);
		if (rc)
			dev_err(&pci_dev->dev,
				"err: cannot start health checking! (err=%d)\n",
				rc);
	} else {
		dev_err(&pci_dev->dev,
			"err: cannot start card services! (err=%d)\n", rc);
	}
}

static int genwqe_sriov_configure(struct pci_dev *dev, int numvfs)
{
	struct genwqe_dev *cd = dev_get_drvdata(&dev->dev);

	if (numvfs > 0) {
		genwqe_setup_vf_jtimer(cd);
		pci_enable_sriov(dev, numvfs);
		return numvfs;
	}
	if (numvfs == 0) {
		pci_disable_sriov(dev);
		return 0;
	}
	return 0;
}

static struct pci_error_handlers genwqe_err_handler = {
	.error_detected = genwqe_err_error_detected,
	.mmio_enabled	= genwqe_err_result_none,
	.link_reset	= genwqe_err_result_none,
	.slot_reset	= genwqe_err_slot_reset,
	.resume		= genwqe_err_resume,
};

static struct pci_driver genwqe_driver = {
	.name	  = genwqe_driver_name,
	.id_table = genwqe_device_table,
	.probe	  = genwqe_probe,
	.remove	  = genwqe_remove,
	.sriov_configure = genwqe_sriov_configure,
	.err_handler = &genwqe_err_handler,
};

/**
 * genwqe_init_module() - Driver registration and initialization
 */
static int __init genwqe_init_module(void)
{
	int rc;

	class_genwqe = class_create(THIS_MODULE, GENWQE_DEVNAME);
	if (IS_ERR(class_genwqe)) {
		pr_err("[%s] create class failed\n", __func__);
		return -ENOMEM;
	}

	debugfs_genwqe = debugfs_create_dir(GENWQE_DEVNAME, NULL);
	if (!debugfs_genwqe) {
		rc = -ENOMEM;
		goto err_out;
	}

	rc = pci_register_driver(&genwqe_driver);
	if (rc != 0) {
		pr_err("[%s] pci_reg_driver (rc=%d)\n", __func__, rc);
		goto err_out0;
	}

	return rc;

 err_out0:
	debugfs_remove(debugfs_genwqe);
 err_out:
	class_destroy(class_genwqe);
	return rc;
}

/**
 * genwqe_exit_module() - Driver exit
 */
static void __exit genwqe_exit_module(void)
{
	pci_unregister_driver(&genwqe_driver);
	debugfs_remove(debugfs_genwqe);
	class_destroy(class_genwqe);
}

module_init(genwqe_init_module);
module_exit(genwqe_exit_module);
