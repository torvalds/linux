/**
 *
 * Synaptics Register Mapped Interface (RMI4) I2C Physical Layer Driver.
 * Copyright (c) 2007-2010, Synaptics Incorporated
 *
 * Author: Js HA <js.ha@stericsson.com> for ST-Ericsson
 * Author: Naveen Kumar G <naveen.gaddipati@stericsson.com> for ST-Ericsson
 * Copyright 2010 (c) ST-Ericsson AB
 */
/*
 * This file is licensed under the GPL2 license.
 *
 *#############################################################################
 * GPL
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 *#############################################################################
 */

#include <linux/input.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/regulator/consumer.h>
#include <linux/module.h>
#include <linux/input/mt.h>
#include "synaptics_i2c_rmi4.h"

/* TODO: for multiple device support will need a per-device mutex */
#define DRIVER_NAME "synaptics_rmi4_i2c"

#define MAX_ERROR_REPORT	6
#define MAX_TOUCH_MAJOR		15
#define MAX_RETRY_COUNT		5
#define STD_QUERY_LEN		21
#define PAGE_LEN		2
#define DATA_BUF_LEN		32
#define BUF_LEN			37
#define QUERY_LEN		9
#define DATA_LEN		12
#define HAS_TAP			0x01
#define HAS_PALMDETECT		0x01
#define HAS_ROTATE		0x02
#define HAS_TAPANDHOLD		0x02
#define HAS_DOUBLETAP		0x04
#define HAS_EARLYTAP		0x08
#define HAS_RELEASE		0x08
#define HAS_FLICK		0x10
#define HAS_PRESS		0x20
#define HAS_PINCH		0x40

#define MASK_16BIT		0xFFFF
#define MASK_8BIT		0xFF
#define MASK_7BIT		0x7F
#define MASK_5BIT		0x1F
#define MASK_4BIT		0x0F
#define MASK_3BIT		0x07
#define MASK_2BIT		0x03
#define TOUCHPAD_CTRL_INTR	0x8
#define PDT_START_SCAN_LOCATION (0x00E9)
#define PDT_END_SCAN_LOCATION	(0x000A)
#define PDT_ENTRY_SIZE		(0x0006)
#define SYNAPTICS_RMI4_TOUCHPAD_FUNC_NUM	(0x11)
#define SYNAPTICS_RMI4_DEVICE_CONTROL_FUNC_NUM	(0x01)

/**
 * struct synaptics_rmi4_fn_desc - contains the function descriptor information
 * @query_base_addr: base address for query
 * @cmd_base_addr: base address for command
 * @ctrl_base_addr: base address for control
 * @data_base_addr: base address for data
 * @intr_src_count: count for the interrupt source
 * @fn_number: function number
 *
 * This structure is used to gives the function descriptor information
 * of the particular functionality.
 */
struct synaptics_rmi4_fn_desc {
	unsigned char	query_base_addr;
	unsigned char	cmd_base_addr;
	unsigned char	ctrl_base_addr;
	unsigned char	data_base_addr;
	unsigned char	intr_src_count;
	unsigned char	fn_number;
};

/**
 * struct synaptics_rmi4_fn - contains the function information
 * @fn_number: function number
 * @num_of_data_sources: number of data sources
 * @num_of_data_points: number of fingers touched
 * @size_of_data_register_block: data register block size
 * @index_to_intr_reg: index for interrupt register
 * @intr_mask: interrupt mask value
 * @fn_desc: variable for function descriptor structure
 * @link: linked list for function descriptors
 *
 * This structure gives information about the number of data sources and
 * the number of data registers associated with the function.
 */
struct synaptics_rmi4_fn {
	unsigned char		fn_number;
	unsigned char		num_of_data_sources;
	unsigned char		num_of_data_points;
	unsigned char		size_of_data_register_block;
	unsigned char		index_to_intr_reg;
	unsigned char		intr_mask;
	struct synaptics_rmi4_fn_desc	fn_desc;
	struct list_head	link;
};

/**
 * struct synaptics_rmi4_device_info - contains the rmi4 device information
 * @version_major: protocol major version number
 * @version_minor: protocol minor version number
 * @manufacturer_id: manufacturer identification byte
 * @product_props: product properties information
 * @product_info: product info array
 * @date_code: device manufacture date
 * @tester_id: tester id array
 * @serial_number: serial number for that device
 * @product_id_string: product id for the device
 * @support_fn_list: linked list for device information
 *
 * This structure gives information about the number of data sources and
 * the number of data registers associated with the function.
 */
struct synaptics_rmi4_device_info {
	unsigned int		version_major;
	unsigned int		version_minor;
	unsigned char		manufacturer_id;
	unsigned char		product_props;
	unsigned char		product_info[2];
	unsigned char		date_code[3];
	unsigned short		tester_id;
	unsigned short		serial_number;
	unsigned char		product_id_string[11];
	struct list_head	support_fn_list;
};

