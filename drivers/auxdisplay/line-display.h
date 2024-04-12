/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Character line display core support
 *
 * Copyright (C) 2016 Imagination Technologies
 * Author: Paul Burton <paul.burton@mips.com>
 *
 * Copyright (C) 2021 Glider bv
 */

#ifndef _LINEDISP_H
#define _LINEDISP_H

#include <linux/device.h>
#include <linux/timer_types.h>

#include <linux/map_to_7segment.h>
#include <linux/map_to_14segment.h>

struct linedisp;

/**
 * enum linedisp_map_type - type of the character mapping
 * @LINEDISP_MAP_SEG7: Map characters to 7 segment display
 * @LINEDISP_MAP_SEG14: Map characters to 14 segment display
 */
enum linedisp_map_type {
	LINEDISP_MAP_SEG7,
	LINEDISP_MAP_SEG14,
};

/**
 * struct linedisp_map - character mapping
 * @type: type of the character mapping
 * @map: conversion character mapping
 * @size: size of the @map
 */
struct linedisp_map {
	enum linedisp_map_type type;
	union {
		struct seg7_conversion_map seg7;
		struct seg14_conversion_map seg14;
	} map;
	unsigned int size;
};

/**
 * struct linedisp_ops - character line display operations
 * @get_map_type: Function called to get the character mapping, if required
 * @update: Function called to update the display. This must not sleep!
 */
struct linedisp_ops {
	int (*get_map_type)(struct linedisp *linedisp);
	void (*update)(struct linedisp *linedisp);
};

/**
 * struct linedisp - character line display private data structure
 * @dev: the line display device
 * @timer: timer used to implement scrolling
 * @ops: character line display operations
 * @buf: pointer to the buffer for the string currently displayed
 * @message: the full message to display or scroll on the display
 * @num_chars: the number of characters that can be displayed
 * @message_len: the length of the @message string
 * @scroll_pos: index of the first character of @message currently displayed
 * @scroll_rate: scroll interval in jiffies
 * @id: instance id of this display
 */
struct linedisp {
	struct device dev;
	struct timer_list timer;
	const struct linedisp_ops *ops;
	struct linedisp_map *map;
	char *buf;
	char *message;
	unsigned int num_chars;
	unsigned int message_len;
	unsigned int scroll_pos;
	unsigned int scroll_rate;
	unsigned int id;
};

int linedisp_register(struct linedisp *linedisp, struct device *parent,
		      unsigned int num_chars, const struct linedisp_ops *ops);
void linedisp_unregister(struct linedisp *linedisp);

#endif /* LINEDISP_H */
