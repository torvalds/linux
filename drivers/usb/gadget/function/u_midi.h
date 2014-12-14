/*
 * u_midi.h
 *
 * Utility definitions for the midi function
 *
 * Copyright (c) 2014 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Author: Andrzej Pietrasiewicz <andrzej.p@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef U_MIDI_H
#define U_MIDI_H

#include <linux/usb/composite.h>

struct f_midi_opts {
	struct usb_function_instance	func_inst;
	int				index;
	char				*id;
	bool				id_allocated;
	unsigned int			in_ports;
	unsigned int			out_ports;
	unsigned int			buflen;
	unsigned int			qlen;

	/*
	 * Protect the data form concurrent access by read/write
	 * and create symlink/remove symlink.
	 */
	 struct mutex			lock;
	 int				refcnt;
};

#endif /* U_MIDI_H */

