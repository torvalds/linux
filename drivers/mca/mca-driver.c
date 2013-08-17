/* -*- mode: c; c-basic-offset: 8 -*- */

/*
 * MCA driver support functions for sysfs.
 *
 * (C) 2002 James Bottomley <James.Bottomley@HansenPartnership.com>
 *
**-----------------------------------------------------------------------------
**  
**  This program is free software; you can redistribute it and/or modify
**  it under the terms of the GNU General Public License as published by
**  the Free Software Foundation; either version 2 of the License, or
**  (at your option) any later version.
**
**  This program is distributed in the hope that it will be useful,
**  but WITHOUT ANY WARRANTY; without even the implied warranty of
**  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**  GNU General Public License for more details.
**
**  You should have received a copy of the GNU General Public License
**  along with this program; if not, write to the Free Software
**  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
**
**-----------------------------------------------------------------------------
 */

#include <linux/device.h>
#include <linux/mca.h>
#include <linux/module.h>

int mca_register_driver(struct mca_driver *mca_drv)
{
	int r;

	if (MCA_bus) {
		mca_drv->driver.bus = &mca_bus_type;
		if ((r = driver_register(&mca_drv->driver)) < 0)
			return r;
		mca_drv->integrated_id = 0;
	}

	return 0;
}
EXPORT_SYMBOL(mca_register_driver);

int mca_register_driver_integrated(struct mca_driver *mca_driver,
				   int integrated_id)
{
	int r = mca_register_driver(mca_driver);

	if (!r)
		mca_driver->integrated_id = integrated_id;

	return r;
}
EXPORT_SYMBOL(mca_register_driver_integrated);

void mca_unregister_driver(struct mca_driver *mca_drv)
{
	if (MCA_bus)
		driver_unregister(&mca_drv->driver);
}
EXPORT_SYMBOL(mca_unregister_driver);
