/***********************license start***************
 * Author: Cavium Networks
 *
 * Contact: support@caviumnetworks.com
 * This file is part of the OCTEON SDK
 *
 * Copyright (c) 2003-2008 Cavium Networks
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, Version 2, as
 * published by the Free Software Foundation.
 *
 * This file is distributed in the hope that it will be useful, but
 * AS-IS and WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE, TITLE, or
 * NONINFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this file; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 * or visit http://www.gnu.org/licenses/.
 *
 * This file may also be available under a different license from Cavium.
 * Contact Cavium Networks for more information
 ***********************license end**************************************/

/*
 * File defining functions for working with different Octeon
 * models.
 */
#include <asm/octeon/octeon.h>

/**
 * Given the chip processor ID from COP0, this function returns a
 * string representing the chip model number. The string is of the
 * form CNXXXXpX.X-FREQ-SUFFIX.
 * - XXXX = The chip model number
 * - X.X = Chip pass number
 * - FREQ = Current frequency in Mhz
 * - SUFFIX = NSP, EXP, SCP, SSP, or CP
 *
 * @chip_id: Chip ID
 *
 * Returns Model string
 */
const char *octeon_model_get_string(uint32_t chip_id)
{
	static char buffer[32];
	return octeon_model_get_string_buffer(chip_id, buffer);
}

/*
 * Version of octeon_model_get_string() that takes buffer as argument,
 * as running early in u-boot static/global variables don't work when
 * running from flash.
 */