/**
 * struct synaptics_rmi4_data - contains the rmi4 device data
 * @rmi4_mod_info: structure variable for rmi4 device info
 * @input_dev: pointer for input device
 * @i2c_client: pointer for i2c client
 * @board: constant pointer for touch platform data
 * @fn_list_mutex: mutex for function list
 * @rmi4_page_mutex: mutex for rmi4 page
 * @current_page: variable for integer
 * @number_of_interrupt_register: interrupt registers count
 * @fn01_ctrl_base_addr: control base address for fn01
 * @fn01_query_base_addr: query base address for fn01
 * @fn01_data_base_addr: data base address for fn01
 * @sensor_max_x: sensor maximum x value
 * @sensor_max_y: sensor maximum y value
 * @regulator: pointer to the regulator structure
 * @wait: wait queue structure variable
 * @touch_stopped: flag to stop the thread function
 * @fingers_supported: maximum supported fingers
 *
 * This structure gives the device data information.
 */
struct synaptics_rmi4_data {
	struct synaptics_rmi4_device_info rmi4_mod_info;
	struct input_dev	*input_dev;
	struct i2c_client	*i2c_client;
	const struct synaptics_rmi4_platform_data *board;
	struct mutex		fn_list_mutex;
	struct mutex		rmi4_page_mutex;
	int			current_page;
	unsigned int		number_of_interrupt_register;
	unsigned short		fn01_ctrl_base_addr;
	unsigned short		fn01_query_base_addr;
	unsigned short		fn01_data_base_addr;
	int			sensor_max_x;
	int			sensor_max_y;
	struct regulator	*regulator;
	wait_queue_head_t	wait;
	bool			touch_stopped;
	unsigned char		fingers_supported;
};

/**
 * synaptics_rmi4_set_page() - sets the page
 * @pdata: pointer to synaptics_rmi4_data structure
 * @address: set the address of the page
 *
 * This function is used to set the page and returns integer.
 */
static int synaptics_rmi4_set_page(struct synaptics_rmi4_data *pdata,
					unsigned int address)
{
	unsigned char	txbuf[PAGE_LEN];
	int		retval;
	unsigned int	page;
	struct i2c_client *i2c = pdata->i2c_client;

	page	= ((address >> 8) & MASK_8BIT);
	if (page != pdata->current_page) {
		txbuf[0]	= MASK_8BIT;
		txbuf[1]	= page;
		retval	= i2c_master_send(i2c, txbuf, PAGE_LEN);
		if (retval != PAGE_LEN)
			dev_err(&i2c->dev, "%s:failed:%d\n", __func__, retval);
		else
			pdata->current_page = page;
	} else
		retval = PAGE_LEN;
	return retval;
}
/**
 * synaptics_rmi4_i2c_block_read() - read the block of data
 * @pdata: pointer to synaptics_rmi4_data structure
 * @address: read the block of data from this offset
 * @valp: pointer to a buffer containing the data to be read
 * @size: number of bytes to read
 *
 * This function is to read the block of data and returns integer.
 */
static int synaptics_rmi4_i2c_block_read(struct synaptics_rmi4_data *pdata,
						unsigned short address,
						unsigned char *valp, int size)
{
	int retval = 0;
	int retry_count = 0;
	int index;
	struct i2c_client *i2c = pdata->i2c_client;

	mutex_lock(&(pdata->rmi4_page_mutex));
	retval = synaptics_rmi4_set_page(pdata, address);
	if (retval != PAGE_LEN)
		goto exit;
	index = address & MASK_8BIT;
retry:
	retval = i2c_smbus_read_i2c_block_data(i2c, index, size, valp);
	if (retval != size) {
		if (++retry_count == MAX_RETRY_COUNT)
			dev_err(&i2c->dev,
				"%s:address 0x%04x size %d failed:%d\n",
					__func__, address, size, retval);
		else {
			synaptics_rmi4_set_page(pdata, address);
			goto retry;
		}
	}
exit:
	mutex_unlock(&(pdata->rmi4_page_mutex));
	return retval;
}

/**
 * synaptics_rmi4_i2c_byte_write() - write the single byte data
 * @pdata: pointer to synaptics_rmi4_data structure
 * @address: write the block of data from this offset
 * @data: data to be write
 *
 * This function is to write the single byte data and returns integer.
 */
static int synaptics_rmi4_i2c_byte_write(struct synaptics_rmi4_data *pdata,
						unsigned short address,
						unsigned char data)
{
	unsigned char txbuf[2];
	int retval = 0;
	struct i2c_client *i2c = pdata->i2c_client;

	/* Can't have anyone else changing the page behind our backs */
	mutex_lock(&(pdata->rmi4_page_mutex));

	retval = synaptics_rmi4_set_page(pdata, address);
	if (retval != PAGE_LEN)
		goto exit;
	txbuf[0]	= address & MASK_8BIT;
	txbuf[1]	= data;
	retval		= i2c_master_send(pdata->i2c_client, txbuf, 2);
	/* Add in retry on writes only in certain error return values */
	if (retval != 2) {
		dev_err(&i2c->dev, "%s:failed:%d\n", __func__, retval);
		retval = -EIO;
	} else
		retval = 1;
exit:
	mutex_unlock(&(pdata->rmi4_page_mutex));
	return retval;
}

/**
 * synpatics_rmi4_touchpad_report() - reports for the rmi4 touchpad device
 * @pdata: pointer to synaptics_rmi4_data structure
 * @rfi: pointer to synaptics_rmi4_fn structure
 *
 * This function calls to reports for the rmi4 touchpad device
 */
