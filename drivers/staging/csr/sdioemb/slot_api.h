/*
 * Slot driver API.
 *
 * Copyright (C) 2007-2009 Cambridge Silicon Radio Ltd.
 *
 * Refer to LICENSE.txt included with this source code for details on
 * the license terms.
 */
#ifndef _SLOT_API_H
#define _SLOT_API_H

#include <sdioemb/sdio_api.h>

struct sdioemb_slot;

/**
 * @defgroup sdriver SDIO slot driver API
 *
 * @brief The SDIO slot driver API provides an interface for the SDIO
 * layer to driver an SDIO slot (socket).
 *
 * Slot drivers register with the SDIO layer (sdioemb_slot_register()),
 * providing functions to starting commands, enabling/disable card
 * interrupts, card detection and bus power control.
 *
 * Functions are provided to notify the SDIO layer when a command has
 * completed (sdioemb_cmd_complete()) and when an SDIO card interrupt has
 * occurred (sdioemb_interrupt()).
 */

#define SDIOEMB_BUS_FREQ_OFF      0
#define SDIOEMB_BUS_FREQ_DEFAULT -1
#define SDIOEMB_BUS_FREQ_IDLE    -2

/**
 * Valid SDIO bus voltage levels.
 *
 * @ingroup sdriver
 */
enum sdioemb_power {
    SDIOEMB_POWER_OFF  =   0, /**< Power switched off. */
    SDIOEMB_POWER_3V3  =  33, /**< Voltage set to 3.3V. */
};

/**
 * SDIO slot capabilities.
 *
 * @ingroup sdriver
 */
struct slot_caps {
    int max_bus_freq;  /**< Maximum bus frequency (Hz). */
    int max_bus_width; /**< Maximum bus width supported (1 or 4 data lines). */
    uint8_t cspi_mode; /**< CSPI_MODE register value (for CSPI capable slots). */
};

/**
 * Controller hardware type.
 *
 * @ingroup sdriver
 */
enum slot_controller_type {
    SDIOEMB_SLOT_TYPE_SD = 0,   /**< SD/SDIO controller. */
    SDIOEMB_SLOT_TYPE_SPI,      /**< SPI controller. */
    SDIOEMB_SLOT_TYPE_SPI_CSPI, /**< SPI controller capable of CSPI. */
};

/**
 * Return values from the add_function() notifier.
 *
 * @ingroup sdriver
 */
enum sdioemb_add_func_status {
    /**
     * The core will call sdioemb_add_function().
     */
    SDIOEMB_ADD_FUNC_NOW = 0,
    /**
     * The slot driver will call sdioemb_add_function() or the
     * function driver will call sdioemb_driver_probe() directly.
     */
    SDIOEMB_ADD_FUNC_DEFERRED = 1,
};

/**
 * Slot/card event notifiers.
 *
 * A slot driver may be notified when certain slot or card events
 * occur.
 *
 * @ingroup sdriver
 */
struct sdioemb_slot_notifiers {
    /**
     * This is called when a card function has been enumerated
     * and initialized but before can be bound to a function driver.
     *
     * A slot driver may use this to create an OS-specific object for
     * the function.  The slot driver must either (a) return
     * SDIOEMB_ADD_FUNC_NOW; (b) return SDIOEMB_ADD_FUNC_DEFERRED and
     * call sdioemb_add_function() later on; (c) return
     * SDIOEMB_ADD_FUNC_DEFERRED and pass the fdev to the function
     * driver for it to call sdioemb_driver_probe() directly; or (d)
     * return an error.
     *
     * The slot driver may need to get a reference to the fdev with
     * sdioemb_get_function() if the lifetime of the OS-specific
     * object extends beyond the subsequent return of the
     * del_function() callback.
     *
     * If this is non-NULL the slot driver must also provide
     * del_function().
     *
     * @param slot the SDIO slot producing the notification.
     * @param fdev the SDIO function being added.
     *
     * @return SDIOEMB_ADD_FUNC_NOW if the function is ready for use.
     * @return SDIOEMB_ADD_FUNC_DEFERRED if sdioemb_add_function() or
     *         sdioemb_driver_probe() will be called later.
     * @return -ve on a error.
     */
    int (*add_function)(struct sdioemb_slot *slot, struct sdioemb_dev *fdev);

    /**
     * This is called when a card function is being removed and after
     * any function driver has been unbound.
     *
     * A slot driver may use this to delete any OS-specific object
     * created by the add_function() notifier.
     *
     * @param slot the SDIO slot producing the notification.
     * @param fdev the SDIO function being deleted.
     */
    void (*del_function)(struct sdioemb_slot *slot, struct sdioemb_dev *fdev);
};

struct sdioemb_slot_priv;

/**
 * An SDIO slot driver.
 *
 * Allocate and free with sdioemb_slot_alloc() and sdioemb_slot_free().
 *
 * @ingroup sdriver
 */
struct sdioemb_slot {
    /**
     * Name of the slot used in diagnostic messages.
     *
     * This would typically include the name of the SDIO controller
     * and the slot number if the controller has multiple slots.
     *
     * This will be set by sdioemb_slot_register() if it is left as an
     * empty string.
     */
    char name[64];

    /**
     * Controller hardware type.
     */
    enum slot_controller_type type;

