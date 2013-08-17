/* 
        ktti.c        (c) 1998  Grant R. Guenther <grant@torque.net>
                          Under the terms of the GNU General Public License.

	ktti.c is a low-level protocol driver for the KT Technology
	parallel port adapter.  This adapter is used in the "PHd" 
        portable hard-drives.  As far as I can tell, this device
	supports 4-bit mode _only_.  

*/

#define KTTI_VERSION      "1.0"

#include <linux/module.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <asm/io.h>

#include "paride.h"

#define j44(a,b)                (((a>>4)&0x0f)|(b&0xf0))

/* cont = 0 - access the IDE register file 
   cont = 1 - access the IDE command set 
*/

static int  cont_map[2] = { 0x10, 0x08 };

static void  ktti_write_regr( PIA *pi, int cont, int regr, int val)

{	int r;

	r = regr + cont_map[cont];

	w0(r); w2(0xb); w2(0xa); w2(3); w2(6); 
	w0(val); w2(3); w0(0); w2(6); w2(0xb);
}

static int ktti_read_regr( PIA *pi, int cont, int regr )

{	int  a, b, r;

        r = regr + cont_map[cont];

        w0(r); w2(0xb); w2(0xa); w2(9); w2(0xc); w2(9); 
	a = r1(); w2(0xc);  b = r1(); w2(9); w2(0xc); w2(9);
	return j44(a,b);

}

static void ktti_read_block( PIA *pi, char * buf, int count )

{	int  k, a, b;

	for (k=0;k<count/2;k++) {
		w0(0x10); w2(0xb); w2(0xa); w2(9); w2(0xc); w2(9);
		a = r1(); w2(0xc); b = r1(); w2(9);
		buf[2*k] = j44(a,b);
		a = r1(); w2(0xc); b = r1(); w2(9);
		buf[2*k+1] = j44(a,b);
	}
}

static void ktti_write_block( PIA *pi, char * buf, int count )

{	int k;

	for (k=0;k<count/2;k++) {
		w0(0x10); w2(0xb); w2(0xa); w2(3); w2(6);
		w0(buf[2*k]); w2(3);
		w0(buf[2*k+1]); w2(6);
		w2(0xb);
	}
}

static void ktti_connect ( PIA *pi  )

{       pi->saved_r0 = r0();
        pi->saved_r2 = r2();
	w2(0xb); w2(0xa); w0(0); w2(3); w2(6);	
}

static void ktti_disconnect ( PIA *pi )

{       w2(0xb); w2(0xa); w0(0xa0); w2(3); w2(4);
	w0(pi->saved_r0);
        w2(pi->saved_r2);
} 

static void ktti_log_adapter( PIA *pi, char * scratch, int verbose )

{       printk("%s: ktti %s, KT adapter at 0x%x, delay %d\n",
                pi->device,KTTI_VERSION,pi->port,pi->delay);

}

static struct pi_protocol ktti = {
	.owner		= THIS_MODULE,
	.name		= "ktti",
	.max_mode	= 1,
	.epp_first	= 2,
	.default_delay	= 1,
	.max_units	= 1,
	.write_regr	= ktti_write_regr,
	.read_regr	= ktti_read_regr,
	.write_block	= ktti_write_block,
	.read_block	= ktti_read_block,
	.connect	= ktti_connect,
	.disconnect	= ktti_disconnect,
	.log_adapter	= ktti_log_adapter,
};

static int __init ktti_init(void)
{
	return paride_register(&ktti);
}

static void __exit ktti_exit(void)
{
	paride_unregister(&ktti);
}

MODULE_LICENSE("GPL");
module_init(ktti_init)
module_exit(ktti_exit)