static int synpatics_rmi4_touchpad_report(struct synaptics_rmi4_data *pdata,
						struct synaptics_rmi4_fn *rfi)
{
	/* number of touch points - fingers down in this case */
	int	touch_count = 0;
	int	finger;
	int	finger_registers;
	int	reg;
	int	finger_shift;
	int	finger_status;
	int	retval;
	int	x, y;
	int	wx, wy;
	unsigned short	data_base_addr;
	unsigned short	data_offset;
	unsigned char	data_reg_blk_size;
	unsigned char	values[2];
	unsigned char	data[DATA_LEN];
	unsigned char	fingers_supported = pdata->fingers_supported;
	struct	i2c_client *client = pdata->i2c_client;
	struct	input_dev *input_dev = pdata->input_dev;

	/* get 2D sensor finger data */
	/*
	 * First get the finger status field - the size of the finger status
	 * field is determined by the number of finger supporte - 2 bits per
	 * finger, so the number of registers to read is:
	 * registerCount = ceil(numberOfFingers/4).
	 * Read the required number of registers and check each 2 bit field to
	 * determine if a finger is down:
	 *	00 = finger not present,
	 *	01 = finger present and data accurate,
	 *	10 = finger present but data may not be accurate,
	 *	11 = reserved for product use.
	 */
	finger_registers	= (fingers_supported + 3)/4;
	data_base_addr		= rfi->fn_desc.data_base_addr;
	retval = synaptics_rmi4_i2c_block_read(pdata, data_base_addr, values,
							finger_registers);
	if (retval != finger_registers) {
		dev_err(&client->dev, "%s:read status registers failed\n",
								__func__);
		return 0;
	}
	/*
	 * For each finger present, read the proper number of registers
	 * to get absolute data.
	 */
	data_reg_blk_size = rfi->size_of_data_register_block;
	for (finger = 0; finger < fingers_supported; finger++) {
		/* determine which data byte the finger status is in */
		reg = finger/4;
		/* bit shift to get finger's status */
		finger_shift	= (finger % 4) * 2;
		finger_status	= (values[reg] >> finger_shift) & 3;
		/*
		 * if finger status indicates a finger is present then
		 * read the finger data and report it
		 */
		input_mt_slot(input_dev, finger);
		input_mt_report_slot_state(input_dev, MT_TOOL_FINGER,
							finger_status != 0);

		if (finger_status) {
			/* Read the finger data */
			data_offset = data_base_addr +
					((finger * data_reg_blk_size) +
					finger_registers);
			retval = synaptics_rmi4_i2c_block_read(pdata,
						data_offset, data,
						data_reg_blk_size);
			if (retval != data_reg_blk_size) {
				dev_err(&client->dev, "%s:read data failed\n",
								__func__);
				return 0;
			}
			x = (data[0] << 4) | (data[2] & MASK_4BIT);
			y = (data[1] << 4) | ((data[2] >> 4) & MASK_4BIT);
			wy = (data[3] >> 4) & MASK_4BIT;
			wx = (data[3] & MASK_4BIT);

			if (pdata->board->x_flip)
				x = pdata->sensor_max_x - x;
			if (pdata->board->y_flip)
				y = pdata->sensor_max_y - y;

			input_report_abs(input_dev, ABS_MT_TOUCH_MAJOR,
								max(wx, wy));
			input_report_abs(input_dev, ABS_MT_POSITION_X, x);
			input_report_abs(input_dev, ABS_MT_POSITION_Y, y);

			/* number of active touch points */
			touch_count++;
		}
	}

	/* sync after groups of events */
	input_mt_sync_frame(input_dev);
	input_sync(input_dev);
	/* return the number of touch points */
	return touch_count;
}

/**
 * synaptics_rmi4_report_device() - reports the rmi4 device
 * @pdata: pointer to synaptics_rmi4_data structure
 * @rfi: pointer to synaptics_rmi4_fn
 *
 * This function is used to call the report function of the rmi4 device.
 */
static int synaptics_rmi4_report_device(struct synaptics_rmi4_data *pdata,
					struct synaptics_rmi4_fn *rfi)
{
	int touch = 0;
	struct	i2c_client *client = pdata->i2c_client;
	static int num_error_reports;
	if (rfi->fn_number != SYNAPTICS_RMI4_TOUCHPAD_FUNC_NUM) {
		num_error_reports++;
		if (num_error_reports < MAX_ERROR_REPORT)
			dev_err(&client->dev, "%s:report not supported\n",
								__func__);
	} else
		touch = synpatics_rmi4_touchpad_report(pdata, rfi);
	return touch;
}
/**
 * synaptics_rmi4_sensor_report() - reports to input subsystem
 * @pdata: pointer to synaptics_rmi4_data structure
 *
 * This function is used to reads in all data sources and reports
 * them to the input subsystem.
 */
