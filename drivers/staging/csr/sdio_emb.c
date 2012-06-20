/*
 * ---------------------------------------------------------------------------
 *
 * FILE: sdio_emb.c
 *
 * PURPOSE: Driver instantiation and deletion for SDIO on Linux.
 *
 *      This file brings together the SDIO bus interface, the UniFi
 *      driver core and the Linux net_device stack.
 *
 * Copyright (C) 2007-2009 by Cambridge Silicon Radio Ltd.
 *
 * Refer to LICENSE.txt included with this source code for details on
 * the license terms.
 *
 * ---------------------------------------------------------------------------
 */
#include <linux/kmod.h>
#include <linux/init.h>
#include <linux/suspend.h>
#include "csr_wifi_hip_unifi.h"
#include "unifi_priv.h"

#include "sdioemb/sdio_api.h"

/* The function driver context, i.e the UniFi Driver */
static CsrSdioFunctionDriver *sdio_func_drv;

#ifdef CONFIG_PM
static int uf_sdio_emb_power_event(struct notifier_block *this, unsigned long event, void *ptr);
#endif

/* The Android wakelock is here for completeness. Typically the MMC driver is used
 * instead of sdioemb, but sdioemb may be used for CSPI.
 */
#ifdef ANDROID_BUILD
struct wake_lock unifi_sdio_wake_lock; /* wakelock to prevent suspend while resuming */
#endif

/* sdioemb driver uses POSIX error codes */
static CsrResult
ConvertSdioToCsrSdioResult(int r)
{
    CsrResult csrResult = CSR_RESULT_FAILURE;

    switch (r) {
        case 0:
            csrResult = CSR_RESULT_SUCCESS;
            break;
        case -EIO:
            csrResult = CSR_SDIO_RESULT_CRC_ERROR;
            break;
            /* Timeout errors */
        case -ETIMEDOUT:
        case -EBUSY:
            csrResult = CSR_SDIO_RESULT_TIMEOUT;
            break;
        case -ENODEV:
        case -ENOMEDIUM:
            csrResult = CSR_SDIO_RESULT_NO_DEVICE;
            break;
        case -EINVAL:
            csrResult = CSR_SDIO_RESULT_INVALID_VALUE;
            break;
        case -ENOMEM:
        case -ENOSYS:
        case -EILSEQ:
        case -ERANGE:
        case -ENXIO:
            csrResult = CSR_RESULT_FAILURE;
            break;
        default:
            unifi_warning(NULL, "Unrecognised SDIO error code: %d\n", r);
            break;
    }

    return csrResult;
}


CsrResult
CsrSdioRead8(CsrSdioFunction *function, CsrUint32 address, CsrUint8 *data)
{
    struct sdioemb_dev *fdev = (struct sdioemb_dev *)function->priv;
    int err;
    err = sdioemb_read8(fdev, address, data);
    if (err) {
        return ConvertSdioToCsrSdioResult(err);
    }
    return CSR_RESULT_SUCCESS;
} /* CsrSdioRead8() */

CsrResult
CsrSdioWrite8(CsrSdioFunction *function, CsrUint32 address, CsrUint8 data)
{
    struct sdioemb_dev *fdev = (struct sdioemb_dev *)function->priv;
    int err;
    err = sdioemb_write8(fdev, address, data);
    if (err) {
        return ConvertSdioToCsrSdioResult(err);
    }
    return CSR_RESULT_SUCCESS;
} /* CsrSdioWrite8() */

CsrResult
CsrSdioRead16(CsrSdioFunction *function, CsrUint32 address, CsrUint16 *data)
{
    struct sdioemb_dev *fdev = (struct sdioemb_dev *)function->priv;
    int r;

    r = sdioemb_read16(fdev, address, data);
    if (r) {
        return ConvertSdioToCsrSdioResult(r);
    }

    return CSR_RESULT_SUCCESS;
} /* CsrSdioRead16() */

