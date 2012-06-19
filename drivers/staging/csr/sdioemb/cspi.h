/*
 * CSPI definitions.
 *
 * Copyright (C) 2007 Cambridge Silicon Radio Ltd.
 *
 * Refer to LICENSE.txt included with this source code for details on
 * the license terms.
 */
#ifndef SDIOEMB_CSPI_H
#define SDIOEMB_CSPI_H

/**
 * @addtogroup sdriver
 *@{*/

#define CSPI_FUNC(f) (f)
#define CSPI_READ    0x10
#define CSPI_WRITE   0x20
#define CSPI_BURST   0x40
#define CSPI_TYPE_MASK 0x70

/**
 * CSPI_MODE function 0 register.
 *
 * Various CSPI mode settings.
 *
 * @see CSPI specification (CS-110124-SP)
 */
#define CSPI_MODE 0xf7
#  define CSPI_MODE_PADDED_WRITE_HDRS      (1 << 7)
#  define CSPI_MODE_PADDED_READ_HDRS       (1 << 6)
/**
 * BigEndianRegisters bit of \ref CSPI_MODE -- enable big-endian CSPI
 * register reads and writes.
 *
 * @warning This bit should never be set as it's not possible to use
 * this mode without knowledge of which registers are 8 bit and which
 * are 16 bit.
 */
#  define CSPI_MODE_BE_REG                 (1 << 5)
#  define CSPI_MODE_BE_BURST               (1 << 4)
#  define CSPI_MODE_INT_ACTIVE_HIGH        (1 << 3)
#  define CSPI_MODE_INT_ON_ERR             (1 << 2)
#  define CSPI_MODE_LEN_FIELD_PRESENT      (1 << 1)
#  define CSPI_MODE_DRV_MISO_ON_RISING_CLK (1 << 0)

#define CSPI_STATUS 0xf8

#define CSPI_PADDING 0xf9
#  define CSPI_PADDING_REG(p)   ((p) << 0)
#  define CSPI_PADDING_BURST(p) ((p) << 4)

#define CSPI_PADDING_MAX       15
#define CSPI_PADDING_REG_DFLT   0
#define CSPI_PADDING_BURST_DFLT 2

/* cmd byte, 3 byte addr, padding, error byte, data word */
#define CSPI_REG_TRANSFER_LEN (1 + 3 + CSPI_PADDING_MAX + 1 + 2)

/*@}*/

#endif /* #ifndef SDIOEMB_CSPI_H */
