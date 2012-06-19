/*
 * SDIO device driver API.
 *
 * Copyright (C) 2007-2008 Cambridge Silicon Radio Ltd.
 *
 * Refer to LICENSE.txt included with this source code for details on
 * the license terms.
 */
#ifndef _SDIO_API_H
#define _SDIO_API_H

/**
 * @defgroup fdriver SDIO function driver API
 *
 * @brief The SDIO function driver API is used to implement drivers
 * for SDIO card functions.
 *
 * Function drivers register with the SDIO driver core
 * (sdio_register_driver()), listing which functions it supports and
 * providing callback functions for card inserts, removes and
 * interrupts.
 *
 * @par \anchor card_io_ops Card I/O operations:
 *
 * - \link sdioemb_read8(struct sdioemb_dev *, uint32_t, uint8_t *) sdioemb_read8()\endlink
 * - \link sdioemb_read16(struct sdioemb_dev *, uint32_t, uint16_t *) sdioemb_read16()\endlink
 * - \link sdioemb_write8(struct sdioemb_dev *, uint32_t, uint8_t) sdioemb_write8()\endlink
 * - \link sdioemb_write16(struct sdioemb_dev *, uint32_t, uint16_t) sdioemb_write16()\endlink
 * - \link sdioemb_f0_read8(struct sdioemb_dev *, uint32_t, uint8_t *) sdioemb_f0_read8()\endlink
 * - \link sdioemb_f0_write8(struct sdioemb_dev *, uint32_t, uint8_t) sdioemb_f0_write8()\endlink
 * - \link sdioemb_read(struct sdioemb_dev *, uint32_t, void *, size_t) sdioemb_read()\endlink
 * - \link sdioemb_write(struct sdioemb_dev *, uint32_t, const void *, size_t) sdioemb_write()\endlink
 */

struct sdioemb_func_driver;
struct sdioemb_dev;
struct sdioemb_dev_priv;

/**
 * An SDIO device.
 *
 * Each SDIO card will have an sdio_dev for each function.
 *
 * None of the fields (except for drv_data) should be written.
 *
 * @ingroup fdriver
 */
struct sdioemb_dev {
    struct sdioemb_func_driver *driver;        /**< Function driver for this device. */
    uint16_t                 vendor_id;     /**< Vendor ID of the card. */
    uint16_t                 device_id;     /**< Device ID of the card. */
    int                      function;      /**< Function number of this device. */
    uint8_t                  interface;     /**< SDIO standard interface number. */
    uint16_t                 max_blocksize; /**< Maximum block size supported. */
    uint16_t                 blocksize;     /**< Blocksize in use. */
    int                      slot_id;       /**< ID of the slot this card is inserted into. */
    void *                   os_device;     /**< Pointer to an OS-specific device structure. */
    struct sdioemb_dev_priv *priv;          /**< Data private to the SDIO core. */
    void *                   drv_data;      /**< Data private to the function driver. */
};

#define SDIOEMB_ANY_ID    0xffff
#define SDIOEMB_UIF_FUNC  0
#define SDIOEMB_ANY_FUNC  0xff
#define SDIOEMB_ANY_IFACE 0xff

/**
 * An entry for an SDIO device ID table.
 *
 * Functions are matched to drivers using any combination of vendor
 * ID, device ID, function number or standard interface.
 *
 * Matching on #function == SDIOEMB_UIF_FUNC is reserved for the SDIO
 * Userspace Interface driver. Card management drivers can match on
 * #function == 0, these will be probed before any function drivers.
 *
 * @ingroup fdriver
 */
struct sdioemb_id_table {
    uint16_t vendor_id; /**< Vendor ID to match or SDIOEMB_ANY_ID */
    uint16_t device_id; /**< Device ID to match or SDIOEMB_ANY_ID */
    int      function;  /**< Function number to match or SDIOEMB_ANY_FUNC */
    uint8_t  interface; /**< SDIO standard interface to match or SDIOEMB_ANY_IFACE */
};

/**
 * A driver for an SDIO function.
 *
 * @ingroup fdriver
 */
struct sdioemb_func_driver {
    /**
     * Driver name used in diagnostics.
     */
    const char *name;

