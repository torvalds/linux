/*
 * LISL02DQ.h -- support STMicroelectronics LISD02DQ
 *               3d 2g Linear Accelerometers via SPI
 *
 * Copyright (c) 2007 Jonathan Cameron <jic23@kernel.org>
 *
 * Loosely based upon tle62x0.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef SPI_LIS3L02DQ_H_
#define SPI_LIS3L02DQ_H_
#define LIS3L02DQ_READ_REG(a) ((a) | 0x80)
#define LIS3L02DQ_WRITE_REG(a) a

/* Calibration parameters */
#define LIS3L02DQ_REG_OFFSET_X_ADDR		0x16
#define LIS3L02DQ_REG_OFFSET_Y_ADDR		0x17
#define LIS3L02DQ_REG_OFFSET_Z_ADDR		0x18

#define LIS3L02DQ_REG_GAIN_X_ADDR		0x19
#define LIS3L02DQ_REG_GAIN_Y_ADDR		0x1A
#define LIS3L02DQ_REG_GAIN_Z_ADDR		0x1B

/* Control Register (1 of 2) */
#define LIS3L02DQ_REG_CTRL_1_ADDR		0x20
/* Power ctrl - either bit set corresponds to on*/
#define LIS3L02DQ_REG_CTRL_1_PD_ON		0xC0

/* Decimation Factor  */
#define LIS3L02DQ_DEC_MASK			0x30
#define LIS3L02DQ_REG_CTRL_1_DF_128		0x00
#define LIS3L02DQ_REG_CTRL_1_DF_64		0x10
#define LIS3L02DQ_REG_CTRL_1_DF_32		0x20
#define LIS3L02DQ_REG_CTRL_1_DF_8		(0x10 | 0x20)

/* Self Test Enable */
#define LIS3L02DQ_REG_CTRL_1_SELF_TEST_ON	0x08

/* Axes enable ctrls */
#define LIS3L02DQ_REG_CTRL_1_AXES_Z_ENABLE	0x04
#define LIS3L02DQ_REG_CTRL_1_AXES_Y_ENABLE	0x02
#define LIS3L02DQ_REG_CTRL_1_AXES_X_ENABLE	0x01

/* Control Register (2 of 2) */
#define LIS3L02DQ_REG_CTRL_2_ADDR		0x21

/* Block Data Update only after MSB and LSB read */
#define LIS3L02DQ_REG_CTRL_2_BLOCK_UPDATE	0x40

/* Set to big endian output */
#define LIS3L02DQ_REG_CTRL_2_BIG_ENDIAN		0x20

/* Reboot memory content */
#define LIS3L02DQ_REG_CTRL_2_REBOOT_MEMORY	0x10

/* Interrupt Enable - applies data ready to the RDY pad */
#define LIS3L02DQ_REG_CTRL_2_ENABLE_INTERRUPT	0x08

/* Enable Data Ready Generation - relationship with previous unclear in docs */
#define LIS3L02DQ_REG_CTRL_2_ENABLE_DATA_READY_GENERATION 0x04

/* SPI 3 wire mode */
#define LIS3L02DQ_REG_CTRL_2_THREE_WIRE_SPI_MODE	0x02

/* Data alignment, default is 12 bit right justified
 * - option for 16 bit left justified */
#define LIS3L02DQ_REG_CTRL_2_DATA_ALIGNMENT_16_BIT_LEFT_JUSTIFIED	0x01

/* Interrupt related stuff */
#define LIS3L02DQ_REG_WAKE_UP_CFG_ADDR			0x23

/* Switch from or combination of conditions to and */
#define LIS3L02DQ_REG_WAKE_UP_CFG_BOOLEAN_AND		0x80

/* Latch interrupt request,
 * if on ack must be given by reading the ack register */
#define LIS3L02DQ_REG_WAKE_UP_CFG_LATCH_SRC		0x40

/* Z Interrupt on High (above threshold) */
#define LIS3L02DQ_REG_WAKE_UP_CFG_INTERRUPT_Z_HIGH	0x20
/* Z Interrupt on Low */
#define LIS3L02DQ_REG_WAKE_UP_CFG_INTERRUPT_Z_LOW	0x10
/* Y Interrupt on High */
#define LIS3L02DQ_REG_WAKE_UP_CFG_INTERRUPT_Y_HIGH	0x08
/* Y Interrupt on Low */
#define LIS3L02DQ_REG_WAKE_UP_CFG_INTERRUPT_Y_LOW	0x04
/* X Interrupt on High */
#define LIS3L02DQ_REG_WAKE_UP_CFG_INTERRUPT_X_HIGH	0x02
/* X Interrupt on Low */
#define LIS3L02DQ_REG_WAKE_UP_CFG_INTERRUPT_X_LOW 0x01

/* Register that gives description of what caused interrupt
 * - latched if set in CFG_ADDRES */
#define LIS3L02DQ_REG_WAKE_UP_SRC_ADDR			0x24
/* top bit ignored */
/* Interrupt Active */
#define LIS3L02DQ_REG_WAKE_UP_SRC_INTERRUPT_ACTIVATED	0x40
/* Interupts that have been triggered */
#define LIS3L02DQ_REG_WAKE_UP_SRC_INTERRUPT_Z_HIGH	0x20
#define LIS3L02DQ_REG_WAKE_UP_SRC_INTERRUPT_Z_LOW	0x10
#define LIS3L02DQ_REG_WAKE_UP_SRC_INTERRUPT_Y_HIGH	0x08
#define LIS3L02DQ_REG_WAKE_UP_SRC_INTERRUPT_Y_LOW	0x04
#define LIS3L02DQ_REG_WAKE_UP_SRC_INTERRUPT_X_HIGH	0x02
#define LIS3L02DQ_REG_WAKE_UP_SRC_INTERRUPT_X_LOW	0x01

