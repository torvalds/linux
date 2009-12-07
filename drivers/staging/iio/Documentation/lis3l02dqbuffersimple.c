/* Industrialio test ring buffer with a lis3l02dq acceleromter
 *
 * Copyright (c) 2008 Jonathan Cameron
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * Assumes suitable udev rules are used to create the dev nodes as named here.
 */

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/dir.h>

#include <linux/types.h>
#include <dirent.h>
#include "iio_util.h"

static const char *ring_access = "/dev/iio/lis3l02dq_ring_access";
static const char *ring_event = "/dev/iio/lis3l02dq_ring_event";
static const char *device_name = "lis3l02dq";
static const char *trigger_name = "lis3l02dq-dev0";
static int NumVals = 3;
static int scan_ts = 1;
static int RingLength = 128;

/*
 * Could get this from ring bps, but only after starting the ring
 * which is a bit late for it to be useful
 */
int size_from_scanmode(int numVals, int timestamp)
{
	if (numVals && timestamp)
		return 16;
	else if (timestamp)
		return 8;
	else
		return numVals*2;
}

int main(int argc, char **argv)
{
	int i, j, k, toread;
	FILE *fp_ev;
	int fp;
	char *data;
	size_t read_size;
	struct iio_event_data dat;

	char	*BaseDirectoryName,
		*TriggerDirectoryName,
		*RingBufferDirectoryName;

	BaseDirectoryName = find_type_by_name(device_name, "device");
	if (BaseDirectoryName == NULL) {
		printf("Failed to find the %s \n", device_name);
		return -1;
	}
	TriggerDirectoryName = find_type_by_name(trigger_name, "trigger");
	if (TriggerDirectoryName == NULL) {
		printf("Failed to find the %s\n", trigger_name);
		return -1;
	}
	RingBufferDirectoryName = find_ring_subelement(BaseDirectoryName,
						       "ring_buffer");
	if (RingBufferDirectoryName == NULL) {
		printf("Failed to find ring buffer\n");
		return -1;
	}

	if (write_sysfs_string_and_verify("trigger/current_trigger",
					  BaseDirectoryName,
					  (char *)trigger_name) < 0) {
		printf("Failed to write current_trigger file \n");
		return -1;
	}

	/* Setup ring buffer parameters */
	if (write_sysfs_int("length", RingBufferDirectoryName,
			    RingLength) < 0) {
		printf("Failed to open the ring buffer length file \n");
		return -1;
	}

	/* Enable the ring buffer */
	if (write_sysfs_int("ring_enable", RingBufferDirectoryName, 1) < 0) {
		printf("Failed to open the ring buffer control file \n");
		return -1;
	};

	data = malloc(size_from_scanmode(NumVals, scan_ts)*RingLength);
	if (!data) {
		printf("Could not allocate space for usespace data store\n");
		return -1;
	}

	/* Attempt to open non blocking the access dev */
	fp = open(ring_access, O_RDONLY | O_NONBLOCK);
	if (fp == -1) { /*If it isn't there make the node */
		printf("Failed to open %s\n", ring_access);
		return -1;
	}
	/* Attempt to open the event access dev (blocking this time) */
	fp_ev = fopen(ring_event, "rb");
	if (fp_ev == NULL) {
		printf("Failed to open %s\n", ring_event);
		return -1;
	}

	/* Wait for events 10 times */
	for (j = 0; j < 10; j++) {
		read_size = fread(&dat, 1, sizeof(struct iio_event_data),
				  fp_ev);
		switch (dat.id) {
		case IIO_EVENT_CODE_RING_100_FULL:
			toread = RingLength;
			break;
		case IIO_EVENT_CODE_RING_75_FULL:
			toread = RingLength*3/4;
			break;
		case IIO_EVENT_CODE_RING_50_FULL:
			toread = RingLength/2;
			break;
		default:
			printf("Unexpecteded event code\n");
			continue;
		}
		read_size = read(fp,
				 data,
				 toread*size_from_scanmode(NumVals, scan_ts));
		if (read_size == -EAGAIN) {
			printf("nothing available \n");
			continue;
		}

		for (i = 0;
		     i < read_size/size_from_scanmode(NumVals, scan_ts);
		     i++) {
			for (k = 0; k < NumVals; k++) {
				__s16 val = *(__s16 *)(&data[i*size_from_scanmode(NumVals, scan_ts)
							     + (k)*2]);
				printf("%05d ", val);
			}
			printf(" %lld\n",
			       *(__s64 *)(&data[(i+1)*size_from_scanmode(NumVals, scan_ts)
						- sizeof(__s64)]));
		}
	}

	/* Stop the ring buffer */
	if (write_sysfs_int("ring_enable", RingBufferDirectoryName, 0) < 0) {
		printf("Failed to open the ring buffer control file \n");
		return -1;
	};

	/* Disconnect from the trigger - writing something that doesn't exist.*/
	write_sysfs_string_and_verify("trigger/current_trigger",
				      BaseDirectoryName, "NULL");
	free(BaseDirectoryName);
	free(TriggerDirectoryName);
	free(RingBufferDirectoryName);
	free(data);

	return 0;
}
