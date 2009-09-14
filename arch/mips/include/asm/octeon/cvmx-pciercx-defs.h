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

#ifndef __CVMX_PCIERCX_DEFS_H__
#define __CVMX_PCIERCX_DEFS_H__

#define CVMX_PCIERCX_CFG000(offset) \
	 (0x0000000000000000ull + (((offset) & 1) * 0))
#define CVMX_PCIERCX_CFG001(offset) \
	 (0x0000000000000004ull + (((offset) & 1) * 0))
#define CVMX_PCIERCX_CFG002(offset) \
	 (0x0000000000000008ull + (((offset) & 1) * 0))
#define CVMX_PCIERCX_CFG003(offset) \
	 (0x000000000000000Cull + (((offset) & 1) * 0))
#define CVMX_PCIERCX_CFG004(offset) \
	 (0x0000000000000010ull + (((offset) & 1) * 0))
#define CVMX_PCIERCX_CFG005(offset) \
	 (0x0000000000000014ull + (((offset) & 1) * 0))
#define CVMX_PCIERCX_CFG006(offset) \
	 (0x0000000000000018ull + (((offset) & 1) * 0))
#define CVMX_PCIERCX_CFG007(offset) \
	 (0x000000000000001Cull + (((offset) & 1) * 0))
#define CVMX_PCIERCX_CFG008(offset) \
	 (0x0000000000000020ull + (((offset) & 1) * 0))
#define CVMX_PCIERCX_CFG009(offset) \
	 (0x0000000000000024ull + (((offset) & 1) * 0))
#define CVMX_PCIERCX_CFG010(offset) \
	 (0x0000000000000028ull + (((offset) & 1) * 0))
#define CVMX_PCIERCX_CFG011(offset) \
	 (0x000000000000002Cull + (((offset) & 1) * 0))
#define CVMX_PCIERCX_CFG012(offset) \
	 (0x0000000000000030ull + (((offset) & 1) * 0))
#define CVMX_PCIERCX_CFG013(offset) \
	 (0x0000000000000034ull + (((offset) & 1) * 0))
#define CVMX_PCIERCX_CFG014(offset) \
	 (0x0000000000000038ull + (((offset) & 1) * 0))
#define CVMX_PCIERCX_CFG015(offset) \
	 (0x000000000000003Cull + (((offset) & 1) * 0))
#define CVMX_PCIERCX_CFG016(offset) \
	 (0x0000000000000040ull + (((offset) & 1) * 0))
#define CVMX_PCIERCX_CFG017(offset) \
	 (0x0000000000000044ull + (((offset) & 1) * 0))
#define CVMX_PCIERCX_CFG020(offset) \
	 (0x0000000000000050ull + (((offset) & 1) * 0))
#define CVMX_PCIERCX_CFG021(offset) \
	 (0x0000000000000054ull + (((offset) & 1) * 0))
#define CVMX_PCIERCX_CFG022(offset) \
	 (0x0000000000000058ull + (((offset) & 1) * 0))
#define CVMX_PCIERCX_CFG023(offset) \
	 (0x000000000000005Cull + (((offset) & 1) * 0))
#define CVMX_PCIERCX_CFG028(offset) \
	 (0x0000000000000070ull + (((offset) & 1) * 0))
#define CVMX_PCIERCX_CFG029(offset) \
	 (0x0000000000000074ull + (((offset) & 1) * 0))
#define CVMX_PCIERCX_CFG030(offset) \
	 (0x0000000000000078ull + (((offset) & 1) * 0))
#define CVMX_PCIERCX_CFG031(offset) \
	 (0x000000000000007Cull + (((offset) & 1) * 0))
#define CVMX_PCIERCX_CFG032(offset) \
	 (0x0000000000000080ull + (((offset) & 1) * 0))
#define CVMX_PCIERCX_CFG033(offset) \
	 (0x0000000000000084ull + (((offset) & 1) * 0))
#define CVMX_PCIERCX_CFG034(offset) \
	 (0x0000000000000088ull + (((offset) & 1) * 0))
#define CVMX_PCIERCX_CFG035(offset) \
	 (0x000000000000008Cull + (((offset) & 1) * 0))
#define CVMX_PCIERCX_CFG036(offset) \
	 (0x0000000000000090ull + (((offset) & 1) * 0))
#define CVMX_PCIERCX_CFG037(offset) \
	 (0x0000000000000094ull + (((offset) & 1) * 0))
#define CVMX_PCIERCX_CFG038(offset) \
	 (0x0000000000000098ull + (((offset) & 1) * 0))
#define CVMX_PCIERCX_CFG039(offset) \
	 (0x000000000000009Cull + (((offset) & 1) * 0))
#define CVMX_PCIERCX_CFG040(offset) \
	 (0x00000000000000A0ull + (((offset) & 1) * 0))
#define CVMX_PCIERCX_CFG041(offset) \
	 (0x00000000000000A4ull + (((offset) & 1) * 0))
#define CVMX_PCIERCX_CFG042(offset) \
	 (0x00000000000000A8ull + (((offset) & 1) * 0))
#define CVMX_PCIERCX_CFG064(offset) \
	 (0x0000000000000100ull + (((offset) & 1) * 0))
#define CVMX_PCIERCX_CFG065(offset) \
	 (0x0000000000000104ull + (((offset) & 1) * 0))
#define CVMX_PCIERCX_CFG066(offset) \
	 (0x0000000000000108ull + (((offset) & 1) * 0))
#define CVMX_PCIERCX_CFG067(offset) \
	 (0x000000000000010Cull + (((offset) & 1) * 0))
#define CVMX_PCIERCX_CFG068(offset) \
	 (0x0000000000000110ull + (((offset) & 1) * 0))
#define CVMX_PCIERCX_CFG069(offset) \
	 (0x0000000000000114ull + (((offset) & 1) * 0))
#define CVMX_PCIERCX_CFG070(offset) \
	 (0x0000000000000118ull + (((offset) & 1) * 0))
#define CVMX_PCIERCX_CFG071(offset) \
	 (0x000000000000011Cull + (((offset) & 1) * 0))
#define CVMX_PCIERCX_CFG072(offset) \
	 (0x0000000000000120ull + (((offset) & 1) * 0))
#define CVMX_PCIERCX_CFG073(offset) \
	 (0x0000000000000124ull + (((offset) & 1) * 0))
#define CVMX_PCIERCX_CFG074(offset) \
	 (0x0000000000000128ull + (((offset) & 1) * 0))
#define CVMX_PCIERCX_CFG075(offset) \
	 (0x000000000000012Cull + (((offset) & 1) * 0))
#define CVMX_PCIERCX_CFG076(offset) \
	 (0x0000000000000130ull + (((offset) & 1) * 0))
#define CVMX_PCIERCX_CFG077(offset) \
	 (0x0000000000000134ull + (((offset) & 1) * 0))
#define CVMX_PCIERCX_CFG448(offset) \
	 (0x0000000000000700ull + (((offset) & 1) * 0))
#define CVMX_PCIERCX_CFG449(offset) \
	 (0x0000000000000704ull + (((offset) & 1) * 0))
#define CVMX_PCIERCX_CFG450(offset) \
	 (0x0000000000000708ull + (((offset) & 1) * 0))