static int synaptics_rmi4_sensor_report(struct synaptics_rmi4_data *pdata)
{
	unsigned char	intr_status[4];
	/* number of touch points - fingers or buttons */
	int touch = 0;
	unsigned int retval;
	struct synaptics_rmi4_fn		*rfi;
	struct synaptics_rmi4_device_info	*rmi;
	struct	i2c_client *client = pdata->i2c_client;

	/*
	 * Get the interrupt status from the function $01
	 * control register+1 to find which source(s) were interrupting
	 * so we can read the data from the source(s) (2D sensor, buttons..)
	 */
	retval = synaptics_rmi4_i2c_block_read(pdata,
					pdata->fn01_data_base_addr + 1,
					intr_status,
					pdata->number_of_interrupt_register);
	if (retval != pdata->number_of_interrupt_register) {
		dev_err(&client->dev,
				"could not read interrupt status registers\n");
		return 0;
	}
	/*
	 * check each function that has data sources and if the interrupt for
	 * that triggered then call that RMI4 functions report() function to
	 * gather data and report it to the input subsystem
	 */
	rmi = &(pdata->rmi4_mod_info);
	list_for_each_entry(rfi, &rmi->support_fn_list, link) {
		if (rfi->num_of_data_sources) {
			if (intr_status[rfi->index_to_intr_reg] &
							rfi->intr_mask)
				touch = synaptics_rmi4_report_device(pdata,
									rfi);
		}
	}
	/* return the number of touch points */
	return touch;
}

/**
 * synaptics_rmi4_irq() - thread function for rmi4 attention line
 * @irq: irq value
 * @data: void pointer
 *
 * This function is interrupt thread function. It just notifies the
 * application layer that attention is required.
 */
static irqreturn_t synaptics_rmi4_irq(int irq, void *data)
{
	struct synaptics_rmi4_data *pdata = data;
	int touch_count;
	do {
		touch_count = synaptics_rmi4_sensor_report(pdata);
		if (touch_count)
			wait_event_timeout(pdata->wait, pdata->touch_stopped,
							msecs_to_jiffies(1));
		else
			break;
	} while (!pdata->touch_stopped);
	return IRQ_HANDLED;
}

/**
 * synpatics_rmi4_touchpad_detect() - detects the rmi4 touchpad device
 * @pdata: pointer to synaptics_rmi4_data structure
 * @rfi: pointer to synaptics_rmi4_fn structure
 * @fd: pointer to synaptics_rmi4_fn_desc structure
 * @interruptcount: count the number of interrupts
 *
 * This function calls to detects the rmi4 touchpad device
 */
static int synpatics_rmi4_touchpad_detect(struct synaptics_rmi4_data *pdata,
					struct synaptics_rmi4_fn *rfi,
					struct synaptics_rmi4_fn_desc *fd,
					unsigned int interruptcount)
{
	unsigned char	queries[QUERY_LEN];
	unsigned short	intr_offset;
	unsigned char	abs_data_size;
	unsigned char	abs_data_blk_size;
	unsigned char	egr_0, egr_1;
	unsigned int	all_data_blk_size;
	int	has_pinch, has_flick, has_tap;
	int	has_tapandhold, has_doubletap;
	int	has_earlytap, has_press;
	int	has_palmdetect, has_rotate;
	int	has_rel;
	int	i;
	int	retval;
	struct	i2c_client *client = pdata->i2c_client;

	rfi->fn_desc.query_base_addr	= fd->query_base_addr;
	rfi->fn_desc.data_base_addr	= fd->data_base_addr;
	rfi->fn_desc.intr_src_count	= fd->intr_src_count;
	rfi->fn_desc.fn_number		= fd->fn_number;
	rfi->fn_number			= fd->fn_number;
	rfi->num_of_data_sources	= fd->intr_src_count;
	rfi->fn_desc.ctrl_base_addr	= fd->ctrl_base_addr;
	rfi->fn_desc.cmd_base_addr	= fd->cmd_base_addr;

	/*
	 * need to get number of fingers supported, data size, etc.
	 * to be used when getting data since the number of registers to
	 * read depends on the number of fingers supported and data size.
	 */
	retval = synaptics_rmi4_i2c_block_read(pdata, fd->query_base_addr,
							queries,
							sizeof(queries));
	if (retval != sizeof(queries)) {
		dev_err(&client->dev, "%s:read function query registers\n",
							__func__);
		return retval;
	}
	/*
	 * 2D data sources have only 3 bits for the number of fingers
	 * supported - so the encoding is a bit weird.
	 */
	if ((queries[1] & MASK_3BIT) <= 4)
		/* add 1 since zero based */
		rfi->num_of_data_points = (queries[1] & MASK_3BIT) + 1;
	else {
		/*
		 * a value of 5 is up to 10 fingers - 6 and 7 are reserved
		 * (shouldn't get these i int retval;n a normal 2D source).
		 */
		if ((queries[1] & MASK_3BIT) == 5)
			rfi->num_of_data_points = 10;
	}
	pdata->fingers_supported = rfi->num_of_data_points;
	/* Need to get interrupt info for handling interrupts */
	rfi->index_to_intr_reg = (interruptcount + 7)/8;
	if (rfi->index_to_intr_reg != 0)
		rfi->index_to_intr_reg -= 1;
	/*
	 * loop through interrupts for each source in fn $11
	 * and or in a bit to the interrupt mask for each.
	 */
	intr_offset = interruptcount % 8;
	rfi->intr_mask = 0;
	for (i = intr_offset;
		i < ((fd->intr_src_count & MASK_3BIT) + intr_offset); i++)
		rfi->intr_mask |= 1 << i;

	/* Size of just the absolute data for one finger */
	abs_data_size	= queries[5] & MASK_2BIT;
	/* One each for X and Y, one for LSB for X & Y, one for W, one for Z */
	abs_data_blk_size = 3 + (2 * (abs_data_size == 0 ? 1 : 0));
	rfi->size_of_data_register_block = abs_data_blk_size;

	/*
	 * need to determine the size of data to read - this depends on
	 * conditions such as whether Relative data is reported and if Gesture
	 * data is reported.
	 */
	egr_0 = queries[7];
	egr_1 = queries[8];

	/*
	 * Get info about what EGR data is supported, whether it has
	 * Relative data supported, etc.
	 */
	has_pinch	= egr_0 & HAS_PINCH;
	has_flick	= egr_0 & HAS_FLICK;
	has_tap		= egr_0 & HAS_TAP;
	has_earlytap	= egr_0 & HAS_EARLYTAP;
	has_press	= egr_0 & HAS_PRESS;
	has_rotate	= egr_1 & HAS_ROTATE;
	has_rel		= queries[1] & HAS_RELEASE;
	has_tapandhold	= egr_0 & HAS_TAPANDHOLD;
	has_doubletap	= egr_0 & HAS_DOUBLETAP;
	has_palmdetect	= egr_1 & HAS_PALMDETECT;

	/*
	 * Size of all data including finger status, absolute data for each
	 * finger, relative data and EGR data
	 */
	all_data_blk_size =
		/* finger status, four fingers per register */
		((rfi->num_of_data_points + 3) / 4) +
		/* absolute data, per finger times number of fingers */
		(abs_data_blk_size * rfi->num_of_data_points) +
		/*
		 * two relative registers (if relative is being reported)
		 */
		2 * has_rel +
		/*
		 * F11_2D_data8 is only present if the egr_0
		 * register is non-zero.
		 */
		!!(egr_0) +
		/*
		 * F11_2D_data9 is only present if either egr_0 or
		 * egr_1 registers are non-zero.
		 */
		(egr_0 || egr_1) +
		/*
		 * F11_2D_data10 is only present if EGR_PINCH or EGR_FLICK of
		 * egr_0 reports as 1.
		 */
		!!(has_pinch | has_flick) +
		/*
		 * F11_2D_data11 and F11_2D_data12 are only present if
		 * EGR_FLICK of egr_0 reports as 1.
		 */
		2 * !!(has_flick);
	return retval;
}

