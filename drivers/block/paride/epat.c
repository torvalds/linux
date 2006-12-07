/* 
        epat.c  (c) 1997-8  Grant R. Guenther <grant@torque.net>
                            Under the terms of the GNU General Public License.

	This is the low level protocol driver for the EPAT parallel
        to IDE adapter from Shuttle Technologies.  This adapter is
        used in many popular parallel port disk products such as the
        SyQuest EZ drives, the Avatar Shark and the Imation SuperDisk.
	
*/

/* Changes:

        1.01    GRG 1998.05.06 init_proto, release_proto
        1.02    Joshua b. Jore CPP(renamed), epat_connect, epat_disconnect

*/

#define EPAT_VERSION      "1.02"

#include <linux/module.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <asm/io.h>

#include "paride.h"

#define j44(a,b)		(((a>>4)&0x0f)+(b&0xf0))
#define j53(a,b)		(((a>>3)&0x1f)+((b<<4)&0xe0))

static int epatc8;

module_param(epatc8, int, 0);
MODULE_PARM_DESC(epatc8, "support for the Shuttle EP1284 chip, "
	"used in any recent Imation SuperDisk (LS-120) drive.");

/* cont =  0   IDE register file
   cont =  1   IDE control registers
   cont =  2   internal EPAT registers
*/

static int cont_map[3] = { 0x18, 0x10, 0 };

static void epat_write_regr( PIA *pi, int cont, int regr, int val)

{	int r;

	r = regr + cont_map[cont];

	switch (pi->mode) {

	case 0:
	case 1:
	case 2:	w0(0x60+r); w2(1); w0(val); w2(4);
		break;

	case 3:
	case 4:
	case 5: w3(0x40+r); w4(val);
		break;

	}
}

static int epat_read_regr( PIA *pi, int cont, int regr )

{	int  a, b, r;

	r = regr + cont_map[cont];

	switch (pi->mode) {

	case 0:	w0(r); w2(1); w2(3); 
		a = r1(); w2(4); b = r1();
		return j44(a,b);

	case 1: w0(0x40+r); w2(1); w2(4);
		a = r1(); b = r2(); w0(0xff);
		return j53(a,b);

	case 2: w0(0x20+r); w2(1); w2(0x25);
		a = r0(); w2(4);
		return a;

	case 3:
	case 4:
	case 5: w3(r); w2(0x24); a = r4(); w2(4);
		return a;

	}
	return -1;	/* never gets here */
}

static void epat_read_block( PIA *pi, char * buf, int count )

{	int  k, ph, a, b;

	switch (pi->mode) {

	case 0:	w0(7); w2(1); w2(3); w0(0xff);
		ph = 0;
		for(k=0;k<count;k++) {
			if (k == count-1) w0(0xfd);
			w2(6+ph); a = r1();
			if (a & 8) b = a; 
			  else { w2(4+ph); b = r1(); }
			buf[k] = j44(a,b);
			ph =  1 - ph;
		}
		w0(0); w2(4);
		break;

	case 1: w0(0x47); w2(1); w2(5); w0(0xff);
		ph = 0;
		for(k=0;k<count;k++) {
			if (k == count-1) w0(0xfd); 
			w2(4+ph);
			a = r1(); b = r2();
			buf[k] = j53(a,b);
			ph = 1 - ph;
		}
		w0(0); w2(4);
		break;

	case 2: w0(0x27); w2(1); w2(0x25); w0(0);
		ph = 0;
		for(k=0;k<count-1;k++) {
			w2(0x24+ph);
			buf[k] = r0();
			ph = 1 - ph;
		}
		w2(0x26); w2(0x27); buf[count-1] = r0(); 
		w2(0x25); w2(4);
		break;

	case 3: w3(0x80); w2(0x24);
		for(k=0;k<count-1;k++) buf[k] = r4();
		w2(4); w3(0xa0); w2(0x24); buf[count-1] = r4();
		w2(4);
		break;

	case 4: w3(0x80); w2(0x24);
		for(k=0;k<(count/2)-1;k++) ((u16 *)buf)[k] = r4w();
		buf[count-2] = r4();
		w2(4); w3(0xa0); w2(0x24); buf[count-1] = r4();
		w2(4);
		break;

	case 5: w3(0x80); w2(0x24);
		for(k=0;k<(count/4)-1;k++) ((u32 *)buf)[k] = r4l();
		for(k=count-4;k<count-1;k++) buf[k] = r4();
		w2(4); w3(0xa0); w2(0x24); buf[count-1] = r4();
		w2(4);
		break;

	}
}

static void epat_write_block( PIA *pi, char * buf, int count )   

{	int ph, k;

	switch (pi->mode) {

	case 0:
	case 1:
	case 2: w0(0x67); w2(1); w2(5);
		ph = 0;
		for(k=0;k<count;k++) {
		  	w0(buf[k]);
			w2(4+ph);
			ph = 1 - ph;
		}
		w2(7); w2(4);
		break;

	case 3: w3(0xc0); 
		for(k=0;k<count;k++) w4(buf[k]);
		w2(4);
		break;

	case 4: w3(0xc0); 
		for(k=0;k<(count/2);k++) w4w(((u16 *)buf)[k]);
		w2(4);
		break;

	case 5: w3(0xc0); 
		for(k=0;k<(count/4);k++) w4l(((u32 *)buf)[k]);
		w2(4);
		break;

	}
}