#define CVMX_PCIERCX_CFG451(offset) \
	 (0x000000000000070Cull + (((offset) & 1) * 0))
#define CVMX_PCIERCX_CFG452(offset) \
	 (0x0000000000000710ull + (((offset) & 1) * 0))
#define CVMX_PCIERCX_CFG453(offset) \
	 (0x0000000000000714ull + (((offset) & 1) * 0))
#define CVMX_PCIERCX_CFG454(offset) \
	 (0x0000000000000718ull + (((offset) & 1) * 0))
#define CVMX_PCIERCX_CFG455(offset) \
	 (0x000000000000071Cull + (((offset) & 1) * 0))
#define CVMX_PCIERCX_CFG456(offset) \
	 (0x0000000000000720ull + (((offset) & 1) * 0))
#define CVMX_PCIERCX_CFG458(offset) \
	 (0x0000000000000728ull + (((offset) & 1) * 0))
#define CVMX_PCIERCX_CFG459(offset) \
	 (0x000000000000072Cull + (((offset) & 1) * 0))
#define CVMX_PCIERCX_CFG460(offset) \
	 (0x0000000000000730ull + (((offset) & 1) * 0))
#define CVMX_PCIERCX_CFG461(offset) \
	 (0x0000000000000734ull + (((offset) & 1) * 0))
#define CVMX_PCIERCX_CFG462(offset) \
	 (0x0000000000000738ull + (((offset) & 1) * 0))
#define CVMX_PCIERCX_CFG463(offset) \
	 (0x000000000000073Cull + (((offset) & 1) * 0))
#define CVMX_PCIERCX_CFG464(offset) \
	 (0x0000000000000740ull + (((offset) & 1) * 0))
#define CVMX_PCIERCX_CFG465(offset) \
	 (0x0000000000000744ull + (((offset) & 1) * 0))
#define CVMX_PCIERCX_CFG466(offset) \
	 (0x0000000000000748ull + (((offset) & 1) * 0))
#define CVMX_PCIERCX_CFG467(offset) \
	 (0x000000000000074Cull + (((offset) & 1) * 0))
#define CVMX_PCIERCX_CFG468(offset) \
	 (0x0000000000000750ull + (((offset) & 1) * 0))
#define CVMX_PCIERCX_CFG490(offset) \
	 (0x00000000000007A8ull + (((offset) & 1) * 0))
#define CVMX_PCIERCX_CFG491(offset) \
	 (0x00000000000007ACull + (((offset) & 1) * 0))
#define CVMX_PCIERCX_CFG492(offset) \
	 (0x00000000000007B0ull + (((offset) & 1) * 0))
#define CVMX_PCIERCX_CFG516(offset) \
	 (0x0000000000000810ull + (((offset) & 1) * 0))
#define CVMX_PCIERCX_CFG517(offset) \
	 (0x0000000000000814ull + (((offset) & 1) * 0))

union cvmx_pciercx_cfg000 {
	uint32_t u32;
	struct cvmx_pciercx_cfg000_s {
		uint32_t devid:16;
		uint32_t vendid:16;
	} s;
	struct cvmx_pciercx_cfg000_s cn52xx;
	struct cvmx_pciercx_cfg000_s cn52xxp1;
	struct cvmx_pciercx_cfg000_s cn56xx;
	struct cvmx_pciercx_cfg000_s cn56xxp1;
};

union cvmx_pciercx_cfg001 {
	uint32_t u32;
	struct cvmx_pciercx_cfg001_s {
		uint32_t dpe:1;
		uint32_t sse:1;
		uint32_t rma:1;
		uint32_t rta:1;
		uint32_t sta:1;
		uint32_t devt:2;
		uint32_t mdpe:1;
		uint32_t fbb:1;
		uint32_t reserved_22_22:1;
		uint32_t m66:1;
		uint32_t cl:1;
		uint32_t i_stat:1;
		uint32_t reserved_11_18:8;
		uint32_t i_dis:1;
		uint32_t fbbe:1;
		uint32_t see:1;
		uint32_t ids_wcc:1;
		uint32_t per:1;
		uint32_t vps:1;
		uint32_t mwice:1;
		uint32_t scse:1;
		uint32_t me:1;
		uint32_t msae:1;
		uint32_t isae:1;
	} s;
	struct cvmx_pciercx_cfg001_s cn52xx;
	struct cvmx_pciercx_cfg001_s cn52xxp1;
	struct cvmx_pciercx_cfg001_s cn56xx;
	struct cvmx_pciercx_cfg001_s cn56xxp1;
};

union cvmx_pciercx_cfg002 {
	uint32_t u32;
	struct cvmx_pciercx_cfg002_s {
		uint32_t bcc:8;
		uint32_t sc:8;
		uint32_t pi:8;
		uint32_t rid:8;
	} s;
	struct cvmx_pciercx_cfg002_s cn52xx;
	struct cvmx_pciercx_cfg002_s cn52xxp1;
	struct cvmx_pciercx_cfg002_s cn56xx;
	struct cvmx_pciercx_cfg002_s cn56xxp1;
};

union cvmx_pciercx_cfg003 {
	uint32_t u32;
	struct cvmx_pciercx_cfg003_s {
		uint32_t bist:8;
		uint32_t mfd:1;
		uint32_t chf:7;
		uint32_t lt:8;
		uint32_t cls:8;
	} s;
	struct cvmx_pciercx_cfg003_s cn52xx;
	struct cvmx_pciercx_cfg003_s cn52xxp1;
	struct cvmx_pciercx_cfg003_s cn56xx;
	struct cvmx_pciercx_cfg003_s cn56xxp1;
};

union cvmx_pciercx_cfg004 {
	uint32_t u32;
	struct cvmx_pciercx_cfg004_s {
		uint32_t reserved_0_31:32;
	} s;
	struct cvmx_pciercx_cfg004_s cn52xx;
	struct cvmx_pciercx_cfg004_s cn52xxp1;
	struct cvmx_pciercx_cfg004_s cn56xx;
	struct cvmx_pciercx_cfg004_s cn56xxp1;
};

union cvmx_pciercx_cfg005 {
	uint32_t u32;
	struct cvmx_pciercx_cfg005_s {
		uint32_t reserved_0_31:32;
	} s;
	struct cvmx_pciercx_cfg005_s cn52xx;
	struct cvmx_pciercx_cfg005_s cn52xxp1;
	struct cvmx_pciercx_cfg005_s cn56xx;
	struct cvmx_pciercx_cfg005_s cn56xxp1;
};

union cvmx_pciercx_cfg006 {
	uint32_t u32;
	struct cvmx_pciercx_cfg006_s {
		uint32_t slt:8;
		uint32_t subbnum:8;
		uint32_t sbnum:8;
		uint32_t pbnum:8;
	} s;
	struct cvmx_pciercx_cfg006_s cn52xx;
	struct cvmx_pciercx_cfg006_s cn52xxp1;
	struct cvmx_pciercx_cfg006_s cn56xx;
	struct cvmx_pciercx_cfg006_s cn56xxp1;
};

