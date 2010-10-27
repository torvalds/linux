/*
 * ngene-cards.c: nGene PCIe bridge driver - card specific info
 *
 * Copyright (C) 2005-2007 Micronas
 *
 * Copyright (C) 2008-2009 Ralph Metzler <rjkm@metzlerbros.de>
 *                         Modifications for new nGene firmware,
 *                         support for EEPROM-copying,
 *                         support for new dual DVB-S2 card prototype
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 only, as published by the Free Software Foundation.
 *
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA
 * Or, point your browser to http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>

#include "ngene.h"

/* demods/tuners */
#include "stv6110x.h"
#include "stv090x.h"
#include "lnbh24.h"
#include "lgdt330x.h"
#include "mt2131.h"


/****************************************************************************/
/* Demod/tuner attachment ***************************************************/
/****************************************************************************/

static int tuner_attach_stv6110(struct ngene_channel *chan)
{
	struct stv090x_config *feconf = (struct stv090x_config *)
		chan->dev->card_info->fe_config[chan->number];
	struct stv6110x_config *tunerconf = (struct stv6110x_config *)
		chan->dev->card_info->tuner_config[chan->number];
	struct stv6110x_devctl *ctl;

	ctl = dvb_attach(stv6110x_attach, chan->fe, tunerconf,
			 &chan->i2c_adapter);
	if (ctl == NULL) {
		printk(KERN_ERR	DEVICE_NAME ": No STV6110X found!\n");
		return -ENODEV;
	}

	feconf->tuner_init          = ctl->tuner_init;
	feconf->tuner_set_mode      = ctl->tuner_set_mode;
	feconf->tuner_set_frequency = ctl->tuner_set_frequency;
	feconf->tuner_get_frequency = ctl->tuner_get_frequency;
	feconf->tuner_set_bandwidth = ctl->tuner_set_bandwidth;
	feconf->tuner_get_bandwidth = ctl->tuner_get_bandwidth;
	feconf->tuner_set_bbgain    = ctl->tuner_set_bbgain;
	feconf->tuner_get_bbgain    = ctl->tuner_get_bbgain;
	feconf->tuner_set_refclk    = ctl->tuner_set_refclk;
	feconf->tuner_get_status    = ctl->tuner_get_status;

	return 0;
}


static int demod_attach_stv0900(struct ngene_channel *chan)
{
	struct stv090x_config *feconf = (struct stv090x_config *)
		chan->dev->card_info->fe_config[chan->number];

	chan->fe = dvb_attach(stv090x_attach,
			feconf,
			&chan->i2c_adapter,
			chan->number == 0 ? STV090x_DEMODULATOR_0 :
					    STV090x_DEMODULATOR_1);
	if (chan->fe == NULL) {
		printk(KERN_ERR	DEVICE_NAME ": No STV0900 found!\n");
		return -ENODEV;
	}

	if (!dvb_attach(lnbh24_attach, chan->fe, &chan->i2c_adapter, 0,
			0, chan->dev->card_info->lnb[chan->number])) {
		printk(KERN_ERR DEVICE_NAME ": No LNBH24 found!\n");
		dvb_frontend_detach(chan->fe);
		return -ENODEV;
	}

	return 0;
}

static struct lgdt330x_config aver_m780 = {
	.demod_address = 0xb2 >> 1,
	.demod_chip    = LGDT3303,
	.serial_mpeg   = 0x00, /* PARALLEL */
	.clock_polarity_flip = 1,
};

static struct mt2131_config m780_tunerconfig = {
	0xc0 >> 1
};

/* A single func to attach the demo and tuner, rather than
 * use two sep funcs like the current design mandates.
 */
static int demod_attach_lg330x(struct ngene_channel *chan)
{
	chan->fe = dvb_attach(lgdt330x_attach, &aver_m780, &chan->i2c_adapter);
	if (chan->fe == NULL) {
		printk(KERN_ERR	DEVICE_NAME ": No LGDT330x found!\n");
		return -ENODEV;
	}

	dvb_attach(mt2131_attach, chan->fe, &chan->i2c_adapter,
		   &m780_tunerconfig, 0);

	return (chan->fe) ? 0 : -ENODEV;
}

/****************************************************************************/
/* Switch control (I2C gates, etc.) *****************************************/
/****************************************************************************/