    /**
     * Set the SD bus clock frequency.
     *
     * The driver's implementation should set the SD bus clock to not
     * more than \a clk Hz (unless \a clk is equal to
     * #SDIOEMB_BUS_FREQ_OFF or #SDIOEMB_BUS_FREQ_IDLE).
     *
     * If \a clk == SDIOEMB_BUS_FREQ_OFF the clock should be stopped.
     *
     * \a clk == SDIOEMB_BUS_FREQ_IDLE indicates that the bus is idle
     * (currently unused) and the host controller may slow (or stop)
     * the SD bus clock to save power on the card.  During this idle
     * state the host controller must be capable of receiving SDIO
     * interrupts (for certain host controllers this may require
     * leaving the clock running).
     *
     * If \a clk is greater than #SDIO_CLOCK_FREQ_NORMAL_SPD (25 MHz)
     * subsequent commands should be done with the controller in high
     * speed mode.
     *
     * Called from: interrupt context.
     *
     * @param slot  the slot to configure.
     * @param clk   new SD bus clock frequency in Hz, SDIOEMB_BUS_FREQ_OFF
     *              or SDIOEMB_BUS_FREQ_IDLE.
     *
     * @return The bus frequency actually configured in Hz.
     */
    int (*set_bus_freq)(struct sdioemb_slot *slot, int clk);

    /**
     * Set the SD bus width.
     *
     * The driver's implementation should set the width of the SD bus
     * for all subsequent data transfers to the specified value.
     *
     * This may be NULL if the driver sets the bus width when starting
     * a command, or the driver is for an SDIO-SPI or CSPI controller.
     *
     * Called from: thread context.
     *
     * @param slot      the slot to configure.
     * @param bus_width new SD bus width (either 1 or 4).
     *
     * @return 0 on success.
     * @return -ve if a low-level error occured when setting the bus width.
     */
    int (*set_bus_width)(struct sdioemb_slot *slot, int bus_width);

    /**
     * Start an SDIO command.
     *
     * The driver's implementation should:
     *
     *   - set the controller's bus width to #bus_width,
     *   - program the controller to start the command.
     *
     * Called from: interrupt context.
     *
     * @param slot  slot to perform the command.
     * @param cmd   SDIO command to start.
     */
    int (*start_cmd)(struct sdioemb_slot *slot, struct sdioemb_cmd *cmd);

    /**
     * Detect if a card is inserted into the slot.
     *
     * Called from: thread context.
     *
     * @param slot slot to check.
     *
     * @return non-zero if a card is inserted; 0 otherwise.
     */
    int (*card_present)(struct sdioemb_slot *slot);

    /**
     * Switch on/off the SDIO bus power and set the SDIO bus voltage.
     *
     * Called from: thread context.
     *
     * @param slot  the slot.
     * @param power the requested voltage.
     *
     * @return 0 on success; -ve on error: -EINVAL - requested voltage
     * is not supported.
     */
    int (*card_power)(struct sdioemb_slot *slot, enum sdioemb_power power);

    /**
     * Enable (unmask) the SDIO card interrupt on the controller.
     *
     * Called from: interrupt context.
     *
     * @param slot the slot to enable the interrupt on..
     */
    void (*enable_card_int)(struct sdioemb_slot *slot);

    /**
     * Disable (mask) the SDIO card interrupt on the controller.
     *
     * Called from: thread context.
     *
     * @param slot the slot to disable the interrupt on.
     */
    void (*disable_card_int)(struct sdioemb_slot *slot);

    /**
     * Perform a hard reset of the card.
     *
     * Hard resets can be achieved in two ways:
     *
     * -# Power cycle (if the slot has power control).
     * -# Platform-specific assertion of a card/chip reset line.
     *
     * If hard resets are not supported, either return 0 or set
     * hard_reset to NULL.
     *
     * @param slot the slot for the card to reset.
     *
     * @return 0 if a hard reset was performed.
     * @return 1 if hard resets are not supported.
     */
    int (*hard_reset)(struct sdioemb_slot *slot);

    struct slot_caps         caps;           /**< Slot capabilities. */
    int                      clock_freq;     /**< SD bus frequency requested by the SDIO layer. */
    int                      bus_width;      /**< Bus width requested by the SDIO layer. */
    struct sdioemb_slot_notifiers notifs;    /**< Slot event notifiers. */
    int                      cspi_reg_pad;   /**< Padding for CSPI register reads. */
    int                      cspi_burst_pad; /**< Padding for CSPI burst reads. */
    struct sdioemb_slot_priv *priv;          /**< Data private to the SDIO layer. */
    void *                   drv_data;       /**< Data private to the slot driver. */
};

struct sdioemb_slot *sdioemb_slot_alloc(size_t drv_data_size);
void sdioemb_slot_free(struct sdioemb_slot *slot);
int sdioemb_slot_register(struct sdioemb_slot *slot);
void sdioemb_slot_unregister(struct sdioemb_slot *slot);
int sdioemb_card_inserted(struct sdioemb_slot *slot);
void sdioemb_card_removed(struct sdioemb_slot *slot);
void sdioemb_interrupt(struct sdioemb_slot *slot);
void sdioemb_cmd_complete(struct sdioemb_slot *slot, struct sdioemb_cmd *cmd);

void sdioemb_suspend(struct sdioemb_slot *slot);
void sdioemb_resume(struct sdioemb_slot *slot);

void sdioemb_add_function(struct sdioemb_dev *fdev);
void sdioemb_del_function(struct sdioemb_dev *fdev);
void sdioemb_get_function(struct sdioemb_dev *fdev);
void sdioemb_put_function(struct sdioemb_dev *fdev);

#endif /* #ifndef _SLOT_API_H */
