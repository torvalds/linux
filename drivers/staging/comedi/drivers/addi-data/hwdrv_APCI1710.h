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

#define COMEDI_SUBD_TTLIO				11	/* Digital Input Output But TTL */
#define COMEDI_SUBD_PWM					12	/* Pulse width Measurement */
#define COMEDI_SUBD_SSI					13	/* Synchronous serial interface */
#define COMEDI_SUBD_TOR					14	/* Tor counter */
#define COMEDI_SUBD_CHRONO              15	/* Chrono meter */
#define COMEDI_SUBD_PULSEENCODER        16	/* Pulse Encoder INP CPT */
#define COMEDI_SUBD_INCREMENTALCOUNTER  17	/* Incremental Counter */

#define      APCI1710_BOARD_NAME			  "apci1710"
#define      APCI1710_BOARD_VENDOR_ID	  0x10E8
#define      APCI1710_BOARD_DEVICE_ID	  0x818F
#define      APCI1710_ADDRESS_RANGE        256
#define      APCI1710_CONFIG_ADDRESS_RANGE 8
#define      APCI1710_INCREMENTAL_COUNTER  0x53430000UL
#define      APCI1710_SSI_COUNTER          0x53490000UL
#define      APCI1710_TTL_IO               0x544C0000UL
#define      APCI1710_DIGITAL_IO           0x44490000UL
#define      APCI1710_82X54_TIMER          0x49430000UL
#define      APCI1710_CHRONOMETER          0x43480000UL
#define      APCI1710_PULSE_ENCODER	      0x495A0000UL
#define      APCI1710_TOR_COUNTER	      0x544F0000UL
#define      APCI1710_PWM		      0x50570000UL
#define      APCI1710_ETM		      0x45540000UL
#define      APCI1710_CDA		      0x43440000UL
#define      APCI1710_DISABLE              0
#define      APCI1710_ENABLE               1
#define      APCI1710_SYNCHRONOUS_MODE     1
#define      APCI1710_ASYNCHRONOUS_MODE    0

//MODULE INFO STRUCTURE

static const comedi_lrange range_apci1710_ttl = { 4, {
			BIP_RANGE(10),
			BIP_RANGE(5),
			BIP_RANGE(2),
			BIP_RANGE(1)
	}
};

static const comedi_lrange range_apci1710_ssi = { 4, {
			BIP_RANGE(10),
			BIP_RANGE(5),
			BIP_RANGE(2),
			BIP_RANGE(1)
	}
};

static const comedi_lrange range_apci1710_inccpt = { 4, {
			BIP_RANGE(10),
			BIP_RANGE(5),
			BIP_RANGE(2),
			BIP_RANGE(1)
	}
};