static struct stv090x_config fe_cineS2 = {
	.device         = STV0900,
	.demod_mode     = STV090x_DUAL,
	.clk_mode       = STV090x_CLK_EXT,

	.xtal           = 27000000,
	.address        = 0x68,

	.ts1_mode       = STV090x_TSMODE_SERIAL_PUNCTURED,
	.ts2_mode       = STV090x_TSMODE_SERIAL_PUNCTURED,

	.repeater_level = STV090x_RPTLEVEL_16,

	.adc1_range	= STV090x_ADC_1Vpp,
	.adc2_range	= STV090x_ADC_1Vpp,

	.diseqc_envelope_mode = true,
};

static struct stv6110x_config tuner_cineS2_0 = {
	.addr	= 0x60,
	.refclk	= 27000000,
	.clk_div = 1,
};

static struct stv6110x_config tuner_cineS2_1 = {
	.addr	= 0x63,
	.refclk	= 27000000,
	.clk_div = 1,
};

static struct ngene_info ngene_info_cineS2 = {
	.type		= NGENE_SIDEWINDER,
	.name		= "Linux4Media cineS2 DVB-S2 Twin Tuner",
	.io_type	= {NGENE_IO_TSIN, NGENE_IO_TSIN},
	.demod_attach	= {demod_attach_stv0900, demod_attach_stv0900},
	.tuner_attach	= {tuner_attach_stv6110, tuner_attach_stv6110},
	.fe_config	= {&fe_cineS2, &fe_cineS2},
	.tuner_config	= {&tuner_cineS2_0, &tuner_cineS2_1},
	.lnb		= {0x0b, 0x08},
	.tsf		= {3, 3},
	.fw_version	= 15,
};

static struct ngene_info ngene_info_satixS2 = {
	.type		= NGENE_SIDEWINDER,
	.name		= "Mystique SaTiX-S2 Dual",
	.io_type	= {NGENE_IO_TSIN, NGENE_IO_TSIN},
	.demod_attach	= {demod_attach_stv0900, demod_attach_stv0900},
	.tuner_attach	= {tuner_attach_stv6110, tuner_attach_stv6110},
	.fe_config	= {&fe_cineS2, &fe_cineS2},
	.tuner_config	= {&tuner_cineS2_0, &tuner_cineS2_1},
	.lnb		= {0x0b, 0x08},
	.tsf		= {3, 3},
	.fw_version	= 15,
};

static struct ngene_info ngene_info_satixS2v2 = {
	.type		= NGENE_SIDEWINDER,
	.name		= "Mystique SaTiX-S2 Dual (v2)",
	.io_type	= {NGENE_IO_TSIN, NGENE_IO_TSIN},
	.demod_attach	= {demod_attach_stv0900, demod_attach_stv0900},
	.tuner_attach	= {tuner_attach_stv6110, tuner_attach_stv6110},
	.fe_config	= {&fe_cineS2, &fe_cineS2},
	.tuner_config	= {&tuner_cineS2_0, &tuner_cineS2_1},
	.lnb		= {0x0a, 0x08},
	.tsf		= {3, 3},
	.fw_version	= 15,
};

static struct ngene_info ngene_info_cineS2v5 = {
	.type		= NGENE_SIDEWINDER,
	.name		= "Linux4Media cineS2 DVB-S2 Twin Tuner (v5)",
	.io_type	= {NGENE_IO_TSIN, NGENE_IO_TSIN},
	.demod_attach	= {demod_attach_stv0900, demod_attach_stv0900},
	.tuner_attach	= {tuner_attach_stv6110, tuner_attach_stv6110},
	.fe_config	= {&fe_cineS2, &fe_cineS2},
	.tuner_config	= {&tuner_cineS2_0, &tuner_cineS2_1},
	.lnb		= {0x0a, 0x08},
	.tsf		= {3, 3},
	.fw_version	= 15,
};

static struct ngene_info ngene_info_duoFlexS2 = {
	.type           = NGENE_SIDEWINDER,
	.name           = "Digital Devices DuoFlex S2 miniPCIe",
	.io_type        = {NGENE_IO_TSIN, NGENE_IO_TSIN},
	.demod_attach   = {demod_attach_stv0900, demod_attach_stv0900},
	.tuner_attach   = {tuner_attach_stv6110, tuner_attach_stv6110},
	.fe_config      = {&fe_cineS2, &fe_cineS2},
	.tuner_config   = {&tuner_cineS2_0, &tuner_cineS2_1},
	.lnb            = {0x0a, 0x08},
	.tsf            = {3, 3},
	.fw_version     = 15,
};

