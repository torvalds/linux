/*
 * Linux driver for digital TV devices equipped with B2C2 FlexcopII(b)/III
 * flexcop-misc.c - miscellaneous functions
 * see flexcop.c for copyright information
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
		err("unknown FlexCop Revision: %x. Please report this to "
				"linux-dvb@linuxtv.org.",
				v.misc_204.Rev_N_sig_revision_hi);
		break;
	}

	if ((fc->has_32_hw_pid_filter = v.misc_204.Rev_N_sig_caps))
		deb_info("this FlexCop has "
				"the additional 32 hardware pid filter.\n");
	else
		deb_info("this FlexCop has "
				"the 6 basic main hardware pid filter.\n");
	/* bus parts have to decide if hw pid filtering is used or not. */
}

static const char *flexcop_revision_names[] = {
	"Unknown chip",
	"FlexCopII",
	"FlexCopIIb",
	"FlexCopIII",
};

static const char *flexcop_device_names[] = {
	[FC_UNK]	= "Unknown device",
	[FC_CABLE]	= "Cable2PC/CableStar 2 DVB-C",
	[FC_AIR_DVBT]	= "Air2PC/AirStar 2 DVB-T",
	[FC_AIR_ATSC1]	= "Air2PC/AirStar 2 ATSC 1st generation",
	[FC_AIR_ATSC2]	= "Air2PC/AirStar 2 ATSC 2nd generation",
	[FC_AIR_ATSC3]	= "Air2PC/AirStar 2 ATSC 3rd generation (HD5000)",
	[FC_SKY_REV23]	= "Sky2PC/SkyStar 2 DVB-S rev 2.3 (old version)",
	[FC_SKY_REV26]	= "Sky2PC/SkyStar 2 DVB-S rev 2.6",
	[FC_SKY_REV27]	= "Sky2PC/SkyStar 2 DVB-S rev 2.7a/u",
	[FC_SKY_REV28]	= "Sky2PC/SkyStar 2 DVB-S rev 2.8",
	[FC_SKYS2_REV33] = "Sky2PC/SkyStar S2 DVB-S/S2 rev 3.3",
};

static const char *flexcop_bus_names[] = {
	"USB",
	"PCI",
};

void flexcop_device_name(struct flexcop_device *fc,
		const char *prefix, const char *suffix)
{
	info("%s '%s' at the '%s' bus controlled by a '%s' %s",
			prefix,	flexcop_device_names[fc->dev_type],
			flexcop_bus_names[fc->bus_type],
			flexcop_revision_names[fc->rev], suffix);
}

void flexcop_dump_reg(struct flexcop_device *fc,
		flexcop_ibi_register reg, int num)
{
	flexcop_ibi_value v;
	int i;
	for (i = 0; i < num; i++) {
		v = fc->read_ibi_reg(fc, reg+4*i);
		deb_rdump("0x%03x: %08x, ", reg+4*i, v.raw);
	}
	deb_rdump("\n");
}
EXPORT_SYMBOL(flexcop_dump_reg);
