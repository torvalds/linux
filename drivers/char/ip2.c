// ip2.c
// This is a dummy module to make the firmware available when needed
// and allows it to be unloaded when not. Rumor is the __initdata 
// macro doesn't always works on all platforms so we use this kludge.
// If not compiled as a module it just makes fip_firm avaliable then
//  __initdata should work as advertized
//

#include <linux/module.h>
#include <linux/version.h>
#include <linux/init.h>
#include <linux/wait.h>

#ifndef __init
#define __init
#endif
#ifndef __initfunc
#define __initfunc(a) a
#endif
#ifndef __initdata
#define __initdata
#endif

#include "./ip2/ip2types.h"		
#include "./ip2/fip_firm.h"		// the meat

int
ip2_loadmain(int *, int  *, unsigned char *, int ); // ref into ip2main.c

/* Note: Add compiled in defaults to these arrays, not to the structure
	in ip2/ip2.h any longer.  That structure WILL get overridden
	by these values, or command line values, or insmod values!!!  =mhw=
*/
static int io[IP2_MAX_BOARDS]= { 0, 0, 0, 0 };
static int irq[IP2_MAX_BOARDS] = { -1, -1, -1, -1 }; 

static int poll_only = 0;

MODULE_AUTHOR("Doug McNash");
MODULE_DESCRIPTION("Computone IntelliPort Plus Driver");
module_param_array(irq, int, NULL, 0);
MODULE_PARM_DESC(irq,"Interrupts for IntelliPort Cards");
module_param_array(io, int, NULL, 0);
MODULE_PARM_DESC(io,"I/O ports for IntelliPort Cards");
module_param(poll_only, bool, 0);
MODULE_PARM_DESC(poll_only,"Do not use card interrupts");


static int __init ip2_init(void)
{
	if( poll_only ) {
		/* Hard lock the interrupts to zero */
		irq[0] = irq[1] = irq[2] = irq[3] = 0;
	}

	return ip2_loadmain(io,irq,(unsigned char *)fip_firm,sizeof(fip_firm));
}
module_init(ip2_init);

MODULE_LICENSE("GPL");

#ifndef MODULE
/******************************************************************************
 *	ip2_setup:
 *		str: kernel command line string
 *
 *	Can't autoprobe the boards so user must specify configuration on
 *	kernel command line.  Sane people build it modular but the others
 *	come here.
 *
 *	Alternating pairs of io,irq for up to 4 boards.
 *		ip2=io0,irq0,io1,irq1,io2,irq2,io3,irq3
 *
 *		io=0 => No board
 *		io=1 => PCI
 *		io=2 => EISA
 *		else => ISA I/O address
 *
 *		irq=0 or invalid for ISA will revert to polling mode
 *
 *		Any value = -1, do not overwrite compiled in value.
 *
 ******************************************************************************/
static int __init ip2_setup(char *str)
{
	int	ints[10];	/* 4 boards, 2 parameters + 2 */
	int	i, j;

	str = get_options (str, ARRAY_SIZE(ints), ints);

	for( i = 0, j = 1; i < 4; i++ ) {
		if( j > ints[0] ) {
			break;
		}
		if( ints[j] >= 0 ) {
			io[i] = ints[j];
		}
		j++;
		if( j > ints[0] ) {
			break;
		}
		if( ints[j] >= 0 ) {
			irq[i] = ints[j];
		}
		j++;
	}
	return 1;
}
__setup("ip2=", ip2_setup);
#endif /* !MODULE */
