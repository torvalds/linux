#ifndef _ASM_POWERPC_OF_DEVICE_H
#define _ASM_POWERPC_OF_DEVICE_H
#ifdef __KERNEL__

#include <linux/device.h>
#include <linux/of.h>

extern struct of_device *of_device_alloc(struct device_node *np,
					 const char *bus_id,
					 struct device *parent);

#endif /* __KERNEL__ */
#endif /* _ASM_POWERPC_OF_DEVICE_H */
