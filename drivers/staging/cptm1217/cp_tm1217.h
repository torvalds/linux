#ifndef __LINUX_I2C_CP_TM1217_H
#define __LINUX_I2C_CP_TM1217_H

struct cp_tm1217_platform_data
{
	int gpio;		/* If not set uses the IRQ resource 0 */
};

#endif
