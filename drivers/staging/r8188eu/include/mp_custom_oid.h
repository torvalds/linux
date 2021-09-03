/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

#ifndef	__CUSTOM_OID_H
#define __CUSTOM_OID_H

/*  by Owen */
/*  0xFF818000 - 0xFF81802F		RTL8180 Mass Production Kit */
/*  0xFF818500 - 0xFF81850F		RTL8185 Setup Utility */
/*  0xFF818580 - 0xFF81858F		RTL8185 Phy Status Utility */

/*  */

/*  by Owen for Production Kit */
/*  For Production Kit with Agilent Equipments */
/*  in order to make our custom oids hopefully somewhat unique */
/*  we will use 0xFF (indicating implementation specific OID) */
/*	81(first byte of non zero Realtek unique identifier) */
/*	80 (second byte of non zero Realtek unique identifier) */
/*	XX (the custom OID number - providing 255 possible custom oids) */

#define OID_RT_PRO_SET_DATA_RATE			0xFF818001
#define OID_RT_PRO_START_TEST				0xFF818002
#define OID_RT_PRO_STOP_TEST				0xFF818003
#define OID_RT_PRO_SET_CHANNEL_DIRECT_CALL		0xFF818008

#define OID_RT_PRO_SET_ANTENNA_BB			0xFF81800E
#define OID_RT_PRO_SET_TX_POWER_CONTROL			0xFF818011

#define OID_RT_PRO_SET_CONTINUOUS_TX			0xFF81800B
#define OID_RT_PRO_SET_SINGLE_CARRIER_TX		0xFF81800C
#define OID_RT_PRO_SET_CARRIER_SUPPRESSION_TX		0xFF81802B
#define OID_RT_PRO_SET_SINGLE_TONE_TX			0xFF818043

#define OID_RT_PRO_RF_WRITE_REGISTRY			0xFF0111C8
#define OID_RT_PRO_RF_READ_REGISTRY			0xFF0111C9

#define OID_RT_PRO_WRITE_BB_REG				0xFF818781
#define OID_RT_PRO_READ_BB_REG				0xFF818782

#define OID_RT_PRO_READ_REGISTER			0xFF871101 /* Q */
#define OID_RT_PRO_WRITE_REGISTER			0xFF871102 /* S */

#define OID_RT_PRO_SET_POWER_TRACKING			0xFF871124 /* S */

#define OID_RT_GET_EFUSE_CURRENT_SIZE			0xFF871208 /* Q */

#define OID_RT_SET_BANDWIDTH				0xFF871209 /* S */

#define OID_RT_SET_RX_PACKET_TYPE			0xFF87120B /* S */

#define OID_RT_GET_EFUSE_MAX_SIZE			0xFF87120C /* Q */

#define OID_RT_PRO_GET_THERMAL_METER			0xFF871210 /* Q */

#define OID_RT_RESET_PHY_RX_PACKET_COUNT		0xFF871211 /* S */
#define OID_RT_GET_PHY_RX_PACKET_RECEIVED		0xFF871212 /* Q */
#define OID_RT_GET_PHY_RX_PACKET_CRC32_ERROR		0xFF871213 /* Q */

#define OID_RT_SET_POWER_DOWN				0xFF871214 /* S */

#define OID_RT_PRO_EFUSE				0xFF871216 /* Q, S */
#define OID_RT_PRO_EFUSE_MAP				0xFF871217 /* Q, S */

#endif /* ifndef	__CUSTOM_OID_H */
