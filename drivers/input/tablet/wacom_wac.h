/*
 * drivers/input/tablet/wacom_wac.h
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef WACOM_WAC_H
#define WACOM_WAC_H

#include <linux/types.h>

/* maximum packet length for USB devices */
#define WACOM_PKGLEN_MAX	64

/* packet length for individual models */
#define WACOM_PKGLEN_PENPRTN	 7
#define WACOM_PKGLEN_GRAPHIRE	 8
#define WACOM_PKGLEN_BBFUN	 9
#define WACOM_PKGLEN_INTUOS	10
#define WACOM_PKGLEN_TPC1FG	 5
#define WACOM_PKGLEN_TPC2FG	14
#define WACOM_PKGLEN_BBTOUCH	20
#define WACOM_PKGLEN_BBTOUCH3	64
#define WACOM_PKGLEN_BBPEN	10
#define WACOM_PKGLEN_WIRELESS	32
#define WACOM_PKGLEN_MTOUCH	62
#define WACOM_PKGLEN_MTTPC	40

/* wacom data size per MT contact */
#define WACOM_BYTES_PER_MT_PACKET	11
#define WACOM_BYTES_PER_24HDT_PACKET	14

/* device IDs */
#define STYLUS_DEVICE_ID	0x02
#define TOUCH_DEVICE_ID		0x03
#define CURSOR_DEVICE_ID	0x06
#define ERASER_DEVICE_ID	0x0A
#define PAD_DEVICE_ID		0x0F

/* wacom data packet report IDs */
#define WACOM_REPORT_PENABLED		2
#define WACOM_REPORT_INTUOSREAD		5
#define WACOM_REPORT_INTUOSWRITE	6
#define WACOM_REPORT_INTUOSPAD		12
#define WACOM_REPORT_INTUOS5PAD		3
#define WACOM_REPORT_TPC1FG		6
#define WACOM_REPORT_TPC2FG		13
#define WACOM_REPORT_TPCMT		13
#define WACOM_REPORT_TPCHID		15
#define WACOM_REPORT_TPCST		16
#define WACOM_REPORT_TPC1FGE		18
#define WACOM_REPORT_24HDT		1

/* device quirks */
#define WACOM_QUIRK_MULTI_INPUT		0x0001
#define WACOM_QUIRK_BBTOUCH_LOWRES	0x0002
#define WACOM_QUIRK_NO_INPUT		0x0004
#define WACOM_QUIRK_MONITOR		0x0008

enum {
	PENPARTNER = 0,
	GRAPHIRE,
	WACOM_G4,
	PTU,
	PL,
	DTU,
	INTUOS,
	INTUOS3S,
	INTUOS3,
	INTUOS3L,
	INTUOS4S,
	INTUOS4,
	INTUOS4L,
	INTUOS5S,
	INTUOS5,
	INTUOS5L,
	WACOM_21UX2,
	WACOM_22HD,
	DTK,
	WACOM_24HD,
	CINTIQ,
	WACOM_BEE,
	WACOM_13HD,
	WACOM_MO,
	WIRELESS,
	BAMBOO_PT,
	WACOM_24HDT,
	TABLETPC,   /* add new TPC below */
	TABLETPCE,
	TABLETPC2FG,
	MTSCREEN,
	MTTPC,
	MAX_TYPE
};

struct wacom_features {
	const char *name;
	int pktlen;
	int x_max;
	int y_max;
	int pressure_max;
	int distance_max;
	int type;
	int x_resolution;
	int y_resolution;
	int device_type;
	int x_phy;
	int y_phy;
	unsigned char unit;
	unsigned char unitExpo;
	int x_fuzz;
	int y_fuzz;
	int pressure_fuzz;
	int distance_fuzz;
	unsigned quirks;
	unsigned touch_max;
	int oVid;
	int oPid;
};

struct wacom_shared {
	bool stylus_in_proximity;
	bool touch_down;
};

struct wacom_wac {
	char name[64];
	unsigned char *data;
	int tool[2];
	int id[2];
	__u32 serial[2];
	struct wacom_features features;
	struct wacom_shared *shared;
	struct input_dev *input;
	int pid;
	int battery_capacity;
	int num_contacts_left;
};

#endif