const char *octeon_model_get_string_buffer(uint32_t chip_id, char *buffer)
{
	const char *family;
	const char *core_model;
	char pass[4];
	int clock_mhz;
	const char *suffix;
	union cvmx_l2d_fus3 fus3;
	int num_cores;
	union cvmx_mio_fus_dat2 fus_dat2;
	union cvmx_mio_fus_dat3 fus_dat3;
	char fuse_model[10];
	uint32_t fuse_data = 0;

	fus3.u64 = cvmx_read_csr(CVMX_L2D_FUS3);
	fus_dat2.u64 = cvmx_read_csr(CVMX_MIO_FUS_DAT2);
	fus_dat3.u64 = cvmx_read_csr(CVMX_MIO_FUS_DAT3);

	num_cores = cvmx_octeon_num_cores();

	/* Make sure the non existent devices look disabled */
	switch ((chip_id >> 8) & 0xff) {
	case 6:		/* CN50XX */
	case 2:		/* CN30XX */
		fus_dat3.s.nodfa_dte = 1;
		fus_dat3.s.nozip = 1;
		break;
	case 4:		/* CN57XX or CN56XX */
		fus_dat3.s.nodfa_dte = 1;
		break;
	default:
		break;
	}

	/* Make a guess at the suffix */
	/* NSP = everything */
	/* EXP = No crypto */
	/* SCP = No DFA, No zip */
	/* CP = No DFA, No crypto, No zip */
	if (fus_dat3.s.nodfa_dte) {
		if (fus_dat2.s.nocrypto)
			suffix = "CP";
		else
			suffix = "SCP";
	} else if (fus_dat2.s.nocrypto)
		suffix = "EXP";
	else
		suffix = "NSP";

	/*
	 * Assume pass number is encoded using <5:3><2:0>. Exceptions
	 * will be fixed later.
	 */
	sprintf(pass, "%u.%u", ((chip_id >> 3) & 7) + 1, chip_id & 7);

	/*
	 * Use the number of cores to determine the last 2 digits of
	 * the model number. There are some exceptions that are fixed
	 * later.
	 */
	switch (num_cores) {
	case 16:
		core_model = "60";
		break;
	case 15:
		core_model = "58";
		break;
	case 14:
		core_model = "55";
		break;
	case 13:
		core_model = "52";
		break;
	case 12:
		core_model = "50";
		break;
	case 11:
		core_model = "48";
		break;
	case 10:
		core_model = "45";
		break;
	case 9:
		core_model = "42";
		break;
	case 8:
		core_model = "40";
		break;
	case 7:
		core_model = "38";
		break;
	case 6:
		core_model = "34";
		break;
	case 5:
		core_model = "32";
		break;
	case 4:
		core_model = "30";
		break;
	case 3:
		core_model = "25";
		break;
	case 2:
		core_model = "20";
		break;
	case 1:
		core_model = "10";
		break;
	default:
		core_model = "XX";
		break;
	}

	/* Now figure out the family, the first two digits */
	switch ((chip_id >> 8) & 0xff) {
	case 0:		/* CN38XX, CN37XX or CN36XX */
		if (fus3.cn38xx.crip_512k) {
			/*
			 * For some unknown reason, the 16 core one is
			 * called 37 instead of 36.
			 */
			if (num_cores >= 16)
				family = "37";
			else
				family = "36";
		} else
			family = "38";
		/*
		 * This series of chips didn't follow the standard
		 * pass numbering.
		 */
		switch (chip_id & 0xf) {
		case 0:
			strcpy(pass, "1.X");
			break;
		case 1:
			strcpy(pass, "2.X");
			break;
		case 3:
			strcpy(pass, "3.X");
			break;
		default:
			strcpy(pass, "X.X");
			break;
		}
		break;
	case 1:		/* CN31XX or CN3020 */
		if ((chip_id & 0x10) || fus3.cn31xx.crip_128k)
			family = "30";
		else
			family = "31";
		/*
		 * This series of chips didn't follow the standard
		 * pass numbering.
		 */
		switch (chip_id & 0xf) {
		case 0:
			strcpy(pass, "1.0");
			break;
		case 2:
			strcpy(pass, "1.1");
			break;
		default:
			strcpy(pass, "X.X");
			break;
		}
		break;
	case 2:		/* CN3010 or CN3005 */
		family = "30";
		/* A chip with half cache is an 05 */
		if (fus3.cn30xx.crip_64k)
			core_model = "05";
		/*
		 * This series of chips didn't follow the standard
		 * pass numbering.
		 */
		switch (chip_id & 0xf) {
		case 0:
			strcpy(pass, "1.0");
			break;
		case 2:
			strcpy(pass, "1.1");
			break;
		default:
			strcpy(pass, "X.X");
			break;
		}
		break;
	case 3:		/* CN58XX */
		family = "58";
		/* Special case. 4 core, no crypto */
		if ((num_cores == 4) && fus_dat2.cn38xx.nocrypto)
			core_model = "29";

		/* Pass 1 uses different encodings for pass numbers */
		if ((chip_id & 0xFF) < 0x8) {
			switch (chip_id & 0x3) {
			case 0:
				strcpy(pass, "1.0");
				break;
			case 1:
				strcpy(pass, "1.1");
				break;
			case 3:
				strcpy(pass, "1.2");
				break;
			default:
				strcpy(pass, "1.X");
				break;
			}
		}
		break;
	case 4:		/* CN57XX, CN56XX, CN55XX, CN54XX */
		if (fus_dat2.cn56xx.raid_en) {
			if (fus3.cn56xx.crip_1024k)
				family = "55";
			else
				family = "57";
			if (fus_dat2.cn56xx.nocrypto)
				suffix = "SP";
			else
				suffix = "SSP";
		} else {
			if (fus_dat2.cn56xx.nocrypto)
				suffix = "CP";
			else {
				suffix = "NSP";
				if (fus_dat3.s.nozip)
					suffix = "SCP";
			}
			if (fus3.cn56xx.crip_1024k)
				family = "54";
			else
				family = "56";
		}
		break;
	case 6:		/* CN50XX */
		family = "50";
		break;
	case 7:		/* CN52XX */
		if (fus3.cn52xx.crip_256k)
			family = "51";
		else
			family = "52";
		break;
	default:
		family = "XX";
		core_model = "XX";
		strcpy(pass, "X.X");
		suffix = "XXX";
		break;
	}

	clock_mhz = octeon_get_clock_rate() / 1000000;

	if (family[0] != '3') {
		/* Check for model in fuses, overrides normal decode */
		/* This is _not_ valid for Octeon CN3XXX models */
		fuse_data |= cvmx_fuse_read_byte(51);
		fuse_data = fuse_data << 8;
		fuse_data |= cvmx_fuse_read_byte(50);
		fuse_data = fuse_data << 8;
		fuse_data |= cvmx_fuse_read_byte(49);
		fuse_data = fuse_data << 8;
		fuse_data |= cvmx_fuse_read_byte(48);
		if (fuse_data & 0x7ffff) {
			int model = fuse_data & 0x3fff;
			int suffix = (fuse_data >> 14) & 0x1f;
			if (suffix && model) {
				/*
				 * Have both number and suffix in
				 * fuses, so both
				 */
				sprintf(fuse_model, "%d%c",
					model, 'A' + suffix - 1);
				core_model = "";
				family = fuse_model;
			} else if (suffix && !model) {
				/*
				 * Only have suffix, so add suffix to
				 * 'normal' model number.
				 */
				sprintf(fuse_model, "%s%c", core_model,
					'A' + suffix - 1);
				core_model = fuse_model;
			} else {
				/*
				 * Don't have suffix, so just use
				 * model from fuses.
				 */
				sprintf(fuse_model, "%d", model);
				core_model = "";
				family = fuse_model;
			}
		}
	}
	sprintf(buffer, "CN%s%sp%s-%d-%s",
		family, core_model, pass, clock_mhz, suffix);
	return buffer;
}