union cvmx_pciercx_cfg007 {
	uint32_t u32;
	struct cvmx_pciercx_cfg007_s {
		uint32_t dpe:1;
		uint32_t sse:1;
		uint32_t rma:1;
		uint32_t rta:1;
		uint32_t sta:1;
		uint32_t devt:2;
		uint32_t mdpe:1;
		uint32_t fbb:1;
		uint32_t reserved_22_22:1;
		uint32_t m66:1;
		uint32_t reserved_16_20:5;
		uint32_t lio_limi:4;
		uint32_t reserved_9_11:3;
		uint32_t io32b:1;
		uint32_t lio_base:4;
		uint32_t reserved_1_3:3;
		uint32_t io32a:1;
	} s;
	struct cvmx_pciercx_cfg007_s cn52xx;
	struct cvmx_pciercx_cfg007_s cn52xxp1;
	struct cvmx_pciercx_cfg007_s cn56xx;
	struct cvmx_pciercx_cfg007_s cn56xxp1;
};

union cvmx_pciercx_cfg008 {
	uint32_t u32;
	struct cvmx_pciercx_cfg008_s {
		uint32_t ml_addr:12;
		uint32_t reserved_16_19:4;
		uint32_t mb_addr:12;
		uint32_t reserved_0_3:4;
	} s;
	struct cvmx_pciercx_cfg008_s cn52xx;
	struct cvmx_pciercx_cfg008_s cn52xxp1;
	struct cvmx_pciercx_cfg008_s cn56xx;
	struct cvmx_pciercx_cfg008_s cn56xxp1;
};

union cvmx_pciercx_cfg009 {
	uint32_t u32;
	struct cvmx_pciercx_cfg009_s {
		uint32_t lmem_limit:12;
		uint32_t reserved_17_19:3;
		uint32_t mem64b:1;
		uint32_t lmem_base:12;
		uint32_t reserved_1_3:3;
		uint32_t mem64a:1;
	} s;
	struct cvmx_pciercx_cfg009_s cn52xx;
	struct cvmx_pciercx_cfg009_s cn52xxp1;
	struct cvmx_pciercx_cfg009_s cn56xx;
	struct cvmx_pciercx_cfg009_s cn56xxp1;
};

union cvmx_pciercx_cfg010 {
	uint32_t u32;
	struct cvmx_pciercx_cfg010_s {
		uint32_t umem_base:32;
	} s;
	struct cvmx_pciercx_cfg010_s cn52xx;
	struct cvmx_pciercx_cfg010_s cn52xxp1;
	struct cvmx_pciercx_cfg010_s cn56xx;
	struct cvmx_pciercx_cfg010_s cn56xxp1;
};

union cvmx_pciercx_cfg011 {
	uint32_t u32;
	struct cvmx_pciercx_cfg011_s {
		uint32_t umem_limit:32;
	} s;
	struct cvmx_pciercx_cfg011_s cn52xx;
	struct cvmx_pciercx_cfg011_s cn52xxp1;
	struct cvmx_pciercx_cfg011_s cn56xx;
	struct cvmx_pciercx_cfg011_s cn56xxp1;
};

union cvmx_pciercx_cfg012 {
	uint32_t u32;
	struct cvmx_pciercx_cfg012_s {
		uint32_t uio_limit:16;
		uint32_t uio_base:16;
	} s;
	struct cvmx_pciercx_cfg012_s cn52xx;
	struct cvmx_pciercx_cfg012_s cn52xxp1;
	struct cvmx_pciercx_cfg012_s cn56xx;
	struct cvmx_pciercx_cfg012_s cn56xxp1;
};

union cvmx_pciercx_cfg013 {
	uint32_t u32;
	struct cvmx_pciercx_cfg013_s {
		uint32_t reserved_8_31:24;
		uint32_t cp:8;
	} s;
	struct cvmx_pciercx_cfg013_s cn52xx;
	struct cvmx_pciercx_cfg013_s cn52xxp1;
	struct cvmx_pciercx_cfg013_s cn56xx;
	struct cvmx_pciercx_cfg013_s cn56xxp1;
};

union cvmx_pciercx_cfg014 {
	uint32_t u32;
	struct cvmx_pciercx_cfg014_s {
		uint32_t reserved_0_31:32;
	} s;
	struct cvmx_pciercx_cfg014_s cn52xx;
	struct cvmx_pciercx_cfg014_s cn52xxp1;
	struct cvmx_pciercx_cfg014_s cn56xx;
	struct cvmx_pciercx_cfg014_s cn56xxp1;
};

union cvmx_pciercx_cfg015 {
	uint32_t u32;
	struct cvmx_pciercx_cfg015_s {
		uint32_t reserved_28_31:4;
		uint32_t dtsees:1;
		uint32_t dts:1;
		uint32_t sdt:1;
		uint32_t pdt:1;
		uint32_t fbbe:1;
		uint32_t sbrst:1;
		uint32_t mam:1;
		uint32_t vga16d:1;
		uint32_t vgae:1;
		uint32_t isae:1;
		uint32_t see:1;
		uint32_t pere:1;
		uint32_t inta:8;
		uint32_t il:8;
	} s;
	struct cvmx_pciercx_cfg015_s cn52xx;
	struct cvmx_pciercx_cfg015_s cn52xxp1;
	struct cvmx_pciercx_cfg015_s cn56xx;
	struct cvmx_pciercx_cfg015_s cn56xxp1;
};

union cvmx_pciercx_cfg016 {
	uint32_t u32;
	struct cvmx_pciercx_cfg016_s {
		uint32_t pmes:5;
		uint32_t d2s:1;
		uint32_t d1s:1;
		uint32_t auxc:3;
		uint32_t dsi:1;
		uint32_t reserved_20_20:1;
		uint32_t pme_clock:1;
		uint32_t pmsv:3;
		uint32_t ncp:8;
		uint32_t pmcid:8;
	} s;
	struct cvmx_pciercx_cfg016_s cn52xx;
	struct cvmx_pciercx_cfg016_s cn52xxp1;
	struct cvmx_pciercx_cfg016_s cn56xx;
	struct cvmx_pciercx_cfg016_s cn56xxp1;
};

union cvmx_pciercx_cfg017 {
	uint32_t u32;
	struct cvmx_pciercx_cfg017_s {
		uint32_t pmdia:8;
		uint32_t bpccee:1;
		uint32_t bd3h:1;
		uint32_t reserved_16_21:6;
		uint32_t pmess:1;
		uint32_t pmedsia:2;
		uint32_t pmds:4;
		uint32_t pmeens:1;
		uint32_t reserved_4_7:4;
		uint32_t nsr:1;
		uint32_t reserved_2_2:1;
		uint32_t ps:2;
	} s;
	struct cvmx_pciercx_cfg017_s cn52xx;
	struct cvmx_pciercx_cfg017_s cn52xxp1;
	struct cvmx_pciercx_cfg017_s cn56xx;
	struct cvmx_pciercx_cfg017_s cn56xxp1;
};