    /**
     * 0 terminated array of functions supported by this device.
     *
     * The driver may (for example) match on a number of vendor
     * ID/device ID/function number triplets or on an SDIO standard
     * interface.
     */
    struct sdioemb_id_table *id_table;

    /**
     * Called by the core when an inserted card has functions which
     * match those listed in id_table.
     *
     * The driver's implementation should (if required):
     *
     *   - perform any additional probing
     *   - do function specific initialization
     *   - allocate and register any function/OS specific devices or interfaces.
     *
     * Called in: thread context.
     *
     * @param fdev the newly inserted device.
     *
     * @return 0 on success; -ve on error.
     */
    int (*probe)(struct sdioemb_dev *fdev);

    /**
     * Called by the core when a card is removed.  This is only called
     * if the probe() call succeeded.
     *
     * The driver's implementation should (if required);
     *
     *   - do any function specific shutdown.
     *   - cleanup any data structures created/registers during probe().
     *
     * Called in: thread context.
     *
     * @param fdev the device being removed.
     */
    void (*remove)(struct sdioemb_dev *fdev);

    /**
     * Called by the core to signal an SDIO interrupt for this card
     * occurs, if interrupts have been enabled with
     * sdioemb_interrupt_enable().
     *
     * The driver's implementation should signal a thread (or similar)
     * to actually handle the interrupt as no card I/O may be
     * performed whilst in interrupt context. When the interrupt is
     * handled, the driver should call sdioemb_interrupt_acknowledge() to
     * enable further interrupts to be signalled.
     *
     * Called in: interrupt context.
     *
     * @param fdev the device which may have raised the interrupt.
     */
    void (*card_int_handler)(struct sdioemb_dev *fdev);

    /**
     * Called by the core to signal a suspend power management
     * event occured.
     *
     * The driver's implementation should (if required)
     * set the card to a low power mode and return as soon
     * as possible. After this function returns, the
     * driver should not start any SDIO commands.
     *
     * Called in: thread context.
     *
     * @param fdev the device handler.
     */
    void (*suspend)(struct sdioemb_dev *fdev);

    /**
     * Called by the core to signal a resume power management
     * event occured.
     *
     * The driver's implementation should (if required)
     * initialise the card to an operational mode and return
     * as soon as possible. If the card has been powered off
     * during suspend, the driver would have to initialise
     * the card from scratch (f/w download, h/w initialisation, etc.).
     *
     * Called in: thread context.
     *
     * @param fdev the device handler.
     */
    void (*resume)(struct sdioemb_dev *fdev);
};

int  sdioemb_driver_register(struct sdioemb_func_driver *fdriver);
void sdioemb_driver_unregister(struct sdioemb_func_driver *fdriver);

int sdioemb_driver_probe(struct sdioemb_func_driver *fdriver, struct sdioemb_dev *fdev);
void sdioemb_driver_remove(struct sdioemb_func_driver *fdriver, struct sdioemb_dev *fdev);

/* For backward compatibility. */
#define sdio_register_driver sdioemb_driver_register
#define sdio_unregister_driver sdioemb_driver_unregister

int sdioemb_set_block_size(struct sdioemb_dev *fdev, uint16_t blksz);
void sdioemb_set_max_bus_freq(struct sdioemb_dev *fdev, int max_freq);
int sdioemb_set_bus_width(struct sdioemb_dev *fdev, int bus_width);

int sdioemb_enable_function(struct sdioemb_dev *fdev);
int sdioemb_disable_function(struct sdioemb_dev *fdev);
int sdioemb_reenable_csr_function(struct sdioemb_dev *dev);
void sdioemb_idle_function(struct sdioemb_dev *fdev);

int sdioemb_read8(struct sdioemb_dev *fdev, uint32_t addr, uint8_t *val);
int sdioemb_read16(struct sdioemb_dev *fdev, uint32_t addr, uint16_t *val);
int sdioemb_write8(struct sdioemb_dev *fdev, uint32_t addr, uint8_t val);
int sdioemb_write16(struct sdioemb_dev *fdev, uint32_t addr, uint16_t val);
int sdioemb_f0_read8(struct sdioemb_dev *fdev, uint32_t addr, uint8_t *val);
int sdioemb_f0_write8(struct sdioemb_dev *fdev, uint32_t addr, uint8_t val);
int sdioemb_read(struct sdioemb_dev *fdev, uint32_t addr, void *data, size_t len);
int sdioemb_write(struct sdioemb_dev *fdev, uint32_t addr, const void *data, size_t len);

