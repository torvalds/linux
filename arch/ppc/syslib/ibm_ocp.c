#include <linux/module.h>
#include <asm/ibm4xx.h>
#include <asm/ocp.h>

struct ocp_sys_info_data ocp_sys_info = {
	.opb_bus_freq	=	50000000,	/* OPB Bus Frequency (Hz) */
	.ebc_bus_freq	=	33333333,	/* EBC Bus Frequency (Hz) */
};

EXPORT_SYMBOL(ocp_sys_info);