union cvmx_pciercx_cfg020 {
	uint32_t u32;
	struct cvmx_pciercx_cfg020_s {
		uint32_t reserved_24_31:8;
		uint32_t m64:1;
		uint32_t mme:3;
		uint32_t mmc:3;
		uint32_t msien:1;
		uint32_t ncp:8;
		uint32_t msicid:8;
	} s;
	struct cvmx_pciercx_cfg020_s cn52xx;
	struct cvmx_pciercx_cfg020_s cn52xxp1;
	struct cvmx_pciercx_cfg020_s cn56xx;
	struct cvmx_pciercx_cfg020_s cn56xxp1;
};

union cvmx_pciercx_cfg021 {
	uint32_t u32;
	struct cvmx_pciercx_cfg021_s {
		uint32_t lmsi:30;
		uint32_t reserved_0_1:2;
	} s;
	struct cvmx_pciercx_cfg021_s cn52xx;
	struct cvmx_pciercx_cfg021_s cn52xxp1;
	struct cvmx_pciercx_cfg021_s cn56xx;
	struct cvmx_pciercx_cfg021_s cn56xxp1;
};

union cvmx_pciercx_cfg022 {
	uint32_t u32;
	struct cvmx_pciercx_cfg022_s {
		uint32_t umsi:32;
	} s;
	struct cvmx_pciercx_cfg022_s cn52xx;
	struct cvmx_pciercx_cfg022_s cn52xxp1;
	struct cvmx_pciercx_cfg022_s cn56xx;
	struct cvmx_pciercx_cfg022_s cn56xxp1;
};

union cvmx_pciercx_cfg023 {
	uint32_t u32;
	struct cvmx_pciercx_cfg023_s {
		uint32_t reserved_16_31:16;
		uint32_t msimd:16;
	} s;
	struct cvmx_pciercx_cfg023_s cn52xx;
	struct cvmx_pciercx_cfg023_s cn52xxp1;
	struct cvmx_pciercx_cfg023_s cn56xx;
	struct cvmx_pciercx_cfg023_s cn56xxp1;
};

union cvmx_pciercx_cfg028 {
	uint32_t u32;
	struct cvmx_pciercx_cfg028_s {
		uint32_t reserved_30_31:2;
		uint32_t imn:5;
		uint32_t si:1;
		uint32_t dpt:4;
		uint32_t pciecv:4;
		uint32_t ncp:8;
		uint32_t pcieid:8;
	} s;
	struct cvmx_pciercx_cfg028_s cn52xx;
	struct cvmx_pciercx_cfg028_s cn52xxp1;
	struct cvmx_pciercx_cfg028_s cn56xx;
	struct cvmx_pciercx_cfg028_s cn56xxp1;
};

union cvmx_pciercx_cfg029 {
	uint32_t u32;
	struct cvmx_pciercx_cfg029_s {
		uint32_t reserved_28_31:4;
		uint32_t cspls:2;
		uint32_t csplv:8;
		uint32_t reserved_16_17:2;
		uint32_t rber:1;
		uint32_t reserved_12_14:3;
		uint32_t el1al:3;
		uint32_t el0al:3;
		uint32_t etfs:1;
		uint32_t pfs:2;
		uint32_t mpss:3;
	} s;
	struct cvmx_pciercx_cfg029_s cn52xx;
	struct cvmx_pciercx_cfg029_s cn52xxp1;
	struct cvmx_pciercx_cfg029_s cn56xx;
	struct cvmx_pciercx_cfg029_s cn56xxp1;
};

union cvmx_pciercx_cfg030 {
	uint32_t u32;
	struct cvmx_pciercx_cfg030_s {
		uint32_t reserved_22_31:10;
		uint32_t tp:1;
		uint32_t ap_d:1;
		uint32_t ur_d:1;
		uint32_t fe_d:1;
		uint32_t nfe_d:1;
		uint32_t ce_d:1;
		uint32_t reserved_15_15:1;
		uint32_t mrrs:3;
		uint32_t ns_en:1;
		uint32_t ap_en:1;
		uint32_t pf_en:1;
		uint32_t etf_en:1;
		uint32_t mps:3;
		uint32_t ro_en:1;
		uint32_t ur_en:1;
		uint32_t fe_en:1;
		uint32_t nfe_en:1;
		uint32_t ce_en:1;
	} s;
	struct cvmx_pciercx_cfg030_s cn52xx;
	struct cvmx_pciercx_cfg030_s cn52xxp1;
	struct cvmx_pciercx_cfg030_s cn56xx;
	struct cvmx_pciercx_cfg030_s cn56xxp1;
};

union cvmx_pciercx_cfg031 {
	uint32_t u32;
	struct cvmx_pciercx_cfg031_s {
		uint32_t pnum:8;
		uint32_t reserved_22_23:2;
		uint32_t lbnc:1;
		uint32_t dllarc:1;
		uint32_t sderc:1;
		uint32_t cpm:1;
		uint32_t l1el:3;
		uint32_t l0el:3;
		uint32_t aslpms:2;
		uint32_t mlw:6;
		uint32_t mls:4;
	} s;
	struct cvmx_pciercx_cfg031_s cn52xx;
	struct cvmx_pciercx_cfg031_s cn52xxp1;
	struct cvmx_pciercx_cfg031_s cn56xx;
	struct cvmx_pciercx_cfg031_s cn56xxp1;
};

union cvmx_pciercx_cfg032 {
	uint32_t u32;
	struct cvmx_pciercx_cfg032_s {
		uint32_t lab:1;
		uint32_t lbm:1;
		uint32_t dlla:1;
		uint32_t scc:1;
		uint32_t lt:1;
		uint32_t reserved_26_26:1;
		uint32_t nlw:6;
		uint32_t ls:4;
		uint32_t reserved_12_15:4;
		uint32_t lab_int_enb:1;
		uint32_t lbm_int_enb:1;
		uint32_t hawd:1;
		uint32_t ecpm:1;
		uint32_t es:1;
		uint32_t ccc:1;
		uint32_t rl:1;
		uint32_t ld:1;
		uint32_t rcb:1;
		uint32_t reserved_2_2:1;
		uint32_t aslpc:2;
	} s;
	struct cvmx_pciercx_cfg032_s cn52xx;
	struct cvmx_pciercx_cfg032_s cn52xxp1;
	struct cvmx_pciercx_cfg032_s cn56xx;
	struct cvmx_pciercx_cfg032_s cn56xxp1;
};

union cvmx_pciercx_cfg033 {
	uint32_t u32;
	struct cvmx_pciercx_cfg033_s {
		uint32_t ps_num:13;
		uint32_t nccs:1;
		uint32_t emip:1;
		uint32_t sp_ls:2;
		uint32_t sp_lv:8;
		uint32_t hp_c:1;
		uint32_t hp_s:1;
		uint32_t pip:1;
		uint32_t aip:1;
		uint32_t mrlsp:1;
		uint32_t pcp:1;
		uint32_t abp:1;
	} s;
	struct cvmx_pciercx_cfg033_s cn52xx;
	struct cvmx_pciercx_cfg033_s cn52xxp1;
	struct cvmx_pciercx_cfg033_s cn56xx;
	struct cvmx_pciercx_cfg033_s cn56xxp1;
};