/**
 * synaptics_rmi4_touchpad_config() - configures the rmi4 touchpad device
 * @pdata: pointer to synaptics_rmi4_data structure
 * @rfi: pointer to synaptics_rmi4_fn structure
 *
 * This function calls to configures the rmi4 touchpad device
 */
int synaptics_rmi4_touchpad_config(struct synaptics_rmi4_data *pdata,
						struct synaptics_rmi4_fn *rfi)
{
	/*
	 * For the data source - print info and do any
	 * source specific configuration.
	 */
	unsigned char data[BUF_LEN];
	int retval = 0;
	struct	i2c_client *client = pdata->i2c_client;

	/* Get and print some info about the data source... */
	/* To Query 2D devices we need to read from the address obtained
	 * from the function descriptor stored in the RMI function info.
	 */
	retval = synaptics_rmi4_i2c_block_read(pdata,
						rfi->fn_desc.query_base_addr,
						data, QUERY_LEN);
	if (retval != QUERY_LEN)
		dev_err(&client->dev, "%s:read query registers failed\n",
								__func__);
	else {
		retval = synaptics_rmi4_i2c_block_read(pdata,
						rfi->fn_desc.ctrl_base_addr,
						data, DATA_BUF_LEN);
		if (retval != DATA_BUF_LEN) {
			dev_err(&client->dev,
				"%s:read control registers failed\n",
								__func__);
			return retval;
		}
		/* Store these for use later*/
		pdata->sensor_max_x = ((data[6] & MASK_8BIT) << 0) |
						((data[7] & MASK_4BIT) << 8);
		pdata->sensor_max_y = ((data[8] & MASK_5BIT) << 0) |
						((data[9] & MASK_4BIT) << 8);
	}
	return retval;
}

/**
 * synaptics_rmi4_i2c_query_device() - query the rmi4 device
 * @pdata: pointer to synaptics_rmi4_data structure
 *
 * This function is used to query the rmi4 device.
 */
