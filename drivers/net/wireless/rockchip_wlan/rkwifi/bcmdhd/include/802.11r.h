/*
 * Copyright (C) 2020, Broadcom.
 *
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 *
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 *
 *
 * <<Broadcom-WL-IPTag/Dual:>>
 *
 * Fundamental constants relating to 802.11r
 */

#ifndef _802_11r_H_
#define _802_11r_H_

#define FBT_R0KH_ID_LEN 49 /* includes null termination */
#define FBT_REASSOC_TIME_DEF	1000

#define DOT11_FBT_SUBELEM_ID_R1KH_ID		1
#define DOT11_FBT_SUBELEM_ID_GTK		2
#define DOT11_FBT_SUBELEM_ID_R0KH_ID		3
#define DOT11_FBT_SUBELEM_ID_IGTK		4
#define DOT11_FBT_SUBELEM_ID_OCI		5u

/*
* FBT Subelement id lenths
*/

#define DOT11_FBT_SUBELEM_R1KH_LEN		6
/* GTK_FIXED_LEN = Key_Info (2Bytes) + Key_Length (1Byte) + RSC (8Bytes) */
#define DOT11_FBT_SUBELEM_GTK_FIXED_LEN		11
/* GTK_MIN_LEN = GTK_FIXED_LEN + key (min 16 Bytes) + key_wrap (8Bytes) */
#define DOT11_FBT_SUBELEM_GTK_MIN_LEN		(DOT11_FBT_SUBELEM_GTK_FIXED_LEN + 24)
/* GTK_MAX_LEN = GTK_FIXED_LEN + key (max 32 Bytes) + key_wrap (8Bytes) */
#define DOT11_FBT_SUBELEM_GTK_MAX_LEN		(DOT11_FBT_SUBELEM_GTK_FIXED_LEN + 40)
#define DOT11_FBT_SUBELEM_R0KH_MIN_LEN		1
#define DOT11_FBT_SUBELEM_R0KH_MAX_LEN		48
/* IGTK_LEN = KeyID (2Bytes) + IPN (6Bytes) + Key_Length (1Byte) +
*		Wrapped_Key (key (16Bytes) + key_wrap (8Bytes))
*/
#define DOT11_FBT_SUBELEM_IGTK_LEN		33
#define DOT11_FBT_SUBELEM_OCI_LEN		3u

#endif	/* #ifndef _802_11r_H_ */