union cvmx_pciercx_cfg034 {
	uint32_t u32;
	struct cvmx_pciercx_cfg034_s {
		uint32_t reserved_25_31:7;
		uint32_t dlls_c:1;
		uint32_t emis:1;
		uint32_t pds:1;
		uint32_t mrlss:1;
		uint32_t ccint_d:1;
		uint32_t pd_c:1;
		uint32_t mrls_c:1;
		uint32_t pf_d:1;
		uint32_t abp_d:1;
		uint32_t reserved_13_15:3;
		uint32_t dlls_en:1;
		uint32_t emic:1;
		uint32_t pcc:1;
		uint32_t pic:2;
		uint32_t aic:2;
		uint32_t hpint_en:1;
		uint32_t ccint_en:1;
		uint32_t pd_en:1;
		uint32_t mrls_en:1;
		uint32_t pf_en:1;
		uint32_t abp_en:1;
	} s;
	struct cvmx_pciercx_cfg034_s cn52xx;
	struct cvmx_pciercx_cfg034_s cn52xxp1;
	struct cvmx_pciercx_cfg034_s cn56xx;
	struct cvmx_pciercx_cfg034_s cn56xxp1;
};

union cvmx_pciercx_cfg035 {
	uint32_t u32;
	struct cvmx_pciercx_cfg035_s {
		uint32_t reserved_17_31:15;
		uint32_t crssv:1;
		uint32_t reserved_5_15:11;
		uint32_t crssve:1;
		uint32_t pmeie:1;
		uint32_t sefee:1;
		uint32_t senfee:1;
		uint32_t secee:1;
	} s;
	struct cvmx_pciercx_cfg035_s cn52xx;
	struct cvmx_pciercx_cfg035_s cn52xxp1;
	struct cvmx_pciercx_cfg035_s cn56xx;
	struct cvmx_pciercx_cfg035_s cn56xxp1;
};

union cvmx_pciercx_cfg036 {
	uint32_t u32;
	struct cvmx_pciercx_cfg036_s {
		uint32_t reserved_18_31:14;
		uint32_t pme_pend:1;
		uint32_t pme_stat:1;
		uint32_t pme_rid:16;
	} s;
	struct cvmx_pciercx_cfg036_s cn52xx;
	struct cvmx_pciercx_cfg036_s cn52xxp1;
	struct cvmx_pciercx_cfg036_s cn56xx;
	struct cvmx_pciercx_cfg036_s cn56xxp1;
};

union cvmx_pciercx_cfg037 {
	uint32_t u32;
	struct cvmx_pciercx_cfg037_s {
		uint32_t reserved_5_31:27;
		uint32_t ctds:1;
		uint32_t ctrs:4;
	} s;
	struct cvmx_pciercx_cfg037_s cn52xx;
	struct cvmx_pciercx_cfg037_s cn52xxp1;
	struct cvmx_pciercx_cfg037_s cn56xx;
	struct cvmx_pciercx_cfg037_s cn56xxp1;
};

union cvmx_pciercx_cfg038 {
	uint32_t u32;
	struct cvmx_pciercx_cfg038_s {
		uint32_t reserved_5_31:27;
		uint32_t ctd:1;
		uint32_t ctv:4;
	} s;
	struct cvmx_pciercx_cfg038_s cn52xx;
	struct cvmx_pciercx_cfg038_s cn52xxp1;
	struct cvmx_pciercx_cfg038_s cn56xx;
	struct cvmx_pciercx_cfg038_s cn56xxp1;
};

union cvmx_pciercx_cfg039 {
	uint32_t u32;
	struct cvmx_pciercx_cfg039_s {
		uint32_t reserved_0_31:32;
	} s;
	struct cvmx_pciercx_cfg039_s cn52xx;
	struct cvmx_pciercx_cfg039_s cn52xxp1;
	struct cvmx_pciercx_cfg039_s cn56xx;
	struct cvmx_pciercx_cfg039_s cn56xxp1;
};

union cvmx_pciercx_cfg040 {
	uint32_t u32;
	struct cvmx_pciercx_cfg040_s {
		uint32_t reserved_0_31:32;
	} s;
	struct cvmx_pciercx_cfg040_s cn52xx;
	struct cvmx_pciercx_cfg040_s cn52xxp1;
	struct cvmx_pciercx_cfg040_s cn56xx;
	struct cvmx_pciercx_cfg040_s cn56xxp1;
};

union cvmx_pciercx_cfg041 {
	uint32_t u32;
	struct cvmx_pciercx_cfg041_s {
		uint32_t reserved_0_31:32;
	} s;
	struct cvmx_pciercx_cfg041_s cn52xx;
	struct cvmx_pciercx_cfg041_s cn52xxp1;
	struct cvmx_pciercx_cfg041_s cn56xx;
	struct cvmx_pciercx_cfg041_s cn56xxp1;
};

union cvmx_pciercx_cfg042 {
	uint32_t u32;
	struct cvmx_pciercx_cfg042_s {
		uint32_t reserved_0_31:32;
	} s;
	struct cvmx_pciercx_cfg042_s cn52xx;
	struct cvmx_pciercx_cfg042_s cn52xxp1;
	struct cvmx_pciercx_cfg042_s cn56xx;
	struct cvmx_pciercx_cfg042_s cn56xxp1;
};

union cvmx_pciercx_cfg064 {
	uint32_t u32;
	struct cvmx_pciercx_cfg064_s {
		uint32_t nco:12;
		uint32_t cv:4;
		uint32_t pcieec:16;
	} s;
	struct cvmx_pciercx_cfg064_s cn52xx;
	struct cvmx_pciercx_cfg064_s cn52xxp1;
	struct cvmx_pciercx_cfg064_s cn56xx;
	struct cvmx_pciercx_cfg064_s cn56xxp1;
};

union cvmx_pciercx_cfg065 {
	uint32_t u32;
	struct cvmx_pciercx_cfg065_s {
		uint32_t reserved_21_31:11;
		uint32_t ures:1;
		uint32_t ecrces:1;
		uint32_t mtlps:1;
		uint32_t ros:1;
		uint32_t ucs:1;
		uint32_t cas:1;
		uint32_t cts:1;
		uint32_t fcpes:1;
		uint32_t ptlps:1;
		uint32_t reserved_6_11:6;
		uint32_t sdes:1;
		uint32_t dlpes:1;
		uint32_t reserved_0_3:4;
	} s;
	struct cvmx_pciercx_cfg065_s cn52xx;
	struct cvmx_pciercx_cfg065_s cn52xxp1;
	struct cvmx_pciercx_cfg065_s cn56xx;
	struct cvmx_pciercx_cfg065_s cn56xxp1;
};

union cvmx_pciercx_cfg066 {
	uint32_t u32;
	struct cvmx_pciercx_cfg066_s {
		uint32_t reserved_21_31:11;
		uint32_t urem:1;
		uint32_t ecrcem:1;
		uint32_t mtlpm:1;
		uint32_t rom:1;
		uint32_t ucm:1;
		uint32_t cam:1;
		uint32_t ctm:1;
		uint32_t fcpem:1;
		uint32_t ptlpm:1;
		uint32_t reserved_6_11:6;
		uint32_t sdem:1;
		uint32_t dlpem:1;
		uint32_t reserved_0_3:4;
	} s;
	struct cvmx_pciercx_cfg066_s cn52xx;
	struct cvmx_pciercx_cfg066_s cn52xxp1;
	struct cvmx_pciercx_cfg066_s cn56xx;
	struct cvmx_pciercx_cfg066_s cn56xxp1;
};