static int synaptics_rmi4_i2c_query_device(struct synaptics_rmi4_data *pdata)
{
	int i;
	int retval;
	unsigned char std_queries[STD_QUERY_LEN];
	unsigned char intr_count = 0;
	int data_sources = 0;
	unsigned int ctrl_offset;
	struct synaptics_rmi4_fn *rfi;
	struct synaptics_rmi4_fn_desc	rmi_fd;
	struct synaptics_rmi4_device_info *rmi;
	struct	i2c_client *client = pdata->i2c_client;

	/*
	 * init the physical drivers RMI module
	 * info list of functions
	 */
	INIT_LIST_HEAD(&pdata->rmi4_mod_info.support_fn_list);

	/*
	 * Read the Page Descriptor Table to determine what functions
	 * are present
	 */
	for (i = PDT_START_SCAN_LOCATION; i > PDT_END_SCAN_LOCATION;
						i -= PDT_ENTRY_SIZE) {
		retval = synaptics_rmi4_i2c_block_read(pdata, i,
						(unsigned char *)&rmi_fd,
						sizeof(rmi_fd));
		if (retval != sizeof(rmi_fd)) {
			/* failed to read next PDT entry */
			dev_err(&client->dev, "%s: read error\n", __func__);
			return -EIO;
		}
		rfi = NULL;
		if (rmi_fd.fn_number) {
			switch (rmi_fd.fn_number & MASK_8BIT) {
			case SYNAPTICS_RMI4_DEVICE_CONTROL_FUNC_NUM:
				pdata->fn01_query_base_addr =
						rmi_fd.query_base_addr;
				pdata->fn01_ctrl_base_addr =
						rmi_fd.ctrl_base_addr;
				pdata->fn01_data_base_addr =
						rmi_fd.data_base_addr;
				break;
			case SYNAPTICS_RMI4_TOUCHPAD_FUNC_NUM:
				if (rmi_fd.intr_src_count) {
					rfi = kmalloc(sizeof(*rfi),
						      GFP_KERNEL);
					if (!rfi)
						return -ENOMEM;
					retval = synpatics_rmi4_touchpad_detect
								(pdata,	rfi,
								&rmi_fd,
								intr_count);
					if (retval < 0) {
						kfree(rfi);
						return retval;
					}
				}
				break;
			}
			/* interrupt count for next iteration */
			intr_count += (rmi_fd.intr_src_count & MASK_3BIT);
			/*
			 * We only want to add functions to the list
			 * that have data associated with them.
			 */
			if (rfi && rmi_fd.intr_src_count) {
				/* link this function info to the RMI module */
				mutex_lock(&(pdata->fn_list_mutex));
				list_add_tail(&rfi->link,
					&pdata->rmi4_mod_info.support_fn_list);
				mutex_unlock(&(pdata->fn_list_mutex));
			}
		} else {
			/*
			 * A zero in the function number
			 * signals the end of the PDT
			 */
			dev_dbg(&client->dev,
				"%s:end of PDT\n", __func__);
			break;
		}
	}
	/*
	 * calculate the interrupt register count - used in the
	 * ISR to read the correct number of interrupt registers
	 */
	pdata->number_of_interrupt_register = (intr_count + 7) / 8;
	/*
	 * Function $01 will be used to query the product properties,
	 * and product ID  so we had to read the PDT above first to get
	 * the Fn $01 query address and prior to filling in the product
	 * info. NOTE: Even an unflashed device will still have FN $01.
	 */

	/* Load up the standard queries and get the RMI4 module info */
	retval = synaptics_rmi4_i2c_block_read(pdata,
					pdata->fn01_query_base_addr,
					std_queries,
					sizeof(std_queries));
	if (retval != sizeof(std_queries)) {
		dev_err(&client->dev, "%s:Failed reading queries\n",
							__func__);
		 return -EIO;
	}

	/* Currently supported RMI version is 4.0 */
	pdata->rmi4_mod_info.version_major	= 4;
	pdata->rmi4_mod_info.version_minor	= 0;
	/*
	 * get manufacturer id, product_props, product info,
	 * date code, tester id, serial num and product id (name)
	 */
	pdata->rmi4_mod_info.manufacturer_id	= std_queries[0];
	pdata->rmi4_mod_info.product_props	= std_queries[1];
	pdata->rmi4_mod_info.product_info[0]	= std_queries[2];
	pdata->rmi4_mod_info.product_info[1]	= std_queries[3];
	/* year - 2001-2032 */
	pdata->rmi4_mod_info.date_code[0]	= std_queries[4] & MASK_5BIT;
	/* month - 1-12 */
	pdata->rmi4_mod_info.date_code[1]	= std_queries[5] & MASK_4BIT;
	/* day - 1-31 */
	pdata->rmi4_mod_info.date_code[2]	= std_queries[6] & MASK_5BIT;
	pdata->rmi4_mod_info.tester_id = ((std_queries[7] & MASK_7BIT) << 8) |
						(std_queries[8] & MASK_7BIT);
	pdata->rmi4_mod_info.serial_number =
		((std_queries[9] & MASK_7BIT) << 8) |
				(std_queries[10] & MASK_7BIT);
	memcpy(pdata->rmi4_mod_info.product_id_string, &std_queries[11], 10);

	/* Check if this is a Synaptics device - report if not. */
	if (pdata->rmi4_mod_info.manufacturer_id != 1)
		dev_err(&client->dev, "%s: non-Synaptics mfg id:%d\n",
			__func__, pdata->rmi4_mod_info.manufacturer_id);

	list_for_each_entry(rfi, &pdata->rmi4_mod_info.support_fn_list, link)
		data_sources += rfi->num_of_data_sources;
	if (data_sources) {
		rmi = &(pdata->rmi4_mod_info);
		list_for_each_entry(rfi, &rmi->support_fn_list, link) {
			if (rfi->num_of_data_sources) {
				if (rfi->fn_number ==
					SYNAPTICS_RMI4_TOUCHPAD_FUNC_NUM) {
					retval = synaptics_rmi4_touchpad_config
								(pdata, rfi);
					if (retval < 0)
						return retval;
				} else
					dev_err(&client->dev,
						"%s:fn_number not supported\n",
								__func__);
				/*
				 * Turn on interrupts for this
				 * function's data sources.
				 */
				ctrl_offset = pdata->fn01_ctrl_base_addr + 1 +
							rfi->index_to_intr_reg;
				retval = synaptics_rmi4_i2c_byte_write(pdata,
							ctrl_offset,
							rfi->intr_mask);
				if (retval < 0)
					return retval;
			}
		}
	}
	return 0;
}

