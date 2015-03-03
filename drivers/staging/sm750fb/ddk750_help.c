//#include "ddk750_reg.h"
//#include "ddk750_chip.h"
#include "ddk750_help.h"

volatile unsigned char __iomem * mmio750 = NULL;
char revId750 = 0;
unsigned short devId750 = 0;

/* after driver mapped io registers, use this function first */
void ddk750_set_mmio(volatile unsigned char * addr,unsigned short devId,char revId)
{
	mmio750 = addr;
	devId750 = devId;
	revId750 = revId;
	if(revId == 0xfe)
		printk("found sm750le\n");
}


