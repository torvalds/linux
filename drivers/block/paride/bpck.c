/* 
	bpck.c	(c) 1996-8  Grant R. Guenther <grant@torque.net>
		            Under the terms of the GNU General Public License.

	bpck.c is a low-level protocol driver for the MicroSolutions 
	"backpack" parallel port IDE adapter.  

*/

/* Changes:

	1.01	GRG 1998.05.05 init_proto, release_proto, pi->delay 
	1.02    GRG 1998.08.15 default pi->delay returned to 4

*/

#define	BPCK_VERSION	"1.02" 

#include <linux/module.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <asm/io.h>

#include "paride.h"

#undef r2
#undef w2

#define PC			pi->private
#define r2()			(PC=(in_p(2) & 0xff))
#define w2(byte)  		{out_p(2,byte); PC = byte;}
#define t2(pat)   		{PC ^= pat; out_p(2,PC);}
#define e2()			{PC &= 0xfe; out_p(2,PC);}
#define o2()			{PC |= 1; out_p(2,PC);}

#define j44(l,h)     (((l>>3)&0x7)|((l>>4)&0x8)|((h<<1)&0x70)|(h&0x80))

/* cont = 0 - access the IDE register file 
   cont = 1 - access the IDE command set 
   cont = 2 - use internal bpck register addressing
*/

static int  cont_map[3] = { 0x40, 0x48, 0 };

static int bpck_read_regr( PIA *pi, int cont, int regr )

{       int r, l, h;

	r = regr + cont_map[cont];

	switch (pi->mode) {

	case 0: w0(r & 0xf); w0(r); t2(2); t2(4);
	        l = r1();
        	t2(4);
        	h = r1();
        	return j44(l,h);

	case 1: w0(r & 0xf); w0(r); t2(2);
	        e2(); t2(0x20);
		t2(4); h = r0();
	        t2(1); t2(0x20);
	        return h;

	case 2:
	case 3:
	case 4: w0(r); w2(9); w2(0); w2(0x20);
		h = r4();
		w2(0);
		return h;

	}
	return -1;
}	

static void bpck_write_regr( PIA *pi, int cont, int regr, int val )

{	int	r;

        r = regr + cont_map[cont];

	switch (pi->mode) {

	case 0:
	case 1: w0(r);
		t2(2);
		w0(val);
		o2(); t2(4); t2(1);
		break;

	case 2:
	case 3:
	case 4: w0(r); w2(9); w2(0);
		w0(val); w2(1); w2(3); w2(0);
		break;

	}
}

/* These macros access the bpck registers in native addressing */

#define WR(r,v)		bpck_write_regr(pi,2,r,v)
#define RR(r)		(bpck_read_regr(pi,2,r))

static void bpck_write_block( PIA *pi, char * buf, int count )

{	int i;

	switch (pi->mode) {

	case 0: WR(4,0x40);
		w0(0x40); t2(2); t2(1);
		for (i=0;i<count;i++) { w0(buf[i]); t2(4); }
		WR(4,0);
		break;

	case 1: WR(4,0x50);
                w0(0x40); t2(2); t2(1);
                for (i=0;i<count;i++) { w0(buf[i]); t2(4); }
                WR(4,0x10);
		break;

	case 2: WR(4,0x48);
		w0(0x40); w2(9); w2(0); w2(1);
		for (i=0;i<count;i++) w4(buf[i]);
		w2(0);
		WR(4,8);
		break;

        case 3: WR(4,0x48);
                w0(0x40); w2(9); w2(0); w2(1);
                for (i=0;i<count/2;i++) w4w(((u16 *)buf)[i]);
                w2(0);
                WR(4,8);
                break;
 
        case 4: WR(4,0x48);
                w0(0x40); w2(9); w2(0); w2(1);
                for (i=0;i<count/4;i++) w4l(((u32 *)buf)[i]);
                w2(0);
                WR(4,8);
                break;
 	}
}

static void bpck_read_block( PIA *pi, char * buf, int count )