/**
 * synaptics_rmi4_probe() - Initialze the i2c-client touchscreen driver
 * @i2c: i2c client structure pointer
 * @id:i2c device id pointer
 *
 * This function will allocate and initialize the instance
 * data and request the irq and set the instance data as the clients
 * platform data then register the physical driver which will do a scan of
 * the rmi4 Physical Device Table and enumerate any rmi4 functions that
 * have data sources associated with them.
 */
static int synaptics_rmi4_probe
	(struct i2c_client *client, const struct i2c_device_id *dev_id)
{
	int retval;
	unsigned char intr_status[4];
	struct synaptics_rmi4_data *rmi4_data;
	const struct synaptics_rmi4_platform_data *platformdata =
						client->dev.platform_data;

	if (!i2c_check_functionality(client->adapter,
					I2C_FUNC_SMBUS_BYTE_DATA)) {
		dev_err(&client->dev, "i2c smbus byte data not supported\n");
		return -EIO;
	}

	if (!platformdata) {
		dev_err(&client->dev, "%s: no platform data\n", __func__);
		return -EINVAL;
	}

	/* Allocate and initialize the instance data for this client */
	rmi4_data = kcalloc(2, sizeof(struct synaptics_rmi4_data),
			    GFP_KERNEL);
	if (!rmi4_data)
		return -ENOMEM;

	rmi4_data->input_dev = input_allocate_device();
	if (rmi4_data->input_dev == NULL) {
		dev_err(&client->dev, "%s:input device alloc failed\n",
						__func__);
		retval = -ENOMEM;
		goto err_input;
	}

	rmi4_data->regulator = regulator_get(&client->dev, "vdd");
	if (IS_ERR(rmi4_data->regulator)) {
		dev_err(&client->dev, "%s:get regulator failed\n",
							__func__);
		retval = PTR_ERR(rmi4_data->regulator);
		goto err_get_regulator;
	}
	retval = regulator_enable(rmi4_data->regulator);
	if (retval < 0) {
		dev_err(&client->dev, "%s:regulator enable failed\n",
							__func__);
		goto err_regulator_enable;
	}
	init_waitqueue_head(&rmi4_data->wait);
	/*
	 * Copy i2c_client pointer into RTID's i2c_client pointer for
	 * later use in rmi4_read, rmi4_write, etc.
	 */
	rmi4_data->i2c_client		= client;
	/* So we set the page correctly the first time */
	rmi4_data->current_page		= MASK_16BIT;
	rmi4_data->board		= platformdata;
	rmi4_data->touch_stopped	= false;

	/* init the mutexes for maintain the lists */
	mutex_init(&(rmi4_data->fn_list_mutex));
	mutex_init(&(rmi4_data->rmi4_page_mutex));

	/*
	 * Register physical driver - this will call the detect function that
	 * will then scan the device and determine the supported
	 * rmi4 functions.
	 */
	retval = synaptics_rmi4_i2c_query_device(rmi4_data);
	if (retval) {
		dev_err(&client->dev, "%s: rmi4 query device failed\n",
							__func__);
		goto err_query_dev;
	}

	/* Store the instance data in the i2c_client */
	i2c_set_clientdata(client, rmi4_data);

	/*initialize the input device parameters */
	rmi4_data->input_dev->name	= DRIVER_NAME;
	rmi4_data->input_dev->phys	= "Synaptics_Clearpad";
	rmi4_data->input_dev->id.bustype = BUS_I2C;
	rmi4_data->input_dev->dev.parent = &client->dev;
	input_set_drvdata(rmi4_data->input_dev, rmi4_data);

	/* Initialize the function handlers for rmi4 */
	set_bit(EV_SYN, rmi4_data->input_dev->evbit);
	set_bit(EV_KEY, rmi4_data->input_dev->evbit);
	set_bit(EV_ABS, rmi4_data->input_dev->evbit);

	input_set_abs_params(rmi4_data->input_dev, ABS_MT_POSITION_X, 0,
					rmi4_data->sensor_max_x, 0, 0);
	input_set_abs_params(rmi4_data->input_dev, ABS_MT_POSITION_Y, 0,
					rmi4_data->sensor_max_y, 0, 0);
	input_set_abs_params(rmi4_data->input_dev, ABS_MT_TOUCH_MAJOR, 0,
						MAX_TOUCH_MAJOR, 0, 0);
	input_mt_init_slots(rmi4_data->input_dev,
				rmi4_data->fingers_supported, 0);

	/* Clear interrupts */
	synaptics_rmi4_i2c_block_read(rmi4_data,
			rmi4_data->fn01_data_base_addr + 1, intr_status,
				rmi4_data->number_of_interrupt_register);
	retval = request_threaded_irq(platformdata->irq_number, NULL,
					synaptics_rmi4_irq,
					platformdata->irq_type,
					DRIVER_NAME, rmi4_data);
	if (retval) {
		dev_err(&client->dev, "%s:Unable to get attn irq %d\n",
				__func__, platformdata->irq_number);
		goto err_query_dev;
	}

	retval = input_register_device(rmi4_data->input_dev);
	if (retval) {
		dev_err(&client->dev, "%s:input register failed\n", __func__);
		goto err_free_irq;
	}

	return retval;

err_free_irq:
	free_irq(platformdata->irq_number, rmi4_data);
err_query_dev:
	regulator_disable(rmi4_data->regulator);
err_regulator_enable:
	regulator_put(rmi4_data->regulator);
err_get_regulator:
	input_free_device(rmi4_data->input_dev);
	rmi4_data->input_dev = NULL;
err_input:
	kfree(rmi4_data);

	return retval;
}
/**
 * synaptics_rmi4_remove() - Removes the i2c-client touchscreen driver
 * @client: i2c client structure pointer
 *
 * This function uses to remove the i2c-client
 * touchscreen driver and returns integer.
 */
