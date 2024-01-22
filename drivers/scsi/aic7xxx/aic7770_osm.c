/*
 * Linux driver attachment glue for aic7770 based controllers.
 *
 * Copyright (c) 2000-2003 Adaptec Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 *
 * $Id: //depot/aic7xxx/linux/drivers/scsi/aic7xxx/aic7770_osm.c#14 $
 */

#include "aic7xxx_osm.h"

#include <linux/device.h>
#include <linux/eisa.h>

int
aic7770_map_registers(struct ahc_softc *ahc, u_int port)
{
	/*
	 * Lock out other contenders for our i/o space.
	 */
	if (!request_region(port, AHC_EISA_IOSIZE, "aic7xxx"))
		return (ENOMEM);
	ahc->tag = BUS_SPACE_PIO;
	ahc->bsh.ioport = port;
	return (0);
}

int
aic7770_map_int(struct ahc_softc *ahc, u_int irq)
{
	int error;
	int shared;

	shared = 0;
	if ((ahc->flags & AHC_EDGE_INTERRUPT) == 0)
		shared = IRQF_SHARED;

	error = request_irq(irq, ahc_linux_isr, shared, "aic7xxx", ahc);
	if (error == 0)
		ahc->platform_data->irq = irq;
	
	return (-error);
}

static int
aic7770_probe(struct device *dev)
{
	struct eisa_device *edev = to_eisa_device(dev);
	u_int eisaBase = edev->base_addr+AHC_EISA_SLOT_OFFSET;
	struct	ahc_softc *ahc;
	char	buf[80];
	char   *name;
	int	error;

	sprintf(buf, "ahc_eisa:%d", eisaBase >> 12);
	name = kstrdup(buf, GFP_ATOMIC);
	if (name == NULL)
		return -ENOMEM;
	ahc = ahc_alloc(&aic7xxx_driver_template, name);
	if (ahc == NULL)
		return -ENOMEM;
	ahc->dev = dev;
	error = aic7770_config(ahc, aic7770_ident_table + edev->id.driver_data,
			       eisaBase);
	if (error != 0) {
		ahc->bsh.ioport = 0;
		ahc_free(ahc);
		return error < 0 ? error : -error;
	}

 	dev_set_drvdata(dev, ahc);

	error = ahc_linux_register_host(ahc, &aic7xxx_driver_template);
	return (error);
}

static int
aic7770_remove(struct device *dev)
{
	struct ahc_softc *ahc = dev_get_drvdata(dev);
	u_long s;

	if (ahc->platform_data && ahc->platform_data->host)
			scsi_remove_host(ahc->platform_data->host);

	ahc_lock(ahc, &s);
	ahc_intr_enable(ahc, FALSE);
	ahc_unlock(ahc, &s);

	ahc_free(ahc);
	return 0;
}
 
static struct eisa_device_id aic7770_ids[] = {
	{ "ADP7771", 0 }, /* AHA 274x */
	{ "ADP7756", 1 }, /* AHA 284x BIOS enabled */
	{ "ADP7757", 2 }, /* AHA 284x BIOS disabled */
	{ "ADP7782", 3 }, /* AHA 274x Olivetti OEM */
	{ "ADP7783", 4 }, /* AHA 274x Olivetti OEM (Differential) */
	{ "ADP7770", 5 }, /* AIC7770 generic */
	{ "" }
};
MODULE_DEVICE_TABLE(eisa, aic7770_ids);

static struct eisa_driver aic7770_driver = {
	.id_table	= aic7770_ids,
	.driver = {
		.name   = "aic7xxx",
		.probe  = aic7770_probe,
		.remove = aic7770_remove,
	}
};
  
int
ahc_linux_eisa_init(void)
{
	return eisa_driver_register(&aic7770_driver);
}
  
void
ahc_linux_eisa_exit(void)
{
	eisa_driver_unregister(&aic7770_driver);
}