static struct ngene_info ngene_info_m780 = {
	.type           = NGENE_APP,
	.name           = "Aver M780 ATSC/QAM-B",

	/* Channel 0 is analog, which is currently unsupported */
	.io_type        = { NGENE_IO_NONE, NGENE_IO_TSIN },
	.demod_attach   = { NULL, demod_attach_lg330x },

	/* Ensure these are NULL else the frame will call them (as funcs) */
	.tuner_attach   = { 0, 0, 0, 0 },
	.fe_config      = { NULL, &aver_m780 },
	.avf            = { 0 },

	/* A custom electrical interface config for the demod to bridge */
	.tsf		= { 4, 4 },
	.fw_version	= 15,
};

/****************************************************************************/



/****************************************************************************/
/* PCI Subsystem ID *********************************************************/
/****************************************************************************/

#define NGENE_ID(_subvend, _subdev, _driverdata) { \
	.vendor = NGENE_VID, .device = NGENE_PID, \
	.subvendor = _subvend, .subdevice = _subdev, \
	.driver_data = (unsigned long) &_driverdata }

/****************************************************************************/

static const struct pci_device_id ngene_id_tbl[] __devinitdata = {
	NGENE_ID(0x18c3, 0xabc3, ngene_info_cineS2),
	NGENE_ID(0x18c3, 0xabc4, ngene_info_cineS2),
	NGENE_ID(0x18c3, 0xdb01, ngene_info_satixS2),
	NGENE_ID(0x18c3, 0xdb02, ngene_info_satixS2v2),
	NGENE_ID(0x18c3, 0xdd00, ngene_info_cineS2v5),
	NGENE_ID(0x18c3, 0xdd10, ngene_info_duoFlexS2),
	NGENE_ID(0x18c3, 0xdd20, ngene_info_duoFlexS2),
	NGENE_ID(0x1461, 0x062e, ngene_info_m780),
	{0}
};
MODULE_DEVICE_TABLE(pci, ngene_id_tbl);

/****************************************************************************/
/* Init/Exit ****************************************************************/
/****************************************************************************/

static pci_ers_result_t ngene_error_detected(struct pci_dev *dev,
					     enum pci_channel_state state)
{
	printk(KERN_ERR DEVICE_NAME ": PCI error\n");
	if (state == pci_channel_io_perm_failure)
		return PCI_ERS_RESULT_DISCONNECT;
	if (state == pci_channel_io_frozen)
		return PCI_ERS_RESULT_NEED_RESET;
	return PCI_ERS_RESULT_CAN_RECOVER;
}

static pci_ers_result_t ngene_link_reset(struct pci_dev *dev)
{
	printk(KERN_INFO DEVICE_NAME ": link reset\n");
	return 0;
}

static pci_ers_result_t ngene_slot_reset(struct pci_dev *dev)
{
	printk(KERN_INFO DEVICE_NAME ": slot reset\n");
	return 0;
}

static void ngene_resume(struct pci_dev *dev)
{
	printk(KERN_INFO DEVICE_NAME ": resume\n");
}

static struct pci_error_handlers ngene_errors = {
	.error_detected = ngene_error_detected,
	.link_reset = ngene_link_reset,
	.slot_reset = ngene_slot_reset,
	.resume = ngene_resume,
};

static struct pci_driver ngene_pci_driver = {
	.name        = "ngene",
	.id_table    = ngene_id_tbl,
	.probe       = ngene_probe,
	.remove      = __devexit_p(ngene_remove),
	.err_handler = &ngene_errors,
};

static __init int module_init_ngene(void)
{
	printk(KERN_INFO
	       "nGene PCIE bridge driver, Copyright (C) 2005-2007 Micronas\n");
	return pci_register_driver(&ngene_pci_driver);
}

static __exit void module_exit_ngene(void)
{
	pci_unregister_driver(&ngene_pci_driver);
}

module_init(module_init_ngene);
module_exit(module_exit_ngene);

MODULE_DESCRIPTION("nGene");
MODULE_AUTHOR("Micronas, Ralph Metzler, Manfred Voelkel");
MODULE_LICENSE("GPL");