{	int i, l, h;

	switch (pi->mode) {

      	case 0: WR(4,0x40);
		w0(0x40); t2(2);
		for (i=0;i<count;i++) {
		    t2(4); l = r1();
		    t2(4); h = r1();
		    buf[i] = j44(l,h);
		}
		WR(4,0);
		break;

	case 1: WR(4,0x50);
		w0(0x40); t2(2); t2(0x20);
      	        for(i=0;i<count;i++) { t2(4); buf[i] = r0(); }
	        t2(1); t2(0x20);
	        WR(4,0x10);
		break;

	case 2: WR(4,0x48);
		w0(0x40); w2(9); w2(0); w2(0x20);
		for (i=0;i<count;i++) buf[i] = r4();
		w2(0);
		WR(4,8);
		break;

        case 3: WR(4,0x48);
                w0(0x40); w2(9); w2(0); w2(0x20);
                for (i=0;i<count/2;i++) ((u16 *)buf)[i] = r4w();
                w2(0);
                WR(4,8);
                break;

        case 4: WR(4,0x48);
                w0(0x40); w2(9); w2(0); w2(0x20);
                for (i=0;i<count/4;i++) ((u32 *)buf)[i] = r4l();
                w2(0);
                WR(4,8);
                break;

	}
}

static int bpck_probe_unit ( PIA *pi )

{	int o1, o0, f7, id;
	int t, s;

	id = pi->unit;
	s = 0;
	w2(4); w2(0xe); r2(); t2(2); 
	o1 = r1()&0xf8;
	o0 = r0();
	w0(255-id); w2(4); w0(id);
	t2(8); t2(8); t2(8);
	t2(2); t = r1()&0xf8;
	f7 = ((id % 8) == 7);
	if ((f7) || (t != o1)) { t2(2); s = r1()&0xf8; }
	if ((t == o1) && ((!f7) || (s == o1)))  {
		w2(0x4c); w0(o0);
		return 0;	
	}
	t2(8); w0(0); t2(2); w2(0x4c); w0(o0);
	return 1;
}
	
static void bpck_connect ( PIA *pi  )

{       pi->saved_r0 = r0();
	w0(0xff-pi->unit); w2(4); w0(pi->unit);
	t2(8); t2(8); t2(8); 
	t2(2); t2(2);
	
	switch (pi->mode) {

	case 0: t2(8); WR(4,0);
		break;

	case 1: t2(8); WR(4,0x10);
		break;

	case 2:
        case 3:
	case 4: w2(0); WR(4,8);
		break;

	}

	WR(5,8);

	if (pi->devtype == PI_PCD) {
		WR(0x46,0x10);		/* fiddle with ESS logic ??? */
		WR(0x4c,0x38);
		WR(0x4d,0x88);
		WR(0x46,0xa0);
		WR(0x41,0);
		WR(0x4e,8);
		}
}

static void bpck_disconnect ( PIA *pi )

{	w0(0); 
	if (pi->mode >= 2) { w2(9); w2(0); } else t2(2);
	w2(0x4c); w0(pi->saved_r0);
} 

static void bpck_force_spp ( PIA *pi )

/* This fakes the EPP protocol to turn off EPP ... */

{       pi->saved_r0 = r0();
        w0(0xff-pi->unit); w2(4); w0(pi->unit);
        t2(8); t2(8); t2(8); 
        t2(2); t2(2);

        w2(0); 
        w0(4); w2(9); w2(0); 
        w0(0); w2(1); w2(3); w2(0);     
        w0(0); w2(9); w2(0);
        w2(0x4c); w0(pi->saved_r0);
}

#define TEST_LEN  16

static int bpck_test_proto( PIA *pi, char * scratch, int verbose )

{	int i, e, l, h, om;
	char buf[TEST_LEN];

	bpck_force_spp(pi);

	switch (pi->mode) {

	case 0: bpck_connect(pi);
		WR(0x13,0x7f);
		w0(0x13); t2(2);
		for(i=0;i<TEST_LEN;i++) {
                    t2(4); l = r1();
                    t2(4); h = r1();
                    buf[i] = j44(l,h);
		}
		bpck_disconnect(pi);
		break;

        case 1: bpck_connect(pi);
		WR(0x13,0x7f);
                w0(0x13); t2(2); t2(0x20);
                for(i=0;i<TEST_LEN;i++) { t2(4); buf[i] = r0(); }
                t2(1); t2(0x20);
		bpck_disconnect(pi);
		break;

	case 2:
	case 3:
	case 4: om = pi->mode;
		pi->mode = 0;
		bpck_connect(pi);
		WR(7,3);
		WR(4,8);
		bpck_disconnect(pi);

		pi->mode = om;
		bpck_connect(pi);
		w0(0x13); w2(9); w2(1); w0(0); w2(3); w2(0); w2(0xe0);

		switch (pi->mode) {
		  case 2: for (i=0;i<TEST_LEN;i++) buf[i] = r4();
			  break;
		  case 3: for (i=0;i<TEST_LEN/2;i++) ((u16 *)buf)[i] = r4w();
                          break;
		  case 4: for (i=0;i<TEST_LEN/4;i++) ((u32 *)buf)[i] = r4l();
                          break;
		}

		w2(0);
		WR(7,0);
		bpck_disconnect(pi);

		break;

	}

	if (verbose) {
	    printk("%s: bpck: 0x%x unit %d mode %d: ",
		   pi->device,pi->port,pi->unit,pi->mode);
	    for (i=0;i<TEST_LEN;i++) printk("%3d",buf[i]);
	    printk("\n");
	}

	e = 0;
	for (i=0;i<TEST_LEN;i++) if (buf[i] != (i+1)) e++;
	return e;
}