CsrResult
CsrSdioWrite16(CsrSdioFunction *function, CsrUint32 address, CsrUint16 data)
{
    struct sdioemb_dev *fdev = (struct sdioemb_dev *)function->priv;
    int r;

    r = sdioemb_write16(fdev, address, data);
    if (r) {
        return ConvertSdioToCsrSdioResult(r);
    }

    return CSR_RESULT_SUCCESS;
} /* CsrSdioWrite16() */


CsrResult
CsrSdioF0Read8(CsrSdioFunction *function, CsrUint32 address, CsrUint8 *data)
{
    struct sdioemb_dev *fdev = (struct sdioemb_dev *)function->priv;
    int err;
    err = sdioemb_f0_read8(fdev, address, data);
    if (err) {
        return ConvertSdioToCsrSdioResult(err);
    }
    return CSR_RESULT_SUCCESS;
} /* CsrSdioF0Read8() */


CsrResult
CsrSdioF0Write8(CsrSdioFunction *function, CsrUint32 address, CsrUint8 data)
{
    struct sdioemb_dev *fdev = (struct sdioemb_dev *)function->priv;
    int err;
    err = sdioemb_f0_write8(fdev, address, data);
    if (err) {
        return ConvertSdioToCsrSdioResult(err);
    }
    return CSR_RESULT_SUCCESS;
} /* CsrSdioF0Write8() */

CsrResult
CsrSdioRead(CsrSdioFunction *function, CsrUint32 address, void *data, CsrUint32 length)
{
    struct sdioemb_dev *fdev = (struct sdioemb_dev *)function->priv;
    int err;
    err = sdioemb_read(fdev, address, data, length);
    if (err) {
        return ConvertSdioToCsrSdioResult(err);
    }
    return CSR_RESULT_SUCCESS;
} /* CsrSdioRead() */

CsrResult
CsrSdioWrite(CsrSdioFunction *function, CsrUint32 address, const void *data, CsrUint32 length)
{
    struct sdioemb_dev *fdev = (struct sdioemb_dev *)function->priv;
    int err;
    err = sdioemb_write(fdev, address, data, length);
    if (err) {
        return ConvertSdioToCsrSdioResult(err);
    }
    return CSR_RESULT_SUCCESS;
} /* CsrSdioWrite() */


CsrResult
CsrSdioBlockSizeSet(CsrSdioFunction *function, CsrUint16 blockSize)
{
    struct sdioemb_dev *fdev = (struct sdioemb_dev *)function->priv;
    int r = 0;

    /* Module parameter overrides */
    if (sdio_block_size > -1) {
        blockSize = sdio_block_size;
    }

    unifi_trace(NULL, UDBG1, "Set SDIO function block size to %d\n",
            blockSize);

    r = sdioemb_set_block_size(fdev, blockSize);
    if (r) {
        unifi_error(NULL, "Error %d setting block size\n", r);
    }

    /* Determine the achieved block size to report to the core */
    function->blockSize = fdev->blocksize;

    return ConvertSdioToCsrSdioResult(r);
} /* CsrSdioBlockSizeSet() */


/*
 * ---------------------------------------------------------------------------
 *  CsrSdioMaxBusClockFrequencySet
 *
 *      Set the maximum SDIO bus clock speed to use.
 *
 *  Arguments:
 *      sdio            SDIO context pointer
 *      maxFrequency         maximum clock speed in Hz
 *
 *  Returns:
 *      an error code.
 * ---------------------------------------------------------------------------
 */
CsrResult
CsrSdioMaxBusClockFrequencySet(CsrSdioFunction *function, CsrUint32 maxFrequency)
{
    struct sdioemb_dev *fdev = (struct sdioemb_dev *)function->priv;
    CsrUint32 max_khz = maxFrequency/1000;

    if (!max_khz || max_khz > sdio_clock) {
        max_khz = sdio_clock;
    }
    unifi_trace(NULL, UDBG1, "Setting SDIO bus clock to %d kHz\n", max_khz);
    sdioemb_set_max_bus_freq(fdev, 1000 * max_khz);

    return CSR_RESULT_SUCCESS;
} /* CsrSdioMaxBusClockFrequencySet() */


