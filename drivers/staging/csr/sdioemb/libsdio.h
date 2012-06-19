/*
 * SDIO Userspace Interface library.
 *
 * Copyright (C) 2007 Cambridge Silicon Radio Ltd.
 *
 * Refer to LICENSE.txt included with this source code for details on
 * the license terms.
 */
#ifndef SDIOEMB_LIBSDIO_H
#define SDIOEMB_LIBSDIO_H

/**
 * \defgroup libsdio Userspace SDIO library (libsdio)
 *
 * \brief \e libsdio is a Linux C library for accessing SDIO cards.
 *
 * Use of this library requires several \e sdioemb kernel modules to be
 * loaded:
 *   - \c sdio.
 *   - \c An SDIO slot driver (e.g., \c slot_shc for a standard PCI
 *     SDIO Host Controller).
 *   - \c sdio_uif which provides the required character devices
 *     (/dev/sdio_uif0 for the card in SDIO slot 0 etc.).
 */
/*@{*/

#ifdef __cplusplus
extern "C" {
#endif

#ifdef WIN32
#  define LIBSDIOAPI __stdcall
#else
#  define LIBSDIOAPI
#endif

struct sdio_uif;

/**
 * Handle to an opened SDIO Userspace Interface device.
 */
typedef struct sdio_uif *sdio_uif_t;

enum sdio_status {
    SDIO_SUCCESS   = 0,
    SDIO_EAGAIN    = -1,
    SDIO_EINVAL    = -2,
    SDIO_EIO       = -3,
    SDIO_ENODEV    = -4,
    SDIO_ENOMEM    = -5,
    SDIO_ENOTSUPP  = -6,
    SDIO_ENXIO     = -7,
    SDIO_ETIMEDOUT = -8,
};

/**
 * Card interrupt handler function.
 *
 * @param uif handle to the interrupting device.
 * @param arg data supplied by the caller of sdio_open().
 */
typedef void (LIBSDIOAPI *sdio_int_handler_t)(sdio_uif_t uif, void *arg);

/**
 * Asynchronous IO completion callback function.
 *
 * @param uif    handle to the device that completed the IO operation.
 * @param arg    data supplied by the caller of the asynchronous IO operation.
 * @param status status of the IO operation. 0 is success; -EIO,
 *               -EINVAL, -ETIMEDOUT etc. on an error.
 */
typedef void (LIBSDIOAPI *sdio_io_callback_t)(sdio_uif_t uif, void *arg, int status);

/**
 * Open a SDIO Userspace Interface device and (optionally) register a
 * card interrupt handler and enable card interrupts.
 *
 * Card interrupts are masked before calling int_handler and are
 * unmasked when int_handler returns (unless sdio_interrupt_mask() is
 * called).
 *
 * @param dev_filename  filename of the device to open.
 * @param int_handler   card interrupt handler; or NULL if no
 *                      interrupt handler is required.
 * @param arg           argument to be passed to the interrupt handler.
 *
 * @return handle to the opened device; or NULL on error with errno
 * set.
 */
sdio_uif_t LIBSDIOAPI sdio_open(const char *dev_filename,
                                sdio_int_handler_t int_handler, void *arg);

/**
 * Mask the SDIO interrupt.
 *
 * Call this in an interrupt handler to allow the processing of
 * interrupts to be deferred until after the interrupt handler has
 * returned.
 *
 * @note \e Must only be called from within the interrupt handler
 * registered with sdio_open().
 *
 * @param uif device handle.
 */
void LIBSDIOAPI sdio_interrupt_mask(sdio_uif_t uif);

/**
 * Unmask the SDIO interrupt.
 *
 * Unmasks the SDIO interrupt if it had previously been masked with
 * sdio_interrupt_mask().
 *
 * @param uif device handle.
 */
void LIBSDIOAPI sdio_interrupt_unmask(sdio_uif_t uif);

/**
 * Close an opened SDIO Userspace Interface device, freeing all
 * associated resources.
 *
 * @param uif handle to the device.
 */
void LIBSDIOAPI sdio_close(sdio_uif_t uif);

/**
 * Return the number of functions the card has.
 *
 * @param uif device handle.
 *
 * @return number of card functions.
 */
int LIBSDIOAPI sdio_num_functions(sdio_uif_t uif);

/**
 * Set an SDIO bus to 1 bit or 4 bit wide mode.
 *
 * The CCCR bus interface control register will be read and rewritten
 * with the new bus width.
 *
 * @param uif       device handle.
 * @param bus_width bus width (1 or 4).
 *
 * @return 0 on success; -ve on error with errno set.
 *
 * @note The card capabilities are \e not checked.  The user should
 * ensure 4 bit mode is not enabled on a card that does not support
 * it.
 */
int LIBSDIOAPI sdio_set_bus_width(sdio_uif_t uif, int bus_width);

/**
 * Limit the frequency of (or stop) the SD bus clock.
 *
 * The frequency cannot be set greater than that supported by the card
 * or the controller.
 *
 * @note Stopping the bus clock while other device drivers are
 * executing commands may result in those commands not completing
 * until the bus clock is restarted.
 *
 * @param uif      device handle.
 * @param max_freq maximum frequency (Hz) or 0 to stop the bus clock
 *                 until the start of the next command.
 */
void LIBSDIOAPI sdio_set_max_bus_freq(sdio_uif_t uif, int max_freq);

/**
 * Return the card's manufacturer (vendor) ID.
 *
 * @param uif device handle.
 *
 * @return manufacturer ID.
 */
uint16_t LIBSDIOAPI sdio_manf_id(sdio_uif_t uif);

/**
 * Return the card's card (device) ID.
 *
 * @param uif device handle.
 *
 * @return card ID.
 */
uint16_t LIBSDIOAPI sdio_card_id(sdio_uif_t uif);

/**
 * Return the standard interface code for a function.
 *
 * @param uif  device handle.
 * @param func card function to query.
 *
 * @return the standard interface.
 */
uint8_t LIBSDIOAPI sdio_std_if(sdio_uif_t uif, int func);

/**
 * Return a function's maximum supported block size.
 *
 * @param uif  device handle.
 * @param func card function to query.
 *
 * @return maximum block size.
 */
int LIBSDIOAPI sdio_max_block_size(sdio_uif_t uif, int func);

/**
 * Return a function's current block size.
 *
 * @note This returns the driver's view of the block size and not the
 * value in the function's block size register.
 *
 * @param uif  device handle.
 * @param func card function to query.
 *
 * @return the current block size.
 */
int LIBSDIOAPI sdio_block_size(sdio_uif_t uif, int func);

/**
 * Set a function's block size.
 *
 * The function's block size registers will be written if necessary.
 *
 * @param uif   device handle.
 * @param func  function to modify.
 * @param blksz the new block size; or 0 for the default size.
 *
 * @return 0 on success; or -ve on error with errno set.
 */
int LIBSDIOAPI sdio_set_block_size(sdio_uif_t uif, int func, int blksz);

/**
 * Read an 8 bit register.
 *
 * @param uif  device handle.
 * @param func card function.
 * @param addr register address.
 * @param data the data read.
 *
 * @return 0 on success; or -ve on error with errno set.
 */
int LIBSDIOAPI sdio_read8(sdio_uif_t uif, int func, uint32_t addr, uint8_t *data);

/**
 * Write an 8 bit register.
 *
 * @param uif  device handle.
 * @param func card function.
 * @param addr register address.
 * @param data the data to write.
 *
 * @return 0 on success; or -ve on error with errno set.
 */
int LIBSDIOAPI sdio_write8(sdio_uif_t uif, int func, uint32_t addr, uint8_t data);

/**
 * Read a buffer from a 8 bit wide register/FIFO.
 *
 * The buffer read uses a fixed (not incrementing) address.
 *
 * \a block_size \e must be set to the value writted into \a func's
 * I/O block size FBR register.
 *
 * If \a len % \a block_size == 0, a block mode transfer is used; a
 * byte mode transfer is used if \a len < \a block_size.
 *
 * @param uif        device handle.
 * @param func       card function.
 * @param addr       register/FIFO address.
 * @param data       buffer to store the data read.
 * @param len        length of data to read.
 * @param block_size block size to use for this transfer.
 *
 * @return 0 on success; or -ve on error with errno set.
 */
int LIBSDIOAPI sdio_read(sdio_uif_t uif, int func, uint32_t addr, uint8_t *data,
                      size_t len, int block_size);

/**
 * Write a buffer to an 8 bit wide register/FIFO.
 *
 * The buffer write uses a fixed (not incrementing) address.
 *
 * \a block_size \e must be set to the value writted into \a func's
 * I/O block size FBR register.
 *
 * If \a len % \a block_size == 0, a block mode transfer is used; a
 * byte mode transfer is used if \a len < \a block_size.
 *
 * @param uif        device handle.
 * @param func       card function.
 * @param addr       register/FIFO address.
 * @param data       buffer of data to write.
 * @param len        length of the data to write.
 * @param block_size block size to use for this transfer.
 *
 * @return 0 on success; or -ve on error with errno set.
 */
int LIBSDIOAPI sdio_write(sdio_uif_t uif, int func, uint32_t addr, const uint8_t *data,
                          size_t len, int block_size);

/**
 * Read an 8 bit register, without waiting for completion.
 *
 * @param uif      device handle.
 * @param func     card function.
 * @param addr     register address.
 * @param data     the data read.
 * @param callback function to be called when the read completes.
 * @param arg      argument to be passed to callback.
 *
 * @return 0 on success; or -ve on error with errno set.
 */
int LIBSDIOAPI sdio_read8_async(sdio_uif_t uif, int func, uint32_t addr, uint8_t *data,
                                sdio_io_callback_t callback, void *arg);

/**
 * Write an 8 bit register, without waiting for completion.
 *
 * @param uif      device handle.
 * @param func     card function.
 * @param addr     register address.
 * @param data     the data to write.
 * @param callback function to be called when the write completes.
 * @param arg      argument to be passed to callback.
 *
 * @return 0 on success; or -ve on error with errno set.
 */
int LIBSDIOAPI sdio_write8_async(sdio_uif_t uif, int func, uint32_t addr, uint8_t data,
                                 sdio_io_callback_t callback, void *arg);

/**
 * Read a buffer from a 8 bit wide register/FIFO, without waiting for
 * completion.
 *
 * The buffer read uses a fixed (not incrementing) address.
 *
 * \a block_size \e must be set to the value writted into \a func's
 * I/O block size FBR register.
 *
 * If \a len % \a block_size == 0, a block mode transfer is used; a
 * byte mode transfer is used if \a len < \a block_size.
 *
 * @param uif        device handle.
 * @param func       card function.
 * @param addr       register/FIFO address.
 * @param data       buffer to store the data read.
 * @param len        length of data to read.
 * @param block_size block size to use for this transfer.
 * @param callback   function to be called when the read completes.
 * @param arg        argument to be passed to callback.
 *
 * @return 0 on success; or -ve on error with errno set.
 */
int LIBSDIOAPI sdio_read_async(sdio_uif_t uif, int func, uint32_t addr, uint8_t *data,
                               size_t len, int block_size,
                               sdio_io_callback_t callback, void *arg);

/**
 * Write a buffer to an 8 bit wide register/FIFO, without waiting for
 * completion.
 *
 * The buffer write uses a fixed (not incrementing) address.
 *
 * \a block_size \e must be set to the value writted into \a func's
 * I/O block size FBR register.
 *
 * If \a len % \a block_size == 0, a block mode transfer is used; a
 * byte mode transfer is used if \a len < \a block_size.
 *
 * @param uif        device handle.
 * @param func       card function.
 * @param addr       register/FIFO address.
 * @param data       buffer of data to write.
 * @param len        length of the data to write.
 * @param block_size block size to use for this transfer.
 * @param callback   function to be called when the write completes.
 * @param arg        argument to be passed to callback.
 *
 * @return 0 on success; or -ve on error with errno set.
 */
int LIBSDIOAPI sdio_write_async(sdio_uif_t uif, int func, uint32_t addr, const uint8_t *data,
                                size_t len, int block_size,
                                sdio_io_callback_t callback, void *arg);
/**
 * Force a card removal and reinsertion.
 *
 * This will power cycle the card if the slot hardware supports power
 * control.
 *
 * @note The device handle will no longer be valid.
 *
 * @param uif device handle.
 *
 * @return 0 on success; or -ve on error with errno set.
 */
int LIBSDIOAPI sdio_reinsert_card(sdio_uif_t uif);

#ifdef __cplusplus
} /* extern "C" */
#endif

/*@}*/

#endif /* #ifndef SDIOEMB_LIBSDIO_H */
