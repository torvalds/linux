// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * (c) 1997-1998  Grant R. Guenther <grant@torque.net>
 *
 * This is a low-level driver for the KBIC-951A and KBIC-971A
 * parallel to IDE adapter chips from KingByte Information Systems.
 *
 * The chips are almost identical, however, the wakeup code
 * required for the 971A interferes with the correct operation of
 * the 951A, so this driver registers itself twice, once for
 * each chip.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <asm/io.h>
#include "pata_parport.h"

#define r12w()		(delay_p, inw(pi->port + 1) & 0xffff)

#define j44(a, b)	((((a >> 4) & 0x0f) | (b & 0xf0)) ^ 0x88)
#define j53(w)		(((w >> 3) & 0x1f) | ((w >> 4) & 0xe0))


/*
 * cont = 0 - access the IDE register file
 * cont = 1 - access the IDE command set
 */
static int cont_map[2] = { 0x80, 0x40 };

static int kbic_read_regr(struct pi_adapter *pi, int cont, int regr)
{
	int a, b, s;

	s = cont_map[cont];

	switch (pi->mode) {
	case 0:
		w0(regr | 0x18 | s); w2(4); w2(6); w2(4); w2(1); w0(8);
		a = r1(); w0(0x28); b = r1(); w2(4);
		return j44(a, b);
	case 1:
		w0(regr|0x38 | s); w2(4); w2(6); w2(4); w2(5); w0(8);
		a = r12w(); w2(4);
		return j53(a);
	case 2:
		w0(regr | 0x08 | s); w2(4); w2(6); w2(4); w2(0xa5); w2(0xa1);
		a = r0(); w2(4);
		return a;
	case 3:
	case 4:
	case 5:
		w0(0x20 | s); w2(4); w2(6); w2(4); w3(regr);
		a = r4(); b = r4(); w2(4); w2(0); w2(4);
		return a;
	}

	return -1;
}

static void kbic_write_regr(struct pi_adapter *pi, int cont, int regr, int val)
{
	int s = cont_map[cont];

	switch (pi->mode) {
	case 0:
	case 1:
	case 2:
		w0(regr | 0x10 | s); w2(4); w2(6); w2(4);
		w0(val); w2(5); w2(4);
		break;
	case 3:
	case 4:
	case 5:
		w0(0x20 | s); w2(4); w2(6); w2(4); w3(regr);
		w4(val); w4(val);
		w2(4); w2(0); w2(4);
		break;
	}
}

static void k951_connect(struct pi_adapter *pi)
{
	pi->saved_r0 = r0();
	pi->saved_r2 = r2();
	w2(4);
}

static void k951_disconnect(struct pi_adapter *pi)
{
	w0(pi->saved_r0);
	w2(pi->saved_r2);
}

#define	CCP(x)						\
	do {						\
		w2(0xc4); w0(0xaa); w0(0x55);		\
		w0(0); w0(0xff); w0(0x87);		\
		w0(0x78); w0(x); w2(0xc5);		\
		w2(0xc4); w0(0xff);			\
	} while (0)

static void k971_connect(struct pi_adapter *pi)
{
	pi->saved_r0 = r0();
	pi->saved_r2 = r2();
	CCP(0x20);
	w2(4);
}

static void k971_disconnect(struct pi_adapter *pi)
{
	CCP(0x30);
	w0(pi->saved_r0);
	w2(pi->saved_r2);
}

/*
 * count must be congruent to 0 MOD 4, but all known applications
 *have this property.
 */
static void kbic_read_block(struct pi_adapter *pi, char *buf, int count)
{
	int k, a, b;

	switch (pi->mode) {
	case 0:
		w0(0x98); w2(4); w2(6); w2(4);
		for (k = 0; k < count / 2; k++) {
			w2(1); w0(8);
			a = r1();
			w0(0x28);
			b = r1();
			buf[2 * k] = j44(a, b);
			w2(5);
			b = r1();
			w0(8);
			a = r1();
			buf[2 * k + 1] = j44(a, b);
			w2(4);
		}
		break;
	case 1:
		w0(0xb8); w2(4); w2(6); w2(4);
		for (k = 0; k < count / 4; k++) {
			w0(0xb8);
			w2(4); w2(5);
			w0(8);
			buf[4 * k] = j53(r12w());
			w0(0xb8);
			buf[4 * k + 1] = j53(r12w());
			w2(4); w2(5);
			buf[4 * k + 3] = j53(r12w());
			w0(8);
			buf[4 * k + 2] = j53(r12w());
		}
		w2(4);
		break;
	case 2:
		w0(0x88); w2(4); w2(6); w2(4);
		for (k = 0; k < count / 2; k++) {
			w2(0xa0); w2(0xa1);
			buf[2 * k] = r0();
			w2(0xa5);
			buf[2 * k + 1] = r0();
		}
		w2(4);
		break;
	case 3:
		w0(0xa0); w2(4); w2(6); w2(4); w3(0);
		for (k = 0; k < count; k++)
			buf[k] = r4();
		w2(4); w2(0); w2(4);
		break;
	case 4:
		w0(0xa0); w2(4); w2(6); w2(4); w3(0);
		for (k = 0; k < count / 2; k++)
			((u16 *)buf)[k] = r4w();
		w2(4); w2(0); w2(4);
		break;
	case 5:
		w0(0xa0); w2(4); w2(6); w2(4); w3(0);
		for (k = 0; k < count / 4; k++)
			((u32 *)buf)[k] = r4l();
		w2(4); w2(0); w2(4);
		break;
	}
}