/*
 * ---------------------------------------------------------------------------
 *  CsrSdioInterruptEnable
 *  CsrSdioInterruptDisable
 *
 *      Enable or disable the SDIO interrupt.
 *
 *  Arguments:
 *      sdio            SDIO context pointer
 *
 *  Returns:
 *      Zero on success or a UniFi driver error code.
 * ---------------------------------------------------------------------------
 */
CsrResult
CsrSdioInterruptEnable(CsrSdioFunction *function)
{
    struct sdioemb_dev *fdev = (struct sdioemb_dev *)function->priv;
    int r;

    r = sdioemb_interrupt_enable(fdev);
    if (r) {
        return ConvertSdioToCsrSdioResult(r);
    }

    return CSR_RESULT_SUCCESS;
} /* CsrSdioInterruptEnable() */

CsrResult
CsrSdioInterruptDisable(CsrSdioFunction *function)
{
    struct sdioemb_dev *fdev = (struct sdioemb_dev *)function->priv;
    int r;

    r = sdioemb_interrupt_disable(fdev);
    if (r) {
        return ConvertSdioToCsrSdioResult(r);
    }

    return CSR_RESULT_SUCCESS;
} /* CsrSdioInterruptDisable() */


/*
 * ---------------------------------------------------------------------------
 *  CsrSdioInterruptAcknowledge
 *
 *      Acknowledge an SDIO interrupt.
 *
 *  Arguments:
 *      sdio            SDIO context pointer
 *
 *  Returns:
 *      Zero on success or a UniFi driver error code.
 * ---------------------------------------------------------------------------
 */
void CsrSdioInterruptAcknowledge(CsrSdioFunction *function)
{
    struct sdioemb_dev *fdev = (struct sdioemb_dev *)function->priv;

    sdioemb_interrupt_acknowledge(fdev);
} /* CsrSdioInterruptAcknowledge() */


/*
 * ---------------------------------------------------------------------------
 *  CsrSdioFunctionEnable
 *
 *      Enable i/o on this function.
 *
 *  Arguments:
 *      sdio            SDIO context pointer
 *
 * Returns:
 *      UniFi driver error code.
 * ---------------------------------------------------------------------------
 */
CsrResult
CsrSdioFunctionEnable(CsrSdioFunction *function)
{
    struct sdioemb_dev *fdev = (struct sdioemb_dev *)function->priv;
    int r;

    /* Enable UniFi function (the 802.11 part). */
    r = sdioemb_enable_function(fdev);
    if (r) {
        unifi_error(NULL, "Failed to enable SDIO function %d\n", fdev->function);
        return ConvertSdioToCsrSdioResult(r);
    }
    return CSR_RESULT_SUCCESS;
} /* CsrSdioFunctionEnable() */


/*
 * ---------------------------------------------------------------------------
 *  CsrSdioFunctionDisable
 *
 *      Disable i/o on this function.
 *
 *  Arguments:
 *      sdio            SDIO context pointer
 *
 * Returns:
 *      UniFi driver error code.
 * ---------------------------------------------------------------------------
 */
CsrResult
CsrSdioFunctionDisable(CsrSdioFunction *function)
{
    struct sdioemb_dev *fdev = (struct sdioemb_dev *)function->priv;
    int r;

    /* Disable UniFi function (the 802.11 part). */
    r = sdioemb_disable_function(fdev);
    if (r) {
        unifi_error(NULL, "Failed to disable SDIO function %d\n", fdev->function);
        return ConvertSdioToCsrSdioResult(r);
    }
    return CSR_RESULT_SUCCESS;
} /* CsrSdioFunctionDisable() */


/*
 * ---------------------------------------------------------------------------
 *  CsrSdioFunctionActive
 *
 *      No-op as the bus goes to an active state at the start of every
 *      command.
 *
 *  Arguments:
 *      sdio            SDIO context pointer
 * ---------------------------------------------------------------------------
 */
void
CsrSdioFunctionActive(CsrSdioFunction *function)
{
} /* CsrSdioFunctionActive() */

/*
 * ---------------------------------------------------------------------------
 *  CsrSdioFunctionIdle
 *
 *      Set the function as idle.
 *
 *  Arguments:
 *      sdio            SDIO context pointer
 * ---------------------------------------------------------------------------
 */
