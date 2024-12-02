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

/**
 * struct linedisp - character line display private data structure
 * @dev: the line display device
 * @timer: timer used to implement scrolling
 * @update: function called to update the display
 * @buf: pointer to the buffer for the string currently displayed
 * @message: the full message to display or scroll on the display
 * @num_chars: the number of characters that can be displayed
 * @message_len: the length of the @message string
 * @scroll_pos: index of the first character of @message currently displayed
 * @scroll_rate: scroll interval in jiffies
 */
struct linedisp {
	struct device dev;
	struct timer_list timer;
	void (*update)(struct linedisp *linedisp);
	char *buf;
	char *message;
	unsigned int num_chars;
	unsigned int message_len;
	unsigned int scroll_pos;
	unsigned int scroll_rate;
};

int linedisp_register(struct linedisp *linedisp, struct device *parent,
		      unsigned int num_chars, char *buf,
		      void (*update)(struct linedisp *linedisp));
void linedisp_unregister(struct linedisp *linedisp);

#endif /* LINEDISP_H */
