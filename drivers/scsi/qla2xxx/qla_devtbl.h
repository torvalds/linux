#define QLA_MODEL_NAMES         0x4A

/*
 * Adapter model names and descriptions.
 */
static char *qla2x00_model_name[QLA_MODEL_NAMES*2] = {
	"QLA2340",	"133MHz PCI-X to 2Gb FC, Single Channel",	/* 0x100 */
	"QLA2342",	"133MHz PCI-X to 2Gb FC, Dual Channel",		/* 0x101 */
	"QLA2344",	"133MHz PCI-X to 2Gb FC, Quad Channel",		/* 0x102 */
	"QCP2342",	"cPCI to 2Gb FC, Dual Channel",			/* 0x103 */
	"QSB2340",	"SBUS to 2Gb FC, Single Channel",		/* 0x104 */
	"QSB2342",	"SBUS to 2Gb FC, Dual Channel",			/* 0x105 */
	"QLA2310",	"Sun 66MHz PCI-X to 2Gb FC, Single Channel",	/* 0x106 */
	"QLA2332",	"Sun 66MHz PCI-X to 2Gb FC, Single Channel",	/* 0x107 */
	"QCP2332",	"Sun cPCI to 2Gb FC, Dual Channel",		/* 0x108 */
	"QCP2340",	"cPCI to 2Gb FC, Single Channel",		/* 0x109 */
	"QLA2342",	"Sun 133MHz PCI-X to 2Gb FC, Dual Channel",	/* 0x10a */
	"QCP2342",	"Sun - cPCI to 2Gb FC, Dual Channel",		/* 0x10b */
	"QLA2350",	"133MHz PCI-X to 2Gb FC, Single Channel",	/* 0x10c */
	"QLA2352",	"133MHz PCI-X to 2Gb FC, Dual Channel",		/* 0x10d */
	"QLA2352",	"Sun 133MHz PCI-X to 2Gb FC, Dual Channel",	/* 0x10e */
	" ",		" ",						/* 0x10f */
	" ",		" ",						/* 0x110 */
	" ",		" ",						/* 0x111 */
	" ",		" ",						/* 0x112 */
	" ",		" ",						/* 0x113 */
	" ",		" ",						/* 0x114 */
	"QLA2360",	"133MHz PCI-X to 2Gb FC, Single Channel",	/* 0x115 */
	"QLA2362",	"133MHz PCI-X to 2Gb FC, Dual Channel",		/* 0x116 */
	"QLE2360",	"PCI-Express to 2Gb FC, Single Channel",	/* 0x117 */
	"QLE2362",	"PCI-Express to 2Gb FC, Dual Channel",		/* 0x118 */
	"QLA200",	"133MHz PCI-X to 2Gb FC Optical",		/* 0x119 */
	" ",		" ",						/* 0x11a */
	" ",		" ",						/* 0x11b */
	"QLA200P",	"133MHz PCI-X to 2Gb FC SFP",			/* 0x11c */
	" ",		" ",						/* 0x11d */
	" ",		" ",						/* 0x11e */
	" ",		" ",						/* 0x11f */
	" ",		" ",						/* 0x120 */
	" ",		" ",						/* 0x121 */
	" ",		" ",						/* 0x122 */
	" ",		" ",						/* 0x123 */
	" ",		" ",						/* 0x124 */
	" ",		" ",						/* 0x125 */
	" ",		" ",						/* 0x126 */
	" ",		" ",						/* 0x127 */
	" ",		" ",						/* 0x128 */
	" ",		" ",						/* 0x129 */
	" ",		" ",						/* 0x12a */
	" ",		" ",						/* 0x12b */
	" ",		" ",						/* 0x12c */
	" ",		" ",						/* 0x12d */
	" ",		" ",						/* 0x12e */
	"QLA210",	"133MHz PCI-X to 2Gb FC, Single Channel",	/* 0x12f */
	"EMC 250",	"133MHz PCI-X to 2Gb FC, Single Channel",	/* 0x130 */
	"HP A7538A",	"HP 1p2g PCI-X to 2Gb FC, Single Channel",	/* 0x131 */
	"QLA210",	"Sun 133MHz PCI-X to 2Gb FC, Single Channel",	/* 0x132 */
	"QLA2460",	"PCI-X 2.0 to 4Gb FC, Single Channel",		/* 0x133 */
	"QLA2462",	"PCI-X 2.0 to 4Gb FC, Dual Channel",		/* 0x134 */
	"QMC2462",	"IBM eServer BC 4Gb FC Expansion Card",		/* 0x135 */
	"QMC2462S",	"IBM eServer BC 4Gb FC Expansion Card SFF",	/* 0x136 */
	"QLE2460",	"PCI-Express to 4Gb FC, Single Channel",	/* 0x137 */
	"QLE2462",	"PCI-Express to 4Gb FC, Dual Channel",		/* 0x138 */
	"QME2462",	"Dell BS PCI-Express to 4Gb FC, Dual Channel",	/* 0x139 */
	" ",		" ",						/* 0x13a */
	" ",		" ",						/* 0x13b */
	" ",		" ",						/* 0x13c */
	"QEM2462",	"Sun Server I/O Module 4Gb FC, Dual Channel",	/* 0x13d */
	"QLE210",	"PCI-Express to 2Gb FC, Single Channel",	/* 0x13e */
	"QLE220",	"PCI-Express to 4Gb FC, Single Channel",	/* 0x13f */
	"QLA2460",	"Sun PCI-X 2.0 to 4Gb FC, Single Channel",	/* 0x140 */
	"QLA2462",	"Sun PCI-X 2.0 to 4Gb FC, Dual Channel",	/* 0x141 */
	"QLE2460",	"Sun PCI-Express to 2Gb FC, Single Channel",	/* 0x142 */
	"QLE2462",	"Sun PCI-Express to 4Gb FC, Single Channel",	/* 0x143 */
	"QEM2462"	"Server I/O Module 4Gb FC, Dual Channel",	/* 0x144 */
	"QLE2440",	"PCI-Express to 4Gb FC, Single Channel",	/* 0x145 */
	"QLE2464",	"PCI-Express to 4Gb FC, Quad Channel",		/* 0x146 */
	"QLA2440",	"PCI-X 2.0 to 4Gb FC, Single Channel",		/* 0x147 */
	" ",		" ",						/* 0x148 */
	"QLA2340",	"Sun 133MHz PCI-X to 2Gb FC, Single Channel",	/* 0x149 */
};