static int synaptics_rmi4_remove(struct i2c_client *client)
{
	struct synaptics_rmi4_data *rmi4_data = i2c_get_clientdata(client);
	const struct synaptics_rmi4_platform_data *pdata = rmi4_data->board;

	rmi4_data->touch_stopped = true;
	wake_up(&rmi4_data->wait);
	free_irq(pdata->irq_number, rmi4_data);
	input_unregister_device(rmi4_data->input_dev);
	regulator_disable(rmi4_data->regulator);
	regulator_put(rmi4_data->regulator);
	kfree(rmi4_data);

	return 0;
}

#ifdef CONFIG_PM
/**
 * synaptics_rmi4_suspend() - suspend the touch screen controller
 * @dev: pointer to device structure
 *
 * This function is used to suspend the
 * touch panel controller and returns integer
 */
static int synaptics_rmi4_suspend(struct device *dev)
{
	/* Touch sleep mode */
	int retval;
	unsigned char intr_status;
	struct synaptics_rmi4_data *rmi4_data = dev_get_drvdata(dev);
	const struct synaptics_rmi4_platform_data *pdata = rmi4_data->board;

	rmi4_data->touch_stopped = true;
	disable_irq(pdata->irq_number);

	retval = synaptics_rmi4_i2c_block_read(rmi4_data,
				rmi4_data->fn01_data_base_addr + 1,
				&intr_status,
				rmi4_data->number_of_interrupt_register);
	if (retval < 0)
		return retval;

	retval = synaptics_rmi4_i2c_byte_write(rmi4_data,
					rmi4_data->fn01_ctrl_base_addr + 1,
					(intr_status & ~TOUCHPAD_CTRL_INTR));
	if (retval < 0)
		return retval;

	regulator_disable(rmi4_data->regulator);

	return 0;
}
/**
 * synaptics_rmi4_resume() - resume the touch screen controller
 * @dev: pointer to device structure
 *
 * This function is used to resume the touch panel
 * controller and returns integer.
 */
static int synaptics_rmi4_resume(struct device *dev)
{
	int retval;
	unsigned char intr_status;
	struct synaptics_rmi4_data *rmi4_data = dev_get_drvdata(dev);
	const struct synaptics_rmi4_platform_data *pdata = rmi4_data->board;

	regulator_enable(rmi4_data->regulator);

	enable_irq(pdata->irq_number);
	rmi4_data->touch_stopped = false;

	retval = synaptics_rmi4_i2c_block_read(rmi4_data,
				rmi4_data->fn01_data_base_addr + 1,
				&intr_status,
				rmi4_data->number_of_interrupt_register);
	if (retval < 0)
		return retval;

	retval = synaptics_rmi4_i2c_byte_write(rmi4_data,
					rmi4_data->fn01_ctrl_base_addr + 1,
					(intr_status | TOUCHPAD_CTRL_INTR));
	if (retval < 0)
		return retval;

	return 0;
}

static const struct dev_pm_ops synaptics_rmi4_dev_pm_ops = {
	.suspend = synaptics_rmi4_suspend,
	.resume  = synaptics_rmi4_resume,
};
#endif

static const struct i2c_device_id synaptics_rmi4_id_table[] = {
	{ DRIVER_NAME, 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, synaptics_rmi4_id_table);

static struct i2c_driver synaptics_rmi4_driver = {
	.driver = {
		.name	=	DRIVER_NAME,
		.owner	=	THIS_MODULE,
#ifdef CONFIG_PM
		.pm	=	&synaptics_rmi4_dev_pm_ops,
#endif
	},
	.probe		=	synaptics_rmi4_probe,
	.remove		=	synaptics_rmi4_remove,
	.id_table	=	synaptics_rmi4_id_table,
};

module_i2c_driver(synaptics_rmi4_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("naveen.gaddipati@stericsson.com, js.ha@stericsson.com");
MODULE_DESCRIPTION("synaptics rmi4 i2c touch Driver");
MODULE_ALIAS("i2c:synaptics_rmi4_ts");