void
CsrSdioFunctionIdle(CsrSdioFunction *function)
{
    struct sdioemb_dev *fdev = (struct sdioemb_dev *)function->priv;

    sdioemb_idle_function(fdev);
} /* CsrSdioFunctionIdle() */


CsrResult
CsrSdioPowerOn(CsrSdioFunction *function)
{
    struct sdioemb_dev *fdev = (struct sdioemb_dev *)function->priv;

    if (disable_power_control != 1) {
        sdioemb_power_on(fdev);
    }

    return CSR_RESULT_SUCCESS;
} /* CsrSdioPowerOn() */

void
CsrSdioPowerOff(CsrSdioFunction *function)
{
    struct sdioemb_dev *fdev = (struct sdioemb_dev *)function->priv;
    if (disable_power_control != 1) {
        sdioemb_power_off(fdev);
    }
} /* CsrSdioPowerOff() */


/*
 * ---------------------------------------------------------------------------
 *  CsrSdioHardReset
 *
 *      Hard Resets UniFi is possible.
 *
 *  Arguments:
 *      sdio            SDIO context pointer
 *
 * Returns:
 *      1       if the SDIO driver is not capable of doing a hard reset.
 *      0       if a hard reset was successfully performed.
 *      -CSR_EIO if an I/O error occured while re-initializing the card.
 *              This is a fatal, non-recoverable error.
 *      -CSR_ENODEV if the card is no longer present.
 * ---------------------------------------------------------------------------
 */
CsrResult
CsrSdioHardReset(CsrSdioFunction *function)
{
    struct sdioemb_dev *fdev = (struct sdioemb_dev *)function->priv;
    int r;

    /* Hard reset can be disabled by a module parameter */
    r = 1;
    if (disable_hw_reset != 1) {
        r = sdioemb_hard_reset(fdev); /* may return 1 if can't reset */
        if (r < 0) {
            return ConvertSdioToCsrSdioResult(r);   /* fatal error */
        }
    }

    /* Set the SDIO bus width after a hard reset */
    if (buswidth == 1) {
        unifi_info(NULL, "Setting SDIO bus width to 1\n");
        sdioemb_set_bus_width(fdev, buswidth);
    } else if (buswidth == 4) {
        unifi_info(NULL, "Setting SDIO bus width to 4\n");
        sdioemb_set_bus_width(fdev, buswidth);
    }

    if(r == 1)
    {
        return CSR_SDIO_RESULT_NOT_RESET;
    }

    return ConvertSdioToCsrSdioResult(r);

} /* CsrSdioHardReset() */


int csr_sdio_linux_remove_irq(CsrSdioFunction *function)
{
    struct sdioemb_dev *fdev = (struct sdioemb_dev *)function->priv;

    return sdioemb_interrupt_disable(fdev);
}

int csr_sdio_linux_install_irq(CsrSdioFunction *function)
{
    struct sdioemb_dev *fdev = (struct sdioemb_dev *)function->priv;

    return sdioemb_interrupt_enable(fdev);
}


/*
 * ---------------------------------------------------------------------------
 *  uf_glue_sdio_int_handler
 *      Card interrupt callback.
 *
 * Arguments:
 *      fdev            SDIO context pointer
 *
 * Returns:
 *      None.
 * ---------------------------------------------------------------------------
 */
static void
uf_glue_sdio_int_handler(struct sdioemb_dev *fdev)
{
    CsrSdioFunction *sdio_ctx = fdev->drv_data;
    CsrSdioInterruptDsrCallback func_dsr_callback;

    /* If the function driver has registered a handler, call it */
    if (sdio_func_drv && sdio_func_drv->intr) {
        /* The function driver may return a DSR. */
        func_dsr_callback = sdio_func_drv->intr(sdio_ctx);
        /* If it did return a DSR handle, call it */
        if (func_dsr_callback) {
            func_dsr_callback(sdio_ctx);
        }
    }
}

#ifdef CONFIG_PM

/*
 * Power Management notifier
 */
struct uf_sdio_emb_pm_notifier
{
    struct list_head list;

