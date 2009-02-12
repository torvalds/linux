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

#define APCI1710_30MHZ           30
#define APCI1710_33MHZ           33
#define APCI1710_40MHZ           40

#define APCI1710_BINARY_MODE     0x1
#define APCI1710_GRAY_MODE       0x0

#define APCI1710_SSI_READ1VALUE				1
#define APCI1710_SSI_READALLVALUE			2

#define APCI1710_SSI_SET_CHANNELON       0
#define APCI1710_SSI_SET_CHANNELOFF		1
#define APCI1710_SSI_READ_1CHANNEL		2
#define APCI1710_SSI_READ_ALLCHANNEL   	3

/*
+----------------------------------------------------------------------------+
|                       SSI INISIALISATION FUNCTION                          |
+----------------------------------------------------------------------------+
*/
INT i_APCI1710_InsnConfigInitSSI(comedi_device * dev, comedi_subdevice * s,
	comedi_insn * insn, lsampl_t * data);

INT i_APCI1710_InsnReadSSIValue(comedi_device * dev, comedi_subdevice * s,
	comedi_insn * insn, lsampl_t * data);

INT i_APCI1710_InsnBitsSSIDigitalIO(comedi_device * dev, comedi_subdevice * s,
	comedi_insn * insn, lsampl_t * data);
