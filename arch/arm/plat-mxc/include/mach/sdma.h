#ifndef __MACH_MXC_SDMA_H__
#define __MACH_MXC_SDMA_H__

/**
 * struct sdma_platform_data - platform specific data for SDMA engine
 *
 * @sdma_version	The version of this SDMA engine
 * @cpu_name		used to generate the firmware name
 * @to_version		CPU Tape out version
 */
struct sdma_platform_data {
	int sdma_version;
	char *cpu_name;
	int to_version;
};

#endif /* __MACH_MXC_SDMA_H__ */