union cvmx_pciercx_cfg067 {
	uint32_t u32;
	struct cvmx_pciercx_cfg067_s {
		uint32_t reserved_21_31:11;
		uint32_t ures:1;
		uint32_t ecrces:1;
		uint32_t mtlps:1;
		uint32_t ros:1;
		uint32_t ucs:1;
		uint32_t cas:1;
		uint32_t cts:1;
		uint32_t fcpes:1;
		uint32_t ptlps:1;
		uint32_t reserved_6_11:6;
		uint32_t sdes:1;
		uint32_t dlpes:1;
		uint32_t reserved_0_3:4;
	} s;
	struct cvmx_pciercx_cfg067_s cn52xx;
	struct cvmx_pciercx_cfg067_s cn52xxp1;
	struct cvmx_pciercx_cfg067_s cn56xx;
	struct cvmx_pciercx_cfg067_s cn56xxp1;
};

union cvmx_pciercx_cfg068 {
	uint32_t u32;
	struct cvmx_pciercx_cfg068_s {
		uint32_t reserved_14_31:18;
		uint32_t anfes:1;
		uint32_t rtts:1;
		uint32_t reserved_9_11:3;
		uint32_t rnrs:1;
		uint32_t bdllps:1;
		uint32_t btlps:1;
		uint32_t reserved_1_5:5;
		uint32_t res:1;
	} s;
	struct cvmx_pciercx_cfg068_s cn52xx;
	struct cvmx_pciercx_cfg068_s cn52xxp1;
	struct cvmx_pciercx_cfg068_s cn56xx;
	struct cvmx_pciercx_cfg068_s cn56xxp1;
};

union cvmx_pciercx_cfg069 {
	uint32_t u32;
	struct cvmx_pciercx_cfg069_s {
		uint32_t reserved_14_31:18;
		uint32_t anfem:1;
		uint32_t rttm:1;
		uint32_t reserved_9_11:3;
		uint32_t rnrm:1;
		uint32_t bdllpm:1;
		uint32_t btlpm:1;
		uint32_t reserved_1_5:5;
		uint32_t rem:1;
	} s;
	struct cvmx_pciercx_cfg069_s cn52xx;
	struct cvmx_pciercx_cfg069_s cn52xxp1;
	struct cvmx_pciercx_cfg069_s cn56xx;
	struct cvmx_pciercx_cfg069_s cn56xxp1;
};

union cvmx_pciercx_cfg070 {
	uint32_t u32;
	struct cvmx_pciercx_cfg070_s {
		uint32_t reserved_9_31:23;
		uint32_t ce:1;
		uint32_t cc:1;
		uint32_t ge:1;
		uint32_t gc:1;
		uint32_t fep:5;
	} s;
	struct cvmx_pciercx_cfg070_s cn52xx;
	struct cvmx_pciercx_cfg070_s cn52xxp1;
	struct cvmx_pciercx_cfg070_s cn56xx;
	struct cvmx_pciercx_cfg070_s cn56xxp1;
};

union cvmx_pciercx_cfg071 {
	uint32_t u32;
	struct cvmx_pciercx_cfg071_s {
		uint32_t dword1:32;
	} s;
	struct cvmx_pciercx_cfg071_s cn52xx;
	struct cvmx_pciercx_cfg071_s cn52xxp1;
	struct cvmx_pciercx_cfg071_s cn56xx;
	struct cvmx_pciercx_cfg071_s cn56xxp1;
};

union cvmx_pciercx_cfg072 {
	uint32_t u32;
	struct cvmx_pciercx_cfg072_s {
		uint32_t dword2:32;
	} s;
	struct cvmx_pciercx_cfg072_s cn52xx;
	struct cvmx_pciercx_cfg072_s cn52xxp1;
	struct cvmx_pciercx_cfg072_s cn56xx;
	struct cvmx_pciercx_cfg072_s cn56xxp1;
};

union cvmx_pciercx_cfg073 {
	uint32_t u32;
	struct cvmx_pciercx_cfg073_s {
		uint32_t dword3:32;
	} s;
	struct cvmx_pciercx_cfg073_s cn52xx;
	struct cvmx_pciercx_cfg073_s cn52xxp1;
	struct cvmx_pciercx_cfg073_s cn56xx;
	struct cvmx_pciercx_cfg073_s cn56xxp1;
};

union cvmx_pciercx_cfg074 {
	uint32_t u32;
	struct cvmx_pciercx_cfg074_s {
		uint32_t dword4:32;
	} s;
	struct cvmx_pciercx_cfg074_s cn52xx;
	struct cvmx_pciercx_cfg074_s cn52xxp1;
	struct cvmx_pciercx_cfg074_s cn56xx;
	struct cvmx_pciercx_cfg074_s cn56xxp1;
};

union cvmx_pciercx_cfg075 {
	uint32_t u32;
	struct cvmx_pciercx_cfg075_s {
		uint32_t reserved_3_31:29;
		uint32_t fere:1;
		uint32_t nfere:1;
		uint32_t cere:1;
	} s;
	struct cvmx_pciercx_cfg075_s cn52xx;
	struct cvmx_pciercx_cfg075_s cn52xxp1;
	struct cvmx_pciercx_cfg075_s cn56xx;
	struct cvmx_pciercx_cfg075_s cn56xxp1;
};

union cvmx_pciercx_cfg076 {
	uint32_t u32;
	struct cvmx_pciercx_cfg076_s {
		uint32_t aeimn:5;
		uint32_t reserved_7_26:20;
		uint32_t femr:1;
		uint32_t nfemr:1;
		uint32_t fuf:1;
		uint32_t multi_efnfr:1;
		uint32_t efnfr:1;
		uint32_t multi_ecr:1;
		uint32_t ecr:1;
	} s;
	struct cvmx_pciercx_cfg076_s cn52xx;
	struct cvmx_pciercx_cfg076_s cn52xxp1;
	struct cvmx_pciercx_cfg076_s cn56xx;
	struct cvmx_pciercx_cfg076_s cn56xxp1;
};

union cvmx_pciercx_cfg077 {
	uint32_t u32;
	struct cvmx_pciercx_cfg077_s {
		uint32_t efnfsi:16;
		uint32_t ecsi:16;
	} s;
	struct cvmx_pciercx_cfg077_s cn52xx;
	struct cvmx_pciercx_cfg077_s cn52xxp1;
	struct cvmx_pciercx_cfg077_s cn56xx;
	struct cvmx_pciercx_cfg077_s cn56xxp1;
};

union cvmx_pciercx_cfg448 {
	uint32_t u32;
	struct cvmx_pciercx_cfg448_s {
		uint32_t rtl:16;
		uint32_t rtltl:16;
	} s;
	struct cvmx_pciercx_cfg448_s cn52xx;
	struct cvmx_pciercx_cfg448_s cn52xxp1;
	struct cvmx_pciercx_cfg448_s cn56xx;
	struct cvmx_pciercx_cfg448_s cn56xxp1;
};

