/******************************************************************************
 *
 * Copyright(c) 2013 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/

#error "You have defined CONFIG_CUSTOMIZED_COUNTRY_CHPLAN_MAP to use a customized map of your own instead of the default one"
#error "Before removing these error notifications, please make sure regulatory certification requirements of your target markets"

static const struct country_chplan CUSTOMIZED_country_chplan_map[] = {
	COUNTRY_CHPLAN_ENT("TW", 0x39, 1, 0xFF), /* Taiwan */
};

static const u16 CUSTOMIZED_country_chplan_map_sz = sizeof(CUSTOMIZED_country_chplan_map) / sizeof(struct country_chplan);
