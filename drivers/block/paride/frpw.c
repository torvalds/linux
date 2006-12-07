/* 
	frpw.c	(c) 1996-8  Grant R. Guenther <grant@torque.net>
		            Under the terms of the GNU General Public License

	frpw.c is a low-level protocol driver for the Freecom "Power"
	parallel port IDE adapter.
	
	Some applications of this adapter may require a "printer" reset
	prior to loading the driver.  This can be done by loading and
	unloading the "lp" driver, or it can be done by this driver
	if you define FRPW_HARD_RESET.  The latter is not recommended
	as it may upset devices on other ports.

*/

/* Changes:

        1.01    GRG 1998.05.06 init_proto, release_proto
			       fix chip detect
			       added EPP-16 and EPP-32
	1.02    GRG 1998.09.23 added hard reset to initialisation process
	1.03    GRG 1998.12.14 made hard reset conditional

*/

#define	FRPW_VERSION	"1.03" 

#include <linux/module.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <asm/io.h>

#include "paride.h"

#define cec4		w2(0xc);w2(0xe);w2(0xe);w2(0xc);w2(4);w2(4);w2(4);
#define j44(l,h)	(((l>>4)&0x0f)|(h&0xf0))

/* cont = 0 - access the IDE register file 
   cont = 1 - access the IDE command set 
*/

static int  cont_map[2] = { 0x08, 0x10 };

static int frpw_read_regr( PIA *pi, int cont, int regr )

{	int	h,l,r;

	r = regr + cont_map[cont];

	w2(4);
	w0(r); cec4;
	w2(6); l = r1();
	w2(4); h = r1();
	w2(4); 

	return j44(l,h);

}

static void frpw_write_regr( PIA *pi, int cont, int regr, int val)

{	int r;

        r = regr + cont_map[cont];

	w2(4); w0(r); cec4; 
	w0(val);
	w2(5);w2(7);w2(5);w2(4);
}

static void frpw_read_block_int( PIA *pi, char * buf, int count, int regr )

