/* 
	friq.c	(c) 1998    Grant R. Guenther <grant@torque.net>
		            Under the terms of the GNU General Public License

	friq.c is a low-level protocol driver for the Freecom "IQ"
	parallel port IDE adapter.   Early versions of this adapter
	use the 'frpw' protocol.
	
	Freecom uses this adapter in a battery powered external 
	CD-ROM drive.  It is also used in LS-120 drives by
	Maxell and Panasonic, and other devices.

	The battery powered drive requires software support to
	control the power to the drive.  This module enables the
	drive power when the high level driver (pcd) is loaded
	and disables it when the module is unloaded.  Note, if
	the friq module is built in to the kernel, the power
	will never be switched off, so other means should be
	used to conserve battery power.

*/

/* Changes:

	1.01	GRG 1998.12.20	 Added support for soft power switch
*/

#define	FRIQ_VERSION	"1.01" 

#include <linux/module.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <asm/io.h>

#include "paride.h"

#define CMD(x)		w2(4);w0(0xff);w0(0xff);w0(0x73);w0(0x73);\
			w0(0xc9);w0(0xc9);w0(0x26);w0(0x26);w0(x);w0(x);

#define j44(l,h)	(((l>>4)&0x0f)|(h&0xf0))

/* cont = 0 - access the IDE register file 
   cont = 1 - access the IDE command set 
*/

static int  cont_map[2] = { 0x08, 0x10 };

static int friq_read_regr( PIA *pi, int cont, int regr )

{	int	h,l,r;

	r = regr + cont_map[cont];

	CMD(r);
	w2(6); l = r1();
	w2(4); h = r1();
	w2(4); 

	return j44(l,h);

}

static void friq_write_regr( PIA *pi, int cont, int regr, int val)

{	int r;

        r = regr + cont_map[cont];

	CMD(r);
	w0(val);
	w2(5);w2(7);w2(5);w2(4);
}

static void friq_read_block_int( PIA *pi, char * buf, int count, int regr )

{       int     h, l, k, ph;

        switch(pi->mode) {

        case 0: CMD(regr); 
                for (k=0;k<count;k++) {
                        w2(6); l = r1();
                        w2(4); h = r1();
                        buf[k] = j44(l,h);
                }
                w2(4);
                break;

        case 1: ph = 2;
                CMD(regr+0xc0); 
                w0(0xff);
                for (k=0;k<count;k++) {
                        w2(0xa4 + ph); 
                        buf[k] = r0();
                        ph = 2 - ph;
                } 
                w2(0xac); w2(0xa4); w2(4);
                break;

	case 2: CMD(regr+0x80);
		for (k=0;k<count-2;k++) buf[k] = r4();
		w2(0xac); w2(0xa4);
		buf[count-2] = r4();
		buf[count-1] = r4();
		w2(4);
		break;

	case 3: CMD(regr+0x80);
                for (k=0;k<(count/2)-1;k++) ((u16 *)buf)[k] = r4w();
                w2(0xac); w2(0xa4);
                buf[count-2] = r4();
                buf[count-1] = r4();
                w2(4);
                break;

	case 4: CMD(regr+0x80);
                for (k=0;k<(count/4)-1;k++) ((u32 *)buf)[k] = r4l();
                buf[count-4] = r4();
                buf[count-3] = r4();
                w2(0xac); w2(0xa4);
                buf[count-2] = r4();
                buf[count-1] = r4();
                w2(4);
                break;

        }
}

static void friq_read_block( PIA *pi, char * buf, int count)

{	friq_read_block_int(pi,buf,count,0x08);
}

static void friq_write_block( PIA *pi, char * buf, int count )
 
{	int	k;

	switch(pi->mode) {

	case 0:
	case 1: CMD(8); w2(5);
        	for (k=0;k<count;k++) {
			w0(buf[k]);
			w2(7);w2(5);
		}
		w2(4);
		break;

	case 2: CMD(0xc8); w2(5);
		for (k=0;k<count;k++) w4(buf[k]);
		w2(4);
		break;

        case 3: CMD(0xc8); w2(5);
                for (k=0;k<count/2;k++) w4w(((u16 *)buf)[k]);
                w2(4);
                break;

        case 4: CMD(0xc8); w2(5);
                for (k=0;k<count/4;k++) w4l(((u32 *)buf)[k]);
                w2(4);
                break;
	}
}

static void friq_connect ( PIA *pi  )

{       pi->saved_r0 = r0();
        pi->saved_r2 = r2();
	w2(4);
}

static void friq_disconnect ( PIA *pi )

{       CMD(0x20);
	w0(pi->saved_r0);
        w2(pi->saved_r2);
} 

static int friq_test_proto( PIA *pi, char * scratch, int verbose )

{       int     j, k, r;
	int	e[2] = {0,0};

	pi->saved_r0 = r0();	
	w0(0xff); udelay(20); CMD(0x3d); /* turn the power on */
	udelay(500);
	w0(pi->saved_r0);

	friq_connect(pi);
	for (j=0;j<2;j++) {
                friq_write_regr(pi,0,6,0xa0+j*0x10);
                for (k=0;k<256;k++) {
                        friq_write_regr(pi,0,2,k^0xaa);
                        friq_write_regr(pi,0,3,k^0x55);
                        if (friq_read_regr(pi,0,2) != (k^0xaa)) e[j]++;
                        }
                }
	friq_disconnect(pi);

	friq_connect(pi);
        friq_read_block_int(pi,scratch,512,0x10);
        r = 0;
        for (k=0;k<128;k++) if (scratch[k] != k) r++;
	friq_disconnect(pi);

        if (verbose)  {
            printk("%s: friq: port 0x%x, mode %d, test=(%d,%d,%d)\n",
                   pi->device,pi->port,pi->mode,e[0],e[1],r);
        }

        return (r || (e[0] && e[1]));
}


static void friq_log_adapter( PIA *pi, char * scratch, int verbose )

{       char    *mode_string[6] = {"4-bit","8-bit",
				   "EPP-8","EPP-16","EPP-32"};

        printk("%s: friq %s, Freecom IQ ASIC-2 adapter at 0x%x, ", pi->device,
		FRIQ_VERSION,pi->port);
        printk("mode %d (%s), delay %d\n",pi->mode,
		mode_string[pi->mode],pi->delay);

	pi->private = 1;
	friq_connect(pi);
	CMD(0x9e);  		/* disable sleep timer */
	friq_disconnect(pi);

}

static void friq_release_proto( PIA *pi)
{
	if (pi->private) {		/* turn off the power */
		friq_connect(pi);
		CMD(0x1d); CMD(0x1e);
		friq_disconnect(pi);
		pi->private = 0;
	}
}

static struct pi_protocol friq = {
	.owner		= THIS_MODULE,
	.name		= "friq",
	.max_mode	= 5,
	.epp_first	= 2,
	.default_delay	= 1,
	.max_units	= 1,
	.write_regr	= friq_write_regr,
	.read_regr	= friq_read_regr,
	.write_block	= friq_write_block,
	.read_block	= friq_read_block,
	.connect	= friq_connect,
	.disconnect	= friq_disconnect,
	.test_proto	= friq_test_proto,
	.log_adapter	= friq_log_adapter,
	.release_proto	= friq_release_proto,
};

static int __init friq_init(void)
{
	return paride_register(&friq)-1;
}

static void __exit friq_exit(void)
{
	paride_unregister(&friq);
}

MODULE_LICENSE("GPL");
module_init(friq_init)
module_exit(friq_exit)
