/*
 *  Common functions for kernel modules using Dell SMBIOS
 *
 *  Copyright (c) Red Hat <mjg@redhat.com>
 *  Copyright (c) 2014 Gabriele Mazzotta <gabriele.mzt@gmail.com>
 *  Copyright (c) 2014 Pali Rohár <pali.rohar@gmail.com>
 *
 *  Based on documentation in the libsmbios package:
 *  Copyright (C) 2005-2014 Dell Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#ifndef _DELL_SMBIOS_H_
#define _DELL_SMBIOS_H_

/* This structure will be modified by the firmware when we enter
 * system management mode, hence the volatiles */

struct calling_interface_buffer {
	u16 class;
	u16 select;
	volatile u32 input[4];
	volatile u32 output[4];
} __packed;

struct calling_interface_token {
	u16 tokenID;
	u16 location;
	union {
		u16 value;
		u16 stringlength;
	};
};

int dell_smbios_error(int value);

struct calling_interface_buffer *dell_smbios_get_buffer(void);
void dell_smbios_clear_buffer(void);
void dell_smbios_release_buffer(void);
void dell_smbios_send_request(int class, int select);

struct calling_interface_token *dell_smbios_find_token(int tokenid);
#endif
