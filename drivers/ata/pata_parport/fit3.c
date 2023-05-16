// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * (c) 1998  Grant R. Guenther <grant@torque.net>
 *
 * fit3.c is a low-level protocol driver for newer models
 * of the Fidelity International Technology parallel port adapter.
 * This adapter is used in their TransDisk 3000 portable
 * hard-drives, as well as CD-ROM, PD-CD and other devices.
 *
 * The TD-2000 and certain older devices use a different protocol.
 * Try the fit2 protocol module with them.
 *
 * NB:  The FIT adapters do not appear to support the control
 * registers.  So, we map ALT_STATUS to STATUS and NO-OP writes
 * to the device control register - this means that IDE reset
 * will not work on these devices.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <asm/io.h>
#include "pata_parport.h"

#define j44(a, b)	(((a >> 3) & 0x0f) | ((b << 1) & 0xf0))

#define w7(byte)	out_p(7, byte)
#define r7()		(in_p(7) & 0xff)

/*
 * cont = 0 - access the IDE register file
 * cont = 1 - access the IDE command set
 */

static void fit3_write_regr(struct pi_adapter *pi, int cont, int regr, int val)
{
	if (cont == 1)
		return;

	switch (pi->mode) {
	case 0:
	case 1:
		w2(0xc); w0(regr); w2(0x8); w2(0xc);
		w0(val); w2(0xd);
		w0(0);   w2(0xc);
		break;
	case 2:
		w2(0xc); w0(regr); w2(0x8); w2(0xc);
		w4(val); w4(0);
		w2(0xc);
		break;
	}
}

static int fit3_read_regr(struct pi_adapter *pi, int cont, int regr)
{
	int  a, b;

	if (cont) {
		if (regr != 6)
			return 0xff;
		regr = 7;
	}

	switch (pi->mode) {
	case 0:
		w2(0xc); w0(regr + 0x10); w2(0x8); w2(0xc);
		w2(0xd); a = r1();
		w2(0xf); b = r1();
		w2(0xc);
		return j44(a, b);
	case 1:
		w2(0xc); w0(regr + 0x90); w2(0x8); w2(0xc);
		w2(0xec); w2(0xee); w2(0xef); a = r0();
		w2(0xc);
		return a;
	case 2:
		w2(0xc); w0(regr + 0x90); w2(0x8); w2(0xc);
		w2(0xec);
		a = r4(); b = r4();
		w2(0xc);
		return a;
	}

	return -1;
}

static void fit3_read_block(struct pi_adapter *pi, char *buf, int count)
{
	int  k, a, b, c, d;

	switch (pi->mode) {
	case 0:
		w2(0xc); w0(0x10); w2(0x8); w2(0xc);
		for (k = 0; k < count / 2; k++) {
			w2(0xd); a = r1();
			w2(0xf); b = r1();
			w2(0xc); c = r1();
			w2(0xe); d = r1();
			buf[2 * k] = j44(a, b);
			buf[2 * k + 1] = j44(c, d);
		}
		w2(0xc);
		break;
	case 1:
		w2(0xc); w0(0x90); w2(0x8); w2(0xc);
		w2(0xec); w2(0xee);
		for (k = 0; k < count / 2; k++) {
			w2(0xef); a = r0();
			w2(0xee); b = r0();
			buf[2 * k] = a;
			buf[2 * k + 1] = b;
		}
		w2(0xec);
		w2(0xc);
		break;
	case 2:
		w2(0xc); w0(0x90); w2(0x8); w2(0xc);
		w2(0xec);
		for (k = 0; k < count; k++)
			buf[k] = r4();
		w2(0xc);
		break;
	}
}

static void fit3_write_block(struct pi_adapter *pi, char *buf, int count)
{
	int k;

	switch (pi->mode) {
	case 0:
	case 1:
		w2(0xc); w0(0); w2(0x8); w2(0xc);
		for (k = 0; k < count / 2; k++) {
			w0(buf[2 * k]); w2(0xd);
			w0(buf[2 * k + 1]); w2(0xc);
		}
		break;
	case 2:
		w2(0xc); w0(0); w2(0x8); w2(0xc);
		for (k = 0; k < count; k++)
			w4(buf[k]);
		w2(0xc);
		break;
	}
}

static void fit3_connect(struct pi_adapter *pi)
{
	pi->saved_r0 = r0();
	pi->saved_r2 = r2();
	w2(0xc); w0(0); w2(0xa);
	if (pi->mode == 2) {
		w2(0xc); w0(0x9);
		w2(0x8); w2(0xc);
	}
}

static void fit3_disconnect(struct pi_adapter *pi)
{
	w2(0xc); w0(0xa); w2(0x8); w2(0xc);
	w0(pi->saved_r0);
	w2(pi->saved_r2);
}

static void fit3_log_adapter(struct pi_adapter *pi)
{
	char *mode_string[3] = { "4-bit", "8-bit", "EPP"};

	dev_info(&pi->dev,
		 "FIT 3000 adapter at 0x%x, mode %d (%s), delay %d\n",
		 pi->port, pi->mode, mode_string[pi->mode], pi->delay);
}

static struct pi_protocol fit3 = {
	.owner		= THIS_MODULE,
	.name		= "fit3",
	.max_mode	= 3,
	.epp_first	= 2,
	.default_delay	= 1,
	.max_units	= 1,
	.write_regr	= fit3_write_regr,
	.read_regr	= fit3_read_regr,
	.write_block	= fit3_write_block,
	.read_block	= fit3_read_block,
	.connect	= fit3_connect,
	.disconnect	= fit3_disconnect,
	.log_adapter	= fit3_log_adapter,
};

MODULE_LICENSE("GPL");
module_pata_parport_driver(fit3);
