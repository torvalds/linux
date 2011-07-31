/* Industrialio ring buffer with a lis3l02dq accelerometer
 *
 * Copyright (c) 2008 Jonathan Cameron
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is primarily intended as an example application.
 */

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/dir.h>
#include <linux/types.h>
#include "iio_utils.h"

const char *device_name = "lis3l02dq";
const char *trigger_name_base = "lis3l02dq-dev";
const int num_vals = 3;
const int scan_ts = 1;
const int buf_len = 128;
const int num_loops = 10;

/*
 * Could get this from ring bps, but only after starting the ring
 * which is a bit late for it to be useful.
 *
 * Todo: replace with much more generic version based on scan_elements
 * directory.
 */
int size_from_scanmode(int num_vals, int timestamp)
{
	if (num_vals && timestamp)
		return 16;
	else if (timestamp)
		return 8;
	else
		return num_vals*2;
}

int main(int argc, char **argv)
{
	int ret;
	int i, j, k, toread;
	FILE *fp_ev;
	int fp;

	char *trigger_name, *dev_dir_name, *buf_dir_name;
	char *data;
	size_t read_size;
	struct iio_event_data dat;
	int dev_num, trig_num;

	char *buffer_access, *buffer_event;
	const char *iio_dir = "/sys/bus/iio/devices/";
	int scan_size;
	float gain = 1;


	/* Find out which iio device is the accelerometer. */
	dev_num = find_type_by_name(device_name, "device");
	if (dev_num < 0) {
		printf("Failed to find the %s\n", device_name);
		ret = -ENODEV;
		goto error_ret;
	}
	printf("iio device number being used is %d\n", dev_num);
	asprintf(&dev_dir_name, "%sdevice%d", iio_dir, dev_num);

	/*
	 * Build the trigger name.
	 * In this case we want the lis3l02dq's data ready trigger
	 * for this lis3l02dq. The naming is lis3l02dq_dev[n], where
	 * n matches the device number found above.
	 */
	ret = asprintf(&trigger_name, "%s%d", trigger_name_base, dev_num);
	if (ret < 0) {
		ret = -ENOMEM;
		goto error_free_dev_dir_name;
	}

	/*
	 * Find the trigger by name.
	 * This is techically unecessary here as we only need to
	 * refer to the trigger by name and that name is already
	 * known.
	 */
	trig_num = find_type_by_name(trigger_name, "trigger");
	if (trig_num < 0) {
		printf("Failed to find the %s\n", trigger_name);
		ret = -ENODEV;
		goto error_free_triggername;
	}
	printf("iio trigger number being used is %d\n", trig_num);

	/*
	 * Read in the scale value - in a more generic case, first
	 * check for accel_scale, then the indivual channel scales
	 */
	ret = read_sysfs_float("accel_scale", dev_dir_name, &gain);
	if (ret)
		goto error_free_triggername;;

	/*
	 * Construct the directory name for the associated buffer.
	 * As we know that the lis3l02dq has only one buffer this may
	 * be built rather than found.
	 */
	ret = asprintf(&buf_dir_name, "%sdevice%d:buffer0", iio_dir, dev_num);
	if (ret < 0) {
		ret = -ENOMEM;
		goto error_free_triggername;
	}
	/* Set the device trigger to be the data rdy trigger found above */
	ret = write_sysfs_string_and_verify("trigger/current_trigger",
					dev_dir_name,
					trigger_name);
	if (ret < 0) {
		printf("Failed to write current_trigger file\n");
		goto error_free_buf_dir_name;
	}

	/* Setup ring buffer parameters */
	ret = write_sysfs_int("length", buf_dir_name, buf_len);
	if (ret < 0)
		goto error_free_buf_dir_name;

	/* Enable the buffer */
	ret = write_sysfs_int("ring_enable", buf_dir_name, 1);
	if (ret < 0)
		goto error_free_buf_dir_name;

	data = malloc(size_from_scanmode(num_vals, scan_ts)*buf_len);
	if (!data) {
		ret = -ENOMEM;
		goto error_free_buf_dir_name;
	}

	ret = asprintf(&buffer_access,
		       "/dev/device%d:buffer0:access0",
		       dev_num);
	if (ret < 0) {
		ret = -ENOMEM;
		goto error_free_data;
	}

	ret = asprintf(&buffer_event, "/dev/device%d:buffer0:event0", dev_num);
	if (ret < 0) {
		ret = -ENOMEM;
		goto error_free_data;
	}
	/* Attempt to open non blocking the access dev */
	fp = open(buffer_access, O_RDONLY | O_NONBLOCK);
	if (fp == -1) { /*If it isn't there make the node */
		printf("Failed to open %s\n", buffer_access);
		ret = -errno;
		goto error_free_buffer_event;
	}
	/* Attempt to open the event access dev (blocking this time) */
	fp_ev = fopen(buffer_event, "rb");
	if (fp_ev == NULL) {
		printf("Failed to open %s\n", buffer_event);
		ret = -errno;
		goto error_close_buffer_access;
	}

	/* Wait for events 10 times */
	for (j = 0; j < num_loops; j++) {
		read_size = fread(&dat, 1, sizeof(struct iio_event_data),
				  fp_ev);
		switch (dat.id) {
		case IIO_EVENT_CODE_RING_100_FULL:
			toread = buf_len;
			break;
		case IIO_EVENT_CODE_RING_75_FULL:
			toread = buf_len*3/4;
			break;
		case IIO_EVENT_CODE_RING_50_FULL:
			toread = buf_len/2;
			break;
		default:
			printf("Unexpecteded event code\n");
			continue;
		}
		read_size = read(fp,
				 data,
				 toread*size_from_scanmode(num_vals, scan_ts));
		if (read_size == -EAGAIN) {
			printf("nothing available\n");
			continue;
		}
		scan_size = size_from_scanmode(num_vals, scan_ts);
		for (i = 0; i < read_size/scan_size; i++) {
			for (k = 0; k < num_vals; k++) {
				__s16 val = *(__s16 *)(&data[i*scan_size
							     + (k)*2]);
				printf("%05f ", (float)val*gain);
			}
			printf(" %lld\n",
			       *(__s64 *)(&data[(i + 1)
						*size_from_scanmode(num_vals,
								    scan_ts)
						- sizeof(__s64)]));
		}
	}

	/* Stop the ring buffer */
	ret = write_sysfs_int("ring_enable", buf_dir_name, 0);
	if (ret < 0)
		goto error_close_buffer_event;

	/* Disconnect from the trigger - just write a dummy name.*/
	write_sysfs_string("trigger/current_trigger",
			dev_dir_name, "NULL");

error_close_buffer_event:
	fclose(fp_ev);
error_close_buffer_access:
	close(fp);
error_free_data:
	free(data);
error_free_buffer_access:
	free(buffer_access);
error_free_buffer_event:
	free(buffer_event);
error_free_buf_dir_name:
	free(buf_dir_name);
error_free_triggername:
	free(trigger_name);
error_free_dev_dir_name:
	free(dev_dir_name);
error_ret:
	return ret;
}
