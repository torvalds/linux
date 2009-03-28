
#include <linux/module.h>

unsigned int __mxc_cpu_type;
EXPORT_SYMBOL(__mxc_cpu_type);

void mxc_set_cpu_type(unsigned int type)
{
	__mxc_cpu_type = type;
}

