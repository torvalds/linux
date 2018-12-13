// SPDX-License-Identifier: GPL-2.0
/*
 * u_midi.h
 *
 * Utility definitions for the midi function
 *
 * Copyright (c) 2014 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Author: Andrzej Pietrasiewicz <andrzejtp2010@gmail.com>
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

