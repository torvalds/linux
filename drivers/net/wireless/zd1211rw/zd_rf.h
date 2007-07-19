/* zd_rf.h
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#ifndef _ZD_RF_H
#define _ZD_RF_H

#define UW2451_RF			0x2
#define UCHIP_RF			0x3
#define AL2230_RF			0x4
#define AL7230B_RF			0x5	/* a,b,g */
#define THETA_RF			0x6
#define AL2210_RF			0x7
#define MAXIM_NEW_RF			0x8
#define UW2453_RF			0x9
#define AL2230S_RF			0xa
#define RALINK_RF			0xb
#define INTERSIL_RF			0xc
#define RF2959_RF			0xd
#define MAXIM_NEW2_RF			0xe
#define PHILIPS_RF			0xf

#define RF_CHANNEL(ch) [(ch)-1]

/* Provides functions of the RF transceiver. */

enum {
	RF_REG_BITS = 6,
	RF_VALUE_BITS = 18,
	RF_RV_BITS = RF_REG_BITS + RF_VALUE_BITS,
};

struct zd_rf {
	u8 type;

	u8 channel;

	/* whether channel integration and calibration should be updated
	 * defaults to 1 (yes) */
	u8 update_channel_int:1;

	/* whether CR47 should be patched from the EEPROM, if the appropriate
	 * flag is set in the POD. The vendor driver suggests that this should
	 * be done for all RF's, but a bug in their code prevents but their
	 * HW_OverWritePhyRegFromE2P() routine from ever taking effect. */
	u8 patch_cck_gain:1;

	/* private RF driver data */
	void *priv;

	/* RF-specific functions */
	int (*init_hw)(struct zd_rf *rf);
	int (*set_channel)(struct zd_rf *rf, u8 channel);
	int (*switch_radio_on)(struct zd_rf *rf);
	int (*switch_radio_off)(struct zd_rf *rf);
	int (*patch_6m_band_edge)(struct zd_rf *rf, u8 channel);
	void (*clear)(struct zd_rf *rf);
};

const char *zd_rf_name(u8 type);
void zd_rf_init(struct zd_rf *rf);
void zd_rf_clear(struct zd_rf *rf);
int zd_rf_init_hw(struct zd_rf *rf, u8 type);

int zd_rf_scnprint_id(struct zd_rf *rf, char *buffer, size_t size);

int zd_rf_set_channel(struct zd_rf *rf, u8 channel);

int zd_switch_radio_on(struct zd_rf *rf);
int zd_switch_radio_off(struct zd_rf *rf);

int zd_rf_patch_6m_band_edge(struct zd_rf *rf, u8 channel);
int zd_rf_generic_patch_6m(struct zd_rf *rf, u8 channel);

static inline int zd_rf_should_update_pwr_int(struct zd_rf *rf)
{
	return rf->update_channel_int;
}

static inline int zd_rf_should_patch_cck_gain(struct zd_rf *rf)
{
	return rf->patch_cck_gain;
}

int zd_rf_patch_6m_band_edge(struct zd_rf *rf, u8 channel);
int zd_rf_generic_patch_6m(struct zd_rf *rf, u8 channel);

/* Functions for individual RF chips */

int zd_rf_init_rf2959(struct zd_rf *rf);
int zd_rf_init_al2230(struct zd_rf *rf);
int zd_rf_init_al7230b(struct zd_rf *rf);
int zd_rf_init_uw2453(struct zd_rf *rf);

#endif /* _ZD_RF_H */
