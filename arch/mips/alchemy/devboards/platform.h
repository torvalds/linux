#ifndef _DEVBOARD_PLATFORM_H_
#define _DEVBOARD_PLATFORM_H_

#include <linux/init.h>

int __init db1x_register_pcmcia_socket(unsigned long pseudo_attr_start,
				       unsigned long pseudo_attr_len,
				       unsigned long pseudo_mem_start,
				       unsigned long pseudo_mem_end,
				       unsigned long pseudo_io_start,
				       unsigned long pseudo_io_end,
				       int card_irq,
				       int cd_irq,
				       int stschg_irq,
				       int eject_irq,
				       int id);

#endif