{       int     h, l, k, ph;

        switch(pi->mode) {

        case 0: w2(4); w0(regr); cec4;
                for (k=0;k<count;k++) {
                        w2(6); l = r1();
                        w2(4); h = r1();
                        buf[k] = j44(l,h);
                }
                w2(4);
                break;

        case 1: ph = 2;
                w2(4); w0(regr + 0xc0); cec4;
                w0(0xff);
                for (k=0;k<count;k++) {
                        w2(0xa4 + ph); 
                        buf[k] = r0();
                        ph = 2 - ph;
                } 
                w2(0xac); w2(0xa4); w2(4);
                break;

        case 2: w2(4); w0(regr + 0x80); cec4;
                for (k=0;k<count;k++) buf[k] = r4();
                w2(0xac); w2(0xa4);
                w2(4);
                break;

	case 3: w2(4); w0(regr + 0x80); cec4;
		for (k=0;k<count-2;k++) buf[k] = r4();
		w2(0xac); w2(0xa4);
		buf[count-2] = r4();
		buf[count-1] = r4();
		w2(4);
		break;

	case 4: w2(4); w0(regr + 0x80); cec4;
                for (k=0;k<(count/2)-1;k++) ((u16 *)buf)[k] = r4w();
                w2(0xac); w2(0xa4);
                buf[count-2] = r4();
                buf[count-1] = r4();
                w2(4);
                break;

	case 5: w2(4); w0(regr + 0x80); cec4;
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

static void frpw_read_block( PIA *pi, char * buf, int count)

{	frpw_read_block_int(pi,buf,count,0x08);
}

static void frpw_write_block( PIA *pi, char * buf, int count )
 
{	int	k;

	switch(pi->mode) {

	case 0:
	case 1:
	case 2: w2(4); w0(8); cec4; w2(5);
        	for (k=0;k<count;k++) {
			w0(buf[k]);
			w2(7);w2(5);
		}
		w2(4);
		break;

	case 3: w2(4); w0(0xc8); cec4; w2(5);
		for (k=0;k<count;k++) w4(buf[k]);
		w2(4);
		break;

        case 4: w2(4); w0(0xc8); cec4; w2(5);
                for (k=0;k<count/2;k++) w4w(((u16 *)buf)[k]);
                w2(4);
                break;

        case 5: w2(4); w0(0xc8); cec4; w2(5);
                for (k=0;k<count/4;k++) w4l(((u32 *)buf)[k]);
                w2(4);
                break;
	}
}

static void frpw_connect ( PIA *pi  )

{       pi->saved_r0 = r0();
        pi->saved_r2 = r2();
	w2(4);
}

static void frpw_disconnect ( PIA *pi )

{       w2(4); w0(0x20); cec4;
	w0(pi->saved_r0);
        w2(pi->saved_r2);
} 

/* Stub logic to see if PNP string is available - used to distinguish
   between the Xilinx and ASIC implementations of the Freecom adapter.
*/

static int frpw_test_pnp ( PIA *pi )

/*  returns chip_type:   0 = Xilinx, 1 = ASIC   */

{	int olddelay, a, b;

#ifdef FRPW_HARD_RESET
        w0(0); w2(8); udelay(50); w2(0xc);   /* parallel bus reset */
        mdelay(1500);
#endif

	olddelay = pi->delay;
	pi->delay = 10;

	pi->saved_r0 = r0();
        pi->saved_r2 = r2();
	
	w2(4); w0(4); w2(6); w2(7);
	a = r1() & 0xff; w2(4); b = r1() & 0xff;
	w2(0xc); w2(0xe); w2(4);

	pi->delay = olddelay;
        w0(pi->saved_r0);
        w2(pi->saved_r2);

	return ((~a&0x40) && (b&0x40));
} 

/* We use the pi->private to remember the result of the PNP test.
   To make this work, private = port*2 + chip.  Yes, I know it's
   a hack :-(
*/

static int frpw_test_proto( PIA *pi, char * scratch, int verbose )

{       int     j, k, r;
	int	e[2] = {0,0};

	if ((pi->private>>1) != pi->port)
	   pi->private = frpw_test_pnp(pi) + 2*pi->port;

	if (((pi->private%2) == 0) && (pi->mode > 2)) {
	   if (verbose) 
		printk("%s: frpw: Xilinx does not support mode %d\n",
			pi->device, pi->mode);
	   return 1;
	}

	if (((pi->private%2) == 1) && (pi->mode == 2)) {
	   if (verbose)
		printk("%s: frpw: ASIC does not support mode 2\n",
			pi->device);
	   return 1;
	}

	frpw_connect(pi);
	for (j=0;j<2;j++) {
                frpw_write_regr(pi,0,6,0xa0+j*0x10);
                for (k=0;k<256;k++) {
                        frpw_write_regr(pi,0,2,k^0xaa);
                        frpw_write_regr(pi,0,3,k^0x55);
                        if (frpw_read_regr(pi,0,2) != (k^0xaa)) e[j]++;
                        }
                }
	frpw_disconnect(pi);

	frpw_connect(pi);
        frpw_read_block_int(pi,scratch,512,0x10);
        r = 0;
        for (k=0;k<128;k++) if (scratch[k] != k) r++;
	frpw_disconnect(pi);

        if (verbose)  {
            printk("%s: frpw: port 0x%x, chip %ld, mode %d, test=(%d,%d,%d)\n",
                   pi->device,pi->port,(pi->private%2),pi->mode,e[0],e[1],r);
        }

        return (r || (e[0] && e[1]));
}


static void frpw_log_adapter( PIA *pi, char * scratch, int verbose )

{       char    *mode_string[6] = {"4-bit","8-bit","EPP",
				   "EPP-8","EPP-16","EPP-32"};

        printk("%s: frpw %s, Freecom (%s) adapter at 0x%x, ", pi->device,
		FRPW_VERSION,((pi->private%2) == 0)?"Xilinx":"ASIC",pi->port);
        printk("mode %d (%s), delay %d\n",pi->mode,
		mode_string[pi->mode],pi->delay);

}

static struct pi_protocol frpw = {
	.owner		= THIS_MODULE,
	.name		= "frpw",
	.max_mode	= 6,
	.epp_first	= 2,
	.default_delay	= 2,
	.max_units	= 1,
	.write_regr	= frpw_write_regr,
	.read_regr	= frpw_read_regr,
	.write_block	= frpw_write_block,
	.read_block	= frpw_read_block,
	.connect	= frpw_connect,
	.disconnect	= frpw_disconnect,
	.test_proto	= frpw_test_proto,
	.log_adapter	= frpw_log_adapter,
};

static int __init frpw_init(void)
{
	return paride_register(&frpw)-1;
}

static void __exit frpw_exit(void)
{
	paride_unregister(&frpw);
}

MODULE_LICENSE("GPL");
module_init(frpw_init)
module_exit(frpw_exit)
