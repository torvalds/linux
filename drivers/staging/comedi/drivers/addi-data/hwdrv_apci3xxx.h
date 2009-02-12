/**
@verbatim

Copyright (C) 2004,2005  ADDI-DATA GmbH for the source code of this module.

        ADDI-DATA GmbH
        Dieselstrasse 3
        D-77833 Ottersweier
        Tel: +19(0)7223/9493-0
        Fax: +49(0)7223/9493-92
        http://www.addi-data-com
        info@addi-data.com

This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program; if not, write to the Free Software Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA

You shoud also find the complete GPL in the COPYING file accompanying this source code.

@endverbatim
*/

#ifndef COMEDI_SUBD_TTLIO
#define COMEDI_SUBD_TTLIO   11	/* Digital Input Output But TTL */
#endif

#ifndef ADDIDATA_ENABLE
#define ADDIDATA_ENABLE  1
#define ADDIDATA_DISABLE 0
#endif

#define APCI3XXX_SINGLE                              0
#define APCI3XXX_DIFF                                1
#define APCI3XXX_CONFIGURATION                       0

#define APCI3XXX_TTL_INIT_DIRECTION_PORT2   0

#ifdef __KERNEL__

static const comedi_lrange range_apci3XXX_ai = { 8, {BIP_RANGE(10),
			BIP_RANGE(5),
			BIP_RANGE(2),
			BIP_RANGE(1),
			UNI_RANGE(10),
			UNI_RANGE(5),
			UNI_RANGE(2),
	UNI_RANGE(1)}
};

static const comedi_lrange range_apci3XXX_ttl = { 12, {BIP_RANGE(1),
			BIP_RANGE(1),
			BIP_RANGE(1),
			BIP_RANGE(1),
			BIP_RANGE(1),
			BIP_RANGE(1),
			BIP_RANGE(1),
			BIP_RANGE(1),
			BIP_RANGE(1),
			BIP_RANGE(1),
			BIP_RANGE(1),
	BIP_RANGE(1)}
};

static const comedi_lrange range_apci3XXX_ao = { 2, {BIP_RANGE(10),
	UNI_RANGE(10)}
};
#endif
