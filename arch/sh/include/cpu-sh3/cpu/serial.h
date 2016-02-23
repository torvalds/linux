#ifndef __CPU_SH3_SERIAL_H
#define __CPU_SH3_SERIAL_H

#include <linux/serial_sci.h>

extern struct plat_sci_port_ops sh770x_sci_port_ops;
extern struct plat_sci_port_ops sh7710_sci_port_ops;
extern struct plat_sci_port_ops sh7720_sci_port_ops;

#endif /* __CPU_SH3_SERIAL_H */
