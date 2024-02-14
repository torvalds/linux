/* 
	on20.c	(c) 1996-8  Grant R. Guenther <grant@torque.net>
		            Under the terms of the GNU General Public License.

        on20.c is a low-level protocol driver for the
        Onspec 90c20 parallel to IDE adapter. 
*/

/* Changes:

        1.01    GRG 1998.05.06 init_proto, release_proto

*/

#define	ON20_VERSION	"1.01"

#include <linux/module.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <asm/io.h>

#include "paride.h"

#define op(f)	w2(4);w0(f);w2(5);w2(0xd);w2(5);w2(0xd);w2(5);w2(4);
#define vl(v)	w2(4);w0(v);w2(5);w2(7);w2(5);w2(4);

#define j44(a,b)  (((a>>4)&0x0f)|(b&0xf0))

/* cont = 0 - access the IDE register file 
   cont = 1 - access the IDE command set 
*/

static int on20_read_regr( PIA *pi, int cont, int regr )

{	int h,l, r ;

        r = (regr<<2) + 1 + cont;

        op(1); vl(r); op(0);

	switch (pi->mode)  {

        case 0:  w2(4); w2(6); l = r1();
                 w2(4); w2(6); h = r1();
                 w2(4); w2(6); w2(4); w2(6); w2(4);
		 return j44(l,h);

	case 1:  w2(4); w2(0x26); r = r0(); 
                 w2(4); w2(0x26); w2(4);
		 return r;

	}
	return -1;
}	

static void on20_write_regr( PIA *pi, int cont, int regr, int val )

{	int r;

	r = (regr<<2) + 1 + cont;

	op(1); vl(r); 
	op(0); vl(val); 
	op(0); vl(val);
}

static void on20_connect ( PIA *pi)

{	pi->saved_r0 = r0();
        pi->saved_r2 = r2();

	w2(4);w0(0);w2(0xc);w2(4);w2(6);w2(4);w2(6);w2(4); 
	if (pi->mode) { op(2); vl(8); op(2); vl(9); }
	       else   { op(2); vl(0); op(2); vl(8); }
}

static void on20_disconnect ( PIA *pi )

{	w2(4);w0(7);w2(4);w2(0xc);w2(4);
        w0(pi->saved_r0);
        w2(pi->saved_r2);
} 

static void on20_read_block( PIA *pi, char * buf, int count )

{	int     k, l, h; 

	op(1); vl(1); op(0);

	for (k=0;k<count;k++) 
	    if (pi->mode) {
		w2(4); w2(0x26); buf[k] = r0();
	    } else {
		w2(6); l = r1(); w2(4);
		w2(6); h = r1(); w2(4);
		buf[k] = j44(l,h);
	    }
	w2(4);
}

static void on20_write_block(  PIA *pi, char * buf, int count )

{	int	k;

	op(1); vl(1); op(0);

	for (k=0;k<count;k++) { w2(5); w0(buf[k]); w2(7); }
	w2(4);
}

static void on20_log_adapter( PIA *pi, char * scratch, int verbose )

{       char    *mode_string[2] = {"4-bit","8-bit"};

        printk("%s: on20 %s, OnSpec 90c20 at 0x%x, ",
                pi->device,ON20_VERSION,pi->port);
        printk("mode %d (%s), delay %d\n",pi->mode,
		mode_string[pi->mode],pi->delay);

}

static struct pi_protocol on20 = {
	.owner		= THIS_MODULE,
	.name		= "on20",
	.max_mode	= 2,
	.epp_first	= 2,
	.default_delay	= 1,
	.max_units	= 1,
	.write_regr	= on20_write_regr,
	.read_regr	= on20_read_regr,
	.write_block	= on20_write_block,
	.read_block	= on20_read_block,
	.connect	= on20_connect,
	.disconnect	= on20_disconnect,
	.log_adapter	= on20_log_adapter,
};

static int __init on20_init(void)
{
	return paride_register(&on20);
}

static void __exit on20_exit(void)
{
	paride_unregister(&on20);
}

MODULE_LICENSE("GPL");
module_init(on20_init)
module_exit(on20_exit)
