// SPDX-License-Identifier: GPL-2.0+
/*  ----------------------------------------------------------------------
    Copyright (C) 2000  Cesar Miquel  (miquel@df.uba.ar)
    ---------------------------------------------------------------------- */

#define RIO_SEND_COMMAND			0x1
#define RIO_RECV_COMMAND			0x2

#define RIO_DIR_OUT               	        0x0
#define RIO_DIR_IN				0x1

struct RioCommand {
	short length;
	int request;
	int requesttype;
	int value;
	int index;
	void __user *buffer;
	int timeout;
};
