/*
 * CSR specific SDIO registers.
 *
 * Copyright (C) 2007 Cambridge Silicon Radio Ltd.
 *
 * Refer to LICENSE.txt included with this source code for details on
 * the license terms.
 */
#ifndef SDIOEMB_SDIO_CSR_H
#define SDIOEMB_SDIO_CSR_H

/**
 * @defgroup registers CSR specific SDIO registers
 *
 * Registers at 0xF0 - 0xFF in the CCCR are reserved for vendor
 * specific registers.  The registers documented here are specific to
 * following CSR chips:
 *
 *   - BlueCore (6 and later)
 *   - UltraCore
 *@{
 */

/**
 * Interrupt status/host wakeup register.
 *
 * This controls a function's deep sleep state.
 *
 * @see enum sdio_sleep_state
 */
#define SDIO_CSR_SLEEP_STATE 0xf0
#  define SDIO_CSR_SLEEP_STATE_FUNC(f) ((f) << 4)
#  define SDIO_CSR_SLEEP_STATE_RDY_INT_EN  0x02
#  define SDIO_CSR_SLEEP_STATE_WAKE_REQ    0x01

/**
 * Host interrupt clear register.
 *
 * Writing a 1 to bit 0 clears an SDIO interrupt raised by a generic
 * function.
 */
#define SDIO_CSR_HOST_INT 0xf1
#  define SDIO_CSR_HOST_INT_CL 0x01

/**
 * From host scratch register 0.
 *
 * A read/write register that can be used for signalling between the
 * host and the chip.
 *
 * The usage of this register depends on the version of the chip or
 * firmware.
 */
#define SDIO_CSR_FROM_HOST_SCRATCH0 0xf2

/**
 * From host scratch register 1.
 *
 * @see SDIO_CSR_FROM_HOST_SCRATCH0
 */
#define SDIO_CSR_FROM_HOST_SCRATCH1 0xf3

/**
 * To host scratch register 0.
 *
 * A read only register that may be used for signalling between the
 * chip and the host.
 *
 * The usage of this register depends on the version of the chip or
 * firmware.
 */
#define SDIO_CSR_TO_HOST_SCRATCH0 0xf4

/**
 * To host scratch register 1.
 *
 * @see SDIO_CSR_TO_HOST_SCRATCH0
 */
#define SDIO_CSR_TO_HOST_SCRATCH1 0xf5

/**
 * Extended I/O enable.
 *
 * Similar to the standard CCCR I/O Enable register, this is used to
 * detect if an internal reset of a function has occured and
 * (optionally) reenable it.
 *
 * An internal reset is detected by CCCR I/O Enable bit being set and
 * the corresponding EXT_IO_EN bit being clear.
 */
#define SDIO_CSR_EXT_IO_EN 0xf6

/**
 * Deep sleep states as set via the sleep state register.
 *
 * These states are used to control when the chip may go into a deep
 * sleep (a low power mode).
 *
 * Since a chip in deep sleep may not respond to SDIO commands, the
 * host should ensure that the chip is not in deep sleep before
 * attempting SDIO commands to functions 1 to 7.
 *
 * The available states are:
 *
 * AWAKE - chip must not enter deep sleep and should exit deep sleep
 * if it's currently sleeping.
 *
 * TORPID - chip may enter deep sleep.
 *
 * DROWSY - a transition state between TORPID and AWAKE.  This is
 * AWAKE plus the chip asserts an interrupt when the chip is awake.
 *
 * @see SDIO_CSR_SLEEP_STATE
 */
enum sdio_sleep_state {
    SLEEP_STATE_AWAKE  = SDIO_CSR_SLEEP_STATE_WAKE_REQ,
    SLEEP_STATE_DROWSY = SDIO_CSR_SLEEP_STATE_WAKE_REQ | SDIO_CSR_SLEEP_STATE_RDY_INT_EN,
    SLEEP_STATE_TORPID = 0x00,
};

/*@}*/

/*
 * Generic function registers (with byte addresses).
 */

/*
 * SDIO_MODE is chip dependant, see the sdio_mode table in sdio_cspi.c
 * to add support for new chips.
 */
#define SDIO_MODE /* chip dependant */
#  define SDIO_MODE_CSPI_EN 0x40

#endif /* SDIOEMB_SDIO_CSR_H */
