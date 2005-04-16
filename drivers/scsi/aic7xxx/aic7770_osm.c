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

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0)
#include <linux/device.h>
#include <linux/eisa.h>

#define EISA_MFCTR_CHAR0(ID) (char)(((ID>>26) & 0x1F) | '@')  /* Bits 26-30 */
#define EISA_MFCTR_CHAR1(ID) (char)(((ID>>21) & 0x1F) | '@')  /* Bits 21-25 */
#define EISA_MFCTR_CHAR2(ID) (char)(((ID>>16) & 0x1F) | '@')  /* Bits 16-20 */
#define EISA_PRODUCT_ID(ID)  (short)((ID>>4)  & 0xFFF)        /* Bits  4-15 */
#define EISA_REVISION_ID(ID) (uint8_t)(ID & 0x0F)             /* Bits  0-3  */

static int aic7770_eisa_dev_probe(struct device *dev);
static int aic7770_eisa_dev_remove(struct device *dev);
static struct eisa_driver aic7770_driver = {
	.driver = {
		.name   = "aic7xxx",
		.probe  = aic7770_eisa_dev_probe,
		.remove = aic7770_eisa_dev_remove,
	}
};

typedef  struct device *aic7770_dev_t;
#else
#define MINSLOT			1
#define NUMSLOTS		16
#define IDOFFSET		0x80

typedef void *aic7770_dev_t;
#endif

static int aic7770_linux_config(struct aic7770_identity *entry,
				aic7770_dev_t dev, u_int eisaBase);

int
ahc_linux_eisa_init(void)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0)
	struct eisa_device_id *eid;
	struct aic7770_identity *id;
	int i;

	if (aic7xxx_probe_eisa_vl == 0)
		return -ENODEV;

	/*
	 * Linux requires the EISA IDs to be specified in
	 * the EISA ID string format.  Perform the conversion
	 * and setup a table with a NUL terminal entry.
	 */
	aic7770_driver.id_table = malloc(sizeof(struct eisa_device_id) *
					 (ahc_num_aic7770_devs + 1),
					 M_DEVBUF, M_NOWAIT);
	if (aic7770_driver.id_table == NULL)
		return -ENOMEM;

	for (eid = (struct eisa_device_id *)aic7770_driver.id_table,
	     id = aic7770_ident_table, i = 0;
	     i < ahc_num_aic7770_devs; eid++, id++, i++) {

		sprintf(eid->sig, "%c%c%c%03X%01X",
			EISA_MFCTR_CHAR0(id->full_id),
			EISA_MFCTR_CHAR1(id->full_id),
			EISA_MFCTR_CHAR2(id->full_id),
			EISA_PRODUCT_ID(id->full_id),
			EISA_REVISION_ID(id->full_id));
		eid->driver_data = i;
	}
	eid->sig[0] = 0;

	return eisa_driver_register(&aic7770_driver);
#else /* LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0) */
	struct aic7770_identity *entry;
	u_int  slot;
	u_int  eisaBase;
	u_int  i;
	int ret = -ENODEV;

	if (aic7xxx_probe_eisa_vl == 0)
		return ret;

	eisaBase = 0x1000 + AHC_EISA_SLOT_OFFSET;
	for (slot = 1; slot < NUMSLOTS; eisaBase+=0x1000, slot++) {
		uint32_t eisa_id;
		size_t	 id_size;

		if (request_region(eisaBase, AHC_EISA_IOSIZE, "aic7xxx") == 0)
			continue;

		eisa_id = 0;
		id_size = sizeof(eisa_id);
		for (i = 0; i < 4; i++) {
			/* VLcards require priming*/
			outb(0x80 + i, eisaBase + IDOFFSET);
			eisa_id |= inb(eisaBase + IDOFFSET + i)
				   << ((id_size-i-1) * 8);
		}
		release_region(eisaBase, AHC_EISA_IOSIZE);
		if (eisa_id & 0x80000000)
			continue;  /* no EISA card in slot */

		entry = aic7770_find_device(eisa_id);
		if (entry != NULL) {
			aic7770_linux_config(entry, NULL, eisaBase);
			ret = 0;
		}
	}
	return ret;
#endif
}

void
ahc_linux_eisa_exit(void)
{
	if(aic7xxx_probe_eisa_vl != 0 && aic7770_driver.id_table != NULL) {
		eisa_driver_unregister(&aic7770_driver);
		free(aic7770_driver.id_table, M_DEVBUF);
	}
}

static int
aic7770_linux_config(struct aic7770_identity *entry, aic7770_dev_t dev,
		     u_int eisaBase)
{
	struct	ahc_softc *ahc;
	char	buf[80];
	char   *name;
	int	error;

	/*
	 * Allocate a softc for this card and
	 * set it up for attachment by our
	 * common detect routine.
	 */
	sprintf(buf, "ahc_eisa:%d", eisaBase >> 12);
	name = malloc(strlen(buf) + 1, M_DEVBUF, M_NOWAIT);
	if (name == NULL)
		return (ENOMEM);
	strcpy(name, buf);
	ahc = ahc_alloc(&aic7xxx_driver_template, name);
	if (ahc == NULL)
		return (ENOMEM);
	error = aic7770_config(ahc, entry, eisaBase);
	if (error != 0) {
		ahc->bsh.ioport = 0;
		ahc_free(ahc);
		return (error);
	}
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0)
	dev->driver_data = (void *)ahc;
	if (aic7xxx_detect_complete)
		error = ahc_linux_register_host(ahc, &aic7xxx_driver_template);
#endif
	return (error);
}

int
aic7770_map_registers(struct ahc_softc *ahc, u_int port)
{
	/*
	 * Lock out other contenders for our i/o space.
	 */
	if (request_region(port, AHC_EISA_IOSIZE, "aic7xxx") == 0)
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
		shared = SA_SHIRQ;

	error = request_irq(irq, ahc_linux_isr, shared, "aic7xxx", ahc);
	if (error == 0)
		ahc->platform_data->irq = irq;
	
	return (-error);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0)
static int
aic7770_eisa_dev_probe(struct device *dev)
{
	struct eisa_device *edev;

	edev = to_eisa_device(dev);
	return (aic7770_linux_config(aic7770_ident_table + edev->id.driver_data,
				    dev, edev->base_addr+AHC_EISA_SLOT_OFFSET));
}

static int
aic7770_eisa_dev_remove(struct device *dev)
{
	struct ahc_softc *ahc;
	u_long l;

	/*
	 * We should be able to just perform
	 * the free directly, but check our
	 * list for extra sanity.
	 */
	ahc_list_lock(&l);
	ahc = ahc_find_softc((struct ahc_softc *)dev->driver_data);
	if (ahc != NULL) {
		u_long s;

		ahc_lock(ahc, &s);
		ahc_intr_enable(ahc, FALSE);
		ahc_unlock(ahc, &s);
		ahc_free(ahc);
	}
	ahc_list_unlock(&l);

	return (0);
}
#endif