union cvmx_pciercx_cfg449 {
	uint32_t u32;
	struct cvmx_pciercx_cfg449_s {
		uint32_t omr:32;
	} s;
	struct cvmx_pciercx_cfg449_s cn52xx;
	struct cvmx_pciercx_cfg449_s cn52xxp1;
	struct cvmx_pciercx_cfg449_s cn56xx;
	struct cvmx_pciercx_cfg449_s cn56xxp1;
};

union cvmx_pciercx_cfg450 {
	uint32_t u32;
	struct cvmx_pciercx_cfg450_s {
		uint32_t lpec:8;
		uint32_t reserved_22_23:2;
		uint32_t link_state:6;
		uint32_t force_link:1;
		uint32_t reserved_8_14:7;
		uint32_t link_num:8;
	} s;
	struct cvmx_pciercx_cfg450_s cn52xx;
	struct cvmx_pciercx_cfg450_s cn52xxp1;
	struct cvmx_pciercx_cfg450_s cn56xx;
	struct cvmx_pciercx_cfg450_s cn56xxp1;
};

union cvmx_pciercx_cfg451 {
	uint32_t u32;
	struct cvmx_pciercx_cfg451_s {
		uint32_t reserved_30_31:2;
		uint32_t l1el:3;
		uint32_t l0el:3;
		uint32_t n_fts_cc:8;
		uint32_t n_fts:8;
		uint32_t ack_freq:8;
	} s;
	struct cvmx_pciercx_cfg451_s cn52xx;
	struct cvmx_pciercx_cfg451_s cn52xxp1;
	struct cvmx_pciercx_cfg451_s cn56xx;
	struct cvmx_pciercx_cfg451_s cn56xxp1;
};

union cvmx_pciercx_cfg452 {
	uint32_t u32;
	struct cvmx_pciercx_cfg452_s {
		uint32_t reserved_26_31:6;
		uint32_t eccrc:1;
		uint32_t reserved_22_24:3;
		uint32_t lme:6;
		uint32_t reserved_8_15:8;
		uint32_t flm:1;
		uint32_t reserved_6_6:1;
		uint32_t dllle:1;
		uint32_t reserved_4_4:1;
		uint32_t ra:1;
		uint32_t le:1;
		uint32_t sd:1;
		uint32_t omr:1;
	} s;
	struct cvmx_pciercx_cfg452_s cn52xx;
	struct cvmx_pciercx_cfg452_s cn52xxp1;
	struct cvmx_pciercx_cfg452_s cn56xx;
	struct cvmx_pciercx_cfg452_s cn56xxp1;
};

union cvmx_pciercx_cfg453 {
	uint32_t u32;
	struct cvmx_pciercx_cfg453_s {
		uint32_t dlld:1;
		uint32_t reserved_26_30:5;
		uint32_t ack_nak:1;
		uint32_t fcd:1;
		uint32_t ilst:24;
	} s;
	struct cvmx_pciercx_cfg453_s cn52xx;
	struct cvmx_pciercx_cfg453_s cn52xxp1;
	struct cvmx_pciercx_cfg453_s cn56xx;
	struct cvmx_pciercx_cfg453_s cn56xxp1;
};

union cvmx_pciercx_cfg454 {
	uint32_t u32;
	struct cvmx_pciercx_cfg454_s {
		uint32_t reserved_29_31:3;
		uint32_t tmfcwt:5;
		uint32_t tmanlt:5;
		uint32_t tmrt:5;
		uint32_t reserved_11_13:3;
		uint32_t nskps:3;
		uint32_t reserved_4_7:4;
		uint32_t ntss:4;
	} s;
	struct cvmx_pciercx_cfg454_s cn52xx;
	struct cvmx_pciercx_cfg454_s cn52xxp1;
	struct cvmx_pciercx_cfg454_s cn56xx;
	struct cvmx_pciercx_cfg454_s cn56xxp1;
};

union cvmx_pciercx_cfg455 {
	uint32_t u32;
	struct cvmx_pciercx_cfg455_s {
		uint32_t m_cfg0_filt:1;
		uint32_t m_io_filt:1;
		uint32_t msg_ctrl:1;
		uint32_t m_cpl_ecrc_filt:1;
		uint32_t m_ecrc_filt:1;
		uint32_t m_cpl_len_err:1;
		uint32_t m_cpl_attr_err:1;
		uint32_t m_cpl_tc_err:1;
		uint32_t m_cpl_fun_err:1;
		uint32_t m_cpl_rid_err:1;
		uint32_t m_cpl_tag_err:1;
		uint32_t m_lk_filt:1;
		uint32_t m_cfg1_filt:1;
		uint32_t m_bar_match:1;
		uint32_t m_pois_filt:1;
		uint32_t m_fun:1;
		uint32_t dfcwt:1;
		uint32_t reserved_11_14:4;
		uint32_t skpiv:11;
	} s;
	struct cvmx_pciercx_cfg455_s cn52xx;
	struct cvmx_pciercx_cfg455_s cn52xxp1;
	struct cvmx_pciercx_cfg455_s cn56xx;
	struct cvmx_pciercx_cfg455_s cn56xxp1;
};

union cvmx_pciercx_cfg456 {
	uint32_t u32;
	struct cvmx_pciercx_cfg456_s {
		uint32_t reserved_2_31:30;
		uint32_t m_vend1_drp:1;
		uint32_t m_vend0_drp:1;
	} s;
	struct cvmx_pciercx_cfg456_s cn52xx;
	struct cvmx_pciercx_cfg456_s cn52xxp1;
	struct cvmx_pciercx_cfg456_s cn56xx;
	struct cvmx_pciercx_cfg456_s cn56xxp1;
};

union cvmx_pciercx_cfg458 {
	uint32_t u32;
	struct cvmx_pciercx_cfg458_s {
		uint32_t dbg_info_l32:32;
	} s;
	struct cvmx_pciercx_cfg458_s cn52xx;
	struct cvmx_pciercx_cfg458_s cn52xxp1;
	struct cvmx_pciercx_cfg458_s cn56xx;
	struct cvmx_pciercx_cfg458_s cn56xxp1;
};

union cvmx_pciercx_cfg459 {
	uint32_t u32;
	struct cvmx_pciercx_cfg459_s {
		uint32_t dbg_info_u32:32;
	} s;
	struct cvmx_pciercx_cfg459_s cn52xx;
	struct cvmx_pciercx_cfg459_s cn52xxp1;
	struct cvmx_pciercx_cfg459_s cn56xx;
	struct cvmx_pciercx_cfg459_s cn56xxp1;
};

union cvmx_pciercx_cfg460 {
	uint32_t u32;
	struct cvmx_pciercx_cfg460_s {
		uint32_t reserved_20_31:12;
		uint32_t tphfcc:8;
		uint32_t tpdfcc:12;
	} s;
	struct cvmx_pciercx_cfg460_s cn52xx;
	struct cvmx_pciercx_cfg460_s cn52xxp1;
	struct cvmx_pciercx_cfg460_s cn56xx;
	struct cvmx_pciercx_cfg460_s cn56xxp1;
};

