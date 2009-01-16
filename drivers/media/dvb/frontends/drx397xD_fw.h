/*
 * Firmware definitions for Micronas drx397xD
 *
 * Copyright (C) 2007 Henk Vergonet <Henk.Vergonet@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; If not, see <http://www.gnu.org/licenses/>.
 */

#ifdef _FW_ENTRY
	_FW_ENTRY("drx397xD.A2.fw",	DRXD_FW_A2 = 0,	DRXD_FW_A2	),
	_FW_ENTRY("drx397xD.B1.fw",	DRXD_FW_B1,	DRXD_FW_B1	),
#undef _FW_ENTRY
#endif /* _FW_ENTRY */

#ifdef _BLOB_ENTRY
	_BLOB_ENTRY("InitAtomicRead",	DRXD_InitAtomicRead = 0	),
	_BLOB_ENTRY("InitCE",		DRXD_InitCE		),
	_BLOB_ENTRY("InitCP",		DRXD_InitCP		),
	_BLOB_ENTRY("InitEC",		DRXD_InitEC		),
	_BLOB_ENTRY("InitEQ",		DRXD_InitEQ		),
	_BLOB_ENTRY("InitFE_1",		DRXD_InitFE_1		),
	_BLOB_ENTRY("InitFE_2",		DRXD_InitFE_2		),
	_BLOB_ENTRY("InitFT",		DRXD_InitFT		),
	_BLOB_ENTRY("InitSC",		DRXD_InitSC		),
	_BLOB_ENTRY("ResetCEFR",	DRXD_ResetCEFR		),
	_BLOB_ENTRY("ResetECRAM",	DRXD_ResetECRAM		),
	_BLOB_ENTRY("microcode",	DRXD_microcode		),
#undef _BLOB_ENTRY
#endif /* _BLOB_ENTRY */