int sdioemb_hard_reset(struct sdioemb_dev *fdev);

void sdioemb_power_on(struct sdioemb_dev *fdev);
void sdioemb_power_off(struct sdioemb_dev *fdev);

int sdioemb_interrupt_enable(struct sdioemb_dev *fdev);
int sdioemb_interrupt_disable(struct sdioemb_dev *fdev);
void sdioemb_interrupt_acknowledge(struct sdioemb_dev *fdev);

int sdioemb_cis_get_tuple(struct sdioemb_dev *fdev, uint8_t tuple,
                       void *buf, size_t len);

void sdioemb_suspend_function(struct sdioemb_dev *fdev);
void sdioemb_resume_function(struct sdioemb_dev *fdev);

/**
 * SDIO command status.
 *
 * @ingroup fdriver
 */
enum sdioemb_cmd_status {
    SDIOEMB_CMD_OK          = 0x00, /**< Command successful. */

    SDIOEMB_CMD_ERR_CMD     = 0x01,
    SDIOEMB_CMD_ERR_DAT     = 0x02,

    SDIOEMB_CMD_ERR_CRC     = 0x10,
    SDIOEMB_CMD_ERR_TIMEOUT = 0x20,
    SDIOEMB_CMD_ERR_OTHER   = 0x40,

    SDIOEMB_CMD_ERR_CMD_CRC     = SDIOEMB_CMD_ERR_CMD | SDIOEMB_CMD_ERR_CRC,     /**< Response CRC error. */
    SDIOEMB_CMD_ERR_CMD_TIMEOUT = SDIOEMB_CMD_ERR_CMD | SDIOEMB_CMD_ERR_TIMEOUT, /**< Response time out. */
    SDIOEMB_CMD_ERR_CMD_OTHER   = SDIOEMB_CMD_ERR_CMD | SDIOEMB_CMD_ERR_OTHER,   /**< Other response error. */
    SDIOEMB_CMD_ERR_DAT_CRC     = SDIOEMB_CMD_ERR_DAT | SDIOEMB_CMD_ERR_CRC,     /**< Data CRC error. */
    SDIOEMB_CMD_ERR_DAT_TIMEOUT = SDIOEMB_CMD_ERR_DAT | SDIOEMB_CMD_ERR_TIMEOUT, /**< Data receive time out. */
    SDIOEMB_CMD_ERR_DAT_OTHER   = SDIOEMB_CMD_ERR_DAT | SDIOEMB_CMD_ERR_OTHER,   /**< Other data error. */

    SDIOEMB_CMD_ERR_NO_CARD = 0x04, /**< No card present. */

    SDIOEMB_CMD_IN_PROGRESS = 0xff, /**< Command still in progress. */
};

/**
 * A response to an SDIO command.
 *
 * For R1, R4, R5, and R6 responses only the middle 32 bits of the
 * response are stored, the leading octet (start and direction bits
 * and command index) and trailing octet (CRC and stop bit) are
 * discarded.
 *
 * @bug R2 and R3 responses are not used by SDIO and are not
 * supported.
 *
 * @ingroup fdriver
 */
union sdioemb_response {
    uint32_t r1;
    uint32_t r4;
    uint32_t r5;
    uint32_t r6;
};

/**
 * SDIO command parameters and response.
 */
struct sdioemb_cmd_resp {
    uint8_t  cmd;                 /**< Command index (0 to 63). */
    uint32_t arg;                 /**< Command argument. */
    union sdioemb_response response; /**< Response to the command. Valid
                                     iff the command has completed and
                                     (sdio_cmd::status & SDIOEMB_CMD_ERR_CMD) == 0.*/
};

/**
 * CSPI command parameters and response.
 */
struct cspi_cmd_resp {
    unsigned cmd : 8;  /**< Command octet (type, and function). */
    unsigned addr: 24; /**< 24 bit address. */
    uint16_t val;      /**< Word to write or read from the card (for non-burst commands). */
    uint8_t  response; /**< Response octet.  Valid iff the command has completed and
                          (sdio_cmd::status & SDIOEMB_CMD_ERR_CMD) == 0. */
};