    CsrSdioFunction *sdio_ctx;
    struct notifier_block pm_notifier;
};

/* PM notifier list head */
static struct uf_sdio_emb_pm_notifier uf_sdio_emb_pm_notifiers = {
    .sdio_ctx = NULL,
};

/*
 * ---------------------------------------------------------------------------
 * uf_sdio_emb_register_pm_notifier
 * uf_sdio_emb_unregister_pm_notifier
 *
 *      Register/unregister for power management events. A list is used to
 *	allow multiple card instances to be supported.
 *
 *  Arguments:
 *      sdio_ctx - CSR SDIO context to associate PM notifier to
 *
 *  Returns:
 *      Register function returns NULL on error
 * ---------------------------------------------------------------------------
 */
static struct uf_sdio_emb_pm_notifier *
uf_sdio_emb_register_pm_notifier(CsrSdioFunction *sdio_ctx)
{
    /* Allocate notifier context for this card instance */
    struct uf_sdio_emb_pm_notifier *notifier_ctx = kmalloc(sizeof(struct uf_sdio_emb_pm_notifier), GFP_KERNEL);

    if (notifier_ctx)
    {
        notifier_ctx->sdio_ctx = sdio_ctx;
        notifier_ctx->pm_notifier.notifier_call = uf_sdio_emb_power_event;

        list_add(&notifier_ctx->list, &uf_sdio_emb_pm_notifiers.list);

        if (register_pm_notifier(&notifier_ctx->pm_notifier)) {
            printk(KERN_ERR "unifi: register_pm_notifier failed\n");
        }
    }

    return notifier_ctx;
}

static void
uf_sdio_emb_unregister_pm_notifier(CsrSdioFunction *sdio_ctx)
{
    struct uf_sdio_emb_pm_notifier *notifier_ctx;
    struct list_head *node, *q;

    list_for_each_safe(node, q, &uf_sdio_emb_pm_notifiers.list) {
        notifier_ctx = list_entry(node, struct uf_sdio_emb_pm_notifier, list);

        /* If it matches, unregister and free the notifier context */
        if (notifier_ctx && notifier_ctx->sdio_ctx == sdio_ctx)
        {
            if (unregister_pm_notifier(&notifier_ctx->pm_notifier)) {
                printk(KERN_ERR "unifi: unregister_pm_notifier failed\n");
            }

            /* Remove from list */
            notifier_ctx->sdio_ctx = NULL;
            list_del(node);
            kfree(notifier_ctx);
        }
    }
}

/*
 * ---------------------------------------------------------------------------
 * uf_sdio_emb_power_event
 *
 *      Handler for power management events.
 *
 *      We need to handle suspend/resume events while the userspace is unsuspended
 *      to allow the SME to run its suspend/resume state machines.
 *
 *  Arguments:
 *      event   event ID
 *
 *  Returns:
 *      Status of the event handling
 * ---------------------------------------------------------------------------
 */
static int
uf_sdio_emb_power_event(struct notifier_block *this, unsigned long event, void *ptr)
{
    struct uf_sdio_emb_pm_notifier *notifier_ctx = container_of(this,
                                                                struct uf_sdio_emb_pm_notifier,
                                                                pm_notifier);

    /* Call the CSR SDIO function driver's suspend/resume method
     * while the userspace is unsuspended.
     */
    switch (event) {
        case PM_POST_HIBERNATION:
        case PM_POST_SUSPEND:
            printk(KERN_INFO "%s:%d resume\n", __FUNCTION__, __LINE__ );
            if (sdio_func_drv && sdio_func_drv->resume) {
                sdio_func_drv->resume(notifier_ctx->sdio_ctx);
            }
            break;

        case PM_HIBERNATION_PREPARE:
        case PM_SUSPEND_PREPARE:
            printk(KERN_INFO "%s:%d suspend\n", __FUNCTION__, __LINE__ );
            if (sdio_func_drv && sdio_func_drv->suspend) {
                sdio_func_drv->suspend(notifier_ctx->sdio_ctx);
            }
            break;
    }
    return NOTIFY_DONE;
}

#endif /* CONFIG_PM */