static void kbic_write_block(struct pi_adapter *pi, char *buf, int count)
{
	int k;

	switch (pi->mode) {
	case 0:
	case 1:
	case 2:
		w0(0x90); w2(4); w2(6); w2(4);
		for (k = 0; k < count / 2; k++) {
			w0(buf[2 * k + 1]);
			w2(0); w2(4);
			w0(buf[2 * k]);
			w2(5); w2(4);
		}
		break;
	case 3:
		w0(0xa0); w2(4); w2(6); w2(4); w3(0);
		for (k = 0; k < count / 2; k++) {
			w4(buf[2 * k + 1]);
			w4(buf[2 * k]);
		}
		w2(4); w2(0); w2(4);
		break;
	case 4:
		w0(0xa0); w2(4); w2(6); w2(4); w3(0);
		for (k = 0; k < count / 2; k++)
			w4w(swab16(((u16 *)buf)[k]));
		w2(4); w2(0); w2(4);
		break;
	case 5:
		w0(0xa0); w2(4); w2(6); w2(4); w3(0);
		for (k = 0; k < count / 4; k++)
			w4l(swab16(((u16 *)buf)[2 * k]) |
			    swab16(((u16 *)buf)[2 * k + 1]) << 16);
		w2(4); w2(0); w2(4);
		break;
	}
}

static void kbic_log_adapter(struct pi_adapter *pi, char *chip)
{
	char *mode[6] = { "4-bit", "5/3", "8-bit", "EPP-8", "EPP_16", "EPP-32"};

	dev_info(&pi->dev, "KingByte %s at 0x%x, mode %d (%s), delay %d\n",
		 chip, pi->port, pi->mode, mode[pi->mode], pi->delay);
}

static void k951_log_adapter(struct pi_adapter *pi)
{
	kbic_log_adapter(pi, "KBIC-951A");
}

static void k971_log_adapter(struct pi_adapter *pi)
{
	kbic_log_adapter(pi, "KBIC-971A");
}

static struct pi_protocol k951 = {
	.owner		= THIS_MODULE,
	.name		= "k951",
	.max_mode	= 6,
	.epp_first	= 3,
	.default_delay	= 1,
	.max_units	= 1,
	.write_regr	= kbic_write_regr,
	.read_regr	= kbic_read_regr,
	.write_block	= kbic_write_block,
	.read_block	= kbic_read_block,
	.connect	= k951_connect,
	.disconnect	= k951_disconnect,
	.log_adapter	= k951_log_adapter,
};

static struct pi_protocol k971 = {
	.owner		= THIS_MODULE,
	.name		= "k971",
	.max_mode	= 6,
	.epp_first	= 3,
	.default_delay	= 1,
	.max_units	= 1,
	.write_regr	= kbic_write_regr,
	.read_regr	= kbic_read_regr,
	.write_block	= kbic_write_block,
	.read_block	= kbic_read_block,
	.connect	= k971_connect,
	.disconnect	= k971_disconnect,
	.log_adapter	= k971_log_adapter,
};

static int __init kbic_init(void)
{
	int rv;

	rv = pata_parport_register_driver(&k951);
	if (rv < 0)
		return rv;
	rv = pata_parport_register_driver(&k971);
	if (rv < 0)
		pata_parport_unregister_driver(&k951);
	return rv;
}

static void __exit kbic_exit(void)
{
	pata_parport_unregister_driver(&k951);
	pata_parport_unregister_driver(&k971);
}

MODULE_LICENSE("GPL");
module_init(kbic_init)
module_exit(kbic_exit)