/**
 * An SDIO command, its status and response.
 *
 * sdio_cmd is used to submit SDIO commands to a device and return its
 * status and any response or data.
 *
 * @ingroup fdriver
 */
struct sdioemb_cmd {
    /**
     * The SDIO device which submitted the command.  Set by the
     * core.
     */
    struct sdioemb_dev *owner;

    /**
     * Called by the core when the command has been completed.
     *
     * Called in: interrupt context.
     *
     * @param cmd the completed command.
     */
    void (*callback)(struct sdioemb_cmd *cmd);

    /**
     * Set of flags specifying the response type, data transfer
     * direction and other parameters.
     *
     * For SDIO commands set at least one of the response types:
     *   - #SDIOEMB_CMD_FLAG_RESP_NONE
     *   - #SDIOEMB_CMD_FLAG_RESP_R1
     *   - #SDIOEMB_CMD_FLAG_RESP_R1B
     *   - #SDIOEMB_CMD_FLAG_RESP_R2
     *   - #SDIOEMB_CMD_FLAG_RESP_R3
     *   - #SDIOEMB_CMD_FLAG_RESP_R4
     *   - #SDIOEMB_CMD_FLAG_RESP_R5
     *   - #SDIOEMB_CMD_FLAG_RESP_R5B
     *   - #SDIOEMB_CMD_FLAG_RESP_R6
     *
     * and any of the additional flags:
     *   - #SDIOEMB_CMD_FLAG_READ
     *
     * For CSPI commands set:
     *   - #SDIOEMB_CMD_FLAG_CSPI
     */
    unsigned flags;

    /**
     * SDIO command parameters and response.
     *
     * Valid only if #SDIOEMB_CMD_FLAG_CSPI is \e not set in #flags.
     */
    struct sdioemb_cmd_resp sdio;

    /**
     * CSPI command parameters and response.
     *
     * Valid only if #SDIOEMB_CMD_FLAG_CSPI is set in #flags.
     */
    struct cspi_cmd_resp cspi;

    /**
     * Buffer of data to read or write.
     *
     * Must be set to NULL if the command is not a data transfer.
     */
    uint8_t *data;

    /**
     * Length of #data in octets.
     *
     * len must be either: less than the device's sdio_dev::blocksize;
     * or a multiple of the device's sdio_dev::blocksize.
     */
    size_t len;

    /**
     * Status of the command after it has completed.
     */
    enum sdioemb_cmd_status status;

    /**
     * Data private to caller of sdioemb_start_cmd().
     */
    void *priv;
};

/** @addtogroup fdriver
 *@{*/
#define SDIOEMB_CMD_FLAG_RESP_NONE 0x00 /**< No response. */
#define SDIOEMB_CMD_FLAG_RESP_R1   0x01 /**< R1 response. */
#define SDIOEMB_CMD_FLAG_RESP_R1B  0x02 /**< R1b response. */
#define SDIOEMB_CMD_FLAG_RESP_R2   0x03 /**< R2 response. */
#define SDIOEMB_CMD_FLAG_RESP_R3   0x04 /**< R3 response. */
#define SDIOEMB_CMD_FLAG_RESP_R4   0x05 /**< R4 response. */
#define SDIOEMB_CMD_FLAG_RESP_R5   0x06 /**< R5 response. */
#define SDIOEMB_CMD_FLAG_RESP_R5B  0x07 /**< R5b response. */
#define SDIOEMB_CMD_FLAG_RESP_R6   0x08 /**< R6 response. */
#define SDIOEMB_CMD_FLAG_RESP_MASK 0xff /**< Mask for response type. */
#define SDIOEMB_CMD_FLAG_RAW     0x0100 /**< @internal Bypass the command queues. */
#define SDIOEMB_CMD_FLAG_READ    0x0200 /**< Data transfer is a read, not a write. */
#define SDIOEMB_CMD_FLAG_CSPI    0x0400 /**< CSPI transfer, not SDIO or SDIO-SPI. */
#define SDIOEMB_CMD_FLAG_ABORT   0x0800 /**< Data transfer abort command. */
/*@}*/

int sdioemb_start_cmd(struct sdioemb_dev *fdev, struct sdioemb_cmd *cmd);

#endif /* #ifndef _SDIO_API_H */