/* these macros access the EPAT registers in native addressing */

#define	WR(r,v)		epat_write_regr(pi,2,r,v)
#define	RR(r)		(epat_read_regr(pi,2,r))

/* and these access the IDE task file */

#define WRi(r,v)         epat_write_regr(pi,0,r,v)
#define RRi(r)           (epat_read_regr(pi,0,r))

/* FIXME:  the CPP stuff should be fixed to handle multiple EPATs on a chain */

#define CPP(x) 	w2(4);w0(0x22);w0(0xaa);w0(0x55);w0(0);w0(0xff);\
                w0(0x87);w0(0x78);w0(x);w2(4);w2(5);w2(4);w0(0xff);

static void epat_connect ( PIA *pi )

{       pi->saved_r0 = r0();
        pi->saved_r2 = r2();

 	/* Initialize the chip */
	CPP(0);

	if (epatc8) {
		CPP(0x40);CPP(0xe0);
		w0(0);w2(1);w2(4);
		WR(0x8,0x12);WR(0xc,0x14);WR(0x12,0x10);
		WR(0xe,0xf);WR(0xf,4);
		/* WR(0xe,0xa);WR(0xf,4); */
		WR(0xe,0xd);WR(0xf,0);
		/* CPP(0x30); */
	}

        /* Connect to the chip */
	CPP(0xe0);
        w0(0);w2(1);w2(4); /* Idle into SPP */
        if (pi->mode >= 3) {
          w0(0);w2(1);w2(4);w2(0xc);
          /* Request EPP */
          w0(0x40);w2(6);w2(7);w2(4);w2(0xc);w2(4);
        }

	if (!epatc8) {
		WR(8,0x10); WR(0xc,0x14); WR(0xa,0x38); WR(0x12,0x10);
	}
}

static void epat_disconnect (PIA *pi)
{	CPP(0x30);
	w0(pi->saved_r0);
	w2(pi->saved_r2);
}

static int epat_test_proto( PIA *pi, char * scratch, int verbose )

{       int     k, j, f, cc;
	int	e[2] = {0,0};

        epat_connect(pi);
	cc = RR(0xd);
	epat_disconnect(pi);

	epat_connect(pi);
	for (j=0;j<2;j++) {
  	    WRi(6,0xa0+j*0x10);
            for (k=0;k<256;k++) {
                WRi(2,k^0xaa);
                WRi(3,k^0x55);
                if (RRi(2) != (k^0xaa)) e[j]++;
                }
	    }
        epat_disconnect(pi);

        f = 0;
        epat_connect(pi);
        WR(0x13,1); WR(0x13,0); WR(0xa,0x11);
        epat_read_block(pi,scratch,512);
	
        for (k=0;k<256;k++) {
            if ((scratch[2*k] & 0xff) != k) f++;
            if ((scratch[2*k+1] & 0xff) != (0xff-k)) f++;
        }
        epat_disconnect(pi);

        if (verbose)  {
            printk("%s: epat: port 0x%x, mode %d, ccr %x, test=(%d,%d,%d)\n",
		   pi->device,pi->port,pi->mode,cc,e[0],e[1],f);
	}
	
        return (e[0] && e[1]) || f;
}

static void epat_log_adapter( PIA *pi, char * scratch, int verbose )

{	int	ver;
        char    *mode_string[6] = 
		   {"4-bit","5/3","8-bit","EPP-8","EPP-16","EPP-32"};

	epat_connect(pi);
	WR(0xa,0x38);		/* read the version code */
        ver = RR(0xb);
        epat_disconnect(pi);

	printk("%s: epat %s, Shuttle EPAT chip %x at 0x%x, ",
		pi->device,EPAT_VERSION,ver,pi->port);
	printk("mode %d (%s), delay %d\n",pi->mode,
		mode_string[pi->mode],pi->delay);

}

static struct pi_protocol epat = {
	.owner		= THIS_MODULE,
	.name		= "epat",
	.max_mode	= 6,
	.epp_first	= 3,
	.default_delay	= 1,
	.max_units	= 1,
	.write_regr	= epat_write_regr,
	.read_regr	= epat_read_regr,
	.write_block	= epat_write_block,
	.read_block	= epat_read_block,
	.connect	= epat_connect,
	.disconnect	= epat_disconnect,
	.test_proto	= epat_test_proto,
	.log_adapter	= epat_log_adapter,
};

static int __init epat_init(void)
{
#ifdef CONFIG_PARIDE_EPATC8
	epatc8 = 1;
#endif
	return paride_register(&epat)-1;
}

static void __exit epat_exit(void)
{
	paride_unregister(&epat);
}

MODULE_LICENSE("GPL");
module_init(epat_init)
module_exit(epat_exit)
