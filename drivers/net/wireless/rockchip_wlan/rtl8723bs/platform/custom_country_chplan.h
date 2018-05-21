/******************************************************************************
 *
 * Copyright(c) 2013 - 2017 Realtek Corporation.
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
 *****************************************************************************/

#error "You have defined CONFIG_CUSTOMIZED_COUNTRY_CHPLAN_MAP to use a customized map of your own instead of the default one"
#error "Before removing these error notifications, please make sure regulatory certification requirements of your target markets"

static const struct country_chplan CUSTOMIZED_country_chplan_map[] = {
	COUNTRY_CHPLAN_ENT("TW", 0x76, 1, 0x3FF), /* Taiwan */
};