#define LIS3L02DQ_REG_WAKE_UP_ACK_ADDR			0x25

/* Status register */
#define LIS3L02DQ_REG_STATUS_ADDR			0x27
/* XYZ axis data overrun - first is all overrun? */
#define LIS3L02DQ_REG_STATUS_XYZ_OVERRUN		0x80
#define LIS3L02DQ_REG_STATUS_Z_OVERRUN			0x40
#define LIS3L02DQ_REG_STATUS_Y_OVERRUN			0x20
#define LIS3L02DQ_REG_STATUS_X_OVERRUN			0x10
/* XYZ new data available - first is all 3 available? */
#define LIS3L02DQ_REG_STATUS_XYZ_NEW_DATA		0x08
#define LIS3L02DQ_REG_STATUS_Z_NEW_DATA			0x04
#define LIS3L02DQ_REG_STATUS_Y_NEW_DATA			0x02
#define LIS3L02DQ_REG_STATUS_X_NEW_DATA			0x01

/* The accelerometer readings - low and high bytes.
 * Form of high byte dependent on justification set in ctrl reg */
#define LIS3L02DQ_REG_OUT_X_L_ADDR			0x28
#define LIS3L02DQ_REG_OUT_X_H_ADDR			0x29
#define LIS3L02DQ_REG_OUT_Y_L_ADDR			0x2A
#define LIS3L02DQ_REG_OUT_Y_H_ADDR			0x2B
#define LIS3L02DQ_REG_OUT_Z_L_ADDR			0x2C
#define LIS3L02DQ_REG_OUT_Z_H_ADDR			0x2D

/* Threshold values for all axes and both above and below thresholds
 * - i.e. there is only one value */
#define LIS3L02DQ_REG_THS_L_ADDR			0x2E
#define LIS3L02DQ_REG_THS_H_ADDR			0x2F

#define LIS3L02DQ_DEFAULT_CTRL1 (LIS3L02DQ_REG_CTRL_1_PD_ON	      \
				 | LIS3L02DQ_REG_CTRL_1_AXES_Z_ENABLE \
				 | LIS3L02DQ_REG_CTRL_1_AXES_Y_ENABLE \
				 | LIS3L02DQ_REG_CTRL_1_AXES_X_ENABLE \
				 | LIS3L02DQ_REG_CTRL_1_DF_128)

#define LIS3L02DQ_DEFAULT_CTRL2	0

#define LIS3L02DQ_MAX_TX 12
#define LIS3L02DQ_MAX_RX 12
/**
 * struct lis3l02dq_state - device instance specific data
 * @us:			actual spi_device
 * @trig:		data ready trigger registered with iio
 * @buf_lock:		mutex to protect tx and rx
 * @tx:			transmit buffer
 * @rx:			receive buffer
 **/
struct lis3l02dq_state {
	struct spi_device		*us;
	struct iio_trigger		*trig;
	struct mutex			buf_lock;
	bool				trigger_on;

	u8	tx[LIS3L02DQ_MAX_RX] ____cacheline_aligned;
	u8	rx[LIS3L02DQ_MAX_RX] ____cacheline_aligned;
};

int lis3l02dq_spi_read_reg_8(struct iio_dev *indio_dev,
			     u8 reg_address,
			     u8 *val);

int lis3l02dq_spi_write_reg_8(struct iio_dev *indio_dev,
			      u8 reg_address,
			      u8 val);

int lis3l02dq_disable_all_events(struct iio_dev *indio_dev);

#ifdef CONFIG_IIO_BUFFER
/* At the moment triggers are only used for buffer
 * filling. This may change!
 */
void lis3l02dq_remove_trigger(struct iio_dev *indio_dev);
int lis3l02dq_probe_trigger(struct iio_dev *indio_dev);

int lis3l02dq_configure_buffer(struct iio_dev *indio_dev);
void lis3l02dq_unconfigure_buffer(struct iio_dev *indio_dev);

#ifdef CONFIG_LIS3L02DQ_BUF_RING_SW
#define lis3l02dq_free_buf iio_sw_rb_free
#define lis3l02dq_alloc_buf iio_sw_rb_allocate
#endif
#ifdef CONFIG_LIS3L02DQ_BUF_KFIFO
#define lis3l02dq_free_buf iio_kfifo_free
#define lis3l02dq_alloc_buf iio_kfifo_allocate
#endif
irqreturn_t lis3l02dq_data_rdy_trig_poll(int irq, void *private);
#define lis3l02dq_th lis3l02dq_data_rdy_trig_poll

#else /* CONFIG_IIO_BUFFER */
#define lis3l02dq_th lis3l02dq_nobuffer

static inline void lis3l02dq_remove_trigger(struct iio_dev *indio_dev)
{
}
static inline int lis3l02dq_probe_trigger(struct iio_dev *indio_dev)
{
	return 0;
}

static int lis3l02dq_configure_buffer(struct iio_dev *indio_dev)
{
	return 0;
}
static inline void lis3l02dq_unconfigure_buffer(struct iio_dev *indio_dev)
{
}
#endif /* CONFIG_IIO_BUFFER */
#endif /* SPI_LIS3L02DQ_H_ */