/*
 * ---------------------------------------------------------------------------
 *  uf_glue_sdio_probe
 *
 *      Card insert callback.
 *
 * Arguments:
 *      fdev            SDIO context pointer
 *
 * Returns:
 *      UniFi driver error code.
 * ---------------------------------------------------------------------------
 */
static int
uf_glue_sdio_probe(struct sdioemb_dev *fdev)
{
    CsrSdioFunction *sdio_ctx;

    unifi_info(NULL, "UniFi card inserted\n");

    /* Allocate context and private in one lump */
    sdio_ctx = (CsrSdioFunction *)kmalloc(sizeof(CsrSdioFunction),
                                          GFP_KERNEL);
    if (sdio_ctx == NULL) {
        return -ENOMEM;
    }


    sdio_ctx->sdioId.manfId = fdev->vendor_id;
    sdio_ctx->sdioId.cardId = fdev->device_id;
    sdio_ctx->sdioId.sdioFunction = fdev->function;
    sdio_ctx->sdioId.sdioInterface = 0;
    sdio_ctx->blockSize = fdev->blocksize;
    sdio_ctx->priv = (void *)fdev;
    sdio_ctx->features = 0;

    /* Module parameter enables byte mode */
    if (sdio_byte_mode) {
        sdio_ctx->features |= CSR_SDIO_FEATURE_BYTE_MODE;
    }

    /* Set up pointer to func_priv in middle of lump */
    fdev->drv_data = sdio_ctx;

    /* Always override default SDIO bus clock */
    unifi_trace(NULL, UDBG1, "Setting SDIO bus clock to %d kHz\n", sdio_clock);
    sdioemb_set_max_bus_freq(fdev, 1000 * sdio_clock);

#ifdef CONFIG_PM
    /* Register to get PM events */
    if (uf_sdio_emb_register_pm_notifier(sdio_ctx) == NULL) {
        unifi_error(NULL, "%s: Failed to register for PM events\n", __FUNCTION__);
    }
#endif

    /* Call the main UniFi driver inserted handler */
    if (sdio_func_drv && sdio_func_drv->inserted) {
        uf_add_os_device(fdev->slot_id, fdev->os_device);
        sdio_func_drv->inserted(sdio_ctx);
    }

#ifdef ANDROID_BUILD
    /* Take the wakelock */
    unifi_trace(NULL, UDBG1, "emb probe: take wake lock\n");
    wake_lock(&unifi_sdio_wake_lock);
#endif

    return 0;
} /* uf_glue_sdio_probe() */



/*
 * ---------------------------------------------------------------------------
 *  uf_sdio_remove
 *
 *      Card removal callback.
 *
 * Arguments:
 *      fdev            SDIO device
 *
 * Returns:
 *      UniFi driver error code.
 * ---------------------------------------------------------------------------
 */
static void
uf_sdio_remove(struct sdioemb_dev *fdev)
{
    CsrSdioFunction *sdio_ctx = fdev->drv_data;

    unifi_info(NULL, "UniFi card removed\n");

    /* Clean up the SDIO function driver */
    if (sdio_func_drv && sdio_func_drv->removed) {
        sdio_func_drv->removed(sdio_ctx);
    }

#ifdef CONFIG_PM
    /* Unregister for PM events */
    uf_sdio_emb_unregister_pm_notifier(sdio_ctx);
#endif

    kfree(sdio_ctx);

} /* uf_sdio_remove */


/*
 * ---------------------------------------------------------------------------
 *  uf_glue_sdio_suspend
 *
 *      System suspend callback.
 *
 * Arguments:
 *      fdev            SDIO device
 *
 * Returns:
 *
 * ---------------------------------------------------------------------------
 */
static void
uf_glue_sdio_suspend(struct sdioemb_dev *fdev)
{
    unifi_info(NULL, "Suspending...\n");

} /* uf_glue_sdio_suspend() */


/*
 * ---------------------------------------------------------------------------
 *  uf_glue_sdio_resume
 *
 *      System resume callback.
 *
 * Arguments:
 *      fdev            SDIO device
 *
 * Returns:
 *
 * ---------------------------------------------------------------------------
 */
