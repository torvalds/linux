/*
 * CNS3xxx common devices
 *
 * Copyright 2008 Cavium Networks
 *		  Scott Shu
 * Copyright 2010 MontaVista Software, LLC.
 *		  Anton Vorontsov <avorontsov@mvista.com>
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, Version 2, as
 * published by the Free Software Foundation.
 */

#ifndef __CNS3XXX_DEVICES_H_
#define __CNS3XXX_DEVICES_H_

void __init cns3xxx_ahci_init(void);
void __init cns3xxx_sdhci_init(void);

#endif /* __CNS3XXX_DEVICES_H_ */
