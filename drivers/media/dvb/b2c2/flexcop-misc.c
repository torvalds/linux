/*
 * This file is part of linux driver the digital TV devices equipped with B2C2 FlexcopII(b)/III
 *
 * flexcop-misc.c - miscellaneous functions.
 *
 * see flexcop.c for copyright information.
 */
#include "flexcop.h"

void flexcop_determine_revision(struct flexcop_device *fc)
{
	flexcop_ibi_value v = fc->read_ibi_reg(fc,misc_204);

	switch (v.misc_204.Rev_N_sig_revision_hi) {
		case 0x2:
			deb_info("found a FlexCopII.\n");
			fc->rev = FLEXCOP_II;
			break;
		case 0x3:
			deb_info("found a FlexCopIIb.\n");
			fc->rev = FLEXCOP_IIB;
			break;
		case 0x0:
			deb_info("found a FlexCopIII.\n");
			fc->rev = FLEXCOP_III;
			break;
		default:
			err("unkown FlexCop Revision: %x. Please report the linux-dvb@linuxtv.org.",v.misc_204.Rev_N_sig_revision_hi);
			break;
	}

	if ((fc->has_32_hw_pid_filter = v.misc_204.Rev_N_sig_caps))
		deb_info("this FlexCop has the additional 32 hardware pid filter.\n");
	else
		deb_info("this FlexCop has only the 6 basic main hardware pid filter.\n");
	/* bus parts have to decide if hw pid filtering is used or not. */
}

const char *flexcop_revision_names[] = {
	"Unkown chip",
	"FlexCopII",
	"FlexCopIIb",
	"FlexCopIII",
};

const char *flexcop_device_names[] = {
	"Unkown device",
	"AirStar 2 DVB-T",
	"AirStar 2 ATSC",
	"SkyStar 2 DVB-S",
	"SkyStar 2 DVB-S (old version)",
	"CableStar 2 DVB-C",
};

const char *flexcop_bus_names[] = {
	"USB",
	"PCI",
};

void flexcop_device_name(struct flexcop_device *fc,const char *prefix,const
		char *suffix)
{
	info("%s '%s' at the '%s' bus controlled by a '%s' %s",prefix,
			flexcop_device_names[fc->dev_type],flexcop_bus_names[fc->bus_type],
			flexcop_revision_names[fc->rev],suffix);
}