static void
uf_glue_sdio_resume(struct sdioemb_dev *fdev)
{
    unifi_info(NULL, "Resuming...\n");

#ifdef ANDROID_BUILD
    unifi_trace(NULL, UDBG1, "emb resume: take wakelock\n");
    wake_lock(&unifi_sdio_wake_lock);
#endif

} /* uf_glue_sdio_resume() */




static struct sdioemb_func_driver unifi_sdioemb = {
    .name = "unifi",
    .id_table = NULL,           /* Filled in when main driver registers */

    .probe  = uf_glue_sdio_probe,
    .remove = uf_sdio_remove,
    .card_int_handler = uf_glue_sdio_int_handler,
    .suspend  = uf_glue_sdio_suspend,
    .resume = uf_glue_sdio_resume,
};


/*
 * ---------------------------------------------------------------------------
 *  CsrSdioFunctionDriverRegister
 *  CsrSdioFunctionDriverUnregister
 *
 *      These functions are called from the main module load and unload
 *      functions. They perform the appropriate operations for the
 *      SDIOemb driver.
 *
 *  Arguments:
 *      None.
 *
 *  Returns:
 *      None.
 * ---------------------------------------------------------------------------
 */
CsrResult
CsrSdioFunctionDriverRegister(CsrSdioFunctionDriver *sdio_drv)
{
    int r;
    int i;

    printk("Unifi: Using CSR embedded SDIO driver\n");

    if (sdio_func_drv) {
        unifi_error(NULL, "sdio_emb: UniFi driver already registered\n");
        return CSR_SDIO_RESULT_INVALID_VALUE;
    }

    /* Build ID table to pass to sdioemb */
    unifi_sdioemb.id_table = CsrPmemAlloc(sizeof(struct sdioemb_id_table) * (sdio_drv->idsCount + 1));
    if (unifi_sdioemb.id_table == NULL) {
        unifi_error(NULL, "sdio_emb: Failed to allocate memory for ID table (%d IDs)\n", sdio_drv->idsCount);
        return CSR_RESULT_FAILURE;
    }
    for (i = 0; i < sdio_drv->idsCount; i++) {
        unifi_sdioemb.id_table[i].vendor_id = sdio_drv->ids[i].manfId;
        unifi_sdioemb.id_table[i].device_id = sdio_drv->ids[i].cardId;
        unifi_sdioemb.id_table[i].function  = sdio_drv->ids[i].sdioFunction;
        unifi_sdioemb.id_table[i].interface = sdio_drv->ids[i].sdioInterface;
    }
    unifi_sdioemb.id_table[i].vendor_id = 0;
    unifi_sdioemb.id_table[i].device_id = 0;
    unifi_sdioemb.id_table[i].function  = 0;
    unifi_sdioemb.id_table[i].interface = 0;

    /* Save the registered driver description */
    sdio_func_drv = sdio_drv;

#ifdef CONFIG_PM
    /* Initialise PM notifier list */
    INIT_LIST_HEAD(&uf_sdio_emb_pm_notifiers.list);
#endif

#ifdef ANDROID_BUILD
    wake_lock_init(&unifi_sdio_wake_lock, WAKE_LOCK_SUSPEND, "unifi_sdio_work");
#endif

    /* Register ourself with sdioemb */
    r = sdioemb_driver_register(&unifi_sdioemb);
    if (r) {
        unifi_error(NULL, "Failed to register UniFi SDIO driver: %d\n", r);
        return ConvertSdioToCsrSdioResult(r);
    }

    return CSR_RESULT_SUCCESS;
} /* CsrSdioFunctionDriverRegister() */


void
CsrSdioFunctionDriverUnregister(CsrSdioFunctionDriver *sdio_drv)
{
    sdioemb_driver_unregister(&unifi_sdioemb);

#ifdef ANDROID_BUILD
    wake_lock_destroy(&unifi_sdio_wake_lock);
#endif

    sdio_func_drv = NULL;

    CsrPmemFree(unifi_sdioemb.id_table);
    unifi_sdioemb.id_table = NULL;
} /* CsrSdioFunctionDriverUnregister() */