union cvmx_pciercx_cfg461 {
	uint32_t u32;
	struct cvmx_pciercx_cfg461_s {
		uint32_t reserved_20_31:12;
		uint32_t tchfcc:8;
		uint32_t tcdfcc:12;
	} s;
	struct cvmx_pciercx_cfg461_s cn52xx;
	struct cvmx_pciercx_cfg461_s cn52xxp1;
	struct cvmx_pciercx_cfg461_s cn56xx;
	struct cvmx_pciercx_cfg461_s cn56xxp1;
};

union cvmx_pciercx_cfg462 {
	uint32_t u32;
	struct cvmx_pciercx_cfg462_s {
		uint32_t reserved_20_31:12;
		uint32_t tchfcc:8;
		uint32_t tcdfcc:12;
	} s;
	struct cvmx_pciercx_cfg462_s cn52xx;
	struct cvmx_pciercx_cfg462_s cn52xxp1;
	struct cvmx_pciercx_cfg462_s cn56xx;
	struct cvmx_pciercx_cfg462_s cn56xxp1;
};

union cvmx_pciercx_cfg463 {
	uint32_t u32;
	struct cvmx_pciercx_cfg463_s {
		uint32_t reserved_3_31:29;
		uint32_t rqne:1;
		uint32_t trbne:1;
		uint32_t rtlpfccnr:1;
	} s;
	struct cvmx_pciercx_cfg463_s cn52xx;
	struct cvmx_pciercx_cfg463_s cn52xxp1;
	struct cvmx_pciercx_cfg463_s cn56xx;
	struct cvmx_pciercx_cfg463_s cn56xxp1;
};

union cvmx_pciercx_cfg464 {
	uint32_t u32;
	struct cvmx_pciercx_cfg464_s {
		uint32_t wrr_vc3:8;
		uint32_t wrr_vc2:8;
		uint32_t wrr_vc1:8;
		uint32_t wrr_vc0:8;
	} s;
	struct cvmx_pciercx_cfg464_s cn52xx;
	struct cvmx_pciercx_cfg464_s cn52xxp1;
	struct cvmx_pciercx_cfg464_s cn56xx;
	struct cvmx_pciercx_cfg464_s cn56xxp1;
};

union cvmx_pciercx_cfg465 {
	uint32_t u32;
	struct cvmx_pciercx_cfg465_s {
		uint32_t wrr_vc7:8;
		uint32_t wrr_vc6:8;
		uint32_t wrr_vc5:8;
		uint32_t wrr_vc4:8;
	} s;
	struct cvmx_pciercx_cfg465_s cn52xx;
	struct cvmx_pciercx_cfg465_s cn52xxp1;
	struct cvmx_pciercx_cfg465_s cn56xx;
	struct cvmx_pciercx_cfg465_s cn56xxp1;
};

union cvmx_pciercx_cfg466 {
	uint32_t u32;
	struct cvmx_pciercx_cfg466_s {
		uint32_t rx_queue_order:1;
		uint32_t type_ordering:1;
		uint32_t reserved_24_29:6;
		uint32_t queue_mode:3;
		uint32_t reserved_20_20:1;
		uint32_t header_credits:8;
		uint32_t data_credits:12;
	} s;
	struct cvmx_pciercx_cfg466_s cn52xx;
	struct cvmx_pciercx_cfg466_s cn52xxp1;
	struct cvmx_pciercx_cfg466_s cn56xx;
	struct cvmx_pciercx_cfg466_s cn56xxp1;
};

union cvmx_pciercx_cfg467 {
	uint32_t u32;
	struct cvmx_pciercx_cfg467_s {
		uint32_t reserved_24_31:8;
		uint32_t queue_mode:3;
		uint32_t reserved_20_20:1;
		uint32_t header_credits:8;
		uint32_t data_credits:12;
	} s;
	struct cvmx_pciercx_cfg467_s cn52xx;
	struct cvmx_pciercx_cfg467_s cn52xxp1;
	struct cvmx_pciercx_cfg467_s cn56xx;
	struct cvmx_pciercx_cfg467_s cn56xxp1;
};

union cvmx_pciercx_cfg468 {
	uint32_t u32;
	struct cvmx_pciercx_cfg468_s {
		uint32_t reserved_24_31:8;
		uint32_t queue_mode:3;
		uint32_t reserved_20_20:1;
		uint32_t header_credits:8;
		uint32_t data_credits:12;
	} s;
	struct cvmx_pciercx_cfg468_s cn52xx;
	struct cvmx_pciercx_cfg468_s cn52xxp1;
	struct cvmx_pciercx_cfg468_s cn56xx;
	struct cvmx_pciercx_cfg468_s cn56xxp1;
};

union cvmx_pciercx_cfg490 {
	uint32_t u32;
	struct cvmx_pciercx_cfg490_s {
		uint32_t reserved_26_31:6;
		uint32_t header_depth:10;
		uint32_t reserved_14_15:2;
		uint32_t data_depth:14;
	} s;
	struct cvmx_pciercx_cfg490_s cn52xx;
	struct cvmx_pciercx_cfg490_s cn52xxp1;
	struct cvmx_pciercx_cfg490_s cn56xx;
	struct cvmx_pciercx_cfg490_s cn56xxp1;
};

union cvmx_pciercx_cfg491 {
	uint32_t u32;
	struct cvmx_pciercx_cfg491_s {
		uint32_t reserved_26_31:6;
		uint32_t header_depth:10;
		uint32_t reserved_14_15:2;
		uint32_t data_depth:14;
	} s;
	struct cvmx_pciercx_cfg491_s cn52xx;
	struct cvmx_pciercx_cfg491_s cn52xxp1;
	struct cvmx_pciercx_cfg491_s cn56xx;
	struct cvmx_pciercx_cfg491_s cn56xxp1;
};

union cvmx_pciercx_cfg492 {
	uint32_t u32;
	struct cvmx_pciercx_cfg492_s {
		uint32_t reserved_26_31:6;
		uint32_t header_depth:10;
		uint32_t reserved_14_15:2;
		uint32_t data_depth:14;
	} s;
	struct cvmx_pciercx_cfg492_s cn52xx;
	struct cvmx_pciercx_cfg492_s cn52xxp1;
	struct cvmx_pciercx_cfg492_s cn56xx;
	struct cvmx_pciercx_cfg492_s cn56xxp1;
};

union cvmx_pciercx_cfg516 {
	uint32_t u32;
	struct cvmx_pciercx_cfg516_s {
		uint32_t phy_stat:32;
	} s;
	struct cvmx_pciercx_cfg516_s cn52xx;
	struct cvmx_pciercx_cfg516_s cn52xxp1;
	struct cvmx_pciercx_cfg516_s cn56xx;
	struct cvmx_pciercx_cfg516_s cn56xxp1;
};

union cvmx_pciercx_cfg517 {
	uint32_t u32;
	struct cvmx_pciercx_cfg517_s {
		uint32_t phy_ctrl:32;
	} s;
	struct cvmx_pciercx_cfg517_s cn52xx;
	struct cvmx_pciercx_cfg517_s cn52xxp1;
	struct cvmx_pciercx_cfg517_s cn56xx;
	struct cvmx_pciercx_cfg517_s cn56xxp1;
};

#endif
