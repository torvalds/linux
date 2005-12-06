/*
 *  fixup-tb0226.c, The TANBAC TB0226 specific PCI fixups.
 *
 *  Copyright (C) 2002-2005  Yoichi Yuasa <yuasa@hh.iij4u.or.jp>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <linux/init.h>
#include <linux/pci.h>

#include <asm/vr41xx/giu.h>
#include <asm/vr41xx/tb0226.h>

int __init pcibios_map_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	int irq = -1;

	switch (slot) {
	case 12:
		vr41xx_set_irq_trigger(GD82559_1_PIN,
				       IRQ_TRIGGER_LEVEL,
				       IRQ_SIGNAL_THROUGH);
		vr41xx_set_irq_level(GD82559_1_PIN, IRQ_LEVEL_LOW);
		irq = GD82559_1_IRQ;
		break;
	case 13:
		vr41xx_set_irq_trigger(GD82559_2_PIN,
				       IRQ_TRIGGER_LEVEL,
				       IRQ_SIGNAL_THROUGH);
		vr41xx_set_irq_level(GD82559_2_PIN, IRQ_LEVEL_LOW);
		irq = GD82559_2_IRQ;
		break;
	case 14:
		switch (pin) {
		case 1:
			vr41xx_set_irq_trigger(UPD720100_INTA_PIN,
					       IRQ_TRIGGER_LEVEL,
					       IRQ_SIGNAL_THROUGH);
			vr41xx_set_irq_level(UPD720100_INTA_PIN,
					     IRQ_LEVEL_LOW);
			irq = UPD720100_INTA_IRQ;
			break;
		case 2:
			vr41xx_set_irq_trigger(UPD720100_INTB_PIN,
					       IRQ_TRIGGER_LEVEL,
					       IRQ_SIGNAL_THROUGH);
			vr41xx_set_irq_level(UPD720100_INTB_PIN,
					     IRQ_LEVEL_LOW);
			irq = UPD720100_INTB_IRQ;
			break;
		case 3:
			vr41xx_set_irq_trigger(UPD720100_INTC_PIN,
					       IRQ_TRIGGER_LEVEL,
					       IRQ_SIGNAL_THROUGH);
			vr41xx_set_irq_level(UPD720100_INTC_PIN,
					     IRQ_LEVEL_LOW);
			irq = UPD720100_INTC_IRQ;
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}

	return irq;
}

/* Do platform specific device initialization at pci_enable_device() time */
int pcibios_plat_dev_init(struct pci_dev *dev)
{
	return 0;
}
