/* 
        dstr.c    (c) 1997-8  Grant R. Guenther <grant@torque.net>
                              Under the terms of the GNU General Public License.

        dstr.c is a low-level protocol driver for the 
        DataStor EP2000 parallel to IDE adapter chip.

*/

/* Changes:

        1.01    GRG 1998.05.06 init_proto, release_proto

*/

#define DSTR_VERSION      "1.01"

#include <linux/module.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <asm/io.h>

#include <linux/pata_parport.h>

/* mode codes:  0  nybble reads, 8-bit writes
                1  8-bit reads and writes
                2  8-bit EPP mode
		3  EPP-16
		4  EPP-32
*/

#define j44(a,b)  (((a>>3)&0x07)|((~a>>4)&0x08)|((b<<1)&0x70)|((~b)&0x80))

#define P1	w2(5);w2(0xd);w2(5);w2(4);
#define P2	w2(5);w2(7);w2(5);w2(4);
#define P3      w2(6);w2(4);w2(6);w2(4);

/* cont = 0 - access the IDE register file 
   cont = 1 - access the IDE command set 
*/

static int  cont_map[2] = { 0x20, 0x40 };

static int dstr_read_regr( PIA *pi, int cont, int regr )

{       int     a, b, r;

        r = regr + cont_map[cont];

	w0(0x81); P1;
	if (pi->mode) { w0(0x11); } else { w0(1); }
	P2; w0(r); P1;

        switch (pi->mode)  {

        case 0: w2(6); a = r1(); w2(4); w2(6); b = r1(); w2(4);
                return j44(a,b);

        case 1: w0(0); w2(0x26); a = r0(); w2(4);
                return a;

	case 2:
	case 3:
        case 4: w2(0x24); a = r4(); w2(4);
                return a;

        }
        return -1;
}       

static void dstr_write_regr(  PIA *pi, int cont, int regr, int val )

{       int  r;

        r = regr + cont_map[cont];

	w0(0x81); P1; 
	if (pi->mode >= 2) { w0(0x11); } else { w0(1); }
	P2; w0(r); P1;
	
        switch (pi->mode)  {

        case 0:
        case 1: w0(val); w2(5); w2(7); w2(5); w2(4);
		break;

	case 2:
	case 3:
        case 4: w4(val); 
                break;
        }
}

#define  CCP(x)  w0(0xff);w2(0xc);w2(4);\
		 w0(0xaa);w0(0x55);w0(0);w0(0xff);w0(0x87);w0(0x78);\
		 w0(x);w2(5);w2(4);

static void dstr_connect ( PIA *pi  )

{       pi->saved_r0 = r0();
        pi->saved_r2 = r2();
        w2(4); CCP(0xe0); w0(0xff);
}

static void dstr_disconnect ( PIA *pi )

{       CCP(0x30);
        w0(pi->saved_r0);
        w2(pi->saved_r2);
} 

static void dstr_read_block( PIA *pi, char * buf, int count )

{       int     k, a, b;

        w0(0x81); P1;
        if (pi->mode) { w0(0x19); } else { w0(9); }
	P2; w0(0x82); P1; P3; w0(0x20); P1;

        switch (pi->mode) {

        case 0: for (k=0;k<count;k++) {
                        w2(6); a = r1(); w2(4);
                        w2(6); b = r1(); w2(4);
                        buf[k] = j44(a,b);
                } 
                break;

        case 1: w0(0);
                for (k=0;k<count;k++) {
                        w2(0x26); buf[k] = r0(); w2(0x24);
                }
                w2(4);
                break;

        case 2: w2(0x24); 
                for (k=0;k<count;k++) buf[k] = r4();
                w2(4);
                break;

        case 3: w2(0x24); 
                for (k=0;k<count/2;k++) ((u16 *)buf)[k] = r4w();
                w2(4);
                break;

        case 4: w2(0x24); 
                for (k=0;k<count/4;k++) ((u32 *)buf)[k] = r4l();
                w2(4);
                break;

        }
}

static void dstr_write_block( PIA *pi, char * buf, int count )

{       int	k;

        w0(0x81); P1;
        if (pi->mode) { w0(0x19); } else { w0(9); }
        P2; w0(0x82); P1; P3; w0(0x20); P1;

        switch (pi->mode) {

        case 0:
        case 1: for (k=0;k<count;k++) {
                        w2(5); w0(buf[k]); w2(7);
                }
                w2(5); w2(4);
                break;

        case 2: w2(0xc5);
                for (k=0;k<count;k++) w4(buf[k]);
		w2(0xc4);
                break;

        case 3: w2(0xc5);
                for (k=0;k<count/2;k++) w4w(((u16 *)buf)[k]);
                w2(0xc4);
                break;

        case 4: w2(0xc5);
                for (k=0;k<count/4;k++) w4l(((u32 *)buf)[k]);
                w2(0xc4);
                break;

        }
}


static void dstr_log_adapter( PIA *pi, char * scratch, int verbose )

{       char    *mode_string[5] = {"4-bit","8-bit","EPP-8",
				   "EPP-16","EPP-32"};

        printk("%s: dstr %s, DataStor EP2000 at 0x%x, ",
                pi->device,DSTR_VERSION,pi->port);
        printk("mode %d (%s), delay %d\n",pi->mode,
		mode_string[pi->mode],pi->delay);

}

static struct pi_protocol dstr = {
	.owner		= THIS_MODULE,
	.name		= "dstr",
	.max_mode	= 5,
	.epp_first	= 2,
	.default_delay	= 1,
	.max_units	= 1,
	.write_regr	= dstr_write_regr,
	.read_regr	= dstr_read_regr,
	.write_block	= dstr_write_block,
	.read_block	= dstr_read_block,
	.connect	= dstr_connect,
	.disconnect	= dstr_disconnect,
	.log_adapter	= dstr_log_adapter,
};

static int __init dstr_init(void)
{
	return paride_register(&dstr);
}

static void __exit dstr_exit(void)
{
	paride_unregister(&dstr);
}

MODULE_LICENSE("GPL");
module_init(dstr_init)
module_exit(dstr_exit)