static void bpck_read_eeprom ( PIA *pi, char * buf )

{       int i, j, k, p, v, f, om, od;

	bpck_force_spp(pi);

	om = pi->mode;  od = pi->delay;
	pi->mode = 0; pi->delay = 6;

	bpck_connect(pi);
	
	WR(4,0);
	for (i=0;i<64;i++) {
	    WR(6,8);  
	    WR(6,0xc);
	    p = 0x100;
	    for (k=0;k<9;k++) {
		f = (((i + 0x180) & p) != 0) * 2;
		WR(6,f+0xc); 
		WR(6,f+0xd); 
		WR(6,f+0xc);
		p = (p >> 1);
	    }
	    for (j=0;j<2;j++) {
		v = 0;
		for (k=0;k<8;k++) {
		    WR(6,0xc); 
		    WR(6,0xd); 
		    WR(6,0xc); 
		    f = RR(0);
		    v = 2*v + (f == 0x84);
		}
		buf[2*i+1-j] = v;
	    }
	}
	WR(6,8);
	WR(6,0);
	WR(5,8);

	bpck_disconnect(pi);

        if (om >= 2) {
                bpck_connect(pi);
                WR(7,3);
                WR(4,8);
                bpck_disconnect(pi);
        }

	pi->mode = om; pi->delay = od;
}

static int bpck_test_port ( PIA *pi ) 	/* check for 8-bit port */

{	int	i, r, m;

	w2(0x2c); i = r0(); w0(255-i); r = r0(); w0(i);
	m = -1;
	if (r == i) m = 2;
	if (r == (255-i)) m = 0;

	w2(0xc); i = r0(); w0(255-i); r = r0(); w0(i);
	if (r != (255-i)) m = -1;
	
	if (m == 0) { w2(6); w2(0xc); r = r0(); w0(0xaa); w0(r); w0(0xaa); }
	if (m == 2) { w2(0x26); w2(0xc); }

	if (m == -1) return 0;
	return 5;
}

static void bpck_log_adapter( PIA *pi, char * scratch, int verbose )

{	char	*mode_string[5] = { "4-bit","8-bit","EPP-8",
				    "EPP-16","EPP-32" };

#ifdef DUMP_EEPROM
	int i;
#endif

	bpck_read_eeprom(pi,scratch);

#ifdef DUMP_EEPROM
	if (verbose) {
	   for(i=0;i<128;i++)
		if ((scratch[i] < ' ') || (scratch[i] > '~'))
		    scratch[i] = '.';
	   printk("%s: bpck EEPROM: %64.64s\n",pi->device,scratch);
	   printk("%s:              %64.64s\n",pi->device,&scratch[64]);
	}
#endif

	printk("%s: bpck %s, backpack %8.8s unit %d",
		pi->device,BPCK_VERSION,&scratch[110],pi->unit);
	printk(" at 0x%x, mode %d (%s), delay %d\n",pi->port,
		pi->mode,mode_string[pi->mode],pi->delay);
}

static struct pi_protocol bpck = {
	.owner		= THIS_MODULE,
	.name		= "bpck",
	.max_mode	= 5,
	.epp_first	= 2,
	.default_delay	= 4,
	.max_units	= 255,
	.write_regr	= bpck_write_regr,
	.read_regr	= bpck_read_regr,
	.write_block	= bpck_write_block,
	.read_block	= bpck_read_block,
	.connect	= bpck_connect,
	.disconnect	= bpck_disconnect,
	.test_port	= bpck_test_port,
	.probe_unit	= bpck_probe_unit,
	.test_proto	= bpck_test_proto,
	.log_adapter	= bpck_log_adapter,
};

static int __init bpck_init(void)
{
	return paride_register(&bpck);
}

static void __exit bpck_exit(void)
{
	paride_unregister(&bpck);
}

MODULE_LICENSE("GPL");
module_init(bpck_init)
module_exit(bpck_exit)
